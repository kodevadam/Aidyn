/*
 * ultra64.h - Linux native compatibility shim for N64 libultra SDK
 *
 * Replaces the N64 SDK headers so that game source code (src_pseudo/) can be
 * compiled natively on x86-64 Linux.  Actual implementations live in
 * platform/linux/os_impl.cpp and the other backend files.
 */
#pragma once

/* _GNU_SOURCE must be defined before ANY system header to expose all POSIX
 * symbols (pthread_*, clock_nanosleep, etc.) on Linux. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* =========================================================================
 * Basic N64 integer types
 * ========================================================================= */
typedef uint8_t   u8;
typedef int8_t    s8;
typedef uint16_t  u16;
typedef int16_t   s16;
typedef uint32_t  u32;
typedef int32_t   s32;
typedef uint64_t  u64;
typedef int64_t   s64;
typedef float     f32;
typedef double    f64;
typedef u64       OSTime;
typedef s32       OSPri;
typedef u32       OSId;
typedef u32       OSEvent;

/* =========================================================================
 * TV / region
 * ========================================================================= */
#define OS_TV_PAL   0
#define OS_TV_NTSC  1
#define OS_TV_MPAL  2

extern u32 osTvType;

/* =========================================================================
 * Thread priorities
 * ========================================================================= */
#define OS_PRIORITY_IDLE      0
#define OS_PRIORITY_APPMAX    127
#define OS_PRIORITY_PIMGR     150
#define OS_PRIORITY_VIMGR     254
#define OS_PRIORITY_RMON      250

/* OS event types */
#define OS_EVENT_SW1          0
#define OS_EVENT_SW2          1
#define OS_EVENT_CART         2
#define OS_EVENT_COUNTER      3
#define OS_EVENT_SP           4
#define OS_EVENT_SI           5
#define OS_EVENT_AI           6
#define OS_EVENT_VI           7
#define OS_EVENT_PI           8
#define OS_EVENT_DP           9
#define OS_EVENT_CPU_BREAK    10
#define OS_EVENT_SP_BREAK     11
#define OS_EVENT_FAULT        12
#define OS_EVENT_THREADSTATUS 13
#define OS_EVENT_PRENMI       14

/* =========================================================================
 * Message queues
 * ========================================================================= */
typedef void *OSMesg;

#define OS_MESG_NOBLOCK  0
#define OS_MESG_BLOCK    1
#define OS_MESG_PRI_NORMAL 0
#define OS_MESG_PRI_HIGH   1

/* pthread must be included before <mutex>/<condition_variable> so that the
 * compiler sees the pthread_* symbols before GCC's internal thread wrappers
 * try to reference them. */
#include <pthread.h>

#ifdef __cplusplus
#include <mutex>
#include <condition_variable>
#include <queue>

struct OSMesgQueue {
    std::mutex              mtx;
    std::condition_variable cv;
    OSMesg                 *msgBuf;   /* caller-supplied ring buffer (unused on Linux but kept for ABI) */
    s32                     msgCount; /* capacity */
    std::queue<OSMesg>      msgs;
};

#else
/* C-visible opaque layout (must be at least as large as the C++ version) */
typedef struct {
    unsigned char opaque[128];
} OSMesgQueue;
#endif /* __cplusplus */

/* =========================================================================
 * I/O message (used by Pi manager DMA)
 * ========================================================================= */
typedef struct {
    OSMesg  hdr;
    u32     type;
    u32     pri;
    u32     devAddr;
    void   *vAddr;
    u32     nbytes;
    s32     status;
} OSIoMesg;

/* =========================================================================
 * OS Thread
 * ========================================================================= */
#include <pthread.h>

/* MIPS CPU register context (N64 thread context) */
typedef struct {
    u64 at, v0, v1, a0, a1, a2, a3;
    u64 t0, t1, t2, t3, t4, t5, t6, t7;
    u64 s0, s1, s2, s3, s4, s5, s6, s7;
    u64 t8, t9;
    u64 gp, sp, s8, ra;
    u64 lo, hi;
    u32 sr;    /* status register */
    u32 cause; /* cause register */
    u32 badvaddr;
    u32 pc;    /* program counter */
    u32 rcp;
    u64 fpcsr;
    u64 fp0,  fp2,  fp4,  fp6;
    u64 fp8,  fp10, fp12, fp14;
    u64 fp16, fp18, fp20, fp22;
    u64 fp24, fp26, fp28, fp30;
} __OSThreadContext;

typedef struct OSThread_s {
    struct OSThread_s *next;
    OSPri              priority;
    OSId               id;
    pthread_t          thread;
    pthread_attr_t     attr;
    /* entry / arg stored so we can start later */
    void             (*entry)(void *);
    void              *arg;
    int                started;
    __OSThreadContext  context; /* MIPS CPU context (PC, RA, cause, SP, etc.) */
} OSThread;

/* MIPS CAUSE register bits */
#define CAUSE_BD        0x80000000  /* branch delay */
#define CAUSE_IP8       0x00008000  /* interrupt pending 8 */
#define CAUSE_IP7       0x00004000
#define CAUSE_IP6       0x00002000
#define CAUSE_IP5       0x00001000
#define CAUSE_IP4       0x00000800
#define CAUSE_IP3       0x00000400
#define CAUSE_SW2       0x00000200
#define CAUSE_SW1       0x00000100
#define CAUSE_EXCMASK   0x0000007C  /* exception code mask */
#define EXC_INT         0x00000000  /* interrupt */
#define EXC_MOD         0x00000004  /* TLB modification */
#define EXC_RMISS       0x00000008  /* TLB miss on load */
#define EXC_WMISS       0x0000000C  /* TLB miss on store */
#define EXC_RADE        0x00000010  /* address error on load */
#define EXC_WADE        0x00000014  /* address error on store */
#define EXC_IBE         0x00000018  /* bus error on instruction */
#define EXC_DBE         0x0000001C  /* bus error on data */
#define EXC_SYSCALL     0x00000020  /* syscall */
#define EXC_BREAK       0x00000024  /* breakpoint */
#define EXC_II          0x00000028  /* illegal instruction */
#define EXC_CPU         0x0000002C  /* coprocessor unusable */
#define EXC_OV          0x00000030  /* overflow */
#define EXC_TRAP        0x00000034  /* trap */
#define EXC_VCEI        0x00000038  /* virtual coherency on instruction fetch */
#define EXC_FPE         0x0000003C  /* floating point */
#define EXC_WATCH       0x00000054  /* watch */
#define EXC_VCED        0x0000005C  /* virtual coherency on data */

/* =========================================================================
 * Video interface (VI)
 * ========================================================================= */
typedef struct {
    struct {
        u32 type;
        u32 width;
        u32 burst;
        u32 vSync;
        u32 hSync;
        u32 leap;
        u32 hStart;
        u32 xScale;
        u32 vCurrent;
    } comRegs;
    struct {
        u32 origin;
        u32 yScale;
        u32 vStart;
        u32 vBurst;
        u32 vIntr;
        u32 vScale;
    } fldRegs[2];
} OSViMode;

/* Pre-defined VI mode table indices */
#define OS_VI_NTSC_LAN1  2
#define OS_VI_NTSC_LAN2  3   /* NTSC 32-bit color low-res */
#define OS_VI_PAL_LAN1  10
#define OS_VI_PAL_LAN2  11   /* PAL 32-bit color low-res */
#define OS_VI_MPAL_LAN1  6
#define OS_VI_MPAL_LAN2  7   /* MPAL 32-bit color low-res */

extern OSViMode osViModeTable[];

/* =========================================================================
 * Graphics – Gfx display list command (64-bit word)
 *
 * On Linux these words are written to a buffer that the rendering backend
 * interprets.  The GBI macros below write minimal sentinel data so the
 * buffer is not empty, but actual OpenGL calls are driven by gfx_backend.cpp.
 * ========================================================================= */
