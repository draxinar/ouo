#ifndef VERSION_H_
#define VERSION_H_

#include <stdint.h>

/*
 * Table-driven client version detection and encryption configuration.
 * Every supported client has an entry in g_VersionTable[] carrying its
 * cipher, protocol, game-encryption mode, and CLI argument.
 * SetClientVersion, parseargs, and the auto-detect path all read it.
 */

__extension__ typedef struct CUserSock CUserSock;

/*
 * Supported client enum. Each version with distinct cipher keys or
 * packet sizes gets an entry; sub-versions that share keys (e.g.
 * 1.25.35g/h/i, 1.26.0a/b) reuse their base version's entry.
 */
enum {
	CLIENT_DEMO = 0,      // UoDemo.exe - no encryption
	CLIENT_12530 = 1,     // 1.25.30 - single-round (A1=0xF1A372D5, B=0x3A1FD527)
	CLIENT_12531 = 2,     // 1.25.31 - single-round (A1=0x395A647C, B=0x0297BCC6)
	CLIENT_12532 = 3,     // 1.25.32 - single-round (A1=0x389DE58C, B=0x026950C6)
	CLIENT_12533 = 4,
	CLIENT_12534 = 5,     // 1.25.34 - single-round (A1=0x38ECEDAC, B=0x025720C6)
	CLIENT_12535 = 6,     // 1.25.35 - single-round (A1=0x383477BC, B=0x02345CC6)
	CLIENT_12536 = 7,     // 1.25.36 - polynomial cipher
	CLIENT_12537 = 8,     // 1.25.37 - double-round (Win + Linux)
	CLIENT_12600 = 9,     // 1.26.0  - double-round, 1.26+ packets
	CLIENT_12601 = 10,    // 1.26.1 - double-round, 1.26+ packets
	CLIENT_12602 = 11,    // 1.26.2 - double-round, 1.26+ packets
	CLIENT_12603 = 12,    // 1.26.3 - double-round, 1.26+ packets
	CLIENT_12604 = 13,    // 1.26.4 - double-round, 1.26+ packets
	CLIENT_200 = 14,      // 2.0.0 (also 2.0.0a/b) - BF only (UOR)
	CLIENT_200C = 15,     // 2.0.0c (also 2.0.0d-g1) - BF+TF (UOR)
	CLIENT_201 = 16,      // 2.0.1 - BF+TF (UOR)
	CLIENT_202 = 17,      // 2.0.2 - BF+TF (UOR)
	CLIENT_203 = 18,      // 2.0.3 - BF+TF (UOR)
	CLIENT_204 = 19,      // 2.0.4 - TF only (UOR)
	CLIENT_205 = 20,      // 2.0.5 - TF only (UOR)
	CLIENT_206 = 21,      // 2.0.6 - TF only (UOR)
	CLIENT_207 = 22,      // 2.0.7 - TF only (UOR)
	CLIENT_208 = 23,      // 2.0.8 - TF only (UOR)
	CLIENT_GOD208 = 24,   // 2.0.8n GodClient (UOR)
	CLIENT_209 = 25,      // 2.0.9 - TF only (UOR)
	CLIENT_300 = 26,      // 3.0.0 - TF only (TD)
	CLIENT_301 = 27,      // 3.0.1 - TF only (TD)
	CLIENT_302 = 28,      // 3.0.2 - TF only (TD)
	CLIENT_303 = 29,      // 3.0.3 - TF only (TD)
	CLIENT_304 = 30,      // 3.0.4 - TF only (TD)
	CLIENT_305 = 31,      // 3.0.5 - TF only (TD)
	CLIENT_306 = 32,      // 3.0.6 - TF only (TD)
	CLIENT_307 = 33,      // 3.0.7 - TF only (TD)
	CLIENT_308 = 34,      // 3.0.8 - TF only (TD)
	CLIENT_400 = 35,      // 4.0.0 - TF only (AOS)
	CLIENT_401 = 36,      // 4.0.1 - TF only (AOS)
	CLIENT_402 = 37,      // 4.0.2 - TF only (AOS)
	CLIENT_403 = 38,      // 4.0.3 - TF only (AOS)
	CLIENT_404 = 39,      // 4.0.4 - TF only (AOS/SE)
	CLIENT_405 = 40,      // 4.0.5 - TF only (AOS/SE)
	CLIENT_406 = 41,      // 4.0.6 - TF only (AOS/SE)
	CLIENT_407 = 42,      // 4.0.7 - TF only (AOS/SE)
	CLIENT_408 = 43,      // 4.0.8 - TF only (AOS/SE)
	CLIENT_409 = 44,      // 4.0.9 - TF only (AOS/SE)
	CLIENT_4010 = 45,     // 4.0.10 - TF only (AOS/SE)
	CLIENT_4011 = 46,     // 4.0.11 - TF only (AOS/SE)
	CLIENT_500 = 47,      // 5.0.0 - TF only (ML)
	CLIENT_501 = 48,      // 5.0.1 - TF only (ML)
	CLIENT_502 = 49,      // 5.0.2 - TF only (ML)
	CLIENT_503 = 50,      // 5.0.3 - TF only (ML)
	CLIENT_504 = 51,      // 5.0.4 - TF only (ML)
	CLIENT_505 = 52,      // 5.0.5 - TF only (ML)
	CLIENT_506 = 53,      // 5.0.6 - TF only (ML)
	CLIENT_5065 = 54,     // 5.0.6.5 - TF only (ML, A1==A2 aliased)
	CLIENT_507 = 55,      // 5.0.7.x - TF only (ML, A1==A2 aliased)
	CLIENT_508 = 56,      // 5.0.8.x - TF only (ML, A1==A2 aliased)
	CLIENT_509 = 57,      // 5.0.9.x - TF only (ML, A1==A2 aliased)
	CLIENT_126_AUTO = 58, // Auto-detect client version from login packet
};

