/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file    ioHdlcstream_spi_platform_impl.h
 * @brief   Inline SAMA5D2x hooks for the ChibiOS SPI stream backend.
 */

#ifndef IOHDLCSTREAM_SPI_PLATFORM_IMPL_H
#define IOHDLCSTREAM_SPI_PLATFORM_IMPL_H

/**
 * @brief   Applies ioHdlc SPI requirements to the platform configuration.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareConfig(
    ioHdlcStreamChibiosSpi *ctx) {

#if SAMA_SPI_CACHE_USER_MANAGED == FALSE
  ctx->cfgp->cache_user_managed = true;
#else
  (void)ctx;
#endif
}

/**
 * @brief   Handles a pending DATA_READY event already consumed by the adapter.
 * @details SAMA PIO status is read-to-clear for the whole port, so no safe
 *          selective clear is available.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformClearDrPendingI(
    ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;
}

/**
 * @brief   Stops both DMA channels and clears the SPI FIFOs.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformResetTransferI(
    ioHdlcStreamChibiosSpi *ctx) {

  dmaChannelDisable(ctx->spip->dmatx);
  dmaChannelDisable(ctx->spip->dmarx);
  ctx->spip->spi->SPI_CR = SPI_CR_TXFCLR | SPI_CR_RXFCLR;
  (void)ctx->spip->spi->SPI_RDR;
  (void)ctx->spip->spi->SPI_SR;
  ctx->spip->state = SPI_READY;
}

/**
 * @brief   Prepares a slave RX transfer.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveRxI(
    ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;
}

/**
 * @brief   Prepares slave RX after a completed slave TX transfer.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveRxAfterTxI(
    ioHdlcStreamChibiosSpi *ctx) {

  ioHdlcStreamSpiPlatformResetTransferI(ctx);
}

/**
 * @brief   Prepares a slave TX transfer after an RX boundary.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveTx(
    ioHdlcStreamChibiosSpi *ctx) {

  ioHdlcStreamSpiPlatformResetTransferI(ctx);
}

/**
 * @brief   Cancels an armed slave RX transfer without waiting for clocks.
 *
 * @param[in] ctx       SPI stream context
 * @return              true because the platform handled the cancellation
 */
static inline bool ioHdlcStreamSpiPlatformCancelSlaveRxI(
    ioHdlcStreamChibiosSpi *ctx) {

  ioHdlcStreamSpiPlatformResetTransferI(ctx);

  return true;
}

#endif /* IOHDLCSTREAM_SPI_PLATFORM_IMPL_H */
