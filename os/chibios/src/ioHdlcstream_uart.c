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
 * @file    ioHdlcstream_uart.c
 * @brief   ChibiOS adapter for the OS-agnostic stream interface (UART backend).
 * @details Typical integration:
 *          - Prepare a @p UARTConfig and a @p UARTDriver (e.g. @p &UARTD1).
 *          - Initialize the ChibiOS adapter and bind the abstract port:
 *              @code
 *              static ioHdlcStreamChibiosUart uobj;
 *              static ioHdlcStreamPort        port;
 *              ioHdlcStreamPortChibiosUartObjectInit(&port, &uobj, &UARTD1, &uartcfg);
 *              @endcode
 *          - Transmission: use @p ioHdlcStream_send(&core, ptr, len, cookie).
 *            The @p cookie is returned in @p on_tx_done for ownership release
 *            or bookkeeping.
 *
 *          Operational notes:
 *          - The adapter assigns @p txend1_cb/@p rxend_cb/@p rxerr_cb/@p timeout_cb
 *            on @p UARTConfig.
 *          - RX timeout is propagated as a notification so the core can
 *            discard/rearm a frame as needed.
 *          - Only one RX is in-flight; the core re-arms subsequent portions of
 *            the same frame (multi-chunk) via @p on_rx according to the
 *            expected length.
 */


#include "ioHdlcstream_uart.h"

/*===========================================================================*/
/* Local types.                                                              */
/*===========================================================================*/

/*===========================================================================*/
/* Local functions.                                                          */
/*===========================================================================*/

static void chb_txend_cb(UARTDriver *uartp) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)uartp->ip;
  if (!ctx) return;
  chDbgAssert(ctx->cbs && ctx->cbs->on_tx_done, "uart txend cb: callbacks not set");
  void *framep = ctx->tx_framep;
  /* clear busy */
  ctx->tx_framep = NULL;
  ctx->cbs->on_tx_done((void *)ctx->cbs->cb_ctx, framep);
}

static void chb_rxend_cb(UARTDriver *uartp) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)uartp->ip;
  if (!ctx) return;
  chDbgAssert(ctx->cbs && ctx->cbs->on_rx, "uart rxend cb: callbacks not set");
  /* RX of the armed buffer completed (usually 1 byte). */
  ctx->rx_busy = false;
  ctx->cbs->on_rx((void *)ctx->cbs->cb_ctx, 0);
}

static void chb_rxerr_cb(UARTDriver *uartp, uartflags_t e) {
  (void)e;
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)uartp->ip;
  if (!ctx) return;
  chDbgAssert(ctx->cbs && ctx->cbs->on_rx_error, "uart rxerr cb: callbacks not set");
  uint32_t mask = 0;
#if defined(UART_PARITY_ERROR)
  if (e & UART_PARITY_ERROR) mask |= IOHDLC_STREAM_ERR_PARITY;
#endif
#if defined(UART_FRAMING_ERROR)
  if (e & UART_FRAMING_ERROR) mask |= IOHDLC_STREAM_ERR_FRAMING;
#endif
#if defined(UART_OVERRUN_ERROR)
  if (e & UART_OVERRUN_ERROR) mask |= IOHDLC_STREAM_ERR_OVERRUN;
#endif
  ctx->cbs->on_rx_error((void *)ctx->cbs->cb_ctx, mask);
}

static void chb_timeout_cb(UARTDriver *uartp) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)uartp->ip;
  if (!ctx) return;
  chDbgAssert(ctx->cbs && ctx->cbs->on_rx_error, "uart timeout cb: callbacks not set");
  ctx->cbs->on_rx_error((void *)ctx->cbs->cb_ctx, IOHDLC_STREAM_ERR_TMO);
}

/*===========================================================================*/
/* Port ops implementation.                                                  */
/*===========================================================================*/

static void chb_start(void *vctx, const ioHdlcStreamCallbacks *cbs) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  /* Validate callbacks once and treat them as invariants. */
  chDbgAssert(cbs && cbs->on_rx && cbs->on_tx_done && cbs->on_rx_error,
              "uart start: invalid callbacks");
  ctx->cbs = cbs;
  ctx->tx_framep = NULL;
  ctx->rx_busy = false;

  /* Bind callbacks and start UART. */
  ctx->uartp->ip = ctx;
  if (ctx->cfgp) {
    /* Prefer txend1. */
    ctx->cfgp->txend1_cb = chb_txend_cb;
    ctx->cfgp->rxend_cb  = chb_rxend_cb;
    ctx->cfgp->rxerr_cb  = chb_rxerr_cb;
    ctx->cfgp->timeout_cb = chb_timeout_cb;
  }
  uartStart(ctx->uartp, ctx->cfgp);
}

static void chb_stop(void *vctx) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  uartStop(ctx->uartp);
}

static bool chb_tx_submit(void *vctx, const uint8_t *ptr, size_t len, void *cookie) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  /* Consider busy if a frame is pending or TX engine not idle. */
  if (ctx->tx_framep != NULL || ctx->uartp->txstate != UART_TX_IDLE) return false;
  ctx->tx_framep = cookie;
  if (port_is_isr_context())
    uartStartSendI(ctx->uartp, len, ptr);
  else
    uartStartSend(ctx->uartp, len, ptr);
  return true;
}

static bool chb_tx_busy(void *vctx) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  return ctx->uartp->txstate != UART_TX_IDLE;
}

static bool chb_rx_submit(void *vctx, uint8_t *ptr, size_t len) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  if (ctx->rx_busy) return false; /* one RX at a time */
  ctx->rx_busy = true; /* mark busy */
  if (port_is_isr_context())
    uartStartReceiveI(ctx->uartp, len, ptr);
  else
    uartStartReceive(ctx->uartp, len, ptr);
  return true;
}

static void chb_rx_cancel(void *vctx) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  if (port_is_isr_context())
    uartStopReceiveI(ctx->uartp);
  else
    uartStopReceive(ctx->uartp);
  ctx->rx_busy = false;
}

static const ioHdlcStreamPortOps chibios_ops = {
  .start     = chb_start,
  .stop      = chb_stop,
  .tx_submit = chb_tx_submit,
  .tx_busy   = chb_tx_busy,
  .rx_submit = chb_rx_submit,
  .rx_cancel = chb_rx_cancel,
};

/*===========================================================================*/
/* Exported helper API.                                                      */
/*===========================================================================*/

/**
 * @brief   Initializes a ChibiOS UART port object.
 * @param[out] port    destination port handle to be bound to this object
 * @param[out] obj     object storage provided by the caller
 * @param[in]  uartp   ChibiOS UART driver instance
 * @param[in]  cfgp    UART configuration to be used (callbacks will be set)
 */
void ioHdlcStreamPortChibiosUartObjectInit(ioHdlcStreamPort *port,
                                     ioHdlcStreamChibiosUart *obj,
                                     UARTDriver *uartp,
                                     UARTConfig *cfgp) {
  obj->uartp = uartp;
  obj->cfgp  = cfgp;
  obj->cbs   = NULL;
  obj->tx_framep = NULL;
  obj->rx_busy = false;

  port->ctx = obj;
  port->ops = &chibios_ops;
}
