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
 * @file    include/ioHdlcuart_chibios.h
 * @brief   Public API for the ChibiOS adapter of the UART DMA interface.
 * @details This header exposes the adapter object storage and the initializer
 *          used to bind a ChibiOS UART into the generic ioHdlcUartPort.
 */

#ifndef IOHDLCUART_CHIBIOS_H
#define IOHDLCUART_CHIBIOS_H

#include "ch.h"
#include "hal.h"

#include "ioHdlcuart_new.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Adapter object storage for the ChibiOS UART backend.
 * @note  The fields are exposed to allow static allocation; treat them as
 *        implementation details.
 */
typedef struct ioHdlcUartChibios {
  UARTDriver  *uartp;
  UARTConfig  *cfgp;

  const ioHdlcUartCallbacks *cbs;

  /* TX tracking */
  void          *tx_cookie;

  /* RX tracking (only cookie used to detect in-flight transfer) */
  void    *rx_cookie;
} ioHdlcUartChibios;

/**
 * @brief   Initializes a ChibiOS UART port object and binds ops/ctx.
 *
 * @param[out] port   destination port handle to be bound to this object
 * @param[out] obj    adapter object storage (provided by the caller)
 * @param[in]  uartp  ChibiOS UART driver instance (e.g. &UARTD1)
 * @param[in]  cfgp   UART configuration to be used (callbacks will be set)
 */
void ioHdlcUartPortChibiosObjectInit(ioHdlcUartPort *port,
                                     ioHdlcUartChibios *obj,
                                     UARTDriver *uartp,
                                     UARTConfig *cfgp);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCUART_CHIBIOS_H */
