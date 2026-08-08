/*
 * GM edit mode - in-game editor for world tiles, items, and mobiles.
 *
 * Players flagged as GameMaster can switch into edit mode to pick,
 * move, stamp, and retarget entities by clicking; requests flow back
 * through the same packet path as normal play but route through this
 * module's handlers instead of gameplay dispatch.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dat.h"

#include "dynamic.h"
#include "main.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "res.h"
#include "vtable.h"
#include "world.h"

/*
 * 0x004D64E0 - SendReadOnlyError
 *
 * Sends an "Access Denied: <name> is read-only." system message to a
 * player. Used by GM editor delegates when the target data file is not
 * loaded.
 */
void
SendReadOnlyError(CPlayer *player, const char *name)
{
	char text[256];
	uint8_t obuf[0x42C];

	sprintf(text, "Access Denied: %s is read-only.", name);
	PacketManager_MakePacket_TEXT(obuf, NULL, (CItem *)&player->mobile.container.item, 6, text, 0x3B2, 0);
	SendToClient((CItem *)&player->mobile.container.item, obuf, -1);
}
/*
 * 0x004D653F - SetTerrainTile
 *
 * GM editor delegate: writes a terrain cell's tile ID and/or Z at
 * (x, y). -666 is the "no change" sentinel for both fields. Marks
 * the block dirty and either broadcasts or defers the update.
 */
int
SetTerrainTile(intptr_t mode, int x, int y, int tileID, int elevation)
{
	int blockIdx;
	uint8_t pkt[0xD0];

	if (!g_TerrainDataLoaded) {
		if (mode == 0)
			return 0;
		SendReadOnlyError((CPlayer *)mode, "Terrain");
		return 0;
	}

	CBlockManager_Lock(&g_SpatialGrid);

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);

	if (tileID != (int)0xFFFFFD66)
		g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].tileID = (uint16_t)tileID;

	if (elevation != (int)0xFFFFFD66)
		g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].z = (int8_t)elevation;

	g_SpatialGrid.cells[blockIdx].flags111 |= 0x01;

	MapFileManager_SeekBlock(g_PoolBaseField_CC + blockIdx);

	if (g_UpdatesEnabled) {
		PacketManager_MakePacket_UPD_TERRCHUNK(pkt, blockIdx);
		MapFileManager_WriteBlock(g_PoolBaseField_CC + blockIdx, pkt, GetPacketOffset(pkt) & 0xFFFF);
	} else {
		g_SpatialGrid.cells[blockIdx].flags111 |= 0x02;
	}

	CBlockManager_Unlock(&g_SpatialGrid);

	return 1;
}

/*
 * 0x004D66CD - SetTerrSingle
 *
 * Sets terrain tile at a single world coordinate. Independent
 * implementation from SetTerrainTile (0x004D653F) - uses
 * MapFileManager_FlushBlock instead of MapFileManager_WriteBlock.
 */
int
SetTerrSingle(intptr_t mode, int x, int y, int tileID, int elevation)
{
	int blockIdx;
	uint8_t pkt[0xD0];

	if (!g_TerrainDataLoaded) {
		if (mode == 0)
			return 0;
		SendReadOnlyError((CPlayer *)mode, "Terrain");
		return 0;
	}

	CBlockManager_Lock(&g_SpatialGrid);

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);

	if (tileID != (int)0xFFFFFD66)
		g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].tileID = (uint16_t)tileID;

	if (elevation != (int)0xFFFFFD66)
		g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].z = (int8_t)elevation;

	g_SpatialGrid.cells[blockIdx].flags111 |= 0x01;

	MapFileManager_SeekBlock(g_PoolBaseField_CC + blockIdx);

	if (g_UpdatesEnabled) {
		PacketManager_MakePacket_UPD_TERRCHUNK(pkt, blockIdx);
		MapFileManager_FlushBlock(g_PoolBaseField_CC + blockIdx);
	} else {
		g_SpatialGrid.cells[blockIdx].flags111 |= 0x02;
	}

	CBlockManager_Unlock(&g_SpatialGrid);

	return 1;
}

