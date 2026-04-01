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

/*===========================================================================*/
/* Forward declarations                                                      */
/*===========================================================================*/

/* ioHdlcDriver VMT implementation */
static void drv_start(void *instance, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp);
static void drv_stop(void *instance);
static size_t drv_send_frame(void *instance, iohdlc_frame_t *fp);
static iohdlc_frame_t *drv_recv_frame(void *instance, iohdlc_timeout_t tmo);
static const ioHdlcDriverCapabilities* drv_get_capabilities(void *instance);
static int32_t drv_configure(void *instance, uint8_t fcs_size, bool transparency, uint8_t fff_type);

/* Internal RX state machine */
static void s_on_rx(void *cb_ctx, uint32_t errmask);
static void s_on_tx_done(void *cb_ctx, void *framep);
static void s_on_rx_error(void *cb_ctx, uint32_t errmask);
static void s_handle_rx_error(ioHdlcSwDriver *d);

/*===========================================================================*/
/* Driver Capabilities                                                       */
/*===========================================================================*/

static const ioHdlcDriverCapabilities s_swdriver_caps = {
  .fcs = {
    .supported_sizes = {0, 2, 0, 0},  /* Supports FCS 0 (none) and 2 (16-bit) */
    .default_size = 2,
    .hw_support = false
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

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/**
 * @brief   Initialize software HDLC driver.
 * @details Initializes the driver object to a valid pre-start state.
 *          After initialization, the caller may configure the driver and then
 *          start it through the generic framed-driver API.
 * @param[in] drv   Driver instance to initialize.
 */
void ioHdlcSwDriverInit(ioHdlcSwDriver *drv) {
  drv->vmt = &s_vmt;
  drv->fpp = NULL;
  drv->fcs_size = 2;              /* Default: 16-bit FCS */
  drv->apply_transparency = false;
  drv->frame_format_size = 0;
  
  drv->rx_stagep = NULL;
  drv->rx_in_frame = NULL;
  drv->started = false;
  
  ioHdlc_frameq_init(&drv->raw_recept_q);
  iohdlc_sem_init(&drv->raw_recept_sem, 0);
  IOHDLC_RAWQ_MUTEX_INIT(drv->raw_recept_mtx);

#ifndef IOHDLC_USE_MOCK_ADAPTER
  /* Init TX queue (real HW only) */
  ioHdlc_frameq_init(&drv->raw_tx_q);
#endif
}

/*===========================================================================*/
/* ioHdlcDriver VMT Implementation                                          */
/*===========================================================================*/

static void drv_start(void *instance, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp) {
  (void)phyconfigp;
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  ioHdlcStreamPort *portp = (ioHdlcStreamPort *)phyp;

  drv->fpp = fpp;
  drv->port = *portp;  /* Copy port handle */

  /* Setup HAL callbacks */
  drv->hal_cbs.on_rx = s_on_rx;
  drv->hal_cbs.on_tx_done = s_on_tx_done;
  drv->hal_cbs.on_rx_error = s_on_rx_error;
  drv->hal_cbs.cb_ctx = drv;

  /* Allocate DMA-safe staging buffer */
  drv->rx_stagep = (uint8_t *)iohdlc_dma_alloc(1u, IOHDLC_DMA_ALIGN_DEFAULT);
  IOHDLC_ASSERT(drv->rx_stagep != NULL, "DMA-safe staging allocation failed");
  *drv->rx_stagep = 0;
  drv->rx_in_frame = NULL;

  /* Start port and begin RX */
  drv->port.ops->start(drv->port.ctx, &drv->hal_cbs);
  iohdlc_sys_lock();
  (void)drv->port.ops->rx_submit(drv->port.ctx, drv->rx_stagep, 1);
  iohdlc_sys_unlock();
  
  drv->started = true;
}

static void drv_stop(void *instance) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  
  if (!drv || !drv->started) {
    return;
  }
  
  /* Stop port (terminates RX thread) */
  if (drv->port.ops && drv->port.ops->stop) {
    drv->port.ops->stop(drv->port.ctx);
  }
  
  /* Free staging buffer */
  if (drv->rx_stagep) {
    iohdlc_dma_free(drv->rx_stagep);
    drv->rx_stagep = NULL;
  }
  
  drv->started = false;
}

static size_t drv_send_frame(void *instance, iohdlc_frame_t *fp) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  iohdlc_frame_t *nfp = fp;
  size_t payload_len = fp->elen;  /* Core semantics: no FCS */
  size_t wire_len = payload_len;

  if (payload_len > 0) {
    /* If FFF enabled, valorize it NOW (driver knows total wire length) */
    if (drv->frame_format_size) {
      uint16_t total_wire_len = payload_len + drv->fcs_size;
      
      if (drv->frame_format_size == 1) {
        /* TYPE 0: 1 byte, bit 7=0 */
        fp->frame[0] = (uint8_t)total_wire_len;
      } else if (drv->frame_format_size == 2) {
        /* TYPE 1: 2 bytes, bit 15-12=1000 */
        fp->frame[0] = 0x80 | ((total_wire_len >> 8) & 0x0F);
        fp->frame[1] = (uint8_t)(total_wire_len & 0xFF);
      } else {
        return EMSGSIZE;  /* Message too long */
      }
    }
    
    /* Add FCS at offset WITHOUT modifying fp->elen */
    if (drv->fcs_size > 0) {
      ioHdlcFrameAddFCS_at(fp, payload_len);
      wire_len = payload_len + drv->fcs_size;
    }
    
    /* Apply transparency encoding if configured (requires copy) */
    if (drv->apply_transparency) {
      nfp = hdlcTakeFrame(drv->fpp);
      if (nfp == NULL)
        return ENOMEM;  /* No memory (errno-compatible) */
      (void)ioHdlcFrameTransparentEncode(nfp, fp);
      wire_len = nfp->elen;  /* Transparency changes length */
    } else {
      hdlcAddRef(drv->fpp, nfp);
    }
  }

  /* Add closing FLAG */
  nfp->frame[wire_len++] = IOHDLC_FLAG;

#ifdef IOHDLC_USE_MOCK_ADAPTER
  /* Mock adapter: direct submit with opening flag if idle */
  nfp->openingflag = 0;
  if (drv->port.ops && drv->port.ops->tx_busy &&
      !drv->port.ops->tx_busy(drv->port.ctx)) {
    nfp->openingflag = IOHDLC_FLAG;
  }
  const uint8_t *ptr = nfp->openingflag ? &nfp->openingflag : nfp->frame;
  size_t len = nfp->openingflag ? wire_len + 1 : wire_len;
  
  /* Submit TX (spin until accepted) */
  while (!drv->port.ops->tx_submit(drv->port.ctx, ptr, len, (void *)nfp)) {
    iohdlc_thread_yield();
  }
#else
  /* Real HW: enqueue for ISR processing */
  iohdlc_sys_lock();
  
  if (!drv->port.ops->tx_busy(drv->port.ctx)) {
    /* TX idle: kickstart directly (don't enqueue) */
    nfp->openingflag = IOHDLC_FLAG;  /* First frame needs opening flag */
    
    const uint8_t *ptr = &nfp->openingflag;
    size_t len = wire_len + 1;  /* +1 for opening flag */
    (void)drv->port.ops->tx_submit(drv->port.ctx, ptr, len, (void *)nfp);
  } else {
    /* TX busy: enqueue for ISR processing (no opening flag) */
    if (nfp->q_aux.next == NULL) {
      nfp->openingflag = 0;  /* Contiguous with previous frame */
      ioHdlc_frameq_insert(&drv->raw_tx_q, &nfp->q_aux);
    } else {
      /* This can only happen if the caller retrasmit frames
         already enqueued. */
      hdlcReleaseFrame(drv->fpp, nfp);  /* Release new frame */
    }
  }
  iohdlc_sys_unlock();
#endif

  return 0;
}

