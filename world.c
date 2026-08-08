/*
 * CWorld - top-level world state and save cycle.
 *
 * Owns the entity registry, tick counter, decay scanner, and save
 * scheduler. Coordinates startup ordering (terrain, regions, spawns,
 * dynamic) and orchestrates periodic world saves through the dynamic
 * save pipeline.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "bboard.h"
#include "container.h"
#include "main.h"
#include "multi.h"
#include "npc.h"
#include "player.h"
#include "signpost.h"
#include "skill.h"
#include "item.h"
#include "stddeque.h"
#include "timer.h"
#include "vtable.h"
#include "weapon.h"
#include "world.h"

static void CWorld_ScanAndDecay(void); // 0x0045971D
static void Noop_4851F0(void); // 0x004851F0
static void Noop_4851F5(void); // 0x004851F5
static uint32_t AllocNextSerial(void); // 0x00485248
static void CDataManager_InitMapBuffers(CWorld *this); // 0x0048EE03
static void CDataManager_InitAnimBuffer(CWorld *this); // 0x0048EEB2
static void CDataManager_InitTileBuffer(CWorld *this); // 0x0048EF1A
static void CDataManager_InitArtBuffer(CWorld *this); // 0x0048F05A

CWorld *g_World;
TileDataEntry *g_ItemTileData;
int g_WorldActive = 1; // 0x0061DA80: 1 during normal operation, 0 during bulk delete
int g_WorldActive2 = 1; // 0x0061DA84: 1 during normal operation, 0 during bulk delete
int g_DeleteAllowed; // 0x00645B04: 1 when entity deletion is enabled
int g_isLoadingWorld; // 0x00699B64: 1 during world load (SendAppearance skips terrain)
int g_hasLoadedWorld; // 0x00699B68: 1 after world load complete

// 0x006933AC - CWorld+0x4805C: current hash bucket
static uint32_t g_DecayScanPos;
// 0x006933B0 - CWorld+0x48060: ticks between scans (signed, binary uses idiv)
static int g_DecayInterval;
// 0x006933B4 - CWorld+0x48064: buckets per invocation
static uint32_t g_DecayBucketsPerTick;
// 0x006933B8 - CWorld+0x48068: deletion threshold
static uint8_t g_DecayMax;
// 0x006933B9 - CWorld+0x48069: home decay rate
static uint8_t g_DecayHomeRate;
// 0x006933BA - CWorld+0x4806A: per-scan increment
static uint8_t g_DecayIncrement;

// Gump dimension table (binary: 0x006F0618).
// Indexed by bodyType, gives container gump dimensions.
GumpDimEntry g_GumpDimTable[TILEDATA_MAX_ITEMS];

/*
 * 0x00421240 - CWorld::GetHomeDecayRate
 *
 * Returns the decay increment applied to items resting at their
 * home location.
 */
uint8_t
CWorld_GetHomeDecayRate(void)
{
	return g_DecayHomeRate;
}

/*
 * 0x00421260 - CWorld::GetNonHomeDecayRate
 *
 * Returns the decay increment applied to items away from their home
 * location.
 */
uint8_t
CWorld_GetNonHomeDecayRate(void)
{
	return g_DecayIncrement;
}

/*
 * 0x00421280 - CWorld::GetDecayMax
 *
 * Returns the decay-counter cap; items reaching this value are
 * removed.
 */
uint8_t
CWorld_GetDecayMax(void)
{
	return g_DecayMax;
}

/*
 * 0x004212A0 - CWorld::GetDecayInterval
 *
 * Returns the tick interval between decay scans (0 disables decay).
 */
uint32_t
CWorld_GetDecayInterval(void)
{
	return g_DecayInterval;
}

/*
 * 0x004212C0 - CWorld::GetDecayBucketsPerTick
 *
 * Returns the number of hash buckets the decay scanner visits per
 * tick.
 */
uint32_t
CWorld_GetDecayBucketsPerTick(void)
{
	return g_DecayBucketsPerTick;
}
/*
 * 0x0045960F - CWorld::InitDecay
 *
 * Resets scanPos to 0, then sets decay parameters by mode:
 *   mode 0 (debug): interval=1, buckets=4, homeRate=0x70, inc=0x70, max=0xFA
 *   mode 1 (normal): interval=1, buckets=0x6D, max=0x48, homeRate=0x12, inc=1
 *   else (disabled): interval=0, buckets=0, max=0xFF, homeRate=0, inc=0
 *
 * MODIFIED: mode 1 tuned to match uo.stratics.com decay timings.
 * The binary's mode 1 used buckets=0x800 (2048) and inc=6, giving a
 * ground-item lifetime of 12 visits * 8s = 96s - the demo compressed
 * retail's 3-hour ground decay into 96 seconds for test play. Retail
 * documents 3 hours. We restore that by slowing the scan to 0x6D (109)
 * buckets/tick (cycle = 65536/109 * 0.25s ~= 150s) and dropping the
 * increment to 1, giving 72 visits * 150s ~= 10800s = 3 hours.
 *
 * Mode 0 (debug) and the disabled default are exact matches to the
 * binary. Mode 3 (fast test) is custom: items decay in ~2-3 seconds
 * (buckets=0x4000 gives ~1s cycle, max=3 with inc=1 = 3 visits).
 */
void
CWorld_InitDecay(int mode)
{
	g_DecayScanPos = 0;
	switch (mode) {
	case 0:
		g_DecayInterval = 1;
		g_DecayBucketsPerTick = 4;
		g_DecayHomeRate = 0x70;
		g_DecayIncrement = 0x70;
		g_DecayMax = 0xFA;
		break;
	case 1:
		g_DecayInterval = 1;
		g_DecayBucketsPerTick = 0x6D;
		g_DecayMax = 0x48;
		g_DecayHomeRate = 0x12;
		g_DecayIncrement = 1;
		break;
	case 3:
		g_DecayInterval = 1;
		g_DecayBucketsPerTick = 0x4000;
		g_DecayMax = 3;
		g_DecayHomeRate = 1;
		g_DecayIncrement = 1;
		break;
	default:
		g_DecayInterval = 0;
		g_DecayBucketsPerTick = 0;
		g_DecayMax = 0xFF;
		g_DecayHomeRate = 0;
		g_DecayIncrement = 0;
		break;
	}
}

/*
 * 0x004596EB - CWorld::DecayTick
 *
 * Drives the decay scanner once gameTick is divisible by the decay
 * interval. No-op when decay is disabled.
 */
void
CWorld_DecayTick(int gameTick)
{
	if (g_DecayInterval <= 0)
		return;
	if (gameTick % g_DecayInterval != 0)
		return;
	CWorld_ScanAndDecay();
}

/*
 * 0x0045971D - CWorld::ScanAndDecay
 *
 * Walks g_DecayBucketsPerTick hash buckets starting at the saved
 * scan position, calling each entity's DecayTick. Wraps at 0xFFFF.
 */
static void
CWorld_ScanAndDecay(void)
{
	uint32_t i;
	CItem *item;
	CVector serials;
	uintptr_t *ptr;

	for (i = 0; i < g_DecayBucketsPerTick; i++) {
		CVector_Constructor(&serials, "\x01");
		item = g_World->hashTable[g_DecayScanPos];
		while (item != NULL) {
			CVector_PushBack(&serials, item->serial);
			item = item->hashNext;
		}

		ptr = (uintptr_t *)serials.begin;
		while (ptr != (uintptr_t *)serials.end) {
			item = CWorld_FindBySerial(g_World, (uint32_t)*ptr);
			if (item != NULL)
				((void (*)(void *))VT_FN(item, VT_DECAY_TICK))(item);
			ptr++;
		}

		CVector_Destructor(&serials);
		g_DecayScanPos = (g_DecayScanPos + 1) & 0xFFFF;
	}
}

/*
 * 0x00485160 - World_IsEntityInHash
 *
 * Returns 1 when item is reachable from its serial bucket in the global
 * world hash table.
 */
int
World_IsEntityInHash(CItem *item)
{
	CItem *node;

	if (item == NULL)
		return 0;

	node = g_World->hashTable[item->serial & 0xFFFF];
	while (node != NULL) {
		if (node == item)
			return 1;
		node = node->hashNext;
	}
	return 0;
}

/*
 * 0x004851AC - World_PreloadPlayerSerial
 *
 * No-op stub invoked while loading player entities from disk.
 */
void
World_PreloadPlayerSerial(uint32_t serial)
{
	USED(serial);
}

