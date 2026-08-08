/*
 * Object variable string encoding.
 *
 * Escape / unescape helpers for embedding arbitrary strings (names,
 * wombat tags, free-form text) in the backslash- and space-delimited
 * dynamic save format.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "combat.h"
#include "dynamic.h"
#include "list.h"
#include "region.h"
#include "ustring.h"
#include "world.h"

/*
 * 0x004C45D6 - ObjVar_EscapeStr
 *
 * Escapes spaces and backslashes in src into a static buffer and
 * returns it. No bounds check.
 */
// 0x006E76A8 - ObjVar_EscapeStr output scratch buffer
static char g_EscapeBuf[0x4000];

char *
ObjVar_EscapeStr(const char *src)
{
	char *dst;

	dst = g_EscapeBuf;
	while (src != NULL && *src != '\0') {
		if (*src == ' ' || *src == '\\')
			*dst++ = '\\';
		*dst++ = *src++;
	}
	*dst = '\0';
	return g_EscapeBuf;
}
/*
 * 0x004C4643 - ObjVar_UnescapeStr
 *
 * Reverses ObjVar_EscapeStr into dst, stopping at NUL or an
 * unescaped space. Returns the pointer past the consumed input.
 */
const char *
ObjVar_UnescapeStr(const char *src, char *dst)
{
	while (*src != '\0' && *src != ' ') {
		if (*src == '\\')
			src++;
		*dst++ = *src++;
	}
	*dst = '\0';
	return src;
}

/*
 * 0x004C4698 - ObjVar_EscapeUStr
 *
 * Hex-encodes each UCS-2 character as 4 lowercase nibbles into a
 * static buffer and returns it.
 */
// 0x006EB6A8 - ObjVar_EscapeUStr output scratch buffer
static char g_UEscapeBuf[0x4000];

char *
ObjVar_EscapeUStr(const uint16_t *src)
{
	static const char *hexchars = "0123456789abcdef";
	char *dst;

	dst = g_UEscapeBuf;
	while (src != NULL && *src != 0) {
		uint16_t ch = *src;
		*dst++ = hexchars[(ch >> 12)];
		*dst++ = hexchars[(ch >> 8) & 0xF];
		*dst++ = hexchars[(ch >> 4) & 0xF];
		*dst++ = hexchars[ch & 0xF];
		src++;
	}
	*dst = '\0';
	return g_UEscapeBuf;
}

/*
 * 0x004C4765 - Hex2Wchar
 *
 * Inverse of ObjVar_EscapeUStr: decodes 4-nibble groups into
 * 16-bit chars until NUL or space.
 */
char *
Hex2Wchar(char *src, unsigned short *dst)
{
	int nib0, nib1, nib2, nib3;

	while (*src != '\0' && *src != ' ') {
		nib0 = (src[0] >= 'a') ? src[0] - 0x57 : src[0] - '0';
		nib1 = (src[1] >= 'a') ? src[1] - 0x57 : src[1] - '0';
		nib2 = (src[2] >= 'a') ? src[2] - 0x57 : src[2] - '0';
		nib3 = (src[3] >= 'a') ? src[3] - 0x57 : src[3] - '0';
		*dst = (unsigned short)((nib0 << 12) | (nib1 << 8) | (nib2 << 4) | nib3);
		src += 4;
		dst++;
	}
	*dst = 0;
	return src;
}

/*
 * 0x004C486A - ObjVar_ValidateListPtr
 *
 * Returns 1 when ptr is non-NULL. The original has unused
 * file/line args from a compiled-out assert.
 */
int
ObjVar_ValidateListPtr(CList *ptr)
{
	return (ptr != NULL) ? 1 : 0;
}

/*
 * 0x004C488C - List_SerializeToBuf
 *
 * Emits each node as a " TYPE VALUE" token into buf, recursing into
 * nested lists. Types: 0=int, 1=str, 2=ust, 3=loc, 4=obj, 5=lis.
 */
