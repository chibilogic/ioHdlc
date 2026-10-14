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
#include "ioHdlc.h"
#include "ioHdlcswdriver.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

enum {
  REGISTER_ENTERED, REGISTER_ALLOWED, WAIT_ENTERED, WAIT_ALLOWED,
  READY_SIGNALED, START_RETURNED, GATE_COUNT
};

typedef struct {
  ioHdlcSwDriver driver;
  iohdlc_station_t station;
  iohdlc_station_peer_t peer;
  uint8_t arena[16384];
  iohdlc_binary_semaphore_t gates[GATE_COUNT];
  bool hold_register;
  bool hold_wait;
  uint32_t fail_creation;
  int32_t result;
} fixture_t;

static fixture_t *fixture;
static _Thread_local bool track_creations;
static _Thread_local uint32_t creations;

int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                           void *(*entry)(void *), void *arg);
void __real_iohdlc_evt_register(iohdlc_event_source_t *esp,
                                 iohdlc_event_listener_t *elp,
                                 eventmask_t events, eventflags_t flags);
int __real_iohdlc_bsem_wait_timeout(iohdlc_bsem_t *bsem, uint32_t timeout);
void __real_iohdlc_bsem_signal(iohdlc_bsem_t *bsem);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                           void *(*entry)(void *), void *arg) {
  if (track_creations) {
    ++creations;
    if (creations == 2U) {
      pthread_mutex_lock(&fixture->station.cm_es.lock);
      CHECK(fixture->station.cm_es.listeners == &fixture->station.cm_listener);
      pthread_mutex_unlock(&fixture->station.cm_es.lock);
    }
    if (creations == fixture->fail_creation)
      return EAGAIN;
  }
  return __real_pthread_create(thread, attr, entry, arg);
}

void __wrap_iohdlc_evt_register(iohdlc_event_source_t *esp,
                                 iohdlc_event_listener_t *elp,
                                 eventmask_t events, eventflags_t flags) {
  if (fixture->hold_register && elp == &fixture->station.cm_listener) {
    iohdlc_bsem_signal(&fixture->gates[REGISTER_ENTERED]);
    CHECK(iohdlc_bsem_wait_timeout(&fixture->gates[REGISTER_ALLOWED],
                                    2000U) == MSG_OK);
  }
  __real_iohdlc_evt_register(esp, elp, events, flags);
}

int __wrap_iohdlc_bsem_wait_timeout(iohdlc_bsem_t *bsem, uint32_t timeout) {
  if (track_creations && fixture->hold_register &&
      bsem == &fixture->station.runner_context->tx_ready) {
    CHECK(timeout == IOHDLC_WAIT_FOREVER);
    iohdlc_bsem_signal(&fixture->gates[WAIT_ENTERED]);
    if (fixture->hold_wait)
      CHECK(iohdlc_bsem_wait_timeout(&fixture->gates[WAIT_ALLOWED],
                                      2000U) == MSG_OK);
  }
  return __real_iohdlc_bsem_wait_timeout(bsem, timeout);
}

void __wrap_iohdlc_bsem_signal(iohdlc_bsem_t *bsem) {
  runner_context_t *ctx = fixture->station.runner_context;

  if (fixture->hold_register && ctx != NULL && bsem == &ctx->tx_ready) {
    pthread_mutex_lock(&fixture->station.cm_es.lock);
    CHECK(fixture->station.cm_es.listeners == &fixture->station.cm_listener);
    CHECK(pthread_equal(fixture->station.cm_listener.thread, pthread_self()));
    pthread_mutex_unlock(&fixture->station.cm_es.lock);
    __real_iohdlc_bsem_signal(bsem);
    __real_iohdlc_bsem_signal(&fixture->gates[READY_SIGNALED]);
    return;
  }
  __real_iohdlc_bsem_signal(bsem);
}

