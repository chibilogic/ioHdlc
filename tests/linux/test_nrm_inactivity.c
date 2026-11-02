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
#include "ioHdlc_core.h"
#include "ioHdlc_runner.h"
#include "ioHdlcswdriver.h"
#include "mock_stream_adapter.h"
#include <errno.h>
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
  const struct _iohdlc_driver_vmt *original_vmt;
  iohdlc_station_t station;
  iohdlc_station_peer_t peer;
  iohdlc_station_config_t config;
  mock_stream_t stream;
  mock_stream_adapter_t adapter;
  ioHdlcStreamPort port;
  uint8_t arena[32768];
  iohdlc_app_listener_t app;
  iohdlc_event_listener_t expiry;
  iohdlc_binary_semaphore_t rx_done;
  iohdlc_binary_semaphore_t tx_done;
  iohdlc_binary_semaphore_t tx_paused;
  iohdlc_binary_semaphore_t tx_resume;
  iohdlc_mutex_t gate;
  bool pause_requested;
  bool received;
  uint32_t sent;
} fixture_t;

uint32_t __real_ioHdlcWaitEvents(iohdlc_station_t *station);

uint32_t __wrap_ioHdlcWaitEvents(iohdlc_station_t *station) {
  fixture_t *f = (fixture_t *)station->driver;
  bool pause;

  iohdlc_mutex_lock(&f->gate);
  pause = f->pause_requested;
  f->pause_requested = false;
  iohdlc_mutex_unlock(&f->gate);
  if (pause) {
    iohdlc_bsem_signal(&f->tx_paused);
    CHECK(iohdlc_bsem_wait_timeout(&f->tx_resume, 5000U) == 0);
  }
  return __real_ioHdlcWaitEvents(station);
}

static iohdlc_frame_t *observe_rx(void *instance, iohdlc_timeout_t timeout) {
  fixture_t *f = instance;
  iohdlc_frame_t *fp;

  /* The next receive begins only after the core has processed this frame. */
  if (f->received)
    iohdlc_bsem_signal(&f->rx_done);
  fp = f->original_vmt->recv_frame(instance, timeout);
  f->received = fp != NULL;
  return fp;
}

static int32_t observe_tx(void *instance, iohdlc_frame_t *fp) {
  fixture_t *f = instance;
  int32_t result = f->original_vmt->send_frame(instance, fp);

  iohdlc_mutex_lock(&f->gate);
  ++f->sent;
  iohdlc_mutex_unlock(&f->gate);
  iohdlc_bsem_signal(&f->tx_done);
  return result;
}

static fixture_t *setup(uint32_t timeout_ms, uint8_t mode, uint8_t flags) {
  fixture_t *f = calloc(1U, sizeof *f);

  CHECK(f != NULL);
  iohdlc_mutex_init(&f->gate);
  iohdlc_bsem_init(&f->rx_done, true);
  iohdlc_bsem_init(&f->tx_done, true);
  iohdlc_bsem_init(&f->tx_paused, true);
  iohdlc_bsem_init(&f->tx_resume, true);
  mock_stream_init(&f->stream, NULL);
  mock_stream_adapter_init(&f->adapter, &f->stream);
  f->port = mock_stream_adapter_get_port(&f->adapter);
  ioHdlcSwDriverInit(&f->driver, NULL);
  f->original_vmt = f->driver.vmt;
  f->vmt = *f->original_vmt;
  f->vmt.recv_frame = observe_rx;
  f->vmt.send_frame = observe_tx;
  f->driver.vmt = &f->vmt;
  f->config = (iohdlc_station_config_t) {
    .mode = mode, .flags = flags, .log2mod = 3U, .addr = 2U,
    .driver = (ioHdlcDriver *)&f->driver,
    .frame_arena = f->arena, .frame_arena_size = sizeof f->arena,
    .fff_type = 1U, .phydriver = &f->port,
    .inactivity_timeout_ms = timeout_ms
  };
  CHECK(ioHdlcStationInit(&f->station, &f->config) == 0);
  CHECK(ioHdlcAddPeer(&f->station, &f->peer, 1U) == 0);
  (void)iohdlc_evt_wait_any_timeout(EVENT_MASK(1) | EVENT_MASK(2), 0U);
  CHECK(ioHdlcAppListenerRegister(&f->station, &f->app, EVENT_MASK(1),
          IOHDLC_APP_LINK_UP | IOHDLC_APP_LINK_DOWN | IOHDLC_APP_LINK_LOST) == 0);
  iohdlc_evt_register(&f->station.cm_es, &f->expiry, EVENT_MASK(2),
                      IOHDLC_EVT_T3_TMO);
  CHECK(ioHdlcRunnerStart(&f->station) == 0);
  return f;
}

