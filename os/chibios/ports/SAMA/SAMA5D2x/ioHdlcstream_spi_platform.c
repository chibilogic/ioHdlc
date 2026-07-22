/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file    ioHdlcstream_spi_platform.c
 * @brief   SAMA5D2x SPI stream platform hooks.
 */

#include "ioHdlcstream_spi_platform.h"

#if CH_KERNEL_MAJOR < 7
/**
 * @brief   Stops an active SAMA SPI transfer from a locked context.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformStopTransferI(ioHdlcStreamChibiosSpi *ctx) {
  ioHdlcStreamSpiPlatformResetTransferI(ctx);
}
#endif

/**
 * @brief   Aborts an active SAMA slave transfer from a locked context.
 *
 * @param[in] ctx       SPI stream context
 * @return              true because the platform handled the abort
 */
bool ioHdlcStreamSpiPlatformAbortSlaveI(ioHdlcStreamChibiosSpi *ctx) {
  ioHdlcStreamSpiPlatformResetTransferI(ctx);

  return true;
}
