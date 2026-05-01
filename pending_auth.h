#ifndef PENDING_AUTH_H_
#define PENDING_AUTH_H_

#include <stdint.h>
#include <time.h>

/*
 * One entry in the pending-auth table that bridges login to game
 * connections. BRITANNIA_SELECT (0xA0) hands the client a random nonce
 * via USER_SERVER (0x8C); the nonce is the 4-byte login seed on the
 * reconnect and also the Twofish key for 2.0.4+ clients.
 */
__extension__ typedef struct PendingAuth PendingAuth;
struct PendingAuth {
	uint32_t nonce;
	int gameCipher;         // GAME_NONE / GAME_BF / GAME_TF / GAME_BF_TF
	int bfIndex;            // Blowfish table index
	uint16_t *packetTable;
	int xorVersion;         // CLIENT_* enum for 1.25.x XOR, or -1
	int clientEnum;         // CLIENT_* enum of the detected client, or -1
	time_t created;
};

uint32_t PendingAuth_Create(int gameCipher, int bfIndex, uint16_t *packetTable, int xorVersion, int clientEnum);
PendingAuth *PendingAuth_Find(uint32_t nonce);
void PendingAuth_Remove(uint32_t nonce);
void PendingAuth_ExpireStale(void);

#endif /* PENDING_AUTH_H_ */
