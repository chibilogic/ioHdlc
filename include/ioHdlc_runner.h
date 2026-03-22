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
 * @file    ioHdlc_runner.h
 * @brief   Runner for HDLC station core.
 *
 * @details Creates TX/RX execution contexts and maps OS events and timers to
 *          the core entry points. This module is responsible for the execution
 *          model around the protocol core, not for protocol semantics.
 *
 *          The runner is the layer that owns execution contexts such as
 *          threads, waits for OS events, and invokes the core entry points in
 *          the appropriate order. Applications typically interact with it only
 *          through start and stop operations.
 *
 *          Ownership model:
 *          - the station remains owned by the caller;
 *          - the runner owns only the execution contexts and timer/event glue
 *            it creates around the station.
 *
 * @addtogroup ioHdlc_runner
 * @{
 */

#ifndef IOHDLC_RUNNER_H_
#define IOHDLC_RUNNER_H_

#include "ioHdlctypes.h"
#include "ioHdlcosal.h"

/*===========================================================================*/
/* Runner context                                                            */
/*===========================================================================*/

/**
 * @brief   Runtime bookkeeping for runner-owned execution contexts.
 * @details Stores the execution resources created by the runner so they can be
 *          stopped and joined cleanly during shutdown.
 */
typedef struct {
  iohdlc_thread_t *tx_thread;   /* TX thread reference */
  iohdlc_thread_t *rx_thread;   /* RX thread reference */
  bool tx_started;       /* TRUE if TX thread created successfully */
  bool rx_started;       /* TRUE if RX thread created successfully */
} runner_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/** @ingroup ioHdlc_runner */
int32_t ioHdlcRunnerStart(iohdlc_station_t *station);

/** @ingroup ioHdlc_runner */
int32_t ioHdlcRunnerStop(iohdlc_station_t *station);

/** @ingroup ioHdlc_runner */
void ioHdlcStartReplyTimer(iohdlc_station_peer_t *peer,
                           iohdlc_timer_kind_t timer_kind,
                           uint32_t timeout_ms);
/** @ingroup ioHdlc_runner */
void ioHdlcRestartReplyTimer(iohdlc_station_peer_t *peer,
                             iohdlc_timer_kind_t timer_kind,
                             uint32_t timeout_ms);
/** @ingroup ioHdlc_runner */
void ioHdlcStopReplyTimer(iohdlc_station_peer_t *peer,
                          iohdlc_timer_kind_t timer_kind);
/** @ingroup ioHdlc_runner */
bool ioHdlcIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                               iohdlc_timer_kind_t timer_kind);
/** @ingroup ioHdlc_runner */
uint32_t ioHdlcWaitEvents(iohdlc_station_t *station);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_RUNNER_H_ */

/** @} */
