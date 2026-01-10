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
 * @brief   Linux/POSIX runner for HDLC station core.
 *
 * @details Creates TX/RX threads and maps pthread/timer to core entry points.
 */

#ifndef IOHDLC_RUNNER_H_
#define IOHDLC_RUNNER_H_

#include "ioHdlctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

void ioHdlcRunnerStart(iohdlc_station_t *station);
void ioHdlcRunnerStop(iohdlc_station_t *station);

/* Reply timer control (runner side, maps to POSIX timers). */
void ioHdlcRunnerStartReplyTimer(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind,
                                 uint32_t timeout_ms);
void ioHdlcRunnerRestartReplyTimer(iohdlc_station_peer_t *peer,
                                   iohdlc_timer_kind_t timer_kind,
                                   uint32_t timeout_ms);
void ioHdlcRunnerStopReplyTimer(iohdlc_station_peer_t *peer,
                                iohdlc_timer_kind_t timer_kind);
bool ioHdlcRunnerIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                                     iohdlc_timer_kind_t timer_kind);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_RUNNER_H_ */
