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
 * @file    mock_stream.c
 * @brief   Mock stream implementation (unified OSAL version).
 */

#include "mock_stream.h"
#include "ioHdlcosal.h"
#include <string.h>

/* Optional logging support */
#ifdef IOHDLC_LOG_LEVEL
#include "ioHdlc_log.h"
__attribute__((weak)) void iohdlc_log_msg(iohdlc_log_dir_t a, uint8_t b, const char *c, ...) {
  (void)a; (void)b; (void)c;
}
#endif

/*===========================================================================*/
/* Buffer Operations (Platform-abstracted)                                  */
/*===========================================================================*/

static void buffer_init(mock_buffer_t *buf) {
  memset(buf->data, 0, sizeof buf->data);
  buf->head = 0;
  buf->tail = 0;
  buf->count = 0;
  iohdlc_mutex_init(&buf->lock);
  iohdlc_condvar_init(&buf->not_empty);
  iohdlc_condvar_init(&buf->not_full);
}

static void buffer_destroy(mock_buffer_t *buf) {
  iohdlc_condvar_destroy(&buf->not_empty);
  iohdlc_condvar_destroy(&buf->not_full);
  /* Note: OSAL mutexes don't have explicit destroy on ChibiOS */
}

/**
 * @brief   Write to circular buffer with timeout.
 * @param[in] buf         Buffer to write to.
 * @param[in] data        Data to write.
 * @param[in] size        Number of bytes to write.
 * @param[in] timeout_ms  Timeout in milliseconds (-1 = infinite, 0 = immediate).
 * @return                Number of bytes written (may be < size on timeout).
 */
static size_t buffer_write(mock_buffer_t *buf, const uint8_t *data, size_t size, 
                           int timeout_ms) {
  size_t written = 0;
  
  iohdlc_mutex_lock(&buf->lock);
  
  while (written < size) {
    /* Wait for space if buffer full */
    while (buf->count >= MOCK_STREAM_BUFFER_SIZE) {
      if (timeout_ms == 0) {
        iohdlc_mutex_unlock(&buf->lock);
        return written;
      }
      
      msg_t result = iohdlc_condvar_wait_timeout(&buf->not_full, &buf->lock, 
          timeout_ms);
      if (result == MSG_TIMEOUT)
        return written;
    }
    
    /* Write one byte */
    buf->data[buf->tail] = data[written];
    buf->tail = (buf->tail + 1) % MOCK_STREAM_BUFFER_SIZE;
    buf->count++;
    written++;
  }
  
  iohdlc_condvar_signal(&buf->not_empty);
  iohdlc_mutex_unlock(&buf->lock);
  
  return written;
}

/**
 * @brief   Read from circular buffer with timeout.
 * @param[in]  buf         Buffer to read from.
 * @param[out] data        Destination buffer.
 * @param[in]  size        Number of bytes to read.
 * @param[in]  timeout_ms  Timeout in milliseconds (-1 = infinite, 0 = immediate).
 * @return                 Number of bytes read (may be < size on timeout).
 */
static size_t buffer_read(mock_buffer_t *buf, uint8_t *data, size_t size, 
                          int timeout_ms) {
  size_t read_bytes = 0;
  
  iohdlc_mutex_lock(&buf->lock);
  
  /* Wait for data if buffer empty */
  while (buf->count == 0) {
    if (timeout_ms == 0) {
      iohdlc_mutex_unlock(&buf->lock);
      return 0;
    }
    
    msg_t result = iohdlc_condvar_wait_timeout(&buf->not_empty, &buf->lock, 
        timeout_ms);
    if (result == MSG_TIMEOUT)
      return 0;
  }
  
  /* Read available bytes (up to size) */
  while (read_bytes < size && buf->count > 0) {
    data[read_bytes] = buf->data[buf->head];
    buf->head = (buf->head + 1) % MOCK_STREAM_BUFFER_SIZE;
    buf->count--;
    read_bytes++;
  }
  
  iohdlc_condvar_signal(&buf->not_full);
  iohdlc_mutex_unlock(&buf->lock);
  
  return read_bytes;
}

