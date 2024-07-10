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
 * @file    include/ioHdlcframe.h
 * @brief   HDLC frame definitions header.
 * @details
 *
 * @addtogroup hdlc_types
 * @{
 */

#ifndef IOHDLCFRAME_H_
#define IOHDLCFRAME_H_

#define FRAME_ENCODED   0x01
#define FRAME_HASFCS    0x02
#define FRAME_TEMPORARY 0x04

#define HDLC_BASIC_MIN_L  4
#define HDLC_FRFMT_MIN_L  5

/*
 * @brief   HDLC frame queue header.
 */
struct iohdlc_frame_q {
  iohdlc_frame_t *next;
  iohdlc_frame_t *prev;

  volatile uint16_t  nelem;       /* Number of elements in the queue. */
};

/*
 * @brief   Type of a HDLC frame.
 * @note    The frame includes the frame format (optional), the address,
 *          the control, and the FCS octets.
 *
 */
struct iohdlc_frame {
  iohdlc_frame_t *next;
  iohdlc_frame_t *prev;

  uint16_t elen;                  /* Effective length of the frame, excluding
                                     FLAG and FCS. */
  uint8_t  flags;                 /* Encoded or Temporary. */
  uint8_t  openingflag;           /* Optional opening flag. */
  uint8_t  frame[];
};

#endif /* IOHDLCFRAME_H_ */

/** @} */
