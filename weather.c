/*
 * Weather system - circular weather zones with ramping intensity.
 *
 * WeatherManager loads zones from weather.txt, advances each zone's
 * precipitation intensity every tick, and sends weather-change packets
 * to players as they enter or leave zones or as conditions shift.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bankdefs.h"
#include "filemanager.h"
#include "gmedit.h"
#include "io.h"
#include "npc.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "terrain.h"
#include "utils.h"
#include "weather.h"

static void *WeatherNode_Constructor(WeatherNode *self, int x, int y, void *dataPtr, PathNodeList *parent); // 0x0049E8FB
static int WeatherEffect_AbsDiff(int a, int b); // 0x004A2515
static int WeatherEffect_RandomOffset(int val, int range); // 0x004A2529
static void WeatherEffect_UpdateRegions(intptr_t mode, int regionIdx, int baseX, int baseY); // 0x004A2556
static int WeatherEffect_Tick(int x, int y); // 0x004A2699
static int WeatherManager_getSeasonModifier(void); // 0x004E0A12
static int WeatherManager_getLatitudeModifier(int16_t y); // 0x004E0A66
static void CWeatherRegion_tick(CWeatherRegion *r); // 0x004E0BCE
static void WeatherEffect_LoadPolygons(FILE *fp, int scale, CItem *region, int isText); // 0x004A275A
static void WeatherEffect_ApplyAltitude(FILE *fp, CItem *region); // 0x004A304E
static void WeatherEffect_ApplyTilePatterns(FILE *fp, int scale, CItem *region); // 0x004A3148
static void WeatherEffect_UpdateBlocks(int mode); // 0x004A3CA3

/*
 * PathNodeList_Constructor (0x0049E814) is currently static in wombat_exec.c.
 * WeatherNode_NewConstruct (0x0049E882) is currently static in region.c.
 * WeatherNodeList_ScalarDelete (0x004A3E90) is currently static in region.c.
 * These will need to be made non-static when this code is wired up.
 */

/*
 * One row of g_WeatherTerrainRules (0x006205B8, 0x24 bytes): four corner
 * Z references, four adjacent tile ids, and a randomization range. The
 * table is terminated by z0 == -1.
 */
typedef struct WeatherTerrainRule {
	int32_t z0;    // +0x00
	int32_t z1;    // +0x04
	int32_t z2;    // +0x08
	int32_t z3;    // +0x0C
	int32_t tile0; // +0x10
	int32_t tile1; // +0x14
	int32_t tile2; // +0x18
	int32_t tile3; // +0x1C
	int32_t range; // +0x20
} WeatherTerrainRule;

// 0x006205B8 - terrain rule table terminated by z0 == -1
// clang-format off
static WeatherTerrainRule g_WeatherTerrainRules[] = {
	{-15,   0,   0,   0, -15,  -3,   2,  -3, 1},
	{  0,   0, -15, -15,  -1,  -1, -15, -15, 4},
	{  0, -15, -15,   0,  -1, -15, -15,  -1, 4},
	{-15, -15,   0,   0, -15, -15,  -1,  -1, 2},
	{-15,   0,   0, -15, -15,  -1,  -1, -15, 2},
	{  0, -15,   0,   0,  -2, -15,   0,   0, 1},
	{  0,   0,   0, -15,  -2,   0,   0, -15, 1},
	{  0,   0, -15,   0,   1,  -1, -15,  -1, 4},
	{ -1,  -1,  -1,  -1,   0,   0,   0,   0, 0},
};
// clang-format on

WeatherManager g_WeatherManager;

/*
 * 0x0049E83F - WeatherNode container destructor loop
 *
 * Thiscall. Iterates the linked list of children at this[+0x08].
 * For each child node, calls WeatherNode_ScalarDelete with flags=1
 * (delete + free). Loops until this[+0x08] is NULL.
 * Called from WeatherNodeList_ScalarDelete.
 */
void
WeatherNodeList_DestroyChildren(PathNodeList *this)
{
	WeatherNode *child;

	for (;;) {
		child = this->head;
		if (child == NULL)
			break;
		WeatherNode_ScalarDelete(child, 1);
	}
}

/*
 * 0x0049E882 - WeatherNode::new+construct
 *
 * Allocates a WeatherNode and runs its constructor. Returns NULL when the
 * allocation fails.
 */
void *
WeatherNode_NewConstruct(PathNodeList *parent, int x, int y, void *dataPtr)
{
	WeatherNode *node;

	node = (WeatherNode *)OperatorNew(sizeof(WeatherNode));
	if (node != NULL)
		return WeatherNode_Constructor(node, x, y, dataPtr, parent);
	return NULL;
}

/*
 * 0x0049E8FB - WeatherNode constructor
 *
 * Stores (x, y, dataPtr, parent) in the node and prepends it to parent's
 * head-linked child list, bumping parent->count.
 */
