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
 * @file    mock_bus.h
 * @brief   Mock shared bus for multipoint testing (RS-485 style).
 * @details Provides N ports on a broadcast bus: a write on any port is
 *          delivered to the RX buffer of all other ports.
 */

#ifndef MOCK_BUS_H
#define MOCK_BUS_H

#include "mock_stream.h"
#include "mock_stream_adapter.h"
#include "ioHdlcstreamport.h"

/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

#define MOCK_BUS_MAX_PORTS  8

/*===========================================================================*/
/* Types                                                                     */
/*===========================================================================*/

/**
 * @brief   Mock bus instance (statically allocated).
 */
typedef struct {
  mock_stream_t         ports[MOCK_BUS_MAX_PORTS];
  mock_stream_adapter_t adapters[MOCK_BUS_MAX_PORTS];
  uint8_t               num_ports;
} mock_bus_t;

/*===========================================================================*/
/* API Functions                                                             */
/*===========================================================================*/

/**
 * @brief   Initialize mock bus with N ports.
 * @details Creates N mock streams cross-connected in broadcast topology
 *          and initializes an adapter for each.
 * @param[in] bus        Pre-allocated bus structure.
 * @param[in] num_ports  Number of ports (1..MOCK_BUS_MAX_PORTS).
 * @param[in] config     Optional stream config applied to all ports (NULL = default).
 */
void mock_bus_init(mock_bus_t *bus, uint8_t num_ports,
                   const mock_stream_config_t *config);

/**
 * @brief   Deinitialize mock bus and all its ports.
 * @param[in] bus    Bus to deinitialize.
 */
void mock_bus_deinit(mock_bus_t *bus);

/**
 * @brief   Get the mock stream for a port.
 * @param[in] bus    Bus instance.
 * @param[in] index  Port index (0..num_ports-1).
 * @return           Pointer to mock stream, or NULL if index out of range.
 */
mock_stream_t *mock_bus_get_stream(mock_bus_t *bus, uint8_t index);

/**
 * @brief   Get the ioHdlcStreamPort for a port.
 * @param[in] bus    Bus instance.
 * @param[in] index  Port index (0..num_ports-1).
 * @return           Stream port structure for ioHdlcSwDriver.
 */
ioHdlcStreamPort mock_bus_get_port(mock_bus_t *bus, uint8_t index);

/**
 * @brief   Clear all buffers on all ports.
 * @param[in] bus    Bus instance.
 */
void mock_bus_clear(mock_bus_t *bus);

#endif /* MOCK_BUS_H */