typedef union {
    u64 force_structure_alignment;
    struct { u32 hi, lo; } w;
    struct { u32 w0, w1; } words; /* alternate names used in some game code */
    struct {
        u8  cmd;
        u8  pad[3];
        u32 lo;
    } dma;
} Gfx;

/* Fixed-point 4x4 matrix (N64 format: 16.16) */
typedef struct {
    s16 m[4][4];
    u16 f[4][4];
} Mtx, Mtx_t;

/* Viewport */
typedef struct {
    s16 vscale[4];
    s16 vtrans[4];
} Vp, Vp_t;

/* =========================================================================
 * N64 RSP Vertex (Vtx)
 * 16 bytes: position (s16 x3), pad, texcoord (s16 x2), colour/normal (u8 x4)
 * ========================================================================= */
typedef struct {
    s16  ob[3];   /* x, y, z object-space position */
    u16  flag;    /* flag (usually 0) */
    s16  tc[2];   /* texture coordinates (s10.5 fixed point) */
    u8   cn[4];   /* colour (RGBA) or normal (packed) + alpha */
} Vtx_t, Vtx_tn;

typedef union {
    Vtx_t   v;    /* standard vertex member (libultra convention) */
    Vtx_t   n;    /* alias used by some game code */
    Vtx_tn  t;    /* same – aliased for convenience */
    long long int force_structure_alignment;
} Vtx;

/* =========================================================================
 * N64 Lighting structures
 * ========================================================================= */
typedef struct {
    u8 col[3];   /* diffuse colour RGB */
    u8 pad1;
    u8 colc[3];  /* copy of col (N64 HW quirk) */
    u8 pad2;
    s8 dir[3];   /* direction (normalised, -128..127) */
    u8 pad3;
} Light_t;

typedef struct {
    u8 col[3];   /* ambient colour RGB */
    u8 pad1;
    u8 colc[3];  /* copy */
    u8 pad2;
} Ambient_t;

/* Light / Ambient – match N64 SDK union layout so that .l member works */
typedef union {
    Light_t   l;
    long long int force_structure_alignment;
} Light;

typedef union {
    Ambient_t a;
    long long int force_structure_alignment;
} Ambient;

typedef struct {
    Ambient a;
    Light   l[1];
} Lights0;

typedef struct {
    Ambient a;
    Light   l[1];
} Lights1;

typedef struct {
    Ambient a;
    Light   l[2];
} Lights2;

typedef struct {
    Ambient a;
    Light   l[3];
} Lights3;

typedef struct {
    Ambient a;
    Light   l[7];
} Lights7;

/* =========================================================================
 * LookAt – lookat matrix for texture generation
 * ========================================================================= */
typedef struct {
    u8 col[3];
    u8 pad1;
    u8 colc[3];
    u8 pad2;
    s8 dir[3];
    u8 pad3;
} LookAt_t;

typedef struct {
    LookAt_t l[2];
} LookAt;

/* =========================================================================
 * Dynamic light (used for per-frame lighting updates)
 * ========================================================================= */
typedef struct {
    u8 col[3];
    u8 pad1;
    u8 colc[3];
    u8 pad2;
    s8 dir[3];
    u8 pad3;
} DirLight;

/* Fog parameters (stored in OS_PKTCNT_FOGINFO word) */
typedef struct {
    s16 fm; /* fog multiplier */
    s16 fo; /* fog offset */
} FogParams;

/* =========================================================================
 * Controller / Controller Pak
 * ========================================================================= */
#define MAXCONTROLLERS 4

/* Button bitmasks (matching N64 SDK) */
#define CONT_A        0x8000
#define CONT_B        0x4000
#define CONT_G        0x2000  /* Z trigger */
#define CONT_START    0x1000
#define CONT_UP       0x0800
#define CONT_DOWN     0x0400
#define CONT_LEFT     0x0200
#define CONT_RIGHT    0x0100
#define CONT_L        0x0020
#define CONT_R        0x0010
#define U_CBUTTONS    0x0008
#define D_CBUTTONS    0x0004
#define L_CBUTTONS    0x0002
#define R_CBUTTONS    0x0001

typedef struct {
    u16 type;
    u8  status;
    u8  errno_;
} OSContStatus;

typedef struct {
    u16 button;
    s8  stick_x;
    s8  stick_y;
    u8  errno_;
} OSContPad;

typedef struct {
    int channel;
    int initialized;
    u8  status;
} OSPfs;

/* OSPfsState – controller pak file state */
typedef struct {
    u8  company_code;
    u8  game_code;
    u8  game_name[16];
    u8  ext_name[4];
    u32 file_size;
} OSPfsState;

/* PFS error codes */
#define PFS_ERR_NOPACK       1
#define PFS_ERR_NEW_PACK     2
#define PFS_ERR_INCONSISTENT 3
#define PFS_ERR_CONTRFAIL    4
#define PFS_ERR_BAD_DATA     5
#define PFS_ERR_ID_FATAL     6
#define PFS_DATA_FULL        7
#define PFS_DIR_FULL         8
#define PFS_ERR_DEVICE       9
#define PFS_ERR_INVALID      10
#define PFS_ERR_EXIST        11

/* PFS read/write flags */
#define PFS_READ             0
#define PFS_WRITE            1
/* PFS block/page sizes */
#define PFS_ONE_PAGE         32    /* bytes per page */
#define BLOCKSIZE            256   /* bytes per block (8 pages) */

/* PI DMA direction */
#define OS_READ              0
#define OS_WRITE             1

/* Controller Pak card status bits */
#define CONT_CARD_ON         0x01
#define CONT_CARD_PULL       0x02

/* Controller error bits */
#define CONT_NO_RESPONSE_ERROR  0x08
#define CONT_OVERRUN_ERROR      0x04

/* Controller type constants */
#define CONT_TYPE_MASK          0x1FFF
#define CONT_TYPE_NORMAL        0x0005

/* =========================================================================
 * Audio types (ALSynth / libultra audio subsystem)
 *
 * The game uses the N64 audio library (alSynNew, ALPlayer, ALHeap, etc.).
 * On Linux these are all stubbed – the real audio backend (SDL_mixer or
 * OpenAL) is driven from audio_backend.cpp.
 * ========================================================================= */
typedef s32 ALMicroTime;
typedef u8  ALPan;
typedef void *ALDMAproc;

typedef struct {
    u8 *base;
    u8 *cur;
    s32 len;
    s32 count;
} ALHeap;

/* Loop descriptors */
typedef struct {
    u32 start;
    u32 end;
    s32 count;
} ALRawLoop;

typedef ALRawLoop ALADPCMLoop;

/* Wave table with waveInfo sub-union (game accesses rawWave/adpcmWave) */
typedef struct {
    u8  type;
    u8  flags;
    u8  loopCount;
    u8  pad;
    u8 *base;
    u32 len;
    union {
        struct {
            u8       *base;
            ALRawLoop *loop;
        } rawWave;
        struct {
            u8         *base;
            ALADPCMLoop *loop;
            void        *book;
        } adpcmWave;
    } waveInfo;
} ALWaveTable;

/* Physical voice – internal N64 audio structure, stub for compilation */
typedef struct {
    struct { u8 *state; } decoder;
} PVoice_s;

typedef struct {
    int       id;
    int       active;
    ALWaveTable *wavetable;
    s32       pitch;
    u8        vol;
    u8        pan;
    PVoice_s *pvoice;
} ALVoice;

typedef struct {
    int maxPVoices;
    int maxUpdates;
    int maxVVoices;
    ALDMAproc dmaproc;
    int fxType;
    u32 outputRate;
    ALHeap *heap;
} ALSynConfig;

typedef struct {
    u8 priority;
    u8 fxBus;
    u8 unityPitch;
} ALVoiceConfig;

typedef struct {
    int voiceCount;
    void *backend;
    u32 outputRate;
} ALSynth;

