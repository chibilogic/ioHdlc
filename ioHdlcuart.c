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

#include "ch.h"

#include "hal.h"
#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcqueue.h"
#include "ioHdlcdriver.h"
#include "ioHdlcuart.h"
#include "ioHdlcll.h"

/*
 * HDLC driver implementation using the ChibiOS UART driver.
 */

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * End of frame transmission.
 */
static void txend(UARTDriver *uartp)
{
  ioHdclUartDriver *ip = (ioHdclUartDriver *)(uartp->ip);
  if (ip->frameintx) {
    hdlcReleaseFrame(ip->fpp, ip->frameintx);
  }
  ip->frameintx = NULL;
  chSysLockFromISR();
  chBSemSignalI(&ip->tx_on_air);
  chSysUnlockFromISR();
}

/**
 * Receiver error callback.
 */
static void rxerr(UARTDriver *uartp, uartflags_t e)
{
  (void)e;
  ioHdclUartDriver *ip = (ioHdclUartDriver *)(uartp->ip);
  ip->flags |= HDLC_UART_ERROR;
}

/**
 * Receiver timeout callback.
 */
static void timeout(UARTDriver *uartp)
{
  ioHdclUartDriver *ip = (ioHdclUartDriver *)(uartp->ip);
  if (ip->frameinrx != NULL) {

    /* A timeout occured while receiving a frame, alias
       it is a intra-frame timeout.
       Discard the current frame and start receiving a
       new frame. */
    hdlcReleaseFrame(ip->fpp, ip->frameinrx);
    ip->frameinrx = NULL;
    ip->flags &= ~HDLC_UART_ERROR;
    chSysLockFromISR();
    uartStopReceiveI(uartp);
    uartStartReceiveI(uartp, 1, &ip->flagoctet);
    chSysUnlockFromISR();
  }
}

/**
 * End of character/frame reception.
 */
static void rxend(UARTDriver *uartp)
{
  ioHdclUartDriver *ip = (ioHdclUartDriver *)(uartp->ip);
  uint8_t *charbuf;
  size_t n = 1;

  if (NULL != ip->frameinrx) {

    /* It's in the state of receiving a started frame.*/
    charbuf = &ip->frameinrx->frame[ip->frameinrx->elen];
    if (*charbuf == HDLC_FLAG) {

      /* Found a FLAG octet, the frame separator.*/
      if (!ip->frameinrx->elen)
        goto nextoctet;   /* empty frame.*/

      /* If the length of the frame is incorrect,
         discard the received bad frame.*/
      if (ip->frameinrx->elen < HDLC_BASIC_MIN_L) {
        ip->frameinrx->elen = 0;            /* we not need to return the frame
                                               buffer to the pool because the
                                               current octet is the FLAG.*/
        charbuf = &ip->frameinrx->frame[0];
        goto nextoctet;
      }

      /* The raw frame is ready.
         Put the frame in the receiving queue and start
         the reception of a new frame.*/
      ioHdlc_frameq_insert(&ip->raw_recept_q, ip->frameinrx);
      chSysLockFromISR();
      chSemSignalI(&ip->raw_recept_sem);  /* one frame more.*/
      chSysUnlockFromISR();
      ip->flagoctet = HDLC_FLAG;  /* the FLAG separator is also a start FLAG.*/
      ip->frameinrx = NULL;
    } else {

      if (ip->flags & HDLC_UART_ERROR) {
        /* Bad frame. An uart error occured.
           Discard the frame, returning the buffer to the pool.*/
        hdlcReleaseFrame(ip->fpp, ip->frameinrx);
        ip->frameinrx = NULL;
        goto newframe;
      }
      /* The first octet of the frame could be the frame format field
         if configured for this.*/
      if ((ip->frameinrx->elen == 0) &&
          (ip->flags & HDLC_UART_HASFF) && !(ip->flags & HDLC_UART_TRANS)) {

        /* in this case, use the frame length in the frame format field.*/
        if (!(ip->frameinrx->frame[0] & 0x80)) {
          n = (size_t)ip->frameinrx->frame[0];
          ip->frameinrx->elen = n;

          /* include closing FLAG in the count of number of octets
             to receive, so doesn't decrement n.*/
          charbuf = &ip->frameinrx->frame[1];
        } else {
          /* Bad Format Type in frame format field.
             discard the frame, returning the buffer to the pool.*/
          hdlcReleaseFrame(ip->fpp, ip->frameinrx);
          ip->frameinrx = NULL;
        }
      } else {
        /* continue to receive octets.*/
        ++charbuf;
        if (++ip->frameinrx->elen >= ip->fpp->framesize) {
          /* Bad frame. The number of octets exceeds the frame size.
             Discard the frame, returning the buffer to the pool.*/
          hdlcReleaseFrame(ip->fpp, ip->frameinrx);
          ip->frameinrx = NULL;
        }
      }
    }
  }

newframe:
  if (NULL == ip->frameinrx) {

    /* Start the reception of a new frame searching for FLAG octet,
       the start of the frame.*/
    charbuf = &ip->flagoctet;
    if (*charbuf != HDLC_FLAG)
      goto nextoctet;

    /* Found the start of new frame, allocate a
       receiving frame buffer.*/
    ip->flagoctet = 0;
    ip->frameinrx = hdlcTakeFrame(ip->fpp);

    /* If no more free frame buffer are available, skip the
       receiving frame.*/
    if (NULL == ip->frameinrx)
      goto nextoctet;
    ip->frameinrx->elen = 0;
    charbuf = &ip->frameinrx->frame[0];
  }

nextoctet:
  ip->flags &= ~HDLC_UART_ERROR;
  chSysLockFromISR();
  uartStartReceiveI(uartp, n, charbuf);
  chSysUnlockFromISR();
}

