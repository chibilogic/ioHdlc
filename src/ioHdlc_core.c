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
 * @file    ioHdlc_core.c
 * @brief   ISO 13239 HDLC protocol core implementation.
 * @details Implements HDLC station core functionality including:
 *          - NRM (Normal Response Mode)
 *          - I-frame, S-frame, and U-frame handling
 *          - Sequence number validation (N(S), N(R))
 *          - Checkpoint retransmission (ISO 13239 5.6.2.1)
 *          - REJ exception handling with overlap detection
 *          - Runner-ops integration for OS abstraction
 */

#include "ioHdlc_core.h"
#include "ioHdlc.h"
#include "ioHdlc_app_events.h"
#include "ioHdlc_log.h"
#include "ioHdlclist.h"
#include "ioHdlcosal.h"
#include <errno.h>
#include <stdio.h>

/* Runner ops pointer (shared with ioHdlc.c for broadcast_flags access) */
const ioHdlcRunnerOps *s_runner_ops = NULL;

/* Forward declarations for U-frame handler */
static void handleUFrame(iohdlc_station_t *s, iohdlc_frame_t *fp);

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
void ioHdlcBroadcastFlags(iohdlc_station_t *s, uint32_t flags) {
  if (s_runner_ops && s_runner_ops->broadcast_flags)
    s_runner_ops->broadcast_flags(s, flags);
}

static void ioHdlcBroadcastFlagsApp(iohdlc_station_t *s, uint32_t flags) {
  if (s_runner_ops && s_runner_ops->broadcast_flags)
    s_runner_ops->broadcast_flags_app(s, flags);
}

/* Clear and release all frames in a queue. */
static void clearFrameQ(iohdlc_station_peer_t *p, iohdlc_frame_q_t *q) {
  while (!ioHdlc_frameq_isempty(q)) {
    iohdlc_frame_t *fp = q->next;
    ioHdlc_frameq_delete(fp);
    hdlcReleaseFrame(p->stationp->frame_pool, fp);
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
  p->i_pending_count = 0;
  clearFrameQ(p, &p->i_retrans_q);
  clearFrameQ(p, &p->i_recept_q);
  clearFrameQ(p, &p->i_trans_q);
}

/* Set tx_fn and rx_fn based on mode, or reset to NULL if disconnected. */
static void setModeFunctions(iohdlc_station_t *s, uint8_t mode) {
  if (mode == IOHDLC_OM_NRM) {
    s->tx_fn = nrmTx;
    s->rx_fn = nrmRx;
  } else if (mode == IOHDLC_OM_ARM) {
    s->tx_fn = armTx;
    s->rx_fn = armRx;
  } else if (mode == IOHDLC_OM_ABM) {
    s->tx_fn = abmTx;
    s->rx_fn = abmRx;
  } else {
    /* Disconnected modes (NDM, ADM): reset to NULL */
    s->tx_fn = NULL;
    s->rx_fn = NULL;
  }
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
static bool sendOpportunity(iohdlc_station_t *s, uint32_t *flags) {
  *flags &= ~IOHDLC_EVT_LINIDLE;
  return IOHDLC_IS_NRM(s) ? nrmSendOpportunity(s) : abmSendOpportunity(s);
}

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Peer round-robin helper.
 * @details Advances @p s->c_peer to the next peer in the circular list.
 *          If the list is empty returns NULL and leaves @p c_peer unchanged.
 * @param[in] s   Station descriptor
 * @return  Next distinct peer, or NULL if there is no next
 */
iohdlc_station_peer_t *ioHdlcNextPeer(iohdlc_station_t *s) {
  IOHDLC_ASSERT(s != NULL, "ioHdlcNextPeer: station is NULL");
  iohdlc_station_peer_t *head = (iohdlc_station_peer_t *)&s->peers;

  if (ioHdlc_peerl_isempty((const iohdlc_peer_list_t *)head))
    return NULL;

  /* Compute next with wrap to list head. */
  s->c_peer = s->c_peer->next;
  if (s->c_peer == head)
    s->c_peer = head->next;

  return s->c_peer;
}

static void handleUFrame(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Handle U-frames common to all modes (NRM, ABM, ARM, etc.).
     Per ISO 13239:
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
  
  iohdlc_mutex_lock(&p->state_mutex);
  if (is_command) {
    /* U-frame command received (we are Secondary or Combined in ABM).
       Per 6.11.4.1.3: if already UM_RCVED in progress, ignore additional
       commands until response sent. */
    if (p->um_state & IOHDLC_UM_RCVED) {
      hdlcReleaseFrame(s->frame_pool, fp);
      iohdlc_mutex_unlock(&p->state_mutex);
      return;
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
      p->ss_state |= IOHDLC_SS_ST_CONN;
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
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_UMRECVD);
    
  } else {
    /* U-frame response received (we are Primary or Combined in ABM).
       Per 6.11.4.1.1/1.2: responses must have F bit set. */
    
    /* Verify we have an outstanding UM command (UM_SENT). */
    if (!(p->um_state & IOHDLC_UM_SENT)) {
      /* Unsolicited response → discard. */
      hdlcReleaseFrame(s->frame_pool, fp);
      iohdlc_mutex_unlock(&p->state_mutex);
      return;
    }
    
    /* Verify F bit is set (mandatory for responses). */
    if (!has_pf) {
      /* Missing F bit → protocol error, discard. */
      hdlcReleaseFrame(s->frame_pool, fp);
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
        /* DISC accepted: enter disconnected mode. */
        s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
        setModeFunctions(s, s->mode);  /* Reset to NULL */
        p->ss_state &= ~IOHDLC_SS_ST_CONN;
      } else {
        /* Connection command accepted (SNRM/SARM/SABM).
           Mode was already set by linkup() before sending command. */
        setModeFunctions(s, s->mode);
        p->ss_state |= IOHDLC_SS_ST_CONN;
      }
      
      uint8_t cmd = p->um_cmd;  /* Save command for app notification */
      /* Clear UM state. */
      resetPeerUm(p);
      
      /* Notify core internal events. */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_CONNCHG);
      
      /* Notify application: determine if link up or link down based on um_cmd. */
      ioHdlcBroadcastFlagsApp(s, (cmd == IOHDLC_U_DISC) ? 
                            IOHDLC_APP_LINK_DOWN : IOHDLC_APP_LINK_UP);
      
    } else if (u_cmd == IOHDLC_U_DM) {
      /* DM received: peer disconnected or refused connection. */
      p->ss_state &= ~IOHDLC_SS_ST_CONN;
      p->ss_state |= IOHDLC_SS_ST_DISM;
      resetPeerUm(p);
      
      /* Notify core internal events. */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_CONNCHG);
      
      /* Notify application: DM means link refused or link down. */
      s_runner_ops->broadcast_flags_app(s, (p->um_state & IOHDLC_UM_SENT) ? 
                            IOHDLC_APP_LINK_REFUSED : IOHDLC_APP_LINK_LOST);
      
    } else if (u_cmd == IOHDLC_U_FRMR) {
      /* FRMR received: peer detected protocol error.
         TODO: Log error, consider link recovery. */
      resetPeerUm(p);
    }
    
    /* Mark F received and clear UM_SENT. */
    s->pf_state |= IOHDLC_F_RCVED;
    p->um_state &= ~IOHDLC_UM_SENT;
  }
  
  iohdlc_mutex_unlock(&p->state_mutex);
  hdlcReleaseFrame(s->frame_pool, fp);
}

