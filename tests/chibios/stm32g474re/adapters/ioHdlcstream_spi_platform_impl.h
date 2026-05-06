/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file    ioHdlcstream_spi_platform_impl.h
 * @brief   Inline STM32G474RE platform hooks for the ChibiOS SPI stream backend.
 */

#ifndef IOHDLCSTREAM_SPI_PLATFORM_IMPL_H
#define IOHDLCSTREAM_SPI_PLATFORM_IMPL_H

/**
 * @brief   Cancels STM32 slave RX DMA and drains residual FIFO bytes.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformQuickCancelSlaveRxI(
    ioHdlcStreamChibiosSpi *ctx) {
  unsigned i;

  dmaStreamDisable(ctx->spip->dmatx);
  dmaStreamDisable(ctx->spip->dmarx);

  for (i = 0U; i < 4U; i++) {
    if ((ctx->spip->spi->SR & SPI_SR_RXNE) == 0U) {
      break;
    }
    (void)ctx->spip->spi->DR;
  }
  (void)ctx->spip->spi->SR;

  ctx->spip->spi->CR2 |= SPI_CR2_TXDMAEN;
  ctx->spip->state = SPI_READY;
}

/**
 * @brief   Prepares a slave RX transfer.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveRxI(
    ioHdlcStreamChibiosSpi *ctx) {

  ctx->spip->spi->CR2 &= ~SPI_CR2_TXDMAEN;
}

/**
 * @brief   Prepares a slave TX transfer after a RX boundary.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveTx(
    ioHdlcStreamChibiosSpi *ctx) {

  ioHdlcStreamSpiPlatformQuickCancelSlaveRxI(ctx);
}

/**
 * @brief   Cancels an armed slave RX transfer without waiting for clocks.
 *
 * @param[in] ctx       SPI stream context
 * @return              true if the platform handled the cancellation
 */
static inline bool ioHdlcStreamSpiPlatformCancelSlaveRxI(
    ioHdlcStreamChibiosSpi *ctx) {

  ioHdlcStreamSpiPlatformQuickCancelSlaveRxI(ctx);

  return true;
}

#endif /* IOHDLCSTREAM_SPI_PLATFORM_IMPL_H */
