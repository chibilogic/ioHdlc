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
 * @file    test_exchange.c
 * @brief   Parametrized HDLC test with real traffic and statistics.
 */

#include "test_framework.h"
#include "test_helpers.h"
#include "test_arenas.h"
#include "adapter_interface.h"
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcqueue.h"
#include "ioHdlcswdriver.h"
#include "ioHdlc_runner.h"
#include "ioHdlcfmempool.h"
#include "ioHdlcosal.h"
#include "ioHdlc_app_events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef IOHDLC_LOG_LEVEL
#define IOHDLC_LOG_LEVEL 0
#endif

static iohdlc_station_t *st_pri, *st_sec;
static iohdlc_station_peer_t *s_pri_peer, *s_sec_peer;
/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

#define PRIMARY_ADDR    0x01
#define SECONDARY_ADDR  0x02
#define WINDOW_SIZE     7
#define RETRY_TOTAL_TIMEOUT_TARGET_MS 25000U
#ifndef EXCHANGE_ARENA_SIZE
#define EXCHANGE_ARENA_SIZE 32768
#endif

static volatile bool test_failed_global = false;
static volatile bool s_exchange_error_reported = false;
static volatile uint32_t s_exchange_active_workers = 0U;
static iohdlc_mutex_t s_exchange_state_mutex;
static const uint8_t s_exchange_optfuncs_norej[5] = {
  0x00, 0x00, IOHDLC_OPT_SST, 0x00, IOHDLC_OPT_FFF | IOHDLC_OPT_INH
};
static uint8_t s_pri_writer_buf[TEST_EXCHANGE_MAX_PACKET_SIZE];
static uint8_t s_sec_writer_buf[TEST_EXCHANGE_MAX_PACKET_SIZE];
static uint8_t s_pri_reader_buf[TEST_EXCHANGE_MAX_PACKET_SIZE];
static uint8_t s_sec_reader_buf[TEST_EXCHANGE_MAX_PACKET_SIZE];

/*===========================================================================*/
/* Thread Functions                                                          */
/*===========================================================================*/

typedef struct {
  iohdlc_station_t *station;
  iohdlc_station_peer_t *peer;
  test_statistics_t *stats;
  iohdlc_mutex_t *stats_mutex;
  test_config_t *config;
  uint8_t *buffer;
  size_t buffer_size;
  uint32_t seq;
  bool enabled;  /* Whether this thread should be active */
} thread_context_t;

static void s_exchange_abort_peer(iohdlc_station_peer_t *peer) {
  if (peer == NULL) {
    return;
  }

  iohdlc_mutex_lock(&peer->state_mutex);
  if ((peer->ss_state & IOHDLC_SS_ST_CONN) != 0U &&
      peer->stationp->connected_count > 0U) {
    peer->stationp->connected_count--;
  }
  peer->ss_state &= (uint8_t)~(IOHDLC_SS_ST_CONN |
                               IOHDLC_SS_TERM_ORDERLY);
  peer->ss_state |= IOHDLC_SS_TERM_ABORTED;
  iohdlc_condvar_broadcast(&peer->tx_cv);
  iohdlc_condvar_broadcast(&peer->rx_cv);
  iohdlc_mutex_unlock(&peer->state_mutex);
}

static uint32_t s_exchange_last_retry_timeout_ms(uint32_t t1_ms) {
  uint64_t tmo;

  tmo = (uint64_t)t1_ms * IOHDLC_LAST_RETRY_T1_RATIO;
  if (tmo < IOHDLC_LAST_RETRY_TIMEOUT_MIN_MS)
    tmo = IOHDLC_LAST_RETRY_TIMEOUT_MIN_MS;

  if (tmo > ~(uint32_t)0U)
    return ~(uint32_t)0U;

  return (uint32_t)tmo;
}

static uint32_t s_exchange_retry_total_timeout_ms(uint32_t t1_ms,
                                                  uint8_t retry_max) {
  uint64_t total;

  if (retry_max >= 32U)
    return ~(uint32_t)0U;

  total = (uint64_t)t1_ms * (((uint64_t)1U << retry_max) - 1U);
  total += s_exchange_last_retry_timeout_ms(t1_ms);
  if (total > ~(uint32_t)0U)
    return ~(uint32_t)0U;

  return (uint32_t)total;
}

static uint8_t s_exchange_auto_poll_retry_max(uint32_t t1_ms) {
  uint8_t best_n2 = 1U;
  uint64_t best_diff = ~(uint64_t)0U;

  for (uint8_t n2 = 1U; n2 <= TEST_POLL_RETRY_MAX_LIMIT; n2++) {
    uint64_t total = s_exchange_retry_total_timeout_ms(t1_ms, n2);
    uint64_t diff = total > RETRY_TOTAL_TIMEOUT_TARGET_MS ?
        total - RETRY_TOTAL_TIMEOUT_TARGET_MS :
        RETRY_TOTAL_TIMEOUT_TARGET_MS - total;

    if (diff < best_diff) {
      best_diff = diff;
      best_n2 = n2;
    }

    if (total > RETRY_TOTAL_TIMEOUT_TARGET_MS && diff > best_diff)
      break;
  }

  return best_n2;
}

static void s_exchange_resolve_retry_config(test_config_t *config) {
  uint32_t t1_ms;

  IOHDLC_ASSERT(config != NULL, "s_exchange_resolve_retry_config: null config");

  t1_ms = config->reply_timeout_ms != 0U ?
      config->reply_timeout_ms :
      IOHDLC_REPLY_TIMEOUT_MS_DEFAULT;

  if (config->poll_retry_max == 0U) {
    config->poll_retry_max = s_exchange_auto_poll_retry_max(t1_ms);
    config->poll_retry_max_auto = true;
  } else {
    config->poll_retry_max_auto = false;
  }

  config->poll_retry_total_timeout_ms =
      s_exchange_retry_total_timeout_ms(t1_ms, config->poll_retry_max);
}