/*===========================================================================*/
/* NRM RX helper functions.                                                  */
/*===========================================================================*/

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

static bool isNRValid(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                      uint32_t nr) {
  /* N(R) is INVALID if it identifies:
     a) An I-frame previously transmitted and acknowledged (nr < p->nr)
     b) An I-frame not transmitted (nr > p->vs)
     
     Valid range: p->nr <= nr <= p->vs (in modular arithmetic)
     Equivalent: distance from p->nr to nr must be <= distance from p->nr to p->vs */
  
  uint32_t nr_offset = (nr - p->nr) & s->modmask;     /* Distance from last ACK to new N(R) */
  uint32_t vs_offset = (p->vs - p->nr) & s->modmask;  /* Distance from last ACK to next to send */
  
  return nr_offset <= vs_offset;
}

static bool processNR(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                      uint32_t nr) {
  /* Remove acknowledged frames from retransmission queue.
     Frames with N(S) < nr have been received by peer.
     
     Also clear checkpoint/REJ state if their tracked frames are ACKed.
     
     NOTE: Caller must hold state_mutex when calling this function.
     Returns true if semaphore should be signaled (space became available).
  */
  
  bool released_frames = false;
  
  while (p->nr != nr && !ioHdlc_frameq_isempty(&p->i_retrans_q)) {
    iohdlc_frame_t *acked_fp = ioHdlc_frameq_remove(&p->i_retrans_q);
    uint32_t acked_ns = extractNS(s, acked_fp);

    /* TODO: maybe we should check acked_ns vs p->nr */
    
    /* Clear checkpoint state if this was the tracked frame */
    if (p->chkpt_actioned != 0 && (p->chkpt_actioned - 1) == acked_ns) {
      p->chkpt_actioned = 0;
    }
    
    /* Clear REJ state if this was the tracked frame */
    if (p->rej_actioned != 0 && (p->rej_actioned - 1) == acked_ns) {
      p->rej_actioned = 0;
    }
    
    hdlcReleaseFrame(s->frame_pool, acked_fp);
    p->i_pending_count--;  /* Decrement pending counter */
    released_frames = true;
    /* Increment with modmask */
    p->nr = (p->nr + 1) & s->modmask;
    if (acked_ns == p->vs_atlast_pf)
      p->vs_atlast_pf = (p->vs_atlast_pf + 1) & s->modmask;
  }
  
  /* Update last acknowledged N(R). */
  p->nr = nr;
  
  /* Return true if should signal semaphore.
     Caller will signal outside the lock. */
  return released_frames &&
         (p->i_pending_count < (2 * p->ks)) &&
         (hdlcPoolGetState(s->frame_pool) == IOHDLC_POOL_NORMAL);
}

