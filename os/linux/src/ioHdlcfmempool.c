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
#include "ioHdlcosal.h"
#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcfmempool.h"
#include <string.h>
#include <assert.h>
/**
 * HDLC frame pool implementation for Linux using free-list.
 */

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Take frame from pool (with locking).
 */
static iohdlc_frame_t * take(void *ip) {
  ioHdlcFrameMemPool *fmpp = (ioHdlcFrameMemPool *)ip;
  iohdlc_frame_t *fp = NULL;
  
  pthread_mutex_lock(&fmpp->mp.lock);
  
  /* Pop from free list */
  if (fmpp->mp.free_list != NULL) {
    fp = (iohdlc_frame_t *)fmpp->mp.free_list;
    fmpp->mp.free_list = *(void **)fmpp->mp.free_list;  /* Next in list */
    fp->refs = 1;
    fmpp->allocated++;
    
    /* Check low watermark with hysteresis */
    uint32_t free = fmpp->total - fmpp->allocated;
    if (fmpp->state == IOHDLC_POOL_NORMAL && free <= fmpp->low_threshold) {
      fmpp->state = IOHDLC_POOL_LOW_WATER;
      pthread_mutex_unlock(&fmpp->mp.lock);
      if (fmpp->on_low != NULL)
        fmpp->on_low(fmpp->cb_arg);
      return fp;
    }
  }
  
  pthread_mutex_unlock(&fmpp->mp.lock);
  return fp;
}

/**
 * @brief   Release frame back to pool (with locking).
 */
static void release(void *ip, iohdlc_frame_t *fp) {
  ioHdlcFrameMemPool *fmpp = (ioHdlcFrameMemPool *)ip;
  
  pthread_mutex_lock(&fmpp->mp.lock);
  
  assert(fp->refs > 0 && "frame ref count mismatch");
  
  if (--fp->refs == 0) {
    /* Push to free list */
    *(void **)fp = fmpp->mp.free_list;
    fmpp->mp.free_list = fp;
    fmpp->allocated--;
    
    /* Check high watermark with hysteresis */
    uint32_t free = fmpp->total - fmpp->allocated;
    if (fmpp->state == IOHDLC_POOL_LOW_WATER && free > fmpp->high_threshold) {
      fmpp->state = IOHDLC_POOL_NORMAL;
      pthread_mutex_unlock(&fmpp->mp.lock);
      if (fmpp->on_normal != NULL)
        fmpp->on_normal(fmpp->cb_arg);
      return;
    }
  }
  
  pthread_mutex_unlock(&fmpp->mp.lock);
}

/**
 * @brief   Add reference to frame (with locking).
 */
static void addref(iohdlc_frame_t *fp) {
  /* Note: In real implementation should have per-frame lock or atomic ops.
   * For simplicity using assert - caller must ensure no concurrent addref/release */
  assert(fp->refs > 0 && "adding ref to freed frame");
  __sync_fetch_and_add(&fp->refs, 1);  /* Atomic increment */
}

static const struct _iohdlc_fmempool_vmt vmt = {
    .take = take,
    .release = release,
    .addref = addref
};

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Initialize HDLC frame memory pool.
 *
 * @param[in] fmpp        Frame pool object
 * @param[in] arena       Memory arena for frames
 * @param[in] arenasize   Size of arena in bytes
 * @param[in] framesize   Size of each frame payload
 * @param[in] framealign  Alignment requirement (power of 2)
 */
void fmpInit(ioHdlcFrameMemPool *fmpp, uint8_t *arena, size_t arenasize,
              size_t framesize, uint32_t framealign) {
  uint32_t n, es;
  uint8_t *p;

  assert((framealign & (framealign-1)) == 0 && "framealign must be a power of 2");

  framesize = framesize * 2 + sizeof(iohdlc_frame_t);  /* Account for worst-case transparency */
  
  /* Align the arena and adjust its size */
  p = (uint8_t *)(((uintptr_t)arena + framealign - 1) & ~(uintptr_t)(framealign - 1));
  arenasize = (size_t)((arena + arenasize) - p);

  /* Compute the size of aligned frame and the number of frames in pool */
  es = (framesize + framealign - 1) & ~(framealign - 1);
  n = arenasize / es;

  /* Initialize mutex */
  pthread_mutex_init(&fmpp->mp.lock, NULL);
  
  /* Build free list */
  fmpp->mp.free_list = NULL;
  fmpp->mp.element_size = es;
  
  for (uint32_t i = 0; i < n; i++) {
    void *elem = p + (i * es);
    *(void **)elem = fmpp->mp.free_list;
    fmpp->mp.free_list = elem;
  }

  /* Initialize base class */
  fmpp->vmt = &vmt;
  fmpp->framesize = framesize;
  fmpp->total = n;
  fmpp->allocated = 0;
  
  /* Initialize watermark with sensible defaults */
  fmpp->low_pct = 20;   /* Enter LOW_WATER at 20% free */
  fmpp->high_pct = 60;  /* Exit LOW_WATER at 60% free */
  fmpp->low_threshold = (n * 20) / 100;
  fmpp->high_threshold = (n * 60) / 100;
  fmpp->state = IOHDLC_POOL_NORMAL;
  fmpp->on_low = NULL;     /* No callback by default */
  fmpp->on_normal = NULL;
  fmpp->cb_arg = NULL;
}

/**
 * @brief   Configure watermark thresholds and callbacks.
 */
void hdlcPoolConfigWatermark(ioHdlcFramePool *fpp, uint8_t low_pct, 
                             uint8_t high_pct, void (*on_low)(void *),
                             void (*on_normal)(void *), void *cb_arg) {
  assert(fpp != NULL && "pool is NULL");
  assert(low_pct <= 100 && high_pct <= 100 && "invalid percentage");
  assert(low_pct < high_pct && "low must be < high for hysteresis");
  
  ioHdlcFrameMemPool *fmpp = (ioHdlcFrameMemPool *)fpp;
  pthread_mutex_lock(&fmpp->mp.lock);
  
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
  iohdlc_pool_state_t old_state = fpp->state;
  
  if (free <= fpp->low_threshold) {
    fpp->state = IOHDLC_POOL_LOW_WATER;
  } else {
    fpp->state = IOHDLC_POOL_NORMAL;
  }
  
  pthread_mutex_unlock(&fmpp->mp.lock);
  
  /* Call callbacks if state changed */
  if (old_state != fpp->state) {
    if (fpp->state == IOHDLC_POOL_LOW_WATER && on_low != NULL) {
      on_low(cb_arg);
    } else if (fpp->state == IOHDLC_POOL_NORMAL && on_normal != NULL) {
      on_normal(cb_arg);
    }
  }
}
