/*
 * Dynamic world load pipeline.
 *
 * Top-level driver that walks dynidx0.mul, reconstructs every entity
 * block, resolves parent / container relationships, and re-inserts the
 * results into the world spatial index.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "container.h"
#include "dynamic.h"
#include "egg.h"
#include "filemanager.h"
#include "io.h"
#include "load.h"
#include "magicfactory.h"
#include "feature.h"
#include "main.h"
#include "multi.h"
#include "resbank.h"
#include "skill.h"
#include "template.h"
#include "time.h"
#include "utils.h"
#include "wombat_compile.h"
#include "world.h"

#define MAX_RESOURCE_TYPES 102

/*
 * 0x004DE4D7 - LoadAll_ArtDim
 *
 * Thiscall shim that delegates to LoadGumpDimTable.
 */
void
LoadAll_ArtDim(void)
{
	LoadGumpDimTable();
}
/*
 * 0x004E1120 - Terrain_LoadMap
 *
 * Reads map0.mul into g_SpatialGrid.cells as 8x8 tileID/Z cells per block.
 *
 * MODIFIED: sets g_TerrainDataLoaded at end (not in the binary).
 */
void
Terrain_LoadMap(void)
{
	uint16_t tileID;
	FILE *fp;
	int blockIdx, row, col;
	uint32_t blockHeader;

	CBlockManager_Lock(&g_SpatialGrid);

	fp = fopen_ServerSide(GLOBAL_file_map0_mul, "rb");
	if (g_shutdown) {
		CBlockManager_Unlock(&g_SpatialGrid);
		return;
	}

	for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
		CBlock *blk = &g_SpatialGrid.cells[blockIdx];

		blk->flags111 = 0;

		fread_ServerSide(&blockHeader, 4, 1, fp);
		SwapEndian(&blockHeader);

		for (row = 0; row < 8; row++) {
			for (col = 0; col < 8; col++) {
				MapCell *cell;

				cell = (MapCell *)&blk->pad00[row * 32 + col * 4];

				fread_ServerSide(&tileID, 2, 1, fp);
				SwapEndian(&tileID);
				cell->tileID = tileID;

				fread_ServerSide(&cell->z, 1, 1, fp);
			}
		}
	}

	fclose_ServerSide(fp);

	CBlockManager_Unlock(&g_SpatialGrid);

	g_TerrainDataLoaded = 1;
}

/*
 * 0x004E1287 - LoadAll_Statics
 *
 * Wraps Terrain_LoadStatics with Static_Lock/Static_Unlock calls.
 */
void
LoadAll_Statics(void)
{
	Static_Lock();
	Terrain_LoadStatics();
	Static_Unlock();
}

/*
 * 0x004E129B - LoadDynamicPhase
 *
 * Runs LoadDynamic0 with script-creation deferral and pending-load
 * bookkeeping around it.
 */
void
LoadDynamicPhase(void)
{
	ScriptCreation_BeginDefer();
	Dynamic_SetPendingLoad();
	LoadDynamic0();
	ScriptCreation_FlushDeferred();
	Dynamic_ClearPendingAndFireEvents();
}

/*
 * 0x004E12B9 - LoadAll_AnimData
 *
 * Reads animdata.mul into g_AnimData: 16384 records of 64 frame bytes
 * followed by four trailing bytes, prefixed by a 4-byte group header
 * every eight records.
 */
