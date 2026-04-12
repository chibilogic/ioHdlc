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
 * @file    adapter_spi.c
 * @brief   Hardware SPI adapter for integration testing.
 * @details Uses real SPI drivers for on-target testing.
 *          Requires physical MOSI/MISO/SCK cross-connection between the two
 *          SPI endpoints, with CS lines handled by NSS hardware (or fixed low).
 *
 *          Board-specific endpoint defines are in board_config.h:
 *            TEST_SPI_ENDPOINT_A   - SPIDriver for primary station (master)
 *            TEST_SPI_ENDPOINT_B   - SPIDriver for secondary station (slave)
 *            TEST_SPI_CFG_A_CR1    - SPI_CR1 value for endpoint A
 *            TEST_SPI_CFG_B_CR1    - SPI_CR1 value for endpoint B
 *            TEST_SPI_CFG_A_CR2    - SPI_CR2 value for endpoint A
 *            TEST_SPI_CFG_B_CR2    - SPI_CR2 value for endpoint B
 *            TEST_SPI_CS_PORT_A    - CS GPIO port for endpoint A
 *            TEST_SPI_CS_PAD_A     - CS GPIO pad for endpoint A
 *            TEST_SPI_CS_PORT_B    - CS GPIO port for endpoint B
 *            TEST_SPI_CS_PAD_B     - CS GPIO pad for endpoint B
 */

#include "ch.h"
#include "hal.h"
#include "adapter_interface.h"
#include "board_config.h"
#include "ioHdlcstream_spi.h"

/*===========================================================================*/
/* Local variables                                                           */
/*===========================================================================*/

/* SPI configurations for both endpoints */
static SPIConfig spi_cfg_a = {
  .circular = false,
  .slave    = false,    /* master */
  .data_cb  = NULL,     /* overwritten by adapter at start */
  .error_cb = NULL,     /* overwritten by adapter at start */
  .ssport   = TEST_SPI_CS_PORT_A,
  .sspad    = TEST_SPI_CS_PAD_A,
  .cr1      = TEST_SPI_CFG_A_CR1,
  .cr2      = TEST_SPI_CFG_A_CR2,
};

static SPIConfig spi_cfg_b = {
  .circular = false,
  .slave    = true,     /* slave */
  .data_cb  = NULL,     /* overwritten by adapter at start */
  .error_cb = NULL,     /* overwritten by adapter at start */
  .ssport   = TEST_SPI_CS_PORT_B,
  .sspad    = TEST_SPI_CS_PAD_B,
  .cr1      = TEST_SPI_CFG_B_CR1,
  .cr2      = TEST_SPI_CFG_B_CR2,
};

/* ioHdlcStream SPI context objects */
static ioHdlcStreamChibiosSpi spi_endpoint_a_obj;
static ioHdlcStreamChibiosSpi spi_endpoint_b_obj;

/* Port structures */
static ioHdlcStreamPort port_a;
static ioHdlcStreamPort port_b;

/*===========================================================================*/
/* Adapter implementation                                                    */
/*===========================================================================*/

#if defined(IOHDLC_SPI_USE_DR)
static void spi_dr_callback(void *arg) {
  /* Called from PAL/EXTI ISR when slave asserts DATA_READY.
   * I-class functions inside DataReadyI require the system lock. */
  chSysLockFromISR();
  ioHdlcStreamSpiDataReadyI((ioHdlcStreamChibiosSpi *)arg);
  chSysUnlockFromISR();
}
#endif

static void adapter_spi_init(void) {
  /* Endpoint A: SPI master */
  ioHdlcStreamPortChibiosSpiObjectInit(&port_a,
                                       &spi_endpoint_a_obj,
                                       &TEST_SPI_ENDPOINT_A,
                                       &spi_cfg_a,
                                       true,              /* is_master */
#if defined(IOHDLC_SPI_USE_DR)
                                       TEST_SPI_DR_LINE_A /* DR input  */
#else
                                       PAL_NOLINE
#endif
                                       );

  /* Endpoint B: SPI slave */
  ioHdlcStreamPortChibiosSpiObjectInit(&port_b,
                                       &spi_endpoint_b_obj,
                                       &TEST_SPI_ENDPOINT_B,
                                       &spi_cfg_b,
                                       false,             /* is_slave  */
#if defined(IOHDLC_SPI_USE_DR)
                                       TEST_SPI_DR_LINE_B /* DR output */
#else
                                       PAL_NOLINE
#endif
                                       );

#if defined(IOHDLC_SPI_USE_DR)
  /* Register DATA_READY callback and keep EXTI permanently armed.
   * The driver uses dr_armed flag to gate the callback — no
   * palDisableLineEventI/palEnableLineEventI calls are made, so the
   * PAL _pal_events entry is never cleared by _pal_clear_event(). */
  palSetLineCallback(TEST_SPI_DR_LINE_A, spi_dr_callback, &spi_endpoint_a_obj);
  palEnableLineEvent(TEST_SPI_DR_LINE_A, PAL_EVENT_MODE_RISING_EDGE);
#endif
}

static void adapter_spi_deinit(void) {
#if defined(IOHDLC_SPI_USE_DR)
  palDisableLineEvent(TEST_SPI_DR_LINE_A);
#endif
}

static ioHdlcStreamPort adapter_spi_get_port_a(void) {
  return port_a;
}

static ioHdlcStreamPort adapter_spi_get_port_b(void) {
  return port_b;
}

/*===========================================================================*/
/* Exported adapter                                                          */
/*===========================================================================*/

const test_adapter_t spi_adapter = {
  .name                    =  "SPI Hardware",
  .init                    =  adapter_spi_init,
  .deinit                  =  adapter_spi_deinit,
  .reset                   =  NULL,
  .get_port_a              =  adapter_spi_get_port_a,
  .get_port_b              =  adapter_spi_get_port_b,
  .configure_error_injection = NULL,
  .constraints             =  ADAPTER_CONSTRAINT_TWA_ONLY|
                              ADAPTER_CONSTRAINT_NRM_ONLY,
};
