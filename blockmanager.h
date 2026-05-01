/*
 * Spatial grid for entity lookup by location. The map is divided into
 * 8x8-tile cells; each cell stores linked lists of items and mobiles
 * for fast spatial queries.
 */
#ifndef BLOCKMANAGER_H_
#define BLOCKMANAGER_H_

#include <stdint.h>

#include "config.h"
#include "entitymap.h"

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CVector CVector;
struct CItem;
struct CLocation;

/*
 * Node in a CBlock's per-cell tracking list (block offset 0x10C).
 * data[0] holds a nearby entity pointer, data[1] holds the event type.
 */
typedef struct CBlockTrackingNode {
	struct CItem *entity;             // 0x00
	uintptr_t data[2];                // 0x04
	struct CBlockTrackingNode *next;  // 0x0C
} CBlockTrackingNode;

/*
 * One spatial grid cell (0x11C bytes). Binary constructor at 0x0042F070.
 */
typedef struct CBlock {
	uint8_t pad00[0x100];             // 0x00 - terrain/static cache data
	struct CItem *staticHead;         // 0x100 - static item list (chain via offset 0x10)
	struct CItem *itemHead;           // 0x104 - dynamic item list (chain via offset 0x20)
	struct CItem *chunkEgg;           // 0x108 - CEgg entity list (used by spawn system)
	CBlockTrackingNode *trackingHead; // 0x10C
	uint8_t flags110;                 // 0x110
	uint8_t flags111;                 // 0x111
	uint16_t weatherSeason;           // 0x112 - init 0xFFFF
	uint16_t weatherDay;              // 0x114 - init 0xFFFF
	uint16_t weatherNight;            // 0x116 - init 0xFFFF
	int16_t lightLevel;               // 0x118 - 0: use global default
	uint8_t pad11A[2];                // 0x11A
} CBlock;

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CBlock) == 0x11C, "CBlock size mismatch");
#endif

/*
 * Top-level spatial grid (0x14 bytes). Binary instance: g_SpatialGrid
 * at 0x006933D8, constructor at 0x0042EB10.
 */
typedef struct CBlockManager {
	int gridWidth;   // 0x00 - columns (mapWidth / 8)
	int gridHeight;  // 0x04 - rows (mapHeight / 8)
	int totalBlocks; // 0x08 - gridWidth * gridHeight
#if __SIZEOF_POINTER__ == 8
	int _pad64;      // 64-bit alignment pad
#endif
	CBlock *cells;   // 0x0C - calloc'd array
	int unk10;       // 0x10
} CBlockManager;

/*
 * Map bounds aliased into CConfig fields. The binary's g_mapWidth..
 * g_mapStartY globals at 0x00698320..0x0069832C overlap g_Config+0x30..+0x3C.
 */
#define g_mapWidth  ((int)g_Config.width)
#define g_mapHeight ((int)g_Config.height)
#define g_mapStartX ((int)g_Config.x)
#define g_mapStartY ((int)g_Config.y)

struct CVector;

extern int g_UpdatesEnabled; // 0x00624E3C
extern int g_UpdatesSuppressCount; // 0x006EFF44
extern CBlockManager g_SpatialGrid;

void CBlockManager_Init(CBlockManager *this); // 0x0042EB10
void UpdateRegion(CItem *entity); // 0x0042EB4F
void Block_RemoveTrackingNode(CItem *entity); // 0x0042ED29
int CheckWalkPassability(CItem *mob, CLocation *oldLoc, CLocation *newLoc); // 0x0042EDBF
void CBlockManager_Lock(CBlockManager *this); // 0x0042F199
void CBlockManager_Unlock(CBlockManager *this); // 0x0042F1A4
void CBlockManager_Setup(CBlockManager *this, int widthTiles, int heightTiles); // 0x0042F1AF
void CBlockManager_GetBlockOrigin(CBlockManager *this, int blockIdx, int *outX, int *outY); // 0x0042F229
int CBlockManager_GetBlockIndex(CBlockManager *this, int x, int y, int unused); // 0x0042F265
int Terrain_GetBlockIndex(int x, int y); // 0x0042F265
int CBlockManager_GetBlockIndexFromLoc(CBlockManager *this, CLocation *loc, int extra); // 0x0042F2CF
void CBlockManager_GetNearbyBlocks(CBlockManager *this, CLocation *loc, int range, int *blockBuf, int maxBlocks); // 0x0042F2F7
int CBlockManager_IsValidCoordAbsolute(CBlockManager *this, int x, int y); // 0x0042F40B
int CBlockManager_IsValidCoord(CBlockManager *this, int x, int y); // 0x0042F43F
int Terrain_InBounds(int x, int y); // 0x0042F43F
int CBlockManager_IsNearBorder(CBlockManager *this, int x, int y); // 0x0042F48C
int CBlockManager_WrapCoord(CBlockManager *this, CLocation *loc); // 0x0042F522
void CBlockManager_GetItemsAtLocationXY(CBlockManager *this, CVector *list, CLocation *loc); // 0x0042F53D
void CBlockManager_GetItemsAtLocationXYZ(CBlockManager *this, CVector *list, CLocation *loc); // 0x0042F5CB
void CBlockManager_GetItemsAtLocation(CBlockManager *this, CVector *list, CLocation *loc); // 0x0042F670
int CBlockManager_RestoreItems(CBlockManager *this, CVector *list); // 0x0042F715
int CEntityMap_RemoveFromGrid(CBlockManager *this, CVector *vec); // 0x0042F8A7
void LoadAll_Books(void); // 0x0042FA08
void LoadAll_RegionGrid(void); // 0x0042FA12
void CBlockManager_LoadAll(CBlockManager *this); // 0x0042FD01
void ArrayIterator_ForEach(void *ptr, int stride, int count, void (*callback)(void *)); // 0x0042FE70
CBlock *CBlockManager_GetBlock(CBlockManager *this, int blockIdx);

#endif /* BLOCKMANAGER_H_ */
