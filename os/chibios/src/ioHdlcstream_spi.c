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
 * @details Binds a ChibiOS @p SPIDriver to @ref ioHdlcStreamPort.  SPI TX and
 *          RX DMA operations are mutually exclusive; the software driver owns
 *          TX ordering and this adapter executes the selected contiguous frame
 *          submission.  Masters using DATA_READY keep RX priority while the
 *          slave is presenting data.
 */

#include "ioHdlcstream_spi.h"
#include "ioHdlcstream_spi_platform.h"
#include "ioHdlcosal.h"
#include "ioHdlcdma.h"
#include "ioHdlcll.h"
#include <errno.h>

#define IOHDLC_SPI_ST_TICK_US \
  ((1000000ULL + CH_CFG_ST_FREQUENCY - 1ULL) / CH_CFG_ST_FREQUENCY)

#if CH_CFG_ST_TIMEDELTA > 0
#define IOHDLC_SPI_ST_MIN_INTERVAL_US \
  (CH_CFG_ST_TIMEDELTA * IOHDLC_SPI_ST_TICK_US)
#else
#define IOHDLC_SPI_ST_MIN_INTERVAL_US \
  IOHDLC_SPI_ST_TICK_US
#endif

/* Period of the slave watchdog virtual timer. */
#if !defined(IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US)
#define IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US  (4U * IOHDLC_SPI_ST_MIN_INTERVAL_US)
#endif

/*
 * Maximum time a SPI slave may stay in a stale RX or TX transaction. This
 * value must be lower than the minimum HDLC T1 used on the SPI link.
 */
#if !defined(IOHDLC_SPI_SLAVE_WATCHDOG_DELAY_US)
#define IOHDLC_SPI_SLAVE_WATCHDOG_DELAY_US  (3U * IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US)
#endif

#if IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US == 0U
#error "IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US must be greater than zero"
#endif

#if IOHDLC_SPI_SLAVE_WATCHDOG_DELAY_US == 0U
#error "IOHDLC_SPI_SLAVE_WATCHDOG_DELAY_US must be greater than zero"
#endif

#if IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US < IOHDLC_SPI_ST_MIN_INTERVAL_US
#error "IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US is below the minimum ChibiOS interval"
#endif

static uint16_t chb_spi_slave_watchdog_ticks_from_us(uint32_t watchdog_us) {
  uint64_t ticks;

  ticks = ((uint64_t)watchdog_us + (uint64_t)IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US - 1ULL) /
          (uint64_t)IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US;
  if (ticks == 0ULL)
    ticks = 1ULL;
  if (ticks > 65535ULL)
    ticks = 65535ULL;

  return (uint16_t)ticks;
}

/*===========================================================================*/
/* Local callback implementations.                                           */
/*===========================================================================*/

static void chb_spi_start_receive_i(ioHdlcStreamChibiosSpi *ctx, size_t len, uint8_t *ptr);
static inline bool chb_spi_try_start_master_rx_i(ioHdlcStreamChibiosSpi *ctx);

