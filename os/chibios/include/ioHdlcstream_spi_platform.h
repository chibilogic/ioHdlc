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
 * @file    ioHdlcstream_spi_platform.h
 * @brief   Platform hooks for the ChibiOS SPI stream backend.
 */

#ifndef IOHDLCSTREAM_SPI_PLATFORM_H
#define IOHDLCSTREAM_SPI_PLATFORM_H

#include <stdbool.h>
#include "ioHdlcstream_spi.h"
#include "ioHdlcstream_spi_platform_impl.h"

void ioHdlcStreamSpiPlatformStart(ioHdlcStreamChibiosSpi *ctx);
void ioHdlcStreamSpiPlatformStopI(ioHdlcStreamChibiosSpi *ctx);
bool ioHdlcStreamSpiPlatformAbortSlaveI(ioHdlcStreamChibiosSpi *ctx);

#if CH_KERNEL_MAJOR < 7
void ioHdlcStreamSpiPlatformStopTransferI(ioHdlcStreamChibiosSpi *ctx);
#endif

#endif /* IOHDLCSTREAM_SPI_PLATFORM_H */
