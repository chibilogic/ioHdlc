/*
    ioHdlc - Copyright (C) 2024 Isidoro Orabona

    GNU General Public License Usage

    ioHdlc software is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ioHdlc software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with ioHdlc software.  If not, see <http://www.gnu.org/licenses/>.

    Commercial License Usage

    Licensees holding valid commercial ioHdlc licenses may use this file in
    accordance with the commercial license agreement provided in accordance with
    the terms contained in a written agreement between you and Isidoro Orabona.
    For further information contact via email on github account.
*/

/**
 * @file    ioHdlcswdriver.c
 * @brief   HDLC software driver (unified implementation).
 * @details Integrates RX multi-chunk state machine + protocol logic (FCS, transparency).
 */

#include "ioHdlcswdriver.h"
#include "ioHdlcosal.h"
#include "ioHdlcll.h"

/*===========================================================================*/
/* Forward declarations                                                      */
/*===========================================================================*/

/* ioHdlcDriver VMT implementation */
static void drv_start(void *instance, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp);
static size_t drv_send_frame(void *instance, iohdlc_frame_t *fp);
static iohdlc_frame_t *drv_recv_frame(void *instance, iohdlc_timeout_t tmo);
static bool drv_get_hwtransparency(void *instance);
static void drv_set_applytransparency(void *instance, bool tr);
static void drv_set_hasframeformat(void *instance, bool hff);

/* Internal RX state machine */
static void s_hal_on_rx(void *cb_ctx, uint32_t errmask);
static void s_hal_on_tx_done(void *cb_ctx, void *framep);
static void s_hal_on_rx_error(void *cb_ctx, uint32_t errmask);
static void s_handle_rx_error(ioHdlcSwDriver *d);

/*===========================================================================*/
/* Driver VMT                                                                */
/*===========================================================================*/

static const struct _iohdlc_driver_vmt s_vmt = {
  .start                 = drv_start,
  .send_frame            = drv_send_frame,
  .recv_frame            = drv_recv_frame,
  .get_hwtransparency    = drv_get_hwtransparency,
  .set_applytransparency = drv_set_applytransparency,
  .set_hasframeformat    = drv_set_hasframeformat
};

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void ioHdlcSwDriverInit(ioHdlcSwDriver *drv) {
  drv->vmt = &s_vmt;
  drv->fpp = NULL;
  drv->apply_transparency = false;
  drv->has_frame_format = false;
  
  drv->rx_stagep = NULL;
  drv->rx_in_frame = NULL;
  drv->started = false;
  
  ioHdlc_frameq_init(&drv->raw_recept_q);
  iohdlc_sem_init(&drv->raw_recept_sem, 0);
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
  drv->hal_cbs.on_rx = s_hal_on_rx;
  drv->hal_cbs.on_tx_done = s_hal_on_tx_done;
  drv->hal_cbs.on_rx_error = s_hal_on_rx_error;
  drv->hal_cbs.cb_ctx = drv;

  /* Allocate DMA-safe staging buffer */
  drv->rx_stagep = (uint8_t *)iohdlc_dma_alloc(1u, IOHDLC_DMA_ALIGN_DEFAULT);
  IOHDLC_ASSERT(drv->rx_stagep != NULL, "DMA-safe staging allocation failed");
  *drv->rx_stagep = 0;
  drv->rx_in_frame = NULL;

  /* Start port and begin RX */
  drv->port.ops->start(drv->port.ctx, &drv->hal_cbs);
  (void)drv->port.ops->rx_submit(drv->port.ctx, drv->rx_stagep, 1);
  
  drv->started = true;
}

static size_t drv_send_frame(void *instance, iohdlc_frame_t *fp) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  iohdlc_frame_t *nfp = fp;
  size_t size = 0;

  if (fp->elen) {
    /* Add FCS */
    frameAddFCS(fp);
    size = fp->elen;
    
    /* Apply transparency encoding if configured */
    if (drv->apply_transparency) {
      nfp = hdlcTakeFrame(drv->fpp);
      if (nfp == NULL)
        return 1; /* NOMEM */
      (void)frameTransparentEncode(nfp, fp);
      size = nfp->elen;
    } else {
      hdlcAddRef(drv->fpp, nfp);
    }
  }

  /* Add closing FLAG */
  nfp->frame[size] = HDLC_FLAG;
  nfp->openingflag = 0;
  
  /* Add opening FLAG if line idle */
  if (drv->port.ops && drv->port.ops->tx_busy &&
      !drv->port.ops->tx_busy(drv->port.ctx)) {
    nfp->openingflag = HDLC_FLAG;
    ++size;
  }

  const uint8_t *ptr = nfp->openingflag ? &nfp->openingflag : nfp->frame;
  size_t len = size + 1; /* Include closing FLAG */

  /* Submit TX (spin until accepted) */
  while (!drv->port.ops->tx_submit(drv->port.ctx, ptr, len, (void *)nfp)) {
    iohdlc_thread_yield();
  }
  
  return 0;
}

