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
