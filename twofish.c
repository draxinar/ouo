/*
 * Twofish cipher for UO client 2.0.4+ game connections.
 *
 * Client-to-server uses Twofish with a per-connection key schedule;
 * server-to-client is an XOR stream seeded by MD5 over the same key
 * material. Both ciphers are wired up by version.c at login.
 */

#include <stdint.h>
#include <string.h>

#include "dat.h"
#include "twofish.h"

__extension__ typedef struct MD5_CTX MD5_CTX;

static void MD5Init(MD5_CTX *ctx); // 0x004B8BD0
static void MD5Update(MD5_CTX *ctx, const uint8_t *input, unsigned int inputLen); // 0x004B8C00
static void MD5Final(uint8_t digest[16], MD5_CTX *ctx); // 0x004B8CC0
static void MD5Transform(uint32_t state[4], const uint8_t block[64]); // 0x004B8D80
static uint32_t RS_mult(uint32_t k0, uint32_t k1); // 0x004EAD80
static void Precomp_MDS_setup(void); // 0x004EAE10
static uint32_t h_func(uint32_t x, const uint32_t *key, int k64Cnt);
static inline uint32_t g_func(uint32_t x, const uint32_t *sBox);
static void MD5_Decode(uint32_t *output, const uint8_t *input, unsigned int len);
static void MD5_Encode(uint8_t *output, const uint32_t *input, unsigned int len);

/*
 * 0x00564410 - client.exe 2.0.8 - Q0
 *
 * Standard Twofish Q0 permutation table.
 */
// clang-format off
static const uint8_t Q0[256] = {
	0xA9,0x67,0xB3,0xE8,0x04,0xFD,0xA3,0x76,
	0x9A,0x92,0x80,0x78,0xE4,0xDD,0xD1,0x38,
	0x0D,0xC6,0x35,0x98,0x18,0xF7,0xEC,0x6C,
	0x43,0x75,0x37,0x26,0xFA,0x13,0x94,0x48,
	0xF2,0xD0,0x8B,0x30,0x84,0x54,0xDF,0x23,
	0x19,0x5B,0x3D,0x59,0xF3,0xAE,0xA2,0x82,
	0x63,0x01,0x83,0x2E,0xD9,0x51,0x9B,0x7C,
	0xA6,0xEB,0xA5,0xBE,0x16,0x0C,0xE3,0x61,
	0xC0,0x8C,0x3A,0xF5,0x73,0x2C,0x25,0x0B,
	0xBB,0x4E,0x89,0x6B,0x53,0x6A,0xB4,0xF1,
	0xE1,0xE6,0xBD,0x45,0xE2,0xF4,0xB6,0x66,
	0xCC,0x95,0x03,0x56,0xD4,0x1C,0x1E,0xD7,
	0xFB,0xC3,0x8E,0xB5,0xE9,0xCF,0xBF,0xBA,
	0xEA,0x77,0x39,0xAF,0x33,0xC9,0x62,0x71,
	0x81,0x79,0x09,0xAD,0x24,0xCD,0xF9,0xD8,
	0xE5,0xC5,0xB9,0x4D,0x44,0x08,0x86,0xE7,
	0xA1,0x1D,0xAA,0xED,0x06,0x70,0xB2,0xD2,
	0x41,0x7B,0xA0,0x11,0x31,0xC2,0x27,0x90,
	0x20,0xF6,0x60,0xFF,0x96,0x5C,0xB1,0xAB,
	0x9E,0x9C,0x52,0x1B,0x5F,0x93,0x0A,0xEF,
	0x91,0x85,0x49,0xEE,0x2D,0x4F,0x8F,0x3B,
	0x47,0x87,0x6D,0x46,0xD6,0x3E,0x69,0x64,
	0x2A,0xCE,0xCB,0x2F,0xFC,0x97,0x05,0x7A,
	0xAC,0x7F,0xD5,0x1A,0x4B,0x0E,0xA7,0x5A,
	0x28,0x14,0x3F,0x29,0x88,0x3C,0x4C,0x02,
	0xB8,0xDA,0xB0,0x17,0x55,0x1F,0x8A,0x7D,
	0x57,0xC7,0x8D,0x74,0xB7,0xC4,0x9F,0x72,
	0x7E,0x15,0x22,0x12,0x58,0x07,0x99,0x34,
	0x6E,0x50,0xDE,0x68,0x65,0xBC,0xDB,0xF8,
	0xC8,0xA8,0x2B,0x40,0xDC,0xFE,0x32,0xA4,
	0xCA,0x10,0x21,0xF0,0xD3,0x5D,0x0F,0x00,
	0x6F,0x9D,0x36,0x42,0x4A,0x5E,0xC1,0xE0,
};
// clang-format on

