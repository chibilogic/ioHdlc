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
 * @file    include/ioHdlc_app_events.h
 * @brief   Application-facing link event definitions.
 * @details Defines coarse-grained status notifications that an integration can
 *          expose upward when mapping protocol behaviour to application-level
 *          events.
 *
 *          These flags are intentionally higher level than the internal event
 *          masks in @ref ioHdlc_events.h. They are suitable for user-facing
 *          state changes such as link establishment, refusal, teardown, and
 *          loss detection.
 *
 * @addtogroup ioHdlc_api
 * @{
 */

#ifndef IOHDLC_APP_EVENTS_H_
#define IOHDLC_APP_EVENTS_H_

/**
 * @brief   Application-visible link event flags.
 */
#define IOHDLC_APP_LINK_UP       0x0001 /**< Link established successfully (UA received). */
#define IOHDLC_APP_LINK_REFUSED  0x0002 /**< Link connection refused (DM received). */
#define IOHDLC_APP_LINK_DOWN     0x0004 /**< Link disconnected (UA/DM after DISC). */
#define IOHDLC_APP_LINK_TIMEOUT  0x0008 /**< Link operation timeout (no response). */
#define IOHDLC_APP_LINK_LOST     0x0010 /**< Link lost spontaneously (unexpected DM/timeout). */
#define IOHDLC_APP_DATA_READY    0x0020 /**< Data available for reading (future use). */
#define IOHDLC_APP_FRMR_RECEIVED 0x0040 /**< FRMR received from peer (link recovery needed). */
#endif /* IOHDLC_APP_EVENTS_H_ */

/** @} */
