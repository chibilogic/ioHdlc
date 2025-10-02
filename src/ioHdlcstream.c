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
#include "ioHdlcll.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"

/*===========================================================================*/
/* Local definitions.                                                        */
/*===========================================================================*/

/*===========================================================================*/
/* Local functions.                                                          */
/*===========================================================================*/

static void s_hal_on_rx(void *cb_ctx, bool timed_out);
static void s_hal_on_tx_done(void *cb_ctx, void *cookie);
static void s_hal_on_rx_error(void *cb_ctx, uint32_t errmask);

/*===========================================================================*/
/* Exported functions.                                                       */
/*===========================================================================*/

/**
 * @brief   Initializes the UART helper.
 */
bool ioHdlcStream_init(ioHdlcStream              *d,
                       const ioHdlcStreamPort   *port,
                       const ioHdlcStreamConfig *cfg,
                       ioHdlcFramePool          *pool,
                       void                     *upper_ctx) {
  if (!d || !port || !port->ops || !cfg || !pool || !cfg->deliver_rx_frame)
    return false;

  d->port     = *port;
  d->cfg      = *cfg;
  d->upper_ctx = upper_ctx;
  d->pool      = pool;

  d->hal_cbs.on_rx       = s_hal_on_rx;
  d->hal_cbs.on_tx_done  = s_hal_on_tx_done;
  d->hal_cbs.on_rx_error = s_hal_on_rx_error;
  d->hal_cbs.cb_ctx      = d;

  d->rx_flagoctet   = 0;
  d->rx_in_frame    = NULL;
  /* Framesize is available from pool->framesize when needed. */
  d->started        = false;

  return true;
}

/**
 * @brief   Starts the UART port and arms the initial RX header.
 */
bool ioHdlcStream_start(ioHdlcStream *d) {
  if (!d || d->started || !d->port.ops || !d->port.ops->start || !d->port.ops->rx_submit)
    return false;

  d->port.ops->start(d->port.ctx, &d->hal_cbs);

  /* Start in idle state: read 1 byte searching for FLAG. */
  d->rx_in_frame = NULL;
  (void)d->port.ops->rx_submit(d->port.ctx, &d->rx_flagoctet, 1);

  d->started = true;
  return true;
}

/**
 * @brief   Stops RX/TX operations.
 */
void ioHdlcStream_stop(ioHdlcStream *d) {
  if (!d || !d->started) return;
  if (d->port.ops && d->port.ops->rx_cancel) d->port.ops->rx_cancel(d->port.ctx);
  if (d->port.ops && d->port.ops->stop)      d->port.ops->stop(d->port.ctx);
  d->started = false;
}

/**
 * @brief   Submits a TX buffer to the adapter.
 */
bool ioHdlcStream_send(ioHdlcStream *d, const uint8_t *ptr, size_t len, void *cookie) {
  if (!d || !d->port.ops || !d->port.ops->tx_submit || !ptr || len == 0)
    return false;
  return d->port.ops->tx_submit(d->port.ctx, ptr, len, cookie);
}

/*===========================================================================*/
/* Local functions.                                                          */
/*===========================================================================*/

/**
 * @brief   Arms for the next RX chunk at current write offset.
 */
static void s_arm_idle(ioHdlcStream *d) {
  (void)d->port.ops->rx_submit(d->port.ctx, &d->rx_flagoctet, 1);
}

static void s_arm_inframe_from(ioHdlcStream *d, size_t offset, size_t need) {
  uint8_t *ptr = &d->rx_in_frame->frame[offset];
  (void)d->port.ops->rx_submit(d->port.ctx, ptr, need);
}

/**
 * @brief   Delivers the completed frame and arms the next header.
 */
static void s_deliver_and_continue(ioHdlcStream *d) {
  size_t len = d->rx_in_frame ? d->rx_in_frame->elen : 0;
  d->cfg.deliver_rx_frame(d->upper_ctx, (void *)d->rx_in_frame, len);
  /* Reset for next frame. */
  d->rx_in_frame = NULL;
  s_arm_idle(d);
}

/**
 * @brief   Handles an RX error by discarding the current frame and rearming.
 */
static void s_handle_rx_error(ioHdlcStream *d) {
  if (d->rx_in_frame) hdlcReleaseFrame(d->pool, d->rx_in_frame);
  d->rx_in_frame = NULL;
  s_arm_idle(d);
}

