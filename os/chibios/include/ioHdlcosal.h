/*
 * OS Abstraction Layer for ChibiOS.
 */
#ifndef IOHDLCOSAL_H_
#define IOHDLCOSAL_H_

#include "ch.h"
#include "hal.h"

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

/* Event listener wrappers */
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

#endif /* IOHDLCOSAL_H_ */
