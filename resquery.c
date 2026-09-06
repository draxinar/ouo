/*
 * Resource queries - attribute lookups layered over CResManager tables.
 *
 * Higher-level callers (combat, speech, spawning) reach through here to
 * ask questions like "what damage does this weapon do?" or "what string
 * does this speech key resolve to?" without threading CSearchCtx
 * iterators through every call site.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "container.h"
#include "egg.h"
#include "gamecentmon.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_reader.h"
#include "packet_utils.h"
#include "player.h"
#include "region.h"
#include "resbank.h"
#include "terrain.h"
#include "time.h"
#include "vtable.h"

static void ResQuery_SessionDestruct(ResQuerySession *session, int freeMemory); // 0x004B9A80
static char *ResQuery_WriteResourceData(char *cursor, uint32_t param, uint8_t flags); // 0x004B52DF
static void ResQuery_SelectRegion(ResQuerySession *session, int hashIndex); // 0x004B36E3
static void ResQuery_SessionCleanup(ResQuerySession *session); // 0x004B3639
static void ResQuery_SessionConstruct(ResQuerySession *session); // 0x004B35E6
static ResQuerySession *ResQuery_GetOrCreateSession(CPlayer *player); // 0x004B3439
static void ResQuery_ResetTimestamps(int slotIndex); // 0x004B33CC

__extension__ typedef struct ResQuerySession {
	int32_t *regionNames;
	int32_t *templateNames;
	int32_t *field103D8;
	int32_t *templateChainCountCache;
	int32_t regionIndex;
	uint32_t serial;
	int32_t timestamp;
} ResQuerySession;

/*
 * 0x004B33CC - ResQuery_ResetTimestamps
 *
 * Resets timestamp on all session slots, setting the given
 * slotIndex to 0 (most recent) and incrementing all others.
 */
static void
ResQuery_ResetTimestamps(int slotIndex)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (g_ResSessionSlots[i] != NULL && g_ResSessionSlots[i]->timestamp != -1)
			g_ResSessionSlots[i]->timestamp++;
	}
	g_ResSessionSlots[slotIndex]->timestamp = 0;
}
/*
 * 0x004B3439 - ResQuery_GetOrCreateSession
 *
 * Looks up or creates a session for the given player.
 * Uses LRU replacement if all 8 slots are full.
 */
static ResQuerySession *
ResQuery_GetOrCreateSession(CPlayer *player)
{
	int i;
	int emptySlot;
	int oldestTime;
	ResQuerySession *session;

	// One-time init.
	if (!g_ResSessionInit) {
		g_ResSessionInit = 1;
		for (i = 0; i < 8; i++)
			g_ResSessionSlots[i] = NULL;
	}

	// Combined search: find existing match or first empty slot.
	emptySlot = -1;
	for (i = 0; i < 8; i++) {
		if (g_ResSessionSlots[i] == NULL) {
			emptySlot = i;
		} else if (g_ResSessionSlots[i]->serial == ((CItem *)player)->serial) {
			ResQuery_ResetTimestamps(i);
			return g_ResSessionSlots[i];
		}
	}

	// LRU eviction if no empty slot.
	if (emptySlot == -1) {
		oldestTime = 0;
		for (i = 0; i < 8; i++) {
			if (g_ResSessionSlots[i]->timestamp >= oldestTime) {
				oldestTime = g_ResSessionSlots[i]->timestamp;
				emptySlot = i;
			}
		}
		if (g_ResSessionSlots[emptySlot] != NULL)
			ResQuery_SessionDestruct(g_ResSessionSlots[emptySlot], 1);
	}

	// Allocate new session.
	session = (ResQuerySession *)OperatorNew(sizeof(ResQuerySession));
	if (session != NULL)
		ResQuery_SessionConstruct(session);
	g_ResSessionSlots[emptySlot] = session;
	g_ResSessionSlots[emptySlot]->serial = ((CItem *)player)->serial;
	return g_ResSessionSlots[emptySlot];
}

