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
/* Timer implementation (POSIX timer_t)                                      */
/*===========================================================================*/

static iohdlc_virtual_timer_t* s_select_timer(iohdlc_station_peer_t *peer,
                                               iohdlc_timer_kind_t timer_kind) {
  return (timer_kind == IOHDLC_TIMER_I_REPLY) ? &peer->i_reply_tmr : &peer->reply_tmr;
}

static void s_timer_signal_handler(int sig, siginfo_t *si, void *uc) {
  (void)sig;
  (void)uc;
  
  /* Extract timer context from siginfo */
  iohdlc_virtual_timer_t *timer = (iohdlc_virtual_timer_t *)si->si_value.sival_ptr;
  if (!timer || !timer->peer) {
    return;
  }
  
  /* Mark timer as expired */
  timer->expired = true;
  
  /* Signal events to the station - this posts to all listeners */
  iohdlc_station_peer_t *peer = timer->peer;
  iohdlc_evt_broadcast_flags(&peer->stationp->cm_es, (eventflags_t)timer->kind);
}

static void s_setup_timer_signal(void) {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = s_timer_signal_handler;
  sigemptyset(&sa.sa_mask);
  
  if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
    /* Handle error */
    return;
  }
  
  initialized = true;
}

void ioHdlcRunnerStartReplyTimer(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind,
                                 uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  
  s_setup_timer_signal();
  
  /* Stop existing timer if armed */
  if (timer->armed) {
    timer_delete(timer->timer_id);
    timer->armed = false;
  }
  
  /* Create new timer */
  struct sigevent sev;
  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = SIGRTMIN;
  sev.sigev_value.sival_ptr = timer;
  
  if (timer_create(CLOCK_MONOTONIC, &sev, &timer->timer_id) == -1) {
    return;
  }
  
  /* Set timer context */
  timer->peer = peer;
  timer->kind = timer_kind;
  timer->expired = false;
  timer->armed = true;
  
  /* Arm timer */
  struct itimerspec its;
  its.it_value.tv_sec = timeout_ms / 1000;
  its.it_value.tv_nsec = (timeout_ms % 1000) * 1000000;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;
  
  if (timer_settime(timer->timer_id, 0, &its, NULL) == -1) {
    timer_delete(timer->timer_id);
    timer->armed = false;
    return;
  }
}

void ioHdlcRunnerRestartReplyTimer(iohdlc_station_peer_t *peer,
                                   iohdlc_timer_kind_t timer_kind,
                                   uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  
  if (timer->armed) {
    /* Just restart with new timeout */
    timer->expired = false;
    
    struct itimerspec its;
    its.it_value.tv_sec = timeout_ms / 1000;
    its.it_value.tv_nsec = (timeout_ms % 1000) * 1000000;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    
    timer_settime(timer->timer_id, 0, &its, NULL);
  }
}

void ioHdlcRunnerStopReplyTimer(iohdlc_station_peer_t *peer,
                                iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  
  if (timer->armed) {
    timer_delete(timer->timer_id);
    timer->armed = false;
    timer->expired = false;
  }
}

bool ioHdlcRunnerIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                                     iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  return !timer->armed && timer->expired;
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
