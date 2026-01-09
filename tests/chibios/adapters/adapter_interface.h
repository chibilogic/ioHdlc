/**
 * @file    adapter_interface.h
 * @brief   Common interface for test stream adapters.
 * @details Defines abstraction layer for mock and hardware UART adapters,
 *          allowing test scenarios to be backend-agnostic.
 */

#ifndef ADAPTER_INTERFACE_H
#define ADAPTER_INTERFACE_H

#include "ioHdlcstreamport.h"

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
   * @brief Get ioHdlcStreamPort for endpoint A (Primary station).
   * @return Port structure with ctx and ops configured.
   */
  ioHdlcStreamPort (*get_port_a)(void);
  
  /**
   * @brief Get ioHdlcStreamPort for endpoint B (Secondary station).
   * @return Port structure with ctx and ops configured.
   */
  ioHdlcStreamPort (*get_port_b)(void);
  
} test_adapter_t;

#endif /* ADAPTER_INTERFACE_H */
