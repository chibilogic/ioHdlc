/*
 * ioHdlc test board mapping for BV9000CL.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/*
 * Console output.
 * BV9000CL board support configures UART1 as SD1.
 */
#define TEST_OUTPUT_SD    SD1

/*
 * ioHdlc UART endpoint.
 * UART3 is exposed through UARTD3. This frontend has one physical HDLC port,
 * so run the shell exchange test as either --endpoint=a or --endpoint=b.
 */
#define TEST_ENDPOINT_A   UARTD3
#define TEST_ENDPOINT_B   UARTD3

#endif /* BOARD_CONFIG_H */