/*
 * 0x004851B1 - World_FindBySerial_Either
 *
 * Returns 1 when serial resolves to an entity, retrying with the
 * item/mobile range bit (0x40000000) flipped.
 */
int
World_FindBySerial_Either(uint32_t serial)
{
	if (CWorld_FindBySerial(g_World, serial) != NULL)
		return 1;
	if (CWorld_FindBySerial(g_World, serial ^ 0x40000000) != NULL)
		return 1;
	return 0;
}

/*
 * 0x004851F0 - (unknown, no-op)
 *
 * Empty function. Called from CItem_Delete on the non-shutdown
 * path with no observable effect.
 */
static __attribute__((unused)) void
Noop_4851F0(void)
{
}

/*
 * 0x004851F5 - (unknown, no-op)
 *
 * Empty function with no callers, sitting next to Noop_4851F0.
 */
static __attribute__((unused)) void
Noop_4851F5(void)
{
}

/*
 * 0x00485248 - AllocNextSerial
 *
 * Reads g_World.nextSerial, increments, returns old value (post-increment).
 * Binary loads from global 0x0064B350 (CWorld+0x00).
 */
static uint32_t
AllocNextSerial(void)
{
	return g_World->nextSerial++;
}

/*
 * 0x0048527C - CItem::SetSerialField
 *
 * Cdecl, 2 args (CItem*, uint32_t). Directly sets item->serial (offset 0x40).
 */
void
CItem_SetSerialField(CItem *item, uint32_t serial)
{
	item->serial = serial;
}

/*
 * 0x0048528A - CWorld::GetItemTileQuantity
 *
 * Returns the tiledata quantity byte for body type id. Used by
 * CMultiManager_CanExistAt for multi component height checks.
 */
uint8_t
CWorld_GetItemTileQuantity(uint16_t id)
{
	return g_ItemTileData[id & 0xFFFF].quantity;
}

/*
 * 0x004852B0 - CWorld::GetResourceWeight
 *
 * Returns the tiledata weight byte for bodyType. The caller must
 * ensure bodyType is in range; no bounds check is performed.
 */
uint8_t
CWorld_GetResourceWeight(uint16_t bodyType)
{
	return g_ItemTileData[bodyType].weight;
}

/*
 * 0x0048B65C - CDataManager::SetLockdown
 *
 * Thiscall forwarder: calls CItem_SetLockdown on the this pointer.
 */
void
CDataManager_SetLockdown(CItem *this, int set)
{
	CItem_SetLockdown(this, set);
}

/*
 * 0x0048B675 - CDataManager::Init
 *
 * Thiscall on CDataManager (g_World at 0x0064B350, ~296KB struct).
 * Initializes the massive data manager:
 *   1. Zeros buffer pointers at +0x0004806C..+0x00048080 (6 dwords)
 *   2. Sets magic at +0x00 = 0x40000001, counter at +0x04 = 0
 *   3. Zeros hash table at +0x24 (0x00010000 entries)
 *   4. Zeros fields at +0x00040024..+0x00040054 (various data fields)
 *   5. Calls InitMapBuffers, InitAnimBuffer, InitTileBuffer, InitArtBuffer
 *   6. Checks DECAY_TEST env var (dead code - result overwritten by 2)
 *   7. Calls CWorld_InitDecay(2)
 *   8. Zeros two 0x1000-entry arrays at +0x00040058 and +0x00044058
 *
 * CWorld struct covers the full CDataManager layout to ~0x00048084.
 */
void
CDataManager_Init(CWorld *this)
{
	int i;
	char *envVal;

	// 1. Zero critical section / arena page pointers
	this->critSectTileData1 = NULL;
	this->critSectTileData2 = NULL;
	this->critSectAnimData = NULL;
	this->critSectHueData1 = NULL;
	this->critSectHueData2 = NULL;
	this->_pad_48080 = 0;

	// 2. Magic and counter
	this->nextSerial = 0x40000001;
	this->ready = 0;

	// 3. Zero hash table (0x00010000 entries at +0x24)
	for (i = 0; i < 0x10000; i++)
		this->hashTable[i] = NULL;

	// 4. Zero entity counters
	this->entityCount = 0;
	this->staticItemCount = 0;
	this->dynamicItemCount = 0;
	this->resourceEntityCount = 0;
	this->bboardCount = 0;
	this->signpostCount = 0;
	this->containerCount = 0;
	this->mobileCount = 0;
	this->npcCount = 0;
	this->playerCreateCount = 0;
	this->npcSubCount1 = 0;
	this->npcSubCount2 = 0;
	this->normalNPCCount = 0;

	// 5. Init buffers
	CDataManager_InitMapBuffers(this);
	CDataManager_InitAnimBuffer(this);
	CDataManager_InitTileBuffer(this);
	CDataManager_InitArtBuffer(this);

	// Connect CWorld fields to global data pointers (binary: these are
	// the same memory since globals alias CWorld struct offsets)
	g_ItemTileData = (TileDataEntry *)this->itemTileData;
	g_AnimData = (AnimDataEntry *)this->animData;
	g_HueData = (HueEntry *)this->hueData;
	g_HueDataExp = (HueEntryExpanded *)this->hueDataExp;

	// 6. DECAY_TEST env check (binary dead code: unconditionally overwrites)
	this->bboardHead = 0;
	envVal = getenv("DECAY_TEST");
	if (envVal != NULL) {
		// binary: strcasecmp with "on" - result ignored
		USED(envVal);
	}
	CWorld_InitDecay(2);

	// 8. Zero template chain arrays
	for (i = 0; i < 0x1000; i++) {
		this->templateChain[i] = 0;
		this->templateChainCount[i] = 0;
	}
}

/*
 * 0x0048B8B5 - CDataManager::SetReady
 *
 * Sets CDataManager.ready (offset +4) to 1.
 */
void
CDataManager_SetReady(CWorld *this)
{
	this->ready = 1;
}

/*
 * 0x0048B8CA - CWorld::FindBySerial
 *
 * Hash table lookup: serial & 0xFFFF as bucket index
 * Walks chain via CItem.hashNext (offset 0x2C)
 */
CItem *
CWorld_FindBySerial(CWorld *world, uint32_t serial)
{
	uint16_t bucket;
	CItem *ent;

	bucket = serial & 0xFFFF;
	ent = world->hashTable[bucket];
	while (ent != NULL) {
		if (ent->serial == serial)
			return ent;
		ent = ent->hashNext;
	}
	return NULL;
}

/*
 * 0x0048B910 - CWorld::FindNPCBySerial
 *
 * Calls FindBySerial then checks IsNPC via vtable[0xE4] dispatch.
 * Returns the entity if found and IsNPC returns true, NULL otherwise.
 */
CItem *
CWorld_FindNPCBySerial(CWorld *world, uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(world, serial);
	if (ent == NULL)
		return NULL;
	if (!VT_IsNPC(ent))
		return NULL;
	return ent;
}

/*
 * 0x0048B94D - CWorld::FindMobileBySerial
 *
 * Calls FindBySerial then checks IsMobile via vtable[0xD0] dispatch.
 * Returns the entity if found and IsMobile returns true, NULL otherwise.
 */
CItem *
CWorld_FindMobileBySerial(CWorld *world, uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(world, serial);
	if (ent == NULL)
		return NULL;
	if (!VT_IsMobile(ent))
		return NULL;
	return ent;
}

/*
 * 0x0048B98A - CWorld method (orphaned noop)
 *
 * No-op. Zero callers and zero data references in the binary.
 */
static __attribute__((unused)) void
CWorld_Noop_48B98A(CWorld *world)
{
	USED(world);
}

/*
 * 0x0048B995 - CWorld method (orphaned DupId scanner)
 *
 * Walks every hash bucket and reports each pair of entities sharing a
 * serial as "DupId: N (TypeA-TypeB)" to the player, then reassigns the
 * duplicate's serial via AllocSerial. Sends "No Duplicate Id's found."
 * when none are detected.
 *
 * Zero callers and zero data references in the binary.
 */
