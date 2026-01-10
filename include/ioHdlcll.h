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
 * @file    include/ioHdlcll.h
 * @brief   HDLC low level header.
 * @details
 *
 * @addtogroup hdlc_lowlevel
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
  /* Legacy API (modifies frame->elen) */
  void frameAddFCS(iohdlc_frame_t *frame);
  bool frameCheckFCS(const iohdlc_frame_t *frame);
  
  /* New API (works at specific offset, does NOT modify elen) */
  void frameAddFCS_at(iohdlc_frame_t *frame, size_t offset);
  bool frameCheckFCS_at(const iohdlc_frame_t *frame, size_t total_len);
  
  /* Transparency encoding/decoding */
  bool frameTransparentEncode(iohdlc_frame_t *dst, const iohdlc_frame_t *src);
  void frameTransparentDecode(iohdlc_frame_t *dst, const iohdlc_frame_t *src);
#ifdef __cplusplus
}
#endif

#endif /* IOHDLCLL_H_ */

/** @} */
