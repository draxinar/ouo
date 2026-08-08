/*
 * CString - reference-counted char string.
 *
 * Construction, copy-on-write assignment, concatenation, and stream
 * helpers reproduced from MSVC's binary layout.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cstring.h"
#include "region.h"

/*
 * 0x0063E160 - g_EmptyCString
 *
 * Shared empty-string sentinel: CStrings default-construct pointing
 * here so callers never observe a NULL data pointer.
 */
CString g_EmptyCString = { NULL, 0, 0, 0 };

// 0x006EFF24 - static null byte returned by CString_CharAt on out-of-bounds
static char CString_nullbyte;

/*
 * 0x0040CC80 - CString::~CString (scalar deleting destructor)
 *
 * Runs the destructor and optionally frees the object.
 */
CString *
CString_ScalarDelete(CString *s, int flags)
{
	CString_Destructor(s);
	if (flags & 1)
		OperatorDelete(s);
	return s;
}

/*
 * 0x0043EBC0 - CString::DefaultConstructorWrap
 *
 * Thin wrapper around CString_DefaultConstructor that returns str.
 */
CString *
CString_DefaultConstructorWrap(CString *str)
{
	CString_DefaultConstructor(str);
	return str;
}

/*
 * 0x0043EBE0 - CString::~CString (destructor wrap)
 *
 * Thin wrapper around CString_Destructor with no return value.
 */
void __attribute__((unused))
CString_DestructorWrap(CString *str)
{
	CString_Destructor(str);
}

/*
 * 0x004D2F80 - CString::CString (default constructor)
 *
 * Zeroes fields, sets refCount = 1, and assigns the empty string.
 */
void
CString_DefaultConstructor(CString *s)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	CString_AssignInternal(s, "");
}

/*
 * 0x004D2FC2 - CString::~CString
 *
 * Frees the backing data buffer and clears the pointer.
 */
void
CString_Destructor(CString *s)
{
	if (s->data != NULL) {
		OperatorDelete(s->data);
		s->data = NULL;
	}
}

/*
 * 0x004D2FF4 - CString::CString(const CString&)
 *
 * Copy constructor: zero-initializes fields, then copies other's
 * C-string contents via CString_AssignInternal.
 */
CString *
CString_CopyConstructor(CString *s, CString *other)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	CString_AssignInternal(s, CString_GetCStr(other));
	return s;
}

/*
 * 0x004D303C - CString::CString(LPCSTR)
 *
 * Initializes this to empty then assigns from str.
 */
CString *
CString_Constructor(CString *s, const char *str)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	CString_AssignInternal(s, str);
	if (s->data == NULL) {
		s->capacity = 0;
		s->length = 0;
	}
	return s;
}

/*
 * 0x004D309B - CString::CString(char)
 *
 * Constructs a CString from a single character.
 *
 * FIXED: Binary sets capacity = refCount (1), allocates 1 byte, then
 * writes char + NUL (2 bytes) - a 1-byte heap overflow. Allocate
 * refCount + 1 bytes to fit the NUL terminator.
 */
CString *
CString_ConstructorFromChar(CString *s, char c)
{
	s->data = NULL;
	s->length = 0;
	s->refCount = 1;
	s->capacity = 0;
	s->capacity = s->refCount + 1;
	s->data = (char *)OperatorNew(s->capacity);
	s->data[0] = c;
	s->data[1] = '\0';
	s->length = 1;
	return s;
}

/*
 * 0x004D310F - CString::operator=(const char *)
 *
 * Assigns str, reallocating the buffer if needed.
 *
 * FIXED: Binary uses strcpy, which is undefined for overlapping buffers.
 * Script_removePrefix passes str pointing into this CString's own data
 * (str = s->data + prefixLen); MSVC's left-to-right strcpy happens to
 * work on Windows but is UB per C. Using memmove handles the overlap
 * correctly everywhere.
 *
 * FIXED: When str == NULL and s->data is also NULL, the binary's
 * Clear2 (0x004D4260) only clears existing buffers, so s->data stays
 * NULL and the next strcasecmp/strcpy on the CString segfaults.
 * Allocate a 1-byte "" buffer so the CString is always valid.
 */
