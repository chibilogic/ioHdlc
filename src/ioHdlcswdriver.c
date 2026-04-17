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
 * @file    ioHdlcswdriver.c
 * @brief   HDLC software driver.
 * @details Integrates RX multi-chunk state machine + protocol logic
 *          (FCS, transparency).
 *
 *          This module is the reference framed-driver implementation layered
 *          on top of @ref ioHdlcStreamPort. It converts transport callbacks
 *          into frame objects, validates and transforms received data, and
 *          serializes outbound frames according to the configured FCS, FFF,
 *          and transparency policy.
 *
 * @addtogroup ioHdlc_drivers
 * @{
 */

#include "ioHdlcswdriver.h"
#include "ioHdlcosal.h"
#include "ioHdlcll.h"
#include <errno.h>
#include <string.h>

/*===========================================================================*/
/* Forward declarations                                                      */
/*===========================================================================*/

static void s_on_rx(void *cb_ctx, uint32_t errmask);
static void s_on_tx_done(void *cb_ctx, void *framep);
static const struct _iohdlc_driver_vmt s_vmt;
static const ioHdlcStreamDriverOps s_stream_drvops;

/*===========================================================================*/
/* Driver Capabilities                                                       */
/*===========================================================================*/

static const ioHdlcDriverCapabilities s_swdriver_base_caps = {
  .modulo = {
    .supported_log2mods = {3, 7, 0, 0},  /* Supports modulo 8 and 128. */
  },
  .fcs = {
    .supported_sizes = {0, 2, 0, 0},  /* Supports FCS 0 (none) and 2 (16-bit) */
    .default_size = 2,
  },
  .transparency = {
    .hw_support = false,
    .sw_available = true  /* Software implementation available */
  },
  .fff = {
    .supported_types = {0, 1, 2, 0},  /* Supports: none, TYPE0 (1 byte), TYPE1 (2 bytes) */
    .default_type = 1,                /* Default: TYPE0 (1 byte) */
    .hw_support = false
  }
};

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

static bool s_fcs_size_supported(const uint8_t *supported_sizes, uint8_t fcs_size) {
  size_t i;

  IOHDLC_ASSERT(supported_sizes != NULL, "s_fcs_size_supported: null supported_sizes");

  for (i = 0U; i < 4U; ++i) {
    if (supported_sizes[i] == fcs_size)
      return true;
  }

  return false;
}

static void s_fcs_sizes_clear(uint8_t *supported_sizes) {
  IOHDLC_ASSERT(supported_sizes != NULL, "s_fcs_sizes_clear: null supported_sizes");
  memset(supported_sizes, 0, 4U);
}

static bool s_backend_fcs_supported(const ioHdlcSwDriver *drv, uint8_t fcs_size) {
  IOHDLC_ASSERT(drv != NULL, "s_backend_fcs_supported: null driver");

  return drv->fcs.backend != NULL &&
         s_fcs_size_supported(drv->fcs.backend->supported_sizes, fcs_size);
}

static bool s_port_tx_fcs_offload_supported(const ioHdlcSwDriver *drv, uint8_t fcs_size) {
  IOHDLC_ASSERT(drv != NULL, "s_port_tx_fcs_offload_supported: null driver");

  return s_fcs_size_supported(drv->port.tx_fcs_offload_sizes, fcs_size);
}

static bool s_fcs_check_none(ioHdlcSwDriver *drv, const uint8_t *buf, size_t total_len) {
  (void)drv;
  (void)buf;
  (void)total_len;
  return true;
}

static bool s_fcs_check_sw(ioHdlcSwDriver *drv, const uint8_t *buf, size_t total_len) {
  uint16_t crc;

  IOHDLC_ASSERT(drv != NULL, "s_fcs_check_sw: null driver");
  IOHDLC_ASSERT(buf != NULL, "s_fcs_check_sw: null buffer");
  IOHDLC_ASSERT(drv->config.fcs_size == 2U, "s_fcs_check_sw: unsupported FCS size");

  ioHdlcFcsInit(&crc);
  ioHdlcFcsUpdate(&crc, buf, total_len);
  return crc == 0xf0b8;
}

static bool s_fcs_check_backend(ioHdlcSwDriver *drv, const uint8_t *buf, size_t total_len) {
  IOHDLC_ASSERT(drv != NULL, "s_fcs_check_backend: null driver");
  IOHDLC_ASSERT(drv->fcs.backend != NULL, "s_fcs_check_backend: missing backend");
  IOHDLC_ASSERT(drv->fcs.backend->check != NULL, "s_fcs_check_backend: missing check hook");
  IOHDLC_ASSERT(buf != NULL, "s_fcs_check_backend: null buffer");

  return drv->fcs.backend->check(drv->fcs.backend_ctx, drv->config.fcs_size, buf, total_len);
}

static void s_fcs_compute_none(ioHdlcSwDriver *drv, const iohdlc_tx_seg_t *segv,
                               uint8_t segc, uint8_t *fcs_out) {
  (void)drv;
  (void)segv;
  (void)segc;
  (void)fcs_out;
}

static void s_fcs_compute_sw(ioHdlcSwDriver *drv, const iohdlc_tx_seg_t *segv,
                             uint8_t segc, uint8_t *fcs_out) {
  uint16_t crc;
  uint8_t i;

  IOHDLC_ASSERT(drv != NULL, "s_fcs_compute_sw: null driver");
  IOHDLC_ASSERT(segv != NULL || segc == 0U, "s_fcs_compute_sw: null segments");
  IOHDLC_ASSERT(fcs_out != NULL, "s_fcs_compute_sw: null output");
  IOHDLC_ASSERT(drv->config.fcs_size == 2U, "s_fcs_compute_sw: unsupported FCS size");

  ioHdlcFcsInit(&crc);
  for (i = 0U; i < segc; ++i) {
    if (segv[i].len > 0U)
      ioHdlcFcsUpdate(&crc, segv[i].ptr, segv[i].len);
  }
  crc = ioHdlcFcsFinalize(crc);
  fcs_out[0] = (uint8_t)(crc & 0xFFU);
  fcs_out[1] = (uint8_t)(crc >> 8);
}

static void s_fcs_compute_backend(ioHdlcSwDriver *drv, const iohdlc_tx_seg_t *segv,
                                  uint8_t segc, uint8_t *fcs_out) {
  IOHDLC_ASSERT(drv != NULL, "s_fcs_compute_backend: null driver");
  IOHDLC_ASSERT(drv->fcs.backend != NULL, "s_fcs_compute_backend: missing backend");
  IOHDLC_ASSERT(drv->fcs.backend->compute != NULL, "s_fcs_compute_backend: missing compute hook");
  IOHDLC_ASSERT(segv != NULL || segc == 0U, "s_fcs_compute_backend: null segments");
  IOHDLC_ASSERT(fcs_out != NULL, "s_fcs_compute_backend: null output");

  drv->fcs.backend->compute(drv->fcs.backend_ctx, drv->config.fcs_size, segv, segc, fcs_out);
}

static void s_resolve_fcs_caps(ioHdlcSwDriver *drv) {
  const ioHdlcSwDriverFcsBackend *backend;
  size_t i;
  uint8_t next_slot = 1U;

  IOHDLC_ASSERT(drv != NULL, "s_resolve_fcs_caps: null driver");

  drv->caps = s_swdriver_base_caps;
  backend = drv->fcs.backend;

  if (backend != NULL &&
      backend->check != NULL &&
      backend->compute != NULL) {
    /* Public FCS capabilities are the union of the driver's native support
       and the extra sizes contributed by a complete backend implementation. */
    /* Slot 0 is reserved for the "no FCS" capability. */
    while (next_slot < 4U && drv->caps.fcs.supported_sizes[next_slot] != 0U)
      ++next_slot;

    for (i = 0U; i < 4U; ++i) {
      uint8_t fcs_size = backend->supported_sizes[i];

      if (fcs_size == 0U ||
          s_fcs_size_supported(drv->caps.fcs.supported_sizes, fcs_size))
        continue;

      IOHDLC_ASSERT(next_slot < 4U, "s_resolve_fcs_caps: supported size list overflow");
      drv->caps.fcs.supported_sizes[next_slot++] = fcs_size;
    }
  }

  drv->caps.fcs.default_size = s_swdriver_base_caps.fcs.default_size;
  if (backend != NULL &&
      s_fcs_size_supported(drv->caps.fcs.supported_sizes, backend->default_size))
    drv->caps.fcs.default_size = backend->default_size;
}

static void s_resolve_fcs_policy(ioHdlcSwDriver *drv) {
  bool backend_check = false;
  bool backend_compute = false;

  IOHDLC_ASSERT(drv != NULL, "s_resolve_fcs_policy: null driver");

  if (drv->config.fcs_size == 0U) {
    drv->fcs.rx_check_fn = s_fcs_check_none;
    drv->fcs.tx_compute_fn = s_fcs_compute_none;
    drv->fcs.tx_defer_to_port = false;
    return;
  }

  backend_check =
      drv->fcs.backend != NULL &&
      drv->fcs.backend->check != NULL &&
      s_backend_fcs_supported(drv, drv->config.fcs_size);
  backend_compute =
      drv->fcs.backend != NULL &&
      drv->fcs.backend->compute != NULL &&
      s_backend_fcs_supported(drv, drv->config.fcs_size);

  drv->fcs.rx_check_fn = backend_check ? s_fcs_check_backend : s_fcs_check_sw;
  drv->fcs.tx_compute_fn = backend_compute ? s_fcs_compute_backend : s_fcs_compute_sw;
  drv->fcs.tx_defer_to_port =
      !drv->config.apply_transparency &&
      !drv->port.tx_materialize_plan &&
      s_port_tx_fcs_offload_supported(drv, drv->config.fcs_size);
}

/**
 * @brief   Initialize software HDLC driver.
 * @details Initializes the driver object to a valid pre-start state.
 *          After initialization, the caller may configure the driver and then
 *          start it through the generic framed-driver API.
 * @param[in] drv     Driver instance to initialize.
 * @param[in] config  Optional initialization config, NULL for software defaults.
 */
void ioHdlcSwDriverInit(ioHdlcSwDriver *drv, const ioHdlcSwDriverInitConfig *config) {
  const ioHdlcSwDriverFcsBackend *fcs_backend = NULL;

  IOHDLC_ASSERT(drv != NULL, "ioHdlcSwDriverInit: null driver");

  if (config != NULL) {
    fcs_backend = config->fcs_backend;
    IOHDLC_ASSERT(fcs_backend != NULL || config->fcs_backend_ctx == NULL,
                  "ioHdlcSwDriverInit: FCS backend ctx without backend");
  }

  drv->vmt = &s_vmt;
  drv->fpp = NULL;
  drv->caps = s_swdriver_base_caps;
  drv->port.constraints = 0U;
  drv->port.assists = 0U;
  s_fcs_sizes_clear(drv->port.tx_fcs_offload_sizes);
  drv->port.tx_materialize_plan = true;
  drv->fcs.backend = fcs_backend;
  drv->fcs.backend_ctx = (config != NULL) ? config->fcs_backend_ctx : NULL;
  drv->fcs.rx_check_fn = s_fcs_check_sw;
  drv->fcs.tx_compute_fn = s_fcs_compute_sw;
  drv->fcs.tx_defer_to_port = false;
  drv->config.fcs_size = drv->caps.fcs.default_size;
  drv->config.apply_transparency = false;
  drv->config.frame_format_size = 0;
  
  drv->rx.stagep = NULL;
  drv->rx.in_frame = NULL;
  drv->runtime.started = false;
  
  ioHdlc_frameq_init(&drv->rx.recept_q);
  iohdlc_sem_init(&drv->rx.recept_sem, 0);
  IOHDLC_RAWQ_MUTEX_INIT(drv->rx.recept_mtx);

#ifndef IOHDLC_USE_MOCK_ADAPTER
  ioHdlc_frameq_init(&drv->tx.raw_q);
  drv->tx.inflight_fp = NULL;
  drv->tx.shadow_fp = NULL;
  drv->tx.shadow_prefix_len = 0U;
  drv->tx.shadow_suffix_len = 0U;
#endif
  s_resolve_fcs_caps(drv);
  drv->config.fcs_size = drv->caps.fcs.default_size;
  s_resolve_fcs_policy(drv);
}

/*===========================================================================*/
/* Driver Lifecycle and Configuration                                       */
/*===========================================================================*/

static void drv_start(void *instance, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp) {
  (void)phyconfigp;
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  ioHdlcStreamPort *portp = (ioHdlcStreamPort *)phyp;
  const iohdlc_stream_caps_t *caps = NULL;

  drv->fpp = fpp;
  drv->port.handle = *portp;
  if (drv->port.handle.ops != NULL && drv->port.handle.ops->get_caps != NULL)
    caps = drv->port.handle.ops->get_caps(drv->port.handle.ctx);
  drv->port.constraints = caps ? caps->constraints : 0U;
  drv->port.assists = caps ? caps->assists : 0U;
  s_fcs_sizes_clear(drv->port.tx_fcs_offload_sizes);
  if (caps != NULL)
    memcpy(drv->port.tx_fcs_offload_sizes, caps->tx_fcs_offload_sizes,
           sizeof drv->port.tx_fcs_offload_sizes);
  drv->port.tx_materialize_plan =
      caps == NULL || (drv->port.assists & IOHDLC_PORT_AST_TX_NEEDS_CONTIG) != 0U;
  s_resolve_fcs_policy(drv);

  drv->port.callbacks.on_rx = s_on_rx;
  drv->port.callbacks.on_tx_done = s_on_tx_done;
  drv->port.callbacks.on_rx_error = s_on_rx;
  drv->port.callbacks.cb_ctx = drv;

  drv->rx.stagep = (uint8_t *)iohdlc_dma_alloc(1u, IOHDLC_DMA_ALIGN_DEFAULT);
  IOHDLC_ASSERT(drv->rx.stagep != NULL, "DMA-safe staging allocation failed");
  *drv->rx.stagep = 0;
  drv->rx.in_frame = NULL;

  drv->port.handle.ops->start(drv->port.handle.ctx, &drv->port.callbacks, &s_stream_drvops);
  iohdlc_sys_lock();
  (void)drv->port.handle.ops->rx_submit(drv->port.handle.ctx, drv->rx.stagep, 1);
  iohdlc_sys_unlock();
  
#ifndef IOHDLC_USE_MOCK_ADAPTER
  drv->tx.inflight_fp = NULL;
  drv->tx.shadow_fp = NULL;
  drv->tx.shadow_prefix_len = 0U;
  drv->tx.shadow_suffix_len = 0U;
#endif
  drv->runtime.started = true;
}

static void drv_stop(void *instance) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  
  if (!drv || !drv->runtime.started) {
    return;
  }

  if (drv->port.handle.ops && drv->port.handle.ops->stop) {
    drv->port.handle.ops->stop(drv->port.handle.ctx);
  }
  
  if (drv->rx.stagep) {
    iohdlc_dma_free(drv->rx.stagep);
    drv->rx.stagep = NULL;
  }
  drv->port.constraints = 0U;
  drv->port.assists = 0U;
  s_fcs_sizes_clear(drv->port.tx_fcs_offload_sizes);
  drv->port.tx_materialize_plan = true;
  drv->runtime.started = false;
}

