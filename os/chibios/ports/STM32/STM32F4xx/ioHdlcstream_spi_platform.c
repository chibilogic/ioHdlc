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
 * @file    ioHdlcstream_spi_platform.c
 * @brief   STM32F411RE SPI stream platform hook translation unit.
 */

#include "ioHdlcstream_spi_platform.h"

/**
 * @brief   Starts platform-specific SPI services.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformStart(ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;
}

/**
 * @brief   Stops platform-specific SPI services from a locked context.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformStopI(ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;
}

bool ioHdlcStreamSpiPlatformAbortSlaveI(ioHdlcStreamChibiosSpi *ctx) {

  (void)ctx;

  return false;
}
