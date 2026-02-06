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

volatile int __t = 0;
volatile int __q = 0;
volatile int __p = 0;

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

static void ioHdlcSetDisconnected(iohdlc_station_peer_t *p) {
  p->ss_state &= ~IOHDLC_SS_ST_CONN;
  iohdlc_condvar_broadcast(&p->tx_cv);  
}

static void ioHdlcSetConnected(iohdlc_station_peer_t *p) {
  p->ss_state |= IOHDLC_SS_ST_CONN;
  iohdlc_condvar_broadcast(&p->tx_cv);  
}


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
  
  if (p->poll_retry_count >= p->poll_retry_max) {
    /* Max retries exceeded: declare link down. */
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINKDOWN);
    
    /* Cleanup: stop timers, reset counters, clear U-frame state. */
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
    ioHdlcStopReplyTimer(p, IOHDLC_TIMER_I_REPLY);
    p->poll_retry_count = 0;
    p->um_state &= ~(IOHDLC_UM_SENDING | IOHDLC_UM_SENT);
    
    /* Mark peer disconnected (blocks further transmissions). */
    ioHdlcSetDisconnected(p);
    
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
  p->nr = p->vr = p->vs = p->vs_highest = 0;
  p->ss_state = 0;
  p->poll_retry_count = 0;
  p->i_pending_count = 0;
  clearFrameQ(p, &p->i_retrans_q);
  clearFrameQ(p, &p->i_recept_q);
  clearFrameQ(p, &p->i_trans_q);
  iohdlc_sem_init(&p->i_recept_sem, 0);
}

