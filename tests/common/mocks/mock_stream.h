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
 * @file    mock_stream.h
 * @brief   Mock bidirectional byte stream for testing (unified OSAL version).
 * @details Provides error injection, loopback, and peer connection modes.
 */

#ifndef MOCK_STREAM_H
#define MOCK_STREAM_H

#include "ioHdlcosal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

#if !defined(MOCK_STREAM_BUFFER_SIZE)
#define MOCK_STREAM_BUFFER_SIZE 4096
#endif

#if !defined(MOCK_STREAM_MAX_PEERS)
#define MOCK_STREAM_MAX_PEERS  3
#endif


/*===========================================================================*/
/* Types                                                                     */
/*===========================================================================*/

typedef struct mock_stream mock_stream_t;

/**
 * @brief   Error filter callback.
 * @details Called before each write to decide whether to corrupt that frame.
 * @param[in] write_count   Incremental write counter (starts at 0).
 * @param[in] data          Pointer to frame data being written.
 * @param[in] size          Size of frame data.
 * @param[in] userdata      User-provided context pointer.
 * @return                  true = corrupt this frame, false = pass through.
 */
typedef bool (*mock_stream_error_filter_t)(uint32_t write_count, 
                                           const uint8_t *data, 
                                           size_t size, 
                                           void *userdata);

/**
 * @brief   Tamper callback.
 * @details Called before each write to allow modification of frame content.
 *          The callback receives a mutable copy of the data and may modify
 *          any byte in-place. If the callback returns true (data modified),
 *          the FCS is recalculated so the modified frame remains valid.
 * @param[in] write_count   Incremental write counter (starts at 0).
 * @param[in,out] data      Mutable pointer to frame data.
 * @param[in] size          Size of frame data.
 * @param[in] userdata      User-provided context pointer.
 * @return                  true = data was modified (recalculate FCS),
 *                          false = no changes.
 */
typedef bool (*mock_stream_tamper_t)(uint32_t write_count,
                                     uint8_t *data,
                                     size_t size,
                                     void *userdata);

/**
 * @brief   Mock stream configuration.
 */
typedef struct {
  bool loopback;                            /**< Enable loopback mode. */
  bool inject_errors;                       /**< Enable error injection. */
  uint32_t error_rate;                      /**< Error rate (0-100%). */
  uint32_t delay_us;                        /**< Transmission delay (μs). */
  mock_stream_error_filter_t error_filter;  /**< Optional error filter callback. */
  void *error_userdata;                     /**< User data for error filter. */
  mock_stream_tamper_t tamper_filter;       /**< Optional tamper callback (modify content). */
  void *tamper_userdata;                    /**< User data for tamper filter. */
} mock_stream_config_t;

/**
 * @brief   Circular buffer structure (OSAL-based).
 */
typedef struct {
  uint8_t data[MOCK_STREAM_BUFFER_SIZE];
  size_t head;              /**< Write position. */
  size_t tail;              /**< Read position. */
  size_t count;             /**< Bytes in buffer. */
  iohdlc_mutex_t lock;      /**< Buffer protection. */
  iohdlc_condvar_t not_empty;  /**< Signal when data available. */
  iohdlc_condvar_t not_full;   /**< Signal when space available. */
} mock_buffer_t;

/**
 * @brief   Mock stream instance.
 */
struct mock_stream {
  mock_buffer_t rx_buf;       /**< RX circular buffer. */
  mock_buffer_t tx_buf;       /**< TX circular buffer (unused in peer mode). */
  iohdlc_mutex_t state_lock;  /**< State protection. */
  mock_stream_config_t config;/**< Configuration. */
  mock_stream_t *peers[MOCK_STREAM_MAX_PEERS]; /**< Connected peers. */
  uint8_t        peer_count;  /**< Number of connected peers. */
  bool closed;                /**< Stream closed flag. */
  uint32_t write_count;       /**< Write operation counter (for error injection). */
};

/*===========================================================================*/
/* API Functions                                                             */
/*===========================================================================*/

