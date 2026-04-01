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
 * @file    include/ioHdlc.h
 * @brief   Public umbrella header for the ioHdlc API.
 * @details Includes the primary public types and integration interfaces
 *          required by most applications. This header is intended as the main
 *          public entry point when using ioHdlc from application code.
 *
 *          Typical integrations can include only this header and then inspect
 *          more specific headers only when implementing a custom driver,
 *          transport backend, or frame-pool strategy.
 *
 *          The public API is layered. At a minimum, most applications need:
 *          - station/core types and public constants;
 *          - a driver implementation;
 *          - a frame pool implementation;
 *          - the runner and OSAL facilities required by the selected target.
 *
 *          Recommended integration sequence:
 *          - initialize the station configuration and runtime objects;
 *          - configure the driver and frame pool used by the station;
 *          - start the runner/execution model used by the selected target;
 *          - use the station-facing read/write and control APIs;
 *          - stop runner and drivers before tearing down owned resources.
 *
 * @addtogroup ioHdlc_api_h
 * @{
 */

#ifndef IOHDLC_H_
#define IOHDLC_H_

#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcfmempool.h"
#include "ioHdlcdriver.h"
#include "ioHdlcqueue.h"
#include "ioHdlcosal.h"
#include "ioHdlc_runner.h"

/*===========================================================================*/
/* Module constants.                                                         */
/*===========================================================================*/

/** @cond IOHDLC_INTERNAL */
/**
 * @name    ISO13239 Control field bits format
 * @{
 */

/* Frame identifier bits. They are always the lsb of the
   first octet of the control field. */
#define IOHDLC_I_ID       0x00  /**< @brief I-frame identifier value. */
#define IOHDLC_I_MASK     0x01  /**< @brief I-frame bits mask.        */
#define IOHDLC_S_ID       0x01  /**< @brief S-frame identifier value. */
#define IOHDLC_S_MASK     0x03  /**< @brief S-frame bits mask.        */
#define IOHDLC_U_ID       0x03  /**< @brief U-frame identifier value. */
#define IOHDLC_U_MASK     0x03  /**< @brief U-frame bits mask.        */

#define IOHDLC_U_TYPE     0x03  /**< @brief U-frame type.             */
#define IOHDLC_S_TYPE     0x01  /**< @brief S-frame type.             */
#define IOHDLC_I_TYPE     0x02  /**< @brief I-frame type.             */
#define IOHDLC_A_TYPE     0x00  /**< @brief I-frame type alt.         */

/** @brief Test whether a control octet identifies an I-frame. */
#define IOHDLC_IS_I_FRM(ctrl)     (((ctrl) & IOHDLC_I_MASK) == IOHDLC_I_ID)
/** @brief Test whether a control octet identifies an S-frame. */
#define IOHDLC_IS_S_FRM(ctrl)     (((ctrl) & IOHDLC_S_MASK) == IOHDLC_S_ID)
/** @brief Test whether a control octet identifies a U-frame. */
#define IOHDLC_IS_U_FRM(ctrl)     (((ctrl) & IOHDLC_U_MASK) == IOHDLC_U_ID)

/* S-frame functions.
   The SS bits are low to hi, i.e. S bits = 01 means 2 decimal.*/
#define IOHDLC_S_FUN_MASK 0x0C  /**< Mask for S-frame function bits. */
#define IOHDLC_S_RR       0x00  /**< @brief Receive ready, RR, (S bits = 00) command and response. */
#define IOHDLC_S_REJ      0x08  /**< @brief Reject, REJ, (S bits = 01) command and response. */
#define IOHDLC_S_RNR      0x04  /**< @brief Receive not ready, RNR, (S bits = 10) command and response. */
#define IOHDLC_S_SREJ     0x0C  /**< @brief Selective reject, SREJ, (S bits = 11) command and response. */

/* U-frame functions. */
#define IOHDLC_U_FUN_MASK 0xEC  /**< Mask for U-frame function bits. */
#define IOHDLC_U_SNRM     0x80  /**< @brief Set normal response mode (SNRM) command. */
#define IOHDLC_U_SARM     0x0C  /**< @brief Set asynchronous response mode (SARM) command. */
#define IOHDLC_U_SABM     0x2C  /**< @brief Set asynchronous balanced mode (SABM) command. */
#define IOHDLC_U_DISC     0x40  /**< @brief Disconnect (DISC) command. */
#define IOHDLC_U_SNRME    0xCC  /**< @brief Set normal response mode extended (SNRME) command. */
#define IOHDLC_U_SARME    0x4C  /**< @brief Set asynchronous response mode extended (SARME) command- */
#define IOHDLC_U_SABME    0x6C  /**< @brief Set asynchronous balanced mode extended (SABME) command. */
#define IOHDLC_U_SIM      0x04  /**< @brief Set initialization mode (SIM) command. */
#define IOHDLC_U_UP       0x20  /**< @brief Unnumbered poll (UP) command. */
#define IOHDLC_U_UI       0x00  /**< @brief Unnumbered information (UI) command/response. */
#define IOHDLC_U_XID      0xAC  /**< @brief Exchange identification (XID) command/response. */
#define IOHDLC_U_RSET     0x8C  /**< @brief Reset (RSET) command. */
#define IOHDLC_U_TEST     0xE0  /**< @brief Test (TEST) command/response. */
#define IOHDLC_U_SM       0xC0  /**< @brief Set mode (SM) command. */
#define IOHDLC_U_UIH      0xEC  /**< @brief Unnumbered information with header check (UIH) command/response. */

#define IOHDLC_U_UA       0x60  /**< @brief Unnumbered acknowledgment (UA) response. */
#define IOHDLC_U_FRMR     0x84  /**< @brief Frame reject (FRMR) response. */
#define IOHDLC_U_DM       0x0C  /**< @brief Disconnected mode (DM) response. */
#define IOHDLC_U_RD       0x40  /**< @brief Request disconnect (RD) response. */
#define IOHDLC_U_RIM      0x04  /**< @brief Request initialization mode (RIM) response. */
/** @} */

/** @endcond */

#define IOHDLC_PF_BIT     0x10  /**< Poll/final bit in the first control octet for modulo 8. */

/**
 * @name    FRMR reason bits (ISO 13239, 5.5.3.1)
 * @{
 */
#define IOHDLC_FRMR_W    0x01  /**< @brief Control field invalid or not implemented. */
#define IOHDLC_FRMR_X    0x02  /**< @brief Information field not permitted. */
#define IOHDLC_FRMR_Y    0x04  /**< @brief Invalid N(R). */
#define IOHDLC_FRMR_Z    0x08  /**< @brief Information field exceeded maximum length. */
/** @} */
#define IOHDLC_PFx_BIT    0x01  /**< Poll/final bit in non-first control octets for extended modes. */

/**
 * @name    Operational and non-operational modes and flags
 * @{
 */
#define IOHDLC_OM_ABM     0x01  /**< @brief Asynchronous balanced mode (ABM). */
#define IOHDLC_OM_ARM     0x02  /**< @brief Asynchronous response mode (ARM). */
#define IOHDLC_OM_NRM     0x03  /**< @brief Normal response mode (NRM). */
#define IOHDLC_OM_NDM     0x04  /**< @brief Normal disconnected mode (NDM). */
#define IOHDLC_OM_ADM     0x05  /**< @brief Asynchronous disconnected mode (ADM). */
#define IOHDLC_OM_IM      0x06  /**< @brief Initialization mode (IM). */

#define IOHDLC_FLG_PRI    0x01  /**< @brief Primary station flag. */
#define IOHDLC_FLG_TWA    0x02  /**< @brief Two Way Alternate flag. */
#define IOHDLC_FLG_IDL    0x04  /**< @brief Idle line flag. */
#define IOHDLC_FLG_BUSY   0x08  /**< @brief Local station is busy. */
#define IOHDLC_FLG_TRACE  0x80  /**< @brief Trace flag. */

/** @} */

/**
 * @name    Supported optional functions over basic
 * @{
 *
 * See "HDLC optional functions" parameter in Table 16
 */
#define IOHDLC_OPT_REJ_OCT  0         /* Bit 2 -> Octet nr.0 (bit numbered starting by 1).*/
#define IOHDLC_OPT_REJ      (1U << 1) /**< @brief Option 2    - REJ recovery. Default. */

#define IOHDLC_OPT_SST_OCT  2         /* Bit 18 -> Octet nr.2 */
#define IOHDLC_OPT_SST      (1U << 1) /**< @brief Option 15   - Start/stop transmission. Default. */

#define IOHDLC_OPT_STB_OCT  2         /* Bit 19 -> Octet nr.2 */
#define IOHDLC_OPT_STB      (1U << 2) /**< @brief Option 15.1 - Start/stop transmission with basic
                                                                transparency. Default. */
#define IOHDLC_OPT_FFF_OCT  4         /* Bit 33 -> Octet nr.4 */
#define IOHDLC_OPT_FFF      (1U << 0) /**< @brief Option 22   - Frame format field. Type 0/1 supported. */

#define IOHDLC_OPT_INH_OCT  4         /* Bit 35 -> Octet nr.4 */
#define IOHDLC_OPT_INH      (1U << 2) /**< @brief Option 24   - Inhibit bit or octet insertion. */
/** @} */

/**
 * @name    Application event mask
 * @{
 */
#define IOHDLC_APP_EVT_MASK_DEFAULT  EVENT_MASK(31)  /**< @brief Default event mask for app API.
                                                            High bit to avoid conflicts with user events. */
/** @} */

/** @cond IOHDLC_INTERNAL */
/**
 * @name    Time-critical option flags
 * @brief   Fast-access flags for performance-critical code paths.
 *          Mirrors selected bits from optfuncs[] for zero-branch access.
 * @{
 */
#define IOHDLC_CFLG_FFF   0x01  /**< @brief Frame Format Field present (Option 22). */
#define IOHDLC_CFLG_REJ   0x02  /**< @brief REJ recovery supported (Option 2). */
#define IOHDLC_CFLG_STB   0x04  /**< @brief Start/stop with basic transparency (Option 15.1). */
/** @} */

/* pf_state definitions. */
#define IOHDLC_P_RCVED    0x01  /**< Secondary-side flag: a received P still needs acknowledgement. */
#define IOHDLC_F_RCVED    0x02  /**< Primary-side flag: an outstanding poll has already received F. */

/* um_state definitions. */
#define IOHDLC_UM_SENT    0x01  /**< Unnumbered command sent and not yet acknowledged. */
#define IOHDLC_UM_RCVED   0x02  /**< Unnumbered command received and pending response. */

/* ss_state definitions. */
#define IOHDLC_SS_BUSY    0x01  /**< Peer-side busy state; I-frames cannot currently be accepted. */
#define IOHDLC_SS_REJPEND 0x02  /**< A REJ supervisory frame still needs to be sent. */
#define IOHDLC_SS_RECVING 0x04  /**< Peer is currently receiving I-frames. */
#define IOHDLC_SS_NEED_P  0x08  /**< Primary-side hint that the next suitable S-frame should carry P. */
#define IOHDLC_SS_ST_DISM 0x40  /**< Peer is known to be in disconnected mode. */
#define IOHDLC_SS_ST_CONN 0x80  /**< Peer is currently connected. */

/* helper macros */
/** @brief Test whether the local station is secondary. */
#define IOHDLC_IS_SEC(s)      (!((s)->flags & IOHDLC_FLG_PRI))
/** @brief Test whether the local station is primary. */
#define IOHDLC_IS_PRI(s)      ((s)->flags & IOHDLC_FLG_PRI)
/** @brief Test whether the local station is currently busy. */
#define IOHDLC_IS_BUSY(s)     ((s)->flags & IOHDLC_FLG_BUSY)
/** @brief Test whether the station is in a disconnected mode. */
#define IOHDLC_IS_DISC(s)     (((s)->mode == IOHDLC_OM_NDM) || \
                               ((s)->mode == IOHDLC_OM_ADM))
