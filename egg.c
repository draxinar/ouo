/*
 * CEgg / CResBankManager - egg-based NPC spawn system.
 *
 * Eggs are invisible world items that periodically spawn NPCs according
 * to per-region templates loaded from resbank.mul. SpawnTick walks the
 * regions each tick, respects spawn budgets, and drives ecology idle
 * scans for the resulting NPCs.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "container.h"
#include "dat.h"
#include "defcon.h"
#include "egg.h"
#include "resbank.h"
#include "time.h"
#include "io.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "resbank.h"
#include "utils.h"
#include "vtable.h"
#include "world.h"

__extension__ typedef struct SpawnReturnData SpawnReturnData;
static void Spawn_ReturnToOwner(SpawnReturnData *data); // 0x004853C7
static void StaticInit_HashResTypeName(void); // 0x004A83B0
static void CResourceTypeManager_Constructor(void); // 0x004A85E4

// Binary globals.
int g_SpawnEnabled = 1;           // 0x00621398
int g_IsInitialSpawn;             // 0x006E7654
int g_SpawningInProgress;         // 0x006E72EC

/*
 * CUSTOM: per-NPC home-location respawn queue (reservation pattern).
 *
 * The pre-stub OSI design had `Spawn_ScheduleRespawn(loc, info1, info2)`
 * record a single-NPC respawn at the dying NPC's home location after a
 * timed delay. Stubbed to a no-op in the demo binary. Under
 * FEAT_PERNPC_RESPAWN we reintroduce that path: every NPC death enqueues a
 * fire-time stamped (template, x, y, z) entry; PendingNPCRespawn_Tick
 * (called from the spawn block in time.c) walks the list each tick and
 * fires any entry whose deadline has elapsed via SpawnAtPointForLocation.
 *
 * The queue is the AUTHORITATIVE respawn clock: CountMobilesInBox
 * (resbank.c) counts pending entries as live mobiles, so the slot is
 * reserved against SpawnTick's random walker until the entry fires.
 * Without that reservation the random walker would fill the slot within
 * 8 s of any kill and the per-NPC fire would be rejected by the density
 * gate. State is in-memory only - a restart drops the queue and the
 * random walker repopulates from scratch.
 */
PendingNPCRespawn *g_pendingNPCRespawnHead;
int g_PerNPCRespawnDelayMs = 1024 * 1000; // ~17 min, matches 1 RespawnTimerCheck cycle

void
PendingNPCRespawn_Enqueue(uint16_t templateId, int16_t x, int16_t y, int8_t z)
{
	PendingNPCRespawn *p;

	p = (PendingNPCRespawn *)malloc(sizeof(*p));
	if (p == NULL)
		return;
	p->templateId = templateId;
	p->x = x;
	p->y = y;
	p->z = z;
	p->fireTickMs = GetTickCount_UO() + (uint32_t)g_PerNPCRespawnDelayMs;
	p->next = g_pendingNPCRespawnHead;
	g_pendingNPCRespawnHead = p;
}

void
PendingNPCRespawn_Tick(void)
{
	PendingNPCRespawn **pp;
	uint32_t now;

	now = GetTickCount_UO();
	pp = &g_pendingNPCRespawnHead;

	while (*pp != NULL) {
		PendingNPCRespawn *p = *pp;
		if ((int32_t)(p->fireTickMs - now) > 0) {
			pp = &p->next;
			continue;
		}
		// Unlink BEFORE checking density so this entry's reservation
		// slot in CountMobilesInBox isn't double-counted.
		*pp = p->next;
		// Drop the entry if the target sub-region is at cap.
		// SpawnAtPointForLocation -> SpawnAtPoint has no density check
		// of its own (it is the binary's direct-spawn API used by
		// scripts), so without this gate the queue would compound
		// spawns beyond cap on every cycle and templates like lich /
		// skeleton / zombie / spider grow monotonically while the
		// server idles.
		if (!DensityAtCapForRespawn(p->templateId, p->x, p->y))
			(void)SpawnAtPointForLocation(p->templateId, (uint16_t)p->x, (uint16_t)p->y, p->z, 6);
		free(p);
	}
}

