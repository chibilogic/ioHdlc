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
 * @file    ioHdlc_core.c
 * @brief   OS-agnostic HDLC station core scaffold.
 * @details Provides runner-ops integration, RX/TX loop stubs, and event
 *          mapping. Real protocol logic is progressively migrated from
 *          src/ioHdlc.c.
 */

#include "ioHdlc_core.h"
#include "ioHdlc.h"
#include "ioHdlclist.h"

static const ioHdlcRunnerOps *s_runner_ops = NULL;

/*===========================================================================*/
/* Module local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

/* Broadcast event flags to station event source. */
static void ioHdlcBroadcastFlags(iohdlc_station_t *s, uint32_t flags) {
  if (s_runner_ops && s_runner_ops->broadcast_flags)
    s_runner_ops->broadcast_flags(s, flags);
}

/* Map UM command to supported connection mode. */
static uint8_t um2supportedConnMode(uint8_t um_cmd) {
  if (um_cmd == IOHDLC_U_SABM)
    return IOHDLC_OM_ABM;
  if (um_cmd == IOHDLC_U_SNRM)
    return IOHDLC_OM_NRM;
  if (um_cmd == IOHDLC_U_SARM)
    return IOHDLC_OM_ARM;
  return 0;
}

/* Map supported connection mode to UM command. */
static uint8_t supportedConnMode2um(uint8_t mode) {
  if (mode == IOHDLC_OM_ABM)
    return IOHDLC_U_SABM;
  if (mode == IOHDLC_OM_NRM)
    return IOHDLC_U_SNRM;
  if (mode == IOHDLC_OM_ARM)
    return IOHDLC_U_SARM;
  return 0;
}

/* Clear P/F received flags on station. */
static void ackRcvedP(iohdlc_station_peer_t *p) { p->stationp->pf_state &= ~IOHDLC_P_RCVED; }
static void ackRcvedF(iohdlc_station_peer_t *p) { p->stationp->pf_state &= ~IOHDLC_F_RCVED; }

/* Build and send a UM frame immediately. */
static void sendUMframe(iohdlc_station_t *s, uint32_t addr, uint32_t umcmd) {
  iohdlc_frame_t *fp = hdlcTakeFrame(s->frame_pool);
  if (fp == NULL) return;
  uint8_t *b = fp->frame;
  if (IOHDLC_HAS_FFF(s))
    *b++ = 5; /* FF(1) + ADDR(1) + CMD(1) + FCS(2) */
  *b++ = (uint8_t)addr;
  *b++ = (uint8_t)(umcmd | IOHDLC_U_ID);
  fp->elen = (uint16_t)(b - fp->frame);
  (void)hdlcSendFrame(s->driver, fp);
  hdlcReleaseFrame(s->frame_pool, fp);
}

/* Clear and release all frames in a queue. */
static void clearFrameQ(iohdlc_station_peer_t *p, iohdlc_frame_q_t *q) {
  while (!ioHdlc_frameq_isempty(q)) {
    hdlcReleaseFrame(p->stationp->frame_pool, q->next);
    ioHdlc_frameq_delete(q->next);
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
  
  if (p->poll_retry_count >= p->poll_retry_max) {
    /* Max retries exceeded: declare link down. */
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINKDOWN);
    
    /* Cleanup: stop timers, reset counters, clear U-frame state. */
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_I_REPLY);
    p->poll_retry_count = 0;
    p->um_state &= ~(IOHDLC_UM_SENDING | IOHDLC_UM_SENT);
    
    /* Mark peer disconnected (blocks further transmissions). */
    p->ss_state &= ~IOHDLC_SS_ST_CONN;
    
    return false;  /* Link down, do not retry. */
  }
  
  return true;  /* Retry allowed. */
}

/* Reset peer UM state variables. */
static void resetPeerUm(iohdlc_station_peer_t *p) {
  p->um_state = 0;
  p->um_cmd = 0;
}

/* Reset peer protocol variables, queues and reply timer. */
static void resetPeerVars(iohdlc_station_peer_t *p) {
  ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
  ioHdlcStopReplyTimer(p, IOHDLC_TIMER_I_REPLY);
  p->nr = p->vr = p->vs = 0;
  p->ss_state = 0;
  p->poll_retry_count = 0;
  clearFrameQ(p, &p->i_retrans_q);
  clearFrameQ(p, &p->i_recept_q);
  clearFrameQ(p, &p->i_trans_q);
}

/*
 * @brief Check if there is a send opportunity in NRM mode.
 * @note  In NRM, a send opportunity exists:
 *        - Primary TWA: when F has been received (no P in flight).
 *        - Primary TWS: always permitted.
 *        - Secondary (TWA/TWS): when P has been received.
 * @param[in] s   Station descriptor
 * @return  true if station can transmit based on P/F bit protocol rules
 */
