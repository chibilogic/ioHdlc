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
 * @brief   Hardware SPI adapter for the MIP104 i.MX95 M7 frontend.
 */

#include "ch.h"
#include "hal.h"
#include "adapter_interface.h"
#include "board_config.h"
#include "ioHdlcstream_spi.h"

static SPIConfig spi_cfg_a = {
  .data_cb  = NULL,
  .error_cb = NULL,
  .filler   = 0xFFU,
  .cfgr1    = LPSPI_CFGR1_OUTCFG(1U),
  .ccr      = 0U,
  .ccr1     = 0U,
  .fcr      = LPSPI_FCR_TXWATER(0U) | LPSPI_FCR_RXWATER(0U),
  .tcr      = LPSPI_TCR_PCS(0U)
};

static ioHdlcStreamChibiosSpi spi_endpoint_a_obj;
static ioHdlcStreamPort port_a;

static void adapter_spi_init(void) {
  uint32_t actual_baud;

  actual_baud = spi_lld_config_set_timing(&TEST_SPI_ENDPOINT_A, &spi_cfg_a,
                                          TEST_SPI_BAUD);
  chDbgAssert(actual_baud != 0U, "invalid LPSPI timing");

  ioHdlcStreamPortChibiosSpiObjectInit(&port_a,
                                       &spi_endpoint_a_obj,
                                       &TEST_SPI_ENDPOINT_A,
                                       &spi_cfg_a,
                                       true,
                                       TEST_SPI_DR_LINE_A);
}

static ioHdlcStreamPort adapter_spi_get_port_a(void) {
  return port_a;
}

static ioHdlcStreamPort adapter_spi_get_port_b(void) {
  ioHdlcStreamPort port = {0};

  return port;
}

const test_adapter_t spi_adapter = {
  .name = "SPI Hardware",
  .init = adapter_spi_init,
  .deinit = NULL,
  .reset = NULL,
  .configure_timing = NULL,
  .get_port_a = adapter_spi_get_port_a,
  .get_port_b = adapter_spi_get_port_b,
  .configure_error_injection = NULL,
  .constraints = ADAPTER_CONSTRAINT_TWA_ONLY |
                 ADAPTER_CONSTRAINT_NRM_ONLY,
};
