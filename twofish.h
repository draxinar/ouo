#ifndef TWOFISH_H_
#define TWOFISH_H_

/*
 * Twofish cipher for UO game connections (2.0.4+).
 *
 * The client uses Twofish in ECB as a streaming cipher: a 256-byte buffer
 * is seeded with the identity bytes 0x00-0xFF, encrypted once, and XOR'd
 * byte-by-byte with incoming data. When the buffer is exhausted (pos
 * reaches 256), it is re-encrypted in place. Encryption by client
 * version: 1.26.0-2.0.0 Blowfish only (GAME_BF), 2.0.0c-2.0.3 Blowfish
 * + Twofish (GAME_BF_TF), 2.0.4+ Twofish only (GAME_TF). All binary
 * addresses below are from client.exe 2.0.8.
 */

#include <stdint.h>

/*
 * Streaming Twofish cipher context. Layout matches the client's 0x1250
 * byte object: keyInstance (key schedule, S-boxes) at 0x0000-0x1123,
 * cipherInstance (mode, IV) at 0x1124-0x114B, the 256-byte keystream
 * buffer at 0x114C-0x124B, and the stream position counter at 0x124C.
 */
__extension__ typedef struct TwofishCipher {
	/* keyInstance: 0x0000 */
	uint8_t ki_direction;       /* 0x0000 */
	uint8_t ki_pad1[3];         /* 0x0003 */
	int32_t ki_keyLen;          /* 0x0004 - bits, rounded to 64 */
	uint8_t ki_keyMaterial[64]; /* 0x0008 - raw key hex or zeros */
	uint8_t ki_keySig;          /* 0x0048 */
	uint8_t ki_pad2[7];         /* 0x0049 */
	int32_t ki_numRounds;       /* 0x0050 */
	uint32_t ki_key32[8];       /* 0x0054 - actual key dwords */
	uint32_t ki_sboxKeys[4];    /* 0x0074 - Reed-Solomon derived */
	uint32_t ki_subKeys[40];    /* 0x0084 - round subkeys + whitening */
	uint32_t ki_sBox[1024];     /* 0x0124 - interleaved key-dependent S-box */
	/* cipherInstance: 0x1124 */
	uint8_t ci_mode;      /* 0x1124 */
	uint8_t ci_IV[16];    /* 0x1125 */
	uint8_t ci_pad3[3];   /* 0x1135 */
	int32_t ci_cipherSig; /* 0x1138 */
	int32_t ci_iv32[4];   /* 0x113C */
	/* streaming state: 0x114C */
	uint8_t streamBuf[256]; /* 0x114C */
	int32_t streamPos;      /* 0x124C */
} TwofishCipher;                /* 0x1250 total */

/*
 * Server-to-client XOR cipher keyed by MD5(streamBuf). The client stores
 * the same 16-byte hash at [conn + 0x00031370] and XORs received bytes
 * with hash[pos % 16] before Huffman decompression.
 */
__extension__ typedef struct TwofishSendCipher {
	uint8_t md5Hash[16]; /* MD5(streamBuf) */
	uint32_t pos;        /* rotating position counter */
} TwofishSendCipher;

void TwofishSendCipher_Init(TwofishSendCipher *sc, TwofishCipher *tc); // 0x0042D209
void TwofishSendCipher_Encrypt(TwofishSendCipher *sc, uint8_t *buf, int len); // 0x0042D8E0
void TwofishCipher_Decrypt(TwofishCipher *tc, uint8_t *buf, int len); // 0x004D8D30
void reKey(TwofishCipher *ki); // 0x004EB2D0
int blockEncrypt(TwofishCipher *ci, TwofishCipher *ki, const uint8_t *input, int inputLen, uint8_t *outBuffer); // 0x004EBFF0

#endif /* TWOFISH_H_ */