/*
 * 0x004B35E6 - ResQuery_SessionConstruct
 *
 * Initializes a ResQuerySession to default state.
 * Sets all pointers to NULL, regionIndex to -1, serial/timestamp to 0.
 */
static void
ResQuery_SessionConstruct(ResQuerySession *session)
{
	session->regionNames = NULL;
	session->templateNames = NULL;
	session->field103D8 = NULL;
	session->templateChainCountCache = NULL;
	session->regionIndex = -1;
	session->serial = 0xFFFFFFFF;
	session->timestamp = 0;
}

/*
 * 0x004B3639 - ResQuery_SessionCleanup
 *
 * Frees the four allocated buffers in a session.
 */
static void
ResQuery_SessionCleanup(ResQuerySession *session)
{
	if (session->regionNames != NULL) {
		OperatorDelete(session->regionNames);
		session->regionNames = NULL;
	}
	if (session->templateNames != NULL) {
		OperatorDelete(session->templateNames);
		session->templateNames = NULL;
	}
	if (session->field103D8 != NULL) {
		OperatorDelete(session->field103D8);
		session->field103D8 = NULL;
	}
	if (session->templateChainCountCache != NULL) {
		OperatorDelete(session->templateChainCountCache);
		session->templateChainCountCache = NULL;
	}
}

/*
 * 0x004B36E3 - ResQuery_SelectRegion
 *
 * Selects a region by hash table index. Allocates and populates
 * the session's cached arrays from the selected region's data.
 */
static void
ResQuery_SelectRegion(ResQuerySession *session, int hashIndex)
{
	// If already on this region, nothing to do.
	if (session->regionIndex == hashIndex)
		return;

	// First selection: allocate buffers.
	if (session->regionIndex == -1) {
		session->regionNames = (int32_t *)OperatorNew(g_ResQueryTypeCount * sizeof(int32_t));
		session->templateNames = (int32_t *)OperatorNew(g_ResQueryTypeCount * sizeof(int32_t));
		session->field103D8 = (int32_t *)OperatorNew(g_ResQueryTypeCount * sizeof(int32_t));
		session->templateChainCountCache = (int32_t *)OperatorNew(0x4000);
		memset(session->templateChainCountCache, 0, 0x4000);
	}

	// Set new region and zero the type arrays.
	session->regionIndex = hashIndex;
	memset(session->regionNames, 0, g_ResQueryTypeCount * sizeof(int32_t));
	memset(session->templateNames, 0, g_ResQueryTypeCount * sizeof(int32_t));
	memset(session->field103D8, 0, g_ResQueryTypeCount * sizeof(int32_t));
}

/*
 * 0x004B3B0F - HandlePacket_ResourceQuery (packet 0x79, response 0x7A)
 *
 * GM Resource Editor handler. Parses subtype(Byte), flags(Byte),
 * resourceIndex(Word), param(DWord) and dispatches a 23-case switch on
 * (subtype - 1) covering the resource system. Uses an 8-slot global
 * session context array.
 */
