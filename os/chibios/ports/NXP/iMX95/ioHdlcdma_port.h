/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file    ioHdlcdma_port.h
 * @brief   DMA/cache policy for NXP i.MX95 ChibiOS ports.
 */

#ifndef IOHDLCDMA_PORT_H_
#define IOHDLCDMA_PORT_H_

#define IOHDLC_DMA_CACHE_MAINTENANCE_REQUIRED TRUE

#if defined(IMX95_NOCACHE_ENABLE) && (IMX95_NOCACHE_ENABLE == TRUE)
extern uint8_t __nocache_base__[];
extern uint8_t __nocache_end__[];

#define IOHDLC_DMA_NOCACHE_RANGE_AVAILABLE TRUE
#define IOHDLC_DMA_NOCACHE_BASE            ((uintptr_t)__nocache_base__)
#define IOHDLC_DMA_NOCACHE_END             ((uintptr_t)__nocache_end__)
#endif

#endif /* IOHDLCDMA_PORT_H_ */
