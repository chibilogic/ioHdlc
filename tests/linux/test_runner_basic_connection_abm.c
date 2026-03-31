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
#include <stdbool.h>
#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"
#include "adapter_mock.h"
/*===========================================================================*/
/* Main Test Runner                                                          */
/*===========================================================================*/
int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Basic Connection (ABM Mode)\n");
  test_printf("═══════════════════════════════════════════════════════════════\n\n");
  /* Initialize adapter */
  mock_adapter.init();
  RUN_TEST_ADAPTER(test_abm_data_exchange, &mock_adapter);
  /* Deinitialize adapter */
  mock_adapter.deinit();
  TEST_SUMMARY();
  return (failed_count == 0) ? 0 : 1;
}