static bool nrmSendOpportunity(iohdlc_station_t *s) {
  return IOHDLC_IS_PRI(s) ?
      (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) : true) :
      IOHDLC_P_ISRCVED(s);
}

/*
 * @brief Check if there is a send opportunity in ABM/ARM modes.
 * @note  In asynchronous modes:
 *        - TWA: send opportunity exists when idle state is detected (IOHDLC_FLG_IDL).
 *        - TWS: send opportunity always exists.
 * @param[in] s   Station descriptor
 * @return  true if station can transmit
 */
static bool abmSendOpportunity(iohdlc_station_t *s) {
  return !IOHDLC_USE_TWA(s) || IOHDLC_ST_IDLE(s);
}

/*
 * @brief Generic send opportunity check (dispatches to mode-specific function).
 * @param[in] s   Station descriptor
 * @return  true if station can transmit
 */
static bool sendOpportunity(iohdlc_station_t *s) {
  return IOHDLC_IS_NRM(s) ? nrmSendOpportunity(s) : abmSendOpportunity(s);
}

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Peer round-robin helper.
 * @details Advances @p s->c_peer to the next peer in the circular list.
 *          If the list is empty returns NULL and leaves @p c_peer unchanged.
 * @param[in,out] s   Station descriptor
 * @return  Next distinct peer, or NULL if there is no next
 */
iohdlc_station_peer_t *ioHdlcNextPeer(iohdlc_station_t *s) {
  IOHDLC_ASSERT(s != NULL, "ioHdlcNextPeer: station is NULL");
  iohdlc_station_peer_t *head = (iohdlc_station_peer_t *)&s->peers;

  if (ioHdlc_peerl_isempty(head))
    return NULL;

  /* Compute next with wrap to list head. */
  s->c_peer = s->c_peer ? s->c_peer->next : head->next;

  return s->c_peer;
}

bool ioHdlcCoreInit(iohdlc_station_t *station) {
  (void)station;
  /* TODO: migrate station initialization logic from src/ioHdlc.c if/when needed. */
  return true;
}