/** @brief Test whether the station runs in NRM mode. */
#define IOHDLC_IS_NRM(s)      ((s)->mode == IOHDLC_OM_NRM)
/** @brief Test whether the station runs in NDM mode. */
#define IOHDLC_IS_NDM(s)      ((s)->mode == IOHDLC_OM_NDM)
/** @brief Test whether the station runs in ABM mode. */
#define IOHDLC_IS_ABM(s)      ((s)->mode == IOHDLC_OM_ABM)
/** @brief Test whether the station runs in ARM mode. */
#define IOHDLC_IS_ARM(s)      ((s)->mode == IOHDLC_OM_ARM)
/** @brief Test whether the station runs in ADM mode. */
#define IOHDLC_IS_ADM(s)      ((s)->mode == IOHDLC_OM_ADM)
/** @brief Test whether a received poll is pending acknowledgement. */
#define IOHDLC_P_ISRCVED(s)   ((s)->pf_state & IOHDLC_P_RCVED)
/** @brief Test whether a final response has been received. */
#define IOHDLC_F_ISRCVED(s)   ((s)->pf_state & IOHDLC_F_RCVED)
/** @brief Test whether a poll has been sent and is still awaiting final response. */
#define IOHDLC_P_SENT(s)      (((s)->pf_state & IOHDLC_F_RCVED) == 0)
/** @brief Test whether a final bit has been sent by the secondary side. */
#define IOHDLC_F_SENT(s)      (((s)->pf_state & IOHDLC_P_RCVED) == 0)
/** @brief Test whether a P/F exchange is currently in flight. */
#define IOHDLC_P_ISFLYING(s)  ((IOHDLC_IS_PRI(s) && !IOHDLC_F_ISRCVED(s)) || \
                               (IOHDLC_IS_SEC(s) && !IOHDLC_P_ISRCVED(s)))