static __attribute__((unused)) void
CWorld_FindDuplicateIds(CWorld *world, CPlayer *player)
{
	int found;
	int i;
	CItem *ent1;
	CItem *ent2;
	char msg[128];

	found = 0;
	for (i = 0; i < 0x10000; i++) {
		ent1 = world->hashTable[i];
		while (ent1 != NULL) {
			ent2 = ent1->hashNext;
			while (ent2 != NULL) {
				if (ent1->serial == ent2->serial) {
					sprintf(msg, "DupId: %d (", ent1->serial);
					// Type of ent1
					if (VT_IsPlayer(ent1))
						strcat(msg, "Player");
					else if (VT_IsNPC(ent1))
						strcat(msg, "NPC");
					else if (VT_IsMobile(ent1))
						strcat(msg, "Mobile");
					else if (VT_IsMobile2(ent1))
						strcat(msg, "Container");
					else if (((int (*)(void *))VT_FN(ent1, VT_ATTACH_SPATIAL))(ent1))
						strcat(msg, "Static");
					else
						strcat(msg, "Dynamic");
					strcat(msg, "-");
					// Type of ent2
					if (VT_IsPlayer(ent2))
						strcat(msg, "Player");
					else if (VT_IsNPC(ent2))
						strcat(msg, "NPC");
					else if (VT_IsMobile(ent2))
						strcat(msg, "Mobile");
					else if (VT_IsMobile2(ent2))
						strcat(msg, "Container");
					else if (((int (*)(void *))VT_FN(ent2, VT_ATTACH_SPATIAL))(ent2))
						strcat(msg, "Static");
					else
						strcat(msg, "Dynamic");
					strcat(msg, ")");
					CPlayer_SystemMessage(player, msg);
					found = 1;
					((void (*)(void *, uint32_t))VT_FN(ent2, VT_SET_SERIAL))(ent2, CWorld_AllocSerial(world));
				}
				ent2 = ent2->hashNext;
			}
			ent1 = ent1->hashNext;
		}
	}
	if (found == 0)
		CPlayer_SystemMessage(player, "No Duplicate Id's found.");
}

/*
 * 0x0048BCE7 - CWorld method (orphaned stub)
 *
 * Sends "Compression not yet implemented." to the player.
 *
 * Zero callers and zero data references in the binary.
 */
static __attribute__((unused)) void
CWorld_CompressionStub(CWorld *world, CPlayer *player)
{
	USED(world);
	CPlayer_SystemMessage(player, "Compression not yet implemented.");
}

/*
 * 0x0048BD01 - CWorld method (orphaned "set" command handler)
 *
 * Parses "set <target> <variable> <value>", resolves the target entity by
 * name/serial/"me", and assigns the value to the named field. Refreshes the
 * entity via Hide + ReturnToTracked when the assignment changed something
 * and sendUpdate is set, then logs to EventLogger as "godcommand"/"misc".
 *
 * Supported variables: hue, phue, location, naturalac/nac, naturalwc/nwc,
 * strength/s, dexterity/d, intelligence/i, strengthmod/sm, dexteritymod/dm,
 * intelligencemod/im, maxhp, curhp, maxmana, curmana, maxfatigue,
 * curfatigue, notoriety, fame, karma, changefame, changekarma, name, body,
 * hunger, skill, skillmod, attitude (NPC only).
 *
 * Zero callers and zero data references in the binary. The SCommand
 * dispatch table is BSS-zeroed and never populated, so this handler is
 * never registered or invoked.
 */
