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
 * @file    include/ioHdlc.h
 * @brief   HDLC definition and types header.
 * @details
 *
 * @addtogroup hdlc
 * @{
 */

#ifndef IOHDLC_H_
#define IOHDLC_H_

#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcdriver.h"
#include "ioHdlcqueue.h"
#include "ioHdlcosal.h"

/*===========================================================================*/
/* Module constants.                                                         */
/*===========================================================================*/

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

#define IOHDLC_IS_I_FRM(ctrl)     (((ctrl) & IOHDLC_I_MASK) == IOHDLC_I_ID)
#define IOHDLC_IS_S_FRM(ctrl)     (((ctrl) & IOHDLC_S_MASK) == IOHDLC_S_ID)
#define IOHDLC_IS_U_FRM(ctrl)     (((ctrl) & IOHDLC_U_MASK) == IOHDLC_U_ID)

/* S-frame functions.
   The SS bits are low to hi, i.e. S bits = 01 means 2 decimal.*/
#define IOHDLC_S_FUN_MASK 0x0C
#define IOHDLC_S_RR       0x00  /**< @brief Receive ready, RR, (S bits = 00) command and response. */
#define IOHDLC_S_REJ      0x08  /**< @brief Reject, REJ, (S bits = 01) command and response. */
#define IOHDLC_S_RNR      0x04  /**< @brief Receive not ready, RNR, (S bits = 10) command and response. */
#define IOHDLC_S_SREJ     0x0C  /**< @brief Selective reject, SREJ, (S bits = 11) command and response. */

/* U-frame functions. */
#define IOHDLC_U_FUN_MASK 0xEC
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

#define IOHDLC_PF_BIT     0x10  /* Command (poll) / Response (final) bit in control field modulo 8,
                                   first octet. */
#define IOHDLC_PFx_BIT    0x01  /* Command (poll) / Response (final) bit in control field
                                   modulo != 8, non first octet. */

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
#define IOHDLC_FLG_BUSY   0x08  /**< @brief Busy station flag. */

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
 * @name    Time-critical option flags
 * @{
 * @brief   Fast-access flags for performance-critical code paths.
 *          Mirrors selected bits from optfuncs[] for zero-branch access.
 */
#define IOHDLC_CFLG_FFF   0x01  /**< @brief Frame Format Field present (Option 22). */
#define IOHDLC_CFLG_REJ   0x02  /**< @brief REJ recovery supported (Option 2). */
#define IOHDLC_CFLG_STB   0x04  /**< @brief Start/stop with basic transparency (Option 15.1). */
/** @} */

/* pf_state definitions. */
#define IOHDLC_P_RCVED    0x01  /* Secondary: P received and to acknowledge in the
                                   next frame to tx, or in the last frame if NRM. */
#define IOHDLC_F_RCVED    0x02  /* Primary: F received in response to a sent P. If
                                   not set, P was sent. It's set at reset.*/
#define IOHDLC_P_SLCIT    0x04  /* Primary: Solicit response.*/
#define IOHDLC_PF_INHB    0x80  /* P/F checkpoint inhibited. slcit */

/* um_state definitions. */
#define IOHDLC_UM_SENT    0x01  /* Unnumbered command sent and not acknowledged yet. */
#define IOHDLC_UM_RCVED   0x02  /* Unnumbered command received and to acknowledge. */
#define IOHDLC_UM_SENDING 0x04  /* Unnumbered command pending transmission. */

/* ss_state definitions. */
#define IOHDLC_SS_BUSY    0x01  /* Busy state.
                                   Temporarily the peer cannot receive I-frames. */
#define IOHDLC_SS_SENDING 0x02  /* An S-frame is being sent to the peer. */                                     
//#define IOHDLC_SS_RNR_RCV 0x04  /* RNR received from the peer. */
//#define IOHDLC_SS_RNR_SNT 0x08  /* RNR sent to the peer. */
#define IOHDLC_SS_ST_DISM 0x40  /* Peer in disconnected mode (DM received). */
#define IOHDLC_SS_ST_CONN 0x80  /* Peer connected. */

/* helper macros */
#define IOHDLC_IS_SEC(s)      (!((s)->flags & IOHDLC_FLG_PRI))
#define IOHDLC_IS_PRI(s)      ((s)->flags & IOHDLC_FLG_PRI)
#define IOHDLC_IS_DISC(s)     (((s)->mode == IOHDLC_OM_NDM) || \
                               ((s)->mode == IOHDLC_OM_ADM))
#define IOHDLC_IS_NRM(s)      ((s)->mode == IOHDLC_OM_NRM)
#define IOHDLC_IS_NDM(s)      ((s)->mode == IOHDLC_OM_NDM)
#define IOHDLC_IS_ABM(s)      ((s)->mode == IOHDLC_OM_ABM)
#define IOHDLC_IS_ARM(s)      ((s)->mode == IOHDLC_OM_ARM)
#define IOHDLC_IS_ADM(s)      ((s)->mode == IOHDLC_OM_ADM)
#define IOHDLC_P_ISRCVED(s)   ((s)->pf_state & IOHDLC_P_RCVED)
#define IOHDLC_F_ISRCVED(s)   ((s)->pf_state & IOHDLC_F_RCVED)
#define IOHDLC_P_ISFLYING(s)  ((IOHDLC_IS_PRI(s) && !IOHDLC_F_ISRCVED(s)) || \
                               (IOHDLC_IS_SEC(s) && !IOHDLC_P_ISRCVED(s)))
