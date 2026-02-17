/*
    ioHdlc - Test Runner for Frame Pool Tests (Linux)
    
    This is a standalone test runner for Linux/POSIX systems.
    It calls test functions from the common test scenarios.
*/

#include <stdio.h>
#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"

/*===========================================================================*/
/* Main Test Runner                                                          */
/*===========================================================================*/

int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Frame Pool Tests\n");
  test_printf("═══════════════════════════════════════════════\n\n");

  RUN_TEST(test_pool_init);
  RUN_TEST(test_take_release);
  RUN_TEST(test_addref);
  RUN_TEST(test_watermark);
  RUN_TEST(test_exhaust_pool);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
