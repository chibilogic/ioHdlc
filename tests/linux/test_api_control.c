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
/* Deterministic control API tests: submit through the real API and feed
   decoded U responses through the real RX core, without a running TX worker. */
#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlcswdriver.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

typedef struct {
  ioHdlcSwDriver driver;
  struct _iohdlc_driver_vmt vmt;
  iohdlc_station_t station;
  iohdlc_station_peer_t peers[2];
  uint8_t arena[16384];
  iohdlc_event_listener_t requests;
  iohdlc_frame_t *next_rx;
  iohdlc_binary_semaphore_t releasing;
  iohdlc_binary_semaphore_t resume;
} fixture_t;

typedef enum { CALL_LINK_UP, CALL_LINK_DOWN, CALL_TEST } call_kind_t;

typedef struct {
  iohdlc_station_peer_t *peer;
  call_kind_t kind;
  int32_t result;
  uint32_t elapsed_ms;
} call_t;

static iohdlc_frame_t *receive_one(void *instance, iohdlc_timeout_t timeout) {
  fixture_t *f = instance;
  iohdlc_frame_t *fp = f->next_rx;

  (void)timeout;
  f->next_rx = NULL;
  if (fp == NULL)
    f->station.stop_requested = true;
  return fp;
}

static fixture_t *setup(void) {
  fixture_t *f = calloc(1U, sizeof *f);
  iohdlc_station_config_t config = {0};

  CHECK(f != NULL);
  ioHdlcSwDriverInit(&f->driver, NULL);
  f->vmt = *f->driver.vmt;
  f->vmt.recv_frame = receive_one;
  f->driver.vmt = &f->vmt;
  config.mode = IOHDLC_OM_NDM;
  config.flags = IOHDLC_FLG_PRI;
  config.log2mod = 3U;
  config.addr = 1U;
  config.driver = (ioHdlcDriver *)&f->driver;
  config.frame_arena = f->arena;
  config.frame_arena_size = sizeof f->arena;
  config.fff_type = 1U;
  config.reply_timeout_ms = 20U;
  config.poll_retry_max = 1U;
  CHECK(ioHdlcStationInit(&f->station, &config) == 0);
  CHECK(ioHdlcAddPeer(&f->station, &f->peers[0], 2U) == 0);
  CHECK(ioHdlcAddPeer(&f->station, &f->peers[1], 3U) == 0);
  iohdlc_evt_register(&f->station.cm_es, &f->requests, EVENT_MASK(2),
                      IOHDLC_EVT_LINK_REQ);
  return f;
}

static void teardown(fixture_t *f) {
  iohdlc_evt_unregister(&f->station.cm_es, &f->requests);
  CHECK(ioHdlcStationDeinit(&f->station) == 0);
  CHECK(f->station.frame_pool.allocated == 0U);
  free(f);
}

static void *control_call(void *arg) {
  call_t *call = arg;
  uint32_t start_ms = iohdlc_time_now_ms();

  switch (call->kind) {
  case CALL_LINK_UP:
    call->result = ioHdlcStationLinkUp(call->peer->stationp,
                                       call->peer->addr, IOHDLC_OM_NRM);
    break;
  case CALL_LINK_DOWN:
    call->result = ioHdlcStationLinkDown(call->peer->stationp,
                                         call->peer->addr);
    break;
  case CALL_TEST:
    call->result = ioHdlcPeerTest(call->peer, 8U, 1000U);
    break;
  }
  call->elapsed_ms = iohdlc_time_now_ms() - start_ms;
  return NULL;
}

static pthread_t start_call(fixture_t *f, call_t *call) {
  pthread_t thread;

  (void)iohdlc_evt_get_and_clear_flags(&f->requests);
  (void)iohdlc_evt_wait_any_timeout(EVENT_MASK(2), 0U);
  CHECK(pthread_create(&thread, NULL, control_call, call) == 0);
  CHECK(iohdlc_evt_wait_any_timeout(EVENT_MASK(2), 1000U) != 0U);
  CHECK(iohdlc_evt_get_and_clear_flags(&f->requests) & IOHDLC_EVT_LINK_REQ);
  return thread;
}

