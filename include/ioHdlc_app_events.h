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
#ifndef IOHDLC_APP_EVENTS_H_
#define IOHDLC_APP_EVENTS_H_
/* Application event masks for link management. */
#define IOHDLC_APP_LINK_UP       0x0001  /* Link established successfully (UA received). */
#define IOHDLC_APP_LINK_REFUSED  0x0002  /* Link connection refused (DM received). */
#define IOHDLC_APP_LINK_DOWN     0x0004  /* Link disconnected (UA/DM after DISC). */
#define IOHDLC_APP_LINK_TIMEOUT  0x0008  /* Link operation timeout (no response). */
#define IOHDLC_APP_LINK_LOST     0x0010  /* Link lost spontaneously (unexpected DM/timeout). */
#define IOHDLC_APP_DATA_READY    0x0020  /* Data available for reading (future use). */
#endif /* IOHDLC_APP_EVENTS_H_ */
