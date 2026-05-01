/*
 * GOST 28147-89 Feistel cipher in CTR gamma mode.
 *
 * Drives the page encryption / decryption done by ContainerHandle for
 * every block read from or written to uodemo.dat.
 */

#include <stddef.h>
#include <stdint.h>

static uint32_t Feistel_SBoxLookup(uint32_t val); // 0x004E4D20
static void Feistel_F_Encrypt(const uint32_t input[2], uint32_t output[2]); // 0x004E4E70
static void Feistel_F_Decrypt(const uint32_t input[2], uint32_t output[2]); // 0x004E4F85

// S-box globals. Binary: BSS at 0x00700640..0x00701640.
uint32_t g_SBox0[256]; // 0x00700640
uint32_t g_SBox1[256]; // 0x00700A40
uint32_t g_SBox2[256]; // 0x00700E40
uint32_t g_SBox3[256]; // 0x00701240

// 0x00627630 - eight 32-bit subkeys
static uint32_t g_SubKeys[8] = {
	0xf7aa4c98,
	0xbf62c067,
	0x0b524334,
	0xbc28a2d0,
	0x6b57c2d1,
	0x233843aa,
	0xc06d0210,
	0x38d4f804,
};

// 0x00627720 - encrypt IV (identical to decrypt IV in binary)
static uint32_t g_EncryptIV[2] = { 0x591dcb84, 0xcba84b2c };
// 0x00627728 - decrypt IV (identical to encrypt IV in binary)
static uint32_t g_DecryptIV[2] = { 0x591dcb84, 0xcba84b2c };

/*
 * Encrypt key schedule program (68 bytes). Indices into encrypt dispatch
 * table at 0x00627650: 0=LoadKey, 1=XorW1, 2=Key++, 3=XorW0, 4=Key--,
 * 5=XorW1, 6=XorW0.
 */
// 0x00627690 - encrypt key schedule program (68 bytes)
static uint8_t g_EncryptSchedule[68] = {
	0x00,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x00,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x00,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x01,
	0x02,
	0x03,
	0x02,
	0x04,
	0x05,
	0x04,
	0x06,
	0x04,
	0x05,
	0x04,
	0x06,
	0x04,
	0x05,
	0x04,
	0x06,
	0x04,
	0x05,
	0x04,
	0x06,
	0x04,
};

/*
 * Decrypt key schedule program (68 bytes). Indices into decrypt dispatch
 * table at 0x00627670: 0=XorW0, 1=XorW1, 2=Key--, 3=XorW0, 4=Key++,
 * 5=XorW1, 6=LoadKey.
 */
// 0x006276D8 - decrypt key schedule program (68 bytes)
static uint8_t g_DecryptSchedule[68] = {
	0x06,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x06,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x06,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x05,
	0x04,
	0x03,
	0x04,
	0x02,
	0x01,
	0x02,
	0x00,
	0x02,
	0x01,
	0x02,
	0x00,
	0x02,
	0x01,
	0x02,
	0x00,
	0x02,
	0x01,
	0x02,
	0x00,
	0x02,
};

/*
 * Dispatch tables (7 function pointers each). Binary: .data at 0x00627650
 * (encrypt) and 0x00627670 (decrypt). The decrypt table is the encrypt
 * table reversed: encrypt[0..6] = {LoadKey, XorW1, Key++, XorW0, Key--,
 * XorW1, XorW0}; decrypt[0..6] = {XorW0, XorW1, Key--, XorW0, Key++,
 * XorW1, LoadKey}. Schedule bytes are indices into the respective table,
 * so the same byte value maps to different operations in encrypt vs decrypt.
 */

/*
 * S-box permutation tables (8 tables of 16 uint32 entries each).
 * Binary: .rdata at 0x005F02A0..0x005F04A0.
 * Init_UODEMODAT builds the 256-entry S-boxes from these via
 * g_SBox[i*16+perm[j]] = j for each table.
 */
// 0x005F02A0 - S-box permutation table 0
static const uint32_t g_Perm0[16] = {
	1,
	15,
	13,
	0,
	5,
	7,
	10,
	4,
	9,
	2,
	3,
	14,
	6,
	11,
	8,
	12,
};

