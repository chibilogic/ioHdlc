/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
 */
/**
 * @file    ioHdlc_link_manager.c
 * @brief   Optional application-level ioHdlc link manager utility.
 */

#include <errno.h>
#include <string.h>

#include "ioHdlc_link_manager.h"

#define IOHDLC_LINK_MANAGER_STOP_FLAG 0x01U

/**
 * @brief   Read the stop request under the manager lock.
 */
static bool isStopRequested(iohdlc_link_manager_t *managerp) {
  bool requested;

  iohdlc_mutex_lock(&managerp->lock);
  requested = managerp->stop_requested;
  iohdlc_mutex_unlock(&managerp->lock);

  return requested;
}

/**
 * @brief   Notify one effective state transition.
 */
static void notifyState(iohdlc_link_manager_t *managerp,
                        iohdlc_peer_state_t *last_statep) {
  iohdlc_peer_state_t state;

  state = ioHdlcPeerGetState(managerp->config.peerp);
  if (state == *last_statep)
    return;

  *last_statep = state;
  if (managerp->config.on_state_change != NULL)
    managerp->config.on_state_change(
        managerp, state, managerp->config.callback_arg);
}

/**
 * @brief   Wait for a station event, retry deadline, or stop request.
 */
static void waitEvent(iohdlc_app_listener_t *listenerp,
                      iohdlc_event_listener_t *stop_listenerp,
                      uint32_t timeout_ms) {
  (void)ioHdlcAppListenerWait(listenerp, timeout_ms);
  (void)iohdlc_evt_get_and_clear_flags(stop_listenerp);
}

/**
 * @brief   Initialize a caller-owned link manager object.
 * @details Initialization has no thread or listener side effects. Active mode
 *          requires a non-zero retry interval and a valid HDLC mode.
 */
void ioHdlcLinkManagerObjectInit(
    iohdlc_link_manager_t *managerp,
    const iohdlc_link_manager_config_t *configp) {

  IOHDLC_ASSERT(managerp != NULL,
                "ioHdlcLinkManagerObjectInit: null manager");
  IOHDLC_ASSERT(configp != NULL,
                "ioHdlcLinkManagerObjectInit: null config");
  IOHDLC_ASSERT(configp->stationp != NULL,
                "ioHdlcLinkManagerObjectInit: null station");
  IOHDLC_ASSERT(configp->peerp != NULL,
                "ioHdlcLinkManagerObjectInit: null peer");
  IOHDLC_ASSERT(configp->event_mask != 0U &&
                (configp->event_mask & (configp->event_mask - 1U)) == 0U,
                "ioHdlcLinkManagerObjectInit: invalid event mask");
  IOHDLC_ASSERT(configp->event_mask != IOHDLC_APP_EVT_MASK_DEFAULT,
                "ioHdlcLinkManagerObjectInit: reserved event mask");
  IOHDLC_ASSERT(!configp->active || configp->retry_interval_ms != 0U,
                "ioHdlcLinkManagerObjectInit: zero retry interval");
  IOHDLC_ASSERT(!configp->active ||
                IOHDLC_MODE_TO_UCMD(configp->mode) != 0U,
                "ioHdlcLinkManagerObjectInit: invalid mode");
  IOHDLC_ASSERT(!configp->active || configp->connect != NULL ||
                !IOHDLC_IS_SEC(configp->stationp) ||
                configp->mode == IOHDLC_OM_ABM,
                "ioHdlcLinkManagerObjectInit: secondary cannot connect");

  memset(managerp, 0, sizeof *managerp);
  managerp->config = *configp;
  iohdlc_evt_init(&managerp->stop_es);
  iohdlc_mutex_init(&managerp->lock);
}

/**
 * @brief   Run passive monitoring and optional active reconnection.
 * @details The caller supplies the execution thread. The function registers
 *          and unregisters all listeners in that same context. Application
 *          events are treated as prompts to read the synchronized peer state,
 *          so coalescing and events emitted for other peers are harmless.
 * @return  Zero after a stop request, or -1 if the manager is already running
 *          or listener registration fails.
 */