char *
CString_AssignInternal(CString *s, const char *str)
{
	int needed;

	if (str == NULL) {
		if (s->data == NULL) {
			s->capacity = (s->refCount > 0) ? s->refCount : 1;
			s->data = (char *)malloc(s->capacity);
		}
		if (s->data != NULL)
			s->data[0] = '\0';
		s->length = 0;
		return NULL;
	}
	needed = strlen(str) + 1;
	if (needed > s->capacity) {
		if (s->data != NULL)
			OperatorDelete(s->data);
		s->capacity = strlen(str) + s->refCount;
		s->data = (char *)malloc(s->capacity);
	}
	memmove(s->data, str, needed);
	s->length = strlen(s->data);
	return s->data;
}

/*
 * 0x004D31BD - CString::AssignRange
 *
 * Assigns up to maxCount characters from str, clearing the string on NULL.
 */
char *
CString_AssignRange(CString *s, const char *str, int maxCount)
{
	int srcLen, finalLen;

	if (str == NULL) {
		CString_Clear2(s);
		return NULL;
	}

	srcLen = strlen(str);
	if (srcLen < maxCount)
		finalLen = srcLen;
	else
		finalLen = maxCount;

	if (finalLen + 1 > s->capacity) {
		if (s->data != NULL)
			OperatorDelete(s->data);
		s->capacity = finalLen + s->refCount;
		s->data = (char *)OperatorNew(s->capacity);
	}

	memcpy(s->data, str, finalLen);
	s->data[finalLen] = '\0';
	s->length = finalLen;

	return s->data;
}

/*
 * 0x004D3289 - CString::ConcatCStr
 *
 * Appends a C string, growing the buffer if needed.
 */
char *
CString_ConcatCStr(CString *s, const char *str)
{
	char *newBuf;

	if (str == NULL || str[0] == '\0')
		return s->data;

	if (s->length + (int)strlen(str) + 1 > s->capacity) {
		s->capacity = s->length + (int)strlen(str) + s->refCount;
		newBuf = (char *)OperatorNew(s->capacity);
		newBuf[0] = '\0';
		if (s->data != NULL)
			strcpy(newBuf, s->data);
		strcat(newBuf, str);
		if (s->data != NULL)
			OperatorDelete(s->data);
		s->data = newBuf;
	} else {
		strcat(s->data, str);
	}
	s->length = strlen(s->data);
	return s->data;
}

/*
 * 0x004D3387 - CString::GetCStr
 *
 * Returns the NUL-terminated data pointer.
 */
char *
CString_GetCStr(CString *s)
{
	return s->data;
}

/*
 * 0x004D3397 - CString::GetData
 *
 * Returns the backing data pointer.
 */
char *
CString_GetData(CString *s)
{
	return s->data;
}

/*
 * 0x004D33A7 - CString::operator=(const char *)
 *
 * Assigns the C-string str to the CString.
 */
CString *
CString_AssignCStr(CString *s, const char *str)
{
	CString_AssignInternal(s, str);
	return s;
}

/*
 * 0x004D33C3 - CString::operator=(const CString&)
 *
 * Copies the contents of other into this string.
 */
CString *
CString_Assign(CString *s, CString *other)
{
	CString_AssignInternal(s, other->data);
	return s;
}

/*
 * 0x004D33E1 - CString::SetFromInt
 *
 * Formats value as a decimal string and assigns it.
 */
CString *
CString_SetFromInt(CString *s, int value)
{
	char buf[256];
	sprintf(buf, "%d", value);
	CString_AssignInternal(s, buf);
	return s;
}

/*
 * 0x004D3426 - CString::SetFromChar
 *
 * Sets the string to a single character.
 */
CString *
CString_SetFromChar(CString *s, char c)
{
	if (s->capacity < 2) {
		OperatorDelete(s->data);
		s->data = (char *)OperatorNew(2);
	}
	s->data[0] = c;
	s->data[1] = '\0';
	s->length = 1;
	return s;
}

/*
 * 0x004D3481 - CString::operator+=(const CString&)
 *
 * Appends other's contents to this string.
 */
