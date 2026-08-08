/*
 * CBlockManager - spatial grid index of entities.
 *
 * Divides the world into fixed-size blocks and stores each entity in
 * the block that matches its tile. Provides the per-block iterators
 * used by range queries and the block-crossing hooks that keep the
 * index synchronised with entity movement.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "account.h"
#include "book.h"
#include "container.h"
#include "convo.h"
#include "defcon.h"
#include "egg.h"
#include "gamecentmon.h"
#include "load.h"
#include "main.h"
#include "multi.h"
#include "nodepool.h"
#include "npc.h"
#include "packet_handler.h"
#include "player.h"
#include "region.h"
#include "resbank.h"
#include "socket.h"
#include "time.h"
#include "vtable.h"
#include "weapon.h"
#include "weather.h"
#include "world.h"

static void CBlock_Init(CBlock *blk); // 0x0042F070

CBlockManager g_SpatialGrid;

int g_UpdatesSuppressCount; // 0x006EFF44
int g_UpdatesEnabled = 1;  // 0x00624E3C (binary default: 1 = enabled)

/*
 * 0x0042EB10 - CBlockManager::Init
 *
 * Zero-initializes a CBlockManager before CBlockManager_Setup populates it.
 */
void
CBlockManager_Init(CBlockManager *this)
{
	this->gridWidth = 0;
	this->gridHeight = 0;
	this->totalBlocks = 0;
	this->cells = NULL;
	this->unk10 = 0;
}

/*
 * 0x0042EB4F - UpdateRegion
 *
 * For scripted or mobile entities, registers enter (0x10) and leave
 * (0x11) ObjVar tracking nodes on the entity's grid cell.
 */
void
UpdateRegion(CItem *entity)
{
	int blockIdx;
	CVector enterList;
	CVector leaveList;
	char enterFlag;
	char leaveFlag;
	uintptr_t *iter;
	CBlockTrackingNode *node;

	if (entity == NULL)
		return;

	if (!CItem_HasScripts(entity)) {
		if (!VT_IsMobile(entity))
			return;
	}

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &entity->resourceEntity.entity.location, 0);
	if (blockIdx == -1)
		return;

	enterFlag = 0;
	CVector_Constructor(&enterList, &enterFlag);
	leaveFlag = 0;
	CVector_Constructor(&leaveList, &leaveFlag);

	CItem_PopulateObjVarList(entity, &enterList, 0x10);
	CItem_PopulateObjVarList(entity, &leaveList, 0x11);

	iter = (uintptr_t *)enterList.begin;
	while (iter != (uintptr_t *)enterList.end) {
		node = (CBlockTrackingNode *)OperatorNew(sizeof(CBlockTrackingNode));
		node->entity = entity;
		node->data[0] = *iter;
		node->data[1] = 0x10;
		node->next = g_SpatialGrid.cells[blockIdx].trackingHead;
		g_SpatialGrid.cells[blockIdx].trackingHead = node;
		iter++;
	}

	iter = (uintptr_t *)leaveList.begin;
	while (iter != (uintptr_t *)leaveList.end) {
		node = (CBlockTrackingNode *)OperatorNew(sizeof(CBlockTrackingNode));
		node->entity = entity;
		node->data[0] = *iter;
		node->data[1] = 0x11;
		node->next = g_SpatialGrid.cells[blockIdx].trackingHead;
		g_SpatialGrid.cells[blockIdx].trackingHead = node;
		iter++;
	}

	CVector_Destructor(&leaveList);
	CVector_Destructor(&enterList);
}

/*
 * 0x0042ED29 - Block_RemoveTrackingNode
 *
 * Removes every tracking node belonging to entity from the grid cell
 * containing it.
 */
void
Block_RemoveTrackingNode(CItem *entity)
{
	int blockIdx;
	CBlockTrackingNode **pp;
	CBlockTrackingNode *node;

	if (entity == NULL)
		return;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, CEntity_GetLocation(&entity->resourceEntity.entity), 0);
	if (blockIdx == -1)
		return;

	pp = &g_SpatialGrid.cells[blockIdx].trackingHead;
	while (*pp != NULL) {
		if ((*pp)->entity == entity) {
			node = *pp;
			*pp = node->next;
			OperatorDelete(node);
		} else {
			pp = &(*pp)->next;
		}
	}
}

