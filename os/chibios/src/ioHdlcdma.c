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
 * @file    ioHdlcdma.c
 * @brief   DMA-safe buffer allocation for ChibiOS.
 */

#include "ch.h"
#include "hal.h"
#include "chmemheaps.h"
#include "ioHdlcdma.h"

#ifndef IOHDLC_DMA_HEAP_SIZE
#define IOHDLC_DMA_HEAP_SIZE 1024U
#endif

#if !defined(NO_CACHE)
/* Assume the existence of a non-cacheable section named ".nocache". */
#define NO_CACHE  __attribute__((section (".nocache")))
#endif

static memory_heap_t s_dma_heap;
static bool s_dma_heap_inited = false;
static NO_CACHE CH_HEAP_AREA(s_dma_heap_area, IOHDLC_DMA_HEAP_SIZE);

static void s_dma_heap_init_once(void) {
  if (!s_dma_heap_inited) {
    chHeapObjectInit(&s_dma_heap, s_dma_heap_area, sizeof s_dma_heap_area);
    s_dma_heap_inited = true;
  }
}

void *iohdlc_dma_alloc(size_t size, size_t align) {
  if (!iohdlc_dma_is_coherent(s_dma_heap_area, sizeof s_dma_heap_area))
    return NULL;

  s_dma_heap_init_once();
  return chHeapAllocAligned(&s_dma_heap, size, (unsigned)align);
}

void iohdlc_dma_free(void *p) {
  if (p != NULL)
    chHeapFree(p);
}
