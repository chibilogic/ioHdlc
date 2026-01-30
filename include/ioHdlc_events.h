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
#ifndef IOHDLC_EVENTS_H_
#define IOHDLC_EVENTS_H_
/* Event masks used for command management. */

/**
 * @brief     Event flags
 */
#define IOHDLC_EVT_C_RPLYTMO  0x0001  /* Command reply timer timeout. */
#define IOHDLC_EVT_I_RPLYTMO  0x0002  /* I-frame reply timer timeout. */
#define IOHDLC_EVT_UMRECVD    0x0004  /* Unnumbered command received. */
#define IOHDLC_EVT_CONNCHG    0x0008  /* Connection state change attempted. */
#define IOHDLC_EVT_CONNSTR    0x0010  /* Connection start/stop requested. */
#define IOHDLC_EVT_LINIDLE    0x0020  /* Line idle detected. */
#define IOHDLC_EVT_SSNDREQ    0x0040  /* Supervision frame transmit requested. */
#define IOHDLC_EVT_LINKDOWN   0x0080  /* Link down (retry count exhausted). */
#define IOHDLC_EVT_ISNDREQ    0x0100  /* I-frame transmit requested. */
#define IOHDLC_EVT_PFHONOR    0x0200  /* Check if we must honor P/F. */
#define IOHDLC_EVT_POOLNORM   0x0400  /* Frame pool returned to normal. */
#endif /* IOHDLC_EVENTS_H_ */