static const ioHdlcDriverCapabilities* drv_get_capabilities(void *instance) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;

  return &drv->caps;
}

static int32_t drv_configure(void *instance, uint8_t fcs_size, bool transparency, uint8_t fff_type) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;

  /* Validate FCS size against supported sizes */
  if (!s_fcs_size_supported(drv->caps.fcs.supported_sizes, fcs_size)) {
    return ENOTSUP;  /* Operation not supported */
  }

  /* Validate FFF type against supported types */
  bool valid_fff = false;
  for (size_t i = 0U; i < 4U; ++i) {
    if (drv->caps.fff.supported_types[i] == fff_type) {
      valid_fff = true;
      break;
    }
  }
  if (!valid_fff) {
    return ENOTSUP;  /* FFF type not supported */
  }

  /* FFF and transparency are mutually exclusive. */
  if (transparency && fff_type != 0) {
    return EINVAL;  /* Invalid argument */
  }

  /* Validate transparency support */
  if (transparency && !drv->caps.transparency.hw_support &&
      !drv->caps.transparency.sw_available) {
    return ENOTSUP;
  }

  /* Configuration is valid - store it */
  drv->config.fcs_size = fcs_size;
  drv->config.apply_transparency = transparency;
  drv->config.frame_format_size = fff_type;
  s_resolve_fcs_policy(drv);

  return 0;  /* Success */
}

