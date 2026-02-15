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
 * @file    mock_stream_adapter_chibios.c
 * @brief   Mock stream adapter for ChibiOS.
 * @details Implements async RX thread to match Linux behavior.
 */

#include "mock_stream_adapter.h"
#include "../../../include/ioHdlcstreamport.h"
#include "../../../os/chibios/include/ioHdlcosal.h"
#include <stdlib.h>
#include <string.h>

/* ChibiOS doesn't have ssize_t */
typedef int ssize_t;

/*===========================================================================*/
/* Forward declarations                                                      */
/*===========================================================================*/

static THD_FUNCTION(adapter_rx_thread, arg);

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

  mock_stream_adapter_t *adapter = chHeapAlloc(NULL, sizeof(*adapter));
  if (!adapter) {
    return NULL;
  }

  memset(adapter, 0, sizeof(*adapter));
  adapter->stream = stream;
  adapter->running = false;
  adapter->thread_started = false;
  chMtxObjectInit(&adapter->rx_lock);
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
    /* Wait for thread to exit */
    chThdWait(adapter->rx_thread);
    adapter->thread_started = false;
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
  
  if (!adapter || adapter->running) {
    return;
  }

  /* Save callbacks */
  if (cbs) {
    adapter->callbacks = *cbs;
  }

  /* Start RX thread */
  adapter->running = true;
  adapter->rx_thread = chThdCreateFromHeap(NULL, 2048, "mock_rx", NORMALPRIO, 
                                            adapter_rx_thread, adapter);
  if (adapter->rx_thread) {
    adapter->thread_started = true;
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
    chThdWait(adapter->rx_thread);
    adapter->thread_started = false;
  }
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
  
  if (!adapter || !ptr || len == 0) {
    return false;
  }

  /* Register buffer for async RX */
  chMtxLock(&adapter->rx_lock);
  adapter->rx_buf = ptr;
  adapter->rx_len = len;
  adapter->rx_pos = 0;
  chMtxUnlock(&adapter->rx_lock);
  
  return true;
}

static void port_rx_cancel(void *ctx) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)ctx;
  
  if (!adapter) {
    return;
  }

  /* Clear RX buffer registration */
  chMtxLock(&adapter->rx_lock);
  adapter->rx_buf = NULL;
  adapter->rx_len = 0;
  adapter->rx_pos = 0;
  chMtxUnlock(&adapter->rx_lock);
}

/*===========================================================================*/
/* RX background thread                                                      */
/*===========================================================================*/

static THD_FUNCTION(adapter_rx_thread, arg) {
  mock_stream_adapter_t *adapter = (mock_stream_adapter_t *)arg;

  while (adapter->running) {
    chMtxLock(&adapter->rx_lock);
    
    /* Check if we have a buffer to fill */
    if (adapter->rx_buf && adapter->rx_pos < adapter->rx_len) {
      uint8_t *target = &adapter->rx_buf[adapter->rx_pos];
      size_t remaining = adapter->rx_len - adapter->rx_pos;
      chMtxUnlock(&adapter->rx_lock);
      
      /* Read remaining bytes into the buffer (200ms timeout) */
      size_t result = mock_stream_read(adapter->stream, target, remaining, TIME_MS2I(200));
      
      if (result >= 1) {
        /* Bytes received, update position */
        chMtxLock(&adapter->rx_lock);
        if (adapter->rx_buf && adapter->rx_pos < adapter->rx_len) {
          adapter->rx_pos += result;
          
          /* Notify when request is complete */
          if (adapter->rx_pos >= adapter->rx_len) {
            if (adapter->callbacks.on_rx) {
              adapter->callbacks.on_rx(adapter->callbacks.cb_ctx, 0);
            }
          }
        }
        chMtxUnlock(&adapter->rx_lock);
      }
      /* else timeout or error - continue accumulating */
    } else {
      chMtxUnlock(&adapter->rx_lock);
      ioHdlc_sleep_ms(1);  /* Wait 1ms before checking again */
    }
  }
}
