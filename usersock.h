#ifndef USERSOCK_H_
#define USERSOCK_H_

#include <stdint.h>

#include "socket.h"

__extension__ typedef struct CSocket CSocket;
__extension__ typedef struct CUserSock CUserSock;
__extension__ typedef struct CUserSock_vtable CUserSock_vtable;
__extension__ typedef struct CGameCrypt CGameCrypt;
__extension__ typedef struct TwofishCipher TwofishCipher;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CAccount CAccount;
__extension__ typedef struct CListenSocket CListenSocket;

/*
 * Game-connection socket: CSocket plus a 64KiB recv buffer, the bound
 * CPlayer, and per-connection decryption state. Everything past player
 * is CUSTOM multi-client support (the binary shipped with XOR only).
 */
struct CUserSock {
	CSocket socket;
	uint8_t buf[65536];
	int invalidPacketCount;
	int curr;
	int addr;
	int seedConsumed;
	CPlayer *player;
	uint32_t cryptKey1;          // XOR key (rotates per byte)
	uint32_t cryptKey2;          // rotation helper
	int encrypted;
#if __SIZEOF_POINTER__ == 8
	int _pad64;                  // 64-bit alignment pad
#endif
	CGameCrypt *gameCrypt;       // Blowfish (1.26.0-2.0.3)
	TwofishCipher *twofishCrypt; // Twofish (2.0.4+)
	uint32_t cryptA1;
	uint32_t cryptA2;
	uint32_t cryptB;
	int cryptDoubleRound;
	int cryptPolynomial;         // 1.25.36 polynomial cipher
	int detectedBfIndex;         // Blowfish table index from auto-detect
	int detectedKeyIndex;        // CLIENT_* from auto-detect (-1 if none)
	int detectedGodClient;       // 1 if plaintext GodClient 2.0.8n
	uint16_t *packetTable;       // proto-specific packet sizes
	char clientVersion[24];      // version string from 0xBD
	CAccount *account;
};

/*
 * Vtable layout for CUserSock.
 */
struct CUserSock_vtable {
	CUserSock *(*Destructor)(CUserSock *, uint8_t);
	int16_t (*Recv)(CUserSock *);
	CSocket *(*Send)(CSocket *);
	int (*IsBuffer)(CSocket *);
	CSocket *(*Close)(CSocket *);
};

extern CUserSock_vtable VTABLE_CUserSock;
extern CUserSock *GLOBAL_CUserSock;
extern uint8_t g_ServerAddr[4];
extern uint16_t g_ServerPort;

uint16_t UserSock_DoHandlePacket(CUserSock *this); // 0x0047EB43
int CUserSock_Accept(CListenSocket *this); // 0x0047EF0C
CUserSock *CUserSock_Constructor(CUserSock *this, int s, int addr); // 0x0047EF93
CSocket *CUserSock_Delete(CUserSock *this); // 0x0047F060
int16_t CUserSock_Handle_Input(CUserSock *this); // 0x0047F0FF
void CUserSock_SendPacketRaw(CUserSock *this, uint8_t *buf, uint32_t size); // 0x0047F222
CUserSock *CUserSock_Destructor(CUserSock *this, uint8_t v); // 0x0047F280
int IsPacketDynamicSize2(uint8_t type); // 0x0047F320
void UserSock_Decrypt(CUserSock *this, uint8_t *buf, int len); // 0x0047F340
void CUserSock_GetServerIP(CUserSock *this);
void CUserSock_InitCrypt(CUserSock *this, uint32_t seed);

#endif /* USERSOCK_H_ */
