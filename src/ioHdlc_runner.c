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
 * @brief   Runner for HDLC station core.
 *
 * @details Manages TX/RX threads and integrates timers/events with core.
 */

#include "ioHdlc_runner.h"
#include "ioHdlc_core.h"
#include "ioHdlc.h"
#include "ioHdlcosal.h"
#include <string.h>
#include <errno.h>

/*===========================================================================*/
/* Forward declarations for runner ops                                       */
/*===========================================================================*/

static void *HdlcTxThread(void *arg) {
  ioHdlcTxEntry(arg);
  return 0;
}

static void *HdlcRxThread(void *arg) {
  ioHdlcRxEntry(arg);
  return 0;
}

int32_t ioHdlcRunnerStart(iohdlc_station_t *station) {
  
  /* Allocate and initialize runner context */
  runner_context_t *ctx = IOHDLC_MALLOC(sizeof *ctx);
  if (!ctx) {
    iohdlc_errno = ENOMEM;
    return -1;  /* Out of memory - abort runner start */
  }
  memset(ctx, 0, sizeof *ctx);
  station->runner_context = ctx;
  station->stop_requested = false;
  
  /* Create TX thread and save reference (joinable) */
  ctx->tx_thread = iohdlc_thread_create("HDLC-TX", 2048, 1,
                                         HdlcTxThread, station);
  if (ctx->tx_thread == NULL) {
    /* TX thread creation failed - cleanup and abort */
    IOHDLC_FREE(ctx);
    station->runner_context = NULL;
    iohdlc_errno = EAGAIN;  /* Resource temporarily unavailable */
    return -1;
  }
  ctx->tx_started = true;
  
  /* Create RX thread and save reference (joinable) */
  ctx->rx_thread = iohdlc_thread_create("HDLC-RX", 2048, 1,
                                         HdlcRxThread, station);
  if (ctx->rx_thread == NULL) {
    /* RX thread creation failed - stop TX thread and cleanup */
    station->stop_requested = true;
    iohdlc_evt_broadcast_flags(&station->cm_es, 0xFFFFFFFF);  /* Wake TX */
    iohdlc_thread_join((iohdlc_thread_t *)ctx->tx_thread);  /* Wait for TX termination */
    IOHDLC_FREE(ctx);
    station->runner_context = NULL;
    iohdlc_errno = EAGAIN;  /* Resource temporarily unavailable */
    return -1;
  }
  ctx->rx_started = true;
  
  /* Success */
  iohdlc_errno = 0;
  return 0;
}

int32_t ioHdlcRunnerStop(iohdlc_station_t *station) {
  runner_context_t *ctx = station->runner_context;
  
  if (!ctx) {
    iohdlc_errno = 0;
    return 0;  /* Runner not started or already stopped - not an error */
  }
  
  /* Request threads to stop */
  station->stop_requested = true;
  
  /* Wake up TX thread (may be blocked in wait_events) */
  iohdlc_evt_broadcast_flags(&station->cm_es, 0xFFFFFFFF);
  
  /* Join threads - wait for graceful termination */
  if (ctx->tx_started && ctx->tx_thread != NULL) {
    iohdlc_thread_join(ctx->tx_thread);  /* Block until TX exits */
    ctx->tx_started = false;
  }
  
  if (ctx->rx_started && ctx->rx_thread != NULL) {
    iohdlc_thread_join(ctx->rx_thread);  /* RX has internal timeout, will exit */
    ctx->rx_started = false;
  }
  
  /* Free context */
  IOHDLC_FREE(ctx);
  station->runner_context = NULL;
  
  iohdlc_errno = 0;
  return 0;
}

static iohdlc_virtual_timer_t *s_select_timer(iohdlc_station_peer_t *peer,
                                              iohdlc_timer_kind_t timer_kind) {
  return (timer_kind == IOHDLC_TIMER_T3) ? &peer->t3_tmr : &peer->reply_tmr;
}

/* Timer callbacks (OS ISR context) -> wrap common handler. */
static void s_timer_cb(iohdlc_virtual_timer_t *timer, void *arg) {
  (void) arg;
  timer->expired = true;  /* Mark timer as expired. */
  iohdlc_evt_broadcast_flags_isr(timer->esp, timer->evt_flag);
}

void ioHdlcStartReplyTimer(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind,
                                 uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  timer->expired = false;
  iohdlc_vt_set(timer, timeout_ms, s_timer_cb, 0);
}

void ioHdlcRestartReplyTimer(iohdlc_station_peer_t *peer,
                                   iohdlc_timer_kind_t timer_kind,
                                   uint32_t timeout_ms) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  if (iohdlc_vt_is_armed(timer)) {
    timer->expired = false;
    iohdlc_vt_set(timer, timeout_ms, s_timer_cb, 0);
  }
}

void ioHdlcStopReplyTimer(iohdlc_station_peer_t *peer,
                                iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  iohdlc_vt_reset(timer);
  timer->expired = false;  /* Clear expired flag when explicitly stopped. */
}

bool ioHdlcIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                                     iohdlc_timer_kind_t timer_kind) {
  iohdlc_virtual_timer_t *timer = s_select_timer(peer, timer_kind);
  /* Timer is expired if it's not armed AND the expired flag is set.
   * This distinguishes "expired" from "never started" or "explicitly stopped". */
  return !iohdlc_vt_is_armed(timer) && timer->expired;
}

uint32_t ioHdlcWaitEvents(iohdlc_station_t *station) {
  (void) iohdlc_evt_wait_any_timeout(EVENT_MASK(0), IOHDLC_WAIT_FOREVER);
  return (uint32_t) iohdlc_evt_get_and_clear_flags(&station->cm_listener);
}
