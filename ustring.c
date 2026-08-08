/*
 * UString - reference-counted wide (uint16_t) string.
 *
 * The wide-character counterpart to CString, used for Unicode names,
 * book text, and other content sent in UTF-16 packets.
 */

#include <ctype.h>
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
		OperatorDelete(s->data);
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
 * 0x004DCF78 - CUString::CUString (from wchar_t)
 *
 * Constructs a one-character CUString. Sets capacity from refCount
 * (both 1), allocates capacity wide characters, then stores the
 * character and its terminator.
 *
 * The allocation is one wide character short: capacity is 1 so
 * OperatorNew gets 2 bytes, but the character and the terminator take
 * 4. Reproduced as it stands in the binary - nothing calls this.
 */
static __attribute__((unused)) CUString *
CUString_ConstructorChar(CUString *s, uint16_t c)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	s->capacity = s->refCount;
	s->data = (char *)OperatorNew(s->capacity * 2);
	*(uint16_t *)s->data = c;
	*(uint16_t *)(s->data + 2) = 0;
	s->length = 1;
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
			OperatorDelete(s->data);
		s->capacity = UString_Length(wstr) + s->refCount;
		s->data = (char *)OperatorNew(s->capacity * 2);
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
			OperatorDelete(s->data);
		s->capacity = count + s->refCount;
		s->data = (char *)OperatorNew(s->capacity * 2);
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
		newBuf = (char *)OperatorNew(s->capacity * 2);
		*(unsigned short *)newBuf = 0;
		if (s->data != NULL)
			wcscpy16(newBuf, s->data);
		wcscat16(newBuf, wstr);
		if (s->data != NULL)
			OperatorDelete(s->data);
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
 * 0x004DD371 - CUString::operator= (from wchar_t)
 *
 * Assigns a single wide character. Reallocates a 4-byte buffer when the
 * current capacity is under two wide characters, then stores the
 * character and its terminator.
 */
