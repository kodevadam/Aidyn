/*
 * heap_shim.cpp – Game heap backed by a low-address memory pool.
 *
 * Replaces src_pseudo/memcheck.cpp and src_pseudo/heap.cpp entirely.
 *
 * The original N64 code uses 32-bit pointer arithmetic everywhere:
 *   (void *)((s32)ptr + offset), (int)ptr, etc.
 * On 64-bit Linux, system malloc returns addresses above 0x7F0000000000
 * which are truncated by these casts, producing garbage pointers.
 *
 * Solution: allocate ALL game memory from a pool in the lower 2 GB of
 * virtual address space via mmap(MAP_32BIT).  This guarantees that every
 * pointer fits in 31 bits, so (s32)/(int)/(u32) casts are lossless.
 *
 * The pool uses a simple first-fit free-list allocator with coalescing.
 */

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>
#include <mutex>
#include <sys/mman.h>

#include "../../include/memcheck.h"
#include "../../include/heapN64.h"

/* =========================================================================
 * Globals that memcheck.cpp and heap.cpp normally define
 * ========================================================================= */
MemCheck_struct gMemCheckStruct = {};
u16 gExpPakFlag = 0;
MemMon_struct gMemMonitor = {};

/* =========================================================================
 * Low-address pool allocator
 *
 * We reserve a generous pool via mmap(MAP_32BIT) and manage it with a
 * simple first-fit free-list.  Freed blocks are coalesced with their
 * neighbours to reduce fragmentation.
 * ========================================================================= */

/* 128 MB pool – far more than the N64's 8 MB, but the game loads/frees
 * assets repeatedly so we need headroom for fragmentation waste. */
static constexpr size_t POOL_SIZE = 128 * 1024 * 1024;
static constexpr size_t ALIGN     = 16; /* allocation alignment */

/* Block header prepended to every allocation (free or in-use).
 * 'size' includes the header.  Low bit of size = 1 means in-use. */
struct PoolBlock {
    size_t size;       /* total block size incl. header; bit 0 = used flag */
    PoolBlock *next;   /* next in free list (only valid when free) */
};

static constexpr size_t PB_SZ = sizeof(PoolBlock);

static u8        *sPool     = nullptr;
static size_t     sPoolSize = 0;
static PoolBlock *sFreeList = nullptr;
static std::mutex sPoolMtx;  /* thread-safety: N64 was single-core */

static inline size_t blk_size(PoolBlock *b)  { return b->size & ~(size_t)1; }
static inline bool   blk_used(PoolBlock *b)  { return b->size & 1; }
static inline void   blk_mark_used(PoolBlock *b) { b->size |= 1; }
static inline void   blk_mark_free(PoolBlock *b) { b->size &= ~(size_t)1; }
static inline PoolBlock *blk_next_phys(PoolBlock *b) {
    return (PoolBlock *)((u8 *)b + blk_size(b));
}

static void pool_ensure_init(void);

static void pool_init(void *base, size_t len) {
    sPool     = (u8 *)base;
    sPoolSize = len;

    /* Entire pool is one big free block */
    sFreeList        = (PoolBlock *)base;
    sFreeList->size  = len;   /* free (bit 0 = 0) */
    sFreeList->next  = nullptr;
}

/* Lazy-init: global constructors may call operator new (and thus
 * pool_alloc) before MemoryCheck runs.  If the pool isn't set up yet,
 * create it now. */
static void pool_ensure_init(void) {
    if (sPool) return;
    void *pool = mmap(nullptr, POOL_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                       -1, 0);
    if (pool == MAP_FAILED) {
        fprintf(stderr, "[heap] mmap(MAP_32BIT, %zu MB) failed in early init\n",
                POOL_SIZE / (1024*1024));
        abort();
    }
    pool_init(pool, POOL_SIZE);
    fprintf(stderr, "[heap] Low-address pool (early init): %p .. %p (%zu MB)\n",
            pool, (u8 *)pool + POOL_SIZE, POOL_SIZE / (1024*1024));
}

static size_t align_up(size_t n) {
    return (n + ALIGN - 1) & ~(ALIGN - 1);
}

static void *pool_alloc(size_t user_size) {
    pool_ensure_init();
    std::lock_guard<std::mutex> lock(sPoolMtx);
    size_t need = align_up(user_size + PB_SZ);
    if (need < PB_SZ + ALIGN) need = PB_SZ + ALIGN; /* minimum block */

    /* First-fit search */
    PoolBlock **prev = &sFreeList;
    for (PoolBlock *b = sFreeList; b; prev = &b->next, b = b->next) {
        size_t bsz = blk_size(b);
        if (bsz < need) continue;

        /* Split if remainder is large enough for a new free block */
        if (bsz >= need + PB_SZ + ALIGN) {
            PoolBlock *rest = (PoolBlock *)((u8 *)b + need);
            rest->size = bsz - need;
            rest->next = b->next;
            *prev = rest;
            b->size = need;
        } else {
            /* Use entire block */
            *prev = b->next;
            need = bsz; /* use full size */
        }

        blk_mark_used(b);
        return (u8 *)b + PB_SZ;
    }

    /* Dump free-list stats to diagnose exhaustion */
    size_t total_free = 0, largest_free = 0;
    int free_count = 0;
    for (PoolBlock *f = sFreeList; f && free_count < 10000; f = f->next, free_count++) {
        size_t fsz = blk_size(f);
        total_free += fsz;
        if (fsz > largest_free) largest_free = fsz;
    }
    fprintf(stderr, "[heap] pool_alloc(%zu) FAILED – need=%zu  free_blocks=%d  total_free=%zu  largest=%zu\n",
            user_size, need, free_count, total_free, largest_free);
    return nullptr;
}

