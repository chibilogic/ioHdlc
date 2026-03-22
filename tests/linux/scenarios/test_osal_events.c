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
 * @file    test_osal_events.c
 * @brief   Test event system OSAL implementation.
 *
 * @details Validates:
 *          - Event source initialization
 *          - Listener registration/unregistration
 *          - Event broadcasting
 *          - Event waiting with timeout
 *          - Thread-local storage
 *          - Multiple listeners
 *          - Event flags filtering
 */

#include "../../common/test_helpers.h"
#include "ioHdlcosal.h"
#include <pthread.h>
#include <time.h>

/*===========================================================================*/
/* Test: Event Source Init                                                   */
/*===========================================================================*/

static bool test_event_source_init(void) {
  iohdlc_event_source_t es;

  iohdlc_evt_init(&es);

  /* No direct way to verify, but should not crash */
  TEST_ASSERT(true, "Event source init should succeed");

  return 0;
}

/*===========================================================================*/
/* Test: Register and Broadcast Single Listener                              */
/*===========================================================================*/

static bool test_event_register_broadcast(void) {
  iohdlc_event_source_t es;
  iohdlc_event_listener_t listener;
  eventflags_t flags;
  eventmask_t mask;

  iohdlc_evt_init(&es);

  /* Register listener for specific flags */
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0x0F);

  /* Broadcast matching flag */
  iohdlc_evt_broadcast_flags(&es, 0x01);

  /* Wait with immediate timeout should return the mask */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT(mask != 0, "Should receive broadcasted event");
  
  /* Get and clear flags */
  flags = iohdlc_evt_get_and_clear_flags(&listener);
  TEST_ASSERT_EQUAL(0x01, flags, "Should receive broadcasted flag");

  /* Second wait should timeout (event consumed) */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT_EQUAL(0, mask, "Second wait should timeout");

  iohdlc_evt_unregister(&es, &listener);

  return 0;
}

/*===========================================================================*/
/* Test: Multiple Flags                                                      */
/*===========================================================================*/

static bool test_event_multiple_flags(void) {
  iohdlc_event_source_t es;
  iohdlc_event_listener_t listener;
  eventflags_t flags;
  eventmask_t mask;

  iohdlc_evt_init(&es);
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0xFF);

  /* Broadcast multiple flags */
  iohdlc_evt_broadcast_flags(&es, 0x05);  /* bits 0 and 2 */

  /* Wait and get flags */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT(mask != 0, "Should receive event");
  flags = iohdlc_evt_get_and_clear_flags(&listener);
  TEST_ASSERT_EQUAL(0x05, flags, "Should receive all broadcasted flags");

  /* Broadcast new flags */
  iohdlc_evt_broadcast_flags(&es, 0x03);  /* bits 0 and 1 */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT(mask != 0, "Should receive new event");
  flags = iohdlc_evt_get_and_clear_flags(&listener);
  TEST_ASSERT_EQUAL(0x03, flags, "Should receive new flags");

  iohdlc_evt_unregister(&es, &listener);

  return 0;
}

/*===========================================================================*/
/* Test: Event Filtering by Registered Flags                                 */
/*===========================================================================*/

static bool test_event_filtering(void) {
  iohdlc_event_source_t es;
  iohdlc_event_listener_t listener;
  eventflags_t flags;
  eventmask_t mask;

  iohdlc_evt_init(&es);

  /* Register listener only for flags 0x0F (lower nibble) */
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0x0F);

  /* Broadcast flag outside registered mask */
  iohdlc_evt_broadcast_flags(&es, 0x10);  /* bit 4, not in 0x0F */

  /* Wait should timeout (flag not in our mask) */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 10);
  TEST_ASSERT_EQUAL(0, mask, "Should not receive flag outside registered mask");

  /* Broadcast flag inside registered mask */
  iohdlc_evt_broadcast_flags(&es, 0x04);  /* bit 2, in 0x0F */

  /* Wait should succeed */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT(mask != 0, "Should receive event");
  flags = iohdlc_evt_get_and_clear_flags(&listener);
  TEST_ASSERT_EQUAL(0x04, flags, "Should receive flag inside registered mask");

  iohdlc_evt_unregister(&es, &listener);

  return 0;
}

/*===========================================================================*/
/* Test: Timeout Behavior                                                    */
/*===========================================================================*/

