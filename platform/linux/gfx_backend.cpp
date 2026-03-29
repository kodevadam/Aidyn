/*
 * gfx_backend.cpp – SDL2 + OpenGL rendering backend.
 *
 * Replaces the N64 RCP (RSP + RDP) graphics pipeline.
 *
 * The game builds N64 display lists (arrays of Gfx commands) and submits
 * them via Graphics::CreateTask() / osScScheduleTask().  On Linux:
 *
 *   1. We create an SDL2 window + OpenGL 3.3 Core context.
 *   2. Each submitted OSScTask carries a Gfx* display list pointer.
 *   3. GfxBackend::SubmitFrame() walks the display list and translates
 *      each command to an OpenGL call.  (Currently a skeleton – commands
 *      are logged/ignored until the full translator is implemented.)
 *   4. SDL_GL_SwapWindow() presents the result.
 *
 * Build dependency: SDL2, OpenGL (libGL), GLEW (or glad).
 *
 * This file is compiled only on Linux (PLATFORM_LINUX guard).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* SDL2 */
#include <SDL2/SDL.h>

/* OpenGL */
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#include "../../ultra/include/2.0I/ultra64.h"
#include "../../ultra/include/2.0I/PR/sched.h"

/* =========================================================================
 * Backend state
 * ========================================================================= */
namespace GfxBackend {

static SDL_Window   *sWindow   = nullptr;
static SDL_GLContext sGLCtx    = nullptr;
static SDL_Renderer *sRenderer = nullptr;   /* used in software mode */
static int           sWidth    = 640;   /* 2× native 320 */
static int           sHeight   = 480;   /* 2× native 240 */
static bool          sRunning  = false;
static bool          sSoftware = false;     /* true = SDL2 software renderer */

/* VI (video interface) overrides from osViSetMode() */
static OSViMode *sCurrentMode  = nullptr;

/* =========================================================================
 * Init / Shutdown
 * ========================================================================= */
/* ---- Software-mode init (SDL2 renderer, no OpenGL) ---- */
static bool init_software(int width, int height) {
    fprintf(stderr, "[gfx] Creating window (software mode, no OpenGL)...\n");
    sWindow = SDL_CreateWindow(
        "Aidyn Chronicles: The First Mage",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!sWindow) {
        fprintf(stderr, "[gfx] FATAL: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    fprintf(stderr, "[gfx]   Window created OK\n");

    sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!sRenderer) {
        fprintf(stderr, "[gfx] FATAL: SDL_CreateRenderer(SOFTWARE) failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(sWindow);
        sWindow = nullptr;
        return false;
    }
    fprintf(stderr, "[gfx]   Software renderer created OK\n");

    /* Test clear + present */
    SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sRenderer);
    SDL_RenderPresent(sRenderer);
    return true;
}

/* ---- OpenGL-mode init ---- */
static bool init_opengl(int width, int height) {
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    fprintf(stderr, "[gfx] Creating window (OpenGL)...\n");

    sWindow = SDL_CreateWindow(
        "Aidyn Chronicles: The First Mage",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!sWindow) {
        fprintf(stderr, "[gfx]   SDL_CreateWindow(OPENGL) failed: %s\n", SDL_GetError());
        return false;
    }
    fprintf(stderr, "[gfx]   Window created OK\n");

    fprintf(stderr, "[gfx]   SDL_GL_CreateContext...\n");
    sGLCtx = SDL_GL_CreateContext(sWindow);
    if (!sGLCtx) {
        fprintf(stderr, "[gfx]   SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(sWindow);
        sWindow = nullptr;
        return false;
    }

    if (SDL_GL_MakeCurrent(sWindow, sGLCtx) != 0) {
        fprintf(stderr, "[gfx]   SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(sGLCtx);
        SDL_DestroyWindow(sWindow);
        sGLCtx  = nullptr;
        sWindow = nullptr;
        return false;
    }

    /* VSync */
    if (SDL_GL_SetSwapInterval(1) != 0) {
        fprintf(stderr, "[gfx]   VSync request failed: %s (continuing)\n", SDL_GetError());
    } else {
        fprintf(stderr, "[gfx]   VSync enabled\n");
    }

    /* GL state */
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    /* Test clear+swap */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(sWindow);

    const char *glVer  = (const char *)glGetString(GL_VERSION);
    const char *glslVer= (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    const char *glRend = (const char *)glGetString(GL_RENDERER);
    fprintf(stderr, "[gfx]   OpenGL %s | GLSL %s | %s\n",
            glVer  ? glVer  : "?",
            glslVer? glslVer: "?",
            glRend ? glRend : "?");
    return true;
}

bool Init(int width, int height, bool softwareMode) {
    sWidth  = width;
    sHeight = height;
    sSoftware = softwareMode;

    /* ---- SDL_Init ----
     * Only init VIDEO here.  Audio and gamecontroller subsystems are
     * initialised later by main.cpp so that --no-audio can skip them. */
    fprintf(stderr, "[gfx] SDL_Init(VIDEO)...\n");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[gfx] FATAL: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    fprintf(stderr, "[gfx]   SDL initialised OK\n");
    fprintf(stderr, "[gfx]   Video driver: %s\n", SDL_GetCurrentVideoDriver());

    bool ok = false;

    if (!sSoftware) {
        /* Try OpenGL first.  NOTE: some drivers segfault inside
         * SDL_CreateWindow when SDL_WINDOW_OPENGL is set – the only
         * workaround is --software.  If it merely *fails*, we fall
         * back to software automatically. */
        ok = init_opengl(width, height);
        if (!ok) {
            fprintf(stderr, "[gfx]   OpenGL init failed – falling back to software renderer\n");
            sSoftware = true;
        }
    }

    if (sSoftware) {
        ok = init_software(width, height);
    }

    if (!ok) {
        fprintf(stderr, "[gfx] FATAL: Could not create a window with any backend.\n");
        fprintf(stderr, "[gfx]   Hint: try SDL_VIDEODRIVER=x11 or =wayland\n");
        SDL_Quit();
        return false;
    }

    sRunning = true;
    fprintf(stderr, "[gfx] Ready (%dx%d, %s)\n", width, height,
            sSoftware ? "software" : "OpenGL");
    return true;
}

void Shutdown() {
    fprintf(stderr, "[gfx] Shutdown...\n");
    if (sRenderer) SDL_DestroyRenderer(sRenderer);
    if (sGLCtx)    SDL_GL_DeleteContext(sGLCtx);
    if (sWindow)   SDL_DestroyWindow(sWindow);
    SDL_Quit();
    sRenderer = nullptr;
    sGLCtx    = nullptr;
    sWindow   = nullptr;
    sRunning  = false;
    fprintf(stderr, "[gfx] Shutdown complete\n");
}

bool IsRunning() { return sRunning; }

/* =========================================================================
 * Event pump – call once per frame from the main thread.
 * Returns false when the user closes the window.
 * ========================================================================= */
bool PollEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            sRunning = false;
            return false;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                sWidth  = ev.window.data1;
                sHeight = ev.window.data2;
                if (!sSoftware) glViewport(0, 0, sWidth, sHeight);
            }
            break;
        default:
            break;
        }
    }
    return sRunning;
}

/* =========================================================================
 * RSP/RDP state — tracks N64 graphics state for OpenGL translation
 * ========================================================================= */
static struct {
    /* Fill / prim / env / fog colors (packed RGBA8888) */
    u32 fillColor;
    u32 fogColor;
    u32 blendColor;
    u32 primColor;
    u32 envColor;

