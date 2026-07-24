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
 * @brief   STM32G474RE SPI stream platform hook translation unit.
 */

#include "ioHdlcstream_spi_platform.h"

static inline void s_serve_overrun_irq(SPIDriver *spip) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)spip->ip;

  if ((spip->spi->SR & SPI_SR_OVR) == 0U)
    return;

  if (ctx != NULL && ctx->started && !ctx->is_master)
    ioHdlcStreamSpiSlaveOverrunI(ctx);
  else {
    (void)spip->spi->DR;
    (void)spip->spi->SR;
  }
}

#if STM32_SPI_USE_SPI1
OSAL_IRQ_HANDLER(VectorCC) {

  OSAL_IRQ_PROLOGUE();

  s_serve_overrun_irq(&SPID1);

  OSAL_IRQ_EPILOGUE();
}
#endif

#if STM32_SPI_USE_SPI2
OSAL_IRQ_HANDLER(VectorD0) {

  OSAL_IRQ_PROLOGUE();

  s_serve_overrun_irq(&SPID2);

  OSAL_IRQ_EPILOGUE();
}
#endif

#if STM32_SPI_USE_SPI3
OSAL_IRQ_HANDLER(Vector10C) {

  OSAL_IRQ_PROLOGUE();

  s_serve_overrun_irq(&SPID3);

  OSAL_IRQ_EPILOGUE();
}
#endif

#if STM32_SPI_USE_SPI4
OSAL_IRQ_HANDLER(Vector190) {

  OSAL_IRQ_PROLOGUE();

  s_serve_overrun_irq(&SPID4);

  OSAL_IRQ_EPILOGUE();
}
#endif

/**
 * @brief   Enables the slave SPI error interrupt.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformStart(ioHdlcStreamChibiosSpi *ctx) {
  uint32_t irq = 0U;
  uint32_t priority = 0U;

  if (ctx->is_master)
    return;

  if (false) {
  }
#if STM32_SPI_USE_SPI1
  else if (ctx->spip == &SPID1) {
    irq = SPI1_IRQn;
    priority = STM32_SPI_SPI1_IRQ_PRIORITY;
  }
#endif
#if STM32_SPI_USE_SPI2
  else if (ctx->spip == &SPID2) {
    irq = SPI2_IRQn;
    priority = STM32_SPI_SPI2_IRQ_PRIORITY;
  }
#endif
#if STM32_SPI_USE_SPI3
  else if (ctx->spip == &SPID3) {
    irq = SPI3_IRQn;
    priority = STM32_SPI_SPI3_IRQ_PRIORITY;
  }
#endif
#if STM32_SPI_USE_SPI4
  else if (ctx->spip == &SPID4) {
    irq = SPI4_IRQn;
    priority = STM32_SPI_SPI4_IRQ_PRIORITY;
  }
#endif
  else {
    chDbgAssert(false, "unsupported slave SPI instance");
    return;
  }

  (void)ctx->spip->spi->DR;
  (void)ctx->spip->spi->SR;
  ctx->spip->spi->CR2 |= SPI_CR2_ERRIE;
  nvicEnableVector(irq, priority);
}

/**
 * @brief   Disables the slave SPI error interrupt from a locked context.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformStopI(ioHdlcStreamChibiosSpi *ctx) {
  uint32_t irq = 0U;

  if (ctx->is_master)
    return;

  if (false) {
  }
#if STM32_SPI_USE_SPI1
  else if (ctx->spip == &SPID1)
    irq = SPI1_IRQn;
#endif
#if STM32_SPI_USE_SPI2
  else if (ctx->spip == &SPID2)
    irq = SPI2_IRQn;
#endif
#if STM32_SPI_USE_SPI3
  else if (ctx->spip == &SPID3)
    irq = SPI3_IRQn;
#endif
#if STM32_SPI_USE_SPI4
  else if (ctx->spip == &SPID4)
    irq = SPI4_IRQn;
#endif
  else {
    chDbgAssert(false, "unsupported slave SPI instance");
    return;
  }

  ctx->spip->spi->CR2 &= ~SPI_CR2_ERRIE;
  nvicDisableVector(irq);
}

/**
 * @brief   Aborts a slave transfer during teardown without waiting for clocks.
 *
 * @param[in] ctx       SPI stream context
 * @return              true if the platform handled the abort
 */
bool ioHdlcStreamSpiPlatformAbortSlaveI(ioHdlcStreamChibiosSpi *ctx) {
  uint32_t cr1;
  uint32_t cr2;

  dmaStreamDisable(ctx->spip->dmatx);
  dmaStreamDisable(ctx->spip->dmarx);

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
  cr2 = ctx->cfgp->cr2 | SPI_CR2_FRXTH | SPI_CR2_RXDMAEN |
        SPI_CR2_TXDMAEN | SPI_CR2_ERRIE;

  ctx->spip->spi->CR1 = cr1;
  ctx->spip->spi->CR2 = cr2;
  ctx->spip->spi->CR1 = cr1 | SPI_CR1_SPE;
  ctx->spip->state = SPI_READY;
  if (ctx->started)
    ioHdlcStreamSpiPlatformStart(ctx);

  return true;
}