/** @brief Acknowledge the pending received poll state. */
#define IOHDLC_ACK_P(s)       ((s)->pf_state &= ~IOHDLC_P_RCVED)
/** @brief Acknowledge the pending received final state. */
#define IOHDLC_ACK_F(s)       ((s)->pf_state &= ~IOHDLC_F_RCVED)
/** @brief Test whether Frame Format Field support is active. */
#define IOHDLC_HAS_FFF(s)     ((s)->flags_critical & IOHDLC_CFLG_FFF)
/** @brief Test whether REJ recovery is enabled. */
#define IOHDLC_USE_REJ(s)     ((s)->flags_critical & IOHDLC_CFLG_REJ)
/** @brief Test whether start/stop transparency is enabled. */
#define IOHDLC_USE_STB(s)     ((s)->flags_critical & IOHDLC_CFLG_STB)
/** @brief Test whether the station runs in Two-Way Alternate mode. */
#define IOHDLC_USE_TWA(s)     ((s)->flags & IOHDLC_FLG_TWA)
/** @brief Test whether line-idle state is currently latched. */
#define IOHDLC_ST_IDLE(s)     ((s)->flags & IOHDLC_FLG_IDL)
/** @brief Test whether an unnumbered command is outstanding. */
#define IOHDLC_UM_ISSENT(p)   ((p)->um_state & IOHDLC_UM_SENT)
/** @brief Test whether a peer is currently disconnected. */
#define IOHDLC_PEER_DISC(p)   (!((p)->ss_state & IOHDLC_SS_ST_CONN))
/** @brief Test whether a peer has advertised busy state. */
#define IOHDLC_PEER_BUSY(p)   (((p)->ss_state & IOHDLC_SS_BUSY))
/** @brief Test whether a peer state indicates the need to send P. */
#define IOHDLC_NEED_P(p)      ((p)->ss_state & IOHDLC_SS_NEED_P)
/** @brief Mark that the next suitable transmit opportunity should send P. */
#define IOHDLC_SET_NEED_P(s,p)  (IOHDLC_IS_PRI(s) ? ((p)->ss_state |= IOHDLC_SS_NEED_P) : 0)
/** @brief Clear the pending-need-P state. */
#define IOHDLC_CLR_NEED_P(p)  ((p)->ss_state &= ~IOHDLC_SS_NEED_P)

