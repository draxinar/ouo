#ifndef STL_H_
#define STL_H_

#include <stdint.h>

#include "streambuf.h"

__extension__ typedef struct CEntityMap CEntityMap;
__extension__ typedef struct CList CList;
__extension__ typedef struct CString CString;
__extension__ typedef struct StdPtrList StdPtrList;
__extension__ typedef struct StdPtrNode StdPtrNode;
__extension__ typedef struct SurfaceInfo SurfaceInfo;
__extension__ typedef struct CLocation CLocation;

/*
 * MSVC std::locale::facet base class (8 bytes).
 */
__extension__ typedef struct CLocaleFacet CLocaleFacet;
struct CLocaleFacet {
	void **vtable; // +0x00
	uint32_t refs; // +0x04
};

/*
 * MSVC std::basic_ios<char> (52 bytes), including its std::ios_base base.
 * state carries the goodbit/badbit/failbit/eofbit mask.
 */
__extension__ typedef struct CIosBase CIosBase;
struct CIosBase {
	void **vtable;          // +0x00
	uint32_t state;         // +0x04
	uint32_t except;        // +0x08
	uint32_t flags;         // +0x0C (fmtflags)
	uint32_t precision;     // +0x10
	uint32_t width;         // +0x14
	uint32_t iosFields[2];  // +0x18
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;        // 64-bit alignment pad
#endif
	CLocaleFacet locale;    // +0x20
	void *streambuf;        // +0x28 (_Strbuf)
	void *tiestream;        // +0x2C (_Tiestr)
	char fill;              // +0x30 (_Fillch)
};

// A std::ostream manipulator: takes and returns the stream.
typedef void *(*OStreamManip)(void *);

// std::ios_base::fmtflags bits used by the stream inserters.
#define IOS_ADJUSTFIELD 0x1C0
#define IOS_LEFT        0x040

// std::ios_base::iostate bits.
#define IOS_BADBIT 0x4

// CBasicFstream vbtable slot indices (vbtable at 0x005EE040).
#define FSTREAM_VBT_SELF  0  // +0x00: offset to self
#define FSTREAM_VBT_VBASE 1  // +0x04: offset to virtual base (CIosBase)

// CLocaleFacet vtable slot index.
#define LOCALE_VT_SCALAR_DTOR 0  // +0x00: scalar deleting destructor

/*
 * MSVC std::basic_fstream<char> (140 bytes). Combines a filebuf
 * sub-object with the virtual basic_ios sub-object.
 */
__extension__ typedef struct CBasicFstream CBasicFstream;
struct CBasicFstream {
	uintptr_t *vtable;      // +0x00 (fstream vtable / vbptr)
	CStdioFileBuf filebuf;  // +0x04
	CIosBase ios;           // +0x58
};

/*
 * MSVC 5.0 std::_Tree with full allocator/comparator fields (24 bytes).
 * Backs the CScriptStringDB string set.
 */
__extension__ typedef struct CStlTreeFull CStlTreeFull;
struct CStlTreeFull {
	uint32_t allocField; // +0x00 (nop allocator)
	uint32_t kfnField;   // +0x04 (nop key-of-func)
	uint32_t predField;  // +0x08 (nop predicate)
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64; // 64-bit alignment pad
#endif
	void *head;     // +0x0C (sentinel node)
	uint32_t multi; // +0x10 (0=set, 1=multiset)
	uint32_t size;  // +0x14
};

/*
 * MSVC locale::_Locimp category (16 bytes): CLocaleFacet plus a 64-bit
 * timestamp. Constructor at 0x00404CE0.
 */
__extension__ typedef struct CLocaleCategory CLocaleCategory;
struct CLocaleCategory {
	uintptr_t *vtable; // +0x00
	uintptr_t member;  // +0x04
	uint32_t time_lo;  // +0x08
	uint32_t time_hi;  // +0x0C
};

/*
 * One CScriptStringDB element (16 bytes) - MSVC basic_string instantiation
 * carrying a single sdb.txt line.
 */