/*
 * 0x0042EDBF - CheckWalkPassability
 *
 * Collects the tracking nodes within range 7 of oldLoc, then for each
 * nearby entity fires Entity_ExecuteEvent; returns 0 if any event blocks
 * the walk or if mob disappears mid-scan.
 */
int
CheckWalkPassability(CItem *mob, CLocation *oldLoc, CLocation *newLoc)
{
	int result;
	uint32_t mobSerial;
	int blockBuf[0x400];
	int *blockIter;
	CBlockTrackingNode *node;
	int count;
	uint32_t serials[0x400];
	uintptr_t types[0x400];
	uintptr_t args[0x400];
	CItem *ent;
	CItem *nearEnt;
	int i;

	result = 1;
	mobSerial = mob->serial;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, oldLoc, 7, blockBuf, 0x400);

	count = 0;
	blockIter = blockBuf;

	while (*blockIter != -1) {
		node = g_SpatialGrid.cells[*blockIter].trackingHead;
		while (node != NULL) {
			if (node->entity == NULL || node->entity == mob) {
				node = node->next;
				continue;
			}
			serials[count] = node->entity->serial;
			types[count] = node->data[0];
			args[count] = node->data[1];
			count += 1;
			if (count >= 0x400)
				goto phase2;
			node = node->next;
		}
		blockIter += 1;
	}

phase2:
	for (i = 0; i < count; i++) {
		ent = CWorld_FindBySerial(g_World, mobSerial);
		if (ent == NULL || !VT_IsMobile(ent)) {
			result = 0;
			goto done;
		}
		if (ent != mob) {
			result = 0;
			goto done;
		}

		nearEnt = CWorld_FindBySerial(g_World, serials[i]);
		if (nearEnt == NULL)
			continue;

		if (!Entity_ExecuteEvent(&nearEnt->resourceEntity.entity, (int)args[i], types[i], mob, oldLoc, newLoc)) {
			result = 0;
			goto done;
		}
	}

done:
	ent = CWorld_FindBySerial(g_World, mobSerial);
	if (ent == NULL || !VT_IsMobile(ent))
		return 0;
	if (ent != mob)
		return 0;
	return result;
}

/*
 * 0x0042F070 - CBlock::Init
 *
 * Zeros the cell and sets chain heads and weather fields to defaults.
 */
static void
CBlock_Init(CBlock *blk)
{
	memset(blk, 0, 0xC0);
	blk->staticHead = NULL;
	blk->itemHead = NULL;
	blk->chunkEgg = NULL;
	blk->trackingHead = NULL;
	blk->flags110 = 0;
	blk->flags111 = 0;
	blk->weatherSeason = 0xFFFF;
	blk->weatherDay = 0xFFFF;
	blk->weatherNight = 0xFFFF;
	blk->lightLevel = 0;
}

/*
 * 0x0042F199 - CBlockManager::Lock
 *
 * No-op stub.
 */
void
CBlockManager_Lock(CBlockManager *this)
{
	USED(this);
}

/*
 * 0x0042F1A4 - CBlockManager::Unlock
 *
 * No-op stub.
 */
void
CBlockManager_Unlock(CBlockManager *this)
{
	USED(this);
}

/*
 * 0x0042F1AF - CBlockManager::Setup
 *
 * Allocates the grid cells, initializes each spatial map, and kicks off
 * the main data load.
 *
 * MODIFIED: spatial maps use static C instances instead of heap-allocated
 * C++ multiset objects. The binary's `new CBlock[totalBlocks]` expansion is
 * inlined here as malloc + an explicit CBlock_Init loop. Also folds in
 * CNPCHash_Constructor and MultiComponentPool_Init, which the binary runs
 * earlier via MSVC __initterm static initializers (we lack that hook).
 */