/*===========================================================================*/
/* TX Framing Utilities                                                      */
/*===========================================================================*/

static int32_t s_emit_fff(const ioHdlcSwDriver *drv, size_t payload_len,
                          uint8_t *dst, size_t *len_out) {
  uint16_t total_wire_len;

  IOHDLC_ASSERT(drv != NULL, "s_emit_fff: null driver");
  IOHDLC_ASSERT(dst != NULL, "s_emit_fff: null dst");
  IOHDLC_ASSERT(len_out != NULL, "s_emit_fff: null len_out");

  total_wire_len = (uint16_t)(payload_len + drv->config.frame_format_size + drv->config.fcs_size);

  if (drv->config.frame_format_size == 1U) {
    if ((total_wire_len & 0xFF80U) != 0U)
      return EMSGSIZE;
    dst[0] = (uint8_t)total_wire_len;
    *len_out = 1U;
    return 0;
  }

  if (drv->config.frame_format_size == 2U) {
    if ((total_wire_len & 0xF000U) != 0U)
      return EMSGSIZE;
    dst[0] = (uint8_t)(0x80U | ((total_wire_len >> 8) & 0x0FU));
    dst[1] = (uint8_t)(total_wire_len & 0xFFU);
    *len_out = 2U;
    return 0;
  }

  *len_out = 0U;
  return 0;
}

