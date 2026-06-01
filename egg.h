#ifndef EGG_H_
#define EGG_H_

#include <stdint.h>
#include <stdio.h>

/*
 * Spawn-egg support: CResBankManager (at 0x006DBD60) manages
 * CResBankRegion objects loaded from resbank.mul, and CEgg items
 * persisted in dynamic0.mul drive per-region repopulation.
 *
 * Source: D:\TornadoAlley\Projects\UltimaOnline\area\resbank.cxx
 */

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CResourceNode CResourceNode;
#define MAX_RESOURCE_TYPES 102

__extension__ typedef struct CDefcon CDefcon;
__extension__ typedef struct CResBankRegion CResBankRegion;
__extension__ typedef struct CResBankManager CResBankManager;
__extension__ typedef struct ResSpawnEntry ResSpawnEntry;
__extension__ typedef struct SubRegion SubRegion;
__extension__ typedef struct CPlayer CPlayer;

/*
 * Named bounding box inside a CResBankRegion (0x36 bytes). Stored in
 * CResBankRegion.templateDb and consumed by SpawnInSubRegion to limit
 * density per sub-area.
 */
struct SubRegion {
	uint16_t regionNameIndex; // 0x00 - index into g_RegionNames
	uint16_t x1;              // 0x02 - west bound
	uint16_t y1;              // 0x04 - north bound
	uint16_t x2;              // 0x06 - east bound
	uint16_t y2;              // 0x08 - south bound
	int16_t z1;               // 0x0A - min altitude
	int16_t z2;               // 0x0C - max altitude
	char name[40];            // 0x0E - name string (e.g. "SCALING")
};

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(SubRegion) == 0x36, "SubRegion size mismatch (expected 0x36)");
#endif

// Maximum template slots per region (binary: 0x66 = 102).
#define RESBANK_MAX_TEMPLATES 102

// resbank.mul magic number.
#define RESBANK_MAGIC 0x4ACE0001

/*
 * Weighted spawn entry in CResBankRegion.spawnEntries (0x10 bytes). Each
 * entry pairs an NPC template with per-sub-region scaling weights used by
 * the frequency-based selector.
 */
struct ResSpawnEntry {
	uint16_t templateId;        // 0x00
	uint16_t frequency;         // 0x02
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;            // 64-bit alignment pad
#endif
	uint16_t *subRegionIds;     // 0x04
	uint16_t *scalingWts;       // 0x08
	uint16_t subRegionCapacity; // 0x0C
	uint16_t numSubRegions;     // 0x0E
};

/*
 * Per-region spawn state (0x14DF8 bytes). Holds the NPC template slots,
 * weighted spawn entries, bounding box, and respawn bookkeeping for one
 * region in g_ResBankManager's linked list. Constructor 0x004AEA00.
 */
