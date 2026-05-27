/*
 * CTimerManager - per-entity countdown timers driven from the main tick.
 *
 * Every CEntity owns a singly-linked chain of timer nodes keyed by
 * eventType. On each world tick the manager decrements remaining
 * counts and fires expired nodes through a dispatch table so combat
 * flags, AI pauses, and script waits all expire in deterministic
 * order.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"
#include "npc.h"
#include "packet_handler.h"
#include "player.h"
#include "timer.h"
#include "vg_pool.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "wombat_exec.h"
#include "world.h"

static void TimerHandler_AIScheduleDelete(CItem *entity, int timerType, char *extraData); // 0x00435AE0
static void TimerHandler_SetStringDetach(CItem *entity, int timerType, char *extraData); // 0x00435B3B
static void TimerHandler_SetStringProp(CItem *entity, int timerType, char *extraData); // 0x00435B7B
static void TimerHandler_Criminal(CItem *entity, int timerType, char *extraData); // 0x00435B96
static void TimerHandler_SetChaos(CItem *entity, int timerType, char *extraData); // 0x00435BA7
static void TimerHandler_SetOrder(CItem *entity, int timerType, char *extraData); // 0x00435BBD
static void TimerHandler_ControllerTimeout(CItem *entity, int timerType, char *extraData); // 0x00435BD3
static void TimerHandler_Delete(CItem *entity, int timerType, char *extraData); // 0x00435C39
static void TimerHandler_OnlineCheck(CItem *entity, int timerType, char *extraData); // 0x00435C52
static void TimerHandler_NPCTick(CItem *entity, int timerType, char *extraData); // 0x00435C77
static void TimerHandler_DecayContainer(CItem *entity, int timerType, char *extraData); // 0x00435CA0
static void TimerHandler_IdleDisconnect(CItem *entity, int timerType, char *extraData); // 0x00435D5C
static void TimerHandler_DoorClose(CItem *entity, int timerType, char *extraData); // 0x00435D88
static void TimerHandler_Callback(CItem *entity, int timerType, char *extraData); // 0x00435D9D
static void TimerHandler_Speech(CItem *entity, int timerType, char *extraData); // 0x00435DB4
static void TimerHandler_ValuelessDecay(CItem *entity, int timerType, char *extraData); // 0x00435DD4
static void TimerHandler_WarModeClear(CItem *entity, int timerType, char *extraData); // 0x00435DF9
static void TimerHandler_Unsquelch(CItem *entity, int timerType, char *extraData); // 0x00435E40
static void TimerHandler_Unfreeze(CItem *entity, int timerType, char *extraData); // 0x00435E8B
static void TimerHandler_ClearInvuln(CItem *entity, int timerType, char *extraData); // 0x00435ED6
static int IsSerialInEntityList(uint32_t serial); // 0x00435F23
static TimerEntityNode *AddSerialToEntityList(uint32_t serial); // 0x00435F59
static void PruneEntityList(CVector *outList); // 0x00435FB4
static void FreeTimerNode(TimerNode *node); // 0x00436073
static TimerNode *AllocTimerNode(void); // 0x004360B7
static int CanSerializeTimer(int eventType); // 0x00436130
static void CEntity_AddTimer(CItem *entity, int countdown, int eventType, int timerType, intptr_t extraData); // 0x0043632E
static int CItem_ProcessEntityTimers(CItem *entity); // 0x004364E7
static void RemoveEntityNode(TimerEntityNode *node);

// Binary globals
static TimerNode *g_timerFreeList;        /* 0x0063F81C */
static TimerEntityNode *g_entityListHead; /* 0x0063F814 */
static TimerEntityNode *g_entityListTail; /* 0x0063F818 */

// Timer dispatch handler signature (binary: void handler(CItem*, int, char*))
typedef void (*TimerDispatchFn)(CItem *entity, int timerType, char *extraData);

