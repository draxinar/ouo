/*
 * Defcon manager - ambient-danger level driving lightning strikes.
 *
 * Tracks a per-region defcon level, advances it on a timer, and
 * triggers effects (lightning bolts, guard arrivals) at random
 * locations when the level rises above a threshold.
 */

#include <stdint.h>
#include <stdlib.h>

#include "dat.h"

#include "container.h"
#include "defcon.h"
#include "egg.h"
#include "multi.h"
#include "npc.h"
#include "player.h"
#include "timer.h"
#include "utils.h"
#include "vtable.h"

static void Defcon_StrikeLightning(CItem *entity); // 0x004D7D1F
static void CDefcon_CheckLevel2(void); // 0x0043733F
static void CDefcon_CheckLevel3(void); // 0x004371D2
static void CDefcon_CheckLevel5(void); // 0x00437096
static void CDefcon_CheckLevel1(void); // 0x00436F1C
static void CDefcon_Level1Stub(int arg); // 0x00436F0F
static void CDefcon_CheckLevel4(void); // 0x00436E08
static int CDefcon_KillNPCs(int killCount); // 0x00436D6C
static CNPC *CDefcon_FindNextNPC(CNPC *npc, int startBucket, int *curBucket); // 0x00436D10
static CNPC *CDefcon_NextHashNPC(CNPC *npc, int startBucket, int *curBucket); // 0x00436CB2
static char *CDefcon_CheckNPCArmageddon(CNPC *npc); // 0x00436C9B
static int CDefcon_IsOverloaded(void); // 0x00436C1A
static int CDefcon_PlayerOverThreshold1(void); // 0x00436BB0

/*
 * 0x00436AC0 - CDefcon::Init
 *
 * Seeds the DEFCON server-load limiter with default thresholds.
 *
 * FIXED: Binary sets maxNPCCount to 0xAF0 (2800). The world save
 * already contains ~6000 NPCs, so VT_DROP_AT_FEET pushes g_NormalNPCCount
 * past the binary's cap immediately on load and CDefcon_IsFull blocks
 * all animated spawns. Raised to 100000 so a populated world still has
 * respawn headroom.
 */
void
CDefcon_Init(void)
{
	g_Defcon.maxNPCCount = 100000;          // binary: 0xAF0 = 2800
	g_Defcon.npcKillCount = 0x64;
	g_Defcon.defcon4PThreshold = 0x3E8;
	g_Defcon.defcon4Cooldown = 0x16;
	g_Defcon.defcon4NPCThreshold = 0x4B;
	g_Defcon.defcon1PThreshold = 0x2BC;
	g_Defcon.defcon3PThreshold = 0x384;
	g_Defcon.defcon5PThreshold = 0x44C;
	g_Defcon.defcon2PThreshold = 0x320;
	g_Defcon.defcon1Active = 1;
	g_Defcon.defcon3Active = 0;
	g_Defcon.defcon2Active = 0;
	g_Defcon.defcon4Timer = 0;
}

/*
 * 0x00436B4F - CDefcon::Update
 *
 * Runs every 16 ticks and dispatches the five DEFCON level checks.
 */
void
CDefcon_Update(void)
{
	CDefcon_CheckLevel4();
	CDefcon_CheckLevel1();
	CDefcon_CheckLevel5();
	CDefcon_CheckLevel3();
	CDefcon_CheckLevel2();
}

/*
 * 0x00436B82 - CDefcon::GetMoveRate
 *
 * Returns 10 when the player count exceeds defcon3PThreshold, else 100.
 */
int
CDefcon_GetMoveRate(CDefcon *defcon)
{
	int playerCount;

	playerCount = CPlayerList_GetCount() & 0xFFFF;
	if (playerCount > defcon->defcon3PThreshold)
		return 10;
	return 100;
}

/*
 * 0x00436BB0 - CDefcon::PlayerOverThreshold1
 *
 * Returns 1 when the online-player count exceeds the DEFCON 1 threshold.
 */
static int
CDefcon_PlayerOverThreshold1(void)
{
	int playerCount;

	playerCount = CPlayerList_GetCount() & 0xFFFF;
	if (playerCount > g_Defcon.defcon1PThreshold)
		return 1;
	return 0;
}

/*
 * 0x00436BDB - CDefcon::IsFull
 *
 * Returns 1 when the NPC or player count is above its threshold.
 */
int
CDefcon_IsFull(void)
{
	if (g_NormalNPCCount > g_Defcon.maxNPCCount)
		return 1;

	if ((CPlayerList_GetCount() & 0xFFFF) > g_Defcon.defcon2PThreshold)
		return 1;

	return 0;
}

