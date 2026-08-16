#ifndef WOMBAT_STL_H_
#define WOMBAT_STL_H_

#include <stdint.h>

#include "stl.h"

void CSdbStrVector_Init(CScriptStringDB *this); // 0x0040106A
char *String_CStr(CSdbStr *this); // 0x00401510
void *CSdbStrVector_At(CScriptStringDB *this, uint32_t index); // 0x00401A00

#endif /* WOMBAT_STL_H_ */