// clang-format off
struct CResBankRegion {
	// Indexed by RESOURCE TYPE id (parsed from restypes.mul) across
	// every reader and writer:
	//   - LoadBankDefs writes baselines keyed by restypes.mul id.
	//   - CResBankManager_InitRespawn loops by the same id space.
	//   - Under FEAT_CLOSED_ECONOMY, DeductSpawnFromBank writes
	//     quantities[node->id]; production-node ids on templates
	//     (templatestable.dat) ARE resource-type ids, so the index
	//     spaces are the same. Keep them aligned: if a future change
	//     introduces a separate node id space, this slot would need
	//     to be re-indexed.
	int32_t quantities[RESBANK_MAX_TEMPLATES];          // 0x000 - spawn quota per resource-type slot
	int32_t x1;                                         // 0x198
	int32_t y1;                                         // 0x19C
	int32_t x2;                                         // 0x1A0
	int32_t y2;                                         // 0x1A4
	CResBankRegion *next;                               // 0x1A8
	CResBankRegion *prev;                               // 0x1AC
	char name[128];                                     // 0x1B0
	int32_t regionIndex;                                // 0x230 - hash table index
	int32_t minRespawnTime;                             // 0x234
	int32_t maxRespawnTime;                             // 0x238
	int32_t needsRebuild;                               // 0x23C - set by ctor to 1
	int32_t competitionStockCache[0x4000];              // 0x240 - per-body-type vendor stock
	int32_t spawnedCounts[RESBANK_MAX_TEMPLATES];       // 0x10240
	int32_t field103D8[RESBANK_MAX_TEMPLATES];          // 0x103D8
	int32_t maxSpawns[RESBANK_MAX_TEMPLATES];           // 0x10570
	int32_t templateHash[0x1000];                       // 0x10708 - 4096-dword hash
	uint16_t respawnTemplateId[RESBANK_MAX_TEMPLATES];  // 0x14708 - 0xFFFF = empty
	uint16_t respawnAmount[RESBANK_MAX_TEMPLATES];      // 0x147D4
	uint8_t respawnCountdown[RESBANK_MAX_TEMPLATES];    // 0x148A0
	uint16_t _pad14906;                                 // 0x14906
	void *templateDb;                                   // 0x14908 - array of SubRegion
	int32_t templateDbCapacity;                         // 0x1490C
	int32_t templateDbCount;                            // 0x14910
	ResSpawnEntry *spawnEntries;                        // 0x14914
	int32_t spawnEntryCapacity;                         // 0x14918
	int32_t entryCount;                                 // 0x1491C
	int32_t totalFrequency;                             // 0x14920 - sum of entry frequencies
	int32_t field14924;                                 // 0x14924
	int32_t field14928;                                 // 0x14928
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64_1492C;                              // 64-bit alignment pad
#endif
	uintptr_t classEntryIndices[RESBANK_MAX_TEMPLATES]; // 0x1492C
	int32_t classTotalFrequency[RESBANK_MAX_TEMPLATES]; // 0x14AC4
	uint16_t classCapacity[RESBANK_MAX_TEMPLATES];      // 0x14C5C
	uint16_t classCount[RESBANK_MAX_TEMPLATES];         // 0x14D28
	uint8_t nospawn;                                    // 0x14DF4
	uint8_t noWander;                                   // 0x14DF5
	uint16_t _pad14DF6;                                 // 0x14DF6
};
// clang-format on

// Static assert: CResBankRegion must be exactly 0x00014DF8 bytes.
#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CResBankRegion) == 0x14DF8, "CResBankRegion size mismatch (expected 0x14DF8)");
#endif

/*
 * Global spawn director (g_ResBankManager at 0x006DBD60). Owns the
 * linked list and hash table of CResBankRegion objects and drives
 * SpawnTick iteration.
 */
struct CResBankManager {
	CResBankRegion *first;          // 0x00
	CResBankRegion *last;           // 0x04
	CResBankRegion *current;        // 0x08 - SpawnTick cursor
	CResBankRegion *noRegion;       // 0x0C - "No Region" fallback
	CResBankRegion *hashTable[256]; // 0x10
	int32_t regionCount;            // 0x410
	int32_t maxPerTick;             // 0x414 - init 32
	int32_t listCount;              // 0x418
	uint8_t _pad41C[0x1C];          // 0x41C
	int32_t respawnChunkTimer;      // 0x438 - init -1
	int32_t respawnDelay;           // 0x43C - init 100
};

/*
 * Named resource type used by the ecology system (0x290 bytes). One per
 * entry in g_ResTypeEntries; chained by hash on internalName.
 */
// clang-format off
__extension__ typedef struct CResourceType {
	struct CResourceType *next;     // 0x00
	int typeId;                     // 0x04
#if __SIZEOF_POINTER__ == 8
	int _typeId_pad64;              // match CSearchCtx.bucket width on 64-bit
#endif
	char internalName[128];         // 0x08
	char foodName[128];             // 0x88
	char shelterName[128];          // 0x108
	char desireName[128];           // 0x188
	char productionName[128];       // 0x208
	int field_288;                  // 0x288 - defaults to 1
	int field_28C;                  // 0x28C
} CResourceType;
// clang-format on

// Hash table for CResourceTypeManager::FindByName (binary: this+0x08).
// 512 buckets, hash = ((tolower(name[0]) << 3) ^ tolower(name[1])) & 0x1FF.
#define RES_TYPE_HASH_SIZE 512

