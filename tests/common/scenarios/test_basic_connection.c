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

#include "../../common/test_helpers.h"
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

/**
 * @brief   Initialize a test station with minimal configuration.
 * @details Helper to reduce boilerplate in tests that need a station.
 *          Creates a basic primary NRM station with modulo 8.
 */
static int32_t init_test_station(iohdlc_station_t *station,
                                 ioHdlcFramePool *frame_pool,
                                 ioHdlcDriver *driver,
                                 uint32_t addr) {
  iohdlc_station_config_t config;

  /* Initialize frame pool (minimal mock) */
  memset(frame_pool, 0, sizeof *frame_pool);
  frame_pool->framesize = FRAME_SIZE;
  
  /* Initialize mock driver */
  memset(driver, 0, sizeof *driver);

  /* Configure station */
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;  /* Primary station */
  config.log2mod = 3;  /* Modulo 8 */
  config.addr = addr;
  config.driver = driver;
  config.fpp = frame_pool;
  config.optfuncs = NULL;  /* Use defaults: REJ, SST, INH, FFF enabled */
  config.phydriver = NULL;
  config.phydriver_config = NULL;

  /* Initialize station */
  memset(station, 0, sizeof *station);
  return ioHdlcStationInit(station, &config);
}

/*===========================================================================*/
/* Test: Station Creation                                                    */
/*===========================================================================*/

bool test_station_creation(void) {
  iohdlc_station_t station;
  ioHdlcFramePool frame_pool;
  ioHdlcDriver mock_driver;
  int32_t result;

  /* Initialize station using helper */
  result = init_test_station(&station, &frame_pool, &mock_driver, PRIMARY_ADDR);

  /* Validate initialization */
  TEST_ASSERT(result == 0, "Station init should succeed");
  TEST_ASSERT(station.addr == PRIMARY_ADDR, "Station address should match");
  TEST_ASSERT(station.mode == IOHDLC_OM_NRM, "Station mode should be NRM");
  TEST_ASSERT(station.flags == IOHDLC_FLG_PRI, "Station should be primary");
  TEST_ASSERT(station.modmask == 7, "Modulo 8 should have modmask 7");
  TEST_ASSERT(station.ctrl_size == 1, "Modulo 8 should have ctrl_size 1");
  TEST_ASSERT(station.frame_pool == &frame_pool, "Frame pool should be set");
  TEST_ASSERT(station.driver == &mock_driver, "Driver should be set");
  TEST_ASSERT(station.errorno == 0, "No error should be set");

  test_printf("✅ Station creation and initialization successful\n");
  return 0;
}

/*===========================================================================*/
/* Test: Peer Creation                                                       */
/*===========================================================================*/

bool test_peer_creation(void) {
  iohdlc_station_t station;
  ioHdlcFramePool frame_pool;
  ioHdlcDriver mock_driver;
  iohdlc_station_peer_t peer;
  int32_t result;

  /* Initialize station */
  result = init_test_station(&station, &frame_pool, &mock_driver, PRIMARY_ADDR);
  TEST_ASSERT(result == 0, "Station init should succeed");

  /* Add peer to station */
  result = ioHdlcAddPeer(&station, &peer, SECONDARY_ADDR);

  /* Validate peer initialization */
  TEST_ASSERT(result == 0, "Peer add should succeed");
  TEST_ASSERT(peer.addr == SECONDARY_ADDR, "Peer address should match");
  TEST_ASSERT(peer.stationp == &station, "Peer should reference station");
  TEST_ASSERT(peer.ks == 7, "Peer ks should match modmask (7)");
  TEST_ASSERT(peer.kr == 7, "Peer kr should match modmask (7)");
  
  /* Validate mifl calculation: FRAME_SIZE - (FFF + ADDR + CTRL + FCS)
     For modulo 8 with FFF: 128 - (1 + 1 + 1 + fcs_size) */
  uint32_t expected_mifl = FRAME_SIZE - (station.frame_offset + 1 + station.ctrl_size + station.fcs_size);
  TEST_ASSERT(peer.mifls == expected_mifl, "Peer mifls should be calculated correctly");
  TEST_ASSERT(peer.miflr == expected_mifl, "Peer miflr should be calculated correctly");
  
  /* Validate queues are initialized (empty) */
  TEST_ASSERT(ioHdlc_frameq_isempty(&peer.i_recept_q), "Peer i_recept_q should be empty");
  TEST_ASSERT(ioHdlc_frameq_isempty(&peer.i_retrans_q), "Peer i_retrans_q should be empty");
  TEST_ASSERT(ioHdlc_frameq_isempty(&peer.i_trans_q), "Peer i_trans_q should be empty");
  
  /* Validate peer is in station's peer list */
  iohdlc_station_peer_t *found_peer = addr2peer(&station, SECONDARY_ADDR);
  TEST_ASSERT(found_peer == &peer, "Peer should be findable in station's peer list");
  
  /* Test duplicate address rejection */
  iohdlc_station_peer_t duplicate_peer;
  result = ioHdlcAddPeer(&station, &duplicate_peer, SECONDARY_ADDR);
  TEST_ASSERT(result == -1, "Adding duplicate peer should fail");
  TEST_ASSERT(station.errorno == EEXIST, "Error should be EEXIST");

  test_printf("✅ Peer creation and initialization successful\n");
  return 0;
}

