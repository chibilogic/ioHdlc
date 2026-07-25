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
 *          exclusive. DATA_READY is part of the SPI physical protocol: masters
 *          wait for the slave notification before clocking RX data.
 *
 * @note    The adapter installs the SPI completion callbacks at start time.
 * @note    SPI links transmit octets MSB-first, following the usual SPI wire
 *          convention. Timing and select-mode fields remain under caller and
 *          platform control.
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
  bool                          started;    /**< Runtime accepts IRQ activity  */
  const ioHdlcStreamCallbacks  *cbs;        /**< Callbacks registered at start */
  const iohdlc_stream_caps_t   *caps;       /**< Capability descriptor         */

  /* TX state */
  void                         *tx_framep;  /**< Cookie forwarded to on_tx_done */
  bool                          tx_active;  /**< DMA TX in progress             */

  /* RX state */
  uint8_t                      *rx_ptr;     /**< Buffer saved by rx_submit      */
  size_t                        rx_n;       /**< Length of saved RX buffer      */
  iohdlc_rx_mode_t              rx_mode;    /**< Physical RX packet position    */
  bool                          rx_active;  /**< RX transfer active/completing  */
  bool                          rx_allowed; /**< Master can clock current packet */
  bool                          slave_tx_needs_prepare; /**< RX->TX boundary flag */
  bool                          slave_rx_watchdog_gate; /**< RX may timeout      */
  uint16_t                      slave_watchdog_limit_ticks; /**< RX/TX timeout limit */
  uint16_t                      slave_rx_watchdog_ticks; /**< RX timeout ticks    */
  uint16_t                      slave_tx_watchdog_ticks; /**< TX timeout ticks    */
  virtual_timer_t               slave_watchdog_vt; /**< Slave RX/TX guard        */

  /* DATA_READY GPIO line.                                                     */
  /* Master: input monitored via PAL event; slave: output asserted on TX send.  */
  ioline_t                      dr_line;    /**< DATA_READY GPIO line           */
  /* Master only: DATA_READY edge state. */
  bool                          dr_epoch_active; /**< DATA_READY physical epoch */
  bool                          dr_captured; /**< Unconsumed DATA_READY edge     */
  bool                          dr_collision; /**< DR seen while master was TX   */
} ioHdlcStreamChibiosSpi;

/**
 * @brief   Initialises a ChibiOS SPI port object and binds it to @p port.
 *
 * @param[out] port       destination port handle to be bound to this object
 * @param[out] obj        object storage provided by the caller
 * @param[in]  spip       ChibiOS SPI driver instance (e.g. &SPID1)
 * @param[in]  cfgp       SPI configuration; completion callbacks are installed
 *                        by the adapter at start time
 * @param[in]  is_master  true if this node drives the SPI clock
 */
void ioHdlcStreamPortChibiosSpiObjectInit(ioHdlcStreamPort *port,
                                          ioHdlcStreamChibiosSpi *obj,
                                          SPIDriver *spip, SPIConfig *cfgp,
                                          bool is_master, ioline_t dr_line);

/**
 * @brief   Configure the SPI slave RX/TX watchdog delay.
 * @details If not called, the backend uses its compile-time default delay.
 *
 * @param[in,out] obj       SPI stream object
 * @param[in]     delay_us  watchdog delay in microseconds
 */
void ioHdlcStreamSpiSetSlaveWatchdogDelay(ioHdlcStreamChibiosSpi *obj,
                                          uint32_t delay_us);

/**
 * @brief   Called from a PAL event callback on DATA_READY edges.
 * @note    Must be called from ISR context (PAL callback).
 * @param[in] ctx  master SPI context registered for this DATA_READY line.
 */
void ioHdlcStreamSpiDataReadyI(ioHdlcStreamChibiosSpi *ctx);

void ioHdlcStreamSpiSlaveOverrunI(ioHdlcStreamChibiosSpi *ctx);

#endif /* IOHDLCSTREAM_SPI_H */
