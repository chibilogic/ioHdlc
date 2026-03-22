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
 * @file    src/ioHdlcpool_common.c
 * @brief   Common pool helper functions implementation.
 * @details OS-independent watermark logic shared by all pool implementations.
 *
 *          This module contains threshold and hysteresis bookkeeping only.
 *          Locking, allocation, reference counting, and callback dispatch
 *          policy remain the responsibility of the concrete pool backend.
 *
 * @addtogroup ioHdlc_pool
 * @{
 */

#include <stdint.h>
#include <stdbool.h>
#include "ioHdlcpool_common.h"

/**
 * @brief   Initialize watermark configuration to sensible defaults.
 * @details Sets low watermark at 20% and high watermark at 60% of total.
 *          Initializes callbacks to NULL.
 * @note    The concrete pool remains responsible for choosing when this helper
 *          is invoked during construction.
 *
 * @param[in] fpp       Frame pool base class pointer
 * @param[in] total     Total number of frames in pool
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
 * @details Called after successfully allocating a frame from pool.
 *          Checks if free count has dropped below low threshold and updates
 *          state accordingly. Returns callback to invoke if state changed.
 *
 * @note    Caller must invoke callback OUTSIDE of any critical section/lock.
 * @note    This helper only reports a transition; it does not itself call the
 *          callback.
 *
 * @param[in] fpp       Frame pool base class pointer
 * @param[out] notify   Set to true if callback should be invoked
 * @param[out] cb_arg   Callback argument (only valid if notify=true)
 * @return              Callback function to invoke, or NULL if no notification needed
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
 * @details Called after successfully releasing a frame back to pool.
 *          Checks if free count has risen above high threshold and updates
 *          state accordingly. Returns callback to invoke if state changed.
 *
 * @note    Caller must invoke callback OUTSIDE of any critical section/lock.
 * @note    This helper only reports a transition; it does not itself call the
 *          callback.
 *
 * @param[in] fpp       Frame pool base class pointer
 * @param[out] notify   Set to true if callback should be invoked
 * @param[out] cb_arg   Callback argument (only valid if notify=true)
 * @return              Callback function to invoke, or NULL if no notification needed
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

/** @} */
