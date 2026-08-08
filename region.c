/*
 * Region system - named map areas with game-rule flags.
 *
 * RegionManager reads regions.txt into a list of CRegion rectangles
 * and serves the spatial, prefix, and name lookups used by combat
 * (guards), spawning, weather, and chat broadcasts.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bankdefs.h"
#include "blockmanager.h"
#include "cstring.h"
#include "dat.h"
#include "help_queue.h"
#include "io.h"
#include "item.h"
#include "load.h"
#include "player.h"
#include "region.h"
#include "res.h"
#include "utils.h"
#include "weather.h"

/*
 * One chained entry in the region-def hash table (16 bytes on 32-bit).
 */
__extension__ typedef struct CRegionDefNode CRegionDefNode;
struct CRegionDefNode {
	void *data;           // +0x00
	uint32_t field04;     // +0x04
	uint32_t field08;     // +0x08
	CRegionDefNode *next; // +0x0C
};

/*
 * Hash-table header for CRegionDefNode lists.
 */
__extension__ typedef struct CRegionDefList CRegionDefList;
struct CRegionDefList {
	CRegionDefNode **buckets; // +0x00
	int count;                // +0x04
	uint8_t flag;             // +0x08
};

static void *CRegionDefList_Constructor(CRegionDefList *this, int count, char flag); // 0x004695B0
static void CRegionDefList_Cleanup(CRegionDefList *this); // 0x00469600
static void *CRegionDefNode_ScalarDelete(CRegionDefNode *this, int flags); // 0x004696A0
static void CRegionDefNode_ScalarDtor(CRegionDefNode *this); // 0x00469720
static void CRegion_InitFields(CRegion *this); // 0x004A3EF2
static int CRegion_GetOccupancy(CRegion *this, int unused); // 0x004A418E
static void RegionManager_Destructor(void); // 0x004A4371
static int RegionManager_EraseByPrefix(uint32_t prefix); // 0x004A44A4
static FILE *RegionManager_SaveRegionLine(uint16_t version, FILE *fp, CRegion *region); // 0x004A46E5
static void RegionManager_SaveRegionBinary(uint16_t version, FILE *fp, CRegion *region); // 0x004A4804
static void RegionManager_ParseRegionLine(uint16_t version, const char *line, int prefixFlag, int prefixValue); // 0x004A4A71
static void CRegion_NullDestructor1(CRegion *this, uint16_t arg); // 0x004A500F
static int RegionManager_ValidateAndInsert(CRegion *region, int flags); // 0x004A4C42
static void RegionManager_SortedAreaInsert(CRegion *region, CResList *list); // 0x004A4DD7
static void RegionManager_FilterEmptyDesc(CResList *list); // 0x004A5029
static void RegionManager_RebuildGrid(void); // 0x004A6046
static void *CRegionGrid_ScalarDelete(CRegionGrid *this, int flag); // 0x004A6110
static void CRegionGrid_VectorDtor(CRegionGrid *this); // 0x004A62B0
static int CRegionGrid_IsInBounds(CRegionGrid *this, int x, int y); // 0x004A6510
__extension__ typedef struct CRegion_Client CRegion_Client;
static void RegionManager_readRegionsIntoMemory(CRegion_Client **regions); // 0x005568CC

CRegionGrid *g_RegionGrid;
uint16_t g_RegionManagerVersion; // binary: RegionManager+0x00 (0x006990B0)
CResManager g_RegionByFileRM;  // binary: RegionManager+0x04 (0x006990B4)
CResManager g_RegionByNameRM;  // binary: RegionManager+0x21C (0x006992CC)
CResManager g_RegionRM;        // binary: RegionManager+0x434 (0x006994E4)
int g_RegionByNameCount;       // binary: 0x006DA96C
int g_RegionAllCount;          // binary: 0x006DA970
const char *g_RegionsFilePath; // binary: [0x006999DC], set during FileManager init

/*
 * 0x0042FEA0 - Region_HasWeatherData
 *
 * Returns 1 if the region has a valid (non-zero, non-0xFFFF)
 * weatherDay or weatherSeason value.
 */
int
Region_HasWeatherData(CRegion *region)
{
	uint16_t weatherDay, season;

	weatherDay = (uint16_t)region->weatherDay;
	if (weatherDay != 0xFFFF && weatherDay != 0)
		return 1;

	season = (uint16_t)region->weatherSeason;
	if (season != 0xFFFF && season != 0)
		return 1;

	return 0;
}

/*
 * 0x004695B0 - CRegionDefList::CRegionDefList
 *
 * Allocates a zero-filled bucket array of count entries.
 */
static __attribute__((unused)) void *
CRegionDefList_Constructor(CRegionDefList *this, int count, char flag)
{
	this->count = count;
	this->flag = flag;
	this->buckets = (CRegionDefNode **)OperatorNew(count * (int)sizeof(CRegionDefNode *));
	memset(this->buckets, 0, count * sizeof(CRegionDefNode *));
	return this;
}

static void *CRegionDefNode_ScalarDelete(CRegionDefNode *this, int flags);

/*
 * 0x00469600 - CRegionDefList::Cleanup
 *
 * Frees each bucket's node chain and the bucket array.
 */
static __attribute__((unused)) void
CRegionDefList_Cleanup(CRegionDefList *this)
{
	int i;
	CRegionDefNode *node;
	CRegionDefNode *cur;
	CRegionDefNode *tmp;

	i = 0;
	while (i < this->count) {
		if (this->buckets[i] != NULL) {
			node = this->buckets[i];
			while (node != NULL)
				node = node->next;
			cur = this->buckets[i];
			tmp = cur;
			if (tmp != NULL)
				CRegionDefNode_ScalarDelete(tmp, 1);
		}
		i++;
	}
	OperatorDelete(this->buckets);
}

/*
 * 0x004696A0 - CRegionDefNode::ScalarDelete
 *
 * Scalar deleting destructor for region def nodes.
 */
