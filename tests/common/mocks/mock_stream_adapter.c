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
 * @file    mock_stream_adapter.c
 * @brief   Mock stream adapter implementation (unified OSAL version).
 * @details Connects mock_stream to ioHdlcSwDriver via ioHdlcStreamPort.
 */

#include "mock_stream_adapter.h"
#include "ioHdlcstreamport.h"
#include "ioHdlcosal.h"
#include <string.h>

/*===========================================================================*/
/* Forward declarations                                                      */
/*===========================================================================*/

static void* adapter_rx_thread(void *arg);

/* Port operations */
static void port_start(void *ctx, const ioHdlcStreamCallbacks *cbs);
static void port_stop(void *ctx);
static bool port_tx_submit(void *ctx, const uint8_t *ptr, size_t len, void *framep);
static bool port_tx_busy(void *ctx);
static bool port_rx_submit(void *ctx, uint8_t *ptr, size_t len);
static void port_rx_cancel(void *ctx);

/*===========================================================================*/
/* Port operations table                                                     */
/*===========================================================================*/

static const ioHdlcStreamPortOps s_port_ops = {
  .start = port_start,
  .stop = port_stop,
  .tx_submit = port_tx_submit,
  .tx_busy = port_tx_busy,
  .rx_submit = port_rx_submit,
  .rx_cancel = port_rx_cancel
};

/*===========================================================================*/
/* Implementation                                                            */
/*===========================================================================*/

mock_stream_adapter_t* mock_stream_adapter_create(mock_stream_t *stream) {
  if (!stream) {
    return NULL;
  }
  
  mock_stream_adapter_t *adapter = IOHDLC_MALLOC(sizeof(mock_stream_adapter_t));
  if (!adapter) {
    return NULL;
  }
  
  memset(adapter, 0, sizeof(*adapter));
  adapter->stream = stream;
  adapter->running = false;
  adapter->thread_started = false;
  iohdlc_mutex_init(&adapter->rx_lock);
  adapter->rx_buf = NULL;
  adapter->rx_len = 0;
  adapter->rx_pos = 0;
  
  return adapter;
}

void mock_stream_adapter_destroy(mock_stream_adapter_t *adapter) {
  if (!adapter) {
    return;
  }
  
  /* Stop RX thread if running */
  if (adapter->thread_started) {
    adapter->running = false;
    iohdlc_thread_join(adapter->rx_thread);
    adapter->thread_started = false;
  }
  
  IOHDLC_FREE(adapter);
}

ioHdlcStreamPort mock_stream_adapter_get_port(mock_stream_adapter_t *adapter) {
  ioHdlcStreamPort port = {
    .ctx = adapter,
    .ops = &s_port_ops
  };
  return port;
}

/*===========================================================================*/
/* Port operations implementation                                            */
/*===========================================================================*/

static void port_start(void *ctx, const ioHdlcStreamCallbacks *cbs) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  if (!adapter || adapter->running) {
    return;
  }
  
  /* Save callbacks */
  if (cbs) {
    adapter->callbacks = *cbs;
  }
  
  /* Start RX thread */
  adapter->running = true;
  
  /* Thread configuration */
  const char *thread_name = "mock_rx";
  size_t stack_size = 2048;
  uint32_t priority = 0;  /* Default priority */
  
  adapter->rx_thread = iohdlc_thread_create(thread_name, stack_size, priority,
                                             adapter_rx_thread, adapter);
  if (adapter->rx_thread) {
    adapter->thread_started = true;
  } else {
    adapter->running = false;
  }
}

static void port_stop(void *ctx) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  if (!adapter || !adapter->running) {
    return;
  }
  
  /* Stop RX thread */
  adapter->running = false;
  if (adapter->thread_started) {
    iohdlc_thread_join(adapter->rx_thread);
    adapter->thread_started = false;
  }
}

static bool port_tx_submit(void *ctx, const uint8_t *ptr, size_t len, void *framep) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  if (!adapter || !ptr || len == 0) {
    return false;
  }
  
  /* Non-blocking write to mock stream */
  ssize_t written = mock_stream_write(adapter->stream, ptr, len, 0);
  
  if (written == (ssize_t)len) {
    /* TX complete, invoke callback */
    if (adapter->callbacks.on_tx_done) {
      adapter->callbacks.on_tx_done(adapter->callbacks.cb_ctx, framep);
    }
    return true;
  }
  
  return false;
}

static bool port_tx_busy(void *ctx) {
  /* Mock stream is never busy (has large buffer) */
  (void)ctx;
  return false;
}

static bool port_rx_submit(void *ctx, uint8_t *ptr, size_t len) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  if (!adapter || !ptr || len == 0) {
    return false;
  }
  
  iohdlc_mutex_lock(&adapter->rx_lock);
  adapter->rx_buf = ptr;
  adapter->rx_len = len;
  adapter->rx_pos = 0;
  iohdlc_mutex_unlock(&adapter->rx_lock);
  
  return true;
}

static void port_rx_cancel(void *ctx) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  if (!adapter) {
    return;
  }
  
  /* Clear RX buffer registration */
  iohdlc_mutex_lock(&adapter->rx_lock);
  adapter->rx_buf = NULL;
  adapter->rx_len = 0;
  adapter->rx_pos = 0;
  iohdlc_mutex_unlock(&adapter->rx_lock);
}

/*===========================================================================*/
/* RX background thread                                                      */
/*===========================================================================*/

static void* adapter_rx_thread(void *arg) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)arg;
  
  while (adapter->running) {
    iohdlc_mutex_lock(&adapter->rx_lock);
    
    /* Check if we have a buffer to fill */
    if (adapter->rx_buf && adapter->rx_pos < adapter->rx_len) {
      uint8_t *target = &adapter->rx_buf[adapter->rx_pos];
      size_t remaining = adapter->rx_len - adapter->rx_pos;
      iohdlc_mutex_unlock(&adapter->rx_lock);
      
      /* Read remaining bytes into the buffer (200ms timeout) */
      ssize_t result = mock_stream_read(adapter->stream, target, remaining, 200);
      
      if (result >= 1) {
        /* Bytes received, update position */
        iohdlc_mutex_lock(&adapter->rx_lock);
        if (adapter->rx_buf && adapter->rx_pos < adapter->rx_len) {
          adapter->rx_pos += (size_t)result;
          
          /* Notify when request is complete */
          if (adapter->rx_pos >= adapter->rx_len) {
            if (adapter->callbacks.on_rx) {
              adapter->callbacks.on_rx(adapter->callbacks.cb_ctx, 0);
            }
          }
        }
        iohdlc_mutex_unlock(&adapter->rx_lock);
      } else if (result < 0) {
        /* Error - notify with lock held */
        iohdlc_mutex_lock(&adapter->rx_lock);
        if (adapter->callbacks.on_rx_error) {
          adapter->callbacks.on_rx_error(adapter->callbacks.cb_ctx, IOHDLC_STREAM_ERR_OVERRUN);
        }
        iohdlc_mutex_unlock(&adapter->rx_lock);
      }
      /* else timeout (result == 0) - continue accumulating */
    } else {
      iohdlc_mutex_unlock(&adapter->rx_lock);
      ioHdlc_sleep_ms(1);  /* Wait 1ms before checking again */
    }
  }
  
  return NULL;
}