void
HandlePacket_ResourceQuery(CPlayer *this, uint8_t *buf, uint16_t packetLen)
{
	uint8_t subtype;
	uint8_t flags;
	uint16_t resourceIndex;
	uint32_t param;
	ResQuerySession *session;
	char dataBuf[0xE020];
	char *cursor;
	uint8_t pktBuf[0xE004];
	int32_t dataLen;
	PacketReader reader;

	if (!PacketSecurity_RequireGM(this, PacketType_ResourceQuery, "HandlePacket_ResourceQuery"))
		return;

	PacketReader_Init(&reader, buf, packetLen, GetSizeLength(buf));
	if (!PacketReader_ReadU8(&reader, &subtype) || !PacketReader_ReadU8(&reader, &flags) || !PacketReader_ReadU16(&reader, &resourceIndex) ||
	        !PacketReader_ReadU32(&reader, &param)) {
		PacketSecurity_ClosePlayer(this, PacketType_ResourceQuery, "HandlePacket_ResourceQuery", "truncated fixed fields");
		return;
	}

	session = NULL;

	// Subtype 9 does not need a session.
	if ((subtype & 0xFF) != 9)
		session = ResQuery_GetOrCreateSession(this);

	cursor = dataBuf;

	switch (subtype & 0xFF) {

	case 1: {
		// List all resource regions.
		CResBankRegion *region;
		int regionCount;
		char *headerCursor;

		cursor += 2; // reserve space for count word
		region = g_ResBankManager.first;
		regionCount = 0;

		while (region != NULL) {
			char *nameCursor;
			char *nameEnd;

			nameCursor = cursor;
			nameEnd = nameCursor + 0x80;

			// Copy region name (up to 128 bytes).
			strcpy(nameCursor, region->name);

			// Write bounding box after name.
			WriteInt32LE(&nameEnd, region->x1);
			WriteInt32LE(&nameEnd, region->y1);
			WriteInt32LE(&nameEnd, region->x2);
			WriteInt32LE(&nameEnd, region->y2);
			cursor = nameEnd;

			region = region->next;
			regionCount++;
		}

		// Write count at the start.
		headerCursor = dataBuf;
		WriteInt16LE(&headerCursor, (int16_t)regionCount);
		break;
	}

	case 2: {
		// Return resource type count and write names.
		char *headerCursor;
		char *nameEnd;

		headerCursor = cursor;
		nameEnd = headerCursor + 0x80;

		// Copy server name.
		strcpy(headerCursor, g_Config.serverName);

		// Write map dimensions after server name.
		WriteInt32LE(&nameEnd, g_mapStartX);
		WriteInt32LE(&nameEnd, g_mapStartY);
		WriteInt32LE(&nameEnd, g_mapWidth);
		WriteInt32LE(&nameEnd, g_mapHeight);
		cursor = nameEnd;
		break;
	}

	case 3: {
		// Return resource type count.
		WriteInt16LE(&cursor, (int16_t)g_ResourceTypeCount);
		// Cache the count as 32-bit.
		g_ResQueryTypeCount = g_ResourceTypeCount;
		break;
	}

	case 4: {
		// Query region data for a specific region.
		CResBankRegion *region;
		int changeCount;
		int i;
		int changeFlags;
		int32_t spawnedCount, quantity, field103D8Val;

		if (g_ResQueryTypeCount == 0)
			break;

		// Select the region by hash index.
		ResQuery_SelectRegion(session, flags & 0xFF);

		region = g_ResBankManager.hashTable[flags & 0xFF];
		if (region == NULL)
			break;

		// Reserve 4 bytes for header.
		cursor += 4;
		changeCount = 0;

		for (i = 0; i < g_ResQueryTypeCount; i++) {
			changeFlags = 0;

			// Compare spawnedCounts.
			if (region->spawnedCounts[i] != session->regionNames[i])
				changeFlags |= 0x4000;

			// Compare quantities.
			if (region->quantities[i] != session->templateNames[i])
				changeFlags |= 0x2000;

			// Compare field103D8.
			if (region->field103D8[i] != session->field103D8[i])
				changeFlags |= 0x1000;

			if (changeFlags == 0)
				continue;

			// Read current values.
			spawnedCount = region->spawnedCounts[i];
			quantity = region->quantities[i];
			field103D8Val = region->field103D8[i];

			// Update session cache.
			session->regionNames[i] = spawnedCount;
			session->templateNames[i] = quantity;
			session->field103D8[i] = field103D8Val;

			// Write to response.
			// Check if any value exceeds 16-bit range.
			if (spawnedCount > 0x7FFF || quantity > 0x7FFF || spawnedCount < (int32_t)0xFFFF8000 || quantity < (int32_t)0xFFFF8000 || field103D8Val > 0x7FFF ||
			        field103D8Val < (int32_t)0xFFFF8000) {
				// 32-bit mode: set high bit in index word.
				int16_t indexWord = (int16_t)(i | changeFlags | 0x8000);
				WriteInt16LE(&cursor, indexWord);

				if (changeFlags & 0x4000)
					WriteInt32LE(&cursor, spawnedCount);
				if (changeFlags & 0x2000)
					WriteInt32LE(&cursor, quantity);
				if (changeFlags & 0x1000)
					WriteInt32LE(&cursor, field103D8Val);
			} else {
				// 16-bit mode.
				int16_t indexWord = (int16_t)(i | changeFlags);
				WriteInt16LE(&cursor, indexWord);

				if (changeFlags & 0x4000)
					WriteInt16LE(&cursor, (int16_t)spawnedCount);
				if (changeFlags & 0x2000)
					WriteInt16LE(&cursor, (int16_t)quantity);
				if (changeFlags & 0x1000)
					WriteInt16LE(&cursor, (int16_t)field103D8Val);
			}

			changeCount++;

			// Check buffer limit.
			dataLen = (int)(cursor - dataBuf);
			if (dataLen > 0x1FF6)
				break;
		}

		if (changeCount == 0)
			return;

		// Write header at start of buffer.
		{
			char *headerCursor = dataBuf;
			WriteInt16LE(&headerCursor, (int16_t)(flags & 0xFF));
			WriteInt16LE(&headerCursor, (int16_t)changeCount);
		}
		break;
	}

	case 5: {
		// Clear session cached arrays.
		if (session->regionIndex == -1)
			return;

		// Zero the four cached arrays.
		memset(session->regionNames, 0, g_ResQueryTypeCount * sizeof(int32_t));
		memset(session->templateNames, 0, g_ResQueryTypeCount * sizeof(int32_t));
		memset(session->field103D8, 0, g_ResQueryTypeCount * sizeof(int32_t));
		memset(session->templateChainCountCache, 0, 0x4000);
		return;
	}

	case 6: {
		// Set resource quantity (add param to existing).
		CResBankRegion *region;

		region = g_ResBankManager.hashTable[flags & 0xFF];
		if (region == NULL)
			return;

		CResBankRegion_SetTemplate(region, resourceIndex & 0xFFFF, region->quantities[resourceIndex & 0xFFFF] + (int32_t)param);
		return;
	}

	case 7: {
		// Get region respawn g_Config.
		CResBankRegion *region;

		region = g_ResBankManager.hashTable[flags & 0xFF];
		if (region == NULL)
			break;

		WriteInt32LE(&cursor, region->minRespawnTime);
		WriteInt32LE(&cursor, region->maxRespawnTime);
		break;
	}

	case 8: {
		// Set region respawn g_Config.
		CResBankRegion *region;
		int32_t oldMinRespawnTime, oldMaxRespawnTime;

		region = g_ResBankManager.hashTable[flags & 0xFF];
		if (region == NULL)
			return;

		oldMinRespawnTime = region->minRespawnTime;
		oldMaxRespawnTime = region->maxRespawnTime;

		region->minRespawnTime = resourceIndex & 0xFFFF;
		region->maxRespawnTime = (int32_t)param;

		// If values changed, refresh.
		if (oldMinRespawnTime != (int)(resourceIndex & 0xFFFF) || oldMaxRespawnTime != (int32_t)param)
			RefreshResourceRegions();
		return;
	}

	case 9: {
		// Template kill/respawn control.
		int templateId;
		int i;
		int found;

		templateId = resourceIndex & 0xFFFF;

		if (templateId == 0)
			goto case9_done;
		if (templateId == (int)g_npcProcessFilter)
			goto case9_done;

		if (templateId == 1) {
			g_npcProcessFilter = 1;
			goto case9_done;
		}
		if (templateId == 2) {
			g_npcProcessFilter = 0;
			goto case9_done;
		}

		// Set filter to this template ID.
		g_npcProcessFilter = (uint32_t)templateId;

		// Walk NPC hash table, kill matching NPCs.
		found = 0;
		for (i = 0; i <= 0x3F; i++) {
			CNPC *npc = g_NPCHash[i];
			while (npc != NULL) {
				CNPC *next;
				next = npc->npcHashNext;
				if (npc != NULL) {
					uint8_t removed = ((CItem *)npc)->resourceEntity.entity.removedFromWorld;
					if (removed == 0) {
						uint16_t npcBody = (uint16_t)(((CItem *)npc)->serial & 0xFFFF);
						if (npcBody == (uint16_t)templateId)
							found = 1;
					}
				}
				npc = next;
			}
		}
		USED(found);

case9_done:
		// Check template limit.
		if ((int32_t)param + 1 >= g_ResQueryTemplateLimit)
			return;

		cursor = ResQuery_WriteResourceData(cursor, param, flags & 0xFF);
		break;
	}

	case 10: {
		// Get spawn enabled flag.
		WriteInt32LE(&cursor, g_SpawnEnabled);
		break;
	}

	case 11: {
		// Set spawn enabled flag.
		g_SpawnEnabled = (int32_t)param;
		return;
	}

	case 12: {
		// Get object counts (10 globals at 0x0068B374-0x0068B3A0).
		char *p = cursor;
		WriteInt32LE(&p, (int32_t)g_PlayerCreateCount);
		WriteInt32LE(&p, (int32_t)g_NPCCount);
		WriteInt32LE(&p, (int32_t)g_EntityCount);
		WriteInt32LE(&p, (int32_t)g_StaticItemCount);
		WriteInt32LE(&p, (int32_t)g_DynamicItemCount);
		WriteInt32LE(&p, (int32_t)g_ResourceEntityCount);
		WriteInt32LE(&p, g_BBoardCount);
		WriteInt32LE(&p, g_SignpostCount);
		WriteInt32LE(&p, (int32_t)g_MobileCount);
		WriteInt32LE(&p, g_ContainerCount);
		cursor = p;
		break;
	}

	case 13: {
		// Tick all NPCs (delete/respawn).
		GameCentMon_TickAllNPCs();
		return;
	}

	case 14: {
		// Get initial spawn flag.
		WriteInt32LE(&cursor, g_IsInitialSpawn);
		break;
	}

	case 15: {
		// Set initial spawn flag.
		g_IsInitialSpawn = (int32_t)param;
		return;
	}

	case 16: {
		// Get region name for given location.
		if ((int32_t)param == 0) {
			ResourceQuery_Stub();
			return;
		}
		if ((int32_t)param != 1)
			return;

		{
			CLocation loc;
			CResBankRegion *region;
			char msgBuf[256];

			CLocation_SetLoc(&loc, &((CItem *)this)->resourceEntity.entity.location);
			region = CResBankManager_GetRegionByLocation(loc.x, loc.y);

			snprintf(msgBuf, sizeof(msgBuf), "%s (%d)", region->name, region->regionIndex);

			strcpy((char *)cursor, msgBuf);
			cursor += strlen(msgBuf) + 1;
		}
		break;
	}

	case 17: {
		// List all template names (paged response).
		char *headerStart;
		int count;
		int totalSize;
		int i;

		headerStart = cursor;
		cursor += 4; // reserve for header
		totalSize = 4;
		count = 0;

		for (i = 0; i < 0x800; i++) {
			char *name;
			int nameLen;

			if (!CResManager_HasByInt(&g_TemplatesRM, i))
				continue;

			name = g_TemplateNames[i];
			if (name == NULL)
				continue;

			nameLen = strlen(name) + 1;
			if (nameLen > 0x14)
				nameLen = 0x14;

			// Write template index.
			WriteInt32LE(&cursor, i);

			// Write name length byte.
			*(uint8_t *)cursor = (uint8_t)nameLen;
			cursor++;

			// Copy name.
			memcpy(cursor, name, nameLen);
			// Null-terminate.
			cursor[nameLen - 1] = '\0';
			cursor += nameLen;

			count++;
			totalSize += nameLen + 5; // 4 (index) + 1 (len) + nameLen

			// Check if buffer is getting full.
			if (totalSize > 0x1F40) {
				// Send mid-flush packet.
				WriteInt32LE(&headerStart, count);

				dataLen = (int)(cursor - dataBuf);
				BuildTriggerPacket(pktBuf, subtype & 0xFF, dataLen, dataBuf);
				SendToClient((CItem *)this, pktBuf, -1);

				// Reset for next batch.
				cursor = dataBuf;
				headerStart = cursor;
				cursor += 4;
				totalSize = 4;
				count = 0;
			}
		}

		if (count == 0)
			return;

		WriteInt32LE(&headerStart, count);
		break;
	}

	case 18: {
		// Diff template chain counts.
		int changeCount;
		int i;

		if (session->regionIndex == -1)
			ResQuery_SelectRegion(session, 0);

		cursor += 2; // reserve for change count
		changeCount = 0;

		for (i = 0; i < 0x1000; i++) {
			int32_t actual, cached;
			char *writeCursor;
			char *saveCursor;

			actual = g_TemplateChainCount[i];
			cached = session->templateChainCountCache[i];

			if (actual == cached)
				continue;

			// Update cache.
			session->templateChainCountCache[i] = actual;

			writeCursor = cursor;

			// Check if value exceeds 16-bit range.
			if (actual > 0x7FFF) {
				// 32-bit mode: set high bit.
				int16_t indexWord = (int16_t)(i | 0x8000);
				WriteInt16LE(&writeCursor, indexWord);
				saveCursor = writeCursor;
				WriteInt32LE(&saveCursor, actual);
				cursor = saveCursor;
			} else {
				// 16-bit mode.
				WriteInt16LE(&writeCursor, (int16_t)i);
				WriteInt16LE(&writeCursor, (int16_t)actual);
				cursor = writeCursor;
			}

			changeCount++;
		}

		if (changeCount == 0)
			return;

		// Write count at start.
		{
			char *headerCursor = dataBuf;
			WriteInt16LE(&headerCursor, (int16_t)changeCount);
		}
		break;
	}

	case 19: {
		// Teleport player to template chain entity location.
		CItem *entity;
		int templateId;
		CLocation loc;
		int16_t x, y;
		int16_t z;

		templateId = resourceIndex & 0xFFFF;
		if (templateId >= 0x1000)
			return;

		entity = g_TemplateChain[templateId];
		if (entity == NULL) {
			// Try secondary table if templateId < 199.
			if (templateId < 0xC7)
				entity = g_TemplateChain[templateId + 0x3E8];
		}
		if (entity == NULL)
			return;

		x = entity->resourceEntity.entity.location.x + 1;
		y = entity->resourceEntity.entity.location.y + 1;
		z = (int16_t)GameCentMon_CalcZ(x, y);
		CLocation_Set(&loc, x, y, z);

		// Validate coordinates.
		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)loc.x, (int)loc.y))
			return;

		// Hide player, then teleport.
		((void (*)(void *))VT_FN((CItem *)this, VT_HIDE))((CItem *)this);
		((void (*)(void *, CLocation *))VT_FN((CItem *)this, VT_DROP_AT_FEET))((CItem *)this, &loc);
		return;
	}

	case 20: {
		// Tick template chains for given template ID.
		GameCentMon_TickTemplateChains(resourceIndex & 0xFFFF);
		return;
	}

	case 21: {
		// Broadcast event: template kill.
		struct {
			uint32_t serial;
			int32_t eventType;
			int32_t pad;
			uint32_t templateId;
		} eventData;

		eventData.serial = ((CItem *)this)->serial;
		eventData.eventType = 0x0A;
		eventData.pad = 0;
		eventData.templateId = resourceIndex & 0xFFFF;
		GameCentMon_BroadcastEvent((char *)&eventData, 0x10);
		return;
	}

	case 22: {
		// Get NPC AI timer reset value (binary: 0x0061D67C).
		WriteInt32LE(&cursor, (int32_t)g_npcAITimerReset);
		break;
	}

	case 23: {
		// Broadcast event: resource spawn.
		struct {
			uint32_t serial;
			int32_t eventType;
			int32_t pad;
			uint32_t templateId;
		} eventData;

		eventData.serial = ((CItem *)this)->serial;
		eventData.eventType = 0x0B;
		eventData.pad = 0;
		eventData.templateId = resourceIndex & 0xFFFF;
		GameCentMon_BroadcastEvent((char *)&eventData, 0x10);
		return;
	}

	default:
		return;
	}

	// Send response packet.
	dataLen = (int)(cursor - dataBuf);
	BuildTriggerPacket(pktBuf, subtype & 0xFF, dataLen, dataBuf);
	SendToClient((CItem *)this, pktBuf, -1);
}

