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
 * @file    include/ioHdlcstreamport.h
 * @brief   Transport abstraction layer for byte-stream HDLC drivers.
 * @details Defines the HAL interface for various transports (UART, SPI, I2C, Mock, etc).
 *          This is the lowest layer that adapts hardware-specific byte streams
 *          to a uniform interface. Backends implementing this interface must
 *          document callback context, ownership of submitted buffers, and any
 *          transport-specific restrictions such as DMA alignment or ISR-only
 *          semantics.
 *
 *          This abstraction sits below ioHdlcDriver. It is responsible for
 *          moving bytes and transport events, not for protocol semantics or
 *          frame ownership policy above the transport layer.
 *
 * @addtogroup ioHdlc_stream
 * @{
 */

#ifndef IOHDLCSTREAMPORT_H
#define IOHDLCSTREAMPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ioHdlctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Port constraint flags
 * @brief Bitmask of transport-level limitations declared by a backend.
 * @details Reported through @ref ioHdlcStreamPortOps::get_caps.
 *          The protocol core checks these during station init and link-up to
 *          reject configurations that the underlying transport cannot support.
 * @{
 */
/** Link is half-duplex: only TWA (Two-Way Alternate) is supported. */
#define IOHDLC_PORT_CONSTR_TWA_ONLY  (1u << 0)
/** Link supports NRM only: ABM (Asynchronous Balanced Mode) is not supported. */
#define IOHDLC_PORT_CONSTR_NRM_ONLY  (1u << 1)
/** @} */

/**
 * @name Stream error mask flags
 * @brief Bit flags reported by the RX error callback.
 * @{
 */
#define IOHDLC_STREAM_ERR_OVERRUN  (1u << 0)
#define IOHDLC_STREAM_ERR_FRAMING  (1u << 1)
#define IOHDLC_STREAM_ERR_PARITY   (1u << 2)
#define IOHDLC_STREAM_ERR_TMO      (1u << 3)
#define IOHDLC_STREAM_ERR_OTHER    (1u << 4)
/** @} */

/**
 * @brief   HAL/adapter -> driver callbacks.
 * @details Invoked by the transport backend when RX, TX, or error conditions
 *          occur. The exact execution context is backend-specific and must be
 *          documented by the implementation that triggers the callback.
 * @note    Backends should clearly state whether callbacks run in thread,
 *          task, interrupt, or deferred-work context.
 */
typedef void (*ioHdlcStreamOnRx)(void *cb_ctx, uint32_t errmask);
typedef void (*ioHdlcStreamOnTxDone)(void *cb_ctx, void *framep);
typedef void (*ioHdlcStreamOnRxError)(void *cb_ctx, uint32_t errmask);

/**
 * @brief   Callback bundle registered by a driver on a stream backend.
 * @details A transport implementation stores this structure during
 *          @ref ioHdlcStreamPortOps::start and later invokes the supplied
 *          callbacks to notify RX progress, TX completion, and RX errors.
 */
typedef struct ioHdlcStreamCallbacks {
  ioHdlcStreamOnRx      on_rx;      /**< RX byte ready or timeout notification */
  ioHdlcStreamOnTxDone  on_tx_done; /**< TX buffer has been fully sent */
  ioHdlcStreamOnRxError on_rx_error;/**< Stream/DMA error notification */
  void                 *cb_ctx;     /**< Opaque callback owner context passed back to all callbacks. */
} ioHdlcStreamCallbacks;

/**
 * @brief   Driver services exposed to the stream adapter.
 * @details Called synchronously by the adapter using the same @p cb_ctx value
 *          carried in @ref ioHdlcStreamCallbacks.
 */
typedef struct ioHdlcStreamDriverOps {
  int32_t (*build_tx_plan)(void *cb_ctx,
                           const iohdlc_frame_t *fp,
                           const iohdlc_tx_plan_opts_t *opts,
                           iohdlc_tx_plan_t *plan);
} ioHdlcStreamDriverOps;

/**
 * @brief Driver -> HAL/adapter operations.
 * @note  The OS-/HAL-specific adapter implementation provides these hooks.
 * @note  Ownership of RX and TX buffers submitted through this interface must
 *        be documented by the concrete backend.
 * @note  Implementations must also document whether re-arming RX or TX from a
 *        callback is supported and in which execution contexts that is valid.
 */
typedef struct ioHdlcStreamPortOps {
  /** Query transport constraints and backend execution assists. */
  const iohdlc_stream_caps_t *(*get_caps)(void *ctx);

  /** Start the adapter and bind callbacks/services. */
  void (*start)(void *ctx,
                const ioHdlcStreamCallbacks *cbs,
                const ioHdlcStreamDriverOps *drvops);
  
  /** Stop/shutdown the adapter. */
  void (*stop)(void *ctx);

  /**
   * @brief Submit a frame send request.
   * @details The adapter executes the current frame submission selected by the
   *          driver. The frame carries the per-send header snapshot in
   *          @p fp->tx_snapshot and may also carry a contiguous wire image
   *          prepared by the driver. Backends may use the frame storage
   *          directly or query @ref ioHdlcStreamDriverOps::build_tx_plan.
   *          Backends that advertise TX FCS offload may leave the FCS deferred
   *          in the plan only for sizes listed in @p get_caps()->tx_fcs_offload_sizes.
   * @return 0 on success, errno-compatible error code otherwise.
   */
  int32_t (*tx_submit_frame)(void *ctx, iohdlc_frame_t *fp);
  
  /** Query TX busy (may be NULL if not needed). */
  bool (*tx_busy)(void *ctx);

  /** Arm a RX transfer of 'len' bytes into 'ptr'. */
  bool (*rx_submit)(void *ctx, uint8_t *ptr, size_t len);
  
  /** Cancel current RX transfer if supported. */
  void (*rx_cancel)(void *ctx);
} ioHdlcStreamPortOps;

/**
 * @brief   Port handle (ops + opaque context).
 * @details Represents a transport adapter (UART, SPI, I2C, Mock, etc).
 *          The ctx pointer is opaque and specific to each adapter implementation.
 *          A valid ioHdlcStreamPort requires both a context pointer and a
 *          compatible operations table.
 * @note    The port handle itself does not own the underlying transport
 *          object unless the concrete adapter explicitly documents that model.
 */
typedef struct ioHdlcStreamPort {
  void *ctx;                          /**< Opaque adapter context */
  const ioHdlcStreamPortOps *ops;     /**< Virtual method table */
} ioHdlcStreamPort;

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSTREAMPORT_H */

/** @} */
