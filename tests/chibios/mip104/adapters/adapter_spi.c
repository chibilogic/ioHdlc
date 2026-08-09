/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 */
/**
 * @file    adapter_spi.c
 * @brief   MIP104 dual-port SPI adapter for shell exchange tests.
 */

#include "ch.h"
#include "hal.h"
#include "adapter_interface.h"
#include "board_config.h"
#include "ioHdlcstream_spi.h"

#define TEST_SPI_SCBR \
  ((TEST_SPI_INPUT_CLOCK_A + TEST_SPI_BAUD - 1U) / TEST_SPI_BAUD)

static SPIConfig spi_cfg_a = {
  .end_cb = NULL,
  .npcs = 0U,
  .mr = SPI_MR_MODFDIS,
  .csr = SPI_CSR_NCPHA | SPI_CSR_SCBR(TEST_SPI_SCBR),
  .slave = false,
};

static SPIConfig spi_cfg_b = {
  .end_cb = NULL,
  .npcs = 0U,
  .mr = 0U,
  .csr = SPI_CSR_NCPHA,
  .slave = true,
};

static ioHdlcStreamChibiosSpi spi_endpoint_a_obj;
static ioHdlcStreamChibiosSpi spi_endpoint_b_obj;
static ioHdlcStreamPort port_a;
static ioHdlcStreamPort port_b;

static void adapter_spi_init(void) {
  ioHdlcStreamPortChibiosSpiObjectInit(&port_a, &spi_endpoint_a_obj,
                                       &TEST_SPI_ENDPOINT_A, &spi_cfg_a,
                                       true, TEST_SPI_DR_LINE_A);
  ioHdlcStreamPortChibiosSpiObjectInit(&port_b, &spi_endpoint_b_obj,
                                       &TEST_SPI_ENDPOINT_B, &spi_cfg_b,
                                       false, TEST_SPI_DR_LINE_B);
}

static void adapter_spi_configure_timing(uint32_t reply_timeout_ms) {
  ioHdlcStreamSpiSetSlaveWatchdogDelay(&spi_endpoint_b_obj,
                                       reply_timeout_ms * 1000U / 2U);
}

static ioHdlcStreamPort adapter_spi_get_port_a(void) {
  return port_a;
}

static ioHdlcStreamPort adapter_spi_get_port_b(void) {
  return port_b;
}

const test_adapter_t spi_adapter = {
  .name = TEST_SPI_ADAPTER_NAME,
  .init = adapter_spi_init,
  .deinit = NULL,
  .reset = NULL,
  .configure_timing = adapter_spi_configure_timing,
  .get_port_a = adapter_spi_get_port_a,
  .get_port_b = adapter_spi_get_port_b,
  .configure_error_injection = NULL,
  .constraints = ADAPTER_CONSTRAINT_TWA_ONLY |
                 ADAPTER_CONSTRAINT_NRM_ONLY,
};
