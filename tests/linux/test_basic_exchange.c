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

/* Signal handling (Linux-only) */
#ifdef __linux__
#include <signal.h>
#endif

static iohdlc_station_t *st_pri, *st_sec;
/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

#define PRIMARY_ADDR    0x01
#define SECONDARY_ADDR  0x02
#define WINDOW_SIZE     7
#define MAX_PACKET_SIZE 128  /* Max packet size for tests */
#define WTMO 1800
#define RTMO 2200

static volatile bool test_running_global = true;

/*===========================================================================*/
/* Signal Handler (Linux-only)                                              */
/*===========================================================================*/

#ifdef __linux__
static void sigint_handler(int sig) {
  (void)sig;
  test_running_global = false;
  test_request_stop();  /* Signal stop via OS-agnostic API */
  printf("\n\nTest interrupted. Stopping...\n");
}
#endif

/*===========================================================================*/
/* Thread Functions                                                          */
/*===========================================================================*/

typedef struct {
  iohdlc_station_t *station;
  iohdlc_station_peer_t *peer;
  test_statistics_t *stats;
  iohdlc_mutex_t *stats_mutex;
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
  bool test_running = true;

  if (!ctx->enabled) {
    return NULL;  /* Thread not needed for this direction */
  }
  
  while (test_running && !test_should_stop()) {
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
      
      if ((ctx->station->addr == 3) && ((ctx->seq & 0x0FF) == 0)) {
        ioHdlc_sleep_ms(600);
      }
      ssize_t sent = ioHdlcWriteTmo(ctx->peer, buffer, packet_size, WTMO);
      if (sent >= (ssize_t)packet_size) {
        iohdlc_mutex_lock(ctx->stats_mutex);
        ctx->stats->packets_sent++;
        ctx->stats->total_bytes_sent += sent;
        iohdlc_mutex_unlock(ctx->stats_mutex);
        packets_sent++;
      } else {
        test_dump_station_state(st_pri, "Pri At writer error");
        test_dump_station_state(st_sec, "Sec At writer error");

        if (iohdlc_errno == ETIMEDOUT)
          fprintf(stderr, "Writer %u Timeout!\n", ctx->station->addr);
        else
          fprintf(stderr, "Writer %u Error %d!\n", ctx->station->addr,
            iohdlc_errno);
        test_running = false;
        break;
      }
    }
    
    if (packets_sent >= ctx->config->exchanges_per_iteration) {
      packets_sent = 0;
      iterations++;
    }
    
    ioHdlc_sleep_ms(1);  /* Small yield (was 1us, now 1ms min granularity) */
  }
  fprintf(stderr, "Writer %u Data written (iters %d)!\n", ctx->station->addr, iterations);
  test_running_global = false;
  return NULL;
}

/**
 * @brief Reader thread - only receives data
 */
static void *reader_thread(void *arg) {
  thread_context_t *ctx = (thread_context_t *)arg;
  uint8_t buffer[MAX_PACKET_SIZE + TEST_PACKET_HEADER_SIZE];
  bool test_running = true;

  if (!ctx->enabled) {
    return NULL;  /* Thread not needed for this direction */
  }
  
  while (test_running && !test_should_stop()) {
   
    ssize_t received = ioHdlcReadTmo(ctx->peer, buffer, ctx->config->bytes_per_exchange, RTMO);

    /* Every 256 frames, introduce a small delay to simulate pool low condition */
    if (((ctx->seq+1) & 0xFF) == 0) {
      ioHdlc_sleep_ms(45);
    }
    if (received > 0 && (size_t)received >= ctx->config->bytes_per_exchange) {
      iohdlc_mutex_lock(ctx->stats_mutex);
      test_validate_packet(buffer, received, &ctx->seq, ctx->stats);
      iohdlc_mutex_unlock(ctx->stats_mutex);
    } else if (received > 0) {
      fprintf(stderr, "Warning: received short packet (%zd bytes)\n", received);
    } else if (received == 0) {
      fprintf(stderr, "Reader %u zero read!\n", ctx->station->addr);
      test_running = false;  /* No data received, assume test end */
    } else {
      test_dump_station_state(ctx->station, "Pri At reader error");
      test_dump_station_state(st_sec, "Sec At writer error");

      fprintf(stderr, "Reader %u Error %d!\n", ctx->station->addr, iohdlc_errno);
      test_running = false;
    }
    if (ctx->stats->packets_received >=
        ctx->config->exchanges_per_iteration * ctx->config->duration_value &&
        ctx->config->duration_type == TEST_BY_COUNT) {
      fprintf(stderr, "Reader %u All data read\n", ctx->station->addr);
      test_running = false;  /* All data received, assume test end */
      break;
    }
  }

  test_running_global = false;
    
  return NULL;
}

