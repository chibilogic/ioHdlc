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
 * @file    include/ioHdlctx.h
 * @brief   Shared transmit types for core/driver/adapter.
 * @details Defines the common data structures used to describe transmit
 *          snapshots, transport assists, and driver-generated transmit
 *          plans. These types are intentionally policy-free: they do not by
 *          themselves decide queuing, ownership, or backend strategy.
 *
 * @addtogroup ioHdlc_tx
 * @{
 */

#ifndef IOHDLCTX_H
#define IOHDLCTX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "ioHdlctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Per-send mutable fields owned by protocol logic.
 * @details Stored in the frame so adapters can queue pending sends without
 *          dynamic allocation. The immutable identity of the frame (payload, N(S),
 *          frame kind) remains outside this structure.
 */
#define IOHDLC_TXS_INLINE_CTRL_MAX 2U

typedef struct {
  uint8_t  addr;            /**< Address octet selected by the core. */
  uint8_t  ctrl[IOHDLC_TXS_INLINE_CTRL_MAX]; /**< Inline control snapshot for modulo-8/128 paths. */
  uint8_t  lens;            /**< Low nibble: ctrl_len, high nibble: trailer_len past elen. */
} iohdlc_tx_snapshot_t;

#define IOHDLC_TXS_CTRL_LEN_MASK   0x0FU
#define IOHDLC_TXS_TRAILER_SHIFT   4U
#define IOHDLC_TXS_TRAILER_MASK    0xF0U

static inline uint8_t ioHdlc_txs_get_ctrl_len(const iohdlc_tx_snapshot_t *txs) {
  return txs->lens & IOHDLC_TXS_CTRL_LEN_MASK;
}

static inline void ioHdlc_txs_set_ctrl_len(iohdlc_tx_snapshot_t *txs, uint8_t ctrl_len) {
  txs->lens = (uint8_t)((txs->lens & IOHDLC_TXS_TRAILER_MASK) |
                        (ctrl_len & IOHDLC_TXS_CTRL_LEN_MASK));
}

static inline uint8_t ioHdlc_txs_get_trailer_len(const iohdlc_tx_snapshot_t *txs) {
  return (uint8_t)((txs->lens >> IOHDLC_TXS_TRAILER_SHIFT) & IOHDLC_TXS_CTRL_LEN_MASK);
}

static inline void ioHdlc_txs_set_trailer_len(iohdlc_tx_snapshot_t *txs, uint8_t trailer_len) {
  txs->lens = (uint8_t)((txs->lens & IOHDLC_TXS_CTRL_LEN_MASK) |
                        ((trailer_len & IOHDLC_TXS_CTRL_LEN_MASK) << IOHDLC_TXS_TRAILER_SHIFT));
}

/**
 * @brief   One segment of a transmit plan.
 */
typedef struct {
  const uint8_t *ptr;       /**< Segment start address. */
  size_t len;               /**< Segment length in bytes. */
} iohdlc_tx_seg_t;

/**
 * @brief Maximum number of segments emitted by the common formatter.
 */
#define IOHDLC_TXPLAN_MAX_SEGS     3U
/** @brief Scratch prefix capacity used by the common formatter. */
#define IOHDLC_TXPLAN_PREFIX_MAX  16U
/** @brief Scratch suffix capacity used by the common formatter. */
#define IOHDLC_TXPLAN_SUFFIX_MAX   8U
/** @brief Sentinel used when a TX plan has no segment index for a field. */
#define IOHDLC_TXPLAN_SEG_NONE   0xFFU

/**
 * @brief   FCS placement and coverage metadata for a TX plan.
 * @details When @p size is non-zero, the covered range starts at
 *          @p segv[begin_seg][begin_off] and ends at
 *          @p segv[end_seg][end_exclusive - 1], spanning full intermediate
 *          segments when @p begin_seg != @p end_seg.
 *
 *          If @p emitted is zero, the adapter/backend is expected to append or
 *          insert the FCS immediately before the suffix/postamble segment(s).
 */
typedef struct {
  size_t size;              /**< FCS size in bytes. Zero means no FCS. */
  size_t begin_off;         /**< First covered byte offset in begin_seg. */
  size_t end_exclusive;     /**< Exclusive end offset in end_seg. */
  uint8_t begin_seg;        /**< First covered segment index, or SEG_NONE. */
  uint8_t end_seg;          /**< Last covered segment index, or SEG_NONE. */
  uint8_t emitted;          /**< Non-zero if FCS bytes are already in the plan. */
} iohdlc_tx_fcs_desc_t;

/**
 * @brief   Driver-generated transmit plan.
 * @details Describes the wire image as a small prefix, an optional zero-copy
 *          payload segment, and a small suffix. Adapters may either consume
 *          the segments directly or materialize them into a contiguous buffer.
 */
typedef struct {
  uint8_t prefix[IOHDLC_TXPLAN_PREFIX_MAX]; /**< Driver-owned prefix scratch. */
  uint8_t suffix[IOHDLC_TXPLAN_SUFFIX_MAX]; /**< Driver-owned suffix scratch. */
  iohdlc_tx_seg_t segv[IOHDLC_TXPLAN_MAX_SEGS]; /**< Emitted segments in wire order. */
  iohdlc_tx_fcs_desc_t fcs; /**< FCS coverage and emission metadata. */
  uint8_t segc;            /**< Number of valid entries in @p segv. */
  uint8_t prefix_len;      /**< Number of valid bytes in @p prefix. */
  uint8_t suffix_len;      /**< Number of valid bytes in @p suffix. */
  size_t wire_len;         /**< Total serialized length in bytes. */
} iohdlc_tx_plan_t;

/**
 * @brief   Local build options chosen by the adapter at commit time.
 * @details These options are physical/link-layer decisions, not protocol
 *          semantics, and therefore do not belong to the core transmit state.
 */
typedef struct {
  bool prepend_opening_flag; /**< Emit an opening FLAG before the frame. */
} iohdlc_tx_plan_opts_t;

/** @brief Bitmask type for transport constraints. */
typedef uint32_t iohdlc_stream_constraint_mask_t;
/** @brief Bitmask type for transport execution assists. */
typedef uint32_t iohdlc_stream_assist_mask_t;

/**
 * @name Stream Assist Flags
 * @{
 */
#define IOHDLC_PORT_AST_TX_SCATTER_GATHER (1u << 0) /**< Backend can start true SG TX. */
#define IOHDLC_PORT_AST_TX_SEAMLESS_CHAIN (1u << 1) /**< Backend can arm the next TX without a visible wire gap. */
#define IOHDLC_PORT_AST_TX_NEEDS_CONTIG   (1u << 2) /**< Backend requires one contiguous TX buffer. */
#define IOHDLC_PORT_AST_TX_DONE_IN_ISR    (1u << 3) /**< TX completion callback runs in ISR context. */
/** @} */

/**
 * @brief   Unified stream capability descriptor.
 * @details Groups hard transport constraints with backend execution assists.
 *          Existing IOHDLC_PORT_CONSTR_* flags remain valid values for the
 *          @p constraints mask until stream-port APIs are migrated.
 */
typedef struct {
  iohdlc_stream_constraint_mask_t constraints; /**< Hard transport constraints. */
  iohdlc_stream_assist_mask_t assists;         /**< Backend execution assists. */
  uint8_t tx_fcs_offload_sizes[4];             /**< FCS sizes the backend can emit on TX. */
} iohdlc_stream_caps_t;

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCTX_H */

/** @} */
