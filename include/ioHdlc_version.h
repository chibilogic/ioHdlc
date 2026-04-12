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
 * @file    include/ioHdlc_version.h
 * @brief   ioHdlc semantic version macros.
 * @details The repo build integrates this header with version components
 *          derived from the latest Git tag. External builds may override the
 *          macros below or rely on the fallback values shipped with the tree.
 */

#ifndef IOHDLC_VERSION_H_
#define IOHDLC_VERSION_H_

#ifndef IOHDLC_VERSION_MAJOR
#define IOHDLC_VERSION_MAJOR 1
#endif

#ifndef IOHDLC_VERSION_MINOR
#define IOHDLC_VERSION_MINOR 0
#endif

#ifndef IOHDLC_VERSION_PATCH
#define IOHDLC_VERSION_PATCH 0
#endif

#define IOHDLC_VERSION_STRINGIFY_AUX(x) #x
#define IOHDLC_VERSION_STRINGIFY(x) IOHDLC_VERSION_STRINGIFY_AUX(x)

#define IOHDLC_VERSION_STRING \
  IOHDLC_VERSION_STRINGIFY(IOHDLC_VERSION_MAJOR) "." \
  IOHDLC_VERSION_STRINGIFY(IOHDLC_VERSION_MINOR) "." \
  IOHDLC_VERSION_STRINGIFY(IOHDLC_VERSION_PATCH)

#endif /* IOHDLC_VERSION_H_ */