static void reply(fixture_t *f, uint32_t index, uint8_t command) {
  iohdlc_station_peer_t *p = &f->peers[index];
  iohdlc_frame_t *fp = hdlcTakeFrame(&f->station.frame_pool);

  CHECK(fp != NULL);
  iohdlc_mutex_lock(&p->state_mutex);
  p->um_state |= IOHDLC_UM_SENT;
  ioHdlcBuildUFrame(&f->station, p, fp, command, true, true);
  if (command == IOHDLC_U_TEST) {
    CHECK(p->um_api_frame != NULL);
    fp->elen = p->um_api_frame->elen;
    memcpy(fp->frame, p->um_api_frame->frame, fp->elen);
  }
  iohdlc_mutex_unlock(&p->state_mutex);
  f->station.c_peer = p;
  f->next_rx = fp;
  ioHdlcRxEntry(&f->station);
  f->station.stop_requested = false;
}

static void test_control_exclusion(void) {
  for (call_kind_t kind = CALL_LINK_UP; kind <= CALL_TEST; ++kind) {
    fixture_t *f = setup();
    call_t call = {.peer = &f->peers[0], .kind = kind};
    pthread_t thread;
    uint8_t command;

    if (kind == CALL_LINK_DOWN) {
      f->station.mode = IOHDLC_OM_NRM;
      f->station.connected_count = 1U;
      f->peers[0].ss_state = IOHDLC_SS_ST_CONN;
    }
    thread = start_call(f, &call);
    iohdlc_mutex_lock(&f->peers[0].state_mutex);
    command = f->peers[0].um_cmd;
    iohdlc_mutex_unlock(&f->peers[0].state_mutex);

    CHECK(ioHdlcStationLinkUp(&f->station, 2U, IOHDLC_OM_NRM) == -EBUSY);
    CHECK(ioHdlcStationLinkDown(&f->station, 2U) == -EBUSY);
    CHECK(ioHdlcPeerTest(&f->peers[0], 8U, 100U) == -EBUSY);
    if (kind == CALL_LINK_UP)
      CHECK(ioHdlcStationLinkUp(&f->station, 3U, IOHDLC_OM_NRM) == -EBUSY);
    iohdlc_mutex_lock(&f->peers[0].state_mutex);
    CHECK(f->peers[0].um_cmd == command);
    iohdlc_mutex_unlock(&f->peers[0].state_mutex);

    reply(f, 0U, kind == CALL_TEST ? IOHDLC_U_TEST : IOHDLC_U_UA);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(call.result == 0);
    CHECK(!f->peers[0].um_api_busy);
    CHECK(f->station.linkup_peer == NULL);
    if (kind == CALL_TEST) {
      thread = start_call(f, &call);
      reply(f, 0U, IOHDLC_U_TEST);
      CHECK(pthread_join(thread, NULL) == 0);
      CHECK(call.result == 0);
    }
    teardown(f);
  }
}

static void pause_release(void *arg) {
  fixture_t *f = arg;

  iohdlc_bsem_signal(&f->releasing);
  CHECK(iohdlc_bsem_wait_timeout(&f->resume, 1000U) == MSG_OK);
}

static void test_completion_ownership(void) {
  fixture_t *f = setup();
  call_t call = {.peer = &f->peers[0], .kind = CALL_TEST};
  pthread_t thread;

  iohdlc_bsem_init(&f->releasing, true);
  iohdlc_bsem_init(&f->resume, true);
  thread = start_call(f, &call);
  /* Pause the API after validation, when releasing the last owned frame.
     The core's command release leaves one response reference outstanding. */
  pthread_mutex_lock(&f->station.frame_pool.mp.lock);
  f->station.frame_pool.state = IOHDLC_POOL_LOW_WATER;
  f->station.frame_pool.high_threshold = f->station.frame_pool.total - 1U;
  f->station.frame_pool.on_normal = pause_release;
  f->station.frame_pool.cb_arg = f;
  pthread_mutex_unlock(&f->station.frame_pool.mp.lock);
  reply(f, 0U, IOHDLC_U_TEST);
  CHECK(iohdlc_bsem_wait_timeout(&f->releasing, 1000U) == MSG_OK);
  iohdlc_mutex_lock(&f->peers[0].state_mutex);
  CHECK(f->peers[0].um_cmd == 0 && f->peers[0].um_api_frame == NULL);
  iohdlc_mutex_unlock(&f->peers[0].state_mutex);
  CHECK(ioHdlcStationLinkUp(&f->station, 2U, IOHDLC_OM_NRM) == -EBUSY);
  CHECK(ioHdlcStationLinkDown(&f->station, 2U) == -EBUSY);
  CHECK(ioHdlcPeerTest(&f->peers[0], 8U, 100U) == -EBUSY);
  iohdlc_bsem_signal(&f->resume);
  CHECK(pthread_join(thread, NULL) == 0);
  CHECK(call.result == 0);
  teardown(f);
}