/*===========================================================================*/
/* Main Test                                                                 */
/*===========================================================================*/

int main(int argc, char **argv) {
  test_config_t config;
  test_statistics_t stats_primary, stats_secondary;
  iohdlc_mutex_t stats_mutex_primary, stats_mutex_secondary;
  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t station_config;
  thread_context_t ctx_pri_writer, ctx_pri_reader, ctx_sec_writer, ctx_sec_reader;
  iohdlc_thread_t *thread_pri_writer, *thread_pri_reader, *thread_sec_writer, *thread_sec_reader;
  static uint8_t arena_primary[16384], arena_secondary[16384];
  int32_t result;
  uint32_t start_time, elapsed_time;
  
  st_pri = &station_primary;
  st_sec = &station_secondary;

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
  
  /* Initialize mutexes */
  iohdlc_mutex_init(&stats_mutex_primary);
  iohdlc_mutex_init(&stats_mutex_secondary);
  
  /* Setup signal handler (Linux-only) */
#ifdef __linux__
  signal(SIGINT, sigint_handler);
#endif
  
  printf("\n");
  printf("========================================\n");
  printf("Initializing HDLC stations...\n");
  printf("========================================\n\n");
  
  /* Create mock streams */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = (config.error_rate > 0),
    .error_rate = config.error_rate,
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
  
  /* Configure primary station */
  /* Note: Using default optfuncs (NULL) which enables TYPE0 FFF (max 127 bytes frame) */
  station_config.mode = config.mode;
  station_config.flags = IOHDLC_FLG_PRI | (config.use_twa ? IOHDLC_FLG_TWA : 0);
  station_config.log2mod = 3;
  station_config.addr = PRIMARY_ADDR;
  station_config.driver = (ioHdlcDriver *)&driver_primary;
  station_config.frame_arena = arena_primary;
  station_config.frame_arena_size = sizeof arena_primary;
  station_config.max_info_len = 0;
  station_config.pool_watermark = 0;  /* Auto: 20% min 1 */
  station_config.fff_type = 1;  /* TYPE0 */
  station_config.optfuncs = NULL;  /* Use defaults (TYPE0 FFF) */
  station_config.phydriver = &port_primary;
  station_config.phydriver_config = NULL;
  station_config.reply_timeout_ms = config.reply_timeout_ms;
  station_config.poll_retry_max = config.poll_retry_max;
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &station_config);
  if (result != 0) {
    fprintf(stderr, "Primary station init failed: %d\n", result);
    return 1;
  }
  
  /* Configure secondary station */
  station_config.mode = IOHDLC_OM_NDM;
  station_config.flags = config.use_twa ? IOHDLC_FLG_TWA : 0;
  station_config.addr = SECONDARY_ADDR;
  station_config.driver = (ioHdlcDriver *)&driver_secondary;
  station_config.frame_arena = arena_secondary;
  station_config.frame_arena_size = sizeof arena_secondary;
  station_config.max_info_len = 0;  /* Auto: 122 bytes for TYPE0 FFF */
  station_config.pool_watermark = 0;  /* Auto: 20% min 1 */
  station_config.phydriver = &port_secondary;
  station_config.reply_timeout_ms = config.reply_timeout_ms;
  station_config.poll_retry_max = config.poll_retry_max;
  
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
  ioHdlc_sleep_ms(50);
  
  /* Establish connection */
  printf("Establishing connection...\n");
  result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, config.mode);
  if (result != 0) {
    fprintf(stderr, "Link up failed: %d\n", result);
    goto cleanup;
  }
  
  ioHdlc_sleep_ms(100);
  
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
  thread_pri_writer = iohdlc_thread_create("pri_writer", 0, 0, writer_thread, &ctx_pri_writer);
  thread_pri_reader = iohdlc_thread_create("pri_reader", 0, 0, reader_thread, &ctx_pri_reader);
  thread_sec_writer = iohdlc_thread_create("sec_writer", 0, 0, writer_thread, &ctx_sec_writer);
  thread_sec_reader = iohdlc_thread_create("sec_reader", 0, 0, reader_thread, &ctx_sec_reader);
  
  /* Monitor progress */
  while (test_running_global && !test_should_stop()) {
    ioHdlc_sleep_ms(config.progress_interval_ms);
    elapsed_time = (iohdlc_time_now_ms() - start_time) / 1000;
    
    if (config.duration_type == TEST_BY_TIME) {
      if (elapsed_time >= config.duration_value) {
        test_running_global = false;
      }
      printf("Elapsed: %u/%u seconds | PRI: %u sent, %u rcv | SEC: %u sent, %u rcv\n",
             elapsed_time, config.duration_value,
             stats_primary.packets_sent, stats_primary.packets_received,
             stats_secondary.packets_sent, stats_secondary.packets_received);
    } else if (config.duration_type == TEST_BY_COUNT) {
      /* Calculate progress based on packets sent/received vs expected */
      uint32_t expected_total = config.duration_value * config.exchanges_per_iteration;
      uint32_t current_sent = 0;
      uint32_t current_rcv = 0;
      
      if (config.traffic_direction == TRAFFIC_PRI_TO_SEC) {
        current_sent = stats_primary.packets_sent;
        current_rcv = stats_secondary.packets_received;
      } else if (config.traffic_direction == TRAFFIC_SEC_TO_PRI) {
        current_sent = stats_secondary.packets_sent;
        current_rcv = stats_primary.packets_received;
      } else {  /* BIDIRECTIONAL */
        current_sent = stats_primary.packets_sent + stats_secondary.packets_sent;
        current_rcv = stats_primary.packets_received + stats_secondary.packets_received;
        expected_total *= 2;  /* Both directions */
      }
      
      printf("Progress: %u/%u packets sent, %u rcv | PRI: %u/%u | SEC: %u/%u\n",
             current_sent, expected_total, current_rcv,
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
  iohdlc_thread_join(thread_pri_writer);
  iohdlc_thread_join(thread_pri_reader);
  iohdlc_thread_join(thread_sec_writer);
  iohdlc_thread_join(thread_sec_reader);
  
  ioHdlcStationLinkDown(&station_primary, station_primary.c_peer->addr);

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
  printf("  Seq errors:       %u\n", stats_primary.packets_reordered);
  printf("  Bytes sent:       %lu\n", stats_primary.total_bytes_sent);
  printf("  Bytes received:   %lu\n", stats_primary.total_bytes_received);
  printf("\n");
  
  printf("Secondary Station:\n");
  printf("  Packets sent:     %u\n", stats_secondary.packets_sent);
  printf("  Packets received: %u\n", stats_secondary.packets_received);
  printf("  Seq errors:       %u\n", stats_secondary.packets_reordered);
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
    printf("  Throughput: %.2f bytes/s (%.2f KB/s)\n", throughput_p2s, throughput_p2s / 1024.0f);
    printf("\n");
    
    printf("Secondary → Primary Traffic:\n");
    printf("  Sent:       %u packets (%lu bytes)\n",
           stats_secondary.packets_sent, stats_secondary.total_bytes_sent);
    printf("  Received:   %u packets (%lu bytes)\n",
           stats_primary.packets_received, stats_primary.total_bytes_received);
    printf("  Lost:       %u packets (%.2f%%)\n", lost_s2p, loss_percent_s2p);
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
