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
/**
 * @file    test_runner_multipoint_nrm.c
 * @brief   Standalone test runner for NRM multipoint tests (Linux/POSIX).
 */

#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"

int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Multipoint NRM\n");
  test_printf("═══════════════════════════════════════════════\n\n");

  RUN_TEST(test_multipoint_connect_two_secondaries);
  RUN_TEST(test_multipoint_data_exchange);
  RUN_TEST(test_multipoint_selective_disconnect);
  RUN_TEST(test_multipoint_address_isolation);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