/* Check if u_cmd is a connection U command */
static bool isConnectionUCommand(uint8_t u_cmd) {
  return (u_cmd == IOHDLC_U_SNRM) || (u_cmd == IOHDLC_U_SARM) ||
         (u_cmd == IOHDLC_U_SABM);
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
      hdlcReleaseFrame(&s->frame_pool, fp);
      return;
    }
  } else {
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
       Per 6.11.4.1.3: if already UM_RCVED in progress, ignore additional
       commands until response sent. */
    if (p->um_state & IOHDLC_UM_RCVED) {
      hdlcReleaseFrame(&s->frame_pool, fp);
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
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_UMRECVD);
    
  } else {
    /* U-frame response received (we are Primary).
       Per 6.11.4.1.1/1.2: responses must have F bit set. */
    
    /* Verify we have an outstanding UM command (UM_SENT). */
    if (!(p->um_state & IOHDLC_UM_SENT)) {
      /* Unsolicited response → discard. */
      hdlcReleaseFrame(&s->frame_pool, fp);
      iohdlc_mutex_unlock(&p->state_mutex);
      return;
    }
    
    /* Verify F bit is set (mandatory for responses). */
    if (!has_pf) {
      /* Missing F bit → protocol error, discard. */
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
        /* DISC accepted: enter disconnected mode. */
        s->mode = IOHDLC_IS_NRM(s) ? IOHDLC_OM_NDM : IOHDLC_OM_ADM;
        setModeFunctions(s, s->mode);  /* Reset to NULL */
        ioHdlcSetDisconnected(p);
      } else {
        /* Connection command accepted (SNRM/SARM/SABM).
           Mode was already set by linkup() before sending command. */
        setModeFunctions(s, s->mode);
        ioHdlcSetConnected(p);
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
      ioHdlcSetDisconnected(p);
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
  hdlcReleaseFrame(&s->frame_pool, fp);
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
  uint32_t vs_offset = (p->vs_highest - p->nr) & s->modmask;  /* Distance from last ACK to next to send */
  
  return nr_offset <= vs_offset;
}

static bool processNR(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                      uint32_t nr) {
  /* Remove acknowledged frames from retransmission queue.
     Frames with N(S) < nr have been received by peer.
     
     Also clear checkpoint/REJ state if their tracked frames are ACKed.
     
     Returns true if semaphore should be signaled (space became available).
  */
  
  bool released_frames = false;
  
  while (p->nr != nr && !ioHdlc_frameq_isempty(&p->i_retrans_q)) {
    iohdlc_frame_t *acked_fp = ioHdlc_frameq_remove(&p->i_retrans_q);
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
  iohdlc_frame_t *fp = p->i_retrans_q.next;
  uint32_t first_ns = 0;  /* N(S) + 1 of first frame to retransmit, 0 if none */
  
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

#if 0
    /* ISO 13239 5.6.2.1 case a): Check if this frame matches REJ N(R). */
    if (p->rej_actioned != 0 && frame_ns == (p->rej_actioned - 1)) {
      /* This frame would be retransmitted by both checkpoint AND REJ.
         Inhibit entire checkpoint retransmission. */
      return false;
    }
#endif
    /* This frame was sent before checkpoint: mark for retransmission. */
    if (first_ns == 0) {
      first_ns = frame_ns + 1;  /* Save its N(S) + 1 */
    }
    fp = fp->next;    /* Continue scanning */
  }
  
  /* If we found frames to retransmit, move them to i_trans_q. */
  if (first_ns != 0) {
    ioHdlc_frameq_move(&p->i_trans_q, p->i_retrans_q.next,
      p->i_retrans_q.prev);
    
    /* Mark checkpoint as actioned with first frame N(S).
       Used to detect overlap with subsequent REJ (5.6.2.2). */
    p->chkpt_actioned = first_ns;
    p->vs = first_ns - 1;  /* Reset V(S) to first retransmit frame N(S) */
    return true;
  }
  
  return false;
}

static bool handleCheckpointAndAck(iohdlc_station_t *s, iohdlc_station_peer_t *p,
                                   uint32_t nr,
                                   bool pf,
                                   bool *should_signal_tx_out,
                                   bool *checkpoint_moved_out,
                                   uint32_t *broadcast_flags_out) {
  /* Common processing for both I-frames and S-frames:
     1. Process N(R) to acknowledge our sent frames
     2. Handle P/F bit for checkpointing
     3. Manage reply timer based on role and P/F bit
     
     Returns flags via output parameters for deferred signaling.
  */
  
  /* Validate N(R) before processing.*/
  if (!isNRValid(s, p, nr)) {
    /* Protocol error: invalid N(R) received.
       TODO: Send FRMR with Y bit set (invalid N(R)).
       For now: discard frame. */
    IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr, "Invalid N(R) %u, V(S)=%u, N(R)=%u",
                  nr, p->vs, p->nr);
    return false;
  }
  
  /* Process N(R) to acknowledge our sent frames. */
  *should_signal_tx_out = processNR(s, p, nr);
  
  /* Handle P/F bit for checkpointing (independent of frame type). */
  *checkpoint_moved_out = false;
  if (pf) {
    /* Both Primary and Secondary do checkpoint operations. */
    
    /* Checkpoint retransmission: retransmit unacknowledged frames
       with N(S) < vs_atlast_pf. Move them from i_retrans_q to i_trans_q.
       
       Note: The checkpoint verification is implicit in this function.
       If N(R) >= vs_atlast_pf, processNR() has already removed all frames
       from i_retrans_q and checkpointRetransmit() will find nothing to move.
       If N(R) < vs_atlast_pf, frames remain in i_retrans_q and will be
       moved to i_trans_q for retransmission (error recovery). */
       
    *checkpoint_moved_out = checkpointRetransmit(s, p);
    
    /* Role-specific P/F handling. */
    if (IOHDLC_IS_PRI(s)) {
      /* Primary received F=1: acknowledge poll and stop timer. */
      s->pf_state |= IOHDLC_F_RCVED;
      p->poll_retry_count = 0;
      ioHdlcStopReplyTimer(p, IOHDLC_TIMER_REPLY);
      
      /* If there are pending I-frames, signal TX to send them if
         peer is ready to receive. 
         Also, if the station is in the process of reading I-frames
         from the peer, signal TX to honor the received F.*/
      if (p->i_pending_count)
        *broadcast_flags_out |= IOHDLC_EVT_ISNDREQ;
      else if (p->ss_state & IOHDLC_SS_RECVING)
        *broadcast_flags_out |= IOHDLC_EVT_PFHONOR;
    } else {
      /* Secondary received P=1: it shall respond with F=1.
         Signal TX for I-frame tx and honor the P/F bit. */
      s->pf_state |= IOHDLC_P_RCVED;
      *broadcast_flags_out |= IOHDLC_EVT_PFHONOR;
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
  
  return true;
}

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
       ISO 13239 5.6.2.1 case a): only one REJ at a time.
       If REJ already actioned, first REJ will retransmit all needed frames.
       REJ is only sent if the option is negotiated (IOHDLC_USE_REJ).
       Without REJ, recovery relies on checkpoint timeout (slower but standard-compliant). */
#if defined(IOHDLC_LOG_R)
    IOHDLC_LOG_WARN(IOHDLC_LOG_RX, s->addr, "N(S) %u, exp %u",
                  ns, expected_ns);
#endif
    if (!IOHDLC_USE_TWA(s) && IOHDLC_USE_REJ(s) && p->rej_actioned == 0) {
      p->rej_actioned = expected_ns + 1;
      p->ss_fun = IOHDLC_S_REJ;
      *broadcast_flags_out |= IOHDLC_EVT_SSNDREQ;  /* REJ needs S-frame transmission */
    } else if (p->rej_actioned != 0) {
#if defined(IOHDLC_LOG_R)
      IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "REJ already actioned");
#endif
    }
    return false;  /* Discard frame */
  }
  
  /* Frame is in sequence: enqueue for application. */
  p->ss_state |= IOHDLC_SS_IF_RCVD;
  ioHdlc_frameq_insert(&p->i_recept_q, fp);
  
  /* Always signal PFHONOR for valid I-frames */
  *broadcast_flags_out |= IOHDLC_EVT_PFHONOR;
  
  /* Clear REJ exception if this is the frame that completes recovery.
     rej_actioned = x means waiting for frame with N(S) = x-1. */
  if (p->rej_actioned != 0 && ns == (p->rej_actioned - 1))
    p->rej_actioned = 0;
  
  if (IOHDLC_PEER_BUSY(p) && pf)
    p->ss_state &= ~IOHDLC_SS_BUSY;
   
  /* Check frame pool watermark and set local busy if LOW_WATER.
     This triggers RNR transmission to apply flow control. */
  if (!IOHDLC_IS_BUSY(s) && hdlcPoolGetState(&s->frame_pool) == IOHDLC_POOL_LOW_WATER) {
    p->ss_fun = IOHDLC_S_RNR;
    s->flags |= IOHDLC_FLG_BUSY;  /* Mark that we are busy */
    *broadcast_flags_out |= IOHDLC_EVT_SSNDREQ;  /* RNR needs S-frame transmission */
  }
  
  /* Increment V(R) - frame accepted. */
  p->vr = (p->vr + 1) & s->modmask;
  
  return true;  /* Frame accepted */
}

