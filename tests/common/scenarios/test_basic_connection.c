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
 * @file    test_basic_connection.c
 * @brief   Test basic HDLC connection establishment.
 *
 * @details Validates:
 *          - Station/peer creation and initialization
 *          - SNRM handshake (Primary → Secondary)
 *          - UA response timing
 *          - Mode transition to NRM
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

#define PRIMARY_ADDR    0x01
#define SECONDARY_ADDR  0x02
#define WINDOW_SIZE     7
#define FRAME_SIZE      128  /* Frame pool frame size for tests */

/*===========================================================================*/
/* Test Helpers                                                              */
/*===========================================================================*/

typedef struct {
  uint32_t check_calls;
  uint32_t compute_calls;
  uint8_t last_fcs_size;
  size_t last_total_len;
} test_fcs_backend_probe_t;

static bool test_fcs_backend_check(void *fcs_backend_ctx, uint8_t fcs_size,
                                   const uint8_t *buf, size_t total_len) {
  test_fcs_backend_probe_t *probe = (test_fcs_backend_probe_t *)fcs_backend_ctx;
  uint16_t fcs = 0;

  if (probe != NULL) {
    probe->check_calls++;
    probe->last_fcs_size = fcs_size;
    probe->last_total_len = total_len;
  }

  if (fcs_size == 0U)
    return true;
  if (fcs_size != 2U || total_len < 2U)
    return false;

  ioHdlcComputeFCS(buf, total_len - 2U, &fcs);
  return buf[total_len - 2U] == (uint8_t)(fcs & 0xFFU) &&
         buf[total_len - 1U] == (uint8_t)(fcs >> 8);
}

static void test_fcs_backend_compute(void *fcs_backend_ctx, uint8_t fcs_size,
                                     const iohdlc_tx_seg_t *segv, uint8_t segc,
                                     uint8_t *fcs_out) {
  test_fcs_backend_probe_t *probe = (test_fcs_backend_probe_t *)fcs_backend_ctx;
  uint16_t crc;
  uint8_t i;

  if (probe != NULL) {
    probe->compute_calls++;
    probe->last_fcs_size = fcs_size;
  }

  if (fcs_size != 2U)
    return;

  ioHdlcFcsInit(&crc);
  for (i = 0U; i < segc; ++i) {
    if (segv[i].len > 0U)
      ioHdlcFcsUpdate(&crc, segv[i].ptr, segv[i].len);
  }
  crc = ioHdlcFcsFinalize(crc);
  fcs_out[0] = (uint8_t)(crc & 0xFFU);
  fcs_out[1] = (uint8_t)(crc >> 8);
}

static const ioHdlcSwDriverFcsBackend test_fcs_backend = {
  .supported_sizes = {2, 0, 0, 0},
  .default_size = 2,
  .check = test_fcs_backend_check,
  .compute = test_fcs_backend_compute,
};

/* Mock driver VMT with minimal capabilities */
static const ioHdlcDriverCapabilities mock_caps = {
  .fcs = {
    .supported_sizes = {0, 2, 0, 0},
    .default_size = 2,
  },
  .transparency = {
    .hw_support = false,
    .sw_available = false
  },
  .fff = {
    .supported_types = {0, 1, 0, 0},
    .default_type = 1,
    .hw_support = false
  }
};

static const ioHdlcDriverCapabilities* mock_get_caps(void *instance) {
  (void)instance;
  return &mock_caps;
}

static int32_t mock_configure(void *instance, uint8_t fcs_size, bool transparency, uint8_t fff_type) {
  (void)instance; (void)fcs_size; (void)transparency; (void)fff_type;
  return 0;  /* Success */
}

static void mock_start(void *instance, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp) {
  (void)instance; (void)phyp; (void)phyconfigp; (void)fpp;
}

static int32_t mock_send_frame(void *instance, iohdlc_frame_t *fp) {
  (void)instance; (void)fp;
  return 0;
}