/**
 * @brief   Create mock stream (allocates memory).
 * @param[in] config    Optional configuration (NULL = default).
 * @return              Initialized stream, or NULL on failure.
 */
mock_stream_t* mock_stream_create(const mock_stream_config_t *config);

/**
 * @brief   Destroy mock stream (frees memory).
 * @param[in] stream    Stream to destroy.
 */
void mock_stream_destroy(mock_stream_t *stream);

/**
 * @brief   Initialize mock stream (for pre-allocated streams).
 * @details Use this for statically allocated streams instead of mock_stream_create().
 * @param[in] stream    Pre-allocated stream structure.
 * @param[in] config    Optional configuration (NULL = default).
 */
void mock_stream_init(mock_stream_t *stream, const mock_stream_config_t *config);

/**
 * @brief   Deinitialize mock stream (does not free memory).
 * @details Use this for statically allocated streams instead of mock_stream_destroy().
 * @param[in] stream    Stream to deinitialize.
 */
void mock_stream_deinit(mock_stream_t *stream);

/**
 * @brief   Connect two streams bidirectionally.
 * @details After connection, writes to stream_a appear in stream_b's RX buffer
 *          and vice versa.
 * @param[in] stream_a  First stream.
 * @param[in] stream_b  Second stream.
 */
void mock_stream_connect(mock_stream_t *stream_a, mock_stream_t *stream_b);

/**
 * @brief   Add a one-directional peer (stream's writes go to peer's RX).
 * @details For bidirectional connection, call this on both sides or use
 *          mock_stream_connect() which does it for you.
 * @param[in] stream    Source stream.
 * @param[in] peer      Peer to add as a receiver.
 */
void mock_stream_add_peer(mock_stream_t *stream, mock_stream_t *peer);

/**
 * @brief   Disconnect stream from all its peers.
 * @param[in] stream    Stream to disconnect.
 */
void mock_stream_disconnect(mock_stream_t *stream);

/**
 * @brief   Read bytes from stream.
 * @param[in]  stream      Stream to read from.
 * @param[out] buf         Destination buffer.
 * @param[in]  size        Number of bytes to read.
 * @param[in]  timeout_ms  Timeout in milliseconds (0 = non-blocking, -1 = infinite).
 * @return                 Number of bytes read, 0 on timeout, -1 on error.
 */
ssize_t mock_stream_read(mock_stream_t *stream, uint8_t *buf, size_t size, 
                         int timeout_ms);

/**
 * @brief   Write bytes to stream.
 * @param[in] stream      Stream to write to.
 * @param[in] buf         Source buffer.
 * @param[in] size        Number of bytes to write.
 * @param[in] timeout_ms  Timeout in milliseconds (0 = non-blocking, -1 = infinite).
 * @return                Number of bytes written, 0 on timeout, -1 on error.
 */
ssize_t mock_stream_write(mock_stream_t *stream, const uint8_t *buf, size_t size, 
                          int timeout_ms);

/**
 * @brief   Get number of bytes available in RX buffer.
 * @param[in] stream    Stream to query.
 * @return              Number of bytes available.
 */
size_t mock_stream_available(mock_stream_t *stream);

/**
 * @brief   Inject data directly into RX buffer (bypasses peer/loopback).
 * @param[in] stream    Target stream.
 * @param[in] data      Data to inject.
 * @param[in] size      Size of data.
 * @return              Number of bytes injected.
 */
size_t mock_stream_inject_rx(mock_stream_t *stream, const uint8_t *data, size_t size);

/**
 * @brief   Drain TX buffer (only used in non-peer, non-loopback mode).
 * @param[in]  stream   Stream to drain from.
 * @param[out] buf      Destination buffer.
 * @param[in]  size     Max bytes to drain.
 * @return              Number of bytes drained.
 */
size_t mock_stream_drain_tx(mock_stream_t *stream, uint8_t *buf, size_t size);

/**
 * @brief   Clear both RX and TX buffers.
 * @param[in] stream    Stream to clear.
 */
void mock_stream_clear(mock_stream_t *stream);

#endif /* MOCK_STREAM_H */
