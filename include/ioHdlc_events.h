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
#define IOHDLC_EVT_C_RPLYTMO    0x0001  /* Command reply timer timeout. */
#define IOHDLC_EVT_I_RPLYTMO    0x0002  /* I-frame reply timer timeout. */
#define IOHDLC_EVT_I_RECVD      0x0004  /* I-frame received. */
#define IOHDLC_EVT_RR_RECVD     0x0008  /* RR received. */
#define IOHDLC_EVT_RNR_RECVD    0x0010  /* RNR received. */
#define IOHDLC_EVT_xREJ_RECVD   0x0020  /* REJ received. */
#define IOHDLC_EVT_POOL_ST_CHG  0x0040  /* Frame pool state changed. */
#define IOHDLC_EVT_UM_RECVD     0x0080  /* Unnumbered command received. u_rsp
                                           indicates the response. */
#define IOHDLC_EVT_LINK_ST_CHG  0x0100  /* Link state changed. */
#define IOHDLC_EVT_LINK_REQ     0x0200  /* Link change requested. u_cmd
                                           indicates the requested change. */
#define IOHDLC_EVT_LINE_IDLE    0x0400  /* Line idle detected. */
#define IOHDLC_EVT_LINK_DOWN    0x0800  /* Link down (retry count exhausted). */
#define IOHDLC_EVT_TX_IFRM_ENQ  0x1000  /* I-frame enqueued for transmission. */
#define IOHDLC_EVT_REJ_ACTED    0x2000  /* REJ exception has been actioned. */
#define IOHDLC_EVT_PF_RECVD     0x4000  /* P/F bit received. */

#endif /* IOHDLC_EVENTS_H_ */
