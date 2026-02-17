/*
 * ioHdlc Test Framework - ChibiOS Configuration Parser
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
/**
 * @file    test_config_chibios.c
 * @brief   ChibiOS compile-time configuration parser.
 * @details Configuration is set via Makefile defines at compile time.
 *          Example: make TEST_MODE=NRM TEST_SIZE=120 TEST_COUNT=100
 */

#include "test_framework.h"

/*===========================================================================*/
/* Compile-Time Configuration Defaults                                      */
/*===========================================================================*/

/**
 * @brief   Operating mode (override with -DTEST_MODE=IOHDLC_OM_xxx)
 */
#ifndef TEST_MODE
#define TEST_MODE IOHDLC_OM_NRM
#endif

/**
 * @brief   Two-Way Alternate flag (override with -DTEST_USE_TWA=1)
 */
#ifndef TEST_USE_TWA
#define TEST_USE_TWA 0
#endif

/**
 * @brief   Test duration type (override with -DTEST_DURATION_TYPE=xxx)
 */
#ifndef TEST_DURATION_TYPE
#define TEST_DURATION_TYPE TEST_BY_COUNT
#endif

/**
 * @brief   Test duration value in iterations or seconds
 */
#ifndef TEST_DURATION_VALUE
#define TEST_DURATION_VALUE 10
#endif

/**
 * @brief   Number of exchanges per iteration
 */
#ifndef TEST_EXCHANGES
#define TEST_EXCHANGES 10
#endif

/**
 * @brief   Packet size in bytes (max 120 for TYPE0 FFF)
 */
#ifndef TEST_PACKET_SIZE
#define TEST_PACKET_SIZE 64
#endif

/**
 * @brief   Traffic direction (0=PRI->SEC, 1=SEC->PRI, 2=BIDIRECTIONAL)
 */
#ifndef TEST_DIRECTION
#define TEST_DIRECTION TRAFFIC_BIDIRECTIONAL
#endif

/**
 * @brief   Error injection rate 0-100%
 */
#ifndef TEST_ERROR_RATE
#define TEST_ERROR_RATE 0
#endif

/**
 * @brief   Reply timeout in milliseconds (0 = use default 100ms)
 */
#ifndef TEST_REPLY_TIMEOUT
#define TEST_REPLY_TIMEOUT 0
#endif

/**
 * @brief   Max poll retries (0 = use default 5)
 */
#ifndef TEST_POLL_RETRY_MAX
#define TEST_POLL_RETRY_MAX 0
#endif

/**
 * @brief   Progress update interval in milliseconds
 */
#ifndef TEST_PROGRESS_INTERVAL
#define TEST_PROGRESS_INTERVAL 1000
#endif

/**
 * @brief   Test name for reporting
 */
#ifndef TEST_NAME
#define TEST_NAME "ChibiOS Test"
#endif

/*===========================================================================*/
/* Configuration Parser Implementation                                       */
/*===========================================================================*/

/**
 * @brief   Parse test configuration from compile-time defines.
 * @note    argc/argv are ignored on ChibiOS (kept for API compatibility).
 *
 * @param[out] cfg      Configuration structure to fill
 * @param[in] argc      Argument count (ignored)
 * @param[in] argv      Argument vector (ignored)
 * @return              true (always succeeds with compile-time config)
 */
bool test_parse_config(test_config_t *cfg, int argc, char **argv) {
  (void)argc;  /* Unused on ChibiOS */
  (void)argv;  /* Unused on ChibiOS */
  
  /* Set configuration from compile-time defines */
  cfg->mode = TEST_MODE;
  cfg->use_twa = (TEST_USE_TWA != 0);
  cfg->duration_type = TEST_DURATION_TYPE;
  cfg->duration_value = TEST_DURATION_VALUE;
  cfg->exchanges_per_iteration = TEST_EXCHANGES;
  cfg->bytes_per_exchange = TEST_PACKET_SIZE;
  cfg->traffic_direction = TEST_DIRECTION;
  cfg->error_rate = TEST_ERROR_RATE;
  cfg->reply_timeout_ms = TEST_REPLY_TIMEOUT;
  cfg->poll_retry_max = TEST_POLL_RETRY_MAX;
  cfg->progress_interval_ms = TEST_PROGRESS_INTERVAL;
  cfg->test_name = TEST_NAME;
  
  return true;
}
