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
 * @file    test_multipoint_nrm.c
 * @brief   NRM multipoint tests with mock bus.
 *
 * @details Validates multipoint NRM operation with 1 primary + 2 secondaries
 *          on a shared broadcast bus:
 *          - Sequential connection of multiple secondaries
 *          - Round-robin data exchange
 *          - Selective disconnect
 *          - Address isolation (no cross-talk)
 */

#include "test_helpers.h"
#include "test_arenas.h"
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcswdriver.h"
#include "ioHdlc_runner.h"
#include "ioHdlcfmempool.h"
#include "mock_bus.h"
#include <string.h>
#include <errno.h>

/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

#define PRIMARY_ADDR      0x01
#define SECONDARY_A_ADDR  0x02
#define SECONDARY_B_ADDR  0x03
#define REPLY_TIMEOUT     500   /* ms */

/*===========================================================================*/
/* Multipoint Test Context                                                   */
/*===========================================================================*/

typedef struct {
  mock_bus_t bus;
  ioHdlcSwDriver drv_pri, drv_sec_a, drv_sec_b;
  iohdlc_station_t st_pri, st_sec_a, st_sec_b;
  iohdlc_station_peer_t peer_pri_a, peer_pri_b;  /* Primary's peers. */
  iohdlc_station_peer_t peer_sec_a, peer_sec_b;  /* Each secondary's peer. */
} mp_ctx_t;

/**
 * @brief   Initialize multipoint topology: 1 primary + 2 secondaries on bus.
 * @return  0 on success, 1 on failure.
 */
static int mp_setup(mp_ctx_t *ctx) {
  int32_t result;
  iohdlc_station_config_t config;

  memset(ctx, 0, sizeof(*ctx));

  mock_bus_init(&ctx->bus, 3, NULL);

  ioHdlcStreamPort port_pri   = mock_bus_get_port(&ctx->bus, 0);
  ioHdlcStreamPort port_sec_a = mock_bus_get_port(&ctx->bus, 1);
  ioHdlcStreamPort port_sec_b = mock_bus_get_port(&ctx->bus, 2);

  ioHdlcSwDriverInit(&ctx->drv_pri, NULL);
  ioHdlcSwDriverInit(&ctx->drv_sec_a, NULL);
  ioHdlcSwDriverInit(&ctx->drv_sec_b, NULL);

  /* Primary station. */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&ctx->drv_pri;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = TEST_ARENA_SIZE;
  config.fff_type = 1;
  config.phydriver = &port_pri;
  config.reply_timeout_ms = REPLY_TIMEOUT;

  result = ioHdlcStationInit(&ctx->st_pri, &config);
  if (result != 0) return 1;

  /* Secondary A. */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = SECONDARY_A_ADDR;
  config.driver = (ioHdlcDriver *)&ctx->drv_sec_a;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = TEST_ARENA_SIZE;
  config.fff_type = 1;
  config.phydriver = &port_sec_a;
  config.reply_timeout_ms = REPLY_TIMEOUT;

  result = ioHdlcStationInit(&ctx->st_sec_a, &config);
  if (result != 0) return 1;

  /* Secondary B. */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = SECONDARY_B_ADDR;
  config.driver = (ioHdlcDriver *)&ctx->drv_sec_b;
  config.frame_arena = shared_arena_secondary_b;
  config.frame_arena_size = TEST_ARENA_SIZE;
  config.fff_type = 1;
  config.phydriver = &port_sec_b;
  config.reply_timeout_ms = REPLY_TIMEOUT;

  result = ioHdlcStationInit(&ctx->st_sec_b, &config);
  if (result != 0) return 1;

  /* Add peers. Primary has 2 peers, each secondary has 1 peer. */
  result = ioHdlcAddPeer(&ctx->st_pri, &ctx->peer_pri_a, SECONDARY_A_ADDR);
  if (result != 0) return 1;
  result = ioHdlcAddPeer(&ctx->st_pri, &ctx->peer_pri_b, SECONDARY_B_ADDR);
  if (result != 0) return 1;
  result = ioHdlcAddPeer(&ctx->st_sec_a, &ctx->peer_sec_a, PRIMARY_ADDR);
  if (result != 0) return 1;
  result = ioHdlcAddPeer(&ctx->st_sec_b, &ctx->peer_sec_b, PRIMARY_ADDR);
  if (result != 0) return 1;

  /* Start runners. */
  result = ioHdlcRunnerStart(&ctx->st_pri);
  if (result != 0) return 1;

  result = ioHdlcRunnerStart(&ctx->st_sec_a);
  if (result != 0) return 1;

  result = ioHdlcRunnerStart(&ctx->st_sec_b);
  if (result != 0) return 1;

  ioHdlc_sleep_ms(100);
  return 0;
}

/**
 * @brief   Teardown multipoint topology.
 */
static void mp_teardown(mp_ctx_t *ctx) {
  ioHdlcStationDeinit(&ctx->st_pri);
  ioHdlcStationDeinit(&ctx->st_sec_a);
  ioHdlcStationDeinit(&ctx->st_sec_b);

  mock_bus_deinit(&ctx->bus);
}

