/**
 * @file    mock_stream_chibios.h
 * @brief   Mock stream implementation for ChibiOS testing.
 *
 * @details Provides a memory-based bidirectional stream that simulates
 *          a serial connection using ChibiOS primitives (MemoryStream,
 *          threads, mutexes). Supports cross-connection between two
 *          mock streams for testing HDLC protocol interactions.
 */

#ifndef MOCK_STREAM_CHIBIOS_H
#define MOCK_STREAM_CHIBIOS_H

#include "ch.h"
#include "hal.h"
#include "memstreams.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*===========================================================================*/
/* Mock Stream Types                                                         */
/*===========================================================================*/

#define MOCK_STREAM_BUFFER_SIZE 4096

/**
 * @brief   Mock stream configuration.
 */
typedef struct {
  bool loopback;          /**< TX data automatically fed to RX */
  bool inject_errors;     /**< Randomly corrupt data (not implemented on ChibiOS) */
  uint32_t error_rate;    /**< Error probability (0-1000 = 0-100%, not implemented) */
  uint32_t delay_us;      /**< Simulated transmission delay in microseconds */
} mock_stream_config_t;

/**
 * @brief   Circular buffer for stream data.
 */
typedef struct {
  uint8_t data[MOCK_STREAM_BUFFER_SIZE];
  size_t head;
  size_t tail;
  size_t count;
  mutex_t lock;
  condition_variable_t not_empty;
  condition_variable_t not_full;
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
  mutex_t state_lock;
} mock_stream_t;

/*===========================================================================*/
/* Mock Stream API                                                           */
/*===========================================================================*/

/**
 * @brief   Create a new mock stream (allocates memory).
 * @param[in] config    Configuration (NULL for defaults)
 * @return              Pointer to mock stream, or NULL on error
 * @note    ChibiOS version: uses chHeapAlloc for allocation
 */
mock_stream_t* mock_stream_create(const mock_stream_config_t *config);

/**
 * @brief   Destroy a mock stream (frees memory).
 * @param[in] stream    Mock stream to destroy
 * @note    ChibiOS version: uses chHeapFree for deallocation
 */
void mock_stream_destroy(mock_stream_t *stream);

/**
 * @brief   Initialize a mock stream (static allocation).
 * @param[in] stream    Pointer to mock stream structure
 * @param[in] config    Configuration (NULL for defaults)
 */
void mock_stream_init(mock_stream_t *stream, const mock_stream_config_t *config);

/**
 * @brief   Deinitialize a mock stream (static allocation).
 * @param[in] stream    Mock stream to deinitialize
 */
void mock_stream_deinit(mock_stream_t *stream);

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
 * @param[in] timeout_ms Timeout in milliseconds (TIME_INFINITE for infinite)
 * @return              Number of bytes read
 */
size_t mock_stream_read(mock_stream_t *stream, uint8_t *buf, size_t size, sysinterval_t timeout_ms);

/**
 * @brief   Write data to mock stream.
 * @param[in] stream    Mock stream
 * @param[in] buf       Data to write
 * @param[in] size      Number of bytes to write
 * @param[in] timeout_ms Timeout in milliseconds (TIME_INFINITE for infinite)
 * @return              Number of bytes written
 */
size_t mock_stream_write(mock_stream_t *stream, const uint8_t *buf, size_t size, sysinterval_t timeout_ms);

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

#endif /* MOCK_STREAM_CHIBIOS_H */
