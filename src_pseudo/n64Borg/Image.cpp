#include "n64Borg.h"
#include "crash.h"
#include "romcopy.h"
#ifdef __linux__
#include "endian_swap.h"
static inline void swapBorgListing_img(BorgListing *l) {
    BE16S(l->Type); BE16S(l->Compression);
    BE32S(l->compressed); BE32S(l->uncompressed); BE32S(l->Offset);
}
#endif

#define FILENAME "./src/n64BorgImage.cpp"

float sImageHScale=1.0f;
float sImageVScale=1.0f;
u8 fade_texture[88]={0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0,0,0};

u8 borg8_func_b(void *param_1,void *param_2){return false;}


void borg8_func_a(Borg8Header *param_1){
  if (((param_1->dat).format == BORG8_CI8) || ((param_1->dat).format == BORG8_CI4))
    SetPointer(param_1,dat.palette);
  else (param_1->dat).palette = NULL;
  SetPointer(param_1,dat.offset);
}

void borg8_free_ofunc(Borg8Header *param_1){
  s32 iVar1 = get_memUsed();
  if (param_1->head.index == -1) HFREE(param_1,0x8f);
  else dec_borg_count(param_1->head.index);
  borg_mem[8]-= (iVar1 - get_memUsed());
  borg_count[8]--;
}

//load "Borg8" image
//@param index: index of image (see "BORG8_*" #defines)
//@returns Header of image
/* Return a valid but empty 1×1 Borg8Header so callers don't crash on NULL.
 * Each call allocates a fresh header so it can be safely freed by borg8_free. */
static Borg8Header* borg8_empty_stub(u32 index) {
    Borg8Header *stub;
    ALLOCS(stub, sizeof(Borg8Header), 36);
    if (!stub) return nullptr;
    memset(stub, 0, sizeof(Borg8Header));
    stub->head.index = -1;
    stub->dat.Width = 1;
    stub->dat.Height = 1;
    stub->dat.format = BORG8_RGBA16;
    stub->dat.offset = nullptr;
    stub->dat.palette = nullptr;
    fprintf(stderr, "[loadBorg8] returning empty stub for index %u\n", index);
    return stub;
}

