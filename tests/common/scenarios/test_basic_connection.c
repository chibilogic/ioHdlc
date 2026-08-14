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
#include "ioHdlc_app_events.h"
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

typedef struct {
  iohdlc_station_peer_t *peerp;
  iohdlc_binary_semaphore_t started;
  const uint8_t *datap;
  size_t size;
  ssize_t result;
  int error;
} reset_write_context_t;

static uint8_t reset_write_payload[2U * FRAME_SIZE];

/**
 * @brief   Runs the logical write interrupted by the recovery SNRM test.
 */
static void *reset_write_thread(void *arg) {
  reset_write_context_t *contextp = arg;

  iohdlc_bsem_signal(&contextp->started);
  contextp->result = ioHdlcWriteTmo(contextp->peerp, contextp->datap,
                                    contextp->size, 1000U);
  contextp->error = iohdlc_errno;
  return NULL;
}

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
  .modulo = {
    .supported_log2mods = {3, 7, 0, 0},
  },
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

typedef struct {
  ioHdlcSwDriver driver_primary;
  ioHdlcSwDriver driver_secondary;
  iohdlc_station_t station_primary;
  iohdlc_station_t station_secondary;
  iohdlc_station_peer_t peer_at_primary;
  iohdlc_station_peer_t peer_at_secondary;
  ioHdlcStreamPort port_primary;
  ioHdlcStreamPort port_secondary;
} test_link_pair_t;

static int32_t init_test_link_pair(const test_adapter_t *adapter,
                                   test_link_pair_t *pair,
                                   uint8_t primary_mode,
                                   uint8_t primary_flags,
                                   uint8_t secondary_mode,
                                   uint8_t secondary_flags) {
  iohdlc_station_config_t config;
  int32_t result;

  memset(pair, 0, sizeof *pair);
  pair->port_primary = adapter->get_port_a();
  pair->port_secondary = adapter->get_port_b();

  ioHdlcSwDriverInit(&pair->driver_primary, NULL);
  ioHdlcSwDriverInit(&pair->driver_secondary, NULL);

  memset(&config, 0, sizeof config);
  config.mode = primary_mode;
  config.flags = primary_flags;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&pair->driver_primary;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.max_info_len = 0;
  config.pool_watermark = 0;
  config.fff_type = 1;
  config.optfuncs = NULL;
  config.phydriver = &pair->port_primary;
  config.phydriver_config = NULL;
  config.reply_timeout_ms = 0;

  result = ioHdlcStationInit(&pair->station_primary, &config);
  if (result != 0)
    return result;

  memset(&config, 0, sizeof config);
  config.mode = secondary_mode;
  config.flags = secondary_flags;
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&pair->driver_secondary;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = sizeof shared_arena_secondary;
  config.max_info_len = 0;
  config.pool_watermark = 0;
  config.fff_type = 1;
  config.optfuncs = NULL;
  config.phydriver = &pair->port_secondary;
  config.phydriver_config = NULL;
  config.reply_timeout_ms = 0;

  result = ioHdlcStationInit(&pair->station_secondary, &config);
  if (result != 0)
    return result;

  result = ioHdlcAddPeer(&pair->station_primary, &pair->peer_at_primary,
                         SECONDARY_ADDR);
  if (result != 0)
    return result;

  return ioHdlcAddPeer(&pair->station_secondary, &pair->peer_at_secondary,
                       PRIMARY_ADDR);
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
  TEST_ASSERT(ioHdlcPeerGetState(&peer) == IOHDLC_PEER_STATE_DISCONNECTED,
              "New peer should report disconnected state");
  
  /* Test duplicate address rejection */
  iohdlc_station_peer_t duplicate_peer;
  result = ioHdlcAddPeer(&station, &duplicate_peer, SECONDARY_ADDR);
  TEST_ASSERT(result == -1, "Adding duplicate peer should fail");
  TEST_ASSERT(iohdlc_errno == EEXIST, "Error should be EEXIST");

  test_printf("✅ Peer creation and initialization successful\n");
  return 0;
}

/*===========================================================================*/
/* Test: Application Event Listener                                          */
/*===========================================================================*/

