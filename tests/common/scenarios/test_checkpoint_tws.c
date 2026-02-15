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
 * @file    test_checkpoint_tws.c
 * @brief   Test checkpoint retransmission in TWS mode.
 *
 * @details Validates:
 *          - Scenario A.1: Frame loss with window full (TWS without REJ)
 *          - Checkpoint retransmission behavior (ISO 13239 5.6.2.1)
 *          - P/F bit protocol correctness
 *          - Frame recovery after checkpoint failure
 *
 * @note    TWS = Two-Way Simultaneous: Primary sends P as soon as possible.
 *          These tests disable REJ to validate checkpoint-only recovery.
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
#include "ch.h"
#include "chprintf.h"
#include "../../chibios/mocks/mock_stream_chibios.h"
#include "../../chibios/mocks/mock_stream_adapter.h"
#else
#include <stdio.h>
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
#define FRAME_SIZE      256
#define REPLY_TIMEOUT   1000  /* ms */
#define ACK_TIMEOUT     100   /* ms */

/*===========================================================================*/
/* Error Filter for Selective Frame Loss                                    */
/*===========================================================================*/

/**
 * @brief   Error filter callback to drop frame I1,0 (I-frame N(S)=1).
 * @details Returns true for the write_count corresponding to I1,0.
 * @note    write_count counts ALL frames: U, S, and I frames!
 *          During connection setup, we have extra frames (SNRM retry, etc.)
 *          We need to identify I1,0 by its content, not just write_count.
 */
/**
 * @brief   Error filter callback to corrupt frame I1,0 (I-frame N(S)=1).
 * @details Returns true for I-frame with N(S)=1 to trigger corruption.
 * @note    Frame structure: [0]=flag, [1]=addr_hi, [2]=addr_lo, [3]=control
 * @note    Only corrupts the FIRST transmission, allows retransmissions through.
 * @return  true = corrupt this frame, false = pass through
 */
static bool drop_frame_1_filter(uint32_t write_count, const uint8_t *data, 
                                size_t size, void *userdata) {
  static int i1_0_count = 0;  /* Count how many times we've seen I1,0 */
  (void)write_count;
  (void)userdata;
  
  /* Only corrupt I-frames (size > 10 means I-frame with payload) */
  if (size < 10) {
    return false;
  }
  
  /* Control byte is at position 3 (after flag + 2-byte address) */
  if (size >= 4) {
    uint8_t ctrl = data[3];
    /* I-frame has bit 0 = 0, N(S) is in bits 1-3 */
    if ((ctrl & 0x01) == 0) {
      uint8_t ns = (ctrl >> 1) & 0x07;
      if (ns == 1) {
        i1_0_count++;
        /* Only corrupt the FIRST time we see I1,0 */
        if (i1_0_count == 1) {
          return true;  /* Corrupt first I-frame with N(S)=1 */
        }
      }
    }
  }
  
  return false;
}

/**
 * @brief   Error filter to corrupt frames I1,0 and I3,0 (N(S)=1 and N(S)=3).
 * @details Test scenario A.2.1: multiple non-consecutive frame loss.
 * @return  true = corrupt this frame, false = pass through
 */
static bool drop_frames_1_and_3_filter(uint32_t write_count, const uint8_t *data, 
                                       size_t size, void *userdata) {
  static int i1_count = 0, i3_count = 0;
  (void)write_count;
  (void)userdata;
  
  if (size < 10) return false;
  
  if (size >= 4) {
    uint8_t ctrl = data[3];
    if ((ctrl & 0x01) == 0) {
      uint8_t ns = (ctrl >> 1) & 0x07;
      if (ns == 1) {
        i1_count++;
        return (i1_count == 1);  /* Corrupt first I1,0 */
      } else if (ns == 3) {
        i3_count++;
        return (i3_count == 1);  /* Corrupt first I3,0 */
      }
    }
  }
  
  return false;
}

/**
 * @brief   Error filter to corrupt frames I0,0 and I7,0 (first and last).
 * @details Test scenario A.2.2: first and last frame loss.
 * @return  true = corrupt this frame, false = pass through
 */
