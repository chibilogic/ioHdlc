/*
    ioHdlc - Copyright (C) 2024 Isidoro Orabona

    GNU General Public License Usage

    ioHdlc software is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ioHdlc software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with ioHdlc software.  If not, see <http://www.gnu.org/licenses/>.

    Commercial License Usage

    Licensees holding valid commercial ioHdlc licenses may use this file in
    accordance with the commercial license agreement provided in accordance with
    the terms contained in a written agreement between you and Isidoro Orabona.
    For further information contact via email on github account.
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

#include "ioHdlc_events.h"

/* ssize_t typedef for Write/Read APIs (follows POSIX semantics) */
#if !defined(ssize_t) && !defined(_SSIZE_T_DEFINED)
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