/*
 * Binary: bitmap at 0x0063D8F8. Indexed by artID >> 5, bit tested with 1 << (artID & 0x1F).
 * If bit is set, ValidateInWorld attaches a script named after the artID.
 * In the demo build, no bits are set (all zero).
 */
uint32_t g_ValidateInWorldBitmap[512]; // 0x0063D8F8
CResBankManager g_ResBankManager; // 0x006DBD60

// Binary: CDefcon at 0x00699968 - server load limiter.
CDefcon g_Defcon; // 0x00699968

// Spawn statistics counters (binary: 0x006DC17C+).
int g_SpawnAttemptCount;        // 0x006DC17C
int g_SpawnNoTemplateCount;     // 0x006DC180
int g_SpawnSkipCount;           // 0x006DC184 - DEFCON shopkeeper gate
int g_SpawnDensityRejectCount;  // 0x006DC188
int g_SpawnCanSpawnRejectCount; // 0x006DC18C
int g_SpawnLocationFailCount;   // 0x006DC190
int g_SpawnCreateFailCount;     // 0x006DC194

/*
 * Egg processing globals (set during ProcessDynamicEggEntities).
 * g_ProcessingEggs: set to 1 during egg processing pass.
 * Checked by CEgg Delete override (0x0048510A) - deletion only proceeds
 * when this flag is set. Regular CItem Delete (0x00485059) ignores this.
 */
int g_ProcessingEggs; // 0x006CA908

// g_SuppressRespawn: set to 1 around egg entity deletion to suppress
// respawn scheduling. Referenced in CEgg save/registration code.
int g_SuppressRespawn; // 0x006CA8E8

// Binary global: g_TemplatesEnabled at 0x00698310.
// Controls whether GM template editing is allowed.
int g_TemplatesEnabled;

char g_TemplateNameBuf[64]; // 0x006E2250

/*
 * CResourceTypeManager (binary: 0x006DC1A0).
 * Binary stores an array of pointers to resource type objects at +0x04,
 * with count at +0x00. Each object has the name at +0x08.
 * Binary: CResourceTypeManager at 0x006DC1A0 has count(+0), entries[102](+4),
 * hashTable[512](+8). entries[] stores heap-allocated CResourceType* pointers.
 */
#define MAX_RESOURCE_TYPES 102
int g_ResourceTypeCount = 1; // 0x006DC1A0 - starts at 1 (index 0 unused)
CResourceType *g_ResTypeEntries[MAX_RESOURCE_TYPES]; // 0x006DC1A4
CResourceType *g_ResTypeHashTable[RES_TYPE_HASH_SIZE];
char *g_ResourceTypeNames[MAX_RESOURCE_TYPES]; // compatibility for callers

// Resource type ID globals (binary: 0x006E7294-0x006E72E4).
// Populated by InitResourceTypeIds (0x004B2A6A) during startup.
int g_ResTypeId_Meat;          // 0x006E7294 - "meat"
int g_ResTypeId_Water;         // 0x006E7298 - "water"
int g_ResTypeId_Self;          // 0x006E729C - "self"
int g_ResTypeId_Humans;        // 0x006E72A0 - "humans"
int g_ResTypeId_Feathers;      // 0x006E72A4 - "feathers"
int g_ResTypeId_Hide;          // 0x006E72A8 - "hide"
int g_ResTypeId_Wool;          // 0x006E72AC - "wool"
int g_ResTypeId_Gold;          // 0x006E72B0 - "gold"
int g_ResTypeId_Magic;         // 0x006E72B4 - "magic"
int g_ResTypeId_Darkness;      // 0x006E72B8 - "darkness"
int g_ResTypeId_Metal;         // 0x006E72BC - "metal"
int g_ResTypeId_Cloth;         // 0x006E72C0 - "cloth"
int g_ResTypeId_Leather;       // 0x006E72C4 - "leather"
int g_ResTypeId_Wood;          // 0x006E72C8 - "wood"
int g_ResTypeId_Good;          // 0x006E72CC - "good"
int g_ResTypeId_Evil;          // 0x006E72D0 - "evil"
int g_ResTypeId_CarnivoreMeat; // 0x006E72D4 - "CARNIVOREMEAT"
int g_ResTypeId_Jewels;        // 0x006E72D8 - "jewels"
int g_ResTypeId_Danger;        // 0x006E72DC - "danger"
int g_ResTypeId_Bone;          // 0x006E72E0 - "bone"
int g_ResTypeId_Weapon;        // 0x006E72E4 - "weapon"

