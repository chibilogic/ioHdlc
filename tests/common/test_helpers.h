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

#define TEST_ASSERT_NULL(ptr, msg) \
  TEST_ASSERT((ptr) == NULL, msg)

#define TEST_ASSERT_NOT_NULL(ptr, msg) \
  TEST_ASSERT((ptr) != NULL, msg)

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
