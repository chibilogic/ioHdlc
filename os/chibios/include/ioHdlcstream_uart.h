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
 * @file    ioHdlcstream_uart.h
 * @brief   ChibiOS UART adapter for HDLC stream port.
 *
 * @details Provides integration between ChibiOS UART driver and ioHdlc stream interface.
 */

#ifndef IOHDLCSTREAM_UART_H
#define IOHDLCSTREAM_UART_H

#include "ch.h"
#include "hal.h"

#include "ioHdlcstreamport.h"

typedef struct ioHdlcStreamChibiosUart {
  UARTDriver  *uartp;
  UARTConfig  *cfgp;
  const ioHdlcStreamCallbacks *cbs;
  void        * volatile tx_framep;        /* TX in-flight frame pointer */
  bool        rx_busy;           /* RX in progress */
} ioHdlcStreamChibiosUart;

void ioHdlcStreamPortChibiosUartObjectInit(ioHdlcStreamPort *port,
                                           ioHdlcStreamChibiosUart *obj,
                                           UARTDriver *uartp,
                                           UARTConfig *cfgp);

#endif /* IOHDLCSTREAM_UART_H */
