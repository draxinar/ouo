#ifndef NPC_H_
#define NPC_H_

#include <stdint.h>

#include "mobile.h"

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CString CString;
__extension__ typedef struct CDataBuffer CDataBuffer;
__extension__ typedef struct PathNodeList PathNodeList;

__extension__ typedef struct CFragListNode CFragListNode;
__extension__ typedef struct CFragmentList CFragmentList;

/*
 * Sixth rung of the hierarchy: CEntity -> CResourceEntity -> CItem -> CContainer
 * -> CMobile -> CNPC (0x480 bytes, sibling of CPlayer). Constructor chain:
 * CMobile_Constructor -> CResourceMobile_Init (0x0048104C) -> CNPC_Constructor
 * (0x004617B4). Offsets 0x37C+ overlap CPlayer's layout but carry AI-specific
 * semantics.
 */
struct CNPC {
	CMobile mobile; // 0x000-0x37B

	// CResourceMobile fields (0x0048104C)
	CNPC *npcHashNext;         // 0x37C (hash by serial & 0x3F, table at 0x00698868)
	CNPC *npcHashPrev;         // 0x380
	uint32_t behaviorFlags;    // 0x384 (init 0, 0x1003F = all behaviors disabled)
	CLocation patrolTarget;    // 0x388 (wander/patrol target location, binary: WanderStep 0x004AA46D)
	CLocation desireLoc;       // 0x38E (save: "desireLoc", init -1,-1,0)
	CLocation lastDesireLoc;   // 0x394 (save: "lastDesireLoc", init -1,-1,0)
	CLocation loiterLoc;       // 0x39A (save: "loiterInfo" x,y,z)
	CLocation homeLoc;         // 0x3A0 (save: "homeLoc")
	uint8_t _npc_pad3A6[0x02]; // 0x3A6-0x3A7

