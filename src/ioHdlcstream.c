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
 * @file    ioHdlcstream.c
 * @brief   OS-agnostic byte-stream core for frame-oriented HDLC drivers.
*/

#include "ioHdlcstream.h"
#include "ioHdlcosal.h"
#include "ioHdlcll.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"

/*===========================================================================*/
/* Local definitions.                                                        */
/*===========================================================================*/

/*===========================================================================*/
/* Local functions.                                                          */
/*===========================================================================*/

static void s_hal_on_rx(void *cb_ctx, uint32_t errmask);
static void s_hal_on_tx_done(void *cb_ctx, void *framep);
static void s_hal_on_rx_error(void *cb_ctx, uint32_t errmask);

/*===========================================================================*/
/* Exported functions.                                                       */
/*===========================================================================*/

/**
 * @brief   Initializes the stream helper.
 */
bool ioHdlcStream_init(ioHdlcStream              *d,
                       const ioHdlcStreamPort   *port,
                       const ioHdlcStreamConfig *cfg,
                       ioHdlcFramePool          *pool,
                       void                     *upper_ctx) {
  if (!d || !port || !port->ops || !cfg || !pool || !cfg->deliver_rx_frame)
    return false;

  /* Validate required port ops once and treat them as invariants. */
  IOHDLC_ASSERT(port->ops->start != NULL, "port->ops->start must be set");
  IOHDLC_ASSERT(port->ops->rx_submit != NULL, "port->ops->rx_submit must be set");
  IOHDLC_ASSERT(port->ops->tx_submit != NULL, "port->ops->tx_submit must be set");

  d->port     = *port;
  d->cfg      = *cfg;
  d->upper_ctx = upper_ctx;
  d->pool      = pool;

  d->hal_cbs.on_rx       = s_hal_on_rx;
  d->hal_cbs.on_tx_done  = s_hal_on_tx_done;
  d->hal_cbs.on_rx_error = s_hal_on_rx_error;
  d->hal_cbs.cb_ctx      = d;

  d->rx_stagep = (uint8_t *)iohdlc_dma_alloc(1u, IOHDLC_DMA_ALIGN_DEFAULT);
  if (!d->rx_stagep) {
    return false; /* DMA-safe staging not available */
  }
  *d->rx_stagep = 0;
  d->rx_in_frame    = NULL;
  /* Framesize is available from pool->framesize when needed. */
  d->started        = false;

  return true;
}

/**
 * @brief   Starts the stream port and arms the initial RX header.
 */
bool ioHdlcStream_start(ioHdlcStream *d) {
  if (!d || d->started)
    return false;

  /* Invariants validated at init. */
  IOHDLC_ASSERT(d->port.ops && d->port.ops->start && d->port.ops->rx_submit,
                "invalid port ops at start");

  d->port.ops->start(d->port.ctx, &d->hal_cbs);

  /* Start in idle state: read 1 byte searching for FLAG. */
  d->rx_in_frame = NULL;
  (void)d->port.ops->rx_submit(d->port.ctx, d->rx_stagep, 1);

  d->started = true;
  return true;
}

/**
 * @brief   Stops RX/TX operations.
 */
void ioHdlcStream_stop(ioHdlcStream *d) {
  if (!d || !d->started) return;
  /* Mark as stopped first to ignore any late callbacks. */
  d->started = false;
  /* Optional ops: call if provided by the adapter. */
  if (d->port.ops->rx_cancel) d->port.ops->rx_cancel(d->port.ctx);
  if (d->port.ops->stop)      d->port.ops->stop(d->port.ctx);
}

/**
 * @brief   Deinitializes the stream helper and releases resources.
 * @note    If the stream is started, it is stopped first.
 */
void ioHdlcStream_deinit(ioHdlcStream *d) {
  if (!d) return;
  if (d->started) ioHdlcStream_stop(d);
  if (d->rx_in_frame) {
    hdlcReleaseFrame(d->pool, d->rx_in_frame);
    d->rx_in_frame = NULL;
  }
  if (d->rx_stagep) {
    iohdlc_dma_free(d->rx_stagep);
    d->rx_stagep = NULL;
  }
}

/**
 * @brief   Submits a TX buffer to the adapter.
 */
bool ioHdlcStream_send(ioHdlcStream *d, const uint8_t *ptr, size_t len, void *framep) {
  if (!d || !ptr || len == 0)
    return false;
  /* Invariants validated at init. */
  IOHDLC_ASSERT(d->port.ops && d->port.ops->tx_submit, "invalid port ops at send");
  return d->port.ops->tx_submit(d->port.ctx, ptr, len, framep);
}

/*===========================================================================*/
/* Local functions.                                                          */
/*===========================================================================*/

/**
 * @brief   Handles an RX error by discarding the current frame.
 */
