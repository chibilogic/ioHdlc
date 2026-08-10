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
 * @file    test_link_manager.c
 * @brief   Linux tests for the optional ioHdlc link manager utility.
 */

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ioHdlc_link_manager.h"

#define TEST_EVENT_MASK          EVENT_MASK(3)
#define TEST_RETRY_INTERVAL_MS   20U
#define TEST_WAIT_TIMEOUT_MS     500U
#define TEST_MAX_STATES          16U
#define TEST_MAX_ATTEMPTS        8U

typedef struct {
  iohdlc_station_t station;
  iohdlc_station_peer_t peer;
  iohdlc_link_manager_t manager;
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  iohdlc_peer_state_t states[TEST_MAX_STATES];
  struct timespec attempts[TEST_MAX_ATTEMPTS];
  size_t state_count;
  size_t attempt_count;
  bool ready;
  bool thread_started;
  int32_t run_result;
} link_manager_test_t;

#define TEST_CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "FAIL: %s\n", message); \
      result = 1; \
      goto cleanup; \
    } \
  } while (0)

/**
 * @brief   Add milliseconds to an absolute timespec.
 */
static void addMilliseconds(struct timespec *timep, uint32_t milliseconds) {
  timep->tv_sec += (time_t)(milliseconds / 1000U);
  timep->tv_nsec += (long)(milliseconds % 1000U) * 1000000L;
  if (timep->tv_nsec >= 1000000000L) {
    timep->tv_sec++;
    timep->tv_nsec -= 1000000000L;
  }
}

/**
 * @brief   Sleep without depending on ioHdlc thread timing helpers.
 */
static void sleepMilliseconds(uint32_t milliseconds) {
  struct timespec delay;

  delay.tv_sec = (time_t)(milliseconds / 1000U);
  delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
  (void)nanosleep(&delay, NULL);
}

/**
 * @brief   Initialize the minimal station and peer state used by the utility.
 */
static void testObjectInit(link_manager_test_t *ctxp) {
  memset(ctxp, 0, sizeof *ctxp);
  iohdlc_evt_init(&ctxp->station.app_es);
  iohdlc_mutex_init(&ctxp->peer.state_mutex);
  ctxp->peer.addr = 2U;
  (void)pthread_mutex_init(&ctxp->lock, NULL);
  (void)pthread_cond_init(&ctxp->cond, NULL);
}

/**
 * @brief   Release host-side test synchronization objects.
 */
static void testObjectDeinit(link_manager_test_t *ctxp) {
  (void)pthread_cond_destroy(&ctxp->cond);
  (void)pthread_mutex_destroy(&ctxp->lock);
}

/**
 * @brief   Set the synchronized peer state used by ioHdlcPeerGetState().
 */
static void setPeerState(link_manager_test_t *ctxp,
                         iohdlc_peer_state_t state) {
  uint8_t ss_state = 0U;

  if (state == IOHDLC_PEER_STATE_CONNECTED)
    ss_state = IOHDLC_SS_ST_CONN;
  else if (state == IOHDLC_PEER_STATE_ORDERLY_CLOSED)
    ss_state = IOHDLC_SS_TERM_ORDERLY;
  else if (state == IOHDLC_PEER_STATE_ABORTED)
    ss_state = IOHDLC_SS_TERM_ABORTED;

  iohdlc_mutex_lock(&ctxp->peer.state_mutex);
  ctxp->peer.ss_state = ss_state;
  iohdlc_mutex_unlock(&ctxp->peer.state_mutex);
}

/**
 * @brief   Record manager notifications from its execution thread.
 */
static void stateCallback(iohdlc_link_manager_t *managerp,
                          iohdlc_peer_state_t state, void *arg) {
  link_manager_test_t *ctxp = arg;

  (void)managerp;
  (void)pthread_mutex_lock(&ctxp->lock);
  if (ctxp->state_count < TEST_MAX_STATES)
    ctxp->states[ctxp->state_count++] = state;
  (void)pthread_cond_broadcast(&ctxp->cond);
  (void)pthread_mutex_unlock(&ctxp->lock);
}

