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
 * @file    ioHdlcstream_uart_platform.c
 * @brief   SAMA5D2x platform hooks for the ChibiOS UART stream backend.
 */

#include "ioHdlcstream_uart_platform.h"

#define IOHDLC_SAMA_UART_RX_TIMEOUT_BITS 30U

void ioHdlcStreamUartPlatformPrepareConfig(UARTDriver *uartp,
                                           UARTConfig *cfgp) {
  (void)uartp;

  if (cfgp->timeout == 0U)
    cfgp->timeout = IOHDLC_SAMA_UART_RX_TIMEOUT_BITS;
}

void ioHdlcStreamUartPlatformRxCancelCleanup(UARTDriver *uartp) {
  (void)uartp;
}