/*
 * 0x00564510 - client.exe 2.0.8 - Q1
 *
 * Standard Twofish Q1 permutation table.
 */
// clang-format off
static const uint8_t Q1[256] = {
	0x75,0xF3,0xC6,0xF4,0xDB,0x7B,0xFB,0xC8,
	0x4A,0xD3,0xE6,0x6B,0x45,0x7D,0xE8,0x4B,
	0xD6,0x32,0xD8,0xFD,0x37,0x71,0xF1,0xE1,
	0x30,0x0F,0xF8,0x1B,0x87,0xFA,0x06,0x3F,
	0x5E,0xBA,0xAE,0x5B,0x8A,0x00,0xBC,0x9D,
	0x6D,0xC1,0xB1,0x0E,0x80,0x5D,0xD2,0xD5,
	0xA0,0x84,0x07,0x14,0xB5,0x90,0x2C,0xA3,
	0xB2,0x73,0x4C,0x54,0x92,0x74,0x36,0x51,
	0x38,0xB0,0xBD,0x5A,0xFC,0x60,0x62,0x96,
	0x6C,0x42,0xF7,0x10,0x7C,0x28,0x27,0x8C,
	0x13,0x95,0x9C,0xC7,0x24,0x46,0x3B,0x70,
	0xCA,0xE3,0x85,0xCB,0x11,0xD0,0x93,0xB8,
	0xA6,0x83,0x20,0xFF,0x9F,0x77,0xC3,0xCC,
	0x03,0x6F,0x08,0xBF,0x40,0xE7,0x2B,0xE2,
	0x79,0x0C,0xAA,0x82,0x41,0x3A,0xEA,0xB9,
	0xE4,0x9A,0xA4,0x97,0x7E,0xDA,0x7A,0x17,
	0x66,0x94,0xA1,0x1D,0x3D,0xF0,0xDE,0xB3,
	0x0B,0x72,0xA7,0x1C,0xEF,0xD1,0x53,0x3E,
	0x8F,0x33,0x26,0x5F,0xEC,0x76,0x2A,0x49,
	0x81,0x88,0xEE,0x21,0xC4,0x1A,0xEB,0xD9,
	0xC5,0x39,0x99,0xCD,0xAD,0x31,0x8B,0x01,
	0x18,0x23,0xDD,0x1F,0x4E,0x2D,0xF9,0x48,
	0x4F,0xF2,0x65,0x8E,0x78,0x5C,0x58,0x19,
	0x8D,0xE5,0x98,0x57,0x67,0x7F,0x05,0x64,
	0xAF,0x63,0xB6,0xFE,0xF5,0xB7,0x3C,0xA5,
	0xCE,0xE9,0x68,0x44,0xE0,0x4D,0x43,0x69,
	0x29,0x2E,0xAC,0x15,0x59,0xA8,0x0A,0x9E,
	0x6E,0x47,0xDF,0x34,0x35,0x6A,0xCF,0xDC,
	0x22,0xC9,0xC0,0x9B,0x89,0xD4,0xED,0xAB,
	0x12,0xA2,0x0D,0x52,0xBB,0x02,0x2F,0xA9,
	0xD7,0x61,0x1E,0xB4,0x50,0x04,0xF6,0xC2,
	0x16,0x25,0x86,0x56,0x55,0x09,0xBE,0x91,
};
// clang-format on

/*
 * 0x00564614 - client.exe 2.0.8 - numRoundsTable
 *
 * Twofish round count indexed by keyLen/64. Index 0 is unused; 128-bit,
 * 192-bit and 256-bit keys all use 16 rounds.
 */
__attribute__((unused)) static const int numRoundsTable[5] = { 0, 16, 16, 16, 16 };

/*
 * P_ij permutation choice indices (standard Twofish) used by the
 * h-function at 0x004EB2D0. P_XY selects Q0 or Q1 for byte X at layer Y;
 * layer 0 = MDS precomp, 1 = outermost explicit, 2..4 = inner layers.
 */
#define P_00 1
#define P_01 0
#define P_02 0
#define P_03 1
#define P_04 1
#define P_10 0
#define P_11 0
#define P_12 1
#define P_13 1
#define P_14 0
#define P_20 1
#define P_21 1
#define P_22 0
#define P_23 0
#define P_24 0
#define P_30 0
#define P_31 1
#define P_32 1
#define P_33 0
#define P_34 1

// Custom - Q permutation table pointers indexed by P_ij value
static const uint8_t *const QTab[2] = { Q0, Q1 };

/* GF(2^8) polynomials */
#define MDS_GF_FDBK 0x169 /* x^8+x^6+x^5+x^3+1 for MDS matrix */
#define RS_GF_FDBK  0x14D  /* x^8+x^6+x^3+x^2+1 for RS matrix */

