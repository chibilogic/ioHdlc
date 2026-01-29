/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU Lesser General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
 */
/**
 * @file    test_basic_connection_twa.c
 * @brief   Test basic HDLC connection establishment in TWA transmission mode.
 *
 * @details Validates:
 *          - Data exexchange in Two-Way Alternate (TWA) mode
 */

#include "../../common/test_helpers.h"
#include "../../common/test_arenas.h"
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcqueue.h"
#include "ioHdlcswdriver.h"
#include "ioHdlc_runner.h"
#include "ioHdlcfmempool.h"
#include <string.h>
#include <errno.h>

#ifdef IOHDLC_USE_CHIBIOS
#include "../../chibios/mocks/mock_stream_chibios.h"
#include "../../chibios/mocks/mock_stream_adapter.h"
#else
#include "../../linux/mocks/mock_stream.h"
#include "../../linux/mocks/mock_stream_adapter.h"
#include <pthread.h>
#include <unistd.h>
#endif

/*===========================================================================*/
/* Test Configuration                                                        */
/*===========================================================================*/

#define PRIMARY_ADDR    0x01
#define SECONDARY_ADDR  0x02
#define WINDOW_SIZE     7
#define FRAME_SIZE      128  /* Frame pool frame size for tests */

/*===========================================================================*/
/* Test Helpers                                                              */
/*===========================================================================*/

/*===========================================================================*/
/* Test: Data Exchange                                                       */
/*===========================================================================*/

/**
 * @brief   Test bidirectional data exchange after SNRM handshake.
 * @details Validates:
 *          - I-frame transmission from primary to secondary
 *          - I-frame reception and content verification
 *          - Echo response from secondary to primary
 *          - Complete round-trip data integrity
 */
int test_data_exchange_twa(void) {
  int test_result = 0;  /* Success by default, set to 1 on failure */
  
  /* Test message */
  const char *test_msg = "Hello ioHdlc, welcome in the HDLC world.";
  size_t msg_len = strlen(test_msg);
  
  /* Setup: same as test_snrm_handshake */
  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  int32_t result;
  
  /* Create mock streams */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = false,
    .error_rate = 0,
    .delay_us = 100
  };
  
  stream_primary = mock_stream_create(&stream_config);
  stream_secondary = mock_stream_create(&stream_config);
  TEST_ASSERT_GOTO(stream_primary != NULL && stream_secondary != NULL, "Stream creation failed");
  mock_stream_connect(stream_primary, stream_secondary);
  
  /* Create adapters */
  adapter_primary = mock_stream_adapter_create(stream_primary);
  adapter_secondary = mock_stream_adapter_create(stream_secondary);
  TEST_ASSERT_GOTO(adapter_primary != NULL && adapter_secondary != NULL, "Adapter creation failed");
  
  /* Get ports */
  ioHdlcStreamPort port_primary = mock_stream_adapter_get_port(adapter_primary);
  ioHdlcStreamPort port_secondary = mock_stream_adapter_get_port(adapter_secondary);
  
  /* Initialize stream drivers */
  ioHdlcSwDriverInit(&driver_primary);
  ioHdlcSwDriverInit(&driver_secondary);
  
  /* Configure primary station */
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI | IOHDLC_FLG_TWA;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.max_info_len = 0;  /* Auto */
  config.pool_watermark = 0;  /* Auto: 10% min 8 */
  config.fff_type = 1;  /* TYPE0 */
  config.optfuncs = NULL;
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");
  
  /* Configure secondary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_TWA;
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = sizeof shared_arena_secondary;
  config.max_info_len = 0;  /* Auto */
  config.pool_watermark = 0;  /* Auto: 10% min 8 */
  config.fff_type = 1;  /* TYPE0 */
  config.optfuncs = NULL;
  config.phydriver = &port_secondary;
  config.phydriver_config = NULL;
  
  memset(&station_secondary, 0, sizeof station_secondary);
  result = ioHdlcStationInit(&station_secondary, &config);
  TEST_ASSERT(result == 0, "Secondary station init failed");
  
  /* Add peers */
  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Add peer to primary failed");
  
  result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Add peer to secondary failed");
  
  /* Start runner threads */
  ioHdlcRunnerStart(&station_primary);
  ioHdlcRunnerStart(&station_secondary);
  
#ifdef IOHDLC_USE_CHIBIOS
  chThdSleepMilliseconds(50);
#else
  usleep(50000);
#endif
  
  /* Establish connection (SNRM handshake) */
  int ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  if (ret != 0) {
    test_printf("LinkUp returned error: %d\n", ret);
  }
  TEST_ASSERT_GOTO(ret == 0, "LinkUp failed");
  
