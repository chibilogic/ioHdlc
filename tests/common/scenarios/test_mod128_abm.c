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
 * @file    test_mod128_abm.c
 * @brief   Standalone modulo-128 ABM/TWS test.
 *
 * @details Validates:
 *          - SABM/UA handshake in ABM
 *          - modulo 128 station configuration
 *          - data transfer beyond sequence 127
 *          - wrap-around from 127 to 0 on protocol state variables
 */

#include "test_framework.h"
#include "test_helpers.h"
#include "test_arenas.h"
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcswdriver.h"
#include "ioHdlc_runner.h"
#include "adapter_interface.h"
#include <errno.h>
#include <string.h>

/*===========================================================================*/
/* Test Configuration                                                        */
/*===========================================================================*/

#define STATION_A_ADDR      0x01
#define STATION_B_ADDR      0x02
#define MOD128_PACKET_SIZE  48
#define MOD128_WRAP_COUNT   128U
#define MOD128_EXTRA_COUNT  32U
#define MOD128_TOTAL_COUNT  (MOD128_WRAP_COUNT + MOD128_EXTRA_COUNT)
#define MOD128_ARENA_SIZE   32768U

/*===========================================================================*/
/* Helpers                                                                   */
/*===========================================================================*/

static int send_and_check(iohdlc_station_peer_t *tx_peer,
                          iohdlc_station_peer_t *rx_peer,
                          uint32_t seq) {
  uint8_t tx_buf[MOD128_PACKET_SIZE];
  uint8_t rx_buf[MOD128_PACKET_SIZE];
  size_t packet_size;
  ssize_t transferred;

  packet_size = test_generate_packet(seq, MOD128_PACKET_SIZE, tx_buf, sizeof tx_buf);
  if (packet_size != MOD128_PACKET_SIZE) {
    test_printf("Packet generation failed at seq=%u\r\n", seq);
    return 1;
  }

  transferred = ioHdlcWriteTmo(tx_peer, tx_buf, packet_size, 500);
  if (transferred != (ssize_t)packet_size) {
    test_printf("Write failed at seq=%u (err=%d)\r\n", seq, iohdlc_errno);
    return 1;
  }

  memset(rx_buf, 0, sizeof rx_buf);
  transferred = ioHdlcReadTmo(rx_peer, rx_buf, packet_size, 500);
  if (transferred != (ssize_t)packet_size) {
    test_printf("Read failed at seq=%u (err=%d)\r\n", seq, iohdlc_errno);
    return 1;
  }

  if (memcmp(tx_buf, rx_buf, packet_size) != 0) {
    test_printf("Payload mismatch at seq=%u\r\n", seq);
    return 1;
  }

  return 0;
}

/*===========================================================================*/
/* Test: Modulo 128 ABM/TWS                                                  */
/*===========================================================================*/

