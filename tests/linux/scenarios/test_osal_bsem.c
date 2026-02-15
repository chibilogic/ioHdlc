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
 * @file    test_osal_bsem.c
 * @brief   Test binary semaphore OSAL implementation.
 *
 * @details Validates:
 *          - Initialization (taken/not taken)
 *          - Signal/Wait basic operation
 *          - Timeout behavior (infinite, immediate, timed)
 *          - Binary semantics (no signal accumulation)
 *          - Thread safety
 *          - Reset operation
 */

#include "../../common/test_helpers.h"
#include "ioHdlcosal.h"
#include <pthread.h>
#include <time.h>

/*===========================================================================*/
/* Test: Basic Init and Signal                                               */
/*===========================================================================*/

static bool test_bsem_init_signal(void) {
  iohdlc_bsem_t bsem;
  int result;

  /* Test 1: Init as not taken (signaled), immediate wait should succeed */
  iohdlc_bsem_init(&bsem, false);
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(0, result, "Wait on initially signaled semaphore should succeed");

  /* Test 2: Init as taken (not signaled), immediate wait should timeout */
  iohdlc_bsem_init(&bsem, true);
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(-ETIMEDOUT, result, "Wait on initially taken semaphore should timeout");

  /* Test 3: Signal and wait */
  iohdlc_bsem_signal(&bsem);
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(0, result, "Wait after signal should succeed");

  /* Test 4: Second immediate wait should timeout (signal consumed) */
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(-ETIMEDOUT, result, "Second wait without signal should timeout");

  return 0;
}

/*===========================================================================*/
/* Test: Binary Semantics (No Accumulation)                                  */
/*===========================================================================*/

static bool test_bsem_no_accumulation(void) {
  iohdlc_bsem_t bsem;
  int result;

  /* Init as taken */
  iohdlc_bsem_init(&bsem, true);

  /* Signal multiple times */
  iohdlc_bsem_signal(&bsem);
  iohdlc_bsem_signal(&bsem);
  iohdlc_bsem_signal(&bsem);
  iohdlc_bsem_signal(&bsem);
  iohdlc_bsem_signal(&bsem);

  /* First wait should succeed */
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(0, result, "First wait after multiple signals should succeed");

  /* Second wait should timeout (signals don't accumulate) */
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(-ETIMEDOUT, result, "Second wait should timeout - signals must not accumulate");

  return 0;
}

/*===========================================================================*/
/* Test: Timeout Behavior                                                    */
/*===========================================================================*/

static bool test_bsem_timeout(void) {
  iohdlc_bsem_t bsem;
  int result;
  struct timespec start, end;
  long elapsed_ms;

  /* Init as taken */
  iohdlc_bsem_init(&bsem, true);

  /* Test 1: Immediate timeout */
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(-ETIMEDOUT, result, "Immediate wait should timeout");

  /* Test 2: Timed timeout (100ms) */
  clock_gettime(CLOCK_MONOTONIC, &start);
  result = iohdlc_bsem_wait_timeout(&bsem, 100);
  clock_gettime(CLOCK_MONOTONIC, &end);
  
  elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
               (end.tv_nsec - start.tv_nsec) / 1000000;
  
  TEST_ASSERT_EQUAL(-ETIMEDOUT, result, "Timed wait should timeout");
  TEST_ASSERT_RANGE(80, 150, elapsed_ms, "Timeout should be approximately 100ms");

  /* Test 3: Timed wait that succeeds (signal during wait tested in thread test) */
  iohdlc_bsem_signal(&bsem);
  clock_gettime(CLOCK_MONOTONIC, &start);
  result = iohdlc_bsem_wait_timeout(&bsem, 500);
  clock_gettime(CLOCK_MONOTONIC, &end);
  
  elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
               (end.tv_nsec - start.tv_nsec) / 1000000;
  
  TEST_ASSERT_EQUAL(0, result, "Timed wait with signal should succeed");
  TEST_ASSERT(elapsed_ms < 50, "Should return immediately when already signaled");

  return 0;
}

/*===========================================================================*/
/* Test: Thread Safety - Signal from Another Thread                          */
/*===========================================================================*/

typedef struct {
  iohdlc_bsem_t *bsem;
  int delay_ms;
  bool signal_sent;
} thread_signal_arg_t;

static void* thread_signal_delayed(void *arg) {
  thread_signal_arg_t *targ = (thread_signal_arg_t *)arg;
  
  ioHdlc_sleep_ms(targ->delay_ms);
  iohdlc_bsem_signal(targ->bsem);
  targ->signal_sent = true;
  
  return NULL;
}

static bool test_bsem_thread_signal(void) {
  iohdlc_bsem_t bsem;
  pthread_t thread;
  thread_signal_arg_t arg;
  int result;
  struct timespec start, end;
  long elapsed_ms;

  /* Init as taken */
  iohdlc_bsem_init(&bsem, true);

  /* Setup thread to signal after 100ms */
  arg.bsem = &bsem;
  arg.delay_ms = 100;
  arg.signal_sent = false;

  clock_gettime(CLOCK_MONOTONIC, &start);
  pthread_create(&thread, NULL, thread_signal_delayed, &arg);

  /* Wait with 500ms timeout (should succeed when thread signals) */
  result = iohdlc_bsem_wait_timeout(&bsem, 500);
  clock_gettime(CLOCK_MONOTONIC, &end);

  pthread_join(thread, NULL);

  elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
               (end.tv_nsec - start.tv_nsec) / 1000000;

  TEST_ASSERT_EQUAL(0, result, "Wait should succeed when signaled by thread");
  TEST_ASSERT(arg.signal_sent, "Signal should have been sent by thread");
  TEST_ASSERT_RANGE(80, 150, elapsed_ms, "Should wake up after ~100ms");

  return 0;
}

