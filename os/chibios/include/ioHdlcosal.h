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
 * @file    ioHdlcosal.h
 * @brief   OS Abstraction Layer for ChibiOS.
 *
 * @details Provides OSAL types and macros mapping to ChibiOS primitives.
 */

#ifndef IOHDLCOSAL_H_
#define IOHDLCOSAL_H_

#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include "ioHdlctypes.h"
#include <errno.h>

/**
 * @brief   Signed size type (not standard in ChibiOS/newlib-nano).
 */
typedef int ssize_t;

/**
 * @brief   Thread-local errno access.
 * @details ChibiOS uses standard errno (thread-local if newlib configured with reentrant).
 *          This macro provides consistent interface across platforms.
 * @note    Ensure newlib is configured with --enable-newlib-reent-thread-local for TLS errno.
 */
#define iohdlc_errno errno

/**
 * @brief   Platform-independent snprintf.
 * @details Maps to ChibiOS chsnprintf for consistent formatted printing.
 */
#define iohdlc_snprintf chsnprintf

#define iohdlc_event_source_t event_source_t

typedef event_listener_t iohdlc_event_listener_t;

/**
 * @brief Extends virtual timer with expiry state tracking and event flag.
 * @note  This object adds a flag to track expiry state.
 *        It adds also the event flag to broadcast.
 */
typedef struct {
  virtual_timer_t vt;         /* ChibiOS virtual timer. */
  uint32_t  evt_flag;         /* Event flag to broadcast when timer expires. */
  iohdlc_event_source_t *esp; /* Event source the evt_flag is emitted from. */
  volatile bool expired;      /* True if timer expired (cleared on
                                 start/restart/stop). Volatile: modified from
                                 ISR context, read from thread. */
} iohdlc_virtual_timer_t;

typedef semaphore_t iohdlc_sem_t;
typedef binary_semaphore_t iohdlc_binary_semaphore_t;
typedef memory_pool_t iohdlc_memory_pool_t;

/* Mutex type */
struct iohdlc_mutex {
  mutex_t mtx;
};

/* Mutex operations */
static inline void iohdlc_mutex_init(iohdlc_mutex_t *mp) {
  chMtxObjectInit(&mp->mtx);
}

static inline void iohdlc_mutex_lock(iohdlc_mutex_t *mp) {
  chMtxLock(&mp->mtx);
}

static inline void iohdlc_mutex_unlock(iohdlc_mutex_t *mp) {
  chMtxUnlock(&mp->mtx);
}

static inline void iohdlc_sem_init(iohdlc_sem_t *sp, cnt_t n) {
  chSemObjectInit(sp, n);
}

static inline msg_t iohdlc_sem_wait_timeout(iohdlc_sem_t *sp, uint32_t timeout_ms) {
  sysinterval_t timeout = (timeout_ms == IOHDLC_WAIT_FOREVER) ?
                          TIME_INFINITE : TIME_MS2I(timeout_ms);
  return chSemWaitTimeout(sp, timeout);
}

static inline void iohdlc_sem_signal_i(iohdlc_sem_t *sp) {
  chSemSignalI(sp);
}

static inline void iohdlc_sem_signal(iohdlc_sem_t *sp) {
  chSemSignal(sp);
}

static inline void iohdlc_bsem_init(iohdlc_binary_semaphore_t *bsp, bool taken) {
  chBSemObjectInit(bsp, taken);
}

static inline msg_t iohdlc_bsem_wait_timeout(iohdlc_binary_semaphore_t *bsp, uint32_t timeout_ms) {
  sysinterval_t timeout = (timeout_ms == IOHDLC_WAIT_FOREVER) ?
                          TIME_INFINITE : TIME_MS2I(timeout_ms);
  return chBSemWaitTimeout(bsp, timeout);
}

static inline msg_t iohdlc_bsem_wait_timeout_ms(iohdlc_binary_semaphore_t *bsp, uint32_t timeout_ms) {
  sysinterval_t timeout = (timeout_ms == IOHDLC_WAIT_FOREVER) ? 
                          TIME_INFINITE : TIME_MS2I(timeout_ms);
  return chBSemWaitTimeout(bsp, timeout);
}

static inline void iohdlc_bsem_signal(iohdlc_binary_semaphore_t *bsp) {
  chBSemSignal(bsp);
}