static void *
WeatherNode_Constructor(WeatherNode *self, int x, int y, void *dataPtr, PathNodeList *parent)
{
	WeatherNode *oldHead;

	self->x = (double)x;
	self->y = (double)y;
	self->dataPtr = dataPtr;
	self->parent = parent;

	// Link into parent's child list
	oldHead = parent->head;
	self->next = oldHead;
	if (oldHead != NULL)
		oldHead->prev = self;
	self->prev = NULL;
	parent->head = self;

	parent->count++;

	return self;
}

/*
 * 0x0049E97D - WeatherNode unlink destructor
 *
 * Thiscall. Unlinks this node from the parent's doubly-linked child list.
 * Fields: self[0x14] = next, self[0x18] = prev, self[0x1C] = parent.
 * If next exists, patches next->prev = prev. If prev exists, patches
 * prev->next = next. Otherwise, updates parent->head (at +0x08) = next.
 * Decrements parent->count (at +0x04).
 * Called from WeatherNode_ScalarDelete.
 */
void
WeatherNode_Unlink(WeatherNode *self)
{
	WeatherNode *next = self->next;
	WeatherNode *prev = self->prev;
	PathNodeList *plist = (PathNodeList *)self->parent;

	if (next != NULL)
		next->prev = prev;

	if (prev != NULL) {
		prev->next = next;
	} else {
		plist->head = next;
	}

	plist->count--;
}

/*
 * 0x004A2515 - WeatherEffect_AbsDiff
 *
 * Cdecl, 2 args. Returns abs(a - b).
 */
static int
WeatherEffect_AbsDiff(int a, int b)
{
	return abs(a - b);
}

/*
 * 0x004A2529 - WeatherEffect_RandomOffset
 *
 * Cdecl, 2 args (val, range). If val is 0 or -15 (0xFFFFFFF1),
 * returns val directly. Otherwise returns val + rand() % (2*range+1) - range.
 */
static int
WeatherEffect_RandomOffset(int val, int range)
{
	if (val == 0 || val == -15)
		return val;
	return val + rand() % (2 * range + 1) - range;
}

/*
 * 0x004A2556 - WeatherEffect_UpdateRegions
 *
 * Cdecl, 4 args (mode, regionIdx, baseX, baseY). Loops through 9
 * neighboring tile positions (3x3 grid, skipping center at index 4).
 * For each position, gets the land tile ID and reads a region byte
 * from the per-region 9-char pattern string. Switches on the byte:
 * 0x20 (' ') = skip, 0x30 ('0') = slope required (return if all 4 Z
 * corners are equal), 0x31 ('1') = water required (return if tile ID
 * is not in the water range). If every required position matches, the
 * center letter selects the target tile ID from g_WeatherTileIDs and
 * SetTerrainTile is called with z = -666.
 *
 * Binary data tables:
 *   0x00620700: x-offsets {-1,0,1,-1,0,1,-1,0,1}
 *   0x00620728: y-offsets {-1,-1,-1,0,0,0,1,1,1}
 *   0x00620400: pointer array to region pattern strings (A..Y + NULL)
 *   0x00620398: 25-entry tile ID table (0x4C..0x64) used both for the
 *               water-match check (indices 4..11) and for the final
 *               SetTerrainTile call indexed by (centerChar - 'A'). In
 *               the binary the base is written as 0x00620294 so that
 *               the raw sign-extended character 'A' (0x41) lands at
 *               0x00620398 via (char * 4 + 0x00620294).
 */
// 0x00620700 - 3x3 neighbor x-offsets
static const int g_WeatherXOff[9] = { -1, 0, 1, -1, 0, 1, -1, 0, 1 };
// 0x00620728 - 3x3 neighbor y-offsets
static const int g_WeatherYOff[9] = { -1, -1, -1, 0, 0, 0, 1, 1, 1 };

// 0x00620398 - 25-entry tile ID table (water check + center-char lookup)
static const uint32_t g_WeatherTileIDs[25] = {
	0x4C,
	0x4D,
	0x4E,
	0x4F,
	0x50,
	0x51,
	0x52,
	0x53,
	0x54,
	0x55,
	0x56,
	0x57,
	0x58,
	0x59,
	0x5A,
	0x5B,
	0x5C,
	0x5D,
	0x5E,
	0x5F,
	0x60,
	0x61,
	0x62,
	0x63,
	0x64,
};

// 0x00620400 - per-region 9-char pattern strings (A..Y + NULL terminator)
static const char *const g_WeatherRegionDefs[26] = {
	" 0 0A    ",
	" 0  B0   ",
	"    C0 0 ",
	"   0D  0 ",
	" 0  E    ",
	"    F0   ",
	"    G  0 ",
	"   0H    ",
	"0   I    ",
	"  0 J    ",
	"    K   0",
	"    L 0  ",
	" 1 1M    ",
	" 1  N1   ",
	"    O1 1 ",
	"   1P  1 ",
	" 1  Q    ",
	"    R1   ",
	"    S  1 ",
	"   1T    ",
	"1   U    ",
	"  1 V    ",
	"    W   1",
	"    X 1  ",
	"    Y    ",
	NULL,
};

