/*
 * ioHdlc test board mapping for the MIP104 i.MX95 M7 frontend.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "board.h"

/*
 * Test console output.
 * Uses the board serial console on LPUART2.
 */
#define TEST_OUTPUT_SD        SD2

/*
 * Test SPI bus selection.
 * The local endpoint is the master and samples the remote slave DATA_READY.
 */
#define TEST_SPI_BUS_SISP0    0U
#define TEST_SPI_BUS_SISP1    1U

#ifndef TEST_SPI_BUS
#define TEST_SPI_BUS          TEST_SPI_BUS_SISP0
#endif

#if TEST_SPI_BUS == TEST_SPI_BUS_SISP0
#define TEST_SPI_ENDPOINT_A   SPID3
#define TEST_SPI_DR_LINE_A    LINE_SISP0_DATA_READY
#elif TEST_SPI_BUS == TEST_SPI_BUS_SISP1
#define TEST_SPI_ENDPOINT_A   SPID6
#define TEST_SPI_DR_LINE_A    LINE_SISP1_DATA_READY
#else
#error "TEST_SPI_BUS must select TEST_SPI_BUS_SISP0 or TEST_SPI_BUS_SISP1"
#endif

#ifndef TEST_SPI_BAUD
/* Default baud 5560000U. Tested up to 15390000 */
#define TEST_SPI_BAUD         10000000U
#endif

#endif /* BOARD_CONFIG_H */
