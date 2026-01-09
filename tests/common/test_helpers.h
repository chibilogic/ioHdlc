/**
 * @file    test_helpers.h
 * @brief   Common test utilities and assertions.
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Platform-specific includes */
#ifdef IOHDLC_USE_CHIBIOS
  #include "ch.h"
  /* ChibiOS: declare printf function (implemented in .c) */
  void test_printf_impl(const char *fmt, ...);
  #define test_printf test_printf_impl
#else
  #include <stdio.h>
  #include <stdlib.h>
  #include <time.h>
  /* Linux/POSIX: use standard printf */
  #define test_printf printf
#endif

/*===========================================================================*/
/* Test Framework Macros                                                     */
/*===========================================================================*/

extern int test_failures;
extern int test_successes;
extern int failed_count;   /* Alias for compatibility */
extern int passed_count;   /* Alias for compatibility */

#define TEST_ASSERT(condition, msg) \
  do { \
    if (!(condition)) { \
      test_printf("❌ FAIL: %s:%d: %s\r\n", __FILE__, __LINE__, msg); \
      test_failures++; \
      return 1; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_EQ(expected, actual, msg) \
  do { \
    if ((expected) != (actual)) { \
      test_printf("❌ FAIL: %s:%d: %s (expected=%d, actual=%d)\r\n", \
              __FILE__, __LINE__, msg, (int)(expected), (int)(actual)); \
      test_failures++; \
      return 1; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_EQUAL TEST_ASSERT_EQ

#define TEST_ASSERT_NEQ(expected, actual, msg) \
  do { \
    if ((expected) == (actual)) { \
      test_printf("❌ FAIL: %s:%d: %s (both=%d)\r\n", \
              __FILE__, __LINE__, msg, (int)(expected)); \
      test_failures++; \
      return 1; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_RANGE(min, max, actual, msg) \
  do { \
    if ((actual) < (min) || (actual) > (max)) { \
      test_printf("❌ FAIL: %s:%d: %s (expected range [%ld, %ld], actual=%ld)\r\n", \
              __FILE__, __LINE__, msg, (long)(min), (long)(max), (long)(actual)); \
      test_failures++; \
      return 1; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_NULL(ptr, msg) \
  TEST_ASSERT((ptr) == NULL, msg)

#define TEST_ASSERT_NOT_NULL(ptr, msg) \
  TEST_ASSERT((ptr) != NULL, msg)

/*===========================================================================*/
/* Test Assertions with Cleanup (goto-based)                                */
/*===========================================================================*/

/**
 * @brief   Test assertions that jump to cleanup label on failure.
 * @details These macros set test_result variable and goto test_cleanup label.
 *          The test function must declare: int test_result = 0;
 *          And define a cleanup section: test_cleanup:
 */

#define TEST_ASSERT_GOTO(condition, msg) \
  do { \
    if (!(condition)) { \
      test_printf("❌ FAIL: %s:%d: %s\r\n", __FILE__, __LINE__, msg); \
      test_failures++; \
      test_result = 1; \
      goto test_cleanup; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_EQ_GOTO(expected, actual, msg) \
  do { \
    if ((expected) != (actual)) { \
      test_printf("❌ FAIL: %s:%d: %s (expected=%d, actual=%d)\r\n", \
              __FILE__, __LINE__, msg, (int)(expected), (int)(actual)); \
      test_failures++; \
      test_result = 1; \
      goto test_cleanup; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_EQUAL_GOTO TEST_ASSERT_EQ_GOTO

#define TEST_ASSERT_NEQ_GOTO(expected, actual, msg) \
  do { \
    if ((expected) == (actual)) { \
      test_printf("❌ FAIL: %s:%d: %s (both=%d)\r\n", \
              __FILE__, __LINE__, msg, (int)(expected)); \
      test_failures++; \
      test_result = 1; \
      goto test_cleanup; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_RANGE_GOTO(min, max, actual, msg) \
  do { \
    if ((actual) < (min) || (actual) > (max)) { \
      test_printf("❌ FAIL: %s:%d: %s (expected range [%ld, %ld], actual=%ld)\r\n", \
              __FILE__, __LINE__, msg, (long)(min), (long)(max), (long)(actual)); \
      test_failures++; \
      test_result = 1; \
      goto test_cleanup; \
    } else { \
      test_successes++; \
    } \
  } while(0)

#define TEST_ASSERT_NULL_GOTO(ptr, msg) \
  TEST_ASSERT_GOTO((ptr) == NULL, msg)

#define TEST_ASSERT_NOT_NULL_GOTO(ptr, msg) \
  TEST_ASSERT_GOTO((ptr) != NULL, msg)

/*===========================================================================*/
/* Test Framework Runners                                                    */
/*===========================================================================*/

#define RUN_TEST(test_func) \
  do { \
    test_printf("🧪 Running %s...\r\n", #test_func); \
    int result = test_func(); \
    if (result == 0) { \
      test_printf("✅ PASS: %s\r\n\r\n", #test_func); \
    } else { \
      test_printf("❌ FAIL: %s\r\n\r\n", #test_func); \
    } \
    passed_count = test_successes; \
    failed_count = test_failures; \
  } while(0)

#define TEST_SUITE_START(name) \
  do { \
    test_printf("\r\n════════════════════════════════════════════════════════\r\n"); \
    test_printf("  %s\r\n", name); \
    test_printf("════════════════════════════════════════════════════════\r\n\r\n"); \
  } while(0)

#define TEST_SUITE_END() \
  do { \
    test_printf("\r\n════════════════════════════════════════════════════════\r\n"); \
    test_printf("  Suite Summary: Passed: %d, Failed: %d\r\n", test_successes, test_failures); \
    if (test_failures > 0) { \
      test_printf("  ❌ SOME TESTS FAILED\r\n"); \
    } else { \
      test_printf("  ✅ ALL TESTS PASSED\r\n"); \
    } \
    test_printf("════════════════════════════════════════════════════════\r\n\r\n"); \
  } while(0)

#define TEST_RUN(test_func, description) \
  do { \
    test_printf("🧪 %s...\r\n", description); \
    int result = test_func(); \
    if (result == 0) { \
      test_printf("   ✅ PASS\r\n\r\n"); \
    } else { \
      test_printf("   ❌ FAIL\r\n\r\n"); \
    } \
  } while(0)

#define TEST_SUMMARY() \
  do { \
    test_printf("════════════════════════════════════════════════════════\r\n"); \
    test_printf("  Test Summary: Passed: %d, Failed: %d\r\n", passed_count, failed_count); \
    test_printf("════════════════════════════════════════════════════════\r\n\r\n"); \
  } while(0)

/*===========================================================================*/
/* Test Utilities                                                            */
/*===========================================================================*/

/**
 * @brief   Hexdump utility for debugging.
 */
static inline void test_hexdump(const char *label, const uint8_t *data, size_t len) {
  test_printf("%s (%zu bytes):\r\n", label, len);
  for (size_t i = 0; i < len; i++) {
    test_printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0) test_printf("\r\n");
  }
  if (len % 16 != 0) test_printf("\r\n");
}

/**
 * @brief   Compare byte arrays.
 */
static inline bool test_mem_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  return memcmp(a, b, len) == 0;
}

#ifndef IOHDLC_USE_CHIBIOS
/**
 * @brief   Sleep for milliseconds (Linux only).
 */
static inline void test_sleep_ms(uint32_t ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}
#else
/**
 * @brief   Sleep for milliseconds (ChibiOS).
 */
static inline void test_sleep_ms(uint32_t ms) {
  chThdSleepMilliseconds(ms);
}
#endif

#endif /* TEST_HELPERS_H */
