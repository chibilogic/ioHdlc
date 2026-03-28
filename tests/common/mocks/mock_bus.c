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
 * @file    mock_bus.c
 * @brief   Mock shared bus implementation.
 */

#include "mock_bus.h"
#include <string.h>

void mock_bus_init(mock_bus_t *bus, uint8_t num_ports,
                   const mock_stream_config_t *config) {
  if (!bus || num_ports == 0 || num_ports > MOCK_BUS_MAX_PORTS) {
    return;
  }

  memset(bus, 0, sizeof(*bus));
  bus->num_ports = num_ports;

  /* Initialize all streams. */
  for (uint8_t i = 0; i < num_ports; i++) {
    mock_stream_init(&bus->ports[i], config);
  }

  /* Cross-connect: every port sees all others (broadcast). */
  for (uint8_t i = 0; i < num_ports; i++) {
    for (uint8_t j = 0; j < num_ports; j++) {
      if (i != j) {
        mock_stream_add_peer(&bus->ports[i], &bus->ports[j]);
      }
    }
  }

  /* Initialize adapters. */
  for (uint8_t i = 0; i < num_ports; i++) {
    mock_stream_adapter_init(&bus->adapters[i], &bus->ports[i]);
  }
}

void mock_bus_deinit(mock_bus_t *bus) {
  if (!bus) {
    return;
  }

  for (uint8_t i = 0; i < bus->num_ports; i++) {
    mock_stream_adapter_deinit(&bus->adapters[i]);
  }

  for (uint8_t i = 0; i < bus->num_ports; i++) {
    mock_stream_disconnect(&bus->ports[i]);
  }

  for (uint8_t i = 0; i < bus->num_ports; i++) {
    mock_stream_deinit(&bus->ports[i]);
  }
}

mock_stream_t *mock_bus_get_stream(mock_bus_t *bus, uint8_t index) {
  if (!bus || index >= bus->num_ports) {
    return NULL;
  }
  return &bus->ports[index];
}

ioHdlcStreamPort mock_bus_get_port(mock_bus_t *bus, uint8_t index) {
  ioHdlcStreamPort port = {0};
  if (!bus || index >= bus->num_ports) {
    return port;
  }
  return mock_stream_adapter_get_port(&bus->adapters[index]);
}

void mock_bus_clear(mock_bus_t *bus) {
  if (!bus) {
    return;
  }
  for (uint8_t i = 0; i < bus->num_ports; i++) {
    mock_stream_clear(&bus->ports[i]);
  }
}