static inline void iohdlc_bsem_signal_i(iohdlc_binary_semaphore_t *bsp) {
  chBSemSignalI(bsp);
}

/*===========================================================================*/
/* Time Conversion                                                           */
/*===========================================================================*/

/**
 * @brief   Infinite timeout constant.
 */
#define IOHDLC_WAIT_FOREVER  0xFFFFFFFFU

/**
 * @brief   Get current time in milliseconds.
 */
static inline uint32_t iohdlc_time_now_ms(void) {
  return TIME_I2MS(chVTGetSystemTimeX());
}

/*===========================================================================*/
/* Virtual Timer                                                             */
/*===========================================================================*/

typedef void (*iohdlc_vt_callback_t)(iohdlc_virtual_timer_t *vtp, void *par);

/**
 * @brief   Initialize virtual timer.
 * @note    On ChibiOS, virtual timers are statically initialized,
 *          so this is a no-op. Just clear the expired flag.
 */
static inline void iohdlc_vt_init(iohdlc_virtual_timer_t *vtp,
                      iohdlc_event_source_t *esp, uint32_t evt_flag) {
  vtp->expired = false;
  vtp->esp = esp;
  vtp->evt_flag = evt_flag;
}

/**
 * @brief   Check if virtual timer is armed (running).
 * @note    ChibiOS native API: chVTIsArmed().
 *
 * @param[in] vtp       Virtual timer pointer
 * @return              true if timer is armed, false otherwise
 */
static inline bool iohdlc_vt_is_armed(iohdlc_virtual_timer_t *vtp) {
  return chVTIsArmed(&vtp->vt);
}

static inline void iohdlc_vt_set(iohdlc_virtual_timer_t *vtp,
                    uint32_t delay_ms, iohdlc_vt_callback_t callback,
                    void *par) {
  chVTSet(&vtp->vt, TIME_MS2I(delay_ms), (vtfunc_t)callback, par);
}

static inline void iohdlc_vt_reset(iohdlc_virtual_timer_t *vtp) {
  chVTReset(&vtp->vt);
  vtp->expired = false;
}

/*===========================================================================*/
/* Condition Variable (maps to ChibiOS condition_variable_t)                 */
/*===========================================================================*/

typedef condition_variable_t iohdlc_condvar_t;

/**
 * @brief   Initialize condition variable.
 */
static inline void iohdlc_condvar_init(iohdlc_condvar_t *cvp) {
  chCondObjectInit(cvp);
}

/**
 * @brief   Destroy condition variable (no-op in ChibiOS).
 */
static inline void iohdlc_condvar_destroy(iohdlc_condvar_t *cvp) {
  (void)cvp;  /* ChibiOS has no destroy operation */
}

/**
 * @brief   Wait on condition variable (infinite timeout).
 * @pre     Caller must hold the associated mutex locked.
 * @post    Mutex is re-acquired before return.
 *
 * @param[in] cvp       Condition variable
 * @param[in] mtxp      Associated mutex (must be locked by caller)
 * @return              MSG_OK (always)
 */
static inline msg_t iohdlc_condvar_wait(iohdlc_condvar_t *cvp, iohdlc_mutex_t *mtxp) {
  (void)mtxp;  /* ChibiOS chCondWait uses implicit mutex from priority */
  return chCondWait(cvp);
}

/**
 * @brief   Wait on condition variable with timeout.
 * @pre     Caller must hold the associated mutex locked.
 * @post    Mutex is re-acquired before return.
 *
 * @param[in] cvp       Condition variable
 * @param[in] mtxp      Associated mutex (must be locked by caller)
 * @param[in] timeout   Timeout in system ticks
 * @return              MSG_OK on success, MSG_TIMEOUT on timeout
 */
static inline msg_t iohdlc_condvar_wait_timeout(iohdlc_condvar_t *cvp, 
                                                  iohdlc_mutex_t *mtxp,
                                                  uint32_t timeout_ms) {
  (void)mtxp;  /* ChibiOS chCondWaitTimeout uses implicit mutex */
  sysinterval_t timeout = (timeout_ms == IOHDLC_WAIT_FOREVER) ?
                          TIME_INFINITE : TIME_MS2I(timeout_ms);
  return chCondWaitTimeout(cvp, timeout);
}

