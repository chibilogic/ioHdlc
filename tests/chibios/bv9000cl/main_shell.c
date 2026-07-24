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
 * @brief   ChibiOS interactive shell for ioHdlc tests on BV9000CL.
 */

#include "ch.h"
#include "hal.h"
#include "ioHdlcosal.h"
#include "chprintf.h"
#include "shell.h"
#include "board_config.h"
#include "adapter_interface.h"

extern int test_exchange_main(const test_adapter_t *adapter, int argc,
                              char **argv);
extern const test_adapter_t uart_adapter;

static void cmd_exchange(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)chp;

  test_exchange_main(&uart_adapter, argc, argv);
}

static const ShellCommand commands[] = {
  {"exchange", cmd_exchange},
  {NULL, NULL}
};

static char histbuf[SHELL_MAX_HIST_BUFF];

static const ShellConfig shell_cfg = {
  (BaseSequentialStream *)&TEST_OUTPUT_SD,
  commands,
#if SHELL_USE_HISTORY == TRUE
  histbuf,
  sizeof histbuf
#endif
};

static THD_WORKING_AREA(wa_blinker, 512);
static THD_FUNCTION(blinker_thread, arg) {
  (void)arg;

  chRegSetThreadName("blinker");

  while (true) {
    palSetLine(LINE_LED_RGB_R);
    palSetLine(LINE_LED_MAINTENANCE);
    chThdSleepMilliseconds(80);
    palClearLine(LINE_LED_RGB_R);
    palClearLine(LINE_LED_MAINTENANCE);
    chThdSleepMilliseconds(120);
    palSetLine(LINE_LED_RGB_R);
    palSetLine(LINE_LED_MAINTENANCE);
    chThdSleepMilliseconds(120);
    palClearLine(LINE_LED_RGB_R);
    palClearLine(LINE_LED_MAINTENANCE);
    chThdSleepMilliseconds(120);
    palSetLine(LINE_LED_RGB_R);
    palSetLine(LINE_LED_MAINTENANCE);
    chThdSleepMilliseconds(160);
    palClearLine(LINE_LED_RGB_R);
    palClearLine(LINE_LED_MAINTENANCE);
    chThdSleepMilliseconds(600);
    chSysLock();
    wdgResetI(&WDGD0);
    chSysUnlock();
  }
}

int main(void) {
  static const SerialConfig serialcfg = {
    115200U,
    0U,
    UART_MR_PAR_NO,
  };

  halInit();
  chSysInit();

  sdStart(&TEST_OUTPUT_SD, &serialcfg);
  ioHdlcSDx = (BaseSequentialStream *)&TEST_OUTPUT_SD;

  shellInit();
  chThdCreateStatic(wa_blinker, sizeof wa_blinker, NORMALPRIO - 1,
                    blinker_thread, NULL);

  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD, SHELL_NEWLINE_STR);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD,
           "ioHdlc Exchange Test Shell" SHELL_NEWLINE_STR);
  chprintf((BaseSequentialStream *)&TEST_OUTPUT_SD,
           "Use exchange --endpoint=a or exchange --endpoint=b"
           SHELL_NEWLINE_STR);

  shellThread((void *)&shell_cfg);

  return 0;
}
