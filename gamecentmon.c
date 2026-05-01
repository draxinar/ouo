/*
 * GameCentral monitor - administrative command channel.
 *
 * Accepts connections from the GameCentral admin tool, authenticates
 * the operator, and dispatches its commands (kick, broadcast, shutdown,
 * login gates) into the game loop via channel queues.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dat.h"

#include "egg.h"
#include "gamecentmon.h"
#include "io.h"
#include "log.h"
#include "main.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_utils.h"
#include "player.h"
#include "region.h"
#include "template.h"
#include "time.h"
#include "vtable.h"
#include "world.h"

__extension__ typedef struct GCMEntityListBuilder GCMEntityListBuilder;

static void GameCentMon_ResetDecay(void); // 0x004B8A7C
static void GameCentMon_BuildEntityList(GCMEntityListBuilder *builder, char *responseBuf, uint32_t serial); // 0x004B8992
static void GameCentMon_FlushEntityList(GCMEntityListBuilder *builder); // 0x004B88F7
static void GameCentMon_WriteEntityData(GCMEntityListBuilder *builder, CItem *entity); // 0x004B878D
static void GameCentMon_BroadcastAll(char *data, int len); // 0x004B83E3
static void GameCentMon_SetVersion(uint32_t ver); // 0x004B83D6
static void GameCentMon_UpdateTimestamp(CPlayer *player); // 0x004B7B24
static void GameCentMon_SelectTarget(CPlayer *player, int index); // 0x004B7AAD
static void GameCentMon_DeselectTarget(CPlayer *player); // 0x004B7A0D
static void BuildGameCentMonResponse(uint8_t *buf, uint8_t subtype, uint16_t flag, char *data, uint16_t datalen); // 0x004B795D

// GCMEntityListBuilder struct
__extension__ typedef struct GCMEntityListBuilder {
	char *responseBuf;  // 0x00 - response buffer
	int count;          // 0x04 - number of entries written
	uint32_t serial;    // 0x08 - target player serial
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;    // Custom: 64-bit - align pointer to 8-byte boundary
#endif
	char *cursor;       // 0x0C - current write position
} GCMEntityListBuilder;

// Forward declarations for static functions
static void GameCentMon_BuildEntityList(GCMEntityListBuilder *builder, char *responseBuf, uint32_t serial);

/*
 * 0x004B37D1 - GameCentMon_TickAllNPCs
 *
 * Deletes every non-player entity in the NPC hash.
 */
void
GameCentMon_TickAllNPCs(void)
{
	int i;
	CNPC *npc, *next;

	for (i = 0; i <= 0x3F; i++) {
		npc = g_NPCHash[i];
		while (npc != NULL) {
			next = npc->npcHashNext;
			if (!VT_IsPlayer((CItem *)npc)) {
				if (npc != NULL) {
					((void (*)(void *))VT_FN((CItem *)npc, VT_DELETE))((CItem *)npc);
				}
			}
			npc = next;
		}
	}
}
/*
 * 0x004B3AC9 - GameCentMon_TickTemplateChains
 *
 * Deletes every entity in the given template chain. Returns for
 * out-of-range ids (>= 0x1000).
 */
