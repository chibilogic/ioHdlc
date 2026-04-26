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
#ifndef BOARD_NUCLEOG474_H
#define BOARD_NUCLEOG474_H
/*
 * Test console output
 * Uses the board VCP on LPUART1 (PA2/PA3).
 */
#define TEST_OUTPUT_SD    LPSD1
/*
 * Test endpoints for HDLC protocol
 * UARTD1: USART1 - Endpoint A (Primary station)
 * UARTD3: USART3 - Endpoint B (Secondary station)
 *
 * Physical connections required:
 *   UARTD1_TX (PA9,  USART1_TX)  <-->  UARTD3_RX (PC11, USART3_RX)
 *   UARTD1_RX (PA10, USART1_RX)  <-->  UARTD3_TX (PC10, USART3_TX)
 *
 * Console VCP: PA2 = LPUART1_TX, PA3 = LPUART1_RX
 */
#define TEST_ENDPOINT_A   UARTD1
#define TEST_ENDPOINT_B   UARTD3

/*
 * Test endpoints for SPI
 * SPID1: SPI1 - Endpoint A (master)
 * SPID2: SPI2 - Endpoint B (slave)
 *
 * Physical connections required (same board loopback):
 *   SPI1_SCK  (PB3)  <-->  SPI2_SCK  (PB13)
 *   SPI1_MISO (PB4)  <-->  SPI2_MISO (PB14)
 *   SPI1_MOSI (PB5)  <-->  SPI2_MOSI (PB15)
 *
 * Optionally, if TEST_SPI_USE_CS is defined:
 *   SPI1_NSS  (PA4)  <-->  SPI2_NSS  (PB12)
 */
#define TEST_SPI_ENDPOINT_A     SPID1
#define TEST_SPI_ENDPOINT_B     SPID2

/*
 * Define TEST_SPI_USE_CS to enable hardware CS (NSS) lines.
 * When undefined, SSM+SSI are used (software slave management,
 * slave always selected) and only 3 wires are needed.
 */
#define TEST_SPI_USE_CS

/* CS (NSS) physical pins - used only when TEST_SPI_USE_CS is defined */
#define TEST_SPI_CS_PORT_A_HW   GPIOA
#define TEST_SPI_CS_PAD_A_HW    4U
#define TEST_SPI_CS_PORT_B_HW   GPIOB
#define TEST_SPI_CS_PAD_B_HW    12U

#if defined(TEST_SPI_USE_CS)
/* Hardware NSS: master drives PA4, slave listens on PB12 */
#define TEST_SPI_CFG_A_CR1      (SPI_CR1_BR_2 | /*SPI_CR1_BR_1 |*/ SPI_CR1_BR_0 | SPI_CR1_SSM | SPI_CR1_SSI)
#define TEST_SPI_CFG_B_CR1      0
#define TEST_SPI_CS_PORT_A      TEST_SPI_CS_PORT_A_HW
#define TEST_SPI_CS_PAD_A       TEST_SPI_CS_PAD_A_HW
#define TEST_SPI_CS_PORT_B      TEST_SPI_CS_PORT_B_HW
#define TEST_SPI_CS_PAD_B       TEST_SPI_CS_PAD_B_HW
#else
/* Software NSS: SSM=1 SSI=1 on master (no MODF), SSM=1 SSI=0 on slave (always selected) */
#define TEST_SPI_CFG_A_CR1      (SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0 | SPI_CR1_SSM | SPI_CR1_SSI)
#define TEST_SPI_CFG_B_CR1      (SPI_CR1_SSM)
#define TEST_SPI_CS_PORT_A      NULL
#define TEST_SPI_CS_PAD_A       0U
#define TEST_SPI_CS_PORT_B      NULL
#define TEST_SPI_CS_PAD_B       0U
#endif

/* CR2: default 8-bit, DMA managed by ChibiOS */
#define TEST_SPI_CFG_A_CR2      0
#define TEST_SPI_CFG_B_CR2      0

/*
 * DATA_READY signal for SPI master/slave synchronization.
 * Used only when IOHDLC_SPI_USE_DR is defined at compile time.
 * Slave asserts this line (high) when it has a frame ready to transmit.
 * Master monitors it via PAL event callback to know when to start receive DMA.
 *
 * Physical connection required:
 *   PA8 (master input)  <-->  PB10 (slave output)
 */
#define TEST_SPI_DR_LINE_A    PAL_LINE(GPIOA, 8U)   /* master: DR input  */
#define TEST_SPI_DR_LINE_B    PAL_LINE(GPIOB, 10U)  /* slave:  DR output */

#endif /* BOARD_NUCLEOG474_H */
