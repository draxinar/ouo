/*
 * wombat_stl.c - MSVC C++ runtime closure pulled in by the SDB loader.
 *
 * The wombat scripting subsystem indexes script strings through
 * CScriptStringDB / CSdbStr. An orphaned helper, CScriptStringDB_BuildIndex,
 * uses a std::ifstream to parse sdb.txt - and that single std::ifstream
 * drags in the entire MSVC STL templated graph the binary instantiated
 * to support it:
 *
 *   - basic_filebuf<char>, basic_fstream<char>, basic_string<char>
 *   - locale + locale::facet + locale::id + std::use_facet
 *   - std::char_traits<char>, std::allocator
 *   - std::_Tree<basic_string<char>>
 *   - MFC's CStdioFile subset (used as the file backing for basic_filebuf)
 *   - the static-init guards bracketing their construction
 *
 * BuildIndex itself has zero callers in the binary - it is compiled-in
 * dead code carried for decompilation completeness, and so is everything
 * here. This translation unit exists so stl.c can stay focused on
 * container primitives that other subsystems actually link against.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "dat.h"
#include "dice.h"
#include "io.h"
#include "item.h"
#include "multi.h"
#include "region.h"
#include "stl.h"
#include "weapon.h"

static void CSdbStrVector_Destructor(CScriptStringDB *this); // 0x00401057
static void CSdbStrVector_Init(CScriptStringDB *this); // 0x0040106A
static void *Tree_Isnil_Guard(StdTreeNode *this); // 0x00401400
static unsigned char Tree_Isnil(StdTreeNode *this); // 0x00401430
static void Stream_DestructorHelper(CBasicFstream *this); // 0x00401490
static char *String_CStr(CSdbStr *this); // 0x00401510
static void CallFnPtrWithThis(uintptr_t *this, void (*fn)(void *)); // 0x00401540
static void *IStream_Putback(CBasicFstream *this, char ch); // 0x00401560
static void *IStream_Ipfx(CBasicFstream *this); // 0x004016A0
static void BasicIos_Init(CIosBase *this); // 0x00401710
static void CSdbStrVector_DestroyDealloc(CScriptStringDB *this); // 0x00401960
static void CSdbStrVector_Clear(CScriptStringDB *this); // 0x00401A50
static void *CSdbStr_VectorCtor(CBasicFstream *this, void *filename, int openMode, int constructBase); // 0x00401A80
static void Stream_SubDestructor(CIosBase *this); // 0x00401B60
static void Stream_SetVbptr(CStdioFileBuf *this); // 0x00401BD0
static void Stream_CloseFile(CBasicFstream *this); // 0x00401BF0
static void CSdbStr_VectorDtor(CStdioFileBuf *this); // 0x00401C20
static void CStdioFile_ScalarDelete(CLocaleFacet *this); // 0x00401CC0
static void *Locale_Facet_Decref(CLocaleFacet *this); // 0x00401D10
static void Tree_NodeDestructor(CStreamBuf *this); // 0x00401D70
static void *CharTraits_Copy(void *dest, void *src, uint32_t count); // 0x00401F30
static uint32_t StdList_DerefOrNil(uint32_t *valPtr); // 0x004022C0
static unsigned char CharTraits_Find(char val, int count); // 0x004022F0
static unsigned char CharTraits_FindUB(int val, int count); // 0x00402490
static unsigned char Fgetc_Wrapper(char *dest, void *file); // 0x004026C0
static void Stream_Destructor(CBasicFstream *this); // 0x004029C0
static void *String_ScalarDestructor2(CSdbStr *this, int flags); // 0x00402A20
static void *String_AppendN(CSdbStr *this, uint32_t count, char ch); // 0x00402A50
static void *CharTraits_Assign(uintptr_t dest, uint32_t count, void *src); // 0x00402AD0
static void String_AssignCStr(CSdbStr *this, void *src); // 0x00402AF0
static uint32_t Strlen_Wrapper(void *str); // 0x00402B20
static void *CString_Erase(CSdbStr *this, unsigned int offset, unsigned int count); // 0x00402B40
static void *Memmove_Wrapper(void *dest, void *src, uint32_t count); // 0x00402BF0
static char *String_Begin(CSdbStr *this); // 0x00402C10
static char *String_End(CSdbStr *this); // 0x00402C30
static void CString_Release_Stl(CSdbStr *this, int doFree); // 0x00402C70
static void *CString_ConstructorWithInit(CBasicFstream *this, void *subObj, char initData, int hasData, int constructBase); // 0x00402D30
static void IosBase_SetState2(CIosBase *this, uint32_t state, char excflag); // 0x00402DE0
static void *Tree_NodeConstructor(CIosBase *this); // 0x00402E20
static void *Stream_BaseConstructor(CIosBase *this); // 0x00402E40
static void *Iterator_Constructor(char *this, void *stream); // 0x00402EB0
static void Iterator_Destructor(char *this); // 0x00402EE0
static void *CSdbStrVector_InsertN(CScriptStringDB *this, uintptr_t position, CSdbStr *value); // 0x004032A0
static void *CSdbStrVector_EraseRange(CScriptStringDB *this, int begin, uint32_t count); // 0x004032F0
static void *CStdioFile_ConstructorAlloc(CStdioFileBuf *this, void *filePtr); // 0x00403370
static void *Locale_FacetConstructorAlloc(CLocaleFacet *this); // 0x004033E0
static void Locale_Facet_Incref(CLocaleFacet *this); // 0x00403450
static void *CStdioFile_Open(CStdioFileBuf *this, void *filename, void *mode); // 0x00403490
static void *CStdioFile_Close(CStdioFileBuf *this); // 0x004034E0
static void *VDispatch_10(uintptr_t *this, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7); // 0x00403520
static void *VDispatch_14(uintptr_t *this, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7); // 0x00403560
static void *String_AssignPtrLen(CSdbStr *this, void *src, uint32_t len); // 0x004035A0
static void String_Eos(CSdbStr *this, uint32_t len); // 0x004035F0
static void CString_LockBuffer(CSdbStr *this); // 0x00403640
static int String_Grow(CSdbStr *this, uint32_t newcap, int flag); // 0x004036C0
static uintptr_t PtrAdd_NullCheck(uintptr_t base, uintptr_t offset); // 0x00403800
static void String_Freeze(CSdbStr *this); // 0x00403850
static unsigned char Stream_GoodCheck(CBasicFstream *this); // 0x004038C0
static unsigned char Stream_OpenCheck(CIosBase *this); // 0x00403930
static void Iterator_CheckAdvance(CBasicFstream *this); // 0x00403950
static void StreamBuf_BaseConstructor(CStreamBuf *this); // 0x00403A10
static void CStdioFile_Setptr(CStdioFileBuf *this, uintptr_t filePtr, uint32_t openFlag); // 0x00403CC0
static void Stream_PostOpenLocaleSetup(CStdioFileBuf *this); // 0x00403DB0
static void *Locale_Facet_CopyAssign(uintptr_t *this, uintptr_t *src); // 0x00403ED0
static void *Locale_Facet_ReplaceAssign(uintptr_t *this, uintptr_t *src); // 0x00403F00
static unsigned int VDispatch_VT1(uintptr_t *this); // 0x00403F70
static void *Locale_Facet_InitSrc(CSdbStr *this, void *src); // 0x00403F90
static uint32_t String_GrowCap(CSdbStr *this); // 0x00403FC0
static void CString_Reserve(CSdbStr *this, unsigned int requestedCapacity); // 0x00404000
static void *Locale_Facet_InitCompile(CStreamBuf *this, void *src); // 0x00404110
static void *Allocator_Allocate(StdAllocator *this, uint32_t count); // 0x004042B0
static uint32_t String_MaxSize(CSdbStr *this); // 0x004042D0
static uint32_t Tree_SetSize(CStlTreeFull *this, uint32_t newSize); // 0x00404630
static void ScalarDestructor_Flags0(CSdbStr *obj); // 0x00404790
static void *locale_Getfacet(void *facets, int throwOnMissing, void *handle); // 0x004047A0
static void *CStdioFile_Constructor(CStdioFileBuf *this, int arg); // 0x004048C0
static void *CStdioFile_ConstructorArg(CStdioFileBuf *this, void *arg); // 0x004048E0
static void CStdioFile_DestructorBody(CStdioFileBuf *this); // 0x004048F0
static void *CStdioFile_CopyConstructorBody(CStdioFileBuf *this, CStdioFileBuf *src); // 0x00404970
static uint32_t Locale_Id_GetOrAlloc(uint32_t *this); // 0x004049A0
static void *Locale_UseFacet(void *arg1, void *arg2, void *arg3); // 0x004049F0
static void *OperatorNew_Clamp(int size); // 0x00404A80
static void *Allocator_Constructor16(void *dest, void *src); // 0x00404AA0
static void *String_CopyConstructor(CSdbStr *this, void *src); // 0x00404AE0
static void *String_Stl_Replace(CSdbStr *this, void *src, uint32_t offset, uint32_t count); // 0x00404B20
static void VDispatch_1C(uintptr_t *this, uint32_t a1, uint32_t a2); // 0x00404CC0
static void *Locale_ConstructorCategory(CLocaleCategory *this, void *arg); // 0x00404CE0
static void *CLocaleCategory_ConstructorBody(CLocaleCategory *this, void *arg); // 0x00404D70
static CLocaleFacet *Locale_Facet_Constructor(CLocaleFacet *this, void *arg); // 0x00404DA0
static void *CLocaleCategory_ScalarDtor(CLocaleCategory *this, int flags); // 0x00404E30
static void CLocaleCategory_DestructorBody(CLocaleCategory *this); // 0x00404E60
static void CLocaleCategory_VtableDtor(CLocaleCategory *this); // 0x00404F10
static int Locale_Facet_GetCat(void); // 0x00404F30
static void Locale_Facet_RegisterAtexit(void); // 0x00404F40
static void String_Copy(CSdbStr *this, void *src); // 0x00404FB0
static void CopySystemTime(CLocaleCategory *this, void *dest); // 0x00404FD0
static void *GetSystemTime8(uintptr_t *this, void *result); // 0x00405000
static void StaticInitGuard_1(void); // 0x004050C0
static void StaticInitGuard_2(void); // 0x004050F0
static int32_t g_StdioStreamDefault; /* 0x009A3C00 - MSVC CRT default stream position */
// MSVC vtable arrays from UoDemo.exe (used only in unused decompiled code)
static uintptr_t g_vt_IosBase[] = { 0 };            /* 0x005EE000 */
static uintptr_t g_vt_BasicFstream_Vbptr[] = { 0 };  /* 0x005EE004 */
static uintptr_t g_vt_BasicFstream[] = {
	                                           /* 0x005EE008 */
	0,         // [0] offset to self
	offsetof(CBasicFstream, ios)                  // [1] offset from vbptr to virtual base (CIosBase)
};

