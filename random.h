#ifndef RANDOM_H_
#define RANDOM_H_

#include <stdint.h>

uint32_t CRandom_GetDefaultSeed(void); // 0x00467E01
int CRandom_ParseAndRoll(const char *str); // 0x0046849E

#endif /* RANDOM_H_ */