static void
WeatherEffect_UpdateRegions(intptr_t mode, int regionIdx, int baseX, int baseY)
{
	int i, j;
	uint32_t tileID;
	uint8_t regionByte;
	int *zQuad;

	for (i = 0; i < 9; i++) {
		if (i == 4)
			continue;

		tileID = CTerrainManager_GetLandTileID(NULL, baseX + g_WeatherXOff[i], baseY + g_WeatherYOff[i]);

		regionByte = (uint8_t)g_WeatherRegionDefs[regionIdx][i];

		if (regionByte == 0x20) {
			continue;
		} else if (regionByte == 0x30) {
			// slope required - abort if this tile is perfectly flat
			zQuad = CTerrainManager_GetLandZQuad(baseX + g_WeatherXOff[i], baseY + g_WeatherYOff[i]);
			if (zQuad[0] != zQuad[1])
				continue;
			if (zQuad[1] != zQuad[2])
				continue;
			if (zQuad[2] != zQuad[3])
				continue;
			return;
		} else if (regionByte == 0x31) {
			// water required - abort if this tile is not in 0x50..0x57
			for (j = 4; j < 12; j++) {
				if (tileID == g_WeatherTileIDs[j])
					break;
			}
			if (j == 12)
				return;
		} else {
			continue;
		}
	}

	SetTerrainTile(mode, baseX, baseY, g_WeatherTileIDs[(int)(int8_t)g_WeatherRegionDefs[regionIdx][4] - 'A'], -666);
}

/*
 * 0x004A2699 - WeatherEffect_Tick
 *
 * Cdecl, 2 args (x, y). Checks 4 tile corners: (x,y), (x,y-1),
 * (x-1,y), (x-1,y-1). For each, looks up the land tile ID via
 * CTerrainManager_GetLandTileID, then checks the textureID field
 * (offset +0x04) of the LandTileData entry. Returns 0 if any
 * corner has textureID == 0, returns 1 if all are non-zero.
 */
static int
WeatherEffect_Tick(int x, int y)
{
	uint16_t tileID;

	tileID = CTerrainManager_GetLandTileID(NULL, x, y);
	if (g_LandTileData[tileID].textureID == 0)
		return 0;

	tileID = CTerrainManager_GetLandTileID(NULL, x, y - 1);
	if (g_LandTileData[tileID].textureID == 0)
		return 0;

	tileID = CTerrainManager_GetLandTileID(NULL, x - 1, y);
	if (g_LandTileData[tileID].textureID == 0)
		return 0;

	tileID = CTerrainManager_GetLandTileID(NULL, x - 1, y - 1);
	if (g_LandTileData[tileID].textureID == 0)
		return 0;

	return 1;
}

/*
 * 0x004A275A - WeatherEffect_LoadPolygons
 *
 * Loads weather polygons (text or binary format) from fp, registers
 * each polygon as a weather node tied to its named res-bank set,
 * then walks the spatial grid: voiding-out tiles update the region
 * mapping, and a second pass matches terrain Z quads against the
 * WeatherTerrainRule table.
 *
 * FIXED: in the text-format path, the binary passes the nodeList
 * pointer's value (not its address) as the first %d arg to fscanf,
 * so fscanf overwrites the first 4 bytes of *nodeList with the
 * `closed` count. The corrupted slot is then handed to
 * WeatherNode_NewConstruct/CResBankManager_ProcessEntry/
 * WeatherNodeList_ScalarDelete. We use a separate `closed` local to
 * avoid the corruption. Orphaned dead code in the binary, but the
 * bug would crash if ever called.
 */
