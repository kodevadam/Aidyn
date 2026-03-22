/*
 * stubs.cpp — Linux stub definitions for N64 ROM data globals and
 * unimplemented game functions.  All ROM-sourced data is zero/null
 * here; real content would come from a ROM extraction step.
 *
 * IMPORTANT: include only headers that do NOT define global variables.
 * Use forward declarations for all pointer-only types to avoid pulling
 * in headers that define globals (n64Borg.h, dialoug.h, entity.h, etc.)
 */

/* itemID.h → typedefs.h → ultra64.h  (Mtx, LookAt, OSThread, u8/u16/u32…) */
#include "itemID.h"              /* ArrayHeader — safe */
#include "eventFlag.h"           /* GameStateFunnel — safe */
#include "borg/borg9/b9voxel.h"  /* monsterpartyEntry — safe */

/* ---- Forward-declare pointer-only types ---- */
struct mapDataList;
struct EntityDB;
struct ChestDB;
struct SpellDB;
struct Borg8Header;
struct voxelObject;
struct playerData;
struct Borg9Data;
struct CombatSubstructA;
struct CombatSubstructB;
struct CombatEntity;
struct Borg11Header;
class  BaseWidget;

/* ---- ROM data globals ---- */

u8  CheatStrings[1]         = {0};
u8  armorDB[1]              = {0};
u8  entitydb[1]             = {0};
u8  spelldb[1]              = {0};
u8  cinematic_text[1]       = {0};
u8  cinematic_text_dat[1]   = {0};
u8  lensflare_bss           = 0;

u16 gDebugFlag              = 0;
u16 gLoadedMapIndecies[22][30][3] = {};
u16 light_count             = 0;

u32 gamestate_cheats1       = 0;
u32 gamestate_cheats2       = 0;
u32 bitfeild_array[1]       = {0};
u32 copyrightStrings        = 0; /* TODO: this is u32 not void*, needs special handling - 0xb1ff8f30 */

void* audiokey_rom          = (void*)0x11FF5300;
/* N64 ROM symbols – these were linker-placed in ROM, so &symbol gave the
 * ROM address.  On Linux the game code passes these directly (after fixing
 * seed.cpp to drop the &).  Values are N64 physical addresses; DmaRead
 * strips PI_ROM_BASE (0x10000000) to get file offsets. */
void* borg_files            = (void*)0x100F5DA0; /* ROM offset 0x0F5DA0 */
void* borg_listings         = (void*)0x11F98790; /* ROM offset 0x1F98790 */
void* cinematic_titles      = (void*)0x11FFB140;
void* combat_romstrings     = (void*)0x11FFC4D0;
void* common_string_array   = (void*)0x11FF6700;
void* dialouge_entity       = (void*)0x11FE3CE0;
void* gameStatemod_dat      = (void*)0x11FE4060;
void* itemDB                = (void*)0x11FDB5E0;
void* journal_ROM           = (void*)0x11FF90B0;
void* romstring_controller  = (void*)0x11FFC0C0;
void* romstring_credits     = (void*)0x11FFD330;
void* romstring_items       = (void*)0x11FDD7F0;
void* romstring_potiondetails = (void*)0x11FE04E0;
void* romstring_skills      = (void*)0x11FDFD50;
void* romstring_spells      = (void*)0x11FDE9B0;
void* romstring_stats       = (void*)0x11FDE6C0;
void* weapondb              = nullptr;
void* RomstringPotion       = (void*)0x11FFDA20;

mapDataList*     MapDataList_pointer = nullptr;
EntityDB*        gEntityDB           = nullptr;
ChestDB*         gChestDBp           = nullptr;
SpellDB*         gSpellDBp           = nullptr;

Borg8Header*     gCloudBorg8Base[3]  = {};
Borg8Header*     sSkyObjBss[3]       = {};

ArrayHeader      chestdb             = {};
ArrayHeader      shopdb              = {};

GameStateFunnel  gamestatefunnel_rom = {};

char**           debug_state_labels  = nullptr;
char**           bool_labels         = nullptr;
char**           on_off_labels       = nullptr;

/* CrashBuff = typedef u16[300][400] — avoid crash.h (pulls in OSThread structs) */
u16 crash_framebuffer[300][400] = {};

/* Flycam_entry layout: u32 + 8×u16 = 20 bytes */
u8 gFlycamSequences[20] = {};

/* monsterpartyEntry = ItemID(u16) + u8 min + u8 max = 4 bytes */
monsterpartyEntry globals_rom[1] = {};

/* ---- NOOP / stub functions ---- */

void NOOP_8005d704(s16){}
void NOOP_80072228(CombatSubstructB*){}

/* event_flag_skill_ — called with skill level, no-op stub */
void event_flag_skill_(s8){}

/* Minimap stub implementations */
void Minimap_Save(u8*){}
void Minimap_Load(u8*){}

/* Dialog button stubs */
BaseWidget* Dialoug_AButton(BaseWidget*, BaseWidget*){return nullptr;}
BaseWidget* Dialoug_BButton(BaseWidget*, BaseWidget*){return nullptr;}
BaseWidget* Dialoug_LeftButton(BaseWidget*, BaseWidget*){return nullptr;}
BaseWidget* Dialoug_RightButton(BaseWidget*, BaseWidget*){return nullptr;}

/* Intro menu right button stub */
BaseWidget* IntroMenu_RightFunc(BaseWidget*, BaseWidget*){return nullptr;}

/* Armor menu stub */
void makeArmorMenu(u8){}

/* Borg11 loader stub — returns null; real impl would load from ROM */
Borg11Header* loadBorg11(u32){return nullptr;}

/* Voxel object check stubs */
u8 monsterparty_obj_check(voxelObject*, playerData*){return 0;}
u8 some_monster_check(voxelObject*, Borg9Data*){return 0;}
u8 some_trigger_check(voxelObject*, Borg9Data*){return 0;}
u8 savepoint_appear_check(voxelObject*, Borg9Data*){return 0;}

/* Combat substruct stubs */
u32 FUN_80071ec4(CombatSubstructA*, u8, u8, u8, u8, u8, u8(*)[2]){return 0;}
u32 FUN_800720f8(CombatSubstructA*, u8, u8, u8, u8){return 0;}
void FUN_8006f8d8(CombatEntity*, u16, u8){}
void clear_substruct2_arrayB(CombatSubstructB*){}

/* Debug teleport/form stubs */
void DebugChangeForm(BaseWidget*){}
void DebugTeleport(BaseWidget*){}

/* N64 OS stub */
OSThread* __osGetCurrFaultedThread(){return nullptr;}

/* guLookAtReflect — N64 matrix function stub */
void guLookAtReflect(Mtx*, LookAt*, float, float, float, float, float, float, float, float, float){}
