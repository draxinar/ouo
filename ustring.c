/*
 * UString - reference-counted wide (uint16_t) string.
 *
 * The wide-character counterpart to CString, used for Unicode names,
 * book text, and other content sent in UTF-16 packets.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "region.h"
#include "ustring.h"

/*
 * 0x0063E164 - g_EmptyCUString
 *
 * Shared empty-wide-string sentinel: CUStrings default-construct
 * pointing here so callers never observe a NULL data pointer.
 */
CUString g_EmptyCUString = { NULL, 0, 0, 0 };

/*
 * 0x00420D00 - CUString::~CUString (scalar deleting destructor)
 *
 * Runs the destructor and optionally frees the object.
 */
CUString *
CUString_ScalarDelete(CUString *s, int flags)
{
	CUString_Destructor(s);
	if (flags & 1)
		OperatorDelete(s);
	return s;
}
/*
 * 0x004DA900 - UString_Length (wcslen for uint16_t)
 *
 * Returns the length (in uint16_t units) of a null-terminated wide string.
 */
int
UString_Length(const void *wstr)
{
	const unsigned short *ws = (const unsigned short *)wstr;
	int i;

	i = 0;
	while (ws[i] != 0)
		i++;
	return i;
}

/*
 * 0x004DA92D - wcscpy16
 *
 * Copies a null-terminated uint16_t wide string from src to dst.
 */
void
wcscpy16(void *dst, const void *src)
{
	unsigned short *d = (unsigned short *)dst;
	const unsigned short *s = (const unsigned short *)src;

	if (d == NULL)
		return;
	if (s == NULL)
		return;
	while (*s != 0) {
		*d = *s;
		d++;
		s++;
	}
	*d = 0;
}

/*
 * 0x004DA972 - wcsncpy16
 *
 * Copies up to count uint16_t characters from src to dst and null-terminates.
 */
void
wcsncpy16(void *dst, const void *src, int count)
{
	unsigned short *d = (unsigned short *)dst;
	const unsigned short *s = (const unsigned short *)src;

	if (d == NULL)
		return;
	if (s == NULL)
		return;
	while (*s != 0) {
		if (count-- == 0)
			break;
		*d = *s;
		d++;
		s++;
	}
	*d = 0;
}

/*
 * 0x004DA9C7 - wcstoi16
 *
 * Narrows a UCS-2 string to ASCII (non-ASCII becomes space) and calls atoi().
 */
int
wcstoi16(const uint16_t *src)
{
	char buf[0x1004];
	char *dst;
	uint16_t ch;

	dst = buf;
	for (;;) {
		ch = *src;
		if (ch == 0)
			break;
		if (ch >= 0x80)
			ch = 0x20;
		*dst = (char)ch;
		src++;
		dst++;
	}
	*dst = '\0';
	return atoi(buf);
}

/*
 * 0x004DAA49 - wcscat16
 *
 * Appends uint16_t wide string src to the end of dst.
 */
void
wcscat16(void *dst, const void *src)
{
	unsigned short *d = (unsigned short *)dst;
	const unsigned short *s = (const unsigned short *)src;

	if (d == NULL)
		return;
	if (s == NULL)
		return;
	while (*d != 0)
		d++;
	while (*s != 0) {
		*d = *s;
		d++;
		s++;
	}
	*d = 0;
}

/*
 * 0x004DAAA5 - wcscmp16 (case-sensitive UCS-2 string comparison)
 *
 * Returns 0 if equal, 1 if a > b, -1 if a < b.
 */
int
wcscmp16(const uint16_t *a, const uint16_t *b)
{
	for (;;) {
		if (*a == 0 && *b == 0)
			return 0;
		if (*a != *b) {
			if (*a >= *b)
				return 1;
			return -1;
		}
		a++;
		b++;
	}
}

/*
 * 0x004DAB0E - ucscmp (case-insensitive UCS-2 string comparison)
 *
 * Folds A-Z to a-z. Returns 0 if equal, 1 if a > b, -1 if a < b.
 */
int
ucscmp(const uint16_t *a, const uint16_t *b)
{
	uint16_t ca, cb;

	for (;;) {
		if (*a == 0 && *b == 0)
			return 0;
		ca = *a;
		if (ca >= 'A' && ca <= 'Z')
			ca += 0x20;
		cb = *b;
		if (cb >= 'A' && cb <= 'Z')
			cb += 0x20;
		if (ca != cb) {
			if (ca >= cb)
				return 1;
			return -1;
		}
		a++;
		b++;
	}
}