static void chb_spi_slave_watchdog_cb(virtual_timer_t *vtp, void *p) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)p;
  void *tx_framep = NULL;
  bool rx_aborted = false;
  bool tx_aborted = false;
  bool rearm = false;

  (void)vtp;

  chSysLockFromISR();
  if (!ctx->is_master && ctx->started) {
    rearm = true;
    if (ctx->tx_active) {
      ctx->slave_tx_watchdog_ticks++;
      ctx->slave_rx_watchdog_ticks = 0U;
      if (ctx->slave_tx_watchdog_ticks >= ctx->slave_watchdog_limit_ticks) {
        tx_framep = ctx->tx_framep;
        if (!ioHdlcStreamSpiPlatformAbortSlaveI(ctx))
          spiStopTransferI(ctx->spip, NULL);

        ctx->tx_active = false;
        ctx->tx_framep = NULL;
        ctx->rx_active = false;
        ctx->rx_ptr = NULL;
        ctx->rx_n = 0U;
        ctx->rx_mode = IOHDLC_RX_START_PACKET;
        ctx->slave_tx_needs_prepare = false;
        ctx->slave_rx_watchdog_gate = false;
        ctx->slave_rx_watchdog_ticks = 0U;
        ctx->slave_tx_watchdog_ticks = 0U;
        palClearLine(ctx->dr_line);
        tx_aborted = true;
        rx_aborted = true;
      }
    }
    else if (ctx->rx_active && ctx->slave_rx_watchdog_gate) {
      ctx->slave_rx_watchdog_ticks++;
      ctx->slave_tx_watchdog_ticks = 0U;
      if (ctx->slave_rx_watchdog_ticks >= ctx->slave_watchdog_limit_ticks) {
        if (!ioHdlcStreamSpiPlatformAbortSlaveI(ctx))
          spiStopTransferI(ctx->spip, NULL);

        ctx->rx_active = false;
        ctx->rx_ptr = NULL;
        ctx->rx_n = 0U;
        ctx->rx_mode = IOHDLC_RX_START_PACKET;
        ctx->slave_tx_needs_prepare = false;
        ctx->slave_rx_watchdog_gate = false;
        ctx->slave_rx_watchdog_ticks = 0U;
        ctx->slave_tx_watchdog_ticks = 0U;
        rx_aborted = true;
      }
    }
    else {
      ctx->slave_rx_watchdog_ticks = 0U;
      ctx->slave_tx_watchdog_ticks = 0U;
    }
  }
  if (tx_aborted) {
    chDbgAssert(ctx->cbs != NULL && ctx->cbs->on_tx_error_i != NULL,
                "spi TX watchdog: missing TX error callback");
    ctx->cbs->on_tx_error_i(ctx->cbs->cb_ctx, tx_framep, IOHDLC_STREAM_ERR_TMO);
  }
  if (rearm && ctx->started)
    chVTSetI(&ctx->slave_watchdog_vt, TIME_US2I(IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US),
             chb_spi_slave_watchdog_cb, ctx);
  chSysUnlockFromISR();

  if (rx_aborted) {
    chDbgAssert(ctx->cbs != NULL && ctx->cbs->on_rx_error != NULL,
                "spi RX watchdog: missing RX error callback");
    ctx->cbs->on_rx_error(ctx->cbs->cb_ctx, IOHDLC_STREAM_ERR_OVERRUN);
  }
}

static void chb_spi_start_receive_i(ioHdlcStreamChibiosSpi *ctx, size_t len,
                                    uint8_t *ptr) {
  iohdlc_dma_rx_prepare(ptr, len);
  if (!ctx->is_master)
    ioHdlcStreamSpiPlatformPrepareSlaveRxI(ctx);
  spiStartReceiveI(ctx->spip, len, ptr);
}

static inline void *chb_spi_abort_master_tx_i(ioHdlcStreamChibiosSpi *ctx) {
  void *tx_framep = ctx->tx_framep;

#if SPI_SELECT_MODE != SPI_SELECT_MODE_NONE
  spiUnselectI(ctx->spip);
#endif
  spiStopTransferI(ctx->spip, NULL);

  ctx->tx_active = false;
  ctx->tx_framep = NULL;
  ctx->dr_captured = false;
  ctx->dr_collision = true;
  ctx->rx_allowed = false;

  return tx_framep;
}

static inline bool chb_spi_master_tx_blocked_i(ioHdlcStreamChibiosSpi *ctx) {
  return ctx->tx_active || ctx->rx_active || ctx->dr_epoch_active;
}

static inline bool chb_spi_try_start_master_rx_i(ioHdlcStreamChibiosSpi *ctx) {
  if (ctx->rx_ptr == NULL || ctx->rx_active || ctx->tx_active || ctx->dr_collision)
    return false;

  if (ctx->spip->state == SPI_ACTIVE)
    return false;

  if (!ctx->rx_allowed) {
    if (!ctx->dr_captured)
      return false;
    ctx->dr_captured = false;
    ctx->rx_allowed = true;
  }
  else if (ctx->rx_mode == IOHDLC_RX_START_PACKET)
    return false;

  ctx->rx_active = true;
  if (ctx->rx_n > 1U)
    ctx->rx_allowed = false;
  spiSelectI(ctx->spip);
  chb_spi_start_receive_i(ctx, ctx->rx_n, ctx->rx_ptr);
  return true;
}

/**
 * @brief   Data callback (SPI v2 @p data_cb): transfer completed without errors.
 * @details Dispatches to the TX or RX handler based on which transfer was
 *          active.  Called from ISR context.
 */