static void handleUFrame(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Handle U-frames common to all modes (NRM, ABM, ARM, etc.).
     Per ISO 13239:
     - Commands (SNRM, SARM, SABM, DISC): processed by secondary/combined
     - Responses (UA, DM, FRMR): processed by primary/combined
     
     RX responsability: validate, register event, notify TX.
     TX responsability: state transitions, variable resets, timer control.
  */
  const uint8_t *foctp = IOHDLC_HAS_FFF(s) ? &fp->frame[1] : &fp->frame[0];
  const uint8_t addr = foctp[0];
  const uint8_t ctrl = foctp[1];
  const uint8_t u_cmd = ctrl & IOHDLC_U_FUN_MASK;
  const bool has_pf = (ctrl & IOHDLC_PF_BIT) != 0;
  
  iohdlc_station_peer_t *p = s->c_peer;
  
  /* Determine if frame is command or response.
     Command: addressed to this station (addr == s->addr)
     Response: from current peer (addr == p->addr) */
  const bool is_command = (addr == s->addr);
  
  /* Validate frame origin matches current peer. */
  if (is_command) {
    /* Command to us: must be from c_peer. */
    if (addr != s->addr) {
      hdlcReleaseFrame(s->frame_pool, fp);
      return;
    }
  } else {
    /* Response from peer: must be from c_peer. */
    if (!p || addr != p->addr) {
      /* Spurious frame from wrong peer or no peer selected → discard. */
      hdlcReleaseFrame(s->frame_pool, fp);
      return;
    }
  }
  
  if (is_command) {
    /* U-frame command received (we are Secondary or Combined in ABM).
       Per 6.11.4.1.3: if already UM_RCVED in progress, ignore additional
       commands until response sent. */
    if (p->um_state & IOHDLC_UM_RCVED) {
      hdlcReleaseFrame(s->frame_pool, fp);
      return;
    }
    
    /* Register command and P bit. */
    p->um_cmd = u_cmd;
    if (has_pf)
      s->pf_state |= IOHDLC_P_RCVED;
    
    /* Determine appropriate response based on command and current state.
       Per 6.11.4.1.1: Secondary validates command and decides response. */
    
    uint8_t om = um2supportedConnMode(u_cmd);
    if (om) {
      /* Set mode command (SNRM/SARM/SABM).
         Per 6.11.4.1.1: Secondary accepts, changes mode, resets variables. */
      s->mode = om;
      resetPeerVars(p);
      p->ss_state |= IOHDLC_SS_ST_CONN;
      p->um_rsp = IOHDLC_U_UA;
      
    } else if (u_cmd == IOHDLC_U_DISC) {
      /* DISC command received. */
      if (IOHDLC_IS_DISC(s)) {
        /* Already disconnected: respond with DM. */
        p->um_rsp = IOHDLC_U_DM;
      } else {
        /* Connected: accept DISC, enter disconnected mode. */
        s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
        resetPeerVars(p);
        p->um_rsp = IOHDLC_U_UA;
      }
      
    } else {
      /* Unsupported or unimplemented command.
         Per 6.11.4.1.3: respond with DM. */
      p->um_rsp = IOHDLC_U_DM;
    }
    
    /* Mark command received and notify TX to send response. */
    p->um_state |= IOHDLC_UM_RCVED;
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_UMRECVD);
    
  } else {
    /* U-frame response received (we are Primary or Combined in ABM).
       Per 6.11.4.1.1/1.2: responses must have F bit set. */
    
    /* Verify we have an outstanding UM command (UM_SENT). */
    if (!(p->um_state & IOHDLC_UM_SENT)) {
      /* Unsolicited response → discard. */
      hdlcReleaseFrame(s->frame_pool, fp);
      return;
    }
    
    /* Verify F bit is set (mandatory for responses per standard). */
    if (!has_pf) {
      /* Missing F bit → protocol error, discard. */
      hdlcReleaseFrame(s->frame_pool, fp);
      return;
    }
    
    /* Valid response received. Process based on response type.
       We know what we requested because um_cmd is still set. */
    
    /* Stop reply timer immediately. */
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
    
    if (u_cmd == IOHDLC_U_UA) {
      /* UA received: command accepted. */
      
      if (p->um_cmd == IOHDLC_U_DISC) {
        /* DISC accepted: enter disconnected mode. */
        s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
        p->ss_state &= ~IOHDLC_SS_ST_CONN;
      } else {
        /* Connection command accepted (SNRM/SARM/SABM).
           Mode was already set by linkup() before sending command. */
        p->ss_state |= IOHDLC_SS_ST_CONN;
      }
      
      /* Reset peer variables and clear UM state. */
      resetPeerVars(p);
      resetPeerUm(p);
      
      /* Notify application (unblock linkup/linkdown). */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_CONNCHG);
      
    } else if (u_cmd == IOHDLC_U_DM) {
      /* DM received: peer disconnected or refused connection. */
      p->ss_state &= ~IOHDLC_SS_ST_CONN;
      p->ss_state |= IOHDLC_SS_ST_DISM;
      resetPeerUm(p);
      
      /* Notify application (unblock linkup/linkdown with error). */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_CONNCHG);
      
    } else if (u_cmd == IOHDLC_U_FRMR) {
      /* FRMR received: peer detected protocol error.
         TODO: Log error, consider link recovery. */
      resetPeerUm(p);
    }
    
    /* Mark F received and clear UM_SENT. */
    s->pf_state |= IOHDLC_F_RCVED;
    p->um_state &= ~IOHDLC_UM_SENT;
  }
  
  hdlcReleaseFrame(s->frame_pool, fp);
}

/*===========================================================================*/
/* NRM RX helper functions.                                                  */
/*===========================================================================*/

static bool seqLessThan(uint32_t seq1, uint32_t seq2, uint32_t modulus) {
  /* Returns true if seq1 < seq2 in modular arithmetic. */
  uint32_t diff = (seq2 - seq1) % modulus;
  return (diff > 0 && diff < modulus / 2);
}

static uint32_t extractNS(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Extract N(S) from control field based on modulus. */
  if (s->modulus == 8) {
    /* Modulo 8: N(S) in bits 5-7 of ctrl[0]. */
    return (fp->ctrl[0] >> 5) & 0x07;
  } else {
    /* Modulo 128: N(S) in bits 1-7 of ctrl[1]. */
    return (fp->ctrl[1] >> 1) & 0x7F;
  }
}

static uint32_t extractNR(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Extract N(R) from control field based on modulus. */
  if (s->modulus == 8) {
    /* Modulo 8: N(R) in bits 1-3 of ctrl[0]. */
    return (fp->ctrl[0] >> 1) & 0x07;
  } else {
    /* Modulo 128: N(R) in bits 1-7 of ctrl[2]. */
    return (fp->ctrl[2] >> 1) & 0x7F;
  }
}