// 0x005F02E0 - S-box permutation table 1
static const uint32_t g_Perm1[16] = {
	13,
	11,
	4,
	1,
	3,
	15,
	5,
	9,
	0,
	10,
	14,
	7,
	6,
	8,
	2,
	12,
};

// 0x005F0320 - S-box permutation table 2
static const uint32_t g_Perm2[16] = {
	4,
	11,
	10,
	0,
	7,
	2,
	1,
	13,
	3,
	6,
	8,
	5,
	9,
	12,
	15,
	14,
};

// 0x005F0360 - S-box permutation table 3
static const uint32_t g_Perm3[16] = {
	6,
	12,
	7,
	1,
	5,
	15,
	13,
	8,
	4,
	10,
	9,
	14,
	0,
	3,
	11,
	2,
};

// 0x005F03A0 - S-box permutation table 4
static const uint32_t g_Perm4[16] = {
	7,
	13,
	10,
	1,
	0,
	8,
	9,
	15,
	14,
	4,
	6,
	12,
	11,
	2,
	5,
	3,
};

// 0x005F03E0 - S-box permutation table 5
static const uint32_t g_Perm5[16] = {
	5,
	8,
	1,
	13,
	10,
	3,
	4,
	2,
	14,
	15,
	12,
	7,
	6,
	0,
	9,
	11,
};

// 0x005F0420 - S-box permutation table 6
static const uint32_t g_Perm6[16] = {
	14,
	11,
	4,
	12,
	6,
	13,
	15,
	10,
	2,
	3,
	8,
	1,
	0,
	7,
	5,
	9,
};

// 0x005F0460 - S-box permutation table 7
static const uint32_t g_Perm7[16] = {
	4,
	10,
	9,
	2,
	13,
	8,
	0,
	14,
	6,
	11,
	1,
	12,
	7,
	15,
	5,
	3,
};

/*
 * 0x004E4D20 - Feistel_SBoxLookup
 *
 * GOST S-box pass: byte-wise lookup, sum, ROL32 by 11.
 */
static uint32_t
Feistel_SBoxLookup(uint32_t val)
{
	uint32_t result;

	result = g_SBox0[val & 0xFF];
	result += g_SBox1[(val >> 8) & 0xFF];
	result += g_SBox2[(val >> 16) & 0xFF];
	result += g_SBox3[(val >> 24) & 0xFF];

	// ROL32 by 11.
	result = (result << 11) | (result >> 21);
	return result;
}

/*
 * 0x004E4DB0 - Feistel_Encrypt
 *
 * In-place CTR-mode encryption of blockCount 8-byte blocks. Counters
 * advance with independent self-carry (+0x01010101 and +0x01010104);
 * each 0x200-block chunk re-derives state from the IV.
 */
void
Feistel_Encrypt(uint32_t *data, int blockCount)
{
	uint32_t state[2];
	uint32_t keystream[2];
	int total;
	int cursor;
	int chunkEnd;
	int inner;

	total = blockCount * 2;
	cursor = 0;

	while (cursor < total) {
		Feistel_F_Encrypt(g_EncryptIV, state);

		chunkEnd = cursor + 0x400;
		inner = cursor;

		while (inner < total) {
			// Independent self-carry on each counter half.
			state[0] += 0x01010101;
			if (state[0] < 0x01010101)
				state[0]++;
			state[1] += 0x01010104;
			if (state[1] < 0x01010104)
				state[1]++;

			Feistel_F_Encrypt(state, keystream);

			data[0] ^= keystream[0];
			data[1] ^= keystream[1];
			data += 2;

			inner += 2;
			if (inner >= chunkEnd)
				break;
		}
		cursor = chunkEnd;
	}
}

/*
 * 0x004E4E70 - Feistel_F_Encrypt
 *
 * Runs the 68-step encrypt schedule on the input pair. The binary
 * dispatches 7 opcodes via a function-pointer table; we switch on
 * the index. Output is word-swapped.
 */
