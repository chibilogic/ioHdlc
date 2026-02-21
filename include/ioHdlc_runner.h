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
 * @file    ioHdlc_runner.h
 * @brief   Runner for HDLC station core.
 *
 * @details Creates TX/RX threads and maps OS events/timers to core entry points.
 */

#ifndef IOHDLC_RUNNER_H_
#define IOHDLC_RUNNER_H_

#include "ioHdlctypes.h"
#include "ioHdlcosal.h"

/*===========================================================================*/
/* Runner context                                                            */
/*===========================================================================*/

typedef struct {
  iohdlc_thread_t *tx_thread;   /* TX thread reference */
  iohdlc_thread_t *rx_thread;   /* RX thread reference */
  bool tx_started;       /* TRUE if TX thread created successfully */
  bool rx_started;       /* TRUE if RX thread created successfully */
} runner_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Start HDLC runner (TX/RX threads).
 * @param[in] station   Station to start runner for
 * @return              0 on success, -1 on failure (check iohdlc_errno)
 * @retval 0            Success
 * @retval -1           Failure (errno: ENOMEM=out of memory, EAGAIN=thread creation failed)
 */
int32_t ioHdlcRunnerStart(iohdlc_station_t *station);

/**
 * @brief   Stop HDLC runner (join TX/RX threads).
 * @param[in] station   Station to stop runner for
 * @return              0 on success (always succeeds)
 */
int32_t ioHdlcRunnerStop(iohdlc_station_t *station);

/* Reply timer control (runner side, maps to OS timers). */
void ioHdlcStartReplyTimer(iohdlc_station_peer_t *peer,
                           iohdlc_timer_kind_t timer_kind,
                           uint32_t timeout_ms);
void ioHdlcRestartReplyTimer(iohdlc_station_peer_t *peer,
                             iohdlc_timer_kind_t timer_kind,
                             uint32_t timeout_ms);
void ioHdlcStopReplyTimer(iohdlc_station_peer_t *peer,
                          iohdlc_timer_kind_t timer_kind);
bool ioHdlcIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                               iohdlc_timer_kind_t timer_kind);
uint32_t ioHdlcWaitEvents(iohdlc_station_t *station);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_RUNNER_H_ */
