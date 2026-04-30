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
 * @file    main_shell.c
 * @brief   ChibiOS interactive shell for ioHdlc tests.
 * @details Provides shell commands to run tests interactively.
 */

#include "ch.h"
#include "hal.h"
#include "ioHdlcosal.h"
#include "chprintf.h"
#include "shell.h"
#include "shell_cmd.h"
#include "board_config.h"
#include "test_helpers.h"
#include "adapter_interface.h"

/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

/* Select adapter based on build configuration */
#if defined(USE_UART_ADAPTER)
  extern const test_adapter_t uart_adapter;
  #define TEST_ADAPTER (&uart_adapter)
#elif defined(USE_SPI_ADAPTER)
  extern const test_adapter_t spi_adapter;
  #define TEST_ADAPTER (&spi_adapter)
#else
  extern const test_adapter_t mock_adapter;
  #define TEST_ADAPTER (&mock_adapter)
#endif

/*===========================================================================*/
/* External Test Functions                                                   */
/*===========================================================================*/

/* Exchange test with adapter support */
extern int test_exchange_main(const test_adapter_t *adapter, int argc, char **argv);

/*===========================================================================*/
/* Serial Configuration                                                      */
/*===========================================================================*/

static const SerialConfig sdcfg = {
  .speed = 115200
};

/*===========================================================================*/
/* Shell Commands                                                            */
/*===========================================================================*/

/**
 * @brief   Run exchange test with full Linux-compatible parameter support.
 * @details Accepts command line arguments:
 *          --size=N          Packet size in bytes, header included
 *          --count=N         Run for N iterations (default: 100)
 *          --exchanges=N     Exchanges per iteration (default: 10)
 *          -p N              Poll interval in ms (default: 1000)
 *          --error-rate N    Error injection rate 0-100% (default: 0)
 *          --direction DIR   Exchange direction: both|a2b|b2a (default: both)
 *          --reply-timeout N Reply timeout in ms (default: 100)
 */
static void cmd_exchange(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)chp;
  
  /* Pass the configured adapter and arguments to test_exchange_main. */
  test_exchange_main(TEST_ADAPTER, argc, argv);
}

/*===========================================================================*/
/* Shell Command Table                                                       */
/*===========================================================================*/

static const ShellCommand commands[] = {
  {"exchange",   cmd_exchange},
  {NULL, NULL}
};

/*===========================================================================*/
/* Shell Configuration                                                       */
/*===========================================================================*/

#define SHELL_WA_SIZE   THD_WORKING_AREA_SIZE(8192)

static char histbuf[SHELL_MAX_HIST_BUFF];

static const ShellConfig shell_cfg = {
  (BaseSequentialStream *)&TEST_OUTPUT_SD,
  commands,
#if SHELL_USE_HISTORY == TRUE
  histbuf,
  sizeof(histbuf)
#endif
};

/*===========================================================================*/
/* LED Blinker Thread                                                        */
/*===========================================================================*/

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

/*===========================================================================*/
/* Application Entry Point                                                   */
/*===========================================================================*/

int main(void) {
  /*
   * System initializations.
   */
  halInit();
  chSysInit();
  
  /*
   * Activates serial driver.
   */
  sdStart(&TEST_OUTPUT_SD, &sdcfg);
  ioHdlcSDx = (BaseSequentialStream *)&TEST_OUTPUT_SD;

  /*
   * Shell initialization.
   */
  shellInit();

  /*
   * Creates the LED blinker thread.
   */
  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO-1, Thread1, NULL);

  /*
   * Shell thread (on main thread).
   */
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, SHELL_NEWLINE_STR);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "════════════════════════════════════════════════════════" SHELL_NEWLINE_STR);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "  ioHdlc Exchange Test Shell v0.3" SHELL_NEWLINE_STR);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "════════════════════════════════════════════════════════" SHELL_NEWLINE_STR);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, 
           "Type 'help' for commands, 'exchange --help' for test options" SHELL_NEWLINE_STR);
  
  /* Run shell in main thread */
  shellThread((void *)&shell_cfg);
  
  /* Should never reach here */
  return 0;
}
