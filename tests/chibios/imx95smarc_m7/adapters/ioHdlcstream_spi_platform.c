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
 * @brief   IMX95 platform hooks for the ChibiOS SPI stream backend.
 */

#include "ioHdlcstream_spi.h"
#include "ioHdlcstream_spi_platform.h"

void ioHdlcStreamSpiPlatformPrepareSlaveTx(ioHdlcStreamChibiosSpi *ctx) {
  (void)ctx;
}

bool ioHdlcStreamSpiPlatformCancelSlaveRxI(ioHdlcStreamChibiosSpi *ctx) {
  (void)ctx;

  return false;
}

bool ioHdlcStreamSpiPlatformAbortSlaveI(ioHdlcStreamChibiosSpi *ctx) {
  (void)ctx;

  return false;
}
