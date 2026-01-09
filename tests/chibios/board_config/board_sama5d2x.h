/**
 * @file    board_sama5d2x.h
 * @brief   SAMA5D2x (BV1000GTV) board configuration for test suite.
 * @details Defines UART endpoints for HDLC protocol testing.
 *          Pin configuration is handled by ChibiOS board files.
 */

#ifndef BOARD_SAMA5D2X_H
#define BOARD_SAMA5D2X_H

/*
 * Test console output
 * Uses Serial Driver SD1 for test results and debug messages
 */
#define TEST_OUTPUT_SD    SD1

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
 *   ChibiOS/os/hal/boards/BV1000GTV/board.h
 *   ChibiOS/os/hal/boards/BV1000GTV/board.c
 */
#define TEST_ENDPOINT_A   UARTD2
#define TEST_ENDPOINT_B   FUARTD1

#endif /* BOARD_SAMA5D2X_H */
