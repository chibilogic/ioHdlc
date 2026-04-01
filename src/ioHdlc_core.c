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
 * @file    ioHdlc_core.c
 * @brief   ISO 13239 HDLC protocol core implementation.
 * @details Implements HDLC station core functionality including:
 *          - NRM (Normal Response Mode)
 *          - I-frame, S-frame, and U-frame handling
 *          - Sequence number validation (N(S), N(R))
 *          - Checkpoint retransmission (ISO 13239 5.6.2.1)
 *          - REJ exception handling with overlap detection
 *          - Support Frame Format Field TYPE0 and TYPE1
 *
 *          This module owns the protocol-state transitions for peers once
 *          frames have been admitted by the driver layer. It consumes and
 *          produces frame references under the ownership rules established by
 *          the station frame pool and uses runner events/timers as deferred
 *          execution triggers.
 *
 * @addtogroup ioHdlc_core
 * @{
 */

#include "ioHdlc_core.h"
#include "ioHdlc.h"
#include "ioHdlc_app_events.h"
#include "ioHdlc_log.h"
#include "ioHdlclist.h"
#include "ioHdlcosal.h"
#include <errno.h>

/* Forward declarations for U-frame handler */
static void handleUFrame(iohdlc_station_t *s, iohdlc_frame_t *fp);

/*===========================================================================*/
/* Module local definitions.                                                 */
/*===========================================================================*/

/* Exponential backoff for reply timer */
#define IOHDLC_TIMER_BACKOFF(s,p) \
          (s->reply_timeout_ms << (p)->poll_retry_count)

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

static void setModeFunctions(iohdlc_station_t *s, uint8_t mode);

/**
 * @brief   Mark a peer as disconnected and wake blocked transmitters.
 * @param[in] p   Peer state to update.
 */
static void ioHdlcSetDisconnected(iohdlc_station_peer_t *p) {
  if (p->ss_state & IOHDLC_SS_ST_CONN) {
    p->stationp->connected_count--;
  }
  p->ss_state &= ~IOHDLC_SS_ST_CONN;
  iohdlc_condvar_broadcast(&p->tx_cv);
}

/**
 * @brief   Mark a peer as connected and wake blocked transmitters.
 * @param[in] p   Peer state to update.
 */
static void ioHdlcSetConnected(iohdlc_station_peer_t *p) {
  if (!(p->ss_state & IOHDLC_SS_ST_CONN)) {
    p->stationp->connected_count++;
  }
  p->ss_state |= IOHDLC_SS_ST_CONN;
  iohdlc_condvar_broadcast(&p->tx_cv);
}

/**
 * @brief   Drain a frame queue and release every frame back to the pool.
 * @details Used during peer reset paths so retransmission, receive, and
 *          pending-transmit queues cannot leak frame references across state
 *          transitions.
 * @param[in] p   Owning peer state.
 * @param[in] q   Queue to drain.
 */
static void clearFrameQ(iohdlc_station_peer_t *p, iohdlc_frame_q_t *q) {
  while (!ioHdlc_frameq_isempty(q)) {
    iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(q);
    iohdlc_frame_t *fp = IOHDLC_FRAME_FROM_Q(qh);
    hdlcReleaseFrame(&p->stationp->frame_pool, fp);
  }
}

/**
 * @brief   Handle timeout retry logic and declare link down if needed.
 * @details Increments retry counter and checks against max limit.
 *          If limit exceeded, declares link down, stops timers, resets state,
 *          and marks peer as disconnected.
 * 
 * @param[in] s     Station descriptor
 * @param[in] p     Peer descriptor
 * @return  true to retry (counter < max), false if link down (max retries exceeded)
 */
static bool handleTimeoutRetry(iohdlc_station_t *s, iohdlc_station_peer_t *p) {
  p->poll_retry_count++;
#if defined(IOHDLC_ENABLE_STATISTICS)
  p->stats.timeouts++;
#endif
  
  if (p->poll_retry_count >= p->poll_retry_max) {
    /* Max retries exceeded: declare link down. */
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_DOWN);
    
    /* Cleanup: stop timers, reset counters, clear U-frame state. */
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_T3);
    p->poll_retry_count = 0;
    p->um_state &= ~(IOHDLC_UM_SENT);
    
    /* Mark peer disconnected (blocks further transmissions). */
    ioHdlcSetDisconnected(p);

    /* Enter disconnected mode if no other peer is still connected. */
    if (s->connected_count == 0) {
      s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
      setModeFunctions(s, s->mode);
    }

    return false;  /* Link down, do not retry. */
  }
  
  return true;  /* Retry allowed. */
}

/**
 * @brief   Reset unnumbered-frame negotiation state for a peer.
 * @param[in] p   Peer state to reset.
 */
static void resetPeerUm(iohdlc_station_peer_t *p) {
  p->um_state = 0;
  p->um_cmd = 0;
}

/**
 * @brief   Reset peer protocol state, timers, and owned queues.
 * @details This is the heavy-weight reset path used after mode changes,
 *          accepted disconnects, and similar protocol boundary transitions.
 * @param[in] p   Peer state to reset.
 */
static void resetPeerVars(iohdlc_station_peer_t *p) {
  ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
  ioHdlcStopReplyTimer(p, IOHDLC_TIMER_T3);
  p->nr = p->vr = p->vs = p->vs_highest = 0;
  p->ss_state = 0;
  p->poll_retry_count = 0;
  p->i_pending_count = 0;
  p->frmr_condition = false;
  clearFrameQ(p, &p->i_retrans_q);
  clearFrameQ(p, &p->i_recept_q);
  clearFrameQ(p, &p->i_trans_q);
  iohdlc_sem_init(&p->i_recept_sem, 0);
}

/**
 * @brief   Set FRMR exception condition on a peer.
 * @details Records the rejected frame's control field and reason, then
 *          triggers the TX path to send FRMR at the next opportunity.
 *
 * @param[in] s             Station descriptor.
 * @param[in] p             Peer state.
 * @param[in] rejected_ctrl Pointer to the control byte(s) of the rejected frame.
 * @param[in] ctrl_size     Number of control bytes (1 for mod 8, 2 for mod 128).
 * @param[in] is_command    true if the rejected frame was a command.
 * @param[in] reason        IOHDLC_FRMR_W/X/Y/Z reason bits.
 */
static void setFrmrCondition(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                              const uint8_t *rejected_ctrl, uint8_t ctrl_size,
                              bool is_command, uint8_t reason) {
  p->frmr_condition = true;
  p->frmr_rejected_ctrl[0] = rejected_ctrl[0];
  p->frmr_rejected_ctrl[1] = (ctrl_size > 1) ? rejected_ctrl[1] : 0;
  p->frmr_reason = reason;
  p->frmr_cr = is_command;

  /* Trigger TX to send FRMR response. */
  p->um_rsp = IOHDLC_U_FRMR;
  p->um_state |= IOHDLC_UM_RCVED;
  ioHdlcBroadcastFlags(s, IOHDLC_EVT_UM_RECVD);
}

/**
 * @brief   Build FRMR response frame with information field.
 * @details Encodes the rejected control field, current V(S)/V(R), C/R bit,
 *          and W/X/Y/Z reason bits per ISO 13239, 5.5.3.1.
 *
 * @param[in] s       Station descriptor.
 * @param[in] p       Peer state (V(S), V(R) taken at TX time).
 * @param[in] fp      Frame to build into.
 * @param[in] set_pf  true to set the P/F bit.
 */
static void buildFrmrResponse(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                               iohdlc_frame_t *fp, bool set_pf) {
  /* Address: response uses station address. */
  IOHDLC_FRAME_ADDR(s, fp) = s->addr;

  /* Control: FRMR U-frame with optional F bit.
     U-frame control is always 1 byte, regardless of modulo. */
  IOHDLC_FRAME_CTRL(s, fp, 0) = IOHDLC_U_FRMR | IOHDLC_U_ID;
  if (set_pf)
    IOHDLC_FRAME_CTRL(s, fp, 0) |= IOHDLC_PF_BIT;

  /* Information field starts after FFF + addr(1) + ctrl(1). */
  uint8_t *info = &fp->frame[s->frame_offset + 2];
  uint8_t info_len;

  if (s->ctrl_size == 1) {
    /* Modulo 8: 3-byte info field (ISO 13239, 5.5.3.1).
       Byte 0: rejected control field
       Byte 1: [V(S) bits 2-0] [C/R] [V(R) bits 2-0] [0]
       Byte 2: [0] [0] [0] [0] [W] [X] [Y] [Z] */
    info[0] = p->frmr_rejected_ctrl[0];
    info[1] = (uint8_t)(((p->vs & 0x07) << 1) |
                         (p->frmr_cr ? 0x10 : 0) |
                         ((p->vr & 0x07) << 5));
    info[2] = p->frmr_reason;
    info_len = 3;
  } else {
    /* Modulo 128: 5-byte info field (ISO 13239, 5.5.3.1).
       Byte 0-1: rejected control field (2 bytes)
       Byte 2:   [V(S) bits 6-0] [0]
       Byte 3:   [V(R) bits 6-0] [C/R]
       Byte 4:   [0] [0] [0] [0] [W] [X] [Y] [Z] */
    info[0] = p->frmr_rejected_ctrl[0];
    info[1] = p->frmr_rejected_ctrl[1];
    info[2] = (uint8_t)((p->vs & 0x7F) << 1);
    info[3] = (uint8_t)(((p->vr & 0x7F) << 1) | (p->frmr_cr ? 0x01 : 0));
    info[4] = p->frmr_reason;
    info_len = 5;
  }

  /* elen: FFF + addr(1) + ctrl(1) + info */
  fp->elen = (uint16_t)(s->frame_offset + 2 + info_len);
}

/**
 * @brief   Check whether a U-frame opcode requests a connection mode change.
 * @param[in] u_cmd   U-frame opcode to classify.
 * @return  true if the opcode is one of the supported connect-mode commands.
 */