static size_t send_frame(void *instance, iohdlc_frame_t *fp) {
  ioHdclUartDriver *ip = (ioHdclUartDriver *)instance;
  iohdlc_frame_t *nfp = fp;
  size_t size = 0;

  /* The UART hardware doesn't help us. Add FCS at the end of a
     non empty frame.*/
  if (fp->elen) {
    frameAddFCS(fp);
    size = nfp->elen;
    if (ip->flags & HDLC_UART_TRANS) {

      /* Apply octet transparency to the sending frame.
         We need to expand the frame content, so allocate
         a new free frame.*/
      nfp = hdlcTakeFrame(ip->fpp);
      if (NULL == nfp)
        return 1; /* NOMEM */

      /* Encode the given frame into the nfp frame.*/
      (void) frameTransparentEncode(nfp, fp);
      size = nfp->elen;
    } else {
      hdlcAddRef(ip->fpp, nfp);
    }
  }
  nfp->frame[size] = HDLC_FLAG; /* add closing FLAG. */
  nfp->openingflag = 0;
  chSysLock();
  if (chBSemGetStateI(&ip->tx_on_air) == false) {
    /* No other frame is in transmission, so
       add an opening flag to the frame. */
    nfp->openingflag = HDLC_FLAG;
    ++size;
  }
  chBSemWaitTimeoutS(&ip->tx_on_air, TIME_INFINITE);
  chSysUnlock();
  ip->frameintx = nfp;
  uartStartSend(ip->uartp, size + 1, nfp->openingflag ?
      &nfp->openingflag : nfp->frame);
  return 0;
}

static iohdlc_frame_t * recv_frame(void *instance, iohdlc_timeout_t tmo) {
  ioHdclUartDriver *ip = (ioHdclUartDriver *)instance;
  iohdlc_frame_t *fp;

  while (true) {
    fp = NULL;
    if (chSemWaitTimeout(&ip->raw_recept_sem, TIME_MS2I(tmo)) != MSG_TIMEOUT) {
      chSysLock();
      fp = ioHdlc_frameq_remove(&ip->raw_recept_q);
      chSysUnlock();
    }
    if (NULL == fp)
      break;         /* Timeout, exit. */
    if (ip->flags & HDLC_UART_TRANS)
      frameTransparentDecode(fp, fp);

    if (frameCheckFCS(fp) &&
        (!(ip->flags & HDLC_UART_HASFF) || (fp->elen == fp->frame[0])))
      break;
    /* The FCS is incorrect or, in case the frame had the frame
       format field, the length in the format field does not match
       the calculated frame length, so discard the frame and repeat.*/
    hdlcReleaseFrame(ip->fpp, fp);
  };

  /* Adjust the @p elen field, discarding the count of the FCS octets. */
  fp->elen -= 2;
  return fp;
}

static bool get_hwtransparency(void *ip) {
  (void)ip;

  return false;
}

static void set_applytransparency(void *ip, bool tr) {
  ioHdclUartDriver *instance = (ioHdclUartDriver *)ip;
  instance->flags &= ~HDLC_UART_TRANS;
  if (tr)
    instance->flags |= HDLC_UART_TRANS;
}

static void set_hasframeformat(void *ip, bool hff) {
  ioHdclUartDriver *instance = (ioHdclUartDriver *)ip;
  instance->flags &= ~HDLC_UART_HASFF;
  if (hff)
    instance->flags |= HDLC_UART_HASFF;
}

static void start(void *ip, void *phyp, void *phyconfigp, ioHdlcFramePool *fpp) {
  ioHdclUartDriver *instance = (ioHdclUartDriver *)ip;
  UARTDriver *uartp = (UARTDriver *)phyp;
  UARTConfig *uartconfigp = (UARTConfig *)phyconfigp;

  instance->fpp = fpp;
  instance->uartp = uartp;
  uartp->ip = instance;
  uartconfigp->txend1_cb = txend;
  uartconfigp->rxend_cb = rxend;
  uartconfigp->rxerr_cb = rxerr;
  uartconfigp->timeout_cb = timeout;
  uartStart(uartp, uartconfigp);
  uartStartReceive(uartp, 1, &instance->flagoctet);
}

/*
 * ioHdclUartDriver Interface implementation.
 */
static const struct _iohdlc_uart_driver_vmt vmt = {
    .start = start,
    .send_frame = send_frame,
    .recv_frame = recv_frame,
    .set_applytransparency = set_applytransparency,
    .get_hwtransparency = get_hwtransparency,
    .set_hasframeformat = set_hasframeformat
};

void ioHdclUartDriverInit(ioHdclUartDriver *uhp) {
  uhp->vmt = &vmt;
  uhp->flags = 0;
  uhp->flagoctet = 0;
  uhp->frameinrx = uhp->frameintx = 0;
  ioHdlc_frameq_init(&uhp->raw_recept_q);
  chSemObjectInit(&uhp->raw_recept_sem, 0);
  chBSemObjectInit(&uhp->tx_on_air, false);
}