/* MDS matrix GF multiply helpers inlined by Precomp_MDS_setup (0x004EAE10) */
#define LFSR1(x) (((x) >> 1) ^ (((x) & 1) ? (MDS_GF_FDBK / 2) : 0))
#define LFSR2(x) (((x) >> 2) ^ (((x) & 2) ? (MDS_GF_FDBK / 2) : 0) ^ (((x) & 1) ? (MDS_GF_FDBK / 4) : 0))

#define Mx_X(x) ((uint8_t)((x) ^ LFSR2(x)))            /* multiply by 0x5B */
#define Mx_Y(x) ((uint8_t)((x) ^ LFSR2(x) ^ LFSR1(x))) /* multiply by 0xEF */

/*
 * 0x00D70440-0x00D7143F - client.exe 2.0.8 - MDS
 *
 * Precomputed Maximum Distance Separable lookup tables used by the
 * Twofish h-function.
 */
static uint32_t MDS[4][256];

/*
 * 0x00564610 - client.exe 2.0.8 - needMDSinit
 *
 * Set until Precomp_MDS_setup populates MDS on the first reKey call.
 */
static int needMDSinit = 1;

/* ROL/ROR bit rotation */
#define ROL(x, n) (((x) << ((n) & 31)) | ((x) >> (32 - ((n) & 31))))
#define ROR(x, n) (((x) >> ((n) & 31)) | ((x) << (32 - ((n) & 31))))

/* Byte extraction */
#define B0(x) ((uint8_t)(x))
#define B1(x) ((uint8_t)((x) >> 8))
#define B2(x) ((uint8_t)((x) >> 16))
#define B3(x) ((uint8_t)((x) >> 24))

/*
 * MD5 context used by the client 2.0.8 Twofish send cipher. Layout
 * matches the RSA Data Security reference implementation (RFC 1321):
 *   0x00: state[4]   - message digest state (A,B,C,D)
 *   0x10: count[2]   - 64-bit bit count
 *   0x18: buffer[64] - input accumulation buffer
 */

__extension__ typedef struct MD5_CTX {
	uint32_t state[4];  /* 0x00 */
	uint32_t count[2];  /* 0x10 */
	uint8_t buffer[64]; /* 0x18 */
} MD5_CTX;                  /* 0x58 total */

/*
 * 0x00557078 - client.exe 2.0.8 - MD5_PADDING
 *
 * RFC 1321 MD5 padding constant applied by MD5Final to bring the
 * input length up to a multiple of 64 bytes.
 */
