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

/* Map UM command to supported connection mode. */
static uint8_t um2supportedConnMode(uint8_t um_cmd) {
  if (um_cmd == IOHDLC_U_SABM)
    return IOHDLC_OM_ABM;
  if (um_cmd == IOHDLC_U_SNRM)
    return IOHDLC_OM_NRM;
  return 0;
}

/* Map supported connection mode to UM command. */
static uint8_t supportedConnMode2um(uint8_t mode) {
  if (mode == IOHDLC_OM_ABM)
    return IOHDLC_U_SABM;
  if (mode == IOHDLC_OM_NRM)
    return IOHDLC_U_SNRM;
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
 * @brief Check if there is a send opportunity
 * @note  In the case of normal modes, a send opportunity exists
 *        - when a frame with P bit set to 1 is received if @p s is secondary or
 *        - when a frame with F bit set to 1 is received if @p s is primary
 *        - when @p s is primary and not in TWA.
 *        In the case of asynchronous modes, two-way alternate,
 *        a send opportunity exists when a idle state is detected
 *        on the link (IOHDLC_FLG_IDL).
 *        In the case of asynchronous modes, two-way simultaneous,
 *        a send opportunity ever exists.
 */
static bool thereIsSendOpportunity(iohdlc_station_t *s, uint8_t pf_bit) {
  if ((!IOHDLC_IS_NRM(s) && !IOHDLC_USE_TWA(s)) ||
      (!IOHDLC_IS_NRM(s) && IOHDLC_ST_IDLE(s))  ||
      ( IOHDLC_IS_NRM(s) && pf_bit))
    return true;
  return false;
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

void ioHdlcOnRxFrame(iohdlc_station_t *station, iohdlc_frame_t *fp) {
  (void)station;
  /* TODO: hook into RX state machine; currently handled by original code. */
  /* Placeholder: avoid leaks until full RX logic is ported. */
  if (fp) hdlcReleaseFrame(station->frame_pool, fp);
}

uint32_t ioHdlcOnLineIdle(iohdlc_station_t *station) {
  (void)station;
  return (uint32_t)IOHDLC_EVT_LINIDLE;
}

static uint32_t nrmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p,
  uint32_t cm_flags) {

  /* Check if we have send opportunity.
     Primary TWA: need F received (no P in flight).
     Primary TWS: always permitted.
     Secondary TWA/TWS: need P received. */
  bool pf_received = IOHDLC_IS_PRI(s) ?
      (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) : true) :
      IOHDLC_P_ISRCVED(s);

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

    if (thereIsSendOpportunity(s, pf_received)) {
      const outstanding = (p->vs >= p->nr) ?
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
          ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, 100);
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
    /* Poll/reply timeout occurred: need to re-send frame with P set. */
    cm_flags &= ~IOHDLC_EVT_C_RPLYTMO;
    s->pf_state |= IOHDLC_F_RCVED; /* Force P not sent. */
  }

  /* Recalculate pf_received since S-frame transmission or timeout may have cleared flags. */
  pf_received = IOHDLC_IS_PRI(s) ?
      (IOHDLC_USE_TWA(s) ? IOHDLC_F_ISRCVED(s) : true) :
      IOHDLC_P_ISRCVED(s);

  bool i_frame_sent = false;  /* Track if at least one I-frame was sent. */
  
  while (!ioHdlc_frameq_isempty(&p->i_trans_q)) {
    /* Check transmission window availability. */
    const uint32_t outstanding = (p->vs >= p->nr) ?
        (p->vs - p->nr) : (p->ks - (p->nr - p->vs) + 1);
    if (outstanding >= p->ks)
      /* Window full: cannot send I-frames. */
      break;

    if (!thereIsSendOpportunity(s, pf_received))
      /* Cannot send I-frame now (physical layer busy or TWA waiting for link). */
      break;

    /* Extract frame from transmission queue with lookahead. */
    iohdlc_frame_t *next_fp = NULL;
    iohdlc_frame_t *fp = ioHdlc_frameq_remove_la(&p->i_trans_q, &next_fp);
    if (fp == NULL) break;  /* Safety check. */

    /* Determine whether to set P/F bit.
       Primary TWA: Set P on LAST I-frame (always, we have the link).
       Primary TWS: Set P as soon as possible (if no P in flight).
       Secondary (TWA & TWS): Set F on LAST I-frame (when window will be full or no more frames). */
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
        ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, 100);
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
    /* In TWA, if we have permission (P/F received) but didn't send I-frames,
       we should send an S-frame to acknowledge and cede the link.
       In TWS, we may also want to send periodic acknowledgments. */
    if (thereIsSendOpportunity(s, pf_received)) {
      /* Determine S-frame function: RR or RNR based on reception queue state. */
      if (s->flags & IOHDLC_FLG_BUSY) {
        /* Reception queue is busy/full: send RNR. */
        p->ss_fun = IOHDLC_S_RNR;
      } else {
        /* Reception queue is ready: send RR. */
        p->ss_fun = IOHDLC_S_RR;
      }
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
  bool r, sPF;

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
      sPF = IOHDLC_P_ISRCVED(s);
      cm_flags &= ~IOHDLC_EVT_UMRECVD;        /* serve the event, if any.*/
      if (thereIsSendOpportunity(s, sPF)) {
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
        p->poll_retry_count++;
        if (p->poll_retry_count >= p->poll_retry_max) {
          /* Retry limit exceeded: link is down. */
          ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINKDOWN);
          /* Reset retry counter and abort this U command attempt. */
          p->poll_retry_count = 0;
          p->um_state &= ~(IOHDLC_UM_SENDING | IOHDLC_UM_SENT);
          ioHdlcNextPeer(s);  /* Move to next peer. */
          continue;
        }
        /* Retry: will retransmit below. */
      }

      sPF = true;
      /* A link management has been requested.*/
      p->um_state |= IOHDLC_UM_SENDING;
      if (thereIsSendOpportunity(s, sPF)) {
        /* TODO: Build and send UM command p->um_cmd.*/
        ioHdlcStartReplyTimer(p, IOHDLC_TIMER_REPLY, 100);
        p->um_state &= ~IOHDLC_UM_SENDING;
        p->um_state |= IOHDLC_UM_SENT;
        p->um_sent = p->um_cmd;
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
        uint32_t flags = ioHdlcOnLineIdle(s);
        if (s_runner_ops && s_runner_ops->broadcast_flags && flags)
          s_runner_ops->broadcast_flags(s, flags);
      }
      continue;
    }
    s->flags &= ~IOHDLC_FLG_IDL;
    /* Hand off to core protocol handler (owns fp). */
    ioHdlcOnRxFrame(s, fp);
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