void
LoadAll_AnimData(void)
{
	FILE *fp;
	int i, j;
	uint32_t groupHeader;

	CDataManager_LockAnimData();

	fp = FileManager_OpenByType(0x15, NULL, "rb");
	if (g_shutdown) {
		CDataManager_UnlockAnimData();
		return;
	}

	memset(g_AnimData, 0, 0x110000);

	for (i = 0; i < 0x4000; i++) {
		if ((i & 7) == 0) {
			fread_ServerSide(&groupHeader, 4, 1, fp);
			SwapEndian(&groupHeader);
		}

		for (j = 0; j < 0x40; j++) {
			fread_ServerSide(&g_AnimData[i].frameData[j], 1, 1, fp);
		}

		fread_ServerSide(&g_AnimData[i].unk40, 1, 1, fp);
		fread_ServerSide(&g_AnimData[i].frameCount, 1, 1, fp);
		fread_ServerSide(&g_AnimData[i].frameInterval, 1, 1, fp);
		fread_ServerSide(&g_AnimData[i].startInterval, 1, 1, fp);
	}

	fclose_ServerSide(fp);
	CDataManager_UnlockAnimData();
}

/*
 * 0x004E1432 - LoadAll_TemplateIdx
 *
 * Reads hues.mul into g_HueData: 1024 records of 32 color uint16s plus
 * start/end colors and a 20-byte name, with a 4-byte group header every
 * eight records.
 */
void
LoadAll_TemplateIdx(void)
{
	FILE *fp;
	int i, j;
	uint32_t groupHeader;
	uint16_t val;

	CDataManager_LockHueData();

	fp = FileManager_OpenByType(0x18, NULL, "rb");
	if (g_shutdown) {
		CDataManager_UnlockHueData();
		return;
	}

	for (i = 0; i < 0x400; i++) {
		if ((i & 7) == 0) {
			fread_ServerSide(&groupHeader, 4, 1, fp);
			SwapEndian(&groupHeader);
		}

		for (j = 0; j < 0x20; j++) {
			fread_ServerSide(&val, 2, 1, fp);
			SwapEndian(&val);
			g_HueData[i].colors[j] = val;
		}

		fread_ServerSide(&val, 2, 1, fp);
		SwapEndian(&val);
		g_HueData[i].startColor = val;

		fread_ServerSide(&val, 2, 1, fp);
		SwapEndian(&val);
		g_HueData[i].endColor = val;

		fread_ServerSide(g_HueData[i].name, 0x14, 1, fp);
	}

	fclose_ServerSide(fp);
	CDataManager_UnlockHueData();
}

/*
 * 0x004E15B6 - LoadAll_TiledataVersion
 *
 * Reads the verdata.mul version word into g_TimeManager.hour (the binary
 * repurposes that storage; the time manager overwrites it later).
 */
void
LoadAll_TiledataVersion(void)
{
	FILE *f;
	uint32_t version;

	f = FileManager_OpenByType(0x3E, NULL, "rb");
	if (f == NULL)
		return;
	fread_ServerSide(&version, 4, 1, f);
	g_TimeManager.hour = version;
	SwapEndian((uint32_t *)&g_TimeManager.hour);
	fclose_ServerSide(f);
}

/*
 * 0x004E1613 - Terrain_LoadTileData
 *
 * Reads tiledata.mul into g_LandTileData and g_ItemTileData. Each section
 * holds 16384 records grouped in runs of 32 with a 4-byte group header.
 *
 * MODIFIED: FileManager_OpenByType's second arg is the binary's global
 * filename scratch buffer at 0x700624; the POSIX port formats the path
 * internally and accepts NULL. The g_shutdown early return preserves the
 * binary behavior (file handle left open, tile-data lock not released)
 * because the process is exiting.
 */
