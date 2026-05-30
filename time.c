/*
 * Time - monotonic tick source and formatted wall-clock helpers.
 *
 * Replaces Win32's timeGetTime with a monotonic clock while still
 * returning a 32-bit millisecond counter so the scheduler, packet
 * throttles, and world saves compare ticks the way the binary did.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dat.h"

#include "nodepool.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "stddeque.h"
#include "taglist.h"
#include "time.h"
#include "weather.h"
#include "world.h"

#include "egg.h"
#include "load.h"
#include "npc.h"
#include "skill.h"
#include "timer.h"
#include "dynamic.h"
#include "account.h"
#include "twofish.h"
#include "bboard.h"
#include "wombat_exec.h"
#include "defcon.h"
#include "feature.h"
#include "gamecentmon.h"
#include "main.h"
#include "resbank.h"
#include "resource_regrowth.h"

static void StaticInit_TimeManager(void); // 0x00467893
static int CLightManager_GetMoonLightContribution(void); // 0x00473C9E
static void CTimeManager_ConstructorVTable(void); // 0x004D8043
static int TimeManager_ParseTimeUnit(const char *unitName, int *outCount); // 0x004D8449
static int TimeManager_MatchTimePattern(const char *pattern, int value); // 0x004D853D
static void CTimeManager_SeasonAdvance(void); // 0x004D89F1
static void CTimeManager_DayAdvance(void); // 0x004D8A06
static void CTimeManager_DispatchEventList(CTimeEventNode *head); // 0x004D8A53
static void StaticInit_EventRingBuffer(void); // 0x004D8D9F

CTimeManager g_TimeManager;

// 0x00648290
int32_t g_globalLightLevel;

// 0x00699A98
int32_t g_hourlyLightTable[24];

// 0x00699A78
int32_t g_moonPhaseLightTable[8];

// 0x006F0418 - per-minute event subscription heads
static CTimeEventNode *minuteEventHeads[60];
// 0x006F0358 - per-hour event subscription heads
static CTimeEventNode *hourEventHeads[24];
// 0x006F03C8 - per-day event subscription heads
static CTimeEventNode *dayEventHeads[7];
// 0x006F03B8 - per-week event subscription heads
static CTimeEventNode *weekEventHeads[4];
// 0x006F03E8 - per-month event subscription heads
static CTimeEventNode *monthEventHeads[12];
// 0x006F03E4 - per-year event subscription heads
static CTimeEventNode *yearEventHeads[1];

// 0x006F0508 - saved next pointer protecting DispatchEventList iteration
static CTimeEventNode *g_savedEventNext;

/*
 * 0x00467893 - Static init wrapper
 *
 * Static initializer that constructs g_TimeManager.
 */
static __attribute__((unused)) void
StaticInit_TimeManager(void)
{
	CTimeManager_Init();
}

// 0x00648294 - CLightManager subscription list of ScriptAttachNode pointers
static CResList g_lightScriptList;

// 0x0068B394 - total CBulletinBoard count.
int g_BBoardCount;

/*
 * 0x00473BD0 - CLightManager::CLightManager
 *
 * Initializes the global light level and subscription list, then runs
 * an initial light tick.
 */
void
CLightManager_Constructor(void)
{
	CResListNode_Constructor_bin((CResListNode *)&g_lightScriptList);
	g_globalLightLevel = 0;
	GlobalLightManager_Tick(0);
}

/*
 * 0x00473C29 - CLightManager::~CLightManager
 *
 * Destroys the subscription list.
 */
void
CLightManager_Destructor(void)
{
	CResList_Destructor(&g_lightScriptList);
}

/*
 * 0x00473C3F - CTimeManager::GetTrammelPhase
 *
 * Returns the current Trammel moon phase derived from game time.
 */
int
CTimeManager_GetTrammelPhase(void)
{
	return ((g_GameHour * 6 + g_GameMinute / 10 + 1) / 3 + 4) & 7;
}

/*
 * 0x00473C74 - CTimeManager::GetFeluccaPhase
 *
 * Returns the current Felucca moon phase derived from game time.
 */
int
CTimeManager_GetFeluccaPhase(void)
{
	return (g_GameHour * 6 + g_GameMinute / 10 + 4) & 7;
}

/*
 * 0x00473C9E - CLightManager::GetMoonLightContribution
 *
 * Returns the combined moon light contribution from both phases.
 */