static bool isConnectionUCommand(uint8_t u_cmd) {
  return (u_cmd == IOHDLC_U_SNRM) || (u_cmd == IOHDLC_U_SABM);
}

/**
 * @brief   Bind mode-specific TX/RX handlers on the station object.
 * @param[in] s      Station descriptor.
 * @param[in] mode   Operating mode to bind.
 */
static void setModeFunctions(iohdlc_station_t *s, uint8_t mode) {
  if (mode == IOHDLC_OM_NRM) {
    s->tx_fn = ioHdlcNrmTx;
    s->rx_fn = ioHdlcNrmRx;
  } else if (mode == IOHDLC_OM_ABM) {
    s->tx_fn = ioHdlcAbmTx;
    s->rx_fn = ioHdlcAbmRx;
    s->flags |= IOHDLC_FLG_PRI;
    s->pf_state |= IOHDLC_F_RCVED;  /* Combined station: free to poll. */
  } else {
    /* Disconnected modes (NDM, ADM): reset to NULL */
    s->tx_fn = NULL;
    s->rx_fn = NULL;
  }
}

/**
 * @brief   Check whether NRM rules currently allow transmission.
 * @note    In NRM, a send opportunity exists:
 *          - Primary TWA: when F has been received and no poll is outstanding.
 *          - Primary TWS: always permitted.
 *          - Secondary TWA/TWS: when P has been received.
 * @param[in] s   Station descriptor.
 * @return  true if the station can transmit under current P/F rules.
 */
static bool nrmSendOpportunity(iohdlc_station_t *s) {
  return IOHDLC_IS_PRI(s) ?
      (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) : true) :
      IOHDLC_P_ISRCVED(s);
}

/**
 * @brief   Check whether asynchronous modes currently allow transmission.
 * @note    In asynchronous modes:
 *          - TWA: transmission is allowed when line-idle state is detected.
 *          - TWS: transmission is always permitted.
 * @param[in] s   Station descriptor.
 * @return  true if the station can transmit.
 */
static bool abmSendOpportunity(iohdlc_station_t *s) {
  return !IOHDLC_USE_TWA(s) || IOHDLC_ST_IDLE(s);
}

/**
 * @brief   Dispatch send-opportunity evaluation to the current mode.
 * @param[in] s   Station descriptor.
 * @return  true if the active operating mode permits transmission.
 */
static bool sendOpportunity(iohdlc_station_t *s) {
  return IOHDLC_IS_NRM(s) ? nrmSendOpportunity(s) : abmSendOpportunity(s);
}

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Peer round-robin helper.
 * @details Advances @p s->c_peer to the next peer in the circular list
 *          that matches the requested filter, then returns it.
 *          If the list is empty returns NULL and leaves @p c_peer unchanged.
 *          If no matching peer is found after a full traversal, @p c_peer
 *          is left on the starting peer and that peer is returned.
 * @param[in] s                 Station descriptor.
 * @param[in] find_pending_only Filter selector:
 *            - @c true  — accept only disconnected peers that have a pending
 *                         @p um_cmd.
 *            - @c false — accept any connected peer, or any disconnected peer
 *                         that has a pending @p um_cmd (normal round-robin).
 * @return  Next matching peer, or NULL if the list is empty.
 */
static iohdlc_station_peer_t *ioHdlcNextPeer(iohdlc_station_t *s,
                                             bool find_pending_only) {
  IOHDLC_ASSERT(s != NULL, "ioHdlcNextPeer: station is NULL");
  iohdlc_station_peer_t *head = (iohdlc_station_peer_t *)&s->peers;

  if (ioHdlc_peerl_isempty((const iohdlc_peer_list_t *)head))
    return NULL;

  iohdlc_station_peer_t *start = s->c_peer;
  do {
    s->c_peer = s->c_peer->next;
    if (s->c_peer == head)
      s->c_peer = head->next;

    if (find_pending_only) {
      /* Accept: disconnected peer with pending um_cmd only. */
      if (IOHDLC_PEER_DISC(s->c_peer) && s->c_peer->um_cmd != 0)
        return s->c_peer;
    } else {
      /* Accept: connected peer, or disconnected peer with pending um_cmd. */
      if (!IOHDLC_PEER_DISC(s->c_peer) || s->c_peer->um_cmd != 0)
        return s->c_peer;
    }

  } while (s->c_peer != start);

  return s->c_peer;  /* No matching peer found: stay on current. */
}

/**
 * @brief   Process an unnumbered frame in the context of the current peer.
 * @details Handles both command and response U-frames, including connection
 *          establishment, disconnect handling, and reply-timer completion.
 * @param[in] s    Station descriptor.
 * @param[in] fp   Received U-frame.
 */
static void handleUFrame(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Handle U-frames common to all modes (NRM, ABM, ARM, etc.).
     Per ISO 13239, currently supports:
     - Commands (SNRM, SARM, SABM, DISC): processed by secondary/combined
     - Responses (UA, DM, FRMR): processed by primary/combined
  */
  const uint8_t addr = IOHDLC_FRAME_ADDR(s, fp);
  const uint8_t ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);
  const uint8_t u_cmd = ctrl & IOHDLC_U_FUN_MASK;
  const bool has_pf = IOHDLC_FRAME_GET_PF(s, fp);
  
  iohdlc_station_peer_t *p = s->c_peer;
  
  /* Determine if frame is command or response.
     Command: addressed to this station (addr == s->addr)
     Response: from current peer (addr == p->addr) */
  const bool is_command = (addr == s->addr);
  
  /* Validate frame origin matches current peer. */
  if (!is_command) {
    /* Response from peer: must be from c_peer. */
    if (!p || addr != p->addr) {
      /* Spurious frame from wrong peer or no peer selected → discard. */
      hdlcReleaseFrame(&s->frame_pool, fp);
      return;
    }
  }
  
  iohdlc_mutex_lock(&p->state_mutex);
  if (is_command) {
    /* U-frame command received (we are Secondary or Combined in ABM).
       Per 5.5.3.3: if already UM_RCVED in progress, ignore additional
       commands until response sent -- UNLESS this is a mode setting
       command or DISC during FRMR condition, which must be accepted
       to allow recovery (ISO 13239, 5.5.3). */
    if ((p->um_state & IOHDLC_UM_RCVED) || p->frmr_condition) {
      if (p->frmr_condition &&
          (isConnectionUCommand(u_cmd) || u_cmd == IOHDLC_U_DISC)) {
        /* Mode setting or DISC clears FRMR condition: accept the command. */
        p->frmr_condition = false;
        p->um_state &= ~IOHDLC_UM_RCVED;
      } else {
        hdlcReleaseFrame(&s->frame_pool, fp);
        iohdlc_mutex_unlock(&p->state_mutex);
        return;
      }
    }
    
    /* Register command and P bit. */
    p->um_cmd = u_cmd;
    if (has_pf)
      s->pf_state |= IOHDLC_P_RCVED;
    
    /* Determine appropriate response based on command and current state.
       Per 6.11.4.1.1: Secondary validates command and decides response. */    
    uint8_t om = IOHDLC_UCMD_TO_MODE(u_cmd);
    if (om) {
      /* Set mode command (SNRM/SARM/SABM).
         Per 6.11.4.1.1: Secondary accepts, changes mode, resets variables. */
      s->mode = om;
      setModeFunctions(s, om);
      resetPeerVars(p);
      ioHdlcSetConnected(p);
      p->um_rsp = IOHDLC_U_UA;
      
    } else if (u_cmd == IOHDLC_U_DISC) {
      /* DISC command received. */
      if (IOHDLC_IS_DISC(s)) {
        /* Already disconnected: respond with DM. */
        p->um_rsp = IOHDLC_U_DM;
      } else {
        /* Connected: accept DISC, enter disconnected mode.
           The TX will complete the disconnect operations. */
        p->um_rsp = IOHDLC_U_UA;
      }
      
    } else {
      /* Unsupported or unimplemented command.
         Per 6.11.4.1.3: respond with DM. */
      p->um_rsp = IOHDLC_U_DM;
    }
    
    /* Mark command received and notify TX to send response. */
    p->um_state |= IOHDLC_UM_RCVED;
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_UM_RECVD);
    
  } else {
    /* U-frame response received (we are Primary).
       Per 5.5.3.3: responses must match P bit. */
    
    /* Verify that a UM command is outstanding (UM_SENT)
       and that the F bit matches the P bit of the transmitted command. */
    if (!(p->um_state & IOHDLC_UM_SENT) || (has_pf ^ IOHDLC_P_SENT(s))) {
      /* Unsolicited or mismatched response -> discard. */
      hdlcReleaseFrame(&s->frame_pool, fp);
      iohdlc_mutex_unlock(&p->state_mutex);
      return;
    }
    
    /* Valid response received. Process based on response type.
       We know what we requested because um_cmd is still set. */
    
    /* Stop reply timer immediately. */
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
    
    if (u_cmd == IOHDLC_U_UA) {
      /* UA received: command accepted. */
      
      /* Reset peer variables. */
      resetPeerVars(p);
      if (p->um_cmd == IOHDLC_U_DISC) {
        /* DISC accepted: disconnect this peer. */
        ioHdlcSetDisconnected(p);

        /* Enter disconnected mode only if no other peer is still connected. */
        if (s->connected_count == 0) {
          s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
          setModeFunctions(s, s->mode);
        }
      } else {
        /* Connection command accepted (SNRM/SARM/SABM).
           Mode was already set by linkup() before sending command. */
        setModeFunctions(s, s->mode);
        ioHdlcSetConnected(p);
      }
      
      uint8_t cmd = p->um_cmd;  /* Save command for app notification */
      /* Clear UM state. */
      resetPeerUm(p);
      
      /* Re-trigger LINK_REQ to chain-serve other pending peers;
         ioHdlcNextPeer(s,true) in TX guards the switch. */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_ST_CHG | IOHDLC_EVT_LINK_REQ);
      
      /* Notify application: determine if link up or link down based on um_cmd. */
      ioHdlcBroadcastFlagsApp(s, (cmd == IOHDLC_U_DISC) ? 
                            IOHDLC_APP_LINK_DOWN : IOHDLC_APP_LINK_UP);
      
    } else if (u_cmd == IOHDLC_U_DM) {
      /* DM received: peer disconnected or refused connection. */
      ioHdlcSetDisconnected(p);
      p->ss_state |= IOHDLC_SS_ST_DISM;
      resetPeerUm(p);
      
      /* Re-trigger LINK_REQ (same rationale as UA path above). */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_ST_CHG | IOHDLC_EVT_LINK_REQ);
      
      /* Notify application: DM means link refused or link down. */
      ioHdlcBroadcastFlagsApp(s, (p->um_state & IOHDLC_UM_SENT) ? 
                            IOHDLC_APP_LINK_REFUSED : IOHDLC_APP_LINK_LOST);
      
    } else if (u_cmd == IOHDLC_U_FRMR) {
      /* FRMR received: peer detected protocol error.
         Per ISO 13239, recovery is a higher-layer responsibility.
         At minimum the primary must issue a mode setting command (SNRM). */
      IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr, "FRMR received from peer");
      resetPeerUm(p);
      ioHdlcBroadcastFlagsApp(s, IOHDLC_APP_FRMR_RECEIVED);
    }
    
    /* Mark F received and clear UM_SENT. */
    if (has_pf)
      s->pf_state |= IOHDLC_F_RCVED;
    p->um_state &= ~IOHDLC_UM_SENT;
  }
  
  iohdlc_mutex_unlock(&p->state_mutex);
  hdlcReleaseFrame(&s->frame_pool, fp);
}

