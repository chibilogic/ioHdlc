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
 * @file    test_helpers.c
 * @brief   Test utilities implementation.
 */

#include "test_helpers.h"

int test_failures = 0;
int test_successes = 0;
int failed_count = 0;   /* Alias for test_failures */
int passed_count = 0;   /* Alias for test_successes */