__extension__ typedef struct CSdbStr CSdbStr;
struct CSdbStr {
	int allocField; // +0x00 (allocator byte + 3 pad)
#if __SIZEOF_POINTER__ == 8
	int _pad64;     // 64-bit alignment pad
#endif
	char *data;     // +0x04 (NULL when empty)
	int length;     // +0x08
	int capacity;   // +0x0C
};

/*
 * std::vector<CSdbStr> backing the string database at 0x0063D8E8. Loaded
 * from sdb.txt by 0x0040107D; bytecode references strings by index.
 */
__extension__ typedef struct CScriptStringDB CScriptStringDB;
struct CScriptStringDB {
	int allocField; // +0x00
#if __SIZEOF_POINTER__ == 8
	int _pad64;     // 64-bit alignment pad
#endif
	CSdbStr *first; // +0x04 (_Myfirst)
	CSdbStr *last;  // +0x08 (_Mylast)
	CSdbStr *end;   // +0x0C (_Myend)
};

/*
 * MSVC std::vector<T> (16 bytes), used for entity pointer lists, deferred
 * serial collection, and spatial query results. Constructor at 0x00426460.
 */
__extension__ typedef struct CVector CVector;
struct CVector {
	char type;      // +0x00
	char pad[3];    // +0x01
#if __SIZEOF_POINTER__ == 8
	char _pad64[4]; // 64-bit alignment pad
#endif
	void *begin;    // +0x04
	void *end;      // +0x08
	void *capacity; // +0x0C
};

/*
 * Context for CEntityManager::GetNextMobile: dword flag followed by a
 * CVector of candidate serials.
 */
__extension__ typedef struct CNextMobileCtx CNextMobileCtx;
struct CNextMobileCtx {
	uint32_t field_0; // +0x00
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;  // 64-bit alignment pad
#endif
	CVector vector;   // +0x04
};

/*
 * MSVC std::_Tree node (24 bytes): left/parent/right links plus key/value
 * and color (0=red, 1=black; the nil sentinel is black).
 */
__extension__ typedef struct StdTreeNode StdTreeNode;
struct StdTreeNode {
	StdTreeNode *left;   // +0x00
	StdTreeNode *parent; // +0x04
	StdTreeNode *right;  // +0x08
	uintptr_t key;       // +0x0C
	uintptr_t value;     // +0x10
	int color;           // +0x14
};

/*
 * MSVC std::map wrapper (16 bytes) over StdTreeNode.
 */
__extension__ typedef struct StdMapTree StdMapTree;
struct StdMapTree {
	uint8_t comp[4];   // +0x00 (comparator bytes, unused)
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4]; // 64-bit alignment pad
#endif
	StdTreeNode *head; // +0x04 (sentinel header)
	int multi;         // +0x08 (0 for std::map)
	uint32_t size;     // +0x0C
};

/*
 * MSVC std::allocator<T> (empty class, 1 byte). Threaded as the receiver of
 * STL _Uninit_fill_n, _Uninit_copy, _Constructor, and _Destroy template
 * instantiations; the byte itself is never read.
 */
__extension__ typedef struct StdAllocator StdAllocator;
struct StdAllocator {
	uint8_t _dummy;
};

/*
 * MSVC smart pointer wrapper (12 bytes). Scalar deleting destructors
 * dispatch through vtable[0].
 */
__extension__ typedef struct CSmartPtr CSmartPtr;
struct CSmartPtr {
	uintptr_t *vtable; // +0x00
	uint32_t field04;  // +0x04
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;   // 64-bit alignment pad
#endif
	void *owned;       // +0x08
};

/*
 * Entry in a file index list (16 bytes): name plus offset/size integers.
 */
__extension__ typedef struct CFileEntry CFileEntry;
struct CFileEntry {
	char *name;     // +0x00
	int nameLength; // +0x04
	int fileOffset; // +0x08
	int fileSize;   // +0x0C
};