/*===========================================================================*/
/* NRM RX helper functions.                                                  */
/*===========================================================================*/

/**
 * @brief   Extract the N(S) value from an I-frame.
 * @param[in] s    Station descriptor.
 * @param[in] fp   Received or queued frame.
 * @return  The decoded N(S) sequence number.
 */
static uint32_t extractNS(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Extract N(S) from control field based on modmask. */
  if (s->modmask == 7) {
    /* Modulo 8: N(S) in bits 1-3 of ctrl[0]. */
    return (IOHDLC_FRAME_CTRL(s, fp, 0) >> 1) & 0x07;
  } else {
    /* Modulo 128+: N(S) in bits 1-7 of ctrl[0]. */
    return (IOHDLC_FRAME_CTRL(s, fp, 0) >> 1) & 0x7F;
  }
}

/**
 * @brief   Extract the N(R) value from an I-frame or S-frame.
 * @param[in] s    Station descriptor.
 * @param[in] fp   Received or queued frame.
 * @return  The decoded N(R) sequence number.
 */
static uint32_t extractNR(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Extract N(R) from control field based on modmask. */
  if (s->modmask == 7) {
    /* Modulo 8: N(R) in bits 5-7 of ctrl[0]. */
    return (IOHDLC_FRAME_CTRL(s, fp, 0) >> 5) & 0x07;
  } else {
    /* Modulo 128+: N(R) in bits 1-7 of ctrl[1]. */
    return (IOHDLC_FRAME_CTRL(s, fp, 1) >> 1) & 0x7F;
  }
}

/**
 * @brief   Validate a received N(R) against the current transmit window.
 * @param[in] s    Station descriptor.
 * @param[in] p    Peer state.
 * @param[in] nr   Received acknowledgment number.
 * @return  true if @p nr falls within the valid acknowledged-to-sent range.
 */
static bool isNRValid(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                      uint32_t nr) {
  /* N(R) is INVALID if it identifies:
     a) An I-frame previously transmitted and acknowledged (nr < p->nr)
     b) An I-frame not transmitted (nr > p->vs)
     
     Valid range: p->nr <= nr <= p->vs (in modular arithmetic)
     Equivalent: distance from p->nr to nr must be <= distance from p->nr to p->vs */
  
  uint32_t nr_offset = (nr - p->nr) & s->modmask;     /* Distance from last ACK to new N(R) */
  uint32_t vs_offset = (p->vs_highest - p->nr) & s->modmask;  /* Distance from last ACK to next to send */
  
  return nr_offset <= vs_offset;
}

/**
 * @brief   Consume acknowledgments up to a received N(R).
 * @details Releases acknowledged frames from the retransmission queue and
 *          updates checkpoint tracking as required.
 * @param[in] s    Station descriptor.
 * @param[in] p    Peer state.
 * @param[in] nr   Received acknowledgment number.
 * @return  true if one or more queued frames were released.
 */
static bool processNR(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                      uint32_t nr) {
  /* Remove acknowledged frames from retransmission queue.
     Frames with N(S) < nr have been received by peer.
     Also clear checkpoint/REJ state if their tracked frames are ACKed.
     Returns true if space became available.
  */
  bool released_frames = false;
  
  while (p->nr != nr && !ioHdlc_frameq_isempty(&p->i_retrans_q)) {
    iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(&p->i_retrans_q);
    iohdlc_frame_t *acked_fp = IOHDLC_FRAME_FROM_Q(qh);
    uint32_t acked_ns = extractNS(s, acked_fp);

    if (acked_ns != p->nr) {
      /* This should never happen if queues are consistent. */
      IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr,
                       "Inconsistent N(R) processing: expected N(S)=%u, got N(S)=%u",
                       p->nr, acked_ns);
    }

    hdlcReleaseFrame(&s->frame_pool, acked_fp);
    p->i_pending_count--;  /* Decrement pending counter */
    released_frames = true;
    /* Increment with modmask */
    p->nr = (p->nr + 1) & s->modmask;

    /* Inhibit retransmission on acknowledgment of the last P/F frame. */
    if (acked_ns == p->vs_atlast_pf) {
      p->vs_atlast_pf = (p->vs_atlast_pf + 1) & s->modmask;
    }
  }
  
  /* Returns true if the stakeholders should be notified.*/
  return released_frames;
}

/**
 * @brief   Move checkpoint-retransmission frames back to the transmit queue.
 * @details Implements checkpoint recovery by selecting all outstanding frames
 *          preceding the last poll/final checkpoint and moving them to the
 *          transmit queue head.
 * @param[in] s   Station descriptor.
 * @param[in] p   Peer state.
 * @return  true if at least one frame was scheduled for retransmission.
 */
static bool checkpointRetransmit(iohdlc_station_t *s, iohdlc_station_peer_t *p) {
  /* Move frames from i_retrans_q back to i_trans_q for checkpoint retransmission.
     Per ISO 13239 5.6.2.1: Both Primary and Secondary do this.
     ISO 13239 5.6.2.1 case a): An actioned REJ with P/F=0 inhibits checkpoint
     retransmission if it would retransmit the same I-frame.
     Returns true if frames were moved, false otherwise.
  */

  if (ioHdlc_frameq_isempty(&p->i_retrans_q))
    return false;  /* Nothing to retransmit */
  
  /* Find the first frame that needs checkpoint retransmission. */
  uint32_t first_ns = 0;  /* N(S) + 1 of first frame to retransmit, 0 if none */
  
  /* Scan the retransmission queue to find frames with N(S) < vs_atlast_pf.
     Since i_retrans_q is FIFO and contiguous, we scan until we find 
     the frame with N(S) == vs_atlast_pf (excluded from retransmission).
     
     Check for REJ overlap: if rej_actioned != 0, it contains N(R)+1 from the REJ.
     If we find a frame with N(S) == N(R), inhibit entire checkpoint. */
  for (iohdlc_frame_q_t *fqp = p->i_retrans_q.next;
       fqp != &p->i_retrans_q;
       fqp = fqp->next) {
    iohdlc_frame_t *fp = IOHDLC_FRAME_FROM_Q(fqp);
    uint32_t frame_ns = extractNS(s, fp);
    
    if (frame_ns == p->vs_atlast_pf) {
      /* Found checkpoint frame: stop here (don't include it). */
      break;
    }

    /* This frame was sent before checkpoint: mark for retransmission. */
    if (first_ns == 0) {
      first_ns = frame_ns + 1;  /* Save its N(S) + 1 */
    }
  }
  
  /* If we found frames to retransmit, move them to i_trans_q. */
  if (first_ns != 0) {
    ioHdlc_frameq_move(&p->i_trans_q, p->i_retrans_q.next,
      p->i_retrans_q.prev);
    
    /* Mark checkpoint as actioned with first frame N(S).
       Used to detect overlap with subsequent REJ (5.6.2.2). */
    p->chkpt_actioned = first_ns;
#if defined(IOHDLC_ENABLE_STATISTICS)
    p->stats.checkpoints++;
#endif
    p->vs = first_ns - 1;  /* Reset V(S) to first retransmit frame N(S) */
    return true;
  }
  
  return false;
}

/**
 * @brief   Process acknowledgment and checkpoint side effects for a received frame.
 * @details Validates the received N(R), releases acknowledged frames, updates
 *          P/F state, and manages reply/t3 timers based on command/response.
 * @param[in] s                      Station descriptor.
 * @param[in] p                      Peer state.
 * @param[in] nr                     Received N(R) value.
 * @param[in] pf                     Poll/final bit extracted from the frame.
 * @param[in] is_command             true if the received frame is a command
 *                                   (addr == station address).
 * @param[out] should_signal_tx_out  Set when waiting writers should be woken.
 * @param[out] checkpoint_moved_out  Set when checkpoint retransmission moved frames.
 * @param[out] broadcast_flags_out   Accumulated core event flags to broadcast.
 * @return  true on success, false if the received N(R) is invalid.
 */