static __attribute__((unused)) void
CWorld_SCommand_Set(CWorld *world, CPlayer *player, const char *text)
{
	char valueBuf[256];
	char targetNameBuf[80];
	char varName[80];
	int targetNameLen;
	int changed;
	int sendUpdate;
	int intValue;
	const char *ptr;
	CItem *target;
	CMobile *mob;
	CLocation loc;
	int sscanfResult;
	int blockIdx;
	int zFromTerrain;
	CWeaponDice dice;
	char skillName[256];
	int skillNum;
	int16_t skillValue;
	char skillmodName[256];
	int skillmodNum;
	int16_t skillmodValue;
	CString logStr;
	CString notFoundStr;
	CNPC *npc;

	memset(valueBuf, 0, 255);
	targetNameLen = 0;
	changed = 0;
	sendUpdate = 1;

	// Look for target name in single-quotes first, then double-quotes
	ptr = strchr(text, '\'');
	if (ptr == NULL)
		ptr = strchr(text, '"');

	if (ptr != NULL) {
		// Extract quoted target name
		ptr++;
		while (ptr != NULL && *ptr != '\0' && *ptr != '\'' && *ptr != '"') {
			targetNameBuf[targetNameLen] = *ptr;
			targetNameLen++;
			ptr++;
		}
		ptr++;
		// Parse "variable value" after the closing quote
		sscanfResult = sscanf(ptr, "%s %[^\n]s", varName, valueBuf);
		if (sscanfResult < 2) {
			if (player != NULL) {
				CPlayer_SystemMessage(player, "Format: set <target> <variable>"
				                              " <value>");
				CPlayer_SystemMessage(player, "vars: strength, dexterity, "
				                              "intelligence, maxhp, curhp, "
				                              "maxmana, curmana, maxfatigue, "
				                              "curfatigue, hue, notoriety, "
				                              "fame, karma, name, body, skill,"
				                              " location, naturalac, naturalwc");
			}
			return;
		}
	} else {
		// No quotes - parse "target variable value" directly
		sscanfResult = sscanf(text, "%s %s %[^\n]s", targetNameBuf, varName, valueBuf);
		if (sscanfResult < 3) {
			if (player != NULL) {
				CPlayer_SystemMessage(player, "Format: set <target> <variable>"
				                              " <value>");
				CPlayer_SystemMessage(player, "vars: strength, dexterity, "
				                              "intelligence, maxhp, curhp, "
				                              "maxmana, curmana, maxfatigue, "
				                              "curfatigue, hue, notoriety, "
				                              "fame, karma, name, body, skill,"
				                              " location, naturalac, naturalwc");
			}
			return;
		}
	}

	// Parse the value as integer
	intValue = atoi(valueBuf);

	// Find target: try by name first
	// Binary calls CPlayer_FindByName (0x00450B20, static in player.c).
	target = (CItem *)FindPlayer(g_PlayerList.head, targetNameBuf);
	if (target != NULL) {
		if (target->resourceEntity.entity.removedFromWorld)
			target = NULL;
	}

	// Try by serial if name lookup failed
	if (target == NULL)
		target = CWorld_FindBySerial(world, (uint32_t)atoi(targetNameBuf));

	// Try "me" keyword
	if (target == NULL) {
		if (strcasecmp(targetNameBuf, "me") == 0)
			target = (CItem *)player;
	}

	if (target == NULL) {
		if (player != NULL)
			CPlayer_SystemMessage(player, "Target not found.");
		return;
	}

	// -- Item-level properties (hue, phue, location) --

	if (strcasecmp(varName, "hue") == 0) {
		target->resourceEntity.entity.color = (uint16_t)intValue;
		changed = 1;
	}

	if (strcasecmp(varName, "phue") == 0) {
		target->resourceEntity.entity.color = (uint16_t)(intValue | 0x8000);
		changed = 1;
	}

	if (strcasecmp(varName, "location") == 0) {
		CLocation_Init(&loc);
		sscanfResult = sscanf(valueBuf, "%hu %hu %hd", &loc.x, &loc.y, &loc.z);
		if (sscanfResult >= 2) {
			if (sscanfResult == 2) {
				if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int)loc.x, (int)loc.y)) {
					blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)loc.x, (int)loc.y, 0);
					zFromTerrain = (int)(int8_t)g_SpatialGrid.cells[blockIdx].pad00[(loc.y & 7) * 32 + (loc.x & 7) * 4 + 2];
					loc.z = (int16_t)zFromTerrain;
				}
			}
			changed = 1;
			sendUpdate = 0;
			if (CItem_HasMulti_Filter(target)) {
				TriggerEdit_MultiUpdate(target, &loc);
			} else {
				if (!((int (*)(void *, CLocation *))VT_FN(target, VT_TRANSFER_TO))(target, &loc)) {
					if (player != NULL)
						CPlayer_SystemMessage(player, "Invalid location");
				}
			}
		} else {
			if (player != NULL)
				CPlayer_SystemMessage(player, "Usage: set target location"
				                              " [x y|x y z]");
		}
	}

	// -- Mobile-level properties --

	if (!VT_IsMobile(target))
		goto post_mobile;

	mob = (CMobile *)target;

	if (strcasecmp(varName, "naturalac") == 0 || strcasecmp(varName, "nac") == 0) {
		CMobile_SetBonusAC(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "naturalwc") == 0 || strcasecmp(varName, "nwc") == 0) {
		CDiceRoll_InitParse(&dice, valueBuf);
		CMobile_SetArmorRating(mob, &dice);
		changed = 1;
	}

	if (strcasecmp(varName, "strength") == 0 || strcasecmp(varName, "s") == 0) {
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_BASE_STAT))(mob, 0, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "dexterity") == 0 || strcasecmp(varName, "d") == 0) {
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_BASE_STAT))(mob, 1, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "intelligence") == 0 || strcasecmp(varName, "i") == 0) {
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_BASE_STAT))(mob, 2, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "strengthmod") == 0 || strcasecmp(varName, "sm") == 0) {
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_STAT_BONUS))(mob, 0, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "dexteritymod") == 0 || strcasecmp(varName, "dm") == 0) {
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_STAT_BONUS))(mob, 1, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "intelligencemod") == 0 || strcasecmp(varName, "im") == 0) {
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_STAT_BONUS))(mob, 2, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "maxhp") == 0) {
		((void (*)(void *, int))VT_FN(target, VT_SET_MAX_HP))(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "curhp") == 0) {
		((void (*)(void *, int, int))VT_FN(target, VT_SET_HP))(mob, intValue, 0);
		changed = 1;
	}

	if (strcasecmp(varName, "maxmana") == 0) {
		((void (*)(void *, int))VT_FN(target, VT_SET_MAX_MANA))(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "curmana") == 0) {
		((void (*)(void *, int))VT_FN(target, VT_SET_MANA))(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "maxfatigue") == 0) {
		((void (*)(void *, int))VT_FN(target, VT_SET_MAX_STAMINA))(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "curfatigue") == 0) {
		((void (*)(void *, int))VT_FN(target, VT_SET_STAMINA))(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "notoriety") == 0) {
		((void (*)(void *, int))VT_FN(target, VT_SET_NOTORIETY))(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "fame") == 0) {
		CMobile_SetFame(mob, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "karma") == 0) {
		CMobile_SetKarma(mob, (int16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "changefame") == 0) {
		CMobile_ChangeFame(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "changekarma") == 0) {
		CMobile_ChangeKarma(mob, intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "name") == 0) {
		CMobile_SetName(mob, valueBuf);
		changed = 1;
	}

	if (strcasecmp(varName, "body") == 0) {
		CEntity_SetBodyType(target, (uint16_t)intValue);
		changed = 1;
	}

	if (strcasecmp(varName, "hunger") == 0) {
		mob->hunger = (uint8_t)intValue;
		changed = 1;
	}

	if (strcasecmp(varName, "skill") == 0) {
		skillNum = 0;
		memset(skillName, 0, 256);
		skillValue = 0;
		sscanfResult = sscanf(valueBuf, "%s %d", skillName, (int *)&skillValue);
		if (sscanfResult < 2) {
			if (player != NULL)
				CPlayer_SystemMessage(player, "Format: set <target> skill"
				                              " <value>");
			goto skill_done;
		}
		if (isdigit((unsigned char)skillName[0]))
			skillNum = atoi(skillName);
		else
			skillNum = SkillManager_GetSkillNumber(skillName);
		if (skillNum < 0) {
			CPlayer_SystemMessage(player, "Invalid skill");
			goto skill_done;
		}
		CMobile_SetSkill(mob, (int8_t)skillNum, skillValue);
		if (player != NULL)
			CSkillManager_SendSkillList(&g_SkillManager, (CItem *)player);
skill_done:
		changed = 1;
	}

	if (strcasecmp(varName, "skillmod") == 0) {
		skillmodNum = 0;
		memset(skillmodName, 0, 256);
		skillmodValue = 0;
		sscanfResult = sscanf(valueBuf, "%s %d", skillmodName, (int *)&skillmodValue);
		if (sscanfResult < 2) {
			if (player != NULL)
				CPlayer_SystemMessage(player, "Format: set <target> skillmod"
				                              " <value>");
			goto skillmod_done;
		}
		if (isdigit((unsigned char)skillmodName[0]))
			skillmodNum = atoi(skillmodName);
		else
			skillmodNum = SkillManager_GetSkillNumber(skillmodName);
		if (skillmodNum < 0) {
			CPlayer_SystemMessage(player, "Invalid skill");
			goto skillmod_done;
		}
		CMobile_SetSkillBonus(mob, (int8_t)skillmodNum, skillmodValue);
		if (player != NULL)
			CSkillManager_SendSkillList(&g_SkillManager, (CItem *)player);
skillmod_done:
		changed = 1;
	}

post_mobile:
	// NPC-only: attitude (binary field at mob+0x28B = attackMode)
	if (VT_IsNPC(target)) {
		npc = (CNPC *)target;
		if (strcasecmp(varName, "attitude") == 0) {
			npc->mobile.attackMode = (uint8_t)intValue;
			changed = 1;
		}
	}

	// Refresh entity if changed and sendUpdate
	if (changed == 1 && sendUpdate == 1) {
		if (!target->resourceEntity.entity.removedFromWorld) {
			((void (*)(void *))VT_FN(target, VT_HIDE))(target);
			((void (*)(void *))VT_FN(target, VT_RETURN_TO_TRACKED))(target);
		}
	} else if (changed == 0) {
		// Variable not recognized
		CString_Constructor(&notFoundStr, "Variable '");
		CString_AppendCStr(&notFoundStr, varName);
		CString_AppendCStr(&notFoundStr, "' not found.");
		if (player != NULL)
			CPlayer_SystemMessage(player, CString_GetBuffer(&notFoundStr));
		CString_Destructor(&notFoundStr);
	}

	// Log the command to EventLogger
	if (changed == 0)
		return;
	CString_Constructor(&logStr, "set ");
	CString_AppendCStr(&logStr, text);
	if (player != NULL) {
		EventLogger_Log(&g_EventLogger, player->accountNum, (uint32_t)(uint8_t)player->characterNum, CMobile_GetSerial((CMobile *)player),
		        CMobile_GetName_VT((CItem *)player), "godcommand", "misc", CString_GetBuffer(&logStr));
	}
	CString_Destructor(&logStr);
}

/*
 * 0x0048C99A - CWorld::SCommand_Query (orphaned)
 *
 * Parses "query <target> <variable>", resolves target via name / "me"
 * / serial + VT_IsMobile, and sends "Value: %d" to the player. Reads
 * base and modded stats, hp/mana/fatigue pairs, hunger, notoriety,
 * fame (u16), karma (s16), and adjusted fame/karma. Unknown variables
 * fall through as 0. ORPHANED: the SCommand dispatch table is never
 * populated, so this handler is unreachable.
 */
static __attribute__((unused)) void
CWorld_SCommand_Query(CWorld *world, CPlayer *player, const char *text)
{
	char targetNameBuf[80];
	char varName[80];
	char msgBuf[256];
	int value;
	CMobile *target;
	CItem *entity;
	uint32_t serial;
	int sscanfResult;

	sscanfResult = sscanf(text, "%s %[^\n]s", targetNameBuf, varName);
	if (sscanfResult < 2) {
		CPlayer_SystemMessage(player, "Format: query <target> <variable>");
		CPlayer_SystemMessage(player, "Vars: strength, dexterity, intelligence, "
		                              "maxhp, curhp, maxmana, curmana, "
		                              "maxfatigue, curfatigue");
		return;
	}

	target = (CMobile *)FindPlayer(g_PlayerList.head, targetNameBuf);
	if (target == NULL) {
		if (strcmp(targetNameBuf, "me") == 0)
			target = (CMobile *)player;
	}
	if (target == NULL) {
		serial = (uint32_t)atoi(targetNameBuf);
		entity = CWorld_FindBySerial(world, serial);
		if (entity != NULL && VT_IsMobile(entity))
			target = (CMobile *)entity;
	}
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Target not found.");
		return;
	}

	if (strcmp(varName, "strength") == 0 || strcmp(varName, "s") == 0) {
		value = (int)(int16_t)CMobile_GetBaseStat(target, 0);
	} else if (strcmp(varName, "dexterity") == 0 || strcmp(varName, "d") == 0) {
		value = (int)(int16_t)CMobile_GetBaseStat(target, 1);
	} else if (strcmp(varName, "intelligence") == 0 || strcmp(varName, "i") == 0) {
		value = (int)(int16_t)CMobile_GetBaseStat(target, 2);
	} else if (strcmp(varName, "strengthmod") == 0 || strcmp(varName, "sm") == 0) {
		value = (int)(int16_t)CMobile_GetStatBonus(target, 0);
	} else if (strcmp(varName, "dexteritymod") == 0 || strcmp(varName, "dm") == 0) {
		value = (int)(int16_t)CMobile_GetStatBonus(target, 1);
	} else if (strcmp(varName, "intelligencemod") == 0 || strcmp(varName, "im") == 0) {
		value = (int)(int16_t)CMobile_GetStatBonus(target, 2);
	} else if (strcmp(varName, "hunger") == 0) {
		value = (int)target->hunger;
	} else if (strcmp(varName, "maxhp") == 0) {
		value = (int)target->maxHp;
	} else if (strcmp(varName, "curhp") == 0) {
		value = (int)target->hp;
	} else if (strcmp(varName, "maxmana") == 0) {
		value = (int)target->maxMana;
	} else if (strcmp(varName, "curmana") == 0) {
		value = (int)target->mana;
	} else if (strcmp(varName, "maxfatigue") == 0) {
		value = (int)target->maxStamina;
	} else if (strcmp(varName, "curfatigue") == 0) {
		value = (int)target->stamina;
	} else if (strcmp(varName, "notoriety") == 0) {
		value = (int)(int8_t)target->notoriety;
	} else if (strcmp(varName, "fame") == 0) {
		value = (int)(uint16_t)target->fame;
	} else if (strcmp(varName, "karma") == 0) {
		value = (int)(int16_t)target->karma;
	} else if (strcmp(varName, "adjfame") == 0) {
		value = CMobile_GetAdjFame(target);
	} else if (strcmp(varName, "adjkarma") == 0) {
		value = CMobile_GetAdjKarma(target);
	} else {
		value = 0;
	}

	sprintf(msgBuf, "Value: %d", value);
	CPlayer_SystemMessage(player, msgBuf);
}

