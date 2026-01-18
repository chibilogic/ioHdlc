/*
 * ioHdlc Basic Exchange Test - Parametrized Version
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
/**
 * @file    test_basic_exchange.c
 * @brief   Parametrized HDLC test with real traffic and statistics.
 */

#include "../common/test_framework.h"
#include "../common/test_helpers.h"
#include "../common/test_arenas.h"
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcqueue.h"
#include "ioHdlcswdriver.h"
#include "ioHdlc_runner.h"
#include "ioHdlcfmempool.h"
#include "ioHdlcosal.h"
#include "../linux/mocks/mock_stream.h"
#include "../linux/mocks/mock_stream_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

#define PRIMARY_ADDR    0x01
#define SECONDARY_ADDR  0x02
#define WINDOW_SIZE     7
#define FRAME_SIZE      128
#define MAX_PACKET_SIZE 120  /* Limited by TYPE0 FFF (max 127 bytes frame) */

static volatile bool test_running_global = true;

/*===========================================================================*/
/* Signal Handler                                                            */
/*===========================================================================*/

static void sigint_handler(int sig) {
  (void)sig;
  test_running_global = false;
  printf("\n\nTest interrupted. Stopping...\n");
}

/*===========================================================================*/
/* Thread Functions                                                          */
/*===========================================================================*/

typedef struct {
  iohdlc_station_t *station;
  iohdlc_station_peer_t *peer;
  test_statistics_t *stats;
  pthread_mutex_t *stats_mutex;
  test_config_t *config;
  uint32_t seq;
  bool enabled;  /* Whether this thread should be active */
} thread_context_t;

/**
 * @brief Writer thread - only sends data
 */
static void *writer_thread(void *arg) {
  thread_context_t *ctx = (thread_context_t *)arg;
  uint8_t buffer[MAX_PACKET_SIZE + TEST_PACKET_HEADER_SIZE];
  uint32_t packets_sent = 0;
  uint32_t iterations = 0;
  uint32_t start_time = iohdlc_time_now_ms();
  volatile bool test_running = true;

  if (!ctx->enabled) {
    return NULL;  /* Thread not needed for this direction */
  }
  
  while (test_running) {
    /* Check duration */
    if (ctx->config->duration_type == TEST_BY_TIME) {
      uint32_t elapsed = (iohdlc_time_now_ms() - start_time) / 1000;
      if (elapsed >= ctx->config->duration_value) {
        break;
      }
    } else if (ctx->config->duration_type == TEST_BY_COUNT) {
      if (iterations >= ctx->config->duration_value) {
        break;
      }
    }
    
    /* Send burst of packets */
    while (packets_sent < ctx->config->exchanges_per_iteration && !IOHDLC_PEER_DISC(ctx->peer)) {
      size_t packet_size = test_generate_packet(ctx->seq++, 
                                                ctx->config->bytes_per_exchange,
                                                buffer, sizeof buffer);
      
      ssize_t sent = ioHdlcWriteTmo(ctx->peer, buffer, packet_size, 20000);
      if (sent >= (ssize_t)packet_size) {
        pthread_mutex_lock(ctx->stats_mutex);
        ctx->stats->packets_sent++;
        ctx->stats->total_bytes_sent += sent;
        pthread_mutex_unlock(ctx->stats_mutex);
        packets_sent++;
      } else {
        if (ctx->station->errorno == ETIMEDOUT)
          fprintf(stderr, "Writer %u timeout!\n", ctx->station->addr);
        else
          fprintf(stderr, "Writer %u Error %d!\n", ctx->station->addr,
            ctx->station->errorno);
        break;
      }
    }
    
    if (packets_sent >= ctx->config->exchanges_per_iteration) {
      packets_sent = 0;
      iterations++;
    }
    
    usleep(1);  /* Small yield */
  }
  fprintf(stderr, "Writer %u END (iters %d)!\n", ctx->station->addr, iterations);
  usleep(1000000);  /* Wait for any pending receptions */
  test_running_global = false;

  fprintf(stderr, "Writer Link down\n");
  ioHdlcStationLinkDown(ctx->station, ctx->peer->addr);

  return NULL;
}

/**
 * @brief Reader thread - only receives data
 */
