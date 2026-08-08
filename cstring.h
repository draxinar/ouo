#ifndef CSTRING_H_
#define CSTRING_H_

#include <stdint.h>

__extension__ typedef struct CString CString;

/*
 * Reference-counted string object (0x10 bytes). data is NULL for an
 * empty string; refCount is initialized to 1.
 */
struct CString {
	char *data;             // 0x00
	int length;             // 0x04
	int refCount;           // 0x08
	int capacity;           // 0x0C
};

static inline int
CString_GetLength(CString *s)
{
	return s->length;
}

extern CString g_EmptyCString; // 0x0063E160

CString *CString_DefaultConstructorWrap(CString *str); // 0x0043EBC0
CString *CString_ScalarDelete(CString *s, int flags); // 0x0040CC80
void CString_DestructorWrap(CString *str); // 0x0043EBE0
void CString_DefaultConstructor(CString *s); // 0x004D2F80
void CString_Destructor(CString *s); // 0x004D2FC2
CString *CString_CopyConstructor(CString *s, CString *other); // 0x004D2FF4
CString *CString_Constructor(CString *s, const char *str); // 0x004D303C
CString *CString_ConstructorFromChar(CString *s, char c); // 0x004D309B
char *CString_AssignInternal(CString *s, const char *str); // 0x004D310F
char *CString_AssignRange(CString *s, const char *str, int maxCount); // 0x004D31BD
char *CString_ConcatCStr(CString *s, const char *str); // 0x004D3289
char *CString_GetCStr(CString *s); // 0x004D3387
char *CString_GetData(CString *s); // 0x004D3397
CString *CString_AssignCStr(CString *s, const char *str); // 0x004D33A7
CString *CString_Assign(CString *s, CString *other); // 0x004D33C3
CString *CString_SetFromInt(CString *s, int value); // 0x004D33E1
CString *CString_SetFromChar(CString *s, char c); // 0x004D3426
CString *CString_ConcatCString(CString *s, CString *other); // 0x004D3481
CString *CString_AppendCStr(CString *s, const char *str); // 0x004D349F
CString *CString_ConcatInt(CString *s, int value); // 0x004D34BB
CString *CString_ConcatUInt(CString *s, unsigned int value); // 0x004D354A
CString *CString_ConcatChar(CString *s, char c); // 0x004D358F
int CString_EqualCString(CString *s, CString *other); // 0x004D3680
int CString_EqualCString2(CString *s, CString *other); // 0x004D36AE
int CString_CompareStr(CString *s, const char *str); // 0x004D36DC
int CString_CompareNoCase(CString *s, const char *str); // 0x004D3782
char *CString_CharAt(CString *s, unsigned int index); // 0x004D37AE
CString *CString_OpPlusCStr(CString *s, const char *str); // 0x004D37D5
CString *CString_OpPlusCString(CString *s, CString *other); // 0x004D38BB
CString *CString_Mid(CString *obj, int nFirst, int nCount); // 0x004D3BF9
char *CString_GetBuffer(CString *s); // 0x004D3C86
char *CString_GetCStr2(CString *s); // 0x004D3C96
int CString_GetString(CString *s); // 0x004D3CA6
int CString_LessThan(CString *s, CString *other); // 0x004D3CBF
int CString_GreaterThan(CString *s, CString *other); // 0x004D3CEC
int CString_Contains(CString *s, char c); // 0x004D3E13
int CString_Replace(CString *dest, CString *source, CString *find, CString *replace); // 0x004D3E6D
int CString_Tokenize(CString *s, int *pos, int *lineCount, CString *outToken, CString *delimiters, CString *whitespace, int direction); // 0x004D3F22
int CString_SkipToDelimiter(CString *s, int *pos, int *lineCount, CString *delimiters, int direction); // 0x004D4119
int CString_IsNumeric(CString *s); // 0x004D41AB
int CString_IsEmpty(CString *s); // 0x004D4208
void CString_Clear(CString *s); // 0x004D4230
void CString_Clear2(CString *s); // 0x004D4260

#endif /* CSTRING_H_ */
