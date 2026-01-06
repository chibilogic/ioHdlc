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

#include "ioHdlctypes.h"

#define HDLC_BASIC_MIN_L  4
#define HDLC_FRFMT_MIN_L  5

/*===========================================================================*/
/* Module constants.                                         */
/*===========================================================================*/

#define IOHDLC_FRM_NS_PRESERVE  0x01  /**< Preserve N(S) in frame. */

/*===========================================================================*/
/* Module data structures and types.                                         */
/*===========================================================================*/

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

  uint8_t  flags;                 /* Frame flags (e.g., control bits) */
  uint16_t elen;                  /* Effective length of the frame, excluding
                                     FLAG and FCS. */
  uint8_t  refs;                  /* Number of references to this frame. */
  uint8_t  openingflag;           /* Optional opening flag. */
  uint8_t  frame[];
};

/*===========================================================================*/
/* Module macros.                                                            */
/*===========================================================================*/

/**
 * @name    Frame field access macros
 * @{
 * @brief   Direct access to frame fields in frame[] buffer.
 *          These macros handle optional FFF (Frame Format Field) offset.
 * 
 * @note    Use station->frame_offset.
 *          frame_offset is 0 if FFF not present, 1 if FFF present.
 */

/**
 * @brief   Get address field from frame.
 * @param   s   Pointer to station (for frame_offset).
 * @param   fp  Pointer to frame.
 * @return  Address field value.
 */
#define IOHDLC_FRAME_ADDR(s, fp)  ((fp)->frame[(s)->frame_offset])

/**
 * @brief   Get control field octet from frame.
 * @param   s   Pointer to station (for frame_offset).
 * @param   fp  Pointer to frame.
 * @param   idx Control field octet index (0 for first, 1-7 for extended).
 * @return  Control field octet value.
 */
#define IOHDLC_FRAME_CTRL(s, fp, idx)  ((fp)->frame[(s)->frame_offset + 1 + (idx)])

/**
 * @brief   Get pointer to info field start.
 * @param   s   Pointer to station (for frame_offset and ctrl_size).
 * @param   fp  Pointer to frame.
 * @return  Pointer to first info field octet.
 * @note    Works for all formats:
 *          - Basic (modulo 8): ctrl_size=1, offset = frame_offset + 1 + 1
 *          - Extended (modulo 128): ctrl_size=2, offset = frame_offset + 1 + 2
 *          - Extended (modulo 32768): ctrl_size=4, offset = frame_offset + 1 + 4
 *          - Extended (modulo 2^31): ctrl_size=8, offset = frame_offset + 1 + 8
 */
#define IOHDLC_FRAME_INFO(s, fp)  (&(fp)->frame[(s)->frame_offset + 1 + (s)->ctrl_size])

/**
 * @brief   Set N(S) in I-frame control field.
 * @param   s   Pointer to station (for modmask).
 * @param   fp  Pointer to frame.
 * @param   ns  N(S) value to set.
 * @note    Modulo 8: N(S) in bits 1-3 of ctrl[0]
 *          Modulo 128: N(S) in bits 1-7 of ctrl[0]
 */
#define IOHDLC_FRAME_SET_NS(s, fp, ns) \
  do { \
    if ((s)->modmask == 7) { \
      IOHDLC_FRAME_CTRL(s, fp, 0) = \
        (IOHDLC_FRAME_CTRL(s, fp, 0) & ~0x0E) | (((ns) & 0x07) << 1); \
    } else { \
      IOHDLC_FRAME_CTRL(s, fp, 0) = \
        (IOHDLC_FRAME_CTRL(s, fp, 0) & ~0xFE) | (((ns) & 0x7F) << 1); \
    } \
  } while (0)

/**
 * @brief   Set N(R) in I/S-frame control field.
 * @param   s   Pointer to station (for modmask).
 * @param   fp  Pointer to frame.
 * @param   nr  N(R) value to set.
 * @note    Modulo 8: N(R) in bits 5-7 of ctrl[0]
 *          Modulo 128: N(R) in bits 1-7 of ctrl[1]
 */
#define IOHDLC_FRAME_SET_NR(s, fp, nr) \
  do { \
    if ((s)->modmask == 7) { \
      IOHDLC_FRAME_CTRL(s, fp, 0) = \
        (IOHDLC_FRAME_CTRL(s, fp, 0) & ~0xE0) | (((nr) & 0x07) << 5); \
    } else { \
      IOHDLC_FRAME_CTRL(s, fp, 1) = \
        (IOHDLC_FRAME_CTRL(s, fp, 1) & ~0xFE) | (((nr) & 0x7F) << 1); \
    } \
  } while(0)

/**
 * @brief   Set P/F bit in control field.
 * @param   s   Pointer to station (for pfoctet).
 * @param   fp  Pointer to frame.
 * @param   pf  true to set P/F bit, false to clear.
 * @note    Uses station->pfoctet to determine which control byte contains P/F.
 *          pfoctet=0: P/F is bit 4 of ctrl[0] (modulo 8)
 *          pfoctet=1,2,4: P/F is bit 0 of ctrl[pfoctet] (modulo 128, 32768, 2^31)
 */
#define IOHDLC_FRAME_SET_PF(s, fp, pf) \
  do { \
    if ((s)->pfoctet == 0) { \
      if (pf) \
        IOHDLC_FRAME_CTRL(s, fp, 0) |= IOHDLC_PF_BIT; \
      else \
        IOHDLC_FRAME_CTRL(s, fp, 0) &= ~IOHDLC_PF_BIT; \
    } else { \
      if (pf) \
        IOHDLC_FRAME_CTRL(s, fp, (s)->pfoctet) |= IOHDLC_PFx_BIT; \
      else \
        IOHDLC_FRAME_CTRL(s, fp, (s)->pfoctet) &= ~IOHDLC_PFx_BIT; \
    } \
  } while(0)

/**
 * @brief   Get P/F bit from control field.
 * @param   s   Pointer to station (for pfoctet).
 * @param   fp  Pointer to frame.
 * @return  true if P/F bit is set, false otherwise.
 * @note    Uses station->pfoctet to determine which control byte contains P/F.
 *          pfoctet=0: P/F is bit 4 of ctrl[0] (modulo 8)
 *          pfoctet=1,2,4: P/F is bit 0 of ctrl[pfoctet] (modulo 128, 32768, 2^31)
 */
#define IOHDLC_FRAME_GET_PF(s, fp) \
  (((s)->pfoctet == 0) ? \
    ((IOHDLC_FRAME_CTRL(s, fp, 0) & IOHDLC_PF_BIT) != 0) : \
    ((IOHDLC_FRAME_CTRL(s, fp, (s)->pfoctet) & IOHDLC_PFx_BIT) != 0))

/** @} */

#endif /* IOHDLCFRAME_H_ */

/** @} */
