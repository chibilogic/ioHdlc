/*
    ioHdlc - Copyright (C) 2024 Isidoro Orabona

    GNU General Public License Usage

    ioHdlc software is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ioHdlc software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with ioHdlc software.  If not, see <http://www.gnu.org/licenses/>.

    Commercial License Usage

    Licensees holding valid commercial ioHdlc licenses may use this file in
    accordance with the commercial license agreement provided in accordance with
    the terms contained in a written agreement between you and Isidoro Orabona.
    For further information contact via email on github account.
*/

/**
 * @file    include/ioHdlcstream.h
 * @brief   OS-agnostic byte-stream core for frame-oriented HDLC drivers.
 * @details This header defines a small abstraction layer ("port ops") and a
 *          core helper that orchestrates multi-chunk RX on the same frame
 *          (read header, derive total length, then read the remaining bytes),
 *          and notifies the upper layer when a frame is complete.
 */

#ifndef IOHDLCSTREAM_H
#define IOHDLCSTREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"

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

/* Forward declaration. */
struct ioHdlcStream;

/**
 * @brief HAL/adapter -> core callbacks (invoked from driver ISR/task).
 */
typedef void (*ioHdlcStreamOnRx)(void *cb_ctx, uint32_t errmask);
typedef void (*ioHdlcStreamOnTxDone)(void *cb_ctx, void *framep);
typedef void (*ioHdlcStreamOnRxError)(void *cb_ctx, uint32_t errmask);

typedef struct ioHdlcStreamCallbacks {
  ioHdlcStreamOnRx      on_rx;      /* RX byte ready or timeout notification */
  ioHdlcStreamOnTxDone  on_tx_done; /* TX buffer has been fully sent */
  ioHdlcStreamOnRxError on_rx_error;/* Stream/DMA error notification */
  void                 *cb_ctx;     /* passed back as first argument */
} ioHdlcStreamCallbacks;

/**
 * @brief Core -> HAL/adapter operations.
 * @details Required vs optional ops:
 *          - Required: @p start, @p rx_submit, @p tx_submit
 *          - Optional: @p tx_busy, @p rx_cancel, @p stop
 * @note  The OS-/HAL-specific implementation provides these hooks.
 */
typedef struct ioHdlcStreamPortOps {
  /* Required: start the adapter and bind callbacks. */
  void (*start)(void *ctx, const ioHdlcStreamCallbacks *cbs);
  /* Optional: stop/shutdown the adapter. */
  void (*stop)(void *ctx);

  /* Required: submit a TX buffer (non-blocking). Returns false if busy. */
  bool (*tx_submit)(void *ctx, const uint8_t *ptr, size_t len, void *framep);
  /* Optional: query TX busy (may be NULL if not needed). */
  bool (*tx_busy)(void *ctx);

  /* Required: arm a RX transfer of 'len' bytes into 'ptr'. */
  bool (*rx_submit)(void *ctx, uint8_t *ptr, size_t len);
  /* Optional: cancel current RX transfer if supported. */
  void (*rx_cancel)(void *ctx);
} ioHdlcStreamPortOps;

/**
 * @brief Port handle (ops + opaque context).
 */
typedef struct ioHdlcStreamPort {
  void *ctx;
  const ioHdlcStreamPortOps *ops;
} ioHdlcStreamPort;

/**
 * @brief Deliver a completed RX frame (exact length @p len), identified by @p framep.
 */
typedef void (*ioHdlcStream_DeliverRxFrame)(void *upper_ctx,
                                             void *framep,
                                             size_t len);

typedef struct ioHdlcStreamConfig {
  /* Parsing options (mirrors legacy driver flags): */
  bool has_frame_format;  /* true -> first byte can be length (FFF) */
  bool apply_transparency;/* true -> octet transparency applied on TX; RX decode is done later */

  /* Upper integration: delivery callback (ownership passes to upper). */
  ioHdlcStream_DeliverRxFrame deliver_rx_frame;    /* mandatory */
} ioHdlcStreamConfig;

/**
 * @brief Core stream helper object.
 */
typedef struct ioHdlcStream {
  /* Port and parameters */
  ioHdlcStreamPort    port;

  /* Upper integration */
  void                    *upper_ctx;
  ioHdlcStreamConfig       cfg;
  ioHdlcFramePool         *pool;        /* frame pool used for RX allocation */

  /* HAL callbacks wrapper storage */
  ioHdlcStreamCallbacks    hal_cbs;

  /* RX state (single in-flight frame) */
  uint8_t           rx_flagoctet;  /* staging octet while searching for start FLAG */
  iohdlc_frame_t   *rx_in_frame;   /* current frame being filled, NULL if idle */

  bool     started;
} ioHdlcStream;

/**
 * @name Lifecycle
 * @{ 
 */
bool ioHdlcStream_init(ioHdlcStream              *d,
                       const ioHdlcStreamPort   *port,
                       const ioHdlcStreamConfig *cfg,
                       ioHdlcFramePool          *pool,
                       void                     *upper_ctx);

bool ioHdlcStream_start(ioHdlcStream *d);
void ioHdlcStream_stop(ioHdlcStream *d);
/** @} */

/**
 * @brief Submit a TX frame buffer to be sent.
 * @note  @p framep will be echoed in @p on_tx_done.
 */
bool ioHdlcStream_send(ioHdlcStream *d, const uint8_t *ptr, size_t len, void *framep);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSTREAM_H */