// Template classification globals (populated by InitTemplateClassification).
// g_RegionNameCount: number of distinct region names (0x006E7290).
int g_RegionNameCount; // 0x006E7290
// g_RegionNames: array of malloc'd region name strings (0x006E4290).
char *g_RegionNames[0x800]; // 0x006E4290
// g_RegionTemplateIds: per-region array of template IDs (0x006E0250).
uint16_t *g_RegionTemplateIds[0x800]; // 0x006E0250
// g_RegionTemplateCount: number of templates registered per region (0x006E2290).
uint16_t g_RegionTemplateCount[0x800]; // 0x006E2290
// g_RegionTemplateCapacity: allocated capacity per region (0x006E6290).
uint16_t g_RegionTemplateCapacity[0x800]; // 0x006E6290
// g_TemplateBodyTypes: per-template body type ID lists (0x006DC250).
// Each entry is a malloc'd block: [count:byte, id0:uint16, ..., id7:uint16].
uint8_t *g_TemplateBodyTypes[0x1000]; // 0x006DC250
// g_BuildingSpawnEntries flag (0x006E72F0).
int g_BuildingSpawnEntries; // 0x006E72F0

/*
 * Body type resource template serials (binary: 0x006213A0).
 * 13 entries, one per GetBodyTypeIndex category (0-12).
 * Each value is an item serial indexing into the resource entity
 * template array at g_World+0x1C (binary: 0x0064B36C).
 * Values read from binary .data section (6500-6521, category 12 = 0).
 */
uint32_t g_BodyTypeSerials[13] = {
	0x1964, 0x1965, 0x1966, 0x1967, // cat 0-3: cave, forest, grass, jungle
	0x1968, 0x1969, 0x196A, 0x196B, // cat 4-7: rock, sand, snow, crops
	0x196C,           // cat 8: water
	0x1977, 0x1978, 0x1979,         // cat 9-11: dirt, furrows, lava
	0x0000            // cat 12: unused
};

// ResEntitySlot typedef and g_ResEntitySlots extern are in egg.h.
#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(ResEntitySlot) == 0x1C, "ResEntitySlot size");
#endif

ResEntitySlot *g_ResEntitySlots; // binary: *(g_World+0x1C) at 0x0064B36C

/*
 * g_TemplateFlags - binary global byte array at 0x006E3290.
 * Indexed by templateId. Stores ClassifyTemplateItems result:
 *   0 -> no water resource
 *   1 -> has water resource only
 *   2 -> has water resource and other type-1 items
 */
int8_t g_TemplateFlags[0x10000]; // 0x006E3290

/*
 * Harvestable body type IDs (binary: 0x00621A20).
 * Zero-terminated array of 310 item body types (trees, stumps, logs, etc.)
 * that have harvesting scripts in the binary.
 * ProcessDynamicItemForSpawn (0x004B91B2) checks each item's bodyType against
 * this array and clears its script pointer (offset 0x48) if matched.
 * In our code, we don't have the wombat script engine, so the script-clearing
 * is a no-op. But we faithfully compare against the array.
 */