CString *
CString_ConcatCString(CString *s, CString *other)
{
	CString_ConcatCStr(s, other->data);
	return s;
}

/*
 * 0x004D349F - CString::operator+=(const char *)
 *
 * Appends the C-string str to this string.
 */
CString *
CString_AppendCStr(CString *s, const char *str)
{
	CString_ConcatCStr(s, str);
	return s;
}

/*
 * 0x004D34BB - CString::ConcatInt
 *
 * Formats value as a decimal string and appends it.
 */
CString *
CString_ConcatInt(CString *s, int value)
{
	char buf[256];
	sprintf(buf, "%d", value);
	CString_ConcatCStr(s, buf);
	return s;
}

/*
 * 0x004D3500 - CString::operator+=(float)
 *
 * Formats the value with "%f" into a 256-byte stack buffer and appends
 * it. The argument is promoted to double for the sprintf call.
 */
static __attribute__((unused)) CString *
CString_ConcatFloat(CString *s, float value)
{
	char buf[256];

	sprintf(buf, "%f", (double)value);
	CString_ConcatCStr(s, buf);
	return s;
}

/*
 * 0x004D354A - CString::ConcatUInt
 *
 * Formats value as an unsigned decimal string and appends it.
 */
CString *
CString_ConcatUInt(CString *s, unsigned int value)
{
	char buf[256];
	sprintf(buf, "%u", value);
	CString_ConcatCStr(s, buf);
	return s;
}

/*
 * 0x004D358F - CString::ConcatChar
 *
 * Appends a single character, growing the buffer if needed.
 */
CString *
CString_ConcatChar(CString *s, char c)
{
	char *newBuf;

	if (s->length + 2 > s->capacity) {
		newBuf = (char *)OperatorNew(s->length + 2);
		if (s->data != NULL) {
			strcpy(newBuf, s->data);
			OperatorDelete(s->data);
		}
		newBuf[s->length] = c;
		newBuf[s->length + 1] = '\0';
		s->data = newBuf;
	} else {
		s->data[s->length] = c;
		s->data[s->length + 1] = '\0';
	}
	s->length = strlen(s->data);
	return s;
}

/*
 * 0x004D3680 - CString::operator==(const CString&)
 *
 * Case-insensitive comparison. Returns 1 if equal, 0 otherwise.
 */
int
CString_EqualCString(CString *s, CString *other)
{
	return strcasecmp(s->data, other->data) == 0;
}

/*
 * 0x004D36AE - CString::operator==(const CString&)
 *
 * Duplicate of 0x004D3680 at a separate binary entry point.
 */
int
CString_EqualCString2(CString *s, CString *other)
{
	return strcasecmp(s->data, other->data) == 0;
}

/*
 * 0x004D36DC - CString::CompareStr
 *
 * Case-sensitive compare. Returns 1 if equal, 0 otherwise.
 */
int
CString_CompareStr(CString *s, const char *str)
{
	return strcasecmp(s->data, str) == 0;
}

/*
 * 0x004D3708 - CString::operator!=(char)
 *
 * Returns 0 when the string holds exactly one character equal to c
 * ignoring case, 1 otherwise.
 */
static __attribute__((unused)) int
CString_NotEqualChar(CString *s, char c)
{
	if (s->length != 1)
		return 1;
	if (tolower((unsigned char)s->data[0]) == tolower((unsigned char)c))
		return 0;
	return 1;
}

/*
 * 0x004D3754 - CString::operator!=(const CString&)
 *
 * Case-insensitive comparison via stricmp. Returns 1 if different.
 */
static __attribute__((unused)) int
CString_NotEqualCString(CString *s, CString *other)
{
	return strcasecmp(s->data, other->data) != 0;
}

/*
 * 0x004D3782 - CString::CompareNoCase
 *
 * Case-insensitive compare. Returns 0 if equal, 1 if not.
 */
int
CString_CompareNoCase(CString *s, const char *str)
{
	return strcasecmp(s->data, str) != 0;
}

/*
 * 0x004D37AE - CString::CharAt
 *
 * Returns pointer to the char at index, or a static null byte if OOB.
 */