/*
 * 0x0048CE9B - CWorld::SCommand_Add (orphaned)
 *
 * Parses "add <target> <variable> <value>", resolves target like
 * Query, and adds the signed delta to the stat. Base strength /
 * dexterity / intelligence are mutated as direct u16 adds; hp / mana
 * / fatigue pairs use the vtable setter on top of the current getter
 * value. Unknown variables reply "Variable not found.". ORPHANED:
 * the SCommand dispatch table is never populated.
 */
static __attribute__((unused)) void
CWorld_SCommand_Add(CWorld *world, CPlayer *player, const char *text)
{
	char targetNameBuf[80];
	char varName[80];
	int delta;
	CMobile *target;
	CItem *entity;
	uint32_t serial;
	int sscanfResult;

	sscanfResult = sscanf(text, "%s %s %d", targetNameBuf, varName, &delta);
	if (sscanfResult < 3) {
		CPlayer_SystemMessage(player, "Format: add <target> <variable> <value>");
		CPlayer_SystemMessage(player, "vars: strength, dexterity, intelligence, "
		                              "maxhp, curhp, maxmana, curmana, "
		                              "maxfatigue, curfatigue");
		return;
	}

	target = (CMobile *)FindPlayer(g_PlayerList.head, targetNameBuf);
	if (target == NULL) {
		if (strcmp(targetNameBuf, "me") == 0)
			target = (CMobile *)player;
	}
	if (target == NULL) {
		serial = (uint32_t)atoi(targetNameBuf);
		entity = CWorld_FindBySerial(world, serial);
		if (entity != NULL && VT_IsMobile(entity))
			target = (CMobile *)entity;
	}
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Target not found.");
		return;
	}

	if (strcmp(varName, "strength") == 0) {
		target->baseStr = (uint16_t)(target->baseStr + (uint16_t)delta);
	} else if (strcmp(varName, "dexterity") == 0) {
		target->baseDex = (uint16_t)(target->baseDex + (uint16_t)delta);
	} else if (strcmp(varName, "intelligence") == 0) {
		target->baseInt = (uint16_t)(target->baseInt + (uint16_t)delta);
	} else if (strcmp(varName, "maxhp") == 0) {
		((void (*)(void *, int))VT_FN((CItem *)target, VT_SET_MAX_HP))(target, (int)CMobile_GetMaxHp(target) + delta);
	} else if (strcmp(varName, "curhp") == 0) {
		((void (*)(void *, int, int))VT_FN((CItem *)target, VT_SET_HP))(target, (int)CMobile_GetHp(target) + delta, 0);
	} else if (strcmp(varName, "maxmana") == 0) {
		((void (*)(void *, int))VT_FN((CItem *)target, VT_SET_MAX_MANA))(target, (int)CMobile_GetMaxMana(target) + delta);
	} else if (strcmp(varName, "curmana") == 0) {
		((void (*)(void *, int))VT_FN((CItem *)target, VT_SET_MANA))(target, (int)CMobile_GetMana(target) + delta);
	} else if (strcmp(varName, "maxfatigue") == 0) {
		((void (*)(void *, int))VT_FN((CItem *)target, VT_SET_MAX_STAMINA))(target, (int)CMobile_GetMaxStamina(target) + delta);
	} else if (strcmp(varName, "curfatigue") == 0) {
		((void (*)(void *, int))VT_FN((CItem *)target, VT_SET_STAMINA))(target, (int)CMobile_GetStamina(target) + delta);
	} else {
		CPlayer_SystemMessage(player, "Variable not found.");
	}
}

/*
 * 0x0048D175 - CWorld::AllocSerial
 *
 * Thiscall on CWorld. Checks isLoading (CWorld+0x08): if loading,
 * returns 0 (entities get serial 0 during world load; LoadDynamic0
 * sets the real serial from saved data via the id= key). Otherwise
 * calls AllocNextSerial (0x00485248) for the serial counter increment.
 */
uint32_t
CWorld_AllocSerial(CWorld *world)
{
	if (world->isLoading != 0)
		return 0;
	return AllocNextSerial();
}

/*
 * 0x0048D9FC - CWorld::FindEntityInRange
 *
 * Returns the entity matching serial that lies within range of
 * refEntity. Searches the spatial grid blocks near refEntity and
 * recurses into any containers it visits. Returns NULL when no
 * match is found.
 */
CItem *
CWorld_FindEntityInRange(CWorld *world, CEntity *refEntity, uint32_t serial, int range)
{
	CLocation loc;
	int blockBuf[256];
	int i;
	CItem *iter, *found;

	USED(world);

	CLocation_Init(&loc);
	CLocation_SetLoc(&loc, &refEntity->location);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &loc, range, blockBuf, 256);

	for (i = 0; blockBuf[i] != -1; i++) {
		iter = g_SpatialGrid.cells[blockBuf[i]].itemHead;

		// Iterate spatial item list (+0x104 via spatialNext +0x20)
		for (; iter != NULL; iter = iter->spatialNext) {
			if (CLocation_ChebyshevDistance(&iter->resourceEntity.entity.location, &loc) > range)
				continue;

			if (iter->serial == serial)
				return iter;

			if (VT_IsMobile2(iter)) {
				found = CContainer_FindItemBySerial((CContainer *)iter, serial);
				if (found != NULL)
					return found;
			}
		}
	}

	return NULL;
}

/*
 * 0x0048DAF7 - CWorld::FindEntityBySerialInRange
 *
 * Scans every block within range of loc for the entity with the given
 * serial, and for any mobile in range also searches its contents.
 * Returns NULL once the block list terminates.
 */
static __attribute__((unused)) CItem *
CWorld_FindEntityBySerialInRange(CWorld *world, CLocation *loc, uint32_t serial, int range)
{
	int blockBuf[0x100];
	CItem *iter;
	CItem *found;
	int i;

	USED(world);
	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockBuf, 0x100);

	for (i = 0; blockBuf[i] != -1; i++) {
		for (iter = g_SpatialGrid.cells[blockBuf[i]].itemHead; iter != NULL; iter = iter->spatialNext) {
			if (CLocation_ChebyshevDistance(&iter->resourceEntity.entity.location, loc) > range)
				continue;
			if (iter->serial == serial)
				return iter;
			if (((int (*)(void *))VT_FN(iter, VT_IS_MOBILE2))(iter)) {
				found = CContainer_FindItemBySerial((CContainer *)iter, serial);
				if (found != NULL)
					return found;
			}
		}
	}
	return NULL;
}

/*
 * 0x0048DBDB - FindHighestItemAtXY
 *
 * Returns the dynamic or static item with the highest Z at (x, y),
 * or NULL when nothing is stacked there.
 */