static void *reader_thread(void *arg) {
  thread_context_t *ctx = (thread_context_t *)arg;
  uint8_t buffer[MAX_PACKET_SIZE + TEST_PACKET_HEADER_SIZE];
  volatile bool test_running = true;

  if (!ctx->enabled) {
    return NULL;  /* Thread not needed for this direction */
  }
  
  while (test_running) {
   
    ssize_t received = ioHdlcReadTmo(ctx->peer, buffer, ctx->config->bytes_per_exchange +
      TEST_PACKET_HEADER_SIZE, 20000);
    if (((ctx->seq+1) & 0xFF) == 0) {
      usleep(100000);
    }
    if (received > 0 && (size_t)received >= TEST_PACKET_HEADER_SIZE) {
      pthread_mutex_lock(ctx->stats_mutex);
      test_validate_packet(buffer, received, &ctx->seq, ctx->stats);
      pthread_mutex_unlock(ctx->stats_mutex);
    } else if (received > 0) {
      fprintf(stderr, "Warning: received short packet (%zd bytes)\n", received);
    } else if (received == 0) {
      test_running = false;  /* No data received, assume test end */
    } else {
      fprintf(stderr, "Reader timeout!\n");
    }
    if (ctx->stats->packets_received >=
        ctx->config->exchanges_per_iteration * ctx->config->duration_value &&
        ctx->config->duration_type == TEST_BY_COUNT) {
      test_running = false;  /* All data received, assume test end */
      break;
    }
  }

  usleep(1000000);  /* Wait for any pending receptions */
  test_running_global = false;
  fprintf(stderr, "Reader Link down\n");
  ioHdlcStationLinkDown(ctx->station, ctx->peer->addr);
    
  return NULL;
}

/*===========================================================================*/
/* Main Test                                                                 */
/*===========================================================================*/