static void test_refusal_modes(void) {
  for (uint32_t connected = 0U; connected <= 1U; ++connected) {
    fixture_t *f = setup();
    call_t call = {.peer = &f->peers[1], .kind = CALL_LINK_UP};
    uint8_t original_mode = connected ? IOHDLC_OM_NRM : IOHDLC_OM_NDM;
    pthread_t thread;

    f->station.mode = original_mode;
    f->station.connected_count = connected;
    if (connected)
      f->peers[0].ss_state = IOHDLC_SS_ST_CONN;
    thread = start_call(f, &call);
    reply(f, 1U, IOHDLC_U_DM);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(call.result == -ECONNREFUSED);
    CHECK(f->station.mode == original_mode);
    CHECK(f->station.connected_count == connected);
    CHECK(f->station.linkup_peer == NULL);
    if (connected) {
      CHECK(ioHdlcStationLinkUp(&f->station, 3U, IOHDLC_OM_ABM) == -EINVAL);
      CHECK(f->station.mode == IOHDLC_OM_NRM);
    }
    teardown(f);
  }
}

static void test_timeout_budget_and_late_reply(void) {
  for (uint32_t accept = 0U; accept <= 1U; ++accept) {
    fixture_t *f = setup();
    call_t call = {.peer = &f->peers[0], .kind = CALL_LINK_UP};
    pthread_t thread = start_call(f, &call);

    for (uint32_t i = 0U; i < 50U; ++i) {
      ioHdlcBroadcastFlagsApp(&f->station,
                              IOHDLC_APP_LINK_UP | IOHDLC_APP_LINK_REFUSED);
      ioHdlc_sleep_ms(10U);
    }
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(call.result == -ETIMEDOUT);
    CHECK(call.elapsed_ms < 500U);
    CHECK(f->station.mode == IOHDLC_OM_NRM);
    CHECK(f->station.linkup_peer == &f->peers[0]);
    CHECK(!f->peers[0].um_api_busy);
    CHECK(ioHdlcStationLinkUp(&f->station, 3U, IOHDLC_OM_NRM) == -EBUSY);
    CHECK(ioHdlcStationLinkDown(&f->station, 2U) == -EBUSY);
    CHECK(ioHdlcPeerTest(&f->peers[0], 8U, 100U) == -EBUSY);

    reply(f, 0U, accept ? IOHDLC_U_UA : IOHDLC_U_DM);
    CHECK(f->station.linkup_peer == NULL);
    CHECK(f->station.mode == (accept ? IOHDLC_OM_NRM : IOHDLC_OM_NDM));
    CHECK(f->station.connected_count == accept);
    call.peer = &f->peers[1];
    thread = start_call(f, &call);
    reply(f, 1U, IOHDLC_U_UA);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(call.result == 0);
    CHECK(f->station.mode == IOHDLC_OM_NRM);
    teardown(f);
  }
}

static void test_last_peer_closes_during_linkup(void) {
  fixture_t *f = setup();
  call_t call = {.peer = &f->peers[1], .kind = CALL_LINK_UP};
  pthread_t thread;

  f->station.mode = IOHDLC_OM_NRM;
  f->station.connected_count = 1U;
  f->peers[0].ss_state = IOHDLC_SS_ST_CONN;
  thread = start_call(f, &call);
  f->peers[0].um_cmd = IOHDLC_U_DISC;
  reply(f, 0U, IOHDLC_U_UA);
  CHECK(f->station.mode == IOHDLC_OM_NDM);
  reply(f, 1U, IOHDLC_U_UA);
  CHECK(pthread_join(thread, NULL) == 0);
  CHECK(call.result == 0);
  CHECK(f->station.mode == IOHDLC_OM_NRM);
  CHECK(f->station.rx_fn == ioHdlcNrmRx);
  CHECK(f->station.connected_count == 1U);
  teardown(f);
}