char *
CString_CharAt(CString *s, unsigned int index)
{
	if (index < (unsigned int)s->length)
		return s->data + index;
	return &CString_nullbyte;
}

/*
 * 0x004D37D5 - CString::operator+(const char *)
 *
 * Returns a pointer to a lazy-initialized static CString holding
 * this + str. Not thread-safe (matches binary behavior).
 */
CString *
CString_OpPlusCStr(CString *s, const char *str)
{
	static CString g_opPlusCStr;
	static int g_opPlusCStr_init;

	if (!g_opPlusCStr_init) {
		g_opPlusCStr_init = 1;
		CString_DefaultConstructor(&g_opPlusCStr);
	}
	CString_AssignCStr(&g_opPlusCStr, s->data);
	CString_AppendCStr(&g_opPlusCStr, str);
	return &g_opPlusCStr;
}

/*
 * 0x004D3848 - CString::operator+(char)
 *
 * Same as 0x004D37D5 but appends a single character.
 */
static __attribute__((unused)) CString *
CString_OpPlusChar(CString *s, char c)
{
	static CString g_opPlusChar;
	static int g_opPlusChar_init;

	if (!g_opPlusChar_init) {
		g_opPlusChar_init = 1;
		CString_DefaultConstructor(&g_opPlusChar);
	}
	CString_AssignCStr(&g_opPlusChar, s->data);
	CString_ConcatChar(&g_opPlusChar, c);
	return &g_opPlusChar;
}

/*
 * 0x004D38BB - CString::operator+(const CString&)
 *
 * Same as 0x004D37D5 but appends a CString.
 */
CString *
CString_OpPlusCString(CString *s, CString *other)
{
	static CString g_opPlusCString;
	static int g_opPlusCString_init;

	if (!g_opPlusCString_init) {
		g_opPlusCString_init = 1;
		CString_DefaultConstructor(&g_opPlusCString);
	}
	CString_AssignCStr(&g_opPlusCString, s->data);
	CString_ConcatCString(&g_opPlusCString, other);
	return &g_opPlusCString;
}

// 0x006EFEE0 - static CString returned by CString_Mid
static CString g_MidResult = { NULL, 0, 1, 0 };

/*
 * 0x004D392E - CString::MakeUpper
 *
 * Uppercases the string in place via toupper.
 */
static __attribute__((unused)) CString *
CString_MakeUpper(CString *s)
{
	char *p;

	if (s->data != NULL && s->data[0] != '\0') {
		for (p = s->data; *p != '\0'; p++)
			*p = (char)toupper((unsigned char)*p);
	}
	return s;
}

/*
 * 0x004D3988 - CString::MakeLower
 *
 * Lowercases the string in place via tolower.
 */
static __attribute__((unused)) CString *
CString_MakeLower(CString *s)
{
	char *p;

	if (s->data != NULL && s->data[0] != '\0') {
		for (p = s->data; *p != '\0'; p++)
			*p = (char)tolower((unsigned char)*p);
	}
	return s;
}

/*
 * 0x004D39E2 - CString::Prepend(char)
 *
 * Inserts a character in front of the current contents, growing the
 * buffer by refCount bytes when length + 1 no longer fits.
 *
 * Prepend never rewrites capacity, so repeated calls keep reallocating
 * from the same stale value. Reproduced as it stands - nothing calls
 * this.
 */
static __attribute__((unused)) CString *
CString_PrependChar(CString *s, char c)
{
	char *newBuf;
	int cap;

	if (s->length + 1 < s->capacity)
		cap = s->capacity;
	else
		cap = s->capacity + s->refCount;

	newBuf = (char *)OperatorNew(cap);
	newBuf[0] = c;
	strcpy(newBuf + 1, s->data);
	OperatorDelete(s->data);
	s->data = newBuf;
	s->length = strlen(s->data);
	return s;
}

/*
 * 0x004D3A81 - CString::Prepend(const char *)
 *
 * Inserts a C string in front of the current contents. The growth step
 * is the larger of the inserted length and refCount.
 *
 * The old buffer is leaked - unlike 0x004D39E2 this one never releases
 * it. Reproduced as it stands - nothing calls this.
 */
