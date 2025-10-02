/*
    ioHdlc - Copyright (C) 2024 Isidoro Orabona

    GNU General Public License Usage

    ioHdlc software is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ioHdlc software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with ioHdlc software.  If not, see <http://www.gnu.org/licenses/>.

    Commercial License Usage

    Licensees holding valid commercial ioHdlc licenses may use this file in
    accordance with the commercial license agreement provided in accordance with
    the terms contained in a written agreement between you and Isidoro Orabona.
    For further information contact via email on github account.
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
  void frameAddFCS(iohdlc_frame_t *frame);
  bool frameCheckFCS(const iohdlc_frame_t *frame);
  bool frameTransparentEncode(iohdlc_frame_t *dst, const iohdlc_frame_t *src);
  void frameTransparentDecode(iohdlc_frame_t *dst, const iohdlc_frame_t *src);
#ifdef __cplusplus
}
#endif

#endif /* IOHDLCLL_H_ */

/** @} */
