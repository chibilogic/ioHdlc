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
 *
 * @details Provides DMA-safe allocation helpers using ChibiOS heap in no-cache section.
 */

#include "ch.h"
#include "hal.h"
#include "chmemheaps.h"
#include "chprintf.h"
#include "ioHdlc.h"
#include "ioHdlcosal.h"

#ifndef IOHDLC_DMA_HEAP_SIZE
#define IOHDLC_DMA_HEAP_SIZE 1024u
#endif

#if !defined(NO_CACHE)
/* Assume the existence of a no cached section named ".nocache".*/
#define NO_CACHE  __attribute__((section (".nocache")))
#endif

static memory_heap_t s_dma_heap;
static bool s_dma_heap_inited = false;
static NO_CACHE CH_HEAP_AREA(s_dma_heap_area, IOHDLC_DMA_HEAP_SIZE);

static void s_dma_heap_init_once(void) {
  if (!s_dma_heap_inited) {
    chHeapObjectInit(&s_dma_heap, s_dma_heap_area, sizeof(s_dma_heap_area));
    s_dma_heap_inited = true;
  }
}

void *iohdlc_dma_alloc(size_t size, size_t align) {
  s_dma_heap_init_once();
  return chHeapAllocAligned(&s_dma_heap, size, (unsigned)align);
}

void iohdlc_dma_free(void *p) {
  if (p) chHeapFree(p);
}

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