static bool test_event_timeout(void) {
  iohdlc_event_source_t es;
  iohdlc_event_listener_t listener;
  eventmask_t mask;
  struct timespec start, end;
  long elapsed_ms;

  iohdlc_evt_init(&es);
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0xFF);

  /* Test 1: Immediate timeout (no event) */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT_EQUAL(0, mask, "Immediate wait with no event should timeout");

  /* Test 2: Timed timeout (100ms) */
  clock_gettime(CLOCK_MONOTONIC, &start);
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 100);
  clock_gettime(CLOCK_MONOTONIC, &end);

  elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
               (end.tv_nsec - start.tv_nsec) / 1000000;

  TEST_ASSERT_EQUAL(0, mask, "Timed wait should timeout");
  TEST_ASSERT_RANGE(80, 150, elapsed_ms, "Timeout should be approximately 100ms");

  iohdlc_evt_unregister(&es, &listener);

  return 0;
}

/*===========================================================================*/
/* Test: Thread-Local Storage (Different Threads)                            */
/*===========================================================================*/

typedef struct {
  iohdlc_event_source_t *es;
  iohdlc_event_listener_t listener;
  int thread_id;
  eventflags_t received_flags;
  bool wait_succeeded;
} thread_event_arg_t;

static void* thread_event_waiter(void *arg) {
  thread_event_arg_t *targ = (thread_event_arg_t *)arg;
  eventmask_t mask;

  /* Register listener in THIS thread context */
  iohdlc_evt_register(targ->es, &targ->listener, EVENT_MASK(0), 0xFF);

  /* Wait for event (will be broadcasted by main thread) */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 500);
  if (mask != 0) {
    targ->received_flags = iohdlc_evt_get_and_clear_flags(&targ->listener);
    targ->wait_succeeded = true;
  }

  /* Unregister before exit */
  iohdlc_evt_unregister(targ->es, &targ->listener);

  return NULL;
}

static bool test_event_thread_local(void) {
  iohdlc_event_source_t es;
  pthread_t thread;
  thread_event_arg_t arg;

  iohdlc_evt_init(&es);

  /* Setup thread */
  arg.es = &es;
  arg.thread_id = 1;
  arg.received_flags = 0;
  arg.wait_succeeded = false;

  pthread_create(&thread, NULL, thread_event_waiter, &arg);

  /* Let thread start waiting */
  ioHdlc_sleep_ms(50);

  /* Broadcast event from main thread */
  iohdlc_evt_broadcast_flags(&es, 0x42);

  pthread_join(thread, NULL);

  TEST_ASSERT(arg.wait_succeeded, "Thread should have received event");
  TEST_ASSERT_EQUAL(0x42, arg.received_flags, "Thread should receive correct flags");

  return 0;
}

/*===========================================================================*/
/* Test: Multiple Listeners on Same Event Source                             */
/*===========================================================================*/

#define NUM_LISTENERS 3

static void* thread_multi_listener(void *arg) {
  thread_event_arg_t *targ = (thread_event_arg_t *)arg;
  eventmask_t mask;

  iohdlc_evt_register(targ->es, &targ->listener, EVENT_MASK(0), 0xFF);

  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 500);
  if (mask != 0) {
    targ->received_flags = iohdlc_evt_get_and_clear_flags(&targ->listener);
    targ->wait_succeeded = true;
  }

  iohdlc_evt_unregister(targ->es, &targ->listener);

  return NULL;
}

static bool test_event_multiple_listeners(void) {
  iohdlc_event_source_t es;
  pthread_t threads[NUM_LISTENERS];
  thread_event_arg_t args[NUM_LISTENERS];
  int i;

  iohdlc_evt_init(&es);

  /* Create multiple threads, each with its own listener */
  for (i = 0; i < NUM_LISTENERS; i++) {
    args[i].es = &es;
    args[i].thread_id = i;
    args[i].received_flags = 0;
    args[i].wait_succeeded = false;
    pthread_create(&threads[i], NULL, thread_multi_listener, &args[i]);
  }

  /* Let all threads register and start waiting */
  ioHdlc_sleep_ms(100);

  /* Broadcast event - ALL listeners should receive it */
  iohdlc_evt_broadcast_flags(&es, 0xAB);

  /* Join all threads */
  for (i = 0; i < NUM_LISTENERS; i++) {
    pthread_join(threads[i], NULL);
  }

  /* Verify all threads received the event */
  for (i = 0; i < NUM_LISTENERS; i++) {
    TEST_ASSERT(args[i].wait_succeeded, "All listeners should receive broadcast");
    TEST_ASSERT_EQUAL(0xAB, args[i].received_flags, "All listeners should receive correct flags");
  }

  return 0;
}