static void
Feistel_F_Encrypt(const uint32_t input[2], uint32_t output[2])
{
	uint32_t word0, word1;
	const uint32_t *keyPtr;
	int i;

	word0 = input[0];
	word1 = input[1];
	keyPtr = NULL;

	for (i = 0; i < 68; i++) {
		switch (g_EncryptSchedule[i]) {
		case 0:
			keyPtr = g_SubKeys;
			break;
		case 1:
			word1 ^= Feistel_SBoxLookup(*keyPtr + word0);
			break;
		case 2:
			keyPtr++;
			break;
		case 3:
			word0 ^= Feistel_SBoxLookup(*keyPtr + word1);
			break;
		case 4:
			keyPtr--;
			break;
		case 5:
			// Binary has a separate dispatch slot with the same body as case 1.
			word1 ^= Feistel_SBoxLookup(*keyPtr + word0);
			break;
		case 6:
			// Binary has a separate dispatch slot with the same body as case 3.
			word0 ^= Feistel_SBoxLookup(*keyPtr + word1);
			break;
		}
	}

	output[0] = word1;
	output[1] = word0;
}

/*
 * 0x004E4EC5 - Feistel_Decrypt
 *
 * In-place CTR-mode decryption. Identical to Feistel_Encrypt but uses
 * the decrypt IV and Feistel_F_Decrypt.
 */
void
Feistel_Decrypt(uint32_t *data, int blockCount)
{
	uint32_t state[2];
	uint32_t keystream[2];
	int total;
	int cursor;
	int chunkEnd;
	int inner;

	total = blockCount * 2;
	cursor = 0;

	while (cursor < total) {
		Feistel_F_Decrypt(g_DecryptIV, state);

		chunkEnd = cursor + 0x400;
		inner = cursor;

		while (inner < total) {
			// Independent self-carry on each counter half.
			state[0] += 0x01010101;
			if (state[0] < 0x01010101)
				state[0]++;
			state[1] += 0x01010104;
			if (state[1] < 0x01010104)
				state[1]++;

			Feistel_F_Decrypt(state, keystream);

			data[0] ^= keystream[0];
			data[1] ^= keystream[1];
			data += 2;

			inner += 2;
			if (inner >= chunkEnd)
				break;
		}
		cursor = chunkEnd;
	}
}

/*
 * 0x004E4F85 - Feistel_F_Decrypt
 *
 * Mirror of Feistel_F_Encrypt using the decrypt schedule and a reversed
 * opcode table: 0=XorW0, 1=XorW1, 2=Key--, 3=XorW0, 4=Key++, 5=XorW1,
 * 6=LoadKey.
 */
static void
Feistel_F_Decrypt(const uint32_t input[2], uint32_t output[2])
{
	uint32_t word0, word1;
	const uint32_t *keyPtr;
	int i;

	word0 = input[0];
	word1 = input[1];
	keyPtr = NULL;

	for (i = 0; i < 68; i++) {
		switch (g_DecryptSchedule[i]) {
		case 0:
			word0 ^= Feistel_SBoxLookup(*keyPtr + word1);
			break;
		case 1:
			word1 ^= Feistel_SBoxLookup(*keyPtr + word0);
			break;
		case 2:
			keyPtr--;
			break;
		case 3:
			word0 ^= Feistel_SBoxLookup(*keyPtr + word1);
			break;
		case 4:
			keyPtr++;
			break;
		case 5:
			word1 ^= Feistel_SBoxLookup(*keyPtr + word0);
			break;
		case 6:
			keyPtr = g_SubKeys;
			break;
		}
	}

	output[0] = word1;
	output[1] = word0;
}

/*
 * Helper - Feistel_InitSBoxes
 *
 * Builds the four 256-entry S-boxes from the eight 16-entry permutation
 * tables, pre-shifted to their byte lane. Extracted from the binary's
 * Init_UODEMODAT loop at 0x004E51C4..0x004E523E.
 */
void
Feistel_InitSBoxes(void)
{
	int idx;
	int hi, lo;

	for (idx = 0; idx < 256; idx++) {
		hi = idx >> 4;
		lo = idx & 0xF;
		g_SBox3[idx] = (g_Perm0[hi] << 4 | g_Perm1[lo]) << 24;
		g_SBox2[idx] = (g_Perm2[hi] << 4 | g_Perm3[lo]) << 16;
		g_SBox1[idx] = (g_Perm4[hi] << 4 | g_Perm5[lo]) << 8;
		g_SBox0[idx] = (g_Perm6[hi] << 4 | g_Perm7[lo]);
	}
}
