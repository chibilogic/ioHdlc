/**
 * @file    mock_stream_adapter.h
 * @brief   Adapter to connect mock_stream to ioHdlcstream interface.
 */

#ifndef MOCK_STREAM_ADAPTER_H
#define MOCK_STREAM_ADAPTER_H

#include "mock_stream.h"
#include "../../../include/ioHdlcstream.h"
#include <pthread.h>

/**
 * @brief   Mock stream adapter structure.
 */
typedef struct {
  mock_stream_t *stream;
  ioHdlcStreamCallbacks callbacks;
  pthread_t rx_thread;
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
 * @return              Port structure for ioHdlcStream/ioHdlcStreamDriver
 */
ioHdlcStreamPort mock_stream_adapter_get_port(mock_stream_adapter_t *adapter);

#endif /* MOCK_STREAM_ADAPTER_H */