Borg8Header* loadBorg8(u32 index){
  setBorgFlag();
  borgHeader *item = getBorgItem(index);
  if (!item) return borg8_empty_stub(index);

#ifdef __linux__
  /* On N64 (32-bit), loadBorg8 casts Borg1Header* to Borg8Header*.
   * On 64-bit Linux we must construct the Borg8Header manually.
   *
   * The N64 overlay between Borg1Header and Borg8Header depends on the
   * allocation layout (combined vs. separate).  Rather than replicate that
   * fragile overlay, we reconstruct Borg8 fields from the parsed Borg1Data
   * and the raw N64 data blob. */
  s16 listingType = get_borg_listing_type(index);
  if (listingType == 1) {
    Borg1Header *b1 = (Borg1Header *)item;
    if (!b1->dat) return borg8_empty_stub(index);

    u16 b1type = b1->dat->type;
    if (b1type > 8) return borg8_empty_stub(index);

    /* Log all Borg1Data fields for diagnostics */
    fprintf(stderr, "[loadBorg8] index=%u Borg1Data: type=%u flag=0x%04x W=%u H=%u lods=%u move=%u "
            "dList=%p bmp=%p pal=%p unk14=0x%x\n",
            index, b1->dat->type, b1->dat->flag, b1->dat->Width, b1->dat->Height,
            b1->dat->lods, b1->dat->move,
            (void*)b1->dat->dList, (void*)b1->dat->bmp, (void*)b1->dat->pallette,
            b1->dat->unk14);

    Borg8Header *b8;
    ALLOCS(b8, sizeof(Borg8Header), 35);
    if (!b8) return borg8_empty_stub(index);
    b8->head = *item;

    static const u16 b1_to_b8[] = {
      BORG8_RGBA16, BORG8_IA16, BORG8_CI8, BORG8_IA8,
      BORG8_I8, BORG8_CI4, BORG8_IA4, BORG8_I4, BORG8_RBGA32
    };
    b8->dat.format = (b1type < 9) ? b1_to_b8[b1type] : BORG8_RGBA16;
    b8->dat.unk06  = ((u16)b1->dat->lods << 8) | (u16)b1->dat->move;

    /* Bitmap and palette pointers.
     * On N64 overlay: palette = dList field, offset = bmp field.
     * Our parsed Borg1Data has these as host pointers. */
    b8->dat.offset  = (void *)b1->bitmapA;

    /* For CI formats, palette lives at dList position in the N64 overlay.
     * For non-CI formats, palette = NULL. */
    bool isCI = (b1type == 2 || b1type == 5); /* B1_CI8 or B1_CI4 */
    b8->dat.palette = isCI ? (u16 *)b1->dat->dList : nullptr;
    /* Fallback: if dList was NULL but pallette was set, use pallette */
    if (isCI && !b8->dat.palette && b1->dat->pallette)
      b8->dat.palette = b1->dat->pallette;

    /* Width and Height reconstruction.
     *
     * On N64 the Borg8 overlay reads Width and Height from fields that
     * overlap with the Borg1 data differently depending on the allocation
     * layout.  The Borg1Data u8 Width/Height fields (max 255) aren't enough
     * for larger textures like font atlases.
     *
     * Strategy: use the Borg1Data.flag as Width (the N64 overlay interprets
     * flag as Borg8.Width in the combined allocation layout), then compute
     * Height from the bitmap size.  If flag is 0 or unreasonable, fall back
     * to the u8 Width. */
    u16 b1flag = b1->dat->flag;
    u8  w8     = b1->dat->Width;
    u8  h8     = b1->dat->Height;

    /* Bytes per pixel for height computation */
    u32 bpp = 2; /* RGBA16 default */
    switch (b1type) {
      case 2: case 3: case 4: bpp = 1; break; /* CI8, IA8, I8 */
      case 5: case 6: case 7: bpp = 1; break; /* CI4/IA4/I4 half-byte, but stride is in pixels */
      case 8: bpp = 4; break; /* RGBA32 */
    }

    /* Determine real width: try flag first, fall back to u8 Width */
    u16 realW = (b1flag > 0 && b1flag <= 1024) ? b1flag : (u16)w8;

    /* Determine real height from bitmap byte count */
    u16 realH = (u16)h8;
    BorgListing bl;
    extern void *BorgListingPointer;
    if ((s32)index >= 0 && (s32)index < 4328 && b1->bitmapA) {
      ROMCOPYS(&bl, (void *)((uintptr_t)BorgListingPointer + index * sizeof(BorgListing) + 8),
               sizeof(BorgListing), 37);
      swapBorgListing_img(&bl);
      u32 totalData = bl.uncompressed;

      /* Compute bitmap offset: difference between bmp pointer and raw data start.
       * The raw data start = bmp - bmp_raw_offset. We can find the raw base from
       * the dList pointer: dList is at raw + dList_offset.
       * If both bmp and dList are available, rawBase = min(bmp, dList) - smallest_offset.
       *
       * Simpler: bitmap bytes = totalData - bmpOffset. Try to recover bmpOffset
       * from the pointer difference between bmp and the earliest known pointer. */
      u32 bmpOff = 0x18; /* default: 24-byte Borg1Data header */

      /* If we have both dList and bmp pointers, compute bmpOff from their difference */
      if (b1->dat->dList && b1->dat->bmp) {
        uintptr_t dListAddr = (uintptr_t)b1->dat->dList;
        uintptr_t bmpAddr   = (uintptr_t)b1->dat->bmp;
        /* rawBase = min of (dListAddr, bmpAddr) minus its offset.
         * For CI8: dList is likely at offset 24 (palette), bmp after palette. */
        if (bmpAddr > dListAddr) {
          /* dList (palette) comes first, bmp comes after */
          bmpOff = (u32)(bmpAddr - dListAddr) + 24; /* dList typically at offset 24 */
        }
      }

      u32 bitmapBytes = (totalData > bmpOff) ? totalData - bmpOff : 0;
      if (realW > 0 && bpp > 0 && bitmapBytes > 0) {
        u32 stride = realW;
        /* For 4-bit formats, 2 pixels per byte */
        if (b1type == 5 || b1type == 6 || b1type == 7)
          stride = (realW + 1) / 2;
        else
          stride = realW * bpp;

        u32 computedH = bitmapBytes / stride;
        if (computedH > 0 && computedH <= 2048) {
          realH = (u16)computedH;
        }
      }

      fprintf(stderr, "[loadBorg8] index=%u: listing uncomp=%u bmpOff=%u bitmapBytes=%u → W=%u H=%u (b1flag=%u w8=%u h8=%u)\n",
              index, totalData, bmpOff, bitmapBytes, realW, realH, b1flag, w8, h8);
    }

    b8->dat.Width  = realW;
    b8->dat.Height = realH;

    fprintf(stderr, "[loadBorg8] Borg1→Borg8: index=%u fmt=%u W=%u H=%u bmp=%p pal=%p\n",
            index, b8->dat.format, b8->dat.Width, b8->dat.Height,
            b8->dat.offset, (void*)b8->dat.palette);
    return b8;
  }
#endif

  return (Borg8Header *)item;
}


