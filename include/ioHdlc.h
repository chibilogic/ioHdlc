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
 * @name    Operational and non-operational modes
 * @{
 */
#define IOHDLC_OM_ABM     0x01  /**< @brief Asynchronous balanced mode (ABM). */
#define IOHDLC_OM_ARM     0x02  /**< @brief Asynchronous response mode (ARM). */
#define IOHDLC_OM_NRM     0x03  /**< @brief Normal response mode (NRM). */
#define IOHDLC_OM_NDM     0x04  /**< @brief Normal disconnected mode (NDM). */
#define IOHDLC_OM_ADM     0x05  /**< @brief Asynchronous disconnected mode (ADM). */
#define IOHDLC_OM_IM      0x06  /**< @brief Initialization mode (IM). */
#define IOHDLC_OM_TWA     0x40  /**< @brief Two Way Alternate flag. */
#define IOHDLC_OM_PRI     0x80  /**< @brief Primary station flag. */
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
#define IOHDLC_OPT_FFF      (1U << 0) /**< @brief Option 22   - Frame format field. Type 0 supported. */

#define IOHDLC_OPT_INH_OCT  4         /* Bit 35 -> Octet nr.4 */
#define IOHDLC_OPT_INH      (1U << 2) /**< @brief Option 24   - Inhibit bit or octet insertion. */
/** @} */

/* pf_state definitions. */
#define IOHDLC_P_RCVED    0x01  /* P received and to acknowledge in the next frame to tx,
                                   or in the last frame if NRM. */
#define IOHDLC_F_RCVED    0x02  /* F received. */
#define IOHDLC_P_SENT     0x04  /* P sent and not acknowledged yet. */
#define IOHDLC_PF_INHB    0x80  /* P/F checkpoint inhibited. */

/* um_state definitions. */
#define IOHDLC_UM_SENT    0x01  /* Unnumbered command sent and not acknowledged yet. */
#define IOHDLC_UM_RCVED   0x02  /* Unnumbered command received and to acknowledged. */

/* ss_state definitions. */
#define IOHDLC_SS_BUSY    0x01  /* Busy state.
                                   Temporarily the peer cannot receive I-frames. */
#define IOHDLC_SS_RNR_RCV 0x02  /* RNR received from the peer. */
#define IOHDLC_SS_RNR_SNT 0x04  /* RNR sent to the peer. */
#define IOHDLC_SS_RPL_STT 0x08  /* Reply timer has started. */
#define IOHDLC_SS_ST_CONN 0x80  /* Peer connected. */

/* support macros */
#define IOHDLC_IS_SEC(s)      (!((s)->mode & IOHDLC_OM_PRI))
#define IOHDLC_IS_PRI(s)      ((s)->mode & IOHDLC_OM_PRI)
#define IOHDLC_IS_DISC(s)     ((((s)->mode & 0x0F) == IOHDLC_OM_NDM) || \
                               (((s)->mode & 0x0F) == IOHDLC_OM_ADM))
#define IOHDLC_IS_NRM(s)      (((s)->mode & 0x0F) == IOHDLC_OM_NRM)
#define IOHDLC_IS_ABM(s)      (((s)->mode & 0x0F) == IOHDLC_OM_ABM)
#define IOHDLC_PEER_DISC(p)   (!((p)->ss_state & IOHDLC_SS_ST_CONN))
#define IOHDLC_HAS_FFF(s)     (s->optfuncs[IOHDLC_OPT_FFF_OCT] & IOHDLC_OPT_FFF)
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
#define EVT_CM_RPLYTMO    0x01  /* A reply timer has timed out. */
#define EVT_CM_UMRECVD    0x02  /* An UM command have been received. */
#define EVT_CM_CONNCHG    0x04  /* An connection state has changed. */
#define EVT_CM_CONNSTR    0x08  /* Connection start has requested. */

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
                                   Note the invariant (vs-nr) % (ks+1) = len(i_retrans_q) */
  uint32_t  rej_actioned;       /* a value x != 0 of this field indicates that a
                                   REJ exception with N(R) = x-1 is in action. The receipt
                                   of a I-frame with N(S) = x-1 clears the exception. */
  uint32_t  vs_atlast_pf;       /* V(S) at the time of transmission of the last
                                   frame with the P bit set in case of primary/combined station
                                   or with the F bit set in case of secondary station. */
  uint8_t   pf_state;           /* P/F sent/received state. See definitions. */
  uint8_t   um_state;           /* Unnumbered state. See definitions. */
  uint8_t   ss_state;           /* Supervision state. See definitions. */
  uint8_t   um_cmd;             /* Unnumbered command to_send/sent. */

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

  /* virtual timers. */
  iohdlc_virtual_timer_t reply_tmo;   /* Primary/combined station command reply time-out
                                         and/or primary/secondary/combined station I frame
                                         reply time-out. */

};

/**
 * @brief   Type of a HDLC station descriptor.
 */
struct iohdlc_station {

  /* configuration parameters. */
  uint8_t   mode;               /* Operational mode of this station. */
  uint8_t   modulus;            /* Modulus, expressed as log2 modulus. (3, 7, 15, 31). */
  uint8_t   pfoctet;            /* P/F octet number. Calculated from modulus. (0, 1, 2, 4). */
  uint8_t   optfuncs[5];        /* Active HDLC optional functions among those supported.
                                   See ISO13239 Table 16. */
  uint32_t  addr;               /* Address of the station. */


  /* state, peers, pool and queues. */
  int32_t   errorno;            /* number of last error. Follows the posix list of values. */
  iohdlc_peer_list_t  peers;    /* The header of the list of the peers of this station. Stations
                                   in ABM mode and secondary stations have only one peer. */
  iohdlc_frame_q_t  ni_trans_q; /* S-frame and U-frame transmission queue. Common to all peers.
                                   Maybe unnecessary. */
  iohdlc_frame_q_t  ni_recept_q;/* S-frame and U-frame reception queue. Common to all peers.
                                   Maybe unnecessary. */
  ioHdlcFramePool *frame_pool;  /* Pool of free frames. Any station has a its own pool of frames
                                   to be used for any frame type, and for transmission
                                   and reception. The pool shall be dimensioned in order to satisfy
                                   the windows size and the reception buffering of all the
                                   peers. */

  /* link driver. */
  ioHdlcDriver *driver;         /* Data link driver the station operates on. */

  /* events. */
  iohdlc_event_source_t cm_es;  /* Source of the events related to commands. */
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
  int32_t ioHdlcStationLinkUp(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr);
  int32_t ioHdlcStationLinkDown(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr);
  int32_t ioHdlcWrite(iohdlc_station_peer_t *ioHdlcpeerp, const void *buf, size_t count);
  int32_t ioHdlcRead(iohdlc_station_peer_t *ioHdlcpeerp, void *buf, size_t count);
  int32_t ioHdlcAddPeer(iohdlc_station_t *ioHdlcsp, iohdlc_station_peer_t *peer, uint32_t addr, uint32_t mifl);
  iohdlc_station_peer_t *addr2peer(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr);
  void ioHdlcStationInit(iohdlc_station_t *ioHdlcsp, uint32_t modulus, uint8_t mode,
      uint32_t addr, ioHdlcDriver *driver, ioHdlcFramePool *fpp);
#ifdef __cplusplus
}
#endif

/*===========================================================================*/
/* Module inline functions.                                                  */
/*===========================================================================*/

#endif /* IOHDLC_H_ */

/** @} */