/*
 * 0x004B5043 - HandlePacket_SendResources
 *
 * GM Resource Definition Editor (packet 0x87, no response). Reads
 * mode(Byte), resIndex(Word), dataLen(Word), data(String). Mode 0 updates
 * an existing region, mode 1 creates a new one, mode 2 deletes. Parses 4
 * little-endian int32s (x1, y1, x2, y2) and a pascal string (name) from
 * the data blob, then runs RefreshResourceRegions. The binary's
 * SaveResources companion is an empty stub.
 */
void
HandlePacket_SendResources(CPlayer *this, uint8_t *buf, uint16_t packetLen)
{
	uint8_t mode;
	uint16_t resIndex, dataLen;
	const uint8_t *data;
	CResBankRegion *region;
	char *cursor;
	int nameLen;
	PacketReader reader;
	uint32_t nameOffset;
	size_t nameCap;

	if (!PacketSecurity_RequireGM(this, PacketType_SendResources, "HandlePacket_SendResources"))
		return;

	PacketReader_Init(&reader, buf, packetLen, 3);
	if (!PacketReader_ReadU8(&reader, &mode) || !PacketReader_ReadU16(&reader, &resIndex) || !PacketReader_ReadU16(&reader, &dataLen)) {
		PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "truncated fixed fields");
		return;
	}
	if (packetLen < 8 || dataLen != (uint16_t)(packetLen - 8)) {
		PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "data length does not match packet");
		return;
	}
	if (!PacketReader_ReadBytesPtr(&reader, &data, dataLen)) {
		PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "truncated resource data");
		return;
	}

	if (mode > 2) {
		PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "invalid resource mode");
		return;
	}

	if (mode == 0 || mode == 2) {
		if (resIndex >= 256 || g_ResBankManager.hashTable[resIndex] == NULL) {
			PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "invalid resource region");
			return;
		}
		region = g_ResBankManager.hashTable[resIndex];
	} else {
		region = NULL;
	}

	if (mode == 0 || mode == 1) {
		if (dataLen < 17) {
			PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "truncated region update");
			return;
		}

		nameOffset = 17u;
		nameLen = data[16];
		nameCap = sizeof(((CResBankRegion *)0)->name);
		if (nameOffset + (uint32_t)nameLen > dataLen || (size_t)nameLen >= nameCap) {
			PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "invalid region name length");
			return;
		}
	}

	if (mode == 1) {
		region = OperatorNew(sizeof(CResBankRegion));
		if (region == NULL) {
			PacketSecurity_ClosePlayer(this, PacketType_SendResources, "HandlePacket_SendResources", "region allocation failed");
			return;
		}
		CResBankRegion_Constructor(region);
	}

	if (mode == 0 || mode == 1) {
		cursor = (char *)data;

		region->x1 = ReadInt32LE(&cursor);
		region->y1 = ReadInt32LE(&cursor);
		region->x2 = ReadInt32LE(&cursor);
		region->y2 = ReadInt32LE(&cursor);

		nameLen = (uint8_t)*cursor;
		cursor++;
		memcpy(region->name, cursor, nameLen);
		region->name[nameLen] = '\0';

		if (mode == 1)
			CResBankManager_AddRegion(region);

		SaveResources();
	} else if (mode == 2) {
		CResBankManager_RemoveRegion(region, 1);

		SaveResources();
	}

	RefreshResourceRegions();
}