static void processNR(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                      uint32_t nr) {
  /* Remove acknowledged frames from retransmission queue.
     Frames with N(S) < nr have been received by peer. */
  
  while (p->nr != nr && !ioHdlc_frameq_isempty(&p->i_retrans_q)) {
    iohdlc_frame_t *acked_fp = ioHdlc_frameq_remove(&p->i_retrans_q);
    hdlcReleaseFrame(s->frame_pool, acked_fp);
    p->nr = (p->nr + 1) % s->modulus;
  }
  
  /* Update last acknowledged N(R). */
  p->nr = nr;
}

static void checkpointRetransmit(iohdlc_station_t *s, iohdlc_station_peer_t *p) {
  /* Move frames from i_retrans_q back to i_trans_q for checkpoint retransmission.
     Per ISO 13239 5.6.2.1: Both Primary and Secondary do this.
     
     Strategy: Find the range of frames in i_retrans_q with N(S) < vs_atlast_pf
     and move them to the head of i_trans_q using ioHdlc_frameq_move().
  */
  
  if (ioHdlc_frameq_isempty(&p->i_retrans_q))
    return;  // Nothing to retransmit
  
  /* Find the first frame that needs checkpoint retransmission. */
  iohdlc_frame_t *first_fp = NULL;
  iohdlc_frame_t *last_fp = NULL;
  iohdlc_frame_t *fp = p->i_retrans_q.next;
  
  /* Scan the retransmission queue to find frames with N(S) < vs_atlast_pf. */
  while (fp != (iohdlc_frame_t *)&p->i_retrans_q) {
    uint32_t frame_ns = extractNS(s, fp);
    
    if (seqLessThan(frame_ns, p->vs_atlast_pf, s->modulus)) {
      /* This frame needs checkpoint retransmission. */
      if (first_fp == NULL)
        first_fp = fp;  // Mark first frame
      last_fp = fp;     // Update last frame
      fp = fp->next;    // Continue scanning
    } else {
      /* Found a frame sent after checkpoint: stop here. */
      break;
    }
  }
  
  /* If we found frames to retransmit, move them to i_trans_q. */
  if (first_fp != NULL) {
    ioHdlc_frameq_move(&p->i_trans_q, first_fp, last_fp);
  }
}

static void handleCheckpointAndAck(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                                   iohdlc_frame_t *fp) {
  /* Common processing for both I-frames and S-frames:
     1. Process N(R) to acknowledge our sent frames
     2. Handle P/F bit for checkpointing (both Primary and Secondary)
     3. Manage reply timer based on role and P/F bit
     
     TODO: Verify thread synchronization between RX and TX.
           The sequence processNR() → handleSFrame(REJ) → checkpoint must be
           atomic relative to TX to prevent race conditions where TX acks frames
           before rej_actioned is set, potentially leaving rej_actioned orphaned.
  */
  
  uint8_t ctrl = fp->ctrl[0];
  uint32_t nr = extractNR(s, fp);
  bool pf = (ctrl & IOHDLC_PF_BIT) != 0;
  
  /* 1. ALWAYS process N(R) to acknowledge our sent frames. */
  processNR(s, p, nr);
  
  /* 2. ALWAYS handle P/F bit for checkpointing (independent of frame type). */
  if (pf) {
    /* Both Primary and Secondary do checkpoint operations. */
    
    /* Checkpoint retransmission: retransmit unacknowledged frames
       with N(S) < vs_atlast_pf. Move them from i_retrans_q to i_trans_q.
       
       Note: The checkpoint verification is implicit in this function.
       If N(R) >= vs_atlast_pf, processNR() has already removed all frames
       from i_retrans_q and checkpointRetransmit() will find nothing to move.
       If N(R) < vs_atlast_pf, frames remain in i_retrans_q and will be
       moved to i_trans_q for retransmission (error recovery). 
       
       ISO 13239 5.6.2.1 case a): checkpoint inhibited if REJ already actioned.
       Also inhibited if rej_actioned would be in checkpoint range. */
    if (p->rej_actioned == 0) {
      /* No REJ active: checkpoint can proceed. */
      checkpointRetransmit(s, p);
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_CHKPT);
    } else {
      /* REJ active (rej_actioned = x means N(R) = x-1 in REJ recovery).
         Checkpoint only if N(R) of REJ not in checkpoint range.
         Checkpoint would retransmit frames with N(S) < vs_atlast_pf.
         If (rej_actioned-1) >= vs_atlast_pf, REJ range doesn't overlap. */
      uint32_t rej_nr = p->rej_actioned - 1;
      if (!seqLessThan(rej_nr, p->vs_atlast_pf, s->modulus)) {
        /* REJ N(R) is outside checkpoint range: safe to checkpoint. */
        checkpointRetransmit(s, p);
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_CHKPT);
      }
      /* Otherwise, REJ recovery is handling these frames: skip checkpoint. */
    }
    
    /* C. Role-specific P/F handling. */
    if (IOHDLC_IS_PRI(s)) {
      /* Primary received F=1: acknowledge poll and stop timer. */
      s->pf_state |= IOHDLC_F_RCVED;
      p->poll_retry_count = 0;
      ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
      
    } else {
      /* Secondary received P=1: must respond with F=1. */
      s->pf_state |= IOHDLC_P_RCVED;
    }
    
  } else {
    /* pf == false (F=0 for Primary, P=0 for Secondary) */
    
    if (IOHDLC_IS_PRI(s)) {
      /* Primary received F=0: peer sent something but not final response yet.
         Restart timer to keep waiting for F=1. */
      if (!IOHDLC_F_ISRCVED(s)) {
        /* Only restart if we're still waiting for F (P is outstanding). */
        ioHdlcRestartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
      }
    }
  }
}

