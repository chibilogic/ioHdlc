/**
 * @file    ioHdlcosal.h
 * @brief   OSAL abstraction for Linux/POSIX.
 *
 * @addtogroup IOHDLC_OSAL
 * @{
 */

#ifndef IOHDLCOSAL_H
#define IOHDLCOSAL_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sched.h>
#include <signal.h>

/*===========================================================================*/
/* Event Types                                                               */
/*===========================================================================*/

/**
 * @brief   Event mask and flags types.
 */
typedef uint32_t eventmask_t;
typedef uint32_t eventflags_t;

/**
 * @brief   Virtual timer callback type.
 */
typedef void (*iohdlc_vt_callback_t)(void *vtp, void *par);

/**
 * @brief   Virtual timer structure (POSIX implementation).
 */
typedef struct {
  timer_t timer_id;             /**< POSIX timer ID */
  iohdlc_vt_callback_t callback;/**< Callback function */
  void *par;                    /**< User parameter */
  bool armed;                   /**< Timer is armed/running */
  bool expired;                 /**< Timer expired flag */
  pthread_mutex_t lock;         /**< Protect state */
  
  /* Timer kind (for event broadcasting in runner) */
  uint32_t kind;                /**< Timer kind (REPLY or I_REPLY) */
} iohdlc_virtual_timer_t;

/**
 * @brief   Thread event state (per-thread events like ChibiOS).
 */
typedef struct {
  pthread_mutex_t lock;
  pthread_cond_t cond;
  eventmask_t pending;  /**< Pending events */
} iohdlc_thread_events_t;

/**
 * @brief   Event listener structure.
 */
typedef struct iohdlc_event_listener {
  struct iohdlc_event_listener *next;  /**< Next listener in chain */
  pthread_t thread;                     /**< Listening thread (unused, kept for compatibility) */
  iohdlc_thread_events_t *thread_events; /**< Thread event state */
  eventmask_t events;                   /**< Event mask */
  eventflags_t flags;                   /**< Pending flags */
  eventflags_t wflags;                  /**< Wanted flags */
  pthread_mutex_t lock;                 /**< Protect flags */
} iohdlc_event_listener_t;

/**
 * @brief   Event source structure.
 */
typedef struct {
  iohdlc_event_listener_t *listeners;  /**< Listener chain */
  pthread_mutex_t lock;                 /**< Protect list */
} iohdlc_event_source_t;

/*===========================================================================*/
/* Time Conversion                                                           */
/*===========================================================================*/

/**
 * @brief   Event mask helper macro (compatible with ChibiOS).
 */
#define EVENT_MASK(n) ((eventmask_t)(1UL << (n)))

/**
 * @brief   Message type (return codes).
 */
typedef int32_t msg_t;

/**
 * @brief   Message return codes.
 */
#define MSG_OK      0
#define MSG_TIMEOUT -1
#define MSG_RESET   -2

/**
 * @brief   System time type (milliseconds).
 */
typedef uint32_t iohdlc_systime_t;

/**
 * @brief   Convert milliseconds to system time.
 */
#define IOHDLC_TIME_MS2I(ms) ((iohdlc_systime_t)(ms))

/**
 * @brief   Infinite timeout.
 */
#define IOHDLC_TIME_INFINITE ((iohdlc_systime_t)-1)

/**
 * @brief   Immediate timeout (non-blocking).
 */
#define IOHDLC_TIME_IMMEDIATE ((iohdlc_systime_t)0)

/*===========================================================================*/
/* Binary Semaphore                                                          */
/*===========================================================================*/

/**
 * @brief   Counting semaphore (POSIX).
 */
typedef struct {
  sem_t sem;
} iohdlc_sem_t;

/**
 * @brief   Binary semaphore structure (POSIX).
 * @details Uses mutex+condvar+flag to ensure true binary semantics.
 *          Flag is either true or false, never accumulates.
 */
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool signaled;
} iohdlc_bsem_t;

/* Alias for compatibility */
typedef iohdlc_bsem_t iohdlc_binary_semaphore_t;

/*===========================================================================*/
/* Mutex (forward declaration needed by condvar)                            */
/*===========================================================================*/

/**
 * @brief   Mutex structure (POSIX).
 */
struct iohdlc_mutex {
  pthread_mutex_t mtx;
};

typedef struct iohdlc_mutex iohdlc_mutex_t;

/**
 * @brief   Initialize binary semaphore.
 */
void iohdlc_bsem_init(iohdlc_bsem_t *bsem, bool taken);

/**
 * @brief   Wait on binary semaphore with timeout.
 * @return  0 on success, -ETIMEDOUT on timeout.
 */
int iohdlc_bsem_wait_timeout(iohdlc_bsem_t *bsem, iohdlc_systime_t timeout);

/**
 * @brief   Wait on binary semaphore with timeout (milliseconds).
 * @return  MSG_OK on success, MSG_TIMEOUT on timeout.
 */
