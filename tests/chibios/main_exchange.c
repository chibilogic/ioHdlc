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
 * @file    main_exchange.c
 * @brief   ChibiOS entry point for exchange test.
 * @details Configure test parameters via Makefile defines (see test_config_chibios.c).
 *          Produces iohdlc_exchange.elf binary (separate from iohdlc_tests.elf).
 */

#include "ch.h"
#include "hal.h"
#include "ioHdlcosal.h"
#include "chprintf.h"
#include "board_config.h"
#include "adapter_interface.h"

/* Select adapter based on build configuration */
#ifdef USE_UART_ADAPTER
  extern const test_adapter_t uart_adapter;
  #define TEST_ADAPTER (&uart_adapter)
#else
  extern const test_adapter_t mock_adapter;
  #define TEST_ADAPTER (&mock_adapter)
#endif

/* Entry point from test_exchange.c */
extern int test_exchange_main(const test_adapter_t *adapter, int argc, char **argv);

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
 * Exchange test runner thread.
 */
static THD_WORKING_AREA(waExchangeTest, 8192);  /* Large stack for exchange test */
static THD_FUNCTION(ExchangeTestRunner, arg) {
  (void)arg;
  chRegSetThreadName("exchange_test");
  
  /* Wait for serial to be ready */
  chThdSleepMilliseconds(100);
  
  /* Run the exchange test with configured adapter (UART or mock) */
  test_exchange_main(TEST_ADAPTER, 0, NULL);
  
  /* Test completed - loop forever */
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
   */
  halInit();
  chSysInit();
  
  /*
   * Activates serial driver using the driver default configuration.
   */
  sdStart(&TEST_OUTPUT_SD, &sdcfg);
  ioHdlcSDx = (BaseSequentialStream *)&TEST_OUTPUT_SD;

  /*
   * Creates the blinker thread.
   */
  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO-1, Thread1, NULL);

  /*
   * Creates the exchange test thread.
   */
  chThdCreateStatic(waExchangeTest, sizeof(waExchangeTest), 
                    NORMALPRIO, ExchangeTestRunner, NULL);
  
  /*
   * Main thread - idle loop.
   */
  while (true) {
    chThdSleepMilliseconds(500);
  }
}