static void handleIFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                         iohdlc_frame_t *fp) {
  /* Handle I-frame specific logic:
     - Validate sequence number N(S)
     - Enqueue frame for application if valid
     - Generate REJ if out-of-sequence
  */
  
  uint32_t ns = extractNS(s, fp);
  
  /* Validate N(S) against V(R). */
  if (ns != p->vr) {
    /* Out-of-sequence error detected. */
    hdlcReleaseFrame(s->frame_pool, fp);
    
    /* Send REJ to request retransmission.
       ISO 13239 5.6.2.1 case a): only one REJ at a time.
       If REJ already actioned, first REJ will retransmit all needed frames. */
    if (!IOHDLC_USE_TWA(s) && p->rej_actioned == 0) {
      p->ss_fun = IOHDLC_S_REJ;
      p->ss_nr = p->vr;
      p->rej_actioned = p->vr + 1;  /* Mark REJ as actioned (value = N(R) + 1) */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_SSNDREQ);
    }
    
    return;
  }
  
  /* Frame is in sequence: enqueue for application. */
  ioHdlc_frameq_insert(&p->i_recept_q, fp);
  
  /* Clear REJ exception if this is the frame that completes recovery.
     rej_actioned = x means waiting for frame with N(S) = x-1. */
  if (p->rej_actioned != 0 && ns == (p->rej_actioned - 1)) {
    p->rej_actioned = 0;
  }
  
  // TODO: Check if queue is approaching full (watermark) to send RNR
  // For now, assume queue has enough space (allocated for kr+1 frames)
  
  /* Increment V(R) - frame accepted. */
  p->vr = (p->vr + 1) % s->modulus;
}

static void handleSFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                         iohdlc_frame_t *fp) {
  /* Handle S-frame specific logic:
     - Update peer busy state (RR/RNR)
     - Process REJ for retransmission
     - Ignore SREJ (not implemented)
  */
  
  uint8_t ctrl = fp->ctrl[0];
  uint8_t s_fun = ctrl & IOHDLC_S_FUN_MASK;
  
  /* Handle S-frame function. */
  switch (s_fun) {
    case IOHDLC_S_RR:
      /* Peer ready to receive: clear busy flag. */
      p->ss_state &= ~IOHDLC_SS_BUSY;
      break;
      
    case IOHDLC_S_RNR:
      /* Peer not ready: set busy flag. */
      p->ss_state |= IOHDLC_SS_BUSY;
      break;
      
    case IOHDLC_S_REJ:
      /* Peer requests retransmission from N(R).
         processNR() has already removed frames with N(S) < nr from i_retrans_q.
         Now move remaining frames (N(S) >= nr) from i_retrans_q to head of i_trans_q.
         TX will take frames from i_trans_q (never from i_retrans_q directly).
         
         Mark REJ as actioned to:
         1. Prevent duplicate REJ (same or different N(R))
         2. Inhibit checkpoint retransmission of overlapping frames
         
         ISO 13239 5.6.2.1 case a): first REJ retransmits all needed frames. */
      if (!IOHDLC_USE_TWA(s)) {
        uint32_t nr = extractNR(s, fp);
        
        /* Move frames from i_retrans_q to head of i_trans_q for retransmission. */
        ioHdlc_frameq_move(&p->i_retrans_q, &p->i_trans_q, true);
        
        /* Mark REJ as actioned (value = N(R) + 1 to distinguish from 0 = not actioned). */
        p->rej_actioned = nr + 1;
        
        /* Signal retransmission event (TX will send from i_trans_q). */
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_SSNDREQ);
      }
      break;
      
    case IOHDLC_S_SREJ:
      /* Selective reject: not implemented. Ignore. */
      break;
  }
  
  hdlcReleaseFrame(s->frame_pool, fp);
}

