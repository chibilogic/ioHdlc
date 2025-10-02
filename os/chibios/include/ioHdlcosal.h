/*
 * OS Abstraction Layer for ChibiOS.
 */
#ifndef IOHDLCOSAL_H_
#define IOHDLCOSAL_H_

#include "ch.h"
#include "hal.h"

#define iohdlc_event_source_t event_source_t
#define iohdlc_virtual_timer_t virtual_timer_t

typedef semaphore_t iohdlc_sem_t;

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

static inline void iohdlc_sys_lock_isr(void) { chSysLockFromISR(); }
static inline void iohdlc_sys_unlock_isr(void) { chSysUnlockFromISR(); }
static inline void iohdlc_sys_lock(void) { chSysLock(); }
static inline void iohdlc_sys_unlock(void) { chSysUnlock(); }
static inline void iohdlc_thread_yield(void) { chThdYield(); }

#endif /* IOHDLCOSAL_H_ */

