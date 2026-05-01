#ifndef TRADE_H_
#define TRADE_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CTradeSession CTradeSession;

/*
 * Active trade between two players (28 bytes). Held in a singly-linked
 * list off a static head; each side owns a trade container and an accept
 * flag.
 */
struct CTradeSession {
	CItem *container1;          // +0x00
	CItem *container2;          // +0x04
	CPlayer *player1;           // +0x08
	CPlayer *player2;           // +0x0C
	uint32_t accept1;           // +0x10
	uint32_t accept2;           // +0x14
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;            // 64-bit alignment pad
#endif
	struct CTradeSession *next; // +0x18
};

void CTradeSession_FindOrCreateSession(CTradeSession **listHead, CPlayer *player1, CPlayer *player2, CItem *item); // 0x004D97B2
CTradeSession *CTradeSession_CreateSession(CTradeSession *this, CPlayer *player1, CPlayer *player2, CItem *item); // 0x004D997A
void CTradeSession_ReturnItems(CTradeSession *session); // 0x004D9BFE
void SetTradeAcceptState(CTradeSession *session, uint32_t accept1, uint32_t accept2); // 0x004D9E57

#endif /* TRADE_H_ */