static bool handleCheckpointAndAck(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                                   uint32_t nr,
                                   bool pf,
                                   bool is_command,
                                   bool *should_signal_tx_out,
                                   bool *checkpoint_moved_out,
                                   uint32_t *broadcast_flags_out) {
  /* Common processing for both I-frames and S-frames:
     - Process N(R) to acknowledge our sent frames
     - Handle P/F bit for checkpointing
     - Manage reply timer based on command/response and P/F bit
     is_command: true if received frame is a command (addr == s->addr).
     Returns flags via output parameters for deferred signaling.
  */

  /* Validate N(R) before processing.*/
  if (!isNRValid(s, p, nr)) {
    /* Protocol error: invalid N(R) received.
       Send FRMR with Y bit set (invalid N(R)).*/
    IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr, "Invalid N(R) %u, V(S)=%u, N(R)=%u",
                  nr, p->vs, p->nr);
    return false;
  }

  /* Process N(R) to acknowledge our sent frames. */
  *should_signal_tx_out = processNR(s, p, nr);

  /* Handle P/F bit for checkpointing (independent of frame type). */
  *checkpoint_moved_out = false;
  if (pf) {
    /* Checkpoint retransmission: retransmit unacknowledged frames
       with N(S) < vs_atlast_pf. Move them from i_retrans_q to i_trans_q.

       ISO 13239 5.6.2.1 point (h): in ABM (combined station), checkpoint
       is inhibited on receiving P=1 (command). NRM primary/secondary both
       do checkpoint normally on their respective P/F reception. */
    if (!(IOHDLC_IS_ABM(s) && is_command))
      *checkpoint_moved_out = checkpointRetransmit(s, p);

    /* Command/response P/F handling.
       is_command: received a command → P/F bit is a Poll → must respond with F.
       !is_command: received a response → P/F bit is a Final → close our poll. */
    if (!is_command) {
      /* Received F=1 (response to our poll): acknowledge and stop timer. */
      s->pf_state |= IOHDLC_F_RCVED;
      p->poll_retry_count = 0;
      ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
      ioHdlcStartReplyTimer(p, IOHDLC_TIMER_T3,
            s->reply_timeout_ms * IOHDLC_DFL_T3_T1_RATIO);
    } else {
      /* Received P=1 (command): shall respond with F=1. */
      s->pf_state |= IOHDLC_P_RCVED;
    }
    *broadcast_flags_out |= IOHDLC_EVT_PF_RECVD;

  } else {
    /* pf == false */
    if (!is_command) {
      /* Received response with F=0: peer sent something but not final yet. */
      ioHdlcRestartReplyTimer(p, IOHDLC_TIMER_T3,
        s->reply_timeout_ms * IOHDLC_DFL_T3_T1_RATIO);
      if (!IOHDLC_F_ISRCVED(s)) {
        /* Only restart if we're still waiting for F (P is outstanding). */
        ioHdlcRestartReplyTimer(p, IOHDLC_TIMER_REPLY,
                                    IOHDLC_TIMER_BACKOFF(s, p));
      }
    }
  }

  return true;
}

/**
 * @brief   Process an I-frame after common acknowledgment handling.
 * @details Validates N(S), enqueues accepted frames for the reader side, and
 *          raises REJ or local-busy conditions when needed.
 * @param[in] s                    Station descriptor.
 * @param[in] p                    Peer state.
 * @param[in] fp                   Received I-frame.
 * @param[in] pf                   Poll/final bit extracted from the frame.
 * @param[out] broadcast_flags_out Accumulated core event flags to broadcast.
 * @return  true if the frame was accepted and queued for upper-layer reading.
 */
static bool handleIFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                         iohdlc_frame_t *fp,
                         bool pf,
                         uint32_t *broadcast_flags_out) {
  /* Handle I-frame specific logic:
     - Validate sequence number N(S)
     - Enqueue frame for application if valid
     - Generate REJ if out-of-sequence
     
     Caller must hold state_mutex.
     Returns flags via output parameters for deferred signaling.
     Returns false if frame should be discarded (out-of-sequence).
  */
  uint32_t ns = extractNS(s, fp);
  uint32_t expected_ns = p->vr;
  
  if (ns != expected_ns) {
    /* Out-of-sequence error detected. */
    
    /* Send REJ to request retransmission.
       REJ is not supported in TWA as it provides no practical benefit.
       ISO 13239 5.6.2.1 case a): only one REJ at a time.
       If REJ already actioned, first REJ will retransmit all needed frames.
       REJ is only sent if the option is negotiated (IOHDLC_USE_REJ).
       Without REJ, recovery relies on P/F checkpoint (often slower). */
#if defined(IOHDLC_LOG_R)
    IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr, "N(S) %u, exp %u",
                  ns, expected_ns);
#endif
#if defined(IOHDLC_ENABLE_STATISTICS)
    p->stats.out_of_sequence++;
#endif
    if (!IOHDLC_USE_TWA(s) && IOHDLC_USE_REJ(s) && p->rej_actioned == 0) {
      p->rej_actioned = expected_ns + 1;
      p->ss_state |= IOHDLC_SS_REJPEND;
      *broadcast_flags_out |= IOHDLC_EVT_REJ_ACTED;  /* REJ needs S-frame transmission */
    } else if (p->rej_actioned != 0) {
#if defined(IOHDLC_LOG_R)
      IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "REJ already actioned");
#endif
    }
    return false;  /* Discard frame */
  }
  
  /* Frame is in sequence: enqueue for application. */
  ioHdlc_frameq_insert(&p->i_recept_q, &fp->q);
  
  /* Signal the reception of a valid I-frame */
  *broadcast_flags_out |= IOHDLC_EVT_I_RECVD;
  
  /* Clear REJ exception if this is the frame that completes recovery.
     rej_actioned = x means waiting for frame with N(S) = x-1. */
  if (p->rej_actioned != 0 && ns == (p->rej_actioned - 1))
    p->rej_actioned = 0;
  
  if (IOHDLC_PEER_BUSY(p) && pf)
    p->ss_state &= ~IOHDLC_SS_BUSY;
   
  /* Check frame pool watermark and set local busy if LOW_WATER.
     This triggers RNR transmission to apply flow control. */
  if (!IOHDLC_IS_BUSY(s) && hdlcPoolGetState(&s->frame_pool) == IOHDLC_POOL_LOW_WATER) {
    s->flags |= IOHDLC_FLG_BUSY;  /* Mark that we are busy */
    *broadcast_flags_out |= IOHDLC_EVT_POOL_ST_CHG;
#if defined(IOHDLC_ENABLE_STATISTICS)
    p->stats.pool_low_water++;
#endif
  }
  
  /* Increment V(R) - frame accepted. */
  p->vr = (p->vr + 1) & s->modmask;
  
  return true;  /* Frame accepted */
}

/**
 * @brief   Process an S-frame after common acknowledgment handling.
 * @param[in] s                    Station descriptor.
 * @param[in] p                    Peer state.
 * @param[in] fp                   Received S-frame.
 * @param[in] pf                   Poll/final bit extracted from the frame.
 * @param[out] broadcast_flags_out Accumulated core event flags to broadcast.
 */
static void handleSFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                         iohdlc_frame_t *fp,
                         bool pf,
                         uint32_t *broadcast_flags_out) {
  /* Handle S-frame specific logic:
     - Update peer busy state (RR/RNR)
     - Process REJ for retransmission
     - Ignore SREJ (not implemented)
     Returns flags via output parameters for deferred signaling.
  */
  
  (void)pf;
  uint8_t ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);
  uint8_t s_fun = ctrl & IOHDLC_S_FUN_MASK;
  
  /* Handle S-frame function. */
  switch (s_fun) {
    case IOHDLC_S_RR:
      /* Peer ready to receive: clear busy flag. */
      p->ss_state &= ~IOHDLC_SS_BUSY;
      *broadcast_flags_out |= IOHDLC_EVT_RR_RECVD;
      break;
      
    case IOHDLC_S_RNR:
      /* Peer not ready: set busy flag. */
      p->ss_state |= IOHDLC_SS_BUSY;
      *broadcast_flags_out |= IOHDLC_EVT_RNR_RECVD;
      IOHDLC_SET_NEED_P(s, p);
      break;
      
    case IOHDLC_S_REJ:
      /* Peer requests retransmission from N(R).
         processNR() has already removed frames with N(S) < nr from i_retrans_q.
         Now move remaining frames (N(S) >= nr) from i_retrans_q to head of i_trans_q.
         
         Mark REJ as actioned to:
         1. Prevent duplicate REJ (same or different N(R))
         2. Inhibit checkpoint retransmission of overlapping frames
         
         5.6.2.2: If checkpoint retransmission is already handling the same
         particular I frame (same N(S)), REJ shall be inhibited. */
      if (!IOHDLC_USE_TWA(s)) {
#if defined(IOHDLC_LOG_R)
        uint32_t nr = extractNR(s, fp);
#endif
        /* Check if checkpoint is active and starting with same particular I frame.
           "same particular I frame" = same N(S) value. */
        if (p->chkpt_actioned == 0) {
          /* REJ acts: move all remaining frames from i_retrans_q to head of i_trans_q.
             processNR() has already removed frames with N(S) < N(R),
             so i_retrans_q now contains exactly the frames to retransmit (N(S) >= N(R)). */
#if defined(IOHDLC_LOG_R)
          IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "REJ done on N(R)=%u", nr);
#endif
          if (!ioHdlc_frameq_isempty(&p->i_retrans_q)) {
            iohdlc_frame_q_t *first_qh = p->i_retrans_q.next;
            iohdlc_frame_q_t *last_qh = p->i_retrans_q.prev;
            ioHdlc_frameq_move(&p->i_trans_q, first_qh, last_qh);
            p->vs = p->vs_atlast_pf = extractNS(s, IOHDLC_FRAME_FROM_Q(first_qh)); 
            *broadcast_flags_out |= IOHDLC_EVT_TX_IFRM_ENQ;
          }
        } else {
#if defined(IOHDLC_LOG_R)
          IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr,
                           "Inhibit REJ N(R)=%u, chkpt=%u", nr, p->chkpt_actioned);
#endif
          p->chkpt_actioned = 0;
        }
