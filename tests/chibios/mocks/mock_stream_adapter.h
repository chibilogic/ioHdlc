/**
 * @file    mock_stream_adapter.h
 * @brief   Adapter to connect mock_stream to ioHdlcstream interface.
 */

#ifndef MOCK_STREAM_ADAPTER_H
#define MOCK_STREAM_ADAPTER_H

#include "mock_stream_chibios.h"
#include "../../../include/ioHdlcstreamport.h"
#include "ch.h"

/**
 * @brief   Mock stream adapter structure.
 */
typedef struct {
  mock_stream_t *stream;
  ioHdlcStreamCallbacks callbacks;
  thread_t *rx_thread;
  bool running;
  bool thread_started;
} mock_stream_adapter_t;

/**
 * @brief   Create mock stream adapter.
 * @param[in] stream    Mock stream instance
 * @return              Initialized adapter
 */
mock_stream_adapter_t* mock_stream_adapter_create(mock_stream_t *stream);

/**
 * @brief   Destroy mock stream adapter.
 * @param[in] adapter   Adapter to destroy
 */
void mock_stream_adapter_destroy(mock_stream_adapter_t *adapter);

/**
 * @brief   Get ioHdlcStreamPort for adapter.
 * @param[in] adapter   Adapter instance
 * @return              Port structure for ioHdlcSwDriver transport layer
 */
ioHdlcStreamPort mock_stream_adapter_get_port(mock_stream_adapter_t *adapter);

#endif /* MOCK_STREAM_ADAPTER_H */
