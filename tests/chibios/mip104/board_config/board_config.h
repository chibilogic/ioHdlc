/*
 * ioHdlc test board mapping for MIP104.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/*
 * Console output.
 * MIP104 board support configures UART1 as SD1.
 */
#define TEST_OUTPUT_SD    SD1

#define TEST_SPI_BAUD     1000000U

#if defined(TEST_SPI_MASTER_FLEXCOM2)
#define TEST_SPI_ENDPOINT_A       FSPID2
#define TEST_SPI_ENDPOINT_B       SPID1
#define TEST_SPI_INPUT_CLOCK_A    SAMA_FLEXCOM2CLK
#define TEST_SPI_DR_LINE_A        LINE_SISP0_IRQ
#define TEST_SPI_DR_LINE_B        LINE_SISP1_IRQ
#define TEST_SPI_ADAPTER_NAME     "SPI FLEXCOM2 master / SPI1 slave"
#elif defined(TEST_SPI_MASTER_SPI1)
#define TEST_SPI_ENDPOINT_A       SPID1
#define TEST_SPI_ENDPOINT_B       FSPID2
#define TEST_SPI_INPUT_CLOCK_A    (SAMA_MCK / SAMA_H64MX_H32MX_RATIO)
#define TEST_SPI_DR_LINE_A        LINE_SISP1_IRQ
#define TEST_SPI_DR_LINE_B        LINE_SISP0_IRQ
#define TEST_SPI_ADAPTER_NAME     "SPI1 master / FLEXCOM2 slave"
#endif

#endif /* BOARD_CONFIG_H */
