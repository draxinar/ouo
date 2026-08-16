#ifndef FEISTEL_H_
#define FEISTEL_H_

/*
 * GOST 28147-89 block cipher in CTR (gamma) mode.
 * Binary: encrypt at 0x004E4DB0, decrypt at 0x004E4EC5,
 * S-box lookup at 0x004E4D20, key schedule dispatch at 0x004E4E70/0x004E4F85.
 *
 * Four 256-entry S-boxes at 0x00700640, 0x00700A40, 0x00700E40, 0x00701240.
 * Eight 32-bit subkeys at 0x00627630.
 * Key schedule programs at 0x00627690 (encrypt) and 0x006276D8 (decrypt).
 * Dispatch tables at 0x00627650 (encrypt) and 0x00627670 (decrypt).
 * IVs at 0x00627720 (encrypt) and 0x00627728 (decrypt) - identical values.
 *
 * The cipher is structurally present in UoDemo.exe but functionally
 * inactive: Config_Constructor (0x00467590) which calls Init_UODEMODAT
 * (0x004E519A) has zero callers, so the S-boxes remain BSS-zeroed and
 * every F(x) returns 0, making all XOR operations identity.
 *
 * We decompile the cipher exactly for completeness. In standalone mode
 * (no uodemo.dat), the S-boxes remain zero and the cipher is a no-op,
 * matching the binary's runtime behavior.
 */

#include <stdint.h>

extern const uint32_t g_Perm0[16]; // 0x005F02A0
extern const uint32_t g_Perm1[16]; // 0x005F02E0
extern const uint32_t g_Perm2[16]; // 0x005F0320
extern const uint32_t g_Perm3[16]; // 0x005F0360
extern const uint32_t g_Perm4[16]; // 0x005F03A0
extern const uint32_t g_Perm5[16]; // 0x005F03E0
extern const uint32_t g_Perm6[16]; // 0x005F0420
extern const uint32_t g_Perm7[16]; // 0x005F0460

extern uint32_t g_SBox0[256]; // 0x00700640
extern uint32_t g_SBox1[256]; // 0x00700A40
extern uint32_t g_SBox2[256]; // 0x00700E40
extern uint32_t g_SBox3[256]; // 0x00701240

void Feistel_Encrypt(uint32_t *data, int blockCount); // 0x004E4DB0
void Feistel_Decrypt(uint32_t *data, int blockCount); // 0x004E4EC5

#endif
