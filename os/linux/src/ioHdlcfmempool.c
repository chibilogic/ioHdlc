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
 * @file    os/linux/src/ioHdlcfmempool.c
 * @brief   Linux fixed-memory frame pool implementation.
 * @details Implements @ref ioHdlcFrameMemPool using a mutex-protected free
 *          list over a caller-provided arena.
 *
 *          The backend keeps allocation deterministic while delegating common
 *          watermark policy to the shared pool helpers. Callback invocation is
 *          intentionally performed after releasing the internal mutex.
 *
 * @addtogroup ioHdlc_backends
 * @{
 */

#include "ioHdlcosal.h"
#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcfmempool.h"
#include "ioHdlcpool_common.h"
#include <string.h>
#include <assert.h>
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
    fp->q_aux.next = NULL;
    fp->q_aux.prev = NULL;
    fp->tx_snapshot.addr = 0;
    memset(fp->tx_snapshot.ctrl, 0, sizeof fp->tx_snapshot.ctrl);
    fp->tx_snapshot.lens = 0;
    fp->openingflag = 0;
    fmpp->allocated++;
    
    /* Check low watermark with hysteresis (common logic) */
    bool notify;
    void *cb_arg;
    void (*cb)(void *) = hdlc_pool_check_low_watermark((ioHdlcFramePool *)fmpp, &notify, &cb_arg);
    
    pthread_mutex_unlock(&fmpp->mp.lock);
    
    if (notify && cb != NULL)
      cb(cb_arg);
      
    return fp;
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
    
    /* Check high watermark with hysteresis (common logic) */
    bool notify;
    void *cb_arg;
    void (*cb)(void *) = hdlc_pool_check_high_watermark((ioHdlcFramePool *)fmpp, &notify, &cb_arg);
    
    pthread_mutex_unlock(&fmpp->mp.lock);
    
    if (notify && cb != NULL)
      cb(cb_arg);
      
    return;
  }
  
  pthread_mutex_unlock(&fmpp->mp.lock);
}

/**
 * @brief   Add reference to frame (with locking).
 * @note    This path uses an atomic increment; the backend still expects the
 *          wider ownership model to prevent invalid lifetime races.
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
 * @brief   Initialize a fixed-memory frame pool.
 * @details Binds the pool to a caller-owned memory arena and prepares the
 *          internal allocator state.
 * @note    The arena must remain valid for the full lifetime of @p fmpp.
 *
 * @param[in] fmpp        Frame memory pool instance
 * @param[in] arena       Pointer to caller-owned memory arena
 * @param[in] arenasize   Size of arena in bytes
 * @param[in] framesize   Size of each frame payload
 * @param[in] framealign  Alignment requirement (power of 2)
 */
void fmpInit(ioHdlcFrameMemPool *fmpp, uint8_t *arena, size_t arenasize,
              size_t framesize, uint32_t framealign) {
  uint32_t n, es;
  uint8_t *p;
  iohdlc_frame_t *framep = NULL;

  assert((framealign & (framealign-1)) == 0 && "framealign must be a power of 2");

  fmpp->framesize = framesize;
  framesize = framesize + sizeof *framep;
  
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
  
  for (uint32_t i = 0; i < n; i++) {
    void *elem = p + (i * es);
    *(void **)elem = fmpp->mp.free_list;
    fmpp->mp.free_list = elem;
  }

  /* Initialize base class */
  fmpp->vmt = &vmt;
  fmpp->total = n;
  fmpp->allocated = 0;
  
  /* Initialize watermark with sensible defaults (common logic) */
  hdlc_pool_init_watermark((ioHdlcFramePool *)fmpp, n);
}

/**
 * @brief   Configure watermark thresholds and callbacks.
 * @details Recomputes hysteresis thresholds and applies any resulting state
 *          transition after releasing the internal pool lock.
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

/** @} */
