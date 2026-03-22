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
 * @file    include/ioHdlcfmempool.h
 * @brief   HDLC frame pool factory abstraction
 * @details Declares the fixed-memory pool implementation of
 *          @ref ioHdlcFramePool. This pool allocates frame objects from a
 *          caller-provided arena and is typically the default choice for
 *          embedded integrations that require deterministic memory usage.
 *
 *          The arena storage is owned by the caller. The pool object borrows
 *          that memory for its entire lifetime after initialization.
 *
 * @addtogroup ioHdlc_pool
 * @{
 */

#ifndef IOHDLCFMEMPOOL_H_
#define IOHDLCFMEMPOOL_H_

#include "ioHdlcframepool.h"
#include "ioHdlcosal.h"

/*
 * @extends @p ioHdlcFramePool
 */
#define _iohdlc_fmempool_methods    \
  _iohdlc_framepool_methods         \

#define _iohdlc_fmempool_data       \
  _iohdlc_framepool_data            \
  iohdlc_memory_pool_t mp;          \


/**
 * @brief   @p ioHdlcFrameMemPool vmt.
 */
struct _iohdlc_fmempool_vmt {
  _iohdlc_fmempool_methods
};

/**
 * @brief   HDLC frame mem pool class.
 * @details Extends @ref ioHdlcFramePool with the OSAL memory-pool object used
 *          to manage the backing arena.
 */
typedef struct {
  const struct _iohdlc_fmempool_vmt *vmt;
  _iohdlc_fmempool_data
} ioHdlcFrameMemPool;

#ifdef __cplusplus
extern "C" {
#endif
  /** @ingroup ioHdlc_pool */
  void fmpInit(ioHdlcFrameMemPool *fmpp, uint8_t *arena, size_t arenasize, size_t framesize, uint32_t framealign);
#ifdef __cplusplus
}
#endif

#endif /* IOHDLCFMEMPOOL_H_ */

/** @} */