uint32_t g_HarvestableBodyTypes[] = {
	0x0970, 0x0971, 0x0972, 0x0973, 0x0978, 0x0979, 0x097B, 0x097C, 0x097D, 0x097E, 0x098C, 0x0994, 0x0995, 0x0996, 0x0997, 0x0998, 0x0999, 0x099A, 0x099B, 0x099F, 0x09A9,
	0x09AA, 0x09B3, 0x09B6, 0x09B7, 0x09B8, 0x09BB, 0x09BC, 0x09BF, 0x09C0, 0x09C1, 0x09C7, 0x09C8, 0x09C9, 0x09CA, 0x09CB, 0x09CC, 0x09CD, 0x09CE, 0x09CF, 0x09D0, 0x09D1,
	0x09D2, 0x09D3, 0x09D8, 0x09D9, 0x09DA, 0x09DB, 0x09E9, 0x09EA, 0x09EB, 0x09EE, 0x09EF, 0x09F0, 0x09F2, 0x09FA, 0x0A0F, 0x0A12, 0x0A15, 0x0A18, 0x0A1A, 0x0A1D, 0x0A22,
	0x0A25, 0x0A26, 0x0A27, 0x0A28, 0x0A29, 0x0A2A, 0x0A4C, 0x0A4D, 0x0A4E, 0x0A4F, 0x0A50, 0x0A51, 0x0A52, 0x0A53, 0x0A9D, 0x0A9E, 0x0B1A, 0x0B1D, 0x0B26, 0x0B2C, 0x0B2D,
	0x0B2E, 0x0B2F, 0x0B30, 0x0B31, 0x0B32, 0x0B33, 0x0B34, 0x0B35, 0x0B49, 0x0B4A, 0x0B4B, 0x0B4C, 0x0B4E, 0x0B4F, 0x0B50, 0x0B51, 0x0B52, 0x0B53, 0x0B54, 0x0B55, 0x0B56,
	0x0B57, 0x0B58, 0x0B59, 0x0B5A, 0x0B5B, 0x0B5C, 0x0B5D, 0x0B5E, 0x0B7C, 0x0B7D, 0x0B8F, 0x0B90, 0x0C3F, 0x0C40, 0x0C5C, 0x0C5D, 0x0C61, 0x0C62, 0x0C63, 0x0C6A, 0x0C6B,
	0x0C6C, 0x0C6D, 0x0C6E, 0x0C6F, 0x0C70, 0x0C71, 0x0C72, 0x0C73, 0x0C74, 0x0C75, 0x0C77, 0x0C78, 0x0C79, 0x0C7A, 0x0C7B, 0x0C7C, 0x0C7F, 0x0C80, 0x0C81, 0x0C82, 0x0C95,
	0x0C96, 0x0CA8, 0x0CAA, 0x0CAB, 0x0D16, 0x0D17, 0x0D18, 0x0D19, 0x0D1A, 0x0D94, 0x0D95, 0x0D96, 0x0D97, 0x0D98, 0x0D99, 0x0D9A, 0x0D9B, 0x0D9C, 0x0D9D, 0x0D9E, 0x0D9F,
	0x0DA0, 0x0DA1, 0x0DA2, 0x0DA3, 0x0DA4, 0x0DA5, 0x0DA6, 0x0DA7, 0x0DA8, 0x0DA9, 0x0DAA, 0x0DAB, 0x0DE0, 0x0E3E, 0x0E3F, 0x0E42, 0x0E43, 0x0E5A, 0x0E5B, 0x0E7D, 0x0E7E,
	0x0E9C, 0x0E9D, 0x0E9E, 0x0EB1, 0x0EB2, 0x0EB3, 0x0EB4, 0x0F06, 0x0F64, 0x0F6B, 0x0FF6, 0x0FF7, 0x0FF8, 0x0FF9, 0x0FFB, 0x0FFC, 0x0FFD, 0x0FFE, 0x0FFF, 0x1000, 0x1001,
	0x1002, 0x103B, 0x103C, 0x1040, 0x1041, 0x1044, 0x10FF, 0x1100, 0x15F9, 0x15FA, 0x15FB, 0x15FC, 0x15FE, 0x15FF, 0x1600, 0x1601, 0x1602, 0x1604, 0x1606, 0x1608, 0x160A,
	0x160B, 0x160C, 0x171D, 0x171E, 0x171F, 0x1720, 0x1721, 0x1722, 0x1723, 0x1724, 0x1725, 0x1726, 0x1727, 0x1728, 0x1729, 0x172A, 0x172B, 0x172C, 0x172D, 0x1853, 0x1854,
	0x1857, 0x1858, 0x1920, 0x1921, 0x1922, 0x1923, 0x1924, 0x1925, 0x1926, 0x1928, 0x192C, 0x192D, 0x192E, 0x192F, 0x1930, 0x1931, 0x1932, 0x1934, 0x1AD3, 0x1EBD, 0x1F14,
	0x1F15, 0x1F16, 0x1F17, 0x1F7D, 0x1F7E, 0x1F7F, 0x1F80, 0x1F81, 0x1F82, 0x1F83, 0x1F84, 0x1F85, 0x1F86, 0x1F87, 0x1F88, 0x1F89, 0x1F8A, 0x1F8B, 0x1F8C, 0x1F8D, 0x1F8E,
	0x1F8F, 0x1F90, 0x1F91, 0x1F92, 0x1F93, 0x1F94, 0x1F95, 0x1F96, 0x1F97, 0x1F98, 0x1F99, 0x1F9A, 0x1F9B, 0x1F9C, 0x1F9D, 0x1F9E,
	0x0000 // terminator
};