static void chb_spi_data_cb(SPIDriver *spip) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)spip->ip;
  if (!ctx) return;
  if (!ctx->started) return;

  if (ctx->tx_active) {
    /* ---- TX finished ---------------------------------------------------- */
    void *framep = ctx->tx_framep;
    ctx->tx_active  = false;
    ctx->tx_framep  = NULL;

    chDbgAssert(ctx->cbs && ctx->cbs->on_tx_done,
                "spi end_cb: on_tx_done not set");

#if SPI_SELECT_MODE != SPI_SELECT_MODE_NONE
    if (ctx->is_master)
      spiUnselectI(spip);
#endif

    if (!ctx->is_master)
      ctx->slave_tx_watchdog_ticks = 0U;

    /* Notify the swdriver. on_tx_done() may synchronously submit the next
     * frame selected by the driver, setting tx_active = true again. */
    ctx->cbs->on_tx_done(ctx->cbs->cb_ctx, framep);
    if (!ctx->started) return;

    /* If on_tx_done did not start a new TX and an RX buffer is ready,
     * restart receive. */
    if (!ctx->tx_active && ctx->rx_ptr != NULL) {
      if (ctx->is_master) {
        /* Start only from an unconsumed DATA_READY edge. */
        chSysLockFromISR();
        (void)chb_spi_try_start_master_rx_i(ctx);
        chSysUnlockFromISR();
      } else {
        ctx->rx_active = true;
        chSysLockFromISR();
        ioHdlcStreamSpiPlatformPrepareSlaveRxAfterTxI(ctx);
        chb_spi_start_receive_i(ctx, ctx->rx_n, ctx->rx_ptr);
        chSysUnlockFromISR();
        palClearLine(ctx->dr_line);
      }
    }
    else if (!ctx->tx_active && !ctx->is_master)
      palClearLine(ctx->dr_line);

  } else if (ctx->rx_active) {
    /* ---- RX finished ---------------------------------------------------- */
    size_t rx_n = ctx->rx_n;
    iohdlc_dma_rx_complete(ctx->rx_ptr, ctx->rx_n);
    ctx->rx_active = false;
    ctx->rx_ptr    = NULL;
    ctx->rx_n      = 0;
    if (ctx->is_master && rx_n > 1U)
      ctx->rx_allowed = false;
    if (!ctx->is_master)
      ctx->slave_tx_needs_prepare = true;

    chDbgAssert(ctx->cbs && ctx->cbs->on_rx,
                "spi data_cb: on_rx not set");

#if SPI_SELECT_MODE != SPI_SELECT_MODE_NONE
    if (ctx->is_master)
      spiUnselectI(spip);
#endif

    ctx->cbs->on_rx(ctx->cbs->cb_ctx, 0);
    if (ctx->is_master)
      ctx->cbs->on_tx_ready_i(ctx->cbs->cb_ctx);
  }
}

/**
 * @brief   Error callback (SPI v2 @p error_cb): DMA error during transfer.
 * @details Resets the in-flight state and notifies the swdriver using the
 *          callback matching the failed transfer direction.
 */
static void chb_spi_error_cb(SPIDriver *spip) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)spip->ip;
  void *tx_framep;
  bool tx_failed;

  if (!ctx) return;
  if (!ctx->started) return;

  tx_failed = ctx->tx_active;
  tx_framep = ctx->tx_framep;

  /* Reset both state machines: the DMA transfer was aborted by the LLD. */
  ctx->tx_active = false;
  ctx->tx_framep = NULL;
  ctx->rx_active = false;
  ctx->rx_ptr    = NULL;
  ctx->rx_n      = 0;
  ctx->rx_mode   = IOHDLC_RX_START_PACKET;
  ctx->rx_allowed = false;
  ctx->slave_tx_needs_prepare = false;
  ctx->slave_rx_watchdog_gate = false;
  ctx->slave_rx_watchdog_ticks = 0U;
  ctx->slave_tx_watchdog_ticks = 0U;
  ctx->dr_epoch_active = false;
  ctx->dr_captured = false;
  ctx->dr_collision = false;

  if (!ctx->is_master)
    palClearLine(ctx->dr_line);

#if SPI_SELECT_MODE != SPI_SELECT_MODE_NONE
  if (ctx->is_master)
    spiUnselectI(ctx->spip);
