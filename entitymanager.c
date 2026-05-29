/*
 * CEntityManager - dead-entity archive indexed by serial.
 *
 * Keeps a hash of recently destroyed entities so that references still
 * holding their serial can be resolved back to the object on demand
 * (e.g. logout / login ghosting, delayed packet dispatch).
 */

#include <stddef.h>
#include <stdint.h>
#include <strings.h>

#include "dat.h"
#include "egg.h"
#include "entitymanager.h"
#include "item.h"
#include "npc.h"
#include "packet_manager.h"
#include "player.h"
#include "skill.h"
#include "timer.h"
#include "vtable.h"
#include "world.h"

static void EntityManager_DeleteArchivedEntity(uint32_t serial); // 0x00491FE0
static void EntityManager_MarkAll(void); // 0x00491F60
static void EntityManager_Remove(CItem *entity); // 0x00491F10
static CItem *EntityManager_RestoreFromArchive(uint32_t serial); // 0x00491E40
static StdPtrList *CEntityManager_GetList(StdPtrList *this); // 0x00491E30
static StdPtrList *EntityManager_Constructor(StdPtrList *this, const void *init); // 0x00491E10
static void EntityManager_DeleteArchivedBySerial(uint32_t serial); // 0x00491B35
static void EntityManager_MarkAllEntities(void); // 0x00491B26
static void EntityManager_RestoreAndDelete(uint32_t serial); // 0x00491AE9

/*
 * 0x0049196F - ArchiveHash_Insert
 *
 * Head-inserts entity into g_ArchiveHash[serial & 0x3FFF].
 */
void
ArchiveHash_Insert(CItem *item)
{
	uint32_t bucket;

	bucket = item->serial & 0x3FFF;
	item->hashNext = g_ArchiveHash[bucket];
	if (item->hashNext != NULL)
		item->hashNext->hashPrev = item;
	item->hashPrev = NULL;
	g_ArchiveHash[bucket] = item;
}

/*
 * 0x004919C2 - ArchiveHash_Remove
 *
 * Unlinks entity from g_ArchiveHash chain. Inverse of ArchiveHash_Insert.
 */
void
ArchiveHash_Remove(CItem *item)
{
	if (item->hashNext != NULL)
		item->hashNext->hashPrev = item->hashPrev;

	if (item->hashPrev != NULL) {
		item->hashPrev->hashNext = item->hashNext;
	} else {
		uint32_t bucket = item->serial & 0x3FFF;
		g_ArchiveHash[bucket] = item->hashNext;
	}
	item->hashPrev = NULL;
	item->hashNext = NULL;
}

/*
 * 0x00491A26 - ArchiveHash_Find
 *
 * Looks up an archived entity by serial. Returns NULL if not found.
 */
CItem *
ArchiveHash_Find(uint32_t serial)
{
	CItem *cur;

	cur = g_ArchiveHash[serial & 0x3FFF];
	while (cur != NULL) {
		if (cur->serial == serial)
			return cur;
		cur = cur->hashNext;
	}
	return NULL;
}

/*
 * 0x00491A65 - EntityManager::GetListPtr
 *
 * Returns the entity manager's StdPtrList.
 */
StdPtrList *
EntityManager_GetListPtr(void)
{
	return CEntityManager_GetList(&g_entityMgrList);
}

/*
 * 0x00491A74 - EntityManager::RestoreBySerial
 *
 * Global wrapper for EntityManager_RestoreFromArchive.
 */
CItem *
EntityManager_RestoreBySerial(uint32_t serial)
{
	return EntityManager_RestoreFromArchive(serial);
}

/*
 * 0x00491AA7 - BroadcastDestroyAndRemove
 *
 * Broadcasts DESTROY_OBJECT within range 18 then archives the entity.
 */
void
BroadcastDestroyAndRemove(CItem *entity)
{
	uint8_t buf[8];

	PacketManager_MakePacket_DESTROY_OBJECT(buf, entity->serial);
	SendPacketInRange(buf, &entity->resourceEntity.entity.location, 0x12);
	EntityManager_Remove(entity);
}

