/*
 * Terrain and static tile lookups.
 *
 * Loads the map/statics MUL data, exposes the per-tile land and static
 * surface queries used for walkability, Z-resolution, line-of-sight,
 * and region membership, and owns the free list for statically-placed
 * CResourceEntity nodes.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "dynamic.h"
#include "egg.h"
#include "gmedit.h"
#include "main.h"
#include "packet_manager.h"
#include "player.h"
#include "stddeque.h"
#include "utils.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "wombat_exec.h"
#include "world.h"

__extension__ typedef struct LOSContext LOSContext;
__extension__ typedef struct SurfaceList SurfaceList;

static CTerrainManager *CTerrainManager_Constructor(CTerrainManager *this); // 0x00469AAB
static void CTerrainManager_ConstructorVTable(void); // 0x00469AC2
static int CTerrainManager_IsNotWaterTile(CTerrainManager *this, int tileId); // 0x00469AD6
static void Terrain_BuildSurfaceList(SurfaceList *list, CLocation loc, int moveType, CItem *mob, int zOffset); // 0x00469AF4
static int CTerrainManager_GetDistance(CTerrainManager *this, CLocation loc1, CLocation loc2); // 0x0046ACC8
static int CBlockManager_FindSpawnSpot(CLocation *loc, int walkZMin, int walkZMax, int zMin, int zMax, int height, int moveType, CItem *mob); // 0x0046B276
static SurfaceInfo *SurfaceInfo_Constructor(SurfaceInfo *this, uint32_t flags, int16_t z, int16_t height, CItem *item); // 0x0046B920
static void SurfaceInfo_Set(SurfaceInfo *this, uint32_t flags, int16_t z, int16_t height, CItem *item); // 0x0046B950
static int SurfaceInfo_GetTopZ(SurfaceInfo *s); // 0x0046B990
static void *LOSContext_Constructor(LOSContext *this, int zValue, int flags); // 0x0046B9E0
static SurfaceInfo *SurfaceInfoRevIter_Deref(SurfaceInfo **iter); // 0x0046BAB0
static SurfaceInfo **SurfaceInfoRevIter_PostDec(SurfaceInfo **iter, SurfaceInfo **result); // 0x0046BAD0
static int LOS_CheckAtLocation(CLocation *loc, int zValue, int flags); // 0x0046BED0
static int LOS_BlockCheck(LOSContext *ctx, CLocation *loc, CItem *entity); // 0x0046BFC0
static int LOS_HeightCheck(LOSContext *ctx, CLocation *loc, CItem *entity); // 0x0046C010
static int Terrain_IsContainerFilter(CItem *entity); // 0x0046C740
static void Terrain_LoadStaticsBlock(int extra, int blockIdx, uint8_t *data, int dataLen); // 0x004C4364
static int SurfaceInfo_CompareZ(const void *a, const void *b);
static void SurfaceList_Init(SurfaceList *list);
static void SurfaceList_Add(SurfaceList *list, uint32_t flags, int16_t z, int16_t height, CItem *item);
static int LOS_GetFlags(CItem *ent);

LandTileData *g_LandTileData;

// SurfaceList helper type (fixed-size array for surface info entries).
#define SURFACE_LIST_MAX 128

__extension__ typedef struct SurfaceList {
	SurfaceInfo entries[SURFACE_LIST_MAX];
	int count;
} SurfaceList;

static void
SurfaceList_Init(SurfaceList *list)
{
	list->count = 0;
}

static void
SurfaceList_Add(SurfaceList *list, uint32_t flags, int16_t z, int16_t height, CItem *item)
{
	SurfaceInfo *s;

	if (list->count >= SURFACE_LIST_MAX)
		return;
	s = &list->entries[list->count++];
	s->flags = flags;
	s->z = z;
	s->height = height;
	s->item = item;
}

// LOSContext type (used by LOS raycast functions).
__extension__ typedef struct LOSContext {
	int zValue; // 0x00
	int flags;  // 0x04
} LOSContext;

__attribute__((unused)) void *CVector_InsertAtSI(CVector *this, uint32_t index, SurfaceInfo *value);
/*
 * 0x00459D50 - DynFix_LogError
 *
 * Logs a formatted "filename (line N)" message to EventLogger under
 * the "dynfix"/"misc" category.
 */
void
DynFix_LogError(int lineNum, const char *filename)
{
	char buf[1024];

	sprintf(buf, "%s (line %d)", filename, lineNum);
	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "dynfix", "misc", buf);
}

/*
 * 0x00459D9F - FindEntityByBodyTypeAtXYZ
 *
 * Returns the first entity at exact (x,y,z) whose body type matches,
 * by walking the spatial grid's item chain.
 */
CItem *
FindEntityByBodyTypeAtXYZ(int x, int y, int z, int bodyType)
{
	int blockIdx;
	CItem *walk;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y))
		return NULL;

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	walk = g_SpatialGrid.cells[blockIdx].itemHead;
	while (walk != NULL) {
		if (CEntity_GetBodyType(walk) == (uint16_t)bodyType) {
			if ((int16_t)walk->resourceEntity.entity.location.x == x && (int16_t)walk->resourceEntity.entity.location.y == y &&
			        (int16_t)walk->resourceEntity.entity.location.z == z)
				return walk;
		}
		walk = walk->spatialNext;
	}
	return NULL;
}

/*
 * 0x0045B308 - StoreTileDataEntry
 *
 * Dispatches to StoreLandTileData or StoreItemTileData based on bit
 * 0x8000 of artID (land tile flag), stripping the flag first.
 */
void
StoreTileDataEntry(CPlayer *player, uint16_t artID, uint32_t flags, char *name, uint8_t weight, uint8_t layer, uint32_t miscData, uint16_t value1, uint16_t value2, uint16_t height,
        uint8_t quantity, uint16_t textureID)
{
	int isLand;

	isLand = (((uint32_t)artID & 0xFFFF) & 0x8000) != 0;
	artID &= 0x7FFF;

	if (isLand) {
		StoreLandTileData(player, (uint16_t)artID & 0xFFFF, flags, textureID & 0xFFFF, name);
	} else {
		StoreItemTileData(
		        player, (uint16_t)artID & 0xFFFF, flags, weight & 0xFF, layer & 0xFF, miscData, value1 & 0xFFFF, value2 & 0xFFFF, height & 0xFFFF, quantity & 0xFF, name);
	}
}

/*
 * 0x0045B3C4 - CWorld::InsertResourceTileNode
 *
 * Inserts a resource node into the g_ResEntitySlots entry for an item
 * tile. Land tiles (bit 0x8000 set) are ignored.
 */
void
CWorld_InsertResourceTileNode(uint16_t artID, CResourceNode *node)
{
	int isLandTile;

	isLandTile = ((artID & 0xFFFF) & 0x8000) != 0;
	artID &= 0x7FFF;

	if (!isLandTile) {
		CWorld_Lock();
		CResourceEntity_InsertNode((CItem *)&g_ResEntitySlots[artID & 0xFFFF], node);
		CWorld_Unlock();
	}
}

/*
 * 0x00469750 - Terrain_IsVoidTile
 *
 * Returns 1 if the land tile ID is a void/nodraw tile (2,
 * 0x1AE-0x1B5, or 0x1DB).
 */
int
Terrain_IsVoidTile(uint16_t tileID)
{
	uint32_t id;

	id = tileID & 0xFFFF;
	if (id <= 0x1B5) {
		if (id >= 0x1AE)
			return 1;
		if (id == 2)
			return 1;
		return 0;
	}
	if (id == 0x1DB)
		return 1;
	return 0;
}

/*
 * 0x00469791 - IsNoDrawType
 *
 * Returns 1 if bodyType has the tiledata nodraw flag (0x10000), or
 * is 1, in range 0x2198-0x21A2, or 0x375A.
 */
int
IsNoDrawType(uint16_t bodyType)
{
	uint32_t flags;

	flags = g_ItemTileData[bodyType & 0xFFFF].flags;
	if (flags & 0x10000)
		return 1;
	switch (bodyType & 0xFFFF) {
	case 1:
	case 0x375A:
		return 1;
	default:
		if (bodyType >= 0x2198 && bodyType <= 0x21A2)
			return 1;
		return 0;
	}
}