void
GameCentMon_TickTemplateChains(int templateId)
{
	CItem *entity;

	if (templateId >= 0x1000)
		return;

	for (;;) {
		if (g_TemplateChain[templateId] == NULL)
			return;
		if (g_TemplateChain[templateId] != NULL) {
			entity = g_TemplateChain[templateId];
			((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
		}
	}
}

/*
 * 0x004B795D - BuildGameCentMonResponse
 *
 * Builds a GameCentMon response packet (0x96): subtype/flag/datalen
 * header followed by the payload string.
 */
static void
BuildGameCentMonResponse(uint8_t *buf, uint8_t subtype, uint16_t flag, char *data, uint16_t datalen)
{
	PutPacketType(buf, 0x96, 0x2008);
	PutByte(buf, subtype);
	PutWord(buf, flag);
	PutWord(buf, datalen);
	PutString(buf, data, datalen);
}

/*
 * 0x004B79BE - GameCentMon_CalcZ
 *
 * Returns terrain Z at (x, y), or 0 if off-map.
 */
int
GameCentMon_CalcZ(int x, int y)
{
	int blockIndex;

	blockIndex = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	if (blockIndex < 0)
		return 0;
	return (int)g_MapBlocks[blockIndex].cells[(y & 7) * 8 + (x & 7)].z;
}

/*
 * 0x004B7A0D - GameCentMon_DeselectTarget
 *
 * Sends the deselect response to the player and to the stored target
 * (if different), clears g_gcmTarget, and broadcasts event {0, 3, 0}.
 */
static void
GameCentMon_DeselectTarget(CPlayer *player)
{
	uint8_t responseBuf[0x2014];
	char dataBuf[0x2000];
	int32_t broadcastBuf[3];

	if (player != NULL) {
		BuildGameCentMonResponse(responseBuf, 8, 0, dataBuf, 1);
		SendToClient((CItem *)player, responseBuf, -1);
	}

	if (g_gcmTarget != NULL && g_gcmTarget != (CItem *)player) {
		SendToClient(g_gcmTarget, responseBuf, -1);
	}

	g_gcmTarget = NULL;

	broadcastBuf[0] = 0;
	broadcastBuf[1] = 3;
	broadcastBuf[2] = 0;
	GameCentMon_BroadcastEvent((char *)broadcastBuf, 0xC);
}

/*
 * 0x004B7AAD - GameCentMon_SelectTarget
 *
 * Starts monitoring the given player. If a monitor is already active
 * (gcmState != 0) defers to DeselectTarget instead.
 */
static void
GameCentMon_SelectTarget(CPlayer *player, int index)
{
	uint8_t responseBuf[0x2008];

	USED(index);

	if (g_gcmState != 0) {
		GameCentMon_DeselectTarget(player);
		return;
	}

	g_gcmTarget = (CItem *)player;
	g_gcmState = 1;

	BuildGameCentMonResponse(responseBuf, 8, 1, (char *)(responseBuf + 8), 1);
	SendToClient((CItem *)player, responseBuf, -1);

	g_gcmTimestamp = GetTickCount_UO();
}

/*
 * 0x004B7B24 - GameCentMon_UpdateTimestamp
 *
 * Refreshes the monitor timestamp if the given player is the active
 * target.
 */
static void
GameCentMon_UpdateTimestamp(CPlayer *player)
{
	if (g_gcmState != 1)
		return;
	if (g_gcmTarget != (CItem *)player)
		return;
	g_gcmTimestamp = GetTickCount_UO();
}

/*
 * 0x004B7B47 - HandlePacket_GameCentMon
 *
 * Handles GameCentMon client packets (0x96). Subtype 0x11 streams
 * bankdefs.txt back in 8000-byte chunks, 0x02 is a GM teleport, 0x08
 * selects or deselects the monitored target; everything else fans out
 * to a broadcast event. Binary dispatch is a two-level jump table at
 * 0x004B822F / 0x004B8203.
 */
void
HandlePacket_GameCentMon(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t subtype;
	uint16_t param1, dataLen;
	char *data;
	char *dataPtr;
	uint8_t responseBuf[0x2008];
	char localBuf[0x2030];
	int switchIndex;
	int32_t broadcastBuf[3];
	int numDwords, i;
	char *readCur, *writeCur;
	int16_t teleX, teleY;
	CLocation teleLoc;
	FILE *fp;
	char *fileData;
	int32_t fileSize;
	int32_t offset;
	uint8_t savedByte;

	off = 0;
	GetByte(buf, &off, &subtype);
	GetWord(buf, &off, &param1);
	GetWord(buf, &off, &dataLen);
	GetString(buf, &off, &data, dataLen);

	dataPtr = data;
	dataPtr = data;

	GameCentMon_UpdateTimestamp(this);

	if ((subtype & 0xFF) == 0x11) {
		CString pathStr;

		CString_Constructor(&pathStr, "../.rundir/bankdefs.txt");
		fp = fopen_ServerSide(CString_GetBuffer(&pathStr), "r");
		if (fp == NULL) {
			BuildGameCentMonResponse(responseBuf, subtype, 0, localBuf, 1);
			SendToClient((CItem *)this, responseBuf, -1);
			CString_Destructor(&pathStr);
			goto gcm_done;
		}

		fseek_ServerSide(fp, 0, 2);
		fileSize = ftell_ServerSide(fp);
		fileData = NULL;

		if (fileSize == 0) {
			BuildGameCentMonResponse(responseBuf, subtype, 0, localBuf, 1);
			SendToClient((CItem *)this, responseBuf, -1);
			CString_Destructor(&pathStr);
			goto gcm_done;
		}

		fseek_ServerSide(fp, 0, 0);

		fileData = (char *)OperatorNew(fileSize + 1);

		fread_ServerSide(fileData, 1, fileSize, fp);

		fclose_ServerSide(fp);

		fileData[fileSize] = '\0';
		fileSize = fileSize + 1;

		offset = 0;
		while (fileSize > 0x1F40) {
			offset += 0x1F40;
			fileSize -= 0x1F40;

			savedByte = (uint8_t)fileData[offset];
			fileData[offset] = '\0';

			// flag 1 = more chunks follow.
			BuildGameCentMonResponse(responseBuf, subtype, 1, fileData + offset - 0x1F40, 0x1F41);
			SendToClient((CItem *)this, responseBuf, -1);

			fileData[offset] = (char)savedByte;
		}

		// flag 2 = final chunk.
		BuildGameCentMonResponse(responseBuf, subtype, 2, fileData + offset, (uint16_t)fileSize);
		SendToClient((CItem *)this, responseBuf, -1);

		OperatorDelete(fileData);
		CString_Destructor(&pathStr);
		goto gcm_done;
	}

	switchIndex = (subtype & 0xFF);
	switchIndex = switchIndex - 1;
	if ((uint32_t)switchIndex > 0x11)
		goto gcm_done;

	{
		static const uint8_t byteTable[18] = { 0, 1, 2, 2, 2, 2, 2, 3, 4, 10, 5, 10, 6, 10, 7, 8, 10, 9 };
		int caseIndex = byteTable[switchIndex];

		switch (caseIndex) {
		case 9:
			broadcastBuf[0] = (int32_t)this->mobile.container.item.serial;
			broadcastBuf[1] = 9;
			broadcastBuf[2] = 0;
			GameCentMon_BroadcastEvent((char *)broadcastBuf, 0xC);
			break;

		case 4:
			broadcastBuf[0] = (int32_t)this->mobile.container.item.serial;
			broadcastBuf[1] = 1;
			broadcastBuf[2] = 0;
			GameCentMon_BroadcastEvent((char *)broadcastBuf, 0xC);
			break;

		case 8:
			broadcastBuf[0] = (int32_t)this->mobile.container.item.serial;
			broadcastBuf[1] = 8;
			broadcastBuf[2] = 0;
			GameCentMon_BroadcastEvent((char *)broadcastBuf, 0xC);
			break;

		case 7:
			broadcastBuf[0] = (int32_t)this->mobile.container.item.serial;
			broadcastBuf[1] = 7;
			broadcastBuf[2] = 0;
			GameCentMon_BroadcastEvent((char *)broadcastBuf, 0xC);
			break;

		case 6: {
			numDwords = (int)(dataLen & 0xFFFF) / 4;
			readCur = dataPtr;
			writeCur = dataPtr;
			for (i = 0; i < numDwords; i++) {
				int32_t val = ReadInt32LE(&readCur);
				*(int32_t *)writeCur = val;
				writeCur += 4;
			}
			*(int32_t *)dataPtr = (int32_t)this->mobile.container.item.serial;
			*(int32_t *)(dataPtr + 4) = 4;
			*(int32_t *)(dataPtr + 8) = 0;
			GameCentMon_BroadcastEvent(dataPtr, dataLen & 0xFFFF);
			break;
		}

		case 5:
			broadcastBuf[0] = (int32_t)this->mobile.container.item.serial;
			broadcastBuf[1] = 2;
			broadcastBuf[2] = 0;
			GameCentMon_BroadcastEvent((char *)broadcastBuf, 0xC);
			broadcastBuf[1] = 6;
			GameCentMon_BroadcastEvent((char *)broadcastBuf, 0xC);
			break;

		case 3:
			if ((param1 & 0xFFFF) == 0xFF) {
				GameCentMon_DeselectTarget(this);
			} else {
				GameCentMon_SelectTarget(this, param1 & 0xFFFF);
			}
			break;

		case 1: {
			readCur = data;
			teleX = ReadInt16LE(&readCur);
			teleY = ReadInt16LE(&readCur);

			CLocation_Init(&teleLoc);
			CLocation_Set(&teleLoc, teleX, teleY, (int16_t)GameCentMon_CalcZ(teleX & 0xFFFF, teleY & 0xFFFF));

			if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)teleLoc.x, (int)(int16_t)teleLoc.y)) {
				((void (*)(CItem *))VT_FN(&this->mobile.container.item, VT_HIDE))((CItem *)this);
				((void (*)(CItem *, CLocation *))VT_FN(&this->mobile.container.item, VT_DROP_AT_FEET))((CItem *)this, &teleLoc);
			}
			break;
		}

		case 2:
		case 0: {
			int32_t allBuf[3];
			allBuf[0] = (int32_t)this->mobile.container.item.serial;
			allBuf[1] = subtype & 0xFF;
			allBuf[2] = 0;
			GameCentMon_BroadcastAll((char *)allBuf, 0xC);
			break;
		}

		default:
			break;
		}
	}