static int
CLightManager_GetMoonLightContribution(void)
{
	int trammel, felucca;

	trammel = CTimeManager_GetTrammelPhase();
	felucca = CTimeManager_GetFeluccaPhase();
	return g_moonPhaseLightTable[trammel] + g_moonPhaseLightTable[felucca];
}

/*
 * 0x00473CCD - GlobalLightManager::Tick
 *
 * Recomputes the global light level from the hourly table minus moon
 * contribution. When it changes, sends SUNLIGHT packets to each player
 * whose effective level (after per-block override) differs.
 */
void
GlobalLightManager_Tick(uint32_t arg)
{
	int targetLevel;
	int blockIndex;
	int blockLight;
	CPlayer *p;
	uint8_t obuf[8];

	USED(arg);

	targetLevel = g_hourlyLightTable[g_TimeManager.hour] - CLightManager_GetMoonLightContribution();
	if (targetLevel < 0)
		targetLevel = 0;

	if (targetLevel == g_globalLightLevel)
		return;

	g_globalLightLevel = targetLevel;

	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		if (p->mobile.container.item.resourceEntity.entity.removedFromWorld)
			continue;

		blockIndex = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &p->mobile.container.item.resourceEntity.entity.location, 0);
		blockLight = (int)g_MapBlocks[blockIndex].lightOverride;
		if (blockLight == 0)
			blockLight = g_globalLightLevel;

		if (blockLight == (int)(int16_t)p->lastLightLevel)
			continue;

		p->lastLightLevel = (uint16_t)blockLight;
		PacketManager_MakePacket_SUNLIGHT(obuf, (uint8_t)blockLight);
		Entity_BroadcastPacket((CItem *)p, p->mobile.container.item.serial, obuf);
	}
}

/*
 * 0x00473DCF - CLightManager::RegisterScript
 *
 * Subscribes a script attach node to global light change events.
 */
void
CLightManager_RegisterScript(ScriptAttachNode *scriptNode)
{
	CResList_InsertBack(&g_lightScriptList, &scriptNode);
}

/*
 * 0x00473DEB - CLightManager::UnregisterScript
 *
 * Removes a script attach node from the light subscription list.
 */
void
CLightManager_UnregisterScript(ScriptAttachNode *scriptNode)
{
	CResListNode *found;

	found = CResList_FindByValue(&g_lightScriptList, &scriptNode, NULL, 1);
	if (CResList_IsValid(&g_lightScriptList, found))
		CResList_RemoveAndFree(&g_lightScriptList, found, 1);
}

/*
 * 0x00473E34 - CLightManager::NotifyMoonChange
 *
 * Fires event 0x26 on every entity subscribed to light changes when
 * either moon phase has changed.
 */
void
CLightManager_NotifyMoonChange(int trammelChanged, int feluccaChanged)
{
	CResListNode *node;
	void *dataPtr;
	void *scriptNode;

	node = CResList_Begin(&g_lightScriptList);
	while (CResList_IsValid(&g_lightScriptList, node)) {
		dataPtr = CResList_GetData(&g_lightScriptList, node);
		scriptNode = *(void **)dataPtr;
		node = CResList_Next(&g_lightScriptList, node);

		if (scriptNode != NULL) {
			Entity_ExecuteEvent((CEntity *)((ScriptAttachNode *)scriptNode)->entity, 0x26, trammelChanged, feluccaChanged);
		}
	}
}
/*
 * 0x004D7F23 - CTimeManager::CTimeManager
 *
 * Initializes clock fields (hour=12), event list arrays, and moon phase
 * tracking state.
 */
void
CTimeManager_Init(void)
{
	g_TimeManager.vtableSlot = 0;
	g_TimeManager.tickCount = 0;
	g_TimeManager.year = 0;
	g_TimeManager.month = 0;
	g_TimeManager.week = 0;
	g_TimeManager.day = 0;
	g_TimeManager.hour = 12;
	g_TimeManager.minute = 0;
	g_TimeManager.seconds = 0;

	g_TimeManager.minuteEvents = minuteEventHeads;
	memset(minuteEventHeads, 0, sizeof(minuteEventHeads));

	g_TimeManager.hourEvents = hourEventHeads;
	memset(hourEventHeads, 0, sizeof(hourEventHeads));

	g_TimeManager.dayEvents = dayEventHeads;
	memset(dayEventHeads, 0, sizeof(dayEventHeads));

	g_TimeManager.weekEvents = weekEventHeads;
	memset(weekEventHeads, 0, sizeof(weekEventHeads));

	g_TimeManager.monthEvents = monthEventHeads;
	memset(monthEventHeads, 0, sizeof(monthEventHeads));

	g_TimeManager.yearEvents = yearEventHeads;
	memset(yearEventHeads, 0, sizeof(yearEventHeads));

	g_TimeManager.lastTrammelPhase = 0xFFFFFFFF;
	g_TimeManager.lastFeluccaPhase = 0xFFFFFFFF;
}