static uint32_t s_exchange_io_timeout_ms(const iohdlc_station_t *station,
                                         const iohdlc_station_peer_t *peer) {
  uint32_t t1_ms;
  uint32_t total_ms;

  IOHDLC_ASSERT(station != NULL, "s_exchange_io_timeout_ms: null station");
  IOHDLC_ASSERT(peer != NULL, "s_exchange_io_timeout_ms: null peer");

  t1_ms = station->reply_timeout_ms;
  total_ms = s_exchange_retry_total_timeout_ms(t1_ms, peer->poll_retry_max);
  if (total_ms > (~(uint32_t)0U - 1000U))
    return ~(uint32_t)0U;
  return total_ms + 1000U;
}

/**
 * @brief Writer thread - only sends data
 */
static void *writer_thread(void *arg) {
  thread_context_t *ctx = (thread_context_t *)arg;
  const uint32_t write_tmo = s_exchange_io_timeout_ms(ctx->station, ctx->peer);
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
                                                ctx->buffer, ctx->buffer_size);
      if (packet_size == 0) {
        test_printf("Writer %u configuration error: packet size %u exceeds test buffer (%u bytes max)\r\n",
                    ctx->station->addr,
                    ctx->config->bytes_per_exchange,
                    (unsigned)ctx->buffer_size);
        test_running = false;
        break;
      }
      
      if ((ctx->station->addr == 3) && ((ctx->seq & 0x0FF) == 0)) {
        ioHdlc_sleep_ms(600);
      }
      ssize_t sent = ioHdlcWriteTmo(ctx->peer, ctx->buffer, packet_size, write_tmo);
      if (sent >= (ssize_t)packet_size) {
        iohdlc_mutex_lock(ctx->stats_mutex);
        ctx->stats->packets_sent++;
        ctx->stats->total_bytes_sent += sent;
        iohdlc_mutex_unlock(ctx->stats_mutex);
        packets_sent++;
      } else {
        bool dump_once = false;

        iohdlc_mutex_lock(&s_exchange_state_mutex);
        test_failed_global = true;
        test_request_stop();
        if (!s_exchange_error_reported) {
          s_exchange_error_reported = true;
          dump_once = true;
        }
        iohdlc_mutex_unlock(&s_exchange_state_mutex);

        s_exchange_abort_peer(s_pri_peer);
        s_exchange_abort_peer(s_sec_peer);

        if (dump_once) {
          if (st_pri != NULL) {
            test_dump_station_state(st_pri, "A At writer error");
          }
          if (st_sec != NULL) {
            test_dump_station_state(st_sec, "B At writer error");
          }
        }

        if (iohdlc_errno == ETIMEDOUT)
          test_printf("Writer %u Timeout!\r\n", ctx->station->addr);
        else
          test_printf("Writer %u Error %d!\r\n", ctx->station->addr,
            iohdlc_errno);
        test_running = false;
        break;
      }
    }
    
    if (packets_sent >= ctx->config->exchanges_per_iteration) {
      packets_sent = 0;
      iterations++;
    }
  }
  test_printf("Writer %u Data written (iters %d)!\r\n", ctx->station->addr, iterations);
  iohdlc_mutex_lock(&s_exchange_state_mutex);
  if (s_exchange_active_workers > 0U) {
    s_exchange_active_workers--;
  }
  iohdlc_mutex_unlock(&s_exchange_state_mutex);
  return NULL;
}

/**
 * @brief Reader thread - only receives data
 */
static void *reader_thread(void *arg) {
  thread_context_t *ctx = (thread_context_t *)arg;
  const uint32_t read_tmo = s_exchange_io_timeout_ms(ctx->station, ctx->peer);
  bool test_running = true;

  if (!ctx->enabled) {
    return NULL;  /* Thread not needed for this direction */
  }
  
  while (test_running && !test_should_stop()) {
   
    ssize_t received = ioHdlcReadTmo(ctx->peer, ctx->buffer, ctx->config->bytes_per_exchange,
                                     read_tmo);

    /* Watermark test: delay every 256 packets to simulate pool pressure */
    if (ctx->config->watermark_delay_ms > 0 && ((ctx->seq+1) & 0xFF) == 0) {
      ioHdlc_sleep_ms(ctx->config->watermark_delay_ms);
    }
    
    if (received > 0 && (size_t)received >= ctx->config->bytes_per_exchange) {
      iohdlc_mutex_lock(ctx->stats_mutex);
      test_validate_packet(ctx->buffer, received, &ctx->seq, ctx->stats);
      iohdlc_mutex_unlock(ctx->stats_mutex);
    } else if (received > 0) {
      test_printf("Warning: received short packet (%d bytes)\r\n", (int)received);
    } else if (received == 0) {
      test_printf("Reader %u zero read!\r\n", ctx->station->addr);
      test_running = false;  /* No data received, assume test end */
    } else {
      bool dump_once = false;

      iohdlc_mutex_lock(&s_exchange_state_mutex);
      test_failed_global = true;
      test_request_stop();
      if (!s_exchange_error_reported) {
        s_exchange_error_reported = true;
        dump_once = true;
      }
      iohdlc_mutex_unlock(&s_exchange_state_mutex);

      s_exchange_abort_peer(s_pri_peer);
      s_exchange_abort_peer(s_sec_peer);

      if (dump_once) {
        if (st_pri != NULL) {
          test_dump_station_state(st_pri, "A At reader error");
        }
        if (st_sec != NULL) {
          test_dump_station_state(st_sec, "B At reader error");
        }
      }

      test_printf("Reader %u Error %d!\r\n", ctx->station->addr, iohdlc_errno);
      test_running = false;
    }
    if (ctx->stats->packets_received >=
        ctx->config->exchanges_per_iteration * ctx->config->duration_value &&
        ctx->config->duration_type == TEST_BY_COUNT) {
      test_printf("Reader %u All data read\r\n", ctx->station->addr);
      test_running = false;  /* All data received, assume test end */
      break;
    }
  }

  iohdlc_mutex_lock(&s_exchange_state_mutex);
  if (s_exchange_active_workers > 0U) {
    s_exchange_active_workers--;
  }
  iohdlc_mutex_unlock(&s_exchange_state_mutex);
    
  return NULL;
}

