/*
 * OS-agnostic HDLC station core (Option B scaffolding).
 * Note: This is a scaffold preserving original src/ioHdlc.c.
 * Real logic should be progressively migrated here.
 */

#include "ioHdlc_core.h"
#include "ioHdlc.h"

static const ioHdlcRunnerOps *s_runner_ops = NULL;

bool ioHdlcCoreInit(iohdlc_station_t *station) {
  (void)station;
  /* TODO: migrate station initialization logic from src/ioHdlc.c if/when needed. */
  return true;
}

void ioHdlcOnRxFrame(iohdlc_station_t *station, iohdlc_frame_t *fp) {
  (void)station;
  (void)fp;
  /* TODO: hook into RX state machine; currently handled by original code. */
}

uint32_t ioHdlcOnReplyTimer(iohdlc_station_peer_t *peer) {
  /* Original code broadcasted EVT_CM_RPLYTMO; the runner should signal it. */
  (void)peer;
  return (uint32_t)EVT_CM_RPLYTMO;
}

uint32_t ioHdlcOnLineIdle(iohdlc_station_t *station) {
  (void)station;
  return (uint32_t)EVT_CM_LINIDLE;
}

void ioHdlcTxEntry(void *stationp) {
  iohdlc_station_t *s = (iohdlc_station_t *)stationp;
  if (!s) return;
  /* TODO: runner should have registered an event listener for station->cm_es
     and provide wait_events implementation. */
  const uint32_t mask = 0; /* single mask bucket as in original. */
  uint32_t cm_flags = 0;
  for (;;) {
    if (!cm_flags) {
      if (s_runner_ops && s_runner_ops->wait_events)
        cm_flags = s_runner_ops->wait_events(s, mask);
      else
        break; /* cannot wait, exit */
    }
    /* TODO: migrate peer iteration and sending logic from original transmitterTask.
       This skeleton preserves control flow for future porting. */
    cm_flags = 0;
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
    /* Decode address/control as in original. */
    uint8_t *foctp = IOHDLC_HAS_FFF(s) ? &fp->frame[1] : &fp->frame[0];
    uint32_t addr = foctp[0];
    uint8_t ctrl = foctp[1];
    (void)ctrl;
    bool is_a_command = (addr == s->addr);
    (void)is_a_command;
    /* TODO: migrate the rest of receiverTask logic here. */
    /* Until then, release frame to avoid leaks in skeleton. */
    hdlcReleaseFrame(s->frame_pool, fp);
  }
}

void ioHdlcOnEvent(iohdlc_station_t *station, uint32_t event_flags) {
  (void)station;
  (void)event_flags;
  /* TODO: handle station events (EVT_CM_RPLYTMO, EVT_CM_LINIDLE, etc.) here.
     The runner should call this after mapping its OS event mechanism. */
}

void ioHdlcRegisterRunnerOps(const ioHdlcRunnerOps *ops) {
  s_runner_ops = ops;
}

void ioHdlcStartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms) {
  if (s_runner_ops && s_runner_ops->start_reply_timer)
    s_runner_ops->start_reply_timer(peer, timeout_ms);
}

void ioHdlcRestartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms) {
  if (s_runner_ops && s_runner_ops->restart_reply_timer)
    s_runner_ops->restart_reply_timer(peer, timeout_ms);
}

void ioHdlcStopReplyTimer(iohdlc_station_peer_t *peer) {
  if (s_runner_ops && s_runner_ops->stop_reply_timer)
    s_runner_ops->stop_reply_timer(peer);
}
