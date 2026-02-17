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
 * @file    mock_stream_adapter.h
 * @brief   Adapter to connect mock_stream to ioHdlcstream interface (unified OSAL version).
 */

#ifndef MOCK_STREAM_ADAPTER_H
#define MOCK_STREAM_ADAPTER_H

#include "mock_stream.h"
#include "ioHdlcstreamport.h"
#include "ioHdlcosal.h"

/**
 * @brief   Mock stream adapter structure (OSAL-based).
 */
typedef struct {
  mock_stream_t *stream;
  ioHdlcStreamCallbacks callbacks;
  iohdlc_thread_t *rx_thread;
  bool running;
  bool thread_started;
  
  /* RX buffer management */
  iohdlc_mutex_t rx_lock;
  uint8_t *rx_buf;
  size_t rx_len;
  size_t rx_pos;
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
