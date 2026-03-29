#include "globals.h"
#include "titlesplash.h"

u8 intro_music_flag=true;
u8 NOOP_flag=false;

void load_intro_music(){
  u8 abStack_18;
  s32 aiStack_14;

  gGlobals.titleSplashVars.flag = 1;
  u32 BGM = gExpPakFlag?BORG12_Intro_Exp:BORG12_Intro_NoExp;
  gGlobals.titleSplashVars.introMusic = loadBorg12(BGM);
  if (!gGlobals.titleSplashVars.introMusic || !gGlobals.titleSplashVars.introMusic->dat) {
    fprintf(stderr, "[introMusic] intro music failed to load (borg %u), skipping\n", BGM);
    return;
  }
  DCM::Add(&abStack_18,&aiStack_14,&(gGlobals.titleSplashVars.introMusic)->dat->sub,0xa5,0x80,1,-1,0);
  gGlobals.titleSplashVars.introMusicDatA = abStack_18;
  gGlobals.titleSplashVars.introMusicDatB = aiStack_14;
}

s32 appState_0(Gfx **param_1){
  u8 bVar2;

#ifdef __linux__
  /* Skip splash screen on Linux — borg scenes/textures can't render yet,
   * and the controller thread may not be feeding input to advance timers. */
  fprintf(stderr, "[appState_0] Skipping splash screen on Linux\n");
  return 1;
#endif
  while (WHANDLE->GetTail()) WHANDLE->Tick(1);
  if (intro_music_flag) {
    load_intro_music();
    intro_music_flag = false;
  }
  if (gGlobals.titleSplashVars.flag == 1) {
    bVar2 = TitleSplash::Show(param_1);
    gGlobals.titleSplashVars.flag = bVar2;
  }
  bVar2 = gGlobals.titleSplashVars.flag == 0;
  if (bVar2) NOOP_flag = true;
  if (NOOP_flag) {
    noop_intromusic();
    intro_music_flag = true;
    NOOP_flag = false;
  }
  return (s32)bVar2;
}

void noop_intromusic(){}