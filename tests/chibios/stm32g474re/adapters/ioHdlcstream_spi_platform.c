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

static void reset_spi(ioHdlcStreamChibiosSpi *ctx) {

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
}

static void configure_slave(ioHdlcStreamChibiosSpi *ctx) {
  uint32_t cr1;
  uint32_t cr2;

  cr1 = ctx->cfgp->cr1 & ~(SPI_CR1_MSTR | SPI_CR1_SPE);
  cr2 = ctx->cfgp->cr2 | SPI_CR2_FRXTH | SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;

  ctx->spip->spi->CR1 = cr1;
  ctx->spip->spi->CR2 = cr2;
  ctx->spip->spi->CR1 = cr1 | SPI_CR1_SPE;
}

static void quick_cancel_slave_rx_i(ioHdlcStreamChibiosSpi *ctx) {
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

  ctx->spip->state = SPI_READY;
}

/**
 * @brief   Prepares a slave TX transfer after a RX boundary.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformPrepareSlaveTx(ioHdlcStreamChibiosSpi *ctx) {

  quick_cancel_slave_rx_i(ctx);
}

/**
 * @brief   Cancels an armed slave RX transfer without waiting for clocks.
 *
 * @param[in] ctx       SPI stream context
 * @return              true if the platform handled the cancellation
 */
bool ioHdlcStreamSpiPlatformCancelSlaveRxI(ioHdlcStreamChibiosSpi *ctx) {

  quick_cancel_slave_rx_i(ctx);

  return true;
}

/**
 * @brief   Aborts a slave transfer during teardown without waiting for clocks.
 *
 * @param[in] ctx       SPI stream context
 * @return              true if the platform handled the abort
 */
bool ioHdlcStreamSpiPlatformAbortSlaveI(ioHdlcStreamChibiosSpi *ctx) {

  dmaStreamDisable(ctx->spip->dmatx);
  dmaStreamDisable(ctx->spip->dmarx);
  reset_spi(ctx);
  configure_slave(ctx);
  ctx->spip->state = SPI_READY;

  return true;
}
