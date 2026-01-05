/**
 * @file    mock_stream_chibios.c
 * @brief   Mock stream implementation for ChibiOS testing.
 */

#include "mock_stream_chibios.h"
#include <string.h>

/*===========================================================================*/
/* Circular Buffer Helpers                                                   */
/*===========================================================================*/

static void buffer_init(mock_buffer_t *buf) {
  buf->head = 0;
  buf->tail = 0;
  buf->count = 0;
  chMtxObjectInit(&buf->lock);
  chCondObjectInit(&buf->not_empty);
  chCondObjectInit(&buf->not_full);
}

static void buffer_deinit(mock_buffer_t *buf) {
  (void)buf;
  /* ChibiOS mutexes/condvars don't need explicit cleanup */
}

static size_t buffer_write(mock_buffer_t *buf, const uint8_t *data, size_t len, sysinterval_t timeout) {
  size_t written = 0;
  
  chMtxLock(&buf->lock);
  
  while (written < len) {
    /* Wait for space if buffer full */
    while (buf->count >= MOCK_STREAM_BUFFER_SIZE) {
      if (timeout == TIME_IMMEDIATE) {
        chMtxUnlock(&buf->lock);
        return written;
      }
      if (chCondWaitTimeout(&buf->not_full, timeout) == MSG_TIMEOUT) {
        chMtxUnlock(&buf->lock);
        return written;
      }
    }
    
    /* Write one byte */
    buf->data[buf->head] = data[written];
    buf->head = (buf->head + 1) % MOCK_STREAM_BUFFER_SIZE;
    buf->count++;
    written++;
    
    /* Signal readers */
    chCondSignal(&buf->not_empty);
  }
  
  chMtxUnlock(&buf->lock);
  return written;
}

static size_t buffer_read(mock_buffer_t *buf, uint8_t *data, size_t len, sysinterval_t timeout) {
  size_t read = 0;
  
  chMtxLock(&buf->lock);
  
  while (read < len) {
    /* Wait for data if buffer empty */
    while (buf->count == 0) {
      if (timeout == TIME_IMMEDIATE) {
        chMtxUnlock(&buf->lock);
        return read;
      }
      if (chCondWaitTimeout(&buf->not_empty, timeout) == MSG_TIMEOUT) {
        chMtxUnlock(&buf->lock);
        return read;
      }
    }
    
    /* Read one byte */
    data[read] = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % MOCK_STREAM_BUFFER_SIZE;
    buf->count--;
    read++;
    
    /* Signal writers */
    chCondSignal(&buf->not_full);
  }
  
  chMtxUnlock(&buf->lock);
  return read;
}

static size_t buffer_available(mock_buffer_t *buf) {
  chMtxLock(&buf->lock);
  size_t count = buf->count;
  chMtxUnlock(&buf->lock);
  return count;
}

static void buffer_clear(mock_buffer_t *buf) {
  chMtxLock(&buf->lock);
  buf->head = 0;
  buf->tail = 0;
  buf->count = 0;
  chCondSignal(&buf->not_empty);
  chCondSignal(&buf->not_full);
  chMtxUnlock(&buf->lock);
}

/*===========================================================================*/
/* Mock Stream Implementation                                                */
/*===========================================================================*/

mock_stream_t* mock_stream_create(const mock_stream_config_t *config) {
  /* Allocate memory from ChibiOS heap */
  mock_stream_t *stream = (mock_stream_t *)chHeapAlloc(NULL, sizeof(mock_stream_t));
  if (stream == NULL) {
    return NULL;
  }
  
  /* Initialize the stream */
  mock_stream_init(stream, config);
  return stream;
}

void mock_stream_destroy(mock_stream_t *stream) {
  if (stream == NULL) {
    return;
  }
  
  /* Deinitialize */
  mock_stream_deinit(stream);
  
  /* Free memory */
  chHeapFree(stream);
}

