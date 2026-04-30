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
 * @file    ioHdlcstream_spi.h
 * @brief   ChibiOS SPI adapter for HDLC stream port.
 *
 * @details Provides the SPI-backed @ref ioHdlcStreamPort context used by the
 *          software HDLC driver.  TX and RX DMA operations are mutually
 *          exclusive.
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
  SPIDriver                    *spip;       /**< ChibiOS SPI driver instance   */
  SPIConfig                    *cfgp;       /**< SPI configuration             */
  bool                          is_master;  /**< true = master, false = slave  */
  const ioHdlcStreamCallbacks  *cbs;        /**< Callbacks registered at start */
  const iohdlc_stream_caps_t   *caps;       /**< Capability descriptor         */

  /* TX state */
  void                         *tx_framep;  /**< Cookie forwarded to on_tx_done */
  bool                          tx_active;  /**< DMA TX in progress             */

  /* RX state */
  uint8_t                      *rx_ptr;     /**< Buffer armed by rx_submit      */
  size_t                        rx_n;       /**< Length of armed RX buffer      */
  bool                          rx_active;  /**< DMA RX in progress             */
  bool                          slave_tx_needs_prepare; /**< RX->TX boundary flag */

  /* DATA_READY GPIO line (optional, see IOHDLC_SPI_USE_DR).                    */
  /* Master: input monitored via PAL event; slave: output asserted on TX send.  */
  /* Set to PAL_NOLINE if DATA_READY signalling is not used.                    */
  ioline_t                      dr_line;    /**< DATA_READY GPIO line           */
  bool                          dr_armed;   /**< true = next DR edge starts RX  */
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
                                          bool                     is_master,
                                          ioline_t                 dr_line);

#if defined(IOHDLC_SPI_USE_DR)
/**
 * @brief   Called from a PAL event callback when the slave DATA_READY line
 *          goes high.  Disarms the edge interrupt and starts SPI DMA receive.
 * @note    Must be called from ISR context (PAL callback).
 * @param[in] ctx  master SPI context registered for this DATA_READY line.
 */
void ioHdlcStreamSpiDataReadyI(ioHdlcStreamChibiosSpi *ctx);
#endif

#endif /* IOHDLCSTREAM_SPI_H */