static __attribute__((unused)) void
WeatherEffect_LoadPolygons(FILE *fp, int scale, CItem *region, int isText)
{
	int numPolies;
	int i, j, k;
	void *allocPtr;
	PathNodeList *nodeList;
	int closed, numPoints;
	int px, py;
	int setIndex;
	char nameBuf[128];
	PathNodeList *tmpPtr;
	int blockIdx;
	int originX, originY;
	int bx, by;
	int tileID;
	int ruleIdx;
	int *zQuad;

	if (isText) {
		// Text format path.
		fscanf(fp, "NumPolies %d\n", &numPolies);
		for (i = 0; i < numPolies; i++) {
			allocPtr = OperatorNew(sizeof(PathNodeList));
			if (allocPtr != NULL)
				nodeList = PathNodeList_Constructor(allocPtr);
			else
				nodeList = NULL;

			fscanf(fp, "\nClosed    %d\nNumPoints %d\n", &closed, &numPoints);

			for (j = 0; j < numPoints; j++) {
				fscanf(fp, "  x y set %d %d %s\n", &px, &py, nameBuf);

				// Replace underscores with spaces.
				for (k = 0; k < 0x80; k++) {
					if (nameBuf[k] == '_')
						nameBuf[k] = ' ';
				}

				setIndex = ResBankSet_LookupByName(nameBuf);
				px -= 1;
				py -= 1;
				px = px / scale;
				py = py / scale;
				WeatherNode_NewConstruct(nodeList, px, py, (void *)(intptr_t)setIndex);
			}

			CResBankManager_ProcessEntry((CDistribEntry *)nodeList, (CEntity *)region);

			tmpPtr = nodeList;
			if (tmpPtr != NULL)
				WeatherNodeList_ScalarDelete(tmpPtr, 1);
		}
	} else {
		// Binary format path.
		fread_ServerSide(&numPolies, 4, 1, fp);
		SwapEndian(&numPolies);
		for (i = 0; i < numPolies; i++) {
			allocPtr = OperatorNew(sizeof(PathNodeList));
			if (allocPtr != NULL)
				nodeList = PathNodeList_Constructor(allocPtr);
			else
				nodeList = NULL;

			fread_ServerSide(&closed, 4, 1, fp);
			SwapEndian(&closed);
			fread_ServerSide(nodeList, 4, 1, fp);
			SwapEndian(nodeList);

			for (j = 0; j < closed; j++) {
				fread_ServerSide(&px, 4, 1, fp);
				SwapEndian(&px);
				fread_ServerSide(&py, 4, 1, fp);
				SwapEndian(&py);
				fread_ServerSide(&setIndex, 4, 1, fp);
				SwapEndian(&setIndex);
				px -= 1;
				py -= 1;
				px = px / scale;
				py = py / scale;
				WeatherNode_NewConstruct(nodeList, px, py, (void *)(intptr_t)setIndex);
			}

			CResBankManager_ProcessEntry((CDistribEntry *)nodeList, (CEntity *)region);

			tmpPtr = nodeList;
			if (tmpPtr != NULL)
				WeatherNodeList_ScalarDelete(tmpPtr, 1);
		}
	}

	// Pass 1: iterate all blocks, clear active flag, check void tiles.
	for (i = 0; i < 0x19; i++) {
		for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
			if (!(g_SpatialGrid.cells[blockIdx].flags111 & 0x40))
				continue;

			CBlockManager_GetBlockOrigin(&g_SpatialGrid, blockIdx, &originX, &originY);

			for (bx = originY; bx < originY + 8; bx++) {
				for (by = originX; by < originX + 8; by++) {
					tileID = g_SpatialGrid.cells[blockIdx].pad00[((bx & 7) * 8 + (by & 7)) * 4];
					tileID |= g_SpatialGrid.cells[blockIdx].pad00[((bx & 7) * 8 + (by & 7)) * 4 + 1] << 8;
					if (tileID == 0) {
						WeatherEffect_UpdateRegions((intptr_t)region, i, by, bx);
					}
				}
			}
		}
	}

	// Pass 2: iterate all blocks, clear active flag, apply terrain rules.
	for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
		if (!(g_SpatialGrid.cells[blockIdx].flags111 & 0x40))
			continue;

		// Clear the active flag.
		g_SpatialGrid.cells[blockIdx].flags111 &= ~0x40;

		CBlockManager_GetBlockOrigin(&g_SpatialGrid, blockIdx, &originX, &originY);

		for (bx = originY; bx < originY + 8; bx++) {
			for (by = originX; by < originX + 8; by++) {
				zQuad = CTerrainManager_GetLandZQuad(by, bx);

				for (ruleIdx = 0; g_WeatherTerrainRules[ruleIdx].z0 != -1; ruleIdx++) {
					if (WeatherEffect_AbsDiff(zQuad[0], g_WeatherTerrainRules[ruleIdx].z0) >= 9)
						continue;
					if (WeatherEffect_AbsDiff(zQuad[1], g_WeatherTerrainRules[ruleIdx].z1) >= 9)
						continue;
					if (WeatherEffect_AbsDiff(zQuad[2], g_WeatherTerrainRules[ruleIdx].z2) >= 9)
						continue;
					if (WeatherEffect_AbsDiff(zQuad[3], g_WeatherTerrainRules[ruleIdx].z3) >= 9)
						continue;

					// Position (x, y): check texture, set tile.
					if (WeatherEffect_Tick(by, bx)) {
						SetTerrainTile((intptr_t)region, by, bx, -666,
						        WeatherEffect_RandomOffset(g_WeatherTerrainRules[ruleIdx].tile0, g_WeatherTerrainRules[ruleIdx].range));
					}

					// Position (x, y+1): check texture, set tile.
					if (WeatherEffect_Tick(by + 1, bx)) {
						SetTerrainTile((intptr_t)region, by + 1, bx, -666,
						        WeatherEffect_RandomOffset(g_WeatherTerrainRules[ruleIdx].tile1, g_WeatherTerrainRules[ruleIdx].range));
					}

					// Position (x+1, y+1): check texture, set tile.
					if (WeatherEffect_Tick(by + 1, bx + 1)) {
						SetTerrainTile((intptr_t)region, by + 1, bx + 1, -666,
						        WeatherEffect_RandomOffset(g_WeatherTerrainRules[ruleIdx].tile2, g_WeatherTerrainRules[ruleIdx].range));
					}

					// Position (x+1, y): check texture, set tile.
					if (WeatherEffect_Tick(by, bx + 1)) {
						SetTerrainTile((intptr_t)region, by, bx + 1, -666,
						        WeatherEffect_RandomOffset(g_WeatherTerrainRules[ruleIdx].tile3, g_WeatherTerrainRules[ruleIdx].range));
					}
				}
			}
		}
	}
}