static uintptr_t g_vt_IStream[] = { 0 };             /* 0x005EE010 */
static uintptr_t g_vt_BasicFilebuf[14] = { 0 };      /* 0x005EE018 - 14 entries */
static uintptr_t g_vt_CStreamBuf[12] = { 0 };        /* 0x005EE050 - 12 entries */
static uintptr_t g_vt_CString[] = { 0 };             /* 0x005EE090 */
// 0x005EE088 - empty string buffer returned by basic_string<char>::_Nullstr
static char g_NullStr[4];
static uintptr_t g_vt_LocaleCategoryBase[] = { 0 };           /* 0x005EE098 */
static uintptr_t g_vt_StreamBase[] = { 0 };           /* 0x005F778C */
static uintptr_t g_vt_LocaleFacet[] = { 0 };          /* 0x005F77FC - std::locale::facet abstract base */
static uintptr_t g_vt_CStdioFile[] = { 0 };           /* 0x005F78FC */
static uintptr_t g_vt_LocaleCategory[] = { 0 };       /* 0x005F794C */
static uint32_t g_locale_facet_id;                  /* 0x009A3BFC */

/*
 * 0x00401000 - CScriptStringDB::CScriptStringDB
 *
 * Constructs the string vector, then loads path into it. The vector
 * constructor takes a stack element as its fill prototype.
 */
static __attribute__((unused)) CScriptStringDB *
CScriptStringDB_Constructor(CScriptStringDB *db, const char *path)
{
	char proto;

	proto = 0;
	CVector_Constructor((CVector *)db, &proto);
	CScriptStringDB_Load(db, path);
	return db;
}

/*
 * 0x00401057 - std::vector<CSdbStr>::~vector wrapper
 *
 * Delegates to the vector destructor.
 */
static __attribute__((unused)) void
CSdbStrVector_Destructor(CScriptStringDB *this)
{
	CSdbStrVector_DestroyDealloc(this);
}

/*
 * 0x0040106A - std::vector<CSdbStr>::init wrapper
 *
 * Delegates to the vector clear/init routine.
 */
static __attribute__((unused)) void
CSdbStrVector_Init(CScriptStringDB *this)
{
	CSdbStrVector_Clear(this);
}

/*
 * 0x004011D7 - CScriptStringDB::BuildIndex
 *
 * ORPHANED: zero callers in binary. The original constructed a local
 * std::set<CSdbStr>, populated it from the DB, then destructed the set
 * without ever using it - pure dead code. The MSVC red-black-tree internals
 * are not replicated in this codebase, so the body is empty.
 */
void
CScriptStringDB_BuildIndex(CScriptStringDB *db, void *arg)
{
	USED(db);
	USED(arg);
}

/*
 * 0x00401400 - std::_Tree::_Isnil check with null-self guard
 *
 * Returns this when Tree_Isnil reports non-nil, else NULL.
 */
static __attribute__((unused)) void *
Tree_Isnil_Guard(StdTreeNode *this)
{
	unsigned char r = Tree_Isnil(this);
	if (r)
		return NULL;
	return this;
}

/*
 * 0x00401430 - std::_Tree node _Isnil check
 *
 * Returns 1 when the node's color byte has bits 1 or 2 set.
 */
static unsigned char
Tree_Isnil(StdTreeNode *this)
{
	int val = CSearchCtx_GetBucket((CSearchCtx *)this);
	int masked = val & 6;
	return masked != 0;
}

/*
 * 0x00401490 - Stream destructor helper
 *
 * Destroys the stream sub-object then re-initializes the basic_ios base.
 */
static __attribute__((unused)) void
Stream_DestructorHelper(CBasicFstream *this)
{
	Stream_SubDestructor(&this->ios);
	BasicIos_Init(&this->ios);
}

/*
 * 0x004014C0 - basic_string<char> copy-assign from source
 *
 * Copies the first byte from src, releases own data, then assigns from
 * the c_str of dest.
 */
void *
String_CopyAssign(CSdbStr *this, void *dest, void *src)
{
	*(char *)this = *(char *)src;
	CString_Release_Stl(this, 0);
	String_AssignCStr(this, dest);
	return this;
}

/*
 * 0x004014F0 - basic_string<char>::_Tidy (release with free)
 *
 * Releases the string buffer and frees the storage.
 */
void
String_Tidy(CSdbStr *this)
{
	CString_Release_Stl(this, 1);
}

/*
 * 0x00401510 - basic_string<char>::c_str
 *
 * Returns the string's data pointer, or the static empty string when NULL.
 */
static char *
String_CStr(CSdbStr *this)
{
	if (this->data == NULL)
		return (char *)String_Nullstr();
	return this->data;
}

/*
 * 0x00401540 - Call function pointer with this
 *
 * Invokes the supplied function pointer with this as its argument.
 */
static __attribute__((unused)) void
CallFnPtrWithThis(uintptr_t *this, void (*fn)(void *))
{
	fn(this);
}

/*
 * 0x00401560 - std::istream::putback
 *
 * Constructs a sentry, checks the good-bit, then writes ch through the
 * underlying streambuf via sputc. Sets badbit when the sentry fails or
 * sputc returns EOF. Returns this.
 */
static __attribute__((unused)) void *
IStream_Putback(CBasicFstream *this, char ch)
{
	uintptr_t *p = (uintptr_t *)this;
	uint32_t state = 0;
	char sentry[16];

	Iterator_Constructor(sentry, this);

	// Binary calls thiscall byte-getter at 0x004e0270 (sentry good check)
	unsigned char good = CWeaponDef_GetId((CWeaponDef *)sentry);
	if (good == 0) {
		state |= 4;
	} else {
		// Binary calls thiscall reader at 0x00472f80 for rdbuf (ios+0x28)
		uintptr_t *vt = (uintptr_t *)p[0];
		CIosBase *ios = (CIosBase *)((char *)this + vt[FSTREAM_VBT_VBASE]);
		CStreamBuf *rdbuf = (CStreamBuf *)(intptr_t)CMultiSlave_GetTypeId((CMultiSlave *)ios);
		int result = CStreamBuf_sputc(rdbuf, ch);
		int eof = StdNilRef();
		if (CmpPtrValueEqual(&result, &eof))
			state |= 4;
	}

	{
		uintptr_t *vt2 = (uintptr_t *)p[0];
		CIosBase *sub2 = (CIosBase *)((char *)this + vt2[FSTREAM_VBT_VBASE]);
		IosBase_SetState(sub2, state, 0);
	}

	Iterator_Destructor(sentry);
	return this;
}

/*
 * 0x004016A0 - std::istream::_Ipfx (input prefix)
 *
 * Flushes the tied stream when the ios sub-object is non-nil; sets badbit
 * if pubsync reports an error.
 */
static void *
IStream_Ipfx(CBasicFstream *this)
{
	uintptr_t *p = (uintptr_t *)this;
	uintptr_t *vt = (uintptr_t *)p[0];
	CIosBase *ios = (CIosBase *)((char *)this + vt[FSTREAM_VBT_VBASE]);
	int state = 0;

	if (!Tree_Isnil((StdTreeNode *)ios)) {
		int result = CStreamBuf_pubsync((CStreamBuf *)ios->streambuf);
		if (result == -1)
			state |= 4;
	}
	ios = (CIosBase *)((char *)this + ((uintptr_t *)p[0])[FSTREAM_VBT_VBASE]);
	IosBase_SetState(ios, state, 0);
	return this;
}

/*
 * 0x00401710 - basic_ios::_Init (set vtable and call base init)
 *
 * Stamps the basic_ios vtable. The original also called the MSVC CRT base
 * class init which is a no-op here.
 */
static void
BasicIos_Init(CIosBase *this)
{
	this->vtable = (void **)g_vt_IosBase;
	// fcn.005c0150 is MSVC CRT base class init - no-op in our code
}

/*
 * 0x00401730 - std::ios_base::setstate wrapper
 *
 * ORs state into ios->state (when non-zero) and forwards to IosBase_SetState2.
 */
void
IosBase_SetState(CIosBase *this, uint32_t state, char excflag)
{
	if (state != 0) {
		uint32_t newstate = this->state | state;
		IosBase_SetState2(this, newstate, excflag);
	}
}

/*
 * 0x00401960 - std::vector<CSdbStr>::_Destroy_and_dealloc
 *
 * Destroys all elements in [begin, end), frees the backing buffer, and
 * resets the begin/end/capacity pointers to NULL.
 */