gcm_done:;
	USED(localBuf);
	USED(dataPtr);
}

/*
 * 0x004B8241 - LoadAll_LVNData
 *
 * Resolves the LVN region name for the current map and logs an "I am"
 * identity line. The RegionManager search writes a city/dungeon name
 * (minus its prefix) into buf; the prefix up to NUL or '_' is copied
 * into g_LVNDestBuf and joined with hostname/ip for the log line.
 */
void
LoadAll_LVNData(void)
{
	char buf[256]; // var_10ch
	char *dest; // var_128h - advancing write pointer
	int len, i;
	CString str;

	dest = g_LVNDestBuf;
	*dest = '\0';
	strcpy(buf, "lvn");

	RegionManager_FindLVNRegion(&g_RegionRM, g_mapStartX, g_mapStartY, g_mapStartX + g_mapWidth, g_mapStartY + g_mapHeight, buf, 0x80);

	len = (int)strlen(buf);
	if (len > 0x7F)
		len = 0x7F;

	for (i = 0; i < len; i++) {
		if ((signed char)buf[i] == 0)
			break;
		if ((signed char)buf[i] == '_')
			break;
		*dest = buf[i];
		dest++;
	}
	*dest = '\0';

	CString_DefaultConstructor(&str);
	LoadAll_LVN_Hostname(&str);
	EventLogger_Log(&g_EventLogger, 0, 0, 0, "pvn", "name", "misc", CString_GetBuffer(&str));
	CString_Destructor(&str);
}