void
CBlockManager_Setup(CBlockManager *this, int widthTiles, int heightTiles)
{
	int i;

	this->gridWidth = widthTiles / 8;
	this->gridHeight = heightTiles / 8;
	this->totalBlocks = this->gridWidth * this->gridHeight;

	this->cells = malloc(this->totalBlocks * sizeof(CBlock));
	if (this->cells == NULL)
		return;

	for (i = 0; i < this->totalBlocks; i++)
		CBlock_Init(&this->cells[i]);

	ItemMap_Init();
	MobileMap_Init();
	CNPCHash_Constructor();
	CNPCMap_Init();
	MultiComponentPool_Init();
	RegionManager_AllocGrid();

	InitPoolSizes();

	CBlockManager_LoadAll(this);

	NamedResource_LoadAll();
}

/*
 * 0x0042F229 - CBlockManager::GetBlockOrigin
 *
 * Computes world X,Y origin of a block from its index.
 * Column-major layout: blockIdx / gridHeight = column, remainder = row.
 */
void
CBlockManager_GetBlockOrigin(CBlockManager *this, int blockIdx, int *outX, int *outY)
{
	*outX = g_mapStartX + (blockIdx / this->gridHeight) * 8;
	*outY = g_mapStartY + (blockIdx % this->gridHeight) * 8;
}

/*
 * 0x0042F265 - CBlockManager::GetBlockIndex
 *
 * Returns the column-major block index for (x, y), or -1 if outside the
 * configured map. The third argument is ignored but kept to match the
 * binary's signature.
 */
int
CBlockManager_GetBlockIndex(CBlockManager *this, int x, int y, int unused)
{
	int cellX, cellY;

	USED(unused);

	if (x < g_mapStartX)
		return -1;
	if (x >= g_mapStartX + g_mapWidth)
		return -1;
	if (y < g_mapStartY)
		return -1;
	if (y >= g_mapStartY + g_mapHeight)
		return -1;

	cellX = (x - g_mapStartX) >> 3;
	cellY = (y - g_mapStartY) >> 3;
	return cellX * this->gridHeight + cellY;
}

/*
 * 0x0042F265 - Terrain_GetBlockIndex
 *
 * Column-major block index for world coordinates. X varies slower than Y.
 */
int
Terrain_GetBlockIndex(int x, int y)
{
	int bx, by;

	if (!Terrain_InBounds(x, y))
		return -1;
	bx = (x - g_MapOriginX) >> 3;
	by = (y - g_MapOriginY) >> 3;
	return bx * g_MapBlocksH + by;
}

/*
 * 0x0042F2CF - CBlockManager::GetBlockIndexFromLoc
 *
 * Delegates to GetBlockIndex using loc->x, loc->y.
 */
int
CBlockManager_GetBlockIndexFromLoc(CBlockManager *this, CLocation *loc, int extra)
{
	return CBlockManager_GetBlockIndex(this, (int)loc->x, (int)loc->y, extra);
}

/*
 * 0x0042F2F7 - CBlockManager::GetNearbyBlocks
 *
 * Fills blockBuf[] with block indices of all cells within 'range'
 * tiles of 'loc'. Terminates with -1 sentinel. Column-major layout.
 */
void
CBlockManager_GetNearbyBlocks(CBlockManager *this, CLocation *loc, int range, int *blockBuf, int maxBlocks)
{
	int minCellX, maxCellX, minCellY, maxCellY;
	int cellX, cellY, count, idx;

	minCellX = ((int)loc->x - range - g_mapStartX) >> 3;
	maxCellX = ((int)loc->x + range - g_mapStartX) >> 3;
	minCellY = ((int)loc->y - range - g_mapStartY) >> 3;
	maxCellY = ((int)loc->y + range - g_mapStartY) >> 3;

	count = 0;
	for (cellY = minCellY; cellY <= maxCellY; cellY++) {
		for (cellX = minCellX; cellX <= maxCellX; cellX++) {
			if (cellY < 0 || cellY >= this->gridHeight)
				continue;
			if (cellX < 0 || cellX >= this->gridWidth)
				continue;

			idx = cellX * this->gridHeight + cellY;
			blockBuf[count] = idx;
			count++;

			if (count >= maxBlocks - 1) {
				blockBuf[count] = -1;
				return;
			}
		}
	}
	blockBuf[count] = -1;
}

