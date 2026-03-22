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
 * @file    ioHdlc_log.h
 * @brief   HDLC frame logging facility.
 * @details Provides compile-time configurable logging for transmitted and
 *          received HDLC frames. Zero overhead when disabled.
 *
 *          This facility is observational only: it must not be used to encode
 *          protocol decisions or timing-sensitive behaviour. When enabled, the
 *          active backend must still tolerate the added formatting cost in the
 *          selected execution context.
 *
 * @addtogroup ioHdlc_log
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

const char* sfun_to_str(iohdlc_log_sfun_t fun);

/**
 * @brief   Runtime enable/disable flag.
 * @note    Can be modified at runtime to control logging dynamically.
 * @note    Integrations must provide the required synchronization if this flag
 *          is changed concurrently with active logging calls.
 */
extern bool iohdlc_log_enabled;

/** @ingroup ioHdlc_log */
void iohdlc_log_iframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        uint32_t ns, uint32_t nr, bool pf, size_t len,
                        uint32_t pending, uint32_t window, uint8_t flags);

/** @ingroup ioHdlc_log */
void iohdlc_log_sframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        iohdlc_log_sfun_t fun, uint32_t nr, bool pf,
                        uint32_t pending, uint8_t flags);

/** @ingroup ioHdlc_log */
void iohdlc_log_uframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        iohdlc_log_ufun_t fun, bool pf);

/** @ingroup ioHdlc_log */
void iohdlc_log_msg(iohdlc_log_dir_t dir, uint8_t saddr, const char *msg, ...);

/*===========================================================================*/
/* Logging macros (compile-time conditional)                                 */
/*===========================================================================*/

/** @brief Log an I-frame when logging is enabled. */
#define IOHDLC_LOG_IFRAME(dir, saddr, addr, ns, nr, pf, len, pending, window, flags) \
  iohdlc_log_iframe(dir, saddr, addr, ns, nr, pf, len, pending, window, flags)

/** @brief Log an S-frame when logging is enabled. */
#define IOHDLC_LOG_SFRAME(dir, saddr, addr, fun, nr, pf, pending, flags) \
  iohdlc_log_sframe(dir, saddr, addr, fun, nr, pf, pending, flags)

/** @brief Log a U-frame when logging is enabled. */
#define IOHDLC_LOG_UFRAME(dir, saddr, addr, fun, pf) \
  iohdlc_log_uframe(dir, saddr, addr, fun, pf)

/** @brief Log a warning-level message when logging is enabled. */
#define IOHDLC_LOG_WARN(dir, saddr, msg, ...) \
  iohdlc_log_msg(dir, saddr, msg, ##__VA_ARGS__)

/** @brief Log a generic message when logging is enabled. */
#define IOHDLC_LOG_MSG(dir, saddr, msg, ...) \
  iohdlc_log_msg(dir, saddr, msg, ##__VA_ARGS__)
  
#else /* IOHDLC_LOG_LEVEL == OFF */

/* No-op macros when logging disabled (zero overhead) */
/** @brief No-op I-frame logging macro when logging is disabled. */
#define IOHDLC_LOG_IFRAME(...) ((void)0)
/** @brief No-op S-frame logging macro when logging is disabled. */
#define IOHDLC_LOG_SFRAME(...) ((void)0)
/** @brief No-op U-frame logging macro when logging is disabled. */
#define IOHDLC_LOG_UFRAME(...) ((void)0)
/** @brief No-op generic logging macro when logging is disabled. */
#define IOHDLC_LOG_MSG(...) ((void)0)
/** @brief No-op warning logging macro when logging is disabled. */
#define IOHDLC_LOG_WARN(...) ((void)0)

#endif /* IOHDLC_LOG_LEVEL > OFF */

#endif /* IOHDLC_LOG_H_ */

/** @} */
