/**
 * @file    adapter_uart.c
 * @brief   Hardware UART adapter for integration testing.
 * @details Uses real UART drivers (UARTD2, FUARTD1) for on-target testing.
 *          Requires physical TX/RX cross-connection between endpoints.
 */

#include "adapter_interface.h"
#include "../board_config/board_config.h"
#include "ioHdlcstream_uart.h"

/*===========================================================================*/
/* Local variables                                                           */
/*===========================================================================*/

/* UART configurations for both endpoints */
static UARTConfig uart_cfg_a = {
  .txend1_cb = NULL,
  .txend2_cb = NULL,
  .rxend_cb = NULL,
  .rxchar_cb = NULL,
  .rxerr_cb = NULL,
  .timeout_cb = NULL,
  .speed = 115200,
  .cr = 0,
  .mr = UART_MR_PAR_NO,
  .timeout = 0
};

static UARTConfig uart_cfg_b = {
  .txend1_cb = NULL,
  .txend2_cb = NULL,
  .rxend_cb = NULL,
  .rxchar_cb = NULL,
  .rxerr_cb = NULL,
  .timeout_cb = NULL,
  .speed = 115200,
  .cr = 0,
  .mr = UART_MR_PAR_NO,
  .timeout = 0
};

/* ioHdlcStream UART context objects */
static ioHdlcStreamChibiosUart uart_endpoint_a_obj;
static ioHdlcStreamChibiosUart uart_endpoint_b_obj;

/* Port structures */
static ioHdlcStreamPort port_a;
static ioHdlcStreamPort port_b;

/*===========================================================================*/
/* Adapter implementation                                                    */
/*===========================================================================*/

static void adapter_uart_init(void) {
  /* Initialize UART endpoint A (UARTD2 - Primary) */
  ioHdlcStreamPortChibiosUartObjectInit(&port_a, 
                                        &uart_endpoint_a_obj,
                                        &TEST_ENDPOINT_A,
                                        &uart_cfg_a);
  
  /* Initialize UART endpoint B (FUARTD1 - Secondary) */
  ioHdlcStreamPortChibiosUartObjectInit(&port_b,
                                        &uart_endpoint_b_obj,
                                        &TEST_ENDPOINT_B,
                                        &uart_cfg_b);
}

static void adapter_uart_deinit(void) {
  /* Stop UART drivers */
  uartStop(&TEST_ENDPOINT_A);
  uartStop(&TEST_ENDPOINT_B);
}

static ioHdlcStreamPort adapter_uart_get_port_a(void) {
  return port_a;
}

static ioHdlcStreamPort adapter_uart_get_port_b(void) {
  return port_b;
}

/*===========================================================================*/
/* Exported adapter                                                          */
/*===========================================================================*/

const test_adapter_t uart_adapter = {
  .name = "UART Hardware (UARTD2 + FUARTD1)",
  .init = adapter_uart_init,
  .deinit = adapter_uart_deinit,
  .get_port_a = adapter_uart_get_port_a,
  .get_port_b = adapter_uart_get_port_b
};
