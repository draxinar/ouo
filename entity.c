/*
 * CEntity - root of the entity hierarchy.
 *
 * Body-type assignment, scripted-event dispatch, world add/remove
 * (spatial grid, hash, mobile/NPC/player lists), notoriety timers, and
 * the CEntity constructor/destructor pair.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include "egg.h"
#include "entitymanager.h"
#include "help_queue.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "resbank.h"
#include "shopkeeper.h"
#include "taglist.h"
#include "timer.h"
#include "vtable.h"
#include "wombat.h"
#include "wombat_compile.h"
#include "world.h"

// 0x0068B37C
uint32_t g_EntityCount;

// 0x0068B390
uint32_t g_ResourceEntityCount;

/*
 * 0x00420F60 - CEntity::SetBodyType
 *
 * Validates bodyType via CWorld_IsValidItemResource, falling back to 0x097D.
 */
void
CEntity_SetBodyType(CItem *item, uint16_t bodyType)
{
	if (!CWorld_IsValidItemResource(bodyType))
		bodyType = 0x097D;
	item->resourceEntity.entity.bodyType = bodyType;
}

/*
 * 0x0042B92F - Entity_ExecuteEvent
 *
 * MODIFIED: routes through CWombatManager_FireTrigger_va (the Wombat
 * script engine),
 * rather than calling the binary's ExtractEventParams directly.
 */
char *
Entity_ExecuteEvent(CEntity *this, int eventType, ...)
{
	va_list ap;
	int ret;

	va_start(ap, eventType);
	ret = CWombatManager_FireTrigger_va((CItem *)this, eventType, ap);
	va_end(ap);

	return (char *)(intptr_t)ret;
}

/*
 * 0x00432290 - CEntity::GetRemovedFromWorld (vtable[0x8C])
 *
 * Returns the entity's removedFromWorld flag.
 */
int
CEntity_GetRemovedFromWorld(CItem *item)
{
	return (int)item->resourceEntity.entity.removedFromWorld;
}

/*
 * 0x004858AF - CEntity::CEntity
 *
 * Invalidates location, installs the CEntity vtable, and bumps g_EntityCount.
 */
CEntity *
CEntity_Constructor(CEntity *this)
{
	CLocation_Init(&this->location);
	this->vtable = &g_vtable_CEntity;
	g_EntityCount++;
	CEntity_SetBodyType((CItem *)this, 0);
	this->removedFromWorld = 1;
	this->color = 0;
	return this;
}

/*
 * 0x004858FA - CEntity::~CEntity
 *
 * Restores the CEntity vtable, invokes a no-op stub when still in-world,
 * and decrements g_EntityCount.
 */
void
CEntity_Destructor(CEntity *this)
{
	this->vtable = &g_vtable_CEntity;
	if (this->removedFromWorld == 0)
		CResBankMagicCtx_PostInit((CResListNode **)this);
	g_EntityCount--;
}

/*
 * 0x004862CF - GetLandTileFlags
 *
 * Returns the movement-type-filtered flags for a land tile. Cases 0/1/2/6
 * set 0x600 when the tile is not walkable; case 7 always returns 0x600;
 * case 8 passes through to 0/1/2/6 for non-water tiles.
 */
uint32_t
GetLandTileFlags(uint16_t tileID, int moveType)
{
	uint32_t tileFlags;
	uint32_t result;

	if (Terrain_IsVoidTile(tileID))
		return 0;

	tileFlags = g_LandTileData[(int)tileID].flags;
	result = 0x40;

	switch (moveType) {
	case 8:
		if (tileID >= 0x71 && tileID <= 0x78)
			break;
		if (tileID >= 0x09 && tileID <= 0x15)
			break;
		if (tileID >= 0x150 && tileID <= 0x15C)
			break;
		// FALLTHROUGH
	case 0:
	case 1:
	case 2:
	case 6:
		if (!(tileFlags & 0x40))
			result |= 0x0600;
		break;
	case 3:
	case 4:
		if (tileFlags & 0x80)
			result |= 0x0600;
		break;
	case 5:
		if (!(tileFlags & 0x40) || (tileFlags & 0x80))
			result |= 0x0600;
		break;
	case 7:
		return 0x600;
	default:
		break;
	}

	return result;
}

