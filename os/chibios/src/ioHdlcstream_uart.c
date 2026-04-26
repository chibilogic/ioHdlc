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
 * @file    ioHdlcstream_uart.c
 * @brief   ChibiOS adapter for the OS-agnostic stream interface (UART backend).
 * @details Binds a ChibiOS @p UARTDriver to @ref ioHdlcStreamPort.  The
 *          software driver owns TX ordering and provides a contiguous wire
 *          image for each submission; this adapter starts the UART transfer
 *          and reports TX/RX completion through the registered callbacks.
 */

#include "ioHdlcstream_uart.h"
#include "ioHdlcll.h"
#include "ioHdlcosal.h"
#include <errno.h>

static bool chb_tx_submit(void *vctx, const uint8_t *ptr, size_t len, void *cookie);

static void chb_txend_cb(UARTDriver *uartp) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)uartp->ip;
  iohdlc_frame_t *done_fp;

  if (!ctx) return;
  chDbgAssert(ctx->cbs && ctx->cbs->on_tx_done, "uart txend cb: callbacks not set");
  done_fp = (iohdlc_frame_t *)ctx->tx_framep;
  /* Clear the in-flight cookie before the callback may submit again. */
  ctx->tx_framep = NULL;

  ctx->cbs->on_tx_done(ctx->cbs->cb_ctx, done_fp);
}

static void chb_rxend_cb(UARTDriver *uartp) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)uartp->ip;
  if (!ctx) return;
  chDbgAssert(ctx->cbs && ctx->cbs->on_rx, "uart rxend cb: callbacks not set");
  ctx->cbs->on_rx(ctx->cbs->cb_ctx, 0);
}

static void chb_rxerr_cb(UARTDriver *uartp, uartflags_t e) {
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
  if (mask == 0U)
    mask = IOHDLC_STREAM_ERR_OTHER;
  ctx->cbs->on_rx_error(ctx->cbs->cb_ctx, mask);
}

static void chb_timeout_cb(UARTDriver *uartp) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)uartp->ip;
  if (!ctx) return;
  chDbgAssert(ctx->cbs && ctx->cbs->on_rx_error, "uart timeout cb: callbacks not set");
  ctx->cbs->on_rx_error(ctx->cbs->cb_ctx, IOHDLC_STREAM_ERR_TMO);
}

/*===========================================================================*/
/* Port ops implementation.                                                  */
/*===========================================================================*/

static const iohdlc_stream_caps_t chibios_uart_caps = {
  .constraints = 0,
  .assists = IOHDLC_PORT_AST_TX_DONE_IN_ISR |
             IOHDLC_PORT_AST_TX_NEEDS_CONTIG,
  .tx_fcs_offload_sizes = {0, 0, 0, 0},
};

static const iohdlc_stream_caps_t *chb_get_caps(void *vctx) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  return ctx->caps ? ctx->caps : &chibios_uart_caps;
}

static void chb_start(void *vctx,
                      const ioHdlcStreamCallbacks *cbs,
                      const ioHdlcStreamDriverOps *drvops) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  (void)drvops;
  /* Validate callbacks once and treat them as invariants. */
  chDbgAssert(cbs && cbs->on_rx && cbs->on_tx_done && cbs->on_rx_error,
              "uart start: invalid callbacks");
  ctx->cbs = cbs;
  ctx->tx_framep = NULL;
  /* Bind callbacks and start UART. */
  ctx->uartp->ip = ctx;
  if (ctx->cfgp) {
    /* Prefer txend1. */
    ctx->cfgp->txend1_cb = chb_txend_cb;
    ctx->cfgp->rxend_cb  = chb_rxend_cb;
    ctx->cfgp->rxerr_cb  = chb_rxerr_cb;
    ctx->cfgp->timeout_cb = chb_timeout_cb;
#ifdef USART_CR1_IDLEIE
    ctx->cfgp->cr1 = USART_CR1_IDLEIE;
#endif
#ifdef USART_CR1_FIFOEN
    //ctx->cfgp->cr1 |= USART_CR1_FIFOEN;
#endif
  }
  uartStart(ctx->uartp, ctx->cfgp);
}

static void chb_stop(void *vctx) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  uartStop(ctx->uartp);
}

static int32_t chb_tx_submit_frame(void *vctx, iohdlc_frame_t *fp) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  const uint8_t *ptr = fp->frame;
  size_t len = (size_t)fp->elen + ioHdlc_txs_get_trailer_len(&fp->tx_snapshot);

  chDbgAssert(ctx != NULL, "uart tx_submit: null ctx");
  chDbgAssert(fp != NULL, "uart tx_submit: null frame");
  chDbgAssert(ctx->cbs != NULL, "uart tx_submit: callbacks not set");

  if (ctx->tx_framep != NULL || ctx->uartp->txstate == UART_TX_ACTIVE)
    return EAGAIN;

  if (fp->openingflag == IOHDLC_FLAG) {
    ptr = &fp->openingflag;
    len += 1U;
  }

  /* UART consumes a contiguous wire image prepared by the swdriver. */
  return chb_tx_submit(vctx, ptr, len, fp) ? 0 : EIO;
}

static bool chb_tx_submit(void *vctx, const uint8_t *ptr, size_t len, void *cookie) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;

  chDbgAssert(ctx != NULL, "uart tx_submit: null ctx");
  chDbgAssert(ptr != NULL, "uart tx_submit: null ptr");
  chDbgAssert(len > 0U, "uart tx_submit: zero length");
  chDbgAssert(ctx->tx_framep == NULL && ctx->uartp->txstate != UART_TX_ACTIVE,
              "uart tx_submit: tx is busy");
  ctx->tx_framep = cookie;
  uartStartSendI(ctx->uartp, len, ptr);
  return true;
}

static bool chb_tx_busy(void *vctx) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  chDbgAssert(ctx != NULL, "uart tx_busy: null ctx");
  return ctx->uartp->txstate != UART_TX_IDLE;
}

static bool chb_rx_submit(void *vctx, uint8_t *ptr, size_t len) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  chDbgAssert(ctx != NULL, "uart rx_submit: null ctx");
  chDbgAssert(ptr != NULL, "uart rx_submit: null ptr");
  chDbgAssert(len > 0U, "uart rx_submit: zero length");
  if (ctx->uartp->rxstate == UART_RX_ACTIVE) {
    return false; /* one RX at a time */
  }
  uartStartReceiveI(ctx->uartp, len, ptr);
  return true;
}

static void chb_rx_cancel(void *vctx) {
  ioHdlcStreamChibiosUart *ctx = (ioHdlcStreamChibiosUart *)vctx;
  chDbgAssert(ctx != NULL, "uart rx_cancel: null ctx");
  uartStopReceiveI(ctx->uartp);
}

static const ioHdlcStreamPortOps chibios_ops = {
  .get_caps  = chb_get_caps,
  .start     = chb_start,
  .stop      = chb_stop,
  .tx_submit_frame = chb_tx_submit_frame,
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
  obj->caps = &chibios_uart_caps;
  obj->tx_framep = NULL;

  port->ctx = obj;
  port->ops = &chibios_ops;
}
