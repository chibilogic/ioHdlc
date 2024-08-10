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
#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcfmempool.h"

/**
 * HDLC frame pool implementation using ChibiOS mempools.
 */

/*===========================================================================*/
/* Module local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

static syssts_t _chSysGetStatusAndLockX(void) {

  syssts_t sts = port_get_irq_status();
  if (port_is_isr_context()) {
    chSysLockFromISR();
  } else {
    chSysLock();
  }
  return sts;
}

static void _chSysRestoreStatusX(syssts_t sts) {
  (void )sts;
  if (port_is_isr_context()) {
    chSysUnlockFromISR();
  } else {
    chSchRescheduleS();
    chSysUnlock();
  }
}

/*
 *  ioHdlcFramePool interface implementation.
 */

static iohdlc_frame_t * take(void *ip) {
  syssts_t sts = _chSysGetStatusAndLockX();
  iohdlc_frame_t *fp = chPoolAllocI(&((ioHdlcFrameMemPool *)ip)->mp);
  fp->refs = 1;
  _chSysRestoreStatusX(sts);
  return fp;
}

static void release(void *ip, iohdlc_frame_t *fp) {
  syssts_t sts = _chSysGetStatusAndLockX();
  chDbgAssert(fp->refs > 0, "frame ref count mismatch");
  if (--fp->refs == 0) {
    chPoolFreeI(&((ioHdlcFrameMemPool *)ip)->mp, fp);
  }
  _chSysRestoreStatusX(sts);
}

static void addref(iohdlc_frame_t *fp) {
  syssts_t sts = _chSysGetStatusAndLockX();
  ++fp->refs;
  _chSysRestoreStatusX(sts);
}

static const struct _iohdlc_fmempool_vmt vmt = {
    .take = take,
    .release = release,
    .addref = addref
};

void fmpInit(ioHdlcFrameMemPool *fmpp, uint8_t *arena, size_t arenasize,
              size_t framesize, uint32_t framealign) {
  uint32_t n, es;
  uint8_t *p;

  chDbgAssert((framealign & (framealign-1)) == 0, "framealign must be a power of 2");

  framesize = framesize * 2 + sizeof (iohdlc_frame_t);
  /* Align the arena and adjust its size.*/
  p = (uint8_t *)((uint32_t)(arena + framealign - 1) & ~(framealign - 1));
  arenasize = arena + arenasize - p;

  /* Compute the size of aligned frame and the number of frames
     in the pool, n.*/
  es = (framesize + framealign - 1) & ~(framealign - 1);
  n = arenasize / es;

  /* Initialize and load the pool.*/
  chPoolObjectInit(&fmpp->mp, es, NULL);
  chPoolLoadArray(&fmpp->mp, p, n);

  fmpp->vmt = &vmt;
  ((ioHdlcFramePool *)fmpp)->framesize = framesize;
}
