/**
 * @file    mock_stream_adapter_chibios.c
 * @brief   Mock stream adapter for ChibiOS (simplified, memory-based).
 * @details Minimal implementation for ChibiOS test environment.
 */

#include "mock_stream_adapter.h"
#include "../../../include/ioHdlcstream_driver.h"
#include <stdlib.h>
#include <string.h>

/* Port operations */
static void port_start(void *ctx, const ioHdlcStreamCallbacks *cbs);
static void port_stop(void *ctx);
static bool port_tx_submit(void *ctx, const uint8_t *ptr, size_t len, void *framep);
static bool port_tx_busy(void *ctx);
static bool port_rx_submit(void *ctx, uint8_t *ptr, size_t len);

static const ioHdlcStreamPortOps s_port_ops = {
  .start = port_start,
  .stop = port_stop,
  .tx_submit = port_tx_submit,
  .tx_busy = port_tx_busy,
  .rx_submit = port_rx_submit,
  .rx_cancel = NULL
};

mock_stream_adapter_t* mock_stream_adapter_create(mock_stream_t *stream) {
  if (!stream) {
    return NULL;
  }

  mock_stream_adapter_t *adapter = chHeapAlloc(NULL, sizeof(*adapter));
  if (!adapter) {
    return NULL;
  }

  memset(adapter, 0, sizeof(*adapter));
  adapter->stream = stream;
  return adapter;
}

void mock_stream_adapter_destroy(mock_stream_adapter_t *adapter) {
  if (!adapter) {
    return;
  }
  chHeapFree(adapter);
}

ioHdlcStreamPort mock_stream_adapter_get_port(mock_stream_adapter_t *adapter) {
  ioHdlcStreamPort port = {
    .ctx = adapter,
    .ops = &s_port_ops
  };
  return port;
}

static void port_start(void *ctx, const ioHdlcStreamCallbacks *cbs) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  if (cbs) {
    adapter->callbacks = *cbs;
  }
}

static void port_stop(void *ctx) {
  (void)ctx;
}

static bool port_tx_submit(void *ctx, const uint8_t *ptr, size_t len, void *framep) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  /* Simplified: directly write (mock stream has large buffer) */
  ssize_t written = mock_stream_write(adapter->stream, ptr, len, 0);
  
  if (written == (ssize_t)len && adapter->callbacks.on_tx_done) {
    adapter->callbacks.on_tx_done(adapter->callbacks.cb_ctx, framep);
    return true;
  }
  return false;
}

static bool port_tx_busy(void *ctx) {
  (void)ctx;
  return false;
}

static bool port_rx_submit(void *ctx, uint8_t *ptr, size_t len) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  /* Simplified: try immediate read */
  ssize_t result = mock_stream_read(adapter->stream, ptr, len, 0);
  
  if (result > 0 && adapter->callbacks.on_rx) {
    adapter->callbacks.on_rx(adapter->callbacks.cb_ctx, 0);
    return true;
  }
  return (result >= 0);
}