static int32_t s_build_tx_plan(void *cb_ctx, const iohdlc_frame_t *fp,
                               const iohdlc_tx_plan_opts_t *opts,
                               iohdlc_tx_plan_t *plan) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)cb_ctx;
  const iohdlc_tx_plan_opts_t default_opts = {
    .prepend_opening_flag = false,
  };
  const iohdlc_tx_plan_opts_t *eff_opts = (opts != NULL) ? opts : &default_opts;
  uint8_t prefix_len = 0;
  uint8_t suffix_len = 0;
  size_t body_len = 0;
  size_t info_off = 0;
  size_t fff_len = 0;
  uint8_t segc = 0;
  iohdlc_tx_seg_t fcs_segv[2];
  uint8_t fcs_segc = 0U;
  uint8_t prefix_seg_idx = IOHDLC_TXPLAN_SEG_NONE;
  uint8_t body_seg_idx = IOHDLC_TXPLAN_SEG_NONE;
  uint8_t prefix_cov_off = 0;
  uint8_t prefix_cov_len = 0;
  const uint8_t *ctrl_src = NULL;
  uint8_t ctrl_len = 0;

  IOHDLC_ASSERT(drv != NULL, "s_build_tx_plan: null driver");
  IOHDLC_ASSERT(fp != NULL, "s_build_tx_plan: null frame");
  IOHDLC_ASSERT(plan != NULL, "s_build_tx_plan: null plan");

  /* Start from an empty non-transparent submission plan. */
  plan->segc = 0;
  plan->prefix_len = 0U;
  plan->suffix_len = 0U;
  plan->wire_len = 0U;
  plan->fcs.size = 0U;
  plan->fcs.begin_off = 0U;
  plan->fcs.end_exclusive = 0U;
  plan->fcs.begin_seg = IOHDLC_TXPLAN_SEG_NONE;
  plan->fcs.end_seg = IOHDLC_TXPLAN_SEG_NONE;
  plan->fcs.emitted = 0U;

  if (drv->config.apply_transparency)
    return ENOTSUP;

  if (eff_opts->prepend_opening_flag) {
    if (prefix_len >= IOHDLC_TXPLAN_PREFIX_MAX)
      return EMSGSIZE;
    plan->prefix[prefix_len++] = IOHDLC_FLAG;
  }

  /* Derive the frame geometry from the TX snapshot currently bound to fp. */
  ctrl_len = ioHdlc_txs_get_ctrl_len(&fp->tx_snapshot);

  IOHDLC_ASSERT(ctrl_len != 0U, "s_build_tx_plan: missing control snapshot");
  if (ctrl_len > IOHDLC_TXS_INLINE_CTRL_MAX)
    return ENOTSUP;

  info_off = drv->config.frame_format_size + 1U + ctrl_len;
  IOHDLC_ASSERT(fp->elen >= info_off, "s_build_tx_plan: frame shorter than header geometry");
  body_len = fp->elen - info_off;
  ctrl_src = fp->tx_snapshot.ctrl;

  /* Materialize the non-transparent prefix: optional FFF, address, control. */
  if (drv->config.frame_format_size != 0U) {
    size_t hdlc_len = 1U + ctrl_len + body_len;
    int32_t ret = s_emit_fff(drv, hdlc_len, &plan->prefix[prefix_len], &fff_len);
    if (ret != 0)
      return ret;
    IOHDLC_ASSERT((size_t)prefix_len + fff_len <= IOHDLC_TXPLAN_PREFIX_MAX,
                  "s_build_tx_plan: prefix overflow after FFF");
    prefix_len += fff_len;
  }

  IOHDLC_ASSERT((size_t)prefix_len + 1U + ctrl_len <= IOHDLC_TXPLAN_PREFIX_MAX,
                "s_build_tx_plan: prefix overflow on address/control");
  plan->prefix[prefix_len++] = fp->tx_snapshot.addr;
  memcpy(&plan->prefix[prefix_len], ctrl_src, ctrl_len);
  prefix_len += ctrl_len;
  prefix_cov_off = eff_opts->prepend_opening_flag ? 1U : 0U;
  prefix_cov_len = (prefix_len > prefix_cov_off) ?
      (uint8_t)(prefix_len - prefix_cov_off) : 0U;

  if (drv->config.fcs_size != 0U) {
    /* Describe the exact byte domain covered by FCS on this submission. */
    plan->fcs.size = drv->config.fcs_size;
    plan->fcs.begin_off = prefix_cov_len > 0U ? prefix_cov_off : 0U;
    plan->fcs.end_exclusive = body_len;

    if (prefix_cov_len > 0U) {
      fcs_segv[fcs_segc].ptr = &plan->prefix[prefix_cov_off];
      fcs_segv[fcs_segc].len = prefix_cov_len;
      ++fcs_segc;
    }
    if (body_len > 0U) {
      fcs_segv[fcs_segc].ptr = &fp->frame[info_off];
      fcs_segv[fcs_segc].len = body_len;
      ++fcs_segc;
    }

    if (!drv->fcs.tx_defer_to_port) {
      IOHDLC_ASSERT(fcs_segc != 0U, "s_build_tx_plan: empty FCS coverage");
      IOHDLC_ASSERT(drv->fcs.tx_compute_fn != NULL,
                    "s_build_tx_plan: missing TX FCS compute hook");
      IOHDLC_ASSERT((size_t)suffix_len + drv->config.fcs_size <= IOHDLC_TXPLAN_SUFFIX_MAX,
                    "s_build_tx_plan: suffix overflow on FCS");
      drv->fcs.tx_compute_fn(drv, fcs_segv, fcs_segc, &plan->suffix[suffix_len]);
      suffix_len = (uint8_t)(suffix_len + drv->config.fcs_size);
      plan->fcs.emitted = 1U;
    } else {
      plan->fcs.emitted = 0U;
    }
  }

  /* The closing flag is always carried in the suffix segment. */
  IOHDLC_ASSERT(suffix_len < IOHDLC_TXPLAN_SUFFIX_MAX,
                "s_build_tx_plan: suffix overflow on closing flag");
  plan->suffix[suffix_len++] = IOHDLC_FLAG;

  /* Expose the physical submission as prefix/body/suffix segments. */
  if (prefix_len > 0U) {
    IOHDLC_ASSERT(segc < IOHDLC_TXPLAN_MAX_SEGS, "s_build_tx_plan: too many TX segments");
    prefix_seg_idx = (uint8_t)segc;
    plan->segv[segc].ptr = plan->prefix;
    plan->segv[segc].len = prefix_len;
    ++segc;
  }
  if (body_len > 0U) {
    IOHDLC_ASSERT(segc < IOHDLC_TXPLAN_MAX_SEGS, "s_build_tx_plan: too many TX segments");
    body_seg_idx = (uint8_t)segc;
    plan->segv[segc].ptr = &fp->frame[info_off];
    plan->segv[segc].len = body_len;
    ++segc;
  }
  if (suffix_len > 0U) {
    IOHDLC_ASSERT(segc < IOHDLC_TXPLAN_MAX_SEGS, "s_build_tx_plan: too many TX segments");
    plan->segv[segc].ptr = plan->suffix;
    plan->segv[segc].len = suffix_len;
    ++segc;
  }

  if (plan->fcs.size > 0U) {
    /* Translate the FCS byte-domain description into segment indices. */
    if (prefix_cov_len > 0U) {
      plan->fcs.begin_seg = prefix_seg_idx;
    } else {
      plan->fcs.begin_seg = body_seg_idx;
      plan->fcs.begin_off = 0U;
    }

    if (body_len > 0U) {
      plan->fcs.end_seg = body_seg_idx;
      plan->fcs.end_exclusive = body_len;
    } else if (prefix_cov_len > 0U) {
      plan->fcs.end_seg = prefix_seg_idx;
      plan->fcs.end_exclusive = prefix_len;
    }
  }

  /* Publish the final segment list and total wire length. */
  plan->segc = segc;
  plan->prefix_len = prefix_len;
  plan->suffix_len = suffix_len;
  plan->wire_len = prefix_len + body_len + suffix_len;
  if (plan->fcs.size > 0U && plan->fcs.emitted == 0U)
    plan->wire_len += plan->fcs.size;
  return 0;
}