#endif

  if (tx_failed && ctx->cbs && ctx->cbs->on_tx_error_i) {
    chSysLockFromISR();
    ctx->cbs->on_tx_error_i(ctx->cbs->cb_ctx, tx_framep, IOHDLC_STREAM_ERR_OTHER);
    chSysUnlockFromISR();
  }
  else if (ctx->cbs && ctx->cbs->on_rx_error)
    ctx->cbs->on_rx_error(ctx->cbs->cb_ctx, IOHDLC_STREAM_ERR_OVERRUN);
}

/**
 * @brief   Handles a slave SPI RX overrun detected by a platform ISR.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiSlaveOverrunI(ioHdlcStreamChibiosSpi *ctx) {
  void *tx_framep = NULL;
  bool rx_aborted = false;
  bool tx_aborted = false;

  chDbgAssert(ctx != NULL, "spi overrun: null ctx");

  chSysLockFromISR();
  if (ctx->started) {
    tx_framep = ctx->tx_framep;
    tx_aborted = ctx->tx_active;
    rx_aborted = true;

    if (!ioHdlcStreamSpiPlatformAbortSlaveI(ctx))
      spiStopTransferI(ctx->spip, NULL);

    ctx->tx_active = false;
    ctx->tx_framep = NULL;
    ctx->rx_active = false;
    ctx->rx_ptr = NULL;
    ctx->rx_n = 0;
    ctx->rx_mode = IOHDLC_RX_START_PACKET;
    ctx->slave_tx_needs_prepare = false;
    ctx->rx_allowed = false;
    ctx->slave_rx_watchdog_gate = false;
    ctx->slave_rx_watchdog_ticks = 0U;
    ctx->slave_tx_watchdog_ticks = 0U;
    ctx->dr_epoch_active = false;
    ctx->dr_captured = false;
    ctx->dr_collision = false;
    palClearLine(ctx->dr_line);
  }
  if (tx_aborted) {
    chDbgAssert(ctx->cbs != NULL && ctx->cbs->on_tx_error_i != NULL,
                "spi overrun: missing TX error callback");
    ctx->cbs->on_tx_error_i(ctx->cbs->cb_ctx, tx_framep, IOHDLC_STREAM_ERR_OTHER);
  }
  chSysUnlockFromISR();

  if (rx_aborted) {
    chDbgAssert(ctx->cbs != NULL && ctx->cbs->on_rx_error != NULL,
                "spi overrun: missing RX error callback");
    ctx->cbs->on_rx_error(ctx->cbs->cb_ctx, IOHDLC_STREAM_ERR_OVERRUN);
  }
}

/*===========================================================================*/
/* Port ops implementation.                                                  */
/*===========================================================================*/

static const iohdlc_stream_caps_t chibios_spi_caps = {
  .constraints = IOHDLC_PORT_CONSTR_TWA_ONLY | IOHDLC_PORT_CONSTR_NRM_ONLY,
  .assists = IOHDLC_PORT_AST_TX_DONE_IN_ISR | IOHDLC_PORT_AST_TX_NEEDS_CONTIG,
  .tx_fcs_offload_sizes = {0, 0, 0, 0},
};

static const iohdlc_stream_caps_t *chb_spi_get_caps(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  return ctx->caps ? ctx->caps : &chibios_spi_caps;
}

static void chb_spi_start(void *vctx, const ioHdlcStreamCallbacks *cbs,
                          const ioHdlcStreamDriverOps *drvops) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  (void)drvops;

  chDbgAssert(cbs && cbs->on_rx && cbs->on_tx_done &&
              cbs->on_tx_error_i && cbs->on_tx_ready_i && cbs->on_rx_error,
              "spi start: invalid callbacks");

  ctx->cbs       = cbs;
  ctx->started   = false;
  ctx->tx_framep = NULL;
  ctx->tx_active = false;
  ctx->rx_ptr    = NULL;
  ctx->rx_n      = 0;
  ctx->rx_mode   = IOHDLC_RX_START_PACKET;
  ctx->rx_active = false;

  ctx->slave_tx_needs_prepare = false;
  ctx->slave_rx_watchdog_gate = false;
  ctx->slave_rx_watchdog_ticks = 0U;
  ctx->slave_tx_watchdog_ticks = 0U;
  ctx->rx_allowed = false;
  ctx->dr_epoch_active = false;
  ctx->dr_captured = false;
  ctx->dr_collision = false;
  chVTReset(&ctx->slave_watchdog_vt);

  if (!ctx->is_master)
    palClearLine(ctx->dr_line);

  /* Install callbacks, slave flag, and bind context pointer. */
  ctx->spip->ip = ctx;
  if (ctx->cfgp) {
    ctx->cfgp->data_cb  = chb_spi_data_cb;
    ctx->cfgp->error_cb = chb_spi_error_cb;
#if SPI_SUPPORTS_SLAVE_MODE
    ctx->cfgp->slave    = !ctx->is_master;
#endif
  }
  if (ctx->is_master)
    spiUnselect(ctx->spip);
  spiStart(ctx->spip, ctx->cfgp);
