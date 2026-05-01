#ifndef BLOWFISH_H_
#define BLOWFISH_H_

#include <stdint.h>

/*
 * Blowfish cipher for UO game connections (1.26.0+).
 *
 * The client uses Blowfish in CFB (Cipher FeedBack) mode with 8-byte
 * blocks on the game connection; the login connection stays on the
 * simpler XOR cipher. The switch is triggered by the USER_SERVER packet
 * handler. Key and IV come from 25 fixed entries in the client's .rdata
 * section, selected per client: 1.26.0 uses index 0, 1.26.1+ and 2.0.x
 * use index 1. Binary addresses live alongside each function in
 * blowfish.c.
 */

__extension__ typedef struct CGameCrypt CGameCrypt;

/*
 * Blowfish cipher context. The server stores P and S inline and uses
 * the first 8 bytes of Table A[tableIndex] as the decrypt IV (matching
 * the client's encrypt direction).
 *
 * 1.26.0 client layout (0x105C bytes at CClientSock+0x08):
 *   0x0000-0x0047: P-array (18 uint32s)
 *   0x0048-0x1047: S-boxes (4 x 256 uint32s)
 *   0x1048-0x104F: block A (16 bytes)
 *   0x104C:        block counter
 *   0x1050-0x105F: block B (streaming IV, 16 bytes)
 *   0x1054:        position counter (0-7)
 *   0x1058:        table index byte
 *
 * 2.0.0 client layout (0x22 bytes, P/S stored globally):
 *   0x00-0x0F: block 1 (Table B[index] when flag=1)
 *   0x10-0x1F: block 2 (Table A[index] when flag=1)
 *   0x20:      position counter | block counter nibbles
 *   0x21:      table index byte
 */
struct CGameCrypt {
	uint32_t P[18];
	uint32_t S[4][256];
	uint8_t iv[8];      // first 8 bytes of Table A[tableIndex]
	int pos;            // 0-7, position within the current 8-byte block
	int tableIndex;     // 0-24, index into the client's BF table
	int byteCounter;    // bytes since last re-key (for 0x522C rotation)
	int enableRotation; // 1 if key rotation is active (2.0.0c+)
};

void CGameCrypt_Init(CGameCrypt *ctx, int tableIndex);
void CGameCrypt_InitWithRotation(CGameCrypt *ctx, int tableIndex);
void CGameCrypt_Decrypt(CGameCrypt *ctx, uint8_t *buf, int len);

#endif /* BLOWFISH_H_ */
