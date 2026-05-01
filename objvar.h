#ifndef OBJVAR_H_
#define OBJVAR_H_

#include <stdint.h>

__extension__ typedef struct CDataBuffer CDataBuffer;
__extension__ typedef struct CList CList;
__extension__ typedef struct CString CString;
char *ObjVar_EscapeStr(const char *src); // 0x004C45D6
const char *ObjVar_UnescapeStr(const char *src, char *dst); // 0x004C4643
char *ObjVar_EscapeUStr(const uint16_t *src); // 0x004C4698
char *Hex2Wchar(char *src, unsigned short *dst); // 0x004C4765
int ObjVar_ValidateListPtr(CList *ptr); // 0x004C486A
void List_SerializeToBuf(CDataBuffer *buf, CList *list); // 0x004C488C
void List_SerializeToString(char *outStr, CList *list, uint32_t maxLen); // 0x004C4BC6
void ObjVar_SerializeToString(CString *str, CList *list); // 0x004C4F99
const char *List_DeserializeFromBuf(const char *text, CList *list, int count); // 0x004C520C
void ObjVar_LoadFromLine(uint32_t serial, const char *val); // 0x004CCBEA

#endif /* OBJVAR_H_ */