void
List_SerializeToBuf(CDataBuffer *buf, CList *list)
{
	char tmp[1024];
	CListNode *node;
	CList *sublist;

	node = list->head;
	while (node != NULL) {
		switch (node->typeTag) {
		case 0: // int
			sprintf(tmp, " %3s %d", g_tagTypeNames[node->typeTag], (int)node->value);
			CDataBuffer_Append(buf, tmp, strlen(tmp));
			break;
		case 4: // obj (unsigned)
			sprintf(tmp, " %3s %u", g_tagTypeNames[node->typeTag], (unsigned)node->value);
			CDataBuffer_Append(buf, tmp, strlen(tmp));
			break;
		case 3: // loc (CLocation pointer)
		{
			CLocation *loc = (CLocation *)(uintptr_t)node->value;
			sprintf(tmp, " %3s %d %d %d", g_tagTypeNames[node->typeTag], (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)loc->z);
			CDataBuffer_Append(buf, tmp, strlen(tmp));
			break;
		}
		case 1: // str (CString pointer)
		{
			char *escaped = ObjVar_EscapeStr(CString_GetData((CString *)(uintptr_t)node->value));
			sprintf(tmp, " %3s %s", g_tagTypeNames[node->typeTag], escaped);
			CDataBuffer_Append(buf, tmp, strlen(tmp));
			break;
		}
		case 2: // ust (CUString pointer)
		{
			char *escaped = ObjVar_EscapeUStr((const uint16_t *)(uintptr_t)CUString_GetData((CUString *)(uintptr_t)node->value));
			sprintf(tmp, "%3s %s", g_tagTypeNames[node->typeTag], escaped);
			CDataBuffer_Append(buf, tmp, strlen(tmp));
			break;
		}
		case 5: // lis (nested CList pointer)
			sublist = (CList *)(uintptr_t)node->value;
			if (!ObjVar_ValidateListPtr(sublist)) {
				sprintf(tmp, " %3s %d", g_tagTypeNames[node->typeTag], 0);
				CDataBuffer_Append(buf, tmp, strlen(tmp));
			} else {
				sprintf(tmp, " %3s %d", g_tagTypeNames[node->typeTag], sublist->count);
				CDataBuffer_Append(buf, tmp, strlen(tmp));
				List_SerializeToBuf(buf, sublist);
			}
			break;
		}
		node = node->next;
	}
}

/*
 * 0x004C4BC6 - List_SerializeToString
 *
 * Size-bounded variant of List_SerializeToBuf that writes into outStr.
 * Only int, obj, and loc entries enforce the maxLen cap; str/ust/lis
 * strcat unconditionally (binary behavior).
 */
void
List_SerializeToString(char *outStr, CList *list, uint32_t maxLen)
{
	char tmp[1024];
	CListNode *node;
	CList *sublist;
	char *escaped;

	node = list->head;
	while (node != NULL) {
		switch (node->typeTag) {
		case 0: // int
			sprintf(tmp, " %3s %d", g_tagTypeNames[node->typeTag], (int)node->value);
			if (strlen(outStr) + strlen(tmp) >= maxLen)
				return;
			strcat(outStr, tmp);
			break;
		case 4: // obj (unsigned)
			sprintf(tmp, " %3s %u", g_tagTypeNames[node->typeTag], (unsigned)node->value);
			if (strlen(outStr) + strlen(tmp) >= maxLen)
				return;
			strcat(outStr, tmp);
			break;
		case 3: // loc (CLocation pointer)
		{
			CLocation *loc = (CLocation *)(uintptr_t)node->value;
			sprintf(tmp, " %3s %d %d %d", g_tagTypeNames[node->typeTag], (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)loc->z);
			if (strlen(outStr) + strlen(tmp) >= maxLen)
				return;
			strcat(outStr, tmp);
			break;
		}
		case 1: // str (CString pointer)
		{
			escaped = ObjVar_EscapeStr(CString_GetData((CString *)(uintptr_t)node->value));
			sprintf(tmp, " %3s %s", g_tagTypeNames[node->typeTag], escaped);
			strcat(outStr, tmp);
			break;
		}
		case 2: // ust (CUString pointer)
		{
			escaped = ObjVar_EscapeUStr((const uint16_t *)(uintptr_t)CUString_GetData((CUString *)(uintptr_t)node->value));
			sprintf(tmp, " %3s %s", g_tagTypeNames[node->typeTag], escaped);
			strcat(outStr, tmp);
			break;
		}
		case 5: // lis (nested CList pointer)
			sublist = (CList *)(uintptr_t)node->value;
			if (!ObjVar_ValidateListPtr(sublist)) {
				sprintf(tmp, " %3s %d", g_tagTypeNames[node->typeTag], 0);
				strcat(outStr, tmp);
			} else {
				sprintf(tmp, " %3s %d", g_tagTypeNames[node->typeTag], sublist->count);
				strcat(outStr, tmp);
				List_SerializeToString(outStr, sublist, maxLen);
			}
			break;
		}
		node = node->next;
	}
}

