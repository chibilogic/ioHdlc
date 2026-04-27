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
 * @file    ioHdlcstream_uart_platform.h
 * @brief   Platform hooks for the ChibiOS UART stream backend.
 */

#ifndef IOHDLCSTREAM_UART_PLATFORM_H
#define IOHDLCSTREAM_UART_PLATFORM_H

#include "ch.h"
#include "hal.h"

void ioHdlcStreamUartPlatformPrepareConfig(UARTDriver *uartp,
                                           UARTConfig *cfgp);
void ioHdlcStreamUartPlatformRxCancelCleanup(UARTDriver *uartp);

#endif /* IOHDLCSTREAM_UART_PLATFORM_H */