static void
CSdbStrVector_DestroyDealloc(CScriptStringDB *this)
{
	Destroy_Range16((StdAllocator *)this, this->first, this->last);
	free(this->first);
	this->first = NULL;
	this->last = NULL;
	this->end = NULL;
}

/*
 * 0x00401A00 - std::vector<CSdbStr>::operator[] (element access)
 *
 * Returns a pointer to the index-th CSdbStr, computed as begin + index * 16.
 */
void *
CSdbStrVector_At(CScriptStringDB *this, uint32_t index)
{
	uintptr_t bucket = CSearchCtx_GetBucket((CSearchCtx *)this);
	return (char *)bucket + index * sizeof(CSdbStr);
}

/*
 * 0x00401A20 - std::vector<CSdbStr>::insert
 *
 * Reads the current size and forwards to CSdbStrVector_InsertN to append value.
 */
void *
CSdbStrVector_Insert(CScriptStringDB *this, CSdbStr *value)
{
	uintptr_t count = (uintptr_t)((CVector *)this)->end;
	return (void *)CSdbStrVector_InsertN(this, (uint32_t)count, value);
}

/*
 * 0x00401A50 - std::vector<CSdbStr>::clear
 *
 * Erases the full [begin, end) range from the vector.
 */
static void
CSdbStrVector_Clear(CScriptStringDB *this)
{
	uintptr_t count = (uintptr_t)StdList_GetSize((StdPtrList *)this);
	int bucket = CSearchCtx_GetBucket((CSearchCtx *)this);
	CSdbStrVector_EraseRange(this, bucket, (uint32_t)count);
}

/*
 * 0x00401A80 - CSdbStr vector element constructor
 *
 * Initializes a basic_fstream: optionally constructs the ios base, sets up
 * the filebuf, stamps the vbptr vtable, then opens (filename, openMode|2).
 * Sets failbit on the ios when the open fails.
 */
static __attribute__((unused)) void *
CSdbStr_VectorCtor(CBasicFstream *this, void *filename, int openMode, int constructBase)
{
	CBasicFstream *fs = this;

	if (constructBase != 0) {
		fs->vtable = (uintptr_t *)g_vt_BasicFstream;
		Tree_NodeConstructor(&fs->ios);
	}

	CString_ConstructorWithInit(fs, &fs->filebuf, 0, 1, 0);
	CStdioFile_ConstructorAlloc(&fs->filebuf, 0);

	uintptr_t *vt = (uintptr_t *)fs->vtable;
	uintptr_t offset = vt[FSTREAM_VBT_VBASE];
	*(uintptr_t *)((char *)fs + offset) = (uintptr_t)g_vt_BasicFstream_Vbptr;

	void *result = CStdioFile_Open(&fs->filebuf, filename, (void *)(intptr_t)(openMode | 2));
	if (result == NULL) {
		uintptr_t *vt2 = (uintptr_t *)fs->vtable;
		CIosBase *base = (CIosBase *)((char *)fs + vt2[FSTREAM_VBT_VBASE]);
		IosBase_SetState(base, 2, 0);
	}

	return this;
}

/*
 * 0x00401B60 - Stream sub-object destructor
 *
 * Destroys the basic_fstream's filebuf and rewires the vbptr vtable for the
 * surrounding stream sub-object.
 */
static void
Stream_SubDestructor(CIosBase *this)
{
	CBasicFstream *fs = (CBasicFstream *)((char *)this - offsetof(CBasicFstream, ios));
	uintptr_t *vt = (uintptr_t *)fs->vtable;
	uintptr_t offset = vt[FSTREAM_VBT_VBASE];
	*(uintptr_t *)((char *)fs + offset) = (uintptr_t)g_vt_BasicFstream_Vbptr;

	CSdbStr_VectorDtor(&fs->filebuf);
	Stream_SetVbptr(&fs->filebuf);
}

/*
 * 0x00401BD0 - Set vbptr vtable entry for stream sub-object
 *
 * Stamps the istream vtable into the virtual base entry of the enclosing
 * basic_fstream.
 */
static void
Stream_SetVbptr(CStdioFileBuf *this)
{
	CBasicFstream *fs = (CBasicFstream *)((char *)this - offsetof(CBasicFstream, filebuf));
	uintptr_t *vt = (uintptr_t *)fs->vtable;
	uintptr_t offset = vt[FSTREAM_VBT_VBASE];
	uintptr_t *target = (uintptr_t *)((char *)fs + offset);
	*target = (uintptr_t)g_vt_IStream;
}

/*
 * 0x00401BF0 - Close file and set error state if needed
 *
 * Closes the underlying filebuf; sets failbit on the ios when the close
 * reports an error.
 */
static __attribute__((unused)) void
Stream_CloseFile(CBasicFstream *this)
{
	uintptr_t *p = (uintptr_t *)this;
	void *result = CStdioFile_Close(&this->filebuf);
	if (result == NULL) {
		uintptr_t *vt = (uintptr_t *)p[0];
		CIosBase *base = (CIosBase *)((char *)this + vt[FSTREAM_VBT_VBASE]);
		IosBase_SetState(base, 2, 0);
	}
}

/*
 * 0x00401C20 - CSdbStr vector element destructor
 *
 * basic_filebuf destructor: closes the file when open, releases the buffer,
 * destroys the locale facet, and runs the tree-node base destructor.
 */
static void
CSdbStr_VectorDtor(CStdioFileBuf *this)
{
	this->base.vtable = (void **)g_vt_BasicFilebuf;

	if (this->openFlag != 0)
		CStdioFile_Close(this);

	if (this->allocPtr != NULL)
		String_ScalarDestructor2((CSdbStr *)this->allocPtr, 1);

	CStdioFile_ScalarDelete((CLocaleFacet *)&this->localeFacet);
	Tree_NodeDestructor(&this->base);
}

/*
 * 0x00401CC0 - CStdioFile::ScalarDelete
 *
 * Drops one reference on the embedded locale facet and, when that brings the
 * count to zero, runs the facet's scalar deleting destructor.
 */
static void
CStdioFile_ScalarDelete(CLocaleFacet *this)
{
	uintptr_t *p = (uintptr_t *)this;
	if (p[0] == 0)
		return;

	void *result = Locale_Facet_Decref((void *)p[0]);
	if (result != NULL) {
		uintptr_t *obj = (uintptr_t *)result;
		uintptr_t *vt = (uintptr_t *)obj[0];
		typedef void *(*ScalarDestructorFn)(void *, int);
		((ScalarDestructorFn)vt[LOCALE_VT_SCALAR_DTOR])(result, 1);
	}
}

/*
 * 0x00401D10 - locale::facet::_Decref (refcount decrement)
 *
 * Decrements the facet's refcount and returns this when the count drops to
 * zero (caller should delete), else NULL.
 */
static void *
Locale_Facet_Decref(CLocaleFacet *this)
{
	CLocaleFacet *f = this;
	// fcn.005c0310 / fcn.005c03d0 are critical section enter/leave - no-op
	if (f->refs > 0 && f->refs < 0xFFFFFFFF)
		f->refs -= 1;
	// neg/sbb/not/and: returns this if refs == 0 (needs deletion), else NULL
	if (f->refs == 0)
		return this;
	return NULL;
}

/*
 * 0x00401D70 - std::_Tree node destructor (set vtable, call ScalarDelete)
 *
 * Stamps the streambuf vtable and releases the embedded locale facet.
 */
static void
Tree_NodeDestructor(CStreamBuf *this)
{
	this->vtable = (void **)g_vt_CStreamBuf;
	CStdioFile_ScalarDelete((CLocaleFacet *)&this->_Ploc);
}

/*
 * 0x00401E20 - char_traits<char>::to_int_type
 *
 * Returns the byte pointed to by arg zero-extended to unsigned int.
 */
unsigned int
char_traits_to_int_type(const char *p)
{
	return (unsigned char)*p;
}

/*
 * 0x00401F30 - char_traits<char>::copy (memmove wrapper)
 *
 * Wraps memmove(dest, src, count).
 */
static void *
CharTraits_Copy(void *dest, void *src, uint32_t count)
{
	return memmove(dest, src, count);
}

/*
 * 0x00401F50 - char_traits<char>::deref
 *
 * Returns the byte pointed to by arg.
 */
char
char_traits_deref(const char *p)
{
	return *p;
}

/*
 * 0x004022C0 - StdList::DerefOrNil
 *
 * Returns *valPtr unless it equals the nil sentinel from StdNilRef. In the
 * sentinel case the original branchless idiom collapses to 0 because
 * StdNilRef returns -1.
 */
static __attribute__((unused)) uint32_t
StdList_DerefOrNil(uint32_t *valPtr)
{
	uint32_t nilVal;
	uint32_t val;

	nilVal = (uint32_t)StdNilRef();
	val = *valPtr;
	if (val != nilVal)
		return val;
	// neg/sbb/inc: returns 1 if nilVal==0, else 0
	nilVal = (uint32_t)StdNilRef();
	if (nilVal == 0)
		return 1;
	return 0;
}

/*
 * 0x004022F0 - char_traits<char>::find (fputc stream variant)
 *
 * Writes the sign-extended char to the FILE; returns 1 on success, 0 on EOF.
 */
static __attribute__((unused)) unsigned char
CharTraits_Find(char val, int count)
{
	int result = fputc((int)(signed char)val, (FILE *)(intptr_t)count);
	return result != -1;
}

/*
 * 0x00402490 - char_traits<char>::find (unsigned byte fputc variant)
 *
 * Masks the value to a byte and writes it to the FILE; returns 1 on success,
 * 0 on EOF.
 */
static __attribute__((unused)) unsigned char
CharTraits_FindUB(int val, int count)
{
	int masked = val & 0xFF;
	int result = fputc(masked, (FILE *)(intptr_t)count);
	return result != -1;
}