/*===========================================================================*/
/* Test 1: Connect two secondaries                                           */
/*===========================================================================*/

int test_multipoint_connect_two_secondaries(void) {
  mp_ctx_t ctx;
  int test_result = 0;
  int32_t result;

  test_printf("=== Test: Connect two secondaries sequentially ===\r\n");

  if (mp_setup(&ctx) != 0) {
    test_printf("  Setup failed\r\n");
    test_failures++;
    return 1;
  }

  /* Connect secondary A. */
  test_printf("  Connecting secondary A...\r\n");
  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_A_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp A failed");
  ioHdlc_sleep_ms(300);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_pri_a),
                   "Primary peer A should be connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_sec_a),
                   "Secondary A should be connected");
  test_printf("  Secondary A connected\r\n");

  /* Connect secondary B. */
  test_printf("  Connecting secondary B...\r\n");
  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_B_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp B failed");
  ioHdlc_sleep_ms(300);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_pri_b),
                   "Primary peer B should be connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_sec_b),
                   "Secondary B should be connected");
  test_printf("  Secondary B connected\r\n");

  /* Verify A is still connected (SNRM to B should not disturb A). */
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_pri_a),
                   "Peer A should still be connected after B's SNRM");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_sec_a),
                   "Secondary A should still be connected after B's SNRM");

test_cleanup:
  mp_teardown(&ctx);
  return test_result;
}

/*===========================================================================*/
/* Test 2: Data exchange with both secondaries                               */
/*===========================================================================*/

int test_multipoint_data_exchange(void) {
  mp_ctx_t ctx;
  int test_result = 0;
  int32_t result;
  char tx_buf[32], rx_buf[64];
  ssize_t sent, received;

  test_printf("=== Test: Data exchange with two secondaries ===\r\n");

  if (mp_setup(&ctx) != 0) {
    test_printf("  Setup failed\r\n");
    test_failures++;
    return 1;
  }

  /* Connect both. */
  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_A_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp A failed");
  ioHdlc_sleep_ms(300);

  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_B_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp B failed");
  ioHdlc_sleep_ms(300);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_pri_a), "Peer A not connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_pri_b), "Peer B not connected");

  /* Primary → Secondary A. */
  test_printf("  Primary → Secondary A...\r\n");
  memcpy(tx_buf, "Hello SecA", 11);
  sent = ioHdlcWriteTmo(&ctx.peer_pri_a, tx_buf, 11, 3000);
  TEST_ASSERT_EQ_GOTO(11, (int)sent, "Write to A failed");
  ioHdlc_sleep_ms(500);

  received = ioHdlcReadTmo(&ctx.peer_sec_a, rx_buf, 11, 3000);
  TEST_ASSERT_EQ_GOTO(11, (int)received, "Read at A failed");
  TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, 11) == 0, "Data mismatch at A");
  test_printf("  OK\r\n");

  /* Primary → Secondary B. */
  test_printf("  Primary → Secondary B...\r\n");
  memcpy(tx_buf, "Hello SecB", 11);
  sent = ioHdlcWriteTmo(&ctx.peer_pri_b, tx_buf, 11, 3000);
  TEST_ASSERT_EQ_GOTO(11, (int)sent, "Write to B failed");
  ioHdlc_sleep_ms(500);

  received = ioHdlcReadTmo(&ctx.peer_sec_b, rx_buf, 11, 3000);
  TEST_ASSERT_EQ_GOTO(11, (int)received, "Read at B failed");
  TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, 11) == 0, "Data mismatch at B");
  test_printf("  OK\r\n");

  /* Secondary A → Primary. */
  test_printf("  Secondary A → Primary...\r\n");
  memcpy(tx_buf, "Reply from A", 13);
  sent = ioHdlcWriteTmo(&ctx.peer_sec_a, tx_buf, 13, 3000);
  TEST_ASSERT_EQ_GOTO(13, (int)sent, "Write from A failed");
  ioHdlc_sleep_ms(500);

  received = ioHdlcReadTmo(&ctx.peer_pri_a, rx_buf, 13, 3000);
  TEST_ASSERT_EQ_GOTO(13, (int)received, "Read from A at primary failed");
  TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, 13) == 0, "Reply data mismatch A");
  test_printf("  OK\r\n");

  /* Secondary B → Primary. */
  test_printf("  Secondary B → Primary...\r\n");
  memcpy(tx_buf, "Reply from B", 13);
  sent = ioHdlcWriteTmo(&ctx.peer_sec_b, tx_buf, 13, 3000);
  TEST_ASSERT_EQ_GOTO(13, (int)sent, "Write from B failed");
  ioHdlc_sleep_ms(500);

  received = ioHdlcReadTmo(&ctx.peer_pri_b, rx_buf, 13, 3000);
  TEST_ASSERT_EQ_GOTO(13, (int)received, "Read from B at primary failed");
  TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, 13) == 0, "Reply data mismatch B");
  test_printf("  OK\r\n");

test_cleanup:
  mp_teardown(&ctx);
  return test_result;
}

