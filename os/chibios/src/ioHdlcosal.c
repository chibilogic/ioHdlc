/*
 * ioHdlc OS Abstraction for ChibiOS: DMA-safe allocation helpers.
 * Default implementation uses a local ChibiOS heap carved from a
 * static buffer. The allocator honors alignment and supports free.
 */

#include "ch.h"
#include "hal.h"
#include "chmemheaps.h"

#include "ioHdlcosal.h"

#ifndef IOHDLC_DMA_HEAP_SIZE
#define IOHDLC_DMA_HEAP_SIZE 1024u
#endif

#if !defined(NO_CACHE)
/* Assume the existence of a no cached section named ".nocache".*/
#define NO_CACHE  __attribute__((section (".nocache")))
#endif

static memory_heap_t s_dma_heap;
static bool s_dma_heap_inited = false;
static NO_CACHE CH_HEAP_AREA(s_dma_heap_area, IOHDLC_DMA_HEAP_SIZE);

static void s_dma_heap_init_once(void) {
  if (!s_dma_heap_inited) {
    chHeapObjectInit(&s_dma_heap, s_dma_heap_area, sizeof(s_dma_heap_area));
    s_dma_heap_inited = true;
  }
}

void *iohdlc_dma_alloc(size_t size, size_t align) {
  s_dma_heap_init_once();
  return chHeapAllocAligned(&s_dma_heap, size, (unsigned)align);
}

void iohdlc_dma_free(void *p) {
  if (p) chHeapFree(p);
}

