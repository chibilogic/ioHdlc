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
 * @file    include/ioHdlcswdriver.h
 * @brief   HDLC software driver (unified framing + protocol logic).
 * @details Implements ioHdlcDriver interface with software transparency and FCS.
 *          Uses ioHdlcStreamPort for transport abstraction (UART, SPI, Mock, etc).
 *          This header documents the reference software driver that bridges the
 *          framed driver contract to a byte-stream transport.
 *
 *          Intended use:
 *          - configure the driver before starting it;
 *          - bind it to a stream-port implementation and a frame pool at start;
 *          - let the stream backend deliver RX/TX/error callbacks;
 *          - stop it to release runtime resources owned by the driver.
 *
 *          This is a software reference implementation. It is useful both as a
 *          production driver on simple targets and as documentation of the
 *          expected behaviour of custom driver implementations.
 *
 *          Execution model notes:
 *          - callback context is inherited from the selected stream backend;
 *          - the blocking receive API is layered on top of the internal RX
 *            queue and synchronization objects stored in this driver.
 *
 * @addtogroup ioHdlc_drivers
 * @{
 */

#ifndef IOHDLCSWDRIVER_H
#define IOHDLCSWDRIVER_H

#include "ioHdlcdriver.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcqueue.h"
#include "ioHdlcll.h"
#include "ioHdlcstreamport.h"
#include "ioHdlcosal.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ioHdlcSwDriver;

typedef bool (*iohdlc_swdriver_fcs_check_fn_t)(struct ioHdlcSwDriver *drv,
                                               const uint8_t *buf,
                                               size_t total_len);
typedef void (*iohdlc_swdriver_fcs_compute_fn_t)(struct ioHdlcSwDriver *drv,
                                                 const iohdlc_tx_seg_t *segv,
                                                 uint8_t segc,
                                                 uint8_t *fcs_out);

typedef struct {
  ioHdlcStreamPort handle;
  ioHdlcStreamCallbacks callbacks;
  iohdlc_stream_constraint_mask_t constraints;
  iohdlc_stream_assist_mask_t assists;
  uint8_t tx_fcs_offload_sizes[4];
  bool tx_materialize_plan;
} ioHdlcSwDriverPortState;

typedef struct {
  uint8_t supported_sizes[4]; /* FCS sizes handled by this backend. */
  uint8_t default_size;       /* Preferred FCS size when also supported by the driver. */
  bool (*check)(void *fcs_backend_ctx, uint8_t fcs_size,
                const uint8_t *buf, size_t total_len);
  void (*compute)(void *fcs_backend_ctx, uint8_t fcs_size,
                  const iohdlc_tx_seg_t *segv, uint8_t segc, uint8_t *fcs);
} ioHdlcSwDriverFcsBackend;

typedef struct {
  const ioHdlcSwDriverFcsBackend *fcs_backend;
  void *fcs_backend_ctx;      /* Opaque context owned by the integration. */
} ioHdlcSwDriverInitConfig;

typedef struct {
  uint8_t fcs_size;          /* Configured FCS size in bytes. */
  bool apply_transparency;   /* Software transparency enabled. */
  uint8_t frame_format_size; /* Frame Format Field size in bytes. */
} ioHdlcSwDriverConfig;

typedef struct {
  uint8_t *stagep;          /* DMA-safe staging byte. */
  iohdlc_frame_t *in_frame; /* Frame currently assembled, NULL if idle. */
  iohdlc_sem_t recept_sem;
  iohdlc_frame_q_t recept_q;
  IOHDLC_RAWQ_MUTEX_DECLARE(recept_mtx);
} ioHdlcSwDriverRxState;

#ifndef IOHDLC_USE_MOCK_ADAPTER
typedef struct {
  iohdlc_frame_q_t raw_q;    /* Driver-owned logical TX queue. */
  iohdlc_frame_t *inflight_fp;
  iohdlc_frame_t *shadow_fp; /* Staged resend image for the inflight frame. */
  uint8_t shadow_prefix[IOHDLC_TXPLAN_PREFIX_MAX];
  uint8_t shadow_suffix[IOHDLC_TXPLAN_SUFFIX_MAX];
  uint8_t shadow_prefix_len;
  uint8_t shadow_suffix_len;
} ioHdlcSwDriverTxState;
#endif

typedef struct {
  bool started;
} ioHdlcSwDriverRuntimeState;

typedef struct {
  const ioHdlcSwDriverFcsBackend *backend;
  void *backend_ctx;
  iohdlc_swdriver_fcs_check_fn_t rx_check_fn;
  iohdlc_swdriver_fcs_compute_fn_t tx_compute_fn;
  bool tx_defer_to_port;
} ioHdlcSwDriverFcsState;

/**
 * @brief   HDLC software driver instance.
 * @details Stores the stream-port binding, framing configuration, RX assembly
 *          state, TX execution state, and lifecycle flags used by the software
 *          framer/deframer implementation.
 *
 *          Ownership notes:
 *          - the stream-port handle is copied into @p port at start time;
 *          - @p rx.stagep is allocated and freed by the driver;
 *          - frames held by RX/TX queues remain subject to frame-pool
 *            reference management.
 *
 *          Callers should treat the runtime fields as internal implementation
 *          state even though the structure is visible in the header.
 */
typedef struct ioHdlcSwDriver {
  /* ioHdlcDriver interface */
  const struct _iohdlc_driver_vmt *vmt;
  _iohdlc_driver_data

  ioHdlcDriverCapabilities caps;   /* Effective capabilities for this instance. */
  ioHdlcSwDriverPortState port;  /* Transport binding and declared assists. */
  ioHdlcSwDriverFcsState fcs;    /* Optional FCS backend binding. */
  ioHdlcSwDriverConfig config;   /* Framing configuration selected at configure(). */
  ioHdlcSwDriverRxState rx;      /* RX assembly state and blocking receive queue. */

#ifndef IOHDLC_USE_MOCK_ADAPTER
  ioHdlcSwDriverTxState tx;      /* TX execution state for the swdriver-owned queue. */
#endif

  ioHdlcSwDriverRuntimeState runtime;
} ioHdlcSwDriver;

/** @ingroup ioHdlc_drivers */
void ioHdlcSwDriverInit(ioHdlcSwDriver *drv, const ioHdlcSwDriverInitConfig *config);

/**
 * @brief   Stop software HDLC driver.
 * @details Stops port operations (terminates RX thread) and releases resources.
 *          Safe to call multiple times (idempotent).
 * @param[in] drv   Driver instance to stop.
 */
static inline void ioHdlcSwDriverStop(ioHdlcSwDriver *drv) {
  hdlcStop((ioHdlcDriver *)drv);
}

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSWDRIVER_H */

/** @} */