static bool drop_frames_0_and_7_filter(uint32_t write_count, const uint8_t *data, 
                                       size_t size, void *userdata) {
  static int i0_count = 0, i7_count = 0;
  (void)write_count;
  (void)userdata;
  
  if (size < 10) return false;
  
  if (size >= 4) {
    uint8_t ctrl = data[3];
    if ((ctrl & 0x01) == 0) {
      uint8_t ns = (ctrl >> 1) & 0x07;
      if (ns == 0) {
        i0_count++;
        return (i0_count == 1);  /* Corrupt first I0,0 */
      } else if (ns == 7) {
        i7_count++;
        return (i7_count == 1);  /* Corrupt first I7,0 */
      }
    }
  }
  
  return false;
}

/*===========================================================================*/
/* Test Helpers                                                              */
/*===========================================================================*/

/**
 * @brief   Wait for condition with timeout.
 * @param   condition   Condition to wait for
 * @param   timeout_ms  Timeout in milliseconds
 * @return  true if condition met, false if timeout
 */
static bool wait_for_condition(bool (*condition)(void *), void *arg, uint32_t timeout_ms) {
  uint32_t elapsed = 0;
  const uint32_t poll_interval = 10;  /* ms */
  
  while (elapsed < timeout_ms) {
    if (condition(arg)) {
      return true;
    }
    ioHdlc_sleep_ms(poll_interval);
    elapsed += poll_interval;
  }
  return false;
}

/*===========================================================================*/
/* Test: Scenario A.1.1 - Frame Loss with Window Full (TWS without REJ)    */
/*===========================================================================*/

/**
 * @brief   Test scenario A.1.1: Frame loss causes window full, checkpoint retransmit.
 * @details Sequence:
 *          1. Primary sends burst: N(S)=0 (P=1), 1, 2, 3
 *          2. Frame N(S)=1 is LOST (no P bit on this frame)
 *          3. Secondary receives N(S)=0 → responds RR(1) F=1
 *          4. Primary receives F=1, N(R)=1 → checkpoint satisfied
 *          5. Secondary receives N(S)=2 → out-of-sequence (expected 1)
 *          6. Without REJ: discards silently, N(R) stays at 1
 *          7. Primary continues N(S)=4,5,6 → all discarded
 *          8. Primary window full (7 frames outstanding)
 *          9. Primary sends N(S)=7 with P=1 (or RR P=1 if nothing to send)
 *          10. Secondary responds RR(1) F=1 (still blocked at N(R)=1!)
 *          11. Primary detects N(R)=1 < vs_atlast_pf → checkpoint failure
 *          12. Primary retransmits from N(S)=1 to N(S)=7
 *
 * @note    This test validates checkpoint retransmission (ISO 13239 5.6.2.1).
 * @return  0 on success, 1 on failure
 */
