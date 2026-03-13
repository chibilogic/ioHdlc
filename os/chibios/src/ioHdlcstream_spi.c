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
 * @file    ioHdlcstream_spi.c
 * @brief   ChibiOS adapter for the OS-agnostic stream interface (SPI backend).
 *
 * @details Typical integration:
 *          - Prepare a @p SPIConfig and a @p SPIDriver (e.g. @p &SPID1).
 *          - Initialise the ChibiOS SPI adapter and bind the abstract port:
 *              @code
 *              static ioHdlcStreamChibiosSpi spi_obj;
 *              static ioHdlcStreamPort       port;
 *              ioHdlcStreamPortChibiosSpiObjectInit(&port, &spi_obj,
 *                                                  &SPID1, &spicfg,
 *                                                  true);
 *              @endcode
 *
 *          Operational notes:
 *          - TX and RX DMA operations are mutually exclusive.  The swdriver
 *            guarantees half-duplex ordering via TWA mode.
 *          - @p tx_submit() preempts a running RX: it calls
 *            @p spiStopTransferI() and restarts from TX.  The partial RX
 *            buffer is silently discarded; FCS mismatch in @p drv_recv_frame
 *            will discard the incomplete frame cleanly.
 *          - After TX completes (@p txend2), if @p rx_ptr is still armed and
 *            no new TX was triggered by @p on_tx_done, RX is restarted
 *            automatically.
 *          - The @p SPIConfig::end_cb field is overwritten at @p start time;
 *            do not set it in the caller-provided @p cfgp.
 *          - REJ must be disabled in the ioHdlc core config for SPI
 *            connections.  Use checkpoint retransmission for recovery.
 */

#include "ioHdlcstream_spi.h"

/*===========================================================================*/
/* Forward declarations.                                                     */
/*===========================================================================*/

static void chb_spi_data_cb(SPIDriver *spip);
static void chb_spi_error_cb(SPIDriver *spip);

/*===========================================================================*/
/* Local callback implementations.                                           */
/*===========================================================================*/

/**
 * @brief   Data callback (SPI v2 @p data_cb): transfer completed without errors.
 * @details Dispatches to the TX or RX handler based on which transfer was
 *          active.  Called from ISR context.
 */
static void chb_spi_data_cb(SPIDriver *spip) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)spip->ip;
  if (!ctx) return;

  if (ctx->tx_active) {
    /* ---- TX finished ---------------------------------------------------- */
    void *framep = ctx->tx_framep;
    ctx->tx_active  = false;
    ctx->tx_framep  = NULL;

    chDbgAssert(ctx->cbs && ctx->cbs->on_tx_done,
                "spi end_cb: on_tx_done not set");

    /* Notify the swdriver.  on_tx_done() may synchronously call tx_submit()
     * for the next queued frame, setting tx_active = true again. */
    ctx->cbs->on_tx_done(ctx->cbs->cb_ctx, framep);

    /* If on_tx_done did NOT enqueue a new TX and an RX buffer is ready,
     * restart receive. */
    if (!ctx->tx_active && ctx->rx_ptr != NULL) {
      ctx->rx_active = true;
      chSysLockFromISR();
      spiStartReceiveI(ctx->spip, ctx->rx_n, ctx->rx_ptr);
      chSysUnlockFromISR();
    }

  } else if (ctx->rx_active) {
    /* ---- RX finished ---------------------------------------------------- */
    ctx->rx_active = false;
    ctx->rx_ptr    = NULL;
    ctx->rx_n      = 0;

    chDbgAssert(ctx->cbs && ctx->cbs->on_rx,
                "spi data_cb: on_rx not set");

    ctx->cbs->on_rx(ctx->cbs->cb_ctx, 0);
  }
}

/**
 * @brief   Error callback (SPI v2 @p error_cb): DMA error during transfer.
 * @details Resets the in-flight state and notifies the swdriver via
 *          @p on_rx_error so it can discard any partial frame.
 */
static void chb_spi_error_cb(SPIDriver *spip) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)spip->ip;
  if (!ctx) return;

  /* Reset both state machines — the DMA transfer was aborted by the LLD. */
  ctx->tx_active = false;
  ctx->tx_framep = NULL;
  ctx->rx_active = false;
  ctx->rx_ptr    = NULL;
  ctx->rx_n      = 0;

  if (ctx->cbs && ctx->cbs->on_rx_error) {
    ctx->cbs->on_rx_error(ctx->cbs->cb_ctx, IOHDLC_STREAM_ERR_OVERRUN);
  }
}

