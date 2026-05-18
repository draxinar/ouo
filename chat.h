/*
 * Custom - UO chat protocol (packets 0xB2/0xB3/0xB5).
 *
 * Server-side implementation of the Ultima Online chat system. UoDemo.exe's
 * packet dispatcher stops at packet type 0xB1 and has no chat handlers; the
 * protocol layer is decompiled from client 1.25.37 and the conference/user
 * logic is a CUSTOM server-side system with no binary equivalent.
 */
#ifndef CHAT_H_
#define CHAT_H_

#include <stdint.h>

__extension__ typedef struct CPlayer CPlayer;

void Chat_Init(void);
void Chat_OnPlayerDisconnect(CPlayer *player);
void HandlePacket_CHAT_TEXT(CPlayer *player, uint8_t *buf);
void HandlePacket_CHAT_OPEN(CPlayer *player, uint8_t *buf);

#endif /* CHAT_H_ */