static inline msg_t iohdlc_bsem_wait_timeout_ms(iohdlc_bsem_t *bsem, uint32_t timeout_ms) {
  int result = iohdlc_bsem_wait_timeout(bsem, IOHDLC_TIME_MS2I(timeout_ms));
  return (result == 0) ? MSG_OK : MSG_TIMEOUT;
}

/**
 * @brief   Signal binary semaphore.
 * @details Sets signaled flag to true (never accumulates beyond 1).
 */
void iohdlc_bsem_signal(iohdlc_bsem_t *bsem);

/**
 * @brief   Reset binary semaphore.
 */
static inline void iohdlc_bsem_reset(iohdlc_bsem_t *bsem, bool taken) {
  pthread_mutex_lock(&bsem->mutex);
  bsem->signaled = !taken;
  pthread_mutex_unlock(&bsem->mutex);
}

/*===========================================================================*/
/* Condition Variable (aligned with ChibiOS chCond API)                      */
/*===========================================================================*/

/**
 * @brief   Condition variable structure (POSIX).
 * @details Compatible with ChibiOS condition_variable_t API.
 */
typedef struct {
  pthread_cond_t cond;
} iohdlc_condvar_t;

/* Alias for ChibiOS compatibility */
typedef iohdlc_condvar_t condition_variable_t;

/**
 * @brief   Initialize condition variable.
 * @note    Maps to chCondObjectInit() in ChibiOS.
 */
void iohdlc_condvar_init(iohdlc_condvar_t *cvp);

/**
 * @brief   Destroy condition variable.
 */
void iohdlc_condvar_destroy(iohdlc_condvar_t *cvp);

/**
 * @brief   Wait on condition variable (infinite timeout).
 * @pre     Caller must hold the associated mutex locked.
 * @post    Mutex is re-acquired before return.
 * @note    Maps to chCondWait() in ChibiOS.
 * 
 * @param[in] cvp       Condition variable
 * @param[in] mtxp      Associated mutex (must be locked by caller)
 * @return              MSG_OK (always, no timeout)
 */
msg_t iohdlc_condvar_wait(iohdlc_condvar_t *cvp, iohdlc_mutex_t *mtxp);

/**
 * @brief   Wait on condition variable with timeout.
 * @pre     Caller must hold the associated mutex locked.
 * @post    Mutex is re-acquired before return (even on timeout).
 * @note    Maps to chCondWaitTimeout() in ChibiOS.
 * 
 * @param[in] cvp       Condition variable
 * @param[in] mtxp      Associated mutex (must be locked by caller)
 * @param[in] timeout   Timeout in system ticks (use IOHDLC_TIME_MS2I for ms)
 * @return              MSG_OK on success, MSG_TIMEOUT on timeout
 */
msg_t iohdlc_condvar_wait_timeout(iohdlc_condvar_t *cvp, 
                                   iohdlc_mutex_t *mtxp,
                                   iohdlc_systime_t timeout);

/**
 * @brief   Signal condition variable (wake one waiting thread).
 * @note    Maps to chCondSignal() in ChibiOS.
 * 
 * @param[in] cvp       Condition variable
 */
void iohdlc_condvar_signal(iohdlc_condvar_t *cvp);

/**
 * @brief   Broadcast condition variable (wake all waiting threads).
 * @note    Maps to chCondBroadcast() in ChibiOS.
 * 
 * @param[in] cvp       Condition variable
 */
void iohdlc_condvar_broadcast(iohdlc_condvar_t *cvp);

/*===========================================================================*/
/* Counting Semaphore                                                        */
/*===========================================================================*/

/**
 * @brief   Initialize counting semaphore.
 */
static inline void iohdlc_sem_init(iohdlc_sem_t *sp, int32_t n) {
  sem_init(&sp->sem, 0, n);
}

/**
 * @brief   Wait on semaphore with timeout (bool return).
 */
static inline bool iohdlc_sem_wait_ok(iohdlc_sem_t *sp, uint32_t ms) {
  if (ms == IOHDLC_TIME_INFINITE) {
    return (sem_wait(&sp->sem) == 0);
  } else {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000L;
    }
    return (sem_timedwait(&sp->sem, &ts) == 0);
  }
}

/**
 * @brief   Signal semaphore (ISR-safe, but no-op distinction on Linux).
 */
static inline void iohdlc_sem_signal_i(iohdlc_sem_t *sp) {
  sem_post(&sp->sem);
}

/**
 * @brief   Signal semaphore (non-ISR context).
 */
static inline void iohdlc_sem_signal(iohdlc_sem_t *sp) {
  sem_post(&sp->sem);
}

/*===========================================================================*/
/* Memory Pool                                                               */
/*===========================================================================*/

/**
 * @brief   Memory pool type using free-list (Linux implementation).
 */
typedef struct {
  pthread_mutex_t lock;      /**< Mutex for thread-safety.                  */
  void *free_list;           /**< Head of free list.                        */
  size_t element_size;       /**< Size of each element.                     */
} iohdlc_memory_pool_t;

/*===========================================================================*/
/* Mutex Operations                                                          */
/*===========================================================================*/

/**
 * @brief   Initialize mutex.
 */
