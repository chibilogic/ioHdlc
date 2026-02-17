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
 * @file    test_runner_basic_connection.c
 * @brief   Standalone test runner for basic connection tests (Linux/POSIX).
 *
 * @details Platform-specific test runner that executes OS-agnostic test
 *          scenarios. Provides main() entry point for standalone execution.
 */

#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"

/**
 * @brief   Main test runner entry point.
 * @return  0 on success (all tests passed), 1 on failure
 */
int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Basic Connection\n");
  test_printf("═══════════════════════════════════════════════\n\n");

  RUN_TEST(test_station_creation);
  RUN_TEST(test_peer_creation);
  RUN_TEST(test_snrm_handshake);
  RUN_TEST(test_data_exchange);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