// 0x00613C18 - timer dispatch table (21 entries indexed by eventType)
static TimerDispatchFn g_timerDispatchTable[21] = {
	NULL,             /*  0 */
	TimerHandler_Delete,            /*  1 */
	TimerHandler_DecayContainer,    /*  2 */
	TimerHandler_IdleDisconnect,    /*  3 */
	TimerHandler_DoorClose,         /*  4 */
	TimerHandler_Callback,          /*  5 */
	TimerHandler_AIScheduleDelete,  /*  6 */
	TimerHandler_Speech,            /*  7 */
	TimerHandler_ValuelessDecay,    /*  8 */
	TimerHandler_OnlineCheck,       /*  9 */
	TimerHandler_NPCTick,           /* 10 */
	TimerHandler_SetStringProp,     /* 11 */
	TimerHandler_WarModeClear,      /* 12 */
	TimerHandler_SetStringDetach,   /* 13 */
	TimerHandler_Unfreeze,          /* 14 */
	TimerHandler_Unsquelch,         /* 15 */
	TimerHandler_ClearInvuln,       /* 16 */
	TimerHandler_Criminal,          /* 17 */
	TimerHandler_SetChaos,          /* 18 */
	TimerHandler_SetOrder,          /* 19 */
	TimerHandler_ControllerTimeout, /* 20 */
};
#define TIMER_DISPATCH_TABLE_SIZE 21

/*
 * 0x00435AE0 - TimerHandler_AIScheduleDelete
 *
 * Clears the aiScheduleTimer field on the player identified by
 * timerType, then deletes the entity.
 */