/*===========================================================================*/
/* Test 3: Selective disconnect                                              */
/*===========================================================================*/

int test_multipoint_selective_disconnect(void) {
  mp_ctx_t ctx;
  int test_result = 0;
  int32_t result;
  char tx_buf[32], rx_buf[64];
  ssize_t sent, received;

  test_printf("=== Test: Selective disconnect ===\r\n");

  if (mp_setup(&ctx) != 0) {
    test_printf("  Setup failed\r\n");
    test_failures++;
    return 1;
  }

  /* Connect both. */
  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_A_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp A failed");
  ioHdlc_sleep_ms(300);

  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_B_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp B failed");
  ioHdlc_sleep_ms(300);

  /* Disconnect A. */
  test_printf("  Disconnecting secondary A...\r\n");
  result = ioHdlcStationLinkDown(&ctx.st_pri, SECONDARY_A_ADDR);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkDown A failed");
  ioHdlc_sleep_ms(300);

  TEST_ASSERT_GOTO(IOHDLC_PEER_DISC(&ctx.peer_pri_a),
                   "Peer A should be disconnected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&ctx.peer_pri_b),
                   "Peer B should still be connected");
  test_printf("  A disconnected, B still connected\r\n");

  /* Exchange data with B. */
  test_printf("  Data exchange with B after A's disconnect...\r\n");
  ioHdlc_sleep_ms(200);  /* Allow DISC/UA exchange to settle. */
  memcpy(tx_buf, "Still here B", 13);
  sent = ioHdlcWriteTmo(&ctx.peer_pri_b, tx_buf, 13, 5000);
  TEST_ASSERT_EQ_GOTO(13, (int)sent, "Write to B failed");
  ioHdlc_sleep_ms(1000);

  received = ioHdlcReadTmo(&ctx.peer_sec_b, rx_buf, 13, 5000);
  TEST_ASSERT_EQ_GOTO(13, (int)received, "Read at B failed");
  TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, 13) == 0, "Data mismatch B");
  test_printf("  OK\r\n");

test_cleanup:
  mp_teardown(&ctx);
  return test_result;
}

/*===========================================================================*/
/* Test 4: Address isolation                                                 */
/*===========================================================================*/

int test_multipoint_address_isolation(void) {
  mp_ctx_t ctx;
  int test_result = 0;
  int32_t result;
  char tx_buf[32], rx_buf[64];
  ssize_t sent, received;

  test_printf("=== Test: Address isolation ===\r\n");

  if (mp_setup(&ctx) != 0) {
    test_printf("  Setup failed\r\n");
    test_failures++;
    return 1;
  }

  /* Connect both. */
  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_A_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp A failed");
  ioHdlc_sleep_ms(300);

  result = ioHdlcStationLinkUp(&ctx.st_pri, SECONDARY_B_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_EQ_GOTO(0, result, "LinkUp B failed");
  ioHdlc_sleep_ms(300);

  /* Send data to peer A only. */
  test_printf("  Sending data to A, verifying B does not receive...\r\n");
  memcpy(tx_buf, "Only for A", 11);
  sent = ioHdlcWriteTmo(&ctx.peer_pri_a, tx_buf, 11, 3000);
  TEST_ASSERT_EQ_GOTO(11, (int)sent, "Write to A failed");
  ioHdlc_sleep_ms(500);

  /* A should receive. */
  received = ioHdlcReadTmo(&ctx.peer_sec_a, rx_buf, 11, 2000);
  TEST_ASSERT_EQ_GOTO(11, (int)received, "A should receive data");
  TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, 11) == 0, "Data mismatch at A");

  /* B should NOT receive (timeout → returns -1 with ETIMEDOUT). */
  received = ioHdlcReadTmo(&ctx.peer_sec_b, rx_buf, 11, 500);
  TEST_ASSERT_EQ_GOTO(-1, (int)received, "B should NOT receive data addressed to A");
  test_printf("  OK: B got nothing\r\n");

  /* Send data to peer B only. */
  test_printf("  Sending data to B, verifying A does not receive...\r\n");
  memcpy(tx_buf, "Only for B", 11);
  sent = ioHdlcWriteTmo(&ctx.peer_pri_b, tx_buf, 11, 3000);
  TEST_ASSERT_EQ_GOTO(11, (int)sent, "Write to B failed");
  ioHdlc_sleep_ms(500);

  /* B should receive. */
  received = ioHdlcReadTmo(&ctx.peer_sec_b, rx_buf, 11, 2000);
  TEST_ASSERT_EQ_GOTO(11, (int)received, "B should receive data");
  TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, 11) == 0, "Data mismatch at B");

  /* A should NOT receive (timeout → returns -1 with ETIMEDOUT). */
  received = ioHdlcReadTmo(&ctx.peer_sec_a, rx_buf, 11, 500);
  TEST_ASSERT_EQ_GOTO(-1, (int)received, "A should NOT receive data addressed to B");
  test_printf("  OK: A got nothing\r\n");

test_cleanup:
  mp_teardown(&ctx);
  return test_result;
}