/*
 * 0x004B83D6 - GameCentMon_SetVersion
 *
 * Sets g_gcmVersion.
 */
static void
GameCentMon_SetVersion(uint32_t ver)
{
	g_gcmVersion = ver;
}

/*
 * 0x004B83E3 - GameCentMon_BroadcastAll
 *
 * Handles GCM "all" events. Event 1 replies with server status; events
 * 3-7 toggle spawn flags or tick NPCs; event 2 and unknown replies with
 * an empty response.
 */
static void
GameCentMon_BroadcastAll(char *data, int len)
{
	char *cursor;
	uint32_t serial;
	int32_t eventType;
	char buf[0x2020];
	char *cursorOut;
	char *cursorSave;
	uint16_t flag;
	uint8_t responseBuf[0x2014];
	CItem *entity;
	uint16_t dataLen;

	USED(len);

	cursor = data;
	serial = *(uint32_t *)cursor;
	eventType = *(int32_t *)(cursor + 4);

	cursorOut = buf;
	cursorSave = buf;
	flag = 1;

	switch (eventType - 1) {
	case 2: // Event 3: enable spawning.
		g_SpawnEnabled = 1;
		return;

	case 3: // Event 4: disable spawning.
		g_SpawnEnabled = 0;
		return;

	case 4: // Event 5: clear initial-spawn flag.
		g_IsInitialSpawn = 0;
		return;

	case 5: // Event 6: set initial-spawn flag.
		g_IsInitialSpawn = 1;
		return;

	case 6: // Event 7: tick all NPCs.
		GameCentMon_TickAllNPCs();
		return;

	case 0: { // Event 1: server status response.
		uint16_t pcTickCount;
		uint16_t timingVal1;
		uint16_t timingVal2;
		uint16_t playerCount;
		CPlayer *p;

		eventType = 0;
		GameCentMon_SetVersion(*(uint32_t *)(cursor + 8));

		*(uint8_t *)cursorOut = (uint8_t)g_gcmVersion;
		cursorOut++;

		*(uint8_t *)cursorOut = (uint8_t)g_SpawnEnabled;
		cursorOut++;

		*(uint8_t *)cursorOut = (uint8_t)g_IsInitialSpawn;
		cursorOut++;

		{
			int nameLen = strlen(g_LVNDestBuf) + 1;
			*(uint8_t *)cursorOut = (uint8_t)nameLen;
			cursorOut++;
			memcpy(cursorOut, g_LVNDestBuf, nameLen);
			cursorOut += nameLen;
		}

		pcTickCount = (uint16_t)GameCentMon_GetPlayerCount();

		timingVal1 = (uint16_t)g_TimingField9EC;
		if ((int32_t)g_TimingField9EC > 0xFFFF)
			timingVal1 = 0xFFFF;

		timingVal2 = (uint16_t)g_TimingField9F0;
		if ((int32_t)g_TimingField9F0 > 0xFFFF)
			timingVal2 = 0xFFFF;

		cursorSave = cursorOut;
		playerCount = 0;
		for (p = g_PlayerList.head; p != NULL; p = p->next) {
			if (p->mobile.container.item.resourceEntity.entity.removedFromWorld != 0)
				continue;
			playerCount++;
		}

		WriteInt16LE(&cursorSave, (int16_t)playerCount);
		WriteInt16LE(&cursorSave, (int16_t)g_NPCCount);
		WriteInt16LE(&cursorSave, (int16_t)g_mapStartX);
		WriteInt16LE(&cursorSave, (int16_t)g_mapStartY);
		{
			int32_t mapEndX = g_mapStartX + g_mapWidth;
			WriteInt16LE(&cursorSave, (int16_t)mapEndX);
		}
		{
			int32_t mapEndY = g_mapStartY + g_mapHeight;
			WriteInt16LE(&cursorSave, (int16_t)mapEndY);
		}
		WriteInt16LE(&cursorSave, (int16_t)pcTickCount);
		WriteInt16LE(&cursorSave, (int16_t)timingVal1);
		WriteInt16LE(&cursorSave, (int16_t)timingVal2);

		cursorOut = cursorSave;
		break;
	}

	case 1:
	default:
		break;
	}

	dataLen = cursorOut - buf;
	BuildGameCentMonResponse(responseBuf, (uint8_t)eventType, flag, buf, dataLen);

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		return;
	if (!VT_IsPlayer(entity))
		return;
	SendToClient(entity, responseBuf, -1);
}

