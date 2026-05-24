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
 * @brief   STM32G474RE platform hooks for the ChibiOS UART stream backend.
 */

#include "ioHdlcstream_uart_platform.h"

void ioHdlcStreamUartPlatformPrepareConfig(UARTDriver *uartp,
                                           UARTConfig *cfgp) {
  (void)uartp;

#ifdef USART_CR1_IDLEIE
  cfgp->cr1 |= USART_CR1_IDLEIE;
#endif
#ifdef USART_CR1_FIFOEN
  cfgp->cr1 |= USART_CR1_FIFOEN;
#endif
}

void ioHdlcStreamUartPlatformRxCancelCleanup(UARTDriver *uartp) {
  /*
   * On STM32 USARTv3 a framing/overrun condition can leave unread data in the
   * optional RX FIFO. Flush transport-local RX state here so the next one-shot
   * submit starts from a clean hardware boundary.
   */
#if defined(USART_RQR_RXFRQ)
  uartp->usart->RQR = USART_RQR_RXFRQ;
#else
  (void)uartp;
#endif
}
