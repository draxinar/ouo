#ifndef TERRAIN_H_
#define TERRAIN_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CTerrainManager CTerrainManager;
__extension__ typedef struct TileDataEntry TileDataEntry;
__extension__ typedef struct MapCell MapCell;
__extension__ typedef struct MapBlock MapBlock;
__extension__ typedef struct LandTileData LandTileData;
__extension__ typedef struct StaticEntry StaticEntry;
__extension__ typedef struct StaticItem StaticItem;
__extension__ typedef struct SurfaceInfo SurfaceInfo;

/*
 * Per-bodyType properties in the item tiledata table (40 bytes).
 * g_World+0x14 points at a 0x4000-entry array populated at startup via
 * StoreTileDataEntry.
 */
struct TileDataEntry {
	uint32_t flags;     // 0x00
	uint8_t weight;     // 0x04
	uint8_t layer;      // 0x05 (equipment layer / quality)
	uint8_t _td_pad[2]; // 0x06
	uint32_t miscData;  // 0x08
	uint16_t value1;    // 0x0C
	uint16_t value2;    // 0x0E
	uint16_t height;    // 0x10
	uint8_t quantity;   // 0x12
	char name[21];      // 0x13
};

/*
 * TileDataEntry.flags bits checked by the binary on items.
 */
enum TileFlag {
	TF_ARTICLE_A = 0x00004000,    // "a " article prefix
	TF_ARTICLE_AN = 0x00008000,   // "an " article prefix
	TF_ARTICLE_MASK = 0x0000C000, // both bits: "the " article prefix
	TF_WEAPON = 0x00000002,
	TF_STACKABLE = 0x00000800,
	TF_MAP = 0x00100000,          // map/signpost item
	TF_CONTAINER = 0x00200000,
	TF_EQUIPPABLE = 0x00400000,
	TF_LIGHT = 0x00800000,
	TF_ARMOR = 0x08000000,
	TF_DOOR = 0x20000000,
};

/*
 * One land cell in the spatial grid (4 bytes in memory, 3 bytes on disk).
 * On disk in map0.mul: 2-byte tileID + 1-byte Z.
 */
struct MapCell {
	uint16_t tileID; // +0x00
	int8_t z;        // +0x02
	uint8_t _pad;    // +0x03
};

/*
 * One 8x8 block of the spatial grid (284 bytes) at g_MapBlocks
 * (*(0x006933E4)), indexed by block index * 0x11C. The cell array is
 * row-major (y*8 + x) and is followed by per-block entity list heads and
 * weather/light overrides.
 */
struct MapBlock {
	MapCell cells[64];      // +0x000
	CItem *staticHead;      // +0x100 (chain via CResourceEntity.nextInContainer)
	CItem *itemHead;        // +0x104 (chain via CItem.spatialNext)
	CItem *eggHead;         // +0x108 (CEgg list, used by BuildSpawnEntries)
	void *trackingHead;     // +0x10C (CBlockTrackingNode list)
	uint8_t flags110;       // +0x110
	uint8_t flags111;       // +0x111
	uint16_t weatherSeason; // +0x112
	uint16_t weatherDay;    // +0x114
	uint16_t weatherNight;  // +0x116
	int16_t lightOverride;  // +0x118 (0 = use global)
	uint8_t _block_pad[2];  // +0x11A
};

/*
 * Per-land-tile-type properties loaded from the first section of
 * tiledata.mul (26 bytes; 16384 entries, in groups of 32 with a 4-byte
 * group header on disk).
 */
struct LandTileData {
	uint32_t flags;     // +0x00
	uint16_t textureID; // +0x04
	char name[20];      // +0x06
};

/*
 * On-disk static-object record from statics0.mul (7 bytes, packed to
 * match the file layout).
 */
struct __attribute__((packed)) StaticEntry {
	uint16_t itemID;
	uint8_t xOff;
	uint8_t yOff;
	int8_t z;
	uint16_t hue;
};

/*
 * Walkable/collidable surface at a tile location (12 bytes), built by
 * BuildTileList (0x00469AF4) and consumed by CanWalk/GetMinMaxZ.
 */
struct SurfaceInfo {
	uint32_t flags; // +0x00 (TF_SURFACE, TF_IMPASSABLE, ...)
	int16_t z;      // +0x04
	int16_t height; // +0x06
	CItem *item;    // +0x08 (NULL for land)
};

/*
 * Land-section flags from tiledata.mul. Same bit positions overlap with
 * item tiledata flags but live in a separate table.
 */
enum LandTileFlag {
	LTF_IMPASSABLE = 0x00000040,
	LTF_WET = 0x00000080,
};

