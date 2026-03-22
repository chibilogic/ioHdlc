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
 * @file    include/ioHdlc_events.h
 * @brief   Core event mask definitions for the protocol engine.
 * @details Defines the logical event bits exchanged between the protocol
 *          state machine, runner, timers, and resource-management hooks.
 *
 *          These constants describe protocol-level conditions only. They do
 *          not prescribe how an OS/backend stores, waits on, or broadcasts the
 *          corresponding events.
 *
 * @addtogroup ioHdlc_runner
 * @{
 */

#ifndef IOHDLC_EVENTS_H_
#define IOHDLC_EVENTS_H_

/**
 * @brief   Protocol event flags.
 * @details Multiple flags may be combined in the same event word.
 */
#define IOHDLC_EVT_C_RPLYTMO    0x0001  /* Command reply timer timeout. */
#define IOHDLC_EVT_T3_TMO       0x0002  /* T3 timer timeout. */
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

/** @} */