/*
 * 0x0042F40B - CBlockManager::IsValidCoordAbsolute
 *
 * True if (x, y) lies in the absolute UO map [0..6143, 0..4095].
 */
int
CBlockManager_IsValidCoordAbsolute(CBlockManager *this, int x, int y)
{
	USED(this);
	if (x < 0 || x > 0x17FF)
		return 0;
	if (y < 0 || y > 0xFFF)
		return 0;
	return 1;
}

/*
 * 0x0042F43F - CBlockManager::IsValidCoord
 *
 * Checks against configured map bounds (startX..startX+width, etc.).
 */
int
CBlockManager_IsValidCoord(CBlockManager *this, int x, int y)
{
	USED(this);
	if (x < g_mapStartX)
		return 0;
	if (x >= g_mapStartX + g_mapWidth)
		return 0;
	if (y < g_mapStartY)
		return 0;
	if (y >= g_mapStartY + g_mapHeight)
		return 0;
	return 1;
}

/*
 * 0x0042F43F - Terrain_InBounds
 *
 * True if (x, y) lies within the loaded map.
 */
int
Terrain_InBounds(int x, int y)
{
	if (x < g_MapOriginX || x >= g_MapOriginX + (g_MapBlocksW * 8))
		return 0;
	if (y < g_MapOriginY || y >= g_MapOriginY + (g_MapBlocksH * 8))
		return 0;
	return 1;
}

/*
 * 0x0042F48C - CBlockManager::IsNearBorder
 *
 * True if (x, y) is within 18 tiles of an active map border (each
 * border is gated by the matching g_Config flag).
 */
int
CBlockManager_IsNearBorder(CBlockManager *this, int x, int y)
{
	if (!CBlockManager_IsValidCoord(this, x, y))
		return 0;

	if (g_Config.borderNorth) {
		if (y - g_mapStartY <= 0x12)
			return 1;
	}
	if (g_Config.borderWest) {
		if (x - g_mapStartX <= 0x12)
			return 1;
	}
	if (g_Config.borderSouth) {
		if (g_mapStartY + g_mapHeight - y <= 0x12)
			return 1;
	}
	if (g_Config.borderEast) {
		if (g_mapStartX + g_mapWidth - x <= 0x12)
			return 1;
	}
	return 0;
}

/*
 * 0x0042F522 - CBlockManager::WrapCoord
 *
 * No-op stub; copies loc to itself and returns 0.
 */
int
CBlockManager_WrapCoord(CBlockManager *this, CLocation *loc)
{
	USED(this);
	CLocation_SetLoc(loc, loc);
	return 0;
}

/*
 * 0x0042F53D - CBlockManager::GetItemsAtLocationXY
 *
 * Appends every item at (loc->x, loc->y) to list. Z is ignored.
 */
