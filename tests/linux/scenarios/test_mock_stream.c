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
 * @file    test_mock_stream.c
 * @brief   Test mock stream functionality.
 *
 * @details Validates:
 *          - Mock stream creation/destruction
 *          - Read/write operations
 *          - Loopback mode
 *          - Peer connection (bidirectional)
 */

#include "../../common/test_helpers.h"
#include "mock_stream.h"
#include <string.h>

/*===========================================================================*/
/* Test: Mock Stream Creation                                                */
/*===========================================================================*/

static bool test_mock_stream_creation(void) {
  mock_stream_t *stream;

  /* Create stream with default config */
  stream = mock_stream_create(NULL);
  TEST_ASSERT_NOT_NULL(stream, "Failed to create mock stream");

  /* Cleanup */
  mock_stream_destroy(stream);

  return 0;
}

/*===========================================================================*/
/* Test: Loopback Mode                                                       */
/*===========================================================================*/

static bool test_loopback(void) {
  mock_stream_t *stream;
  mock_stream_config_t config = {
    .loopback = true,
    .inject_errors = false,
    .error_rate = 0,
    .delay_us = 0
  };
  uint8_t tx_data[] = "Hello, World!";
  uint8_t rx_data[32];
  ssize_t written, read_bytes;

  /* Create stream with loopback */
  stream = mock_stream_create(&config);
  TEST_ASSERT_NOT_NULL(stream, "Failed to create mock stream");

  /* Write data */
  written = mock_stream_write(stream, tx_data, sizeof(tx_data), 100);
  TEST_ASSERT_EQ(sizeof(tx_data), written, "Write failed");

  /* Read data back (loopback) */
  read_bytes = mock_stream_read(stream, rx_data, sizeof(rx_data), 100);
  TEST_ASSERT_EQ(sizeof(tx_data), read_bytes, "Read failed");
  TEST_ASSERT(test_mem_equal(tx_data, rx_data, sizeof(tx_data)), "Data mismatch");

  mock_stream_destroy(stream);
  return 0;
}

/*===========================================================================*/
/* Test: Peer Connection                                                     */
/*===========================================================================*/

static bool test_peer_connection(void) {
  mock_stream_t *stream_a, *stream_b;
  uint8_t msg_a[] = "From A to B";
  uint8_t msg_b[] = "From B to A";
  uint8_t buffer[32];
  ssize_t written, read_bytes;

  /* Create two streams */
  stream_a = mock_stream_create(NULL);
  stream_b = mock_stream_create(NULL);
  TEST_ASSERT_NOT_NULL(stream_a, "Failed to create stream A");
  TEST_ASSERT_NOT_NULL(stream_b, "Failed to create stream B");

  /* Connect them */
  mock_stream_connect(stream_a, stream_b);

  /* A writes, B reads */
  written = mock_stream_write(stream_a, msg_a, sizeof(msg_a), 100);
  TEST_ASSERT_EQ(sizeof(msg_a), written, "Stream A write failed");

  read_bytes = mock_stream_read(stream_b, buffer, sizeof(buffer), 100);
  TEST_ASSERT_EQ(sizeof(msg_a), read_bytes, "Stream B read failed");
  TEST_ASSERT(test_mem_equal(msg_a, buffer, sizeof(msg_a)), "A->B data mismatch");

  /* B writes, A reads */
  written = mock_stream_write(stream_b, msg_b, sizeof(msg_b), 100);
  TEST_ASSERT_EQ(sizeof(msg_b), written, "Stream B write failed");

  read_bytes = mock_stream_read(stream_a, buffer, sizeof(buffer), 100);
  TEST_ASSERT_EQ(sizeof(msg_b), read_bytes, "Stream A read failed");
  TEST_ASSERT(test_mem_equal(msg_b, buffer, sizeof(msg_b)), "B->A data mismatch");

  /* Disconnect and cleanup */
  mock_stream_disconnect(stream_a);
  mock_stream_destroy(stream_a);
  mock_stream_destroy(stream_b);

  return 0;
}

/*===========================================================================*/
/* Test: Read Timeout                                                        */
/*===========================================================================*/

static bool test_read_timeout(void) {
  mock_stream_t *stream;
  uint8_t buffer[32];
  ssize_t read_bytes;

  stream = mock_stream_create(NULL);
  TEST_ASSERT_NOT_NULL(stream, "Failed to create mock stream");

  /* Try to read from empty stream with timeout */
  read_bytes = mock_stream_read(stream, buffer, sizeof(buffer), 50);
  TEST_ASSERT_EQ(0, read_bytes, "Should timeout and return 0");

  mock_stream_destroy(stream);
  return 0;
}

/*===========================================================================*/
/* Test: Inject and Drain                                                    */
/*===========================================================================*/

static bool test_inject_drain(void) {
  mock_stream_t *stream;
  uint8_t inject_data[] = {0xAA, 0xBB, 0xCC, 0xDD};
  uint8_t rx_buffer[16];
  uint8_t tx_buffer[16];
  size_t injected, drained;

  stream = mock_stream_create(NULL);
  TEST_ASSERT_NOT_NULL(stream, "Failed to create mock stream");

  /* Inject data into RX buffer */
  injected = mock_stream_inject_rx(stream, inject_data, sizeof(inject_data));
  TEST_ASSERT_EQ(sizeof(inject_data), injected, "Inject failed");

  /* Read injected data */
  ssize_t read_bytes = mock_stream_read(stream, rx_buffer, sizeof(rx_buffer), 100);
  TEST_ASSERT_EQ(sizeof(inject_data), read_bytes, "Read failed");
  TEST_ASSERT(test_mem_equal(inject_data, rx_buffer, sizeof(inject_data)), "Injected data mismatch");

  /* Write data and drain TX buffer */
  mock_stream_write(stream, inject_data, sizeof(inject_data), 100);
  drained = mock_stream_drain_tx(stream, tx_buffer, sizeof(tx_buffer));
  TEST_ASSERT_EQ(sizeof(inject_data), drained, "Drain failed");
  TEST_ASSERT(test_mem_equal(inject_data, tx_buffer, sizeof(inject_data)), "Drained data mismatch");

  mock_stream_destroy(stream);
  return 0;
}

/*===========================================================================*/
/* Main Test Runner                                                          */
/*===========================================================================*/

int main(void) {
  printf("\n");
  printf("═══════════════════════════════════════════════\n");
  printf("  ioHdlc Test Suite - Mock Stream\n");
  printf("═══════════════════════════════════════════════\n\n");

  RUN_TEST(test_mock_stream_creation);
  RUN_TEST(test_loopback);
  RUN_TEST(test_peer_connection);
  RUN_TEST(test_read_timeout);
  RUN_TEST(test_inject_drain);

  printf("═══════════════════════════════════════════════\n");
  printf("  Test Summary\n");
  printf("═══════════════════════════════════════════════\n");
  printf("  Passed: %d\n", test_successes);
  printf("  Failed: %d\n", test_failures);
  printf("═══════════════════════════════════════════════\n\n");

  return (test_failures == 0) ? 0 : 1;
}
