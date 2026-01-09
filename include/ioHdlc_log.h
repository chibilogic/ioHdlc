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
 * @file    ioHdlc_log.h
 * @brief   HDLC frame logging facility.
 * @details Provides compile-time configurable logging for transmitted and
 *          received HDLC frames. Zero overhead when disabled.
 *
 * @addtogroup hdlc_log
 * @{
 */

#ifndef IOHDLC_LOG_H_
#define IOHDLC_LOG_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*===========================================================================*/
/* Logging configuration                                                     */
/*===========================================================================*/

/**
 * @brief   Logging levels.
 */
#define IOHDLC_LOG_LEVEL_OFF     0  /**< No logging (zero overhead).          */
#define IOHDLC_LOG_LEVEL_FRAMES  1  /**< Log frame headers only.              */
#define IOHDLC_LOG_LEVEL_DATA    2  /**< Log headers + first N bytes.         */
#define IOHDLC_LOG_LEVEL_FULL    3  /**< Log headers + complete hex dump.     */

/**
 * @brief   Compile-time logging level.
 * @note    Define in Makefile: -DIOHDLC_LOG_LEVEL=1
 */
#ifndef IOHDLC_LOG_LEVEL
#define IOHDLC_LOG_LEVEL IOHDLC_LOG_LEVEL_OFF
#endif

/**
 * @brief   Direction indicators.
 */
typedef enum {
  IOHDLC_LOG_TX = 0,  /**< Transmitted frame (Send).                        */
  IOHDLC_LOG_RX = 1   /**< Received frame (Receive).                        */
} iohdlc_log_dir_t;

/**
 * @brief   S-frame function codes for logging.
 */
typedef enum {
  IOHDLC_LOG_RR = 0,    /**< Receive Ready.                                 */
  IOHDLC_LOG_RNR = 1,   /**< Receive Not Ready.                             */
  IOHDLC_LOG_REJ = 2,   /**< Reject.                                        */
  IOHDLC_LOG_SREJ = 3   /**< Selective Reject.                              */
} iohdlc_log_sfun_t;

/**
 * @brief   U-frame function codes for logging.
 */
typedef enum {
  IOHDLC_LOG_SNRM = 0,  /**< Set Normal Response Mode.                      */
  IOHDLC_LOG_SARM = 1,  /**< Set Asynchronous Response Mode.                */
  IOHDLC_LOG_SABM = 2,  /**< Set Asynchronous Balanced Mode.                */
  IOHDLC_LOG_DISC = 3,  /**< Disconnect.                                    */
  IOHDLC_LOG_UA = 4,    /**< Unnumbered Acknowledgment.                     */
  IOHDLC_LOG_DM = 5,    /**< Disconnected Mode.                             */
  IOHDLC_LOG_FRMR = 6   /**< Frame Reject.                                  */
} iohdlc_log_ufun_t;

/**
 * @brief   Optional flags for frame context.
 */
#define IOHDLC_LOG_FLAG_RETX   0x01  /**< Retransmitted frame.               */
#define IOHDLC_LOG_FLAG_REJ    0x02  /**< REJ-triggered transmission.        */
#define IOHDLC_LOG_FLAG_BUSY   0x04  /**< Local busy condition.              */

/*===========================================================================*/
/* Logging API                                                               */
/*===========================================================================*/

#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF

/**
 * @brief   Runtime enable/disable flag.
 * @note    Can be modified at runtime to control logging dynamically.
 */
extern bool iohdlc_log_enabled;

/**
 * @brief   Log an I-frame (Information frame).
 *
 * @param[in] dir       Direction (TX or RX)
 * @param[in] saddr     Station address (local)
 * @param[in] addr      Address field in frame
 * @param[in] ns        N(S) sequence number
 * @param[in] nr        N(R) sequence number
 * @param[in] pf        P/F bit value
 * @param[in] len       Payload length in bytes
 * @param[in] pending   Pending frame count (for window tracking)
 * @param[in] window    Window size (ks)
 * @param[in] flags     Optional flags (RETX, REJ, etc.)
 */
void iohdlc_log_iframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        uint32_t ns, uint32_t nr, bool pf, size_t len,
                        uint32_t pending, uint32_t window, uint8_t flags);

/**
 * @brief   Log an S-frame (Supervisory frame).
 *
 * @param[in] dir       Direction (TX or RX)
 * @param[in] saddr     Station address (local)
 * @param[in] addr      Address field in frame
 * @param[in] fun       S-frame function (RR, RNR, REJ, SREJ)
 * @param[in] nr        N(R) sequence number
 * @param[in] pf        P/F bit value
 * @param[in] flags     Optional flags (BUSY, etc.)
 */
void iohdlc_log_sframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        iohdlc_log_sfun_t fun, uint32_t nr, bool pf, uint8_t flags);

/**
 * @brief   Log a U-frame (Unnumbered frame).
 *
 * @param[in] dir       Direction (TX or RX)
 * @param[in] saddr     Station address (local)
 * @param[in] addr      Address field in frame
 * @param[in] fun       U-frame function (SNRM, UA, DISC, etc.)
 * @param[in] pf        P/F bit value
 */
void iohdlc_log_uframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        iohdlc_log_ufun_t fun, bool pf);

/**
 * @brief   Log a msg.
 *
 * @param[in] dir       Direction (TX or RX)
 * @param[in] msg       Message to log
 */
void iohdlc_log_msg(iohdlc_log_dir_t dir, uint8_t saddr, const char *msg, ...);

/*===========================================================================*/
/* Logging macros (compile-time conditional)                                 */
/*===========================================================================*/

#define IOHDLC_LOG_IFRAME(dir, saddr, addr, ns, nr, pf, len, pending, window, flags) \
  iohdlc_log_iframe(dir, saddr, addr, ns, nr, pf, len, pending, window, flags)

#define IOHDLC_LOG_SFRAME(dir, saddr, addr, fun, nr, pf, flags) \
  iohdlc_log_sframe(dir, saddr, addr, fun, nr, pf, flags)

#define IOHDLC_LOG_UFRAME(dir, saddr, addr, fun, pf) \
  iohdlc_log_uframe(dir, saddr, addr, fun, pf)

#define IOHDLC_LOG_WARN(dir, saddr, msg, ...) \
  iohdlc_log_msg(dir, saddr, msg, ##__VA_ARGS__)

#else /* IOHDLC_LOG_LEVEL == OFF */

/* No-op macros when logging disabled (zero overhead) */
#define IOHDLC_LOG_IFRAME(...) ((void)0)
#define IOHDLC_LOG_SFRAME(...) ((void)0)
#define IOHDLC_LOG_UFRAME(...) ((void)0)
#define IOHDLC_LOG_MSG(...) ((void)0)
#define IOHDLC_LOG_WARN(...) ((void)0)

#endif /* IOHDLC_LOG_LEVEL > OFF */

#endif /* IOHDLC_LOG_H_ */

/** @} */
