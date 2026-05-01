/*
 * CDiceRoll - "NdF+B" dice expression parser and roller.
 *
 * Used for weapon damage, armor class, durability, and NPC stat
 * generation: parses the expression string once, then rolls on demand
 * against the server RNG.
 */

#include <stdint.h>

#include "cstring.h"
#include "dat.h"
#include "dice.h"
#include "utils.h"

static CWeaponDice *CDiceRoll_InitThiscall(CWeaponDice *d, int numDice, int faces, int bonus, int unused); // 0x004599ED

/*
 * 0x00459840 - CDiceRoll::CDiceRoll
 *
 * Zero-initialises the struct.
 */
CWeaponDice *
CDiceRoll_Constructor(CWeaponDice *d)
{
	CDiceRoll_Clear(d);
	return d;
}

/*
 * 0x00459872 - CDiceRoll::InitParse
 *
 * Clears the struct and parses a dice expression string.
 */
CWeaponDice *
CDiceRoll_InitParse(CWeaponDice *d, const char *s)
{
	CDiceRoll_Clear(d);
	CDiceRoll_Parse(d, s);
	return d;
}

/*
 * 0x00459896 - CDiceRoll::Average
 *
 * Returns numDice * (faces + 1) / 2 + bonus.
 */
int
CDiceRoll_Average(CWeaponDice *d)
{
	int count = (int)(int8_t)d->numDice;
	int faces = (int)(uint8_t)d->diceFaces;
	int bonus = (int)(int8_t)d->bonus;

	return count * (faces + 1) / 2 + bonus;
}

/*
 * 0x004598C3 - CDiceRoll::Roll
 *
 * Returns the sum of numDice rolls of [1..faces] plus bonus.
 */
int
CDiceRoll_Roll(CWeaponDice *d)
{
	int total = 0;
	int i;

	for (i = 0; i < (int)(int8_t)d->numDice; i++)
		total += GetRandomRange(1, d->diceFaces);
	total += (int)(int8_t)d->bonus;
	return total;
}

/*
 * 0x00459938 - CDiceRoll::Clear
 *
 * Zeroes all dice fields.
 */
void
CDiceRoll_Clear(CWeaponDice *d)
{
	CDiceRoll_Init(d, 0, 0, 0, 0);
}

/*
 * 0x00459953 - CDiceRoll::ToString
 *
 * Appends the dice expression ("NdF", "NdF+B", or "NdF-B") to out.
 */
void
CDiceRoll_ToString(CWeaponDice *d, CString *out)
{
	int n, f, b;

	n = (int)(int8_t)CDiceRoll_GetNumDice(d);
	f = (int)(uint8_t)CDiceRoll_GetDiceFaces(d);
	b = (int)(int8_t)CDiceRoll_GetBonus(d);

	CString_ConcatInt(out, n);
	CString_AppendCStr(out, "d");
	CString_ConcatInt(out, f);
	if (b == 0)
		return;
	if (b > 0)
		CString_AppendCStr(out, "+");
	CString_ConcatInt(out, b);
}

/*
 * 0x004599ED - CDiceRoll::InitThiscall
 *
 * Truncates each arg to a byte and forwards to CDiceRoll_Init.
 */
static __attribute__((unused)) CWeaponDice *
CDiceRoll_InitThiscall(CWeaponDice *d, int numDice, int faces, int bonus, int unused)
{
	CDiceRoll_Init(d, (int8_t)(uint8_t)numDice, (uint8_t)faces, (int8_t)(uint8_t)bonus, (uint8_t)unused);
	return d;
}

/*
 * 0x00459A15 - CDiceRoll::Init
 *
 * Stores numDice, faces, and bonus. The 4th arg is ignored.
 */
void
CDiceRoll_Init(CWeaponDice *d, int8_t numDice, uint8_t faces, int8_t bonus, uint8_t unused)
{
	USED(unused);
	d->numDice = (uint8_t)numDice;
	d->diceFaces = faces;
	d->bonus = (uint8_t)bonus;
}

/*
 * 0x00459A3C - CDiceRoll::GetNumDice
 *
 * Returns the numDice field as a signed byte.
 */
int8_t
CDiceRoll_GetNumDice(CWeaponDice *d)
{
	return (int8_t)d->numDice;
}

