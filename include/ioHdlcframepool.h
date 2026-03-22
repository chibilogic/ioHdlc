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
 * @file    include/ioHdlcframepool.h
 * @brief   HDLC frame pool definitions header.
 * @details Defines the frame-pool abstraction used to allocate and recycle
 *          frame buffers. Pool implementations are responsible for ownership
 *          tracking, reference-count-aware release semantics, and optional
 *          watermark notifications.
 *
 *          Ownership model:
 *          - frames are allocated by the pool and returned to the pool;
 *          - addref/release semantics allow frames to be shared across layers;
 *          - the concrete pool implementation defines its own synchronization
 *            strategy, but users should assume that frame ownership must be
 *            explicit and balanced.
 *
 *          Integration guidance:
 *          - drivers and core logic should treat the pool as the sole allocator
 *            for frame objects;
 *          - callers should balance every successful take/addref path with a
 *            matching release path;
 *          - watermark callbacks are advisory integration hooks, not protocol
 *            events.
 *
 * @addtogroup ioHdlc_pool
 * @{
 */

#ifndef IOHDLCFRAMEPOOL_H_
#define IOHDLCFRAMEPOOL_H_

#include <stddef.h>  /* for size_t */
#include <stdint.h>  /* for uint32_t, uint8_t */
#include <stdbool.h> /* for bool */

/**
 * @brief   Forward declaration of the shared frame type.
 */
typedef struct iohdlc_frame iohdlc_frame_t;

/**
 * @brief   Frame pool watermark state.
 * @details Exposes whether the pool is in its normal range or below the low
 *          watermark threshold configured by the integration.
 */
typedef enum {
  IOHDLC_POOL_NORMAL = 0,    /**< Free frames above high threshold */
  IOHDLC_POOL_LOW_WATER = 1  /**< Free frames at or below low threshold */
} iohdlc_pool_state_t;

/**
 * @brief   @p ioHdlcFramePool interface methods.
 */
#define _iohdlc_framepool_methods                   \
  iohdlc_frame_t * (*take)(void *ip);               \
  void (*release)(void *ip, iohdlc_frame_t *fp);    \
  void (*addref)(iohdlc_frame_t *fp);               \

/**
 * @brief   Common storage shared by all frame-pool implementations.
 */
#define _iohdlc_framepool_data                      \
  size_t framesize;                                 \
  uint32_t total;      /* total frames in pool */   \
  uint32_t allocated;  /* currently allocated */    \
  uint32_t low_threshold;  /* low watermark */      \
  uint32_t high_threshold; /* high watermark */     \
  uint8_t low_pct;     /* low threshold % */        \
  uint8_t high_pct;    /* high threshold % */       \
  iohdlc_pool_state_t state; /* current state */    \
  void (*on_low)(void *arg);    /* LOW_WATER cb */  \
  void (*on_normal)(void *arg); /* NORMAL cb */     \
  void *cb_arg;        /* callback argument */      \

/**
 * @brief   @p ioHdlcFramePool vmt.
 * @details Concrete pool implementations expose allocation and reference
 *          management through this table.
 */
struct _iohdlc_framepool_vmt {
  _iohdlc_framepool_methods
};

/**
 * @brief   HDLC frame pool base class.
 * @details The base class stores both the allocation interface and the pool
 *          state required for watermark tracking.
 */
typedef struct {
  const struct _iohdlc_framepool_vmt *vmt;
  _iohdlc_framepool_data
} ioHdlcFramePool;

/**
 * @brief   Hdlc get a new frame from a frame pool.
 * @details Use this define to request a free frame buffer
 *          from the frame pool pointed by @p fpp.
 *
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 *
 * @return            the pointer to the allocated frame.
 * @retval NULL       if a free frame is not available.
 * @note              The caller becomes responsible for eventually balancing
 *                    the ownership through @ref hdlcReleaseFrame or equivalent
 *                    reference-count operations.
 */
#define hdlcTakeFrame(fpp)          ((fpp)->vmt->take(fpp))

/**
 * @brief   Hdlc release a frame to a frame pool.
 * @details Use this define to release the @p fp frame buffer.
 *          It will be returned to the frame pool pointed by @p fpp,
 *          if reference count reach 0.
 *
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @param[in]   fp    the pointer to the frame that is being released.
 * @note              The frame is returned to the pool only when its reference
 *                    count reaches zero.
 */
#define hdlcReleaseFrame(fpp, fp)   ((fpp)->vmt->release(fpp, fp))

/**
 * @brief   Hdlc add a reference to a frame.
 * @details Use this define to share the frame between
 *          different threads.
 *
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @param[in]   fp    the pointer to the frame referenced.
 *
 */
#define hdlcAddRef(fpp, fp)         ((fpp)->vmt->addref(fp))

/**
 * @brief   Get total frame count in pool.
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @return          total number of frames in pool
 */
#define hdlcPoolTotal(fpp)          ((fpp)->total)

/**
 * @brief   Get currently allocated frame count.
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @return          number of frames currently allocated
 */
#define hdlcPoolAllocated(fpp)      ((fpp)->allocated)

/**
 * @brief   Get free frame count.
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @return          number of frames currently free
 */
#define hdlcPoolFree(fpp)           ((fpp)->total - (fpp)->allocated)

/**
 * @brief   Get pool watermark state.
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @return          current watermark state (NORMAL or LOW_WATER)
 */
#define hdlcPoolGetState(fpp)       ((fpp)->state)

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief   Configure low and high watermark callbacks for a frame pool.
 * @details The callback context and the exact execution context in which the
 *          callbacks run are implementation-defined and must be documented by
 *          the concrete pool implementation.
 * @note    Callbacks should be used to react to memory pressure, not as a
 *          substitute for explicit pool accounting in application code.
 */
  void hdlcPoolConfigWatermark(ioHdlcFramePool *fpp, uint8_t low_pct, 
                                uint8_t high_pct, void (*on_low)(void *),
                                void (*on_normal)(void *), void *cb_arg);
#ifdef __cplusplus
}
#endif

#endif /* IOHDLCFRAMEPOOL_H_ */

/** @} */