static void test_partial_io_timeout(void) {
  fixture_t *f = setup();
  iohdlc_station_peer_t *p = &f->peers[0];
  uint8_t input[256];
  uint8_t output[256] = {0};
  iohdlc_const_iovec_t write_iov[2];
  iohdlc_iovec_t read_iov[2];
  iohdlc_frame_q_t *qh;
  uint32_t margin = p->ks / IOHDLC_WRITER_PENDING_MARGIN_DIVISOR;

  CHECK(p->mifls + 1U <= sizeof input);
  memset(input, 0x5A, sizeof input);
  p->ss_state = IOHDLC_SS_ST_CONN;
  if (margin < IOHDLC_WRITER_PENDING_MARGIN_MIN)
    margin = IOHDLC_WRITER_PENDING_MARGIN_MIN;
  p->i_pending_count = p->ks + margin - 1U;
  write_iov[0] = (iohdlc_const_iovec_t){input, 1U};
  write_iov[1] = (iohdlc_const_iovec_t){input + 1U, p->mifls};
  CHECK(ioHdlcWriteVTmo(p, write_iov, 2U, 0U) == (ssize_t)p->mifls);
  CHECK(ioHdlcWriteTmo(p, input, 1U, 0U) == -ETIMEDOUT);

  qh = ioHdlc_frameq_remove(&p->i_trans_q);
  CHECK(qh != NULL);
  p->i_pending_count = 0U;
  ioHdlc_frameq_insert(&p->i_recept_q, qh);
  read_iov[0] = (iohdlc_iovec_t){output, 1U};
  read_iov[1] = (iohdlc_iovec_t){output + 1U, p->mifls};
  CHECK(ioHdlcReadVTmo(p, read_iov, 2U, 0U) == (ssize_t)p->mifls);
  CHECK(memcmp(input, output, p->mifls) == 0);
  CHECK(output[p->mifls] == 0U);
  CHECK(ioHdlcReadTmo(p, output, 1U, 0U) == -ETIMEDOUT);
  teardown(f);
}

static void test_rejected_arguments(void) {
  fixture_t *f = setup();
  uint32_t value = 17U;

  CHECK(ioHdlcPeerUiGet(NULL, &value) == -EINVAL);
  CHECK(ioHdlcPeerUiGet(&f->peers[0], NULL) == -EINVAL);
  CHECK(ioHdlcPeerUiGet(&f->peers[0], &value) == 0 && value == 17U);
  f->peers[0].ui_rx_pending = true;
  f->peers[0].ui_rx_value = 42U;
  CHECK(ioHdlcPeerUiGet(&f->peers[0], &value) == 1 && value == 42U);
  CHECK(ioHdlcPeerUiGet(&f->peers[0], &value) == 0);
  CHECK(ioHdlcPeerGetState(NULL) == -EINVAL);
  CHECK(IOHDLC_PEER_STATE_DISCONNECTED == 0);
  CHECK(IOHDLC_PEER_STATE_CONNECTED == 1);
  CHECK(ioHdlcStationLinkUpEx(&f->station, 2U, IOHDLC_OM_ABM, 0U) == -EINVAL);
  f->station.port_constraints = IOHDLC_PORT_CONSTR_NRM_ONLY;
  f->station.flags = 0U;
  CHECK(ioHdlcStationLinkUp(&f->station, 2U, IOHDLC_OM_ABM) == -ENOTSUP);
  CHECK(f->station.flags == 0U && f->station.mode == IOHDLC_OM_NDM);
  CHECK(f->station.linkup_peer == NULL && !f->peers[0].um_api_busy);
  teardown(f);
}

int main(void) {
  test_rejected_arguments();
  test_control_exclusion();
  test_completion_ownership();
  test_refusal_modes();
  test_timeout_budget_and_late_reply();
  test_last_peer_closes_during_linkup();
  test_partial_io_timeout();
  puts("Control API regressions passed");
  return 0;
}
