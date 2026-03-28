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
 * @file    ioHdlc_core.h
 * @brief   OS-agnostic HDLC station core interface.
 *
 * @details Provides entry points for runner implementations and core protocol logic.
 *          This module defines the boundary between the runner/execution model
 *          and the protocol core. The functions declared here are not generic
 *          application entry points; they are intended for the components that
 *          deliver frames, timers, and event notifications to the station core.
 *
 *          Ownership model:
 *          - received frames delivered through this interface are consumed by
 *            the core and must not be reused by the caller unless explicitly
 *            retained by a higher layer;
 *          - timer and event notifications are edge-like execution signals,
 *            not persistent state objects owned by the core.
 *
 *          Execution model:
 *          - runner implementations call these functions from the execution
 *            contexts they own;
 *          - exact thread or ISR context depends on the runner/backend pair
 *            and must be documented by the caller side.
 *
 * @addtogroup ioHdlc_core
 * @{
 */

#ifndef IOHDLC_CORE_H_
#define IOHDLC_CORE_H_

#include "ioHdlctypes.h"
#include "ioHdlcframe.h"

/**
 * @brief   Broadcast internal core event flags.
 */
#define ioHdlcBroadcastFlags(s, flags) iohdlc_evt_broadcast_flags(&(s)->cm_es, (flags))

/**
 * @brief   Broadcast application-facing event flags.
 */
#define ioHdlcBroadcastFlagsApp(s, flags) iohdlc_evt_broadcast_flags(&(s)->app_es, flags)

#ifdef __cplusplus
extern "C" {
#endif

/* Runner -> Core: entry points */

/** @ingroup ioHdlc_core */
void ioHdlcOnRxFrame(iohdlc_station_t *station, iohdlc_frame_t *fp);

/** @ingroup ioHdlc_core */
uint32_t ioHdlcOnLineIdle(iohdlc_station_t *station);

/* Thread/task entry points (runner creates threads and calls these). */
/** @ingroup ioHdlc_core */
void ioHdlcTxEntry(void *stationp);
/** @ingroup ioHdlc_core */
void ioHdlcRxEntry(void *stationp);

/* Mode-specific TX/RX handlers (exposed for ioHdlcStationInit). */
/** @ingroup ioHdlc_core */
uint32_t nrmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
/** @ingroup ioHdlc_core */
void nrmRx(iohdlc_station_t *s, iohdlc_frame_t *fp);
/** @ingroup ioHdlc_core */
uint32_t armTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
/** @ingroup ioHdlc_core */
void armRx(iohdlc_station_t *s, iohdlc_frame_t *fp);
/** @ingroup ioHdlc_core */
uint32_t abmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
/** @ingroup ioHdlc_core */
void abmRx(iohdlc_station_t *s, iohdlc_frame_t *fp);

/** @ingroup ioHdlc_core
 *  @see ioHdlcNextPeer() */
iohdlc_station_peer_t *ioHdlcNextPeer(iohdlc_station_t *station, bool find_pending_only);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_CORE_H_ */

/** @} */
