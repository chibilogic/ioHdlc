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

#if SAMA_SPI_USE_SPI0 && !defined(SAMA_SPI_SPI0_IRQ_PRIORITY)
#error "SAMA_SPI_SPI0_IRQ_PRIORITY must be defined"
#endif

#if SAMA_SPI_USE_SPI1 && !defined(SAMA_SPI_SPI1_IRQ_PRIORITY)
#error "SAMA_SPI_SPI1_IRQ_PRIORITY must be defined"
#endif

#if SAMA_SPI_USE_FLEXCOM0 && !defined(SAMA_SPI_FLEXCOM0_IRQ_PRIORITY)
#error "SAMA_SPI_FLEXCOM0_IRQ_PRIORITY must be defined"
#endif

#if SAMA_SPI_USE_FLEXCOM1 && !defined(SAMA_SPI_FLEXCOM1_IRQ_PRIORITY)
#error "SAMA_SPI_FLEXCOM1_IRQ_PRIORITY must be defined"
#endif

#if SAMA_SPI_USE_FLEXCOM2 && !defined(SAMA_SPI_FLEXCOM2_IRQ_PRIORITY)
#error "SAMA_SPI_FLEXCOM2_IRQ_PRIORITY must be defined"
#endif

#if SAMA_SPI_USE_FLEXCOM3 && !defined(SAMA_SPI_FLEXCOM3_IRQ_PRIORITY)
#error "SAMA_SPI_FLEXCOM3_IRQ_PRIORITY must be defined"
#endif

#if SAMA_SPI_USE_FLEXCOM4 && !defined(SAMA_SPI_FLEXCOM4_IRQ_PRIORITY)
#error "SAMA_SPI_FLEXCOM4_IRQ_PRIORITY must be defined"
#endif

static inline void s_serve_overrun_irq(SPIDriver *spip) {
  ioHdlcStreamChibiosSpi *ctx = (ioHdlcStreamChibiosSpi *)spip->ip;
  uint32_t sr = spip->spi->SPI_SR;

  if ((sr & SPI_SR_OVRES) != 0U &&
      ctx != NULL && ctx->started && !ctx->is_master)
    ioHdlcStreamSpiSlaveOverrunI(ctx);
}

#define IOHDLC_SAMA_SPI_OVERRUN_HANDLER(name, driver)                       \
  OSAL_IRQ_HANDLER(name) {                                                  \
    OSAL_IRQ_PROLOGUE();                                                    \
    s_serve_overrun_irq(&(driver));                                         \
    aicAckInt();                                                            \
    OSAL_IRQ_EPILOGUE();                                                    \
  }

#if SAMA_SPI_USE_SPI0
IOHDLC_SAMA_SPI_OVERRUN_HANDLER(iohdlc_sama_spi0_handler, SPID0)
#endif

#if SAMA_SPI_USE_SPI1
IOHDLC_SAMA_SPI_OVERRUN_HANDLER(iohdlc_sama_spi1_handler, SPID1)
#endif

#if SAMA_SPI_USE_FLEXCOM0
IOHDLC_SAMA_SPI_OVERRUN_HANDLER(iohdlc_sama_flexcom0_spi_handler, FSPID0)
#endif

#if SAMA_SPI_USE_FLEXCOM1
IOHDLC_SAMA_SPI_OVERRUN_HANDLER(iohdlc_sama_flexcom1_spi_handler, FSPID1)
#endif

#if SAMA_SPI_USE_FLEXCOM2
IOHDLC_SAMA_SPI_OVERRUN_HANDLER(iohdlc_sama_flexcom2_spi_handler, FSPID2)
#endif

#if SAMA_SPI_USE_FLEXCOM3
IOHDLC_SAMA_SPI_OVERRUN_HANDLER(iohdlc_sama_flexcom3_spi_handler, FSPID3)
#endif

#if SAMA_SPI_USE_FLEXCOM4
IOHDLC_SAMA_SPI_OVERRUN_HANDLER(iohdlc_sama_flexcom4_spi_handler, FSPID4)
#endif

#undef IOHDLC_SAMA_SPI_OVERRUN_HANDLER