bool test_application_event_listener(void) {
  iohdlc_station_t station;
  uint8_t frame_arena[1024];
  ioHdlcDriver mock_driver;
  iohdlc_app_listener_t listener_a, listener_b;
  eventflags_t flags;
  int32_t result;

  result = init_test_station(&station, frame_arena, &mock_driver, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Station init should succeed");

  result = ioHdlcAppListenerRegister(&station, &listener_a, EVENT_MASK(0),
                                     IOHDLC_APP_LINK_UP |
                                     IOHDLC_APP_LINK_LOST);
  TEST_ASSERT(result == 0, "First application listener registration failed");
  result = ioHdlcAppListenerRegister(&station, &listener_b, EVENT_MASK(1),
                                     IOHDLC_APP_LINK_LOST);
  TEST_ASSERT(result == 0, "Second application listener registration failed");

  ioHdlcBroadcastFlagsApp(&station, IOHDLC_APP_LINK_UP);
  flags = ioHdlcAppListenerWait(&listener_a, 0U);
  TEST_ASSERT(flags == IOHDLC_APP_LINK_UP,
              "Interested listener should receive LINK_UP");
  flags = ioHdlcAppListenerWait(&listener_b, 0U);
  TEST_ASSERT(flags == 0U,
              "Filtered listener should not receive LINK_UP");

  ioHdlcBroadcastFlagsApp(&station, IOHDLC_APP_LINK_LOST);
  ioHdlcBroadcastFlagsApp(&station, IOHDLC_APP_LINK_LOST);
  flags = ioHdlcAppListenerWait(&listener_a, 0U);
  TEST_ASSERT(flags == IOHDLC_APP_LINK_LOST,
              "Repeated application flags should coalesce");
  flags = ioHdlcAppListenerWait(&listener_b, 0U);
  TEST_ASSERT(flags == IOHDLC_APP_LINK_LOST,
              "Application events should be broadcast to all listeners");

  ioHdlcAppListenerUnregister(&listener_b);
  ioHdlcAppListenerUnregister(&listener_a);

  TEST_ASSERT(ioHdlcPeerGetState(NULL) == IOHDLC_PEER_STATE_INVALID,
              "Null peer should report invalid state");
  TEST_ASSERT(iohdlc_errno == EINVAL,
              "Null peer state query should report EINVAL");
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
  TEST_ASSERT(caps->modulo.supported_log2mods[0] == 3U,
              "Software driver should expose modulo-8 support");
  TEST_ASSERT(caps->modulo.supported_log2mods[1] == 7U,
              "Software driver should expose modulo-128 support");
  TEST_ASSERT(caps->fcs.default_size == 2U, "Software driver default FCS should be 2");
  TEST_ASSERT(caps->fcs.supported_sizes[0] == 0U, "Software driver should keep the no-FCS slot");
  TEST_ASSERT(caps->fcs.supported_sizes[1] == 2U, "Software driver should expose FCS-16 support");

  ioHdlcSwDriverInit(&hw_driver, &init_config);
  caps = hdlcGetCapabilities((ioHdlcDriver *)&hw_driver);
  TEST_ASSERT(caps != NULL, "Backend driver capabilities should be available");
  TEST_ASSERT(caps->modulo.supported_log2mods[0] == 3U,
              "Backend driver should preserve modulo-8 support");
  TEST_ASSERT(caps->modulo.supported_log2mods[1] == 7U,
              "Backend driver should preserve modulo-128 support");
  TEST_ASSERT(caps->fcs.default_size == 2U, "Backend should preserve default FCS size");
  TEST_ASSERT(caps->fcs.supported_sizes[0] == 0U, "Supported FCS size list should keep software fallback");
  TEST_ASSERT(caps->fcs.supported_sizes[1] == 2U, "Supported FCS size list should retain FCS-16");

  test_printf("✅ Software-driver FCS backend capabilities successful\n");
  return 0;
}

bool test_swdriver_rejects_unsupported_modulo(void) {
  ioHdlcSwDriver sw_driver;
  iohdlc_station_t station;
  iohdlc_station_config_t config;
  uint8_t frame_arena[1024];
  int32_t result;

  ioHdlcSwDriverInit(&sw_driver, NULL);

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
  config.log2mod = 15;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&sw_driver;
  config.frame_arena = frame_arena;
  config.frame_arena_size = sizeof frame_arena;
  config.fff_type = 1;

  memset(&station, 0, sizeof station);
  result = ioHdlcStationInit(&station, &config);

  TEST_ASSERT(result == -1, "Station init should reject unsupported modulo");
  TEST_ASSERT(iohdlc_errno == ENOTSUP, "Unsupported modulo should report ENOTSUP");
  return 0;
}

bool test_read_zero_length_returns_zero(void) {
  iohdlc_station_t station;
  uint8_t frame_arena[1024];
  ioHdlcDriver mock_driver;
  iohdlc_station_peer_t peer;
  char dummy = 0;
  int32_t result;
  ssize_t received;

  result = init_test_station(&station, frame_arena, &mock_driver, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Station init should succeed");

  result = ioHdlcAddPeer(&station, &peer, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Peer add should succeed");

  received = ioHdlcReadTmo(&peer, &dummy, 0U, 0U);
  TEST_ASSERT(received == 0, "Zero-length read should return 0");

  return 0;
}

bool test_read_never_connected_returns_enotconn(void) {
  iohdlc_station_t station;
  uint8_t frame_arena[1024];
  ioHdlcDriver mock_driver;
  iohdlc_station_peer_t peer;
  char dummy = 0;
  int32_t result;
  ssize_t received;

  result = init_test_station(&station, frame_arena, &mock_driver, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Station init should succeed");

  result = ioHdlcAddPeer(&station, &peer, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Peer add should succeed");

  received = ioHdlcReadTmo(&peer, &dummy, 1U, 0U);
  TEST_ASSERT(received == -1, "Read on never-connected peer should fail");
  TEST_ASSERT(iohdlc_errno == ENOTCONN, "Never-connected read should report ENOTCONN");

  return 0;
}

bool test_read_reports_pending_terminal(void) {
  iohdlc_station_t station;
  uint8_t frame_arena[1024];
  ioHdlcDriver mock_driver;
  iohdlc_station_peer_t peer;
  char dummy = 0;
  int32_t result;
  ssize_t received;

  result = init_test_station(&station, frame_arena, &mock_driver, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Station init should succeed");

  result = ioHdlcAddPeer(&station, &peer, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Peer add should succeed");

  peer.ss_state = IOHDLC_SS_ST_CONN;
  peer.stream_terminal_pending = IOHDLC_SS_TERM_ABORTED |
                                 IOHDLC_STREAM_TX_RESET_PENDING;
  received = ioHdlcReadTmo(&peer, &dummy, 1U, 0U);
  TEST_ASSERT(received == -1,
              "Reconnected read should report the pending abort");
  TEST_ASSERT(iohdlc_errno == ECONNRESET,
              "Pending abort should report ECONNRESET");
  TEST_ASSERT(peer.stream_terminal_pending ==
              IOHDLC_STREAM_TX_RESET_PENDING,
              "Reader should only consume the pending RX abort");

  peer.stream_terminal_pending = IOHDLC_SS_TERM_ORDERLY;
  received = ioHdlcReadTmo(&peer, &dummy, 1U, 0U);
  TEST_ASSERT(received == 0,
              "Reconnected read should report the pending orderly EOF");
  TEST_ASSERT(peer.stream_terminal_pending == 0U,
              "Reader should consume the pending orderly EOF");

  return 0;
}

bool test_connected_snrm_resets_stream_io(const test_adapter_t *adapter) {
  int test_result = 0;
  test_link_pair_t pair;
  reset_write_context_t write_context = {0};
  reset_write_context_t queued_write_context = {0};
  iohdlc_thread_t *write_thread = NULL;
  iohdlc_thread_t *queued_write_thread = NULL;
  const char message[] = "New HDLC stream";
  const char queued_message[] = "Queued after reset";
  char receive_buffer[sizeof message];
  char queued_receive_buffer[sizeof queued_message];
  char dummy;
  uint32_t writer_margin;
  uint32_t writer_limit;
  uint32_t wait_count;
  ssize_t transferred;
  int32_t result;
  bool write_active = false;

  result = init_test_link_pair(adapter, &pair,
                               IOHDLC_OM_NDM, IOHDLC_FLG_PRI,
                               IOHDLC_OM_NDM, 0U);
  TEST_ASSERT_GOTO(result == 0, "Link pair init failed");

  result = ioHdlcRunnerStart(&pair.station_primary);
  TEST_ASSERT_GOTO(result == 0, "Primary runner start failed");
  result = ioHdlcRunnerStart(&pair.station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Secondary runner start failed");
  ioHdlc_sleep_ms(50U);

  result = ioHdlcStationLinkUp(&pair.station_primary, SECONDARY_ADDR,
                               IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(result == 0, "Initial LinkUp failed");

  /* Let one fragment enter the old stream, then hold the second fragment in
     flow control until the recovery SNRM terminates the active write. */
  writer_margin = pair.peer_at_secondary.ks /
                  IOHDLC_WRITER_PENDING_MARGIN_DIVISOR;
  if (writer_margin < IOHDLC_WRITER_PENDING_MARGIN_MIN)
    writer_margin = IOHDLC_WRITER_PENDING_MARGIN_MIN;
  writer_limit = pair.peer_at_secondary.ks + writer_margin;
  TEST_ASSERT_GOTO(pair.peer_at_secondary.mifls + 1U <=
                   sizeof reset_write_payload,
                   "Reset write payload is too small");
  iohdlc_mutex_lock(&pair.peer_at_secondary.state_mutex);
  pair.peer_at_secondary.i_pending_count = writer_limit - 1U;
  iohdlc_mutex_unlock(&pair.peer_at_secondary.state_mutex);
  write_context.peerp = &pair.peer_at_secondary;
  write_context.datap = reset_write_payload;
  write_context.size = pair.peer_at_secondary.mifls + 1U;
  iohdlc_bsem_init(&write_context.started, true);
  write_thread = iohdlc_thread_create("reset_writer", 0U, 0,
                                      reset_write_thread, &write_context);
  TEST_ASSERT_GOTO(write_thread != NULL, "Reset writer creation failed");
  TEST_ASSERT_GOTO(iohdlc_bsem_wait_timeout(&write_context.started, 1000U) ==
                   MSG_OK, "Reset writer did not start");
  for (wait_count = 0U; wait_count < 1000U; ++wait_count) {
    iohdlc_mutex_lock(&pair.peer_at_secondary.state_mutex);
    write_active = (pair.peer_at_secondary.ss_state &
                    IOHDLC_SS_SENDING) != 0U;
    iohdlc_mutex_unlock(&pair.peer_at_secondary.state_mutex);
    if (write_active)
      break;
    ioHdlc_sleep_ms(1U);
  }
  TEST_ASSERT_GOTO(write_active, "Reset writer was not admitted");

  /* This complete write remains behind write_gate. It has not entered the old
     stream and must be admitted on the connection established by the SNRM. */
  queued_write_context.peerp = &pair.peer_at_secondary;
  queued_write_context.datap = (const uint8_t *)queued_message;
  queued_write_context.size = sizeof queued_message;
  iohdlc_bsem_init(&queued_write_context.started, true);
  queued_write_thread = iohdlc_thread_create(
      "queued_writer", 0U, 0, reset_write_thread, &queued_write_context);
  TEST_ASSERT_GOTO(queued_write_thread != NULL,
                   "Queued writer creation failed");
  TEST_ASSERT_GOTO(
      iohdlc_bsem_wait_timeout(&queued_write_context.started, 1000U) ==
      MSG_OK, "Queued writer did not start");
  ioHdlc_sleep_ms(10U);

  /* Model asymmetric NRM loss: only the primary detects the failure. The
     secondary remains connected until the next SNRM resets its session. */
  iohdlc_mutex_lock(&pair.peer_at_primary.state_mutex);
  pair.peer_at_primary.ss_state &= (uint8_t)~IOHDLC_SS_ST_CONN;
  pair.peer_at_primary.ss_state |= IOHDLC_SS_TERM_ABORTED;
  pair.station_primary.connected_count--;
  iohdlc_mutex_unlock(&pair.peer_at_primary.state_mutex);

  result = ioHdlcStationLinkUp(&pair.station_primary, SECONDARY_ADDR,
                               IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(result == 0, "Recovery LinkUp failed");
  iohdlc_thread_join(write_thread);
  write_thread = NULL;
  iohdlc_thread_join(queued_write_thread);
  queued_write_thread = NULL;
  TEST_ASSERT_GOTO(write_context.result == -1,
                   "Old-stream write should fail across connected SNRM");
  TEST_ASSERT_GOTO(write_context.error == ECONNRESET,
                   "Old-stream write should report ECONNRESET");
  TEST_ASSERT_GOTO(queued_write_context.result ==
                   (ssize_t)sizeof queued_message,
                   "Writer waiting at the gate should enter the new stream");
  TEST_ASSERT_GOTO(pair.peer_at_secondary.stream_terminal_pending ==
                   IOHDLC_SS_TERM_ABORTED,
                   "Connected SNRM should terminate the previous RX stream");

  transferred = ioHdlcReadTmo(&pair.peer_at_primary, queued_receive_buffer,
                               sizeof queued_receive_buffer, 500U);
  TEST_ASSERT_GOTO(transferred == (ssize_t)sizeof queued_message,
                   "Queued new-stream write was not delivered");
  TEST_ASSERT_GOTO(memcmp(queued_receive_buffer, queued_message,
                          sizeof queued_message) == 0,
                   "Queued new-stream data mismatch");

  transferred = ioHdlcReadTmo(&pair.peer_at_secondary, &dummy, 1U, 0U);
  TEST_ASSERT_GOTO(transferred == -1,
                   "First read after connected SNRM should fail");
  TEST_ASSERT_GOTO(iohdlc_errno == ECONNRESET,
                   "Connected SNRM should report ECONNRESET");

  transferred = ioHdlcWriteTmo(&pair.peer_at_primary, message,
                                sizeof message, 500U);
  TEST_ASSERT_GOTO(transferred == (ssize_t)sizeof message,
                   "New-stream write failed");
  transferred = ioHdlcReadTmo(&pair.peer_at_secondary, receive_buffer,
                               sizeof receive_buffer, 500U);
  TEST_ASSERT_GOTO(transferred == (ssize_t)sizeof message,
                   "New-stream read failed");
  TEST_ASSERT_GOTO(memcmp(receive_buffer, message, sizeof message) == 0,
                   "New-stream data mismatch");

test_cleanup:
  if (write_thread != NULL || queued_write_thread != NULL) {
    iohdlc_mutex_lock(&pair.peer_at_secondary.state_mutex);
    pair.peer_at_secondary.stream_terminal_pending |=
        IOHDLC_STREAM_TX_RESET_PENDING;
    pair.peer_at_secondary.i_pending_count = 0U;
    iohdlc_condvar_broadcast(&pair.peer_at_secondary.tx_cv);
    iohdlc_mutex_unlock(&pair.peer_at_secondary.state_mutex);
  }
  iohdlc_thread_join(write_thread);
  iohdlc_thread_join(queued_write_thread);
  ioHdlcStationDeinit(&pair.station_primary);
  ioHdlcStationDeinit(&pair.station_secondary);
  return test_result;
}

bool test_vectored_io_validation(void) {
  iohdlc_station_t station;
  uint8_t frame_arena[1024];
  ioHdlcDriver mock_driver;
  iohdlc_station_peer_t peer;
  iohdlc_const_iovec_t write_iov[2];
  iohdlc_iovec_t read_iov[2];
  uint8_t byte = 0U;
  int32_t result;
  ssize_t transferred;

  result = init_test_station(&station, frame_arena, &mock_driver, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Station init should succeed");
  result = ioHdlcAddPeer(&station, &peer, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Peer add should succeed");

  TEST_ASSERT(ioHdlcWriteVTmo(&peer, NULL, 0U, 0U) == 0,
              "Empty vectored write should return zero");
  TEST_ASSERT(ioHdlcReadVTmo(&peer, NULL, 0U, 0U) == 0,
              "Empty vectored read should return zero");

  iohdlc_errno = 0;
  transferred = ioHdlcWriteVTmo(&peer, NULL, 1U, 0U);
  TEST_ASSERT(transferred == -1 && iohdlc_errno == EINVAL,
              "Non-empty vectored write should require an iovec array");
  iohdlc_errno = 0;
  transferred = ioHdlcReadVTmo(&peer, NULL, 1U, 0U);
  TEST_ASSERT(transferred == -1 && iohdlc_errno == EINVAL,
              "Non-empty vectored read should require an iovec array");

  write_iov[0].iov_base = NULL;
  write_iov[0].iov_len = 0U;
  TEST_ASSERT(ioHdlcWriteVTmo(&peer, write_iov, 1U, 0U) == 0,
              "Zero-length write vector should accept a null base");
  read_iov[0].iov_base = NULL;
  read_iov[0].iov_len = 0U;
  TEST_ASSERT(ioHdlcReadVTmo(&peer, read_iov, 1U, 0U) == 0,
              "Zero-length read vector should accept a null base");

  write_iov[0].iov_len = 1U;
  iohdlc_errno = 0;
  transferred = ioHdlcWriteVTmo(&peer, write_iov, 1U, 0U);
  TEST_ASSERT(transferred == -1 && iohdlc_errno == EINVAL,
              "Non-empty write vector should reject a null base");

  read_iov[0].iov_len = 1U;
  iohdlc_errno = 0;
  transferred = ioHdlcReadVTmo(&peer, read_iov, 1U, 0U);
  TEST_ASSERT(transferred == -1 && iohdlc_errno == EINVAL,
              "Non-empty read vector should reject a null base");

  write_iov[0].iov_base = &byte;
  write_iov[0].iov_len = (size_t)-1 >> 1;
  write_iov[1].iov_base = &byte;
  write_iov[1].iov_len = 1U;
  iohdlc_errno = 0;
  transferred = ioHdlcWriteVTmo(&peer, write_iov, 2U, 0U);
  TEST_ASSERT(transferred == -1 && iohdlc_errno == EINVAL,
              "Vectored write should reject totals above SSIZE_MAX");

  read_iov[0].iov_base = &byte;
  read_iov[0].iov_len = (size_t)-1 >> 1;
  read_iov[1].iov_base = &byte;
  read_iov[1].iov_len = 1U;
  iohdlc_errno = 0;
  transferred = ioHdlcReadVTmo(&peer, read_iov, 2U, 0U);
  TEST_ASSERT(transferred == -1 && iohdlc_errno == EINVAL,
              "Vectored read should reject totals above SSIZE_MAX");

  iohdlc_errno = 0;
  TEST_ASSERT(ioHdlcWriteTmo(&peer, &byte, 0U, 0U) == -1 &&
              iohdlc_errno == EINVAL,
              "Scalar zero-length write should retain EINVAL semantics");
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
  config.mode = IOHDLC_OM_ADM;
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

bool test_test_command_disconnected(const test_adapter_t *adapter) {
  int test_result = 0;
  test_link_pair_t pair;
  int32_t result;

  result = init_test_link_pair(adapter, &pair,
                               IOHDLC_OM_NDM, IOHDLC_FLG_PRI,
                               IOHDLC_OM_NDM, 0);
  TEST_ASSERT(result == 0, "TEST pair init failed");

  result = ioHdlcRunnerStart(&pair.station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&pair.station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start secondary runner");
  ioHdlc_sleep_ms(50);

  result = ioHdlcPeerTest(&pair.peer_at_primary, 0U, 200U);
  TEST_ASSERT_GOTO(result == 0, "Zero-length TEST should succeed in NDM");

  result = ioHdlcPeerTest(&pair.peer_at_primary, 16U, 200U);
  TEST_ASSERT_GOTO(result == 0, "Small TEST should succeed in NDM");

  result = ioHdlcPeerTest(&pair.peer_at_primary,
                          pair.peer_at_primary.mifls, 200U);
  TEST_ASSERT_GOTO(result == 0, "Maximum TEST should succeed in NDM");

  TEST_ASSERT_GOTO(IOHDLC_PEER_DISC(&pair.peer_at_primary),
                   "Primary peer should remain disconnected after TEST");
  TEST_ASSERT_GOTO(IOHDLC_PEER_DISC(&pair.peer_at_secondary),
                   "Secondary peer should remain disconnected after TEST");

  result = ioHdlcPeerTest(&pair.peer_at_secondary, 4U, 50U);
  TEST_ASSERT_GOTO(result == -1, "Secondary NDM TEST initiation should fail");
  TEST_ASSERT_GOTO(iohdlc_errno == ENOTSUP,
                   "Secondary NDM TEST initiation should report ENOTSUP");

  result = ioHdlcPeerTest(&pair.peer_at_primary,
                          pair.peer_at_primary.mifls + 1U, 50U);
  TEST_ASSERT_GOTO(result == -1, "Oversized TEST should fail");
  TEST_ASSERT_GOTO(iohdlc_errno == EMSGSIZE,
                   "Oversized TEST should report EMSGSIZE");

test_cleanup:
  TEST_ASSERT(ioHdlcStationDeinit(&pair.station_primary) == 0,
              "Primary deinit should succeed");
  TEST_ASSERT(ioHdlcStationDeinit(&pair.station_secondary) == 0,
              "Secondary deinit should succeed");
  return test_result;
}

bool test_test_command_connected(const test_adapter_t *adapter) {
  int test_result = 0;
  test_link_pair_t pair;
  int32_t result;

  result = init_test_link_pair(adapter, &pair,
                               IOHDLC_OM_NDM, IOHDLC_FLG_PRI,
                               IOHDLC_OM_NDM, 0);
  TEST_ASSERT(result == 0, "TEST pair init failed");

  result = ioHdlcRunnerStart(&pair.station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&pair.station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start secondary runner");
  ioHdlc_sleep_ms(50);

  result = ioHdlcStationLinkUp(&pair.station_primary, SECONDARY_ADDR,
                               IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(result == 0, "LinkUp should succeed before connected TEST");
  ioHdlc_sleep_ms(100);

  result = ioHdlcPeerTest(&pair.peer_at_primary, 32U, 200U);
  TEST_ASSERT_GOTO(result == 0, "TEST should succeed in connected NRM");

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&pair.peer_at_primary),
                   "Primary peer should remain connected after TEST");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&pair.peer_at_secondary),
                   "Secondary peer should remain connected after TEST");

test_cleanup:
  TEST_ASSERT(ioHdlcStationDeinit(&pair.station_primary) == 0,
              "Primary deinit should succeed");
  TEST_ASSERT(ioHdlcStationDeinit(&pair.station_secondary) == 0,
              "Secondary deinit should succeed");
  return test_result;
}

bool test_test_command_timeout_preserves_link_state(const test_adapter_t *adapter) {
  int test_result = 0;
  test_link_pair_t pair;
  int32_t result;

  result = init_test_link_pair(adapter, &pair,
                               IOHDLC_OM_NDM, IOHDLC_FLG_PRI,
                               IOHDLC_OM_NDM, 0);
  TEST_ASSERT(result == 0, "TEST pair init failed");

  result = ioHdlcRunnerStart(&pair.station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  ioHdlc_sleep_ms(50);

  result = ioHdlcPeerTest(&pair.peer_at_primary, 8U, 30U);
  TEST_ASSERT_GOTO(result == -1, "TEST without receiver should timeout");
  TEST_ASSERT_GOTO(iohdlc_errno == ETIMEDOUT,
                   "TEST timeout should report ETIMEDOUT");
  TEST_ASSERT_GOTO(IOHDLC_PEER_DISC(&pair.peer_at_primary),
                   "TEST timeout should not connect the peer");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_ABORTED(&pair.peer_at_primary),
                   "TEST timeout should not mark link aborted");

test_cleanup:
  TEST_ASSERT(ioHdlcStationDeinit(&pair.station_primary) == 0,
              "Primary deinit should succeed");
  TEST_ASSERT(ioHdlcStationDeinit(&pair.station_secondary) == 0,
              "Secondary deinit should succeed");
  return test_result;
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

bool test_vectored_io_exchange(const test_adapter_t *adapter) {
  int test_result = 0;
  test_link_pair_t pair;
  uint8_t expected[250];
  uint8_t rx_head[1];
  uint8_t rx_body[80];
  uint8_t rx_tail[169];
  uint8_t partial_head[2];
  uint8_t partial_tail[10];
  iohdlc_const_iovec_t write_iov[4];
  iohdlc_iovec_t read_iov[4];
  iohdlc_iovec_t partial_iov[2];
  int32_t result;
  ssize_t transferred;
  size_t i;

  result = init_test_link_pair(adapter, &pair,
                               IOHDLC_OM_NDM, IOHDLC_FLG_PRI,
                               IOHDLC_OM_NDM, 0);
  TEST_ASSERT(result == 0, "Vectored I/O pair init failed");

  result = ioHdlcRunnerStart(&pair.station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&pair.station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start secondary runner");
  ioHdlc_sleep_ms(50);

  result = ioHdlcStationLinkUp(&pair.station_primary, SECONDARY_ADDR,
                               IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(result == 0, "LinkUp should succeed for vectored I/O");

  for (i = 0U; i < sizeof expected; ++i)
    expected[i] = (uint8_t)(i * 37U + 11U);

  write_iov[0].iov_base = expected;
  write_iov[0].iov_len = 3U;
  write_iov[1].iov_base = NULL;
  write_iov[1].iov_len = 0U;
  write_iov[2].iov_base = expected + 3U;
  write_iov[2].iov_len = 117U;
  write_iov[3].iov_base = expected + 120U;
  write_iov[3].iov_len = sizeof expected - 120U;

  transferred = ioHdlcWriteVTmo(&pair.peer_at_primary, write_iov, 4U, 1000U);
  TEST_ASSERT_GOTO(transferred == (ssize_t)sizeof expected,
                   "Vectored write should queue the complete stream");

  memset(rx_head, 0, sizeof rx_head);
  memset(rx_body, 0, sizeof rx_body);
  memset(rx_tail, 0, sizeof rx_tail);
  read_iov[0].iov_base = rx_head;
  read_iov[0].iov_len = sizeof rx_head;
  read_iov[1].iov_base = rx_body;
  read_iov[1].iov_len = sizeof rx_body;
  read_iov[2].iov_base = NULL;
  read_iov[2].iov_len = 0U;
  read_iov[3].iov_base = rx_tail;
  read_iov[3].iov_len = sizeof rx_tail;

  transferred = ioHdlcReadVTmo(&pair.peer_at_secondary, read_iov, 4U, 1000U);
  TEST_ASSERT_GOTO(transferred == (ssize_t)sizeof expected,
                   "Vectored read should fill the complete destination");
  TEST_ASSERT_GOTO(memcmp(rx_head, expected, sizeof rx_head) == 0,
                   "Vectored read head should match");
  TEST_ASSERT_GOTO(memcmp(rx_body, expected + sizeof rx_head,
                          sizeof rx_body) == 0,
                   "Vectored read body should match");
  TEST_ASSERT_GOTO(memcmp(rx_tail,
                          expected + sizeof rx_head + sizeof rx_body,
                          sizeof rx_tail) == 0,
                   "Vectored read tail should match");

  TEST_ASSERT_GOTO(
      iohdlc_bsem_wait_timeout(&pair.peer_at_primary.write_gate, 0U) == MSG_OK,
      "Write gate should initially be available");
  iohdlc_errno = 0;
  transferred = ioHdlcWriteVTmo(&pair.peer_at_primary, write_iov, 1U, 0U);
  TEST_ASSERT_GOTO(transferred == -1 && iohdlc_errno == ETIMEDOUT,
                   "Competing write should time out at the write gate");
  iohdlc_bsem_signal(&pair.peer_at_primary.write_gate);

  TEST_ASSERT_GOTO(
      iohdlc_bsem_wait_timeout(&pair.peer_at_secondary.read_gate, 0U) == MSG_OK,
      "Read gate should initially be available");
  iohdlc_errno = 0;
  transferred = ioHdlcReadVTmo(&pair.peer_at_secondary, read_iov, 1U, 0U);
  TEST_ASSERT_GOTO(transferred == -1 && iohdlc_errno == ETIMEDOUT,
                   "Competing read should time out at the read gate");
  iohdlc_bsem_signal(&pair.peer_at_secondary.read_gate);

  transferred = ioHdlcWriteTmo(&pair.peer_at_primary, expected, 7U, 1000U);
  TEST_ASSERT_GOTO(transferred == 7, "Partial-read source write should succeed");

  memset(partial_head, 0, sizeof partial_head);
  memset(partial_tail, 0, sizeof partial_tail);
  partial_iov[0].iov_base = partial_head;
  partial_iov[0].iov_len = sizeof partial_head;
  partial_iov[1].iov_base = partial_tail;
  partial_iov[1].iov_len = sizeof partial_tail;
  iohdlc_errno = EAGAIN;
  transferred = ioHdlcReadVTmo(&pair.peer_at_secondary, partial_iov, 2U, 100U);
  TEST_ASSERT_GOTO(transferred == 7,
                   "Vectored read timeout should return transferred bytes");
  TEST_ASSERT_GOTO(iohdlc_errno == EAGAIN,
                   "Partial vectored read should preserve iohdlc_errno");
  TEST_ASSERT_GOTO(memcmp(partial_head, expected, sizeof partial_head) == 0,
                   "Partial vectored read head should match");
  TEST_ASSERT_GOTO(memcmp(partial_tail, expected + sizeof partial_head, 5U) == 0,
                   "Partial vectored read tail should match");

test_cleanup:
  ioHdlcStationDeinit(&pair.station_primary);
  ioHdlcStationDeinit(&pair.station_secondary);
  return test_result;
}

/**
 * @brief   Test best-effort UI delivery on an established link.
 * @details Validates:
 *          - UI send is rejected while disconnected
 *          - UI reception raises an application event
 *          - UI payload can be consumed once via ioHdlcPeerUiGet()
 *          - UI delivery does not alter V(S), V(R), or N(R)
 */
bool test_ui_exchange(const test_adapter_t *adapter) {
  int test_result = 0;
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  iohdlc_app_listener_t listener;
  bool listener_registered = false;
  int32_t result;
  int ret;
  uint32_t ui_value;
  uint32_t rx_value = 0U;
  uint32_t pri_vs, pri_vr, pri_nr;
  uint32_t sec_vs, sec_vr, sec_nr;
  eventflags_t flags;

  ioHdlcStreamPort port_primary = adapter->get_port_a();
  ioHdlcStreamPort port_secondary = adapter->get_port_b();

  ioHdlcSwDriverInit(&driver_primary, NULL);
  ioHdlcSwDriverInit(&driver_secondary, NULL);

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.max_info_len = 0;
  config.pool_watermark = 0;
  config.fff_type = 1;
  config.optfuncs = NULL;
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;
  config.reply_timeout_ms = 0;

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
  config.max_info_len = 0;
  config.pool_watermark = 0;
  config.fff_type = 1;
  config.optfuncs = NULL;
  config.phydriver = &port_secondary;
  config.phydriver_config = NULL;
  config.reply_timeout_ms = 0;

  memset(&station_secondary, 0, sizeof station_secondary);
  result = ioHdlcStationInit(&station_secondary, &config);
  TEST_ASSERT_GOTO(result == 0, "Secondary station init failed");

  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to primary failed");
  result = ioHdlcAddPeer(&station_secondary, &peer_at_secondary, PRIMARY_ADDR);
  TEST_ASSERT_GOTO(result == 0, "Add peer to secondary failed");

  ret = ioHdlcPeerUiSend(&peer_at_primary, 0x11223344U);
  TEST_ASSERT_GOTO(ret == -1, "UI send should fail while disconnected");
  TEST_ASSERT_GOTO(iohdlc_errno == ENOTCONN, "Disconnected UI send should report ENOTCONN");

  result = ioHdlcRunnerStart(&station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start secondary runner");

  ioHdlc_sleep_ms(50);

  ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(ret == 0, "LinkUp failed");

  ioHdlc_sleep_ms(100);

  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  TEST_ASSERT_GOTO(!ioHdlcPeerUiGet(&peer_at_secondary, &rx_value),
                   "UI cache should be empty before transmission");

  pri_vs = peer_at_primary.vs;
  pri_vr = peer_at_primary.vr;
  pri_nr = peer_at_primary.nr;
  sec_vs = peer_at_secondary.vs;
  sec_vr = peer_at_secondary.vr;
  sec_nr = peer_at_secondary.nr;

  result = ioHdlcAppListenerRegister(&station_secondary, &listener,
                                     EVENT_MASK(0), IOHDLC_APP_UI_RECEIVED);
  TEST_ASSERT_GOTO(result == 0, "UI listener registration failed");
  listener_registered = true;

  ui_value = 0xA5B4C3E2U;
  ret = ioHdlcPeerUiSend(&peer_at_primary, ui_value);
  TEST_ASSERT_GOTO(ret == 0, "Connected UI send should succeed");

  flags = ioHdlcAppListenerWait(&listener, 1000U);
  TEST_ASSERT_GOTO((flags & IOHDLC_APP_UI_RECEIVED) != 0U,
                   "Timed out waiting for UI event");

  TEST_ASSERT_GOTO(ioHdlcPeerUiGet(&peer_at_secondary, &rx_value),
                   "UI payload should be available");
  TEST_ASSERT_GOTO(rx_value == ui_value, "UI payload mismatch");
  TEST_ASSERT_GOTO(!ioHdlcPeerUiGet(&peer_at_secondary, &rx_value),
                   "UI payload should be consumed after first read");

  TEST_ASSERT_GOTO(peer_at_primary.vs == pri_vs, "UI must not alter primary V(S)");
  TEST_ASSERT_GOTO(peer_at_primary.vr == pri_vr, "UI must not alter primary V(R)");
  TEST_ASSERT_GOTO(peer_at_primary.nr == pri_nr, "UI must not alter primary N(R)");
  TEST_ASSERT_GOTO(peer_at_secondary.vs == sec_vs, "UI must not alter secondary V(S)");
  TEST_ASSERT_GOTO(peer_at_secondary.vr == sec_vr, "UI must not alter secondary V(R)");
  TEST_ASSERT_GOTO(peer_at_secondary.nr == sec_nr, "UI must not alter secondary N(R)");

test_cleanup:
  if (listener_registered)
    ioHdlcAppListenerUnregister(&listener);
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
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
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
  config.mode = IOHDLC_OM_ADM;
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

  ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_ABM);
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

/*===========================================================================*/
/* Test: Orderly Close Preserves Buffered RX                                 */
/*===========================================================================*/

bool test_orderly_close_preserves_buffered_rx(const test_adapter_t *adapter) {
  int test_result = 0;
  const char *test_msg = "Buffered RX survives local DISC";
  size_t msg_len = strlen(test_msg);
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  iohdlc_app_listener_t listener;
  ioHdlcStreamPort port_primary = adapter->get_port_a();
  ioHdlcStreamPort port_secondary = adapter->get_port_b();
  char recv_buf[64];
  ssize_t sent;
  ssize_t received;
  int32_t result;
  int ret;
  bool rx_queued = false;
  bool listener_registered = false;
  eventflags_t flags;
  int i;

  memset(&station_primary, 0, sizeof station_primary);
  memset(&station_secondary, 0, sizeof station_secondary);
  ioHdlcSwDriverInit(&driver_primary, NULL);
  ioHdlcSwDriverInit(&driver_secondary, NULL);

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.frame_arena = shared_arena_primary;
  config.frame_arena_size = sizeof shared_arena_primary;
  config.fff_type = 1;
  config.phydriver = &port_primary;

  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT_GOTO(result == 0, "Primary station init failed");

  memset(&config, 0, sizeof config);
  config.mode = IOHDLC_OM_ADM;
  config.flags = 0;
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.frame_arena = shared_arena_secondary;
  config.frame_arena_size = sizeof shared_arena_secondary;
  config.fff_type = 1;
  config.phydriver = &port_secondary;

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

  ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_ABM);
  TEST_ASSERT_GOTO(ret == 0, "LinkUp failed");

  ioHdlc_sleep_ms(100);

  sent = ioHdlcWriteTmo(&peer_at_secondary, test_msg, msg_len, 500);
  TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Secondary write failed");

  for (i = 0; i < 25; ++i) {
    iohdlc_mutex_lock(&peer_at_primary.state_mutex);
    rx_queued = !ioHdlc_frameq_isempty(&peer_at_primary.i_recept_q);
    iohdlc_mutex_unlock(&peer_at_primary.state_mutex);
    if (rx_queued)
      break;
    ioHdlc_sleep_ms(20);
  }
  TEST_ASSERT_GOTO(rx_queued,
                   "Primary RX queue should contain unread data before DISC");

  result = ioHdlcAppListenerRegister(&station_primary, &listener,
                                     EVENT_MASK(0), IOHDLC_APP_LINK_DOWN |
                                                    IOHDLC_APP_LINK_LOST);
  TEST_ASSERT_GOTO(result == 0, "Link event listener registration failed");
  listener_registered = true;

  ret = ioHdlcStationLinkDown(&station_primary, SECONDARY_ADDR);
  TEST_ASSERT_GOTO(ret == 0, "LinkDown failed");

  flags = ioHdlcAppListenerWait(&listener, 100U);
  TEST_ASSERT_GOTO((flags & IOHDLC_APP_LINK_DOWN) != 0U,
                   "Orderly close should publish LINK_DOWN");
  TEST_ASSERT_GOTO((flags & IOHDLC_APP_LINK_LOST) == 0U,
                   "Orderly close should not publish LINK_LOST");

  TEST_ASSERT_GOTO(IOHDLC_PEER_ORDERLY_CLOSED(&peer_at_primary),
                   "Primary peer should be marked orderly closed");
  TEST_ASSERT_GOTO(IOHDLC_PEER_ORDERLY_CLOSED(&peer_at_secondary),
                   "Secondary peer should be marked orderly closed");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_ABORTED(&peer_at_primary),
                   "Primary peer should not be marked aborted");
  TEST_ASSERT_GOTO(ioHdlcPeerGetState(&peer_at_primary) ==
                   IOHDLC_PEER_STATE_ORDERLY_CLOSED,
                   "Primary peer should report orderly-closed state");
  TEST_ASSERT_GOTO(peer_at_primary.stream_terminal_pending ==
                   IOHDLC_SS_TERM_ORDERLY,
                   "Orderly close should publish a pending RX terminal");

  memset(recv_buf, 0, sizeof recv_buf);
  received = ioHdlcReadTmo(&peer_at_primary, recv_buf, sizeof recv_buf, 100);
  TEST_ASSERT_GOTO(received == (ssize_t)msg_len,
                   "Primary should still read buffered data after orderly close");
  TEST_ASSERT_GOTO(memcmp(recv_buf, test_msg, msg_len) == 0,
                   "Buffered data should survive orderly close");
  TEST_ASSERT_GOTO(peer_at_primary.stream_terminal_pending ==
                   IOHDLC_SS_TERM_ORDERLY,
                   "Buffered RX should not consume the pending terminal");
  received = ioHdlcReadTmo(&peer_at_primary, recv_buf, sizeof recv_buf, 0);
  TEST_ASSERT_GOTO(received == 0,
                   "Primary should observe EOF once orderly-close RX is drained");
  TEST_ASSERT_GOTO(peer_at_primary.stream_terminal_pending == 0U,
                   "Orderly EOF should consume the pending RX terminal");

test_cleanup:
  if (listener_registered)
    ioHdlcAppListenerUnregister(&listener);
  ioHdlc_sleep_ms(100);
  ioHdlcStationDeinit(&station_primary);
  ioHdlcStationDeinit(&station_secondary);

  return test_result;
}

bool test_remote_disc_preserves_buffered_rx(const test_adapter_t *adapter) {
  int test_result = 0;
  const char *test_msg = "Buffered RX survives remote DISC";
  size_t msg_len = strlen(test_msg);
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  iohdlc_app_listener_t listener;
  ioHdlcStreamPort port_primary = adapter->get_port_a();
  ioHdlcStreamPort port_secondary = adapter->get_port_b();
  eventflags_t flags;
  char recv_buf[64];
  ssize_t sent;
  ssize_t received;
  int32_t result;
  int ret;
  bool rx_queued = false;
  bool listener_registered = false;
  int i;

  memset(&station_primary, 0, sizeof station_primary);
  memset(&station_secondary, 0, sizeof station_secondary);
  ioHdlcSwDriverInit(&driver_primary, NULL);
  ioHdlcSwDriverInit(&driver_secondary, NULL);

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

  ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_ABM);
  TEST_ASSERT_GOTO(ret == 0, "LinkUp failed");

  ioHdlc_sleep_ms(100);

  sent = ioHdlcWriteTmo(&peer_at_secondary, test_msg, msg_len, 500);
  TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Secondary write failed");

  for (i = 0; i < 25; ++i) {
    iohdlc_mutex_lock(&peer_at_primary.state_mutex);
    rx_queued = !ioHdlc_frameq_isempty(&peer_at_primary.i_recept_q);
    iohdlc_mutex_unlock(&peer_at_primary.state_mutex);
    if (rx_queued)
      break;
    ioHdlc_sleep_ms(20);
  }
  TEST_ASSERT_GOTO(rx_queued,
                   "Primary RX queue should contain unread data before remote DISC");

  result = ioHdlcAppListenerRegister(&station_primary, &listener,
                                     EVENT_MASK(1), IOHDLC_APP_LINK_DOWN |
                                                    IOHDLC_APP_LINK_LOST);
  TEST_ASSERT_GOTO(result == 0, "Remote link event listener registration failed");
  listener_registered = true;

  ret = ioHdlcStationLinkDown(&station_secondary, PRIMARY_ADDR);
  TEST_ASSERT_GOTO(ret == 0, "Secondary LinkDown failed");

  flags = ioHdlcAppListenerWait(&listener, 100U);
  TEST_ASSERT_GOTO((flags & IOHDLC_APP_LINK_DOWN) != 0U,
                   "Remote DISC receiver should publish LINK_DOWN");
  TEST_ASSERT_GOTO((flags & IOHDLC_APP_LINK_LOST) == 0U,
                   "Remote DISC receiver should not publish LINK_LOST");

  TEST_ASSERT_GOTO(IOHDLC_PEER_ORDERLY_CLOSED(&peer_at_primary),
                   "Primary peer should be marked orderly closed after remote DISC");
  TEST_ASSERT_GOTO(IOHDLC_PEER_ORDERLY_CLOSED(&peer_at_secondary),
                   "Secondary peer should be marked orderly closed after remote DISC");
  TEST_ASSERT_GOTO(!IOHDLC_PEER_ABORTED(&peer_at_primary),
                   "Primary peer should not be marked aborted after remote DISC");

  memset(recv_buf, 0, sizeof recv_buf);
  received = ioHdlcReadTmo(&peer_at_primary, recv_buf, sizeof recv_buf, 100);
  TEST_ASSERT_GOTO(received == (ssize_t)msg_len,
                   "Primary should still read buffered data after remote DISC");
  TEST_ASSERT_GOTO(memcmp(recv_buf, test_msg, msg_len) == 0,
                   "Buffered data should survive remote DISC");
  received = ioHdlcReadTmo(&peer_at_primary, recv_buf, sizeof recv_buf, 0);
  TEST_ASSERT_GOTO(received == 0,
                   "Primary should observe EOF once remote DISC RX is drained");

test_cleanup:
  if (listener_registered)
    ioHdlcAppListenerUnregister(&listener);
  ioHdlc_sleep_ms(100);
  ioHdlcStationDeinit(&station_primary);
  ioHdlcStationDeinit(&station_secondary);

  return test_result;
}

bool test_link_timeout_marks_peer_aborted(const test_adapter_t *adapter) {
  ioHdlcSwDriver driver_primary;
  iohdlc_station_t station_primary;
  iohdlc_station_peer_t peer_at_primary;
  iohdlc_station_config_t config;
  iohdlc_app_listener_t listener;
  ioHdlcStreamPort port_primary = adapter->get_port_a();
  int32_t result;
  int ret;
  eventflags_t flags;
  bool listener_registered = false;

  memset(&station_primary, 0, sizeof station_primary);
  ioHdlcSwDriverInit(&driver_primary, NULL);

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
  config.reply_timeout_ms = 20U;
  config.poll_retry_max = 1U;

  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");

  result = ioHdlcAddPeer(&station_primary, &peer_at_primary, SECONDARY_ADDR);
  TEST_ASSERT(result == 0, "Add peer to primary failed");
  TEST_ASSERT(ioHdlcPeerGetState(&peer_at_primary) ==
              IOHDLC_PEER_STATE_DISCONNECTED,
              "Peer should initially report disconnected state");
  TEST_ASSERT(!IOHDLC_PEER_ORDERLY_CLOSED(&peer_at_primary),
              "Peer should not start in orderly-closed state");
  TEST_ASSERT(!IOHDLC_PEER_ABORTED(&peer_at_primary),
              "Peer should not start in aborted state");

  result = ioHdlcRunnerStart(&station_primary);
  TEST_ASSERT(result == 0, "Failed to start primary runner");

  result = ioHdlcAppListenerRegister(&station_primary, &listener,
                                     EVENT_MASK(0), IOHDLC_APP_LINK_LOST);
  TEST_ASSERT(result == 0, "Link event listener registration failed");
  listener_registered = true;

  ioHdlc_sleep_ms(20);

  ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM);
  TEST_ASSERT(ret == -1, "LinkUp without responder should fail");
  TEST_ASSERT(iohdlc_errno == ETIMEDOUT, "LinkUp failure should surface as timeout");
  TEST_ASSERT(IOHDLC_PEER_DISC(&peer_at_primary),
              "Peer should be disconnected after failed LinkUp");
  TEST_ASSERT(IOHDLC_PEER_ABORTED(&peer_at_primary),
              "Peer should be marked aborted after link timeout");
  TEST_ASSERT(!IOHDLC_PEER_ORDERLY_CLOSED(&peer_at_primary),
              "Peer should not be marked orderly closed after timeout");
  TEST_ASSERT(ioHdlcPeerGetState(&peer_at_primary) ==
              IOHDLC_PEER_STATE_ABORTED,
              "Timed-out link attempt should report aborted state");
  flags = ioHdlcAppListenerWait(&listener, 0U);
  TEST_ASSERT((flags & IOHDLC_APP_LINK_LOST) == 0U,
              "Never-connected peer should not publish LINK_LOST");
#if defined(IOHDLC_ENABLE_STATISTICS)
  TEST_ASSERT(peer_at_primary.stats.timeouts == 2U,
              "poll_retry_max=1 should allow one retry plus final timeout");
#endif
  {
    char dummy = 0;
    ssize_t received = ioHdlcReadTmo(&peer_at_primary, &dummy, 1U, 0U);
    TEST_ASSERT(received == -1, "Read on aborted peer should fail");
    TEST_ASSERT(iohdlc_errno == ECONNRESET,
                "Read on aborted peer should report ECONNRESET");
  }

  ioHdlc_sleep_ms(50);
  if (listener_registered)
    ioHdlcAppListenerUnregister(&listener);
  ioHdlcStationDeinit(&station_primary);

  return 0;
}

bool test_connected_link_timeout_emits_lost(const test_adapter_t *adapter) {
  int test_result = 0;
  test_link_pair_t pair;
  iohdlc_app_listener_t listener;
  bool listener_registered = false;
  bool secondary_stopped = false;
  eventflags_t flags;
  int32_t result;

  result = init_test_link_pair(adapter, &pair,
                               IOHDLC_OM_NDM, IOHDLC_FLG_PRI,
                               IOHDLC_OM_NDM, 0);
  TEST_ASSERT(result == 0, "Link-loss pair init failed");

  result = ioHdlcRunnerStart(&pair.station_primary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start primary runner");
  result = ioHdlcRunnerStart(&pair.station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Failed to start secondary runner");
  ioHdlc_sleep_ms(50);

  result = ioHdlcStationLinkUp(&pair.station_primary, SECONDARY_ADDR,
                               IOHDLC_OM_NRM);
  TEST_ASSERT_GOTO(result == 0, "LinkUp should succeed before link loss");
  TEST_ASSERT_GOTO(ioHdlcPeerGetState(&pair.peer_at_primary) ==
                   IOHDLC_PEER_STATE_CONNECTED,
                   "Peer should report connected state before link loss");

  iohdlc_mutex_lock(&pair.peer_at_primary.state_mutex);
  pair.station_primary.reply_timeout_ms = 20U;
  pair.peer_at_primary.poll_retry_max = 1U;
  iohdlc_mutex_unlock(&pair.peer_at_primary.state_mutex);

  result = ioHdlcAppListenerRegister(&pair.station_primary, &listener,
                                     EVENT_MASK(0), IOHDLC_APP_LINK_LOST);
  TEST_ASSERT_GOTO(result == 0, "Link-loss listener registration failed");
  listener_registered = true;

  result = ioHdlcStationDeinit(&pair.station_secondary);
  TEST_ASSERT_GOTO(result == 0, "Secondary deinit should succeed");
  secondary_stopped = true;

  flags = ioHdlcAppListenerWait(&listener, 1000U);
  TEST_ASSERT_GOTO((flags & IOHDLC_APP_LINK_LOST) != 0U,
                   "Connected peer timeout should publish LINK_LOST");
  TEST_ASSERT_GOTO(ioHdlcPeerGetState(&pair.peer_at_primary) ==
                   IOHDLC_PEER_STATE_ABORTED,
                   "Lost peer should report aborted state");
  TEST_ASSERT_GOTO(pair.peer_at_primary.stream_terminal_pending ==
                   IOHDLC_SS_TERM_ABORTED,
                   "Link loss should publish a pending RX abort");

  {
    char dummy = 0;
    ssize_t received = ioHdlcReadTmo(&pair.peer_at_primary, &dummy, 1U, 0U);

    TEST_ASSERT_GOTO(received == -1,
                     "Read should report the pending link loss");
    TEST_ASSERT_GOTO(iohdlc_errno == ECONNRESET,
                     "Pending link loss should report ECONNRESET");
    TEST_ASSERT_GOTO(pair.peer_at_primary.stream_terminal_pending == 0U,
                     "Read should consume the pending link loss");
  }

test_cleanup:
  if (listener_registered)
    ioHdlcAppListenerUnregister(&listener);
  ioHdlcStationDeinit(&pair.station_primary);
  if (!secondary_stopped)
    ioHdlcStationDeinit(&pair.station_secondary);
  return test_result;
}