/**
 * @brief   Return the test-controlled physical readiness state.
 */
static bool readyCallback(void *arg) {
  link_manager_test_t *ctxp = arg;
  bool ready;

  (void)pthread_mutex_lock(&ctxp->lock);
  ready = ctxp->ready;
  (void)pthread_mutex_unlock(&ctxp->lock);
  return ready;
}

/**
 * @brief   Fail the first connection attempt and complete subsequent ones.
 */
static int32_t connectCallback(iohdlc_station_t *stationp,
                               iohdlc_station_peer_t *peerp,
                               uint8_t mode, void *arg) {
  link_manager_test_t *ctxp = arg;
  size_t attempt;

  (void)stationp;
  (void)peerp;
  (void)mode;

  (void)pthread_mutex_lock(&ctxp->lock);
  attempt = ++ctxp->attempt_count;
  if (attempt <= TEST_MAX_ATTEMPTS)
    (void)clock_gettime(CLOCK_MONOTONIC, &ctxp->attempts[attempt - 1U]);
  (void)pthread_cond_broadcast(&ctxp->cond);
  (void)pthread_mutex_unlock(&ctxp->lock);

  if (attempt == 1U)
    return -1;

  setPeerState(ctxp, IOHDLC_PEER_STATE_CONNECTED);
  return 0;
}

/**
 * @brief   Run the manager in its application-owned host thread.
 */
static void *managerThread(void *arg) {
  link_manager_test_t *ctxp = arg;

  ctxp->run_result = ioHdlcLinkManagerRun(&ctxp->manager);
  return NULL;
}

/**
 * @brief   Wait until a state has occurred a requested number of times.
 */
static bool waitForState(link_manager_test_t *ctxp,
                         iohdlc_peer_state_t wanted, size_t occurrences,
                         uint32_t timeout_ms) {
  struct timespec deadline;
  bool found = false;

  (void)clock_gettime(CLOCK_REALTIME, &deadline);
  addMilliseconds(&deadline, timeout_ms);

  (void)pthread_mutex_lock(&ctxp->lock);
  for (;;) {
    size_t count = 0U;

    for (size_t i = 0U; i < ctxp->state_count; ++i) {
      if (ctxp->states[i] == wanted)
        count++;
    }
    if (count >= occurrences) {
      found = true;
      break;
    }
    if (pthread_cond_timedwait(&ctxp->cond, &ctxp->lock, &deadline) ==
        ETIMEDOUT)
      break;
  }
  (void)pthread_mutex_unlock(&ctxp->lock);
  return found;
}

/**
 * @brief   Wait for a requested number of connection attempts.
 */
static bool waitForAttemptCount(link_manager_test_t *ctxp, size_t count,
                                uint32_t timeout_ms) {
  struct timespec deadline;
  bool reached = false;

  (void)clock_gettime(CLOCK_REALTIME, &deadline);
  addMilliseconds(&deadline, timeout_ms);

  (void)pthread_mutex_lock(&ctxp->lock);
  while (ctxp->attempt_count < count) {
    if (pthread_cond_timedwait(&ctxp->cond, &ctxp->lock, &deadline) ==
        ETIMEDOUT)
      break;
  }
  reached = ctxp->attempt_count >= count;
  (void)pthread_mutex_unlock(&ctxp->lock);
  return reached;
}

/**
 * @brief   Read the number of recorded state callbacks.
 */
static size_t getStateCount(link_manager_test_t *ctxp) {
  size_t count;

  (void)pthread_mutex_lock(&ctxp->lock);
  count = ctxp->state_count;
  (void)pthread_mutex_unlock(&ctxp->lock);
  return count;
}

