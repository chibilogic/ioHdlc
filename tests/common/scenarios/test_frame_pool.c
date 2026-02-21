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
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcfmempool.h"
#include "test_helpers.h"
#include "test_arenas.h"
#include <string.h>
#define ARENA_SIZE 4096
#define FRAME_SIZE 128
static ioHdlcFrameMemPool frame_pool;
static int low_water_called = 0;
static int normal_called = 0;
static void on_low_water(void *arg) {
    (void)arg;
    low_water_called++;
}
static void on_normal_water(void *arg) {
    (void)arg;
    normal_called++;
}
/*===========================================================================*/
/* Test functions                                                            */
/*===========================================================================*/
int test_pool_init(void) {
    fmpInit(&frame_pool, shared_arena_single, ARENA_SIZE, FRAME_SIZE, 8);
    TEST_ASSERT(frame_pool.total > 0, "Pool should have frames");
    TEST_ASSERT(frame_pool.allocated == 0, "Pool should start empty");
    TEST_ASSERT(frame_pool.state == IOHDLC_POOL_NORMAL, "Pool should start in NORMAL state");
    return 0;
}
int test_take_release(void) {
    /* Take a frame */
    iohdlc_frame_t *f1 = hdlcTakeFrame((ioHdlcFramePool *)&frame_pool);
    TEST_ASSERT(f1 != NULL, "Should allocate frame");
    TEST_ASSERT(f1->refs == 1, "New frame should have refcount 1");
    TEST_ASSERT(frame_pool.allocated == 1, "Pool should track allocation");
    /* Take another */
    iohdlc_frame_t *f2 = hdlcTakeFrame((ioHdlcFramePool *)&frame_pool);
    TEST_ASSERT(f2 != NULL, "Should allocate second frame");
    TEST_ASSERT(f2 != f1, "Different frames");
    TEST_ASSERT(frame_pool.allocated == 2, "Pool should track 2 allocations");
    /* Release first */
    hdlcReleaseFrame((ioHdlcFramePool *)&frame_pool, f1);
    TEST_ASSERT(frame_pool.allocated == 1, "Pool should track release");
    /* Release second */
    hdlcReleaseFrame((ioHdlcFramePool *)&frame_pool, f2);
    TEST_ASSERT(frame_pool.allocated == 0, "Pool should be empty");
    return 0;
}
int test_addref(void) {
    /* Take frame */
    iohdlc_frame_t *f = hdlcTakeFrame((ioHdlcFramePool *)&frame_pool);
    TEST_ASSERT(f != NULL, "Should allocate frame");
    TEST_ASSERT(f->refs == 1, "New frame should have refcount 1");
    /* Add reference */
    hdlcAddRef((ioHdlcFramePool *)&frame_pool, f);
    TEST_ASSERT(f->refs == 2, "Refcount should be 2");
    TEST_ASSERT(frame_pool.allocated == 1, "Pool should still track 1 allocation");
    /* Release once (still allocated) */
    hdlcReleaseFrame((ioHdlcFramePool *)&frame_pool, f);
    TEST_ASSERT(f->refs == 1, "Refcount should be back to 1");
    TEST_ASSERT(frame_pool.allocated == 1, "Pool should still track allocation");
    /* Release again (freed) */
    hdlcReleaseFrame((ioHdlcFramePool *)&frame_pool, f);
    TEST_ASSERT(frame_pool.allocated == 0, "Pool should be empty");
    return 0;
}
int test_watermark(void) {
    low_water_called = 0;
    normal_called = 0;
    /* Configure watermark: LOW at 20% free, NORMAL at 60% free */
    hdlcPoolConfigWatermark((ioHdlcFramePool *)&frame_pool, 20, 60, 
                            on_low_water, on_normal_water, NULL);
    TEST_ASSERT(frame_pool.low_threshold > 0, "Low threshold configured");
    TEST_ASSERT(frame_pool.high_threshold > frame_pool.low_threshold, "High > Low threshold");
    /* Allocate until LOW_WATER */
    iohdlc_frame_t *frames[32];
    uint32_t n = 0;
    while (n < frame_pool.total && frame_pool.state == IOHDLC_POOL_NORMAL) {
        frames[n] = hdlcTakeFrame((ioHdlcFramePool *)&frame_pool);
        if (frames[n] != NULL) {
            n++;
        } else {
            break;
        }
    }
    TEST_ASSERT(frame_pool.state == IOHDLC_POOL_LOW_WATER, "Should enter LOW_WATER");
    TEST_ASSERT(low_water_called == 1, "Low water callback should fire");
    /* Release back to NORMAL: need free > high_threshold */
    /* allocated must be < (total - high_threshold) */
    uint32_t target_allocated = frame_pool.total - frame_pool.high_threshold - 1;
    uint32_t to_release = n - target_allocated;
    for (uint32_t i = 0; i < to_release; i++) {
        hdlcReleaseFrame((ioHdlcFramePool *)&frame_pool, frames[i]);
    }
    TEST_ASSERT(frame_pool.state == IOHDLC_POOL_NORMAL, "Should return to NORMAL");
    TEST_ASSERT(normal_called == 1, "Normal callback should fire");
    /* Release remaining */
    for (uint32_t i = to_release; i < n; i++) {
        hdlcReleaseFrame((ioHdlcFramePool *)&frame_pool, frames[i]);
    }
    TEST_ASSERT(frame_pool.allocated == 0, "Pool should be empty");
    return 0;
}
int test_exhaust_pool(void) {
    iohdlc_frame_t *frames[32];  /* Reduced - arena may not hold 64 */
    uint32_t n = 0;
    uint32_t max_frames = frame_pool.total < 32 ? frame_pool.total : 32;
    /* Allocate all frames */
    for (n = 0; n < max_frames; n++) {
        frames[n] = hdlcTakeFrame((ioHdlcFramePool *)&frame_pool);
        if (frames[n] == NULL) {
            break;  /* Pool exhausted */
        }
    }
    TEST_ASSERT(n == max_frames, "Should allocate expected frames");
    TEST_ASSERT(frame_pool.allocated == n, "Pool should track all allocations");
    /* Attempt to take one more (should fail) */
    iohdlc_frame_t *f = hdlcTakeFrame((ioHdlcFramePool *)&frame_pool);
    TEST_ASSERT(f == NULL, "Should fail when pool exhausted");
    /* Release all */
    for (uint32_t i = 0; i < n; i++) {
        hdlcReleaseFrame((ioHdlcFramePool *)&frame_pool, frames[i]);
    }
    TEST_ASSERT(frame_pool.allocated == 0, "Pool should be empty");
    return 0;
}