static void *
CRegionDefNode_ScalarDelete(CRegionDefNode *this, int flags)
{
	CRegionDefNode_ScalarDtor(this);
	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x00469720 - CRegionDefNode dtor
 *
 * Frees the node's data pointer.
 */
static void
CRegionDefNode_ScalarDtor(CRegionDefNode *this)
{
	OperatorDelete(this->data);
}

/*
 * 0x004A3E28 - Noop_4A3E28
 *
 * No-op; called from CEScript_CmdQLoad with three ignored args.
 */
void
Noop_4A3E28(void *player, const char *valueBuf, int param)
{
	USED(player);
	USED(valueBuf);
	USED(param);
}

/*
 * 0x004A3E30 - WeatherNode scalar deleting destructor
 *
 * Unlinks and optionally frees a weather node.
 */
void *
WeatherNode_ScalarDelete(WeatherNode *this, int flags)
{
	WeatherNode_Unlink(this);

	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x004A3E60 - CResBankSetMember scalar deleting destructor
 *
 * Removes the member from the spatial list and optionally frees it.
 */
void *
CResBankSetMember_ScalarDelete(CResBankSetMember *this, int flags)
{
	CResBankSetMember_RemoveFromSpatialList(this);
	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x004A3E90 - WeatherNodeList scalar deleting destructor
 *
 * Destroys children and optionally frees the list.
 */
void *
WeatherNodeList_ScalarDelete(PathNodeList *this, int flags)
{
	WeatherNodeList_DestroyChildren(this);

	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x004A3EC0 - CRegion::Init
 *
 * Wrapper that forwards to CRegion_InitFields.
 */
CRegion *
CRegion_Init(CRegion *this)
{
	CRegion_InitFields(this);
	return this;
}

/*
 * 0x004A3ED6 - CRegion::Assign
 *
 * Assignment operator wrapping CRegion_CopyFrom.
 */
CRegion *
CRegion_Assign(CRegion *this, CRegion *src)
{
	CRegion_CopyFrom(this, src);
	return this;
}

/*
 * 0x004A3EF2 - CRegion::InitFields
 *
 * Zeroes all CRegion fields and empties the two name strings.
 */
static void
CRegion_InitFields(CRegion *this)
{
	this->prefix = 0;
	this->y = 0;
	this->x = 0;
	this->height = 0;
	this->width = 0;
	this->zMax = 0;
	this->zMin = 0;
	this->type = 0;
	this->weatherNight = 0;
	this->weatherDay = 0;
	this->weatherSeason = 0;
	strcpy(this->name, "");
	strcpy(this->name2, "");
	this->lightLevel = 0;
}

/*
 * 0x004A3F8F - CRegion::isCoordInside
 *
 * Returns 1 if the location lies within the region's bounding box
 * and Z range.
 */
int
CRegion_isCoordInside(CRegion *r, CLocation *loc)
{
	int16_t x = (int16_t)loc->x;
	int16_t y = (int16_t)loc->y;
	int16_t z = loc->z;

	if (x < r->x || x > r->x + r->width)
		return 0;
	if (y < r->y || y > r->y + r->height)
		return 0;
	if (z < r->zMin || z > r->zMax)
		return 0;
	return 1;
}

/*
 * 0x004A4021 - CRegion::CopyFrom
 *
 * Copies all fields from src into this region.
 */
void
CRegion_CopyFrom(CRegion *this, CRegion *src)
{
	this->prefix = src->prefix;
	memcpy(this->name, src->name, REGION_NAME_LEN);
	this->x = src->x;
	this->y = src->y;
	this->width = src->width;
	this->height = src->height;
	this->zMin = src->zMin;
	this->zMax = src->zMax;
	memcpy(this->name2, src->name2, REGION_DESC_LEN);
	this->weatherSeason = src->weatherSeason;
	this->weatherDay = src->weatherDay;
	this->weatherNight = src->weatherNight;
	this->type = src->type;
	this->lightLevel = src->lightLevel;
}

/*
 * 0x004A4103 - CRegion::GetArea
 *
 * Returns the region's width * height.
 */
int
CRegion_GetArea(CRegion *this)
{
	return (int)this->width * (int)this->height;
}

/*
 * 0x004A411F - CRegion::~CRegion
 *
 * No-op destructor.
 */
void
CRegion_Destructor(CRegion *this)
{
	USED(this);
}

/*
 * 0x004A412A - CRegion::ValidateBounds
 *
 * Returns 1 if width/height are non-negative and zMin <= zMax.
 */
int
CRegion_ValidateBounds(CRegion *this)
{
	if (this->x > this->x + this->width)
		return 0;
	if (this->y > this->y + this->height)
		return 0;
	if (this->zMin > this->zMax)
		return 0;
	return 1;
}

/*
 * 0x004A418E - CRegion::GetOccupancy
 *
 * Scans every tile in the region's bounding rectangle and returns
 * the percentage of tiles that hold a static item.
 */
static int __attribute__((unused))
CRegion_GetOccupancy(CRegion *this, int unused)
{
	int found;
	int totalCells;
	int occupiedCells;
	int curX;
	int curY;
	int blockIdx;
	CItem *iter;
	CLocation *loc;
	int areaFactor;
	int occupancyPct;
	int deadStore;

	USED(unused);
	found = 0;
	totalCells = 0;
	occupiedCells = 0;

	for (curX = (int)this->x; curX < (int)this->x + (int)this->width; curX++) {
		for (curY = (int)this->y; curY < (int)this->y + (int)this->height; curY++) {
			found = 0;
			blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, curX, curY, 0);
			if (blockIdx > -1) {
				iter = g_SpatialGrid.cells[blockIdx].staticHead;
				while (iter != NULL) {
					loc = CEntity_GetLocation((CEntity *)iter);
					if ((int)(int16_t)loc->x == curX) {
						if ((int)(int16_t)loc->y == curY) {
							found = 1;
						}
					}
					iter = (CItem *)iter->resourceEntity.nextInContainer;
				}
			}
			totalCells++;
			if (found != 0)
				occupiedCells++;
		}
	}

	if (totalCells != 0) {
		areaFactor = totalCells / 64;
		occupancyPct = occupiedCells * 100 / totalCells;
		deadStore = areaFactor * occupancyPct / 100;
		USED(deadStore);
	}

	if (totalCells != 0)
		return occupiedCells * 100 / totalCells;
	return 0;
}

/*
 * 0x004A42F0 - RegionManager::RegionManager
 *
 * Initializes the ByFile, ByName, and All CResManagers, clears the
 * grid pointer, and writes the default save-format version (0xFACF).
 */
void
RegionManager_Constructor(void)
{
	CResManager_Constructor(&g_RegionByFileRM, 1);
	CResManager_Constructor(&g_RegionByNameRM, 1);
	CResManager_Constructor(&g_RegionRM, 1);
	g_RegionGrid = NULL;
	g_RegionManagerVersion = 0xFACF;
}

/*
 * 0x004A4371 - RegionManager::~RegionManager
 *
 * Destroys the grid (if present) and the three CResManagers in
 * reverse order.
 */
static __attribute__((unused)) void
RegionManager_Destructor(void)
{
	if (g_RegionGrid != NULL)
		CRegionGrid_ScalarDelete(g_RegionGrid, 1);

	CResManager_StrKeyDestructor_ByNameAll(&g_RegionRM);
	CResManager_StrKeyDestructor_ByNameAll(&g_RegionByNameRM);
	CResManager_IntKeyDestructor_ByFile(&g_RegionByFileRM);
}

/*
 * 0x004A4408 - RegionManager::InitOccupancy
 *
 * Pre-computes CRegion_GetOccupancy on every region at init; the
 * per-region result is discarded.
 */
void
RegionManager_InitOccupancy(void)
{
	CSearchCtx iterCtx;
	CSearchCtx beginCtx;
	CSearchCtx nextCtx;
	CRegion *region;
	int occupancy;

	CSearchCtx_Constructor(&iterCtx);
	CSearchCtx_Add(&iterCtx, CResManager_BeginIterWrapper(&g_RegionRM, &beginCtx));

	while (CSearchCtx_Find(&iterCtx)) {
		region = *(CRegion **)CResManager_GetResultCtx(&g_RegionRM, &iterCtx);

		occupancy = CRegion_GetOccupancy(region, 0);
		USED(occupancy);

		CSearchCtx_Add(&iterCtx, CResManager_NextIterWrapper(&g_RegionRM, &nextCtx, &iterCtx));
	}
}

/*
 * 0x004A4489 - RegionManager::Init
 *
 * Loads regions from regions.txt, then initializes occupancy.
 */
void
RegionManager_Init(void)
{
	RegionManager_LoadRegions();
	RegionManager_InitOccupancy();
}

/*
 * 0x004A44A4 - RegionManager::EraseByPrefix
 *
 * Removes the region with the given prefix from the ByFile RM
 * and, if present, from the ByName and All RMs. Returns 1 if a
 * region was erased.
 */
static int
RegionManager_EraseByPrefix(uint32_t prefix)
{
	CSearchCtx ctx1;
	CSearchCtx ctx2;
	CSearchCtx out1, out2, out3, out4, out5, out6;

	CSearchCtx_Constructor(&ctx1);
	CSearchCtx_Constructor(&ctx2);

	// BeginIter on ByFile RM (this+0x04). Binary: 0x004A6B70.
	CSearchCtx_Add(&ctx1, CResList_BeginIter_ByFile(&g_RegionByFileRM, &out1, &prefix, 1));

	if (!CSearchCtx_Find(&ctx1))
		return 0;

	// EraseAndFree on ByFile RM. Binary: 0x004A6C70.
	CResList_EraseAndFree_ByFile(&g_RegionByFileRM, &out2, &ctx1, 1);

	// BeginIter on ByName RM (this+0x21C). Binary: 0x004A6D80.
	CSearchCtx_Add(&ctx2, CResList_BeginIter_ByNameAll(&g_RegionByNameRM, &out3, &prefix, 1));

	if (CSearchCtx_Find(&ctx2)) {
		// EraseAndFree on ByName RM. Binary: 0x004A6E40.
		CResList_EraseAndFree_ByNameAll(&g_RegionByNameRM, &out4, &ctx2, 1);
	}

	// BeginIter on All RM (this+0x434). Binary: 0x004A6D80.
	CSearchCtx_Add(&ctx2, CResList_BeginIter_ByNameAll(&g_RegionRM, &out5, &prefix, 1));

	if (CSearchCtx_Find(&ctx2)) {
		// EraseAndFree on All RM. Binary: 0x004A6E40.
		CResList_EraseAndFree_ByNameAll(&g_RegionRM, &out6, &ctx2, 1);
	}

	return 1;
}

/*
 * 0x004A45B1 - RegionManager::AddRegion
 *
 * GM editor entry point for adding or updating a region. Replaces
 * any existing entry with the same prefix, inserts into ByFile
 * and ByName RMs, adds to the All RM if the coords are in-bounds,
 * then rebuilds the grid.
 */
void
RegionManager_AddRegion(CRegion *region)
{
	CSearchCtx iterCtx;
	CSearchCtx output;
	CRegion *found;

	if (region->prefix >= 0x2000)
		return;

	RegionManager_EraseByPrefix(region->prefix);

	if (strcmp(region->name, "") == 0)
		strcpy(region->name, "NONE");

	// FindByKey on ByFile RM. Binary: 0x004A69E0.
	// Passes region as uint32_t* keyPtr (prefix is at offset 0).
	CSearchCtx_Constructor(&iterCtx);
	CSearchCtx_Add(&iterCtx, CResManager_FindByKey_ByFile(&g_RegionByFileRM, &output, (uint32_t *)region));

	if (!CSearchCtx_Find(&iterCtx))
		goto rebuild;

	// GetResult+NextIter on ByFile RM. Binary: 0x004A6C30.
	// Binary passes region pointer as direction (nonzero = forward).
	CResList_GetResultNextIter_ByFile(&g_RegionByFileRM, &iterCtx, 1);

	// GetResultCtx returns the CRegion* stored in the RM entry.
	found = (CRegion *)CResManager_GetResultCtx(&g_RegionByFileRM, &iterCtx);

	// Insert into ByName RM. Binary: 0x004A6D10 on this+0x21C.
	// Passes found as uint32_t* keyPtr (prefix at offset 0), &found as valuePtr.
	CResList_Insert_ByNameAll(&g_RegionByNameRM, (uint32_t *)found, (void **)&found);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)found->x, (int)(int16_t)found->y)) {
		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)found->x + (int)(int16_t)found->width, (int)(int16_t)found->y + (int)(int16_t)found->height))
			goto rebuild;
	}

	// Insert into All RM. Binary: 0x004A6D10 on this+0x434.
	CResList_Insert_ByNameAll(&g_RegionRM, (uint32_t *)found, (void **)&found);

