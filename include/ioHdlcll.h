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
 * @file    include/ioHdlcll.h
 * @brief   HDLC low level header.
 * @details Declares low-level frame transformation helpers used by software
 *          drivers and test utilities to add/check FCS fields and apply
 *          transparency encoding.
 *
 *          These helpers operate on already allocated frame buffers. They do
 *          not allocate memory and they do not define ownership transfer.
 *          Callers remain responsible for buffer sizing, frame lifetime, and
 *          offset selection when using the explicit-offset variants.
 *
 * @addtogroup ioHdlc_frames
 * @{
 */

#ifndef IOHDLCLL_H_
#define IOHDLCLL_H_

#include "ioHdlcframe.h"
#include "ioHdlctypes.h"

/*===========================================================================*/
/* Module constants.                                                         */
/*===========================================================================*/

#define HDLC_FLAG     0x7E
#define HDLC_CTLESC   0x7D
#define HDLC_TMASK    0x20

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif
  /** @ingroup ioHdlc_frames */
  void frameAddFCS(iohdlc_frame_t *frame);

  /** @ingroup ioHdlc_frames */
  bool frameCheckFCS(const iohdlc_frame_t *frame);

  /** @ingroup ioHdlc_frames */
  void frameAddFCS_at(iohdlc_frame_t *frame, size_t offset);

  /** @ingroup ioHdlc_frames */
  bool frameCheckFCS_at(const iohdlc_frame_t *frame, size_t total_len);

  /**
   * @brief   Compute FCS-16 over a raw byte buffer.
   * @details CRC-16/X.25 (ISO 13239). The result is the complemented CRC
   *          ready to be appended as [lo, hi] after the payload.
   * @param[in] buf   Data buffer.
   * @param[in] len   Number of bytes to cover.
   * @param[out] fcs  Resulting 16-bit FCS (complemented).
   * @ingroup ioHdlc_frames
   */
  void ioHdlcComputeFCS(const uint8_t *buf, size_t len, uint16_t *fcs);

  /** @ingroup ioHdlc_frames */
  bool frameTransparentEncode(iohdlc_frame_t *dst, const iohdlc_frame_t *src);

  /** @ingroup ioHdlc_frames */
  void frameTransparentDecode(iohdlc_frame_t *dst, const iohdlc_frame_t *src);
#ifdef __cplusplus
}
#endif

#endif /* IOHDLCLL_H_ */

/** @} */
