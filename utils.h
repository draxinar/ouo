#ifndef UTILS_H_
#define UTILS_H_

#include <stdint.h>

void SwapEndian(void *value); // 0x0045ACC0
int GetRandomRange(int min, int max); // 0x004683DF
int GetRandomRange64(int min, int max); // 0x0046840E
int CRandom_RollDice(int numDice, int numSides); // 0x00468453
void StringAssign(char **pStr, const char *newStr); // 0x00469410
void SwapInt16(int16_t *a, int16_t *b); // 0x0046C070
int GetRandom(int max); // 0x00481805
void SwapBytes(int8_t *a, int8_t *b); // 0x004D63D0

#endif /* UTILS_H_ */
