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
 * @file    ioHdlc_link_manager.h
 * @brief   Optional application-level ioHdlc link manager utility.
 * @details Provides peer-state monitoring and optional active reconnection
 *          without adding policy to the protocol core. Point-to-point users
 *          can also observe link events at unchanged state. The application
 *          owns the runner.
 */

#ifndef IOHDLC_LINK_MANAGER_H_
#define IOHDLC_LINK_MANAGER_H_

#include "ioHdlc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct iohdlc_link_manager iohdlc_link_manager_t;

/**
 * @brief   Test whether the physical integration is ready for link-up.
 * @return  @c true when a connection attempt may start.
 */
typedef bool (*iohdlc_link_manager_ready_cb_t)(void *arg);

/**
 * @brief   Start one link-up attempt.
 * @details A null callback selects ioHdlcStationLinkUp().
 */
typedef int32_t (*iohdlc_link_manager_connect_cb_t)(
    iohdlc_station_t *stationp, iohdlc_station_peer_t *peerp,
    uint8_t mode, void *arg);

/**
 * @brief   Notify an observed change in the managed peer's state.
 * @details Runs on the ioHdlcLinkManagerRun() thread, including an initial
 *          observation after listener registration. Suitable for multipoint:
 *          station events do not notify this callback if the peer state is
 *          unchanged. Transitions completed between observations may coalesce.
 */
typedef void (*iohdlc_link_manager_state_cb_t)(
    iohdlc_link_manager_t *managerp, iohdlc_peer_state_t state, void *arg);

/**
 * @brief   Notify link events together with the current peer state.
 * @details Requires a station with exactly one peer. An alternative to
 *          on_state_change, not an additional lifecycle handler.
 *          The callback runs in the thread executing
 *          ioHdlcLinkManagerRun(), including an initial observation after
 *          listener registration. Flags are coalesced, not a transition log:
 *          an UP event can invalidate an old stream even if state remains
 *          CONNECTED. The callback must retire old I/O before preparing a new
 *          stream. Use ioHdlcLinkManagerCheck() around blocking preparation
 *          and before publication; on false, return without publishing.
 */
typedef void (*iohdlc_link_manager_event_cb_t)(
    iohdlc_link_manager_t *managerp, iohdlc_peer_state_t state,
    eventflags_t events, void *arg);

/**
 * @brief   Link manager configuration.
 * @details Initialize to zero and configure at most one of on_state_change
 *          (peer-state observation, including multipoint) and on_link_event
 *          (single-peer station events, including unchanged final state).
 */
typedef struct {
  iohdlc_station_t *stationp;             /**< Managed station. */
  iohdlc_station_peer_t *peerp;           /**< Managed peer. */
  eventmask_t event_mask;                 /**< Dedicated runner-thread event bit. */
  uint32_t retry_interval_ms;             /**< Active-mode readiness/retry interval. */
  uint8_t mode;                           /**< Mode passed to link-up attempts. */
  bool active;                            /**< Enable active connection attempts. */
  iohdlc_link_manager_ready_cb_t is_ready; /**< Optional readiness hook. */
  iohdlc_link_manager_connect_cb_t connect; /**< Optional link-up hook. */
  iohdlc_link_manager_state_cb_t on_state_change; /**< Optional state hook. */
  void *callback_arg;                     /**< Shared callback argument. */
  iohdlc_link_manager_event_cb_t on_link_event; /**< Alternative point-to-point hook. */
} iohdlc_link_manager_config_t;

/**
 * @brief   Caller-owned link manager object.
 * @details Members are private to the utility and must not be modified after
 *          ioHdlcLinkManagerObjectInit().
 */
struct iohdlc_link_manager {
  iohdlc_link_manager_config_t config;
  iohdlc_event_source_t stop_es;
  iohdlc_mutex_t lock;
  iohdlc_app_listener_t listener;
  eventflags_t pending_events;
  iohdlc_peer_state_t notified_state;
  bool stop_requested;
  bool running;
};

void ioHdlcLinkManagerObjectInit(
    iohdlc_link_manager_t *managerp,
    const iohdlc_link_manager_config_t *configp);
int32_t ioHdlcLinkManagerRun(iohdlc_link_manager_t *managerp);
bool ioHdlcLinkManagerCheck(iohdlc_link_manager_t *managerp);
void ioHdlcLinkManagerStop(iohdlc_link_manager_t *managerp);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_LINK_MANAGER_H_ */