extern void *g_StdTreeNilNode; // 0x0063D8A4
extern int g_StdTreeNilRef; // 0x0063D8A8

void CCriticalSection_Unlock(uint32_t *this); // 0x0046C8E2
void CCriticalSection_Lock(uint32_t *this); // 0x0046C8ED
uint32_t CVector_GetCount16_Thiscall(CVector *this); // 0x004013A1
int CmpPtrValueEqual(const void *a, const void *b); // 0x00401670
int StdNilRef(void); // 0x00401690
uint32_t CVector_GetCount16(CVector *list); // 0x004019C0
int StdList_GetSize(StdPtrList *this); // 0x00403280
void Destroy_Range16(StdAllocator *this, void *first, void *last); // 0x00403340
void CVector16_Insert(CVector *this, void *pos, uint32_t count, void *val); // 0x00403A70
uintptr_t StdKfn_Identity(uintptr_t unused, uintptr_t key); // 0x00403DA0
void *Construct_Range16(CVector *this, void *dest, void *end, void *src); // 0x00404210
void *CVector_Allocate16(CVector *this, uint32_t count); // 0x00404290
uint32_t CIosBase_Width(CIosBase *this); // 0x00404610
void *Construct_RangeCopy16(void *first, void *last, void *dest); // 0x004046E0
uint8_t StdGetByte(uint32_t val); // 0x00404720
void Destroy_RangeFwd16(void *first, void *last, void *value); // 0x00404730
void *Destroy_RangeBwd16(void *first, void *last, void *dest); // 0x00404760
char CBasicIos_Fill(CIosBase *this); // 0x00404CA0
StdPtrNode **StdPtrIter_Constructor(StdPtrNode **iter); // 0x004066F0
void vector_Fill(void *first, void *last, void *value); // 0x00406A00
void StdPtrList_Destructor(StdPtrList *this); // 0x00420D30
StdPtrNode **StdPtrIter_CopyConstructor(StdPtrNode **dest, StdPtrNode **source); // 0x00421360
void *StdMap_InsertWrapper(StdMapTree *this, void *arg1, void *arg2); // 0x00421380
void *StdMap_End(StdMapTree *this, void *outIter); // 0x004213B0
void *StdMap_LowerBound(StdMapTree *this, void *outIter, void *keyPtr); // 0x004213D0
void *StdMap_Begin(StdMapTree *this, void *outIter); // 0x00421A90
void *StdMap_EraseWrapper(StdMapTree *this, void *outIter, void *pos); // 0x00421410
void *StdMap_FindWrapper(StdMapTree *this, void *outIter, void *keyPtr); // 0x00421430
void *StdMap_PairConstructor(uintptr_t *this, void *key, void *value); // 0x00421520
void CEntityMap_RangeQueryToList(CEntityMap *this, CList *list, int x, int y, int range); // 0x00421580
void *StdTreeNode_Value(StdTreeNode *node); // 0x004227A0
void *StdTree_FindInsertPos_Int(StdMapTree *tree, int *keyPtr); // 0x004234E0
void StdTree_LowerBound_Int(void *iter); // 0x004237F0
void Vector_SortByZ(void *begin, void *end, char typeTag); // 0x00423900
void Vector_SortByType(void *begin, void *end, uint8_t typeTag); // 0x00423930
int GameCentMon_GetPlayerCount(void); // 0x00423AA0
void StdPtrList_ConstructorWithType(StdPtrList *list, void *typeBytePtr); // 0x00424D30
void StdPtrList_ScalarDelete_4DD0(StdPtrList *list, void *value); // 0x00424DD0
void StdPtrList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos); // 0x00424E00
void *StdPtrList_EraseRange_4F70(StdPtrList *list, void *resultIter, StdPtrNode *beginNode, StdPtrNode *endNode); // 0x00424F70
void CVector_Destroy4_Range(CVector *this, void *first, void *last); // 0x00422740
void CVector_Constructor(CVector *list, const char *typeFlag); // 0x00426460
void CVector_Destructor(CVector *list); // 0x004264A0
void *vector_CopyBackward(void *first, void *last, void *dest_end); // 0x00426820
StdPtrNode **StdPtrList_Begin(StdPtrList *list, StdPtrNode **outIter); // 0x00426CA0
void StdPtrList_PushBack(StdPtrList *list, void *value); // 0x00426CD0
void StdPtrList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x00426D00
int StdPtrIter_Eq(StdPtrNode **a, StdPtrNode **b); // 0x00426E00
void CVector_PushBack(CVector *list, uintptr_t value); // 0x0042FF00
void CVector_PushBack_C960(CVector *this, uintptr_t *value); // 0x0046C960
uint32_t CVector_GetCount(CVector *list); // 0x004301A0
StdPtrNode **StdPtrIter_PostInc(StdPtrNode **iter, StdPtrNode **outIter, int dummy); // 0x0044D770
StdPtrNode **StdPtrNode_GetPrev(StdPtrNode *node); // 0x0044F710
void **StdPtrNode_GetValue(StdPtrNode *node); // 0x0044F720
void *StdPtrList_Charalloc(StdPtrList *list, uint32_t size); // 0x0044F750
void StdPtrList_Init(StdPtrList *list, const void *init); // 0x00457BB0
void StdPtrList_Clear(StdPtrList *list); // 0x00457BF0
StdPtrNode **StdPtrList_EraseRange(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last); // 0x00457D70
void Vector_SortRaw(void *begin, void *end); // 0x00457F90
void CFileEntry_Destructor(CFileEntry *self); // 0x00458F80
StdPtrNode *StdPtrNode_GetNext(StdPtrNode *node); // 0x0045AC80
void StdPtrIter_Inc(StdPtrNode **iter); // 0x0045AC90
StdPtrNode **StdPtrList_End(StdPtrList *list, StdPtrNode **outIter); // 0x00462050
void Vector_SortByDistPairEntry(uintptr_t *begin, uintptr_t *end, CLocation refLoc); // 0x004625D0
void **StdPtrIter_Deref(StdPtrNode **iter); // 0x004626A0
void *CVector_VecDestructor_Region(CVector *this, int flags); // 0x004A6410
__extension__ typedef struct CFragment CFragment;
__extension__ typedef struct CDefine CDefine;
__extension__ typedef struct StdPtrIterFull StdPtrIterFull;
StdPtrIterFull *StdPtrIter_Next(StdPtrIterFull *this); // 0x00420E80
StdPtrIterFull *StdPtrIter_Prev(StdPtrIterFull *this); // 0x00420EA0
void Vector_SortByDistPair(uintptr_t *begin, uintptr_t *end, CLocation refLoc); // 0x00462390
void *StdPtrList_InitLogin(StdPtrList *this, const void *init); // 0x0045AAB0
void *CMapNode_ScalarDtor(CFragment *this, int flags); // 0x0044D710
void *CMapIterator_ScalarDtor(CDefine *this, int flags); // 0x0044D740
void vector_SwapWrapper(uintptr_t *a, uintptr_t *b); // 0x00463990
void StdPtrList_Destructor_HelpQueue(StdPtrList *this); // 0x00469550
void CVector_DestructorSI(CVector *this); // 0x0046BA10
void CVector_PushBackSI(CVector *this, SurfaceInfo *value); // 0x0046BA80
void CVector_DestroyRangeSI(CVector *this, SurfaceInfo *first, SurfaceInfo *last); // 0x0046BB50
void SortSurface_Entry(SurfaceInfo *begin, SurfaceInfo *end, char typeTag); // 0x0046BEA0
void SortSurface_UnguardedInsert(SurfaceInfo *pos, SurfaceInfo *value, char typeTag); // 0x0046C3D0
int SortSurface_Compare(SurfaceInfo *a, SurfaceInfo *b); // 0x0046C430
int CVector_IsEmpty(CVector *vec); // 0x00472FD0
void CVector_EraseBack(CVector *vec); // 0x00472FF0
uintptr_t **CVector_PopBack(uint32_t **iter, uintptr_t **out); // 0x00473010
void *CVector_EraseSingle(CVector *vec, void *pos); // 0x00473040
void CVector_Swap16(CVector *dst, CVector *src); // 0x004787E0
void CVector_ClearAndFree16(CVector *vec); // 0x004788B0
CVector *CVector_AssignOp16(CVector *this, CVector *src); // 0x00478910
void *CVector_Resize16(CVector *vec, void *newEnd, void *srcStart); // 0x00478AE0
void CVector_ClearAndFree1C(CVector *vec); // 0x00478B30
uint32_t CVector_GetCount1C(CVector *list); // 0x00478BA0
void CVector_ClearAndFree6(CVector *this); // 0x00479080
void CVector_PushBack6(CVector *this, void *element); // 0x004790F0
uint32_t CVector_GetCapacity(CVector *list); // 0x00479380
uint32_t CVector_GetCapacity16(CVector *list); // 0x004793C0
void Destroy16_Range(CVector *this, void *first, void *last); // 0x00479450
void Destroy1C_Range(CVector *this, void *first, void *last); // 0x00479510
void Destroy6_Range(CVector *this, void *first, void *last); // 0x00479630
uint32_t CVector_GetCount6(CVector *list); // 0x0047A3F0
void *CVector_Allocate6(CVector *this, uint32_t count); // 0x0047A4B0
void *Vector_Find(void *begin, void *end, uintptr_t *value); // 0x0047ACC0
int StdAllocator_Equal(void); // 0x0047B150
void SortInt_Dispatch(void *first, void *last, uint8_t cmpVal, int unused); // 0x0047BEA0
void SortDist_Dispatch(void *first, void *last, CLocation cmpLoc); // 0x0047BF30
void Vector_SortByDist(void *begin, void *end, CLocation refLoc); // 0x0047F2B0
void *PacketGetDynamicSize(uint8_t *buf); // 0x0047F350
int PacketIsEDEDEDED(uint8_t *buf); // 0x0047F3A0
void *StdPtrList16_Constructor(StdPtrList *this, const void *init); // 0x004845E0
void StdPtrList_Destructor_NPC(StdPtrList *list); // 0x00484620
void StdPtrList16_InsertEnd(StdPtrList *list, void *value); // 0x00484680
void StdPtrList_Erase16(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos); // 0x004846B0
int StdPtrIter_Neq(StdPtrNode **a, StdPtrNode **b); // 0x00484770
void *StdPtrList16_VecDtor(StdPtrList *this, int flags); // 0x004847A0
void CopyFrom16(CVector *this, void *dst, void *src); // 0x00479890
StdPtrNode *StdFileList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint); // 0x00484920
StdPtrNode *StdPtrList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint); // 0x00484B90
StdPtrNode **StdPtrList_FindString(StdPtrNode **result, StdPtrNode **begin, StdPtrNode **end, CString *searchStr); // 0x00484C00
void *CVector_Erase(CVector *vec, void *first, void *last); // 0x004A64C0
StdPtrNode **StdPtrIter_BaseConstructor(StdPtrNode **iter); // 0x004E30A0
void *StdTreeIter_Deref(void *iter); // 0x004E30B0 (iterator is a stack-local pointer)
void *StdTreeNode_KeyValuePtr(StdTreeNode *node); // 0x004E30D0
void StdTree_Clear(StdMapTree *tree); // 0x004E63B0
StdTreeNode *StdTree_Insert(StdMapTree *tree, uintptr_t key, uintptr_t value); // 0x004E6420
void StdTree_RBErase(StdMapTree *tree, StdTreeNode *z); // 0x004E6500
StdTreeNode *StdTree_LowerBound(StdMapTree *tree, uintptr_t key); // 0x004E6FF0

#endif /* STL_H_ */