/*
 * 0x00436C1A - CDefcon::IsOverloaded
 *
 * Returns 1 when both the player count and the adjusted NPC count
 * exceed their DEFCON 4 thresholds.
 */
static int
CDefcon_IsOverloaded(void)
{
	int playerCount;
	int normalNPCs;

	playerCount = CPlayerList_GetCount() & 0xFFFF;
	if (playerCount <= g_Defcon.defcon4PThreshold)
		return 0;

	normalNPCs = g_NormalNPCCount - g_NPCSubCount1 - g_NPCSubCount2;
	if (normalNPCs <= g_Defcon.defcon4NPCThreshold)
		return 0;

	return 1;
}

/*
 * 0x00436C9B - CDefcon::CheckNPCArmageddon
 *
 * Fires the @armageddon event on npc and returns its result.
 */
static char *
CDefcon_CheckNPCArmageddon(CNPC *npc)
{
	return CNPC_CheckArmageddon(npc, 1);
}

/*
 * 0x00436CB2 - CDefcon::NextHashNPC
 *
 * Walks the NPC hash table, wrapping buckets until a non-empty one
 * is found or startBucket is reached again.
 */
static CNPC *
CDefcon_NextHashNPC(CNPC *npc, int startBucket, int *curBucket)
{
	if (npc != NULL)
		npc = npc->npcHashNext;

	while (npc == NULL) {
		*curBucket = (*curBucket + 1) % 64;
		if (*curBucket == startBucket)
			return npc;
		npc = g_NPCHash[*curBucket];
	}

	return npc;
}

/*
 * 0x00436D10 - CDefcon::FindNextNPC
 *
 * Returns the next NPC whose @armageddon trigger fires, or NULL.
 */
static CNPC *
CDefcon_FindNextNPC(CNPC *npc, int startBucket, int *curBucket)
{
	if (npc == NULL)
		npc = CDefcon_NextHashNPC(npc, startBucket, curBucket);

	while (npc != NULL) {
		if (CDefcon_CheckNPCArmageddon(npc))
			return npc;
		npc = CDefcon_NextHashNPC(npc, startBucket, curBucket);
	}

	return npc;
}

/*
 * 0x00436D6C - CDefcon::KillNPCs
 *
 * Deletes up to killCount NPCs whose @armageddon trigger fires,
 * starting from a random hash bucket.
 */
static int
CDefcon_KillNPCs(int killCount)
{
	int killed;
	CNPC *npc;
	CNPC *savedNext;
	int startBucket;
	int curBucket;

	killed = 0;
	savedNext = NULL;
	startBucket = GetRandomRange(0, 63);
	curBucket = startBucket;
	npc = g_NPCHash[startBucket];

	while (killed < killCount) {
		npc = CDefcon_FindNextNPC(npc, startBucket, &curBucket);
		if (npc == NULL)
			break;
		savedNext = npc->npcHashNext;
		if (npc != NULL)
			((void (*)(void *))VT_FN((CItem *)npc, VT_DELETE))(npc);
		killed++;
		npc = savedNext;
	}

	return killed;
}

/*
 * 0x00436E08 - CDefcon::CheckLevel4
 *
 * DEFCON 4: culls NPCs when the server is overloaded, respecting
 * the cooldown timer.
 */
static void
CDefcon_CheckLevel4(void)
{
	int killed;
	CString str;

	if (g_Defcon.defcon4Timer > 0)
		g_Defcon.defcon4Timer--;
	if (g_Defcon.defcon4Timer != 0)
		return;
	if (!CDefcon_IsOverloaded())
		return;

	killed = CDefcon_KillNPCs(g_Defcon.npcKillCount);
	if (killed <= 0) {
		g_Defcon.defcon4Timer += g_Defcon.defcon4Cooldown;
		return;
	}

	CString_Constructor(&str, "Going to DEFCON 4 (NPC kill) (");
	CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
	CString_AppendCStr(&str, ")");
	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
	CString_Destructor(&str);
}

/*
 * 0x00436F0F - CDefcon no-op stub
 *
 * Called from CheckLevel1 before toggling defcon1Active.
 */
static void
CDefcon_Level1Stub(int activating)
{
	USED(activating);
}

/*
 * 0x00436F1C - CDefcon::CheckLevel1
 *
 * DEFCON 1: toggles player-creation block on player-count threshold.
 */