#if defined(STM32G474xx)
  if (!ctx->is_master) {
    ctx->spip->spi->CR2 |= SPI_CR2_ERRIE;
    if (ctx->spip == &SPID2)
      nvicEnableVector(SPI2_IRQn, STM32_SPI_SPI2_IRQ_PRIORITY);
  }
#endif
  ctx->started = true;
  if (!ctx->is_master)
    chVTSet(&ctx->slave_watchdog_vt, TIME_US2I(IOHDLC_SPI_SLAVE_WATCHDOG_PERIOD_US),
            chb_spi_slave_watchdog_cb, ctx);
}

static void chb_spi_stop(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  bool slave_aborted = false;

  /* Disarm the software state before aborting the hardware transfer.  A
   * DATA_READY IRQ can otherwise re-enter and submit RX during teardown. */
  chSysLock();
  ctx->started   = false;
  ctx->dr_epoch_active = false;
  ctx->dr_captured = false;
  ctx->dr_collision = false;
  ctx->tx_framep = NULL;
  ctx->tx_active = false;
  ctx->rx_ptr    = NULL;
  ctx->rx_n      = 0;
  ctx->rx_active = false;
  ctx->rx_mode   = IOHDLC_RX_START_PACKET;
  ctx->slave_tx_needs_prepare = false;
  ctx->slave_rx_watchdog_gate = false;
  ctx->slave_rx_watchdog_ticks = 0U;
  ctx->slave_tx_watchdog_ticks = 0U;
  ctx->rx_allowed = false;
  chVTResetI(&ctx->slave_watchdog_vt);
  if (!ctx->is_master)
    palClearLine(ctx->dr_line);
  if (!ctx->is_master)
    slave_aborted = ioHdlcStreamSpiPlatformAbortSlaveI(ctx);
  chSysUnlock();

  /* Stop any pending transactions. */
  if (!ctx->is_master) {
    if (!slave_aborted)
      spiStopTransfer(ctx->spip, NULL);
  } else {
    spiStopTransfer(ctx->spip, NULL);
    spiUnselect(ctx->spip);
  }

  /* Stop the SPI. */
  spiStop(ctx->spip);
}

/**
 * @brief   Submit a TX buffer.
 * @details DATA_READY masters reject TX while RX is active or ready; slaves may
 *          still cancel their pending RX when switching direction.
 */
