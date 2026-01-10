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

#include "ioHdlcosal.h"
#include "ioHdlcdriver.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlctypes.h"

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