rebuild:
	RegionManager_RebuildGrid();
}

/*
 * 0x004A46E5 - RegionManager::SaveRegionLine
 *
 * Writes a CRegion as one text line to fp. Versions > 0xFACE
 * include weatherNight and lightLevel.
 */
static __attribute__((unused)) FILE *
RegionManager_SaveRegionLine(uint16_t version, FILE *fp, CRegion *region)
{
	if ((uint16_t)version > 0xFACE) {
		fprintf(fp, "%5u %5hd %5hd %5hd %5hd %5hd %5hd %12d %s %hu %hu %hu %hu %u %s\n", region->prefix, region->x, region->y, region->width, region->height, region->zMin,
		        region->zMax, 0, region->name, (unsigned short)region->weatherSeason, (unsigned short)region->weatherDay, (unsigned short)region->weatherNight,
		        (unsigned short)region->type, (unsigned int)(int16_t)region->lightLevel, region->name2);
	} else {
		fprintf(fp, "%5u %5hd %5hd %5hd %5hd %5hd %5hd %12d %s %hu %hu %hu %s\n", region->prefix, region->x, region->y, region->width, region->height, region->zMin,
		        region->zMax, 0, region->name, (unsigned short)region->weatherSeason, (unsigned short)region->weatherDay, (unsigned short)region->type, region->name2);
	}
	return fp;
}

/*
 * 0x004A4804 - RegionManager::SaveRegionBinary
 *
 * Writes a CRegion to fp in binary (big-endian) form. Versions
 * > 0xFACE include weatherNight and lightLevel.
 */
static __attribute__((unused)) void
RegionManager_SaveRegionBinary(uint16_t version, FILE *fp, CRegion *region)
{
	int tmp;

	fwrite_ServerSide(region->name, 0x28, 1, fp);

	tmp = region->prefix;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = (int)(int16_t)region->x;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = (int)(int16_t)region->y;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = (int)(int16_t)region->width;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = (int)(int16_t)region->height;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = (int)(int16_t)region->zMin;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = (int)(int16_t)region->zMax;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = 0;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	fwrite_ServerSide(region->name2, 0x28, 1, fp);

	tmp = (unsigned short)region->weatherDay;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = (unsigned short)region->weatherSeason;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	if ((uint16_t)version > 0xFACE) {
		tmp = (unsigned short)region->weatherNight;
		SwapEndian(&tmp);
		fwrite_ServerSide(&tmp, 4, 1, fp);
	}

	fwrite_ServerSide(&region->type, 1, 1, fp);

	if ((uint16_t)version > 0xFACE) {
		tmp = (int)(int16_t)region->lightLevel;
		SwapEndian(&tmp);
		fwrite_ServerSide(&tmp, 4, 1, fp);
	}
}

/*
 * 0x004A4A71 - RegionManager::ParseRegionLine
 *
 * Parses one text line into a newly allocated CRegion and inserts
 * it via ValidateAndInsert. Versions > 0xFACE expect the extended
 * format with weatherNight and lightLevel.
 */
static void
RegionManager_ParseRegionLine(uint16_t version, const char *line, int prefixFlag, int prefixValue)
{
	uint16_t type_temp;
	uint16_t season_temp;
	uint16_t day_temp;
	uint16_t night_temp;
	uint16_t light_temp;
	int flags;
	CRegion *region;
	CRegion *tmp;

	flags = 0;
	type_temp = 0;
	season_temp = 0;
	day_temp = 0;
	night_temp = 0;
	light_temp = 0;

	tmp = (CRegion *)OperatorNew(0x8C);
	if (tmp != NULL)
		region = CRegion_Init(tmp);
	else
		region = NULL;

	if (region == NULL)
		return;

	if ((version & 0xFFFF) > 0xFACE) {
		sscanf(line, "%u %hd %hd %hd %hd %hd %hd %d %s %hu %hu %hu %hu %hu %[^\n]s\n", &region->prefix, &region->x, &region->y, &region->width, &region->height,
		        &region->zMin, &region->zMax, &flags, region->name, &season_temp, &day_temp, &night_temp, &type_temp, &light_temp, region->name2);
		region->weatherNight = (int16_t)night_temp;
		region->lightLevel = (int16_t)light_temp;
	} else {
		sscanf(line, "%u %hd %hd %hd %hd %hd %hd %d %s %hu %hu %hu %[^\n]s\n", &region->prefix, &region->x, &region->y, &region->width, &region->height, &region->zMin,
		        &region->zMax, &flags, region->name, &season_temp, &day_temp, &type_temp, region->name2);
	}

	region->weatherSeason = (int16_t)season_temp;
	region->weatherDay = (int16_t)day_temp;
	region->type = (uint8_t)type_temp;

	if (prefixFlag == 1)
		region->prefix = prefixValue & 0xFFFF;

	if (RegionManager_ValidateAndInsert(region, flags) != 0)
		return;

	CRegion_ScalarDelete(region, 1);
}