/*
 * 0x004B8750 - GameCentMon_CheckTimeout
 *
 * Deselects the monitored target after 10 min of inactivity (or on
 * tick-count wraparound).
 */
void
GameCentMon_CheckTimeout(void)
{
	int32_t elapsed;

	if (g_gcmState != 1)
		return;

	elapsed = (int32_t)(GetTickCount_UO() - g_gcmTimestamp);
	if (elapsed < 0 || elapsed > 0x927C0)
		GameCentMon_DeselectTarget((CPlayer *)g_gcmTarget);
}

/*
 * 0x004B878D - GameCentMon_WriteEntityData
 *
 * Emits a 12-byte resource record for every type==3 node on entity
 * (layout: two zero words, id, skip word, value3), flushing at 500
 * records. Then recurses into container contents and 30-slot mobile
 * equipment.
 */
static void
GameCentMon_WriteEntityData(GCMEntityListBuilder *builder, CItem *entity)
{
	CResourceNode *node;
	CItem *child;
	int i;

	if (entity == NULL)
		return;

	for (node = entity->resourceEntity.firstChild; node != NULL; node = node->next) {
		char *localCursor;
		char *localCursor2;

		if (node->id == 0)
			continue;
		if ((int8_t)node->type != 3)
			continue;

		localCursor = builder->cursor;

		WriteInt16LE(&localCursor, 0);
		WriteInt16LE(&localCursor, 0);
		WriteInt16LE(&localCursor, (int16_t)node->id);
		localCursor += 2;

		localCursor2 = localCursor;
		WriteInt32LE(&localCursor2, node->value3);

		builder->cursor += 0x0C;
		builder->count++;

		if (builder->count >= 0x1F4)
			GameCentMon_FlushEntityList(builder);
	}

	if (VT_IsMobile2(entity)) {
		for (child = ((CContainer *)entity)->contents; child != NULL; child = child->spatialNext) {
			GameCentMon_WriteEntityData(builder, child);
		}
	}

	if (VT_IsMobile(entity)) {
		CMobile *mob = (CMobile *)entity;
		for (i = 0; i < 0x1E; i++) {
			GameCentMon_WriteEntityData(builder, mob->equipment[i]);
		}
	}
}