static void s_handle_rx_error(ioHdlcStream *d) {
  if (d->rx_in_frame)
    hdlcReleaseFrame(d->pool, d->rx_in_frame);
  d->rx_in_frame = NULL;
}

/**
 * @brief   HAL callback: end of an RX chunk (or timeout).
 */
static void s_hal_on_rx(void *cb_ctx, uint32_t errmask) {
  ioHdlcStream *d = (ioHdlcStream *)cb_ctx;
  size_t n = 1;
  uint8_t *b;

  if (!d || !d->started) return;

  if (errmask & IOHDLC_STREAM_ERR_TMO) {
    /* A timeout occurred while receiving a frame, alias
       it is a intra-frame timeout.
       Discard the current frame and start receiving a
       new frame. */
    if (d->rx_in_frame && d->rx_in_frame->elen != 0) {
      s_handle_rx_error(d);
      (void)d->port.ops->rx_cancel(d->port.ctx);
      goto newframe;
    }
    /* A timeout occurred after receiving a frame, alias
       it is a inter-frame timeout. The line can therefore be
       considered idle. Signal IDLE line to higher levels.*/
    d->cfg.deliver_rx_frame(d->upper_ctx, NULL, 0);
    return;
  } else if (errmask) {
      s_handle_rx_error(d);
      goto newframe;
  }

  if (NULL != d->rx_in_frame) {

    /* It's in the state of receiving a started frame.*/
    b = &d->rx_in_frame->frame[d->rx_in_frame->elen];
    if (*b == HDLC_FLAG) {

      /* Found a FLAG octet, the frame separator.*/
      if (!d->rx_in_frame->elen)
        goto nextoctet;   /* empty frame.*/

      /* If the length of the frame is incorrect,
         discard the received bad frame.*/
      if (d->rx_in_frame->elen < HDLC_BASIC_MIN_L) {
        d->rx_in_frame->elen = 0;           /* we not need to return the frame
                                               buffer to the pool because the
                                               current octet is the FLAG.*/
        b = &d->rx_in_frame->frame[0];
        goto nextoctet;
      }

      /* The raw frame is ready.
         Delivery it and start
         the reception of a new frame.*/
      d->cfg.deliver_rx_frame(d->upper_ctx, (void *)d->rx_in_frame, d->rx_in_frame->elen);
      d->rx_in_frame = NULL;
      *d->rx_stagep = HDLC_FLAG;  /* the FLAG separator is also a start FLAG.*/
    } else {

      /* The first octet of the frame could be the frame format field
         if configured for this.*/
      if ((d->rx_in_frame->elen == 0) &&
           d->cfg.has_frame_format && !d->cfg.apply_transparency) {

        /* in this case, use the frame length in the frame format field.*/
        if (!(d->rx_in_frame->frame[0] & 0x80) &&
            ((size_t)d->rx_in_frame->frame[0] < d->pool->framesize)) {
          n = (size_t)d->rx_in_frame->frame[0];
          d->rx_in_frame->elen = n;

          /* include closing FLAG in the count of number of octets
             to receive, so doesn't decrement n.*/
          b = &d->rx_in_frame->frame[1];
        } else {
          /* Bad Format Type in frame format field.
             discard the frame, returning the buffer to the pool.*/
          s_handle_rx_error(d);
        }
      } else {
        /* continue to receive octets.*/
        ++b;
        if (++d->rx_in_frame->elen >= d->pool->framesize) {
          /* Bad frame. The number of octets exceeds the frame size.
             Discard the frame, returning the buffer to the pool.*/
          s_handle_rx_error(d);
        }
      }
    }
  }

newframe:
  if (!d->rx_in_frame) {
    b = d->rx_stagep;

    if (*b != HDLC_FLAG) 
      goto nextoctet;

    *d->rx_stagep = 0;

    /* Found the start of new frame, allocate a
       receiving frame buffer.*/
    d->rx_in_frame = hdlcTakeFrame(d->pool);

    if (NULL == d->rx_in_frame)
      goto nextoctet;
    d->rx_in_frame->elen = 0;
    b = &d->rx_in_frame->frame[0];
  }

nextoctet:
  /* Arm next RX chunk. */
  (void)d->port.ops->rx_submit(d->port.ctx, b, n);
}

/**
 * @brief   HAL callback: TX buffer completed.
 */
static void s_hal_on_tx_done(void *cb_ctx, void *framep) {
  ioHdlcStream *d = (ioHdlcStream *)cb_ctx;
  if (framep) {
    /* framep is the frame pointer to be released back to the pool. */
    hdlcReleaseFrame(d->pool, (iohdlc_frame_t *)framep);
  }
}

/**
 * @brief   HAL callback: RX error.
 */
static void s_hal_on_rx_error(void *cb_ctx, uint32_t errmask) {
  s_hal_on_rx(cb_ctx, errmask);
}