void
Terrain_LoadTileData(void)
{
	FILE *fp;
	int i;
	uint32_t groupHeader;

	CDataManager_LockTileData();
	fp = FileManager_OpenByType(0x1F, NULL, "rb");
	if (g_shutdown)
		return;

	for (i = 0; i < LAND_TILE_COUNT; i++) {
		if ((i & 0x1F) == 0) {
			fread_ServerSide(&groupHeader, 4, 1, fp);
			SwapEndian(&groupHeader);
		}

		fread_ServerSide(&g_LandTileData[i].flags, 4, 1, fp);
		SwapEndian(&g_LandTileData[i].flags);

		fread_ServerSide(&g_LandTileData[i].textureID, 2, 1, fp);
		SwapEndian(&g_LandTileData[i].textureID);

		fread_ServerSide(g_LandTileData[i].name, 0x14, 1, fp);
	}

	for (i = 0; i < ITEM_TILE_COUNT; i++) {
		if ((i & 0x1F) == 0) {
			fread_ServerSide(&groupHeader, 4, 1, fp);
			SwapEndian(&groupHeader);
		}

		fread_ServerSide(&g_ItemTileData[i].flags, 4, 1, fp);
		SwapEndian(&g_ItemTileData[i].flags);

		fread_ServerSide(&g_ItemTileData[i].weight, 1, 1, fp);
		fread_ServerSide(&g_ItemTileData[i].layer, 1, 1, fp);

		fread_ServerSide(&g_ItemTileData[i].miscData, 4, 1, fp);
		SwapEndian(&g_ItemTileData[i].miscData);

		fread_ServerSide(&g_ItemTileData[i].value1, 2, 1, fp);
		SwapEndian(&g_ItemTileData[i].value1);

		fread_ServerSide(&g_ItemTileData[i].value2, 2, 1, fp);
		SwapEndian(&g_ItemTileData[i].value2);

		fread_ServerSide(&g_ItemTileData[i].height, 2, 1, fp);
		SwapEndian(&g_ItemTileData[i].height);

		fread_ServerSide(&g_ItemTileData[i].quantity, 1, 1, fp);

		fread_ServerSide(g_ItemTileData[i].name, 0x14, 1, fp);
	}

	fclose_ServerSide(fp);
	CDataManager_UnlockTileData();
}

/*
 * 0x004E190A - LoadResTypes
 *
 * Loads resource type entries from restypes.mul via FileManager_OpenByType.
 */
void
LoadResTypes(void)
{
	FILE *f;
	uint32_t count;
	uint32_t i;

	f = FileManager_OpenByType(0x2C, NULL, "rb");
	if (f == NULL)
		return;

	fread_ServerSide(&count, 4, 1, f);
	SwapEndian(&count);

	for (i = 0; i < count; i++) {
		uint32_t recId;
		uint32_t tmpVal;
		char nameBuf[128];
		CResourceType *rt;

		rt = malloc(sizeof(CResourceType));
		if (rt != NULL)
			rt = CResourceType_Constructor(rt);

		fread_ServerSide(&recId, 4, 1, f);
		SwapEndian(&recId);
		rt->typeId = recId;

		if (recId > (uint32_t)g_ResourceTypeCount)
			g_ResourceTypeCount = recId;

		fread_ServerSide(nameBuf, 0x80, 1, f);
		CResourceType_SetInternalName(rt, nameBuf);

		fread_ServerSide(nameBuf, 0x80, 1, f);
		CResourceType_SetFoodName(rt, nameBuf);

		fread_ServerSide(nameBuf, 0x80, 1, f);
		CResourceType_SetShelterName(rt, nameBuf);

		fread_ServerSide(nameBuf, 0x80, 1, f);
		CResourceType_SetDesireName(rt, nameBuf);

		fread_ServerSide(nameBuf, 0x80, 1, f);
		CResourceType_SetProductionName(rt, nameBuf);

		fread_ServerSide(&tmpVal, 4, 1, f);
		SwapEndian(&tmpVal);
		CResourceType_SetField288(rt, tmpVal);

		fread_ServerSide(&tmpVal, 4, 1, f);
		SwapEndian(&tmpVal);
		CResourceType_SetField28C(rt, tmpVal);

		CResourceTypeManager_RegisterType(rt);
	}

	fclose_ServerSide(f);
}

/*
 * 0x004E1BFA - LoadGumpDimTable
 *
 * Loads the gump container dimension cache into g_GumpDimTable, building
 * it from artidx.mul + art.mul when the artdim.dat cache file is missing.
 */