/*
 * 0x004A4C42 - RegionManager::ValidateAndInsert
 *
 * Validates a region, normalizes name/description, and inserts it
 * into the ByFile and ByName RMs. Also inserts into the All RM
 * when at least one corner is in-bounds. Returns 1 on success.
 */
static int
RegionManager_ValidateAndInsert(CRegion *region, int flags)
{
	USED(flags);

	if (!CRegion_ValidateBounds(region))
		return 0;

	if (strcmp(region->name, "") == 0 || strcmp(region->name, "0") == 0)
		strcpy(region->name, "NONE");

	if (strcmp(region->name2, "NONE") == 0)
		strcpy(region->name2, "");

	if (!CResManager_Insert_ByFile(&g_RegionByFileRM, (uint32_t *)region, region))
		return 0;

	CResList_Insert_ByNameAll(&g_RegionByNameRM, (uint32_t *)region, (void **)&region);

	g_RegionByNameCount++;

	if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)region->x, (int)(int16_t)region->y) ||
	        CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)region->x + (int)(int16_t)region->width, (int)(int16_t)region->y + (int)(int16_t)region->height)) {
		g_RegionAllCount++;
		CResList_Insert_ByNameAll(&g_RegionRM, (uint32_t *)region, (void **)&region);
	}

	return 1;
}

/*
 * 0x004A4DB1 - RegionManager::PopulateByLocation
 *
 * Populates outList with ByName regions containing loc.
 */
int
RegionManager_PopulateByLocation(CResList *outList, CLocation *loc)
{
	return RegionManager_PopulateFromRM(outList, loc, &g_RegionByNameRM);
}

/*
 * 0x004A4DD7 - RegionManager::SortedAreaInsert
 *
 * Inserts region into list in ascending-area order.
 */
static void
RegionManager_SortedAreaInsert(CRegion *region, CResList *list)
{
	CResListNode *node;

	node = CResList_Begin(list);
	for (;;) {
		CRegion *existing;

		if (!CResList_IsValid(list, node)) {
			CResList_AppendAndStore_ByNameAll(list, (uint32_t *)&region);
			return;
		}

		existing = *(CRegion **)CResList_GetData(list, node);
		if (CRegion_GetArea(existing) > CRegion_GetArea(region)) {
			CResList_InsertAndStore_ByNameAll(list, node, (uint32_t *)&region);
			return;
		}

		node = CResList_Next(list, node);
	}
}

/*
 * 0x004A4E53 - RegionManager::PopulateFromRM
 *
 * Walks an RM and inserts every region containing loc into
 * outList, sorted by area ascending. Returns 1 if outList is
 * non-empty.
 */
int
RegionManager_PopulateFromRM(CResList *outList, CLocation *loc, CResManager *rm)
{
	CSearchCtx ctx, temp;
	CRegion *region;

	CSearchCtx_Constructor(&ctx);
	CResManager_BeginIter(rm, &temp);
	CSearchCtx_Add(&ctx, &temp);

	while (CSearchCtx_Find(&ctx)) {
		region = *(CRegion **)CResManager_GetResultCtx(rm, &ctx);

		if (CRegion_isCoordInside(region, loc))
			RegionManager_SortedAreaInsert(region, outList);

		CResManager_NextIter(rm, &temp, &ctx);
		CSearchCtx_Add(&ctx, &temp);
	}

	return outList->count > 0 ? 1 : 0;
}

/*
 * 0x004A4EEC - RegionManager::LoadRegions
 *
 * Reads regions.txt: parses the version header then each line via
 * ParseRegionLine, and rebuilds the spatial grid at the end.
 */
void
RegionManager_LoadRegions(void)
{
	FILE *fp;
	FILE *fp2;
	char line[512];
	int firstLine;
	uint16_t version;

	g_RegionByNameCount = 0;
	g_RegionAllCount = g_RegionByNameCount;

	fp = fopen_ServerSide(g_RegionsFilePath, "rb");
	if (fp == NULL) {
		CRegion_NullDestructor1(NULL, g_RegionManagerVersion);
		fp2 = fopen_ServerSide(g_RegionsFilePath, "rb");
		USED(fp2);
	}

	firstLine = 1;
	version = 0xFACE;

	while (fgets_ServerSide(line, 0x1FF, fp) != NULL) {
		if (firstLine) {
			firstLine = 0;
			if (sscanf(line, "version %hu\n", &version) == 1)
				continue;
			version = 0xFACE;
		}
		RegionManager_ParseRegionLine(version, line, 0, 0);
	}

	fclose_ServerSide(fp);
	RegionManager_RebuildGrid();
}

/*
 * 0x004A500F - CRegion no-op destructor 1
 *
 * No-op.
 */
static void
CRegion_NullDestructor1(CRegion *this, uint16_t arg)
{
	USED(this);
	USED(arg);
}

/*
 * 0x004A501C - CRegion no-op destructor 2
 *
 * No-op.
 */
void
CRegion_NullDestructor2(CRegion *this, uint16_t arg)
{
	USED(this);
	USED(arg);
}

/*
 * 0x004A5029 - RegionManager::FilterEmptyDesc
 *
 * Removes regions with empty description (name2) from list.
 */
static void
RegionManager_FilterEmptyDesc(CResList *list)
{
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		CRegion *region = *(CRegion **)CResList_GetData(list, node);
		if (strcmp(region->name2, "") == 0) {
			node = (CResListNode *)CResList_EraseAndFree_ByNameAllVal(list, node, 1);
		} else {
			node = CResList_Next(list, node);
		}
	}
}

/*
 * 0x004A509A - RegionManager::GetDescAt
 *
 * Writes the description of the first described region containing loc
 * into outStr and returns 1, or clears outStr and returns 0 when no
 * region qualifies. The cut-down form of GetLocalizedDesc: no sort, no
 * dungeon-entrance table and no second location.
 */
static __attribute__((unused)) int
RegionManager_GetDescAt(CString *outStr, CLocation *loc)
{
	CResList list;
	CResListNode *node;
	int found;

	CResListNode_Constructor_bin((CResListNode *)&list);

	RegionManager_PopulateByLocation(&list, loc);
	RegionManager_FilterEmptyDesc(&list);
	CString_Clear(outStr);

	node = CResList_Begin(&list);
	if (!CResList_IsValid(&list, node)) {
		found = 0;
		CResList_Destructor_ByNameAllVal(&list);
		return found;
	}

	CString_AssignCStr(outStr, (*(CRegion **)CResList_GetData(&list, node))->name2);
	found = 1;
	CResList_Destructor_ByNameAllVal(&list);
	return found;
}

/*
 * 0x004A5164 - RegionManager::GetLocalizedDesc
 *
 * Finds the smallest described region containing inLoc1, fills
 * outStr with its description, and writes a representative coord
 * (dungeon entrance for types 1-8, bounding-box center otherwise)
 * to outLoc. Returns 0 if no region, 1 if inLoc2 is outside it,
 * 2 if inLoc2 is inside.
 */