#define IOHDLC_ACK_P(s)       ((s)->pf_state &= ~IOHDLC_P_RCVED)
#define IOHDLC_ACK_F(s)       ((s)->pf_state &= ~IOHDLC_F_RCVED)
#define IOHDLC_HAS_FFF(s)     ((s)->flags_critical & IOHDLC_CFLG_FFF)
#define IOHDLC_USE_TWA(s)     ((s)->flags & IOHDLC_FLG_TWA)
#define IOHDLC_ST_IDLE(s)     ((s)->flags & IOHDLC_FLG_IDL)
#define IOHDLC_ST_BUSY(s)     ((s)->flags & IOHDLC_FLG_BUSY)
#define IOHDLC_UM_INPROG(p)   ((p)->um_state & (IOHDLC_UM_SENT|IOHDLC_UM_SENDING))
#define IOHDLC_PEER_DISC(p)   (!((p)->ss_state & IOHDLC_SS_ST_CONN))
#define IOHDLC_PEER_BUSY(p)   (((p)->ss_state & IOHDLC_SS_BUSY))

/**
 * @name    System-defined parameters
 * @{
 */
#define IOHDLC_DFL_I_SIZE       64
#define IOHDLC_DFL_MODULUS      8
/** @} */

/**
 * @brief     Event flags
 */

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
  uint32_t  addr;               /* Address of the peer. 0 if is not determined yet. */
  uint32_t  ks;                 /* Window size k, transmit. Max modulus - 1. */
  uint32_t  kr;                 /* Window size k, receive. Max modulus - 1. */
  uint32_t  mifls;              /* Maximum information field length, transmit. */
  uint32_t  miflr;              /* Maximum information field length, receive. */

  /* state variables. */
  uint32_t  vs;                 /* Send state variable V(S). */
  uint32_t  vr;                 /* Receive state variable V(R). */
  uint32_t  nr;                 /* Last N(R) received and accepted.
                                   Invariant: (vs-nr) & modmask = len(i_retrans_q), must be ≤ ks. */
  uint32_t  rej_actioned;       /* a value x != 0 of this field indicates that a
                                   REJ exception with N(R) = x-1 is in action. The receipt
                                   of a I-frame with N(S) = x-1 clears the exception. */
  uint32_t  chkpt_actioned;     /* a value x != 0 indicates checkpoint retransmission
                                   started with first frame N(S) = x-1. Used to inhibit
                                   REJ if requesting same particular I frame (ISO 13239). */
  uint32_t  vs_atlast_pf;       /* V(S) at the time of transmission of the last
                                   frame with the P bit set in case of primary/combined station
                                   or with the F bit set in case of secondary station. */
  uint32_t  i_pending_count;    /* Total pending I-frames: len(i_trans_q) + len(i_retrans_q).
                                   Incremented when adding to i_trans_q, decremented when
                                   removing from i_retrans_q. Maintained for O(1) flow control. */
  uint8_t   um_state;           /* Unnumbered state. See definitions. */
  uint8_t   ss_state;           /* Supervision state. See definitions. */
  uint8_t   um_cmd;             /* Unnumbered command to_send. */
  uint8_t   um_rsp;             /* Unnumbered response. */
  uint8_t   ss_fun;             /* Supervision function to send. */
  uint32_t  ss_nr;              /* N(R) in the S frame to send, rcvd, sent. */
  uint8_t   ss_fsent;           /* Last supervision function sent. TODO: Is it necessary?*/

  /* data queues. */
  iohdlc_frame_q_t i_retrans_q; /* I-frame retransmission queue. No more than ks frames
                                   will be in this queue.*/
  iohdlc_frame_q_t i_recept_q;  /* I-frame reception queue. Space shall be available for
                                   at least kr+1 frames. A limit > kr will be set to send
                                   a RNR S-frame. */
                                /* Letta dalla read applicativa, scritta dal task di ricezione,
                                   che dunque non si blocca sulla coda.
                                   Usare semaforo? */
  iohdlc_frame_q_t i_trans_q;   /* I-frame transmission queue.
                                   The frames in this queue have address and N(S) defined, but
                                   not N(R) nor P/F. The latter two will be set when the frame
                                   will be picked for actual transmission.
                                   No more than ks frames will be in this queue. */

  /* flow control. */
  iohdlc_binary_semaphore_t tx_sem;  /* TX flow control semaphore.
                                        Blocks app when len(i_retrans_q) + len(i_trans_q) >= 2*ks
                                        or pool is LOW_WATER. Signaled when space becomes available. */

  /* virtual timers. */
  iohdlc_virtual_timer_t reply_tmr;   /* Primary/combined station command reply time-out
                                         timer. */
  iohdlc_virtual_timer_t i_reply_tmr; /* Primary/secondary/combined station I-frame reply
                                         time-out timer. */

  /* retry counters. */
  uint8_t   poll_retry_count;         /* Current number of retries for frames with P=1 
                                         (poll bit). Incremented on reply_tmr expiry,
                                         reset when F=1 response received. */
  uint8_t   poll_retry_max;           /* Maximum number of retries allowed for poll frames.
                                         When poll_retry_count >= poll_retry_max, the link
                                         is considered down. */

};

