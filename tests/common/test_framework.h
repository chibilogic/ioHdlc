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
 * @file    test_framework.h
 * @brief   Common test framework for parametrized HDLC testing.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>
#include "ioHdlc.h"

/*===========================================================================*/
/* Test Configuration                                                        */
/*===========================================================================*/

/**
 * @brief   Test duration type.
 */
typedef enum {
  TEST_BY_COUNT,      /**< Repeat N iterations */
  TEST_BY_TIME,       /**< Run for N seconds */
  TEST_INFINITE       /**< Run until manual stop */
} test_duration_type_t;

/**
 * @brief   Traffic direction.
 */
typedef enum {
  TRAFFIC_PRI_TO_SEC,     /**< Primary writes only */
  TRAFFIC_SEC_TO_PRI,     /**< Secondary writes only */
  TRAFFIC_BIDIRECTIONAL   /**< Both write */
} test_traffic_direction_t;

/**
 * @brief   Test configuration structure.
 */
typedef struct {
  /* Mode configuration */
  uint8_t mode;                         /**< IOHDLC_OM_NRM, ARM, ABM */
  bool use_twa;                         /**< true=TWA, false=TWS */
  uint16_t modulo;                      /**< HDLC modulus: 8 or 128 */
  
  /* Test duration */
  test_duration_type_t duration_type;   /**< How to measure duration */
  uint32_t duration_value;              /**< Count or seconds */
  
  /* Data exchange pattern */
  uint32_t exchanges_per_iteration;     /**< Writes per iteration */
  uint32_t bytes_per_exchange;          /**< Size of each write */
  
  /* Traffic direction */
  test_traffic_direction_t traffic_direction;
  
  /* Error injection */
  uint8_t error_rate;                   /**< Error rate 0-100% (0=disabled) */
  
  /* HDLC protocol parameters */
  uint16_t reply_timeout_ms;            /**< Reply timeout in ms (0=default 100ms) */
  uint8_t poll_retry_max;               /**< Max poll retries (0=default 5) */
  uint32_t krs;                         /**< Window size ks=kr (0=use modmask default) */
  
  /* Progress reporting */
  uint32_t progress_interval_ms;        /**< Progress update interval in ms (default: 1000) */
  
  /* Watermark testing */
  uint32_t watermark_delay_ms;          /**< Reader delay every 256 packets (ms), 0=disabled */
  
  /* Test name (for reporting) */
  const char *test_name;
} test_config_t;

/*===========================================================================*/
/* Test Packet Format                                                        */
/*===========================================================================*/

/**
 * @brief   Test packet header.
 * @details Simple packet format with sequence number and timestamp.
 *          No magic/checksum needed (HDLC FCS handles corruption).
 */
typedef struct __attribute__((packed)) {
  uint32_t sequence;      /**< Monotonic counter (detect loss/reorder) */
  uint32_t timestamp_ms;  /**< For latency measurement */
  uint16_t payload_len;   /**< Data length */
  uint8_t payload[];      /**< Variable length data */
} test_packet_t;

#define TEST_PACKET_HEADER_SIZE (sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t))

/*===========================================================================*/
/* Test Statistics                                                           */
/*===========================================================================*/

/**
 * @brief   Test statistics structure.
 */
typedef struct {
  /* Packet counters */
  uint32_t packets_sent;
  uint32_t packets_received;
  uint32_t packets_lost;
  uint32_t packets_reordered;
  
  /* Byte counters */
  uint64_t total_bytes_sent;
  uint64_t total_bytes_received;
  
  /* Latency tracking */
  uint32_t min_latency_ms;
  uint32_t max_latency_ms;
  uint64_t sum_latency_ms;  /**< For average calculation */
  
  /* Test duration */
  uint32_t start_time_ms;
  uint32_t end_time_ms;
} test_statistics_t;

/*===========================================================================*/
/* API Functions                                                             */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Parse test configuration from command line arguments.
 * @details Platform-specific implementation (Linux vs ChibiOS).
 *
 * @param[out] cfg      Configuration structure to fill
 * @param[in] argc      Argument count
 * @param[in] argv      Argument vector
 * @return              true if parsing successful, false on error
 */
bool test_parse_config(test_config_t *cfg, int argc, char **argv);

/**
 * @brief   Initialize test statistics.
 *
 * @param[out] stats    Statistics structure to initialize
 */
void test_init_statistics(test_statistics_t *stats);

/**
 * @brief   Generate test packet with sequence and payload.
 *
 * @param[in] seq           Sequence number
 * @param[in] payload_size  Size of payload (bytes)
 * @param[out] buffer       Buffer to write packet to
 * @param[in] buffer_size   Size of buffer
 * @return                  Total packet size, or 0 on error
 */
size_t test_generate_packet(uint32_t seq, uint32_t packet_size,
                             uint8_t *buffer, size_t buffer_size);

/**
 * @brief   Validate received test packet and update statistics.
 *
 * @param[in] buffer            Buffer containing received packet
 * @param[in] len               Length of received data
 * @param[in,out] expected_seq  Expected sequence number (updated by function)
 * @param[in,out] stats         Statistics to update
 * @return                      true if packet valid, false on error
 */
bool test_validate_packet(const uint8_t *buffer, size_t len,
                          uint32_t *expected_seq, test_statistics_t *stats);

/**
 * @brief   Print test configuration.
 *
 * @param[in] cfg           Configuration to print
 */
void test_print_config(const test_config_t *cfg);

/**
 * @brief   Print test statistics report.
 *
 * @param[in] stats         Statistics to print
 */
void test_print_statistics(const test_statistics_t *stats);

/**
 * @brief   Dump HDLC station and current peer state for debugging.
 *
 * @param[in] station       Station to dump
 * @param[in] label         Optional label for the dump (can be NULL)
 */
void test_dump_station_state(iohdlc_station_t *station, const char *label);

/*===========================================================================*/
/* Test Control API (OS-agnostic stop mechanism)                            */
/*===========================================================================*/

/**
 * @brief   Global test stop flag.
 * @details Set by test_request_stop() to signal test threads to terminate.
 *          On Linux: called from SIGINT handler (Ctrl-C).
 *          On ChibiOS: called from shell command or monitoring thread.
 */
extern volatile bool test_stop_requested;

/**
 * @brief   Request test to stop gracefully.
 * @note    Thread-safe: can be called from signal handlers or other threads.
 */
static inline void test_request_stop(void) {
  test_stop_requested = true;
}

/**
 * @brief   Check if test stop has been requested.
 * @return  true if stop requested, false otherwise
 */
static inline bool test_should_stop(void) {
  return test_stop_requested;
}

#ifdef __cplusplus
}
#endif

#endif /* TEST_FRAMEWORK_H */
