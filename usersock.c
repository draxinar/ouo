/*
 * CUserSock - per-client game connection state.
 *
 * Owns the socket, encryption context, logged-in player handle, and
 * packet dispatch loop for a single client; runs the login handshake
 * and then forwards inbound packets to the packet handler table.
 */

#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"
#include "blowfish.h"
#include "packet_handler.h"
#include "packet_utils.h"
#include "pending_auth.h"
#include "player.h"
#include "time.h"
#include "twofish.h"
#include "usersock.h"
#include "version.h"

CUserSock_vtable VTABLE_CUserSock = {
	CUserSock_Destructor,
	CUserSock_Handle_Input,
	CSocket_Send,
	CSocket_IsBuffer,
	CSocket_Close,
};

CUserSock *GLOBAL_CUserSock;
uint8_t g_ServerAddr[4];
uint16_t g_ServerPort = 2593;

/*
 * 0x0047EB43 - UserSock_DoHandlePacket
 *
 * Packet dispatch loop: reads packet type, validates pre-login gate,
 * dispatches to per-type handlers.
 *
 * MODIFIED: seed consumption handles multi-client encryption (Blowfish,
 * Twofish, XOR auto-detect, GodClient plaintext) for clients >= 1.25.35
 * that send a 4-byte login seed. Packet type limit raised from 0xB6 to
 * 0xE3 for 1.26+ through 5.0.x.
 */