/*
 * 0x00421180 - CResourceType::GetInternalName
 *
 * Returns the resource type's internalName.
 */
char *
CResourceType_GetInternalName(CResourceType *resType)
{
	return resType->internalName;
}

/*
 * 0x004211C0 - CResourceType::GetFoodName
 *
 * Returns the foodName field. Paired with GetName1/GetName2/GetName3, which
 * return the shelter/desire/production names respectively.
 */
char *
CResourceType_GetFoodName(CResourceType *resType)
{
	return resType->foodName;
}

/*
 * 0x004211E0 - CResourceType::GetName1
 *
 * Returns the shelterName field.
 */
char *
CResourceType_GetName1(CResourceType *resType)
{
	return resType->shelterName;
}

/*
 * 0x00421200 - CResourceType::GetName2
 *
 * Returns the desireName field.
 */
char *
CResourceType_GetName2(CResourceType *resType)
{
	return resType->desireName;
}

/*
 * 0x00421220 - CResourceType::GetName3
 *
 * Returns the productionName field.
 */
char *
CResourceType_GetName3(CResourceType *resType)
{
	return resType->productionName;
}

/*
 * 0x0048510A - CEgg::Delete (vtable[0x90])
 *
 * Egg deletion only proceeds during the egg cleanup pass. Calls
 * DeleteCheck1 (no Check2), then invokes the scalar deleting destructor.
 */
void
CEgg_Delete(CItem *item)
{
	if (!g_ProcessingEggs)
		return;
	if (!CItem_DeleteCheck1(item))
		return;
	if (item != NULL)
		((void *(*)(void *, int))VT_FN(item, VT_DTOR))(item, 1);
}

/*
 * 0x004853BD - Spawn_ScheduleRespawn
 *
 * No-op stub in the demo binary. Its binary call sites (the CResourceMobile
 * destructor's orphaned-owner respawn fallback, CItem_ConsumeResource) therefore
 * do nothing. The closed economy returns resources to the regional bank
 * synchronously at the destruction site (RefundResourceNodesToBank for corpse
 * decay, Script_returnResourcesToBank for harvest), not through this stub.
 */
void
Spawn_ScheduleRespawn(CLocation *loc, int info1, int info2)
{
	USED(loc);
	USED(info1);
	USED(info2);
}

/*
 * 0x004853C2 - Noop_4853C2
 *
 * Empty function with no callers in the binary.
 */
static __attribute__((unused)) void
Noop_4853C2(void)
{
}

/*
 * Packed argument block (12 bytes) handed to Spawn_ReturnToOwner so the
 * callback can locate the owning egg node and credit back info2 units.
 */
struct SpawnReturnData {
	int16_t x;       // +0x00
	int16_t y;       // +0x02
	int16_t info1;   // +0x04
	int16_t info2;   // +0x06
	uint32_t serial; // +0x08
};

/*
 * 0x004853C7 - Spawn_ReturnToOwner
 *
 * Returns info2 resource amount to the spawn owner's matching node, or
 * defers to Spawn_ScheduleRespawn when the owner or node is missing.
 * No callers in the binary.
 */