void mock_stream_init(mock_stream_t *stream, const mock_stream_config_t *config) {
  buffer_init(&stream->rx_buf);
  buffer_init(&stream->tx_buf);
  chMtxObjectInit(&stream->state_lock);
  
  if (config != NULL) {
    stream->config = *config;
  } else {
    /* Default configuration */
    stream->config.loopback = false;
    stream->config.inject_errors = false;
    stream->config.error_rate = 0;
    stream->config.delay_us = 0;
  }
  
  stream->peer = NULL;
  stream->closed = false;
}

void mock_stream_deinit(mock_stream_t *stream) {
  chMtxLock(&stream->state_lock);
  stream->closed = true;
  
  if (stream->peer != NULL) {
    mock_stream_disconnect(stream);
  }
  
  chMtxUnlock(&stream->state_lock);
  
  buffer_deinit(&stream->rx_buf);
  buffer_deinit(&stream->tx_buf);
}

void mock_stream_connect(mock_stream_t *stream_a, mock_stream_t *stream_b) {
  chMtxLock(&stream_a->state_lock);
  chMtxLock(&stream_b->state_lock);
  
  stream_a->peer = stream_b;
  stream_b->peer = stream_a;
  
  chMtxUnlock(&stream_b->state_lock);
  chMtxUnlock(&stream_a->state_lock);
}

void mock_stream_disconnect(mock_stream_t *stream) {
  chMtxLock(&stream->state_lock);
  
  if (stream->peer != NULL) {
    mock_stream_t *peer = stream->peer;
    stream->peer = NULL;
    
    chMtxLock(&peer->state_lock);
    peer->peer = NULL;
    chMtxUnlock(&peer->state_lock);
  }
  
  chMtxUnlock(&stream->state_lock);
}

size_t mock_stream_read(mock_stream_t *stream, uint8_t *buf, size_t size, sysinterval_t timeout_ms) {
  if (stream->closed) {
    return 0;
  }
  
  return buffer_read(&stream->rx_buf, buf, size, TIME_MS2I(timeout_ms));
}

size_t mock_stream_write(mock_stream_t *stream, const uint8_t *buf, size_t size, sysinterval_t timeout_ms) {
  if (stream->closed) {
    return 0;
  }
  
  /* Simulate transmission delay (convert microseconds to milliseconds) */
  if (stream->config.delay_us > 0) {
    uint32_t delay_ms = (stream->config.delay_us + 999) / 1000;  /* Round up */
    if (delay_ms > 0) {
      chThdSleepMilliseconds(delay_ms);
    }
  }
  
  /* Handle loopback or peer connection */
  chMtxLock(&stream->state_lock);
  
  size_t written;
  
  if (stream->config.loopback) {
    /* Loopback: write to own TX buffer, then copy to own RX */
    chMtxUnlock(&stream->state_lock);
    written = buffer_write(&stream->tx_buf, buf, size, TIME_MS2I(timeout_ms));
    if (written > 0) {
      chMtxLock(&stream->state_lock);
      buffer_write(&stream->rx_buf, buf, written, TIME_IMMEDIATE);
      chMtxUnlock(&stream->state_lock);
    }
  } else if (stream->peer != NULL) {
    /* Peer connection: write directly to peer's RX (skip own TX buffer) */
    mock_stream_t *peer = stream->peer;
    chMtxUnlock(&stream->state_lock);
    written = buffer_write(&peer->rx_buf, buf, size, TIME_MS2I(timeout_ms));
  } else {
    /* No loopback, no peer: write to own TX buffer only */
    chMtxUnlock(&stream->state_lock);
    written = buffer_write(&stream->tx_buf, buf, size, TIME_MS2I(timeout_ms));
  }
  
  return written;
}

size_t mock_stream_available(mock_stream_t *stream) {
  return buffer_available(&stream->rx_buf);
}

size_t mock_stream_inject_rx(mock_stream_t *stream, const uint8_t *data, size_t size) {
  return buffer_write(&stream->rx_buf, data, size, TIME_IMMEDIATE);
}

size_t mock_stream_drain_tx(mock_stream_t *stream, uint8_t *buf, size_t size) {
  return buffer_read(&stream->tx_buf, buf, size, TIME_IMMEDIATE);
}

void mock_stream_clear(mock_stream_t *stream) {
  buffer_clear(&stream->rx_buf);
  buffer_clear(&stream->tx_buf);
}