/*
 * 0x004D6841 - StoreHues
 *
 * GM editor delegate for packet 0x49 (NEW_HUES). Writes the new hue
 * into g_HueDataExp, rebuilds the disk-format group buffer, and
 * flushes it via MapFileManager.
 *
 * Binary quirk: writes go to g_HueDataExp but the group buffer is
 * rebuilt from g_HueData, so the new colors are never persisted.
 * Reproduced exactly.
 */
int
StoreHues(CPlayer *player, int hueIndex, uint16_t *colors, uint16_t startColor, uint16_t endColor, char *name)
{
	int i, j;
	int groupBase;
	int groupStart;
	int writeOff;
	uint8_t groupBuf[0x2C0];
	uint8_t packetBuf[0x2CC];
	uint16_t packetLen;

	if (!g_HueDataLoaded) {
		SendReadOnlyError(player, "Hues");
		return 0;
	}

	groupBase = hueIndex / 8;
	MapFileManager_SeekBlock(g_HueFileOffset + groupBase);

	CDataManager_LockHueData();

	for (i = 0; i < 0x20; i++)
		g_HueDataExp[hueIndex].colors[i] = colors[i];

	g_HueDataExp[hueIndex].startColor = startColor;
	g_HueDataExp[hueIndex].endColor = endColor;

	strcpy(g_HueDataExp[hueIndex].name, name);

	CDataManager_UnlockHueData();

	memset(groupBuf, 0, 0x2C0);
	writeOff = 0;
	groupStart = groupBase * 8;
	for (i = groupStart; i < groupBase * 8 + 8; i++) {
		for (j = 0; j < 0x20; j++) {
			*(uint16_t *)(groupBuf + writeOff) = g_HueData[i].colors[j];
			writeOff += 2;
		}
		*(uint16_t *)(groupBuf + writeOff) = g_HueData[i].startColor;
		writeOff += 2;
		*(uint16_t *)(groupBuf + writeOff) = g_HueData[i].endColor;
		writeOff += 2;
		strcpy((char *)(groupBuf + writeOff), g_HueData[i].name);
		writeOff += 0x14;
	}

	PacketManager_MakePacket_HUEDATA(packetBuf, groupBase);
	packetLen = GetPacketOffset(packetBuf) & 0xFFFF;

	MapFileManager_WriteBlock(g_HueFileOffset + groupBase, packetBuf, packetLen);

	return 1;
}

/*
 * 0x004D6AF6 - ArtTile_WriteEntry
 *
 * GM editor delegate for art tile writes (packets 0x44 DESTROY_ART and
 * 0x46 NEW_ART). Rewrites the artidx.mul / art.mul entry for artId.
 * g_ArtLoaded is never set in the binary, so this always bails with
 * "Art" read-only in practice.
 */
int
ArtTile_WriteEntry(CPlayer *player, int artId, int dataLen, uint8_t *data)
{
	CIndexedFileManager fm;
	uint8_t buf[0x10010];
	uint16_t packetLen;

	CIndexedFileManager_Constructor(&fm);

	if (!g_ArtLoaded) {
		SendReadOnlyError(player, "Art");
		CIndexedFileManager_Destructor(&fm);
		return 0;
	}

	MapFileManager_SeekBlock(g_PoolBaseItems + artId);

	CIndexedFileManager_Open(&fm, "../.rundir/artidx.mul", "../.rundir/art.mul", "r+b");

	CIndexedFileManager_WriteBlock(&fm, artId, data, dataLen, 0);

	CIndexedFileManager_Close(&fm);

	// Binary: GetPacketOffset on uninitialized local buffer, result passed
	// to MapFileManager_WriteBlock (no-op). Reproduced exactly.
	packetLen = GetPacketOffset(buf) & 0xFFFF;
	MapFileManager_WriteBlock(g_PoolBaseItems + artId, buf, packetLen);

	CIndexedFileManager_Destructor(&fm);
	return 1;
}

/*
 * 0x004D6C1F - StoreLandTileData
 *
 * GM editor delegate for packet 0x45 (NEW_TERRAIN). Overwrites a
 * g_LandTileData entry and rewrites the enclosing 32-entry group.
 */
