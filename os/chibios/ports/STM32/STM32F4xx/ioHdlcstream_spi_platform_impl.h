/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file    ioHdlcstream_spi_platform_impl.h
 * @brief   Inline STM32F411RE platform hooks for the ChibiOS SPI stream backend.
 */

#ifndef IOHDLCSTREAM_SPI_PLATFORM_IMPL_H
#define IOHDLCSTREAM_SPI_PLATFORM_IMPL_H

/**
 * @brief   Applies ioHdlc SPI requirements to the platform configuration.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareConfig(ioHdlcStreamChibiosSpi *ctx) {
  (void)ctx;
}

/**
 * @brief   Prepares a slave RX transfer.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveRxI(ioHdlcStreamChibiosSpi *ctx) {
  ctx->spip->spi->CR2 = (ctx->spip->spi->CR2 | SPI_CR2_ERRIE) & ~SPI_CR2_TXDMAEN;
}

/**
 * @brief   Prepares slave RX after a completed slave TX transfer.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveRxAfterTxI(ioHdlcStreamChibiosSpi *ctx) {
  (void)ctx->spip->spi->DR;
  (void)ctx->spip->spi->SR;

  ioHdlcStreamSpiPlatformPrepareSlaveRxI(ctx);
}

/**
 * @brief   Prepares a slave TX transfer after a RX boundary.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveTx(ioHdlcStreamChibiosSpi *ctx) {
  ctx->spip->spi->CR2 |= SPI_CR2_ERRIE | SPI_CR2_TXDMAEN;
}

/**
 * @brief   Cancels an armed slave RX transfer without waiting for clocks.
 *
 * @param[in] ctx       SPI stream context
 * @return              true if the platform handled the cancellation
 */
static inline bool ioHdlcStreamSpiPlatformCancelSlaveRxI(ioHdlcStreamChibiosSpi *ctx) {
  dmaStreamDisable(ctx->spip->dmatx);
  dmaStreamDisable(ctx->spip->dmarx);

  (void)ctx->spip->spi->DR;
  (void)ctx->spip->spi->SR;

  ctx->spip->spi->CR2 |= SPI_CR2_ERRIE | SPI_CR2_TXDMAEN;
  ctx->spip->state = SPI_READY;

  return true;
}

#endif /* IOHDLCSTREAM_SPI_PLATFORM_IMPL_H */
