/*
 * ChibiOS UART adapter header for HDLC stream port.
 */
#ifndef IOHDLCSTREAM_CHIBIOS_UART_H
#define IOHDLCSTREAM_CHIBIOS_UART_H

#include "ch.h"
#include "hal.h"

#include "ioHdlcstream.h"

typedef struct ioHdlcStreamChibiosUart {
  UARTDriver  *uartp;
  UARTConfig  *cfgp;
  const ioHdlcStreamCallbacks *cbs;
  void          *tx_framep; /* TX in-flight frame pointer */
  bool           rx_busy;   /* RX in progress */
} ioHdlcStreamChibiosUart;

void ioHdlcStreamPortChibiosUartObjectInit(ioHdlcStreamPort *port,
                                           ioHdlcStreamChibiosUart *obj,
                                           UARTDriver *uartp,
                                           UARTConfig *cfgp);

#endif /* IOHDLCSTREAM_CHIBIOS_UART_H */