/*===========================================================================*/
/* Test: Unregister Before Broadcast                                         */
/*===========================================================================*/

static bool test_event_unregister(void) {
  iohdlc_event_source_t es;
  iohdlc_event_listener_t listener;
  eventmask_t mask;

  iohdlc_evt_init(&es);
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0xFF);

  /* Unregister immediately */
  iohdlc_evt_unregister(&es, &listener);

  /* Broadcast event */
  iohdlc_evt_broadcast_flags(&es, 0x55);

  /* Register again and check - should NOT see old event */
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0xFF);
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 10);
  TEST_ASSERT_EQUAL(0, mask, "Should not receive events after unregister");

  iohdlc_evt_unregister(&es, &listener);

  return 0;
}

/*===========================================================================*/
/* Test: Event Accumulation                                                  */
/*===========================================================================*/

static bool test_event_accumulation(void) {
  iohdlc_event_source_t es;
  iohdlc_event_listener_t listener;
  eventflags_t flags;
  eventmask_t mask;

  iohdlc_evt_init(&es);
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0xFF);

  /* Broadcast multiple different flags rapidly */
  iohdlc_evt_broadcast_flags(&es, 0x01);
  iohdlc_evt_broadcast_flags(&es, 0x02);
  iohdlc_evt_broadcast_flags(&es, 0x04);

  /* Wait should return event */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT(mask != 0, "Should receive accumulated events");
  flags = iohdlc_evt_get_and_clear_flags(&listener);
  TEST_ASSERT_EQUAL(0x07, flags, "Events should accumulate (OR)");

  /* Second wait should timeout (all flags consumed) */
  mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 0);
  TEST_ASSERT_EQUAL(0, mask, "Second wait should timeout after consumption");

  iohdlc_evt_unregister(&es, &listener);

  return 0;
}

/*===========================================================================*/
/* Test: Stress - Rapid Broadcast/Wait                                       */
/*===========================================================================*/

#define STRESS_ITERATIONS 200

static bool test_event_stress(void) {
  iohdlc_event_source_t es;
  iohdlc_event_listener_t listener;
  eventflags_t flags;
  eventmask_t mask;
  int i;

  iohdlc_evt_init(&es);
  iohdlc_evt_register(&es, &listener, EVENT_MASK(0), 0xFF);

  for (i = 0; i < STRESS_ITERATIONS; i++) {
    eventflags_t expected = (i & 0xFF) | 0x01;  /* Ensure at least one bit set */
    
    iohdlc_evt_broadcast_flags(&es, expected);
    mask = iohdlc_evt_wait_any_timeout(EVENT_MASK(0), 100);
    TEST_ASSERT(mask != 0, "Stress test: should receive event");
    flags = iohdlc_evt_get_and_clear_flags(&listener);
    TEST_ASSERT_EQUAL(expected, flags, "Stress test: flags should match");
  }

  iohdlc_evt_unregister(&es, &listener);

  return 0;
}

/*===========================================================================*/
/* Test Suite Runner                                                          */
/*===========================================================================*/

int main(void) {
  TEST_SUITE_START("OSAL Event System Tests");

  TEST_RUN(test_event_source_init, "Event source initialization");
  TEST_RUN(test_event_register_broadcast, "Register and broadcast");
  TEST_RUN(test_event_multiple_flags, "Multiple flags");
  TEST_RUN(test_event_filtering, "Event filtering by mask");
  TEST_RUN(test_event_timeout, "Timeout behavior");
  TEST_RUN(test_event_thread_local, "Thread-local storage");
  TEST_RUN(test_event_multiple_listeners, "Multiple listeners");
  TEST_RUN(test_event_unregister, "Unregister behavior");
  TEST_RUN(test_event_accumulation, "Event accumulation");
  TEST_RUN(test_event_stress, "Stress test");

  TEST_SUITE_END();
  return 0;
}
