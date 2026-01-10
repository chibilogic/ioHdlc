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
 * @file    test_arenas.c
 * @brief   Shared memory arenas for test frame pools.
 */

#include "test_arenas.h"

/**
 * @brief   Global memory arenas shared across all tests.
 * @details These arenas are allocated in BSS section and reused by all tests
 *          to minimize memory footprint. Total: 24 KB instead of ~151 KB.
 */
uint8_t shared_arena_primary[TEST_ARENA_SIZE];
uint8_t shared_arena_secondary[TEST_ARENA_SIZE];
uint8_t shared_arena_single[TEST_ARENA_SIZE];