static iohdlc_frame_t* mock_recv_frame(void *instance, iohdlc_timeout_t tmo) {
  (void)instance; (void)tmo;
  return NULL;
}

static const struct _iohdlc_driver_vmt mock_vmt = {
  .start = mock_start,
  .send_frame = mock_send_frame,
  .recv_frame = mock_recv_frame,
  .get_capabilities = mock_get_caps,
  .configure = mock_configure
};

/**
 * @brief   Initialize a test station with minimal configuration.
 * @details Helper to reduce boilerplate in tests that need a station.
 *          Creates a basic primary NRM station with modulo 8.
 */
static int32_t init_test_station(iohdlc_station_t *station,
                                 uint8_t *frame_arena,
                                 ioHdlcDriver *driver,
                                 uint32_t addr) {
  iohdlc_station_config_t config;

  /* Initialize mock driver with VMT */
  memset(driver, 0, sizeof *driver);
  driver->vmt = &mock_vmt;

  /* Configure station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;  /* Primary station */
  config.log2mod = 3;  /* Modulo 8 */
  config.addr = addr;
  config.driver = driver;
  config.frame_arena = frame_arena;
  config.frame_arena_size = 1024;  /* Reasonable size */
  config.max_info_len = 0;  /* Auto */
  config.pool_watermark = 0;  /* Auto: 10% min 8 */
  config.fff_type = 1;  /* TYPE0 */
  config.optfuncs = NULL;  /* Use defaults: REJ, SST, INH, FFF enabled */
  config.phydriver = NULL;
  config.phydriver_config = NULL;
  config.reply_timeout_ms = 0;  /* Use default (100ms) */

  /* Initialize station */
  memset(station, 0, sizeof *station);
  return ioHdlcStationInit(station, &config);
}

/*===========================================================================*/
/* Test: Station Creation                                                    */
/*===========================================================================*/

bool test_station_creation(void) {
  iohdlc_station_t station;
  uint8_t frame_arena[1024];
  ioHdlcDriver mock_driver;
  int32_t result;

  /* Initialize station using helper */
  result = init_test_station(&station, frame_arena, &mock_driver, PRIMARY_ADDR);

  /* Validate initialization */
  TEST_ASSERT(result == 0, "Station init should succeed");
  TEST_ASSERT(station.addr == PRIMARY_ADDR, "Station address should match");
  TEST_ASSERT(station.mode == IOHDLC_OM_NDM, "Station mode should be NDM");
  TEST_ASSERT(station.flags == IOHDLC_FLG_PRI, "Station should be primary");
  TEST_ASSERT(station.framing.modmask == 7, "Modulo 8 should have modmask 7");
  TEST_ASSERT(station.framing.ctrl_size == 1, "Modulo 8 should have ctrl_size 1");
  TEST_ASSERT(station.frame_pool.framesize > 0, "Frame pool should be initialized");
  TEST_ASSERT(station.driver == &mock_driver, "Driver should be set");

  test_printf("✅ Station creation and initialization successful\n");
  return 0;
}

/*===========================================================================*/
/* Test: Peer Creation                                                       */
/*===========================================================================*/