/**
 * @brief   Materialize the queued TX snapshot header into the frame wire image.
 * @details Copies the address and control octets stored in @p tx_snapshot into the
 *          serialized frame buffer so contiguous transmit paths can consume
 *          the frame directly.
 */
static int32_t s_apply_tx_snapshot_header(const ioHdlcSwDriver *drv,
                                          iohdlc_frame_t *fp) {
  const uint8_t ctrl_len = ioHdlc_txs_get_ctrl_len(&fp->tx_snapshot);
  const size_t info_off = (size_t)drv->config.frame_format_size + 1U + ctrl_len;
  IOHDLC_ASSERT(drv != NULL, "s_apply_tx_snapshot_header: null driver");
  IOHDLC_ASSERT(fp != NULL, "s_apply_tx_snapshot_header: null frame");
  IOHDLC_ASSERT(ctrl_len != 0U, "s_apply_tx_snapshot_header: missing control snapshot");
  IOHDLC_ASSERT(ctrl_len <= IOHDLC_TXS_INLINE_CTRL_MAX,
                "s_apply_tx_snapshot_header: control field exceeds inline snapshot");
  IOHDLC_ASSERT(fp->elen >= info_off,
                "s_apply_tx_snapshot_header: frame shorter than header geometry");

  fp->frame[drv->config.frame_format_size] = fp->tx_snapshot.addr;
  memcpy(&fp->frame[drv->config.frame_format_size + 1U], fp->tx_snapshot.ctrl, ctrl_len);
  return 0;
}

static int32_t s_apply_tx_plan(iohdlc_frame_t *fp, const iohdlc_tx_plan_t *plan) {
  const uint8_t prefix_len = plan->prefix_len;
  const uint8_t suffix_len = plan->suffix_len;
  const size_t trailer_len = plan->wire_len - (size_t)fp->elen;

  IOHDLC_ASSERT(fp != NULL, "s_apply_tx_plan: null frame");
  IOHDLC_ASSERT(plan != NULL, "s_apply_tx_plan: null plan");
  IOHDLC_ASSERT(plan->wire_len >= fp->elen, "s_apply_tx_plan: wire length shorter than frame");
  IOHDLC_ASSERT(plan->fcs.size == 0U || plan->fcs.emitted != 0U,
                "s_apply_tx_plan: contiguous path requires emitted FCS");
  IOHDLC_ASSERT(trailer_len <= 0x0FU, "s_apply_tx_plan: trailer length exceeds snapshot encoding");

  if (prefix_len > 0U)
    memcpy(fp->frame, plan->prefix, prefix_len);
  if (suffix_len > 0U)
    memcpy(&fp->frame[fp->elen], plan->suffix, suffix_len);

  ioHdlc_txs_set_trailer_len(&fp->tx_snapshot, (uint8_t)trailer_len);
  return 0;
}

/*===========================================================================*/
/* TX Execution                                                              */
/*===========================================================================*/

