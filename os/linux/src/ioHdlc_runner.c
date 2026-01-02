/*
 * Linux/POSIX runner for HDLC station core.
 * Creates TX/RX threads and maps pthread/timer to core entry points.
 */

#include "ioHdlc_runner.h"
#include "ioHdlc_core.h"
#include "ioHdlc.h"
#include "ioHdlcosal.h"
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <errno.h>

/*===========================================================================*/
/* Forward declarations                                                      */
/*===========================================================================*/

static uint32_t s_wait_events(iohdlc_station_t *station, uint32_t mask);
static uint32_t s_get_events_flags(iohdlc_station_t *station);
static void s_broadcast_flags(iohdlc_station_t *station, uint32_t flags);
static void s_broadcast_flags_app(iohdlc_station_t *station, uint32_t flags);

/*===========================================================================*/
/* Thread entry points                                                       */
/*===========================================================================*/

static void* hdlc_tx_thread(void *arg) {
  iohdlc_station_t *station = (iohdlc_station_t *)arg;
  
  /* Register event listener for this TX thread */
  iohdlc_evt_register(&station->cm_es, &station->cm_listener,
                      EVENT_MASK(0),
                      IOHDLC_EVT_C_RPLYTMO|IOHDLC_EVT_UMRECVD|
                      IOHDLC_EVT_CONNSTR|IOHDLC_EVT_LINIDLE);
  
  ioHdlcTxEntry(station);
  
  /* Unregister before thread exits */
  iohdlc_evt_unregister(&station->cm_es, &station->cm_listener);
  
  return NULL;
}

static void* hdlc_rx_thread(void *arg) {
  ioHdlcRxEntry((iohdlc_station_t *)arg);
  return NULL;
}

/*===========================================================================*/
/* Runner start/stop                                                         */
/*===========================================================================*/

void ioHdlcRunnerStart(iohdlc_station_t *station) {
  static const ioHdlcRunnerOps s_ops = {
    .start_reply_timer      = ioHdlcRunnerStartReplyTimer,
    .restart_reply_timer    = ioHdlcRunnerRestartReplyTimer,
    .stop_reply_timer       = ioHdlcRunnerStopReplyTimer,
    .is_reply_timer_expired = ioHdlcRunnerIsReplyTimerExpired,
    .wait_events            = s_wait_events,
    .broadcast_flags        = s_broadcast_flags,
    .broadcast_flags_app    = s_broadcast_flags_app,
    .get_events_flags       = s_get_events_flags,
  };

  ioHdlcRegisterRunnerOps(&s_ops);
  
  /* Create TX and RX threads (listener will be registered by TX thread itself) */
  pthread_t tx_thread, rx_thread;
  pthread_attr_t attr;
  
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  
  if (pthread_create(&tx_thread, &attr, hdlc_tx_thread, station) != 0) {
    /* Handle error */
    pthread_attr_destroy(&attr);
    return;
  }
  
  if (pthread_create(&rx_thread, &attr, hdlc_rx_thread, station) != 0) {
    /* Handle error */
    pthread_attr_destroy(&attr);
    return;
  }
  
  pthread_attr_destroy(&attr);
}

void ioHdlcRunnerStop(iohdlc_station_t *station) {
  (void)station;
  /* TODO: add thread references and perform graceful stop if needed. */
}

/*===========================================================================*/
/* Timer implementation using OSAL API                                       */
/*===========================================================================*/

static iohdlc_virtual_timer_t* s_select_timer(iohdlc_station_peer_t *peer,
                                               iohdlc_timer_kind_t timer_kind) {
  return (timer_kind == IOHDLC_TIMER_I_REPLY) ? &peer->i_reply_tmr : &peer->reply_tmr;
}

/**
 * @brief   Timer callback invoked by OSAL when timer expires.
 * @details Called in separate thread (SIGEV_THREAD). Broadcasts event to station.
 */
static void s_timer_callback(void *vtp, void *par) {
  iohdlc_virtual_timer_t *timer = (iohdlc_virtual_timer_t *)vtp;
  iohdlc_station_peer_t *peer = (iohdlc_station_peer_t *)par;
  
  if (!peer || !peer->stationp) {
    return;
  }
  
  /* Broadcast timer expiry event to station's event source */
  iohdlc_evt_broadcast_flags(&peer->stationp->cm_es, (eventflags_t)timer->kind);
}

void ioHdlcRunnerStartReplyTimer(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind,
                                 uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  
  /* Store timer kind for callback */
  timer->kind = timer_kind;
  timer->expired = false;
  
  /* Use OSAL API to set the timer */
  iohdlc_vt_set(timer, timeout_ms, s_timer_callback, peer);
}

void ioHdlcRunnerRestartReplyTimer(iohdlc_station_peer_t *peer,
                                   iohdlc_timer_kind_t timer_kind,
                                   uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  
  if (iohdlc_vt_is_armed(timer)) {
    /* Reset expired flag and restart timer */
    timer->expired = false;
    iohdlc_vt_set(timer, timeout_ms, s_timer_callback, peer);
  }
}

void ioHdlcRunnerStopReplyTimer(iohdlc_station_peer_t *peer,
                                iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  
  /* Use OSAL API to reset the timer */
  iohdlc_vt_reset(timer);
  timer->expired = false;
}

bool ioHdlcRunnerIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                                     iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  return !iohdlc_vt_is_armed(timer) && timer->expired;
}

/*===========================================================================*/
/* Event management                                                          */
/*===========================================================================*/

static uint32_t s_wait_events(iohdlc_station_t *station, uint32_t mask) {
  (void)mask; /* single bucket for now */
  iohdlc_evt_wait_any_timeout(EVENT_MASK(0), IOHDLC_WAIT_FOREVER);
  return iohdlc_evt_get_and_clear_flags(&station->cm_listener);
}

static uint32_t s_get_events_flags(iohdlc_station_t *station) {
  return iohdlc_evt_get_and_clear_flags(&station->cm_listener);
}

static void s_broadcast_flags(iohdlc_station_t *station, uint32_t flags) {
  iohdlc_evt_broadcast_flags(&station->cm_es, (eventflags_t)flags);
}

static void s_broadcast_flags_app(iohdlc_station_t *station, uint32_t flags) {
  iohdlc_evt_broadcast_flags(&station->app_es, (eventflags_t)flags);
}
