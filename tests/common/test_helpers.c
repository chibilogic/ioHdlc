/**
 * @file    test_helpers.c
 * @brief   Test utilities implementation.
 */

#include "test_helpers.h"

#ifdef IOHDLC_USE_CHIBIOS
#include <stdarg.h>
#include "hal.h"
#include "chprintf.h"

/* External serial driver (defined in main) */
extern SerialDriver SD0;

/**
 * @brief   Printf implementation for ChibiOS using chprintf.
 */
void test_printf_impl(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  chvprintf((BaseSequentialStream *)&SD1, fmt, ap);
  va_end(ap);
}
#endif

int test_failures = 0;
int test_successes = 0;
int failed_count = 0;   /* Alias for test_failures */
int passed_count = 0;   /* Alias for test_successes */