/*
 * 0x004B88F7 - GameCentMon_FlushEntityList
 *
 * Sends the accumulated records as a 0x0B response and resets the
 * builder. Sleeps 100 ms after the send to throttle the client.
 */
static void
GameCentMon_FlushEntityList(GCMEntityListBuilder *builder)
{
	int dataLen;
	CItem *entity;

	dataLen = builder->count * 0x0C;

	BuildGameCentMonResponse((uint8_t *)builder->responseBuf, 0x0B, (uint16_t)builder->count, builder->responseBuf + 8, dataLen);

	entity = CWorld_FindBySerial(g_World, builder->serial);
	if (entity != NULL) {
		if (VT_IsPlayer(entity)) {
			SendToClient(entity, (uint8_t *)builder->responseBuf, -1);
			{
				struct timespec ts = { 0, 100 * 1000000 };
				nanosleep(&ts, NULL);
			}
		}
	}

	builder->count = 0;
	builder->cursor = builder->responseBuf + 8;
}

/*
 * 0x004B8992 - GameCentMon_BuildEntityList
 *
 * Walks every spatial grid cell then the player list (matching on the
 * 0x00010000 pflags monitor bit and removedFromWorld != 0), emitting
 * resource records via GameCentMon_WriteEntityData. Sends a final
 * flush for any leftover records.
 */
static void
GameCentMon_BuildEntityList(GCMEntityListBuilder *builder, char *responseBuf, uint32_t serial)
{
	int i;
	CItem *entity;
	CPlayer *player;

	builder->responseBuf = responseBuf;
	builder->serial = serial;
	builder->count = 0;
	builder->cursor = responseBuf + 8;

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		MapBlock *block = &g_MapBlocks[i];
		entity = block->itemHead;
		for (; entity != NULL; entity = entity->spatialNext) {
			GameCentMon_WriteEntityData(builder, entity);
		}
	}

	for (player = g_PlayerList.head; player != NULL; player = player->next) {
		if (!(player->pflags & 0x10000))
			continue;
		if (player->mobile.container.item.resourceEntity.entity.removedFromWorld == 0)
			continue;
		GameCentMon_WriteEntityData(builder, (CItem *)player);
	}

	if (builder->count > 0)
		GameCentMon_FlushEntityList(builder);
}

