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
 * @file    test_runner_exchange.c
 * @brief   Linux entry point for exchange test.
 */

#include "adapter_mock.h"

/* Entry point from test_exchange.c */
extern int test_exchange_main(const test_adapter_t *adapter, int argc, char **argv);

int main(int argc, char **argv) {
  return test_exchange_main(&mock_adapter, argc, argv);
}
