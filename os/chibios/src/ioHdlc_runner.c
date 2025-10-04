/*
 * ChibiOS runner for HDLC station core.
 */
#include "ch.h"
#include "hal.h"

#include "ioHdlc_core.h"
#include "ioHdlc_runner.h"
#include "ioHdlc.h"

static THD_FUNCTION(HdlcTxThread, arg) {
  ioHdlcTxEntry(arg);
}

static THD_FUNCTION(HdlcRxThread, arg) {
  ioHdlcRxEntry(arg);
}

void ioHdlcRunnerStart(iohdlc_station_t *station) {
  static const ioHdlcRunnerOps s_ops = {
    .start_reply_timer   = ioHdlcRunnerStartReplyTimer,
    .restart_reply_timer = ioHdlcRunnerRestartReplyTimer,
    .stop_reply_timer    = ioHdlcRunnerStopReplyTimer,
    .wait_events         = s_wait_events,
    .broadcast_flags     = s_broadcast_flags,
  };
  ioHdlcRegisterRunnerOps(&s_ops);
  (void)ioHdlcCoreInit(station);
  /* Register event listener for station events. */
  chEvtRegisterMaskWithFlags(&station->cm_es, &s_cm_listener, EVENT_MASK(0),
      EVT_CM_RPLYTMO|EVT_CM_UMRECVD|EVT_CM_CONNSTR|EVT_CM_LINIDLE);
  chThdCreateFromHeap(NULL, 2048, "HDLC-TX", NORMALPRIO + 1, HdlcTxThread, station);
  chThdCreateFromHeap(NULL, 2048, "HDLC-RX", NORMALPRIO + 1, HdlcRxThread, station);
}

void ioHdlcRunnerStop(iohdlc_station_t *station) {
  (void)station;
  /* TODO: add thread references and perform graceful stop if needed. */
}

/* Timer callback (OS ISR context) -> maps to core and broadcasts flags. */
static void s_reply_timer_cb(void *peerp) {
  iohdlc_station_peer_t *p = (iohdlc_station_peer_t *)peerp;
  uint32_t flags = ioHdlcOnReplyTimer(p);
  chSysLockFromISR();
  chEvtBroadcastFlagsI(&p->stationp->cm_es, flags);
  chSysUnlockFromISR();
}

void ioHdlcRunnerStartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms) {
  chVTSet(&peer->reply_tmo, TIME_MS2I(timeout_ms), s_reply_timer_cb, peer);
  peer->ss_state |= IOHDLC_SS_RPL_STT;
}

void ioHdlcRunnerRestartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms) {
  if (chVTIsArmed(&peer->reply_tmo)) {
    chVTSet(&peer->reply_tmo, TIME_MS2I(timeout_ms), s_reply_timer_cb, peer);
    peer->ss_state |= IOHDLC_SS_RPL_STT;
  }
}

void ioHdlcRunnerStopReplyTimer(iohdlc_station_peer_t *peer) {
  chVTReset(&peer->reply_tmo);
  peer->ss_state &= ~IOHDLC_SS_RPL_STT;
}
static event_listener_t s_cm_listener;

static uint32_t s_wait_events(iohdlc_station_t *station, uint32_t mask) {
  (void)mask; /* single bucket */
  (void) chEvtWaitAny(EVENT_MASK(0));
  return (uint32_t) chEvtGetAndClearFlags(&s_cm_listener);
}

static void s_broadcast_flags(iohdlc_station_t *station, uint32_t flags) {
  chEvtBroadcastFlags(&station->cm_es, (eventflags_t)flags);
}