int
RegionManager_GetLocalizedDesc(CString *outStr, int16_t *outLoc, CLocation *inLoc1, CLocation *inLoc2)
{
	// Dungeon entrance coords, binary table at 0x00620DE4.
	static const int16_t g_LocalizedCoords[8][3] = {
		{ 0x1444, 0x027e, 0x0000 }, // type 1: Destard
		{ 0x0498, 0x0a4c, 0x0000 }, // type 2: Covetous
		{ 0x09c3, 0x0395, 0x0000 }, // type 3: Shame
		{ 0x0201, 0x0618, 0x0000 }, // type 4: Wrong
		{ 0x07fa, 0x00d8, 0x000e }, // type 5: Despise
		{ 0x0511, 0x0439, 0x0000 }, // type 6: Doom
		{ 0x1711, 0x0011, 0x0040 }, // type 7: Hythloth
		{ 0x1711, 0x0011, 0x0040 }, // type 8: (same as 7)
	};
	CResList sortedList;
	CResListNode *node;
	CRegion *region;
	int16_t *pOut;
	int hasCoords;
	int type;
	int result;

	// Binary keeps var_20h = 0 through the function; paths for
	// nonzero values are dead.
	hasCoords = 0;
	pOut = outLoc;

	CResListNode_Constructor_bin((CResListNode *)&sortedList);

	RegionManager_PopulateByLocation(&sortedList, inLoc1);

	RegionManager_FilterEmptyDesc(&sortedList);

	CString_Clear(outStr);

	node = CResList_Begin(&sortedList);
	if (!CResList_IsValid(&sortedList, node)) {
		CResList_Destructor_ByNameAllVal(&sortedList);
		return 0;
	}

	region = *(CRegion **)CResList_GetData(&sortedList, node);

	CString_AssignCStr(outStr, region->name2);

	type = region->type;

	if (type > 0 && type <= 8) {
		pOut[0] = g_LocalizedCoords[type - 1][0];
		pOut[1] = g_LocalizedCoords[type - 1][1];
		pOut[2] = g_LocalizedCoords[type - 1][2];
	} else {
		pOut[0] = (int16_t)(region->x + region->width / 2);
		pOut[1] = (int16_t)(region->y + region->height / 2);
		pOut[2] = (int16_t)((region->zMin + region->zMax) / 2);
	}

	if (CRegion_isCoordInside(region, inLoc2)) {
		if (hasCoords)
			result = 4;
		else
			result = 2;
	} else {
		if (hasCoords)
			result = 3;
		else
			result = 1;
	}

	CResList_Destructor_ByNameAllVal(&sortedList);
	return result;
}

/*
 * 0x004A537F - CRegion scalar deleting destructor
 *
 * Invokes both no-op destructors with the region's id word.
 */
void
CRegion_ScalarDtor(CRegion *this)
{
	uint16_t id = *(uint16_t *)this;
	CRegion_NullDestructor2(this, id);
	CRegion_NullDestructor1(this, id);
}

/*
 * 0x004A5620 - RegionManager::SearchRegionsByPrefix
 *
 * Appends every region at loc whose name matches the given prefix
 * (case-insensitive) to outList.
 */
void
RegionManager_SearchByPrefix(CResList *outList, CLocation *loc, const char *prefix)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *iter, *end;
	int prefixLen;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, loc);

	prefixLen = (int)strlen(prefix);

	iter = (uintptr_t *)localVec.begin;
	end = (uintptr_t *)localVec.end;
	while (iter != end) {
		char *regionName = ((CRegion *)(*iter))->name;

		if (strncasecmp(regionName, prefix, prefixLen) == 0) {
			CResList_AppendAndStore_ByNameAll(outList, (uint32_t *)iter);
		}
		iter++;
	}

	CVector_Destructor(&localVec);
}

/*
 * 0x004A56FD - SearchTag
 *
 * Returns the iterator to the first region whose name matches the
 * tag prefix (case-insensitive), or list->end.
 */
uintptr_t *
RegionGrid_SearchTag(CVector *list, const char *tag)
{
	uintptr_t *iter, *end;
	int tagLen;

	tagLen = strlen(tag);

	iter = (uintptr_t *)list->begin;
	end = (uintptr_t *)list->end;
	while (iter != end) {
		char *name = ((CRegion *)(*iter))->name;
		if (strncasecmp(name, tag, tagLen) == 0)
			return iter;
		iter++;
	}
	return end;
}

/*
 * 0x004A5762 - RegionManager::inJusticeRegion
 *
 * Returns 1 if the location lies in a "justice" or "city" region.
 */
int
RegionManager_inJusticeRegion(int16_t x, int16_t y, int16_t z)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *result;
	CLocation loc;

	loc.x = (uint16_t)x;
	loc.y = (uint16_t)y;
	loc.z = z;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, &loc);

	result = RegionGrid_SearchTag(&localVec, "justice");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	result = RegionGrid_SearchTag(&localVec, "city");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	CVector_Destructor(&localVec);
	return 0;
}

/*
 * 0x004A5848 - RegionManager::isGuardedRegion
 *
 * Returns 1 if the location lies in a "no_guard" region.
 */
int
RegionManager_isGuardedRegion(int16_t x, int16_t y, int16_t z)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *result;
	CLocation loc;

	loc.x = (uint16_t)x;
	loc.y = (uint16_t)y;
	loc.z = z;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, &loc);

	result = RegionGrid_SearchTag(&localVec, "no_guard");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	CVector_Destructor(&localVec);
	return 0;
}

/*
 * 0x004A58F4 - RegionManager::IsInRegion
 *
 * Returns 1 if the location lies in a region whose name matches the
 * prefix (case-insensitive).
 */
int
RegionManager_IsInRegion(CLocation *loc, const char *prefix)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *result;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, loc);

	result = RegionGrid_SearchTag(&localVec, prefix);
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	CVector_Destructor(&localVec);
	return 0;
}

/*
 * 0x004A599F - RegionManager::isInCityRegion
 *
 * Returns 1 if the location lies in a "city" region.
 */
int
RegionManager_isInCityRegion(int16_t x, int16_t y, int16_t z)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *result;
	CLocation loc;

	loc.x = (uint16_t)x;
	loc.y = (uint16_t)y;
	loc.z = z;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, &loc);

	result = RegionGrid_SearchTag(&localVec, "city");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	CVector_Destructor(&localVec);
	return 0;
}

/*
 * 0x004A5A4B - RegionManager::FindLVNRegion
 *
 * Writes the city/dungeon name covering the start..end AABB into
 * outBuf, or "Unknown?" if none matches. Strips the "city_" /
 * "dungn_" prefix from the region name.
 */
void
RegionManager_FindLVNRegion(CResManager *rm, int startX, int startY, int endX, int endY, char *outBuf, int maxLen)
{
	CSearchCtx ctx;
	CSearchCtx temp;
	CRegion *region;
	int typeByte;

	strncpy(outBuf, "", maxLen);

	CSearchCtx_Constructor(&ctx);
	CSearchCtx_Add(&ctx, CResManager_BeginIterWrapper(rm, &temp));

	while (CSearchCtx_Find(&ctx)) {
		region = *(CRegion **)CResManager_GetResultCtx(rm, &ctx);

		typeByte = (signed char)region->name[0];
		if (typeByte != 'C' && typeByte != 'D' && typeByte != 'c' && typeByte != 'd')
			goto next;

		if (!ValidateRegionBounds(startX, startY, endX, endY, (int)region->x, (int)region->y, (int)region->x + (int)region->width, (int)region->y + (int)region->height))
			goto next;

		if (strncasecmp(region->name, "city_", 5) == 0) {
			strncpy(outBuf, region->name + 5, maxLen);
			return;
		}

		if (strncasecmp(region->name, "dungn_", 6) == 0) {
			strncpy(outBuf, region->name + 6, maxLen);
			return;
		}

next:
		CSearchCtx_Add(&ctx, CResManager_NextIterWrapper(rm, &temp, &ctx));
	}

	strncpy(outBuf, "Unknown?", maxLen);
}

/*
 * 0x004A5BBA - RegionManager::isHousingOkay
 *
 * Returns whether housing is allowed at the location. Deny tags:
 * "dungeon", "dungn", "city", "town", "housing_no", "justice".
 * Allow tag: "housing_yes". If flag==0, deny wins; otherwise allow
 * wins.
 */