CItem *
FindHighestItemAtXY(CWorld *world, int16_t x, int16_t y)
{
	CItem *best;
	int bestZ;
	int blockIdx;
	CItem *iter;
	CLocation loc;

	USED(world);
	best = NULL;
	bestZ = (int32_t)0xFFFFFF80;
	loc.x = x;
	loc.y = y;
	loc.z = 0;
	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &loc, 0);

	// Dynamic item chain (block+0x104, spatialNext at 0x20)
	iter = g_SpatialGrid.cells[blockIdx].itemHead;
	while (iter != NULL) {
		if (iter->resourceEntity.entity.location.x == x && iter->resourceEntity.entity.location.y == y) {
			if ((int16_t)iter->resourceEntity.entity.location.z > bestZ) {
				bestZ = (int16_t)iter->resourceEntity.entity.location.z;
				best = iter;
			}
		}
		iter = iter->spatialNext;
	}

	// Static item chain (block+0x100, nextInContainer at 0x10)
	iter = g_SpatialGrid.cells[blockIdx].staticHead;
	while (iter != NULL) {
		if (iter->resourceEntity.entity.location.x == x && iter->resourceEntity.entity.location.y == y) {
			if ((int16_t)iter->resourceEntity.entity.location.z > bestZ) {
				bestZ = (int16_t)iter->resourceEntity.entity.location.z;
				best = iter;
			}
		}
		iter = (CItem *)iter->resourceEntity.nextInContainer;
	}

	return best;
}

/*
 * 0x0048DCD9 - CWorld::FindEntityAtXY
 *
 * Returns the first entity whose location matches loc in x and y,
 * searching the dynamic chain before the static one. z is ignored.
 */
static __attribute__((unused)) CItem *
CWorld_FindEntityAtXY(CWorld *world, CLocation *loc)
{
	int blockIdx;
	CItem *iter;

	USED(world);
	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);

	// Dynamic item chain (block+0x104, spatialNext at 0x20)
	iter = g_SpatialGrid.cells[blockIdx].itemHead;
	while (iter != NULL) {
		if (iter->resourceEntity.entity.location.x == loc->x && iter->resourceEntity.entity.location.y == loc->y)
			return iter;
		iter = iter->spatialNext;
	}

	// Static item chain (block+0x100, nextInContainer at 0x10)
	iter = g_SpatialGrid.cells[blockIdx].staticHead;
	while (iter != NULL) {
		if (iter->resourceEntity.entity.location.x == loc->x && iter->resourceEntity.entity.location.y == loc->y)
			return iter;
		iter = (CItem *)iter->resourceEntity.nextInContainer;
	}

	return NULL;
}

/*
 * 0x0048DD9B - CWorld::FindEntityAtXYZ
 *
 * As 0x0048DCD9 but also requires the z to match the given value.
 */
static __attribute__((unused)) CItem *
CWorld_FindEntityAtXYZ(CWorld *world, CLocation *loc, int16_t z)
{
	int blockIdx;
	CItem *iter;

	USED(world);
	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);

	iter = g_SpatialGrid.cells[blockIdx].itemHead;
	while (iter != NULL) {
		if (iter->resourceEntity.entity.location.x == loc->x && iter->resourceEntity.entity.location.y == loc->y && (int16_t)iter->resourceEntity.entity.location.z == z)
			return iter;
		iter = iter->spatialNext;
	}

	iter = g_SpatialGrid.cells[blockIdx].staticHead;
	while (iter != NULL) {
		if (iter->resourceEntity.entity.location.x == loc->x && iter->resourceEntity.entity.location.y == loc->y && (int16_t)iter->resourceEntity.entity.location.z == z)
			return iter;
		iter = (CItem *)iter->resourceEntity.nextInContainer;
	}

	return NULL;
}

/*
 * 0x0048DF7B - CWorld::GetItemTileDataCopy
 *
 * Copies the tiledata entry for id into a static TileDataEntry and
 * returns it. The layer byte is refreshed from CWorld_GetItemLayer
 * rather than taken from the table, and the 21-byte name is copied one
 * byte at a time - only 0x14 of the 21, so the last byte of the static
 * copy keeps whatever the previous call left there.
 */
static __attribute__((unused)) TileDataEntry *
CWorld_GetItemTileDataCopy(int id)
{
	// 0x006CA8B8 - static tiledata record returned to the caller
	static TileDataEntry copy;
	TileDataEntry *src;
	char *dst;
	int i;

	src = &g_ItemTileData[id];
	copy.flags = src->flags;
	copy.weight = src->weight;
	copy.layer = (uint8_t)CWorld_GetItemLayer((uint16_t)id);
	copy.miscData = src->miscData;
	copy.value1 = src->value1;
	copy.value2 = src->value2;
	copy.height = src->height;
	copy.quantity = src->quantity;

	dst = copy.name;
	for (i = 0; i < 0x14; i++) {
		*dst = src->name[i];
		dst++;
	}
	return &copy;
}

/*
 * 0x0048E084 - CWorld::NotifyEntityArrival
 *
 * Sends an entity update for entity to every entity within 18 tiles,
 * and tells the NPCs among them that a player came into range. Unlike
 * its live neighbours this walks the spatial grid blocks directly. The
 * world pointer is never touched.
 */
static __attribute__((unused)) void
CWorld_NotifyEntityArrival(CWorld *world, CItem *entity)
{
	int blockIds[256];
	int i;
	CItem *cur;

	USED(world);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &entity->resourceEntity.entity.location, 0x12, blockIds, 0x100);

	for (i = 0; blockIds[i] != -1; i++) {
		for (cur = g_SpatialGrid.cells[blockIds[i]].itemHead; cur != NULL; cur = cur->spatialNext) {
			if (CLocation_ChebyshevDistance(&cur->resourceEntity.entity.location, &entity->resourceEntity.entity.location) >= 0x13)
				continue;

			((void (*)(void *, CItem *, int))VT_FN(cur, VT_SEND_ENTITY_UPDATE))(cur, entity, 1);

			if (((int (*)(void *))VT_FN(cur, VT_IS_NPC))(cur))
				CNPC_OnPlayerEnteredRange((CMobile *)cur, (CMobile *)entity);
		}
	}
}

/*
 * 0x0048E39F - CWorld::CreateItem
 *
 * Allocates and constructs the right CItem subclass for bodyType (map
 * signpost, bulletin board, container, weapon/armor, or plain item), sets
 * the body type. Returns NULL on bad bodyType
 * or non-item class.
 *
 * MODIFIED: AllocateItem (0x0048E15F, 576 bytes) is inlined here rather
 * than called as a separate function; binary uses operator new, we use
 * calloc + Construct. The binary's AllocateItem also performs a
 * VT_IsContainer + CMobile_GetSerial logging pair before returning;
 * we omit that. The binary's static-entity branch (isStatic=1, used
 * by CEditorObj_HandleEdit) is handled separately at the caller via
 * CreateStaticEntity. The binary's
 * redundant NULL check before VT_DELETE on the failure path is omitted.
 */
CItem *
CWorld_CreateItem(CWorld *world, uint16_t bodyType)
{
	CItem *item;
	uint32_t tileFlags;

	USED(world);

	if (!CWorld_LookupItemResource(bodyType))
		return NULL;

	tileFlags = CWorld_GetItemTileFlags(bodyType);

	if (tileFlags & TF_MAP) {
		item = calloc(1, sizeof(CSignpost));
		if (item != NULL)
			CSignpost_Constructor((CSignpost *)item);
	} else if ((bodyType & 0xFFFF) == 0x1E5E || (bodyType & 0xFFFF) == 0x1E5F) {
		item = calloc(1, sizeof(CBulletinBoard));
		if (item != NULL)
			CBulletinBoard_Constructor((CBulletinBoard *)item);
	} else if (tileFlags & TF_CONTAINER) {
		item = calloc(1, sizeof(CContainer));
		if (item != NULL)
			CContainer_Constructor((CContainer *)item);
	} else if (tileFlags & (TF_WEAPON | TF_ARMOR)) {
		item = CWeapon_Create(bodyType);
	} else {
		item = calloc(1, sizeof(CItem));
		if (item != NULL)
			CItem_Constructor(item);
	}

	if (item == NULL)
		return NULL;

	item->resourceEntity.entity.bodyType = bodyType;

	// All CItem-derived return 1; guards against non-item entities.
	if (!VT_IsContainer(item)) {
		((void (*)(void *))VT_FN(item, VT_DELETE))(item);
		return NULL;
	}

	return item;
}

/*
 * 0x0048E3FA - CWorld::CreateContainerItem
 *
 * Creates a new item via CWorld_CreateItem, then verifies it is a
 * container type via vtable[0xD4] (IsMobile2). If not a container,
 * deletes the item via vtable[0x90] (Delete) and returns NULL.
 */