/**
 * @name    System-defined parameters
 * @{
 */
#define IOHDLC_DFL_I_SIZE       64  /**< Default I-frame payload budget used by legacy paths. */
#define IOHDLC_DFL_MODULUS      8   /**< Default modulo value for basic configurations. */
#define IOHDLC_DFL_T3_T1_RATIO  5   /**< Default ratio between T3 and T1 timeouts. */
/** @} */

/** @endcond */

/*===========================================================================*/
/* Module pre-compile time settings.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

/*===========================================================================*/
/* Module data structures and types.                                         */
/*===========================================================================*/

/**
 * @brief   Station peer list header.
 */
struct iohdlc_peer_list {
  iohdlc_station_peer_t *next;
  iohdlc_station_peer_t *prev;
};

#if defined(IOHDLC_ENABLE_STATISTICS)
/**
 * @brief   Peer statistics counters for debugging and monitoring.
 * @note    Grouped in a separate structure for easy access and reporting.
 * @note    Enabled only when IOHDLC_ENABLE_STATISTICS is defined.
 */
struct iohdlc_peer_stats {
  volatile uint32_t rej_received;       /* REJ frames received (peer detected error). */
  volatile uint32_t checkpoints;        /* Checkpoint retransmissions triggered. */
  volatile uint32_t timeouts;           /* Reply timeout events. */
  volatile uint32_t out_of_sequence;    /* Out-of-sequence I-frames discarded. */
  volatile uint32_t pool_low_water;     /* Pool low-watermark events (RNR backpressure applied). */
};
#endif /* IOHDLC_ENABLE_STATISTICS */