//gets called before almost every borg8 draw command
//@param gfx: display list
//@param flag: determines combine and render mode
//@param h: screen height
//@param w: screen width
//@returns display list changes
Gfx * borg8DlistInit(Gfx *gfx,u8 flag,u16 h,u16 v){
  u32 word1;
  u32 word0;
  
  sImageHScale = (h > 0) ? h / (float)SCREEN_WIDTH : 1.0f;
  sImageVScale = (v > 0) ? v / (float)SCREEN_HEIGHT : 1.0f;
  gDPPipeSync(gfx++);
  gDPSetCycleType(gfx++,G_CYC_1CYCLE);
  gDPPipelineMode(gfx++,G_PM_1PRIMITIVE);
  gDPSetColorDither(gfx++,0);
  gDPSetAlphaDither(gfx++,0);
  gDPSetTexturePersp(gfx++,0);
  gDPSetTextureLOD(gfx++,0);
  gDPSetTextureFilter(gfx++,G_TF_BILERP);
  gDPSetTextureConvert(gfx++,G_TC_FILT);
  gDPSetTextureDetail(gfx++,0);
  gDPSetCombineKey(gfx++,0);
  gSPClearGeometryMode(gfx++,0);
  gSPTexture(gfx++,0,0,0,0,0);
  if (flag & 2){gDPSetRenderMode(gfx++,G_RM_XLU_SURF2,(G_BL_CLR_MEM<<22));}
  else {gDPSetRenderMode(gfx++,G_RM_OPA_SURF2,G_RM_PASS);}
  //Set combine (solid or alpha. Most times alpha)
  if ((flag & 4) == 0) {
    //gDPSetCombineLERP();
    word0 = 0xfcffffff;
    word1 = 0xfffcf279;
  }
  else {
    //gDPSetCombineLERP();
    word0 = 0xfc119623;
    word1 = 0xff2fffff;
  }
  Gfx* g= gfx++;
  g->words.w0 = word0;
  g->words.w1 = word1;
  return gfx;
}