/* Remove 'blk' from free list by scanning for it.
 * Only needed during coalescing when a neighbour is already free. */
static void freelist_remove(PoolBlock *blk) {
    PoolBlock **prev = &sFreeList;
    for (PoolBlock *b = sFreeList; b; prev = &b->next, b = b->next) {
        if (b == blk) { *prev = b->next; return; }
    }
}

static void pool_free(void *ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(sPoolMtx);
    PoolBlock *b = (PoolBlock *)((u8 *)ptr - PB_SZ);
    blk_mark_free(b);

    /* Coalesce with next physical neighbour */
    PoolBlock *nxt = blk_next_phys(b);
    if ((u8 *)nxt < sPool + sPoolSize && !blk_used(nxt)) {
        freelist_remove(nxt);
        b->size += blk_size(nxt);
    }

    /* Coalesce with previous physical neighbour (scan for it) */
    if ((u8 *)b > sPool) {
        /* Walk from pool start to find block ending right before 'b'.
         * This is O(n) but acceptable for a game with few allocations. */
        PoolBlock *scan = (PoolBlock *)sPool;
        while (scan < b) {
            PoolBlock *snext = blk_next_phys(scan);
            if (snext == b && !blk_used(scan)) {
                freelist_remove(scan);
                scan->size += blk_size(b);
                b = scan; /* 'b' is now the merged block */
                break;
            }
            scan = snext;
        }
    }

    /* Insert at head of free list */
    b->next   = sFreeList;
    sFreeList = b;
}

/* =========================================================================
 * MemoryCheck
 * ========================================================================= */

static constexpr int FB_W   = 320;
static constexpr int FB_H   = 240;
static constexpr int FB_BPP = 4;
static constexpr size_t FB_SIZE = FB_W * FB_H * FB_BPP;

static constexpr size_t HEAP_SIZE = 8 * 1024 * 1024;

void MemoryCheck(uintptr_t ramstart, uintptr_t size) {
    (void)ramstart; (void)size;

    /* Ensure the low-address pool exists (may already be created by
     * early operator new calls from global constructors). */
    pool_ensure_init();

    fprintf(stderr, "[heap] Low-address pool: %p .. %p (%zu MB)\n",
            sPool, sPool + sPoolSize, sPoolSize / (1024*1024));

    /* Allocate fixed buffers from the pool */
    u16 *depthBuf = (u16 *)pool_alloc(FB_W * FB_H * sizeof(u16));
    u8  *fbBlock  = (u8  *)pool_alloc(FB_SIZE * 2);
    u8  *heapBuf  = (u8  *)pool_alloc(HEAP_SIZE);

    if (!depthBuf || !fbBlock || !heapBuf) {
        fprintf(stderr, "[heap] MemoryCheck: pool allocation failed\n");
        exit(1);
    }

    memset(depthBuf, 0, FB_W * FB_H * sizeof(u16));
    memset(fbBlock,  0, FB_SIZE * 2);

    gMemCheckStruct.ramstartVal       = 0;
    gMemCheckStruct.DepthBuffer       = depthBuf;
    gMemCheckStruct.heapStart         = heapBuf;
    gMemCheckStruct.frameBuffers[0]   = fbBlock;
    gMemCheckStruct.frameBuffers[1]   = fbBlock + FB_SIZE;
    gMemCheckStruct.RamSize           = 8 * 1024 * 1024;
    gMemCheckStruct.ramVal0           = 0;
    gMemCheckStruct.frameBufferSize0  = (u32)FB_SIZE;
    gMemCheckStruct.mem_free_allocated= (u32)HEAP_SIZE;
    gMemCheckStruct.frameBufferSize1  = (u32)FB_SIZE;

    gExpPakFlag = 1;

    fprintf(stderr, "[heap] MemoryCheck complete: heap=%p (%zu MB)\n",
            heapBuf, HEAP_SIZE / (1024*1024));
}

/* =========================================================================
 * HeapInit / HeapAlloc / HeapFree
 * ========================================================================= */

static constexpr size_t HB_SZ = sizeof(HeapBlock);

void HeapInit(void *start, size_t size) {
    (void)start; (void)size;
}

void *HeapAlloc(size_t size, char *file, u32 line) {
    (void)file; (void)line;
    if (size == 0) size = 1;
    /* Reject obviously corrupt allocation sizes (> 32 MB) */
    if (size > 32 * 1024 * 1024) {
        fprintf(stderr, "[heap] HeapAlloc(%zu) rejected – likely corrupt size (file=%s line=%u)\n",
                size, file ? file : "?", line);
        return nullptr;
    }
    /* Allocate from the low-address pool instead of system malloc */
    HeapBlock *hb = (HeapBlock *)pool_alloc(HB_SZ + size);
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
    pool_free((u8 *)ptr - HB_SZ);
}

/* =========================================================================
 * MemMon stubs
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
