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
 * @file    include/ioHdlcuart_new.h
 * @brief   OS-agnostic UART DMA adapter for frame-oriented HDLC drivers.
 * @details This header defines a small abstraction layer ("port ops") and a
 *          core helper that orchestrates multi-chunk RX on the same frame
 *          (read header, derive total length, then read the remaining bytes),
 *          and notifies the upper layer when a frame is complete.
 */

#ifndef IOHDLCUART_NEW_H
#define IOHDLCUART_NEW_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parity configuration.
 */
typedef enum ioHdlcUartParity {
  IOHDLC_UART_PARITY_NONE = 0,
  IOHDLC_UART_PARITY_EVEN = 1,
  IOHDLC_UART_PARITY_ODD  = 2,
} ioHdlcUartParity;

/**
 * @brief Stop bits configuration.
 */
typedef enum ioHdlcUartStopBits {
  IOHDLC_UART_STOP_1 = 0,
  IOHDLC_UART_STOP_2 = 1,
} ioHdlcUartStopBits;

/**
 * @brief Generic UART parameters (OS-/HAL-agnostic).
 */
typedef struct ioHdlcUartParams {
  uint32_t          baud;
  uint8_t           data_bits;   /* 7, 8, etc. */
  ioHdlcUartParity  parity;
  ioHdlcUartStopBits stop_bits;
  bool              hw_flow;     /* RTS/CTS */
} ioHdlcUartParams;

/**
 * @name UART error mask flags
 * @brief Bit flags reported by the RX error callback.
 * @{ 
 */
#define IOHDLC_UART_ERR_OVERRUN  (1u << 0)
#define IOHDLC_UART_ERR_FRAMING  (1u << 1)
#define IOHDLC_UART_ERR_PARITY   (1u << 2)
/** @} */

/* Forward declaration. */
struct ioHdlcUartNew;

/**
 * @brief HAL/adapter -> core callbacks (invoked from driver ISR/task).
 */
typedef void (*ioHdlcUartOnRx)(void *cb_ctx, bool timed_out);
typedef void (*ioHdlcUartOnTxDone)(void *cb_ctx, void *cookie);
typedef void (*ioHdlcUartOnRxError)(void *cb_ctx, uint32_t errmask);

typedef struct ioHdlcUartCallbacks {
  ioHdlcUartOnRx      on_rx;      /* RX byte ready or timeout notification */
  ioHdlcUartOnTxDone  on_tx_done;  /* TX buffer has been fully sent */
  ioHdlcUartOnRxError on_rx_error; /* UART/DMA error notification */
  void               *cb_ctx;       /* passed back as first argument */
} ioHdlcUartCallbacks;

/**
 * @brief Core -> HAL/adapter operations.
 * @note  The OS-/HAL-specific implementation provides these hooks.
 */
typedef struct ioHdlcUartPortOps {
  void (*start)(void *ctx, const ioHdlcUartParams *p, const ioHdlcUartCallbacks *cbs);
  void (*stop)(void *ctx);

  /* Submit a TX buffer (non-blocking). Returns false if busy. */
  bool (*tx_submit)(void *ctx, const uint8_t *ptr, size_t len, void *cookie);
  /* Optional: query TX busy (may be NULL if not needed). */
  bool (*tx_busy)(void *ctx);

  /* Arm a DMA RX transfer of 'len' bytes into 'ptr'. The 'cookie' */
  /* is returned verbatim in on_rx_chunk.                          */
  bool (*rx_submit)(void *ctx, uint8_t *ptr, size_t len);
  void (*rx_cancel)(void *ctx);
} ioHdlcUartPortOps;

/**
 * @brief Port handle (ops + opaque context).
 */
typedef struct ioHdlcUartPort {
  void *ctx;
  const ioHdlcUartPortOps *ops;
} ioHdlcUartPort;

/**
 * @brief Deliver a completed RX frame (exact length @p len), identified by @p cookie.
 */
typedef void (*ioHdlcUartNew_DeliverRxFrame)(void *upper_ctx,
                                             void *cookie,
                                             size_t len);

typedef struct ioHdlcUartNewConfig {
  /* Parsing options (mirrors legacy driver flags): */
  bool has_frame_format;  /* true -> first byte can be length (FFF) */
  bool apply_transparency;/* true -> octet transparency applied on TX; RX decode is done later */

  /* Upper integration: delivery callback (ownership passes to upper). */
  ioHdlcUartNew_DeliverRxFrame deliver_rx_frame;    /* mandatory */
} ioHdlcUartNewConfig;

/**
 * @brief Core UART helper object.
 */
typedef struct ioHdlcUartNew {
  /* Port and parameters */
  ioHdlcUartPort    port;
  ioHdlcUartParams  params;

  /* Upper integration */
  void                    *upper_ctx;
  ioHdlcUartNewConfig      cfg;
  ioHdlcFramePool         *pool;        /* frame pool used for RX allocation */

  /* HAL callbacks wrapper storage */
  ioHdlcUartCallbacks      hal_cbs;

  /* RX state (single in-flight frame) */
  uint8_t           rx_flagoctet;  /* staging octet while searching for start FLAG */
  iohdlc_frame_t   *rx_in_frame;   /* current frame being filled, NULL if idle */

  bool     started;
} ioHdlcUartNew;

/**
 * @name Lifecycle
 * @{ 
 */
bool ioHdlcUartNew_init(ioHdlcUartNew             *d,
                        const ioHdlcUartPort      *port,
                        const ioHdlcUartParams    *params,
                        const ioHdlcUartNewConfig *cfg,
                        ioHdlcFramePool           *pool,
                        void                      *upper_ctx);

bool ioHdlcUartNew_start(ioHdlcUartNew *d);
void ioHdlcUartNew_stop(ioHdlcUartNew *d);
/** @} */

/**
 * @brief Submit a TX frame buffer to be sent.
 * @note  @p cookie will be echoed in @p on_tx_done.
 */
bool ioHdlcUartNew_send(ioHdlcUartNew *d, const uint8_t *ptr, size_t len, void *cookie);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCUART_NEW_H */