static bool checkpointRetransmit(iohdlc_station_t *s, iohdlc_station_peer_t *p) {
  /* Move frames from i_retrans_q back to i_trans_q for checkpoint retransmission.
     Per ISO 13239 5.6.2.1: Both Primary and Secondary do this.
     
     ISO 13239 5.6.2.1 case a): An actioned REJ with P/F=0 inhibits checkpoint
     retransmission if it would retransmit the same I-frame.
     
     Returns true if frames were moved, false otherwise.
     
     NOTE: Caller must hold state_mutex when calling this function.
  */
  
  if (ioHdlc_frameq_isempty(&p->i_retrans_q))
    return false;  /* Nothing to retransmit */
  
  /* Find the first frame that needs checkpoint retransmission. */
  iohdlc_frame_t *first_fp = NULL;
  iohdlc_frame_t *last_fp = NULL;
  iohdlc_frame_t *fp = p->i_retrans_q.next;
  uint32_t first_ns = 0;  /* N(S) of first frame to retransmit */
  
  /* Scan the retransmission queue to find frames with N(S) < vs_atlast_pf.
     Since i_retrans_q is FIFO and contiguous, we scan until we find 
     the frame with N(S) == vs_atlast_pf (excluded from retransmission).
     
     Check for REJ overlap: if rej_actioned != 0, it contains N(R)+1 from the REJ.
     If we find a frame with N(S) == N(R), inhibit entire checkpoint. */
  while (fp != (iohdlc_frame_t *)&p->i_retrans_q) {
    uint32_t frame_ns = extractNS(s, fp);
    
    if (frame_ns == p->vs_atlast_pf) {
      /* Found checkpoint frame: stop here (don't include it). */
      break;
    }
    
    /* ISO 13239 5.6.2.1 case a): Check if this frame matches REJ N(R). */
    if (p->rej_actioned != 0 && frame_ns == (p->rej_actioned - 1)) {
      /* This frame would be retransmitted by both checkpoint AND REJ.
         Inhibit entire checkpoint retransmission per standard. */
      return false;
    }
    
    /* This frame was sent before checkpoint: mark for retransmission. */
    if (first_fp == NULL) {
      first_fp = fp;    // Mark first frame
      first_ns = frame_ns;  // Save its N(S)
    }
    last_fp = fp;     // Update last frame
    fp = fp->next;    // Continue scanning
  }
  
  /* If we found frames to retransmit, move them to i_trans_q. */
  if (first_fp != NULL) {
    ioHdlc_frameq_move(&p->i_trans_q, first_fp, last_fp);
    
    /* Mark checkpoint as actioned with first frame N(S).
       Used to detect overlap with subsequent REJ (5.6.2.2). */
    p->chkpt_actioned = first_ns + 1;
    p->vs = first_ns;  /* Reset V(S) to first retransmit frame N(S) */
    
    return true;
  }
  
  return false;
}