void
LoadGumpDimTable(void)
{
	FILE *f;
	FILE *fidx;
	FILE *fart;
	int32_t offset;
	int16_t width;
	int16_t height;
	int i;

	f = fopen_ServerSide("../.rundir/artdim.dat", "rb");
	if (f != NULL) {
		fread_ServerSide(g_GumpDimTable, 4, TILEDATA_MAX_ITEMS, f);
		fclose_ServerSide(f);
		return;
	}

	fidx = FileManager_OpenByType(0x17, NULL, "rb");
	fart = FileManager_OpenByType(0x16, NULL, "rb");

	offset = -1;
	for (i = 0; i < TILEDATA_MAX_ITEMS; i++) {
		fseek_ServerSide(fidx, (i + 0x4000) * 12, 0);
		fread_ServerSide(&offset, 4, 1, fidx);
		SwapEndian(&offset);
		if (offset == -1) {
			width = 0;
			height = 0;
		} else {
			fseek_ServerSide(fart, offset + 4, 0);
			fread_ServerSide(&width, 2, 1, fart);
			SwapEndian(&width);
			fread_ServerSide(&height, 2, 1, fart);
			SwapEndian(&height);
		}
		g_GumpDimTable[i].width = width;
		g_GumpDimTable[i].height = height;
	}
	fclose_ServerSide(fart);
	fclose_ServerSide(fidx);

	f = fopen_ServerSide("../.rundir/artdim.dat", "wb");
	fwrite_ServerSide(g_GumpDimTable, 4, TILEDATA_MAX_ITEMS, f);
	fclose_ServerSide(f);
}

/*
 * 0x004E1DB3 - LoadItemRes
 *
 * Reads itemres.mul: 0x4000 template slots, each with a node count followed
 * by resource nodes (value1/value2/value3/type plus 16 bytes of padding and
 * a uint16 id). Nodes whose art id is a valid item resource are inserted
 * onto the matching g_ResEntitySlots entry; type-3 nodes clamp value3
 * into value1.
 */
void
LoadItemRes(void)
{
	FILE *f;
	int i, j;
	uint32_t nodeCount;

	f = FileManager_OpenByType(0x29, NULL, "rb");
	if (f == NULL)
		return;

	CWorld_Lock();

	for (i = 0; i < 0x4000; i++) {
		fread_ServerSide(&nodeCount, 4, 1, f);
		SwapEndian(&nodeCount);

		for (j = 0; j < (int)nodeCount; j++) {
			CResourceNode *node;
			uint32_t v1, v2, v3;
			uint32_t rawId;

			node = ResourceNode_AllocFromPool();

			fread_ServerSide(&v1, 4, 1, f);
			SwapEndian(&v1);
			fread_ServerSide(&v2, 4, 1, f);
			SwapEndian(&v2);
			fread_ServerSide(&v3, 4, 1, f);
			SwapEndian(&v3);

			node->value1 = (int32_t)v1;
			node->value2 = (int32_t)v2;
			node->value3 = (int32_t)v3;

			fread_ServerSide(&node->type, 1, 1, f);

			fseek_ServerSide(f, 16, SEEK_CUR);

			fread_ServerSide(&rawId, 4, 1, f);
			SwapEndian(&rawId);
			node->id = (uint16_t)(rawId & 0xFFFF);

			if (node->id != 0 && node->type == 3 && node->value3 > node->value1) {
				node->value3 = node->value1;
			}

			if (CWorld_IsValidItemResource((uint16_t)i)) {
				CResourceEntity_InsertNode((CItem *)&g_ResEntitySlots[i], node);
			}
		}
	}

	fclose_ServerSide(f);
	CWorld_Unlock();
}

/*
 * 0x004E1F94 - LoadAll_TemplateStartup
 *
 * Thiscall shim that delegates to CTemplateManager_startup.
 */