    /* Scissor (in screen pixels, not N64 fixed-point) */
    int scissorX, scissorY, scissorW, scissorH;

    /* Geometry mode flags */
    u32 geometryMode;

    /* Cycle type (G_CYC_*) */
    u32 cycleType;

    /* N64 vertex buffer: RSP can hold 32 vertices for F3DEX2 */
    struct Vertex {
        float x, y, z, w;       /* position (after model-view transform on N64) */
        float u, v;             /* texture coords */
        u8 r, g, b, a;         /* vertex color */
    } vtxBuf[64];

    /* Matrix stack (simplified: just model-view and projection) */
    float modelview[4][4];
    float projection[4][4];
    float mvpStack[16][4][4];
    int mvpDepth;

    /* Texture state */
    struct {
        uintptr_t imgAddr;   /* from G_SETTIMG: pointer to texture data */
        u32 imgFmt;          /* G_IM_FMT_RGBA, _CI, _IA, _I */
        u32 imgSiz;          /* G_IM_SIZ_4b, _8b, _16b, _32b */
        u32 imgWidth;        /* width in pixels */
    } tex;

    uintptr_t tlutAddr;      /* palette address from G_LOADTLUT */
    u32 texType;             /* texture lookup type (G_TT_NONE, G_TT_RGBA16) */

