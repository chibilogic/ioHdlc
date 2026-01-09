/**
 * @file    ioHdlc_events.h
 * @brief   Event and timer definitions for Linux/POSIX.
 */

#ifndef IOHDLC_EVENTS_H
#define IOHDLC_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

/* Event flags */
#define IOHDLC_EVT_C_RPLYTMO  (1U << 0)
#define IOHDLC_EVT_I_RPLYTMO  (1U << 1)

/* Event mask and flags types */
typedef uint32_t eventmask_t;
typedef uint32_t eventflags_t;

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
void iohdlc_vt_init(iohdlc_virtual_timer_t *vtp);
void iohdlc_vt_set(iohdlc_virtual_timer_t *vtp, uint32_t delay_ms, 
                   iohdlc_vt_callback_t callback, void *par);
void iohdlc_vt_reset(iohdlc_virtual_timer_t *vtp);
bool iohdlc_vt_is_armed(iohdlc_virtual_timer_t *vtp);

/* Event source operations */
void iohdlc_evt_init(iohdlc_event_source_t *esp);
void iohdlc_evt_broadcast_flags(iohdlc_event_source_t *esp, eventflags_t flags);

/* Event listener operations */
void iohdlc_evt_register(iohdlc_event_source_t *esp, 
                         iohdlc_event_listener_t *elp,
                         eventmask_t events, 
                         eventflags_t wflags);
void iohdlc_evt_unregister(iohdlc_event_source_t *esp,
                           iohdlc_event_listener_t *elp);
eventmask_t iohdlc_evt_wait_any_timeout(eventmask_t events, uint32_t timeout_ms);
eventflags_t iohdlc_evt_get_and_clear_flags(iohdlc_event_listener_t *elp);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_EVENTS_H */