int
RegionManager_isHousingOkay(int16_t x, int16_t y, int16_t z, int flag)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *result;
	int deny, allow;
	CLocation loc;

	loc.x = (uint16_t)x;
	loc.y = (uint16_t)y;
	loc.z = z;

	deny = 0;
	allow = 0;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, &loc);

	result = RegionGrid_SearchTag(&localVec, "dungeon");
	if (result != (uintptr_t *)localVec.end)
		deny = 1;

	result = RegionGrid_SearchTag(&localVec, "dungn");
	if (result != (uintptr_t *)localVec.end)
		deny = 1;

	result = RegionGrid_SearchTag(&localVec, "city");
	if (result != (uintptr_t *)localVec.end)
		deny = 1;

	result = RegionGrid_SearchTag(&localVec, "town");
	if (result != (uintptr_t *)localVec.end)
		deny = 1;

	result = RegionGrid_SearchTag(&localVec, "housing_no");
	if (result != (uintptr_t *)localVec.end)
		deny = 1;

	result = RegionGrid_SearchTag(&localVec, "housing_yes");
	if (result != (uintptr_t *)localVec.end)
		allow = 1;

	result = RegionGrid_SearchTag(&localVec, "justice");
	if (result != (uintptr_t *)localVec.end)
		deny = 1;

	CVector_Destructor(&localVec);

	if (flag == 0) {
		if (deny)
			return 0;
		if (allow)
			return 1;
		return 0;
	} else {
		if (allow)
			return 1;
		if (deny)
			return 0;
		return 1;
	}
}

/*
 * 0x004A5DF4 - RegionManager::areSpellsOkay
 *
 * Returns 0 if the location lies in a "nospell" region, 1 otherwise.
 */
int
RegionManager_areSpellsOkay(int16_t x, int16_t y, int16_t z)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *result;
	CLocation loc;

	loc.x = (uint16_t)x;
	loc.y = (uint16_t)y;
	loc.z = z;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, &loc);

	result = RegionGrid_SearchTag(&localVec, "nospell");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 0;
	}

	CVector_Destructor(&localVec);
	return 1;
}

/*
 * 0x004A5EA0 - RegionManager::IsInSpecialArea
 *
 * Returns 1 if the location lies in an "inn", "safelo", or "tavern"
 * region.
 */
int
RegionManager_IsInSpecialArea(CLocation *loc)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *result;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CRegionGrid_Populate(g_RegionGrid, &localVec, loc);

	result = RegionGrid_SearchTag(&localVec, "inn");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	result = RegionGrid_SearchTag(&localVec, "safelo");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	result = RegionGrid_SearchTag(&localVec, "tavern");
	if (result != (uintptr_t *)localVec.end) {
		CVector_Destructor(&localVec);
		return 1;
	}

	CVector_Destructor(&localVec);
	return 0;
}

/*
 * 0x004A5FC3 - RegionManager::AllocGrid
 *
 * Allocates the global region grid covering the world (0,0)..(0x17FF,
 * 0xFFF) at a 64-tile cell size (shift 6).
 */
void
RegionManager_AllocGrid(void)
{
	CRegionGrid *grid;

	grid = (CRegionGrid *)OperatorNew(sizeof(CRegionGrid));
	if (grid != NULL)
		CRegionGrid_Constructor(grid, 0, 0, 0x17FF, 0xFFF, 6);
	else
		grid = NULL;

	g_RegionGrid = grid;
}

/*
 * 0x004A6046 - RegionManager::RebuildGrid
 *
 * Clears the region grid and re-inserts every region from
 * g_RegionByNameRM at its current bounding box.
 */
static void
RegionManager_RebuildGrid(void)
{
	CSearchCtx iterCtx;
	CSearchCtx nextCtx;
	CRegion *region;

	CRegionGrid_ClearAll(g_RegionGrid);

	CSearchCtx_Constructor(&iterCtx);
	CResManager_BeginIterWrapper(&g_RegionByNameRM, &nextCtx);
	CSearchCtx_Add(&iterCtx, &nextCtx);

	while (CSearchCtx_Find(&iterCtx)) {
		region = *(CRegion **)CResManager_GetResultCtx(&g_RegionByNameRM, &iterCtx);

		CRegionGrid_InsertRegion(g_RegionGrid, region, (int)region->x, (int)region->y, (int)region->x + (int)region->width, (int)region->y + (int)region->height);

		CResManager_NextIterWrapper(&g_RegionByNameRM, &nextCtx, &iterCtx);
		CSearchCtx_Add(&iterCtx, &nextCtx);
	}
}

/*
 * 0x004A6110 - CRegionGrid scalar deleting destructor
 *
 * Scalar deleting destructor: tears down the bucket array and frees the
 * grid when flag & 1.
 */
