/*
 * GM Player Menu - .players gump and .gotoplayer direct teleport.
 *
 * Builds a paginated generic gump (0xB0) listing every other connected
 * player, with each player's serial encoded as the button reply ID. The
 * 0xB1 response is interposed in HandlePacket_GumpMenuSelection by gump
 * ID and routed here. Teleport mirrors the .go pattern: VT_HIDE,
 * VT_DROP_AT_FEET, then CMobile_NotifyNearbyPlayers, landing one tile
 * east of the target.
 *
 * CUSTOM - no binary equivalent.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "blockmanager.h"
#include "container.h"
#include "cstring.h"
#include "entity.h"
#include "gm_player_menu.h"
#include "list.h"
#include "location.h"
#include "mobile.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "vtable.h"
#include "wombat.h"

#define GM_PLAYER_MENU_ROWS_PER_PAGE 20
#define GM_PLAYER_MENU_MAX_ROWS      200

/*
 * Helper - append_cmd
 *
 * Format a gump layout command and append it as a CString to cmdList.
 * CList_Append deep-copies the CString; the local is destructed after
 * append.
 */
static void
append_cmd(CList *cmdList, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	CString tmp;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	CString_Constructor(&tmp, buf);
	CList_Append(cmdList, WTYPE_STRING, (uintptr_t)&tmp);
	CString_Destructor(&tmp);
}

/*
 * Helper - append_text
 */
static void
append_text(CList *textList, const char *s)
{
	CString tmp;

	CString_Constructor(&tmp, s);
	CList_Append(textList, WTYPE_STRING, (uintptr_t)&tmp);
	CString_Destructor(&tmp);
}

/*
 * Custom - GM_FindConnectedPlayerByName
 *
 * Walk g_PlayerList and return the first connected player whose name
 * matches case-insensitively. Equivalent to the binary's static
 * CPlayer_FindByName but without the EntityManager fallback - we want
 * online players only.
 */
CPlayer *
GM_FindConnectedPlayerByName(const char *name)
{
	CPlayer *p;
	char *pname;

	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		pname = ((char *(*)(void *))VT_FN(&p->mobile.container.item, VT_GET_NAME))(p);
		if (pname != NULL && strcasecmp(pname, name) == 0)
			return p;
	}
	return NULL;
}

/*
 * Custom - count_name_matches
 *
 * Returns how many connected players (excluding 'exclude') match name.
 */
static int
count_name_matches(const char *name, CPlayer *exclude)
{
	CPlayer *p;
	char *pname;
	int count;

	count = 0;
	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		if (p == exclude)
			continue;
		pname = ((char *(*)(void *))VT_FN(&p->mobile.container.item, VT_GET_NAME))(p);
		if (pname != NULL && strcasecmp(pname, name) == 0)
			count++;
	}
	return count;
}

/*
 * Custom - GM_ResolveConnectedPlayer
 *
 * Resolves arg to a connected CPlayer*. arg may be a hex/dec serial
 * (parsed via strtoul base 0) or a case-insensitive name match. Returns
 * NULL on miss or ambiguity, after emitting an explanatory system
 * message to gm. cmdName is the slash command being resolved for; it
 * fills the "Use .<cmdName> 0xSERIAL to pick one." disambiguation hint.
 */