static void teardown(fixture_t *f) {
  CHECK(ioHdlcRunnerStop(&f->station) == 0);
  CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
  CHECK(!f->peer.t3_tmr.expired);
  ioHdlcAppListenerUnregister(&f->app);
  iohdlc_evt_unregister(&f->station.cm_es, &f->expiry);
  CHECK(ioHdlcStationDeinit(&f->station) == 0);
  mock_stream_adapter_deinit(&f->adapter);
  mock_stream_deinit(&f->stream);
  iohdlc_bsem_destroy(&f->rx_done);
  iohdlc_bsem_destroy(&f->tx_done);
  iohdlc_bsem_destroy(&f->tx_paused);
  iohdlc_bsem_destroy(&f->tx_resume);
  free(f);
}

static void pause_tx(fixture_t *f) {
  iohdlc_mutex_lock(&f->gate);
  f->pause_requested = true;
  iohdlc_mutex_unlock(&f->gate);
  ioHdlcBroadcastFlags(&f->station, IOHDLC_EVT_LINE_IDLE);
  CHECK(iohdlc_bsem_wait_timeout(&f->tx_paused, 2000U) == 0);
}

static void inject(fixture_t *f, uint8_t addr, uint8_t ctrl, size_t info_len,
                   bool bad_fcs, bool incomplete) {
  uint8_t bytes[32] = {IOHDLC_FLAG};
  uint16_t fcs;
  size_t len = info_len + 7U;

  CHECK(len <= sizeof bytes);
  bytes[1] = (uint8_t)(info_len + 5U);
  bytes[2] = addr;
  bytes[3] = ctrl;
  ioHdlcComputeFCS(&bytes[1], info_len + 3U, &fcs);
  bytes[info_len + 4U] = (uint8_t)fcs ^ (bad_fcs ? 1U : 0U);
  bytes[info_len + 5U] = (uint8_t)(fcs >> 8);
  bytes[info_len + 6U] = IOHDLC_FLAG;
  if (incomplete)
    len = 4U;
  CHECK(mock_stream_inject_rx(&f->stream, bytes, len) == len);
  if (!bad_fcs && !incomplete)
    CHECK(iohdlc_bsem_wait_timeout(&f->rx_done, 2000U) == 0);
}

static void connect_secondary(fixture_t *f) {
  inject(f, 2U, IOHDLC_U_TYPE | IOHDLC_U_SNRM | IOHDLC_PF_BIT,
          0U, false, false);
  CHECK(ioHdlcAppListenerWait(&f->app, 2000U) == IOHDLC_APP_LINK_UP);
  CHECK(iohdlc_bsem_wait_timeout(&f->tx_done, 2000U) == 0);
  CHECK(ioHdlcPeerGetState(&f->peer) == IOHDLC_PEER_STATE_CONNECTED);
}

static void wait_expiry(fixture_t *f) {
  CHECK(iohdlc_evt_wait_any_timeout(EVENT_MASK(2), 2000U) != 0U);
  CHECK(iohdlc_evt_get_and_clear_flags(&f->expiry) == IOHDLC_EVT_T3_TMO);
}

static void test_config_and_disabled(void) {
  fixture_t *f = setup(0U, IOHDLC_OM_NDM, 0U);
  iohdlc_station_config_t config = f->config;
  iohdlc_station_t invalid;

  config.inactivity_timeout_ms = IOHDLC_WAIT_FOREVER;
  CHECK(ioHdlcStationInit(&invalid, &config) == -EINVAL);
  CHECK(!iohdlc_vt_timeout_valid(0U));
  CHECK(!iohdlc_vt_timeout_valid(IOHDLC_WAIT_FOREVER));
  CHECK(iohdlc_vt_timeout_valid(1U));
  CHECK(iohdlc_vt_timeout_valid(UINT32_MAX - 1U));
  connect_secondary(f);
  CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
  CHECK(ioHdlcAppListenerWait(&f->app, 250U) == 0U);
  CHECK(ioHdlcPeerGetState(&f->peer) == IOHDLC_PEER_STATE_CONNECTED);
  teardown(f);

  f = setup(1U, IOHDLC_OM_NDM, 0U);
  CHECK(f->station.inactivity_timeout_ms == 1U);
  CHECK(f->station.reply_timeout_ms > 1U);
  CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
  teardown(f);

  f = setup(IOHDLC_WAIT_FOREVER, IOHDLC_OM_NDM, IOHDLC_FLG_PRI);
  CHECK(f->station.inactivity_timeout_ms == 0U);
  teardown(f);
  f = setup(IOHDLC_WAIT_FOREVER, IOHDLC_OM_ADM, 0U);
  CHECK(f->station.inactivity_timeout_ms == 0U);
  inject(f, 2U, IOHDLC_U_TYPE | IOHDLC_U_SABM | IOHDLC_PF_BIT,
          0U, false, false);
  CHECK(ioHdlcAppListenerWait(&f->app, 2000U) == IOHDLC_APP_LINK_UP);
  CHECK(iohdlc_bsem_wait_timeout(&f->tx_done, 2000U) == 0);
  CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
  CHECK(ioHdlcAppListenerWait(&f->app, 250U) == 0U);
  teardown(f);
}