static void nrmRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Handle I-frames and S-frames specific to NRM mode. */
  
  iohdlc_station_peer_t *p;
  uint32_t addr;
  uint8_t ctrl;
  
  /* Decode address and control. */
  addr = fp->addr;
  ctrl = fp->ctrl[0];
  
  /* Identify peer from address. */
  p = addr2peer(s, addr);
  if (p == NULL) {
    hdlcReleaseFrame(s->frame_pool, fp);
    return;
  }
  
  /* Common checkpoint and acknowledgment processing for all I/S frames. */
  handleCheckpointAndAck(s, p, fp);
  
  /* Branch by frame type for specific handling. */
  if (IOHDLC_IS_I_FRM(ctrl)) {
    handleIFrame(s, p, fp);
  } else if (IOHDLC_IS_S_FRM(ctrl)) {
    handleSFrame(s, p, fp);
  } else {
    /* Unexpected frame type (U-frames already handled upstream). */
    hdlcReleaseFrame(s->frame_pool, fp);
  }
}

static uint32_t nrmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags) {

  /* Check if a S is requested
     NRM NOTE:
      In TWA, sending (S)REJ has poor utility. So we choose to send
      only RR and RNR and the recption of REJ in TWA is ignored.
           
     The sending of an S frame can be requested by receiver in these cases:
        receiver detects an out of sequence error (REJ) if not TWA
        receiver I-frame queue is (almost) full (RNR)
     It can be requested by the I-frame consumer when a full I-frame queue
     becomes receptive again (RR)
     It can be requested by this TX itself, if it have no I-frame to txmit, or
     following a poll timeout (RR or RNR), or if the transmission window 
     is full.*/

  if ((cm_flags & IOHDLC_EVT_SSNDREQ) || (p->ss_state & IOHDLC_SS_SENDING)) {
    cm_flags &= ~IOHDLC_EVT_SSNDREQ;
    p->ss_state |= IOHDLC_SS_SENDING;

    if (nrmSendOpportunity(s)) {
      const uint32_t outstanding = (p->vs >= p->nr) ?
          (p->vs - p->nr) : (p->ks - (p->nr - p->vs) + 1);
      const bool window_full = outstanding >= p->ks;
      const bool no_i_frame = ioHdlc_frameq_isempty(&p->i_trans_q) || window_full;
      bool set_pf = IOHDLC_IS_PRI(s) ?
        (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) && no_i_frame : IOHDLC_F_ISRCVED(s)) :
        (IOHDLC_P_ISRCVED(s) && no_i_frame);
      /* TODO: Build and send S frame p->ss_fun.
                Set P/F to set_pf.*/
      p->ss_state &= ~IOHDLC_SS_SENDING;
      if (set_pf) {
        if (IOHDLC_IS_PRI(s)) {
          /* If sent P, ack F and start the timer.*/
          IOHDLC_ACK_F(s);
          ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
        } else
          IOHDLC_ACK_P(s);
        /* Update checkpoint reference.*/
        p->vs_atlast_pf = p->vs;
      }
    } else {

      /* if cannot send S now, we cannot send any other type of frame too,
          so retry later.*/
      return cm_flags;
    }
  }

  /* I-frame transmission: follow NRM rules (TWA vs TWS, Primary vs Secondary). */
  
  if (cm_flags & IOHDLC_EVT_C_RPLYTMO) {
    /* Poll/reply timeout occurred: handle retry logic. */
    cm_flags &= ~IOHDLC_EVT_C_RPLYTMO;
    
    if (!handleTimeoutRetry(s, p)) {
      /* Link down: max retries exceeded, cannot send I-frames. */
      return cm_flags;
    }
    
    /* Retry: force P bit to be sent on next I-frame. */
    s->pf_state |= IOHDLC_F_RCVED;
  }

  bool i_frame_sent = false;  /* Track if at least one I-frame was sent. */
  while (!ioHdlc_frameq_isempty(&p->i_trans_q)) {
    /* Check transmission window availability. */
    const uint32_t outstanding = (p->vs >= p->nr) ?
        (p->vs - p->nr) : (p->ks - (p->nr - p->vs) + 1);
    if (outstanding >= p->ks)
      /* Window full: cannot send I-frames. */
      break;

    if (!nrmSendOpportunity(s))
      /* Cannot send I-frame now (TWA waiting for P/F bit). */
      break;
  
    /* Poll for new events before sending each I-frame.
       This allows interrupting the burst if critical events occur. */
    cm_flags |= s_runner_ops->get_events_flags(s);
    
    /* Check for critical events requiring immediate attention:
       - IOHDLC_EVT_UMRECVD: U-frame received (disconnect/mode change)
       - IOHDLC_EVT_LINKDOWN: Link failure detected
       - IOHDLC_EVT_SSNDREQ: Urgent S-frame requested (local busy condition)
       - IOHDLC_PEER_BUSY: Peer went into RNR state */
    if (cm_flags & IOHDLC_EVT_LINKDOWN) {
      /* Link is down: abort transmission immediately. */
      return cm_flags;
    }
    
    if ((cm_flags & (IOHDLC_EVT_UMRECVD | IOHDLC_EVT_SSNDREQ)) ||
        IOHDLC_PEER_BUSY(p)) {
      /* Critical event detected: exit I-frame loop immediately. */
      break;
    }

    /* Extract frame from transmission queue with lookahead. */
    iohdlc_frame_t *next_fp = NULL;
    iohdlc_frame_t *fp = ioHdlc_frameq_remove_la(&p->i_trans_q, &next_fp);
    if (fp == NULL) break;  /* Safety check. */

    /* Determine whether to set P/F bit.
       Primary TWA: Set P on last I-frame (always, we have the link).
       Primary TWS: Set P as soon as possible (if no P in flight).
       Secondary (TWA & TWS): Set F on last I-frame (when window will be full or no more frames). */
    bool set_pf = false;
    const bool is_last_frame = ((outstanding + 1) >= p->ks) || (next_fp == NULL);
    
    if (IOHDLC_IS_PRI(s)) {
      /* Primary: behavior differs between TWA and TWS.
         TWA: set P on last frame (we have the link).
         TWS: set P as soon as possible (if no P in flight). */
      set_pf = IOHDLC_USE_TWA(s) ? is_last_frame : IOHDLC_F_ISRCVED(s); 
    } else {
      /* Secondary: always set F on last frame (both TWA and TWS). */
      set_pf = is_last_frame;
    }

    /* TODO: Build and send I-frame:
       - Set N(S) = p->vs
       - Set N(R) = p->vr
       - Set P/F = set_pf
       - Transmit frame */

    /* Mark that we sent at least one I-frame. */
    i_frame_sent = true;

    /* Move frame to retransmission queue. */
    ioHdlc_frameq_insert(&p->i_retrans_q, fp);

    /* Advance V(S). */
    p->vs = (p->vs + 1) % (p->ks + 1);

    /* Handle P/F state. */
    if (set_pf) {
      if (IOHDLC_IS_PRI(s)) {
        /* Primary sent P: ack any received F and start command reply timer. */
        IOHDLC_ACK_F(s);
        ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
      } else {
        /* Secondary sent F: ack received P. */
        IOHDLC_ACK_P(s);
      }
      /* Update checkpoint reference. */
      p->vs_atlast_pf = p->vs;
    }

    /* In TWA, stop after sending one frame with F (secondary) or P (primary). */
    if (IOHDLC_USE_TWA(s) && set_pf)
      break;
  }

  /* If no I-frame was sent but we have the opportunity/need to respond,
     prepare to send an opportunistic S-frame (RR or RNR). */
  if (!i_frame_sent) {
    /* In TWA, if we still have permission on the link but didn't send I-frames,
       we should send an S-frame to acknowledge and cede the link.
       In TWS, we may also want to send periodic acknowledgments. */
    if (nrmSendOpportunity(s)) {
      /* Determine S-frame function: RR or RNR based on reception queue state. */
      p->ss_fun = IOHDLC_ST_BUSY(s) ? IOHDLC_S_RNR : IOHDLC_S_RR;

      /* Raise event to trigger S-frame transmission. */
      cm_flags |= IOHDLC_EVT_SSNDREQ;
    }
  }

  return cm_flags;
}