static void *
CRegionGrid_ScalarDelete(CRegionGrid *this, int flag)
{
	CRegionGrid_VectorDtor(this);

	if (flag & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x004A6140 - CRegion scalar deleting destructor
 *
 * Scalar deleting destructor: runs CRegion_Destructor and frees the object
 * when flag & 1.
 */
CRegion *
CRegion_ScalarDelete(CRegion *this, int flag)
{
	CRegion_Destructor(this);
	if (flag & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x004A6170 - CRegionGrid::CRegionGrid
 *
 * Allocates a flat width*height CVector bucket array covering the
 * world bounds, using 1 << shift as cell size.
 */
CRegionGrid *
CRegionGrid_Constructor(CRegionGrid *this, int xMin, int yMin, int xMax, int yMax, int shift)
{
	int count;
	char *alloc;
	CVector *bucketsPtr;
	int i;
	char typeFlag;

	this->shift = shift;
	this->xOffset = xMin >> shift;
	this->xMaxBlock = (xMax + (1 << shift) - 1) >> shift;
	this->yOffset = yMin >> shift;
	this->yMaxBlock = (yMax + (1 << shift) - 1) >> shift;
	this->width = this->xMaxBlock - this->xOffset + 1;
	this->height = this->yMaxBlock - this->yOffset + 1;

	count = this->width * this->height;
	// Custom: 64-bit - sizeof(uintptr_t) header for alignment
	alloc = (char *)OperatorNew((size_t)(count * sizeof(CVector) + sizeof(uintptr_t)));
	if (alloc != NULL) {
		*(uint32_t *)alloc = count;
		for (i = 0; i < count; i++)
			CVector_Constructor((CVector *)(alloc + sizeof(uintptr_t) + i * sizeof(CVector)), &typeFlag);
		bucketsPtr = (CVector *)(alloc + sizeof(uintptr_t));
	} else {
		bucketsPtr = NULL;
	}
	this->buckets = bucketsPtr;
	return this;
}

/*
 * 0x004A62B0 - CRegionGrid vector array destructor
 *
 * Destroys each CVector bucket in reverse order and frees the backing
 * allocation (size prefix in the pointer header).
 */
static void
CRegionGrid_VectorDtor(CRegionGrid *this)
{
	if (this->buckets == NULL)
		return;

	CVector_VecDestructor_Region(this->buckets, 3);
}

/*
 * 0x004A62F0 - CRegionGrid::ClearAll
 *
 * Clears all buckets in the grid.
 */
void
CRegionGrid_ClearAll(CRegionGrid *grid)
{
	int total;
	int i;
	CVector *cell;

	total = grid->width * grid->height;
	for (i = 0; i < total; i++) {
		cell = &grid->buckets[i];
		CVector_Erase(cell, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)cell), cell->end);
	}
}

/*
 * 0x004A6370 - CRegionGrid::Populate
 *
 * Pushes every region covering loc onto outList. Resolves the
 * bucket for the location, then filters by CRegion_isCoordInside.
 */
void
CRegionGrid_Populate(CRegionGrid *grid, CVector *outList, CLocation *loc)
{
	int idx;
	uintptr_t *iter;

	if (!CRegionGrid_IsInBounds(grid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return;

	idx = CEntityMap_GetBlockIdx((CEntityMap *)grid, (int)(int16_t)loc->x, (int)(int16_t)loc->y);

	iter = (uintptr_t *)grid->buckets[idx].begin;
	while (iter != (uintptr_t *)grid->buckets[idx].end) {
		CRegion *region = (CRegion *)*iter;
		if (CRegion_isCoordInside(region, loc))
			CVector_PushBack(outList, (uintptr_t)region);
		iter++;
	}
}

/*
 * 0x004A6510 - CRegionGrid::IsInBounds
 *
 * Returns 1 if (x,y) maps to a valid grid cell.
 */
static int
CRegionGrid_IsInBounds(CRegionGrid *this, int x, int y)
{
	int col, row;

	col = (x >> this->shift) - this->xOffset;
	row = (y >> this->shift) - this->yOffset;

	if (col < 0)
		return 0;
	if (col >= this->width)
		return 0;
	if (row < 0)
		return 0;
	if (row >= this->height)
		return 0;
	return 1;
}

/*
 * 0x004A6EA0 - CRegionGrid::InsertRegion
 *
 * Pushes region into every grid bucket overlapping its bounding
 * box, clamped to grid extents.
 */
void
CRegionGrid_InsertRegion(CRegionGrid *this, CRegion *region, int x, int y, int xEnd, int yEnd)
{
	int xMin, xMax, yMin, yMax;
	int idx, cols;
	int row, col;

	xMin = (x >> this->shift) - this->xOffset;
	xMax = (xEnd >> this->shift) - this->xOffset;
	yMin = (y >> this->shift) - this->yOffset;
	yMax = (yEnd >> this->shift) - this->yOffset;

	if (xMin < 0)
		xMin = 0;
	if (xMax >= this->width)
		xMax = this->width - 1;
	if (yMin < 0)
		yMin = 0;
	if (yMax >= this->height)
		yMax = this->height - 1;

	idx = yMin * this->width + xMin;
	cols = xMax - xMin + 1;

	for (row = yMin; row <= yMax; row++) {
		for (col = xMin; col <= xMax; col++) {
			CVector_PushBack(&this->buckets[idx], (uintptr_t)region);
			idx++;
		}
		idx += this->width - cols;
	}
}

/*
 * 0x004E8110 - operator delete
 *
 * MSVC operator delete: frees ptr via the CRT free.
 */
__attribute__((unused)) void
OperatorDelete(void *ptr)
{
	free(ptr);
}

/*
 * 0x004E84C0 - operator new
 *
 * MSVC operator new: allocates size bytes via the CRT malloc.
 */
__attribute__((unused)) void *
OperatorNew(uint32_t size)
{
	return malloc(size);
}

/*
 * Binary client CRegion: 0x65 bytes, no prefix field.
 * Used by readRegionsIntoMemory below. Packed because
 * lightLevel (int16_t) sits at odd offset +0x63.
 */
__extension__ typedef struct __attribute__((packed)) CRegion_Client {
	char name[0x28];         // +0x00
	int16_t x;               // +0x28
	int16_t y;               // +0x2A
	int16_t width;           // +0x2C
	int16_t height;          // +0x2E
	int16_t zMin;            // +0x30
	int16_t zMax;            // +0x32
	char name2[0x28];        // +0x34
	int16_t weatherSeason;   // +0x5C
	int16_t weatherDay;      // +0x5E
	int16_t weatherNight;    // +0x60
	uint8_t type;            // +0x62
	int16_t lightLevel;      // +0x63
} CRegion_Client;            // sizeof = 0x65

/*
 * File record for FACE format (version == 0xFACE).
 * Also used by OLD format (< 0xFACE) as assembly target.
 * Packed because uint32_t prefix sits at non-4-aligned offset +0x36.
 */
__extension__ typedef struct __attribute__((packed)) CRegionFileRecord_FACE {
	char name[0x28];         // +0x00
	int16_t slot;            // +0x28
	int16_t x;               // +0x2A
	int16_t y;               // +0x2C
	int16_t width;           // +0x2E
	int16_t height;          // +0x30
	int16_t zMin;            // +0x32
	int16_t zMax;            // +0x34
	uint32_t prefix;         // +0x36
	char name2[0x28];        // +0x3A
	int16_t weatherSeason;   // +0x62
	int16_t weatherDay;      // +0x64
	uint8_t type;            // +0x66
} CRegionFileRecord_FACE;    // sizeof = 0x67

/*
 * File record for newest format (version > 0xFACE).
 * Extends FACE with weatherNight and lightLevel.
 * Packed because int16_t lightLevel sits at odd offset +0x69.
 */
__extension__ typedef struct __attribute__((packed)) CRegionFileRecord_NEW {
	char name[0x28];         // +0x00
	int16_t slot;            // +0x28
	int16_t x;               // +0x2A
	int16_t y;               // +0x2C
	int16_t width;           // +0x2E
	int16_t height;          // +0x30
	int16_t zMin;            // +0x32
	int16_t zMax;            // +0x34
	uint32_t prefix;         // +0x36
	char name2[0x28];        // +0x3A
	int16_t weatherSeason;   // +0x62
	int16_t weatherDay;      // +0x64
	int16_t weatherNight;    // +0x66
	uint8_t type;            // +0x68
	int16_t lightLevel;      // +0x69
} CRegionFileRecord_NEW;     // sizeof = 0x6B

/*
 * 0x005568CC - RegionManager::readRegionsIntoMemory
 *
 * Client binary .mul parser. Loads the regions array from
 * "regions.mul" in one of three format versions (< 0xFACE, == 0xFACE,
 * > 0xFACE). Unused on the server, which loads regions via
 * RegionManager_LoadRegions (text format).
 */
static __attribute__((unused)) void
RegionManager_readRegionsIntoMemory(CRegion_Client **regions)
{
	FILE *fp;
	uint16_t version;
	int i;

	fp = fopen("regions.mul", "rb");
	if (fp == NULL) {
		printf("There is no regions.mul file.\n");
		return;
	}

	version = 0;
	if (fread(&version, 2, 1, fp) != 1) {
		fclose(fp);
		return;
	}

	if ((version & 0xFFFF) < 0xFACE) {
		// OLD FORMAT
		fseek(fp, 0, 0);

		CRegionFileRecord_FACE temp;

		memset(temp.name2, 0, 0x28);
		temp.weatherDay = 0;
		temp.weatherSeason = temp.weatherDay;
		temp.type = 0;

		// Separate read buffers (binary uses distinct stack vars)
		uint8_t rd_name[0x28];
		int16_t rd_slot;
		int16_t rd_x, rd_y, rd_w, rd_h, rd_zMin, rd_zMax;
		uint32_t rd_prefix;

		for (i = 0; i < 0x2000; i++) {
			// 0x005569F3
			if (fread(rd_name, 0x28, 1, fp) != 1)
				break;
			// 0x00556A10
			if (fread(&rd_slot, 2, 1, fp) != 1)
				break;
			// 0x00556A2D
			if (fread(&rd_x, 2, 1, fp) != 1)
				break;
			// 0x00556A4A
			if (fread(&rd_y, 2, 1, fp) != 1)
				break;
			// 0x00556A63
			if (fread(&rd_w, 2, 1, fp) != 1)
				break;
			// 0x00556A7C
			if (fread(&rd_h, 2, 1, fp) != 1)
				break;
			// 0x00556A95
			if (fread(&rd_zMin, 2, 1, fp) != 1)
				break;
			// 0x00556AAE
			if (fread(&rd_zMax, 2, 1, fp) != 1)
				break;
			// 0x00556AC7
			if (fread(&rd_prefix, 4, 1, fp) != 1)
				break;

			memcpy(temp.name, rd_name, 0x28);
			temp.x = rd_x;
			temp.y = rd_y;
			temp.width = rd_w;
			temp.height = rd_h;
			temp.zMin = rd_zMin;
			temp.zMax = rd_zMax;

			int16_t slot = rd_slot;

			if (regions[slot] != NULL)
				OperatorDelete(regions[slot]);

			CRegion_Client *region;
			CRegion_Client *raw = (CRegion_Client *)OperatorNew(sizeof(CRegion_Client));
			if (raw != NULL) {
				memset(raw->name, 0, 0x28);
				raw->x = 0;
				raw->y = 0;
				raw->width = 0;
				raw->height = 0;
				raw->zMin = 0;
				raw->zMax = 0;
				memset(raw->name2, 0, 0x28);
				raw->weatherNight = 0;
				raw->weatherDay = 0;
				raw->weatherSeason = 0;
				raw->type = 0;
				raw->lightLevel = 0;
				region = raw;
			} else {
				region = NULL;
			}

			regions[slot] = region;

			memcpy(region->name, temp.name, 0x28);
			region->x = temp.x;
			region->y = temp.y;
			region->width = temp.width;
			region->height = temp.height;
			region->zMin = temp.zMin;
			region->zMax = temp.zMax;
			memcpy(region->name2, temp.name2, 0x28);
			region->weatherSeason = temp.weatherSeason;
			region->weatherDay = temp.weatherDay;
			region->type = temp.type;
		}

	} else if ((version & 0xFFFF) == 0xFACE) {
		// FACE FORMAT - reads directly into temp struct
		CRegionFileRecord_FACE temp;

		for (i = 0; i < 0x2000; i++) {
			// 0x00556C5B
			if (fread(temp.name, 0x28, 1, fp) != 1)
				break;
			// 0x00556C7B
			if (fread(&temp.slot, 2, 1, fp) != 1)
				break;
			// 0x00556C9B
			if (fread(&temp.x, 2, 1, fp) != 1)
				break;
			// 0x00556CBB
			if (fread(&temp.y, 2, 1, fp) != 1)
				break;
			// 0x00556CDB
			if (fread(&temp.width, 2, 1, fp) != 1)
				break;
			// 0x00556CFB
			if (fread(&temp.height, 2, 1, fp) != 1)
				break;
			// 0x00556D1B
			if (fread(&temp.zMin, 2, 1, fp) != 1)
				break;
			// 0x00556D3B
			if (fread(&temp.zMax, 2, 1, fp) != 1)
				break;
			// 0x00556D5B
			if (fread(&temp.prefix, 4, 1, fp) != 1)
				break;
			// 0x00556D77
			if (fread(temp.name2, 0x28, 1, fp) != 1)
				break;
			// 0x00556D93
			if (fread(&temp.weatherSeason, 2, 1, fp) != 1)
				break;
			// 0x00556DAF
			if (fread(&temp.weatherDay, 2, 1, fp) != 1)
				break;
			// 0x00556DCB
			if (fread(&temp.type, 1, 1, fp) != 1)
				break;

			// 0x00556DEC
			int16_t slot = temp.slot;

			// 0x00556DF9
			if (regions[slot] != NULL)
				OperatorDelete(regions[slot]);

			// 0x00556E24
			CRegion_Client *region;
			CRegion_Client *raw = (CRegion_Client *)OperatorNew(sizeof(CRegion_Client));
			if (raw != NULL) {
				// CRegion_Constructor_Client (0x005562B4 -> 0x005565B6)
				memset(raw->name, 0, 0x28);
				raw->x = 0;
				raw->y = 0;
				raw->width = 0;
				raw->height = 0;
				raw->zMin = 0;
				raw->zMax = 0;
				memset(raw->name2, 0, 0x28);
				raw->weatherNight = 0;
				raw->weatherDay = 0;
				raw->weatherSeason = 0;
				raw->type = 0;
				raw->lightLevel = 0;
				region = raw;
			} else {
				region = NULL;
			}

			// 0x00556E6E
			regions[slot] = region;

			memcpy(region->name, temp.name, 0x28);
			region->x = temp.x;
			region->y = temp.y;
			region->width = temp.width;
			region->height = temp.height;
			region->zMin = temp.zMin;
			region->zMax = temp.zMax;
			memcpy(region->name2, temp.name2, 0x28);
			region->weatherSeason = temp.weatherSeason;
			region->weatherDay = temp.weatherDay;
			region->type = temp.type;
			// no-op CResManager insert
		}

	} else {
		// NEWEST FORMAT (version > 0xFACE)
		CRegionFileRecord_NEW temp;

		for (i = 0; i < 0x2000; i++) {
			// 0x00556EF5
			if (fread(temp.name, 0x28, 1, fp) != 1)
				break;
			// 0x00556F15
			if (fread(&temp.slot, 2, 1, fp) != 1)
				break;
			// 0x00556F35
			if (fread(&temp.x, 2, 1, fp) != 1)
				break;
			// 0x00556F55
			if (fread(&temp.y, 2, 1, fp) != 1)
				break;
			// 0x00556F75
			if (fread(&temp.width, 2, 1, fp) != 1)
				break;
			// 0x00556F95
			if (fread(&temp.height, 2, 1, fp) != 1)
				break;
			// 0x00556FB5
			if (fread(&temp.zMin, 2, 1, fp) != 1)
				break;
			// 0x00556FD5
			if (fread(&temp.zMax, 2, 1, fp) != 1)
				break;
			// 0x00556FF5
			if (fread(&temp.prefix, 4, 1, fp) != 1)
				break;
			// 0x00557015
			if (fread(temp.name2, 0x28, 1, fp) != 1)
				break;
			// 0x00557035
			if (fread(&temp.weatherSeason, 2, 1, fp) != 1)
				break;
			// 0x00557051
			if (fread(&temp.weatherDay, 2, 1, fp) != 1)
				break;
			// 0x0055706D
			if (fread(&temp.weatherNight, 2, 1, fp) != 1)
				break;
			// 0x00557089
			if (fread(&temp.type, 1, 1, fp) != 1)
				break;
			// 0x005570A5
			if (fread(&temp.lightLevel, 2, 1, fp) != 1)
				break;

			// 0x005570C6
			int16_t slot = temp.slot;

			// 0x005570D3
			if (regions[slot] != NULL)
				OperatorDelete(regions[slot]);

			// 0x005570FE
			CRegion_Client *region;
			CRegion_Client *raw = (CRegion_Client *)OperatorNew(sizeof(CRegion_Client));
			if (raw != NULL) {
				// CRegion_Constructor_Client (0x005562B4 -> 0x005565B6)
				memset(raw->name, 0, 0x28);
				raw->x = 0;
				raw->y = 0;
				raw->width = 0;
				raw->height = 0;
				raw->zMin = 0;
				raw->zMax = 0;
				memset(raw->name2, 0, 0x28);
				raw->weatherNight = 0;
				raw->weatherDay = 0;
				raw->weatherSeason = 0;
				raw->type = 0;
				raw->lightLevel = 0;
				region = raw;
			} else {
				region = NULL;
			}

			// 0x00557148
			regions[slot] = region;

			memcpy(region->name, temp.name, 0x28);
			region->x = temp.x;
			region->y = temp.y;
			region->width = temp.width;
			region->height = temp.height;
			region->zMin = temp.zMin;
			region->zMax = temp.zMax;
			memcpy(region->name2, temp.name2, 0x28);
			region->weatherSeason = temp.weatherSeason;
			region->weatherDay = temp.weatherDay;
			region->weatherNight = temp.weatherNight;
			region->type = temp.type;
			region->lightLevel = temp.lightLevel;
			// no-op CResManager insert
		}
	}

	// 0x005571A0
	fclose(fp);
}
