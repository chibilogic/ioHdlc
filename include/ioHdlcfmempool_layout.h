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
 * @file    include/ioHdlcfmempool_layout.h
 * @brief   Shared fixed-frame-pool layout helpers.
 * @details Slots are arranged so the flexible payload member is cacheline
 *          aligned when the DMA policy reports a non-coherent backing arena.
 */

#ifndef IOHDLCFMEMPOOL_LAYOUT_H_
#define IOHDLCFMEMPOOL_LAYOUT_H_

#include "ioHdlcframe.h"
#include "ioHdlcdma.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t *base;
  size_t element_size;
  uint32_t count;
} iohdlc_fmempool_layout_t;

static inline uintptr_t iohdlc_fmempool_align_up_uintptr(uintptr_t value, size_t align) {

  return (value + align - 1U) & ~((uintptr_t)align - 1U);
}

static inline size_t iohdlc_fmempool_align_up_size(size_t value, size_t align) {

  return (value + align - 1U) & ~(align - 1U);
}

static inline size_t iohdlc_fmempool_min_object_align(void) {
#if defined(__GNUC__)
  return (size_t)__alignof__(iohdlc_frame_t);
#else
  return sizeof(uintptr_t);
#endif
}

static inline iohdlc_fmempool_layout_t iohdlc_fmempool_layout(void *arena, size_t arenasize,
                                                             size_t framesize, size_t preferred_align) {
  iohdlc_fmempool_layout_t layout = {0};
  size_t data_align = iohdlc_dma_buffer_alignment(arena, arenasize);
  size_t object_align = preferred_align;
  size_t payload_align = (size_t)IOHDLC_FRAME_PAYLOAD_ALIGNMENT;
  size_t min_object_align = iohdlc_fmempool_min_object_align();
  size_t header_size = offsetof(iohdlc_frame_t, frame);
  uintptr_t start = (uintptr_t)arena;
  uintptr_t end = start + arenasize;
  uintptr_t base;
  size_t usable;

  if (data_align > payload_align)
    return layout;

  if (object_align < min_object_align)
    object_align = min_object_align;
  if (object_align < payload_align)
    object_align = payload_align;

  layout.element_size = iohdlc_fmempool_align_up_size(header_size + framesize, object_align);

  base = iohdlc_fmempool_align_up_uintptr(start, object_align);
  if (base >= end)
    return layout;

  usable = (size_t)(end - base);
  layout.base = (uint8_t *)base;
  layout.count = (uint32_t)(usable / layout.element_size);

  return layout;
}

#endif /* IOHDLCFMEMPOOL_LAYOUT_H_ */