static int32_t drv_send_frame(void *instance, iohdlc_frame_t *fp) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  iohdlc_tx_plan_t plan;
  iohdlc_frame_t *nfp = fp;
  size_t payload_len = fp->elen;  /* Core semantics: no FCS */
  size_t wire_len = payload_len;
  uint8_t trailer_len = 1U;
  bool plan_ready = false;
  int32_t prep_ret = 0;

  if (payload_len > 0U) {
    /* Stage the next physical submission according to the configured framing. */
    if (drv->config.apply_transparency) {
      prep_ret = s_apply_tx_snapshot_header(drv, fp);
      if (prep_ret != 0)
        return prep_ret;

      if (drv->config.frame_format_size != 0U) {
        uint16_t total_wire_len = payload_len + drv->config.fcs_size;

        if (drv->config.frame_format_size == 1U) {
          fp->frame[0] = (uint8_t)total_wire_len;
        } else if (drv->config.frame_format_size == 2U) {
          fp->frame[0] = 0x80U | ((total_wire_len >> 8) & 0x0FU);
          fp->frame[1] = (uint8_t)(total_wire_len & 0xFFU);
        } else {
          IOHDLC_ASSERT(false, "drv_send_frame: unsupported configured FFF size");
          return EINVAL;
        }
      }

      if (drv->config.fcs_size > 0U) {
        const iohdlc_tx_seg_t seg = {
          .ptr = fp->frame,
          .len = payload_len,
        };

        IOHDLC_ASSERT(drv->fcs.tx_compute_fn != NULL,
                      "drv_send_frame: missing transparent TX FCS compute hook");
        drv->fcs.tx_compute_fn(drv, &seg, 1U, &fp->frame[payload_len]);
        wire_len = payload_len + drv->config.fcs_size;
        trailer_len = (uint8_t)(drv->config.fcs_size + 1U);
      }

      nfp = hdlcTakeFrame(drv->fpp);
      if (nfp == NULL)
        return ENOMEM;  /* No memory (errno-compatible) */
      (void)ioHdlcFrameTransparentEncode(nfp, fp);
      wire_len = nfp->elen;
      trailer_len = 1U;
    } else {
      prep_ret = s_build_tx_plan(drv, fp, NULL, &plan);
      if (prep_ret != 0)
        return prep_ret;
      plan_ready = true;
      wire_len = plan.wire_len;
      trailer_len = (uint8_t)(wire_len - payload_len);
      hdlcAddRef(drv->fpp, nfp);
    }
  }

  if (drv->config.apply_transparency) {
    nfp->frame[wire_len++] = IOHDLC_FLAG;
    ioHdlc_txs_set_trailer_len(&nfp->tx_snapshot, trailer_len);
  }

#ifdef IOHDLC_USE_MOCK_ADAPTER
  /* Mock adapter: submit synchronously through the adapter wrapper. */
  if (plan_ready && drv->port.tx_materialize_plan) {
    prep_ret = s_apply_tx_plan(nfp, &plan);
    if (prep_ret != 0) {
      hdlcReleaseFrame(drv->fpp, nfp);
      return prep_ret;
    }
  }
  nfp->openingflag = 0;
  if (drv->port.handle.ops && drv->port.handle.ops->tx_busy &&
      !drv->port.handle.ops->tx_busy(drv->port.handle.ctx)) {
    nfp->openingflag = IOHDLC_FLAG;
  }
  if (drv->port.handle.ops && drv->port.handle.ops->tx_submit_frame) {
    int32_t ret = drv->port.handle.ops->tx_submit_frame(drv->port.handle.ctx, nfp);
    if (ret != 0) {
      hdlcReleaseFrame(drv->fpp, nfp);
      return ret;
    }
  }
#else
  /*
   * The swdriver owns the only logical TX queue. Backends execute the current
   * submission and, at most, keep the next resend image shadowed for the same
   * inflight frame.
   */
  iohdlc_sys_lock();

  if (!drv->port.handle.ops->tx_busy(drv->port.handle.ctx)) {
    /* TX idle: kickstart directly (don't enqueue). */
    if (plan_ready && drv->port.tx_materialize_plan) {
      prep_ret = s_apply_tx_plan(nfp, &plan);
      if (prep_ret != 0) {
        iohdlc_sys_unlock();
        hdlcReleaseFrame(drv->fpp, nfp);
        return prep_ret;
      }
    }
    nfp->openingflag = IOHDLC_FLAG;
    drv->tx.inflight_fp = nfp;
    prep_ret = drv->port.handle.ops->tx_submit_frame(drv->port.handle.ctx, nfp);
    if (prep_ret != 0) {
      drv->tx.inflight_fp = NULL;
      iohdlc_sys_unlock();
      hdlcReleaseFrame(drv->fpp, nfp);
      return prep_ret;
    }
  } else if (nfp->q_aux.next == NULL) {
    /* First queueing, or the same frame is re-queued while still inflight. */
    if (plan_ready && drv->port.tx_materialize_plan) {
      if (drv->tx.inflight_fp == nfp) {
        const uint8_t prefix_len = plan.prefix_len;
        const uint8_t suffix_len = plan.suffix_len;
        const size_t trailer_len = plan.wire_len - (size_t)nfp->elen;

        IOHDLC_ASSERT(plan.wire_len >= nfp->elen,
                      "drv_send_frame: shadow wire length shorter than frame");
        IOHDLC_ASSERT(plan.fcs.size == 0U || plan.fcs.emitted != 0U,
                      "drv_send_frame: shadow path requires emitted FCS");
        IOHDLC_ASSERT(trailer_len <= 0x0FU,
                      "drv_send_frame: trailer length exceeds snapshot encoding");

        drv->tx.shadow_fp = nfp;
        drv->tx.shadow_prefix_len = prefix_len;
        drv->tx.shadow_suffix_len = suffix_len;

        if (prefix_len > 0U)
          memcpy(drv->tx.shadow_prefix, plan.prefix, prefix_len);
        if (suffix_len > 0U)
          memcpy(drv->tx.shadow_suffix, plan.suffix, suffix_len);

        ioHdlc_txs_set_trailer_len(&nfp->tx_snapshot, (uint8_t)trailer_len);
        prep_ret = 0;
      } else {
        prep_ret = s_apply_tx_plan(nfp, &plan);
      }

      if (prep_ret != 0) {
        iohdlc_sys_unlock();
        hdlcReleaseFrame(drv->fpp, nfp);
        return prep_ret;
      }
    }
    nfp->openingflag = 0;
    ioHdlc_frameq_insert(&drv->tx.raw_q, &nfp->q_aux);
  } else {
    /* Already queued: refresh ordering without changing the active wire image. */
    if (plan_ready && drv->port.tx_materialize_plan) {
      prep_ret = s_apply_tx_plan(nfp, &plan);
      if (prep_ret != 0) {
        iohdlc_sys_unlock();
        hdlcReleaseFrame(drv->fpp, nfp);
        return prep_ret;
      }
    }
    ioHdlc_frameq_move_tail(&drv->tx.raw_q, &nfp->q_aux);
    hdlcReleaseFrame(drv->fpp, nfp);
  }
  iohdlc_sys_unlock();
