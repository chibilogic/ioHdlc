/*
 * ChibiOS runner for HDLC station core.
 */
#include "ch.h"
#include "hal.h"

#include "ioHdlc_core.h"
#include "ioHdlc_runner.h"
#include "ioHdlc.h"

static event_listener_t s_cm_listener;

static THD_FUNCTION(HdlcTxThread, arg) {
  ioHdlcTxEntry(arg);
}

static THD_FUNCTION(HdlcRxThread, arg) {
  ioHdlcRxEntry(arg);
}

void ioHdlcRunnerStart(iohdlc_station_t *station) {
  static const ioHdlcRunnerOps s_ops = {
    .start_reply_timer      = ioHdlcRunnerStartReplyTimer,
    .restart_reply_timer    = ioHdlcRunnerRestartReplyTimer,
    .stop_reply_timer       = ioHdlcRunnerStopReplyTimer,
    .is_reply_timer_expired = ioHdlcRunnerIsReplyTimerExpired,
    .wait_events            = s_wait_events,
    .broadcast_flags        = s_broadcast_flags,
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

static iohdlc_virtual_timer_t *s_select_timer(iohdlc_station_peer_t *peer,
                                              iohdlc_timer_kind_t timer_kind) {
  return (timer_kind == IOHDLC_TIMER_I_REPLY) ? &peer->i_reply_tmr : &peer->reply_tmr;
}

static void s_handle_timer_expiry(iohdlc_station_peer_t *peer,
                                  iohdlc_timer_kind_t timer_kind) {
  chSysLockFromISR();
  chEvtBroadcastFlagsI(&peer->stationp->cm_es, (eventflags_t)timer_kind);
  chSysUnlockFromISR();
}

/* Timer callbacks (OS ISR context) -> wrap common handler. */
static void s_reply_timer_cb(void *arg) {
  s_handle_timer_expiry((iohdlc_station_peer_t *)arg, IOHDLC_TIMER_REPLY);
}

static void s_i_reply_timer_cb(void *arg) {
  s_handle_timer_expiry((iohdlc_station_peer_t *)arg, IOHDLC_TIMER_I_REPLY);
}

static vtfunc_t s_select_cb(iohdlc_timer_kind_t timer_kind) {
  return (timer_kind == IOHDLC_TIMER_I_REPLY) ? s_i_reply_timer_cb : s_reply_timer_cb;
}

void ioHdlcRunnerStartReplyTimer(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind,
                                 uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  chVTSet(timer, TIME_MS2I(timeout_ms), s_select_cb(timer_kind), peer);
}

void ioHdlcRunnerRestartReplyTimer(iohdlc_station_peer_t *peer,
                                   iohdlc_timer_kind_t timer_kind,
                                   uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  if (chVTIsArmed(timer)) {
    chVTSet(timer, TIME_MS2I(timeout_ms), s_select_cb(timer_kind), peer);
  }
}

void ioHdlcRunnerStopReplyTimer(iohdlc_station_peer_t *peer,
                                iohdlc_timer_kind_t timer_kind) {
  chVTReset(s_select_timer(peer, timer_kind));
}

bool ioHdlcRunnerIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                                     iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  return !chVTIsArmed(timer);
}

static uint32_t s_wait_events(iohdlc_station_t *station, uint32_t mask) {
  (void)mask; /* single bucket */
  (void) chEvtWaitAny(EVENT_MASK(0));
  return (uint32_t) chEvtGetAndClearFlags(&s_cm_listener);
}

static void s_broadcast_flags(iohdlc_station_t *station, uint32_t flags) {
  chEvtBroadcastFlags(&station->cm_es, (eventflags_t)flags);
}