int main(int argc, char **argv) {
  test_config_t config;
  test_statistics_t stats_primary, stats_secondary;
  pthread_mutex_t stats_mutex_primary = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t stats_mutex_secondary = PTHREAD_MUTEX_INITIALIZER;
  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  ioHdlcFrameMemPool pool_primary, pool_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t station_config;
  thread_context_t ctx_pri_writer, ctx_pri_reader, ctx_sec_writer, ctx_sec_reader;
  pthread_t thread_pri_writer, thread_pri_reader, thread_sec_writer, thread_sec_reader;
  uint8_t arena_primary[16384], arena_secondary[16384];
  int32_t result;
  uint32_t start_time, elapsed_time;
  
  /* Parse configuration */
  if (!test_parse_config(&config, argc, argv)) {
    return 1;
  }
  
  /* Enable HDLC logging if compiled in */
#if IOHDLC_LOG_LEVEL > 0
  extern bool iohdlc_log_enabled;
  iohdlc_log_enabled = true;
#endif
  
  /* Print configuration */
  printf("\n");
  test_print_config(&config);
  
  /* Initialize statistics */
  memset(&stats_primary, 0, sizeof stats_primary);
  memset(&stats_secondary, 0, sizeof stats_secondary);
  stats_primary.start_time_ms = iohdlc_time_now_ms();
  stats_secondary.start_time_ms = iohdlc_time_now_ms();
  
  /* Setup signal handler */
  signal(SIGINT, sigint_handler);
  
  printf("\n");
  printf("========================================\n");
  printf("Initializing HDLC stations...\n");
  printf("========================================\n\n");
  
  /* Create mock streams */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = false,
    .error_rate = 0,
    .delay_us = 100
  };
  
  stream_primary = mock_stream_create(&stream_config);
  stream_secondary = mock_stream_create(&stream_config);
  if (stream_primary == NULL || stream_secondary == NULL) {
    fprintf(stderr, "Failed to create mock streams\n");
    return 1;
  }
  mock_stream_connect(stream_primary, stream_secondary);
  
  /* Create adapters */
  adapter_primary = mock_stream_adapter_create(stream_primary);
  adapter_secondary = mock_stream_adapter_create(stream_secondary);
  if (adapter_primary == NULL || adapter_secondary == NULL) {
    fprintf(stderr, "Failed to create adapters\n");
    return 1;
  }
  
  ioHdlcStreamPort port_primary = mock_stream_adapter_get_port(adapter_primary);
  ioHdlcStreamPort port_secondary = mock_stream_adapter_get_port(adapter_secondary);
  
  /* Initialize drivers */
  ioHdlcSwDriverInit(&driver_primary);
  ioHdlcSwDriverInit(&driver_secondary);
  
  /* Initialize frame pools */
  fmpInit(&pool_primary, arena_primary, sizeof arena_primary, FRAME_SIZE, 8);
  fmpInit(&pool_secondary, arena_secondary, sizeof arena_secondary, FRAME_SIZE, 8);
  
  /* Configure primary station */
  /* Note: Using default optfuncs (NULL) which enables TYPE0 FFF (max 127 bytes frame) */
  station_config.mode = config.mode;
  station_config.flags = IOHDLC_FLG_PRI;
  station_config.log2mod = 3;
  station_config.addr = PRIMARY_ADDR;
  station_config.driver = (ioHdlcDriver *)&driver_primary;
  station_config.fpp = (ioHdlcFramePool *)&pool_primary;
  station_config.optfuncs = NULL;  /* Use defaults (TYPE0 FFF) */
  station_config.phydriver = &port_primary;
  station_config.phydriver_config = NULL;
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &station_config);
  if (result != 0) {
    fprintf(stderr, "Primary station init failed: %d\n", result);
    return 1;
  }
  
  /* Configure secondary station */
  station_config.mode = IOHDLC_OM_NDM;
  station_config.flags = 0;
  station_config.addr = SECONDARY_ADDR;
  station_config.driver = (ioHdlcDriver *)&driver_secondary;
  station_config.fpp = (ioHdlcFramePool *)&pool_secondary;
  station_config.phydriver = &port_secondary;
  
  memset(&station_secondary, 0, sizeof station_secondary);
  result = ioHdlcStationInit(&station_secondary, &station_config);
  if (result != 0) {
    fprintf(stderr, "Secondary station init failed: %d\n", result);
    return 1;
  }
  
  /* Add peers */
  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  if (result != 0) {
    fprintf(stderr, "Add peer to primary failed: %d\n", result);
    return 1;
  }
  
  result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
  if (result != 0) {
    fprintf(stderr, "Add peer to secondary failed: %d\n", result);
    return 1;
  }
  
  /* Start runners */
  printf("Starting HDLC protocol runners...\n");
  ioHdlcRunnerStart(&station_primary);
  ioHdlcRunnerStart(&station_secondary);
  usleep(50000);
  
  /* Establish connection */
  printf("Establishing connection...\n");
  result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, config.mode);
  if (result != 0) {
    fprintf(stderr, "Link up failed: %d\n", result);
    goto cleanup;
  }
  
  usleep(100000);
  
  if (IOHDLC_PEER_DISC(&peer_at_primary) || IOHDLC_PEER_DISC(&peer_at_secondary)) {
    fprintf(stderr, "Connection not established\n");
    goto cleanup;
  }
  
  printf("✅ Connection established\n\n");
  
  /* Prepare thread contexts - 4 threads: writer and reader for each station */
  
  /* Primary writer */
  ctx_pri_writer.station = &station_primary;
  ctx_pri_writer.peer = &peer_at_primary;
  ctx_pri_writer.stats = &stats_primary;
  ctx_pri_writer.stats_mutex = &stats_mutex_primary;
  ctx_pri_writer.config = &config;
  ctx_pri_writer.seq = 0;
  ctx_pri_writer.enabled = (config.traffic_direction == TRAFFIC_PRI_TO_SEC ||
                            config.traffic_direction == TRAFFIC_BIDIRECTIONAL);
  
  /* Primary reader */
  ctx_pri_reader.station = &station_primary;
  ctx_pri_reader.peer = &peer_at_primary;
  ctx_pri_reader.stats = &stats_primary;
  ctx_pri_reader.stats_mutex = &stats_mutex_primary;
  ctx_pri_reader.config = &config;
  ctx_pri_reader.seq = 0;
  ctx_pri_reader.enabled = (config.traffic_direction == TRAFFIC_SEC_TO_PRI ||
                            config.traffic_direction == TRAFFIC_BIDIRECTIONAL);
  
  /* Secondary writer */
  ctx_sec_writer.station = &station_secondary;
  ctx_sec_writer.peer = &peer_at_secondary;
  ctx_sec_writer.stats = &stats_secondary;
  ctx_sec_writer.stats_mutex = &stats_mutex_secondary;
  ctx_sec_writer.config = &config;
  ctx_sec_writer.seq = 0;
  ctx_sec_writer.enabled = (config.traffic_direction == TRAFFIC_SEC_TO_PRI ||
                            config.traffic_direction == TRAFFIC_BIDIRECTIONAL);
  
  /* Secondary reader */
  ctx_sec_reader.station = &station_secondary;
  ctx_sec_reader.peer = &peer_at_secondary;
  ctx_sec_reader.stats = &stats_secondary;
  ctx_sec_reader.stats_mutex = &stats_mutex_secondary;
  ctx_sec_reader.config = &config;
  ctx_sec_reader.seq = 0;
  ctx_sec_reader.enabled = (config.traffic_direction == TRAFFIC_PRI_TO_SEC ||
                            config.traffic_direction == TRAFFIC_BIDIRECTIONAL);
  
  printf("========================================\n");
  printf("Starting data exchange...\n");
  printf("========================================\n\n");
  
  /* Start 4 threads */
  start_time = iohdlc_time_now_ms();
  pthread_create(&thread_pri_writer, NULL, writer_thread, &ctx_pri_writer);
  pthread_create(&thread_pri_reader, NULL, reader_thread, &ctx_pri_reader);
  pthread_create(&thread_sec_writer, NULL, writer_thread, &ctx_sec_writer);
  pthread_create(&thread_sec_reader, NULL, reader_thread, &ctx_sec_reader);
  
  /* Monitor progress */
  while (test_running_global) {
    usleep(100000);
    elapsed_time = (iohdlc_time_now_ms() - start_time) / 1000;
    
    if (config.duration_type == TEST_BY_TIME) {
      if (elapsed_time >= config.duration_value) {
        test_running_global = false;
      }
      printf("Elapsed: %u/%u seconds | PRI: %u sent, %u rcv | SEC: %u sent, %u rcv\n",
             elapsed_time, config.duration_value,
             stats_primary.packets_sent, stats_primary.packets_received,
             stats_secondary.packets_sent, stats_secondary.packets_received);
    } else if (config.duration_type == TEST_INFINITE) {
      printf("Elapsed: %u seconds | PRI: %u sent, %u rcv | SEC: %u sent, %u rcv\n",
             elapsed_time,
             stats_primary.packets_sent, stats_primary.packets_received,
             stats_secondary.packets_sent, stats_secondary.packets_received);
    }
  }
  
  elapsed_time = (iohdlc_time_now_ms() - start_time) / 1000;
  stats_primary.end_time_ms = iohdlc_time_now_ms();
  stats_secondary.end_time_ms = iohdlc_time_now_ms();
  
  /* Wait for threads */
  printf("\nStopping threads...\n");
  pthread_join(thread_pri_writer, NULL);
  pthread_join(thread_pri_reader, NULL);
  pthread_join(thread_sec_writer, NULL);
  pthread_join(thread_sec_reader, NULL);
  
  /* Print results */
  printf("\n");
  printf("========================================\n");
  printf("TEST COMPLETED\n");
  printf("========================================\n\n");
  
  printf("Total elapsed time: %u seconds\n\n", elapsed_time);
  
  /* Print station statistics */
  printf("Primary Station:\n");
  printf("  Packets sent:     %u\n", stats_primary.packets_sent);
  printf("  Packets received: %u\n", stats_primary.packets_received);
  printf("  Bytes sent:       %lu\n", stats_primary.total_bytes_sent);
  printf("  Bytes received:   %lu\n", stats_primary.total_bytes_received);
  printf("\n");
  
  printf("Secondary Station:\n");
  printf("  Packets sent:     %u\n", stats_secondary.packets_sent);
  printf("  Packets received: %u\n", stats_secondary.packets_received);
  printf("  Bytes sent:       %lu\n", stats_secondary.total_bytes_sent);
  printf("  Bytes received:   %lu\n", stats_secondary.total_bytes_received);
  printf("\n");
  
  /* Calculate and print traffic statistics based on direction */
  if (config.traffic_direction == TRAFFIC_PRI_TO_SEC) {
    uint32_t lost = (stats_primary.packets_sent > stats_secondary.packets_received) ?
                     (stats_primary.packets_sent - stats_secondary.packets_received) : 0;
    float loss_percent = (stats_primary.packets_sent > 0) ?
                          (100.0f * lost / stats_primary.packets_sent) : 0.0f;
    float throughput = (elapsed_time > 0) ?
                        ((float)stats_secondary.total_bytes_received / elapsed_time) : 0.0f;
    
    printf("Primary → Secondary Traffic:\n");
    printf("  Sent:       %u packets (%lu bytes)\n",
           stats_primary.packets_sent, stats_primary.total_bytes_sent);
    printf("  Received:   %u packets (%lu bytes)\n",
           stats_secondary.packets_received, stats_secondary.total_bytes_received);
    printf("  P seq err:  %u packets\n", stats_primary.packets_reordered);
    printf("  S seq err:  %u packets\n", stats_secondary.packets_reordered);
    printf("  Lost:       %u packets (%.2f%%)\n", lost, loss_percent);
    printf("  Throughput: %.2f bytes/s (%.2f KB/s)\n", throughput, throughput / 1024.0f);
    printf("\n");
  } else if (config.traffic_direction == TRAFFIC_SEC_TO_PRI) {
    uint32_t lost = (stats_secondary.packets_sent > stats_primary.packets_received) ?
                     (stats_secondary.packets_sent - stats_primary.packets_received) : 0;
    float loss_percent = (stats_secondary.packets_sent > 0) ?
                          (100.0f * lost / stats_secondary.packets_sent) : 0.0f;
    float throughput = (elapsed_time > 0) ?
                        ((float)stats_primary.total_bytes_received / elapsed_time) : 0.0f;
    
    printf("Secondary → Primary Traffic:\n");
    printf("  Sent:       %u packets (%lu bytes)\n",
           stats_secondary.packets_sent, stats_secondary.total_bytes_sent);
    printf("  Received:   %u packets (%lu bytes)\n",
           stats_primary.packets_received, stats_primary.total_bytes_received);
    printf("  Lost:       %u packets (%.2f%%)\n", lost, loss_percent);
    printf("  P seq err:  %u packets\n", stats_primary.packets_reordered);
    printf("  S seq err:  %u packets\n", stats_secondary.packets_reordered);
    printf("  Throughput: %.2f bytes/s (%.2f KB/s)\n", throughput, throughput / 1024.0f);
    printf("\n");
  } else if (config.traffic_direction == TRAFFIC_BIDIRECTIONAL) {
    /* Primary → Secondary */
    uint32_t lost_p2s = (stats_primary.packets_sent > stats_secondary.packets_received) ?
                         (stats_primary.packets_sent - stats_secondary.packets_received) : 0;
    float loss_percent_p2s = (stats_primary.packets_sent > 0) ?
                              (100.0f * lost_p2s / stats_primary.packets_sent) : 0.0f;
    float throughput_p2s = (elapsed_time > 0) ?
                            ((float)stats_secondary.total_bytes_received / elapsed_time) : 0.0f;
    
    /* Secondary → Primary */
    uint32_t lost_s2p = (stats_secondary.packets_sent > stats_primary.packets_received) ?
                         (stats_secondary.packets_sent - stats_primary.packets_received) : 0;
    float loss_percent_s2p = (stats_secondary.packets_sent > 0) ?
                              (100.0f * lost_s2p / stats_secondary.packets_sent) : 0.0f;
    float throughput_s2p = (elapsed_time > 0) ?
                            ((float)stats_primary.total_bytes_received / elapsed_time) : 0.0f;
    
    printf("Primary → Secondary Traffic:\n");
    printf("  Sent:       %u packets (%lu bytes)\n",
           stats_primary.packets_sent, stats_primary.total_bytes_sent);
    printf("  Received:   %u packets (%lu bytes)\n",
           stats_secondary.packets_received, stats_secondary.total_bytes_received);
    printf("  Lost:       %u packets (%.2f%%)\n", lost_p2s, loss_percent_p2s);
    printf("  Reordered:  %u packets\n", stats_secondary.packets_reordered);
    printf("  Throughput: %.2f bytes/s (%.2f KB/s)\n", throughput_p2s, throughput_p2s / 1024.0f);
    printf("\n");
    
    printf("Secondary → Primary Traffic:\n");
    printf("  Sent:       %u packets (%lu bytes)\n",
           stats_secondary.packets_sent, stats_secondary.total_bytes_sent);
    printf("  Received:   %u packets (%lu bytes)\n",
           stats_primary.packets_received, stats_primary.total_bytes_received);
    printf("  Lost:       %u packets (%.2f%%)\n", lost_s2p, loss_percent_s2p);
    printf("  Reordered:  %u packets\n", stats_primary.packets_reordered);
    printf("  Throughput: %.2f bytes/s (%.2f KB/s)\n", throughput_s2p, throughput_s2p / 1024.0f);
    printf("\n");
  }
  
cleanup:
  /* Stop runners */
  ioHdlcRunnerStop(&station_primary);
  ioHdlcRunnerStop(&station_secondary);
  
  /* Cleanup */
  mock_stream_adapter_destroy(adapter_primary);
  mock_stream_adapter_destroy(adapter_secondary);
  mock_stream_destroy(stream_primary);
  mock_stream_destroy(stream_secondary);
  
  return 0;
}