static __attribute__((unused)) void
Spawn_ReturnToOwner(SpawnReturnData *data)
{
	CLocation loc;
	int info1, info2;
	uint32_t serial;
	CItem *spawnOwner;
	CResourceNode *node;

	CLocation_Init(&loc);
	CLocation_Set(&loc, data->x, data->y, 0);
	CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc.x, (int16_t)loc.y);

	info1 = (int32_t)data->info1;
	info2 = (int32_t)data->info2;
	serial = data->serial;

	spawnOwner = CWorld_FindBySerial(g_World, serial);
	if (spawnOwner != NULL) {
		node = CResourceEntity_FindNode(spawnOwner, info1, 3);
		if (node != NULL) {
			CResourceEntity_NotifyPreModify(spawnOwner);
			node->value3 += info2;
			CResourceEntity_NotifyPostModify(spawnOwner);
			CResourceEntity_NotifyPostModifyIfActive(spawnOwner);
		} else {
			Spawn_ScheduleRespawn(&loc, info1, info2);
		}
	} else {
		Spawn_ScheduleRespawn(&loc, info1, info2);
	}
}

/*
 * Binary NPC sub-category counts at 0x0068B388 and 0x0068B38C.
 * Subtracted from g_NormalNPCCount in IsOverloaded check (0x00436C1A).
 * These track vendor/invulnerable NPC counts that shouldn't be culled.
 * Not currently maintained - always 0.
 */
int g_NPCSubCount1; // 0x0068B388
int g_NPCSubCount2; // 0x0068B38C
// Binary: direction shuffle table at 0x0061BBB8 (4 ints, initially {0,1,2,3}).
int g_FindSpawnDirTable[4] = { 0, 1, 2, 3 };

/*
 * 0x00490557 - CEgg::~CEgg
 *
 * Restores the CEgg vtable, then chains to CItem_Destructor.
 */
void
CEgg_Destructor(CItem *egg)
{
	egg->resourceEntity.entity.vtable = &g_vtable_CEgg;
	CItem_Destructor(egg);
}

/*
 * 0x004905B5 - CEgg::DropAtFeet (vtable[0x04])
 *
 * Calls CLocation_IsEqualXYZ (result discarded) when nextInContainer holds
 * a valid CLocation, then forwards to CItem_DropAtFeet.
 */
void
CEgg_DropAtFeet(CItem *self, CLocation *loc)
{
	if (!CLocation_IsInvalid((CLocation *)&self->resourceEntity.nextInContainer))
		CLocation_IsEqualXYZ(loc, (CLocation *)&self->resourceEntity.nextInContainer);
	CItem_DropAtFeet(self, loc);
}

/*
 * 0x004905EC - CEgg::SetLocation (vtable[0x08])
 *
 * Calls CLocation_IsEqualXYZ (result discarded) when nextInContainer holds
 * a valid CLocation, then forwards to CItem_SetLocation_VT.
 */
void
CEgg_SetLocation(CItem *self, CLocation *loc)
{
	if (!CLocation_IsInvalid((CLocation *)&self->resourceEntity.nextInContainer))
		CLocation_IsEqualXYZ(loc, (CLocation *)&self->resourceEntity.nextInContainer);
	CItem_SetLocation_VT(self, loc);
}

/*
 * 0x004913B0 - CEgg scalar deleting destructor
 *
 * Scalar deleting destructor: runs CEgg_Destructor and frees the object
 * when flags & 1.
 */
void *
CEgg_ScalarDelete(CItem *self, int flags)
{
	CEgg_Destructor(self);
	if (flags & 1)
		free(self);
	return NULL;
}

/*
 * 0x0049DDA0 - CResourceType::SetInternalName
 *
 * Copies name into the internalName field.
 */
void
CResourceType_SetInternalName(CResourceType *this, const char *name)
{
	strcpy(this->internalName, name);
}

/*
 * 0x0049DDC0 - CResourceType::SetFoodName
 *
 * Copies name into the foodName field.
 */
void
CResourceType_SetFoodName(CResourceType *this, const char *name)
{
	strcpy(this->foodName, name);
}