static bool chb_spi_tx_submit(void *vctx, const uint8_t *ptr, size_t len,
                               void *cookie) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  bool needs_slave_tx_prepare;
  bool discard_rx = false;

  chDbgAssert(ctx != NULL, "spi tx_submit: null ctx");
  chDbgAssert(ptr != NULL, "spi tx_submit: null ptr");
  chDbgAssert(len > 0U, "spi tx_submit: zero length");
  if (!ctx->started) return false;
  chDbgAssert(!ctx->tx_active, "spi tx_submit: tx already active");

  needs_slave_tx_prepare = !ctx->is_master && ctx->slave_tx_needs_prepare;

  if (ctx->is_master) {
    if (chb_spi_master_tx_blocked_i(ctx))
      return false;
    if (ctx->rx_ptr != NULL)
      discard_rx = true;
  }

  if (ctx->rx_active) {
    ctx->rx_active = false;
    if (ctx->is_master) {
      spiUnselectI(ctx->spip);
      spiStopTransferI(ctx->spip, NULL);
    } else {
      if (ioHdlcStreamSpiPlatformCancelSlaveRxI(ctx)) {
        needs_slave_tx_prepare = false;
        ctx->slave_tx_needs_prepare = false;
      } else {
        spiStopTransferI(ctx->spip, NULL);
        needs_slave_tx_prepare = true;
      }
      discard_rx = true;
    }
  }
  else if (!ctx->is_master && ctx->rx_ptr != NULL)
    discard_rx = true;

  if (discard_rx) {
    /* In NRM/TWA SPI, TX is a turn boundary: previous RX state is stale. */
    ctx->tx_active = true;
    ctx->cbs->on_rx_error(ctx->cbs->cb_ctx, IOHDLC_STREAM_ERR_OVERRUN);
  }

  ctx->tx_framep = cookie;
  ctx->tx_active = true;
  if (ctx->is_master) {
    ctx->dr_captured = false;
    ctx->rx_allowed = false;
  }
  if (needs_slave_tx_prepare) {
    ioHdlcStreamSpiPlatformPrepareSlaveTx(ctx);
    ctx->slave_tx_needs_prepare = false;
  }
  iohdlc_dma_tx_prepare(ptr, len);
  if (ctx->is_master) {
    spiSelectI(ctx->spip);
    spiStartSendI(ctx->spip, len, ptr);
  } else {
    ctx->slave_rx_watchdog_gate = false;
    ctx->slave_rx_watchdog_ticks = 0U;
    ctx->slave_tx_watchdog_ticks = 0U;
    palClearLine(ctx->dr_line);
    spiStartSendI(ctx->spip, len, ptr);
    /* Slave: assert DATA_READY to signal the master that TX data is ready. */
    palSetLine(ctx->dr_line);
  }

  return true;
}

static int32_t chb_spi_tx_submit_frame(void *vctx, iohdlc_frame_t *fp) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  const uint8_t *ptr = fp->frame;
  size_t len = fp->tx_len;

  chDbgAssert(ctx != NULL, "spi tx_submit_frame: null ctx");
  chDbgAssert(fp != NULL, "spi tx_submit_frame: null frame");
  chDbgAssert(ctx->cbs != NULL, "spi tx_submit_frame: callbacks not set");

  if (fp->openingflag == IOHDLC_FLAG) {
    ptr = &fp->openingflag;
    len += 1U;
  }

  /* SPI consumes a contiguous wire image prepared by the swdriver. */
  return chb_spi_tx_submit(vctx, ptr, len, fp) ? 0 : EIO;
}

static bool chb_spi_tx_busy(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  chDbgAssert(ctx != NULL, "spi tx_busy: null ctx");
  if (!ctx->started)
    return false;

  if (ctx->is_master)
    return chb_spi_master_tx_blocked_i(ctx);

  return ctx->tx_active;
}

/**
 * @brief   Arm an RX buffer.
 * @details If TX is currently active the buffer is saved and RX DMA will be
 *          launched automatically at the end of the TX transfer.  If TX is
 *          idle, DMA is started immediately.
 */
static bool chb_spi_rx_submit(void *vctx, uint8_t *ptr, size_t len,
                              iohdlc_rx_mode_t mode) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;

  chDbgAssert(ctx != NULL, "spi rx_submit: null ctx");
  chDbgAssert(ptr != NULL, "spi rx_submit: null ptr");
  chDbgAssert(len > 0U, "spi rx_submit: zero length");
  if (!ctx->started) return false;

  /* Save the pending buffer (also used as "pending" signal in txend2). */
  ctx->rx_ptr = ptr;
  ctx->rx_n   = len;
  ctx->rx_mode = mode;
  if (!ctx->is_master) {
    ctx->slave_rx_watchdog_gate = mode != IOHDLC_RX_START_PACKET;
    ctx->slave_rx_watchdog_ticks = 0U;
  }
  if (ctx->is_master) {
    /* Master cannot start DMA without a valid DATA_READY epoch. */
    if (!ctx->tx_active)
      (void)chb_spi_try_start_master_rx_i(ctx);
    /* else: data_cb will handle after TX completes. */
  } else {
    if (!ctx->tx_active) {
      ctx->rx_active = true;
      chb_spi_start_receive_i(ctx, len, ptr);
    }
    /* else: DMA will be started by chb_spi_data_cb after TX completes. */
  }

  return true;
}

