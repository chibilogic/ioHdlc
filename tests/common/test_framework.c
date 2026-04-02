/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
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
/* Test Control (OS-agnostic stop mechanism)                                */
/*===========================================================================*/

/**
 * @brief   Global test stop flag (initialized to false).
 */
volatile bool test_stop_requested = false;

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

size_t test_generate_packet(uint32_t seq, uint32_t packet_size,
                             uint8_t *buffer, size_t buffer_size) {
  /* packet_size is total size INCLUDING header */
  if (packet_size < TEST_PACKET_HEADER_SIZE) {
    return 0;  /* Packet too small for header */
  }
  
  if (packet_size > buffer_size) {
    return 0;  /* Buffer too small */
  }
  
  uint32_t payload_size = packet_size - TEST_PACKET_HEADER_SIZE;
  
  test_packet_t *pkt = (test_packet_t *)buffer;
  pkt->sequence = seq;
  pkt->timestamp_ms = iohdlc_time_now_ms();
  pkt->payload_len = (uint16_t)payload_size;
  
  /* Fill payload with pattern for debugging (optional) */
  for (uint32_t i = 0; i < payload_size; i++) {
    pkt->payload[i] = (uint8_t)(seq + i);
  }
  
  return packet_size;
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
    case IOHDLC_OM_ABM: mode_str = "ABM"; break;
    default: mode_str = "UNKNOWN"; break;
  }
  test_printf("Mode:         %s-%s\n", mode_str, cfg->use_twa ? "TWA" : "TWS");
  test_printf("Modulo:       %u\n", cfg->modulo);
  
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
  
  /* Watermark testing */
  if (cfg->watermark_delay_ms > 0) {
    test_printf("Watermark delay: %u ms every 256 packets\n", cfg->watermark_delay_ms);
  }
  
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
  test_printf("  Sent:       " U64_FMT " bytes\n", U64_ARGS(stats->total_bytes_sent));
  test_printf("  Received:   " U64_FMT " bytes\n", U64_ARGS(stats->total_bytes_received));
  
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

/**
 * @brief   Dump HDLC station and current peer state for debugging.
 */
