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
 * @file    adapter_mock.c
 * @brief   Mock stream adapter for unit testing.
 * @details Uses memory-based mock streams for testing without hardware.
 */

#include "adapter_interface.h"
#include "mock_stream.h"

/*===========================================================================*/
/* Local variables                                                           */
/*===========================================================================*/

static mock_stream_t mock_endpoint_a;
static mock_stream_t mock_endpoint_b;

/* Port operations for mock stream (to be implemented) */
static const ioHdlcStreamPortOps mock_stream_ops;

/*===========================================================================*/
/* Adapter implementation                                                    */
/*===========================================================================*/

static void adapter_mock_init(void) {
  /* Initialize both mock endpoints */
  mock_stream_init(&mock_endpoint_a, NULL);
  mock_stream_init(&mock_endpoint_b, NULL);
  
  /* Connect them bidirectionally */
  mock_stream_connect(&mock_endpoint_a, &mock_endpoint_b);
}

static void adapter_mock_deinit(void) {
  /* Disconnect streams */
  mock_stream_disconnect(&mock_endpoint_a);
  mock_stream_disconnect(&mock_endpoint_b);
  
  /* Deinitialize */
  mock_stream_deinit(&mock_endpoint_a);
  mock_stream_deinit(&mock_endpoint_b);
}

static ioHdlcStreamPort adapter_mock_get_port_a(void) {
  return (ioHdlcStreamPort){
    .ctx = &mock_endpoint_a,
    .ops = &mock_stream_ops
  };
}

static ioHdlcStreamPort adapter_mock_get_port_b(void) {
  return (ioHdlcStreamPort){
    .ctx = &mock_endpoint_b,
    .ops = &mock_stream_ops
  };
}

/*===========================================================================*/
/* Exported adapter                                                          */
/*===========================================================================*/

const test_adapter_t mock_adapter = {
  .name = "Mock Stream (memory-based)",
  .init = adapter_mock_init,
  .deinit = adapter_mock_deinit,
  .get_port_a = adapter_mock_get_port_a,
  .get_port_b = adapter_mock_get_port_b
};