static void chb_spi_rx_cancel(void *vctx) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)vctx;
  chDbgAssert(ctx != NULL, "spi rx_cancel: null ctx");
  if (ctx->is_master && ctx->rx_active) {
    spiUnselectI(ctx->spip);
    spiStopTransferI(ctx->spip, NULL);
    ctx->rx_active = false;
  }
  ctx->rx_ptr = NULL;
  ctx->rx_n   = 0;
  ctx->rx_mode = IOHDLC_RX_START_PACKET;
  ctx->dr_epoch_active = false;
  ctx->dr_captured = false;
  ctx->dr_collision = false;
  ctx->rx_allowed = false;
  if (!ctx->is_master) {
    ctx->slave_rx_watchdog_gate = false;
    ctx->slave_rx_watchdog_ticks = 0U;
    ctx->slave_tx_watchdog_ticks = 0U;
  }
}

/**
 * @brief   Called from a board-level PAL event callback when the slave
 *          DATA_READY line changes state.
 * @details DATA_READY rising edge grants one clocking credit. The falling edge
 *          closes the physical slave-packet epoch and releases TX-collision
 *          recovery. START RX consumes the captured edge; CONTINUE RX is
 *          allowed while the credit remains.
 * @note    Must be called from ISR context.
 */
void ioHdlcStreamSpiDataReadyI(ioHdlcStreamChibiosSpi *ctx) {
  bool dr_high;

  chDbgAssert(ctx->is_master, "spi DR event on slave");

  if (!ctx->started)
    return;

  dr_high = palReadLine(ctx->dr_line) != PAL_LOW;
  if (!dr_high) {
    ctx->dr_epoch_active = false;
    ctx->dr_captured = false;
    ctx->dr_collision = false;
    ctx->rx_allowed = false;
    ctx->cbs->on_tx_ready_i(ctx->cbs->cb_ctx);
    return;
  }

  ctx->dr_epoch_active = true;

  if (ctx->tx_active) {
    void *tx_framep = chb_spi_abort_master_tx_i(ctx);
    ctx->cbs->on_tx_error_i(ctx->cbs->cb_ctx, tx_framep, IOHDLC_STREAM_ERR_OTHER);
    return;
  }

  if (ctx->dr_collision)
    return;

  if (!ctx->rx_allowed)
    ctx->dr_captured = true;

  (void)chb_spi_try_start_master_rx_i(ctx);
}

static const ioHdlcStreamPortOps chibios_spi_ops = {
  .get_caps  = chb_spi_get_caps,
  .start     = chb_spi_start,
  .stop      = chb_spi_stop,
  .tx_submit_frame = chb_spi_tx_submit_frame,
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
void ioHdlcStreamPortChibiosSpiObjectInit(ioHdlcStreamPort *port,
                                          ioHdlcStreamChibiosSpi *obj,
                                          SPIDriver *spip, SPIConfig *cfgp,
                                          bool is_master, ioline_t dr_line) {
  chDbgAssert(dr_line != PAL_NOLINE, "spi object init: DATA_READY required");

  obj->spip      = spip;
  obj->cfgp      = cfgp;
  obj->is_master = is_master;
  obj->started   = false;
  obj->dr_line   = dr_line;
  obj->dr_epoch_active = false;
  obj->dr_captured = false;
  obj->dr_collision = false;
  obj->cbs       = NULL;
  obj->caps      = &chibios_spi_caps;
  obj->tx_framep = NULL;
  obj->tx_active = is_master;
  obj->rx_ptr    = NULL;
  obj->rx_n      = 0;
  obj->rx_mode   = IOHDLC_RX_START_PACKET;
  obj->rx_active = !is_master;
  obj->rx_allowed = false;
  obj->slave_tx_needs_prepare = false;
  obj->slave_rx_watchdog_gate = false;
  obj->slave_watchdog_limit_ticks =
    chb_spi_slave_watchdog_ticks_from_us(IOHDLC_SPI_SLAVE_WATCHDOG_DELAY_US);
  obj->slave_rx_watchdog_ticks = 0U;
  obj->slave_tx_watchdog_ticks = 0U;
  chVTObjectInit(&obj->slave_watchdog_vt);

  port->ctx = obj;
  port->ops = &chibios_spi_ops;
}

void ioHdlcStreamSpiSetSlaveWatchdogDelay(ioHdlcStreamChibiosSpi *obj,
                                          uint32_t delay_us) {
  chDbgAssert(obj != NULL, "spi watchdog config: null object");

  obj->slave_watchdog_limit_ticks =
    chb_spi_slave_watchdog_ticks_from_us(delay_us);
}