/*
 * 0x00469AAB - CTerrainManager::CTerrainManager
 *
 * Constructor for g_TerrainManager. Sets the vtable pointer; our C
 * port has no vtable dispatch so the body is empty.
 */
static __attribute__((unused)) CTerrainManager *
CTerrainManager_Constructor(CTerrainManager *this)
{
	USED(this);
	return this;
}

/*
 * 0x00469AC2 - CTerrainManager::CTerrainManager (vtable setup)
 *
 * Sets the CTerrainManager vtable pointer on the global instance.
 * No-op in our C port.
 */
static void __attribute__((unused))
CTerrainManager_ConstructorVTable(void)
{
}

/*
 * 0x00469AD6 - CTerrainManager::IsNotWaterTile
 *
 * Returns the boolean negation of IsWaterTile.
 */
static __attribute__((unused)) int
CTerrainManager_IsNotWaterTile(CTerrainManager *this, int tileId)
{
	USED(this);
	return !IsWaterTile(tileId);
}

/*
 * 0x00469AF4 - Terrain_BuildSurfaceList
 *
 * Collects land tile and item surfaces at (x,y) into list. When mob
 * is non-NULL, surface flags are obtained via the mob's virtual
 * check callbacks instead of the default tiledata lookup. zOffset
 * is added to the land tile surface's base Z.
 *
 * FIXED: The binary iterates the per-block dynamic item chain via
 * spatialNext with no cycle guard. If CItem_InternalMove is ever
 * invoked on an item still linked in that chain (e.g. insertion
 * into its own block's head produces item->spatialNext == item),
 * this loop hangs the server. A bounded iteration cap was added to
 * the dynamic chain scan so corruption can be reported and skipped
 * instead of locking up the AI tick.
 */
static void
Terrain_BuildSurfaceList(SurfaceList *list, CLocation loc, int moveType, CItem *mob, int zOffset)
{
	int blockIdx;
	uint16_t tileID;
	uint32_t surfFlags;
	int minLandZ, avgLandZ;
	CItem *item;
	int x = (int)(int16_t)loc.x;
	int y = (int)(int16_t)loc.y;
	int guard;
	// Binary has a dead local init that is never read; preserved.
	int dead_init = 0;
	USED(dead_init);

	SurfaceList_Init(list);

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &loc, 0);

	tileID = Terrain_GetLandTileID(x, y);
	if (mob != NULL)
		surfFlags = ((uint32_t (*)(void *, uint32_t))VT_FN(mob, VT_CHECK_SURFACE))(mob, (uint32_t)tileID);
	else
		surfFlags = GetLandTileFlags(tileID, moveType);

	if (surfFlags != 0) {
		minLandZ = Terrain_GetMinLandZ(x, y);
		avgLandZ = Terrain_GetAvgLandZ(x, y);
		SurfaceList_Add(list, surfFlags, (int16_t)(minLandZ + zOffset), (int16_t)(avgLandZ - minLandZ), NULL);
	}

	// Dynamic items
	item = g_MapBlocks[blockIdx].itemHead;
	guard = 0;
	while (item != NULL) {
		if (++guard > 8192) {
			fprintf(stderr,
			        "Terrain_BuildSurfaceList: dynamic "
			        "chain cycle at block %d (loc %d,%d); "
			        "bailing out\n",
			        blockIdx, x, y);
			break;
		}
		if (CLocation_IsEqualXY(&item->resourceEntity.entity.location, &loc)) {
			if (mob != NULL)
				surfFlags = ((uint32_t (*)(void *, void *))VT_FN(mob, VT_CHECK_SURFACE_OF))(mob, item);
			else
				surfFlags = ((int (*)(void *, int))VT_FN(item, VT_GET_SURFACE_FLAGS))(item, moveType);
			if (surfFlags != 0) {
				int height = ((int (*)(void *))VT_FN(item, VT_GET_HEIGHT))(item);
				SurfaceList_Add(list, surfFlags, item->resourceEntity.entity.location.z, (int16_t)height, item);
			}
		}
		item = item->spatialNext;
	}

	// Static items
	item = (CItem *)g_MapBlocks[blockIdx].staticHead;
	while (item != NULL) {
		if (CLocation_IsEqualXY(&item->resourceEntity.entity.location, &loc)) {
			int itemZ;

			if (mob != NULL)
				surfFlags = ((uint32_t (*)(void *, void *))VT_FN(mob, VT_CHECK_SURFACE_OF))(mob, item);
			else
				surfFlags = ((int (*)(void *, int))VT_FN(item, VT_GET_SURFACE_FLAGS))(item, moveType);
			itemZ = item->resourceEntity.entity.location.z;

			// Binary dead code: z adjustment for tile 0x1796, moveType 3/4.
			// Computed into local but original z used for the surface entry.
			if (moveType == 3 || moveType == 4) {
				if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x1796) {
					if (itemZ == -1)
						itemZ = -5;
				}
			}

			if (surfFlags != 0) {
				int height = ((int (*)(void *))VT_FN(item, VT_GET_HEIGHT))(item);
				SurfaceList_Add(list, surfFlags, item->resourceEntity.entity.location.z, (int16_t)height, item);
			}
		}
		item = (CItem *)item->resourceEntity.nextInContainer;
	}
}

/*
 * 0x00469D58 - CTerrainManager::CheckMoveBlocked
 *
 * Checks whether the given vertical extent at (x,y) is blocked by an
 * impassable surface. Returns 0 if blocked, else a bitmask: bit 0
 * clear, bit 1 standing on surface, bit 2 surface present.
 */
int
CTerrainManager_CheckMoveBlocked(CLocation loc, int height, int moveType, CItem *mob, int useInterpolatedZ)
{
	SurfaceList list;
	int i;
	int result;
	int z = (int)(int16_t)loc.z;

	result = 1;

	Terrain_BuildSurfaceList(&list, loc, moveType, mob, useInterpolatedZ);

	for (i = 0; i < list.count; i++) {
		SurfaceInfo *s = &list.entries[i];

		if (s->flags & TF_IMPASSABLE) {
			if (z + height > s->z) {
				int topZ = SurfaceInfo_GetTopZ(s);
				if (topZ > z) {
					return 0;
				}
			}
		}

		if (s->flags & TF_SURFACE) {
			int topZ = SurfaceInfo_GetTopZ(s);
			result |= 4;
			if (topZ == z)
				result |= 2;
		}
	}

	return result;
}

/*
 * 0x00469E81 - CTerrainManager::GetMinMaxZ
 *
 * Computes the allowed Z range at a location for movement. First
 * entry (land) sets the baseline; remaining entries adjust minZ for
 * impassable items below z and both minZ/maxZ for bridges at z.
 *
 * FIXED: Binary assumes entries[0] is always the land tile, but when
 * the land tile is void (GetLandTileFlags returns 0) the land entry
 * is skipped and entries[0] becomes the first item surface. The binary
 * then calls Terrain_GetAvgLandZ on the wrong entry, returning the
 * raw terrain Z instead of the item surface Z. This breaks movement
 * in underground rooms like the Wind entrance marble room where void
 * land tiles sit above walkable static floor tiles. Fix: check
 * entries[0].item == NULL before applying land Z.
 */
void
CTerrainManager_GetMinMaxZ(int *outMinZ, int *outMaxZ, CLocation loc, int direction, int moveType, CItem *mob, int useInterpolatedZ)
{
	SurfaceList list;
	int i;
	int topZ, landZ;
	int x = (int)(int16_t)loc.x;
	int y = (int)(int16_t)loc.y;
	int z = (int)(int16_t)loc.z;

	Terrain_BuildSurfaceList(&list, loc, moveType, mob, 0);

	*outMinZ = -128;
	*outMaxZ = z;

	// First entry: land surface baseline (only if actually land).
	if (list.count > 0) {
		SurfaceInfo *first = &list.entries[0];

		if (first->item == NULL && (first->flags & TF_SURFACE)) {
			topZ = SurfaceInfo_GetTopZ(first);
			if (topZ <= z) {
				if (useInterpolatedZ)
					landZ = Terrain_GetInterpolatedZ(x, y, direction);
				else
					landZ = Terrain_GetAvgLandZ(x, y);
				if (*outMinZ < landZ)
					*outMinZ = landZ;
				if (*outMaxZ < landZ)
					*outMaxZ = landZ;
			}
		}
	}

	for (i = 1; i < list.count; i++) {
		SurfaceInfo *s = &list.entries[i];

		topZ = SurfaceInfo_GetTopZ(s);

		// Impassable below current z raises the minZ floor.
		if ((s->flags & TF_IMPASSABLE) && topZ <= z) {
			if (*outMinZ < topZ)
				*outMinZ = topZ;
		}

		// Bridge at current z adjusts the Z range.
		if ((s->flags & TF_BRIDGE) && z == topZ) {
			int rawTop = s->z + s->height;
			if (*outMaxZ < rawTop)
				*outMaxZ = rawTop;
			if (*outMinZ > s->z)
				*outMinZ = s->z;
		}
	}
}

