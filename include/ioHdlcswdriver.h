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

  /* Configuration (set by configure()) */
  uint8_t fcs_size;         /* FCS size in bytes (0, 2, 4) */
  bool apply_transparency;  /* Software transparency enabled */
  uint8_t frame_format_size;/* Frame Format Field (0, 1, 2) */

  /* RX state (multi-chunk assembly) */
  uint8_t          *rx_stagep;    /* Staging octet buffer (DMA-safe) */
  iohdlc_frame_t   *rx_in_frame;  /* Current frame being filled, NULL if idle */

  /* RX queue for blocking API */
  iohdlc_sem_t         raw_recept_sem;
  iohdlc_frame_q_t     raw_recept_q;
  IOHDLC_RAWQ_MUTEX_DECLARE(raw_recept_mtx);  /* Mutex protection (Linux only) */

#ifndef IOHDLC_USE_MOCK_ADAPTER
  /* TX queue for ISR processing (real HW only) */
  iohdlc_frame_q_t     raw_tx_q;              /* Unbounded queue (limited by ks) */
#endif

  bool     started;
} ioHdlcSwDriver;

/**
 * @brief   Initialize software HDLC driver.
 * @param[in] drv   Driver instance to initialize
 */
void ioHdlcSwDriverInit(ioHdlcSwDriver *drv);

/**
 * @brief   Stop software HDLC driver.
 * @details Stops port operations (terminates RX thread) and releases resources.
 *          Safe to call multiple times (idempotent).
 * @param[in] drv   Driver instance to stop
 */
static inline void ioHdlcSwDriverStop(ioHdlcSwDriver *drv) {
  hdlcStop((ioHdlcDriver *)drv);
}

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSWDRIVER_H */