static void handleSFrame(iohdlc_station_t *s, iohdlc_station_peer_t *p, 
                         iohdlc_frame_t *fp,
                         bool pf,
                         uint32_t *broadcast_flags_out) {
  /* Handle S-frame specific logic:
     - Update peer busy state (RR/RNR)
     - Process REJ for retransmission
     - Ignore SREJ (not implemented)
     
     Caller must hold state_mutex.
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
      break;
      
    case IOHDLC_S_RNR:
      /* Peer not ready: set busy flag. */
      p->ss_state |= IOHDLC_SS_BUSY;
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
        if (p->chkpt_actioned == 0 /*|| nr != p->chkpt_actioned - 1*/) {
          /* REJ acts: move all remaining frames from i_retrans_q to head of i_trans_q.
             processNR() has already removed frames with N(S) < N(R),
             so i_retrans_q now contains exactly the frames to retransmit (N(S) >= N(R)). */
#if defined(IOHDLC_LOG_R)
          IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "REJ done on N(R)=%u", nr);
#endif
          if (!ioHdlc_frameq_isempty(&p->i_retrans_q)) {
            iohdlc_frame_t *first = p->i_retrans_q.next;
            iohdlc_frame_t *last = p->i_retrans_q.prev;
            ioHdlc_frameq_move(&p->i_trans_q, first, last);
            p->vs = p->vs_atlast_pf = extractNS(s, first); 
            *broadcast_flags_out |= IOHDLC_EVT_ISNDREQ;  /* REJ moved frames to transmission queue */
          }
        } else {
#if defined(IOHDLC_LOG_R)
          IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr,
                           "Inhibit REJ N(R)=%u, chkpt=%u", nr, p->chkpt_actioned);
