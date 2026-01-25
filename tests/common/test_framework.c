/*
 * ioHdlc Test Framework
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
/**
 * @file    test_framework.c
 * @brief   Common test framework implementation.
 */

#include "test_framework.h"
#include "test_helpers.h"
#include "ioHdlcosal.h"
#include <string.h>

/*===========================================================================*/
/* Statistics Functions                                                      */
/*===========================================================================*/

void test_init_statistics(test_statistics_t *stats) {
  memset(stats, 0, sizeof(test_statistics_t));
  stats->min_latency_ms = UINT32_MAX;
  stats->start_time_ms = iohdlc_time_now_ms();
}

/*===========================================================================*/
/* Packet Generation/Validation                                              */
/*===========================================================================*/

size_t test_generate_packet(uint32_t seq, uint32_t payload_size,
                             uint8_t *buffer, size_t buffer_size) {
  size_t total_size = TEST_PACKET_HEADER_SIZE + payload_size;
  
  if (total_size > buffer_size) {
    return 0;  /* Buffer too small */
  }
  
  test_packet_t *pkt = (test_packet_t *)buffer;
  pkt->sequence = seq;
  pkt->timestamp_ms = iohdlc_time_now_ms();
  pkt->payload_len = payload_size;
  
  /* Fill payload with pattern for debugging (optional) */
  for (uint32_t i = 0; i < payload_size; i++) {
    pkt->payload[i] = (uint8_t)(seq + i);
  }
  
  return total_size;
}

bool test_validate_packet(const uint8_t *buffer, size_t len,
                          uint32_t *expected_seq, test_statistics_t *stats) {
  if (len < TEST_PACKET_HEADER_SIZE) {
    return false;  /* Packet too short */
  }
  
  const test_packet_t *pkt = (const test_packet_t *)buffer;
  
  /* Check sequence number */
  if (pkt->sequence != *expected_seq) {
    if (pkt->sequence < *expected_seq) {
      /* Reordered packet (arrived late) */
      stats->packets_reordered++;
    } else {
      /* Lost packets (gap in sequence) */
      uint32_t lost = pkt->sequence - *expected_seq;
      stats->packets_lost += lost;
    }
    *expected_seq = pkt->sequence + 1;
  } else {
    (*expected_seq)++;
  }
  
  /* Calculate latency */
  uint32_t now = iohdlc_time_now_ms();
  uint32_t latency = now - pkt->timestamp_ms;
  
  if (latency < stats->min_latency_ms) {
    stats->min_latency_ms = latency;
  }
  if (latency > stats->max_latency_ms) {
    stats->max_latency_ms = latency;
  }
  stats->sum_latency_ms += latency;
  
  /* Update counters */
  stats->packets_received++;
  stats->total_bytes_received += len;
  
  return true;
}

/*===========================================================================*/
/* Reporting Functions                                                       */
/*===========================================================================*/

void test_print_config(const test_config_t *cfg) {
  test_printf("\n");
  test_printf("Test Configuration:\n");
  test_printf("===================\n");
  test_printf("Test name:    %s\n", cfg->test_name ? cfg->test_name : "unnamed");
  
  /* Mode */
  const char *mode_str;
  switch (cfg->mode) {
    case IOHDLC_OM_NRM: mode_str = "NRM"; break;
    case IOHDLC_OM_ARM: mode_str = "ARM"; break;
    case IOHDLC_OM_ABM: mode_str = "ABM"; break;
    default: mode_str = "UNKNOWN"; break;
  }
  test_printf("Mode:         %s-%s\n", mode_str, cfg->use_twa ? "TWA" : "TWS");
  
  /* Duration */
  test_printf("Duration:     ");
  switch (cfg->duration_type) {
    case TEST_BY_COUNT:
      test_printf("%u iterations\n", cfg->duration_value);
      break;
    case TEST_BY_TIME:
      test_printf("%u seconds\n", cfg->duration_value);
      break;
    case TEST_INFINITE:
      test_printf("infinite (until Ctrl-C)\n");
      break;
  }
  
  /* Traffic pattern */
  test_printf("Exchanges:    %u per iteration\n", cfg->exchanges_per_iteration);
  test_printf("Packet size:  %u bytes\n", cfg->bytes_per_exchange);
  
  /* Direction */
  const char *dir_str;
  switch (cfg->traffic_direction) {
    case TRAFFIC_PRI_TO_SEC: dir_str = "Primary -> Secondary"; break;
    case TRAFFIC_SEC_TO_PRI: dir_str = "Secondary -> Primary"; break;
    case TRAFFIC_BIDIRECTIONAL: dir_str = "Bidirectional"; break;
    default: dir_str = "UNKNOWN"; break;
  }
  test_printf("Direction:    %s\n", dir_str);
  
  /* Error injection */
  test_printf("Error rate:   %u%% %s\n", cfg->error_rate, 
              cfg->error_rate == 0 ? "(disabled)" : "(enabled)");
  
  /* Protocol parameters */
  test_printf("Reply timeout: %u ms %s\n", 
              cfg->reply_timeout_ms == 0 ? 100 : cfg->reply_timeout_ms,
              cfg->reply_timeout_ms == 0 ? "(default)" : "");
  test_printf("\n");
}

void test_print_statistics(const test_statistics_t *stats) {
  uint32_t duration_ms = stats->end_time_ms - stats->start_time_ms;
  
  test_printf("\n");
  test_printf("Test Results:\n");
  test_printf("=============\n");
  test_printf("Duration:     %u.%03u seconds\n", 
         duration_ms / 1000, duration_ms % 1000);
  
  test_printf("\nPacket Statistics:\n");
  test_printf("  Sent:       %u packets\n", stats->packets_sent);
  test_printf("  Received:   %u packets\n", stats->packets_received);
  test_printf("  Lost:       %u packets", stats->packets_lost);
  if (stats->packets_sent > 0) {
    test_printf(" (%.2f%%)", (stats->packets_lost * 100.0) / stats->packets_sent);
  }
  test_printf("\n");
  test_printf("  Reordered:  %u packets\n", stats->packets_reordered);
  
  test_printf("\nByte Statistics:\n");
  test_printf("  Sent:       %llu bytes\n", (unsigned long long)stats->total_bytes_sent);
  test_printf("  Received:   %llu bytes\n", (unsigned long long)stats->total_bytes_received);
  
  if (duration_ms > 0) {
    double throughput = (stats->total_bytes_received * 1000.0) / duration_ms;
    test_printf("  Throughput: %.2f bytes/s (%.2f KB/s)\n",
           throughput, throughput / 1024.0);
  }
  
  test_printf("\nLatency Statistics:\n");
  if (stats->packets_received > 0) {
    test_printf("  Min:        %u ms\n", stats->min_latency_ms);
    test_printf("  Max:        %u ms\n", stats->max_latency_ms);
    test_printf("  Avg:        %.2f ms\n",
           (double)stats->sum_latency_ms / stats->packets_received);
  } else {
    test_printf("  No packets received\n");
  }
  test_printf("\n");
}