static uint32_t armTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags);

static uint32_t abmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags) {
  /* S requested
     NOTE per ABM:
        Come distinguere tra command e response, in ordine di priorità:
        se devo inviare un P è command.
        se devo inviare un F è response.
        se !IOHDLC_P_ISRCVED(s) è response.
        se IOHDLC_P_ISRCVED(s) è command.*/    
  return 0;
}

void ioHdlcTxEntry(void *stationp) {
  iohdlc_station_t *s = (iohdlc_station_t *)stationp;
  iohdlc_station_peer_t *p;
  const uint32_t mask = 0;
  uint32_t cm_flags = 0;
  bool r;

  if (!s) return;
  for (;;) {
    if (!cm_flags) {
      if (s_runner_ops && s_runner_ops->wait_events)
        cm_flags = s_runner_ops->wait_events(s, mask);
      else
        break; /* cannot wait, exit */
    }
    /* Proceed by priority. U -> S -> I -> OS.*/
    p = s->c_peer;
    if (NULL == p) { cm_flags = 0; continue; }

    /* U response */
    if (p->um_state & IOHDLC_UM_RCVED) {
      /* If an unnumbered command has been received, the um_rsp field contains
         the response to send, valued by the receiver on the received
         UM command basis.*/
      cm_flags &= ~IOHDLC_EVT_UMRECVD;        /* serve the event, if any.*/
      if (sendOpportunity(s)) {
        p->um_state &= ~IOHDLC_UM_RCVED;  /* ack UM */
        IOHDLC_ACK_P(s);                  /* ack P  */
        /* TODO: Build and send UM response p->um_rsp.*/
      } else
        continue;
    }

    /* U command */
    if ((cm_flags & IOHDLC_EVT_CONNSTR) || (p->um_state & IOHDLC_UM_SENDING) || 
        (p->um_state & IOHDLC_UM_SENT) &&
            (r = ioHdlcIsReplyTimerExpired(p, IOHDLC_TIMER_REPLY))) {

      cm_flags &= ~(IOHDLC_EVT_CONNSTR |
                    IOHDLC_EVT_C_RPLYTMO);  /* serve all the possible events.*/
      
      /* Evaluate timer expiry and manage retry counter. */
      if (r) {
        if (!handleTimeoutRetry(s, p)) {
          /* Link down: max retries exceeded, switch to next peer. */
          ioHdlcNextPeer(s);
          continue;
        }
        /* Retry: will retransmit below. */
      }

      /* A link management has been requested.*/
      p->um_state |= IOHDLC_UM_SENDING;
      if (sendOpportunity(s)) {
        /* TODO: Build and send UM command p->um_cmd.*/
        ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
        p->um_state &= ~IOHDLC_UM_SENDING;
        p->um_state |= IOHDLC_UM_SENT;
        IOHDLC_ACK_F(s);                  /* ack F  */
      }
    }
    if (IOHDLC_UM_INPROG(p))
      continue;

    if (s->tx_fn)
      cm_flags = s->tx_fn(s, p, cm_flags);
  }
}