//Build dlist for rendering Borg8
//@param g display list
//@param borg8 image
//@param x x position
//@param y y position
//@param xOff x position offset
//@param yOff y position offset
//@param h horizontal
//@param v vertical
//@param xScale horizontal scale
//@param yScale vertical scale
//@param red red
//@param green green
//@param blue blue
//@param alpha alpha
//@returns display list change
Gfx * N64BorgImageDraw(Gfx *g,Borg8Header *borg8,float x,float y,u16 xOff,u16 yOff,u16 h,u16 v,
                      float xScale,float yScale,u8 red,u8 green,u8 blue,u8 alpha) {
  { static int entryLog = 0;
    if (entryLog < 3 && borg8 && (borg8->dat).offset && (uintptr_t)(borg8->dat).offset > 0x40000000) {
      fprintf(stderr, "[borg8draw] ENTRY: borg8=%p offset=%p W=%u H=%u fmt=%u g=%p h=%u v=%u xOff=%u yOff=%u\n",
              (void*)borg8, (borg8->dat).offset, (borg8->dat).Width, (borg8->dat).Height,
              (borg8->dat).format, (void*)g, (unsigned)h, (unsigned)v, (unsigned)xOff, (unsigned)yOff);
      fflush(stderr);
      entryLog++;
    }
  }
  u16 uVar1;
  s16 dsdx16;
  u32 uVar4;
  int fmt;
  int iVar6;
  u32 uVar7;
  u32 uVar8;
  int iVar9;
  u32 uVar12;
  u32 iters;
  int iVar14;
  u32 uVar15;
  u32 uVar16;
  u32 vVis;
  u32 uVar18;
  u32 uVar19;
  u32 uVar20;
  u32 xOff32;
  u32 uVar22;
  u32 i;
  u32 hVis;
  u32 uVar27;
  s16 sVar28;
  int iVar29;
  float fVar30;
  int iVar31;
  s32 dsdx;
  float fVar33;
  float fVar36;
  double dVar34;
  double dVar35;
  float fVar37;
  float imgXScale;
  float imgYScale;
  u32 dtdy;
  short dtdy16;
  
  imgXScale = xScale * sImageHScale;
  void*BMP = (borg8->dat).offset;
  imgYScale = yScale * sImageVScale;
  /* Guard: zero scale causes SIGFPE in 1024.0/scale → (int)inf conversion */
  if (imgXScale == 0.0f) imgXScale = 1.0f;
  if (imgYScale == 0.0f) imgYScale = 1.0f;
  hVis = (u32)h - (u32)xOff;
  fVar36 = x * sImageHScale;
  uVar1 = (borg8->dat).Width;
  { static int drawLog = 0;
    if (drawLog < 3 && BMP != nullptr && (uintptr_t)BMP > 0x40000000) {
      fprintf(stderr, "[borg8draw] FONT BMP=%p W=%u fmt=%u h=%u xOff=%u v=%u yOff=%u hVis=%u g_before=%p\n",
              BMP, (borg8->dat).Width, (borg8->dat).format,
              (unsigned)h, (unsigned)xOff, (unsigned)v, (unsigned)yOff,
              hVis, (void*)g);
      fflush(stderr);
      drawLog++;
    }
  }
  /* Guard: zero-size draws cause division underflow → skip */
  if (h <= xOff || v <= yOff || hVis == 0) return g;
  gDPPipeSync(g++);
  gDPSetPrimColor(g++,0,0,red,green,blue,alpha);
  uVar16 = (u32)yOff;
  fVar33 = 4.0f;
  vVis = (u32)v - (u32)yOff;
  if (vVis == 0) return g;
  fVar37 = y * sImageVScale * 4.0f;
  iVar29 = (int)((float)(int)hVis * imgXScale * 4.0f);
  iVar31 = (int)(fVar36 * 4.0f);
  dsdx = (int)(1024.0f / imgXScale);
  dtdy = (u32)(1024.0f / imgYScale);
  if (8 < ((borg8->dat).format - BORG8_RBGA32))
    CRASH("N64BorgImage.cpp N64BorgImageDraw","Image type was  not recognized.");
  xOff32 = (u32)xOff;
  uVar20 = (u32)xOff;
  dsdx16 = (s16)dsdx;
  dtdy16 = (s16)dtdy;
  sVar28 = (s16)iVar31;
  u16 currYoff = yOff; /* hoisted from case blocks to avoid jump-over-declaration errors */
  switch((borg8->dat).format) {
  case BORG8_RBGA32:
    if ((int)hVis < 2) fmt = 2 - hVis;
    else {
      fmt = 2 - (hVis & 1);
      if ((hVis & 1) == 0) fmt = 0;
    }
    iters = (hVis + fmt) * 4;
    uVar22 = 0x1000 / iters - 1;
    iters = vVis / uVar22;
    vVis = vVis - iters * uVar22;
    if (vVis == 0) {
      iters--;
      vVis = uVar22;
    }
    dVar35 = (double)(int)uVar22;
    i = 0;
    dVar34 = (double)(int)vVis;
    gSPSetOtherMode(g++,G_SETOTHERMODE_H,29/*?*/,2,G_TT_NONE);
    fVar33 = (float)dVar35 * imgYScale * 4.0f;
    uVar4 = (u32)sVar28;
    currYoff=yOff;
    for(i=0;i<iters;i++){
      currYoff+=uVar22;
      Borg8LoadTextureBlock(g++,BMP,G_IM_FMT_RGBA,G_IM_SIZ_32b,borg8->dat.Width,hVis,xOff,yOff,currYoff,uVar22);
      gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + fVar33), 0, 0, 0, dsdx16, dtdy16);
      fVar37+= fVar33;
    }
    Borg8LoadTextureBlock(g++,BMP,G_IM_FMT_RGBA,G_IM_SIZ_32b,borg8->dat.Width,hVis,xOff,yOff,yOff,vVis - 1);
    gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + (float)dVar34 * imgYScale * 4.0f),
     0, 0, 0, dsdx16, dtdy16);
    break;
  case BORG8_RGBA16:
  case BORG8_IA16:
    if ((int)hVis < 4) fmt = 4 - hVis;
    else {
      fmt = 4 - (hVis & 3);
      if ((hVis & 3) == 0) fmt = 0;
    }
    iters = (hVis + fmt) * 2;
    uVar22 = 0x1000 / iters - 1;
    iters = vVis / uVar22;
    vVis = vVis - iters * uVar22;
    if (vVis == 0) {
      iters = iters - 1;
      vVis = uVar22;
    }
    fVar30 = (float)(int)uVar22 * imgYScale * 4.0f;
    fmt = G_IM_FMT_IA;
    if ((borg8->dat).format == BORG8_RGBA16) {
      fmt = G_IM_FMT_RGBA;
    }
    dVar35 = (double)(int)vVis;
    gSPSetOtherMode(g++,G_SETOTHERMODE_H,29/*?*/,2,G_TT_NONE);
    uVar4 = (u32)sVar28;
    currYoff=yOff;
    for(i=0;i<iters;i++){
      currYoff+=uVar22;
      Borg8LoadTextureBlock(g++,BMP,fmt,G_IM_SIZ_16b,borg8->dat.Width,hVis,xOff,yOff,currYoff,uVar22);
      gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + fVar30), 0, 0, 0, dsdx16, dtdy16);
      fVar37+= fVar30;
    }
    Borg8LoadTextureBlock(g++,BMP,fmt,G_IM_SIZ_16b,borg8->dat.Width,hVis,xOff,yOff,yOff,vVis - 1);
    gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + (float)dVar35 * imgYScale * 4.0f),
     0, 0, 0, dsdx16, dtdy16);
    break;
  default:
    if ((int)hVis < 8) {
      fmt = 8 - hVis;
    }
    else {
      fmt = 8 - (hVis & 7);
      if ((hVis & 7) == 0) {
        fmt = 0;
      }
    }
    if ((borg8->dat).format == BORG8_CI8) iters = 0x800;
    else iters = 0x1000;
    uVar22 = iters / (hVis + fmt) - 1;
    iters = vVis / uVar22;
    vVis = vVis - iters * uVar22;
    if (vVis == 0) {
      iters = iters - 1;
      vVis = uVar22;
    }
    fVar30 = (float)(int)uVar22;
    fVar30 = fVar30 * imgYScale * 4.0f;
    if ((borg8->dat).format == BORG8_CI8) {
      fmt = G_IM_FMT_CI;
      gSPSetOtherMode(g++,G_SETOTHERMODE_H,29/*?*/,2,G_TT_RGBA16);
      gDPLoadTLUT_pal256(g++,(borg8->dat).palette);
      gDPLoadSync(g++);
    }
    else {
      fmt = G_IM_FMT_I;
      if ((borg8->dat).format == BORG8_IA8) {
        fmt = G_IM_FMT_IA;
      }
      gSPSetOtherMode(g++,G_SETOTHERMODE_H,29/*?*/,2,G_TT_NONE);
    }
    dVar35 = (double)(int)vVis;
    uVar4 = (u32)sVar28;
    currYoff=yOff;
    for(i=0;i<iters;i++){
      currYoff+=uVar22;
      Borg8LoadTextureBlock(g++,BMP,fmt,G_IM_SIZ_8b,borg8->dat.Width,hVis,xOff,yOff,currYoff,uVar22);
      gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + fVar30), 0, 0, 0, dsdx16, dtdy16);
      fVar37+= fVar30;
    }
    Borg8LoadTextureBlock(g++,BMP,fmt,G_IM_SIZ_8b,borg8->dat.Width,hVis,xOff,yOff,yOff,vVis - 1);
    gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + (float)dVar35 * imgYScale * 4.0f),
     0, 0, 0, dsdx16, dtdy16);
    { static int ci8Log = 0;
      if (ci8Log < 3 && BMP != nullptr && (uintptr_t)BMP > 0x40000000) {
        /* Check what was actually written to the display list */
        Gfx *check = g - 11; /* approximate: back up to where SETTIMG should be */
        fprintf(stderr, "[borg8draw] CI8 done: BMP=%p g_after=%p pal=%p iters=%u vVis=%u check_cmd=0x%08x check_addr=0x%08x\n",
                BMP, (void*)g, (void*)(borg8->dat).palette, iters, vVis,
                (check >= (g-20)) ? check->w.hi : 0, (check >= (g-20)) ? check->w.lo : 0);
        ci8Log++;
      }
    }
    break;
  case BORG8_CI4:
  case BORG8_IA4:
  case BORG8_I4:
    if ((int)hVis < 0x10) {
      fmt = 0x10 - hVis;
    }
    else {
      fmt = 0x10 - (hVis & 0xf);
      if ((hVis & 0xf) == 0) {
        fmt = 0;
      }
    }
    iters = hVis + fmt >> 1;
    if ((borg8->dat).format == BORG8_CI4) uVar22 = 0x800;
    else uVar22 = 0x1000;
    uVar22 = uVar22 / iters - 1;
    iters = vVis / uVar22;
    vVis = vVis - iters * uVar22;
    if (vVis == 0) {
      iters--;
      vVis = uVar22;
    }
    fVar30 = (float)(int)uVar22;
    fVar30 = fVar30 * imgYScale * 4.0f;
    if ((borg8->dat).format == BORG8_CI4) {
      fmt = G_IM_FMT_CI;
      gSPSetOtherMode(g++,G_SETOTHERMODE_H,29/*?*/,2,G_TT_RGBA16);
      gDPLoadTLUT_pal256(g++,(borg8->dat).palette);//not pal16?
      gDPLoadSync(g++);
    }
    else {
      fmt = G_IM_FMT_I;
      if ((borg8->dat).format == BORG8_IA4) {
        fmt = G_IM_FMT_IA;
      }
      gSPSetOtherMode(g++,G_SETOTHERMODE_H,29/*?*/,2,G_TT_NONE);
    }
    iVar6 = xOff - 1;
    dVar35 = (double)(int)vVis;
    uVar4 = (u32)sVar28;
    currYoff=yOff;
    for(i=0;i<iters;i++){
      currYoff+=uVar22;
      Borg8LoadTextureBlock(g++,BMP,fmt,G_IM_SIZ_4b,borg8->dat.Width>>1,hVis,xOff,yOff,currYoff,uVar22);
      gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + fVar30), 0, 0, 0, dsdx16, dtdy16);
      fVar37+= fVar30;
    }
    Borg8LoadTextureBlock(g++,BMP,fmt,G_IM_SIZ_4b,borg8->dat.Width>>1,hVis,xOff,yOff,yOff,vVis - 1);
    gSPScisTextureRectangle(g++, sVar28, fVar37, (iVar31 + iVar29), (fVar37 + (float)dVar35 * imgYScale * 4.0f),
     0, 0, 0, dsdx16, dtdy16);
     break;
  }
  return g;
}


