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
 * @brief   DMA-safe buffer helpers for Linux/POSIX.
 */

#ifndef IOHDLCDMA_H
#define IOHDLCDMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#define IOHDLC_DMA_ALIGN_DEFAULT 1

static inline size_t iohdlc_dma_alignment(void) {

  return IOHDLC_DMA_ALIGN_DEFAULT;
}

static inline bool iohdlc_dma_is_coherent(const void *p, size_t n) {
  (void)p;
  (void)n;
  return true;
}

static inline size_t iohdlc_dma_buffer_alignment(const void *p, size_t n) {
  (void)p;
  (void)n;
  return 1U;
}

static inline void iohdlc_dma_tx_prepare(const void *p, size_t n) {
  (void)p;
  (void)n;
}

static inline void iohdlc_dma_rx_prepare(void *p, size_t n) {
  (void)p;
  (void)n;
}

static inline void iohdlc_dma_rx_complete(void *p, size_t n) {
  (void)p;
  (void)n;
}

static inline void *iohdlc_dma_alloc(size_t size, size_t align) {
  (void)align;
  return malloc(size);
}

static inline void iohdlc_dma_free(void *ptr) {
  free(ptr);
}

#endif /* IOHDLCDMA_H */