static void handleCheckpointAndAck(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                                   iohdlc_frame_t *fp) {
  /* Common processing for both I-frames and S-frames:
     1. Process N(R) to acknowledge our sent frames
     2. Handle P/F bit for checkpointing (both Primary and Secondary)
     3. Manage reply timer based on role and P/F bit
     
     THREAD SAFETY: The sequence processNR() → checkpoint/REJ handling must be
     atomic relative to TX. The mutex is held across the entire sequence to
     prevent TX from observing intermediate state.
  */
  
  uint32_t nr = extractNR(s, fp);
  bool pf = IOHDLC_FRAME_GET_PF(s, fp);
  
  /* Validate N(R) before processing.*/
  if (!isNRValid(s, p, nr)) {
    /* Protocol error: invalid N(R) received.
       TODO: Send FRMR with Y bit set (invalid N(R)).
       For now: discard frame. */
    hdlcReleaseFrame(s->frame_pool, fp);
    return;
  }
  
  /* Lock for entire checkpoint sequence to ensure atomicity */
  iohdlc_mutex_lock(&p->state_mutex);
  
  /* Process N(R) to acknowledge our sent frames. */
  bool should_signal_tx = processNR(s, p, nr);
  
  /* Handle P/F bit for checkpointing (independent of frame type). */
  bool checkpoint_moved_frames = false;
  if (pf) {
    /* Both Primary and Secondary do checkpoint operations. */
    
    /* Checkpoint retransmission: retransmit unacknowledged frames
       with N(S) < vs_atlast_pf. Move them from i_retrans_q to i_trans_q.
       
       Note: The checkpoint verification is implicit in this function.
       If N(R) >= vs_atlast_pf, processNR() has already removed all frames
       from i_retrans_q and checkpointRetransmit() will find nothing to move.
       If N(R) < vs_atlast_pf, frames remain in i_retrans_q and will be
       moved to i_trans_q for retransmission (error recovery). 
       
       checkpointRetransmit() internally checks ISO 13239 5.6.2.1 case a):
       inhibits if REJ is active and would retransmit the same I-frame. */
    checkpoint_moved_frames = checkpointRetransmit(s, p);
    
    /* Role-specific P/F handling. */
    if (IOHDLC_IS_PRI(s)) {
      /* Primary received F=1: acknowledge poll and stop timer.
         Signal TX for I-frame, if any. */
      s->pf_state |= IOHDLC_F_RCVED;
      p->poll_retry_count = 0;
      ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
      if (p->i_pending_count)
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_ISNDREQ);
      
    } else {
      /* Secondary received P=1: must respond with F=1.
         Signal TX for I-frame tx and honor the P/F bit. */
      s->pf_state |= IOHDLC_P_RCVED;
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_PFHONOR);
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
  
  iohdlc_mutex_unlock(&p->state_mutex);
  
  /* Broadcast TX condition variable if space became available (outside lock).
     Wakes ALL waiting write threads so they can re-check condition.
     This is efficient with burst ACKs: 3 ACKs -> 3 writes can proceed. */
  if (should_signal_tx) {
    iohdlc_condvar_broadcast(&p->tx_cv);
  }
  
  /* Broadcast event if checkpoint moved frames (outside lock) */
  if (checkpoint_moved_frames) {
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_ISNDREQ);
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
  
  /* Validate N(S) against V(R). Need lock to read vr. */
  iohdlc_mutex_lock(&p->state_mutex);
  uint32_t expected_ns = p->vr;
  
  if (ns != expected_ns) {
    /* Out-of-sequence error detected. */
    
    /* Send REJ to request retransmission.
       ISO 13239 5.6.2.1 case a): only one REJ at a time.
       If REJ already actioned, first REJ will retransmit all needed frames.
       REJ is only sent if the option is negotiated (IOHDLC_USE_REJ).
       Without REJ, recovery relies on checkpoint timeout (slower but standard-compliant). */
    IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr, "N(S) %u, exp %u",
                  ns, expected_ns);
    bool should_broadcast = false;
    if (!IOHDLC_USE_TWA(s) && IOHDLC_USE_REJ(s) && p->rej_actioned == 0) {
      p->ss_fun = IOHDLC_S_REJ;
      p->rej_actioned = p->vr + 1;  /* Mark REJ as actioned (value = N(R) + 1) */
      should_broadcast = true;
    }
    
    iohdlc_mutex_unlock(&p->state_mutex);
    
    hdlcReleaseFrame(s->frame_pool, fp);
    
    if (should_broadcast) {
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_SSNDREQ);
    }
    
    return;
  }
  
  /* Frame is in sequence: enqueue for application. */
  p->ss_state |= IOHDLC_SS_IF_RCVD;
  ioHdlcBroadcastFlags(s, IOHDLC_EVT_PFHONOR);
  ioHdlc_frameq_insert(&p->i_recept_q, fp);
  
  /* Clear REJ exception if this is the frame that completes recovery.
     rej_actioned = x means waiting for frame with N(S) = x-1. */
  if (p->rej_actioned != 0 && ns == ((p->rej_actioned - 1) & s->modmask)) {
    p->rej_actioned = 0;
  }
  
  /* Check frame pool watermark and set local busy if LOW_WATER.
     This triggers RNR transmission to apply flow control. */
  bool should_broadcast_rnr = false;
  if (hdlcPoolGetState(s->frame_pool) == IOHDLC_POOL_LOW_WATER) {
    if (!IOHDLC_ST_BUSY(s)) {
      s->flags |= IOHDLC_FLG_BUSY;
      p->ss_fun = IOHDLC_S_RNR;
      should_broadcast_rnr = true;
    }
  }
  
  /* Increment V(R) - frame accepted (use modmask for modular increment). */
  p->vr = (p->vr + 1) & s->modmask;
  
  iohdlc_mutex_unlock(&p->state_mutex);
  
  /* Signal the counting semaphore to unblock ioHdlcReadTmo() if waiting (increment count) */
  iohdlc_sem_signal(&p->i_recept_sem);
  
  /* Broadcast RNR event if needed (outside lock) */
  if (should_broadcast_rnr) {
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_SSNDREQ);
  }
}

