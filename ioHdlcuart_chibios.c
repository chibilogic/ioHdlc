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
 * @file    ioHdlcuart_chibios.c
 * @brief   ChibiOS adapter for the OS-agnostic UART DMA interface.
 * @details Typical integration:
 *          - Prepare a @p UARTConfig and a @p UARTDriver (e.g. @p &UARTD1).
 *          - Initialize the ChibiOS adapter and bind the abstract port:
 *              @code
 *              static ioHdlcUartChibios uobj;
 *              static ioHdlcUartPort    port;
 *              ioHdlcUartPortChibiosObjectInit(&port, &uobj, &UARTD1, &uartcfg);
 *              @endcode
 *          - Define the UART parameters (@p ioHdlcUartParams) and the core
 *            configuration (@p ioHdlcUartNewConfig):
 *              @li @p has_frame_format: enables length-prefixed frames (FFF)
 *              @li @p deliver_rx_frame: integrate with the caller's delivery
 *                  path (queue/handler). RX frames are taken/released via the
 *                  pool reference passed at init.
 *          - Initialize and start the generic core:
 *              @code
 *              ioHdlcUartNew core;
 *              ioHdlcUartNew_init(&core, &port, &params, &cfg, upper_ctx);
 *              ioHdlcUartNew_start(&core);
 *              @endcode
 *          - Transmission: use @p ioHdlcUartNew_send(&core, ptr, len, cookie).
 *            The @p cookie is returned in @p on_tx_done for upper-layer
 *            bookkeeping.
 *
 *          Operational notes:
 *          - The adapter assigns @p txend1_cb/@p rxend_cb/@p rxerr_cb/@p timeout_cb
 *            on @p UARTConfig.
 *          - RX timeout is propagated as an error so the core can discard/rearm
 *            a frame.
 *          - Only one RX is in-flight; the core re-arms subsequent portions of
 *            the same frame (multi-chunk) in @p on_rx_chunk, according to the
 *            expected length.
 *          - Functions use I or non-I variants depending on context
 *            (@p chSysIsInISR()).
 */


#include "ioHdlcuart_chibios.h"

/*===========================================================================*/
/* Local types.                                                              */
/*===========================================================================*/

/*===========================================================================*/
/* Local functions.                                                          */
/*===========================================================================*/

static void chb_txend_cb(UARTDriver *uartp) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)uartp->ip;
  if (ctx && ctx->cbs && ctx->cbs->on_tx_done) {
    void *cookie = ctx->tx_cookie;
    /* clear busy */
    ctx->tx_cookie = NULL;
    ctx->cbs->on_tx_done((void *)ctx->cbs->cb_ctx, cookie);
  }
}

static void chb_rxend_cb(UARTDriver *uartp) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)uartp->ip;
  if (ctx && ctx->cbs && ctx->cbs->on_rx) {
    /* RX of the armed buffer completed (usually 1 byte). */
    ctx->rx_cookie = NULL;
    ctx->cbs->on_rx((void *)ctx->cbs->cb_ctx, false);
  }
}

static void chb_rxerr_cb(UARTDriver *uartp, uartflags_t e) {
  (void)e;
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)uartp->ip;
  if (ctx && ctx->cbs && ctx->cbs->on_rx_error) {
    uint32_t mask = 0;
#if defined(UART_PARITY_ERROR)
    if (e & UART_PARITY_ERROR) mask |= IOHDLC_UART_ERR_PARITY;
#endif
#if defined(UART_FRAMING_ERROR)
    if (e & UART_FRAMING_ERROR) mask |= IOHDLC_UART_ERR_FRAMING;
#endif
#if defined(UART_OVERRUN_ERROR)
    if (e & UART_OVERRUN_ERROR) mask |= IOHDLC_UART_ERR_OVERRUN;
#endif
    ctx->cbs->on_rx_error((void *)ctx->cbs->cb_ctx, mask);
  }
}

static void chb_timeout_cb(UARTDriver *uartp) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)uartp->ip;
  if (ctx && ctx->cbs && ctx->cbs->on_rx) {
    ctx->cbs->on_rx((void *)ctx->cbs->cb_ctx, true);
  }
}

/*===========================================================================*/
/* Port ops implementation.                                                  */
/*===========================================================================*/

static void chb_start(void *vctx, const ioHdlcUartParams *p, const ioHdlcUartCallbacks *cbs) {
  (void)p; /* configuration mapping is platform-specific; assumed pre-set in cfgp */
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)vctx;

  ctx->cbs = cbs;
  ctx->tx_cookie = NULL;
  ctx->rx_cookie = NULL;

  /* Bind callbacks and start UART. */
  ctx->uartp->ip = ctx;
  if (ctx->cfgp) {
    /* Prefer txend1 for compatibility, as used in the existing driver. */
    ctx->cfgp->txend1_cb = chb_txend_cb;
    ctx->cfgp->rxend_cb  = chb_rxend_cb;
    ctx->cfgp->rxerr_cb  = chb_rxerr_cb;
    ctx->cfgp->timeout_cb = chb_timeout_cb;
  }
  uartStart(ctx->uartp, ctx->cfgp);
}

static void chb_stop(void *vctx) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)vctx;
  uartStop(ctx->uartp);
}

static bool chb_tx_submit(void *vctx, const uint8_t *ptr, size_t len, void *cookie) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)vctx;
  /* Consider busy if a cookie is pending or TX engine not idle. */
  if (ctx->tx_cookie != NULL || ctx->uartp->txstate != UART_TX_IDLE) return false;
  ctx->tx_cookie = cookie;
  if (chSysIsInISR())
    uartStartSendI(ctx->uartp, len, ptr);
  else
    uartStartSend(ctx->uartp, len, ptr);
  return true;
}

static bool chb_tx_busy(void *vctx) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)vctx;
  return ctx->uartp->txstate != UART_TX_IDLE;
}

static bool chb_rx_submit(void *vctx, uint8_t *ptr, size_t len) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)vctx;
  if (ctx->rx_cookie != NULL) return false; /* one RX at a time */
  ctx->rx_cookie = (void *)1; /* mark busy */
  if (chSysIsInISR())
    uartStartReceiveI(ctx->uartp, len, ptr);
  else
    uartStartReceive(ctx->uartp, len, ptr);
  return true;
}

static void chb_rx_cancel(void *vctx) {
  ioHdlcUartChibios *ctx = (ioHdlcUartChibios *)vctx;
  if (chSysIsInISR())
    uartStopReceiveI(ctx->uartp);
  else
    uartStopReceive(ctx->uartp);
  ctx->rx_cookie = NULL;
}

static const ioHdlcUartPortOps chibios_ops = {
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
void ioHdlcUartPortChibiosObjectInit(ioHdlcUartPort *port,
                                     ioHdlcUartChibios *obj,
                                     UARTDriver *uartp,
                                     UARTConfig *cfgp) {
  obj->uartp = uartp;
  obj->cfgp  = cfgp;
  obj->cbs   = NULL;
  obj->tx_cookie = NULL;
  obj->rx_cookie = NULL;

  port->ctx = obj;
  port->ops = &chibios_ops;
}