/*===========================================================================*/
/* Port ops implementation.                                                  */
/*===========================================================================*/

static void chb_spi_start(void *vctx, const ioHdlcStreamCallbacks *cbs) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;

  chDbgAssert(cbs && cbs->on_rx && cbs->on_tx_done,
              "spi start: invalid callbacks");

  ctx->cbs       = cbs;
  ctx->tx_framep = NULL;
  ctx->tx_active = false;
  ctx->rx_ptr    = NULL;
  ctx->rx_n      = 0;
  ctx->rx_active = false;

  /* Install callbacks, slave flag, and bind context pointer. */
  ctx->spip->ip = ctx;
  if (ctx->cfgp) {
    ctx->cfgp->data_cb  = chb_spi_data_cb;
    ctx->cfgp->error_cb = chb_spi_error_cb;
    ctx->cfgp->slave    = !ctx->is_master;
  }
  spiStart(ctx->spip, ctx->cfgp);
}

static void chb_spi_stop(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  spiStop(ctx->spip);
  ctx->tx_active = false;
  ctx->rx_active = false;
  ctx->rx_ptr    = NULL;
  ctx->rx_n      = 0;
}

/**
 * @brief   Submit a TX buffer.
 * @details If an RX is in progress it is aborted first (TX preempts RX).
 *          The partial receive is discarded silently; FCS checking in
 *          @p drv_recv_frame will reject the incomplete frame.
 */
static bool chb_spi_tx_submit(void *vctx, const uint8_t *ptr, size_t len,
                               void *cookie) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;

  chDbgAssert(!ctx->tx_active, "spi tx_submit: tx already active");

  if (ctx->rx_active) {
    spiStopTransferI(ctx->spip, NULL);
    ctx->rx_active = false;
    /* rx_ptr/rx_n are left as-is so RX can be re-armed after TX completes. */
  }

  ctx->tx_framep = cookie;
  ctx->tx_active = true;
  spiStartSendI(ctx->spip, len, ptr);
  return true;
}

static bool chb_spi_tx_busy(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  return ctx->tx_active;
}

/**
 * @brief   Arm an RX buffer.
 * @details If TX is currently active the buffer is saved and RX DMA will be
 *          launched automatically at the end of the TX transfer.  If TX is
 *          idle, DMA is started immediately.
 */
static bool chb_spi_rx_submit(void *vctx, uint8_t *ptr, size_t len) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;

  /* Save the armed buffer (also used as "pending" signal in txend2). */
  ctx->rx_ptr = ptr;
  ctx->rx_n   = len;

  if (!ctx->tx_active) {
    ctx->rx_active = true;
    spiStartReceiveI(ctx->spip, len, ptr);
  }
  /* else: DMA will be started by chb_spi_end_cb after TX completes. */

  return true;
}

static void chb_spi_rx_cancel(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  if (ctx->rx_active) {
    spiStopTransferI(ctx->spip, NULL);
    ctx->rx_active = false;
  }
  ctx->rx_ptr = NULL;
  ctx->rx_n   = 0;
}

static const ioHdlcStreamPortOps chibios_spi_ops = {
  .start     = chb_spi_start,
  .stop      = chb_spi_stop,
  .tx_submit = chb_spi_tx_submit,
  .tx_busy   = chb_spi_tx_busy,
  .rx_submit = chb_spi_rx_submit,
  .rx_cancel = chb_spi_rx_cancel,
};

/*===========================================================================*/
/* Exported helper API.                                                      */
/*===========================================================================*/

/**
 * @brief   Initialises a ChibiOS SPI port object.
 *
 * @param[out] port       destination port handle to be bound to this object
 * @param[out] obj        object storage provided by the caller
 * @param[in]  spip       ChibiOS SPI driver instance
 * @param[in]  cfgp       SPI configuration (end_cb will be set at start time)
 * @param[in]  is_master  true if this node drives the SPI clock
 */
void ioHdlcStreamPortChibiosSpiObjectInit(ioHdlcStreamPort       *port,
                                          ioHdlcStreamChibiosSpi *obj,
                                          SPIDriver              *spip,
                                          SPIConfig              *cfgp,
                                          bool                    is_master) {
  obj->spip      = spip;
  obj->cfgp      = cfgp;
  obj->is_master = is_master;
  obj->cbs       = NULL;
  obj->tx_framep = NULL;
  obj->tx_active = false;
  obj->rx_ptr    = NULL;
  obj->rx_n      = 0;
  obj->rx_active = false;

  port->ctx = obj;
  port->ops = &chibios_spi_ops;
}