static void
CDefcon_CheckLevel1(void)
{
	CString str;

	if (g_Defcon.defcon1Active == 1) {
		if (CDefcon_PlayerOverThreshold1()) {
			CDefcon_Level1Stub(1);
			g_Defcon.defcon1Active = 0;

			CString_Constructor(&str, "Going to DEFCON 1 (No player creation) (");
			CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
			CString_AppendCStr(&str, ")");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
	} else if (g_Defcon.defcon1Active == 0) {
		if (!CDefcon_PlayerOverThreshold1()) {
			CDefcon_Level1Stub(0);
			g_Defcon.defcon1Active = 1;

			CString_Constructor(&str, "Standing down from DEFCON 1 (player creation) (");
			CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
			CString_AppendCStr(&str, ")");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
	}
}

/*
 * 0x00437096 - CDefcon::CheckLevel5
 *
 * DEFCON 5: fires a teleport storm when the player count is well
 * above the threshold.
 */
static void
CDefcon_CheckLevel5(void)
{
	int excess;
	CString str;

	excess = (uint16_t)CPlayerList_GetCount() - g_Defcon.defcon5PThreshold;
	if (excess > 0) {
		excess += 5;
		Defcon_TeleportStorm(excess);

		CString_Constructor(&str, "Going to DEFCON 5 (teleport storm) (");
		CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
		CString_AppendCStr(&str, ")");
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
		CString_Destructor(&str);
	}
}

/*
 * 0x004371D2 - CDefcon::CheckLevel3
 *
 * DEFCON 3: toggles NPC slowdown on player-count threshold.
 */
static void
CDefcon_CheckLevel3(void)
{
	int playerCount;
	CString str;

	if (g_Defcon.defcon3Active == 0) {
		playerCount = (uint16_t)CPlayerList_GetCount();
		if (playerCount > g_Defcon.defcon3PThreshold) {
			g_Defcon.defcon3Active = 1;

			CString_Constructor(&str, "Going to DEFCON 3 (NPC slowdown) (");
			CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
			CString_AppendCStr(&str, ")");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
	} else {
		playerCount = (uint16_t)CPlayerList_GetCount();
		if (playerCount <= g_Defcon.defcon3PThreshold) {
			g_Defcon.defcon3Active = 0;

			CString_Constructor(&str, "Standing down from DEFCON 3 (NPC slowdown) (");
			CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
			CString_AppendCStr(&str, ")");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
	}
}

/*
 * 0x0043733F - CDefcon::CheckLevel2
 *
 * DEFCON 2: toggles NPC no-spawn on player-count threshold.
 */
static void
CDefcon_CheckLevel2(void)
{
	int playerCount;
	CString str;

	if (g_Defcon.defcon2Active == 0) {
		playerCount = (uint16_t)CPlayerList_GetCount();
		if (playerCount > g_Defcon.defcon2PThreshold) {
			g_Defcon.defcon2Active = 1;

			CString_Constructor(&str, "Going to DEFCON 2 (NPC no spawn) (");
			CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
			CString_AppendCStr(&str, ")");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
	} else {
		playerCount = (uint16_t)CPlayerList_GetCount();
		if (playerCount <= g_Defcon.defcon2PThreshold) {
			g_Defcon.defcon2Active = 0;

			CString_Constructor(&str, "Standing down from DEFCON 2 (NPC no spawn) (");
			CString_ConcatInt(&str, (uint16_t)CPlayerList_GetCount());
			CString_AppendCStr(&str, ")");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "panic", "gamestatus", CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
	}
}

/*
 * 0x004D7D1F - Defcon_StrikeLightning
 *
 * Strikes lightning on a player and schedules a follow-up timer.
 */
static void
Defcon_StrikeLightning(CItem *entity)
{
	Script_doLightning(entity->serial);
	ScheduleEvent(8, entity->serial, 0x0A, 0, 0);
}

/*
 * 0x004D7D4A - Defcon_TeleportStorm
 *
 * Strikes lightning on up to count random eligible players via a
 * partial Fisher-Yates shuffle.
 */
void
Defcon_TeleportStorm(int count)
{
	CVector vec;
	CPlayer *player;
	CItem *selected;
	int total, i, idx;
	uintptr_t *elems;

	CVector_Constructor(&vec, "");
	player = g_PlayerList.head;
	while (player != NULL) {
		if (!CPlayer_IsEditing(player) && !CEntity_HasTimerEx((CItem *)player, 0x0A, 0)) {
			CVector_PushBack(&vec, (uintptr_t)player);
		}
		player = player->next;
	}
	total = CVector_GetCount(&vec);
	if (count > total)
		count = total;
	for (i = 0; i < count; i++) {
		idx = rand() % total;
		elems = (uintptr_t *)vec.begin;
		selected = (CItem *)elems[idx];
		total -= 1;
		elems[idx] = elems[total];
		Defcon_StrikeLightning(selected);
	}
	CVector_Destructor(&vec);
}