/*
 * 0x004C4F99 - ObjVar::SerializeToString
 *
 * Emits each node as " typeName value" into a CString via AppendCStr.
 * The loc/str/ust/lis cases redundantly re-emit the type prefix
 * inside the switch, matching the binary exactly.
 */
void
ObjVar_SerializeToString(CString *str, CList *list)
{
	CListNode *node;
	CList *sublist;

	node = list->head;
	while (node != NULL) {
		CString_AppendCStr(str, " ");
		CString_AppendCStr(str, g_tagTypeNames[node->typeTag]);
		CString_AppendCStr(str, " ");

		switch (node->typeTag) {
		case 0: // int
			CString_ConcatInt(str, (int)node->value);
			break;
		case 4: // obj (unsigned)
			CString_ConcatUInt(str, node->value);
			break;
		case 3: // loc (CLocation pointer)
		{
			CLocation *loc = (CLocation *)(uintptr_t)node->value;
			CString_AppendCStr(str, " ");
			CString_AppendCStr(str, g_tagTypeNames[node->typeTag]);
			CString_AppendCStr(str, " ");
			CString_ConcatInt(str, (int)(int16_t)loc->x);
			CString_AppendCStr(str, " ");
			CString_ConcatInt(str, (int)(int16_t)loc->y);
			CString_AppendCStr(str, " ");
			CString_ConcatInt(str, (int)(int16_t)loc->z);
			break;
		}
		case 1: // str (CString pointer)
		{
			char *escaped;
			CString_AppendCStr(str, " ");
			CString_AppendCStr(str, g_tagTypeNames[node->typeTag]);
			CString_AppendCStr(str, " ");
			escaped = ObjVar_EscapeStr(CString_GetData((CString *)(uintptr_t)node->value));
			CString_AppendCStr(str, escaped);
			break;
		}
		case 2: // ust (CUString pointer)
		{
			char *escaped;
			CString_AppendCStr(str, " ");
			CString_AppendCStr(str, g_tagTypeNames[node->typeTag]);
			CString_AppendCStr(str, " ");
			escaped = ObjVar_EscapeUStr((const uint16_t *)(uintptr_t)CUString_GetData((CUString *)(uintptr_t)node->value));
			CString_AppendCStr(str, escaped);
			break;
		}
		case 5: // lis (nested CList pointer)
			sublist = (CList *)(uintptr_t)node->value;
			if (!ObjVar_ValidateListPtr(sublist)) {
				CString_ConcatInt(str, 0);
			} else {
				CString_AppendCStr(str, " ");
				CString_AppendCStr(str, g_tagTypeNames[node->typeTag]);
				CString_AppendCStr(str, " ");
				CString_ConcatInt(str, sublist->count);
				ObjVar_SerializeToString(str, sublist);
			}
			break;
		}
		node = node->next;
	}
}

/*
 * 0x004C520C - List_DeserializeFromBuf
 *
 * Reads count " TYPE VALUE" entries out of text, rebuilds the
 * CList, and returns the pointer past the consumed input.
 */