/*
 * 0x00459A4C - CDiceRoll::GetDiceFaces
 *
 * Returns the diceFaces field.
 */
uint8_t
CDiceRoll_GetDiceFaces(CWeaponDice *d)
{
	return d->diceFaces;
}

/*
 * 0x00459A5D - CDiceRoll::GetBonus
 *
 * Returns the bonus field as a signed byte.
 */
int8_t
CDiceRoll_GetBonus(CWeaponDice *d)
{
	return (int8_t)d->bonus;
}

/*
 * 0x00459A6E - CDiceRoll::GetField4
 *
 * Always returns 0. Called by Copy and IsZero as the 4th field getter.
 */
uint8_t
CDiceRoll_GetField4(CWeaponDice *d)
{
	USED(d);
	return 0;
}

/*
 * 0x00459A7B - CDiceRoll::Parse
 *
 * Parses a dice expression. Supported forms:
 *   "NdF", "NdF+B", "NdF-B", "B", "+B", "-B", "!NdF".
 * The '!' (randomize-at-creation) flag is parsed but discarded. The
 * binary does not handle the "B+NdF" form - it stops at the first
 * number when no 'd' follows.
 */
void
CDiceRoll_Parse(CWeaponDice *d, const char *s)
{
	int firstNum;
	int sign1;
	int randomFlag;
	int sign2;
	int secondNum;
	int thirdNum;
	int hasSecondPart;
	int numDice, faces, bonus;
	const char *p;

	p = s;
	firstNum = 0;
	sign1 = 1;
	randomFlag = 0;
	sign2 = 1;

	if (*p == '+') {
		sign1 = 1;
		p++;
	} else if (*p == '-') {
		sign1 = -1;
		p++;
	} else if (*p == '!') {
		randomFlag = 1;
		p++;
	}
	USED(randomFlag);

	while (*p >= '0' && *p <= '9') {
		firstNum = firstNum * 10 + (*p - '0');
		p++;
	}

	numDice = firstNum;
	faces = 0;
	bonus = 0;

	if (*p == 'd') {
		p++;

		secondNum = 0;
		while (*p >= '0' && *p <= '9') {
			secondNum = secondNum * 10 + (*p - '0');
			p++;
		}
		faces = secondNum;

		hasSecondPart = 0;
		thirdNum = 0;
		sign2 = 1;

		if (*p == '!') {
			p++;
			hasSecondPart = 1;
		} else if (*p == '+') {
			p++;
			sign2 = 1;
			hasSecondPart = 1;
		} else if (*p == '-') {
			p++;
			sign2 = -1;
			hasSecondPart = 1;
		}

		if (hasSecondPart) {
			while (*p >= '0' && *p <= '9') {
				thirdNum = thirdNum * 10 + (*p - '0');
				p++;
			}
			bonus = thirdNum;
		}
	} else {
		faces = 0;
		numDice = 0;
		bonus = firstNum;
		sign2 = sign1;
		sign1 = 1;
	}

	numDice = numDice * sign1;
	bonus = bonus * sign2;

	CDiceRoll_Init(d, (int8_t)numDice, (uint8_t)faces, (int8_t)bonus, 0);
}

/*
 * 0x00459CAE - CDiceRoll::Copy
 *
 * Copies src into dst via getters and CDiceRoll_Init.
 */
void
CDiceRoll_Copy(CWeaponDice *dst, CWeaponDice *src)
{
	CDiceRoll_Init(dst, CDiceRoll_GetNumDice(src), CDiceRoll_GetDiceFaces(src), CDiceRoll_GetBonus(src), CDiceRoll_GetField4(src));
}

/*
 * 0x00459CEA - CDiceRoll::IsZero
 *
 * Returns 1 if every getter returns 0.
 */
int
CDiceRoll_IsZero(CWeaponDice *d)
{
	if ((int)(int8_t)CDiceRoll_GetNumDice(d) != 0)
		return 0;
	if ((int)(uint8_t)CDiceRoll_GetDiceFaces(d) != 0)
		return 0;
	if ((int)(int8_t)CDiceRoll_GetBonus(d) != 0)
		return 0;
	if ((int)(uint8_t)CDiceRoll_GetField4(d) != 0)
		return 0;
	return 1;
}
