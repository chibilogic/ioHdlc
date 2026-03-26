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
 *            @p spiStopTransferI() and restarts from TX. If there is a partial
 *            RX buffer, it is silently discarded; FCS mismatch in
 *            @p drv_recv_frame will discard the incomplete frame cleanly.
 *          - After TX completes (@p txend2), if @p rx_ptr is still armed and
 *            no new TX was triggered by @p on_tx_done, RX is restarted
 *            automatically.
 *          - The @p SPIConfig::end_cb field is overwritten at @p start time;
 *            do not set it in the caller-provided @p cfgp.
 *          - REJ will be disabled in the ioHdlc core config because SPI
 *            use TWA. It will use checkpoint retransmission for recovery.
 */

#include "ioHdlcstream_spi.h"

/*===========================================================================*/
/* Forward declarations.                                                     */
/*===========================================================================*/

static void chb_spi_data_cb(SPIDriver *spip);
static void chb_spi_error_cb(SPIDriver *spip);

#include "ioHdlcswdriver.h"

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

    spiUnselectI(spip);

    /* Notify the swdriver.  on_tx_done() may synchronously call tx_submit()
     * for the next queued frame, setting tx_active = true again. */
#if defined(IOHDLC_SPI_USE_DR)
    /* Slave: deassert DATA_READY before notifying upper layer. */
    if (!ctx->is_master) {
      palClearLine(ctx->dr_line);
    }
#endif
    ctx->cbs->on_tx_done(ctx->cbs->cb_ctx, framep);

    /* If on_tx_done did NOT enqueue a new TX and an RX buffer is ready,
     * restart receive. */
    if (!ctx->tx_active && ctx->rx_ptr != NULL) {
#if defined(IOHDLC_SPI_USE_DR)
      if (ctx->is_master) {
        /* Re-arm DATA_READY interrupt; if slave has already asserted DR the
         * rising edge would not fire again — check level and start DMA now. */
        chSysLockFromISR();
        if (palReadLine(ctx->dr_line) == PAL_HIGH) {
          ctx->rx_active = true;
          spiSelectI(ctx->spip);
          spiStartReceiveI(ctx->spip, ctx->rx_n, ctx->rx_ptr);
        } else {
          ctx->dr_armed = true;
        }
        chSysUnlockFromISR();
      } else {
#endif
        ctx->rx_active = true;
        chSysLockFromISR();
#if !defined(IOHDLC_SPI_USE_DR)
        if (ctx->is_master)
          spiSelectI(ctx->spip);
#endif
        spiStartReceiveI(ctx->spip, ctx->rx_n, ctx->rx_ptr);
        chSysUnlockFromISR();
#if defined(IOHDLC_SPI_USE_DR)
      }
#endif
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

  /* Do not submit new transactions before stopping the SPI. */
  ctx->tx_active = false;
  ctx->rx_active = false;

  /* Stop any pending transactions. */
  spiStopTransfer(ctx->spip, NULL);

  /* Stop the SPI. */
  spiStop(ctx->spip);
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
    ctx->rx_active = false;
    if (ctx->is_master) spiUnselectI(ctx->spip);
    spiStopTransferI(ctx->spip, NULL);
    /* rx_ptr/rx_n are left as-is so RX can be re-armed after TX completes. */
  }
#if defined(IOHDLC_SPI_USE_DR)
  else if (ctx->is_master && ctx->rx_ptr != NULL) {
    /* DR edge may be pending — disarm the software flag before TX starts. */
    ctx->dr_armed = false;
  }
#endif

  ctx->tx_framep = cookie;
  ctx->tx_active = true;
  if (ctx->is_master) spiSelectI(ctx->spip);
  spiStartSendI(ctx->spip, len, ptr);
#if defined(IOHDLC_SPI_USE_DR)
  /* Slave: assert DATA_READY to signal the master that a frame is queued. */
  if (!ctx->is_master) {
    palSetLine(ctx->dr_line);
  }
#endif
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

#if defined(IOHDLC_SPI_USE_DR)
  if (ctx->is_master) {
    /* Master cannot start DMA without clock — wait for DATA_READY signal. */
    if (!ctx->tx_active) {
      /* If slave has already asserted DR
       * before we enabled the event, check level and start DMA immediately. */
      if (palReadLine(ctx->dr_line) == PAL_HIGH) {
        ctx->rx_active = true;
        spiSelectI(ctx->spip);
        spiStartReceiveI(ctx->spip, len, ptr);
      } else {
        ctx->dr_armed = true;
      }
    }
    /* else: data_cb will handle after TX completes. */
  } else {
#endif
    if (!ctx->tx_active) {
      ctx->rx_active = true;
      if (ctx->is_master) spiSelectI(ctx->spip);
      spiStartReceiveI(ctx->spip, len, ptr);
    }
    /* else: DMA will be started by chb_spi_data_cb after TX completes. */
#if defined(IOHDLC_SPI_USE_DR)
  }
#endif

  return true;
}

static void chb_spi_rx_cancel(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  if (ctx->is_master && ctx->rx_active) {
    spiUnselectI(ctx->spip);
    spiStopTransferI(ctx->spip, NULL);
    ctx->rx_active = false;
  }
#if defined(IOHDLC_SPI_USE_DR)
  else if (ctx->is_master && ctx->rx_ptr != NULL) {
    /* DR edge may be pending — disarm the software flag. */
    ctx->dr_armed = false;
  }
#endif
  ctx->rx_ptr = NULL;
  ctx->rx_n   = 0;
}

#if defined(IOHDLC_SPI_USE_DR)
/**
 * @brief   Called from a board-level PAL event callback when the slave
 *          DATA_READY line goes high (one-shot rising edge).
 * @details Disarms the EXTI event and starts SPI DMA receive.  If a TX is
 *          in progress the call is a no-op; data_cb will restart RX afterwards.
 * @note    Must be called from ISR context.
 */
void ioHdlcStreamSpiDataReadyI(ioHdlcStreamChibiosSpi *ctx) {
  if (!ctx->dr_armed) return;
  ctx->dr_armed = false;
  if (ctx->rx_ptr != NULL && !ctx->rx_active && !ctx->tx_active) {
    ctx->rx_active = true;
    spiSelectI(ctx->spip);
    spiStartReceiveI(ctx->spip, ctx->rx_n, ctx->rx_ptr);
  }
}
#endif

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
                                          bool                    is_master,
                                          ioline_t                dr_line) {
  obj->spip      = spip;
  obj->cfgp      = cfgp;
  obj->is_master = is_master;
  obj->dr_line   = dr_line;
  obj->dr_armed  = false;
  obj->cbs       = NULL;
  obj->tx_framep = NULL;
  obj->tx_active = is_master;
  obj->rx_ptr    = NULL;
  obj->rx_n      = 0;
  obj->rx_active = !is_master;

  port->ctx = obj;
  port->ops = &chibios_spi_ops;
}
