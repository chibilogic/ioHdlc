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
 * @file    include/ioHdlc_conf.h
 * @brief   Build-time configuration defaults for ioHdlc.
 * @details This header collects policy-level defaults that integrators may
 *          override at compile time before including the public umbrella
 *          header. Protocol constants and wire-format bit definitions do not
 *          belong here.
 *
 * @addtogroup ioHdlc_config
 * @{
 */

#ifndef IOHDLC_CONF_H_
#define IOHDLC_CONF_H_

/**
 * @brief   Maximum number of retries for link establishment.
 */
#ifndef IOHDLC_LINKUP_MAX_RETRIES
#define IOHDLC_LINKUP_MAX_RETRIES 3U
#endif

/**
 * @brief   Maximum number of retries for link termination.
 */
#ifndef IOHDLC_LINKDOWN_MAX_RETRIES
#define IOHDLC_LINKDOWN_MAX_RETRIES 3U
#endif

/**
 * @brief   Default base reply timeout in milliseconds.
 * @details This is the base T1 value before the per-peer retry backoff is
 *          applied. Reply-timeout recovery does not use linear waits.
 */
#ifndef IOHDLC_REPLY_TIMEOUT_MS_DEFAULT
#define IOHDLC_REPLY_TIMEOUT_MS_DEFAULT 100U
#endif

/**
 * @brief   Address-based skew divisor for ABM/ADM reply timeouts.
 * @details A value of 10 means a 10%% skew step per address increment.
 */
#ifndef IOHDLC_REPLY_TIMEOUT_ADDR_SKEW_DIVISOR
#define IOHDLC_REPLY_TIMEOUT_ADDR_SKEW_DIVISOR 10U
#endif

/**
 * @brief   Default maximum number of poll retries per peer.
 * @details Reply-timeout recovery uses exponential backoff on T1, so the
 *          cumulative wait grows geometrically as this value increases.
 */
#ifndef IOHDLC_POLL_RETRY_MAX_DEFAULT
#define IOHDLC_POLL_RETRY_MAX_DEFAULT 8U
#endif

/**
 * @brief   Default INFO length when no FFF is used and max_info_len is auto.
 */
#ifndef IOHDLC_MAX_INFO_LEN_DEFAULT_NO_FFF
#define IOHDLC_MAX_INFO_LEN_DEFAULT_NO_FFF 122U
#endif

/**
 * @brief   Default low watermark percentage for the frame pool.
 */
#ifndef IOHDLC_POOL_WATERMARK_PCT_DEFAULT
#define IOHDLC_POOL_WATERMARK_PCT_DEFAULT 20U
#endif

/**
 * @brief   Multiplier used to derive the high watermark from the low one.
 */
#ifndef IOHDLC_POOL_WATERMARK_HIGH_MULTIPLIER
#define IOHDLC_POOL_WATERMARK_HIGH_MULTIPLIER 2U
#endif

/**
 * @brief   Minimum number of frames required in the arena.
 */
#ifndef IOHDLC_MIN_FRAME_POOL_FRAMES
#define IOHDLC_MIN_FRAME_POOL_FRAMES 2U
#endif

/**
 * @brief   Alignment passed to the default frame-pool backend.
 */
#ifndef IOHDLC_FRAME_POOL_ALIGNMENT
#define IOHDLC_FRAME_POOL_ALIGNMENT 8U
#endif

/**
 * @brief   Fallback peer MIFL used when the frame pool is too small to derive it.
 */
#ifndef IOHDLC_PEER_MIFL_FALLBACK
#define IOHDLC_PEER_MIFL_FALLBACK 64U
#endif

/**
 * @brief   Divisor used to derive the writer pending margin from K.
 */
#ifndef IOHDLC_WRITER_PENDING_MARGIN_DIVISOR
#define IOHDLC_WRITER_PENDING_MARGIN_DIVISOR 8U
#endif

/**
 * @brief   Minimum writer pending margin added to K.
 */
#ifndef IOHDLC_WRITER_PENDING_MARGIN_MIN
#define IOHDLC_WRITER_PENDING_MARGIN_MIN 7U
#endif

/**
 * @brief   Receive-loop timeout used to re-check stop requests.
 */
#ifndef IOHDLC_RX_ENTRY_RECV_TIMEOUT_MS
#define IOHDLC_RX_ENTRY_RECV_TIMEOUT_MS 500U
#endif

#endif /* IOHDLC_CONF_H_ */

/** @} */