/*
 * 0x004DAC00 - isASCII
 *
 * Returns 1 if the low 16 bits of c are < 0x80, else 0.
 */
int
isASCII(int c)
{
	if ((c & 0xFFFF) < 0x80)
		return 1;
	return 0;
}

/*
 * 0x004DAC1D - IsSpace16
 *
 * Returns 1 if the UCS-2 character is a space (0x20), else 0.
 */
int
IsSpace16(int c)
{
	if ((c & 0xFFFF) == 0x20)
		return 1;
	return 0;
}

/*
 * 0x004DCE60 - CUString::CUString() (default constructor)
 *
 * Zeroes fields, sets refCount = 1, and assigns the empty UCS-2 string.
 */
CUString *
CUString_DefaultConstructor(CUString *s)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	{
		static const uint16_t emptyUStr[1] = { 0 };
		CUString_AssignInternal(s, emptyUStr);
	}
	return s;
}

/*
 * 0x004DCEA2 - CUString::~CUString (destructor)
 *
 * Frees the data buffer if non-NULL.
 */
void
CUString_Destructor(CUString *s)
{
	if (s->data != NULL) {
		free(s->data);
		s->data = NULL;
	}
}

/*
 * 0x004DCED4 - CUString::CUString(const CUString&) (copy constructor)
 *
 * Initializes from another CUString via AssignInternal.
 */
CUString *
CUString_CopyConstructor(CUString *s, CUString *other)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	CUString_AssignInternal(s, other->data);
	return s;
}

/*
 * 0x004DCF19 - CUString::CUString (from const wchar_t *)
 *
 * Constructs a CUString from a wide C string.
 */
CUString *
CUString_Constructor(CUString *s, const void *wstr)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	CUString_AssignInternal(s, wstr);
	if (s->data == NULL) {
		s->capacity = 0;
		s->length = 0;
	}
	return s;
}

/*
 * 0x004DCFF2 - CUString::operator= (from const wchar_t *)
 *
 * Assigns a wide C string, growing the buffer if needed.
 * Capacity is in wchar_t units; allocation is capacity*2 bytes.
 */
char *
CUString_AssignInternal(CUString *s, const void *wstr)
{
	int needed;

	if (wstr == NULL) {
		CUString_Clear(s);
		return NULL;
	}
	needed = UString_Length(wstr) + 1;
	if (needed > s->capacity) {
		if (s->data != NULL)
			free(s->data);
		s->capacity = UString_Length(wstr) + s->refCount;
		s->data = (char *)malloc(s->capacity * 2);
	}
	wcscpy16(s->data, wstr);
	s->length = UString_Length(s->data);
	return s->data;
}

/*
 * 0x004DD0A2 - CUString::Mid
 *
 * Copies up to count wide chars from src into this, growing the buffer
 * as needed. Returns this->data, or NULL if src is NULL.
 */
char *
CUString_Mid(CUString *s, const void *src, int count)
{
	int srcLen;

	if (src == NULL) {
		CUString_Clear(s);
		return NULL;
	}
	srcLen = UString_Length(src);
	if (srcLen < count)
		count = srcLen;
	if (count + 1 > s->capacity) {
		if (s->data != NULL)
			free(s->data);
		s->capacity = count + s->refCount;
		s->data = (char *)malloc(s->capacity * 2);
	}
	wcsncpy16(s->data, src, count);
	((unsigned short *)s->data)[count] = 0;
	s->length = count;
	return s->data;
}

/*
 * 0x004DD172 - CUString::ConcatInternal
 *
 * Appends wstr to this string, growing the buffer as needed.
 */
char *
CUString_ConcatInternal(CUString *s, const void *wstr)
{
	int needed;
	char *newBuf;

	if (wstr == NULL)
		return s->data;
	if (*(const unsigned short *)wstr == 0)
		return s->data;
	needed = UString_Length(wstr) + s->length + 1;
	if (needed > s->capacity) {
		s->capacity = UString_Length(wstr) + s->length + s->refCount;
		newBuf = (char *)malloc(s->capacity * 2);
		*(unsigned short *)newBuf = 0;
		if (s->data != NULL)
			wcscpy16(newBuf, s->data);
		wcscat16(newBuf, wstr);
		if (s->data != NULL)
			free(s->data);
		s->data = newBuf;
	} else {
		wcscat16(s->data, wstr);
	}
	s->length = UString_Length(s->data);
	return s->data;
}

/*
 * 0x004DD276 - CUString::GetCStr
 *
 * Returns the data pointer. Separate entry point from GetData.
 */