/*
 * 0x004A304E - WeatherEffect_ApplyAltitude
 *
 * Reads altitude data from a binary file and applies it to the terrain.
 * Seeks to end of file to determine size, computes grid dimension as
 * (int)(sqrt((double)fileSize) + 0.5). Then iterates region->y to
 * region->y + gridDim (outer) and region->x to region->x + gridDim
 * (inner), reading one byte per tile from the file. For each valid
 * coordinate, sets terrain elevation to (byte & 0xFF) - 0x80 (signed
 * offset from 128), with tile ID -666 (no change).
 *
 * Args: fp = open file, region = entity pointer providing origin x/y
 * at offsets +0x0A and +0x0C (CEntity.location.x/y).
 */
static __attribute__((unused)) void
WeatherEffect_ApplyAltitude(FILE *fp, CItem *region)
{
	int fileSize;
	int gridDim;
	int outerY, innerX;
	int8_t val;

	fseek_ServerSide(fp, 0, 2);
	fileSize = ftell_ServerSide(fp);
	fseek_ServerSide(fp, 0, 0);

	gridDim = (int)(sqrt((double)fileSize) + 0.5);

	for (outerY = region->resourceEntity.entity.location.y; outerY < region->resourceEntity.entity.location.y + gridDim; outerY++) {
		for (innerX = region->resourceEntity.entity.location.x; innerX < region->resourceEntity.entity.location.x + gridDim; innerX++) {
			fread_ServerSide(&val, 1, 1, fp);
			if (CBlockManager_IsValidCoord(&g_SpatialGrid, innerX, outerY)) {
				SetTerrainTile((intptr_t)region, innerX, outerY, -666, (val & 0xFF) - 0x80);
			}
		}
	}
}

/*
 * 0x004A3148 - WeatherEffect_ApplyTilePatterns
 *
 * Reads width/height (uint16_t each) from a binary file, then iterates
 * the tile grid reading color values (uint16_t per tile). For tiles
 * aligned to the scale grid, looks up a CResBankSet by type 3 (TERRAIN)
 * and color, then uses rand() modulo memberCount to pick a random member.
 * If the member has a valid tileId, calls SetTerrainTile.
 *
 * After the first pass, performs 4 additional passes over the border
 * region tiles (starting from region->y+1 to region->y+height-2), checking
 * 4 adjacent tile patterns against each CResBankSet of type 7 (TRANSITION).
 * Each pattern checks if the center tile plus specific neighbors all belong
 * to the same set, then applies the appropriate transition tile.
 *
 * Args: fp = open file, scale = grid scale divisor,
 * region = entity pointer providing origin x/y at +0x0A/+0x0C.
 */
static __attribute__((unused)) void
WeatherEffect_ApplyTilePatterns(FILE *fp, int scale, CItem *region)
{
	uint16_t width, height;
	int outerY, innerX;
	uint16_t color;
	int tileX, tileY;
	CResBankSet *set;
	CResBankSetMember *member;
	int memberIdx;
	int loopI;
	CResBankSet *cur;
	int16_t regionX, regionY;

	// Seek past 2-byte header, read width and height.
	fseek_ServerSide(fp, 2, 0);
	fread_ServerSide(&width, 2, 1, fp);
	SwapEndian(&width);
	fread_ServerSide(&height, 2, 1, fp);
	SwapEndian(&height);

	regionX = region->resourceEntity.entity.location.x;
	regionY = region->resourceEntity.entity.location.y;

	// Pass 0: read color grid and apply terrain tiles at scale boundaries.
	for (outerY = 0; (uint32_t)outerY < (height & 0xFFFF); outerY++) {
		for (innerX = 0; (uint32_t)innerX < (width & 0xFFFF); innerX++) {
			fread_ServerSide(&color, 2, 1, fp);
			SwapEndian(&color);

			tileX = regionX + innerX / scale;
			tileY = regionY + outerY / scale;

			if (!CBlockManager_IsValidCoord(&g_SpatialGrid, tileX, tileY))
				continue;

			if (innerX % scale != 0)
				continue;
			if (outerY % scale != 0)
				continue;

			set = ResBankSet_FindByTypeAndColor(3, color & 0xFFFF);
			if (set == NULL)
				continue;
			if (set->memberCount == 0)
				continue;

			member = set->memberTail;
			memberIdx = rand() % set->memberCount;

			for (loopI = 0; loopI < memberIdx; loopI++) {
				member = member->prev;
			}

			if (member->tileId == 0)
				continue;

			SetTerrainTile((intptr_t)region, tileX, tileY, member->tileId, -666);
		}
	}

	// Pass 1: South-East pattern.
	// Check: (x,y), (x+1,y), (x,y-1), and diagonal(y-1,x+1) with subtype 1.
	for (outerY = regionY + 1; outerY < regionY + (int)(height & 0xFFFF) - 1; outerY++) {
		for (innerX = regionX + 1; innerX < regionX + (int)(width & 0xFFFF) - 1; innerX++) {
			for (cur = g_ResBankSetListHead; cur != NULL; cur = cur->next) {
				if (cur->type != 7)
					continue;

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					goto pass1_next;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY), 0))
					goto pass1_next;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY - 1), 0))
					goto pass1_next;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY - 1), 1))
					goto pass1_next;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 3)->tileId, -666);
				goto pass1_done;