/**
 * @brief   Type of a HDLC station peer.
 * @note    A station peer represents the state of the
 *          link that the station maintains with a peer.
 */
struct iohdlc_station_peer {
  iohdlc_station_peer_t *next;  /* Next peer in the list. */
  iohdlc_station_peer_t *prev;  /* Previous peer in the list. */
  iohdlc_station_t *stationp;   /* The station this peer belongs on. */

  /* configuration parameters. */
  uint32_t addr;                /* Address of the peer. 0 if is not determined yet. */
  uint32_t ks;                  /* Window size k, transmit. Max modulus - 1. */
  uint32_t kr;                  /* Window size k, receive. Max modulus - 1. */
  uint32_t mifls;               /* Maximum information field length, transmit. */
  uint32_t miflr;               /* Maximum information field length, receive. */

  /* state variables. */
  volatile uint32_t vs;         /* Send state variable V(S). */
  volatile uint32_t vr;         /* Receive state variable V(R). */
  volatile uint32_t nr;         /* Last N(R) received and accepted + 1.
                                   Invariant: (vs-nr) & modmask = len(i_retrans_q), must be ≤ ks. */
  volatile uint32_t vs_highest; /* Highest value of state variable V(S) in the
                                   same numbering cycle. */
  volatile uint32_t rej_actioned; /* a value x != 0 of this field indicates that a
                                     REJ exception with N(R) = x-1 is in action. The receipt
                                     of a I-frame with N(S) = x-1 clears the exception. */
  volatile uint32_t chkpt_actioned; /* a value x != 0 indicates checkpoint retransmission
                                       started with first frame N(S) = x-1. Used to inhibit
                                       REJ if requesting same particular I frame (ISO 13239). */
  volatile uint32_t vs_atlast_pf; /* V(S) at the time of transmission of the last
                                     frame with the P bit set in case of primary/combined station
                                     or with the F bit set in case of secondary station. */
  volatile uint32_t i_pending_count;  /* Total pending I-frames: len(i_trans_q) + len(i_retrans_q).
                                         Incremented when adding to i_trans_q, decremented when
                                         removing from i_retrans_q. Maintained for O(1) flow control. */
  volatile uint8_t  um_state;   /* Unnumbered state. See definitions. */
  volatile uint8_t  ss_state;   /* Supervision state. See definitions. */
  uint8_t  um_cmd;              /* Unnumbered command event payload. */
  uint8_t  um_rsp;              /* Unnumbered response event payload. */

