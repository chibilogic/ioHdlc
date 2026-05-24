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
 * @file    ioHdlcosal.c
 * @brief   ioHdlc OS Abstraction for ChibiOS.
 */

#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include "ioHdlc.h"
#include "ioHdlcosal.h"

/*===========================================================================*/
/* Logging Support                                                           */
/*===========================================================================*/

/**
 * @brief   Stream for logging output (configured by application).
 */
BaseSequentialStream *ioHdlcSDx = NULL;

/**
 * @brief   Get current time in milliseconds (relative to first call).
 * @return  Milliseconds with fractional part as double.
 */
double iohdlc_osal_get_time_ms(void) {
  static systime_t first_time = 0;
  static bool initialized = false;
  
  systime_t now = chVTGetSystemTime();
  
  if (!initialized) {
    first_time = now;
    initialized = true;
    return 0.0;
  }
  
  systime_t elapsed = chTimeDiffX(first_time, now);
  return (double)TIME_I2MS(elapsed);
}

static MUTEX_DECL(ioHdlcLogMutex);

int locked_chvprintf(BaseSequentialStream *chp, const char *fmt, va_list ap){
  chMtxLock(&ioHdlcLogMutex);
  int result = chvprintf(chp, fmt, ap);
  chMtxUnlock(&ioHdlcLogMutex);
  return result;
}

int locked_chprintf(BaseSequentialStream *chp, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = locked_chvprintf(chp, fmt, args);
  va_end(args);
  return result;
}