/*
 * 0x0046A050 - CTerrainManager::GetStandableZ
 *
 * Returns the highest standable Z at (x,y) that is <= maxZ. For each
 * item at the location: bridges contribute z + height/2, surfaces
 * contribute z + height, short (<2) non-surfaces contribute z + height,
 * taller non-surfaces are ignored. Falls back to avgLandZ.
 */
int
CTerrainManager_GetStandableZ(int x, int y, int maxZ)
{
	CLocation loc;
	int blockIdx;
	CItem *item;
	int bestZ;
	int topZ;
	int flags, height;
	int avgZ;

	bestZ = -128;

	loc.x = (uint16_t)x;
	loc.y = (uint16_t)y;
	loc.z = 0;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &loc, 0);

	// Dynamic item chain
	item = g_MapBlocks[blockIdx].itemHead;
	while (item != NULL) {
		if (!CLocation_IsEqualXY(&item->resourceEntity.entity.location, &loc))
			goto next_dynamic;

		topZ = (int)(int16_t)item->resourceEntity.entity.location.z;
		flags = LOS_GetFlags(item);

		if (flags & 0x400) {
			height = LOS_GetHeight(item);
			topZ += height / 2;
		} else if (flags & 0x200) {
			height = LOS_GetHeight(item);
			topZ += height;
		} else {
			height = LOS_GetHeight(item);
			if (height >= 2)
				goto next_dynamic;
			topZ += height;
		}

		if (topZ > bestZ && topZ <= maxZ)
			bestZ = topZ;

next_dynamic:
		item = item->spatialNext;
	}

	// Static item chain
	item = g_MapBlocks[blockIdx].staticHead;
	while (item != NULL) {
		if (!CLocation_IsEqualXY(&item->resourceEntity.entity.location, &loc))
			goto next_static;

		topZ = (int)(int16_t)item->resourceEntity.entity.location.z;
		flags = LOS_GetFlags(item);

		if (flags & 0x400) {
			height = LOS_GetHeight(item);
			topZ += height / 2;
		} else if (flags & 0x200) {
			height = LOS_GetHeight(item);
			topZ += height;
		} else {
			height = LOS_GetHeight(item);
			if (height >= 2)
				goto next_static;
			topZ += height;
		}

		if (topZ > bestZ && topZ <= maxZ)
			bestZ = topZ;

next_static:
		item = (CItem *)item->resourceEntity.nextInContainer;
	}

	avgZ = Terrain_GetAvgLandZ(x, y);
	if (bestZ == -128 || (avgZ > bestZ && avgZ <= maxZ))
		bestZ = avgZ;

	return bestZ;
}

/*
 * 0x0046A25D - CTerrainManager::CanWalk
 *
 * Finds the best walkable Z at a location within [minZ, maxZ] given
 * a required vertical clearance. Builds and sorts the surface list,
 * then for each impassable entry with enough clearance searches
 * backward for walkable (surface or bridge) entries. Returns the
 * nearest matching Z to the current z, or -128 if none.
 */
int
CTerrainManager_CanWalk(CLocation loc, int minZ, int maxZ, int height, int moveType, CItem *mob, int useInterpolatedZ)
{
	int x = (int)(int16_t)loc.x;
	int y = (int)(int16_t)loc.y;
	int z = (int)(int16_t)loc.z;
	SurfaceList list;
	int i, j;
	int bestZ, bestDist;
	int currentZ, maxTopZ;
	int entryZ, surfTopZ, zDiff;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)x, (int16_t)y))
		return -128;

	Terrain_BuildSurfaceList(&list, loc, moveType, mob, useInterpolatedZ);

	qsort(list.entries, list.count, sizeof(SurfaceInfo), SurfaceInfo_CompareZ);

	// Ceiling sentinel: impassable barrier at z=128, height=128
	SurfaceList_Add(&list, 0x40, 128, 128, NULL);

	if (z < minZ)
		z = minZ;

	bestZ = -128;
	bestDist = 1000000;
	currentZ = minZ;
	maxTopZ = -128;

	for (i = 0; i < list.count; i++) {
		SurfaceInfo *entry = &list.entries[i];
		int topZ;

		if (!(entry->flags & 0x40)) {
			continue;
		}

		entryZ = entry->z;
		if (entryZ - currentZ >= height) {
			// Search backward from entry[i-1] for walkable surfaces.
			for (j = i - 1; j >= 0; j--) {
				SurfaceInfo *s = &list.entries[j];
				int canWalk;

				if (!(s->flags & 0x600))
					continue;

				surfTopZ = SurfaceInfo_GetTopZ(s);

				if (surfTopZ < maxTopZ)
					continue;

				if (entryZ - surfTopZ < height)
					continue;

				// TF_SURFACE within maxZ, or TF_BRIDGE with
				// base within maxZ.
				canWalk = 0;
				if (surfTopZ <= maxZ && (s->flags & 0x200))
					canWalk = 1;
				else if ((s->flags & 0x400) && s->z <= maxZ)
					canWalk = 1;

				if (!canWalk)
					continue;

				zDiff = z - surfTopZ;
				if (zDiff < 0) {
					zDiff = -zDiff;
				} else if (moveType == 2) {
					if (z + height <= entryZ) {
						surfTopZ = z;
						zDiff = 0;
					} else {
						surfTopZ = entryZ - height;
						zDiff = surfTopZ - z;
					}
				}

				if (zDiff < bestDist) {
					bestZ = surfTopZ;
					bestDist = zDiff;
				}
			}
		}

		topZ = SurfaceInfo_GetTopZ(entry);
		if (currentZ < topZ)
			currentZ = topZ;
		if (maxTopZ < topZ)
			maxTopZ = topZ;
	}

	return bestZ;
}

/*
 * 0x0046A517 - CTerrainManager::GetValidZAtEntity
 *
 * Detaches an entity from the spatial grid, runs GetMinMaxZ and
 * CanWalk at its location with mobiles ignored, then re-attaches it.
 * Writes the result to *outZ and returns 1 if a valid Z was found.
 */
int
CTerrainManager_GetValidZAtEntity(CItem *entity, int *outZ, uint32_t height)
{
	CLocation loc;
	int savedIgnore;
	int moveType;
	int minZ, maxZ;

	if (!VT_IsRemoved(entity))
		((int (*)(void *))VT_FN(entity, VT_HAS_CONTAINER))(entity);

	CLocation_SetLoc(&loc, &entity->resourceEntity.entity.location);

	moveType = ((int (*)(void *))VT_FN(entity, VT_GET_MOVEMENT_TYPE))(entity) & 0xFF;

	savedIgnore = g_IgnoreMobiles;
	g_IgnoreMobiles = 1;

	((void (*)(void *))VT_FN(entity, VT_DETACH_SPATIAL))(entity);

	CTerrainManager_GetMinMaxZ(&minZ, &maxZ, loc, 0, moveType, entity, 0);

	*outZ = CTerrainManager_CanWalkWrapper(loc, minZ, maxZ, (int)height, moveType, NULL, 0);

	((void (*)(void *, void *))VT_FN(entity, VT_SET_LOCATION))(entity, &loc);

	g_IgnoreMobiles = savedIgnore;

	return (*outZ != -128) ? 1 : 0;
}

/*
 * 0x0046A608 - CTerrainManager::GetDropZ
 *
 * Searches the full Z range at a location for a walkable surface.
 * Returns 1 and writes the Z to *outZ on success, 0 otherwise.
 */