/**
 * @brief   Read the number of active connection attempts.
 */
static size_t getAttemptCount(link_manager_test_t *ctxp) {
  size_t count;

  (void)pthread_mutex_lock(&ctxp->lock);
  count = ctxp->attempt_count;
  (void)pthread_mutex_unlock(&ctxp->lock);
  return count;
}

/**
 * @brief   Read the interval between the first two connection attempts.
 */
static int64_t getRetryIntervalNs(link_manager_test_t *ctxp) {
  int64_t interval;

  (void)pthread_mutex_lock(&ctxp->lock);
  interval =
      (int64_t)(ctxp->attempts[1].tv_sec - ctxp->attempts[0].tv_sec) *
      1000000000LL +
      (int64_t)(ctxp->attempts[1].tv_nsec - ctxp->attempts[0].tv_nsec);
  (void)pthread_mutex_unlock(&ctxp->lock);
  return interval;
}

/**
 * @brief   Stop and join a test manager if its thread was started.
 */
static void stopManager(link_manager_test_t *ctxp) {
  if (!ctxp->thread_started)
    return;

  ioHdlcLinkManagerStop(&ctxp->manager);
  (void)pthread_join(ctxp->thread, NULL);
  ctxp->thread_started = false;
}

/**
 * @brief   Verify passive state monitoring, filtering, and stop wakeup.
 */
static int testPassiveManager(void) {
  link_manager_test_t ctx;
  iohdlc_link_manager_config_t config;
  size_t count;
  int result = 0;

  testObjectInit(&ctx);
  memset(&config, 0, sizeof config);
  config.stationp = &ctx.station;
  config.peerp = &ctx.peer;
  config.event_mask = TEST_EVENT_MASK;
  config.on_state_change = stateCallback;
  config.callback_arg = &ctx;
  ioHdlcLinkManagerObjectInit(&ctx.manager, &config);

  TEST_CHECK(pthread_create(&ctx.thread, NULL, managerThread, &ctx) == 0,
             "cannot start passive manager thread");
  ctx.thread_started = true;
  TEST_CHECK(waitForState(&ctx, IOHDLC_PEER_STATE_DISCONNECTED, 1U,
                          TEST_WAIT_TIMEOUT_MS),
             "initial disconnected state not reported");

  setPeerState(&ctx, IOHDLC_PEER_STATE_CONNECTED);
  iohdlc_evt_broadcast_flags(&ctx.station.app_es, IOHDLC_APP_LINK_UP);
  TEST_CHECK(waitForState(&ctx, IOHDLC_PEER_STATE_CONNECTED, 1U,
                          TEST_WAIT_TIMEOUT_MS),
             "connected transition not reported");

  count = getStateCount(&ctx);
  iohdlc_evt_broadcast_flags(&ctx.station.app_es, IOHDLC_APP_LINK_DOWN);
  sleepMilliseconds(40U);
  TEST_CHECK(getStateCount(&ctx) == count,
             "event for an unchanged managed peer produced a transition");

  setPeerState(&ctx, IOHDLC_PEER_STATE_ORDERLY_CLOSED);
  iohdlc_evt_broadcast_flags(&ctx.station.app_es, IOHDLC_APP_LINK_DOWN);
  TEST_CHECK(waitForState(&ctx, IOHDLC_PEER_STATE_ORDERLY_CLOSED, 1U,
                          TEST_WAIT_TIMEOUT_MS),
             "orderly close transition not reported");

  errno = 0;
  TEST_CHECK(ioHdlcLinkManagerRun(&ctx.manager) == -1 && errno == EBUSY,
             "concurrent run was not rejected");

cleanup:
  stopManager(&ctx);
  if (result == 0 && ctx.run_result != 0) {
    fprintf(stderr, "FAIL: passive manager returned %d\n", ctx.run_result);
    result = 1;
  }
  testObjectDeinit(&ctx);
  return result;
}

