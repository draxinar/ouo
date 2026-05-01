/*
 * Utils - endian stubs, composed 64-bit random, and other grab-bag helpers.
 *
 * SwapEndian is a deliberate no-op carried over from the little-endian
 * binary; GetRandom64 composes a 64-bit value from four CRandom calls.
 * Any helper that does not fit a subsystem but still mirrors a binary
 * routine lives here.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

static int64_t GetRandom64(void); // 0x00467D9F

/*
 * 0x0045ACC0 - SwapEndian
 *
 * No-op byte-swap stub. Called from save/load functions around int32
 * fields; intentionally empty because the format is little-endian.
 */
void
SwapEndian(void *value)
{
	USED(value);
}

/*
 * 0x00467D9F - GetRandom64
 *
 * Returns a 64-bit random number assembled from four rand() calls
 * (two 16-bit halves form each of the low and high 32-bit halves).
 */
static int64_t
GetRandom64(void)
{
	int r1, r2, r3, r4;
	int32_t lo, hi;

	r1 = rand();
	r2 = rand();
	lo = (r1 << 16) | r2;
	r3 = rand();
	r4 = rand();
	hi = (r3 << 16) | r4;
	return ((int64_t)hi << 32) | (int64_t)lo;
}

/*
 * 0x004683DF
 */
int
GetRandomRange(int min, int max)
{
	int result;

	if (max >= min)
		result = min + rand() % (max - min + 1);
	else
		result = 0;
	return result;
}

/*
 * 0x0046840E - GetRandomRange64
 *
 * 64-bit version of GetRandomRange. Generates a 64-bit random via
 * GetRandom64, computes rand % (max - min + 1) + min using 64-bit
 * signed remainder (__allrem in binary). Returns low 32 bits.
 * Thiscall on CRandom in binary (this pointer stored but unused).
 * Single caller: DecaySkill (0x0047364A).
 */
int
GetRandomRange64(int min, int max)
{
	int64_t r;
	int64_t range;
	int64_t result;

	if (max < min)
		return 0;
	r = GetRandom64();
	range = (int64_t)(max - min + 1);
	result = r % range + (int64_t)min;
	return (int)result;
}

/*
 * 0x00468453 - CRandom::RollDice
 *
 * Rolls numDice dice each with numSides sides, returns total.
 * Thiscall on CRandom in binary (this pointer ignored).
 */
int
CRandom_RollDice(int numDice, int numSides)
{
	int total = 0;
	int i;

	for (i = 0; i < numDice; i++)
		total += GetRandomRange(1, numSides);
	return total;
}

/*
 * 0x00469410 - StringAssign
 *
 * Frees the old string at *pStr, then allocates a copy of newStr.
 * If newStr is NULL, *pStr is set to NULL.
 */
void
StringAssign(char **pStr, const char *newStr)
{
	if (*pStr != NULL) {
		free(*pStr);
		*pStr = NULL;
	}
	if (newStr != NULL) {
		*pStr = malloc(strlen(newStr) + 1);
		strcpy(*pStr, newStr);
	}
}

/*
 * 0x0046C070 - SwapInt16
 *
 * Cdecl, 2 pointer args. Swaps two int16_t values via a local temp.
 * Called by FindSpawnSpotInBox to sort min/max coordinate pairs.
 */
void
SwapInt16(int16_t *a, int16_t *b)
{
	int16_t tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

/*
 * 0x00481805
 */
int
GetRandom(int max)
{
	return rand() % max;
}

/*
 * 0x004D63D0 - SwapBytes
 *
 * Swaps the bytes at *a and *b.
 */
void
SwapBytes(int8_t *a, int8_t *b)
{
	int8_t tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}
