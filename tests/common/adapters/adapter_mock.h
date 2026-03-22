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
 * @file    adapter_mock.h
 * @brief   Unified mock stream adapter declaration (all platforms).
 */

#ifndef ADAPTER_MOCK_H
#define ADAPTER_MOCK_H

#include "adapter_interface.h"

/**
 * @brief Unified mock stream adapter instance.
 * @details Provides mock stream backend with error injection support.
 *          Works on both Linux/POSIX and ChibiOS platforms.
 */
extern const test_adapter_t mock_adapter;

#endif /* ADAPTER_MOCK_H */