static iohdlc_frame_t *drv_recv_frame(void *instance, iohdlc_timeout_t tmo) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  iohdlc_frame_t *fp;

  for (;;) {
    /* Wait for RX frame or timeout */
    fp = NULL;
    if (iohdlc_sem_wait_ok(&drv->raw_recept_sem, tmo)) {
      iohdlc_sys_lock();
      if (!ioHdlc_frameq_isempty(&drv->raw_recept_q))
        fp = ioHdlc_frameq_remove(&drv->raw_recept_q);
      iohdlc_sys_unlock();
    }
    if (fp == NULL)
      return NULL; /* Timeout or idle */

    /* Decode transparency if configured */
    if (drv->apply_transparency)
      frameTransparentDecode(fp, fp);

    /* Validate FCS and frame format */
    if (frameCheckFCS(fp) && (!drv->has_frame_format || (fp->elen == fp->frame[0])))
      break;

    /* Bad frame - discard and retry */
    hdlcReleaseFrame(drv->fpp, fp);
  }

  /* Remove FCS from payload */
  fp->elen -= 2;
  return fp;
}

static bool drv_get_hwtransparency(void *instance) {
  (void)instance;
  return false;  /* Software transparency */
}

static void drv_set_applytransparency(void *instance, bool tr) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  drv->apply_transparency = tr;
}

static void drv_set_hasframeformat(void *instance, bool hff) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)instance;
  drv->has_frame_format = hff;
}

/*===========================================================================*/
/* RX Multi-Chunk State Machine (Internal)                                  */
/*===========================================================================*/

static void s_handle_rx_error(ioHdlcSwDriver *drv) {
  if (drv->rx_in_frame) {
    hdlcReleaseFrame(drv->fpp, drv->rx_in_frame);
    drv->rx_in_frame = NULL;
  }
}

/**
 * @brief   HAL callback: RX byte/chunk received or timeout.
 */
static void s_hal_on_rx(void *cb_ctx, uint32_t errmask) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)cb_ctx;
  size_t n = 1;
  uint8_t *b;

  if (!drv || !drv->started)
    return;

  /* Handle timeout */
  if (errmask & IOHDLC_STREAM_ERR_TMO) {
    if (drv->rx_in_frame && drv->rx_in_frame->elen != 0) {
      /* Intra-frame timeout - discard partial frame */
      s_handle_rx_error(drv);
      if (drv->port.ops->rx_cancel)
        drv->port.ops->rx_cancel(drv->port.ctx);
      goto newframe;
    }
    /* Inter-frame timeout - signal IDLE */
    iohdlc_sys_lock_isr();
    iohdlc_sem_signal_i(&drv->raw_recept_sem);
    iohdlc_sys_unlock_isr();
    return;
  }
  
  /* Handle other errors */
  if (errmask) {
    s_handle_rx_error(drv);
    goto newframe;
  }

  /* Process received byte */
  if (drv->rx_in_frame != NULL) {
    b = &drv->rx_in_frame->frame[drv->rx_in_frame->elen];
    
    if (*b == HDLC_FLAG) {
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
      ioHdlc_frameq_insert(&drv->raw_recept_q, drv->rx_in_frame);
      iohdlc_sys_lock_isr();
      iohdlc_sem_signal_i(&drv->raw_recept_sem);
      iohdlc_sys_unlock_isr();
      
      drv->rx_in_frame = NULL;
      *drv->rx_stagep = HDLC_FLAG;  /* FLAG separator is also opening FLAG */
      
    } else {
      /* Check for Frame Format Field (first byte = length) */
      if ((drv->rx_in_frame->elen == 0) &&
           drv->has_frame_format && !drv->apply_transparency) {
        if (!(drv->rx_in_frame->frame[0] & 0x80) &&
            ((size_t)drv->rx_in_frame->frame[0] < drv->fpp->framesize)) {
          /* Valid FFF - read exact length */
          n = (size_t)drv->rx_in_frame->frame[0];
          drv->rx_in_frame->elen = n;
          b = &drv->rx_in_frame->frame[1];
        } else {
          /* Invalid FFF - discard frame */
          s_handle_rx_error(drv);
        }
      } else {
        /* Continue accumulating bytes */
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

    if (*b != HDLC_FLAG)
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
  (void)drv->port.ops->rx_submit(drv->port.ctx, b, n);
}

/**
 * @brief   HAL callback: TX complete.
 */
static void s_hal_on_tx_done(void *cb_ctx, void *framep) {
  ioHdlcSwDriver *drv = (ioHdlcSwDriver *)cb_ctx;
  if (framep) {
    hdlcReleaseFrame(drv->fpp, (iohdlc_frame_t *)framep);
  }
}

/**
 * @brief   HAL callback: RX error.
 */
static void s_hal_on_rx_error(void *cb_ctx, uint32_t errmask) {
  s_hal_on_rx(cb_ctx, errmask);
}