typedef struct {
  iohdlc_station_peer_t *peer;
  bool write;
  ssize_t result;
} io_call_t;

static void *blocked_io(void *arg) {
  io_call_t *call = arg;
  uint8_t byte = 0;

  call->result = call->write ? ioHdlcWrite(call->peer, &byte, 1U) :
                               ioHdlcRead(call->peer, &byte, 1U);
  return NULL;
}

static void test_loss_and_pending_response(uint8_t flags) {
  fixture_t *f = setup(200U, IOHDLC_OM_NDM, flags);
  uint8_t data[8192] = {0};
  io_call_t reader = {.peer = &f->peer};
  io_call_t writer = {.peer = &f->peer, .write = true};
  pthread_t read_thread, write_thread;
  uint32_t sent;
  bool sending = false;

  connect_secondary(f);
  pause_tx(f);
  sent = f->sent;
  CHECK(ioHdlcWriteTmo(&f->peer, data, sizeof data, 0U) > 0);
  CHECK(pthread_create(&read_thread, NULL, blocked_io, &reader) == 0);
  CHECK(pthread_create(&write_thread, NULL, blocked_io, &writer) == 0);
  for (uint32_t i = 0U; i < 1000U && !sending; ++i) {
    iohdlc_mutex_lock(&f->peer.state_mutex);
    sending = (f->peer.ss_state & IOHDLC_SS_SENDING) != 0U;
    iohdlc_mutex_unlock(&f->peer.state_mutex);
    if (!sending)
      ioHdlc_sleep_ms(1U);
  }
  CHECK(sending);
  inject(f, 2U, IOHDLC_U_TYPE | IOHDLC_U_TEST | IOHDLC_PF_BIT,
          0U, false, false);
  wait_expiry(f);
  iohdlc_bsem_signal(&f->tx_resume);
  CHECK(ioHdlcAppListenerWait(&f->app, 2000U) == IOHDLC_APP_LINK_LOST);
  CHECK(pthread_join(read_thread, NULL) == 0);
  CHECK(pthread_join(write_thread, NULL) == 0);
  CHECK(reader.result == -ECONNRESET);
  CHECK(writer.result == -ECONNRESET);
  CHECK(ioHdlcPeerGetState(&f->peer) == IOHDLC_PEER_STATE_ABORTED);
  CHECK(f->station.mode == IOHDLC_OM_NDM);
  CHECK(f->station.connected_count == 0U);
  CHECK(f->peer.poll_retry_count == 0U);
  CHECK(f->peer.stats.timeouts == 1U);
  CHECK(ioHdlcAppListenerWait(&f->app, 250U) == 0U);
  pause_tx(f);
  CHECK(f->sent == sent);
  iohdlc_bsem_signal(&f->tx_resume);
  teardown(f);
}

static void test_receive_activity(void) {
  static const struct {
    uint8_t ctrl;
    size_t info_len;
  } frames[] = {
    {IOHDLC_S_TYPE | IOHDLC_S_RR, 0U}, {0U, 1U}, {0U, 1U},
    {IOHDLC_U_TYPE | IOHDLC_U_UI, sizeof (uint32_t)},
    {IOHDLC_U_TYPE | IOHDLC_U_UI, 1U},
    {IOHDLC_U_TYPE | IOHDLC_U_TEST, 0U}
  };
  fixture_t *f = setup(200U, IOHDLC_OM_NDM, IOHDLC_FLG_TWA);
  uint8_t byte;

  connect_secondary(f);
  pause_tx(f);
  for (size_t i = 0U; i < sizeof frames / sizeof frames[0]; ++i) {
    /* A real expired timer and its queued event precede each RX rearm. */
    wait_expiry(f);
    inject(f, 2U, frames[i].ctrl, frames[i].info_len, false, false);
    CHECK(iohdlc_vt_is_armed(&f->peer.t3_tmr));
    CHECK(!f->peer.t3_tmr.expired);
  }
  CHECK(ioHdlcReadTmo(&f->peer, &byte, 1U, 0U) == 1);
  CHECK(f->peer.vr == 1U);
  CHECK(f->peer.stats.out_of_sequence == 1U);
  iohdlc_bsem_signal(&f->tx_resume);
  CHECK(iohdlc_bsem_wait_timeout(&f->tx_done, 2000U) == 0);
  CHECK(ioHdlcAppListenerWait(&f->app, 50U) == 0U);
  CHECK(ioHdlcPeerGetState(&f->peer) == IOHDLC_PEER_STATE_CONNECTED);
  teardown(f);
}