#if defined(IOHDLC_ENABLE_STATISTICS)
        p->stats.rej_received++;
#endif
        *broadcast_flags_out |= IOHDLC_EVT_xREJ_RECVD;
        IOHDLC_SET_NEED_P(s, p);
      }
      break;
      
    case IOHDLC_S_SREJ:
      /* Selective reject: not implemented. Ignore. */
      break;
  }
}

/**
 * @brief   Receive-side handler for NRM mode.
 * @details Processes I-frames and S-frames once common U-frame handling has
 *          already been excluded by the RX entry loop.
 * @param[in] s    Station descriptor.
 * @param[in] fp   Received frame owned by the core until accepted or released.
 */
/**
 * @brief   Common receive-side processing for I-frames and S-frames.
 * @details Shared by NRM and ABM modes. Handles FRMR, checkpoint/ACK,
 *          I-frame validation, and S-frame processing.
 * @param[in] s           Station descriptor.
 * @param[in] p           Peer state (already validated by caller).
 * @param[in] fp          Received frame.
 * @param[in] is_command  true if the frame is a command (addr == s->addr).
 */
static void commonRx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                     iohdlc_frame_t *fp, bool is_command) {

  const uint32_t addr = IOHDLC_FRAME_ADDR(s, fp);
  const uint8_t ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);
  const bool pf = IOHDLC_FRAME_GET_PF(s, fp);
  const uint32_t nr = extractNR(s, fp);

  /* Flags for deferred signaling (after lock release). */
  bool should_signal_tx = false;
  bool checkpoint_moved = false;
  uint32_t broadcast_flags = 0;
  bool frame_accepted = false;

  /* Single lock for entire processing sequence.
     This prevents TX from observing intermediate state where checkpoint/ACK
     is complete but I-frame validation/state update is not yet done. */
  iohdlc_mutex_lock(&p->state_mutex);

  /* FRMR exception active: per ISO 13239 5.5.3, do not accept I/S frames
     except for examining P bit and N(R). Re-trigger FRMR on each poll. */
  if (p->frmr_condition) {
    if (pf) {
      /* Poll received: re-trigger FRMR transmission. */
      p->um_rsp = IOHDLC_U_FRMR;
      p->um_state |= IOHDLC_UM_RCVED;
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_UM_RECVD);
    }
    iohdlc_mutex_unlock(&p->state_mutex);
    hdlcReleaseFrame(&s->frame_pool, fp);
    return;
  }

#if defined(IOHDLC_LOG_R) && IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
    const uint8_t addr2 = IOHDLC_FRAME_ADDR(s, fp);
    bool is_final = s->addr != addr2;
    uint32_t nns, nnr, qns;

    nns = extractNS(s, fp);
    nnr = nr;
    qns = ioHdlc_frameq_isempty(&p->i_retrans_q) ? s->modmask+1 :
      extractNS(s, IOHDLC_FRAME_FROM_Q(p->i_retrans_q.next));

    /* Log received frame based on type */
    if (IOHDLC_IS_I_FRM(ctrl)) {
      IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "A%u I%u,%u %c pnr=%u vfp=%u fr=%u",
		   addr2, nns, nnr,
		   pf ? (is_final ? 'F' : 'P') : '-',
		   p->nr, p->vs_atlast_pf, qns);
    } else if (IOHDLC_IS_S_FRM(ctrl)) {
      iohdlc_log_sfun_t log_fun = (ctrl & IOHDLC_S_FUN_MASK) >> 2;
      IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "A%u %s%u %c pnr=%u vfp=%u fr=%u",
		     addr2, iohdlc_sfun_to_str(log_fun), nnr,
		   pf ? (is_final ? 'F' : 'P') : '-',
		   p->nr, p->vs_atlast_pf, qns);
    }
#endif

  /* Common checkpoint and acknowledgment processing for all I/S frames. */
  if (!handleCheckpointAndAck(s, p, nr, pf, is_command, &should_signal_tx, &checkpoint_moved, &broadcast_flags)) {
    /* Invalid N(R): set FRMR exception condition (ISO 13239, 5.5.3).
       Extract full control field (1 or 2 bytes) from the frame. */
    {
      uint8_t rejected_ctrl[2];
      rejected_ctrl[0] = IOHDLC_FRAME_CTRL(s, fp, 0);
      rejected_ctrl[1] = (s->ctrl_size > 1) ? IOHDLC_FRAME_CTRL(s, fp, 1) : 0;
      setFrmrCondition(s, p, rejected_ctrl, s->ctrl_size, (addr != s->addr), IOHDLC_FRMR_Y);
    }
    iohdlc_mutex_unlock(&p->state_mutex);
    hdlcReleaseFrame(&s->frame_pool, fp);
    return;
  }

  /* Branch by frame type for specific handling. */
  if (IOHDLC_IS_I_FRM(ctrl)) {
    IOHDLC_SET_NEED_P(s, p);
    frame_accepted = handleIFrame(s, p, fp, pf, &broadcast_flags);
  } else {
    handleSFrame(s, p, fp, pf, &broadcast_flags);
  }

  /* Add checkpoint/ACK related events to broadcast flags */
  if (checkpoint_moved) {
    broadcast_flags |= IOHDLC_EVT_TX_IFRM_ENQ;
  }

  if (broadcast_flags) {
    ioHdlcBroadcastFlags(s, broadcast_flags);
  }

  /* Signal user threads waiting on flow control */
  if (should_signal_tx) {
    iohdlc_condvar_broadcast(&p->tx_cv);
  }

  iohdlc_mutex_unlock(&p->state_mutex);

  /* Signal application that I-frame is ready to read */
  if (frame_accepted) {
    iohdlc_sem_signal(&p->i_recept_sem);
    return;
  }

  hdlcReleaseFrame(&s->frame_pool, fp);
}

void ioHdlcNrmRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {

  const uint32_t addr = IOHDLC_FRAME_ADDR(s, fp);
  iohdlc_station_peer_t *p = s->c_peer;

  /* NRM address filter: primary expects peer address, secondary expects
     station address. */
  if (IOHDLC_IS_PRI(s)) {
    if (addr != p->addr) {
      hdlcReleaseFrame(&s->frame_pool, fp);
      return;
    }
  } else {
    if (addr != s->addr) {
      hdlcReleaseFrame(&s->frame_pool, fp);
      return;
    }
  }

  commonRx(s, p, fp, (addr == s->addr));
}

/**
 * @brief   Receive-side handler for ABM mode.
 * @details ABM combined station accepts both commands (addr == s->addr)
 *          and responses (addr == p->addr) from the single peer.
 * @param[in] s    Station descriptor.
 * @param[in] fp   Received frame.
 */
void ioHdlcAbmRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {

  const uint32_t addr = IOHDLC_FRAME_ADDR(s, fp);
  iohdlc_station_peer_t *p = s->c_peer;

  /* ABM address filter: accept both command (addr == s->addr) and
     response (addr == p->addr) from the peer. */
  if (addr != s->addr && addr != p->addr) {
    hdlcReleaseFrame(&s->frame_pool, fp);
    return;
  }

  commonRx(s, p, fp, (addr == s->addr));
}

/**
 * @brief   RX worker entry point for the runner.
 * @details Pulls frames from the framed driver, detects idle-line conditions,
 *          dispatches U-frames directly, and delegates I/S frames to the
 *          mode-specific RX handler.
 * @param[in] stationp   Opaque pointer to the station instance.
 */
void ioHdlcRxEntry(void *stationp) {
  iohdlc_station_t *s = (iohdlc_station_t *)stationp;
  if (!s) return;
  for (;;) {
    /* Check if stop requested */
    if (s->stop_requested) {
      break;
    }

    /* Use short timeout (500ms) to allow stop check */
    iohdlc_frame_t *fp = hdlcRecvFrame(s->driver, (iohdlc_timeout_t)500);
    if (fp == NULL) {
      /* Check again before treating as idle line */
      if (s->stop_requested) {
        break;
      }
      /* Idle line or timeout */
      s->flags |= IOHDLC_FLG_IDL;
      if (s->flags & IOHDLC_FLG_TWA) {
        /* In TWA mode, line idle might be significant - broadcast event. */
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINE_IDLE);
      }
      IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr, "--");
      continue;
    }
    s->flags &= ~IOHDLC_FLG_IDL;

    /* Decode control octet to determine frame type. */
    const uint8_t ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);

    /* Handle U-frames common to all modes. */
    if (IOHDLC_IS_U_FRM(ctrl)) {
      handleUFrame(s, fp);
      continue;
    }

    /* Call mode-specific RX handler for I and S frames.
       rx_fn is NULL in disconnected mode (NDM/ADM); discard the frame. */
    if (s->rx_fn != NULL) {
      s->rx_fn(s, fp);
    } else {
      hdlcReleaseFrame(&s->frame_pool, fp);
    }
  }
  ioHdlcStopReplyTimer(s->c_peer, IOHDLC_TIMER_REPLY);
  ioHdlcStopReplyTimer(s->c_peer, IOHDLC_TIMER_T3);
}

/*===========================================================================*/
/* Frame Building Functions                                                  */
/*===========================================================================*/

/**
 * @brief   Build U-frame (Unnumbered frame) for transmission.
 * @details Constructs control field and sets address field according to
 *          ISO 13239 command/response semantics.
 *          
 * @param[in] s           Station descriptor
 * @param[in] p           Peer descriptor
 * @param[in,out] fp      Frame to build (must be allocated)
 * @param[in] u_fun       U-frame function code (IOHDLC_U_xxx: SNRM, UA, DISC, etc.)
 * @param[in] set_pf      true to set P/F bit
 * @param[in] is_command  true if command, false if response
 * 
 * @note U-frames have 1-byte control field regardless of modulo.
 *       Control field format: [M4 M3 P/F M2 M1 1 1]
 *       where M bits encode the function.
 *       P/F bit is always bit 4 of ctrl[0] for U-frames, even in extended modes.
 * @note This function handles only U-frames WITHOUT info field.
 *       For FRMR/XID/TEST/UI, use buildUFrameWithInfo().
 */
