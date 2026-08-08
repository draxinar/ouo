/*
 * Custom - GM Player Menu
 *
 * .players opens a generic gump (0xB0) listing every other connected
 * player; clicking teleports the GM adjacent to that player.
 * .gotoplayer <name|0xSERIAL> performs the same teleport without a UI.
 * .bank [name|0xSERIAL] opens a bank box (cursor target if no arg).
 * .paperdoll [name|0xSERIAL] opens a paperdoll (cursor target if no arg).
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
void GM_BankCommand(CPlayer *gm, const char *arg);
void GM_PaperdollCommand(CPlayer *gm, const char *arg);
CPlayer *GM_FindConnectedPlayerByName(const char *name);

#endif /* GM_PLAYER_MENU_H_ */
