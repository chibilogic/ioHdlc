/*
    ioHdlc - Test Runner for Checkpoint TWS Tests (Linux)
    
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
  test_printf("═══════════════════════════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Checkpoint Retransmission Tests (TWS Mode)\n");
  test_printf("═══════════════════════════════════════════════════════════════════\n\n");

  RUN_TEST_ADAPTER(test_A1_1_frame_loss_window_full, &mock_adapter);
  RUN_TEST_ADAPTER(test_A2_1_multiple_frame_loss, &mock_adapter);
  RUN_TEST_ADAPTER(test_A2_2_first_and_last_frame_loss, &mock_adapter);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
