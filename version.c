/*
 * Client version negotiation and cipher selection.
 *
 * g_VersionTable[] lists every supported client revision with its
 * encryption constants and protocol quirks; at login the server
 * identifies the client, selects the matching entry, and configures
 * the inbound and outbound ciphers accordingly.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"
#include "dat.h"
#include "packet_utils.h"
#include "usersock.h"
#include "version.h"

int g_NoCrypt = 0;
int g_UseEncryption = 0;
int g_UseBlowfish = 0;
int g_ClientVersion = CLIENT_DEMO;

// Cipher constants - defaults are for 1.25.35 (single round).
// Set by SetClientVersion(). Used only in forced (non-auto-detect) mode.
uint32_t g_CryptA1 = 0x383477BC;
uint32_t g_CryptA2 = 0x383477BC;
uint32_t g_CryptB = 0x02345CC6;
int g_CryptDoubleRound = 0;
int g_CryptPolynomial = 0;

// Auto-detect mode: try all key sets on the first login packet.
int g_AutoDetect = 0;

// Per-protocol packet tables. Initialized by Version_InitPacketTables().
// g_PacketTable (packet_utils.c) is the base (UoDemo/PROTO_DEMO) table.
// These are copies with protocol-specific size patches applied.
// Custom - 1.25.x packet size table (multi-client support)
static uint16_t *g_PacketTable_125;
// Custom - 1.26.x packet size table (multi-client support)
static uint16_t *g_PacketTable_126;

/*
 * Master version table. One entry per CLIENT_* enum value.
 *
 * Columns: clientEnum, cliArg, name, a1, a2, b, cipherType,
 *          protocol, gameCipher, bfIndex, autoDetectable.
 *
 * autoDetectable=1: included in AutoDetectXorKeys trial loop.
 * Single-round (1.25.30-35), double-round (1.25.37, 1.26.0+) entries
 * are auto-detectable. Polynomial (1.25.36) uses AutoDetectPolynomial.
 *
 * Note: CLIENT_200 and CLIENT_200C share XOR keys (same a1/a2/b) but differ
 * in game cipher. Only CLIENT_200 is auto-detectable; the game connection
 * uses trial decryption to distinguish 2.0.0 (BF-only) from 2.0.0c (BF+TF).
 * The BRITANNIA_SELECT handler allocates both ciphers when it detects
 * CLIENT_200, then trial decrypt sorts it out.
 *
 * Note: GodClient 2.0.8n binary contains cipher constants
 * (A1=A2=0x2CD3257D, B=0xA37F527F) identical to regular 2.0.8 except A1
 * is one higher, but the encryption code is patched out with NOPs so they
 * are never used. Cannot be auto-detected (LSB difference lost in first key
 * rotation right-shift). Use -client god208 instead.
 */
