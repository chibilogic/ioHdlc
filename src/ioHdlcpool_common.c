/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU Lesser General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
 */
/**
 * @file    src/ioHdlcpool_common.c
 * @brief   Common pool helper functions implementation.
 * @details OS-independent watermark logic shared by all pool implementations.
 */

#include <stdint.h>
#include <stdbool.h>
#include "ioHdlcpool_common.h"

/**
 * @brief   Initialize watermark configuration to sensible defaults.
 */
void hdlc_pool_init_watermark(ioHdlcFramePool *fpp, uint32_t total) {
  fpp->low_pct = 20;   /* Enter LOW_WATER at 20% free */
  fpp->high_pct = 60;  /* Exit LOW_WATER at 60% free */
  fpp->low_threshold = (total * 20) / 100;
  fpp->high_threshold = (total * 60) / 100;
  fpp->state = IOHDLC_POOL_NORMAL;
  fpp->on_low = NULL;     /* No callback by default */
  fpp->on_normal = NULL;
  fpp->cb_arg = NULL;
}

/**
 * @brief   Check and update watermark state after frame allocation.
 */
void (*hdlc_pool_check_low_watermark(ioHdlcFramePool *fpp, bool *notify, void **cb_arg))(void *) {
  uint32_t free = fpp->total - fpp->allocated;
  
  if (fpp->state == IOHDLC_POOL_NORMAL && free <= fpp->low_threshold) {
    fpp->state = IOHDLC_POOL_LOW_WATER;
    *notify = true;
    *cb_arg = fpp->cb_arg;
    return fpp->on_low;
  }
  
  *notify = false;
  return NULL;
}

/**
 * @brief   Check and update watermark state after frame release.
 */
void (*hdlc_pool_check_high_watermark(ioHdlcFramePool *fpp, bool *notify, void **cb_arg))(void *) {
  uint32_t free = fpp->total - fpp->allocated;
  
  if (fpp->state == IOHDLC_POOL_LOW_WATER && free > fpp->high_threshold) {
    fpp->state = IOHDLC_POOL_NORMAL;
    *notify = true;
    *cb_arg = fpp->cb_arg;
    return fpp->on_normal;
  }
  
  *notify = false;
  return NULL;
}