static void
TimerHandler_AIScheduleDelete(CItem *entity, int timerType, char *extraData)
{
	CItem *found;

	USED(extraData);
	found = CWorld_FindBySerial(g_World, (uint32_t)timerType);
	if (found != NULL) {
		if (VT_IsPlayer(found)) {
			if (((CPlayer *)found)->aiScheduleTimer != 0)
				((CPlayer *)found)->aiScheduleTimer = 0;
		}
	}
	if (entity != NULL)
		((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
}

/*
 * 0x00435B3B - TimerHandler_SetStringDetach
 *
 * Applies the string prop stored in extraData, then detaches the
 * entity from the spatial grid and drops it at its current location.
 */
static void
TimerHandler_SetStringDetach(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	if (extraData == NULL)
		return;
	if (entity == NULL)
		return;
	TriggerEdit_SetStringProp(entity, extraData);
	((void (*)(void *))VT_FN(entity, VT_DETACH_SPATIAL))(entity);
	((void (*)(void *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, &entity->resourceEntity.entity.location);
}

/*
 * 0x00435B7B - TimerHandler_SetStringProp
 *
 * Applies the string prop stored in extraData to the entity.
 */
static void
TimerHandler_SetStringProp(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	if (extraData != NULL)
		TriggerEdit_SetStringProp(entity, extraData);
}

/*
 * 0x00435B96 - TimerHandler_Criminal
 *
 * Forwards to CEntity_ProcessNotorietyTimer.
 */
static void
TimerHandler_Criminal(CItem *entity, int timerType, char *extraData)
{
	USED(extraData);
	CEntity_ProcessNotorietyTimer(entity, timerType);
}

/*
 * 0x00435BA7 - TimerHandler_SetChaos
 *
 * Sets the "chaos" string prop on the entity.
 */
static void
TimerHandler_SetChaos(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	TriggerEdit_SetStringProp(entity, "chaos");
}

/*
 * 0x00435BBD - TimerHandler_SetOrder
 *
 * Sets the "order" string prop on the entity.
 */
static void
TimerHandler_SetOrder(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	TriggerEdit_SetStringProp(entity, "order");
}

/*
 * 0x00435BD3 - TimerHandler_ControllerTimeout
 *
 * When the controllerTimeout tag is present, detaches all controller-
 * related scripts from the entity.
 */
static void
TimerHandler_ControllerTimeout(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	if (!CResourceEntity_HasTag(entity, "controllerTimeout", 0))
		return;
	CResourceEntity_DetachScript(entity, "controller");
	CResourceEntity_DetachScript(entity, "controllerName");
	CResourceEntity_DetachScript(entity, "controllerGuild");
	CResourceEntity_DetachScript(entity, "controllerGuildType");
	CResourceEntity_DetachScript(entity, "controllerTimeout");
	CResourceEntity_DetachScript(entity, "defensive");
}

/*
 * 0x00435C39 - TimerHandler_Delete
 *
 * Deletes the entity.
 */
static void
TimerHandler_Delete(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	if (entity != NULL)
		((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
}

/*
 * 0x00435C52 - TimerHandler_OnlineCheck
 *
 * Probes whether the player is still online.
 */
static void
TimerHandler_OnlineCheck(CItem *entity, int timerType, char *extraData)
{
	CPlayer *player;

	USED(timerType);
	USED(extraData);
	if (!VT_IsPlayer(entity))
		return;
	player = (CPlayer *)entity;
	CPlayer_IsPlayerOnline(player);
}

/*
 * 0x00435C77 - TimerHandler_NPCTick
 *
 * Calls Noop_4D7D1A when the entity is a player. The callee is an
 * empty function, so this handler is effectively dead code.
 */
static void
TimerHandler_NPCTick(CItem *entity, int timerType, char *extraData)
{
	CPlayer *player;

	USED(timerType);
	USED(extraData);
	if (!VT_IsPlayer(entity))
		return;
	player = (CPlayer *)entity;
	Noop_4D7D1A(player);
}

/*
 * 0x00435CA0 - TimerHandler_DecayContainer
 *
 * Corpse/container decay. For a mobile corpse, moves its non-hair
 * contents to the corpse location and deletes hair items, then
 * deletes the corpse itself.
 */
static void
TimerHandler_DecayContainer(CItem *entity, int timerType, char *extraData)
{
	CItem *child, *next;
	CLocation savedLoc;
	CItem *mob;

	USED(timerType);
	USED(extraData);
	if (!((int (*)(void *))VT_FN(entity, VT_HAS_CORPSE_EQ))(entity))
		return;
	if (!VT_IsMobile2(entity))
		goto final_delete;
	mob = entity;
	CLocation_SetLoc(&savedLoc, ((CLocation * (*)(void *)) VT_FN(mob, VT_GET_LOCATION))(mob));
	child = ((CContainer *)mob)->contents;
	while (child != NULL) {
		next = child->spatialNext;
		if (VT_IsHair(child)) {
			if (child != NULL)
				((void (*)(void *))VT_FN(child, VT_DELETE))(child);
		} else {
			((void (*)(void *, CLocation *))VT_FN(child, VT_MOVE_TO))(child, &savedLoc);
		}
		child = next;
	}
final_delete:
	if (entity != NULL)
		((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
}

/*
 * 0x00435D5C - TimerHandler_IdleDisconnect
 *
 * Disconnects an idle player unless the pflags idle-exempt bit is set.
 */
static void
TimerHandler_IdleDisconnect(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	if (!VT_IsPlayer(entity))
		return;
	if (((CPlayer *)entity)->pflags & 4)
		return;
	CPlayer_Disconnect((CPlayer *)entity);
}

/*
 * 0x00435D88 - TimerHandler_DoorClose
 *
 * Closes a door that was opened earlier.
 */
static void
TimerHandler_DoorClose(CItem *entity, int timerType, char *extraData)
{
	USED(extraData);
	DoorClose(entity, timerType);
}

/*
 * 0x00435D9D - TimerHandler_Callback
 *
 * Fires script event 0x21 on the entity with the stored timerType.
 */
static void
TimerHandler_Callback(CItem *entity, int timerType, char *extraData)
{
	USED(extraData);
	Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x21, timerType);
}

/*
 * 0x00435DB4 - TimerHandler_Speech
 *
 * Emotes the string stored in extraData using default color/font/lang.
 */
static void
TimerHandler_Speech(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	if (extraData != NULL)
		((void (*)(void *, const char *, int, int, int))VT_FN(entity, VT_EMOTE_CSTRING))(entity, extraData, -1, -1, -1);
}

/*
 * 0x00435DD4 - TimerHandler_ValuelessDecay
 *
 * Deletes the entity only if it is currently valueless.
 */
static void
TimerHandler_ValuelessDecay(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	if (!CItem_IsValueless(entity))
		return;
	if (entity != NULL)
		((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
}

/*
 * 0x00435DF9 - TimerHandler_WarModeClear
 *
 * Turns off war mode on a dead player that still has the war-mode
 * mobile flag set.
 */
static void
TimerHandler_WarModeClear(CItem *entity, int timerType, char *extraData)
{
	CPlayer *player;

	USED(timerType);
	USED(extraData);
	if (!VT_IsPlayer(entity))
		return;
	player = (CPlayer *)entity;
	if (!VT_IsDead(entity))
		return;
	if (!CMobile_CheckMobileFlag((CMobile *)player, 0x40))
		return;
	CPlayer_SetWarModeBroadcast(player, 0);
}

/*
 * 0x00435E40 - TimerHandler_Unsquelch
 *
 * Clears the squelch status on a mobile and notifies the player.
 */
static void
TimerHandler_Unsquelch(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	if (!VT_IsMobile(entity))
		return;
	if (!CMobile_IsSquelched((CMobile *)entity))
		return;
	CMobile_SetStatusFlag((CMobile *)entity, 4, 0);
	if (!VT_IsPlayer(entity))
		return;
	CPlayer_SystemMessage((CPlayer *)entity, "You are no longer squelched.");
}

/*
 * 0x00435E8B - TimerHandler_Unfreeze
 *
 * Clears the frozen status flag and notifies the player.
 */
static void
TimerHandler_Unfreeze(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	if (!VT_IsMobile(entity))
		return;
	if (!CPlayer_IsGhost((CPlayer *)entity))
		return;
	CMobile_SetStatusFlag((CMobile *)entity, 2, 0);
	if (!VT_IsPlayer(entity))
		return;
	CPlayer_SystemMessage((CPlayer *)entity, "You are no longer frozen.");
}

/*
 * 0x00435ED6 - TimerHandler_ClearInvuln
 *
 * Clears invulnerability on a mobile and notifies the player.
 */
static void
TimerHandler_ClearInvuln(CItem *entity, int timerType, char *extraData)
{
	USED(timerType);
	USED(extraData);
	if (!VT_IsMobile(entity))
		return;
	if (!CMobile_IsInvulnerable((CMobile *)entity))
		return;
	((void (*)(void *))VT_FN(entity, VT_CLR_INVULN))(entity);
	if (!VT_IsPlayer(entity))
		return;
	CPlayer_SystemMessage((CPlayer *)entity, "You are no longer invulnerable.");
}

/*
 * 0x00435F23 - IsSerialInEntityList
 *
 * Returns 1 if the serial is present in the entity tracking list.
 */
static int
IsSerialInEntityList(uint32_t serial)
{
	TimerEntityNode *cur;

	for (cur = g_entityListHead; cur != NULL; cur = cur->next) {
		if (cur->serial == serial)
			return 1;
	}
	return 0;
}

/*
 * 0x00435F59 - AddSerialToEntityList
 *
 * Appends a new node for the serial to the tail of the entity
 * tracking list.
 */
static TimerEntityNode *
AddSerialToEntityList(uint32_t serial)
{
	TimerEntityNode *node;

	node = malloc(sizeof(TimerEntityNode));
	node->prev = g_entityListTail;
	if (node->prev != NULL)
		node->prev->next = node;
	else
		g_entityListHead = node;
	g_entityListTail = node;
	node->next = NULL;
	node->serial = serial;
	return node;
}

/*
 * 0x00435FB4 - PruneEntityList
 *
 * Walks the entity tracking list: pushes serials of entities that
 * still exist and have timers into outList, and removes dead or
 * timer-less entries from the tracking list.
 */
static void
PruneEntityList(CVector *outList)
{
	TimerEntityNode *cur, *next;
	CItem *entity;

	for (cur = g_entityListHead; cur != NULL; cur = next) {
		next = cur->next;
		entity = CWorld_FindBySerial(g_World, cur->serial);
		if (entity != NULL && entity->timerHead != NULL) {
			CVector_PushBack(outList, cur->serial);
		} else {
			RemoveEntityNode(cur);
		}
	}
}

/*
 * 0x00436073 - FreeTimerNode
 *
 * Releases a timer node back to the free list after freeing its
 * extraData string.
 */
static void
FreeTimerNode(TimerNode *node)
{
	if (node->extraData != NULL) {
		free(node->extraData);
		node->extraData = NULL;
	}
	node->next = g_timerFreeList;
	g_timerFreeList = node;
	VG_POOL_FREE(&g_timerFreeList, node);
}

/*
 * 0x004360B7 - AllocTimerNode
 *
 * Pops a timer node from the free list, allocating a fresh block of
 * 0x400 nodes when the free list is empty.
 */
static TimerNode *
AllocTimerNode(void)
{
	static int poolCreated;
	TimerNode *node;
	TimerNode *block;
	int i;

	if (!poolCreated) {
		VG_CREATE_POOL(&g_timerFreeList);
		poolCreated = 1;
	}

	if (g_timerFreeList != NULL) {
		node = g_timerFreeList;
		VG_POOL_ALLOC(&g_timerFreeList, node, sizeof(TimerNode));
		VG_MAKE_DEFINED(&node->next, sizeof(node->next));
		g_timerFreeList = node->next;
		return node;
	}
	block = malloc(0x400 * sizeof(TimerNode));
	for (i = 0x3FF; i >= 1; i--) {
		block[i].next = g_timerFreeList;
		g_timerFreeList = &block[i];
	}
	VG_POOL_ALLOC(&g_timerFreeList, &block[0], sizeof(TimerNode));
	return &block[0];
}

/*
 * 0x00436130 - CanSerializeTimer
 *
 * Returns 1 if the timer event type should be saved. Event types 7,
 * 10 and 11 are transient internal events and are skipped.
 */
static int
CanSerializeTimer(int eventType)
{
	if (eventType == 7)
		return 0;
	if (eventType <= 9)
		return 1;
	if (eventType <= 11)
		return 0;
	return 1;
}

/*
 * 0x0043615B - CEntity::SerializeCallbacks
 *
 * Serializes the entity's serializable timers into buf as
 * "callback=<event> <type> <countdown> ... 0". Returns buf on success
 * or NULL if no timers were emitted.
 */
char *
CEntity_SaveTimers(CItem *entity, char *buf, int bufSize)
{
	TimerNode *t;
	int totalLen;
	int found;
	char temp[128];
	int len;

	USED(bufSize);
	if (entity->timerHead == NULL)
		return 0;

	totalLen = 9;
	found = 0;
	strcpy(buf, "callback=");

	for (t = entity->timerHead; t != NULL; t = t->next) {
		if (!CanSerializeTimer(t->eventType))
			continue;
		found = 1;
		sprintf(temp, "%d %d %d ", t->eventType, t->timerType, t->countdown);
		len = strlen(temp);
		strcat(buf, temp);
		totalLen += len;
	}
	USED(totalLen);
	if (!found)
		return 0;
	strcat(buf, "0");
	return buf;
}

/*
 * 0x00436261 - CEntity::LoadCallbacks
 *
 * Parses the "callback=" value as triplets (eventType, timerType,
 * countdown) terminated by a zero eventType, and re-adds each timer
 * via CEntity_AddTimer.
 */
void
CEntity_LoadTimers(CItem *entity, const char *val)
{
	const char *p;
	int eventType, timerType, countdown;

	p = val;
	eventType = atoi(p);
	while (eventType != 0) {
		while (*p != ' ')
			p++;
		p++;
		timerType = atoi(p);
		while (*p != ' ')
			p++;
		p++;
		countdown = atoi(p);
		while (*p != ' ')
			p++;
		p++;
		CEntity_AddTimer(entity, countdown, eventType, timerType, 0);
		eventType = atoi(p);
	}
}

/*
 * 0x0043632E - CEntity::AddTimer
 *
 * Allocates a timer node, copies extraData if present, and prepends
 * it to the entity's timer chain. Registers the entity in the global
 * tracking list the first time it gets a timer.
 */
static void
CEntity_AddTimer(CItem *entity, int countdown, int eventType, int timerType, intptr_t extraData)
{
	TimerNode *node;

	node = AllocTimerNode();
	node->eventType = eventType;
	node->timerType = timerType;
	node->countdown = countdown;
	if (extraData != 0) {
		const char *src = (const char *)extraData;
		size_t len = strlen(src);
		node->extraData = malloc(len + 1);
		strcpy(node->extraData, src);
	} else {
		node->extraData = NULL;
	}

	if (entity->timerHead == NULL) {
		if (!IsSerialInEntityList(entity->serial))
			AddSerialToEntityList(entity->serial);
	}
	node->next = entity->timerHead;
	entity->timerHead = node;
}

/*
 * 0x004363E2 - CEntity::RemoveTimer
 *
 * Removes every timer on the entity that matches (eventType,
 * timerType) and frees its node.
 */
void
CEntity_RemoveTimer(CItem *entity, int eventType, int timerType)
{
	TimerNode **pp, *t;

	pp = &entity->timerHead;
	while (*pp != NULL) {
		t = *pp;
		if (t->eventType == eventType && t->timerType == timerType) {
			*pp = t->next;
			FreeTimerNode(t);
		} else {
			pp = &t->next;
		}
	}
}

/*
 * 0x00436452 - CEntity::HasScheduledEvent
 *
 * Returns 1 if a timer matching (eventType, callbackId) is currently
 * scheduled on the entity.
 */
int
CEntity_HasTimerEx(CItem *entity, int eventType, int callbackId)
{
	TimerNode *t;

	for (t = entity->timerHead; t != NULL; t = t->next) {
		if (t->eventType == eventType && t->timerType == callbackId)
			return 1;
	}
	return 0;
}

/*
 * 0x004364AE - CEntity::RemoveAllTimers
 *
 * Frees every timer node on the entity's timer chain.
 */
void
CEntity_RemoveAllTimers(CItem *entity)
{
	TimerNode *t;

	while (entity->timerHead != NULL) {
		t = entity->timerHead;
		entity->timerHead = t->next;
		FreeTimerNode(t);
	}
}

/*
 * 0x004364E7 - CItem::ProcessEntityTimers
 *
 * Decrements each of the entity's timers and dispatches any that
 * expired via g_timerDispatchTable. Re-validates the entity between
 * dispatches and bails out if it is deleted. Returns the number of
 * timers fired.
 *
 * FIXED: binary passes an uninitialized stack variable as the type
 * flag to CVector_Constructor, which dereferences it. Initialize to 0
 * matching the CVector_ConstructorWrapper pattern at 0x00479FD0.
 */
static int
CItem_ProcessEntityTimers(CItem *entity)
{
	CVector vec;
	char typeByte = 0;
	TimerNode **pp, *t;
	uint32_t serial;
	uintptr_t *ptr;
	int count;

	if (entity->timerHead == NULL)
		return 0;
	if (entity->resourceEntity.entity.removedFromWorld)
		return 0;

	serial = entity->serial;

	CVector_Constructor(&vec, &typeByte);

	pp = &entity->timerHead;
	while (*pp != NULL) {
		t = *pp;
		if (t->countdown > 0) {
			t->countdown--;
			pp = &t->next;
		} else {
			*pp = t->next;
			CVector_PushBack(&vec, (uintptr_t)t);
		}
	}

	ptr = (uintptr_t *)vec.begin;
	while (ptr != (uintptr_t *)vec.end) {
		t = (TimerNode *)*ptr;

		g_timerDispatchTable[t->eventType](entity, t->timerType, t->extraData);

		// Handler may have deleted the entity; if so, free remaining
		// nodes without firing them.
		if (CWorld_FindBySerial(g_World, serial) != entity) {
			ptr++;
			while (ptr != (uintptr_t *)vec.end) {
				t = (TimerNode *)*ptr;
				FreeTimerNode(t);
				ptr++;
			}
			CVector_Destructor(&vec);
			return 0;
		}

		FreeTimerNode(t);
		ptr++;
	}

	count = CVector_GetCount(&vec);
	CVector_Destructor(&vec);
	return count;
}

/*
 * 0x00436688 - ScheduleEvent (CTimerManager::AddTimer)
 *
 * Replaces any existing timer with the same (eventType, timerType)
 * pair on the entity identified by serial and schedules a new one.
 */
void
ScheduleEvent(int delay, uint32_t serial, int eventType, int timerType, intptr_t extraData)
{
	CItem *entity;

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		return;

	CEntity_RemoveTimer(entity, eventType, timerType);
	CEntity_AddTimer(entity, delay, eventType, timerType, extraData);
}

/*
 * 0x004366CF - TimerManager_ProcessTimers
 *
 * Takes a snapshot of live, timered entity serials via
 * PruneEntityList and processes each one's timers. The snapshot
 * makes iteration safe against reentrant entity deletion.
 *
 * FIXED: binary passes an uninitialized stack variable as the type
 * flag to CVector_Constructor, which dereferences it. Initialize to 0
 * matching the CVector_ConstructorWrapper pattern at 0x00479FD0.
 */
void
TimerManager_ProcessTimers(void)
{
	CVector list;
	char typeByte = 0;
	uintptr_t *p;
	CItem *entity;

	CVector_Constructor(&list, &typeByte);
	PruneEntityList(&list);

	for (p = (uintptr_t *)list.begin; p != (uintptr_t *)list.end; p++) {
		entity = CWorld_FindBySerial(g_World, (uint32_t)*p);
		if (entity != NULL)
			CItem_ProcessEntityTimers(entity);
	}

	CVector_Destructor(&list);
}

/*
 * 0x0043676C - CTimerManager::EnsureTracked
 *
 * Adds the entity's serial to the tracking list if not already
 * present. Called from CEntity_AddToWorld when the entity has timers.
 */
void
CTimerManager_EnsureTracked(CItem *entity)
{
	if (IsSerialInEntityList(entity->serial))
		return;
	AddSerialToEntityList(entity->serial);
}

/*
 * Helper - RemoveEntityNode
 *
 * Unlinks and frees an entity tracking node. The binary inlines this
 * inside PruneEntityList (0x00435FB4).
 */
static void
RemoveEntityNode(TimerEntityNode *node)
{
	if (node->next != NULL)
		node->next->prev = node->prev;
	else
		g_entityListTail = node->prev;
	if (node->prev != NULL)
		node->prev->next = node->next;
	else
		g_entityListHead = node->next;
	free(node);
}

/*
 * Custom - Timer_DestroyPools
 *
 * Server-shutdown cleanup. Walks the timer-node freelist marking
 * each node defined so valgrind can read its next pointer, then
 * ends pool tracking. The VG_* macros are no-ops when VALGRIND is
 * not defined.
 */
void
Timer_DestroyPools(void)
{
	TimerNode *cur, *next;

	if (g_timerFreeList == NULL)
		return;
	for (cur = g_timerFreeList; cur != NULL; cur = next) {
		VG_MAKE_DEFINED(cur, sizeof(*cur));
		next = cur->next;
	}
	VG_DESTROY_POOL(&g_timerFreeList);
}
