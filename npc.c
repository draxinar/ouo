/*
 * CNPC - non-player mobile AI.
 *
 * Idle scan, pathing, door interaction, combat target selection, and
 * ecology state (predator / prey / scavenger transitions, pack behaviour).
 * Wires per-template Wombat triggers into the AI tick.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "combat.h"
#include "container.h"
#include "convo.h"
#include "defcon.h"
#include "egg.h"
#include "feature.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "shopkeeper.h"
#include "taglist.h"
#include "template.h"
#include "timer.h"
#include "utils.h"
#include "vtable.h"
#include "weather.h"
#include "wombat_compile.h"
#include "world.h"

static int CNPC_IsAversionTarget(CItem *self, CItem *target); // 0x00432300
static int CNPC_IsPredatorTarget(CItem *self, CItem *target); // 0x004323A5
static int CNPC_CanPackTarget(CItem *self, CItem *target); // 0x00432401
static void CNPC_ScanForTargets(CItem *self, int range, int isPredator, int isFlying, int isPacking); // 0x00432524
static void CNPC_ScavengerPickup(CItem *self); // 0x00432831
static void CNPC_IdleScan(CItem *self); // 0x00432997
static void CNPC_EcologyTick(CItem *self); // Custom
static void CNPC_PreyFleeScan(CItem *self, int range, int fodderType); // Custom
static void CNPC_DepositScavengedAtShelter(CItem *self); // Custom
static void CNPC_DepositContainerAt(CItem *self, CLocation *loc, int taggedOnly); // Custom
static int CNPC_CarryingHoard(CNPC *npc); // Custom
static void CNPC_StashHoardPile(CNPC *npc, CItem *pile, uint16_t body); // Custom
static void CNPC_HoardReturnHome(CNPC *npc); // Custom
static void CNPC_SeekShelterHandler(CNPC *npc); // Custom
static void CNPC_SeekDesiresHandler(CNPC *npc); // Custom
static void CNPC_PurseDesiresHandler(CNPC *npc); // Custom
static void CNPC_PurseDesiresPlayer(CNPC *npc, CItem *target); // Custom
static int CNPC_HasFoodAppetite(CNPC *npc); // Custom
static int CNPC_GrazeHandler(CNPC *npc); // Custom
static int CNPC_CarnivoreFeed(CNPC *npc); // Custom
static int CNPC_ForageLeash(CNPC *npc); // Custom
static int CNPC_GetPowerLevel(CItem *entity); // 0x00432C65
#ifndef CUSTOM_ECOLOGY_DEBUG
__attribute__((unused))
#endif
static void NPC_DebugState(CItem *entity); // 0x00432D82
static int CNPC_SfxCheck(CNPC *npc); // 0x00461CC0
static void StaticInit_NPCHash(void); // 0x00467758
static int DirFromDelta(int dx, int dy); // 0x00481FC1
static int CNPC_WalkToPatrolTarget(CNPC *npc, int arg1, int arg2); // 0x00482009
static void NPC_PathWalk(CNPC *npc); // 0x004822FD
static int CNPC_ReturnToSpawnIfFrozen(CNPC *npc); // 0x004826B2
static int CNPC_WalkToward(CNPC *npc, CLocation *loc); // 0x0048296D
static void NPC_AIMovePre(CNPC *npc); // 0x00482A9C
static int CNPC_ShouldFlee(CNPC *npc); // 0x00483CC5
static int CNPC_CanFlee(CNPC *npc); // 0x00483D20
static void CNPC_SetEffectCheckCounter(CNPC *npc, uint32_t serial); // 0x00483DAB
static void CNPC_UpdateCombatInfo(CNPC *npc); // 0x00483DE8
static void CNPCManager_DrainActiveList(void); // 0x00483F6A
__extension__ typedef struct CNPCManagerBlock CNPCManagerBlock;
static void *CNPCManagerBlock_ScalarDelete(CNPCManagerBlock *this, int flags); // 0x00484320
static void *StdPtrList_ScalarDelete_NPC(StdPtrList *this, int flags); // 0x00484380
static int CNPCManager_GetCountMinus4(StdPtrList *this); // 0x00484400
static void CNPCManagerBlock_Destructor(CNPCManagerBlock *this); // 0x00484560
static int Path_AtStep(PathNode *step, CLocation *loc, int dir); // 0x00484C50
static void Path_GetCurrentStep(CNPC *npc, PathNode *out); // 0x0049DF20
static void Path_Discard(CNPC *npc);
static void Path_Install(CNPC *npc, PathNode *path, uint32_t stepsRemaining);
static int Path_IsCurrent(CNPC *npc, uintptr_t path, uint32_t generation);
static void Path_AdvanceStep(CNPC *npc); // 0x0049DF51
static int Path_StepCheck(CNPC *npc, PathNode *node, int dir, PathNode *result); // 0x0049DFA4
static StdPtrNode **SearchNode_IterConstructor(StdPtrNode **self); // 0x0049E590
static double PathNode_ComputeInterp(double x); // 0x0049E690
static void PathNode_SetFromLoc(PathNode *this, CLocation *loc, int16_t dir); // 0x0049E650
static PathNode *PathNode_Copy(PathNode *this, PathNode *src); // 0x0049E600
static PathNode *PathNode_InitFromLoc(PathNode *this, CLocation *loc, int16_t dir); // 0x0049E5B0
static int CNPC_IsTargetNotHidden(CNPC *npc, CItem *target); // 0x004A81B0
static int CNPC_CanSeeTarget(CNPC *npc, CItem *target); // 0x004A81D8
static void CNPC_PackBehaviorScan(CNPC *npc); // 0x004A8C73
static void CNPC_InitFromResourceNodes(CNPC *npc); // 0x004A8E15
static void CNPC_CombatPatrol(CNPC *npc); // 0x004A9676
static void CNPC_PostInitBehaviorCheck(CNPC *npc); // 0x004A974D
static void CNPC_CombatChase(CNPC *npc); // 0x004A9E8E
static int CNPC_IsPackCompatible(CItem *self, CItem *target); // 0x004AA062
static int CNPC_FindFoodInPack(CMobile *mob, CMobile *feeder); // 0x004AA1B2
static int CNPC_CheckFoodNearby(CMobile *mob); // 0x004AA1E3
static CMobile *CNPC_FindHungriestFollower(CNPC *npc); // 0x004AA292
static int CNPC_TryEatFood(CNPC *npc); // 0x004AA330
static void CNPC_FoodSeek(CNPC *npc); // 0x004AA3EB
static void CNPC_WanderTick(CNPC *npc); // 0x004AA572
static void CNPC_FoodTransition(CNPC *npc); // 0x004AA678
static void CNPC_FindNearestPrey(CNPC *self, int startLevel, int actionType); // 0x004AA7A3
static void CNPC_ScanAndEngageNearest(CNPC *npc); // 0x004AAB0E
static void CNPC_RunawayTick(CNPC *npc); // 0x004AAC3A
static void CNPC_EnterWanderState(CNPC *npc, int minSteps, int maxSteps); // 0x004AB213
static int CNPC_EatFood(CNPC *npc, CItem *food); // 0x004AB60E
static int CNPC_ConsumeFood(CNPC *npc, CItem *food); // 0x004AB702
static void CNPC_WanderCountdown(CNPC *npc); // 0x004AB8EE
static void CNPC_ResourceWander(CNPC *npc); // 0x004AB938
static int CNPC_RelocateToSpawn(CNPC *npc, CLocation *spawnLoc); // 0x004ABCC8
static void CNPC_ResourcePackScan(CNPC *npc); // 0x004ABD40
static void CNPC_ResourceWanderPost(CNPC *npc); // 0x004ABE66
static void CNPC_PurseShelterHandler(CNPC *npc); // 0x004ABEFE
static int CNPC_CompareFightStrength(CNPC *npc, CMobile *target); // 0x004AC0E3
static void NPC_AddToFollowerTarget(CMobile *target); // 0x004AC248
static int CNPC_CheckPetHunger(CNPC *npc); // 0x004AC69B
static void CNPC_HandleCorpseEat(CNPC *npc, CItem *corpse); // 0x004AC7BC
static void CNPC_WalkAnimDispatch(CMobile *mob); // 0x004AC9C0

// Local forward declarations

/*
 * Per-NPC manager scratch block (0x20 bytes on 32-bit). Only vecPtrList is
 * accessed by the surviving code; the leading seven dwords model unknown
 * binary fields so the layout matches.
 */
__extension__ typedef struct CNPCManagerBlock CNPCManagerBlock;
struct CNPCManagerBlock {
	uint32_t fields[7]; // +0x00
	void *vecPtrList;   // +0x1C
};

// PathNodeList defined in weather.h (binary 0x0C-byte linked list header).

#define NPC_COMBAT_AI_MAX_DEPTH 32

// Global NPC list head (0x006474D0 in binary).
CNPC *g_NPCListHead;

// 0x00647CD4 - tracks last-constructed NPC
static CNPC *g_NPCListTail;

// 0x0069A794 - Per-difficulty-level NPC list heads (4 levels: 0-3).
CNPC *g_NPCLevelList[4];

// 0x00698868 - NPC hash table: 64 buckets, keyed by serial & 0x3F.
CNPC *g_NPCHash[64];

// 0x0068B378 - Global NPC count (incremented on create, decremented on destroy).
uint32_t g_NPCCount;

/*
 * 0x0068B3A4 - Non-invulnerable NPC count. Incremented/decremented by
 * 0x004838B7/0x004838DB which check CMobile_IsInvulnerable before modifying.
 * Used by CDefcon_IsFull (0x00436BDB) to gate creature spawns in TrySpawn.
 */
int g_NormalNPCCount;

CNPC *g_currentNPC;             // 0x0069A7A4
uint32_t g_NPCHashCursor;       // 0x0069A790
CMobile *g_nextCombatMobile;    // 0x00699A48
CNPC *g_NPCHashIterNext;        // NPC manager+0x108 (iteration scratch)
CVector g_NPCActiveList;        // NPC manager+0x110 (deferred heartbeat queue)
uint32_t g_npcProcessFilter;    // 0x006E7650 (NPC food/resource processing filter)
CItem *g_npcMoveTargetEnt;      // 0x00699A28 (debug: last NPC resource target)

// Ecology prey-finding result arrays (32 entries max).
static int g_EcoPreyAction[32];  // 0x006DA9A8 - action type per entry
static int g_EcoPreyResType[32]; // 0x006DC1B0 - resource type per entry
static CNPC *g_EcoPreyTarget[32]; // 0x006DB7D8 - target NPC per entry
static int g_EcoPreyLevel[32];   // 0x006DAF58 - level index per entry
static int g_EcoPreyCount;       // 0x006DC230 - current entry count

// 0x006DC248 - Reentrancy guard for FollowerCombatHandler.
static int g_followerCombatGuard;
// 0x006DC240 - Number of entries in g_followerTargetArray.
static int g_followerTargetCount;
// 0x006DC244 - Number of entries in g_followerEntityArray.
static int g_followerEntityCount;
// 0x006DB3D8 - Target array (CMobile pointers to attackers and their followers).
static CMobile *g_followerTargetArray[0x100];
// 0x006DB860 - Entity array (this NPC and its followers).
static CMobile *g_followerEntityArray[0x100];
// 0x006DAA28 - Best entity index for each target (-1 = unassigned).
static int g_followerBestEntity[0x100];
// 0x006DAFD8 - Best distance for each target (0x7FFFFFFF = infinity).
static int g_followerBestDist[0x100];
// 0x006DAE58 - Flag array: 1 if entity has been assigned a target.
static uint8_t g_followerFlagged[0x100];

/*
 * 0x00432300 - CNPC::IsAversionTarget
 *
 * True when the target exceeds the "aversionPower" ratio or falls
 * below the "aversionRepute" notoriety threshold on self.
 */
static int
CNPC_IsAversionTarget(CItem *self, CItem *target)
{
	int aversionPower;
	int selfPower, targetPower;
	int ratio;
	int aversionRepute;
	int notoriety;

	if (CResourceEntity_HasTag(self, "aversionPower", 0)) {
		CResourceEntity_GetTagInt(self, "aversionPower", &aversionPower);
		selfPower = CNPC_GetPowerLevel(self);
		targetPower = CNPC_GetPowerLevel(target);
		ratio = targetPower * 100 / selfPower;
		if (ratio > aversionPower)
			return 1;
	}
	if (CResourceEntity_HasTag(self, "aversionRepute", 0)) {
		CResourceEntity_GetTagInt(self, "aversionRepute", &aversionRepute);
		notoriety = CMobile_GetNotoriety((CMobile *)target);
		if (notoriety < aversionRepute)
			return 1;
	}
	return 0;
}

/*
 * 0x004323A5 - CNPC::IsPredatorTarget
 *
 * True when self carries a "predator" tag whose value matches the
 * target's "foddertype" tag.
 */
static int
CNPC_IsPredatorTarget(CItem *self, CItem *target)
{
	int predatorTag;
	int fodderType;

	predatorTag = CNPC_GetPredatorTag(self);
	fodderType = 0;
	if (predatorTag == 0)
		return 0;
	if (!CResourceEntity_HasTag(target, "foddertype", 0))
		return 0;
	CResourceEntity_GetTagInt(target, "foddertype", &fodderType);
	if (predatorTag != fodderType)
		return 0;
	return 1;
}

/*
 * 0x00432401 - CNPC::CanPackTarget
 *
 * True when the target is an NPC and pack-compatible with self.
 */
static int
CNPC_CanPackTarget(CItem *self, CItem *target)
{
	if (!VT_IsNPC(target))
		return 0;
	return CNPC_IsPackCompatible(self, target);
}

/*
 * 0x00432438 - CNPC::GetScavengerTag
 *
 * Returns the integer value of the "scavenger" ObjVar tag, or 0 if absent.
 */
int
CNPC_GetScavengerTag(CItem *entity)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(entity, "scavenger", 0))
		CResourceEntity_GetTagInt(entity, "scavenger", &val);
	return val;
}

/*
 * 0x00432473 - CNPC::GetPredatorTag
 *
 * Returns the integer value of the "predator" ObjVar tag, or 0 if absent.
 */
int
CNPC_GetPredatorTag(CItem *entity)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(entity, "predator", 0))
		CResourceEntity_GetTagInt(entity, "predator", &val);
	return val;
}

/*
 * 0x004324AE - CNPC::GetFlyingTag
 *
 * Returns the integer value of the "flying" ObjVar tag, or 0 if absent.
 */
int
CNPC_GetFlyingTag(CItem *entity)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(entity, "flying", 0))
		CResourceEntity_GetTagInt(entity, "flying", &val);
	return val;
}

/*
 * 0x004324E9 - CNPC::GetPackingTag
 *
 * Returns the integer value of the "packing" ObjVar tag, or 0 if absent.
 */
int
CNPC_GetPackingTag(CItem *entity)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(entity, "packing", 0))
		CResourceEntity_GetTagInt(entity, "packing", &val);
	return val;
}

/*
 * 0x00432524 - CNPC::ScanForTargets
 *
 * Queries NPC and item maps in range, evaluates each entity as
 * predator / aversion / pack target, then picks one and dispatches
 * combat, pack-merge, or flee.
 *
 * FIXED: the binary relied on C++ RAII to construct/destruct the local
 * CList. The C version must call CList_Constructor at entry and CList_Clear
 * on every return; without it the uninitialized CList.head crashed in
 * free(). Never noticed in the binary because this function was dead
 * code (IdleScan was never called).
 *
 * FIXED: the binary dereferences the result of CWorld_FindBySerial
 * without a NULL check, so a stale serial in the spatial map crashes
 * the vtable load. Skip NULL entities, matching the pattern in every
 * other CWorld_FindBySerial caller in this file (and in the Custom
 * CNPC_PreyFleeScan helper). Also dead-code in the binary.
 */
static void
CNPC_ScanForTargets(CItem *self, int range, int isPredator, int isFlying, int isPacking)
{
	struct {
		CItem *entity;
		int type;
	} targets[10];
	int targetCount;
	int foundTarget;
	CList resultList;
	CListNode *node;
	CItem *entity;
	int chosenIdx;
	int actionType;
	CItem *packTarget;
	int dx, dy;
	int targetPower;

	targetCount = 0;
	foundTarget = 0;

	CList_Constructor(&resultList);

	CEntityMap_RangeQueryToList(g_NPCMap, &resultList, (int)(int16_t)self->resourceEntity.entity.location.x, (int)(int16_t)self->resourceEntity.entity.location.y, range);
	CEntityMap_RangeQueryToList(g_ItemMap, &resultList, (int)(int16_t)self->resourceEntity.entity.location.x, (int)(int16_t)self->resourceEntity.entity.location.y, range);

	for (node = resultList.head; node != NULL; node = node->next) {
		entity = CWorld_FindBySerial(g_World, node->value);

		if (entity == NULL)
			continue;

		if (VT_IsHidden(entity))
			continue;

		if (entity == self)
			continue;

		if (isPredator) {
			if (CNPC_IsPredatorTarget(self, entity)) {
				targets[targetCount].entity = entity;
				targets[targetCount].type = 0;
				targetCount++;
				if (targetCount >= 10)
					break;
				foundTarget = 1;
			}
		}

		if (isFlying) {
			if (CNPC_IsAversionTarget(self, entity)) {
				targets[targetCount].entity = entity;
				targets[targetCount].type = 2;
				targetCount++;
				if (targetCount >= 10)
					break;
				foundTarget = 1;
			}
		}

		if (!isPacking)
			continue;
		if (((CMobile *)self)->hasFollowers != 0)
			continue;
		if (((CMobile *)self)->isFollower != 0)
			continue;
		if (!CNPC_CanPackTarget(self, entity))
			continue;
		targetPower = CNPC_GetPowerLevel(entity);
		if (targetPower < CNPC_GetPowerLevel(self))
			continue;
		targets[targetCount].entity = entity;
		targets[targetCount].type = 1;
		targetCount++;
		if (targetCount >= 10)
			break;
		foundTarget = 1;
	}

	if (!foundTarget) {
		CList_Clear(&resultList);
		return;
	}

	chosenIdx = GetRandomRange(1, targetCount) - 1;
	actionType = targets[chosenIdx].type;

	switch (actionType) {
	case 0:
		CombatInitiate((CMobile *)self, (CMobile *)targets[chosenIdx].entity, 1);
		((CNPC *)self)->aiState = 3;
		break;
	case 1:
		if (VT_IsNPC(targets[chosenIdx].entity)) {
			packTarget = targets[chosenIdx].entity;
			if (packTarget != NULL)
				NPC_PackMerge((CMobile *)self, (CMobile *)packTarget);
		}
		break;
	case 2:
		CLocation_SetLoc(&((CNPC *)self)->patrolTarget, &self->resourceEntity.entity.location);
		dx = (int16_t)targets[chosenIdx].entity->resourceEntity.entity.location.x - (int16_t)self->resourceEntity.entity.location.x;
		dy = (int16_t)targets[chosenIdx].entity->resourceEntity.entity.location.y - (int16_t)self->resourceEntity.entity.location.y;
		((CNPC *)self)->patrolTarget.x += (int16_t)(dx * 10 * -1);
		((CNPC *)self)->patrolTarget.y += (int16_t)(dy * 10 * -1);
		((CNPC *)self)->isWalking = 1;
		((CNPC *)self)->aiState = 2;
		break;
	}

	CList_Clear(&resultList);
}

/*
 * 0x00432831 - CNPC::ScavengerPickup
 *
 * Picks up every moveable, non-mobile item with bodyType >= 3 in a
 * 16-tile box around the NPC and stashes it into the NPC's container.
 */