/**
 * @brief   Signal condition variable (wake one waiting thread).
 *
 * @param[in] cvp       Condition variable
 */
static inline void iohdlc_condvar_signal(iohdlc_condvar_t *cvp) {
  chCondSignal(cvp);
}

/**
 * @brief   Broadcast condition variable (wake all waiting threads).
 *
 * @param[in] cvp       Condition variable
 */
static inline void iohdlc_condvar_broadcast(iohdlc_condvar_t *cvp) {
  chCondBroadcast(cvp);
}

static inline void iohdlc_evt_init(iohdlc_event_source_t *esp) {
  chEvtObjectInit(esp);
}

static inline void iohdlc_evt_register(iohdlc_event_source_t *esp, 
                                       iohdlc_event_listener_t *elp, 
                                       eventmask_t events, 
                                       eventflags_t flags) {
  chEvtRegisterMaskWithFlags(esp, elp, events, flags);
}

static inline void iohdlc_evt_unregister(iohdlc_event_source_t *esp,
                                         iohdlc_event_listener_t *elp) {
  chEvtUnregister(esp, elp);
}

static inline eventmask_t iohdlc_evt_wait_any_timeout(eventmask_t events, 
                                                       uint32_t timeout_ms) {
  sysinterval_t timeout = (timeout_ms == IOHDLC_WAIT_FOREVER) ? 
                          TIME_INFINITE : TIME_MS2I(timeout_ms);
  return chEvtWaitAnyTimeout(events, timeout);
}

static inline eventflags_t iohdlc_evt_get_and_clear_flags(iohdlc_event_listener_t *elp) {
  return chEvtGetAndClearFlags(elp);
}

static inline void iohdlc_evt_broadcast_flags(iohdlc_event_source_t *esp,
                                    eventflags_t flags) {
  chEvtBroadcastFlags(esp, flags);
}

static inline void iohdlc_evt_broadcast_flags_isr(iohdlc_event_source_t *esp,
                                    eventflags_t flags) {
  chSysLockFromISR();
  chEvtBroadcastFlagsI(esp, flags);
  chSysUnlockFromISR();
}

static inline void iohdlc_sys_lock_isr(void) { chSysLockFromISR(); }
static inline void iohdlc_sys_unlock_isr(void) { chSysUnlockFromISR(); }
static inline void iohdlc_sys_lock(void) { chSysLock(); }
static inline void iohdlc_sys_unlock(void) { chSysUnlock(); }
static inline void iohdlc_thread_yield(void) { chThdYield(); }

/*===========================================================================*/
/* Raw Reception Queue Mutex (SwDriver specific)                            */
/*===========================================================================*/

/**
 * @brief   Raw queue mutex - ChibiOS uses scheduler lock instead.
 * @note    Mutex field is not declared (zero footprint), locks map to chSys*.
 */
#define IOHDLC_RAWQ_MUTEX_DECLARE(name)   /* nothing */
#define IOHDLC_RAWQ_MUTEX_INIT(m)         (void)0
#define IOHDLC_RAWQ_LOCK(m)               chSysLock()
#define IOHDLC_RAWQ_UNLOCK(m)             chSysUnlock()
#define IOHDLC_RAWQ_LOCK_ISR(m)           chSysLockFromISR()
#define IOHDLC_RAWQ_UNLOCK_ISR(m)         chSysUnlockFromISR()

/*
 * OS-agnostic assertion hook for ioHdlc core modules.
 * Maps to ChibiOS debug assert; in release builds this compiles away.
 */
#ifndef IOHDLC_ASSERT
#define IOHDLC_ASSERT(cond, msg) chDbgAssert((cond), (msg))
#endif

/* DMA-safe allocation API (OS-provided). Implemented in
 * os/chibios/ioHdlcosal.c using ChibiOS mem heaps.*/
#ifndef IOHDLC_DMA_ALIGN_DEFAULT
#define IOHDLC_DMA_ALIGN_DEFAULT 32u
#endif

void *iohdlc_dma_alloc(size_t size, size_t align);
void iohdlc_dma_free(void *p);

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
 * @note    ChibiOS: outputs to configured stream (ioHdlcSDx).
 *          Must be set before logging is used.
 */
