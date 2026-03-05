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
 * @file    test_helpers.h
 * @brief   Common test utilities and assertions.
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ioHdlcosal.h"

/* Declare the same printf function used for logs*/
#define test_printf IOHDLC_OSAL_PRINTF    

/*===========================================================================*/
/* 64-bit Integer Printing Helpers                                          */
/*===========================================================================*/

/**
 * @brief   Macros for printing uint64_t values on platforms without %llu.
 * @details chprintf on embedded systems often lacks 64-bit support.
 *          These macros split values into high/low 32-bit parts.
 * @note    Use with test_printf or chprintf.
 */

/**
 * @brief   Print uint64_t as decimal (split into high/low).
 * @usage   test_printf("Value: " U64_FMT "\r\n", U64_ARGS(myval));
 */
#define U64_FMT "%u%09u"
#define U64_ARGS(val) \
  (uint32_t)((val) / 1000000000ULL), \
  (uint32_t)((val) % 1000000000ULL)

/**
 * @brief   Print uint64_t as hexadecimal (high:low format).
 * @usage   test_printf("Addr: " U64_HEX "\r\n", U64_HEX_ARGS(addr));
 */
#define U64_HEX "%08X:%08X"
#define U64_HEX_ARGS(val) \
  (uint32_t)((val) >> 32), \
  (uint32_t)((val) & 0xFFFFFFFFULL)

/**
 * @brief   Print uint64_t in KB units.
 * @usage   test_printf("Size: " U64_KB " KB\r\n", U64_KB_ARGS(bytes));
 */
#define U64_KB "%u.%02u"
#define U64_KB_ARGS(val) \
  (uint32_t)((val) / 1024ULL), \
  (uint32_t)(((val) % 1024ULL) * 100 / 1024)

/**
 * @brief   Print uint64_t in MB units.
 * @usage   test_printf("Size: " U64_MB " MB\r\n", U64_MB_ARGS(bytes));
 */
#define U64_MB "%u.%02u"
#define U64_MB_ARGS(val) \
  (uint32_t)((val) / (1024ULL * 1024)), \
  (uint32_t)(((val) % (1024ULL * 1024)) * 100 / (1024ULL * 1024))


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

#define RUN_TEST_ADAPTER(test_func, adapter) \
  do { \
    test_printf("🧪 Running %s...\r\n", #test_func); \
    int result = test_func(adapter); \
    if (result == 0) { \
      test_printf("✅ PASS: %s\r\n\r\n", #test_func); \
    } else { \
      test_printf("❌ FAIL: %s\r\n\r\n", #test_func); \
    } \
    /* TEMPORARILY DISABLED for debugging deadlock */ \
    /* if ((adapter)->reset) { */ \
    /*   (adapter)->reset(); */ \
    /* } */ \
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
  test_printf("%s (%u bytes):\r\n", label, (uint32_t)len);
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

#endif /* TEST_HELPERS_H */