#ifdef IOHDLC_USE_CHIBIOS
  chThdSleepMilliseconds(100);
#else
  usleep(100000);
#endif
  
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  
  test_printf("Connection established, starting data exchange...\n");
  
  /* Declare buffers */
  char recv_buf[128];
  char echo_buf[128];
  
  /* Primary sends message to secondary */
  int i;
  ssize_t sent;

  test_printf("Primary sending %zu bytes...\n", msg_len*10);
  for (i = 0; i < 10; ++i) {
    sent = ioHdlcWriteTmo(&peer_at_primary, test_msg, msg_len, 2000);
    if (sent != (ssize_t)msg_len) {
      test_printf("❌ Primary write returned %zd (expected %zu), errno=%d\n", 
                  sent, msg_len, iohdlc_errno);
    }
    TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Primary write failed");
    test_printf("Primary sent %zd bytes\n", sent);
  }
#ifdef IOHDLC_USE_CHIBIOS
  chThdSleepMilliseconds(500);
#else
  usleep(500000);
#endif

  /* Secondary receives message */
  memset(recv_buf, 0, sizeof recv_buf);
  ssize_t received;
  for (i = 0; i < 10; ++i) {
    received = ioHdlcReadTmo(&peer_at_secondary, recv_buf, msg_len, 2000);
    test_printf("Secondary read returned %zd bytes (expected %zu), errno=%d\n",
                received, msg_len, iohdlc_errno);
    if (received > 0 && received <= (ssize_t)sizeof recv_buf) {
      /* Null-terminate for printing */
      recv_buf[received < (ssize_t)sizeof recv_buf ? (size_t)received : sizeof recv_buf-1] = '\0';
      test_printf("  Data: \"%s\"\n", recv_buf);
      /* Also print hex for first 20 bytes to debug */
      test_printf("  Hex: ");
      for (ssize_t i = 0; i < received && i < 20; i++) {
        test_printf("%02x ", (unsigned char)recv_buf[i]);
      }
      test_printf("\n");
    }
  }
  TEST_ASSERT_GOTO(received == (ssize_t)msg_len, "Secondary read failed");
  TEST_ASSERT_GOTO(memcmp(recv_buf, test_msg, msg_len) == 0, "Received data mismatch");
  test_printf("Secondary received %zd bytes: \"%s\"\n", received, recv_buf);
  
  /* Secondary echoes message back to primary */
  sent = ioHdlcWriteTmo(&peer_at_secondary, recv_buf, received, 2000);
  TEST_ASSERT_GOTO(sent == received, "Secondary echo write failed");
  test_printf("Secondary echoed %zd bytes\n", sent);
  
  /* Primary receives echo */
  memset(echo_buf, 0, sizeof echo_buf);
  received = ioHdlcReadTmo(&peer_at_primary, echo_buf, 40 /*sizeof echo_buf - 1*/, 2000);
  TEST_ASSERT_GOTO(received == (ssize_t)msg_len, "Primary echo read failed");
  TEST_ASSERT_GOTO(memcmp(echo_buf, test_msg, msg_len) == 0, "Echo data mismatch");
  test_printf("Primary received echo %zd bytes: \"%s\"\n", received, echo_buf);
  
#ifdef IOHDLC_USE_CHIBIOS
  chThdSleepMilliseconds(200);
#else
  usleep(200000);
#endif

  test_printf("✅ Data exchange completed successfully\n");
  
  /* Disconnect */
  ret = ioHdlcStationLinkDown(&station_primary, SECONDARY_ADDR);
  TEST_ASSERT_GOTO(ret == 0, "LinkDown failed");
  
test_cleanup:
#ifdef IOHDLC_USE_CHIBIOS
  chThdSleepMilliseconds(200);
#else
  usleep(200000);
#endif
  /* Stop runners */
  ioHdlcRunnerStop(&station_primary);
  ioHdlcRunnerStop(&station_secondary);
  
  /* Cleanup */
  mock_stream_adapter_destroy(adapter_primary);
  mock_stream_adapter_destroy(adapter_secondary);
  mock_stream_destroy(stream_primary);
  mock_stream_destroy(stream_secondary);
  
  return test_result;
}

/*===========================================================================*/
/* Main Test Runner                                                          */
/*===========================================================================*/

#ifndef IOHDLC_USE_CHIBIOS
/* Standalone test main for Linux/POSIX */
int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Basic Connection (TWA)\n");
  test_printf("═══════════════════════════════════════════════\n\n");

  RUN_TEST(test_data_exchange_twa);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
#endif /* IOHDLC_USE_CHIBIOS */
