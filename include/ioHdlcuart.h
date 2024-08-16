/*
    ioHdlc - Copyright (C) 2024 Isidoro Orabona

    GNU General Public License Usage

    ioHdlc software is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ioHdlc software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with ioHdlc software.  If not, see <http://www.gnu.org/licenses/>.

    Commercial License Usage

    Licensees holding valid commercial ioHdlc licenses may use this file in
    accordance with the commercial license agreement provided in accordance with
    the terms contained in a written agreement between you and Isidoro Orabona.
    For further information contact via email on github account.
 */

/**
 * @file    include/ioHdlcuart.h
 * @brief   HDLC driver implementation using UART.
 * @details This implementation is a ChibiOS HAL dependent implementation of
 *          the HDLC driver interface using the ChibiOS UART driver.
 *
 * @addtogroup hdlc_drivers
 * @{
 */

#ifndef IOHDLCUART_H_
#define IOHDLCUART_H_

/**
 * flags defines
 */
#define HDLC_UART_HASFF   0x0001
#define HDLC_UART_TRANS   0x0002
#define HDLC_UART_ERROR   0x0004

/**
 * @extends @p ioHdclDriver
 *
 * @brief   @p ioHdclUartDriver specific methods.
 */
#define _iohdlc_uart_driver_methods    \
  _iohdlc_driver_methods

#define _iohdlc_uart_driver_data  \
  _iohdlc_driver_data             \
  UARTDriver *uartp;              /* ChibiOS UART driver. */                \
  uint8_t flags;                  /* see flags defines. */                  \
  semaphore_t raw_recept_sem;     /* received raw frames semaphore/count.*/ \
  iohdlc_frame_q_t raw_recept_q;  /* queue of received raw frames.*/        \
  binary_semaphore_t tx_on_air;   /* transmission is in progress. */        \
  iohdlc_frame_t *frameinrx;      /* frame currently in reception. */       \
  iohdlc_frame_t *frameintx;      /* frame currently in transmission. */    \
  uint8_t flagoctet;              /* used to receive flag octet. */         \

/**
 * @extends _iohdlc_driver_vmt
 *
 * @brief   @p ioHdclUartDriver specific methods.
 */
struct _iohdlc_uart_driver_vmt {
  _iohdlc_uart_driver_methods
};

struct ioHdclUartDriver {
  const struct _iohdlc_uart_driver_vmt *vmt;
  _iohdlc_uart_driver_data
};

typedef struct ioHdclUartDriver ioHdclUartDriver;

#ifdef __cplusplus
extern "C" {
#endif
  void ioHdclUartDriverInit(ioHdclUartDriver *uhp);
#ifdef __cplusplus
}
#endif

#endif /* IOHDLCUART_H_ */
