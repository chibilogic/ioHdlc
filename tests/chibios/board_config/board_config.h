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
 * @file    board_config.h
 * @brief   Board-specific UART configuration for test suite.
 * @details Includes appropriate board configuration based on target platform.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Include board-specific configuration */
#if !defined(BOARD_NAME)
  #include "board_sama5d2x.h"
#else
  #error "Board configuration not available for this target"
#endif

#endif /* BOARD_CONFIG_H */
