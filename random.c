/*
 * CRandom - Mersenne-like RNG used by every gameplay roll in the binary.
 *
 * Wraps a seeded generator behind RandRange, RandPercent, and the ANSI
 * table used for dice-style skill checks. Combat damage, treasure,
 * spawn choice, and skill gain all consume from this one stream so
 * replays stay deterministic when the seed is fixed.
 */

#include <stdint.h>

#include "main.h"
#include "utils.h"

static uint32_t CRandom_GetGameTick(void); // 0x00467DF1

/*
 * 0x00467DF1 - CRandom::GetGameTick
 *
 * Returns the current global game tick. Called by DecaySkill.
 */
static uint32_t __attribute__((unused))
CRandom_GetGameTick(void)
{
	return g_GameTick;
}
/*
 * 0x00467E01 - CRandom::GetDefaultSeed
 *
 * Returns the constant default RNG seed (0x33FA0481) used to
 * initialise skill timestamps.
 */
uint32_t
CRandom_GetDefaultSeed(void)
{
	return 0x33FA0481;
}

/*
 * 0x0046849E - CRandom::ParseAndRoll
 *
 * Thiscall on g_CRandom (0x006982F0). Parses a dice expression from a
 * string and rolls it. Formats:
 *   "N"       - plain integer
 *   "NdM"     - roll N dice with M sides
 *   "NdM+B"   - roll N dice with M sides, add bonus B
 *   "NdM-B"   - roll N dice with M sides, subtract B
 *   "NdM!B"   - roll N dice with M sides, randomly add or subtract B
 * Prefix: '+' = positive, '-' = negative, '!' = random sign.
 */
int
CRandom_ParseAndRoll(const char *str)
{
	int numValue = 0;
	int sign = 1;
	int numSides;
	int bonus;

	if (*str == '+') {
		sign = 1;
		str++;
	} else if (*str == '-') {
		sign = -1;
		str++;
	} else if (*str == '!') {
		if (GetRandomRange(1, 2) == 1)
			sign = 1;
		else
			sign = -1;
		str++;
	}

	while (*str >= '0' && *str <= '9') {
		numValue = numValue * 10 + (*str - '0');
		str++;
	}

	if (*str != 'd')
		return sign * numValue;

	numSides = 0;
	str++;
	while (*str >= '0' && *str <= '9') {
		numSides = numSides * 10 + (*str - '0');
		str++;
	}

	if (*str == '!') {
		bonus = 0;
		str++;
		while (*str >= '0' && *str <= '9') {
			bonus = bonus * 10 + (*str - '0');
			str++;
		}
		if (GetRandomRange(1, 2) == 1)
			return sign * (CRandom_RollDice(numValue, numSides) - bonus);
		else
			return sign * (CRandom_RollDice(numValue, numSides) + bonus);
	} else if (*str == '+') {
		bonus = 0;
		str++;
		while (*str >= '0' && *str <= '9') {
			bonus = bonus * 10 + (*str - '0');
			str++;
		}
		return sign * (CRandom_RollDice(numValue, numSides) + bonus);
	} else if (*str == '-') {
		bonus = 0;
		str++;
		while (*str >= '0' && *str <= '9') {
			bonus = bonus * 10 + (*str - '0');
			str++;
		}
		return sign * (CRandom_RollDice(numValue, numSides) - bonus);
	} else {
		return sign * CRandom_RollDice(numValue, numSides);
	}
}
