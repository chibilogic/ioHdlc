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
 * @file    ioHdlc_runner.c
 * @brief   ChibiOS runner for HDLC station core.
 *
 * @details Manages TX/RX threads and integrates ChibiOS timers/events with core.
 */

#include "ch.h"
#include "hal.h"

#include "ioHdlc_core.h"
#include "ioHdlc_runner.h"
#include "ioHdlc.h"
#include <string.h>

/*===========================================================================*/
/* Runner context (ChibiOS-specific)                                        */
/*===========================================================================*/

typedef struct {
  thread_t *tx_thread;   /* TX thread reference from chThdCreateFromHeap */
  thread_t *rx_thread;   /* RX thread reference */
  bool tx_started;       /* TRUE if TX thread created successfully */
  bool rx_started;       /* TRUE if RX thread created successfully */
} chibios_runner_context_t;

/*===========================================================================*/
/* Forward declarations for runner ops                                       */
/*===========================================================================*/
static uint32_t s_wait_events(iohdlc_station_t *station, uint32_t mask);
static uint32_t s_get_events_flags(iohdlc_station_t *station);
static void s_broadcast_flags(iohdlc_station_t *station, uint32_t flags);
static void s_broadcast_flags_app(iohdlc_station_t *station, uint32_t flags);

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
    .broadcast_flags_app    = s_broadcast_flags_app,
    .get_events_flags       = s_get_events_flags,
  };

  ioHdlcRegisterRunnerOps(&s_ops);
  
  /* Allocate and initialize runner context */
  chibios_runner_context_t *ctx = chHeapAlloc(NULL, sizeof(chibios_runner_context_t));
  if (!ctx) {
    return;  /* Out of memory - abort runner start */
  }
  memset(ctx, 0, sizeof *ctx);
  station->runner_context = ctx;
  station->stop_requested = false;
  
  /* Create TX thread and save reference (joinable) */
  ctx->tx_thread = chThdCreateFromHeap(NULL, 2048, "HDLC-TX", 
                                       NORMALPRIO + 1, HdlcTxThread, station);
  if (ctx->tx_thread == NULL) {
    /* TX thread creation failed - cleanup and abort */
    chHeapFree(ctx);
    station->runner_context = NULL;
    return;
  }
  ctx->tx_started = true;
  
  /* Create RX thread and save reference (joinable) */
  ctx->rx_thread = chThdCreateFromHeap(NULL, 2048, "HDLC-RX",
                                       NORMALPRIO + 1, HdlcRxThread, station);
  if (ctx->rx_thread == NULL) {
    /* RX thread creation failed - stop TX thread and cleanup */
    station->stop_requested = true;
    chEvtBroadcastFlags(&station->cm_es, 0xFFFFFFFF);  /* Wake TX */
    chThdWait(ctx->tx_thread);  /* Wait for TX termination */
    chHeapFree(ctx);
    station->runner_context = NULL;
    return;
  }
  ctx->rx_started = true;
}

void ioHdlcRunnerStop(iohdlc_station_t *station) {
  chibios_runner_context_t *ctx = (chibios_runner_context_t *)station->runner_context;
  
  if (!ctx) {
    return;  /* Runner not started or already stopped */
  }
  
  /* Request threads to stop */
  station->stop_requested = true;
  
  /* Wake up TX thread (may be blocked in wait_events) */
  chEvtBroadcastFlags(&station->cm_es, 0xFFFFFFFF);
  
  /* Join threads - wait for graceful termination */
  if (ctx->tx_started && ctx->tx_thread != NULL) {
    msg_t exit_msg = chThdWait(ctx->tx_thread);  /* Block until TX exits */
    (void)exit_msg;  /* Ignore exit code for now */
    ctx->tx_started = false;
  }
  
  if (ctx->rx_started && ctx->rx_thread != NULL) {
    msg_t exit_msg = chThdWait(ctx->rx_thread);  /* RX has internal timeout, will exit */
    (void)exit_msg;
    ctx->rx_started = false;
  }
  
  /* Free context (working area already freed by ChibiOS after Wait) */
  chHeapFree(ctx);
  station->runner_context = NULL;
}

static iohdlc_virtual_timer_t *s_select_timer(iohdlc_station_peer_t *peer,
                                              iohdlc_timer_kind_t timer_kind) {
  return (timer_kind == IOHDLC_TIMER_T3) ? &peer->t3_tmr : &peer->reply_tmr;
}

static void s_handle_timer_expiry(iohdlc_station_peer_t *peer,
                                  iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  timer->expired = true;  /* Mark timer as expired. */
  chSysLockFromISR();
  chEvtBroadcastFlagsI(&peer->stationp->cm_es, (eventflags_t)timer_kind);
  chSysUnlockFromISR();
}

/* Timer callbacks (OS ISR context) -> wrap common handler. */
static void s_reply_timer_cb(virtual_timer_t *vt, void *arg) {
  (void)vt;
  s_handle_timer_expiry((iohdlc_station_peer_t *)arg, IOHDLC_TIMER_REPLY);
}

static void s_t3_reply_timer_cb(virtual_timer_t *vt, void *arg) {
  (void)vt;
  s_handle_timer_expiry((iohdlc_station_peer_t *)arg, IOHDLC_TIMER_T3);
}

static vtfunc_t s_select_cb(iohdlc_timer_kind_t timer_kind) {
  return (timer_kind == IOHDLC_TIMER_T3) ? s_t3_reply_timer_cb : s_reply_timer_cb;
}

void ioHdlcRunnerStartReplyTimer(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind,
                                 uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  timer->expired = false;
  chVTSet(&timer->vt, TIME_MS2I(timeout_ms), s_select_cb(timer_kind), peer);
}

void ioHdlcRunnerRestartReplyTimer(iohdlc_station_peer_t *peer,
                                   iohdlc_timer_kind_t timer_kind,
                                   uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  if (chVTIsArmed(&timer->vt)) {
    timer->expired = false;
    chVTSet(&timer->vt, TIME_MS2I(timeout_ms), s_select_cb(timer_kind), peer);
  }
}

void ioHdlcRunnerStopReplyTimer(iohdlc_station_peer_t *peer,
                                iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  chVTReset(&timer->vt);
  timer->expired = false;  /* Clear expired flag when explicitly stopped. */
}

bool ioHdlcRunnerIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                                     iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  /* Timer is expired if it's not armed AND the expired flag is set.
   * This distinguishes "expired" from "never started" or "explicitly stopped". */
  return !chVTIsArmed(&timer->vt) && timer->expired;
}

static uint32_t s_wait_events(iohdlc_station_t *station, uint32_t mask) {
  (void)mask; /* single bucket */
  (void) chEvtWaitAny(EVENT_MASK(0));
  return (uint32_t) chEvtGetAndClearFlags(&station->cm_listener);
}

static uint32_t s_get_events_flags(iohdlc_station_t *station) {
  return (uint32_t) chEvtGetAndClearFlags(&station->cm_listener);
}

static void s_broadcast_flags(iohdlc_station_t *station, uint32_t flags) {
  chEvtBroadcastFlags(&station->cm_es, (eventflags_t)flags);
}

static void s_broadcast_flags_app(iohdlc_station_t *station, uint32_t flags) {
  chEvtBroadcastFlags(&station->app_es, (eventflags_t)flags);
}