static void setup(void) {
  iohdlc_station_config_t config = {0};

  fixture = calloc(1U, sizeof *fixture);
  CHECK(fixture != NULL);
  for (uint32_t i = 0; i < GATE_COUNT; ++i)
    iohdlc_bsem_init(&fixture->gates[i], true);
  ioHdlcSwDriverInit(&fixture->driver, NULL);
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3U;
  config.addr = 1U;
  config.driver = (ioHdlcDriver *)&fixture->driver;
  config.frame_arena = fixture->arena;
  config.frame_arena_size = sizeof fixture->arena;
  config.fff_type = 1U;
  CHECK(ioHdlcStationInit(&fixture->station, &config) == 0);
  CHECK(ioHdlcAddPeer(&fixture->station, &fixture->peer, 2U) == 0);
}

static void teardown(void) {
  CHECK(ioHdlcStationDeinit(&fixture->station) == 0);
  CHECK(fixture->station.runner_context == NULL);
  CHECK(!fixture->station.runner_started);
  CHECK(fixture->station.cm_es.listeners == NULL);
  for (uint32_t i = 0; i < GATE_COUNT; ++i)
    iohdlc_bsem_destroy(&fixture->gates[i]);
  free(fixture);
  fixture = NULL;
}

static void *start_runner(void *arg) {
  (void)arg;
  track_creations = true;
  creations = 0U;
  fixture->result = ioHdlcRunnerStart(&fixture->station);
  CHECK(creations == 2U);
  track_creations = false;
  iohdlc_bsem_signal(&fixture->gates[START_RETURNED]);
  return NULL;
}

static void test_startup_barrier(bool signal_before_wait) {
  pthread_t thread;

  setup();
  fixture->hold_register = true;
  fixture->hold_wait = signal_before_wait;
  CHECK(pthread_create(&thread, NULL, start_runner, NULL) == 0);
  CHECK(iohdlc_bsem_wait_timeout(&fixture->gates[REGISTER_ENTERED], 2000U) == MSG_OK);
  CHECK(iohdlc_bsem_wait_timeout(&fixture->gates[WAIT_ENTERED], 2000U) == MSG_OK);
  CHECK(iohdlc_bsem_wait_timeout(&fixture->gates[START_RETURNED], 0U) != MSG_OK);

  iohdlc_bsem_signal(&fixture->gates[REGISTER_ALLOWED]);
  if (signal_before_wait) {
    CHECK(iohdlc_bsem_wait_timeout(&fixture->gates[READY_SIGNALED], 2000U) == MSG_OK);
    CHECK(iohdlc_bsem_wait_timeout(&fixture->gates[START_RETURNED], 0U) != MSG_OK);
    iohdlc_bsem_signal(&fixture->gates[WAIT_ALLOWED]);
  }
  CHECK(pthread_join(thread, NULL) == 0);
  CHECK(fixture->result == 0);
  CHECK(fixture->station.runner_started);
  CHECK(pthread_equal(fixture->station.cm_listener.thread,
                        fixture->station.runner_context->tx_thread->handle));
  CHECK(ioHdlcRunnerStop(&fixture->station) == 0);
  teardown();
}

static void test_creation_failure(uint32_t fail_creation) {
  setup();
  fixture->fail_creation = fail_creation;
  track_creations = true;
  creations = 0U;
  CHECK(ioHdlcRunnerStart(&fixture->station) == -EAGAIN);
  CHECK(creations == fail_creation);
  track_creations = false;
  CHECK(fixture->station.runner_context == NULL);
  CHECK(!fixture->station.runner_started);
  CHECK(fixture->station.cm_es.listeners == NULL);
  CHECK(ioHdlcRunnerStop(&fixture->station) == 0);

  CHECK(ioHdlcRunnerStart(&fixture->station) == 0);
  CHECK(ioHdlcRunnerStop(&fixture->station) == 0);
  teardown();
}

int main(void) {
  alarm(20U);
  test_startup_barrier(false);
  test_startup_barrier(true);
  test_creation_failure(1U);
  test_creation_failure(2U);

  setup();
  for (uint32_t i = 0; i < 10U; ++i) {
    CHECK(ioHdlcRunnerStart(&fixture->station) == 0);
    CHECK(ioHdlcRunnerStop(&fixture->station) == 0);
    CHECK(!fixture->station.runner_started);
    CHECK(fixture->station.runner_context == NULL);
    CHECK(fixture->station.cm_es.listeners == NULL);
  }
  teardown();
  alarm(0U);
  puts("Runner startup regressions passed");
  return 0;
}