/*
 * 0x0049DDF0 - CResourceType::SetShelterName
 *
 * Copies name into the shelterName field.
 */
void
CResourceType_SetShelterName(CResourceType *this, const char *name)
{
	strcpy(this->shelterName, name);
}

/*
 * 0x0049DE20 - CResourceType::SetDesireName
 *
 * Copies name into the desireName field.
 */
void
CResourceType_SetDesireName(CResourceType *this, const char *name)
{
	strcpy(this->desireName, name);
}

/*
 * 0x0049DE50 - CResourceType::SetProductionName
 *
 * Copies name into the productionName field.
 */
void
CResourceType_SetProductionName(CResourceType *this, const char *name)
{
	strcpy(this->productionName, name);
}

/*
 * 0x0049DE80 - CResourceType::SetField288
 *
 * Stores val into field_288.
 */
void
CResourceType_SetField288(CResourceType *this, int val)
{
	this->field_288 = val;
}

/*
 * 0x0049DEA0 - CResourceType::SetField28C
 *
 * Stores val into field_28C.
 */
void
CResourceType_SetField28C(CResourceType *this, int val)
{
	this->field_28C = val;
}

/*
 * 0x0049DEE0 - CResourceType::GetField288
 *
 * Returns field_288.
 */
int
CResourceType_GetField288(CResourceType *resType)
{
	return resType->field_288;
}

/*
 * 0x0049DF00 - CResourceType::GetField28C
 *
 * Returns field_28C.
 */
int
CResourceType_GetField28C(CResourceType *resType)
{
	return resType->field_28C;
}

/*
 * 0x004A83B0 - StaticInit_HashResTypeName
 *
 * Static-init wrapper that constructs the global CResourceTypeManager.
 */
static __attribute__((unused)) void
StaticInit_HashResTypeName(void)
{
	CResourceTypeManager_Constructor();
}

/*
 * 0x004A841F - CResourceType::CResourceType
 *
 * Sets the five "No ..." default name strings, assigns an auto-incremented
 * typeId via AddResourceType, and sets field_288=1, field_28C=0.
 */
CResourceType *
CResourceType_Constructor(CResourceType *this)
{
	CResourceType_SetInternalName(this, "No internal name");
	CResourceType_SetFoodName(this, "No food name");
	CResourceType_SetShelterName(this, "No shelter name");
	CResourceType_SetDesireName(this, "No desire name");
	CResourceType_SetProductionName(this, "No production name");

	if (g_ResourceTypeCount >= MAX_RESOURCE_TYPES)
		this->typeId = g_ResourceTypeCount - 1;
	else {
		this->typeId = g_ResourceTypeCount;
		g_ResourceTypeCount++;
	}
#if __SIZEOF_POINTER__ == 8
	this->_typeId_pad64 = 0;
#endif

	CResourceType_SetField288(this, 1);
	CResourceType_SetField28C(this, 0);
	return this;
}

/*
 * 0x004A84C7 - CResourceType::Save
 *
 * Writes a CResourceType to restypes.mul:
 *   typeId(4) + 5 name[128] fields + field_288(4) + field_28C(4).
 * All int32 fields are swapped to big-endian before writing.
 */
void
CResourceType_Save(CResourceType *this, FILE *fp)
{
	int tmp;

	tmp = this->typeId;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	fwrite_ServerSide(this->internalName, 0x80, 1, fp);
	fwrite_ServerSide(this->foodName, 0x80, 1, fp);
	fwrite_ServerSide(this->shelterName, 0x80, 1, fp);
	fwrite_ServerSide(this->desireName, 0x80, 1, fp);
	fwrite_ServerSide(this->productionName, 0x80, 1, fp);

	tmp = this->field_288;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = this->field_28C;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);
}

/*
 * 0x004A85E4 - CResourceTypeManager::CResourceTypeManager
 *
 * Zeroes entries/hashTable and seeds count=1. Decomposed into globals in
 * our code and inlined into LoadResTypes.
 */