/**
 * @brief   HAL callback: end of an RX chunk (or timeout).
 */
static void s_hal_on_rx(void *cb_ctx, bool timed_out) {
  ioHdlcStream *d = (ioHdlcStream *)cb_ctx;
  if (!d || !d->started) return;

  if (timed_out) {
    /* Intra-frame timeout -> discard current frame; inter-frame -> ignore. */
    if (d->rx_in_frame && d->rx_in_frame->elen != 0) {
      s_handle_rx_error(d);
      return;
    }
    /* Inter-frame idle: just keep searching for FLAG. */
    s_arm_idle(d);
    return;
  }

  /* Multi-byte burst complete (FFF optimization):
     - The N from FFF does not include FLAGs; original driver includes the
       closing FLAG within the armed burst so that the same FLAG acts as
       separator/opening for the next frame.
     - Here we verify the last received byte (at frame[elen]) is a FLAG,
       then deliver the frame and immediately arm the next frame start. */
  /* No special branch: closing FLAG is handled in general path below. */

  uint8_t b;
  if (!d->rx_in_frame) {
    b = d->rx_flagoctet;
    if (b == HDLC_FLAG) {
      /* Start of a new frame: allocate buffer lazily. */
      iohdlc_frame_t *fp = hdlcTakeFrame(d->pool);
      if (!fp) {
        /* No buffer available, stay idle and keep scanning. */
        d->rx_in_frame = NULL;
        s_arm_idle(d);
        return;
      }
      d->rx_in_frame = fp;
      d->rx_in_frame->elen = 0;
      s_arm_inframe_from(d, d->rx_in_frame->elen, 1);
      return;
    }
    s_arm_idle(d);
    return;
  }
  /* In-frame: byte has been written into frame buffer. */
  b = d->rx_in_frame->frame[d->rx_in_frame->elen];

  /* In frame: handle closing flag. */
  if (b == HDLC_FLAG) {
    if (d->rx_in_frame->elen == 0) {
      /* Empty frame, ignore and keep searching. */
      d->rx_in_frame = NULL;
      s_arm_idle(d);
      return;
    }
    /* Basic sanity on minimum length. */
    if (d->rx_in_frame->elen < HDLC_BASIC_MIN_L) {
      /* Not enough bytes, drop and restart new frame search. */
      d->rx_in_frame->elen = 0;
      d->rx_in_frame = NULL;
      s_arm_idle(d);
      return;
    }
    /* Frame complete: deliver to upper layer. */
    s_deliver_and_continue(d);
    return;
  }

  /* Regular byte: store into frame if capacity allows. */
  if (!d->rx_in_frame || d->rx_in_frame->elen >= d->pool->framesize) {
    s_handle_rx_error(d);
    return;
  }
  d->rx_in_frame->elen++;

  /* Optional: length-prefixed handling (FFF) with burst arming. */
  if (d->rx_in_frame->elen == 1 && d->cfg.has_frame_format && !d->cfg.apply_transparency) {
    if ((d->rx_in_frame->frame[0] & 0x80) == 0) {
      size_t n = d->rx_in_frame->frame[0];
      if (n > d->pool->framesize - 1) {
        s_handle_rx_error(d);
        return;
      }
      d->rx_in_frame->elen = n; /* payload+FCS length; closing flag at frame[n] */
      s_arm_inframe_from(d, 1, n);
      return;
    } else {
      /* Bad format type, discard frame. */
      s_handle_rx_error(d);
      return;
    }
  }

  /* Continue reading next byte. */
  s_arm_inframe_from(d, d->rx_in_frame->elen, 1);
}

/**
 * @brief   HAL callback: TX buffer completed.
 */
static void s_hal_on_tx_done(void *cb_ctx, void *cookie) {
  ioHdlcStream *d = (ioHdlcStream *)cb_ctx;
  if (cookie) {
    /* Cookie is the frame pointer to be released back to the pool. */
    hdlcReleaseFrame(d->pool, (iohdlc_frame_t *)cookie);
  }
}

/**
 * @brief   HAL callback: RX error.
 */
static void s_hal_on_rx_error(void *cb_ctx, uint32_t errmask) {
  (void)errmask;
  ioHdlcStream *d = (ioHdlcStream *)cb_ctx;
  if (!d || !d->started) return;
  s_handle_rx_error(d);
}