/*
 * 0x004B8A7C - GameCentMon_ResetDecay
 *
 * Resets decay counters on every item-type entity in the world hash
 * (vtable classifications 2, 4, 8, 0x1000) and runs one DecayTick.
 */
static void
GameCentMon_ResetDecay(void)
{
	int i;
	CItem *entity, *next;
	int classification;

	for (i = 0; i < 0x10000; i++) {
		entity = g_World->hashTable[i];
		while (entity != NULL) {
			next = entity->hashNext;
			classification = CItem_ClassifyEntityByVtable(entity);
			if (classification == 8) {
				CItem_SetDecayCount(entity, CItem_GetGlobalDecayMax(entity));
				CItem_DecayTick(entity);
			} else if (classification <= 8) {
				if (classification == 2 || classification == 4) {
					CItem_SetDecayCount(entity, CItem_GetGlobalDecayMax(entity));
					CItem_DecayTick(entity);
				}
			} else if (classification == 0x1000) {
				CItem_SetDecayCount(entity, CItem_GetGlobalDecayMax(entity));
				CItem_DecayTick(entity);
			}
			entity = next;
		}
	}
}

/*
 * 0x004B8B25 - GameCentMon_BroadcastEvent
 *
 * Handles the 11 GCM broadcast events: resource region updates,
 * template reloads, spawn-entry rebuild, decay reset, and entity
 * list queries. Dispatches on eventType-1.
 */