uint16_t
UserSock_DoHandlePacket(CUserSock *this)
{
	int result;
	int i;
	uint8_t *packetType;
	uint16_t g_PacketSize;
	uint8_t *buf;

	// MODIFIED: seed consumption for multi-client support.
	// Binary (0x0047EB43) has no seed handling - UO Demo uses unencrypted
	// packets. Clients >= 1.25.35 send a 4-byte login seed before the first packet.
	if (this->seedConsumed == 0) {
		uint32_t seed;

		if (this->curr < 4)
			return 0;

		// Seedless plaintext detection: UoDemo and 1.25.33 send no
		// login seed - the first byte is the packet type (0x80).
		// Check for a valid plaintext ACCT_LOGIN_REQ before trying
		// seed consumption. A seeded encrypted client whose seed
		// starts with 0x80 will fail the printable-ASCII check on
		// the following bytes (encrypted data, not an account name).
		if (g_AutoDetect && this->buf[0] == 0x80 && AutoDetectPlaintext(this->buf, this->curr)) {
			this->detectedKeyIndex = CLIENT_DEMO;
			this->packetTable = Version_GetPacketTable(PROTO_DEMO);
			this->seedConsumed = 1;
			Log_Auth(this->addr, "auto-detected seedless plaintext client (UoDemo)");
			if (this->curr <= 0)
				return 0;
			goto seed_done;
		}

		memcpy(&seed, this->buf, 4);
		seed = ntohl(seed);

		{
			PendingAuth *pa = PendingAuth_Find(seed);
			if (pa) {
                        // Game connection - cipher config from login handoff.
				if (pa->gameCipher == GAME_BF_TF && this->curr <= 4)
					return 0;
				this->packetTable = pa->packetTable;
				if (pa->clientEnum >= 0)
					this->detectedKeyIndex = pa->clientEnum;
				switch (pa->gameCipher) {
				case GAME_BF_TF:
                                // 2.0.0c-2.0.3: BF with rotation + TF.
                                // Trial decrypt: try TF+BF, fall back to BF only.
					this->gameCrypt = (CGameCrypt *)malloc(sizeof(CGameCrypt));
					if (this->gameCrypt)
						CGameCrypt_InitWithRotation(this->gameCrypt, pa->bfIndex);
					this->twofishCrypt = (TwofishCipher *)malloc(sizeof(TwofishCipher));
					TwofishCipher_Init(this->twofishCrypt, htonl(seed));
					{
						int dataLen = this->curr - 4;
						uint8_t *saved = malloc(dataLen);
						memcpy(saved, &this->buf[4], dataLen);

						TwofishCipher_Decrypt(this->twofishCrypt, &this->buf[4], dataLen);
						CGameCrypt_Decrypt(this->gameCrypt, &this->buf[4], dataLen);

						if (this->buf[4] == 0x91) {
							free(saved);
						} else {
							memcpy(&this->buf[4], saved, dataLen);
							free(saved);
							CGameCrypt_Init(this->gameCrypt, this->gameCrypt->tableIndex);
							CGameCrypt_Decrypt(this->gameCrypt, &this->buf[4], dataLen);
							if (this->buf[4] == 0x91) {
								free(this->twofishCrypt);
								this->twofishCrypt = NULL;
							} else {
								PendingAuth_Remove(seed);
								this->socket.status = SocketClosing;
								return 0;
							}
						}
					}
					break;
				case GAME_TF:
                                // 2.0.4+: Twofish only.
					this->twofishCrypt = (TwofishCipher *)malloc(sizeof(TwofishCipher));
					TwofishCipher_Init(this->twofishCrypt, htonl(seed));
					this->socket.sendCipher = malloc(sizeof(TwofishSendCipher));
					TwofishSendCipher_Init(this->socket.sendCipher, this->twofishCrypt);
					if (this->curr > 4)
						TwofishCipher_Decrypt(this->twofishCrypt, &this->buf[4], this->curr - 4);
					break;
				case GAME_BF:
                                // 1.26.0-2.0.0b: Blowfish only (no rotation).
					this->gameCrypt = (CGameCrypt *)malloc(sizeof(CGameCrypt));
					if (this->gameCrypt)
						CGameCrypt_Init(this->gameCrypt, pa->bfIndex);
					if (this->curr > 4)
						CGameCrypt_Decrypt(this->gameCrypt, &this->buf[4], this->curr - 4);
					break;
				case GAME_NONE:
                                // Plaintext or 1.25.x XOR inherited from login.
					if (pa->xorVersion >= 0) {
						const ClientVersionInfo *xorInfo = Version_Find(pa->xorVersion);
						if (xorInfo) {
							this->cryptA1 = xorInfo->cryptA1;
							this->cryptA2 = xorInfo->cryptA2;
							this->cryptB = xorInfo->cryptB;
							this->cryptDoubleRound = (xorInfo->cipherType == CIPHER_DOUBLE_ROUND);
							this->cryptPolynomial = (xorInfo->cipherType == CIPHER_POLYNOMIAL);
							CUserSock_InitCrypt(this, seed);
							if (this->curr > 4)
								UserSock_Decrypt(this, &this->buf[4], this->curr - 4);
							Log_Game(this->addr, "game connection: inherited XOR cipher from %s", xorInfo->name);
						}
					}
					break;
				}
				PendingAuth_Remove(seed);
			} else if (g_AutoDetect) {
				if (this->curr <= 4)
					return 0;
				if (g_NoCrypt) {
                                // Nocrypt auto-detect: data is plaintext, skip cipher trials.
                                // CLIENT_VERSION (0xBD) refines detectedKeyIndex later.
					if (!AutoDetectPlaintext(&this->buf[4], this->curr - 4)) {
						this->socket.status = SocketClosing;
						return 0;
					}
				} else if (AutoDetectXorKeys(this, seed, &this->buf[4], this->curr - 4)) {
					CUserSock_InitCrypt(this, seed);
					UserSock_Decrypt(this, &this->buf[4], this->curr - 4);
				} else if (AutoDetectPolynomial(this, seed, &this->buf[4], this->curr - 4)) {
					CUserSock_InitCrypt(this, seed);
					UserSock_Decrypt(this, &this->buf[4], this->curr - 4);
				} else if (AutoDetectPlaintext(&this->buf[4], this->curr - 4)) {
					this->detectedGodClient = 1;
				} else {
					this->socket.status = SocketClosing;
					return 0;
				}
			} else {
				if (g_UseEncryption)
					CUserSock_InitCrypt(this, seed);
				if (g_UseEncryption && this->curr > 4)
					UserSock_Decrypt(this, &this->buf[4], this->curr - 4);
			}
		}

		for (i = 4; i < this->curr; i++)
			this->buf[i - 4] = this->buf[i];
		this->curr -= 4;
		this->seedConsumed = 1;
		if (this->curr <= 0)
			return 0;
seed_done:;
	}

	buf = this->buf;

	while (1) {
		// UserSock_Decrypt is a no-op in the demo binary.
		packetType = &buf[0];

		if (PacketIsDynamicSize(buf)) {
			memcpy(&g_PacketSize, &buf[1], 2);
			result = ntohs_inplace(&g_PacketSize);
		} else {
			g_PacketSize = this->packetTable[5 * *packetType];
		}
		// MODIFIED: binary uses 0xB6, we use 0xE3 for 1.26+ through 5.0.x packet types
		if ((g_PacketSize & 0xFFFF) >= 1 && (*packetType & 0xFF) < 0xE3)
			break;
		// Invalid packet type or zero size: skip 1 byte
		this->invalidPacketCount++;
		g_PacketSize++;
loop:
		for (i = g_PacketSize; i < this->curr; i++)
			this->buf[i - g_PacketSize] = this->buf[i];
		result = 1; // binary: eax = this (artifact, return value never used)
		this->curr -= g_PacketSize;
		if (this->curr <= 0)
			return result;
	}
	// Pre-login gate: only accept specific packet types before seedConsumed.
	// Binary accepts: 0x00, 0x5D, 0x80, 0x83, 0x84, 0x01, 0x91, 0xA4.
	// MODIFIED: also accept 0xA0 (BRITANNIA_SELECT) for two-phase login flow.
	if (this->seedConsumed != 1) {
		if (*packetType != 0x00 && *packetType != PacketType_PRELOGIN && *packetType != PacketType_ACCT_LOGIN_REQ && *packetType != PacketType_ACCT_DEL_CHAR &&
		        *packetType != PacketType_CHG_CHAR_PW && *packetType != 0x01 && *packetType != PacketType_POSTLOGIN && *packetType != PacketType_HARDWARE_INFO &&
		        *packetType != PacketType_BRITANNIA_SELECT) {
			this->socket.status = 1;
			return 1; // binary: eax = this (artifact, return value never used)
		}
	}
	// Post-login dispatch gate.
	// MODIFIED: added BRITANNIA_SELECT for two-phase login flow.
	if (*packetType == PacketType_ACCT_LOGIN_REQ || *packetType == PacketType_ACCT_DEL_CHAR || *packetType == PacketType_CHG_CHAR_PW || *packetType == 0x01 ||
	        *packetType == PacketType_POSTLOGIN || *packetType == PacketType_HARDWARE_INFO || *packetType == PacketType_BRITANNIA_SELECT || this->seedConsumed == 1 ||
	        PacketIsEDEDEDED(buf) != 0) {
		if (this->curr < (int)(g_PacketSize & 0xFFFF))
			return result;
		PacketGetDynamicSize(buf);
		switch (*packetType) {
		case 0x00: // PacketType_LOGIN
			HandlePacket_LOGIN(this, buf);
			break;
		case PacketType_PRELOGIN: // 0x5D
			HandlePacket_PRELOGIN(this, buf);
			break;
		case PacketType_ACCT_LOGIN_REQ: // 0x80
			HandlePacket_ACCT_LOGIN_REQ(this, buf);
			break;
		case PacketType_ACCT_DEL_CHAR: // 0x83
			HandlePacket_ACCT_DEL_CHAR(this, buf);
			break;
		case PacketType_CHG_CHAR_PW: // 0x84
			HandlePacket_CHG_CHAR_PW(this, buf);
			break;
		case PacketType_POSTLOGIN: // 0x91
			HandlePacket_POSTLOGIN(this, buf);
			break;
		// MODIFIED: BRITANNIA_SELECT not in binary's CUserSock switch.
		// Binary handles it in DoHandlePacket_Player (0xA0, case 41).
		// Clients >= 1.25.30 send 0xA0 on the login connection before a player
		// exists, so we handle it here for multi-client support.
		case PacketType_BRITANNIA_SELECT: // 0xA0
			HandlePacket_BRITANNIA_SELECT(this, buf);
			break;
		// MODIFIED: CLIENT_VERSION (0xBD) arrives after POSTLOGIN but before
		// PRELOGIN/character-select, so this->player is not yet set.
		// Dispatch at the CUserSock layer so clientVersion is populated in
		// time for PRELOGIN's auth log event.
		case PacketType_CLIENT_VERSION: // 0xBD
			HandlePacket_CLIENT_VERSION(this, buf);
			break;
		default:
			if (this->player)
				DoHandlePacket_Player(this->player, *packetType, buf);
			break;
		}
		g_PacketSize = g_PacketSize & 0xFFFF;
		goto loop;
	}
	this->socket.status = 1;
	return 1; // binary: eax = this (artifact, return value never used)
}