typedef struct {
    int dummy;
} ALGlobals;

typedef struct ALPlayer_s {
    struct ALPlayer_s *next;
    ALMicroTime (*handler)(void *);
    void *clientData;
    ALGlobals *globals;
    int active;
} ALPlayer;

/* Acmd – RSP audio command; game accesses .adpcm sub-field */
typedef union {
    u64 force_structure_alignment;
    struct { u32 w0, w1; } words;
    struct { u32 type; u32 data; } adpcm;
} Acmd;

typedef void (*ALVoiceHandler)(void *);

/* Wave types */
#define AL_ADPCM_WAVE  0
#define AL_RAW16_WAVE  1
#ifndef AL_RAW8_WAVE
#define AL_RAW8_WAVE   2
#endif

/* FX types */
#define AL_FX_NONE      0
#define AL_FX_SMALLROOM 1
#define AL_FX_BIGROOM   2
#define AL_FX_ECHO      3
#define AL_FX_CHORUS    4
#define AL_FX_FLANGER   5

/* RSP microcode binary symbols (N64 ROM segments, stubbed on Linux) */
extern u8 rspbootTextStart[4];
extern u8 aspMainTextStart[4];
extern u8 aspMainDataStart[4];
extern u8 gspF3DEX2_fifoTextStart[4];
extern u8 gspF3DEX2_fifoDataStart[4];

/* =========================================================================
 * GBI – Graphics Binary Interface macros
 *
 * All macros expand to a no-op pointer advance.  The real display list
 * format is N64-specific; on Linux we rely on the backend to render game
 * state through its own API rather than parsing the binary display list.
 *
 * Macros that *return* Gfx* (the functional API) just return gdl unchanged.
 * Macros that write via pointer (the statement API) just advance the pointer.
 * ========================================================================= */

/* _DW – N64 SDK statement-wrapper macro (passes its argument through) */
#define _DW(x) x

/* Bit-shift utility (N64 SDK macros) */
#define _SHIFTL(v, s, w)  (((u32)(v) & ((0x01 << (w)) - 1)) << (s))
#define _SHIFTR(v, s, w)  (((u32)(v) >> (s)) & ((0x01 << (w)) - 1))

/* -------------------------------------------------------------------------
 * F3DEX2 GBI opcode constants
 * ------------------------------------------------------------------------- */
#define G_SPNOOP          0x00
#define G_VTX             0x01
#define G_MODIFYVTX       0x02
#define G_CULLDL          0x03
#define G_BRANCH_Z        0x04
#define G_TRI1            0x05
#define G_TRI2            0x06
#define G_QUAD            0x07
#define G_TEXTURE_CMD     0xD7  /* renamed to avoid clash with G_TEXTURE flag */
#define G_POPMTX          0xD8
#define G_GEOMETRYMODE    0xD9
#define G_MTX             0xDA
#define G_MOVEWORD        0xDB
#define G_MOVEMEM         0xDC
#define G_DL              0xDE
#define G_ENDDL           0xDF
#define G_SETOTHERMODE_L  0xE2
#define G_SETOTHERMODE_H  0xE3
#define G_TEXRECT         0xE4
#define G_TEXRECTFLIP     0xE5
#define G_RDPLOADSYNC     0xE6
#define G_RDPPIPESYNC     0xE7
#define G_RDPTILESYNC     0xE8
#define G_RDPFULLSYNC     0xE9
#define G_SETSCISSOR_CMD  0xED
#define G_SETPRIMDEPTH    0xEE
#define G_LOADTLUT        0xF0
#define G_SETTILESIZE     0xF2
#define G_LOADBLOCK       0xF3
#define G_LOADTILE        0xF4
#define G_SETTILE         0xF5
#define G_FILLRECT        0xF6
#define G_SETFILLCOLOR    0xF7
#define G_SETFOGCOLOR     0xF8
#define G_SETBLENDCOLOR   0xF9
#define G_SETPRIMCOLOR    0xFA
#define G_SETENVCOLOR     0xFB
#define G_SETCOMBINE      0xFC
#define G_SETTIMG         0xFD
#define G_SETZIMG         0xFE
#define G_SETCIMG         0xFF

/* G_MOVEWORD indices */
#define G_MWO_POINT_RGBA  0x10
#define G_MWO_POINT_ST    0x14
#define G_MW_SEGMENT      0x06
#define G_MW_FOG          0x08
#define G_MW_PERSPNORM    0x0E
#define G_MW_CLIP         0x04

/* Matrix parameter flags */
#define G_MTX_MODELVIEW     0x00
#define G_MTX_PROJECTION    0x04
#define G_MTX_MUL           0x00
#define G_MTX_LOAD          0x02
#define G_MTX_NOPUSH        0x00
#define G_MTX_PUSH          0x01

/* Texture image formats */
#define G_IM_FMT_RGBA   0
#define G_IM_FMT_YUV    1
#define G_IM_FMT_CI     2
#define G_IM_FMT_IA     3
#define G_IM_FMT_I      4

/* Texture image component sizes */
#define G_IM_SIZ_4b   0
#define G_IM_SIZ_8b   1
#define G_IM_SIZ_16b  2
#define G_IM_SIZ_32b  3

/* G_MOVEMEM indices */
#define G_MV_VIEWPORT     0x08
#define G_MV_LIGHT        0x0A
#define G_MV_LOOKATY       0x82
#define G_MV_LOOKATX       0x84

/* -------------------------------------------------------------------------
 * GBI helpers
 * ------------------------------------------------------------------------- */

/* Write a display list command (statement form, used with gdl++) */
#define _GBI_CMD(gdl, hi_val, lo_val) do { \
    (gdl)->w.hi = (u32)(hi_val); \
    (gdl)->w.lo = (u32)(lo_val); \
} while(0)

/* Static initializer for Gfx array literals (little-endian u64 packing) */
#define _GBI_W(h, l) { (u64)(u32)(h) | ((u64)(u32)(l) << 32) }
#define _GBI_W0(h)   { (u64)(u32)(h) }

/* Backward compat: _GBI_NOP still used in some edge cases */
#define _GBI_NOP(gdl) _GBI_CMD(gdl, 0, 0)

/* -------------------------------------------------------------------------
 * GBI statement macros (g* prefix) — write into Gfx* then caller does gdl++
 * ------------------------------------------------------------------------- */

/* --- Pipeline sync / NOP --- */
#define gDPPipeSync(gdl)             _GBI_CMD(gdl, (u32)G_RDPPIPESYNC << 24, 0)
#define gDPFullSync(gdl)             _GBI_CMD(gdl, (u32)G_RDPFULLSYNC << 24, 0)
#define gDPTileSync(gdl)             _GBI_CMD(gdl, (u32)G_RDPTILESYNC << 24, 0)
#define gDPLoadSync(gdl)             _GBI_CMD(gdl, (u32)G_RDPLOADSYNC << 24, 0)
#define gDPNoOp(gdl)                 _GBI_CMD(gdl, 0, 0)
#define gSPNoOp(gdl)                 _GBI_CMD(gdl, 0, 0)

/* --- Display list control --- */
#define gSPEndDisplayList(gdl)       _GBI_CMD(gdl, (u32)G_ENDDL << 24, 0)
#define gSPDisplayList(gdl, dl)      _GBI_CMD(gdl, (u32)G_DL << 24, (u32)(uintptr_t)(dl))
#define gSPBranchList(gdl, dl)       _GBI_CMD(gdl, ((u32)G_DL << 24) | 0x010000, (u32)(uintptr_t)(dl))

/* --- Matrix --- */
#define gSPMatrix(gdl, m, p) _GBI_CMD(gdl, \
    ((u32)G_MTX << 24) | _SHIFTL(sizeof(Mtx)-1, 5, 19) | ((u32)((p) ^ G_MTX_PUSH) & 0xFF), \
    (u32)(uintptr_t)(m))
