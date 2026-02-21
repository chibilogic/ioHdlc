/*
    ioHdlc - Test Runner for Basic Connection TWA Tests (Linux)
    
    This is a standalone test runner for Linux/POSIX systems.
    It calls test functions from the common test scenarios.
*/

#include <stdio.h>
#include <stdbool.h>
#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"
#include "adapter_mock.h"

/*===========================================================================*/
/* Main Test Runner                                                          */
/*===========================================================================*/

int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Basic Connection (TWA Mode)\n");
  test_printf("═══════════════════════════════════════════════════════════════\n\n");

  /* Initialize adapter */
  mock_adapter.init();

  RUN_TEST_ADAPTER(test_data_exchange_twa, &mock_adapter);

  /* Deinitialize adapter */
  mock_adapter.deinit();

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