void
CBlockManager_GetItemsAtLocationXY(CBlockManager *this, CVector *list, CLocation *loc)
{
	int idx;
	CItem *cur;

	idx = CBlockManager_GetBlockIndexFromLoc(this, loc, 0);
	if (idx < 0)
		return;

	cur = this->cells[idx].itemHead;
	while (cur != NULL) {
		if ((int)loc->x == (int)CEntity_GetLocation(&cur->resourceEntity.entity)->x && (int)loc->y == (int)CEntity_GetLocation(&cur->resourceEntity.entity)->y) {
			CVector_PushBack(list, (uintptr_t)cur);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x0042F5CB - CBlockManager::GetItemsAtLocationXYZ
 *
 * Appends every item at (loc->x, loc->y) whose z is at or above loc->z.
 */
void
CBlockManager_GetItemsAtLocationXYZ(CBlockManager *this, CVector *list, CLocation *loc)
{
	int idx;
	CItem *cur;

	idx = CBlockManager_GetBlockIndexFromLoc(this, loc, 0);
	if (idx < 0)
		return;

	cur = this->cells[idx].itemHead;
	while (cur != NULL) {
		if ((int)loc->x == (int)CEntity_GetLocation(&cur->resourceEntity.entity)->x && (int)loc->y == (int)CEntity_GetLocation(&cur->resourceEntity.entity)->y &&
		        (int)loc->z <= (int)CEntity_GetLocation(&cur->resourceEntity.entity)->z) {
			CVector_PushBack(list, (uintptr_t)cur);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x0042F670 - CBlockManager::GetItemsAtLocation
 *
 * Appends every item at (loc->x, loc->y) whose z is at or below loc->z.
 */
void
CBlockManager_GetItemsAtLocation(CBlockManager *this, CVector *list, CLocation *loc)
{
	int idx;
	CItem *cur;

	idx = CBlockManager_GetBlockIndexFromLoc(this, loc, 0);
	if (idx < 0)
		return;

	cur = this->cells[idx].itemHead;
	while (cur != NULL) {
		if ((int)loc->x == (int)CEntity_GetLocation(&cur->resourceEntity.entity)->x && (int)loc->y == (int)CEntity_GetLocation(&cur->resourceEntity.entity)->y &&
		        (int)loc->z >= (int)CEntity_GetLocation(&cur->resourceEntity.entity)->z) {
			CVector_PushBack(list, (uintptr_t)cur);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x0042F715 - CBlockManager::RestoreItems
 *
 * Returns the items named by list to valid ground. When the first item
 * resolves a new Z via the terrain manager the whole group is shifted by
 * the same delta; otherwise each player is sent home and each item is
 * replaced via CItem_PlaceInWorld.
 */
int
CBlockManager_RestoreItems(CBlockManager *this, CVector *list)
{
	uintptr_t *iter;
	CItem *entity;
	CLocation localLoc;
	int firstTime;
	int zDelta;
	int validHeight;
	int outZ;
	int origZ;
	uint32_t height;

	USED(this);

	iter = (uintptr_t *)list->begin;
	firstTime = 1;
	zDelta = 0;
	validHeight = 1;

	for (;;) {
		if (iter == (uintptr_t *)list->end)
			break;

		entity = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		iter++;

		if (entity == NULL)
			continue;

		if (((int (*)(void *))VT_FN(entity, VT_HAS_CONTAINER))(entity))
			continue;

		if (firstTime) {
			firstTime = 0;
			outZ = 0;

			origZ = (int)(int16_t)CEntity_GetLocation(&entity->resourceEntity.entity)->z;

			height = ((int (*)(void *))VT_FN(entity, VT_GET_HEIGHT))(entity);

			if (CTerrainManager_GetValidZAtEntity(entity, &outZ, height)) {
				zDelta = outZ - origZ;
			} else {
				validHeight = 0;
			}
		}

		if (validHeight) {
			if (zDelta != 0) {
				CLocation_SetLoc(&localLoc, CEntity_GetLocation(&entity->resourceEntity.entity));
				localLoc.z = (int16_t)((int16_t)localLoc.z + (int16_t)zDelta);
				((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);
				((void (*)(void *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, &localLoc);
			}
		} else {
			if (VT_IsPlayer(entity)) {
				CPlayer_ReturnToHome((CPlayer *)entity);
			} else {
				CItem_PlaceInWorld(entity, 1);
			}
		}
	}

	return 1;
}

/*
 * 0x0042F8A7 - CEntityMap::RemoveFromGrid
 *
 * Sorts vec by serial and runs the Z validation walker over it.
 */
int
CEntityMap_RemoveFromGrid(CBlockManager *this, CVector *vec)
{
	char cmp1 = 0;
	char cmp2 = 0;
	char result;

	USED(this);

	SortBySerial_Entry((uintptr_t *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)vec), (uintptr_t *)vec->end, cmp1);

	result = EntityMap_ForEachValidateSerialZ((uintptr_t *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)vec), (uintptr_t *)vec->end, cmp2);
	USED(result);

	return 1;
}

/*
 * 0x0042FA08 - LoadAll_Books
 *
 * Delegates to BookContent_loadAll.
 */
void
LoadAll_Books(void)
{
	BookContent_loadAll();
}

/*
 * 0x0042FA12 - LoadAll_RegionGrid
 *
 * Sorts regions (non-LVN first, then LVN by descending area) and stamps
 * each grid cell they cover with the region's weather/light fields. The
 * last writer wins, so smaller LVN regions take precedence.
 */
void
LoadAll_RegionGrid(void)
{
	CResList sortedList;
	CSearchCtx ctx, temp;
	CResListNode *node;
	CRegion *newReg, *existing;
	int col, row;

	CResListNode_Constructor_bin((CResListNode *)&sortedList);

	CSearchCtx_Constructor(&ctx);
	CSearchCtx_Add(&ctx, CResManager_BeginIterWrapper(&g_RegionRM, &temp));

	while (CSearchCtx_Find(&ctx)) {
		newReg = *(CRegion **)CResManager_GetResultCtx(&g_RegionRM, &ctx);

		CSearchCtx_Add(&ctx, CResManager_NextIterWrapper(&g_RegionRM, &temp, &ctx));

		node = CResList_Begin(&sortedList);
		for (;;) {
			if (!CResList_IsValid(&sortedList, node)) {
				CResList_AppendAndStore_ByNameAll(&sortedList, (uint32_t *)&newReg);
				break;
			}
			existing = *(CRegion **)CResList_GetData(&sortedList, node);

			if (!Region_HasWeatherData(newReg)) {
				CResList_InsertAndStore_ByNameAll(&sortedList, node, (uint32_t *)&newReg);
				break;
			}

			if (!Region_HasWeatherData(existing)) {
				node = CResList_Next(&sortedList, node);
				continue;
			}

			if (CRegion_GetArea(newReg) > CRegion_GetArea(existing)) {
				CResList_InsertAndStore_ByNameAll(&sortedList, node, (uint32_t *)&newReg);
				break;
			}

			node = CResList_Next(&sortedList, node);
		}
	}

	node = CResList_Begin(&sortedList);
	while (CResList_IsValid(&sortedList, node)) {
		CRegion *r = *(CRegion **)CResList_GetData(&sortedList, node);
		int startCol, startRow, endCol, endRow;

		startCol = ((int)r->x - g_mapStartX) / 8;
		startRow = ((int)r->y - g_mapStartY) / 8;
		endCol = ((int)r->x + (int)r->width - g_mapStartX) / 8;
		endRow = ((int)r->y + (int)r->height - g_mapStartY) / 8;

		for (col = startCol; col <= endCol; col++) {
			if (col < 0 || col >= g_SpatialGrid.gridWidth)
				continue;
			for (row = startRow; row <= endRow; row++) {
				CBlock *blk;

				if (row < 0 || row >= g_SpatialGrid.gridHeight)
					continue;
				blk = &g_SpatialGrid.cells[col * g_SpatialGrid.gridHeight + row];
				blk->weatherSeason = (uint16_t)r->weatherSeason;
				if (blk->weatherSeason == 0)
					blk->weatherSeason = 0xFFFF;
				blk->weatherDay = (uint16_t)r->weatherDay;
				blk->weatherNight = (uint16_t)r->weatherNight;
				blk->lightLevel = r->lightLevel;
			}
		}

		node = CResList_Next(&sortedList, node);
	}

	CResList_Destructor_ByNameAllVal(&sortedList);
}

/*
 * 0x0042FD01 - CBlockManager::LoadAll
 *
 * Sequential loader for every data subsystem, with progress updates.
 *
 * MODIFIED: calls subsystem initializers that the binary performs via
 * CRT static init or pre-Setup globals, bridges standalone C globals to
 * CWorld fields, and enables normal decay (the binary ships disabled).
 */
void
CBlockManager_LoadAll(CBlockManager *this)
{
	USED(this);

	g_World = calloc(1, sizeof(CWorld));
	if (g_World == NULL) {
		fprintf(stderr, "CBlockManager_LoadAll: failed to allocate g_World\n");
		return;
	}
	CDataManager_Init(g_World);
	g_ItemTileData = (TileDataEntry *)g_World->itemTileData;
	g_LandTileData = (LandTileData *)g_World->landTileData;
	g_ResEntitySlots = (ResEntitySlot *)g_World->resEntitySlots;
	CWorld_InitDecay(1);
	CTimeManager_Init();
	TagNodePool_InitA();
	{
		uint8_t initByte = 0;
		StdPtrList_Init(&g_loginScriptList, &initByte);
		StdPtrList_Init(&g_entityMgrList, &initByte);
	}
	CResBankManager_Init();
	CDefcon_Init();

	Noop_47DF20("loading");
	ProgressBar_Update(0);

	LoadAll_Skills();
	LoadAll_Books();

	ProgressBar_Update(5);
	LoadAll_ObjScrBitmap();
	WeatherManager_startup();

	ProgressBar_Update(10);
	LoadAll_TiledataVersion();
	LoadAll_TemplateIdx();
	LoadAll_AnimData();

	ProgressBar_Update(15);
	Terrain_LoadTileData();
	LoadAll_MultiManager();
	LoadAll_StartupScript();

	ProgressBar_Update(20);
	LoadResTypes();

	ProgressBar_Update(25);
	LoadItemRes();

	ProgressBar_Update(30);
	CStringMatcher_Init(&g_StringMatcher, 10, 0x200);
	CConversationManager_Init(&g_ConvoMgr);

	ProgressBar_Update(35);
	Terrain_LoadMap();

	ProgressBar_Update(40);
	LoadAll_TemplateStartup();

	ProgressBar_Update(50);
	CWeaponManager_LoadAll(&g_WeaponManager);

	ProgressBar_Update(60);
	LoadResRegions();
	InitResourceTypeIds();
	LoadAll_Statics();

	ProgressBar_Update(70);
	LoadDynamicPhase();

	// Custom: accounts load after dynamic so max accountNum is known.
	Account_Init();
	Account_LoadAll();

	ProgressBar_Update(80);
	LoadAll_LoginScriptEntries();
	EnsureSpawnEntriesBuilt();
	g_RegionsFilePath = GLOBAL_file_regions_txt;
	RegionManager_Init();

	ProgressBar_Update(90);
	LoadAll_LVNData();
	LoadAll_RegionGrid();
	InitTemplateClassification();
	LoadAll_ResBankDistrib();
	LoadAll_ArtDim();

	ProgressBar_Update(95);
	g_LoadingComplete = 1;
	ProgressBar_Update(-1);

	// Custom: seed the world with 10 minutes of initial spawn.
	if (g_SpawnEnabled) {
		g_IsInitialSpawn = 1;
		g_AutoInitialSpawnDeadline = GetTickCount_UO() + 600000;
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "spawn", "misc", "initial spawn enabled for 600 seconds");
	}
}

/*
 * 0x0042FE70 - ArrayIterator_ForEach
 *
 * Invokes callback(ptr) count times, stepping ptr by stride each call.
 */
void
ArrayIterator_ForEach(void *ptr, int stride, int count, void (*callback)(void *))
{
	for (;;) {
		count--;
		if (count < 0)
			break;
		callback(ptr);
		ptr = (char *)ptr + stride;
	}
}

/*
 * Helper - CBlockManager_GetBlock
 *
 * Returns the grid cell at blockIdx, or NULL if out of range.
 */
CBlock *
CBlockManager_GetBlock(CBlockManager *this, int blockIdx)
{
	if (blockIdx < 0 || blockIdx >= this->totalBlocks)
		return NULL;
	return &this->cells[blockIdx];
}