static void handleSFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                         iohdlc_frame_t *fp) {
  /* Handle S-frame specific logic:
     - Update peer busy state (RR/RNR)
     - Process REJ for retransmission
     - Ignore SREJ (not implemented)
  */
  
  uint8_t ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);
  uint8_t s_fun = ctrl & IOHDLC_S_FUN_MASK;
  
  /* Handle S-frame function. */
  switch (s_fun) {
    case IOHDLC_S_RR:
      /* Peer ready to receive: clear busy flag. */
      iohdlc_mutex_lock(&p->state_mutex);
      p->ss_state &= ~IOHDLC_SS_BUSY;
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
      
    case IOHDLC_S_RNR:
      /* Peer not ready: set busy flag. */
      iohdlc_mutex_lock(&p->state_mutex);
      p->ss_state |= IOHDLC_SS_BUSY;
      iohdlc_mutex_unlock(&p->state_mutex);
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
        uint32_t nr = extractNR(s, fp);
        bool should_broadcast = false;
        
        iohdlc_mutex_lock(&p->state_mutex);
        
        /* Check if checkpoint is active and starting with same particular I frame.
           "same particular I frame" = same N(S) value. */
        if (p->chkpt_actioned != 0 && (p->chkpt_actioned - 1) == nr) {
          /* Same frame: REJ inhibited per 5.6.2.2.
             Mark REJ as actioned to prevent duplicate attempts. */
          p->rej_actioned = nr + 1;
        } else {
          /* REJ acts: move ALL remaining frames from i_retrans_q to head of i_trans_q.
             processNR() has already removed frames with N(S) < N(R),
             so i_retrans_q now contains exactly the frames to retransmit (N(S) >= N(R)). */
          if (!ioHdlc_frameq_isempty(&p->i_retrans_q)) {
            iohdlc_frame_t *first = p->i_retrans_q.next;
            iohdlc_frame_t *last = p->i_retrans_q.prev;
            ioHdlc_frameq_move(&p->i_trans_q, first, last);
            should_broadcast = true;
          }
          
          /* Mark REJ as actioned (value = N(R) + 1 to distinguish from 0 = not actioned).
             Clear checkpoint state since REJ is now handling retransmission. */
          p->rej_actioned = nr + 1;
          p->chkpt_actioned = 0;
        }
        
        iohdlc_mutex_unlock(&p->state_mutex);
        
        /* Broadcast event outside lock */
        if (should_broadcast) {
          ioHdlcBroadcastFlags(s, IOHDLC_EVT_ISNDREQ);
        }
      }
      break;
      
    case IOHDLC_S_SREJ:
      /* Selective reject: not implemented. Ignore. */
      break;
  }
  
  hdlcReleaseFrame(s->frame_pool, fp);
}

void nrmRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Handle I-frames and S-frames specific to NRM mode. */
  
  iohdlc_station_peer_t *p;
  uint32_t addr;
  uint8_t ctrl;
  
  /* Decode address and control. */
  addr = IOHDLC_FRAME_ADDR(s, fp);
  ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);
  
  /* Check address. */
  p = s->c_peer;
  if (IOHDLC_IS_PRI(s)) {
    /* Primary: address must match current peer. */
    if (addr != p->addr) {
      hdlcReleaseFrame(s->frame_pool, fp);
      return;
    }
  } else {
    /* Secondary: address must match station address. */
    if (addr != s->addr) {
      hdlcReleaseFrame(s->frame_pool, fp);
      return;
    }
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

void armRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {
    (void)s;
    (void)fp;
}

void abmRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {
    (void)s;
    (void)fp;
}

/*===========================================================================*/
/* Frame Building Functions                                                  */
/*===========================================================================*/

/**
 * @brief   Build U-frame (Unnumbered frame) for transmission.
 * @details Constructs control field and sets address field according to
 *          ISO 13239 command/response semantics. Calculates elen and
 *          valorizes FFF if present.
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
  hdlcReleaseFrame(s->frame_pool, fp);
  return (err == 0);
}

/**
 * @brief   Build S-frame (Supervisory frame) for transmission.
 * @details Constructs control field with N(R) and sets address field.
 *          Calculates elen and valorizes FFF if present.
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

uint32_t nrmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags) {

  /* Check if a S is requested
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
    
    iohdlc_mutex_lock(&p->state_mutex);
    p->ss_state |= IOHDLC_SS_SENDING;

    if (nrmSendOpportunity(s)) {
      const uint32_t outstanding = (p->vs - p->nr) & s->modmask;
      const bool window_full = outstanding >= p->ks;
      const bool no_i_frame = ioHdlc_frameq_isempty(&p->i_trans_q) || window_full;
      bool set_pf = IOHDLC_IS_PRI(s) ?
        (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) && no_i_frame : IOHDLC_F_ISRCVED(s)) :
        (IOHDLC_P_ISRCVED(s) && no_i_frame);
      
      /* Read values under lock before building frame (including ss_fun snapshot) */
      const uint32_t nr = p->vr;
      const uint32_t vs_for_checkpoint = p->vs;
      const uint8_t ss_fun_snapshot = p->ss_fun;
      
      iohdlc_mutex_unlock(&p->state_mutex);
      
      iohdlc_frame_t *fp = hdlcTakeFrame(s->frame_pool);
      if (fp != NULL) {
        bool is_command = IOHDLC_IS_PRI(s);

        buildSFrame(s, p, fp, ss_fun_snapshot, nr, set_pf, is_command);
#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
        /* Log S-frame transmission (before send, frame will be released) */
        iohdlc_log_sfun_t log_fun = (ss_fun_snapshot >> 2);
        uint8_t log_flags = (ss_fun_snapshot == IOHDLC_S_RNR) ? IOHDLC_LOG_FLAG_BUSY : 0;
        uint8_t log_addr = IOHDLC_FRAME_ADDR(s, fp);
#endif
        
        /* Re-acquire lock for atomic state update + send + timer operations */
        iohdlc_mutex_lock(&p->state_mutex);
        
        /* Update checkpoint reference and ACK P/F BEFORE sending */
        if (set_pf) {
          p->vs_atlast_pf = vs_for_checkpoint;
          if (IOHDLC_IS_PRI(s)) {
            IOHDLC_ACK_F(s);
          } else {
            IOHDLC_ACK_P(s);
          }
        }
        
        /* Send frame under lock to ensure state consistency */
        (void)sendFrame(s, fp);
        
        /* Start timer if needed (still under lock) */
        if (set_pf && IOHDLC_IS_PRI(s)) {
          ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
        }
        
        IOHDLC_LOG_SFRAME(IOHDLC_LOG_TX, s->addr, log_addr,
                          log_fun, nr, set_pf, log_flags);
        
        p->ss_state &= ~IOHDLC_SS_SENDING;
        iohdlc_mutex_unlock(&p->state_mutex);
      } else {
        /* Frame allocation failed */
        iohdlc_mutex_lock(&p->state_mutex);
        p->ss_state &= ~IOHDLC_SS_SENDING;
        iohdlc_mutex_unlock(&p->state_mutex);
      }
    } else {
      iohdlc_mutex_unlock(&p->state_mutex);
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
    
    /* Retry: force P bit to be sent on next I-frame or opportunistic S-frame. */
    s->pf_state |= IOHDLC_F_RCVED;
    p->ss_state |= IOHDLC_SS_IF_RCVD;
  }

  cm_flags &= ~(IOHDLC_EVT_LINIDLE|IOHDLC_EVT_ISNDREQ|IOHDLC_EVT_PFHONOR);

  bool i_frame_sent = false;  /* Track if at least one I-frame was sent. */
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

    if (!nrmSendOpportunity(s)) {
      /* Cannot send I-frame now (TWA waiting for P/F bit). */
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }
  
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
      iohdlc_mutex_unlock(&p->state_mutex);
      return cm_flags;
    }
    
    if ((cm_flags & (IOHDLC_EVT_UMRECVD | IOHDLC_EVT_SSNDREQ)) ||
        IOHDLC_PEER_BUSY(p)) {
      /* Critical event detected: exit I-frame loop immediately. */
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }

    /* Extract frame from transmission queue with lookahead. */
    iohdlc_frame_t *next_fp = NULL;
    iohdlc_frame_t *fp = ioHdlc_frameq_remove_la(&p->i_trans_q, &next_fp);
    if (fp == NULL) {
      iohdlc_mutex_unlock(&p->state_mutex);
      break;  /* Safety check. */
    }

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

    /* Read vr for N(R) field */
    uint32_t nr_value = p->vr;
    
    /* Move frame to retransmission queue. */
    ioHdlc_frameq_insert(&p->i_retrans_q, fp);

    /* Set N(S) and then advance V(S) - use
       modmask for modular arithmetic on full numbering space. */
    IOHDLC_FRAME_SET_NS(s, fp, p->vs);
    p->vs = (p->vs + 1) & s->modmask;

    /* Update checkpoint reference and ACK P/F BEFORE sending (atomic with state) */
    if (set_pf) {
      p->vs_atlast_pf = p->vs;
      if (IOHDLC_IS_PRI(s)) {
        IOHDLC_ACK_F(s);
      } else {
        IOHDLC_ACK_P(s);
      }
    }
    
    /* Update N(R) and P/F in frame (can be done under lock, operates on frame buffer) */
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

    /* Send frame under lock to ensure state consistency */
    (void)hdlcSendFrame(s->driver, fp);
    
    /* Start timer if needed (still under lock for atomicity) */
    if (set_pf && IOHDLC_IS_PRI(s)) {
      ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
    }
    
    iohdlc_mutex_unlock(&p->state_mutex);
    
    /* Log I-frame transmission */
    IOHDLC_LOG_IFRAME(IOHDLC_LOG_TX, s->addr, log_addr,
                      log_ns, log_nr, set_pf, info_len,
                      p->i_pending_count, p->ks, fflags);

    /* Mark that we sent at least one I-frame. */
    i_frame_sent = true;

    /* In TWA, stop after sending one frame with F (secondary) or P (primary). */
    if (IOHDLC_USE_TWA(s) && set_pf)
      break;
  }

  /* If no I-frame was sent but we have the opportunity/need to respond,
     prepare to send an opportunistic S-frame (RR or RNR). */
  if (!i_frame_sent && ((p->ss_state & IOHDLC_SS_IF_RCVD) ||
                        IOHDLC_P_ISRCVED(s) || IOHDLC_F_ISRCVED(s))) {
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
  p->ss_state &= ~IOHDLC_SS_IF_RCVD;

  return cm_flags;
}