static iohdlc_frame_t *drv_recv_frame(void *instance, iohdlc_timeout_t tmo) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  iohdlc_frame_t *fp;

  for (;;) {
    /* Wait for RX frame or timeout */
    fp = NULL;
    if (iohdlc_sem_wait_timeout(&drv->raw_recept_sem, tmo) == MSG_OK) {
      IOHDLC_RAWQ_LOCK(drv->raw_recept_mtx);
      if (!ioHdlc_frameq_isempty(&drv->raw_recept_q)) {
        iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(&drv->raw_recept_q);
        fp = IOHDLC_FRAME_FROM_Q(qh);
      }
      IOHDLC_RAWQ_UNLOCK(drv->raw_recept_mtx);
    }
    if (fp == NULL)
      return NULL; /* Timeout or idle */

    /* At this point fp->elen includes FCS (from RX state machine) */
    size_t total_len = fp->elen;
    
    /* Decode transparency if configured */
    if (drv->apply_transparency) {
      ioHdlcFrameTransparentDecode(fp, fp);
      total_len = fp->elen;  /* Updated after decode */
    }

    /* Validate FCS using explicit total_len */
    bool fcs_ok = true;
    if (drv->fcs_size > 0) {
      fcs_ok = ioHdlcFrameCheckFCS_at(fp, total_len);
    }
    
    /* Validate FFF if present */
    bool fff_ok = true;
    if (drv->frame_format_size && total_len > 0) {
      uint16_t declared_len = fp->frame[0];
      if (declared_len & 0x80) {
        /* TYPE 1: 2 bytes */
        declared_len = ((declared_len & 0x0F) << 8) | fp->frame[1];
      }
      fff_ok = (declared_len == total_len);
    }
    
    if (!fcs_ok || !fff_ok) {
      /* Bad frame - discard and retry */
      hdlcReleaseFrame(drv->fpp, fp);
      continue;
    }
    
    break;
  }

  /* Remove FCS from payload (restore Core semantics) */
  fp->elen -= drv->fcs_size;
  return fp;
}