int
StoreLandTileData(CPlayer *player, int tileIndex, uint32_t flags, uint16_t textureID, char *name)
{
	int groupBase;
	uint8_t packetBuf[0x50C];
	uint16_t packetLen;

	if (!g_TileDataLoaded) {
		SendReadOnlyError(player, "Terrain TileData");
		return 0;
	}

	CDataManager_LockTileData();

	g_LandTileData[tileIndex].flags = flags;
	g_LandTileData[tileIndex].textureID = textureID;
	strcpy(g_LandTileData[tileIndex].name, name);

	CDataManager_UnlockTileData();

	groupBase = tileIndex / 32;
	MapFileManager_SeekBlock(g_TileDataFileOffset + groupBase);

	PacketManager_MakePacket_TILEDATA(packetBuf, groupBase);
	packetLen = GetPacketOffset(packetBuf) & 0xFFFF;

	MapFileManager_WriteBlock(g_TileDataFileOffset + groupBase, packetBuf, packetLen);

	return 1;
}

/*
 * 0x004D6D1B - StoreItemTileData
 *
 * GM editor delegate for packet 0x46 (NEW_ART) item-tile mode.
 * Overwrites a g_ItemTileData entry and rewrites the enclosing
 * 32-entry group (offset by 0x200 from land-tile groups).
 */
int
StoreItemTileData(CPlayer *player, int itemIndex, uint32_t flags, uint8_t weight, uint8_t layer, uint32_t miscData, uint16_t value1, uint16_t value2, uint16_t height,
        uint8_t quantity, char *name)
{
	int groupBase;
	uint8_t packetBuf[0x50C];
	uint16_t packetLen;

	if (!g_TileDataLoaded) {
		SendReadOnlyError(player, "Item TileData");
		return 0;
	}

	CDataManager_LockTileData();

	g_ItemTileData[itemIndex].flags = flags;
	g_ItemTileData[itemIndex].weight = weight;
	g_ItemTileData[itemIndex].layer = layer;
	g_ItemTileData[itemIndex].miscData = miscData;
	g_ItemTileData[itemIndex].value1 = value1;
	g_ItemTileData[itemIndex].value2 = value2;
	g_ItemTileData[itemIndex].height = height;
	g_ItemTileData[itemIndex].quantity = quantity;
	strcpy(g_ItemTileData[itemIndex].name, name);

	CDataManager_UnlockTileData();

	groupBase = itemIndex / 32 + 0x200;
	MapFileManager_SeekBlock(g_TileDataFileOffset + groupBase);

	PacketManager_MakePacket_TILEDATA(packetBuf, groupBase);
	packetLen = GetPacketOffset(packetBuf) & 0xFFFF;

	MapFileManager_WriteBlock(g_TileDataFileOffset + groupBase, packetBuf, packetLen);

	return 1;
}

/*
 * 0x004D6E92 - AnimData_UpdateEntry
 *
 * GM editor delegate for packet 0x48 (NEW_ANIM). Overwrites one
 * g_AnimData entry (64 frame bytes + 4 timing bytes) and rewrites
 * the enclosing 8-entry group via packet 0x43.
 */
int
AnimData_UpdateEntry(CPlayer *player, int animIndex, uint8_t *data)
{
	int i, j;
	int groupBase;
	int groupStart;
	int writeOff;
	uint8_t groupBuf[0x220];
	uint8_t packetBuf[0x22C];
	uint16_t packetLen;

	if (!g_AnimDataLoaded) {
		SendReadOnlyError(player, "AnimData");
		return 0;
	}

	groupBase = animIndex / 8;
	MapFileManager_SeekBlock(g_PoolBaseField_E4 + groupBase);

	CDataManager_LockAnimData();

	for (j = 0; j < 0x40; j++) {
		g_AnimData[animIndex].frameData[j] = *data;
		data++;
	}

	g_AnimData[animIndex].unk40 = *data;
	data++;
	g_AnimData[animIndex].frameCount = *data;
	data++;
	g_AnimData[animIndex].frameInterval = *data;
	data++;
	g_AnimData[animIndex].startInterval = *data;
	data++;

	CDataManager_UnlockAnimData();

	writeOff = 0;
	groupStart = groupBase * 8;
	for (i = groupStart; i < groupBase * 8 + 8; i++) {
		for (j = 0; j < 0x40; j++) {
			groupBuf[writeOff] = g_AnimData[i].frameData[j];
			writeOff++;
		}
		groupBuf[writeOff] = g_AnimData[i].unk40;
		writeOff++;
		groupBuf[writeOff] = g_AnimData[i].frameCount;
		writeOff++;
		groupBuf[writeOff] = g_AnimData[i].frameInterval;
		writeOff++;
		groupBuf[writeOff] = g_AnimData[i].startInterval;
		writeOff++;
	}

	PacketManager_MakePacket_NEW_ANIMDATA(packetBuf, groupBase, 0, groupBuf);
	packetLen = GetPacketOffset(packetBuf) & 0xFFFF;

	MapFileManager_WriteBlock(g_PoolBaseField_E4 + groupBase, packetBuf, packetLen);

	return 1;
}