// clang-format off
const ClientVersionInfo g_VersionTable[] = {
	// clientEnum       cliArg    name                  cryptA1     cryptA2     cryptB      cipher              proto          game       bfI auto
	{CLIENT_DEMO,       "demo",   "UoDemo",             0,          0,          0,          CIPHER_NONE,        PROTO_DEMO,    GAME_NONE, 0,  0},
	{CLIENT_12530,      "12530",  "1.25.30",            0xF1A372D5, 0xF1A372D5, 0x3A1FD527, CIPHER_SINGLE_ROUND,PROTO_125, GAME_NONE, 0,  1},
	{CLIENT_12531,      "12531",  "1.25.31",            0x395A647C, 0x395A647C, 0x0297BCC6, CIPHER_SINGLE_ROUND,PROTO_125, GAME_NONE, 0,  1},
	{CLIENT_12532,      "12532",  "1.25.32",            0x389DE58C, 0x389DE58C, 0x026950C6, CIPHER_SINGLE_ROUND,PROTO_125, GAME_NONE, 0,  1},
	{CLIENT_12533,      "12533",  "1.25.33",            0,          0,          0,          CIPHER_NONE,        PROTO_125, GAME_NONE, 0,  0},
	{CLIENT_12534,      "12534",  "1.25.34",            0x38ECEDAC, 0x38ECEDAC, 0x025720C6, CIPHER_SINGLE_ROUND,PROTO_125, GAME_NONE, 0,  1},
	{CLIENT_12535,      "12535",  "1.25.35",            0x383477BC, 0x383477BC, 0x02345CC6, CIPHER_SINGLE_ROUND,PROTO_125, GAME_NONE, 0,  1},
	{CLIENT_12536,      "12536",  "1.25.36",            0,          0,          0,          CIPHER_POLYNOMIAL,  PROTO_125, GAME_NONE, 0,  0},
	{CLIENT_12537,      "12537",  "1.25.37",            0x378757DC, 0x378757DC, 0x0595DCC6, CIPHER_DOUBLE_ROUND,PROTO_125, GAME_NONE, 0,  1},
	{CLIENT_12600,      "12600",  "1.26.0",             0x32952758, 0x32952759, 0x0A5D500B, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF,   0,  1},
	{CLIENT_12601,      "12601",  "1.26.1",             0x32AD2548, 0x32AD2549, 0x0A417C0B, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF,   1,  1},
	{CLIENT_12602,      "12602",  "1.26.2",             0x32E52F78, 0x32E52F79, 0x0A65200B, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF,   1,  1},
	{CLIENT_12603,      "12603",  "1.26.3",             0x323D3568, 0x323D3569, 0x0A095C0B, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF,   1,  1},
	{CLIENT_12604,      "12604",  "1.26.4",             0x32750718, 0x32750719, 0x0A2D100B, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF,   1,  1},
	{CLIENT_200,        "200",    "2.0.0",              0x2D13A5FC, 0x2D13A5FD, 0xA39D527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF,   1,  1},
	{CLIENT_200C,       "200c",   "2.0.0c",             0x2D13A5FC, 0x2D13A5FD, 0xA39D527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF_TF,1,  0},
	{CLIENT_201,        "201",    "2.0.1",              0x2D2BA7EC, 0x2D2BA7ED, 0xA3817E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF_TF,1,  1},
	{CLIENT_202,        "202",    "2.0.2",              0x2D63ADDC, 0x2D63ADDD, 0xA3A5227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF_TF,1,  1},
	{CLIENT_203,        "203",    "2.0.3",              0x2DBBB7CC, 0x2DBBB7CD, 0xA3C95E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_BF_TF,1,  1},
	{CLIENT_204,        "204",    "2.0.4",              0x2DF385BC, 0x2DF385BD, 0xA3ED127F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_205,        "205",    "2.0.5",              0x2C0B97AC, 0x2C0B97AD, 0xA310DE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_206,        "206",    "2.0.6",              0x2C43ED9C, 0x2C43ED9D, 0xA334227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_207,        "207",    "2.0.7",              0x2C9BC78C, 0x2C9BC78D, 0xA35BFE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_208,        "208",    "2.0.8",              0x2CD3257C, 0x2CD3257D, 0xA37F527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_209,        "209",    "2.0.9",              0x2CEB076C, 0x2CEB076D, 0xA363BE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_300,        "300",    "3.0.0",              0x2D93A5FC, 0x2D93A5FD, 0xA3DD527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_301,        "301",    "3.0.1",              0x2DABA7EC, 0x2DABA7ED, 0xA3C17E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_302,        "302",    "3.0.2",              0x2DE3ADDC, 0x2DE3ADDD, 0xA3E5227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_303,        "303",    "3.0.3",              0x2D3BB7CC, 0x2D3BB7CD, 0xA3895E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_304,        "304",    "3.0.4",              0x2D7385BC, 0x2D7385BD, 0xA3AD127F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_305,        "305",    "3.0.5",              0x2C8B97AC, 0x2C8B97AD, 0xA350DE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_306,        "306",    "3.0.6",              0x2CC3ED9C, 0x2CC3ED9D, 0xA374227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_307,        "307",    "3.0.7",              0x2C1BC78C, 0x2C1BC78D, 0xA31BFE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_308,        "308",    "3.0.8",              0x2C53257C, 0x2C53257D, 0xA33F527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_GOD208,     "god208", "2.0.8n GodClient",   0,          0,          0,          CIPHER_NONE,        PROTO_126,     GAME_NONE, 0,  0},
	{CLIENT_400,        "400",    "4.0.0",              0x2E13A5FC, 0x2E13A5FD, 0xA21D527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_401,        "401",    "4.0.1",              0x2E2BA7EC, 0x2E2BA7ED, 0xA2017E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_402,        "402",    "4.0.2",              0x2E63ADDC, 0x2E63ADDD, 0xA225227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_403,        "403",    "4.0.3",              0x2EBBB7CC, 0x2EBBB7CD, 0xA2495E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_404,        "404",    "4.0.4",              0x2EF385BC, 0x2EF385BD, 0xA26D127F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_405,        "405",    "4.0.5",              0x2F0B97AC, 0x2F0B97AD, 0xA290DE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_406,        "406",    "4.0.6",              0x2F43ED9C, 0x2F43ED9D, 0xA2B4227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_407,        "407",    "4.0.7",              0x2F9BC78C, 0x2F9BC78D, 0xA2DBFE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_408,        "408",    "4.0.8",              0x2FD3257C, 0x2FD3257D, 0xA2FF527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_409,        "409",    "4.0.9",              0x2FEB076C, 0x2FEB076D, 0xA2E3BE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_4010,       "4010",   "4.0.10",             0x2C236D5C, 0x2C236D5D, 0xA300A27F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_4011,       "4011",   "4.0.11",             0x2C7B574C, 0x2C7B574D, 0xA32D9E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_500,        "500",    "5.0.0",              0x2E93A5FC, 0x2E93A5FD, 0xA25D527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_501,        "501",    "5.0.1",              0x2EABA7EC, 0x2EABA7ED, 0xA2417E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_502,        "502",    "5.0.2",              0x2EE3ADDC, 0x2EE3ADDD, 0xA265227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_503,        "503",    "5.0.3",              0x2E3BB7CC, 0x2E3BB7CD, 0xA2095E7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_504,        "504",    "5.0.4",              0x2E7385BC, 0x2E7385BD, 0xA22D127F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_505,        "505",    "5.0.5",              0x2F8B97AC, 0x2F8B97AD, 0xA2D0DE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_506,        "506",    "5.0.6",              0x2FC3ED9C, 0x2FC3ED9D, 0xA2F4227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_5065,       "5065",   "5.0.6.5",            0x2FC3ED9D, 0x2FC3ED9D, 0xA2F4227F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_507,        "507",    "5.0.7",              0x2F1BC78D, 0x2F1BC78D, 0xA29BFE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_508,        "508",    "5.0.8",              0x2F53257D, 0x2F53257D, 0xA2BF527F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
	{CLIENT_509,        "509",    "5.0.9",              0x2F6B076D, 0x2F6B076D, 0xA2A3BE7F, CIPHER_DOUBLE_ROUND,PROTO_126,     GAME_TF,   1,  1},
};
// clang-format on
const int g_VersionTableSize = nelem(g_VersionTable);

const ClientVersionInfo *
Version_Find(int clientEnum)
{
	for (int i = 0; i < g_VersionTableSize; i++)
		if (g_VersionTable[i].clientEnum == clientEnum)
			return &g_VersionTable[i];
	return NULL;
}

const ClientVersionInfo *
Version_FindByArg(const char *arg)
{
	for (int i = 0; i < g_VersionTableSize; i++)
		if (g_VersionTable[i].cliArg && strcmp(g_VersionTable[i].cliArg, arg) == 0)
			return &g_VersionTable[i];
	return NULL;
}

const ClientVersionInfo *
Version_FindByName(const char *name)
{
	for (int i = 0; i < g_VersionTableSize; i++)
		if (strcmp(g_VersionTable[i].name, name) == 0)
			return &g_VersionTable[i];
	return NULL;
}

/*
 * AutoDetectXorKeys - try all auto-detectable key sets to find matching cipher.
 *
 * Trial-decrypts the first encrypted packet (ACCT_LOGIN_REQ, 0x80) with
 * each auto-detectable entry in g_VersionTable[]. Supports both single-round
 * (1.25.30-35) and double-round (1.25.37, 1.26.0+) ciphers. Validates by
 * checking that byte 0 decrypts to 0x80 and bytes 1-30 (account name field)
 * are printable ASCII or null.
 *
 * On match: sets per-connection cipher fields, packet table, and returns 1.
 * No match: returns 0.
 */
int
AutoDetectXorKeys(CUserSock *this, uint32_t seed, uint8_t *encrypted, int len)
{
	int k, i;
	uint32_t key1, key2, temp;
	uint8_t decrypted;

	if (len < 31)
		return 0;

	uint32_t baseKey1 = ((~seed << 16) ^ 0x13570000) | ((seed & 0xffff) ^ 0xaaaa);
	uint32_t baseKey2 = ((~seed & 0xffff0000u) ^ 0xabcd0000u) | ((seed >> 16) ^ 0x4321);

	for (k = 0; k < g_VersionTableSize; k++) {
		const ClientVersionInfo *info = &g_VersionTable[k];
		if (!info->autoDetectable)
			continue;

		key1 = baseKey1;
		key2 = baseKey2;
		int valid = 1;

		for (i = 0; i < 31; i++) {
			decrypted = encrypted[i] ^ (uint8_t)key1;

			if (i == 0 && decrypted != 0x80) {
				valid = 0;
				break;
			}
			if (i >= 1 && decrypted != 0 && (decrypted < 0x20 || decrypted > 0x7E)) {
				valid = 0;
				break;
			}

			// Rotate keys using the cipher type for this table entry
			{
				uint32_t oldKey1 = key1;
				uint32_t oldKey2 = key2;
				if (info->cipherType == CIPHER_DOUBLE_ROUND) {
					temp = ((oldKey2 >> 1) | (oldKey1 << 31)) ^ info->cryptA1;
					key2 = ((temp >> 1) | (oldKey1 << 31)) ^ info->cryptA2;
				} else {
					// Single-round
					key2 = ((oldKey2 >> 1) | (oldKey1 << 31)) ^ info->cryptA1;
				}
				key1 = ((oldKey1 >> 1) | (oldKey2 << 31)) ^ info->cryptB;
			}
		}

		if (valid) {
			this->cryptA1 = info->cryptA1;
			this->cryptA2 = info->cryptA2;
			this->cryptB = info->cryptB;
			this->cryptDoubleRound = (info->cipherType == CIPHER_DOUBLE_ROUND);
			this->detectedBfIndex = info->bfIndex;
			this->detectedKeyIndex = info->clientEnum;
			this->packetTable = Version_GetPacketTable(info->protocol);
			Log_Auth(this->addr, "auto-detected client version: %s (%s)", info->name,
			        info->gameCipher == GAME_TF      ? "Twofish"
			        : info->gameCipher == GAME_BF_TF ? "Blowfish+Twofish"
			        : info->gameCipher == GAME_BF    ? "Blowfish"
			                                         : "XOR only");
			return 1;
		}
	}

	return 0;
}

/*
 * AutoDetectPolynomial - try 1.25.36 polynomial cipher.
 *
 * Trial-decrypts using the 6 hardcoded polynomial constants (same as
 * UserSock_Decrypt's polynomial path). Same validation as AutoDetectXorKeys.
 *
 * On match: sets per-connection cipher fields, packet table, and returns 1.
 * No match: returns 0.
 */
int
AutoDetectPolynomial(CUserSock *this, uint32_t seed, uint8_t *encrypted, int len)
{
	int i;
	uint32_t key1, key2;
	uint8_t decrypted;

	if (len < 31)
		return 0;

	key1 = ((~seed << 16) ^ 0x13570000) | ((seed & 0xffff) ^ 0xaaaa);
	key2 = ((~seed & 0xffff0000u) ^ 0xabcd0000u) | ((seed >> 16) ^ 0x4321);

	for (i = 0; i < 31; i++) {
		decrypted = encrypted[i] ^ (uint8_t)key1;

		if (i == 0 && decrypted != 0x80)
			return 0;
		if (i >= 1 && decrypted != 0 && (decrypted < 0x20 || decrypted > 0x7E))
			return 0;

		// Polynomial key rotation (matches UserSock_Decrypt, VA 0x00489320)
		{
			uint32_t k1 = key1;
			uint32_t k2 = key2;
			uint32_t k2sq = k2 * k2;
			uint32_t k1sq = k1 * k1;
			key2 = (0x387FC5CCu >> (k2sq * 5 & 31)) + k2 * 0x387FC5CCu + k1sq * 0x35CE9581u + 0x07AFCC37u;
			key1 = (0x021510C6u >> (k1sq * 3 & 31)) - key2 * key2 * 0x4C3A1353u + k1 * 0x021510C6u + 0x16EF783Fu;
		}
	}

	this->cryptPolynomial = 1;
	this->detectedKeyIndex = CLIENT_12536;
	this->packetTable = Version_GetPacketTable(PROTO_125);
	Log_Auth(this->addr, "auto-detected client version: 1.25.36 (polynomial)");
	return 1;
}

/*
 * AutoDetectPlaintext - check if data after the seed is unencrypted.
 *
 * Called when AutoDetectXorKeys fails (no key matched). Checks whether
 * the data is already valid plaintext, indicating a GodClient 2.0.8n
 * connection (all encryption patched out).
 *
 * Two valid plaintext patterns:
 *   0x80 (ACCT_LOGIN_REQ): standard UI login flow.
 *     Bytes 1-30 = account name (printable ASCII or null).
 *   0x91 (POSTLOGIN): direct .con command or game connection.
 *     Bytes 1-4 = encryption key (any value).
 *     Bytes 5-34 = character name (printable ASCII or null).
 *
 * Returns 1 if plaintext detected, 0 otherwise.
 */
int
AutoDetectPlaintext(uint8_t *data, int len)
{
	int i, start, end;

	if (len < 1)
		return 0;

	if (data[0] == 0x80) {
		start = 1;
		end = 31;
	} else if (data[0] == 0x91) {
		start = 5;
		end = 35;
	} else {
		return 0;
	}

	if (len < end)
		return 0;

	for (i = start; i < end; i++) {
		if (data[i] != 0 && (data[i] < 0x20 || data[i] > 0x7E))
			return 0;
	}

	return 1;
}

/*
 * Custom - Version_InitPacketTables
 *
 * Builds per-protocol packet tables from the base g_PacketTable (UoDemo).
 * Must be called once at startup before any clients connect.
 */
void
Version_InitPacketTables(void)
{
	size_t sz = g_PacketTableSize;

	g_PacketTable_125 = (uint16_t *)malloc(sz);
	g_PacketTable_126 = (uint16_t *)malloc(sz);
	memcpy(g_PacketTable_125, g_PacketTable, sz);
	memcpy(g_PacketTable_126, g_PacketTable, sz);

	// PROTO_125 patches (1.25.30+)
	g_PacketTable_125[5 * 0x0B] = 266;
	g_PacketTable_125[5 * 0x10] = 215;
	g_PacketTable_125[5 * 0x1F] = 8;
	g_PacketTable_125[5 * 0x59] = 0x8000;

	// PROTO_126 patches (1.26.0+) - includes all 125 patches
	g_PacketTable_126[5 * 0x0B] = 266;
	g_PacketTable_126[5 * 0x10] = 215;
	g_PacketTable_126[5 * 0x1F] = 8;
	g_PacketTable_126[5 * 0x59] = 0x8000;
	g_PacketTable_126[5 * 0x00] = 104;
	g_PacketTable_126[5 * 0x02] = 7;
	g_PacketTable_126[5 * 0x93] = 99;
}

/*
 * Custom - Version_GetPacketTable
 *
 * Returns the packet table for a given protocol.
 */
uint16_t *
Version_GetPacketTable(int protocol)
{
	switch (protocol) {
	case PROTO_125:
		return g_PacketTable_125;
	case PROTO_126:
		return g_PacketTable_126;
	default:
		return g_PacketTable;
	}
}

/*
 * SetClientVersion - configure protocol and encryption for a client version.
 *
 * Looks up the version in g_VersionTable[] and applies cipher constants,
 * cipher mode, encryption enable, and packet table patches. Call once
 * during init before any clients connect.
 */
void
SetClientVersion(int version)
{
	g_ClientVersion = version;
	g_AutoDetect = 0;

	if (version == CLIENT_126_AUTO) {
		g_UseEncryption = 1;
		g_UseBlowfish = 1;
		g_AutoDetect = 1;
		return;
	}

	const ClientVersionInfo *info = Version_Find(version);
	if (!info) {
		fprintf(stderr, "fatal: unknown client version %d\n", version);
		exit(1);
	}

	g_UseEncryption = (info->cipherType != CIPHER_NONE);
	g_UseBlowfish = (info->protocol == PROTO_126);
	g_CryptDoubleRound = (info->cipherType == CIPHER_DOUBLE_ROUND);
	g_CryptPolynomial = (info->cipherType == CIPHER_POLYNOMIAL);

	if (info->cipherType == CIPHER_SINGLE_ROUND || info->cipherType == CIPHER_DOUBLE_ROUND) {
		g_CryptA1 = info->cryptA1;
		g_CryptA2 = info->cryptA2;
		g_CryptB = info->cryptB;
	}
}

/*
 * Custom - Version_GetConnVer
 *
 * Returns the CLIENT_* enum for a connection.
 * In auto-detect mode, uses the per-connection detectedKeyIndex.
 * When auto-detect hasn't resolved (-1), defaults to fallback.
 * In fixed mode, returns g_ClientVersion.
 */
int
Version_GetConnVer(CUserSock *us, int fallback)
{
	if (us == NULL)
		return fallback;
	if (!g_AutoDetect)
		return g_ClientVersion;
	if (us->detectedKeyIndex < 0)
		return fallback;
	return us->detectedKeyIndex;
}