static void test_unrelated_and_damaged_frames(void) {
  for (uint32_t kind = 0U; kind < 3U; ++kind) {
    fixture_t *f = setup(100U, IOHDLC_OM_NDM, 0U);

    connect_secondary(f);
    pause_tx(f);
    wait_expiry(f);
    if (kind == 0U) {
      inject(f, 3U, IOHDLC_S_TYPE | IOHDLC_S_RR, 0U, false, false);
      inject(f, 3U, IOHDLC_U_TYPE | IOHDLC_U_TEST, 0U, false, false);
    } else {
      inject(f, 2U, IOHDLC_S_TYPE | IOHDLC_S_RR, 0U,
              kind == 1U, kind == 2U);
      CHECK(iohdlc_bsem_wait_timeout(&f->rx_done, 100U) != 0);
    }
    CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
    iohdlc_bsem_signal(&f->tx_resume);
    CHECK(ioHdlcAppListenerWait(&f->app, 2000U) == IOHDLC_APP_LINK_LOST);
    teardown(f);
  }
}

static void test_reset_disconnect_and_reconnect(void) {
  fixture_t *f = setup(200U, IOHDLC_OM_NDM, 0U);

  /* TEST in NDM must not start inactivity monitoring. */
  inject(f, 2U, IOHDLC_U_TYPE | IOHDLC_U_TEST | IOHDLC_PF_BIT,
          0U, false, false);
  CHECK(iohdlc_bsem_wait_timeout(&f->tx_done, 2000U) == 0);
  CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
  CHECK(ioHdlcAppListenerWait(&f->app, 250U) == 0U);
  connect_secondary(f);

  pause_tx(f);
  wait_expiry(f);
  inject(f, 2U, IOHDLC_U_TYPE | IOHDLC_U_SNRM | IOHDLC_PF_BIT,
          0U, false, false);
  CHECK(ioHdlcAppListenerWait(&f->app, 2000U) == IOHDLC_APP_LINK_UP);
  CHECK(f->station.connected_count == 1U);
  iohdlc_bsem_signal(&f->tx_resume);
  CHECK(iohdlc_bsem_wait_timeout(&f->tx_done, 2000U) == 0);
  CHECK(ioHdlcAppListenerWait(&f->app, 50U) == 0U);

  inject(f, 2U, IOHDLC_U_TYPE | IOHDLC_U_DISC | IOHDLC_PF_BIT,
          0U, false, false);
  CHECK(iohdlc_bsem_wait_timeout(&f->tx_done, 2000U) == 0);
  CHECK(ioHdlcAppListenerWait(&f->app, 2000U) == IOHDLC_APP_LINK_DOWN);
  CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
  ioHdlcBroadcastFlags(&f->station, IOHDLC_EVT_T3_TMO);
  CHECK(ioHdlcAppListenerWait(&f->app, 250U) == 0U);

  connect_secondary(f);
  CHECK(ioHdlcAppListenerWait(&f->app, 2000U) == IOHDLC_APP_LINK_LOST);
  connect_secondary(f);
  ioHdlcBroadcastFlags(&f->station, IOHDLC_EVT_T3_TMO);
  CHECK(ioHdlcAppListenerWait(&f->app, 50U) == 0U);
  CHECK(f->station.connected_count == 1U);
  CHECK(ioHdlcRunnerStop(&f->station) == 0);
  CHECK(!iohdlc_vt_is_armed(&f->peer.t3_tmr));
  CHECK(ioHdlcAppListenerWait(&f->app, 250U) == 0U);
  teardown(f);
}

int main(void) {
  test_config_and_disabled();
  test_loss_and_pending_response(0U);
  test_loss_and_pending_response(IOHDLC_FLG_TWA);
  test_receive_activity();
  test_unrelated_and_damaged_frames();
  test_reset_disconnect_and_reconnect();
  puts("NRM secondary inactivity tests passed");
  return 0;
}