#define gSPPopMatrix(gdl, p) _GBI_CMD(gdl, \
    ((u32)G_POPMTX << 24) | 0x380002, (u32)(sizeof(Mtx)))
#define gSPLoadMatrix(gdl, m)        gSPMatrix(gdl, m, G_MTX_LOAD)
#define gSPInsertMatrix(gdl, ...)    _GBI_CMD(gdl, 0, 0)
#define gSPForceMatrix(gdl, mptr)    _GBI_CMD(gdl, (u32)G_MOVEMEM << 24, (u32)(uintptr_t)(mptr))

/* --- Viewport --- */
#define gSPViewport(gdl, vp) _GBI_CMD(gdl, \
    ((u32)G_MOVEMEM << 24) | (G_MV_VIEWPORT << 8) | 0x38, \
    (u32)(uintptr_t)(vp))

/* --- Vertex / triangle --- */
#define gSPVertex(gdl, v, n, v0) _GBI_CMD(gdl, \
    ((u32)G_VTX << 24) | ((u32)(n) << 12) | (((u32)(v0) + (u32)(n)) << 1), \
    (u32)(uintptr_t)(v))

#define gSP1Triangle(gdl, v0, v1, v2, flag) _GBI_CMD(gdl, \
    ((u32)G_TRI1 << 24) | ((u32)(v0)*2 << 16) | ((u32)(v1)*2 << 8) | ((u32)(v2)*2), 0)

#define gSP2Triangles(gdl, v0, v1, v2, f0, v3, v4, v5, f1) _GBI_CMD(gdl, \
    ((u32)G_TRI2 << 24) | ((u32)(v0)*2 << 16) | ((u32)(v1)*2 << 8) | ((u32)(v2)*2), \
    ((u32)(v3)*2 << 16) | ((u32)(v4)*2 << 8) | ((u32)(v5)*2))

#define gSP4Triangles(gdl, ...)      _GBI_CMD(gdl, (u32)G_TRI2 << 24, 0)

#define gSPModifyVertex(gdl, vtx, where, val) _GBI_CMD(gdl, \
    ((u32)G_MODIFYVTX << 24) | ((u32)(where) << 16) | ((u32)(vtx)*2), (u32)(val))

/* --- Lighting --- */
#define gSPSetLights0(gdl, l)        _GBI_CMD(gdl, (u32)G_MOVEMEM << 24, 0)
#define gSPSetLights1(gdl, l)        _GBI_CMD(gdl, (u32)G_MOVEMEM << 24, 0)
#define gSPSetLights2(gdl, l)        _GBI_CMD(gdl, (u32)G_MOVEMEM << 24, 0)
#define gSPNumLights(gdl, n)         _GBI_CMD(gdl, \
    ((u32)G_MOVEWORD << 24) | (0x80000 | ((u32)(n)+1)*0x20), 0)
#define gSPLight(gdl, l, n) _GBI_CMD(gdl, \
    ((u32)G_MOVEMEM << 24) | (G_MV_LIGHT << 8) | 0x18, \
    (u32)(uintptr_t)(l))
#define gSPLookAt(gdl, l) _GBI_CMD(gdl, \
    ((u32)G_MOVEMEM << 24) | (G_MV_LOOKATY << 8), \
    (u32)(uintptr_t)(l))

/* --- Geometry mode --- */
#define gSPSetGeometryMode(gdl, m)   _GBI_CMD(gdl, ((u32)G_GEOMETRYMODE << 24) | 0x00FFFFFF, (u32)(m))
#define gSPClearGeometryMode(gdl, m) _GBI_CMD(gdl, ((u32)G_GEOMETRYMODE << 24) | (~(u32)(m) & 0x00FFFFFF), 0)
#define gSPLoadGeometryMode(gdl, m)  _GBI_CMD(gdl, (u32)G_GEOMETRYMODE << 24, (u32)(m))

/* --- Texture state --- */
#define gSPTexture(gdl, s, t, level, tile, on) _GBI_CMD(gdl, \
    ((u32)G_TEXTURE_CMD << 24) | ((u32)(level) << 11) | ((u32)(tile) << 8) | (u32)!!(on), \
    ((u32)(s) << 16) | ((u32)(t) & 0xFFFF))

/* --- Texture image / tile --- */
#define gDPSetTextureImage(gdl, fmt, siz, width, img) _GBI_CMD(gdl, \
    ((u32)G_SETTIMG << 24) | ((u32)(fmt) << 21) | ((u32)(siz) << 19) | ((u32)(width) - 1), \
    (u32)(uintptr_t)(img))

#define gDPSetTile(gdl, fmt, siz, line, tmem, tile, palette, cmt, maskt, shiftt, cms, masks, shifts) \
    _GBI_CMD(gdl, \
        ((u32)G_SETTILE << 24) | ((u32)(fmt) << 21) | ((u32)(siz) << 19) | ((u32)(line) << 9) | (u32)(tmem), \
        ((u32)(tile) << 24) | ((u32)(palette) << 20) | ((u32)(cmt) << 18) | ((u32)(maskt) << 14) | \
        ((u32)(shiftt) << 10) | ((u32)(cms) << 8) | ((u32)(masks) << 4) | (u32)(shifts))

#define gDPLoadTile(gdl, tile, uls, ult, lrs, lrt) _GBI_CMD(gdl, \
    ((u32)G_LOADTILE << 24) | ((u32)(uls) << 12) | (u32)(ult), \
    ((u32)(tile) << 24) | ((u32)(lrs) << 12) | (u32)(lrt))

#define gDPSetTileSize(gdl, tile, uls, ult, lrs, lrt) _GBI_CMD(gdl, \
    ((u32)G_SETTILESIZE << 24) | ((u32)(uls) << 12) | (u32)(ult), \
    ((u32)(tile) << 24) | ((u32)(lrs) << 12) | (u32)(lrt))

#define gDPLoadBlock(gdl, tile, uls, ult, lrs, dxt) _GBI_CMD(gdl, \
    ((u32)G_LOADBLOCK << 24) | ((u32)(uls) << 12) | (u32)(ult), \
    ((u32)(tile) << 24) | ((u32)(lrs) << 12) | (u32)(dxt))

/* Multi-command texture load helpers — encode as single commands for simplicity */
#define gDPLoadTextureBlock(gdl, ...)          _GBI_CMD(gdl, (u32)G_SETTIMG << 24, 0)
#define gDPLoadTextureBlock_4b(gdl, ...)       _GBI_CMD(gdl, (u32)G_SETTIMG << 24, 0)
#define gDPLoadTextureTile(gdl, ...)           _GBI_CMD(gdl, (u32)G_SETTIMG << 24, 0)
#define gDPLoadTextureTile_4b(gdl, ...)        _GBI_CMD(gdl, (u32)G_SETTIMG << 24, 0)

#define gDPSetTextureLUT(gdl, mode)  _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (14 << 8) | 2, (u32)(mode) << 14)

#define gDPLoadTLUT_pal16(gdl, pal, dram) _GBI_CMD(gdl, \
    ((u32)G_LOADTLUT << 24), ((u32)(pal) << 24) | 0x0F0000)

#define gDPLoadTLUT_pal256(gdl, dram) _GBI_CMD(gdl, \
    ((u32)G_LOADTLUT << 24), 0x00FF0000)