#endif
          p->chkpt_actioned = 0;
        }
      }
      break;
      
    case IOHDLC_S_SREJ:
      /* Selective reject: not implemented. Ignore. */
      break;
  }
}

void nrmRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {
  /* Handle I-frames and S-frames specific to NRM mode.*/
  
  iohdlc_station_peer_t *p;
  uint32_t addr, nr;
  uint8_t ctrl;
  bool pf;
  
  /* Decode address and control. */
  addr = IOHDLC_FRAME_ADDR(s, fp);
  ctrl = IOHDLC_FRAME_CTRL(s, fp, 0);
  pf = IOHDLC_FRAME_GET_PF(s, fp);
  nr = extractNR(s, fp);
  
  /* Check address. */
  p = s->c_peer;
  if (IOHDLC_IS_PRI(s)) {
    /* Primary: address must match current peer. */
    if (addr != p->addr) {
      hdlcReleaseFrame(&s->frame_pool, fp);
      return;
    }
  } else {
    /* Secondary: address must match station address. */
    if (addr != s->addr) {
      hdlcReleaseFrame(&s->frame_pool, fp);
      return;
    }
  }
  
  /* Flags for deferred signaling (after lock release). */
  bool should_signal_tx = false;
  bool checkpoint_moved = false;
  uint32_t broadcast_flags = 0;
  bool frame_accepted = false;
  
  /* Single lock for entire processing sequence.
     This prevents TX from observing intermediate state where checkpoint/ACK
     is complete but I-frame validation/state update is not yet done. */
  iohdlc_mutex_lock(&p->state_mutex);
  
#if defined(IOHDLC_LOG_R) && IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
    const uint8_t addr2 = IOHDLC_FRAME_ADDR(s, fp);
    bool is_final = s->addr != addr2;
    uint32_t nns, nnr, qns;

    nns = extractNS(s, fp);
    nnr = nr;
    qns = ioHdlc_frameq_isempty(&p->i_retrans_q) ? s->modmask+1 :
      extractNS(s, p->i_retrans_q.next);

    /* Log received frame based on type */
    if (IOHDLC_IS_I_FRM(ctrl)) {
      /* size_t info_len = fp->elen - (s->frame_offset + 1 + s->ctrl_size);*/
      IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "A%u I%u,%u %c pnr=%u vfp=%u fr=%u",
		   addr2, nns, nnr,
		   pf ? (is_final ? 'F' : 'P') : '-',
		   p->nr, p->vs_atlast_pf, qns);
    } else if (IOHDLC_IS_S_FRM(ctrl)) {
      iohdlc_log_sfun_t log_fun = (ctrl & IOHDLC_S_FUN_MASK) >> 2;
      IOHDLC_LOG_MSG(IOHDLC_LOG_RX, s->addr, "A%u %s%u %c pnr=%u vfp=%u fr=%u",
		     addr2, sfun_to_str(log_fun), nnr,
		   pf ? (is_final ? 'F' : 'P') : '-',
		   p->nr, p->vs_atlast_pf, qns);
    }
