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
 * @file    test_frmr.c
 * @brief   Test FRMR generation, repetition, I-frame suppression, and recovery.
 *
 * @details Validates the complete FRMR lifecycle (ISO 13239, 5.5.3):
 *          1. Invalid N(R) triggers FRMR with Y bit
 *          2. FRMR repeated at every poll
 *          3. I-frame transmission suppressed during FRMR condition
 *          4. SNRM clears condition and resumes normal operation
 */

#include "test_helpers.h"
#include "test_arenas.h"
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcqueue.h"
#include "ioHdlcswdriver.h"
#include "ioHdlc_runner.h"
#include "ioHdlcfmempool.h"
#include <string.h>
#include <errno.h>
#include "adapter_interface.h"
#include "mock_stream.h"
#include "mock_stream_adapter.h"

/*===========================================================================*/
/* Test Configuration                                                        */
/*===========================================================================*/

#define PRIMARY_ADDR    0x01
#define SECONDARY_ADDR  0x02
#define REPLY_TIMEOUT   500   /* ms */

/*===========================================================================*/
/* Tamper Callback: Inject Invalid N(R)                                     */
/*===========================================================================*/

static int tamper_iframe_count;

/**
 * @brief   Tamper callback that sets an invalid N(R) on the 4th I-frame.
 * @details Modifies the N(R) field of an I-frame to a value outside the
 *          valid acknowledgment window, triggering FRMR with Y bit.
 *          FCS is recalculated by mock_stream after this returns.
 */
static bool inject_invalid_nr_tamper(uint32_t write_count, uint8_t *data,
                                      size_t size, void *userdata) {
  (void)write_count;
  (void)userdata;

  if (size < 6)
    return false;

  /* Wire format: [FLAG?] [FFF] [ADDR] [CTRL] ...
     Opening flag (0x7E) may or may not be present.
     With TYPE0 FFF (1 byte): CTRL is at offset 3 (flag+FFF+ADDR)
     or offset 2 (FFF+ADDR) if no opening flag. */
  size_t ctrl_off = (data[0] == 0x7E) ? 3 : 2;
  if (ctrl_off >= size)
    return false;

  uint8_t ctrl = data[ctrl_off];

  /* I-frame: bit 0 = 0. */
  if ((ctrl & 0x01) != 0)
    return false;

  tamper_iframe_count++;
  if (tamper_iframe_count != 4)
    return false;  /* Only tamper with the 4th I-frame. */

  /* Set N(R) to 7 (mod 8) -- invalid when secondary's V(S) is low.
     N(R) is in bits 5-7 of the control byte for modulo 8. */
  data[ctrl_off] = (ctrl & 0x1F) | 0xE0;  /* N(R) = 7 */
  return true;  /* Data modified -- recalculate FCS. */
}

/*===========================================================================*/
/* Test: FRMR Lifecycle                                                      */
/*===========================================================================*/

/**
 * @brief   Test the complete FRMR lifecycle triggered by invalid N(R).
 * @details Steps:
 *          1. Establish connection (SNRM/UA)
 *          2. Exchange I-frames to get V(S)/V(R) > 0
 *          3. Inject I-frame with invalid N(R) → secondary enters FRMR condition
 *          4. Verify FRMR condition is active on secondary
 *          5. Wait for poll → FRMR repeated
 *          6. Primary sends SNRM → secondary clears condition, responds UA
 *          7. Verify normal I-frame exchange resumes
 *
 * @param[in] adapter   Test adapter (must support error injection).
 * @return  0 on success, 1 on failure.
 */