/* --- Combiner / render mode --- */
#define gDPSetCombineMode(gdl, a, b) _GBI_CMD(gdl, (u32)G_SETCOMBINE << 24, 0)
#define gDPSetCombineLERP(gdl, ...)  _GBI_CMD(gdl, (u32)G_SETCOMBINE << 24, 0)
#define gDPSetCombine(gdl, a, b)     _GBI_CMD(gdl, ((u32)G_SETCOMBINE << 24) | ((u32)(a) & 0x00FFFFFF), (u32)(b))
#define gDPSetRenderMode(gdl, a, b)  _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_L << 24) | (3 << 8) | 29, (u32)(a) | (u32)(b))
#define gDPSetAlphaCompare(gdl, c)   _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_L << 24) | (0 << 8) | 2, (u32)(c))
#define gDPSetDepthSource(gdl, src)  _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_L << 24) | (2 << 8) | 1, (u32)(src) << 2)
#define gDPSetDepthImage(gdl, img)   _GBI_CMD(gdl, (u32)G_SETZIMG << 24, (u32)(uintptr_t)(img))
#define gDPSetColorImage(gdl, fmt, siz, w, img) _GBI_CMD(gdl, \
    ((u32)G_SETCIMG << 24) | ((u32)(fmt) << 21) | ((u32)(siz) << 19) | ((u32)(w) - 1), \
    (u32)(uintptr_t)(img))

/* --- Color registers --- */
#define gDPSetColor(gdl, cmd, val)   _GBI_CMD(gdl, (u32)(cmd) << 24, (u32)(val))
#define DPRGBColor(gdl, cmd, r, g, b, a) _GBI_CMD(gdl, (u32)(cmd) << 24, \
    ((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))
#define gDPSetFillColor(gdl, c)      _GBI_CMD(gdl, (u32)G_SETFILLCOLOR << 24, (u32)(c))
#define gDPSetFogColor(gdl, r, g, b, a) _GBI_CMD(gdl, (u32)G_SETFOGCOLOR << 24, \
    ((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))
#define gDPSetBlendColor(gdl, r, g, b, a) _GBI_CMD(gdl, (u32)G_SETBLENDCOLOR << 24, \
    ((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))
#define gDPSetPrimColor(gdl, m, l, r, g, b, a) _GBI_CMD(gdl, \
    ((u32)G_SETPRIMCOLOR << 24) | ((u32)(m) << 8) | (u32)(l), \
    ((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))
#define gDPSetEnvColor(gdl, r, g, b, a) _GBI_CMD(gdl, (u32)G_SETENVCOLOR << 24, \
    ((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))

/* --- Fill / scissor rectangles --- */
#define gDPFillRectangle(gdl, xl, yl, xh, yh) _GBI_CMD(gdl, \
    ((u32)G_FILLRECT << 24) | ((u32)(xh) << 14) | ((u32)(yh) << 2), \
    ((u32)(xl) << 14) | ((u32)(yl) << 2))
#define gDPScisFillRectangle(gdl, ...)         _GBI_CMD(gdl, (u32)G_FILLRECT << 24, 0)
#define gDPSetScissor(gdl, mode, xl, yl, xh, yh) _GBI_CMD(gdl, \
    ((u32)G_SETSCISSOR_CMD << 24) | ((u32)((xl)*4) << 12) | (u32)((yl)*4), \
    ((u32)(mode) << 24) | ((u32)((xh)*4) << 12) | (u32)((yh)*4))
#define gDPSetPrimDepth(gdl, z, dz)  _GBI_CMD(gdl, (u32)G_SETPRIMDEPTH << 24, \
    ((u32)(z) << 16) | (u32)(dz))

/* --- Texture rectangle (2D blit) --- */
#define gSPScisTextureRectangle(gdl, ...) _GBI_CMD(gdl, (u32)G_TEXRECT << 24, 0)

/* --- Fog --- */
#define gSPFogPosition(gdl, mn, mx) _GBI_CMD(gdl, \
    ((u32)G_MOVEWORD << 24) | (G_MW_FOG << 16), \
    ((u32)((128000 / ((mx) - (mn))) & 0xFFFF) << 16) | \
    (u32)((500 - (mn)) * 256 / ((mx) - (mn)) & 0xFFFF))
#define gSPSetFog(gdl, ...)          _GBI_CMD(gdl, ((u32)G_MOVEWORD << 24) | (G_MW_FOG << 16), 0)

/* --- Segment --- */
#define gSPSegment(gdl, seg, base) _GBI_CMD(gdl, \
    ((u32)G_MOVEWORD << 24) | (G_MW_SEGMENT << 16) | ((u32)(seg) * 4), \
    (u32)(uintptr_t)(base))

/* --- Other RSP commands --- */
#define gSPClipRatio(gdl, r)                    _GBI_CMD(gdl, ((u32)G_MOVEWORD << 24) | (G_MW_CLIP << 16), 0)
#define gSPPerspNormalize(gdl, s)               _GBI_CMD(gdl, ((u32)G_MOVEWORD << 24) | (G_MW_PERSPNORM << 16), (u32)(s))
#define gSPCullDisplayList(gdl, vstart, vend)   _GBI_CMD(gdl, ((u32)G_CULLDL << 24) | ((u32)(vstart)*2), (u32)(vend)*2)
#define gSPBranchLessZraw(gdl, ...)             _GBI_CMD(gdl, (u32)G_BRANCH_Z << 24, 0)
#define gSPBranchLessZ(gdl, ...)                _GBI_CMD(gdl, (u32)G_BRANCH_Z << 24, 0)

/* --- SetOtherMode wrappers --- */
#define gDPSetCycleType(gdl, c) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (20 << 8) | 2, (u32)(c) << 20)
#define gDPPipelineMode(gdl, m) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (23 << 8) | 1, (u32)(m) << 23)
#define gDPSetCombineKey(gdl, k) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (8 << 8) | 1, (u32)(k) << 8)
#define gDPSetColorDither(gdl, d) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (22 << 8) | 2, (u32)(d) << 22)
#define gDPSetAlphaDither(gdl, d) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (4 << 8) | 2, (u32)(d) << 4)
#define gDPSetTextureLOD(gdl, l) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (12 << 8) | 1, (u32)(l) << 12)
#define gDPSetTextureConvert(gdl, c) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (9 << 8) | 3, (u32)(c) << 9)
#define gDPSetTextureFilter(gdl, f) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (12 << 8) | 2, (u32)(f) << 12)
#define gDPSetTexturePersp(gdl, p) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (19 << 8) | 1, (u32)(p) << 19)
#define gDPSetTextureDetail(gdl, d) _GBI_CMD(gdl, \
    ((u32)G_SETOTHERMODE_H << 24) | (17 << 8) | 2, (u32)(d) << 17)

/* gSPSetOtherMode(gdl, cmd, sft, len, data) — generic other-mode setter */
#define gSPSetOtherMode(gdl, cmd, sft, len, data) _GBI_CMD(gdl, \
    ((u32)(cmd) << 24) | ((u32)(sft) << 8) | (u32)(len), (u32)(data))

/* gDma1p – write a DMA1 packet directly into display list */
#define gDma1p(gdl, c, p, n, v)  _GBI_CMD(gdl, \
    ((u32)(c) << 24) | ((u32)(n) << 5) | (u32)(v), (u32)(uintptr_t)(p))

/* gDPWord — write arbitrary hi/lo values */
#define gDPWord(gdl, h, l)  _GBI_CMD(gdl, (u32)(h), (u32)(l))

/* -------------------------------------------------------------------------
 * GBI static initializer macros (gs* prefix) — for Gfx array literals
 * Uses little-endian u64 packing: u64 = (u32)hi | ((u64)(u32)lo << 32)
 * ------------------------------------------------------------------------- */
#define gsDPPipeSync()                  _GBI_W0((u32)G_RDPPIPESYNC << 24)
#define gsDPFullSync()                  _GBI_W0((u32)G_RDPFULLSYNC << 24)
#define gsDPTileSync()                  _GBI_W0((u32)G_RDPTILESYNC << 24)
#define gsDPLoadSync()                  _GBI_W0((u32)G_RDPLOADSYNC << 24)
#define gsDPNoOp()                      _GBI_W0(0)
#define gsSPNoop()                      _GBI_W0(0)
#define gsSPNoOp()                      _GBI_W0(0)
#define gsSPEndDisplayList()            _GBI_W0((u32)G_ENDDL << 24)
#define gsSPDisplayList(dl)             _GBI_W((u32)G_DL << 24, (u32)(uintptr_t)(dl))
#define gsSPBranchList(dl)              _GBI_W(((u32)G_DL << 24) | 0x010000, (u32)(uintptr_t)(dl))