uint32_t abmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags) {
  /* S requested
     NOTE per ABM:
        Come distinguere tra command e response, in ordine di priorità:
        se devo inviare un P è command.
        se devo inviare un F è response.
        se !IOHDLC_P_ISRCVED(s) è response.
        se IOHDLC_P_ISRCVED(s) è command.*/    
  (void)s;
  (void)p;
  (void)cm_flags;
  return 0;
}

uint32_t armTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags) {
  /* S requested
     NOTE per ARM:
        Come distinguere tra command e response, in ordine di priorità:
        se devo inviare un P è command.
        se devo inviare un F è response.
        se !IOHDLC_P_ISRCVED(s) è response.
        se IOHDLC_P_ISRCVED(s) è command.*/    
  (void)s;
  (void)p;
  (void)cm_flags;
  return 0;
}

void ioHdlcTxEntry(void *stationp) {
  iohdlc_station_t *s = (iohdlc_station_t *)stationp;
  iohdlc_station_peer_t *p;
  const uint32_t mask = 0;
  uint32_t cm_flags = 0;
  bool r;

  if (!s) return;

  /* Register event listener */
  iohdlc_evt_register(&s->cm_es, &s->cm_listener,
                    EVENT_MASK(0),
                    IOHDLC_EVT_C_RPLYTMO|IOHDLC_EVT_UMRECVD|IOHDLC_EVT_PFHONOR|
                    IOHDLC_EVT_CONNSTR|IOHDLC_EVT_LINIDLE|IOHDLC_EVT_ISNDREQ);
  for (;;) {
    if (!cm_flags) {
      if (s_runner_ops && s_runner_ops->wait_events)
        cm_flags = s_runner_ops->wait_events(s, mask);
      else
        break; /* cannot wait, exit */
    }

    /* Check if stop requested */
    if (s->stop_requested) {
      break;
    }
    
    /* Proceed by priority. U -> S -> I -> OS.*/
    p = s->c_peer;
    if (NULL == p) { cm_flags = 0; continue; }

    iohdlc_mutex_lock(&p->state_mutex);

    /* U response */
    if (p->um_state & IOHDLC_UM_RCVED) {
      /* If an unnumbered command has been received, the um_rsp field contains
         the response to send, valued by the receiver on the received
         UM command basis.*/
      cm_flags &= ~IOHDLC_EVT_UMRECVD;        /* serve the event, if any.*/
      if (sendOpportunity(s, &cm_flags)) {
        p->um_state &= ~IOHDLC_UM_RCVED;  /* ack UM */
        IOHDLC_ACK_P(s);                  /* ack P  */
        
        /* Build and send UM response. */
        iohdlc_frame_t *fp = hdlcTakeFrame(s->frame_pool);
        if (fp != NULL) {
          buildUFrame(s, p, fp, p->um_rsp, true, false);  /* F=1, response */

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
            s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
            setModeFunctions(s, s->mode);  /* Reset to NULL */
            resetPeerVars(p);
          }
        }
      } else {
        iohdlc_mutex_unlock(&p->state_mutex);
        continue;
      }
    }

    /* U command */
    if ((cm_flags & IOHDLC_EVT_CONNSTR) || (p->um_state & IOHDLC_UM_SENDING) || 
        ((p->um_state & IOHDLC_UM_SENT) &&
            (r = ioHdlcIsReplyTimerExpired(p, IOHDLC_TIMER_REPLY)))) {

      if ((cm_flags & IOHDLC_EVT_CONNSTR) && IOHDLC_IS_PRI(s)) {
        /* Set F received on primary station. */
        s->pf_state |= IOHDLC_F_RCVED;
      }              
      cm_flags &= ~(IOHDLC_EVT_CONNSTR |
                    IOHDLC_EVT_C_RPLYTMO);  /* serve all the possible events.*/
      
      /* Evaluate timer expiry and manage retry counter. */
      if (r) {
        if (!handleTimeoutRetry(s, p)) {
          /* Link down: max retries exceeded, switch to next peer. */
          resetPeerUm(p);
          resetPeerVars(p);
          ioHdlcNextPeer(s);
          iohdlc_mutex_unlock(&p->state_mutex);
          continue;
        }
        /* Retry: will retransmit below. */
      }

      /* A link management has been requested.*/
      p->um_state |= IOHDLC_UM_SENDING;
      if (sendOpportunity(s, &cm_flags)) {
        /* Build and send UM command. */
        iohdlc_frame_t *fp = hdlcTakeFrame(s->frame_pool);
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
        
        ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
        p->um_state &= ~IOHDLC_UM_SENDING;
        p->um_state |= IOHDLC_UM_SENT;
        IOHDLC_ACK_F(s);                  /* ack F  */
      }
    }
    iohdlc_mutex_unlock(&p->state_mutex);
    if (IOHDLC_UM_INPROG(p) || IOHDLC_PEER_DISC(p)) {
      cm_flags &= ~(IOHDLC_EVT_LINIDLE|IOHDLC_EVT_ISNDREQ|IOHDLC_EVT_PFHONOR);
      continue;
    }

    if (s->tx_fn)
      cm_flags = s->tx_fn(s, p, cm_flags);
  }

  iohdlc_evt_unregister(&s->cm_es, &s->cm_listener);
}

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
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINIDLE);
      }
      continue;
    }
    s->flags &= ~IOHDLC_FLG_IDL;
    
    /* Decode control octet to determine frame type. */
    const uint8_t ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);