/*
 * 0x004B52DF - ResQuery_WriteResourceData
 *
 * Writes filtered resource entries from the circular buffer to the
 * response. Reserves 8-byte header (count + lastValue), then iterates
 * the circular entry buffer starting from the smallest value > param.
 * Entries are filtered by flags nibbles: low nibble matches entry type
 * (0xF = all), high nibble must intersect entry category. Copies
 * matching entry name strings into the buffer. Writes count and last
 * value into the reserved header at the end.
 */
static char *
ResQuery_WriteResourceData(char *cursor, uint32_t param, uint8_t flags)
{
	char *savedCursor;
	int highFilter;
	int lowFilter;
	int idx;
	int32_t bestVal;
	int i;
	int remaining;
	int count;
	int entryByte;
	int entryLow;
	int entryHigh;
	int len;

	savedCursor = cursor;
	cursor += 8;

	highFilter = flags & 0xF0;
	lowFilter = flags & 0x0F;

	idx = 0;
	bestVal = 0x7FFFFFFF;

	// Find starting index: smallest value > param.
	for (i = 0; i < 0x64; i++) {
		if (g_ResQueryEntryValues[i] > (int32_t)param && g_ResQueryEntryValues[i] < bestVal) {
			bestVal = g_ResQueryEntryValues[i];
			idx = i;
		}
	}

	remaining = 0x1F40;
	count = 0;

	while (remaining > 0) {
		entryByte = *(signed char *)g_ResQueryEntryNames[idx];
		entryLow = entryByte & 0x0F;
		entryHigh = entryByte & 0xF0;

		if (lowFilter != 0x0F) {
			if (lowFilter != entryLow)
				goto skip;
		}
		if ((highFilter & entryHigh) == 0)
			goto skip;

		// Match: copy entry name string.
		len = strlen(g_ResQueryEntryNames[idx]) + 1;
		strcpy(cursor, g_ResQueryEntryNames[idx]);
		cursor += len;
		remaining -= len;
		count++;

skip:
		bestVal = g_ResQueryEntryValues[idx];
		idx++;
		if (idx == 0x64)
			idx = 0;
		if (idx == g_ResQueryEntryIndex)
			break;
	}

	// Write header: count and last value.
	WriteInt32LE(&savedCursor, count);
	WriteInt32LE(&savedCursor, bestVal);

	return cursor;
}

/*
 * 0x004B9A24 - TracedMemmove
 *
 * memmove with a call-site trace. The trace line is formatted into a
 * stack buffer and never printed, so only the copy has any effect.
 */
static __attribute__((unused)) void
TracedMemmove(void *dst, const void *src, size_t len, const char *file, int line)
{
	char buf[512];

	sprintf(buf, "File : %s Line %d from %X to %X len %d\n", file, line, (unsigned)(uintptr_t)src, (unsigned)(uintptr_t)dst, (unsigned)len);
	memmove(dst, src, len);
}

/*
 * 0x004B9A80 - ResQuery_SessionDestruct
 *
 * Destructor: cleans up and optionally frees the session.
 */
static void
ResQuery_SessionDestruct(ResQuerySession *session, int freeMemory)
{
	ResQuery_SessionCleanup(session);
	if (freeMemory & 1)
		OperatorDelete(session);
}
