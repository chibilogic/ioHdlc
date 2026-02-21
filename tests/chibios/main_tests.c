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
#include "ch.h"
#include "hal.h"
#include "ioHdlcosal.h"
#include "chprintf.h"
#include "test_helpers.h"
#include "adapter_interface.h"
#include "board_config.h"
/* Select adapter based on build configuration */
#ifdef USE_UART_ADAPTER
  extern const test_adapter_t uart_adapter;
  #define TEST_ADAPTER (&uart_adapter)
#else
  extern const test_adapter_t mock_adapter;
  #define TEST_ADAPTER (&mock_adapter)
#endif
/* Test function prototypes from common scenarios */
extern int test_pool_init(void);
extern int test_take_release(void);
extern int test_addref(void);
extern int test_watermark(void);
extern int test_exhaust_pool(void);
/* Basic connection tests */
extern int test_station_creation(void);
extern int test_peer_creation(void);
extern bool test_snrm_handshake(const test_adapter_t *adapter);
extern int test_connection_timeout(void);
/* TWA mode basic tests */
extern int test_data_exchange_twa(const test_adapter_t *adapter);
/* Checkpoint retransmission tests - TWS */
extern bool test_A1_1_frame_loss_window_full(const test_adapter_t *adapter);
extern bool test_A2_1_multiple_frame_loss(const test_adapter_t *adapter);
extern bool test_A2_2_first_and_last_frame_loss(const test_adapter_t *adapter);

/* Checkpoint retransmission tests - TWA mode */
extern bool test_A1_1_frame_loss_window_full_twa(const test_adapter_t *adapter);
extern bool test_A2_1_multiple_frame_loss_twa(const test_adapter_t *adapter);
extern bool test_A2_2_first_and_last_frame_loss_twa(const test_adapter_t *adapter);

/*
 * Serial configuration for test output console.
 */
static const SerialConfig sdcfg = {
  .speed = 115200
};

/*
 * Green LED blinker thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThread1, 128);
static THD_FUNCTION(Thread1, arg) {

  (void)arg;
  chRegSetThreadName("blinker");
  while (true) {
    palSetPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleepMilliseconds(80);
    palClearPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleepMilliseconds(120);
    palSetPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleepMilliseconds(120);
    palClearPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleepMilliseconds(120);
    palSetPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleepMilliseconds(160);
    palClearPad(GPIOA, GPIOA_LED_GREEN);
    chThdSleepMilliseconds(600);
  }
}

/*
 * Test runner thread.
 */
static THD_WORKING_AREA(waTestRunner, 4096);
static THD_FUNCTION(TestRunner, arg) {
  (void)arg;
  chRegSetThreadName("test_runner");
  /* Wait for serial to be ready */
  chThdSleepMilliseconds(100);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "════════════════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  ioHdlc Test Suite - ChibiOS/ARM\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "════════════════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Adapter: %s\r\n", TEST_ADAPTER->name);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "════════════════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  /* Initialize test adapter (mock or UART) */
  TEST_ADAPTER->init();
  /* Run Frame Pool Tests */
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Frame Pool Tests\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  RUN_TEST(test_pool_init);
  RUN_TEST(test_take_release);
  RUN_TEST(test_addref);
  RUN_TEST(test_watermark);
  RUN_TEST(test_exhaust_pool);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  /* Basic Connection Tests */
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Basic Connection Tests\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  RUN_TEST(test_station_creation);
  RUN_TEST(test_peer_creation);
  RUN_TEST_ADAPTER(test_snrm_handshake, TEST_ADAPTER);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");

  /* Basic Connection Tests - TWA Mode */
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Basic Connection Tests (TWA)\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");

  RUN_TEST_ADAPTER(test_data_exchange_twa, TEST_ADAPTER);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");

  /* Checkpoint Retransmission Tests */
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Checkpoint Retransmission Tests (TWS)\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  RUN_TEST_ADAPTER(test_A1_1_frame_loss_window_full, TEST_ADAPTER);
  RUN_TEST_ADAPTER(test_A2_1_multiple_frame_loss, TEST_ADAPTER);
  RUN_TEST_ADAPTER(test_A2_2_first_and_last_frame_loss, TEST_ADAPTER);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");

  /* Checkpoint Retransmission Tests - TWA */
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Checkpoint Retransmission Tests (TWA)\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");

  RUN_TEST_ADAPTER(test_A1_1_frame_loss_window_full_twa, TEST_ADAPTER);
  RUN_TEST_ADAPTER(test_A2_1_multiple_frame_loss_twa, TEST_ADAPTER);
  RUN_TEST_ADAPTER(test_A2_2_first_and_last_frame_loss_twa, TEST_ADAPTER);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");

  /* Final summary */
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Final Summary\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Total Passed: %d\r\n", passed_count);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  Total Failed: %d\r\n", failed_count);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "═══════════════════════════════════════════════\r\n");
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, "\r\n");
  if (failed_count == 0) {
    chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
             "✅ All Tests Completed Successfully\r\n\r\n");
  } else {
    chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
             "❌ Some Tests Failed\r\n\r\n");
  }
  /* Deinitialize test adapter */
  TEST_ADAPTER->deinit();
  /* Tests completed - loop forever */
  while (true) {
    chThdSleepMilliseconds(1000);
  }
}

/*
 * Application entry point.
 */
int main(void) {
  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();
  /*
   * Activates serial driver 0 using the driver default configuration.
   */
  sdStart(&TEST_OUTPUT_SD, &sdcfg);
  ioHdlcSDx = (BaseSequentialStream *)&TEST_OUTPUT_SD;

  /*
   * Creates the blinker thread.
   */
  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO-1, Thread1, NULL);

  /*
   * Creates the test runner thread.
   */
  chThdCreateStatic(waTestRunner, sizeof(waTestRunner), 
                    NORMALPRIO, TestRunner, NULL);
  /*
   * Normal main() thread activity - idle loop.
   */
  while (true) {
    chThdSleepMilliseconds(500);
  }
}