bool test_A1_1_frame_loss_window_full(void) {
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  uint8_t arena_primary[8192], arena_secondary[8192];
  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  iohdlc_station_config_t config;
  int32_t result;
  
  /* Configure mock stream for primary: inject errors when writing to secondary */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = true,      /* Enable error injection */
    .error_rate = 1000,          /* 100% corruption when triggered */
    .delay_us = 0,
    .error_filter = drop_frame_1_filter,  /* Drop only frame I1,0 */
    .error_userdata = NULL
  };

  test_printf("\r\n=== Test A.1.1: Frame Loss + Window Full (TWS, no REJ) ===\r\n");

  /* Create mock streams */
  stream_primary = mock_stream_create(&stream_config);  /* Primary: corrupt I1,0 */
  stream_secondary = mock_stream_create(NULL);  /* Secondary: no errors */
  TEST_ASSERT(stream_primary != NULL && stream_secondary != NULL, 
              "Stream creation failed");
  
  /* Connect streams (bidirectional) */
  mock_stream_connect(stream_primary, stream_secondary);
  
  /* Create adapters */
  adapter_primary = mock_stream_adapter_create(stream_primary);
  adapter_secondary = mock_stream_adapter_create(stream_secondary);
  TEST_ASSERT(adapter_primary != NULL && adapter_secondary != NULL, 
              "Adapter creation failed");
  
  /* Get stream ports */
  ioHdlcStreamPort port_primary = mock_stream_adapter_get_port(adapter_primary);
  ioHdlcStreamPort port_secondary = mock_stream_adapter_get_port(adapter_secondary);
  
  /* Initialize stream drivers */
  ioHdlcSwDriverInit(&driver_primary);
  ioHdlcSwDriverInit(&driver_secondary);
  
  /* Initialize frame pools with shared arena */
  
  /* Configure optional functions: disable REJ, enable others */
  static const uint8_t optfuncs_norej[5] = {
    0x00,                              /* Octet 0: REJ bit disabled */
    0x00,                              /* Octet 1: unused */
    IOHDLC_OPT_SST,                    /* Octet 2: SST */
    0x00,                              /* Octet 3: unused */
    IOHDLC_OPT_FFF | IOHDLC_OPT_INH    /* Octet 4: FFF + INH */
  };
  
  /* Configure primary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = arena_primary; config.frame_arena_size = sizeof arena_primary; config.max_info_len = 0; config.pool_watermark = 0; config.fff_type = 1;
  config.optfuncs = optfuncs_norej;  /* Disable REJ */
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");
  
  /* Configure secondary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;  /* Secondary starts in disconnected mode */
  config.flags = 0;  /* Secondary */
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.frame_arena = arena_secondary; config.frame_arena_size = sizeof arena_secondary; config.max_info_len = 0; config.pool_watermark = 0; config.fff_type = 1;
  config.optfuncs = optfuncs_norej;  /* Disable REJ */
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
  
  /* Set timeouts (on station, not peer) */
  station_primary.reply_timeout_ms = REPLY_TIMEOUT;
  
  station_secondary.reply_timeout_ms = REPLY_TIMEOUT;
  
  
  /* Start runner threads for both stations */
  test_printf("Starting runner threads...\r\n");
  ioHdlcRunnerStart(&station_primary);
  ioHdlcRunnerStart(&station_secondary);
  
  /* Wait for threads to be ready */
  ioHdlc_sleep_ms(100);

  /* Establish connection (SNRM handshake) */
  test_printf("Establishing connection (SNRM/UA)...\r\n");
  result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT(result == 0, "LinkUp failed");
  
  /* Allow time for SNRM → UA exchange */
  ioHdlc_sleep_ms(200);
  
  /* Verify connection established */
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  test_printf("✅ Connection established\r\n");
  
  /* Send burst of I-frames from primary */
  test_printf("\r\nSending burst: N(S)=0 (P=1), 1, 2, 3, 4, 5, 6, 7...\r\n");
  char test_data[64];
  ssize_t sent, t_sent;
  int i;
  
  /* Send 8 frames to fill the window (modulo 8, window=7) */
  for (t_sent = 0, i = 0; i < 8; i++) {
#ifdef IOHDLC_USE_CHIBIOS
    chsnprintf(test_data, sizeof test_data, "Frame %d data", i);
#else
    snprintf(test_data, sizeof test_data, "Frame %d data", i);
#endif
    sent = ioHdlcWriteTmo(&peer_at_primary, test_data, strlen(test_data), 2000);
    if (sent != (ssize_t)strlen(test_data)) {
      test_printf("❌ Write frame %d failed: sent=%d, errno=%d\r\n", 
                  i, (int32_t)sent, iohdlc_errno);
    }
    TEST_ASSERT(sent == (ssize_t)strlen(test_data), "Frame send failed");
    test_printf("  Sent frame N(S)=%d (%d bytes)\r\n", i, (int32_t)sent);
    t_sent += sent;
    /* Small delay between frames */
    ioHdlc_sleep_ms(1);
  }
  
  test_printf("\r\nWaiting for protocol to detect frame loss and retransmit...\r\n");
  test_printf("Expected: Secondary receives N(S)=0, drops N(S)=1, window full, checkpoint retransmit\r\n");
  
  /* Wait for checkpoint timeout + retransmission cycles */
  /* With reply_timeout=1000ms, need multiple cycles to recover */
  ioHdlc_sleep_ms(50);  /* 50 milliseconds for multiple checkpoint cycles */
  
  /* Secondary should eventually receive all frames after retransmission */
  test_printf("\r\nReading frames at secondary (may take multiple reads)...\r\n");
  char recv_buf[256];
  size_t total_received = 0;
  int read_attempts = 0;
  const int max_attempts = 10;
  
  /* Read multiple times to collect all retransmitted frames */
  while (total_received < 96 && read_attempts < max_attempts) {
    memset(recv_buf, 0, sizeof recv_buf);
    ssize_t received = ioHdlcReadTmo(&peer_at_secondary, recv_buf, 
                                     t_sent - total_received, 1000);
    
    if (received > 0) {
      test_printf("  Read attempt %d: +%d bytes (total now: %u)\r\n", 
                  read_attempts + 1, (int32_t)received, (uint32_t)(total_received + received));
      total_received += (size_t)received;
    } else if (received == 0) {
      test_printf("  Read attempt %d: no data (timeout), total: %u bytes\r\n",
                  read_attempts + 1, (uint32_t)total_received);
    } else {
      test_printf("  Read attempt %d: error %d, errno=%d\r\n", 
                  read_attempts + 1, (int32_t)received, iohdlc_errno);
    }
    
    read_attempts++;
    
    /* If we got all data, no need to retry */
    if (total_received >= 96) {
      break;
    }
    
    /* Small delay between reads */
    ioHdlc_sleep_ms(100);
  }
  
  /* Calculate expected size: 8 frames x 12 bytes each = 96 bytes */
  size_t expected_size = 8 * 12;
  test_printf("\r\nTotal bytes received: %u (expected %u) after %d read attempts\r\n", 
              (uint32_t)total_received, (uint32_t)expected_size, read_attempts);
  
  if (total_received == expected_size) {
    test_printf("✅ All frames eventually delivered after checkpoint retransmission!\r\n");
  } else {
    test_printf("❌ Received %u bytes (expected %u) - retransmission failed!\r\n", 
                (uint32_t)total_received, (uint32_t)expected_size);
  }
  
  /* Verify ALL frames were eventually received via retransmission */
  TEST_ASSERT(total_received == expected_size, 
              "All 8 frames must be received after checkpoint retransmission");

  /* Cleanup */
  ioHdlcRunnerStop(&station_primary);
  ioHdlcRunnerStop(&station_secondary);
  mock_stream_adapter_destroy(adapter_primary);
  mock_stream_adapter_destroy(adapter_secondary);
  mock_stream_destroy(stream_primary);
  mock_stream_destroy(stream_secondary);

  return 0;
}