static void
CResourceTypeManager_Constructor(void)
{
	g_ResourceTypeCount = 1;
	memset(g_ResTypeEntries, 0, sizeof(g_ResTypeEntries));
	memset(g_ResTypeHashTable, 0, sizeof(g_ResTypeHashTable));
}

/*
 * restypes.mul format used by LoadResTypes (0x004E190A):
 *   uint32 count (101 in demo)
 *   count records of 652 bytes each:
 *     +0x00: uint32 id (1-based)
 *     +0x04: char[128] internalName
 *     +0x84: char[128] foodName
 *     +0x104: char[128] shelterName
 *     +0x184: char[128] desireName
 *     +0x204: char[128] productionName
 *     +0x284: uint32 field_288
 *     +0x288: uint32 field_28C
 */

/*
 * 0x004A8684 - HashResTypeName
 *
 * Returns ((tolower(name[0]) << 3) ^ tolower(name[1])) & 0x1FF.
 */
int
HashResTypeName(const char *name)
{
	int h;

	h = tolower((unsigned char)name[0]) << 3;
	h ^= tolower((unsigned char)name[1]);
	h &= 0x1FF;
	return h;
}

/*
 * 0x004A86B9 - CResourceTypeManager::SendAll
 *
 * GM editor: sends a RESTYPE_DATA packet for every registered resource type.
 */
void
CResourceTypeManager_SendAll(CItem *player)
{
	int i;
	uint8_t buf[0x298];
	CResourceType *entry;

	for (i = 0; i < 0x66; i++) {
		entry = g_ResTypeEntries[i];
		if (entry == NULL)
			continue;
		MakePacket_RESTYPE_DATA(buf, CSearchCtx_GetBucket((CSearchCtx *)entry));
		SendToClient(player, buf, -1);
	}
}

/*
 * 0x004A872D - CResourceTypeManager::RegisterType
 *
 * Stores rt in the entries array at its bucket index and appends it to the
 * hash chain for its internal name.
 */
void
CResourceTypeManager_RegisterType(CResourceType *rt)
{
	int hash;
	CResourceType *tail;

	CSearchCtx_GetBucket((CSearchCtx *)rt);
	g_ResTypeEntries[CSearchCtx_GetBucket((CSearchCtx *)rt)] = rt;

	hash = HashResTypeName(CResourceType_GetInternalName(rt));
	tail = g_ResTypeHashTable[hash];
	while (tail != NULL && tail->next != NULL)
		tail = tail->next;
	if (tail != NULL)
		tail->next = rt;
	else
		g_ResTypeHashTable[HashResTypeName(CResourceType_GetInternalName(rt))] = rt;
	rt->next = NULL;
}

/*
 * 0x004A87C3 - CResourceTypeManager::FindByName
 *
 * Walks the hash chain with a case-insensitive name compare.
 */
CResourceType *
CResourceTypeManager_FindByName(const char *name)
{
	int hash;
	CResourceType *node;

	hash = HashResTypeName(name);
	node = g_ResTypeHashTable[hash];
	while (node != NULL) {
		if (strcasecmp(CResourceType_GetInternalName(node), name) == 0)
			return node;
		node = node->next;
	}
	return NULL;
}

/*
 * 0x004A8875 - CResourceTypeManager::GetId
 *
 * Returns entries[index], populated by RegisterType during LoadResTypes.
 */
CResourceType *
CResourceTypeManager_GetId(int index)
{
	return g_ResTypeEntries[index];
}

/*
 * Convenience macro matching binary calling convention.
 * Binary callers pass __FILE__ and __LINE__ equivalents.
 */
#define ValidateTemplateId(idx) ValidateResId((idx), "D:\\TornadoAlley\\Projects\\UltimaOnline\\area\\resbank.cxx", __LINE__)

/*
 * 0x004B3180 - CEgg::CEgg
 *
 * Chains to CItem, installs the CEgg vtable, and sets ItemFlag_ServerOnly.
 */
CItem *
CEgg_Constructor(CItem *egg)
{
	CItem_Constructor(egg);
	egg->resourceEntity.entity.vtable = &g_vtable_CEgg;
	egg->itemFlags |= ItemFlag_ServerOnly;
	return egg;
}