extern BaseSequentialStream *ioHdlcSDx;
int locked_chvprintf(BaseSequentialStream *chp, const char *fmt, va_list ap);
int locked_chprintf(BaseSequentialStream *chp, const char *fmt, ...);

#define IOHDLC_OSAL_PRINTF(fmt, ...) locked_chprintf(ioHdlcSDx, fmt, ##__VA_ARGS__)
#define IOHDLC_OSAL_VPRINTF(fmt, args) locked_chvprintf(ioHdlcSDx, fmt, args)

/*===========================================================================*/
/* Timing Support (OS-abstracted)                                           */
/*===========================================================================*/

/**
 * @brief   Sleep for milliseconds (ChibiOS).
 */
static inline void ioHdlc_sleep_ms(uint32_t ms) {
  chThdSleepMilliseconds(ms);
}

/**
 * @brief   Sleep for microseconds (ChibiOS).
 */
static inline void ioHdlc_sleep_us(uint32_t us) {
  chThdSleepMicroseconds(us);
}

/*===========================================================================*/
/* Thread Abstraction                                                        */
/*===========================================================================*/

/**
 * @brief   Thread handle (opaque OS-specific implementation).
 */
typedef struct iohdlc_thread iohdlc_thread_t;

/**
 * @brief   Thread entry point signature.
 * @note    Portable across different platforms.
 *          Returns void* for compatibility.
 */
typedef void* (*iohdlc_thread_fn_t)(void* arg);

/**
 * @brief   Thread handle structure (ChibiOS implementation).
 */
struct iohdlc_thread {
  thread_t* handle;  /**< ChibiOS thread reference */
};

/**
 * @brief   Create and start a new thread.
 *
 * @param[in] name          Thread name (for debugging)
 * @param[in] stack_size    Stack size in bytes (0 = use default 2048)
 * @param[in] priority      Thread priority offset from NORMALPRIO (0 = NORMALPRIO)
 * @param[in] entry         Thread entry point function
 * @param[in] arg           Argument passed to entry point
 * @return                  Thread handle on success, NULL on failure
 */
static inline iohdlc_thread_t* iohdlc_thread_create(
    const char* name,
    size_t stack_size,
    int priority,
    iohdlc_thread_fn_t entry,
    void* arg) {
  
  /* Allocate thread handle from heap */
  iohdlc_thread_t* thread = (iohdlc_thread_t*)chHeapAlloc(NULL, sizeof(iohdlc_thread_t));
  if (thread == NULL) {
    return NULL;
  }
  
  /* Calculate working area size (includes stack + thread descriptor)
   * Default to 2048 bytes stack if not specified */
  size_t wsize = (stack_size > 0) ? 
                  THD_WORKING_AREA_SIZE(stack_size) : 
                  THD_WORKING_AREA_SIZE(2048);
  
  /* Calculate thread priority (NORMALPRIO + offset) */
  tprio_t prio = (tprio_t)(NORMALPRIO + priority);
  
  /* Suppress cast warning: iohdlc_thread_fn_t returns void* for Linux compatibility,
   * but ChibiOS tfunc_t returns void. The cast is safe because return value is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
  thread->handle = chThdCreateFromHeap(NULL, wsize, name, prio,
                                       (tfunc_t)entry, arg);
#pragma GCC diagnostic pop
  
  if (thread->handle == NULL) {
    chHeapFree(thread);
    return NULL;
  }
  
  return thread;
}

/**
 * @brief   Wait for thread termination and cleanup resources.
 * @note    Thread handle is freed after join completes.
 *          Thread stack is automatically freed by ChibiOS after chThdWait().
 *
 * @param[in] thread        Thread handle (freed after join)
 */
static inline void iohdlc_thread_join(iohdlc_thread_t* thread) {
  if (thread != NULL && thread->handle != NULL) {
    /* Wait for thread termination (also frees thread's stack) */
    chThdWait(thread->handle);
    
    /* Free our handle structure */
    chHeapFree(thread);
  }
}

/*===========================================================================*/
/* Memory allocation                                                         */
/*===========================================================================*/

#define IOHDLC_MALLOC(size) chHeapAlloc(NULL, size)
#define IOHDLC_FREE(obj)    chHeapFree(obj)

#endif /* IOHDLCOSAL_H_ */