/**
 * @brief   Type of a HDLC station descriptor.
 */
struct iohdlc_station {

  /* configuration parameters. */
  uint8_t   mode;               /* Operational mode of this station. */
  uint8_t   flags;              /* Station flags: TWA, PRIMARY, IDLE, BUSY. */
  uint8_t   pf_state;           /* P/F sent/received state. See definitions. */
  uint32_t  modmask;            /* Modulus bit mask: 7 for mod 8, 127 for mod 128, 32767 for mod 32768, 2147483647 for mod 2^31. */
  uint8_t   pfoctet;            /* P/F octet number. Calculated from modulus. (0, 1, 2, 4). */
  uint8_t   ctrl_size;          /* Control field size in bytes (1, 2, 4, 8). Precalculated
                                   from modulus for fast frame field access. */
  uint8_t   frame_offset;       /* Precalculated FFF offset: 0 if no FFF, 1 if FFF present.
                                   Used for fast frame field access without runtime checks. */
  uint8_t   flags_critical;     /* Time-critical option flags (FFF, REJ, STB) for fast access. */
  uint8_t   optfuncs[5];        /* Active HDLC optional functions among those supported.
                                   See ISO13239 Table 16. Maintains ISO format for XID. */
  uint16_t  reply_timeout_ms;   /* Reply timer timeout value in milliseconds. Default: 100ms. */
  uint32_t  addr;               /* Address of the station. */
  iohdlc_station_peer_t *c_peer;    /* The peer the station is currently talking to. */
  iohdlc_station_peer_t *arm_peer;  /* The peer currently in arm mode, if any. */

  /* state, peers, pool and queues. */
  int32_t   errorno;            /* number of last error. Follows the posix list of values. */
  iohdlc_peer_list_t  peers;    /* The header of the list of the peers of this station. Stations
                                   in ABM mode and secondary stations have only one peer. */
  ioHdlcFramePool *frame_pool;  /* Pool of free frames. Any station has a its own pool of frames
                                   to be used for any frame type, and for transmission
                                   and reception. The pool shall be dimensioned in order to satisfy
                                   the windows size and the reception buffering of all the
                                   peers. */
  iohdlc_tx_fn_t tx_fn;         /* Active transmit handler for the current mode. */
  iohdlc_rx_fn_t rx_fn;         /* Active receiver handler for the current mode. */

  /* link driver. */
  ioHdlcDriver *driver;         /* Data link driver the station operates on. */

  /* events. */
  iohdlc_event_source_t cm_es;  /* Source of the events related to commands. */
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
  ioHdlcFramePool *fpp;   /**< @brief the frame pool used by the station.    */
  void *phydriver;        /**< @brief the physical driver used by the implementor. */
  void *phydriver_config; /**< @brief the physical driver configuration.     */
};

/*===========================================================================*/
/* Module macros.                                                            */
/*===========================================================================*/

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif
  int32_t ioHdlcStationLinkUp(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr, uint8_t mode);
  int32_t ioHdlcStationLinkDown(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr);
  int32_t ioHdlcWrite(iohdlc_station_peer_t *ioHdlcpeerp, const void *buf, size_t count);
  int32_t ioHdlcRead(iohdlc_station_peer_t *ioHdlcpeerp, void *buf, size_t count);
  int32_t ioHdlcAddPeer(iohdlc_station_t *ioHdlcsp, iohdlc_station_peer_t *peer, uint32_t addr, uint32_t mifl);
  iohdlc_station_peer_t *addr2peer(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr);
  int32_t ioHdlcStationInit(iohdlc_station_t *ioHdlcsp, const iohdlc_station_config_t *ioHdlcsconfp);
  //int32_t ioHdlcStationInit(iohdlc_station_t *ioHdlcsp, const iohdlc_station_config_t *ioHdlcsconfp);
#if 0
  int32_t ioHdlcStationInit(iohdlc_station_t *ioHdlcsp, uint32_t modulus, uint8_t mode,
      uint32_t addr, ioHdlcDriver *driver, ioHdlcFramePool *fpp);
#endif
#ifdef __cplusplus
}
#endif

/*===========================================================================*/
/* Module inline functions.                                                  */
/*===========================================================================*/

#endif /* IOHDLC_H_ */

/** @} */