int
CTerrainManager_GetDropZ(CLocation *loc, int *outZ, int stepHeight)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;
	*outZ = CTerrainManager_CanWalkWrapper(*loc, -128, 128, stepHeight, 1, NULL, 0);
	return (*outZ != -128) ? 1 : 0;
}

/*
 * 0x0046A66F - IsWaterTile / IsSeaCreature
 *
 * Returns 1 if the ID is a water tile or sea creature body type
 * (ranges 0xA8-0xAB or 0x136-0x137). Same function used in both
 * contexts in the binary.
 */
int
IsWaterTile(int id)
{
	if (id >= 0xA8 && id <= 0xAB)
		return 1;
	if (id >= 0x136 && id <= 0x137)
		return 1;
	return 0;
}

/*
 * 0x0046A6CA - Terrain_GetInterpolatedZ
 *
 * Returns the land Z at (x,y) interpolated by direction: one corner
 * for diagonals, average of two adjacent corners for cardinals.
 *
 * FIXED: Binary computes cornerIdx = direction / 2 at 0x0046A70C
 * with no follow-up mask, then indexes the 4-entry corner tables.
 * With the 0x80 running bit set (e.g. 0x86 = running west) cornerIdx
 * is 67 and the read returns garbage. prevIdx at 0x0046A744 uses
 * ((direction - 1) / 2) & 3, confirming 0..3 is the intended
 * domain. Fix: mask cornerIdx.
 */
int
Terrain_GetInterpolatedZ(int x, int y, int direction)
{
	int cornerIdx, prevIdx;
	int corner1, corner2;

	if (!Terrain_InBounds(x + 1, y + 1))
		return Terrain_GetLandZ(x, y);

	cornerIdx = (direction / 2) & 3;
	corner1 = Terrain_GetLandZ(x + g_TerrainCornerDX[cornerIdx], y + g_TerrainCornerDY[cornerIdx]);

	if (direction & 1)
		return corner1;

	prevIdx = ((direction - 1) / 2) & 3;
	corner2 = Terrain_GetLandZ(x + g_TerrainCornerDX[prevIdx], y + g_TerrainCornerDY[prevIdx]);
	return (corner1 + corner2) / 2;
}

/*
 * 0x0046A783 - Terrain_GetAvgLandZ
 *
 * Averages the diagonal pair (TL+BR or TR+BL) with the smaller
 * height difference among the four corners at (x,y).
 */
int
Terrain_GetAvgLandZ(int x, int y)
{
	int tl, tr, br, bl;
	int diffTLBR, diffTRBL;

	if (!Terrain_InBounds(x + 1, y + 1))
		return Terrain_GetLandZ(x, y);

	tl = Terrain_GetLandZ(x, y);
	tr = Terrain_GetLandZ(x + 1, y);
	br = Terrain_GetLandZ(x + 1, y + 1);
	bl = Terrain_GetLandZ(x, y + 1);

	diffTLBR = tl - br;
	if (diffTLBR < 0)
		diffTLBR = -diffTLBR;
	diffTRBL = tr - bl;
	if (diffTRBL < 0)
		diffTRBL = -diffTRBL;

	if (diffTLBR > diffTRBL)
		return (tr + bl) / 2;
	return (tl + br) / 2;
}

/*
 * 0x0046A853 - Terrain_GetMinLandZ
 *
 * Returns the minimum Z among the four land tile corners at (x,y).
 */
int
Terrain_GetMinLandZ(int x, int y)
{
	int tl, tr, br, bl, minZ;

	if (!Terrain_InBounds(x + 1, y + 1))
		return Terrain_GetLandZ(x, y);

	tl = Terrain_GetLandZ(x, y);
	tr = Terrain_GetLandZ(x + 1, y);
	br = Terrain_GetLandZ(x + 1, y + 1);
	bl = Terrain_GetLandZ(x, y + 1);

	minZ = tl;
	if (tr < minZ)
		minZ = tr;
	if (br < minZ)
		minZ = br;
	if (bl < minZ)
		minZ = bl;
	return minZ;
}

/*
 * 0x0046A91E - Terrain_GetLandZ
 *
 * Returns the raw land Z at world coordinate (x,y).
 */
int
Terrain_GetLandZ(int x, int y)
{
	int blockIdx;
	int localX, localY;

	blockIdx = Terrain_GetBlockIndex(x, y);
	if (blockIdx < 0)
		return 0;
	localX = (x - g_MapOriginX) & 7;
	localY = (y - g_MapOriginY) & 7;
	return g_MapBlocks[blockIdx].cells[localY * 8 + localX].z;
}

/*
 * 0x0046A974 - CTerrainManager::GetLandZQuad
 *
 * Fills a static 4-int array with the land Z of the four corners at
 * (x,y), (x+1,y), (x+1,y+1), (x,y+1) and returns its address.
 */
// 0x00699A18 - scratch buffer returned by CTerrainManager_GetLandZQuad
static int g_LandZQuad[4];

int *
CTerrainManager_GetLandZQuad(int x, int y)
{
	g_LandZQuad[0] = Terrain_GetLandZ(x, y);
	g_LandZQuad[1] = Terrain_GetLandZ(x + 1, y);
	g_LandZQuad[2] = Terrain_GetLandZ(x + 1, y + 1);
	g_LandZQuad[3] = Terrain_GetLandZ(x, y + 1);
	return g_LandZQuad;
}

/*
 * 0x0046A9E6 - Terrain_GetLandTileID
 *
 * Returns the land tile ID at world coordinate (x,y).
 */
uint16_t
Terrain_GetLandTileID(int x, int y)
{
	CLocation loc;
	int blockIdx;

	loc.x = (int16_t)x;
	loc.y = (int16_t)y;
	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &loc, 0);
	if (blockIdx < 0)
		return 0;
	return g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].tileID;
}

/*
 * 0x0046AA3C - CTerrainManager::GetLandTileID
 *
 * CTerrainManager overload that returns the land tile ID at (x,y)
 * via CBlockManager_GetBlockIndex.
 */
uint16_t
CTerrainManager_GetLandTileID(CTerrainManager *this, int x, int y)
{
	int blockIdx;

	USED(this);

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	if (blockIdx < 0)
		return 0;
	return g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].tileID;
}

/*
 * 0x0046AA95 - CTerrainManager::MovePlayer
 *
 * Player movement dispatch: turn-only if direction differs from the
 * current facing, else walk-check and move. After a successful move,
 * applies the shove mechanic draining blocker stamina proportional
 * to the attacker/blocker STR ratio.
 */
void
CTerrainManager_MovePlayer(CItem *player, int direction, uint8_t sequence)
{
	uint8_t maskedDir;
	CString blockerName, shoveMsg;

	maskedDir = (uint8_t)(direction & 0x7F);

	// Track outermost call via g_MoveCurrentPlayer.
	if (g_MoveCurrentPlayer == NULL) {
		g_MoveCurrentPlayer = player;
		g_MoveBlocker = NULL;
	}

	if ((maskedDir & 0xFF) != (((CMobile *)player)->direction & 0x7F)) {
		if (CPlayer_CanMoveDirection((CPlayer *)player, maskedDir & 0xFF)) {
			DoTurn(player, direction, 1, sequence);
		} else {
			CPlayer_SendMoveDeny((CPlayer *)player, sequence);
		}
	} else {
		if (!((int (*)(void *, int, int))VT_FN(player, VT_WALK_CHECK))(player, maskedDir & 0xFF, 0)) {
			CPlayer_SendMoveDeny((CPlayer *)player, sequence);
		} else {
			DoMove(player, direction, 1, sequence);

			// Shove mechanic.
			if (g_MoveCurrentPlayer == player && g_MoveBlocker != NULL) {
				CString_DefaultConstructor(&blockerName);

				if (VT_IsHidden(g_MoveBlocker)) {
					CString_AssignCStr(&blockerName, "something invisible");
				} else {
					CString_AssignCStr(&blockerName, (char *)((char *(*)(void *))VT_FN(g_MoveBlocker, VT_GET_NAME))(g_MoveBlocker));
				}

				CString_Constructor(&shoveMsg, "Being perfectly rested, you shove ");
				CString_ConcatCString(&shoveMsg, &blockerName);
				CString_AppendCStr(&shoveMsg, " out of the way.");
				CPlayer_SystemMessage((CPlayer *)player, CString_GetBuffer(&shoveMsg));

				int staminaDrain = 10;
				int playerStr = (int)(int16_t)CMobile_GetStat((CMobile *)player, 0);
				int blockerStr = (int)(int16_t)CMobile_GetStat((CMobile *)g_MoveBlocker, 0);

				if (playerStr > blockerStr) {
					int bStr = (int)(int16_t)CMobile_GetStat((CMobile *)g_MoveBlocker, 0);
					int pStr = (int)(int16_t)CMobile_GetStat((CMobile *)player, 0);
					staminaDrain = (bStr * 10) / pStr;
				}

				int curStam = CMobile_GetStamina((CMobile *)player);
				((void (*)(void *, int))VT_FN(player, VT_SET_STAMINA))(player, curStam - staminaDrain);

				CString_Destructor(&shoveMsg);
				CString_Destructor(&blockerName);
			}
		}
	}

	if (g_MoveCurrentPlayer == player) {
		g_MoveCurrentPlayer = NULL;
		g_MoveBlocker = NULL;
	}
}