static CPlayer *
GM_ResolveConnectedPlayer(CPlayer *gm, const char *arg, const char *cmdName)
{
	CPlayer *target;
	CPlayer *p;
	char *pname;
	uint32_t serial;
	char *endp;
	int isSerial;
	int matches;
	char msg[120];

	if (arg == NULL || arg[0] == '\0') {
		CPlayer_SystemMessage(gm, "Player not found.");
		return NULL;
	}

	isSerial = 0;
	target = NULL;
	endp = NULL;
	if ((arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) || (arg[0] >= '0' && arg[0] <= '9')) {
		unsigned long parsed = strtoul(arg, &endp, 0);
		if (endp != NULL && *endp == '\0') {
			isSerial = 1;
			serial = (uint32_t)parsed;
			target = CPlayerList_FindBySerial(serial);
		}
	}

	if (!isSerial) {
		matches = count_name_matches(arg, gm);
		if (matches == 0) {
			CPlayer_SystemMessage(gm, "Player not found.");
			return NULL;
		}
		if (matches > 1) {
			snprintf(msg, sizeof(msg), "Multiple players named '%s':", arg);
			CPlayer_SystemMessage(gm, msg);
			for (p = g_PlayerList.head; p != NULL; p = p->next) {
				if (p == gm)
					continue;
				pname = ((char *(*)(void *))VT_FN(&p->mobile.container.item, VT_GET_NAME))(p);
				if (pname != NULL && strcasecmp(pname, arg) == 0) {
					snprintf(msg, sizeof(msg), "  %s [0x%08X]", pname, CMobile_GetSerial(&p->mobile));
					CPlayer_SystemMessage(gm, msg);
				}
			}
			snprintf(msg, sizeof(msg), "Use .%s 0xSERIAL to pick one.", cmdName);
			CPlayer_SystemMessage(gm, msg);
			return NULL;
		}
		target = GM_FindConnectedPlayerByName(arg);
	}

	if (target == NULL) {
		CPlayer_SystemMessage(gm, "Player not found.");
		return NULL;
	}
	return target;
}

/*
 * Custom - GM_OpenPlayerMenu
 *
 * Builds and sends a paginated generic-gump player roster (custom gump
 * ID GM_PLAYER_MENU_GUMP_ID) to the GM. Each row is one player formatted
 * as "name [0xSERIAL]"; the player's serial is used as the button reply
 * ID. Excludes the GM itself. 20 rows per page; pagination uses UO's
 * { page N } directive with Prev/Next buttons that don't roundtrip.
 */
void
GM_OpenPlayerMenu(CPlayer *gm)
{
	CList cmdList;
	CList textList;
	CPlayer *p;
	char *pname;
	uint32_t serial;
	int totalRows;
	int totalPages;
	int row;
	int pageRow;
	int page;
	int textIdx;
	char nameBuf[80];
	uint8_t buf[0x1001C];

	// Count other connected players (capped at GM_PLAYER_MENU_MAX_ROWS).
	totalRows = 0;
	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		if (p == gm)
			continue;
		totalRows++;
		if (totalRows >= GM_PLAYER_MENU_MAX_ROWS)
			break;
	}
	if (totalRows == 0) {
		CPlayer_SystemMessage(gm, "No other players connected.");
		return;
	}
	totalPages = (totalRows + GM_PLAYER_MENU_ROWS_PER_PAGE - 1) / GM_PLAYER_MENU_ROWS_PER_PAGE;

	CList_Constructor(&cmdList);
	CList_Constructor(&textList);

	// Page 0: always-visible background, title, and close button.
	append_cmd(&cmdList, "{ page 0 }");
	append_cmd(&cmdList, "{ resizepic 0 0 5054 320 540 }");
	append_cmd(&cmdList, "{ text 60 12 0 0 }");
	append_cmd(&cmdList, "{ button 280 12 4014 4015 1 0 0 }");
	append_text(&textList, "Connected Players");
	textIdx = 1;

	// Per-page rows. p iterates the player list independently of row index
	// so the GM is skipped without breaking the pagination math.
	row = 0;
	page = 0;
	pageRow = 0;
	for (p = g_PlayerList.head; p != NULL && row < totalRows; p = p->next) {
		if (p == gm)
			continue;
		if (pageRow == 0) {
			page++;
			append_cmd(&cmdList, "{ page %d }", page);
			if (page > 1)
				append_cmd(&cmdList, "{ button 20 500 4014 4015 0 %d 0 }", page - 1);
			if (page < totalPages)
				append_cmd(&cmdList, "{ button 280 500 4005 4007 0 %d 0 }", page + 1);
		}
		serial = CMobile_GetSerial(&p->mobile);
		pname = ((char *(*)(void *))VT_FN(&p->mobile.container.item, VT_GET_NAME))(p);
		append_cmd(&cmdList, "{ button 20 %d 4005 4007 1 0 %u }", 40 + pageRow * 22, serial);
		append_cmd(&cmdList, "{ text 60 %d 0 %d }", 42 + pageRow * 22, textIdx);
		snprintf(nameBuf, sizeof(nameBuf), "%s [0x%08X]", pname != NULL ? pname : "?", serial);
		append_text(&textList, nameBuf);
		textIdx++;
		row++;
		pageRow++;
		if (pageRow >= GM_PLAYER_MENU_ROWS_PER_PAGE)
			pageRow = 0;
	}

	PacketManager_MakePacket_GUMP_GENERIC(buf, CMobile_GetSerial(&gm->mobile), GM_PLAYER_MENU_GUMP_ID, 100, 100, &cmdList, &textList);
	SendPacketToPlayer(gm, buf, -1);

	CList_Destructor(&textList);
	CList_Destructor(&cmdList);
}

