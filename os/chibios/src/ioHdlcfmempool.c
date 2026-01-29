/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU Lesser General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
 */
#include "ch.h"
#include "ioHdlcosal.h"
#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcfmempool.h"
#include "ioHdlcpool_common.h"
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
  ioHdlcFrameMemPool *fmpp = (ioHdlcFrameMemPool *)ip;
  syssts_t sts = _chSysGetStatusAndLockX();
  iohdlc_frame_t *fp = chPoolAllocI(&fmpp->mp);
  if (fp != NULL) {
    fp->refs = 1;
    fmpp->allocated++;
    
    /* Check low watermark with hysteresis (common logic) */
    bool notify;
    void *cb_arg;
    void (*cb)(void *) = hdlc_pool_check_low_watermark((ioHdlcFramePool *)fmpp, &notify, &cb_arg);
    
    _chSysRestoreStatusX(sts);
    
    if (notify && cb != NULL)
      cb(cb_arg);
      
    return fp;
  }
  _chSysRestoreStatusX(sts);
  return fp;
}

static void release(void *ip, iohdlc_frame_t *fp) {
  ioHdlcFrameMemPool *fmpp = (ioHdlcFrameMemPool *)ip;
  syssts_t sts = _chSysGetStatusAndLockX();
  chDbgAssert(fp->refs > 0, "frame ref count mismatch");
  if (--fp->refs == 0) {
    chPoolFreeI(&fmpp->mp, fp);
    fmpp->allocated--;
    
    /* Check high watermark with hysteresis (common logic) */
    bool notify;
    void *cb_arg;
    void (*cb)(void *) = hdlc_pool_check_high_watermark((ioHdlcFramePool *)fmpp, &notify, &cb_arg);
    
    _chSysRestoreStatusX(sts);
    
    if (notify && cb != NULL)
      cb(cb_arg);
      
    return;
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

  ((ioHdlcFramePool *)fmpp)->framesize = framesize;
  framesize = framesize + sizeof (iohdlc_frame_t);
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
  ((ioHdlcFramePool *)fmpp)->total = n;
  ((ioHdlcFramePool *)fmpp)->allocated = 0;
  
  /* Initialize watermark with sensible defaults (common logic) */
  hdlc_pool_init_watermark((ioHdlcFramePool *)fmpp, n);
}

void hdlcPoolConfigWatermark(ioHdlcFramePool *fpp, uint8_t low_pct, 
                             uint8_t high_pct, void (*on_low)(void *),
                             void (*on_normal)(void *), void *cb_arg) {
  chDbgAssert(fpp != NULL, "pool is NULL");
  chDbgAssert(low_pct <= 100 && high_pct <= 100, "invalid percentage");
  chDbgAssert(low_pct < high_pct, "low must be < high for hysteresis");
  
  /* Update configuration */
  fpp->low_pct = low_pct;
  fpp->high_pct = high_pct;
  fpp->on_low = on_low;
  fpp->on_normal = on_normal;
  fpp->cb_arg = cb_arg;
  
  /* Recalculate absolute thresholds */
  fpp->low_threshold = (fpp->total * low_pct) / 100;
  fpp->high_threshold = (fpp->total * high_pct) / 100;
  
  /* Re-evaluate state based on current free count */
  uint32_t free = fpp->total - fpp->allocated;
  if (free <= fpp->low_threshold) {
    fpp->state = IOHDLC_POOL_LOW_WATER;
    if (on_low != NULL)
      on_low(cb_arg);
  } else {
    fpp->state = IOHDLC_POOL_NORMAL;
  }
}