pass1_next:

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					goto pass1_next2;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY), 0))
					goto pass1_next2;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY + 1), 0))
					goto pass1_next2;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY + 1), 1))
					goto pass1_next2;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 4)->tileId, -666);
				goto pass1_done;
pass1_next2:

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					goto pass1_next3;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY), 0))
					goto pass1_next3;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY + 1), 0))
					goto pass1_next3;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY + 1), 1))
					goto pass1_next3;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 5)->tileId, -666);
				goto pass1_done;
pass1_next3:

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					continue;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY), 0))
					continue;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY - 1), 0))
					continue;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY - 1), 1))
					continue;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 2)->tileId, -666);
				goto pass1_done;
			}
pass1_done:;
		}
	}

	// Pass 2: North-West pattern (with subtype 1 checks on 3 neighbors).
	for (outerY = regionY + 1; outerY < regionY + (int)(height & 0xFFFF) - 1; outerY++) {
		for (innerX = regionX + 1; innerX < regionX + (int)(width & 0xFFFF) - 1; innerX++) {
			for (cur = g_ResBankSetListHead; cur != NULL; cur = cur->next) {
				if (cur->type != 7)
					continue;

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					goto pass2_next;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY), 1))
					goto pass2_next;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY - 1), 1))
					goto pass2_next;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY - 1), 1))
					goto pass2_next;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 9)->tileId, -666);
				goto pass2_done;
pass2_next:

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					goto pass2_next2;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY), 1))
					goto pass2_next2;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY + 1), 1))
					goto pass2_next2;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY + 1), 1))
					goto pass2_next2;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 6)->tileId, -666);
				goto pass2_done;
pass2_next2:

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					goto pass2_next3;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY), 1))
					goto pass2_next3;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY + 1), 1))
					goto pass2_next3;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY + 1), 1))
					goto pass2_next3;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 7)->tileId, -666);
				goto pass2_done;
pass2_next3:

				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0))
					continue;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY), 1))
					continue;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY - 1), 1))
					continue;
				if (!CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY - 1), 1))
					continue;

				SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 8)->tileId, -666);
				goto pass2_done;
			}
pass2_done:;
		}
	}

	// Pass 3: Edge detection (2-neighbor checks with subtypes 0 and 1).
	for (outerY = regionY + 1; outerY < regionY + (int)(height & 0xFFFF) - 1; outerY++) {
		for (innerX = regionX + 1; innerX < regionX + (int)(width & 0xFFFF) - 1; innerX++) {
			for (cur = g_ResBankSetListHead; cur != NULL; cur = cur->next) {
				if (cur->type != 7)
					continue;

				// East edge: (x,y) + (x+1,y).
				if (CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0) &&
				        CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX + 1, outerY), 1)) {
					SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 0xB)->tileId, -666);
					goto pass3_done;
				}

				// South edge: (x,y) + (x-1,y).
				if (CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0) &&
				        CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX - 1, outerY), 1)) {
					SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 0xD)->tileId, -666);
					goto pass3_done;
				}

				// North edge: (x,y) + (x,y+1).
				if (CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0) &&
				        CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY + 1), 1)) {
					SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 0xC)->tileId, -666);
					goto pass3_done;
				}

				// West edge: (x,y) + (x,y-1).
				if (CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY), 0) &&
				        CResBankSet_ContainsMember(cur, CTerrainManager_GetLandTileID(NULL, innerX, outerY - 1), 1)) {
					SetTerrainTile((intptr_t)region, innerX, outerY, CResBankSet_GetTypeMember(cur, 0xA)->tileId, -666);
					goto pass3_done;
				}
			}
pass3_done:;
		}
	}
}

/*
 * 0x004A3CA3 - WeatherEffect_UpdateBlocks
 *
 * Iterates all spatial grid blocks. For each block, iterates the 8x8
 * tile region. For each tile, gets the land tile ID via
 * CTerrainManager_GetLandTileID. If the tile ID is in range
 * [0xA8, 0xAB], calls SetTerrainTile on the tile and up to 3 adjacent
 * tiles (x+1, x+1/y+1, x+1/y) with tileID -666 and elevation -5,
 * checking coordinate validity via CBlockManager_IsValidCoord for each.
 *
 * Args: mode = value passed through to SetTerrainTile as first arg.
 */