/*
 * 0x0047EF0C - CUserSock::Accept
 *
 * Accepts a pending connection on the listen socket, allocates and
 * constructs a CUserSock around it, and bumps the connection metric.
 */
int
CUserSock_Accept(CListenSocket *this)
{
	CUserSock *res;
	CUserSock *socket;
	int s;
	uint32_t addr;

	s = CListenSocket_Accept(this, &addr);
	if (s != -1) {
		socket = (CUserSock *)malloc(sizeof(*socket));
		if (socket)
			res = CUserSock_Constructor(socket, s, addr);
		else
			res = 0;
		s = (res != NULL);
	}
	return s;
}

/*
 * 0x0047EF93 - CUserSock::CUserSock
 *
 * Initializes a CUserSock wrapping fd s and remote addr: installs the
 * vtable, zeroes the receive buffer and crypto state, and seeds the
 * per-connection cipher constants from the active detection profile.
 */
CUserSock *
CUserSock_Constructor(CUserSock *this, int s, int addr)
{
	CSocket_Constructor(&this->socket);
	this->socket.vtable = (CSocket_vtable *)&VTABLE_CUserSock;
	this->socket.name = "CUserSock";
	memset(this->buf, 0, sizeof(this->buf));
	this->curr = 0;
	this->invalidPacketCount = 0;
	this->addr = addr;
	this->seedConsumed = 0;
	this->player = 0;
	this->cryptKey1 = 0;
	this->cryptKey2 = 0;
	this->encrypted = 0;
	this->gameCrypt = NULL;
	this->twofishCrypt = NULL;
	this->detectedBfIndex = 0;
	this->detectedKeyIndex = -1;
	this->detectedGodClient = 0;
	this->clientVersion[0] = '\0';
	this->account = NULL;

	// Copy global cipher constants to per-connection fields.
	// In auto-detect mode, these stay zero until AutoDetectXorKeys
	// matches a key set on the first login packet.
	if (!g_AutoDetect) {
		this->cryptA1 = g_CryptA1;
		this->cryptA2 = g_CryptA2;
		this->cryptB = g_CryptB;
		this->cryptDoubleRound = g_CryptDoubleRound;
		this->cryptPolynomial = g_CryptPolynomial;
	} else {
		this->cryptA1 = 0;
		this->cryptA2 = 0;
		this->cryptB = 0;
		this->cryptDoubleRound = 0;
		this->cryptPolynomial = 0;
	}

	// Per-connection packet table. Default to 1.26+ in auto-detect mode
	// (updated after detection) or the forced version's protocol table.
	// Game connections override this during seed consumption via
	// PendingAuth lookup.
	if (g_AutoDetect) {
		this->packetTable = Version_GetPacketTable(PROTO_126);
	} else {
		const ClientVersionInfo *info = Version_Find(g_ClientVersion);
		this->packetTable = Version_GetPacketTable(info ? info->protocol : PROTO_126);
	}

	CSocket_SetSocket(&this->socket, s);
	CSocket_SetBlock(&this->socket, 0);
	CUserSock_GetServerIP(this);
	GLOBAL_CUserSock = this;
	return this;
}

