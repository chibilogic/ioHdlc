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
 * @file    include/ioHdlcswdriver.h
 * @brief   HDLC software driver (unified framing + protocol logic).
 * @details Implements ioHdlcDriver interface with software transparency and FCS.
 *          Uses ioHdlcStreamPort for transport abstraction (UART, SPI, Mock, etc).
 */

#ifndef IOHDLCSWDRIVER_H
#define IOHDLCSWDRIVER_H

#include "ioHdlcdriver.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcqueue.h"
#include "ioHdlcll.h"
#include "ioHdlcstreamport.h"
#include "ioHdlcosal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   HDLC software driver structure.
 * @details Implements complete HDLC protocol with software transparency and FCS.
 *          Integrates RX multi-chunk state machine, protocol logic, and blocking API.
 */
typedef struct ioHdlcSwDriver {
  /* ioHdlcDriver interface */
  const struct _iohdlc_driver_vmt *vmt;
  _iohdlc_driver_data

  /* Port abstraction */
  ioHdlcStreamPort    port;
  ioHdlcStreamCallbacks hal_cbs;

  /* Configuration */
  bool apply_transparency;
  bool has_frame_format;

  /* RX state (multi-chunk assembly) */
  uint8_t          *rx_stagep;    /* Staging octet buffer (DMA-safe) */
  iohdlc_frame_t   *rx_in_frame;  /* Current frame being filled, NULL if idle */

  /* RX queue for blocking API */
  iohdlc_sem_t         raw_recept_sem;
  iohdlc_frame_q_t     raw_recept_q;

  bool     started;
} ioHdlcSwDriver;

/**
 * @brief   Initialize software HDLC driver.
 * @param[in] drv   Driver instance to initialize
 */
void ioHdlcSwDriverInit(ioHdlcSwDriver *drv);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSWDRIVER_H */