/*
 * 0x004026C0 - fgetc wrapper with EOF check
 *
 * Reads one character; stores it in *dest and returns 1, or returns 0 on EOF.
 */
static __attribute__((unused)) unsigned char
Fgetc_Wrapper(char *dest, void *file)
{
	int ch = fgetc((FILE *)file);
	if (ch == -1)
		return 0;
	*dest = (char)ch;
	return 1;
}

/*
 * 0x004029C0 - Stream destructor (set vbptr vtable, call init)
 *
 * Restores the istream vbptr vtable on the filebuf and reinitializes the
 * basic_ios.
 */
static __attribute__((unused)) void
Stream_Destructor(CBasicFstream *this)
{
	Stream_SetVbptr(&this->filebuf);
	BasicIos_Init((CIosBase *)&this->filebuf);
}

/*
 * 0x00402A20 - basic_string<char>::scalar deleting destructor
 *
 * Runs the string destructor and frees the object when flags&1 is set.
 */
static void *
String_ScalarDestructor2(CSdbStr *this, int flags)
{
	String_Tidy(this);
	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x00402A50 - basic_string<char>::append (n copies of char)
 *
 * Appends count copies of ch to the string, growing storage as needed.
 */
static __attribute__((unused)) void *
String_AppendN(CSdbStr *this, uint32_t count, char ch)
{
	CSdbStr *s = this;
	uint32_t maxsize = 0x3ffffff; // [0x005ee084]
	if (maxsize - s->length <= count) {
		// fcn.005c03f0 throws length_error - we just return
		return this;
	}
	if (count > 0) {
		uint32_t newlen = s->length + count;
		if (String_Grow(this, newlen, 0)) {
			CharTraits_Assign((uintptr_t)s->data + s->length, count, &ch);
			String_Eos(this, newlen);
		}
	}
	return this;
}

/*
 * 0x00402AD0 - char_traits<char>::assign (fill n bytes)
 *
 * Fills dest with count copies of the byte at *src.
 */
static void *
CharTraits_Assign(uintptr_t dest, uint32_t count, void *src)
{
	char ch = *(char *)src;
	return memset((void *)dest, (int)(signed char)ch, count);
}

/*
 * 0x00402AF0 - basic_string<char>::assign from c_str
 *
 * Assigns from a NUL-terminated source by computing its length and
 * forwarding to String_AssignPtrLen.
 */
static void
String_AssignCStr(CSdbStr *this, void *src)
{
	uint32_t len = Strlen_Wrapper(src);
	String_AssignPtrLen(this, src, len);
}

/*
 * 0x00402B20 - strlen wrapper
 *
 * Returns strlen of the input string.
 */
static uint32_t
Strlen_Wrapper(void *str)
{
	return strlen((const char *)str);
}

/*
 * 0x00402B40 - basic_string<char>::erase(offset, count)
 *
 * Erases count bytes at offset by moving the tail forward and updating the
 * length. The original asserts when offset > length; we skip the assert
 * and trust the caller.
 */
static void *
CString_Erase(CSdbStr *this, unsigned int offset, unsigned int count)
{
	CSdbStr *cs = this;
	char **pData = &cs->data;
	unsigned int *pLen = (unsigned int *)&cs->length;
	unsigned int newLen;

	// Binary calls fcn.005C0890 if offset > length - we skip the assert

	String_Freeze(this);

	if (*pLen - offset < count)
		count = *pLen - offset;

	if (count > 0 && *pData != NULL) {
		Memmove_Wrapper(*pData + offset, *pData + offset + count, *pLen - offset - count);

		newLen = *pLen - count;
		if (String_Grow(this, newLen, 0)) {
			String_Eos(this, newLen);
		}
	}

	return this;
}

/*
 * 0x00402BF0 - memmove wrapper (3 args)
 *
 * Thin wrapper around memmove(dest, src, count).
 */
static void *
Memmove_Wrapper(void *dest, void *src, uint32_t count)
{
	return memmove(dest, src, count);
}

/*
 * 0x00402C10 - basic_string<char>::begin
 *
 * Locks the buffer and returns its data pointer.
 */
static __attribute__((unused)) char *
String_Begin(CSdbStr *this)
{
	CString_LockBuffer(this);
	return this->data;
}

/*
 * 0x00402C30 - basic_string<char>::end
 *
 * Locks the buffer and returns data + length, using PtrAdd_NullCheck for
 * the addition.
 */
static __attribute__((unused)) char *
String_End(CSdbStr *this)
{
	CString_LockBuffer(this);
	return (char *)PtrAdd_NullCheck((uintptr_t)this->data, (uintptr_t)this->length);
}

/*
 * 0x00402C60 - basic_string<char>::_Nullstr
 *
 * Returns a pointer to the static empty string buffer.
 */
char *
String_Nullstr(void)
{
	return g_NullStr;
}

/*
 * 0x00402C70 - CString::Release
 *
 * Releases the string's storage. With doFree clear or data==NULL, just zeros
 * the fields. Otherwise the refcount byte at data[-1] decides: 0 or 0xFF
 * (exclusive/locked) frees the buffer; any other value just decrements the
 * shared refcount. Always clears data/length/capacity at the end.
 */
static void
CString_Release_Stl(CSdbStr *this, int doFree)
{
	CSdbStr *cs = this;
	char **pData = &cs->data;
	unsigned int *pLen = (unsigned int *)&cs->length;
	unsigned int *pCap = (unsigned int *)&cs->capacity;
	char *refPtr;
	unsigned char refcnt;

	if (!(doFree & 0xFF) || *pData == NULL)
		goto done;

	refPtr = CString_Refcnt(*pData);
	refcnt = (unsigned char)*refPtr;

	if (refcnt == 0) {
		free(*pData - 1);
	} else {
		refPtr = CString_Refcnt(*pData);
		refcnt = (unsigned char)*refPtr;
		if (refcnt == 0xFF) {
			free(*pData - 1);
		} else {
			refPtr = CString_Refcnt(*pData);
			*refPtr = (char)((unsigned char)*refPtr - 1);
		}
	}

done:
	*pData = NULL;
	*pLen = 0;
	*pCap = 0;
}

/*
 * 0x00402D30 - CString::CString (with init)
 *
 * basic_string-with-init constructor. When constructBase is set, builds the
 * full base sub-object via Tree_NodeConstructor; always stamps the istream
 * vbptr vtable; when hasData is set, runs CStreamBuf_Constructor to install
 * the initData byte into the virtual base string.
 */
static void *
CString_ConstructorWithInit(CBasicFstream *this, void *subObj, char initData, int hasData, int constructBase)
{
	uintptr_t *p = (uintptr_t *)this;
	USED(subObj);

	if (constructBase != 0) {
		p[0] = (uintptr_t)g_vt_CString;
		Tree_NodeConstructor((CIosBase *)&this->filebuf);
	}

	uintptr_t *vt = (uintptr_t *)p[0];
	uintptr_t offset = vt[FSTREAM_VBT_VBASE];
	*(uintptr_t *)((char *)this + offset) = (uintptr_t)g_vt_IStream;

	if ((hasData & 0xFF) != 0) {
		void *base = (char *)this + ((uintptr_t *)p[0])[FSTREAM_VBT_VBASE];
		CStreamBuf_Constructor(base, subObj, initData);
	}

	return this;
}

/*
 * 0x00402DE0 - std::ios_base::setstate (state + exception handling)
 *
 * Stores the new ios state, ORing in badbit when the streambuf is NULL.
 */
static __attribute__((unused)) void
IosBase_SetState2(CIosBase *this, uint32_t state, char excflag __attribute__((unused)))
{
	CIosBase *ios = this;
	uint32_t finalstate;
	if (ios->streambuf == NULL)
		finalstate = state | 4;
	else
		finalstate = state;
	// fcn.005bfc50 stores finalstate & 7 into state field; exception path omitted
	ios->state = finalstate & 7;
}

/*
 * 0x00402E20 - std::_Tree node base constructor
 *
 * Initializes the stream base and stamps the basic_ios vtable.
 */
static void *
Tree_NodeConstructor(CIosBase *this)
{
	Stream_BaseConstructor(this);
	this->vtable = (void **)g_vt_IosBase;
	return this;
}

/*
 * 0x00402E40 - Stream base constructor (init facet + set vtable)
 *
 * Initializes the embedded locale facet with refs=0 and stamps the
 * stream-base vtable.
 */
static void *
Stream_BaseConstructor(CIosBase *this)
{
	locale_facet_Constructor(&this->locale, 0);
	this->locale.refs = 0;
	this->vtable = (void **)g_vt_StreamBase;
	return this;
}

/*
 * 0x00402E70 - locale::facet::facet
 *
 * Base locale-facet constructor; the refs argument is unused.
 */
void *
locale_facet_Constructor(void *facet, int refs)
{
	USED(refs);
	return facet;
}

/*
 * 0x00402EB0 - Iterator constructor from stream reference
 *
 * Captures the stream's good-bit and a pointer back to the stream into
 * the iterator.
 */
static void *
Iterator_Constructor(char *this, void *stream)
{
	char *p = this;
	unsigned char goodbit = Stream_GoodCheck(stream);
	p[0] = goodbit;
	*(void **)(p + sizeof(void *)) = stream;
	return this;
}

/*
 * 0x00402EE0 - Iterator destructor
 *
 * Outside an active exception (the only path the binary takes), advances
 * the iterator's stream via Iterator_CheckAdvance.
 */
static void
Iterator_Destructor(char *this)
{
	// Binary calls fcn.005c0bf0 (std::uncaught_exception) here; the
	// inlined body is `xor al,al; ret`, so it always reports false.
	unsigned char uncaught = 0;
	if (!uncaught) {
		uintptr_t *p = (uintptr_t *)this;
		Iterator_CheckAdvance((CBasicFstream *)p[1]);
	}
}

/*
 * 0x004032A0 - std::vector<CSdbStr>::_Insert_n (single element)
 *
 * Inserts one CSdbStr at the given position by translating it through
 * CVector16_Insert and re-deriving the position from the post-insert begin
 * pointer.
 */
static void *
CSdbStrVector_InsertN(CScriptStringDB *this, uintptr_t position, CSdbStr *value)
{
	uintptr_t bucket = CSearchCtx_GetBucket((CSearchCtx *)this);
	uintptr_t index = (position - bucket) / sizeof(CSdbStr);
	CVector16_Insert((CVector *)this, (void *)position, 1, value);
	bucket = CSearchCtx_GetBucket((CSearchCtx *)this);
	return (void *)(bucket + index * sizeof(CSdbStr));
}

/*
 * 0x004032F0 - std::vector<CSdbStr>::erase range
 *
 * Erases a range of CSdbStr elements: shifts the tail down via
 * Construct_RangeCopy16, destroys the leftover tail, and updates end.
 */
static void *
CSdbStrVector_EraseRange(CScriptStringDB *this, int begin, uint32_t count)
{
	uintptr_t *p = (uintptr_t *)this;
	uintptr_t end_count = (uintptr_t)((CVector *)this)->end;
	uintptr_t *eraseEnd = (uintptr_t *)Construct_RangeCopy16((void *)(uintptr_t)count, (void *)(uintptr_t)end_count, (void *)(uintptr_t)begin);
	Destroy_Range16((StdAllocator *)this, eraseEnd, ((CVector *)this)->end);
	p[2] = (uintptr_t)eraseEnd;
	return (void *)(uintptr_t)begin;
}

/*
 * 0x00403370 - CStdioFile ctor with allocator
 *
 * Initializes the streambuf base, allocates the locale facet, stamps the
 * basic_filebuf vtable, then attaches filePtr via CStdioFile_Setptr.
 */
static void *
CStdioFile_ConstructorAlloc(CStdioFileBuf *this, void *filePtr)
{
	CStdioFileBuf *fb = this;

	StreamBuf_BaseConstructor(&this->base);

	Locale_FacetConstructorAlloc((CLocaleFacet *)&fb->localeFacet);

	fb->base.vtable = (void **)g_vt_BasicFilebuf;

	CStdioFile_Setptr(this, (uintptr_t)filePtr, 0);

	return this;
}

/*
 * 0x004033E0 - locale::facet allocator ctor
 *
 * MSVC CRT allocates the global locale facet and incref's it; on Linux the
 * locale infrastructure is unused, so the embedded pointer is just zeroed.
 */
static void *
Locale_FacetConstructorAlloc(CLocaleFacet *this)
{
	// Binary calls fcn.005c0d00 (locale allocator) and stores the resulting
	// global facet pointer here, then bumps its refcount inside a critical
	// section (fcn.005c0310 / fcn.005c03d0). We don't replicate the locale
	// infrastructure, so the slot is zeroed and the incref is skipped.
	uintptr_t *p = (uintptr_t *)this;
	p[0] = 0;
	return this;
}

/*
 * 0x00403450 - locale::facet::_Incref (refcount increment)
 *
 * Increments the facet refcount, capped at 0xFFFFFFFF.
 */
static void
Locale_Facet_Incref(CLocaleFacet *this)
{
	// Binary wraps the body in fcn.005c0310 / fcn.005c03d0 (critical
	// section enter/leave); we don't replicate the lock.
	CLocaleFacet *f = this;
	if (f->refs < 0xFFFFFFFF)
		f->refs += 1;
}

/*
 * 0x00403490 - CStdioFile::open
 *
 * Refuses to reopen an already-open filebuf. Otherwise opens (filename, mode),
 * attaches the FILE* via CStdioFile_Setptr, then runs the post-open setup
 * via Stream_PostOpenLocaleSetup.
 */
static void *
CStdioFile_Open(CStdioFileBuf *this, void *filename, void *mode)
{
	CStdioFileBuf *fb = this;

	if (fb->fileBuf != NULL)
		return NULL;

	// Binary calls fcn.005c1230 (locale facet file open); in our build
	// we just use fopen directly.
	void *result = fopen((const char *)filename, (const char *)mode);
	if (result == NULL)
		return NULL;

	CStdioFile_Setptr(this, (uintptr_t)result, 1);
	Stream_PostOpenLocaleSetup(this);
	return this;
}

/*
 * 0x004034E0 - CStdioFile::close (fclose wrapper)
 *
 * Closes the embedded FILE* and clears the filebuf state via
 * CStdioFile_Setptr. Returns NULL on a fclose error or when no file was open.
 */
static void *
CStdioFile_Close(CStdioFileBuf *this)
{
	CStdioFileBuf *fb = this;
	if (fb->fileBuf == NULL)
		return NULL;
	int result = fclose_ServerSide((FILE *)fb->fileBuf);
	if (result != 0)
		return NULL;
	CStdioFile_Setptr(this, 0, 2);
	return this;
}

/*
 * 0x00403520 - Virtual dispatch wrapper (vtable offset 0x10)
 *
 * Forwards seven args to vtable[4].
 */
static __attribute__((unused)) void *
VDispatch_10(uintptr_t *this, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
	typedef void *(*VTFn)(uintptr_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
	uintptr_t *vt = (uintptr_t *)this[0];
	return ((VTFn)vt[4])(this, a1, a2, a3, a4, a5, a6, a7);
}

/*
 * 0x00403560 - Virtual dispatch wrapper (vtable offset 0x14)
 *
 * Forwards seven args to vtable[5].
 */
static __attribute__((unused)) void *
VDispatch_14(uintptr_t *this, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
	typedef void *(*VTFn)(uintptr_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
	uintptr_t *vt = (uintptr_t *)this[0];
	return ((VTFn)vt[5])(this, a1, a2, a3, a4, a5, a6, a7);
}

/*
 * 0x004035A0 - basic_string<char>::assign (from ptr + length)
 *
 * Grows the buffer, copies len bytes from src, then sets the new length.
 */
static void *
String_AssignPtrLen(CSdbStr *this, void *src, uint32_t len)
{
	CSdbStr *cs = this;
	if (String_Grow(this, len, 1)) {
		CharTraits_Copy(cs->data, src, len);
		String_Eos(this, len);
	}
	return this;
}

/*
 * 0x004035F0 - basic_string<char>::_Eos (set length + null terminator)
 *
 * Records the new length and writes a NUL terminator at data[length].
 */
static void
String_Eos(CSdbStr *this, uint32_t len)
{
	CSdbStr *cs = this;
	char nul = 0;
	cs->length = len;
	char_traits_assign(cs->data + len, &nul);
}

/*
 * 0x00403630 - char_traits<char>::assign
 *
 * Copies one byte from src to dest.
 */
void
char_traits_assign(char *dest, const char *src)
{
	*dest = *src;
}

/*
 * 0x00403640 - CString::LockBuffer
 *
 * Locks the basic_string buffer to suppress copy-on-write sharing. When the
 * refcount byte at data[-1] is non-zero and not already 0xFF, triggers a
 * copy-on-write via String_Grow; afterwards stamps the refcount to 0xFF
 * to mark exclusive ownership.
 */
static void
CString_LockBuffer(CSdbStr *this)
{
	CSdbStr *cs = this;
	char **pData = &cs->data;
	unsigned int *pLen = (unsigned int *)&cs->length;
	char *refPtr;
	unsigned char refcnt;

	if (*pData != NULL) {
		refPtr = CString_Refcnt(*pData);
		refcnt = (unsigned char)*refPtr;
		if (refcnt != 0) {
			refPtr = CString_Refcnt(*pData);
			refcnt = (unsigned char)*refPtr;
			if (refcnt != 0xFF) {
				String_Grow(this, *pLen, 0);
			}
		}
	}

	if (*pData != NULL) {
		refPtr = CString_Refcnt(*pData);
		*refPtr = (char)0xFF;
	}
}

/*
 * 0x004036C0 - std::basic_string::_Grow
 *
 * Ensures the string has room for newcap characters, handling the
 * ref-counted shared-buffer and small-buffer-optimization cases. Returns 1
 * when the buffer was grown or was already large enough, 0 on failure.
 */
static int
String_Grow(CSdbStr *this, uint32_t newcap, int flag)
{
	CSdbStr *cs = this;
	uint32_t max_cap = String_GrowCap(this);
	if (max_cap < newcap) {
		// fcn.005c03f0 throws length_error - no-op in our build
	}

	if (cs->data != NULL) {
		char *refp = CString_Refcnt(cs->data);
		unsigned int rc = (unsigned char)*refp;
		if (rc != 0) {
			refp = CString_Refcnt(cs->data);
			rc = (unsigned char)*refp;
			if (rc != 0xff) {
				if (newcap == 0) {
					char *rp = CString_Refcnt(cs->data);
					*rp = (char)((unsigned char)*rp - 1);
					CString_Release_Stl(this, 0);
					return 0;
				}
				CString_Reserve(this, newcap);
				return 1;
			}
		}
	}

	if (newcap == 0) {
		if (flag & 0xff) {
			CString_Release_Stl(this, 1);
		} else {
			if (cs->data != NULL)
				String_Eos(this, 0);
		}
		return 0;
	}

	if (flag & 0xff) {
		if ((unsigned int)cs->capacity > 0x1f || (unsigned int)cs->capacity < newcap) {
			CString_Release_Stl(this, 1);
			CString_Reserve(this, newcap);
			return 1;
		}
	}

	if (flag & 0xff)
		return 1;
	if ((unsigned int)cs->capacity >= newcap)
		return 1;
	CString_Reserve(this, newcap);
	return 1;
}

/*
 * 0x00403800 - Pointer add with NULL check
 *
 * Returns base + offset, except when base is NULL, in which case it returns 0.
 */
static uintptr_t
PtrAdd_NullCheck(uintptr_t base, uintptr_t offset)
{
	if (base == 0)
		return 0;
	return base + offset;
}

/*
 * 0x00403830 - basic_string<char>::_Refcnt
 *
 * Returns a pointer to the reference-count byte that sits one byte
 * before the string data buffer (data - 1).
 */
char *
CString_Refcnt(char *data)
{
	return data - 1;
}

/*
 * 0x00403850 - basic_string<char>::_Freeze (unique copy)
 *
 * If the buffer is shared (refcount > 0 and < 0xFF), releases the shared
 * reference and reassigns from a captured copy of the data pointer to give
 * the string an exclusive buffer.
 */
static void
String_Freeze(CSdbStr *this)
{
	CSdbStr *cs = this;
	if (cs->data == NULL)
		return;
	char *refp = CString_Refcnt(cs->data);
	unsigned char ref = (unsigned char)*refp;
	if (ref == 0)
		return;
	refp = CString_Refcnt(cs->data);
	ref = (unsigned char)*refp;
	if (ref == 0xFF)
		return;
	char *saved = cs->data;
	CString_Release_Stl(this, 1);
	String_AssignCStr(this, saved);
}

/*
 * 0x004038C0 - Stream good-bit check (via iterator state)
 *
 * Returns the stream's open state. When the stream is open and the event
 * ring has pending entries, runs IStream_Ipfx to advance past them first.
 */
static unsigned char
Stream_GoodCheck(CBasicFstream *this)
{
	uintptr_t *p = (uintptr_t *)this;
	uintptr_t *vt = (uintptr_t *)p[0];
	void *sub = (char *)this + vt[FSTREAM_VBT_VBASE];
	unsigned char isOpen = Stream_OpenCheck(sub);
	if (isOpen) {
		uint32_t count = EventRingBuffer_Count();
		if (count != 0) {
			uint32_t count2 = EventRingBuffer_Count();
			IStream_Ipfx((void *)(uintptr_t)count2);
		}
	}
	sub = (char *)this + ((uintptr_t *)p[0])[FSTREAM_VBT_VBASE];
	return Stream_OpenCheck(sub);
}

/*
 * 0x00403930 - Stream open check (non-NULL bucket)
 *
 * Returns 1 when the search context's bucket is non-zero.
 */
static unsigned char
Stream_OpenCheck(CIosBase *this)
{
	int val = CSearchCtx_GetBucket((CSearchCtx *)this);
	return val != 0;
}

/*
 * 0x00403950 - Iterator check and advance
 *
 * Advances the iterator's stream via IStream_Ipfx when the val-node has
 * bit 2 set.
 */
static void
Iterator_CheckAdvance(CBasicFstream *this)
{
	uintptr_t *p = (uintptr_t *)this;
	uintptr_t *vt = (uintptr_t *)p[0];
	void *sub = (char *)this + vt[FSTREAM_VBT_VBASE];
	int val = CSearchCtx_GetValNode(sub);
	if (val & 2)
		IStream_Ipfx(this);
}

/*
 * 0x00403A10 - Streambuf base ctor
 *
 * Initializes the embedded locale facet, stamps the streambuf vtable, and
 * resets the streambuf pointer fields.
 */
static void
StreamBuf_BaseConstructor(CStreamBuf *this)
{
	CStreamBuf *sb = this;

	Locale_FacetConstructorAlloc((CLocaleFacet *)&sb->_Ploc);

	sb->vtable = (void **)g_vt_CStreamBuf;

	CStreamBuf_InitPtrs(sb);
}

/*
 * 0x00403CA0 - Destroy single 16-byte element via scalar delete
 *
 * Runs the scalar deleting destructor with flags=0 on the element.
 */
void
Destroy_Single16(StdAllocator *this, void *elem)
{
	USED(this);
	ScalarDestructor_Flags0(elem);
}

/*
 * 0x00403CC0 - CStdioFile::_Setptr
 *
 * Attaches a FILE* to the filebuf. When openFlag==0 the locale facet is
 * torn down and rebuilt and the alloc pointer is cleared. The streambuf
 * pointers are reset, then re-pointed at the FILE*'s internal buffer when
 * one was supplied. The position fields are reset to the stdio default.
 */
static void
CStdioFile_Setptr(CStdioFileBuf *this, uintptr_t filePtr, uint32_t openFlag)
{
	CStdioFileBuf *fb = this;

	fb->openFlag = (openFlag == 1) ? 1 : 0;

	if (openFlag == 0) {
		CStdioFile_ScalarDelete((CLocaleFacet *)&fb->localeFacet);
		uintptr_t facet = StdKfn_Identity(4, (uintptr_t)&fb->localeFacet);
		if (facet != 0) {
			Locale_FacetConstructorAlloc((CLocaleFacet *)facet);
		}
		fb->allocPtr = NULL;
	}

	CStreamBuf_InitPtrs(&fb->base);

	if (filePtr != 0) {
		if (fb->openFlag == 0) {
			uintptr_t buf = filePtr;
			CStreamBuf_SetAll(&fb->base, buf + 8, buf, buf + 4, buf + 8, buf, buf + 4);
		}
	}

	fb->fileBuf = (void *)filePtr;
	fb->posField2 = g_StdioStreamDefault;
	fb->posField1 = g_StdioStreamDefault;
	fb->facet = NULL;
}

/*
 * 0x00403DB0 - CScript::CompileExpression
 *
 * Wires up the MSVC STL locale facets that the wombat compiler needs after
 * a stream is opened: looks up a facet, copy/replace-assigns it into the
 * filebuf's slot, clears the pointer when the facet reports empty, and
 * allocates the auxiliary src object on first use.
 */
static void
Stream_PostOpenLocaleSetup(CStdioFileBuf *this)
{
	CStdioFileBuf *fb = this;
	uintptr_t local_var[2];

	void *handle = Locale_Facet_InitCompile(&this->base, (void *)local_var);

	// The 0 and 1 here are residual from InitCompile's pre-push sequence
	// (push 1, push 0); only `handle` is the explicit argument.
	fb->facet = locale_Getfacet(handle, 0, (void *)(uintptr_t)1);

	CStdioFile_ScalarDelete((CLocaleFacet *)local_var);

	// Binary builds a 2-slot stack temporary {&localeFacet, facet} and
	// passes ecx pointing to it; we materialize the same shape explicitly.
	void *facet38 = fb->facet;
	uintptr_t stackTemp[2];
	stackTemp[0] = (uintptr_t)&fb->localeFacet;
	stackTemp[1] = (uintptr_t)facet38;
	Locale_Facet_CopyAssign((uintptr_t *)stackTemp, (uintptr_t *)&fb->localeFacet);

	uintptr_t local_e8[2];
	void *useFacetResult = Locale_UseFacet(local_e8, &fb->localeFacet, facet38);

	Locale_Facet_ReplaceAssign((uintptr_t *)&fb->localeFacet, (uintptr_t *)useFacetResult);
	CStdioFile_ScalarDelete((CLocaleFacet *)local_e8);

	unsigned int vt1_result = VDispatch_VT1(fb->facet);
	if (vt1_result & 0xFF)
		fb->facet = NULL;

	if (fb->allocPtr == NULL) {
		void *newObj = (void *)OperatorNew(0x10);
		if (newObj != NULL) {
			uint32_t local_dc;
			newObj = Locale_Facet_InitSrc((CSdbStr *)newObj, &local_dc);
		}
		fb->allocPtr = newObj;
	}
}

/*
 * 0x00403ED0 - locale::facet copy-assign (copy pointer + incref)
 *
 * Copies the pointer from *src into *this and incref's the new target.
 */
static void *
Locale_Facet_CopyAssign(uintptr_t *this, uintptr_t *src)
{
	uintptr_t *p = this;
	uintptr_t *s = src;
	p[0] = s[0];
	Locale_Facet_Incref((void *)p[0]);
	return this;
}

/*
 * 0x00403F00 - locale::facet replace-assign (decref old, assign new, incref)
 *
 * When *this differs from *src, decrefs the old facet (running its scalar
 * deleting destructor when the count hits zero), then copies the new
 * pointer in and incref's it.
 */
static void *
Locale_Facet_ReplaceAssign(uintptr_t *this, uintptr_t *src)
{
	uintptr_t *p = this;
	uintptr_t *s = src;
	if (p[0] != s[0]) {
		void *old = Locale_Facet_Decref((void *)p[0]);
		if (old != NULL) {
			uintptr_t *obj = (uintptr_t *)old;
			uintptr_t *vt = (uintptr_t *)obj[0];
			typedef void *(*ScalarDestructorFn)(void *, int);
			((ScalarDestructorFn)vt[0])(old, 1);
		}
		p[0] = s[0];
		Locale_Facet_Incref((void *)p[0]);
	}
	return this;
}

/*
 * 0x00403F70 - Virtual dispatch via vtable[1]
 *
 * Calls vtable[1] on this with no extra arguments.
 */
static unsigned int
VDispatch_VT1(uintptr_t *this)
{
	typedef unsigned int (*VTFn)(uintptr_t *);
	uintptr_t *vt = (uintptr_t *)this[0];
	return ((VTFn)vt[1])(this);
}

/*
 * 0x00403F90 - locale::facet::_Init from source (copy byte + release)
 *
 * Copies one byte from src into this and clears the embedded string fields
 * via CString_Release_Stl(this, 0).
 */
static void *
Locale_Facet_InitSrc(CSdbStr *this, void *src)
{
	*(char *)this = *(char *)src;
	CString_Release_Stl(this, 0);
	return this;
}

/*
 * 0x00403FC0 - basic_string<char>::_Grow (capacity check)
 *
 * Returns max_size - 2 when max_size > 2, else 1.
 */
static uint32_t
String_GrowCap(CSdbStr *this)
{
	uint32_t maxsz = String_MaxSize(this);
	if (maxsz > 2)
		return maxsz - 2;
	return 1;
}

/*
 * 0x00404000 - CString::Reserve
 *
 * Grows the string's buffer to at least requestedCapacity. The new size is
 * rounded up to a multiple of 32 unless the current max_size cap forces a
 * tighter allocation. Old data is copied into the new buffer, the old
 * buffer is released, and the refcount/capacity/length fields are reset.
 */
static void
CString_Reserve(CSdbStr *this, unsigned int requestedCapacity)
{
	CSdbStr *cs = this;
	char **pData = &cs->data;
	unsigned int *pLen = (unsigned int *)&cs->length;
	unsigned int *pCap = (unsigned int *)&cs->capacity;
	unsigned int newCap;
	char *newBuf;
	unsigned int oldLen;
	char *refPtr;

	newCap = requestedCapacity | 0x1F;

	if (String_GrowCap(this) < newCap)
		newCap = requestedCapacity;

	newBuf = (char *)Allocator_Allocate((StdAllocator *)this, newCap + 2);

	if (*pLen > 0)
		CharTraits_Copy(newBuf + 1, *pData, *pLen);

	oldLen = *pLen;

	CString_Release_Stl(this, 1);

	*pData = newBuf + 1;

	refPtr = CString_Refcnt(*pData);
	*refPtr = 0;

	*pCap = newCap;

	String_Eos(this, oldLen);
}

/*
 * 0x00404110 - locale::facet::_Init for CompileExpression
 *
 * Copy-assigns the streambuf's locale facet into *src and returns src.
 */
static void *
Locale_Facet_InitCompile(CStreamBuf *this, void *src)
{
	// Binary's argument order is swapped: ecx = src, then push this+0x34.
	// We copy from this->_Ploc into *src.
	Locale_Facet_CopyAssign((uintptr_t *)src, (uintptr_t *)&this->_Ploc);
	return src;
}

/*
 * 0x00404250 - std::_Construct_n (construct n elements from value)
 *
 * Copy-constructs count 16-byte elements at dest, each from value.
 */
void
ConstructN_16(StdAllocator *this, void *dest, uint32_t count, void *value)
{
	char *d = (char *)dest;
	while (count > 0) {
		Allocator_CopyConstruct16(this, d, value);
		count--;
		d += 16;
	}
}

/*
 * 0x004042B0 - Allocator::allocate (allocate n * 16 bytes)
 *
 * Allocates room for count 16-byte elements via OperatorNew_Clamp.
 */
static void *
Allocator_Allocate(StdAllocator *this, uint32_t count)
{
	USED(this);
	return OperatorNew_Clamp(count);
}

/*
 * 0x004042D0 - basic_string<char>::max_size
 *
 * Always returns 0xFFFFFFFF; the binary's >0 comparison degenerates.
 */
static uint32_t
String_MaxSize(CSdbStr *this)
{
	USED(this);
	return 0xFFFFFFFF;
}

/*
 * 0x00404380 - Copy-construct 16-byte element via allocator
 *
 * Forwards to Allocator_Constructor16.
 */
void
Allocator_CopyConstruct16(StdAllocator *this, void *dest, void *src)
{
	USED(this);
	Allocator_Constructor16(dest, src);
}

/*
 * 0x004043A0 - ScriptStringDB_Insert
 *
 * ORPHANED: only caller is CScriptStringDB_BuildIndex, which itself has zero
 * callers. The original walks an MSVC std::set's red-black tree to insert
 * a string; the STL internals are not replicated, so the body is empty.
 */
void *
ScriptStringDB_Insert(void *setObj, const char *str)
{
	USED(setObj);
	USED(str);
	return NULL;
}

/*
 * 0x00404630 - std::_Tree::_Size setter (swap)
 *
 * Replaces the tree's size with newSize and returns the previous value.
 */
static __attribute__((unused)) uint32_t
Tree_SetSize(CStlTreeFull *this, uint32_t newSize)
{
	CStlTreeFull *tree = this;
	uint32_t old = tree->size;
	tree->size = newSize;
	return old;
}

/*
 * 0x00404790 - Scalar deleting destructor wrapper (flags=0)
 *
 * Runs String_ScalarDestructor2 with flags=0 (destruct in place, do not free).
 */
static void
ScalarDestructor_Flags0(CSdbStr *obj)
{
	String_ScalarDestructor2(obj, 0);
}

/*
 * 0x004047A0 - locale::_Getfacet
 *
 * MSVC CRT locale facet lookup. The Linux build does not replicate the CRT
 * locale table, so the body returns NULL.
 */
static void *
locale_Getfacet(void *facets, int throwOnMissing, void *handle)
{
	USED(facets);
	USED(throwOnMissing);
	USED(handle);
	return NULL;
}

/*
 * 0x004048C0 - CStdioFile::CStdioFile constructor
 *
 * Stamps the CStdioFile vtable. The original delegated to a CFile base
 * constructor; that path is part of the MSVC CRT and is not replicated.
 */
static __attribute__((unused)) void *
CStdioFile_Constructor(CStdioFileBuf *this, int arg)
{
	// Binary calls CFile base ctor at 0x004E84F0 first (sets CFile vtable,
	// allocates name copy, sets refcount); we override it with the CStdioFile
	// vtable on the next line so the base-ctor effects are no-ops here.
	USED(arg);
	this->base.vtable = (void **)g_vt_CStdioFile;
	return this;
}

/*
 * 0x004048E0 - CStdioFile::CStdioFile constructor with arg
 *
 * This address is NOT a separate function - it falls mid-instruction in the
 * epilogue of CStdioFile_Constructor (0x004048C0). There are zero
 * cross-references to it. The real second CStdioFile constructor is at
 * 0x00404970, the MSVC MFC copy constructor.
 */
static __attribute__((unused)) void *
CStdioFile_ConstructorArg(CStdioFileBuf *this, void *arg)
{
	USED(arg);
	this->base.vtable = (void **)g_vt_CStdioFile;
	return this;
}

/*
 * 0x004048F0 - CStdioFile destructor body
 *
 * Stamps the CStdioFile vtable; the binary's CFile base destructor is a CRT
 * internal that does nothing in our build.
 */
static __attribute__((unused)) void
CStdioFile_DestructorBody(CStdioFileBuf *this)
{
	this->base.vtable = (void **)g_vt_CStdioFile;
}

/*
 * 0x00404970 - CStdioFile copy constructor body
 *
 * Stamps the CStdioFile vtable; the CFile base copy ctor is a CRT internal
 * we do not replicate.
 */
static __attribute__((unused)) void *
CStdioFile_CopyConstructorBody(CStdioFileBuf *this, CStdioFileBuf *src)
{
	USED(src);
	this->base.vtable = (void **)g_vt_CStdioFile;
	return this;
}

/*
 * 0x004049A0 - locale::id::_Id_cnt (get or allocate unique ID)
 *
 * Returns the locale id, allocating a new one from the global counter
 * when this->id is zero.
 */
static uint32_t
Locale_Id_GetOrAlloc(uint32_t *this)
{
	// Binary wraps the body in fcn.005c0310 / fcn.005c03d0 (critical
	// section enter/leave); we don't replicate the lock.
	uint32_t *p = this;
	static uint32_t s_idCounter = 49; // initial value at [0x9a3a80]
	if (p[0] == 0) {
		s_idCounter++;
		p[0] = s_idCounter;
	}
	return p[0];
}

/*
 * 0x004049F0 - locale use_facet wrapper
 *
 * Looks up a facet from the locale (stubbed to NULL on Linux) and copies it
 * into the destination via Locale_Facet_CopyAssign, then tears down the
 * temporary at arg2.
 */
static void *
Locale_UseFacet(void *arg1, void *arg2, void *arg3)
{
	void *threadType;
	uint32_t facetId;
	void *facet;

	threadType = (void *)(intptr_t)Locale_Facet_GetCat();
	facetId = Locale_Id_GetOrAlloc((uint32_t *)&g_locale_facet_id);

	// Binary calls fcn.005c1440 (std::use_facet) here; we don't replicate
	// MSVC's locale facet table, so the lookup yields NULL.
	facet = NULL;
	USED(threadType);
	USED(facetId);
	USED(arg3);

	Locale_Facet_CopyAssign(arg1, (uintptr_t *)&facet);
	CStdioFile_ScalarDelete((CLocaleFacet *)arg2);

	return arg1;
}

/*
 * 0x00404A80 - operator new wrapper (clamp negative to zero)
 *
 * Allocates size bytes via malloc, treating negative sizes as zero.
 */
static void *
OperatorNew_Clamp(int size)
{
	if (size < 0)
		size = 0;
	return malloc(size);
}

/*
 * 0x00404AA0 - Allocator::construct (copy-construct 16-byte element)
 *
 * Allocates the destination slot via StdKfn_Identity and copy-constructs
 * a basic_string from src into it.
 */
static void *
Allocator_Constructor16(void *dest, void *src)
{
	void *alloc = (void *)StdKfn_Identity(16, (uintptr_t)dest);
	if (alloc == NULL)
		return NULL;
	return String_CopyConstructor(alloc, src);
}

/*
 * 0x00404AE0 - basic_string<char> copy constructor from ref
 *
 * Releases this string's storage and replaces its contents with the full
 * range from src.
 */
static void *
String_CopyConstructor(CSdbStr *this, void *src)
{
	*(char *)this = *(char *)src;
	CString_Release_Stl(this, 0);
	String_Stl_Replace(this, src, 0, 0x3ffffff);
	return this;
}

/*
 * 0x00404B20 - basic_string<char>::_Replace
 *
 * Replaces this string's contents with count chars from src starting at
 * offset. Handles self-assignment via CString_Erase, takes a refcounted
 * fast-path when src has an exclusive buffer, and otherwise falls back to
 * grow + copy + Eos.
 */
static void *
String_Stl_Replace(CSdbStr *this, void *src, uint32_t offset, uint32_t count)
{
	uint32_t srcLen, n;
	CSdbStr *cs = this;
	CSdbStr *srcCs = (CSdbStr *)src;

	srcLen = srcCs->length;
	if (srcLen < offset) {
		abort();
	}

	srcLen = srcCs->length;
	n = srcLen - offset;

	if (count < n)
		n = count;

	if (this == src) {
		uint32_t maxsize = 0x3ffffff;
		CString_Erase(this, offset + n, maxsize);
		CString_Erase(this, 0, offset);
		return this;
	}

	if (n == 0 || (uint32_t)srcCs->length != n)
		goto grow_path;

	{
		char *refPtr = CString_Refcnt(String_CStr(src));
		unsigned int refcnt = (unsigned char)*refPtr;
		if (refcnt >= 0xfe)
			goto grow_path;
	}

	if (!(StdAllocator_Equal() & 0xFF))
		goto grow_path;

	CString_Release_Stl(this, 1);

	cs->data = String_CStr(src);
	cs->length = srcCs->length;
	cs->capacity = CSearchCtx_GetValNode((CSearchCtx *)src);

	{
		char *refPtr = CString_Refcnt(cs->data);
		*(unsigned char *)refPtr = (unsigned char)(*(unsigned char *)refPtr + 1);
	}
	return this;

grow_path:
	if (!(String_Grow(this, n, 1) & 0xFF))
		return this;

	CharTraits_Copy(cs->data, (void *)(String_CStr(src) + offset), n);
	String_Eos(this, n);

	return this;
}

/*
 * 0x00404C80 - basic_string<char> copy-assign (dispatch to _Copy)
 *
 * Forwards to String_Copy to assign the entire source string.
 */
void
String_CopyAssignDispatch(CSdbStr *this, void *src)
{
	String_Copy(this, src);
}

/*
 * 0x00404CC0 - Virtual dispatch via vtable[7] (offset 0x1C)
 *
 * Forwards two args to vtable[7].
 */
static __attribute__((unused)) void
VDispatch_1C(uintptr_t *this, uint32_t a1, uint32_t a2)
{
	typedef void (*VTFn)(uintptr_t *, uint32_t, uint32_t);
	uintptr_t *vt = (uintptr_t *)this[0];
	((VTFn)vt[7])(this, a1, a2);
}

/*
 * 0x00404CE0 - locale ctor with category
 *
 * Initializes the locale base, stamps the LocaleCategory vtable, then
 * installs the time fields. The original opened the "C" locale via the
 * MSVC CRT _Getcat helpers, which we stub out.
 */
static __attribute__((unused)) void *
Locale_ConstructorCategory(CLocaleCategory *this, void *arg)
{
	void *getcat_result;

	CLocaleCategory_ConstructorBody(this, arg);

	this->vtable = (uintptr_t *)g_vt_LocaleCategory;

	// Binary calls fcn.005c67a0 (_Getcat) on the literal "C" to construct a
	// stack-local locale state, then fcn.005c68e0 to tear it down after the
	// CopySystemTime call. We don't replicate _Getcat, so pass NULL.
	getcat_result = NULL;
	CopySystemTime(this, getcat_result);

	return this;
}

/*
 * 0x00404D70 - CScriptObj constructor body (set vtable)
 *
 * Runs the locale::facet base constructor and stamps the CScriptObj vtable.
 */
static void *
CLocaleCategory_ConstructorBody(CLocaleCategory *this, void *arg)
{
	Locale_Facet_Constructor((CLocaleFacet *)this, arg);
	this->vtable = (uintptr_t *)g_vt_LocaleCategoryBase;
	return this;
}

/*
 * 0x00404DA0 - locale::facet::facet (base constructor)
 *
 * Stores the initial refcount and stamps the std::locale::facet base
 * vtable (one null entry, one pure-virtual slot for ~facet). Called by
 * derived facet constructors (e.g. CLocaleCategory_ConstructorBody) before
 * they overwrite the vtable with their own.
 */
__attribute__((unused)) static CLocaleFacet *
Locale_Facet_Constructor(CLocaleFacet *this, void *arg)
{
	this->refs = (uint32_t)(uintptr_t)arg;
	this->vtable = (void **)g_vt_LocaleFacet;
	return this;
}

/*
 * 0x00404E00 - locale::facet::_Init
 *
 * Initializes a locale facet's vtable. The C port does not replicate
 * locale-facet vtables, so this is a no-op.
 */
void
locale_facet_Init(void *facet)
{
	uintptr_t *p = (uintptr_t *)facet;
	p[0] = (uintptr_t)g_vt_LocaleFacet;
}

/*
 * 0x00404E30 - CScript object scalar deleting destructor
 *
 * Runs the destructor body and frees the object when flags&1 is set.
 */
static __attribute__((unused)) void *
CLocaleCategory_ScalarDtor(CLocaleCategory *this, int flags)
{
	// Destructor: call CLocaleCategory_DestructorBody (0x00404E60)
	CLocaleCategory_DestructorBody(this);

	// If flags & 1, free the object
	if (flags & 1)
		OperatorDelete(this);

	return this;
}

/*
 * 0x00404E60 - CScriptObj destructor body (calls locale_facet_Init)
 *
 * Delegates to locale_facet_Init.
 */
static void
CLocaleCategory_DestructorBody(CLocaleCategory *this)
{
	locale_facet_Init(this);
}

/*
 * 0x00404F10 - CScriptObj vtable setup + destructor
 *
 * Stamps the LocaleCategory vtable then runs CLocaleCategory_DestructorBody.
 */
static __attribute__((unused)) void
CLocaleCategory_VtableDtor(CLocaleCategory *this)
{
	this->vtable = (uintptr_t *)g_vt_LocaleCategory;
	CLocaleCategory_DestructorBody(this);
}

/*
 * 0x00404F30 - locale::facet::_Getcat (returns locale category)
 *
 * Static virtual returning the locale category constant for this facet
 * class. Returns 2 (LC_CTYPE in MSVC's _CAT_* enum). Called from
 * Locale_UseFacet as the F::_Getcat() argument to the use_facet pattern.
 */
static int __attribute__((unused))
Locale_Facet_GetCat(void)
{
	return 2;
}

/*
 * 0x00404F40 - Locale_Facet_RegisterAtexit
 *
 * Caches a CLocaleFacet pointer in a global slot, bumps its refcount via
 * Locale_Facet_Incref, and registers an atexit destructor (0x00405030) that
 * decrefs and destroys the cached facet at process exit. Called by
 * locale_Getfacet (0x004047A0) the first time a facet is materialized.
 *
 * MODIFIED: binary wraps the body in a CCriticalSection enter/leave pair;
 * Linux is single-threaded and we do not replicate the MSVC locale facet
 * cache, so this is a no-op.
 */
static __attribute__((unused)) void
Locale_Facet_RegisterAtexit(void)
{
}

/*
 * 0x00404FB0 - basic_string<char>::_Copy (assign from source, 0, -1)
 *
 * Replaces the entire string with the contents of src.
 */
static void
String_Copy(CSdbStr *this, void *src)
{
	String_Stl_Replace(this, src, 0, 0xFFFFFFFF);
}

/*
 * 0x00404FD0 - Copy 8-byte time value from system clock
 *
 * Reads the current 64-bit system time via GetSystemTime8 and stores it
 * into the locale category's time fields.
 */
static void
CopySystemTime(CLocaleCategory *this, void *dest)
{
	CLocaleCategory *lc = this;
	uint32_t result[2];
	GetSystemTime8(dest, result);
	lc->time_lo = result[0];
	lc->time_hi = result[1];
}

/*
 * 0x00405000 - Get system time as 8-byte value
 *
 * Stores the current time as a 64-bit microseconds-since-epoch value at
 * *result. The original used MSVC CRT _ftime64; the Linux build uses
 * gettimeofday.
 */
static void *
GetSystemTime8(uintptr_t *this, void *result)
{
	struct timeval tv;
	uint32_t *r = (uint32_t *)result;
	USED(this);
	gettimeofday(&tv, NULL);
	uint64_t t = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
	r[0] = (uint32_t)t;
	r[1] = (uint32_t)(t >> 32);
	return result;
}

/*
 * 0x004050C0 - Static init guard (flag at 0x009A3BF9)
 *
 * One-time initialization guard: sets a static flag the first time it runs.
 */
static __attribute__((unused)) void
StaticInitGuard_1(void)
{
	static unsigned char s_initFlag1 = 0;
	if (!(s_initFlag1 & 1))
		s_initFlag1 |= 1;
}

/*
 * 0x004050F0 - Static init guard (flag at 0x009A3BF8)
 *
 * One-time initialization guard: sets a separate static flag on first run.
 */
static __attribute__((unused)) void
StaticInitGuard_2(void)
{
	static unsigned char s_initFlag2 = 0;
	if (!(s_initFlag2 & 1))
		s_initFlag2 |= 1;
}