#if defined(IOHDLC_LOG_R)
#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
    const uint8_t addr = IOHDLC_FRAME_ADDR(s, fp);
    const bool pf = IOHDLC_FRAME_GET_PF(s, fp);
#endif
    /* Log received frame based on type */
    if (IOHDLC_IS_I_FRM(ctrl)) {
      size_t info_len = fp->elen - (s->frame_offset + 1 + s->ctrl_size);
      IOHDLC_LOG_IFRAME(IOHDLC_LOG_RX, s->addr, addr,
                        extractNS(s, fp), extractNR(s, fp), pf, info_len, 0, 0, 0);
    } else if (IOHDLC_IS_S_FRM(ctrl)) {
      iohdlc_log_sfun_t log_fun = (ctrl & IOHDLC_S_FUN_MASK) >> 2;
      uint8_t log_flags = ((ctrl & IOHDLC_S_FUN_MASK) == IOHDLC_S_RNR) ? IOHDLC_LOG_FLAG_BUSY : 0;
      IOHDLC_LOG_SFRAME(IOHDLC_LOG_RX, s->addr, addr, log_fun, extractNR(s, fp), pf, log_flags);
    } else if (IOHDLC_IS_U_FRM(ctrl)) {
      uint8_t u_cmd = ctrl & IOHDLC_U_FUN_MASK;
      iohdlc_log_ufun_t log_fun = (u_cmd == IOHDLC_U_SNRM) ? IOHDLC_LOG_SNRM :
                                   (u_cmd == IOHDLC_U_SARM) ? IOHDLC_LOG_SARM :
                                   (u_cmd == IOHDLC_U_SABM) ? IOHDLC_LOG_SABM :
                                   (u_cmd == IOHDLC_U_DISC) ? IOHDLC_LOG_DISC :
                                   (u_cmd == IOHDLC_U_UA) ? IOHDLC_LOG_UA :
                                   (u_cmd == IOHDLC_U_DM) ? IOHDLC_LOG_DM :
                                   (u_cmd == IOHDLC_U_FRMR) ? IOHDLC_LOG_FRMR : 0;
      IOHDLC_LOG_UFRAME(IOHDLC_LOG_RX, s->addr, addr, log_fun, pf);
    }
#endif

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