/**
 * @brief   Test A.2.1: Multiple frame loss (N(S)=1 and N(S)=3).
 * @details Same as A.1.1 but with two non-consecutive frames lost.
 */
bool test_A2_1_multiple_frame_loss(void) {
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;

  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  iohdlc_station_config_t config;
  int32_t result;
  
  /* Configure mock stream for primary: inject errors when writing to secondary */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = true,      /* Enable error injection */
    .error_rate = 1000,          /* 100% corruption when triggered */
    .delay_us = 0,
    .error_filter = drop_frames_1_and_3_filter,  /* Drop frames I1,0 and I3,0 */
    .error_userdata = NULL
  };

  test_printf("\r\n=== Test A.2.1: Multiple Frame Loss (I1,0 and I3,0) ===\r\n");

  /* Create mock streams */
  stream_primary = mock_stream_create(&stream_config);  /* Primary: corrupt I1,0 and I3,0 */
  stream_secondary = mock_stream_create(NULL);  /* Secondary: no errors */
  TEST_ASSERT(stream_primary != NULL && stream_secondary != NULL, 
              "Stream creation failed");
  
  /* Connect streams (bidirectional) */
  mock_stream_connect(stream_primary, stream_secondary);
  
  /* Create adapters */
  adapter_primary = mock_stream_adapter_create(stream_primary);
  adapter_secondary = mock_stream_adapter_create(stream_secondary);
  TEST_ASSERT(adapter_primary != NULL && adapter_secondary != NULL, 
              "Adapter creation failed");
  
  /* Get stream ports */
  ioHdlcStreamPort port_primary = mock_stream_adapter_get_port(adapter_primary);
  ioHdlcStreamPort port_secondary = mock_stream_adapter_get_port(adapter_secondary);
  
  /* Initialize stream drivers */
  ioHdlcSwDriverInit(&driver_primary);
  ioHdlcSwDriverInit(&driver_secondary);
  
  /* Configure optional functions: disable REJ, enable others */
  static const uint8_t optfuncs_norej[5] = {
    0x00,                              /* Octet 0: REJ bit disabled */
    0x00,                              /* Octet 1: unused */
    IOHDLC_OPT_SST,                    /* Octet 2: SST */
    0x00,                              /* Octet 3: unused */
    IOHDLC_OPT_FFF | IOHDLC_OPT_INH    /* Octet 4: FFF + INH */
  };
  
  /* Configure primary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.max_info_len = 0;  /* Auto */
  config.pool_watermark = 0;  /* Auto: 10% min 8 */
  config.fff_type = 1;  /* TYPE0 */
  config.optfuncs = optfuncs_norej;  /* Disable REJ */
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");
  
  /* Configure secondary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;  /* Secondary starts in disconnected mode */
  config.flags = 0;  /* Secondary */
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = sizeof shared_arena_secondary;
  config.max_info_len = 0;  /* Auto */
  config.pool_watermark = 0;  /* Auto: 10% min 8 */
  config.fff_type = 1;  /* TYPE0 */
  config.optfuncs = optfuncs_norej;  /* Disable REJ */
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
  
  /* Set timeouts (on station, not peer) */
  station_primary.reply_timeout_ms = REPLY_TIMEOUT;
  
  station_secondary.reply_timeout_ms = REPLY_TIMEOUT;
  
  
  /* Start runner threads for both stations */
  test_printf("Starting runner threads...\r\n");
  ioHdlcRunnerStart(&station_primary);
  ioHdlcRunnerStart(&station_secondary);
  
  /* Wait for threads to be ready */
  ioHdlc_sleep_ms(100);

  /* Establish connection (SNRM handshake) */
  test_printf("Establishing connection (SNRM/UA)...\r\n");
  result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT(result == 0, "LinkUp failed");
  
  /* Allow time for SNRM → UA exchange */
  ioHdlc_sleep_ms(200);
  
  /* Verify connection established */
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  test_printf("✅ Connection established\r\n");
  
  /* Send burst of I-frames from primary */
  test_printf("\r\nSending burst: N(S)=0 (P=1), 1, 2, 3, 4, 5, 6, 7...\r\n");
  char test_data[64];
  ssize_t sent, t_sent;
  int i;
  
  /* Send 8 frames to fill the window (modulo 8, window=7) */
  for (t_sent = 0, i = 0; i < 8; i++) {
#ifdef IOHDLC_USE_CHIBIOS
    chsnprintf(test_data, sizeof test_data, "Frame %d data", i);
#else
    snprintf(test_data, sizeof test_data, "Frame %d data", i);
#endif
    sent = ioHdlcWriteTmo(&peer_at_primary, test_data, strlen(test_data), 2000);
    if (sent != (ssize_t)strlen(test_data)) {
      test_printf("❌ Write frame %d failed: sent=%d, errno=%d\r\n", 
                  i, (int32_t)sent, iohdlc_errno);
    }
    TEST_ASSERT(sent == (ssize_t)strlen(test_data), "Frame send failed");
    test_printf("  Sent frame N(S)=%d (%d bytes)\r\n", i, (int32_t)sent);
    t_sent += (size_t)sent;
    /* Small delay between frames */
    ioHdlc_sleep_ms(1);
  }
  
  test_printf("\r\nWaiting for protocol to detect frame loss and retransmit...\r\n");
  test_printf("Expected: Secondary receives N(S)=0, drops N(S)=1 and N(S)=3, checkpoint retransmit\r\n");
  
  /* Wait for checkpoint timeout + retransmission cycles */
  /* With reply_timeout=1000ms, need multiple cycles to recover */
  ioHdlc_sleep_ms(50);  /* 50 milliseconds for multiple checkpoint cycles */
  
  /* Secondary should eventually receive all frames after retransmission */
  test_printf("\r\nReading frames at secondary (may take multiple reads)...\r\n");
  char recv_buf[256];
  size_t total_received = 0;
  int read_attempts = 0;
  const int max_attempts = 10;
  
  /* Read multiple times to collect all retransmitted frames */
  while (total_received < 96 && read_attempts < max_attempts) {
    memset(recv_buf, 0, sizeof recv_buf);
    ssize_t received = ioHdlcReadTmo(&peer_at_secondary, recv_buf, 
                                     t_sent - total_received, 1000);
    
    if (received > 0) {
      test_printf("  Read attempt %d: +%d bytes (total now: %u)\r\n", 
                  read_attempts + 1, (int32_t)received, (uint32_t)(total_received + received));
      total_received += (size_t)received;
    } else if (received == 0) {
      test_printf("  Read attempt %d: no data (timeout), total: %u bytes\r\n",
                  read_attempts + 1, (uint32_t)total_received);
    } else {
      test_printf("  Read attempt %d: error %d, errno=%d\r\n", 
                  read_attempts + 1, (int32_t)received, iohdlc_errno);
    }
    
    read_attempts++;
    
    /* If we got all data, no need to retry */
    if (total_received >= 96) {
      break;
    }
    
    /* Small delay between reads */
    ioHdlc_sleep_ms(100);
  }
  
  /* Calculate expected size: 8 frames x 12 bytes each = 96 bytes */
  size_t expected_size = 8 * 12;
  test_printf("\r\nTotal bytes received: %u (expected %u) after %d read attempts\r\n", 
              (uint32_t)total_received, (uint32_t)expected_size, read_attempts);
  
  if (total_received == expected_size) {
    test_printf("✅ All frames eventually delivered after checkpoint retransmission!\r\n");
  } else {
    test_printf("❌ Received %u bytes (expected %u) - retransmission failed!\r\n", 
                (uint32_t)total_received, (uint32_t)expected_size);
  }
  
  /* Verify ALL frames were eventually received via retransmission */
  TEST_ASSERT(total_received == expected_size, 
              "All 8 frames must be received after checkpoint retransmission");

  /* Cleanup */
  ioHdlcRunnerStop(&station_primary);
  ioHdlcRunnerStop(&station_secondary);
  mock_stream_adapter_destroy(adapter_primary);
  mock_stream_adapter_destroy(adapter_secondary);
  mock_stream_destroy(stream_primary);
  mock_stream_destroy(stream_secondary);

  return 0;
}

