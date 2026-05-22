/*
 * ioHdlc test board mapping for the IMX95 SMARC M7 frontend.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "board.h"

/*
 * Test console output.
 * Uses the board serial console on LPUART3.
 */
#define TEST_OUTPUT_SD        SD3

/*
 * Test SPI endpoint.
 * LPSPI6 is the only endpoint currently exposed by this frontend.
 */
#define TEST_SPI_ENDPOINT_A   SPID6

/*
 * DATA_READY signal for SPI master/slave synchronization.
 * The local endpoint is the master and samples the remote slave line here.
 */
#define TEST_SPI_DR_LINE_A    LINE_SPI_DATA_READY

#ifndef TEST_SPI_BAUD
#define TEST_SPI_BAUD         5350000U
#endif

#endif /* BOARD_CONFIG_H */
