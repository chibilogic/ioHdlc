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
 * @file    ioHdlcdma.h
 * @brief   DMA-safe buffer and cache maintenance helpers for ChibiOS.
 */

#ifndef IOHDLCDMA_H_
#define IOHDLCDMA_H_

#include "ch.h"
#include "hal.h"
#include "cache.h"
#include "ioHdlctypes.h"
#include <stddef.h>
#include <stdint.h>

#include "ioHdlcdma_port.h"

#ifndef IOHDLC_DMA_CACHE_MAINTENANCE_REQUIRED
#error "IOHDLC_DMA_CACHE_MAINTENANCE_REQUIRED must be defined by ioHdlcdma_port.h"
#endif

#ifndef IOHDLC_DMA_CACHE_LINE_SIZE
#if IOHDLC_DMA_CACHE_MAINTENANCE_REQUIRED == TRUE
#define IOHDLC_DMA_CACHE_LINE_SIZE          CACHE_LINE_SIZE
#else
#define IOHDLC_DMA_CACHE_LINE_SIZE          1U
#endif
#endif

#ifndef IOHDLC_DMA_ALIGN_DEFAULT
#define IOHDLC_DMA_ALIGN_DEFAULT            IOHDLC_DMA_CACHE_LINE_SIZE
#endif

#ifndef IOHDLC_DMA_NOCACHE_RANGE_AVAILABLE
#define IOHDLC_DMA_NOCACHE_RANGE_AVAILABLE  FALSE
#endif

#if IOHDLC_DMA_NOCACHE_RANGE_AVAILABLE == TRUE
#if !defined(IOHDLC_DMA_NOCACHE_BASE) || !defined(IOHDLC_DMA_NOCACHE_END)
#error "IOHDLC_DMA_NOCACHE_BASE and IOHDLC_DMA_NOCACHE_END must be defined"
#endif
#endif

static inline size_t iohdlc_dma_alignment(void) {

  return (size_t)IOHDLC_DMA_ALIGN_DEFAULT;
}

static inline bool iohdlc_dma_is_coherent(const void *p, size_t n) {
#if IOHDLC_DMA_CACHE_MAINTENANCE_REQUIRED == FALSE
  (void)p;
  (void)n;
  return true;
#elif IOHDLC_DMA_NOCACHE_RANGE_AVAILABLE == TRUE
  uintptr_t start = (uintptr_t)p;
  uintptr_t end = start + n;

  return (start >= (uintptr_t)IOHDLC_DMA_NOCACHE_BASE) &&
         (end <= (uintptr_t)IOHDLC_DMA_NOCACHE_END);
#else
  (void)p;
  (void)n;
  return false;
#endif
}

static inline size_t iohdlc_dma_buffer_alignment(const void *p, size_t n) {

  return iohdlc_dma_is_coherent(p, n) ? 1U : (size_t)IOHDLC_DMA_CACHE_LINE_SIZE;
}

static inline void iohdlc_dma_cache_flush_range(const void *p, size_t n) {
#if IOHDLC_DMA_CACHE_MAINTENANCE_REQUIRED == TRUE
  uintptr_t base;
  uintptr_t limit;

  if ((p == NULL) || (n == 0U))
    return;

  base = (uintptr_t)p & ~((uintptr_t)IOHDLC_DMA_CACHE_LINE_SIZE - 1U);
  limit = ((uintptr_t)p + n + IOHDLC_DMA_CACHE_LINE_SIZE - 1U) &
          ~((uintptr_t)IOHDLC_DMA_CACHE_LINE_SIZE - 1U);

  cacheBufferFlush((void *)base, (size_t)(limit - base));
#else
  (void)p;
  (void)n;
#endif
}

static inline void iohdlc_dma_cache_invalidate_range(void *p, size_t n) {
#if IOHDLC_DMA_CACHE_MAINTENANCE_REQUIRED == TRUE
  uintptr_t base;
  uintptr_t limit;

  if ((p == NULL) || (n == 0U))
    return;

  base = (uintptr_t)p & ~((uintptr_t)IOHDLC_DMA_CACHE_LINE_SIZE - 1U);
  limit = ((uintptr_t)p + n + IOHDLC_DMA_CACHE_LINE_SIZE - 1U) &
          ~((uintptr_t)IOHDLC_DMA_CACHE_LINE_SIZE - 1U);

  cacheBufferInvalidate((void *)base, (size_t)(limit - base));
#else
  (void)p;
  (void)n;
#endif
}

static inline void iohdlc_dma_tx_prepare(const void *p, size_t n) {

  if (!iohdlc_dma_is_coherent(p, n))
    iohdlc_dma_cache_flush_range(p, n);
}

static inline void iohdlc_dma_rx_prepare(void *p, size_t n) {

  if (!iohdlc_dma_is_coherent(p, n))
    iohdlc_dma_cache_invalidate_range(p, n);
}

static inline void iohdlc_dma_rx_complete(void *p, size_t n) {

  if (!iohdlc_dma_is_coherent(p, n))
    iohdlc_dma_cache_invalidate_range(p, n);
}

void *iohdlc_dma_alloc(size_t size, size_t align);
void iohdlc_dma_free(void *p);

#endif /* IOHDLCDMA_H_ */