static __attribute__((unused)) void
WeatherEffect_UpdateBlocks(int mode)
{
	int blockIdx;
	int originX, originY;
	int bx, by;
	int tileID;

	for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
		CBlockManager_GetBlockOrigin(&g_SpatialGrid, blockIdx, &originX, &originY);

		for (bx = originY; bx < originY + 8; bx++) {
			for (by = originX; by < originX + 8; by++) {
				tileID = CTerrainManager_GetLandTileID(NULL, by, bx);

				if (tileID < 0xA8 || tileID > 0xAB)
					continue;

				// Set current tile.
				SetTerrainTile(mode, by, bx, -666, -5);

				// Adjacent tile (y+1).
				if (CBlockManager_IsValidCoord(&g_SpatialGrid, by + 1, bx)) {
					SetTerrainTile(mode, by + 1, bx, -666, -5);
				}

				// Adjacent tile (y+1, x+1).
				if (CBlockManager_IsValidCoord(&g_SpatialGrid, by + 1, bx + 1)) {
					SetTerrainTile(mode, by + 1, bx + 1, -666, -5);
				}

				// Adjacent tile (y, x+1).
				if (CBlockManager_IsValidCoord(&g_SpatialGrid, by, bx + 1)) {
					SetTerrainTile(mode, by, bx + 1, -666, -5);
				}
			}
		}
	}
}
/*
 * 0x004E04FC - WeatherManager::getAverageWeather
 *
 * Returns the average temperature at loc by summing the season modifier,
 * latitude modifier, and all weather regions whose center is within
 * Chebyshev range of loc.
 */
int
WeatherManager_getAverageWeather(CLocation *loc)
{
	CWeatherRegion *r;
	int accumulator, count;
	int dist;
	CLocation regionLoc;

	CLocation_Init(&regionLoc);
	accumulator = 0;
	count = 0;

	accumulator += WeatherManager_getSeasonModifier();
	accumulator += WeatherManager_getLatitudeModifier(loc->y);
	count++;

	for (r = g_WeatherManager.first; r != NULL; r = r->next) {
		regionLoc.x = r->centerX;
		regionLoc.y = r->centerY;
		regionLoc.z = 0;
		dist = CLocation_ChebyshevDistance(loc, &regionLoc);
		if (dist >= r->radius)
			continue;
		accumulator += CWeatherRegion_getCurrentWeather(r);
		count++;
	}

	if (count > 0)
		accumulator /= count;
	return accumulator;
}

/*
 * 0x004E05DE - WeatherManager::startup
 *
 * Parses weather.txt: the first line is the region count and each
 * subsequent block of fields ("Name", "CenterX", "Radius", ...)
 * defines one CWeatherRegion. Each region is constructed and pushed
 * onto the manager's region list.
 */
int
WeatherManager_startup(void)
{
	FILE *f;
	char line[512];
	int regionCount, i;
	CWeatherRegion *r;
	int centerX, centerY;
	int precipChance, extremeTempChance, extremeWeatherChance;

	f = FileManager_OpenByType(0x2D, NULL, "r");
	if (f == NULL)
		return 0;

	fgets_ServerSide(line, 511, f);
	sscanf(line, "%d\n", &regionCount);

	for (i = 0; i < regionCount; i++) {
		r = (CWeatherRegion *)OperatorNew(sizeof(CWeatherRegion));
		if (r != NULL)
			CWeatherRegion_Init(r);

		// "Name: %s\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "Name: %s\n", r->name);

		// "CenterX: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "CenterX: %d\n", &centerX);

		// "CenterY: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "CenterY: %d\n", &centerY);

		// "Radius: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "Radius: %d\n", &r->radius);

		// "MoveSpeed: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "MoveSpeed: %d\n", &r->moveSpeed);

		// "Average Temperature: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "Average Temperature: %d\n", &r->avgTemperature);

		// "Chance of precipitation: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "Chance of precipitation: %d\n", &precipChance);

		// "Chance of extreme temperature: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "Chance of extreme temperature: %d\n", &extremeTempChance);

		// "Chance of extreme weather: %d\n"
		fgets_ServerSide(line, 511, f);
		sscanf(line, "Chance of extreme weather: %d\n", &extremeWeatherChance);

		r->centerX = (int16_t)centerX;
		r->centerY = (int16_t)centerY;
		r->precipChance = precipChance;
		r->extremeTempChance = extremeTempChance;
		r->extremeWeatherChance = extremeWeatherChance;

		// 0x004E09A3 WeatherManager_addRegionToList - append to tail
		r->prev = g_WeatherManager.last;
		if (r->prev != NULL)
			r->prev->next = r;
		g_WeatherManager.last = r;
		if (g_WeatherManager.first == NULL)
			g_WeatherManager.first = r;
		r->next = NULL;
	}

	fclose_ServerSide(f);
	return 0;
}

/*
 * 0x004E096F - WeatherManager::UpdateWeather
 *
 * Calls CWeatherRegion::tick on every region in the manager's list.
 */
void
WeatherManager_UpdateWeather(void)
{
	CWeatherRegion *r;

	for (r = g_WeatherManager.first; r != NULL; r = r->next)
		CWeatherRegion_tick(r);
}

/*
 * 0x004E0A12 - WeatherManager::getSeasonModifier
 *
 * Returns temperature adjustment based on current season.
 */
static int
WeatherManager_getSeasonModifier(void)
{
	switch (g_WeatherManager.season) {
	case 0:
		return -30; // winter
	case 1:
		return 5; // spring
	case 2:
		return 30; // summer
	case 3:
		return -5; // fall
	default:
		return 0;
	}
}