/*
 * 0x004D8043 - CTimeManager::CTimeManager (vtable setup)
 *
 * Sets the CTimeManager vtable pointer in the binary. No-op here since
 * CTimeManager does not use a vtable in our port.
 */
static void __attribute__((unused))
CTimeManager_ConstructorVTable(void)
{
}

/*
 * 0x004D805C - CTimeManager::LogEntityStats
 *
 * Logs player, NPC and dynamic item counts when arg==0. The arg==1
 * path is a no-op, matching the binary.
 */
void
CTimeManager_LogEntityStats(int arg)
{
	int playerCount, npcCount, normalNPCCount, nonnormalCount;
	int dynamicCount;
	int timingVal1, timingVal2;
	CString str;

	playerCount = CPlayerList_GetCount() & 0xFFFF;
	npcCount = (int)g_NPCCount;
	normalNPCCount = g_NormalNPCCount;
	nonnormalCount = npcCount - normalNPCCount;
	dynamicCount = (int)g_DynamicItemCount;
	timingVal1 = (int)g_TimingField9F0;
	timingVal2 = (int)g_TimingField9EC;
	USED(timingVal1);
	USED(timingVal2);

	if (arg != 0)
		return;

	CString_DefaultConstructor(&str);
	CString_ConcatInt(&str, playerCount);
	CString_AppendCStr(&str, " players, ");
	CString_ConcatInt(&str, npcCount);
	CString_AppendCStr(&str, " NPCs (");
	CString_ConcatInt(&str, normalNPCCount);
	CString_AppendCStr(&str, " normal, ");
	CString_ConcatInt(&str, nonnormalCount);
	CString_AppendCStr(&str, " nonnormal), ");
	CString_ConcatInt(&str, dynamicCount);
	CString_AppendCStr(&str, " dynamics on ");
	CString_AppendCStr(&str, g_LVNDestBuf);

	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "stats", "misc", CString_GetBuffer(&str));

	CString_Destructor(&str);
}

/*
 * 0x004D81B8 - CTimeManager::Update
 *
 * Main game tick. Runs every subsystem heartbeat (decay, NPC AI, combat,
 * spawn, weather, timers, player, clock, events, light) in the binary's
 * exact order. CUSTOM additions: auto initial-spawn deadline, periodic
 * world save.
 */