static void buildUFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                        iohdlc_frame_t *fp, uint8_t u_fun, bool set_pf,
                        bool is_command) {
  /* Set address field per ISO 13239 4.2.2 */
  uint8_t addr = is_command ? p->addr : s->addr;
  IOHDLC_FRAME_ADDR(s, fp) = addr;
  
  /* Build control field:
     U-frame format: bits 0-1 = 11 (U-frame identifier)
     Bits 2-7 contain M bits (function code) and P/F bit.
     u_fun contains only the M bits, we must OR with 0x03 to set bits 0-1. */
  IOHDLC_FRAME_CTRL(s, fp, 0) = u_fun | IOHDLC_U_ID;  /* Add U-frame identifier (0x03) */
  
  /* Set P/F bit (always bit 4 for U-frames, even in extended mode) */
  if (set_pf) {
    IOHDLC_FRAME_CTRL(s, fp, 0) |= IOHDLC_PF_BIT;
  }
  
  /* Calculate elen: FFF + ADDR + CTRL (U-frame always has 1 byte ctrl) */
  uint8_t *end = fp->frame + s->frame_offset;
  end += 2;  /* ADDR(1) + CTRL(1) - U-frame control is always 1 byte */
  fp->elen = (uint16_t)(end - fp->frame);
  
  /* FFF will be valorized by driver (driver knows FCS size) */
}

/**
 * @brief   Send frame to driver and release it.
 * @details Wrapper for hdlcSendFrame() that handles errors and frame release.
 *          The frame is always released after transmission attempt.
 *          
 * @param[in] s     Station descriptor
 * @param[in] fp    Frame to send (will be released after send)
 * 
 * @return          true if frame sent successfully, false on error
 * 
 * @note The driver is responsible for:
 *       - FCS calculation and insertion
 *       - FLAG (0x7E) insertion (opening/closing)
 *       - Octet transparency encoding (if enabled)
 *       - FFF (Frame Format Field) valorization (if enabled)
 * @note This function always releases the frame, even on error.
 */
static bool sendFrame(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  size_t err = hdlcSendFrame(s->driver, fp);
  hdlcReleaseFrame(&s->frame_pool, fp);
  return (err == 0);
}

/**
 * @brief   Build S-frame (Supervisory frame) for transmission.
 * @details Constructs control field with N(R) and sets address field.
 *          Calculates elen.
 *          
 * @param[in] s           Station descriptor
 * @param[in] p           Peer descriptor
 * @param[in,out] fp      Frame to build (must be allocated)
 * @param[in] s_fun       S-frame function code (IOHDLC_S_RR/RNR/REJ/SREJ)
 * @param[in] nr          N(R) value to send (acknowledgment)
 * @param[in] set_pf      true to set P/F bit
 * @param[in] is_command  true if command, false if response
 * 
 * @note S-frames have variable control field size based on modulo.
 *       Macros IOHDLC_FRAME_SET_NR() and IOHDLC_FRAME_SET_PF() handle
 *       all moduli (8, 128, 32768, 2^31) automatically.
 */
static void buildSFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                        iohdlc_frame_t *fp, uint8_t s_fun, uint32_t nr,
                        bool set_pf, bool is_command) {
  /* Set address field per ISO 13239 4.2.2 */
  uint8_t addr = is_command ? p->addr : s->addr;
  IOHDLC_FRAME_ADDR(s, fp) = addr;
  
  /* Build control field: S function bits + S-frame ID (0x01) */
  IOHDLC_FRAME_CTRL(s, fp, 0) = s_fun | IOHDLC_S_ID;
  
  /* Set N(R) and P/F using macros (handle all moduli automatically) */
  IOHDLC_FRAME_SET_NR(s, fp, nr);
  IOHDLC_FRAME_SET_PF(s, fp, set_pf);
  
  /* Calculate elen: FFF + ADDR + CTRL (no info field for S-frames) */
  uint8_t *end = fp->frame + s->frame_offset;
  end += 1 + s->ctrl_size;  /* ADDR(1) + CTRL(1,2,4,8) */
  fp->elen = (uint16_t)(end - fp->frame);
  
  /* FFF will be valorized by driver (driver knows FCS size) */
}

/**
 * @brief   Prepare an S-frame for deferred transmission.
 * @details Allocates a frame from the pool, chooses the correct P/F behaviour
 *          for the current role and mode, and builds the outgoing S-frame if
 *          transmission is currently allowed.
 * @param[in] s       Station descriptor.
 * @param[in] p       Peer state.
 * @param[in] s_fun   Supervisory function code to emit.
 * @return  A prepared frame ready for @ref sendFrame, or NULL if none can be
 *          prepared at this time.
 */
static iohdlc_frame_t *prepareSFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                       uint8_t s_fun) {
  if (!sendOpportunity(s))
    return NULL;

  /* Command/response: in NRM constant by role, in ABM varies by P/F state.
     Per ISO 13239 4.2.2: command addr = peer, response addr = station. */
  const bool is_command = IOHDLC_IS_ABM(s) ?
      !IOHDLC_P_ISRCVED(s) : IOHDLC_IS_PRI(s);

  const uint32_t outstanding = (p->vs - p->nr) & s->modmask;
  const bool window_full = outstanding >= p->ks;
  const bool no_i_frame = ioHdlc_frameq_isempty(&p->i_trans_q) || window_full;
  bool set_pf = is_command ?
    (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) && no_i_frame : IOHDLC_F_ISRCVED(s)) :
    (IOHDLC_P_ISRCVED(s) && (no_i_frame || IOHDLC_PEER_BUSY(p)));

  iohdlc_frame_t *fp = hdlcTakeFrame(&s->frame_pool);
  if (fp != NULL) {
    buildSFrame(s, p, fp, s_fun, p->vr, set_pf, is_command);
#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
    /* Log S-frame transmission (before send, frame will be released) */
    iohdlc_log_sfun_t log_fun = (s_fun >> 2);
    uint8_t log_flags = (s_fun == IOHDLC_S_RNR) ? IOHDLC_LOG_FLAG_BUSY : 0;
    uint8_t log_addr = IOHDLC_FRAME_ADDR(s, fp);
#endif

    /* Update checkpoint reference and ACK P/F before sending.
       In ABM with crossed polls (sending F while own P outstanding),
       preserve V(SC) from our P — ISO 13239 5.6.2.1(h). */
    if (set_pf) {
      if (is_command || !IOHDLC_IS_ABM(s) || IOHDLC_F_ISRCVED(s))
        p->vs_atlast_pf = p->vs;
      is_command ? IOHDLC_ACK_F(s) : IOHDLC_ACK_P(s);
      IOHDLC_CLR_NEED_P(p);
    }

    IOHDLC_LOG_SFRAME(IOHDLC_LOG_TX, s->addr, log_addr, log_fun,
                      p->vr, set_pf, p->i_pending_count, log_flags);

    /* If sending command with P (poll), start the T1 timer and stop the T3. */
    if (set_pf && is_command) {
      ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, IOHDLC_TIMER_BACKOFF(s, p));
      ioHdlcStopReplyTimer(p, IOHDLC_TIMER_T3);
    }
  }
  return fp;
}

/**
 * @brief   NRM transmit-side scheduler.
 * @details Consumes pending core events, sends connection-management U-frames,
 *          transmits I-frames within the current window and P/F rules, and
 *          emits opportunistic or recovery S-frames when required.
 * @param[in] s         Station descriptor.
 * @param[in] p         Peer state for the currently selected peer.
 * @param[in] cm_flags  Pending core event flags to serve.
 * @return  Residual event flags that still require later handling.
 */