/*
 * Movement-related item tile flags used by the terrain system. Extends
 * TileFlag above.
 */
enum TerrainTileFlag {
	TF_SURFACE = 0x00000200,
	TF_IMPASSABLE = 0x00000040,
	TF_WET = 0x00000080,
	TF_BRIDGE = 0x00000400,
};

// Terrain globals
// Binary: g_TerrainManager at 0x00697A4C, g_SpatialGrid at 0x006933D8
extern LandTileData *g_LandTileData; // land tile flags (16384 entries)

/*
 * Grid dimensions and block array - binary uses CBlockManager fields directly.
 * g_MapBlocks is *(0x006933E4) = g_SpatialGrid.cells, cast to our MapBlock type.
 * g_MapBlocksW/H are this->gridWidth/gridHeight. g_MapOriginX/Y are g_Config.x/y.
 */
#include "location.h"
#include "blockmanager.h"

__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CVector CVector;
#define g_MapBlocks  ((MapBlock *)g_SpatialGrid.cells)  /* *(0x006933E4) */
#define g_MapBlocksW (g_SpatialGrid.gridWidth)   /* this->gridWidth */
#define g_MapBlocksH (g_SpatialGrid.gridHeight)  /* this->gridHeight */
#define g_MapOriginX g_mapStartX                  /* g_MapOriginX = g_Config.x */
#define g_MapOriginY g_mapStartY                  /* g_MapOriginY = g_Config.y */

#define LAND_TILE_COUNT 16384
#define ITEM_TILE_COUNT 16384
#define STEP_HEIGHT     16

// 0x0046B692 - Find entities at exact (x,y) with z in [zMin,zMax].
struct CVector;

/*
 * Animation metadata entry (68 bytes), loaded from animdata.mul into the
 * 0x4000-entry array at [0x0064B35C].
 */
__extension__ typedef struct AnimDataEntry {
	uint8_t frameData[64]; // +0x00
	uint8_t unk40;         // +0x40
	uint8_t frameCount;    // +0x41
	uint8_t frameInterval; // +0x42
	uint8_t startInterval; // +0x43
} AnimDataEntry;

/*
 * On-disk hue table entry (88 bytes), loaded from hues.mul into the
 * 0x400-entry array at [0x0064B360]. Disk layout is groups of 8 with a
 * 4-byte group header.
 */
__extension__ typedef struct HueEntry {
	uint16_t colors[32]; // +0x00
	uint16_t startColor; // +0x40
	uint16_t endColor;   // +0x42
	char name[20];       // +0x44
} HueEntry;

/*
 * Expanded in-memory hue entry (148 bytes) at [0x0064B370]: HueEntry
 * fields followed by 60 bytes of runtime scratch (purpose unknown).
 */
__extension__ typedef struct HueEntryExpanded {
	uint16_t colors[32]; // +0x00
	uint16_t startColor; // +0x40
	uint16_t endColor;   // +0x42
	char name[20];       // +0x44
	uint8_t extra[0x3C]; // +0x58
} HueEntryExpanded;

// 0x004D716B - MapBlock_ReadEntry
// GM editor delegate for region updates. Writes region to map file.
struct CRegion;

/*
 * 0x0045B3C4 - CWorld::InsertResourceTileNode
 * Checks land tile flag (bit 0x8000), strips it, inserts resource node
 * into g_ResEntitySlots for item tiles (not land tiles).
 */
__extension__ typedef struct CResourceNode CResourceNode;

extern const int g_TerrainCornerDX[4]; // 0x0061BB98
extern int g_StaticItemCount; // 0x0068B380
extern uint32_t g_DynamicItemCount; // 0x0068B384
extern int g_TerrainDataLoaded; // 0x006982F8
extern CVector g_arenaPageVec; // 0x00699A30
extern int g_IgnoreMobiles; // 0x006CA8E4
extern CItem *g_StaticFreeList; // 0x006CA8FC
extern LandTileData *g_LandTileData;
extern const int g_TerrainCornerDY[4];
extern const int g_TerrainDirDX[8];
extern const int g_TerrainDirDY[8];

void DynFix_LogError(int lineNum, const char *filename); // 0x00459D50
CItem *FindEntityByBodyTypeAtXYZ(int x, int y, int z, int bodyType); // 0x00459D9F
void StoreTileDataEntry(CPlayer *player, uint16_t artID, uint32_t flags, char *name, uint8_t weight, uint8_t layer, uint32_t miscData, uint16_t value1, uint16_t value2,
        uint16_t height, uint8_t quantity, uint16_t textureID); // 0x0045B308
