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
 * @file    include/ioHdlctypes.h
 * @brief   HDLC types header.
 * @details
 *
 * @addtogroup hdlc_types
 * @{
 */

#ifndef IOHDLCTYPES_H_
#define IOHDLCTYPES_H_

#include <stdint.h>
#include <stdbool.h>

/* Include sys/types.h if available to get ssize_t from system headers */
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__) || \
    (defined(__NEWLIB__) && defined(__arm__))
#include <sys/types.h>
#endif

#include "ioHdlc_events.h"

/* Define ssize_t only if system headers didn't provide it */
#if !defined(_SSIZE_T_DEFINED) && !defined(__ssize_t_defined) && !defined(_SSIZE_T_DECLARED)
#if defined(__LP64__) || defined(_WIN64)
typedef int64_t ssize_t;
#else
typedef int32_t ssize_t;
#endif
#define _SSIZE_T_DEFINED
#endif

typedef struct iohdlc_station iohdlc_station_t;
typedef struct iohdlc_station_config iohdlc_station_config_t;
typedef struct iohdlc_station_peer iohdlc_station_peer_t;
typedef struct iohdlc_peer_list iohdlc_peer_list_t;
typedef struct iohdlc_frame iohdlc_frame_t;
typedef struct iohdlc_frame_q iohdlc_frame_q_t;
typedef uint32_t iohdlc_timeout_t;
typedef uint32_t (*iohdlc_tx_fn_t)(iohdlc_station_t *s,
                                   iohdlc_station_peer_t *p,
                                   uint32_t cm_flags);

typedef void (*iohdlc_rx_fn_t)(iohdlc_station_t *s,
                               iohdlc_frame_t *fp);

typedef enum {
  IOHDLC_TIMER_REPLY   = IOHDLC_EVT_C_RPLYTMO,
  IOHDLC_TIMER_I_REPLY = IOHDLC_EVT_I_RPLYTMO,
} iohdlc_timer_kind_t;

/**
 * @brief   Infinite timeout for blocking operations.
 * @details Used in Write/Read APIs to wait indefinitely.
 */
#define IOHDLC_WAIT_FOREVER  0xFFFFFFFFU

/* Forward declaration for mutex type (defined in OSAL) */
typedef struct iohdlc_mutex iohdlc_mutex_t;

#endif /* IOHDLCTYPES_H_ */

/** @} */