/*
 * 0x0048FFD2 - CEntity::ProcessNotorietyTimer
 *
 * Expires one of five notoriety ObjVars (criminal, aggressionVictimList,
 * lawfullyDamaged, murderCount, recentlyReported). murderCount counts down
 * and re-schedules itself, dropping out of murderer status at count 4;
 * lawfullyDamaged for players notifies the previously-damaged list; the
 * rest simply detach the ObjVar and broadcast an appearance update.
 */
void
CEntity_ProcessNotorietyTimer(CItem *this, int timerType)
{
	static const char *const g_notorietyVarNames[] = {
		"criminal",             /* 0 */
		"aggressionVictimList", /* 1 */
		"lawfullyDamaged",      /* 2 */
		"murderCount",          /* 3 */
		"recentlyReported"      /* 4 */
	};
	const char *varName;
	uint32_t serial;

	varName = g_notorietyVarNames[timerType];
	serial = this->serial;

	if (timerType == 2) {
		CList *list;

		list = CResourceEntity_GetTagEntity(this, varName);
		if (list == NULL)
			return;

		if (VT_IsPlayer(this)) {
			CVector players;
			uintptr_t *iter;

			CVector_Constructor(&players, "\x01");
			GetNearbyPlayers(&players, ((CLocation * (*)(CItem *)) VT_FN(this, VT_GET_LOCATION))(this), 0x12);

			for (iter = (uintptr_t *)players.begin; iter != (uintptr_t *)players.end; iter++) {
				CItem *nearby = (CItem *)*iter;
				if (!CList_Find(list, 4, nearby->serial))
					*iter = 0;
			}

			CResourceEntity_DetachScript(this, varName);

			for (iter = (uintptr_t *)players.begin; iter != (uintptr_t *)players.end; iter++) {
				if (*iter != 0) {
					CItem *nearby = (CItem *)*iter;
					((void (*)(CItem *, CItem *, int))VT_FN(nearby, VT_SEND_ENTITY_UPDATE))(nearby, this, 0);
				}
			}

			CVector_Destructor(&players);
		} else {
			CResourceEntity_DetachScript(this, varName);
		}
	} else if (timerType == 3) {
		int count = 0;

		CResourceEntity_GetTagInt(this, varName, &count);
		count--;

		if (count <= 0) {
			CResourceEntity_DetachScript(this, varName);
		} else {
			ScheduleEvent(0x1c200, serial, 0x11, timerType, 0);

			CEntity_SetObjVar(this, varName, 0, (uint32_t)count);

			if (count == 4) {
				CVector vec;

				CVector_Constructor(&vec, "\x01");
				GetNearbyPlayers(&vec, ((CLocation * (*)(CItem *)) VT_FN(this, VT_GET_LOCATION))(this), 0x12);
				((void (*)(CItem *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &vec, 0);
				CVector_Destructor(&vec);
			}
		}
	} else {
		CVector vec;

		CResourceEntity_DetachScript(this, varName);

		CVector_Constructor(&vec, "\x01");
		GetNearbyPlayers(&vec, ((CLocation * (*)(CItem *)) VT_FN(this, VT_GET_LOCATION))(this), 0x12);
		((void (*)(CItem *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &vec, 0);
		CVector_Destructor(&vec);
	}
}

/*
 * 0x00491100 - CEntity::`scalar deleting destructor'
 *
 * ORPHANED: zero callers in the binary. MSVC-emitted vtable[0] thunk
 * for the abstract CEntity base; all concrete entities are destroyed
 * through their subclass ScalarDelete thunks.
 */
static __attribute__((unused)) void *
CEntity_ScalarDelete(CEntity *this, int flags)
{
	CEntity_Destructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x004DA050 - CEntity::RemoveFromWorld
 *
 * Unlinks the entity from the serial hash, spatial grid, template chain,
 * mobile/player/NPC lists, and equipment/contents chains. Recurses into
 * container contents and mobile equipment slots.
 */
void
CEntity_RemoveFromWorld(CItem *this)
{
	CVector childVec;
	char typeFlag;
	CItem *child;
	uintptr_t *iter;
	uintptr_t *iterEnd;
	CMobile *mob;
	CNPC *npc;
	CNPC *npcCast;
	CPlayer *player;
	int blockIdx;
	int i;

	if (VT_IsMobile2(this)) {
		if (((int (*)(void *))VT_FN(this, VT_CANCEL_TRADE))(this))
			return;
	}

	if (this->hashNext != NULL)
		this->hashNext->hashPrev = this->hashPrev;

	if (this->hashPrev != NULL) {
		this->hashPrev->hashNext = this->hashNext;
	} else {
		uint32_t hashIdx = this->serial & 0xFFFF;
		if (g_World->hashTable[hashIdx] == this)
			g_World->hashTable[hashIdx] = this->hashNext;
	}
	this->hashPrev = NULL;
	this->hashNext = NULL;

	if (g_ArchiveFlag)
		ArchiveHash_Insert(this);

	if (this->parent == NULL) {
		if (!this->resourceEntity.entity.removedFromWorld) {
			blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int16_t)this->resourceEntity.entity.location.x, (int16_t)this->resourceEntity.entity.location.y, 0);

			if (this->spatialNext != NULL)
				this->spatialNext->spatialPrev = this->spatialPrev;

			if (this->spatialPrev != NULL) {
				this->spatialPrev->spatialNext = this->spatialNext;
			} else {
				if (g_SpatialGrid.cells[blockIdx].itemHead == this)
					g_SpatialGrid.cells[blockIdx].itemHead = this->spatialNext;
			}

			this->spatialPrev = NULL;
			this->spatialNext = NULL;
		} else {
			this->resourceEntity.entity.removedFromWorld = 0;
			this->spatialNext = SPATIAL_SENTINEL;
		}
	}

	TemplateChain_Remove(this);

	if (!VT_IsMobile2(this))
		return;

	typeFlag = 0;
	CVector_Constructor(&childVec, &typeFlag);

	child = ((CContainer *)this)->contents;
	while (child != NULL) {
		CVector_PushBack(&childVec, (uintptr_t)child);
		child = child->spatialNext;
	}

	iter = (uintptr_t *)childVec.begin;
	iterEnd = (uintptr_t *)childVec.end;
	while (iter != iterEnd) {
		CEntity_RemoveFromWorld((CItem *)*iter);
		iter++;
	}

	if (!VT_IsMobile(this))
		goto cleanup;

	mob = (CMobile *)this;

	for (i = 0; i < 30; i++) {
		if (mob->equipment[i] != NULL)
			CEntity_RemoveFromWorld(mob->equipment[i]);
	}

	if (mob->nextMobile != NULL)
		mob->nextMobile->prevMobile = mob->prevMobile;

	if (mob->prevMobile != NULL) {
		mob->prevMobile->nextMobile = mob->nextMobile;
	} else {
		if (g_MobileListHead == mob)
			g_MobileListHead = mob->nextMobile;
	}
	mob->nextMobile = NULL;
	mob->prevMobile = NULL;

	if (mob->isFollower) {
		CMobile *owner;

		if (mob->nextFollower != NULL) {
			mob->nextFollower->prevFollower = mob->prevFollower;
		} else {
			owner = mob->owner;
			if (owner->lastFollower == mob)
				owner->lastFollower = mob->prevFollower;
		}

		if (mob->prevFollower != NULL) {
			mob->prevFollower->nextFollower = mob->nextFollower;
		} else {
			owner = mob->owner;
			if (owner->firstFollower == mob)
				owner->firstFollower = mob->nextFollower;
		}

		mob->isFollower = 0;
		mob->owner = NULL;
	}

	if (VT_IsNPC(this)) {
		npc = (CNPC *)this;

		CNPCManager_RemoveFromSpatialMap(this);

		if (npc->npcHashNext != NULL)
			npc->npcHashNext->npcHashPrev = npc->npcHashPrev;

		if (npc->npcHashPrev != NULL) {
			npc->npcHashPrev->npcHashNext = npc->npcHashNext;
		} else {
			uint32_t bucket = this->serial & 0x3F;
			if (g_NPCHash[bucket] == npc)
				g_NPCHash[bucket] = npc->npcHashNext;
		}
		npc->npcHashNext = NULL;
		npc->npcHashPrev = NULL;

		if (CNPC_IsInLevelList(npc))
			CNPC_RemoveFromLevelList(npc);

		if (VT_IsVendor(this)) {
			CShopkeeper_UnlinkFromVendorList(npc);
		} else if (((int (*)(void *))VT_FN(this, VT_CHECK_EC))(this)) {
			npcCast = (CNPC *)this;

			MobileMap_Remove(this);

			if (npcCast->nextNPC != NULL)
				npcCast->nextNPC->prevNPC = npcCast->prevNPC;

			if (npcCast->prevNPC != NULL) {
				npcCast->prevNPC->nextNPC = npcCast->nextNPC;
			} else {
				if (g_NPCListHead == npcCast)
					g_NPCListHead = npcCast->nextNPC;
			}
			npcCast->nextNPC = NULL;
			npcCast->prevNPC = NULL;
		}
	} else if (VT_IsPlayer(this)) {
		player = (CPlayer *)this;

		ItemMap_Remove(this);

		CPlayerList_RemovePlayer(player);

		if (CPlayer_IsCounselor(player))
			CHelpQueue_DecrCounselors(&g_HelpQueue, player);
	}

cleanup:
	CVector_Destructor(&childVec);
}

/*
 * 0x004DA5C5 - CEntity::AddToWorld
 *
 * Inverse of CEntity_RemoveFromWorld. Re-inserts the entity into the serial
 * hash, spatial grid, template chain, and type-specific lists, then recurses
 * into contents and equipment.
 */
void
CEntity_AddToWorld(CItem *entity)
{
	CMobile *mob;
	CNPC *npc;
	CPlayer *player;
	CItem *child;
	int blockIdx;
	int i;

	if (g_ArchiveFlag)
		ArchiveHash_Remove(entity);

	entity->hashNext = g_World->hashTable[entity->serial & 0xFFFF];
	if (entity->hashNext != NULL)
		entity->hashNext->hashPrev = entity;
	entity->hashPrev = NULL;
	g_World->hashTable[entity->serial & 0xFFFF] = entity;

	if (entity->parent == NULL) {
		if (entity->spatialNext == SPATIAL_SENTINEL) {
			entity->resourceEntity.entity.removedFromWorld = 1;
		} else {
			blockIdx = CBlockManager_GetBlockIndex(
			        &g_SpatialGrid, (int16_t)entity->resourceEntity.entity.location.x, (int16_t)entity->resourceEntity.entity.location.y, 0);

			entity->spatialNext = g_SpatialGrid.cells[blockIdx].itemHead;
			if (entity->spatialNext != NULL)
				entity->spatialNext->spatialPrev = entity;
			entity->spatialPrev = NULL;
			g_SpatialGrid.cells[blockIdx].itemHead = entity;
		}
	}

	TemplateChain_Insert(entity);

	if (entity->timerHead != NULL)
		CTimerManager_EnsureTracked(entity);

	if (!VT_IsMobile2(entity))
		return;

	child = ((CContainer *)entity)->contents;
	while (child != NULL) {
		CEntity_AddToWorld(child);
		child = child->spatialNext;
	}

	if (!VT_IsMobile(entity))
		return;

	mob = (CMobile *)entity;

	for (i = 0; i < 0x1E; i++) {
		if (mob->equipment[i] != NULL)
			CEntity_AddToWorld(mob->equipment[i]);
	}

	mob->nextMobile = g_MobileListHead;
	if (mob->nextMobile != NULL)
		mob->nextMobile->prevMobile = mob;
	mob->prevMobile = NULL;
	g_MobileListHead = mob;

	if (VT_IsNPC(entity)) {
		npc = (CNPC *)entity;

		CNPCManager_AddToSpatialMap(entity);

		npc->npcHashNext = g_NPCHash[entity->serial & 0x3F];
		if (npc->npcHashNext != NULL)
			npc->npcHashNext->npcHashPrev = npc;
		npc->npcHashPrev = NULL;
		g_NPCHash[entity->serial & 0x3F] = npc;

		CNPC_IsInLevelList(npc);
		CNPC_AddToLevelList(npc);

		if (VT_IsVendor(entity)) {
			CShopkeeper_LinkToVendorList(npc);
		} else if (((int (*)(void *))VT_FN(entity, VT_CHECK_EC))(entity)) {
			npc = (CNPC *)entity;

			MobileMap_Insert(entity);

			npc->nextNPC = g_NPCListHead;
			if (npc->nextNPC != NULL)
				npc->nextNPC->prevNPC = npc;
			npc->prevNPC = NULL;
			g_NPCListHead = npc;
		}
	} else if (VT_IsPlayer(entity)) {
		player = (CPlayer *)entity;

		ItemMap_Insert(entity);

		CPlayerList_AddPlayer(player);

		if (CPlayer_IsCounselor(player))
			CHelpQueue_NotifyLogin(&g_HelpQueue, player);
	}
}
