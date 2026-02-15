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
 * @file    ioHdlc_core.h
 * @brief   OS-agnostic HDLC station core interface.
 *
 * @details Provides entry points for runner implementations and core protocol logic.
 *          Option B scaffolding that preserves original src/ioHdlc.c for reference.
 */

#ifndef IOHDLC_CORE_H_
#define IOHDLC_CORE_H_

#include "ioHdlctypes.h"
#include "ioHdlcframe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runner -> Core: entry points */

/* RX path: deliver a received frame to the core (from lower layers). */
void ioHdlcOnRxFrame(iohdlc_station_t *station, iohdlc_frame_t *fp);

/* Line idle notification (runner signals inter-frame idle). */
/* Returns station event flags the runner should signal (e.g., IOHDLC_EVT_LINE_IDLE). */
uint32_t ioHdlcOnLineIdle(iohdlc_station_t *station);

/* Runner ops registration (timer controls etc.). */
typedef struct ioHdlcRunnerOps {
  void (*start_reply_timer)(iohdlc_station_peer_t *peer,
                            iohdlc_timer_kind_t timer_kind,
                            uint32_t timeout_ms);
  void (*restart_reply_timer)(iohdlc_station_peer_t *peer,
                              iohdlc_timer_kind_t timer_kind,
                              uint32_t timeout_ms);
  void (*stop_reply_timer)(iohdlc_station_peer_t *peer,
                           iohdlc_timer_kind_t timer_kind);
  bool (*is_reply_timer_expired)(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind);
  /* Event helpers */
  uint32_t (*wait_events)(iohdlc_station_t *station, uint32_t mask);
  void (*broadcast_flags)(iohdlc_station_t *station, uint32_t flags);
  void (*broadcast_flags_app)(iohdlc_station_t *station, uint32_t flags);
  uint32_t (*get_events_flags)(iohdlc_station_t *station);
} ioHdlcRunnerOps;

void ioHdlcRegisterRunnerOps(const ioHdlcRunnerOps *ops);

/* Core → Runner: wrappers to control reply timer via registered ops. */
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
void ioHdlcBroadcastFlags(iohdlc_station_t *s, uint32_t flags);


/* Thread/task entry points (runner creates threads and calls these). */
void ioHdlcTxEntry(void *stationp);
void ioHdlcRxEntry(void *stationp);

/* Mode-specific TX/RX handlers (exposed for ioHdlcStationInit). */
uint32_t nrmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
void nrmRx(iohdlc_station_t *s, iohdlc_frame_t *fp);
uint32_t armTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
void armRx(iohdlc_station_t *s, iohdlc_frame_t *fp);
uint32_t abmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
void abmRx(iohdlc_station_t *s, iohdlc_frame_t *fp);

/* helpers */
iohdlc_station_peer_t *ioHdlcNextPeer(iohdlc_station_t *station);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_CORE_H_ */