int32_t ioHdlcLinkManagerRun(iohdlc_link_manager_t *managerp) {
  iohdlc_app_listener_t listener;
  iohdlc_event_listener_t stop_listener;
  iohdlc_peer_state_t last_state = IOHDLC_PEER_STATE_INVALID;
  uint32_t retry_deadline = 0U;
  bool retry_pending = false;
  int32_t result;

  IOHDLC_ASSERT(managerp != NULL, "ioHdlcLinkManagerRun: null manager");

  iohdlc_mutex_lock(&managerp->lock);
  if (managerp->running) {
    iohdlc_mutex_unlock(&managerp->lock);
    iohdlc_errno = EBUSY;
    return -1;
  }
  if (managerp->stop_requested) {
    iohdlc_mutex_unlock(&managerp->lock);
    return 0;
  }
  managerp->running = true;
  iohdlc_mutex_unlock(&managerp->lock);

  result = ioHdlcAppListenerRegister(
      managerp->config.stationp, &listener, managerp->config.event_mask,
      IOHDLC_APP_LINK_UP | IOHDLC_APP_LINK_DOWN | IOHDLC_APP_LINK_LOST);
  if (result != 0)
    goto done;

  iohdlc_evt_register(&managerp->stop_es, &stop_listener,
                      managerp->config.event_mask,
                      IOHDLC_LINK_MANAGER_STOP_FLAG);

  notifyState(managerp, &last_state);
  while (!isStopRequested(managerp)) {
    iohdlc_peer_state_t state;

    state = ioHdlcPeerGetState(managerp->config.peerp);
    if (!managerp->config.active || state == IOHDLC_PEER_STATE_CONNECTED) {
      retry_pending = false;
      waitEvent(&listener, &stop_listener, IOHDLC_WAIT_FOREVER);
      notifyState(managerp, &last_state);
      continue;
    }

    if (retry_pending) {
      uint32_t now = iohdlc_time_now_ms();

      if ((int32_t)(now - retry_deadline) < 0) {
        waitEvent(&listener, &stop_listener, retry_deadline - now);
        notifyState(managerp, &last_state);
        continue;
      }
      retry_pending = false;
    }

    if (managerp->config.is_ready == NULL ||
        managerp->config.is_ready(managerp->config.callback_arg)) {
      if (isStopRequested(managerp))
        break;

      if (managerp->config.connect != NULL)
        (void)managerp->config.connect(
            managerp->config.stationp, managerp->config.peerp,
            managerp->config.mode, managerp->config.callback_arg);
      else
        (void)ioHdlcStationLinkUp(managerp->config.stationp,
                                  managerp->config.peerp->addr,
                                  managerp->config.mode);

      notifyState(managerp, &last_state);
      if (ioHdlcPeerGetState(managerp->config.peerp) ==
          IOHDLC_PEER_STATE_CONNECTED)
        continue;
    }

    retry_deadline = iohdlc_time_now_ms() +
                     managerp->config.retry_interval_ms;
    retry_pending = true;
  }

  iohdlc_evt_unregister(&managerp->stop_es, &stop_listener);
  ioHdlcAppListenerUnregister(&listener);
  result = 0;

done:
  iohdlc_mutex_lock(&managerp->lock);
  managerp->running = false;
  iohdlc_mutex_unlock(&managerp->lock);
  return result;
}

/**
 * @brief   Request termination of the manager run loop.
 * @details The stop event wakes passive and retry waits without polling. A
 *          synchronous link-up call already in progress completes according
 *          to its normal ioHdlc timeout before the run loop can return.
 */
void ioHdlcLinkManagerStop(iohdlc_link_manager_t *managerp) {
  IOHDLC_ASSERT(managerp != NULL, "ioHdlcLinkManagerStop: null manager");

  iohdlc_mutex_lock(&managerp->lock);
  managerp->stop_requested = true;
  iohdlc_mutex_unlock(&managerp->lock);
  iohdlc_evt_broadcast_flags(&managerp->stop_es,
                             IOHDLC_LINK_MANAGER_STOP_FLAG);
}