/*
 * 0x004E0A66 - WeatherManager::getLatitudeModifier
 *
 * Returns temperature adjustment based on Y coordinate (latitude).
 * Low Y = north (cold), high Y = south (warm).
 */
static int
WeatherManager_getLatitudeModifier(int16_t y)
{
	if (y >= 0 && y < 32)
		return -30;
	if (y > 96)
		return 30;
	return 0;
}

/*
 * 0x004E0A95 - GetSeasonAsText
 *
 * Returns the printable season name for the given season id.
 */
const char *
GetSeasonAsText(int season)
{
	switch (season) {
	case 0:
		return "winter";
	case 1:
		return "spring";
	case 2:
		return "summer";
	case 3:
		return "fall";
	default:
		return "bad season";
	}
}

/*
 * 0x004E0AEB - WeatherManager::AdvanceSeason
 *
 * Called from CTimeManager_SeasonAdvance when month rolls over.
 * Advances the weather season: 0=winter, 1=spring, 2=summer, 3=fall.
 */
void
WeatherManager_AdvanceSeason(void)
{
	g_WeatherManager.season++;
	if (g_WeatherManager.season > 3)
		g_WeatherManager.season = 0;
}

/*
 * 0x004E0B18 - CWeatherRegion::Init
 *
 * Thiscall constructor. Zeros 5 precipitation simulation fields:
 * intensity, duration, cooldownCounter, cooldownTarget, isPrecipitating.
 * Note: moveSpeed (0x134) and isWaning (0x13C) are NOT zeroed.
 */
void
CWeatherRegion_Init(CWeatherRegion *r)
{
	r->isPrecipitating = 0;
	r->cooldownTarget = 0;
	r->cooldownCounter = 0;
	r->intensity = 0;
	r->duration = 0;
}

/*
 * 0x004E0B81 - CWeatherRegion::getCurrentWeather
 *
 * Returns this region's weather value: avgTemp + season + latitude(center).
 */
int
CWeatherRegion_getCurrentWeather(CWeatherRegion *r)
{
	return r->avgTemperature + WeatherManager_getSeasonModifier() + WeatherManager_getLatitudeModifier(r->centerY);
}

/*
 * 0x004E0BCE - CWeatherRegion::tick
 *
 * Advances one tick of the region's precipitation simulation. Even
 * during cooldown a random roll is taken; on a state change the
 * current weather is broadcast as a WEATHERCHANGE packet to clients
 * within the region's radius.
 */
static void
CWeatherRegion_tick(CWeatherRegion *r)
{
	int temp;
	int shouldBroadcast;
	int numEffects;
	uint8_t obuf[8];
	CLocation loc;

	if (!r->isPrecipitating) {
		if (r->cooldownTarget > 0) {
			r->cooldownCounter++;
			if (r->cooldownCounter > r->cooldownTarget) {
				r->cooldownTarget = 0;
				r->cooldownCounter = 0;
			}
		}
		if (GetRandomRange(1, 100) < r->precipChance) {
			r->intensity = 0;
			r->duration = r->precipChance + GetRandomRange(1, 100) + 100;
			if (r->duration > 70)
				r->duration = 70;
			r->isPrecipitating = 1;
			r->isWaning = 0;
			r->currentIntensity = 0;
			r->currentTemp = r->precipChance;
		}
	}

	if (r->isPrecipitating) {
		r->currentIntensity = r->intensity;
		if (!r->isWaning) {
			r->intensity += 5;
			if (r->intensity > r->duration)
				r->isWaning = 1;
		} else {
			r->intensity -= 5;
			if (r->intensity < 1) {
				r->isPrecipitating = 0;
				r->cooldownCounter = 0;
				r->cooldownTarget = (100 - r->intensity) * 50;
			}
		}
	}

	temp = CWeatherRegion_getCurrentWeather(r);
	shouldBroadcast = 0;
	if (r->isPrecipitating)
		shouldBroadcast = 1;

	if (shouldBroadcast) {
		numEffects = r->currentIntensity;
		if (numEffects > 70)
			numEffects = 70;
		PacketManager_MakePacket_WEATHERCHANGE(obuf, 0x02, (uint8_t)numEffects, (uint8_t)temp);
		CLocation_Init(&loc);
		loc.x = r->centerX;
		loc.y = r->centerY;
		loc.z = 0;
		SendPacketInRange(obuf, &loc, r->radius);
	}
}

/*
 * 0x004E0E3D - WeatherManager::sendWeatherToPlayer
 *
 * Sends a WEATHERCHANGE packet (type 0xFE) carrying the season and the
 * average temperature at the entity's location. Called on login.
 */
void
WeatherManager_sendWeatherToPlayer(CItem *entity)
{
	int temp;
	uint8_t obuf[8];

	temp = WeatherManager_getAverageWeather(&entity->resourceEntity.entity.location);

	PacketManager_MakePacket_WEATHERCHANGE(obuf, 0xFE, (uint8_t)g_WeatherManager.season, (uint8_t)temp);
	Entity_BroadcastPacket(entity, entity->serial, obuf);
}