/*
 * 0x0046ACC8 - CTerrainManager::GetDistance
 *
 * Chebyshev distance between two locations: max(|dx|, |dy|, |dz|/11).
 * ORPHANED: zero callers in binary.
 */
static __attribute__((unused)) int
CTerrainManager_GetDistance(CTerrainManager *this, CLocation loc1, CLocation loc2)
{
	int dx, dy, dz;

	USED(this);

	dx = (int16_t)loc1.x - (int16_t)loc2.x;
	if (dx < 0)
		dx = -dx;

	dy = (int16_t)loc1.y - (int16_t)loc2.y;
	if (dy < 0)
		dy = -dy;

	dz = loc1.z - loc2.z;
	if (dz < 0)
		dz = -dz;
	dz = dz / 11;

	if (dx > dy) {
		if (dx > dz)
			return dx;
		else
			return dz;
	} else {
		if (dy > dz)
			return dy;
		else
			return dz;
	}
}

/*
 * 0x0046ADA5 - CTerrainManager::LOSRaycast
 *
 * Casts two parallel rays perpendicular to the line of sight using
 * 16.16 fixed-point stepping. Returns 1 if at least one ray reaches
 * the destination unblocked, 0 if both are blocked. flags bit 0 adds
 * 0x3000 (walls/roofs), bit 1 adds 0x40 (impassable) to blockers.
 */
int
CTerrainManager_LOSRaycast(CLocation *srcArg, CLocation *dstArg, int flags)
{
	CLocation src, dst;
	int srcHeight, dstHeight;
	int srcAbove, dstAbove;
	int dx, dy, dz;
	int steps;
	int zHigh, zLow;
	int losFlags;
	int dx_fp, dy_fp, dz_fp;
	int z_fp;
	int x1_fp, y1_fp, x2_fp, y2_fp;
	int ray_alive[2];
	int i, j;

	src = *srcArg;
	dst = *dstArg;

	srcHeight = Terrain_GetAvgLandZ((int)(int16_t)src.x, (int)(int16_t)src.y);
	dstHeight = Terrain_GetAvgLandZ((int)(int16_t)dst.x, (int)(int16_t)dst.y);

	// Source and destination must be on the same side of the terrain.
	srcAbove = ((int)(int16_t)src.z >= srcHeight) ? 1 : 0;
	dstAbove = ((int)(int16_t)dst.z >= dstHeight) ? 1 : 0;
	if (srcAbove != dstAbove)
		return 0;

	losFlags = 0;
	if (flags & 1)
		losFlags |= 0x3000;
	if (flags & 2)
		losFlags |= 0x40;

	dx = (int)(int16_t)dst.x - (int)(int16_t)src.x;
	dy = (int)(int16_t)dst.y - (int)(int16_t)src.y;
	dz = (int)(int16_t)dst.z - (int)(int16_t)src.z;

	steps = abs(dx);
	if (abs(dy) > steps)
		steps = abs(dy);

	if ((int)(int16_t)dst.z < (int)(int16_t)src.z) {
		zHigh = (int)(int16_t)src.z;
		zLow = (int)(int16_t)dst.z;
	} else {
		zHigh = (int)(int16_t)dst.z;
		zLow = (int)(int16_t)src.z;
	}

	// Short-distance path.
	if (steps <= 1) {
		if (abs(dz) < 16)
			return 1;

		src.z = (int16_t)zLow;
		dst.z = (int16_t)zLow;

		if (LOS_CheckAtLocation(&src, zHigh, losFlags))
			return 0;

		if (steps > 0) {
			if (LOS_CheckAtLocation(&dst, zHigh, losFlags))
				return 0;
		}

		return 1;
	}

	// Long-distance path: fixed-point ray stepping.
	dx_fp = (dx << 16) / steps;
	dy_fp = (dy << 16) / steps;
	dz_fp = (dz << 16) / (steps - 1);

	z_fp = ((int)(int16_t)src.z << 16) + 0x8000;

	// Ray 1 offset +perpendicular from center line.
	x1_fp = ((int)(int16_t)src.x << 16) + (dy_fp >> 2) + 0x8000;
	y1_fp = ((int)(int16_t)src.y << 16) + 0x8000 - (dx_fp >> 2);

	// Ray 2 offset -perpendicular.
	x2_fp = x1_fp - (dy_fp >> 1);
	y2_fp = y1_fp + (dx_fp >> 1);

	ray_alive[0] = 1;
	ray_alive[1] = 1;

	for (i = 0; i < steps - 1; i++) {
		CLocation tiles[2];
		CLocation prevLoc;
		int blocked;
		int curZ;

		x1_fp += dx_fp;
		y1_fp += dy_fp;
		x2_fp += dx_fp;
		y2_fp += dy_fp;

		CLocation_Init(&tiles[0]);
		CLocation_Init(&tiles[1]);
		tiles[0].x = (uint16_t)(x1_fp >> 16);
		tiles[0].y = (uint16_t)(y1_fp >> 16);
		tiles[0].z = (int16_t)(z_fp >> 16);
		tiles[1].x = (uint16_t)(x2_fp >> 16);
		tiles[1].y = (uint16_t)(y2_fp >> 16);
		tiles[1].z = (int16_t)(z_fp >> 16);

		// Advance z after setting tiles but before checking.
		z_fp += dz_fp;

		prevLoc.x = 0xFFFF;
		prevLoc.y = 0xFFFF;
		prevLoc.z = 0;

		blocked = 0;

		for (j = 0; j < 2; j++) {
			uint16_t tileID;

			if (ray_alive[j] == 0)
				continue;

			// Skip duplicate tile (both rays converged).
			if (CLocation_IsEqualXY(&tiles[j], &prevLoc))
				goto store_result;

			prevLoc.x = tiles[j].x;
			prevLoc.y = tiles[j].y;
			prevLoc.z = tiles[j].z;

			blocked = 1;

			tileID = Terrain_GetLandTileID((int)(int16_t)tiles[j].x, (int)(int16_t)tiles[j].y);
			if (g_LandTileData[tileID].flags & 0x10) {
				blocked = 0;
				goto store_result;
			}

			curZ = z_fp >> 16;
			if (curZ < (int)(int16_t)prevLoc.z) {
				prevLoc.z = (int16_t)curZ;
				curZ = (int)(int16_t)tiles[j].z;
			}
			if ((int)(int16_t)prevLoc.z > zLow)
				prevLoc.z -= 1;
			if (curZ < zHigh)
				curZ += 1;

			if (LOS_CheckAtLocation(&prevLoc, curZ, losFlags))
				blocked = 0;

store_result:
			ray_alive[j] = blocked;
		}

		if (ray_alive[0] == 0 && ray_alive[1] == 0)
			return 0;
	}

	return 1;
}

/*
 * 0x0046B1E7 - CEntity::CanSee
 *
 * Raycasts line-of-sight from entityA's vertical midpoint to
 * entityB's vertical midpoint.
 */