void
CTimeManager_Update(void)
{
	uint32_t tickCount;

	// GCM monitoring pre-empts normal ticks.
	if (g_gcmState != 0) {
		GameCentMon_CheckTimeout();
		return;
	}

	TagNodePool_FlushA();
	TagNodePool_FlushB();

	g_TimeManager.currentTime = GetTickCount_UO();

	tickCount = g_TimeManager.tickCount;

	if ((tickCount & 0x3FF) == 0)
		LogTimingStats();
	if ((tickCount & 0x1FFF) == 0)
		CTimeManager_LogEntityStats(0);
	if ((tickCount & 0x3F) == 0)
		CTimeManager_LogEntityStats(1);

	tickCount = (tickCount + 1) & 0x3FFFFFFF;
	g_TimeManager.tickCount = tickCount;
	if ((tickCount & 0x1FFF) == 0)
		CResBankManager_PrintSpawnStatistics();

	if ((tickCount & 0x7FF) == 0) {
		uint8_t tbuf[8];
		PacketManager_MakePacket_GAMETIME(tbuf);
		CPlayerList_SendPacketToAll(tbuf);
	}

	if ((tickCount & 0xF) == 0)
		CDefcon_Update();

	if ((tickCount & 0x7FF) == 0)
		CHintManager_Tick();

	CWorld_DecayTick(tickCount);

	if ((tickCount & 0x7) == 0)
		CNPCHash_HeartbeatTick();

	CNPCHash_RegenMoveTick();

	CMobileManager_CombatHeartBeat(tickCount);

	if (g_IsInitialSpawn)
		CResBankManager_SpawnTick(0, 0);
	if ((tickCount & 0x1F) == 0)
		CResBankManager_SpawnTick(1, 0);

	// Custom: auto-disable initial spawn after deadline.
	if (g_AutoInitialSpawnDeadline != 0 && GetTickCount_UO() >= g_AutoInitialSpawnDeadline) {
		g_IsInitialSpawn = 0;
		g_AutoInitialSpawnDeadline = 0;
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "spawn", "misc", "initial spawn period ended");
	}

	if ((tickCount & 0xFFF) == 0)
		CResBankManager_RespawnTimerCheck();
	CResBankManager_ProcessRespawnChunks();

	// CUSTOM (FEAT_PERNPC_RESPAWN): fire per-NPC home-location respawns
	// whose deadline has elapsed. Runs after RespawnTimerCheck so any
	// FEAT_CLOSED_ECONOMY per-resource budget refund from item decay is
	// in place by the time we retry the spawn at the dead NPC's home tile.
	if (feat(FEAT_PERNPC_RESPAWN))
		PendingNPCRespawn_Tick();

	if ((tickCount & 0x7F) == 0)
		WeatherManager_UpdateWeather();

	if ((tickCount & 0x7FF) == 0)
		CPlayerList_AddSunlight(1);

	if ((tickCount & 0xFFF) == 0)
		CSkillManager_Update();

	TimerManager_ProcessTimers();

	CPlayerList_Heartbeat();

	CTimeManager_Tick();

	TimeManager_DispatchEvents();

	CTimeManager_CheckMoonPhases();

	// Every 0x12C0 ticks: decrement per-mobile light timers and refresh
	// the global light level.
	if ((tickCount % 0x12C0) == 0) {
		CMobile *mob;
		for (mob = g_MobileListHead; mob != NULL; mob = mob->nextMobile) {
			if (mob->lightTime > 0) {
				mob->lightTime--;
				if (mob->lightTime == 0)
					CMobile_SetLight(mob, 0, 0);
			}
		}

		GlobalLightManager_Tick(tickCount / 0x12C0);
	}

	// Custom: periodic world save every 4096 ticks (~17 min). UoDemo
	// ships SaveDynamic0 unwired; this restores a save cadence using
	// the same bitmask pattern as the other periodic subsystems.
	if ((tickCount & 0xFFF) == 0 && tickCount != 0) {
		BackupFile(GLOBAL_file_dynidx0_mul, GLOBAL_file_dynidx0_bkp);
		BackupFile(GLOBAL_file_dynamic0_mul, GLOBAL_file_dynamic0_bkp);
		SaveDynamic0();
		Account_SaveAll();
		// CUSTOM (FEAT_CLOSED_ECONOMY): persist the live resource bank alongside
		// the periodic world save so drained quantities[] survive a restart.
		// No-op in the default build (SaveAll_ResBank forwards to NoOp).
		SaveAll_ResBank();
	}

	// Custom: gamewide resource-regrowth tick every 16384 ticks
	// (~68 min). Applies value3 = min(value1, value3 + value2) to
	// every type=3 resource node on every CItem (skipping eggs and
	// mobiles).
	if (feat(FEAT_RESOURCE_REGROWTH) && (tickCount & 0x3FFF) == 0 && tickCount != 0)
		ResourceRegrowthTick();
}

/*
 * 0x004D8449 - TimeManager_ParseTimeUnit
 *
 * Maps a time unit name ("min", "hour", "day", "week", "month", "year")
 * to its index (0-5) and slot count. Returns 6 with count 0 on unknown.
 */
static int
TimeManager_ParseTimeUnit(const char *unitName, int *outCount)
{
	if (strcmp(unitName, "min") == 0) {
		*outCount = 60;
		return 0;
	}
	if (strcmp(unitName, "hour") == 0) {
		*outCount = 24;
		return 1;
	}
	if (strcmp(unitName, "day") == 0) {
		*outCount = 7;
		return 2;
	}
	if (strcmp(unitName, "week") == 0) {
		*outCount = 4;
		return 3;
	}
	if (strcmp(unitName, "month") == 0) {
		*outCount = 12;
		return 4;
	}
	if (strcmp(unitName, "year") == 0) {
		*outCount = 1;
		return 5;
	}
	*outCount = 0;
	return 6;
}

