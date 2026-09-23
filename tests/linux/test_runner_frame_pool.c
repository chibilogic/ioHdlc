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
#include <stdio.h>
#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"
#include "ioHdlcframe.h"
#include "ioHdlcfmempool.h"

static struct {
  ioHdlcFrameMemPool pool;
  iohdlc_frame_t *frame;
  uint8_t arena[4096];
} concurrent_pool;

static void *ref_worker(void *arg) {
  ioHdlcFramePool *pool = arg;
  iohdlc_frame_t *frame = concurrent_pool.frame;

  for (uint32_t i = 0; i < 100000; ++i) {
    hdlcAddRef(pool, frame);
    hdlcReleaseFrame(pool, frame);
  }
  hdlcReleaseFrame(pool, frame);
  return NULL;
}

static int test_concurrent_addref_release(void) {
  ioHdlcFramePool *pool = (ioHdlcFramePool *)&concurrent_pool.pool;
  pthread_t threads[4];
  uint32_t started = 0;
  bool joined = true;

  fmpInit(&concurrent_pool.pool, concurrent_pool.arena,
          sizeof concurrent_pool.arena, 128, 8);
  concurrent_pool.frame = hdlcTakeFrame(pool);
  TEST_ASSERT(concurrent_pool.frame != NULL, "Should allocate shared frame");

  /* Keep the original reference until every worker has released its own. */
  for (; started < sizeof threads / sizeof threads[0]; ++started) {
    hdlcAddRef(pool, concurrent_pool.frame);
    if (pthread_create(&threads[started], NULL, ref_worker, pool) != 0) {
      hdlcReleaseFrame(pool, concurrent_pool.frame);
      break;
    }
  }
  for (uint32_t i = 0; i < started; ++i)
    if (pthread_join(threads[i], NULL) != 0)
      joined = false;

  TEST_ASSERT(joined, "Should join every reference worker");
  TEST_ASSERT(concurrent_pool.frame->refs == 1,
              "Only the original reference should remain");
  TEST_ASSERT(pool->allocated == 1, "Shared frame must remain allocated");
  hdlcReleaseFrame(pool, concurrent_pool.frame);
  TEST_ASSERT(pool->allocated == 0, "Last release should recycle shared frame");

  iohdlc_frame_t *frame = hdlcTakeFrame(pool);
  TEST_ASSERT(frame == concurrent_pool.frame, "Should reuse the recycled frame");
  TEST_ASSERT(frame->refs == 1, "Recycled frame should have one reference");
  hdlcReleaseFrame(pool, frame);
  TEST_ASSERT(started == sizeof threads / sizeof threads[0],
              "Should start every reference worker");
  return 0;
}

/*===========================================================================*/
/* Main Test Runner                                                          */
/*===========================================================================*/
int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - Frame Pool Tests\n");
  test_printf("═══════════════════════════════════════════════\n\n");
  RUN_TEST(test_pool_init);
  RUN_TEST(test_take_release);
  RUN_TEST(test_addref);
  RUN_TEST(test_watermark);
  RUN_TEST(test_exhaust_pool);
  RUN_TEST(test_concurrent_addref_release);
  TEST_SUMMARY();
  return (failed_count == 0) ? 0 : 1;
}