  /* data queues. */
  iohdlc_frame_q_t i_retrans_q; /* I-frame retransmission queue. No more than ks frames
                                   will be in this queue.*/
  iohdlc_frame_q_t i_recept_q;  /* I-frame reception queue. */
  iohdlc_frame_q_t i_trans_q;   /* I-frame transmission queue.
                                   The frames in this queue have address defined, but
                                   not N(R), N(S) or P/F. The latter three will be set when
                                   the frame will be picked for actual transmission.
                                   A limit is imposed on the number of frames in this queue,
                                   typically set to ks, to reduce frame consumption from
                                   the pool. */
                                   
  /* flow control. */
  iohdlc_condvar_t tx_cv;       /* TX flow control condition variable.
                                   Blocks app when i_pending_count >= 2*ks or pool is LOW_WATER.
                                   Broadcast when space becomes available (ACK received or
                                   pool normal). */
  iohdlc_sem_t i_recept_sem;    /* RX data available counting semaphore (one count per frame).
                                   Signaled when I-frame arrives in i_recept_q.
                                   Used by Read to block until data available. */
  iohdlc_mutex_t state_mutex;   /* Mutex protecting protocol state variables:
                                   nr, vr, vs, i_pending_count, queues (i_retrans_q, i_trans_q),
                                   chkpt_actioned, rej_actioned, ss_state,
				   partial_read_frame/offset. */
  /* partial read state. */
  iohdlc_frame_t *partial_read_frame;  /* Frame being read partially (NULL if none). */
  size_t partial_read_offset;          /* Offset within partial_read_frame's info field. */

  /* virtual timers. */
  iohdlc_virtual_timer_t reply_tmr;   /* Primary/combined station command reply time-out
                                         timer. */
  iohdlc_virtual_timer_t t3_tmr;/* Primary/secondary/combined station T3 time-out timer. */

  /* retry counters. */
  volatile uint8_t poll_retry_count;  /* Current number of retries for frames with P=1 (poll bit).
                                         Incremented on reply_tmr expiry, reset when F=1
                                         response received. Volatile: accessed from timer context. */
  uint8_t poll_retry_max;       /* Maximum number of retries allowed for poll frames.
                                   When poll_retry_count >= poll_retry_max, the link
                                   is considered down. */

  /* FRMR exception condition state (ISO 13239, 5.5.3). */
  volatile bool frmr_condition;       /* true = frame reject exception active. */
  uint8_t  frmr_rejected_ctrl[2];     /* Control field of the rejected frame
                                         (1 byte mod 8, 2 bytes mod 128). */
  uint8_t  frmr_reason;              /* IOHDLC_FRMR_W/X/Y/Z reason bits. */
  bool     frmr_cr;                  /* C/R bit of the rejected frame. */

#if defined(IOHDLC_ENABLE_STATISTICS)
  /* statistics counters. */
  struct iohdlc_peer_stats stats;  /* Debug/monitoring statistics. */
#endif
};

/**
 * @brief   Type of a HDLC station descriptor.
 */
struct iohdlc_station {