static const ioHdlcDriverCapabilities* drv_get_capabilities(void *instance) {
  (void)instance;
  return &s_swdriver_caps;
}

static int32_t drv_configure(void *instance, uint8_t fcs_size, bool transparency, uint8_t fff_type) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  
  /* Validate FCS size against supported sizes */
  bool valid_fcs = false;
  for (int i = 0; i < 4; i++) {
    if (s_swdriver_caps.fcs.supported_sizes[i] == fcs_size) {
      valid_fcs = true;
      break;
    }
  }
  if (!valid_fcs) {
    return ENOTSUP;  /* Operation not supported */
  }
  
  /* Validate FFF type against supported types */
  bool valid_fff = false;
  for (int i = 0; i < 4; i++) {
    if (s_swdriver_caps.fff.supported_types[i] == fff_type) {
      valid_fff = true;
      break;
    }
  }
  if (!valid_fff) {
    return ENOTSUP;  /* FFF type not supported */
  }
  
  /* CRITICAL: FFF and Transparency are mutually exclusive */
  if (transparency && fff_type != 0) {
    return EINVAL;  /* Invalid argument */
  }
  
  /* Validate transparency support */
  if (transparency && !s_swdriver_caps.transparency.hw_support && 
      !s_swdriver_caps.transparency.sw_available) {
    return ENOTSUP;
  }
  
  /* Configuration is valid - store it */
  drv->fcs_size = fcs_size;
  drv->apply_transparency = transparency;
  drv->frame_format_size = fff_type;
  
  return 0;  /* Success */
}

/*===========================================================================*/
/* RX Multi-Chunk State Machine (Internal)                                  */
/*===========================================================================*/

/**
 * @brief   Abort the current RX frame under construction.
 * @details Releases any partially assembled frame and resets the staging byte
 *          so the RX state machine can restart on the next delimiter.
 */
