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
 * @brief   Prepares a slave RX transfer.
 *
 * @param[in] ctx       SPI stream context
 */
static inline void ioHdlcStreamSpiPlatformPrepareSlaveRxI(
    ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;
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