const char *
List_DeserializeFromBuf(const char *text, CList *list, int count)
{
	CLocation loc;
	char buf[0x8000];
	int i, j;
	int intval;
	unsigned int uintval;
	int x, y, z;
	CString str;
	CUString ustr;
	CList *sublist;

	CLocation_Init(&loc);

	for (i = 0; i < count; i++) {
		for (j = 0; j < 7; j++) {
			if (strncmp(g_tagTypeNames[j], text, 3) == 0)
				break;
		}
		while (*text != '\0' && *text != ' ')
			text++;
		if (*text != '\0')
			text++;

		switch (j) {
		case 0: // int
			sscanf(text, "%d", &intval);
			while (*text != '\0' && *text != ' ')
				text++;
			if (*text != '\0')
				text++;
			CList_Append(list, 0, (uint32_t)intval);
			break;
		case 4: // obj (unsigned)
			sscanf(text, "%u", &uintval);
			while (*text != '\0' && *text != ' ')
				text++;
			if (*text != '\0')
				text++;
			CList_Append(list, 4, uintval);
			break;
		case 1: // str (CString)
			text = ObjVar_UnescapeStr(text, buf);
			if (*text != '\0')
				text++;
			CString_Constructor(&str, buf);
			CList_Append(list, 1, (uintptr_t)&str);
			CString_Destructor(&str);
			break;
		case 2: // ust (CUString)
			text = (const char *)Hex2Wchar((char *)text, (unsigned short *)buf);
			if (*text != '\0')
				text++;
			CUString_Constructor(&ustr, buf);
			CList_Append(list, 2, (uintptr_t)&ustr);
			CUString_Destructor(&ustr);
			break;
		case 3: // loc (CLocation)
			sscanf(text, "%d %d %d", &x, &y, &z);
			while (*text != '\0' && *text != ' ')
				text++;
			if (*text != '\0')
				text++;
			while (*text != '\0' && *text != ' ')
				text++;
			if (*text != '\0')
				text++;
			while (*text != '\0' && *text != ' ')
				text++;
			if (*text != '\0')
				text++;
			CLocation_Set(&loc, (int16_t)x, (int16_t)y, (int16_t)z);
			CList_Append(list, 3, (uintptr_t)&loc);
			break;
		case 5: // lis (nested CList)
			sscanf(text, "%d", &intval);
			while (*text != '\0' && *text != ' ')
				text++;
			if (*text != '\0')
				text++;
			sublist = (CList *)OperatorNew(sizeof(CList));
			if (sublist != NULL)
				CList_Constructor(sublist);
			text = List_DeserializeFromBuf(text, sublist, intval);
			CList_Append(list, 5, (uintptr_t)sublist);
			if (sublist != NULL)
				CList_ScalarDelete(sublist, 1);
			break;
		}
	}
	return text;
}

/*
 * 0x004CCBEA - ObjVar_LoadFromLine
 *
 * Parses a wom_var= value and stores the objvar.
 * Input format: "<type3> <name> <value>" (the part after "wom_var=").
 */
void
ObjVar_LoadFromLine(uint32_t serial, const char *val)
{
	char typebuf[8];
	char namebuf[64];
	const char *p;
	int n;
	CItem *item;

	n = sscanf(val, "%7s %63s", typebuf, namebuf);
	if (n < 2)
		return;

	p = val;
	while (*p && *p != ' ')
		p++;
	while (*p == ' ')
		p++;
	while (*p && *p != ' ')
		p++;
	while (*p == ' ')
		p++;

	item = CWorld_FindBySerial(g_World, serial);
	if (item == NULL)
		return;

	if (strcmp(typebuf, "int") == 0) {
		int ival = 0;
		sscanf(p, "%d", &ival);
		Dynamic_LoadEntity(item, namebuf, 0, (uintptr_t)ival);
	} else if (strcmp(typebuf, "str") == 0) {
		char decoded[256];
		ObjVar_UnescapeStr(p, decoded);
		Dynamic_LoadEntity(item, namebuf, 1, (uintptr_t)decoded);
	} else if (strcmp(typebuf, "ust") == 0) {
		unsigned short decoded[256];
		Hex2Wchar((char *)p, decoded);
		Dynamic_LoadEntity(item, namebuf, 2, (uintptr_t)decoded);
	} else if (strcmp(typebuf, "loc") == 0) {
		int x = 0, y = 0, z = 0;
		CLocation loc;
		sscanf(p, "%d %d %d", &x, &y, &z);
		CLocation_Init(&loc);
		CLocation_Set(&loc, (int16_t)x, (int16_t)y, (int16_t)z);
		Dynamic_LoadEntity(item, namebuf, 3, (uintptr_t)&loc);
	} else if (strcmp(typebuf, "obj") == 0) {
		unsigned int oval = 0;
		sscanf(p, "%u", &oval);
		Dynamic_LoadEntity(item, namebuf, 4, (uintptr_t)oval);
	} else if (strcmp(typebuf, "lis") == 0) {
		int count = 0;
		const char *elemStart;
		CList *list;

		sscanf(p, "%d", &count);
		elemStart = p;
		while (*elemStart && *elemStart != ' ')
			elemStart++;
		if (*elemStart)
			elemStart++;

		list = (CList *)OperatorNew(sizeof(CList));
		if (list != NULL)
			CList_Constructor(list);
		List_DeserializeFromBuf(elemStart, list, count);
		Dynamic_LoadEntity(item, namebuf, 5, (uintptr_t)list);
	}
}