static size_t buffer_available(mock_buffer_t *buf) {
  iohdlc_mutex_lock(&buf->lock);
  size_t count = buf->count;
  iohdlc_mutex_unlock(&buf->lock);
  return count;
}

static void buffer_clear(mock_buffer_t *buf) {
  iohdlc_mutex_lock(&buf->lock);
  buf->head = 0;
  buf->tail = 0;
  buf->count = 0;
  iohdlc_condvar_broadcast(&buf->not_empty);
  iohdlc_condvar_broadcast(&buf->not_full);
  iohdlc_mutex_unlock(&buf->lock);
}

/*===========================================================================*/
/* Mock Stream Implementation                                                */
/*===========================================================================*/

void mock_stream_init(mock_stream_t *stream, const mock_stream_config_t *config) {
  if (!stream) {
    return;
  }
  
  memset(stream, 0, sizeof(*stream));
  
  buffer_init(&stream->rx_buf);
  buffer_init(&stream->tx_buf);
  iohdlc_mutex_init(&stream->state_lock);
  
  if (config) {
    stream->config = *config;
  } else {
    /* Default configuration */
    stream->config.loopback = false;
    stream->config.inject_errors = false;
    stream->config.error_rate = 0;
    stream->config.delay_us = 0;
    stream->config.error_filter = NULL;
    stream->config.error_userdata = NULL;
  }
  
  stream->peer = NULL;
  stream->closed = false;
  stream->write_count = 0;
}

void mock_stream_deinit(mock_stream_t *stream) {
  if (!stream) {
    return;
  }
  
  mock_stream_disconnect(stream);
  
  buffer_destroy(&stream->rx_buf);
  buffer_destroy(&stream->tx_buf);
  /* Note: OSAL mutexes don't have explicit destroy on ChibiOS */
}

mock_stream_t* mock_stream_create(const mock_stream_config_t *config) {
  mock_stream_t *stream = IOHDLC_MALLOC(sizeof(mock_stream_t));
  if (!stream) {
    return NULL;
  }
  
  mock_stream_init(stream, config);
  return stream;
}

void mock_stream_destroy(mock_stream_t *stream) {
  if (!stream) {
    return;
  }
  
  mock_stream_deinit(stream);
  IOHDLC_FREE(stream);
}

void mock_stream_connect(mock_stream_t *stream_a, mock_stream_t *stream_b) {
  if (!stream_a || !stream_b) {
    return;
  }
  
  iohdlc_mutex_lock(&stream_a->state_lock);
  iohdlc_mutex_lock(&stream_b->state_lock);
  
  stream_a->peer = stream_b;
  stream_b->peer = stream_a;
  
  iohdlc_mutex_unlock(&stream_b->state_lock);
  iohdlc_mutex_unlock(&stream_a->state_lock);
}

void mock_stream_disconnect(mock_stream_t *stream) {
  if (!stream) {
    return;
  }
  
  iohdlc_mutex_lock(&stream->state_lock);
  
  if (stream->peer) {
    iohdlc_mutex_lock(&stream->peer->state_lock);
    stream->peer->peer = NULL;
    iohdlc_mutex_unlock(&stream->peer->state_lock);
    stream->peer = NULL;
  }
  
  iohdlc_mutex_unlock(&stream->state_lock);
}

ssize_t mock_stream_read(mock_stream_t *stream, uint8_t *buf, size_t size, 
                         int timeout_ms) {
  if (!stream || stream->closed) {
    return -1;
  }
  
  return (ssize_t)buffer_read(&stream->rx_buf, buf, size, timeout_ms);
}