/*
 * 0x004D716B - MapBlock_ReadEntry
 *
 * GM editor delegate for region updates. Registers the region via
 * RegionManager_AddRegion and flushes the matching map block.
 * The player argument is passed by the binary but never used.
 */
int
MapBlock_ReadEntry(CPlayer *player, CRegion *region)
{
	CSearchCtx ctx;
	CSearchCtx output;
	uint8_t packetBuf[0x70];
	uint16_t packetLen;

	USED(player);

	MapFileManager_SeekBlock(g_PoolBaseField_EC + region->prefix);

	RegionManager_AddRegion(region);

	CSearchCtx_Constructor(&ctx);
	CSearchCtx_Add(&ctx, CResList_BeginIter_ByFile(&g_RegionByFileRM, &output, &region->prefix, 1));

	if (CSearchCtx_Find(&ctx)) {
		CRegion *found = CResManager_GetResultCtx(&g_RegionByFileRM, &ctx);
		PacketManager_MakePacket_UPD_REGIONS(packetBuf, found);
		packetLen = GetPacketOffset(packetBuf) & 0xFFFF;
		MapFileManager_WriteBlock(g_PoolBaseField_EC + region->prefix, packetBuf, packetLen);
	}

	return 1;
}

/*
 * 0x004D721C - World_SuppressUpdates
 *
 * Bumps the update-suppress ref-count and disables entity update
 * broadcasts. Paired with World_RestoreUpdates.
 */
void
World_SuppressUpdates(void)
{
	g_UpdatesSuppressCount = g_UpdatesSuppressCount + 1;
	g_UpdatesEnabled = 0;
}

/*
 * 0x004D7238 - World_RestoreUpdates
 *
 * Drops the update-suppress ref-count. When it reaches zero,
 * flushes deferred-dirty blocks (bits 0x02 / 0x04 in flags111) and
 * broadcasts a RESTARTVER packet to all players.
 */
void
World_RestoreUpdates(void)
{
	int i;
	uint8_t buf[4];

	g_UpdatesSuppressCount = g_UpdatesSuppressCount - 1;
	if (g_UpdatesSuppressCount != 0)
		return;

	g_UpdatesEnabled = 1;

	CBlockManager_Lock(&g_SpatialGrid);

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		CBlock *blk = &g_SpatialGrid.cells[i];

		if (blk->flags111 & 0x04) {
			blk->flags111 = (blk->flags111 & ~0x04) | 0x01;
			MapFileManager_SeekBlock(g_PoolBaseField_FC + i);
			MapFileManager_FlushBlock(g_PoolBaseField_FC + i);
		}

		if (blk->flags111 & 0x02) {
			blk->flags111 = (blk->flags111 & ~0x02) | 0x01;
			MapFileManager_SeekBlock(g_PoolBaseField_CC + i);
			MapFileManager_FlushBlock(g_PoolBaseField_CC + i);
		}
	}

	CBlockManager_Unlock(&g_SpatialGrid);

	PacketManager_MakePacket_RESTARTVER(buf);

	CPlayerList_SendPacketToAll(buf);
}

/*
 * 0x004D73B0 - Player_StepInDirection
 *
 * Moves a player entity one tile in the given direction, gated by
 * the virtual WalkCheck hook.
 */
void
Player_StepInDirection(CItem *entity, int direction)
{
	CLocation outLoc;
	CItem *player;

	CLocation_Init(&outLoc);

	if (!VT_IsPlayer(entity))
		return;

	player = entity;
	if (!((int (*)(void *, int, CLocation *))VT_FN(player, VT_WALK_CHECK))(player, direction, &outLoc))
		return;

	DoMove(player, direction, 0, 0);
}