int
CEntity_CanSee(CItem *entityA, CItem *entityB, int flags)
{
	CLocation srcLoc, dstLoc, delta;
	CLocation *entLoc;
	int halfHeight;

	halfHeight = ((int (*)(void *))VT_FN(entityB, VT_GET_HEIGHT))(entityB) / 2;
	CLocation_Constructor3D(&delta, 0, 0, (int16_t)halfHeight);
	entLoc = ((CLocation * (*)(void *)) VT_FN(entityB, VT_GET_LOCATION))(entityB);
	CLocation_AddWrapped(entLoc, &dstLoc, &delta);

	halfHeight = ((int (*)(void *))VT_FN(entityA, VT_GET_HEIGHT))(entityA) / 2;
	CLocation_Constructor3D(&delta, 0, 0, (int16_t)halfHeight);
	entLoc = ((CLocation * (*)(void *)) VT_FN(entityA, VT_GET_LOCATION))(entityA);
	CLocation_AddWrapped(entLoc, &srcLoc, &delta);

	return CTerrainManager_LOSRaycast(&srcLoc, &dstLoc, flags);
}

/*
 * 0x0046B276 - CBlockManager::FindSpawnSpot
 *
 * Spiral-searches outward from loc for a walkable spawn position
 * within [zMin, zMax] rings, using a shuffled direction table.
 */
static int
CBlockManager_FindSpawnSpot(CLocation *loc, int walkZMin, int walkZMax, int zMin, int zMax, int height, int moveType, CItem *mob)
{
	CLocation origLoc;
	CLocation candidates[4];
	int dist, sweep, dirIdx;
	int z;
	int i, j, tmp;

	CLocation_SetLoc(&origLoc, loc);

	if (zMin <= 0) {
		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
			return 0;

		z = CTerrainManager_CanWalkWrapper(*loc, walkZMin, walkZMax, height, moveType, mob, 0);
		if (z != -128) {
			loc->z = (int16_t)z;
			Location_WrappedChebyshevDistance(&origLoc, loc);
			return 1;
		}

		zMin = 1;
	}

	for (i = 0; i < 4; i++) {
		j = rand() & 3;
		tmp = g_FindSpawnDirTable[i];
		g_FindSpawnDirTable[i] = g_FindSpawnDirTable[j];
		g_FindSpawnDirTable[j] = tmp;
	}

	for (dist = zMin; dist <= zMax; dist++) {
		for (sweep = 1 - dist; sweep <= dist; sweep++) {
			ArrayIterator_ForEach(candidates, 6, 4, (void (*)(void *))CLocation_Init);

			CLocation_Set(&candidates[0], (int16_t)((int)(int16_t)origLoc.x + sweep), (int16_t)((int)(int16_t)origLoc.y - dist), origLoc.z);
			CLocation_Set(&candidates[1], (int16_t)((int)(int16_t)origLoc.x + dist), (int16_t)((int)(int16_t)origLoc.y + sweep), origLoc.z);
			CLocation_Set(&candidates[2], (int16_t)((int)(int16_t)origLoc.x - sweep), (int16_t)((int)(int16_t)origLoc.y + dist), origLoc.z);
			CLocation_Set(&candidates[3], (int16_t)((int)(int16_t)origLoc.x - dist), (int16_t)((int)(int16_t)origLoc.y - sweep), origLoc.z);

			for (dirIdx = 0; dirIdx < 4; dirIdx++) {
				int d = g_FindSpawnDirTable[dirIdx];

				CLocation_SetLoc(loc, &candidates[d]);

				if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
					continue;

				z = CTerrainManager_CanWalkWrapper(*loc, walkZMin, walkZMax, height, moveType, mob, 0);
				if (z == -128)
					continue;

				loc->z = (int16_t)z;
				Location_WrappedChebyshevDistance(&origLoc, loc);
				return 1;
			}
		}
	}

	return 0;
}

/*
 * 0x0046B4F3 - FindSpawnSpotInBox
 *
 * Randomly samples ((dx*dy*factor) >> 8 + 1) points inside the box
 * [min..max], returning the first that passes CanWalk, Z bounds, and
 * an optional callback.
 */
int
FindSpawnSpotInBox(CLocation *result, int16_t minX, int16_t minY, int16_t minZ, int16_t maxX, int16_t maxY, int16_t maxZ, int factor, int height, int moveType, CItem *mob,
        int (*callback)(CLocation *))
{
	int dx, dy, attempts;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)minX, (int)minY))
		return 0;
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)maxX, (int)maxY))
		return 0;

	if ((int)minX > (int)maxX)
		SwapInt16(&minX, &maxX);
	if ((int)minY > (int)maxY)
		SwapInt16(&minY, &maxY);
	if ((int)minZ > (int)maxZ)
		SwapInt16(&minZ, &maxZ);

	dx = (int)maxX - (int)minX + 1;
	dy = (int)maxY - (int)minY + 1;
	attempts = ((dx * dy * factor) >> 8) + 1;

	while (attempts > 0) {
		int ry, rx, z;
		CLocation minLoc, delta, candidate, temp;

		attempts--;

		// Binary calls GetRandom(dy) before GetRandom(dx).
		ry = GetRandom(dy);
		rx = GetRandom(dx);
		CLocation_Constructor3D(&delta, (int16_t)rx, (int16_t)ry, 0);
		minLoc.x = (uint16_t)minX;
		minLoc.y = (uint16_t)minY;
		minLoc.z = minZ;
		CLocation_AddWrapped(&minLoc, &candidate, &delta);

		z = CTerrainManager_CanWalkWrapper(candidate, (int)minZ, (int)maxZ, height, moveType, mob, 0);

		if (z == -128)
			continue;

		if ((int)minZ > z)
			continue;
		if ((int)maxZ < z)
			continue;

		CLocation_SetLoc(&temp, &candidate);
		temp.z = (int16_t)z;

		if (callback != NULL) {
			if (!callback(&temp))
				continue;
		}

		CLocation_SetLoc(result, &temp);
		return 1;
	}

	return 0;
}

/*
 * 0x0046B692 - CTerrainManager::FindEntitiesAtXYZ
 *
 * Pushes into result every entity at (x,y) with z in [zMin, zMax]
 * that passes the optional filter. Returns match count.
 */
int
CTerrainManager_FindEntitiesAtXYZ(int16_t x, int16_t y, int16_t zMin, int16_t zMax, CVector *result, int (*filter)(CItem *))
{
	int blockIdx;
	int count;
	CItem *walk;

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	if (blockIdx == -1)
		return 0;

	count = 0;
	walk = g_MapBlocks[blockIdx].itemHead;
	while (walk != NULL) {
		if ((int16_t)walk->resourceEntity.entity.location.x == (int16_t)x && (int16_t)walk->resourceEntity.entity.location.y == (int16_t)y &&
		        (int16_t)walk->resourceEntity.entity.location.z >= (int16_t)zMin && (int16_t)walk->resourceEntity.entity.location.z <= (int16_t)zMax) {
			if (filter == NULL || filter(walk) == 1) {
				count++;
				CVector_PushBack(result, (uintptr_t)walk);
			}
		}
		walk = walk->spatialNext;
	}

	return count;
}

/*
 * 0x0046B75E - CTerrainManager::FindDoorAtLoc
 *
 * Returns the first door entity at loc within [minZ, maxZ], or NULL.
 */
CItem *
CTerrainManager_FindDoorAtLoc(CLocation *loc, int16_t minZ, int16_t maxZ)
{
	CVector localVec;
	uintptr_t *iter;
	CItem *entity;

	CVector_Constructor(&localVec, "\x04");

	CTerrainManager_FindEntitiesAtXYZ((int16_t)loc->x, (int16_t)loc->y, minZ, maxZ, &localVec, Terrain_IsContainerFilter);

	iter = (uintptr_t *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)&localVec);
	while (iter != (uintptr_t *)localVec.end) {
		entity = (CItem *)*iter;
		if (((int (*)(void *))VT_FN(entity, VT_IS_DOOR))(entity) == 1) {
			CVector_Destructor(&localVec);
			return entity;
		}
		iter++;
	}

	CVector_Destructor(&localVec);
	return NULL;
}

/*
 * 0x0046B841 - CTerrainManager::CanWalkWrapper
 *
 * Thin wrapper that forwards all args to CTerrainManager_CanWalk.
 */
