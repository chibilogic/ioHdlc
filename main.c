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
#include "ioHdlcfmempool.h"
#include "ioHdlcuart.h"
#include "chprintf.h"

static BaseSequentialStream *chp2 = ((BaseSequentialStream *)&SD1);

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

/*
 * Serial config.
 */
static const SerialConfig sdcfg = {
    115200,
    0,
    US_MR_CHRL_8_BIT | US_MR_PAR_NO
};

static UARTConfig uart_cfg_one = {
  .txend1_cb = NULL,
  .txend2_cb = NULL,
  .rxend_cb = NULL,
  .rxchar_cb = NULL,
  .rxerr_cb = NULL,
  .timeout_cb = NULL,
  .timeout = 20,
  .speed = 38400,
  .cr = 0,                                /* CR register */
  .mr = US_MR_CHRL_8_BIT | US_MR_PAR_NO,  /* MR register */
};

static UARTConfig uart_cfg_two = {
  .txend1_cb = NULL,
  .txend2_cb = NULL,
  .rxend_cb = NULL,
  .rxchar_cb = NULL,
  .rxerr_cb = NULL,
  .timeout_cb = NULL,
  .timeout = 20,
  .speed = 38400,
  .cr = 0,                                /* CR register */
  .mr = US_MR_CHRL_8_BIT | US_MR_PAR_NO,  /* MR register */
};

static NO_CACHE uint8_t arenaOne[65536];
static NO_CACHE uint8_t arenaTwo[65536];
static ioHdlcFrameMemPool fmpOne;
static ioHdlcFrameMemPool fmpTwo;
static NO_CACHE ioHdclUartDriver linkDriverOne;
static NO_CACHE ioHdclUartDriver linkDriverTwo;
static iohdlc_station_t theStationOne;
static iohdlc_station_t theStationTwo;
static iohdlc_station_peer_t thePeerOfOne;
static iohdlc_station_peer_t thePeerOfTwo;

static const iohdlc_station_config_t theStationOneConfig = {
  .mode     = IOHDLC_OM_NDM,
  .flags    = IOHDLC_FLG_PRI,
  .modulus  = 8,
  .addr     = 1,
  .driver   = (ioHdlcDriver *)&linkDriverOne,
  .fpp      = (ioHdlcFramePool *)&fmpOne,
  .phydriver        = &UARTD2,
  .phydriver_config = &uart_cfg_one,
};

static const iohdlc_station_config_t theStationTwoConfig = {
  .mode     = IOHDLC_OM_NDM,
  .flags    = 0,
  .modulus  = 8,
  .addr     = 2,
  .driver   = (ioHdlcDriver *)&linkDriverTwo,
  .fpp      = (ioHdlcFramePool *)&fmpTwo,
  .phydriver        = &FUARTD1,
  .phydriver_config = &uart_cfg_two,
};

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
  sdStart(&SD1, &sdcfg); /* console */
  chprintf(chp2, "Starting ioHDLC test.\r\n");

  fmpInit(&fmpOne, arenaOne, sizeof arenaOne, IOHDLC_DFL_I_SIZE, 4);
  ioHdclUartDriverInit(&linkDriverOne);
  ioHdlcStationInit(&theStationOne, &theStationOneConfig);
  ioHdlcAddPeer(&theStationOne, &thePeerOfOne, 2, IOHDLC_DFL_I_SIZE);

  fmpInit(&fmpTwo, arenaTwo, sizeof arenaTwo, IOHDLC_DFL_I_SIZE, 4);
  ioHdclUartDriverInit(&linkDriverTwo);
  ioHdlcStationInit(&theStationTwo,  &theStationTwoConfig);
  ioHdlcAddPeer(&theStationTwo, &thePeerOfTwo, 1, IOHDLC_DFL_I_SIZE);

  /*
   * Creates the blinker thread.
   */
  chThdCreateStatic(waThread1, sizeof waThread1, NORMALPRIO, Thread1, NULL);

  /*
   * Normal main() thread activity, in this demo it does nothing except
   * sleeping in a loop and check the button state.
   */
  while (true) {
    int32_t r;
    theStationTwo.flags &= ~IOHDLC_FLG_PRI;
    r = ioHdlcStationLinkUp(&theStationOne, 2, IOHDLC_OM_NRM);
    if (r == -1) {
      chprintf(chp2, "Error establishing a NRM connection with peer at addr %d. Errno = %d.\r\n",
          2, theStationOne.errorno);
    } else
      chprintf(chp2, "NRM connected with peer at addr %d!\r\n", 2);
    r = ioHdlcStationLinkDown(&theStationOne, 2);
    if (r == -1) {
      chprintf(chp2, "Error closing a connection with peer at addr %d. Errno = %d.\r\n",
          2, theStationOne.errorno);
    } else
      chprintf(chp2, "Disconnected from peer at addr %d!\r\n", 2);

    chprintf(chp2, "\r\nABM...\r\n", 2);

    chThdSleepMilliseconds(5000);
    theStationTwo.flags |= IOHDLC_FLG_PRI;
    r = ioHdlcStationLinkUp(&theStationOne, 2, IOHDLC_OM_ABM);
    if (r == -1) {
      chprintf(chp2, "Error establishing a ABM connection with peer at addr %d. Errno = %d.\r\n",
          2, theStationOne.errorno);
    } else
      chprintf(chp2, "ABM connected with peer at addr %d!\r\n", 2);
    r = ioHdlcStationLinkDown(&theStationOne, 2);
    if (r == -1) {
      chprintf(chp2, "Error closing a connection with peer at addr %d. Errno = %d.\r\n",
          2, theStationOne.errorno);
    } else
      chprintf(chp2, "Disconnected from peer at addr %d!\r\n", 2);

    chprintf(chp2, "\r\nAgain...\r\n", 2);

    chThdSleepMilliseconds(5000);
  }
}