#define gsDPSetCombine(a, b)            _GBI_W(((u32)G_SETCOMBINE << 24) | ((u32)(a) & 0x00FFFFFF), (u32)(b))
#define gsDPSetCycleType(c)             _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (20 << 8) | 2, (u32)(c) << 20)
#define gsDPPipelineMode(m)             _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (23 << 8) | 1, (u32)(m) << 23)
#define gsDPSetCombineMode(a, b)        _GBI_W0((u32)G_SETCOMBINE << 24)
#define gsDPSetCombineKey(k)            _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (8 << 8) | 1, (u32)(k) << 8)
#define gsDPSetColorDither(d)           _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (22 << 8) | 2, (u32)(d) << 22)
#define gsDPSetAlphaDither(d)           _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (4 << 8) | 2, (u32)(d) << 4)
#define gsDPSetAlphaCompare(c)          _GBI_W(((u32)G_SETOTHERMODE_L << 24) | (0 << 8) | 2, (u32)(c))
#define gsDPSetTextureLOD(l)            _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (12 << 8) | 1, (u32)(l) << 12)
#define gsDPSetTextureLUT(l)            _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (14 << 8) | 2, (u32)(l) << 14)
#define gsDPSetTextureConvert(c)        _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (9 << 8) | 3, (u32)(c) << 9)
#define gsDPSetTextureFilter(f)         _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (12 << 8) | 2, (u32)(f) << 12)
#define gsDPSetTexturePersp(p)          _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (19 << 8) | 1, (u32)(p) << 19)
#define gsDPSetTextureDetail(d)         _GBI_W(((u32)G_SETOTHERMODE_H << 24) | (17 << 8) | 2, (u32)(d) << 17)
#define gsDPSetRenderMode(a, b)         _GBI_W(((u32)G_SETOTHERMODE_L << 24) | (3 << 8) | 29, (u32)(a) | (u32)(b))
#define gsDPSetFillColor(c)             _GBI_W((u32)G_SETFILLCOLOR << 24, (u32)(c))
#define gsDPFillRectangle(xl, yl, xh, yh) _GBI_W( \
    ((u32)G_FILLRECT << 24) | ((u32)(xh) << 14) | ((u32)(yh) << 2), \
    ((u32)(xl) << 14) | ((u32)(yl) << 2))
#define gsDPSetScissor(...)             _GBI_W0((u32)G_SETSCISSOR_CMD << 24)
#define gsDPSetPrimColor(m, l, r, g, b, a) _GBI_W( \
    ((u32)G_SETPRIMCOLOR << 24) | ((u32)(m) << 8) | (u32)(l), \
    ((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))
#define gsDPSetEnvColor(r, g, b, a)     _GBI_W((u32)G_SETENVCOLOR << 24, \
    ((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))
#define gsDPSetColorImage(fmt, siz, w, img) _GBI_W( \
    ((u32)G_SETCIMG << 24) | ((u32)(fmt) << 21) | ((u32)(siz) << 19) | ((u32)(w) - 1), \
    (u32)(uintptr_t)(img))
#define gsDPSetDepthImage(img)          _GBI_W((u32)G_SETZIMG << 24, (u32)(uintptr_t)(img))
#define gsDPSetDepthSource(s)           _GBI_W(((u32)G_SETOTHERMODE_L << 24) | (2 << 8) | 1, (u32)(s) << 2)
#define gsDPSetTile(...)                _GBI_W0((u32)G_SETTILE << 24)
#define gsDPSetTileSize(...)            _GBI_W0((u32)G_SETTILESIZE << 24)
#define gsDPWord(h, l)                  _GBI_W((u32)(h), (u32)(l))

#define gsSPMatrix(m, p)                _GBI_W(((u32)G_MTX << 24) | ((u32)((p) ^ G_MTX_PUSH) & 0xFF), (u32)(uintptr_t)(m))
#define gsSPVertex(v, n, idx)           _GBI_W(((u32)G_VTX << 24) | ((u32)(n) << 12) | (((u32)(idx) + (u32)(n)) << 1), (u32)(uintptr_t)(v))
#define gsSP1Triangle(v0, v1, v2, flag) _GBI_W(((u32)G_TRI1 << 24) | ((u32)(v0)*2 << 16) | ((u32)(v1)*2 << 8) | ((u32)(v2)*2), 0)
#define gsSP2Triangles(v0, v1, v2, f0, v3, v4, v5, f1) _GBI_W( \
    ((u32)G_TRI2 << 24) | ((u32)(v0)*2 << 16) | ((u32)(v1)*2 << 8) | ((u32)(v2)*2), \
    ((u32)(v3)*2 << 16) | ((u32)(v4)*2 << 8) | ((u32)(v5)*2))
#define gsSP4Triangles(...)             _GBI_W0((u32)G_TRI2 << 24)
#define gsSPLoadGeometryMode(w)         _GBI_W((u32)G_GEOMETRYMODE << 24, (u32)(w))
#define gsSPSetGeometryMode(w)          _GBI_W(((u32)G_GEOMETRYMODE << 24) | 0x00FFFFFF, (u32)(w))
#define gsSPClearGeometryMode(w)        _GBI_W(((u32)G_GEOMETRYMODE << 24) | (~(u32)(w) & 0x00FFFFFF), 0)
#define gsSPTexture(sc, tc, lv, tile, on) _GBI_W( \
    ((u32)G_TEXTURE_CMD << 24) | ((u32)(lv) << 11) | ((u32)(tile) << 8) | (u32)!!(on), \
    ((u32)(sc) << 16) | ((u32)(tc) & 0xFFFF))
#define gsSPLight(l, n)                 _GBI_W0((u32)G_MOVEMEM << 24)
#define gsSPNumLights(n)                _GBI_W0((u32)G_MOVEWORD << 24)
#define gsSPLookAt(l)                   _GBI_W0((u32)G_MOVEMEM << 24)
#define gsSPFogPosition(mn, mx)         _GBI_W0(((u32)G_MOVEWORD << 24) | ((u32)G_MW_FOG << 16))
#define gsSPSegment(seg, base)          _GBI_W(((u32)G_MOVEWORD << 24) | ((u32)G_MW_SEGMENT << 16) | ((u32)(seg)*4), (u32)(uintptr_t)(base))
#define gsSPClipRatio(r)                _GBI_W0(((u32)G_MOVEWORD << 24) | ((u32)G_MW_CLIP << 16))
#define gsSPViewport(vp)                _GBI_W(((u32)G_MOVEMEM << 24) | ((u32)G_MV_VIEWPORT << 8) | 0x38, (u32)(uintptr_t)(vp))

/* DP mode constants missing from N64 SDK shim */
#define G_CD_NOISE          2
#define G_CD_BAYER          1
#define G_CD_DISABLE        0
#define G_TC_FILT           2
#define G_TC_CONV           0
#define G_TF_BILERP         2
#define G_TF_POINT          0
#define G_TF_AVERAGE        3
#define G_ZS_PRIM           0
#define G_ZS_PIXEL          1
#define G_CYC_1CYCLE        0
#define G_CYC_2CYCLE        1
#define G_CYC_COPY          2
#define G_CYC_FILL          3
#define G_PM_1PRIMITIVE     1
#define G_PM_NPRIMITIVE     0
#define G_TP_PERSP          1
#define G_TP_NONE           0
#define G_TL_LOG            1
#define G_TL_TILE           0
#define G_TD_CLAMP          0
#define G_TD_SHARPEN        1
#define G_TD_DETAIL         2
#define G_AD_PATTERN        0
#define G_AD_NOTPATTERN     1
#define G_AD_NOISE          2
#define G_AD_DISABLE        3
#define G_TL_LOD            1
#define G_CD_MAGICSQ        3
#define G_SC_NON_INTERLACE  0
#define G_AC_NONE           0
#define G_AC_THRESHOLD      1
#define G_AC_DITHER         3
#define G_TLUT_NONE         0
#define G_TLUT_RGBA16       2
#define G_TLUT_IA16         3
#define G_ON                1
#define G_OFF               0
/* G_MTX_*, G_IM_FMT_*, G_IM_SIZ_* defined in GBI section above */