bool test_peer_creation(void) {
  iohdlc_station_t station;
  uint8_t frame_arena[1024];
  ioHdlcDriver mock_driver;
  iohdlc_station_peer_t peer;
  int32_t result;

  /* Initialize station */
  result = init_test_station(&station, frame_arena, &mock_driver, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Station init should succeed");

  /* Add peer to station */
  result = ioHdlcAddPeer(&station, &peer, SECONDARY_ADDR);

  /* Validate peer initialization */
  TEST_ASSERT(result == 0, "Peer add should succeed");
  TEST_ASSERT(peer.addr == SECONDARY_ADDR, "Peer address should match");
  TEST_ASSERT(peer.stationp == &station, "Peer should reference station");
  TEST_ASSERT(peer.ks == 7, "Peer ks should match modmask (7)");
  TEST_ASSERT(peer.kr == 7, "Peer kr should match modmask (7)");
  
  /* Validate mifl calculation: framesize - (FFF + ADDR + CTRL + FCS + 1)
     Should use actual frame_pool.framesize, not the constant FRAME_SIZE */

  uint32_t expected_mifl = station.frame_pool.framesize -
                           (station.framing.frame_offset + 1 +
                            station.framing.ctrl_size + station.fcs_size + 1);
  TEST_ASSERT(peer.mifls == expected_mifl, "Peer mifls should be calculated correctly");
  TEST_ASSERT(peer.miflr == expected_mifl, "Peer miflr should be calculated correctly");
  
  /* Validate queues are initialized (empty) */
  TEST_ASSERT(ioHdlc_frameq_isempty(&peer.i_recept_q), "Peer i_recept_q should be empty");
  TEST_ASSERT(ioHdlc_frameq_isempty(&peer.i_retrans_q), "Peer i_retrans_q should be empty");
  TEST_ASSERT(ioHdlc_frameq_isempty(&peer.i_trans_q), "Peer i_trans_q should be empty");
  
  /* Validate peer is in station's peer list */
  iohdlc_station_peer_t *found_peer = ioHdlcAddr2peer(&station, SECONDARY_ADDR);
  TEST_ASSERT(found_peer == &peer, "Peer should be findable in station's peer list");
  
  /* Test duplicate address rejection */
  iohdlc_station_peer_t duplicate_peer;
  result = ioHdlcAddPeer(&station, &duplicate_peer, SECONDARY_ADDR);
  TEST_ASSERT(result == -1, "Adding duplicate peer should fail");
  TEST_ASSERT(iohdlc_errno == EEXIST, "Error should be EEXIST");

  test_printf("✅ Peer creation and initialization successful\n");
  return 0;
}

/*===========================================================================*/
/* Test: Software Driver FCS Backend Capabilities                            */
/*===========================================================================*/

bool test_swdriver_fcs_backend_capabilities(void) {
  ioHdlcSwDriver sw_driver;
  ioHdlcSwDriver hw_driver;
  ioHdlcSwDriverInitConfig init_config;
  test_fcs_backend_probe_t probe;
  const ioHdlcDriverCapabilities *caps;

  memset(&probe, 0, sizeof probe);
  memset(&init_config, 0, sizeof init_config);
  init_config.fcs_backend = &test_fcs_backend;
  init_config.fcs_backend_ctx = &probe;

  ioHdlcSwDriverInit(&sw_driver, NULL);
  caps = hdlcGetCapabilities((ioHdlcDriver *)&sw_driver);
  TEST_ASSERT(caps != NULL, "Software driver capabilities should be available");
  TEST_ASSERT(caps->fcs.default_size == 2U, "Software driver default FCS should be 2");
  TEST_ASSERT(caps->fcs.supported_sizes[0] == 0U, "Software driver should keep the no-FCS slot");
  TEST_ASSERT(caps->fcs.supported_sizes[1] == 2U, "Software driver should expose FCS-16 support");

  ioHdlcSwDriverInit(&hw_driver, &init_config);
  caps = hdlcGetCapabilities((ioHdlcDriver *)&hw_driver);
  TEST_ASSERT(caps != NULL, "Backend driver capabilities should be available");
  TEST_ASSERT(caps->fcs.default_size == 2U, "Backend should preserve default FCS size");
  TEST_ASSERT(caps->fcs.supported_sizes[0] == 0U, "Supported FCS size list should keep software fallback");
  TEST_ASSERT(caps->fcs.supported_sizes[1] == 2U, "Supported FCS size list should retain FCS-16");

  test_printf("✅ Software-driver FCS backend capabilities successful\n");
  return 0;
}

/*===========================================================================*/
/* Test: SNRM Handshake - Two Connected Stations                            */
/*===========================================================================*/

bool test_snrm_handshake(const test_adapter_t *adapter) {
  /* Two stations connected via adapter endpoints */
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  int32_t result;

  /* Get stream ports from adapter */
  ioHdlcStreamPort port_primary = adapter->get_port_a();
  ioHdlcStreamPort port_secondary = adapter->get_port_b();
  
  /* Initialize stream drivers */
  ioHdlcSwDriverInit(&driver_primary, NULL);
  ioHdlcSwDriverInit(&driver_secondary, NULL);
  
  /* Configure primary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;
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
  config.reply_timeout_ms = 0;  /* Use default (100ms) */
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");
  
  /* Configure secondary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = 0;  /* Secondary */
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
  config.reply_timeout_ms = 0;  /* Use default (100ms) */
  
  memset(&station_secondary, 0, sizeof station_secondary);
  result = ioHdlcStationInit(&station_secondary, &config);
  TEST_ASSERT(result == 0, "Secondary station init failed");
  
  /* Add peers */
  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Add peer to primary failed");
  
  result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Add peer to secondary failed");
  
  /* Verify initial disconnected state */
  TEST_ASSERT(IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be disconnected initially");
  TEST_ASSERT(IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be disconnected initially");
  
  /* Start runner threads for both stations */
  test_printf("Starting runner threads...\n");
  result = ioHdlcRunnerStart(&station_primary);
  TEST_ASSERT(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&station_secondary);
  TEST_ASSERT(result == 0, "Failed to start secondary runner");
  
  /* Allow time for threads to initialize and register listeners */
  ioHdlc_sleep_ms(50);  /* 50 ms */
  
  /* Initiate connection from primary to secondary */
  test_printf("Calling ioHdlcStationLinkUp...\n");
  int ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM); // DEBUG: Set breakpoint here
  if (ret != 0) {
    test_printf("❌ ioHdlcStationLinkUp returned %d, errno=%d\n", ret, iohdlc_errno);
  }
  TEST_ASSERT(ret == 0, "ioHdlcStationLinkUp should succeed");
  
  /* Allow time for protocol exchange (SNRM → UA) */
  ioHdlc_sleep_ms(100);  /* 100 ms */
  
  /* Verify connection established at both ends */
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  TEST_ASSERT(iohdlc_errno == 0, "Primary station should have no errors");
  TEST_ASSERT(iohdlc_errno == 0, "Secondary station should have no errors");
  
  test_printf("✅ SNRM handshake completed successfully\n");
  
  TEST_ASSERT(ioHdlcStationDeinit(&station_primary) == 0,
              "Primary deinit should succeed");
  TEST_ASSERT(ioHdlcStationDeinit(&station_secondary) == 0,
              "Secondary deinit should succeed");
  TEST_ASSERT(ioHdlcStationDeinit(&station_primary) == 0,
              "Primary deinit should be idempotent");
  TEST_ASSERT(ioHdlcStationDeinit(&station_secondary) == 0,
              "Secondary deinit should be idempotent");
  
  return 0;
}

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
bool test_data_exchange(const test_adapter_t *adapter) {
  int test_result = 0;  /* Success by default, set to 1 on failure */
  
  /* Test message */
  const char *test_msg = "Hello ioHdlc, welcome in the HDLC world.";
  size_t msg_len = strlen(test_msg);
  
  /* Setup: same as test_snrm_handshake */
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  int32_t result;
  
  /* Get stream ports from adapter */
  ioHdlcStreamPort port_primary = adapter->get_port_a();
  ioHdlcStreamPort port_secondary = adapter->get_port_b();
  
  /* Initialize stream drivers */
  ioHdlcSwDriverInit(&driver_primary, NULL);
  ioHdlcSwDriverInit(&driver_secondary, NULL);
  
  /* Configure primary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;  /* | IOHDLC_FLG_TWA; */
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
  config.reply_timeout_ms = 0;  /* Use default (100ms) */
  
  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");
  
  /* Configure secondary station */
  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = 0; /* |IOHDLC_FLG_TWA; */
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
  config.reply_timeout_ms = 0;  /* Use default (100ms) */

  memset(&station_secondary, 0, sizeof station_secondary);
  result = ioHdlcStationInit(&station_secondary, &config);
  TEST_ASSERT(result == 0, "Secondary station init failed");
  
  /* Add peers */
  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Add peer to primary failed");
  
  result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Add peer to secondary failed");
  
  /* Start runner threads */
  result = ioHdlcRunnerStart(&station_primary);
  TEST_ASSERT(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&station_secondary);
  TEST_ASSERT(result == 0, "Failed to start secondary runner");
  
  ioHdlc_sleep_ms(50);
  
  /* Establish connection (SNRM handshake) */
  int ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  if (ret != 0) {
    test_printf("LinkUp returned error: %d\n", ret);
  }
  TEST_ASSERT_GOTO(ret == 0, "LinkUp failed");
  
  ioHdlc_sleep_ms(100);
  
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  
  test_printf("Connection established, starting data exchange...\n");
  
  /* Declare buffers */
  char recv_buf[128];
  char echo_buf[128];
  
  /* Primary sends message to secondary */
  int i;
  ssize_t sent;

  test_printf("Primary sending %u bytes...\n", (uint32_t)(msg_len*10));
  for (i = 0; i < 10; ++i) {
    sent = ioHdlcWriteTmo(&peer_at_primary, test_msg, msg_len, 500);
    if (sent != (ssize_t)msg_len) {
      test_printf("❌ Primary write returned %d (expected %u), errno=%d\n", 
                  (int)sent, (unsigned int)msg_len, iohdlc_errno);
    }
    TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Primary write failed");
    test_printf("Primary sent %d bytes\n", (int)sent);
  }
  ioHdlc_sleep_ms(500);

  /* Secondary receives message */
  memset(recv_buf, 0, sizeof recv_buf);
  ssize_t received;
  for (i = 0; i < 10; ++i) {
    received = ioHdlcReadTmo(&peer_at_secondary, recv_buf, msg_len, 500);
    test_printf("Secondary read returned %d bytes (expected %u), errno=%d\n",
                (int32_t)received, (uint32_t)msg_len, iohdlc_errno);
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
  test_printf("Secondary received %d bytes: \"%s\"\n", (int32_t)received, recv_buf);
  
  /* Secondary echoes message back to primary */
  sent = ioHdlcWriteTmo(&peer_at_secondary, recv_buf, received, 500);
  TEST_ASSERT_GOTO(sent == received, "Secondary echo write failed");
  test_printf("Secondary echoed %d bytes\n", (int32_t)sent);
  
  /* Primary receives echo */
  memset(echo_buf, 0, sizeof echo_buf);
  received = ioHdlcReadTmo(&peer_at_primary, echo_buf, 40 /*sizeof echo_buf - 1*/, 500);
  test_printf("Primary received echo %d bytes: \"%s\"\n", (int32_t)received, echo_buf);
  TEST_ASSERT_GOTO(received == (ssize_t)msg_len, "Primary echo read failed");
  TEST_ASSERT_GOTO(memcmp(echo_buf, test_msg, msg_len) == 0, "Echo data mismatch");
  test_printf("Primary received echo %d bytes: \"%s\"\n", (int32_t)received, echo_buf);

  ioHdlc_sleep_ms(200);
  
  /* Disconnect */
  ret = ioHdlcStationLinkDown(&station_primary, SECONDARY_ADDR);
  TEST_ASSERT_GOTO(ret == 0, "LinkDown failed");
  
test_cleanup:
  ioHdlc_sleep_ms(200);
  ioHdlcStationDeinit(&station_primary);
  ioHdlcStationDeinit(&station_secondary);
  
  return test_result;
}

/*===========================================================================*/
/* Test: Data Exchange with FCS Backend                                     */
/*===========================================================================*/

bool test_data_exchange_with_fcs_backend(const test_adapter_t *adapter) {
  int test_result = 0;
  const char *test_msg = "FCS backend path";
  size_t msg_len = strlen(test_msg);
  ioHdlcSwDriver driver_primary, driver_secondary;
  test_fcs_backend_probe_t probe_primary, probe_secondary;
  ioHdlcSwDriverInitConfig init_primary, init_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  ioHdlcStreamPort port_primary = adapter->get_port_a();
  ioHdlcStreamPort port_secondary = adapter->get_port_b();
  const ioHdlcDriverCapabilities *caps;
  char recv_buf[64];
  ssize_t sent;
  ssize_t received;
  int32_t result;
  int ret;

  memset(&station_primary, 0, sizeof station_primary);
  memset(&station_secondary, 0, sizeof station_secondary);
  memset(&probe_primary, 0, sizeof probe_primary);
  memset(&probe_secondary, 0, sizeof probe_secondary);
  memset(&init_primary, 0, sizeof init_primary);
  memset(&init_secondary, 0, sizeof init_secondary);
  init_primary.fcs_backend = &test_fcs_backend;
  init_primary.fcs_backend_ctx = &probe_primary;
  init_secondary.fcs_backend = &test_fcs_backend;
  init_secondary.fcs_backend_ctx = &probe_secondary;

  ioHdlcSwDriverInit(&driver_primary, &init_primary);
  ioHdlcSwDriverInit(&driver_secondary, &init_secondary);

  caps = hdlcGetCapabilities((ioHdlcDriver *)&driver_primary);
  TEST_ASSERT_GOTO(caps != NULL, "Primary driver capabilities should be available");
  TEST_ASSERT_GOTO(caps->fcs.supported_sizes[1] == 2U,
                   "Primary driver should expose FCS-16 support");

  caps = hdlcGetCapabilities((ioHdlcDriver *)&driver_secondary);
  TEST_ASSERT_GOTO(caps != NULL, "Secondary driver capabilities should be available");
  TEST_ASSERT_GOTO(caps->fcs.supported_sizes[1] == 2U,
                   "Secondary driver should expose FCS-16 support");

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.fff_type = 1;
  config.phydriver = &port_primary;

  memset(&station_primary, 0, sizeof station_primary);
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT_GOTO(result == 0, "Primary station init failed");

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_NDM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = sizeof shared_arena_secondary;
  config.fff_type = 1;
  config.phydriver = &port_secondary;

  memset(&station_secondary, 0, sizeof station_secondary);
  result = ioHdlcStationInit(&station_secondary, &config);
  TEST_ASSERT_GOTO(result == 0, "Secondary station init failed");

  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to primary failed");
  result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to secondary failed");

  result = ioHdlcRunnerStart(&station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start secondary runner");

  ioHdlc_sleep_ms(50);

  ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(ret == 0, "LinkUp failed");

  ioHdlc_sleep_ms(100);

  sent = ioHdlcWriteTmo(&peer_at_primary, test_msg, msg_len, 500);
  TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Primary write failed");

  memset(recv_buf, 0, sizeof recv_buf);
  received = ioHdlcReadTmo(&peer_at_secondary, recv_buf, sizeof recv_buf, 500);
  TEST_ASSERT_GOTO(received == (ssize_t)msg_len, "Secondary read failed");
  TEST_ASSERT_GOTO(memcmp(recv_buf, test_msg, msg_len) == 0, "Received data mismatch");

  TEST_ASSERT_GOTO(probe_primary.check_calls > 0U, "Primary FCS backend check should be used");
  TEST_ASSERT_GOTO(probe_secondary.check_calls > 0U, "Secondary FCS backend check should be used");
  TEST_ASSERT_GOTO(probe_primary.compute_calls > 0U, "Primary FCS backend compute should be used");
  TEST_ASSERT_GOTO(probe_secondary.compute_calls > 0U, "Secondary FCS backend compute should be used");
  TEST_ASSERT_GOTO(probe_secondary.last_fcs_size == 2U, "Backend should validate FCS-16 frames");

test_cleanup:
  ioHdlc_sleep_ms(100);
  ioHdlcStationDeinit(&station_primary);
  ioHdlcStationDeinit(&station_secondary);

  return test_result;
}
