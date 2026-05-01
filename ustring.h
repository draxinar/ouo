#ifndef USTRING_H_
#define USTRING_H_

#include <stdint.h>

/*
 * Wide-char (wchar16) string object (16 bytes). Same shape as CString but
 * for uint16_t elements. Constructor 0x004DCF19, destructor 0x004DCEA2.
 */
__extension__ typedef struct CUString CUString;
struct CUString {
	char *data;   // +0x00
	int length;   // +0x04
	int refCount; // +0x08
	int capacity; // +0x0C
};

/*
 * {uint16_t *, count} pair (8 bytes) used as a cheap uint16 array view by
 * CIntArray_At (0x004DD827) and CUShortArray_Contains (0x004DDEA9).
 */
__extension__ typedef struct CIntArray CIntArray;
struct CIntArray {
	uint16_t *data; // +0x00
	uint32_t count; // +0x04
};

extern CUString g_EmptyCUString; // 0x0063E164

CUString *CUString_ScalarDelete(CUString *s, int flags); // 0x00420D00
int UString_Length(const void *wstr); // 0x004DA900
void wcscpy16(void *dst, const void *src); // 0x004DA92D
void wcsncpy16(void *dst, const void *src, int count); // 0x004DA972
int wcstoi16(const uint16_t *src); // 0x004DA9C7
void wcscat16(void *dst, const void *src); // 0x004DAA49
int wcscmp16(const uint16_t *a, const uint16_t *b); // 0x004DAAA5
int ucscmp(const uint16_t *a, const uint16_t *b); // 0x004DAB0E
int isASCII(int c); // 0x004DAC00
int IsSpace16(int c); // 0x004DAC1D
CUString *CUString_DefaultConstructor(CUString *s); // 0x004DCE60
void CUString_Destructor(CUString *s); // 0x004DCEA2
CUString *CUString_CopyConstructor(CUString *s, CUString *other); // 0x004DCED4
CUString *CUString_Constructor(CUString *s, const void *wstr); // 0x004DCF19
char *CUString_AssignInternal(CUString *s, const void *wstr); // 0x004DCFF2
char *CUString_Mid(CUString *s, const void *src, int count); // 0x004DD0A2
char *CUString_ConcatInternal(CUString *s, const void *wstr); // 0x004DD172
char *CUString_GetCStr(CUString *s); // 0x004DD276
char *CUString_GetData(CUString *s); // 0x004DD286
CUString *CUString_AssignCStr(CUString *s, const void *wstr); // 0x004DD296
CUString *CUString_Assign(CUString *s, CUString *other); // 0x004DD2B2
CUString *CUString_AssignStr(CUString *s, int value); // 0x004DD2D0
CUString *CUString_ConcatCUString(CUString *s, CUString *other); // 0x004DD3D0
CUString *CUString_ConcatCStr(CUString *s, const void *wstr); // 0x004DD3EE
CUString *CUString_ConcatChar(CUString *s, unsigned short c); // 0x004DD5F2
int CUString_EqualCUString(CUString *s, CUString *other); // 0x004DD720
uint16_t *CIntArray_At(CIntArray *arr, uint32_t index); // 0x004DD827
uint32_t CUString_GetPtr(CUString *s); // 0x004DDD1C
int CUShortArray_Contains(CIntArray *arr, uint16_t value); // 0x004DDEA9
void CUString_Clear(CUString *s); // 0x004DE330

#endif