/* Combiner macro constants (not meaningful on Linux) */
#define G_CC_MODULATEI       0,0,0,0,0,0,0,0
#define G_CC_MODULATEIA      0,0,0,0,0,0,0,0
#define G_CC_DECALRGB        0,0,0,0,0,0,0,0
#define G_CC_DECALRGBA       0,0,0,0,0,0,0,0
#define G_CC_BLENDI          0,0,0,0,0,0,0,0
#define G_CC_PRIMITIVE       0,0,0,0,0,0,0,0
#define G_CC_SHADE           0,0,0,0,0,0,0,0
#define G_CC_ADDRGB          0,0,0,0,0,0,0,0
#define G_CC_TRILERP         0,0,0,0,0,0,0,0
#define G_CC_INTERFERENCE    0,0,0,0,0,0,0,0

/* Render mode constants */
#define G_RM_OPA_SURF           0
#define G_RM_AA_OPA_SURF        0
#define G_RM_RA_OPA_SURF        0
#define G_RM_ZB_OPA_SURF        0
#define G_RM_AA_ZB_OPA_SURF     0
#define G_RM_AA_ZB_OPA_DECAL    0
#define G_RM_AA_ZB_OPA_INTER    0
#define G_RM_AA_ZB_TEX_EDGE     0
#define G_RM_AA_ZB_XLU_SURF     0
#define G_RM_AA_ZB_XLU_DECAL    0
#define G_RM_AA_ZB_XLU_INTER    0
#define G_RM_FOG_SHADE_A        0
#define G_RM_FOG_PRIM_A         0
#define G_RM_PASS               0
#define G_RM_NOOP               0
#define G_RM_VISCVG             0
#define G_RM_OPA_CI             0
#define G_RM_XLU_SURF           0
#define G_RM_CLD_SURF           0
#define G_RM_TEX_EDGE           0
#define G_RM_PCK_RGBA           0
#define G_RM_2                  0
#define G_RM_OPA_SURF2          0
#define G_RM_AA_OPA_SURF2       0
#define G_RM_RA_OPA_SURF2       0
#define G_RM_ZB_OPA_SURF2       0
#define G_RM_AA_ZB_OPA_SURF2    0
#define G_RM_RA_ZB_OPA_SURF2    0
#define G_RM_AA_ZB_OPA_DECAL2   0
#define G_RM_AA_ZB_OPA_INTER2   0
#define G_RM_AA_ZB_TEX_EDGE2    0
#define G_RM_AA_ZB_XLU_SURF2    0
#define G_RM_AA_ZB_XLU_DECAL2   0
#define G_RM_AA_ZB_XLU_INTER2   0
#define G_RM_FOG_SHADE_A2       0
#define G_RM_FOG_PRIM_A2        0
#define G_RM_PASS2              0
#define G_RM_NOOP2              0

/* Geometry mode flags */
#define G_ZBUFFER           0x00000001
#define G_SHADE             0x00000004
#define G_FOG               0x00010000
#define G_LIGHTING          0x00020000
#define G_TEXTURE_GEN       0x00040000
#define G_TEXTURE_GEN_LINEAR 0x00080000
#define G_LOD               0x00100000
#define G_SHADING_SMOOTH    0x00200000
#define G_CULL_FRONT        0x00000200
#define G_CULL_BACK         0x00000400
#define G_CULL_BOTH         (G_CULL_FRONT|G_CULL_BACK)
#define G_CLIPPING          0x00800000

/* Pixel packing macros */
#define GPACK_RGBA5551(r,g,b,a)  ((((r)&0x1f)<<11)|(((g)&0x1f)<<6)|(((b)&0x1f)<<1)|((a)&0x1))
#define GPACK_ZDZ(z,dz)          (((z)<<2)|(dz))

/* Light initializer macros (N64 SDK gdSPDefAmbient / gdSPDefLight equivalents) */
#define gDefAmbient(r,g,b) \
    {{{(u8)(r),(u8)(g),(u8)(b)},0,{(u8)(r),(u8)(g),(u8)(b)},0,{0,0,0},0}}
#define gDefLight(r,g,b,X,Y,Z) \
    {{{(u8)(r),(u8)(g),(u8)(b)},0,{(u8)(r),(u8)(g),(u8)(b)},0,{(s8)(X),(s8)(Y),(s8)(Z)},0}}

/* Tile numbers */
#define G_TX_RENDERTILE 0
#define G_TX_LOADTILE   7
#define G_TX_NOMIRROR   0
#define G_TX_WRAP       0
#define G_TX_MIRROR     0x1
#define G_TX_CLAMP      0x2
#define G_TX_NOMASK     0
#define G_TX_NOLOD      0

/* Texture LUT modes */
#define G_TT_NONE       0
#define G_TT_RGBA16     2
#define G_TT_IA16       3

/* Texture image fractional bit shift */
#define G_TEXTURE_IMAGE_FRAC 2

/* Texture size load-block suffixes (needed for Borg8LoadTextureBlock) */
#define G_IM_SIZ_4b_LINE_BYTES  0
#define G_IM_SIZ_8b_LINE_BYTES  0
#define G_IM_SIZ_16b_LINE_BYTES 0
#define G_IM_SIZ_32b_LINE_BYTES 0
#define G_IM_SIZ_4b_LOAD_BLOCK  G_IM_SIZ_16b
#define G_IM_SIZ_8b_LOAD_BLOCK  G_IM_SIZ_16b
#define G_IM_SIZ_16b_LOAD_BLOCK G_IM_SIZ_16b
#define G_IM_SIZ_32b_LOAD_BLOCK G_IM_SIZ_32b

/* SetOtherMode_H shift/size constants */
#define G_MDSFT_TEXTLUT   14
#define G_MDSFT_TEXTFILT  12
#define G_MDSFT_TEXTDETAIL 17
#define G_MDSFT_TEXTLOD   12
#define G_MDSFT_TEXTPERSP 19
#define G_MDSFT_CYCLETYPE 20
#define G_MDSFT_PIPELINE  23
#define G_MDSIZ_TEXTLUT   2
/* G_SETOTHERMODE_H defined in GBI section above */