/**
 * @brief   Enables slave SPI overrun detection.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformStart(ioHdlcStreamChibiosSpi *ctx) {
  bool (*handler)(void) = NULL;
  uint32_t source = 0U;
  uint8_t priority = 0U;

  if (ctx->is_master)
    return;

  if (false) {
  }
#if SAMA_SPI_USE_SPI0
  else if (ctx->spip == &SPID0) {
    handler = iohdlc_sama_spi0_handler;
    source = ID_SPI0;
    priority = SAMA_SPI_SPI0_IRQ_PRIORITY;
  }
#endif
#if SAMA_SPI_USE_SPI1
  else if (ctx->spip == &SPID1) {
    handler = iohdlc_sama_spi1_handler;
    source = ID_SPI1;
    priority = SAMA_SPI_SPI1_IRQ_PRIORITY;
  }
#endif
#if SAMA_SPI_USE_FLEXCOM0
  else if (ctx->spip == &FSPID0) {
    handler = iohdlc_sama_flexcom0_spi_handler;
    source = ID_FLEXCOM0;
    priority = SAMA_SPI_FLEXCOM0_IRQ_PRIORITY;
  }
#endif
#if SAMA_SPI_USE_FLEXCOM1
  else if (ctx->spip == &FSPID1) {
    handler = iohdlc_sama_flexcom1_spi_handler;
    source = ID_FLEXCOM1;
    priority = SAMA_SPI_FLEXCOM1_IRQ_PRIORITY;
  }
#endif
#if SAMA_SPI_USE_FLEXCOM2
  else if (ctx->spip == &FSPID2) {
    handler = iohdlc_sama_flexcom2_spi_handler;
    source = ID_FLEXCOM2;
    priority = SAMA_SPI_FLEXCOM2_IRQ_PRIORITY;
  }
#endif
#if SAMA_SPI_USE_FLEXCOM3
  else if (ctx->spip == &FSPID3) {
    handler = iohdlc_sama_flexcom3_spi_handler;
    source = ID_FLEXCOM3;
    priority = SAMA_SPI_FLEXCOM3_IRQ_PRIORITY;
  }
#endif
#if SAMA_SPI_USE_FLEXCOM4
  else if (ctx->spip == &FSPID4) {
    handler = iohdlc_sama_flexcom4_spi_handler;
    source = ID_FLEXCOM4;
    priority = SAMA_SPI_FLEXCOM4_IRQ_PRIORITY;
  }
#endif
  else {
    chDbgAssert(false, "slave SPI instance not enabled");
    return;
  }

  ctx->spip->spi->SPI_IDR = SPI_IDR_OVRES;
  aicSetSourcePriority(source, priority);
  aicSetSourceHandler(source, handler);
  (void)ctx->spip->spi->SPI_SR;
  aicEnableInt(source);
  ctx->spip->spi->SPI_IER = SPI_IER_OVRES;
}

/**
 * @brief   Disables slave SPI overrun detection from a locked context.
 *
 * @param[in] ctx       SPI stream context
 */
void ioHdlcStreamSpiPlatformStopI(ioHdlcStreamChibiosSpi *ctx) {
  uint32_t source = 0U;

  if (ctx->is_master)
    return;

  if (false) {
  }
#if SAMA_SPI_USE_SPI0
  else if (ctx->spip == &SPID0)
    source = ID_SPI0;
#endif
#if SAMA_SPI_USE_SPI1
  else if (ctx->spip == &SPID1)
    source = ID_SPI1;
#endif
#if SAMA_SPI_USE_FLEXCOM0
  else if (ctx->spip == &FSPID0)
    source = ID_FLEXCOM0;
#endif
#if SAMA_SPI_USE_FLEXCOM1
  else if (ctx->spip == &FSPID1)
    source = ID_FLEXCOM1;
#endif
#if SAMA_SPI_USE_FLEXCOM2
  else if (ctx->spip == &FSPID2)
    source = ID_FLEXCOM2;
#endif
#if SAMA_SPI_USE_FLEXCOM3
  else if (ctx->spip == &FSPID3)
    source = ID_FLEXCOM3;
#endif
#if SAMA_SPI_USE_FLEXCOM4
  else if (ctx->spip == &FSPID4)
    source = ID_FLEXCOM4;
#endif
  else {
    chDbgAssert(false, "slave SPI instance not enabled");
    return;
  }

  ctx->spip->spi->SPI_IDR = SPI_IDR_OVRES;
  aicDisableInt(source);
}

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