CItem *
CWorld_CreateContainerItem(CWorld *world, uint16_t bodyType)
{
	CItem *item;

	item = CWorld_CreateItem(world, bodyType);
	if (item == NULL)
		return NULL;

	if (VT_IsMobile2(item))
		return item;

	// Not a container - delete and return NULL
	if (item != NULL)
		((void (*)(void *))VT_FN(item, VT_DELETE))(item);
	return NULL;
}

/*
 * 0x0048E450 - CWorld::CreateWeaponItem
 *
 * Creates an item and keeps it only when it reports as a weapon;
 * otherwise deletes it through the vtable and returns NULL.
 */
static __attribute__((unused)) CItem *
CWorld_CreateWeaponItem(CWorld *world, uint16_t bodyType)
{
	CItem *item;

	item = CWorld_CreateItem(world, bodyType);
	if (item == NULL)
		return NULL;
	if (((int (*)(void *))VT_FN(item, VT_IS_WEAPON))(item) == 0) {
		((void (*)(void *))VT_FN(item, VT_DELETE))(item);
		return NULL;
	}
	return item;
}

/*
 * 0x0048E4A6 - CItem::TestTypeBit
 *
 * Maps a single-bit type selector onto the matching vtable predicate
 * and returns its result. 0x0200 and 0x0400 are recognised but always
 * answer 0, and so does any value that is not one of the listed bits or
 * a null entity.
 */
static __attribute__((unused)) int
CItem_TestTypeBit(CItem *entity, int bit)
{
	if (entity == NULL)
		return 0;

	switch (bit) {
	case 0x0002:
		return ((int (*)(void *))VT_FN(entity, VT_IS_CONTAINER))(entity);
	case 0x0004:
		return ((int (*)(void *))VT_FN(entity, VT_IS_MOBILE2))(entity);
	case 0x0008:
		return ((int (*)(void *))VT_FN(entity, VT_IS_SPATIAL))(entity);
	case 0x0010:
		return ((int (*)(void *))VT_FN(entity, VT_IS_MOBILE))(entity);
	case 0x0020:
		return ((int (*)(void *))VT_FN(entity, VT_IS_NPC))(entity);
	case 0x0040:
		return ((int (*)(void *))VT_FN(entity, VT_IS_VENDOR))(entity);
	case 0x0080:
		return ((int (*)(void *))VT_FN(entity, VT_CHECK_EC))(entity);
	case 0x0100:
		return ((int (*)(void *))VT_FN(entity, VT_IS_PLAYER))(entity);
	case 0x0200:
		return 0;
	case 0x0400:
		return 0;
	case 0x0800:
		return ((int (*)(void *))VT_FN(entity, VT_IS_DEAD))(entity);
	case 0x1000:
		return ((int (*)(void *))VT_FN(entity, VT_IS_WEAPON))(entity);
	case 0x2000:
		return ((int (*)(void *))VT_FN(entity, VT_CHECK_DC))(entity);
	default:
		return 0;
	}
}

/*
 * 0x0048E664 - CItem::ClassifyEntityByVtable
 *
 * Returns a bitmask classifying the entity's runtime type by
 * probing boolean vtable slots in a decision tree:
 *   1     = non-container (CEntity/CResourceEntity)
 *   2     = CItem (or CEgg)
 *   4     = CContainer (or CMulti)
 *   8     = CSignpost
 *   0x10  = CMobile (non-NPC, non-player)
 *   0x20  = CNPC (plain NPC)
 *   0x40  = CShopkeeper (NPC + IsVendor)
 *   0x80  = CGuard (NPC + CheckEC)
 *   0x100 = CPlayer
 *   0x1000 = CWeapon
 *   0x2000 = unused (no entity returns true)
 */
int
CItem_ClassifyEntityByVtable(CItem *this)
{
	if (!VT_IsContainer(this))
		return 1;

	if (VT_IsSpatial(this))
		return 8;

	if (VT_IsWeapon(this))
		return 0x1000;

	if (!VT_IsMobile2(this))
		return 2;

	if (((int (*)(void *))VT_FN(this, VT_CHECK_DC))(this))
		return 0x2000;

	if (!VT_IsMobile(this))
		return 4;

	if (VT_IsPlayer(this))
		return 0x100;

	if (!VT_IsNPC(this))
		return 0x10;

	if (VT_IsVendor(this))
		return 0x40;

	if (((int (*)(void *))VT_FN(this, VT_CHECK_EC))(this))
		return 0x80;

	return 0x20;
}

/*
 * 0x0048ED04 - CWorld::IsValidItemResource
 *
 * Returns 1 when bodyType lies inside the legal item-tile range.
 */
int
CWorld_IsValidItemResource(uint16_t bodyType)
{
	return bodyType < TILEDATA_MAX_ITEMS;
}

/*
 * 0x0048ED25 - CWorld::LookupItemResource
 *
 * Wrapper around CWorld_IsValidItemResource that loads bodyType as
 * 16-bit before delegating.
 */
int
CWorld_LookupItemResource(uint16_t bodyType)
{
	return CWorld_IsValidItemResource(bodyType);
}

/*
 * 0x0048EE03 - CDataManager::InitMapBuffers
 *
 * Allocates the land tile-data and item tile-data arenas (0x70000 and
 * 0xA0000 bytes) and unlocks their critical sections.
 */
static void
CDataManager_InitMapBuffers(CWorld *this)
{
	this->critSectTileData1 = ArenaAllocator_Alloc(0x70000, (void **)&this->landTileData);
	this->critSectTileData2 = ArenaAllocator_Alloc(0xA0000, (void **)&this->itemTileData);
}

/*
 * 0x0048EE64 - CDataManager::LockTileData
 *
 * Single-threaded server - the tile-data critical sections are no-ops.
 */
void
CDataManager_LockTileData(void)
{
}

/*
 * 0x0048EE8B - CDataManager::UnlockTileData
 *
 * Single-threaded server - the tile-data critical sections are no-ops.
 */
void
CDataManager_UnlockTileData(void)
{
}

/*
 * 0x0048EEB2 - CDataManager::InitAnimBuffer
 *
 * Allocates the 0x110000-byte anim-data arena and unlocks its critical
 * section.
 */
static void
CDataManager_InitAnimBuffer(CWorld *this)
{
	this->critSectAnimData = ArenaAllocator_Alloc(0x110000, (void **)&this->animData);
}

/*
 * 0x0048EEE8 - CDataManager::LockAnimData
 *
 * Single-threaded server - the anim-data critical section is a no-op.
 */
void
CDataManager_LockAnimData(void)
{
}

/*
 * 0x0048EF01 - CDataManager::UnlockAnimData
 *
 * Single-threaded server - the anim-data critical section is a no-op.
 */
void
CDataManager_UnlockAnimData(void)
{
}

/*
 * 0x0048EF1A - CDataManager::InitTileBuffer
 *
 * Allocates the two hue-data arenas (0x40740 and 0x6C660 bytes) and clears
 * the name field of all 3000 entries in each table.
 */
static void
CDataManager_InitTileBuffer(CWorld *this)
{
	int i;

	this->critSectHueData1 = ArenaAllocator_Alloc(0x40740, (void **)&this->hueData);
	this->critSectHueData2 = ArenaAllocator_Alloc(0x6C660, (void **)&this->hueDataExp);

	for (i = 0; i < 0xBB8; i++)
		strcpy(((HueEntry *)this->hueData)[i].name, "");
	for (i = 0; i < 0xBB8; i++)
		strcpy(((HueEntryExpanded *)this->hueDataExp)[i].name, "");
}

/*
 * 0x0048EFF6 - CDataManager::LockHueData
 *
 * Single-threaded server - the hue-data critical sections are no-ops.
 */
void
CDataManager_LockHueData(void)
{
}

/*
 * 0x0048F01D - CDataManager::UnlockHueData
 *
 * Single-threaded server - the hue-data critical sections are no-ops.
 */
void
CDataManager_UnlockHueData(void)
{
}

/*
 * 0x0048F044 - CWorld::Lock
 *
 * Single-threaded server - no-op.
 */
void
CWorld_Lock(void)
{
}

/*
 * 0x0048F04F - CWorld::Unlock
 *
 * Single-threaded server - no-op.
 */
void
CWorld_Unlock(void)
{
}