#endif

  return 0;
}

/*===========================================================================*/
/* RX Path                                                                   */
/*===========================================================================*/

/**
 * @brief   Abort the current RX frame under construction.
 * @details Releases any partially assembled frame and resets the staging byte
 *          so the RX state machine can restart on the next delimiter.
 */
static void s_handle_rx_error(ioHdlcSwDriver *drv) {
  if (drv->rx.in_frame) {
    hdlcReleaseFrame(drv->fpp, drv->rx.in_frame);
    drv->rx.in_frame = NULL;
  }
  drv->rx.stagep[0] = 0; /* Clear the staged delimiter/data byte. */
}

/**
 * @brief   Frame parser callback: RX byte/chunk received or timeout.
 * @details This is the heart of the software RX path. It consumes one staged
 *          byte or one FFF-sized chunk at a time, recognizes frame delimiters,
 *          handles timeout/error conditions, and enqueues completed frames for
 *          the blocking receive API.
 * @note    Runs in the callback context defined by the selected stream port.
 */
static void s_on_rx(void *cb_ctx, uint32_t errmask) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)cb_ctx;
  size_t n = 1;
  uint8_t *b = 0;

  /* Handle timeout */
  if (errmask & IOHDLC_STREAM_ERR_TMO) {
    if (drv->rx.in_frame && drv->rx.in_frame->elen != 0) {
      /* Intra-frame timeout - discard partial frame */
      iohdlc_sys_lock_isr();
      drv->port.handle.ops->rx_cancel(drv->port.handle.ctx);
      iohdlc_sys_unlock_isr();
      s_handle_rx_error(drv);
      goto newframe;
    }
    /* Inter-frame timeout - signal IDLE */
    IOHDLC_RAWQ_LOCK_ISR(drv->rx.recept_mtx);
    iohdlc_sem_signal_i(&drv->rx.recept_sem);
    IOHDLC_RAWQ_UNLOCK_ISR(drv->rx.recept_mtx);
    return;
  }
  
  /* Handle other errors */
  if (errmask) {
    s_handle_rx_error(drv);
    iohdlc_sys_lock_isr();
    drv->port.handle.ops->rx_cancel(drv->port.handle.ctx);
    iohdlc_sys_unlock_isr();
    goto newframe;
  }

  /* Process received byte */
  if (drv->rx.in_frame != NULL) {
    b = &drv->rx.in_frame->frame[drv->rx.in_frame->elen];
    
    if ((*b == IOHDLC_FLAG) && (drv->config.frame_format_size != 2 ||
        drv->rx.in_frame->elen != 1)) {
      /* Frame complete */
      if (!drv->rx.in_frame->elen)
        goto nextoctet;  /* Empty frame */

      if (drv->rx.in_frame->elen < HDLC_BASIC_MIN_L) {
        /* Too short - discard */
        drv->rx.in_frame->elen = 0;
        b = &drv->rx.in_frame->frame[0];
        goto nextoctet;
      }

      /* Deliver frame to upper layer */
      IOHDLC_RAWQ_LOCK_ISR(drv->rx.recept_mtx);
      ioHdlc_frameq_insert(&drv->rx.recept_q, &drv->rx.in_frame->q);
      iohdlc_sem_signal_i(&drv->rx.recept_sem);
      IOHDLC_RAWQ_UNLOCK_ISR(drv->rx.recept_mtx);
      
      drv->rx.in_frame = NULL;
      *drv->rx.stagep = IOHDLC_FLAG; /* Closing FLAG is also the next opening FLAG. */
      
    } else {
      /* Check for Frame Format Field (FFF) */
      if (drv->config.frame_format_size && !drv->config.apply_transparency) {
        if (drv->config.frame_format_size == 1 && drv->rx.in_frame->elen == 0) {
          /* TYPE 0: 1 byte FFF, bit 7 = 0 */
          if (!(drv->rx.in_frame->frame[0] & 0x80) &&
              ((size_t)drv->rx.in_frame->frame[0] != 0) &&
              ((size_t)drv->rx.in_frame->frame[0] < drv->fpp->framesize)) {
            /* Valid TYPE 0 FFF - read exact length */
            n = (size_t)drv->rx.in_frame->frame[0];
            drv->rx.in_frame->elen = n;
            b = &drv->rx.in_frame->frame[1];
          } else {
            /* Invalid TYPE 0 FFF - discard frame */
            s_handle_rx_error(drv);
            n = 1;
          }
        } else if (drv->config.frame_format_size == 2) {
          if (drv->rx.in_frame->elen == 0) {
            /* TYPE 1: first byte, bit 15-12 = 1000 */
            if ((drv->rx.in_frame->frame[0] & 0xF0) == 0x80) {
              /* Valid TYPE 1 first byte - continue to read second byte */
              ++drv->rx.in_frame->elen;
              b = &drv->rx.in_frame->frame[1];
            } else {
              /* Invalid TYPE 1 first byte - discard frame */
              s_handle_rx_error(drv);
            }
          } else if (drv->rx.in_frame->elen == 1) {
            /* TYPE 1: second byte received, calculate total length */
            n = ((drv->rx.in_frame->frame[0] & 0x0F) << 8) |
                                 drv->rx.in_frame->frame[1];
            if (n && n < drv->fpp->framesize) {
              /* The second FFF byte is already in the buffer. */
              drv->rx.in_frame->elen = n--;
              b = &drv->rx.in_frame->frame[2];
            } else {
              /* Frame too large - discard */
              s_handle_rx_error(drv);
              n = 1;
            }
          } else {
            /* Already past FFF bytes - continue accumulating */
            ++b;
            if (++drv->rx.in_frame->elen >= drv->fpp->framesize) {
              s_handle_rx_error(drv);
            }
          }
        } else {
          /* Continue accumulating bytes (no FFF or unknown type) */
          ++b;
          if (++drv->rx.in_frame->elen >= drv->fpp->framesize) {
            s_handle_rx_error(drv);
          }
        }
      } else {
        /* No FFF - continue accumulating bytes */
        ++b;
        if (++drv->rx.in_frame->elen >= drv->fpp->framesize) {
          /* Frame too large - discard */
          s_handle_rx_error(drv);
        }
      }
    }
  }

