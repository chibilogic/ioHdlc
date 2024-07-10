/*
    ioHdlc - Copyright (C) 2024 Isidoro Orabona

    GNU General Public License Usage

    Protocol Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Protocol Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with Protocol Library.  If not, see <http://www.gnu.org/licenses/>.

    Commercial License Usage

    Licensees holding valid commercial ioHdlc licenses may use this file in
    accordance with the commercial license agreement provided in accordance with
    the terms contained in a written agreement between you and Isidoro Orabona.
    For further information contact via email on github account.
 */

#include "ch.h"
#include "hal.h"
#include "ioHdlc.h"
#include "ioHdlcuart.h"
#include "chprintf.h"

static BaseSequentialStream *chp = ((BaseSequentialStream *)&FSD1);
static BaseSequentialStream *chp2 = ((BaseSequentialStream *)&SD1);
static BaseSequentialStream *chp3 = ((BaseSequentialStream *)&SD2);

/*
 * LED blinker thread, times are in milliseconds.
 */
static THD_WORKING_AREA(waThread1, 512);
static THD_FUNCTION(Thread1, arg) {

  (void)arg;
  chRegSetThreadName("blinker");

  while (true) {
    palToggleLine(LINE_LED_ROW);
    chThdSleepMilliseconds(80);
    palToggleLine(LINE_LED_ROW);
    chThdSleepMilliseconds(120);
    palToggleLine(LINE_LED_ROW);
    chThdSleepMilliseconds(120);
    palToggleLine(LINE_LED_ROW);
    chThdSleepMilliseconds(120);
    palToggleLine(LINE_LED_ROW);
    chThdSleepMilliseconds(160);
    palToggleLine(LINE_LED_ROW);
    chThdSleepMilliseconds(600);
  }
}

static THD_WORKING_AREA(waThread2, 512);
static THD_FUNCTION(Thread2, arg) {
  uint8_t c;
  (void)arg;

  chRegSetThreadName("echo");
  while (true) {
    c = streamGet(chp);
    streamPut(chp2, c);
  }
}

/*
 * Serial config.
 */
static const SerialConfig sdcfg = {
    115200,
    0,
    US_MR_CHRL_8_BIT | US_MR_PAR_NO
};

static ioHdlcuartConfig uart_cfg = {
  {
      .txend1_cb = NULL,
      .txend2_cb = NULL,
      .rxend_cb = NULL,
      .rxchar_cb = NULL,
      .rxerr_cb = NULL,
      .timeout_cb = NULL,
      .timeout = 20,
      .speed = 115200,
      .cr = 0,                                /* CR register */
      .mr = US_MR_CHRL_8_BIT | US_MR_PAR_NO,  /* MR register */
  },
  0,
};

static ioHdclUartDriver bla;

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
   * Activates the serial driver 1 using the driver default configuration.
   */
  sdStart(&FSD1, &sdcfg);
  sdStart(&SD1, &sdcfg);
  sdStart(&SD2, &sdcfg);
  chprintf(chp2, "Starting ioHDLC test.\r\n");


  /*
   * Creates the blinker thread.
   */
  chThdCreateStatic(waThread1, sizeof waThread1, NORMALPRIO, Thread1, NULL);
  /*
   * Creates the echo thread.
   */
  chThdCreateStatic(waThread2, sizeof waThread2, NORMALPRIO, Thread2, NULL);

  /*
   * Normal main() thread activity, in this demo it does nothing except
   * sleeping in a loop and check the button state.
   */
  while (true) {
    chprintf(chp3, "Testing ioHDLC echo on (AUX, FCOM1) serial port pair.\r\n");
    chThdSleepMilliseconds(500);
  }
}
