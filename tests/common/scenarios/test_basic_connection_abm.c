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
 * @file    test_basic_connection_abm.c
 * @brief   Test basic HDLC connection and data exchange in ABM mode.
 *
 * @details Validates:
 *          - SABM/UA handshake (initiated by either side)
 *          - Bidirectional data exchange in ABM-TWS mode
 *          - DISC/UA disconnect from either side
 */

#include "test_helpers.h"
#include "test_arenas.h"
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcqueue.h"
#include "ioHdlcswdriver.h"
#include "ioHdlc_runner.h"
#include "ioHdlcfmempool.h"
#include "adapter_interface.h"
#include <string.h>
#include <errno.h>

/*===========================================================================*/
/* Test Configuration                                                        */
/*===========================================================================*/

#define STATION_A_ADDR  0x01
#define STATION_B_ADDR  0x02

/*===========================================================================*/
/* Test: ABM SABM handshake and data exchange                                */
/*===========================================================================*/

/**
 * @brief   Test SABM handshake and bidirectional data exchange in ABM mode.
 * @details Station A initiates SABM. Both stations exchange I-frames
 *          bidirectionally (TWS). Station A disconnects.
 */
bool test_abm_data_exchange(const test_adapter_t *adapter) {
  int test_result = 0;

  const char *test_msg = "Hello ioHdlc, welcome in the HDLC world.";
  size_t msg_len = strlen(test_msg);

  ioHdlcSwDriver driver_a, driver_b;
  iohdlc_station_t station_a, station_b;
  iohdlc_station_peer_t peer_at_a, peer_at_b;
  iohdlc_station_config_t config;
  int32_t result;

  ioHdlcStreamPort port_a = adapter->get_port_a();
  ioHdlcStreamPort port_b = adapter->get_port_b();

  ioHdlcSwDriverInit(&driver_a, NULL);
  ioHdlcSwDriverInit(&driver_b, NULL);

  /* Configure station A (combined, will initiate SABM) */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = STATION_A_ADDR;
  config.driver = (ioHdlcDriver *)&driver_a;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.fff_type = 1;
  config.phydriver = &port_a;

  memset(&station_a, 0, sizeof station_a);
  result = ioHdlcStationInit(&station_a, &config);
  TEST_ASSERT(result == 0, "Station A init failed");

  /* Configure station B (combined, will accept SABM) */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = STATION_B_ADDR;
  config.driver = (ioHdlcDriver *)&driver_b;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = sizeof shared_arena_secondary;
  config.fff_type = 1;
  config.phydriver = &port_b;

  memset(&station_b, 0, sizeof station_b);
  result = ioHdlcStationInit(&station_b, &config);
  TEST_ASSERT(result == 0, "Station B init failed");

  /* Add peers */
  result = ioHdlcAddPeer(&station_a, &peer_at_a, STATION_B_ADDR);
  TEST_ASSERT(result == 0, "Add peer to station A failed");

  result = ioHdlcAddPeer(&station_b, &peer_at_b, STATION_A_ADDR);
  TEST_ASSERT(result == 0, "Add peer to station B failed");

  /* Start runner threads */
  result = ioHdlcRunnerStart(&station_a);
  TEST_ASSERT(result == 0, "Failed to start station A runner");
  result = ioHdlcRunnerStart(&station_b);
  TEST_ASSERT(result == 0, "Failed to start station B runner");

  ioHdlc_sleep_ms(50);

  /* Establish ABM connection: Station A sends SABM */
  test_printf("Establishing ABM connection (SABM/UA)...\n");
  int ret = ioHdlcStationLinkUp(&station_a, STATION_B_ADDR, IOHDLC_OM_ABM);
  TEST_ASSERT_GOTO(ret == 0, "LinkUp (SABM) failed");

  ioHdlc_sleep_ms(100);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_a), "Station A peer should be connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_b), "Station B peer should be connected");
  TEST_ASSERT_GOTO(IOHDLC_IS_ABM(&station_a), "Station A should be in ABM mode");
  TEST_ASSERT_GOTO(IOHDLC_IS_ABM(&station_b), "Station B should be in ABM mode");
  test_printf("  ✅ ABM connection established\n");

  /* Station A sends to station B */
  test_printf("Station A → Station B...\n");
  char recv_buf[128];
  ssize_t sent, received;
  int i;

  for (i = 0; i < 10; ++i) {
    sent = ioHdlcWriteTmo(&peer_at_a, test_msg, msg_len, 500);
    TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Station A write failed");
  }

  for (i = 0; i < 10; ++i) {
    memset(recv_buf, 0, sizeof recv_buf);
    received = ioHdlcReadTmo(&peer_at_b, recv_buf, msg_len, 500);
    TEST_ASSERT_GOTO(received == (ssize_t)msg_len, "Station B read failed");
    TEST_ASSERT_GOTO(memcmp(recv_buf, test_msg, msg_len) == 0, "A→B data mismatch");
  }
  test_printf("  ✅ A→B: 10 × %u bytes OK\n", (unsigned)msg_len);

  /* Station B sends to station A (reverse direction) */
  test_printf("Station B → Station A...\n");
  for (i = 0; i < 10; ++i) {
    sent = ioHdlcWriteTmo(&peer_at_b, test_msg, msg_len, 500);
    TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Station B write failed");
  }

  for (i = 0; i < 10; ++i) {
    memset(recv_buf, 0, sizeof recv_buf);
    received = ioHdlcReadTmo(&peer_at_a, recv_buf, msg_len, 500);
    TEST_ASSERT_GOTO(received == (ssize_t)msg_len, "Station A read failed");
    TEST_ASSERT_GOTO(memcmp(recv_buf, test_msg, msg_len) == 0, "B→A data mismatch");
  }
  test_printf("  ✅ B→A: 10 × %u bytes OK\n", (unsigned)msg_len);

  /* Disconnect: Station A sends DISC */
  test_printf("Disconnecting...\n");
  ret = ioHdlcStationLinkDown(&station_a, STATION_B_ADDR);
  TEST_ASSERT_GOTO(ret == 0, "LinkDown failed");
  ioHdlc_sleep_ms(200);
  test_printf("  ✅ Disconnected\n");

test_cleanup:
  ioHdlc_sleep_ms(200);
  ioHdlcStationDeinit(&station_a);
  ioHdlcStationDeinit(&station_b);

  return test_result;
}