// clang-format off
static const uint8_t MD5_PADDING[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
// clang-format on

/* Round functions inlined by MD5Transform (0x004B8D80) */
#define MD5_F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~(z))))

#define MD5_FF(a, b, c, d, x, s, ac)                                \
	{                                                           \
		(a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROL((a), (s));                                \
		(a) += (b);                                         \
	}
#define MD5_GG(a, b, c, d, x, s, ac)                                \
	{                                                           \
		(a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROL((a), (s));                                \
		(a) += (b);                                         \
	}
#define MD5_HH(a, b, c, d, x, s, ac)                                \
	{                                                           \
		(a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROL((a), (s));                                \
		(a) += (b);                                         \
	}
#define MD5_II(a, b, c, d, x, s, ac)                                \
	{                                                           \
		(a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac); \
		(a) = ROL((a), (s));                                \
		(a) += (b);                                         \
	}

/* Shift amounts */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

/*
 * 0x0042D209 - client.exe 2.0.8 - TwofishSendCipher_Init
 *
 * Derives a server-to-client XOR key by hashing the Twofish keystream
 * buffer with MD5 and resets the rotating position counter. The client
 * stores the same 16-byte hash at [conn + 0x00031370].
 */
void
TwofishSendCipher_Init(TwofishSendCipher *sc, TwofishCipher *tc)
{
	MD5_CTX ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, tc->streamBuf, 256);
	MD5Final(sc->md5Hash, &ctx);
	sc->pos = 0;
}

/*
 * 0x0042D8E0 - client.exe 2.0.8 - TwofishSendCipher_Encrypt
 *
 * XOR-encrypts len bytes in-place with the 16-byte MD5 key, rotating
 * through hash[pos % 16]. Inverse of the client's receive-side XOR.
 */
void
TwofishSendCipher_Encrypt(TwofishSendCipher *sc, uint8_t *buf, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		buf[i] ^= sc->md5Hash[sc->pos % 16];
		sc->pos++;
	}
}

/*
 * 0x004B8BD0 - client.exe 2.0.8 - MD5Init
 *
 * Initializes the MD5 context with the RFC 1321 initial state.
 */
static void
MD5Init(MD5_CTX *ctx)
{
	ctx->count[0] = 0;
	ctx->count[1] = 0;
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xefcdab89;
	ctx->state[2] = 0x98badcfe;
	ctx->state[3] = 0x10325476;
}

/*
 * 0x004B8C00 - client.exe 2.0.8 - MD5Update
 *
 * Updates context with inputLen bytes from input. Calls MD5Transform
 * for each complete 64-byte block accumulated.
 */
static void
MD5Update(MD5_CTX *ctx, const uint8_t *input, unsigned int inputLen)
{
	unsigned int i, index, partLen;

	// Compute number of bytes mod 64
	index = (unsigned int)((ctx->count[0] >> 3) & 0x3F);

	// Update number of bits
	if ((ctx->count[0] += ((uint32_t)inputLen << 3)) < ((uint32_t)inputLen << 3))
		ctx->count[1]++;
	ctx->count[1] += ((uint32_t)inputLen >> 29);

	partLen = 64 - index;

	// Transform as many times as possible
	if (inputLen >= partLen) {
		memcpy(&ctx->buffer[index], input, partLen);
		MD5Transform(ctx->state, ctx->buffer);

		for (i = partLen; i + 63 < inputLen; i += 64)
			MD5Transform(ctx->state, &input[i]);

		index = 0;
	} else {
		i = 0;
	}

	// Buffer remaining input
	memcpy(&ctx->buffer[index], &input[i], inputLen - i);
}

/*
 * 0x004B8CC0 - client.exe 2.0.8 - MD5Final
 *
 * Ends an MD5 message-digest operation, writing the digest and
 * zeroizing the context (rep stosd 0x16 dwords = 88 bytes).
 */
static void
MD5Final(uint8_t digest[16], MD5_CTX *ctx)
{
	uint8_t bits[8];
	unsigned int index, padLen;

	// Save number of bits
	MD5_Encode(bits, ctx->count, 8);

	// Pad out to 56 mod 64
	index = (unsigned int)((ctx->count[0] >> 3) & 0x3f);
	padLen = (index < 56) ? (56 - index) : (120 - index);
	MD5Update(ctx, MD5_PADDING, padLen);

	// Append length (before padding)
	MD5Update(ctx, bits, 8);

	// Store state in digest
	MD5_Encode(digest, ctx->state, 16);

	// Zeroize sensitive information
	memset(ctx, 0, sizeof(*ctx));
}

/*
 * 0x004B8D80 - client.exe 2.0.8 - MD5Transform
 *
 * MD5 basic transformation. Transforms state based on block.
 * Fully unrolled, 64 rounds.
 */
static void
MD5Transform(uint32_t state[4], const uint8_t block[64])
{
	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t x[16];

	MD5_Decode(x, block, 64);

	// Round 1 (F function)
	MD5_FF(a, b, c, d, x[0], S11, 0xd76aa478);
	MD5_FF(d, a, b, c, x[1], S12, 0xe8c7b756);
	MD5_FF(c, d, a, b, x[2], S13, 0x242070db);
	MD5_FF(b, c, d, a, x[3], S14, 0xc1bdceee);
	MD5_FF(a, b, c, d, x[4], S11, 0xf57c0faf);
	MD5_FF(d, a, b, c, x[5], S12, 0x4787c62a);
	MD5_FF(c, d, a, b, x[6], S13, 0xa8304613);
	MD5_FF(b, c, d, a, x[7], S14, 0xfd469501);
	MD5_FF(a, b, c, d, x[8], S11, 0x698098d8);
	MD5_FF(d, a, b, c, x[9], S12, 0x8b44f7af);
	MD5_FF(c, d, a, b, x[10], S13, 0xffff5bb1);
	MD5_FF(b, c, d, a, x[11], S14, 0x895cd7be);
	MD5_FF(a, b, c, d, x[12], S11, 0x6b901122);
	MD5_FF(d, a, b, c, x[13], S12, 0xfd987193);
	MD5_FF(c, d, a, b, x[14], S13, 0xa679438e);
	MD5_FF(b, c, d, a, x[15], S14, 0x49b40821);

	// Round 2 (G function)
	MD5_GG(a, b, c, d, x[1], S21, 0xf61e2562);
	MD5_GG(d, a, b, c, x[6], S22, 0xc040b340);
	MD5_GG(c, d, a, b, x[11], S23, 0x265e5a51);
	MD5_GG(b, c, d, a, x[0], S24, 0xe9b6c7aa);
	MD5_GG(a, b, c, d, x[5], S21, 0xd62f105d);
	MD5_GG(d, a, b, c, x[10], S22, 0x02441453);
	MD5_GG(c, d, a, b, x[15], S23, 0xd8a1e681);
	MD5_GG(b, c, d, a, x[4], S24, 0xe7d3fbc8);
	MD5_GG(a, b, c, d, x[9], S21, 0x21e1cde6);
	MD5_GG(d, a, b, c, x[14], S22, 0xc33707d6);
	MD5_GG(c, d, a, b, x[3], S23, 0xf4d50d87);
	MD5_GG(b, c, d, a, x[8], S24, 0x455a14ed);
	MD5_GG(a, b, c, d, x[13], S21, 0xa9e3e905);
	MD5_GG(d, a, b, c, x[2], S22, 0xfcefa3f8);
	MD5_GG(c, d, a, b, x[7], S23, 0x676f02d9);
	MD5_GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);

	// Round 3 (H function)
	MD5_HH(a, b, c, d, x[5], S31, 0xfffa3942);
	MD5_HH(d, a, b, c, x[8], S32, 0x8771f681);
	MD5_HH(c, d, a, b, x[11], S33, 0x6d9d6122);
	MD5_HH(b, c, d, a, x[14], S34, 0xfde5380c);
	MD5_HH(a, b, c, d, x[1], S31, 0xa4beea44);
	MD5_HH(d, a, b, c, x[4], S32, 0x4bdecfa9);
	MD5_HH(c, d, a, b, x[7], S33, 0xf6bb4b60);
	MD5_HH(b, c, d, a, x[10], S34, 0xbebfbc70);
	MD5_HH(a, b, c, d, x[13], S31, 0x289b7ec6);
	MD5_HH(d, a, b, c, x[0], S32, 0xeaa127fa);
	MD5_HH(c, d, a, b, x[3], S33, 0xd4ef3085);
	MD5_HH(b, c, d, a, x[6], S34, 0x04881d05);
	MD5_HH(a, b, c, d, x[9], S31, 0xd9d4d039);
	MD5_HH(d, a, b, c, x[12], S32, 0xe6db99e5);
	MD5_HH(c, d, a, b, x[15], S33, 0x1fa27cf8);
	MD5_HH(b, c, d, a, x[2], S34, 0xc4ac5665);

	// Round 4 (I function)
	MD5_II(a, b, c, d, x[0], S41, 0xf4292244);
	MD5_II(d, a, b, c, x[7], S42, 0x432aff97);
	MD5_II(c, d, a, b, x[14], S43, 0xab9423a7);
	MD5_II(b, c, d, a, x[5], S44, 0xfc93a039);
	MD5_II(a, b, c, d, x[12], S41, 0x655b59c3);
	MD5_II(d, a, b, c, x[3], S42, 0x8f0ccc92);
	MD5_II(c, d, a, b, x[10], S43, 0xffeff47d);
	MD5_II(b, c, d, a, x[1], S44, 0x85845dd1);
	MD5_II(a, b, c, d, x[8], S41, 0x6fa87e4f);
	MD5_II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
	MD5_II(c, d, a, b, x[6], S43, 0xa3014314);
	MD5_II(b, c, d, a, x[13], S44, 0x4e0811a1);
	MD5_II(a, b, c, d, x[4], S41, 0xf7537e82);
	MD5_II(d, a, b, c, x[11], S42, 0xbd3af235);
	MD5_II(c, d, a, b, x[2], S43, 0x2ad7d2bb);
	MD5_II(b, c, d, a, x[9], S44, 0xeb86d391);

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

	// Zeroize sensitive information
	memset(x, 0, sizeof(x));
}

/*
 * 0x004D8D30 - client.exe 2.0.8 - TwofishCipher_Decrypt
 *
 * Decrypts len bytes in-place by XORing against the streaming keystream
 * buffer, re-encrypting the buffer whenever its 256 bytes are exhausted.
 */
void
TwofishCipher_Decrypt(TwofishCipher *tc, uint8_t *buf, int len)
{
	uint8_t outBuf[256];
	int i;

	for (i = 0; i < len; i++) {
		if (tc->streamPos >= 256) {
			// Re-encrypt the keystream buffer
			blockEncrypt(tc, tc, tc->streamBuf, 256 * 8, outBuf);
			memcpy(tc->streamBuf, outBuf, 256);
			tc->streamPos = 0;
		}
		buf[i] ^= tc->streamBuf[tc->streamPos++];
	}
}

/*
 * 0x004EAD80 - client.exe 2.0.8 - RS_mult
 *
 * Reed-Solomon remainder over GF(2^8) used to derive the S-box keys
 * from the user key during key schedule.
 */
static uint32_t
RS_mult(uint32_t k0, uint32_t k1)
{
	uint32_t r = 0;
	int i, j;
	for (i = 0; i < 2; i++) {
		r ^= (i) ? k0 : k1;
		for (j = 0; j < 4; j++) {
			uint8_t b = (uint8_t)(r >> 24);
			uint32_t g2 = (uint32_t)(((b << 1) ^ ((b & 0x80) ? RS_GF_FDBK : 0)) & 0xFF);
			uint32_t g3 = (uint32_t)(((b >> 1) ^ ((b & 1) ? RS_GF_FDBK >> 1 : 0)) ^ g2);
			r = (r << 8) ^ (g3 << 24) ^ (g2 << 16) ^ (g3 << 8) ^ b;
		}
	}
	return r;
}

/*
 * 0x004EAE10 - client.exe 2.0.8 - Precomp_MDS_setup
 *
 * Populates the four 256-entry MDS lookup tables from the Q0/Q1
 * permutations. Runs once, before the first h-function call.
 */
static void
Precomp_MDS_setup(void)
{
	int i;
	uint8_t m1[2], mX[2], mY[2];

	for (i = 0; i < 256; i++) {
		m1[0] = Q0[i];
		mX[0] = Mx_X(m1[0]);
		mY[0] = Mx_Y(m1[0]);

		m1[1] = Q1[i];
		mX[1] = Mx_X(m1[1]);
		mY[1] = Mx_Y(m1[1]);

		MDS[0][i] = (uint32_t)m1[P_00] | ((uint32_t)mX[P_00] << 8) | ((uint32_t)mY[P_00] << 16) | ((uint32_t)mY[P_00] << 24);
		MDS[1][i] = (uint32_t)mY[P_10] | ((uint32_t)mY[P_10] << 8) | ((uint32_t)mX[P_10] << 16) | ((uint32_t)m1[P_10] << 24);
		MDS[2][i] = (uint32_t)mX[P_20] | ((uint32_t)mY[P_20] << 8) | ((uint32_t)m1[P_20] << 16) | ((uint32_t)mY[P_20] << 24);
		MDS[3][i] = (uint32_t)mX[P_30] | ((uint32_t)m1[P_30] << 8) | ((uint32_t)mY[P_30] << 16) | ((uint32_t)mX[P_30] << 24);
	}
	needMDSinit = 0;
}

/*
 * 0x004EB2D0 - client.exe 2.0.8 - reKey
 *
 * Computes the round subkeys and key-dependent S-box for the given
 * cipher context. P_XY selects Q0 or Q1 at byte position X, layer Y
 * (0=MDS, 1=outer, 2..4=inner).
 */
void
reKey(TwofishCipher *ki)
{
	int i, k64Cnt, subkeyCnt;
	uint32_t A, B;
	uint32_t k32e[4], k32o[4]; /* even/odd key dwords for subkey h() */
	uint32_t sboxKeys[4];
	uint8_t key8[32]; /* sboxKey bytes for S-box construction */

	if (needMDSinit)
		Precomp_MDS_setup();

	k64Cnt = (ki->ki_keyLen + 63) / 64;
	subkeyCnt = ki->ki_numRounds * 2 + 8;

	// Split key into even/odd dwords, compute sboxKeys via Reed-Solomon
	for (i = 0; i < k64Cnt; i++) {
		k32e[i] = ki->ki_key32[2 * i];
		k32o[i] = ki->ki_key32[2 * i + 1];
		sboxKeys[k64Cnt - 1 - i] = RS_mult(k32e[i], k32o[i]);
	}
	memcpy(ki->ki_sboxKeys, sboxKeys, sizeof(sboxKeys));

	// Store sboxKeys as bytes for S-box construction
	memcpy(key8, sboxKeys, sizeof(sboxKeys));

	// Compute round subkeys: A uses even key dwords, B uses odd
	for (i = 0; i < subkeyCnt / 2; i++) {
		A = h_func((uint32_t)(2 * i) * 0x01010101u, k32e, k64Cnt);
		B = h_func((uint32_t)(2 * i + 1) * 0x01010101u, k32o, k64Cnt);
		B = ROL(B, 8);
		ki->ki_subKeys[2 * i] = A + B;
		ki->ki_subKeys[2 * i + 1] = ROL(A + 2 * B, 9);
	}

	/*
	 * Compute key-dependent S-box (interleaved layout, 1024 entries).
	 * P_XY: X=byte, Y=layer. Innermost layer first, outermost (layer 1)
	 * feeds into MDS table which already contains layer 0 Q permutation.
	 *
	 * g(x) = sBox[2*B0(x)] ^ sBox[2*B1(x)+1]
	 *       ^ sBox[512+2*B2(x)] ^ sBox[512+2*B3(x)+1] */
	{
		uint8_t qa[256];
		int j;

		/* Column 0: sBox[2*i] - byte position 0 */
		switch (k64Cnt) {
		case 4:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_04][j] ^ key8[12];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_03][qa[j]] ^ key8[8];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_02][qa[j]] ^ key8[4];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j] = MDS[0][QTab[P_01][qa[j]] ^ key8[0]];
			break;
		case 3:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_03][j] ^ key8[8];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_02][qa[j]] ^ key8[4];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j] = MDS[0][QTab[P_01][qa[j]] ^ key8[0]];
			break;
		case 2:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_02][j] ^ key8[4];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j] = MDS[0][QTab[P_01][qa[j]] ^ key8[0]];
			break;
		case 1:
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j] = MDS[0][QTab[P_01][j] ^ key8[0]];
			break;
		}

		/* Column 1: sBox[2*i+1] - byte position 1 */
		switch (k64Cnt) {
		case 4:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_14][j] ^ key8[13];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_13][qa[j]] ^ key8[9];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_12][qa[j]] ^ key8[5];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j + 1] = MDS[1][QTab[P_11][qa[j]] ^ key8[1]];
			break;
		case 3:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_13][j] ^ key8[9];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_12][qa[j]] ^ key8[5];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j + 1] = MDS[1][QTab[P_11][qa[j]] ^ key8[1]];
			break;
		case 2:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_12][j] ^ key8[5];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j + 1] = MDS[1][QTab[P_11][qa[j]] ^ key8[1]];
			break;
		case 1:
			for (j = 0; j < 256; j++)
				ki->ki_sBox[2 * j + 1] = MDS[1][QTab[P_11][j] ^ key8[1]];
			break;
		}

		/* Column 2: sBox[512+2*i] - byte position 2 */
		switch (k64Cnt) {
		case 4:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_24][j] ^ key8[14];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_23][qa[j]] ^ key8[10];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_22][qa[j]] ^ key8[6];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j] = MDS[2][QTab[P_21][qa[j]] ^ key8[2]];
			break;
		case 3:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_23][j] ^ key8[10];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_22][qa[j]] ^ key8[6];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j] = MDS[2][QTab[P_21][qa[j]] ^ key8[2]];
			break;
		case 2:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_22][j] ^ key8[6];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j] = MDS[2][QTab[P_21][qa[j]] ^ key8[2]];
			break;
		case 1:
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j] = MDS[2][QTab[P_21][j] ^ key8[2]];
			break;
		}

		/* Column 3: sBox[512+2*i+1] - byte position 3 */
		switch (k64Cnt) {
		case 4:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_34][j] ^ key8[15];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_33][qa[j]] ^ key8[11];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_32][qa[j]] ^ key8[7];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j + 1] = MDS[3][QTab[P_31][qa[j]] ^ key8[3]];
			break;
		case 3:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_33][j] ^ key8[11];
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_32][qa[j]] ^ key8[7];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j + 1] = MDS[3][QTab[P_31][qa[j]] ^ key8[3]];
			break;
		case 2:
			for (j = 0; j < 256; j++)
				qa[j] = QTab[P_32][j] ^ key8[7];
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j + 1] = MDS[3][QTab[P_31][qa[j]] ^ key8[3]];
			break;
		case 1:
			for (j = 0; j < 256; j++)
				ki->ki_sBox[512 + 2 * j + 1] = MDS[3][QTab[P_31][j] ^ key8[3]];
			break;
		}
	}

	/*
	 * The client reverses the round subkey pairs at 0x004EBEE0 when
	 * direction==0 and its blockEncrypt iterates subkeys from sk[38]
	 * down to sk[8]. Our blockEncrypt runs the forward loop (sk[8] up
	 * to sk[38]) so skipping the reversal produces the same net result.
	 */
}