bool test_abm_mod128_wraparound(const test_adapter_t *adapter) {
  int test_result = 0;
  ioHdlcSwDriver driver_a, driver_b;
  iohdlc_station_t station_a, station_b;
  iohdlc_station_peer_t peer_at_a, peer_at_b;
  iohdlc_station_config_t config;
  static uint8_t arena_a[MOD128_ARENA_SIZE];
  static uint8_t arena_b[MOD128_ARENA_SIZE];
  int32_t result;
  ioHdlcStreamPort port_a = adapter->get_port_a();
  ioHdlcStreamPort port_b = adapter->get_port_b();
  uint32_t seq;

  ioHdlcSwDriverInit(&driver_a, NULL);
  ioHdlcSwDriverInit(&driver_b, NULL);

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
  config.log2mod = 7;
  config.addr = STATION_A_ADDR;
  config.driver = (ioHdlcDriver *)&driver_a;
  config.frame_arena = arena_a;
  config.frame_arena_size = sizeof arena_a;
  config.fff_type = 1;
  config.phydriver = &port_a;

  memset(&station_a, 0, sizeof station_a);
  result = ioHdlcStationInit(&station_a, &config);
  TEST_ASSERT_GOTO(result == 0, "Station A init failed");

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
  config.log2mod = 7;
  config.addr = STATION_B_ADDR;
  config.driver = (ioHdlcDriver *)&driver_b;
  config.frame_arena = arena_b;
  config.frame_arena_size = sizeof arena_b;
  config.fff_type = 1;
  config.phydriver = &port_b;

  memset(&station_b, 0, sizeof station_b);
  result = ioHdlcStationInit(&station_b, &config);
  TEST_ASSERT_GOTO(result == 0, "Station B init failed");

  result = ioHdlcAddPeer(&station_a, &peer_at_a, STATION_B_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to station A failed");

  result = ioHdlcAddPeer(&station_b, &peer_at_b, STATION_A_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to station B failed");

  result = ioHdlcRunnerStart(&station_a);
  TEST_ASSERT_GOTO(result == 0, "Failed to start station A runner");

  result = ioHdlcRunnerStart(&station_b);
  TEST_ASSERT_GOTO(result == 0, "Failed to start station B runner");

  ioHdlc_sleep_ms(50);

  test_printf("Establishing ABM modulo-128 connection...\r\n");
  result = ioHdlcStationLinkUp(&station_a, STATION_B_ADDR, IOHDLC_OM_ABM);
  TEST_ASSERT_GOTO(result == 0, "LinkUp (SABM) failed");

  ioHdlc_sleep_ms(100);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_a), "Station A peer should be connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_b), "Station B peer should be connected");
  TEST_ASSERT_GOTO(IOHDLC_IS_ABM(&station_a), "Station A should be in ABM mode");
  TEST_ASSERT_GOTO(IOHDLC_IS_ABM(&station_b), "Station B should be in ABM mode");
  TEST_ASSERT_GOTO(station_a.framing.modmask == 127, "Station A should use modulo 128");
  TEST_ASSERT_GOTO(station_b.framing.modmask == 127, "Station B should use modulo 128");
  TEST_ASSERT_GOTO(station_a.framing.ctrl_size == 2, "Station A should use 2-byte control field");
  TEST_ASSERT_GOTO(station_b.framing.ctrl_size == 2, "Station B should use 2-byte control field");

  test_printf("Sending %u packets A -> B...\r\n", MOD128_TOTAL_COUNT);
  for (seq = 0; seq < MOD128_WRAP_COUNT; ++seq) {
    TEST_ASSERT_GOTO(send_and_check(&peer_at_a, &peer_at_b, seq) == 0,
                     "A -> B transfer failed before wrap");
  }

  ioHdlc_sleep_ms(50);
  TEST_ASSERT_GOTO(peer_at_a.vs == 0, "A sender V(S) should wrap to 0 after 128 frames");
  TEST_ASSERT_GOTO(peer_at_a.nr == 0, "A sender N(R) should wrap to 0 after 128 frames");
  TEST_ASSERT_GOTO(peer_at_b.vr == 0, "B receiver V(R) should wrap to 0 after 128 frames");

  for (seq = MOD128_WRAP_COUNT; seq < MOD128_TOTAL_COUNT; ++seq) {
    TEST_ASSERT_GOTO(send_and_check(&peer_at_a, &peer_at_b, seq) == 0,
                     "A -> B transfer failed after wrap");
  }

  ioHdlc_sleep_ms(50);
  TEST_ASSERT_GOTO(peer_at_a.vs == MOD128_EXTRA_COUNT,
                   "A sender V(S) should advance past wrap");
  TEST_ASSERT_GOTO(peer_at_a.nr == MOD128_EXTRA_COUNT,
                   "A sender N(R) should advance past wrap");
  TEST_ASSERT_GOTO(peer_at_b.vr == MOD128_EXTRA_COUNT,
                   "B receiver V(R) should advance past wrap");

  test_printf("Sending %u packets B -> A...\r\n", MOD128_TOTAL_COUNT);
  for (seq = 0; seq < MOD128_WRAP_COUNT; ++seq) {
    TEST_ASSERT_GOTO(send_and_check(&peer_at_b, &peer_at_a, seq) == 0,
                     "B -> A transfer failed before wrap");
  }

  ioHdlc_sleep_ms(50);
  TEST_ASSERT_GOTO(peer_at_b.vs == 0, "B sender V(S) should wrap to 0 after 128 frames");
  TEST_ASSERT_GOTO(peer_at_b.nr == 0, "B sender N(R) should wrap to 0 after 128 frames");
  TEST_ASSERT_GOTO(peer_at_a.vr == 0, "A receiver V(R) should wrap to 0 after 128 frames");

  for (seq = MOD128_WRAP_COUNT; seq < MOD128_TOTAL_COUNT; ++seq) {
    TEST_ASSERT_GOTO(send_and_check(&peer_at_b, &peer_at_a, seq) == 0,
                     "B -> A transfer failed after wrap");
  }

  ioHdlc_sleep_ms(50);
  TEST_ASSERT_GOTO(peer_at_b.vs == MOD128_EXTRA_COUNT,
                   "B sender V(S) should advance past wrap");
  TEST_ASSERT_GOTO(peer_at_b.nr == MOD128_EXTRA_COUNT,
                   "B sender N(R) should advance past wrap");
  TEST_ASSERT_GOTO(peer_at_a.vr == MOD128_EXTRA_COUNT,
                   "A receiver V(R) should advance past wrap");

  result = ioHdlcStationLinkDown(&station_a, STATION_B_ADDR);
  TEST_ASSERT_GOTO(result == 0, "LinkDown failed");

test_cleanup:
  ioHdlc_sleep_ms(100);
  ioHdlcStationDeinit(&station_a);
  ioHdlcStationDeinit(&station_b);

  return test_result;
}
