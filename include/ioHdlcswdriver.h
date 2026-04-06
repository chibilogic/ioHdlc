/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU General Public License v3.0 (or later)
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
 *          This header documents the reference software driver that bridges the
 *          framed driver contract to a byte-stream transport.
 *
 *          Intended use:
 *          - configure the driver before starting it;
 *          - bind it to a stream-port implementation and a frame pool at start;
 *          - let the stream backend deliver RX/TX/error callbacks;
 *          - stop it to release runtime resources owned by the driver.
 *
 *          This is a software reference implementation. It is useful both as a
 *          production driver on simple targets and as documentation of the
 *          expected behaviour of custom driver implementations.
 *
 *          Execution model notes:
 *          - callback context is inherited from the selected stream backend;
 *          - the blocking receive API is layered on top of the internal RX
 *            queue and synchronization objects stored in this driver.
 *
 * @addtogroup ioHdlc_drivers
 * @{
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
 *          The structure stores both persistent configuration and runtime state.
 *
 *          Ownership notes:
 *          - @p port is copied into the driver at start time;
 *          - @p rx_stagep is allocated and freed by the driver;
 *          - frames queued in RX/TX paths remain subject to frame-pool
 *            reference management.
 *
 *          Callers should treat the runtime fields as internal implementation
 *          state even though the structure is visible in the header.
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
  iohdlc_sem_t         tx_progress_sem;       /* Signaled on each TX completion */
  iohdlc_frame_t      *tx_inflight_fp;        /* Frame currently owned by HW */
#endif

  bool     started;
} ioHdlcSwDriver;

/** @ingroup ioHdlc_drivers */
void ioHdlcSwDriverInit(ioHdlcSwDriver *drv);

bool ioHdlcSwDriverIsFrameTxOwned(ioHdlcSwDriver *drv,
                                  const iohdlc_frame_t *fp);
void ioHdlcSwDriverWaitTxProgress(ioHdlcSwDriver *drv, uint32_t timeout_ms);

/**
 * @brief   Stop software HDLC driver.
 * @details Stops port operations (terminates RX thread) and releases resources.
 *          Safe to call multiple times (idempotent).
 * @param[in] drv   Driver instance to stop.
 */
static inline void ioHdlcSwDriverStop(ioHdlcSwDriver *drv) {
  hdlcStop((ioHdlcDriver *)drv);
}

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSWDRIVER_H */

/** @} */