static void
CNPC_ScavengerPickup(CItem *self)
{
	CItem *ent, *next;
	CLocation loc;
	int x, y;
	int blockIndex;

	for (x = (int)(int16_t)self->resourceEntity.entity.location.x - 8; x < (int)(int16_t)self->resourceEntity.entity.location.x + 8; x += 8) {
		for (y = (int)(int16_t)self->resourceEntity.entity.location.y - 8; y < (int)(int16_t)self->resourceEntity.entity.location.y + 8; y += 8) {
			blockIndex = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
			if (blockIndex < 0)
				return;

			ent = g_SpatialGrid.cells[blockIndex].itemHead;
			while (ent != NULL) {
				next = ent->spatialNext;

				if (!((int (*)(void *, void *))VT_FN(ent, VT_IS_MOVEABLE))(ent, self))
					goto next_ent;

				if ((CEntity_GetBodyType(ent) & 0xFFFF) < 3)
					goto next_ent;

				if (VT_IsMobile(ent))
					goto next_ent;

				((int (*)(void *))VT_FN(self, VT_GET_STORED_WEIGHT))(self);
				((int (*)(void *))VT_FN(ent, VT_GET_WEIGHT))(ent);
				((int (*)(void *))VT_FN(self, VT_GET_MAX_WEIGHT))(self);

				((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);

				CLocation_Init(&loc);
				CLocation_Set(&loc, -1, -1, -1);
				((void (*)(void *, void *, void *))VT_FN(ent, VT_ADD_TO_CONTAINER))(ent, self, &loc);

next_ent:
				ent = next;
			}
		}
	}
}

/*
 * 0x00432997 - CNPC::IdleScan
 *
 * Idle-tick dispatcher: reads behavioral tags (flying/predator/packing/
 * scavenger), runs state-specific transitions and target scanning, and
 * randomizes a patrol target when left in IDLE_WANDER.
 *
 * FIXED: the binary incorrectly writes patrolTarget from the entity's
 * offset 0x10 (nextInContainer) instead of 0x0A (location). Subsequent
 * code in the same function uses the correct offsets.
 */
static void
CNPC_IdleScan(CItem *self)
{
	CNPC *npc;
	int hasFlyingLocal;
	int isPredator;
	int isPacking;
	int savedState;
	int isFlying;
	int flyingFlag;

	npc = (CNPC *)self;
	hasFlyingLocal = 0;
	isPredator = 0;
	isPacking = 0;
	savedState = npc->aiState;
	isFlying = 1;
	flyingFlag = 0;

	if (GetRandomRange(1, 10) == 1)
		CMobile_NPC_SetAIState(&npc->mobile, 1);

	if (VT_IsVendor(self))
		return;

	if (CNPC_GetFlyingTag(self)) {
		flyingFlag = 1;
		CNPC_SetRunState(&npc->mobile, 1);
	}

	if (CNPC_GetPredatorTag(self))
		isPredator = 1;

	if (CNPC_GetPackingTag(self))
		isPacking = 1;

	if (CNPC_GetScavengerTag(self)) {
		if (npc->aiState != 3 && npc->aiState == 0xa) {
			if (GetRandomRange(0, 25) == 1)
				CNPC_ScavengerPickup(self);
		}
	}

	if (GetRandomRange(1, 100) == 1) {
		CLocation_SetLoc(&npc->patrolTarget, &self->resourceEntity.entity.location);
		npc->isWalking = 1;
		npc->aiState = 0;
	}

	if (npc->aiState == 4)
		return;
	if (npc->aiState == 3)
		return;

	if (npc->aiState == 7) {
		npc->wanderBurstCount--;
		if ((int)npc->wanderBurstCount >= 1)
			return;
		npc->aiState = 0xa;
	}

	if (npc->aiState == 0xa)
		npc->scanTimer = 1;

	npc->scanTimer--;

	if ((int)npc->scanTimer < 1) {
		npc->scanTimer = GetRandomRange(1, 2);
		if (hasFlyingLocal || isPredator || isPacking) {
			CNPC_ScanForTargets(self, 20, isPredator, isFlying, isPacking);
		}
	}

#ifdef CUSTOM_ECOLOGY_DEBUG
	// Binary 0x0043305E calls NPC_DebugState here as a developer aid,
	// broadcasting the AI state as overhead speech. Suppressed in
	// production builds because under CUSTOM_ECOLOGY every idle NPC
	// would chatter its state every tick. Compile with
	// -DCUSTOM_ECOLOGY_DEBUG to re-enable.
	NPC_DebugState(self);
#endif

	if (npc->aiState != 0xa)
		return;

	npc->aiState = 0;
	npc->patrolTarget.x = self->resourceEntity.entity.location.x;
	npc->patrolTarget.y = self->resourceEntity.entity.location.y;
	npc->patrolTarget.z = self->resourceEntity.entity.location.z;
	npc->patrolTarget.x += (int16_t)(GetRandomRange(0, 10) - 5);
	npc->patrolTarget.y += (int16_t)(GetRandomRange(0, 10) - 5);
	npc->patrolTarget.z = 0;
	npc->isWalking = 1;

	USED(savedState);
	USED(flyingFlag);
}

/*
 * 0x00432C65 - CNPC::GetPowerLevel
 *
 * Aggregates spellCasterLevel tag, combat/magic skills, and base stats
 * into a power score used by IsAversionTarget.
 */
static int
CNPC_GetPowerLevel(CItem *entity)
{
	CMobile *mob;
	int level;
	int spellCasterLevel;

	mob = (CMobile *)entity;
	level = 0;
	spellCasterLevel = 0;

	if (CResourceEntity_HasTag(entity, "spellCasterLevel", 0))
		CResourceEntity_GetTagInt(entity, "spellCasterLevel", &spellCasterLevel);

	level += spellCasterLevel * 5;
	level += (int)(uint16_t)mob->skills[5] / 100;
	level += (int)(uint16_t)mob->skills[40] / 100;
	level += (int)(uint16_t)mob->skills[42] / 100;
	level += (int)(uint16_t)mob->skills[41] / 200;
	level += (int)(uint16_t)mob->skills[27] / 40;
	level += ((int)(uint16_t)mob->baseStr + (int)(uint16_t)mob->baseDex + (int)(uint16_t)mob->baseInt) / 3;

	return level;
}

/*
 * 0x00432D82 - NPC_DebugState
 *
 * Speaks the NPC's current aiState as overhead debug text.
 */
static void
NPC_DebugState(CItem *entity)
{
	char buf[512];
	char *stateName;
	int state;

	state = ((CNPC *)entity)->aiState;

	switch (state) {
	case 10:
		stateName = "Idle";
		break;
	case 0:
		stateName = "Wander";
		break;
	case 1:
		stateName = "Pursue";
		break;
	case 2:
		stateName = "Runaway";
		break;
	case 3:
		stateName = "Combat";
		break;
	case 4:
		stateName = "Following";
		break;
	case 5:
		stateName = "Talking";
		break;
	case 6:
		stateName = "Loiter";
		break;
	case 7:
		stateName = "Sleep";
		break;
	default:
		stateName = "Bad State";
		break;
	}

	sprintf(buf, "myState: %d  (%s)", state, stateName);
	((void (*)(void *, char *, int, int, int))VT_FN(entity, VT_SAY_CSTRING))(entity, buf, -1, -1, -1);
}

/*
 * 0x0045E4D7 - CNPC::DoWalk
 *
 * mode == 1: sets patrolTarget to loc with [-2,+3] random X/Y scatter
 * and enters state 4. Other modes are no-ops.
 */
void
CNPC_DoWalk(CNPC *this, int mode, CLocation *loc)
{
	if (mode != 1)
		return;

	CLocation_SetLoc(&this->patrolTarget, loc);
	this->patrolTarget.x += GetRandomRange(1, 6) - 3;
	this->patrolTarget.y += GetRandomRange(1, 6) - 3;
	CNPC_SetState(this, 4);
}

/*
 * 0x00461700 - CGuard::CGuard
 *
 * No-args guard constructor used by the save loader. Bumps vtable from
 * CResourceMobile to CNPC (binary 0x005EECF0, our g_vtable_CGuard),
 * links into the NPC list, sets state=IDLE, npcSfx=0xFFFF.
 */
void
CGuard_Constructor(CNPC *npc)
{
	CMobile *mob = &npc->mobile;

	CResourceMobile_Init(mob);
	CEntity_SetType(&mob->container.item.resourceEntity.entity, ETYPE_GUARD);

	npc->nextNPC = g_NPCListHead;
	if (npc->nextNPC != NULL)
		npc->nextNPC->prevNPC = npc;
	g_NPCListHead = npc;
	npc->prevNPC = NULL;

	CNPC_SetState(npc, NPC_STATE_IDLE);
	npc->npcSfx = 0xFFFF;
	g_NPCSubCount1++;
}

/*
 * 0x004617B4 - CNPC::CNPC
 *
 * Full-args NPC constructor: bumps vtable from CResourceMobile to CNPC
 * (binary 0x005EECF0, our g_vtable_CGuard), links into the global NPC
 * list, sets state=IDLE, npcSfx=0xFFFF, then places the body at loc.
 */
void
CNPC_Constructor(CNPC *npc, uint16_t bodyType, CLocation *loc)
{
	CMobile *mob = &npc->mobile;

	CResourceMobile_Init(mob);
	CEntity_SetType(&mob->container.item.resourceEntity.entity, ETYPE_GUARD);

	npc->nextNPC = g_NPCListHead;
	if (npc->nextNPC != NULL)
		npc->nextNPC->prevNPC = npc;
	g_NPCListHead = npc;
	npc->prevNPC = NULL;

	// Redundant, already set by InitFields
	CNPC_SetState(npc, NPC_STATE_IDLE);

	npc->npcSfx = 0xFFFF;
	g_NPCSubCount1++;
	CResourceMobile_SetBodyAndPlace(mob, bodyType, loc);
}

/*
 * 0x0046188E - CNPC::~CNPC
 *
 * Hides the NPC if still in world, tears down scripts/tags, unlinks
 * from the global NPC list, decrements g_NPCSubCount1, then chains
 * to ~CResourceMobile.
 */
void
CNPC_Destructor(CNPC *npc)
{
	CItem *item = &npc->mobile.container.item;

	if (item->resourceEntity.entity.removedFromWorld == 0)
		CNPC_Hide_VT(item);

	CItem_ClearScriptsAndTags(item);

	if (g_NPCListTail == npc)
		g_NPCListTail = npc->nextNPC;

	if (npc->nextNPC != NULL)
		npc->nextNPC->prevNPC = npc->prevNPC;
	if (npc->prevNPC != NULL)
		npc->prevNPC->nextNPC = npc->nextNPC;
	if (g_NPCListHead == npc)
		g_NPCListHead = npc->nextNPC;

	g_NPCSubCount1--;
	CResourceMobile_Destructor(npc);
}

/*
 * 0x0046197C - CNPC::DropAtFeet (VT_DROP_AT_FEET override)
 *
 * Places the NPC at loc, inserts it into the spatial index, clears
 * removedFromWorld, refreshes its region, and broadcasts an entity
 * update to nearby players.
 */
void
CNPC_DropAtFeet_VT(CItem *self, CLocation *loc)
{
	CVector nearbyList;
	uintptr_t *iter;

	if ((int16_t)self->resourceEntity.entity.location.x == -1) {
		CLocation_SetLoc(&self->resourceEntity.entity.location, loc);
	}

	CItem_InternalMove(self, loc, 0);
	MobileMap_Insert(self);
	self->resourceEntity.entity.removedFromWorld = 0;
	UpdateRegion(self);

	if (!VT_IsHidden(self)) {
		CVector_Constructor(&nearbyList, "\x01");
		GetNearbyPlayers(&nearbyList, &self->resourceEntity.entity.location, 0x12);
		iter = (uintptr_t *)nearbyList.begin;
		while (iter != (uintptr_t *)nearbyList.end) {
			((void (*)(CItem *, CItem *, int))VT_FN(self, VT_SEND_ENTITY_UPDATE))(self, (CItem *)*iter, 1);
			iter++;
		}
		CVector_Destructor(&nearbyList);
	}

	CResourceEntity_NotifyPostModifyIfActive(self);
	CNPCManager_AddToSpatialMap(self);
	CItem_ReleaseTracking(self);
	CMobile_IncrNormalNPCCount((CMobile *)self);
}

/*
 * 0x00461A9C - CNPC vtable[0x08] SetLocation
 *
 * CNPC override of VT_SET_LOCATION. Simpler version of DropAtFeet.
 * Sets location, schedules decay if valueless, calls InternalMove
 * with flag=1, adds to NPC spatial index, clears removedFromWorld,
 * updates region, adds to NPC hash tracking, releases tracking,
 * increments normal NPC count.
 */
void
CNPC_SetLocation_VT(CItem *self, CLocation *loc)
{
	if ((int16_t)self->resourceEntity.entity.location.x == -1) {
		CLocation_SetLoc(&self->resourceEntity.entity.location, loc);
	}

	if (CItem_IsValueless(self)) {
		ScheduleEvent(0xF0, self->serial, 8, 0, 0);
	}

	CItem_InternalMove(self, loc, 1);
	MobileMap_Insert(self);
	self->resourceEntity.entity.removedFromWorld = 0;
	UpdateRegion(self);
	CResourceEntity_NotifyPostModifyIfActive(self);
	CNPCManager_AddToSpatialMap(self);
	CItem_ReleaseTracking(self);
	CMobile_IncrNormalNPCCount((CMobile *)self);
}

/*
 * 0x00461B3D - CNPC vtable[0x0C] Hide
 *
 * CNPC override of VT_HIDE. Removes NPC from the statics spatial
 * index, then calls CNPC_RemoveFromWorldGrid to remove from the
 * CBlockManager spatial grid and send DESTROY_OBJECT to nearby players.
 */
void
CNPC_Hide_VT(CItem *self)
{
	MobileMap_Remove(self);
	CNPC_RemoveFromWorldGrid(self);
}

/*
 * 0x00461B5C - CNPC vtable[0x10] DetachSpatial
 *
 * Removes the NPC from MobileMap before the base VT_DETACH_SPATIAL.
 */
void
CNPC_DetachSpatial_VT(CItem *self)
{
	MobileMap_Remove(self);
	CMobile_DetachSpatial_VT(self);
}

/*
 * 0x00461CC0 - CNPC::SfxCheck
 *
 * Self-destruct countdown for temporary NPCs: resets to 720 each
 * combat tick, counts down otherwise, and at 0 plays the explosion
 * animation/sound and deletes the NPC. No-op when npcSfx == 0xFFFF.
 */
static int
CNPC_SfxCheck(CNPC *npc)
{
	CMobile *mob;

	mob = &npc->mobile;

	if (npc->npcSfx != 0xFFFF && npc->aiState == NPC_STATE_ATTACK_TARGET)
		npc->npcSfx = 0x2D0;

	if (npc->npcSfx == 0xFFFF)
		return 0;

	npc->npcSfx--;
	if (npc->npcSfx > 0)
		return 0;

	Script_doLocAnimation(CItem_GetLocationVT((CItem *)mob), 0x3728, 10, 10, 0, 0);
	Script_sfx(CItem_GetLocationVT((CItem *)mob), 0x1FE, 0);
	if (mob != NULL)
		((void (*)(void *))VT_FN((CItem *)mob, VT_DELETE))(mob);
	return 1;
}

/*
 * 0x00461DA0 - CNPC::ScalarDelete
 *
 * Scalar deleting destructor: runs CNPC_Destructor and frees the NPC when
 * flags & 1.
 */
void *
CNPC_ScalarDelete(CNPC *npc, int flags)
{
	CNPC_Destructor(npc);
	if (flags & 1)
		OperatorDelete(npc);
	return NULL;
}

/*
 * 0x00463910 - CNPC::TestBehavior (vtable[0x244])
 *
 * Returns 1 if any of the given bits are set in behaviorFlags.
 */
int
CNPC_TestBehavior_VT(CNPC *self, int flag)
{
	return (self->behaviorFlags & flag) ? 1 : 0;
}

/*
 * 0x00463930 - CNPC::SetBehavior (vtable[0x248])
 *
 * Sets the given bits in behaviorFlags.
 */
void
CNPC_SetBehavior_VT(CNPC *self, int flag)
{
	self->behaviorFlags |= flag;
}

/*
 * 0x00463960 - CNPC::ClrBehavior (vtable[0x24C])
 *
 * Clears the given bits in behaviorFlags. CPlayer overrides this
 * slot for aggressor tracking.
 */
void
CNPC_ClrBehavior_VT(CNPC *self, int flag)
{
	self->behaviorFlags &= ~flag;
}

/*
 * 0x00467758 - StaticInit_NPCHash
 *
 * Static-init wrapper that constructs the global NPC hash table.
 */
static __attribute__((unused)) void
StaticInit_NPCHash(void)
{
	CNPCHash_Constructor();
}

/*
 * 0x00480920 - CNPCMap::Init
 *
 * Allocates the NPC spatial CEntityMap sized to the world bounds
 * (blockShift=6) and installs it as g_NPCMap.
 */
void
CNPCMap_Init(void)
{
	CEntityMap *map;

	map = (CEntityMap *)OperatorNew(sizeof(CEntityMap));
	if (map != NULL) {
		CEntityMap_Constructor(map, g_mapStartX, g_mapStartY, g_mapStartX + g_mapWidth - 1, g_mapStartY + g_mapHeight - 1, 6);
	}
	g_NPCMap = map;
}

/*
 * 0x004809C2 - CNPCHash::CNPCHash
 *
 * Zeros all 64 NPC hash buckets, clears the iteration cursor and
 * g_NPCMap, and constructs the deferred heartbeat queue.
 */
void
CNPCHash_Constructor(void)
{
	int i;
	char typeFlag = 0;

	g_NPCMap = NULL;
	CVector_Constructor(&g_NPCActiveList, &typeFlag);

	for (i = 0; i < 64; i++)
		g_NPCHash[i] = NULL;
	g_NPCHashIterNext = NULL;
}

/*
 * 0x00480A25 - CNPCManager::~CNPCManager
 *
 * Deletes the NPC map block and the deferred heartbeat queue.
 */
void
CNPCManager_Destructor(void)
{
	if (g_NPCMap != NULL)
		CNPCManagerBlock_ScalarDelete((CNPCManagerBlock *)g_NPCMap, 1);
	CVector_Destructor(&g_NPCActiveList);
}

/*
 * 0x00480A9B - NPC_FindBySerial
 *
 * Returns the NPC with the given serial, or NULL.
 */
CNPC *
NPC_FindBySerial(uint32_t serial)
{
	CNPC *npc;

	npc = g_NPCHash[serial & 0x3F];
	while (npc != NULL) {
		if (npc->mobile.container.item.serial == serial)
			return npc;
		npc = npc->npcHashNext;
	}
	return NULL;
}

/*
 * 0x00480ADB - CNPCManager::CreateMobileAt
 *
 * Builds a CResourceMobile with body type 1 at (x, y, 0). The result
 * goes into a local the function never reads and it always returns 0,
 * so the mobile is created and leaked. The binary's RET declares 0x24
 * bytes of arguments while only the first two words are ever read.
 */
static __attribute__((unused)) int
CNPCManager_CreateMobileAt(int16_t x, int16_t y)
{
	CLocation loc;
	CNPC *npc;

	CLocation_Init(&loc);
	CLocation_Set(&loc, x, y, 0);

	npc = (CNPC *)OperatorNew(sizeof(CNPC));
	if (npc != NULL)
		CResourceMobile_Constructor(&npc->mobile, 1, &loc);

	return 0;
}

/*
 * 0x00480B7A - CNPCHash::HeartbeatTick
 *
 * Drains the deferred heartbeat queue, then ticks CNPC_Heartbeat on
 * 8 hash buckets (cursor-based, round-robin across calls) for NPCs
 * present in the world and not inside a container.
 */
void
CNPCHash_HeartbeatTick(void)
{
	int i;
	CNPC *npc;

	CNPCManager_DrainActiveList();

	for (i = 0; i < 8; i++) {
		npc = g_NPCHash[g_NPCHashCursor];
		while (npc != NULL) {
			g_NPCHashIterNext = npc->npcHashNext;
			if (npc->mobile.container.item.resourceEntity.entity.removedFromWorld == 0 && npc->mobile.container.item.parent == NULL) {
				g_currentNPC = npc;
				CNPC_Heartbeat(npc);
				g_currentNPC = NULL;
			}
			npc = g_NPCHashIterNext;
		}
		g_NPCHashCursor = (g_NPCHashCursor + 1) & 0x3F;
	}
	g_NPCHashIterNext = NULL;
}

/*
 * 0x00480C34 - CNPCHash::RegenMoveTick
 *
 * Every tick, regenerates stats on all 64 hash buckets and runs
 * CNPC_AIMoveTick for NPCs present in the world and not inside a
 * container.
 */
void
CNPCHash_RegenMoveTick(void)
{
	int i;
	CNPC *npc;

	for (i = 0; i < 64; i++) {
		npc = g_NPCHash[i];
		while (npc != NULL) {
			g_NPCHashIterNext = npc->npcHashNext;
			CMobile_NPC_RegenTick(&npc->mobile);
			if (npc->mobile.container.item.resourceEntity.entity.removedFromWorld == 0 && npc->mobile.container.item.parent == NULL) {
				g_currentNPC = npc;
				CNPC_AIMoveTick(npc);
				g_currentNPC = NULL;
			}
			npc = g_NPCHashIterNext;
		}
	}
	g_NPCHashIterNext = NULL;
}

/*
 * 0x00480CD6 - CResourceMobile::SetBodyAndPlace
 *
 * Sets the body type and dispatches VT_DROP_AT_FEET to place the
 * mobile at loc.
 */
void
CResourceMobile_SetBodyAndPlace(CMobile *mob, uint16_t bodyType, CLocation *loc)
{
	CPlayer_SetBodyType((CPlayer *)mob, bodyType);
	((void (*)(void *, CLocation *))VT_FN(&mob->container.item, VT_DROP_AT_FEET))(&mob->container.item, loc);
}

/*
 * 0x00480CFF - CNPC::InitFields
 *
 * Initializes all NPC-specific fields (including state=IDLE and
 * a random tick offset), increments g_NPCCount, and inserts the NPC
 * into the serial hash table.
 */
void
CNPC_InitFields(CNPC *npc)
{
	CMobile *mob = &npc->mobile;

	g_NPCCount++;
	npc->_npc_unk404 = 0;
	npc->isWalking = 0;
	mob->container.item.itemFlags = 0;
	npc->_npc_unk45C = 0;
	NPC_AddToHash(npc);
	CLocation_Invalidate(&npc->patrolTarget);
	CNPC_SetState(npc, NPC_STATE_IDLE);
	npc->prevAIState = NPC_STATE_IDLE;
	// Overwritten later to 0xC8
	npc->npcInfo1_0 = 0;
	npc->actionTarget = 0;
	npc->pathArray = 0;
	npc->lastWanderQuadrant = 4;
	npc->resTplCount0 = 0;
	npc->resTplCount1 = 0;
	npc->npcSearchRange = 0xFF;
	npc->npcLevel = 0;
	npc->levelListPrev = NULL;
	npc->levelListNext = NULL;
	npc->followObj2 = 0;
	npc->followObj1 = 0;
	npc->followObj3 = 0;
	npc->npcInfo1_1 = 0;
	mob->altBodyType = 0;
	mob->stomach = 0xFF;
	npc->npcJob = "mystic llamaherder";
	npc->npcTown = "the wilderness";
	mob->moveSpeedAccum = 0;
	mob->statClock = 0;
	// Overwriting the 0 set earlier
	npc->npcInfo1_0 = 0xC8;

	npc->resTplPtr0 = NULL;
	npc->resTplPtr1 = NULL;
	npc->resTplPtr2 = NULL;
	npc->resTplPtr3 = NULL;
	npc->resTplPtr4 = NULL;
	npc->resTplPtr5 = NULL;
	npc->resTplPtr6 = NULL;
	npc->resTplPtr7 = NULL;

	npc->tickCount = 0;
	npc->npcInfo1_3 = GetRandomRange(1, 5);
	npc->desireLoc.x = 0xFFFF;
	npc->desireLoc.y = 0xFFFF;
	npc->desireLoc.z = 0;
	npc->lastDesireLoc.x = 0xFFFF;
	npc->lastDesireLoc.y = 0xFFFF;
	npc->lastDesireLoc.z = 0;
	npc->npcCombatTarget = 0;
	npc->speechCounter = 0;
	npc->aiByte2 = 0;
	npc->aiByte3 = 0;
	npc->aiDelayCounter = 0;
	npc->npcFlee = 0x10;
	npc->frozenCombatFlag = 0;
	npc->npcFreezeTimer = 0;
	npc->npcAITarget = 0;
	npc->effectCheckCounter = 0;
	npc->hungerCapacity = 0;
	npc->wanderBurstCount = 1;
	npc->wanderSteps = 1;
	npc->resourceTargetSerial = 0;
	npc->loiterData = 0;
	npc->homeInfo1 = 0;
	npc->homeInfo2 = 0;
	npc->homeInfo3 = 0;
}

/*
 * 0x0048104C - CResourceMobile::CResourceMobile
 *
 * No-args CResourceMobile constructor: chains CMobile_Constructor,
 * clears behaviorFlags and the five CLocation waypoints, and runs
 * CNPC_InitFields. Used for guard NPCs.
 */
void
CResourceMobile_Init(CMobile *mob)
{
	CNPC *npc = (CNPC *)mob;

	CMobile_Constructor(mob);
	npc->behaviorFlags = 0;

	npc->patrolTarget.x = 0xFFFF;
	npc->patrolTarget.y = 0xFFFF;
	npc->patrolTarget.z = (int16_t)0xFFFF;
	npc->desireLoc.x = 0xFFFF;
	npc->desireLoc.y = 0xFFFF;
	npc->desireLoc.z = (int16_t)0xFFFF;
	npc->lastDesireLoc.x = 0xFFFF;
	npc->lastDesireLoc.y = 0xFFFF;
	npc->lastDesireLoc.z = (int16_t)0xFFFF;
	npc->loiterLoc.x = 0xFFFF;
	npc->loiterLoc.y = 0xFFFF;
	npc->loiterLoc.z = (int16_t)0xFFFF;
	npc->homeLoc.x = 0xFFFF;
	npc->homeLoc.y = 0xFFFF;
	npc->homeLoc.z = (int16_t)0xFFFF;

	npc->convoFragList = NULL;
	npc->pathArray = 0;
	npc->pathSpeed = -1;
	npc->pathStepsRemaining = 0;
	CEntity_SetType(&mob->container.item.resourceEntity.entity, ETYPE_NPC);
	CNPC_InitFields(npc);
}

/*
 * 0x00481127 - CResourceMobile::CResourceMobile
 *
 * Full-args CResourceMobile constructor for "normal" NPCs: sets up
 * waypoints, places the body at loc, then runs CNPC_InitFields.
 */
void
CResourceMobile_Constructor(CMobile *mob, uint16_t bodyType, CLocation *loc)
{
	CNPC *npc = (CNPC *)mob;

	CMobile_Constructor(mob);
	npc->behaviorFlags = 0;

	npc->patrolTarget.x = 0xFFFF;
	npc->patrolTarget.y = 0xFFFF;
	npc->patrolTarget.z = (int16_t)0xFFFF;
	npc->desireLoc.x = 0xFFFF;
	npc->desireLoc.y = 0xFFFF;
	npc->desireLoc.z = (int16_t)0xFFFF;
	npc->lastDesireLoc.x = 0xFFFF;
	npc->lastDesireLoc.y = 0xFFFF;
	npc->lastDesireLoc.z = (int16_t)0xFFFF;
	npc->loiterLoc.x = 0xFFFF;
	npc->loiterLoc.y = 0xFFFF;
	npc->loiterLoc.z = (int16_t)0xFFFF;
	npc->homeLoc.x = 0xFFFF;
	npc->homeLoc.y = 0xFFFF;
	npc->homeLoc.z = (int16_t)0xFFFF;

	npc->convoFragList = NULL;
	npc->pathArray = 0;
	npc->pathSpeed = -1;
	npc->pathStepsRemaining = 0;
	CEntity_SetType(&mob->container.item.resourceEntity.entity, ETYPE_NPC);
	CResourceMobile_SetBodyAndPlace(mob, bodyType, loc);
	CNPC_InitFields(npc);
}

/*
 * 0x00481228 - CResourceMobile::~CResourceMobile
 *
 * CResourceMobile destructor - middle of the CNPC destructor chain:
 * CNPC_Destructor -> CResourceMobile_Destructor -> CMobile_Destructor.
 * Sets vtable to CNPC, dismounts rider if parent is mounted mobile,
 * removes from world grid, clears scripts/tags, clears g_currentNPC,
 * handles respawn (returns resource count to spawn owner entity or
 * calls Spawn_ScheduleRespawn - a binary no-op at 0x004853BD),
 * removes from NPC hash and level list, releases convoFragList through
 * the list's scalar deleting destructor (0x00484380), frees resource
 * template pointers, chains to CMobile_Destructor.
 *
 * CUSTOM (FEAT_CLOSED_ECONOMY, FEAT_PERNPC_RESPAWN): death returns the
 * NPC's type 3 resource nodes to its region's spawned count and enqueues
 * a per-NPC respawn.
 */
void
CResourceMobile_Destructor(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	CItem *item = &mob->container.item;
	CItem *parent;
	CResourceNode *node;
	CItem *spawnOwner;

	CEntity_SetType(&item->resourceEntity.entity, ETYPE_NPC);

	parent = item->parent;
	if (parent != NULL) {
		if (VT_IsMobile(parent)) {
			if (((CMobile *)parent)->equipment[25] != NULL)
				CMobile_Dismount((CMobile *)parent);
		}
	}

	if (!item->resourceEntity.entity.removedFromWorld)
		CNPC_RemoveFromWorldGrid(item);

	CItem_ClearScriptsAndTags(item);

	if (npc == g_currentNPC)
		g_currentNPC = NULL;

	if ((npc->behaviorFlags & 0x800) != 0 && !g_SuppressRespawn && npc->homeInfo3 != 0) {
		if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)npc->homeLoc.x, (int16_t)npc->homeLoc.y)) {
			spawnOwner = CWorld_FindBySerial(g_World, npc->homeInfo3);
			if (spawnOwner != NULL) {
				node = CResourceEntity_FindNode(spawnOwner, (uint16_t)npc->homeInfo1, 3);
				if (node != NULL) {
					CResourceEntity_NotifyPreModify(spawnOwner);
					node->value3 += npc->homeInfo2;
					CResourceEntity_NotifyPostModify(spawnOwner);
					CResourceEntity_NotifyPostModifyIfActive(spawnOwner);
				} else {
					Spawn_ScheduleRespawn(&npc->homeLoc, npc->homeInfo1, npc->homeInfo2);
				}
			} else {
				Spawn_ScheduleRespawn(&npc->homeLoc, npc->homeInfo1, npc->homeInfo2);
			}
		}
		CNPC_ClrBehavior_VT(npc, 0x800);
	}

	NPC_RemoveFromHash(npc);

	// CUSTOM: NPC death no longer refunds the bank - the corpse holds the
	// produced resources. Refund happens when the corpse decays (see
	// RefundResourceNodesToBank in resbank.c) or when the resource is
	// extracted via Script_returnResourcesToBank (FEAT_CLOSED_ECONOMY
	// harvest-side credit). Death only mirrors the spawn-side bookkeeping
	// (SubtractFromSpawnedCount) and feeds the per-NPC respawn queue.
	if (feat(FEAT_CLOSED_ECONOMY) || feat(FEAT_PERNPC_RESPAWN)) {
		uint16_t ti = (uint16_t)CResourceEntity_GetTemplateIndex(item);
		if (ti != 0xFFFF) {
			NPCTemplate *tmpl = CResManager_GetTemplateByID(ti);
			if (tmpl != NULL) {
				CResBankRegion *region = NULL;
				CResourceNode *nd;
				int hasType3 = 0;

				if (feat(FEAT_CLOSED_ECONOMY))
					region = CResBankManager_GetRegionByLocation(item->resourceEntity.entity.location.x, item->resourceEntity.entity.location.y);

				for (nd = tmpl->resourceNodes; nd != NULL; nd = nd->next) {
					if (nd->type != 3 || nd->id == 0)
						continue;
					if (feat(FEAT_CLOSED_ECONOMY) && region != NULL && region != g_ResBankManager.noRegion)
						CResBankRegion_SubtractFromSpawnedCount(region, nd->id, nd->value1);
					hasType3 = 1;
				}
				// Per-NPC respawn only for NPCs that were created
				// DIRECTLY in a sub-region listed in the template's
				// <region>. NPCs created via a parent spawner's
				// <eq> directive (e.g. Undead Group's lich child
				// at the spawner's tile in CEMETERY_MOONGLOW,
				// outside any LICH_* bbox) come from a separate
				// mechanism and would compound past cap if both
				// fired in parallel.
				if (hasType3 && feat(FEAT_PERNPC_RESPAWN)) {
					int16_t *cloc = (int16_t *)&item->resourceEntity.nextInContainer;
					if (LocationInTemplateSubRegion(ti, cloc[0], cloc[1])) {
						PendingNPCRespawn_Enqueue(ti, cloc[0], cloc[1], (int8_t)cloc[2]);
					}
				}
			}
		}
	}

	if (npc->convoFragList != NULL) {
		StdPtrList_ScalarDelete_NPC((StdPtrList *)npc->convoFragList, 1);
		npc->convoFragList = NULL;
	}

	if (npc->pathArray != 0)
		Path_Discard(npc);

	free(npc->resTplPtr0);
	npc->resTplPtr0 = NULL;
	free(npc->resTplPtr1);
	npc->resTplPtr1 = NULL;
	free(npc->resTplPtr2);
	npc->resTplPtr2 = NULL;
	free(npc->resTplPtr3);
	npc->resTplPtr3 = NULL;
	free(npc->resTplPtr4);
	npc->resTplPtr4 = NULL;
	free(npc->resTplPtr5);
	npc->resTplPtr5 = NULL;
	free(npc->resTplPtr6);
	npc->resTplPtr6 = NULL;
	free(npc->resTplPtr7);
	npc->resTplPtr7 = NULL;

	CMobile_Destructor(mob);
}

/*
 * 0x004813F0 - NPC_RemoveFromHash
 *
 * Decrements g_NPCCount, fixes up the iterator cursor to avoid
 * use-after-free mid-iteration, unlinks from the bucket chain, and
 * removes the NPC from its level list.
 */
void
NPC_RemoveFromHash(CNPC *npc)
{
	uint32_t bucket;

	g_NPCCount--;

	// Iterator fixup: advance to npcHashNext to prevent use-after-free
	if (g_NPCHashIterNext == npc)
		g_NPCHashIterNext = npc->npcHashNext;

	if (npc->npcHashNext != NULL)
		npc->npcHashNext->npcHashPrev = npc->npcHashPrev;
	if (npc->npcHashPrev != NULL)
		npc->npcHashPrev->npcHashNext = npc->npcHashNext;
	else {
		bucket = npc->mobile.container.item.serial & 0x3F;
		if (g_NPCHash[bucket] == npc)
			g_NPCHash[bucket] = npc->npcHashNext;
	}

	CNPC_RemoveFromLevelList(npc);
}

/*
 * 0x00481643 - CNPC::IsInLevelList
 *
 * Returns 1 if this NPC is linked into g_NPCLevelList for its level.
 */
int
CNPC_IsInLevelList(CNPC *npc)
{
	if (npc->levelListPrev != NULL)
		return 1;
	if (npc->levelListNext != NULL)
		return 1;
	if (npc->npcLevel != 0 && g_NPCLevelList[npc->npcLevel] == npc)
		return 1;
	return 0;
}

/*
 * 0x004816A1 - CNPC::AddToLevelList
 *
 * Prepends this NPC to g_NPCLevelList[npcLevel]. No-op if level==0.
 */
void
CNPC_AddToLevelList(CNPC *npc)
{
	uint8_t level;

	level = npc->npcLevel;
	if (level == 0)
		return;
	if (g_NPCLevelList[level] != NULL)
		g_NPCLevelList[level]->levelListNext = npc;
	npc->levelListPrev = g_NPCLevelList[level];
	g_NPCLevelList[level] = npc;
	npc->levelListNext = NULL;
}

/*
 * 0x00481728 - CNPC::RemoveFromLevelList
 *
 * Unlinks this NPC from g_NPCLevelList[npcLevel]. No-op if level==0.
 */
void
CNPC_RemoveFromLevelList(CNPC *npc)
{
	uint8_t level;

	level = npc->npcLevel;
	if (level == 0)
		return;
	if (g_NPCLevelList[level] == npc)
		g_NPCLevelList[level] = npc->levelListPrev;
	if (npc->levelListNext != NULL)
		npc->levelListNext->levelListPrev = npc->levelListPrev;
	if (npc->levelListPrev != NULL)
		npc->levelListPrev->levelListNext = npc->levelListNext;
	npc->levelListNext = NULL;
	npc->levelListPrev = NULL;
}

/*
 * 0x004818AA - CMobile::GetWalkZ (vtable[0x218])
 *
 * Returns the walkable Z at loc: terrain Z + 10 for water mobiles,
 * otherwise loc.z.
 */
int
CMobile_GetWalkZ(CItem *mob, CLocation loc)
{
	int mt;

	mt = ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF;
	if (mt == 2)
		return CTerrainManager_GetStandableZ((int)(int16_t)loc.x, (int)(int16_t)loc.y, (int)(int16_t)loc.z) + 10;
	return (int)(int16_t)loc.z;
}

/*
 * 0x00481B73 - CMobile::DoWalk (vtable[0x20C])
 *
 * Moves the mobile one tile in dir. Steps: frozen-NPC guard, direction
 * update, terrain Z check (speed==-128 computes Z, else uses speed as Z),
 * passability and reveal checks, spatial grid update, remove/insert/
 * overlap visibility notifications, and climb stamina drain.
 */
void
CMobile_DoWalk(CItem *mob, int dir, int speed)
{
	CLocation oldLoc;
	CLocation newLoc;
	CLocation *locPtr;
	CMobile *m = (CMobile *)mob;
	int curDir;
	int oldBlock, newBlock;
	int minZ, maxZ;
	int16_t resultZ;
	int oldZ;
	uint8_t destroyPkt[16];
	uint16_t pktOffset;
	CVector removeList, insertList, overlapList;
	uintptr_t *iter;
	char typeA, typeB, typeC;

	CLocation_Init(&oldLoc);
	CLocation_Init(&newLoc);

	if (VT_IsNPC(mob)) {
		if (((CNPC *)mob)->behaviorFlags & 0x8000)
			return;
	}

	locPtr = CEntity_GetLocation(&mob->resourceEntity.entity);
	CLocation_SetLoc(&newLoc, locPtr);

	CLocation_MoveDir(&newLoc, dir & 0x7f);

	curDir = CItem_GetDirectionVT(mob);
	if (dir != curDir)
		CMobile_SetDirection(mob, dir);

	locPtr = CEntity_GetLocation(&mob->resourceEntity.entity);
	CLocation_SetLoc(&oldLoc, locPtr);

	oldBlock = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, locPtr, 0);
	newBlock = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &newLoc, 0);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)newLoc.x, (int)(int16_t)newLoc.y))
		return;

	if (speed == -128) {
		GetMinMaxZForEntity(mob, oldLoc, m->direction, &minZ, &maxZ);

		resultZ = (int16_t)CMobile_GetWalkZ(mob, newLoc);

		resultZ = CTerrainManager_CanWalkWrapper(newLoc, minZ, maxZ, VT_GetHeight(mob), ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF, mob, 0);
	} else {
		resultZ = (int16_t)speed;
	}

	if ((int)(int16_t)resultZ == -128)
		return;

	newLoc.z = resultZ;

	if (!CheckWalkPassability(mob, &oldLoc, &newLoc))
		return;

	if (VT_IsHidden(mob)) {
		((void (*)(void *, int))VT_FN(mob, VT_SET_HIDDEN))(mob, 0);
	}

	if (CItem_IsMultiOwner(mob) == 1 || oldBlock != newBlock) {
		((void (*)(void *))VT_FN(mob, VT_DETACH_SPATIAL))(mob);
		((void (*)(void *, CLocation *))VT_FN(mob, VT_SET_LOCATION))(mob, &newLoc);
	} else {
		CLocation_SetLoc(&mob->resourceEntity.entity.location, &newLoc);
	}

	PacketManager_MakePacket_DESTROY_OBJECT(destroyPkt, mob->serial);
	pktOffset = GetPacketOffset(destroyPkt) & 0xFFFF;

	CVector_Constructor(&removeList, &typeA);
	CVector_Constructor(&insertList, &typeB);
	CVector_Constructor(&overlapList, &typeC);

	CollectMovementVisibility(&removeList, &insertList, &overlapList, (int)(int16_t)mob->resourceEntity.entity.location.x, (int)(int16_t)mob->resourceEntity.entity.location.y,
	        (int)(int16_t)oldLoc.x, (int)(int16_t)oldLoc.y, 18);

	// (9a) Notify entities leaving visibility (removeList): flag=0
	for (iter = (uintptr_t *)removeList.begin; iter != (uintptr_t *)removeList.end; iter++) {
		((void (*)(void *, CItem *, int))VT_FN(mob, VT_SEND_ENTITY_UPDATE))(mob, (CItem *)*iter, 0);
	}

	// (9b) Notify entities entering visibility (insertList): flag=1
	for (iter = (uintptr_t *)insertList.begin; iter != (uintptr_t *)insertList.end; iter++) {
		((void (*)(void *, CItem *, int))VT_FN(mob, VT_SEND_ENTITY_UPDATE))(mob, (CItem *)*iter, 1);
	}

	// (9c) Send DESTROY_OBJECT to overlap entities
	for (iter = (uintptr_t *)overlapList.begin; iter != (uintptr_t *)overlapList.end; iter++) {
		SendToClient((CItem *)*iter, destroyPkt, (int)pktOffset);
	}

	// (10) Stamina drain for climb
	oldZ = (int)(int16_t)oldLoc.z;
	locPtr = &mob->resourceEntity.entity.location;
	if (oldZ > (int)(int16_t)locPtr->z) {
		CMobile_DrainStaminaForClimb(m, oldZ - (int)(int16_t)locPtr->z);
	}

	// Destroy CVectors in reverse order
	CVector_Destructor(&overlapList);
	CVector_Destructor(&insertList);
	CVector_Destructor(&removeList);
}

/*
 * 0x00481FC1 - DirFromDelta
 *
 * Returns the direction index 0-7 matching (dx, dy), or -1 when no match.
 */
static int
DirFromDelta(int dx, int dy)
{
	static const int dirDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
	static const int dirDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	int i;

	for (i = 0; i < 8; i++) {
		if (dirDx[i] == dx && dirDy[i] == dy)
			return i;
	}
	return -1;
}

/*
 * 0x00482009 - CNPC::WalkToPatrolTarget
 *
 * Tries the primary direction toward patrolTarget and, on block,
 * random clockwise/counter-clockwise alternates. Returns 0 on a
 * successful step and 1 when no walk was needed or all paths blocked.
 */
static int
CNPC_WalkToPatrolTarget(CNPC *npc, int arg1, int arg2)
{
	CLocation *npcLoc;
	int dx, dy;
	int direction;
	int walkZ;
	int speed;
	int walkResult;
	int altDirCW, altDirCCW;
	int randomChoice;

	USED(arg1);
	USED(arg2);

	if (CLocation_IsInvalid(&npc->patrolTarget))
		return 1;

	npcLoc = CItem_GetLocationVT((CItem *)npc);

	if (Location_WrappedChebyshevDistance(&npc->patrolTarget, npcLoc) <= 0)
		return 1;

	dx = 0;
	dy = 0;
	if ((int)npc->patrolTarget.x < (int)npcLoc->x)
		dx = -1;
	if ((int)npc->patrolTarget.y < (int)npcLoc->y)
		dy = -1;
	if ((int)npc->patrolTarget.x > (int)npcLoc->x)
		dx = 1;
	if ((int)npc->patrolTarget.y > (int)npcLoc->y)
		dy = 1;

	direction = DirFromDelta(dx, dy);

	if (((int (*)(void *, int, int *))VT_FN((CItem *)npc, VT_WALK_CHECK))(npc, direction, &walkZ))
		goto found_direction;

	if (Location_WrappedChebyshevDistance(&npc->patrolTarget, npcLoc) == 1)
		return 1;

	walkResult = CMobile_CheckWalkDir(npc, direction);
	if (walkResult >= 2)
		return 1;
	if (walkResult == 1)
		return 0;

	if (((int (*)(void *, int, int *))VT_FN((CItem *)npc, VT_WALK_CHECK))(npc, direction, &walkZ))
		goto found_direction;

	altDirCW = (direction + 1) & 7;
	altDirCCW = (direction + 7) & 7;

	randomChoice = rand() & 1;

	walkResult = CMobile_CheckWalkDir(npc, randomChoice == 0 ? altDirCW : altDirCCW);
	if (walkResult >= 2)
		return 1;
	if (walkResult == 1)
		return 0;

	if (((int (*)(void *, int, int *))VT_FN((CItem *)npc, VT_WALK_CHECK))(npc, randomChoice == 0 ? altDirCW : altDirCCW, &walkZ)) {
		direction = randomChoice == 0 ? altDirCW : altDirCCW;
		goto found_direction;
	}

	walkResult = CMobile_CheckWalkDir(npc, randomChoice != 0 ? altDirCW : altDirCCW);
	if (walkResult >= 2)
		return 1;
	if (walkResult == 1)
		return 0;

	if (!((int (*)(void *, int, int *))VT_FN((CItem *)npc, VT_WALK_CHECK))(npc, randomChoice != 0 ? altDirCW : altDirCCW, &walkZ))
		return 1;

	direction = randomChoice != 0 ? altDirCW : altDirCCW;

found_direction:
	if (direction != (int)((CMobile *)npc)->direction)
		CMobile_SetDirection((CItem *)npc, direction);

	speed = 100;
	if (npc->aiByte3 == 1 || npc->aiByte3 == 2) {
		speed = 400;
		direction |= 0x80; // set running bit
	}

	((void (*)(void *, int))VT_FN((CItem *)npc, VT_HANDLE_STAM_DRAIN))(npc, speed);

	((void (*)(void *, int, int))VT_FN((CItem *)npc, VT_DO_WALK))(npc, direction, walkZ);

	return 0;
}

/*
 * 0x004822FD - NPC_PathWalk
 *
 * NPC pathfinding walk step, called while pathArray != 0. Reads the current
 * path step, advances the index, then either turns or walks toward it.
 * Fires event 0x1F when the destination is reached, 0x20 on failure.
 */
static void
NPC_PathWalk(CNPC *npc)
{
	int pathSpeed;
	PathNode stepLoc;
	CLocation *npcLoc;
	int stepDir;
	int checkResult;
	int walkZ;
	uintptr_t walkedPath;
	uint32_t walkedPathGeneration;
	CVector vec;
	char typeFlag;

	pathSpeed = npc->pathSpeed;

	if (!VT_IsDead((CItem *)npc)) {
		if (((int (*)(void *))VT_FN((CItem *)npc, VT_GET_STAMINA))(npc))
			return;
	}

	Path_GetCurrentStep(npc, &stepLoc);
	Path_AdvanceStep(npc);
	walkedPath = npc->pathArray;
	walkedPathGeneration = CNPC_GetPathGeneration(npc);

	npcLoc = &npc->mobile.container.item.resourceEntity.entity.location;

	if (Path_AtStep(&stepLoc, npcLoc, (int)((CMobile *)npc)->direction)) {
		if (npc->pathArray == 0) {
			if (pathSpeed != -1)
				Entity_ExecuteEvent((CEntity *)npc, 0x1F, pathSpeed);
			return;
		}
		// path not consumed yet - fall through to direction check
	}

	stepDir = (int)(int16_t)stepLoc.dir;

	if ((int)((CMobile *)npc)->direction != stepDir) {
		((void (*)(void *))VT_FN((CItem *)npc, VT_DETACH_SPATIAL))(npc);
		CMobile_SetDirection((CItem *)npc, stepDir);
		((void (*)(void *, CLocation *))VT_FN((CItem *)npc, VT_SET_LOCATION))(npc, npcLoc);

		CVector_Constructor(&vec, &typeFlag);
		GetNearbyPlayers(&vec, npcLoc, 0x12);
		((void (*)(void *, CVector *, int))VT_FN((CItem *)npc, VT_NOTIFY_NEARBY))(npc, &vec, 0);

		if (!Path_AtStep(&stepLoc, npcLoc, (int)((CMobile *)npc)->direction)) {
			Path_Discard(npc);
			if (pathSpeed != -1)
				Entity_ExecuteEvent((CEntity *)npc, 0x20, pathSpeed);
		}

		CVector_Destructor(&vec);
		return;
	}

	checkResult = CMobile_CheckWalkDir(npc, stepDir);
	if (npc != g_currentNPC)
		return;
	if (!Path_IsCurrent(npc, walkedPath, walkedPathGeneration))
		return;
	if (checkResult == 3)
		return;
	if (checkResult == 1)
		return;

	checkResult = ((int (*)(void *, int, int *))VT_FN((CItem *)npc, VT_WALK_CHECK))(npc, stepDir, &walkZ);
	if (npc != g_currentNPC || !Path_IsCurrent(npc, walkedPath, walkedPathGeneration))
		return;
	if (checkResult) {
		((void (*)(void *, int, int))VT_FN((CItem *)npc, VT_DO_WALK))(npc, stepDir, walkZ);
		if (npc != g_currentNPC)
			return;
		if (!Path_IsCurrent(npc, walkedPath, walkedPathGeneration))
			return;

		if (Path_AtStep(&stepLoc, npcLoc, (int)((CMobile *)npc)->direction)) {
			if (npc->pathArray == 0) {
				if (pathSpeed != -1)
					Entity_ExecuteEvent((CEntity *)npc, 0x1F, pathSpeed);
			}
			return;
		}
	}

	if (npc->pathArray != 0)
		Path_Discard(npc);
	if (pathSpeed != -1)
		Entity_ExecuteEvent((CEntity *)npc, 0x20, pathSpeed);
}

/*
 * 0x00482580 - CMobile::NPC_RegenTick
 *
 * Called once per tick for each NPC. Handles mana regen (timer 0x278,
 * period 20), HP regen (timer 0x27C, period from hungerCapacity and
 * stomach), and stamina regen via StaminaRegenTick.
 */
void
CMobile_NPC_RegenTick(CMobile *this)
{
	CNPC *npc = (CNPC *)this;

	CMobile_IncrementLifeclock(this);

	// Mana regen: timer at 0x278, period 20
	this->manaRegenTimer--;
	if (this->manaRegenTimer < 0) {
		this->manaRegenTimer = 20;
		if (this->mana < this->maxMana)
			((uint32_t (*)(void *, int))VT_FN((CItem *)this, VT_SET_MANA))(this, (int)this->mana + 1);
		if (this->mana > this->maxMana)
			((uint32_t (*)(void *, int))VT_FN((CItem *)this, VT_SET_MANA))(this, (int)this->mana - 1);
	}

	// HP regen: timer at 0x27C
	this->hpRegenTimer--;
	if (this->hpRegenTimer < 0) {
		// Formula: 100 + (hungerCapacity - stomach) * 10
		this->hpRegenTimer = 100 + ((int)npc->hungerCapacity - (int)this->stomach) * 10;
		if (this->hp < this->maxHp) {
			((uint32_t (*)(void *, int, int))VT_FN((CItem *)this, VT_SET_HP))(this, (int)this->hp + 1, 0);
			((void (*)(void *))VT_FN((CItem *)this, VT_SEND_HP_UPDATE))(this);
		}
	}

	((void (*)(void *))VT_FN((CItem *)this, VT_STAM_REGEN))(this);
}

/*
 * 0x004826B2 - CNPC::ReturnToSpawnIfFrozen
 *
 * For frozen, home-bound NPCs: returns 1 if already at or snaps the
 * NPC back to its spawn. Returns 0 when unfrozen or no 0x80000 flag.
 */
static __attribute__((unused)) int
CNPC_ReturnToSpawnIfFrozen(CNPC *npc)
{
	// NPCs repurpose CResourceEntity.nextInContainer (0x10) as the
	// spawn CLocation.
	CLocation *spawnLoc = (CLocation *)&npc->mobile.container.item.resourceEntity.nextInContainer;
	CLocation *entLoc = &npc->mobile.container.item.resourceEntity.entity.location;

	if (!((int (*)(void *, uint32_t))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 0x80000))
		return 0;

	if (!CNPC_IsFrozen(npc))
		return 0;

	if (CLocation_IsEqualXYZ(spawnLoc, entLoc))
		return 1;

	CNPC_RelocateToSpawn(npc, spawnLoc);
	npc->npcAITarget = 0;
	npc->effectCheckCounter = 0;
	return 1;
}

/*
 * 0x00482733 - CNPC::Heartbeat
 *
 * Main per-NPC tick. Resource init if npcSearchRange == 0xFF,
 * vendor restock, freeze/sleep optimization, AI timer decrements,
 * wander timer, then CNPC_HandleStates.
 *
 * CUSTOM (FEAT_ECOLOGY): runs the hunger lifecycle the binary initialises
 * but never advances.
 */
