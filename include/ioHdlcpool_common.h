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
 * @file    include/ioHdlcpool_common.h
 * @brief   Common pool helper functions (OS-independent).
 * @details Provides shared watermark logic for all pool implementations.
 *
 * @addtogroup hdlc_pool
 * @{
 */

#ifndef IOHDLCPOOL_COMMON_H_
#define IOHDLCPOOL_COMMON_H_

#include <stdint.h>
#include <stdbool.h>
#include "ioHdlcframepool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Initialize watermark configuration to sensible defaults.
 * @details Sets low watermark at 20% and high watermark at 60% of total.
 *          Initializes callbacks to NULL.
 *
 * @param[in] fpp       Frame pool base class pointer
 * @param[in] total     Total number of frames in pool
 */
void hdlc_pool_init_watermark(ioHdlcFramePool *fpp, uint32_t total);

/**
 * @brief   Check and update watermark state after frame allocation.
 * @details Called after successfully allocating a frame from pool.
 *          Checks if free count has dropped below low threshold and updates
 *          state accordingly. Returns callback to invoke if state changed.
 *
 * @note    Caller must invoke callback OUTSIDE of any critical section/lock.
 *
 * @param[in] fpp       Frame pool base class pointer
 * @param[out] notify   Set to true if callback should be invoked
 * @param[out] cb_arg   Callback argument (only valid if notify=true)
 * @return              Callback function to invoke, or NULL if no notification needed
 */
void (*hdlc_pool_check_low_watermark(ioHdlcFramePool *fpp, bool *notify, void **cb_arg))(void *);

/**
 * @brief   Check and update watermark state after frame release.
 * @details Called after successfully releasing a frame back to pool.
 *          Checks if free count has risen above high threshold and updates
 *          state accordingly. Returns callback to invoke if state changed.
 *
 * @note    Caller must invoke callback OUTSIDE of any critical section/lock.
 *
 * @param[in] fpp       Frame pool base class pointer
 * @param[out] notify   Set to true if callback should be invoked
 * @param[out] cb_arg   Callback argument (only valid if notify=true)
 * @return              Callback function to invoke, or NULL if no notification needed
 */
void (*hdlc_pool_check_high_watermark(ioHdlcFramePool *fpp, bool *notify, void **cb_arg))(void *);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCPOOL_COMMON_H_ */

/** @} */