int
CTerrainManager_CanWalkWrapper(CLocation loc, int minZ, int maxZ, int height, int moveType, CItem *mob, int useInterpolatedZ)
{
	return CTerrainManager_CanWalk(loc, minZ, maxZ, height, moveType, mob, useInterpolatedZ);
}

/*
 * 0x0046B87E - CBlockManager::FindSpawnSpotExt
 *
 * Wrapper that forwards all args to CBlockManager_FindSpawnSpot.
 */
int
CBlockManager_FindSpawnSpotExt(CLocation *loc, int walkZMin, int walkZMax, int zMin, int zMax, int height, int moveType, CItem *mob)
{
	return CBlockManager_FindSpawnSpot(loc, walkZMin, walkZMax, zMin, zMax, height, moveType, mob);
}

/*
 * 0x0046B8B3 - FindSpawnSpot
 *
 * Calls CBlockManager_FindSpawnSpotExt with walkZ bounds -128..128.
 */
int
FindSpawnSpot(CLocation *loc, int zMin, int zMax, int height, int moveType, CItem *mob)
{
	return CBlockManager_FindSpawnSpotExt(loc, -128, 128, zMin, zMax, height, moveType, mob);
}

/*
 * 0x0046B8F0 - CTerrainManager::`scalar deleting destructor'
 *
 * ORPHANED: zero callers in the binary. The CTerrainManager singleton
 * is never heap-allocated with delete, so this MSVC-emitted vtable[0]
 * thunk is never invoked.
 */