  /* configuration parameters. */
  uint8_t   mode;               /* Operational mode of this station. */
  uint8_t   flags;              /* Station flags: TWA, PRIMARY, IDLE, BUSY. */
  uint8_t   pf_state;           /* P/F sent/received state. See definitions. */
  uint8_t   pfoctet;            /* P/F octet number. Calculated from modulus. (0, 1, 2, 4). */
  uint32_t  modmask;            /* Modulus bit mask: 7 for mod 8, 127 for mod 128, 32767 for
                                   mod 32768, 2147483647 for mod 2^31. */
  uint8_t   ctrl_size;          /* Control field size in bytes (1, 2, 4, 8). Precalculated
                                   from modulus for fast frame field access. */
  uint8_t   frame_offset;       /* Precalculated FFF offset: 0 if no FFF, 1 or, 2 if FFF present.
                                   Used for fast frame field access without runtime checks. */
  uint8_t   fcs_size;           /* FCS size in bytes (0, 2, 4). Queried from driver. */
  uint8_t   flags_critical;     /* Time-critical option flags (FFF, REJ, STB) for fast access. */
  uint8_t   optfuncs[5];        /* Active HDLC optional functions among those supported.
                                   See ISO13239 Table 16. Maintains ISO format for XID. */
  uint16_t  reply_timeout_ms;   /* Reply timer timeout value in milliseconds. Default: 100ms. */
  uint8_t   poll_retry_max_cfg; /* Configured max poll retries (from station config). Default: 5. */
  uint32_t  port_constraints;  /* Transport constraints copied from phydriver at init. IOHDLC_PORT_CONSTR_* */
  uint32_t  addr;               /* Address of the station. */
  iohdlc_station_peer_t *c_peer;    /* The peer the station is currently talking to. */
  volatile uint8_t connected_count; /* Number of currently connected peers. */

  /* state, peers, pool and queues. */
  iohdlc_peer_list_t  peers;    /* The header of the list of the peers of this station. Stations
                                   in ABM mode and secondary stations have only one peer. */
  ioHdlcFrameMemPool frame_pool;/* Frame pool auto-initialized from arena. Pool dimensioned to satisfy
                                   the windows size and the reception buffering of all peers. */
  iohdlc_tx_fn_t tx_fn;         /* Active transmit handler for the current mode. */
  iohdlc_rx_fn_t rx_fn;         /* Active receiver handler for the current mode. */

  /* link driver. */
  ioHdlcDriver *driver;         /* Data link driver the station operates on. */

  /* events. */
  iohdlc_event_source_t cm_es;   /* Event source for internal core events (RX/TX/timer). */
  iohdlc_event_source_t app_es;  /* Event source for application events (link status, data). */
  iohdlc_event_listener_t cm_listener; /* Event listener for TX thread. */

  /* runner context (OS-specific). */
  volatile bool stop_requested;  /* Flag to request thread termination. */
  bool driver_started;           /* TRUE if the station lifecycle started the associated driver. */
  bool runner_started;           /* TRUE if runner threads are currently active. */
  runner_context_t *runner_context; /* Runner data (thread handles, etc). */
};

/**
 * @brief   Type of a HDLC station configuration.
 */
struct iohdlc_station_config {
  uint8_t  mode;          /**< @brief initial operational mode               */
  uint8_t  flags;         /**< @brief station flags: TWA, PRIMARY.           */
  uint8_t  log2mod;       /**< @brief log2mod, log2(modulus).                */
  uint32_t addr;          /**< @brief address of the station.                */
  ioHdlcDriver *driver;   /**< @brief the link driver interface implementor. */
  
  /* Frame pool auto-initialization from arena */
  void *frame_arena;      /**< @brief Arena memory for frame pool.           */
  size_t frame_arena_size;/**< @brief Arena size in bytes.                   */
  uint32_t max_info_len;  /**< @brief Max INFO field length (0=auto).        */
  uint8_t pool_watermark; /**< @brief Pool low watermark (0=auto: 10% min 8).*/
  uint8_t fff_type;       /**< @brief FFF type: 0=auto from optfuncs,
                                      1=TYPE0(max 127 bytes),
                                      2=TYPE1(max 4095 bytes).               */

  const uint8_t *optfuncs;/**< @brief optional functions array (5 bytes),
                                      NULL for defaults.                     */
  void *phydriver;        /**< @brief the physical driver used by the station*/
  void *phydriver_config; /**< @brief the physical driver configuration.     */
  uint16_t reply_timeout_ms; /**< @brief T1 reply timeout in ms. Must account
                                      for: (ks × frame_tx_time) + wire_RTT +
                                      peer_processing + margin.              */
  uint8_t poll_retry_max; /**< @brief max poll retries (0 = default 5)       */
};