void
LoadAll_TemplateStartup(void)
{
	CTemplateManager_startup();
}

/*
 * 0x004E1FA3 - ValidateRegionBounds
 *
 * Standard AABB overlap test between a region and a world rectangle.
 */
int
ValidateRegionBounds(int x1, int y1, int x2, int y2, int worldX, int worldY, int worldEndX, int worldEndY)
{
	if (x2 > worldX && x1 < worldEndX && y2 > worldY && y1 < worldEndY)
		return 1;
	return 0;
}

/*
 * 0x004E1FD1 - LoadResRegions
 *
 * Parses resregions.txt in the server directory, clamping each region
 * rectangle to the world bounds and registering valid regions with the
 * CResBankManager before kicking off CResBankManager_LoadResBank.
 */
void
LoadResRegions(void)
{
	FILE *f;
	CString pathStr;
	char line[512];
	int worldX, worldY, worldEndX, worldEndY;

	CString_Constructor(&pathStr, "../.rundir/");
	CString_AppendCStr(&pathStr, g_Config.serverName);
	CString_AppendCStr(&pathStr, "/resregions.txt");

	f = fopen_ServerSide(CString_GetBuffer(&pathStr), "r");
	if (f == NULL) {
		CString_Destructor(&pathStr);
		return;
	}

	while (!feof_ServerSide(f)) {
		CResBankRegion *region;
		int nParsed;

		region = (CResBankRegion *)malloc(sizeof(CResBankRegion));
		if (region == NULL)
			break;
		CResBankRegion_Constructor(region);

		fgets_ServerSide(line, 0x1FF, f);
		nParsed = sscanf(line, "Region Name: %s\n", region->name);
		if (nParsed <= 0) {
			CResBankRegion_Cleanup(region);
			free(region);
			break;
		}

		fgets_ServerSide(line, 0x1FF, f);
		nParsed = sscanf(line, "Region Rect: %d %d %d %d\n", &region->x1, &region->y1, &region->x2, &region->y2);
		if (nParsed <= 0) {
			CResBankRegion_Cleanup(region);
			free(region);
			break;
		}

		worldX = g_mapStartX;
		worldY = g_mapStartY;
		worldEndX = g_mapStartX + g_mapWidth;
		worldEndY = g_mapStartY + g_mapHeight;

		if (!ValidateRegionBounds(region->x1, region->y1, region->x2, region->y2, worldX, worldY, worldEndX, worldEndY)) {
			CResBankRegion_Cleanup(region);
			free(region);
			continue;
		}

		if (region->x1 < worldX)
			region->x1 = worldX;
		if (region->y1 < worldY)
			region->y1 = worldY;
		if (region->x2 > worldEndX)
			region->x2 = worldEndX;
		if (region->y2 > worldEndY)
			region->y2 = worldEndY;

		if (region->x1 < region->x2 && region->y1 < region->y2) {
			CResBankManager_AddRegion(region);
		} else {
			CResBankRegion_Cleanup(region);
			free(region);
		}
	}

	fclose_ServerSide(f);

	CResBankManager_LoadResBank();

	CString_Destructor(&pathStr);
}

/*
 * 0x004E2447 - LoadAll_ObjScrBitmap
 *
 * Reads objscr.txt as whitespace-delimited decimal art ids and sets the
 * matching bit in g_ValidateInWorldBitmap (ValidateInWorld consults this
 * mask to decide which arts carry scripts).
 */
void
LoadAll_ObjScrBitmap(void)
{
	FILE *f;
	int value;
	char ch;

	f = FileManager_OpenByType(0x31, "objscr.txt", "rb");
	if (f == NULL)
		return;

	value = 0;
	while (!feof_ServerSide(f)) {
		fread_ServerSide(&ch, 1, 1, f);
		if (isdigit((signed char)ch)) {
			value = value * 10 + ((signed char)ch - 0x30);
		} else {
			if (value > 0) {
				g_ValidateInWorldBitmap[value >> 5] |= (1 << (value & 0x1F));
				value = 0;
			}
		}
	}
	fclose_ServerSide(f);
}