	uint32_t aiState;              // 0x3A8 (current AI state, init 0xA via 0x00421300)
	uint32_t ltype;                // 0x3AC (load: "ltype", save: "stateInfo" field 1)
	uint32_t stateInfo2;           // 0x3B0 (save: "stateInfo" field 2, init 0)
	uint32_t prevAIState;          // 0x3B4 (saved by SetState: prev = current)
	uint32_t resourceTargetSerial; // 0x3B8 (init 0, serial of food/resource target)
	uint32_t actionTarget;         // 0x3BC (target serial for AI, set by CombatInitiate)
	uint32_t followObj1;           // 0x3C0 (save: "followObjs" field 1, serial)
	uint32_t followObj2;           // 0x3C4 (save: "followObjs" field 2, serial)
	uint32_t wanderSteps;          // 0x3C8 (init 1, wander countdown steps)
	uint32_t loiterData;           // 0x3CC (save: "loiterInfo" field 3, init 0)
	uint32_t wanderBurstCount;     // 0x3D0 (StartWander burst counter, init 1)
	uint32_t followObj3;           // 0x3D4 (save: "followObjs" field 3, serial)
	uint32_t npcInfo1_0;           // 0x3D8 (save: "npcInfo1" field 0, init 0xC8=200)
	uint32_t npcInfo1_1;           // 0x3DC (save: "npcInfo1" field 1, init 0)
	uint8_t lastWanderQuadrant;    // 0x3E0 (wander anti-repeat quadrant, binary: WanderStep 0x004AA46D)
	uint8_t _npc_pad3E1[0x03];     // 0x3E1-0x3E3
	uint32_t isWalking;            // 0x3E4 (walking-in-progress flag, binary: 0x004A99ED)
	uint8_t resourceType;          // 0x3E8 (resource type ID for gathering AI)
	uint8_t resourceAITarget;      // 0x3E9 (resource AI target flag)
	uint8_t resourceRate;          // 0x3EA (resource consumption rate)
	uint8_t _npc_pad3EB;           // 0x3EB
	uint32_t hungerCapacity;       // 0x3EC (init 0, HP regen formula + getHungerLevel divisor)
	uint32_t tickCount;            // 0x3F0 (save: "npcInfo1" field 2, incremented each AI tick)
	uint32_t npcInfo1_3;           // 0x3F4 (save: "npcInfo1" field 3, dword, init random 1-5)
	uint8_t _npc_pad3F8;           // 0x3F8
	uint8_t homeInfo1;             // 0x3F9 (save: "homeInfo" field 0, uint8)
	uint8_t homeInfo2;             // 0x3FA (save: "homeInfo" field 1, uint8)
	uint8_t _npc_pad3FB;           // 0x3FB
	uint32_t homeInfo3;            // 0x3FC (save: "homeInfo" field 2, init 0)
	uint32_t _npc_unk400;          // 0x400
	uint32_t _npc_unk404;          // 0x404 (init 0)
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64_cfl;          // 64-bit alignment pad
#endif
	CFragmentList *convoFragList; // 0x408 (binary: CFragmentList*, init 0)
	uint32_t npcSearchRange;      // 0x40C (init 0xFF)
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64_rtp0;         // 64-bit alignment pad
#endif
	// Resource template data pointers (0x410-0x434):
	// Allocated during template resource node processing (0x004A8E15).
	// Freed and cleared on rescan. Unused in our code (template data
	// is applied directly in CTemplateManager_CreateFromTemplate).
	void *resTplPtr0;      // 0x410 (heap ptr, freed in rescan)
	void *resTplPtr1;      // 0x414 (heap ptr, freed in rescan)
	void *resTplPtr2;      // 0x418 (heap ptr, freed in rescan)
	uint32_t resTplCount0; // 0x41C (counter, cleared in rescan)
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64_rtp3;  // 64-bit alignment pad
#endif
	void *resTplPtr3;      // 0x420 (heap ptr, freed in rescan)
	void *resTplPtr4;      // 0x424 (heap ptr, freed in rescan)
	void *resTplPtr5;      // 0x428 (heap ptr, freed in rescan)
	uint32_t resTplCount1; // 0x42C (counter, cleared in rescan)
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64_rtp6;  // 64-bit alignment pad
#endif
	void *resTplPtr6;      // 0x430 (heap ptr, freed in rescan)
	void *resTplPtr7;      // 0x434 (heap ptr, freed in rescan)
	// NPC difficulty-level doubly-linked list (0x438-0x440):
	// Global heads at g_NPCLevelList[4] (0x0069A794).
	// Managed by InsertIntoLevelList (0x004816A1), RemoveFromLevelList (0x00481728).
	CNPC *levelListPrev;         // 0x438 (prev NPC at same difficulty level)
	CNPC *levelListNext;         // 0x43C (next NPC at same difficulty level)
	uint8_t npcLevel;            // 0x440 (difficulty level 0-3, index into g_NPCLevelList)
	uint8_t _npc_pad441[0x03];   // 0x441-0x443
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64_job[4];       // 64-bit alignment pad
#endif
	char *npcJob;                // 0x444 (load: "job", binary init: static string ptr)
	char *npcTown;               // 0x448 (binary init: static string ptr)
	uint32_t npcAITarget;        // 0x44C (runtime combat target serial, SetCombatTarget 0x00483D92)
	uint32_t effectCheckCounter; // 0x450 (120-tick cycle: poison/curse cleanup, 0x004514A2)
	uint32_t npcCombatTarget;    // 0x454 (save: "npcInfo2" field 1, serial)
	uint8_t speechCounter;       // 0x458 (save: "npcInfo2" field 0, idle speech countdown)
	uint8_t aiByte2;             // 0x459 (init 0)
	uint8_t aiByte3;             // 0x45A (init 0)
	uint8_t aiDelayCounter;      // 0x45B (init 0, set by SetAIState: 4/6/10)
	uint8_t _npc_unk45C;         // 0x45C (init 0)
	uint8_t npcFlee;             // 0x45D (save: "npcflee", init 0x10=16, default=skip)
	uint16_t frozenCombatFlag;   // 0x45E (init 0, nonzero = frozen NPC in combat)
	int16_t npcFreezeTimer;      // 0x460 (init 0, flee/freeze countdown)
	uint8_t _npc_pad462[0x02];   // 0x462-0x463
	uint32_t scanTimer;          // 0x464 (AI scan timer countdown, used by CNPC_IdleScan)
#if __SIZEOF_POINTER__ == 8
	uint32_t _npc_pad64_468;     // 64-bit alignment pad
#endif

	// CResourceMobile continued
	uintptr_t pathArray;         // 0x468 (init 0, PathNode* array from A* pathfinding)
	int32_t pathSpeed;           // 0x46C (init -1, pathfinding movement speed, set by walkTo)
	uint32_t pathStepsRemaining; // 0x470 (init 0, remaining steps in path)