/*
 * Thin shim over CResourceEntity at g_World+0x1C (0x0064B36C). Lets
 * getResource() and AddResourceNodesByCategory walk the resource-node
 * list head without pulling in the full entity layout.
 */
__extension__ typedef struct ResEntitySlot {
#if __SIZEOF_POINTER__ == 8
	uint8_t pad[0x28]; // must match offsetof(CResourceEntity, firstChild) on 64-bit
#else
	uint8_t pad[0x18]; // must match offsetof(CResourceEntity, firstChild) on 32-bit
#endif
	CResourceNode *nodeHead; // resource node list head
} ResEntitySlot;

__extension__ typedef struct CResBankSet CResBankSet;
__extension__ typedef struct CResBankSetMember CResBankSetMember;
__extension__ typedef struct CResBankVertex CResBankVertex;

__extension__ typedef struct TerrainRule TerrainRule;

extern int g_FindSpawnDirTable[4]; // 0x0061BBB8
extern int g_SpawnEnabled; // 0x00621398

// CUSTOM: per-NPC home-location respawn queue (see egg.c).
typedef struct PendingNPCRespawn {
	struct PendingNPCRespawn *next;
	uint16_t templateId;
	int16_t x, y;
	int8_t z;
	uint32_t fireTickMs;
} PendingNPCRespawn;

extern PendingNPCRespawn *g_pendingNPCRespawnHead;
extern int g_PerNPCRespawnDelayMs;

// CUSTOM: refund an item's type-3 nodes to the regional bank. Called from
// destruction sites under FEAT_CLOSED_ECONOMY. Defined in resbank.c.
void RefundResourceNodesToBank(CItem *item);

// CUSTOM: deduct a template's type-3 production nodes from the regional bank.
// Called from every NPC spawn path under FEAT_CLOSED_ECONOMY. Defined in resbank.c.
void DeductSpawnFromBank(uint16_t templateId, CLocation *loc);

void PendingNPCRespawn_Enqueue(uint16_t templateId, int16_t x, int16_t y, int8_t z);
void PendingNPCRespawn_Tick(void);
extern uint32_t g_HarvestableBodyTypes[]; // 0x00621A20
extern uint32_t g_ValidateInWorldBitmap[512]; // 0x0063D8F8
extern uint32_t g_BodyTypeSerials[13]; // 0x0064B36C
extern ResEntitySlot *g_ResEntitySlots; // 0x0064B36C
extern int g_NPCSubCount1; // 0x0068B388
extern int g_NPCSubCount2; // 0x0068B38C
extern int g_TemplatesEnabled; // 0x00698310
extern int g_SuppressRespawn; // 0x006CA8E8
extern int g_ProcessingEggs; // 0x006CA908
extern CResBankManager g_ResBankManager; // 0x006DBD60
extern int g_SpawnAttemptCount; // 0x006DC17C
extern int g_SpawnNoTemplateCount; // 0x006DC180
extern int g_SpawnSkipCount; // 0x006DC184
extern int g_SpawnDensityRejectCount; // 0x006DC188
extern int g_SpawnCanSpawnRejectCount; // 0x006DC18C
extern int g_SpawnLocationFailCount; // 0x006DC190
extern int g_SpawnCreateFailCount; // 0x006DC194
extern int g_ResourceTypeCount; // 0x006DC1A0
extern CResourceType *g_ResTypeEntries[MAX_RESOURCE_TYPES]; // 0x006DC1A4
extern uint8_t *g_TemplateBodyTypes[0x1000]; // 0x006DC250
extern uint16_t *g_RegionTemplateIds[0x800]; // 0x006E0250
extern char g_TemplateNameBuf[64]; // 0x006E2250
extern uint16_t g_RegionTemplateCount[0x800]; // 0x006E2290
extern int8_t g_TemplateFlags[0x10000]; // 0x006E3290
extern char *g_RegionNames[0x800]; // 0x006E4290
extern uint16_t g_RegionTemplateCapacity[0x800]; // 0x006E6290
extern int g_RegionNameCount; // 0x006E7290
extern int g_ResTypeId_Meat; // 0x006E7294
extern int g_ResTypeId_Water; // 0x006E7298
extern int g_ResTypeId_Self; // 0x006E729C
extern int g_ResTypeId_Humans; // 0x006E72A0
extern int g_ResTypeId_Feathers; // 0x006E72A4
extern int g_ResTypeId_Hide; // 0x006E72A8
extern int g_ResTypeId_Wool; // 0x006E72AC
extern int g_ResTypeId_Gold; // 0x006E72B0
extern int g_ResTypeId_Magic; // 0x006E72B4
extern int g_ResTypeId_Darkness; // 0x006E72B8
extern int g_ResTypeId_Metal; // 0x006E72BC
extern int g_ResTypeId_Cloth; // 0x006E72C0
extern int g_ResTypeId_Leather; // 0x006E72C4
extern int g_ResTypeId_Wood; // 0x006E72C8
extern int g_ResTypeId_Good; // 0x006E72CC
extern int g_ResTypeId_Evil; // 0x006E72D0
extern int g_ResTypeId_CarnivoreMeat; // 0x006E72D4
extern int g_ResTypeId_Jewels; // 0x006E72D8
extern int g_ResTypeId_Danger; // 0x006E72DC
extern int g_ResTypeId_Bone; // 0x006E72E0
extern int g_ResTypeId_Weapon; // 0x006E72E4
extern int g_SpawningInProgress; // 0x006E72EC
extern int g_BuildingSpawnEntries; // 0x006E72F0
extern int g_IsInitialSpawn; // 0x006E7654
extern CResourceType *g_ResTypeHashTable[RES_TYPE_HASH_SIZE];
extern char *g_ResourceTypeNames[MAX_RESOURCE_TYPES];

