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
  test_printf("═══════════════════════════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Checkpoint Retransmission Tests (TWA Mode)\n");
  test_printf("═══════════════════════════════════════════════════════════════════\n\n");
  RUN_TEST_ADAPTER(test_A1_1_frame_loss_window_full_twa, &mock_adapter);
  RUN_TEST_ADAPTER(test_A2_1_multiple_frame_loss_twa, &mock_adapter);
  RUN_TEST_ADAPTER(test_A2_2_first_and_last_frame_loss_twa, &mock_adapter);
  TEST_SUMMARY();
  return (failed_count == 0) ? 0 : 1;
}