//simplified wrapper for N64BorgImageDraw()
//@param gfx: display list
//@param borg8: image
//@param x: x position
//@param x: x position
//@param Hscale: horizontal scale
//@param Vscale: vertical scale
//@param R: red
//@param G: green
//@param B: blue
//@param A: alpha
//@returns display list change
Gfx* Borg8_DrawSimple(Gfx*g,Borg8Header *borg8,float x,float y,float Hscale,
                   float Vscale,u8 R,u8 G,u8 B,u8 A){
  return 
    N64BorgImageDraw(g,borg8,x,y,0,0,(borg8->dat).Width,(borg8->dat).Height,Hscale,Vscale,R,G,B,A);
}

void borg8_free(Borg8Header *param_1){
  s32 iVar1 = get_memUsed();
  if ((param_1->head).index == -1) HFREE(param_1,0x24f);
  else dec_borg_count((param_1->head).index);
  borg_mem[8]-= (iVar1 - get_memUsed());
  borg_count[8]--;
}
//another function to draw a rectangle
//@param gfx: display list
//@param x: x position
//@param x: x position
//@param H: height
//@param W: width
//@param R: red
//@param G: green
//@param B: blue
//@param A: alpha
//@returns display list changes
Gfx * DrawRectangle(Gfx *gfx,u16 x,u16 y,u16 H,u16 V,u8 R,u8 G,u8 B,u8 A){
  int sicsH;
  int sicsY;
  int sicsV;
  int dsdx;
  int sicsX;
  u32 dtdy;
  

  gDPLoadSync(gfx++);
  gDPSetPrimColor(gfx++,0,0,R,G,B,A);
  gDPSetTextureLUT(gfx++,0);
  gDPLoadTextureBlock(gfx++,fade_texture,G_IM_FMT_I,G_IM_SIZ_4b,8,8,0,0,2,2,0,0,0);
  sicsX = (x * sImageHScale);
  sicsY = (y * sImageVScale);
  sicsH = ((float)H * sImageHScale);
  sicsV = ((float)V * sImageVScale);
  dsdx = ((8.0f / (float)(sicsH - sicsX)) * 1024.0f);
  dtdy = ((8.0f / (float)(sicsV - sicsY)) * 1024.0f);
  gSPScisTextureRectangle(gfx++,sicsX,sicsY,sicsH,sicsV,0,0,0,dsdx,dtdy);
  return gfx;
}

