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
 * @file    include/ioHdlcstreamport.h
 * @brief   Transport abstraction layer for byte-stream HDLC drivers.
 * @details Defines the HAL interface for various transports (UART, SPI, I2C, Mock, etc).
 *          This is the lowest layer that adapts hardware-specific byte streams
 *          to a uniform interface.
 */

#ifndef IOHDLCSTREAMPORT_H
#define IOHDLCSTREAMPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Stream error mask flags
 * @brief Bit flags reported by the RX error callback.
 * @{ 
 */
#define IOHDLC_STREAM_ERR_OVERRUN  (1u << 0)
#define IOHDLC_STREAM_ERR_FRAMING  (1u << 1)
#define IOHDLC_STREAM_ERR_PARITY   (1u << 2)
#define IOHDLC_STREAM_ERR_TMO      (1u << 3)
/** @} */

/**
 * @brief HAL/adapter -> driver callbacks (invoked from driver ISR/task).
 */
typedef void (*ioHdlcStreamOnRx)(void *cb_ctx, uint32_t errmask);
typedef void (*ioHdlcStreamOnTxDone)(void *cb_ctx, void *framep);
typedef void (*ioHdlcStreamOnRxError)(void *cb_ctx, uint32_t errmask);

typedef struct ioHdlcStreamCallbacks {
  ioHdlcStreamOnRx      on_rx;      /**< RX byte ready or timeout notification */
  ioHdlcStreamOnTxDone  on_tx_done; /**< TX buffer has been fully sent */
  ioHdlcStreamOnRxError on_rx_error;/**< Stream/DMA error notification */
  void                 *cb_ctx;     /**< Passed back as first argument */
} ioHdlcStreamCallbacks;

/**
 * @brief Driver -> HAL/adapter operations.
 * @details Required vs optional ops:
 *          - Required: @p start, @p rx_submit, @p tx_submit
 *          - Optional: @p tx_busy, @p rx_cancel, @p stop
 * @note  The OS-/HAL-specific adapter implementation provides these hooks.
 */
typedef struct ioHdlcStreamPortOps {
  /** Required: start the adapter and bind callbacks. */
  void (*start)(void *ctx, const ioHdlcStreamCallbacks *cbs);
  
  /** Optional: stop/shutdown the adapter. */
  void (*stop)(void *ctx);

  /** Required: submit a TX buffer (non-blocking). Returns false if busy. */
  bool (*tx_submit)(void *ctx, const uint8_t *ptr, size_t len, void *framep);
  
  /** Optional: query TX busy (may be NULL if not needed). */
  bool (*tx_busy)(void *ctx);

  /** Required: arm a RX transfer of 'len' bytes into 'ptr'. */
  bool (*rx_submit)(void *ctx, uint8_t *ptr, size_t len);
  
  /** Optional: cancel current RX transfer if supported. */
  void (*rx_cancel)(void *ctx);
} ioHdlcStreamPortOps;

/**
 * @brief Port handle (ops + opaque context).
 * @details Represents a transport adapter (UART, SPI, I2C, Mock, etc).
 *          The ctx pointer is opaque and specific to each adapter implementation.
 */
typedef struct ioHdlcStreamPort {
  void *ctx;                          /**< Opaque adapter context */
  const ioHdlcStreamPortOps *ops;     /**< Virtual method table */
} ioHdlcStreamPort;

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSTREAMPORT_H */
