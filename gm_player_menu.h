/*
 * Custom - GM Player Menu
 *
 * .players opens a generic gump (0xB0) listing every other connected
 * player; clicking teleports the GM adjacent to that player.
 * .gotoplayer <name> performs the same teleport without a UI.
 */

#ifndef GM_PLAYER_MENU_H_
#define GM_PLAYER_MENU_H_

#include <stdint.h>

__extension__ typedef struct CPlayer CPlayer;

#define GM_PLAYER_MENU_GUMP_ID 0x474D504Cu /* 'GMPL' */

void GM_OpenPlayerMenu(CPlayer *gm);
void GM_HandlePlayerMenuResponse(CPlayer *gm, uint32_t buttonID);
void GM_TeleportGmAdjacentTo(CPlayer *gm, CPlayer *target);
void GM_GotoPlayerCommand(CPlayer *gm, const char *arg);
CPlayer *GM_FindConnectedPlayerByName(const char *name);

#endif /* GM_PLAYER_MENU_H_ */
