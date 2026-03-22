/*
 * heap_shim.cpp – Game heap allocator mapped to system malloc/free.
 *
 * Replaces src_pseudo/memcheck.cpp and src_pseudo/heap.cpp entirely.
 * The game uses a custom slab allocator (heapN64.h / HeapAlloc / HeapFree)
 * originally backed by a fixed-size N64 RDRAM region.  On Linux we simply
 * forward to the system heap.
 */

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

#include "../../include/memcheck.h"
#include "../../include/heapN64.h"

/* =========================================================================
 * Globals that memcheck.cpp and heap.cpp normally define
 * ========================================================================= */
MemCheck_struct gMemCheckStruct = {};
u16 gExpPakFlag = 0;
MemMon_struct gMemMonitor = {};

/* =========================================================================
 * MemoryCheck – called by seed.cpp to detect RDRAM size and set up
 * framebuffer/heap pointers in gMemCheckStruct.
 *
 * We populate the struct with Linux-friendly values so the rest of the
 * initialisation sequence proceeds normally.
 * ========================================================================= */

/* Framebuffer dimensions (matching SCREEN_WIDTH/HEIGHT in graphics.h) */
static constexpr int FB_W  = 320;
static constexpr int FB_H  = 240;
static constexpr int FB_BPP= 2; /* 16-bit colour */
static constexpr size_t FB_SIZE = FB_W * FB_H * FB_BPP;

/* Static buffers (N64 used RDRAM directly; on Linux we just malloc them) */
static u8  *sHeapBuf   = nullptr;
static u16 *sDepthBuf  = nullptr;
static u8  *sFB[2]     = {};

static constexpr size_t HEAP_SIZE = 8 * 1024 * 1024; /* 8 MB – more than enough */

void MemoryCheck(uintptr_t ramstart, uintptr_t size) {
    (void)ramstart; (void)size;

    /* Allocate backing storage */
    sDepthBuf = (u16 *)calloc(FB_W * FB_H, sizeof(u16));
    sFB[0]    = (u8  *)calloc(FB_SIZE, 1);
    sFB[1]    = (u8  *)calloc(FB_SIZE, 1);
    sHeapBuf  = (u8  *)malloc(HEAP_SIZE);

    if (!sDepthBuf || !sFB[0] || !sFB[1] || !sHeapBuf) {
        fprintf(stderr, "[heap] MemoryCheck: allocation failed\n");
        exit(1);
    }

    /* Populate the struct that the rest of the game reads */
    gMemCheckStruct.ramstartVal       = 0;
    gMemCheckStruct.DepthBuffer       = sDepthBuf;
    gMemCheckStruct.heapStart         = sHeapBuf;
    gMemCheckStruct.frameBuffers[0]   = sFB[0];
    gMemCheckStruct.frameBuffers[1]   = sFB[1];
    gMemCheckStruct.RamSize           = 8 * 1024 * 1024; /* simulate Expansion Pak */
    gMemCheckStruct.ramVal0           = 0;
    gMemCheckStruct.frameBufferSize0  = (u32)FB_SIZE;
    gMemCheckStruct.mem_free_allocated= (u32)HEAP_SIZE;
    gMemCheckStruct.frameBufferSize1  = (u32)FB_SIZE;

    gExpPakFlag = 1; /* expansion pak "detected" */

    fprintf(stderr, "[heap] MemoryCheck complete: heap=%p (%zu MB)\n",
            sHeapBuf, HEAP_SIZE / (1024*1024));
}

/* =========================================================================
 * HeapInit / HeapAlloc / HeapFree
 *
 * The game calls HeapInit once then allocates with HALLOC / ALLOCS macros.
 * We forward everything to malloc/free so the game's allocator logic still
 * runs but uses real system memory.
 * ========================================================================= */

static constexpr size_t HB_SZ = sizeof(HeapBlock);

void HeapInit(void *start, size_t size) {
    /* On Linux the heap is already system-managed; just note the intent. */
    (void)start; (void)size;
}

void *HeapAlloc(size_t size, char *file, u32 line) {
    (void)file; (void)line;
    if (size == 0) size = 1;
    HeapBlock *hb = (HeapBlock *)malloc(HB_SZ + size);
    if (!hb) {
        fprintf(stderr, "[heap] HeapAlloc(%zu) failed\n", size);
        return nullptr;
    }
    hb->size = (u32)size;
#if DEBUGVER
    if (file) strncpy(hb->filename, file, 23);
    else      memset(hb->filename, 0, 24);
#endif
    return (u8 *)hb + HB_SZ;
}

void HeapFree(void *ptr, char *file, u32 line) {
    (void)file; (void)line;
    if (!ptr) return;
    HeapBlock *hb = (HeapBlock *)((u8 *)ptr - HB_SZ);
    free(hb);
}

/* =========================================================================
 * MemMon stubs (the game reads memory statistics for the debug display)
 * ========================================================================= */
u32 get_MemFree(void)          { return (u32)HEAP_SIZE; }
u32 get_memFree_2(void)        { return (u32)HEAP_SIZE; }
u32 get_memUsed(void)          { return 0; }
u32 get_obj_free(void)         { return 256; }
u32 Ofunc_get_MemFreeMax(void) { return (u32)HEAP_SIZE; }
u32 Ofunc_get_objCount(void)   { return 0; }
u32 Ofunc_get_obj_count_2(void){ return 0; }
u32 Ofunc_80098200(void *p)    { (void)p; return 0; }
void malloc_update_mem_mon(HeapBlock *h, u32 p2) { (void)h; (void)p2; }
void print_mem_allocated(memPrint *func_, u16 *p) { (void)func_; (void)p; }

/* =========================================================================
 * Global operator new / delete
 *
 * Signatures must match heapN64.h (no noexcept on delete – legacy header).
 * The sized-delete and array overloads are extras not in the header.
 * ========================================================================= */
void *operator new(size_t size) {
    return HeapAlloc(size, nullptr, 0);
}

void *operator new[](size_t size) {
    return HeapAlloc(size, nullptr, 0);
}

void operator delete(void *ptr) {
    HeapFree(ptr, nullptr, 0);
}

void operator delete[](void *ptr) {
    HeapFree(ptr, nullptr, 0);
}

void operator delete(void *ptr, size_t) {
    HeapFree(ptr, nullptr, 0);
}

void operator delete[](void *ptr, size_t) {
    HeapFree(ptr, nullptr, 0);
}
