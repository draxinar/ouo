/*
 * MSVC std::basic_streambuf<char> methods.
 *
 * Internal accessor and manipulator functions reproduced from MSVC's
 * STL; used by the Wombat scripting engine's stream classes.
 */

#include <stddef.h>
#include <stdint.h>

#include "dat.h"
#include "wombat.h"

static CStreamPos *CStreamPos_Constructor(CStreamPos *this, uint32_t offset); // 0x00402F10
static CStreamPos *CStreamPos_Constructor3(CStreamPos *this, uint32_t begin, uint32_t end, uint32_t offset); // 0x00402F60
static void CStreamBuf_Setg(CStreamBuf *this, uintptr_t gbeg, uintptr_t gcur, uintptr_t gcount); // 0x00404300
static void CStreamBuf_Setp(CStreamBuf *this, uintptr_t pbeg, uintptr_t pend); // 0x00404340

// MSVC STL default state globals (all zero in binary .bss)
// 0x005F7728 - streampos default _Off2 field
static uint32_t g_streampos_default_Off2;
// 0x005F772C - streampos default _State field
static uint32_t g_streampos_default_State;
// 0x009A3C04 - streampos default _State2 field
static uint32_t g_streampos_default_State2;

/*
 * 0x00402F10 - CStreamPos constructor
 *
 * Initializes the stream position fields from MSVC STL default-state
 * globals.
 */
static __attribute__((unused)) CStreamPos *
CStreamPos_Constructor(CStreamPos *this, uint32_t offset)
{
	this->_Off = offset;
	this->_Off2 = g_streampos_default_Off2;
	this->_State = g_streampos_default_State;
	this->_State2 = g_streampos_default_State2;
	return this;
}

/*
 * 0x00402F60 - CStreamPos constructor (3 args)
 *
 * Initializes stream position with given offsets.
 */
static __attribute__((unused)) CStreamPos *
CStreamPos_Constructor3(CStreamPos *this, uint32_t begin, uint32_t end, uint32_t offset)
{
	this->_Off = 0;
	this->_Off2 = end;
	this->_State = offset;
	this->_State2 = begin;
	return this;
}

/*
 * 0x00402FA0 - CStreamPos::GetState2
 *
 * Returns the _State2 field.
 */
int32_t
CStreamPos_GetState2(CStreamPos *pos)
{
	return pos->_State2;
}

/*
 * 0x00402FC0 - CStreamPos::GetState
 *
 * Returns _Off2 in low 32 bits and _State in high 32 bits.
 */
int64_t
CStreamPos_GetState(CStreamPos *pos)
{
	int64_t result;

	result = (int64_t)(uint32_t)pos->_Off2;
	result |= (int64_t)(uint32_t)pos->_State << 32;
	return result;
}

/*
 * 0x00402FE0 - CStreamPos::GetEndOffset
 *
 * Returns this->_Off + this->_Off2.
 */
int32_t
CStreamPos_GetEndOffset(CStreamPos *pos)
{
	return pos->_Off + pos->_Off2;
}

/*
 * 0x00403000 - CStreamBuf::pubsync
 *
 * Calls the sync virtual function via vtable[11].
 */
int
CStreamBuf_pubsync(CStreamBuf *sb)
{
	typedef int (*SyncFn)(CStreamBuf *);
	SyncFn fn = (SyncFn)(uintptr_t)sb->vtable[SB_VT_SYNC];
	return fn(sb);
}

/*
 * 0x00403020 - CStreamBuf::sputc
 *
 * Puts a character into the put area. If pptr() is non-NULL and
 * pptr() < epptr(), stores the character at the current put position
 * and advances it. Otherwise falls back to the overflow virtual
 * function (vtable[1]).
 */
int
CStreamBuf_sputc(CStreamBuf *sb, char c)
{
	char *pp;
	char *ep;

	pp = CStreamBuf_pptr(sb);
	if (pp != 0) {
		pp = CStreamBuf_pptr(sb);
		ep = CStreamBuf_epptr(sb);
		if ((uintptr_t)pp < (uintptr_t)ep) {
			char *old;
			old = CStreamBuf_Pninc(sb);
			*old = c;
			return char_traits_to_int_type(old);
		}
	}
	{
		typedef int (*OverflowFn)(CStreamBuf *, int);
		OverflowFn fn = (OverflowFn)(uintptr_t)sb->vtable[SB_VT_OVERFLOW];
		return fn(sb, char_traits_to_int_type(&c));
	}
}

/*
 * 0x004030A0 - CStreamBuf::eback
 *
 * Returns the beginning of the get area: *_IGfirst.
 */
char *
CStreamBuf_eback(CStreamBuf *sb)
{
	return *sb->_IGfirst;
}

/*
 * 0x004030C0 - CStreamBuf::gptr
 *
 * Returns the current get position: *_IGnext.
 */
char *
CStreamBuf_gptr(CStreamBuf *sb)
{
	return *sb->_IGnext;
}

/*
 * 0x004030E0 - CStreamBuf::pptr
 *
 * Returns the current put position: *_IPnext.
 */
char *
CStreamBuf_pptr(CStreamBuf *sb)
{
	return *sb->_IPnext;
}

/*
 * 0x00403100 - CStreamBuf::egptr
 *
 * Returns the end of the get area: *_IGnext + *_IGcount.
 */
char *
CStreamBuf_egptr(CStreamBuf *sb)
{
	return *sb->_IGnext + *sb->_IGcount;
}

