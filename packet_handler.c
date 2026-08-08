/*
 * Inbound packet dispatch.
 *
 * Owns the handler table indexed by packet id and the HandlePacket_*
 * routines that consume each client packet: login, movement, combat
 * commands, speech, targeting, and so on.
 */

#include <ctype.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "dat.h"

#include "account.h"
#include "bboard.h"
#include "book.h"
#include "chat.h"
#include "combat.h"
#include "container.h"
#include "dynamic.h"
#include "egg.h"
#include "entitymanager.h"
#include "feature.h"
#include "gamecentmon.h"
#include "gm_player_menu.h"
#include "gmedit.h"
#include "help_queue.h"
#include "io.h"
#include "main.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "packet_utils.h"
#include "pending_auth.h"
#include "player.h"
#include "region.h"
#include "resquery.h"
#include "shopkeeper.h"
#include "signpost.h"
#include "skill.h"
#include "taglist.h"
#include "template.h"
#include "time.h"
#include "timer.h"
#include "trade.h"
#include "usersock.h"
#include "utils.h"
#include "version.h"
#include "vtable.h"
#include "weather.h"
#include "wombat_compile.h"
#include "world.h"

static void LastSpell(CPlayer *player); // 0x00447C2E
static CItem *FindSpellInContainer(CItem *container, int spellId); // 0x0044800A
static void OpenSpellbookToSpell(CPlayer *player, char *text); // 0x0044819A
static void DoJail(CItem *victim, CItem *commander); // 0x0044DB60
static void DoTeleport(CItem *source, CItem *target, CLocation *loc); // 0x0044DBC8
static void GetJailLocation(CLocation *out); // 0x0044DC29
static int GetUnjailLocation(CItem *victim, CLocation *outLoc); // 0x0044DCB7
static void DoUnjail(CItem *victim, CItem *commander); // 0x0044DD03
static void DoRelease(CItem *victim, CItem *commander, const char *locName); // 0x0044DD9B
static void GetRandomRecallLocation(CLocation *out); // 0x0044DE2B
static CEditorObj *CEditorObj_Constructor(CEditorObj *this); // 0x0045ACD0
static void CEditorObj_Destructor(CEditorObj *this); // 0x0045AD08
static void SIMPED_Apply(uint32_t startX, uint32_t startY, uint32_t data, uint16_t extentX, uint16_t extentY, uint32_t tileGroupIdx); // 0x0045B4C0
static int SIMPED_FindTileEntry(int groupIdx, int tileID); // 0x0045B67D
static int SIMPED_FindTileGroup(int x, int y, int groupIdx); // 0x0045B6D7
static void StaticInit_EditorTileGroups(void); // 0x004677D6
static void ValidateEntityAccess(CPlayer *player, uint32_t *serialPtr, int useCheck); // 0x004922FF
static void Speech_BroadcastDead(CPlayer *speaker, uint8_t speechType, char *text, uint16_t hue, uint16_t font, uint16_t range); // 0x004935E7
static void Speech_BroadcastAlive(CPlayer *speaker, uint8_t speechType, char *text, uint16_t hue, uint16_t font, uint16_t range); // 0x004938F6
static void PlayDropSound(CItem *item, CItem *container, CItem *entity); // 0x00493FAF
static void SendEquipSound(CItem *entity); // 0x00494080
static int ValidateSerials(CItem *expectedPlayer, uint32_t playerSerial, CItem *expectedItem, uint32_t itemSerial, CItem *expectedTarget, uint32_t targetSerial); // 0x00494C27
static void DropObj_Bounce(CItem *source, CItem *item, CLocation *loc); // 0x00494CAE
static const char *GetCanHoldFailReason(int code); // 0x00494D33
static void SendEntityResourceNodes(CItem *entity, CItem *player, uint32_t unused, uint32_t serial); // 0x00496430
static void BBoard_BroadcastWrapper(uint8_t *buf); // 0x00497AF4
static void BuildGodViewPacket(uint8_t *buf, uint8_t type, uint16_t count, int dataLen, uint8_t *data); // 0x004B4C97
static void TriggerEdit_Op545E(CItem *ent, const char *data); // 0x004B545E
static void TriggerEdit_Op546F(const char *scriptName); // 0x004B546F
static void BuildTriggerEditResponse(uint8_t *buf, uint8_t subtype, uint16_t connIndex, char *data, uint16_t datalen); // 0x004B5640
static void TriggerEdit_DeleteEntity(CItem *entity); // 0x004B5775
static int IsBookGraphic(uint16_t graphic); // 0x004DB296
static void DoorOpen(CItem *door); // 0x004DB2C1
static CItem *FindPairedDoor(CLocation *loc); // 0x004DBB94
static void CastToContainer(CItem **itemPtr); // 0x004DBCC3
static void UseSpellbook(CPlayer *player, CItem *spellbook); // 0x004DBE7B
static void UseScrollCase(CPlayer *player, CItem *scrollcase); // 0x004DC0BC
static void DispatchDoubleClickMobile(CPlayer *player, uint32_t serial, CLocation *loc, CItem *entity, int isPaperdoll); // 0x004DC220
static void UseLight(CItem *entity); // 0x004DC350
void OpenPaperdoll(CPlayer *player, uint32_t playerSerial, CItem *target); // 0x004DC388
static void OpenSpellbook(CPlayer *player, uint32_t playerSerial, CItem *spellbook); // 0x004DC511
static void DoDoubleClick(CPlayer *this, uint32_t playerSerial, CLocation *playerLoc, CItem *entity, int isPaperdoll); // 0x004DC611
static void DeadDoubleClick(CPlayer *this, CItem *entity); // 0x004DCD62
static uint16_t LightToggleLookup(uint16_t graphic);
static int CItem_GetByteProp(CItem *ent); // 0x00490AC3
static int CItem_GetSortKey(CItem *ent); // 0x00490C6D
static const char *TriggerEdit_CString_GetCStr(void *cstr); // 0x004D3387
static void GM_TargetJail(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z); // Custom
static void GM_TargetUnjail(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z); // Custom
static void GM_TargetRelease(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z); // Custom

// Custom - optional release location staged by .release for GM_TargetRelease
static char g_PendingReleaseLoc[64];
// Custom - non-empty flag for g_PendingReleaseLoc (empty string is a valid "no loc")
static int g_PendingReleaseLocSet;

// Custom - shared outbound packet buffer size
static int bufSize = 8192;

// GameCentMon globals (binary: 0x006E7658, 0x006E765C, 0x006E7660)
uint32_t g_gcmState;              // 0x006E7658: monitor state (0=idle, 1=active, 2=broadcast)
CItem *g_gcmTarget;       // 0x006E765C: pointer to monitored player
uint32_t g_gcmTimestamp;          // 0x006E7660: timestamp from timeGetTime

// Binary global at 0x00621A18: GCM version/identifier, init 0xFFFFFFFF.
// Set by fcn.004b83d6, read in BroadcastAll case 0 and BroadcastEvent case 3.
uint32_t g_gcmVersion = 0xFFFFFFFF;

// Binary global at 0x0061D67C: NPC AI timer reset value, default 15.
// Set by BroadcastEvent case 10 (event 11).
uint32_t g_npcAITimerReset = 15;

/*
 * 0x00648318 - Editor tile group table (64 entries, populated by GM tools).
 * Each entry points to a tile group data structure with tile IDs at +0x58
 * and per-slot patterns/indices at +0x158 (0x124 bytes per slot, 64 slots).
 */
CEditorTileGroup *g_EditorTileGroups[64];

/*
 * 0x00645B48 - Static creation flag (used by AllocateItem/CEditorObj_HandleEdit).
 * When set, AllocateItem (0x0048E15F) uses CreateStaticEntity instead of the
 * normal class factory. Set to 1 for edit type 11, reset to 0 after creation.
 */
int g_EditorStaticCreateFlag;

// Timing globals (binary .bss, declared in main.c)

// Map file globals (binary, declared in main.c / packet_manager.c)

// NPC constructor chain (npc.c / resbank.c)

// Binary thiscall struct used by GameCentMon_BuildEntityList (0x004B8992).
// Passed in ecx, accumulates entity resource data for GCM entity queries.
__extension__ typedef struct {
	char *responseBuf;  // 0x00 - response buffer (packet header + data)
	int count;          // 0x04 - number of entries written
	uint32_t serial;    // 0x08 - target player serial
	char *cursor;       // 0x0C - current write position in data area
} GCMEntityListBuilder;

/*
 * Notoriety hue table at 0x0061DB10
 */
const uint16_t g_NotorietyHueTable[] = {
	0x0000, // unused
	0x0059, // Innocent (blue)
	0x003F, // Ally (green)
	0x03B2, // Neutral (gray)
	0x03B2, // Criminal (gray)
	0x0090, // Enemy (orange)
	0x0022, // Murderer (red)
	0x0000, // unused
};

/*
 * 0x00616828 - g_JailLocations (10 entries)
 *
 * GetJailLocation picks rand() % 10 from this table.
 */
static const struct {
	int16_t x, _p0;
	int16_t y, _p1;
	int16_t z, _p2;
} g_JailLocations[10] = {
	{ 5275, 0, 1164, 0, 0, 0 },
	{ 5285, 0, 1164, 0, 0, 0 },
	{ 5295, 0, 1164, 0, 0, 0 },
	{ 5305, 0, 1164, 0, 0, 0 },
	{ 5275, 0, 1174, 0, 0, 0 },
	{ 5285, 0, 1174, 0, 0, 0 },
	{ 5295, 0, 1174, 0, 0, 0 },
	{ 5305, 0, 1174, 0, 0, 0 },
	{ 5275, 0, 1185, 0, 0, 0 },
	{ 5295, 0, 1185, 0, 0, 0 },
};

/*
 * 0x006168A0 - g_RecallLocations (14 entries, one per city)
 *
 * GetRandomRecallLocation picks rand() % 14 from this table.
 */
static const struct {
	int16_t x, _p0;
	int16_t y, _p1;
	int16_t z, _p2;
} g_RecallLocations[14] = {
	{ 1504, 0, 1529, 0, 40, 0 }, /* Britain */
	{ 1632, 0, 1674, 0, 15, 0 }, /* Britain alt */
	{ 1427, 0, 3706, 0, 0, 0 },  /* Jhelom */
	{ 3718, 0, 2185, 0, 20, 0 }, /* Magincia */
	{ 2434, 0, 444, 0, 15, 0 },  /* Minoc */
	{ 4487, 0, 1133, 0, 0, 0 },  /* Moonglow */
	{ 3778, 0, 1202, 0, 0, 0 },  /* Nujel'm */
	{ 3692, 0, 2521, 0, 0, 0 },  /* Ocllo */
	{ 2968, 0, 3363, 0, 15, 0 }, /* Serpent's Hold */
	{ 617, 0, 2145, 0, 0, 0 },   /* Skara Brae */
	{ 1892, 0, 2845, 0, 20, 0 }, /* Trinsic */
	{ 1810, 0, 2720, 0, 8, 0 },  /* Trinsic alt */
	{ 2848, 0, 931, 0, 0, 0 },   /* Vesper */
	{ 615, 0, 984, 0, 0, 0 },    /* Yew */
};

/*
 * 0x0061B038 - g_NamedLocations (.goto command, 38 entries)
 *
 * FindLocation does a case-insensitive linear search over this table.
 */
static const struct {
	const char *name;
	int16_t x, _p0;
	int16_t y, _p1;
	int16_t z, _p2;
} g_NamedLocations[] = {
	{ "jail1", 5275, 0, 1164, 0, 0, 0 },
	{ "jail2", 5285, 0, 1164, 0, 0, 0 },
	{ "jail3", 5295, 0, 1164, 0, 0, 0 },
	{ "jail4", 5305, 0, 1164, 0, 0, 0 },
	{ "jail5", 5275, 0, 1174, 0, 0, 0 },
	{ "jail6", 5285, 0, 1174, 0, 0, 0 },
	{ "jail7", 5295, 0, 1174, 0, 0, 0 },
	{ "jail8", 5305, 0, 1174, 0, 0, 0 },
	{ "jail9", 5275, 0, 1185, 0, 0, 0 },
	{ "jail10", 5295, 0, 1185, 0, 0, 0 },
	{ "britain1", 1504, 0, 1529, 0, 40, 0 },
	{ "guild1", 1504, 0, 1529, 0, 40, 0 },
	{ "britain2", 1631, 0, 1672, 0, 30, 0 },
	{ "guild2", 1632, 0, 1674, 0, 15, 0 },
	{ "jhelom1", 1427, 0, 3706, 0, 0, 0 },
	{ "guild3", 1427, 0, 3706, 0, 0, 0 },
	{ "magincia1", 3718, 0, 2185, 0, 20, 0 },
	{ "guild4", 3718, 0, 2185, 0, 20, 0 },
	{ "minoc1", 2501, 0, 390, 0, 0, 0 },
	{ "guild5", 2501, 0, 390, 0, 0, 0 },
	{ "moonglow1", 4487, 0, 1133, 0, 0, 0 },
	{ "guild6", 4487, 0, 1133, 0, 0, 0 },
	{ "nujelm1", 3778, 0, 1202, 0, 0, 0 },
	{ "guild7", 3778, 0, 1202, 0, 0, 0 },
	{ "ocllo", 3692, 0, 2521, 0, 0, 0 },
	{ "guild8", 3692, 0, 2521, 0, 0, 0 },
	{ "serpent1", 2968, 0, 3363, 0, 15, 0 },
	{ "guild9", 2968, 0, 3363, 0, 15, 0 },
	{ "skara1", 617, 0, 2145, 0, 0, 0 },
	{ "guild10", 617, 0, 2145, 0, 0, 0 },
	{ "trinsic1", 1892, 0, 2845, 0, 20, 0 },
	{ "guild11", 1892, 0, 2845, 0, 20, 0 },
	{ "trinsic2", 1810, 0, 2720, 0, 8, 0 },
	{ "guild12", 1810, 0, 2720, 0, 8, 0 },
	{ "vesper1", 2848, 0, 931, 0, 0, 0 },
	{ "guild13", 2848, 0, 931, 0, 0, 0 },
	{ "yew1", 615, 0, 984, 0, 0, 0 },
	{ "guild14", 615, 0, 984, 0, 0, 0 },
	{ NULL, 0, 0, 0, 0, 0, 0 },
};

/*
 * 0x00447A91 - NamedResource_Find
 *
 * Linear-search g_NamedResources for an entry matching name;
 * returns its data pointer or NULL.
 */
char *
NamedResource_Find(char *name)
{
	int i;

	i = 0;
	while (g_NamedResources[i].name != NULL) {
		if (strcmp(name, g_NamedResources[i].name) == 0)
			return g_NamedResources[i].data;
		i++;
	}
	return NULL;
}

/*
 * 0x00447AE9 - NamedResource_LoadAll
 *
 * Reloads every g_NamedResources entry from its file, replacing any
 * prior buffer with a NUL-terminated slurp of the file contents.
 */
void
NamedResource_LoadAll(void)
{
	int i;
	FILE *f;
	int size;
	int pos;
	int ch;

	i = 0;
	while (g_NamedResources[i].name != NULL) {
		if (g_NamedResources[i].data != NULL) {
			free(g_NamedResources[i].data);
			g_NamedResources[i].data = NULL;
		}

		f = fopen_ServerSide(g_NamedResources[i].path, "r");
		if (f != NULL) {
			fseek_ServerSide(f, 0, 2);
			size = ftell_ServerSide(f);
			if (size != 0) {
				fseek_ServerSide(f, 0, 0);
				g_NamedResources[i].data = malloc(size + 1);
				pos = 0;
				while (!feof_ServerSide(f)) {
					ch = fgetc_ServerSide(f);
					g_NamedResources[i].data[pos] = ch;
					pos++;
				}
				g_NamedResources[i].data[pos - 1] = '\0';
			}
			fclose_ServerSide(f);
		}
		i++;
	}
}

/*
 * 0x00447C2E - LastSpell
 *
 * Sends "Memory debugging not enabled" as a system message.
 */
static void
LastSpell(CPlayer *player)
{
	CPlayer_SystemMessage(player, "Memory debugging not enabled");
}

/*
 * 0x0044800A - FindSpellInContainer
 *
 * Walks a container's contents list, comparing each child's tiledata
 * sort key (via CItem_GetSpellId at 0x00490DA4 -> CItem_GetSortKey)
 * masked to 16 bits against spellId. Returns the matching child or NULL.
 */
static CItem *
FindSpellInContainer(CItem *container, int spellId)
{
	CItem *child;

	child = ((CContainer *)container)->contents;
	while (child != NULL) {
		if ((CItem_GetSortKey(child) & 0xffff) == spellId)
			return child;
		child = child->spatialNext;
	}
	return NULL;
}

/*
 * 0x0044819A - OpenSpellbookToSpell
 *
 * Parses "spellId spellbookSerial" out of text, finds the matching
 * spell in that spellbook, and fires UseItem on it. Sends "You do not
 * have that spell!" on any miss.
 */
static void
OpenSpellbookToSpell(CPlayer *player, char *text)
{
	int spellId;
	uint32_t serial;
	CItem *spellbook;
	CItem *child;

	sscanf(text, "%d %d", &spellId, (int *)&serial);

	spellbook = CWorld_FindBySerial(g_World, serial);
	if (spellbook == NULL) {
		CPlayer_SystemMessage(player, "You do not have that spell!");
		return;
	}

	child = ((CContainer *)spellbook)->contents;
	while (child != NULL) {
		if ((CItem_GetSortKey(child) & 0xffff) == (uint32_t)spellId) {
			Entity_ExecuteEvent(&child->resourceEntity.entity, UseItem, (uintptr_t)((CItem *)player)->serial);
			return;
		}
		child = child->spatialNext;
	}

	CPlayer_SystemMessage(player, "You do not have that spell!");
}

/*
 * 0x004484B4 - CPlayer::MacroHandler_Stub1
 *
 * Macro subtype 1 handler: no-op.
 */
void
CPlayer_MacroHandler_Stub1(CPlayer *self, uint32_t arg1, uint32_t arg2)
{
	USED(self);
	USED(arg1);
	USED(arg2);
}

/*
 * 0x004484C1 - CPlayer::MacroHandler_Stub2
 *
 * Macro subtype 2 handler: no-op.
 */
void
CPlayer_MacroHandler_Stub2(CPlayer *self, uint32_t arg1, uint32_t arg2)
{
	USED(self);
	USED(arg1);
	USED(arg2);
}

/*
 * 0x004484CE - CPlayer::MacroHandler_Stub3
 *
 * Macro subtype 3 handler: no-op.
 */
void
CPlayer_MacroHandler_Stub3(CPlayer *self, uint32_t arg1, uint32_t arg2)
{
	USED(self);
	USED(arg1);
	USED(arg2);
}

/*
 * 0x004484DB - LVN hostname/IP processor
 *
 * Builds "I am REGION HOSTNAME IP" into str using gethostname
 * and gethostbyname on the LVN destination.
 */
void
LoadAll_LVN_Hostname(CString *str)
{
	char name[256];
	struct hostent *he;
	uint32_t ip, ipCopy;

	CString_AssignCStr(str, "I am ");
	CString_AppendCStr(str, g_LVNDestBuf);

	gethostname(name, 0x100);
	CString_AppendCStr(str, " ");
	CString_AppendCStr(str, name);

	he = gethostbyname(name);
	if (he != NULL) {
		ip = ntohl(*(uint32_t *)he->h_addr_list[0]);
	} else {
		ip = 0;
	}

	ipCopy = ip;
	sprintf(name, "%d.%d.%d.%d", (ipCopy >> 24) & 0xFF, (ipCopy >> 16) & 0xFF, (ipCopy >> 8) & 0xFF, ipCopy & 0xFF);
	CString_AppendCStr(str, " ");
	CString_AppendCStr(str, name);
}

/*
 * 0x004485E7 - ShowIds
 *
 * Shows nearby entity serials and names within range 18. Iterates
 * spatial grid blocks, walks entity chains, checks VT_IS_MOBILE,
 * distance, then sends "serial: name" via VT_SAY_TO_ENTITY.
 */
void
ShowIds(CItem *player)
{
	int blockBuf[0x400];
	CLocation loc;
	int range;
	int i;
	CItem *ent;
	char buf[128];

	range = 18;
	CLocation_SetLoc(&loc, CEntity_GetLocation(&player->resourceEntity.entity));
	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &loc, range, blockBuf, 0x400);

	for (i = 0; blockBuf[i] != -1; i++) {
		ent = g_MapBlocks[blockBuf[i]].itemHead;
		while (ent != NULL) {
			if (!VT_IsMobile(ent)) {
				ent = ent->spatialNext;
				continue;
			}
			if (CLocation_ChebyshevDistance(&loc, CEntity_GetLocation(&ent->resourceEntity.entity)) >= range) {
				ent = ent->spatialNext;
				continue;
			}
			sprintf(buf, "%u: %s", CMobile_GetSerial((CMobile *)ent), ((const char *(*)(void *, int))VT_FN(ent, VT_SPEAK_SYS_MSG))(ent, 1));
			((void (*)(CItem *, CItem *, uint32_t, const char *))VT_FN(ent, VT_SAY_TO_ENTITY))(ent, player, CMobile_GetSerial((CMobile *)player), buf);
			ent = ent->spatialNext;
		}
	}
}
/*
 * 0x0044A7A0 - CloseTrade
 *
 * Returns items, tears down the trade session, and frees the struct
 * when deleteFlag & 1.
 */
CTradeSession *
CloseTrade(CTradeSession *session, int deleteFlag)
{
	CTradeSession_ReturnItems(session);
	if (deleteFlag & 1)
		free(session);
	return NULL;
}

/*
 * 0x0044DA10 - GetCountVictim
 *
 * Looks up the counselor's current victim entity. Reads "counVictim"
 * tag (type 4 = OBJ serial) from the entity's tag list, resolves the
 * serial via CWorld_FindBySerial, and checks VT_IS_PLAYER. Returns
 * the entity pointer if found and is a player, NULL otherwise.
 */
CItem *
GetCountVictim(CItem *entity)
{
	if (!CResourceEntity_HasTag(entity, "counVictim", 4))
		return NULL;
	uint32_t serial = 0;
	CResourceEntity_GetTagObj(entity, "counVictim", &serial);
	CItem *found = CWorld_FindBySerial(g_World, serial);
	if (found == NULL)
		return NULL;
	if (!VT_IsPlayer(found))
		return NULL;
	return found;
}

/*
 * 0x0044DA72 - GetCountVictimTag
 *
 * Reads the "counVictim" OBJ tag (type 4) from entity's tag list
 * into *outSerial. Returns 1 if found, 0 if not.
 */
int
GetCountVictimTag(CItem *entity, uint32_t *outSerial)
{
	if (!CResourceEntity_HasTag(entity, "counVictim", 4))
		return 0;
	*outSerial = 0;
	CResourceEntity_GetTagObj(entity, "counVictim", outSerial);
	return 1;
}

/*
 * 0x0044DAAD - SetCountVictim
 *
 * Sets the "counVictim" OBJ tag on entity. If the tag already exists,
 * attempts to detach "couvVictim" first - note the binary has a typo
 * ("couv" instead of "coun") so this detach is always a no-op.
 */
int
SetCountVictim(CItem *entity, uint32_t serial)
{
	if (CResourceEntity_HasTag(entity, "counVictim", 4))
		CResourceEntity_DetachScript(entity, "couvVictim");
	CEntity_SetObjVar(entity, "counVictim", 4, serial);
	return 1;
}

/*
 * 0x0044DAEA - ClearCountVictim
 *
 * Removes the "counVictim" tag from entity. Returns 1 if removed,
 * 0 if no tag was present.
 */
int
ClearCountVictim(CItem *entity)
{
	if (!CResourceEntity_HasTag(entity, "counVictim", 4))
		return 0;
	CResourceEntity_DetachScript(entity, "counVictim");
	return 1;
}

/*
 * 0x0044DB18 - ShowEntityLocation
 *
 * Formats entity's location as "(%d, %d, %d)" and sends it as a
 * system message to the entity (assumed to be a player).
 */
void
ShowEntityLocation(CItem *entity)
{
	char buf[512];
	CLocation *loc = &entity->resourceEntity.entity.location;
	sprintf(buf, "(%d, %d, %d)", (int)loc->x, (int)loc->y, (int)loc->z);
	CPlayer_SystemMessage((CPlayer *)entity, buf);
}

/*
 * 0x0044DB60 - DoJail
 *
 * Saves the victim's location as the "UnJailLoc" tag, re-arms the
 * "jailcheck" script, picks a random jail cell, and teleports the
 * commander and victim to it.
 */
static void
DoJail(CItem *victim, CItem *commander)
{
	CLocation loc;
	CLocation_Init(&loc);
	CEntity_SetObjVar(victim, "UnJailLoc", 3, (uintptr_t)&victim->resourceEntity.entity.location);
	CResourceEntity_RemoveScript(victim, "jailcheck");
	Entity_AttachScript(victim, "jailcheck", 1);
	GetJailLocation(&loc);
	DoTeleport(commander, victim, &loc);
}

/*
 * 0x0044DBC8 - DoTeleport
 *
 * Teleports source and/or target entities to a location using
 * VT_TRANSFER_TO (vtable[0xB0]). If source teleport fails, sends
 * error to source and returns without teleporting target. Skips
 * target if same as source or NULL.
 */
static void
DoTeleport(CItem *source, CItem *target, CLocation *loc)
{
	if (source != NULL) {
		int ok = ((int (*)(CItem *, CLocation *))VT_FN(source, VT_TRANSFER_TO))(source, loc);
		if (!ok) {
			CPlayer_SystemMessage((CPlayer *)source, "Invalid transfer location");
			return;
		}
	}
	if (source == target)
		return;
	if (target == NULL)
		return;
	if (!((int (*)(CItem *, CLocation *))VT_FN(target, VT_TRANSFER_TO))(target, loc)) {
		CPlayer_SystemMessage((CPlayer *)source, "Invalid transfer location");
	}
}

/*
 * 0x0044DC29 - GetJailLocation
 *
 * Picks a random entry from the 10-cell jail table and writes the
 * coordinates into the output CLocation.
 */
static void
GetJailLocation(CLocation *out)
{
	int idx = rand() % 10;
	CLocation_Set(out, g_JailLocations[idx].x, g_JailLocations[idx].y, g_JailLocations[idx].z);
}

/*
 * 0x0044DC6E - DoJailCommand (MODIFIED)
 *
 * Wrapper called from speech handler. Resolves counselor's current
 * victim via GetCountVictim, sends system message, calls DoJail.
 * When no current victim is set, arms a target cursor via
 * GM_TargetJail so the counselor can click a player directly.
 */
void
DoJailCommand(CItem *commander)
{
	CItem *victim = GetCountVictim(commander);
	if (victim != NULL) {
		CPlayer_SystemMessage((CPlayer *)commander, "Jailing player");
		DoJail(victim, commander);
		return;
	}
	CPlayer *p = (CPlayer *)commander;
	uint8_t tbuf[20];
	p->targetCallback = GM_TargetJail;
	PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
	SendPacketToPlayer(p, tbuf, -1);
	CPlayer_SystemMessage(p, "Select player to jail");
}

/*
 * 0x0044DCB7 - GetUnjailLocation
 *
 * Removes the "jailcheck" script from victim. When victim has an
 * "UnJailLoc" location tag (type 3), copies it into *outLoc, removes the
 * tag, and returns 1. Returns 0 when no unjail location is stored.
 */
static int
GetUnjailLocation(CItem *victim, CLocation *outLoc)
{
	CResourceEntity_RemoveScript(victim, "jailcheck");
	if (CResourceEntity_HasTag(victim, "UnJailLoc", 3)) {
		CResourceEntity_GetTagLoc(victim, "UnJailLoc", outLoc);
		CResourceEntity_DetachScript(victim, "UnJailLoc");
		return 1;
	}
	return 0;
}

/*
 * 0x0044DD03 - DoUnjail
 *
 * Retrieves victim's saved unjail location via GetUnjailLocation.
 * If found, teleports both commander and victim there. If not
 * found, sends error to commander.
 */
static void
DoUnjail(CItem *victim, CItem *commander)
{
	CLocation loc;
	CLocation_Init(&loc);
	if (!GetUnjailLocation(victim, &loc)) {
		if (commander != NULL)
			CPlayer_SystemMessage((CPlayer *)commander, "Victim has no unjail location set, use release.");
	} else {
		DoTeleport(commander, victim, &loc);
	}
}

/*
 * 0x0044DD52 - DoUnjailCommand (MODIFIED)
 *
 * Wrapper called from speech handler. Resolves counselor's current
 * victim via GetCountVictim, sends system message, calls DoUnjail.
 * When no current victim is set, arms a target cursor via
 * GM_TargetUnjail so the counselor can click a player directly.
 */
void
DoUnjailCommand(CItem *commander)
{
	CItem *victim = GetCountVictim(commander);
	if (victim != NULL) {
		CPlayer_SystemMessage((CPlayer *)commander, "Unjailing player");
		DoUnjail(victim, commander);
		return;
	}
	CPlayer *p = (CPlayer *)commander;
	uint8_t tbuf[20];
	p->targetCallback = GM_TargetUnjail;
	PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
	SendPacketToPlayer(p, tbuf, -1);
	CPlayer_SystemMessage(p, "Select player to unjail");
}

/*
 * 0x0044DD9B - DoRelease
 *
 * Releases victim from jail. If locName is provided, tries
 * FindLocation; otherwise picks random recall location. Cleans up
 * jail tags via GetUnjailLocation (result discarded), then teleports
 * both commander and victim to the target location.
 */
static void
DoRelease(CItem *victim, CItem *commander, const char *locName)
{
	int found = 0;
	CLocation loc;
	CLocation_Init(&loc);
	if (locName != NULL) {
		if (FindLocation(locName, &loc)) {
			found = 1;
		} else {
			CPlayer_SystemMessage((CPlayer *)commander, "Invalid release location");
		}
	} else {
		GetRandomRecallLocation(&loc);
		found = 1;
	}
	if (found) {
		CLocation dummy;
		CLocation_Init(&dummy);
		GetUnjailLocation(victim, &dummy);
		DoTeleport(commander, victim, &loc);
	}
}

/*
 * 0x0044DE2B - GetRandomRecallLocation
 *
 * Picks a random entry from the 14-city recall table and writes the
 * coordinates into the output CLocation.
 */
static void
GetRandomRecallLocation(CLocation *out)
{
	int idx = rand() % 14;
	CLocation_Set(out, g_RecallLocations[idx].x, g_RecallLocations[idx].y, g_RecallLocations[idx].z);
}

/*
 * 0x0044DE70 - DoReleaseCommand (MODIFIED)
 *
 * Wrapper called from speech handler. Resolves counselor's current
 * victim via GetCountVictim, sends system message, calls DoRelease
 * with optional location name argument. When no current victim is
 * set, stages the location name and arms a target cursor via
 * GM_TargetRelease so the counselor can click a player directly.
 */
void
DoReleaseCommand(CItem *commander, const char *locName)
{
	CItem *victim = GetCountVictim(commander);
	if (victim != NULL) {
		CPlayer_SystemMessage((CPlayer *)commander, "Releasing player");
		DoRelease(victim, commander, locName);
		return;
	}
	if (locName != NULL && locName[0] != '\0') {
		strncpy(g_PendingReleaseLoc, locName, sizeof(g_PendingReleaseLoc) - 1);
		g_PendingReleaseLoc[sizeof(g_PendingReleaseLoc) - 1] = '\0';
		g_PendingReleaseLocSet = 1;
	} else {
		g_PendingReleaseLoc[0] = '\0';
		g_PendingReleaseLocSet = 0;
	}
	CPlayer *p = (CPlayer *)commander;
	uint8_t tbuf[20];
	p->targetCallback = GM_TargetRelease;
	PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
	SendPacketToPlayer(p, tbuf, -1);
	CPlayer_SystemMessage(p, "Select player to release");
}

/*
 * 0x006264B0 - g_CheckerGraphics
 *
 * Piece graphics for the checkers minigame: 16 white tiles (0x3584)
 * followed by 16 black tiles (0x358B).
 */
// clang-format off
static const uint16_t g_CheckerGraphics[32] = {
	0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584,
	0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584,
	0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B,
	0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B,
};
// clang-format on

/*
 * Not present on UoDemo, but required on clients >= 1.25.35.
 * The order should match the hardcoded list in clients >= 1.26.0.
 */
PlaceName g_PlaceNameList[] = {
	{ "Ocllo", "Bountiful Harvest", 341, 316, 0 },
	{ "Minoc", "Tavern", 2477, 411, 15 },
	{ "Britain", "Sweet Dreams Inn", 1495, 1629, 10 },
	{ "Moonglow", "Docks", 4406, 1045, 0 },
	{ "Trinsic", "West Gate", 1832, 2779, 0 },
	{ "Yew", "Center", 545, 982, 0 },
	{ "Magincia", "Docks", 3675, 2259, 26 },
	{ "Jhelom", "Docks", 1492, 3696, 0 },
	{ "Skara Brae", "Docks", 639, 2236, 0 },
	{ "Vesper", "Ironwood Inn", 2771, 977, 0 },
};

int g_PlaceNameCount = nelem(g_PlaceNameList);

/*
 * 0x0045ACD0 - CEditorObj::CEditorObj
 *
 * Zeroes the 64 g_EditorTileGroups slots.
 */
static CEditorObj *
CEditorObj_Constructor(CEditorObj *this)
{
	int i;

	for (i = 0; i < 64; i++)
		g_EditorTileGroups[i] = NULL;
	return this;
}

/*
 * 0x0045AD08 - CEditorObj::~CEditorObj
 *
 * No-op destructor, registered via atexit.
 */
static __attribute__((unused)) void
CEditorObj_Destructor(CEditorObj *this)
{
	USED(this);
}

/*
 * 0x0045AD13 - CEditorObj::HandleEdit
 *
 * GM editor dispatch for HandlePacket_EDIT. Cases: 0 reports the
 * outdated-form message; 4/11 create a dynamic or static item;
 * 5 removes/replaces the highest item at the target tile; 6 toggles
 * hidden on the player; 7 spawns an NPC from a template.
 */
void
CEditorObj_HandleEdit(CPlayer *player, int type, int16_t x, int16_t y, int8_t z, uint16_t id, uint16_t hue)
{
	CLocation loc;
	CItem *item;
	CString nameStr, logStr;
	int blockIdx;
	int z_val;
	int isDynamic;
	int creatureHeight;
	int terrainZ;
	CItem *npc;

	CLocation_Init(&loc);

	if (!CPlayer_IsEditing(player))
		return;

	switch (type & 0xFF) {
	case 0:
		CConfig_NoOp_4688C1("Outdated form of editing.");
		break;

	case 5: {
		// Place/replace static: find highest item and remove it.
		CLocation_Set(&loc, x, y, 0);
		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x & 0xFFFF, y & 0xFFFF))
			break;

		item = FindHighestItemAtXY(g_World, loc.x, loc.y);
		if (item != NULL) {
			if (VT_IsContainer(item)) {
				if (VT_IsPlayer(item))
					break;
			}
			if (VT_IsContainer(item)) {
				// Delete container via destructor
				CItem *mob = item;
				if (mob != NULL) {
					((CItem * (*)(void *, int)) VT_FN(mob, VT_DTOR))(mob, 1);
				}
			} else {
				FreeStaticItem(item);
			}
		}
		break;
	}

	case 4:
	case 11: {
		// Create item (dynamic for type 4, static for type 11).
		blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x & 0xFFFF, y & 0xFFFF, 0);
		USED(blockIdx); // binary computes but discards

		z_val = (int)(int8_t)z;
		CLocation_Set(&loc, x, y, (int16_t)z_val);

		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x & 0xFFFF, y & 0xFFFF))
			break;

		if ((type & 0xFF) == 0x0B)
			g_EditorStaticCreateFlag = 1;

		Static_Lock();

		isDynamic = ((type & 0xFF) != 4) ? 1 : 0;

		if (isDynamic && g_EditorStaticCreateFlag)
			item = (CItem *)CreateStaticEntity();
		else
			item = CWorld_CreateItem(g_World, id);

		g_EditorStaticCreateFlag = 0;

		if (item == NULL) {
			Static_Unlock();
			break;
		}

		CString_Constructor(&nameStr, ((const char *(*)(void *, int))VT_FN(item, VT_SPEAK_SYS_MSG))(item, 1));

		CEntity_SetBodyType(item, id);

		// Set hue - use arg if nonzero, else tiledata default
		if ((hue & 0xFFFF) != 0) {
			item->resourceEntity.entity.color = hue;
		} else {
			item->resourceEntity.entity.color = g_ItemTileData[id & 0xFFFF].value2;
		}

		if (VT_IsContainer(item)) {
			CLocation_SetLoc((CLocation *)&item->resourceEntity.nextInContainer, &loc);
			CItem_Setup(item, 0, &loc, 0, 1);
		}

		((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, &loc);

		((void (*)(void *))VT_FN(item, VT_REATTACH_SPATIAL))(item);

		Static_Unlock();

		if ((type & 0xFF) == 4) {
			if (!ValidateInWorld(item))
				item = NULL;
		}

		CString_Constructor(&logStr, "obj create ");
		CString_ConcatInt(&logStr, id & 0xFFFF);
		CString_AppendCStr(&logStr, " \"");
		CString_ConcatCString(&logStr, &nameStr);
		CString_AppendCStr(&logStr, "\" (");
		CString_ConcatInt(&logStr, (int)(int16_t)loc.x);
		CString_AppendCStr(&logStr, ", ");
		CString_ConcatInt(&logStr, (int)(int16_t)loc.y);
		CString_AppendCStr(&logStr, ", ");
		CString_ConcatInt(&logStr, (int)(int16_t)loc.z);
		CString_AppendCStr(&logStr, ")");

		EventLogger_Log(&g_EventLogger, player->accountNum, (uint32_t)(uint8_t)player->characterNum, CMobile_GetSerial((CMobile *)player),
		        ((const char *(*)(void *))VT_FN((CItem *)player, VT_GET_NAME))((CItem *)player), "godcommand", "misc", CString_GetBuffer(&logStr));

		CString_Destructor(&logStr);
		CString_Destructor(&nameStr);
		break;
	}

	case 7: {
		// Create NPC from template.
		CLocation_Set(&loc, x, y, (int16_t)(int8_t)z);

		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x & 0xFFFF, y & 0xFFFF))
			break;

		creatureHeight = GetCreatureHeight(id & 0xFFFF);

		terrainZ = CTerrainManager_CanWalkWrapper(loc, -128, 127, 16, creatureHeight, NULL, 0);

		CString_DefaultConstructor(&nameStr);

		if (terrainZ != (int)(int8_t)0x80) {
			loc.z = (int16_t)terrainZ;

			npc = CTemplateManager_CreateFromTemplate(id & 0xFFFF, &loc, 0, 2, NULL);

			if (npc != NULL) {
				CString_AssignCStr(&nameStr, ((const char *(*)(void *, int))VT_FN(npc, VT_SPEAK_SYS_MSG))(npc, 1));
			}
		}

		CString_Constructor(&logStr, "npc create ");
		CString_ConcatInt(&logStr, id & 0xFFFF);
		CString_AppendCStr(&logStr, " \"");
		CString_ConcatCString(&logStr, &nameStr);
		CString_AppendCStr(&logStr, "\"  (");
		CString_ConcatInt(&logStr, (int)(int16_t)loc.x);
		CString_AppendCStr(&logStr, ", ");
		CString_ConcatInt(&logStr, (int)(int16_t)loc.y);
		CString_AppendCStr(&logStr, ", ");
		CString_ConcatInt(&logStr, (int)(int16_t)loc.z);
		CString_AppendCStr(&logStr, ")");

		EventLogger_Log(&g_EventLogger, player->accountNum, (uint32_t)(uint8_t)player->characterNum, CMobile_GetSerial((CMobile *)player),
		        ((const char *(*)(void *))VT_FN((CItem *)player, VT_GET_NAME))((CItem *)player), "godcommand", "misc", CString_GetBuffer(&logStr));

		CString_Destructor(&logStr);
		CString_Destructor(&nameStr);
		break;
	}

	case 6:
		CPlayer_SetHiddenFlag(player, x & 0xFFFF);
		// falls through to default
	default:
		// Cases 1,2,3,8,9,10: no-op
		break;
	}
}

/*
 * 0x0045B4C0 - SIMPED_Apply
 *
 * GM terrain editor: applies random terrain tiles from a tile group
 * to a rectangular area. For each (x,y) in the rectangle, finds the
 * matching tile group slot via SIMPED_FindTileGroup, counts valid
 * tile indices in that slot, picks one randomly, and sets the terrain
 * tile via SetTerrainTile. SuppressUpdates/RestoreUpdates bracket the
 * operation. Special modes: tileGroupIdx == -1 skips tile application,
 * tileGroupIdx == -2 additionally calls RestoreUpdates before starting.
 */
static void
SIMPED_Apply(uint32_t startX, uint32_t startY, uint32_t data, uint16_t extentX, uint16_t extentY, uint32_t tileGroupIdx)
{
	int x, y;
	int groupResult;
	int count, i;
	int randIdx;
	int tileId;
	CEditorTileGroup *group;

	USED(data);

	if (tileGroupIdx == 0xFFFFFFFE)
		World_RestoreUpdates();

	World_SuppressUpdates();

	for (x = (int)startX; x < (int)(startX + (extentX & 0xFFFF)); x++) {
		for (y = (int)startY; y < (int)(startY + (extentY & 0xFFFF)); y++) {
			if (tileGroupIdx == 0xFFFFFFFF)
				continue;

			groupResult = SIMPED_FindTileGroup(x, y, (int)tileGroupIdx);
			if (groupResult == -1)
				continue;

			// Count valid tile indices in this slot
			count = 0;
			for (i = 0; i < 0x40; i++) {
				group = g_EditorTileGroups[tileGroupIdx];
				if (group == NULL)
					continue;
				if (group->slots[groupResult].tileChoices[i] != -1)
					count++;
			}

			if (count == 0)
				continue;

			// Pick random tile from valid entries
			group = g_EditorTileGroups[tileGroupIdx];

			randIdx = GetRandomRange(0, count);
			tileId = group->slots[groupResult].tileChoices[randIdx];

			if (tileId == -1)
				continue;

			SetTerrainTile(0, x, y, tileId, (int)0xFFFFFD66);
		}
	}
}

/*
 * 0x0045B625 - SIMPED_FreeTileGroups
 *
 * Frees every loaded editor tile group and clears its slot.
 */
static __attribute__((unused)) void
SIMPED_FreeTileGroups(void)
{
	int i;

	for (i = 0; i < 0x40; i++) {
		if (g_EditorTileGroups[i] != NULL) {
			OperatorDelete(g_EditorTileGroups[i]);
			g_EditorTileGroups[i] = NULL;
		}
	}
}

/*
 * 0x0045B67D - SIMPED_FindTileEntry
 *
 * Searches a tile group's tile ID array (64 entries at entry+0x58)
 * for a matching tile ID. Returns the index (0-63) or -1 if not found.
 * Tile IDs are masked with 0x7FFF before comparison.
 */
static int
SIMPED_FindTileEntry(int groupIdx, int tileID)
{
	int i;
	CEditorTileGroup *group;

	for (i = 0; i < 0x40; i++) {
		group = g_EditorTileGroups[groupIdx];
		if (group == NULL)
			continue;
		if ((group->tileIds[i] & 0x7FFF) == (tileID & 0x7FFF))
			return i;
	}
	return -1;
}

/*
 * 0x0045B6D7 - SIMPED_FindTileGroup
 *
 * Returns the first of 64 tile-group slots whose 3x3 pattern matches
 * the terrain around (x,y). Pattern values: 0xFF = wildcard (match
 * current group), >=0xFE = don't care, else = match against group
 * at that index. Returns -1 on miss.
 */
static int
SIMPED_FindTileGroup(int x, int y, int groupIdx)
{
	int slot, row, col;
	int found;
	CEditorTileGroup *group;
	int val;
	int terrainTileID;
	int result;

	group = g_EditorTileGroups[groupIdx];
	if (group == NULL)
		return -1;

	for (slot = 0; slot < 0x40; slot++) {
		found = 0;

		for (row = 0; row < 3; row++) {
			for (col = 0; col < 3; col++) {
				val = group->slots[slot].pattern[row][col];

				if (val == 0xFF) {
					// Wildcard: get terrain tile, search in this group
					terrainTileID = Terrain_GetLandTileID(x + row - 1, y + col - 1);
					result = SIMPED_FindTileEntry(groupIdx, terrainTileID);
					if (result == -1) {
						found = -1;
					} else if (found != -1) {
						found = slot;
					}
				}

				if (val >= 0xFE)
					continue;

				// Concrete: get terrain tile, search in group[val]
				terrainTileID = Terrain_GetLandTileID(x + row - 1, y + col - 1);
				result = SIMPED_FindTileEntry(val, terrainTileID);
				if (result == -1) {
					found = -1;
				} else if (found != -1) {
					found = slot;
				}
			}
		}

		if (found != -1)
			return slot;
	}

	return -1;
}

/*
 * 0x00467500 - FindLocation
 *
 * Case-insensitive search for name in g_NamedLocations; writes the
 * coordinates into out and returns 1 on hit, 0 on miss.
 */
int
FindLocation(const char *name, CLocation *out)
{
	int i;
	for (i = 0; g_NamedLocations[i].name != NULL; i++) {
		if (strcasecmp(g_NamedLocations[i].name, name) == 0) {
			CLocation_Set(out, g_NamedLocations[i].x, g_NamedLocations[i].y, g_NamedLocations[i].z);
			return 1;
		}
	}
	return 0;
}

// Door auto-close uses ScheduleEvent (type 0x3C) matching the binary.

CTradeSession *g_TradeSessionList; // 0x006933D4

#define U32GET(p) ((uint32_t)(((p)[0] << 24) | ((p)[1] << 16) | ((p)[2] << 8) | (p)[3]))

/*
 * GetStat clamp: base + bonus, clamped to [0, 0xFDE8].
 */
uint16_t
ClampStat(int32_t val)
{
	if (val < 0)
		return 0;
	if (val > 0xFDE8)
		return 0;
	return (uint16_t)val;
}

/*
 * 0x004677D6 - Static init wrapper
 *
 * Static initializer that constructs g_EditorTileGroups (zeroes all 64
 * tile-group pointers).
 */
static __attribute__((unused)) void
StaticInit_EditorTileGroups(void)
{
	CEditorObj_Constructor((CEditorObj *)g_EditorTileGroups);
}

/*
 * 0x00478770 - GetPacketOffset
 *
 * Returns the payload offset past the opcode: the 16-bit length field
 * for dynamic-size packets, or g_PacketOffset for fixed-size packets.
 */
uint16_t
GetPacketOffset(uint8_t *buf)
{
	int off;

	if (!PacketIsDynamicSize(buf))
		return GetGlobalOffset();
	memcpy(&off, buf + 1, 2);
	return off;
}

/*
 * 0x004787B0 - GetGlobalOffset
 *
 * Returns g_PacketOffset, the fixed-size-packet payload offset.
 */
uint16_t
GetGlobalOffset(void)
{
	return g_PacketOffset;
}

/*
 * 0x004787C0 - PacketIsDynamicSize
 *
 * Binary unconditionally reads g_PacketTable. We select the
 * per-connection packet table when a CUserSock is active, for
 * multi-client support.
 */
int
PacketIsDynamicSize(uint8_t *buf)
{
	uint16_t *pt = GLOBAL_CUserSock ? GLOBAL_CUserSock->packetTable : g_PacketTable;
	return pt[5 * *buf] & PacketDynamicSize;
}

/*
 * 0x0047E0D1 - SendToClient
 *
 * Sends a packet buffer to the usersock of the given entity. If range
 * is -1, resolves packet size via GetPacketOffset.
 *
 * MODIFIED: calls Socket_Copy_To_CSocketBuffer instead of
 * CUserSock_SendPacketRaw so non-demo clients get Huffman compression
 * and Twofish encryption.
 */
void
SendToClient(CItem *entity, uint8_t *buf, int range)
{
	CPlayer *player;

	if (entity == NULL)
		return;
	player = (CPlayer *)entity;
	if (player->usersock == NULL)
		return;
	if (range == -1)
		range = GetPacketOffset(buf) & 0xFFFF;
	Socket_Copy_To_CSocketBuffer((CSocket *)player->usersock, buf, range);
}

/*
 * 0x0047E11A - SendToClientList
 *
 * Iterates a CVector of CPlayer pointers, sends buf to each via
 * SendToClient(entity, buf, -1).
 */
void
SendToClientList(CVector *list, uint8_t *buf)
{
	uintptr_t *iter;

	if (g_shutdown)
		return;

	iter = (uintptr_t *)list->begin;
	while (iter != (uintptr_t *)list->end) {
		SendToClient((CItem *)*iter, buf, -1);
		iter++;
	}
}

/*
 * 0x0047E166 - BroadcastToNearby
 *
 * Cdecl, 3 args (buf, loc, maxRecipients). 3rd arg is a max
 * recipient count cap, not a distance. Checks g_shutdown for
 * early exit. CEntityMap_RangeQuery on g_ItemMap with hardcoded
 * range=12. Vector_SortByDist, cap to maxRecipients, iterate
 * by index sending via SendToClient.
 */
void
BroadcastToNearby(uint8_t *buf, CLocation *loc, int maxRecipients)
{
	CVector list;
	char typeFlag[16] = { 0 };
	uintptr_t *iter;
	int count, i;

	if (g_shutdown)
		return;

	CVector_Constructor(&list, typeFlag);
	CEntityMap_RangeQuery(g_ItemMap, &list, loc->x, loc->y, 12);

	Vector_SortByDist((void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)&list), list.end, loc);

	count = CVector_GetCount(&list);
	if (count < maxRecipients)
		maxRecipients = count;

	if (maxRecipients > 0) {
		iter = (uintptr_t *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)&list);
		for (i = 0; i < maxRecipients; i++) {
			SendToClient((CItem *)*iter, buf, -1);
			iter++;
		}
	}

	CVector_Destructor(&list);
}

/*
 * 0x0047E27D - BroadcastToRangeWithLOS
 *
 * Sends buf to up to maxCount nearby items within range. Items are pulled
 * from g_ItemMap, filtered by a LOS raycast against the eye position
 * (location + (0,0,8)), sorted by distance, and capped before being
 * delivered via SendToClient. Skipped during g_shutdown.
 */
void
BroadcastToRangeWithLOS(uint8_t *buf, CLocation *loc, int maxCount, int range)
{
	CVector query, filtered;
	char typeFlag1[16] = { 0 }, typeFlag2[16] = { 0 };
	uintptr_t *iter;
	CLocation eyeLoc, locCopy, delta;
	int count, i;

	if (g_shutdown)
		return;

	CVector_Constructor(&filtered, typeFlag1);
	CVector_Constructor(&query, typeFlag2);

	CEntityMap_RangeQuery(g_ItemMap, &query, loc->x, loc->y, range);

	iter = (uintptr_t *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)&query);
	while (iter != (uintptr_t *)query.end) {
		CLocation_SetLoc(&locCopy, loc);
		CLocation_Constructor3D(&delta, 0, 0, 8);
		CLocation_AddWrapped(CEntity_GetLocation((CEntity *)*iter), &eyeLoc, &delta);
		if (CTerrainManager_LOSRaycast(&eyeLoc, &locCopy, 1))
			CVector_PushBack(&filtered, *iter);
		iter++;
	}

	Vector_SortByDist((void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)&filtered), filtered.end, loc);

	count = CVector_GetCount(&filtered);
	if (count < maxCount)
		maxCount = count;

	if (maxCount > 0) {
		iter = (uintptr_t *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)&filtered);
		for (i = 0; i < maxCount; i++) {
			SendToClient((CItem *)*iter, buf, -1);
			iter++;
		}
	}

	CVector_Destructor(&query);
	CVector_Destructor(&filtered);
}

/*
 * 0x0047E45C - HandlePacket_LOGIN (packet 0x00 CREATE_CHARACTER)
 *
 * Demo/1.25.x: 100 bytes.
 * Client 1.26.0: 104 bytes (+4 bytes for starting city index at end).
 */
void
HandlePacket_LOGIN(CUserSock *this, uint8_t *buf)
{
	uint8_t obuf[bufSize];
	uint32_t off;
	uint32_t edededed;
	uint16_t pattern1, pattern2;
	uint8_t pattern3;
	char *characterName, *characterPassword;
	uint8_t genre, strength, dexterity, intelligence;
	uint8_t skill1Number, skill2Number, skill3Number;
	uint8_t skill1Value, skill2Value, skill3Value;
	uint16_t skinColor;
	uint16_t hairStyle;
	uint16_t hairColor;
	uint16_t facialHairStyle;
	uint16_t facialHairColor;
	uint8_t unknown, slot;
	uint16_t unknownD, slotD;
	uint32_t clientIP;
	uint32_t colors;
	uint16_t startLocation;
	CPlayer *player;
	int addr;
	int succeed;
	int16_t locX, locY;
	int8_t locZ;

	// Custom: reject if not authenticated
	if (this->account == NULL) {
		Log_Game(this->addr, "rejected (not authenticated)");
		PacketManager_MakePacket_LOGIN_REJECT(&obuf[0], 0x02);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}

	// Custom: enforce the 5-character-per-account limit. The character
	// list (0xA9) shows only 5 slots; without a cap an account accumulates
	// unreachable characters that appear to vanish as they move between the
	// live and archived lists.
	if (CPlayerList_AccountCharCount(this->account->accountNum) >= 5) {
		Log_Game(this->addr, "'%s' character create rejected (account full)", this->account->login);
		PacketManager_MakePacket_LOGIN_REJECT(&obuf[0], 0x00);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}

	locX = locY = locZ = -1;

	off = 0;
	GetDWord(buf, &off, &edededed);
	GetWord(buf, &off, &pattern1);
	GetWord(buf, &off, &pattern2);
	GetByte(buf, &off, &pattern3);
	GetString(buf, &off, &characterName, 30);
	GetString(buf, &off, &characterPassword, 30);
	GetByte(buf, &off, &genre);
	GetByte(buf, &off, &strength);
	GetByte(buf, &off, &dexterity);
	GetByte(buf, &off, &intelligence);
	GetByte(buf, &off, &skill1Number);
	GetByte(buf, &off, &skill1Value);
	GetByte(buf, &off, &skill2Number);
	GetByte(buf, &off, &skill2Value);
	GetByte(buf, &off, &skill3Number);
	GetByte(buf, &off, &skill3Value);
	GetWord(buf, &off, &skinColor);
	GetWord(buf, &off, &hairStyle);
	GetWord(buf, &off, &hairColor);
	GetWord(buf, &off, &facialHairStyle);
	GetWord(buf, &off, &facialHairColor);
	// Use per-connection detected version in auto-detect mode.
	// When auto-detect fails (detectedKeyIndex == -1), default to new
	// format - plaintext test clients always use the 1.26+ layout.
	int connVer = g_AutoDetect ? this->detectedKeyIndex : g_ClientVersion;
	if (connVer < 0)
		connVer = CLIENT_12600;
	if (connVer < CLIENT_12535) {
		startLocation = -1;
		GetByte(buf, &off, &unknown);
		GetByte(buf, &off, &slot);
		GetDWord(buf, &off, &clientIP);
		GetDWord(buf, &off, &colors);
		colors = 0;
	} else {
		// 1.25.35+ uses 3 Words + 2 DWords tail.
		// For 1.26.0+ this is a 104-byte packet; for 1.25.35-37
		// the packet is 100 bytes but the tail fields align.
		GetWord(buf, &off, &startLocation);
		GetWord(buf, &off, &unknownD);
		GetWord(buf, &off, &slotD);
		GetDWord(buf, &off, &clientIP);
		GetDWord(buf, &off, &colors);
		// Pre-1.26 clients don't support character colors.
		if (connVer < CLIENT_12600)
			colors = 0;
	}
	addr = this->addr;
	succeed = 1;
	// Binary calls FindPlayer here and rejects with LOGIN_REJECT(0x02) if
	// the name is taken. Removed: the demo used name as identity (no
	// accounts), but with account-scoped slot-based selection (0x5D) names
	// don't need to be unique.
	if (succeed) {
		if (startLocation > 0 && startLocation < nelem(g_PlaceNameList)) {
			locX = g_PlaceNameList[startLocation].x;
			locY = g_PlaceNameList[startLocation].y;
			locZ = g_PlaceNameList[startLocation].z;
		}
		player = NewCharacter(this, locX, locY, locZ, characterName, characterPassword, genre, strength, dexterity, intelligence, skill1Number, skill1Value, skill2Number,
		        skill2Value, skill3Number, skill3Value, skinColor, hairStyle, hairColor, facialHairStyle, facialHairColor, clientIP, colors);
		Log_Game(this->addr, "'%s' created character '%s'", this->account->login, characterName);
		Player_Login(player, addr);
	} else {
		PacketManager_MakePacket_LOGIN_REJECT(&obuf[0], 0x00);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
	}
}

/*
 * 0x0047E768 - HandlePacket_POSTLOGIN_UserSock
 *
 * Forwards to the PostLogin stub with this->player and this->addr.
 */
void
HandlePacket_POSTLOGIN_UserSock(CUserSock *this)
{
	uint32_t addr;

	if (this->player) {
		addr = this->addr;
		PostLogin(this->player, addr);
	}
}

/*
 * 0x0047E79E - HandlePacket_PRELOGIN
 *
 * Reads character name/password from the login packet and either binds
 * the found player to this socket (disconnecting any previous one) or
 * replies with LOGIN_REJECT.
 */
void
HandlePacket_PRELOGIN(CUserSock *this, uint8_t *buf)
{
	char *characterPassword;
	CPlayer *player;
	uint32_t off;
	char *characterName;
	uint32_t addr;
	uint8_t obuf[16];
	uint32_t edededed;
	int authenticated;

	// Custom: reject if not authenticated
	if (this->account == NULL) {
		Log_Game(this->addr, "rejected (not authenticated)");
		PacketManager_MakePacket_LOGIN_REJECT(&obuf[0], 0x00);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}

	authenticated = 0;
	off = 0;
	GetDWord(buf, &off, &edededed);
	GetString(buf, &off, &characterName, 30);
	GetString(buf, &off, &characterPassword, 30);
	addr = this->addr;
	authenticated = 0;
	// Custom: select character by slot index from the account's character
	// list. This supports duplicate names across accounts. The binary uses
	// FindPlayer by name, but that breaks when two accounts have characters
	// with the same name.
	{
		uint32_t charSlot, clientIP;
		GetDWord(buf, &off, &charSlot);
		GetDWord(buf, &off, &clientIP);
		player = NULL;
		CVector cv;
		char tf = '\x01';
		CVector_Constructor(&cv, &tf);
		CPlayerList_CollectByAccountIDSorted(&cv, this->account->accountNum);
		uint32_t cc = CVector_GetCount(&cv);
		if (charSlot < cc) {
			player = (CPlayer *)((uintptr_t *)cv.begin)[charSlot];
		}
		CVector_Destructor(&cv);
		// accountNum 0 = orphaned; claim it
		if (player != NULL && player->accountNum == 0)
			player->accountNum = this->account->accountNum;
	}
	authenticated = 1;
	if (!authenticated) {
		// Dead code in binary: authentication always succeeds (var set to 1
		// unconditionally before check). LOGIN_REJECT reason 0.
		PacketManager_MakePacket_LOGIN_REJECT(&obuf[0], 0);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}
	if (player == NULL) {
		Log_Game(this->addr, "'%s' rejected (character not found)", this->account->login);
		PacketManager_MakePacket_LOGIN_REJECT(&obuf[0], 1);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}
	if (player->usersock) {
		CUserSock *oldSock = player->usersock;
		oldSock->socket.status = SocketClosing;
		oldSock->player = NULL;
		player->usersock = 0;
	}
	player->usersock = this;
	this->player = player;
	Log_Game(this->addr, "'%s' entered as '%s'", this->account->login, CMobile_GetName_VT((CItem *)player));
	Player_Login(player, addr);
}

/*
 * 0x0047E8D8
 * Unused in client >= 1.25.35.
 */
void
HandlePacket_ACCT_LOGIN_REQ_original(CUserSock *this, uint8_t *buf)
{
	uint8_t obuf[bufSize];
	char characterNames[150];
	char characterPasswords[150];
	int numCharacters;
	char *characterName;
	char *characterPassword;
	uint32_t off;

	off = 0;
	memset(characterNames, 0, sizeof(characterNames));
	memset(characterPasswords, 0, sizeof(characterPasswords));
	GetString(buf, &off, &characterName, 30);
	GetString(buf, &off, &characterPassword, 30);

	// Custom: account validation
	CAccount *acct = Account_FindOrCreate(characterName, characterPassword);
	if (acct == NULL) {
		// Bad password or banned - send empty list
		PacketManager_MakePacket_ACCT_LOGIN_OK(obuf, 0, 0xCD, &characterNames[0], &characterPasswords[0]);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}
	this->account = acct;

	numCharacters = 0;

	{
		CVector charVec;
		char typeFlag = '\x01';
		uint32_t i, count;
		CVector_Constructor(&charVec, &typeFlag);
		CPlayerList_CollectByAccountIDSorted(&charVec, acct->accountNum);
		count = CVector_GetCount(&charVec);
		for (i = 0; i < count && numCharacters < 5; i++) {
			CPlayer *p = (CPlayer *)((uintptr_t *)charVec.begin)[i];
			if (p->mobile.name != NULL) {
				strncpy(&characterNames[numCharacters * 30], p->mobile.name, 29);
				characterNames[numCharacters * 30 + 29] = '\0';
				numCharacters++;
			}
		}
		CVector_Destructor(&charVec);
	}

	PacketManager_MakePacket_ACCT_LOGIN_OK(obuf, numCharacters, 0xCD, &characterNames[0], &characterPasswords[0]);
	Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
}

/*
 * 0x0047E8D8 - HandlePacket_ACCT_LOGIN_REQ
 *
 * Binary sends ACCT_LOGIN_OK (0x86) with the character list directly on
 * the login connection. This is the UoDemo-only login flow.
 *
 * MODIFIED: clients from 1.25.30+ handle BRITANNIA_LIST (0xA8).
 * Decompilation confirms 0xA8 handlers in the packet dispatch of 1.25.30,
 * 1.25.31, 1.25.35, and 1.25.37 (HandlePacket_BRITANNIA_LIST). Clients
 * 1.25.0 and 1.23.37b cap the dispatch table at packet 0xA3 and do not
 * understand it. The cutoff is CLIENT_12530 (1.25.30, compiled Feb 27 1998).
 */
void
HandlePacket_ACCT_LOGIN_REQ(CUserSock *this, uint8_t *buf)
{
	uint8_t obuf[bufSize];

	if (Version_GetConnVer(this, CLIENT_12600) == CLIENT_DEMO) {
		HandlePacket_ACCT_LOGIN_REQ_original(this, buf);
	} else {
		// Custom: validate credentials before sending server list.
		// Send 0x82 (ACCT_LOGIN_FAIL) on bad password, which tells
		// the client to show the appropriate error and return to the
		// login screen.
		char *characterName, *characterPassword;
		uint32_t off = 0;
		GetString(buf, &off, &characterName, 30);
		GetString(buf, &off, &characterPassword, 30);

		CAccount *acct = Account_FindOrCreate(characterName, characterPassword);
		if (acct == NULL) {
			// Bad login/password, banned, or validation failure
			Log_Auth(this->addr, "failed login for '%s'", characterName);
			PacketManager_MakePacket_ACCT_LOGIN_FAIL(&obuf[0], 0x03);
			Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
			return;
		}
		this->account = acct;
		Log_Auth(this->addr, "accepted login for '%s'", characterName);

		PacketManager_MakePacket_BRITANNIA_LIST(&obuf[0], this->serverAddr);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
	}
}

/*
 * 0x0047EA8F - HandlePacket_ACCT_DEL_CHAR
 *
 * Binary parses 3 fields and discards them - character deletion was
 * not implemented in UoDemo.exe.
 *
 * MODIFIED: actually deletes the character. Finds the player by slot
 * index in the global player list, verifies not logged in, removes
 * from the list, deletes the entity via vtable, and sends updated
 * character list (0x86 ALL_CHARACTERS) back to client.
 */
void
HandlePacket_ACCT_DEL_CHAR(CUserSock *this, uint8_t *buf)
{
	char *characterPassword;
	uint32_t off;
	uint32_t characterSlot;
	uint32_t clientIP;
	uint8_t obuf[bufSize];
	CPlayer *target;
	int i;
	char characterNames[150];
	char characterPasswords[150];
	int numCharacters;
	CVector charVec;
	char typeFlag;

	off = 0;
	GetString(buf, &off, &characterPassword, 30);
	GetDWord(buf, &off, &characterSlot);
	GetDWord(buf, &off, &clientIP);

	USED(clientIP);

	// Custom: reject if no account
	if (this->account == NULL) {
		memset(obuf, 0, sizeof(obuf));
		PacketManager_MakePacket_CHG_CHAR_RESULT(obuf, 1);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}

	// Custom: find the player at the requested slot using account's character list
	typeFlag = '\x01';
	CVector_Constructor(&charVec, &typeFlag);
	CPlayerList_CollectByAccountIDSorted(&charVec, this->account->accountNum);
	numCharacters = CVector_GetCount(&charVec);
	target = ((int)characterSlot < numCharacters) ? (CPlayer *)((uintptr_t *)charVec.begin)[characterSlot] : NULL;
	CVector_Destructor(&charVec);

	memset(obuf, 0, sizeof(obuf));

	if (target == NULL) {
		// Slot not found
		PacketManager_MakePacket_CHG_CHAR_RESULT(obuf, 1);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}

	if (target->usersock != NULL) {
		// Character is currently logged in
		PacketManager_MakePacket_CHG_CHAR_RESULT(obuf, 2);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}

	// Remove from player list (if active) or entity manager (if disconnected)
	Log_Game(this->addr, "'%s' deleted character '%s'", this->account->login, CMobile_GetName_VT((CItem *)target));
	CPlayerList_RemovePlayer(target);
	EntityManager_EraseEntity((CItem *)target);
	((void (*)(void *))VT_FN((CItem *)target, VT_DELETE))((CItem *)target);

	// Custom: send updated account-scoped character list back to client
	memset(characterNames, 0, sizeof(characterNames));
	memset(characterPasswords, 0, sizeof(characterPasswords));

	typeFlag = '\x01';
	CVector_Constructor(&charVec, &typeFlag);
	CPlayerList_CollectByAccountIDSorted(&charVec, this->account->accountNum);
	numCharacters = CVector_GetCount(&charVec);
	for (i = 0; i < numCharacters && i < 5; i++) {
		CPlayer *p = (CPlayer *)((uintptr_t *)charVec.begin)[i];
		strncpy(&characterNames[i * 30], p->mobile.name, 29);
		characterNames[i * 30 + 29] = '\0';
		strncpy(&characterPasswords[i * 30], p->password, 29);
		characterPasswords[i * 30 + 29] = '\0';
	}
	CVector_Destructor(&charVec);

	PacketManager_MakePacket_CITIES_AND_CHARS(obuf, &characterNames[0], &characterPasswords[0]);
	Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
}

/*
 * 0x0047EADE - Stub. Reads old password (30b), new password (30b),
 * and 2 DWORDs (character serial + validation token), discards all.
 *
 * Password changing not implemented in UoDemo.
 */
void
HandlePacket_CHG_CHAR_PW(CUserSock *this, uint8_t *buf)
{
	uint32_t off;
	char *oldPw, *newPw;
	uint32_t charSerial, token;

	off = 0;
	GetString(buf, &off, &oldPw, 0x1E);
	GetString(buf, &off, &newPw, 0x1E);
	GetDWord(buf, &off, &charSerial);
	GetDWord(buf, &off, &token);
	USED(this);
}
/*
 * 0x0048E779 - Handle_LookAt
 *
 * Core single-click/look-at logic: visibility check, fire event 0x1C,
 * send a TEXT packet with the target's name/hue, and a container
 * addendum for open containers.
 */
void
Handle_LookAt(CPlayer *this, uint32_t targetSerial)
{
	uint8_t obuf[0x834];
	uint8_t obuf2[0x834];
	CItem *target;
	uint16_t hue;
	char *name;
	uint32_t savedSerial;
	char nameBuf[1024];
	char countBuf[1024];

	target = CWorld_FindBySerial(g_World, targetSerial);
	if (target == NULL)
		return;

	// Visibility checks
	if (!CPlayer_IsEditing(this)) {
		if (VT_IsHidden(target))
			return;
		// If target is a player and is GM, hide from non-editors
		if (VT_IsPlayer(target)) {
			if (CPlayer_IsGameMaster((CPlayer *)target))
				return;
		}
	}

	savedSerial = target->serial;

	if (Entity_ExecuteEvent(&target->resourceEntity.entity, 0x1C, this->mobile.container.item.serial) == 0)
		goto container_addendum;

	hue = 0x03B2;

	if (VT_IsMobile(target)) {
		// Mobile path
		const char *invuln;
		const char *frozen;
		const char *squelched;

		if (CMobile_IsInvulnerable((CMobile *)target))
			invuln = " (invulnerable)";
		else
			invuln = "";

		if (CPlayer_IsGhost((CPlayer *)target))
			frozen = " (frozen)";
		else
			frozen = "";

		if (CMobile_IsSquelched((CMobile *)target))
			squelched = " (squelched)";
		else
			squelched = "";

		name = CMobile_GetNameAndHue(target, &hue, (CItem *)this);
		sprintf(nameBuf, "%s%s%s%s", name, squelched, frozen, invuln);
		name = nameBuf;
	} else {
		name = ((char *(*)(void *, int))VT_FN(target, VT_SPEAK_SYS_MSG))(target, 1);

		if (((int (*)(void *))VT_FN(target, VT_HAS_CORPSE_EQ))(target)) {
			uint16_t cbt = CCorpse_GetCorpseBodyType((CCorpse *)target);
			if (cbt == 0x190 || cbt == 0x191) {
				// Get notoriety hue for human corpse
				int notIdx = ((int (*)(void *, void *))VT_FN(target, VT_GET_NOTORIETY))(target, (CItem *)this);
				hue = g_NotorietyHueTable[notIdx];
			}
		}
	}

	PacketManager_MakePacket_TEXT(obuf, target, (CItem *)this, 6, name, hue, 3);
	SendToClient((CItem *)this, obuf, -1);

container_addendum:
	if (CWorld_FindBySerial(g_World, savedSerial) != target)
		return;
	if (!VT_IsMobile2(target))
		return;
	if (VT_IsMobile(target))
		return;
	if (((int (*)(void *))VT_FN(target, VT_HAS_CORPSE_EQ))(target))
		return;
	if (!((int (*)(void *))VT_FN(target, VT_HAS_ACCESSIBLE_CONTENTS))(target))
		return;
	if (!((int (*)(void *))VT_FN(target, VT_EXCLUDED_AMOUNT))(target))
		return;
	{
		CItem *root = target;
		while (root->parent != NULL)
			root = root->parent;
		if (VT_IsMobile(root)) {
			if (root != (CItem *)this) {
				if (!CPlayer_IsEditing(this))
					return;
			}
		}
	}
	{
		int itemCount;
		int weight;

		itemCount = CContainer_CountItems((CContainer *)target, 1);
		weight = ((int (*)(void *))VT_FN(target, VT_GET_STORED_WEIGHT))(target);

		sprintf(countBuf, "(%d items, %d stones)", itemCount, weight);
		PacketManager_MakePacket_TEXT(obuf2, target, (CItem *)this, 6, countBuf, 0x03B2, 3);
		SendToClient((CItem *)this, obuf2, -1);
	}
}

/*
 * 0x00490AC3 - CItem::GetByteProp
 *
 * Returns CItem::GetLayer().
 */
static int
CItem_GetByteProp(CItem *ent)
{
	return CItem_GetLayerThiscall(ent);
}

/*
 * 0x00490C6D - CItem::GetSortKey
 *
 * Returns the tile-data quantity for sort ordering.
 */
static int
CItem_GetSortKey(CItem *ent)
{
	return CItem_GetTiledataQuantity(ent);
}

/*
 * 0x004922FF - ValidateEntityAccess
 *
 * Walks entity parent chain, validates player can access it.
 * Zeroes *serialPtr on failure. GM bypass. Range check <= 18.
 * useCheck: 1 for use/interaction, 0 for cursor/target operations.
 */
static void
ValidateEntityAccess(CPlayer *player, uint32_t *serialPtr, int useCheck)
{
	CItem *entity, *root;

	entity = CWorld_FindBySerial(g_World, *serialPtr);
	if (entity == NULL) {
		*serialPtr = 0;
		return;
	}

	if (CPlayer_IsEditing(player))
		return;

	// Walk parent chain to root
	root = entity;
	while (root->parent != NULL)
		root = root->parent;

	if (root == entity)
		goto range_check;

	if (root == (CItem *)player)
		return;

	if (!VT_IsMobile(root))
		goto range_check;

	// Root is a mobile
	if (!useCheck) {
		*serialPtr = 0;
		return;
	}

	if (VT_IsMobile2(entity)) {
		if (!CItem_HasSecuredAncestor(entity))
			goto range_check;
		// Secured ancestor found - fall through to backpack check
	}

	// and must be root's backpack (equipment[21], offset 0x2E4)
	if (entity->parent != root) {
		*serialPtr = 0;
		return;
	}
	if (((CMobile *)root)->equipment[21] == entity)
		goto range_check;
	*serialPtr = 0;
	return;

range_check:
	if (CLocation_ChebyshevDistance(&player->mobile.container.item.resourceEntity.entity.location, &root->resourceEntity.entity.location) > 0x12)
		*serialPtr = 0;
}

/*
 * 0x00492746 - Player_Login
 *
 * Thin wrapper: calls Player_LoginSequence, then applies custom
 * account privilege logic (GM/Counselor flags) and debug GM mode.
 */
void
Player_Login(CPlayer *this, uint32_t addr)
{
	Player_LoginSequence(this, addr);

	// Custom: apply account privileges
	if (this->usersock) {
		CAccount *acct = this->usersock->account;
		if (acct && acct->plevel >= 2)
			this->pflags |= PlayerIsGameMaster;
		if (acct && acct->plevel >= 1)
			this->pflags |= PlayerIsCounselor;
		if (acct && (acct->flags & ACCT_GODMODE))
			CPlayer_EnableEditing(this);
	}

	// Custom: auto-enable GM mode if -gm flag is set.
	// Must be after Player_LoginSequence -> SendAppearance which calls
	// DisableEditing.
	if (g_DebugGM) {
		CPlayer_EnableEditing(this);
		this->pflags |= PlayerIsGameMaster;
	}

	// Custom: enable Test Center mode if -test flag is set. Narrow
	// self-admin tier (set/where/help/resurrect), separate from GM.
	if (g_DebugTest)
		this->pflags |= PlayerIsTestCenter;
}

/*
 * 0x0049275B
 */
void
PostLogin(CPlayer *player, uint32_t addr)
{
	USED(player);
	USED(addr);
}

/*
 * 0x00492760 - Player_LoginSequence
 *
 * 16-phase login sequence fired when POSTLOGIN admits a player into
 * the world.
 */
void
Player_LoginSequence(CPlayer *this, uint32_t addr)
{
	uint8_t obuf[0x4C0];

	USED(addr);

	// Phase 0 (0x00492776): SetIsLoaded via 0x0044FA4A
	CPlayer_SetIsLoaded(this);

	// Guard: if IsLoaded bit is NOT set, early exit (close socket).
	// Binary has no NULL check; our code adds NULL guard for safety.
	if (!(this->pflags & PlayerIsLoaded)) {
		if (this->usersock)
			this->usersock->socket.status = SocketClosing;
		return;
	}

	// Phase 1 (0x004927A8): Send LOGIN_CONFIRM (0x1B)
	PacketManager_MakePacket_LOGIN_CONFIRM(&obuf[0], this);
	SendPacketToPlayer(this, &obuf[0], -1);

	// Phase 2 (0x004927CB): Set [usersock + 0x00010034] = 1 (seedConsumed)
	this->usersock->seedConsumed = 1;

	if (this->pflags & 0x2000)
		EntityManager_RestoreBySerial(this->mobile.container.item.serial);

	// Phase 3 (0x00492800): SendAppearance
	CPlayer_SendAppearance(this);

	// Phase 4 (0x00492808): Restore cursor item (equipment[0]).
	if (this->mobile.equipment[0] != NULL) {
		CItem *cursor = this->mobile.equipment[0];

		((void (*)(void *))VT_FN(cursor, VT_HIDE))(cursor);

		CLocation savedLoc;
		CLocation_SetLoc(&savedLoc, ((CLocation * (*)(void *)) VT_FN(&this->mobile.container.item, VT_GET_LOCATION))(&this->mobile.container.item));

		int lockSerial = 0;
		if (CItem_GetTagInt(cursor, "lockedContainer", &lockSerial)) {
			CLocation tmpLoc;
			CLocation_Init(&tmpLoc);
			CLocation_Set(&tmpLoc, -1, -1, 0);
			CItem *cont = CWorld_FindBySerial(g_World, (uint32_t)lockSerial);
			if (cont != NULL && VT_IsMobile2(cont)) {
				((void (*)(void *, CItem *, CLocation *))VT_FN(cursor, VT_ADD_TO_CONTAINER))(cursor, cont, &tmpLoc);
			} else {
				((void (*)(void *, CLocation *))VT_FN(cursor, VT_DROP_AT_FEET))(cursor, &savedLoc);
			}
		} else {
			((void (*)(void *, CLocation *))VT_FN(cursor, VT_DROP_AT_FEET))(cursor, &savedLoc);
		}
	}

	// Phase 5 (0x00492951): Send COMBAT (0x72) with 4 combat bytes
	PacketManager_MakePacket_COMBAT(&obuf[0], this->mobile.combatByte1, this->mobile.combatByte2, this->mobile.combatByte3, this->mobile.combatByte4);
	SendPacketToPlayer(this, &obuf[0], -1);

	// Phase 6 (0x00492975): NotifyNearby
	NotifyNearby(&this->mobile.container.item);

	// Phase 7 (0x0049297A): SetHP/SetMana/SetStamina with current values.
	((uint32_t (*)(void *, int))VT_FN(&this->mobile.container.item, VT_SET_HP))(&this->mobile.container.item, (int)this->mobile.hp);
	((uint32_t (*)(void *, int))VT_FN(&this->mobile.container.item, VT_SET_MANA))(&this->mobile.container.item, (int)this->mobile.mana);
	((uint32_t (*)(void *, int))VT_FN(&this->mobile.container.item, VT_SET_STAMINA))(&this->mobile.container.item, (int)this->mobile.stamina);

	// Phase 8 (0x004929C7): Send GODMODE (0x2B) if editing
	if (CPlayer_IsEditing(this)) {
		PacketManager_MakePacket_GODMODE(&obuf[0], 1);
		SendPacketToPlayer(this, &obuf[0], -1);
	}

	// Phase 9 (0x004929F0): Restore trade windows.
	{
		CTradeSession *session;

		for (session = g_TradeSessionList; session != NULL; session = session->next) {
			if (session->player1 != this && session->player2 != this)
				continue;

			CContainer_SendContainerContents((CContainer *)this, (CItem *)this, this->mobile.container.item.serial, 0);

			if (session->player1 == this) {
				CContainer_SendContainerContents((CContainer *)session->player2, (CItem *)this, this->mobile.container.item.serial, 0);

				PacketManager_MakePacket_TRADE(obuf, 0, ((CItem *)session->player2)->serial, session->container1->serial, session->container2->serial,
				        ((const char *(*)(void *))VT_FN((CItem *)session->player2, VT_GET_NAME))(session->player2));
			} else {
				CContainer_SendContainerContents((CContainer *)session->player1, (CItem *)this, this->mobile.container.item.serial, 0);

				PacketManager_MakePacket_TRADE(obuf, 0, ((CItem *)session->player1)->serial, session->container2->serial, session->container1->serial,
				        ((const char *(*)(void *))VT_FN((CItem *)session->player1, VT_GET_NAME))(session->player1));
			}

			SendToClient((CItem *)this, obuf, -1);

			SetTradeAcceptState(session, session->accept1, session->accept2);

			CContainer_SendContainerContents((CContainer *)session->container1, (CItem *)this, this->mobile.container.item.serial, 0);

			CContainer_SendContainerContents((CContainer *)session->container2, (CItem *)this, this->mobile.container.item.serial, 0);
		}
	}

	// Phase 10 (0x00492BA1): MOTD from login.txt.
	{
		char *motd;

		motd = NamedResource_Find("login.txt");
		if (motd != NULL) {
			PacketManager_MakePacket_TEXT(obuf, NULL, (CItem *)&this->mobile.container.item, 6, motd, 0x03B2, 3);
			SendPacketToPlayer(this, obuf, -1);
		}
	}

	// Phase 11 (0x00492BFE): Send LOGIN_COMPLETE (0x55)
	PacketManager_MakePacket_LOGIN_COMPLETE(&obuf[0]);
	SendPacketToPlayer(this, &obuf[0], -1);

	// Phase 12 (0x00492C1E): CheckGuardZone
	CPlayer_CheckGuardZone(this);

	// Phase 13 (0x00492C30): SetFightTarget
	CPlayer_SetFightTarget(this, this->combatTargetSerial);

	// Phase 14 (0x00492C3E): SendWeather
	WeatherManager_sendWeatherToPlayer(&this->mobile.container.item);

	// Phase 15 (0x00492C4A): Send GAMETIME (0x5B)
	PacketManager_MakePacket_GAMETIME(&obuf[0]);
	SendPacketToPlayer(this, &obuf[0], -1);
}

/*
 * 0x00492CBA - HandlePacket_GODMODE_TOGGLE
 *
 * Reads toggleByte: non-zero runs the IsLoaded check then EnableEditing,
 * zero runs DisableEditing. Always sends a GODMODE packet to the client.
 *
 * FIXED: the binary has no privilege check - any client can toggle god
 * mode. Added an account plevel >= 2 (GM) check before enabling editing.
 */
void
HandlePacket_GODMODE_TOGGLE(CPlayer *this, uint8_t *buf)
{
	uint8_t obuf[0x10];
	uint32_t off;
	uint8_t toggleByte;

	off = 0;
	GetByte(buf, &off, &toggleByte);

	if ((toggleByte & 0xFF) != 0) {
		// FIXED: require GM account privilege to enable editing
		CAccount *acct = NULL;
		if (this->usersock)
			acct = this->usersock->account;
		if (acct == NULL || acct->plevel < 2)
			return;
		if (CPlayer_IsLoaded(this))
			CPlayer_EnableEditing(this);
	} else {
		CPlayer_DisableEditing(this);
	}

	PacketManager_MakePacket_GODMODE(&obuf[0], CPlayer_IsEditing(this));
	SendToClient((CItem *)this, &obuf[0], -1);
}

/*
 * 0x0C @ 0x00492D30 - TILEDATA
 *
 * GM editor tool: updates item tile data properties. Reads 11 fields and
 * forwards them to the editor singleton.
 *
 * Packet fields: artId(Word), flags(DWord), weight(Byte), quality(Byte),
 *   unknown(DWord), quantity(Word), animId(Word), hue(Word),
 *   stackOff(Byte), height(Word), name(String20)
 */
void
HandlePacket_TILEDATA(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint16_t artId, quantity, animId, hue, height;
	uint32_t flags, miscData;
	uint8_t weight, quality, stackOff;
	char *name;

	hue = 0; /* 0x00492D39: pre-initialized */

	off = 0;
	GetWord(buf, &off, &artId);
	GetDWord(buf, &off, &flags);
	GetByte(buf, &off, &weight);
	GetByte(buf, &off, &quality);
	GetDWord(buf, &off, &miscData);
	GetWord(buf, &off, &quantity);
	GetWord(buf, &off, &animId);
	GetWord(buf, &off, &hue);
	GetByte(buf, &off, &stackOff);
	GetWord(buf, &off, &height);
	GetString(buf, &off, &name, 0x14);

	StoreTileDataEntry(this, artId, flags, name, weight, quality, miscData, quantity, animId, hue, stackOff, height);
}

/*
 * 0x61 @ 0x00492E64 - DESTROY_STATIC
 *
 * GM tool: removes a static item from the world. Reads source coordinates
 * and graphic ID, validates coordinates, locks statics, searches the
 * block's static chain for a match, calls FreeStaticItem to unlink and
 * recycle the item, and unlocks statics.
 *
 * Packet fields: srcX(Word), srcY(Word), srcZ(Word), graphic(Word)
 */
void
HandlePacket_DESTROY_STATIC(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint16_t srcX, srcY, srcZ, graphic;
	int blockIdx;
	MapBlock *block;
	CItem *item;

	USED(this);

	off = 0;
	GetWord(buf, &off, &srcX);
	GetWord(buf, &off, &srcY);
	GetWord(buf, &off, &srcZ);
	GetWord(buf, &off, &graphic);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(srcX & 0xFFFF), (int)(srcY & 0xFFFF)))
		return;

	Static_Lock();
	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)(srcX & 0xFFFF), (int)(srcY & 0xFFFF), 0);

	block = &g_MapBlocks[blockIdx];
	item = block->staticHead;

	while (item != NULL) {
		if (item->resourceEntity.entity.location.x == srcX && item->resourceEntity.entity.location.y == srcY && item->resourceEntity.entity.location.z == (int16_t)srcZ &&
		        (CEntity_GetBodyType(item) & 0xFFFF) == (graphic & 0xFFFF)) {
			FreeStaticItem(item);
			break;
		}
		item = item->resourceEntity.nextInContainer;
	}
	Static_Unlock();
}

/*
 * 0x62 @ 0x00492F9E - MOVESTATIC
 *
 * GM tool: moves a static item by a signed delta offset. Reads source
 * coordinates, graphic ID, and XYZ deltas. Validates coordinates, locks
 * statics, searches the block's static chain for the matching item,
 * removes it from the old chain, applies the delta to its location,
 * re-inserts into the new block's chain, and unlocks statics.
 *
 * Packet fields: srcX(Word), srcY(Word), srcZ(Word), graphic(Word),
 *                dZ(Word), dY(Word), dX(Word)
 * Note: binary applies 5th word to Z, 6th to Y, 7th to X.
 */
void
HandlePacket_MOVESTATIC(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint16_t srcX, srcY, srcZ, graphic;
	uint16_t dZ, dY, dX;
	int blockIdx;
	MapBlock *block;
	CItem *item;
	CLocation tempLoc;

	USED(this);

	off = 0;
	GetWord(buf, &off, &srcX);
	GetWord(buf, &off, &srcY);
	GetWord(buf, &off, &srcZ);
	GetWord(buf, &off, &graphic);
	GetWord(buf, &off, &dZ); // 5th word - Z delta
	GetWord(buf, &off, &dY); // 6th word - Y delta
	GetWord(buf, &off, &dX); // 7th word - X delta

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(srcX & 0xFFFF), (int)(srcY & 0xFFFF)))
		return;

	Static_Lock();
	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)(srcX & 0xFFFF), (int)(srcY & 0xFFFF), 0);

	block = &g_MapBlocks[blockIdx];
	item = block->staticHead;

	while (item != NULL) {
		if (item->resourceEntity.entity.location.x == srcX && item->resourceEntity.entity.location.y == srcY && item->resourceEntity.entity.location.z == (int16_t)srcZ &&
		        (CEntity_GetBodyType(item) & 0xFFFF) == (graphic & 0xFFFF)) {

			CLocation_Init(&tempLoc);
			CLocation_CopyFrom(&tempLoc, &item->resourceEntity.entity.location);
			tempLoc.z += (int16_t)dZ;
			tempLoc.y += dY;
			tempLoc.x += dX;

			((void (*)(void *))VT_FN(item, VT_HIDE))(item);

			((void (*)(CItem *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, &tempLoc);

			break;
		}
		item = item->resourceEntity.nextInContainer;
	}
	Static_Unlock();
}

/*
 * 0x00493170 - HandlePacket_POSTLOGIN_Player
 *
 * Parses the POSTLOGIN packet (encryption key, character name and
 * password) and hands off to PostLogin.
 */
void
HandlePacket_POSTLOGIN_Player(CPlayer *this, uint8_t *buf)
{
	char *characterPassword;
	uint32_t off;
	char *characterName;
	uint32_t encryptionKey;

	off = 0;
	GetDWord(buf, &off, &encryptionKey);
	GetString(buf, &off, &characterName, 30);
	GetString(buf, &off, &characterPassword, 30);
	PostLogin(this, 0);
}

/*
 * 0x004931CF - HandlePacket_POSTMSG
 *
 * Stub. Reads title (80b) + body (0x0002000b) strings, discards them.
 */
void
HandlePacket_POSTMSG(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	char *title, *body;

	off = 0;
	GetString(buf, &off, &title, 0x50);
	GetString(buf, &off, &body, 0x2000);
	USED(this);
}

/*
 * 0x0049320F - HandlePacket_CHECK_VER
 *
 * Stub. Reads client version + build DWORDs, discards them.
 */
void
HandlePacket_CHECK_VER(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t clientVersion, clientBuild;

	off = 0;
	GetDWord(buf, &off, &clientVersion);
	GetDWord(buf, &off, &clientBuild);
	USED(this);
}

/*
 * 0x00493248 - HandlePacket_GumpMenuSelection
 *
 * MODIFIED: added payload bounds checking. The binary has no bounds
 * checks on switchCount, textEntryCount, or textLen from the packet,
 * so a malformed or truncated packet causes unbounded reads past the
 * packet data, leading to heap corruption from garbage-driven
 * allocations and an infinite loop in UString_Length.
 *
 * Generic gump menu response. Reads: serial(DWord), gumpID(DWord),
 * buttonID(DWord), switchCount(DWord), switches[](DWord each),
 * textEntryCount(DWord), then per entry: entryID(Word), textLength(Word),
 * unicodeText[](Word each).
 * Validates entity exists via FindBySerial (hash table at 0x0048B8CA).
 * Fires script event 0x37 with gumpID, buttonID, switchList, textList.
 * Binary uses CList for switch/text lists and CUString for text content.
 */
void
HandlePacket_GumpMenuSelection(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint32_t gumpID;
	uint32_t buttonID;
	uint32_t switchCount;
	uint32_t textEntryCount;
	uint32_t i, j;
	uint32_t switchVal;
	uint16_t textLen;
	uint16_t textEntryId;
	uint16_t unicodeChar;
	CItem *entity;
	CList switchList;
	CList textList;
	CUString tempString;
	uint16_t packetLen;
	uint32_t payloadLen;

	// Read packet length from dynamic-size header (buf[1-2], host order).
	memcpy(&packetLen, &buf[1], 2);
	payloadLen = (packetLen > 3) ? packetLen - 3 : 0;

	CList_Constructor(&switchList);
	CList_Constructor(&textList);
	CUString_DefaultConstructor(&tempString);

	off = 0;
	GetDWord(buf, &off, &serial);
	GetDWord(buf, &off, &gumpID);
	GetDWord(buf, &off, &buttonID);

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL) {
		CUString_Destructor(&tempString);
		CList_Destructor(&textList);
		CList_Destructor(&switchList);
		return;
	}

	GetDWord(buf, &off, &switchCount);

	for (i = 0; i < switchCount && off + 4 <= payloadLen; i++) {
		GetDWord(buf, &off, &switchVal);
		CList_Append(&switchList, 0, switchVal);
	}

	if (off + 4 > payloadLen)
		goto done;
	GetDWord(buf, &off, &textEntryCount);
	for (i = 0; i < textEntryCount && off + 4 <= payloadLen; i++) {
		GetWord(buf, &off, &textEntryId);
		GetWord(buf, &off, &textLen);

		CList_Append(&textList, 0, (uint32_t)textEntryId);

		CUString_AssignCStr(&tempString, (const void *)L"");
		for (j = 0; j < textLen && j < 0xEF; j++) {
			if (off + 2 > payloadLen)
				break;
			GetWord(buf, &off, &unicodeChar);
			CUString_ConcatChar(&tempString, unicodeChar);
		}

		CList_Append(&textList, 2, (uintptr_t)&tempString);
	}

	// Custom: route GM player-menu gump (id GM_PLAYER_MENU_GUMP_ID) to its
	// C handler instead of the Wombat script event.
	if (gumpID == GM_PLAYER_MENU_GUMP_ID) {
		GM_HandlePlayerMenuResponse(this, buttonID);
		goto done;
	}

	Entity_ExecuteEvent(&entity->resourceEntity.entity, GumpResponse, (uintptr_t)gumpID, (uintptr_t)this->mobile.container.item.serial, (int)buttonID, &switchList, &textList);

done:
	CUString_Destructor(&tempString);
	CList_Destructor(&textList);
	CList_Destructor(&switchList);
}

/*
 * Second-level GM/editor tool handlers, dispatched only when pflags has
 * PlayerIsEditing set. The binary uses a 20-entry byte map and parallel
 * address table to dispatch the subtype. All entries are development
 * tools for the world editor, not game logic.
 */

/*
 * 0x00493495 - HandlePacket_REQ_MOVE (packet 0x02)
 *
 * Demo/1.25.x: 3 bytes - direction(1), sequence(1)
 * Client 1.26.0: 7 bytes - direction(1), sequence(1), fastwalkKey(4)
 * The extra 4-byte fast-walk prevention key in 1.26.0 is ignored
 * by our server (anti-speed-hack not implemented).
 */
void
HandlePacket_REQ_MOVE(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t direction, sequence;

	off = 0;
	GetByte(buf, &off, &direction);
	GetByte(buf, &off, &sequence);
	// Client 1.26.0: 4-byte fast-walk key follows (off 2-5). Ignored.

	CPlayer_HandleMovement(this, direction, sequence);
}

/*
 * 0x004934DE - HandlePacket_HARDWARE_INFO
 *
 * Telemetry stub. Reads all hardware info fields and discards them.
 */
void
HandlePacket_HARDWARE_INFO(CPlayer *this, uint8_t *buf)
{

	uint32_t off;
	uint8_t valByte;
	uint16_t valWord;
	char *str;
	uint32_t valDWord;

	USED(this);

	off = 0;
	GetByte(buf, &off, &valByte);
	GetWord(buf, &off, &valWord);
	GetByte(buf, &off, &valByte);
	GetString(buf, &off, &str, 32);
	GetString(buf, &off, &str, 32);
	GetString(buf, &off, &str, 32);
	GetString(buf, &off, &str, 32);
	GetWord(buf, &off, &valWord);
	GetWord(buf, &off, &valWord);
	GetDWord(buf, &off, &valDWord);
	GetDWord(buf, &off, &valDWord);
	GetDWord(buf, &off, &valDWord);
}

/*
 * 0x004935E7 - Speech_BroadcastDead
 *
 * Dead speaker speech broadcast. Garbles text (spaces preserved,
 * others become 'O' or 'o' via GetRandomRange). Iterates nearby
 * players via GetNearbyPlayers/CVector. GMs/counselors always hear
 * original. Manifesting dead: alive listeners hear garbled (spirit
 * speakers hear original). Non-manifesting: only dead hear.
 */
static void
Speech_BroadcastDead(CPlayer *speaker, uint8_t speechType, char *text, uint16_t hue, uint16_t font, uint16_t range)
{
	uint8_t obuf[0x42C];
	CVector nearby;
	uintptr_t *ptr;
	int len, i;
	char *garbled;

	len = (int)strlen(text);
	garbled = (char *)malloc((size_t)(len + 1));
	for (i = 0; i < len; i++) {
		if (text[i] == ' ')
			garbled[i] = ' ';
		else
			garbled[i] = (char)(GetRandomRange(0, 1) * 32 + 0x4F);
	}
	garbled[i] = '\0';

	CVector_Constructor(&nearby, "");
	GetNearbyPlayers(&nearby, &speaker->mobile.container.item.resourceEntity.entity.location, (int)(range & 0xFFFF));

	for (ptr = (uintptr_t *)nearby.begin; ptr < (uintptr_t *)nearby.end; ptr++) {
		CPlayer *p = (CPlayer *)*ptr;

		if (CEntity_CanSee((CItem *)speaker, (CItem *)p, 1)) {
			if (CPlayer_IsEditing(p) || CPlayer_IsCounselor(p)) {
				PacketManager_MakePacket_TEXT(obuf, (CItem *)speaker, (CItem *)p, speechType, text, hue, font);
				SendToClient((CItem *)p, obuf, -1);
				continue;
			}
			if (!(speaker->pflags & PlayerIsManifesting)) {
				if (!VT_IsDead((CItem *)p))
					continue;
			}
			if (VT_IsDead((CItem *)p)) {
				PacketManager_MakePacket_TEXT(obuf, (CItem *)speaker, (CItem *)p, speechType, text, hue, font);
			} else if (p->pflags & PlayerSpiritSpeak) {
				PacketManager_MakePacket_TEXT(obuf, (CItem *)speaker, (CItem *)p, speechType, text, hue, font);
			} else {
				PacketManager_MakePacket_TEXT(obuf, (CItem *)speaker, (CItem *)p, speechType, garbled, hue, font);
			}
			SendToClient((CItem *)p, obuf, -1);
		} else {
			if (CPlayer_IsEditing(p) || CPlayer_IsCounselor(p)) {
				PacketManager_MakePacket_TEXT(obuf, (CItem *)speaker, (CItem *)p, speechType, text, hue, font);
				SendToClient((CItem *)p, obuf, -1);
			}
		}
	}

	free(garbled);
	CVector_Destructor(&nearby);
}

/*
 * 0x004938F6 - Speech_BroadcastAlive
 *
 * Alive speaker speech broadcast. Builds TEXT packet, adjusts
 * location by GetHeight/2 z-offset, broadcasts via
 * BroadcastToNearbyWithLOS.
 */
static void
Speech_BroadcastAlive(CPlayer *speaker, uint8_t speechType, char *text, uint16_t hue, uint16_t font, uint16_t range)
{
	uint8_t obuf[0x42C];
	CLocation loc;
	CLocation delta;
	int height;

	PacketManager_MakePacket_TEXT(obuf, (CItem *)speaker, NULL, speechType, text, hue, font);

	height = ((int (*)(void *))VT_FN((CItem *)speaker, VT_GET_HEIGHT))(speaker);
	CLocation_Constructor3D(&delta, 0, 0, (int16_t)(height / 2));
	CLocation_AddWrapped(CEntity_GetLocation(&speaker->mobile.container.item.resourceEntity.entity), &loc, &delta);

	BroadcastToNearbyWithLOS(obuf, &loc, (int)(range & 0xFFFF));
}

/*
 * 0x0049398C - HandlePacket_SPEECH
 *
 * Parse speech packet. Squelch check, font/hue forcing for non-GMs,
 * whitespace validation. Counselor text logged via EventLogger_Log.
 * GM/counselor commands delegated to GmCommandDispatch (0x0044E518).
 * Reveal hidden on speech via Entity_ExecuteEvent
 * "uninvis" event. Switch on speechType to dead/alive speech
 * sub-functions with type-specific ranges.
 */
void
HandlePacket_SPEECH(CPlayer *this, uint8_t *buf)
{
	uint8_t obuf[0x42C];
	uint32_t off;
	uint8_t speechType;
	uint16_t hue;
	uint16_t font;
	char *text;
	CPlayer *p;
	int hasNonSpace;
	int i;

	p = NULL;
	off = 0;

	if (CMobile_IsSquelched(&this->mobile)) {
		CPlayer_SystemMessage(this, "You can not say anything, you have been squelched.");
		return;
	}

	GetByte(buf, &off, &speechType);
	GetWord(buf, &off, &hue);
	GetWord(buf, &off, &font);
	GetString(buf, &off, &text, 241);

	if (!CPlayer_IsEditing(this)) {
		font = 3;
		if ((hue & 0xFFFF) < 2 || (hue & 0xFFFF) > 0x3E9)
			hue = 0x3E9;
	}

	hasNonSpace = 0;
	for (i = 0; text[i] != '\0' && i < 0xF0; i++) {
		if (!isspace((unsigned char)text[i])) {
			hasNonSpace = 1;
			break;
		}
	}
	if (!hasNonSpace)
		return;

#ifdef LOG_VERBOSE
	if (CPlayer_IsCounselor(this)) {
		EventLogger_Log(&g_EventLogger, this->accountNum, (uint32_t)(uint8_t)this->characterNum, CMobile_GetSerial(&this->mobile),
		        ((const char *(*)(void *))VT_FN((CItem *)this, VT_GET_NAME))(this), "counselortext", "misc", text);
	}
#endif

	if (CPlayer_IsCounselor(this) || CPlayer_IsGameMaster(this)) {
		if (text[0] == '.' || text[0] == '=') {
			GmCommandDispatch(&g_HelpQueue, this, text);
			return;
		}
	} else if (CPlayer_IsTestCenter(this)) {
		if (text[0] == '.' || text[0] == '=') {
			TC_CommandDispatch(this, text);
			return;
		}
	}

	// Reveal hidden alive player on speech
	if (!VT_IsDead((CItem *)this) && CItem_HasItemFlag((CItem *)this, 2)) {
		Entity_ExecuteEvent((CEntity *)this, 0x16, CMobile_GetSerial(&this->mobile), "uninvis", "v");
	}

	switch (speechType) {
	case 0:
	case 3:
	case 4:
		// Normal speech, range 9
		if (VT_IsDead((CItem *)this))
			Speech_BroadcastDead(this, speechType, text, hue, font, 9);
		else
			Speech_BroadcastAlive(this, speechType, text, hue, font, 9);
		CMobile_SetSpeechHue(&this->mobile, hue);
		if (!CPlayer_HasDeadFlag(this)) {
			CWorld_SpeechNotifyNearby(
			        &this->mobile.container.item, this->mobile.container.item.serial, &this->mobile.container.item.resourceEntity.entity.location, text, 9);
		}
		break;

	case 8:
		// Whisper, range 1
		if (VT_IsDead((CItem *)this))
			Speech_BroadcastDead(this, speechType, text, hue, font, 1);
		else
			Speech_BroadcastAlive(this, speechType, text, hue, font, 1);
		CMobile_SetSpeechHue(&this->mobile, hue);
		break;

	case 9:
		// Yell, range 18
		if (VT_IsDead((CItem *)this))
			Speech_BroadcastDead(this, speechType, text, hue, font, 18);
		else
			Speech_BroadcastAlive(this, speechType, text, hue, font, 18);
		CMobile_SetSpeechHue(&this->mobile, hue);
		CWorld_SpeechNotifyNearby(&this->mobile.container.item, this->mobile.container.item.serial, &this->mobile.container.item.resourceEntity.entity.location, text, 18);
		break;

	case 1:
		// Broadcast (GM only)
		if (!CPlayer_IsEditing(this))
			break;
		for (p = g_PlayerList.head; p != NULL; p = p->next) {
			PacketManager_MakePacket_TEXT(obuf, (CItem *)this, (CItem *)p, speechType, text, hue, font);
			SendToClient((CItem *)p, obuf, -1);
		}
		break;

	case 2:
		// Emote: dead players cannot emote
		if (VT_IsDead((CItem *)this))
			break;
		Speech_BroadcastAlive(this, speechType, text, hue, font, 7);
		break;

	default:
		break;
	}
}

/*
 * 0x00493E4C - HandlePacket_REQ_OBJUSE
 *
 * Double-click / use object handler. Reads serial (DWord) with high bit
 * 0x80000000 as paperdoll flag. Strips high bit, FindEntityInRange,
 * ValidateEntityAccess, then DispatchDoubleClick (0x004DCD04).
 */
void
HandlePacket_REQ_OBJUSE(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	int isPaperdoll;
	CItem *entity;

	off = 0;
	GetDWord(buf, &off, &serial);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial & 0x7FFFFFFF, 18);
	if (entity == NULL)
		return;

	isPaperdoll = (serial & 0x80000000) != 0;
	serial &= 0x7FFFFFFF;

	ValidateEntityAccess(this, &serial, 1);
	if (serial == 0)
		return;

	DispatchDoubleClick(this, entity, isPaperdoll);
}

/*
 * 0x00493EDE - PlayDropSoundAtItem
 *
 * The same gold/backpack sound selection as PlayDropSound, but played
 * on the item itself rather than on a separate listener entity.
 */
static __attribute__((unused)) void
PlayDropSoundAtItem(CItem *item, CItem *container)
{
	int amount;

	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x0EED) {
		amount = ((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);
		if (amount <= 1) {
			PlaySoundAtEntity(item, 0x35, 0);
		} else if (amount <= 5) {
			PlaySoundAtEntity(item, 0x36, 0);
		} else {
			PlaySoundAtEntity(item, 0x37, 0);
		}
	} else if (container != NULL && (VT_IsMobile(container) || (CEntity_GetBodyType(container) & 0xFFFF) == 0x0E75 || (CEntity_GetBodyType(container) & 0xFFFF) == 0x0E76)) {
		PlaySoundAtEntity(item, 0x48, 0);
	} else {
		PlaySoundAtEntity(item, 0x42, 0);
	}
}

/*
 * 0x00493FAF - PlayDropSound
 *
 * Gold (0xEED): amount<=1 → 0x35, amount<=5 → 0x36, else → 0x37.
 * Binary non-gold path (0x0049401A): checks vtable[0xD0] (IsMobile) on container,
 * then bodyType == 0xE75 or 0xE76 (backpack types). If any match → 0x48.
 * All other cases (ground drop, non-backpack container) → 0x42 default.
 */
static void
PlayDropSound(CItem *item, CItem *container, CItem *entity)
{
	int amount;

	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x0EED) {
		amount = ((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);
		if (amount <= 1) {
			SendSoundToEntity(entity, 0x35, 0);
		} else if (amount <= 5) {
			SendSoundToEntity(entity, 0x36, 0);
		} else {
			SendSoundToEntity(entity, 0x37, 0);
		}
	} else if (container != NULL) {
		if (VT_IsMobile(container) || (CEntity_GetBodyType(container) & 0xFFFF) == 0x0E75 || (CEntity_GetBodyType(container) & 0xFFFF) == 0x0E76) {
			SendSoundToEntity(entity, 0x48, 0);
		} else {
			SendSoundToEntity(entity, 0x42, 0);
		}
	} else {
		SendSoundToEntity(entity, 0x42, 0);
	}
}

/*
 * 0x00494080 - SendEquipSound
 *
 * Wrapper: plays equip sound (0x57) on the entity via SendSoundToEntity.
 */
static void
SendEquipSound(CItem *entity)
{
	SendSoundToEntity(entity, 0x57, 0);
}

/*
 * 0x004942A6 - HandlePacket_REQ_GETOBJ
 *
 * Pick up item. Reads serial(DWord) + amount(Word). Sets player
 * actionState=4 (picking up). Validates range, LOS, IsHair, container
 * ownership, bank access, IsAtHome, accessible contents, and weight
 * capacity. On success the item is attached to player->equipment[0] and
 * an OBJMOVE is broadcast; on failure a GETOBJ_FAILED (0x27) is sent
 * with a reason code.
 *
 * MODIFIED: uses GetNearbyPlayers-based spatial queries instead of the
 * CItemMap spatial grid.
 */
void
HandlePacket_REQ_GETOBJ(CPlayer *this, uint8_t *buf)
{
	uint8_t obuf[8];
	uint8_t objmoveBuf[32];
	CLocation locD8;        // var_28h: item entity location (direct field copy)
	CLocation locE8;        // var_18h: item virtual location (VT_GET_LOCATION)
	CLocation loc98;        // var_68h: player location (saved before WasGotten)
	CLocation eyeLoc;       // var_90h: player eye position for LOS check
	uint32_t off;
	uint32_t serial;
	uint16_t amount;
	uint32_t playerSerial;  // var_48h
	uint32_t itemSerial;    // var_6ch
	CItem *item;            // var_10h
	CItem *held;            // var_80h
	CItem *topMobile;       // var_60h
	CItem *savedParent;     // var_1ch
	CItem *topContainer;    // var_88h
	CItem *savedOrigItem;   // var_50h
	int isStealing;         // var_20h
	int didSplit;           // var_84h -> 7ch area
	CVector contItems;
	char typeFlag;
	CMobile *mob;

	CLocation_Init(&locD8);
	CLocation_Init(&locE8);
	CLocation_Init(&loc98);
	savedOrigItem = NULL;
	isStealing = 0;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetWord(buf, &off, &amount);

	mob = &this->mobile;

	if (CPlayer_IsBusy(this) || CPlayer_HasDeadFlag(this) || VT_IsDead(&mob->container.item)) {
		PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0xFF);
		SendToClient((CItem *)this, obuf, -1);
		return;
	}

	mob->actionState = 4;

	playerSerial = CMobile_GetSerial(mob);

	item = CWorld_FindEntityInRange(g_World, &mob->container.item.resourceEntity.entity, serial, 18);

	if (item == NULL || !((int (*)(void *, void *))VT_FN(item, VT_IS_MOVEABLE))(item, this)) {
		PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
		SendToClient((CItem *)this, obuf, -1);
		// If item exists and has corpse equipment,
		// send entity update to refresh client display
		if (item != NULL) {
			if (((int (*)(void *))VT_FN(item, VT_HAS_CORPSE_EQ))(item))
				((void (*)(void *, void *, int))VT_FN(item, VT_SEND_ENTITY_UPDATE))(item, (CItem *)this, 1);
		}
		return;
	}

	// Already holding an item - drop it first
	held = mob->equipment[0];
	if (held != NULL) {
		if (!held->resourceEntity.entity.removedFromWorld)
			((void (*)(void *))VT_FN(held, VT_HIDE))(held);
		if (!((int (*)(void *))VT_FN(held, VT_RETURN_TO_TRACKED))(held)) {
			CLocation *loc = ((CLocation * (*)(void *)) VT_FN(&mob->container.item, VT_GET_LOCATION))(mob);
			((void (*)(void *, CLocation *))VT_FN(held, VT_DROP_AT_FEET))(held, loc);
		}
	}

	// Returns 0 for resource-flagged items with insufficient resources
	if (!((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item)) {
		if (!CPlayer_IsEditing(this)) {
			CPlayer_SystemMessage(this, "This item needs resources.");
			PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
			SendToClient((CItem *)this, obuf, -1);
			if (((int (*)(void *))VT_FN(item, VT_HAS_CORPSE_EQ))(item))
				((void (*)(void *, void *, int))VT_FN(item, VT_SEND_ENTITY_UPDATE))(item, (CItem *)this, 1);
			return;
		}
	}

	{
		CLocation *itemLoc = ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item);
		CLocation_SetLoc(&locE8, itemLoc);
	}
	CLocation_SetLoc(&locD8, &item->resourceEntity.entity.location);

	if (!CPlayer_IsEditing(this)) {
		int dist = ChebyshevDistXY((int16_t)locE8.x, (int16_t)locE8.y, (int16_t)this->mobile.container.item.resourceEntity.entity.location.x,
		        (int16_t)this->mobile.container.item.resourceEntity.entity.location.y);
		if (dist > 2) {
			PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x01);
			SendToClient((CItem *)this, obuf, -1);
			return;
		}

		// Eye position = player location, z+8
		CLocation_Init(&eyeLoc);
		CLocation_CopyFrom(&eyeLoc, &this->mobile.container.item.resourceEntity.entity.location);
		eyeLoc.z += 8;
		if (!CTerrainManager_LOSRaycast(&locE8, &eyeLoc, 2)) {
			PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x02);
			SendToClient((CItem *)this, obuf, -1);
			return;
		}

		// Hair items cannot be picked up
		if (VT_IsHair(item)) {
			PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
			SendToClient((CItem *)this, obuf, -1);
			return;
		}
	}

	savedParent = item->parent;
	topMobile = CItem_FindTopContainerMobile(item);

	if (topMobile != NULL && VT_IsMobile(topMobile)) {
		if (topMobile != (CItem *)mob) {
			isStealing = 1;
		} else {
			// Check bank box access
			if (CItem_IsInBankBox(savedParent)) {
				if (!CPlayer_IsEditing(this)) {
					if (!CResourceEntity_HasTag((CItem *)this, "bankOpenLoc", 3)) {
						// No bank tag - deny
						PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
						SendToClient((CItem *)this, obuf, -1);
						return;
					}
					// Verify player is still at bank location
					{
						CLocation bankLoc;
						CLocation_Init(&bankLoc);
						CResourceEntity_GetTagLoc((CItem *)this, "bankOpenLoc", &bankLoc);
						if (CLocation_NotEqual(&bankLoc, CEntity_GetLocation(&mob->container.item.resourceEntity.entity))) {
							CResourceEntity_DetachScript((CItem *)this, "bankOpenLoc");
							PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
							SendToClient((CItem *)this, obuf, -1);
							return;
						}
					}
				}
			}
		}
	}

	if (CEntity_IsAtHome(item)) {
		isStealing = 1;
		topMobile = NULL;
	}

	if (item->parent != NULL) {
		if (CEntity_IsAtHome(item->parent)) {
			isStealing = 1;
			topMobile = NULL;
		} else if (!CPlayer_IsEditing(this)) {
			// Check if the parent container allows access
			if (!((int (*)(void *))VT_FN(item->parent, VT_HAS_ACCESSIBLE_CONTENTS))(item->parent)) {
				PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
				SendToClient((CItem *)this, obuf, -1);
				return;
			}
		}
	}

	// Bounty item exception (severed heads 0x3584-0x3591)
	{
		uint16_t bt = CEntity_GetBodyType(item) & 0xFFFF;
		if (bt >= 0x3584 && bt <= 0x3591)
			isStealing = 0;
	}

	// overloadStealing tag override (overwrites isStealing if tag exists)
	CItem_GetTagInt(item, "overloadStealing", &isStealing);

	topContainer = item;
	while (topContainer->parent != NULL)
		topContainer = topContainer->parent;

	if (isStealing != 0 && !CPlayer_IsEditing(this)) {
		int acc = (int)(intptr_t)Entity_ExecuteEvent((CEntity *)topContainer, 0x3D, 5, CMobile_GetSerial(mob), item->serial);
		if (acc == 0) {
			PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
			SendToClient((CItem *)this, obuf, -1);
			return;
		}
		if (!World_ValidatePlayerEntity((CItem *)this, playerSerial) || !World_ValidateEntity(item, serial))
			return;
	}

	CLocation_SetLoc(&loc98, &this->mobile.container.item.resourceEntity.entity.location);

	itemSerial = item->serial;

	// WasGotten event
	{
		int wg = (int)(intptr_t)Entity_ExecuteEvent((CEntity *)item, 0x1E, this->mobile.container.item.serial);
		if (wg == 0) {
			// Script blocked pickup
			if (CWorld_FindBySerial(g_World, itemSerial) != NULL) {
				if (((int (*)(void *))VT_FN(item, VT_HAS_CORPSE_EQ))(item))
					((void (*)(void *, void *, int))VT_FN(item, VT_SEND_ENTITY_UPDATE))(item, (CItem *)this, 1);
			}
			PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
			SendToClient((CItem *)this, obuf, -1);
			return;
		}
	}

	if (!CPlayer_IsEditing(this)) {
		int acc = (int)(intptr_t)Entity_ExecuteEvent((CEntity *)topContainer, 0x3D, 4, CMobile_GetSerial(mob), item->serial);
		if (acc == 0) {
			PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
			SendToClient((CItem *)this, obuf, -1);
			return;
		}

		if (!World_ValidatePlayerEntity((CItem *)this, playerSerial) || !World_ValidateEntity(item, serial))
			return;
	}

	typeFlag = 0;
	CVector_Constructor(&contItems, &typeFlag);
	didSplit = 0;

	// If not a container, collect children for later restore
	if (!((int (*)(void *))VT_FN(item, VT_HAS_CONTAINER))(item))
		CItem_GetContainerItems(item, &contItems);

	if (((int (*)(void *))VT_FN(item, VT_HAS_RESOURCE_FLAG))(item) && (amount & 0xFFFF) != 0 &&
	        (int)(amount & 0xFFFF) < ((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item)) {
		savedOrigItem = item;
		item = CItem_SplitByAmount(item, (uint16_t)amount);
		didSplit = 1;
	} else {
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);
	}

	if (((int (*)(void *, void *, int))VT_FN(item, VT_EQUIP_ON_MOBILE))(item, mob, 0) == 1) {
		SendEquipSound((CItem *)this);

		// OBJMOVE broadcast if item location changed
		if (loc98.x != locE8.x || loc98.y != locE8.y) {
			PacketManager_MakePacket_OBJMOVE(objmoveBuf, CEntity_GetBodyType(item), CItem_IsStackable(item), item->resourceEntity.entity.color,
			        ((uint16_t (*)(void *))VT_FN(item, VT_GET_AMOUNT))(item), ((CItem *)this)->serial, loc98.x, loc98.y, (uint8_t)loc98.z, 0, locE8.x, locE8.y,
			        (uint8_t)locE8.z);
			CPlayerList_BroadcastToTwoLocs(objmoveBuf, &locE8, &loc98, 0x12, (CItem *)this);
		}

		CBlockManager_RestoreItems(&g_SpatialGrid, &contItems);
	} else {
		PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x04);
		SendToClient((CItem *)this, obuf, -1);

		if (CWorld_FindBySerial(g_World, itemSerial) != NULL) {
			if (((int (*)(void *))VT_FN(item, VT_HAS_CORPSE_EQ))(item))
				((void (*)(void *, void *, int))VT_FN(item, VT_SEND_ENTITY_UPDATE))(item, (CItem *)this, 1);
		}

		if (savedOrigItem != NULL) {
			CItem_MergeIntoWrapper(item, savedOrigItem);
		} else {
			if (!((int (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item)) {
				CLocation *loc = ((CLocation * (*)(void *)) VT_FN(&mob->container.item, VT_GET_LOCATION))(mob);
				((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
			}
		}

		PacketManager_MakePacket_GETOBJ_FAILED(obuf, 0x00);
		SendToClient((CItem *)this, obuf, -1);
	}

	if (didSplit == 1) {
		if (!ValidateInWorld(item))
			item = NULL;
	}

	CVector_Destructor(&contItems);
}

/*
 * 0x00494D6F
 * Drop item. Reads serial(DWord) + x(Word) + y(Word) + z(Byte) + containerSerial(DWord).
 * Item being dropped is player->equipment[0] (held item on cursor).
 * containerSerial == 0xFFFFFFFF means drop on ground; otherwise drop into container.
 */

/*
 * 0x00494C27 - ValidateSerials
 *
 * Cdecl, 6 args. Re-verifies 3 serial/entity-pointer pairs after
 * callbacks that might invalidate them. For the first pair, also checks
 * vtable[0x18] (VT_IS_PLAYER). Returns 1 if all valid, 0 otherwise.
 * Called 4 times from HandlePacket_DROP (0x00494D6F) after script/event
 * callbacks to ensure entity pointers haven't gone stale.
 */
static int
ValidateSerials(CItem *expectedPlayer, uint32_t playerSerial, CItem *expectedItem, uint32_t itemSerial, CItem *expectedTarget, uint32_t targetSerial)
{
	CItem *found;

	// Validate player serial
	found = CWorld_FindBySerial(g_World, playerSerial);
	if (found == NULL)
		return 0;
	if (!VT_IsPlayer(found))
		return 0;
	if (found != expectedPlayer)
		return 0;

	// Validate item serial
	found = CWorld_FindBySerial(g_World, itemSerial);
	if (found == NULL)
		return 0;
	if (found != expectedItem)
		return 0;

	// Validate target serial
	found = CWorld_FindBySerial(g_World, targetSerial);
	if (found == NULL)
		return 0;
	if (found != expectedTarget)
		return 0;

	return 1;
}

/*
 * 0x00494CAE - DropObj_Bounce
 *
 * Sends DROPOBJ_FAILED packet to nearby clients, then bounces the item
 * back. If the item is in the world, hides it. Tries VT_RETURN_TO_TRACKED
 * first; if that fails, drops the item at the source entity's location.
 */
static void
DropObj_Bounce(CItem *source, CItem *item, CLocation *loc)
{
	uint8_t buf[8];

	PacketManager_MakePacket_DROPOBJ_FAILED(buf, loc->x, loc->y);
	SendToClient(source, buf, -1);

	if (!(VT_IsRemoved(item) & 0xFF))
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	if (!((int (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item)) {
		CLocation *srcLoc = (CLocation *)((void *(*)(void *))VT_FN(source, VT_GET_LOCATION))(source);
		((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, srcLoc);
	}
}

/*
 * 0x00494D33 - GetCanHoldFailReason
 *
 * Returns an error string for container hold failures.
 * Code 1=weight, 2=items, 3=no container.
 */
static const char *
GetCanHoldFailReason(int code)
{
	if (code == 1)
		return "That container cannot hold more weight.";
	if (code == 2)
		return "That container cannot hold more items.";
	if (code == 3)
		return "There is no container to put that in.";
	return "GetCanHoldFailReason error.";
}

/*
 * 0x00494D6F - HandlePacket_REQ_DROPOBJ
 *
 * Decompilation of the binary drop handler. Handles dropping items on
 * ground, into containers, onto players (trade), and onto NPCs (sell).
 * Uses vtable dispatch for entity type checks and container operations.
 *
 * MODIFIED: when the player is in GM editing mode and the in-range
 * container lookup fails, fall back to CWorld_FindBySerial so the GM
 * can drop into a container the binary's spatial scan would miss
 * (e.g. a remote bank box parented to a far-away mobile, opened via
 * the custom .bank <name|0xSERIAL> command).
 */
void
HandlePacket_REQ_DROPOBJ(CPlayer *this, uint8_t *buf)
{
	CItem *contEntity = NULL;
	CLocation dropLoc, origLoc, targetLoc;
	uint32_t off = 0;
	uint32_t serialItem;
	uint16_t dropX, dropY;
	uint8_t dropZ;
	uint32_t contSerial;
	CItem *item;
	int amount;
	uint32_t playerSerial, contSerial_saved;
	uint32_t itemSerial_saved, contEntitySerial_saved;
	int eventResult;

	CLocation_Init(&dropLoc);
	CLocation_Init(&origLoc);

	GetDWord(buf, &off, &serialItem);
	GetWord(buf, &off, &dropX);
	GetWord(buf, &off, &dropY);
	GetByte(buf, &off, &dropZ);
	GetDWord(buf, &off, &contSerial);
	CLocation_Set(&dropLoc, dropX, dropY, (int16_t)(int8_t)dropZ);

	item = this->mobile.equipment[0];
	if (item == NULL)
		return;

	serialItem = item->serial;

	{
		int lockedVal = 0;
		if (CItem_GetTagInt(item, "lockedContainer", &lockedVal)) {
			if ((uint32_t)lockedVal != contSerial) {
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				return;
			}
		}
	}

	if (serialItem == contSerial || !((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item)) {
		DropObj_Bounce((CItem *)this, item, &dropLoc);
		return;
	}

	{
		CLocation *itemLoc = (CLocation *)((void *(*)(void *))VT_FN(item, VT_GET_LOCATION))(item);
		CLocation_SetLoc(&origLoc, itemLoc);
	}

	CLocation_Init(&targetLoc);

	if (contSerial == 0xFFFFFFFF) {
		CLocation_Set(&targetLoc, dropX, dropY, (int16_t)(int8_t)dropZ);
	} else {
		contEntity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, contSerial, 18);

		// CUSTOM: GMs in editing mode bypass the range check so that
		// .bank <name> on a far-away player accepts drops into the
		// remote bank box. The target container is parented to a
		// mobile outside the spatial-grid scan from the GM's tile.
		if (contEntity == NULL && CPlayer_IsEditing(this)) {
			contEntity = CWorld_FindBySerial(g_World, contSerial);
		}

		if (contEntity == NULL) {
			DropObj_Bounce((CItem *)this, item, &dropLoc);
			return;
		}
		if (VT_IsMobile2(item)) {
			if (CContainer_ContainsEntity((CContainer *)item, contEntity, 1)) {
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				return;
			}
		}

		{
			CLocation *contLoc = (CLocation *)((void *(*)(void *))VT_FN(contEntity, VT_GET_LOCATION))(contEntity);
			CLocation_SetLoc(&targetLoc, contLoc);
		}

		{
			uint16_t contBody = CEntity_GetBodyType(contEntity);
			if (contBody == 0xFA6 || contBody == 0xFAD || contBody == 0xE1C) {
				uint16_t itemBody = CEntity_GetBodyType(item);
				if (itemBody < 0x3584 || itemBody > 0x3591) {
					DropObj_Bounce((CItem *)this, item, &dropLoc);
					return;
				}
			}
		}
	}

	if (!CPlayer_IsEditing(this)) {
		if (contEntity != NULL) {
			CItem *topParent = contEntity;
			while (topParent->parent != NULL)
				topParent = topParent->parent;

			if (topParent != contEntity && topParent != (CItem *)this) {
				if (VT_IsMobile(topParent)) {
					int acc = (int)(intptr_t)Entity_ExecuteEvent(&topParent->resourceEntity.entity, 0x3D, 7, CMobile_GetSerial(&this->mobile), serialItem);
					if (acc != 0) {
						DropObj_Bounce((CItem *)this, item, &dropLoc);
						return;
					}
				}
			}
		}

		{
			int dist = ChebyshevDistXY(targetLoc.x, targetLoc.y, this->mobile.container.item.resourceEntity.entity.location.x,
			        this->mobile.container.item.resourceEntity.entity.location.y);
			if (dist > 2) {
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				return;
			}
		}

		{
			CLocation playerEyeLoc;
			CLocation_Init(&playerEyeLoc);
			CLocation_CopyFrom(&playerEyeLoc, &this->mobile.container.item.resourceEntity.entity.location);
			playerEyeLoc.z += 8;
			if (!CTerrainManager_LOSRaycast(&playerEyeLoc, &targetLoc, 2)) {
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				return;
			}
		}
	}

	amount = 1;
	if (item != NULL) {
		amount = ((int (*)(void *))VT_FN(item, VT_GET_HEIGHT))(item);
		if (amount <= 0)
			amount = 1;
	}

	if (item == NULL)
		goto cleanup;

	if (contSerial == 0xFFFFFFFF) {
		if (CPlayer_IsEditing(this)) {
			if (!CBlockManager_IsValidCoord(&g_SpatialGrid, dropLoc.x, dropLoc.y))
				goto ground_drop_main;
		} else {
			if (!CBlockManager_IsValidCoord(&g_SpatialGrid, dropLoc.x, dropLoc.y))
				goto cleanup;
		}

		if (!(CTerrainManager_CheckMoveBlocked(dropLoc, amount, 0, item, 0) & 2))
			goto cleanup;

ground_drop_main:
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

		if ((dropX & 0xFFFF) != (int)origLoc.x || (dropY & 0xFFFF) != (int)origLoc.y) {
			{
				uint8_t pkt[64];
				uint16_t colorOverride = ((uint16_t (*)(void *))VT_FN(item, VT_GET_AMOUNT))(item);
				PacketManager_MakePacket_OBJMOVE(pkt, CEntity_GetBodyType(item), CItem_IsStackable(item), item->resourceEntity.entity.color, colorOverride, 0,
				        dropX, dropY, dropZ, ((CItem *)this)->serial, origLoc.x, origLoc.y, (uint8_t)origLoc.z);
				CPlayerList_BroadcastToTwoLocs(pkt, &origLoc, &dropLoc, 18, NULL);
			}
		}

		((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, &dropLoc);

		((void (*)(void *))VT_FN(item, VT_REATTACH_SPATIAL))(item);

		Entity_ExecuteEvent(&item->resourceEntity.entity, 0x1B, ((CItem *)this)->serial);

		PlayDropSound(item, NULL, (CItem *)this);

		goto success;
	}

	// Container drop path (0x0049529F)
	if (contEntity == NULL || !VT_IsMobile2(contEntity))
		goto non_container;

	if (CEntity_IsAtHome(contEntity)) {
		uint16_t itemBody = CEntity_GetBodyType(item);
		if (itemBody < 0x3584 || itemBody > 0x3591) {
			if (!CPlayer_IsEditing(this)) {
				CPlayer_SystemMessage(this, "That is not your container, you "
				                            "can't store things here.");
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				return;
			}
		}
	}

	if (CItem_IsInBankBox(contEntity)) {
		if (!CPlayer_IsEditing(this)) {
			if (!CResourceEntity_HasTag((CItem *)this, "bankOpenLoc", 3)) {
				CPlayer_SystemMessage(this, "Access to bank not allowed anymore.");
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				return;
			}
			{
				CLocation bankLoc;
				CLocation_Init(&bankLoc);
				CResourceEntity_GetTagLoc((CItem *)this, "bankOpenLoc", &bankLoc);
				if (CLocation_NotEqual(&bankLoc, CEntity_GetLocation(&((CItem *)this)->resourceEntity.entity))) {
					CResourceEntity_DetachScript((CItem *)this, "bankOpenLoc");
					CPlayer_SystemMessage(this, "Access to bank not allowed "
					                            "anymore.");
					DropObj_Bounce((CItem *)this, item, &dropLoc);
					return;
				}
			}
		}
	}

	PlayDropSound(item, contEntity, (CItem *)this);

	playerSerial = ((CItem *)this)->serial;
	contSerial_saved = contEntity->serial;

	if (contEntity == (CItem *)this)
		goto mobile_container_path;

	if (VT_IsPlayer(contEntity)) {
		if (VT_IsDead(contEntity)) {
			DropObj_Bounce((CItem *)this, item, &dropLoc);
			return;
		}

		eventResult = (int)(intptr_t)Entity_ExecuteEvent(&item->resourceEntity.entity, 0x1B, ((CItem *)this)->serial);

		if (!ValidateSerials((CItem *)this, playerSerial, contEntity, contSerial_saved, item, serialItem))
			return;

		if (CItem_HasScriptByName(item, "trap_")) {
			DropObj_Bounce((CItem *)this, item, &dropLoc);
			goto trade_done;
		}

		if (!(item->resourceEntity.entity.removedFromWorld & 0xFF))
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		CTradeSession_FindOrCreateSession(&g_TradeSessionList, (CPlayer *)contEntity, this, item);
trade_done:
		return;
	}

	if (VT_IsVendor(contEntity)) {
		g_ScriptReturnFlag = 0;

		eventResult = (int)(intptr_t)Entity_ExecuteEvent(&contEntity->resourceEntity.entity, 0x1D, ((CItem *)this)->serial, serialItem);

		if (!ValidateSerials((CItem *)this, playerSerial, contEntity, contSerial_saved, item, serialItem))
			return;

		if (!g_ScriptReturnFlag) {
			DropObj_Bounce((CItem *)this, item, &dropLoc);
			return;
		}

		g_ScriptReturnFlag = 0;
		if (g_ScriptReturnValue == 0) {
			CShopkeeper_SellItemFromPlayer((CNPC *)contEntity, (CItem *)this, item);
			return;
		}

		{
			CItem *dragItem = this->mobile.equipment[0];
			if (dragItem != NULL)
				DropObj_Bounce((CItem *)this, dragItem, &dropLoc);
		}
		return;
	}

mobile_container_path:
	if (VT_IsMobile(contEntity) && contEntity != (CItem *)this)
		goto container_event;

	{
		CItem *walker = contEntity;
		int failReason = 0;

		while (walker->parent != NULL) {
			if (VT_IsMobile(walker->parent))
				break;
			walker = walker->parent;
		}

		if (!((int (*)(void *, CItem *, int *))VT_FN(walker, VT_EQUIP_ITERATE))(walker, item, &failReason)) {
			if (CPlayer_IsEditing(this)) {
				CPlayer_SystemMessage(this, "Your godly powers allow you to "
				                            "over-fill the container.");
			} else {
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				CPlayer_SystemMessage(this, GetCanHoldFailReason(failReason));
				return;
			}
		}
	}

container_event:
	eventResult = (int)(intptr_t)Entity_ExecuteEvent(&contEntity->resourceEntity.entity, 0x1D, ((CItem *)this)->serial, serialItem);

	if (!ValidateSerials((CItem *)this, playerSerial, contEntity, contSerial_saved, item, serialItem))
		return;

	if (eventResult == 0) {
		DropObj_Bounce((CItem *)this, item, &dropLoc);
		return;
	}

	if (!((int (*)(void *))VT_FN(contEntity, VT_HAS_ACCESSIBLE_CONTENTS))(contEntity)) {
		DropObj_Bounce((CItem *)this, item, &dropLoc);
		return;
	}

	if (eventResult != 0 && VT_IsNPC(contEntity)) {
		DropObj_Bounce((CItem *)this, item, &dropLoc);
		return;
	}

	if (VT_IsMobile(contEntity)) {
		int failReason2 = 0;
		if (!((int (*)(void *, CItem *, int *))VT_FN(contEntity, VT_EQUIP_ITERATE))(contEntity, item, &failReason2)) {
			if (CPlayer_IsEditing(this)) {
				CPlayer_SystemMessage(this, "Your godly powers allow you to "
				                            "over-fill the container.");
			} else {
				DropObj_Bounce((CItem *)this, item, &dropLoc);
				CPlayer_SystemMessage(this, GetCanHoldFailReason(failReason2));
				return;
			}
		}
	}

	itemSerial_saved = item->serial;
	contEntitySerial_saved = contEntity->serial;

	item = CWorld_FindBySerial(g_World, itemSerial_saved);
	if (item == NULL)
		return;

	((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	contEntity = CWorld_FindBySerial(g_World, contEntitySerial_saved);
	if (contEntity == NULL)
		return;

	{
		CLocation contLoc;
		CLocation *cl = (CLocation *)((void *(*)(void *))VT_FN(contEntity, VT_GET_LOCATION))(contEntity);
		CLocation_SetLoc(&contLoc, cl);

		if (contLoc.x != origLoc.x || contLoc.y != origLoc.y) {
			uint8_t pkt[64];
			uint16_t colorOverride = ((uint16_t (*)(void *))VT_FN(item, VT_GET_AMOUNT))(item);
			PacketManager_MakePacket_OBJMOVE(pkt, CEntity_GetBodyType(item), CItem_IsStackable(item), item->resourceEntity.entity.color, colorOverride, 0, contLoc.x,
			        contLoc.y, (uint8_t)contLoc.z, ((CItem *)this)->serial, origLoc.x, origLoc.y, (uint8_t)origLoc.z);
			CPlayerList_BroadcastToTwoLocs(pkt, &origLoc, &contLoc, 18, NULL);
		}
	}

	if (VT_IsMobile(contEntity)) {
		uint32_t tmpSerial = item->serial;
		((void (*)(void *, CItem *))VT_FN(item, VT_ADD_TO_EQUIP))(item, contEntity);
		if (CWorld_FindBySerial(g_World, tmpSerial) != item)
			item = NULL;
	} else {
		uint32_t tmpSerial = item->serial;
		((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, contEntity, &dropLoc);

		if (CWorld_FindBySerial(g_World, tmpSerial) != item)
			return;

		Entity_ExecuteEvent(&item->resourceEntity.entity, 0x1B, ((CItem *)this)->serial);

		if (!ValidateSerials((CItem *)this, playerSerial, contEntity, contSerial_saved, item, itemSerial_saved))
			return;
	}

	goto success;

non_container:
	if (contEntity == NULL)
		goto drop_fail;

	if (!((int (*)(void *, CItem *))VT_FN(item, VT_MERGE_INTO))(item, contEntity))
		goto drop_fail;

	PlayDropSound(item, contEntity, (CItem *)this);

	if (contEntity->parent != NULL) {
		CItem *ancestor = contEntity->parent;
		int failReason3 = 0;

		while (ancestor->parent != NULL) {
			if (VT_IsMobile(ancestor->parent))
				break;
			ancestor = ancestor->parent;
		}

		if (!((int (*)(void *, CItem *, int *))VT_FN(ancestor, VT_EQUIP_ITERATE))(ancestor, item, &failReason3)) {
			DropObj_Bounce((CItem *)this, item, &dropLoc);
			CPlayer_SystemMessage(this, GetCanHoldFailReason(failReason3));
			return;
		}
	}

	{
		uint32_t tmpSerial = item->serial;

		eventResult = (int)(intptr_t)Entity_ExecuteEvent(&item->resourceEntity.entity, 0x1B, ((CItem *)this)->serial);

		if (eventResult != 0)
			CItem_MergeIntoWrapper(item, contEntity);

		if (CWorld_FindBySerial(g_World, tmpSerial) != item)
			item = NULL;
	}

	goto success;

drop_fail:
	CLocation_CopyFrom(&dropLoc, &this->mobile.container.item.resourceEntity.entity.location);

	{
		uint8_t failPkt[8];
		PacketManager_MakePacket_DROPOBJ_FAILED(failPkt, dropLoc.x, dropLoc.y);
		SendToClient((CItem *)this, failPkt, -1);
	}

	((void (*)(void *))VT_FN(item, VT_HIDE))(item);
	((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, &dropLoc);
	((void (*)(void *))VT_FN(item, VT_REATTACH_SPATIAL))(item);
	Entity_ExecuteEvent(&item->resourceEntity.entity, 0x1B, ((CItem *)this)->serial);

	goto success;

success: {
	uint8_t okPkt[4];
	PacketManager_MakePacket_DROPOBJ_OK(okPkt);
	SendToClient((CItem *)this, okPkt, -1);
}

	if (CWorld_FindBySerial(g_World, serialItem) != item)
		item = NULL;

	if (item != NULL) {
		CItem *topMobile = CItem_FindTopContainerMobile(item);
		if (topMobile != NULL && topMobile == (CItem *)this)
			CPlayer_CheckWeight(this);
	}
	return;

cleanup:
	if (item == NULL)
		return;

	if (!(item->resourceEntity.entity.removedFromWorld & 0xFF))
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	{
		uint32_t tmpSerial = item->serial;

		if (!((int (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item)) {
			CLocation *playerLoc = (CLocation *)((void *(*)(void *))VT_FN((CItem *)this, VT_GET_LOCATION))((CItem *)this);
			((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, playerLoc);
		}

		item = CWorld_FindBySerial(g_World, tmpSerial);
		if (item == NULL)
			return;
		((void (*)(void *))VT_FN(item, VT_REATTACH_SPATIAL))(item);
	}
}

/*
 * 0x00495C93 - HandlePacket_EDIT
 *
 * GM editor tool: modifies tile properties. Reads 6 fields and forwards them
 * to the editor singleton.
 *
 * Packet fields: type(Byte), x(Word), y(Word), id(Word), z(Byte), hue(Word)
 */
void
HandlePacket_EDIT(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t type;
	uint16_t x, y, id, hue;
	uint8_t z;

	off = 0;
	GetByte(buf, &off, &type);
	GetWord(buf, &off, &x);
	GetWord(buf, &off, &y);
	GetWord(buf, &off, &id);
	GetByte(buf, &off, &z);
	GetWord(buf, &off, &hue);

	CEditorObj_HandleEdit(this, type, (int16_t)x, (int16_t)y, (int8_t)z, id, hue);
}

/*
 * 0x00495D46 - HandlePacket_ATTACK
 *
 * Starts combat against the targeted mobile (range 18), provided the
 * attacker is alive and the target is a mobile.
 */
void
HandlePacket_ATTACK(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t targetSerial;
	CItem *target;

	off = 0;
	GetDWord(buf, &off, &targetSerial);

	// Dead players can't attack
	if (VT_IsDead((CItem *)this))
		return;

	target = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, targetSerial, 18);
	if (target == NULL)
		return;

	// Target must be a mobile
	if (!VT_IsMobile(target))
		return;

	CombatInitiate(&this->mobile, (CMobile *)target, 1);
}

/*
 * 0x00495DBE - HandlePacket_GODCOMMAND
 *
 * Client macro action handler. Reads subcommand(Byte) and text(String up
 * to 241 bytes). Subtype dispatch (subcommand - 1):
 *   0x24=UseSkillByMacro, 0x26=CastSpellByName, 0x27=OpenSpellbookToSpell,
 *   0x43=OpenDoor, 0x56=UseSkillByID, 0x57=LastSkill,
 *   0x58=CastSpellByID, 0x59=LastSpell(stub), 0x5C=SetCombatBytes,
 *   0xC7=ArmDisarm; 0x07/0x08/0x09=NOP stubs
 */
void
HandlePacket_GODCOMMAND(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t subcommand;
	char *text;

	off = 0;
	GetByte(buf, &off, &subcommand);
	GetString(buf, &off, &text, 0xF1);

	switch (subcommand) {
	case 0x07: // Action (stub - immediate ret in binary)
	case 0x08: // (stub)
	case 0x09: // (stub)
		break;
	case 0x24: {
		// 0x00448372 - UseSkillByMacro
		int skillId, skillArg;
		const char *handler;
		const char *attachResult;

		if (VT_IsDead(&this->mobile.container.item)) {
			CPlayer_SystemMessage(this, "You can not use skills while dead.");
			break;
		}
		if (CPlayer_HasDeadFlag(this)) {
			CPlayer_SystemMessage(this, "You can not use skills.");
			break;
		}
		sscanf(text, "%d %d", &skillId, &skillArg);
		if (!CSkillManager_HasSkill(&g_SkillManager, skillId))
			break;
		if (!CSkillManager_CanUseDirect(&g_SkillManager, (uint8_t)skillId)) {
			CPlayer_SystemMessage(this, "That skill cannot be used directly.");
			break;
		}
		if (!Entity_ExecuteEvent(&this->mobile.container.item.resourceEntity.entity, 0x16, (uintptr_t)this->mobile.container.item.serial, "canUseSkill", "v")) {
			CPlayer_BusyMessage(this, 0);
			break;
		}

		if (skillId == 46 && !feat(FEAT_SKILL_MEDITATION)) {
			CPlayer_SystemMessage(this, "You cannot use this skill right now.");
			break;
		}
		if (skillId == 47 && !feat(FEAT_SKILL_STEALTH)) {
			CPlayer_SystemMessage(this, "You cannot use this skill right now.");
			break;
		}
		if (skillId == 48 && !feat(FEAT_SKILL_REMOVE_TRAP)) {
			CPlayer_SystemMessage(this, "You cannot use this skill right now.");
			break;
		}

		if (!(feat(FEAT_SKILL_STEALTH) && skillId == 47)) {
			if (((int (*)(void *))VT_FN(&this->mobile.container.item, VT_IS_HIDDEN))(&this->mobile.container.item)) {
				((void (*)(void *, int))VT_FN(&this->mobile.container.item, VT_SET_HIDDEN))(&this->mobile.container.item, 0);
			}
		}
		handler = CSkillManager_GetSkillHandler(&g_SkillManager, (int8_t)skillId);
		attachResult = Entity_AttachScript(&this->mobile.container.item, handler, 1);
		if (attachResult == NULL)
			attachResult = "Script attached";
		Entity_ExecuteEvent(&this->mobile.container.item.resourceEntity.entity, 0x16, (uintptr_t)this->mobile.container.item.serial, "useSkill", "v");
		USED(attachResult);
		USED(skillArg);
		break;
	}
	case 0x26: {
		// 0x0044822B - CastSpellByName
		char incantation[480];
		uint8_t obuf[0x42C];
		int outIdx = 0;
		int needHyphen = 0;

		while (*text != '\0') {
			if (*text == ' ') {
				incantation[outIdx++] = ' ';
				needHyphen = 0;
			} else {
				if (needHyphen)
					incantation[outIdx++] = '-';
				if (*text >= 'a' && *text <= 'z')
					incantation[outIdx++] = *text - 0x20;
				else
					incantation[outIdx++] = *text;
				needHyphen = 1;
			}
			text++;
		}
		incantation[outIdx] = '\0';
		PacketManager_MakePacket_TEXT(obuf, NULL, (CItem *)&this->mobile.container.item, 6, incantation, 0x3B2, 0);
		SendToClient((CItem *)&this->mobile.container.item, obuf, -1);
		break;
	}
	case 0x27: {
		OpenSpellbookToSpell(this, text);
		break;
	}
	case 0x43: {
		// 0x00447F24: OpenDoor_Equipment
		int i;
		CItem *item;

		for (i = 0; i < 0x1A; i++) {
			item = this->mobile.equipment[i];
			if (item == NULL)
				goto opendoor_nextslot;
			if (((int (*)(void *))VT_FN(item, VT_EXCLUDED_AMOUNT))(item)) {
				DoDoubleClick(this, this->mobile.container.item.serial, CEntity_GetLocation((CEntity *)&this->mobile.container.item), item, 0);
				goto opendoor_done;
			}
			if (!VT_IsMobile2(item))
				goto opendoor_nextslot;
			item = ((CContainer *)item)->contents;
			while (item != NULL) {
				if (((int (*)(void *))VT_FN(item, VT_EXCLUDED_AMOUNT))(item)) {
					DoDoubleClick(this, this->mobile.container.item.serial, CEntity_GetLocation((CEntity *)&this->mobile.container.item), item, 0);
					goto opendoor_done;
				}
				item = item->spatialNext;
			}
opendoor_nextslot:;
		}
opendoor_done:
		break;
	}
	case 0x56: {
		// CastSpellByID (0x00448047): spell-by-ID handler.
		int spellId, i;
		CMobile *mob = &this->mobile;
		CItem *found;

		sscanf(text, "%d", &spellId);
		// Step 1: search equipped items (0x00448065-0x004480F0)
		for (i = 0; i < 26; i++) {
			CItem *equip = mob->equipment[i];
			if (equip == NULL)
				continue;
			if (((int (*)(void *))VT_FN(equip, VT_EXCLUDED_AMOUNT))(equip) == 0)
				continue;
			found = FindSpellInContainer(equip, spellId);
			if (found != NULL) {
				Entity_ExecuteEvent(&found->resourceEntity.entity, UseItem, (uintptr_t)mob->container.item.serial);
				goto castspell_done;
			}
		}
		// Step 2: search backpack contents (0x004480F5-0x00448185)
		if (mob->equipment[21] != NULL) {
			CItem *bp = mob->equipment[21];
			if (VT_IsMobile2(bp)) {
				CItem *bpchild = ((CContainer *)bp)->contents;
				while (bpchild != NULL) {
					if (((int (*)(void *))VT_FN(bpchild, VT_EXCLUDED_AMOUNT))(bpchild) != 0) {
						found = FindSpellInContainer(bpchild, spellId);
						if (found != NULL) {
							Entity_ExecuteEvent(&found->resourceEntity.entity, UseItem, (uintptr_t)mob->container.item.serial);
							goto castspell_done;
						}
					}
					bpchild = bpchild->spatialNext;
				}
			}
		}
		CPlayer_SystemMessage(this, "You do not have that spell!");
castspell_done:
		break;
	}
	case 0x57: {
		// 0x00447DE9 - Direction flag toggle.
		int dirVal, dirVal2;

		dirVal = atoi(text);
		dirVal2 = atoi(text);
		if (CPlayer_GetDirectionFlag(this, (uint8_t)dirVal) != dirVal2)
			CPlayer_ToggleDirectionFlag(this, (uint8_t)dirVal);
		break;
	}
	case 0x58: {
		// 0x00447C8B - OpenDoor via spatial search.
		static const int32_t dirOffX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		static const int32_t dirOffY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
		CLocation targetLoc;
		int dir;
		int blockIdx;
		CItem *cur;

		CLocation_Init(&targetLoc);
		dir = ((int (*)(void *))VT_FN(&this->mobile.container.item, VT_GET_DIRECTION))(&this->mobile.container.item) & 7;
		targetLoc.x = CEntity_GetLocation((CEntity *)&this->mobile.container.item)->x + dirOffX[dir];
		targetLoc.y = CEntity_GetLocation((CEntity *)&this->mobile.container.item)->y + dirOffY[dir];
		targetLoc.z = CEntity_GetLocation((CEntity *)&this->mobile.container.item)->z;

		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)targetLoc.x, (int)(int16_t)targetLoc.y))
			break;

		blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &targetLoc, 0);
		if (blockIdx < 0)
			break;

		cur = g_SpatialGrid.cells[blockIdx].itemHead;
		while (cur != NULL) {
			if (((int (*)(void *))VT_FN(cur, VT_GET_FLAGS))(cur) & 0x20000000) {
				if ((int16_t)CEntity_GetLocation((CEntity *)cur)->x == (int16_t)targetLoc.x &&
				        (int16_t)CEntity_GetLocation((CEntity *)cur)->y == (int16_t)targetLoc.y &&
				        (int16_t)CEntity_GetLocation((CEntity *)cur)->z >= (int16_t)targetLoc.z - 8 &&
				        (int16_t)CEntity_GetLocation((CEntity *)cur)->z <= (int16_t)targetLoc.z + 8) {
					CPlayer_SystemMessage(this, "Opening door...");
					DispatchDoubleClick(this, cur, 0);
				}
			}
			cur = cur->spatialNext;
		}
		break;
	}
	case 0x59:
		LastSpell(this);
		break;
	case 0x5C: {
		// OpenClose / SetCombatBytes (0x00447C40): parses "%d %d %d",
		// sets combatBytes at offsets 0x33D, 0x33E, 0x33F
		int combatByte2, combatByte3, combatByte4;
		sscanf(text, "%d %d %d", &combatByte2, &combatByte3, &combatByte4);
		this->mobile.combatByte2 = (uint8_t)combatByte2;
		this->mobile.combatByte3 = (uint8_t)combatByte3;
		this->mobile.combatByte4 = (uint8_t)combatByte4;
		break;
	}
	case 0xC7: {
		// 0x00447E2E - BowSalute
		uint8_t animBuf[16];

		// Body type check: 0x190, 0x191, or 0x3DB
		if ((CEntity_GetBodyType(&this->mobile.container.item) & 0xFFFF) != 0x190 && (CEntity_GetBodyType(&this->mobile.container.item) & 0xFFFF) != 0x191 &&
		        (CEntity_GetBodyType(&this->mobile.container.item) & 0xFFFF) != 0x3DB)
			break;

		// Check "bow"
		if (strcmp(text, "bow") == 0 && this->mobile.equipment[25] == NULL) {
			PacketManager_MakePacket_ANIM(animBuf, this->mobile.container.item.serial, 0x20, 5, 1, 0, 0, 1);
			BroadcastToNearby(animBuf, &this->mobile.container.item.resourceEntity.entity.location, 0x14);
		} else if (strcmp(text, "salute") == 0 && this->mobile.equipment[25] == NULL) {
			PacketManager_MakePacket_ANIM(animBuf, this->mobile.container.item.serial, 0x21, 5, 1, 0, 0, 1);
			BroadcastToNearby(animBuf, &this->mobile.container.item.resourceEntity.entity.location, 0x14);
		}
		break;
	}
	default:
		// All other subtypes are NOP in the binary (return 1)
		break;
	}
}

/*
 * 0x00495E18 - HandlePacket_REQ_LOOK
 *
 * Packet wrapper: reads serial from packet, calls Handle_LookAt.
 */
void
HandlePacket_REQ_LOOK(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t targetSerial;

	off = 0;
	GetDWord(buf, &off, &targetSerial);
	Handle_LookAt(this, targetSerial);
}

/*
 * 0x00495E4D - HandlePacket_REQ_OBJEQUIP
 *
 * Equips the cursor item onto the target mobile at the given layer;
 * on failure returns the item to its tracked slot or drops at feet.
 */
void
HandlePacket_REQ_OBJEQUIP(CPlayer *this, uint8_t *buf)
{
	uint8_t obuf[0x20];
	uint32_t off;
	uint32_t itemSerial;
	uint8_t layer;
	uint32_t mobileSerial;
	CItem *found;
	CItem *targetMob;
	CItem *item;
	int fail;
	uint32_t savedSerial;

	off = 0;
	GetDWord(buf, &off, &itemSerial);
	GetByte(buf, &off, &layer);
	GetDWord(buf, &off, &mobileSerial);

	found = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, mobileSerial, 18);

	if (found != NULL && VT_IsMobile(found))
		targetMob = found;
	else
		targetMob = NULL;

	item = NULL;
	if (targetMob != NULL)
		item = this->mobile.equipment[0];

	if (item != NULL && item->serial != itemSerial)
		item = NULL;

	if (item == NULL)
		return;

	fail = 0;
	if (targetMob != (CItem *)&this->mobile && !CPlayer_IsEditing(this))
		fail = 1;

	// Can't equip resource items
	if (((int (*)(void *))VT_FN(item, VT_HAS_RESOURCE_FLAG))(item))
		fail = 1;

	if (fail) {
		if (!item->resourceEntity.entity.removedFromWorld)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		if (!((int (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item)) {
			CLocation *loc = ((CLocation * (*)(void *)) VT_FN(&this->mobile.container.item, VT_GET_LOCATION))(this);
			((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
		}
		return;
	}

	PacketManager_MakePacket_DROPOBJ_OK(obuf);
	SendToClient((CItem *)this, obuf, -1);

	((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	if ((layer & 0xFF) >= 0x1A)
		goto bounce;

	savedSerial = item->serial;

	if (((int (*)(void *, void *, int))VT_FN(item, VT_EQUIP_ON_MOBILE))(item, targetMob, (uint8_t)layer) == 1) {
		Entity_ExecuteEvent(&item->resourceEntity.entity, WasDropped, (uintptr_t)this->mobile.container.item.serial);
		SendEquipSound((CItem *)this);
		return;
	}

	if (CWorld_FindBySerial(g_World, savedSerial) != item)
		return;

	if (!item->resourceEntity.entity.removedFromWorld)
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

bounce:
	if (!((int (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item)) {
		CLocation *loc = ((CLocation * (*)(void *)) VT_FN(&this->mobile.container.item, VT_GET_LOCATION))(this);
		((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
	}
}

/*
 * 0x0049606F - HandlePacket_RESOURCETILEDATA
 *
 * GM editor tool: modifies resource tile node data on template slots
 * or entity resource node lists.
 *
 * Reads tileID(Word), mode(Word). If mode > 1, reads serial(DWord).
 * Reads count(Word). If mode != 0, allocates a CResourceNode from pool
 * and reads 7 fields into it (value1, value2, value3, type, two unused
 * DWords, and id).
 *
 * Mode dispatch:
 *   1: insert node into g_ResEntitySlots[tileID & 0x7FFF] (0x0045B3C4)
 *   2: find entity by serial, insert node into entity (0x00485B6F)
 *   3: find entity by serial, walk node list to Nth, remove it (0x00485C53)
 *   0: walk g_ResEntitySlots[tileID].nodeHead to Nth node, remove it
 *      from template slot (0x0045B42C)
 */
void
HandlePacket_RESOURCETILEDATA(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	CResourceNode *node;
	uint16_t tileID, mode, count;
	uint32_t serial;
	uint16_t tmpWord;
	uint32_t tmpDWord;

	off = 0;
	node = NULL;
	GetWord(buf, &off, &tileID);
	GetWord(buf, &off, &mode);

	serial = 0;
	if ((mode & 0xFFFF) > 1)
		GetDWord(buf, &off, &serial);

	GetWord(buf, &off, &count);

	if ((mode & 0xFFFF) != 0) {
		node = ResourceNode_AllocFromPool();

		GetWord(buf, &off, &tmpWord);
		node->value1 = tmpWord & 0xFFFF;

		GetWord(buf, &off, &tmpWord);
		node->value2 = tmpWord & 0xFFFF;

		GetWord(buf, &off, &tmpWord);
		node->value3 = tmpWord & 0xFFFF;

		GetWord(buf, &off, &tmpWord);
		node->type = (uint8_t)tmpWord;

		// Two DWords read but not stored to node
		GetDWord(buf, &off, &tmpDWord);
		GetDWord(buf, &off, &tmpDWord);

		GetWord(buf, &off, &tmpWord);
		node->id = tmpWord;
	}

	if ((mode & 0xFFFF) == 1) {
		CWorld_InsertResourceTileNode(tileID, node);
	} else if ((mode & 0xFFFF) > 1) {
		CItem *entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 0x12);
		if (entity != NULL) {
			if ((mode & 0xFFFF) == 2) {
				CResourceEntity_InsertNode(entity, node);
			}
			if ((mode & 0xFFFF) == 3) {
				int idx = 0;
				CResourceNode *cur = (CResourceNode *)entity->resourceEntity.firstChild;
				while (cur != NULL) {
					if (idx >= (count & 0xFFFF))
						break;
					idx++;
					cur = cur->next;
				}
				if (cur != NULL) {
					CResourceEntity_RemoveNode(entity, cur);
					ResourceNode_ReturnToPool(cur);
				}
			}
		}
	} else {
		int idx = 0;
		CResourceNode *cur;
		ResEntitySlot *slot;

		slot = &g_ResEntitySlots[tileID & 0xFFFF];
		cur = slot->nodeHead;
		while (cur != NULL) {
			if (idx >= (count & 0xFFFF))
				break;
			idx++;
			cur = cur->next;
		}
		if (cur != NULL) {
			CResourceEntity_RemoveStatic(tileID, cur);
		}
	}
}

/*
 * 0x0049630C - HandlePacket_TEMPLATEDATA
 *
 * GM editor tool: modifies template definitions. With dataLen != 0 it reads
 * name/data/flags and either calls the add stub (when g_TemplatesEnabled)
 * or sends a read-only error. With dataLen == 0 it calls the delete stub.
 * Both stubs are no-ops on the binary.
 *
 * Packet fields: templateId(Word), dataLen(Word),
 *   [if dataLen>0: name(String30), data(String128), flags(Word)]
 */
void
HandlePacket_TEMPLATEDATA(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint16_t templateId;
	uint16_t dataLen;

	off = 0;
	GetWord(buf, &off, &templateId);
	GetWord(buf, &off, &dataLen);

	if ((dataLen & 0xFFFF) != 0) {
		char *name;
		char *data;
		uint16_t flags;

		GetString(buf, &off, &name, 0x1E);
		GetString(buf, &off, &data, 0x80);
		GetWord(buf, &off, &flags);

		if (g_TemplatesEnabled) {
			CResBankManager_UpdateTemplateData(&g_ResBankManager, templateId, name, data, flags);
		} else {
			// Send "Access Denied: Templates is read-only."
			SendReadOnlyError(this, "Templates");
		}
	} else {
		CResBankManager_DeleteTemplateData(&g_ResBankManager, templateId);
	}
}

/*
 * 0x004963DD - SendStatusToPlayer
 *
 * Builds a MOBILESTAT (0x11) packet for mob and sends it to player.
 * Binary checks g_World->isLoading (offset 0x08) and vtable[0xD0]
 * (IsMobile) before building and sending. The serial arg is passed
 * to MakePacket_MOBILESTAT for the NPC rename/owner check.
 */
void
SendStatusToPlayer(CMobile *mob, CPlayer *player, uint32_t serial, uint8_t flag)
{
	uint8_t buf[0x44];

	if (g_World->isLoading)
		return;
	if (!VT_IsMobile(&mob->container.item))
		return;
	PacketManager_MakePacket_MOBILESTAT(buf, mob, serial, flag);
	Entity_BroadcastPacket((CItem *)player, serial, buf);
}

/*
 * 0x00496430 - SendEntityResourceNodes
 *
 * Iterates resource nodes (CResourceEntity.firstChild chain) on entity
 * and sends one RESOURCETILEDATA (0x36) packet per node to the player.
 * First loop counts nodes, second loop builds and sends packets.
 * Binary calls Entity_BroadcastPacket(player, unused, buf) which wraps
 * SendToClient(player, buf, -1) with a null check.
 */
static void
SendEntityResourceNodes(CItem *entity, CItem *player, uint32_t unused, uint32_t serial)
{
	int count = 0;
	int count2 = count;
	CResourceNode *cursor;
	uint8_t buf[0x20];

	// First loop: count resource nodes
	cursor = entity->resourceEntity.firstChild;
	while (cursor != NULL) {
		count++;
		cursor = cursor->next;
	}

	// Second loop: iterate and send each node
	cursor = entity->resourceEntity.firstChild;
	while (cursor != NULL) {
		count2++;
		PacketManager_MakePacket_RESOURCETILEDATA(buf, serial, count2 == count, (uint16_t)count2, cursor);
		Entity_BroadcastPacket(player, unused, buf);
		cursor = cursor->next;
	}
}

/*
 * 0x004964CA - HandlePacket_CLIENTQUERY
 *
 * Client extended query: dispatches by subtype to send region music
 * (0x02), vendor data (0x03), target status (0x04), skill list (0x05),
 * single skill (0x06), or resource nodes (0xFD). Requires the sentinel
 * serial 0xEDEDEDED.
 */
void
HandlePacket_CLIENTQUERY(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint8_t subtype;
	uint32_t param;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetByte(buf, &off, &subtype);
	GetDWord(buf, &off, &param);

	// Must have magic serial 0xEDEDEDED
	if (serial != 0xEDEDEDED)
		return;

	switch (subtype) {
	case 0x02: {
		// Region music query (0x00496524).
		// Demo regions have empty music lists so no packets are actually sent.
		if (!CPlayer_IsLoaded(this))
			break;
		if (!CWorld_LookupItemResource((uint16_t)param))
			break;
		CResourceEntity_SendMusicToPlayer((CItem *)&g_ResEntitySlots[param], this, (uint16_t)param);
		break;
	}
	case 0x03:
		// Resource type query (0x004965A6) - GM editing tool.
		if (!CPlayer_IsLoaded(this))
			break;
		CResourceTypeManager_SendAll((CItem *)this);
		break;
	case 0x04: {
		// Status request (0x004965C5) - send MOBILESTAT (0x11) for target entity.
		CMobile *mob;
		CItem *entity;
		uint8_t extended;

		extended = CPlayer_IsEditing(this) ? 1 : 0;

		if (param != this->mobile.container.item.serial)
			CPlayer_SetLastTarget(this, param);

		entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, param, 18);
		if (entity == NULL)
			break;

		mob = (CMobile *)entity;

		// Extended stats if editing OR viewing own character
		if (!extended && entity->serial == this->mobile.container.item.serial)
			extended = 1;

		SendStatusToPlayer(mob, this, this->mobile.container.item.serial, extended);
		break;
	}
	case 0x05: {
		// Full skill list (0x004966EA)
		// MODIFIED: Extended format for 1.26.2+ clients.
		uint8_t obuf[0x820];
		int numSkills;

		numSkills = CSkillManager_GetMaxSkills(&g_SkillManager);
		if (Version_GetConnVer(this->usersock, CLIENT_12602) >= CLIENT_12602) {
			PacketManager_MakePacket_SKILLS_Ext(obuf, numSkills, (CItem *)this);
		} else {
			PacketManager_MakePacket_SKILLS(obuf, numSkills, (CItem *)this);
		}
		SendToClient((CItem *)this, obuf, -1);
		break;
	}
	case 0x06: {
		// Single skill update (0x0049669F)
		// MODIFIED: Extended format for 1.26.2+ clients.
		uint8_t obuf[0x41C];
		int ver06 = Version_GetConnVer(this->usersock, CLIENT_12602);

		if (param >= (uint32_t)CSkillManager_GetMaxSkills(&g_SkillManager))
			break;
		{
			uint16_t skillValue = (uint16_t)CMobile_GetSkillValue((CMobile *)this, (int8_t)param, 0);
			if (ver06 >= CLIENT_12602) {
				uint16_t baseSkill = (uint16_t)CMobile_GetTotalSkill((CMobile *)this, (int8_t)param);
				PacketManager_MakePacket_SKILLS_SINGLE_Ext(obuf, (uint16_t)param, skillValue, baseSkill, feat(FEAT_SKILL_LOCK) ? this->skillLocks[param] : 0);
			} else {
				PacketManager_MakePacket_SKILLS_SINGLE(obuf, (uint16_t)param, skillValue);
			}
		}
		SendToClient((CItem *)this, obuf, -1);
		break;
	}
	case 0xFD: {
		// Resource node query (0x00496657)
		CItem *entity;
		if (!CPlayer_IsLoaded(this))
			break;
		entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, param, 18);
		if (entity == NULL)
			break;
		SendEntityResourceNodes(entity, (CItem *)this, CMobile_GetSerial((CMobile *)this), param);
		break;
	}
	default:
		break;
	}
}

/*
 * 0x35 @ 0x00496839 - AddResource
 *
 * GM editor tool: creates or updates a resource definition. Reads a
 * serial/ID; if 99999 (0x0001869F), allocates a new resource via the
 * resource manager. Otherwise looks up the existing entry. Then reads 5
 * 128-byte string fields and 2 DWord flags and applies each via the
 * resource object's setters. New resources are appended to the global list.
 *
 * Packet fields: serial(DWord), internalName(String128), foodName(String128),
 *   shelterName(String128), desireName(String128), productionName(String128),
 *   flag288(DWord), flag28C(DWord)
 */
void
HandlePacket_AddResource(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	char *internalName, *foodName, *shelterName, *desireName, *productionName;
	uint32_t flag288, flag28C;

	int isNew = 0;
	CResourceType *rt;

	off = 0;
	GetDWord(buf, &off, &serial);

	if (serial == 99999) {
		isNew = 1;
		rt = malloc(sizeof(CResourceType));
		if (rt != NULL)
			rt = CResourceType_Constructor(rt);
	} else {
		rt = CResourceTypeManager_GetId(serial);
	}

	if (rt == NULL)
		return;

	GetString(buf, &off, &internalName, 0x80);
	CResourceType_SetInternalName(rt, internalName);
	GetString(buf, &off, &foodName, 0x80);
	CResourceType_SetFoodName(rt, foodName);
	GetString(buf, &off, &shelterName, 0x80);
	CResourceType_SetShelterName(rt, shelterName);
	GetString(buf, &off, &desireName, 0x80);
	CResourceType_SetDesireName(rt, desireName);
	GetString(buf, &off, &productionName, 0x80);
	CResourceType_SetProductionName(rt, productionName);
	GetDWord(buf, &off, &flag288);
	CResourceType_SetField288(rt, flag288);
	GetDWord(buf, &off, &flag28C);
	CResourceType_SetField28C(rt, flag28C);

	if (isNew)
		CResourceTypeManager_RegisterType(rt);

	USED(this);
}

/*
 * 0x00496A03 - HandlePacket_ELEVCHANGE
 *
 * GM editor tool: changes terrain elevation. Reads x(Word), y(Word),
 * delta(Byte, signed). Gets block index, reads current cell Z from
 * g_MapBlocks[idx].cells[(y&7)*8+(x&7)].z, validates bounds via
 * IsValidCoord, then calls SetTerrainTile(player, x, y, -666,
 * currentZ + (int8_t)delta).
 */
void
HandlePacket_ELEVCHANGE(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint16_t x, y;
	uint8_t delta;
	int blockIdx;
	int currentZ;

	off = 0;
	GetWord(buf, &off, &x);
	GetWord(buf, &off, &y);
	GetByte(buf, &off, &delta);

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)(x & 0xFFFF), (int)(y & 0xFFFF), 0);

	currentZ = (int)g_MapBlocks[blockIdx].cells[((y & 0xFFFF) & 7) * 8 + ((x & 0xFFFF) & 7)].z;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(x & 0xFFFF), (int)(y & 0xFFFF)))
		return;

	SetTerrainTile((intptr_t)this, (int)(x & 0xFFFF), (int)(y & 0xFFFF), -666, currentZ + (int)(int8_t)delta);
}

/*
 * 0x00496AF7 - HandlePacket_MOVEOBJECT
 *
 * GM/editor tool (packet 0x37) that moves the targeted entity by
 * signed-byte deltas (dz, dy, dx).
 */
void
HandlePacket_MOVEOBJECT(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint8_t rawDz, rawDy, rawDx;
	CItem *entity;
	CLocation tempLoc;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetByte(buf, &off, &rawDz);
	GetByte(buf, &off, &rawDy);
	GetByte(buf, &off, &rawDx);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 18);
	if (entity == NULL)
		return;

	CLocation_Init(&tempLoc);
	CLocation_CopyFrom(&tempLoc, &entity->resourceEntity.entity.location);

	tempLoc.z += (int16_t)(int8_t)rawDz;
	tempLoc.y += (int16_t)(int8_t)rawDy;
	tempLoc.x += (int16_t)(int8_t)rawDx;

	((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);

	((void (*)(void *, void *))VT_FN(entity, VT_DROP_AT_FEET))(entity, &tempLoc);
}

/*
 * 0x00496BD6 - HandlePacket_GROUPS
 *
 * Stub. Reads 2 DWORDs from packet and discards them.
 */
void
HandlePacket_GROUPS(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t dword1, dword2;

	off = 0;
	GetDWord(buf, &off, &dword1);
	GetDWord(buf, &off, &dword2);
	USED(this);
}

/*
 * 0x00496C0F - HandlePacket_OFFERACCEPT
 *
 * Vendor Buy (packet 0x3B). Resolves the vendor mobile, parses the
 * accept flag and up to 250 (layer, serial, qty) buy entries off
 * the wire, and delegates the transaction to ProcessBuyList on the
 * vendor.
 */
void
HandlePacket_OFFERACCEPT(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t vendorSerial;
	uint8_t flag;
	CItem *vendor;
	int numItems;
	int i;
	// Binary: 0xC-byte padded struct array on stack (0xBCC bytes total).
	// GetByte writes layer at base + i*0xC + 0, GetDWord writes
	// serial at base + i*0xC + 4, GetWord writes qty at base + i*0xC + 8.
	BuyEntry entries[250];

	off = 0;
	GetDWord(buf, &off, &vendorSerial);
	GetByte(buf, &off, &flag);

	vendor = CWorld_FindEntityInRange(g_World, (CEntity *)this, vendorSerial, 0x12);
	if (vendor == NULL)
		return;

	if (!VT_IsVendor(vendor))
		return;

	if ((flag & 0xFF) != 2)
		return;

	numItems = ((int)(GetPacketOffset(buf) & 0xFFFF) - 8) / 7;

	for (i = 0; i < numItems; i++) {
		GetByte(buf, &off, &entries[i].layer);
		GetDWord(buf, &off, &entries[i].serial);
		GetWord(buf, &off, &entries[i].qty);
	}

	CMobile_ProcessBuyList((CMobile *)vendor, this, numItems, entries);
}

/*
 * 0x00496D55 - HandlePacket_SHOP_OFFER
 *
 * Parses the client's sell list (up to 252 serial+amount entries) and
 * forwards it to the targeted vendor via CMobile_ProcessSellOffer.
 */
void
HandlePacket_SHOP_OFFER(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t vendorSerial;
	uint16_t itemCount;
	int i;
	CItem *vendor;
	// Binary: 0xC-byte padded struct array on stack (0xBCC bytes total).
	// GetDWord writes serial at base + i*0xC + 4, GetWord writes
	// amount at base + i*0xC + 8. ProcessSellOffer reads same offsets.
	SellEntry entries[252];

	off = 0;
	GetDWord(buf, &off, &vendorSerial);
	GetWord(buf, &off, &itemCount);

	for (i = 0; i < (int)(itemCount & 0xFFFF); i++) {
		GetDWord(buf, &off, &entries[i].serial);
		GetWord(buf, &off, &entries[i].amount);
	}

	vendor = CWorld_FindEntityInRange(g_World, (CEntity *)this, vendorSerial, 0x12);
	if (vendor == NULL)
		return;

	if (!VT_IsVendor(vendor))
		return;

	CMobile_ProcessSellOffer((CMobile *)vendor, this, (int)(itemCount & 0xFFFF), entries);
}

/*
 * 0x00496E3B - HandlePacket_NPCCONVO_DATA
 *
 * Reads serial (DWord) and subtype (Byte), then discards all results.
 * The binary also computes buf+0x0A into an unused local.
 */
void
HandlePacket_NPCCONVO_DATA(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint8_t subtype;
	uint8_t *unused;

	USED(this);

	off = 0;
	GetDWord(buf, &off, &serial);
	GetByte(buf, &off, &subtype);
	unused = buf + 0x0A;
	USED(serial);
	USED(subtype);
	USED(unused);
}

/*
 * 0x00496E7D - HandlePacket_DESTROY_OBJECT
 *
 * Deletes the targeted entity via vtable[0x90] Delete, unless it is a
 * player.
 */
void
HandlePacket_DESTROY_OBJECT(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	CItem *entity;

	off = 0;
	GetDWord(buf, &off, &serial);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 18);
	if (entity == NULL)
		return;

	// Cannot delete player entities
	if (VT_IsPlayer(entity))
		return;

	// 0x00496ECA: redundant NULL check (binary artifact)
	if (entity == NULL)
		return;

	((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
}

/*
 * 0x49 @ 0x00496EE2 - NEW_HUES
 *
 * GM editor tool: uploads new hue palette data. Reads a hue group index,
 * 64 bytes of color data (32 uint16 palette entries), start/end color
 * indices, and a 20-byte hue name. The binary delegate sends a "Hues"
 * read-only error when the hue table has not been loaded.
 *
 * Packet fields: index(DWord), colors(String64), startColor(Word),
 *   endColor(Word), name(String20)
 */
void
HandlePacket_NEW_HUES(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t index;
	char *colors;
	uint16_t startColor, endColor;
	char *name;

	off = 0;
	GetDWord(buf, &off, &index);
	GetString(buf, &off, &colors, 0x40);
	GetWord(buf, &off, &startColor);
	GetWord(buf, &off, &endColor);
	GetString(buf, &off, &name, 0x14);

	StoreHues(this, index, (uint16_t *)colors, startColor, endColor, name);
}

/*
 * 0x00496F7D - HandlePacket_NEW_REGION (packet 0x58)
 *
 * GM editor tool: creates or modifies a region definition. Builds a stack
 * CRegion from 14 packet fields and forwards it to MapBlock_ReadEntry.
 *
 * Packet fields: name(String40), prefix(DWord), x(Word), y(Word),
 *   width(Word), height(Word), zMin(Word), zMax(Word), name2(String40),
 *   weatherDay(Word), weatherSeason(Word), weatherNight(Word),
 *   type(Byte), lightLevel(Word)
 *
 * The binary reads weatherDay before weatherSeason, even though weatherDay
 * sits at the higher struct offset.
 */
void
HandlePacket_NEW_REGION(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	CRegion region;
	char *namePtr;
	char *name2Ptr;

	CRegion_Init(&region);
	off = 0;
	GetString(buf, &off, &namePtr, 0x28);
	strncpy(region.name, namePtr, 0x28);
	GetDWord(buf, &off, (uint32_t *)&region.prefix);
	GetWord(buf, &off, (uint16_t *)&region.x);
	GetWord(buf, &off, (uint16_t *)&region.y);
	GetWord(buf, &off, (uint16_t *)&region.width);
	GetWord(buf, &off, (uint16_t *)&region.height);
	GetWord(buf, &off, (uint16_t *)&region.zMin);
	GetWord(buf, &off, (uint16_t *)&region.zMax);
	GetString(buf, &off, &name2Ptr, 0x28);
	strncpy(region.name2, name2Ptr, 0x28);
	GetWord(buf, &off, (uint16_t *)&region.weatherDay);
	GetWord(buf, &off, (uint16_t *)&region.weatherSeason);
	GetWord(buf, &off, (uint16_t *)&region.weatherNight);
	GetByte(buf, &off, &region.type);
	GetWord(buf, &off, (uint16_t *)&region.lightLevel);
	MapBlock_ReadEntry(this, &region);
	CRegion_Destructor(&region);
}

/*
 * 0x48 @ 0x00497161 - NEW_ANIM
 *
 * GM editor tool: uploads new animation data. Reads an animation
 * index (DWord) and 0x44 bytes of entry data, then delegates to
 * AnimData_UpdateEntry (0x004D6E92).
 *
 * Packet fields: animIndex(DWord), entryData(String68)
 */
void
HandlePacket_NEW_ANIM(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t animIndex;
	char *entryData;

	off = 0;
	GetDWord(buf, &off, &animIndex);
	GetString(buf, &off, &entryData, 0x44);

	AnimData_UpdateEntry(this, animIndex, (uint8_t *)entryData);
}

/*
 * 0x4A @ 0x004971B0 - DESTROY_ART
 *
 * GM editor tool: deletes an art tile entry. Reads artId, validates the
 * range [1, 0x00010000), then calls StoreArt with size=-1 and data=NULL
 * to signal deletion. Uses the same delegate as NEW_ART.
 *
 * Packet fields: artId(DWord)
 */
void
HandlePacket_DESTROY_ART(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t artId;

	off = 0;
	GetDWord(buf, &off, &artId);

	if (artId < 1 || artId >= 0x10000)
		return;

	ArtTile_WriteEntry(this, (int)artId, -1, NULL);
}

/*
 * 0x46 @ 0x004971FA - NEW_ART
 *
 * GM editor tool: uploads new art tile data. Reads artId and up to
 * 64KB of art data. Validates artId in [0, 0x00010000). Art size:
 * land tiles (artId < 0x4000) = fixed 0x800 bytes; item tiles
 * (artId >= 0x4000) = computed from data header (dword + height*2 + 8).
 * Delegates to StoreArt (0x004D6AF6) which has 0x00010024-byte stack,
 * checks g_ArtEnabled at 0x006982FC.
 *
 * Packet fields: artId(DWord), artData(String[0x00010000])
 */
void
HandlePacket_NEW_ART(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t artId;
	char *artData;

	off = 0;
	GetDWord(buf, &off, &artId);

	if (artId >= 0x10000)
		return;

	GetString(buf, &off, &artData, 0x10000);

	// 0x00497245: compute art size based on tile type
	int size;
	if ((int)artId < 0x4000) {
		size = 0x800;
	} else {
		// memmove first dword from art data header
		memmove(&size, artData, 4);
		// size = header_dword + height*2 + 8
		uint16_t height;
		memcpy(&height, artData + 6, 2);
		size = size + (int)height * 2 + 8;
	}

	ArtTile_WriteEntry(this, (int)artId, size, (uint8_t *)artData);
}

/*
 * 0x47 @ 0x00497298 - NEW_TERR
 *
 * GM editor tool: sets terrain tile types. Single-tile mode (width=0xFFFF)
 * delegates to SetTerrSingle. Region mode walks the (width, height)
 * rectangle calling SetTerrainTile per valid tile, bracketed with
 * World_SuppressUpdates / World_RestoreUpdates when the area is non-trivial.
 *
 * Packet fields: x(Word), y(Word), terrId(Word), width(Word), height(Word)
 */
void
HandlePacket_NEW_TERR(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint16_t x, y, terrId, width, height;
	int ix, iy;

	off = 0;
	GetWord(buf, &off, &x);
	GetWord(buf, &off, &y);
	GetWord(buf, &off, &terrId);
	GetWord(buf, &off, &width);
	GetWord(buf, &off, &height);

	terrId &= 0x7FFF;

	if ((width & 0xFFFF) == 0xFFFF) {
		SetTerrSingle((intptr_t)this, (int)(x & 0xFFFF), (int)(y & 0xFFFF), (int)(terrId & 0xFFFF), (int)0xFFFFFD66);
		return;
	}

	// 0x00497359
	if ((width & 0xFFFF) > 1 || (height & 0xFFFF) > 1)
		World_SuppressUpdates();

	for (iy = (int)(y & 0xFFFF); iy < (int)(y & 0xFFFF) + (int)(height & 0xFFFF); iy++) {
		for (ix = (int)(x & 0xFFFF); ix < (int)(x & 0xFFFF) + (int)(width & 0xFFFF); ix++) {
			if (CBlockManager_IsValidCoord(&g_SpatialGrid, ix, iy)) {
				SetTerrainTile((intptr_t)this, ix, iy, (int)(terrId & 0xFFFF), (int)0xFFFFFD66);
			}
		}
	}

	if ((width & 0xFFFF) > 1 || (height & 0xFFFF) > 1)
		World_RestoreUpdates();
}

/*
 * 0x0049743B - HandlePacket_MAP_COMMAND
 *
 * Map pin editing and lock management.
 * Reads: serial(DWord), command(Byte), arg(Byte), x(Word), y(Word).
 * FindEntityInRange(serial, 0x12), vtable[0xE0] (VT_IS_SPATIAL) check.
 *
 * Command 6: toggle CSignpost.lockOwner.
 *   Stale check: lockOwner->removedFromWorld (byte offset 6). Respond
 *   with cmd 7 (granted/released) or cmd 8 (denied) via SendToClient.
 *
 * Other commands: require lockOwner==player. Call PlotOnMap (0x0048AF39)
 *   for map pin operations, broadcast via player list function (0x0045004C).
 *
 */
void
HandlePacket_MAP_COMMAND(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint8_t command, arg;
	uint16_t plotX, plotY;
	CItem *entity;
	CSignpost *map;
	CPlayer *lockOwner;
	uint8_t obuf[16];

	off = 0;
	GetDWord(buf, &off, &serial);
	GetByte(buf, &off, &command);
	GetByte(buf, &off, &arg);
	GetWord(buf, &off, &plotX);
	GetWord(buf, &off, &plotY);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 0x12);
	if (entity == NULL)
		return;

	if (!VT_IsSpatial(entity))
		return;

	if ((command & 0xFF) == 6) {
		map = (CSignpost *)entity;
		lockOwner = map->lockOwner;

		// lockOwner offset 6 (CEntity.removedFromWorld)
		if (lockOwner != NULL && lockOwner->mobile.container.item.resourceEntity.entity.removedFromWorld) {
			map->lockOwner = NULL;
		}

		if (map->lockOwner == NULL) {
			map->lockOwner = this;
			PacketManager_MakePacket_MAP_COMMAND(obuf, serial, 7, 1, 0, 0);
			SendToClient((CItem *)this, obuf, -1);
		} else if (map->lockOwner == (CPlayer *)this) {
			map->lockOwner = NULL;
			PacketManager_MakePacket_MAP_COMMAND(obuf, serial, 7, 0, 0, 0);
			SendToClient((CItem *)this, obuf, -1);
		} else {
			PacketManager_MakePacket_MAP_COMMAND(obuf, serial, 8, 0, 0, 0);
			SendToClient((CItem *)this, obuf, -1);
		}
	} else {
		map = (CSignpost *)entity;
		if (map->lockOwner != (CPlayer *)this)
			return;

		PlotOnMap(map, command, arg, plotX, plotY);

		PacketManager_MakePacket_MAP_COMMAND(obuf, serial, command, arg, plotX, plotY);
		CPlayerList_BroadcastInRange(obuf, &this->mobile.container.item.resourceEntity.entity.location, 0x12, this);
	}
}

/*
 * 0x00497628 - HandlePacket_SIMPED
 *
 * GM editor tool: updates item appearance properties. Reads 6 fields
 * and delegates to SIMPED_Apply (0x0045B4C0).
 *
 * Packet fields: serial(DWord), flags(DWord), data(DWord),
 *   artId(Word), hue(Word), tileGroupIdx(DWord)
 */
void
HandlePacket_SIMPED(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial, flags, data, tileGroupIdx;
	uint16_t artId, hue;

	USED(this);

	off = 0;
	GetDWord(buf, &off, &serial);
	GetDWord(buf, &off, &flags);
	GetDWord(buf, &off, &data);
	GetWord(buf, &off, &artId);
	GetWord(buf, &off, &hue);
	GetDWord(buf, &off, &tileGroupIdx);

	SIMPED_Apply(serial, flags, data, artId, hue, tileGroupIdx);
}

/*
 * 0x004976D3 - HandlePacket_DEATH
 *
 * Client responds to the death dialog: deathAction 0 = stay ghost,
 * 1 = resurrect.
 *
 * MODIFIED: binary requires IsGhost (statusFlags bit 0x02) for all
 * paths. The auto-unfreeze timer may clear this bit before the client
 * sends 0x2C, so for deathAction==1 we require only IsDead.
 */
void
HandlePacket_DEATH(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t deathAction;

	off = 0;
	deathAction = 0;
	GetByte(buf, &off, &deathAction);

	// Must be dead
	if (!VT_IsDead(&this->mobile.container.item))
		return;

	if (deathAction == 0 && !CPlayer_IsGhost(this))
		return;

	// MODIFIED: allow deathAction==1 when ghost flag was already
	// cleared by auto-unfreeze timer.
	if (deathAction != 1 && !CPlayer_IsGhost(this))
		return;

	// No-op if ghost flag was already cleared by auto-unfreeze.
	CPlayer_ProcessDeath(this);

	if (deathAction == 1)
		CPlayer_InstantResurrect(this);
}

/*
 * 0x00497754 - HandlePacket_KEY_USE
 *
 * No-op stub.
 */
void
HandlePacket_KEY_USE(CPlayer *this, uint8_t *buf)
{
	USED(this);
	USED(buf);
}

/*
 * 0x00497759 - HandlePacket_FRIENDS
 *
 * No-op stub.
 */
void
HandlePacket_FRIENDS(CPlayer *this, uint8_t *buf)
{
	USED(this);
	USED(buf);
}

/*
 * 0x0049775E - HandlePacket_TARGET
 *
 * Target response handler. Reads: type(Byte), cursorID(DWord),
 * then ValidateEntityAccess on cursorID, reads flags(Byte), serial(DWord),
 * saves serial, then ValidateEntityAccess on serial, reads x(Word),
 * y(Word), z(Word), graphicID(Word).
 * Binary has redundant branching: both type==0 and type!=0 call callback
 * identically at player+0x3C0 if non-NULL.
 * Phase 2: FindBySerial(cursorID), checks cursorID > 0 (unsigned jbe).
 * Dispatches events 0x38, 0x18, 0x19 through scripting system.
 */
void
HandlePacket_TARGET(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t type;
	uint32_t cursorID;
	uint8_t flags;
	uint32_t serial;
	uint32_t savedSerial;
	uint16_t x, y, z;
	uint16_t graphicID;
	CItem *entity;
	CLocation loc;

	off = 0;
	GetByte(buf, &off, &type);
	GetDWord(buf, &off, &cursorID);

	ValidateEntityAccess(this, &cursorID, 0);

	GetByte(buf, &off, &flags);
	GetDWord(buf, &off, &serial);
	savedSerial = serial;

	ValidateEntityAccess(this, &serial, 0);

	GetWord(buf, &off, &x);
	GetWord(buf, &off, &y);
	GetWord(buf, &off, &z);
	GetWord(buf, &off, &graphicID);

	// Binary redundantly branches: both type==0 and type!=0 paths
	// call the same callback identically.
	if ((type & 0xFF) == 0) {
		if (this->targetCallback != NULL) {
			this->targetCallback(this, type, serial, x, y, z);
		}
	} else {
		if (this->targetCallback != NULL) {
			this->targetCallback(this, type, serial, x, y, z);
		}
	}

	entity = CWorld_FindBySerial(g_World, cursorID);
	if (cursorID == 0)
		return;
	if (entity == NULL)
		return;

	CLocation_Init(&loc);
	loc.x = x;
	loc.y = y;
	loc.z = z;

	if ((type & 0xFF) == 0) {
		if (Entity_ExecuteEvent(&entity->resourceEntity.entity, TargetObjectPre, (uintptr_t)this->mobile.container.item.serial, (uintptr_t)savedSerial) == NULL)
			return;
		entity = CWorld_FindBySerial(g_World, cursorID);
		if (Entity_ExecuteEvent(&entity->resourceEntity.entity, TargetObject, (uintptr_t)this->mobile.container.item.serial, (uintptr_t)serial) == NULL)
			return;
		entity = CWorld_FindBySerial(g_World, cursorID);
		if (entity == NULL)
			return;
	}

	Entity_ExecuteEvent(&entity->resourceEntity.entity, TargetLocation, (uintptr_t)this->mobile.container.item.serial, &loc, (int)(graphicID & 0xFFFF));
}

/*
 * 0x0049798B - HandlePacket_TRADE
 *
 * Secure trade handler. Reads subtype(Byte), tradeContainerSerial(DWord),
 * param1(DWord), param2(DWord), extraByte(Byte). Walks the global trade
 * session list looking for a match on either container's serial. Subtype
 * 2 (Accept) runs SetTradeAcceptState; subtype 1 (Cancel) runs CloseTrade
 * with deleteFlag=1.
 */
void
HandlePacket_TRADE(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t subtype;
	uint32_t tradeSerial;
	uint32_t param1, param2;
	uint8_t param3;
	CTradeSession *session;
	CTradeSession *sess2, *sess3;

	off = 0;
	GetByte(buf, &off, &subtype);
	GetDWord(buf, &off, &tradeSerial);
	GetDWord(buf, &off, &param1);
	GetDWord(buf, &off, &param2);
	GetByte(buf, &off, &param3);

	session = g_TradeSessionList;
	while (session != NULL) {
		if (session->container1->serial == tradeSerial)
			break;
		if (session->container2->serial == tradeSerial)
			break;
		session = session->next;
	}

	if (session == NULL)
		return;

	if (subtype == 1) {
		sess2 = session;
		sess3 = sess2;
		if (sess3 != NULL)
			CloseTrade(sess3, 1);
		session = NULL;
	} else if (subtype == 2) {
		if ((CPlayer *)this == session->player1) {
			SetTradeAcceptState(session, (param1 != 0) ? 1 : 0, session->accept2);
		} else if ((CPlayer *)this == session->player2) {
			SetTradeAcceptState(session, session->accept1, (param1 != 0) ? 1 : 0);
		}
	}
	USED(param2);
	USED(param3);
}

/*
 * 0x00497ACD - BBoard_EnableBroadcastMode
 *
 * Runs HandlePacket_BBOARD(NULL, buf) with g_BBoardBroadcastMode set
 * so the bulletin-board posting is broadcast rather than replied to.
 */
void
BBoard_EnableBroadcastMode(uint8_t *buf)
{
	g_BBoardBroadcastMode = 1;
	HandlePacket_BBOARD(NULL, buf);
	g_BBoardBroadcastMode = 0;
}

/*
 * 0x00497AF4 - BBoard_BroadcastWrapper
 *
 * Trampoline to BBoard_EnableBroadcastMode.
 */
static void
BBoard_BroadcastWrapper(uint8_t *buf)
{
	BBoard_EnableBroadcastMode(buf);
}

/*
 * 0x00497B05 - HandlePacket_BBOARD
 *
 * Handles bulletin board packet (0x71). Reads a sub-command byte
 * and dispatches:
 *   3 (case 0): request post body - calls CBulletinBoard::SendPostBody
 *   4 (case 1): request board summary - calls CBulletinBoard::SendBoardSummary
 *   5 (case 2): post new message - calls CBulletinBoard::PostMessage
 *   6 (case 3): remove post - calls CBulletinBoard::RemovePost
 *
 * FIXED: Binary uses strcpy for subject (0x00497CE3) and line content
 * (0x00497D90) without bounds checking. Subject buffer is 0x50 (80) bytes
 * but client-provided length byte allows up to 255 bytes, causing stack
 * overflow. Line content allocates exactly tempByte bytes then strcpy
 * copies without NUL guarantee, causing heap overflow. Fix: use strncpy
 * with proper bounds for subject, allocate tempByte+1 for lines.
 */
void
HandlePacket_BBOARD(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t subCmd;
	uint32_t boardSerial;
	uint32_t postSerial;
	uint32_t replySerial;
	CItem *board;
	CItem *post;
	int i;
	uint8_t tempByte;
	char *tempStr;
	char subject[80];
	uint8_t numLines;
	uintptr_t lines[256];
	CBulletinBoard *bb;

	off = 0;
	GetByte(buf, &off, &subCmd);

	switch (subCmd & 0xFF) {
	case 3: {
		// Request post body
		GetDWord(buf, &off, &boardSerial);
		GetDWord(buf, &off, &postSerial);

		board = CWorld_FindEntityInRange(g_World, (CEntity *)this, boardSerial, 0x12);
		post = CWorld_FindEntityInRange(g_World, (CEntity *)this, postSerial, 0x12);

		if (board != NULL && ((int (*)(void *))VT_FN(board, VT_CHECK_DC))(board) && post != NULL) {
			CBulletinBoard_SendPostBody((CBulletinBoard *)board, this, post);
		}
		break;
	}

	case 4: {
		// Request board summary
		GetDWord(buf, &off, &boardSerial);
		GetDWord(buf, &off, &postSerial);

		board = CWorld_FindEntityInRange(g_World, (CEntity *)this, boardSerial, 0x12);
		post = CWorld_FindEntityInRange(g_World, (CEntity *)this, postSerial, 0x12);

		if (board != NULL && ((int (*)(void *))VT_FN(board, VT_CHECK_DC))(board) && post != NULL) {
			CBulletinBoard_SendBoardSummary((CBulletinBoard *)board, this, post);
		}
		break;
	}

	case 5: {
		// Post new message
		GetDWord(buf, &off, &boardSerial);
		GetDWord(buf, &off, &replySerial);

		// Read subject
		// FIXED: binary uses strcpy (0x00497CE3) - overflow if tempByte > 0x4F
		GetByte(buf, &off, &tempByte);
		GetString(buf, &off, &tempStr, tempByte & 0xFF);
		strncpy(subject, tempStr, sizeof(subject) - 1);
		subject[sizeof(subject) - 1] = '\0';

		// Read lines
		// FIXED: binary allocates tempByte bytes (0x00497D6C) then strcpy
		// (0x00497D90) - heap overflow if string lacks NUL within allocation
		GetByte(buf, &off, &numLines);
		for (i = 0; i < (numLines & 0xFF); i++) {
			GetByte(buf, &off, &tempByte);
			GetString(buf, &off, &tempStr, tempByte & 0xFF);
			lines[i] = (uintptr_t)malloc((tempByte & 0xFF) + 1);
			strncpy((char *)lines[i], tempStr, tempByte & 0xFF);
			((char *)lines[i])[tempByte & 0xFF] = '\0';
		}

		if (g_BBoardBroadcastMode == 0 && boardSerial != 0) {
			board = CWorld_FindEntityInRange(g_World, (CEntity *)this, boardSerial, 0x12);
			post = CWorld_FindEntityInRange(g_World, (CEntity *)this, replySerial, 0x12);

			if (board != NULL && ((int (*)(void *))VT_FN(board, VT_CHECK_DC))(board)) {
				CBulletinBoard_PostMessage((CBulletinBoard *)board, this, post, subject, numLines, lines);
			}
		} else if (boardSerial == 0 && this != NULL && CPlayer_IsEditing(this)) {
			BBoard_BroadcastWrapper(buf);
		} else if (g_BBoardBroadcastMode != 0) {
			for (bb = g_BBoardHead; bb != NULL; bb = bb->bbNext) {
				CBulletinBoard_PostMessage(bb, NULL, NULL, subject, numLines, lines);
			}
		}

		for (i = 0; i < (numLines & 0xFF); i++) {
			free((void *)lines[i]);
		}
		break;
	}

	case 6: {
		// Remove post
		GetDWord(buf, &off, &boardSerial);
		GetDWord(buf, &off, &postSerial);

		board = CWorld_FindEntityInRange(g_World, (CEntity *)this, boardSerial, 0x12);
		post = CWorld_FindEntityInRange(g_World, (CEntity *)this, postSerial, 0x12);

		if (board == NULL || post == NULL)
			break;

		CBulletinBoard_RemovePost((CBulletinBoard *)board, post, (CMobile *)this);
		break;
	}

	default:
		break;
	}
}

/*
 * 0x00497F5F - HandlePacket_PING
 *
 * Reads the ping sequence byte and echoes it back via PingReply.
 */
void
HandlePacket_PING(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t sequence;

	off = 0;
	GetByte(buf, &off, &sequence);
	CPlayer_PingReply(this, sequence);
}

/*
 * 0x00497F90 - HandlePacket_COMBAT
 *
 * Packet 0x72 COMBAT handler. Reads warFlag + 3 bytes, checks
 * CPlayer_HasDeadFlag (0x004566E4), delegates to CPlayer_SetWarMode (0x00454A5B).
 */
void
HandlePacket_COMBAT(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t warFlag, byte2, byte3, byte4;

	off = 0;
	GetByte(buf, &off, &warFlag);
	GetByte(buf, &off, &byte2);
	GetByte(buf, &off, &byte3);
	GetByte(buf, &off, &byte4);

	if (CPlayer_HasDeadFlag(this))
		return;

	CPlayer_SetWarMode(this, warFlag, byte2, byte3, byte4);
}

/*
 * 0x00498015 - HandlePacket_OK_MOVE
 *
 * Recomputes the player's Z and sends a ZMOVE packet back to the
 * client.
 */
void
HandlePacket_OK_MOVE(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t dummy;
	uint8_t obuf[32];

	off = 0;
	GetByte(buf, &off, &dummy);
	GetByte(buf, &off, &dummy);

	CMobile_NotifyNearbyPlayers((CItem *)this);

	PacketManager_MakePacket_ZMOVE(obuf, (CMobile *)this);
	SendToClient((CItem *)this, obuf, -1);
}

/*
 * 0x00498078 - HandlePacket_RENAME_MOB
 *
 * Renames a pet mobile owned by the player and broadcasts a "Pet X
 * renamed to Y." message.
 *
 * FIXED: binary sprintf's into a 128-byte stack buffer without bounds
 * checking; we use snprintf.
 */
void
HandlePacket_RENAME_MOB(CPlayer *this, uint8_t *buf)
{
	uint8_t obuf[0x42C];
	uint32_t off;
	uint32_t serial;
	char *newName;
	CItem *entity;
	char msg[128];

	off = 0;
	GetDWord(buf, &off, &serial);
	GetString(buf, &off, &newName, 0x1E);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 18);
	if (entity == NULL)
		return;

	if (!VT_IsMobile(entity))
		return;

	{
		CMobile *mob = (CMobile *)entity;
		char *oldName;

		if (!CMobile_IsPet(mob))
			return;

		if (!CMobile_CheckOwner(mob, this->mobile.container.item.serial))
			return;

		oldName = ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity);

		// FIXED: binary uses sprintf (0x00498133) - overflow if oldName is long
		snprintf(msg, sizeof(msg), "Pet %s renamed to %s.", oldName, newName);

		CMobile_SetName(mob, newName);
	}

	PacketManager_MakePacket_TEXT(obuf, NULL, (CItem *)&this->mobile.container.item, 6, msg, 0x3B2, 3);

	SendToClient((CItem *)this, obuf, -1);
}

/*
 * 0x00498185 - HandlePacket_PICKEDOBJ
 *
 * Client response to targeting cursor (object picker). Reads
 * targetSerial(DWord), modelID(Word), x(Word), y(Word), z(Word).
 * Looks up entity within range 18, dispatches event 0x24 (PickedObj)
 * to scripting system.
 */
void
HandlePacket_PICKEDOBJ(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t targetSerial;
	uint16_t modelID;
	uint16_t x, y, z;
	CItem *entity;

	off = 0;
	GetDWord(buf, &off, &targetSerial);
	GetWord(buf, &off, &modelID);
	GetWord(buf, &off, &x);
	GetWord(buf, &off, &y);
	GetWord(buf, &off, &z);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, targetSerial, 18);
	if (entity == NULL)
		return;

	// Dispatch event 0x24 (PickedObj) to entity via scripting system
	Entity_ExecuteEvent(&entity->resourceEntity.entity, PickedObj, (uintptr_t)modelID, (uintptr_t)this->mobile.container.item.serial, (int)x, (int)y, (int)z);
}

/*
 * 0x00498255 - HandlePacket_HUEPICKER
 *
 * Hue picker response. Reads serial(DWord), itemID(Word), hueValue(Word).
 * Validates entity via FindEntityInRange(0x12). Fires Entity_ExecuteEvent
 * with (entity, 0x25, itemID & 0xFFFF, player->serial, hueValue & 0xFFFF).
 * Verified exact match with binary.
 */
void
HandlePacket_HUEPICKER(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint16_t itemID;
	uint16_t hueValue;
	CItem *entity;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetWord(buf, &off, &itemID);
	GetWord(buf, &off, &hueValue);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 18);
	if (entity == NULL)
		return;

	Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x25, (uintptr_t)itemID, (uintptr_t)this->mobile.container.item.serial, (int)hueValue);
}

/*
 * 0x004982EA - HandlePacket_MOBNAME
 *
 * Replies to the client with the name of the given mobile serial.
 */
void
HandlePacket_MOBNAME(CPlayer *this, uint8_t *buf)
{
	uint8_t obuf[0x23];
	uint32_t off;
	uint32_t serial;

	off = 0;
	GetDWord(buf, &off, &serial);

	PacketManager_MakePacket_MOBNAME(obuf, this, serial);

	SendToClient((CItem *)this, obuf, -1);
}

/*
 * 0x00498335 - HandlePacket_BRITANNIA_SELECT
 *
 * Answers the server-select packet with a USER_SERVER redirect.
 *
 * MODIFIED: binary is a stub (reads and discards one Word). We perform
 * a multi-client encryption dispatch, sending a USER_SERVER packet and
 * configuring game-connection encryption based on the detected client
 * version.
 */
void
HandlePacket_BRITANNIA_SELECT(CUserSock *this, uint8_t *buf)
{
	uint8_t obuf[bufSize];
	uint32_t off;
	uint16_t v;
	uint32_t nonce;
	uint16_t port;
	int gc;
	int bfIdx;
	int xorVer;
	int clientEnum;
	uint16_t *pktTable;

	memset(obuf, 0, sizeof(obuf));

	USED(this);

	off = 0;
	GetWord(buf, &off, &v);

	gc = GAME_NONE;
	bfIdx = 0;
	xorVer = -1;
	clientEnum = g_AutoDetect ? this->detectedKeyIndex : g_ClientVersion;
	pktTable = this->packetTable;
	port = g_ServerPort;

	if (g_NoCrypt) {
		// -nocrypt: no game cipher, standard port.
	} else if (g_ClientVersion == CLIENT_GOD208 || this->detectedGodClient) {
		// GodClient 0x8C handler (0x0043CCA0) adds 1000 to the port.
		// Send port - 1000 so client connects back to the right port.
		// No Blowfish/Twofish: GodClient game connection is plaintext.
		port = g_ServerPort - 1000;
		clientEnum = CLIENT_GOD208;
	} else {
		const ClientVersionInfo *info = Version_Find(clientEnum);
		if (info) {
			gc = info->gameCipher;
			bfIdx = info->bfIndex;
			// CLIENT_200 shares XOR keys with CLIENT_200C so
			// auto-detect can't distinguish them. Allocate
			// both ciphers; trial decrypt on the game connection
			// sorts it out.
			if (g_AutoDetect && info->clientEnum == CLIENT_200)
				gc = GAME_BF_TF;
			if (gc == GAME_NONE)
				xorVer = info->clientEnum;
			pktTable = Version_GetPacketTable(info->protocol);
		}
	}

	nonce = PendingAuth_Create(gc, bfIdx, pktTable, xorVer, clientEnum);
	PacketManager_MakePacket_USER_SERVER(&obuf[0], v, port, nonce, this->serverAddr);
	Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
}

/*
 * 0x0049835A - HandlePacket_TEXT_ENTRY
 *
 * Text entry gump response. Reads serial(DWord), promptID(DWord),
 * promptType(DWord), text(variable length from packet size).
 * Allocates textBuf via OperatorNew, copies text, null-terminates.
 * Calls CWorld_FindBySerial, then Entity_ExecuteEvent(entity, 0x3A,
 * promptID, CMobile_GetSerial(player), promptType, textBuf).
 * Frees textBuf. Verified exact match with binary.
 */
void
HandlePacket_TEXT_ENTRY(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint32_t promptID;
	uint32_t promptType;
	CItem *entity;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetDWord(buf, &off, &promptID);
	GetDWord(buf, &off, &promptType);

	{
		int textLen;
		char *text;
		char *textBuf;

		textLen = (int)(GetPacketOffset(buf) & 0xFFFF) - (int)off;
		if (textLen <= 1)
			return;

		textBuf = OperatorNew(textLen);
		GetString(buf, &off, &text, textLen);
		strncpy(textBuf, text, textLen);
		textBuf[textLen - 1] = '\0';

		entity = CWorld_FindBySerial(g_World, serial);
		if (entity != NULL) {
			Entity_ExecuteEvent(
			        &entity->resourceEntity.entity, StringResponse, (uintptr_t)promptID, (uintptr_t)this->mobile.container.item.serial, (int)promptType, textBuf);
		}

		OperatorDelete(textBuf);
	}
}

/*
 * 0x00498461 - HandlePacket_REQUEST_ASSIST
 *
 * Help/assistance request. Reads helpType(Byte) and a 256-byte helpText.
 * Copies the text into a 255-byte local buffer. When the player is dead
 * (pflags & PlayerHasDeadFlag), responds with "You can not request help,
 * sorry." Otherwise attaches the "help" script via Entity_AttachScript.
 * Neither helpType nor the captured text is forwarded to the script.
 */
void
HandlePacket_REQUEST_ASSIST(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t helpType;
	char *helpText;
	char helpBuf[256];

	off = 0;
	GetByte(buf, &off, &helpType);
	GetString(buf, &off, &helpText, 256);

	strncpy(helpBuf, helpText, 254);
	helpBuf[255] = '\0';

	if (CPlayer_HasDeadFlag(this)) {
		CPlayer_SystemMessage(this, "You can not request help, sorry.");
		return;
	}

	Entity_AttachScript(&this->mobile.container.item, "help", 1);
	USED(helpType);
	USED(helpBuf);
}

/*
 * 0x00498505 - HandlePacket_RequestAssistance (packet 0x9C)
 *
 * GM-only assistance request handler. Requires the GM flag. Reads request
 * fields, copies to local buffers with NUL termination, builds a
 * CAssistance, populates its CString fields, and submits via
 * CAssistanceQueue::Submit(1).
 *
 * Packet fields: type(Byte), priority(Byte), serial(DWord),
 *   name(String31), category(String15), body(String31)
 *
 * CAssistance object layout (0x38 bytes):
 *   +0x00: serial (dword)
 *   +0x04: CString name (0x10 bytes)
 *   +0x14: type (byte)
 *   +0x15: priority (byte)
 *   +0x18: CString unused (0x10 bytes)
 *   +0x28: CString body (0x10 bytes)
 *
 * Binary quirk: the body memcpy mistakenly reads from the name pointer,
 * copying 0x100 bytes from the name field position in the packet buffer.
 */
void
HandlePacket_RequestAssistance(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t type, priority;
	uint32_t serial;
	char *name, *category, *body;
	char nameBuf[31];     // 0x1E bytes copied + null
	char catBuf[16];      // 0x0F bytes copied + null
	char bodyBuf[257];    // 0x100 bytes copied + null

	if (!CPlayer_IsGameMaster(this))
		return;

	off = 0;

	GetByte(buf, &off, &type);
	GetByte(buf, &off, &priority);
	GetDWord(buf, &off, &serial);
	GetString(buf, &off, &name, 0x1F);

	memcpy(nameBuf, name, 0x1E);
	nameBuf[0x1E] = '\0';

	GetString(buf, &off, &category, 0x0F);

	memcpy(catBuf, category, 0x0F);
	catBuf[0x0E] = '\0';

	GetString(buf, &off, &body, 0x1F);

	// Copies 0x100 bytes from name field position in packet buffer.
	memcpy(bodyBuf, name, 0x100);
	bodyBuf[0xFF] = '\0';

	// three CStrings at +4/+18/+28, type=0, priority=0.
	// Assistance queue subsystem not implemented.
	USED(type);
	USED(priority);
	USED(serial);
	USED(nameBuf);
	USED(catBuf);
	USED(bodyBuf);
}

/*
 * 0x004986A9 - HandlePacket_REQ_TIP
 *
 * No-op stub.
 */
void
HandlePacket_REQ_TIP(CPlayer *this, uint8_t *buf)
{
	USED(this);
	USED(buf);
}

/*
 * 0x004986AE - HandlePacket_STRING_RESPONSE
 *
 * Forwards a dialog-string response to the target entity via event
 * StringResponse (0x3A).
 */
void
HandlePacket_STRING_RESPONSE(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint16_t promptType;
	uint8_t responseType;
	uint16_t textLength;
	char *text;
	CItem *entity;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetWord(buf, &off, &promptType);
	GetByte(buf, &off, &responseType);
	GetWord(buf, &off, &textLength);
	GetString(buf, &off, &text, textLength);

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		return;

	Entity_ExecuteEvent(&entity->resourceEntity.entity, StringResponse, (uintptr_t)promptType, (uintptr_t)this->mobile.container.item.serial, (int)responseType, text);
}

/*
 * 0x9D @ 0x00498846 - GMSingle
 *
 * GM single-click event handler. Requires the GM flag. Reads 9 fields and
 * forwards a subset (type, player serial, serial, artId, x, y, z) to a
 * delegate that the binary stubs out. hue, amount, and name are parsed but
 * never used.
 *
 * Packet fields: type(Byte), serial(DWord), artId(DWord), x(Word),
 *   y(Word), z(Word), hue(DWord), amount(Byte), name(String30)
 */
void
HandlePacket_GMSingle(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t type;
	uint32_t serial, artId, hue;
	uint16_t x, y, z;
	uint8_t amount;
	char *name;

	if (!CPlayer_IsGameMaster(this))
		return;

	type = 0;
	serial = 0;
	artId = 0;
	x = 0xFFFF;
	y = 0xFFFF;
	z = 0xFFFF;
	hue = 0;
	amount = 0;
	name = NULL;

	off = 0;
	GetByte(buf, &off, &type);
	GetDWord(buf, &off, &serial);
	GetDWord(buf, &off, &artId);
	GetWord(buf, &off, &x);
	GetWord(buf, &off, &y);
	GetWord(buf, &off, &z);
	GetDWord(buf, &off, &hue);
	GetByte(buf, &off, &amount);
	GetString(buf, &off, &name, 0x1E);

	{
		CSkillUseCtx ctx;

		CSkillUseCtx_Init(&ctx);
		ctx.type = type;
		ctx.serial = CMobile_GetSerial(&this->mobile);
		ctx.field08 = serial;
		ctx.field0C = artId;
		ctx.location.x = x;
		ctx.location.y = y;
		ctx.location.z = z;
		CEditorObj_HandleGMSingle((CEditorObj *)&g_GMPlayerList, &ctx);
	}
	USED(hue);
	USED(amount);
	USED(name);
}

/*
 * 0x0049D19E - DoHandlePacket_Player
 *
 * Post-login packet dispatcher. Routes packet type to handler function.
 * Binary has 48-entry switch (types 0x02-0xB1, indexed by type-2).
 * Default falls through to GM switch if PlayerIsEditing.
 *
 * MODIFIED: Added CLIENT_VERSION (0xBD) case for 1.26+ clients.
 */
void
DoHandlePacket_Player(CPlayer *this, int type, uint8_t *buf)
{
	switch (type) {
	case PacketType_REQ_MOVE:
		HandlePacket_REQ_MOVE(this, buf);
		break;
	case PacketType_SPEECH:
		HandlePacket_SPEECH(this, buf);
		break;
	case PacketType_GODMODE_TOGGLE:
		HandlePacket_GODMODE_TOGGLE(this, buf);
		break;
	case PacketType_ATTACK:
		HandlePacket_ATTACK(this, buf);
		break;
	case PacketType_REQ_OBJUSE:
		HandlePacket_REQ_OBJUSE(this, buf);
		break;
	case PacketType_REQ_GETOBJ:
		HandlePacket_REQ_GETOBJ(this, buf);
		break;
	case PacketType_REQ_DROPOBJ:
		HandlePacket_REQ_DROPOBJ(this, buf);
		break;
	case PacketType_REQ_LOOK:
		HandlePacket_REQ_LOOK(this, buf);
		break;
	case PacketType_GODCOMMAND:
		HandlePacket_GODCOMMAND(this, buf);
		break;
	case PacketType_REQ_OBJEQUIP:
		HandlePacket_REQ_OBJEQUIP(this, buf);
		break;
	case PacketType_OK_MOVE:
		HandlePacket_OK_MOVE(this, buf);
		break;
	case PacketType_DEATH:
		HandlePacket_DEATH(this, buf);
		break;
	case PacketType_CLIENTQUERY:
		HandlePacket_CLIENTQUERY(this, buf);
		break;
	case PacketType_MOVEOBJECT:
		HandlePacket_MOVEOBJECT(this, buf);
		break;
	case PacketType_GROUPS:
		HandlePacket_GROUPS(this, buf);
		break;
	case PacketType_OFFERACCEPT:
		HandlePacket_OFFERACCEPT(this, buf);
		break;
	case PacketType_CHECK_VER:
		HandlePacket_CHECK_VER(this, buf);
		break;
	case PacketType_POSTMSG:
		HandlePacket_POSTMSG(this, buf);
		break;
	case PacketType_MAP_COMMAND:
		HandlePacket_MAP_COMMAND(this, buf);
		break;
	case PacketType_BOOKPAGE:
		HandlePacket_BOOKPAGE(this, buf);
		break;
	case PacketType_FRIENDS:
		HandlePacket_FRIENDS(this, buf);
		break;
	case PacketType_KEY_USE:
		HandlePacket_KEY_USE(this, buf);
		break;
	case PacketType_TARGET:
		HandlePacket_TARGET(this, buf);
		break;
	case PacketType_TRADE:
		HandlePacket_TRADE(this, buf);
		break;
	case PacketType_BBOARD:
		HandlePacket_BBOARD(this, buf);
		break;
	case PacketType_COMBAT:
		HandlePacket_COMBAT(this, buf);
		break;
	case PacketType_PING:
		if (this != NULL)
			HandlePacket_PING(this, buf);
		break;
	case PacketType_RENAME_MOB:
		HandlePacket_RENAME_MOB(this, buf);
		break;
	case PacketType_ResourceQuery:
		HandlePacket_ResourceQuery(this, buf);
		break;
	case PacketType_PICKEDOBJ:
		HandlePacket_PICKEDOBJ(this, buf);
		break;
	case PacketType_GodViewQuery:
		HandlePacket_GodViewQuery(this, buf);
		break;
	case PacketType_SendResources:
		HandlePacket_SendResources(this, buf);
		break;
	case PacketType_TriggerEdit:
		HandlePacket_TriggerEdit(this, buf);
		break;
	case PacketType_POSTLOGIN:
		HandlePacket_POSTLOGIN_Player(this, buf);
		break;
	case PacketType_BOOKHDR:
		HandlePacket_BOOKHDR(this, buf);
		break;
	case PacketType_HUEPICKER:
		HandlePacket_HUEPICKER(this, buf);
		break;
	case PacketType_GameCentMon:
		HandlePacket_GameCentMon(this, buf);
		break;
	case PacketType_MOBNAME:
		HandlePacket_MOBNAME(this, buf);
		break;
	case PacketType_TEXT_ENTRY:
		HandlePacket_TEXT_ENTRY(this, buf);
		break;
	case PacketType_REQUEST_ASSIST:
		HandlePacket_REQUEST_ASSIST(this, buf);
		break;
	case PacketType_SHOP_OFFER:
		HandlePacket_SHOP_OFFER(this, buf);
		break;
	case PacketType_HARDWARE_INFO:
		HandlePacket_HARDWARE_INFO(this, buf);
		break;
	case PacketType_REQ_TIP:
		HandlePacket_REQ_TIP(this, buf);
		break;
	case PacketType_STRING_RESPONSE:
		HandlePacket_STRING_RESPONSE(this, buf);
		break;
	case PacketType_SPEECH_UNICODE:
		HandlePacket_SPEECH_UNICODE(this, buf);
		break;
	case PacketType_GumpMenuSelection:
		HandlePacket_GumpMenuSelection(this, buf);
		break;
	case PacketType_CHAT_TEXT:
		if (feat(FEAT_CHAT))
			HandlePacket_CHAT_TEXT(this, buf);
		break;
	case PacketType_CHAT_OPEN:
		if (feat(FEAT_CHAT))
			HandlePacket_CHAT_OPEN(this, buf);
		break;
	case PacketType_SKILLS:
		if (feat(FEAT_SKILL_LOCK))
			HandlePacket_SKILLOCK(this, buf);
		break;
	default:
		if (this->pflags & PlayerIsEditing) {
			switch (type) {
			case PacketType_EDIT:
				HandlePacket_EDIT(this, buf);
				break;
			case PacketType_TILEDATA:
				HandlePacket_TILEDATA(this, buf);
				break;
			case PacketType_TEMPLATEDATA:
				HandlePacket_TEMPLATEDATA(this, buf);
				break;
			case PacketType_ELEVCHANGE:
				HandlePacket_ELEVCHANGE(this, buf);
				break;
			case PacketType_NPCCONVO_DATA:
				HandlePacket_NPCCONVO_DATA(this, buf);
				break;
			case PacketType_DESTROY_OBJECT:
				HandlePacket_DESTROY_OBJECT(this, buf);
				break;
			case PacketType_AddResource:
				HandlePacket_AddResource(this, buf);
				break;
			case PacketType_RESOURCETILEDATA:
				HandlePacket_RESOURCETILEDATA(this, buf);
				break;
			case PacketType_NEW_ART:
				HandlePacket_NEW_ART(this, buf);
				break;
			case PacketType_NEW_TERR:
				HandlePacket_NEW_TERR(this, buf);
				break;
			case PacketType_NEW_ANIM:
				HandlePacket_NEW_ANIM(this, buf);
				break;
			case PacketType_NEW_HUES:
				HandlePacket_NEW_HUES(this, buf);
				break;
			case PacketType_DESTROY_ART:
				HandlePacket_DESTROY_ART(this, buf);
				break;
			case PacketType_NEW_REGION:
				HandlePacket_NEW_REGION(this, buf);
				break;
			case PacketType_DESTROY_STATIC:
				HandlePacket_DESTROY_STATIC(this, buf);
				break;
			case PacketType_MOVESTATIC:
				HandlePacket_MOVESTATIC(this, buf);
				break;
			case PacketType_SIMPED:
				HandlePacket_SIMPED(this, buf);
				break;
			case PacketType_RequestAssistance:
				HandlePacket_RequestAssistance(this, buf);
				break;
			case PacketType_GMSingle:
				HandlePacket_GMSingle(this, buf);
				break;
			default:
				return;
			}
		}
		break;
	}
}

/*
 * 0x0049DAC0 - GetSizeLength
 *
 * Binary unconditionally reads g_PacketTable. We select the
 * per-connection packet table when a CUserSock is active, for
 * multi-client support.
 */
uint16_t
GetSizeLength(uint8_t *buf)
{
	uint16_t result;

	{
		uint16_t *pt = GLOBAL_CUserSock ? GLOBAL_CUserSock->packetTable : g_PacketTable;
		if (pt[5 * *buf] & PacketDynamicSize)
			result = 3;
		else
			result = 1;
	}
	return result;
}

/*
 * 0x0049DAA0
 */
uint16_t
SetPacketType(uint8_t *buf, uint8_t type)
{
	*buf = type;
	return type;
}

/*
 * 0x0049DA80
 */
uint16_t
InitializeGlobalPacketOffset(void)
{
	g_PacketOffset = 0;
	return 0;
}

/*
 * 0x0049DB00 - PacketManager::GetPacketByte
 *
 * Returns *ptr, zero-extended.
 */
uint8_t
PacketManager_GetPacketByte(uint8_t *ptr)
{
	return *ptr;
}

/*
 * 0x0049DB50
 */
uint16_t
SetGlobalOffset(uint16_t off)
{
	g_PacketOffset = off;
	return off;
}

/*
 * 0x0049DB10
 */
uint16_t
SetPacketOffset(uint8_t *buf, uint16_t off)
{
	if (!PacketIsDynamicSize(buf))
		return SetGlobalOffset(off);
	memcpy(buf + 1, &off, 2);
	return off;
}

/*
 * 0x004B3390 - BuildTriggerPacket
 *
 * Builds a trigger packet (0x7A). Format:
 * PutPacketType(buf, 0x7A, 0xE004), PutByte(subtype),
 * PutString(data, datalen).
 */
void
BuildTriggerPacket(uint8_t *buf, uint8_t subtype, int32_t datalen, char *data)
{
	PutPacketType(buf, 0x7A, 0xE004);
	PutByte(buf, subtype);
	PutString(buf, data, datalen);
}

// ResQuerySession - session context for HandlePacket_ResourceQuery.
// Binary: 0x1C-byte struct, 8 slots at 0x006E7308.
__extension__ typedef struct ResQuerySession {
	int32_t *regionNames;             // +0x00 - allocated buffer (freed by cleanup)
	int32_t *templateNames;           // +0x04 - allocated buffer (freed by cleanup)
	int32_t *field103D8;              // +0x08 - allocated buffer (freed by cleanup)
	int32_t *templateChainCountCache; // +0x0C - allocated buffer (freed by cleanup)
	int32_t regionIndex;              // +0x10 - current region index (-1 = none)
	uint32_t serial;                  // +0x14 - player serial who owns this session
	int32_t timestamp;                // +0x18 - usage timestamp (0 = most recent)
} ResQuerySession;

// Binary: g_ResSessionSlots[8] at 0x006E7308.
ResQuerySession *g_ResSessionSlots[8];

// Binary: g_ResSessionInit at 0x006E7664.
int g_ResSessionInit;

// Binary: circular buffer of resource entry names at 0x006E7328 (100 char* ptrs).
char *g_ResQueryEntryNames[100]; // 0x006E7328

// Binary: circular buffer of resource entry values at 0x006E74B8 (100 int32_t).
int32_t g_ResQueryEntryValues[100]; // 0x006E74B8

// Binary: cached template processing limit at 0x006E7648.
int g_ResQueryTemplateLimit; // 0x006E7648

// Binary: current write index in circular resource entry buffer at 0x006E764C.
int g_ResQueryEntryIndex; // 0x006E764C

// Binary: cached resource type count at 0x006E7668.
int g_ResQueryTypeCount; // 0x006E7668

// Binary: signpost count at 0x0068B398.
int g_SignpostCount;

/*
 * 0x004B4C97 - BuildGodViewPacket
 *
 * Constructs a 0x7F (GodView) response packet with fixed size 0x2006.
 * type: response subtype byte (query subtype or entity category on mid-flush)
 * count: number of entities in this batch
 * dataLen: bytes of entity data
 * data: entity records (10 bytes each: serial(4) + x(2) + y(2) + category(2))
 */
static void
BuildGodViewPacket(uint8_t *buf, uint8_t type, uint16_t count, int dataLen, uint8_t *data)
{
	PutPacketType(buf, 0x7F, 0x2006);
	PutByte(buf, type);
	PutWord(buf, count);
	PutString(buf, (char *)data, dataLen);
}

/*
 * 0x004B4CE4 - HandlePacket_GodViewQuery
 *
 * GM God View / World Map query (packet 0x7E, response 0x7F).
 * Reads: subtype(Byte). When subtype==0, scans entire spatial grid and returns
 * entity positions classified by type.
 *
 * Classification (vtable dispatch):
 *   VT_IS_NPC (vtable[0xE4]): bodyType < 0xC8 → 1 (animal),
 *     0xC8..0x18F → 2 (humanoid), >= 0x190 → 3 (large/player)
 *   VT_CHECK_EC (vtable[0xEC]): → 0xFE
 *   VT_IS_PLAYER (vtable[0x18]): → 0xFF
 *
 * Batched at 800 entities per 0x7F response packet (fixed 0x2006 bytes).
 * Mid-loop flush writes last entity's category as type byte; final flush
 * writes original subtype. Empty result sends type=0xFF, count=0.
 */
void
HandlePacket_GodViewQuery(CPlayer *this, uint8_t *buf)
{
	uint32_t offset;
	uint8_t subtype;
	uint8_t pktBuf[0x2006];
	uint8_t dataBuf[0x2000];
	uint8_t *dataPtr;
	int count;
	int i;
	CItem *entity;
	uint16_t category;
	int dataLen;

	offset = 0;
	GetByte(buf, &offset, &subtype);

	if (subtype != 0)
		return;

	dataPtr = dataBuf;
	count = 0;

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		entity = g_SpatialGrid.cells[i].itemHead;

		while (entity != NULL) {
			// vtable[0x9C] - VT_ITEM_CHECK_9C
			if (((int (*)(void *))VT_FN(entity, VT_ITEM_CHECK_9C))(entity)) {
				entity = entity->spatialNext;
				continue;
			}

			category = 0;

			// vtable[0xE4] - VT_IS_NPC
			if (VT_IsNPC(entity)) {
				if (CResourceEntity_GetBodyType(entity) < 0xC8)
					category = 1;
				if (CResourceEntity_GetBodyType(entity) >= 0xC8 && CResourceEntity_GetBodyType(entity) < 0x190)
					category = 2;
				if (CResourceEntity_GetBodyType(entity) >= 0x190)
					category = 3;
			}

			// vtable[0xEC] - VT_CHECK_EC
			if (((int (*)(void *))VT_FN(entity, VT_CHECK_EC))(entity))
				category = 0xFE;

			// vtable[0x18] - VT_IS_PLAYER
			if (VT_IsPlayer(entity))
				category = 0xFF;

			if (category == 0) {
				entity = entity->spatialNext;
				continue;
			}

			// Binary: fcn.004b3841/004b395c call SwapEndian on
			// stack copies then memcpy - no-op on LE.
			{
				uint32_t tmpSerial = entity->serial;
				SwapEndian(&tmpSerial);
				memcpy(dataPtr, &tmpSerial, 4);
				dataPtr += 4;
			}
			{
				uint16_t tmpX = entity->resourceEntity.entity.location.x;
				SwapEndian(&tmpX);
				memcpy(dataPtr, &tmpX, 2);
				dataPtr += 2;
			}
			{
				uint16_t tmpY = entity->resourceEntity.entity.location.y;
				SwapEndian(&tmpY);
				memcpy(dataPtr, &tmpY, 2);
				dataPtr += 2;
			}
			{
				uint16_t tmpCat = category;
				SwapEndian(&tmpCat);
				memcpy(dataPtr, &tmpCat, 2);
				dataPtr += 2;
			}

			count++;

			if (count > 0x320) {
				dataLen = (int)(dataPtr - dataBuf);
				BuildGodViewPacket(pktBuf, (uint8_t)(category & 0xFFFF), (uint16_t)count, dataLen, dataBuf);
				SendToClient((CItem *)this, pktBuf, -1);
				count = 0;
				dataPtr = dataBuf;
			}

			entity = entity->spatialNext;
		}
	}

	if (count > 0) {
		dataLen = (int)(dataPtr - dataBuf);
		BuildGodViewPacket(pktBuf, (uint8_t)(subtype & 0xFF), (uint16_t)count, dataLen, dataBuf);
		SendToClient((CItem *)this, pktBuf, -1);
	} else {
		BuildGodViewPacket(pktBuf, 0xFF, count, 0, dataBuf);
		SendToClient((CItem *)this, pktBuf, -1);
	}
}

/*
 * 0x004B503E - SaveResources
 *
 * Empty stub. Called after resource region create/update/delete operations.
 */
void
SaveResources(void)
{
}

/*
 * 0x004B545E - TriggerEdit_Op545E
 *
 * Wrapper that removes a script attachment from entity->tagList->scriptList.
 */
static void
TriggerEdit_Op545E(CItem *ent, const char *data)
{
	CResourceEntity_RemoveScript(ent, data);
}

/*
 * 0x004B546F - TriggerEdit_Op546F
 *
 * Rescans script attachments: removes all instances of the named script
 * from every entity, optionally unlinks and frees the CScript object if
 * no other script inherits from it, then re-attaches the script to all
 * entities that had it. Used by TriggerEdit case 7 to reload a script.
 */
static void
TriggerEdit_Op546F(const char *scriptName)
{
	CVector vec;
	char typeFlag = 0;
	ScriptAttachNode *node;
	ScriptAttachNode *next;
	CScript *target;
	CScript *check;
	CScript **pp;
	uintptr_t *p;

	CVector_Constructor(&vec, &typeFlag);

	// Phase 1: walk global script instance list, remove matching attachments
	node = g_scriptInstanceListHead;
	while (node != NULL) {
		next = node->globalNext;

		if (node->scriptClassPtr == NULL)
			goto advance;
		if ((uintptr_t)node->scriptClassPtr == 0xABCD)
			goto advance;
		if (node->entity == NULL)
			goto advance;

		// Compare script name: CScript->name at offset 0x00
		if (strcmp(*(char **)node->scriptClassPtr, scriptName) == 0) {
			CVector_PushBack(&vec, (uintptr_t)&node->entity);
			CResourceEntity_RemoveScriptNode((CItem *)node->entity, node);
		}

advance:
		node = next;
	}

	// Phase 2: find the CScript with this name in the loaded scripts list
	target = g_ScriptManager.head;
	while (target != NULL) {
		if (strcmp(target->name, scriptName) == 0)
			break;
		target = target->nextLoaded;
	}

	if (target != NULL) {
		// Phase 3: check if any loaded script inherits from target
		check = g_ScriptManager.head;
		while (check != NULL) {
			if (check->parent == target)
				break;
			check = check->nextLoaded;
		}

		// If no script inherits from it, unlink and free
		if (check == NULL) {
			pp = &g_ScriptManager.head;
			while (*pp != NULL) {
				if (*pp == target) {
					*pp = target->nextLoaded;
					// Scalar deleting destructor (0x004B9AB0)
					CScript_Destructor(target);
					OperatorDelete(target);
					break;
				}
				pp = &((*pp)->nextLoaded);
			}
		}
	}

	// Phase 4: re-attach script to all entities that had it
	p = (uintptr_t *)vec.begin;
	while (p < (uintptr_t *)vec.end) {
		Entity_AttachScript(*(CItem **)p, scriptName, 1);
		p++;
	}

	CVector_Destructor(&vec);
}

// 0x004B545E - Trigger operation (called from case 2)

/*
 * 0x004B5640 - BuildTriggerEditResponse
 *
 * Builds a TriggerEdit response packet (0x8A). Format:
 * PutPacketType(buf, 0x8A, 0x2008), PutByte(subtype),
 * PutWord(connIndex), PutWord(datalen), PutString(data, datalen).
 */
static void
BuildTriggerEditResponse(uint8_t *buf, uint8_t subtype, uint16_t connIndex, char *data, uint16_t datalen)
{
	PutPacketType(buf, 0x8A, 0x2008);
	PutByte(buf, subtype);
	PutWord(buf, connIndex);
	PutWord(buf, datalen);
	PutString(buf, data, datalen);
}

/*
 * 0x004B56A1 - TriggerEdit_FindTagDef
 *
 * Searches an entity's tag def list for an entry matching the given name.
 * Returns a pointer to the matching TagNode, or NULL if not found.
 * First checks CResourceEntity_HasTag with type 7, then walks the tag def
 * vector comparing entry->name via strcmp.
 */
TagNode *
TriggerEdit_FindTagDef(CItem *entity, const char *name)
{
	CVector vec;
	char typeFlag;
	TagNode **p;

	if (!CResourceEntity_HasTag(entity, name, 7))
		return NULL;

	CVector_Constructor(&vec, &typeFlag);
	CItem_GetTagDefListRaw(entity, &vec);

	p = (TagNode **)vec.begin;
	while (p < (TagNode **)vec.end) {
		TagNode *entry = *p;
		if (strcmp(entry->name, name) == 0) {
			CVector_Destructor(&vec);
			return entry;
		}
		p++;
	}

	CVector_Destructor(&vec);
	return NULL;
}

/*
 * 0x004B5775 - TriggerEdit_DeleteEntity
 *
 * Toggles an entity between "deleted" (body type 1, placehld script)
 * and "alive" states in the TriggerEdit system.
 *
 * Item path (bodyType != 1 - "delete"):
 *   1. Save _objectType as int ObjVar, set body type to 1.
 *   2. Save _scripts from current script list, remove scripts.
 *   3. Toggle "link"/"_link" prefixes on string tag names via intern.
 *   4. Attach "placehld" script.
 *
 * Mobile path (bodyType == 1 - "undelete"):
 *   1. Restore body type from _objectType tag, remove tag.
 *   2. Remove "placehld", re-attach scripts from _scripts tag.
 *   3. Toggle "link"/"_link" prefixes on string tag names via intern.
 *
 * Saves and restores entity location via VT_HIDE/VT_DROP_AT_FEET.
 */
static void
TriggerEdit_DeleteEntity(CItem *entity)
{
	CLocation savedLoc;
	uint16_t bodyType;
	CVector tagDefVec, scriptVec;
	char typeFlag, typeFlag2;
	uintptr_t *p;

	CLocation_SetLoc(&savedLoc, &entity->resourceEntity.entity.location);

	((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);

	bodyType = CEntity_GetBodyType(entity) & 0xFFFF;

	if (bodyType != 1) {
		// Item path: "delete" - save state and set to placehld

		TriggerEdit_SetStringProp(entity, "_objectType");

		bodyType = CEntity_GetBodyType(entity) & 0xFFFF;
		{
			CString _n;
			CString_Constructor(&_n, "_objectType");
			ObjVar_SetStr(entity, &_n, 0, (uint32_t)bodyType);
		}

		CEntity_SetBodyType(entity, 1);

		TriggerEdit_SetStringProp(entity, "_scripts");

		if (!CItem_HasTagDefs(entity) && !CItem_HasScripts(entity))
			goto item_attach_placehld;

		// Tag def iteration: toggle "link"/"_link" prefixes
		CVector_Constructor(&tagDefVec, &typeFlag);
		CItem_GetTagDefListRaw(entity, &tagDefVec);
		p = (uintptr_t *)tagDefVec.begin;
		while (p < (uintptr_t *)tagDefVec.end) {
			TagNode *entry = (TagNode *)*p;
			if (entry->type == 1) {
				// String tag - check for link prefix
				if (strncmp("link", entry->name, 4) == 0) {
					char internBuf[256];
					internBuf[0] = '_';
					strcpy(&internBuf[1], entry->name);
					entry->name = (char *)CScriptManager_InternString(&g_ScriptManager, internBuf);
				} else if (strncmp("_link", entry->name, 5) == 0) {
					entry->name = (char *)CScriptManager_InternString(&g_ScriptManager, entry->name + 1);
				}
			}
			p++;
		}

		// Script iteration: build "+"-separated list, remove each
		{
			char scriptsBuf[0x238];
			scriptsBuf[0] = '\0';

			CVector_Constructor(&scriptVec, &typeFlag2);
			CItem_GetScriptListRaw(entity, &scriptVec);
			p = (uintptr_t *)scriptVec.begin;
			while (p < (uintptr_t *)scriptVec.end) {
				ScriptAttachNode *node = (ScriptAttachNode *)*p;
				const char *scriptName;
				if (scriptsBuf[0] != '\0')
					strcat(scriptsBuf, "+");
				// node->scriptClassPtr->name
				scriptName = *(const char **)node->scriptClassPtr;
				strcat(scriptsBuf, scriptName);
				TriggerEdit_Op545E(entity, scriptName);
				p++;
			}

			// If scripts were found, save as _scripts ObjVar
			if (scriptsBuf[0] != '\0') {
				CString _v, _n;
				CString_Constructor(&_v, scriptsBuf);
				CString_Constructor(&_n, "_scripts");
				ObjVar_SetStr(entity, &_n, 1, (uintptr_t)&_v);
				CString_Destructor(&_v);
			}

			CVector_Destructor(&scriptVec);
		}

		CVector_Destructor(&tagDefVec);

item_attach_placehld:
		Entity_AttachScript(entity, "placehld", 1);
	} else {
		// Mobile path: "undelete" - restore state from saved props
		TagNode *tagDef;

		tagDef = TriggerEdit_FindTagDef(entity, "_objectType");
		if (tagDef != NULL) {
			uint16_t savedType = (uint16_t)tagDef->value;
			CEntity_SetBodyType(entity, savedType);
		} else {
			((void (*)(void *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, &savedLoc);
			return;
		}

		TriggerEdit_SetStringProp(entity, "_objectType");

		TriggerEdit_Op545E(entity, "placehld");

		tagDef = TriggerEdit_FindTagDef(entity, "_scripts");
		if (tagDef != NULL && ((TagNode *)tagDef)->value != 0) {
			CString *cstr = (CString *)(uintptr_t)((TagNode *)tagDef)->value;
			char *rawStr = CString_GetCStr(cstr);
			int pos = 0;
			int start = 0;

			while (1) {
				if (rawStr[pos] == '+') {
					rawStr[pos] = '\0';
					Entity_AttachScript(entity, &rawStr[start], 1);
					start = pos + 1;
				} else if (rawStr[pos] == '\0') {
					Entity_AttachScript(entity, &rawStr[start], 1);
					break;
				}
				pos++;
			}
		}

		TriggerEdit_SetStringProp(entity, "_scripts");

		if (!CItem_HasTagDefs(entity) && !CItem_HasScripts(entity))
			goto restore_location;

		// Tag def iteration: toggle "link"/"_link" prefixes (same as item path)
		CVector_Constructor(&tagDefVec, &typeFlag);
		CItem_GetTagDefListRaw(entity, &tagDefVec);
		p = (uintptr_t *)tagDefVec.begin;
		while (p < (uintptr_t *)tagDefVec.end) {
			TagNode *entry = (TagNode *)*p;
			if (entry->type == 1) {
				if (strncmp("link", entry->name, 4) == 0) {
					char internBuf[256];
					internBuf[0] = '_';
					strcpy(&internBuf[1], entry->name);
					entry->name = (char *)CScriptManager_InternString(&g_ScriptManager, internBuf);
				} else if (strncmp("_link", entry->name, 5) == 0) {
					entry->name = (char *)CScriptManager_InternString(&g_ScriptManager, entry->name + 1);
				}
			}
			p++;
		}

		CVector_Destructor(&tagDefVec);
	}

restore_location:
	((void (*)(void *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, &savedLoc);
}

/*
 * 0x004B5DC7 - HandlePacket_TriggerEdit
 *
 * GM Trigger/Region Editor (packet 0x8A, response 0x8A).
 * Reads: mode(Byte), connIndex(Word), dataLen(Word), data(String).
 * mode 0x10: query triggers in bounding rect (4096-bucket scan, ~82KB stack)
 * mode 0x0F: reload trigger defs (ProcessDynamicItems)
 * mode 0x0E: edit trigger properties (10 DWords)
 * mode 0x0D: create/query trigger (5 DWords + optional child lookup)
 * mode 0x0C: delete triggers by serial (count + serial loop)
 * mode 0x01-0x0B: trigger operations via jump table (11 cases)
 */
void
HandlePacket_TriggerEdit(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint8_t mode;
	uint16_t connIndex, dataLen;
	char *data;
	char *dataSaved;
	char *readCur;
	char *writeCur;
	uint8_t responseBuf[0x2040];
	char localBuf[0x2038];
	int32_t entitySerial;
	CItem *entity;

	off = 0;
	GetByte(buf, &off, &mode);
	GetWord(buf, &off, &connIndex);
	GetWord(buf, &off, &dataLen);
	GetString(buf, &off, &data, dataLen);

	dataSaved = data;
	writeCur = localBuf;
	readCur = data;

	if ((mode & 0xFF) == 0x10) {
		// Mode 0x10: region query - scan 0x1000 template chain buckets
		char regionBuf[0x10050];
		uint8_t regionPktBuf[0x10054];
		char *regionWriteCur;
		int32_t x1, y1, x2, y2;
		int32_t bucketCounts[0x1000];
		int i, entX, entY, totalFound;
		CItem *cur;
		int32_t totalDataLen;

		regionWriteCur = regionBuf;

		// Read bounding rect
		x1 = ReadInt32LE(&readCur);
		y1 = ReadInt32LE(&readCur);
		x2 = ReadInt32LE(&readCur);
		y2 = ReadInt32LE(&readCur);

		for (i = 0; i < 0x1000; i++) {
			cur = g_TemplateChain[i];
			bucketCounts[i] = 0;
			while (cur != NULL) {
				entX = (int16_t)cur->resourceEntity.entity.location.x;
				entY = (int16_t)cur->resourceEntity.entity.location.y;
				if (entX >= x1 && entX < x2 && entY >= y1 && entY < y2) {
					bucketCounts[i]++;
				}
				cur = cur->templateChainNext;
			}
		}

		WriteInt16LE(&regionWriteCur, (int16_t)connIndex);
		regionWriteCur += 2; // reserve space for totalFound (written later)

		totalFound = 0;
		for (i = 0; i < 0x1000; i++) {
			if (bucketCounts[i] == 0)
				continue;

			if (bucketCounts[i] > 0x7FFF) {
				int16_t idx16 = (int16_t)i;
				idx16 |= (int16_t)0x8000;
				WriteInt16LE(&regionWriteCur, idx16);
				WriteInt32LE(&regionWriteCur, bucketCounts[i]);
			} else {
				WriteInt16LE(&regionWriteCur, (int16_t)i);
				WriteInt16LE(&regionWriteCur, (int16_t)bucketCounts[i]);
			}
			totalFound++;
		}

		if (totalFound == 0)
			return;

		{
			char *countPos = regionBuf + 2;
			WriteInt16LE(&countPos, (int16_t)totalFound);
		}

		totalDataLen = (int32_t)(regionWriteCur - regionBuf);

		BuildTriggerPacket(regionPktBuf, 0x18, totalDataLen, regionBuf);
		SendToClient((CItem *)this, regionPktBuf, -1);
		return;
	}

	if ((mode & 0xFF) == 0x0F) {
		ProcessDynamicItems();
		return;
	}

	if ((mode & 0xFF) == 0x0E) {
		int32_t findSerial;
		CItem *editEnt;
		int32_t newBodyType, newColor;
		int32_t newLocX, newLocY, newLocZ;
		int32_t newBoundX, newBoundY, newBoundZ;
		int32_t newTemplateIdx, newExtra;
		CLocation newLoc;

		findSerial = ReadInt32LE(&readCur);
		editEnt = CWorld_FindBySerial(g_World, findSerial);
		if (editEnt == NULL)
			return;

		newBodyType = ReadInt32LE(&readCur);
		newColor = ReadInt32LE(&readCur);
		newLocX = ReadInt32LE(&readCur);
		newLocY = ReadInt32LE(&readCur);
		newLocZ = ReadInt32LE(&readCur);
		newBoundX = ReadInt32LE(&readCur);
		newBoundY = ReadInt32LE(&readCur);
		newBoundZ = ReadInt32LE(&readCur);
		newTemplateIdx = ReadInt32LE(&readCur);
		newExtra = ReadInt32LE(&readCur);
		USED(newTemplateIdx);
		USED(newExtra);

		if (editEnt->resourceEntity.entity.removedFromWorld == 0) {
			((void (*)(CItem *))VT_FN(editEnt, VT_HIDE))(editEnt);
		}

		CLocation_Set(&editEnt->resourceEntity.entity.location, (int16_t)newBoundX, (int16_t)newBoundY, (int16_t)newBoundZ);

		CEntity_SetBodyType(editEnt, (uint16_t)newBodyType);

		editEnt->resourceEntity.entity.color = (uint16_t)newColor;

		CLocation_Init(&newLoc);
		CLocation_Set(&newLoc, (int16_t)newLocX, (int16_t)newLocY, (int16_t)newLocZ);

		if (editEnt->multiPtr != NULL) {
			TriggerEdit_MultiUpdate(editEnt, &newLoc);
		} else {
			if (CBlockManager_IsValidCoord(&g_SpatialGrid, newLocX, newLocY)) {
				((void (*)(CItem *, CLocation *))VT_FN(editEnt, VT_DROP_AT_FEET))(editEnt, &newLoc);
			} else {
				((void (*)(CItem *, CLocation *))VT_FN(editEnt, VT_DROP_AT_FEET))(editEnt, &editEnt->resourceEntity.entity.location);
			}
		}
		return;
	}

	if ((mode & 0xFF) == 0x0D) {
		int32_t queryBodyType;
		int32_t queryX, queryY, queryZ;
		int32_t hasChild;
		int32_t childSerial;
		CItem *foundEnt;
		int32_t childCount;
		CResourceNode *childCur;
		uint16_t respConnIndex;

		respConnIndex = 0;

		queryBodyType = ReadInt32LE(&readCur);
		queryX = ReadInt32LE(&readCur);
		queryY = ReadInt32LE(&readCur);
		queryZ = ReadInt32LE(&readCur);
		hasChild = ReadInt32LE(&readCur);

		childSerial = 0;
		foundEnt = NULL;

		if (hasChild != 0) {
			childSerial = ReadInt32LE(&readCur);
			foundEnt = CWorld_FindBySerial(g_World, childSerial);
		} else {
			if (CBlockManager_IsValidCoord(&g_SpatialGrid, queryX, queryY)) {
				int blockIdx;
				CItem *scanEnt;

				blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, queryX, queryY, 0);

				scanEnt = g_MapBlocks[blockIdx].staticHead;
				while (scanEnt != NULL) {
					if ((int16_t)scanEnt->resourceEntity.entity.location.x == queryX && (int16_t)scanEnt->resourceEntity.entity.location.y == queryY &&
					        (int16_t)scanEnt->resourceEntity.entity.location.z == queryZ) {
						uint16_t entBodyType;
						entBodyType = CEntity_GetBodyType(scanEnt) & 0xFFFF;
						if ((int)entBodyType == queryBodyType) {
							foundEnt = scanEnt;
							break;
						}
					}
					scanEnt = (CItem *)scanEnt->resourceEntity.nextInContainer;
				}
			}
		}

		if (foundEnt == NULL) {
			BuildTriggerEditResponse(responseBuf, mode, respConnIndex, localBuf, 1);
			SendToClient((CItem *)this, responseBuf, -1);
			return;
		}

		readCur = writeCur;
		respConnIndex++;

		// Write entity body type (via PutWord)
		{
			int32_t bt = CEntity_GetBodyType(foundEnt) & 0xFFFF;
			WriteInt32LE(&readCur, bt);
		}

		respConnIndex++;
		// Write entity color
		{
			uint16_t entColor = foundEnt->resourceEntity.entity.color;
			WriteInt32LE(&readCur, (int32_t)entColor);
		}

		respConnIndex++;
		// Write entity location X
		{
			int32_t locX = (int16_t)foundEnt->resourceEntity.entity.location.x;
			WriteInt32LE(&readCur, locX);
		}

		respConnIndex++;
		// Write entity location Y
		{
			int32_t locY = (int16_t)foundEnt->resourceEntity.entity.location.y;
			WriteInt32LE(&readCur, locY);
		}

		respConnIndex++;
		// Write entity location Z
		{
			int32_t locZ = (int16_t)foundEnt->resourceEntity.entity.location.z;
			WriteInt32LE(&readCur, locZ);
		}

		respConnIndex++;
		// Write hasChild flag
		WriteInt32LE(&readCur, hasChild);

		if (hasChild != 0) {
			CItem *resCur = foundEnt;

			respConnIndex++;
			// Write entity offset 0x10 (nextInContainer low 16 bits)
			{
				int32_t val = (int16_t)(uint16_t)(uintptr_t)resCur->resourceEntity.nextInContainer;
				WriteInt32LE(&readCur, val);
			}

			respConnIndex++;
			// Write entity offset 0x12 (nextInContainer high 16 bits on 32-bit)
			{
				int32_t val = (int16_t)(uint16_t)((uintptr_t)resCur->resourceEntity.nextInContainer >> 16);
				WriteInt32LE(&readCur, val);
			}

			respConnIndex++;
			// Write entity offset 0x14 (staticPrev/pad14 union low 16 bits)
			{
				int32_t val = (int16_t)(uint16_t)(uintptr_t)resCur->resourceEntity.staticPrev;
				WriteInt32LE(&readCur, val);
			}

			respConnIndex++;
			// Write entity serial (offset 0x40)
			WriteInt32LE(&readCur, (int32_t)resCur->serial);

			respConnIndex++;
			// Write byte property via 0x00490AC3
			{
				int32_t val = CItem_GetByteProp(resCur) & 0xFF;
				WriteInt32LE(&readCur, val);
			}

			respConnIndex++;
			// Write sort key via 0x00490C6D
			{
				int32_t val = CItem_GetSortKey(resCur) & 0xFFFF;
				WriteInt32LE(&readCur, val);
			}

			respConnIndex++;
			// Write template index via 0x004E04A0
			{
				int32_t val = CResourceEntity_GetTemplateIndex(resCur) & 0xFFFF;
				WriteInt32LE(&readCur, val);
			}

			// Placeholder for child count (filled in after loop)
			{
				char *childCountPos = readCur;
				readCur += 4;
				respConnIndex++;

				childCount = 0;
				childCur = foundEnt->resourceEntity.firstChild;
				while (childCur != NULL) {
					respConnIndex++;
					// Write child id (CResourceNode.id)
					WriteInt32LE(&readCur, (int32_t)childCur->id);

					respConnIndex++;
					// Write child type (CResourceNode.type, sign-extended)
					WriteInt32LE(&readCur, (int32_t)(int8_t)childCur->type);

					respConnIndex++;
					// Write child value1
					WriteInt32LE(&readCur, childCur->value1);

					respConnIndex++;
					// Write child value2
					WriteInt32LE(&readCur, childCur->value2);

					respConnIndex++;
					// Write child value3
					WriteInt32LE(&readCur, childCur->value3);

					respConnIndex++;
					// Write 0
					WriteInt32LE(&readCur, 0);

					respConnIndex++;
					// Write 0
					WriteInt32LE(&readCur, 0);

					childCount++;
					childCur = childCur->next;
				}

				WriteInt32LE(&childCountPos, childCount);
			}
		}

		{
			int32_t dataSize = (respConnIndex & 0xFFFF) << 2;
			BuildTriggerEditResponse(responseBuf, mode, respConnIndex, localBuf, (uint16_t)dataSize);
			SendToClient((CItem *)this, responseBuf, -1);
		}
		return;
	}

	if ((mode & 0xFF) == 0x0C) {
		int32_t deleteCount;
		int32_t deleteIdx;
		int32_t foundCount;

		deleteCount = ReadInt32LE(&readCur);
		USED(deleteCount);
		foundCount = 0;

		for (deleteIdx = 1; (int)(connIndex & 0xFFFF) > deleteIdx; deleteIdx++) {
			int32_t delSerial;
			int blockScanIdx;

			delSerial = ReadInt32LE(&readCur);

			for (blockScanIdx = 0; blockScanIdx < (int)g_SpatialGrid.totalBlocks; blockScanIdx++) {
				CItem *scanItem;

				scanItem = g_MapBlocks[blockScanIdx].itemHead;
				while (scanItem != NULL) {
					if (((int (*)(void *))VT_FN(scanItem, VT_ITEM_CHECK_9C))(scanItem) == 0) {
						if (VT_IsMobile(scanItem) == 0) {
							// Check body type match
							if ((CEntity_GetBodyType(scanItem) & 0xFFFF) == (uint32_t)delSerial) {
								foundCount++;
							}
						}
					}
					scanItem = scanItem->spatialNext;
				}
			}
		}

		BuildTriggerEditResponse(responseBuf, mode, (uint16_t)foundCount, localBuf, 1);
		SendToClient((CItem *)this, responseBuf, -1);
		return;
	}

	entitySerial = ReadInt32LE(&readCur);
	readCur -= 4; // binary: sub edx, 4 at 0x004b6a9a
	entity = CWorld_FindBySerial(g_World, entitySerial);

	// If entity not found, allow only mode 0x0B (delete multiple)
	if (entity == NULL) {
		if ((mode & 0xFF) != 0x0B)
			return;
	}

	{
		int switchVal = (mode & 0xFF) - 1;

		if (switchVal > 0x0A)
			goto epilogue;

		switch (switchVal) {
		case 10: {
			int32_t delIdx;

			for (delIdx = 0; delIdx < (int)(connIndex & 0xFFFF); delIdx++) {
				int32_t delSer;
				CItem *delEnt;

				delSer = ReadInt32LE(&readCur);
				delEnt = CWorld_FindBySerial(g_World, delSer);
				if (delEnt != NULL) {
					TriggerEdit_DeleteEntity(delEnt);
				}
			}
			return;
		}

		case 9:
			Script_callback(entitySerial, 1, connIndex & 0xFFFF);
			return;

		case 8: {
			char *tagSearchStr;
			int32_t tagFoundCount;
			int32_t tagRemaining;
			int32_t iterIdx;
			CItem *tagEntity;

			tagSearchStr = readCur + 4;
			readCur = writeCur;

			// Write entity serial
			WriteInt32LE(&readCur, entitySerial);

			// Write connIndex
			WriteInt32LE(&readCur, connIndex & 0xFFFF);

			writeCur = readCur;

			tagFoundCount = 0;
			tagRemaining = 0x1F40;

			// Scan all hash buckets for matching entities
			for (iterIdx = 0; iterIdx < 0x10000; iterIdx++) {
				tagEntity = g_World->hashTable[iterIdx];

				while (tagEntity != NULL) {
					if (!CItem_HasTagDefs(tagEntity))
						goto next_tag_entity;

					// Construct CVector and fill with tag defs
					{
						char tagListTypeBuf[0x14254 - 0x14138];
						CVector tmpTagList;
						TagNode **tagIter;
						TagNode *tagDefNode;

						CVector_Constructor(&tmpTagList, tagListTypeBuf);

						CItem_GetTagDefListRaw(tagEntity, &tmpTagList);

						tagIter = (TagNode **)tmpTagList.begin;
						while (tagIter != (TagNode **)tmpTagList.end) {
							tagDefNode = *tagIter;

							// Check type == 1
							if ((int)tagDefNode->type != 1)
								goto next_tag_iter;

							// Check remaining capacity
							if (tagRemaining <= 0)
								goto next_tag_iter;

							// Check name prefix "link"
							{
								const char *tagName = tagDefNode->name;
								if (strncasecmp(tagName, "link", 4) == 0)
									goto next_tag_iter;
							}

							// Get value string
							{
								const char *valStr;
								void *valObj = (void *)tagDefNode->value;
								valStr = TriggerEdit_CString_GetCStr(valObj);

								// Filter by search string if connIndex non-zero
								if ((connIndex & 0xFFFF) != 0) {
									if (strcmp(valStr, tagSearchStr) != 0)
										goto next_tag_iter;
								}

								// Write entity serial
								readCur = writeCur;
								WriteInt32LE(&readCur, (int32_t)tagEntity->serial);
								writeCur = readCur;

								// Copy up to 4 bytes of value string into output
								{
									int cpIdx;
									for (cpIdx = 0; cpIdx < 4; cpIdx++) {
										*(writeCur + cpIdx) = *(valStr + cpIdx);
										if ((int8_t)*(valStr + cpIdx) == 0)
											break;
									}
								}
								writeCur += 4;

								tagRemaining -= 8;
								tagFoundCount++;

								// If connIndex was 0, stop after first match
								if ((connIndex & 0xFFFF) == 0)
									goto next_tag_iter;
							}

next_tag_iter:
							tagIter++;
						}

						CVector_Destructor(&tmpTagList);
					}

next_tag_entity:
					tagEntity = tagEntity->hashNext;
				}
			}

			connIndex = (uint16_t)tagFoundCount;
			goto epilogue;
		}

		case 7: {
			char *opData = readCur + 4;
			TriggerEdit_Op546F(opData);
			return;
		}

		case 5: {
			char *opData = readCur + 4;
			TriggerEdit_SetStringProp(entity, opData);
			return;
		}

		case 4: {
			char *varData = readCur + 4;
			int32_t varSubType = connIndex & 0xFFFF;
			char nameBuf[128];
			char *varCur = varData;

			// Check if first byte is non-zero (has prefix name)
			if ((int8_t)*varCur != 0) {
				strcpy(nameBuf, varCur);
				varCur += strlen(varCur) + 1;
				TriggerEdit_SetStringProp(entity, nameBuf);
			} else {
				varCur++;
			}

			// Copy value name
			strcpy(nameBuf, varCur);
			varCur += strlen(varCur) + 1;

			// Inner switch on varSubType (0-4)
			if (varSubType > 4) {
				// default case - fall through
			} else {
				switch (varSubType) {
				case 2: {
					int32_t locValX, locValY, locValZ;
					char *locCur = varCur;
					CLocation varLoc;

					locValX = ReadInt32LE(&locCur);
					locValY = ReadInt32LE(&locCur);
					locValZ = ReadInt32LE(&locCur);

					CLocation_Init(&varLoc);
					CLocation_Set(&varLoc, (int16_t)locValX, (int16_t)locValY, (int16_t)locValZ);

					CEntity_SetObjVar(entity, nameBuf, 3, (uintptr_t)&varLoc);
					break;
				}
				case 0: {
					int32_t intVal;
					char *intCur = varCur;

					intVal = ReadInt32LE(&intCur);
					CEntity_SetObjVar(entity, nameBuf, 0, (uint32_t)intVal);
					break;
				}
				case 4: {
					int32_t objVal;
					char *objCur = varCur;

					objVal = ReadInt32LE(&objCur);
					CEntity_SetObjVar(entity, nameBuf, 4, (uint32_t)objVal);
					break;
				}
				case 1: {
					CString _v, _n;
					CString_Constructor(&_v, varCur);
					CString_Constructor(&_n, nameBuf);
					ObjVar_SetStr(entity, &_n, 1, (uintptr_t)&_v);
					CString_Destructor(&_v);
					break;
				}
				case 3:
					// fall through to default
					break;
				}
			}

			if ((mode & 0xFF) == 7) {
				uint16_t cpLen = dataLen & 0xFFFF;
				memcpy(writeCur, dataSaved, cpLen);
				writeCur += cpLen;
				goto epilogue;
			}
			return;
		}

		case 2: {
			char *opData = readCur + 4;
			TriggerEdit_Op545E(entity, opData);
			return;
		}

		case 1: {
			char *scriptName = readCur + 4;
			const char *attachResult;

			attachResult = entity != NULL ? Entity_AttachScript(entity, scriptName, 1) : "Entity not found";

			readCur = writeCur;
			WriteInt32LE(&readCur, entitySerial);
			writeCur = readCur;

			if (attachResult != NULL) {
				strcpy(writeCur, attachResult);
				writeCur += strlen(attachResult) + 1;
			}
			goto epilogue;
		}

		case 0: {
			int32_t scriptCount;
			int32_t tagCount;

			readCur = writeCur;

			// Write entity serial
			WriteInt32LE(&readCur, entitySerial);

			// Write entity body type
			{
				int32_t bt = CEntity_GetBodyType(entity) & 0xFFFF;
				WriteInt32LE(&readCur, bt);
			}

			writeCur = readCur + 8; // advance past serial+bt

			// Enumerate scripts
			scriptCount = 0;
			if (CItem_HasScripts(entity)) {
				CVector scriptList;
				char scriptTypeFlag[0x14268 - 0x14224]; // padding
				void **scriptIter;

				CVector_Constructor(&scriptList, scriptTypeFlag);
				CItem_GetScriptListRaw(entity, &scriptList);

				scriptIter = (void **)scriptList.begin;
				while (scriptIter != (void **)scriptList.end) {
					void *scriptEntry = *scriptIter;
					// Get script name: *(*scriptEntry)
					const char *sName = *(const char **)*(void **)scriptEntry;
					uint16_t nameLen = (uint16_t)(strlen(sName) + 1);
					dataLen = nameLen;

					// Copy name to write cursor
					strcpy(writeCur, sName);
					writeCur += nameLen;

					scriptCount++;
					scriptIter++;
				}

				CVector_Destructor(&scriptList);
			}

			WriteInt32LE(&readCur, scriptCount);

			// Enumerate tag definitions
			tagCount = 0;
			if (CItem_HasTagDefs(entity)) {
				CVector tagDefList;
				char tagDefTypeFlag[0x1426c - 0x1423c]; // padding
				TagNode **tagDefIter;

				CVector_Constructor(&tagDefList, tagDefTypeFlag);
				CItem_GetTagDefListRaw(entity, &tagDefList);

				tagDefIter = (TagNode **)tagDefList.begin;
				while (tagDefIter != (TagNode **)tagDefList.end) {
					TagNode *tagDefEntry = *tagDefIter;
					const char *tdName;
					uint16_t tdNameLen;

					// Get tag name
					tdName = tagDefEntry->name;
					tdNameLen = (uint16_t)(strlen(tdName) + 1);
					dataLen = tdNameLen;

					// Copy name to write cursor
					strcpy(writeCur, tdName);
					writeCur += tdNameLen;

					// Write value after name based on type
					{
						char *tagWritePos = writeCur;
						int tagType = (int)tagDefEntry->type;

						switch (tagType) {
						case 0: {
							// Type 0: write 0, then int32 value
							WriteInt32LE(&tagWritePos, 0);
							int32_t intVal = (int32_t)tagDefEntry->value;
							WriteInt32LE(&tagWritePos, intVal);
							writeCur = tagWritePos;
							break;
						}
						case 4: {
							// Type 4: write 4, then int32 value
							WriteInt32LE(&tagWritePos, 4);
							int32_t intVal = (int32_t)tagDefEntry->value;
							WriteInt32LE(&tagWritePos, intVal);
							writeCur = tagWritePos;
							break;
						}
						case 1: {
							// Type 1: write 1, then string value
							const char *strVal;
							void *strObj = (void *)tagDefEntry->value;
							uint16_t strLen;

							WriteInt32LE(&tagWritePos, 1);
							writeCur = tagWritePos;

							strVal = TriggerEdit_CString_GetCStr(strObj);
							strLen = (uint16_t)(strlen(strVal) + 1);
							dataLen = strLen;

							strcpy(writeCur, strVal);
							writeCur += strLen;
							break;
						}
						case 3: {
							// Type 3: write 2, then 3 int16 values (location)
							CLocation *loc = (CLocation *)tagDefEntry->value;

							WriteInt32LE(&tagWritePos, 2);

							// Write x
							{
								int32_t val = (int16_t)loc->x;
								WriteInt32LE(&tagWritePos, val);
							}
							// Write y
							{
								int32_t val = (int16_t)loc->y;
								WriteInt32LE(&tagWritePos, val);
							}
							// Write z
							{
								int32_t val = loc->z;
								WriteInt32LE(&tagWritePos, val);
							}

							writeCur = tagWritePos;
							break;
						}
						default: {
							// Default: write 3
							WriteInt32LE(&tagWritePos, 3);
							writeCur = tagWritePos;
							break;
						}
						}
					}

					tagCount++;
					tagDefIter++;
				}

				CVector_Destructor(&tagDefList);
			}

			WriteInt32LE(&readCur, tagCount);
			goto epilogue;
		}

		default:
			return;
		}
	}

epilogue: {
	int32_t respDataLen = (int32_t)(writeCur - localBuf);

	BuildTriggerEditResponse(responseBuf, mode, connIndex, localBuf, (uint16_t)respDataLen);
	SendToClient((CItem *)this, responseBuf, -1);
}
}

/*
 * 0x004B7616 - createPlaceHolder [527]
 *
 * Creates a placeholder CItem at the same location as entity, copying
 * its bodyType and ObjVar tags. The placeholder is a server-only item
 * used by the spawn/resource bank management system.
 *
 * Steps:
 *   1. CWorld_FindBySerial(serial) - locate entity; return if NULL.
 *   2. TriggerEdit_FindTagDef(entity, "_objectType") - get bodyType; return if NULL.
 *   3. CLocation_SetLoc(savedLoc, entity+0x0A) - save entity location.
 *   4. malloc(0x50) + CItem_Constructor - allocate newItem.
 *   5. CEntity_SetBodyType(newItem, bodyType).
 *   6. CLocation_SetLoc(newItem+0x10, &savedLoc).
 *   7. CItem_Setup(newItem, 0, &savedLoc, 0, 1).
 *   8. Copy entity color word to newItem color.
 *   9. TriggerEdit_FindTagDef(entity, "_scripts") - if non-NULL and value != 0,
 *      parse '+'-delimited script names, call Entity_AttachScript for each.
 *  10. Check CItem_HasScripts + CItem_HasTagDefs; if either nonzero, iterate
 *      tag def list (CItem_GetTagDefListRaw), copying each to newItem.
 *      Tag copy rules (type==1/WTYPE_STRING):
 *        - name starts with "link": skip.
 *        - name starts with "_link": copy with key = name+1.
 *        - name starts with '_' (other): skip.
 *        - otherwise: copy as-is.
 *      Tag copy rules (type!=1):
 *        - name starts with '_': skip.
 *        - otherwise: copy as-is.
 *  11. Set "_objectId" on entity (type 4 = serial of newItem).
 *  12. vtable[0x04] SetLocation on newItem (insert into world).
 *  13. ValidateInWorld(newItem); if 0, newItem = NULL.
 */
void
Script_createPlaceHolder(uint32_t serial)
{
	CItem *entity;
	TagNode *prop;
	uint16_t bodyType;
	CLocation savedLoc;
	void *mem;
	CItem *newItem;
	TagNode *scriptsProp;
	int pos, startPos;
	CVector vec;
	char typeFlag;

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		return;

	prop = TriggerEdit_FindTagDef(entity, "_objectType");
	if (prop == NULL)
		return;

	// bodyType is at prop->value (offset +4 in tag def entry)
	bodyType = (uint16_t)prop->value;

	// Save entity location (entity+0x0A = entity.location)
	CLocation_SetLoc(&savedLoc, &entity->resourceEntity.entity.location);

	// Allocate new CItem: operator new(0x50) + CItem_Constructor
	mem = malloc(sizeof(CItem));
	if (mem != NULL)
		newItem = CItem_Constructor(mem);
	else
		newItem = NULL;
	if (newItem == NULL)
		return;

	// Set body type, location, setup
	CEntity_SetBodyType(newItem, bodyType);
	CLocation_SetLoc((CLocation *)&newItem->resourceEntity.nextInContainer, &savedLoc);
	CItem_Setup(newItem, 0, &savedLoc, 0, 1);

	// Copy color word from entity to newItem
	newItem->resourceEntity.entity.color = entity->resourceEntity.entity.color;

	// Find "_scripts" property; if value != 0, parse '+'-delimited scripts
	scriptsProp = TriggerEdit_FindTagDef(entity, "_scripts");
	if (scriptsProp != NULL && scriptsProp->value != 0) {
		char *scriptsStr = CString_GetCStr((CString *)(uintptr_t)scriptsProp->value);
		pos = 0;
		startPos = 0;
		while (1) {
			if (scriptsStr[pos] == '+') {
				// Null-terminate segment in-place (binary does this)
				scriptsStr[pos] = '\0';
				Entity_AttachScript(newItem, &scriptsStr[startPos], 1);
				startPos = pos + 1;
			} else if (scriptsStr[pos] == '\0') {
				Entity_AttachScript(newItem, &scriptsStr[startPos], 1);
				break;
			}
			pos++;
		}
	}

	// If entity has scripts or tag defs, copy tag defs to newItem
	if (CItem_HasScripts(entity) || CItem_HasTagDefs(entity)) {
		uintptr_t *tagIter;
		typeFlag = 0;
		CVector_Constructor(&vec, &typeFlag);
		CItem_GetTagDefListRaw(entity, &vec);
		for (tagIter = (uintptr_t *)vec.begin; tagIter != (uintptr_t *)vec.end; tagIter++) {
			TagNode *entry = (TagNode *)*tagIter;
			const char *name = entry->name;
			if (entry->type == 1) {
				// WTYPE_STRING: handle "link"/"_link" prefixes
				if (strncmp("link", name, 4) == 0)
					continue;
				if (strncmp("_link", name, 5) != 0)
					goto check_underscore;
				// "_link" matched: copy with name+1
				{
					CString _n;
					CString_Constructor(&_n, name + 1);
					ObjVar_SetStr(newItem, &_n, entry->type, entry->value);
				}
				continue;
			}
check_underscore:
			if (name[0] == '_')
				continue;
			{
				CString _n;
				CString_Constructor(&_n, name);
				ObjVar_SetStr(newItem, &_n, entry->type, entry->value);
			}
		}
		CVector_Destructor(&vec);
	}

	// Set "_objectId" on entity to new item's serial (type 4 = WTYPE_OBJ)
	{
		CString _n;
		CString_Constructor(&_n, "_objectId");
		ObjVar_SetStr(entity, &_n, 4, newItem->serial);
	}

	((void (*)(void *, CLocation *))VT_FN(newItem, VT_DROP_AT_FEET))(newItem, &savedLoc);

	// ValidateInWorld: if 0, newItem becomes NULL (not used after this)
	if (!ValidateInWorld(newItem))
		newItem = NULL;
	USED(newItem);
}

// Set by mobileWillBuy when a bodyType match is found; read by
// CShopkeeper_OpenSellWindow to distinguish "nothing I want" from
// "I can't afford that".
int g_VendorSawMatch;

/*
 * 0x004D3387 - CString::GetCString
 *
 * Returns char* from CString.
 */
static const char *
TriggerEdit_CString_GetCStr(void *cstr)
{
	return *(const char **)cstr;
}

/*
 * 0x004DB130 - BroadcastUnicodeSpeechAtEye
 *
 * Builds a unicode speech packet for entity and broadcasts it from the
 * entity's eye position - its own location raised by half its height -
 * to everyone within 12 tiles with line of sight.
 */
static __attribute__((unused)) void
BroadcastUnicodeSpeechAtEye(CItem *entity, uint8_t speechType, uint16_t *text, uint16_t hue, uint16_t font, uint32_t lang, uint16_t range)
{
	uint8_t obuf[0x830];
	CLocation eyeLoc, delta;

	PacketManager_MakePacket_TEXT_UNICODE(obuf, entity, NULL, speechType, text, hue, font, lang);

	CLocation_Constructor3D(&delta, 0, 0, (int16_t)(((int (*)(void *))VT_FN(entity, VT_GET_HEIGHT))(entity) / 2));
	CLocation_AddWrapped(CEntity_GetLocation(&entity->resourceEntity.entity), &eyeLoc, &delta);

	BroadcastToRangeWithLOS(obuf, &eyeLoc, range, 0xC);
}

/*
 * 0x004DB1C1 - HandlePacket_SPEECH_UNICODE
 *
 * Empty in UoDemo.exe (Unicode speech not implemented in the demo).
 * Packet 0xAD from client (variable-size), two variants:
 *
 * Normal mode (speechType < 0xC0):
 *   [0xAD] [len hi] [len lo] [speechType] [hue] [font]
 *   [lang 4 bytes] [UTF-16BE text, null-terminated]
 *
 * Keyword-encoded mode (speechType & 0xC0, clients 2.0.7+):
 *   [0xAD] [len hi] [len lo] [speechType|0xC0] [hue] [font]
 *   [lang 4 bytes] [12-bit packed keyword data] [ASCII text, null-terminated]
 *   Keyword data: array of 12-bit values packed big-endian nibble order.
 *   First 12-bit value = keyword count (N), followed by N keyword IDs
 *   (indices into speech.mul). Total keyword bytes = ((N+1)*3+1)/2.
 *   Text after keywords is ASCII, not UTF-16.
 *
 * Converts to ASCII and builds a synthetic 0x03 (SPEECH) packet,
 * then delegates to HandlePacket_SPEECH which contains all the
 * speech broadcast, GM command, vendor keyword, and NPC conversation logic.
 */
void
HandlePacket_SPEECH_UNICODE(CPlayer *this, uint8_t *buf)
{
	uint8_t synthBuf[8192];
	uint32_t off;
	uint8_t speechType;
	uint16_t hue, font;
	int textOff;
	char asciiText[241];
	int i, textLen, pktLen;
	uint16_t ch;

	// Parse fields using Get functions (they skip the 3-byte header
	// for variable-size packets via GetSizeLength).
	off = 0;
	GetByte(buf, &off, &speechType); // byte 3: speech type
	GetWord(buf, &off, &hue);        // bytes 4-5: hue
	GetWord(buf, &off, &font);       // bytes 6-7: font

	// Data starts at absolute offset 12 (3-byte header + 5 fields + 4 lang)
	textOff = 12;

	if (speechType & 0xC0) {
		// Keyword-encoded mode: 12-bit packed keyword data before ASCII text.
		// First 12-bit value = keyword count, read from big-endian uint16.
		uint16_t w;
		int count, kwBytes;

		memcpy(&w, &buf[textOff], 2);
		w = (uint16_t)((w >> 8) | (w << 8)); // ntohs
		count = (w & 0xFFF0) >> 4;

		// Total keyword section bytes: (count+1) 12-bit values, packed
		kwBytes = (((count + 1) * 3) / 2) + ((count + 1) % 2);
		textOff += kwBytes;

		// Strip keyword flag to get actual speech type
		speechType &= ~0xC0;

		// Text is null-terminated ASCII
		for (i = 0; i < 240 && buf[textOff + i] != '\0'; i++)
			asciiText[i] = (char)buf[textOff + i];
		asciiText[i] = '\0';
		textLen = i;
	} else {
		// Normal mode: UTF-16BE text at offset 12
		for (i = 0; i < 240; i++) {
			memcpy(&ch, &buf[textOff + i * 2], 2);
			ch = (uint16_t)((ch >> 8) | (ch << 8)); // ntohs
			if (ch == 0)
				break;
			asciiText[i] = (char)(ch & 0xFF);
		}
		asciiText[i] = '\0';
		textLen = i;
	}

	// Build synthetic packet 0x03 (SPEECH) in the format expected by
	// HandlePacket_SPEECH: [0x03] [len_hi] [len_lo] [speechType]
	// [hue_hi] [hue_lo] [font_hi] [font_lo] [ascii text + null]
	pktLen = 8 + textLen + 1;
	synthBuf[0] = 0x03;
	synthBuf[1] = (uint8_t)((pktLen >> 8) & 0xFF);
	synthBuf[2] = (uint8_t)(pktLen & 0xFF);
	synthBuf[3] = speechType;
	synthBuf[4] = (uint8_t)((hue >> 8) & 0xFF);
	synthBuf[5] = (uint8_t)(hue & 0xFF);
	synthBuf[6] = (uint8_t)((font >> 8) & 0xFF);
	synthBuf[7] = (uint8_t)(font & 0xFF);
	memcpy(&synthBuf[8], asciiText, textLen + 1);

	HandlePacket_SPEECH(this, synthBuf);
}

/*
 * 0x004DB1C6 - CWorld::SpeechNotifyNearbyUnicode
 *
 * Filters UCS-2 text down to ASCII (masked to 0x7F) into a 0x1000-byte
 * stack buffer and forwards it to Convo_NotifyNearbyNPCs. The unused
 * argument is always 0 from callers.
 *
 * FIXED: the binary does not NUL-terminate the ASCII buffer before
 * passing it on; we add ascii[count] = '\0'.
 */
void
CWorld_SpeechNotifyNearbyUnicode(CWorld *self, uint32_t serial, CLocation *loc, uint16_t *text, int unused, int range)
{
	char ascii[0x1000];
	int count = 0;
	int i;

	USED(self);
	USED(unused);

	for (i = 0; text[i] != 0; i++) {
		if (isASCII(text[i])) {
			ascii[count] = (char)(text[i] & 0x7F);
			count++;
		}
	}
	ascii[count] = '\0';
	Convo_NotifyNearbyNPCs(serial, loc, ascii, range);
}

/*
 * 0x004DB296 - IsBookGraphic
 *
 * Returns 1 if the graphic is a book type (0x0FEF-0x0FF2).
 */
static int
IsBookGraphic(uint16_t graphic)
{
	return (graphic >= 0x0FEF && graphic <= 0x0FF2);
}

/*
 * 0x00626430 - g_SpellGraphics
 *
 * Spell page art IDs (0x3585-0x3591) for each of the 32 spell slots
 * across the 8 magery circles.
 */
// clang-format off
static const uint16_t g_SpellGraphics[32] = {
	0x3589, 0x3589, 0x3589, 0x3589, 0x3589, 0x3589, 0x3589, 0x3589,
	0x3590, 0x3590, 0x3590, 0x3590, 0x3590, 0x3590, 0x3590, 0x3590,
	0x3586, 0x3586, 0x358D, 0x358D, 0x3588, 0x3588, 0x358F, 0x358F,
	0x3585, 0x3585, 0x358C, 0x358C, 0x358A, 0x3587, 0x3591, 0x358E,
};
// clang-format on

/*
 * 0x00626470 - g_ScrollGraphics
 *
 * Scroll-slot art IDs: 15 entries of 0x3584 followed by 15 of 0x358B.
 */
// clang-format off
static const uint16_t g_ScrollGraphics[30] = {
	0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584,
	0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x3584, 0x358B,
	0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B,
	0x358B, 0x358B, 0x358B, 0x358B, 0x358B, 0x358B,
};
// clang-format on

/*
 * 0x004DB2C1 - DoorOpen
 *
 * Opens a closed door: shifts its location by doorType (0-7),
 * relocates it if the target tile is valid, and plays the open sound
 * for its body range (wooden/metal/stone/gate).
 */
static void
DoorOpen(CItem *door)
{
	CLocation loc;
	uint8_t openFlag;
	uint32_t doorType;
	uint16_t bodyType;

	CLocation_Init(&loc);

	openFlag = CItem_HasStackableFlag(door);
	if ((openFlag & 0xFF) != 0)
		return;

	CLocation_SetLoc(&loc, &door->resourceEntity.entity.location);

	doorType = CWorld_GetItemLayer(door->resourceEntity.entity.bodyType) & 0xFF;

	switch (doorType) {
	case 0:
		CLocation_IncrY(&loc);
		CLocation_DecrX(&loc);
		break;
	case 1:
		CLocation_IncrY(&loc);
		CLocation_IncrX(&loc);
		break;
	case 2:
		CLocation_DecrX(&loc);
		break;
	case 3:
		CLocation_DecrY(&loc);
		CLocation_IncrX(&loc);
		break;
	case 4:
		CLocation_IncrY(&loc);
		CLocation_IncrX(&loc);
		break;
	case 5:
		CLocation_DecrY(&loc);
		CLocation_IncrX(&loc);
		break;
	case 6:
		CLocation_IncrY(&loc);
		CLocation_DecrX(&loc);
		break;
	case 7:
		CLocation_DecrY(&loc);
		break;
	default:
		break;
	}

	if (door->parent != NULL)
		return;
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc.x, (int)(int16_t)loc.y))
		return;

	CItem_SetOpen(door, 1);

	((void (*)(void *))VT_FN(door, VT_HIDE))(door);

	((void (*)(void *, CLocation *))VT_FN(door, VT_DROP_AT_FEET))(door, &loc);

	// Play open sound based on bodyType ranges (0x004DB3F0-0x004DB6A8)
	bodyType = door->resourceEntity.entity.bodyType & 0xFFFF;
	if ((bodyType >= 0x334 && bodyType <= 0x353) || (bodyType >= 0x695 && bodyType <= 0x6B4) || (bodyType >= 0x6D5 && bodyType <= 0x6E4) ||
	        (bodyType >= 0x839 && bodyType <= 0x848) || (bodyType >= 0x866 && bodyType <= 0x875)) {
		PlaySoundAtEntity(door, 0xEA, 0);
		return;
	}
	if ((bodyType >= 0x6B5 && bodyType <= 0x6C4) || (bodyType >= 0x6E5 && bodyType <= 0x6F4)) {
		PlaySoundAtEntity(door, 0xEB, 0);
		return;
	}
	if ((bodyType >= 0x675 && bodyType <= 0x694) || (bodyType >= 0x824 && bodyType <= 0x833) || (bodyType >= 0x84C && bodyType <= 0x85B) ||
	        (bodyType >= 0x1FED && bodyType <= 0x1FFC) || (bodyType >= 0x6C5 && bodyType <= 0x6D4)) {
		PlaySoundAtEntity(door, 0xEC, 0);
		return;
	}
	if ((bodyType >= 0x314 && bodyType <= 0x333) || (bodyType >= 0x354 && bodyType <= 0x365) || (bodyType >= 0x0F4 && bodyType <= 0x0F7)) {
		PlaySoundAtEntity(door, 0xED, 0);
		return;
	}
}

/*
 * 0x004DB6CC - DoorClose
 *
 * Inverse of DoorOpen: relocates the door back to its closed tile and
 * plays the close sound. If a mobile is blocking the closed position,
 * reschedules the close via timer 4 after 0x3C ticks. The binary
 * recomputes CEntity_GetBodyType(door) on every range compare.
 */
void
DoorClose(CItem *door, int unused)
{
	CLocation loc;
	uint8_t openFlag;
	uint32_t doorType;
	int blockIdx;
	CItem *cur;

	USED(unused);

	CLocation_Init(&loc);

	openFlag = CItem_HasStackableFlag(door);
	if ((openFlag & 0xFF) != 1)
		return;

	CLocation_SetLoc(&loc, &door->resourceEntity.entity.location);

	doorType = CItem_GetLayerThiscall(door) & 0xFF;

	switch (doorType) {
	case 0:
		CLocation_DecrY(&loc);
		CLocation_IncrX(&loc);
		break;
	case 1:
		CLocation_DecrY(&loc);
		CLocation_DecrX(&loc);
		break;
	case 2:
		CLocation_IncrX(&loc);
		break;
	case 3:
		CLocation_IncrY(&loc);
		CLocation_DecrX(&loc);
		break;
	case 4:
		CLocation_DecrY(&loc);
		CLocation_DecrX(&loc);
		break;
	case 5:
		CLocation_IncrY(&loc);
		CLocation_DecrX(&loc);
		break;
	case 6:
		CLocation_DecrY(&loc);
		CLocation_IncrX(&loc);
		break;
	case 7:
		CLocation_IncrY(&loc);
		break;
	default:
		break;
	}

	if (door->parent != NULL)
		return;
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc.x, (int)(int16_t)loc.y))
		return;

	// 0x004DB7D5-0x004DB86F: check for blocking mobiles at closed position.
	// Iterates dynamic item chain at the closed position's block.
	// For each item at same x/y/z: checks vtable[0xD0] (IsMobile) and
	// vtable[0x40](0) & 0x40 (impassable movement flag).
	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)(int16_t)loc.x, (int)(int16_t)loc.y, 0);
	for (cur = g_MapBlocks[blockIdx].itemHead; cur != NULL; cur = cur->spatialNext) {
		if ((int16_t)cur->resourceEntity.entity.location.x != (int16_t)loc.x)
			continue;
		if ((int16_t)cur->resourceEntity.entity.location.y != (int16_t)loc.y)
			continue;
		if ((int16_t)cur->resourceEntity.entity.location.z != (int16_t)loc.z)
			continue;
		if (!VT_IsMobile(cur))
			continue;
		if (!(((int (*)(void *, int))VT_FN(cur, VT_GET_SURFACE_FLAGS))(cur, 0) & 0x40))
			continue;
		break;
	}
	if (cur != NULL) {
		ScheduleEvent(0x3C, door->serial, TIMER_EVENT_DOOR_CLOSE, 0, 0);
		return;
	}

	CItem_SetOpen(door, 0);

	((void (*)(void *))VT_FN(door, VT_HIDE))(door);

	((void (*)(void *, CLocation *))VT_FN(door, VT_DROP_AT_FEET))(door, &loc);

	if (((CEntity_GetBodyType(door) & 0xFFFF) >= 0x334 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x353) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x695 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x6B4) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x6D5 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x6E4) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x839 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x848) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x866 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x875)) {
		PlaySoundAtEntity(door, 0xF1, 0);
		return;
	}
	if (((CEntity_GetBodyType(door) & 0xFFFF) >= 0x6B5 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x6C4) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x6E5 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x6F4)) {
		PlaySoundAtEntity(door, 0xF2, 0);
		return;
	}
	if (((CEntity_GetBodyType(door) & 0xFFFF) >= 0x675 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x694) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x824 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x833) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x84C && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x85B) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x1FED && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x1FFC) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x6C5 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x6D4)) {
		PlaySoundAtEntity(door, 0xF3, 0);
		return;
	}
	if (((CEntity_GetBodyType(door) & 0xFFFF) >= 0x314 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x333) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x354 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x365) ||
	        ((CEntity_GetBodyType(door) & 0xFFFF) >= 0x0F4 && (CEntity_GetBodyType(door) & 0xFFFF) <= 0x0F7)) {
		PlaySoundAtEntity(door, 0xF4, 0);
		return;
	}
}

/*
 * 0x004DBB94 - FindPairedDoor
 *
 * Returns the closed, scriptless door tile in the same map block within a
 * Chebyshev distance of 1, or NULL when none qualifies.
 */
static CItem *
FindPairedDoor(CLocation *loc)
{
	int blockIdx;
	MapBlock *block;
	CItem *cur;

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, loc->x, loc->y, 0);

	block = &g_MapBlocks[blockIdx];

	for (cur = block->itemHead; cur != NULL; cur = cur->spatialNext) {
		if (CLocation_ChebyshevDistance(loc, &cur->resourceEntity.entity.location) != 1)
			continue;
		if (!(CWorld_GetItemTileFlags(cur->resourceEntity.entity.bodyType) & TF_DOOR))
			continue;
		if (CItem_HasStackableFlag(cur))
			continue;
		if (CItem_HasScripts(cur))
			continue;
		return cur;
	}

	return NULL;
}

/*
 * 0x004DBC41 - UseDoor
 *
 * Toggles a door. When closed, opens both this door and any paired neighbor
 * and schedules a TIMER_EVENT_DOOR_CLOSE for each. When already open,
 * closes it.
 */
void
UseDoor(CItem *door)
{
	CItem *paired;

	if (!CItem_HasStackableFlag(door)) {
		// Door is closed - open it
		paired = FindPairedDoor(&door->resourceEntity.entity.location);

		DoorOpen(door);
		ScheduleEvent(0x3C, door->serial, TIMER_EVENT_DOOR_CLOSE, 0, 0);

		if (paired != NULL) {
			DoorOpen(paired);
			ScheduleEvent(0x3C, paired->serial, TIMER_EVENT_DOOR_CLOSE, 0, 0);
		}
	} else {
		// Door is open - close it
		DoorClose(door, 0);
	}
}

/*
 * 0x004DBCC3 - CastToContainer
 *
 * Promotes a plain CItem into a CContainer in place, copying its
 * properties onto the new object and replacing the caller's pointer.
 */
static void
CastToContainer(CItem **itemPtr)
{
	CItem *item;
	CContainer *newCont;
	CContainer *tmp;
	CLocation savedLoc;
	uint32_t savedIsLoading;
	uint32_t savedSerial;
	void *mem;

	item = *itemPtr;

	CLocation_SetLoc(&savedLoc, ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item));

	savedIsLoading = g_World->isLoading;
	g_World->isLoading = 1;

	mem = OperatorNew(sizeof(CContainer));
	if (mem != NULL) {
		CContainer_Constructor((CContainer *)mem);
		tmp = (CContainer *)mem;
	} else {
		tmp = NULL;
	}
	newCont = tmp;

	g_World->isLoading = 0;

	savedSerial = item->serial;

	CEntity_SetBodyType(&newCont->item, CEntity_GetBodyType(item));

	// Copy CResourceEntity location (offset 0x10)
	CLocation_SetLoc((CLocation *)&newCont->item.resourceEntity.nextInContainer, (CLocation *)&item->resourceEntity.nextInContainer);

	// if found copy home to original item via CItem_SetHome
	{
		CLocation tmpLoc;
		CLocation_Init(&tmpLoc);
		if (CItem_GetHomeLocation(&newCont->item, &tmpLoc))
			CItem_SetHome(item, &tmpLoc);
	}

	newCont->item.resourceEntity.entity.color = item->resourceEntity.entity.color;

	newCont->item.itemFlags = item->itemFlags;

	newCont->item.tracking = item->tracking;
	item->tracking = NULL;

	CItem_SetDecayCount(&newCont->item, CItem_GetDecayCount(item));

	CResourceEntity_TransferAllResources(&newCont->item, item);

	((void (*)(void *, CLocation *))VT_FN(&newCont->item, VT_SET_LOCATION))(&newCont->item, &savedLoc);

	((void (*)(void *))VT_FN(item, VT_DETACH_SPATIAL))(item);

	g_WorldActive2 = 0;

	if (*itemPtr != NULL)
		((void (*)(void *))VT_FN(*itemPtr, VT_DELETE))(*itemPtr);

	g_WorldActive2 = 1;

	*itemPtr = &newCont->item;

	((void (*)(void *, uint32_t))VT_FN(&newCont->item, VT_SET_SERIAL))(&newCont->item, savedSerial);

	USED(savedIsLoading);
}

/*
 * 0x004DBE7B - UseSpellbook
 *
 * Opens game board or checkers board: if container is empty, populates
 * with 32 items. Checks "isCheckers" tag (0x004DBEDA) to select checker
 * piece graphics vs spell page graphics. Binary has two separate loops
 * (one for checkers, one for spells). Each created item gets location
 * (-1,-1,0), AddToContainer via vtable[0xB4], and "lockedContainer"
 * ObjVar via ObjVar_SetStr.
 */
static void
UseSpellbook(CPlayer *player, CItem *spellbook)
{
	CContainer *cont;
	uint16_t gumpType;
	CLocation loc;
	uint32_t contSerial;
	int i;

	if (!VT_IsMobile2(spellbook))
		CastToContainer(&spellbook);

	cont = (CContainer *)spellbook;

	if (cont->contents == NULL) {
		CLocation_Init(&loc);
		contSerial = cont->item.serial;

		if (CResourceEntity_HasTag(spellbook, "isCheckers", 0)) {
			// Checkers loop (32 items from g_CheckerGraphics)
			for (i = 0; i < 32; i++) {
				CItem *item;
				void *mem;

				CLocation_Set(&loc, (int16_t)0xFFFF, (int16_t)0xFFFF, 0);

				mem = OperatorNew(sizeof(CItem));
				if (mem != NULL)
					item = CItem_Constructor(mem);
				else
					item = NULL;

				CEntity_SetBodyType(item, g_CheckerGraphics[i]);

				((void (*)(void *, void *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, cont, &loc);

				{
					CString _lc;
					CString_Constructor(&_lc, "lockedContainer");
					ObjVar_SetStr(item, &_lc, 0, contSerial);
				}
			}
		} else {
			// Spells loop (32 items from g_SpellGraphics)
			for (i = 0; i < 32; i++) {
				CItem *item;
				void *mem;

				CLocation_Set(&loc, (int16_t)0xFFFF, (int16_t)0xFFFF, 0);

				mem = OperatorNew(sizeof(CItem));
				if (mem != NULL)
					item = CItem_Constructor(mem);
				else
					item = NULL;

				CEntity_SetBodyType(item, g_SpellGraphics[i]);

				((void (*)(void *, void *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, cont, &loc);

				{
					CString _lc;
					CString_Constructor(&_lc, "lockedContainer");
					ObjVar_SetStr(item, &_lc, 0, contSerial);
				}
			}
		}
	}

	gumpType = CItem_GetContainerGump((CItem *)spellbook);
	SendOpenGump(player, player->mobile.container.item.serial, spellbook->serial, gumpType);

	CContainer_SendContainerContents(cont, (CItem *)player, player->mobile.container.item.serial, 0);
}

/*
 * 0x004DC0BC - UseScrollCase
 *
 * Opens a scroll case, populating it with 30 scroll items on first
 * use (same shape as UseSpellbook).
 */
static void
UseScrollCase(CPlayer *player, CItem *scrollcase)
{
	CContainer *cont;
	uint16_t gumpType;
	CLocation loc;
	uint32_t contSerial;
	int i;

	if (!VT_IsMobile2(scrollcase))
		CastToContainer(&scrollcase);

	cont = (CContainer *)scrollcase;

	if (cont->contents == NULL) {
		CLocation_Init(&loc);
		contSerial = cont->item.serial;

		for (i = 0; i < 30; i++) {
			CItem *item;
			void *mem;

			CLocation_Set(&loc, (int16_t)0xFFFF, (int16_t)0xFFFF, 0);

			mem = OperatorNew(sizeof(CItem));
			if (mem != NULL)
				item = CItem_Constructor(mem);
			else
				item = NULL;

			CEntity_SetBodyType(item, g_ScrollGraphics[i]);

			((void (*)(void *, void *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, cont, &loc);

			{
				CString _lc;
				CString_Constructor(&_lc, "lockedContainer");
				ObjVar_SetStr(item, &_lc, 0, contSerial);
			}
		}
	}

	gumpType = CItem_GetContainerGump(scrollcase);
	SendOpenGump(player, player->mobile.container.item.serial, scrollcase->serial, gumpType);
	CContainer_SendContainerContents(cont, (CItem *)player, player->mobile.container.item.serial, 0);
}

/*
 * 0x004DC220 - DispatchDoubleClickMobile
 *
 * Handles double-click on a mobile entity. If target has a parent (is
 * contained), returns immediately. If target is self or bodyType > 0x18F
 * (player body): dismount check or open paperdoll. If target is a
 * rideable NPC (bodyType <= 0x18F, not a player): check fighting status,
 * 2D distance, then mount.
 */
static void
DispatchDoubleClickMobile(CPlayer *player, uint32_t serial, CLocation *loc, CItem *entity, int isPaperdoll)
{
	CLocation entityLoc;

	USED(loc);

	if (entity->parent != NULL)
		return;

	CLocation_SetLoc(&entityLoc, CEntity_GetLocation(&entity->resourceEntity.entity));

	if (entity == (CItem *)player || (CResourceEntity_GetBodyType(entity) & 0xFFFF) > 0x18F) {
		if (player != NULL && (CItem *)player == entity && CMobile_IsMounted(&player->mobile) && !isPaperdoll) {
			CMobile_Dismount(&player->mobile);
		} else {
			OpenPaperdoll(player, serial, entity);
		}
		return;
	}

	if (player == NULL)
		return;

	if (!CMobile_IsRideable((CMobile *)entity))
		return;

	if (VT_IsPlayer(entity))
		return;

	if (((CMobile *)entity)->combatTargetList.count != 0 || ((CMobile *)entity)->attackerList.count != 0) {
		CPlayer_SystemMessage(player, "You can't get on a horse that's fighting.");
		return;
	}

	if (Location_DistanceTo2D(CEntity_GetLocation(&player->mobile.container.item.resourceEntity.entity), CEntity_GetLocation(&entity->resourceEntity.entity)) >= 2) {
		CPlayer_SystemMessage(player, "That is too far away to ride.");
		return;
	}

	CMobile_Mount(&player->mobile, (CMobile *)entity);
}

/*
 * 0x004DC350 - UseLight
 *
 * MODIFIED: binary is a no-op stub. We toggle the light source art
 * ID between lit and unlit states using the Script_setType pattern
 * (VT_DETACH_SPATIAL, SetBodyType, VT_RETURN_TO_TRACKED).
 *
 * The HasScripts guard below exists for one specific reason: lit
 * lanterns (0x0A22, 0x0A15, 0x0A1A) appear as orphans in dynamic0.mul
 * - 22 instances placed pre-lit in inns and rooms - and are NOT in
 * scripts/objscr.txt. We can't add them: torch.m's Q532 (trigger
 * creation) would set fuel=100 and schedule the decay callback, and
 * 50 real-world minutes after every server start Q49C would setType
 * each one to its unlit form. Decorations extinguishing themselves
 * is not what we want.
 *
 * So lanterns stay in C's LightToggleLookup (0x0A25<->0x0A22 etc.)
 * to handle the pre-lit orphans. But the *unlit* lantern IDs ARE in
 * objscr.txt (2584/2589/2597), so a scripted unlit lantern's click
 * runs Q659 which setType's the bodyType to lit and the script
 * transfers; without this guard, the dispatch would fall through to
 * UseLight, find the now-lit bodyType in pairs, and toggle it back -
 * a double-flip with no visible change.
 *
 * Skip when any script is attached - the script is the authoritative
 * use-handler. The C path then covers items the script never
 * touches (wall torches, sconces, lamp posts, lit-frame variants)
 * and the lit-lantern orphans described above.
 */
#if 0
// Original binary no-op stub (COMPLETED)
static void
UseLight(CItem *entity)
{
	USED(entity);
}
#else
static void
UseLight(CItem *entity)
{
	uint16_t graphic;
	uint16_t toggled;

	if (entity->tagList != NULL && CTagListManager_HasScripts(entity->tagList))
		return;

	graphic = entity->resourceEntity.entity.bodyType;
	toggled = LightToggleLookup(graphic);
	if (toggled == 0)
		return;

	((void (*)(void *))VT_FN(entity, VT_DETACH_SPATIAL))(entity);
	CEntity_SetBodyType(entity, toggled);
	((void (*)(void *))VT_FN(entity, VT_RETURN_TO_TRACKED))(entity);
}
#endif

/*
 * 0x004DC355 - SendOpenGump
 *
 * Builds OPEN_GUMP packet and sends via null-guarded SendToClient.
 */
void
SendOpenGump(CPlayer *player, uint32_t playerSerial, uint32_t containerSerial, uint16_t gumpType)
{
	uint8_t buf[8];

	PacketManager_MakePacket_OPEN_GUMP(buf, containerSerial, gumpType);
	Entity_BroadcastPacket((CItem *)player, playerSerial, buf);
}

/*
 * 0x004DC388 - OpenPaperdoll
 *
 * Constructs paperdoll title and sends OPEN_PAPERDOLL packet.
 * Binary calls vtable[0x204] GetPaperdollTitle on target to populate
 * a CString with the title.
 *
 * If target != self AND target IsPlayer AND player is not editing AND
 * target fameLevel < 3: truncates title at last comma. Otherwise uses
 * full title.
 */
void
OpenPaperdoll(CPlayer *player, uint32_t playerSerial, CItem *target)
{
	CString title;
	char buf[60];
	int i;
	uint8_t obuf[68];

	CString_DefaultConstructor(&title);

	((void (*)(void *, CString *))VT_FN(target, VT_PAPERDOLL_TITLE))(target, &title);

	if (target == (CItem *)player || !VT_IsPlayer(target) || (player != NULL && CPlayer_IsEditing(player)) || CMobile_GetFameLevel((CMobile *)target) >= 3) {
		memset(buf, 0, 60);
		strncpy(buf, CString_GetBuffer(&title), 59);
		buf[59] = '\0';
	} else {
		// Truncate at last comma for low-fame players
		strncpy(buf, CString_GetBuffer(&title), 59);
		buf[59] = '\0';
		i = (int)strlen(buf) - 1;
		while (i > 0 && buf[i] != ',')
			i--;
		memset(buf + i, 0, 60 - i);
	}

	PacketManager_MakePacket_OPEN_PAPERDOLL(obuf, target->serial, buf, ((CMobile *)target)->combatByte1);
	Entity_BroadcastPacket((CItem *)player, playerSerial, obuf);
	CString_Destructor(&title);
}

/*
 * 0x004DC511 - OpenSpellbook
 *
 * Opens the spellbook gump (0xFFFF) when the book is on the player or
 * one level inside their pack; otherwise sends a "must be in your
 * backpack" error.
 */
static void
OpenSpellbook(CPlayer *player, uint32_t playerSerial, CItem *spellbook)
{
	int found;
	CItem *parent;

	found = 0;

	parent = spellbook->parent;
	if (parent == NULL) {
		// No parent - spellbook is in world
		found = 1;
	} else {
		if (VT_IsMobile(parent)) {
			if (parent == (CItem *)player)
				found = 1;
		} else {
			if (parent->parent != NULL) {
				if (VT_IsMobile(parent->parent)) {
					if (parent->parent == (CItem *)player) {
						found = 1;
						((void (*)(void *, CItem *, int))VT_FN(parent, VT_SEND_ENTITY_UPDATE))(parent, (CItem *)player, 0);
					}
				}
			}
		}
	}

	if (!found) {
		CPlayer_SystemMessage(player, "The spellbook must be in your backpack (and not in a container within) to open.");
		return;
	}

	((void (*)(void *, CItem *, int))VT_FN(spellbook, VT_SEND_ENTITY_UPDATE))(spellbook, (CItem *)player, 0);

	SendOpenGump(player, playerSerial, spellbook->serial, 0xFFFF);
	CContainer_SendContainerContents((CContainer *)spellbook, (CItem *)player, playerSerial, 0);
}

/*
 * 0x004DC611 - DoDoubleClick
 *
 * Main item/mobile usage dispatcher, called from DispatchDoubleClick
 * for live, non-busy players. Routes the action by entity type:
 * signpost / map, writable book, mobile, spellbook, book, game board
 * (0xFA6), scroll case, container (with snoop check), or the default
 * UseItem fallback.
 */
static void
DoDoubleClick(CPlayer *this, uint32_t playerSerial, CLocation *playerLoc, CItem *entity, int isPaperdoll)
{
	uint32_t serial;
	CItem *parentItem;
	CItem *tmpEntity;
	int dist;
	CLocation entityLoc;
	CItem *tmpItem;
	CPlayer *ownerPlayer;
	int snoopDifficulty;
	CMobile *snoopTarget;
	CTradeSession *securedContainer;
	CItem *sendTarget;

	if (entity == NULL)
		return;

	if (this == NULL) {
		tmpEntity = CWorld_FindBySerial(g_World, playerSerial);
		if (tmpEntity == NULL)
			return;
		if (!VT_IsPlayer(tmpEntity))
			return;
		this = (CPlayer *)tmpEntity;
	}

	if (!Entity_ExecuteEvent((CEntity *)entity, 0x33, (uintptr_t)((CItem *)this)->serial))
		return;

	serial = entity->serial;
	parentItem = NULL;

	if (CPlayer_IsEditing(this))
		goto event_0x17;

	CLocation_SetLoc(&entityLoc, ((CLocation * (*)(void *)) VT_FN(entity, VT_GET_LOCATION))(entity));

	dist = CLocation_ChebyshevDistance(&entityLoc, playerLoc);

	if (VT_IsMobile(entity)) {
		if (!CMobile_IsRideable((CMobile *)entity)) {
			// Non-rideable mobile: max distance 18
			if (dist > 0x12) {
				Entity_SendSystemMessage((CItem *)this, playerSerial, "I can't reach that.");
				return;
			}
			goto parent_walk;
		}
		// Rideable mobile: fall through to dead/distance/Z/LOS checks
	}

	if (VT_IsDead((CItem *)this)) {
		Entity_SendSystemMessage((CItem *)this, ((CItem *)this)->serial, "I am dead and cannot do that.");
		return;
	}

	if (dist > 2)
		goto cant_reach;

	if ((int)entityLoc.z > (int)playerLoc->z + 0x10)
		goto cant_reach;

	{
		int topZ;
		topZ = (int)entityLoc.z;
		topZ += ((int (*)(void *))VT_FN(entity, VT_GET_HEIGHT))(entity);
		if (topZ < (int)playerLoc->z - 8)
			goto cant_reach;
	}

	if (CEntity_CanSee((CItem *)this, entity, 1))
		goto parent_walk;

cant_reach:
	// 0x004dc780
	Entity_SendSystemMessage((CItem *)this, playerSerial, "I can't reach that.");
	return;

parent_walk:
	parentItem = entity->parent;
	if (parentItem != NULL) {
		while (parentItem->parent != NULL)
			parentItem = parentItem->parent;
	}

	if (VT_IsMobile2(entity)) {
		if (!((int (*)(void *))VT_FN(entity, VT_EXCLUDED_AMOUNT))(entity))
			goto event_0x3d_8;
	}

	if (parentItem == NULL)
		goto event_0x3d_8;
	if (!VT_IsMobile(parentItem))
		goto event_0x3d_8;
	if (parentItem->serial == playerSerial)
		goto event_0x3d_8;

	if (Entity_ExecuteEvent((CEntity *)parentItem, 0x3D, 9, (int)CMobile_GetSerial((CMobile *)this), (int)CMobile_GetSerial((CMobile *)entity)))
		return;

	if (!World_ValidateEntity(entity, serial))
		return;

event_0x3d_8:
	if (parentItem != NULL) {
		if (!Entity_ExecuteEvent((CEntity *)parentItem, 0x3D, 8, (int)CMobile_GetSerial((CMobile *)this), (int)CMobile_GetSerial((CMobile *)entity)))
			return;
		if (!World_ValidateEntity(entity, serial))
			return;
	}

event_0x17:
	if (!Entity_ExecuteEvent((CEntity *)entity, 0x17, (uintptr_t)((CItem *)this)->serial))
		return;

	if (!World_ValidateEntity(entity, serial))
		return;

	// Type dispatch

	if (VT_IsSpatial(entity)) {
		tmpItem = entity;
		if (((CSignpost *)tmpItem)->mapExtent[4] != 0) {
			CSignpost_SendMapDisplay((CSignpost *)tmpItem, (CItem *)this, playerSerial);
			CSignpost_SendMapCommand((CSignpost *)tmpItem, (CItem *)this, playerSerial);
		} else {
			((void (*)(CItem *, CItem *, uint32_t, char *))VT_FN(entity, VT_SAY_TO_ENTITY))(entity, (CItem *)this, ((CItem *)this)->serial, "It appears to be blank.");
		}
		return;
	}

	if (((int (*)(void *))VT_FN(entity, VT_CHECK_DC))(entity)) {
		OpenWritableBook(entity, (CItem *)this, playerSerial);
		return;
	}

	if (VT_IsMobile(entity)) {
		DispatchDoubleClickMobile(this, playerSerial, playerLoc, entity, isPaperdoll);
		return;
	}

	if (((int (*)(void *))VT_FN(entity, VT_EXCLUDED_AMOUNT))(entity)) {
		OpenSpellbook(this, playerSerial, entity);
		return;
	}

	if (IsBookGraphic(CEntity_GetBodyType(entity) & 0xFFFF)) {
		BookContent_open((CItem *)this, entity);
		return;
	}

	if ((CEntity_GetBodyType(entity) & 0xFFFF) == 0xFA6) {
		UseSpellbook(this, entity);
		return;
	}

	if ((CEntity_GetBodyType(entity) & 0xFFFF) == 0xFAD || (CEntity_GetBodyType(entity) & 0xFFFF) == 0xE1C) {
		UseScrollCase(this, entity);
		return;
	}

	if (!VT_IsMobile2(entity)) {
		UseItem_Default(this, entity);
		return;
	}

	// Container in another mob's pack - snooping
	if (parentItem != NULL && VT_IsMobile(parentItem) && parentItem != (CItem *)this && !CPlayer_IsEditing(this)) {
		snoopTarget = (CMobile *)parentItem;
		ownerPlayer = NULL;

		if (VT_IsPlayer((CItem *)snoopTarget))
			ownerPlayer = (CPlayer *)snoopTarget;

		if (ownerPlayer != NULL && CPlayer_IsEditing(ownerPlayer)) {
			if (CPlayer_IsGameMaster(ownerPlayer))
				CPlayer_SystemMessage(this, "You can not peek into the container.");
			else
				CPlayer_SystemMessage(this, "You failed to peek into the container.");
			return;
		}

		CMobile_ChangeKarma(&this->mobile, -500);

		snoopDifficulty = (1000 - CMobile_GetSkillValue(&this->mobile, 0x1C, 0)) / 50;

		ProcessCrimeWitness(playerLoc, &this->mobile, snoopTarget, "", snoopDifficulty, 0, 2);

		if (CMobile_DirectUse(&this->mobile, 0x1C) < 0) {
			CPlayer_SystemMessage(this, "You failed to peek into the container.");
			return;
		}

		this->mobile.actionState = 2;
	}

	SendOpenGump(this, playerSerial, entity->serial, CItem_GetContainerGump(entity));
	CContainer_SendContainerContents((CContainer *)entity, (CItem *)this, playerSerial, 0);

	if (CItem_HasSecuredAncestor(entity)) {
		securedContainer = CItem_FindSecuredContainer(entity);
		if ((CItem *)securedContainer->player1 == (CItem *)this)
			sendTarget = (CItem *)securedContainer->player2;
		else
			sendTarget = (CItem *)securedContainer->player1;

		SendOpenGump((CPlayer *)sendTarget, sendTarget->serial, entity->serial, CItem_GetContainerGump(entity));
		CContainer_SendContainerContents((CContainer *)entity, sendTarget, sendTarget->serial, 0);
	}
}

/*
 * 0x004DCC42 - UseItem_Default
 *
 * Fallback for items with no special type in the UseItem dispatch.
 * Looks up tile data flags for the item's graphic:
 *   TF_LIGHT (0x00800000) - calls UseLight(entity)
 *   TF_DOOR  (0x20000000) - calls UseDoor(entity)
 * Otherwise: dead code reads graphic again (result unused).
 */
void
UseItem_Default(CPlayer *player, CItem *entity)
{
	uint16_t graphic;
	uint32_t tileFlags;

	graphic = CEntity_GetBodyType(entity) & 0xFFFF;
	tileFlags = g_ItemTileData[graphic].flags;

	if (tileFlags & TF_LIGHT || (feat(FEAT_LIGHTS) && LightToggleLookup(graphic))) {
		UseLight(entity);
		return;
	}

	if (tileFlags & TF_DOOR) {
		UseDoor(entity);
		return;
	}

	// Dead code: if player != NULL, reads graphic again (result unused)
	if (player != NULL) {
		graphic = CEntity_GetBodyType(entity);
		USED(graphic);
	}
}

/*
 * 0x004DCCB5 - UseItemDispatch
 *
 * Script-callable UseItem path. Fires UseObject (0x33) and UseItem (0x17)
 * events on the entity, then falls back to UseItem_Default for tile flags.
 * Called from Script_useItem (0x0041A30F).
 */
void
UseItemDispatch(CItem *player, CItem *entity)
{
	if (entity == NULL)
		return;

	// Fire UseObject event (0x33)
	if (!Entity_ExecuteEvent(&entity->resourceEntity.entity, UseObject, (uintptr_t)player->serial))
		return;

	// Fire UseItem event (0x17)
	if (!Entity_ExecuteEvent(&entity->resourceEntity.entity, UseItem, (uintptr_t)player->serial))
		return;

	// Fallback: UseItem_Default (tile flags: door/light)
	UseItem_Default((CPlayer *)player, entity);
}

/*
 * 0x004DCD04 - DispatchDoubleClick
 *
 * Top-level double-click dispatcher. If dead, delegates to DeadDoubleClick.
 * If busy, returns. Otherwise calls UseItem and sets actionState = 4.
 */
void
DispatchDoubleClick(CPlayer *this, CItem *entity, int isPaperdoll)
{
	if (CPlayer_HasDeadFlag(this)) {
		DeadDoubleClick(this, entity);
		return;
	}

	if (CPlayer_IsBusy(this))
		return;

	DoDoubleClick(this, this->mobile.container.item.serial, &this->mobile.container.item.resourceEntity.entity.location, entity, isPaperdoll);

	this->mobile.actionState = 4;
}

/*
 * 0x004DCD62 - DeadDoubleClick
 *
 * Double-click handler for dead players. Two blocks:
 * Block 1: If player is counselor, entity is a mobile, and dist < 18 -
 *   open paperdoll on entity.
 * Block 2: If entity is a player who is a counselor, and dist < 18 -
 *   open paperdoll on entity.
 */
static void
DeadDoubleClick(CPlayer *this, CItem *entity)
{
	int dist = 0;

	if (this != NULL && entity != NULL) {
		CLocation *entityLoc = CItem_GetLocationVT(entity);
		CLocation *playerLoc = CItem_GetLocationVT(&this->mobile.container.item);
		dist = CLocation_ChebyshevDistance(playerLoc, entityLoc);
	}

	if (this != NULL && CPlayer_IsCounselor(this) && dist < 0x12 && entity != NULL && VT_IsMobile(entity)) {
		OpenPaperdoll(this, CMobile_GetSerial(&this->mobile), entity);
		this->mobile.actionState = 2;
	}

	if (this != NULL && entity != NULL && VT_IsPlayer(entity)) {
		CPlayer *target = (CPlayer *)entity;
		if (dist < 0x12 && CPlayer_IsCounselor(target)) {
			OpenPaperdoll(this, CMobile_GetSerial(&this->mobile), (CItem *)target);
			this->mobile.actionState = 2;
		}
	}
}

/*
 * Game connection POSTLOGIN handler for non-demo clients (1.25.30+).
 *
 * Enables Huffman compression, sends character list. The client
 * responds with PRELOGIN (0x5D) to select a character or LOGIN (0x00)
 * to create one.
 *
 * FEATURES (0xB9) is only sent for clients >= 1.25.35 - the packet
 * does not exist in client 1.25.32's packet table (ends at 0xB2).
 */
void
HandlePacket_POSTLOGIN(CUserSock *this, uint8_t *buf)
{
	uint8_t obuf[bufSize];
	uint32_t off;
	char *characterName;
	char *characterPassword;
	uint32_t encryptionKey;

	memset(obuf, 0, sizeof(obuf));

	USED(this);

	off = 0;
	GetDWord(buf, &off, &encryptionKey);
	GetString(buf, &off, &characterName, 30);
	GetString(buf, &off, &characterPassword, 30);

	USED(encryptionKey);

	char characterNames[150];
	char characterPasswords[150];
	int numCharacters;

	memset(characterNames, 0, sizeof(characterNames));
	memset(characterPasswords, 0, sizeof(characterPasswords));

	// Custom: account lookup. The client already authenticated via 0x80
	// on the login connection. The game connection (0x91) sends the
	// account name but NOT the password - the relay key from 0x8C
	// provides implicit authentication.
	CAccount *acct = Account_FindByLogin(characterName);
	if (acct == NULL) {
		// 1.25.30 does not Huffman-decompress game connection data.
		if (Version_GetConnVer(this, CLIENT_12600) > CLIENT_12530)
			this->socket.comp = 1;
		if (Version_GetConnVer(this, CLIENT_12600) >= CLIENT_12535) {
			PacketManager_MakePacket_FEATURES(obuf);
			Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		}
		PacketManager_MakePacket_CITIES_AND_CHARS(obuf, &characterNames[0], &characterPasswords[0]);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
		return;
	}
	this->account = acct;

	numCharacters = 0;
	{
		CVector charVec;
		char typeFlag = '\x01';
		uint32_t i, count;
		CVector_Constructor(&charVec, &typeFlag);
		CPlayerList_CollectByAccountIDSorted(&charVec, acct->accountNum);
		count = CVector_GetCount(&charVec);
		for (i = 0; i < count && numCharacters < 5; i++) {
			CPlayer *p = (CPlayer *)((uintptr_t *)charVec.begin)[i];
			if (p->mobile.name != NULL) {
				strncpy(&characterNames[numCharacters * 30], p->mobile.name, 29);
				characterNames[numCharacters * 30 + 29] = '\0';
				strncpy(&characterPasswords[numCharacters * 30], p->password, 29);
				characterPasswords[numCharacters * 30 + 29] = '\0';
				numCharacters++;
			}
		}
		CVector_Destructor(&charVec);
	}

	// 1.25.30 does not Huffman-decompress game connection data.
	if (Version_GetConnVer(this, CLIENT_12600) > CLIENT_12530)
		this->socket.comp = 1;

	if (Version_GetConnVer(this, CLIENT_12600) >= CLIENT_12535) {
		PacketManager_MakePacket_FEATURES(obuf);
		Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
	}

	PacketManager_MakePacket_CITIES_AND_CHARS(obuf, &characterNames[0], &characterPasswords[0]);
	Socket_Copy_To_CSocketBuffer(&this->socket, &obuf[0], -1);
}

/*
 * Helpers for HandlePacket_TriggerEdit.
 */

/*
 * Custom - LightToggleLookup
 *
 * Returns the toggled art ID for a light source, or 0 if not found.
 * Canonical pairs are bidirectional: clicking either side toggles to
 * the other. Lit-frame variants (additional flickering frames in UO
 * art for the same light) toggle to the unlit side of their canonical
 * pair; the canonical lit graphic is used when relighting.
 *
 * The 0x0A28 <-> 0x0A0F pair is deliberately omitted: it conflicts
 * with the new-player candle, where a setBodyType-only flip from the
 * equippable 0x0A0F to the stackable 0x0A28 leaves the resulting
 * item with stack quantity 0 and the move/pickup path refuses it.
 * The vendor-stack flow (0x0A28 -> Q659 -> new 0x0A0F) is handled
 * entirely by torch.m and doesn't need a C entry.
 */
static uint16_t
LightToggleLookup(uint16_t graphic)
{
	static const uint16_t pairs[][2] = {
		{ 0x0A25, 0x0A22 },  // lantern
		{ 0x0A18, 0x0A15 },  // lantern (variant)
		{ 0x0A1D, 0x0A1A },  // hanging lantern
		{ 0x0A0A, 0x0A07 },  // wall torch (east)
		{ 0x0A0C, 0x0A09 },  // wall torch (south)
		{ 0x09FC, 0x09FD },  // wall sconce
		{ 0x0A00, 0x0A02 },  // wall sconce (variant)
		{ 0x0B21, 0x0B20 },  // lamp post 1
		{ 0x0B23, 0x0B22 },  // lamp post 2
		{ 0x0B25, 0x0B24 },  // lamp post 3
	};
	static const uint16_t lit_variants[][2] = {
		{ 0x0B1B, 0x0B1A },  // candle
		{ 0x0B1C, 0x0B1A },  // candle
		{ 0x0B1E, 0x0B1D },  // small candelabra
		{ 0x0B1F, 0x0B1D },  // small candelabra
		{ 0x0B27, 0x0B26 },  // large candelabra
		{ 0x0B28, 0x0B26 },  // large candelabra
		{ 0x0A23, 0x0A22 },  // lantern
		{ 0x0A24, 0x0A22 },  // lantern
		{ 0x0A16, 0x0A15 },  // lantern (variant)
		{ 0x0A17, 0x0A15 },  // lantern (variant)
		{ 0x0A1B, 0x0A1A },  // hanging lantern
		{ 0x0A1C, 0x0A1A },  // hanging lantern
		{ 0x0A08, 0x0A07 },  // wall torch (east)
		{ 0x0A0D, 0x0A09 },  // wall torch (south)
		{ 0x0A0E, 0x0A09 },  // wall torch (south)
		{ 0x09FB, 0x09FD },  // wall sconce
		{ 0x09FE, 0x09FD },  // wall sconce
		{ 0x09FF, 0x09FD },  // wall sconce
	};
	int i, j;

	for (i = 0; i < (int)nelem(pairs); i++) {
		if (graphic == pairs[i][0])
			return pairs[i][1];
		if (graphic == pairs[i][1])
			return pairs[i][0];
	}
	for (i = 0; i < (int)nelem(lit_variants); i++) {
		if (graphic != lit_variants[i][0])
			continue;
		for (j = 0; j < (int)nelem(pairs); j++) {
			if (pairs[j][1] == lit_variants[i][1])
				return pairs[j][0];
		}
	}
	return 0;
}

/*
 * Helper - Vendor_SayTo
 *
 * Vendor NPC says a message to a specific player (binary: vtable[0x6C]).
 */
void
Vendor_SayTo(CPlayer *player, CMobile *vendor, char *text)
{
	uint8_t sbuf[512];
	PacketManager_MakePacket_TEXT(sbuf, (CItem *)&vendor->container.item, (CItem *)&vendor->container.item, 0, text, 0x0035, 3);
	SendPacketToPlayer(player, sbuf, -1);
}

/*
 * Custom handler - 0xBD not present in UoDemo.exe (predates 1.26.0).
 * Clients >= 1.26.0 send this on the game connection after POSTLOGIN,
 * before character select, so dispatch at the CUserSock layer before a
 * CPlayer exists. Format: null-terminated version string (e.g. "2.0.8").
 */
void
HandlePacket_CLIENT_VERSION(CUserSock *this, uint8_t *buf)
{
	uint32_t off;
	char *version;

	off = 0;
	GetString(buf, &off, &version, 20);

	strncpy(this->clientVersion, version, sizeof(this->clientVersion) - 1);
	this->clientVersion[sizeof(this->clientVersion) - 1] = '\0';

	{
		const ClientVersionInfo *info = Version_FindByName(version);
		if (info)
			this->detectedKeyIndex = info->clientEnum;
	}

	Log_Game(this->addr, "'%s' client version %s", this->account ? this->account->login : "?", version);
}

// SpeechKeywordDispatch deleted (CUSTOM) - binary handles via wombat scripts

/*
 * Custom - HandlePacket_SKILLOCK
 *
 * Handles incoming packet 0x3A from clients >= 1.26.2 to set skill lock state.
 * Packet format: [0x3A][size:2][skillID:2][lockState:1] (6 bytes total).
 * Lock states: 0=Up (gain), 1=Down (decay), 2=Locked (no change).
 */
void
HandlePacket_SKILLOCK(CPlayer *this, uint8_t *buf)
{
	uint16_t skillId;
	uint8_t lockState;

	// buf[0] = packet type (0x3A), buf[1..2] = size
	skillId = ntohs(*(uint16_t *)(buf + 3));
	lockState = buf[5];

	if (skillId >= (uint16_t)CSkillManager_GetMaxSkills(&g_SkillManager))
		return;
	if (lockState > 2)
		return;

	this->skillLocks[skillId] = lockState;
}

/*
 * Custom - GM_TargetJail
 *
 * Click callback invoked when .jail is used without a queued victim.
 * Validates that the clicked entity is a player and jails it.
 */
static void
GM_TargetJail(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsPlayer(target)) {
		CPlayer_SystemMessage(player, "Target not a player");
		return;
	}
	CPlayer_SystemMessage(player, "Jailing player");
	DoJail(target, (CItem *)player);
}

/*
 * Custom - GM_TargetUnjail
 *
 * Click callback invoked when .unjail is used without a queued victim.
 * Validates that the clicked entity is a player and unjails it.
 */
static void
GM_TargetUnjail(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsPlayer(target)) {
		CPlayer_SystemMessage(player, "Target not a player");
		return;
	}
	CPlayer_SystemMessage(player, "Unjailing player");
	DoUnjail(target, (CItem *)player);
}

/*
 * Custom - GM_TargetRelease
 *
 * Click callback invoked when .release is used without a queued victim.
 * Consumes the staged location name (if any) and releases the clicked
 * player.
 */
static void
GM_TargetRelease(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	const char *loc = g_PendingReleaseLocSet ? g_PendingReleaseLoc : NULL;
	g_PendingReleaseLoc[0] = '\0';
	g_PendingReleaseLocSet = 0;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsPlayer(target)) {
		CPlayer_SystemMessage(player, "Target not a player");
		return;
	}
	CPlayer_SystemMessage(player, "Releasing player");
	DoRelease(target, (CItem *)player, loc);
}
