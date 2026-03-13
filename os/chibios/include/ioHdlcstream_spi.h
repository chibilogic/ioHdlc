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
 * @file    ioHdlcstream_spi.h
 * @brief   ChibiOS SPI adapter for HDLC stream port.
 *
 * @details Provides integration between ChibiOS SPI driver and ioHdlc stream
 *          interface.  TX and RX DMA operations are mutually exclusive (the
 *          swdriver guarantees TWA half-duplex ordering).
 *
 * @note    The caller must configure @p SPIConfig with a @p NULL end_cb; the
 *          adapter installs its own @p end_cb at start time.
 *
 * @note    REJ must be disabled in the ioHdlc core configuration when using
 *          SPI connections.  Recovery from lost frames happens via
 *          checkpoint retransmission only.
 *
 * @note    FFF (fill-frame forwarding) is strongly recommended; without it
 *          every byte triggers a separate DMA operation.
 */

#ifndef IOHDLCSTREAM_SPI_H
#define IOHDLCSTREAM_SPI_H

#include "ch.h"
#include "hal.h"

#include "ioHdlcstreamport.h"

/**
 * @brief   ChibiOS SPI adapter context.
 */
typedef struct ioHdlcStreamChibiosSpi {
  SPIDriver                    *spip;       /**< ChibiOS SPI driver instance  */
  SPIConfig                    *cfgp;       /**< SPI configuration             */
  bool                          is_master;  /**< true = master, false = slave  */
  const ioHdlcStreamCallbacks  *cbs;        /**< Callbacks registered at start */

  /* TX state */
  void                         *tx_framep;  /**< Cookie forwarded to on_tx_done */
  bool                          tx_active;  /**< DMA TX in progress             */

  /* RX state */
  uint8_t                      *rx_ptr;     /**< Buffer armed by rx_submit      */
  size_t                        rx_n;       /**< Length of armed RX buffer      */
  bool                          rx_active;  /**< DMA RX in progress             */
} ioHdlcStreamChibiosSpi;

/**
 * @brief   Initialises a ChibiOS SPI port object and binds it to @p port.
 *
 * @param[out] port       destination port handle to be bound to this object
 * @param[out] obj        object storage provided by the caller
 * @param[in]  spip       ChibiOS SPI driver instance (e.g. &SPID1)
 * @param[in]  cfgp       SPI configuration; @p end_cb will be overwritten by
 *                        the adapter at start time
 * @param[in]  is_master  true if this node drives the SPI clock
 */
void ioHdlcStreamPortChibiosSpiObjectInit(ioHdlcStreamPort        *port,
                                          ioHdlcStreamChibiosSpi  *obj,
                                          SPIDriver               *spip,
                                          SPIConfig               *cfgp,
                                          bool                     is_master);

#endif /* IOHDLCSTREAM_SPI_H */