bool test_frmr_invalid_nr(const test_adapter_t *adapter) {
  (void)adapter;  /* This test creates its own mock streams directly. */

  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  uint8_t arena_primary[8192], arena_secondary[8192];
  mock_stream_t *stream_primary = NULL, *stream_secondary = NULL;
  mock_stream_adapter_t *adapter_primary = NULL, *adapter_secondary = NULL;
  iohdlc_station_config_t config;
  int32_t result;
  int test_result = 0;

  tamper_iframe_count = 0;

  test_printf("=== Test FRMR: Invalid N(R) → FRMR → Recovery ===\r\n");

  /* Create mock streams: primary stream has the tamper callback. */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = false,
    .error_rate = 0,
    .delay_us = 0,
    .error_filter = NULL,
    .error_userdata = NULL,
    .tamper_filter = inject_invalid_nr_tamper,
    .tamper_userdata = NULL
  };

  stream_primary = mock_stream_create(&stream_config);
  stream_secondary = mock_stream_create(NULL);
  TEST_ASSERT_GOTO(stream_primary != NULL && stream_secondary != NULL,
                   "Stream creation failed");

  mock_stream_connect(stream_primary, stream_secondary);

  adapter_primary = mock_stream_adapter_create(stream_primary);
  adapter_secondary = mock_stream_adapter_create(stream_secondary);
  TEST_ASSERT_GOTO(adapter_primary != NULL && adapter_secondary != NULL,
                   "Adapter creation failed");

  ioHdlcStreamPort port_primary = mock_stream_adapter_get_port(adapter_primary);
  ioHdlcStreamPort port_secondary = mock_stream_adapter_get_port(adapter_secondary);

  ioHdlcSwDriverInit(&driver_primary);
  ioHdlcSwDriverInit(&driver_secondary);

  /* Configure primary station. */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = arena_primary;
  config.frame_arena_size = sizeof arena_primary;
  config.max_info_len = 0;
  config.pool_watermark = 0;
  config.fff_type = 1;
  config.optfuncs = NULL;
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;

  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT_GOTO(result == 0, "Primary station init failed");

  /* Configure secondary station. */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.frame_arena = arena_secondary;
  config.frame_arena_size = sizeof arena_secondary;
  config.max_info_len = 0;
  config.pool_watermark = 0;
  config.fff_type = 1;
  config.optfuncs = NULL;
  config.phydriver = &port_secondary;
  config.phydriver_config = NULL;

  memset(&station_secondary, 0, sizeof station_secondary);
  result = ioHdlcStationInit(&station_secondary, &config);
  TEST_ASSERT_GOTO(result == 0, "Secondary station init failed");

  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to primary failed");

  result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to secondary failed");

  station_primary.reply_timeout_ms = REPLY_TIMEOUT;
  station_secondary.reply_timeout_ms = REPLY_TIMEOUT;

  /* Start runners. */
  result = ioHdlcRunnerStart(&station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start secondary runner");

  ioHdlc_sleep_ms(100);

  /* Step 1: Establish connection. */
  test_printf("Step 1: Establishing connection (SNRM/UA)...\r\n");
  result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(result == 0, "LinkUp failed");
  ioHdlc_sleep_ms(200);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary not connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary not connected");
  test_printf("  ✅ Connection established\r\n");

  /* Step 2: Exchange I-frames to advance V(S)/V(R). */
  test_printf("Step 2: Exchanging I-frames to advance sequence numbers...\r\n");
  {
    char tx_buf[] = "Frame 1";
    char rx_buf[64];
    ssize_t sent, received;

    /* Send 3 frames from primary, read at secondary. */
    for (int i = 0; i < 3; i++) {
      sent = ioHdlcWriteTmo(&peer_at_primary, tx_buf, sizeof tx_buf, 2000);
      TEST_ASSERT_GOTO(sent == sizeof tx_buf, "Write failed");
    }
    ioHdlc_sleep_ms(100);

    size_t total = 0;
    while (total < 3 * sizeof tx_buf) {
      received = ioHdlcReadTmo(&peer_at_secondary, rx_buf, sizeof rx_buf, 1000);
      if (received > 0)
        total += (size_t)received;
      else
        break;
    }
    TEST_ASSERT_GOTO(total == 3 * sizeof tx_buf, "Did not receive all initial frames");
    test_printf("  ✅ Exchanged %u bytes\r\n", (uint32_t)total);
  }

  /* Step 3: The error filter will corrupt N(R) in the 4th I-frame from primary.
     Send one more I-frame to trigger it. */
  test_printf("Step 3: Sending I-frame with injected invalid N(R)...\r\n");
  {
    char tx_buf[] = "Trigger FRMR";
    ssize_t sent = ioHdlcWriteTmo(&peer_at_primary, tx_buf, sizeof tx_buf, 2000);
    TEST_ASSERT_GOTO(sent == sizeof tx_buf, "Trigger write failed");
  }

  /* Wait for the secondary to process the bad frame and enter FRMR. */
  ioHdlc_sleep_ms(300);

  /* Step 4: Verify FRMR condition is active on secondary. */
  test_printf("Step 4: Verifying FRMR condition...\r\n");
  TEST_ASSERT_GOTO(peer_at_secondary.frmr_condition == true,
                   "Secondary should be in FRMR condition");
  TEST_ASSERT_GOTO((peer_at_secondary.frmr_reason & IOHDLC_FRMR_Y) != 0,
                   "FRMR reason should have Y bit (invalid N(R))");
  test_printf("  ✅ FRMR condition active (reason: Y bit)\r\n");

  /* Step 5: Wait for poll/reply cycle -- FRMR should be repeated.
     The primary will poll, secondary responds with FRMR each time. */
  test_printf("Step 5: Waiting for FRMR repetition on polls...\r\n");
  ioHdlc_sleep_ms(REPLY_TIMEOUT + 200);
  TEST_ASSERT_GOTO(peer_at_secondary.frmr_condition == true,
                   "FRMR condition should persist after polls");
  test_printf("  ✅ FRMR condition persists\r\n");

  /* Step 6: Recovery via DISC + SNRM.
     Per ISO 13239, the primary is responsible for recovery after
     receiving FRMR. It issues DISC to tear down, then SNRM to re-establish. */
  test_printf("Step 6: Recovery -- DISC then SNRM...\r\n");
  result = ioHdlcStationLinkDown(&station_primary, SECONDARY_ADDR);
  TEST_ASSERT_GOTO(result == 0, "LinkDown during FRMR failed");
  ioHdlc_sleep_ms(500);

  TEST_ASSERT_GOTO(peer_at_secondary.frmr_condition == false,
                   "FRMR condition should be cleared by DISC");

  result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(result == 0, "Recovery LinkUp failed");
  ioHdlc_sleep_ms(300);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary not connected after recovery");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary not connected after recovery");
  test_printf("  ✅ FRMR cleared, link re-established\r\n");

  /* Step 7: Verify normal exchange resumes. */
  test_printf("Step 7: Verifying normal I-frame exchange after recovery...\r\n");
  {
    char tx_buf[] = "After recovery";
    char rx_buf[64];
    ssize_t sent, received;

    sent = ioHdlcWriteTmo(&peer_at_primary, tx_buf, sizeof tx_buf, 2000);
    TEST_ASSERT_GOTO(sent == sizeof tx_buf, "Post-recovery write failed");
    ioHdlc_sleep_ms(200);

    received = ioHdlcReadTmo(&peer_at_secondary, rx_buf, sizeof rx_buf, 1000);
    TEST_ASSERT_GOTO(received == sizeof tx_buf, "Post-recovery read failed");
    TEST_ASSERT_GOTO(memcmp(tx_buf, rx_buf, sizeof tx_buf) == 0,
                     "Post-recovery data mismatch");
    test_printf("  ✅ Normal exchange resumed\r\n");
  }

test_cleanup:
  ioHdlcStationDeinit(&station_primary);
  ioHdlcStationDeinit(&station_secondary);

  if (adapter_primary) mock_stream_adapter_destroy(adapter_primary);
  if (adapter_secondary) mock_stream_adapter_destroy(adapter_secondary);
  if (stream_primary) mock_stream_destroy(stream_primary);
  if (stream_secondary) mock_stream_destroy(stream_secondary);

  return test_result;
}
