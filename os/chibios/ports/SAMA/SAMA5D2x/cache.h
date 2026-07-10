/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file    cache.h
 * @brief   ChibiOS cache helper shim for SAMA5D2x ports.
 */

#ifndef IOHDLC_SAMA_CACHE_H_
#define IOHDLC_SAMA_CACHE_H_

#include "sama_cache.h"

#define CACHE_LINE_SIZE        L1_CACHE_BYTES
#define cacheBufferFlush(p, n) cacheCleanRegion((p), (uint32_t)(n))
#define cacheBufferInvalidate(p, n) cacheInvalidateRegion((p), (uint32_t)(n))

#endif /* IOHDLC_SAMA_CACHE_H_ */