char *
CUString_GetCStr(CUString *s)
{
	return s->data;
}

/*
 * 0x004DD286 - CUString::GetData
 *
 * Returns the data pointer.
 */
char *
CUString_GetData(CUString *s)
{
	return s->data;
}

/*
 * 0x004DD296 - CUString::AssignCStr
 *
 * Thin wrapper around AssignInternal that returns this.
 */
CUString *
CUString_AssignCStr(CUString *s, const void *wstr)
{
	CUString_AssignInternal(s, wstr);
	return s;
}

/*
 * 0x004DD2B2 - CUString::operator= (from CUString)
 *
 * Assigns from other->data via AssignInternal.
 */
CUString *
CUString_Assign(CUString *s, CUString *other)
{
	CUString_AssignInternal(s, other->data);
	return s;
}

/*
 * 0x004DD2D0 - CUString::operator= (from int)
 *
 * Formats the integer via sprintf, widens each char to uint16_t,
 * and assigns the resulting wide string.
 */
CUString *
CUString_AssignStr(CUString *s, int value)
{
	char buf[256];
	short wbuf[256];
	int i;

	sprintf(buf, "%d", value);
	i = 0;
	for (;;) {
		if ((signed char)buf[i] == 0)
			break;
		wbuf[i] = (short)(signed char)buf[i];
		i++;
	}
	wbuf[i] = 0;
	CUString_AssignInternal(s, wbuf);
	return s;
}

/*
 * 0x004DD3D0 - CUString::operator+=(const CUString&)
 *
 * Appends other's data via ConcatInternal.
 */
CUString *
CUString_ConcatCUString(CUString *s, CUString *other)
{
	CUString_ConcatInternal(s, other->data);
	return s;
}

/*
 * 0x004DD3EE - CUString::operator+=(const wchar_t*)
 *
 * Appends the wide C string via ConcatInternal.
 */
CUString *
CUString_ConcatCStr(CUString *s, const void *wstr)
{
	CUString_ConcatInternal(s, wstr);
	return s;
}

/*
 * 0x004DD5F2 - CUString::ConcatChar
 *
 * Appends a single wide character, growing the buffer as needed.
 */
CUString *
CUString_ConcatChar(CUString *s, unsigned short c)
{
	char *newBuf;

	if (s->length + 2 > s->capacity) {
		newBuf = (char *)malloc(s->length * 2 + 4);
		if (s->data != NULL) {
			wcscpy16(newBuf, s->data);
			free(s->data);
		}
		((unsigned short *)newBuf)[s->length] = c;
		((unsigned short *)newBuf)[s->length + 1] = 0;
		s->data = newBuf;
	} else {
		((unsigned short *)s->data)[s->length] = c;
		((unsigned short *)s->data)[s->length + 1] = 0;
	}
	s->length = UString_Length(s->data);
	return s;
}

/*
 * 0x004DD720 - CUString::operator==(const CUString&)
 *
 * Case-insensitive UCS-2 comparison via ucscmp. Returns 1 if equal.
 */
int
CUString_EqualCUString(CUString *s, CUString *other)
{
	return ucscmp((const uint16_t *)s->data, (const uint16_t *)other->data) == 0;
}

// 0x006F05B4 - static zero returned by CIntArray_At for out-of-bounds index
static uint16_t CIntArray_zero;

/*
 * 0x004DD827 - CIntArray::operator[]
 *
 * Returns a pointer to element index, or to a static zero if out of bounds.
 */
uint16_t *
CIntArray_At(CIntArray *arr, uint32_t index)
{
	if (index >= arr->count)
		return &CIntArray_zero;
	return &arr->data[index];
}

/*
 * 0x004DDD1C - CUString::GetPtr
 *
 * Returns the first field of the object by dereference.
 */
uint32_t
CUString_GetPtr(CUString *s)
{
	return *(uint32_t *)s;
}

/*
 * 0x004DDEA9 - CUShortArray::Contains
 *
 * Linear search for value in the array. Returns 1 if found, 0 otherwise.
 */
int
CUShortArray_Contains(CIntArray *arr, uint16_t value)
{
	uint32_t i;

	if (arr->data == NULL)
		return 0;
	for (i = 0; i < arr->count; i++) {
		if (*CIntArray_At(arr, i) == value)
			return 1;
	}
	return 0;
}

/*
 * 0x004DE330 - CUString::Clear
 *
 * Empties the string in place without freeing the buffer.
 */
void
CUString_Clear(CUString *s)
{
	if (s->data != NULL)
		*(unsigned short *)(s->data) = 0;
	s->length = 0;
}