/*
 * 0x004EBFF0 - client.exe 2.0.8 - blockEncrypt
 *
 * Encrypts inputLen bits of input to outBuffer in ECB mode, one 128-bit
 * block per iteration. ci is unused because ECB has no cipher state.
 */
int
blockEncrypt(TwofishCipher *ci, TwofishCipher *ki, const uint8_t *input, int inputLen, uint8_t *outBuffer)
{
	int n, r;
	uint32_t x0, x1, x2, x3;
	uint32_t t0, t1;
	const uint32_t *sk = ki->ki_subKeys;
	const uint32_t *sBox = ki->ki_sBox;
	int rounds = ki->ki_numRounds;
	int nBlocks = (inputLen + 127) / 128;

	USED(ci); /* ECB mode doesn't use cipher state */

	for (n = 0; n < nBlocks; n++) {
		// Load block (little-endian) and XOR with input whitening
		memcpy(&x0, input + 0, 4);
		memcpy(&x1, input + 4, 4);
		memcpy(&x2, input + 8, 4);
		memcpy(&x3, input + 12, 4);

		x0 ^= sk[0];
		x1 ^= sk[1];
		x2 ^= sk[2];
		x3 ^= sk[3];

		// Feistel rounds
		for (r = 0; r < rounds; r += 2) {
			t0 = g_func(x0, sBox);
			t1 = g_func(ROL(x1, 8), sBox);
			x2 = ROR(x2 ^ (t0 + t1 + sk[8 + 2 * r]), 1);
			x3 = ROL(x3, 1) ^ (t0 + 2 * t1 + sk[9 + 2 * r]);

			t0 = g_func(x2, sBox);
			t1 = g_func(ROL(x3, 8), sBox);
			x0 = ROR(x0 ^ (t0 + t1 + sk[10 + 2 * r]), 1);
			x1 = ROL(x1, 1) ^ (t0 + 2 * t1 + sk[11 + 2 * r]);
		}

		// Output whitening
		x2 ^= sk[4];
		x3 ^= sk[5];
		x0 ^= sk[6];
		x1 ^= sk[7];

		// Store block (Feistel swaps x0/x2 and x1/x3)
		memcpy(outBuffer + 0, &x2, 4);
		memcpy(outBuffer + 4, &x3, 4);
		memcpy(outBuffer + 8, &x0, 4);
		memcpy(outBuffer + 12, &x1, 4);

		input += 16;
		outBuffer += 16;
	}

	return inputLen;
}