/**
 * @brief   Test A.2.2: First and last frame loss (N(S)=0 and N(S)=7).
 * @details Same as A.1.1 but first and last frames are lost.
 */
bool test_A2_2_first_and_last_frame_loss(void) {
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;

  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  iohdlc_station_config_t config;
  int32_t result;
  
  /* Configure mock stream for primary: inject errors when writing to secondary */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = true,      /* Enable error injection */
    .error_rate = 1000,          /* 100% corruption when triggered */
    .delay_us = 0,
    .error_filter = drop_frames_0_and_7_filter,  /* Drop frames I0,0 and I7,0 */
    .error_userdata = NULL
  };

  test_printf("\r\n=== Test A.2.2: First and Last Frame Loss (I0,0 and I7,0) ===\r\n");

  /* Create mock streams */
  stream_primary = mock_stream_create(&stream_config);  /* Primary: corrupt I0,0 and I7,0 */
  stream_secondary = mock_stream_create(NULL);  /* Secondary: no errors */
  TEST_ASSERT(stream_primary != NULL && stream_secondary != NULL, 
              "Stream creation failed");
  
  /* Connect streams (bidirectional) */
  mock_stream_connect(stream_primary, stream_secondary);
  
  /* Create adapters */
  adapter_primary = mock_stream_adapter_create(stream_primary);
  adapter_secondary = mock_stream_adapter_create(stream_secondary);
  TEST_ASSERT(adapter_primary != NULL && adapter_secondary != NULL, 
              "Adapter creation failed");
  
  /* Get stream ports */
  ioHdlcStreamPort port_primary = mock_stream_adapter_get_port(adapter_primary);
  ioHdlcStreamPort port_secondary = mock_stream_adapter_get_port(adapter_secondary);
  
  /* Initialize stream drivers */
  ioHdlcSwDriverInit(&driver_primary);
  ioHdlcSwDriverInit(&driver_secondary);
  
  /* Configure optional functions: disable REJ, enable others */
  static const uint8_t optfuncs_norej[5] = {
    0x00,                              /* Octet 0: REJ bit disabled */
    0x00,                              /* Octet 1: unused */
    IOHDLC_OPT_SST,                    /* Octet 2: SST */
    0x00,                              /* Octet 3: unused */
    IOHDLC_OPT_FFF | IOHDLC_OPT_INH    /* Octet 4: FFF + INH */
  };
  
  /* Configure primary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.max_info_len = 0;  /* Auto */
  config.pool_watermark = 0;  /* Auto: 10% min 8 */
  config.fff_type = 1;  /* TYPE0 */
  config.optfuncs = optfuncs_norej;  /* Disable REJ */
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");
  
  /* Configure secondary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;  /* Secondary starts in disconnected mode */
  config.flags = 0;  /* Secondary */
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = sizeof shared_arena_secondary;
  config.max_info_len = 0;  /* Auto */
  config.pool_watermark = 0;  /* Auto: 10% min 8 */
  config.fff_type = 1;  /* TYPE0 */
  config.optfuncs = optfuncs_norej;  /* Disable REJ */
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
  
  /* Set timeouts (on station, not peer) */
  station_primary.reply_timeout_ms = REPLY_TIMEOUT;
  
  station_secondary.reply_timeout_ms = REPLY_TIMEOUT;
  
  
  /* Start runner threads for both stations */
  test_printf("Starting runner threads...\r\n");
  ioHdlcRunnerStart(&station_primary);
  ioHdlcRunnerStart(&station_secondary);
  
  /* Wait for threads to be ready */
  ioHdlc_sleep_ms(100);

  /* Establish connection (SNRM handshake) */
  test_printf("Establishing connection (SNRM/UA)...\r\n");
  result = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT(result == 0, "LinkUp failed");
  
  /* Allow time for SNRM → UA exchange */
  ioHdlc_sleep_ms(200);
  
  /* Verify connection established */
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  test_printf("✅ Connection established\r\n");
  
  /* Send burst of I-frames from primary */
  test_printf("\r\nSending burst: N(S)=0 (P=1), 1, 2, 3, 4, 5, 6, 7...\r\n");
  char test_data[64];
  ssize_t sent, t_sent;
  int i;
  
  /* Send 8 frames to fill the window (modulo 8, window=7) */
  for (t_sent = 0, i = 0; i < 8; i++) {
#ifdef IOHDLC_USE_CHIBIOS
    chsnprintf(test_data, sizeof test_data, "Frame %d data", i);
#else
    snprintf(test_data, sizeof test_data, "Frame %d data", i);
#endif
    sent = ioHdlcWriteTmo(&peer_at_primary, test_data, strlen(test_data), 2000);
    if (sent != (ssize_t)strlen(test_data)) {
      test_printf("❌ Write frame %d failed: sent=%d, errno=%d\r\n", 
                  i, (int32_t)sent, iohdlc_errno);
    }
    TEST_ASSERT(sent == (ssize_t)strlen(test_data), "Frame send failed");
    test_printf("  Sent frame N(S)=%d (%d bytes)\r\n", i, (int32_t)sent);
    t_sent += (size_t)sent;
    /* Small delay between frames */
    ioHdlc_sleep_ms(1);
  }
  
  test_printf("\r\nWaiting for protocol to detect frame loss and retransmit...\r\n");
  test_printf("Expected: Secondary waits for N(S)=0 (never arrives), checkpoint retransmit\r\n");
  
  /* Wait for checkpoint timeout + retransmission cycles */
  /* With reply_timeout=1000ms, need multiple cycles to recover */
  ioHdlc_sleep_ms(50);  /* 50 milliseconds for multiple checkpoint cycles */
  
  /* Secondary should eventually receive all frames after retransmission */
  test_printf("\r\nReading frames at secondary (may take multiple reads)...\r\n");
  char recv_buf[256];
  size_t total_received = 0;
  int read_attempts = 0;
  const int max_attempts = 10;
  
  /* Read multiple times to collect all retransmitted frames */
  while (total_received < 96 && read_attempts < max_attempts) {
    memset(recv_buf, 0, sizeof recv_buf);
    ssize_t received = ioHdlcReadTmo(&peer_at_secondary, recv_buf, 
                                     t_sent - total_received, 1000);
    
    if (received > 0) {
      test_printf("  Read attempt %d: +%d bytes (total now: %u)\r\n", 
                  read_attempts + 1, (int32_t)received, (uint32_t)(total_received + received));
      total_received += (size_t)received;
    } else if (received == 0) {
      test_printf("  Read attempt %d: no data (timeout), total: %u bytes\r\n",
                  read_attempts + 1, (uint32_t)total_received);
    } else {
      test_printf("  Read attempt %d: error %d, errno=%d\r\n", 
                  read_attempts + 1, (int32_t)received, iohdlc_errno);
    }
    
    read_attempts++;
    
    /* If we got all data, no need to retry */
    if (total_received >= 96) {
      break;
    }
    
    /* Small delay between reads */
    ioHdlc_sleep_ms(100);
  }
  
  /* Calculate expected size: 8 frames x 12 bytes each = 96 bytes */
  size_t expected_size = 8 * 12;
  test_printf("\r\nTotal bytes received: %u (expected %u) after %d read attempts\r\n", 
              (uint32_t)total_received, (uint32_t)expected_size, read_attempts);
  
  if (total_received == expected_size) {
    test_printf("✅ All frames eventually delivered after checkpoint retransmission!\r\n");
  } else {
    test_printf("❌ Received %u bytes (expected %u) - retransmission failed!\r\n", 
                (uint32_t)total_received, (uint32_t)expected_size);
  }
  
  /* Verify ALL frames were eventually received via retransmission */
  TEST_ASSERT(total_received == expected_size, 
              "All 8 frames must be received after checkpoint retransmission");

  /* Cleanup */
  ioHdlcRunnerStop(&station_primary);
  ioHdlcRunnerStop(&station_secondary);
  mock_stream_adapter_destroy(adapter_primary);
  mock_stream_adapter_destroy(adapter_secondary);
  mock_stream_destroy(stream_primary);
  mock_stream_destroy(stream_secondary);

  return 0;
}

/*===========================================================================*/
/* Test Suite Entry Point                                                    */
/*===========================================================================*/

#ifndef IOHDLC_USE_CHIBIOS
int main(void) {
  int failures = 0;

  test_printf("\r\n");
  test_printf("╔════════════════════════════════════════════════════════════╗\r\n");
  test_printf("║  Checkpoint Retransmission Tests - TWS Mode                ║\r\n");
  test_printf("╚════════════════════════════════════════════════════════════╝\r\n");

  /* Run tests */
  if (test_A1_1_frame_loss_window_full()) failures++;
  if (test_A2_1_multiple_frame_loss()) failures++;
  if (test_A2_2_first_and_last_frame_loss()) failures++;

  /* Print summary */
  test_printf("\r\n");
  test_printf("════════════════════════════════════════════════════════════\r\n");
  if (failures == 0) {
    test_printf("✅ All tests PASSED\r\n");
  } else {
    test_printf("❌ %d test(s) FAILED\r\n", failures);
  }
  test_printf("════════════════════════════════════════════════════════════\r\n");

  return failures;
}
#endif /* !IOHDLC_USE_CHIBIOS */
