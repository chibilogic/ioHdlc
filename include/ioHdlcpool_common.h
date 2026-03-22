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
 * @file    include/ioHdlcpool_common.h
 * @brief   Common pool helper functions (OS-independent).
 * @details Provides shared watermark logic for all pool implementations.
 *          These helpers centralize threshold bookkeeping but intentionally do
 *          not perform locking or callback dispatch on behalf of the concrete
 *          pool implementation.
 *
 * @addtogroup ioHdlc_pool
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

/** @ingroup ioHdlc_pool */
void hdlc_pool_init_watermark(ioHdlcFramePool *fpp, uint32_t total);

/** @ingroup ioHdlc_pool */
void (*hdlc_pool_check_low_watermark(ioHdlcFramePool *fpp, bool *notify, void **cb_arg))(void *);

/** @ingroup ioHdlc_pool */
void (*hdlc_pool_check_high_watermark(ioHdlcFramePool *fpp, bool *notify, void **cb_arg))(void *);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCPOOL_COMMON_H_ */

/** @} */