void CWorld_InsertResourceTileNode(uint16_t artID, CResourceNode *node); // 0x0045B3C4
int Terrain_IsVoidTile(uint16_t tileID); // 0x00469750
int IsNoDrawType(uint16_t bodyType); // 0x00469791
int CTerrainManager_CheckMoveBlocked(CLocation loc, int height, int moveType, CItem *mob, int useInterpolatedZ); // 0x00469D58
void CTerrainManager_GetMinMaxZ(int *outMinZ, int *outMaxZ, CLocation loc, int direction, int moveType, CItem *mob, int useInterpolatedZ); // 0x00469E81
int CTerrainManager_GetStandableZ(int x, int y, int maxZ); // 0x0046A050
int CTerrainManager_CanWalk(CLocation loc, int minZ, int maxZ, int height, int moveType, CItem *mob, int useInterpolatedZ); // 0x0046A25D
int CTerrainManager_GetValidZAtEntity(CItem *entity, int *outZ, uint32_t height); // 0x0046A517
int CTerrainManager_GetDropZ(CLocation *loc, int *outZ, int stepHeight); // 0x0046A608
int IsWaterTile(int id); // 0x0046A66F
int Terrain_GetInterpolatedZ(int x, int y, int direction); // 0x0046A6CA
int Terrain_GetAvgLandZ(int x, int y); // 0x0046A783
int Terrain_GetMinLandZ(int x, int y); // 0x0046A853
int Terrain_GetLandZ(int x, int y); // 0x0046A91E
int *CTerrainManager_GetLandZQuad(int x, int y); // 0x0046A974
uint16_t Terrain_GetLandTileID(int x, int y); // 0x0046A9E6
uint16_t CTerrainManager_GetLandTileID(CTerrainManager *this, int x, int y); // 0x0046AA3C
void CTerrainManager_MovePlayer(CItem *player, int direction, uint8_t sequence); // 0x0046AA95
int CTerrainManager_LOSRaycast(CLocation *srcArg, CLocation *dstArg, int flags); // 0x0046ADA5
int CEntity_CanSee(CItem *entityA, CItem *entityB, int flags); // 0x0046B1E7
int FindSpawnSpotInBox(CLocation *result, int16_t minX, int16_t minY, int16_t minZ, int16_t maxX, int16_t maxY, int16_t maxZ, int factor, int range, int height, intptr_t flags,
        int (*callback)(CLocation *)); // 0x0046B4F3
int CTerrainManager_FindEntitiesAtXYZ(int16_t x, int16_t y, int16_t zMin, int16_t zMax, CVector *result, int (*filter)(CItem *)); // 0x0046B692
CItem *CTerrainManager_FindDoorAtLoc(CLocation *loc, int16_t minZ, int16_t maxZ); // 0x0046B75E
int CTerrainManager_CanWalkWrapper(CLocation loc, int minZ, int maxZ, int height, int moveType, CItem *mob, int useInterpolatedZ); // 0x0046B841
int CBlockManager_FindSpawnSpotExt(CLocation *loc, int walkZMin, int walkZMax, int zMin, int zMax, int range, int height, intptr_t flags); // 0x0046B87E
int FindSpawnSpot(CLocation *loc, int zMin, int zMax, int range, int height, intptr_t flags); // 0x0046B8B3
void SurfaceInfo_ConstructorAlloc(CVector *this, SurfaceInfo *dest, SurfaceInfo *src); // 0x0046BE80
void SurfaceInfo_Fill(SurfaceInfo *first, SurfaceInfo *last, SurfaceInfo *src); // 0x0046C0A0
SurfaceInfo *SurfaceInfo_CopyFrom(SurfaceInfo *this, SurfaceInfo *src); // 0x0046C0D0
SurfaceInfo *SurfaceInfo_CopyBackwardSI(SurfaceInfo *first, SurfaceInfo *last, SurfaceInfo *destEnd); // 0x0046C110
void *SurfaceInfo_AllocateN(int count); // 0x0046C140
void *CVector_Allocate4_Terrain(CVector *self, uint32_t count, int unused); // 0x0047A350
void Static_Unlock(void); // 0x004866FC
void Static_Lock(void); // 0x00486701
void Terrain_LoadStatics(void); // 0x004C4499
void MapFileManager_SeekBlock(int offset); // 0x004DE4E7
void MapFileManager_WriteBlock(int offset, void *data, int length); // 0x004DE4F4
void MapFileManager_FlushBlock(int offset); // 0x004DE501
int LOS_GetHeight(CItem *ent);

#endif /* TERRAIN_H_ */