char *CResourceType_GetInternalName(CResourceType *resType); // 0x00421180
char *CResourceType_GetFoodName(CResourceType *resType); // 0x004211C0
char *CResourceType_GetName1(CResourceType *resType); // 0x004211E0
char *CResourceType_GetName2(CResourceType *resType); // 0x00421200
char *CResourceType_GetName3(CResourceType *resType); // 0x00421220
void CResourceType_SetInternalName(CResourceType *this, const char *name); // 0x0049DDA0
void CResourceType_SetFoodName(CResourceType *this, const char *name); // 0x0049DDC0
void CResourceType_SetShelterName(CResourceType *this, const char *name); // 0x0049DDF0
void CResourceType_SetDesireName(CResourceType *this, const char *name); // 0x0049DE20
void CResourceType_SetProductionName(CResourceType *this, const char *name); // 0x0049DE50
void CResourceType_SetField288(CResourceType *this, int val); // 0x0049DE80
void CResourceType_SetField28C(CResourceType *this, int val); // 0x0049DEA0
void CEgg_Delete(CItem *item); // 0x0048510A
void CEgg_Destructor(CItem *egg); // 0x00490557
void CEgg_DropAtFeet(CItem *self, CLocation *loc); // 0x004905B5
void CEgg_SetLocation(CItem *self, CLocation *loc); // 0x004905EC
void *CEgg_ScalarDelete(CItem *self, int flags); // 0x004913B0
int CResourceType_GetField288(CResourceType *resType); // 0x0049DEE0
int CResourceType_GetField28C(CResourceType *resType); // 0x0049DF00
CResourceType *CResourceType_Constructor(CResourceType *this); // 0x004A841F
void CResourceType_Save(CResourceType *this, FILE *fp); // 0x004A84C7
int HashResTypeName(const char *name); // 0x004A8684
void CResourceTypeManager_RegisterType(CResourceType *rt); // 0x004A872D
void CResourceTypeManager_SendAll(CItem *player); // 0x004A86B9
CResourceType *CResourceTypeManager_FindByName(const char *name); // 0x004A87C3
CResourceType *CResourceTypeManager_GetId(int index); // 0x004A8875
CItem *CEgg_Constructor(CItem *egg); // 0x004B3180
void Spawn_ScheduleRespawn(CLocation *loc, int info1, int info2); // 0x004853BD

#endif /* EGG_H_ */
