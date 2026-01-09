/*
 * OS Abstraction Layer for ChibiOS.
 */
#ifndef IOHDLCOSAL_H_
#define IOHDLCOSAL_H_

#include "ch.h"
#include "hal.h"
#include "ioHdlctypes.h"

#define iohdlc_event_source_t event_source_t

typedef event_listener_t iohdlc_event_listener_t;

/**
 * @brief Virtual timer wrapper with expiry state tracking.
 * @note  ChibiOS virtual_timer_t doesn't distinguish between "never started"
 *        and "expired". This wrapper adds a flag to track expiry state.
 */
typedef struct {
  virtual_timer_t vt;       /* ChibiOS virtual timer. */
  bool expired;             /* True if timer expired (cleared on start/restart/stop). */
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

static inline msg_t iohdlc_sem_wait_timeout(iohdlc_sem_t *sp, uint32_t ms) {
  return chSemWaitTimeout(sp, TIME_MS2I(ms));
}

static inline bool iohdlc_sem_wait_ok(iohdlc_sem_t *sp, uint32_t ms) {
  return chSemWaitTimeout(sp, TIME_MS2I(ms)) == MSG_OK;
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

static inline msg_t iohdlc_bsem_wait_timeout(iohdlc_binary_semaphore_t *bsp, sysinterval_t timeout) {
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
 * @brief   Convert milliseconds to system ticks (ChibiOS TIME_MS2I wrapper).
 */
#define IOHDLC_TIME_MS2I(ms) TIME_MS2I(ms)

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

/**
 * @brief   Initialize virtual timer.
 * @note    On ChibiOS, virtual timers are statically initialized,
 *          so this is a no-op. Just clear the expired flag.
 */
static inline void iohdlc_vt_init(iohdlc_virtual_timer_t *vtp) {
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
                                                  sysinterval_t timeout) {
  (void)mtxp;  /* ChibiOS chCondWaitTimeout uses implicit mutex */
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

/* Event source/listener wrappers */
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

static inline void iohdlc_sys_lock_isr(void) { chSysLockFromISR(); }
static inline void iohdlc_sys_unlock_isr(void) { chSysUnlockFromISR(); }
static inline void iohdlc_sys_lock(void) { chSysLock(); }
static inline void iohdlc_sys_unlock(void) { chSysUnlock(); }
static inline void iohdlc_thread_yield(void) { chThdYield(); }

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
 * @note    ChibiOS: outputs to configured stream (iohdlc_log_stream).
 *          Must be set before logging is used.
 */
extern struct base_sequential_stream *iohdlc_osal_log_stream;
#define IOHDLC_OSAL_PRINTF(fmt, ...) chprintf(iohdlc_osal_log_stream, fmt, ##__VA_ARGS__)

#endif /* IOHDLCOSAL_H_ */