static void s_handle_rx_error(ioHdlcSwDriver *drv) {
  if (drv->rx_in_frame) {
    hdlcReleaseFrame(drv->fpp, drv->rx_in_frame);
    drv->rx_in_frame = NULL;
  }
  drv->rx_stagep[0] = 0;  /* Reset staging buffer */
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
    if (drv->rx_in_frame && drv->rx_in_frame->elen != 0) {
      /* Intra-frame timeout - discard partial frame */
      s_handle_rx_error(drv);
      iohdlc_sys_lock_isr();
      drv->port.ops->rx_cancel(drv->port.ctx);
      iohdlc_sys_unlock_isr();
      goto newframe;
    }
    /* Inter-frame timeout - signal IDLE */
    IOHDLC_RAWQ_LOCK_ISR(drv->raw_recept_mtx);
    iohdlc_sem_signal_i(&drv->raw_recept_sem);
    IOHDLC_RAWQ_UNLOCK_ISR(drv->raw_recept_mtx);
    return;
  }
  
  /* Handle other errors */
  if (errmask) {
    s_handle_rx_error(drv);
    iohdlc_sys_lock_isr();
    drv->port.ops->rx_cancel(drv->port.ctx);
    iohdlc_sys_unlock_isr();
    goto newframe;
  }

  /* Process received byte */
  if (drv->rx_in_frame != NULL) {
    b = &drv->rx_in_frame->frame[drv->rx_in_frame->elen];
    
    if (*b == IOHDLC_FLAG) {
      /* Frame complete */
      if (!drv->rx_in_frame->elen)
        goto nextoctet;  /* Empty frame */

      if (drv->rx_in_frame->elen < HDLC_BASIC_MIN_L) {
        /* Too short - discard */
        drv->rx_in_frame->elen = 0;
        b = &drv->rx_in_frame->frame[0];
        goto nextoctet;
      }

      /* Deliver frame to upper layer */
      IOHDLC_RAWQ_LOCK_ISR(drv->raw_recept_mtx);
      ioHdlc_frameq_insert(&drv->raw_recept_q, &drv->rx_in_frame->q);
      iohdlc_sem_signal_i(&drv->raw_recept_sem);
      IOHDLC_RAWQ_UNLOCK_ISR(drv->raw_recept_mtx);
      
      drv->rx_in_frame = NULL;
      *drv->rx_stagep = IOHDLC_FLAG;  /* FLAG separator is also opening FLAG */
      
    } else {
      /* Check for Frame Format Field (FFF) */
      if (drv->frame_format_size && !drv->apply_transparency) {
        if (drv->frame_format_size == 1 && drv->rx_in_frame->elen == 0) {
          /* TYPE 0: 1 byte FFF, bit 7 = 0 */
          if (!(drv->rx_in_frame->frame[0] & 0x80) &&
              ((size_t)drv->rx_in_frame->frame[0] != 0) &&
              ((size_t)drv->rx_in_frame->frame[0] < drv->fpp->framesize)) {
            /* Valid TYPE 0 FFF - read exact length */
            n = (size_t)drv->rx_in_frame->frame[0];
            drv->rx_in_frame->elen = n;
            b = &drv->rx_in_frame->frame[1];
          } else {
            /* Invalid TYPE 0 FFF - discard frame */
            s_handle_rx_error(drv);
            n = 1;
          }
        } else if (drv->frame_format_size == 2) {
          if (drv->rx_in_frame->elen == 0) {
            /* TYPE 1: first byte, bit 15-12 = 1000 */
            if ((drv->rx_in_frame->frame[0] & 0xF0) == 0x80) {
              /* Valid TYPE 1 first byte - continue to read second byte */
              ++drv->rx_in_frame->elen;
              b = &drv->rx_in_frame->frame[1];
            } else {
              /* Invalid TYPE 1 first byte - discard frame */
              s_handle_rx_error(drv);
            }
          } else if (drv->rx_in_frame->elen == 1) {
            /* TYPE 1: second byte received, calculate total length */
            n = ((drv->rx_in_frame->frame[0] & 0x0F) << 8) | 
                                 drv->rx_in_frame->frame[1];
            if (n && n < drv->fpp->framesize) {
              /* Valid TYPE 1 FFF - read exact length */
              drv->rx_in_frame->elen = n--; /* decrement of the 2nd fff byte. */
              b = &drv->rx_in_frame->frame[2];
            } else {
              /* Frame too large - discard */
              s_handle_rx_error(drv);
              n = 1;
            }
          } else {
            /* Already past FFF bytes - continue accumulating */
            ++b;
            if (++drv->rx_in_frame->elen >= drv->fpp->framesize) {
              s_handle_rx_error(drv);
            }
          }
        } else {
          /* Continue accumulating bytes (no FFF or unknown type) */
          ++b;
          if (++drv->rx_in_frame->elen >= drv->fpp->framesize) {
            s_handle_rx_error(drv);
          }
        }
      } else {
        /* No FFF - continue accumulating bytes */
        ++b;
        if (++drv->rx_in_frame->elen >= drv->fpp->framesize) {
          /* Frame too large - discard */
          s_handle_rx_error(drv);
        }
      }
    }
  }