/*
 * Login-packet cipher family. 1.25.36 uses a completely separate
 * polynomial cipher with six hardcoded constants; every other version
 * uses the XOR-shift LFSR in either single- or double-round form.
 */
enum CipherType {
	CIPHER_NONE = 0,
	CIPHER_SINGLE_ROUND = 1,
	CIPHER_DOUBLE_ROUND = 2,
	CIPHER_POLYNOMIAL = 3,
};

/*
 * Which game-channel cipher the client uses once it reaches the game
 * server: Blowfish, Blowfish + Twofish, or Twofish alone.
 */
enum GameCipher {
	GAME_NONE = 0,
	GAME_BF = 1,
	GAME_BF_TF = 2,
	GAME_TF = 3,
};

/*
 * Packet-layout family. Demo through 1.25.37 share the 1.25 sizes;
 * 1.26.0+ bumps 0x00 (+4), 0x02 (+4), and 0x93 (+1). Everything from
 * 2.0.0 upward keeps the 1.26 packet table.
 */
enum Protocol {
	PROTO_DEMO = 0,
	PROTO_125 = 1,
	PROTO_126 = 2,
};

/*
 * One row of g_VersionTable[]: everything needed to configure ciphers
 * and packet tables for a single client release.
 */
typedef struct {
	int clientEnum;
	const char *cliArg;
	const char *name;
	uint32_t cryptA1, cryptA2, cryptB;
	int cipherType;
	int protocol;
	int gameCipher;
	int bfIndex;
	int autoDetectable;
} ClientVersionInfo;

/*
 * Per-byte login cipher, set by SetClientVersion():
 *   buf[i] ^= (key1 & 0xFF), then rotate keys.
 *   Single-round (1.25.30-32, 1.25.34-35):
 *     new_key2 = ((key2 >> 1) | (key1 << 31)) ^ cryptA1
 *     new_key1 = ((key1 >> 1) | (key2 << 31)) ^ cryptB
 *   Double-round (1.25.37+):
 *     temp     = ((key2 >> 1) | (key1 << 31)) ^ cryptA1
 *     new_key2 = ((temp >> 1) | (key1 << 31)) ^ cryptA2
 *     new_key1 = ((key1 >> 1) | (key2 << 31)) ^ cryptB
 */

extern const ClientVersionInfo g_VersionTable[]; // 0x2CD3257D
extern int g_NoCrypt;
extern int g_UseEncryption;
extern int g_UseBlowfish;
extern int g_ClientVersion;
extern uint32_t g_CryptA1;
extern uint32_t g_CryptA2;
extern uint32_t g_CryptB;
extern int g_CryptDoubleRound;
extern int g_CryptPolynomial;
extern int g_AutoDetect;
extern const int g_VersionTableSize;

int Version_GetConnVer(CUserSock *us, int fallback);
const ClientVersionInfo *Version_Find(int clientEnum);
const ClientVersionInfo *Version_FindByArg(const char *arg);
const ClientVersionInfo *Version_FindByName(const char *name);
int AutoDetectXorKeys(CUserSock *this, uint32_t seed, uint8_t *encrypted, int len);
int AutoDetectPolynomial(CUserSock *this, uint32_t seed, uint8_t *encrypted, int len);
int AutoDetectPlaintext(uint8_t *data, int len);
void Version_InitPacketTables(void);
uint16_t *Version_GetPacketTable(int protocol);
void SetClientVersion(int version);

#endif /* VERSION_H_ */