	// CNPC fields (0x004617B4)
	// Offset 0x474: dual-purpose field.
	// CShopkeeper uses this as restockCounter (save: "restockCounter").
	// Regular CNPC uses this as NPC list link (runtime only, not saved).
	__extension__ union {
		uint32_t restockCounter; // 0x474 (vendor: restock timer)
		CNPC *nextNPC;           // 0x474 (regular: NPC list link)
	};
	CNPC *prevNPC;             // 0x478
	__extension__ union {
		__extension__ struct {
			uint16_t npcSfx;           // 0x47C (init 0xFFFF)
			uint8_t _npc_pad47E[0x02]; // 0x47E-0x47F
		};
		CNPC *vendorNext;      // 0x47C (vendor list next link, overlaps npcSfx + pad)
	};
};
// Static assert: CNPC must be exactly 0x480 bytes
#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CNPC) == 0x480, "CNPC size mismatch");
#endif

/*
 * Path node in the heap-allocated A* path result array (8 bytes).
 * Indexed as pathArray[step].
 */
typedef struct {
	int16_t x;   // +0
	int16_t y;   // +2
	int16_t z;   // +4
	int16_t dir; // +6
} PathNode;

/*
 * Open-list node used during A* pathfinding (16 bytes). The binary
 * stacks 512 of these at [ebp - 0x2018].
 */
typedef struct {
	int16_t x;         // +0x00
	int16_t y;         // +0x02
	int16_t z;         // +0x04
	int16_t dir;       // +0x06
	int32_t parentIdx; // +0x08
	int32_t cost;      // +0x0C
} SearchNode;

/*
 * NPC AI state tags stored in CNPC.aiState. Case labels mirror
 * GetNPCStateString at 0x004828B0.
 */
enum {
	NPC_STATE_SEEK_FOOD = 0,
	NPC_STATE_SEEK_SHELTER = 1,
	NPC_STATE_PURSE_SHELTER = 2, // sic: "Purse" in binary, likely typo for "Pursue"
	NPC_STATE_SEEK_DESIRES = 3,
	NPC_STATE_PURSE_DESIRES = 4,
	NPC_STATE_EAT_FOOD = 5,
	NPC_STATE_LOITER = 6,
	NPC_STATE_RUNAWAY = 7,
	NPC_STATE_TALKING = 8,
	NPC_STATE_ATTACK_TARGET = 9,
	NPC_STATE_IDLE = 10, // 0x0A - init default
	NPC_STATE_WANDER = 11,
	NPC_STATE_SLEEP = 12,
	NPC_STATE_FOLLOWING = 13,
};

/*
 * Custom - CNPC.resourceAITarget discriminator (FEAT_ECOLOGY).
 *
 * resourceAITarget was a 0/1 flag; value NPC_RESTGT_MOBILE marks a
 * player pickpocket target, so CNPC_PurseDesiresHandler routes it to
 * the pickpocket path instead of the item-consume path. Value 2 is
 * truthy, so existing if (resourceAITarget) tests are unaffected.
 */
#define NPC_RESTGT_NONE   0
#define NPC_RESTGT_ITEM   1
#define NPC_RESTGT_MOBILE 2

/*
 * 0x00421300 - CNPC::SetState
 *
 * Save current AI state to prevAIState, set new state.
 */
static inline void
CNPC_SetState(CNPC *npc, uint32_t newState)
{
	npc->prevAIState = npc->aiState;
	npc->aiState = newState;
}

extern uint32_t g_npcAITimerReset;        // 0x0061D67C (NPC AI timer reset value)

// NPC manager active heartbeat queue (manager+0x110, deferred heartbeat serials)
__extension__ typedef struct CVector CVector;

extern CNPC *g_NPCListHead; // 0x006474D0
extern uint32_t g_NPCCount; // 0x0068B378
extern int g_NormalNPCCount; // 0x0068B3A4
extern CNPC *g_NPCHash[64]; // 0x00698868
extern CItem *g_npcMoveTargetEnt; // 0x00699A28
extern CMobile *g_nextCombatMobile; // 0x00699A48
extern uint32_t g_NPCHashCursor; // 0x0069A790
extern CNPC *g_NPCLevelList[4]; // 0x0069A794
extern CNPC *g_currentNPC; // 0x0069A7A4
extern uint32_t g_npcProcessFilter; // 0x006E7650
extern CNPC *g_NPCHashIterNext;
extern CVector g_NPCActiveList;