/*===========================================================================*/
/* Main Test                                                                 */
/*===========================================================================*/

iohdlc_station_t station_primary, station_secondary;
static uint8_t s_exchange_arena_primary[EXCHANGE_ARENA_SIZE];
static uint8_t s_exchange_arena_secondary[EXCHANGE_ARENA_SIZE];

/**
 * @brief Exchange test main function.
 * @param[in] adapter  Test adapter (UART or mock), must be non-NULL
 * @param[in] argc     Command line argument count
 * @param[in] argv     Command line arguments
 * @return 0 on success, 1 on failure
 * @note  Called from platform-specific wrappers (test_runner_exchange.c 
 *        for Linux, main_shell.c for ChibiOS).
 */
int test_exchange_main(const test_adapter_t *adapter, int argc, char **argv) {
  test_config_t config;
  test_statistics_t stats_primary, stats_secondary;
  iohdlc_mutex_t stats_mutex_primary, stats_mutex_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t station_config;
  thread_context_t ctx_pri_writer = {0}, ctx_pri_reader = {0};
  thread_context_t ctx_sec_writer = {0}, ctx_sec_reader = {0};
  iohdlc_thread_t *thread_pri_writer = NULL, *thread_pri_reader = NULL;
  iohdlc_thread_t *thread_sec_writer = NULL, *thread_sec_reader = NULL;
  test_statistics_t *stats_local;
  iohdlc_station_peer_t *peer_local;
  thread_context_t *ctx_writer_local;
  thread_context_t *ctx_reader_local;
  ioHdlcStreamPort port_primary = {0}, port_secondary = {0};
  bool twa_explicit = false;
  bool tws_explicit = false;
  bool endpoint_a_active;
  bool endpoint_b_active;
  bool both_endpoints;
  const char *local_label;
  const char *remote_label;
  const uint8_t *optfuncs;
  uint8_t log2mod;
  int result;
  uint32_t start_time = 0U, elapsed_time = 0U;
  uint32_t active_workers;
  bool thread_create_failed = false;
  bool adapter_initialized = false;
  int return_code = 0;

  /* Reset global state for multiple runs */
  test_failed_global = false;
  s_exchange_error_reported = false;
  test_stop_requested = false;
  iohdlc_mutex_init(&s_exchange_state_mutex);
  s_exchange_active_workers = 0U;

  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--twa") == 0) {
      twa_explicit = true;
    }
    else if (strcmp(argv[i], "--tws") == 0) {
      tws_explicit = true;
    }
  }

  memset(&config, 0, sizeof config);
  if (adapter->constraints & ADAPTER_CONSTRAINT_NRM_ONLY)
    config.mode = IOHDLC_OM_NRM;

  /* Parse configuration */
  if (!test_parse_config(&config, argc, argv)) {
    return 1;
  }

  if (config.modulo != 8 && config.modulo != 128) {
    test_printf("Error: Unsupported modulo %u (expected 8 or 128)\r\n",
                config.modulo);
    return 1;
  }

  if ((adapter->constraints & ADAPTER_CONSTRAINT_TWA_ONLY) != 0U &&
      !twa_explicit && !tws_explicit) {
    config.use_twa = true;
  }

  /* Enforce adapter hardware constraints */
  if (adapter->constraints & ADAPTER_CONSTRAINT_TWA_ONLY) {
    if (!config.use_twa) {
      test_printf("Error: adapter '%s' requires TWA mode.\r\n"
                  "       --tws is not supported on this adapter.\r\n"
                  "       Use --twa or omit the mode option (default is TWA for this adapter).\r\n",
                  adapter->name);
      return 1;
    }
  }

  if (adapter->constraints & ADAPTER_CONSTRAINT_NRM_ONLY) {
    if (config.mode != IOHDLC_OM_NRM) {
      test_printf("Error: adapter '%s' requires NRM mode.\r\n"
                  "       Use --mode=nrm or omit the mode option (default is NRM for this adapter).\r\n",
                  adapter->name);
      return 1;
    }
  }

  s_exchange_resolve_retry_config(&config);

  /* Enable HDLC logging if compiled in */
#if IOHDLC_LOG_LEVEL > 0
  extern bool iohdlc_log_enabled;
  iohdlc_log_enabled = true;
