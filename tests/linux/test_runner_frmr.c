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
 * @file    test_runner_frmr.c
 * @brief   Standalone test runner for FRMR tests (Linux/POSIX).
 */

#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"
#include "adapter_mock.h"

int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - FRMR Tests\n");
  test_printf("═══════════════════════════════════════════════\n\n");

  RUN_TEST_ADAPTER(test_frmr_invalid_nr, &mock_adapter);

  TEST_SUMMARY();

  return (failed_count == 0) ? 0 : 1;
}
