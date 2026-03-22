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
 * @file    os/linux/include/ioHdlc_events.h
 * @brief   Event and timer definitions for Linux/POSIX.
 * @details Provides the Linux/POSIX implementation types used by the OSAL
 *          event and timer layer.
 *
 *          This header is backend-specific: it defines how event sources,
 *          listeners, and virtual timers are represented on POSIX systems, not
 *          the protocol-level meaning of event bits.
 *
 * @addtogroup ioHdlc_osal
 * @{
 */

#ifndef IOHDLC_EVENTS_H
#define IOHDLC_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

/* Event flags */
#define IOHDLC_EVT_C_RPLYTMO  (1U << 0)  /**< Command reply timer expiry flag. */
#define IOHDLC_EVT_I_RPLYTMO  (1U << 1)  /**< Information-frame reply timer expiry flag. */

/* Event mask and flags types */
typedef uint32_t eventmask_t;   /**< Bitmask used to wait on a set of events. */
typedef uint32_t eventflags_t;  /**< Broadcast event flag storage type. */

/*===========================================================================*/
/* Virtual Timer                                                             */
/*===========================================================================*/

/**
 * @brief   Virtual timer callback type.
 * @param[in] vtp   Pointer to timer that expired
 * @param[in] par   User parameter passed at timer start
 */
typedef void (*iohdlc_vt_callback_t)(void *vtp, void *par);

/**
 * @brief   Virtual timer structure (POSIX implementation).
 * @details Stores the POSIX timer handle together with callback state and the
 *          synchronization needed to coordinate arming/reset operations.
 */
typedef struct {
  timer_t timerid;              /**< POSIX timer ID */
  iohdlc_vt_callback_t callback;/**< Callback function */
  void *par;                    /**< User parameter */
  bool active;                  /**< Timer is running */
  bool expired;                 /**< Timer expired flag */
  pthread_mutex_t lock;         /**< Protect state */
} iohdlc_virtual_timer_t;

/*===========================================================================*/
/* Event Source and Listener                                                */
/*===========================================================================*/

/**
 * @brief   Event listener structure.
 * @details Each listener is typically associated with one waiting thread.
 */
typedef struct iohdlc_event_listener {
  struct iohdlc_event_listener *next;  /**< Next listener in chain */
  pthread_t thread;                     /**< Listening thread */
  eventmask_t events;                   /**< Event mask */
  eventflags_t flags;                   /**< Pending flags */
  eventflags_t wflags;                  /**< Wanted flags */
  pthread_mutex_t lock;                 /**< Protect flags */
  pthread_cond_t cond;                  /**< Wait condition */
} iohdlc_event_listener_t;

/**
 * @brief   Event source structure.
 * @details Owns the listener chain that receives broadcast event flags.
 */
typedef struct {
  iohdlc_event_listener_t *listeners;  /**< Listener chain */
  pthread_mutex_t lock;                 /**< Protect list */
} iohdlc_event_source_t;

/*===========================================================================*/
/* Function Declarations                                                     */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual timer operations */
/**
 * @brief   Initialize a POSIX virtual timer object.
 */
void iohdlc_vt_init(iohdlc_virtual_timer_t *vtp);

/**
 * @brief   Arm or re-arm a POSIX virtual timer.
 */
void iohdlc_vt_set(iohdlc_virtual_timer_t *vtp, uint32_t delay_ms, 
                   iohdlc_vt_callback_t callback, void *par);

/**
 * @brief   Disarm a POSIX virtual timer.
 */
void iohdlc_vt_reset(iohdlc_virtual_timer_t *vtp);

/**
 * @brief   Check whether a POSIX virtual timer is currently armed.
 */
bool iohdlc_vt_is_armed(iohdlc_virtual_timer_t *vtp);

/* Event source operations */
/**
 * @brief   Initialize a POSIX event source.
 */
void iohdlc_evt_init(iohdlc_event_source_t *esp);

/**
 * @brief   Broadcast event flags to registered listeners.
 */
void iohdlc_evt_broadcast_flags(iohdlc_event_source_t *esp, eventflags_t flags);

/* Event listener operations */
/**
 * @brief   Register a listener on an event source.
 */
void iohdlc_evt_register(iohdlc_event_source_t *esp, 
                         iohdlc_event_listener_t *elp,
                         eventmask_t events, 
                         eventflags_t wflags);

/**
 * @brief   Unregister a listener from an event source.
 */
void iohdlc_evt_unregister(iohdlc_event_source_t *esp,
                           iohdlc_event_listener_t *elp);

/**
 * @brief   Wait for any of the requested events with timeout.
 */
eventmask_t iohdlc_evt_wait_any_timeout(eventmask_t events, uint32_t timeout_ms);

/**
 * @brief   Retrieve and clear pending flags for a listener.
 */
eventflags_t iohdlc_evt_get_and_clear_flags(iohdlc_event_listener_t *elp);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_EVENTS_H */

/** @} */
