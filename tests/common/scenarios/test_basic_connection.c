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
#include "ioHdlcstream_driver.h"
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
  memset(frame_pool, 0, sizeof(*frame_pool));
  frame_pool->framesize = FRAME_SIZE;
  
  /* Initialize mock driver */
  memset(driver, 0, sizeof(*driver));

  /* Configure station */
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;  /* Primary station */
  config.log2mod = 3;  /* Modulo 8 */
  config.addr = addr;
  config.driver = driver;
  config.fpp = frame_pool;
  config.optfuncs = NULL;  /* Use defaults: REJ, SST, STB, FFF enabled */
  config.phydriver = NULL;
  config.phydriver_config = NULL;

  /* Initialize station */
  memset(station, 0, sizeof(*station));
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
     For modulo 8 with FFF: 128 - (1 + 1 + 1 + 2) = 123 */
  uint32_t expected_mifl = FRAME_SIZE - (station.frame_offset + 1 + station.ctrl_size + 2);
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
  ioHdclStreamDriver driver_primary, driver_secondary;
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
  ioHdclStreamDriverInit(&driver_primary);
  ioHdclStreamDriverInit(&driver_secondary);
  
  /* Initialize frame pools with arena */
  static uint8_t arena_primary[8192];
  static uint8_t arena_secondary[8192];
  fmpInit(&pool_primary, arena_primary, sizeof(arena_primary), FRAME_SIZE, 8);
  fmpInit(&pool_secondary, arena_secondary, sizeof(arena_secondary), FRAME_SIZE, 8);
  
  /* Configure primary station */
  config.mode = IOHDLC_OM_NRM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3;
  config.addr = PRIMARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_primary;
  config.fpp = &pool_primary;
  config.optfuncs = NULL;
  config.phydriver = &port_primary;
  config.phydriver_config = NULL;
  
  memset(&station_primary, 0, sizeof(station_primary));
  result = ioHdlcStationInit(&station_primary, &config);
  TEST_ASSERT(result == 0, "Primary station init failed");
  
  /* Configure secondary station */
  memset(&config, 0, sizeof(config));
  config.mode = IOHDLC_OM_NDM;  /* Secondary starts in disconnected mode */
  config.flags = 0;  /* Secondary */
  config.log2mod = 3;
  config.addr = SECONDARY_ADDR;
  config.driver = (ioHdlcDriver *)&driver_secondary;
  config.fpp = &pool_secondary;
  config.optfuncs = NULL;
  config.phydriver = &port_secondary;
  config.phydriver_config = NULL;
  
  memset(&station_secondary, 0, sizeof(station_secondary));
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
  RUN_TEST(test_snrm_handshake);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
#endif /* IOHDLC_USE_CHIBIOS */