/*
 * 0x004D853D - TimeManager_MatchTimePattern
 *
 * Matches a decimal value against a right-to-left pattern where '*'
 * wildcards any digit. Returns 1 on match, 0 otherwise.
 */
static int
TimeManager_MatchTimePattern(const char *pattern, int value)
{
	int len;
	const char *p;

	len = (int)strlen(pattern);
	p = pattern + len - 1;
	while (p >= pattern) {
		if (*p == '*') {
			value /= 10;
		} else {
			if ((value % 10) + '0' != *p)
				return 0;
			value /= 10;
		}
		p--;
	}
	return 1;
}

/*
 * 0x004D85A8 - CTimeManager::RegisterTimedEvent
 *
 * Parses a time expression (e.g. "min:3 hour:**") and registers a
 * subscription node on every matching slot of every listed unit.
 */
void
CTimeManager_RegisterTimedEvent(ScriptAttachNode *id, const char *expression)
{
	char buf[256];
	char token[128];
	char unitBuf[256];
	char *pos;
	int i, unitIndex, count;
	CTimeEventNode **eventArray;
	CTimeEventNode *node;

	strcpy(buf, expression);
	pos = GetValue(buf, token);

	while (token[0] != '\0') {
		for (i = 0; token[i] != '\0' && token[i] != ':'; i++)
			;
		if (token[i] != ':')
			break;

		strncpy(unitBuf, token, (size_t)i);
		unitBuf[i] = '\0';

		unitIndex = TimeManager_ParseTimeUnit(unitBuf, &count);
		if (unitIndex == 6)
			break;

		strcpy(unitBuf, &token[i + 1]);

		for (i = 0; i < count; i++) {
			if (!TimeManager_MatchTimePattern(unitBuf, i))
				continue;

			node = (CTimeEventNode *)malloc(sizeof(CTimeEventNode));
			node->id = id;
			node->data = expression;

			eventArray = (&g_TimeManager.minuteEvents)[unitIndex];
			node->next = eventArray[i];
			eventArray[i] = node;
		}

		pos = GetValue(pos, token);
	}
}

/*
 * 0x004D8795 - CTimeManager::UnregisterTimedEvent
 *
 * Parses the same expression format and removes every subscription node
 * matching (id, expression) from each listed slot. Keeps g_savedEventNext
 * in sync so a concurrent DispatchEventList iteration stays valid.
 */
void
CTimeManager_UnregisterTimedEvent(ScriptAttachNode *id, const char *expression)
{
	char buf[256];
	char token[128];
	char unitBuf[256];
	char *pos;
	int i, unitIndex, count;
	CTimeEventNode **eventArray;
	CTimeEventNode **pp;
	CTimeEventNode *node;

	strcpy(buf, expression);
	pos = GetValue(buf, token);

	while (token[0] != '\0') {
		for (i = 0; token[i] != '\0' && token[i] != ':'; i++)
			;
		if (token[i] != ':')
			break;

		strncpy(unitBuf, token, (size_t)i);
		unitBuf[i] = '\0';

		unitIndex = TimeManager_ParseTimeUnit(unitBuf, &count);
		if (unitIndex == 6)
			break;

		strcpy(unitBuf, &token[i + 1]);

		for (i = 0; i < count; i++) {
			if (!TimeManager_MatchTimePattern(unitBuf, i))
				continue;

			eventArray = (&g_TimeManager.minuteEvents)[unitIndex];
			pp = &eventArray[i];

			while (*pp != NULL) {
				node = *pp;

				if (node->id != id || node->data != expression) {
					pp = &node->next;
					continue;
				}

				*pp = node->next;

				// Protect DispatchEventList iteration.
				if (g_savedEventNext == node)
					g_savedEventNext = node->next;

				free(node);
			}
		}

		pos = GetValue(pos, token);
	}
}

/*
 * 0x004D89F1 - CTimeManager::SeasonAdvance
 *
 * Advances the weather season when the month rolls over.
 */
static void
CTimeManager_SeasonAdvance(void)
{
	WeatherManager_AdvanceSeason();
}

/*
 * 0x004D8A06 - CTimeManager::DayAdvance
 *
 * On day rollover, purges day-old posts from every bulletin board.
 */