/*
 * 0x0047F060 - CUserSock::Delete
 *
 * Tears down a user connection: logs out the player, releases cipher
 * allocations, and chains to the base CSocket::Delete.
 */
CSocket *
CUserSock_Delete(CUserSock *this)
{
	this->socket.vtable = (CSocket_vtable *)&VTABLE_CUserSock;
	if (GLOBAL_CUserSock == this)
		GLOBAL_CUserSock = 0;
	if (this->player) {
		// Custom: log disconnect before logout
		if (this->account)
			Log_Game(this->addr, "'%s' disconnected as '%s'", this->account->login, CMobile_GetName_VT((CItem *)this->player));
		if (CPlayer_IsPlayerOnline(this->player))
			CPlayer_LogOut(this->player, 1);
		this->player->usersock = 0;
	}
	if (this->gameCrypt) {
		free(this->gameCrypt);
		this->gameCrypt = NULL;
	}
	if (this->twofishCrypt) {
		free(this->twofishCrypt);
		this->twofishCrypt = NULL;
	}
	if (this->socket.sendCipher) {
		free(this->socket.sendCipher);
		this->socket.sendCipher = NULL;
	}
	return CSocket_Delete(&this->socket);
}

/*
 * 0x0047F0FF - CUserSock::Handle_Input
 *
 * Reads pending bytes into the connection buffer, decrypts packets,
 * and dispatches each complete packet to its handler.
 */
