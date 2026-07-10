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
 * @file    adapter_uart.c
 * @brief   BV9000CL single-port UART adapter for shell exchange tests.
 */

#include "ch.h"
#include "hal.h"
#include "adapter_interface.h"
#include "board_config.h"
#include "ioHdlcstream_uart.h"

static UARTConfig uart_cfg = {
  .txend1_cb = NULL,
  .txend2_cb = NULL,
  .rxend_cb = NULL,
  .rxchar_cb = NULL,
  .rxerr_cb = NULL,
  .timeout_cb = NULL,
  .timeout = 0U,
  .speed = 115200U,
  .cr = 0U,
  .mr = UART_MR_PAR_NO,
};

static ioHdlcStreamChibiosUart uart_obj;
static ioHdlcStreamPort port;
static bool port_claimed;

static void adapter_uart_init(void) {
  port_claimed = false;
  ioHdlcStreamPortChibiosUartObjectInit(&port, &uart_obj, &TEST_ENDPOINT_A,
                                        &uart_cfg);
}

static void adapter_uart_deinit(void) {
  port_claimed = false;
}

static ioHdlcStreamPort adapter_uart_claim_port(void) {
  ioHdlcStreamPort empty = {0};

  if (port_claimed)
    return empty;

  port_claimed = true;
  return port;
}

const test_adapter_t uart_adapter = {
  .name = "UART3 Hardware",
  .init = adapter_uart_init,
  .deinit = adapter_uart_deinit,
  .reset = NULL,
  .configure_timing = NULL,
  .get_port_a = adapter_uart_claim_port,
  .get_port_b = adapter_uart_claim_port,
  .configure_error_injection = NULL,
  .constraints = 0U,
};