/*===========================================================================*/
/* Test: SNRM Handshake - Two Connected Stations                            */
/*===========================================================================*/

bool test_snrm_handshake(void) {
  /* Two stations connected via mock streams */
  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  ioHdlcFrameMemPool pool_primary, pool_secondary;
  iohdlc_station_peer_t peer_at_primary, peer_at_secondary;
  iohdlc_station_config_t config;
  int32_t result;

  /* Create mock streams */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = false,
    .error_rate = 0,
    .delay_us = 0
  };
  
  stream_primary = mock_stream_create(&stream_config);
  stream_secondary = mock_stream_create(&stream_config);
  TEST_ASSERT(stream_primary != NULL && stream_secondary != NULL, "Stream creation failed");
  
  /* Connect streams (bidirectional) */
  mock_stream_connect(stream_primary, stream_secondary);
  
  /* Create adapters */
  adapter_primary = mock_stream_adapter_create(stream_primary);
  adapter_secondary = mock_stream_adapter_create(stream_secondary);
  TEST_ASSERT(adapter_primary != NULL && adapter_secondary != NULL, "Adapter creation failed");
  
  /* Get stream ports */
  ioHdlcStreamPort port_primary = mock_stream_adapter_get_port(adapter_primary);
  ioHdlcStreamPort port_secondary = mock_stream_adapter_get_port(adapter_secondary);
  
  /* Initialize stream drivers */
  ioHdlcSwDriverInit(&driver_primary);
  ioHdlcSwDriverInit(&driver_secondary);
  
  /* Initialize frame pools with arena */
  static uint8_t arena_primary[8192];
  static uint8_t arena_secondary[8192];
  fmpInit(&pool_primary, arena_primary, sizeof arena_primary, FRAME_SIZE, 8);
  fmpInit(&pool_secondary, arena_secondary, sizeof arena_secondary, FRAME_SIZE, 8);
  
  /* Configure primary station */
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.fpp = (ioHdlcFramePool *)&pool_primary;
  config.optfuncs = NULL;
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
  config.fpp = (ioHdlcFramePool *)&pool_secondary;
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
  
  /* Verify initial disconnected state */
  TEST_ASSERT(IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be disconnected initially");
  TEST_ASSERT(IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be disconnected initially");
  
  /* Start runner threads for both stations */
  test_printf("Starting runner threads...\n");
  ioHdlcRunnerStart(&station_primary);
  ioHdlcRunnerStart(&station_secondary);
  
  /* Allow time for threads to initialize and register listeners */
#ifdef IOHDLC_USE_CHIBIOS
  chThdSleepMilliseconds(50);
#else
  usleep(50000);  /* 50 ms */
#endif
  
  /* Initiate connection from primary to secondary */
  test_printf("Calling ioHdlcStationLinkUp...\n");
  int ret = ioHdlcStationLinkUp(&station_primary, SECONDARY_ADDR, IOHDLC_OM_NRM); // DEBUG: Set breakpoint here
  if (ret != 0) {
    test_printf("❌ ioHdlcStationLinkUp returned %d, station errno=%d\n", ret, station_primary.errorno);
  }
  TEST_ASSERT(ret == 0, "ioHdlcStationLinkUp should succeed"); // DEBUG: Check station_primary.errorno here
  
  /* Allow time for protocol exchange (SNRM → UA) */
#ifdef IOHDLC_USE_CHIBIOS
  chThdSleepMilliseconds(100);
#else
  usleep(100000);  /* 100 ms */
#endif
  
  /* Verify connection established at both ends */
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_primary), "Primary peer should be connected");
  TEST_ASSERT(!IOHDLC_PEER_DISC(&peer_at_secondary), "Secondary peer should be connected");
  TEST_ASSERT(station_primary.errorno == 0, "Primary station should have no errors");
  TEST_ASSERT(station_secondary.errorno == 0, "Secondary station should have no errors");
  
  test_printf("✅ SNRM handshake completed successfully\n");
  
  /* Stop runners */
  ioHdlcRunnerStop(&station_primary);
  ioHdlcRunnerStop(&station_secondary);
  
  /* Cleanup */
  mock_stream_adapter_destroy(adapter_primary);
  mock_stream_adapter_destroy(adapter_secondary);
  mock_stream_destroy(stream_primary);
  mock_stream_destroy(stream_secondary);
  
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
static int test_data_exchange(void) {
  int test_result = 0;  /* Success by default, set to 1 on failure */
  
  /* Test message */
  const char *test_msg = "Hello ioHdlc, welcome in the HDLC world.";
  size_t msg_len = strlen(test_msg);
  
  /* Setup: same as test_snrm_handshake */
  mock_stream_t *stream_primary, *stream_secondary;
  mock_stream_adapter_t *adapter_primary, *adapter_secondary;
  ioHdlcSwDriver driver_primary, driver_secondary;
  iohdlc_station_t station_primary, station_secondary;
  ioHdlcFrameMemPool pool_primary, pool_secondary;
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
  
  /* Initialize frame pools */
  static uint8_t arena_primary[8192];
  static uint8_t arena_secondary[8192];
  fmpInit(&pool_primary, arena_primary, sizeof arena_primary, FRAME_SIZE, 8);
  fmpInit(&pool_secondary, arena_secondary, sizeof arena_secondary, FRAME_SIZE, 8);
  
  /* Configure primary station */
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;  /* | IOHDLC_FLG_TWA; */
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.fpp = (ioHdlcFramePool *)&pool_primary;
  config.optfuncs = NULL;
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;
  
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
  config.fpp = (ioHdlcFramePool *)&pool_secondary;
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
rep:
  test_printf("Primary sending %zu bytes...\n", msg_len*10);
  for (i = 0; i < 10; ++i) {
    sent = ioHdlcWriteTmo(&peer_at_primary, test_msg, msg_len, 2000);
    if (sent != (ssize_t)msg_len) {
      test_printf("❌ Primary write returned %zd (expected %zu), errno=%d\n", 
                  sent, msg_len, station_primary.errorno);
    }
    TEST_ASSERT_GOTO(sent == (ssize_t)msg_len, "Primary write failed");
    test_printf("Primary sent %zd bytes\n", sent);
  }
  usleep(500000);

  /* Secondary receives message */
  memset(recv_buf, 0, sizeof recv_buf);
  ssize_t received;
  for (i = 0; i < 10; ++i) {
    received = ioHdlcReadTmo(&peer_at_secondary, recv_buf, msg_len, 2000);
    test_printf("Secondary read returned %zd bytes (expected %zu), errno=%d\n",
                received, msg_len, station_secondary.errorno);
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
  received = ioHdlcReadTmo(&peer_at_primary, echo_buf, sizeof echo_buf - 1, 2000);
  TEST_ASSERT_GOTO(received == (ssize_t)msg_len, "Primary echo read failed");
  TEST_ASSERT_GOTO(memcmp(echo_buf, test_msg, msg_len) == 0, "Echo data mismatch");
  test_printf("Primary received echo %zd bytes: \"%s\"\n", received, echo_buf);
  
  usleep(200000);
  goto rep;
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
  test_printf("  ioHdlc Test Suite - Basic Connection\n");
  test_printf("═══════════════════════════════════════════════\n\n");

  RUN_TEST(test_station_creation);
  RUN_TEST(test_peer_creation);
  /* TODO: Fix thread cleanup issue between tests */
  RUN_TEST(test_snrm_handshake);
  RUN_TEST(test_data_exchange);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
#endif /* IOHDLC_USE_CHIBIOS */
