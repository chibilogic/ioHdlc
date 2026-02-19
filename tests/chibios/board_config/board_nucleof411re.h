/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU Lesser General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
 */
#ifndef BOARD_NUCLEOF411_H
#define BOARD_NUCLEOF411_H
/*
 * Test console output
 * Uses Serial Driver SD1 for test results and debug messages
 */
#define TEST_OUTPUT_SD    SD2
/*
 * Test endpoints for HDLC protocol
 * UARTD1: UART1 - Endpoint A (Primary station)
 * UARTD6: UART6 - Endpoint B (Secondary station)
 * 
 * Physical connections required:
 *   UARTD1_TX  <-->  UARTD6_RX
 *   UARTD1_RX  <-->  UARTD6_TX
 * 
 * Pin mappings are defined in:
 */
#define TEST_ENDPOINT_A   UARTD1
#define TEST_ENDPOINT_B   UARTD6
#endif /* BOARD_NUCLEOF411_H */