#endif

  /* Common checkpoint and acknowledgment processing for all I/S frames. */
  if (!handleCheckpointAndAck(s, p, nr, pf, &should_signal_tx, &checkpoint_moved, &broadcast_flags)) {
    /* Protocol error occurred during checkpoint/ack processing. */
    iohdlc_mutex_unlock(&p->state_mutex);
    hdlcReleaseFrame(&s->frame_pool, fp);
    return;
  }
  
  /* Branch by frame type for specific handling. */
  if (IOHDLC_IS_I_FRM(ctrl)) {
    frame_accepted = handleIFrame(s, p, fp, pf, &broadcast_flags);
  } else {
    handleSFrame(s, p, fp, pf, &broadcast_flags);
  }
  
  /* Add checkpoint/ACK related events to broadcast flags */
  if (checkpoint_moved) {
    broadcast_flags |= IOHDLC_EVT_ISNDREQ;
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

void armRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {
    (void)s;
    (void)fp;
}

void abmRx(iohdlc_station_t *s, iohdlc_frame_t *fp) {
    (void)s;
    (void)fp;
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

    /* Call mode-specific RX handler for I and S frames. */
    s->rx_fn(s, fp);
  }
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
  hdlcReleaseFrame(&s->frame_pool, fp);
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
  if (cm_flags & (IOHDLC_EVT_SSNDREQ/*|IOHDLC_EVT_POOLNORM*/)) {
    iohdlc_mutex_lock(&p->state_mutex);

    if (__t && s->addr == 2) ++__q;    
    if (nrmSendOpportunity(s)) {
      const uint32_t outstanding = (p->vs - p->nr) & s->modmask;
      const bool window_full = outstanding >= p->ks;
      const bool no_i_frame = ioHdlc_frameq_isempty(&p->i_trans_q) || window_full;
      bool set_pf = IOHDLC_IS_PRI(s) ?
        (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) && no_i_frame : IOHDLC_F_ISRCVED(s)) :
        (IOHDLC_P_ISRCVED(s) && (no_i_frame || IOHDLC_PEER_BUSY(p) /*|| (cm_flags & IOHDLC_EVT_POOLNORM)*/));

      iohdlc_frame_t *fp = hdlcTakeFrame(&s->frame_pool);
      if (fp != NULL) {
        bool is_command = IOHDLC_IS_PRI(s);

#if 0
        if (cm_flags & IOHDLC_EVT_POOLNORM)
          p->ss_fun = IOHDLC_S_RR;
        cm_flags &= ~(IOHDLC_EVT_SSNDREQ|IOHDLC_EVT_POOLNORM);
#else
        cm_flags &= ~(IOHDLC_EVT_SSNDREQ);
#endif
        buildSFrame(s, p, fp, p->ss_fun, p->vr, set_pf, is_command);
#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF
        /* Log S-frame transmission (before send, frame will be released) */
        iohdlc_log_sfun_t log_fun = (p->ss_fun >> 2);
        uint8_t log_flags = (p->ss_fun == IOHDLC_S_RNR) ? IOHDLC_LOG_FLAG_BUSY : 0;
        uint8_t log_addr = IOHDLC_FRAME_ADDR(s, fp);
#endif
        
        /* Update checkpoint reference and ACK P/F before sending */
        if (set_pf) {
          p->vs_atlast_pf = p->vs;
          IOHDLC_IS_PRI(s) ? IOHDLC_ACK_F(s) : IOHDLC_ACK_P(s);
        }
        
        IOHDLC_LOG_SFRAME(IOHDLC_LOG_TX, s->addr, log_addr, log_fun,
                          p->vr, set_pf, p->i_pending_count, log_flags);
        
        if ((__t && !is_command)) {
                fprintf(stderr, "Built frame: fun=0x%02X, cm=0x%02X, set_pf=%d, is_command=%d, q=%d, p=%d\n",
            p->ss_fun, cm_flags, set_pf, is_command, __q, __p);
          __t = __q = __p = 0;
        }

        /* Send frame under lock to ensure state consistency */
        (void)sendFrame(s, fp);
        
        /* Start timer if needed */
        if (set_pf && IOHDLC_IS_PRI(s)) {
          ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
        }
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

    if (ioHdlcIsReplyTimerExpired(p, IOHDLC_TIMER_REPLY)) {
      IOHDLC_LOG_WARN(IOHDLC_LOG_TX, s->addr, "TE");
      if (!handleTimeoutRetry(s, p)) {
        /* Link down: max retries exceeded, cannot send I-frames. */
        iohdlc_mutex_unlock(&p->state_mutex);
        return cm_flags;
      }
      
      /* Retry: force P bit to be sent on next I-frame or opportunistic S-frame. */
      s->pf_state |= IOHDLC_F_RCVED;
      p->ss_state |= IOHDLC_SS_IF_RCVD;
      p->rej_actioned = 0;  /* Clear REJ exception on timeout retry */
    }
  }
  iohdlc_mutex_unlock(&p->state_mutex);

  cm_flags &= ~(IOHDLC_EVT_LINIDLE|IOHDLC_EVT_PFHONOR);

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
  
    if (IOHDLC_PEER_BUSY(p)) {
      iohdlc_mutex_unlock(&p->state_mutex);
      break;
    }

    /* Poll for new events before sending each I-frame.
       This allows interrupting the burst if urgent events occur. */
    cm_flags |= s_runner_ops->get_events_flags(s);
    
    /* Check for urgent events requiring immediate attention:
       - IOHDLC_EVT_UMRECVD: U-frame received (disconnect/mode change)
       - IOHDLC_EVT_LINKDOWN: Link failure detected
       - IOHDLC_EVT_SSNDREQ: Urgent S-frame requested (local busy condition)
       - IOHDLC_PEER_BUSY: Peer went into RNR state */
    if (cm_flags & IOHDLC_EVT_LINKDOWN) {
      /* Link is down: abort transmission immediately. */
      iohdlc_mutex_unlock(&p->state_mutex);
      return cm_flags;
    }
    
    if ((cm_flags & (IOHDLC_EVT_UMRECVD | IOHDLC_EVT_SSNDREQ))) {
      /* Urgent event detected: exit I-frame loop immediately. */
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

    bool set_pf = false;
    /* Determine if P/F bit should be set in this I-frame. 
       If local busy, never set P/F on I-frames. */
    if (!IOHDLC_IS_BUSY(s)) {
      /* Determine whether to set P/F bit.
         Primary TWA: Set P on last I-frame (always, we have the link).
         Primary TWS: Set P as soon as possible (if no P in flight).
         Secondary (TWA & TWS): Set F on last I-frame (when window will be full
         or no more frames). */
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
    }
    /* Read vr for N(R) field */
    uint32_t nr_value = p->vr;
    
    /* Move frame to retransmission queue. */
    ioHdlc_frameq_insert(&p->i_retrans_q, fp);

    /* Set N(S) and then advance V(S) - use
       modmask for modular arithmetic on full numbering space. */
    IOHDLC_FRAME_SET_NS(s, fp, p->vs);
    if (p->vs == p->vs_highest)
      p->vs_highest = (p->vs + 1) & s->modmask;
    p->vs = (p->vs + 1) & s->modmask;

    /* Update checkpoint reference and ACK P/F before sending */
    if (set_pf) {
      p->vs_atlast_pf = p->vs;
      IOHDLC_IS_PRI(s) ? IOHDLC_ACK_F(s) : IOHDLC_ACK_P(s);
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

    /* Log I-frame transmission */
    IOHDLC_LOG_IFRAME(IOHDLC_LOG_TX, s->addr, log_addr,
                      log_ns, log_nr, set_pf, info_len,
                      p->i_pending_count, p->ks, fflags);
    
    /* Send frame under lock to ensure state consistency */
    (void)hdlcSendFrame(s->driver, fp);
    
    /* Start timer if needed */
    if (set_pf && IOHDLC_IS_PRI(s)) {
      ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
    }
    
    iohdlc_mutex_unlock(&p->state_mutex);

    /* Mark that we sent at least one I-frame. */
    i_frame_sent = true;

    /* In TWA, stop after sending one frame with F (secondary) or P (primary). */
    if (IOHDLC_USE_TWA(s) && set_pf)
      break;
  }

  cm_flags &= ~IOHDLC_EVT_ISNDREQ;

  iohdlc_mutex_lock(&p->state_mutex);
  /* If no I-frame was sent but we have the opportunity/need to respond,
     prepare to send an opportunistic S-frame (RR or RNR). */
  if (!i_frame_sent && ((p->ss_state & IOHDLC_SS_IF_RCVD)
			|| IOHDLC_P_ISRCVED(s) || IOHDLC_F_ISRCVED(s))) {
    /* In TWA, if we still have permission on the link but didn't send I-frames,
       we should send an S-frame to acknowledge and cede the link.
       In TWS, we may also want to send periodic acknowledgments. */
    if (nrmSendOpportunity(s)) {
      /* Determine S-frame function: RR or RNR based on local busy state. */
      p->ss_fun = IOHDLC_IS_BUSY(s) ? IOHDLC_S_RNR : IOHDLC_S_RR;

      /* Raise event to trigger S-frame transmission. */
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_SSNDREQ);
    }
  }
  
  /* Check if we need to clear local busy: pool returned NORMAL after RNR. */
  if (IOHDLC_IS_BUSY(s) && 
      hdlcPoolGetState(&s->frame_pool) == IOHDLC_POOL_NORMAL) {

    s->flags &= ~IOHDLC_FLG_BUSY;

    if (s->addr == 2)
      __t = 1, __q = 0;

    /* Raise event for RR transmission. */
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_POOLNORM);
    
    /* Wake up writers blocked on pool availability. */
    iohdlc_condvar_broadcast(&p->tx_cv);
  }
  
  cm_flags &= ~IOHDLC_EVT_POOLNORM;
  p->ss_state &= ~IOHDLC_SS_IF_RCVD;
  iohdlc_mutex_unlock(&p->state_mutex);

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
                    IOHDLC_EVT_CONNSTR|IOHDLC_EVT_LINIDLE|IOHDLC_EVT_ISNDREQ|
                    IOHDLC_EVT_SSNDREQ|IOHDLC_EVT_POOLNORM);
  for (;;) {
    p = s->c_peer;
    p->pend_flags = cm_flags & (IOHDLC_EVT_SSNDREQ/*|IOHDLC_EVT_POOLNORM*/);
    cm_flags &= ~(IOHDLC_EVT_SSNDREQ/*|IOHDLC_EVT_POOLNORM*/);
    if (!cm_flags) {
      /* if (s_runner_ops && s_runner_ops->wait_events) TODO: change to asserts */
      ++__p;
      cm_flags = s_runner_ops->wait_events(s, mask);
    }
    cm_flags |= p->pend_flags;

    /* Check if stop requested */
    if (s->stop_requested) {
      break;
    }
    
    /* Proceed by priority. U -> S -> I -> OS.*/
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
        iohdlc_frame_t *fp = hdlcTakeFrame(&s->frame_pool);
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
            ioHdlcSetDisconnected(p);           /* Do not reset the queues to allow */
            iohdlc_sem_signal(&p->i_recept_sem);/* the reading of remaining frames*/
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

      cm_flags &= ~(IOHDLC_EVT_CONNSTR |
                    IOHDLC_EVT_C_RPLYTMO);  /* serve all the possible events.*/
      
      if (!IOHDLC_IS_PRI(s)) {
        /* A U command must originate from primary station */
        iohdlc_mutex_unlock(&p->state_mutex);
        continue;
      }

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
      if (IOHDLC_PEER_DISC(p) &&
            IOHDLC_USE_TWA(s) && isConnectionUCommand(p->um_cmd)) {
        /* In TWA, preset as F received on primary station. */
        s->pf_state |= IOHDLC_F_RCVED;
      }

      if (sendOpportunity(s, &cm_flags)) {
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
        
        ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, s->reply_timeout_ms);
        p->um_state &= ~IOHDLC_UM_SENDING;
        p->um_state |= IOHDLC_UM_SENT;
        IOHDLC_ACK_F(s);                  /* ack F  */
      }
    }

    iohdlc_mutex_unlock(&p->state_mutex);
    if ((IOHDLC_UM_INPROG(p) && IOHDLC_PEER_DISC(p)) || IOHDLC_UM_ISSENT(p)) {
      cm_flags &= ~(IOHDLC_EVT_LINIDLE|IOHDLC_EVT_ISNDREQ|IOHDLC_EVT_PFHONOR);
      continue;
    }

    if (s->tx_fn)
      cm_flags = s->tx_fn(s, p, cm_flags);
  }

  iohdlc_evt_unregister(&s->cm_es, &s->cm_listener);
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