void ioHdlcRxEntry(void *stationp) {
  iohdlc_station_t *s = (iohdlc_station_t *)stationp;
  if (!s) return;
  for (;;) {
    iohdlc_frame_t *fp = hdlcRecvFrame(s->driver, (iohdlc_timeout_t)0xFFFFFFFFu);
    if (fp == NULL) {
      /* Idle line */
      s->flags |= IOHDLC_FLG_IDL;
      if (s->flags & IOHDLC_FLG_TWA) {
        /* In TWA mode, line idle might be significant - broadcast event. */
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINIDLE);
      }
      continue;
    }
    s->flags &= ~IOHDLC_FLG_IDL;
    
    /* Decode control octet to determine frame type. */
    const uint8_t *foctp = IOHDLC_HAS_FFF(s) ? &fp->frame[1] : &fp->frame[0];
    const uint8_t ctrl = foctp[1];
    
    /* Handle U-frames common to all modes. */
    if (IOHDLC_IS_U_FRM(ctrl)) {
      handleUFrame(s, fp);
      continue;
    }
    
    /* Call mode-specific RX handler for I and S frames. */
    s->rx_fn(s, fp);
  }
}

void ioHdlcRegisterRunnerOps(const ioHdlcRunnerOps *ops) {
  s_runner_ops = ops;
}

void ioHdlcStartReplyTimer(iohdlc_station_peer_t *peer,
    iohdlc_timer_kind_t timer_kind, uint32_t timeout_ms) {
  if (s_runner_ops && s_runner_ops->start_reply_timer)
    s_runner_ops->start_reply_timer(peer, timer_kind, timeout_ms);
}

void ioHdlcRestartReplyTimer(iohdlc_station_peer_t *peer,
    iohdlc_timer_kind_t timer_kind, uint32_t timeout_ms) {
  if (s_runner_ops && s_runner_ops->restart_reply_timer)
    s_runner_ops->restart_reply_timer(peer, timer_kind, timeout_ms);
}

void ioHdlcStopReplyTimer(iohdlc_station_peer_t *peer,
    iohdlc_timer_kind_t timer_kind) {
  if (s_runner_ops && s_runner_ops->stop_reply_timer)
    s_runner_ops->stop_reply_timer(peer, timer_kind);
}

bool ioHdlcIsReplyTimerExpired(iohdlc_station_peer_t *peer,
    iohdlc_timer_kind_t timer_kind) {
  if (s_runner_ops && s_runner_ops->is_reply_timer_expired)
    return s_runner_ops->is_reply_timer_expired(peer, timer_kind);
  return false;
}
