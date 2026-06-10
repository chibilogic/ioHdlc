/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file    ioHdlcstream_spi_platform_impl.h
 * @brief   Inline IMX95 platform hooks for the ChibiOS SPI stream backend.
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

  ctx->cfgp->tcr |= LPSPI_TCR_LSBF(1U);
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

  ioHdlcStreamSpiPlatformPrepareSlaveRxI(ctx);
}

/**
 * @brief   Prepares a slave TX transfer after a RX boundary.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveTx(
    ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;
}

/**
 * @brief   Cancels an armed slave RX transfer without waiting for clocks.
 *
 * @param[in] ctx       SPI stream context
 * @return              true if the platform handled the cancellation
 */
static inline bool ioHdlcStreamSpiPlatformCancelSlaveRxI(
    ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;

  return false;
}

#endif /* IOHDLCSTREAM_SPI_PLATFORM_IMPL_H */