    struct TileState {
        u32 fmt, siz, line, tmem;
        u32 uls, ult, lrs, lrt;  /* tile size in 10.2 FP */
    } tiles[8];
} sRSP;

/* Forward declarations for helpers used by texture converters */
static bool ptr_in_pool(uintptr_t addr);
static void unpack_rgba(u32 c, float *r, float *g, float *b, float *a);

/* Reusable GL texture for TEXRECT blits */
static GLuint sTexRectTex = 0;

/* Convert N64 RGBA5551 (big-endian) to RGBA8888 */
static void rgba16_to_rgba8(const u8 *src, u8 *dst, int numPixels) {
    for (int i = 0; i < numPixels; i++) {
        u16 px = (u16)(src[i*2] << 8) | src[i*2+1]; /* big-endian read */
        dst[i*4+0] = ((px >> 11) & 0x1F) * 255 / 31;
        dst[i*4+1] = ((px >>  6) & 0x1F) * 255 / 31;
        dst[i*4+2] = ((px >>  1) & 0x1F) * 255 / 31;
        dst[i*4+3] = (px & 1) ? 255 : 0;
    }
}

/* Convert N64 IA8 (4-bit I, 4-bit A) to RGBA8888 */
static void ia8_to_rgba8(const u8 *src, u8 *dst, int numPixels) {
    for (int i = 0; i < numPixels; i++) {
        u8 intensity = (src[i] >> 4) * 255 / 15;
        u8 alpha     = (src[i] & 0xF) * 255 / 15;
        dst[i*4+0] = intensity;
        dst[i*4+1] = intensity;
        dst[i*4+2] = intensity;
        dst[i*4+3] = alpha;
    }
}

/* Convert N64 I8 (8-bit intensity) to RGBA8888 */
static void i8_to_rgba8(const u8 *src, u8 *dst, int numPixels) {
    for (int i = 0; i < numPixels; i++) {
        dst[i*4+0] = src[i];
        dst[i*4+1] = src[i];
        dst[i*4+2] = src[i];
        dst[i*4+3] = 255;
    }
}

/* Convert N64 IA4 (3-bit I, 1-bit A) to RGBA8888 */
static void ia4_to_rgba8(const u8 *src, u8 *dst, int numPixels) {
    for (int i = 0; i < numPixels; i++) {
        u8 byte = src[i / 2];
        u8 nib = (i & 1) ? (byte & 0xF) : (byte >> 4);
        u8 intensity = (nib >> 1) * 255 / 7;
        u8 alpha = (nib & 1) ? 255 : 0;
        dst[i*4+0] = intensity;
        dst[i*4+1] = intensity;
        dst[i*4+2] = intensity;
        dst[i*4+3] = alpha;
    }
}

/* Convert N64 I4 (4-bit intensity) to RGBA8888 */
static void i4_to_rgba8(const u8 *src, u8 *dst, int numPixels) {
    for (int i = 0; i < numPixels; i++) {
        u8 byte = src[i / 2];
        u8 nib = (i & 1) ? (byte & 0xF) : (byte >> 4);
        u8 intensity = nib * 255 / 15;
        dst[i*4+0] = intensity;
        dst[i*4+1] = intensity;
        dst[i*4+2] = intensity;
        dst[i*4+3] = 255;
    }
}

/* Convert N64 CI8 (8-bit palette index) to RGBA8888 using RGBA16 palette */
static void ci8_to_rgba8(const u8 *src, u8 *dst, int numPixels, uintptr_t palAddr) {
    const u8 *pal = (const u8 *)palAddr;
    for (int i = 0; i < numPixels; i++) {
        u8 idx = src[i];
        if (pal && ptr_in_pool((uintptr_t)pal)) {
            u16 px = (u16)(pal[idx*2] << 8) | pal[idx*2+1];
            dst[i*4+0] = ((px >> 11) & 0x1F) * 255 / 31;
            dst[i*4+1] = ((px >>  6) & 0x1F) * 255 / 31;
            dst[i*4+2] = ((px >>  1) & 0x1F) * 255 / 31;
            dst[i*4+3] = (px & 1) ? 255 : 0;
        } else {
            dst[i*4+0] = idx; dst[i*4+1] = idx;
            dst[i*4+2] = idx; dst[i*4+3] = 255;
        }
    }
}

/* Convert N64 CI4 (4-bit palette index) to RGBA8888 using RGBA16 palette */
static void ci4_to_rgba8(const u8 *src, u8 *dst, int numPixels, uintptr_t palAddr) {
    const u8 *pal = (const u8 *)palAddr;
    for (int i = 0; i < numPixels; i++) {
        u8 byte = src[i / 2];
        u8 idx = (i & 1) ? (byte & 0xF) : (byte >> 4);
        if (pal && ptr_in_pool((uintptr_t)pal)) {
            u16 px = (u16)(pal[idx*2] << 8) | pal[idx*2+1];
            dst[i*4+0] = ((px >> 11) & 0x1F) * 255 / 31;
            dst[i*4+1] = ((px >>  6) & 0x1F) * 255 / 31;
            dst[i*4+2] = ((px >>  1) & 0x1F) * 255 / 31;
            dst[i*4+3] = (px & 1) ? 255 : 0;
        } else {
            dst[i*4+0] = idx*17; dst[i*4+1] = idx*17;
            dst[i*4+2] = idx*17; dst[i*4+3] = 255;
        }
    }
}

/* Convert N64 RGBA32 (big-endian) to RGBA8888 */
static void rgba32_to_rgba8(const u8 *src, u8 *dst, int numPixels) {
    for (int i = 0; i < numPixels; i++) {
        dst[i*4+0] = src[i*4+0];
        dst[i*4+1] = src[i*4+1];
        dst[i*4+2] = src[i*4+2];
        dst[i*4+3] = src[i*4+3];
    }
}

/* Convert N64 IA16 (8-bit I, 8-bit A) to RGBA8888 */
static void ia16_to_rgba8(const u8 *src, u8 *dst, int numPixels) {
    for (int i = 0; i < numPixels; i++) {
        u8 intensity = src[i*2];
        u8 alpha     = src[i*2+1];
        dst[i*4+0] = intensity;
        dst[i*4+1] = intensity;
        dst[i*4+2] = intensity;
        dst[i*4+3] = alpha;
    }
}

/* Draw a textured 2D rectangle using OpenGL */
static void gl_tex_rect(int xl, int yl, int xh, int yh,
                         const u8 *texData, int texW, int texH,
                         float s0, float t0, float s1, float t1) {
    if (!sTexRectTex) glGenTextures(1, &sTexRectTex);
    glBindTexture(GL_TEXTURE_2D, sTexRectTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);

    /* Map N64 screen coords (320×240) to GL NDC (-1..1) */
    float nx0 = (xl / 320.0f) * 2.0f - 1.0f;
    float ny0 = 1.0f - (yl / 240.0f) * 2.0f;
    float nx1 = (xh / 320.0f) * 2.0f - 1.0f;
    float ny1 = 1.0f - (yh / 240.0f) * 2.0f;

    /* Apply prim color modulation */
    float pr, pg, pb, pa;
    unpack_rgba(sRSP.primColor, &pr, &pg, &pb, &pa);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(pr, pg, pb, pa);
    glBegin(GL_QUADS);
    glTexCoord2f(s0, t0); glVertex2f(nx0, ny0);
    glTexCoord2f(s1, t0); glVertex2f(nx1, ny0);
    glTexCoord2f(s1, t1); glVertex2f(nx1, ny1);
    glTexCoord2f(s0, t1); glVertex2f(nx0, ny1);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

/* =========================================================================
 * Shared helpers (used by both texture converters and DL processing)
 * ========================================================================= */
static bool ptr_in_pool(uintptr_t addr) {
    return addr >= 0x40000000 && addr < 0x50000000;
}

/* Unpack RGBA8888 colour word to floats */
static void unpack_rgba(u32 c, float *r, float *g, float *b, float *a) {
    *r = ((c >> 24) & 0xFF) / 255.0f;
    *g = ((c >> 16) & 0xFF) / 255.0f;
    *b = ((c >>  8) & 0xFF) / 255.0f;
    *a = ((c >>  0) & 0xFF) / 255.0f;
}

/* =========================================================================
 * Display list processing
 *
 * F3DEX2 opcode constants are defined in ultra64.h (G_VTX, G_DL, etc.).
 * The walker reads the opcode from the top byte of each Gfx word and
 * dispatches to the appropriate handler.
 * ========================================================================= */
static unsigned sDLStats[256] = {};
static unsigned sDLFrameCount = 0;

/* Draw a filled 2D rectangle using immediate-mode GL (no shaders needed) */
static void gl_fill_rect(int x0, int y0, int x1, int y1, u32 color) {
    float r, g, b, a;
    unpack_rgba(color, &r, &g, &b, &a);

    /* Map N64 screen coords (320×240) to GL NDC (-1..1) */
    float nx0 = (x0 / 320.0f) * 2.0f - 1.0f;
    float ny0 = 1.0f - (y0 / 240.0f) * 2.0f;
    float nx1 = (x1 / 320.0f) * 2.0f - 1.0f;
    float ny1 = 1.0f - (y1 / 240.0f) * 2.0f;

    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(nx0, ny0);
    glVertex2f(nx1, ny0);
    glVertex2f(nx1, ny1);
    glVertex2f(nx0, ny1);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
}

/* Load N64 vertex data into the RSP vertex buffer */
static void load_vertices(uintptr_t addr, int numVerts, int startIdx) {
    if (!ptr_in_pool(addr)) return;
    if (startIdx + numVerts > 64) numVerts = 64 - startIdx;

    /* N64 Vtx format: 16 bytes per vertex
     * s16 x, y, z; u16 flag; s16 u, v; u8 r, g, b, a (or nx, ny, nz, a) */
    const u8 *src = reinterpret_cast<const u8 *>(addr);
    for (int i = 0; i < numVerts; i++) {
        auto &v = sRSP.vtxBuf[startIdx + i];
        /* N64 is big-endian; data was byte-swapped on load */
        const u8 *p = src + i * 16;
        s16 px = (s16)((p[0] << 8) | p[1]);
        s16 py = (s16)((p[2] << 8) | p[3]);
        s16 pz = (s16)((p[4] << 8) | p[5]);
        /* p[6..7] = flag */
        s16 pu = (s16)((p[8] << 8) | p[9]);
        s16 pv = (s16)((p[10] << 8) | p[11]);
        v.x = (float)px;
        v.y = (float)py;
        v.z = (float)pz;
        v.w = 1.0f;
        v.u = (float)pu / 32.0f;   /* 5.10 fixed point */
        v.v = (float)pv / 32.0f;
        v.r = p[12];
        v.g = p[13];
        v.b = p[14];
        v.a = p[15];
    }
}

/* Draw a triangle from the vertex buffer (vertex-colored, no textures) */
static void draw_tri(int v0, int v1, int v2) {
    auto &a = sRSP.vtxBuf[v0];
    auto &b = sRSP.vtxBuf[v1];
    auto &c = sRSP.vtxBuf[v2];

    /* Simple orthographic projection for now: scale x/y to NDC */
    auto emit = [](const decltype(sRSP.vtxBuf[0]) &vtx) {
        glColor4ub(vtx.r, vtx.g, vtx.b, vtx.a);
        /* Scale vertex positions: N64 screen coords are roughly -320..320, -240..240 */
        glVertex3f(vtx.x / 320.0f, vtx.y / 240.0f, vtx.z / 65536.0f);
    };

    glBegin(GL_TRIANGLES);
    emit(a); emit(b); emit(c);
    glEnd();
}

static void process_display_list(const Gfx *dl, int depth = 0) {
    if (!dl || depth > 16) return;
    if (!ptr_in_pool((uintptr_t)dl)) {
        fprintf(stderr, "[gfx] DL ptr %p outside pool, skipping\n", (void*)dl);
        return;
    }

    { static int dlStartLog = 0;
      if (dlStartLog < 6 && depth == 0) {
        fprintf(stderr, "[gfx] process_display_list START: dl=%p depth=%d\n", (void*)dl, depth);
        dlStartLog++;
      }
    }

    int maxCmds = 10000;
    int cmdCount = 0;
    for (; maxCmds > 0; maxCmds--) {
        u8 cmd = (u8)(dl->w.hi >> 24);
        sDLStats[cmd]++;
        cmdCount++;

        switch (cmd) {
        case G_SPNOOP:
        case G_RDPPIPESYNC:
        case G_RDPFULLSYNC:
        case G_RDPLOADSYNC:
        case G_RDPTILESYNC:
            break;

        case G_ENDDL:
            { static int dlEndLog = 0;
              if (dlEndLog < 6 && depth == 0) {
                fprintf(stderr, "[gfx] process_display_list END: dl_end=%p cmdCount=%d\n", (void*)dl, cmdCount);
                dlEndLog++;
              }
            }
            return;

        case G_DL: {
            uintptr_t addr = (uintptr_t)(u32)dl->w.lo;
            if (ptr_in_pool(addr)) {
                const Gfx *subdl = reinterpret_cast<const Gfx *>(addr);
                bool branch = (dl->w.hi >> 16) & 0x01;
                process_display_list(subdl, depth + 1);
                if (branch) return;
            }
            break;
        }

        case G_MTX:
            /* Store matrix pointer for future use; full transform chain TBD */
            break;

        case G_POPMTX:
            break;

        case G_GEOMETRYMODE: {
            u32 clearBits = dl->w.hi & 0x00FFFFFF;
            u32 setBits   = dl->w.lo;
            sRSP.geometryMode = (sRSP.geometryMode & clearBits) | setBits;
            break;
        }

        case G_VTX: {
            int numVerts = (dl->w.hi >> 12) & 0xFF;
            int startIdx = ((dl->w.hi >> 1) & 0x7F) - numVerts;
            if (startIdx < 0) startIdx = 0;
            uintptr_t addr = (uintptr_t)(u32)dl->w.lo;
            load_vertices(addr, numVerts, startIdx);
            break;
        }

        case G_TRI1: {
            int v0 = ((dl->w.hi >> 16) & 0xFF) / 2;
            int v1 = ((dl->w.hi >>  8) & 0xFF) / 2;
            int v2 = ((dl->w.hi >>  0) & 0xFF) / 2;
            draw_tri(v0, v1, v2);
            break;
        }

        case G_TRI2: {
            int v0 = ((dl->w.hi >> 16) & 0xFF) / 2;
            int v1 = ((dl->w.hi >>  8) & 0xFF) / 2;
            int v2 = ((dl->w.hi >>  0) & 0xFF) / 2;
            int v3 = ((dl->w.lo >> 16) & 0xFF) / 2;
            int v4 = ((dl->w.lo >>  8) & 0xFF) / 2;
            int v5 = ((dl->w.lo >>  0) & 0xFF) / 2;
            draw_tri(v0, v1, v2);
            draw_tri(v3, v4, v5);
            break;
        }

        case G_QUAD: {
            int v0 = ((dl->w.hi >> 16) & 0xFF) / 2;
            int v1 = ((dl->w.hi >>  8) & 0xFF) / 2;
            int v2 = ((dl->w.hi >>  0) & 0xFF) / 2;
            draw_tri(v0, v1, v2);
            break;
        }

        case G_TEXTURE_CMD:
            /* Texture scale/enable — tracked for future texture support */
            break;

        case G_SETTIMG: {
            sRSP.tex.imgFmt   = (dl->w.hi >> 21) & 0x07;
            sRSP.tex.imgSiz   = (dl->w.hi >> 19) & 0x03;
            sRSP.tex.imgWidth = (dl->w.hi & 0xFFF) + 1;
            sRSP.tex.imgAddr  = (uintptr_t)(u32)dl->w.lo;
            {
                static int sSettimgLog = 0;
                if (sSettimgLog < 200 || (u32)dl->w.lo != 0) {
                    fprintf(stderr, "[gfx] SETTIMG: addr=0x%08x fmt=%u siz=%u w=%u pool=%d\n",
                            (u32)dl->w.lo, sRSP.tex.imgFmt, sRSP.tex.imgSiz,
                            sRSP.tex.imgWidth, ptr_in_pool(sRSP.tex.imgAddr));
                    fflush(stderr);
                    if ((u32)dl->w.lo == 0) sSettimgLog++;
                }
            }
            break;
        }

        case G_SETTILE: {
            u32 tile = (dl->w.lo >> 24) & 0x07;
            sRSP.tiles[tile].fmt  = (dl->w.hi >> 21) & 0x07;
            sRSP.tiles[tile].siz  = (dl->w.hi >> 19) & 0x03;
            sRSP.tiles[tile].line = (dl->w.hi >> 9) & 0x1FF;
            sRSP.tiles[tile].tmem = dl->w.hi & 0x1FF;
            break;
        }

        case G_LOADBLOCK:
        case G_LOADTILE:
            /* Texture data loading — the actual pixel data is read from
             * sRSP.tex.imgAddr when G_TEXRECT is processed. */
            break;

        case G_SETTILESIZE: {
            u32 tile = (dl->w.lo >> 24) & 0x07;
            sRSP.tiles[tile].uls = (dl->w.hi >> 12) & 0xFFF;
            sRSP.tiles[tile].ult = (dl->w.hi >>  0) & 0xFFF;
            sRSP.tiles[tile].lrs = (dl->w.lo >> 12) & 0xFFF;
            sRSP.tiles[tile].lrt = (dl->w.lo >>  0) & 0xFFF;
            break;
        }

        case G_LOADTLUT: {
            /* Palette address stored in lo word by our fixed macro */
            uintptr_t addr = (uintptr_t)(u32)dl->w.lo;
            if (ptr_in_pool(addr))
                sRSP.tlutAddr = addr;
            break;
        }

        case G_SETCOMBINE:
            /* Combiner mode — stub until shader-based rendering */
            break;

        case G_SETOTHERMODE_L:
        case G_SETOTHERMODE_H: {
            /* Track cycle type for fill rect handling */
            u32 shift = (dl->w.hi >> 8) & 0xFF;
            if (cmd == G_SETOTHERMODE_H && shift == 20) {
                sRSP.cycleType = (dl->w.lo >> 20) & 0x03;
            }
            break;
        }

        case G_SETSCISSOR_CMD: {
            int x0 = (dl->w.hi >> 12) & 0xFFF;
            int y0 = (dl->w.hi >>  0) & 0xFFF;
            int x1 = (dl->w.lo >> 12) & 0xFFF;
            int y1 = (dl->w.lo >>  0) & 0xFFF;
            /* Fixed-point 10.2 → integer pixels */
            x0 >>= 2; y0 >>= 2; x1 >>= 2; y1 >>= 2;
            sRSP.scissorX = x0;
            sRSP.scissorY = y0;
            sRSP.scissorW = x1 - x0;
            sRSP.scissorH = y1 - y0;
            /* Map to actual window coords */
            float sx = (float)sWidth / 320.0f;
            float sy = (float)sHeight / 240.0f;
            glEnable(GL_SCISSOR_TEST);
            glScissor((int)(x0 * sx), (int)((240 - y1) * sy),
                      (int)((x1 - x0) * sx), (int)((y1 - y0) * sy));
            break;
        }

        case G_FILLRECT: {
            /* Extract coords: hi has xh/yh (right/bottom), lo has xl/yl (left/top) */
            int xh = (dl->w.hi >> 14) & 0x3FF;
            int yh = (dl->w.hi >>  2) & 0x3FF;
            int xl = (dl->w.lo >> 14) & 0x3FF;
            int yl = (dl->w.lo >>  2) & 0x3FF;
            /* In fill mode, the fill color is used directly */
            gl_fill_rect(xl, yl, xh + 1, yh + 1, sRSP.fillColor);
            break;
        }

        case G_SETFILLCOLOR:
            sRSP.fillColor = dl->w.lo;
            break;

        case G_SETFOGCOLOR:
            sRSP.fogColor = dl->w.lo;
            break;

        case G_SETBLENDCOLOR:
            sRSP.blendColor = dl->w.lo;
            break;

        case G_SETPRIMCOLOR:
            sRSP.primColor = dl->w.lo;
            break;

        case G_SETENVCOLOR:
            sRSP.envColor = dl->w.lo;
            break;

        case G_TEXRECT:
        case G_TEXRECTFLIP: {
            /* Single-entry TEXRECT (Linux simplified encoding):
             * hi: G_TEXRECT | xh[11:0]<<12 | yh[11:0]
             * lo: tile<<24 | xl[11:0]<<12 | yl[11:0] */
            int xh = (dl->w.hi >> 12) & 0xFFF;
            int yh = (dl->w.hi >>  0) & 0xFFF;
            /* u32 tile = (dl->w.lo >> 24) & 0x07; */
            int xl = (dl->w.lo >> 12) & 0xFFF;
            int yl = (dl->w.lo >>  0) & 0xFFF;

            /* Convert 10.2 fixed point to integer pixels */
            float fxl = xl / 4.0f, fyl = yl / 4.0f;
            float fxh = xh / 4.0f, fyh = yh / 4.0f;
            int texW = (int)(fxh - fxl);
            int texH = (int)(fyh - fyl);

            static int sTexRectLog = 0;
            if (sTexRectLog < 100) {
                fprintf(stderr, "[gfx] TEXRECT: xl=%.1f yl=%.1f xh=%.1f yh=%.1f w=%d h=%d "
                        "imgAddr=0x%lx fmt=%u siz=%u imgW=%u prim=0x%08x\n",
                        fxl, fyl, fxh, fyh, texW, texH,
                        (unsigned long)sRSP.tex.imgAddr, sRSP.tex.imgFmt, sRSP.tex.imgSiz,
                        sRSP.tex.imgWidth, sRSP.primColor);
                fflush(stderr);
                sTexRectLog++;
            }

            if (texW <= 0 || texH <= 0) break;
            if (texW > 1024 || texH > 1024) break;

            /* Read texture data from the image address set by G_SETTIMG */
            uintptr_t imgAddr = sRSP.tex.imgAddr;
            if (!ptr_in_pool(imgAddr)) {
                if (sTexRectLog <= 33) {
                    fprintf(stderr, "[gfx] TEXRECT: imgAddr 0x%lx NOT in pool, skipping\n",
                            (unsigned long)imgAddr);
                    sTexRectLog++;
                }
                break;
            }

            /* Determine bytes per pixel from image size format */
            int bpp = 0;
            switch (sRSP.tex.imgSiz) {
                case 0: bpp = 0; break; /* 4b — handled specially */
                case 1: bpp = 1; break; /* 8b */
                case 2: bpp = 2; break; /* 16b */
                case 3: bpp = 4; break; /* 32b */
            }

            /* Use the image width from G_SETTIMG for stride, texW for rect width */
            int srcW = (int)sRSP.tex.imgWidth;
            if (srcW <= 0) srcW = texW;

            /* Convert N64 texture data to RGBA8888 */
            int numPixels = texW * texH;
            u8 *rgba = (u8 *)calloc(numPixels * 4, 1);
            if (!rgba) break;

            const u8 *src = (const u8 *)imgAddr;
            u32 fmt = sRSP.tex.imgFmt;
            u32 siz = sRSP.tex.imgSiz;

            /* Convert row by row (source stride may differ from texW) */
            for (int row = 0; row < texH; row++) {
                const u8 *rowSrc;
                u8 *rowDst = rgba + row * texW * 4;

                if (bpp > 0)
                    rowSrc = src + row * srcW * bpp;
                else
                    rowSrc = src + row * srcW / 2; /* 4-bit */

                if (fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_16b)
                    rgba16_to_rgba8(rowSrc, rowDst, texW);
                else if (fmt == G_IM_FMT_RGBA && siz == G_IM_SIZ_32b)
                    rgba32_to_rgba8(rowSrc, rowDst, texW);
                else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_16b)
                    ia16_to_rgba8(rowSrc, rowDst, texW);
                else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_8b)
                    ia8_to_rgba8(rowSrc, rowDst, texW);
                else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_4b)
                    ia4_to_rgba8(rowSrc, rowDst, texW);
                else if (fmt == G_IM_FMT_I && siz == G_IM_SIZ_8b)
                    i8_to_rgba8(rowSrc, rowDst, texW);
                else if (fmt == G_IM_FMT_I && siz == G_IM_SIZ_4b)
                    i4_to_rgba8(rowSrc, rowDst, texW);
                else if (fmt == G_IM_FMT_CI && siz == G_IM_SIZ_8b)
                    ci8_to_rgba8(rowSrc, rowDst, texW, sRSP.tlutAddr);
                else if (fmt == G_IM_FMT_CI && siz == G_IM_SIZ_4b)
                    ci4_to_rgba8(rowSrc, rowDst, texW, sRSP.tlutAddr);
                else {
                    /* Unknown format — fill with magenta for debugging */
                    for (int p = 0; p < texW; p++) {
                        rowDst[p*4+0] = 255; rowDst[p*4+1] = 0;
                        rowDst[p*4+2] = 255; rowDst[p*4+3] = 255;
                    }
                }
            }

            /* Simple UV mapping: full texture, no S/T offset */
            gl_tex_rect((int)fxl, (int)fyl, (int)fxh, (int)fyh,
                        rgba, texW, texH, 0.0f, 0.0f, 1.0f, 1.0f);
            free(rgba);
            break;
        }

        case G_CULLDL:
        case G_BRANCH_Z:
            break;

        case G_MOVEMEM:
        case G_MOVEWORD:
            break;

        case G_SETCIMG:
        case G_SETZIMG:
            break;

        case G_SETPRIMDEPTH:
        case G_MODIFYVTX:
            break;

        default:
            break;
        }