ssize_t mock_stream_write(mock_stream_t *stream, const uint8_t *buf, size_t size, 
                          int timeout_ms) {
  if (!stream || stream->closed) {
    return -1;
  }
  
  /* Simulate delay if configured */
  if (stream->config.delay_us > 0) {
    ioHdlc_sleep_us(stream->config.delay_us);
  }
  
  iohdlc_mutex_lock(&stream->state_lock);
  
  /* Get current write count and increment for next call */
  uint32_t current_write = stream->write_count++;
  
  /* Apply error injection if configured */
  const uint8_t *data_to_send = buf;
  uint8_t *corrupted_data = NULL;
  bool should_corrupt = false;
  
  if (stream->config.inject_errors && stream->config.error_rate > 0) {
    /* If error_filter is provided, use it to decide whether to corrupt */
    if (stream->config.error_filter) {
      should_corrupt = stream->config.error_filter(current_write, buf, size, 
                                                    stream->config.error_userdata);
    } else {
      /* No filter: use pseudo-random corruption based on error_rate percentage
       * Uses write_count as entropy source for deterministic but varied behavior */
      uint32_t pseudo_random = (current_write * 1103515245u + 12345u) % 100;
      should_corrupt = pseudo_random < stream->config.error_rate;
    }
    
    if (should_corrupt) {
      corrupted_data = IOHDLC_MALLOC(size);
      if (corrupted_data) {
        memcpy(corrupted_data, buf, size);
        
#ifdef IOHDLC_LOG_LEVEL
        if (size >= 4) {
          IOHDLC_LOG_WARN(IOHDLC_LOG_RX, buf[2], "C=%02X", buf[3]);
        }
#endif
        
        /* Corrupt only the FCS (last 2 bytes before closing flag) to guarantee FCS failure.
         * For frame of size N:
         *   data[size-1] = closing flag (0x7E)
         *   data[size-2] = FCS high byte
         *   data[size-3] = FCS low byte
         */
        if (size >= 4) {
          corrupted_data[size - 3] ^= 0xFF;  /* Flip all bits in FCS low byte */
          corrupted_data[size - 2] ^= 0xFF;  /* Flip all bits in FCS high byte */
        }
        
        data_to_send = corrupted_data;
      }
    }
  }
  
  ssize_t written;
  
  /* Loopback: write to own TX buffer, then copy to own RX */
  if (stream->config.loopback) {
    iohdlc_mutex_unlock(&stream->state_lock);
    written = (ssize_t)buffer_write(&stream->tx_buf, data_to_send, size, timeout_ms);
    if (written > 0) {
      iohdlc_mutex_lock(&stream->state_lock);
      buffer_write(&stream->rx_buf, data_to_send, (size_t)written, 0);
      iohdlc_mutex_unlock(&stream->state_lock);
    }
  }
  /* Peer connection: write directly to peer's RX (skip own TX buffer) */
  else if (stream->peer) {
    mock_stream_t *peer = stream->peer;
    iohdlc_mutex_unlock(&stream->state_lock);
    written = (ssize_t)buffer_write(&peer->rx_buf, data_to_send, size, timeout_ms);
  }
  /* No loopback, no peer: write to own TX buffer only */
  else {
    iohdlc_mutex_unlock(&stream->state_lock);
    written = (ssize_t)buffer_write(&stream->tx_buf, data_to_send, size, timeout_ms);
  }
  
  /* Free corrupted buffer if allocated */
  if (corrupted_data) {
    IOHDLC_FREE(corrupted_data);
  }
  
  return written;
}

size_t mock_stream_available(mock_stream_t *stream) {
  if (!stream) {
    return 0;
  }
  
  return buffer_available(&stream->rx_buf);
}

size_t mock_stream_inject_rx(mock_stream_t *stream, const uint8_t *data, size_t size) {
  if (!stream) {
    return 0;
  }
  
  return buffer_write(&stream->rx_buf, data, size, 0);
}

size_t mock_stream_drain_tx(mock_stream_t *stream, uint8_t *buf, size_t size) {
  if (!stream) {
    return 0;
  }
  
  return buffer_read(&stream->tx_buf, buf, size, 0);
}

void mock_stream_clear(mock_stream_t *stream) {
  if (!stream) {
    return;
  }
  
  buffer_clear(&stream->rx_buf);
  buffer_clear(&stream->tx_buf);
}
