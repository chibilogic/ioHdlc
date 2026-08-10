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
 * @details Provides passive link monitoring and optional active reconnection
 *          without adding policy to the protocol core. The application owns
 *          the thread that executes the manager.
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
 * @brief   Notify the application of an effective peer-state transition.
 * @details The callback runs in the thread executing
 *          ioHdlcLinkManagerRun(). It is also called once for the initial
 *          state after the manager listener has been registered.
 */
typedef void (*iohdlc_link_manager_state_cb_t)(
    iohdlc_link_manager_t *managerp, iohdlc_peer_state_t state, void *arg);

/**
 * @brief   Link manager configuration.
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
  bool stop_requested;
  bool running;
};

void ioHdlcLinkManagerObjectInit(
    iohdlc_link_manager_t *managerp,
    const iohdlc_link_manager_config_t *configp);
int32_t ioHdlcLinkManagerRun(iohdlc_link_manager_t *managerp);
void ioHdlcLinkManagerStop(iohdlc_link_manager_t *managerp);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_LINK_MANAGER_H_ */