/*
 * Custom - GM_TeleportGmAdjacentTo
 *
 * Teleport the GM to (target.x + 1, target.y, target.z), falling back to
 * the target's exact tile if the adjacent coordinate is off-map. Mirrors
 * the .go command's hide/drop/notify sequence.
 */
void
GM_TeleportGmAdjacentTo(CPlayer *gm, CPlayer *target)
{
	CLocation *tloc;
	CLocation goLoc;
	int gx, gy, gz;
	char msg[80];
	char *tname;

	tloc = CEntity_GetLocation(&target->mobile.container.item.resourceEntity.entity);
	gx = (int)tloc->x + 1;
	gy = (int)tloc->y;
	gz = (int)tloc->z;
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, gx, gy)) {
		gx = (int)tloc->x;
		gy = (int)tloc->y;
	}

	CLocation_Init(&goLoc);
	CLocation_Set(&goLoc, (int16_t)gx, (int16_t)gy, (int16_t)gz);

	((void (*)(void *))VT_FN((CItem *)gm, VT_HIDE))((CItem *)gm);
	((void (*)(void *, CLocation *))VT_FN((CItem *)gm, VT_DROP_AT_FEET))((CItem *)gm, &goLoc);
	CMobile_NotifyNearbyPlayers((CItem *)gm);

	tname = ((char *(*)(void *))VT_FN(&target->mobile.container.item, VT_GET_NAME))(target);
	snprintf(msg, sizeof(msg), "Teleported to %s", tname != NULL ? tname : "player");
	CPlayer_SystemMessage(gm, msg);
}

/*
 * Custom - GM_GotoPlayerCommand
 *
 * Implements `.gotoplayer <arg>`. arg may be:
 *   - a player name (case-insensitive exact match)
 *   - a hex serial (0x prefix) or decimal serial
 * On ambiguous name match, lists every candidate with their serial and
 * asks the GM to disambiguate via 0xSERIAL.
 */
void
GM_GotoPlayerCommand(CPlayer *gm, const char *arg)
{
	CPlayer *target;

	target = GM_ResolveConnectedPlayer(gm, arg, "gotoplayer");
	if (target == NULL)
		return;
	if (target == gm) {
		CPlayer_SystemMessage(gm, "Player not found.");
		return;
	}
	GM_TeleportGmAdjacentTo(gm, target);
}