static inline void iohdlc_mutex_init(iohdlc_mutex_t *m) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&m->mtx, &attr);
  pthread_mutexattr_destroy(&attr);
}

/**
 * @brief   Lock mutex.
 */
#define iohdlc_mutex_lock(m) pthread_mutex_lock(&(m)->mtx)

/**
 * @brief   Unlock mutex.
 */
#define iohdlc_mutex_unlock(m) pthread_mutex_unlock(&(m)->mtx)

/*===========================================================================*/
/* Memory Allocation                                                         */
/*===========================================================================*/

/**
 * @brief   DMA alignment (no special alignment needed on Linux).
 */
#define IOHDLC_DMA_ALIGN_DEFAULT 1

/**
 * @brief   Allocate memory block.
 */
void* iohdlc_alloc(size_t size);

/**
 * @brief   Free memory block.
 */
void iohdlc_free(void* ptr);

/**
 * @brief   Allocate DMA-capable memory (same as regular alloc on Linux).
 */
static inline void* iohdlc_dma_alloc(size_t size, size_t align) {
  (void)align;  /* Alignment ignored on Linux */
  return iohdlc_alloc(size);
}

/**
 * @brief   Free DMA memory.
 */
static inline void iohdlc_dma_free(void* ptr) {
  iohdlc_free(ptr);
}

/*===========================================================================*/
/* System Time                                                               */
/*===========================================================================*/

/**
 * @brief   Get current system time in milliseconds.
 */
iohdlc_systime_t iohdlc_get_systime(void);

/**
 * @brief   Get current time in milliseconds (alias for compatibility).
 */
static inline uint32_t iohdlc_time_now_ms(void) {
  return (uint32_t)iohdlc_get_systime();
}

/*===========================================================================*/
/* System Lock/Unlock (No-op for POSIX userspace)                           */
/*===========================================================================*/

static inline void iohdlc_sys_lock_isr(void) { /* No-op */ }
static inline void iohdlc_sys_unlock_isr(void) { /* No-op */ }
static inline void iohdlc_sys_lock(void) { /* No-op */ }
static inline void iohdlc_sys_unlock(void) { /* No-op */ }
static inline void iohdlc_thread_yield(void) { sched_yield(); }

/*===========================================================================*/
/* Virtual Timer Operations                                                  */
/*===========================================================================*/

void iohdlc_vt_init(iohdlc_virtual_timer_t *vtp);
void iohdlc_vt_set(iohdlc_virtual_timer_t *vtp, uint32_t delay_ms,
                   iohdlc_vt_callback_t callback, void *par);
void iohdlc_vt_reset(iohdlc_virtual_timer_t *vtp);
bool iohdlc_vt_is_armed(iohdlc_virtual_timer_t *vtp);

/*===========================================================================*/
/* Event Source Operations                                                   */
/*===========================================================================*/

void iohdlc_evt_init(iohdlc_event_source_t *esp);
void iohdlc_evt_broadcast_flags(iohdlc_event_source_t *esp, eventflags_t flags);

/*===========================================================================*/
/* Event Listener Operations                                                 */
/*===========================================================================*/

void iohdlc_evt_register(iohdlc_event_source_t *esp,
                         iohdlc_event_listener_t *elp,
                         eventmask_t events,
                         eventflags_t wflags);
void iohdlc_evt_unregister(iohdlc_event_source_t *esp,
                           iohdlc_event_listener_t *elp);
eventflags_t iohdlc_evt_get_and_clear_flags(iohdlc_event_listener_t *elp);

/*===========================================================================*/
/* Thread Events (per-thread event model like ChibiOS)                      */
/*===========================================================================*/

/**
 * @brief   Wait for any event in mask with timeout.
 * @details Waits for events posted to current thread via event sources.
 *          This is the main event wait mechanism used by HDLC threads.
 * @param[in] events      Event mask to wait for
 * @param[in] timeout_ms  Timeout in milliseconds (IOHDLC_WAIT_FOREVER for infinite)
 * @return                Event mask that was signaled (0 on timeout)
 */
eventmask_t iohdlc_evt_wait_any_timeout(eventmask_t events, uint32_t timeout_ms);

/**
 * @brief   Signal events to current thread (from callbacks/ISR).
 * @param[in] events  Event mask to signal
 */
void iohdlc_evt_signal(eventmask_t events);

/*===========================================================================*/
/* Assertion                                                                 */
/*===========================================================================*/

#ifndef IOHDLC_ASSERT
#include <assert.h>
#define IOHDLC_ASSERT(cond, msg) assert((cond) && (msg))
#endif

/*===========================================================================*/
/* Logging Support (OS-abstracted)                                           */
/*===========================================================================*/

/**
 * @brief   Get current time in milliseconds (relative to first call).
 * @return  Milliseconds with fractional part as double.
 */
double iohdlc_osal_get_time_ms(void);

/**
 * @brief   Printf-like output for logging.
 * @note    Linux: outputs to stderr.
 */
#define IOHDLC_OSAL_PRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

#endif /* IOHDLCOSAL_H */

/** @} */