void test_dump_station_state(iohdlc_station_t *station, const char *label) {
  if (station == NULL) {
    test_printf("ERROR: NULL station pointer\n");
    return;
  }
  
  if (label != NULL) {
    test_printf("\n========================================\n");
    test_printf("STATION DUMP: %s\n", label);
    test_printf("========================================\n\n");
  } else {
    test_printf("\n========================================\n");
    test_printf("STATION DUMP\n");
    test_printf("========================================\n\n");
  }
  
  /* Station information */
  test_printf("Station Configuration:\n");
  test_printf("  Address:        0x%02X\n", station->addr);
  test_printf("  Mode:           %s\n", 
         station->mode == IOHDLC_OM_NDM ? "NDM" :
         station->mode == IOHDLC_OM_ADM ? "ADM" :
         station->mode == IOHDLC_OM_NRM ? "NRM" :
         station->mode == IOHDLC_OM_ABM ? "ABM" : "UNKNOWN");
  test_printf("  Flags:          0x%04X %s", station->flags,
         (station->flags & IOHDLC_FLG_PRI) ? "(PRIMARY)" : "(SECONDARY)");
  if (station->flags & IOHDLC_FLG_TWA) test_printf(" TWA");
  if (station->flags & IOHDLC_FLG_BUSY) test_printf(" BUSY");
  test_printf("\n");  
  test_printf("  Modmask:        0x%08X (mod %u)\n", station->modmask, station->modmask + 1);
  test_printf("  Frame offset:   %u byte%s", station->frame_offset,
         station->frame_offset == 1 ? " (FFF TYPE0)" :
         station->frame_offset == 2 ? "s (FFF TYPE1)" : " (No FFF)");
  test_printf("\n");
  test_printf("  Control size:   %u byte%s\n", station->ctrl_size, 
         station->ctrl_size > 1 ? "s" : "");
  test_printf("  FCS size:       %u byte%s\n", station->fcs_size,
         station->fcs_size > 1 ? "s" : "");
  test_printf("  Reply timeout:  %u ms\n", station->reply_timeout_ms);
  test_printf("  Poll retry max: %u\n", station->poll_retry_max_cfg);
  test_printf("  P/F state:      0x%02X", station->pf_state);
  if (station->pf_state & 0x01) test_printf(" P_RCVED");
  if (station->pf_state & 0x02) test_printf(" F_RCVED");
  test_printf("\n");
  
  test_printf("\nFrame Pool:\n");
  test_printf("  Framesize:      %u bytes\n", (uint32_t)station->frame_pool.framesize);
  test_printf("  Total frames:   %u\n", station->frame_pool.total);
  test_printf("  Allocated:      %u\n", station->frame_pool.allocated);
  test_printf("  Free:           %u\n", station->frame_pool.total - station->frame_pool.allocated);
  test_printf("  State:          %s\n",
         station->frame_pool.state == IOHDLC_POOL_NORMAL ? "NORMAL" :
         station->frame_pool.state == IOHDLC_POOL_LOW_WATER ? "LOW_WATER" : "UNKNOWN");
  test_printf("  Low threshold:  %u frames (%u%%)\n",
         station->frame_pool.low_threshold, station->frame_pool.low_pct);
  test_printf("  High threshold: %u frames (%u%%)\n",
         station->frame_pool.high_threshold, station->frame_pool.high_pct);
  
  /* Current peer information */
  iohdlc_station_peer_t *peer = station->c_peer;
  if (peer != NULL) {
    test_printf("\nCurrent Peer (0x%02X):\n", peer->addr);
    test_printf("  State:          0x%08X", peer->ss_state);
    if (peer->ss_state & IOHDLC_SS_ST_CONN) test_printf(" CONNECTED");
    if (peer->ss_state & IOHDLC_SS_ST_DISM) test_printf(" DISCONNECTED");
    if (peer->ss_state & IOHDLC_SS_RECVING) test_printf(" RECEIVING");
    if (peer->ss_state & IOHDLC_SS_REJPEND) test_printf(" REJ-TO-SEND");
    if (peer->ss_state & IOHDLC_SS_BUSY) test_printf(" BUSY");
    test_printf("\n");
    
    test_printf("  Max info (TX):  %u bytes\n", peer->mifls);
    test_printf("  Max info (RX):  %u bytes\n", peer->miflr);
    test_printf("  Window size:    %u frames\n", peer->ks);
    
    test_printf("\nSequence Numbers:\n");
    test_printf("  V(S):           %u (next to send)\n", peer->vs);
    test_printf("  V(R):           %u (next expected)\n", peer->vr);
    test_printf("  V(S) highest:   %u\n", peer->vs_highest);
    test_printf("  N(R):           %u (last acked)\n", peer->nr);
    
    /* Count frames in queues by traversing */
    uint32_t trans_count = 0, retrans_count = 0, recept_count = 0;
    iohdlc_frame_q_t *fqp;
    
    for (fqp = peer->i_trans_q.next; fqp != &peer->i_trans_q; fqp = fqp->next) {
      trans_count++;
    }
    for (fqp = peer->i_retrans_q.next; fqp != &peer->i_retrans_q; fqp = fqp->next) {
      retrans_count++;
    }
    for (fqp = peer->i_recept_q.next; fqp != &peer->i_recept_q; fqp = fqp->next) {
      recept_count++;
    }
    
    test_printf("\nFrame Queues:\n");
    test_printf("  I trans queue:  %u frames\n", trans_count);
    test_printf("  I retrans queue:%u frames\n", retrans_count);
    test_printf("  I recept queue: %u frames\n", recept_count);
    test_printf("  Pending count:  %u (window limit: %u)\n", 
           peer->i_pending_count, peer->ks * 2);
    
    test_printf("\nPartial Read State:\n");
    test_printf("  Frame:          %s\n", peer->partial_read_frame ? "ACTIVE" : "none");
    if (peer->partial_read_frame) {
      test_printf("  Offset:         %u bytes\n", (uint32_t)peer->partial_read_offset);
    }
    
    test_printf("\nCheckpoint/Retry State:\n");
    test_printf("  Poll retry cnt: %u (max: %u)\n", peer->poll_retry_count, peer->poll_retry_max);
    test_printf("  Checkpoint act: %u\n", peer->chkpt_actioned);
    test_printf("  REJ actioned:   %u\n", peer->rej_actioned);
    test_printf("  V(S) at last PF:%u\n", peer->vs_atlast_pf);
    
    test_printf("\nTimer State:\n");
    test_printf("  T1    timer:  %s%s\n",
           iohdlc_vt_is_armed(&peer->reply_tmr) ? "ACTIVE" : "stopped",
           peer->reply_tmr.expired ? " (EXPIRED)" : "");
    test_printf("  T3    timer:  %s%s\n",
           iohdlc_vt_is_armed(&peer->t3_tmr) ? "ACTIVE" : "stopped",
           peer->t3_tmr.expired ? " (EXPIRED)" : "");
  } else {
    test_printf("\nNo current peer\n");
  }
  
  test_printf("\n========================================\n\n");
}