static __attribute__((unused)) CUString *
CUString_AssignChar(CUString *s, uint16_t c)
{
	if (s->capacity < 2) {
		OperatorDelete(s->data);
		s->data = (char *)OperatorNew(4);
	}
	*(uint16_t *)s->data = c;
	*(uint16_t *)(s->data + 2) = 0;
	s->length = 1;
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
 * 0x004DD40A - CUString::operator+=(int)
 *
 * Formats the integer via sprintf, widens each char to uint16_t, and
 * appends the resulting wide string.
 */
static __attribute__((unused)) CUString *
CUString_ConcatInt(CUString *s, int value)
{
	char buf[256];
	short wbuf[256];
	int i;

	sprintf(buf, "%d", value);
	for (i = 0; buf[i] != '\0'; i++)
		wbuf[i] = (short)buf[i];
	wbuf[i] = 0;
	CUString_ConcatInternal(s, wbuf);
	return s;
}

/*
 * 0x004DD4AB - CUString::operator+=(float)
 *
 * Same as 0x004DD40A with a "%f" conversion. The argument is promoted
 * to double for the sprintf call.
 */
static __attribute__((unused)) CUString *
CUString_ConcatFloat(CUString *s, float value)
{
	char buf[256];
	short wbuf[256];
	int i;

	sprintf(buf, "%f", (double)value);
	for (i = 0; buf[i] != '\0'; i++)
		wbuf[i] = (short)buf[i];
	wbuf[i] = 0;
	CUString_ConcatInternal(s, wbuf);
	return s;
}

/*
 * 0x004DD551 - CUString::operator+=(unsigned)
 *
 * Same as 0x004DD40A with a "%u" conversion.
 */
static __attribute__((unused)) CUString *
CUString_ConcatUInt(CUString *s, unsigned int value)
{
	char buf[256];
	short wbuf[256];
	int i;

	sprintf(buf, "%u", value);
	for (i = 0; buf[i] != '\0'; i++)
		wbuf[i] = (short)buf[i];
	wbuf[i] = 0;
	CUString_ConcatInternal(s, wbuf);
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
		newBuf = (char *)OperatorNew(s->length * 2 + 4);
		if (s->data != NULL) {
			wcscpy16(newBuf, s->data);
			OperatorDelete(s->data);
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
 * 0x004DD6BD - CUString::operator==(wchar_t)
 *
 * Compares the first wide character case-sensitively and requires the
 * length field to be zero. The length test is against 0 rather than 1,
 * so the operator only ever reports equality for a NUL argument on an
 * empty string. Reproduced as it stands in the binary - nothing calls
 * this, and its operator!= counterpart at 0x004DD77A tests length == 1
 * and folds case, so the pair is asymmetric in the binary too.
 */
static __attribute__((unused)) int
CUString_EqualChar(CUString *s, uint16_t c)
{
	if (*(uint16_t *)s->data == c && s->length == 0)
		return 1;
	return 0;
}

/*
 * 0x004DD6F2 - CUString::operator==(const CUString&) (case-insensitive)
 *
 * Case-insensitive UCS-2 comparison via ucscmp. Returns 1 if equal.
 */
static __attribute__((unused)) int
CUString_EqualCUString_6F2(CUString *s, CUString *other)
{
	return ucscmp((const uint16_t *)s->data, (const uint16_t *)other->data) == 0;
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
 * 0x004DD77A - CUString::operator!=(wchar_t)
 *
 * Returns 0 when the string holds exactly one wide character equal to c
 * ignoring case, 1 otherwise.
 */
static __attribute__((unused)) int
CUString_NotEqualChar(CUString *s, uint16_t c)
{
	if (s->length != 1)
		return 1;
	if (tolower(*(uint16_t *)s->data) == tolower(c))
		return 0;
	return 1;
}

/*
 * 0x004DD7CD - CUString::operator!=(const CUString&)
 *
 * Case-insensitive UCS-2 comparison via ucscmp. Returns 1 if different.
 */
static __attribute__((unused)) int
CUString_NotEqualCUString(CUString *s, CUString *other)
{
	return ucscmp((const uint16_t *)s->data, (const uint16_t *)other->data) != 0;
}

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
 * 0x004DD851 - CUString::operator+(const wchar_t *)
 *
 * Returns a pointer to a lazy-initialized static CUString holding
 * this + wstr. Not thread-safe (matches binary behavior).
 */
static __attribute__((unused)) CUString *
CUString_OpPlusCStr(CUString *s, const void *wstr)
{
	static CUString g_opPlusCStr;
	static int g_opPlusCStr_init;

	if (!g_opPlusCStr_init) {
		g_opPlusCStr_init = 1;
		CUString_DefaultConstructor(&g_opPlusCStr);
	}
	CUString_AssignCStr(&g_opPlusCStr, s->data);
	CUString_ConcatCStr(&g_opPlusCStr, wstr);
	return &g_opPlusCStr;
}

/*
 * 0x004DD8C4 - CUString::operator+(wchar_t)
 *
 * Same as 0x004DD851 but appends a single wide character.
 */
static __attribute__((unused)) CUString *
CUString_OpPlusChar(CUString *s, uint16_t c)
{
	static CUString g_opPlusChar;
	static int g_opPlusChar_init;

	if (!g_opPlusChar_init) {
		g_opPlusChar_init = 1;
		CUString_DefaultConstructor(&g_opPlusChar);
	}
	CUString_AssignCStr(&g_opPlusChar, s->data);
	CUString_ConcatChar(&g_opPlusChar, c);
	return &g_opPlusChar;
}

/*
 * 0x004DD938 - CUString::operator+(const CUString&)
 *
 * Same as 0x004DD851 but appends a CUString.
 */
static __attribute__((unused)) CUString *
CUString_OpPlusCUString(CUString *s, CUString *other)
{
	static CUString g_opPlusCUString;
	static int g_opPlusCUString_init;

	if (!g_opPlusCUString_init) {
		g_opPlusCUString_init = 1;
		CUString_DefaultConstructor(&g_opPlusCUString);
	}
	CUString_AssignCStr(&g_opPlusCUString, s->data);
	CUString_ConcatCUString(&g_opPlusCUString, other);
	return &g_opPlusCUString;
}

/*
 * 0x004DD9AB - CUString::MakeUpper
 *
 * Uppercases every wide character in place via toupper.
 */
static __attribute__((unused)) CUString *
CUString_MakeUpper(CUString *s)
{
	uint16_t *p;

	if (s->data != NULL && *(uint16_t *)s->data != 0) {
		for (p = (uint16_t *)s->data; *p != 0; p++)
			*p = (uint16_t)toupper(*p);
	}
	return s;
}

/*
 * 0x004DDA0C - CUString::MakeLower
 *
 * Lowercases every wide character in place via tolower.
 */
static __attribute__((unused)) CUString *
CUString_MakeLower(CUString *s)
{
	uint16_t *p;

	if (s->data != NULL && *(uint16_t *)s->data != 0) {
		for (p = (uint16_t *)s->data; *p != 0; p++)
			*p = (uint16_t)tolower(*p);
	}
	return s;
}

/*
 * 0x004DDA6D - CUString::Prepend(wchar_t)
 *
 * Inserts a wide character in front of the current contents. Grows the
 * buffer by refCount characters when length + 1 no longer fits.
 *
 * The old buffer is released but the new one is not sized from the
 * measured length, and Prepend never rewrites capacity, so repeated
 * calls keep reallocating from the same stale capacity. Reproduced as
 * it stands - nothing calls this.
 */
static __attribute__((unused)) CUString *
CUString_PrependChar(CUString *s, uint16_t c)
{
	uint16_t *newBuf;
	int cap;

	if (s->length + 1 < s->capacity)
		cap = s->capacity;
	else
		cap = s->capacity + s->refCount;

	newBuf = (uint16_t *)OperatorNew(cap * 2);
	*newBuf = c;
	wcscpy16(newBuf + 1, s->data);
	OperatorDelete(s->data);
	s->data = (char *)newBuf;
	s->length = UString_Length(s->data);
	return s;
}

/*
 * 0x004DDB10 - CUString::Prepend(const wchar_t *)
 *
 * Inserts a wide C string in front of the current contents. The growth
 * step is the larger of the inserted length and refCount.
 *
 * The old buffer is leaked - unlike 0x004DDA6D this one never releases
 * it. Reproduced as it stands - nothing calls this.
 */
static __attribute__((unused)) CUString *
CUString_PrependCStr(CUString *s, const void *wstr)
{
	char *newBuf;
	int grow;
	int cap;

	grow = UString_Length(wstr);
	if (grow < s->refCount)
		grow = s->refCount;
	if (s->length + grow < s->capacity)
		cap = s->capacity;
	else
		cap = s->capacity + grow;

	newBuf = (char *)OperatorNew(cap * 2);
	wcscpy16(newBuf, wstr);
	wcscat16(newBuf, s->data);
	s->data = newBuf;
	s->length = UString_Length(s->data);
	return s;
}

/*
 * 0x004DDBD0 - CUString::Prepend(const CUString&)
 *
 * Same as 0x004DDB10 but takes the inserted length from other's length
 * field instead of measuring it, and leaks the old buffer the same way.
 */
static __attribute__((unused)) CUString *
CUString_PrependCUString(CUString *s, CUString *other)
{
	char *newBuf;
	int grow;
	int cap;

	grow = other->length;
	if (grow < s->refCount)
		grow = s->refCount;
	if (s->length + grow < s->capacity)
		cap = s->capacity;
	else
		cap = s->capacity + grow;

	newBuf = (char *)OperatorNew(cap * 2);
	wcscpy16(newBuf, other->data);
	wcscat16(newBuf, s->data);
	s->data = newBuf;
	s->length = UString_Length(s->data);
	return s;
}

/*
 * 0x004DDC8C - CUString::Right
 *
 * Returns a pointer to a lazy-initialized static CUString holding the
 * characters from index start onwards, or the empty string when start
 * is past the end.
 */
static __attribute__((unused)) CUString *
CUString_Right(CUString *s, int start, int count)
{
	// 0x006F05BC - static empty wide string the result is reset from
	static const uint16_t emptyWStr[1];
	static CUString g_rightResult;
	static int g_rightResult_init;

	if (!g_rightResult_init) {
		g_rightResult_init = 1;
		CUString_DefaultConstructor(&g_rightResult);
	}
	CUString_AssignInternal(&g_rightResult, emptyWStr);
	if (start < s->length)
		CUString_Mid(&g_rightResult, s->data + start * 2, count);
	return &g_rightResult;
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
 * 0x004DDF09 - CUString::FormatSubstitute
 *
 * Copies fmt into this, replacing each "%<char>" pair with value.
 * Returns the number of substitutions made.
 *
 * The placeholder check reads spec[0] once before the loop and compares
 * that to '%', rather than comparing the character following the '%' in
 * fmt against spec[1]. A placeholder whose first character is not '%'
 * therefore swallows every "%<char>" pair without substituting anything.
 * Reproduced as it stands - nothing calls this.
 */
static __attribute__((unused)) int
CUString_FormatSubstitute(CUString *s, CIntArray *fmt, CIntArray *spec, CUString *value)
{
	uint16_t marker;
	uint16_t ch;
	int count;
	int n;
	int i;

	marker = *CIntArray_At(spec, 0);
	count = 0;
	n = fmt->count;
	for (i = 0; i < n; i++) {
		ch = *CIntArray_At(fmt, i);
		if (ch == 0)
			break;
		if (ch == '%') {
			i++;
			if (i >= n)
				return count;
			if (marker == '%') {
				CUString_ConcatCUString(s, value);
				count++;
			}
		} else {
			CUString_ConcatChar(s, ch);
		}
	}
	return count;
}

/*
 * 0x004DDFDD - CIntArray::ExtractToken
 *
 * Lexer primitive. Walks the wide-character array from *pos in the
 * direction given by forward, skipping leading characters that are in
 * wsSet, and collects the token into out. A character from delimSet
 * encountered first becomes a one-character token of its own; one
 * encountered after the token has started ends the token. Double quotes
 * toggle quoting, inside which wsSet and delimSet are ignored. Newlines
 * bump *lineNo. Returns 0 while still skipping leading characters,
 * 1 once a token has started.
 */
static __attribute__((unused)) int
CIntArray_ExtractToken(CIntArray *text, int *pos, int *lineNo, CUString *out, CIntArray *wsSet, CIntArray *delimSet, int forward)
{
	// 0x006F05C0 - static empty wide string the result is reset from
	static const uint16_t emptyWStr[1];
	int leading;
	int inQuote;
	int step;
	uint16_t ch;

	leading = 1;
	inQuote = 0;
	CUString_AssignCStr(out, emptyWStr);
	step = forward != 0 ? 1 : -1;

	for (;;) {
		if (*CIntArray_At(text, *pos) == 0)
			return !leading;

		if (!inQuote && !leading) {
			if (CUShortArray_Contains(wsSet, *CIntArray_At(text, *pos)))
				return !leading;
		}
		if (!leading && !inQuote) {
			if (CUShortArray_Contains(delimSet, *CIntArray_At(text, *pos)))
				return !leading;
		}
		if (leading && !inQuote) {
			if (CUShortArray_Contains(delimSet, *CIntArray_At(text, *pos))) {
				CUString_ConcatChar(out, *CIntArray_At(text, *pos));
				if (*CIntArray_At(text, *pos) == '\n')
					(*lineNo)++;
				*pos += step;
				leading = 0;
				return !leading;
			}
		}
		if (leading) {
			if (!CUShortArray_Contains(wsSet, *CIntArray_At(text, *pos)))
				leading = 0;
		}

		ch = *CIntArray_At(text, *pos);
		if (ch == '"') {
			if (inQuote) {
				*pos += step;
				return !leading;
			}
			inQuote = 1;
		} else if (!leading) {
			CUString_ConcatChar(out, *CIntArray_At(text, *pos));
		}

		if (*CIntArray_At(text, *pos) == '\n')
			(*lineNo)++;
		*pos += step;
	}
}

/*
 * 0x004DE1E2 - CIntArray::ScanToDelim
 *
 * Advances *pos in the direction given by forward until a character in
 * delims is reached, counting newlines into *lineNo. Returns 1 when a
 * delimiter was found, 0 when the terminator was reached first.
 */
static __attribute__((unused)) int
CIntArray_ScanToDelim(CIntArray *text, int *pos, int *lineNo, CIntArray *delims, int forward)
{
	int step;

	step = forward != 0 ? 1 : -1;
	for (;;) {
		if (*CIntArray_At(text, *pos) == 0)
			return 0;
		if (CUShortArray_Contains(delims, *CIntArray_At(text, *pos)))
			return 1;
		if (*CIntArray_At(text, *pos) == '\n')
			(*lineNo)++;
		*pos += step;
	}
}

/*
 * 0x004DE279 - CIntArray::IsAllDigits
 *
 * Returns 1 when every element is an ASCII digit, 0 otherwise. An empty
 * array returns 1.
 */
static __attribute__((unused)) int
CIntArray_IsAllDigits(CIntArray *arr)
{
	int i;

	for (i = 0; i < (int)arr->count; i++) {
		if (*CIntArray_At(arr, i) < '0' || *CIntArray_At(arr, i) > '9')
			return 0;
	}
	return 1;
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