static void
CTimeManager_DayAdvance(void)
{
	uint32_t newDay;
	CBulletinBoard *bb;

	newDay = g_TimeManager.day + 1;
	if (newDay == 7)
		newDay = 0;

	for (bb = g_BBoardHead; bb != NULL; bb = bb->bbNext) {
		CBulletinBoard_PurgeDayOldPosts(bb, newDay);
	}
}

/*
 * Helper - FindScriptClassByTimerFilter
 *
 * Diagnostic helper for CTimeManager_DispatchEventList. Walks every
 * loaded CScript and returns the first one whose trigHandlers[14]
 * chain owns a CTrigger with filterData == filter. CTriggers are
 * shared between parent and child classes via "inherits" (which
 * pointer-copies trigHandlers heads), so the match is the script
 * class that defines or inherits this exact filter pointer - good
 * enough to identify which script type is leaking subscriptions.
 * Returns NULL if no script class matches (e.g. the CScript was
 * unloaded via TriggerEdit reload).
 */
static const char *
FindScriptClassByTimerFilter(const void *filter)
{
	CScript *sc;
	CTrigger *trig;

	if (filter == NULL)
		return NULL;
	for (sc = g_ScriptManager.head; sc != NULL; sc = sc->nextLoaded) {
		for (trig = (CTrigger *)sc->trigHandlers[14]; trig != NULL; trig = trig->next) {
			if (trig->filterData == filter)
				return sc->name;
		}
	}
	return NULL;
}

/*
 * 0x004D8A53 - CTimeManager::DispatchEventList
 *
 * Walks a time-event subscription list and enqueues event 0x0E for each
 * subscribed entity. Saves the next pointer in g_savedEventNext so the
 * unregister path can keep iteration valid if a node is freed.
 *
 * FIXED: Binary dereferences data->entity unconditionally. If a
 * CTimeEventNode survives past CScriptInstance_Clear (which sets
 * node->entity=NULL and scriptClassPtr=0xABCD before queuing the node
 * for deferred free), the next dispatch SIGSEGVs in CMobile_GetSerial.
 * Fix: skip and log nodes whose entity is NULL or whose attach node has
 * been poisoned. The log resolves node->data (the trigger filterData
 * pointer) back to the owning script class via FindScriptClassByTimerFilter
 * so the leak can be traced to a specific script type.
 */
static void
CTimeManager_DispatchEventList(CTimeEventNode *head)
{
	CTimeEventNode *node;
	ScriptAttachNode *data;
	CMobile *entity;
	uint32_t serial;

	node = head;
	while (node != NULL) {
		g_savedEventNext = node->next;
		data = (ScriptAttachNode *)node->id;
		entity = (CMobile *)data->entity;
		if (entity == NULL || (uintptr_t)data->scriptClassPtr == 0xABCD) {
			char msg[256];
			int poisoned = (uintptr_t)data->scriptClassPtr == 0xABCD;
			const char *scriptName = FindScriptClassByTimerFilter(node->data);
			snprintf(msg, sizeof(msg), "stale subscription node=%p data=%p entity=%p scriptClassPtr=%p script=%s expr='%s'%s", (void *)node, (void *)data,
			        (void *)entity, (void *)data->scriptClassPtr, scriptName != NULL ? scriptName : "(unknown)", node->data != NULL ? node->data : "(null)",
			        poisoned ? " (Clear ran, unregister leaked)" : "");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "time", "stale", msg);
			node = g_savedEventNext;
			continue;
		}
		serial = CMobile_GetSerial(entity);
		EventRingBuffer_Enqueue(serial, 0x0E, (uintptr_t)data);
		node = g_savedEventNext;
	}
}

/*
 * 0x004D8A9A - CTimeManager::Tick
 *
 * Advances the multi-level game clock (seconds -> minutes -> hours ->
 * days -> weeks -> months -> year). At each rollover it dispatches the
 * matching subscription list and triggers day/season advance handlers.
 */
