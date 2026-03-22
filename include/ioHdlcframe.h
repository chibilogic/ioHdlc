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
 * @file    include/ioHdlcframe.h
 * @brief   HDLC frame definitions header.
 * @details Defines the frame representation shared by the protocol core,
 *          drivers, and frame-pool implementations. This module also contains
 *          intrusive queue links and helpers used to move frames across
 *          protocol and driver queues.
 *
 *          The frame object is intentionally storage-oriented: it does not by
 *          itself define protocol validity, synchronization, or ownership
 *          policy. Those contracts are established by the driver, core, and
 *          pool layers that exchange frame references.
 *
 * @addtogroup ioHdlc_frames
 * @{
 */

#ifndef IOHDLCFRAME_H_
#define IOHDLCFRAME_H_

#include "ioHdlctypes.h"

#define HDLC_BASIC_MIN_L  4  /**< Minimum serialized frame length without frame-format field. */
#define HDLC_FRFMT_MIN_L  5  /**< Minimum serialized frame length when a frame-format field is present. */

/*===========================================================================*/
/* Module constants.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Module data structures and types.                                         */
/*===========================================================================*/

/**
 * @brief   HDLC frame queue header (intrusive list).
 * @details Links point to other queue headers embedded in frames, not to frames directly.
 *          This allows a frame to participate in multiple queues simultaneously.
 */
struct iohdlc_frame_q {
  struct iohdlc_frame_q *next;
  struct iohdlc_frame_q *prev;
};

/**
 * @brief   Type of a HDLC frame.
 * @note    The frame includes the frame format (optional), the address,
 *          the control, and the FCS octets.
 *          Embeds two queue headers (q, q_aux) for multi-queue participation.
 * @note    The flexible array member @p frame stores the serialized protocol
 *          octets; its allocation size is determined by the owning pool.
 */
struct iohdlc_frame {
  iohdlc_frame_q_t  q;            /* Primary queue link (protocol queues: i_retrans_q, etc) */
  iohdlc_frame_q_t  q_aux;        /* Auxiliary queue link (driver use: TX queue, etc) */

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
 * @name    Container-of macros for queue headers
 * @{
 * @brief   Retrieve frame pointer from embedded queue header.
 */

/**
 * @brief   Get frame pointer from primary queue header.
 * @param   qp  Pointer to queue header (q field).
 * @return  Pointer to containing frame.
 * @note    q is at offset 0, so simple cast works.
 */
#define IOHDLC_FRAME_FROM_Q(qp) \
  ((iohdlc_frame_t *)(qp))

/**
 * @brief   Get frame pointer from auxiliary queue header.
 * @param   qp  Pointer to queue header (q_aux field).
 * @return  Pointer to containing frame.
 * @note    Uses offsetof for q_aux which is not at offset 0.
 */
#define IOHDLC_FRAME_FROM_Q_AUX(qp) \
  ((iohdlc_frame_t *)((char *)(qp) - offsetof(iohdlc_frame_t, q_aux)))

/** @} */

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