int
CNPC_Heartbeat(CNPC *npc)
{
	CMobile *mob;
	int count;

	mob = &npc->mobile;

	if (npc->npcSearchRange == 0xFF)
		CNPC_InitFromResourceNodes(npc);

	if (VT_IsVendor((CItem *)npc)) {
		npc->restockCounter--;
		if ((int32_t)npc->restockCounter < 0)
			CMobile_VendorRestockTick(mob);
	}

	// Frozen NPCs skip AI until the freeze timer expires or
	// OnPlayerEnteredRange wakes them.
	if (CNPC_IsFrozen(npc)) {
		npc->npcFreezeTimer--;
		if (npc->npcFreezeTimer > 0)
			return 0;
		npc->npcFreezeTimer = 6;
	} else {
		count = CountPlayersInRange(&mob->container.item.resourceEntity.entity.location, 18);
		if (count <= 0)
			CNPC_SetSleeping(npc, 0);
	}

	npc->npcCombatTarget--;
	if ((int32_t)npc->npcCombatTarget < 0) {
		npc->npcCombatTarget = g_npcAITimerReset; // 0x0061D67C
		npc->speechCounter = 0x10;
	}

	npc->npcInfo1_0--;
	if ((int32_t)npc->npcInfo1_0 < 1) {
		npc->npcInfo1_0 = 0x190; // 400 ticks
		npc->npcAITarget = 0;
		npc->effectCheckCounter = 0;
		if ((int32_t)npc->npcInfo1_1 > 0)
			npc->npcInfo1_1--;
	}

	if (feat(FEAT_ECOLOGY)) {
		// Hunger lifecycle (CUSTOM). The binary initializes stomach to
		// hungerCapacity but never decrements or refills it, so its food
		// AI stays dormant. We drive the full loop here, but only for
		// creatures that actually eat: isEater is true only when the
		// template carries a food (type-0) appetite node.
		// CNPC_InitFromResourceNodes defaults hungerCapacity to 99 even
		// for creatures with no food node (undead, elementals, summoned),
		// so gating on hungerCapacity would wrongly starve them - gate on
		// the appetite node instead. Carnivores refill via
		// CNPC_FindFoodInPack (meat corpses); herbivores/foragers refill
		// via CNPC_GrazeHandler (chunk-egg vegetation).
		int isEater = CNPC_HasFoodAppetite(npc);

		// Hunger decay. CNPCHash_HeartbeatTick visits 8 of 64 hash buckets
		// per call (every 8 global ticks = 2s), so each NPC's Heartbeat
		// fires every ~16s. Decrement every 20th tickCount (~320s).
		if (isEater && (npc->tickCount % 20) == 0 && npc->mobile.stomach > 0)
			npc->mobile.stomach--;

		// Starvation (Raph Koster's design: unfed creatures starve, so the
		// population loop self-regulates). Once an eater's stomach empties
		// and it has found no food, it bleeds HP each heartbeat and dies
		// through the normal death path, leaving a corpse for scavengers.
		// Exempt: non-eaters (no food appetite, via isEater), tamed pets
		// (player owner), human-bodied NPCs (townsfolk/vendors/guards),
		// and invulnerable mobiles. Damage is maxHp/8 (floor 1) so death
		// takes ~8 starving heartbeats regardless of creature size; a fed
		// creature refills before reaching 0.
		if (isEater && npc->mobile.stomach == 0 && !CMobile_IsInvulnerable(mob) && !CMobile_IsHumanBodyType(mob) &&
		        (mob->owner == NULL || !VT_IsPlayer((CItem *)mob->owner))) {
			int dmg = (int)mob->maxHp / 8;
			if (dmg < 1)
				dmg = 1;
			if ((int)mob->hp <= dmg) {
				((void (*)(void *, void *, int))VT_FN((CItem *)npc, VT_ON_DEATH_WRAP))(npc, NULL, 1);
				((void (*)(void *))VT_FN((CItem *)npc, VT_DELETE))(npc);
				return 0;
			}
			((uint32_t (*)(void *, int, int))VT_FN((CItem *)npc, VT_SET_HP))(npc, (int)mob->hp - dmg, 0);
		}
	}

	CNPC_HandleStates(npc);

	return 0;
}

/*
 * 0x004828B0 - GetNPCStateString
 *
 * Maps an NPC AI state id to a human-readable label for logging and
 * diagnostics.
 */
const char *
GetNPCStateString(int state)
{
	switch (state) {
	case NPC_STATE_SEEK_FOOD:
		return "Seek Food";
	case NPC_STATE_SEEK_SHELTER:
		return "Seek Shelter";
	case NPC_STATE_PURSE_SHELTER:
		return "Purse Shelter";
	case NPC_STATE_SEEK_DESIRES:
		return "Seek Desires";
	case NPC_STATE_PURSE_DESIRES:
		return "Purse Desires";
	case NPC_STATE_EAT_FOOD:
		return "Eat Food";
	case NPC_STATE_LOITER:
		return "Loiter";
	case NPC_STATE_RUNAWAY:
		return "Runaway";
	case NPC_STATE_TALKING:
		return "Talking";
	case NPC_STATE_ATTACK_TARGET:
		return "Attack Target";
	case NPC_STATE_IDLE:
		return "Idle";
	case NPC_STATE_WANDER:
		return "Wander";
	case NPC_STATE_SLEEP:
		return "Sleep";
	case NPC_STATE_FOLLOWING:
		return "Following";
	default:
		return "Unknown State";
	}
}

/*
 * 0x0048296D - CNPC::WalkToward
 *
 * Combat teleport fallback: finds a spawn spot near loc, hides the
 * NPC, drops it there, and plays teleport FX at both ends. Only runs
 * for CNPC in ATTACK_TARGET state; shopkeepers return 0.
 */
static int
CNPC_WalkToward(CNPC *npc, CLocation *loc)
{
	CLocation localLoc;
	CLocation prevLoc;

	if (!((int (*)(void *))VT_FN((CItem *)npc, VT_CHECK_EC))(npc))
		return 0;

	if (npc->aiState != 9)
		return 0;

	CLocation_CopyFrom(&localLoc, loc);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)localLoc.x, (int)localLoc.y))
		return 0;

	if (!CBlockManager_FindSpawnSpotExt(&localLoc, (int)loc->z - 2, (int)loc->z + 2, 0, 3, ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_HEIGHT))(npc),
	            ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_MOVEMENT_TYPE))(npc) & 0xFF, (CItem *)npc))
		return 0;

	CLocation_CopyFrom(&prevLoc, ((CLocation * (*)(void *)) VT_FN((CItem *)npc, VT_GET_LOCATION))(npc));

	((void (*)(void *))VT_FN((CItem *)npc, VT_HIDE))(npc);
	((void (*)(void *, CLocation *))VT_FN((CItem *)npc, VT_DROP_AT_FEET))(npc, &localLoc);
	Script_doLocAnimation(&prevLoc, 0x3728, 10, 10, 0, 0);
	Script_doLocAnimation(&localLoc, 0x3728, 10, 10, 0, 0);
	Script_sfx(&localLoc, 0x1FE, 0);

	return 1;
}

/*
 * 0x00482A9C - NPC_AIMovePre
 *
 * Empty hook called once per AIMoveTick before state dispatch.
 */
static void
NPC_AIMovePre(CNPC *npc)
{
	USED(npc);
}

/*
 * 0x00482AA1 - CNPC::AIMoveTick
 *
 * Per-tick NPC movement driver. Accumulates speed into moveSpeedAccum
 * based on dexterity, state, and HP ratio; steps once per 100 accum
 * and dispatches state-based behavior (eat, chase, follow, wander).
 */
void
CNPC_AIMoveTick(CNPC *npc)
{
	CMobile *mob;
	CItem *followTarget;
	int speedPct;
	int followClose;
	int speed;
	int curHp;
	int maxHp;
	int adjusted;
	int minSpeed;
	int defconRate;
	int walkResult;
	int dist;
	int followDist;
	char nameBuf[128];
	uint32_t tmpDir;

	mob = &npc->mobile;

	if (((int (*)(void *))VT_FN((CItem *)mob, VT_CHECK_EC))((CItem *)mob)) {
		if (CNPC_SfxCheck(npc) == 1)
			return;
	}

	if (npc->aiDelayCounter != 0) {
		npc->aiDelayCounter--;
		return;
	}

	NPC_AIMovePre(npc);

	if (npc->aiState == NPC_STATE_SLEEP) {
		CNPC_WanderCountdown(npc);
		return;
	}

	if (npc->aiState != NPC_STATE_TALKING && npc->aiState != NPC_STATE_FOLLOWING) {
		if (CNPC_IsFrozen(npc))
			return;
	}

	speedPct = 30;
	followClose = 0;

	if (npc->behaviorFlags & 0x1000) {
		((void (*)(void *, uint32_t))VT_FN((CItem *)mob, VT_SET_STAMINA))(mob, CMobile_GetMaxStamina(mob));

		followTarget = CWorld_FindBySerial(g_World, npc->followObj1);
		if (followTarget != NULL) {
			dist = Location_WrappedChebyshevDistance(
			        CEntity_GetLocation(&mob->container.item.resourceEntity.entity), CEntity_GetLocation(&followTarget->resourceEntity.entity));
			if (dist > 2) {
				followClose = 1;
				speedPct = 100;
			}
		}
	} else {
		switch (npc->aiByte3) {
		case 1:
			speedPct = 80;
			break;
		case 2:
			speedPct = 80;
			break;
		default:
			break;
		}

		switch (npc->aiState) {
		case NPC_STATE_LOITER: // 6
			speedPct = 18;
			break;
		case NPC_STATE_RUNAWAY: // 7
			speedPct = 100;
			break;
		case NPC_STATE_ATTACK_TARGET: // 9
			speedPct = 50;
			break;
		default:
			break;
		}
	}

	speed = ((int (*)(void *))VT_FN((CItem *)mob, VT_GET_SPEED))(mob);
	speed = speed * speedPct / 100;

	curHp = CMobile_GetHp(mob);
	maxHp = CMobile_GetMaxHp(mob);
	if (curHp != (int)maxHp) {
		if (maxHp == 0)
			maxHp = 1;
		adjusted = speed * curHp / (int)maxHp;
		minSpeed = speed / 4;
		if (adjusted < minSpeed)
			adjusted = minSpeed;
		speed = adjusted;
	}

	if (followClose == 0 && (npc->behaviorFlags & 0x100000) && npc->aiState != NPC_STATE_ATTACK_TARGET) {
		if (speed > 0x23)
			speed = 0x23;
	}

	if (speed > 0x58) {
		tmpDir = mob->direction | 0x80;
	} else {
		tmpDir = mob->direction & 0x7F;
	}
	mob->direction = (uint8_t)tmpDir;

	if (!((int (*)(void *))VT_FN((CItem *)mob, VT_CHECK_EC))((CItem *)mob)) {
		if (!(npc->behaviorFlags & 0x1000)) {
			defconRate = CDefcon_GetMoveRate(&g_Defcon);
			if (speed > defconRate)
				speed = defconRate;
		}
	}

	mob->moveSpeedAccum += speed;

	while ((int)mob->moveSpeedAccum >= 100) {
		mob->moveSpeedAccum -= 100;

		if (npc->pathArray != 0) {
			NPC_PathWalk(npc);
			return;
		}

		if (npc->aiState == NPC_STATE_EAT_FOOD) {
			if (!((int (*)(void *, int))VT_FN((CItem *)mob, VT_TEST_BEHAVIOR))(mob, 2)) {
				CNPC_FoodTransition(npc);
				if (npc != g_currentNPC)
					return;
			} else {
				CNPC_SetState(npc, NPC_STATE_IDLE);
			}
			goto after_state_dispatch;
		}

		if (npc->aiState == NPC_STATE_ATTACK_TARGET) {
			if (!((int (*)(void *, int))VT_FN((CItem *)mob, VT_TEST_BEHAVIOR))(mob, 0x40)) {
				CNPC_CombatChase(npc);
				if (npc != g_currentNPC)
					return;
			} else {
				CNPC_SetState(npc, NPC_STATE_IDLE);
			}
		}

after_state_dispatch:
		if (((int (*)(void *, int))VT_FN((CItem *)mob, VT_TEST_BEHAVIOR))(mob, 1)) {
			// NPC has movement disabled, but allow FOLLOWING and TALKING
			if (npc->aiState != NPC_STATE_FOLLOWING && npc->aiState != NPC_STATE_TALKING)
				return;
		}

		if (npc->aiState == NPC_STATE_FOLLOWING) {
			followTarget = (CItem *)mob->owner;
			if (mob->isFollower == 0) {
				followTarget = CWorld_FindBySerial(g_World, npc->followObj1);
			}
			if (followTarget == NULL)
				goto no_walking;

			followDist = 2;
			if (VT_IsNPC(followTarget)) {
				if (CMobile_IsHumanNPC((CMobile *)followTarget))
					followDist = 4;
			}

			dist = Location_WrappedChebyshevDistance(CItem_GetLocationVT(followTarget), CItem_GetLocationVT((CItem *)mob));
			if (dist < followDist) {
				npc->isWalking = 0;
				return;
			}

			npc->isWalking = 1;
			CLocation_SetLoc(&npc->patrolTarget, CItem_GetLocationVT(followTarget));
		}

no_walking:
		if (npc->isWalking == 0)
			goto tick_end;

		g_npcMoveTargetEnt = NULL;
		npc->aiByte2 = 0;

		walkResult = CNPC_WalkToPatrolTarget(npc, 1, 0);
		if (npc != g_currentNPC)
			return;

		dist = Location_WrappedChebyshevDistance(&npc->patrolTarget, CItem_GetLocationVT((CItem *)mob));

		if (walkResult == 0 || dist <= 1)
			goto walk_done;

		if (CNPC_WalkToward(npc, &npc->patrolTarget) == 1) {
			walkResult = CNPC_WalkToPatrolTarget(npc, 1, 0);
			if (npc != g_currentNPC)
				return;
		}

walk_done:
		if (walkResult == 0)
			goto tick_end;

		npc->isWalking = 0;

		if (!CLocation_IsInvalid(&npc->patrolTarget)) {
			dist = Location_WrappedChebyshevDistance(&npc->patrolTarget, CItem_GetLocationVT((CItem *)mob));
		}

		if (dist > 1) {
			if (CNPC_ShouldProcess(npc)) {
				strcpy(nameBuf, "Unknown");
				memset(nameBuf + 8, 0, 0x78);

				if (g_npcMoveTargetEnt != NULL) {
					if (((int (*)(void *))VT_ENT_FN(&g_npcMoveTargetEnt->resourceEntity.entity, VT_IS_CONTAINER))(g_npcMoveTargetEnt)) {
						sprintf(nameBuf, "%s (%d)",
						        ((char *(*)(void *))VT_ENT_FN(&g_npcMoveTargetEnt->resourceEntity.entity, VT_GET_NAME))(g_npcMoveTargetEnt),
						        g_npcMoveTargetEnt->serial);
					} else {
						strcpy(nameBuf, ((char *(*)(void *))VT_ENT_FN(&g_npcMoveTargetEnt->resourceEntity.entity, VT_GET_NAME))(g_npcMoveTargetEnt));
					}
				}
			}

			npc->aiByte2 = 1;
			CNPC_UpdateCombatInfo(npc);

			if (npc->aiState == NPC_STATE_LOITER || npc->aiState == NPC_STATE_WANDER) {
				CNPC_StartWander(npc, GetRandomRange(15, 30), npc->stateInfo2);
			} else {
				CNPC_SetState(npc, npc->stateInfo2);
			}

			if (npc->stateInfo2 == NPC_STATE_WANDER)
				npc->lastWanderQuadrant ^= 2;

			goto run_states;
		}

		if (npc->ltype != npc->aiState)
			CNPC_ShouldProcess(npc);

		if (npc->aiState == NPC_STATE_LOITER || npc->aiState == NPC_STATE_WANDER) {
			CNPC_StartWander(npc, GetRandomRange(15, 30), npc->ltype);
		} else {
			CNPC_SetState(npc, npc->ltype);
		}

run_states:
		CNPC_HandleStates(npc);
		if (npc != g_currentNPC)
			return;

tick_end:
		if (followClose != 0) {
			((void (*)(void *, uint32_t))VT_FN((CItem *)mob, VT_SET_STAMINA))(mob, CMobile_GetMaxStamina(mob));
		}
	}
}

/*
 * 0x00483386 - CMobile::OffsetPastMulti
 *
 * For a mobile standing on a multi, walks loc clear of that multi's
 * footprint: one axis by the multi's width, the other by its height,
 * both chosen from the mobile's facing. Cardinal facings only move
 * along one axis; the diagonals move along both. No-op when the mobile
 * is not on a multi.
 */
static __attribute__((unused)) void
CMobile_OffsetPastMulti(CMobile *this, CLocation *loc)
{
	CLocation minLoc, maxLoc;
	int typeId, width, height;
	int stepsX, stepsY, dirX, dirY;
	int dir;

	if (CItem_GetMultiSlave((CItem *)this) == NULL)
		return;

	dir = this->direction & 0x7F;
	typeId = CMultiSlave_GetTypeId(CItem_GetMultiSlave((CItem *)this));

	CLocation_Init(&minLoc);
	CLocation_Init(&maxLoc);
	CMultiManager_GetExtents(&g_MultiManager, typeId, &minLoc, &maxLoc);

	width = (int16_t)maxLoc.x - (int16_t)minLoc.x + 1;
	height = (int16_t)maxLoc.y - (int16_t)minLoc.y + 1;

	stepsX = 0;
	stepsY = 0;
	switch (dir) {
	case 0:
	case 4:
		stepsY = height;
		break;
	case 2:
	case 6:
		stepsX = width;
		break;
	default:
		stepsX = width;
		stepsY = height;
		break;
	}

	dirX = 0;
	dirY = 0;
	switch (dir) {
	case 0:
		dirY = 0;
		break;
	case 1:
		dirX = 2;
		dirY = 0;
		break;
	case 2:
		dirX = 2;
		break;
	case 3:
		dirX = 2;
		dirY = 4;
		break;
	case 4:
		dirY = 4;
		break;
	case 5:
		dirX = 6;
		dirY = 4;
		break;
	case 6:
		dirX = 6;
		break;
	case 7:
		dirX = 6;
		dirY = 0;
		break;
	}

	for (; stepsX > 0; stepsX--)
		CLocation_MoveDir(loc, dirX);

	for (; stepsY > 0; stepsY--)
		CLocation_MoveDir(loc, dirY);
}

/*
 * 0x0048356D - CNPC vtable[0x204] PaperdollTitle
 *
 * Writes "<name> the <job>" into title, omitting the suffix when
 * the job is "no job".
 */
void
CNPC_PaperdollTitle_VT(CNPC *npc, CString *title)
{
	const char *name;

	name = ((const char *(*)(void *))VT_FN((CItem *)npc, VT_GET_NAME))(npc);
	CString_AssignCStr(title, name);

	if (strcasecmp(npc->npcJob, "no job") != 0) {
		CString_AppendCStr(title, " the ");
		CString_AppendCStr(title, npc->npcJob);
	}
}

/*
 * 0x004835C8 - CResourceMobile vtable[0x140] SetSerial
 *
 * CResourceMobile override of VT_SET_SERIAL. Removes item from old
 * CItem serial hash and NPC hash, sets new serial, re-inserts into both
 * hash tables at the new bucket.
 */
void
CNPC_SetSerial(CItem *self, uint32_t newSerial)
{
	uint32_t bucket;
	CNPC *npc = (CNPC *)self;

	if (self->hashNext != NULL)
		self->hashNext->hashPrev = self->hashPrev;
	if (self->hashPrev != NULL) {
		self->hashPrev->hashNext = self->hashNext;
	} else {
		bucket = self->serial & 0xFFFF;
		g_World->hashTable[bucket] = self->hashNext;
	}

	if (npc->npcHashNext != NULL)
		npc->npcHashNext->npcHashPrev = npc->npcHashPrev;
	if (npc->npcHashPrev != NULL)
		npc->npcHashPrev->npcHashNext = npc->npcHashNext;
	else {
		bucket = self->serial & 0x3F;
		g_NPCHash[bucket] = npc->npcHashNext;
	}

	// The binary calls a no-op Noop_4851F0(serial) here when the serial
	// actually changes; omitted since it has no observable effect, the
	// same treatment CItem_SetSerial gives it.

	self->serial = newSerial;

	CWorld_InsertEntity(g_World, self);

	bucket = newSerial & 0x3F;
	npc->npcHashNext = g_NPCHash[bucket];
	if (npc->npcHashNext != NULL)
		npc->npcHashNext->npcHashPrev = npc;
	npc->npcHashPrev = NULL;
	g_NPCHash[bucket] = npc;
}

/*
 * 0x00483762 - CNPC::RemoveFromWorldGrid
 *
 * Detaches the NPC from spatial chains, sends DESTROY_OBJECT to nearby
 * viewers, and marks it removed from the world.
 */
void
CNPC_RemoveFromWorldGrid(CItem *this)
{
	uint8_t pktBuf[0x14];
	CItem *parent;
	int blockIdx;

	if (this->parent == NULL)
		CMobile_DecrNormalNPCCount((CMobile *)this);

	CItem_AdjustParentWeight(this, this->parent);
	CNPCManager_RemoveFromSpatialMap(this);
	CItem_NotifyMultiDetach(this, 1);
	CResourceEntity_NotifyPreModify(this);
	Block_RemoveTrackingNode(this);
	CItem_UpdateContainInfo(this, 0);
	PacketManager_MakePacket_DESTROY_OBJECT(pktBuf, this->serial);

	if (g_WorldActive && !CItem_IsServerOnly(this)) {
		SendPacketInRange(pktBuf, &this->resourceEntity.entity.location, 0x12);
	}

	if (this->parent != NULL) {
		parent = this->parent;
		if (this->spatialNext != NULL)
			this->spatialNext->spatialPrev = this->spatialPrev;
		if (this->spatialPrev != NULL) {
			this->spatialPrev->spatialNext = this->spatialNext;
		} else {
			if (((CContainer *)parent)->contents == this)
				((CContainer *)parent)->contents = this->spatialNext;
		}
		this->spatialPrev = NULL;
		this->spatialNext = NULL;
	} else {
		if (this->spatialNext != NULL)
			this->spatialNext->spatialPrev = this->spatialPrev;
		if (this->spatialPrev != NULL) {
			this->spatialPrev->spatialNext = this->spatialNext;
		} else {
			blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &this->resourceEntity.entity.location, 0);
			g_SpatialGrid.cells[blockIdx].itemHead = this->spatialNext;
		}
		this->spatialPrev = NULL;
		this->spatialNext = NULL;
	}

	this->parent = NULL;
	this->resourceEntity.entity.removedFromWorld = 1;
}

/*
 * 0x004838B7 - CMobile::IncrNormalNPCCount
 *
 * Bumps g_NormalNPCCount when the mobile is not invulnerable.
 */
void
CMobile_IncrNormalNPCCount(CMobile *mob)
{
	if (!CMobile_IsInvulnerable(mob))
		g_NormalNPCCount++;
}

/*
 * 0x004838DB - CMobile::DecrNormalNPCCount
 *
 * Drops g_NormalNPCCount when the mobile is not invulnerable.
 */
void
CMobile_DecrNormalNPCCount(CMobile *mob)
{
	if (!CMobile_IsInvulnerable(mob))
		g_NormalNPCCount--;
}

/*
 * 0x00483931 - CResourceMobile vtable[0x04] DropAtFeet
 *
 * Places the NPC at loc in the world: seeds the spawn location on
 * first drop, moves and un-hides the entity, notifies nearby players,
 * rejoins the spatial map, and bumps the NPC count.
 */
void
CResourceMobile_DropAtFeet_VT(CItem *self, CLocation *loc)
{
	CVector nearbyList;
	// Binary offset 0x10: nextInContainer repurposed as spawn CLocation
	CLocation *spawnLoc = (CLocation *)&self->resourceEntity.nextInContainer;

	if ((int16_t)spawnLoc->x == -1) {
		CLocation_SetLoc(spawnLoc, loc);
	}

	CItem_InternalMove(self, loc, 0);
	self->resourceEntity.entity.removedFromWorld = 0;
	UpdateRegion(self);

	if (!VT_IsHidden(self)) {
		CVector_Constructor(&nearbyList, "\x01");
		GetNearbyPlayers(&nearbyList, &self->resourceEntity.entity.location, 0x12);
		((void (*)(CItem *, CVector *, int))VT_FN(self, VT_NOTIFY_NEARBY))(self, &nearbyList, 1);
		CVector_Destructor(&nearbyList);
	}

	CResourceEntity_NotifyPostModifyIfActive(self);
	CNPCManager_AddToSpatialMap(self);
	CItem_ReleaseTracking(self);
	CMobile_IncrNormalNPCCount((CMobile *)self);
}

/*
 * 0x00483A1E - CResourceMobile vtable[0x08] SetLocation
 *
 * Seeds the spawn location on first move, delegates to
 * CMobile_SetLocation_VT, and rejoins the spatial map.
 */
void
CResourceMobile_SetLocation_VT(CItem *self, CLocation *loc)
{
	// Binary offset 0x10: nextInContainer repurposed as spawn CLocation
	CLocation *spawnLoc = (CLocation *)&self->resourceEntity.nextInContainer;

	if ((int16_t)spawnLoc->x == -1) {
		CLocation_SetLoc(spawnLoc, loc);
	}

	CMobile_SetLocation_VT(self, loc);
	CNPCManager_AddToSpatialMap(self);
	CMobile_IncrNormalNPCCount((CMobile *)self);
}

/*
 * 0x00483A68 - CNPC::GetLoyalty
 *
 * Returns hungerMod + attackMode clamped to [-127, 127].
 */
int
CNPC_GetLoyalty(CNPC *npc)
{
	int attackVal;
	int hungerMod;
	int unk;
	int val;

	attackVal = (int)npc->mobile.attackMode;
	hungerMod = 2 * CNPC_GetHungerLevel(npc) - 100;
	unk = 0;
	val = hungerMod + attackVal - unk;
	if (val > 127)
		val = 127;
	if (val < -127)
		val = -127;
	return val;
}

/*
 * 0x00483AC2 - CNPC::WalkToLocation
 *
 * Runs A* pathfinding setup to loc; on success stores range in
 * pathSpeed and returns 1, otherwise fires event 0x20 and returns 0.
 * The flag arg is unused.
 */
int
CNPC_WalkToLocation(CItem *mob, int range, CLocation *loc, int flag)
{
	CNPC *npc = (CNPC *)mob;

	USED(flag);

	CNPC_SetupPath(npc, loc, 0x200);

	if (npc->pathArray != 0) {
		npc->pathSpeed = range;
		return 1;
	}

	if (range != -1)
		Entity_ExecuteEvent(&mob->resourceEntity.entity, 0x20, range);

	return 0;
}

/*
 * 0x00483B19 - CNPC::AddFragment
 *
 * Appends name to the NPC's convo-fragment list, allocating the
 * std::list on first use. StdPtrList16_InsertEnd copy-constructs the
 * string into the new node; the sentinel's own value is left
 * unconstructed, which is what lets the destructor free it raw.
 */
void
CNPC_AddFragment(CNPC *npc, CString *name)
{
	void *mem;
	void *init;

	if (npc->convoFragList == NULL) {
		mem = OperatorNew(sizeof(CFragmentList));
		if (mem != NULL)
			npc->convoFragList = (CFragmentList *)StdPtrList16_Constructor((StdPtrList *)mem, &init);
		else
			npc->convoFragList = NULL;
	}

	StdPtrList16_InsertEnd((StdPtrList *)npc->convoFragList, name);
}

/*
 * 0x00483BB0 - CNPC::RemoveFragment
 *
 * Removes every entry matching name (case-insensitive) from the
 * convo-fragment list, destroying the list when it empties.
 */
void
CNPC_RemoveFragment(CNPC *npc, CString *name)
{
	StdPtrList *list;
	StdPtrNode *iter, *first, *last, *findTemp, *endTemp, *eraseTemp;

	if (npc->convoFragList == NULL)
		return;

	list = (StdPtrList *)npc->convoFragList;
	StdPtrIter_BaseConstructor(&iter);

	do {
		last = *StdPtrList_End(list, &endTemp);
		first = *StdPtrList_Begin(list, &findTemp);
		iter = *StdPtrList_FindString(&findTemp, &first, &last, name);

		if (StdPtrIter_Neq(&iter, StdPtrList_End(list, &endTemp)) & 0xFF)
			StdPtrList_Erase16(list, &eraseTemp, iter);
	} while (StdPtrIter_Neq(&iter, StdPtrList_End(list, &endTemp)) & 0xFF);

	if (StdList_GetSize(list) == 0) {
		StdPtrList_ScalarDelete_NPC(list, 1);
		npc->convoFragList = NULL;
	}
}

/*
 * 0x00483CC5 - CNPC::ShouldFlee
 *
 * True when flee is enabled (npcFlee != 0xFF) and HP is at or below
 * the maxHp * npcFlee / 100 threshold.
 */
static int
CNPC_ShouldFlee(CNPC *npc)
{
	uint32_t flee;
	uint32_t threshold;

	flee = npc->npcFlee;
	if (flee == 0xFF)
		return 0;

	threshold = CMobile_GetMaxHp(&npc->mobile) * flee / 100;
	if (CMobile_GetHp(&npc->mobile) <= threshold)
		return 1;
	return 0;
}

/*
 * 0x00483D20 - CNPC::CanFlee
 *
 * True when flee is enabled (npcFlee != 0xFF).
 */
static int
CNPC_CanFlee(CNPC *npc)
{
	if (npc->npcFlee == 0xFF)
		return 0;
	return 1;
}

/*
 * 0x00483D5B - CNPC::SetNPCFlee
 *
 * Stores value into the NPC's npcFlee field.
 */
void
CNPC_SetNPCFlee(CNPC *npc, uint8_t value)
{
	npc->npcFlee = value;
}

/*
 * 0x00483D92 - CNPC::SetCombatTarget
 *
 * Stores serial into the NPC's npcAITarget field.
 */
void
CNPC_SetCombatTarget(CNPC *npc, uint32_t serial)
{
	npc->npcAITarget = serial;
}

/*
 * 0x00483DAB - CNPC::SetEffectCheckCounter
 *
 * Stores serial into the NPC's effectCheckCounter field.
 */
static void
CNPC_SetEffectCheckCounter(CNPC *npc, uint32_t serial)
{
	npc->effectCheckCounter = serial;
}

/*
 * 0x00483DE8 - CNPC::UpdateCombatInfo
 *
 * Syncs effectCheckCounter with the AI target while attacking that
 * same target.
 */
static void
CNPC_UpdateCombatInfo(CNPC *npc)
{
	if (npc->aiState != NPC_STATE_ATTACK_TARGET)
		return;
	if (npc->actionTarget != npc->npcAITarget)
		return;
	CNPC_SetEffectCheckCounter(npc, npc->npcAITarget);
}

/*
 * 0x00483E25 - CNPCManager::AddToSpatialMap
 *
 * Inserts a parentless NPC into g_NPCMap at its current location.
 */
void
CNPCManager_AddToSpatialMap(CItem *entity)
{
	if (entity->parent != NULL)
		return;
	CEntityMap_Insert(g_NPCMap, entity, (int)(int16_t)entity->resourceEntity.entity.location.x, (int)(int16_t)entity->resourceEntity.entity.location.y);
}

/*
 * 0x00483E5D - CNPCManager::RemoveFromSpatialMap
 *
 * Removes a parentless NPC from g_NPCMap at its current location.
 */
void
CNPCManager_RemoveFromSpatialMap(CItem *entity)
{
	if (entity->parent != NULL)
		return;
	CEntityMap_Remove(g_NPCMap, entity, (int)(int16_t)entity->resourceEntity.entity.location.x, (int)(int16_t)entity->resourceEntity.entity.location.y);
}

/*
 * 0x00483E95 - CNPC::SetSleeping
 *
 * wake != 0: thaw a frozen NPC and queue it onto the active list.
 * wake == 0: freeze the NPC if not already frozen.
 */
void
CNPC_SetSleeping(CNPC *npc, int wake)
{
	if (wake) {
		if (CNPC_IsFrozen(npc)) {
			CNPC_ClearFrozen(npc);
			CNPCManager_AddToActiveList(CMobile_GetSerial(&npc->mobile));
		}
	} else {
		if (!CNPC_IsFrozen(npc))
			CNPC_SetFrozen(npc);
	}
}

/*
 * 0x00483EE5 - CNPC::IsFrozen
 *
 * Returns 1 when the NPC carries the frozen behavior bit (0x40000).
 */