int CNPC_GetScavengerTag(CItem *entity); // 0x00432438
int CNPC_GetPredatorTag(CItem *entity); // 0x00432473
int CNPC_GetFlyingTag(CItem *entity); // 0x004324AE
int CNPC_GetPackingTag(CItem *entity); // 0x004324E9
void CNPC_DoWalk(CNPC *this, int mode, CLocation *loc); // 0x0045E4D7
void CGuard_Constructor(CNPC *npc); // 0x00461700
void CNPC_Constructor(CNPC *npc, uint16_t bodyType, CLocation *loc); // 0x004617B4
void CNPC_Destructor(CNPC *npc); // 0x0046188E
void CNPC_DropAtFeet_VT(CItem *self, CLocation *loc); // 0x0046197C
void CNPC_SetLocation_VT(CItem *self, CLocation *loc); // 0x00461A9C
void CNPC_Hide_VT(CItem *self); // 0x00461B3D
void CNPC_DetachSpatial_VT(CItem *self); // 0x00461B5C
void *CNPC_ScalarDelete(CNPC *npc, int flags); // 0x00461DA0
int CNPC_TestBehavior_VT(CNPC *self, int flag); // 0x00463910
void CNPC_SetBehavior_VT(CNPC *self, int flag); // 0x00463930
void CNPC_ClrBehavior_VT(CNPC *self, int flag); // 0x00463960
void CNPCMap_Init(void); // 0x00480920
void CNPCHash_Constructor(void); // 0x004809C2
void CNPCManager_Destructor(void); // 0x00480A25
CNPC *NPC_FindBySerial(uint32_t serial); // 0x00480A9B
void CNPCHash_HeartbeatTick(void); // 0x00480B7A
void CNPCHash_RegenMoveTick(void); // 0x00480C34
void CResourceMobile_SetBodyAndPlace(CMobile *mob, uint16_t bodyType, CLocation *loc); // 0x00480CD6
void CNPC_InitFields(CNPC *npc); // 0x00480CFF
void NPC_AddToHash(CNPC *npc); // 0x00480D3F
void CResourceMobile_Init(CMobile *mob); // 0x0048104C
void CResourceMobile_Constructor(CMobile *mob, uint16_t bodyType, CLocation *loc); // 0x00481127
void CResourceMobile_Destructor(CNPC *npc); // 0x00481228
void NPC_RemoveFromHash(CNPC *npc); // 0x004813F0
int CNPC_IsInLevelList(CNPC *npc); // 0x00481643
void CNPC_AddToLevelList(CNPC *npc); // 0x004816A1
void CNPC_RemoveFromLevelList(CNPC *npc); // 0x00481728
int CMobile_GetWalkZ(CItem *mob, CLocation loc); // 0x004818AA
void CMobile_DoWalk(CItem *mob, int dir, int speed); // 0x00481B73
void CMobile_NPC_RegenTick(CMobile *this); // 0x00482580
int CNPC_Heartbeat(CNPC *npc); // 0x00482733
const char *GetNPCStateString(int state); // 0x004828B0
void CNPC_AIMoveTick(CNPC *npc); // 0x00482AA1
void CNPC_PaperdollTitle_VT(CNPC *npc, CString *title); // 0x0048356D
void CNPC_SetSerial(CItem *self, uint32_t newSerial); // 0x004835C8
void CNPC_RemoveFromWorldGrid(CItem *this); // 0x00483762
void CMobile_IncrNormalNPCCount(CMobile *mob); // 0x004838B7
void CMobile_DecrNormalNPCCount(CMobile *mob); // 0x004838DB
void CResourceMobile_DropAtFeet_VT(CItem *self, CLocation *loc); // 0x00483931
void CResourceMobile_SetLocation_VT(CItem *self, CLocation *loc); // 0x00483A1E
int CNPC_GetLoyalty(CNPC *npc); // 0x00483A68
int CNPC_WalkToLocation(CItem *mob, int range, CLocation *loc, int flag); // 0x00483AC2
void CNPC_AddFragment(CNPC *npc, CString *name); // 0x00483B19
void CNPC_RemoveFragment(CNPC *npc, CString *name); // 0x00483BB0
void CNPC_SetNPCFlee(CNPC *npc, uint8_t value); // 0x00483D5B
void CNPC_SetCombatTarget(CNPC *npc, uint32_t serial); // 0x00483D92
void CNPCManager_AddToSpatialMap(CItem *entity); // 0x00483E25
void CNPCManager_RemoveFromSpatialMap(CItem *entity); // 0x00483E5D
void CNPC_SetSleeping(CNPC *npc, int wake); // 0x00483E95
int CNPC_IsFrozen(CNPC *npc); // 0x00483EE5
void CNPC_SetFrozen(CNPC *npc); // 0x00483F03
void CNPC_ClearFrozen(CNPC *npc); // 0x00483F2D
void CNPCManager_AddToActiveList(uint32_t serial); // 0x00483F4B
char *CNPC_CheckArmageddon(CNPC *npc, int flag); // 0x004841EC
void CNPC_OnPlayerEnteredRange(CMobile *npcMob, CMobile *player); // 0x004842B5
void CNPC_SetInvulnerable_VT(CMobile *mob); // 0x004842D8
void CNPC_ClearInvulnerable_VT(CMobile *mob); // 0x004842FB
void *CResourceMobile_ScalarDelete(CNPC *npc, int flags); // 0x00484350
CNPC *CNPCManager_GetIterNext(void); // 0x00484CB0
void CNPCManager_SetIterNext(CNPC *val); // 0x00484CD0
uint8_t CNPC_GetMovementType_VT(CNPC *npc); // 0x0048B596
void CNPC_SetupPath(CNPC *npc, CLocation *loc, int maxSteps); // 0x0049E0C9
double PathNode_Interpolate(double x1, double x2, double t); // 0x0049E6E2
PathNodeList *PathNodeList_Constructor(PathNodeList *this); // 0x0049E814
void CNPC_StartCombatAI(CMobile *npc, CMobile *target); // 0x004A829F
int CNPC_ShouldProcess(CNPC *npc); // 0x004A8361
int CMobile_NPC_AI_CombatStart(CMobile *this); // 0x004A956E
void CNPC_HandleStates(CNPC *npc); // 0x004A9780
void CNPC_ClearCombatTarget(CMobile *this, int notifyOldTarget); // 0x004A9E0C
void CNPC_WanderStep(CMobile *mob, int range); // 0x004AA46D
int CNPC_IsHostileTo(CMobile *npc, CMobile *target); // 0x004AAA0D
int CNPC_CanEngageTarget(CNPC *npc, CMobile *target); // 0x004AAA30
void CNPC_EngageTarget(CNPC *npc, CMobile *target); // 0x004AAA72
void CNPC_CheckEngageCombat(CNPC *npc, CMobile *target); // 0x004AAAC5
int CNPC_GetHungerLevel(CNPC *npc); // 0x004AAF1E
int CMobile_IsCreatureBody(CMobile *this); // 0x004AB24F
int CMobile_IsHumanNPC(CMobile *this); // 0x004AB310
void NPC_PackMerge(CMobile *mobA, CMobile *mobB); // 0x004AB34C
void CNPC_Loiter(CNPC *npc, int range, CLocation loc); // 0x004AB82F
void CNPC_StartWander(CNPC *npc, int steps, int returnState); // 0x004AB889
void CNPC_FollowerCombatHandler(CMobile *this); // 0x004AC2B8
void ResourceQuery_Stub(void); // 0x004AC7B7
void BroadcastAnimation(CMobile *mob, uint16_t action, uint16_t frameCount, uint16_t repeatCount, uint8_t backward, uint8_t repeat, uint8_t delay); // 0x004AC9F5
void CNPC_SetRunState(CMobile *mob, int running); // 0x004ACA3C
void CMobile_NPC_SetAIState(CMobile *mob, int state); // 0x004ACB07
int NPC_CalcDirectionSimple(CItem *target, CItem *source); // 0x004D7405
void AdvancePhaseIndex(int *phasePtr); // 0x004D7600
void Noop_4D7D1A(CPlayer *player); // 0x004D7D1A
void CNPC_SetupPath8Dir(CNPC *npc, CLocation *loc, int maxSteps);

#endif