void
CTimeManager_Tick(void)
{
	g_TimeManager.seconds++;
	if (g_TimeManager.seconds >= 0x14) {
		g_TimeManager.seconds = 0;
		CTimeManager_DispatchEventList(g_TimeManager.minuteEvents[g_TimeManager.minute]);

		g_TimeManager.minute++;
		if (g_TimeManager.minute >= 0x3C) {
			g_TimeManager.minute = 0;
			CTimeManager_DispatchEventList(g_TimeManager.hourEvents[g_TimeManager.hour]);

			g_TimeManager.hour++;
			if (g_TimeManager.hour >= 0x18) {
				g_TimeManager.hour = 0;
				CTimeManager_DispatchEventList(g_TimeManager.dayEvents[g_TimeManager.day]);
				CTimeManager_DayAdvance();

				g_TimeManager.day++;
				if (g_TimeManager.day >= 7) {
					g_TimeManager.day = 0;
					CTimeManager_DispatchEventList(g_TimeManager.weekEvents[g_TimeManager.week]);

					g_TimeManager.week++;
					if (g_TimeManager.week >= 4) {
						g_TimeManager.week = 0;
						CTimeManager_DispatchEventList(g_TimeManager.monthEvents[g_TimeManager.month]);
						CTimeManager_SeasonAdvance();

						g_TimeManager.month++;
						if (g_TimeManager.month >= 0x0C) {
							g_TimeManager.month = 0;
							CTimeManager_DispatchEventList(g_TimeManager.yearEvents[0]);
							g_TimeManager.year++;
						}
					}
				}
			}
		}
	}
}

/*
 * 0x004D8C5D - CTimeManager::CheckMoonPhases
 *
 * Detects changes in either moon phase and notifies the light manager,
 * updating the stored last-known phase values.
 */
void
CTimeManager_CheckMoonPhases(void)
{
	int trammelChanged = 0;
	int feluccaChanged = 0;
	int trammelPhase, feluccaPhase;

	trammelPhase = CTimeManager_GetTrammelPhase();
	feluccaPhase = CTimeManager_GetFeluccaPhase();

	if (g_TimeManager.lastTrammelPhase != (uint32_t)trammelPhase) {
		trammelChanged = 1;
		g_TimeManager.lastTrammelPhase = (uint32_t)trammelPhase;
	}

	if (g_TimeManager.lastFeluccaPhase != (uint32_t)feluccaPhase) {
		feluccaChanged = 1;
		g_TimeManager.lastFeluccaPhase = (uint32_t)feluccaPhase;
	}

	if (trammelChanged || feluccaChanged)
		CLightManager_NotifyMoonChange(trammelChanged, feluccaChanged);
}

/*
 * 0x004D8CA0 - client.exe 2.0.8 - TwofishCipher_Init
 *
 * Initializes a streaming Twofish context from a 32-bit seed: builds the
 * 128-bit key by replicating keyParam and primes the keystream buffer by
 * encrypting the identity byte sequence 0x00-0xFF.
 */
void
TwofishCipher_Init(TwofishCipher *tc, uint32_t keyParam)
{
	uint8_t outBuf[256];
	int i;

	memset(tc, 0, sizeof(*tc));
	tc->ki_direction = 0;
	tc->ki_keyLen = 128;
	tc->ki_numRounds = 16;

	tc->ci_mode = 1;

	tc->ki_key32[0] = keyParam;
	tc->ki_key32[1] = keyParam;
	tc->ki_key32[2] = keyParam;
	tc->ki_key32[3] = keyParam;

	reKey(tc);

	for (i = 0; i < 256; i++)
		tc->streamBuf[i] = (uint8_t)i;

	blockEncrypt(tc, tc, tc->streamBuf, 256 * 8, outBuf);
	memcpy(tc->streamBuf, outBuf, 256);

	tc->streamPos = 0;
}

/*
 * 0x004D8CE6 - CTimeManager::GetTickCount
 *
 * Returns the current time manager tick count.
 */
uint32_t
CTimeManager_GetTickCount(void)
{
	return g_TimeManager.tickCount;
}

/*
 * 0x004D8CF7 - CTimeManager::FormatTimestamp
 *
 * Formats the current date/time as "%Y-%m-%d %H:%M" into dest.
 *
 * FIXED: the binary passes an uninitialized time_t to localtime (the
 * timeGetTime return value gets clobbered before the store). We call
 * time() to get a valid timestamp.
 */
void
CTimeManager_FormatTimestamp(CString *dest)
{
	char buf[128];
	time_t t;
	struct tm *tm;

	GetTickCount_UO(); // binary: timeGetTime() return is discarded.
	t = time(NULL);    // FIXED: binary uses uninitialized local here.
	tm = localtime(&t);
	strftime(buf, 128, "%Y-%m-%d %H:%M", tm);
	CString_AssignCStr(dest, buf);
}

/*
 * 0x004D8D9F - Static init for g_eventRingBuffer
 *
 * Constructs the event ring buffer deque during static initialization.
 */
