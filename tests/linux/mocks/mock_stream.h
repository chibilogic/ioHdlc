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
 * @file    mock_stream.h
 * @brief   Mock stream implementation for testing.
 *
 * @details Provides a memory-based bidirectional stream that simulates
 *          a serial connection. Supports error injection, loopback,
 *          and cross-connection between two mock streams.
 */

#ifndef MOCK_STREAM_H
#define MOCK_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>

/*===========================================================================*/
/* Mock Stream Types                                                         */
/*===========================================================================*/

#define MOCK_STREAM_BUFFER_SIZE 4096

/**
 * @brief   Error filter callback type.
 * @details Called for each write to decide whether to corrupt this frame.
 * @param   write_count  Sequential write counter (starts at 0)
 * @param   data         Pointer to data being written
 * @param   size         Size of data being written
 * @param   userdata     User-provided context pointer
 * @return  true to corrupt this write, false to pass it through unchanged
 */
typedef bool (*mock_stream_error_filter_t)(uint32_t write_count, const uint8_t *data, 
                                           size_t size, void *userdata);

/**
 * @brief   Mock stream configuration.
 */
typedef struct {
  bool loopback;          /**< TX data automatically fed to RX */
  bool inject_errors;     /**< Randomly corrupt data */
  uint32_t error_rate;    /**< Error probability (0-1000 = 0-100%) */
  uint32_t delay_us;      /**< Simulated transmission delay */
  mock_stream_error_filter_t error_filter;  /**< Optional: callback to selectively corrupt frames */
  void *error_userdata;   /**< Optional: userdata passed to error_filter */
} mock_stream_config_t;

/**
 * @brief   Circular buffer for stream data.
 */
typedef struct {
  uint8_t data[MOCK_STREAM_BUFFER_SIZE];
  size_t head;
  size_t tail;
  size_t count;
  pthread_mutex_t lock;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
} mock_buffer_t;

/**
 * @brief   Mock stream structure.
 */
typedef struct mock_stream {
  mock_buffer_t rx_buf;
  mock_buffer_t tx_buf;
  mock_stream_config_t config;
  struct mock_stream *peer;  /**< Connected peer stream */
  bool closed;
  uint32_t write_count;      /**< Sequential write counter for error_filter */
  pthread_mutex_t state_lock;
} mock_stream_t;

/*===========================================================================*/
/* Mock Stream API                                                           */
/*===========================================================================*/

/**
 * @brief   Create a new mock stream.
 * @param[in] config    Configuration (NULL for defaults)
 * @return              Pointer to mock stream, or NULL on error
 */
mock_stream_t* mock_stream_create(const mock_stream_config_t *config);

/**
 * @brief   Destroy a mock stream.
 * @param[in] stream    Mock stream to destroy
 */
void mock_stream_destroy(mock_stream_t *stream);

/**
 * @brief   Connect two mock streams (bidirectional).
 * @details After connection, data written to stream A appears in stream B's RX,
 *          and vice versa.
 *
 * @param[in] stream_a  First stream
 * @param[in] stream_b  Second stream
 */
void mock_stream_connect(mock_stream_t *stream_a, mock_stream_t *stream_b);

/**
 * @brief   Disconnect two streams.
 * @param[in] stream    Stream to disconnect
 */
void mock_stream_disconnect(mock_stream_t *stream);

/**
 * @brief   Read data from mock stream.
 * @param[in] stream    Mock stream
 * @param[out] buf      Buffer for received data
 * @param[in] size      Maximum bytes to read
 * @param[in] timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return              Number of bytes read, or -1 on error
 */
ssize_t mock_stream_read(mock_stream_t *stream, uint8_t *buf, size_t size, int timeout_ms);

/**
 * @brief   Write data to mock stream.
 * @param[in] stream    Mock stream
 * @param[in] buf       Data to write
 * @param[in] size      Number of bytes to write
 * @param[in] timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return              Number of bytes written, or -1 on error
 */
ssize_t mock_stream_write(mock_stream_t *stream, const uint8_t *buf, size_t size, int timeout_ms);

/**
 * @brief   Get number of bytes available in RX buffer.
 * @param[in] stream    Mock stream
 * @return              Number of bytes available to read
 */
size_t mock_stream_available(mock_stream_t *stream);

/**
 * @brief   Inject raw data directly into RX buffer.
 * @details Useful for testing specific frame scenarios.
 *
 * @param[in] stream    Mock stream
 * @param[in] data      Data to inject
 * @param[in] size      Number of bytes
 * @return              Number of bytes injected
 */
size_t mock_stream_inject_rx(mock_stream_t *stream, const uint8_t *data, size_t size);

/**
 * @brief   Drain TX buffer (retrieve transmitted data).
 * @details Useful for verifying what was transmitted.
 *
 * @param[in] stream    Mock stream
 * @param[out] buf      Buffer for transmitted data
 * @param[in] size      Maximum bytes to drain
 * @return              Number of bytes drained
 */
size_t mock_stream_drain_tx(mock_stream_t *stream, uint8_t *buf, size_t size);

/**
 * @brief   Clear all buffers.
 * @param[in] stream    Mock stream
 */
void mock_stream_clear(mock_stream_t *stream);

#endif /* MOCK_STREAM_H */