int
CNPC_IsFrozen(CNPC *npc)
{
	return ((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 0x40000);
}

/*
 * 0x00483F03 - CNPC::SetFrozen
 *
 * Starts a 6-tick freeze countdown and sets the frozen behavior flag.
 */
void
CNPC_SetFrozen(CNPC *npc)
{
	npc->npcFreezeTimer = 6;
	((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x40000);
}

/*
 * 0x00483F2D - CNPC::ClearFrozen
 *
 * Clears the frozen behavior bit (0x40000) on the NPC.
 */
void
CNPC_ClearFrozen(CNPC *npc)
{
	((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x40000);
}

/*
 * 0x00483F4B - CNPCManager::AddToActiveList
 *
 * Queues a serial onto the deferred-heartbeat list that
 * CNPCManager::DrainActiveList pops from.
 */
void
CNPCManager_AddToActiveList(uint32_t serial)
{
	CVector_PushBack(&g_NPCActiveList, serial);
}

/*
 * 0x00483F6A - CNPCManager::DrainActiveList
 *
 * Pops each queued serial from g_NPCActiveList and runs its heartbeat.
 */
static void
CNPCManager_DrainActiveList(void)
{
	uint32_t serial;
	CNPC *npc;

	while (CVector_GetCount(&g_NPCActiveList) > 0) {
		serial = (uint32_t)*((uintptr_t *)g_NPCActiveList.end - 1);
		npc = (CNPC *)CWorld_FindNPCBySerial(g_World, serial);
		if (npc != NULL)
			CNPC_Heartbeat(npc);
		CVector_EraseBack(&g_NPCActiveList);
	}
}

/*
 * 0x00483FC5 - CNPCHash::CollectSerials
 *
 * Appends the serial of every hashed NPC matching filter to out.
 * filter 1 keeps vendors (vtable[0xE8]), 2 keeps entities passing
 * vtable[0xEC], anything else keeps all. The chain pointer is parked in
 * g_NPCHashIterNext so an NPC can unhash itself mid-walk.
 */
static __attribute__((unused)) void
CNPCHash_CollectSerials(CVector *out, int filter)
{
	CNPC *npc;
	uint32_t serial;
	int i, match;

	for (i = 0; i < 0x40; i++) {
		npc = g_NPCHash[i];
		while (npc != NULL) {
			g_NPCHashIterNext = npc->npcHashNext;
			match = 0;
			if (filter == 1) {
				if (((int (*)(void *))VT_FN((CItem *)npc, VT_IS_VENDOR))(npc))
					match = 1;
			} else if (filter == 2) {
				if (((int (*)(void *))VT_FN((CItem *)npc, VT_CHECK_EC))(npc))
					match = 1;
			} else {
				match = 1;
			}

			if (match) {
				serial = CMobile_GetSerial(&npc->mobile);
				CVector_PushBack(out, serial);
			}
			npc = g_NPCHashIterNext;
		}
	}
}

/*
 * 0x004840A5 - CNPC::HasVendorScript
 *
 * True when the "vendor" script is attached to this NPC.
 */
static __attribute__((unused)) int
CNPC_HasVendorScript(CNPC *this)
{
	CString name;
	int found;

	CString_Constructor(&name, "vendor");
	found = CItem_HasScript((CItem *)this, &name);
	CString_Destructor(&name);

	return found != 0;
}

/*
 * 0x00484112 - CNPC::HasStablesScript
 *
 * True when the "stables" script is attached to this NPC.
 */
static __attribute__((unused)) int
CNPC_HasStablesScript(CNPC *this)
{
	CString name;
	int found;

	CString_Constructor(&name, "stables");
	found = CItem_HasScript((CItem *)this, &name);
	CString_Destructor(&name);

	return found != 0;
}

/*
 * 0x0048417F - CNPC::HasBankerScript
 *
 * True when the "banker" script is attached to this NPC.
 */
static __attribute__((unused)) int
CNPC_HasBankerScript(CNPC *this)
{
	CString name;
	int found;

	CString_Constructor(&name, "banker");
	found = CItem_HasScript((CItem *)this, &name);
	CString_Destructor(&name);

	return found != 0;
}

/*
 * 0x004841EC - CNPC::CheckArmageddon
 *
 * MODIFIED: fires @armageddon (event 0x16) on the NPC with {0, flag} as
 * the sole list entry. Where the binary uses SEH around CList cleanup,
 * this version uses explicit free.
 */
char *
CNPC_CheckArmageddon(CNPC *npc, int flag)
{
	CList *list;
	char *result;

	list = (CList *)OperatorNew(sizeof(CList));
	if (list != NULL)
		CList_Constructor(list);

	CList_Append(list, 0, flag);
	result = Entity_ExecuteEvent(&npc->mobile.container.item.resourceEntity.entity, 0x16, 0, "armageddon", "x", list);

	if (list != NULL)
		CList_ScalarDelete(list, 1);

	return result;
}

/*
 * 0x004842B5 - CNPC::OnPlayerEnteredRange
 *
 * Wakes the NPC and re-evaluates combat when a player enters range.
 */
void
CNPC_OnPlayerEnteredRange(CMobile *npcMob, CMobile *player)
{
	CNPC *npc = (CNPC *)npcMob;

	CNPC_SetSleeping(npc, 1);
	CNPC_CheckEngageCombat(npc, player);
}

/*
 * 0x004842D8 - CNPC vtable[0x230] SetInvulnerable
 *
 * Decr/incr around the base call so the normal-NPC count is
 * recomputed after the invulnerability flag changes.
 */
void
CNPC_SetInvulnerable_VT(CMobile *mob)
{
	CMobile_DecrNormalNPCCount(mob);
	CMobile_SetInvulnerable_VT(mob);
	CMobile_IncrNormalNPCCount(mob);
}

/*
 * 0x004842FB - CNPC vtable[0x234] ClearInvulnerable
 *
 * Decr/incr around the base call so the normal-NPC count is
 * recomputed after the invulnerability flag changes.
 */
void
CNPC_ClearInvulnerable_VT(CMobile *mob)
{
	CMobile_DecrNormalNPCCount(mob);
	CMobile_ClearInvulnerable_VT(mob);
	CMobile_IncrNormalNPCCount(mob);
}

/*
 * 0x00484320 - CNPCManagerBlock::ScalarDelete
 *
 * Scalar deleting destructor: runs the destructor and frees the block when
 * flags & 1.
 */
static void *
CNPCManagerBlock_ScalarDelete(CNPCManagerBlock *this, int flags)
{
	CNPCManagerBlock_Destructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x00484350 - CResourceMobile::ScalarDelete (vtable[0x00])
 *
 * Scalar deleting destructor: runs CResourceMobile_Destructor and frees
 * the NPC when flags & 1.
 */
void *
CResourceMobile_ScalarDelete(CNPC *npc, int flags)
{
	CResourceMobile_Destructor(npc);
	if (flags & 1)
		OperatorDelete(npc);
	return NULL;
}

/*
 * 0x00484380 - StdPtrList::ScalarDelete (NPC variant)
 *
 * Scalar deleting destructor for the NPC-variant StdPtrList: runs the
 * destructor and frees the list when flags & 1.
 */
static void *
StdPtrList_ScalarDelete_NPC(StdPtrList *this, int flags)
{
	StdPtrList_Destructor_NPC(this);
	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x00484400 - CNPCManager::GetCountMinus4
 *
 * Returns the list size minus 4 (sentinel/header slot count).
 */
static __attribute__((unused)) int
CNPCManager_GetCountMinus4(StdPtrList *this)
{
	return StdList_GetSize(this) - 4;
}

/*
 * 0x00484560 - CNPCManagerBlock destructor
 *
 * Releases the block's vector-ptr-list, if any.
 */
static void
CNPCManagerBlock_Destructor(CNPCManagerBlock *this)
{
	if (this->vecPtrList != NULL) {
		StdPtrList16_VecDtor(this->vecPtrList, 3);
	}
}

/*
 * 0x00484C50 - Path_AtStep
 *
 * True when (loc, dir) matches the PathNode's (x, y, z, dir).
 */
static int
Path_AtStep(PathNode *step, CLocation *loc, int dir)
{
	if ((int)(int16_t)loc->x != (int)step->x)
		return 0;
	if ((int)(int16_t)loc->y != (int)step->y)
		return 0;
	if ((int)loc->z != (int)step->z)
		return 0;
	if (dir != (int)step->dir)
		return 0;
	return 1;
}

/*
 * 0x00484CB0 - CNPCManager::GetIterNext
 *
 * Returns the saved iteration-next pointer used to survive node
 * removal during an NPC hash walk.
 */
CNPC *
CNPCManager_GetIterNext(void)
{
	return g_NPCHashIterNext;
}

/*
 * 0x00484CD0 - CNPCManager::SetIterNext
 *
 * Stores val as the saved iteration-next pointer.
 */
void
CNPCManager_SetIterNext(CNPC *val)
{
	g_NPCHashIterNext = val;
}

/*
 * 0x0048B596 - CNPC vtable[0x94] override
 *
 * If movementType == 2 (flying): returns 2 if aiByte3 == 1, else 1.
 * Otherwise delegates to CMobile_GetMovementType.
 */
uint8_t
CNPC_GetMovementType_VT(CNPC *npc)
{
	if (npc->mobile.movementType == 2) {
		if (npc->aiByte3 == 1)
			return 2;
		else
			return 1;
	}
	return CMobile_GetMovementType(&npc->mobile);
}

/*
 * 0x0049DF20 - Path_GetCurrentStep
 *
 * Copies the current path step, masking off the generation bits.
 */
static void
Path_GetCurrentStep(CNPC *npc, PathNode *out)
{
	PathNode *pathArray = (PathNode *)npc->pathArray;
	uint32_t idx = CNPC_GetPathStepsRemaining(npc);

	*out = pathArray[idx];
}

static uint32_t
Path_NextGeneration(CNPC *npc)
{
	return (CNPC_GetPathGeneration(npc) + NPC_PATH_GENERATION_INCREMENT) & NPC_PATH_GENERATION_MASK;
}

static void
Path_Discard(CNPC *npc)
{
	uint32_t nextGeneration = Path_NextGeneration(npc);

	if (npc->pathArray != 0)
		OperatorDelete((void *)npc->pathArray);
	npc->pathArray = 0;
	npc->pathStepsRemaining = nextGeneration;
}

static void
Path_Install(CNPC *npc, PathNode *path, uint32_t stepsRemaining)
{
	npc->pathArray = (uintptr_t)path;
	npc->pathStepsRemaining = CNPC_GetPathGeneration(npc) | (stepsRemaining & NPC_PATH_STEP_MASK);
}

static int
Path_IsCurrent(CNPC *npc, uintptr_t path, uint32_t generation)
{
	return npc->pathArray == path && CNPC_GetPathGeneration(npc) == generation;
}

/*
 * 0x0049DF51 - Path_AdvanceStep
 *
 * Decrements the step index and discards a completed path, retaining
 * the generation used to detect replacement during callbacks.
 */
static void
Path_AdvanceStep(CNPC *npc)
{
	uint32_t stepsRemaining = CNPC_GetPathStepsRemaining(npc);

	if (stepsRemaining <= 1) {
		Path_Discard(npc);
		return;
	}
	npc->pathStepsRemaining = CNPC_GetPathGeneration(npc) | (stepsRemaining - 1);
}

// 0x005EFCE0 - opposite direction table: maps dir d to (d + 4) & 7
static const int g_OppositeDir[8] = { 4, 5, 6, 7, 0, 1, 2, 3 };

/*
 * 0x0049DFA4 - Path step check
 *
 * When the current facing already matches dir, tries to step one tile
 * and writes the resulting node; otherwise just rotates in place.
 * Returns 0 when the step is blocked by terrain or bounds.
 */
static int
Path_StepCheck(CNPC *npc, PathNode *node, int dir, PathNode *result)
{
	CItem *mob = &npc->mobile.container.item;
	int height;
	int newX, newY;
	CLocation newLoc;
	int walkZ;

	if ((int)node->dir != dir) {
		result->x = node->x;
		result->y = node->y;
		result->z = node->z;
		result->dir = (int16_t)dir;
		return 1;
	}

	height = ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF;

	newX = (int)(int16_t)node->x + g_TerrainDirDX[dir];
	newY = (int)(int16_t)node->y + g_TerrainDirDY[dir];
	CLocation_Constructor3D(&newLoc, (int16_t)newX, (int16_t)newY, node->z);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)newLoc.x, (int)(int16_t)newLoc.y))
		return 0;

	walkZ = CTerrainManager_CanWalkWrapper(
	        newLoc, (int)(int16_t)newLoc.z - height, (int)(int16_t)newLoc.z + height, ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF, height, mob, 0);

	if (walkZ == -128)
		return 0;

	PathNode_SetFromLoc(result, &newLoc, (int16_t)dir);
	return 1;
}

/*
 * 0x0049E0C9 - CNPC::SetupPath
 *
 * Breadth-first search over the four cardinal directions with a fixed
 * 512-node open list; stores the traced-back path in npc->pathArray on
 * success and leaves it NULL when no route is found within maxSteps.
 */
void
CNPC_SetupPath(CNPC *npc, CLocation *loc, int maxSteps)
{
	CItem *mob = &npc->mobile.container.item;
	SearchNode nodes[512];
	PathNode resultNode;
	int count;
	int maxDist;
	int i, dir, k;
	int parentCost;
	int newX, newY;
	int pathSize;
	PathNode *pathArray;

	if (maxSteps >= 0x200)
		maxSteps = 0x1FF;

	Path_Discard(npc);

	PathNode_InitFromLoc((PathNode *)&nodes[0], &mob->resourceEntity.entity.location, (int16_t)(((CMobile *)mob)->direction & 7));

	nodes[0].parentIdx = 0;
	nodes[0].cost = 0;
	count = 1;

	maxDist = ChebyshevDistXY(
	                  (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)(int16_t)mob->resourceEntity.entity.location.x, (int)(int16_t)mob->resourceEntity.entity.location.y) +
	          2;

	if (maxDist > 10)
		return;

	for (i = 0; i < count; i++) {
		parentCost = nodes[i].cost;

		for (dir = 0; dir < 8; dir += 2) {
			if ((int)(int16_t)nodes[i].dir == g_OppositeDir[dir])
				continue;

			newX = (int)(int16_t)nodes[i].x + g_TerrainDirDX[dir];
			newY = (int)(int16_t)nodes[i].y + g_TerrainDirDY[dir];

			if (ChebyshevDistXY(newX, newY, (int)(int16_t)loc->x, (int)(int16_t)loc->y) >= maxDist)
				continue;

			for (k = 0; k < count; k++) {
				if ((int)(int16_t)nodes[k].x == newX && (int)(int16_t)nodes[k].y == newY)
					break;
			}

			if (k != count)
				continue;

			if (!Path_StepCheck(npc, (PathNode *)&nodes[i], dir, &resultNode))
				continue;

			PathNode_Copy((PathNode *)&nodes[count], &resultNode);
			nodes[count].parentIdx = i;
			nodes[count].cost = parentCost + 1;
			count++;

			if (ChebyshevDistXY((int)(int16_t)resultNode.x, (int)(int16_t)resultNode.y, (int)(int16_t)loc->x, (int)(int16_t)loc->y) <= 1)
				goto found_path;

			if (count >= maxSteps)
				return;
		}
	}

	return;

found_path:
	pathSize = parentCost + 2;
	pathArray = (PathNode *)OperatorNew(pathSize * sizeof(PathNode));
	if (pathArray == NULL)
		return;

	Path_Install(npc, pathArray, (uint32_t)parentCost);

	k = 0;
	PathNode_Copy(&pathArray[k], &resultNode);
	k++;

	while (parentCost >= 0) {
		PathNode_Copy(&pathArray[k], (PathNode *)&nodes[i]);
		k++;

		i = nodes[i].parentIdx;
		parentCost--;
	}
}

/*
 * 0x0049E590 - SearchNode::IterConstructor
 *
 * Iterator ctor thunk invoked from CNPC::SetupPath.
 */
static __attribute__((unused)) StdPtrNode **
SearchNode_IterConstructor(StdPtrNode **self)
{
	StdPtrIter_BaseConstructor(self);
	return self;
}

/*
 * 0x0049E5B0 - PathNode::InitFromLoc
 *
 * Copies x/y/z from loc and stores dir.
 */
static PathNode *
PathNode_InitFromLoc(PathNode *this, CLocation *loc, int16_t dir)
{
	this->x = loc->x;
	this->y = loc->y;
	this->z = loc->z;
	this->dir = dir;
	return this;
}

/*
 * 0x0049E600 - PathNode::Copy
 *
 * Copies the x/y/z/dir fields from src into this.
 */
static PathNode *
PathNode_Copy(PathNode *this, PathNode *src)
{
	this->x = src->x;
	this->y = src->y;
	this->z = src->z;
	this->dir = src->dir;
	return this;
}

/*
 * 0x0049E650 - PathNode::SetFromLoc
 *
 * Seeds the path node from loc's coordinates and the given direction.
 */
static void
PathNode_SetFromLoc(PathNode *this, CLocation *loc, int16_t dir)
{
	this->x = loc->x;
	this->y = loc->y;
	this->z = loc->z;
	this->dir = dir;
}

/*
 * 0x0049E690 - PathNode_ComputeInterp
 *
 * pow(2.0, log(x) / log(0.5)).
 */
static double
PathNode_ComputeInterp(double x)
{
	double temp1 = log(x);
	double temp2 = log(0.5);
	double quotient = temp1 / temp2;
	return pow(2.0, quotient);
}

/*
 * 0x0049E6E2 - PathNode_Interpolate
 *
 * Normalized PathNode::ComputeInterp-space interpolation between x1
 * and x2 by parameter t.
 */
double
PathNode_Interpolate(double x1, double x2, double t)
{
	double interpVal = (x2 - x1) * t + x1;
	double ci1 = PathNode_ComputeInterp(interpVal);
	double ci2 = PathNode_ComputeInterp(x2);
	double ci3 = PathNode_ComputeInterp(x1);
	double ci4 = PathNode_ComputeInterp(x2);
	return (ci1 - ci2) / (ci3 - ci4);
}

/*
 * 0x0049E814 - PathNodeList::PathNodeList
 *
 * Zero-initializes an empty path node list.
 */
PathNodeList *
PathNodeList_Constructor(PathNodeList *this)
{
	this->count = 0;
	this->head = NULL;
	this->closed = 0;
	return this;
}

/*
 * 0x004A81B0 - CNPC::IsTargetNotHidden
 *
 * Returns 1 when target is not currently hidden.
 */
static int
CNPC_IsTargetNotHidden(CNPC *npc, CItem *target)
{
	USED(npc);
	if (VT_IsHidden(target))
		return 0;
	return 1;
}

/*
 * 0x004A81D8 - CNPC::CanSeeTarget
 *
 * True when the target is not hidden, not on the combat ignore list,
 * and LOS (CTerrainManager_CheckMoveBlocked) is clear. The target is
 * briefly detached from the spatial grid for the check and restored.
 */
static int
CNPC_CanSeeTarget(CNPC *npc, CItem *target)
{
	CLocation targetLoc;
	int height;
	int moveResult;

	if (!CNPC_IsTargetNotHidden(npc, target))
		return 0;

	if (CSerialList_Contains(&npc->mobile.combatTargetList, CMobile_GetSerial((CMobile *)target)))
		return 0;

	CLocation_SetLoc(&targetLoc, CItem_GetLocationVT(target));

	((void (*)(void *))VT_FN(target, VT_DETACH_SPATIAL))(target);

	height = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_HEIGHT))(npc);
	moveResult = CTerrainManager_CheckMoveBlocked(targetLoc, height, ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_MOVEMENT_TYPE))(npc) & 0xFF, (CItem *)npc, 0);

	((void (*)(void *, CLocation *))VT_FN(target, VT_SET_LOCATION))(target, &targetLoc);

	if (moveResult & 2)
		return 1;
	return 0;
}

/*
 * 0x004A829F - CNPC::StartCombatAI
 *
 * Switches the NPC to combat state and runs CombatInitiate. Uses a
 * reentrancy stack so the same (npc, target) pair does not recurse.
 */
void
CNPC_StartCombatAI(CMobile *npc, CMobile *target)
{
	int i;
	uint32_t npcSerial, targetSerial;

	if (g_CombatAIDepth >= NPC_COMBAT_AI_MAX_DEPTH)
		return;

	npcSerial = npc->container.item.serial;
	targetSerial = target->container.item.serial;

	for (i = 0; i < g_CombatAIDepth; i++) {
		if (g_CombatAINPCStack[i] == npcSerial && g_CombatAITargetStack[i] == targetSerial)
			return;
	}

	CMobile_NPC_SetAIState(npc, 5);

	g_CombatAINPCStack[g_CombatAIDepth] = npcSerial;
	g_CombatAITargetStack[g_CombatAIDepth] = targetSerial;
	g_CombatAIDepth++;

	CombatInitiate(npc, target, 1);

	g_CombatAIDepth--;
}

/*
 * 0x004A8361 - CNPC::ShouldProcess
 *
 * True when g_npcProcessFilter is 1 (all) or matches this NPC's serial
 * low word.
 */
int
CNPC_ShouldProcess(CNPC *npc)
{
	uint16_t serial;

	serial = (uint16_t)(npc->mobile.container.item.serial & 0xFFFF);
	if (g_npcProcessFilter == 1)
		return 1;
	if (g_npcProcessFilter == (uint32_t)(serial & 0xFFFF))
		return 1;
	return 0;
}

/*
 * 0x004A8C73 - CNPC::PackBehaviorScan
 *
 * 1/1000 chance per idle tick for a humanoid NPC with pack credits to
 * spawn up to (100 - baseInt)/5 companions around itself and merge
 * their packs.
 */