static __attribute__((unused)) CString *
CString_PrependCStr(CString *s, const char *str)
{
	char *newBuf;
	int grow;
	int cap;

	grow = (int)strlen(str);
	if (grow < s->refCount)
		grow = s->refCount;
	if (s->length + grow < s->capacity)
		cap = s->capacity;
	else
		cap = s->capacity + grow;

	newBuf = (char *)OperatorNew(cap);
	strcpy(newBuf, str);
	strcat(newBuf, s->data);
	s->data = newBuf;
	s->length = strlen(s->data);
	return s;
}

/*
 * 0x004D3B3F - CString::Prepend(const CString&)
 *
 * Same as 0x004D3A81 but takes the inserted length from other's length
 * field instead of measuring it, and leaks the old buffer the same way.
 */
static __attribute__((unused)) CString *
CString_PrependCString(CString *s, CString *other)
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

	newBuf = (char *)OperatorNew(cap);
	strcpy(newBuf, other->data);
	strcat(newBuf, s->data);
	s->data = newBuf;
	s->length = strlen(s->data);
	return s;
}

/*
 * 0x004D3BF9 - CString::Mid
 *
 * Returns a pointer to a static CString holding the substring of obj
 * starting at nFirst for up to nCount chars. Caller must copy before
 * the next call.
 */
CString *
CString_Mid(CString *obj, int nFirst, int nCount)
{
	char *data;
	int len;

	CString_AssignInternal(&g_MidResult, "");

	len = CString_GetLength(obj);
	if (nFirst >= len)
		return &g_MidResult;

	data = CString_GetBuffer(obj);

	CString_AssignRange(&g_MidResult, data + nFirst, nCount);

	return &g_MidResult;
}

/*
 * 0x004D3C86 - CString::GetBuffer
 *
 * Returns the backing data pointer.
 */
char *
CString_GetBuffer(CString *s)
{
	return s->data;
}

/*
 * 0x004D3C96 - CString::GetCStr
 *
 * Duplicate of GetData at a separate binary entry point.
 */
char *
CString_GetCStr2(CString *s)
{
	return s->data;
}

/*
 * 0x004D3CA6 - CString::GetString
 *
 * Returns atoi(this->data). Misnamed in the binary.
 */
int
CString_GetString(CString *s)
{
	return atoi(s->data);
}

/*
 * 0x004D3CBF - CString::operator<(const CString&)
 *
 * Returns 1 when this string sorts strictly before other.
 */
int
CString_LessThan(CString *s, CString *other)
{
	return strcmp(s->data, CString_GetBuffer(other)) < 0;
}

/*
 * 0x004D3CEC - CString::operator>(const CString&)
 *
 * Returns 1 when this string sorts strictly after other.
 */
int
CString_GreaterThan(CString *s, CString *other)
{
	return strcmp(s->data, CString_GetBuffer(other)) > 0;
}

/*
 * 0x004D3E13 - CString::Contains
 *
 * Returns 1 if c appears in the string.
 */
int
CString_Contains(CString *s, char c)
{
	int i;

	if (s->data == NULL)
		return 0;
	for (i = 0; i < s->length; i++) {
		if (*CString_CharAt(s, i) == c)
			return 1;
	}
	return 0;
}

/*
 * 0x004D3E6D - CString::Replace
 *
 * Scans source for '%' markers. Non-'%' chars are copied to dest.
 * When '%' is followed by the trigger character (find[0]) the
 * replace string is appended; other escapes are silently consumed.
 * Returns the number of replacements.
 */
int
CString_Replace(CString *dest, CString *source, CString *find, CString *replace)
{
	char triggerChar;
	int len;
	int i;
	int count;
	char ch;

	triggerChar = *CString_CharAt(find, 0);
	count = 0;
	len = CString_GetLength(source);

	for (i = 0; i < len; i++) {
		ch = *CString_CharAt(source, i);
		if (ch == '\0')
			break;
		if (ch != '%') {
			CString_ConcatChar(dest, ch);
			continue;
		}
		i++;
		if (i >= len)
			break;
		// Binary quirk: ch still holds '%' here and is compared
		// against triggerChar, so the swap only fires when find
		// itself starts with '%'.
		if (ch != triggerChar)
			continue;
		CString_ConcatCString(dest, replace);
		count++;
	}
	return count;
}

