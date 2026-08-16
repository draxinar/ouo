#ifndef WORLD_H_
#define WORLD_H_

#include <stdint.h>

#include "terrain.h"

__extension__ typedef struct CEntity CEntity;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CWorld CWorld;
__extension__ typedef struct TileDataEntry TileDataEntry;

#define TILEDATA_MAX_ITEMS 0x4000

/*
 * One row of the gump-dimensions table at 0x006F0618.
 */
typedef struct GumpDimEntry {
	uint16_t width;
	uint16_t height;
} GumpDimEntry;

/*
 * Singleton at g_World (0x0064B350). Bundles the unified serial counter,
 * the shared tile/hue/anim data pointers, the global 65536-bucket entity
 * hash table (keyed by serial & 0xFFFF, chained through CItem.hashNext),
 * per-kind entity counts, the 0x1000-entry template spawn chain, and the
 * five CCriticalSection slots guarding the shared data tables.
 */
// clang-format off
struct CWorld {
	uint32_t nextSerial;                 // +0x00
	uint32_t ready;                      // +0x04 (set by CDataManager_SetReady)
	uint32_t isLoading;                  // +0x08
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;                     // 64-bit alignment pad
#endif
	void *animData;                      // +0x0C
	void *hueData;                       // +0x10
	void *itemTileData;                  // +0x14
	void *landTileData;                  // +0x18
	void *resEntitySlots;                // +0x1C
	void *hueDataExp;                    // +0x20
	CItem *hashTable[0x10000];           // +0x24
	uint32_t playerCreateCount;          // +0x40024
	uint32_t npcCount;                   // +0x40028
	uint32_t entityCount;                // +0x4002C
	uint32_t staticItemCount;            // +0x40030
	uint32_t dynamicItemCount;           // +0x40034
	uint32_t npcSubCount1;               // +0x40038
	uint32_t npcSubCount2;               // +0x4003C
	uint32_t resourceEntityCount;        // +0x40040
	uint32_t bboardCount;                // +0x40044
	uint32_t signpostCount;              // +0x40048
	uint32_t mobileCount;                // +0x4004C
	uint32_t containerCount;             // +0x40050
	uint32_t normalNPCCount;             // +0x40054
	uint32_t templateChain[0x1000];      // +0x40058
	uint32_t templateChainCount[0x1000]; // +0x44058
	uint32_t bboardHead;                 // +0x48058
	uint8_t _pad_4805C[0x0C];            // +0x4805C
	uint8_t decayMax;                    // +0x48068
	uint8_t _pad_48069[0x03];            // +0x48069
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64_cs[4];                // 64-bit alignment pad
#endif
	void *critSectTileData1;             // +0x4806C (LockTileData)
	void *critSectTileData2;             // +0x48070 (LockTileData)
	void *critSectAnimData;              // +0x48074 (LockAnimData)
	void *critSectHueData1;              // +0x48078 (LockHueData)
	void *critSectHueData2;              // +0x4807C (LockHueData)
	uint32_t _pad_48080;                 // +0x48080
};
// clang-format on

extern int g_WorldActive; // 0x0061DA80
extern int g_WorldActive2; // 0x0061DA84 (1 during normal operation, 0 during bulk delete)
extern int g_DeleteAllowed; // 0x00645B04
extern int g_isLoadingWorld; // 0x00699B64
extern int g_hasLoadedWorld; // 0x00699B68
extern GumpDimEntry g_GumpDimTable[TILEDATA_MAX_ITEMS]; // 0x006F0618
extern CWorld *g_World;
extern TileDataEntry *g_ItemTileData;

uint8_t CWorld_GetHomeDecayRate(void); // 0x00421240
uint8_t CWorld_GetNonHomeDecayRate(void); // 0x00421260
uint8_t CWorld_GetDecayMax(void); // 0x00421280
uint32_t CWorld_GetDecayInterval(void); // 0x004212A0
uint32_t CWorld_GetDecayBucketsPerTick(void); // 0x004212C0
void CWorld_InitDecay(int mode); // 0x0045960F
void CWorld_DecayTick(int gameTick); // 0x004596EB
int World_IsEntityInHash(CItem *item); // 0x00485160
void World_PreloadPlayerSerial(uint32_t serial); // 0x004851AC
int World_FindBySerial_Either(uint32_t serial); // 0x004851B1
uint8_t CWorld_GetItemTileQuantity(uint16_t id); // 0x0048528A
uint8_t CWorld_GetResourceWeight(uint16_t bodyType); // 0x004852B0
void CDataManager_Init(CWorld *this); // 0x0048B675
void CDataManager_SetLockdown(CItem *this, int set); // 0x0048B65C
CItem *CWorld_FindBySerial(CWorld *world, uint32_t serial); // 0x0048B8CA
CItem *CWorld_FindNPCBySerial(CWorld *world, uint32_t serial); // 0x0048B910
void CItem_SetSerialField(CItem *item, uint32_t serial); // 0x0048527C
CItem *CWorld_FindMobileBySerial(CWorld *world, uint32_t serial); // 0x0048B94D
uint32_t CWorld_AllocSerial(CWorld *world); // 0x0048D175
CItem *CWorld_FindEntityInRange(CWorld *world, CEntity *refEntity, uint32_t serial, int range); // 0x0048D9FC
CItem *FindHighestItemAtXY(CWorld *world, int16_t x, int16_t y); // 0x0048DBDB
CItem *CWorld_CreateItem(CWorld *world, uint16_t bodyType); // 0x0048E39F
CItem *CWorld_CreateContainerItem(CWorld *world, uint16_t bodyType); // 0x0048E3FA
int CItem_ClassifyEntityByVtable(CItem *this); // 0x0048E664
int CWorld_IsValidItemResource(uint16_t bodyType); // 0x0048ED04
int CWorld_LookupItemResource(uint16_t bodyType); // 0x0048ED25
void CDataManager_LockTileData(void); // 0x0048EE64
void CDataManager_UnlockTileData(void); // 0x0048EE8B
void CDataManager_LockAnimData(void); // 0x0048EEE8
void CDataManager_UnlockAnimData(void); // 0x0048EF01
void CDataManager_LockHueData(void); // 0x0048EFF6
void CDataManager_UnlockHueData(void); // 0x0048F01D
void CWorld_Lock(void); // 0x0048F044
void CWorld_Unlock(void); // 0x0048F04F
CItem *World_ValidateEntity(CItem *expected, uint32_t serial); // 0x00492120
CItem *World_ValidateMobileEntity(CItem *expected, uint32_t serial); // 0x00492148
CItem *World_ValidatePlayerEntity(CItem *expected, uint32_t serial); // 0x00492188
CItem *World_ValidateNPCEntity(CItem *expected, uint32_t serial); // 0x004921C5
CItem *World_ValidateWeaponEntity(CItem *expected, uint32_t serial); // 0x00492205
CItem *World_ValidateContainerEntity(CItem *expected, uint32_t serial); // 0x00492245
CItem *World_ValidateSpatialEntity(CItem *expected, uint32_t serial); // 0x00492285
CItem *World_ValidateBookEntity(CItem *expected, uint32_t serial); // 0x004922C5
void CWorld_InsertEntity(CWorld *world, CItem *entity);
uint32_t CWorld_GetItemTileFlags(uint16_t bodyType);
void World_ShutdownEntities(void); // CUSTOM

#endif /* WORLD_H_ */