void
GameCentMon_BroadcastEvent(char *data, int len)
{
	char *cursor;
	uint32_t serial;
	int32_t eventType;
	int32_t thirdArg;
	char buf[0x2024];
	char *cursorOut;
	char *savedCursorOut;
	uint32_t flag;
	uint8_t responseBuf[0x2014];
	CItem *entity;
	uint16_t dataLen;

	USED(len);

	cursor = data;
	serial = *(uint32_t *)cursor;
	cursor += 4;
	eventType = *(int32_t *)cursor;
	cursor += 4;
	thirdArg = *(int32_t *)cursor;
	cursor += 4;

	cursorOut = buf;
	savedCursorOut = buf;
	flag = 0;

	switch (eventType - 1) {
	case 10: { // Event 11: set NPC AI timer reset.
		uint32_t val = *(uint32_t *)cursor;
		cursor += 4;
		g_npcAITimerReset = val;
		break;
	}

	case 9: { // Event 10: tick template chain.
		int32_t templateId = *(int32_t *)cursor;
		cursor += 4;
		GameCentMon_TickTemplateChains(templateId);
		break;
	}

	case 8: // Event 9: reload templates.
		CTemplateManager_Shutdown();
		CTemplateManager_startup();
		break;

	case 7: // Event 8: reset decay.
		GameCentMon_ResetDecay();
		break;

	case 6: // Event 7: rebuild spawn entries.
		BuildSpawnEntries();
		break;

	case 4: { // Event 5: spawn-data passthrough.
		int32_t copyLen;
		eventType = 0xB;
		flag = thirdArg;
		copyLen = flag * 12;
		memcpy(cursorOut, data, copyLen);
		cursorOut += copyLen;
		goto send_response;
	}

	case 5: // Event 6: ack with empty 0xC payload.
		eventType = 0xC;
		flag = 0xFFFF;
		cursorOut++;
		goto send_response;

	case 3: { // Event 4: resource region template update.
		uint32_t regionId = *(uint32_t *)cursor;
		cursor += 4;
		if (regionId != g_gcmVersion)
			break;
		{
			int32_t regionIndex = *(int32_t *)cursor;
			cursor += 4;
			if (regionIndex >= g_ResBankManager.regionCount)
				break;
			{
				CResBankRegion *region = g_ResBankManager.hashTable[regionIndex];
				int32_t numTemplates = *(int32_t *)cursor;
				cursor += 4;
				int32_t dataBytes = numTemplates * 4;
				int32_t i;
				for (i = 0; i < numTemplates; i++) {
					int32_t val = *(int32_t *)cursor;
					CResBankRegion_SetTemplate(region, i, val);
					cursor += 4;
				}
				memcpy(region->maxSpawns, cursor, dataBytes);
				region->nospawn = 1;

				eventType = 0xE;
				flag = thirdArg;
				cursorOut = buf;
				WriteInt32LE(&cursorOut, regionIndex);
				goto send_response;
			}
		}
	}

	case 2: // Event 3: clear GCM state.
		g_gcmState = 0;
		break;

	case 1: { // Event 2: entity list query.
		GCMEntityListBuilder entityBuilder;
		char entityBuf[0x202C];
		GameCentMon_BuildEntityList(&entityBuilder, entityBuf, serial);
		eventType = 0xC;
		flag = thirdArg;
		cursorOut = buf;
		cursorOut++;
		goto send_response;
	}

	case 0: { // Event 1: full resource region broadcast.
		int regionCount;
		int regionIdx;

		if (g_gcmState == 0)
			g_gcmState = 2;

		regionCount = g_ResourceTypeCount;

		for (regionIdx = 0; regionIdx < g_ResBankManager.regionCount; regionIdx++) {
			char nameBuf[256];
			int nameLen;
			CResBankRegion *region;
			int resIdx;
			int32_t *maxSpawnPtr;

			cursorOut = buf;
			region = g_ResBankManager.hashTable[regionIdx];

			strcpy(nameBuf, "Unknown?");
			if (strlen(nameBuf) > 6)
				nameBuf[6] = '\0';
			strcat(nameBuf, "+");
			strcat(nameBuf, region->name + 0);
			nameLen = strlen(nameBuf) + 1;

			*(uint8_t *)cursorOut = (uint8_t)nameLen;
			cursorOut++;
			memcpy(cursorOut, nameBuf, nameLen);
			cursorOut += nameLen;
			savedCursorOut = cursorOut;

			WriteInt32LE(&savedCursorOut, region->x1);
			WriteInt32LE(&savedCursorOut, region->y1);
			WriteInt32LE(&savedCursorOut, region->x2);
			WriteInt32LE(&savedCursorOut, region->y2);
			WriteInt32LE(&savedCursorOut, (int32_t)region->nospawn);
			WriteInt32LE(&savedCursorOut, regionCount);
			WriteInt32LE(&savedCursorOut, regionIdx);
			WriteInt32LE(&savedCursorOut, thirdArg);

			for (resIdx = 0; resIdx < regionCount; resIdx++) {
				int32_t val = CResBankRegion_GetQuantity(region, resIdx);
				WriteInt32LE(&savedCursorOut, val);
			}

			maxSpawnPtr = region->maxSpawns;
			for (resIdx = 0; resIdx < regionCount; resIdx++) {
				WriteInt32LE(&savedCursorOut, *maxSpawnPtr);
				maxSpawnPtr++;
			}

			cursorOut = savedCursorOut;
			dataLen = cursorOut - buf;

			BuildGameCentMonResponse(responseBuf, 9, 0, buf, dataLen);
			entity = CWorld_FindBySerial(g_World, serial);
			if (entity == NULL)
				continue;
			if (!VT_IsPlayer(entity))
				continue;
			SendToClient(entity, responseBuf, -1);
		}

		eventType = 0xA;
		flag = thirdArg;
		cursorOut = buf;
		cursorOut++;
		goto send_response;
	}

	default:
		break;
	}

	return;

send_response:
	dataLen = cursorOut - buf;
	BuildGameCentMonResponse(responseBuf, (uint8_t)eventType, (uint16_t)flag, buf, dataLen);
	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		return;
	if (!VT_IsPlayer(entity))
		return;
	SendToClient(entity, responseBuf, -1);
}