static void
CNPC_PackBehaviorScan(CNPC *npc)
{
	int numSpawns;
	int credits;
	CLocation loc;
	CResBankRegion *region;
	int counter;
	int i;

	if (CDefcon_IsFull())
		return;

	if (GetRandomRange(1, 1000) != 1)
		return;

	if (CMobile_IsCreatureBody(&npc->mobile)) {
		((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 4);
		return;
	}

	if ((int)npc->npcInfo1_3 < 1) {
		((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 4);
		return;
	}

	numSpawns = (100 - (int)npc->mobile.baseInt) / 5;
	credits = (int)npc->npcInfo1_3;
	if (numSpawns > credits)
		numSpawns = credits;
	npc->npcInfo1_3 -= numSpawns;
	npc->npcInfo1_3 = 0;

	CLocation_SetLoc(&loc, (CLocation *)&npc->mobile.container.item.resourceEntity.nextInContainer);
	region = CResBankManager_GetRegionByLocation(loc.x, loc.y);
	USED(region);

	counter = 0;
	for (i = 0; i < numSpawns; i++) {
		uint32_t serial;
		uint16_t tmpl;
		int16_t x, y;
		int8_t z;

		tmpl = CResourceEntity_GetTemplateIndex((CItem *)npc);
		x = npc->mobile.container.item.resourceEntity.entity.location.x;
		y = npc->mobile.container.item.resourceEntity.entity.location.y;
		z = npc->mobile.container.item.resourceEntity.entity.location.z;

		serial = SpawnAtPointForLocation(tmpl & 0xFFFF, x, y, z, 3);
		if (serial == 0)
			break;

		{
			CItem *ent = CWorld_FindBySerial(g_World, serial);
			if (ent != NULL) {
				CNPC *spawned = (CNPC *)ent;
				if (spawned->npcSearchRange == 0xFF)
					CNPC_InitFromResourceNodes(spawned);
				if (spawned->behaviorFlags & 0x80)
					NPC_PackMerge(&npc->mobile, &spawned->mobile);
			}
		}
		counter++;
	}
	USED(counter);
}

/*
 * 0x004A8E15 - CNPC::InitFromResourceNodes
 *
 * Rebuilds the NPC's template-derived state (hunger capacity, diet,
 * search range, behavior flags, level list membership, wander region
 * flags) by walking the CResourceNode chain at entity.firstChild.
 */
static void
CNPC_InitFromResourceNodes(CNPC *npc)
{
	CResourceNode *node;

	// Local arrays for resource node data (binary stores into stack
	// arrays that are effectively unused after the function returns -
	// the data was consumed by the old template system)
	uint8_t type1_id[4], type1_val1[4], type1_val3[4];
	uint8_t type2_id[4], type2_switch[4];
	uint8_t type0_id[16], type0_val1[16], type0_val3[16];

	int nodeId;
	int nodeType;
	int switchVar;
	int val3;
	CResBankRegion *region;

	npc->resTplCount1 = 0;
	npc->resTplCount0 = 0;
	npc->npcSearchRange = 0;

	node = npc->mobile.container.item.resourceEntity.firstChild;

	npc->hungerCapacity = 0;

	((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x80);
	((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x100);
	((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x4000);

	while (node != NULL) {
		nodeId = (uint16_t)node->id;
		if (nodeId == 0)
			goto next_node;

		nodeType = (int8_t)node->type;

		if (nodeType == 0) {
			if ((int)node->value1 > (int)npc->hungerCapacity)
				npc->hungerCapacity = node->value1;

			if (nodeId == g_ResTypeId_Meat) {
				val3 = (int)node->value3;
				if (val3 > 3)
					val3 = 3;
				if (val3 > 0)
					npc->npcLevel = (uint8_t)val3;
			}
		} else if (nodeType == 1) {
			if ((int)npc->npcSearchRange >= 4)
				goto next_node;

			type1_id[npc->npcSearchRange] = (uint8_t)nodeId;
			type1_val1[npc->npcSearchRange] = (uint8_t)node->value1;
			type1_val3[npc->npcSearchRange] = (uint8_t)node->value3;
			npc->npcSearchRange++;

			if (node->value3 != 0) {
				((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x4000);
			}
		} else if (nodeType == 2) {
			if ((int)node->value3 < 0) {
				if ((int)npc->resTplCount1 >= 4)
					goto next_node;

				type2_id[npc->resTplCount1] = (uint8_t)nodeId;
				switchVar = -(int)node->value3 - 1;

				if ((unsigned int)switchVar > 5)
					goto next_node;

				switch (switchVar) {
				case 0:
					type2_switch[npc->resTplCount1] = 0;
					npc->resTplCount1++;
					break;
				case 1:
					type2_switch[npc->resTplCount1] = 1;
					npc->resTplCount1++;
					break;
				case 2:
					type2_switch[npc->resTplCount1] = 2;
					npc->resTplCount1++;
					break;
				case 4:
					type2_switch[npc->resTplCount1] = 3;
					npc->resTplCount1++;
					break;
				case 3:
					if (nodeId == g_ResTypeId_Meat) {
						if ((int)node->value1 == 1) {
							type2_switch[npc->resTplCount1] = 4;
							npc->resTplCount1++;
						} else if ((int)node->value1 == 2) {
							type2_switch[npc->resTplCount1] = 5;
							npc->resTplCount1++;
						} else {
							type2_switch[npc->resTplCount1] = 6;
							npc->resTplCount1++;
						}
					} else {
						type2_switch[npc->resTplCount1] = 6;
						npc->resTplCount1++;
					}
					break;
				case 5:
					if (nodeId == g_ResTypeId_Meat) {
						if ((int)node->value1 == 1) {
							type2_switch[npc->resTplCount1] = 7;
							npc->resTplCount1++;
						} else if ((int)node->value1 == 2) {
							type2_switch[npc->resTplCount1] = 8;
							npc->resTplCount1++;
						} else {
							type2_switch[npc->resTplCount1] = 9;
							npc->resTplCount1++;
						}
					} else {
						type2_switch[npc->resTplCount1] = 9;
						npc->resTplCount1++;
					}
					break;
				}
			} else {
				if (nodeId == g_ResTypeId_Self) {
					((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x80);
					if ((int)node->value3 >= 1 && (int)node->value3 <= 3)
						npc->_npc_pad3F8 = (uint8_t)((int)node->value3 - 1);
				} else {
					if ((int)npc->resTplCount0 >= 16)
						goto next_node;

					if ((int)node->value3 < 1 || (int)node->value3 > 3)
						goto next_node;

					type0_id[npc->resTplCount0] = (uint8_t)nodeId;
					type0_val1[npc->resTplCount0] = (uint8_t)node->value1;
					type0_val3[npc->resTplCount0] = (uint8_t)((int)node->value3 - 1);
					npc->resTplCount0++;

					if ((int)node->value3 - 1 == 1) {
						if (nodeId != g_ResTypeId_Self && nodeId != g_ResTypeId_Humans) {
							((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x100);
						}
					}
				}
			}
		}

next_node:
		node = node->next;
	}

	if (CNPC_IsInLevelList(npc))
		CNPC_RemoveFromLevelList(npc);
	CNPC_AddToLevelList(npc);

	if (npc->hungerCapacity == 0)
		npc->hungerCapacity = 99;

	if ((uint8_t)npc->mobile.stomach > 250)
		npc->mobile.stomach = (uint8_t)npc->hungerCapacity;

	if (npc->resTplPtr0 != NULL) {
		OperatorDelete(npc->resTplPtr0);
		npc->resTplPtr0 = NULL;
	}
	if (npc->resTplPtr1 != NULL) {
		OperatorDelete(npc->resTplPtr1);
		npc->resTplPtr1 = NULL;
	}
	if (npc->resTplPtr2 != NULL) {
		OperatorDelete(npc->resTplPtr2);
		npc->resTplPtr2 = NULL;
	}
	if (npc->resTplPtr3 != NULL) {
		OperatorDelete(npc->resTplPtr3);
		npc->resTplPtr3 = NULL;
	}
	if (npc->resTplPtr4 != NULL) {
		OperatorDelete(npc->resTplPtr4);
		npc->resTplPtr4 = NULL;
	}
	if (npc->resTplPtr5 != NULL) {
		OperatorDelete(npc->resTplPtr5);
		npc->resTplPtr5 = NULL;
	}
	if (npc->resTplPtr6 != NULL) {
		OperatorDelete(npc->resTplPtr6);
		npc->resTplPtr6 = NULL;
	}
	if (npc->resTplPtr7 != NULL) {
		OperatorDelete(npc->resTplPtr7);
		npc->resTplPtr7 = NULL;
	}

	((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x380000);

	{
		CLocation loc;
		loc = npc->mobile.container.item.resourceEntity.entity.location;
		region = CResBankManager_GetRegionByLocation(loc.x, loc.y);
	}

	if (region != NULL) {
		if (region->noWander == 1) {
			((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x100000);
		} else if (region->noWander == 2) {
			((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x80000);
		} else {
			((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x200000);
		}
	}

	// Dead template-processing scratch arrays (binary writes without
	// ever reading them back).
	USED(type1_id);
	USED(type1_val1);
	USED(type1_val3);
	USED(type2_id);
	USED(type2_switch);
	USED(type0_id);
	USED(type0_val1);
	USED(type0_val3);

	CNPC_PostInitBehaviorCheck(npc);
}

/*
 * 0x004A956E - CMobile::NPC_AI_CombatStart
 *
 * Picks the nearest attacker from attackerList and either delegates to
 * the follower handler or starts combat AI against that attacker.
 * Returns 0 only when the attacker list had no valid target.
 */
int
CMobile_NPC_AI_CombatStart(CMobile *this)
{
	CNPC *npc;
	uint32_t nearestSerial;
	CItem *targetEnt;
	CMobile *targetMob;
	StdPtrNode *resultIter, *endTemp;

	CSerialList_FindNearestMobile((StdPtrList *)&this->attackerList, &resultIter, &this->container.item.resourceEntity.entity.location);
	if (!(StdPtrIter_Neq(&resultIter, StdPtrList_End((StdPtrList *)&this->attackerList, &endTemp)) & 0xFF))
		return 0;
	nearestSerial = (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&resultIter));

	npc = (CNPC *)this;

	if (this->hasFollowers != 0) {
		CNPC_FollowerCombatHandler(this);
		if (npc != g_currentNPC)
			return 1;
		return 1;
	}

	// No followers: resolve nearest attacker
	targetEnt = CWorld_FindBySerial(g_World, nearestSerial);
	if (targetEnt == NULL)
		return 1;

	targetMob = (CMobile *)targetEnt;

	if (npc->aiState == NPC_STATE_ATTACK_TARGET && CMobile_GetSerial(targetMob) == npc->actionTarget)
		return 1;

	if (CSerialList_Contains(&this->combatTargetList, CMobile_GetSerial(targetMob)))
		return 1;

	CNPC_StartCombatAI(this, targetMob);

	if (npc != g_currentNPC)
		return 1;

	CNPC_ShouldProcess(npc);

	return 1;
}

/*
 * 0x004A9676 - CNPC::CombatPatrol
 *
 * Non-follower NPCs scan their attacker/combat lists. When no combat
 * starts but ATTACK_TARGET still holds a stale actionTarget, the state
 * is reset or re-engaged against the refreshed target.
 */
static void
CNPC_CombatPatrol(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	CItem *target;
	int result;

	if (mob->isFollower != 0)
		return;

	result = CMobile_NPC_AI_CombatStart(mob);

	if (npc != g_currentNPC)
		return;

	if (result != 0)
		return;

	if (npc->aiState != NPC_STATE_ATTACK_TARGET)
		return;

	if (npc->actionTarget == 0)
		return;

	target = CWorld_FindBySerial(g_World, npc->actionTarget);
	if (target == NULL) {
		npc->aiState = NPC_STATE_ATTACK_TARGET;
		npc->actionTarget = 0;
		return;
	}

	if (npc->aiState == NPC_STATE_ATTACK_TARGET && CMobile_GetSerial((CMobile *)target) == npc->actionTarget)
		return;

	CNPC_StartCombatAI(mob, (CMobile *)target);

	if (npc != g_currentNPC)
		return;

	CNPC_ShouldProcess(npc);
}

/*
 * 0x004A974D - CNPC::PostInitBehaviorCheck
 *
 * Triggers a nearest-target scan when behavior bit 8 is clear and the
 * NPC is not already attacking a target.
 */
static void
CNPC_PostInitBehaviorCheck(CNPC *npc)
{
	int hasBit8;

	hasBit8 = ((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 8);
	if (!hasBit8 && npc->aiState != NPC_STATE_ATTACK_TARGET)
		CNPC_ScanAndEngageNearest(npc);
}

/*
 * 0x004A9780 - CNPC::HandleStates
 *
 * Main per-tick AI state machine: resolves follow/pet-distance
 * timeouts, runs the combat patrol, checks idle triggers (hunger,
 * shelter, pack, wander), then dispatches the 14-entry state switch.
 *
 * CUSTOM (FEAT_ECOLOGY): adds the idle predator/prey/pack/scavenger scan
 * and the graze, shelter and desire state handlers.
 */
void
CNPC_HandleStates(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	CLocation *myLoc = &mob->container.item.resourceEntity.entity.location;

	if (npc->npcSearchRange == 0xFF)
		CNPC_InitFromResourceNodes(npc);

	if (mob->container.item.resourceEntity.entity.removedFromWorld)
		return;
	if ((CResourceEntity_GetTemplateIndex((CItem *)npc) & 0xFFFF) == 0xFFFF)
		return;

	npc->tickCount++;

	if (npc->behaviorFlags & 0x2000) {
		if (npc->followObj3 > 0) {
			npc->followObj3--;
		} else {
			((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x3002);
			if (CNPC_ShouldProcess(npc)) {
				CWorld_FindBySerial(g_World, npc->followObj1);
			}
			npc->aiState = NPC_STATE_IDLE;
			npc->isWalking = 0;
		}
	}

	if (mob->isFollower) {
		CMobile *master = mob->owner;
		CLocation *masterLoc = &master->container.item.resourceEntity.entity.location;
		int sqDist = DistBetween(myLoc, masterLoc);
		if (sqDist > 0x400) {
			CMobile_RemoveFollower(master, mob);
			npc->aiState = NPC_STATE_IDLE;
			npc->isWalking = 0;
			if (master->firstFollower == NULL)
				master->hasFollowers = 0;
		}
	}

	if (npc->aiState == NPC_STATE_RUNAWAY) {
		StdPtrNode *runIter, *runEndTemp;
		CSerialList_FindNearestMobile((StdPtrList *)&mob->attackerList, &runIter, myLoc);
		if (StdPtrIter_Neq(&runIter, StdPtrList_End((StdPtrList *)&mob->attackerList, &runEndTemp)) & 0xFF) {
			uint32_t nearSerial = (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&runIter));
			CItem *nearEnt = CWorld_FindBySerial(g_World, nearSerial);
			if (nearEnt != NULL) {
				CLocation nearLoc;
				CLocation_SetLoc(&nearLoc, &nearEnt->resourceEntity.entity.location);
				int dist = DistBetween(&nearLoc, myLoc);
				if (dist < 4)
					npc->aiState = NPC_STATE_IDLE;
			}
		}
		if (npc->aiState == NPC_STATE_RUNAWAY) {
			if (npc->isWalking == 0)
				CNPC_RunawayTick(npc);
		}
		return;
	}

	CNPC_CombatPatrol(npc);
	if (npc != g_currentNPC)
		return;

	CNPC_PostInitBehaviorCheck(npc);

	if (npc->isWalking != 0)
		return;

	// Custom (FEAT_ECOLOGY): keep a .foragemode NPC inside the radius
	// where it still ticks. Checked before the IDLE-only preamble
	// below, because a strayed forager is usually mid-WANDER.
	if (feat(FEAT_ECOLOGY) && CNPC_ForageLeash(npc))
		return;

	if (feat(FEAT_ECOLOGY)) {
		// Ecology idle scanning (predator/prey/pack/scavenger AI).
		// IdleScan is wrapped in CNPC_EcologyTick for state translation -
		// the binary's IdleScan uses state numbering that conflicts with
		// HandleStates. Only runs for idle non-vendor creatures. Can
		// transition the NPC to ATTACK_TARGET (predator pursuit), RUNAWAY
		// (aversion flee), EAT_FOOD (eating countdown), or FOLLOWING (pack
		// merge). CombatInitiate inside IdleScan can destroy NPCs via
		// combat callbacks, so re-check g_currentNPC.
		if (npc->aiState == NPC_STATE_IDLE && !VT_IsVendor((CItem *)npc)) {
			CNPC_EcologyTick((CItem *)npc);
			if (npc != g_currentNPC)
				return;
			if (npc->aiState != NPC_STATE_IDLE)
				return;
		}
	}

	if (npc->aiState == NPC_STATE_IDLE) {
		CLocation tmpLoc;

		CNPC_SetRunState(mob, 0);

		if (mob->isFollower) {
			CMobile *owner = mob->owner;
			CLocation *ownerLoc = &owner->container.item.resourceEntity.entity.location;
			npc->aiState = NPC_STATE_FOLLOWING;
			CLocation_Init(&tmpLoc);
			CLocation_Set(&tmpLoc, ownerLoc->x, ownerLoc->y, 0);
			npc->patrolTarget = tmpLoc;
			npc->ltype = 0xA;
			npc->stateInfo2 = 0xA;
			npc->isWalking = 1;
			CNPC_WalkAnimDispatch(mob);
			return;
		}

		if (npc->behaviorFlags & 0x1000) {
			CItem *leader = CWorld_FindBySerial(g_World, npc->followObj1);
			if (leader == NULL) {
				((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x3002);
				return;
			} else {
				CLocation *leaderLoc = &leader->resourceEntity.entity.location;
				npc->aiState = NPC_STATE_FOLLOWING;
				CLocation_Init(&tmpLoc);
				CLocation_Set(&tmpLoc, leaderLoc->x, leaderLoc->y, 0);
				npc->patrolTarget = tmpLoc;
				npc->ltype = 0xA;
				npc->stateInfo2 = 0xA;
				CNPC_WalkAnimDispatch(mob);
				npc->isWalking = 1;
				return;
			}
		}

		if (((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 0x20000)) {
			CNPC_Loiter(npc, 0x3E8, npc->loiterLoc);
			goto state_switch;
		}

		if (CNPC_CheckPetHunger(npc)) {
			if (!((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 0x10000)) {
				CNPC_ShouldProcess(npc);
				CNPC_SetState(npc, NPC_STATE_SEEK_FOOD);
				goto state_switch;
			}
		}

		if (npc->speechCounter > 0) {
			CNPC_ShouldProcess(npc);
			CNPC_SetState(npc, NPC_STATE_SEEK_SHELTER);
			npc->speechCounter--;
			if (GetRandomRange(1, 2) == 1)
				npc->speechCounter = 0;
			goto state_switch;
		}

		if (npc->tickCount > 0xF0) {
			if (!((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 4)) {
				CNPC_PackBehaviorScan(npc);
			}
		}

		// Custom test aid (FEAT_ECOLOGY): an NPC tagged via the GM
		// .foragemode command always rolls SEEK_DESIRES from IDLE, so
		// the natural forage -> carry -> deposit loop can be exercised
		// without the binary's 50% gate. The scan, pursuit, pickup,
		// carry-home and deposit all still run unforced - only the
		// random roll is replaced.
		if (feat(FEAT_ECOLOGY)) {
			int forageMode = 0;
			CResourceEntity_GetTagInt((CItem *)npc, "foragemode", &forageMode);
			// Mode 1 replaces the roll; mode 2 is leash-only, for a test
			// whose whole point is that this roll is not replaced.
			if (forageMode == 1) {
				CNPC_ShouldProcess(npc);
				CNPC_SetState(npc, NPC_STATE_SEEK_DESIRES);
				goto state_switch;
			}
		}

		// All paths fall through to state switch via jmp 0x004a9ce0.
		{
			int roll = GetRandomRange(1, 10);
			if (roll <= 5) {
				CNPC_ShouldProcess(npc);
				CNPC_SetState(npc, NPC_STATE_SEEK_DESIRES);
				goto state_switch;
			} else if (roll <= 8) {
				if (!((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 0x10000)) {
					CNPC_ShouldProcess(npc);
					CNPC_SetState(npc, NPC_STATE_SEEK_FOOD);
					goto state_switch;
				}
				// Fall through to WANDER when food disabled
			}
			CNPC_SetState(npc, NPC_STATE_WANDER);
			npc->wanderSteps = (uint32_t)GetRandomRange(4, 25);
		}
	}

	// Binary checks hasFollowers, but both branches call ShouldProcess
	// identically (dead conditional).
state_switch:
	CNPC_ShouldProcess(npc);
	switch (npc->aiState) {
	case NPC_STATE_SEEK_FOOD:
		if (((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 0x10002))
			CNPC_SetState(npc, NPC_STATE_IDLE);
		else if (feat(FEAT_ECOLOGY) && (CNPC_GrazeHandler(npc) || CNPC_CarnivoreFeed(npc)))
			CNPC_SetState(npc, NPC_STATE_IDLE);
		else
			CNPC_FoodSeek(npc);
		break;
	case NPC_STATE_SEEK_SHELTER:
		if (feat(FEAT_ECOLOGY))
			CNPC_SeekShelterHandler(npc);
		else
			CNPC_SetState(npc, NPC_STATE_IDLE);
		break;
	case NPC_STATE_PURSE_SHELTER:
		if (((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 0x10))
			CNPC_SetState(npc, NPC_STATE_IDLE);
		else
			CNPC_PurseShelterHandler(npc);
		break;
	case NPC_STATE_SEEK_DESIRES:
		if (feat(FEAT_ECOLOGY))
			CNPC_SeekDesiresHandler(npc);
		else
			CNPC_SetState(npc, NPC_STATE_IDLE);
		break;
	case NPC_STATE_PURSE_DESIRES:
		if (feat(FEAT_ECOLOGY))
			CNPC_PurseDesiresHandler(npc);
		else
			CNPC_SetState(npc, NPC_STATE_IDLE);
		break;
	case NPC_STATE_EAT_FOOD:
		break;
	case NPC_STATE_LOITER:
		CNPC_ResourceWander(npc);
		break;
	case NPC_STATE_RUNAWAY:
		// Note: in the binary, state 7 is primarily handled in the preamble
		// (waypoint processing + isWalking check), so this switch entry is
		// effectively dead code. We call it here for compatibility.
		CNPC_RunawayTick(npc);
		break;
	case NPC_STATE_TALKING:
		CNPC_SetState(npc, NPC_STATE_IDLE);
		break;
	case NPC_STATE_ATTACK_TARGET:
		break;
	case NPC_STATE_WANDER:
		CNPC_WanderTick(npc);
		break;
	case NPC_STATE_SLEEP:
		break; // nop
	case NPC_STATE_IDLE:
		break;
	case NPC_STATE_FOLLOWING:
		break;
	default:
		CNPC_SetState(npc, NPC_STATE_IDLE);
		break;
	}
}

/*
 * 0x004A9E0C - CNPC::ClearCombatTarget
 *
 * Clears this NPC's combat target. If notifyOldTarget is non-zero,
 * also tells the old target to stop fighting with this NPC.
 * Always calls StopFightWith on self, zeros actionTarget, and
 * sets AI state to IDLE.
 */
void
CNPC_ClearCombatTarget(CMobile *this, int notifyOldTarget)
{
	CNPC *npc = (CNPC *)this;
	CItem *entity;

	if (notifyOldTarget) {
		entity = CWorld_FindBySerial(g_World, npc->actionTarget);
		if (entity != NULL) {
			if (VT_IsMobile(entity)) {
				CMobile_StopFightWith((CMobile *)entity, this->container.item.serial, 1);
			}
		}
	}

	CMobile_StopFightWith(this, npc->actionTarget, notifyOldTarget);
	npc->actionTarget = 0;
	CNPC_SetState(npc, NPC_STATE_IDLE);
}

/*
 * 0x004A9E8E - CNPC::CombatChase
 *
 * Handles combat target chasing during ATTACK_TARGET state movement.
 * Looks up actionTarget entity, validates it's alive/visible/in combat
 * list, checks distance. If target is gone/dead/too far, clears combat.
 * If NPC should flee and random check passes, transitions to RUNAWAY.
 * Otherwise sets patrolTarget to target's location and walks toward it.
 */
static void
CNPC_CombatChase(CNPC *npc)
{
	CItem *target;
	CLocation *loc;
	int dist;

	if (npc->actionTarget == 0) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	target = CWorld_FindBySerial(g_World, npc->actionTarget);
	if (target == NULL) {
		CNPC_ClearCombatTarget(&npc->mobile, 0);
		return;
	}

	// VT_CHECK_EC: skip hidden check for CNPC, keep attacking hidden targets
	if (!((int (*)(void *))VT_FN((CItem *)npc, VT_CHECK_EC))(npc)) {
		if (VT_IsHidden(target)) {
			CNPC_ClearCombatTarget(&npc->mobile, 0);
			loc = ((CLocation * (*)(void *)) VT_FN((CItem *)npc, VT_GET_LOCATION))(npc);
			CLocation_CopyFrom(&npc->patrolTarget, loc);
			return;
		}
	}

	if (VT_IsDead(target)) {
		CNPC_ClearCombatTarget(&npc->mobile, 1);
		return;
	}

	if (!CSerialList_Contains(&npc->mobile.combatTargetList, npc->actionTarget)) {
		CNPC_ClearCombatTarget(&npc->mobile, 0);
		return;
	}

	loc = ((CLocation * (*)(void *)) VT_FN((CItem *)npc, VT_GET_LOCATION))(npc);
	dist = CLocation_ChebyshevDistance(loc, ((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target));

	if (dist >= 0x24) {
		CNPC_ClearCombatTarget(&npc->mobile, 1);
		return;
	}

	if (CNPC_ShouldFlee(npc) == 1) {
		if (GetRandomRange(1, 100) < (int)npc->mobile.baseInt) {
			CNPC_ShouldProcess(npc);
			npc->aiState = NPC_STATE_RUNAWAY;
			npc->isWalking = 0;
			CNPC_RunawayTick(npc);
			return;
		}
	}

	loc = ((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target);
	CLocation_CopyFrom(&npc->patrolTarget, loc);
	npc->ltype = NPC_STATE_ATTACK_TARGET;
	npc->stateInfo2 = NPC_STATE_ATTACK_TARGET;
	npc->isWalking = 1;
	CNPC_SetRunState(&npc->mobile, 0);
}

/*
 * 0x004AA062 - CNPC::IsPackCompatible
 *
 * Returns 1 when self and target share a pack-compatible body:
 * same template, both creature bodies, or a match in the template's
 * altBodyType list.
 */
static int
CNPC_IsPackCompatible(CItem *self, CItem *target)
{
	uint16_t selfTemplate;
	uint16_t targetTemplate;
	uint8_t *bodyList;
	int count;
	uint16_t *entries;
	int i;

	selfTemplate = CResourceEntity_GetTemplateIndex(self) & 0xFFFF;
	targetTemplate = CResourceEntity_GetTemplateIndex(target) & 0xFFFF;

	if (targetTemplate == selfTemplate)
		return 1;

	if (CMobile_IsCreatureBody((CMobile *)self) && CMobile_IsCreatureBody((CMobile *)target))
		return 1;

	bodyList = g_TemplateBodyTypes[selfTemplate];
	if (bodyList == NULL)
		goto final_check;

	count = bodyList[0];
	if (count == 0)
		goto final_check;

	entries = (uint16_t *)(bodyList + 2);

	for (i = 0; i < count; i++) {
		if (targetTemplate == entries[i])
			return 1;
	}

	if (((CMobile *)target)->altBodyType == 0)
		goto final_check;

	for (i = 0; i < count; i++) {
		if (((CMobile *)target)->altBodyType == entries[i])
			return 1;
	}

final_check:
	if (((CMobile *)target)->altBodyType == selfTemplate)
		return 1;

	return 0;
}

/*
 * 0x004AA1B2 - CNPC::FindFoodInPack
 *
 * MODIFIED: Binary is a stub that iterates the container and returns 0;
 * the food identification logic was never implemented. With
 * FEAT_ECOLOGY enabled we scan mob's container for items producing
 * MEAT or CARNIVOREMEAT and feed the first match to feeder.
 */
static int
CNPC_FindFoodInPack(CMobile *mob, CMobile *feeder)
{
	CItem *item;
	CItem *next;
	CResourceNode *node;

	item = mob->container.contents;
	while (item != NULL) {
		next = item->spatialNext;

		if (feat(FEAT_ECOLOGY)) {
			node = NULL;
			if (g_ResTypeId_Meat != 0)
				node = CResourceEntity_FindNode(item, (uint16_t)g_ResTypeId_Meat, 3);
			if (node == NULL && g_ResTypeId_CarnivoreMeat != 0)
				node = CResourceEntity_FindNode(item, (uint16_t)g_ResTypeId_CarnivoreMeat, 3);

			if (node != NULL) {
				if (CNPC_ConsumeFood((CNPC *)feeder, item)) {
					// CUSTOM (FEAT_ECOLOGY): ConsumeFood
					// (binary-faithful) only stows the meat item;
					// the documented stomach refill lives here so
					// ConsumeFood stays exact. This is what makes
					// CNPC_TryEatFood's while (stomach <
					// hungerCapacity) top-off loop terminate.
					// stomach is uint8 (0x31C): compute in int and
					// clamp to capacity and the byte range.
					int cap = (int)((CNPC *)feeder)->hungerCapacity;
					int gain = (int)node->value1;
					int s = (int)(uint8_t)feeder->stomach + (gain > 0 ? gain : 0);
					if (s > cap)
						s = cap;
					if (s > 255)
						s = 255;
					feeder->stomach = (uint8_t)s;
					return 1;
				}
			}
		}

		item = next;
	}

	return 0;
}

/*
 * 0x004AA1E3 - CNPC::CheckFoodNearby
 *
 * Returns 1 if food is reachable in this mob's pack, its owner's pack,
 * or any follower's pack. Always 0 in the unmodified binary because
 * FindFoodInPack is a stub there.
 */
static int
CNPC_CheckFoodNearby(CMobile *mob)
{
	CMobile *cur;
	CMobile *follower;

	if (CNPC_FindFoodInPack(mob, mob) != 0)
		return 1;

	if (mob->isFollower == 0 && mob->hasFollowers == 0)
		return 0;

	cur = mob;
	if (mob->isFollower != 0) {
		if (CNPC_FindFoodInPack(mob->owner, mob) != 0)
			return 1;
		cur = mob->owner;
	}

	follower = cur->firstFollower;
	while (follower != NULL) {
		if (CNPC_FindFoodInPack(follower, mob) != 0)
			return 1;
		follower = follower->nextFollower;
	}
	return 0;
}

/*
 * 0x004AA292 - CNPC::FindHungriestFollower
 *
 * Returns the mob (self or follower) with the lowest stomach value
 * still below its hungerCapacity, or NULL when every mob is full.
 */
static CMobile *
CNPC_FindHungriestFollower(CNPC *npc)
{
	int bestStomach;
	CMobile *bestMob;
	CMobile *follower;
	int stomach;

	bestStomach = 0x7FFFFFFF;
	bestMob = NULL;

	stomach = (uint8_t)npc->mobile.stomach;
	if (stomach < (int)npc->hungerCapacity) {
		bestStomach = stomach;
		bestMob = &npc->mobile;
	}

	follower = npc->mobile.firstFollower;
	while (follower != NULL) {
		stomach = (uint8_t)follower->stomach;
		if (stomach < (int)((CNPC *)follower)->hungerCapacity) {
			if (stomach < bestStomach) {
				bestStomach = (uint8_t)follower->stomach;
				bestMob = follower;
			}
		}
		follower = follower->nextFollower;
	}
	return bestMob;
}

/*
 * 0x004AA330 - CNPC::TryEatFood
 *
 * Feeds the NPC (and its followers, if any) until nobody hungrier can
 * be fed, then drops the NPC into IDLE. Returns 0 when no food is
 * reachable; in the unmodified binary it always does, because
 * FindFoodInPack is a stub.
 */
static int
CNPC_TryEatFood(CNPC *npc)
{
	int flag;
	CMobile *mob;

	if (npc->mobile.hasFollowers != 0) {
		flag = 1;
		for (;;) {
			mob = CNPC_FindHungriestFollower(npc);
			if (mob == NULL)
				break;
			if (CNPC_CheckFoodNearby(mob) == 0)
				break;
			flag = 0;
		}
		if (flag == 0) {
			CNPC_SetState(npc, NPC_STATE_IDLE);
			return 1;
		}
		return 0;
	}

	if (CNPC_FindFoodInPack(&npc->mobile, &npc->mobile) == 0)
		return 0;
	for (;;) {
		if ((uint8_t)npc->mobile.stomach >= (int)npc->hungerCapacity)
			break;
		if (CNPC_FindFoodInPack(&npc->mobile, &npc->mobile) == 0)
			break;
	}
	CNPC_SetState(npc, NPC_STATE_IDLE);
	return 1;
}

/*
 * 0x004AA3EB - CNPC::FoodSeek
 *
 * Fires @seekfood; if no script handled it (or eating failed) the NPC
 * wanders 5..25 steps. A failed eat also runs @failfood first.
 */
static void
CNPC_FoodSeek(CNPC *npc)
{
	char *result;

	result = Entity_ExecuteEvent(&npc->mobile.container.item.resourceEntity.entity, 0x13);
	if (result == NULL) {
		if (npc == g_currentNPC)
			CNPC_EnterWanderState(npc, 5, 25);
		return;
	}
	if (npc != g_currentNPC)
		return;
	if (CNPC_TryEatFood(npc) != 0)
		return;
	CNPC_ShouldProcess(npc);
	Entity_ExecuteEvent(&npc->mobile.container.item.resourceEntity.entity, 0x08);
	if (npc == g_currentNPC)
		CNPC_EnterWanderState(npc, 5, 25);
}

/*
 * 0x004AA46D - CNPC::WanderStep
 *
 * Picks a random patrolTarget within +/-(range/2) of the NPC's current
 * position, re-rolling while the chosen quadrant equals the opposite
 * of the previous wander so the NPC doesn't oscillate.
 */
void
CNPC_WanderStep(CMobile *mob, int range)
{
	CNPC *npc = (CNPC *)mob;
	CLocation *mobLoc;
	int curX, curY;
	int half = range / 2;
	int dx, dy;
	uint8_t quadrant;

	mobLoc = &mob->container.item.resourceEntity.entity.location;
	curX = (int)(int16_t)mobLoc->x;
	curY = (int)(int16_t)mobLoc->y;

	do {
		dx = GetRandomRange(0, range) - half;
		dy = GetRandomRange(0, range) - half;

		if (dx > 0 && dy > 0)
			quadrant = 0;
		else if (dx > 0 && dy < 0)
			quadrant = 1;
		else if (dx < 0 && dy < 0)
			quadrant = 2;
		else
			quadrant = 3;
	} while ((quadrant ^ 2) == npc->lastWanderQuadrant);

	npc->lastWanderQuadrant = quadrant;

	npc->patrolTarget.x = (uint16_t)(curX + dx);
	npc->patrolTarget.y = (uint16_t)(curY + dy);
	npc->patrolTarget.z = 0;

	npc->isWalking = 1;
}

/*
 * 0x004AA572 - CNPC::WanderTick
 *
 * One WANDER tick: caps wanderSteps for NPCs, decrements it, and
 * either drops into IDLE (steps exhausted), switches to an idle-pose
 * AI state on a random roll, or takes a WanderStep and dispatches the
 * walk animation.
 */
static void
CNPC_WanderTick(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	int roll;

	if (((int (*)(void *))VT_FN((CItem *)mob, VT_CHECK_EC))(mob)) {
		if (npc->wanderSteps > 7)
			npc->wanderSteps = 7;
	}

	npc->wanderSteps--;
	if ((int32_t)npc->wanderSteps < 1) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	roll = GetRandomRange(1, 10);
	if (roll == 3) {
		if (CMobile_IsVendor(mob)) {
			CMobile_NPC_SetAIState(mob, 2);
			return;
		}
		if (GetRandomRange(1, 4) == 3) {
			CMobile_NPC_SetAIState(mob, 2);
			return;
		}
	} else if (roll >= 4 && roll <= 6) {
		CMobile_NPC_SetAIState(mob, 3);
		return;
	}

	CNPC_WanderStep(mob, 0x14);
	npc->ltype = NPC_STATE_WANDER;
	npc->stateInfo2 = NPC_STATE_WANDER;
	CNPC_WalkAnimDispatch(mob);
}

/*
 * 0x004AA678 - CNPC::FoodTransition
 *
 * Resolves the EAT_FOOD target: drops to IDLE when gone; compares
 * fight strength against mobile targets to pick IDLE / RUNAWAY /
 * combat; otherwise tries to eat the item and returns to IDLE.
 */
static void
CNPC_FoodTransition(CNPC *npc)
{
	CItem *target;
	int result;

	target = CWorld_FindBySerial(g_World, npc->resourceTargetSerial);

	if (target == NULL || target->resourceEntity.entity.removedFromWorld != 0) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	if (VT_IsMobile(target)) {
		result = CNPC_CompareFightStrength(npc, (CMobile *)target);

		if (result == 1) {
			CNPC_SetState(npc, NPC_STATE_IDLE);
			return;
		}

		if (result == 2) {
			CNPC_SetState(npc, NPC_STATE_RUNAWAY);
			npc->actionTarget = target->serial;
			npc->isWalking = 0;
			CNPC_ShouldProcess(npc);
			return;
		}

		CMobile_NPC_SetAIState(&npc->mobile, 5);
		CNPC_StartCombatAI(&npc->mobile, (CMobile *)target);

		if (npc == g_currentNPC)
			CNPC_ShouldProcess(npc);
		return;
	}

	if (CNPC_EatFood(npc, target)) {
		target = CWorld_FindBySerial(g_World, npc->resourceTargetSerial);
		CNPC_ShouldProcess(npc);
		if (target == NULL)
			return;
	}

	CNPC_ShouldProcess(npc);
	CNPC_SetState(npc, NPC_STATE_IDLE);
}

/*
 * 0x004AA7A3 - CNPC::FindNearestPrey
 *
 * Walks g_NPCLevelList from startLevel to 3 and records the nearest
 * non-self, non-pack-compatible NPC into the g_EcoPrey* parallel
 * arrays (capped at 32 entries).
 */
static void __attribute__((unused))
CNPC_FindNearestPrey(CNPC *self, int startLevel, int actionType)
{
	CNPC *bestNPC;
	int bestDist;
	int bestLevel;
	CNPC *cur;
	int dist;
	int level;
	int idx;

	bestNPC = NULL;
	bestDist = 0x7FFFFFFF;
	bestLevel = 0;

	for (level = startLevel; level < 4; level++) {
		cur = g_NPCLevelList[level];
		while (cur != NULL) {
			dist = DistBetween(&cur->mobile.container.item.resourceEntity.entity.location, &self->mobile.container.item.resourceEntity.entity.location);
			if (dist < bestDist && cur != self && !CNPC_IsPackCompatible((CItem *)self, (CItem *)cur)) {
				bestDist = dist;
				bestNPC = cur;
				bestLevel = level;
			}
			cur = cur->levelListPrev;
		}
	}

	if (bestNPC != NULL) {
		if (g_EcoPreyCount < 0x20) {
			idx = g_EcoPreyCount;
			g_EcoPreyAction[idx] = actionType;
			g_EcoPreyResType[idx] = g_ResTypeId_Meat;
			g_EcoPreyTarget[idx] = bestNPC;
			g_EcoPreyLevel[idx] = bestLevel;
			g_EcoPreyCount++;
		}
	}
}

/*
 * 0x004AA8C9 - CNPC::CollectResourceDesires
 *
 * Walks the NPC's resource-template arrays and copies each entry whose
 * kind byte is 0 or 2 into outIds, with the matching outKinds byte set
 * to 0 or 1. Kinds 4 to 9 instead run a prey scan at level 1, 2 or 3
 * with action type 0 or 1; kinds 1 and 3 do nothing. Stops once max
 * entries have been written and returns how many were.
 */
static __attribute__((unused)) int
CNPC_CollectResourceDesires(CNPC *this, uint8_t *outIds, uint8_t *outKinds, int max)
{
	const uint8_t *ids = (const uint8_t *)this->resTplPtr6;
	const uint8_t *kinds = (const uint8_t *)this->resTplPtr7;
	int count, i;

	count = 0;
	for (i = 0; i < (int)this->resTplCount1; i++) {
		switch (kinds[i]) {
		case 0:
			outIds[count] = ids[i];
			outKinds[count] = 0;
			count++;
			break;
		case 2:
			outIds[count] = ids[i];
			outKinds[count] = 1;
			count++;
			break;
		case 4:
			CNPC_FindNearestPrey(this, 1, 0);
			break;
		case 5:
			CNPC_FindNearestPrey(this, 2, 0);
			break;
		case 6:
			CNPC_FindNearestPrey(this, 3, 0);
			break;
		case 7:
			CNPC_FindNearestPrey(this, 1, 1);
			break;
		case 8:
			CNPC_FindNearestPrey(this, 2, 1);
			break;
		case 9:
			CNPC_FindNearestPrey(this, 3, 1);
			break;
		}

		if (count >= max)
			break;
	}
	return count;
}

/*
 * 0x004AAA0D - CNPC::IsHostileTo
 *
 * Hostile when the NPC's vulnFlags intersect the target's resistFlags
 * (bits: 1=meat/carnivore, 2=human, 4=good, 8=evil).
 */
int
CNPC_IsHostileTo(CMobile *npc, CMobile *target)
{
	return CMobile_GetVulnFlags(npc) & CMobile_GetResistFlags(target);
}

/*
 * 0x004AAA30 - CNPC::CanEngageTarget
 *
 * True when the target is hostile and currently visible.
 */
int
CNPC_CanEngageTarget(CNPC *npc, CMobile *target)
{
	if (!CNPC_IsHostileTo(&npc->mobile, target))
		return 0;
	return CNPC_CanSeeTarget(npc, (CItem *)target);
}

/*
 * 0x004AAA72 - CNPC::EngageTarget
 *
 * Stores the target's serial as the combat target and starts combat AI.
 */
void
CNPC_EngageTarget(CNPC *npc, CMobile *target)
{
	CNPC_SetCombatTarget(npc, target->container.item.serial);
	CNPC_StartCombatAI(&npc->mobile, target);
}

/*
 * 0x004AAAC5 - CNPC::CheckEngageCombat
 *
 * Engages target when behaviors are enabled, the NPC isn't already
 * attacking, and CanEngageTarget succeeds.
 */
void
CNPC_CheckEngageCombat(CNPC *npc, CMobile *target)
{
	int bit8 = ((int (*)(void *, int))VT_FN((CItem *)npc, VT_TEST_BEHAVIOR))(npc, 8);
	if (bit8)
		return;
	if (npc->aiState == NPC_STATE_ATTACK_TARGET)
		return;
	if (!CNPC_CanEngageTarget(npc, target))
		return;
	CNPC_EngageTarget(npc, target);
}

/*
 * 0x004AAB0E - CNPC::ScanAndEngageNearest
 *
 * Collects mobiles in range (8 when behaviorFlags has 0x80000, else
 * 0x12) and engages the closest one that passes CanEngageTarget.
 */
static void
CNPC_ScanAndEngageNearest(CNPC *npc)
{
	CVector nearbyList;
	uintptr_t *iter;
	CMobile *bestTarget;
	int bestDist;
	int range;
	int dist;
	char vecType = 0;

	if (npc->mobile.container.item.resourceEntity.entity.removedFromWorld)
		return;

	range = 0x12;
	if (npc->behaviorFlags & 0x80000)
		range = 8;

	CVector_Constructor(&nearbyList, &vecType);

	CollectNearbyMobiles(&nearbyList, CEntity_GetLocation(&npc->mobile.container.item.resourceEntity.entity), range);

	bestTarget = NULL;
	bestDist = 0xFFFF;

	iter = (uintptr_t *)nearbyList.begin;
	while (iter != (uintptr_t *)nearbyList.end) {
		CMobile *target = (CMobile *)(uintptr_t)*iter;
		if (CNPC_CanEngageTarget(npc, target)) {
			dist = Location_WrappedChebyshevDistance(
			        CEntity_GetLocation(&npc->mobile.container.item.resourceEntity.entity), CEntity_GetLocation(&target->container.item.resourceEntity.entity));
			if (bestTarget == NULL || dist < bestDist) {
				bestTarget = target;
				bestDist = dist;
			}
		}
		iter++;
	}

	if (bestTarget != NULL)
		CNPC_EngageTarget(npc, bestTarget);

	CVector_Destructor(&nearbyList);
}

/*
 * 0x004AAC3A - CNPC::RunawayTick
 *
 * RUNAWAY state: bail to IDLE when the actionTarget is gone or >~18
 * tiles away. Otherwise pick the first of up to 32 random points that
 * increases the distance from the target and walk there.
 */
static void
CNPC_RunawayTick(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	CItem *targetEnt;
	CLocation *myLoc, *targetLoc;
	int sqDist;

	targetEnt = CWorld_FindBySerial(g_World, npc->actionTarget);
	if (targetEnt == NULL || targetEnt->resourceEntity.entity.removedFromWorld != 0) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	CNPC_WalkAnimDispatch(mob);

	if (npc->aiByte2 != 0) {
		CNPC_WanderStep(mob, 6);
		npc->ltype = NPC_STATE_RUNAWAY;
		npc->stateInfo2 = NPC_STATE_RUNAWAY;
		return;
	}

	myLoc = &mob->container.item.resourceEntity.entity.location;
	targetLoc = &targetEnt->resourceEntity.entity.location;
	sqDist = DistBetween(myLoc, targetLoc);

	if (sqDist > 0x144) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	{
		int attempts = 0x20;
		CLocation escapeLoc;
		int candidateDist;

		escapeLoc.x = 0xFFFF;
		escapeLoc.y = 0xFFFF;
		escapeLoc.z = (int16_t)0xFFFF;

		while (attempts > 0) {
			int rx = GetRandomRange(0, 10) - 5;
			int ry = GetRandomRange(0, 10) - 5;
			rx += (int)(int16_t)myLoc->x;
			ry += (int)(int16_t)myLoc->y;
			escapeLoc.x = (uint16_t)rx;
			escapeLoc.y = (uint16_t)ry;
			escapeLoc.z = 0;

			candidateDist = DistBetween(targetLoc, &escapeLoc);
			if (candidateDist > sqDist)
				break;

			attempts--;
		}

		if (attempts > 0) {
			npc->patrolTarget = escapeLoc;
			npc->isWalking = 1;
			npc->ltype = NPC_STATE_RUNAWAY;
			npc->stateInfo2 = NPC_STATE_RUNAWAY;
		}
	}
}

/*
 * 0x004AAE01 - CNPC::FindStepToward
 *
 * Tries up to 32 random offsets of [-2,+2] tiles times step around the
 * NPC's own position, looking for one closer to target than the NPC is
 * now. On the first hit target is overwritten with it and 1 is
 * returned; 0 when all 32 tries miss.
 */
static __attribute__((unused)) int
CNPC_FindStepToward(CNPC *this, CLocation *target, int step)
{
	CLocation *here = &this->mobile.container.item.resourceEntity.entity.location;
	CLocation candidate;
	int tries, bestDist, dist, dx, dy;

	tries = 0x20;
	CLocation_Init(&candidate);
	bestDist = DistBetween(target, here);

	for (;;) {
		dx = GetRandomRange(1, 5) - 3;
		dy = GetRandomRange(1, 5) - 3;
		dx *= step;
		dy *= step;
		dx += (int16_t)here->x;
		dy += (int16_t)here->y;
		CLocation_Set(&candidate, (int16_t)dx, (int16_t)dy, 0);

		dist = DistBetween(&candidate, target);
		if (dist < bestDist) {
			CLocation_SetLoc(target, &candidate);
			return 1;
		}

		tries--;
		if (tries == 0)
			return 0;
	}
}

/*
 * 0x004AAF1E - CNPC hunger/loyalty base value
 *
 * Returns a combined hunger/mood score used as the input to loyalty
 * decisions: stomach as a percent of capacity, plus npcIndex mood bias.
 */
int
CNPC_GetHungerLevel(CNPC *npc)
{
	int val, mood;

	if (npc->hungerCapacity == 0)
		npc->hungerCapacity = 1;
	val = (int)npc->mobile.stomach * 100 / (int)npc->hungerCapacity;
	mood = (int)npc->npcInfo1_1;
	if (mood > 10)
		mood = 10;
	mood *= 10;
	mood = mood * 20 / 100 + 80;
	val = val * mood / 100;
	if ((npc->behaviorFlags & 0x4000) && !(npc->behaviorFlags & 0x800))
		val = val * 8 / 10;
	return val;
}

/*
 * 0x004AAFE6 - CNPC::CollectSameSpecies
 *
 * Builds the set of template ids that count as the same species as this
 * NPC - its own, the +/-1000 counterpart when it falls in 0-199 or
 * 1000-1199, and every id in the template's body-type list - then adds
 * every non-following hashed NPC with one of those templates to the
 * prey arrays at level 100 and action -1.
 *
 * The id buffer holds ten entries while the sources can produce eleven
 * (one own id, one counterpart, eight body types), so a full body-type
 * list overruns it into the loop counter that follows it on the stack.
 */
static __attribute__((unused)) void
CNPC_CollectSameSpecies(CNPC *this)
{
	uint16_t ids[10];
	uint8_t count;
	int i, j, k;
	CNPC *npc;

	count = 1;
	ids[0] = (uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this);

	if ((uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this) <= 0xC7) {
		ids[count] = (uint16_t)((uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this) + 0x3E8);
		count++;
	}

	if ((uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this) >= 0x3E8 && (uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this) <= 0x4AF) {
		ids[count] = (uint16_t)((uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this) - 0x3E8);
		count++;
	}

	if (g_TemplateBodyTypes[(uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this)] != NULL) {
		for (j = 0; j < (int)g_TemplateBodyTypes[(uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this)][0]; j++) {
			ids[count] = *(uint16_t *)(g_TemplateBodyTypes[(uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this)] + 2 + j * 2);
			count++;
		}
	}

	for (i = 0; i < 0x40; i++) {
		for (npc = g_NPCHash[i]; npc != NULL; npc = npc->npcHashNext) {
			if (npc->mobile.isFollower != 0)
				continue;

			for (k = 0; k < (int)count; k++) {
				if (ids[k] != (uint16_t)CResourceEntity_GetTemplateIndex((CItem *)npc) || npc == this)
					continue;

				if (g_EcoPreyCount >= 0x20)
					return;

				g_EcoPreyAction[g_EcoPreyCount] = -1;
				g_EcoPreyResType[g_EcoPreyCount] = g_ResTypeId_Self;
				g_EcoPreyTarget[g_EcoPreyCount] = npc;
				g_EcoPreyLevel[g_EcoPreyCount] = 100;
				g_EcoPreyCount++;
			}
		}
	}
}

/*
 * 0x004AB213 - CNPC::EnterWanderState
 *
 * Switches the NPC to WANDER with a random step budget in
 * [minSteps..maxSteps] and kicks off the walk animation.
 */
static void
CNPC_EnterWanderState(CNPC *npc, int minSteps, int maxSteps)
{
	CNPC_SetState(npc, NPC_STATE_WANDER);
	npc->wanderSteps = GetRandomRange(minSteps, maxSteps);
	CNPC_WalkAnimDispatch(&npc->mobile);
}

/*
 * 0x004AB24F - CMobile::IsCreatureBody
 *
 * True when templateIndex is in a creature range (0-199, 1000-1199,
 * 1580-1594, 1604-1619) or the mob is a player body.
 */
int
CMobile_IsCreatureBody(CMobile *this)
{
	uint16_t ti;

	ti = (uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this);

	if (ti <= 0xC7)                 /* 0-199 */
		return 1;
	if (ti >= 0x3E8 && ti <= 0x4AF) /* 1000-1199 */
		return 1;
	if (ti >= 0x644 && ti <= 0x653) /* 1604-1619 */
		return 1;
	if (ti >= 0x62C && ti <= 0x63A) /* 1580-1594 */
		return 1;

	return VT_IsPlayer((CItem *)this);
}

/*
 * 0x004AB310 - CMobile::IsHumanNPC
 *
 * True when templateIndex is in [500, 699] (humanoid NPC range).
 */
int
CMobile_IsHumanNPC(CMobile *this)
{
	uint16_t ti;

	ti = (uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this);
	return (ti >= 0x1F4 && ti <= 0x2BB);                    /* 500-699 */
}

/*
 * 0x004AB34C - NPC_PackMerge
 *
 * Resolves both mobs to their pack leaders and, when there is enough
 * aggregate hunger capacity, folds the weaker leader (and its followers)
 * under the stronger one by encumbrance limit.
 *
 * MODIFIED: UoDemo.exe gates the merge with an inverted comparison
 * (0x004AB432: cmp 0, total/4; jge merge) so it only merges when the
 * pack's combined hungerCapacity is <= 3. CNPC_InitFromResourceNodes
 * defaults hungerCapacity to 99, so the sum is always >> 3 and the
 * binary's pack merge is dead code for every normal creature - packing
 * wolves scan for packmates (ScanForTargets type 1) and call this, but
 * never actually form a pack. With FEAT_ECOLOGY enabled we use the
 * intended "enough aggregate capacity" direction so timber/dire/white/
 * grey wolves (templatestable.dat packing=1) pack up; the inert binary
 * test is kept exactly in the else branch.
 */
void
NPC_PackMerge(CMobile *mobA, CMobile *mobB)
{
	CMobile *leaderA, *leaderB;
	int totalFood;
	CMobile *cur;
	int limitA, limitB;
	CMobile *temp;
	CMobile *next;
	int count;

	leaderA = mobA;
	if (leaderA->isFollower)
		leaderA = leaderA->owner;

	leaderB = mobB;
	if (leaderB->isFollower)
		leaderB = leaderB->owner;

	if (leaderA == leaderB)
		return;

	totalFood = ((CNPC *)leaderA)->hungerCapacity + ((CNPC *)leaderB)->hungerCapacity;

	if (leaderA->hasFollowers) {
		for (cur = leaderA->firstFollower; cur != NULL; cur = cur->nextFollower)
			totalFood += ((CNPC *)cur)->hungerCapacity;
	}

	if (leaderB->hasFollowers) {
		for (cur = leaderB->firstFollower; cur != NULL; cur = cur->nextFollower)
			totalFood += ((CNPC *)cur)->hungerCapacity;
	}

	if (feat(FEAT_ECOLOGY)) {
		// Revive the demo's inert pack merge (see top comment): bail
		// only when there is too little aggregate capacity to pack.
		if (0 >= (totalFood + (totalFood < 0 ? 3 : 0)) / 4)
			return;
	} else {
		if (0 < (totalFood + (totalFood < 0 ? 3 : 0)) / 4)
			return;
	}

	limitB = CMobile_GetEncumbranceLimit(leaderB);
	limitA = CMobile_GetEncumbranceLimit(leaderA);
	if (limitB > limitA) {
		temp = leaderB;
		leaderB = leaderA;
		leaderA = temp;
	}

	count = 1;
	if (leaderB->hasFollowers) {
		for (cur = leaderB->firstFollower; cur != NULL; cur = next) {
			next = cur->nextFollower;
			CMobile_RemoveFollower(leaderB, cur);
			CMobile_AddFollower(leaderA, cur);
			count++;
		}
		leaderB->hasFollowers = 0;
	}

	CMobile_AddFollower(leaderA, leaderB);
	USED(count);
}

/*
 * 0x004AB4D8 - CNPC::TryFollowOppositeSex
 *
 * Locks the NPC onto other as a follow target when its resource type is
 * Self or Humans, its resource AI target flag is set, behaviour bit
 * 0x1000 is clear, it is not already following that serial and the two
 * sexes differ. A resourceAITarget of 2 also sets behaviour 0x2000 and
 * seeds followObj3 with a 100 to 1000 roll. Anything else drops the NPC
 * into state 11 with a 2 to 20 step wander countdown.
 */
static __attribute__((unused)) void
CNPC_TryFollowOppositeSex(CNPC *this, CItem *other)
{
	if ((this->resourceType == g_ResTypeId_Self || this->resourceType == g_ResTypeId_Humans) && this->resourceAITarget != 0 && (this->behaviorFlags & 0x1000) == 0 &&
	        this->followObj2 != other->serial && this->mobile.sex != ((CMobile *)other)->sex) {
		this->npcInfo1_1 = 1000;
		((void (*)(void *, int))VT_FN((CItem *)this, VT_SET_BEHAVIOR))(this, 0x1002);
		this->followObj1 = other->serial;
		this->followObj2 = this->followObj1;

		if (this->resourceAITarget == 2) {
			((void (*)(void *, int))VT_FN((CItem *)this, VT_SET_BEHAVIOR))(this, 0x2000);
			this->followObj3 = GetRandomRange(100, 1000);
		}

		CNPC_ShouldProcess(this);
		CNPC_SetState(this, 10);
	} else {
		CNPC_SetState(this, 11);
		this->wanderSteps = GetRandomRange(2, 20);
	}
}

/*
 * 0x004AB60E - CNPC::EatFood
 *
 * Consumes food through the NPC, its owner, or one of the owner's
 * followers, preferring the NPC itself when it has no pack.
 */
static int
CNPC_EatFood(CNPC *npc, CItem *food)
{
	CMobile *mob = &npc->mobile;
	CMobile *eater;
	CMobile *follower;
	int foodWeight;

	if (((int (*)(void *))VT_FN(food, VT_ITEM_CHECK_9C))(food))
		return 0;

	if (mob->hasFollowers == 0 && mob->isFollower == 0)
		return CNPC_ConsumeFood(npc, food);

	if (CNPC_ConsumeFood(npc, food))
		return 1;

	foodWeight = ((int (*)(void *))VT_FN(food, VT_GET_WEIGHT))(food);
	if (foodWeight > 0xFA)
		return 0;

	eater = mob;

	if (mob->isFollower != 0) {
		eater = mob->owner;
		if (CNPC_ConsumeFood((CNPC *)eater, food))
			return 1;
	}

	follower = eater->firstFollower;
	while (follower != NULL) {
		if (CNPC_ConsumeFood((CNPC *)follower, food))
			return 1;
		follower = follower->nextFollower;
	}

	return 0;
}

/*
 * 0x004AB702 - CNPC::ConsumeFood
 *
 * Tries to stow food inside the NPC's container, honoring weight,
 * capacity (baseInt / 10), and body-type checks, and routing food
 * consumed out of a corpse through CNPC_HandleCorpseEat.
 */
static int
CNPC_ConsumeFood(CNPC *npc, CItem *food)
{
	CMobile *mob = &npc->mobile;
	int foodWeight;
	int maxWeight;
	int storedWeight;
	int count;
	int cap;
	CItem *parent;
	CLocation tempLoc;

	foodWeight = ((int (*)(void *))VT_FN(food, VT_GET_WEIGHT))(food);

	maxWeight = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_MAX_WEIGHT))(npc);
	storedWeight = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_STORED_WEIGHT))(npc);

	count = CContainer_CountItems(&mob->container, 0);

	cap = (int)(uint16_t)mob->baseInt / 10;
	if (cap == 0)
		cap = 1;

	if ((food->resourceEntity.entity.bodyType & 0xFFFF) == 0x2006)
		return 0;

	if (foodWeight + storedWeight > maxWeight)
		return 0;
	if (foodWeight > 0xFA)
		return 0;
	if (count + 1 > cap)
		return 0;

	if (mob->container.item.parent != NULL) {
		parent = mob->container.item.parent;
		while (parent->parent != NULL)
			parent = parent->parent;
		if ((parent->resourceEntity.entity.bodyType & 0xFFFF) == 0x2006) {
			CNPC_HandleCorpseEat(npc, parent);
		}
	}

	CLocation_Set(&tempLoc, -1, -1, 0);

	((void (*)(void *))VT_FN(food, VT_HIDE))(food);

	((void (*)(void *, void *, CLocation *))VT_FN(food, VT_ADD_TO_CONTAINER))(food, npc, &tempLoc);

	return 1;
}

/*
 * 0x004AB82F - CNPC::Loiter
 *
 * Fires @loiter and, if the NPC survives, records range/loc and
 * transitions to LOITER with running cleared.
 */
void
CNPC_Loiter(CNPC *npc, int range, CLocation loc)
{
	Entity_ExecuteEvent(&npc->mobile.container.item.resourceEntity.entity, 0x12);

	if (npc != g_currentNPC)
		return;

	npc->loiterData = range;
	npc->loiterLoc = loc;
	CNPC_SetState(npc, NPC_STATE_LOITER);
	CNPC_SetRunState(&npc->mobile, 0);
}

/*
 * 0x004AB889 - CNPC::StartWander
 *
 * Transitions an NPC into the wander-in-progress state (SLEEP/12).
 * Humanoid NPCs skip straight to returnState; other NPCs play the
 * idle walk anim and store the step budget.
 */
void
CNPC_StartWander(CNPC *npc, int steps, int returnState)
{
	CMobile *mob = &npc->mobile;

	if (CMobile_IsHumanNPC(mob)) {
		CNPC_SetState(npc, returnState);
		return;
	}

	CNPC_SetRunState(mob, 0);
	CMobile_NPC_SetAIState(mob, 2);
	npc->wanderBurstCount = (uint32_t)steps;
	npc->ltype = (uint32_t)returnState;
	CNPC_SetState(npc, NPC_STATE_SLEEP);
	CNPC_ShouldProcess(npc);
}

/*
 * 0x004AB8EE - CNPC::WanderCountdown
 *
 * Decrements wanderBurstCount; humanoid NPCs or NPCs whose budget is
 * exhausted exit to the stored ltype return state.
 */
static void
CNPC_WanderCountdown(CNPC *npc)
{
	npc->wanderBurstCount--;
	if ((int32_t)npc->wanderBurstCount < 1 || CMobile_IsHumanNPC(&npc->mobile)) {
		CNPC_SetState(npc, npc->ltype);
	}
}

/*
 * 0x004AB938 - CNPC::ResourceWander
 *
 * Resource-NPC wander tick: non-resource NPCs fall through to a plain
 * random wander; resource NPCs pick a new patrolTarget within +/-10
 * tiles of loiterLoc (avoiding the opposite quadrant and capped at
 * dist < 49, 30 retries), with occasional idle/vendor poses.
 */
static void
CNPC_ResourceWander(CNPC *npc)
{
	int roll;
	int dx, dy;
	uint8_t dir;
	CLocation targetLoc;
	int retries;

	if (!(npc->behaviorFlags & 0x20000)) {
		npc->loiterData--;
		if ((int32_t)npc->loiterData < 1)
			CNPC_StartWander(npc, GetRandomRange(15, 30), 10);
		return;
	}

	if ((int16_t)npc->loiterLoc.x < 0 || (int16_t)npc->loiterLoc.y < 0)
		CLocation_SetLoc(&npc->loiterLoc, &npc->mobile.container.item.resourceEntity.entity.location);

	roll = GetRandomRange(1, 10);
	if (roll == 3) {
		if (VT_IsVendor((CItem *)npc)) {
			CMobile_NPC_SetAIState(&npc->mobile, 2);
			return;
		}
		if (GetRandomRange(1, 4) == 3) {
			CMobile_NPC_SetAIState(&npc->mobile, 2);
			return;
		}
	} else if (roll >= 4 && roll <= 6) {
		CMobile_NPC_SetAIState(&npc->mobile, 3);
		return;
	}

	CLocation_SetLoc(&targetLoc, &npc->loiterLoc);
	retries = 30;
	for (;;) {
		dx = GetRandomRange(0, 20) - 10;
		dy = GetRandomRange(0, 20) - 10;

		if (dx > 0 && dy > 0)
			dir = 0;
		else if (dx > 0 && dy < 0)
			dir = 1;
		else if (dx < 0 && dy < 0)
			dir = 2;
		else
			dir = 3;

		if ((dir ^ 2) != npc->lastWanderQuadrant) {
			CLocation_Set(
			        &targetLoc, npc->mobile.container.item.resourceEntity.entity.location.x + dx, npc->mobile.container.item.resourceEntity.entity.location.y + dy, 0);
			if (DistBetween(&npc->loiterLoc, &targetLoc) < 49)
				break;
		}

		retries--;
		if (retries < 0)
			break;
	}

	if (retries < 0) {
		VT_IsVendor((CItem *)npc);
		return;
	}

	npc->lastWanderQuadrant = dir;
	CLocation_SetLoc(&npc->patrolTarget, &targetLoc);
	npc->ltype = 6;
	npc->stateInfo2 = 6;
	npc->isWalking = 1;
}

/*
 * 0x004ABBAB - CNPC::PickBestPrey
 *
 * Scores every prey entry whose level reaches the NPC's per-action
 * threshold by distance times level, halving the score for entries that
 * are not mobiles, and keeps the lowest. Writes the winning index to
 * outIndex (-1 when nothing qualified) and returns its target.
 */
static __attribute__((unused)) CNPC *
CNPC_PickBestPrey(CNPC *this, int *outIndex)
{
	const uint8_t *thresholds = (const uint8_t *)this->resTplPtr1;
	CLocation *targetLoc;
	int best, score, i;

	best = 0x7FFFFFFF;
	*outIndex = -1;

	for (i = 0; i < g_EcoPreyCount; i++) {
		if (g_EcoPreyLevel[i] < (int)thresholds[g_EcoPreyAction[i]])
			continue;

		targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)g_EcoPreyTarget[i], VT_GET_LOCATION))(g_EcoPreyTarget[i]);
		score = DistBetween(targetLoc, &this->mobile.container.item.resourceEntity.entity.location);
		score *= g_EcoPreyLevel[i];

		if (((int (*)(void *))VT_FN((CItem *)g_EcoPreyTarget[i], VT_IS_MOBILE))(g_EcoPreyTarget[i]) == 0)
			score >>= 1;

		if (score < best) {
			best = score;
			*outIndex = i;
		}
	}

	if (*outIndex < 0)
		return NULL;
	return g_EcoPreyTarget[*outIndex];
}

/*
 * 0x004ABCC8 - CNPC::RelocateToSpawn
 *
 * Finds a spawn spot within range 2 of spawnLoc, hides the NPC, and
 * drops it there. Returns 1 on success, 0 when no slot is free.
 */
static int
CNPC_RelocateToSpawn(CNPC *npc, CLocation *spawnLoc)
{
	CLocation loc;
	int height;
	int moveType;

	CLocation_SetLoc(&loc, spawnLoc);

	moveType = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_MOVEMENT_TYPE))(npc) & 0xFF;
	height = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_HEIGHT))(npc);

	if (!FindSpawnSpot(&loc, 0, 2, height, moveType, (CItem *)npc))
		return 0;

	((void (*)(void *))VT_FN((CItem *)npc, VT_HIDE))(npc);
	((void (*)(void *, CLocation *))VT_FN((CItem *)npc, VT_DROP_AT_FEET))(npc, &loc);
	return 1;
}

/*
 * 0x004ABD40 - CNPC resource pack scan
 *
 * Keeps a resource-gathering NPC's pack under half its slot capacity
 * and half max weight by hiding and dropping the first item at the
 * NPC's feet until both limits are satisfied.
 */
static void
CNPC_ResourcePackScan(CNPC *npc)
{
	int maxWeight, storedWeight, count, capacity;
	CItem *cur;
	int pass;
	int found;

	while (1) {
		maxWeight = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_MAX_WEIGHT))(npc);
		storedWeight = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_STORED_WEIGHT))(npc);
		count = CContainer_CountItems(&npc->mobile.container, 0);
		capacity = npc->mobile.baseInt / 10;
		if (capacity == 0)
			capacity = 1;
		capacity /= 2;
		if (capacity == 0)
			capacity = 1;
		maxWeight /= 2;

		found = 0;
		for (pass = 0;;) {
			if (count > capacity || storedWeight > maxWeight) {
				cur = npc->mobile.container.contents;
				while (cur != NULL) {
					if (pass != 0) {
						CNPC_ShouldProcess(npc);
						((void (*)(void *))VT_ENT_FN(&cur->resourceEntity.entity, VT_HIDE))(cur);
						((void (*)(void *, void *))VT_ENT_FN(&cur->resourceEntity.entity, VT_DROP_AT_FEET))(
						        cur, &npc->mobile.container.item.resourceEntity.entity.location);
						found = 1;
						break;
					} else {
						cur = cur->spatialNext;
					}
				}
			} else {
				break;
			}
			if (found)
				break;
			pass++;
			if (pass == 2)
				break;
		}
		if (!found)
			return;
	}
}

/*
 * 0x004ABE66 - CNPC resource wander post-handler
 *
 * Resource-gather post-step: kick off a LOITER-return wander, reset
 * loiterData and loiterLoc to homeLoc, try to eat, then prune the
 * pack on every follower and on the NPC.
 */
static void
CNPC_ResourceWanderPost(CNPC *npc)
{
	CMobile *cur;

	CNPC_StartWander(npc, GetRandomRange(20, 30), NPC_STATE_LOITER);
	npc->loiterData = GetRandomRange(50, 500);
	CLocation_SetLoc(&npc->loiterLoc, &npc->homeLoc);
	CNPC_TryEatFood(npc);

	if (npc->mobile.hasFollowers != 0) {
		cur = npc->mobile.firstFollower;
		while (cur != NULL) {
			CNPC_ResourcePackScan((CNPC *)cur);
			cur = cur->nextFollower;
		}
	}
	CNPC_ResourcePackScan(npc);
}

/*
 * 0x004ABEFE - CNPC::PurseShelterHandler
 *
 * PURSE_SHELTER tick: with behavior 0x800 set, runs ResourceWanderPost;
 * with no resource target, starts a random wander. Otherwise locates
 * the target's resource node, consumes resourceRate from it, stamps
 * the gathering info into homeInfo1/2/3 and kicks off the next cycle.
 */
static void
CNPC_PurseShelterHandler(CNPC *npc)
{
	CItem *target;
	CResourceNode *node;
	int amount;

	npc->speechCounter = 0;

	if (npc->behaviorFlags & 0x800) {
		CNPC_ResourceWanderPost(npc);
		return;
	}

	if (!npc->resourceAITarget) {
		CNPC_ShouldProcess(npc);
		CNPC_StartWander(npc, GetRandomRange(20, 30), NPC_STATE_LOITER);
		npc->loiterData = GetRandomRange(50, 500);
		CLocation_SetLoc(&npc->loiterLoc, &npc->mobile.container.item.resourceEntity.entity.location);
		return;
	}

	target = CWorld_FindBySerial(g_World, npc->resourceTargetSerial);
	if (target == NULL || ((int (*)(void *))VT_ENT_FN(&target->resourceEntity.entity, VT_IS_MOBILE))(target) != 0 || target->resourceEntity.entity.removedFromWorld != 0) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	node = CResourceEntity_FindNode(target, (uint8_t)npc->resourceType, 3);
	if (node == NULL) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	amount = node->value3;
	if (amount < npc->resourceRate) {
		CNPC_ShouldProcess(npc);
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	CLocation_SetLoc(&npc->homeLoc, &npc->patrolTarget);

	((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x800);

	npc->homeInfo1 = npc->resourceType;
	npc->homeInfo2 = npc->resourceRate;
	npc->homeInfo3 = npc->resourceTargetSerial;

	CResourceEntity_NotifyPreModify(target);
	node->value3 -= npc->resourceRate;
	CResourceEntity_NotifyPostModify(target);
	CResourceEntity_NotifyPostModifyIfActive(target);

	CNPC_ShouldProcess(npc);
	CNPC_ResourceWanderPost(npc);
}

/*
 * 0x004AC0E3 - CNPC::CompareFightStrength
 *
 * Sums HP plus count-bonus for each side's pack (plus a random bonus
 * for this side) and returns 0 when this side wins, 1 when target is
 * ahead, or 2 when target is dominant and this NPC can flee.
 */
static int
CNPC_CompareFightStrength(CNPC *npc, CMobile *target)
{
	CMobile *thisMob;
	CMobile *targetMob;
	CMobile *follower;
	int thisHP, targetHP;
	int thisCount, targetCount;

	thisMob = &npc->mobile;

	if (thisMob->isFollower)
		thisMob = thisMob->owner;

	targetMob = target;
	if (target->isFollower)
		targetMob = target->owner;

	thisHP = (int)CMobile_GetHp(thisMob);
	thisCount = 1;
	if (thisMob->hasFollowers) {
		follower = thisMob->firstFollower;
		while (follower != NULL) {
			thisHP += (int)CMobile_GetHp(follower);
			thisCount++;
			follower = follower->nextFollower;
		}
	}

	targetHP = (int)CMobile_GetHp(targetMob);
	targetCount = 1;
	if (targetMob->hasFollowers) {
		follower = targetMob->firstFollower;
		while (follower != NULL) {
			targetHP += (int)CMobile_GetHp(follower);
			targetCount++;
			follower = follower->nextFollower;
		}
	}

	thisHP += thisCount * 10;
	targetHP += targetCount * 10;

	thisHP += GetRandomRange(1, thisCount * 10);

	if (thisHP > targetHP)
		return 0;

	thisHP += thisHP / 2;
	if (targetHP < thisHP)
		return 1;

	if (CNPC_CanFlee(npc))
		return 2;
	return 1;
}

/*
 * 0x004AC248 - AddToFollowerTargetArray
 *
 * Appends target to g_followerTargetArray, skipping duplicates and
 * stopping at the 0x100 entry cap.
 */
static void
NPC_AddToFollowerTarget(CMobile *target)
{
	CMobile **ptr;
	int i;

	if (g_followerTargetCount >= 0x100)
		return;
	ptr = g_followerTargetArray;
	for (i = 0; i < g_followerTargetCount; i++) {
		if (*ptr == target)
			return;
		ptr++;
	}
	g_followerTargetArray[g_followerTargetCount] = target;
	g_followerTargetCount++;
}

/*
 * 0x004AC2B8 - CNPC::FollowerCombatHandler
 *
 * Coordinates pack combat: builds an entity array of the leader plus
 * followers and a target array of attackers plus their packs, then
 * greedy-assigns entities to their nearest target (unassigned ones get
 * random targets). Guarded against recursive entry.
 */
void
CNPC_FollowerCombatHandler(CMobile *this)
{
	CMobile *iter;
	CItem *ent;
	CMobile *mob;
	int i;
	CMobile **ptr;
	int bestDist;
	int bestTarget;
	CLocation entLoc, targetLoc;
	int dist;
	int k;
	int displaced;
	int entityIdx;
	CMobile *entity;
	int randomIdx;

	if (g_followerCombatGuard)
		return;
	g_followerCombatGuard = 1;

	g_followerTargetCount = 0;
	g_followerEntityCount = 0;

	g_followerEntityArray[g_followerEntityCount] = this;
	g_followerEntityCount++;

	iter = this->firstFollower;
	while (iter != NULL) {
		if (g_followerEntityCount >= 0x100)
			break;
		g_followerEntityArray[g_followerEntityCount] = iter;
		g_followerEntityCount++;
		iter = iter->nextFollower;
	}

	{
		CSerialNode *sentinel, *snode;
		sentinel = this->attackerList.data;
		for (snode = sentinel->next; snode != sentinel; snode = snode->next) {
			ent = CWorld_FindBySerial(g_World, snode->serial);
			if (ent == NULL)
				continue;
			if (!VT_IsMobile(ent))
				continue;
			mob = (CMobile *)ent;
			if (mob->isFollower)
				mob = mob->owner;
			NPC_AddToFollowerTarget(mob);
			iter = mob->firstFollower;
			while (iter != NULL) {
				NPC_AddToFollowerTarget(iter);
				iter = iter->nextFollower;
			}
		}
	}

	for (i = 0; i < g_followerTargetCount; i++) {
		g_followerBestEntity[i] = -1;
		g_followerBestDist[i] = 0x7FFFFFFF;
	}

	k = g_followerEntityCount - 1;
	while (k >= 0) {
		bestDist = 0x7FFFFFFF;
		bestTarget = -1;

		CLocation_SetLoc(&entLoc, &g_followerEntityArray[k]->container.item.resourceEntity.entity.location);

		ptr = g_followerTargetArray;
		for (i = 0; i < g_followerTargetCount; i++, ptr++) {
			CLocation_SetLoc(&targetLoc, &(*ptr)->container.item.resourceEntity.entity.location);
			dist = DistBetween(&entLoc, &targetLoc);
			if (dist >= bestDist)
				continue;
			if (g_followerBestEntity[i] >= 0 && bestDist >= g_followerBestDist[i])
				continue;
			bestDist = dist;
			bestTarget = i;
		}

		if (bestTarget >= 0) {
			if (g_followerBestEntity[bestTarget] >= 0) {
				displaced = g_followerBestEntity[bestTarget];
				g_followerBestEntity[bestTarget] = k;
				g_followerBestDist[bestTarget] = bestDist;
				k = displaced;
				continue;
			}
			g_followerBestEntity[bestTarget] = k;
			g_followerBestDist[bestTarget] = bestDist;
		}
		k--;
	}

	memset(g_followerFlagged, 0, g_followerEntityCount);

	ptr = g_followerTargetArray;
	for (i = 0; i < g_followerTargetCount; i++, ptr++) {
		if (g_followerBestEntity[i] < 0)
			continue;
		entityIdx = g_followerBestEntity[i];
		if (entityIdx >= g_followerEntityCount)
			continue;
		g_followerFlagged[entityIdx] = 1;
		entity = g_followerEntityArray[entityIdx];
		CNPC_StartCombatAI(entity, *ptr);
	}

	for (i = 0; i < g_followerEntityCount; i++) {
		if (g_followerFlagged[i])
			continue;
		randomIdx = GetRandomRange(0, g_followerTargetCount - 1);
		entity = g_followerEntityArray[i];
		CNPC_StartCombatAI(entity, g_followerTargetArray[randomIdx]);
	}

	g_followerCombatGuard = 0;
}

/*
 * 0x004AC69B - CNPC::CheckPetHunger
 *
 * Returns 1 when this NPC (or anyone in its follower chain) is hungry,
 * i.e. an individual stomach under its capacity / 8 or pack stomach
 * under half total capacity.
 */
static int
CNPC_CheckPetHunger(CNPC *npc)
{
	int threshold;
	int totalStomach;
	int totalCapacity;
	CMobile *walk;

	if (npc->mobile.hasFollowers != 0) {
		threshold = (int)npc->hungerCapacity >> 3;
		if (threshold == 0)
			threshold = 1;
		if ((int)npc->mobile.stomach < threshold)
			return 1;

		totalStomach = (int)npc->mobile.stomach;
		totalCapacity = (int)npc->hungerCapacity;
		walk = npc->mobile.firstFollower;
		while (walk != NULL) {
			threshold = (int)((CNPC *)walk)->hungerCapacity >> 3;
			if (threshold == 0)
				threshold = 1;
			if ((int)walk->stomach < threshold)
				return 1;
			totalStomach += (int)walk->stomach;
			totalCapacity += (int)((CNPC *)walk)->hungerCapacity;
			walk = walk->nextFollower;
		}
		if (totalStomach < totalCapacity / 2)
			return 1;
	} else {
		if ((int)npc->mobile.stomach < (int)npc->hungerCapacity / 2)
			return 1;
	}
	return 0;
}

/*
 * 0x004AC7B7 - ResourceQuery_Stub
 *
 * No-op stub called from HandlePacket_ResourceQuery.
 */
void
ResourceQuery_Stub(void)
{
}

/*
 * 0x004AC7BC - CNPC::HandleCorpseEat
 *
 * Records that this NPC has fed from the corpse by appending its name to
 * the corpse's "users" ObjVar list (or creating the list first).
 *
 * MODIFIED: where the binary wraps ObjVar_SetStr around a by-value CString,
 * we call CEntity_SetObjVar directly and destruct manually.
 */
static void
CNPC_HandleCorpseEat(CNPC *npc, CItem *corpse)
{
	CList *tagEntity;
	CString nameStr;
	char *name;

	tagEntity = CResourceEntity_GetTagEntity(corpse, "users");

	name = ((char *(*)(void *))VT_FN((CItem *)npc, VT_GET_NAME))(npc);
	CString_Constructor(&nameStr, name);

	if (tagEntity != NULL) {
		if (CList_Find(tagEntity, 1, (uintptr_t)&nameStr) == 0)
			CList_Append(tagEntity, 1, (uintptr_t)&nameStr);
	} else {
		CList *newList;
		CList *list;

		newList = (CList *)OperatorNew(sizeof(CList));
		if (newList != NULL)
			list = CList_Constructor(newList);
		else
			list = NULL;

		CList_Append(list, 1, (uintptr_t)&nameStr);

		{
			CString _n;
			CString_Constructor(&_n, "users");
			ObjVar_SetStr(corpse, &_n, 5, (uintptr_t)list);
		}

		if (list != NULL)
			CList_ScalarDelete(list, 1);
	}

	CString_Destructor(&nameStr);
}

/*
 * 0x004AC8E7 - ResEntitySlot_FindTypeThreeValue
 *
 * Scans slot slotIndex's resource-node chain for the first type 3 node
 * whose id is non-zero and equal to id, and returns its value1.
 * Returns -1 when there is no such node.
 */
static __attribute__((unused)) int
ResEntitySlot_FindTypeThreeValue(int slotIndex, int id)
{
	CResourceNode *node;

	for (node = g_ResEntitySlots[slotIndex].nodeHead; node != NULL; node = node->next) {
		if (node->type == 3 && node->id != 0 && node->id == id)
			return node->value1;
	}
	return -1;
}

/*
 * 0x004AC946 - CreateItemAtLocation
 *
 * Creates item id, hues it from the tiledata entry for its body type
 * and drops it at loc. Returns the item, or NULL when it no longer
 * validates against the world.
 *
 * The binary copies loc to entity+0x10 - CResourceEntity::nextInContainer,
 * six bytes past CEntity::location at +0x0A - so the copy lands on the
 * container link instead of the coordinates. Reproduced as it stands;
 * the drop below re-places the item anyway.
 */
static __attribute__((unused)) CItem *
CreateItemAtLocation(uint16_t id, CLocation loc)
{
	CItem *item;

	item = CWorld_CreateItem(g_World, id);
	if (item != NULL) {
		CLocation_SetLoc((CLocation *)(void *)&item->resourceEntity.nextInContainer, &loc);
		item->resourceEntity.entity.color = g_ItemTileData[CEntity_GetBodyType(item)].value2;
		((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, &loc);
		if (ValidateInWorld(item) == 0)
			item = NULL;
	}
	return item;
}

/*
 * 0x004AC9C0 - CNPC::WalkAnimDispatch
 *
 * Sets run state to 1 for flying/ghost mobs (movementType == 2), else 0.
 */
static void
CNPC_WalkAnimDispatch(CMobile *mob)
{
	if (CMobile_GetMovementType(mob) == 2)
		CNPC_SetRunState(mob, 1);
	else
		CNPC_SetRunState(mob, 0);
}

/*
 * 0x004AC9F5 - Broadcast an animation packet to nearby players
 *
 * Builds and broadcasts the ANIM packet (0x6E, 14 bytes) to players
 * within 20 tiles of mob.
 */
void
BroadcastAnimation(CMobile *mob, uint16_t action, uint16_t frameCount, uint16_t repeatCount, uint8_t backward, uint8_t repeat, uint8_t delay)
{
	uint8_t pktBuf[16];

	PacketManager_MakePacket_ANIM(pktBuf, mob->container.item.serial, action, frameCount, repeatCount, backward, repeat, delay);
	BroadcastToNearby(pktBuf, &mob->container.item.resourceEntity.entity.location, 20);
}

/*
 * 0x004ACA3C - CNPC::SetRunState
 *
 * Broadcasts a forward or reversed walk animation on run-state changes
 * (skipped for bodyType >= 150) and stores the new state in aiByte3.
 */
void
CNPC_SetRunState(CMobile *mob, int running)
{
	CNPC *npc = (CNPC *)mob;
	uint16_t bodyType;

	if (running == 1) {
		if (npc->aiByte3 == 1)
			goto end;
		bodyType = mob->container.item.resourceEntity.entity.bodyType;
		if (bodyType >= 0x96)
			goto end;
		BroadcastAnimation(mob, 0x14, 3, 1, 0, 0, 1);
		goto end;
	}
	if (running == 1)
		goto end;
	if (npc->aiByte3 != 1)
		goto end;
	bodyType = mob->container.item.resourceEntity.entity.bodyType;
	if (bodyType >= 0x96)
		goto end;
	BroadcastAnimation(mob, 0x14, 3, 1, 1, 0, 1);
end:
	npc->aiByte3 = (uint8_t)running;
}

/*
 * 0x004ACB07 - CMobile::NPC_SetAIState
 *
 * Plays the walk/run/idle/swing animation and idle SFX for AI states 1-5,
 * selecting frames and aiDelayCounter by bodyType range.
 *
 * FIXED: the binary's case-3 body<400 branch compares against 1 twice,
 * making the r==2 branch dead code. We compare against 2 so the 0x0A
 * animation actually plays for body 200-399.
 */
void
CMobile_NPC_SetAIState(CMobile *mob, int state)
{
	uint16_t body;
	int r;
	CNPC *npc;

	body = mob->container.item.resourceEntity.entity.bodyType;
	npc = (CNPC *)mob;

	// BroadcastAnimation args: action, frameCount, repeatCount, backward, repeat, delay
	switch (state) {

	case 1: // WANDER - gentle walk
		if (body < 150) {
			if (body != 8 && body != 22) {
				// Body 0x15: repeat=1 (binary special case)
				BroadcastAnimation(mob, 0x0B, 5, 1, 0, (body == 0x15) ? 1 : 0, 1);
			}
			npc->aiDelayCounter = 0x0A;
		} else if (body < 200) {
			BroadcastAnimation(mob, 5, 5, 1, 0, 0, 1);
			npc->aiDelayCounter = 0x0A;
		} else if (body < 400) {
			BroadcastAnimation(mob, 3, 5, 1, 0, 0, 1);
			npc->aiDelayCounter = 0x0A;
		} else {
			BroadcastAnimation(mob, 0x22, 2, 1, 0, 0, 1); // human walk
			npc->aiDelayCounter = 4;
		}
		PlaySoundAtEntity((CItem *)mob, mob->sfxNotice, 0);
		break;

	case 2: // IDLE SOUND
		PlaySoundAtEntity((CItem *)mob, mob->sfxIdle, 0);
		break;

	case 3: // WANDER WITH VARIETY
		if (body < 150) {
			r = GetRandomRange(1, 2);
			if (r == 1) {
				BroadcastAnimation(mob, 0x11, 5, 1, 0, 0, 1);
				npc->aiDelayCounter = 0x0A;
			} else if (body != 5) {
				BroadcastAnimation(mob, 0x12, 5, 1, 0, 0, 1);
				npc->aiDelayCounter = 0x0A;
			}
		} else if (body < 200) {
			r = GetRandomRange(1, 2);
			if (r == 1) {
				BroadcastAnimation(mob, 3, 15, 1, 0, 0, 1);
				npc->aiDelayCounter = 0x0A;
			} else {
				BroadcastAnimation(mob, 4, 20, 1, 0, 0, 1);
				npc->aiDelayCounter = 0x0A;
			}
		} else if (body < 400) {
			r = GetRandomRange(1, 3);
			if (r == 1) {
				BroadcastAnimation(mob, 9, 5, 1, 0, 0, 1);
				npc->aiDelayCounter = 0x0A;
			} else if (r == 2) {
				BroadcastAnimation(mob, 0x0A, 3, 1, 0, 0, 1);
				npc->aiDelayCounter = 6;
			} else {
				BroadcastAnimation(mob, 3, 5, 1, 0, 0, 1);
				npc->aiDelayCounter = 0x0A;
			}
		} else {
			r = GetRandomRange(1, 2);
			if (r == 1) {
				BroadcastAnimation(mob, 5, 5, 1, 0, 1, 1); // backward=1
				npc->aiDelayCounter = 4;
			} else {
				BroadcastAnimation(mob, 6, 5, 1, 0, 0, 1);
				npc->aiDelayCounter = 4;
			}
		}
		PlaySoundAtEntity((CItem *)mob, mob->sfxIdle, 0);
		break;

	case 4: // COMBAT SWING - play attack animation + weapon sound
		if (!VT_IsVendor((CItem *)mob)) {
			Combat_PlaySwingAnimation(mob, CMobile_GetWeapon(mob), mob);
			Combat_PlayMeleeMissSfx((CItem *)mob, (CItem *)mob, (CItem *)CMobile_GetWeapon(mob));
			npc->aiDelayCounter = 0x0A;
		}
		break;

	case 5: // COMBAT WANDER
		if (body < 150) {
			if (body != 8 && body != 22) {
				// Body 0x15: repeat=1 (binary special case), others repeat=0
				BroadcastAnimation(mob, 0x0B, 5, 1, 0, (body == 0x15) ? 1 : 0, 1);
			}
			npc->aiDelayCounter = 0x0A;
		} else if (body < 200) {
			BroadcastAnimation(mob, 3, 15, 1, 0, 0, 1);
			npc->aiDelayCounter = 0x0A;
		} else if (body < 400) {
			// Large creatures skip combat-wander animation and fall
			// through to the sound without resetting aiDelayCounter.
		} else {
			BroadcastAnimation(mob, 0x21, 5, 1, 0, 0, 1); // human combat walk
			npc->aiDelayCounter = 4;
		}
		PlaySoundAtEntity((CItem *)mob, mob->sfxNotice, 0);
		break;
	}
}

/*
 * 0x004D7405 - NPC_CalcDirectionSimple
 *
 * Returns the 0-7 direction index from target toward source by
 * mapping {-1,0,+1} X/Y deltas through the g_DirOffset table, with a
 * random fallback.
 */
int
NPC_CalcDirectionSimple(CItem *target, CItem *source)
{
	static const int32_t dirX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }; // 0061BB28
	static const int32_t dirY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 }; // 0061BB50
	int dx, dy;
	CLocation *srcLoc, *tgtLoc;
	int i;

	dx = 0;
	dy = 0;

	tgtLoc = CEntity_GetLocation(&source->resourceEntity.entity);
	srcLoc = CEntity_GetLocation(&target->resourceEntity.entity);
	if (tgtLoc->x < srcLoc->x)
		dx = -1;

	tgtLoc = CEntity_GetLocation(&source->resourceEntity.entity);
	srcLoc = CEntity_GetLocation(&target->resourceEntity.entity);
	if (tgtLoc->y < srcLoc->y)
		dy = -1;

	tgtLoc = CEntity_GetLocation(&source->resourceEntity.entity);
	srcLoc = CEntity_GetLocation(&target->resourceEntity.entity);
	if (tgtLoc->x > srcLoc->x)
		dx = 1;

	tgtLoc = CEntity_GetLocation(&source->resourceEntity.entity);
	srcLoc = CEntity_GetLocation(&target->resourceEntity.entity);
	if (tgtLoc->y > srcLoc->y)
		dy = 1;

	tgtLoc = CEntity_GetLocation(&source->resourceEntity.entity);
	srcLoc = CEntity_GetLocation(&target->resourceEntity.entity);
	if (tgtLoc->x == srcLoc->x)
		dx = 0;

	tgtLoc = CEntity_GetLocation(&source->resourceEntity.entity);
	srcLoc = CEntity_GetLocation(&target->resourceEntity.entity);
	if (tgtLoc->y == srcLoc->y)
		dy = 0;

	for (i = 0; i < 8; i++) {
		if (dirX[i] == dx && dirY[i] == dy)
			return i;
	}

	return GetRandomRange(0, 7);
}

/*
 * 0x004D7536 - NPC_CalcDirectionBetweenLocs
 *
 * Same mapping as NPC_CalcDirectionSimple but on two locations passed
 * by value. The table covers every {-1,0,+1} pair but {0,0}, so the
 * fallback is reached only when the two locations coincide, and it
 * returns -1 where NPC_CalcDirectionSimple rolls a random direction.
 */
static __attribute__((unused)) int
NPC_CalcDirectionBetweenLocs(CLocation target, CLocation source)
{
	static const int32_t dirX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }; // 0061BB28
	static const int32_t dirY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 }; // 0061BB50
	int dx, dy;
	int i;

	dx = 0;
	dy = 0;

	if ((int16_t)source.x < (int16_t)target.x)
		dx = -1;
	if ((int16_t)source.y < (int16_t)target.y)
		dy = -1;
	if ((int16_t)source.x > (int16_t)target.x)
		dx = 1;
	if ((int16_t)source.y > (int16_t)target.y)
		dy = 1;
	if ((int16_t)source.x == (int16_t)target.x)
		dx = 0;
	if ((int16_t)source.y == (int16_t)target.y)
		dy = 0;

	for (i = 0; i < 8; i++) {
		if (dirX[i] == dx && dirY[i] == dy)
			return i;
	}

	return -1;
}

/*
 * 0x004D7600 - AdvancePhaseIndex
 *
 * Increments *phasePtr by 4 modulo 8.
 */
void
AdvancePhaseIndex(int *phasePtr)
{
	*phasePtr += 4;
	if (*phasePtr > 8)
		*phasePtr -= 8;
}

/*
 * 0x004D7CD0 - PickRandomWorldLocation
 *
 * Writes a randomly chosen entry of the location table into out.
 */
static __attribute__((unused)) void
PickRandomWorldLocation(CLocation *out)
{
	/*
	 * 0x00625300 - 137 world locations, read only from here. Entries 0
	 * to 64 trace a continuous route; the rest are scattered across the
	 * map. Stored as 32-bit fields of which only the low word is read.
	 */
	// clang-format off
	static const struct {
		int32_t x, y, z;
	} table[137] = {
		{ 1483, 2167, 0 }, { 1495, 2190, 0 }, { 1498, 2204, 0 }, { 1494, 2221, 2 },
		{ 1503, 2242, 5 }, { 1525, 2245, 5 }, { 1549, 2255, 0 }, { 1561, 2281, 0 },
		{ 1583, 2283, 0 }, { 1612, 2261, 5 }, { 1606, 2297, 0 }, { 1634, 2322, 0 },
		{ 1646, 2354, 0 }, { 1599, 2393, 5 }, { 1612, 2420, 0 }, { 1636, 2454, 0 },
		{ 1670, 2481, 10 }, { 1666, 2516, 5 }, { 1661, 2549, 5 }, { 1666, 2590, 5 },
		{ 1676, 2620, 0 }, { 1685, 2636, 5 }, { 1704, 2648, 0 }, { 1702, 2682, 5 },
		{ 1702, 2719, 10 }, { 1693, 2737, 10 }, { 1693, 2761, 10 }, { 1708, 2775, 5 },
		{ 1730, 2782, 5 }, { 1740, 2788, 5 }, { 1756, 2790, 0 }, { 1780, 2790, 0 },
		{ 1800, 2781, 0 }, { 1822, 2781, 0 }, { 1896, 2778, 0 }, { 1935, 2816, 0 },
		{ 1906, 2856, 20 }, { 1953, 2764, 9 }, { 1945, 2699, 20 }, { 1914, 2700, 18 },
		{ 1867, 2704, 10 }, { 1657, 2463, 5 }, { 1637, 2454, 2 }, { 1623, 2433, 0 },
		{ 1611, 2422, 0 }, { 1599, 2405, 5 }, { 1626, 2371, 0 }, { 1643, 2352, 0 },
		{ 1639, 2347, 0 }, { 1629, 2329, 0 }, { 1622, 2316, 0 }, { 1608, 2310, 5 },
		{ 1611, 2290, 0 }, { 1618, 2279, 5 }, { 1612, 2263, 5 }, { 1596, 2282, 0 },
		{ 1571, 2286, 0 }, { 1560, 2276, 0 }, { 1559, 2261, 0 }, { 1542, 2258, 0 },
		{ 1539, 2254, 0 }, { 1527, 2245, 5 }, { 1501, 2245, 0 }, { 1491, 2229, 5 },
		{ 1497, 2211, 0 }, { 1351, 1229, 0 }, { 1220, 937, 0 }, { 1138, 1054, 5 },
		{ 945, 1111, 0 }, { 852, 1172, 0 }, { 723, 1130, 0 }, { 1495, 801, 0 },
		{ 1818, 730, 0 }, { 1327, 1074, 0 }, { 1954, 747, 0 }, { 2060, 804, 0 },
		{ 1857, 866, -1 }, { 2193, 820, 0 }, { 2356, 965, 0 }, { 2500, 922, 0 },
		{ 2603, 986, 0 }, { 2298, 1194, 0 }, { 2017, 922, 0 }, { 1993, 1071, 0 },
		{ 1929, 1148, 0 }, { 2042, 239, 10 }, { 1300, 641, 16 }, { 1011, 1937, 0 },
		{ 903, 2229, 0 }, { 735, 2236, 0 }, { 2600, 649, 0 }, { 2583, 528, 15 },
		{ 2706, 522, 8 }, { 2852, 575, 0 }, { 2967, 638, 0 }, { 2786, 760, 0 },
		{ 2796, 836, 0 }, { 2285, 928, 0 }, { 2249, 846, 0 }, { 2162, 807, 0 },
		{ 2059, 877, 0 }, { 1895, 1203, 0 }, { 2019, 992, 0 }, { 1972, 752, 0 },
		{ 1889, 748, 0 }, { 1819, 730, 0 }, { 1749, 781, 0 }, { 1657, 800, 0 },
		{ 1567, 837, 0 }, { 1505, 844, 0 }, { 1448, 780, 0 }, { 1368, 823, 0 },
		{ 1442, 905, 0 }, { 1303, 859, 0 }, { 1238, 850, 0 }, { 1237, 928, 0 },
		{ 1164, 987, 5 }, { 1076, 1013, 0 }, { 1044, 1055, 0 }, { 1008, 1108, 0 },
		{ 933, 1149, 0 }, { 796, 1153, 0 }, { 663, 1142, 0 }, { 968, 1236, 0 },
		{ 1072, 1085, 5 }, { 967, 982, 0 }, { 1133, 1053, 5 }, { 1335, 887, 0 },
		{ 1508, 760, 16 }, { 1581, 860, 0 }, { 1645, 763, 0 }, { 1762, 814, 0 },
		{ 1811, 798, 0 }, { 1782, 705, 6 }, { 1959, 772, 0 }, { 1980, 733, 0 },
		{ 2002, 783, 0 },
	};
	// clang-format on
	int idx;

	idx = rand() % 137;
	CLocation_Set(out, (int16_t)table[idx].x, (int16_t)table[idx].y, (int16_t)table[idx].z);
}

/*
 * 0x004D7D1A - Noop_4D7D1A
 *
 * No-op immediately preceding Defcon_StrikeLightning in the binary; no
 * known callers.
 */
void
Noop_4D7D1A(CPlayer *player)
{
	USED(player);
}

/*
 * Custom - CNPC_SetupPath8Dir
 *
 * 8-direction variant of CNPC_SetupPath for GM .walk command.
 * Same BFS algorithm but tries all 8 directions (including diagonals)
 * instead of just 4 cardinals, and supports longer paths (up to 30
 * tiles instead of 10).
 */
void
CNPC_SetupPath8Dir(CNPC *npc, CLocation *loc, int maxSteps)
{
	CItem *mob = &npc->mobile.container.item;
	SearchNode nodes[512];
	PathNode resultNode;
	int count;
	int maxDist;
	int i, dir, k;
	int parentCost;
	int newX, newY;
	int pathSize;
	PathNode *pathArray;

	if (maxSteps >= 0x200)
		maxSteps = 0x1FF;

	Path_Discard(npc);

	nodes[0].x = mob->resourceEntity.entity.location.x;
	nodes[0].y = mob->resourceEntity.entity.location.y;
	nodes[0].z = mob->resourceEntity.entity.location.z;
	nodes[0].dir = (int16_t)(((CMobile *)mob)->direction & 7);
	nodes[0].parentIdx = 0;
	nodes[0].cost = 0;
	count = 1;

	maxDist = ChebyshevDistXY(
	                  (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)(int16_t)mob->resourceEntity.entity.location.x, (int)(int16_t)mob->resourceEntity.entity.location.y) +
	          2;

	if (maxDist > 30)
		return;

	for (i = 0; i < count; i++) {
		parentCost = nodes[i].cost;

		for (dir = 0; dir < 8; dir++) {
			if ((int)(int16_t)nodes[i].dir == g_OppositeDir[dir])
				continue;

			newX = (int)(int16_t)nodes[i].x + g_TerrainDirDX[dir];
			newY = (int)(int16_t)nodes[i].y + g_TerrainDirDY[dir];

			if (ChebyshevDistXY(newX, newY, (int)(int16_t)loc->x, (int)(int16_t)loc->y) >= maxDist)
				continue;

			for (k = 0; k < count; k++) {
				if ((int)(int16_t)nodes[k].x == newX && (int)(int16_t)nodes[k].y == newY)
					break;
			}
			if (k != count)
				continue;

			if (!Path_StepCheck(npc, (PathNode *)&nodes[i], dir, &resultNode))
				continue;

			nodes[count].x = resultNode.x;
			nodes[count].y = resultNode.y;
			nodes[count].z = resultNode.z;
			nodes[count].dir = resultNode.dir;
			nodes[count].parentIdx = i;
			nodes[count].cost = parentCost + 1;
			count++;

			if (ChebyshevDistXY((int)(int16_t)resultNode.x, (int)(int16_t)resultNode.y, (int)(int16_t)loc->x, (int)(int16_t)loc->y) <= 1)
				goto found_path_8dir;

			if (count >= maxSteps)
				return;
		}
	}

	return;

found_path_8dir:
	pathSize = parentCost + 2;
	pathArray = (PathNode *)malloc(pathSize * sizeof(PathNode));
	if (pathArray == NULL)
		return;

	Path_Install(npc, pathArray, (uint32_t)parentCost);

	k = 0;
	pathArray[k] = resultNode;
	k++;

	while (parentCost >= 0) {
		pathArray[k].x = nodes[i].x;
		pathArray[k].y = nodes[i].y;
		pathArray[k].z = nodes[i].z;
		pathArray[k].dir = nodes[i].dir;
		k++;
		i = nodes[i].parentIdx;
		parentCost--;
	}
}

/*
 * Custom - CNPC_PreyFleeScan
 *
 * Per Raph Koster: "everything was supposed to flee things that
 * might eat them." This is the prey-flee path that "didn't fit
 * cleanly into the basic state machine; 'thing that eats you' isn't
 * a resource type so there was nothing clean to hang it off of."
 * Wired here as a Custom helper called from CNPC_EcologyTick when
 * the NPC has a `foddertype` tag and IdleScan didn't already
 * transition to a higher-priority state.
 *
 * Walks the NPC range-query map for entities within `range` tiles,
 * tests each for a `predator` tag matching `fodderType`, and on first
 * match sets patrolTarget away from the threat (10x displacement
 * inversion, mirroring ScanForTargets' aversion-flee branch) and
 * sets aiState to 2 (IdleScan RUNAWAY numbering).
 */
static void
CNPC_PreyFleeScan(CItem *self, int range, int fodderType)
{
	CList resultList;
	CListNode *node;
	CItem *entity;
	int dx, dy;
	int predatorTag;

	CList_Constructor(&resultList);
	CEntityMap_RangeQueryToList(g_NPCMap, &resultList, (int)(int16_t)self->resourceEntity.entity.location.x, (int)(int16_t)self->resourceEntity.entity.location.y, range);

	for (node = resultList.head; node != NULL; node = node->next) {
		entity = CWorld_FindBySerial(g_World, node->value);
		if (entity == NULL || entity == self)
			continue;
		if (VT_IsHidden(entity))
			continue;
		predatorTag = CNPC_GetPredatorTag(entity);
		if (predatorTag == 0 || predatorTag != fodderType)
			continue;

		// Match - flee from this threat. Mirror ScanForTargets' aversion
		// branch: patrolTarget = self_loc + (self_loc - threat_loc) * 10,
		// i.e., 10x away from the threat.
		CLocation_SetLoc(&((CNPC *)self)->patrolTarget, &self->resourceEntity.entity.location);
		dx = (int16_t)entity->resourceEntity.entity.location.x - (int16_t)self->resourceEntity.entity.location.x;
		dy = (int16_t)entity->resourceEntity.entity.location.y - (int16_t)self->resourceEntity.entity.location.y;
		((CNPC *)self)->patrolTarget.x += (int16_t)(dx * 10 * -1);
		((CNPC *)self)->patrolTarget.y += (int16_t)(dy * 10 * -1);
		((CNPC *)self)->isWalking = 1;
		((CNPC *)self)->aiState = 2;
		break;
	}

	CList_Clear(&resultList);
}

/*
 * Custom - CNPC_DepositContainerAt
 *
 * Drops children of self's container at loc. With taggedOnly == 0
 * every movable child is dropped (scavenger deposit). With
 * taggedOnly != 0 only children carrying the "hoardloot" objvar are
 * dropped (hoarder deposit). CNPC_StashHoardPile tags only the loot a
 * desire pursuit acquired, so a creature's intrinsic loot - even when
 * it shares the container - is left untouched.
 *
 * Snapshot-then-drop mirrors CContainer_DecayPlace so iteration is
 * safe across VT_DROP_AT_FEET unlinking each item from the container.
 */
static void
CNPC_DepositContainerAt(CItem *self, CLocation *loc, int taggedOnly)
{
	CContainer *container;
	CVector vec;
	char typeFlag = 0;
	CItem *iter;
	uintptr_t *ptr;

	container = (CContainer *)self;
	if (container->contents == NULL)
		return;

	CVector_Constructor(&vec, &typeFlag);

	iter = container->contents;
	while (iter != NULL) {
		CVector_PushBack(&vec, (uintptr_t)iter);
		iter = iter->spatialNext;
	}

	ptr = (uintptr_t *)vec.begin;
	while (ptr != (uintptr_t *)vec.end) {
		CItem *child = (CItem *)*ptr;
		if (((int (*)(void *, void *))VT_FN(child, VT_IS_MOVEABLE))(child, self) && (!taggedOnly || CResourceEntity_HasTag(child, "hoardloot", 0))) {
			((void (*)(void *))VT_FN(child, VT_HIDE))(child);
			((void (*)(void *, CLocation *))VT_FN(child, VT_DROP_AT_FEET))(child, loc);
		}
		ptr++;
	}

	CVector_Destructor(&vec);
}

/*
 * Custom - CNPC_DepositScavengedAtShelter
 *
 * Drops every movable child of a scavenger NPC's container at
 * homeLoc. Completes Raph Koster's "they SHOULD be picking the item
 * up, taking it back to their shelter location, and leaving it
 * there" loop - CNPC_ScavengerPickup is the pickup half.
 */
static void
CNPC_DepositScavengedAtShelter(CItem *self)
{
	CNPC_DepositContainerAt(self, &((CNPC *)self)->homeLoc, 0);
}

/*
 * Custom - CNPC_CarryingHoard
 *
 * True when the NPC's pack holds at least one item tagged with the
 * "hoardloot" objvar - loot acquired by desire pursuit and not yet
 * delivered to the lair. CNPC_PurseDesiresHandler uses this to
 * recognise the walk-home sub-state.
 */
static int
CNPC_CarryingHoard(CNPC *npc)
{
	CItem *child;

	for (child = npc->mobile.container.contents; child != NULL; child = child->spatialNext) {
		if (CResourceEntity_HasTag(child, "hoardloot", 0))
			return 1;
	}
	return 0;
}

/*
 * Custom - CNPC_EcologyTick
 *
 * Wrapper around CNPC_IdleScan that translates between HandleStates
 * state constants and IdleScan's internal state numbering. The binary
 * never called IdleScan because the two functions use incompatible
 * values for npc->aiState:
 *
 *   HandleStates: NPC_STATE_IDLE=10, NPC_STATE_ATTACK_TARGET=9,
 *                 NPC_STATE_EAT_FOOD=5, NPC_STATE_RUNAWAY=7,
 *                 NPC_STATE_FOLLOWING=13
 *   IdleScan:     0 (reset), 2 (aversion flee), 3 (pursuit),
 *                 4 (following), 7 (eating countdown), 0xa (idle wander)
 *
 * The collision at value 7 is the worst: HandleStates RUNAWAY vs
 * IdleScan EATING. Without translation, an NPC told to eat would flee.
 *
 * This wrapper does bidirectional translation: HandleStates -> IdleScan
 * on entry (so IdleScan sees its own numbering), then IdleScan ->
 * HandleStates on exit (so the state machine that reads aiState next
 * sees the right constant). IdleScan's internal countdown logic for
 * state 7 (eating) and state 0xa (idle wander) works correctly because
 * IdleScan always sees its own numbering.
 *
 * States that neither machine uses (NPC_STATE_WANDER=11, SLEEP=12,
 * TALKING=8, LOITER=6, etc.) pass through untouched - IdleScan's
 * checks match none of them and the exit switch's default preserves
 * the value.
 */
static void
CNPC_EcologyTick(CItem *self)
{
	CNPC *npc = (CNPC *)self;

	// Entry translation: HandleStates state -> IdleScan state
	switch (npc->aiState) {
	case NPC_STATE_IDLE:
		npc->aiState = 0xa;
		break;
	case NPC_STATE_ATTACK_TARGET:
		npc->aiState = 3;
		break;
	case NPC_STATE_FOLLOWING:
		npc->aiState = 4;
		break;
	case NPC_STATE_EAT_FOOD:
		npc->aiState = 7;
		break;
	case NPC_STATE_RUNAWAY:
		npc->aiState = 2;
		break;
	default:
		break;
	}

	CNPC_IdleScan(self);

	// Custom deposit-at-shelter: per Raph Koster, "they SHOULD be
	// picking the item up, taking it back to their shelter location,
	// and leaving it there." CNPC_ScavengerPickup (called from
	// IdleScan) and CNPC_PurseDesiresHandler are the pickup halves;
	// this is the passive deposit. It fires when an NPC idle-wanders
	// at homeLoc still holding loot - a scavenger drops its whole
	// haul, a hoarder whose walk-home was interrupted (combat, flee,
	// stall) drops its tagged hoard.
	if (npc->aiState == 0xa && !CLocation_IsInvalid(&npc->homeLoc)) {
		int dx = (int)(int16_t)npc->homeLoc.x - (int)(int16_t)self->resourceEntity.entity.location.x;
		int dy = (int)(int16_t)npc->homeLoc.y - (int)(int16_t)self->resourceEntity.entity.location.y;
		if (abs(dx) <= 1 && abs(dy) <= 1) {
			if (CNPC_GetScavengerTag(self) > 0)
				CNPC_DepositScavengedAtShelter(self);
			else if (CNPC_CarryingHoard(npc))
				CNPC_DepositContainerAt(self, &npc->homeLoc, 1);
		}
	}

	// Custom prey-flee scan: per Raph Koster, "everything was supposed
	// to flee things that might eat them," but the IdleScan gate only
	// fires ScanForTargets for predator/packing/flying NPCs - so pure
	// prey with `aversionPower` set never actually scans for predators.
	// Wire foddertype-driven prey-flee here. Runs only if IdleScan
	// didn't already transition to a higher-priority state (pursuit/
	// flee/eating/following).
	if (npc->aiState == 0xa && CResourceEntity_HasTag(self, "foddertype", 0)) {
		int fodderType = 0;
		CResourceEntity_GetTagInt(self, "foddertype", &fodderType);
		if (fodderType > 0)
			CNPC_PreyFleeScan(self, 20, fodderType);
	}

	// Exit translation: IdleScan state -> HandleStates state
	switch (npc->aiState) {
	case 0:
		// IdleScan "reset" - patrolTarget set, isWalking=1
		npc->aiState = NPC_STATE_IDLE;
		break;
	case 1:
		// IdleScan pursuit trigger (1/10 chance, line 571)
		npc->aiState = NPC_STATE_SEEK_SHELTER;
		break;
	case 2:
		// IdleScan aversion flee
		npc->aiState = NPC_STATE_RUNAWAY;
		break;
	case 3:
		// IdleScan predator pursuit (after CombatInitiate)
		npc->aiState = NPC_STATE_ATTACK_TARGET;
		break;
	case 4:
		// IdleScan following (early return)
		npc->aiState = NPC_STATE_FOLLOWING;
		break;
	case 7:
		// IdleScan eating countdown
		npc->aiState = NPC_STATE_EAT_FOOD;
		break;
	case 0xa:
		// IdleScan idle wander
		npc->aiState = NPC_STATE_IDLE;
		break;
	default:
		break;
	}
}

/*
 * Custom - CNPC_SeekShelterHandler
 *
 * FEAT_ECOLOGY implementation of NPC_STATE_SEEK_SHELTER. The binary
 * stub transitions straight to IDLE because Koster disabled the scan
 * during UO beta ("cost of doing radial searches followed by
 * pathfinding"). This handler finishes the design:
 *
 *   1. If homeLoc is valid, walk back to it via PurseShelterHandler's
 *      no-target fallback. PurseShelterHandler reads resourceAITarget
 *      == 0 as "loiter here", so arriving at homeLoc naturally settles
 *      the NPC around its remembered shelter.
 *   2. Otherwise walk the NPC's own type-1 resource nodes (shelter
 *      preferences) and scan CItems in every MapBlock covering a
 *      24-tile box around self, testing each for a matching type-3
 *      production node. Mobiles are skipped; shelter targets are
 *      static/dynamic items (tree eggs, cave eggs, static shelter).
 *   3. On hit: populate resourceTargetSerial, resourceType,
 *      resourceRate, resourceAITarget and patrolTarget, set isWalking,
 *      and transition to PURSE_SHELTER. AIMoveTick walks the NPC to
 *      patrolTarget over subsequent ticks; the existing
 *      PurseShelterHandler consumes the node on arrival and writes
 *      homeLoc = patrolTarget.
 *   4. On miss: cool down via scanTimer so we do not re-scan every
 *      tick; transition to IDLE.
 *
 * Koster's perf guidance: cap candidates at 10 (matches
 * ScanForTargets), reuse the spatial grid, and throttle re-scans.
 */
static void
CNPC_SeekShelterHandler(CNPC *npc)
{
	CItem *self = (CItem *)npc;
	CResourceNode *pref;
	CResourceNode *node;
	CItem *ent;
	CItem *candidates[10];
	uint8_t candidateIds[10];
	uint8_t candidateRates[10];
	int candidateCount;
	int hasPref;
	int selfX, selfY;
	int x, y;
	int blockIndex;
	int chosen;

	if (!CLocation_IsInvalid(&npc->homeLoc)) {
		int dx = (int)(int16_t)npc->homeLoc.x - (int)(int16_t)self->resourceEntity.entity.location.x;
		int dy = (int)(int16_t)npc->homeLoc.y - (int)(int16_t)self->resourceEntity.entity.location.y;
		if (abs(dx) > 1 || abs(dy) > 1) {
			CLocation_SetLoc(&npc->patrolTarget, &npc->homeLoc);
			npc->resourceTargetSerial = 0;
			npc->resourceAITarget = 0;
			npc->isWalking = 1;
			Entity_ExecuteEvent(&self->resourceEntity.entity, 0x0D, (uintptr_t)0); // foundshelter (home-return: target=0 sentinel for cached home)
			CNPC_SetState(npc, NPC_STATE_PURSE_SHELTER);
			return;
		}
	}

	hasPref = 0;
	for (pref = self->resourceEntity.firstChild; pref != NULL; pref = pref->next) {
		if (pref->type == 1 && pref->id != 0) {
			hasPref = 1;
			break;
		}
	}
	if (!hasPref) {
		Entity_ExecuteEvent(&self->resourceEntity.entity, 0x0A); // failshelter
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	if (npc->scanTimer != 0) {
		npc->scanTimer--;
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	candidateCount = 0;
	selfX = (int)(int16_t)self->resourceEntity.entity.location.x;
	selfY = (int)(int16_t)self->resourceEntity.entity.location.y;

	for (x = selfX - 24; x <= selfX + 24 && candidateCount < 10; x += 8) {
		for (y = selfY - 24; y <= selfY + 24 && candidateCount < 10; y += 8) {
			blockIndex = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
			if (blockIndex < 0)
				continue;
			ent = g_SpatialGrid.cells[blockIndex].itemHead;
			while (ent != NULL && candidateCount < 10) {
				if (ent == self || ent->resourceEntity.entity.removedFromWorld || VT_IsMobile(ent)) {
					ent = ent->spatialNext;
					continue;
				}
				for (pref = self->resourceEntity.firstChild; pref != NULL && candidateCount < 10; pref = pref->next) {
					if (pref->type != 1 || pref->id == 0)
						continue;
					node = CResourceEntity_FindNode(ent, pref->id, 3);
					if (node == NULL)
						continue;
					if (node->value3 < 1)
						continue;
					candidates[candidateCount] = ent;
					candidateIds[candidateCount] = (uint8_t)pref->id;
					candidateRates[candidateCount] = (uint8_t)(pref->value1 > 0 ? pref->value1 : 1);
					candidateCount++;
					break;
				}
				ent = ent->spatialNext;
			}
		}
	}

	if (candidateCount == 0) {
		npc->scanTimer = 8;
		Entity_ExecuteEvent(&self->resourceEntity.entity, 0x0A); // failshelter
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	chosen = GetRandomRange(1, candidateCount) - 1;
	npc->resourceTargetSerial = candidates[chosen]->serial;
	npc->resourceType = candidateIds[chosen];
	npc->resourceRate = candidateRates[chosen];
	npc->resourceAITarget = 1;
	CLocation_SetLoc(&npc->patrolTarget, &candidates[chosen]->resourceEntity.entity.location);
	npc->isWalking = 1;
	Entity_ExecuteEvent(&self->resourceEntity.entity, 0x0D, (uintptr_t)npc->resourceTargetSerial); // foundshelter
	CNPC_SetState(npc, NPC_STATE_PURSE_SHELTER);
}

/* Custom - player-desire pursuit/cooldown windows, in AI ticks. Shared
 * by every NPC that targets a player via the desire mechanism (thief
 * stealing gold, dragon hunting a jewel-carrier, beggar following). */
#define PLAYER_DESIRE_PURSUE_TIMEOUT 120
#define PLAYER_DESIRE_COOLDOWN       90

/*
 * Custom - CNPC_TickSlotStore / CNPC_TickSlotLoad
 *
 * Pack a 32-bit AI tick stamp (npc->tickCount) into an otherwise-dead
 * CLocation save field (desireLoc / lastDesireLoc). Lets the
 * player-desire pursuit timeout and re-engage cooldown work with no new
 * struct field or save-format change. CLocation.x/.y are uint16_t; the
 * dead fields init to (0xFFFF, 0xFFFF), which loads as 0xFFFFFFFF - the
 * "never" sentinel.
 */
static void
CNPC_TickSlotStore(CLocation *slot, uint32_t tick)
{
	slot->x = (uint16_t)tick;
	slot->y = (uint16_t)(tick >> 16);
}

static uint32_t
CNPC_TickSlotLoad(const CLocation *slot)
{
	return (uint32_t)slot->x | ((uint32_t)slot->y << 16);
}

/*
 * Custom - CNPC_PlayerDesireCoolingDown
 *
 * True while this NPC is within PLAYER_DESIRE_COOLDOWN AI ticks of its
 * last player-desire engagement (stamped in lastDesireLoc when
 * CNPC_PurseDesiresPlayer reaches adjacency and fires acquiredesire).
 * Keeps a thief from immediately re-pickpocketing a nearby player, and
 * a dragon from continuously re-engaging desire pursuit after a kill /
 * attack switch - both share the same gate.
 */
static int
CNPC_PlayerDesireCoolingDown(CNPC *npc)
{
	uint32_t last = CNPC_TickSlotLoad(&npc->lastDesireLoc);

	if (last == 0xFFFFFFFFu)
		return 0;
	return (npc->tickCount - last) < PLAYER_DESIRE_COOLDOWN;
}

/*
 * Custom - DesireBodyMatch / DesireBodyMint (FEAT_ECOLOGY)
 *
 * Body-type ranges that map to a desire resource. The binary has no
 * desire system; these ranges plus the live-body fallback are how
 * CNPC_SeekDesiresHandler and CNPC_PurseDesiresHandler classify items
 * that don't carry an explicit <resource desire> node (e.g. gold
 * piles dropped by a player corpse, jewel stacks created at runtime).
 *
 * Raph Koster: the binary's per-target nodes and the chunk egg
 * are deliberately independent pools; the egg is regenerated
 * by the region bank and read by NPC scans, while per-target nodes are
 * drained by direct harvest. Items minted at runtime without explicit
 * nodes (loose gold piles, jewel stacks) would otherwise be invisible
 * to the desire scan; DesireBodyMatch provides the body-type fallback
 * that catches them.
 *
 * DesireBodyMint resolves the inverse mapping used by PurseDesires to
 * create a representative pile at the lair after draining a chunk-egg
 * node.
 *
 * Cached typeIds are resolved on first use via
 * CResourceTypeManager_FindByName; missing names leave the slot at 0
 * (the desire silently no-ops, never matches).
 */
struct DesireBodyRange {
	uint16_t first;
	uint16_t last;
	int *cachedTypeId;
	const char *name;
};

static int s_DesireGoldId, s_DesireJewelsId, s_DesireMetalId, s_DesireMagicId;
static int s_DesireBodiesResolved;

static const struct DesireBodyRange g_DesireBodies[] = {
	{ 0x0EED, 0x0EED, &s_DesireGoldId, "gold" },
	{ 0x0F10, 0x0F2D, &s_DesireJewelsId, "jewels" },
	{ 0x19B7, 0x19BA, &s_DesireMetalId, "metal" },
	{ 0x1F14, 0x1F17, &s_DesireMagicId, "magic" },
};

static void
DesireBodiesResolve(void)
{
	size_t i;
	CResourceType *rt;

	if (s_DesireBodiesResolved)
		return;
	for (i = 0; i < sizeof(g_DesireBodies) / sizeof(g_DesireBodies[0]); i++) {
		rt = CResourceTypeManager_FindByName(g_DesireBodies[i].name);
		*g_DesireBodies[i].cachedTypeId = (rt != NULL) ? rt->typeId : 0;
	}
	s_DesireBodiesResolved = 1;
}

/*
 * Returns 1 when bodyId is in a range whose desire typeId matches
 * desireTypeId. Used as a fallback when the scanned entity carries
 * no explicit type-3 desire node.
 */
static int
DesireBodyMatch(uint16_t bodyId, int desireTypeId)
{
	size_t i;

	if (desireTypeId <= 0)
		return 0;
	DesireBodiesResolve();
	for (i = 0; i < sizeof(g_DesireBodies) / sizeof(g_DesireBodies[0]); i++) {
		if (*g_DesireBodies[i].cachedTypeId != desireTypeId)
			continue;
		if (bodyId < g_DesireBodies[i].first || bodyId > g_DesireBodies[i].last)
			continue;
		return 1;
	}
	return 0;
}

/*
 * Returns the canonical bodyId for minting a representative pile of
 * desireTypeId, or 0 if the desire has no physical form. Gold is the
 * `0xEED` pile (hardcoded by PurseDesires anyway); other desires
 * resolve via the first range in g_DesireBodies whose typeId matches.
 */
static uint16_t
DesireBodyMint(int desireTypeId)
{
	size_t i;

	if (desireTypeId <= 0)
		return 0;
	DesireBodiesResolve();
	for (i = 0; i < sizeof(g_DesireBodies) / sizeof(g_DesireBodies[0]); i++) {
		if (*g_DesireBodies[i].cachedTypeId == desireTypeId)
			return g_DesireBodies[i].first;
	}
	return 0;
}

/*
 * Custom - CNPC_PlayerCarriesDesireBody
 *
 * True when `player` is carrying at least one item whose body matches
 * desire typeId. Raph Koster: "seeking desires is supposed to work
 * even for finding contents of containers -- such as players carrying
 * gold." This is the predicate the player-desire scan uses to decide
 * whether the player is a candidate for an NPC's positive desire.
 *
 *   - Gold: the binary's existing CMobile_GetTotalQuantityOfType walks
 *     every equipment slot and every sub-container looking for the
 *     0xEED body. This is the recursive accountant the pickpocket gate
 *     has always used; keeping it preserves the "gold tucked in a bag
 *     in a bag" case.
 *   - Non-gold (jewels / metal / magic): a bounded one-level walk of
 *     the main backpack (equipment[21]) matched against
 *     DesireBodyMatch. Matches Raph Koster's "such as gold" framing -
 *     the general principle is desire-driven follow into containers;
 *     the gold case happens to use a recursive accountant because the
 *     binary already had one for it. One level is enough to cover the
 *     visible case (loose items in pack) without unbounded scan costs.
 */
static int
CNPC_PlayerCarriesDesireBody(CItem *player, int desireTypeId)
{
	CItem *pack;
	CItem *child;

	if (desireTypeId <= 0)
		return 0;
	DesireBodiesResolve();
	if (desireTypeId == s_DesireGoldId)
		return CMobile_GetTotalQuantityOfType((CMobile *)player, 0xEED) > 0;
	pack = ((CMobile *)player)->equipment[21];
	if (pack == NULL)
		return 0;
	for (child = ((CContainer *)pack)->contents; child != NULL; child = child->spatialNext) {
		if (DesireBodyMatch((uint16_t)CEntity_GetBodyType(child), desireTypeId))
			return 1;
	}
	return 0;
}

/*
 * Custom - CNPC_IsPlayerDesireMark
 *
 * True when `ent` is a valid player-desire mark for the NPC's `pref`:
 * a live, visible player carrying something that matches the positive
 * desire body, and the NPC is not on player-desire cooldown. The
 * generalized successor of the old CNPC_IsPickpocketMark gate; gold is
 * no longer special-cased at this layer, the body-presence check in
 * CNPC_PlayerCarriesDesireBody handles the gold vs non-gold split.
 */
static int
CNPC_IsPlayerDesireMark(CNPC *npc, CItem *ent, CResourceNode *pref)
{
	if (!VT_IsPlayer(ent))
		return 0;
	if (pref->value2 <= 0 || pref->id == 0)
		return 0;
	if (VT_IsHidden(ent) || VT_IsDead(ent))
		return 0;
	if (!CNPC_PlayerCarriesDesireBody(ent, (int)pref->id))
		return 0;
	if (CNPC_PlayerDesireCoolingDown(npc))
		return 0;
	return 1;
}

/*
 * Custom - CNPC_SeekDesiresHandler
 *
 * FEAT_ECOLOGY implementation of NPC_STATE_SEEK_DESIRES. Mirrors the
 * shelter seeker but scans against type-2 (desire) preferences on
 * the NPC's own resource-node chain. Two semantic paths:
 *
 *   - Aversion (pref->value2 < 0): Koster's "wolf avoids
 *     carnivoremeat" rule. A nearby type-3 node matching an
 *     aversion preference becomes an escape target: actionTarget
 *     is set to the candidate's serial and the NPC transitions to
 *     RUNAWAY so CNPC_RunawayTick walks it away.
 *   - Desire (pref->value2 > 0): a nearby type-3 match becomes the
 *     pursuit target. Populate resourceTargetSerial, resourceType,
 *     resourceRate, resourceAITarget, patrolTarget, set isWalking,
 *     and transition to PURSE_DESIRES.
 *
 * Aversion vs desire: the scan records the closest hit of each
 * class, then the closer wins. Aversions only short-circuit into
 * RUNAWAY when the threat is nearer than any desire - otherwise a
 * single distant aversion (e.g. a water-tile chunk egg several
 * blocks away) would block every hoard attempt on blocks that also
 * contain gold piles. Candidates are walked from every MapBlock
 * covering a 24-tile half-box (matches one WANDER leg's drift). On
 * miss the NPC returns to IDLE; the natural rate limit is the 50%
 * IDLE roll in CNPC_HandleStates.
 *
 * scanTimer is NOT used as a cooldown here. CNPC_IdleScan runs
 * every IDLE tick before the state roll and unconditionally resets
 * scanTimer to 1 or 2 (npc.c:581-586), so a scanTimer != 0 guard
 * would block every desire scan the dragon ever attempts.
 */
static void
CNPC_SeekDesiresHandler(CNPC *npc)
{
	CItem *self = (CItem *)npc;
	CResourceNode *pref;
	CResourceNode *node;
	CItem *ent;
	CItem *desireCandidates[10];
	uint8_t desireIds[10];
	uint8_t desireRates[10];
	uint8_t desireIsMobile[10];
	int desireDist[10];
	int desireCount;
	CItem *aversionTarget;
	int aversionDist;
	int hasPref;
	int selfX, selfY;
	int x, y;
	int blockIndex;
	int chosen;
	int i;
	int nearestDesire;

	hasPref = 0;
	for (pref = self->resourceEntity.firstChild; pref != NULL; pref = pref->next) {
		if (pref->type == 2 && pref->id != 0) {
			hasPref = 1;
			break;
		}
	}
	if (!hasPref) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	// A hoard-carrying NPC delivers its load before foraging again:
	// route to PURSE_DESIRES, whose carry check walks it home. Without
	// this a dense desire field could keep it scanning and never
	// converging on the lair.
	if (CNPC_CarryingHoard(npc)) {
		CNPC_SetState(npc, NPC_STATE_PURSE_DESIRES);
		return;
	}

	desireCount = 0;
	aversionTarget = NULL;
	aversionDist = 0;
	selfX = (int)(int16_t)self->resourceEntity.entity.location.x;
	selfY = (int)(int16_t)self->resourceEntity.entity.location.y;

	for (x = selfX - 24; x <= selfX + 24 && desireCount < 10; x += 8) {
		for (y = selfY - 24; y <= selfY + 24 && desireCount < 10; y += 8) {
			blockIndex = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
			if (blockIndex < 0)
				continue;
			ent = g_SpatialGrid.cells[blockIndex].itemHead;
			while (ent != NULL) {
				if (ent == self || ent->resourceEntity.entity.removedFromWorld) {
					ent = ent->spatialNext;
					continue;
				}
				// Already-hoarded loot keeps a "hoardloot" tag - skip
				// it so a hoarder never re-pursues a pile it (or
				// another hoarder) has carried home and deposited.
				// Deliberate: without it, hoarders would endlessly
				// shuttle the same piles between lairs. The tag rides
				// the pile onto the ground and decays with it, so it
				// is self-cleaning.
				if (CResourceEntity_HasTag(ent, "hoardloot", 0)) {
					ent = ent->spatialNext;
					continue;
				}
				uint16_t entBody = (uint16_t)CEntity_GetBodyType(ent);
				if (VT_IsPlayer(ent)) {
					// Custom: player-desire candidate gate. Raph Koster:
					// "Seeking desires is supposed to work even for
					// finding contents of containers - such as players
					// carrying gold." The matching alpha-era anecdote
					// from UO programmer Brian T Crowder (Quora) is
					// "city guards love pastries", where guards would
					// follow players carrying donuts. For each positive
					// desire on the NPC, check whether the player
					// carries a matching body via
					// CNPC_PlayerCarriesDesireBody - gold recurses every
					// equipment slot and sub-container, non-gold walks
					// the main backpack one level. A match adds the
					// player as a mobile desire candidate; the
					// follow-and-engage step runs in
					// CNPC_PurseDesiresPlayer. Loop breaks on the first
					// matching pref so a single player is added at most
					// once per scan.
					for (pref = self->resourceEntity.firstChild; pref != NULL; pref = pref->next) {
						if (pref->type != 2)
							continue;
						if (!CNPC_IsPlayerDesireMark(npc, ent, pref))
							continue;
						if (desireCount < 10) {
							int pdx = (int)(int16_t)ent->resourceEntity.entity.location.x - selfX;
							int pdy = (int)(int16_t)ent->resourceEntity.entity.location.y - selfY;
							desireCandidates[desireCount] = ent;
							desireIds[desireCount] = (uint8_t)pref->id;
							desireRates[desireCount] = (uint8_t)(pref->value1 > 0 ? pref->value1 : 1);
							desireIsMobile[desireCount] = 1;
							desireDist[desireCount] = (pdx < 0 ? -pdx : pdx) + (pdy < 0 ? -pdy : pdy);
							desireCount++;
						}
						break;
					}
					ent = ent->spatialNext;
					continue;
				}
				for (pref = self->resourceEntity.firstChild; pref != NULL; pref = pref->next) {
					if (pref->type != 2 || pref->id == 0)
						continue;
					node = CResourceEntity_FindNode(ent, pref->id, 3);
					if (node == NULL || node->value3 < 1) {
						// Fallback: physical desire items (gold piles,
						// jewel stacks, metal/magic items) classified
						// by body-type range. Players handled by the
						// player-desire branch above, so ent is never a
						// player here.
						if (!DesireBodyMatch(entBody, (int)pref->id))
							continue;
					}

					int entX = (int)(int16_t)ent->resourceEntity.entity.location.x;
					int entY = (int)(int16_t)ent->resourceEntity.entity.location.y;
					int dx = entX - selfX;
					int dy = entY - selfY;
					int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);

					if (pref->value2 < 0) {
						if (aversionTarget == NULL || dist < aversionDist) {
							aversionTarget = ent;
							aversionDist = dist;
						}
						break;
					}

					if (desireCount < 10) {
						desireCandidates[desireCount] = ent;
						desireIds[desireCount] = (uint8_t)pref->id;
						desireRates[desireCount] = (uint8_t)(pref->value1 > 0 ? pref->value1 : 1);
						desireIsMobile[desireCount] = 0;
						desireDist[desireCount] = dist;
						desireCount++;
					}
					break;
				}
				if (desireCount >= 10)
					break;
				ent = ent->spatialNext;
			}
		}
	}

	nearestDesire = -1;
	for (i = 0; i < desireCount; i++) {
		if (nearestDesire < 0 || desireDist[i] < desireDist[nearestDesire])
			nearestDesire = i;
	}

	if (aversionTarget != NULL && (nearestDesire < 0 || aversionDist <= desireDist[nearestDesire])) {
		npc->actionTarget = aversionTarget->serial;
		npc->isWalking = 1;
		CNPC_SetState(npc, NPC_STATE_RUNAWAY);
		return;
	}

	if (nearestDesire < 0) {
		Entity_ExecuteEvent(&self->resourceEntity.entity, 0x09); // faildesire
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	chosen = nearestDesire;
	npc->resourceTargetSerial = desireCandidates[chosen]->serial;
	npc->resourceType = desireIds[chosen];
	npc->resourceRate = desireRates[chosen];
	CLocation_SetLoc(&npc->patrolTarget, &desireCandidates[chosen]->resourceEntity.entity.location);
	npc->ltype = NPC_STATE_PURSE_DESIRES;
	npc->isWalking = 1;
	if (desireIsMobile[chosen]) {
		// Custom: player-desire pursuit. stateInfo2 also routes to
		// PURSE_DESIRES - a moving player stalls the walker most
		// ticks, and the stall must re-enter the pursuit so
		// CNPC_PurseDesiresPlayer can re-target. desireLoc stamps the
		// pursuit-start tick for the timeout.
		npc->resourceAITarget = NPC_RESTGT_MOBILE;
		npc->stateInfo2 = NPC_STATE_PURSE_DESIRES;
		CNPC_TickSlotStore(&npc->desireLoc, npc->tickCount);
	} else {
		// Walker arrival (AITickStep) re-dispatches via SetState(ltype).
		// Without pointing ltype at PURSE_DESIRES, the dragon walks to the
		// pile, then drops into whatever state StartWander had cached (IDLE
		// or WANDER), and PurseDesiresHandler never runs. stateInfo2 is the
		// fallback when the walk stalls out of range; route that to IDLE so
		// a stuck dragon doesn't cycle forever against an unreachable pile.
		npc->resourceAITarget = NPC_RESTGT_ITEM;
		npc->stateInfo2 = NPC_STATE_IDLE;
	}
	Entity_ExecuteEvent(&self->resourceEntity.entity, 0x0C, (uintptr_t)npc->resourceTargetSerial); // founddesire
	CNPC_SetState(npc, NPC_STATE_PURSE_DESIRES);
}

/*
 * Custom - CNPC_PurseDesiresPlayer
 *
 * FEAT_ECOLOGY player-desire pursuit - the mobile-target arm of
 * NPC_STATE_PURSE_DESIRES. CNPC_SeekDesiresHandler picks a player
 * who carries something matching one of the NPC's positive desires
 * (gold for a thief, jewels for a dragon, etc.); this handler walks
 * the (moving) player down and, on reaching them, fires acquiredesire.
 * Unlike the item-consume path it never drains a resource node or
 * drops a hoard pile - the player is followed, not harvested.
 *
 * The visible behavior at adjacency comes from the NPC's other
 * triggers, not from this handler. Examples:
 *   - thief.m's `acquiredesire` runs takeMoney/barkTo/runAway/
 *     setCriminal -> the thief steals 5% of the player's gold.
 *   - dragonai.m's `enterrange(8)` fires `attack(this, target)` as
 *     the dragon closes within 8 tiles -> the dragon hunts the
 *     player carrying the desired body.
 *   - cityguard.m's `enterrange` only attacks criminals -> guards
 *     follow an innocent carrier (the literal "city guards love
 *     pastries" pattern Brian T Crowder cited on Quora).
 *   - NPCs with no aggressive trigger (beggar, magpie) just follow,
 *     bark at adjacency, and idle off.
 *
 * Flow:
 *   1. Abort to IDLE if the target is gone, dead, no longer a player,
 *      no longer carries the desired body, or pursuit timed out.
 *   2. Re-snapshot patrolTarget onto the player's current tile each
 *      tick so the walker chases a moving player.
 *   3. On adjacency, fire acquiredesire (0x34) with the player
 *      serial. The cooldown stamp lands before the fire so a
 *      script-side no-op (Thieves' Guild bypass; dragon's silent
 *      desire) still consumes the cooldown.
 */
static void
CNPC_PurseDesiresPlayer(CNPC *npc, CItem *target)
{
	CItem *self = (CItem *)npc;
	uint32_t victimSerial;
	int dist;

	// Abort: target logged out, despawned, died, the serial was
	// recycled onto a non-player, or the player no longer carries
	// the desired body (gold spent, jewel dropped, etc.).
	if (target == NULL || target->resourceEntity.entity.removedFromWorld != 0 || !VT_IsPlayer(target) || VT_IsDead(target) ||
	        !CNPC_PlayerCarriesDesireBody(target, (int)npc->resourceType)) {
		npc->resourceTargetSerial = 0;
		npc->resourceAITarget = NPC_RESTGT_NONE;
		npc->isWalking = 0;
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	// Pursuit timeout: bound an endless chase of a fleeing player.
	if (npc->tickCount - CNPC_TickSlotLoad(&npc->desireLoc) > PLAYER_DESIRE_PURSUE_TIMEOUT) {
		npc->resourceTargetSerial = 0;
		npc->resourceAITarget = NPC_RESTGT_NONE;
		npc->isWalking = 0;
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	dist = Location_WrappedChebyshevDistance(&target->resourceEntity.entity.location, &self->resourceEntity.entity.location);

	if (dist > 1) {
		// Not adjacent: re-target the player's current tile and keep
		// chasing. ltype and stateInfo2 both route back to PURSE so
		// walker arrival and walker stall both re-enter this handler.
		CLocation_SetLoc(&npc->patrolTarget, &target->resourceEntity.entity.location);
		npc->ltype = NPC_STATE_PURSE_DESIRES;
		npc->stateInfo2 = NPC_STATE_PURSE_DESIRES;
		npc->isWalking = 1;
		return;
	}

	// Adjacent: fire acquiredesire. Skip the node-drain/hoard/deposit
	// path - the player isn't a harvestable item.
	victimSerial = npc->resourceTargetSerial;
	CNPC_TickSlotStore(&npc->lastDesireLoc, npc->tickCount);
	npc->resourceTargetSerial = 0;
	npc->resourceAITarget = NPC_RESTGT_NONE;
	npc->isWalking = 0;

	Entity_ExecuteEvent(&self->resourceEntity.entity, 0x34, (uintptr_t)victimSerial); // acquiredesire
	if (npc != g_currentNPC)
		return;
	CNPC_ShouldProcess(npc);

	// Respect any script-driven state change (thief.m's runAway sets
	// RUNAWAY; dragonai.m's enterrange may have set ATTACK_TARGET
	// earlier in the walk). Only loop back to SEEK_DESIRES if the
	// script left the NPC in PURSE_DESIRES (no-op acquire).
	if (npc->aiState == NPC_STATE_PURSE_DESIRES)
		CNPC_SetState(npc, NPC_STATE_SEEK_DESIRES);
}

/*
 * Custom - CNPC_StashHoardPile
 *
 * Picks a desire pile up into the NPC's own pack and tags the
 * acquired loot with the "hoardloot" objvar, so the deposit step can
 * tell it from the creature's intrinsic loot. Mirrors
 * CNPC_ScavengerPickup's hide-then-VT_ADD_TO_CONTAINER pattern.
 *
 * VT_ADD_TO_CONTAINER may merge `pile` into an existing stack and
 * free it, so the tag cannot be applied to the pile pointer after
 * the add. Instead the same-body children present before the add are
 * snapshotted; only a same-body child absent from that snapshot - the
 * freshly-added pile - is tagged. When `pile` instead stacks onto
 * same-bodied loot the creature already carried (e.g. an orc's
 * intrinsic SELFCONTAINED gold pile), no new child appears and
 * nothing is tagged, so the deposit step never relocates that
 * intrinsic loot. `body` is a pre-captured bodyType, never the pile
 * pointer.
 */
static void
CNPC_StashHoardPile(CNPC *npc, CItem *pile, uint16_t body)
{
	CLocation loc;
	CVector preExisting;
	char typeFlag = 0;
	CItem *child;
	uintptr_t *ptr;
	int wasPresent;

	CVector_Constructor(&preExisting, &typeFlag);
	for (child = npc->mobile.container.contents; child != NULL; child = child->spatialNext) {
		if ((uint16_t)(CEntity_GetBodyType(child) & 0xFFFF) == body)
			CVector_PushBack(&preExisting, (uintptr_t)child);
	}

	((void (*)(void *))VT_FN(pile, VT_HIDE))(pile);
	CLocation_Init(&loc);
	CLocation_Set(&loc, -1, -1, -1);
	((void (*)(void *, void *, void *))VT_FN(pile, VT_ADD_TO_CONTAINER))(pile, npc, &loc);

	for (child = npc->mobile.container.contents; child != NULL; child = child->spatialNext) {
		if ((uint16_t)(CEntity_GetBodyType(child) & 0xFFFF) != body)
			continue;
		wasPresent = 0;
		for (ptr = (uintptr_t *)preExisting.begin; ptr != (uintptr_t *)preExisting.end; ptr++) {
			if ((CItem *)*ptr == child) {
				wasPresent = 1;
				break;
			}
		}
		if (!wasPresent)
			CEntity_SetObjVar(child, "hoardloot", 0, (uintptr_t)1);
	}

	CVector_Destructor(&preExisting);
}

/*
 * Custom - CNPC_HoardReturnHome
 *
 * The walk-home / deposit step of the desire hoard loop, run after a
 * desire pickup and on every walk-home re-dispatch:
 *
 *   - homeLoc invalid: anchor it to the current tile.
 *   - within 1 tile of homeLoc: drop the tagged hoard at the lair
 *     and re-enter SEEK_DESIRES.
 *   - otherwise: walk to homeLoc. ltype = PURSE_DESIRES routes
 *     walker arrival back here via the carry-state check at the top
 *     of CNPC_PurseDesiresHandler; stateInfo2 = IDLE so an
 *     unreachable lair drops to IDLE - where the CNPC_EcologyTick
 *     safety net deposits - instead of tight-looping.
 */
static void
CNPC_HoardReturnHome(CNPC *npc)
{
	CItem *self = (CItem *)npc;
	int dx, dy;

	if (CLocation_IsInvalid(&npc->homeLoc))
		CLocation_SetLoc(&npc->homeLoc, &self->resourceEntity.entity.location);

	dx = (int)(int16_t)npc->homeLoc.x - (int)(int16_t)self->resourceEntity.entity.location.x;
	dy = (int)(int16_t)npc->homeLoc.y - (int)(int16_t)self->resourceEntity.entity.location.y;

	npc->resourceTargetSerial = 0;
	npc->resourceAITarget = NPC_RESTGT_NONE;

	if (abs(dx) <= 1 && abs(dy) <= 1) {
		CNPC_DepositContainerAt(self, &npc->homeLoc, 1);
		npc->isWalking = 0;
		CNPC_SetState(npc, NPC_STATE_SEEK_DESIRES);
		return;
	}

	CLocation_SetLoc(&npc->patrolTarget, &npc->homeLoc);
	npc->isWalking = 1;
	npc->ltype = NPC_STATE_PURSE_DESIRES;
	npc->stateInfo2 = NPC_STATE_IDLE;
	CNPC_SetState(npc, NPC_STATE_PURSE_DESIRES);
}

/*
 * Custom - CNPC_PurseDesiresHandler
 *
 * FEAT_ECOLOGY implementation of NPC_STATE_PURSE_DESIRES: the
 * pick-up -> carry -> walk-home -> drop loop Raph Koster described
 * ("picking the item up, taking it back to their shelter location,
 * and leaving it there ... if you kill one, you should get the
 * items back!").
 *
 *   1. Carry sub-state: an NPC holding a tagged "hoardloot" pile in
 *      its pack is mid-delivery (or has just arrived). Routed to
 *      CNPC_HoardReturnHome, checked first and unconditionally so a
 *      settled NPC still finishes its delivery and walker arrival
 *      reaches the deposit.
 *   2. A settled NPC (0x800, shelter consumed) with no target
 *      loiters via ResourceWanderPost; a settled NPC that does have
 *      a desire target still forages and hoards.
 *   3. With no resourceAITarget, fall back to a random wander.
 *   4. A mobile target routes to CNPC_PurseDesiresPlayer.
 *   5. An item target is consumed into the pack: a physical desire
 *      pile is picked up whole; a chunk-egg node is drained and a
 *      pile of the drained amount minted. Either way the loot is
 *      tagged "hoardloot" by CNPC_StashHoardPile.
 *   6. Anchor homeLoc on first acquisition, fire acquiredesire, and
 *      hand off to CNPC_HoardReturnHome to walk the loot home.
 *
 * The loot is a live item in the NPC's pack the whole way home, so
 * killing the NPC drops the hoard on its corpse. homeInfo1/2/3 are
 * left untouched: they belong to CNPC_PurseShelterHandler, which
 * stores a serial in homeInfo3 that the death-respawn path reads.
 */
static void
CNPC_PurseDesiresHandler(CNPC *npc)
{
	CItem *self = (CItem *)npc;
	CItem *target;
	CResourceNode *node;
	int stashed = 0;
	uint32_t consumedSerial;

	npc->speechCounter = 0;

	// Carry sub-state: an NPC holding hoardloot-tagged loot is
	// mid-delivery. Routed to CNPC_HoardReturnHome first and
	// unconditionally - a settled NPC still completes its delivery,
	// walker arrival reaches the deposit, and the check no longer
	// depends on resourceAITarget already being NONE (a fragile
	// invariant); CNPC_HoardReturnHome clears the target fields itself.
	if (CNPC_CarryingHoard(npc)) {
		CNPC_HoardReturnHome(npc);
		return;
	}

	// A settled NPC loiters - unless it has a live desire target, in
	// which case it still forages.
	if ((npc->behaviorFlags & 0x800) && npc->resourceAITarget == NPC_RESTGT_NONE) {
		CNPC_ResourceWanderPost(npc);
		return;
	}

	if (!npc->resourceAITarget) {
		CNPC_ShouldProcess(npc);
		CNPC_StartWander(npc, GetRandomRange(20, 30), NPC_STATE_LOITER);
		npc->loiterData = GetRandomRange(50, 500);
		CLocation_SetLoc(&npc->loiterLoc, &self->resourceEntity.entity.location);
		return;
	}

	target = CWorld_FindBySerial(g_World, npc->resourceTargetSerial);

	// Custom: a player desire target routes to the follow/engage
	// handler, which is allowed to act on a mobile (the item path
	// below bails on any mobile target). The handler is shared by
	// every player-desire flavor (thief stealing gold, dragon hunting
	// jewels, etc.) - script-side triggers decide what happens at
	// adjacency.
	if (npc->resourceAITarget == NPC_RESTGT_MOBILE) {
		CNPC_PurseDesiresPlayer(npc, target);
		return;
	}

	if (target == NULL || ((int (*)(void *))VT_ENT_FN(&target->resourceEntity.entity, VT_IS_MOBILE))(target) != 0 || target->resourceEntity.entity.removedFromWorld != 0) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	}

	node = CResourceEntity_FindNode(target, (uint8_t)npc->resourceType, 3);

	if (DesireBodyMatch((uint16_t)CEntity_GetBodyType(target), (int)npc->resourceType)) {
		// Physical desire pile (runtime-dropped gold, jewel stack,
		// etc.): pick the whole pile up into the pack to carry home,
		// rather than deleting it and respawning one at the lair.
		CNPC_StashHoardPile(npc, target, (uint16_t)(CEntity_GetBodyType(target) & 0xFFFF));
		stashed = 1;
	} else if (node == NULL) {
		CNPC_SetState(npc, NPC_STATE_IDLE);
		return;
	} else {
		int amount = node->value3;
		uint16_t pileBody;

		if (amount < npc->resourceRate) {
			CNPC_ShouldProcess(npc);
			CNPC_SetState(npc, NPC_STATE_IDLE);
			return;
		}

		CResourceEntity_NotifyPreModify(target);
		node->value3 -= npc->resourceRate;
		CResourceEntity_NotifyPostModify(target);
		CResourceEntity_NotifyPostModifyIfActive(target);

		// Chunk-egg node drained: mint a pile of the drained amount
		// and stash it in the pack. Gold is always a 0xEED pile;
		// other desires resolve via DesireBodyMint (the inline body
		// classifier). A desire type with no physical body has
		// nothing to carry.
		pileBody = 0;
		if ((int)npc->resourceType == g_ResTypeId_Gold)
			pileBody = 0xEED;
		else
			pileBody = DesireBodyMint((int)npc->resourceType);
		if (pileBody != 0) {
			CItem *pile = CWorld_CreateItem(g_World, pileBody);
			if (pile != NULL) {
				CResourceEntity_AddNodeScaled(pile, (uint16_t)npc->resourceType, 3, (int)npc->resourceRate, 0, (int)npc->resourceRate, 0, 1, 1);
				// A freshly minted gold pile defaults to amount 1;
				// set the stack count so the drained quantity shows.
				if (pileBody == 0xEED)
					pile->amount = (uint16_t)npc->resourceRate;
				CNPC_StashHoardPile(npc, pile, pileBody);
				stashed = 1;
			}
		}
	}

	// Anchor the lair on first acquisition so the hoard has a home.
	if (CLocation_IsInvalid(&npc->homeLoc))
		CLocation_SetLoc(&npc->homeLoc, &self->resourceEntity.entity.location);

	consumedSerial = npc->resourceTargetSerial;
	npc->resourceTargetSerial = 0;
	npc->resourceAITarget = NPC_RESTGT_NONE;
	npc->scanTimer = 0;
	Entity_ExecuteEvent(&self->resourceEntity.entity, 0x34, (uintptr_t)consumedSerial); // acquiredesire
	if (npc != g_currentNPC)
		return;
	CNPC_ShouldProcess(npc);

	if (stashed)
		CNPC_HoardReturnHome(npc);
	else
		CNPC_SetState(npc, NPC_STATE_SEEK_DESIRES);
}

/*
 * Custom - CNPC_HasFoodAppetite (FEAT_ECOLOGY)
 *
 * True when the creature carries a food (type-0) appetite resource node,
 * i.e. it is meant to eat. CNPC_InitFromResourceNodes defaults
 * hungerCapacity to 99 even for creatures with no food node, so the hunger
 * lifecycle (decay, refill, starvation) must key off the appetite node
 * itself rather than hungerCapacity - otherwise undead, elementals and
 * summoned creatures (which carry no food node) would wrongly starve.
 * Stateless rescan of the live node chain (the same chain
 * CNPC_InitFromResourceNodes walks), so it cannot desync across save/load
 * or pack merges.
 */
static int
CNPC_HasFoodAppetite(CNPC *npc)
{
	CResourceNode *node;

	node = npc->mobile.container.item.resourceEntity.firstChild;
	while (node != NULL) {
		if ((int8_t)node->type == 0 && (uint16_t)node->id != 0)
			return 1;
		node = node->next;
	}
	return 0;
}

/*
 * Custom - CNPC_GrazeHandler (FEAT_ECOLOGY)
 *
 * Herbivore/forager feeding. The carnivore food AI (CNPC_FindFoodInPack)
 * only finds MEAT/CARNIVOREMEAT on corpses, but a creature's vegetation
 * food (GRASS, CROPS, FISH, ...) lives as a type-3 production node on the
 * per-8x8-block chunk egg (resbank.c ProcessStaticTiles), regrown over
 * time by CResBankManager_ProcessRespawnChunk and drained from the
 * regional bank under FEAT_CLOSED_ECONOMY. Grazing matches each of the
 * NPC's food (type-0) appetite ids against the chunk egg at its own tile;
 * on a hit with stock it consumes vegetation (node->value3) and refills
 * stomach by the same amount, so the closed loop is preserved (eaten
 * vegetation drops out of the world and regrows from the bank). The lookup
 * is a single chunk-egg fetch with no radial search or pathfinding, so it
 * avoids the cost that got the binary's food-seeking AI disabled. Returns
 * 1 if the NPC grazed.
 *
 * Pure carnivores fall through harmlessly: their only food id is MEAT/
 * CARNIVOREMEAT, which is not a vegetation node on a chunk egg, so
 * CResourceEntity_FindNode returns NULL and they continue to the meat path
 * in CNPC_FoodSeek.
 */
static int
CNPC_GrazeHandler(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	CLocation *loc = &mob->container.item.resourceEntity.entity.location;
	CBlock *block;
	CItem *egg;
	CResourceNode *pref;
	CResourceNode *node;
	int cap;
	int want;
	int bite;
	int blockIndex;

	cap = (int)npc->hungerCapacity;
	if (cap > 255)
		cap = 255; // stomach is uint8
	want = cap - (int)(uint8_t)mob->stomach;
	if (want <= 0)
		return 0; // full / not hungry

	blockIndex = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIndex < 0)
		return 0;
	block = CBlockManager_GetBlock(&g_SpatialGrid, blockIndex);
	if (block == NULL)
		return 0;
	egg = block->chunkEgg;
	if (egg == NULL)
		return 0;

	for (pref = mob->container.item.resourceEntity.firstChild; pref != NULL; pref = pref->next) {
		if ((int8_t)pref->type != 0 || (uint16_t)pref->id == 0)
			continue;
		node = CResourceEntity_FindNode(egg, (uint16_t)pref->id, 3);
		if (node == NULL || node->value3 <= 0)
			continue;

		bite = want;
		if (bite > node->value3)
			bite = node->value3;
		if (bite <= 0)
			continue;

		CResourceEntity_NotifyPreModify(egg);
		node->value3 -= bite;
		CResourceEntity_NotifyPostModify(egg);
		CResourceEntity_NotifyPostModifyIfActive(egg);

		mob->stomach = (uint8_t)((int)(uint8_t)mob->stomach + bite);
		return 1;
	}

	return 0;
}

/*
 * Custom - CNPC_CarnivoreFeed (FEAT_ECOLOGY)
 *
 * Carnivore analog of CNPC_GrazeHandler: completes Koster's "[living
 * things are] attacked first, then eaten" FOOD interaction, which the
 * binary leaves half-wired - predation kills prey but nothing refills the
 * killer's stomach (stomach is only raised by GrazeHandler and by
 * FindFoodInPack on meat already in the pack). A hungry meat-eater scans a
 * 24-tile box for a corpse (or any item) carrying a type-3 MEAT or
 * CARNIVOREMEAT node and drains it, refilling its stomach by the same
 * amount - so a predator eats the fresh corpse of its own kill, and can
 * scavenge others in its territory, instead of starving beside it.
 * CMobile_CreateCorpse drains the prey's production nodes onto the corpse
 * (CARNIVOREMEAT->MEAT), so the meat is present without a player carving it
 * first. The meat lookup mirrors CNPC_FindFoodInPack; the drain/refill and
 * the spatial box-scan mirror CNPC_GrazeHandler and CNPC_ScavengerPickup.
 * Returns 1 if it fed.
 *
 * Herbivores fall through harmlessly: they carry no MEAT/CARNIVOREMEAT food
 * appetite node, so the meat-eater gate returns 0 and they keep grazing.
 */
static int
CNPC_CarnivoreFeed(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	CLocation *loc = &mob->container.item.resourceEntity.entity.location;
	CResourceNode *pref;
	CResourceNode *node;
	CItem *ent;
	int isMeatEater;
	int cap;
	int want;
	int bite;
	int selfX, selfY;
	int x, y;
	int blockIndex;

	// Meat-eater gate: only creatures whose diet (type-0 food appetite)
	// includes MEAT or CARNIVOREMEAT eat corpses; herbivores keep grazing.
	isMeatEater = 0;
	for (pref = mob->container.item.resourceEntity.firstChild; pref != NULL; pref = pref->next) {
		if ((int8_t)pref->type != 0 || (uint16_t)pref->id == 0)
			continue;
		if ((g_ResTypeId_Meat != 0 && (int)pref->id == g_ResTypeId_Meat) || (g_ResTypeId_CarnivoreMeat != 0 && (int)pref->id == g_ResTypeId_CarnivoreMeat)) {
			isMeatEater = 1;
			break;
		}
	}
	if (!isMeatEater)
		return 0;

	cap = (int)npc->hungerCapacity;
	if (cap > 255)
		cap = 255; // stomach is uint8
	want = cap - (int)(uint8_t)mob->stomach;
	if (want <= 0)
		return 0; // full / not hungry

	selfX = (int)(int16_t)loc->x;
	selfY = (int)(int16_t)loc->y;

	for (x = selfX - 24; x <= selfX + 24; x += 8) {
		for (y = selfY - 24; y <= selfY + 24; y += 8) {
			blockIndex = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
			if (blockIndex < 0)
				continue;
			ent = g_SpatialGrid.cells[blockIndex].itemHead;
			while (ent != NULL) {
				if (ent == (CItem *)npc || ent->resourceEntity.entity.removedFromWorld || VT_IsMobile(ent)) {
					ent = ent->spatialNext;
					continue;
				}

				node = NULL;
				if (g_ResTypeId_Meat != 0)
					node = CResourceEntity_FindNode(ent, (uint16_t)g_ResTypeId_Meat, 3);
				if (node == NULL && g_ResTypeId_CarnivoreMeat != 0)
					node = CResourceEntity_FindNode(ent, (uint16_t)g_ResTypeId_CarnivoreMeat, 3);

				if (node != NULL && node->value3 > 0) {
					bite = want;
					if (bite > node->value3)
						bite = node->value3;

					CResourceEntity_NotifyPreModify(ent);
					node->value3 -= bite;
					CResourceEntity_NotifyPostModify(ent);
					CResourceEntity_NotifyPostModifyIfActive(ent);

					mob->stomach = (uint8_t)((int)(uint8_t)mob->stomach + bite);

					if ((CEntity_GetBodyType(ent) & 0xFFFF) == 0x2006) {
						CNPC_HandleCorpseEat(npc, ent);
					}
					return 1;
				}

				ent = ent->spatialNext;
			}
		}
	}

	return 0;
}

/*
 * Custom - CNPC_ForageLeash (FEAT_ECOLOGY)
 *
 * Keeps an NPC tagged by the .foragemode GM test aid inside the radius
 * where it still runs its AI.
 *
 * CNPC::WanderStep re-rolls a patrol target up to ten tiles from the
 * NPC's current tile on every wander step, so a wandering creature is a
 * random walk whose stride carries it tens of tiles within a minute.
 * The AI tick freezes any NPC with no player inside eighteen tiles, and
 * a frozen NPC resets its own freeze timer indefinitely - only
 * CNPC_OnPlayerEnteredRange revives it. A forager that drifts out of a
 * stationary observer's radius is therefore gone for the rest of the
 * run, which left a squad of test dragons stranded about one run in
 * four with nothing foraged at all.
 *
 * The aid has two halves and they are separable: mode 1 both replaces the
 * IDLE roll and leashes, mode 2 leashes only. A test that exists to watch
 * the unforced roll still needs its squad to stay where it can be watched,
 * and freezing is a property of the harness - no player within eighteen
 * tiles - rather than of the AI under test.
 *
 * While foragemode is set, an NPC further than FORAGE_LEASH_TILES from
 * the tile it was tagged on ("foragex"/"foragey", stamped by
 * GM_ApplyForageMode) walks back to it instead of wandering further
 * out, arriving in IDLE so the foragemode roll picks SEEK_DESIRES up
 * again. Returns 1 when it claimed the tick.
 *
 * The anchor is the tag tile rather than homeLoc because homeLoc
 * belongs to the test: the carry-home test re-homes its squad thirty
 * tiles away for the kill-mid-carry phase, and a homeLoc anchor would
 * march every idle dragon out of the observer's range exactly then.
 *
 * A pursuing or carrying NPC is already walking, and the isWalking
 * early-out above this call means the leash never interrupts one.
 *
 * Only NPCs a GM has tagged are affected - the binary's wander is
 * untouched, and an NPC tagged before this field existed is left
 * alone.
 */
#define FORAGE_LEASH_TILES 10

static int
CNPC_ForageLeash(CNPC *npc)
{
	CItem *self = (CItem *)npc;
	CLocation anchor;
	int forageMode = 0;
	int anchorX, anchorY;
	int dx, dy;

	CResourceEntity_GetTagInt(self, "foragemode", &forageMode);
	if (forageMode == 0)
		return 0;

	anchorX = 0;
	anchorY = 0;
	CResourceEntity_GetTagInt(self, "foragex", &anchorX);
	CResourceEntity_GetTagInt(self, "foragey", &anchorY);
	if (anchorX <= 0 || anchorY <= 0)
		return 0;

	dx = anchorX - (int)(int16_t)self->resourceEntity.entity.location.x;
	dy = anchorY - (int)(int16_t)self->resourceEntity.entity.location.y;
	if (abs(dx) <= FORAGE_LEASH_TILES && abs(dy) <= FORAGE_LEASH_TILES)
		return 0;

	CLocation_Init(&anchor);
	CLocation_Set(&anchor, (uint16_t)anchorX, (uint16_t)anchorY, 0);
	CLocation_SetLoc(&npc->patrolTarget, &anchor);
	npc->isWalking = 1;
	npc->ltype = NPC_STATE_IDLE;
	npc->stateInfo2 = NPC_STATE_IDLE;
	CNPC_SetState(npc, NPC_STATE_IDLE);
	return 1;
}

/*
 * Helper - NPC_AddToHash
 *
 * Inserts npc at the head of its serial bucket in g_NPCHash.
 *
 * The binary has no such function: CNPC::InitFields writes the insert
 * inline, twice, and both copies are visible in its disassembly as the
 * `AND EAX,0x3f` bucket computations at 0x00480D44 and 0x00480D88. This
 * was annotated 0x00480D3F, which lands mid-instruction inside InitFields
 * and gave the row a size of 845 bytes - InitFields' own.
 */
void
NPC_AddToHash(CNPC *npc)
{
	uint32_t bucket;

	bucket = npc->mobile.container.item.serial & 0x3F;
	npc->npcHashNext = g_NPCHash[bucket];
	if (npc->npcHashNext != NULL)
		npc->npcHashNext->npcHashPrev = npc;
	npc->npcHashPrev = NULL;
	g_NPCHash[bucket] = npc;
}