/*
 * 0x0048F05A - CDataManager::InitArtBuffer
 *
 * Allocates the 0x4000-entry resource-entity slot array, runs
 * CResourceEntity_Constructor on each slot, and seeds each slot's body
 * type with its index. The loading flag is set across the build.
 */
static void
CDataManager_InitArtBuffer(CWorld *this)
{
	int i;
	char *alloc;
	char *arrayStart;

	this->isLoading = 1;

	// Custom: 64-bit - sizeof(uintptr_t) header for alignment
	alloc = (char *)malloc(0x4000 * sizeof(CResourceEntity) + sizeof(uintptr_t));

	if (alloc != NULL) {
		*(uint32_t *)alloc = 0x4000;
		for (i = 0; i < 0x4000; i++)
			CResourceEntity_Constructor((CResourceEntity *)(alloc + sizeof(uintptr_t) + i * sizeof(CResourceEntity)));
		arrayStart = alloc + sizeof(uintptr_t);
	} else {
		arrayStart = NULL;
	}

	this->resEntitySlots = arrayStart;

	if (arrayStart != NULL) {
		for (i = 0; i < 0x4000; i++) {
			CEntity_SetBodyType((CItem *)((char *)this->resEntitySlots + i * sizeof(CResourceEntity)), (uint16_t)i);
		}
	}

	this->isLoading = 0;
}

/*
 * 0x00492120 - World_ValidateEntity
 *
 * Returns expected when CWorld_FindBySerial(serial) resolves to the same
 * pointer, else NULL.
 */
CItem *
World_ValidateEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = CWorld_FindBySerial(g_World, serial);
	if (found != expected)
		return NULL;
	return expected;
}

/*
 * 0x00492148 - ValidateMobileEntity
 *
 * Validates entity by serial, then checks VT_IS_MOBILE (vtable[0xD0]).
 * Returns entity if valid mobile, NULL otherwise.
 */
CItem *
World_ValidateMobileEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = World_ValidateEntity(expected, serial);
	if (found == NULL)
		return NULL;
	if (!VT_IsMobile(found))
		return NULL;
	return found;
}

/*
 * 0x00492188 - ValidatePlayerEntity
 *
 * Validates entity as mobile, then checks VT_IS_PLAYER (vtable[0x18]).
 * Returns entity if valid player, NULL otherwise.
 * Called from HandlePacket_REQ_GETOBJ.
 */
CItem *
World_ValidatePlayerEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = World_ValidateMobileEntity(expected, serial);
	if (found == NULL)
		return NULL;
	if (!VT_IsPlayer(found))
		return NULL;
	return found;
}

/*
 * 0x004921C5 - ValidateNPCEntity
 *
 * Validates entity as mobile, then checks VT_IS_NPC (vtable[0xE4]).
 * Returns entity if valid NPC, NULL otherwise.
 */
CItem *
World_ValidateNPCEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = World_ValidateMobileEntity(expected, serial);
	if (found == NULL)
		return NULL;
	if (!VT_IsNPC(found))
		return NULL;
	return found;
}

/*
 * 0x00492205 - ValidateWeaponEntity
 *
 * Validates entity by serial, then checks VT_IS_WEAPON (vtable[0xF8]).
 * Returns entity if valid weapon, NULL otherwise.
 */
CItem *
World_ValidateWeaponEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = World_ValidateEntity(expected, serial);
	if (found == NULL)
		return NULL;
	if (!VT_IsWeapon(found))
		return NULL;
	return found;
}

/*
 * 0x00492245 - ValidateContainerEntity
 *
 * Validates entity by serial, then checks VT_IS_MOBILE2 (vtable[0xD4]).
 * Returns entity if valid container/mobile, NULL otherwise.
 */
CItem *
World_ValidateContainerEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = World_ValidateEntity(expected, serial);
	if (found == NULL)
		return NULL;
	if (!VT_IsMobile2(found))
		return NULL;
	return found;
}

/*
 * 0x00492285 - ValidateSpatialEntity
 *
 * Validates entity by serial, then checks VT_IS_SPATIAL (vtable[0xE0]).
 * Returns entity if valid spatial entity, NULL otherwise.
 */
CItem *
World_ValidateSpatialEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = World_ValidateEntity(expected, serial);
	if (found == NULL)
		return NULL;
	if (!VT_IsSpatial(found))
		return NULL;
	return found;
}

/*
 * 0x004922C5 - ValidateBookEntity
 *
 * Validates entity by serial, then checks CItem_IsWritableBook.
 * Returns entity if valid writable book, NULL otherwise.
 */
CItem *
World_ValidateBookEntity(CItem *expected, uint32_t serial)
{
	CItem *found;

	found = World_ValidateEntity(expected, serial);
	if (found == NULL)
		return NULL;
	if (!CItem_IsWritableBook(found))
		return NULL;
	return found;
}

/*
 * Helper - CWorld_InsertEntity
 *
 * Insert entity into hash table by serial.
 * Binary does this inline in entity constructors and CreateItem.
 */
void
CWorld_InsertEntity(CWorld *world, CItem *entity)
{
	uint16_t bucket;

	bucket = entity->serial & 0xFFFF;
	entity->hashNext = world->hashTable[bucket];
	if (entity->hashNext != NULL)
		entity->hashNext->hashPrev = entity;
	entity->hashPrev = NULL;
	world->hashTable[bucket] = entity;
}

/*
 * Helper - CWorld_RemoveEntity
 *
 * Remove entity from hash table by serial.
 * Binary does this inline with doubly-linked O(1) unlink via
 * hashNext (0x2C) and hashPrev (0x30).
 */
void
CWorld_RemoveEntity(CWorld *world, CItem *entity)
{
	uint16_t bucket;

	if (entity->hashNext != NULL)
		entity->hashNext->hashPrev = entity->hashPrev;
	if (entity->hashPrev != NULL) {
		entity->hashPrev->hashNext = entity->hashNext;
	} else {
		bucket = entity->serial & 0xFFFF;
		world->hashTable[bucket] = entity->hashNext;
	}
}

/*
 * Helper - CWorld_GetItemTileFlags
 *
 * Binary reads g_ItemTileData[bodyType].flags inline. This helper adds
 * bounds checking not present in binary.
 */
uint32_t
CWorld_GetItemTileFlags(uint16_t bodyType)
{
	if (g_ItemTileData == NULL || bodyType >= TILEDATA_MAX_ITEMS)
		return 0;
	return g_ItemTileData[bodyType].flags;
}

/*
 * Helper - CWorld_GetItemName
 *
 * Returns item name from tiledata array. Binary reads
 * g_ItemTileData[bodyType].name inline.
 */
char *
CWorld_GetItemName(uint16_t bodyType)
{
	if (g_ItemTileData == NULL || bodyType >= TILEDATA_MAX_ITEMS)
		return "";
	if (g_ItemTileData[bodyType].name[0] == '\0')
		return "";
	return g_ItemTileData[bodyType].name;
}

/*
 * Helper - CWorld_DeleteEntity
 *
 * Wrapper around vtable[0x90] Delete dispatch.
 * Binary uses vtable[0x90] dispatch directly.
 */
void
CWorld_DeleteEntity(CWorld *world, CItem *entity)
{
	USED(world);

	if (entity == NULL)
		return;

	((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
}

/*
 * Custom - World_ShutdownEntities
 *
 * Server-shutdown cleanup walker. Walks every entity still in the
 * world hash table and releases the resources that the binary's
 * exit path leaks: timer chains (via CEntity_RemoveAllTimers),
 * tag-list managers and attached scripts (via CItem_ClearScriptsAndTags),
 * and the lazily-allocated CItemTracking pool node (via
 * CItem_ReleaseTracking). No binary equivalent: the binary just
 * exits the process and these resources stay "in use" forever.
 * Purpose is to keep the valgrind shutdown report focused on real
 * leaks.
 *
 * Does NOT call the entity's destructor or otherwise alter world
 * state: socket and ticker subsystems are still alive at this point
 * and a full destructor walk would broadcast packets, touch shutdown
 * subsystems, and reorder the hash table mid-traversal.
 */
void
World_ShutdownEntities(void)
{
	int i;
	CItem *entity, *next;

	if (g_World == NULL)
		return;

	for (i = 0; i < 0x10000; i++) {
		for (entity = g_World->hashTable[i]; entity != NULL; entity = next) {
			next = entity->hashNext;
			CEntity_RemoveAllTimers(entity);
			CItem_ClearScriptsAndTags(entity);
			CItem_ReleaseTracking(entity);
		}
	}
}
