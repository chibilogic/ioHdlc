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
 * @brief   Hardware UART adapter for integration testing.
 * @details Uses real UART drivers (UARTD1, UARTD6) for on-target testing.
 *          Requires physical TX/RX cross-connection between endpoints.
 */

#include "ch.h"
#include "hal.h"
#include "adapter_interface.h"
#include "board_config.h"
#include "ioHdlcstream_uart.h"

/*===========================================================================*/
/* Local variables                                                           */
/*===========================================================================*/

/* UART configurations for both endpoints */
static UARTConfig uart_cfg_a = {
  .txend1_cb = NULL,
  .txend2_cb = NULL,
  .rxend_cb = NULL,
  .rxchar_cb = NULL,
  .rxerr_cb = NULL,
  .timeout_cb = NULL,
  .speed = 1200000,
};

static UARTConfig uart_cfg_b = {
  .txend1_cb = NULL,
  .txend2_cb = NULL,
  .rxend_cb = NULL,
  .rxchar_cb = NULL,
  .rxerr_cb = NULL,
  .timeout_cb = NULL,
  .speed = 1200000,
};

/* ioHdlcStream UART context objects */
static ioHdlcStreamChibiosUart uart_endpoint_a_obj;
static ioHdlcStreamChibiosUart uart_endpoint_b_obj;

/* Port structures */
static ioHdlcStreamPort port_a;
static ioHdlcStreamPort port_b;

/*===========================================================================*/
/* Adapter implementation                                                    */
/*===========================================================================*/

static void adapter_uart_init(void) {
  /* Initialize UART endpoint A (UARTD2 - Primary) */
  ioHdlcStreamPortChibiosUartObjectInit(&port_a, 
                                        &uart_endpoint_a_obj,
                                        &TEST_ENDPOINT_A,
                                        &uart_cfg_a);
  
  /* Initialize UART endpoint B (FUARTD1 - Secondary) */
  ioHdlcStreamPortChibiosUartObjectInit(&port_b,
                                        &uart_endpoint_b_obj,
                                        &TEST_ENDPOINT_B,
                                        &uart_cfg_b);
}

static void adapter_uart_deinit(void) {
}

static ioHdlcStreamPort adapter_uart_get_port_a(void) {
  return port_a;
}

static ioHdlcStreamPort adapter_uart_get_port_b(void) {
  return port_b;
}

/*===========================================================================*/
/* Exported adapter                                                          */
/*===========================================================================*/

const test_adapter_t uart_adapter = {
  .name = "UART Hardware",
  .init = adapter_uart_init,
  .deinit = adapter_uart_deinit,
  .reset = NULL,  /* Not needed for hardware */
  .get_port_a = adapter_uart_get_port_a,
  .get_port_b = adapter_uart_get_port_b,
  .configure_error_injection = NULL  /* Not supported on hardware */
};