/* =========================================================================
 * Math stubs (N64 gu* functions)
 * Actual implementations in os_impl.cpp using standard math.
 * ========================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

void guMtxF2L(float mf[4][4], Mtx *m);
void guMtxL2F(float mf[4][4], Mtx *m);
void guMtxIdent(Mtx *m);
void guMtxIdentF(float mf[4][4]);
void guMtxCatF(float mf[4][4], float nf[4][4], float rf[4][4]);
void guMtxCatL(Mtx *m, Mtx *n, Mtx *r);
void guOrthoF(float mf[4][4], float l, float r, float b, float t, float n, float f, float s);
void guOrtho(Mtx *m, float l, float r, float b, float t, float n, float f, float s);
void guFrustumF(float mf[4][4], float l, float r, float b, float t, float n, float f, float s);
void guFrustum(Mtx *m, float l, float r, float b, float t, float n, float f, float s);
void guPerspectiveF(float mf[4][4], u16 *perspNorm, float fovy, float aspect, float near, float far, float scale);
void guPerspective(Mtx *m, u16 *perspNorm, float fovy, float aspect, float near, float far, float scale);
void guTranslateF(float mf[4][4], float x, float y, float z);
void guTranslate(Mtx *m, float x, float y, float z);
void guScaleF(float mf[4][4], float x, float y, float z);
void guScale(Mtx *m, float x, float y, float z);
void guRotateF(float mf[4][4], float a, float x, float y, float z);
void guRotate(Mtx *m, float a, float x, float y, float z);
void guRotateRPYF(float mf[4][4], float r, float p, float y_);
void guRotateRPY(Mtx *m, float r, float p, float y_);
void guLookAtF(float mf[4][4], float xEye, float yEye, float zEye,
               float xAt, float yAt, float zAt, float xUp, float yUp, float zUp);
void guLookAt(Mtx *m, float xEye, float yEye, float zEye,
              float xAt, float yAt, float zAt, float xUp, float yUp, float zUp);
void guMtxXFMF(float mf[4][4], float x, float y, float z, float *ox, float *oy, float *oz);
void guVec3f(float x, float y, float z, void *v);
void guVec3fNormalize(float *v);
void guNormalize(float *x, float *y, float *z);
void guVec3fTransform(void *dst, Mtx *m, void *src);
void guAlign(Mtx *m, float heading, float pitch, float roll, float bank);
void guLookAtReflect(Mtx *m, LookAt *l, float xEye, float yEye, float zEye,
                     float xAt, float yAt, float zAt, float xUp, float yUp, float zUp);
void guAlignF(float mf[4][4], float heading, float pitch, float roll, float bank);
void guOrtho(Mtx *m, float l, float r, float b, float t, float n, float f, float s);
void guFrustum(Mtx *m, float l, float r, float b, float t, float n, float f, float s);
void guPerspective(Mtx *m, u16 *persp, float fovy, float aspect, float near, float far, float scale);

/* =========================================================================
 * OS function declarations
 * Implementations in platform/linux/os_impl.cpp
 * ========================================================================= */
void    osInitialize(void);
void    osUnmapTLBAll(void);

void    osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri);
void    osStartThread(OSThread *t);
void    osStopThread(OSThread *t);
void    osDestroyThread(OSThread *t);
void    osSetThreadPri(OSThread *t, OSPri pri);
OSPri   osGetThreadPri(OSThread *t);
OSId    osGetThreadId(OSThread *t);
OSThread *osGetCurrentThread(void);

void    osCreateMesgQueue(OSMesgQueue *mq, OSMesg *buf, s32 count);
s32     osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag);
s32     osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag);

void    osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg);
s32     osAfterPreNMI(void);

void    osCreatePiManager(OSPri pri, OSMesgQueue *mq, OSMesg *buf, s32 count);
s32     osPiStartDma(OSIoMesg *mb, s32 pri, s32 dir, u32 devAddr, void *vAddr, u32 nbytes, OSMesgQueue *mq);
void    osInvalDCache(void *vaddr, s32 nbytes);
void    osWritebackDCache(void *vaddr, s32 nbytes);
void    osWritebackDCacheAll(void);

/* On N64, converts KSEG0/KSEG1 address to physical.  On Linux, identity. */
static inline void *osVirtualToPhysical(void *addr) { return addr; }
static inline void *osPhysicalToVirtual(void *addr) { return addr; }

OSTime  osGetTime(void);
u32     osGetMemSize(void);

void    osViSetMode(OSViMode *mode);
void    osViBlack(u8 active);
void    osViSetYScale(float scale);
void    osViSwapBuffer(void *buffer);
void    osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount);
void    osCreateViManager(OSPri pri);
void    osViSetSpecialFeatures(u32 features);

/* osViSetSpecialFeatures flags */
#define OS_VI_DITHER_FILTER_ON   0x00010000
#define OS_VI_DITHER_FILTER_OFF  0x00020000
#define OS_VI_GAMMA_DITHER_ON    0x00040000
#define OS_VI_GAMMA_DITHER_OFF   0x00080000
#define OS_VI_GAMMA_ON           0x00100000
#define OS_VI_GAMMA_OFF          0x00200000
#define OS_VI_DIVOT_ON           0x00400000
#define OS_VI_DIVOT_OFF          0x00800000

s32     osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *data);
s32     osContStartReadData(OSMesgQueue *mq);
void    osContGetReadData(OSContPad *data);
s32     osContStartQuery(OSMesgQueue *mq);
void    osContGetQuery(OSContStatus *data);
s32     osContSetCh(u8 ch);

s32     osMotorInit(OSMesgQueue *mq, OSPfs *pfs, s32 channel);
s32     osMotorStart(OSPfs *pfs);
s32     osMotorStop(OSPfs *pfs);
s32     osPfsInitPak(OSMesgQueue *mq, OSPfs *pfs, s32 channel);
s32     osPfsFindFile(OSPfs *pfs, u8 company, u8 game, u8 *nlist, u8 *area, s32 *num);
s32     osPfsAllocateFile(OSPfs *pfs, u8 company, u8 game, u8 *note, u8 *ext, s32 length, s32 *file);
s32     osPfsReadWriteFile(OSPfs *pfs, s32 file, u8 flag, s32 offset, s32 size, u8 *data);
s32     osPfsDeleteFile(OSPfs *pfs, u8 company, u8 game, u8 *gameNote, u8 *extNote);
s32     osPfsFileState(OSPfs *pfs, s32 file, OSPfsState *state);
s32     osPfsRepairId(OSPfs *pfs);
s32     osPfsFreeBlocks(OSPfs *pfs, s32 *freeBlocks);
s32     osPfsIsPlug(OSMesgQueue *mq, u8 *pattern);

s32     osGbpakInit(OSMesgQueue *mq, OSPfs *pfs, int channel);

/* Audio stubs */
void    alHeapInit(ALHeap *hp, u8 *base, s32 len);
void   *alHeapAlloc(ALHeap *hp, s32 num, s32 size);
void    alInit(ALGlobals *globals, ALSynConfig *config);
void    alSynNew(ALSynth *s, ALHeap *hp, ALSynConfig *config);
void    alSynDelete(ALSynth *s);
void    alSynAddPlayer(ALSynth *s, ALPlayer *p);
void    alSynRemovePlayer(ALSynth *s, ALPlayer *p);
s32     alSynAllocVoice(ALSynth *s, ALVoice *v, ALVoiceConfig *c);
void    alSynFreeVoice(ALSynth *s, ALVoice *v);
void    alSynStartVoice(ALSynth *s, ALVoice *v);
void    alSynStartVoiceParams(ALSynth *s, ALVoice *v, ALWaveTable *t, f32 pitch, s16 vol, ALPan pan, u8 flag, ALMicroTime time);
void    alSynStopVoice(ALSynth *s, ALVoice *v);
void    alSynSetVol(ALSynth *s, ALVoice *v, s16 vol, ALMicroTime time);
void    alSynSetPitch(ALSynth *s, ALVoice *v, f32 pitch);
void    alSynSetPan(ALSynth *s, ALVoice *v, u8 pan);
void    alClose(ALGlobals *s);
Acmd   *alAudioFrame(Acmd *cmdList, s32 *cmdLen, s16 *buf, u32 len);
u32     osAiSetFrequency(u32 freq);
void    osAiSetNextBuffer(s16 *buf, u32 len);
u32     osAiGetLength(void);

/* udivdi3 – used by TIME_USEC / TIME_NSEC macros.
 * Return type is u32 to match the game's mathN64.h declaration. */
u32     udivdi3(u64 a, u64 b);

/* Math shims */
float   _sqrtf(float x);
float   __sinf(float x) noexcept;
float   __cosf(float x) noexcept;

#ifdef __cplusplus
}
#endif /* __cplusplus */

/* =========================================================================
 * CLEAR / COPY helpers used throughout game code
 * ========================================================================= */
#ifndef CLEAR
#define CLEAR(x)  (memset((x), 0, sizeof(*(x))))
#endif
#ifndef COPY
#define COPY(dst, src) (memcpy((dst), (src), sizeof(*(dst))))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