newframe:
  if (!drv->rx.in_frame) {
    b = drv->rx.stagep;

    if (*b != IOHDLC_FLAG)
      goto nextoctet;

    *drv->rx.stagep = 0;

    /* Allocate new frame buffer */
    drv->rx.in_frame = hdlcTakeFrame(drv->fpp);
    if (drv->rx.in_frame == NULL)
      goto nextoctet;
    
    drv->rx.in_frame->elen = 0;
    b = &drv->rx.in_frame->frame[0];
  }

nextoctet:
  /* Arm next RX byte/chunk */
  IOHDLC_ASSERT(n != 0, "Invalid RX chunk size");
  iohdlc_sys_lock_isr();
  (void)drv->port.handle.ops->rx_submit(drv->port.handle.ctx, b, n);
  iohdlc_sys_unlock_isr();
}

static iohdlc_frame_t *drv_recv_frame(void *instance, iohdlc_timeout_t tmo) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  iohdlc_frame_t *fp;

  for (;;) {
    /* Wait for a complete frame or for the idle/timeout indication. */
    fp = NULL;
    if (iohdlc_sem_wait_timeout(&drv->rx.recept_sem, tmo) == MSG_OK) {
      IOHDLC_RAWQ_LOCK(drv->rx.recept_mtx);
      if (!ioHdlc_frameq_isempty(&drv->rx.recept_q)) {
        iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(&drv->rx.recept_q);
        fp = IOHDLC_FRAME_FROM_Q(qh);
      }
      IOHDLC_RAWQ_UNLOCK(drv->rx.recept_mtx);
    }
    if (fp == NULL)
      return NULL;

    /*
     * The RX state machine delivers frames with any configured FCS still
     * present in the buffer. Validate first, then restore the core payload
     * view before returning the frame upward.
     */
    size_t total_len = fp->elen;
    bool fcs_ok = true;
    bool fff_ok = true;

    if (drv->config.apply_transparency) {
      ioHdlcFrameTransparentDecode(fp, fp);
      total_len = fp->elen;
    }

    if (drv->config.fcs_size > 0U) {
      IOHDLC_ASSERT(drv->fcs.rx_check_fn != NULL, "drv_recv_frame: missing RX FCS check hook");
      fcs_ok = drv->fcs.rx_check_fn(drv, fp->frame, total_len);
    }

    if (drv->config.frame_format_size != 0U && total_len > 0U) {
      uint16_t declared_len = fp->frame[0];
      if ((declared_len & 0x80U) != 0U)
        declared_len = ((declared_len & 0x0FU) << 8) | fp->frame[1];
      fff_ok = (declared_len == total_len);
    }

    if (fcs_ok && fff_ok)
      break;

    hdlcReleaseFrame(drv->fpp, fp);
  }

  fp->elen -= drv->config.fcs_size;
  return fp;
}

/**
 * @brief   Frame sender callback: TX complete.
 * @details Releases the completed frame and, on non-mock backends, kicks the
 *          next queued transmission from the auxiliary TX queue.
 */
static void s_on_tx_done(void *cb_ctx, void *framep) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)cb_ctx;
  iohdlc_frame_t *done_fp = (iohdlc_frame_t *)framep;
  iohdlc_frame_t *next_fp = NULL;

#ifndef IOHDLC_USE_MOCK_ADAPTER
  /* Swdriver-owned TX queue callback path. */
  iohdlc_sys_lock_isr();
  if (drv->tx.inflight_fp == done_fp)
    drv->tx.inflight_fp = NULL;

  if (done_fp != NULL && drv->tx.shadow_fp == done_fp) {
    if (drv->tx.shadow_prefix_len > 0U)
      memcpy(done_fp->frame, drv->tx.shadow_prefix, drv->tx.shadow_prefix_len);
    if (drv->tx.shadow_suffix_len > 0U)
      memcpy(&done_fp->frame[done_fp->elen], drv->tx.shadow_suffix, drv->tx.shadow_suffix_len);

    drv->tx.shadow_fp = NULL;
    drv->tx.shadow_prefix_len = 0U;
    drv->tx.shadow_suffix_len = 0U;
  }
  
  if (!ioHdlc_frameq_isempty(&drv->tx.raw_q)) {
    iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(&drv->tx.raw_q);
    next_fp = IOHDLC_FRAME_FROM_Q_AUX(qh);
    /* The frame leaves the auxiliary TX queue before the next submit. */
    next_fp->q_aux.next = NULL;
    next_fp->q_aux.prev = NULL;
  }

  if (next_fp) {
    drv->tx.inflight_fp = next_fp;
    int32_t ret = drv->port.handle.ops->tx_submit_frame(drv->port.handle.ctx, next_fp);
    IOHDLC_ASSERT(ret == 0, "tx_submit_frame rejected queued frame");
  }
  iohdlc_sys_unlock_isr();
#endif

  if (done_fp)
    hdlcReleaseFrame(drv->fpp, done_fp);
}

/*===========================================================================*/
/* Driver VMT                                                                */
/*===========================================================================*/

static const struct _iohdlc_driver_vmt s_vmt = {
  .start                 = drv_start,
  .stop                  = drv_stop,
  .send_frame            = drv_send_frame,
  .recv_frame            = drv_recv_frame,
  .get_capabilities      = drv_get_capabilities,
  .configure             = drv_configure
};

static const ioHdlcStreamDriverOps s_stream_drvops = {
  .build_tx_plan         = s_build_tx_plan,
};

/** @} */