/**
 * @brief   Verify readiness gating, bounded retries, and reconnection.
 */
static int testActiveManager(void) {
  link_manager_test_t ctx;
  iohdlc_link_manager_config_t config;
  int result = 0;

  testObjectInit(&ctx);
  memset(&config, 0, sizeof config);
  config.stationp = &ctx.station;
  config.peerp = &ctx.peer;
  config.event_mask = TEST_EVENT_MASK;
  config.retry_interval_ms = TEST_RETRY_INTERVAL_MS;
  config.mode = IOHDLC_OM_NRM;
  config.active = true;
  config.is_ready = readyCallback;
  config.connect = connectCallback;
  config.on_state_change = stateCallback;
  config.callback_arg = &ctx;
  ioHdlcLinkManagerObjectInit(&ctx.manager, &config);

  TEST_CHECK(pthread_create(&ctx.thread, NULL, managerThread, &ctx) == 0,
             "cannot start active manager thread");
  ctx.thread_started = true;
  TEST_CHECK(waitForState(&ctx, IOHDLC_PEER_STATE_DISCONNECTED, 1U,
                          TEST_WAIT_TIMEOUT_MS),
             "active manager initial state not reported");

  sleepMilliseconds(3U * TEST_RETRY_INTERVAL_MS);
  TEST_CHECK(getAttemptCount(&ctx) == 0U,
             "connection attempted while physical readiness was false");

  (void)pthread_mutex_lock(&ctx.lock);
  ctx.ready = true;
  (void)pthread_mutex_unlock(&ctx.lock);
  TEST_CHECK(waitForAttemptCount(&ctx, 1U, TEST_WAIT_TIMEOUT_MS),
             "first connection attempt did not start");
  iohdlc_evt_broadcast_flags(&ctx.station.app_es, IOHDLC_APP_LINK_DOWN);
  sleepMilliseconds(TEST_RETRY_INTERVAL_MS / 4U);
  TEST_CHECK(getAttemptCount(&ctx) == 1U,
             "unrelated station event bypassed the retry deadline");
  TEST_CHECK(waitForState(&ctx, IOHDLC_PEER_STATE_CONNECTED, 1U,
                          TEST_WAIT_TIMEOUT_MS),
             "active manager did not connect after readiness");
  TEST_CHECK(getAttemptCount(&ctx) == 2U,
             "active manager did not perform one bounded retry");
  TEST_CHECK(getRetryIntervalNs(&ctx) >=
             (int64_t)(TEST_RETRY_INTERVAL_MS / 2U) * 1000000LL,
             "connection retry occurred without the configured delay");

  setPeerState(&ctx, IOHDLC_PEER_STATE_ABORTED);
  iohdlc_evt_broadcast_flags(&ctx.station.app_es, IOHDLC_APP_LINK_LOST);
  TEST_CHECK(waitForState(&ctx, IOHDLC_PEER_STATE_ABORTED, 1U,
                          TEST_WAIT_TIMEOUT_MS),
             "link loss transition not reported");
  TEST_CHECK(waitForState(&ctx, IOHDLC_PEER_STATE_CONNECTED, 2U,
                          TEST_WAIT_TIMEOUT_MS),
             "active manager did not reconnect after link loss");
  TEST_CHECK(getAttemptCount(&ctx) == 3U,
             "unexpected reconnection attempt count");

cleanup:
  stopManager(&ctx);
  if (result == 0 && ctx.run_result != 0) {
    fprintf(stderr, "FAIL: active manager returned %d\n", ctx.run_result);
    result = 1;
  }
  testObjectDeinit(&ctx);
  return result;
}

/**
 * @brief   Run the link manager utility tests.
 */
int main(void) {
  int result = 0;

  printf("ioHdlc link manager utility tests\n");
  result |= testPassiveManager();
  result |= testActiveManager();
  if (result == 0)
    printf("PASS\n");
  return result;
}
