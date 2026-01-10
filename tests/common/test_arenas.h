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
 * @file    test_arenas.h
 * @brief   Shared memory arenas for test frame pools.
 * @details Provides global memory arenas shared across all tests to reduce
 *          BSS memory usage in embedded targets.
 */

#ifndef TEST_ARENAS_H
#define TEST_ARENAS_H

#include <stdint.h>

/**
 * @brief   Arena size for primary and secondary frame pools.
 * @details 8KB per arena is sufficient for most test scenarios with
 *          frame size 256 bytes and 8-32 frames in pool.
 */
#define TEST_ARENA_SIZE 8192

/**
 * @brief   Shared arena for primary station frame pool.
 * @note    Tests must execute sequentially (not in parallel).
 */
extern uint8_t shared_arena_primary[TEST_ARENA_SIZE];

/**
 * @brief   Shared arena for secondary station frame pool.
 * @note    Tests must execute sequentially (not in parallel).
 */
extern uint8_t shared_arena_secondary[TEST_ARENA_SIZE];

/**
 * @brief   Shared arena for single-station tests.
 * @note    Can be used for test_frame_pool and other standalone tests.
 */
extern uint8_t shared_arena_single[TEST_ARENA_SIZE];

#endif /* TEST_ARENAS_H */