newframe:
  if (!drv->rx_in_frame) {
    b = drv->rx_stagep;

    if (*b != IOHDLC_FLAG)
      goto nextoctet;

    *drv->rx_stagep = 0;

    /* Allocate new frame buffer */
    drv->rx_in_frame = hdlcTakeFrame(drv->fpp);
    if (drv->rx_in_frame == NULL)
      goto nextoctet;
    
    drv->rx_in_frame->elen = 0;
    b = &drv->rx_in_frame->frame[0];
  }

nextoctet:
  /* Arm next RX byte/chunk */
  IOHDLC_ASSERT(n != 0, "Invalid RX chunk size");
  iohdlc_sys_lock_isr();
  (void)drv->port.ops->rx_submit(drv->port.ctx, b, n);
  iohdlc_sys_unlock_isr();
}

/**
 * @brief   Frame sender callback: TX complete.
 * @details Releases the completed frame and, on non-mock backends, kicks the
 *          next queued transmission from the auxiliary TX queue.
 */
static void s_on_tx_done(void *cb_ctx, void *framep) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)cb_ctx;
  
  /* Release completed frame */
  if (framep) {
    hdlcReleaseFrame(drv->fpp, (iohdlc_frame_t *)framep);
  }

#ifndef IOHDLC_USE_MOCK_ADAPTER
  /* Real HW: process next frame from TX queue */
  iohdlc_sys_lock_isr();
  
  iohdlc_frame_t *next_fp = NULL;
  if (!ioHdlc_frameq_isempty(&drv->raw_tx_q)) {
    iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(&drv->raw_tx_q);
    next_fp = IOHDLC_FRAME_FROM_Q_AUX(qh);
    next_fp->q_aux.next = NULL;  /* Clear auxiliary queue link for safety */
  }
  
  /* Submit next frame if available */
  if (next_fp) {
    /* Recalculate wire_len (don't rely on frame fields which may be reused) */
    size_t wire_len;
    if (drv->apply_transparency) {
      /* Frame is encoded: elen already includes encoded payload+FCS */
      wire_len = next_fp->elen + 1;  /* +1 for closing FLAG */
    } else {
      /* Frame is original with FCS added in place */
      wire_len = next_fp->elen + drv->fcs_size + 1;  /* +FCS +FLAG */
    }
    
    (void)drv->port.ops->tx_submit(drv->port.ctx, next_fp->frame,
                                   wire_len, next_fp);
  }
  iohdlc_sys_unlock_isr();
#endif
}

/**
 * @brief   Frame parser callback: RX error.
 */
static void s_on_rx_error(void *cb_ctx, uint32_t errmask) {
  s_on_rx(cb_ctx, errmask);
}

/** @} */