/*===========================================================================*/
/* Test: Multiple Threads Waiting                                            */
/*===========================================================================*/

#define NUM_WAITERS 5

typedef struct {
  iohdlc_bsem_t *bsem;
  int thread_id;
  bool wait_succeeded;
  int wait_order;
} thread_wait_arg_t;

static int wait_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* thread_waiter(void *arg) {
  thread_wait_arg_t *targ = (thread_wait_arg_t *)arg;
  int result;

  result = iohdlc_bsem_wait_timeout(targ->bsem, IOHDLC_TIME_INFINITE);
  
  if (result == 0) {
    pthread_mutex_lock(&counter_mutex);
    targ->wait_succeeded = true;
    targ->wait_order = wait_counter++;
    pthread_mutex_unlock(&counter_mutex);
  }

  return NULL;
}

static bool test_bsem_multiple_waiters(void) {
  iohdlc_bsem_t bsem;
  pthread_t threads[NUM_WAITERS];
  thread_wait_arg_t args[NUM_WAITERS];
  int i, success_count;

  /* Init as taken */
  iohdlc_bsem_init(&bsem, true);
  wait_counter = 0;

  /* Create multiple waiting threads */
  for (i = 0; i < NUM_WAITERS; i++) {
    args[i].bsem = &bsem;
    args[i].thread_id = i;
    args[i].wait_succeeded = false;
    args[i].wait_order = -1;
    pthread_create(&threads[i], NULL, thread_waiter, &args[i]);
  }

  /* Let threads start waiting */
  ioHdlc_sleep_ms(50);

  /* Signal multiple times - only ONE should succeed each time (binary semantics) */
  for (i = 0; i < NUM_WAITERS; i++) {
    iohdlc_bsem_signal(&bsem);
    ioHdlc_sleep_ms(20);  /* Let one thread consume the signal */
  }

  /* Join all threads */
  for (i = 0; i < NUM_WAITERS; i++) {
    pthread_join(threads[i], NULL);
  }

  /* Verify exactly NUM_WAITERS threads succeeded (one per signal) */
  success_count = 0;
  for (i = 0; i < NUM_WAITERS; i++) {
    if (args[i].wait_succeeded) {
      success_count++;
    }
  }

  TEST_ASSERT_EQUAL(NUM_WAITERS, success_count, 
                    "All waiters should succeed after N signals for N threads");

  return 0;
}

/*===========================================================================*/
/* Test: Reset Operation                                                     */
/*===========================================================================*/

static bool test_bsem_reset(void) {
  iohdlc_bsem_t bsem;
  int result;

  /* Init as taken, signal, then reset to taken */
  iohdlc_bsem_init(&bsem, true);
  iohdlc_bsem_signal(&bsem);
  iohdlc_bsem_reset(&bsem, true);

  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(-ETIMEDOUT, result, "After reset to taken, should timeout");

  /* Reset to not taken (signaled) */
  iohdlc_bsem_reset(&bsem, false);
  result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
  TEST_ASSERT_EQUAL(0, result, "After reset to not taken, should succeed");

  return 0;
}

/*===========================================================================*/
/* Test: Stress - Rapid Signal/Wait                                          */
/*===========================================================================*/

#define STRESS_ITERATIONS 1000

static bool test_bsem_stress(void) {
  iohdlc_bsem_t bsem;
  int result;
  int i;

  iohdlc_bsem_init(&bsem, true);

  for (i = 0; i < STRESS_ITERATIONS; i++) {
    iohdlc_bsem_signal(&bsem);
    result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
    TEST_ASSERT_EQUAL(0, result, "Stress test: signal/wait should succeed");

    /* Verify signal consumed */
    result = iohdlc_bsem_wait_timeout(&bsem, IOHDLC_TIME_IMMEDIATE);
    TEST_ASSERT_EQUAL(-ETIMEDOUT, result, "Stress test: second wait should timeout");
  }

  return 0;
}

/*===========================================================================*/
/* Test Suite Runner                                                          */
/*===========================================================================*/

int main(void) {
  TEST_SUITE_START("OSAL Binary Semaphore Tests");

  TEST_RUN(test_bsem_init_signal, "Basic init and signal");
  TEST_RUN(test_bsem_no_accumulation, "Binary semantics - no accumulation");
  TEST_RUN(test_bsem_timeout, "Timeout behavior");
  TEST_RUN(test_bsem_thread_signal, "Thread safety - signal from thread");
  TEST_RUN(test_bsem_multiple_waiters, "Multiple waiters");
  TEST_RUN(test_bsem_reset, "Reset operation");
  TEST_RUN(test_bsem_stress, "Stress test");

  TEST_SUITE_END();
  return 0;
}