uint32_t ioHdlcNrmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                uint32_t cm_flags) {

  cm_flags &= ~(IOHDLC_EVT_LINE_IDLE);

  if (cm_flags & IOHDLC_EVT_C_RPLYTMO) {
    /* Poll/reply timeout occurred: handle retry logic. */
    cm_flags &= ~IOHDLC_EVT_C_RPLYTMO;

    if (ioHdlcIsReplyTimerExpired(p, IOHDLC_TIMER_REPLY)) {
      IOHDLC_LOG_WARN(IOHDLC_LOG_TX, s->addr, "T1");
      if (!handleTimeoutRetry(s, p)) {
        /* Link down: max retries exceeded, cannot send I-frames. */
        iohdlc_mutex_unlock(&p->state_mutex);
        return cm_flags;
      }
      
      /* Retry: force P bit to be sent on next I-frame or opportunistic S-frame. */
      IOHDLC_SET_NEED_P(s, p);
      s->pf_state |= IOHDLC_F_RCVED;
      p->rej_actioned = 0;  /* Clear REJ exception on timeout retry */
    }
  }

  if (cm_flags & IOHDLC_EVT_T3_TMO) {
    /* T3 timeout: handle retry logic. */
    cm_flags &= ~IOHDLC_EVT_T3_TMO;

    if (ioHdlcIsReplyTimerExpired(p, IOHDLC_TIMER_T3)) {
      IOHDLC_LOG_WARN(IOHDLC_LOG_TX, s->addr, "T3");
      
      /* Retry: force P bit to be sent on next I-frame or opportunistic S-frame. */
      IOHDLC_SET_NEED_P(s, p);
    }
  }

  iohdlc_mutex_unlock(&p->state_mutex);

  /* I-frame transmission: follow NRM rules (TWA vs TWS, Primary vs Secondary).
     FRMR exception: suppress I-frame transmission per ISO 13239, 5.5.3. */

  bool i_frame_sent = false;  /* Track if at least one I-frame was sent. */
  if (p->frmr_condition)
    goto skip_i_frames;  /* FRMR active: no I-frame transmission. */

  while (true) {
    /* Lock to check window and dequeue frame atomically */
    iohdlc_mutex_lock(&p->state_mutex);
    
    /* Check if queue is empty */
    if (ioHdlc_frameq_isempty(&p->i_trans_q)) {
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }
    
    /* Check transmission window availability. */
    const uint32_t outstanding = (p->vs - p->nr) & s->modmask;
    if (outstanding >= p->ks) {
      /* Window full: cannot send I-frames. */
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }

    if (!sendOpportunity(s)) {
      /* Cannot send I-frame now (TWA waiting for P/F bit). */
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }
  
    if (IOHDLC_PEER_BUSY(p)) {
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }

    /* Poll for new events before sending each I-frame.
       This allows interrupting the burst if "urgent" events occur. */
    cm_flags |= iohdlc_evt_get_and_clear_flags(&s->cm_listener);
    
    /* Check for urgent events requiring attention:
       - IOHDLC_EVT_LINK_DOWN: Link failure detected */
    if (cm_flags & IOHDLC_EVT_LINK_DOWN) {
      /* Link is down: abort transmission immediately. */
      cm_flags &= ~IOHDLC_EVT_LINK_DOWN;
      iohdlc_mutex_unlock(&p->state_mutex);
      return cm_flags;
    }
    
    /* Extract frame from transmission queue with lookahead. */
    iohdlc_frame_q_t *next_qh = NULL;
    iohdlc_frame_q_t *qh = ioHdlc_frameq_remove_la(&p->i_trans_q, &next_qh);
    iohdlc_frame_t *fp = qh ? IOHDLC_FRAME_FROM_Q(qh) : NULL;
    iohdlc_frame_t *next_fp = next_qh ? IOHDLC_FRAME_FROM_Q(next_qh) : NULL;
    if (fp == NULL) {
      iohdlc_mutex_unlock(&p->state_mutex);
      break;  /* Safety check. */
    }

    IOHDLC_SET_NEED_P(s, p);
    bool set_pf = false;

    /* In NRM, command/response is determined by role (primary=command,
       secondary=response). In ABM, it varies per frame based on P/F state.
       Per ISO 13239 4.2.2: command addr = peer, response addr = station. */
    const bool is_command = IOHDLC_IS_ABM(s) ?
        !IOHDLC_P_ISRCVED(s) : IOHDLC_IS_PRI(s);

    /* Set address at TX time based on command/response decision.
       ISO 13239 4.2.2: command addr = peer, response addr = station.
       ISO 13239 5.4.3.2.3: in ABM, F must be set on the earliest possible
       subsequent response frame after receiving P=1. */
    IOHDLC_FRAME_ADDR(s, fp) = is_command ? p->addr : s->addr;

    /* Determine if P/F bit should be set in this I-frame.
       If local busy, never set P/F on I-frames. */
    if (!IOHDLC_IS_BUSY(s)) {
      /* Determine whether to set P/F bit.
         Command TWA: Set P on last I-frame (we have the link).
         Command TWS: Set P as soon as possible (if no P in flight).
         Response (TWA & TWS): Set F on last I-frame (when window will be full
         or no more frames). */
      const bool is_last_frame = ((outstanding + 1) >= p->ks) || (next_fp == NULL);

      if (is_command) {
        /* Command: behavior differs between TWA and TWS.
          TWA: set P on last frame (we have the link).
          TWS: set P as soon as possible (if no P in flight). */
        set_pf = IOHDLC_USE_TWA(s) ? is_last_frame : IOHDLC_F_ISRCVED(s);
      } else {
        /* Response: set F when we have a P to respond to.
           TWA: F on last frame (end of turn).
           TWS: F on first frame (ISO 13239 5.4.3.2.3: earliest possible).
           With is_command = !P_ISRCVED, ACK_P after F returns to command. */
        set_pf = IOHDLC_P_ISRCVED(s) &&
                 (IOHDLC_USE_TWA(s) ? is_last_frame : true);
      }
    }
    /* Read vr for N(R) field */
    uint32_t nr_value = p->vr;
    
    /* Move frame to retransmission queue. */
    ioHdlc_frameq_insert(&p->i_retrans_q, &fp->q);

    /* Set N(S) and then advance V(S) - use
       modmask for modular arithmetic on full numbering space. */
    IOHDLC_FRAME_SET_NS(s, fp, p->vs);
    if (p->vs == p->vs_highest)
      p->vs_highest = (p->vs + 1) & s->modmask;
    p->vs = (p->vs + 1) & s->modmask;

    /* Update checkpoint reference and ACK P/F before sending.
       In ABM with crossed polls (sending F while own P outstanding),
       preserve V(SC) from our P — ISO 13239 5.6.2.1(h). */
    if (set_pf) {
      if (is_command || !IOHDLC_IS_ABM(s) || IOHDLC_F_ISRCVED(s))
        p->vs_atlast_pf = p->vs;
      is_command ? IOHDLC_ACK_F(s) : IOHDLC_ACK_P(s);
    }
    
    /* Update N(R) and P/F in frame */
    IOHDLC_FRAME_SET_NR(s, fp, nr_value);
    IOHDLC_FRAME_SET_PF(s, fp, set_pf);

#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
    /* Extract values for logging (before send, frame will be released) */
    size_t info_len = fp->elen - (s->frame_offset + 1 + s->ctrl_size);
    uint32_t log_ns = extractNS(s, fp);
    uint32_t log_nr = extractNR(s, fp);
    uint8_t log_addr = IOHDLC_FRAME_ADDR(s, fp);
    uint8_t fflags = 0;
#endif

    /* Log I-frame transmission */
    IOHDLC_LOG_IFRAME(IOHDLC_LOG_TX, s->addr, log_addr,
                      log_ns, log_nr, set_pf, info_len,
                      p->i_pending_count, p->ks, fflags);
    
    /* Send frame under lock to ensure state consistency */
    (void)hdlcSendFrame(s->driver, fp);

    /* If sending command with P (poll), start the T1 timer and stop the T3. */
    if (set_pf && is_command) {
      ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, IOHDLC_TIMER_BACKOFF(s, p));
      ioHdlcStopReplyTimer(p, IOHDLC_TIMER_T3);
    }

    /* Mark that we sent at least one I-frame. */
    i_frame_sent = true;

    /* Check for urgent events requiring attention:
       - IOHDLC_EVT_UMRECVD: U-frame received (disconnect/mode change)
       - IOHDLC_EVT_POOL_ST_CHG: Local busy condition
       - IOHDLC_EVT_RNR_RECVD: Peer went into RNR state */
    if ((cm_flags & (IOHDLC_EVT_UM_RECVD | IOHDLC_EVT_RNR_RECVD |
                     IOHDLC_EVT_POOL_ST_CHG | IOHDLC_EVT_REJ_ACTED))) {
      /* "Urgent" event detected: exit I-frame loop. */
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }

    iohdlc_mutex_unlock(&p->state_mutex);

    /* In TWA, stop after sending one frame with F (secondary) or P (primary). */
    if (IOHDLC_USE_TWA(s) && set_pf)
      break;
  }

skip_i_frames:
  iohdlc_mutex_lock(&p->state_mutex);

  /* Check cm_flags again after re-acquiring the mutex. */
  cm_flags |= iohdlc_evt_get_and_clear_flags(&s->cm_listener);

  iohdlc_frame_t *sframe_to_send = NULL;
  if (p->ss_state & IOHDLC_SS_REJPEND) {
    cm_flags &= ~IOHDLC_EVT_REJ_ACTED;
    if ((sframe_to_send = prepareSFrame(s, p, IOHDLC_S_REJ)) != NULL)
      p->ss_state &= ~IOHDLC_SS_REJPEND;
  }

  /* Check if we need to clear local busy: pool returned NORMAL after RNR. */
  if ((sframe_to_send == NULL) && IOHDLC_IS_BUSY(s) && 
      hdlcPoolGetState(&s->frame_pool) == IOHDLC_POOL_NORMAL) {

    sframe_to_send = prepareSFrame(s, p, IOHDLC_S_RR);
    if (sframe_to_send != NULL) {
      s->flags &= ~IOHDLC_FLG_BUSY;

      /* Wake up writers blocked on pool availability. */
      iohdlc_condvar_broadcast(&p->tx_cv);
    }
  }
  
  /* If no frame was sent, or prepared to, but we have the opportunity/need
     to respond, prepare to send an opportunistic S-frame (RR or RNR). */
  if (sframe_to_send == NULL && !i_frame_sent && (IOHDLC_P_ISRCVED(s) ||
        (IOHDLC_F_ISRCVED(s) && IOHDLC_NEED_P(p)))) {
    /* In TWA, if we still have permission on the link but didn't send I-frames,
       we should send an S-frame to acknowledge and cede the link.
       In TWS, we may also want to send periodic acknowledgments. */
    sframe_to_send = prepareSFrame(s, p, 
      IOHDLC_IS_BUSY(s) ? IOHDLC_S_RNR : IOHDLC_S_RR);
  }
  
  cm_flags &= ~(IOHDLC_EVT_I_RECVD|IOHDLC_EVT_RNR_RECVD|IOHDLC_EVT_POOL_ST_CHG|
                IOHDLC_EVT_TX_IFRM_ENQ|IOHDLC_EVT_PF_RECVD|IOHDLC_EVT_xREJ_RECVD);

  if (sframe_to_send != NULL) {
    /* Send the prepared S-frame. */
    (void) sendFrame(s, sframe_to_send);
  }

  /* NRM multipoint: if primary has nothing pending for this peer, no FRMR
     condition, and the P/F cycle is closed (F received), advance to
     the next peer in the round-robin. ABM is point-to-point only. */
  if (IOHDLC_IS_NRM(s) && IOHDLC_IS_PRI(s) && IOHDLC_F_ISRCVED(s) &&
      !p->frmr_condition &&
      ioHdlc_frameq_isempty(&p->i_trans_q) &&
      ioHdlc_frameq_isempty(&p->i_retrans_q)) {
    iohdlc_station_peer_t *prev = p;
    iohdlc_station_peer_t *next = ioHdlcNextPeer(s, false);
    if (next != NULL && next != prev)
      IOHDLC_SET_NEED_P(s, next);
  }

  iohdlc_mutex_unlock(&p->state_mutex);

  return cm_flags;
}