/*===========================================================================*/
/* Module macros.                                                            */
/*===========================================================================*/

/**
 * @brief   Convert U-frame command to operational mode.
 * @details Maps IOHDLC_U_xxx commands to IOHDLC_OM_xxx mode constants.
 * @param   u_cmd  U-frame command code
 * @return  Operational mode, or 0 if command not a set-mode
 */
#define IOHDLC_UCMD_TO_MODE(u_cmd) \
  ((u_cmd) == IOHDLC_U_SNRM ? IOHDLC_OM_NRM : \
   (u_cmd) == IOHDLC_U_SARM ? IOHDLC_OM_ARM : \
   (u_cmd) == IOHDLC_U_SABM ? IOHDLC_OM_ABM : 0)

/**
 * @brief   Convert operational mode to U-frame command.
 * @details Maps IOHDLC_OM_xxx mode constants to IOHDLC_U_xxx commands.
 * @param   mode  Operational mode
 * @return  U-frame command code, or 0 if mode invalid
 */
#define IOHDLC_MODE_TO_UCMD(mode) \
  ((mode) == IOHDLC_OM_NRM ? IOHDLC_U_SNRM : \
   (mode) == IOHDLC_OM_ARM ? IOHDLC_U_SARM : \
   (mode) == IOHDLC_OM_ABM ? IOHDLC_U_SABM : 0)

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

  int32_t ioHdlcStationLinkUpEx(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr, 
                                uint8_t mode, eventmask_t evt_mask);

  int32_t ioHdlcStationLinkDownEx(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr,
                                  eventmask_t evt_mask);

  /* Convenience macros using default event mask (EVENT_MASK(31)). */
  /** @brief Convenience wrapper using the default application event mask. */
  #define ioHdlcStationLinkUp(s, addr, mode) \
    ioHdlcStationLinkUpEx(s, addr, mode, IOHDLC_APP_EVT_MASK_DEFAULT)
  /** @brief Convenience wrapper using the default application event mask. */
  #define ioHdlcStationLinkDown(s, addr) \
    ioHdlcStationLinkDownEx(s, addr, IOHDLC_APP_EVT_MASK_DEFAULT)

  ssize_t ioHdlcWriteTmo(iohdlc_station_peer_t *ioHdlcpeerp, const void *buf,
      size_t count, uint32_t timeout_ms);

  ssize_t ioHdlcReadTmo(iohdlc_station_peer_t *ioHdlcpeerp, void *buf,
      size_t count, uint32_t timeout_ms);

  /* Convenience macros for blocking operations (infinite timeout). */
  /** @brief Convenience wrapper for blocking write with infinite timeout. */
  #define ioHdlcWrite(peer, buf, count) ioHdlcWriteTmo(peer, buf, count, IOHDLC_WAIT_FOREVER)
  /** @brief Convenience wrapper for blocking read with infinite timeout. */
  #define ioHdlcRead(peer, buf, count) ioHdlcReadTmo(peer, buf, count, IOHDLC_WAIT_FOREVER)

  int32_t ioHdlcAddPeer(iohdlc_station_t *ioHdlcsp, iohdlc_station_peer_t *peer, uint32_t addr);
  int32_t ioHdlcPeerSetWindow(iohdlc_station_peer_t *peer, uint32_t ks, uint32_t kr);

  iohdlc_station_peer_t *ioHdlcAddr2peer(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr);

  /**
   * @brief   Tear down a station and stop its runtime components.
   * @details Force-stops the runner if active and stops the associated driver.
   *          This function is idempotent and is safe to call even after a
   *          partial teardown sequence.
   * @param[in] ioHdlcsp    station descriptor
   * @return                0 on success, -1 on invalid argument
   */
  int32_t ioHdlcStationDeinit(iohdlc_station_t *ioHdlcsp);

  int32_t ioHdlcStationInit(iohdlc_station_t *ioHdlcsp, const iohdlc_station_config_t *ioHdlcsconfp);
#ifdef __cplusplus
}
#endif

/** @} */

#endif /* IOHDLC_H_ */