/*
 * Helper - h_func
 *
 * Computes one 32-bit column of the key-dependent S-box by running x
 * through the outer Q permutations and the MDS matrix. Called by reKey
 * (0x004EB2D0) for both subkey generation and S-box precomputation.
 */
static uint32_t
h_func(uint32_t x, const uint32_t *key, int k64Cnt)
{
	uint8_t b0 = B0(x), b1 = B1(x), b2 = B2(x), b3 = B3(x);

	switch (k64Cnt & 3) {
	case 0: /* 256-bit key - layer 4 (innermost) */
		b0 = QTab[P_04][b0] ^ B0(key[3]);
		b1 = QTab[P_14][b1] ^ B1(key[3]);
		b2 = QTab[P_24][b2] ^ B2(key[3]);
		b3 = QTab[P_34][b3] ^ B3(key[3]);
		// fall through
	case 3: /* 192-bit key - layer 3 */
		b0 = QTab[P_03][b0] ^ B0(key[2]);
		b1 = QTab[P_13][b1] ^ B1(key[2]);
		b2 = QTab[P_23][b2] ^ B2(key[2]);
		b3 = QTab[P_33][b3] ^ B3(key[2]);
		// fall through
	case 2: /* 128-bit key - layer 2 */
		b0 = QTab[P_02][b0] ^ B0(key[1]);
		b1 = QTab[P_12][b1] ^ B1(key[1]);
		b2 = QTab[P_22][b2] ^ B2(key[1]);
		b3 = QTab[P_32][b3] ^ B3(key[1]);
		// fall through
	case 1: /* layer 1 (outermost, before MDS) */
		b0 = QTab[P_01][b0] ^ B0(key[0]);
		b1 = QTab[P_11][b1] ^ B1(key[0]);
		b2 = QTab[P_21][b2] ^ B2(key[0]);
		b3 = QTab[P_31][b3] ^ B3(key[0]);
	}

	return MDS[0][b0] ^ MDS[1][b1] ^ MDS[2][b2] ^ MDS[3][b3];
}