/**
 * @brief   Transmit-side scheduler for ABM mode.
 * @details ABM TX shares the common transmit logic with NRM. The is_command
 *          variable inside the loop uses IOHDLC_IS_ABM() to determine
 *          command/response from P/F state rather than from role.
 *          NRM multipoint round-robin is guarded by IOHDLC_IS_NRM().
 * @param[in] s         Station descriptor.
 * @param[in] p         Peer state.
 * @param[in] cm_flags  Pending core event flags.
 * @return  Residual event flags that still require later handling.
 */
uint32_t ioHdlcAbmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags) {
  return ioHdlcNrmTx(s, p, cm_flags);
}

/**
 * @brief   TX worker entry point for the runner.
 * @details Waits for core events, serves connection-management requests, and
 *          dispatches transmit work to the mode-specific TX scheduler for the
 *          current peer.
 * @param[in] stationp   Opaque pointer to the station instance.
 */
void ioHdlcTxEntry(void *stationp) {
  iohdlc_station_t *s = (iohdlc_station_t *)stationp;
  iohdlc_station_peer_t *p;
  uint32_t flags_mask = 0;
  uint32_t cm_flags = 0;

  if (!s) return;

  flags_mask = IOHDLC_EVT_C_RPLYTMO|IOHDLC_EVT_T3_TMO|
               IOHDLC_EVT_I_RECVD|IOHDLC_EVT_RNR_RECVD|
               IOHDLC_EVT_POOL_ST_CHG|IOHDLC_EVT_LINK_ST_CHG|IOHDLC_EVT_UM_RECVD|
               IOHDLC_EVT_LINK_REQ|IOHDLC_EVT_LINE_IDLE|IOHDLC_EVT_xREJ_RECVD|
               IOHDLC_EVT_TX_IFRM_ENQ|IOHDLC_EVT_REJ_ACTED|IOHDLC_EVT_PF_RECVD;

  /* Register event listener */
  iohdlc_evt_register(&s->cm_es, &s->cm_listener, EVENT_MASK(0), flags_mask);

  for (;;) {
    p = s->c_peer;
    if (!cm_flags)
      cm_flags = ioHdlcWaitEvents(s);

    /* Check if stop requested */
    if (s->stop_requested)
      break;
    
    if (NULL == p) { cm_flags = 0; continue; }

    iohdlc_mutex_lock(&p->state_mutex);

    /* U command */
    if ((cm_flags  & IOHDLC_EVT_LINK_REQ) || ((p->um_state & IOHDLC_UM_SENT) &&
            (cm_flags & IOHDLC_EVT_C_RPLYTMO))) {
      uint32_t s_flags = cm_flags & (IOHDLC_EVT_LINK_REQ|IOHDLC_EVT_C_RPLYTMO);

      /* serve the event(s).*/
      cm_flags &= ~s_flags;
      
      /* Connection management requested. */
      if (!IOHDLC_IS_PRI(s)) {
        /* A U command must originate from primary station */
        cm_flags = 0;
        iohdlc_mutex_unlock(&p->state_mutex);
        continue;
      }

      /* Multipoint: if LINK_REQ but current peer has no pending um_cmd,
         switch to the peer that requested the connection. */
      if ((s_flags & IOHDLC_EVT_LINK_REQ) && p->um_cmd == 0 &&
          !(p->um_state & IOHDLC_UM_SENT)) {
        iohdlc_station_peer_t *target = ioHdlcNextPeer(s, true);
        if (target != NULL && target->um_cmd != 0) {
          iohdlc_mutex_unlock(&p->state_mutex);
          p = target;
          iohdlc_mutex_lock(&p->state_mutex);
        } else {
          /* No peer with pending um_cmd found, ignore. */
          iohdlc_mutex_unlock(&p->state_mutex);
          continue;
        }
      }

            /* Evaluate timer expiry and manage retry counter. */
      if (s_flags & IOHDLC_EVT_C_RPLYTMO) {
        if (!handleTimeoutRetry(s, p)) {
          /* Link down: max retries exceeded, switch to next peer. */
          resetPeerUm(p);
          resetPeerVars(p);
          ioHdlcNextPeer(s, false);
          cm_flags = 0;
          iohdlc_mutex_unlock(&p->state_mutex);
          continue;
        }
        /* Retry: will retransmit below. */
      }

      if (IOHDLC_PEER_DISC(p) &&
            IOHDLC_USE_TWA(s) && isConnectionUCommand(p->um_cmd)) {
        /* In TWA, preset as F received on primary station. */
        s->pf_state |= IOHDLC_F_RCVED;
      }

      if (sendOpportunity(s)) {
        /* Build and send UM command. */
        iohdlc_frame_t *fp = hdlcTakeFrame(&s->frame_pool);
        if (fp != NULL) {
          buildUFrame(s, p, fp, p->um_cmd, true, true);  /* P=1, command */

#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
          /* Extract values for logging before send */
          uint8_t log_addr = IOHDLC_FRAME_ADDR(s, fp);
          iohdlc_log_ufun_t log_fun = (p->um_cmd == IOHDLC_U_SNRM) ? IOHDLC_LOG_SNRM :
                                       (p->um_cmd == IOHDLC_U_SARM) ? IOHDLC_LOG_SARM :
                                       (p->um_cmd == IOHDLC_U_SABM) ? IOHDLC_LOG_SABM :
                                       (p->um_cmd == IOHDLC_U_DISC) ? IOHDLC_LOG_DISC : 0;
#endif
          (void)sendFrame(s, fp);
          
          /* Log U-frame transmission */
          IOHDLC_LOG_UFRAME(IOHDLC_LOG_TX, s->addr, log_addr, log_fun, true);
        }
        
        ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY,
                                  IOHDLC_TIMER_BACKOFF(s, p));
        p->um_state |= IOHDLC_UM_SENT;
        IOHDLC_ACK_F(s);                  /* ack F  */
      } else {
        iohdlc_mutex_unlock(&p->state_mutex);
        continue;
      }
    }

    /* U response: send immediately without sendOpportunity gate.
       A U-response is a reaction to a received command — the peer has
       already finished transmitting and is waiting for our response. */
    if (p->um_state & IOHDLC_UM_RCVED) {
      cm_flags &= ~IOHDLC_EVT_UM_RECVD;
      bool setf = IOHDLC_P_ISRCVED(s);
      p->um_state &= ~IOHDLC_UM_RCVED;  /* ack UM */
      IOHDLC_ACK_P(s);                  /* ack P  */

      /* Build and send UM response. */
      iohdlc_frame_t *fp = hdlcTakeFrame(&s->frame_pool);
      if (fp != NULL) {
        if (p->um_rsp == IOHDLC_U_FRMR)
          buildFrmrResponse(s, p, fp, setf);
        else
          buildUFrame(s, p, fp, p->um_rsp, setf, false);

#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
        /* Extract values for logging before send */
        uint8_t log_addr = IOHDLC_FRAME_ADDR(s, fp);
        iohdlc_log_ufun_t log_fun = (p->um_rsp == IOHDLC_U_UA) ? IOHDLC_LOG_UA :
                                     (p->um_rsp == IOHDLC_U_DM) ? IOHDLC_LOG_DM :
                                     (p->um_rsp == IOHDLC_U_FRMR) ? IOHDLC_LOG_FRMR : 0;
#endif
        (void)sendFrame(s, fp);

        /* Log U-frame transmission */
        IOHDLC_LOG_UFRAME(IOHDLC_LOG_TX, s->addr, log_addr, log_fun, true);

        if (p->um_cmd == IOHDLC_U_DISC) {
          /* DISC has been received: disconnect the link. */
          ioHdlcSetDisconnected(p);           /* Do not reset the queues to allow */
          iohdlc_sem_signal(&p->i_recept_sem);/* the reading of remaining frames*/

          /* Enter disconnected mode only if no other peer connected. */
          if (s->connected_count == 0) {
            s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
            setModeFunctions(s, s->mode);
          }
        }
      }
    }

    cm_flags &= ~(IOHDLC_EVT_LINK_ST_CHG);

    if (IOHDLC_PEER_DISC(p) || IOHDLC_UM_ISSENT(p)) {
      /* Multipoint: if peer is disconnected with no pending UM operations,
         advance to next peer that is connected or has pending um_cmd. */
      if (IOHDLC_IS_PRI(s) && IOHDLC_PEER_DISC(p) &&
          p->um_cmd == 0 && !(p->um_state & IOHDLC_UM_SENT)) {
        iohdlc_mutex_unlock(&p->state_mutex);
        ioHdlcNextPeer(s, false);
        cm_flags = 0;
        continue;
      }
      cm_flags &= IOHDLC_EVT_C_RPLYTMO|IOHDLC_EVT_LINK_REQ;
      iohdlc_mutex_unlock(&p->state_mutex);
      continue;
    }

    if (s->tx_fn)
      cm_flags = s->tx_fn(s, p, cm_flags);
  }

  iohdlc_evt_unregister(&s->cm_es, &s->cm_listener);
  ioHdlcStopReplyTimer(s->c_peer, IOHDLC_TIMER_REPLY);
  ioHdlcStopReplyTimer(s->c_peer, IOHDLC_TIMER_T3);
}

/** @} */
