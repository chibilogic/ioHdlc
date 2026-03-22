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
 * @file    adapter_mock.c
 * @brief   Unified mock stream adapter for testing (all platforms).
 * @details Provides mock stream backend with optional error injection.
 *          Uses static allocation - works on both Linux and ChibiOS.
 */

#include "adapter_interface.h"
#include "mock_stream.h"
#include "mock_stream_adapter.h"

/*===========================================================================*/
/* Local variables                                                           */
/*===========================================================================*/

static mock_stream_t stream_endpoint_a;
static mock_stream_t stream_endpoint_b;
static mock_stream_adapter_t adapter_endpoint_a;
static mock_stream_adapter_t adapter_endpoint_b;
static unsigned int configured_error_rate = 0;  /* 0-100% */

/*===========================================================================*/
/* Adapter implementation                                                    */
/*===========================================================================*/

static void adapter_mock_init(void) {
  /* Configuration with optional error injection */
  mock_stream_config_t stream_config = {
    .loopback = false,
    .inject_errors = (configured_error_rate > 0),
    .error_rate = configured_error_rate * 10,  /* Convert percent to permille */
    .delay_us = 0,
    .error_filter = NULL,
    .error_userdata = NULL
  };
  
  /* Initialize both mock endpoints with static allocation */
  mock_stream_init(&stream_endpoint_a, &stream_config);
  mock_stream_init(&stream_endpoint_b, &stream_config);
  
  /* Connect streams bidirectionally */
  mock_stream_connect(&stream_endpoint_a, &stream_endpoint_b);
  
  /* Initialize adapters with static allocation */
  mock_stream_adapter_init(&adapter_endpoint_a, &stream_endpoint_a);
  mock_stream_adapter_init(&adapter_endpoint_b, &stream_endpoint_b);
}

static void adapter_mock_deinit(void) {
  /* Deinitialize adapters */
  mock_stream_adapter_deinit(&adapter_endpoint_a);
  mock_stream_adapter_deinit(&adapter_endpoint_b);
  
  /* Disconnect and deinitialize streams */
  mock_stream_disconnect(&stream_endpoint_a);
  mock_stream_disconnect(&stream_endpoint_b);
  mock_stream_deinit(&stream_endpoint_a);
  mock_stream_deinit(&stream_endpoint_b);
}

static void adapter_mock_reset(void) {
  /* Clear buffers without destroying threads or state */
  mock_stream_clear(&stream_endpoint_a);
  mock_stream_clear(&stream_endpoint_b);
}

static ioHdlcStreamPort adapter_mock_get_port_a(void) {
  return mock_stream_adapter_get_port(&adapter_endpoint_a);
}

static ioHdlcStreamPort adapter_mock_get_port_b(void) {
  return mock_stream_adapter_get_port(&adapter_endpoint_b);
}

static int adapter_mock_configure_error_injection(unsigned int error_rate_percent) {
  if (error_rate_percent > 100) {
    return -1;  /* Invalid parameter */
  }
  configured_error_rate = error_rate_percent;
  return 0;
}

/*===========================================================================*/
/* Exported adapter                                                          */
/*===========================================================================*/

const test_adapter_t mock_adapter = {
  .name = "Mock Stream (unified)",
  .init = adapter_mock_init,
  .deinit = adapter_mock_deinit,
  .reset = adapter_mock_reset,
  .get_port_a = adapter_mock_get_port_a,
  .get_port_b = adapter_mock_get_port_b,
  .configure_error_injection = adapter_mock_configure_error_injection
};