int16_t
CUserSock_Handle_Input(CUserSock *this)
{
	int n;
	uint8_t packetType;
	uint16_t g_PacketSize;
	uint8_t *buf;

	n = recv(this->socket.s, &this->buf[this->curr], 0x10000 - this->curr, 0);
	// Binary uses a buffered WinSock wrapper (0x004E730E) where recv==0
	// means "no data buffered". On Linux, POSIX recv()==0 means the
	// client closed the connection (EOF). We must close the socket to
	// prevent select() from spinning on the perpetually-readable fd.
	if (n == 0) {
		this->socket.status = SocketClosing;
		return 0;
	}
	if (n == -1) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			this->socket.status = SocketClosing;
		return -1;
	}

	// Decrypt newly received bytes. Only after the 4-byte seed has
	// been consumed (seedConsumed == 1). The seed itself is plaintext.
	if (this->seedConsumed == 1) {
		if (this->twofishCrypt && this->gameCrypt) {
			// BF+TF mode (2.0.0c-2.0.3): Twofish first (outer), then Blowfish (inner)
			TwofishCipher_Decrypt(this->twofishCrypt, &this->buf[this->curr], n);
			CGameCrypt_Decrypt(this->gameCrypt, &this->buf[this->curr], n);
		} else if (this->twofishCrypt) {
			// Game connection: Twofish streaming decrypt (2.0.4+)
			TwofishCipher_Decrypt(this->twofishCrypt, &this->buf[this->curr], n);
		} else if (this->gameCrypt) {
			// Game connection: Blowfish CFB decrypt (1.26.0-2.0.0b)
			CGameCrypt_Decrypt(this->gameCrypt, &this->buf[this->curr], n);
		} else if (g_UseEncryption) {
			// Login connection: XOR cipher decrypt
			UserSock_Decrypt(this, &this->buf[this->curr], n);
		}
	}

	this->curr += n;
	if (this->curr >= 1) {
		// When the 4-byte login seed hasn't been consumed yet,
		// buf[0] is the first byte of the seed (not a packet type).
		// Bypass the packet-size check and go straight to
		// DoHandlePacket once we have at least 4 bytes.
		if (this->seedConsumed == 0) {
			if (this->curr >= 4)
				n = UserSock_DoHandlePacket(this);
		} else {
			buf = this->buf;
			packetType = buf[0];
			if (IsPacketDynamicSize2(packetType)) {
				memcpy(&g_PacketSize, &buf[1], 2);
				ntohs_inplace(&g_PacketSize);
			} else {
				g_PacketSize = this->packetTable[5 * packetType];
			}
			n = g_PacketSize;
			if (this->curr >= g_PacketSize) {
				n = UserSock_DoHandlePacket(this);
			} else if (this->curr == 0x10000) {
				n = 1; // binary: eax = this (artifact, return value never used)
				this->socket.status = SocketClosing;
			}
		}
	}
	return n;
}

/*
 * CUserSock::InitCrypt
 *
 * Derive XOR encryption keys from the client's 4-byte login seed.
 *   key1 = ((~seed ^ 0x1357) << 16) | ((seed ^ 0xAAAA) & 0xFFFF)
 *   key2 = ((~seed & 0xFFFF0000) ^ 0xABCD0000) | ((seed >> 16) ^ 0x4321)
 */
void
CUserSock_InitCrypt(CUserSock *this, uint32_t seed)
{
	uint32_t notkey;

	notkey = ~seed;
	// Client 0x081094A4: cryptKey1 at offset 0x00020010
	this->cryptKey1 = ((notkey << 16) ^ 0x13570000) | ((seed & 0xffff) ^ 0xaaaa);
	// Client 0x081094D4: cryptKey2 at offset 0x00020014
	this->cryptKey2 = ((notkey & 0xffff0000u) ^ 0xabcd0000u) | ((seed >> 16) ^ 0x4321);
	this->encrypted = 1;
}