/*
 * 0x00491AE9 - EntityManager::RestoreAndDelete
 *
 * Restores entity from archive, locks it down, and deletes it.
 * Orphaned in the binary.
 */
static __attribute__((unused)) void
EntityManager_RestoreAndDelete(uint32_t serial)
{
	CItem *entity;

	entity = EntityManager_RestoreFromArchive(serial);
	if (entity != NULL) {
		CItem_SetLockdown(entity, 1);
		if (entity != NULL)
			((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
	}
}

/*
 * 0x00491B26 - EntityManager::MarkAllEntities
 *
 * Global wrapper for EntityManager_MarkAll. Orphaned in the binary.
 */
static __attribute__((unused)) void
EntityManager_MarkAllEntities(void)
{
	EntityManager_MarkAll();
}

/*
 * 0x00491B35 - EntityManager::DeleteArchivedBySerial
 *
 * Global wrapper for EntityManager_DeleteArchivedEntity. Orphaned in the
 * binary.
 */
static __attribute__((unused)) void
EntityManager_DeleteArchivedBySerial(uint32_t serial)
{
	EntityManager_DeleteArchivedEntity(serial);
}

/*
 * 0x00491B48 - EntityManager_AddAllToWorld
 *
 * Calls CEntity_AddToWorld on every archived entity.
 */
void
EntityManager_AddAllToWorld(void)
{
	StdPtrList *list;
	StdPtrNode *iter, *beginTemp, *endTemp, *endCopy, *postIncTemp;

	list = CEntityManager_GetList(&g_entityMgrList);
	StdPtrIter_CopyConstructor(&iter, StdPtrList_Begin(list, &beginTemp));
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End(list, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		CEntity_AddToWorld(*(CItem **)StdPtrIter_Deref(&iter));
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * 0x00491BBF - EntityManager_RemoveAllFromWorld
 *
 * Calls CEntity_RemoveFromWorld on every archived entity.
 */
void
EntityManager_RemoveAllFromWorld(void)
{
	StdPtrList *list;
	StdPtrNode *iter, *beginTemp, *endTemp, *endCopy, *postIncTemp;

	list = CEntityManager_GetList(&g_entityMgrList);
	StdPtrIter_CopyConstructor(&iter, StdPtrList_Begin(list, &beginTemp));
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End(list, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		CEntity_RemoveFromWorld(*(CItem **)StdPtrIter_Deref(&iter));
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * 0x00491C36 - EntityManager::CollectByAccountID
 *
 * Appends every archived entity whose accountNum matches to results.
 */
void
EntityManager_CollectByAccountID(CVector *results, uint32_t accountId)
{
	StdPtrList *list;
	StdPtrNode *iter, *beginTemp, *endTemp, *endCopy, *postIncTemp;
	CItem *entity;

	list = CEntityManager_GetList(&g_entityMgrList);
	StdPtrIter_CopyConstructor(&iter, StdPtrList_Begin(list, &beginTemp));
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End(list, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		entity = *(CItem **)StdPtrIter_Deref(&iter);
		if (((CPlayer *)entity)->accountNum == accountId) {
			CVector_PushBack(results, (uintptr_t)entity);
		}
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * 0x00491CC4 - EntityManager_FindByName
 *
 * Returns the first archived entity whose vtable[0x34] name matches
 * (case-insensitive), or NULL.
 */
CItem *
EntityManager_FindByName(char *name)
{
	StdPtrList *list;
	StdPtrNode *iter, *beginTemp, *endTemp, *endCopy, *postIncTemp;
	CItem *entity;

	list = CEntityManager_GetList(&g_entityMgrList);
	StdPtrIter_CopyConstructor(&iter, StdPtrList_Begin(list, &beginTemp));
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End(list, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		entity = *(CItem **)StdPtrIter_Deref(&iter);
		if (strcasecmp(((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity), name) == 0)
			return entity;
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
	return NULL;
}

/*
 * 0x00491D63 - EntityManager_FindBySerial
 *
 * Returns the archived entity with the given serial, or NULL.
 */
CItem *
EntityManager_FindBySerial(uint32_t serial)
{
	StdPtrList *list;
	StdPtrNode *iter, *beginTemp, *endTemp, *endCopy, *postIncTemp;
	CItem *entity;

	list = CEntityManager_GetList(&g_entityMgrList);
	StdPtrIter_CopyConstructor(&iter, StdPtrList_Begin(list, &beginTemp));
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End(list, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		entity = *(CItem **)StdPtrIter_Deref(&iter);
		if (entity->serial == serial)
			return entity;
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
	return NULL;
}

/*
 * 0x00491E10 - EntityManager::EntityManager
 *
 * Forwards to StdPtrList_Init.
 */
static __attribute__((unused)) StdPtrList *
EntityManager_Constructor(StdPtrList *this, const void *init)
{
	StdPtrList_Init(this, init);
	return this;
}

/*
 * 0x00491E30 - CEntityManager::GetList
 *
 * Identity accessor that returns this.
 */
static StdPtrList *
CEntityManager_GetList(StdPtrList *this)
{
	return this;
}

/*
 * 0x00491E40 - EntityManager::RestoreFromArchive
 *
 * Moves an archived entity back into the world by serial: add-to-world
 * under g_ArchiveFlag, clear aiState bit 0x2000, erase from the archive
 * list. Returns the entity or NULL if not found.
 */
static CItem *
EntityManager_RestoreFromArchive(uint32_t serial)
{
	StdPtrNode *iter, *beginTemp, *endTemp, *postIncTemp, *eraseResult;
	CItem *entity;

	StdPtrIter_BaseConstructor(&iter);
	iter = *StdPtrList_Begin(&g_entityMgrList, &beginTemp);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(&g_entityMgrList, &endTemp)) & 0xFF))
			break;
		entity = *(CItem **)StdPtrIter_Deref(&iter);
		if (entity->serial != serial) {
			StdPtrIter_PostInc(&iter, &postIncTemp, 0);
			continue;
		}
		g_ArchiveFlag = 1;
		CEntity_AddToWorld(entity);
		g_ArchiveFlag = 0;
		// Binary: unconditional AND at offset 0x3A8. See EntityManager_Remove
		// comment for 64-bit divergence.
		if (VT_IsPlayer(entity))
			((CPlayer *)entity)->pflags &= ~0x2000;
		else
			((CNPC *)entity)->aiState &= ~0x2000;
		StdPtrList_Erase(&g_entityMgrList, &eraseResult, iter);
		return entity;
	}
	return NULL;
}

/*
 * 0x00491F10 - EntityManager::Remove
 *
 * Archives an entity: remove-from-world under g_ArchiveFlag, set aiState
 * bit 0x2000, push onto the archive list.
 */
static void
EntityManager_Remove(CItem *entity)
{
	g_ArchiveFlag = 1;
	CEntity_RemoveFromWorld(entity);
	g_ArchiveFlag = 0;

	// Binary: unconditional OR at offset 0x3A8 (aiState for NPC,
	// pflags for CPlayer - same offset in 32-bit). In 64-bit, pointer
	// widening causes these offsets to diverge, so type-check here.
	if (VT_IsPlayer(entity))
		((CPlayer *)entity)->pflags |= 0x2000;
	else
		((CNPC *)entity)->aiState |= 0x2000;

	StdPtrList_PushBack(&g_entityMgrList, entity);
}

/*
 * 0x00491F60 - EntityManager::MarkAll
 *
 * Sets aiState bit 0x00010000 on every archived entity.
 */
static void
EntityManager_MarkAll(void)
{
	StdPtrNode *iter, *beginTemp, *endTemp, *postIncTemp;
	uint32_t *pflags;

	StdPtrIter_BaseConstructor(&iter);
	iter = *StdPtrList_Begin(&g_entityMgrList, &beginTemp);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(&g_entityMgrList, &endTemp)) & 0xFF))
			break;
		pflags = &((CNPC *)*(CItem **)StdPtrIter_Deref(&iter))->aiState;
		*pflags |= 0x00010000;
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * 0x00491FE0 - EntityManager::DeleteArchivedEntity
 *
 * Deletes an archived entity by serial, but only if aiState bit 0x00010000
 * is set (the mark written by EntityManager_MarkAll). The entity is briefly
 * re-added to the world with activity/respawn suspended so VT_DELETE can
 * run, then erased from the archive list.
 */
static void
EntityManager_DeleteArchivedEntity(uint32_t serial)
{
	StdPtrNode *iter, *beginTemp, *endTemp, *postIncTemp, *eraseResult;
	CItem *entity;

	StdPtrIter_BaseConstructor(&iter);
	iter = *StdPtrList_Begin(&g_entityMgrList, &beginTemp);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(&g_entityMgrList, &endTemp)) & 0xFF))
			break;
		entity = *(CItem **)StdPtrIter_Deref(&iter);
		if (entity->serial != serial) {
			StdPtrIter_PostInc(&iter, &postIncTemp, 0);
			continue;
		}
		if (!(((CNPC *)entity)->aiState & 0x10000)) {
			StdPtrIter_PostInc(&iter, &postIncTemp, 0);
			continue;
		}
		g_ArchiveFlag = 1;
		CEntity_AddToWorld(entity);
		g_ArchiveFlag = 0;
		g_WorldActive = 0;
		g_WorldActive2 = 0;
		g_SuppressRespawn = 1;
		g_DeleteAllowed = 1;
		CItem_SetLockdown(entity, 1);
		if (entity != NULL)
			((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
		g_DeleteAllowed = 0;
		g_SuppressRespawn = 0;
		g_WorldActive2 = 1;
		g_WorldActive = 1;
		StdPtrList_Erase(&g_entityMgrList, &eraseResult, iter);
		break;
	}
}

/*
 * 0x00492C6B - CEntityManager::CEntityManager
 *
 * Zero-inits field0 and calls SkillInfo_InitEmpty.
 */
CEntityManager *
CEntityManager_Constructor(CEntityManager *this)
{
	this->field0 = 0;
	SkillInfo_InitEmpty(this);
	return this;
}

/*
 * 0x00492C87 - CEntityManager::~CEntityManager
 *
 * Empty destructor.
 */
void
CEntityManager_Destructor(CEntityManager *this)
{
	USED(this);
}

/*
 * Custom - EntityManager_ShutdownArchive
 *
 * Server-shutdown cleanup walker for the dead-entity archive. A
 * disconnected player (or any entity passed through
 * EntityManager_Remove) is taken out of the world hash and parked in
 * g_ArchiveHash with its scripts, tags, timers, and lazily-allocated
 * tracking node still attached. The binary never releases these - the
 * process just exits - and World_ShutdownEntities only walks
 * g_World->hashTable, so archived entities escape it and their script
 * member-scope buffers leak. Walk the archive and release the same
 * resources World_ShutdownEntities does, keeping the valgrind
 * shutdown report focused on real leaks. No binary equivalent.
 *
 * Like World_ShutdownEntities, this does NOT call the entity
 * destructor: socket and ticker subsystems are still alive at this
 * point and a full destructor walk would broadcast packets and touch
 * shutdown subsystems. Archived entities are disjoint from the world
 * hash, so there is no overlap with World_ShutdownEntities.
 */
void
EntityManager_ShutdownArchive(void)
{
	int i;
	CItem *entity, *next;

	for (i = 0; i < 0x4000; i++) {
		for (entity = g_ArchiveHash[i]; entity != NULL; entity = next) {
			next = entity->hashNext;
			CEntity_RemoveAllTimers(entity);
			CItem_ClearScriptsAndTags(entity);
			CItem_ReleaseTracking(entity);
		}
	}
}