/*
 * 0x004E2506 - LoadAll_StartupScript
 *
 * Runs the canonical "reset ; runfile start.cls ;" bootstrap through
 * CMagicItemFactory_ExecuteScript to load item/effect/script tables.
 */
void
LoadAll_StartupScript(void)
{
	CString str;

	CString_Constructor(&str, "reset ; runfile start.cls ;");
	CMagicItemFactory_ExecuteScript(&g_MagicItemFactory, &str);
	CString_Destructor(&str);
}

/*
 * 0x004E2560 - LoadAll_MultiManager
 *
 * Constructs g_MultiManager and loads its multi definitions.
 */
void
LoadAll_MultiManager(void)
{
	CMultiManager_Constructor(&g_MultiManager);
	CMultiManager_LoadMultiData(&g_MultiManager, 0);
}

/*
 * 0x004E257B - LoadAll_Skills
 *
 * Loads skill definitions and skill data into g_SkillManager.
 */
void
LoadAll_Skills(void)
{
	CSkillManager_LoadSkillDefs(&g_SkillManager);
	CSkillManager_LoadSkills(&g_SkillManager);
}

/*
 * 0x004E25BE - SaveAll_ItemRes
 *
 * Writes itemres.mul by emitting each g_ResEntitySlots chain as a node
 * count followed by the nodes themselves.
 */
void
SaveAll_ItemRes(void)
{
	FILE *fp;
	int i;
	int count;
	CResourceNode *node;

	fp = fopen_ServerSide(FileManager_GetCategory(0x29)->basePath, "wb");

	for (i = 0; i < 0x4000; i++) {
		count = 0;
		node = g_ResEntitySlots[i].nodeHead;
		while (node != NULL) {
			count++;
			node = node->next;
		}

		SwapEndian(&count);
		fwrite_ServerSide(&count, 4, 1, fp);

		node = g_ResEntitySlots[i].nodeHead;
		while (node != NULL) {
			CResourceNode_Save(node, fp);
			node = node->next;
		}
	}

	fclose_ServerSide(fp);
}

/*
 * 0x004E2694 - SaveAll_ResTypes
 *
 * Writes restypes.mul as a populated count followed by each registered
 * CResourceType entry in g_ResTypeEntries.
 */
void
SaveAll_ResTypes(void)
{
	int count;
	FILE *fp;
	int i;
	CResourceType *rt;

	count = 0;
	fp = fopen_ServerSide(FileManager_GetCategory(0x2C)->basePath, "wb");

	for (i = 0; i < 0x66; i++) {
		if (g_ResTypeEntries[i] != NULL)
			count++;
	}

	SwapEndian(&count);
	fwrite_ServerSide(&count, 4, 1, fp);

	for (i = 0; i < 0x66; i++) {
		rt = g_ResTypeEntries[i];
		if (rt != NULL)
			CResourceType_Save(rt, fp);
	}

	fclose_ServerSide(fp);
}

/*
 * 0x004E2753 - SaveAll_ResBank
 *
 * Thin wrapper that calls CResBankManager::NoOp on g_ResBankManager.
 * Zero callers in the binary; the resbank save path was stubbed out.
 *
 * MODIFIED (FEAT_CLOSED_ECONOMY): forward to CResBankManager_SaveResBank so
 * the live bank (drained quantities[]) persists across restarts. The default
 * build keeps the NoOp, preserving binary behavior.
 */
void
SaveAll_ResBank(void)
{
	if (feat(FEAT_CLOSED_ECONOMY)) {
		CResBankManager_SaveResBank();
		return;
	}
	CResBankManager_NoOp(&g_ResBankManager);
}