/*
 * 0x00403120 - CStreamBuf::gbump
 *
 * Advances the get pointer by n positions.
 * Decrements get count and increments get pointer.
 */
void
CStreamBuf_gbump(CStreamBuf *sb, int n)
{
	*sb->_IGcount -= n;
	*sb->_IGnext += n;
}

/*
 * 0x00403160 - CStreamBuf::epptr
 *
 * Returns the end of the put area: *_IPnext + *_IPcount.
 */
char *
CStreamBuf_epptr(CStreamBuf *sb)
{
	return *sb->_IPnext + *sb->_IPcount;
}

/*
 * 0x00403180 - CStreamBuf::_Gndec
 *
 * Decrements the get pointer by 1 (moves backward).
 * Returns the new get pointer value.
 */
char *
CStreamBuf_Gndec(CStreamBuf *sb)
{
	*sb->_IGcount += 1;
	*sb->_IGnext -= 1;
	return *sb->_IGnext;
}

/*
 * 0x004031C0 - CStreamBuf::_Gninc
 *
 * Increments the get pointer by 1 (moves forward).
 * Returns the old get pointer value (before increment).
 */
char *
CStreamBuf_Gninc(CStreamBuf *sb)
{
	char *old;

	*sb->_IGcount -= 1;
	old = *sb->_IGnext;
	*sb->_IGnext += 1;
	return old;
}

/*
 * 0x00403200 - CStreamBuf::pbump
 *
 * Advances the put pointer by n positions.
 * Decrements put count and increments put pointer.
 */
void
CStreamBuf_pbump(CStreamBuf *sb, int n)
{
	*sb->_IPcount -= n;
	*sb->_IPnext += n;
}

/*
 * 0x00403240 - CStreamBuf::_Pninc
 *
 * Increments the put pointer by 1 (moves forward).
 * Returns the old put pointer value (before increment).
 */
char *
CStreamBuf_Pninc(CStreamBuf *sb)
{
	char *old;

	*sb->_IPcount -= 1;
	old = *sb->_IPnext;
	*sb->_IPnext += 1;
	return old;
}

/*
 * 0x004039A0 - CStreamBuf constructor (init fields)
 *
 * Initializes the streambuf pointer, null-fill character, and calls
 * base init. If streambuf is NULL, sets error state via IosBase_SetState.
 * MSVC STL ios_base base-init (0x005C0280) and TLS registration
 * (0x005C0230, conditional on initFlag) are omitted.
 */
__attribute__((unused)) void
CStreamBuf_Constructor(CStreamBuf *this, void *streambuf, char initFlag)
{
	USED(initFlag);
	this->_IPfirst = (char **)streambuf;
	this->_IGcount = 0;
	char fillch = StdGetByte(0x20); // space character
	*(char *)&this->_IPcount = fillch;
	if (streambuf == NULL)
		IosBase_SetState((CIosBase *)this, 4, 0);
}

/*
 * 0x00404150 - CStreamBuf internal init (set pointers to self-offsets)
 *
 * Sets up the get/put area pointer pairs to point at the object's own
 * inline fields, then calls CStreamBuf_Setp and CStreamBuf_Setg with
 * zeros to initialize.
 */
__attribute__((unused)) void
CStreamBuf_InitPtrs(CStreamBuf *this)
{
	this->_IGfirst = &this->_Gfirst;
	this->_Gcount = (intptr_t)&this->_Gnext;
	this->_IGnext = (char **)&this->_Pfirst;
	this->_IPnext = (char **)&this->_Pnext;
	this->_IGcount = &this->_Pcount;
	this->_IPcount = (intptr_t *)&this->_IPfirst;
	CStreamBuf_Setp(this, 0, 0);
	CStreamBuf_Setg(this, 0, 0, 0);
}

/*
 * 0x004041C0 - CStreamBuf set all pointers
 *
 * Sets the six pointer-pair target fields.
 */
__attribute__((unused)) void
CStreamBuf_SetAll(CStreamBuf *this, uintptr_t igFirst, uintptr_t igNext, uintptr_t igCount, uintptr_t gCount, uintptr_t ipNext, uintptr_t ipCount)
{
	this->_IGfirst = (char **)igFirst;
	this->_Gcount = (intptr_t)gCount;
	this->_IGnext = (char **)igNext;
	this->_IPnext = (char **)ipNext;
	this->_IGcount = (intptr_t *)igCount;
	this->_IPcount = (intptr_t *)ipCount;
}

/*
 * 0x00404300 - CStreamBuf::setg (set get area pointers)
 *
 * Sets the get area begin, current, and count via indirect pointers.
 */
static void
CStreamBuf_Setg(CStreamBuf *this, uintptr_t gbeg, uintptr_t gcur, uintptr_t gcount)
{
	*(uintptr_t *)this->_IGfirst = gbeg;
	*(uintptr_t *)this->_IGnext = gcur;
	*(uintptr_t *)this->_IGcount = gcount - gcur;
}

/*
 * 0x00404340 - CStreamBuf::setp (set put area pointers)
 *
 * Sets the put area begin, current, and count via indirect pointers.
 */
static void
CStreamBuf_Setp(CStreamBuf *this, uintptr_t pbeg, uintptr_t pend)
{
	// _Gcount holds an indirect pointer (set by InitPtrs to &_Gnext)
	*(uintptr_t *)this->_Gcount = pbeg;
	*(uintptr_t *)this->_IPnext = pbeg;
	*(uintptr_t *)this->_IPcount = pend - pbeg;
}
