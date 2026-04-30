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
 * @brief   STM32G474RE platform hooks for the ChibiOS SPI stream backend.
 */

#include "ioHdlcstream_spi.h"
#include "ioHdlcstream_spi_platform.h"

void ioHdlcStreamSpiPlatformPrepareSlaveTx(ioHdlcStreamChibiosSpi *ctx) {
  uint32_t cr1;
  uint32_t cr2;

  if (false) {
  }
#if STM32_SPI_USE_SPI1
  else if (ctx->spip == &SPID1) {
    rccResetSPI1();
  }
#endif
#if STM32_SPI_USE_SPI2
  else if (ctx->spip == &SPID2) {
    rccResetSPI2();
  }
#endif
#if STM32_SPI_USE_SPI3
  else if (ctx->spip == &SPID3) {
    rccResetSPI3();
  }
#endif
#if STM32_SPI_USE_SPI4
  else if (ctx->spip == &SPID4) {
    rccResetSPI4();
  }
#endif
  else {
    chDbgAssert(false, "unsupported SPI instance");
  }

  cr1 = ctx->cfgp->cr1 & ~(SPI_CR1_MSTR | SPI_CR1_SPE);
  cr2 = ctx->cfgp->cr2 | SPI_CR2_FRXTH | SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;

  ctx->spip->spi->CR1 = cr1;
  ctx->spip->spi->CR2 = cr2;
  ctx->spip->spi->CR1 = cr1 | SPI_CR1_SPE;
}
