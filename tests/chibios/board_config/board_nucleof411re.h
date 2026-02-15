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
 * UARTD2:  UART2 - Endpoint A (Primary station)
 * FUARTD1: FLEXCOM1 UART - Endpoint B (Secondary station)
 * 
 * Physical connections required:
 *   UARTD2_TX  <-->  FUARTD1_RX
 *   UARTD2_RX  <-->  FUARTD1_TX
 * 
 * Pin mappings are defined in:
 */
#define TEST_ENDPOINT_A   UARTD2
#define TEST_ENDPOINT_B   FUARTD1
#endif /* BOARD_NUCLEOF411_H */