/*
 * 0x0047F222 - CUserSock::SendPacketRaw
 *
 * Allocates a copy of the packet, resolves its dynamic size, and
 * appends to the socket send buffer.
 */
void
CUserSock_SendPacketRaw(CUserSock *this, uint8_t *buf, uint32_t size)
{
	uint8_t *copy;

	copy = (uint8_t *)malloc(size);
	memcpy(copy, buf, size);
	PacketGetDynamicSize(copy);
	Append_To_CSocketBuffer(&this->socket, copy, size);
}

/*
 * 0x0047F280 - CUserSock::~CUserSock
 *
 * Scalar deleting destructor: runs CUserSock_Delete and frees the object
 * when v & 1.
 */
CUserSock *
CUserSock_Destructor(CUserSock *this, uint8_t v)
{
	CUserSock_Delete(this);
	if (v & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047F320 - IsPacketDynamicSize2
 *
 * MODIFIED: uses the current connection's per-connection packet table for
 * multi-client support (binary reads g_PacketTable directly, which only
 * describes the demo protocol). Falls back to g_PacketTable outside of
 * dispatch.
 */
int
IsPacketDynamicSize2(uint8_t type)
{
	uint16_t *pt = GLOBAL_CUserSock ? GLOBAL_CUserSock->packetTable : g_PacketTable;
	return pt[5 * type] & PacketDynamicSize;
}

/*
 * 0x0047F340 - UserSock_Decrypt
 *
 * No-op in the demo binary. When g_UseEncryption is set, decrypts len bytes
 * in-place using the XOR cipher matching the client's CClientSock::Write,
 * selecting single-round, double-round, or polynomial key rotation based
 * on the detected client version.
 */
void
UserSock_Decrypt(CUserSock *this, uint8_t *buf, int len)
{
	int i;
	uint32_t key1, key2, temp;

	if (!this->encrypted)
		return;

	for (i = 0; i < len; i++) {
		buf[i] ^= (uint8_t)this->cryptKey1;

		key1 = this->cryptKey1;
		key2 = this->cryptKey2;

		if (this->cryptPolynomial) {
			// Polynomial cipher (1.25.36, VA 0x00489320)
			uint32_t k2sq = key2 * key2;
			uint32_t k1sq = key1 * key1;
			this->cryptKey2 = (0x387FC5CCu >> (k2sq * 5 & 31)) + key2 * 0x387FC5CCu + k1sq * 0x35CE9581u + 0x07AFCC37u;
			uint32_t nk2 = this->cryptKey2;
			this->cryptKey1 = (0x021510C6u >> (k1sq * 3 & 31)) - nk2 * nk2 * 0x4C3A1353u + key1 * 0x021510C6u + 0x16EF783Fu;
		} else if (this->cryptDoubleRound) {
			// Double-round: key2 goes through two shift-XOR rounds
			temp = ((key2 >> 1) | (key1 << 31)) ^ this->cryptA1;
			this->cryptKey2 = ((temp >> 1) | (key1 << 31)) ^ this->cryptA2;
		} else {
			// Single-round: key2 goes through one shift-XOR round
			this->cryptKey2 = ((key2 >> 1) | (key1 << 31)) ^ this->cryptA1;
		}
		if (!this->cryptPolynomial)
			this->cryptKey1 = ((key1 >> 1) | (key2 << 31)) ^ this->cryptB;
	}
}

/*
 * Custom - CUserSock::GetServerIP
 *
 * Populates g_ServerAddr from getsockname() if not already set.
 * Skips loopback (127.x) and 0.0.0.0: caching those here poisons
 * every subsequent 0xA8/0x8C we send, telling remote clients to
 * reconnect to their own machine. The first non-loopback connection
 * wins; production admins who want a fixed public IP should use -a.
 */
void
CUserSock_GetServerIP(CUserSock *this)
{
	struct sockaddr_in sin;
	socklen_t len;
	uint8_t ip[4];

	if (g_ServerAddr[0] || g_ServerAddr[1] || g_ServerAddr[2] || g_ServerAddr[3])
		return;

	len = sizeof(sin);
	if (getsockname(this->socket.s, (struct sockaddr *)&sin, &len) != 0)
		return;

	memcpy(ip, &sin.sin_addr, 4);
	if (ip[0] == 127)
		return;
	if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0)
		return;

	memcpy(g_ServerAddr, ip, 4);
}
