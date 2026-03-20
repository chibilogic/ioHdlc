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
 * @file    adapter_interface.h
 * @brief   Common interface for test stream adapters.
 * @details Defines abstraction layer for mock and hardware UART adapters,
 *          allowing test scenarios to be backend-agnostic.
 */

#ifndef ADAPTER_INTERFACE_H
#define ADAPTER_INTERFACE_H

#include "ioHdlcstreamport.h"

/** @brief Adapter supports TWA mode only (e.g. SPI hardware). */
#define ADAPTER_CONSTRAINT_TWA_ONLY   (1u << 0)

/**
 * @brief Test adapter interface.
 * @details Provides setup/teardown and port acquisition for test endpoints.
 */
typedef struct {
  const char *name;
  
  /**
   * @brief Initialize adapter and configure endpoints.
   */
  void (*init)(void);
  
  /**
   * @brief Deinitialize adapter and release resources.
   */
  void (*deinit)(void);
  
  /**
   * @brief Reset adapter state (clear buffers) between tests.
   * @details Optional: If NULL, no reset is performed.
   *          Clears TX/RX buffers without destroying threads/state.
   */
  void (*reset)(void);
  
  /**
   * @brief Get ioHdlcStreamPort for endpoint A (Primary station).
   * @return Port structure with ctx and ops configured.
   */
  ioHdlcStreamPort (*get_port_a)(void);
  
  /**
   * @brief Get ioHdlcStreamPort for endpoint B (Secondary station).
   * @return Port structure with ctx and ops configured.
   */
  ioHdlcStreamPort (*get_port_b)(void);
  
  /**
   * @brief Configure error injection (optional, mock adapters only).
   * @param[in] error_rate_percent  Error rate 0-100% (0=disabled).
   * @return 0 on success, -1 if not supported.
   * @note This function may be NULL for adapters that don't support error injection.
   *       Hardware adapters (UART) typically set this to NULL.
   */
  int (*configure_error_injection)(unsigned int error_rate_percent);

  /**
   * @brief Bitmask of adapter hardware constraints.
   * @details Zero means no constraints. See ADAPTER_CONSTRAINT_* defines.
   */
  uint32_t constraints;

} test_adapter_t;

#endif /* ADAPTER_INTERFACE_H */