/*
 * 0x004D3F22 - CString::Tokenize
 *
 * Extracts the next token into outToken starting at *pos. Skips leading
 * delimiters, honors quoted strings, returns lone whitespace as its own
 * token, and counts newlines into *lineCount. direction: 0 = reverse.
 * Returns 1 if a token was produced.
 */
int
CString_Tokenize(CString *s, int *pos, int *lineCount, CString *outToken, CString *delimiters, CString *whitespace, int direction)
{
	int isFirst;
	int inQuote;
	int step;

	isFirst = 1;
	inQuote = 0;
	CString_AssignCStr(outToken, "");
	step = (direction != 0) ? 1 : -1;

	for (;;) {
		if (*CString_CharAt(s, *pos) == '\0')
			break;
		if (inQuote == 0 && isFirst == 0) {
			if (CString_Contains(delimiters, *CString_CharAt(s, *pos)))
				break;
		}
		if (isFirst == 0 && inQuote == 0) {
			if (CString_Contains(whitespace, *CString_CharAt(s, *pos)))
				break;
		}
		if (isFirst == 1 && inQuote == 0) {
			if (CString_Contains(whitespace, *CString_CharAt(s, *pos))) {
				CString_ConcatChar(outToken, *CString_CharAt(s, *pos));
				if (*CString_CharAt(s, *pos) == '\n')
					(*lineCount)++;
				*pos += step;
				isFirst = 0;
				break;
			}
		}
		if (isFirst == 1) {
			if (!CString_Contains(delimiters, *CString_CharAt(s, *pos)))
				isFirst = 0;
		}
		if (*CString_CharAt(s, *pos) == '"') {
			if (inQuote == 0) {
				inQuote = 1;
			} else {
				*pos += step;
				inQuote = 0;
				break;
			}
		} else if (isFirst == 0) {
			CString_ConcatChar(outToken, *CString_CharAt(s, *pos));
		}
		if (*CString_CharAt(s, *pos) == '\n')
			(*lineCount)++;
		*pos += step;
	}
	return (isFirst == 0) ? 1 : 0;
}

/*
 * 0x004D4119 - CString::SkipToDelimiter
 *
 * Advances *pos to the next delimiter, counting newlines in *lineCount.
 * Returns 1 if a delimiter was reached, 0 at end of string.
 */
int
CString_SkipToDelimiter(CString *s, int *pos, int *lineCount, CString *delimiters, int direction)
{
	int step;

	step = (direction != 0) ? 1 : -1;

	for (;;) {
		if (*CString_CharAt(s, *pos) == '\0')
			return 0;
		if (CString_Contains(delimiters, *CString_CharAt(s, *pos)))
			return 1;
		if (*CString_CharAt(s, *pos) == '\n')
			(*lineCount)++;
		*pos += step;
	}
}

/*
 * 0x004D41AB - CString::IsNumeric
 *
 * Returns 1 if every character is a decimal digit (empty string is true).
 */
int
CString_IsNumeric(CString *s)
{
	int i;

	for (i = 0; i < s->length; i++) {
		if (*CString_CharAt(s, i) < '0')
			return 0;
		if (*CString_CharAt(s, i) > '9')
			return 0;
	}
	return 1;
}

/*
 * 0x004D4208 - CString::IsEmpty
 *
 * Returns 1 when the string has no data pointer or starts with a NUL.
 */
int
CString_IsEmpty(CString *s)
{
	if (s->data == NULL)
		return 1;
	if (s->data[0] == '\0')
		return 1;
	return 0;
}

/*
 * 0x004D4230 - CString::Clear
 *
 * Writes a NUL at data[0] and resets length. Buffer is kept.
 */
void
CString_Clear(CString *s)
{
	if (s->data != NULL)
		s->data[0] = '\0';
	s->length = 0;
}

/*
 * 0x004D4260 - CString::Clear2
 *
 * Duplicate of Clear at a separate binary entry point.
 */
void
CString_Clear2(CString *s)
{
	if (s->data != NULL)
		s->data[0] = '\0';
	s->length = 0;
}