static __attribute__((unused)) void *
CTerrainManager_ScalarDelete(CTerrainManager *this, int flags)
{
	CTerrainManager_ConstructorVTable();
	USED(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0046B920 - SurfaceInfo::SurfaceInfo
 *
 * Constructor that delegates to SurfaceInfo_Set.
 */
static __attribute__((unused)) SurfaceInfo *
SurfaceInfo_Constructor(SurfaceInfo *this, uint32_t flags, int16_t z, int16_t height, CItem *item)
{
	SurfaceInfo_Set(this, flags, z, height, item);
	return this;
}

/*
 * 0x0046B950 - SurfaceInfo::Set
 *
 * Sets all fields of a SurfaceInfo.
 */
static void
SurfaceInfo_Set(SurfaceInfo *this, uint32_t flags, int16_t z, int16_t height, CItem *item)
{
	this->flags = flags;
	this->z = z;
	this->height = height;
	this->item = item;
}

/*
 * 0x0046B990 - SurfaceInfo::GetTopZ
 *
 * Returns the top Z of a surface, halving height for bridge items.
 */
static int
SurfaceInfo_GetTopZ(SurfaceInfo *s)
{
	if (s->item != NULL && (s->flags & TF_BRIDGE))
		return s->z + s->height / 2;
	return s->z + s->height;
}

/*
 * 0x0046B9E0 - LOSContext::LOSContext
 *
 * Initializes a LOSContext with the given zValue and flags.
 */
static __attribute__((unused)) void *
LOSContext_Constructor(LOSContext *this, int zValue, int flags)
{
	CIterCtx_Set((void *)this, (void *)(uintptr_t)zValue);
	this->flags = flags;
	return this;
}

/*
 * 0x0046BAB0 - SurfaceInfo reverse_iterator::operator*
 *
 * Dereferences a reverse iterator, returning the element one before
 * the stored pointer.
 */
static __attribute__((unused)) SurfaceInfo *
SurfaceInfoRevIter_Deref(SurfaceInfo **iter)
{
	return *iter - 1;
}

/*
 * 0x0046BAD0 - SurfaceInfo reverse_iterator::operator--
 *
 * Post-decrements a reverse iterator, returning the previous value.
 */
static __attribute__((unused)) SurfaceInfo **
SurfaceInfoRevIter_PostDec(SurfaceInfo **iter, SurfaceInfo **result)
{
	SurfaceInfo *saved = *iter;
	*iter = *iter - 1;
	*result = saved;
	return result;
}

/*
 * 0x0046BE80 - SurfaceInfo::SurfaceInfo (allocator construct)
 *
 * Allocator construct: copy-constructs a SurfaceInfo at dest from src.
 */
void
SurfaceInfo_ConstructorAlloc(CVector *this, SurfaceInfo *dest, SurfaceInfo *src)
{
	USED(this);
	StdDeque_AllocNodeSI(dest, src);
}

/*
 * 0x0046BED0 - LOS_CheckAtLocation
 *
 * Returns 1 if any dynamic or static item at loc blocks the LOS ray.
 */
static int
LOS_CheckAtLocation(CLocation *loc, int zValue, int flags)
{
	int blockIdx;
	CItem *cur;
	LOSContext ctx;

	ctx.zValue = zValue;
	ctx.flags = flags;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return 0;

	cur = g_SpatialGrid.cells[blockIdx].itemHead;
	while (cur != NULL) {
		if (CLocation_IsEqualXY(&cur->resourceEntity.entity.location, loc)) {
			if (LOS_BlockCheck(&ctx, loc, cur))
				return 1;
		}
		cur = cur->spatialNext;
	}

	cur = g_SpatialGrid.cells[blockIdx].staticHead;
	while (cur != NULL) {
		if (CLocation_IsEqualXY(&cur->resourceEntity.entity.location, loc)) {
			if (LOS_BlockCheck(&ctx, loc, cur))
				return 1;
		}
		cur = cur->resourceEntity.nextInContainer;
	}

	return 0;
}

/*
 * 0x0046BFC0 - LOS_BlockCheck
 *
 * Returns 1 if an entity overlaps the ray vertically and has at
 * least one tile flag matching the context's blocking mask.
 */
static int
LOS_BlockCheck(LOSContext *ctx, CLocation *loc, CItem *entity)
{
	int entFlags;

	if (!LOS_HeightCheck(ctx, loc, entity))
		return 0;

	entFlags = LOS_GetFlags(entity);
	if (entFlags & ctx->flags)
		return 1;

	return 0;
}

/*
 * 0x0046C010 - LOS_HeightCheck
 *
 * Returns 1 if entity's vertical extent intersects the LOS ray's
 * height range (entity.z < ctx->zValue and entity.z+height > loc->z).
 */
static int
LOS_HeightCheck(LOSContext *ctx, CLocation *loc, CItem *entity)
{
	CLocation *entLoc;
	int entZ, top;

	entLoc = &entity->resourceEntity.entity.location;
	entZ = (int)(int16_t)entLoc->z;
	if (entZ >= ctx->zValue)
		return 0;

	top = entZ + LOS_GetHeight(entity);
	if (top <= (int)(int16_t)loc->z)
		return 0;

	return 1;
}

/*
 * 0x0046C0A0 - SurfaceInfo_Fill
 *
 * std::fill specialization: copies src into each SurfaceInfo entry
 * in [first, last).
 */
void
SurfaceInfo_Fill(SurfaceInfo *first, SurfaceInfo *last, SurfaceInfo *src)
{
	while (first != last) {
		SurfaceInfo_CopyFrom(first, src);
		first++;
	}
}

/*
 * 0x0046C0D0 - SurfaceInfo::CopyFrom
 *
 * Copies all fields from src into this via SurfaceInfo_Set.
 */
SurfaceInfo *
SurfaceInfo_CopyFrom(SurfaceInfo *this, SurfaceInfo *src)
{
	SurfaceInfo_Set(this, src->flags, src->z, src->height, src->item);
	return this;
}

/*
 * 0x0046C110 - SurfaceInfo_CopyBackwardSI
 *
 * std::copy_backward specialization for SurfaceInfo: copies [first,
 * last) backward ending at destEnd.
 */
SurfaceInfo *
SurfaceInfo_CopyBackwardSI(SurfaceInfo *first, SurfaceInfo *last, SurfaceInfo *destEnd)
{
	while (first != last) {
		last--;
		destEnd--;
		SurfaceInfo_CopyFrom(destEnd, last);
	}
	return destEnd;
}

/*
 * 0x0046C140 - SurfaceInfo_AllocateN
 *
 * Allocates an array of count SurfaceInfo entries (negative counts
 * are clamped to 0).
 */
void *
SurfaceInfo_AllocateN(int count)
{
	if (count < 0)
		count = 0;
	return malloc(count * sizeof(SurfaceInfo));
}

/*
 * 0x0046C740 - CTerrainManager::IsContainerFilter
 *
 * Filter callback that dispatches to VT_IS_CONTAINER, used by
 * CTerrainManager_FindDoorAtLoc.
 */
static int
Terrain_IsContainerFilter(CItem *entity)
{
	return ((int (*)(void *))VT_FN(entity, VT_IS_CONTAINER))(entity);
}

// File-scope global CVector at binary 0x00699A30, stores arena page pointers.
CVector g_arenaPageVec;

__extension__ typedef struct CDequeBlock CDequeBlock;

/*
 * 0x0047A350 - CVector::Allocate4 (uint32_t allocator)
 *
 * Thiscall wrapper that forwards to Vector_AllocElements(count, 0),
 * which saturates count to zero and allocates count*4 bytes via
 * OperatorNew.
 */
void *
CVector_Allocate4_Terrain(CVector *self, uint32_t count, int unused)
{
	USED(self);
	USED(unused);
	if ((int32_t)count < 0)
		count = 0;
	return malloc(count * sizeof(uintptr_t));
}
/*
 * 0x004866FC - Static_Unlock
 *
 * No-op stub in the binary. Called after static entity operations.
 */
void
Static_Unlock(void)
{
}

/*
 * 0x00486701 - Static_Lock
 *
 * No-op stub in the binary. Called before static entity operations.
 */
void
Static_Lock(void)
{
}

/*
 * 0x004C4364 - Terrain_LoadStaticsBlock
 *
 * Creates static entities from a raw statics.mul block (7-byte
 * entries: itemID, xOff, yOff, z, hue) and links them into the
 * block's staticHead chain.
 */
static void
Terrain_LoadStaticsBlock(int extra, int blockIdx, uint8_t *data, int dataLen)
{
	CLocation loc;
	int blockWorldX, blockWorldY;
	int offset;

	USED(extra);

	CLocation_Init(&loc);
	CBlockManager_GetBlockOrigin(&g_SpatialGrid, blockIdx, &blockWorldX, &blockWorldY);

	for (offset = 0; offset < dataLen; offset += 7) {
		CItem *item;
		uint16_t itemID;
		uint8_t xOff, yOff;
		int8_t z;
		uint16_t hue;

		item = CreateStaticEntity();

		memcpy(&itemID, data, 2);
		data += 2;
		SwapEndian(&itemID);

		xOff = *data++;
		yOff = *data++;
		z = *data++;

		memcpy(&hue, data, 2);
		data += 2;
		SwapEndian(&hue);

		CEntity_SetBodyType(item, itemID);

		CLocation_Set(&loc, (int16_t)(blockWorldX + (xOff & 0xFF)), (int16_t)(blockWorldY + (yOff & 0xFF)), (int16_t)z);

		CLocation_SetLoc(&item->resourceEntity.entity.location, &loc);

		item->resourceEntity.entity.color = hue;

		((void (*)(CItem *, CLocation *))VT_FN(item, VT_SET_LOCATION))(item, &loc);
	}
}

/*
 * 0x004C4499 - Terrain_LoadStatics
 *
 * Reads staidx0.mul + statics0.mul via CIndexedFileManager; for each
 * block destroys existing statics and repopulates via
 * Terrain_LoadStaticsBlock.
 */
void
Terrain_LoadStatics(void)
{
	CIndexedFileManager fm;
	int blockIdx;

	CIndexedFileManager_Constructor(&fm);
	CIndexedFileManager_Open(&fm, GLOBAL_file_staidx0_mul, GLOBAL_file_statics0_mul, "rb");

	for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
		uint8_t *data;
		int dataLen;
		int extra;

		while (g_SpatialGrid.cells[blockIdx].staticHead != NULL) {
			CItem *head = g_SpatialGrid.cells[blockIdx].staticHead;
			if (head != NULL)
				((CItem * (*)(CItem *, int)) VT_FN(head, VT_DTOR))(head, 1);
		}

		data = NULL;
		CIndexedFileManager_ReadBlock(&fm, blockIdx, &data, &dataLen, &extra);

		Terrain_LoadStaticsBlock(extra, blockIdx, data, dataLen);

		if (data != NULL) {
			free(data);
			data = NULL;
		}
	}

	CIndexedFileManager_Close(&fm);
	CIndexedFileManager_Destructor(&fm);
}

/*
 * 0x004DE4E7 - MapFileManager::SeekBlock
 *
 * No-op in the demo binary. Would seek to a file offset for map I/O.
 */
void
MapFileManager_SeekBlock(int offset)
{
	USED(offset);
}

/*
 * 0x004DE4F4 - MapFileManager::WriteBlock
 *
 * No-op in the demo binary. Would write a map/data block to file.
 */
void
MapFileManager_WriteBlock(int offset, void *data, int length)
{
	USED(offset);
	USED(data);
	USED(length);
}

/*
 * 0x004DE501 - MapFileManager::FlushBlock
 *
 * No-op in the demo binary. Would flush a pending block write.
 */
void
MapFileManager_FlushBlock(int offset)
{
	USED(offset);
}

/*
 * LOS (Line of Sight) raycast system.
 * Binary: functions at 0x0046ADA5..0x0046C068.
 * Uses a dual-ray Bresenham approach with fixed-point arithmetic.
 */

// CLocation_IsEqualXY and CLocation_Init declared via location.h.

/*
 * vtable[0x28] (GetHeight) and vtable[0x30] (GetFlags) dispatch.
 *
 * The binary calls the entity's virtual GetHeight/GetFlags at each LOS
 * call site. Dispatching through the vtable routes each entity class to
 * its own implementation:
 *   CItem        (0x004910A7 / 0x004322C0): tiledata height/flags by
 *                bodyType plus a door-open offset read from itemFlags.
 *   CMobile      (0x0046F32F / 0x0044A7D0): height 16, flags 0.
 *   StaticEntity (0x00491078 / 0x00491230): tiledata height/flags by
 *                bodyType only. Static entities are CResourceEntity-sized
 *                and have no itemFlags field, so the CItem path's itemFlags
 *                read would run past the allocation.
 */
int
LOS_GetHeight(CItem *ent)
{
	return ((int (*)(void *))VT_FN(ent, VT_GET_HEIGHT))(ent);
}

static int
LOS_GetFlags(CItem *ent)
{
	return ((int (*)(void *))VT_FN(ent, VT_GET_FLAGS))(ent);
}

/*
 * 0x0061BB98 - direction corner lookup table
 *
 * Indexed by dir/2. Used by GetInterpolatedZ for corner selection.
 */
const int g_TerrainCornerDX[4] = { 1, 1, 0, 0 };
const int g_TerrainCornerDY[4] = { 0, 1, 1, 0 };

// 8-direction delta tables.
const int g_TerrainDirDX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
const int g_TerrainDirDY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// Number of static items loaded (for diagnostics).
// Binary: 0x0068B380
int g_StaticItemCount;

// Number of dynamic items in the world.
// Binary: 0x0068B384
uint32_t g_DynamicItemCount;

// 0x006CA8FC - free list head for recycled static items.
// FreeStaticItem (0x0048686C) links freed statics here via nextInContainer.
CItem *g_StaticFreeList;

// 0x006CA8E4 - when set, CMobile_GetSurfaceFlags_VT returns 0 (mobiles non-blocking).
// Binary: CTerrainManager_GetValidZAtEntity saves/sets this to 1 before terrain queries.
int g_IgnoreMobiles;

// 0x006982F8 - 1 when terrain data (map0.mul) has been loaded, 0 otherwise.
// Checked by SetTerrainTile to reject modifications before loading completes.
int g_TerrainDataLoaded;

/*
 * Helper - SurfaceInfo_CompareZ
 *
 * qsort comparator matching the binary's std::sort predicate at
 * 0x0046C430: ascending z, then ascending height as tiebreaker.
 */
static int
SurfaceInfo_CompareZ(const void *a, const void *b)
{
	const SurfaceInfo *sa = (const SurfaceInfo *)a;
	const SurfaceInfo *sb = (const SurfaceInfo *)b;
	if (sa->z != sb->z)
		return (sa->z < sb->z) ? -1 : 1;
	if (sa->height != sb->height)
		return (sa->height < sb->height) ? -1 : 1;
	return 0;
}