        dl++;
    }
}

/* =========================================================================
 * StubFrame – minimal render for testing the window+GL pipeline.
 *
 * Clears to a slowly cycling dark colour and swaps.  If you see the colour
 * change, the GL context, vsync, and swap chain are all working.
 * ========================================================================= */
void StubFrame(unsigned long frameCount) {
    /* Cycle through dark blue → dark teal → dark purple very slowly */
    float t = (float)(frameCount % 600) / 600.0f;
    float r = 0.02f + 0.03f * sinf(t * 6.2832f);
    float g = 0.02f + 0.03f * sinf(t * 6.2832f + 2.094f);
    float b = 0.08f + 0.06f * sinf(t * 6.2832f + 4.189f);

    if (sSoftware) {
        SDL_SetRenderDrawColor(sRenderer,
            (Uint8)(r * 255), (Uint8)(g * 255), (Uint8)(b * 255), 255);
        SDL_RenderClear(sRenderer);
        SDL_RenderPresent(sRenderer);
    } else {
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SDL_GL_SwapWindow(sWindow);
    }
}

void SubmitFrame(OSScTask *task) {
    if (!task) return;

    if (sSoftware) {
        SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sRenderer);
        SDL_RenderPresent(sRenderer);
    } else {
        /* Reset RSP state for this frame */
        sRSP.fillColor = 0;
        sRSP.fogColor = 0;
        sRSP.blendColor = 0;
        sRSP.primColor = 0xFFFFFFFF; /* default white/opaque */
        sRSP.envColor = 0;
        sRSP.geometryMode = 0;
        sRSP.cycleType = 0;
        sRSP.scissorX = 0;
        sRSP.scissorY = 0;
        sRSP.scissorW = sWidth;
        sRSP.scissorH = sHeight;
        sRSP.tex.imgAddr = 0;
        sRSP.tex.imgFmt = 0;
        sRSP.tex.imgSiz = 0;
        sRSP.tex.imgWidth = 0;
        sRSP.tlutAddr = 0;
        sRSP.texType = 0;
        memset(sRSP.tiles, 0, sizeof(sRSP.tiles));

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        /* Walk the display list attached to the task */
        if (task->list.t.data_ptr) {
            Gfx *dlStart = (Gfx*)task->list.t.data_ptr;
            process_display_list(dlStart);
        }

        glDisable(GL_SCISSOR_TEST);

        sDLFrameCount++;
        if (sDLFrameCount <= 3 || (sDLFrameCount % 300 == 0)) {
            fprintf(stderr, "[gfx] frame %u DL stats:", sDLFrameCount);
            for (int i = 0; i < 256; i++) {
                if (sDLStats[i] > 0)
                    fprintf(stderr, " %02x:%u", i, sDLStats[i]);
            }
            fprintf(stderr, "\n");
        }
        memset(sDLStats, 0, sizeof(sDLStats));

        SDL_GL_SwapWindow(sWindow);
    }
}

/* =========================================================================
 * osVi* implementations (strong definitions override os_impl.cpp weak ones)
 * ========================================================================= */
extern "C" void osViSetMode(OSViMode *mode) {
    sCurrentMode = mode;
    /* The game sets up 320×240 or 512×240; we always render at sWidth×sHeight */
}

extern "C" void osViBlack(u8 active) {
    if (active && sWindow) {
        if (sSoftware) {
            SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
            SDL_RenderClear(sRenderer);
            SDL_RenderPresent(sRenderer);
        } else {
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(sWindow);
        }
    }
}

extern "C" void osViSetYScale(float scale) { (void)scale; }

extern "C" void osViSwapBuffer(void *buffer) {
    /* The game sometimes calls this directly; swap now. */
    (void)buffer;
    if (sWindow) {
        if (sSoftware)
            SDL_RenderPresent(sRenderer);
        else
            SDL_GL_SwapWindow(sWindow);
    }
}

} /* namespace GfxBackend */