static __attribute__((unused)) void
StaticInit_EventRingBuffer(void)
{
	uint8_t allocByte = 0;
	StdDeque_Constructor(&g_eventRingBuffer, &allocByte);
}

/*
 * 0x004D8DD6 - EventRingBuffer::Enqueue
 *
 * Pushes a new TimeEvent onto the back of the event ring buffer.
 */
void
EventRingBuffer_Enqueue(uint32_t serial, uint32_t eventType, uintptr_t param)
{
	TimeEvent ev;

	TimeEvent_Constructor(&ev, serial, eventType, param);
	StdDeque_push_back(&g_eventRingBuffer, &ev);
}

/*
 * 0x004D8DFF - TimeManager_DispatchEvents
 *
 * Drains the event ring buffer, dispatching each event to the entity
 * resolved from its serial. When <=25 events remain it stops after a
 * 50ms time budget. Returns the number of events actually fired.
 */
int
TimeManager_DispatchEvents(void)
{
	int count;
	uint32_t startTime;
	TimeEvent *front;
	uint32_t serial, eventType;
	uintptr_t param;
	CEntity *entity;

	count = 0;
	startTime = GetTickCount_UO();

	while (EventRingBuffer_Count() > 0) {
		if (EventRingBuffer_Count() <= 25) {
			if (GetTickCount_UO() - startTime > 50)
				break;
		}

		front = EventRingBuffer_Front();
		serial = front->serial;
		eventType = front->eventType;
		param = front->param;
		EventRingBuffer_Pop();

		entity = (CEntity *)CWorld_FindBySerial(g_World, serial);
		if (entity != NULL && param != 0) {
			Entity_ExecuteEvent(entity, (int)eventType, param);
			count++;
		}
	}

	return count;
}

/*
 * 0x004D8EC3 - CTimeManager::PurgeScriptEvents
 *
 * Walks the event ring buffer and zeros the param of every event whose
 * param matches scriptNode, preventing later dispatch. Returns the
 * number of events neutralized.
 */
int
EventRingBuffer_PurgeScriptEvents(ScriptAttachNode *scriptNode)
{
	int purged;
	StdDequeIter iter;
	StdDequeIter localFront;
	StdDequeIter localEnd;
	StdDequeIter *frontResult;
	StdDequeIter *endResult;
	StdDequeIter oldIter;
	TimeEvent *ev;

	purged = 0;
	StdDequeIter_InitZero(&iter);

	frontResult = StdDeque_CopyFrontIter(&g_eventRingBuffer, &localFront);
	iter.first = frontResult->first;
	iter.last = frontResult->last;
	iter.cur = frontResult->cur;
	iter.node = frontResult->node;

	goto check;
loop:
	StdDequeIter_PostInc(&iter, &oldIter, 0);
check:
	endResult = StdDeque_CopyEndIter(&g_eventRingBuffer, &localEnd);
	if (!StdDequeIter_NotEqual(&iter, endResult))
		goto done;

	ev = StdDequeIter_Deref(&iter);
	if (ev->param != (uintptr_t)scriptNode)
		goto loop;

	ev = StdDequeIter_Deref(&iter);
	ev->param = 0;
	purged = purged + 1;

	goto loop;
done:
	return purged;
}

/*
 * Custom - GetTickCount_UO
 *
 * Linux replacement for Win32 timeGetTime(). Returns milliseconds from
 * CLOCK_MONOTONIC.
 */
uint32_t
GetTickCount_UO(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/*
 * Custom - LightTables_Init
 *
 * CUSTOM: populates the hourly and moon-phase light tables that the
 * binary leaves all-zero (killing day/night and moons in the demo).
 * Hourly scale: 0 = full daylight, 30 = full darkness. Moon scale is
 * subtracted from darkness, peaking at 4 per full moon.
 */
void
LightTables_Init(void)
{
	// Hours 0-3 night, 4 pre-dawn, 5 dawn, 6-17 day, 18 dusk,
	// 19 twilight, 20-23 night.
	static const int32_t hourly[24] = { 30, 30, 30, 30, 20, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 20, 30, 30, 30, 30 };
	// Phases new(0), waxing(1,2,3), full(4), waning(3,2,1).
	static const int32_t moon[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };

	memcpy(g_hourlyLightTable, hourly, sizeof(hourly));
	memcpy(g_moonPhaseLightTable, moon, sizeof(moon));
}