/*
 * Helper - g_func
 *
 * Twofish g() using the precomputed interleaved S-box. Inlined by
 * blockEncrypt in the client binary.
 */
static inline uint32_t
g_func(uint32_t x, const uint32_t *sBox)
{
	return sBox[2 * B0(x)] ^ sBox[2 * B1(x) + 1] ^ sBox[512 + 2 * B2(x)] ^ sBox[512 + 2 * B3(x) + 1];
}

/*
 * Helper - MD5_Decode
 *
 * Unpacks len bytes from input into little-endian uint32_t words.
 * Inlined at 0x004B8D9D-0x004B8DC7 inside MD5Transform.
 */
static void
MD5_Decode(uint32_t *output, const uint8_t *input, unsigned int len)
{
	unsigned int i, j;
	for (i = 0, j = 0; j < len; i++, j += 4) {
		output[i] = ((uint32_t)input[j]) | (((uint32_t)input[j + 1]) << 8) | (((uint32_t)input[j + 2]) << 16) | (((uint32_t)input[j + 3]) << 24);
	}
}

/*
 * Helper - MD5_Encode
 *
 * Packs uint32_t words into little-endian bytes. Inlined inside
 * MD5Final at 0x004B8CD1-0x004B8CFC and 0x004B8D42-0x004B8D6D.
 */
static void
MD5_Encode(uint8_t *output, const uint32_t *input, unsigned int len)
{
	unsigned int i, j;
	for (i = 0, j = 0; j < len; i++, j += 4) {
		output[j] = (uint8_t)(input[i] & 0xff);
		output[j + 1] = (uint8_t)((input[i] >> 8) & 0xff);
		output[j + 2] = (uint8_t)((input[i] >> 16) & 0xff);
		output[j + 3] = (uint8_t)((input[i] >> 24) & 0xff);
	}
}