#endif
  
  /* Print configuration */
  test_printf("\r\n");
  test_print_config(&config);

  endpoint_a_active = config.endpoint_mode != TEST_ENDPOINT_B;
  endpoint_b_active = config.endpoint_mode != TEST_ENDPOINT_A;
  both_endpoints = endpoint_a_active && endpoint_b_active;
  local_label = endpoint_a_active ? "A" : "B";
  remote_label = endpoint_a_active ? "B" : "A";
  log2mod = (config.modulo == 128) ? 7 : 3;
  optfuncs = (adapter->constraints & ADAPTER_CONSTRAINT_TWA_ONLY) ?
      s_exchange_optfuncs_norej : NULL;

  memset(&station_primary, 0, sizeof station_primary);
  memset(&station_secondary, 0, sizeof station_secondary);
  memset(&peer_at_primary, 0, sizeof peer_at_primary);
  memset(&peer_at_secondary, 0, sizeof peer_at_secondary);
  memset(&stats_primary, 0, sizeof stats_primary);
  memset(&stats_secondary, 0, sizeof stats_secondary);

  st_pri = endpoint_a_active ? &station_primary : NULL;
  st_sec = endpoint_b_active ? &station_secondary : NULL;
  s_pri_peer = NULL;
  s_sec_peer = NULL;

  stats_primary.start_time_ms = iohdlc_time_now_ms();
  stats_secondary.start_time_ms = stats_primary.start_time_ms;
  iohdlc_mutex_init(&stats_mutex_primary);
  iohdlc_mutex_init(&stats_mutex_secondary);

  if (adapter->init) {
    adapter->init();
    adapter_initialized = true;
  }

  test_printf("\r\n");
  test_printf("========================================\r\n");
  test_printf(both_endpoints ?
      "Initializing HDLC stations...\r\n" :
      "Initializing local HDLC endpoint...\r\n");
  test_printf("========================================\r\n\r\n");
  test_printf("Using adapter: %s\r\n", adapter->name);

  if (config.error_rate > 0 && adapter->configure_error_injection) {
    if (adapter->configure_error_injection(config.error_rate) != 0) {
      test_printf("Warning: Error injection not supported by adapter\r\n");
    }
  }

  if (endpoint_a_active) {
    if (adapter->get_port_a == NULL) {
      test_printf("Error: adapter '%s' does not provide endpoint A.\r\n",
                  adapter->name);
      return_code = 1;
      goto cleanup;
    }
    port_primary = adapter->get_port_a();
    if (port_primary.ctx == NULL || port_primary.ops == NULL) {
      test_printf("Error: adapter '%s' does not provide endpoint A.\r\n",
                  adapter->name);
      return_code = 1;
      goto cleanup;
    }
  }

  if (endpoint_b_active) {
    if (adapter->get_port_b == NULL) {
      test_printf("Error: adapter '%s' does not provide endpoint B.\r\n",
                  adapter->name);
      return_code = 1;
      goto cleanup;
    }
    port_secondary = adapter->get_port_b();
    if (port_secondary.ctx == NULL || port_secondary.ops == NULL) {
      test_printf("Error: adapter '%s' does not provide endpoint B.\r\n",
                  adapter->name);
      return_code = 1;
      goto cleanup;
    }
  }

  if (endpoint_a_active) {
    ioHdlcSwDriverInit(&driver_primary, NULL);

    station_config.mode = (config.mode == IOHDLC_OM_NRM) ?
        IOHDLC_OM_NDM : IOHDLC_OM_ADM;
    station_config.flags = IOHDLC_FLG_PRI |
        (config.use_twa ? IOHDLC_FLG_TWA : 0U);
    station_config.log2mod = log2mod;
    station_config.addr = PRIMARY_ADDR;
    station_config.driver = (ioHdlcDriver *)&driver_primary;
    station_config.frame_arena = s_exchange_arena_primary;
    station_config.frame_arena_size = sizeof s_exchange_arena_primary;
    station_config.max_info_len = 0;
    station_config.pool_watermark = 0;
    station_config.fff_type = 1;
    station_config.optfuncs = optfuncs;
    station_config.phydriver = &port_primary;
    station_config.phydriver_config = NULL;
    station_config.reply_timeout_ms = config.reply_timeout_ms;
    station_config.poll_retry_max = config.poll_retry_max;

    result = ioHdlcStationInit(&station_primary, &station_config);
    if (result != 0) {
      test_printf("Endpoint A station init failed: %d\r\n", result);
      return_code = 1;
      goto cleanup;
    }

    result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
    if (result != 0) {
      test_printf("Add peer to endpoint A failed: %d\r\n", result);
      return_code = 1;
      goto cleanup;
    }
    s_pri_peer = &peer_at_primary;
  }

  if (endpoint_b_active) {
    ioHdlcSwDriverInit(&driver_secondary, NULL);

    station_config.mode = (config.mode == IOHDLC_OM_NRM) ?
        IOHDLC_OM_NDM : IOHDLC_OM_ADM;
    station_config.flags = config.use_twa ? IOHDLC_FLG_TWA : 0U;
    station_config.log2mod = log2mod;
    station_config.addr = SECONDARY_ADDR;
    station_config.driver = (ioHdlcDriver *)&driver_secondary;
    station_config.frame_arena = s_exchange_arena_secondary;
    station_config.frame_arena_size = sizeof s_exchange_arena_secondary;
    station_config.max_info_len = 0;
    station_config.pool_watermark = 0;
    station_config.fff_type = 1;
    station_config.optfuncs = optfuncs;
    station_config.phydriver = &port_secondary;
    station_config.phydriver_config = NULL;
    station_config.reply_timeout_ms = config.reply_timeout_ms;
    station_config.poll_retry_max = config.poll_retry_max;

    result = ioHdlcStationInit(&station_secondary, &station_config);
    if (result != 0) {
      test_printf("Endpoint B station init failed: %d\r\n", result);
      return_code = 1;
      goto cleanup;
    }

    result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
    if (result != 0) {
      test_printf("Add peer to endpoint B failed: %d\r\n", result);
      return_code = 1;
      goto cleanup;
    }
    s_sec_peer = &peer_at_secondary;
  }

  if (config.krs != 0) {
    if (endpoint_a_active &&
        ioHdlcPeerSetWindow(&peer_at_primary, config.krs, config.krs) != 0) {
      test_printf("Error: --krs %u exceeds modmask\r\n", config.krs);
      return_code = 1;
      goto cleanup;
    }
    if (endpoint_b_active &&
        ioHdlcPeerSetWindow(&peer_at_secondary, config.krs, config.krs) != 0) {
      test_printf("Error: --krs %u exceeds modmask\r\n", config.krs);
      return_code = 1;
      goto cleanup;
    }
  }

  test_printf("Starting HDLC protocol runner%s...\r\n",
              both_endpoints ? "s" : "");
  if (endpoint_a_active) {
    result = ioHdlcRunnerStart(&station_primary);
    TEST_ASSERT(result == 0, "Failed to start endpoint A runner");
  }
  if (endpoint_b_active) {
    result = ioHdlcRunnerStart(&station_secondary);
    TEST_ASSERT(result == 0, "Failed to start endpoint B runner");
  }
  ioHdlc_sleep_ms(50);

  test_printf("Establishing connection...\r\n");
  if (endpoint_a_active) {
    result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, config.mode);
    if (result != 0) {
      test_printf("Link up failed: %d\r\n", result);
      return_code = 1;
      goto cleanup;
    }
  } else if (endpoint_b_active) {
    iohdlc_event_listener_t listener;
    eventflags_t flags = 0U;

    iohdlc_evt_register(&station_secondary.app_es, &listener, EVENT_MASK(0),
                        IOHDLC_APP_LINK_UP | IOHDLC_APP_LINK_REFUSED);
    if (!IOHDLC_PEER_DISC(&peer_at_secondary)) {
      flags = IOHDLC_APP_LINK_UP;
    } else {
      eventmask_t evt = iohdlc_evt_wait_any_timeout(
          EVENT_MASK(0),
          s_exchange_io_timeout_ms(&station_secondary, &peer_at_secondary));
      if (evt != 0U) {
        flags = iohdlc_evt_get_and_clear_flags(&listener);
      }
    }
    iohdlc_evt_unregister(&station_secondary.app_es, &listener);

    if (IOHDLC_PEER_DISC(&peer_at_secondary)) {
      test_printf((flags & IOHDLC_APP_LINK_REFUSED) != 0U ?
          "Connection refused\r\n" :
          "Connection not established\r\n");
      return_code = 1;
      goto cleanup;
    }
  }

  ioHdlc_sleep_ms(100);
  if ((endpoint_a_active && IOHDLC_PEER_DISC(&peer_at_primary)) ||
      (endpoint_b_active && IOHDLC_PEER_DISC(&peer_at_secondary))) {
    test_printf("Connection not established\r\n");
    return_code = 1;
    goto cleanup;
  }

  test_printf("Connection established\r\n\r\n");

  if (endpoint_a_active) {
    ctx_pri_writer.station = &station_primary;
    ctx_pri_writer.peer = &peer_at_primary;
    ctx_pri_writer.stats = &stats_primary;
    ctx_pri_writer.stats_mutex = &stats_mutex_primary;
    ctx_pri_writer.config = &config;
    ctx_pri_writer.buffer = s_pri_writer_buf;
    ctx_pri_writer.buffer_size = sizeof s_pri_writer_buf;
    ctx_pri_writer.enabled = (config.traffic_direction == TRAFFIC_PRI_TO_SEC ||
                              config.traffic_direction == TRAFFIC_BIDIRECTIONAL);

    ctx_pri_reader.station = &station_primary;
    ctx_pri_reader.peer = &peer_at_primary;
    ctx_pri_reader.stats = &stats_primary;
    ctx_pri_reader.stats_mutex = &stats_mutex_primary;
    ctx_pri_reader.config = &config;
    ctx_pri_reader.buffer = s_pri_reader_buf;
    ctx_pri_reader.buffer_size = sizeof s_pri_reader_buf;
    ctx_pri_reader.enabled = (config.traffic_direction == TRAFFIC_SEC_TO_PRI ||
                              config.traffic_direction == TRAFFIC_BIDIRECTIONAL);
  }

  if (endpoint_b_active) {
    ctx_sec_writer.station = &station_secondary;
    ctx_sec_writer.peer = &peer_at_secondary;
    ctx_sec_writer.stats = &stats_secondary;
    ctx_sec_writer.stats_mutex = &stats_mutex_secondary;
    ctx_sec_writer.config = &config;
    ctx_sec_writer.buffer = s_sec_writer_buf;
    ctx_sec_writer.buffer_size = sizeof s_sec_writer_buf;
    ctx_sec_writer.enabled = (config.traffic_direction == TRAFFIC_SEC_TO_PRI ||
                              config.traffic_direction == TRAFFIC_BIDIRECTIONAL);

    ctx_sec_reader.station = &station_secondary;
    ctx_sec_reader.peer = &peer_at_secondary;
    ctx_sec_reader.stats = &stats_secondary;
    ctx_sec_reader.stats_mutex = &stats_mutex_secondary;
    ctx_sec_reader.config = &config;
    ctx_sec_reader.buffer = s_sec_reader_buf;
    ctx_sec_reader.buffer_size = sizeof s_sec_reader_buf;
    ctx_sec_reader.enabled = (config.traffic_direction == TRAFFIC_PRI_TO_SEC ||
                              config.traffic_direction == TRAFFIC_BIDIRECTIONAL);
  }

  s_exchange_active_workers =
      (ctx_pri_writer.enabled ? 1U : 0U) +
      (ctx_pri_reader.enabled ? 1U : 0U) +
      (ctx_sec_writer.enabled ? 1U : 0U) +
      (ctx_sec_reader.enabled ? 1U : 0U);

  test_printf("========================================\r\n");
  test_printf("Starting data exchange...\r\n");
  test_printf("========================================\r\n\r\n");

  start_time = iohdlc_time_now_ms();

  if (ctx_pri_writer.enabled) {
    thread_pri_writer = iohdlc_thread_create("pri_writer", 0, 0,
                                             writer_thread, &ctx_pri_writer);
    if (thread_pri_writer == NULL) {
      iohdlc_mutex_lock(&s_exchange_state_mutex);
      if (s_exchange_active_workers > 0U)
        s_exchange_active_workers--;
      iohdlc_mutex_unlock(&s_exchange_state_mutex);
      test_printf("Failed to create endpoint A writer thread\r\n");
      thread_create_failed = true;
    }
  }
  if (ctx_pri_reader.enabled) {
    thread_pri_reader = iohdlc_thread_create("pri_reader", 0, 0,
                                             reader_thread, &ctx_pri_reader);
    if (thread_pri_reader == NULL) {
      iohdlc_mutex_lock(&s_exchange_state_mutex);
      if (s_exchange_active_workers > 0U)
        s_exchange_active_workers--;
      iohdlc_mutex_unlock(&s_exchange_state_mutex);
      test_printf("Failed to create endpoint A reader thread\r\n");
      thread_create_failed = true;
    }
  }
  if (ctx_sec_writer.enabled) {
    thread_sec_writer = iohdlc_thread_create("sec_writer", 0, 0,
                                             writer_thread, &ctx_sec_writer);
    if (thread_sec_writer == NULL) {
      iohdlc_mutex_lock(&s_exchange_state_mutex);
      if (s_exchange_active_workers > 0U)
        s_exchange_active_workers--;
      iohdlc_mutex_unlock(&s_exchange_state_mutex);
      test_printf("Failed to create endpoint B writer thread\r\n");
      thread_create_failed = true;
    }
  }
  if (ctx_sec_reader.enabled) {
    thread_sec_reader = iohdlc_thread_create("sec_reader", 0, 0,
                                             reader_thread, &ctx_sec_reader);
    if (thread_sec_reader == NULL) {
      iohdlc_mutex_lock(&s_exchange_state_mutex);
      if (s_exchange_active_workers > 0U)
        s_exchange_active_workers--;
      iohdlc_mutex_unlock(&s_exchange_state_mutex);
      test_printf("Failed to create endpoint B reader thread\r\n");
      thread_create_failed = true;
    }
  }

  if (thread_create_failed) {
    test_failed_global = true;
    test_request_stop();
    s_exchange_abort_peer(s_pri_peer);
    s_exchange_abort_peer(s_sec_peer);
  }

  while (!test_should_stop()) {
    iohdlc_mutex_lock(&s_exchange_state_mutex);
    active_workers = s_exchange_active_workers;
    iohdlc_mutex_unlock(&s_exchange_state_mutex);
    if (active_workers == 0U) {
      break;
    }

    ioHdlc_sleep_ms(config.progress_interval_ms);
    if (test_should_stop()) {
      break;
    }

    elapsed_time = (iohdlc_time_now_ms() - start_time) / 1000;

    if (config.duration_type == TEST_BY_TIME) {
      if (elapsed_time >= config.duration_value) {
        test_request_stop();
        break;
      }
      if (both_endpoints) {
        test_printf("Elapsed: %u/%u seconds | A: %u sent, %u rcv | B: %u sent, %u rcv\r\n",
                    elapsed_time, config.duration_value,
                    stats_primary.packets_sent, stats_primary.packets_received,
                    stats_secondary.packets_sent, stats_secondary.packets_received);
      } else {
        stats_local = endpoint_a_active ? &stats_primary : &stats_secondary;
        test_printf("Elapsed: %u/%u seconds | Local %s: %u sent, %u rcv\r\n",
                    elapsed_time, config.duration_value, local_label,
                    stats_local->packets_sent, stats_local->packets_received);
      }
    } else if (config.duration_type == TEST_BY_COUNT) {
      if (both_endpoints) {
        uint32_t expected_total = config.duration_value *
            config.exchanges_per_iteration;
        uint32_t current_sent = 0U;
        uint32_t current_rcv = 0U;

        if (config.traffic_direction == TRAFFIC_PRI_TO_SEC) {
          current_sent = stats_primary.packets_sent;
          current_rcv = stats_secondary.packets_received;
        } else if (config.traffic_direction == TRAFFIC_SEC_TO_PRI) {
          current_sent = stats_secondary.packets_sent;
          current_rcv = stats_primary.packets_received;
        } else {
          current_sent = stats_primary.packets_sent + stats_secondary.packets_sent;
          current_rcv = stats_primary.packets_received + stats_secondary.packets_received;
          expected_total *= 2U;
        }

        test_printf("Progress: %u/%u packets sent, %u rcv | A: %u/%u | B: %u/%u\r\n",
                    current_sent, expected_total, current_rcv,
                    stats_primary.packets_sent, stats_primary.packets_received,
                    stats_secondary.packets_sent, stats_secondary.packets_received);
      } else {
        const uint32_t expected = config.duration_value *
            config.exchanges_per_iteration;

        stats_local = endpoint_a_active ? &stats_primary : &stats_secondary;
        ctx_writer_local = endpoint_a_active ? &ctx_pri_writer : &ctx_sec_writer;
        ctx_reader_local = endpoint_a_active ? &ctx_pri_reader : &ctx_sec_reader;
        test_printf("Progress: Local %s TX %u/%u | RX %u/%u\r\n",
                    local_label,
                    stats_local->packets_sent,
                    ctx_writer_local->enabled ? expected : 0U,
                    stats_local->packets_received,
                    ctx_reader_local->enabled ? expected : 0U);
      }
    } else if (config.duration_type == TEST_INFINITE) {
      if (both_endpoints) {
        test_printf("Elapsed: %u seconds | A: %u sent, %u rcv | B: %u sent, %u rcv\r\n",
                    elapsed_time,
                    stats_primary.packets_sent, stats_primary.packets_received,
                    stats_secondary.packets_sent, stats_secondary.packets_received);
      } else {
        stats_local = endpoint_a_active ? &stats_primary : &stats_secondary;
        test_printf("Elapsed: %u seconds | Local %s: %u sent, %u rcv\r\n",
                    elapsed_time, local_label,
                    stats_local->packets_sent, stats_local->packets_received);
      }
    }
  }

  elapsed_time = (iohdlc_time_now_ms() - start_time) / 1000;
  stats_primary.end_time_ms = iohdlc_time_now_ms();
  stats_secondary.end_time_ms = stats_primary.end_time_ms;

  if (test_failed_global) {
    return_code = 1;
  }

  test_printf("\r\nStopping threads...\r\n");
  test_printf("join A writer...\r\n");
  iohdlc_thread_join(thread_pri_writer);
  test_printf("join A writer done\r\n");
  test_printf("join A reader...\r\n");
  iohdlc_thread_join(thread_pri_reader);
  test_printf("join A reader done\r\n");
  test_printf("join B writer...\r\n");
  iohdlc_thread_join(thread_sec_writer);
  test_printf("join B writer done\r\n");
  test_printf("join B reader...\r\n");
  iohdlc_thread_join(thread_sec_reader);
  test_printf("join B reader done\r\n");

  if (!test_failed_global && endpoint_a_active &&
      station_primary.c_peer != NULL &&
      !IOHDLC_PEER_DISC(&peer_at_primary)) {
    ioHdlcStationLinkDown(&station_primary, station_primary.c_peer->addr);
  }

  test_printf("\r\n");
  test_printf("========================================\r\n");
  test_printf("TEST COMPLETED\r\n");
  test_printf("========================================\r\n\r\n");
  test_printf("Total elapsed time: %u seconds\r\n\r\n", elapsed_time);

  if (both_endpoints) {
    test_printf("Endpoint A:\r\n");
    test_printf("  Packets sent:     %u\r\n", stats_primary.packets_sent);
    test_printf("  Packets received: %u\r\n", stats_primary.packets_received);
    test_printf("  Seq errors:       %u\r\n", stats_primary.packets_reordered);
    test_printf("  Bytes sent:       " U64_FMT "\r\n",
                U64_ARGS(stats_primary.total_bytes_sent));
    test_printf("  Bytes received:   " U64_FMT "\r\n",
                U64_ARGS(stats_primary.total_bytes_received));
    test_printf("\r\n");

    test_printf("Endpoint B:\r\n");
    test_printf("  Packets sent:     %u\r\n", stats_secondary.packets_sent);
    test_printf("  Packets received: %u\r\n", stats_secondary.packets_received);
    test_printf("  Seq errors:       %u\r\n", stats_secondary.packets_reordered);
    test_printf("  Bytes sent:       " U64_FMT "\r\n",
                U64_ARGS(stats_secondary.total_bytes_sent));
    test_printf("  Bytes received:   " U64_FMT "\r\n",
                U64_ARGS(stats_secondary.total_bytes_received));
    test_printf("\r\n");

#if defined(IOHDLC_ENABLE_STATISTICS)
    test_printf("Protocol Statistics (A -> B peer):\r\n");
    test_printf("  REJ received:     %u\r\n", peer_at_primary.stats.rej_received);
    test_printf("  Checkpoints:      %u\r\n", peer_at_primary.stats.checkpoints);
    test_printf("  Timeouts:         %u\r\n", peer_at_primary.stats.timeouts);
    test_printf("  Out of sequence:  %u\r\n", peer_at_primary.stats.out_of_sequence);
    test_printf("  Pool low water:   %u\r\n", peer_at_primary.stats.pool_low_water);
    test_printf("\r\n");

    test_printf("Protocol Statistics (B -> A peer):\r\n");
    test_printf("  REJ received:     %u\r\n", peer_at_secondary.stats.rej_received);
    test_printf("  Checkpoints:      %u\r\n", peer_at_secondary.stats.checkpoints);
    test_printf("  Timeouts:         %u\r\n", peer_at_secondary.stats.timeouts);
    test_printf("  Out of sequence:  %u\r\n", peer_at_secondary.stats.out_of_sequence);
    test_printf("  Pool low water:   %u\r\n", peer_at_secondary.stats.pool_low_water);
    test_printf("\r\n");
#endif

    if (config.traffic_direction == TRAFFIC_PRI_TO_SEC) {
      uint32_t lost = (stats_primary.packets_sent > stats_secondary.packets_received) ?
          (stats_primary.packets_sent - stats_secondary.packets_received) : 0U;
      float loss_percent = (stats_primary.packets_sent > 0U) ?
          (100.0f * lost / stats_primary.packets_sent) : 0.0f;
      float throughput = (elapsed_time > 0U) ?
          ((float)stats_secondary.total_bytes_received / elapsed_time) : 0.0f;

      test_printf("A -> B Traffic:\r\n");
      test_printf("  Sent:       %u packets (" U64_FMT " bytes)\r\n",
                  stats_primary.packets_sent,
                  U64_ARGS(stats_primary.total_bytes_sent));
      test_printf("  Received:   %u packets (" U64_FMT " bytes)\r\n",
                  stats_secondary.packets_received,
                  U64_ARGS(stats_secondary.total_bytes_received));
      test_printf("  Lost:       %u packets (%.2f%%)\r\n", lost, loss_percent);
      test_printf("  Throughput: %.2f bytes/s (%.2f KB/s)\r\n",
                  throughput, throughput / 1024.0f);
      test_printf("\r\n");
    } else if (config.traffic_direction == TRAFFIC_SEC_TO_PRI) {
      uint32_t lost = (stats_secondary.packets_sent > stats_primary.packets_received) ?
          (stats_secondary.packets_sent - stats_primary.packets_received) : 0U;
      float loss_percent = (stats_secondary.packets_sent > 0U) ?
          (100.0f * lost / stats_secondary.packets_sent) : 0.0f;
      float throughput = (elapsed_time > 0U) ?
          ((float)stats_primary.total_bytes_received / elapsed_time) : 0.0f;

      test_printf("B -> A Traffic:\r\n");
      test_printf("  Sent:       %u packets (" U64_FMT " bytes)\r\n",
                  stats_secondary.packets_sent,
                  U64_ARGS(stats_secondary.total_bytes_sent));
      test_printf("  Received:   %u packets (" U64_FMT " bytes)\r\n",
                  stats_primary.packets_received,
                  U64_ARGS(stats_primary.total_bytes_received));
      test_printf("  Lost:       %u packets (%.2f%%)\r\n", lost, loss_percent);
      test_printf("  Throughput: %.2f bytes/s (%.2f KB/s)\r\n",
                  throughput, throughput / 1024.0f);
      test_printf("\r\n");
    } else {
      uint32_t lost_a2b = (stats_primary.packets_sent > stats_secondary.packets_received) ?
          (stats_primary.packets_sent - stats_secondary.packets_received) : 0U;
      float loss_percent_a2b = (stats_primary.packets_sent > 0U) ?
          (100.0f * lost_a2b / stats_primary.packets_sent) : 0.0f;
      float throughput_a2b = (elapsed_time > 0U) ?
          ((float)stats_secondary.total_bytes_received / elapsed_time) : 0.0f;
      uint32_t lost_b2a = (stats_secondary.packets_sent > stats_primary.packets_received) ?
          (stats_secondary.packets_sent - stats_primary.packets_received) : 0U;
      float loss_percent_b2a = (stats_secondary.packets_sent > 0U) ?
          (100.0f * lost_b2a / stats_secondary.packets_sent) : 0.0f;
      float throughput_b2a = (elapsed_time > 0U) ?
          ((float)stats_primary.total_bytes_received / elapsed_time) : 0.0f;

      test_printf("A -> B Traffic:\r\n");
      test_printf("  Sent:       %u packets (" U64_FMT " bytes)\r\n",
                  stats_primary.packets_sent,
                  U64_ARGS(stats_primary.total_bytes_sent));
      test_printf("  Received:   %u packets (" U64_FMT " bytes)\r\n",
                  stats_secondary.packets_received,
                  U64_ARGS(stats_secondary.total_bytes_received));
      test_printf("  Lost:       %u packets (%.2f%%)\r\n",
                  lost_a2b, loss_percent_a2b);
      test_printf("  Throughput: %.2f bytes/s (%.2f KB/s)\r\n",
                  throughput_a2b, throughput_a2b / 1024.0f);
      test_printf("\r\n");

      test_printf("B -> A Traffic:\r\n");
      test_printf("  Sent:       %u packets (" U64_FMT " bytes)\r\n",
                  stats_secondary.packets_sent,
                  U64_ARGS(stats_secondary.total_bytes_sent));
      test_printf("  Received:   %u packets (" U64_FMT " bytes)\r\n",
                  stats_primary.packets_received,
                  U64_ARGS(stats_primary.total_bytes_received));
      test_printf("  Lost:       %u packets (%.2f%%)\r\n",
                  lost_b2a, loss_percent_b2a);
      test_printf("  Throughput: %.2f bytes/s (%.2f KB/s)\r\n",
                  throughput_b2a, throughput_b2a / 1024.0f);
      test_printf("\r\n");
    }
  } else {
    stats_local = endpoint_a_active ? &stats_primary : &stats_secondary;
    peer_local = endpoint_a_active ? &peer_at_primary : &peer_at_secondary;
    ctx_writer_local = endpoint_a_active ? &ctx_pri_writer : &ctx_sec_writer;
    ctx_reader_local = endpoint_a_active ? &ctx_pri_reader : &ctx_sec_reader;

    test_printf("Local Endpoint %s:\r\n", local_label);
    test_printf("  Packets sent:     %u\r\n", stats_local->packets_sent);
    test_printf("  Packets received: %u\r\n", stats_local->packets_received);
    test_printf("  Seq errors:       %u\r\n", stats_local->packets_reordered);
    test_printf("  Bytes sent:       " U64_FMT "\r\n",
                U64_ARGS(stats_local->total_bytes_sent));
    test_printf("  Bytes received:   " U64_FMT "\r\n",
                U64_ARGS(stats_local->total_bytes_received));
    test_printf("\r\n");

#if defined(IOHDLC_ENABLE_STATISTICS)
    test_printf("Protocol Statistics (Local peer %s -> %s):\r\n",
                local_label, remote_label);
    test_printf("  REJ received:     %u\r\n", peer_local->stats.rej_received);
    test_printf("  Checkpoints:      %u\r\n", peer_local->stats.checkpoints);
    test_printf("  Timeouts:         %u\r\n", peer_local->stats.timeouts);
    test_printf("  Out of sequence:  %u\r\n", peer_local->stats.out_of_sequence);
    test_printf("  Pool low water:   %u\r\n", peer_local->stats.pool_low_water);
    test_printf("\r\n");
#endif

    if (ctx_writer_local->enabled) {
      float tx_throughput = (elapsed_time > 0U) ?
          ((float)stats_local->total_bytes_sent / elapsed_time) : 0.0f;

      test_printf("Local %s -> %s Traffic:\r\n", local_label, remote_label);
      test_printf("  Sent:       %u packets (" U64_FMT " bytes)\r\n",
                  stats_local->packets_sent,
                  U64_ARGS(stats_local->total_bytes_sent));
      test_printf("  Throughput: %.2f bytes/s (%.2f KB/s)\r\n",
                  tx_throughput, tx_throughput / 1024.0f);
      test_printf("\r\n");
    }

    if (ctx_reader_local->enabled) {
      float rx_throughput = (elapsed_time > 0U) ?
          ((float)stats_local->total_bytes_received / elapsed_time) : 0.0f;

      test_printf("Local %s <- %s Traffic:\r\n", local_label, remote_label);
      test_printf("  Received:   %u packets (" U64_FMT " bytes)\r\n",
                  stats_local->packets_received,
                  U64_ARGS(stats_local->total_bytes_received));
      test_printf("  Throughput: %.2f bytes/s (%.2f KB/s)\r\n",
                  rx_throughput, rx_throughput / 1024.0f);
      test_printf("\r\n");
    }
  }

cleanup:
  if (endpoint_a_active)
    ioHdlcStationDeinit(&station_primary);
  if (endpoint_b_active)
    ioHdlcStationDeinit(&station_secondary);

  if (adapter_initialized && adapter->deinit) {
    adapter->deinit();
  }

  return return_code;
}