/*
 * Helper - GM_BeginRemoteView / GM_EndRemoteView
 *
 * Make a non-nearby player addressable by the GM's client without
 * leaving a visible avatar at the GM's feet.
 *
 * Begin: spoof target's location to the GM's tile, then push an
 * EQUIPPED_MOB update. The UO client only caches entity updates
 * whose coordinates are inside its view range, so a real far-away
 * location would be filtered and the follow-up OPEN_PAPERDOLL /
 * OPEN_GUMP packets dropped. VT_SEND_ENTITY_UPDATE sends to a
 * single viewer, so only the GM sees the spoof.
 *
 * End: restore the target's real location server-side, then push a
 * second EQUIPPED_MOB. The cache update moves the avatar to the
 * real (off-screen) coordinates so the client stops drawing it,
 * while the gump UI sent in between - referenced by serial, not
 * location - stays open.
 */
static void
GM_BeginRemoteView(CPlayer *gm, CPlayer *target, CLocation *savedLoc)
{
	CEntity *targetEnt;
	CLocation *gmLoc;

	targetEnt = &target->mobile.container.item.resourceEntity.entity;
	*savedLoc = targetEnt->location;
	gmLoc = CEntity_GetLocation(&gm->mobile.container.item.resourceEntity.entity);
	targetEnt->location = *gmLoc;

	((void (*)(CItem *, CItem *, int))VT_FN(&target->mobile.container.item, VT_SEND_ENTITY_UPDATE))(&target->mobile.container.item, (CItem *)gm, 1);
}

static void
GM_EndRemoteView(CPlayer *gm, CPlayer *target, CLocation *savedLoc)
{
	target->mobile.container.item.resourceEntity.entity.location = *savedLoc;
	((void (*)(CItem *, CItem *, int))VT_FN(&target->mobile.container.item, VT_SEND_ENTITY_UPDATE))(&target->mobile.container.item, (CItem *)gm, 1);
}

/*
 * Custom - GM_BankCommand
 *
 * Implements `.bank <name|0xSERIAL>` for opening the bank box of a
 * connected, possibly non-nearby player. Mirrors GotoPlayerCommand's
 * name/serial resolution.
 */
void
GM_BankCommand(CPlayer *gm, const char *arg)
{
	CPlayer *target;
	CLocation savedLoc;

	target = GM_ResolveConnectedPlayer(gm, arg, "bank");
	if (target == NULL)
		return;
	GM_BeginRemoteView(gm, target, &savedLoc);
	CMobile_OpenBankGump(&target->mobile, gm);
	GM_EndRemoteView(gm, target, &savedLoc);
}

/*
 * Custom - GM_PaperdollCommand
 *
 * Implements `.paperdoll <name|0xSERIAL>` for opening the paperdoll of
 * a connected, possibly non-nearby player. Reuses the binary's
 * OpenPaperdoll (0x004DC388); since the GM is in editing mode, the
 * full (un-truncated) title is sent.
 */
void
GM_PaperdollCommand(CPlayer *gm, const char *arg)
{
	CPlayer *target;
	CLocation savedLoc;

	target = GM_ResolveConnectedPlayer(gm, arg, "paperdoll");
	if (target == NULL)
		return;
	GM_BeginRemoteView(gm, target, &savedLoc);
	OpenPaperdoll(gm, CMobile_GetSerial(&gm->mobile), &target->mobile.container.item);
	GM_EndRemoteView(gm, target, &savedLoc);
}

/*
 * Custom - GM_HandlePlayerMenuResponse
 *
 * Called from HandlePacket_GumpMenuSelection when the inbound 0xB1 carries
 * our custom gump ID. buttonID is the target player's serial, or 0 for
 * Cancel.
 */
void
GM_HandlePlayerMenuResponse(CPlayer *gm, uint32_t buttonID)
{
	CPlayer *target;

	if (buttonID == 0)
		return;

	target = CPlayerList_FindBySerial(buttonID);
	if (target == NULL || target == gm) {
		CPlayer_SystemMessage(gm, "Player no longer connected.");
		return;
	}

	GM_TeleportGmAdjacentTo(gm, target);
}
