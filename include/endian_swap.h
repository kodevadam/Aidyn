#pragma once
/*
 * Endian conversion utilities for the Linux native port.
 *
 * The N64 ROM (z64) and CPU are big-endian.  Game code reads multi-byte
 * values from DMA'd ROM data as native integers.  On little-endian x86
 * we must byte-swap those fields after DMA.
 *
 * DmaRead copies raw bytes (no blanket swap) so that byte-oriented data
 * (compressed streams, strings, textures) stays intact.  Callers swap
 * individual struct fields with the macros below.
 */

#include "typedefs.h"
#include <cstring>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline u16 be16(u16 v) { return __builtin_bswap16(v); }
static inline s16 be16s(s16 v) { return (s16)__builtin_bswap16((u16)v); }
static inline u32 be32(u32 v) { return __builtin_bswap32(v); }
static inline s32 be32s(s32 v) { return (s32)__builtin_bswap32((u32)v); }

/* Swap a field in place */
#define BE16(x) do { (x) = be16(x); } while(0)
#define BE16S(x) do { (x) = be16s(x); } while(0)
#define BE32(x) do { (x) = be32(x); } while(0)
#define BE32S(x) do { (x) = be32s(x); } while(0)

#else /* big-endian host — no-ops */

static inline u16 be16(u16 v) { return v; }
static inline s16 be16s(s16 v) { return v; }
static inline u32 be32(u32 v) { return v; }
static inline s32 be32s(s32 v) { return v; }

#define BE16(x)  ((void)0)
#define BE16S(x) ((void)0)
#define BE32(x)  ((void)0)
#define BE32S(x) ((void)0)

#endif
