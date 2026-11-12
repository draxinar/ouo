/*
 * STL primitives - CVector growth, StdPtrList iteration, and string helpers.
 *
 * Gathers the MSVC runtime pieces that the binary inlined from its STL
 * headers: vector push/reserve, list iterator advance, and a handful of
 * shared comparators and deleters. Every container-using subsystem links
 * against these primitives.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "containerhandle.h"
#include "convo.h"
#include "dat.h"
#include "help_queue.h"
#include "io.h"
#include "item.h"
#include "multi.h"
#include "packet_utils.h"
#include "packet_handler.h"
#include "player.h"
#include "region.h"
#include "resbank.h"
#include "stddeque.h"
#include "terrain.h"

#include "weapon.h"
#include "wombat_stl.h"

static void SortByPred_InsertionEntry(uintptr_t *first, uintptr_t *last, SortPred pred); // 0x00423B90
static void SortByPred_Quicksort(uintptr_t *first, uintptr_t *last, SortPred pred, int depth); // 0x00423BC0
static void SortByPred_UnguardedInsert(uintptr_t *pos, uintptr_t val, SortPred pred); // 0x00423C90
static void SortByPred_Insertion(uintptr_t *first, uintptr_t *last, SortPred pred, int depth); // 0x00423F60
static uintptr_t SortByPred_Median3(uintptr_t a, uintptr_t b, uintptr_t c, SortPred pred); // 0x00423FF0
static uintptr_t *SortByPred_Partition(uintptr_t *first, uintptr_t *last, uintptr_t pivot, SortPred pred); // 0x004240C0
static void *CVector_Insert(CVector *this, uintptr_t *pos, uintptr_t *value); // 0x00406710
static void CVector_InsertN(CVector *this, uintptr_t *pos, uint32_t count, uintptr_t *value); // 0x00406760
static uintptr_t *CVector_Ucopy(void *alloc, uintptr_t *begin, uintptr_t *end, uintptr_t *dest); // 0x00406980
static void CVector_UfillN(void *alloc, uintptr_t *first, uint32_t count, uintptr_t *value); // 0x004069C0
static StdPtrIterFull *StdPtrIter_Begin(StdPtrIterFull *this, StdPtrNode *sentinel); // 0x00420E50
static int StdPtrIter_IsValid(StdPtrIterFull *this); // 0x00420F40
static void StdMap_Destructor(StdMapTree *this); // 0x00421450
static void *StdMap_Init(StdMapTree *this, void *arg1, int unused, void *arg3); // 0x00421A50
void *StdMap_Begin(StdMapTree *this, void *outIter); // 0x00421A90
static void *StdTree_InsertWombat(StdMapTree *this, void *outIter, void *keyPtr); // 0x00421AC0
static void *StdTree_RBInsert_Wombat(StdMapTree *this, void *outIter, void *addLeft, void *parent, void *pair); // 0x00422970
static void StdMap_TreeErase(StdMapTree *this, void *outIter, void *pos); // 0x00421CC0
static void *StdMap_EraseRange(StdMapTree *this, void *outIter, void *beginIter, void *endIter); // 0x00422510
static void *StdMap_FindImpl(StdMapTree *this, void *outIter, void *keyPtr); // 0x00422650
static uintptr_t SortMulti_NoOp(uintptr_t arg); // 0x00422770
static int StdMap_IntKeyLess(StdMapTree *this, void *a, void *b); // 0x00422780
static void *StdTreeNode_Key(StdTreeNode *node); // 0x004E3550
static void StdTree_Inc(StdTreeNode **iter); // 0x004E7100
static void StdTree_Dec(StdTreeNode **iter); // 0x004E7040
static StdTreeNode *StdTree_RBInsert(StdMapTree *tree, int addLeft, StdTreeNode *parent, uintptr_t key, uintptr_t value); // 0x004E6CD0
static void StdTree_EraseSubtree(StdMapTree *tree, StdTreeNode *node); // 0x004E6C40
static StdTreeNode *StdTree_EraseRange(StdMapTree *tree, StdTreeNode *first, StdTreeNode *last); // 0x004E6A30
static void *StdMap_LowerBound(StdMapTree *this, void *outIter, void *keyPtr); // 0x004227B0
static void StdMap_EraseRecursive(StdMapTree *this, void *node); // 0x004227E0
static void StdMap_TreeInit(StdMapTree *this); // 0x00422890
static void *StdMap_GetHead(StdMapTree *this); // 0x00422E30
static void StdMap_TreeSplice(StdMapTree *this, void *node); // 0x00422E50
static void *StdMap_TreeMax(void *node); // 0x00422FC0
static void *StdMap_TreeMin(void *node); // 0x00423040
static void *StdMap_GetRmost(StdMapTree *this); // 0x004230C0
static void StdMap_TreeSplice2(StdMapTree *this, void *node); // 0x004230E0
static void *StdMap_IterPostInc(void *iter, void *outIter, int dummy); // 0x00423270
static void *StdMap_IterDec(void *iter); // 0x004232A0
static void *StdTree_Buynode(StdMapTree *this, void *parentNode, int value); // 0x004235B0
static void StdTree_ConstructorPair(StdAllocator *this, void *node, void *pair); // 0x00423600
static void *StdMap_IterInc(void *iter); // 0x00423620
static void StdMap_TreeDec(void *iter); // 0x00423640
static void *StdMap_GetRoot(StdMapTree *this); // 0x004237D0
static void *Vector_AllocElements(int count); // 0x00423960
static void *StdTree_ConstructorNode(void *node, void *pair); // 0x00423990
static int SortByZ_Compare(void *this, uintptr_t a, uintptr_t b); // 0x00423CE0
static int SortByType_Compare(void *this, uintptr_t a, uintptr_t b); // 0x00423E60
static void vector_SwapImpl(uintptr_t *a, uintptr_t *b); // 0x00424340
static void SortRaw_Main(uintptr_t *begin, uintptr_t *end, int depth); // 0x00457FC0
static void SortRaw_InsertionEntry(uintptr_t *begin, uintptr_t *end); // 0x00458040
static void SortRaw_Quicksort(uintptr_t *begin, uintptr_t *end, int depth); // 0x00458070
static void SortRaw_UnguardedInsert(uintptr_t *pos, uintptr_t val); // 0x00458130
static void SortRaw_Insertion(uintptr_t *begin, uintptr_t *end, int depth); // 0x00458170
static uintptr_t SortRaw_Median3(uintptr_t a, uintptr_t b, uintptr_t c); // 0x004581E0
static uintptr_t *SortRaw_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot); // 0x00458260
static int SortRaw_SkipN(const char *str, int count); // 0x004582C0
static int CRT_atoi(char **pStr); // 0x00458854
static void *CFileEntry_ScalarDelete(CFileEntry *self, int flags); // 0x00459290
static void *CFileEntry_CopyConstructor(CFileEntry *self, CFileEntry *src); // 0x00459220
static void *FileEntry_Constructor(void *dst, void *src); // 0x004591E0
static void *FileEntry_DestructorWrapper(void *entry); // 0x004591D0
static void StdFileList_ConstructorWrapper(void *list, void *dst, void *src); // 0x004591B0
static void StdFileList_Destroy(void *list, void *element); // 0x00459190
static void StdFileList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x004590E0
static void StdFileList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos); // 0x00459020
static void StdFileList_PushBack(StdPtrList *list, void *value); // 0x00458FF0
static void *CFileEntry_Constructor(CFileEntry *self, const char *name, int nameLength, int fileOffset, int fileSize); // 0x00458F20
static uint32_t CRT_divmod(uint32_t *pValue, uint32_t divisor); // 0x00458EF0
static void SortByDist_Main(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x00462CA0
static void SortByDist_InsertionEntry(uintptr_t *begin, uintptr_t *end, CLocation refLoc); // 0x00462EC0
static void SortByDist_Quicksort(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x00462F30
static void SortByDist_UnguardedInsert(uintptr_t *pos, uintptr_t val, CLocation refLoc); // 0x00463080
static int SortByDist_Compare(CLocation *refLoc, uintptr_t a, uintptr_t b); // 0x00463360
static void SortByDist_Insertion(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x004633B0
static uintptr_t SortByDist_Median3(uintptr_t a, uintptr_t b, uintptr_t c, CLocation refLoc); // 0x00463480
static uintptr_t *SortByDist_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation refLoc); // 0x004635A0
static void *CRT_PrintlInit(uint32_t *this); // 0x0046CCA0
static void SortSurface_SwapImpl(SurfaceInfo *a, SurfaceInfo *b, int depth); // 0x0046C700
static void SortSurface_Swap(SurfaceInfo *a, SurfaceInfo *b); // 0x0046C6D0
static SurfaceInfo *SortSurface_Partition(SurfaceInfo *begin, SurfaceInfo *end, SurfaceInfo *pivot, char typeTag); // 0x0046C650
static SurfaceInfo *SortSurface_Median3(SurfaceInfo *result, SurfaceInfo *a, SurfaceInfo *b, SurfaceInfo *c, char typeTag); // 0x0046C540
static void SortSurface_Insertion(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth); // 0x0046C490
static void SortSurface_Quicksort(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth); // 0x0046C260
static void SortSurface_InsertionEntry(SurfaceInfo *begin, SurfaceInfo *end, char typeTag); // 0x0046C230
static void SortSurface_Main(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth); // 0x0046C170
static void CVector_UfillNSI(CVector *this, SurfaceInfo *dest, uint32_t count, SurfaceInfo *src); // 0x0046BE40
static SurfaceInfo *CVector_UcopySI(CVector *this, SurfaceInfo *first, SurfaceInfo *last, SurfaceInfo *dest); // 0x0046BE00
static uint32_t CVector_GetCountC(CVector *list); // 0x0046BDC0
static void CVector_InsertSI(CVector *this, SurfaceInfo *pos, uint32_t count, SurfaceInfo *value); // 0x0046BB80
static void *CVector_InsertAtSI(CVector *this, uintptr_t index, SurfaceInfo *value); // 0x0046BB00
static void *CVector_Allocate4(CVector *this, uint32_t count, int unused); // 0x0047A350
static CVector *CVector_AssignOp4(CVector *this, CVector *src); // 0x004791F0
static CVector *CVector_CopyConstruct4(CVector *this, CVector *src); // 0x00479180
static void *SortMultiDist_Partition(void *first, void *last, uintptr_t pivot, CLocation cmpLoc); // 0x0047D3E0
static uintptr_t SortMultiDist_Median3(uintptr_t a, uintptr_t b, uintptr_t c, CLocation cmpLoc); // 0x0047D310
static void *SortMultiInt_Partition(void *first, void *last, uintptr_t pivot, int cmpVal); // 0x0047D200
static uintptr_t SortMultiInt_Median3(uintptr_t a, uintptr_t b, uintptr_t c, int cmpVal); // 0x0047D130
static void SortMultiDist_Quicksort(void *first, void *last, CLocation cmpLoc, int unused); // 0x0047C970
static void SortMultiInt_Quicksort(void *first, void *last, uint8_t cmpVal, int unused); // 0x0047C810
static void StdPtrList_DestructorWrapper_EntityMgr(void); // 0x00491DF0
static StdPtrNode **StdPtrList16_EraseAll(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last); // 0x004848D0
static void StdPtrList_Insert16(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x00484820
static void StdPtrList_DoInsert_4CF610(void *dest, void *source); // 0x004CF610
static void *vector_Copy(void *first, void *last, void *dest); // 0x004D9760
static void *StdTree_CopyIter(void *dest, void *srcPtr, void *srcByte); // 0x004E3500

// Binary 0x0063D8A4: MSVC std::_Tree _Nilnode sentinel pointer for wombat std::map.
// Binary 0x0063D8A8: Reference count for the sentinel.
void *g_StdTreeNilNode;         // 0x0063D8A4
int g_StdTreeNilRef;            // 0x0063D8A8

/*
 * 0x004013A1 - CVector::GetCount16 wrapper
 *
 * Delegates to CVector_GetCount16.
 */
uint32_t
CVector_GetCount16_Thiscall(CVector *this)
{
	return CVector_GetCount16(this);
}

/*
 * 0x00401670 - Pointer value equality comparator
 *
 * Dereferences both and returns 1 if values are equal, 0 otherwise. Used
 * as comparator callback by STL-style container operations (std::find,
 * etc.).
 */
int
CmpPtrValueEqual(const void *a, const void *b)
{
	return *(const uint32_t *)a == *(const uint32_t *)b;
}

/*
 * 0x00401690 - Return -1 constant
 *
 * Returns 0xFFFFFFFF. Used as npos/nil sentinel by STL container
 * operations (27 xrefs).
 */
int
StdNilRef(void)
{
	return -1;
}

/*
 * 0x004019C0 - CVector::GetCount (16-byte elements)
 *
 * Returns element count: (end - begin) / 16, or 0 if begin is NULL.
 */
uint32_t
CVector_GetCount16(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uintptr_t)list->end - (uintptr_t)list->begin) / sizeof(CString);
}

/*
 * 0x00403280 - std::container::size
 *
 * Returns *(int *)(this + 8).
 */
int
StdList_GetSize(StdPtrList *this)
{
	return this->size;
}

/*
 * 0x00403340 - std::_Destroy range (16-byte elements)
 *
 * Iterates from first to last in steps of 16, calling Destroy_Single16 on
 * each.
 */
void
Destroy_Range16(StdAllocator *this, void *first, void *last)
{
	char *p = (char *)first;
	char *e = (char *)last;
	while (p != e) {
		Destroy_Single16(this, p);
		p += sizeof(CString);
	}
}

/*
 * 0x00403A70 - CVector<16-byte>::insert
 *
 * Inserts count copies of val (16-byte element) at pos. Three paths:
 * reallocation needed, tail < count, tail >= count.
 */
void
CVector16_Insert(CVector *this, void *pos, uint32_t count, void *val)
{
	uint32_t spare = ((char *)this->capacity - (char *)this->end) / sizeof(CString);

	if (spare < count) {
		// Need reallocation
		uint32_t existing = CVector_GetCount16(this);
		uint32_t needed;
		if (count >= existing)
			needed = count;
		else
			needed = existing;
		uint32_t total = CVector_GetCount16(this) + needed;

		void *newbuf = CVector_Allocate16(this, total, 0);

		// Copy [begin, pos) to newbuf
		void *newpos = Construct_Range16(this, this->begin, pos, newbuf);

		// Fill count elements at newpos with val
		ConstructN_16((StdAllocator *)this, newpos, count, val);

		// Copy [pos, end) after filled region
		Construct_Range16(this, pos, this->end, (char *)newpos + count * sizeof(CString));

		// Destroy old elements
		Destroy_Range16((StdAllocator *)this, this->begin, this->end);

		// Free old buffer
		StdDeque_DeallocSI(this, this->begin, (int)(((char *)this->capacity - (char *)this->begin) / sizeof(CString)));

		// Update pointers
		this->capacity = (char *)newbuf + total * sizeof(CString);
		uint32_t old_count = CVector_GetCount16(this);
		this->end = (char *)newbuf + (old_count + count) * sizeof(CString);
		this->begin = newbuf;
	} else {
		// Enough spare capacity
		uint32_t tail_count = ((char *)this->end - (char *)pos) / sizeof(CString);

		if (tail_count < count) {
			// Inserting more elements than tail
			// Move tail right: copy [pos, end) to (pos + count*16)
			Construct_Range16(this, pos, this->end, (char *)pos + count * sizeof(CString));

			// Fill [end, end + (count - tail_count)*16) with val
			uint32_t fill_count = count - (((char *)this->end - (char *)pos) / sizeof(CString));
			ConstructN_16((StdAllocator *)this, this->end, fill_count, val);

			// Fill [pos, end) with val (cdecl)
			Fill16_Str(pos, this->end, val);

			// Update end
			this->end = (char *)this->end + count * sizeof(CString);
		} else if (count > 0) {
			// tail >= count, count > 0
			// Copy last count elements past end
			Construct_Range16(this, (char *)this->end - count * sizeof(CString), this->end, this->end);

			// Shift [pos, end - count*16) backward toward end
			CopyBackward16_Str(pos, (char *)this->end - count * sizeof(CString), this->end);

			// Fill [pos, pos + count*16) with val
			Fill16_Str(pos, (char *)pos + count * sizeof(CString), val);

			// Update end
			this->end = (char *)this->end + count * sizeof(CString);
		}
	}
}

/*
 * 0x00403DA0 - Return second argument
 *
 * Returns the second argument unchanged. Used as _Kfn (key-from-value)
 * callback by STL associative containers.
 */
uintptr_t
StdKfn_Identity(uintptr_t unused, uintptr_t key)
{
	USED(unused);
	return key;
}

/*
 * 0x00404210 - std::_Constructor range (copy construct 16-byte elements)
 *
 * Iterates from dest to end in steps of 16, calling
 * Allocator_CopyConstruct16 (copy construct single element) for each,
 * advancing source by 16.
 */
void *
Construct_Range16(CVector *this, void *dest, void *end, void *src)
{
	char *d = (char *)dest;
	char *e = (char *)end;
	char *s = (char *)src;
	while (d != e) {
		Allocator_CopyConstruct16((StdAllocator *)this, s, d);
		s += sizeof(CString);
		d += sizeof(CString);
	}
	return s;
}

/*
 * 0x00404290 - CVector::Allocate16
 *
 * Allocates buffer for 16-byte elements. Returns pointer to buffer.
 */
void *
CVector_Allocate16(CVector *this, uint32_t count, int unused)
{
	USED(this);
	USED(unused);
	return Allocate16_Inner((int)count, NULL);
}

/*
 * 0x00404610 - std::ios_base::width
 *
 * Returns the current field width.
 */
uint32_t
CIosBase_Width(CIosBase *this)
{
	return this->width;
}

/*
 * 0x004046E0 - std::copy (16-byte basic_string elements)
 *
 * Iterates from first to last in steps of 16, calling
 * String_CopyAssignDispatch on each with dest pointer. Returns final dest
 * pointer.
 */
__attribute__((unused)) void *
Copy16_Str(void *first, void *last, void *dest)
{
	CSdbStr *f = (CSdbStr *)first;
	CSdbStr *l = (CSdbStr *)last;
	CSdbStr *d = (CSdbStr *)dest;
	while (f != l) {
		String_CopyAssignDispatch(d, f);
		d++;
		f++;
	}
	return d;
}

/*
 * 0x00404720 - Return low byte of integer argument
 *
 * Truncates val to its low-order byte.
 */
uint8_t
StdGetByte(uint32_t val)
{
	return (uint8_t)val;
}

/*
 * 0x00404730 - std::fill (16-byte basic_string elements)
 *
 * Iterates from first to last in steps of 16, calling
 * String_CopyAssignDispatch on each with value.
 */
void
Fill16_Str(void *first, void *last, void *value)
{
	CSdbStr *f = (CSdbStr *)first;
	CSdbStr *l = (CSdbStr *)last;
	while (f != l) {
		String_CopyAssignDispatch(f, value);
		f++;
	}
}

/*
 * 0x00404760 - std::copy_backward (16-byte basic_string elements)
 *
 * Iterates backward from last to first in steps of 16, calling
 * String_CopyAssignDispatch on each with dest-16. Returns final dest
 * pointer.
 */
void *
CopyBackward16_Str(void *first, void *last, void *dest)
{
	CSdbStr *f = (CSdbStr *)first;
	CSdbStr *l = (CSdbStr *)last;
	CSdbStr *d = (CSdbStr *)dest;
	while (f != l) {
		l--;
		d--;
		String_CopyAssignDispatch(d, l);
	}
	return d;
}

/*
 * 0x00404CA0 - std::basic_ios::fill
 *
 * Returns the character used to pad a field out to its width.
 */
char
CBasicIos_Fill(CIosBase *this)
{
	return this->fill;
}

/*
 * 0x004066C0 - CVector_PushBack
 * 0x00421330 - CVector_PushBack
 * 0x00421550 - CVector_PushBack
 * 0x00426500 - CVector_PushBack
 * 0x0042E190 - CVector_PushBack
 * 0x0042E1C0 - CVector_PushBack
 * 0x0042E1F0 - CVector_PushBack
 * 0x0042FF00 - CVector_PushBack
 * 0x004367A0 - CVector_PushBack
 * 0x00462640 - CVector_PushBack
 * 0x00462670 - CVector_PushBack
 * 0x0046C960 - CVector_PushBack
 * 0x00473860 - CVector_PushBack
 * 0x00479020 - CVector_PushBack
 * 0x00479050 - CVector_PushBack
 * 0x004A6490 - CVector_PushBack
 * 0x004CECB0 - CVector_PushBack
 * 0x004CECE0 - CVector_PushBack
 * 0x004CED10 - CVector_PushBack
 *
 * vector::push_back for a vector of 4-byte elements: inserts value at
 * end(). UoDemo.exe carries nineteen instantiations, one per element
 * type, and they are one stream. The binary takes the value by const
 * reference and reads end() through the +8 getter at 0x00403280, which
 * our C types as StdList_GetSize returning int, so the field is read
 * directly and the value is passed by value.
 */
void
CVector_PushBack(CVector *list, uintptr_t value)
{
	CVector_Insert(list, (uintptr_t *)list->end, &value);
}

/*
 * 0x004066F0 - std::list<void*>::iterator constructor
 * 0x0049E590 - SearchNode_IterConstructor
 *
 * Calls _Container_base ctor (0x004E30A0, no-op). Returns iterator. C++
 * object construction ceremony with no effect.
 */
StdPtrNode **
StdPtrIter_Constructor(StdPtrNode **iter)
{
	StdPtrIter_BaseConstructor(iter);
	return iter;
}

/*
 * 0x00406710 - CVector_Insert
 * 0x00421760 - CVector_Insert
 * 0x004226F0 - CVector_Insert
 * 0x00426530 - CVector_Insert
 * 0x0042E240 - CVector_Insert
 * 0x0042E290 - CVector_Insert
 * 0x0042E2E0 - CVector_Insert
 * 0x0042FF30 - CVector_Insert
 * 0x004367D0 - CVector_Insert
 * 0x004626C0 - CVector_Insert
 * 0x00462710 - CVector_Insert
 * 0x0046C990 - CVector_Insert
 * 0x00473890 - CVector_Insert
 * 0x00479540 - CVector_Insert
 * 0x00479590 - CVector_Insert
 * 0x004A6580 - CVector_Insert
 * 0x004CED40 - CVector_Insert
 * 0x004CED90 - CVector_Insert
 * 0x004CEDE0 - CVector_Insert
 *
 * vector::insert(pos, value): records pos as an index, inserts one element
 * there through CVector_InsertN, and returns the position again from the
 * possibly reallocated buffer. The binary computes the index with >> 2,
 * its elements being 32-bit. The step here is the pointer width, which
 * 64-bit builds need.
 */
static void *
CVector_Insert(CVector *this, uintptr_t *pos, uintptr_t *value)
{
	uintptr_t bucket;
	int offset;

	bucket = (uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)this);
	offset = (int)(((uintptr_t)pos - bucket) / sizeof(uintptr_t));
	CVector_InsertN(this, pos, 1, value);
	bucket = (uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)this);
	return (void *)(bucket + (uintptr_t)offset * sizeof(uintptr_t));
}

/*
 * 0x00406760 - CVector_InsertN
 * 0x004217B0 - CVector_InsertN
 * 0x004232C0 - CVector_InsertN
 * 0x00426580 - CVector_InsertN
 * 0x0042E330 - CVector_InsertN
 * 0x0042E550 - CVector_InsertN
 * 0x0042E770 - CVector_InsertN
 * 0x0042FF80 - CVector_InsertN
 * 0x00436820 - CVector_InsertN
 * 0x00462760 - CVector_InsertN
 * 0x00462980 - CVector_InsertN
 * 0x0046CA00 - CVector_InsertN
 * 0x004738E0 - CVector_InsertN
 * 0x00479BB0 - CVector_InsertN
 * 0x00479DD0 - CVector_InsertN
 * 0x004A65D0 - CVector_InsertN
 * 0x004CEE30 - CVector_InsertN
 * 0x004CF050 - CVector_InsertN
 * 0x004CF270 - CVector_InsertN
 *
 * vector::_Insert_n(pos, count, value): inserts count copies of value
 * before pos. When the spare capacity is too small it allocates
 * size + max(count, size) elements, copies the head, fills the new
 * elements, copies the tail, then destroys and frees the old buffer.
 * Otherwise it shifts in place.
 */
static void
CVector_InsertN(CVector *this, uintptr_t *pos, uint32_t count, uintptr_t *value)
{
	uint32_t curCapacity;
	uint32_t totalCapacity;
	uintptr_t *newBuf;
	uintptr_t *newEnd;

	curCapacity = (uint32_t)(((uintptr_t)this->capacity - (uintptr_t)this->end) / sizeof(uintptr_t));

	if (curCapacity < count) {
		uint32_t growTo;

		if (count < CVector_GetCount(this))
			growTo = CVector_GetCount(this);
		else
			growTo = count;

		totalCapacity = CVector_GetCount(this) + growTo;

		newBuf = (uintptr_t *)CVector_Allocate4(this, totalCapacity, 0);

		newEnd = (uintptr_t *)CVector_Ucopy((StdAllocator *)this, this->begin, pos, newBuf);

		CVector_UfillN((StdAllocator *)this, newEnd, count, value);

		CVector_Ucopy((StdAllocator *)this, pos, this->end, newEnd + count);

		CVector_Destroy4_Range(this, this->begin, this->end);
		{
			uint32_t oldCount = (uint32_t)(((uintptr_t)this->capacity - (uintptr_t)this->begin) / sizeof(uintptr_t));
			StdDeque_DeallocSI(this, this->begin, oldCount);
		}

		this->capacity = (void *)(newBuf + totalCapacity);
		this->end = (void *)(newBuf + CVector_GetCount(this) + count);
		this->begin = newBuf;
	} else {
		uint32_t afterCount = (uint32_t)(((uintptr_t)this->end - (uintptr_t)pos) / sizeof(uintptr_t));

		if (afterCount < count) {
			CVector_Ucopy((StdAllocator *)this, pos, this->end, pos + count);
			CVector_UfillN((StdAllocator *)this, (uintptr_t *)this->end, count - afterCount, value);
			vector_Fill(pos, this->end, value);
			this->end = (void *)((uintptr_t *)this->end + count);
		} else if (count > 0) {
			CVector_Ucopy((StdAllocator *)this, (uintptr_t *)this->end - count, this->end, this->end);
			vector_CopyBackward(pos, (uintptr_t *)this->end - count, this->end);
			vector_Fill(pos, pos + count, value);
			this->end = (void *)((uintptr_t *)this->end + count);
		}
	}
}

/*
 * 0x00406980 - CVector_Ucopy
 * 0x004219D0 - CVector_Ucopy
 * 0x00423750 - CVector_Ucopy
 * 0x004267A0 - CVector_Ucopy
 * 0x0042E990 - CVector_Ucopy
 * 0x0042EA10 - CVector_Ucopy
 * 0x0042EA90 - CVector_Ucopy
 * 0x004301E0 - CVector_Ucopy
 * 0x00436A40 - CVector_Ucopy
 * 0x00462BA0 - CVector_Ucopy
 * 0x00462C20 - CVector_Ucopy
 * 0x0046CC20 - CVector_Ucopy
 * 0x00473B00 - CVector_Ucopy
 * 0x0047A2D0 - CVector_Ucopy
 * 0x0047A370 - CVector_Ucopy
 * 0x004A67F0 - CVector_Ucopy
 * 0x004CF490 - CVector_Ucopy
 * 0x004CF510 - CVector_Ucopy
 * 0x004CF590 - CVector_Ucopy
 *
 * vector::_Ucopy: copies the elements of [begin, end) to dest through the
 * construct call and returns the end of what it wrote.
 */
static uintptr_t *
CVector_Ucopy(void *alloc, uintptr_t *begin, uintptr_t *end, uintptr_t *dest)
{
	while (begin != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)alloc, dest, begin);
		dest++;
		begin++;
	}
	return dest;
}

/*
 * 0x004069C0 - CVector_UfillN
 * 0x00421A10 - CVector_UfillN
 * 0x00423790 - CVector_UfillN
 * 0x004267E0 - CVector_UfillN
 * 0x0042E9D0 - CVector_UfillN
 * 0x0042EA50 - CVector_UfillN
 * 0x0042EAD0 - CVector_UfillN
 * 0x00430220 - CVector_UfillN
 * 0x00436A80 - CVector_UfillN
 * 0x00462BE0 - CVector_UfillN
 * 0x00462C60 - CVector_UfillN
 * 0x0046CC60 - CVector_UfillN
 * 0x00473B40 - CVector_UfillN
 * 0x0047A310 - CVector_UfillN
 * 0x0047A3B0 - CVector_UfillN
 * 0x004A6830 - CVector_UfillN
 * 0x004CF4D0 - CVector_UfillN
 * 0x004CF550 - CVector_UfillN
 * 0x004CF5D0 - CVector_UfillN
 *
 * vector::_Ufill: constructs count elements at first from *value, reading
 * the same source on every pass.
 */
static void
CVector_UfillN(void *alloc, uintptr_t *first, uint32_t count, uintptr_t *value)
{
	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)alloc, first, value);
		count--;
		first++;
	}
}

/*
 * 0x00406A00 - std::fill (forward fill)
 *
 * Fills [first, last) with the element pointed to by value.
 *
 * 64-bit: the binary steps 4 bytes because its elements are 32-bit
 * pointers. The only vector reaching here holds pointers, so the step
 * is the pointer width.
 */
void
vector_Fill(void *first, void *last, void *value)
{
	uintptr_t *p = first;
	uintptr_t *e = last;

	while (p != e) {
		*p = *(uintptr_t *)value;
		p++;
	}
}

/*
 * 0x00420D30 - StdPtrList::~StdPtrList
 *
 * Calls StdMap_Destructor(this) to destroy the underlying tree structure.
 */
void
StdPtrList_Destructor(StdPtrList *this)
{
	StdMap_Destructor((StdMapTree *)this);
}

/*
 * 0x00420E50 - StdPtrIter::Begin
 *
 * Sets this->sentinel = arg, this->current = sentinel->next. Returns this.
 * Iterator init pointing to first element of list.
 */
static __attribute__((unused)) StdPtrIterFull *
StdPtrIter_Begin(StdPtrIterFull *this, StdPtrNode *sentinel)
{
	this->sentinel = sentinel;
	this->current = sentinel->next;
	return this;
}

/*
 * 0x004582FB - CRT_output
 *
 * OMITTED - MSVC CRT runtime function (_output). Part of the CRT printf
 * formatting engine. Not decompiled; Linux uses glibc printf.
 */

/*
 * 0x00420E80 - StdPtrIter::Next
 *
 * Advances iterator to next node by following the next pointer of the
 * current node. Returns this.
 */
StdPtrIterFull *
StdPtrIter_Next(StdPtrIterFull *this)
{
	this->current = this->current->next;
	return this;
}

/*
 * 0x0045889F - CRT_output_number
 *
 * OMITTED - MSVC CRT runtime function (_output number formatting). Part of
 * the CRT printf formatting engine. Not decompiled; Linux uses glibc
 * printf.
 */

/*
 * 0x00458B0C - CRT_printf_mode1
 *
 * OMITTED - MSVC CRT printf wrapper. Formats through CRT_output
 * (0x004582FB) into a 1024-byte stack buffer, sets the output-mode
 * global at 0x00645B24 to 1, emits the buffer with "%s", then clears
 * the mode. Not decompiled; Linux uses glibc printf.
 */

/*
 * 0x00458BAB - CRT_fprintf_mode2
 *
 * OMITTED - as 0x00458B0C but writes to a FILE * with mode 2.
 */

/*
 * 0x00458C1D - CRT_printf_mode3
 *
 * OMITTED - as 0x00458B0C with mode 3 and an explicit va_list.
 */

/*
 * 0x00458C75 - CRT_fprintf_mode4
 *
 * OMITTED - as 0x00458BAB with mode 4 and an explicit va_list.
 */

/*
 * 0x00458CF5 - CRT_AtexitRegister
 *
 * OMITTED - MSVC C++ static-teardown registry. Lazily allocates the
 * 12-byte registry head at 0x00645B28 and appends the callback under an
 * SEH frame. Linux registers destructors through glibc atexit.
 */

/*
 * 0x00458DC8 - CRT_AtexitRunOne
 *
 * OMITTED - walks the 0x00645B28 registry with StdPtrList iterators and
 * invokes the matching entry. Counterpart of 0x00458CF5.
 */

/*
 * 0x00458E5B - CRT_AtexitRunAll
 *
 * OMITTED - as 0x00458DC8 but runs every registered entry.
 */

/*
 * 0x00420EA0 - StdPtrIter::Prev
 *
 * Moves iterator to previous node by following the prev pointer of the
 * current node. Returns this.
 */
StdPtrIterFull *
StdPtrIter_Prev(StdPtrIterFull *this)
{
	this->current = this->current->prev;
	return this;
}

/*
 * 0x00420F40 - StdPtrIter::IsValid
 *
 * Returns 1 if the iterator's current node pointer is non-null, 0
 * otherwise.
 */
static __attribute__((unused)) int
StdPtrIter_IsValid(StdPtrIterFull *this)
{
	return this->current != NULL;
}

/*
 * 0x00421360 - std::list<void*>::iterator copy constructor
 *
 * Copies *source to *dest. Returns dest.
 */
StdPtrNode **
StdPtrIter_CopyConstructor(StdPtrNode **dest, StdPtrNode **source)
{
	*dest = *source;
	return dest;
}

/*
 * 0x00421380 - std::map::insert wrapper
 *
 * Calls StdMap_Init with multi=0 to insert the key-value pair.
 */
void *
StdMap_InsertWrapper(StdMapTree *this, void *arg1, void *arg2)
{
	StdMap_Init(this, arg1, 0, arg2);
	return this;
}

/*
 * 0x004213B0 - std::map::end
 *
 * Calls StdPtrList_End to get the end iterator (sentinel), stores it in
 * the output. Returns output.
 */
void *
StdMap_End(StdMapTree *this, void *outIter)
{
	StdPtrList_End((StdPtrList *)this, outIter);
	return outIter;
}

/*
 * 0x004213D0 - std::map::insert
 *
 * Creates a local iterator, calls std::_Tree::insert on the pair, then
 * copies the result to output.
 */
void *
StdMap_Insert(StdMapTree *this, void *outIter, void *keyPtr)
{
	uintptr_t localIter[2];

	StdTree_InsertWombat(this, localIter, keyPtr);
	StdTree_CopyIter(outIter, localIter, (char *)localIter + sizeof(uintptr_t));
	return outIter;
}

/*
 * 0x00421410 - std::map::erase wrapper
 *
 * Calls StdMap_TreeErase with two iterator args. Returns first arg.
 */
void *
StdMap_EraseWrapper(StdMapTree *this, void *outIter, void *pos)
{
	StdMap_TreeErase(this, outIter, pos);
	return outIter;
}

/*
 * 0x00421430 - std::map::find wrapper
 *
 * Forwards to StdMap_FindImpl and returns outIter.
 */
void *
StdMap_FindWrapper(StdMapTree *this, void *outIter, void *keyPtr)
{
	StdMap_FindImpl(this, outIter, keyPtr);
	return outIter;
}

/*
 * 0x00421450 - std::map::destructor
 *
 * Gets begin/end iterators, runs StdMap_EraseRange to clear the tree, frees
 * the head node, and decrements the global tree reference count.
 */
static void
StdMap_Destructor(StdMapTree *this)
{
	void *endIter, *beginIter, *insertIter;

	StdPtrList_End((StdPtrList *)this, (StdPtrNode **)&endIter);
	StdMap_Begin(this, &beginIter);
	StdMap_EraseRange(this, &insertIter, *(uint32_t **)&beginIter, *(uint32_t **)&endIter);
	free(this->head);
	this->head = NULL;
	this->size = 0;

	g_StdTreeNilRef--;
	if (g_StdTreeNilRef == 0) {
		free(g_StdTreeNilNode);
		g_StdTreeNilNode = NULL;
	}
}

/*
 * 0x00421520 - std::map::value_type constructor
 *
 * Copies *key into pair[0] and *value into pair[1]. Returns this. Used to
 * construct the key-value pair for std::map insert.
 */
void *
StdMap_PairConstructor(uintptr_t *this, void *key, void *value)
{
	this[0] = *(uintptr_t *)key;
	this[1] = *(uintptr_t *)value;
	return this;
}

/*
 * 0x00421580 - CEntityMap::RangeQueryToList
 *
 * Same block-iteration pattern as RangeQuery but populates a CList
 * directly instead of a CVector. For each entity in range, appends
 * (WTYPE_OBJ=4, entity->serial) to the CList. Used by
 * Script_getPlayersInRange, Script_getMobsInRange, Script_getNPCsInRange,
 * and CNPC_ScanForTargets.
 */
void
CEntityMap_RangeQueryToList(CEntityMap *this, CList *list, int x, int y, int range)
{
	int startBlockX, endBlockX, startBlockY, endBlockY;
	int blockIdx, rowWidth;
	int bx, by;
	StdPtrNode *iter, *endNode, *copyIter, *tmpIter;

	// Compute block bounds from coordinate range
	startBlockX = (x - range) >> this->blockShift;
	startBlockX -= this->originX;
	endBlockX = (x + range) >> this->blockShift;
	endBlockX -= this->originX;

	startBlockY = (y - range) >> this->blockShift;
	startBlockY -= this->originY;
	endBlockY = (y + range) >> this->blockShift;
	endBlockY -= this->originY;

	// Clamp to grid bounds
	if (startBlockX < 0)
		startBlockX = 0;
	if (endBlockX >= this->gridW)
		endBlockX = this->gridW - 1;
	if (startBlockY < 0)
		startBlockY = 0;
	if (endBlockY >= this->gridH)
		endBlockY = this->gridH - 1;

	// Compute initial block index and row width
	blockIdx = startBlockY * this->gridW + startBlockX;
	rowWidth = endBlockX - startBlockX + 1;

	for (by = startBlockY; by <= endBlockY; by++) {
		for (bx = startBlockX; bx <= endBlockX; bx++) {
			// Init iterator, get begin
			StdPtrIter_Constructor(&iter);
			StdPtrList_Begin(&this->blocks[blockIdx], &copyIter);
			iter = copyIter;

			while (1) {
				// Get end, compare
				StdPtrList_End(&this->blocks[blockIdx], &endNode);
				StdPtrIter_CopyConstructor(&tmpIter, &endNode);
				if (!StdPtrIter_Neq(&iter, &tmpIter))
					break;

				// Distance check via CMobile_DistXY
				{
					CItem *entity = (CItem *)*StdPtrIter_Deref(&iter);
					if (CMobile_DistXY((CMobile *)entity, x, y) <= range) {
						CItem *e2 = (CItem *)*StdPtrIter_Deref(&iter);
						CList_Append(list, 4, (uint32_t)e2->serial);
					}
				}

				// Post-increment
				StdPtrIter_PostInc(&iter, &tmpIter, 0);
			}

			blockIdx++;
		}
		// Skip to next row: advance by gridW - rowWidth
		blockIdx += this->gridW - rowWidth;
	}
}

/*
 * 0x00421A50 - std::_Tree::_Init
 *
 * Initializes the tree with the given color byte and value byte. Copies
 * bytes from args into the tree structure at offsets +0, +1, +8. Then
 * calls the tree rebalance init function (0x00422890).
 */
static void *
StdMap_Init(StdMapTree *this, void *arg1, int unused, void *arg3)
{
	this->comp[0] = *(uint8_t *)arg3;
	this->comp[1] = *(uint8_t *)arg1;
	this->multi = (uint8_t)unused;
	StdMap_TreeInit(this);
	return this;
}

/*
 * 0x00421A90 - std::_Tree::begin
 *
 * Gets the head node via StdMap_GetHead, reads the node pointer from it,
 * and stores it in the output iterator via CIterCtx_Set. Returns output.
 */
void *
StdMap_Begin(StdMapTree *this, void *outIter)
{
	void *headPtr = StdMap_GetHead(this);
	uintptr_t node = *(uintptr_t *)headPtr;
	CIterCtx_Set(outIter, (void *)node);
	return outIter;
}

/*
 * 0x00421AC0 - std::_Tree::insert (map insert wrapper)
 *
 * Walks the tree to find the insertion point using StdPtrNode accessor
 * functions and g_StdTreeNilNode sentinel. If the key already exists (and
 * multi is not set), returns the existing node. Otherwise calls
 * StdTree_RBInsert_Wombat (0x00422970) to allocate and link a new node
 * with RB rebalancing. Stores result in outIter via StdTree_CopyIter with
 * a trueByte/falseByte flag.
 */
static void *
StdTree_InsertWombat(StdMapTree *this, void *outIter, void *keyPtr)
{
	StdMapTree *tree = this;
	void *where = tree->head;
	void *node;
	uint8_t addLeft = 1;

	node = *(void **)StdMap_GetRoot(this);

	while (node != g_StdTreeNilNode) {
		where = node;
		if (StdMap_IntKeyLess(this, (void *)(uintptr_t)SortMulti_NoOp((uintptr_t)keyPtr), StdTreeNode_Key(node)) & 0xFF) {
			addLeft = 1;
			node = *(void **)StdPtrNode_GetNext((StdPtrNode *)node);
		} else {
			addLeft = 0;
			node = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);
		}
	}

	if (tree->multi) {
		uint8_t trueByte = 1;
		uintptr_t localIter;
		StdTree_RBInsert_Wombat(this, &localIter, node, where, keyPtr);
		StdTree_CopyIter(outIter, &localIter, &trueByte);
		return outIter;
	}

	{
		uintptr_t iterVal;
		CIterCtx_Set(&iterVal, where);

		if (addLeft) {
			uintptr_t beginVal;
			StdMap_Begin(this, &beginVal);
			if (StdPtrIter_Eq((StdPtrNode **)&iterVal, (StdPtrNode **)&beginVal) & 0xFF) {
				uint8_t trueByte = 1;
				uintptr_t localIter;
				StdTree_RBInsert_Wombat(this, &localIter, node, where, keyPtr);
				StdTree_CopyIter(outIter, &localIter, &trueByte);
				return outIter;
			}
			StdMap_IterDec(&iterVal);
		}

		{
			void *iterNode = *(void **)&iterVal;
			if (StdMap_IntKeyLess(this, StdTreeNode_Key(iterNode), (void *)(uintptr_t)SortMulti_NoOp((uintptr_t)keyPtr)) & 0xFF) {
				uint8_t trueByte = 1;
				uintptr_t localIter;
				StdTree_RBInsert_Wombat(this, &localIter, node, where, keyPtr);
				StdTree_CopyIter(outIter, &localIter, &trueByte);
				return outIter;
			}
		}

		{
			uint8_t falseByte = 0;
			StdTree_CopyIter(outIter, &iterVal, &falseByte);
			return outIter;
		}
	}
}

/*
 * 0x00421CC0 - std::_Tree::erase (RB delete with fixup)
 *
 * Erases a single node from the RB tree with full rebalancing. Uses
 * StdPtrNode accessor functions and g_StdTreeNilNode sentinel.
 */
static void
StdMap_TreeErase(StdMapTree *this, void *outIter, void *pos)
{
	void *postDecOut;
	void *y;
	void *z;
	void *x;
	void *w;
	void *tmp;

	tmp = StdMap_IterPostInc(&pos, &postDecOut, 0);
	y = *(void **)tmp;
	z = y;

	if (*(void **)StdPtrNode_GetNext((StdPtrNode *)y) == g_StdTreeNilNode) {
		x = *(void **)StdPtrNode_GetValue((StdPtrNode *)y);
	} else if (*(void **)StdPtrNode_GetValue((StdPtrNode *)y) == g_StdTreeNilNode) {
		x = *(void **)StdPtrNode_GetNext((StdPtrNode *)y);
	} else {
		y = StdMap_TreeMin(*(void **)StdPtrNode_GetValue((StdPtrNode *)y));
		x = *(void **)StdPtrNode_GetValue((StdPtrNode *)y);
	}

	if (y != z) {
		*(void **)StdPtrNode_GetPrev(*(StdPtrNode **)StdPtrNode_GetNext((StdPtrNode *)z)) = y;

		{
			void *zNextAddr = StdPtrNode_GetNext((StdPtrNode *)z);
			void *yNextAddr = StdPtrNode_GetNext((StdPtrNode *)y);
			*(void **)yNextAddr = *(void **)zNextAddr;
		}

		if (y == *(void **)StdPtrNode_GetValue((StdPtrNode *)z)) {
			*(void **)StdPtrNode_GetPrev((StdPtrNode *)x) = y;
		} else {
			{
				void *yPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)y);
				void *xPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)x);
				*(void **)xPrevAddr = *(void **)yPrevAddr;
			}

			{
				void *yParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)y);
				*(void **)StdPtrNode_GetNext((StdPtrNode *)yParent) = x;
			}

			{
				void *zValAddr = StdPtrNode_GetValue((StdPtrNode *)z);
				void *yValAddr = StdPtrNode_GetValue((StdPtrNode *)y);
				*(void **)yValAddr = *(void **)zValAddr;
			}

			{
				void *zRightChild = *(void **)StdPtrNode_GetValue((StdPtrNode *)z);
				*(void **)StdPtrNode_GetPrev((StdPtrNode *)zRightChild) = y;
			}
		}

		if (*(void **)StdMap_GetRoot(this) == z) {
			*(void **)StdMap_GetRoot(this) = y;
		} else {
			void *zParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)z);
			if (*(void **)StdPtrNode_GetNext((StdPtrNode *)zParent) == z) {
				*(void **)StdPtrNode_GetNext((StdPtrNode *)zParent) = y;
			} else {
				*(void **)StdPtrNode_GetValue((StdPtrNode *)zParent) = y;
			}
		}

		{
			void *zPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)z);
			void *yPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)y);
			*(void **)yPrevAddr = *(void **)zPrevAddr;
		}

		vector_SwapImpl((uintptr_t *)StdTreeNode_Color(y), (uintptr_t *)StdTreeNode_Color(z));

		y = z;
	} else {
		{
			void *zPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)z);
			void *xPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)x);
			*(void **)xPrevAddr = *(void **)zPrevAddr;
		}

		if (*(void **)StdMap_GetRoot(this) == z) {
			*(void **)StdMap_GetRoot(this) = x;
		} else {
			void *zParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)z);
			if (*(void **)StdPtrNode_GetNext((StdPtrNode *)zParent) == z) {
				*(void **)StdPtrNode_GetNext((StdPtrNode *)zParent) = x;
			} else {
				*(void **)StdPtrNode_GetValue((StdPtrNode *)zParent) = x;
			}
		}

		if (*(void **)StdMap_GetHead(this) == z) {
			if (*(void **)StdPtrNode_GetValue((StdPtrNode *)z) == g_StdTreeNilNode) {
				void *zPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)z);
				*(void **)StdMap_GetHead(this) = *(void **)zPrevAddr;
			} else {
				*(void **)StdMap_GetHead(this) = StdMap_TreeMin(x);
			}
		}

		if (*(void **)StdMap_GetRmost(this) == z) {
			if (*(void **)StdPtrNode_GetNext((StdPtrNode *)z) == g_StdTreeNilNode) {
				void *zPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)z);
				*(void **)StdMap_GetRmost(this) = *(void **)zPrevAddr;
			} else {
				*(void **)StdMap_GetRmost(this) = StdMap_TreeMax(x);
			}
		}
	}

	if (*(int *)StdTreeNode_Color(y) != 1)
		goto done;

	for (;;) {
		if (x == *(void **)StdMap_GetRoot(this))
			break;
		if (*(int *)StdTreeNode_Color(x) != 1)
			break;

		{
			void *xParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
			if (x == *(void **)StdPtrNode_GetNext((StdPtrNode *)xParent)) {
				w = *(void **)StdPtrNode_GetValue((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));

				if (*(int *)StdTreeNode_Color(w) == 0) {
					*(int *)StdTreeNode_Color(w) = 1;
					*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 0;
					StdMap_TreeSplice(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
					w = *(void **)StdPtrNode_GetValue((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				if (*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) == 1 &&
				        *(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Color(w) = 0;
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					continue;
				}

				if (*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) = 1;
					*(int *)StdTreeNode_Color(w) = 0;
					StdMap_TreeSplice2(this, w);
					w = *(void **)StdPtrNode_GetValue((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				*(int *)StdTreeNode_Color(w) = *(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) = 1;
				StdMap_TreeSplice(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				break;
			} else {
				w = *(void **)StdPtrNode_GetNext((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));

				if (*(int *)StdTreeNode_Color(w) == 0) {
					*(int *)StdTreeNode_Color(w) = 1;
					*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 0;
					StdMap_TreeSplice2(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
					w = *(void **)StdPtrNode_GetNext((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				if (*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) == 1 &&
				        *(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Color(w) = 0;
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					continue;
				}

				if (*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) = 1;
					*(int *)StdTreeNode_Color(w) = 0;
					StdMap_TreeSplice(this, w);
					w = *(void **)StdPtrNode_GetNext((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				*(int *)StdTreeNode_Color(w) = *(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) = 1;
				StdMap_TreeSplice2(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				break;
			}
		}
	}

	*(int *)StdTreeNode_Color(x) = 1;

	// clang-format off
done: {
	void *kvPtr = StdTreeNode_KeyValuePtr(y);
	CVector_Destroy6_Single((CVector *)this, kvPtr);
}
	// clang-format on
	free(y);
	{
		StdMapTree *tree = (StdMapTree *)this;
		tree->size = tree->size - 1;
	}

	*(void **)outIter = pos;
}

/*
 * 0x00422510 - std::_Tree::erase range
 *
 * If valNode is 0, or [begin, end) covers the whole tree, performs a full
 * clear: erases recursively from root, resets size/head/root/most. Otherwise
 * loops erasing individual elements via StdMap_TreeErase.
 */
static void *
StdMap_EraseRange(StdMapTree *this, void *outIter, void *beginIter, void *endIter)
{
	uintptr_t localBegin, localEnd;
	uintptr_t postDec;
	uintptr_t eraseOut;

	// The size read goes through a fixed-offset getter that /OPT:ICF
	// folded onto CSearchCtx::GetValNode; the receiver is the tree.
	if (this->size == 0)
		goto erase_loop;

	localBegin = *(uintptr_t *)StdMap_Begin(this, &localBegin);
	if (StdPtrIter_Neq((StdPtrNode **)&beginIter, (StdPtrNode **)&localBegin) & 0xFF)
		goto erase_loop;

	localEnd = *(uintptr_t *)StdPtrList_End((StdPtrList *)this, (StdPtrNode **)&localEnd);
	if (StdPtrIter_Neq((StdPtrNode **)&endIter, (StdPtrNode **)&localEnd) & 0xFF)
		goto erase_loop;

	// Full tree clear path
	{
		void *mostPtr;

		mostPtr = StdMap_GetRoot(this);
		StdMap_EraseRecursive(this, *(void **)mostPtr);

		mostPtr = StdMap_GetRoot(this);
		*(void **)mostPtr = g_StdTreeNilNode;

		this->size = 0;

		{
			void *headPtr = StdMap_GetHead(this);
			*(StdTreeNode **)headPtr = this->head;
		}
		{
			void *rootPtr = StdMap_GetRmost(this);
			*(StdTreeNode **)rootPtr = this->head;
		}

		StdMap_Begin(this, outIter);
		return outIter;
	}

erase_loop:
	while (StdPtrIter_Neq((StdPtrNode **)&beginIter, (StdPtrNode **)&endIter) & 0xFF) {
		void *decResult;

		decResult = StdMap_IterPostInc(&beginIter, &postDec, 0);
		StdMap_TreeErase(this, &eraseOut, *(void **)decResult);
	}
	*(uintptr_t *)outIter = *(uintptr_t *)&beginIter;
	return outIter;
}

/*
 * 0x00422650 - std::map::find implementation
 *
 * Calls StdMap_LowerBound (0x004227B0) to get lower bound iterator. If found !=
 * end, checks if the key at the found position matches (via comparator).
 * If equal, stores found iterator in output. Otherwise stores end.
 */
static void *
StdMap_FindImpl(StdMapTree *this, void *outIter, void *keyPtr)
{
	uintptr_t findResult;
	uintptr_t endResult;
	uintptr_t endResult2;
	void *result;

	StdMap_LowerBound(this, &findResult, keyPtr);

	if (StdPtrIter_Eq((StdPtrNode **)&findResult, (StdPtrNode **)StdPtrList_End((StdPtrList *)this, (StdPtrNode **)&endResult)) & 0xFF) {
		goto store_end;
	}

	{
		StdTreeNode *valPtr = *(StdTreeNode **)&findResult;
		void *nodeKeyPtr = StdTreeNode_Key(valPtr);
		if (StdMap_IntKeyLess(this, keyPtr, nodeKeyPtr) & 0xFF) {
			goto store_end;
		}
	}

	result = &findResult;
	goto done;

store_end:
	result = StdPtrList_End((StdPtrList *)this, (StdPtrNode **)&endResult2);

done:
	*(uintptr_t *)outIter = *(uintptr_t *)result;
	return outIter;
}

/*
 * 0x00422740 - CVector::Destroy4_Range
 *
 * Calls CVector_Destroy6_Single on each 4-byte slot in [first, last).
 */
void
CVector_Destroy4_Range(CVector *this, void *first, void *last)
{
	char *ptr = (char *)first;

	while (ptr != (char *)last) {
		CVector_Destroy6_Single(this, ptr);
		ptr += 4;
	}
}

/*
 * 0x00422770 - No-op comparator identity function
 *
 * Returns arg unchanged. Used as sort comparator when no reordering is
 * needed.
 */
static uintptr_t
SortMulti_NoOp(uintptr_t arg)
{
	return arg;
}

/*
 * 0x00422780 - std::map integer key comparator
 *
 * Returns 1 if *a < *b, 0 otherwise. Used by std::_Tree for integer-keyed
 * std::map insertion/lookup.
 */
static int
StdMap_IntKeyLess(StdMapTree *this, void *a, void *b)
{
	USED(this);
	return *(int *)a < *(int *)b;
}

/*
 * 0x004227A0 - std::_Tree::_Color
 *
 * Returns &_Color(node) - the node's +0x14 slot. _Buynode stores its
 * colour argument through it, the insert rebalance writes 0 and 1, and
 * _Dec tests it against 0.
 */
void *
StdTreeNode_Color(StdTreeNode *node)
{
	return &node->color;
}

/*
 * 0x004227B0 - std::_Tree::find for integer-keyed std::map
 *
 * Calls StdTree_FindInsertPos_Int (0x004234E0) to find the lower bound,
 * then stores the result in the output iterator via CIterCtx_Set. Returns
 * output.
 */
static void *
StdMap_LowerBound(StdMapTree *this, void *outIter, void *keyPtr)
{
	void *node;

	node = StdTree_FindInsertPos_Int(this, keyPtr);
	CIterCtx_Set(outIter, node);
	return outIter;
}

/*
 * 0x004227E0 - std::_Tree::_Erase_Recursive
 *
 * Recursively destroys tree nodes starting from the given node. For each
 * node: recurse on the right child, then follow the left child
 * iteratively. Frees each node after destroying its key/value.
 */
static void
StdMap_EraseRecursive(StdMapTree *this, void *node)
{
	void *nextNode;

	for (;;) {
		if (node == g_StdTreeNilNode)
			return;

		// Recurse on the right child
		{
			void **valPtr = StdPtrNode_GetValue((StdPtrNode *)node);
			StdMap_EraseRecursive(this, *(void **)valPtr);
		}

		// Save the left child, then free the current node
		{
			void *nextPtr = StdPtrNode_GetNext((StdPtrNode *)node);
			nextNode = *(void **)nextPtr;
		}

		{
			void *kvPtr = StdTreeNode_KeyValuePtr((StdTreeNode *)node);
			CVector_Destroy6_Single((CVector *)this, kvPtr);
		}
		free(node);

		node = nextNode;
	}
}

/*
 * 0x00422890 - std::_Tree::_Init
 *
 * If the global sentinel node is NULL, allocates one via StdTree_Buynode
 * and initializes it. Increments the global reference count. Allocates
 * a new head node via StdTree_Buynode and sets up the head/root/most
 * pointers.
 */
static void
StdMap_TreeInit(StdMapTree *this)
{
	void *headNode;

	if (g_StdTreeNilNode == NULL) {
		g_StdTreeNilNode = StdTree_Buynode(this, NULL, 1);
		*(void **)StdPtrNode_GetNext((StdPtrNode *)g_StdTreeNilNode) = NULL;
		*(void **)StdPtrNode_GetValue((StdPtrNode *)g_StdTreeNilNode) = NULL;
	}
	g_StdTreeNilRef++;

	headNode = StdTree_Buynode(this, g_StdTreeNilNode, 0);
	this->head = headNode;
	this->size = 0;

	{
		void *headPtr = StdMap_GetHead(this);
		*(void **)headPtr = this->head;
	}
	{
		void *rootPtr = StdMap_GetRmost(this);
		*(void **)rootPtr = this->head;
	}
}

/*
 * 0x00422970 - std::_Tree::_Insert (RB tree insert with rebalancing)
 *
 * Uses StdPtrNode accessor functions and g_StdTreeNilNode sentinel.
 * Allocates a new node, links it into the tree, performs red-black
 * rebalancing via StdMap_TreeSplice (left rotate) and StdMap_TreeSplice2
 * (right rotate), then stores the result in outIter via CIterCtx_Set.
 */
static void *
StdTree_RBInsert_Wombat(StdMapTree *this, void *outIter, void *addLeft, void *parent, void *pair)
{
	void *node;
	void *uncle;
	void *x;

	node = StdTree_Buynode(this, parent, 0);
	*(void **)StdPtrNode_GetNext((StdPtrNode *)node) = g_StdTreeNilNode;
	*(void **)StdPtrNode_GetValue((StdPtrNode *)node) = g_StdTreeNilNode;
	StdTree_ConstructorPair((StdAllocator *)this, StdTreeNode_KeyValuePtr((StdTreeNode *)node), pair);
	this->size++;

	if (parent == this->head)
		goto link_left;
	if (addLeft != g_StdTreeNilNode)
		goto link_left;
	if (!(StdMap_IntKeyLess(this, (void *)(uintptr_t)SortMulti_NoOp((uintptr_t)pair), StdTreeNode_Key(parent)) & 0xFF))
		goto link_right;

link_left:
	*(void **)StdPtrNode_GetNext((StdPtrNode *)parent) = node;
	if (parent == this->head) {
		*(void **)StdMap_GetRoot(this) = node;
		*(void **)StdMap_GetRmost(this) = node;
	} else {
		if (parent == *(void **)StdMap_GetHead(this))
			*(void **)StdMap_GetHead(this) = node;
	}
	goto start_fixup;

link_right:
	*(void **)StdPtrNode_GetValue((StdPtrNode *)parent) = node;
	if (parent == *(void **)StdMap_GetRmost(this))
		*(void **)StdMap_GetRmost(this) = node;

start_fixup:
	x = node;

	while (x != *(void **)StdMap_GetRoot(this)) {
		void *xParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
		if (*(int *)StdTreeNode_Color(xParent) != 0)
			break;

		xParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
		void *grandparent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)xParent);

		if (xParent == *(void **)StdPtrNode_GetNext((StdPtrNode *)grandparent)) {
			uncle = *(void **)StdPtrNode_GetValue((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))));

			if (*(int *)StdTreeNode_Color(uncle) == 0) {
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Color(uncle) = 1;
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)));
			} else {
				if (x == *(void **)StdPtrNode_GetValue((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) {
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					StdMap_TreeSplice(this, x);
				}
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				StdMap_TreeSplice2(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x))));
			}
		} else {
			uncle = *(void **)StdPtrNode_GetNext((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))));

			if (*(int *)StdTreeNode_Color(uncle) == 0) {
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Color(uncle) = 1;
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)));
			} else {
				if (x == *(void **)StdPtrNode_GetNext((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) {
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					StdMap_TreeSplice2(this, x);
				}
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Color(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				StdMap_TreeSplice(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x))));
			}
		}
	}

	*(int *)StdTreeNode_Color(*(void **)StdMap_GetRoot(this)) = 1;

	CIterCtx_Set(outIter, node);
	return outIter;
}

/*
 * 0x00422E30 - std::_Tree::_Head accessor
 *
 * Returns pointer to the next field of the head node, equivalent to
 * &head->next (the _Left pointer of the MSVC STL header node).
 */
static void *
StdMap_GetHead(StdMapTree *this)
{
	StdTreeNode *head = this->head;
	return StdPtrNode_GetNext((StdPtrNode *)head);
}

/*
 * 0x00422E50 - std::_Tree::_Lrotate
 *
 * Left rotation about node: its right child takes its place, the child's
 * left subtree becomes node's right, and the root or the parent's
 * matching child pointer is retargeted. The node accessors reach _Left,
 * _Parent and _Right, as StdMap_TreeMin describes. The binary brackets
 * the walk in a std::_Lockit critical section, which we omit along with
 * every other lock in the single-threaded port.
 */
static void
StdMap_TreeSplice(StdMapTree *this, void *node)
{
	void *savedChild;
	void *childNextAddr;

	// The right child rises.
	savedChild = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);

	// Its left subtree becomes node's right.
	childNextAddr = StdPtrNode_GetNext((StdPtrNode *)savedChild);
	*(void **)StdPtrNode_GetValue((StdPtrNode *)node) = *(void **)childNextAddr;

	// Reparent that subtree unless it is the nil node.
	if (*(void **)StdPtrNode_GetNext((StdPtrNode *)savedChild) != g_StdTreeNilNode) {
		void *childNext = *(void **)StdPtrNode_GetNext((StdPtrNode *)savedChild);
		*(void **)StdPtrNode_GetPrev((StdPtrNode *)childNext) = node;
	}

	// The child takes node's parent.
	{
		void *nodePrevAddr = StdPtrNode_GetPrev((StdPtrNode *)node);
		void *childPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)savedChild);
		*(void **)childPrevAddr = *(void **)nodePrevAddr;
	}

	// The root or the parent's matching child now points at the child.
	if (node == *(void **)StdMap_GetRoot(this)) {
		*(void **)StdMap_GetRoot(this) = savedChild;
	} else {
		void *parentNode = *(void **)StdPtrNode_GetPrev((StdPtrNode *)node);
		void *parentNextAddr = StdPtrNode_GetNext((StdPtrNode *)parentNode);
		if (node == *(void **)parentNextAddr) {
			*(void **)StdPtrNode_GetNext((StdPtrNode *)parentNode) = savedChild;
		} else {
			*(void **)StdPtrNode_GetValue((StdPtrNode *)parentNode) = savedChild;
		}
	}

	// node becomes the child's left.
	*(void **)StdPtrNode_GetNext((StdPtrNode *)savedChild) = node;
	*(void **)StdPtrNode_GetPrev((StdPtrNode *)node) = savedChild;
}

/*
 * 0x00422FC0 - std::_Tree::_Max (rightmost walk)
 *
 * Walks the tree from the given node following right-child pointers -
 * the +0x08 field, which StdPtrNode_GetValue reaches - until reaching
 * a node whose right child equals the sentinel. Returns the last
 * non-sentinel node found.
 */
static void *
StdMap_TreeMax(void *node)
{
	while (*(void **)StdPtrNode_GetValue((StdPtrNode *)node) != g_StdTreeNilNode) {
		node = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);
	}
	return node;
}

/*
 * 0x00423040 - std::_Tree::_Min (leftmost walk)
 *
 * Walks the tree from the given node following left-child pointers -
 * the +0x00 field, which StdPtrNode_GetNext reaches - until reaching a
 * node whose left child equals the sentinel. Returns the last
 * non-sentinel node found.
 *
 * The three node accessors are COMDAT-folded with std::list's, so
 * GetNext, GetPrev and GetValue reach _Left, _Parent and _Right here.
 *
 * The binary brackets the walk in the std::_Lockit critical-section
 * guard, 0x005C0310 and 0x005C03D0; that is MSVC CRT locking and the
 * server is single-threaded, so it is not reproduced.
 */
static void *
StdMap_TreeMin(void *node)
{
	while (*(void **)StdPtrNode_GetNext((StdPtrNode *)node) != g_StdTreeNilNode) {
		node = *(void **)StdPtrNode_GetNext((StdPtrNode *)node);
	}
	return node;
}

/*
 * 0x004230C0 - std::_Tree::_Rmost
 *
 * Returns &_Right(_Myhead), the header node's +0x08 slot, which holds the
 * rightmost node.
 */
static void *
StdMap_GetRmost(StdMapTree *this)
{
	StdTreeNode *head = this->head;
	return StdPtrNode_GetValue((StdPtrNode *)head);
}

/*
 * 0x004230E0 - std::_Tree::_Rrotate
 *
 * Right rotation about node, the mirror of StdMap_TreeSplice: its left
 * child takes its place. The binary's std::_Lockit bracket is omitted
 * as elsewhere.
 */
static void
StdMap_TreeSplice2(StdMapTree *this, void *node)
{
	void *savedChild;
	void *childValAddr;

	// The left child rises.
	savedChild = *(void **)StdPtrNode_GetNext((StdPtrNode *)node);

	// Its right subtree becomes node's left.
	childValAddr = StdPtrNode_GetValue((StdPtrNode *)savedChild);
	*(void **)StdPtrNode_GetNext((StdPtrNode *)node) = *(void **)childValAddr;

	// Reparent that subtree unless it is the nil node.
	if (*(void **)StdPtrNode_GetValue((StdPtrNode *)savedChild) != g_StdTreeNilNode) {
		void *childVal = *(void **)StdPtrNode_GetValue((StdPtrNode *)savedChild);
		*(void **)StdPtrNode_GetPrev((StdPtrNode *)childVal) = node;
	}

	// The child takes node's parent.
	{
		void *nodePrevAddr = StdPtrNode_GetPrev((StdPtrNode *)node);
		void *childPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)savedChild);
		*(void **)childPrevAddr = *(void **)nodePrevAddr;
	}

	// The root or the parent's matching child now points at the child.
	if (node == *(void **)StdMap_GetRoot(this)) {
		*(void **)StdMap_GetRoot(this) = savedChild;
	} else {
		void *parentNode = *(void **)StdPtrNode_GetPrev((StdPtrNode *)node);
		void *parentValAddr = StdPtrNode_GetValue((StdPtrNode *)parentNode);
		if (node == *(void **)parentValAddr) {
			*(void **)StdPtrNode_GetValue((StdPtrNode *)parentNode) = savedChild;
		} else {
			*(void **)StdPtrNode_GetNext((StdPtrNode *)parentNode) = savedChild;
		}
	}

	// node becomes the child's right.
	*(void **)StdPtrNode_GetValue((StdPtrNode *)savedChild) = node;
	*(void **)StdPtrNode_GetPrev((StdPtrNode *)node) = savedChild;
}

/*
 * 0x00423270 - std::_Tree::iterator::operator++(int)
 *
 * Saves the current node, advances the iterator through 0x00423620, and
 * writes the saved node into *outIter. Returns outIter.
 */
static void *
StdMap_IterPostInc(void *iter, void *outIter, int dummy)
{
	uintptr_t saved;

	USED(dummy);
	saved = *(uintptr_t *)iter;
	StdMap_IterInc(iter);
	*(uintptr_t *)outIter = saved;
	return outIter;
}

/*
 * 0x004232A0 - std::_Tree iterator _Dec wrapper
 *
 * Calls _Dec (0x00423640) to move iterator to predecessor. Returns this.
 */
static void *
StdMap_IterDec(void *iter)
{
	StdMap_TreeDec(iter);
	return iter;
}

/*
 * 0x004234E0 - std::_Tree::_Lbound (find-insert-position, integer-keyed)
 *
 * Walks the red-black tree to find the insertion position for a key.
 * Sets node = root via StdMap_GetRoot+deref, where = head node (tree+4).
 * Loops while node is not the sentinel (g_StdTreeNilNode). If node key <
 * search key, go right (via StdPtrNode_GetValue). Otherwise update where
 * and go left (via StdPtrNode_GetNext). Returns where.
 */
void *
StdTree_FindInsertPos_Int(StdMapTree *tree, int *keyPtr)
{
	void *node;
	void *where;

	node = *(void **)StdMap_GetRoot(tree);
	where = tree->head;

	while (node != g_StdTreeNilNode) {
		if (StdMap_IntKeyLess(tree, StdTreeNode_Key((StdTreeNode *)node), keyPtr) & 0xFF) {
			// Go right
			node = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);
		} else {
			// Update where, go left
			where = node;
			node = *(void **)StdPtrNode_GetNext((StdPtrNode *)node);
		}
	}
	return where;
}

/*
 * 0x004235B0 - std::_Tree::_Buynode
 *
 * Allocates a 0x18-byte tree node via _Charalloc, sets the node's prev
 * (parent) field to parentNode, and sets the node's value to the given
 * value. Returns the new node.
 */
static void *
StdTree_Buynode(StdMapTree *this, void *parentNode, int value)
{
	void *node;

	node = StdPtrList_Charalloc((StdPtrList *)this, sizeof(StdTreeNode));
	*StdPtrNode_GetPrev((StdPtrNode *)node) = (StdPtrNode *)parentNode;
	*(int *)StdTreeNode_Color(node) = value;
	return node;
}

/*
 * 0x00423600 - std::_Tree node construct via allocator
 *
 * Calls StdTree_ConstructorNode to construct a key-value pair at the
 * given node.
 */
static void
StdTree_ConstructorPair(StdAllocator *this, void *node, void *pair)
{
	USED(this);
	StdTree_ConstructorNode(node, pair);
}

/*
 * 0x00423620 - std::_Tree::iterator::operator++
 *
 * Advances the iterator through StdMap_TreeInc and returns this.
 */
static void *
StdMap_IterInc(void *iter)
{
	StdMap_TreeInc(iter);
	return iter;
}

/*
 * 0x00423640 - std::_Tree::_Dec (iterator decrement)
 *
 * Moves the iterator to the predecessor node in the red-black tree. MSVC
 * STL _Dec logic: handles three cases: 1. If node has color==0 and
 * parent's parent == node (header node), moves to header's right
 * (rightmost). 2. If node has a left child (GetNext != sentinel), finds
 * the rightmost node in the left subtree via StdMap_TreeMax. 3. Otherwise,
 * walks up the tree via parent pointers until finding a node that is not a
 * left child of its parent.
 */
static void
StdMap_TreeDec(void *iter)
{
	void **pNode = (void **)iter;
	void *node = *pNode;

	// Case 1: check if header node (color==0 and parent->parent==node)
	if (*(uint32_t *)StdTreeNode_Color(node) == 0) {
		void *parent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)node);
		void *grandparent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)parent);
		if (grandparent == node) {
			// Header node: move to rightmost
			*pNode = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);
			return;
		}
	}

	// Case 2: has left subtree
	{
		void *leftChild = *(void **)StdPtrNode_GetNext((StdPtrNode *)node);
		if (leftChild != g_StdTreeNilNode) {
			*pNode = StdMap_TreeMax(leftChild);
			return;
		}
	}

	// Case 3: walk up via parent pointers
	{
		void *parent;
		for (;;) {
			parent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)node);
			void *parentLeft = *(void **)StdPtrNode_GetNext((StdPtrNode *)parent);
			if (node != parentLeft)
				break;
			*pNode = parent;
			node = parent;
		}
		*pNode = parent;
	}
}

/*
 * 0x004237D0 - std::_Tree::_Root
 *
 * Returns &_Parent(_Myhead) - the header node's +0x04 slot, which holds
 * the tree root. Every caller dereferences it as the root.
 */
static void *
StdMap_GetRoot(StdMapTree *this)
{
	StdTreeNode *head = this->head;
	return StdPtrNode_GetPrev((StdPtrNode *)head);
}

/*
 * 0x004237F0 - std::_Tree::_Inc (tree iterator increment)
 *
 * Advances the iterator to the next in-order node.
 *
 * If the current node has a right child (value != sentinel), descends to
 * the leftmost node of the right subtree via StdMap_TreeMin. Otherwise,
 * walks up via parent pointers while the current node is the right child
 * of its parent. Final guard: if the resulting node's right child does not
 * equal the last parent climbed, store parent.
 */
void
StdMap_TreeInc(void *iter)
{
	void **pNode = (void **)iter;
	void *node;
	void *rightChild;
	void *parent;

	node = *pNode;
	rightChild = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);

	if (rightChild != g_StdTreeNilNode) {
		*pNode = StdMap_TreeMin(rightChild);
		return;
	}

	for (;;) {
		node = *pNode;
		parent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)node);
		if (node != *(void **)StdPtrNode_GetValue((StdPtrNode *)parent))
			break;
		*pNode = parent;
	}

	if (*(void **)StdPtrNode_GetValue((StdPtrNode *)*pNode) != parent)
		*pNode = parent;
}

/*
 * 0x00423900 - std::sort by z-coordinate
 *
 * Sorts entity pointers by z through SortByPred_Main. SortByZ_Compare is
 * the operator() of the one-byte functor the binary passes.
 *
 * Callers: Script_areObjectsOn, CItem_CollectSurfaceItems,
 * CMultiSlave_GetItems.
 */
void
Vector_SortByZ(void *begin, void *end, char typeTag)
{
	int depth;

	USED(typeTag);
	depth = GameCentMon_GetPlayerCount();
	SortByPred_Main((uintptr_t *)begin, (uintptr_t *)end, SortByZ_Compare, depth);
}

/*
 * 0x00423930 - Vector_SortByType
 *
 * Sorts players by bug stat, highest first, through SortByPred_Main.
 * SortByType_Compare is the operator() of the one-byte functor the binary
 * passes.
 */
void
Vector_SortByType(void *begin, void *end, uint8_t typeTag)
{
	int depth;

	USED(typeTag);
	depth = GameCentMon_GetPlayerCount();
	SortByPred_Main((uintptr_t *)begin, (uintptr_t *)end, SortByType_Compare, depth);
}

/*
 * 0x00423960 - CVector element allocator
 *
 * Clamps count to >= 0, then allocates count elements via operator new
 * (0x004E84C0). Returns pointer to allocated buffer. The binary's element
 * is a 32-bit pointer; these vectors hold pointers, so the element is the
 * pointer width.
 */
static void *
Vector_AllocElements(int count)
{
	if (count < 0)
		count = 0;
	return OperatorNew((uint32_t)count * sizeof(uintptr_t));
}

/*
 * 0x00423990 - std::_Tree node construct
 *
 * Calls StdKfn_Identity to allocate 8 bytes of placement storage, then
 * copies the 8-byte key-value pair into it. Returns pointer to constructed
 * storage, or NULL if allocation failed.
 */
static void *
StdTree_ConstructorNode(void *node, void *pair)
{
	void *dest;

	dest = (void *)StdKfn_Identity(8, (uintptr_t)node);
	if (dest != NULL) {
		uintptr_t *src = (uintptr_t *)pair;
		uintptr_t *d = (uintptr_t *)dest;
		d[0] = src[0];
		d[1] = src[1];
		return dest;
	}
	return NULL;
}

/*
 * 0x00423A10 - SortByZ_Main
 * 0x00423AB0 - SortByType_Entry
 * 0x004308C0 - SortByLocation_Main
 * 0x00430950 - SortBySerial_Main
 *
 * std::sort's _Sort_0 for the four sorts whose functor is an empty class:
 * by z, by bug stat, by location and by serial. Up to 16 elements it is
 * an insertion sort, otherwise the quicksort, an insertion sort of the
 * first 16, and an unguarded insert of each element after them. pred
 * stands where the binary passes its one-byte functor and is the
 * comparator that functor's operator() is.
 */
void
SortByPred_Main(uintptr_t *first, uintptr_t *last, SortPred pred, int depth)
{
	uintptr_t *cur;

	USED(depth);
	if ((int)(last - first) <= 16) {
		SortByPred_InsertionEntry(first, last, pred);
		return;
	}
	SortByPred_Quicksort(first, last, pred, 0);
	SortByPred_InsertionEntry(first, first + 16, pred);
	for (cur = first + 16; cur != last; cur++)
		SortByPred_UnguardedInsert(cur, *cur, pred);
}

/*
 * 0x00423AA0 - GameCentMon::GetPlayerCount / std::_Val_type
 *
 * Binary stub: always returns 0. Two honest names at one folded address.
 * BroadcastAll case 0 calls it for the server status packet's player
 * count; the std::sort wrappers call it as MSVC's _Val_type, passing the
 * begin pointer it ignores, and the zero it returns becomes the sorts'
 * fourth argument - which the parameter name depth describes by position
 * rather than by meaning.
 */
int
GameCentMon_GetPlayerCount(void)
{
	return 0;
}

/*
 * 0x00423B90 - SortByZ_InsertionEntry
 * 0x00423D10 - SortByType_InsertionEntry
 * 0x00430AD0 - SortByLocation_InsertionEntry
 * 0x00430CD0 - SortBySerial_InsertionEntry
 *
 * _Insertion_sort: runs the insertion sort with the _Val_type tag.
 */
static void
SortByPred_InsertionEntry(uintptr_t *first, uintptr_t *last, SortPred pred)
{
	int depth;

	depth = GameCentMon_GetPlayerCount();
	SortByPred_Insertion(first, last, pred, depth);
}

/*
 * 0x00423BC0 - SortByZ_Quicksort
 * 0x00423D40 - SortByType_Quicksort
 * 0x00430B00 - SortByLocation_Quicksort
 * 0x00430D00 - SortBySerial_Quicksort
 *
 * _Sort: while more than 16 elements remain, partitions around the median
 * of the first, middle and last elements, recurses on the right part when
 * it is no larger than the left and on the left part otherwise, and loops
 * on the other.
 */
static void
SortByPred_Quicksort(uintptr_t *first, uintptr_t *last, SortPred pred, int depth)
{
	uintptr_t pivot;
	uintptr_t *mid;

	USED(depth);
	while ((int)(last - first) > 16) {
		pivot = SortByPred_Median3(*first, first[(int)(last - first) / 2], *(last - 1), pred);
		mid = SortByPred_Partition(first, last, pivot, pred);
		if ((int)(last - mid) <= (int)(mid - first)) {
			SortByPred_Quicksort(mid, last, pred, GameCentMon_GetPlayerCount());
			last = mid;
		} else {
			SortByPred_Quicksort(first, mid, pred, GameCentMon_GetPlayerCount());
			first = mid;
		}
	}
}

/*
 * 0x00423C90 - SortByZ_UnguardedInsert (pred: SortByZ_Compare)
 * 0x00423E10 - SortByType_MoveBackward (pred: SortByType_Compare)
 * 0x00430BD0 - SortByLocation_LinearInsert (pred: CEntityMap_CompareLocation)
 * 0x00430DD0 - SortBySerial_LinearInsert (pred: CEntityMap_CompareBySerial)
 *
 * _Unguarded_insert: moves val down past every element it sorts before.
 * The element at the front of the range stops it.
 */
static void
SortByPred_UnguardedInsert(uintptr_t *pos, uintptr_t val, SortPred pred)
{
	uintptr_t *prev;

	for (prev = pos; pred(&pred, val, *--prev) & 0xFF; pos = prev)
		*pos = *prev;
	*pos = val;
}

/*
 * 0x00423CE0 - sort comparator
 *
 * Returns 1 when a.z < b.z (ascending z order).
 */
static int
SortByZ_Compare(void *this, uintptr_t a, uintptr_t b)
{
	int zA, zB;

	USED(this);
	zA = CEntity_GetLocation((CEntity *)a)->z;
	zB = CEntity_GetLocation((CEntity *)b)->z;
	return zA < zB;
}

/*
 * 0x00423E60 - sort comparator
 *
 * Returns 1 when GetBugStat(a) > GetBugStat(b) (descending order).
 */
static int
SortByType_Compare(void *this, uintptr_t a, uintptr_t b)
{
	USED(this);
	return CPlayer_GetBugStat((CItem *)a) > CPlayer_GetBugStat((CItem *)b);
}

/*
 * 0x00423F60 - SortByZ_Insertion (pred: SortByZ_Compare)
 * 0x00424140 - SortByType_InsertionSort (pred: SortByType_Compare)
 * 0x00430E80 - SortByLocation_Insertion (pred: CEntityMap_CompareLocation)
 * 0x00431060 - SortBySerial_Insertion (pred: CEntityMap_CompareBySerial)
 *
 * _Insertion_sort_1: inserts each element after the first, moving the
 * whole prefix up when it sorts before the first element.
 */
static void
SortByPred_Insertion(uintptr_t *first, uintptr_t *last, SortPred pred, int depth)
{
	uintptr_t *cur;
	uintptr_t val;

	USED(depth);
	if (first == last)
		return;
	for (cur = first + 1; cur != last; cur++) {
		val = *cur;
		if (!(pred(&pred, val, *first) & 0xFF)) {
			SortByPred_UnguardedInsert(cur, val, pred);
		} else {
			vector_CopyBackward(first, cur, cur + 1);
			*first = val;
		}
	}
}

/*
 * 0x00423FF0 - SortByZ_Median3 (pred: SortByZ_Compare)
 * 0x004241D0 - SortByType_MedianOfThree (pred: SortByType_Compare)
 * 0x00430F10 - SortByLocation_Median3 (pred: CEntityMap_CompareLocation)
 * 0x004310F0 - SortBySerial_Median3 (pred: CEntityMap_CompareBySerial)
 *
 * _Median: the median of a, b and c under pred.
 */
static uintptr_t
SortByPred_Median3(uintptr_t a, uintptr_t b, uintptr_t c, SortPred pred)
{
	if (pred(&pred, a, b) & 0xFF) {
		if (pred(&pred, b, c) & 0xFF)
			return b;
		if (pred(&pred, a, c) & 0xFF)
			return c;
		return a;
	}
	if (pred(&pred, a, c) & 0xFF)
		return a;
	if (pred(&pred, b, c) & 0xFF)
		return c;
	return b;
}

/*
 * 0x004240C0 - SortByZ_Partition (pred: SortByZ_Compare)
 * 0x004242A0 - SortByType_Partition (pred: SortByType_Compare)
 * 0x00430FE0 - SortByLocation_Partition (pred: CEntityMap_CompareLocation)
 * 0x004311C0 - SortBySerial_Partition (pred: CEntityMap_CompareBySerial)
 *
 * _Unguarded_partition: Hoare partition around pivot. Returns the first
 * element of the right part.
 */
static uintptr_t *
SortByPred_Partition(uintptr_t *first, uintptr_t *last, uintptr_t pivot, SortPred pred)
{
	for (;; first++) {
		while (pred(&pred, *first, pivot) & 0xFF)
			first++;
		do
			last--;
		while (pred(&pred, pivot, *last) & 0xFF);
		if (last <= first)
			return first;
		vector_SwapWrapper(first, last);
	}
}

/*
 * 0x00424340 - iter_swap implementation
 *
 * Swaps the uint32_t values at two pointers.
 */
static void
vector_SwapImpl(uintptr_t *a, uintptr_t *b)
{
	uintptr_t tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

/*
 * 0x00424D30 - StdPtrList_Init_PendingReturn
 * 0x00426C00 - StdPtrList_Init_PendingFree
 * 0x00457BB0 - StdPtrList_Init_EntityMgr
 * 0x0045AAB0 - StdPtrList_Init_Login
 * 0x00461FB0 - StdPtrList_Init_MobileMap
 * 0x004849C0 - StdPtrList_Init_NPCMap
 *
 * std::list<void*>::list, one body for the six lists of pointers: the
 * deferred script-node returns and tag frees, the entity manager list and
 * the ItemMap blocks, the login scripts, and the MobileMap and NPCMap
 * blocks. Copies the allocator byte from *init, allocates the sentinel
 * through StdPtrList_Buynode, and empties the list. Returns this.
 */
StdPtrList *
StdPtrList_Init(StdPtrList *list, void *init)
{
	*(uint8_t *)list = *(uint8_t *)init;
	list->head = StdPtrList_Buynode(list, NULL, NULL);
	list->size = 0;

	return list;
}

/*
 * 0x00424D70 - StdPtrList_Clear_PendingReturn
 * 0x00426C40 - StdPtrList_Clear_PendingFree
 * 0x00457BF0 - StdPtrList_Clear_EntityMgr
 * 0x0045AAF0 - StdPtrList_Clear_Login
 * 0x00461FF0 - StdPtrList_Clear_MobileMap
 * 0x00484A00 - StdPtrList_Clear_NPCMap
 *
 * std::list<void*>::~list: erases every element through
 * StdPtrList_EraseRange, frees the sentinel, and zeroes head and size.
 */
void
StdPtrList_Clear(StdPtrList *list)
{
	StdPtrNode *endIter, *beginIter, *result;

	StdPtrList_End(list, &endIter);
	StdPtrList_Begin(list, &beginIter);
	StdPtrList_EraseRange(list, &result, beginIter, endIter);
	free(list->head);
	list->head = NULL;
	list->size = 0;
}

/*
 * 0x00424DD0 - StdPtrList_PushBack_PendingReturn
 * 0x00426CD0 - StdPtrList_PushBack_PendingFree
 * 0x00457C50 - StdPtrList_PushBack_EntityMgr
 * 0x0045AB50 - StdPtrList_PushBack_Login
 * 0x00462070 - StdPtrList_PushBack_MobileMap
 * 0x00484A60 - StdPtrList_PushBack_NPCMap
 *
 * std::list<void*>::push_back: inserts value before end().
 */
void
StdPtrList_PushBack(StdPtrList *list, void *value)
{
	StdPtrNode *endIter;
	StdPtrNode *result;

	StdPtrList_End(list, &endIter);
	StdPtrList_DoInsert(list, &result, endIter, value);
}

/*
 * 0x00424E00 - std::list<void*>::erase
 *
 * Erases the node at pos. Unlinks prev/next, destroys the stored value
 * via CVector_Destroy6_Single (no-op for pointer elements), frees the
 * node, decrements size. Stores iterator to next element in *result.
 */
void
StdPtrList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos)
{
	StdPtrNode *nextNode;

	nextNode = pos->next;
	pos->prev->next = pos->next;
	pos->next->prev = pos->prev;
	CVector_Destroy6_Single((CVector *)list, StdPtrNode_GetValue(pos));
	free(pos);
	list->size--;
	*result = nextNode;
}

/*
 * 0x00424EC0 - StdPtrList_DoInsert_PendingReturn
 * 0x00426D00 - StdPtrList_DoInsert_PendingFree
 * 0x00457CC0 - StdPtrList_DoInsert_EntityMgr
 * 0x0045AB80 - StdPtrList_DoInsert_Login
 * 0x004620A0 - StdPtrList_DoInsert_MobileMap
 * 0x00484A90 - StdPtrList_DoInsert_NPCMap
 *
 * std::list<void*>::_Insert: inserts value before pos, the iterator the
 * caller passes by value. Allocates the node through StdPtrList_Buynode,
 * links it in front of pos, copies the value through
 * StdPtrList_DoInsert_424FC0, increments the size, and stores the new
 * node in *result, which it returns. The binary reads pos through the
 * [this+0] getter at 0x00421000, which our C types as returning int, so
 * pos is used as passed.
 *
 * _Insert takes the value by const reference, a pointer in the x86 ABI,
 * and StdPtrList_DoInsert_4CF610 reads the value through it. Our callers
 * pass the value itself, so the reference is made here by passing &value
 * on.
 */
StdPtrNode **
StdPtrList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *node;
	StdPtrNode *newNode;

	newNode = StdPtrList_Buynode(list, pos, *StdPtrNode_GetPrev(pos));
	*StdPtrNode_GetPrev(pos) = newNode;
	node = *StdPtrNode_GetPrev(pos);
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;

	StdPtrList_DoInsert_424FC0(list, StdPtrNode_GetValue(node), &value);

	list->size = list->size + 1;
	CIterCtx_Set(result, node);
	return result;
}

/*
 * 0x00424F70 - StdPtrList_EraseRange_PendingReturn
 * 0x00426DB0 - StdPtrList_EraseRange_PendingFree
 * 0x00457D70 - StdPtrList_EraseRange_EntityMgr
 * 0x0045AC30 - StdPtrList_EraseRange_Login
 * 0x00462150 - StdPtrList_EraseRange_MobileMap
 * 0x00484B40 - StdPtrList_EraseRange_NPCMap
 *
 * std::list<void*>::erase(first, last): while first differs from last,
 * steps first past its node and erases that node. Stores first, equal to
 * last by then, in *result and returns result.
 */
StdPtrNode **
StdPtrList_EraseRange(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;
	StdPtrNode *eraseResult;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc_484990(&first, &postIncTemp, 0);
		StdPtrList_Erase(list, &eraseResult, *(StdPtrNode **)&postIncTemp);
	}
	*result = first;
	return result;
}

/*
 * 0x00424FC0 - StdPtrList_DoInsert (first inlined copy)
 *
 * Forwards to the real implementation at 0x004CF610 (the second copy).
 */
void
StdPtrList_DoInsert_424FC0(StdPtrList *this, void *dest, void *source)
{
	USED(this);
	StdPtrList_DoInsert_4CF610(dest, source);
}

/*
 * 0x00426440 - CVector::CVector wrapper
 *
 * Wrapper that passes a local (uninitialized) type byte pointer to
 * CVector_Constructor and returns this. The default constructor closure
 * at 0x004A6470 makes the same call and returns void. The array
 * constructions in CMobile::SendUpdateToList and CRegionGrid::CRegionGrid
 * pass it to the CRT's eh vector constructor iterator, and our loops
 * call CVector_Constructor directly, as for the list closures.
 */
void *
CVector_ConstructorWrapper(CVector *this)
{
	char typeByte = 0;
	CVector_Constructor(this, &typeByte);
	return this;
}

/*
 * 0x00426460 - CVector::CVector (CVector constructor)
 *
 * Initializes an MSVC STL vector-like structure: list->type = *typeFlag (1
 * byte) list->begin = NULL list->end = NULL list->capacity = NULL
 */
CVector *
CVector_Constructor(CVector *list, const char *typeFlag)
{
	list->type = *typeFlag;
	list->begin = NULL;
	list->end = NULL;
	list->capacity = NULL;

	return list;
}

/*
 * 0x004264A0 - CVector::~CVector (CVector destructor)
 *
 * Calls destroy range (0x00422740) on [begin, end) which iterates elements
 * calling 0x00479FF0 -> 0x0045ACC0 (empty function). Then calls deallocate
 * (0x0046C9E0) which calls operator delete (0x004E8110 -> free) on the
 * buffer. Zeros out begin, end, capacity.
 */
void
CVector_Destructor(CVector *list)
{
	CVector_Destroy4_Range(list, list->begin, list->end);
	free(list->begin);

	list->begin = NULL;
	list->end = NULL;
	list->capacity = NULL;
}

/*
 * 0x00426820 - std::copy_backward (uint32_t)
 *
 * Copies [first, last) backward into memory ending at dest_end. Returns
 * pointer to the first copied destination element (dest_end - (last -
 * first)).
 */
void *
vector_CopyBackward(void *first, void *last, void *dest_end)
{
	uintptr_t *s = last;
	uintptr_t *b = first;
	uintptr_t *d = dest_end;

	while (s != b) {
		s--;
		d--;
		*d = *s;
	}
	return d;
}

/*
 * 0x00426CA0 - std::list<void*>::begin
 *
 * Returns iterator to first element. Stores head->next (first real node or
 * sentinel if empty) into *outIter. Returns outIter.
 */
StdPtrNode **
StdPtrList_Begin(StdPtrList *list, StdPtrNode **outIter)
{
	*outIter = list->head->next;
	return outIter;
}

/*
 * 0x00426E00 - std::list<void*>::iterator::operator==
 *
 * Compares two iterators (node pointers) for equality.
 */
int
StdPtrIter_Eq(StdPtrNode **a, StdPtrNode **b)
{
	return *a == *b;
}

/*
 * 0x004301A0 - CVector::GetCount
 *
 * Returns element count: (end - begin) / 4, or 0 if begin is NULL.
 */
uint32_t
CVector_GetCount(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uintptr_t *)list->end - (uintptr_t *)list->begin);
}

/*
 * 0x0044D710 - CFragment::`scalar deleting destructor'
 *
 * Runs CFragment_Destroy and frees the memory when flags & 1. Returns
 * this.
 */
void *
CFragment_ScalarDelete(CFragment *this, int flags)
{
	CFragment_Destroy(this);

	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x0044D740 - CDefine::`scalar deleting destructor'
 *
 * Runs CDefine_Destructor and frees the memory when flags & 1. Returns
 * this.
 */
void *
CDefine_ScalarDelete(CDefine *this, int flags)
{
	CDefine_Destructor(this);

	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x0044D770 - std::list<void*>::iterator::operator++(int)
 *
 * Post-increment. Saves current iterator value, calls _Inc to advance,
 * copies old value to outIter via copy constructor. Returns outIter. dummy
 * parameter is C++ post-increment overload disambiguator.
 * StdPtrIter_PostInc_484990 is the second instantiation, with the copy
 * constructor inlined.
 */
StdPtrNode **
StdPtrIter_PostInc(StdPtrNode **iter, StdPtrNode **outIter, int dummy)
{
	StdPtrNode *saved;

	USED(dummy);
	saved = *iter;
	StdPtrIter_Inc(iter);
	StdPtrIter_CopyConstructor(outIter, &saved);
	return outIter;
}

/*
 * 0x0044F710 - std::list node _Prevnode accessor
 *
 * Returns pointer to node's prev field. Used by _Insert, _Buynode, and
 * many other STL sites.
 */
StdPtrNode **
StdPtrNode_GetPrev(StdPtrNode *node)
{
	return &node->prev;
}

/*
 * 0x0044F720 - std::list node _Myval accessor
 *
 * Returns pointer to node's value field.
 */
void **
StdPtrNode_GetValue(StdPtrNode *node)
{
	return &node->value;
}

/*
 * 0x0044F750 - std::list allocator _Charalloc
 *
 * Allocator wrapper: forwards to the clamping operator new.
 */
void *
StdPtrList_Charalloc(StdPtrList *list, uint32_t size)
{
	USED(list);
	return OperatorNew_Clamp(size, NULL);
}

/*
 * 0x00457F90 - std::sort raw uint32_t
 *
 * Sort entry point for CVector of raw uint32_t values. Calls
 * GameCentMon_GetPlayerCount (always returns 0) for depth limit, then
 * delegates to the main sort function.
 *
 * Callers: CPlayerList_BroadcastToTwoLocs (x2).
 */
void
Vector_SortRaw(void *begin, void *end)
{
	int depth = GameCentMon_GetPlayerCount();
	SortRaw_Main((uintptr_t *)begin, (uintptr_t *)end, depth);
}

/*
 * 0x00457FC0 - std::sort main for raw uint32_t
 *
 * If count <= 16, insertion sort directly. Otherwise, quicksort loop to
 * partitions of <=16 elements, then insertion sort the first 16, then
 * unguarded linear insertion on the rest.
 */
static void
SortRaw_Main(uintptr_t *begin, uintptr_t *end, int depth)
{
	int count;
	uintptr_t *cur;

	USED(depth);
	count = (int)(end - begin);
	if (count <= 16) {
		SortRaw_InsertionEntry(begin, end);
		return;
	}
	SortRaw_Quicksort(begin, end, 0);
	SortRaw_InsertionEntry(begin, begin + 16);
	cur = begin + 16;
	while (cur != end) {
		SortRaw_UnguardedInsert(cur, *cur);
		cur++;
	}
}

/*
 * 0x00458040 - std::sort insertion entry for raw uint32_t
 *
 * Wrapper for insertion sort. Calls GameCentMon_GetPlayerCount (returns 0)
 * for depth, then delegates to the actual insertion sort.
 */
static void
SortRaw_InsertionEntry(uintptr_t *begin, uintptr_t *end)
{
	int depth = GameCentMon_GetPlayerCount();
	SortRaw_Insertion(begin, end, depth);
}

/*
 * 0x00458070 - std::sort quicksort loop for raw uint32_t
 *
 * Quicksort with median-of-three pivot. Recurses on smaller half, iterates
 * on larger. Stops at partitions of <=16 elements.
 */
static void
SortRaw_Quicksort(uintptr_t *begin, uintptr_t *end, int depth)
{
	uintptr_t pivot;
	uintptr_t *mid;
	int rightCount, leftCount;

	USED(depth);
	for (;;) {
		if ((int)(end - begin) <= 16)
			return;
		pivot = SortRaw_Median3(*begin, begin[(int)(end - begin) / 2], *(end - 1));
		mid = SortRaw_Partition(begin, end, pivot);
		rightCount = (int)(end - mid);
		leftCount = (int)(mid - begin);
		if (rightCount > leftCount) {
			SortRaw_Quicksort(begin, mid, GameCentMon_GetPlayerCount());
			begin = mid;
		} else {
			SortRaw_Quicksort(mid, end, GameCentMon_GetPlayerCount());
			end = mid;
		}
	}
}

/*
 * 0x00458130 - std::sort unguarded linear insert for raw uint32_t
 *
 * Shifts elements right from pos while val < preceding element (unsigned
 * comparison), then places val at the correct position.
 */
static void
SortRaw_UnguardedInsert(uintptr_t *pos, uintptr_t val)
{
	uintptr_t *cur = pos;

	for (;;) {
		cur--;
		if (val >= *cur)
			break;
		*pos = *cur;
		pos = cur;
	}
	*pos = val;
}

/*
 * 0x00458170 - std::sort insertion sort for raw uint32_t
 *
 * Walks forward from begin+1. If new element < first (unsigned), shifts
 * all elements right via CopyBackward and places it at begin. Otherwise,
 * uses unguarded linear insert.
 */
static void
SortRaw_Insertion(uintptr_t *begin, uintptr_t *end, int depth)
{
	uintptr_t *cur;
	uintptr_t saved;

	USED(depth);
	if (begin == end)
		return;
	cur = begin;
	for (;;) {
		cur++;
		if (cur == end)
			return;
		saved = *cur;
		if (saved < *begin) {
			vector_CopyBackward(begin, cur, cur + 1);
			*begin = saved;
		} else {
			SortRaw_UnguardedInsert(cur, saved);
		}
	}
}

/*
 * 0x004581E0 - std::sort median of three for raw uint32_t
 *
 * Returns the median value of three uint32_t values using unsigned
 * comparison for pivot selection.
 */
static uintptr_t
SortRaw_Median3(uintptr_t a, uintptr_t b, uintptr_t c)
{
	if (a < b) {
		if (b < c)
			return b;
		else if (a < c)
			return c;
		else
			return a;
	} else {
		if (a < c)
			return a;
		else if (b < c)
			return c;
		else
			return b;
	}
}

/*
 * 0x00458260 - std::sort partition for raw uint32_t
 *
 * Hoare partition scheme with unsigned comparison. Scans forward for
 * elements >= pivot, backward for elements <= pivot, swaps them. Returns
 * partition point.
 */
static uintptr_t *
SortRaw_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot)
{
	for (;;) {
		while (*begin < pivot)
			begin++;
		end--;
		while (pivot < *end)
			end--;
		if (end <= begin)
			return begin;
		vector_SwapWrapper(begin, end);
		begin++;
	}
}

/*
 * 0x004582C0 - SortRaw_SkipN
 *
 * Advances a char pointer up to count bytes while bytes are non-null.
 * Returns the number of bytes actually advanced (offset from start). Used
 * by CRT _output formatting engine.
 */
static int __attribute__((unused))
SortRaw_SkipN(const char *str, int count)
{
	const char *p = str;
	int n;

	for (;;) {
		n = count;
		count = count - 1;
		if (n == 0)
			break;
		if (*(signed char *)p == 0)
			break;
		p++;
	}
	return (int)(p - str);
}

/*
 * 0x00458854 - MSVC CRT _atoi helper
 *
 * Parses decimal digits from the string at *pStr, accumulating value =
 * value * 10 + (ch - '0'). Advances *pStr past all digits. Returns the
 * parsed integer. Called from CRT_output.
 */
static __attribute__((unused)) int
CRT_atoi(char **pStr)
{
	int value = 0;

	for (;;) {
		char ch = **pStr;
		if (!isdigit((unsigned char)ch))
			break;
		value = value * 10 + (ch - '0');
		*pStr = *pStr + 1;
	}
	return value;
}

/*
 * 0x00458EF0 - MSVC CRT divmod helper
 *
 * Performs unsigned division of *pValue by divisor. Stores quotient back
 * in *pValue, returns remainder. Called from CRT_output_number for digit
 * extraction.
 */
static __attribute__((unused)) uint32_t
CRT_divmod(uint32_t *pValue, uint32_t divisor)
{
	uint32_t remainder;
	uint32_t val;

	val = *pValue;
	remainder = val % divisor;
	*pValue = val / divisor;
	return remainder;
}

/*
 * 0x00458F20 - CFileEntry::CFileEntry
 *
 * Allocates strlen+1 bytes, copies name string, stores fields at +4, +8,
 * +0xC. Returns this.
 */
static __attribute__((unused)) void *
CFileEntry_Constructor(CFileEntry *self, const char *name, int nameLength, int fileOffset, int fileSize)
{
	char *buf;
	int len;

	self->nameLength = nameLength;
	self->fileOffset = fileOffset;
	self->fileSize = fileSize;

	len = strlen(name);
	buf = (char *)OperatorNew(len + 1);
	self->name = buf;
	strcpy(buf, name);

	return self;
}

/*
 * 0x00458F80 - CFileEntry::~CFileEntry
 *
 * Frees the entry's name string if non-null.
 */
void
CFileEntry_Destructor(CFileEntry *self)
{
	char *str = self->name;

	if (str != NULL)
		OperatorDelete(str);
}

/*
 * 0x00458FB0 - std::list<CFileEntry>::_Init
 * 0x004845E0 - StdPtrList16_Constructor
 *
 * Copies the allocator byte from init, allocates the sentinel through
 * StdFileList_Buynode, and leaves the list empty. Returns this. The
 * CFileEntry and CString lists share it: both elements are 16 bytes, so
 * one node size serves both.
 */
void *
StdPtrList16_Constructor(StdPtrList *this, const void *init)
{
	*(uint8_t *)this = *(const uint8_t *)init;
	this->head = StdFileList_Buynode(this, NULL, NULL);
	this->size = 0;
	return this;
}

/*
 * 0x00458FF0 - std::list<CFileEntry>::push_back
 *
 * Calls StdPtrList_End to get sentinel, dereferences it, then calls
 * _Insert to insert before sentinel.
 */
static __attribute__((unused)) void
StdFileList_PushBack(StdPtrList *list, void *value)
{
	StdPtrNode *endIter;
	StdPtrNode *insertResult;

	StdPtrList_End(list, &endIter);
	StdFileList_DoInsert(list, &insertResult, endIter, value);
}

/*
 * 0x00459020 - std::list<CFileEntry>::erase
 *
 * Post-increments iterator to capture node, unlinks prev/next via accessor
 * calls, calls _Destroy on value, frees node, decrements size. Stores
 * iterator to next element in *result.
 */
static __attribute__((unused)) void
StdFileList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos)
{
	StdPtrNode *node;
	StdPtrNode *postIncTemp;

	// PostInc(&pos, &postIncTemp, 0) + CSearchCtx_Find
	postIncTemp = pos;
	StdPtrIter_Inc(&pos);
	node = postIncTemp;

	// Unlink: node->prev->next = node->next
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = StdPtrNode_GetNext(node)->next;

	// Unlink: node->next->prev = node->prev
	*StdPtrNode_GetPrev(StdPtrNode_GetNext(node)->next) = *StdPtrNode_GetPrev(node);

	StdFileList_Destroy(list, StdPtrNode_GetValue(node));
	free(node);
	list->size = list->size - 1;
	*result = pos;
}

/*
 * 0x004590E0 - std::list<CFileEntry>::_Insert
 *
 * Allocates new node via _Buynode, links before pos, copies value via
 * ConstructWrapper (0x004591B0), increments size, stores result via
 * CIterCtx_Set.
 */
static void
StdFileList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *node;
	StdPtrNode *newNode;

	node = pos;

	newNode = StdFileList_Buynode(list, node, *StdPtrNode_GetPrev(node));
	*StdPtrNode_GetPrev(node) = newNode;
	node = *StdPtrNode_GetPrev(node);
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;

	StdFileList_ConstructorWrapper(list, StdPtrNode_GetValue(node), value);

	list->size = list->size + 1;
	CIterCtx_Set(result, node);
}

/*
 * 0x00459190 - std::list<CFileEntry>::_Destroy
 *
 * Calls FileEntry_DestructorWrapper (0x004591D0) on the element.
 */
static void
StdFileList_Destroy(void *list, void *element)
{
	USED(list);
	FileEntry_DestructorWrapper(element);
}

/*
 * 0x004591B0 - CFileEntry::_ConstructorWrapper
 *
 * Delegates to Construct (0x004591E0).
 */
static void
StdFileList_ConstructorWrapper(void *list, void *dst, void *src)
{
	USED(list);
	FileEntry_Constructor(dst, src);
}

/*
 * 0x004591D0 - FileEntry_DestructorWrapper
 *
 * Thunk that calls ScalarDelete with flags=0.
 */
static void *
FileEntry_DestructorWrapper(void *entry)
{
	return CFileEntry_ScalarDelete((CFileEntry *)entry, 0);
}

/*
 * 0x004591E0 - FileEntry_Constructor
 *
 * Allocates the destination (16 bytes) and copy-constructs a CFileEntry from
 * src into it. Returns NULL when allocation fails.
 */
static void *
FileEntry_Constructor(void *dst, void *src)
{
	void *ptr;

	ptr = (void *)StdKfn_Identity(0x10, (uintptr_t)dst);
	if (ptr == NULL)
		return NULL;
	return CFileEntry_CopyConstructor((CFileEntry *)ptr, (CFileEntry *)src);
}

/*
 * 0x00459220 - CFileEntry::CFileEntry (copy constructor)
 *
 * Allocates strlen+1, copies string from source->name, copies fields at
 * +4, +8, +0xC. Returns this.
 */
static void *
CFileEntry_CopyConstructor(CFileEntry *self, CFileEntry *src)
{
	int len;
	char *buf;

	len = strlen(src->name);
	buf = (char *)OperatorNew(len + 1);
	self->name = buf;
	strcpy(buf, src->name);

	self->nameLength = src->nameLength;
	self->fileOffset = src->fileOffset;
	self->fileSize = src->fileSize;

	return self;
}

/*
 * 0x00459290 - CFileEntry::ScalarDelete
 *
 * Calls destructor (0x00458F80) to free string, then if flags & 1, frees
 * the memory. Returns this.
 */
static void *
CFileEntry_ScalarDelete(CFileEntry *self, int flags)
{
	CFileEntry_Destructor(self);

	if (flags & 1)
		OperatorDelete(self);
	return self;
}

/*
 * 0x0045AC80 - std::list<void*>::_Next
 *
 * Identity function. Returns node unchanged. In MSVC STL, _Next returns a
 * reference to the node's _Next field; since _Next is at offset 0,
 * returning the node pointer IS returning &(node->next).
 */
StdPtrNode *
StdPtrNode_GetNext(StdPtrNode *node)
{
	return node;
}

/*
 * 0x0045AC90 - std::list<void*>::iterator::_Inc
 *
 * Pre-increment internal. Gets current node, calls _Next(node) to get
 * reference to next field, reads through it to advance iterator.
 */
StdPtrNode **
StdPtrIter_Inc(StdPtrNode **iter)
{
	StdPtrNode *node;
	StdPtrNode *ref;

	node = *iter;
	ref = StdPtrNode_GetNext(node);
	*iter = ref->next;

	return iter;
}

/*
 * 0x00462050 - std::list<void*>::end
 *
 * Returns iterator to past-the-end (sentinel node). Stores head (sentinel)
 * into *outIter. Returns outIter.
 */
StdPtrNode **
StdPtrList_End(StdPtrList *list, StdPtrNode **outIter)
{
	*outIter = list->head;
	return outIter;
}

/*
 * 0x00462390 - std::sort by distance for pair arrays
 *
 * Sort entry called from ProcessCrimeWitness. The functor holds refLoc
 * and is passed by value: the binary copies it through
 * CLocation_CopyFromSrc before every call that takes it, and runs its
 * destructor on every exit, the empty function at 0x00461F80 that
 * CResBankMagicCtx_PostInit shares. _Val_type runs before the copy.
 */
void
Vector_SortByDistPair(uintptr_t *begin, uintptr_t *end, CLocation refLoc)
{
	int depth;
	CLocation arg;

	depth = GameCentMon_GetPlayerCount();
	CLocation_CopyFromSrc(&arg, &refLoc);
	SortByDist_Main(begin, end, arg, depth);
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x004625D0 - std::sort by distance for pair arrays (entry)
 *
 * The distance sort's entry called from CombatManager_SpawnGuards, the
 * same code as Vector_SortByDistPair at its own address.
 */
void
Vector_SortByDistPairEntry(uintptr_t *begin, uintptr_t *end, CLocation refLoc)
{
	int depth;
	CLocation arg;

	depth = GameCentMon_GetPlayerCount();
	CLocation_CopyFromSrc(&arg, &refLoc);
	SortByDist_Main(begin, end, arg, depth);
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x004626A0 - std::list<void*>::iterator::operator*
 *
 * Dereferences iterator to get pointer to stored value. Returns
 * &(node->value).
 */
void **
StdPtrIter_Deref(StdPtrNode **iter)
{
	return &(*iter)->value;
}

/*
 * 0x00462CA0 - SortByDist_Main_Witness
 * 0x00462DB0 - SortByDist_Main_Guards
 * 0x0047F3F0 - SortByDist_Main_Broadcast
 *
 * _Sort_0: insertion sort alone up to 16 elements, otherwise the
 * quicksort, an insertion sort of the first 16 and an unguarded insert of
 * each element after them. Each call takes its own copy of refLoc.
 */
static void
SortByDist_Main(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
{
	CLocation arg;

	USED(depth);
	if ((int)(end - begin) <= 16) {
		CLocation_CopyFromSrc(&arg, &refLoc);
		SortByDist_InsertionEntry(begin, end, arg);
	} else {
		CLocation_CopyFromSrc(&arg, &refLoc);
		SortByDist_Quicksort(begin, end, arg, 0);
		CLocation_CopyFromSrc(&arg, &refLoc);
		SortByDist_InsertionEntry(begin, begin + 16, arg);
		for (begin += 16; begin != end; begin++) {
			CLocation_CopyFromSrc(&arg, &refLoc);
			SortByDist_UnguardedInsert(begin, *begin, arg);
		}
	}
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x00462EC0 - SortByDist_InsertionEntry_Witness
 * 0x00463110 - SortByDist_InsertionEntry_Guards
 * 0x0047F500 - SortByDist_InsertionEntry_Broadcast
 *
 * Wrapper. Reads the unused depth limit from GameCentMon_GetPlayerCount
 * and delegates to SortByDist_Insertion.
 */
static void
SortByDist_InsertionEntry(uintptr_t *begin, uintptr_t *end, CLocation refLoc)
{
	int depth;
	CLocation arg;

	depth = GameCentMon_GetPlayerCount();
	CLocation_CopyFromSrc(&arg, &refLoc);
	SortByDist_Insertion(begin, end, arg, depth);
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x00462F30 - SortByDist_Quicksort_Witness
 * 0x00463180 - SortByDist_Quicksort_Guards
 * 0x0047F570 - SortByDist_Quicksort_Broadcast
 *
 * _Sort: while more than 16 elements remain, partitions around the median
 * of the first, middle and last elements, recurses on the right part when
 * it is no larger than the left and on the left part otherwise, and loops
 * on the other. The partition's copy of refLoc is made before the
 * median's.
 */
static void
SortByDist_Quicksort(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
{
	CLocation partArg, medArg, arg;
	uintptr_t pivot;
	uintptr_t *mid;
	int valType;

	USED(depth);
	while ((int)(end - begin) > 16) {
		CLocation_CopyFromSrc(&partArg, &refLoc);
		CLocation_CopyFromSrc(&medArg, &refLoc);
		pivot = SortByDist_Median3(*begin, begin[(int)(end - begin) / 2], *(end - 1), medArg);
		mid = SortByDist_Partition(begin, end, pivot, partArg);
		if ((int)(end - mid) <= (int)(mid - begin)) {
			valType = GameCentMon_GetPlayerCount();
			CLocation_CopyFromSrc(&arg, &refLoc);
			SortByDist_Quicksort(mid, end, arg, valType);
			end = mid;
		} else {
			valType = GameCentMon_GetPlayerCount();
			CLocation_CopyFromSrc(&arg, &refLoc);
			SortByDist_Quicksort(begin, mid, arg, valType);
			begin = mid;
		}
	}
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x00463080 - SortByDist_UnguardedInsert_Witness
 * 0x004632D0 - SortByDist_UnguardedInsert_Guards
 * 0x0047F6C0 - SortByDist_UnguardedInsert_Broadcast
 *
 * Shifts elements right from pos while val is closer than the preceding
 * element, then places val at correct position.
 */
static void
SortByDist_UnguardedInsert(uintptr_t *pos, uintptr_t val, CLocation refLoc)
{
	uintptr_t *prev;

	for (prev = pos; SortByDist_Compare(&refLoc, val, *--prev) & 0xFF; pos = prev)
		*pos = *prev;
	*pos = val;
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x00463360 - sort comparator by distance
 *
 * Compares two entity pointers by Chebyshev distance from a reference
 * location (ascending). Returns 1 if dist(a) < dist(b).
 */
static int
SortByDist_Compare(CLocation *refLoc, uintptr_t a, uintptr_t b)
{
	int distA, distB;

	distA = Location_WrappedChebyshevDistance(refLoc, CEntity_GetLocation((CEntity *)a));
	distB = Location_WrappedChebyshevDistance(refLoc, CEntity_GetLocation((CEntity *)b));
	return distA < distB;
}

/*
 * 0x004633B0 - SortByDist_Insertion_Witness
 * 0x00463660 - SortByDist_Insertion_Guards
 * 0x0047F750 - SortByDist_Insertion_Broadcast
 *
 * Standard insertion sort for entity pointer arrays. If new element is
 * closer than first, shifts all right via CopyBackward. Otherwise uses
 * unguarded insert.
 */
static void
SortByDist_Insertion(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
{
	CLocation arg;
	uintptr_t *cur;
	uintptr_t val;

	USED(depth);
	if (begin != end) {
		for (cur = begin + 1; cur != end; cur++) {
			val = *cur;
			if (!(SortByDist_Compare(&refLoc, val, *begin) & 0xFF)) {
				CLocation_CopyFromSrc(&arg, &refLoc);
				SortByDist_UnguardedInsert(cur, val, arg);
			} else {
				vector_CopyBackward(begin, cur, cur + 1);
				*begin = val;
			}
		}
	}
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x00463480 - SortByDist_Median3_Witness
 * 0x00463730 - SortByDist_Median3_Guards
 * 0x0047F820 - SortByDist_Median3_Broadcast
 *
 * Returns the median of a, b and c by distance from refLoc. Each of its
 * two returns runs the functor's destructor first.
 */
static uintptr_t
SortByDist_Median3(uintptr_t a, uintptr_t b, uintptr_t c, CLocation refLoc)
{
	uintptr_t r;

	if (SortByDist_Compare(&refLoc, a, b) & 0xFF) {
		if (SortByDist_Compare(&refLoc, b, c) & 0xFF)
			r = b;
		else if (SortByDist_Compare(&refLoc, a, c) & 0xFF)
			r = c;
		else
			r = a;
		CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
		return r;
	}
	if (SortByDist_Compare(&refLoc, a, c) & 0xFF)
		r = a;
	else if (SortByDist_Compare(&refLoc, b, c) & 0xFF)
		r = c;
	else
		r = b;
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
	return r;
}

/*
 * 0x004635A0 - SortByDist_Partition_Witness
 * 0x00463850 - SortByDist_Partition_Guards
 * 0x0047F940 - SortByDist_Partition_Broadcast
 *
 * Hoare partition using distance comparator.
 */
static uintptr_t *
SortByDist_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation refLoc)
{
	for (;; begin++) {
		while (SortByDist_Compare(&refLoc, *begin, pivot) & 0xFF)
			begin++;
		do
			end--;
		while (SortByDist_Compare(&refLoc, pivot, *end) & 0xFF);
		if (end <= begin)
			break;
		vector_SwapWrapper(begin, end);
	}
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
	return begin;
}

/*
 * 0x00463990 - iter_swap wrapper
 *
 * Calls GameCentMon_GetPlayerCount (returns 0), then delegates to the
 * actual swap implementation.
 */
void
vector_SwapWrapper(uintptr_t *a, uintptr_t *b)
{
	GameCentMon_GetPlayerCount();
	vector_SwapImpl(a, b);
}

/*
 * 0x00469550 - StdPtrList help queue destructor
 *
 * Clears the StdPtrList help queue. Erases all elements via
 * StdHelpList_EraseRange(begin, end), frees the sentinel node, and zeros
 * head and size.
 */
void
StdPtrList_Destructor_HelpQueue(StdPtrList *this)
{
	StdPtrList *list = this;
	StdPtrNode *endNode;
	StdPtrNode *beginNode;
	StdPtrNode *result;

	StdPtrList_End(list, &endNode);
	StdPtrList_Begin(list, &beginNode);
	StdHelpList_EraseRange(list, &result, beginNode, endNode);
	free(list->head);
	list->head = NULL;
	list->size = 0;
}

/*
 * 0x0046BA10 - CVector destructor (SurfaceInfo variant)
 *
 * Calls CVector_DestroyRangeSI on [begin, end], then computes (capacity -
 * begin) / 12 and deallocates via StdDeque_Dealloc (which is just free).
 * Zeroes begin/end/capacity.
 */
void
CVector_DestructorSI(CVector *this)
{
	int count;

	CVector_DestroyRangeSI(this, (SurfaceInfo *)this->begin, (SurfaceInfo *)this->end);

	count = ((intptr_t)this->capacity - (intptr_t)this->begin) / sizeof(SurfaceInfo);
	StdDeque_DeallocSI(this, this->begin, count);

	this->begin = NULL;
	this->end = NULL;
	this->capacity = NULL;
}

/*
 * 0x0046BA80 - CVector::push_back (SurfaceInfo variant)
 *
 * Calls StdList_GetSize (returns end offset) then CVector_InsertAt.
 */
void
CVector_PushBackSI(CVector *this, SurfaceInfo *value)
{
	uintptr_t count = (uintptr_t)((CVector *)this)->end;
	CVector_InsertAtSI(this, count, value);
}

/*
 * 0x0046BB00 - CVector::InsertAt (SurfaceInfo variant)
 *
 * Computes offset from CSearchCtx_GetBucket, calls _Insert with count=1,
 * then recomputes offset from bucket base.
 *
 * 64-bit: index is really the insertion position pointer, which the
 * binary passes through a DWORD parameter. It must be pointer-sized
 * here or the high half of the address is lost.
 */
static void *
CVector_InsertAtSI(CVector *this, uintptr_t index, SurfaceInfo *value)
{
	uintptr_t bucket;
	int offset;

	bucket = (uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)this);
	offset = ((uintptr_t)index - bucket) / sizeof(SurfaceInfo);
	CVector_InsertSI(this, (SurfaceInfo *)(uintptr_t)index, 1, value);
	bucket = (uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)this);
	return (void *)(bucket + offset * sizeof(SurfaceInfo));
}

/*
 * 0x0046BB50 - Destroy range (SurfaceInfo variant)
 *
 * Iterates from first to last stepping by 12, calling
 * CVector_Destroy6_Single on each.
 */
void
CVector_DestroyRangeSI(CVector *this, SurfaceInfo *first, SurfaceInfo *last)
{
	while (first != last) {
		CVector_Destroy6_Single(this, first);
		first++;
	}
}

/*
 * 0x0046BB80 - CVector::_Insert (SurfaceInfo variant)
 *
 * Complex insert with reallocation logic matching MSVC CVector::_Insert.
 */
static void
CVector_InsertSI(CVector *this, SurfaceInfo *pos, uint32_t count, SurfaceInfo *value)
{
	uint32_t curCapacity;
	uint32_t totalCapacity;
	SurfaceInfo *newBuf;
	SurfaceInfo *newEnd;

	curCapacity = ((uintptr_t)this->capacity - (uintptr_t)this->end) / sizeof(SurfaceInfo);

	if (curCapacity < count) {
		uint32_t curCount = CVector_GetCountC(this);
		uint32_t growTo;

		if (count < curCount)
			growTo = curCount;
		else
			growTo = count;

		totalCapacity = curCount + growTo;

		newBuf = (SurfaceInfo *)StdDeque_AllocPageSI(this, totalCapacity, 0);

		newEnd = (SurfaceInfo *)CVector_UcopySI(this, (SurfaceInfo *)this->begin, pos, newBuf);

		CVector_UfillNSI(this, newEnd, count, value);

		CVector_UcopySI(this, pos, (SurfaceInfo *)this->end, newEnd + count);

		CVector_DestroyRangeSI(this, (SurfaceInfo *)this->begin, (SurfaceInfo *)this->end);

		{
			uint32_t oldAllocCount = ((uintptr_t)this->capacity - (uintptr_t)this->begin) / sizeof(SurfaceInfo);
			StdDeque_DeallocSI(this, this->begin, oldAllocCount);
		}

		this->capacity = (void *)((uintptr_t)newBuf + totalCapacity * sizeof(SurfaceInfo));
		this->end = (void *)((uintptr_t)newBuf + (CVector_GetCountC(this) + count) * sizeof(SurfaceInfo));
		this->begin = newBuf;
	} else {
		uint32_t afterCount = ((uintptr_t)this->end - (uintptr_t)pos) / sizeof(SurfaceInfo);

		if (afterCount < count) {
			CVector_UcopySI(this, pos, (SurfaceInfo *)this->end, pos + count);

			CVector_UfillNSI(this, (SurfaceInfo *)this->end, count - afterCount, value);

			SurfaceInfo_Fill(pos, (SurfaceInfo *)this->end, value);

			this->end = (void *)((uintptr_t)this->end + count * sizeof(SurfaceInfo));
		} else if (count > 0) {
			CVector_UcopySI(this, (SurfaceInfo *)this->end - count, (SurfaceInfo *)this->end, (SurfaceInfo *)this->end);

			SurfaceInfo_CopyBackwardSI(pos, (SurfaceInfo *)this->end - count, (SurfaceInfo *)this->end);

			SurfaceInfo_Fill(pos, pos + count, value);

			this->end = (void *)((uintptr_t)this->end + count * sizeof(SurfaceInfo));
		}
	}
}

/*
 * 0x0046BDC0 - CVector::GetCount (SurfaceInfo variant)
 *
 * Returns element count for 0x0C-byte (12 byte) elements: (end - begin) /
 * 12. Returns 0 if begin is NULL.
 */
static uint32_t __attribute__((unused))
CVector_GetCountC(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uintptr_t)list->end - (uintptr_t)list->begin) / sizeof(SurfaceInfo);
}

/*
 * 0x0046BE00 - std::_Ucopy (SurfaceInfo variant)
 *
 * Copies SurfaceInfo elements from [first, last) to dest via
 * SurfaceInfo_ConstructorAlloc per element. Returns pointer past last written
 * element.
 */
static SurfaceInfo *
CVector_UcopySI(CVector *this, SurfaceInfo *first, SurfaceInfo *last, SurfaceInfo *dest)
{
	while (first != last) {
		SurfaceInfo_ConstructorAlloc(this, dest, first);
		dest++;
		first++;
	}
	return dest;
}

/*
 * 0x0046BE40 - std::_Ufill_n (SurfaceInfo variant)
 *
 * Fills count SurfaceInfo elements at dest by calling
 * SurfaceInfo_ConstructorAlloc per element with the value from src.
 */
static void
CVector_UfillNSI(CVector *this, SurfaceInfo *dest, uint32_t count, SurfaceInfo *src)
{
	while (count > 0) {
		SurfaceInfo_ConstructorAlloc(this, dest, src);
		count--;
		dest++;
	}
}

/*
 * 0x0046BEA0 - std::sort entry (SurfaceInfo)
 *
 * Computes depth from GameCentMon_GetPlayerCount, then calls sort main.
 */
void
SortSurface_Entry(SurfaceInfo *begin, SurfaceInfo *end, char typeTag)
{
	int depth = GameCentMon_GetPlayerCount();
	SortSurface_Main(begin, end, typeTag, depth);
}

/*
 * 0x0046C170 - std::sort main (SurfaceInfo)
 *
 * If range <= 16 elements, calls insertion sort. Otherwise calls
 * quicksort, then insertion sort on the first 16, then unguarded insertion
 * on the rest.
 */
static void
SortSurface_Main(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth)
{
	SurfaceInfo temp;
	USED(depth);

	if (end - begin <= 16) {
		SortSurface_InsertionEntry(begin, end, typeTag);
		return;
	}

	SortSurface_Quicksort(begin, end, typeTag, 0);
	SortSurface_InsertionEntry(begin, begin + 16, typeTag);

	begin += 16;
	while (begin != end) {
		// Copy current element to temp
		temp.flags = begin->flags;
		temp.z = begin->z;
		temp.height = begin->height;
		temp.item = begin->item;

		SortSurface_UnguardedInsert(begin, &temp, typeTag);

		begin++;
	}
}

/*
 * 0x0046C230 - std::sort insertion entry (SurfaceInfo)
 *
 * Computes depth from GameCentMon_GetPlayerCount, then calls insertion
 * sort.
 */
static void
SortSurface_InsertionEntry(SurfaceInfo *begin, SurfaceInfo *end, char typeTag)
{
	int depth = GameCentMon_GetPlayerCount();
	SortSurface_Insertion(begin, end, typeTag, depth);
}

/*
 * 0x0046C260 - std::sort quicksort (SurfaceInfo)
 *
 * Quicksort loop: while range > 16, choose median-of-3 pivot, partition,
 * recurse on smaller half, and return once the range is 16 or less. The
 * caller runs the insertion pass.
 */
static void
SortSurface_Quicksort(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth)
{
	SurfaceInfo lastVal;
	SurfaceInfo midVal;
	SurfaceInfo firstVal;
	SurfaceInfo medResult;
	SurfaceInfo *pivot;
	int rightCount, leftCount;

	USED(depth);

	while (end - begin > 16) {
		// Read last element (end - 1)
		SurfaceInfo *lastElem = end - 1;
		lastVal = *lastElem;

		// Read middle element
		int half = end - begin;
		half = (half - half % 2) / 2;  // cdq; sub; sar
		SurfaceInfo *midElem = begin + half;
		midVal = *midElem;

		// Read first element
		firstVal = *begin;

		// Median of three
		SortSurface_Median3(&medResult, &firstVal, &midVal, &lastVal, typeTag);

		// Partition
		pivot = SortSurface_Partition(begin, end, &medResult, typeTag);

		rightCount = ((uintptr_t)end - (uintptr_t)pivot) / sizeof(SurfaceInfo);
		leftCount = ((uintptr_t)pivot - (uintptr_t)begin) / sizeof(SurfaceInfo);

		if (rightCount <= leftCount) {
			int d = GameCentMon_GetPlayerCount();
			SortSurface_Quicksort(pivot, end, typeTag, d);
			end = pivot;
		} else {
			int d = GameCentMon_GetPlayerCount();
			SortSurface_Quicksort(begin, pivot, typeTag, d);
			begin = pivot;
		}
	}
}

/*
 * 0x0046C3D0 - Unguarded linear insert (SurfaceInfo)
 *
 * Shifts elements right until insertion point found. Compares value < prev
 * (not prev < value) per binary.
 */
void
SortSurface_UnguardedInsert(SurfaceInfo *pos, SurfaceInfo *value, char typeTag)
{
	SurfaceInfo *prev;

	USED(typeTag);
	prev = pos;

	for (;;) {
		prev = prev - 1;
		if (!(SortSurface_Compare(value, prev) & 0xFF))
			break;
		SurfaceInfo_CopyFrom(pos, prev);
		pos = prev;
	}

	SurfaceInfo_CopyFrom(pos, value);
}

/*
 * 0x0046C430 - SurfaceInfo compare by z then height
 *
 * Returns 1 if a < b (z primary, height secondary), 0 otherwise.
 */
int
SortSurface_Compare(SurfaceInfo *a, SurfaceInfo *b)
{
	if (a->z != b->z)
		return a->z < b->z;
	return a->height < b->height;
}

/*
 * 0x0046C490 - Insertion sort (SurfaceInfo)
 *
 * Standard insertion sort: for each element after begin, if it's less than
 * first, copy backward and insert at front; otherwise do unguarded insert.
 */
static void
SortSurface_Insertion(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth)
{
	SurfaceInfo *cur;
	SurfaceInfo temp;

	USED(depth);

	if (begin == end)
		return;

	cur = begin;
	for (;;) {
		cur++;
		if (cur == end)
			return;

		temp = *cur;

		if (SortSurface_Compare(&temp, begin) & 0xFF) {
			// Less than first: copy backward and insert at front
			SurfaceInfo_CopyBackwardSI(begin, cur, cur + 1);
			SurfaceInfo_CopyFrom(begin, &temp);
		} else {
			// Unguarded insert
			SortSurface_UnguardedInsert(cur, &temp, typeTag);
		}
	}
}

/*
 * 0x0046C540 - Median of three (SurfaceInfo)
 *
 * Returns median of three SurfaceInfo values by comparing with
 * SortSurface_Compare.
 */
static SurfaceInfo *
SortSurface_Median3(SurfaceInfo *result, SurfaceInfo *a, SurfaceInfo *b, SurfaceInfo *c, char typeTag)
{
	SurfaceInfo *chosen;

	USED(typeTag);

	if (SortSurface_Compare(a, b) & 0xFF) {
		// a < b
		if (SortSurface_Compare(b, c) & 0xFF) {
			// a < b < c: median is b
			chosen = b;
		} else {
			// a < b, c <= b
			if (SortSurface_Compare(a, c) & 0xFF) {
				chosen = c;
			} else {
				chosen = a;
			}
		}
	} else {
		// b <= a
		if (SortSurface_Compare(a, c) & 0xFF) {
			// b <= a < c: median is a
			chosen = a;
		} else {
			// c <= a, b <= a
			if (SortSurface_Compare(b, c) & 0xFF) {
				chosen = c;
			} else {
				chosen = b;
			}
		}
	}

	result->flags = chosen->flags;
	result->z = chosen->z;
	result->height = chosen->height;
	result->item = chosen->item;
	return result;
}

/*
 * 0x0046C650 - Partition (SurfaceInfo)
 *
 * Hoare-like partition. Advances begin while pivot < end[--]; if lo >= hi,
 * return lo; otherwise swap and continue.
 */
static SurfaceInfo *
SortSurface_Partition(SurfaceInfo *begin, SurfaceInfo *end, SurfaceInfo *pivot, char typeTag)
{
	USED(typeTag);

	for (;;) {
		// Advance begin while compare(begin, pivot)
		while (SortSurface_Compare(begin, pivot) & 0xFF)
			begin++;

		// Retreat end while compare(pivot, end-1)
		for (;;) {
			end--;
			if (!(SortSurface_Compare(pivot, end) & 0xFF))
				break;
		}

		if (end <= begin)
			return begin;

		SortSurface_Swap(begin, end);
		begin++;
	}
}

/*
 * 0x0046C6D0 - Swap with depth wrapper (SurfaceInfo)
 *
 * Computes depth via GameCentMon_GetPlayerCount, then calls
 * SortSurface_SwapImpl.
 */
static void
SortSurface_Swap(SurfaceInfo *a, SurfaceInfo *b)
{
	int depth = GameCentMon_GetPlayerCount();

	SortSurface_SwapImpl(a, b, depth);
}

/*
 * 0x0046C700 - Swap implementation (SurfaceInfo)
 *
 * Swaps two SurfaceInfo values using a local temp.
 */
static void
SortSurface_SwapImpl(SurfaceInfo *a, SurfaceInfo *b, int depth)
{
	SurfaceInfo temp;

	USED(depth);
	temp = *a;
	SurfaceInfo_CopyFrom(a, b);
	SurfaceInfo_CopyFrom(b, &temp);
}

/*
 * 0x0046C8E2 - CCriticalSection::Unlock
 *
 * No-op. LeaveCriticalSection stub compiled as empty in the
 * single-threaded build. Also reused as InitializeCriticalSection after
 * arena allocation.
 */
void
CCriticalSection_Unlock(uint32_t *this)
{
	USED(this);
}

/*
 * 0x0046C8ED - CCriticalSection::Lock
 *
 * No-op. EnterCriticalSection stub compiled as empty in the
 * single-threaded build.
 */
void
CCriticalSection_Lock(uint32_t *this)
{
	USED(this);
}

/*
 * 0x0046CCA0 - CRT printl init
 *
 * MSVC CRT internal: initializes print-level configuration. Reads "printl"
 * environment variable; if found, parses integer via sscanf("%d", &this).
 * If not found, sets default: field[0] = 0x3FF0 with bits 0x800, 0x1000,
 * 0x100 cleared (= 0x26F0), field[1] = field[0] & 0x0F, field[0] &=
 * 0x3FF0. If field[0] == 0, resets to 0x3FF0. Called from static init at
 * 0x00467C05 on g_PrintlConfig (0x00697A40).
 */
static __attribute__((unused)) void *
CRT_PrintlInit(uint32_t *this)
{
	uint32_t *self = this;
	char *envVal;

	self[0] = 0;
	self[1] = 0;
	self[2] = 4;

	envVal = getenv("printl");
	if (envVal != NULL) {
		sscanf(envVal, "%d", (int *)self);
	} else {
		// Default: 0x3FF0 with bits 0x800, 0x1000, 0x100 cleared
		self[0] = 0x3FF0;
		self[0] &= ~0x0800u;
		self[0] &= ~0x1000u;
		self[0] &= ~0x0100u;
	}

	self[1] = self[0] & 0x0F;
	self[0] = self[0] & 0x3FF0;
	if (self[0] == 0)
		self[0] = 0x3FF0;

	return this;
}

/*
 * 0x00472FD0 - CVector::IsEmpty
 *
 * Returns 1 if count == 0, else 0.
 */
int
CVector_IsEmpty(CVector *vec)
{
	return CVector_GetCount(vec) == 0 ? 1 : 0;
}

/*
 * 0x00472FF0 - CVector::EraseBack
 *
 * Gets end pointer (this+8), subtracts 4 to get last element position,
 * calls CVector_EraseSingle. Alternative PopBack implementation that goes
 * through erase path.
 */
void
CVector_EraseBack(CVector *vec)
{
	CVector_EraseSingle(vec, (uintptr_t *)vec->end - 1);
}

/*
 * 0x00473010 - CVector::PopBack
 *
 * The this pointer is an iterator context: a pointer to a slot holding
 * the current iterator position (a vector end pointer). Saves the slot
 * value, decrements the slot by one element, stores the saved value into
 * *out, and returns out. An element is pointer-width, as SerialIter_Deref
 * reads it, which is the binary's 4 bytes on the 32-bit build. Call sites
 * pass &local_var where local_var was initialized by
 * CMultiComponent_GetIterator (via CIterCtx_Set) to vec->end.
 */
uintptr_t **
CVector_PopBack(uint32_t **iter, uintptr_t **out, int dummy)
{
	uint32_t *saved;

	USED(dummy);
	saved = *iter;
	*iter = (uint32_t *)((char *)*iter - sizeof(uintptr_t));
	*out = (uintptr_t *)saved;
	return out;
}

/*
 * 0x00473040 - CVector::EraseSingle
 *
 * Copies [pos+1, end) to pos via vector_Copy, calls CVector_Destroy4_Range on
 * the vacated last element, decrements end. Returns pos.
 */
void *
CVector_EraseSingle(CVector *vec, void *pos)
{
	vector_Copy((uintptr_t *)pos + 1, vec->end, pos);
	CVector_Destroy4_Range(vec, (uintptr_t *)vec->end - 1, vec->end);
	vec->end = (uintptr_t *)vec->end - 1;
	return pos;
}

/*
 * 0x004787E0 - CVector<CMultiCell16>::Swap
 *
 * If source type matches (via StdAllocator_Equal comparison), swaps
 * begin/end/capacity pointers directly. Otherwise creates a temp copy,
 * assigns source to this via CVector_AssignOp4, then assigns temp to
 * source.
 */
void
CVector_Swap16(CVector *dst, CVector *src)
{
	if (StdAllocator_Equal() & 0xFF) {
		// Same allocator: swap begin/end/capacity directly
		vector_SwapImpl((uintptr_t *)&dst->begin, (uintptr_t *)&src->begin);
		vector_SwapImpl((uintptr_t *)&dst->end, (uintptr_t *)&src->end);
		vector_SwapImpl((uintptr_t *)&dst->capacity, (uintptr_t *)&src->capacity);
	} else {
		// Different allocator: copy-assign via temp
		CVector tmp;
		CVector_CopyConstruct4(&tmp, dst);
		CVector_AssignOp4(dst, src);
		CVector_AssignOp4(src, &tmp);
		CVector_Destructor(&tmp);
	}
}

/*
 * 0x004788B0 - CVector<CMultiCell16>::ClearAndFree
 *
 * Iterates elements (16 bytes each) from begin to end, calling the element
 * destructor (0x004798B0) on each. Then frees the buffer via operator
 * delete (0x0046C9E0) with count = (capacity - begin) / 16. Zeroes
 * begin/end/capacity.
 */
__attribute__((unused)) void
CVector_ClearAndFree16(CVector *vec)
{
	int count;

	Destroy16_Range(vec, vec->begin, vec->end);
	count = ((char *)vec->capacity - (char *)vec->begin) / sizeof(CString);
	StdDeque_DeallocSI(vec, vec->begin, (int)count);
	vec->begin = NULL;
	vec->end = NULL;
	vec->capacity = NULL;
}

/*
 * 0x00478910 - CVector<CMultiCell16>::operator=
 *
 * MSVC std::vector assignment operator for 16-byte elements. Three
 * branches: src_count <= dst_count (copy + destroy excess), src_count
 * <= dst_capacity (copy existing + uninit_copy new), or realloc
 * (destroy all, free, allocate, uninit_copy).
 */
__attribute__((unused)) CVector *
CVector_AssignOp16(CVector *this, CVector *src)
{
	uint32_t srcCount, dstCount, dstCap;
	void *mid;

	if (this == src)
		return this;

	srcCount = CVector_GetCount16(src);
	dstCount = CVector_GetCount16(this);

	if (srcCount <= dstCount) {
		// Copy src elements over existing, destroy the excess.
		mid = Uninit_Copy16_Fwd((void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
		Destroy16_Range(this, mid, this->end);
		this->end = (char *)this->begin + srcCount * sizeof(CString);
	} else {
		dstCap = CVector_GetCapacity16(this);
		if (srcCount <= dstCap) {
			// Copy over existing, then uninit_copy new ones.
			void *splitPt = (char *)CSearchCtx_GetBucket((CSearchCtx *)src) + dstCount * sizeof(CString);
			Uninit_Copy16_Fwd((void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), splitPt, this->begin);
			CVector_Uninit_Copy16_Fwd2(this, splitPt, src->end, this->end);
			this->end = (char *)this->begin + srcCount * sizeof(CString);
		} else {
			// Realloc: destroy, free, allocate, copy.
			Destroy16_Range(this, this->begin, this->end);
			StdDeque_DeallocSI(this, this->begin, (int)(((char *)this->capacity - (char *)this->begin) / (ptrdiff_t)sizeof(CString)));
			this->begin = CVector_Allocate16(this, srcCount, 0);
			this->end = CVector_Uninit_Copy16_Fwd2(this, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
			this->capacity = this->end;
		}
	}
	return this;
}

/*
 * 0x00478AE0 - CVector<CMultiCell16>::Resize
 *
 * Calls Uninit_Copy16_Fwd to copy elements forward, then Destroy16_Range
 * to destroy old trailing. Sets this->end.
 */
__attribute__((unused)) void *
CVector_Resize16(CVector *vec, void *newEnd, void *srcStart)
{
	void *mid;

	mid = Uninit_Copy16_Fwd(srcStart, vec->end, newEnd);
	Destroy16_Range(vec, mid, vec->end);
	vec->end = mid;
	return newEnd;
}

/*
 * 0x00478B30 - CVector<CMultiComponentDef>::ClearAndFree
 *
 * Calls Destroy1C_Range on [begin, end), then frees buffer via operator
 * delete with count = (capacity - begin) / 28 (signed idiv). Zeroes
 * begin/end/capacity.
 */
void
CVector_ClearAndFree1C(CVector *vec)
{
	int count;

	Destroy1C_Range(vec, vec->begin, vec->end);
	count = ((char *)vec->capacity - (char *)vec->begin) / sizeof(CMultiComponentDef);
	StdDeque_DeallocSI(vec, vec->begin, (int)count);
	vec->begin = NULL;
	vec->end = NULL;
	vec->capacity = NULL;
}

/*
 * 0x00478BA0 - CVector::GetCount (CMultiComponent variant)
 *
 * Returns element count for 0x1C-byte (28 byte) elements: (end - begin) /
 * 28. Returns 0 if begin is NULL.
 */
uint32_t __attribute__((unused))
CVector_GetCount1C(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uintptr_t)list->end - (uintptr_t)list->begin) / sizeof(CMultiComponentDef);
}

/*
 * 0x00479080 - CVector<CLocation6>::ClearAndFree
 *
 * Destroys the 6-byte elements, frees the buffer, and zeros
 * begin/end/capacity.
 */
void
CVector_ClearAndFree6(CVector *this)
{
	int count;
	Destroy6_Range(this, this->begin, this->end);
	count = ((char *)this->capacity - (char *)this->begin) / sizeof(CLocation);
	StdDeque_DeallocSI(this, this->begin, (int)count);
	this->begin = NULL;
	this->end = NULL;
	this->capacity = NULL;
}

/*
 * 0x004790F0 - CVector<CLocation6>::push_back
 *
 * Inserts element at the end of the vector.
 */
void
CVector_PushBack6(CVector *this, void *element)
{
	void *endPtr = this->end;
	CDeque6_InsertAtEnd(this, endPtr, element);
}

/*
 * 0x00479180 - CVector::CopyConstruct (pointer elements)
 *
 * Copies the type byte, allocates a buffer sized to src's count, copies
 * the elements in, and sets end = capacity.
 */
static CVector *
CVector_CopyConstruct4(CVector *this, CVector *src)
{
	this->type = src->type;
	this->begin = CVector_Allocate4(this, CVector_GetCount(src), 0);
	this->end = CVector_Ucopy((StdAllocator *)this, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
	this->capacity = this->end;
	return this;
}

/*
 * 0x004791F0 - CVector<uint32_t>::operator=
 *
 * MSVC std::vector assignment operator for 4-byte elements. Same
 * 3-branch structure as CVector_AssignOp16 but for uint32_t.
 */
static CVector *
CVector_AssignOp4(CVector *this, CVector *src)
{
	uint32_t srcCount, dstCount, dstCap;
	void *mid;

	if (this == src)
		return this;

	srcCount = CVector_GetCount(src);
	dstCount = CVector_GetCount(this);

	if (srcCount <= dstCount) {
		vector_Copy((void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
		mid = (char *)this->begin + srcCount * sizeof(uintptr_t);
		CVector_Destroy4_Range(this, mid, this->end);
		this->end = (char *)this->begin + srcCount * sizeof(uintptr_t);
	} else {
		dstCap = CVector_GetCapacity(this);
		if (srcCount <= dstCap) {
			void *splitPt = (char *)CSearchCtx_GetBucket((CSearchCtx *)src) + dstCount * sizeof(uintptr_t);
			vector_Copy((void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), splitPt, this->begin);
			CVector_Ucopy((StdAllocator *)this, splitPt, src->end, this->end);
			this->end = (char *)this->begin + srcCount * sizeof(uintptr_t);
		} else {
			CVector_Destroy4_Range(this, this->begin, this->end);
			StdDeque_DeallocSI(this, this->begin, (int)(((char *)this->capacity - (char *)this->begin) / (ptrdiff_t)sizeof(uintptr_t)));
			this->begin = CVector_Allocate4(this, srcCount, 0);
			this->end = CVector_Ucopy((StdAllocator *)this, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
			this->capacity = this->end;
		}
	}
	return this;
}

/*
 * 0x00479380 - CVector::GetCapacity (4-byte elements)
 *
 * Returns allocated capacity: (capacity - begin) / 4, or 0 if begin is
 * NULL.
 */
uint32_t
CVector_GetCapacity(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uintptr_t *)list->capacity - (uintptr_t *)list->begin);
}

/*
 * 0x004793C0 - CVector::GetCapacity (16-byte elements)
 *
 * Returns allocated capacity: (capacity - begin) / 16, or 0 if begin is
 * NULL.
 */
uint32_t
CVector_GetCapacity16(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uintptr_t)list->capacity - (uintptr_t)list->begin) / sizeof(CString);
}

/*
 * 0x00479450 - Destroy16_Range
 *
 * Iterates 16-byte elements from first to last, calling
 * CVector_Destroy16_Single (0x004798B0) on each.
 */
__attribute__((unused)) void
Destroy16_Range(CVector *this, void *first, void *last)
{
	char *p = (char *)first;
	while (p != (char *)last) {
		CVector_Destroy16_Single(this, p);
		p += sizeof(CString);
	}
}

/*
 * 0x00479510 - Destroy1C_Range
 *
 * Iterates 28-byte elements from first to last, calling
 * CVector_Destroy1C_Single (0x00479B90) on each.
 */
__attribute__((unused)) void
Destroy1C_Range(CVector *this, void *first, void *last)
{
	char *p = (char *)first;
	while (p != (char *)last) {
		CVector_Destroy1C_Single(this, p);
		p += sizeof(CMultiComponentDef);
	}
}

/*
 * 0x00479630 - Destroy6_Range
 *
 * Iterates 6-byte elements from first to last, calling
 * CVector_Destroy6_Single (0x00479FF0) on each.
 */
__attribute__((unused)) void
Destroy6_Range(CVector *this, void *first, void *last)
{
	char *p = (char *)first;
	while (p != (char *)last) {
		CVector_Destroy6_Single(this, p);
		p += sizeof(CLocation);
	}
}

/*
 * 0x00479B50 - CVector<CMultiComponentDef>::Allocate
 *
 * Thiscall wrapper forwarding to Allocate1C (0x0047B1D0) with (count, 0);
 * the second stack argument is declared and never read.
 */
void *
CVector_Allocate1C(CVector *this, int count, int unused_arg)
{
	USED(this);
	USED(unused_arg);
	return Allocate1C(count, NULL);
}

/*
 * 0x0047A350 - CVector::Allocate (4-byte elements)
 *
 * Thiscall wrapper that forwards to Vector_AllocElements(count, 0),
 * which saturates count to zero and allocates count*4 bytes via
 * OperatorNew.
 */
static void *
CVector_Allocate4(CVector *this, uint32_t count, int unused)
{
	USED(this);
	USED(unused);
	return Vector_AllocElements((int)count);
}

/*
 * 0x0047A3F0 - CVector<CLocation>::GetCount
 *
 * Returns element count for 6-byte elements: (end - begin) / 6. Returns 0
 * if begin is NULL.
 */
__attribute__((unused)) uint32_t
CVector_GetCount6(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uintptr_t)list->end - (uintptr_t)list->begin) / sizeof(CLocation);
}

/*
 * 0x0047A4B0 - CVector::Allocate (6-byte elements)
 *
 * Allocates room for count CLocation elements.
 */
__attribute__((unused)) void *
CVector_Allocate6(CVector *this, uint32_t count, int unused)
{
	USED(this);
	USED(unused);
	return Allocate6_Inner((int)count);
}

/*
 * 0x0047ACC0 - std::find for uint32_t
 *
 * Linear scan [begin, end) comparing each element with *value. Returns
 * pointer to found element, or end if not found.
 */
void *
Vector_Find(void *begin, void *end, uintptr_t *value)
{
	uintptr_t *p = begin;
	uintptr_t *e = end;

	while (p != e) {
		if (*p == *value)
			return p;
		p++;
	}
	return p;
}

/*
 * 0x0047B150 - StdAllocator::Equal
 *
 * MSVC STL allocator equality comparison. For default allocators, always
 * returns true (all default allocator instances are interchangeable).
 * Called from std::list splice and std::vector swap to check allocator
 * compatibility before in-place node/pointer operations.
 */
int
StdAllocator_Equal(void)
{
	return 1;
}

/*
 * 0x0047BEA0 - SortInt_Dispatch
 *
 * Sorts an array of int pointers. If count <= 16, calls
 * InsertionSort_Int_Small (insertion sort). Otherwise calls
 * SortMultiInt_Quicksort (quicksort), then insertion-sorts [first,
 * first+64] and the remaining tail elements.
 */
__attribute__((unused)) void
SortInt_Dispatch(void *first, void *last, uint8_t cmpVal, int unused)
{
	int count;

	USED(unused);
	count = ((char *)last - (char *)first) / sizeof(uintptr_t);
	if (count <= 16) {
		InsertionSort_Int_Small(first, last, cmpVal);
		return;
	}
	SortMultiInt_Quicksort(first, last, cmpVal, 0);
	InsertionSort_Int_Small(first, (char *)first + 16 * sizeof(uintptr_t), cmpVal);
	first = (char *)first + 16 * sizeof(uintptr_t);
	while (first != last) {
		InsertionSort_Int_ShiftDown(first, *(uintptr_t *)first, cmpVal);
		first = (char *)first + sizeof(uintptr_t);
	}
}

/*
 * 0x0047BF30 - SortDist_Dispatch
 *
 * Sorts an array of int pointers by distance. If count <= 16, calls
 * InsertionSort_Dist_Small (insertion sort). Otherwise calls
 * SortMultiDist_Quicksort (quicksort), then insertion-sorts [first,
 * first+64] and the remaining tail elements.
 */
__attribute__((unused)) void
SortDist_Dispatch(void *first, void *last, CLocation cmpLoc)
{
	int count;

	count = ((char *)last - (char *)first) / sizeof(uintptr_t);
	if (count <= 16) {
		InsertionSort_Dist_Small(first, last, cmpLoc);
		return;
	}
	SortMultiDist_Quicksort(first, last, cmpLoc, 0);
	InsertionSort_Dist_Small(first, (char *)first + 16 * sizeof(uintptr_t), cmpLoc);
	first = (char *)first + 16 * sizeof(uintptr_t);
	while (first != last) {
		CLocation tmp;
		CLocation_CopyFromSrc(&tmp, &cmpLoc);
		InsertionSort_Dist_ShiftDown(first, *(uintptr_t *)first, tmp);
		first = (char *)first + sizeof(uintptr_t);
	}
}

/*
 * 0x0047C810 - SortMultiInt_Quicksort
 *
 * Quicksort for int* arrays using the integer less-than comparator.
 * Recursive with median-of-3 pivot selection. Stops recursing when the
 * partition size drops to 16 or below.
 */
static void
SortMultiInt_Quicksort(void *first, void *last, uint8_t cmpVal, int unused)
{
	int count;
	uintptr_t pivot;
	void *part;

	USED(unused);
	for (;;) {
		count = ((char *)last - (char *)first) / sizeof(uintptr_t);
		if (count <= 16)
			return;
		pivot = SortMultiInt_Median3(*(uintptr_t *)first, *(uintptr_t *)((char *)first + (count / 2) * sizeof(uintptr_t)), *((uintptr_t *)last - 1), cmpVal);
		part = SortMultiInt_Partition(first, last, pivot, cmpVal);
		if (((char *)last - (char *)part) / sizeof(uintptr_t) <= ((char *)part - (char *)first) / sizeof(uintptr_t)) {
			SortMultiInt_Quicksort(part, last, cmpVal, GameCentMon_GetPlayerCount());
			last = part;
		} else {
			SortMultiInt_Quicksort(first, part, cmpVal, GameCentMon_GetPlayerCount());
			first = part;
		}
	}
}

/*
 * 0x0047C970 - SortMultiDist_Quicksort
 *
 * Quicksort for int* arrays using the distance comparator. Recursive with
 * median-of-3 pivot selection. Stops recursing when the partition size
 * drops to 16 or below.
 */
static void
SortMultiDist_Quicksort(void *first, void *last, CLocation cmpLoc, int unused)
{
	int count;
	uintptr_t pivot;
	void *part;
	CLocation tmp1, tmp2;

	USED(unused);
	for (;;) {
		count = ((char *)last - (char *)first) / sizeof(uintptr_t);
		if (count <= 16)
			return;
		CLocation_CopyFromSrc(&tmp1, &cmpLoc);
		CLocation_CopyFromSrc(&tmp2, &cmpLoc);
		pivot = SortMultiDist_Median3(*(uintptr_t *)first, *(uintptr_t *)((char *)first + (count / 2) * sizeof(uintptr_t)), *((uintptr_t *)last - 1), tmp1);
		part = SortMultiDist_Partition(first, last, pivot, tmp2);
		if (((char *)last - (char *)part) / sizeof(uintptr_t) <= ((char *)part - (char *)first) / sizeof(uintptr_t)) {
			CLocation tmp3;
			CLocation_CopyFromSrc(&tmp3, &cmpLoc);
			SortMultiDist_Quicksort(part, last, tmp3, GameCentMon_GetPlayerCount());
			last = part;
		} else {
			CLocation tmp4;
			CLocation_CopyFromSrc(&tmp4, &cmpLoc);
			SortMultiDist_Quicksort(first, part, tmp4, GameCentMon_GetPlayerCount());
			first = part;
		}
	}
}

/*
 * 0x0047D130 - SortMultiInt_Median3
 *
 * Median-of-three for std::sort with the integer comparator. Returns the
 * median of (a, b, c).
 */
static uintptr_t
SortMultiInt_Median3(uintptr_t a, uintptr_t b, uintptr_t c, int cmpVal)
{
	if (IntLessThan(&a, &b, cmpVal) & 0xFF) {
		if (IntLessThan(&b, &c, cmpVal) & 0xFF)
			return b;
		if (IntLessThan(&a, &c, cmpVal) & 0xFF)
			return c;
		else
			return a;
	} else {
		if (IntLessThan(&a, &c, cmpVal) & 0xFF)
			return a;
		if (IntLessThan(&b, &c, cmpVal) & 0xFF)
			return c;
		else
			return b;
	}
}

/*
 * 0x0047D200 - SortMultiInt_Partition
 *
 * Hoare partition for std::sort with integer comparator. Partitions
 * [first, last) around pivot value.
 */
static void *
SortMultiInt_Partition(void *first, void *last, uintptr_t pivot, int cmpVal)
{
	uintptr_t *lo = (uintptr_t *)first;
	uintptr_t *hi = (uintptr_t *)last;

	for (;;) {
		for (;;) {
			if ((IntLessThan(lo, &pivot, cmpVal) & 0xFF) == 0)
				break;
			lo++;
		}
		for (;;) {
			hi--;
			if ((IntLessThan(&pivot, hi, cmpVal) & 0xFF) == 0)
				break;
		}
		if (hi <= lo)
			return lo;
		vector_SwapWrapper((uintptr_t *)lo, (uintptr_t *)hi);
		lo++;
	}
}

/*
 * 0x0047D310 - SortMultiDist_Median3
 *
 * Median-of-three for std::sort with the distance comparator. Returns the
 * median of (a, b, c).
 */
static uintptr_t
SortMultiDist_Median3(uintptr_t a, uintptr_t b, uintptr_t c, CLocation cmpLoc)
{
	if (CLocation_DistanceComparator2D(&cmpLoc, (CItem *)a, (CItem *)b) & 0xFF) {
		if (CLocation_DistanceComparator2D(&cmpLoc, (CItem *)b, (CItem *)c) & 0xFF)
			return b;
		if (CLocation_DistanceComparator2D(&cmpLoc, (CItem *)a, (CItem *)c) & 0xFF)
			return c;
		else
			return a;
	} else {
		if (CLocation_DistanceComparator2D(&cmpLoc, (CItem *)a, (CItem *)c) & 0xFF)
			return a;
		if (CLocation_DistanceComparator2D(&cmpLoc, (CItem *)b, (CItem *)c) & 0xFF)
			return c;
		else
			return b;
	}
}

/*
 * 0x0047D3E0 - SortMultiDist_Partition
 *
 * Hoare partition for std::sort with the distance comparator. Partitions
 * [first, last) around pivot value.
 */
static void *
SortMultiDist_Partition(void *first, void *last, uintptr_t pivot, CLocation cmpLoc)
{
	uintptr_t *lo = (uintptr_t *)first;
	uintptr_t *hi = (uintptr_t *)last;

	for (;;) {
		for (;;) {
			if ((CLocation_DistanceComparator2D(&cmpLoc, (CItem *)*lo, (CItem *)pivot) & 0xFF) == 0)
				break;
			lo++;
		}
		for (;;) {
			hi--;
			if ((CLocation_DistanceComparator2D(&cmpLoc, (CItem *)pivot, (CItem *)*hi) & 0xFF) == 0)
				break;
		}
		if (hi <= lo)
			return lo;
		vector_SwapWrapper((uintptr_t *)lo, (uintptr_t *)hi);
		lo++;
	}
}

/*
 * 0x0047F2B0 - std::sort by distance
 *
 * The distance sort's entry called from BroadcastToNearby and
 * BroadcastToRangeWithLOS, the same code as Vector_SortByDistPair at its
 * own address.
 */
void
Vector_SortByDist(void *begin, void *end, CLocation refLoc)
{
	int depth;
	CLocation arg;

	depth = GameCentMon_GetPlayerCount();
	CLocation_CopyFromSrc(&arg, &refLoc);
	SortByDist_Main((uintptr_t *)begin, (uintptr_t *)end, arg, depth);
	CResBankMagicCtx_PostInit((CResListNode **)&refLoc);
}

/*
 * 0x0047F350 - PacketGetDynamicSize
 */
void *
PacketGetDynamicSize(uint8_t *buf)
{
	uint16_t v;
	void *result;

	result = (void *)(uintptr_t)PacketIsDynamicSize(buf);
	if (result) {
		memcpy(&v, buf + 1, 2);
		htons_inplace(&v);
		result = memcpy(buf + 1, &v, 2);
	}
	return result;
}

/*
 * 0x0047F3A0 - PacketIsEDEDEDED
 *
 * Returns 1 when the 4-byte payload immediately after the opcode (or
 * after the opcode and 16-bit length for dynamic-size packets) matches
 * the 0xEDEDEDED client sentinel.
 */
int
PacketIsEDEDEDED(uint8_t *buf)
{
	uint32_t v;

	if (PacketIsDynamicSize(buf))
		memcpy(&v, buf + 3, 4);
	else
		memcpy(&v, buf + 1, 4);
	return (v == 0xEDEDEDED);
}

/*
 * 0x00484620 - StdPtrList::~StdPtrList (NPC variant)
 *
 * Gets Begin and End iterators, erases entire range via
 * StdPtrList16_EraseAll, frees sentinel node, zeroes head/size.
 */
__attribute__((unused)) void
StdPtrList_Destructor_NPC(StdPtrList *list)
{
	StdPtrNode *endNode, *beginNode, *result;

	StdPtrList_End(list, &endNode);
	StdPtrList_Begin(list, &beginNode);
	StdPtrList16_EraseAll(list, &result, beginNode, endNode);
	free(list->head);
	list->head = NULL;
	list->size = 0;
}

/*
 * 0x00484680 - StdPtrList16::InsertEnd
 *
 * Gets the End() iterator and inserts value before it.
 */
void
StdPtrList16_InsertEnd(StdPtrList *list, void *value)
{
	StdPtrNode *endNode;
	StdPtrNode *result;
	StdPtrNode **pEnd;

	pEnd = &endNode;
	StdPtrList_End(list, pEnd);
	StdPtrList_Insert16(list, &result, endNode, value);
}

/*
 * 0x004846B0 - std::list<16-byte>::erase
 *
 * Template instantiation of std::list::erase for 16-byte element type.
 * Post-increments iterator (0x00484990), unlinks node, calls
 * CVector_Destroy16_Single (0x004798B0) on value, frees node, decrements
 * count. Stores next iterator to *result.
 */
void
StdPtrList_Erase16(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos)
{
	StdPtrNode *postIncTemp;
	StdPtrNode *node;

	StdPtrIter_PostInc_484990(&pos, &postIncTemp, 0);
	node = postIncTemp;

	node->prev->next = node->next;
	node->next->prev = node->prev;
	CVector_Destroy16_Single((CVector *)list, (void *)&node->value);

	free(node);

	list->size--;
	*result = pos;
}

/*
 * 0x00484770 - std::list<void*>::iterator::operator!=
 *
 * Compares two iterators for inequality. Calls operator== and inverts.
 */
int
StdPtrIter_Neq(StdPtrNode **a, StdPtrNode **b)
{
	return !StdPtrIter_Eq(a, b);
}

/*
 * 0x004847A0 - Vector deleting destructor (NPC variant)
 *
 * With flags & 2, runs StdPtrList16_DestroyAll on each 12-byte element
 * before freeing the slab header. Otherwise runs the single-element
 * destructor and optionally frees the object.
 *
 * The element count is read at its native width, where the MSVC CRT
 * stores it as a uint32 ahead of the slab - the 64-bit portability
 * allowance, not a behavioural deviation.
 */
void *
StdPtrList16_VecDtor(StdPtrList *this, int flags)
{
	if (flags & 2) {
		// Custom: 64-bit - MSVC CRT stores count as uint32, but
		// allocation header is pointer-sized for alignment
		int count = *(uint32_t *)((char *)this - sizeof(uintptr_t));
		StdPtrList *arr = this;
		int i;
		for (i = count - 1; i >= 0; i--)
			StdPtrList_Clear(&arr[i]);
		OperatorDelete((char *)this - sizeof(uintptr_t));
	} else {
		StdPtrList_Clear(this);
		if (flags & 1)
			OperatorDelete(this);
	}
	return this;
}

/*
 * 0x00484820 - std::list<16-byte>::_Insert
 *
 * Buys a node linked between pos and its predecessor, splices it in,
 * copy-constructs the 16-byte value into it, bumps the count, and stores
 * the iterator into *result.
 */
static void
StdPtrList_Insert16(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *node;
	StdPtrNode *newNode;

	// The binary takes the iterator by value and dereferences it here;
	// pos is already the node that iterator points at.
	node = pos;
	newNode = StdFileList_Buynode(list, node, *StdPtrNode_GetPrev(node));
	*StdPtrNode_GetPrev(node) = newNode;
	node = *StdPtrNode_GetPrev(node);
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;

	CopyFrom16((CVector *)list, StdPtrNode_GetValue(node), value);

	list->size++;
	CIterCtx_Set(result, node);
}

/*
 * 0x004848D0 - StdPtrList16::EraseAll
 *
 * While first != last, post-increments first (0x00484990), extracts value,
 * erases old position from list. Copies final iterator to result[0]. This
 * is a loop wrapper around Erase16.
 */
static StdPtrNode **
StdPtrList16_EraseAll(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;
	StdPtrNode *tmp;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc_484990(&first, &postIncTemp, 0);
		tmp = *(StdPtrNode **)&postIncTemp;
		StdPtrList_Erase16(list, &postIncTemp, tmp);
	}
	*result = first;

	return result;
}

/*
 * 0x00484920 - std::list<CFileEntry>::_Buynode
 *
 * Allocates a 0x18 (24) byte node via _Charalloc. Sets next to nextHint
 * (or self if NULL) via _Nextnode, and prev to prevHint (or self if NULL)
 * via _Prevnode. Returns new node. Template instantiation of _Buynode for
 * CFileEntry (0x10 value + 8 header).
 */
StdPtrNode *
StdFileList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint)
{
	StdPtrNode *newNode;
	StdPtrNode *nextVal;
	StdPtrNode *prevVal;

	newNode = (StdPtrNode *)StdPtrList_Charalloc(list, 2 * sizeof(void *) + sizeof(CFileEntry));

	if (nextHint != NULL)
		nextVal = nextHint;
	else
		nextVal = newNode;
	StdPtrNode_GetNext(newNode)->next = nextVal;

	if (prevHint != NULL)
		prevVal = prevHint;
	else
		prevVal = newNode;
	*StdPtrNode_GetPrev(newNode) = prevVal;

	return newNode;
}

/*
 * 0x00484990 - std::list<void*>::iterator::operator++(int)
 *
 * The second instantiation of StdPtrIter_PostInc: saves the iterator,
 * advances it through _Inc and stores the saved value into outIter with
 * one store, where 0x0044D770 calls the copy constructor. Returns
 * outIter. dummy is the post-increment overload's int.
 */
StdPtrNode **
StdPtrIter_PostInc_484990(StdPtrNode **iter, StdPtrNode **outIter, int dummy)
{
	StdPtrNode *saved;

	USED(dummy);
	saved = *iter;
	StdPtrIter_Inc(iter);
	*outIter = saved;
	return outIter;
}

/*
 * 0x00484B90 - std::list _Buynode
 *
 * Allocates a 12-byte node via _Charalloc. Sets next to nextHint (or self
 * if NULL) via _Nextnode, and prev to prevHint (or self if NULL) via
 * _Prevnode. Returns new node.
 */
StdPtrNode *
StdPtrList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint)
{
	StdPtrNode *newNode;
	StdPtrNode *nextVal;
	StdPtrNode *prevVal;

	newNode = (StdPtrNode *)StdPtrList_Charalloc(list, sizeof(StdPtrNode));

	if (nextHint != NULL)
		nextVal = nextHint;
	else
		nextVal = newNode;
	StdPtrNode_GetNext(newNode)->next = nextVal;

	if (prevHint != NULL)
		prevVal = prevHint;
	else
		prevVal = newNode;
	*StdPtrNode_GetPrev(newNode) = prevVal;

	return newNode;
}

/*
 * 0x00484C00 - StdPtrList_FindString
 *
 * Iterates from begin to end, dereferences each element as CString*,
 * compares with searchStr using CString_EqualCString2 (case-insensitive).
 * Writes matching iterator (or end) to *result and returns result.
 */
StdPtrNode **
StdPtrList_FindString(StdPtrNode **result, StdPtrNode **begin, StdPtrNode **end, CString *searchStr)
{
	while (StdPtrIter_Neq(begin, end) & 0xFF) {
		CString *elem = (CString *)StdPtrIter_Deref(begin);
		if (CString_EqualCString2(elem, searchStr))
			break;
		StdPtrIter_Inc(begin);
	}
	*result = *begin;
	return result;
}

/*
 * 0x00491DF0 - StdPtrList destructor wrapper
 *
 * Erases all nodes and frees the sentinel via StdPtrList_Clear. Used as the
 * atexit callback delegate for g_entityMgrList cleanup.
 */
static __attribute__((unused)) void
StdPtrList_DestructorWrapper_EntityMgr(void)
{
	StdPtrList_Clear(&g_entityMgrList);
}

/*
 * 0x004A6410 - CVector array deleting destructor
 *
 * If flags & 2: reads the count at alloc-4, calls CVector_Destructor on each
 * CVector in the prefixed array in reverse order, then frees the alloc-4
 * block. If flags & 2 is NOT set: just calls CVector_Destructor on this, and if
 * flags & 1, frees this.
 */
void *
CVector_VecDestructor_Region(CVector *this, int flags)
{
	if (flags & 2) {
		int i;
		// Custom: 64-bit - sizeof(uintptr_t) header for alignment
		char *alloc = (char *)this - sizeof(uintptr_t);
		int count = *(uint32_t *)alloc;
		for (i = count - 1; i >= 0; i--)
			CVector_Destructor(&this[i]);
		OperatorDelete(alloc);
	} else {
		CVector_Destructor(this);
		if (flags & 1)
			OperatorDelete(this);
	}
	return this;
}

/*
 * 0x004A64C0 - CVector::Erase(first, last)
 *
 * Calls std::copy (0x004D9760) to shift trailing elements, _Destroy
 * (0x00422740) on the dead tail, then sets end to the new position.
 * Returns first.
 */
void *
CVector_Erase(CVector *vec, void *first, void *last)
{
	void *newEnd;

	newEnd = vector_Copy(last, vec->end, first);
	CVector_Destroy4_Range(vec, newEnd, vec->end);
	vec->end = newEnd;

	return first;
}

/*
 * 0x004CF610 - StdPtrList_DoInsert (second copy, actual impl)
 *
 * Calls StdKfn_Identity(4, dest). If result is non-null, copies *source to
 * *result (single dword).
 */
static void
StdPtrList_DoInsert_4CF610(void *dest, void *source)
{
	void *result;

	result = (void *)StdKfn_Identity(4, (uintptr_t)dest);
	if (result != NULL) {
		*(uintptr_t *)result = *(uintptr_t *)source;
	}
}

/*
 * 0x004D9760 - std::copy (uintptr_t element-by-element copy)
 *
 * Copies [first, last) to dest. Returns pointer past last copied element.
 */
static void *
vector_Copy(void *first, void *last, void *dest)
{
	uintptr_t *s = first;
	uintptr_t *e = last;
	uintptr_t *d = dest;

	while (s != e) {
		*d = *s;
		d++;
		s++;
	}
	return d;
}

/*
 * 0x004E30A0 - _Container_base default constructor
 *
 * No-op - saves ECX to stack, returns it. MSVC STL iterator base class
 * constructor. 31 xrefs.
 */
StdPtrNode **
StdPtrIter_BaseConstructor(StdPtrNode **iter)
{
	USED(iter);
	return iter;
}

/*
 * 0x004E30B0 - std::_Tree iterator dereference
 *
 * Dereferences this->ptr (the StdTreeNode pointer stored in the iterator),
 * calls StdTreeNode_KeyValuePtr to get the key/value pair address. Returns
 * &node->key.
 */
void *
StdTreeIter_Deref(void *iter)
{
	void *node = *(void **)iter;
	return StdTreeNode_KeyValuePtr(node);
}

/*
 * 0x004E30D0 - std::_Tree node key/value accessor
 *
 * Returns pointer to the key/value pair within a StdTreeNode. Key starts
 * at offset 0x0C in the 32-bit binary (after left, parent, right
 * pointers).
 */
void *
StdTreeNode_KeyValuePtr(StdTreeNode *node)
{
	return &node->key;
}

/*
 * 0x004E3500 - std::_Tree iterator copy (MSVC CRT)
 *
 * Copies the node pointer and the trailing byte from src to dest.
 */
static void *
StdTree_CopyIter(void *dest, void *srcPtr, void *srcByte)
{
	*(void **)dest = *(void **)srcPtr;
	*(uint8_t *)((char *)dest + sizeof(void *)) = *(uint8_t *)srcByte;
	return dest;
}

/*
 * 0x004E3550 - std::_Tree::_Key
 *
 * Returns pointer to the key field within a red-black tree node.
 */
static void *
StdTreeNode_Key(StdTreeNode *node)
{
	return &node->key;
}

/*
 * 0x004E63B0 - std::_Tree::clear
 *
 * Erases all nodes, frees the head node, and decrements the nil sentinel
 * reference count. If refcount reaches 0, frees the nil sentinel and
 * zeroes its global pointer.
 */
void
StdTree_Clear(StdMapTree *tree)
{
	StdTreeNode *head = tree->head;

	// Erase all nodes (binary: calls _EraseRange with begin/end).
	(void)StdTree_EraseRange(tree, head->left, head);

	// Free head node.
	OperatorDelete(head);
	tree->head = NULL;
	tree->size = 0;

	// Decrement nil sentinel refcount.
	g_HandleMapNilRef--;
	if (g_HandleMapNilRef == 0) {
		OperatorDelete(g_HandleMapNil);
		g_HandleMapNil = NULL;
	}
}

/*
 * 0x004E6420 - std::map::insert
 *
 * Walks the tree to find the insertion point for the given key. Returns
 * the existing node when the key already exists, otherwise allocates and
 * links a new node.
 */
StdTreeNode *
StdTree_Insert(StdMapTree *tree, uintptr_t key, uintptr_t value)
{
	StdTreeNode *head = tree->head;
	StdTreeNode *nil = g_HandleMapNil;
	StdTreeNode *y = head;
	StdTreeNode *x = head->parent; // root
	int addLeft = 1;

	// Walk the tree to find the insertion point.
	while (x != nil) {
		addLeft = key < x->key;
		y = x;
		if (addLeft)
			x = x->left;
		else
			x = x->right;
	}

	// Check for a duplicate key.
	if (tree->multi)
		return StdTree_RBInsert(tree, addLeft, y, key, value);

	StdTreeNode *iter = y;
	if (addLeft) {
		if (y == head->left)
			return StdTree_RBInsert(tree, 1, y, key, value);
		StdTree_Dec(&iter);
	}
	if (iter->key < key)
		return StdTree_RBInsert(tree, addLeft, y, key, value);
	// Duplicate key: return existing node.
	return iter;
}

/*
 * 0x004E6500 - std::_Tree::erase (RB delete with fixup)
 *
 * Erases the node at pos and returns the iterator that follows it
 * through ret: pos advances through _Inc before the node is unlinked.
 * The rebalancing follows MSVC's _Tree::erase with _Lrotate and _Rrotate
 * inlined, then the node is freed and the size decremented.
 * g_HandleMapNil is a real node, so x may be the nil node: its parent is
 * written and its color, black, is read and written, as in the binary.
 * The binary brackets the minimum and maximum walks and each rotation in
 * a std::_Lockit critical section, which we omit along with every other
 * lock in the single-threaded port.
 */
StdTreeNode **
StdTree_RBErase(StdMapTree *tree, StdTreeNode **ret, StdTreeNode *pos)
{
	StdTreeNode *head = tree->head;
	StdTreeNode *nil = g_HandleMapNil;
	StdTreeNode *x, *y, *z, *w, *n, *c;
	int color;

	y = pos;
	StdTree_Inc(&pos);
	z = y;
	if (y->left == nil)
		x = y->right;
	else if (y->right == nil)
		x = y->left;
	else {
		y = y->right;
		while (y->left != nil)
			y = y->left;
		x = y->right;
	}

	if (y != z) {
		// Relink the successor y in z's place.
		z->left->parent = y;
		y->left = z->left;
		if (y == z->right)
			x->parent = y;
		else {
			x->parent = y->parent;
			y->parent->left = x;
			y->right = z->right;
			z->right->parent = y;
		}
		if (head->parent == z)
			head->parent = y;
		else if (z->parent->left == z)
			z->parent->left = y;
		else
			z->parent->right = y;
		y->parent = z->parent;
		color = y->color;
		y->color = z->color;
		z->color = color;
		y = z;
	} else {
		// Splice z out, and update the leftmost and rightmost.
		x->parent = y->parent;
		if (head->parent == z)
			head->parent = x;
		else if (z->parent->left == z)
			z->parent->left = x;
		else
			z->parent->right = x;
		if (head->left == z) {
			if (z->right == nil)
				head->left = z->parent;
			else {
				n = x;
				while (n->left != nil)
					n = n->left;
				head->left = n;
			}
		}
		if (head->right == z) {
			if (z->left == nil)
				head->right = z->parent;
			else {
				n = x;
				while (n->right != nil)
					n = n->right;
				head->right = n;
			}
		}
	}

	if (y->color == 1) {
		while (x != head->parent && x->color == 1) {
			if (x == x->parent->left) {
				w = x->parent->right;
				if (w->color == 0) {
					w->color = 1;
					x->parent->color = 0;
					// _Lrotate(x->parent)
					n = x->parent;
					c = n->right;
					n->right = c->left;
					if (c->left != nil)
						c->left->parent = n;
					c->parent = n->parent;
					if (n == head->parent)
						head->parent = c;
					else if (n == n->parent->left)
						n->parent->left = c;
					else
						n->parent->right = c;
					c->left = n;
					n->parent = c;
					w = x->parent->right;
				}
				if (w->left->color == 1 && w->right->color == 1) {
					w->color = 0;
					x = x->parent;
				} else {
					if (w->right->color == 1) {
						w->left->color = 1;
						w->color = 0;
						// _Rrotate(w)
						c = w->left;
						w->left = c->right;
						if (c->right != nil)
							c->right->parent = w;
						c->parent = w->parent;
						if (w == head->parent)
							head->parent = c;
						else if (w == w->parent->right)
							w->parent->right = c;
						else
							w->parent->left = c;
						c->right = w;
						w->parent = c;
						w = x->parent->right;
					}
					w->color = x->parent->color;
					x->parent->color = 1;
					w->right->color = 1;
					// _Lrotate(x->parent)
					n = x->parent;
					c = n->right;
					n->right = c->left;
					if (c->left != nil)
						c->left->parent = n;
					c->parent = n->parent;
					if (n == head->parent)
						head->parent = c;
					else if (n == n->parent->left)
						n->parent->left = c;
					else
						n->parent->right = c;
					c->left = n;
					n->parent = c;
					break;
				}
			} else {
				w = x->parent->left;
				if (w->color == 0) {
					w->color = 1;
					x->parent->color = 0;
					// _Rrotate(x->parent)
					n = x->parent;
					c = n->left;
					n->left = c->right;
					if (c->right != nil)
						c->right->parent = n;
					c->parent = n->parent;
					if (n == head->parent)
						head->parent = c;
					else if (n == n->parent->right)
						n->parent->right = c;
					else
						n->parent->left = c;
					c->right = n;
					n->parent = c;
					w = x->parent->left;
				}
				if (w->right->color == 1 && w->left->color == 1) {
					w->color = 0;
					x = x->parent;
				} else {
					if (w->left->color == 1) {
						w->right->color = 1;
						w->color = 0;
						// _Lrotate(w)
						c = w->right;
						w->right = c->left;
						if (c->left != nil)
							c->left->parent = w;
						c->parent = w->parent;
						if (w == head->parent)
							head->parent = c;
						else if (w == w->parent->left)
							w->parent->left = c;
						else
							w->parent->right = c;
						c->left = w;
						w->parent = c;
						w = x->parent->left;
					}
					w->color = x->parent->color;
					x->parent->color = 1;
					w->left->color = 1;
					// _Rrotate(x->parent)
					n = x->parent;
					c = n->left;
					n->left = c->right;
					if (c->right != nil)
						c->right->parent = n;
					c->parent = n->parent;
					if (n == head->parent)
						head->parent = c;
					else if (n == n->parent->right)
						n->parent->right = c;
					else
						n->parent->left = c;
					c->right = n;
					n->parent = c;
					break;
				}
			}
		}
		x->color = 1;
	}

	OperatorDelete(y);
	tree->size--;
	*ret = pos;
	return ret;
}

/*
 * 0x004E6A30 - std::_Tree::erase range
 *
 * Erases all nodes in [first, last) and returns the iterator that follows
 * the erased range. When the range is the whole tree it runs
 * _Erase(_Root()) with the first level inlined, as the binary does:
 * StdTree_EraseSubtree takes each right subtree and the loop frees the
 * nodes down the left spine. Otherwise it erases one by one. The binary
 * returns the iterator through a hidden sret pointer, and we return it
 * directly, since the port models a tree iterator as the bare node
 * pointer. Its std::_Lockit brackets are omitted as elsewhere.
 */
static StdTreeNode *
StdTree_EraseRange(StdMapTree *tree, StdTreeNode *first, StdTreeNode *last)
{
	StdTreeNode *head = tree->head;
	StdTreeNode *tmp, *x, *y;

	if (tree->size != 0 && first == head->left && last == head) {
		x = head->parent;
		for (y = x; y != g_HandleMapNil; x = y) {
			StdTree_EraseSubtree(tree, y->right);
			y = y->left;
			OperatorDelete(x);
		}
		head->parent = g_HandleMapNil;
		tree->size = 0;
		head->left = head;
		head->right = head;
		return head->left;
	}

	// Partial range: erase(first++) one by one.
	while (first != last) {
		StdTreeNode *old = first;
		StdTree_Inc(&first);
		StdTree_RBErase(tree, &tmp, old);
	}
	return first;
}

/*
 * 0x004E6C40 - std::_Tree::_Erase (recursive subtree destruction)
 *
 * Recursively erases the right subtree, then iteratively follows left
 * children, freeing each node. Stops at the nil sentinel.
 *
 * The binary brackets the walk in the std::_Lockit critical-section
 * guard, 0x005C0310 on the way in and 0x005C03D0 on the way out. That
 * is MSVC CRT locking and the server is single-threaded, so it is not
 * reproduced.
 */
static void
StdTree_EraseSubtree(StdMapTree *tree, StdTreeNode *node)
{
	USED(tree);
	while (node != g_HandleMapNil) {
		StdTree_EraseSubtree(tree, node->right);
		StdTreeNode *next = node->left;
		OperatorDelete(node);
		node = next;
	}
}

/*
 * 0x004E6CD0 - std::_Tree::_Insert (RB insert with fixup)
 *
 * Allocates a new 0x18-byte node, links it as a child of parent, and
 * performs red-black rebalancing. Updates head's leftmost/rightmost/root.
 *
 * MODIFIED: the binary decides the side with
 * `parent == head || _X != _Nil || key < parent->key` (0x004E6D44 through
 * 0x004E6D66), taking a node pointer where this takes a boolean. Its only
 * caller passes _Nil for that pointer at all three sites, so the live
 * decision is the first and third terms, and StdTree_Insert hoists the
 * third into `addLeft`. The composition matches; this function on its own
 * does not, and would attach right where the binary attaches left if it
 * were ever called with addLeft = 0 and key < parent->key.
 */
static StdTreeNode *
StdTree_RBInsert(StdMapTree *tree, int addLeft, StdTreeNode *parent, uintptr_t key, uintptr_t value)
{
	StdTreeNode *head = tree->head;
	StdTreeNode *nil = g_HandleMapNil;
	StdTreeNode *node;
	StdTreeNode *x;

	// Allocate and initialize the new node.
	node = OperatorNew(sizeof(StdTreeNode));
	node->parent = parent;
	node->color = 0; // red
	node->left = nil;
	node->right = nil;
	node->key = key;
	node->value = value;

	tree->size++;

	// Link the node into the tree.
	if (parent == head) {
		// First node: becomes root.
		head->left = node;      // leftmost
		head->parent = node;    // root
		head->right = node;     // rightmost
	} else if (addLeft) {
		parent->left = node;
		if (parent == head->left)
			head->left = node; // new leftmost
	} else {
		parent->right = node;
		if (parent == head->right)
			head->right = node; // new rightmost
	}

	// Red-black fixup.
	x = node;
	while (x != head->parent && x->parent->color == 0) {
		StdTreeNode *p = x->parent;
		StdTreeNode *g = p->parent;

		if (p == g->left) {
			// Parent is left child of grandparent.
			StdTreeNode *uncle = g->right;
			if (uncle->color == 0) {
				// Case 1: uncle is red - recolor.
				p->color = 1;
				uncle->color = 1;
				g->color = 0;
				x = g;
			} else {
				if (x == p->right) {
					// Case 2: x is right child - left rotate at parent.
					x = p;
					// Left rotate at x.
					StdTreeNode *y = x->right;
					x->right = y->left;
					if (y->left != nil)
						y->left->parent = x;
					y->parent = x->parent;
					if (x == head->parent)
						head->parent = y;
					else if (x == x->parent->left)
						x->parent->left = y;
					else
						x->parent->right = y;
					y->left = x;
					x->parent = y;
				}
				// Case 3: x is left child - right rotate at grandparent.
				x->parent->color = 1;
				x->parent->parent->color = 0;
				{
					StdTreeNode *gg = x->parent->parent;
					StdTreeNode *y = gg->left;
					gg->left = y->right;
					if (y->right != nil)
						y->right->parent = gg;
					y->parent = gg->parent;
					if (gg == head->parent)
						head->parent = y;
					else if (gg == gg->parent->right)
						gg->parent->right = y;
					else
						gg->parent->left = y;
					y->right = gg;
					gg->parent = y;
				}
			}
		} else {
			// Mirror: parent is right child of grandparent.
			StdTreeNode *uncle = g->left;
			if (uncle->color == 0) {
				// Case 1: uncle is red - recolor.
				p->color = 1;
				uncle->color = 1;
				g->color = 0;
				x = g;
			} else {
				if (x == p->left) {
					// Case 2: x is left child - right rotate at parent.
					x = p;
					StdTreeNode *y = x->left;
					x->left = y->right;
					if (y->right != nil)
						y->right->parent = x;
					y->parent = x->parent;
					if (x == head->parent)
						head->parent = y;
					else if (x == x->parent->right)
						x->parent->right = y;
					else
						x->parent->left = y;
					y->right = x;
					x->parent = y;
				}
				// Case 3: x is right child - left rotate at grandparent.
				x->parent->color = 1;
				x->parent->parent->color = 0;
				{
					StdTreeNode *gg = x->parent->parent;
					StdTreeNode *y = gg->right;
					gg->right = y->left;
					if (y->left != nil)
						y->left->parent = gg;
					y->parent = gg->parent;
					if (gg == head->parent)
						head->parent = y;
					else if (gg == gg->parent->left)
						gg->parent->left = y;
					else
						gg->parent->right = y;
					y->left = gg;
					gg->parent = y;
				}
			}
		}
	}
	// Root must be black.
	head->parent->color = 1;

	return node;
}

/*
 * 0x004E6FF0 - std::_Tree::lower_bound
 *
 * Finds the first node with key >= search key. Returns head (end iterator)
 * if no such node exists. Compares as unsigned at node offset 0xC.
 */
StdTreeNode *
StdTree_LowerBound(StdMapTree *tree, uintptr_t key)
{
	StdTreeNode *head = tree->head;
	StdTreeNode *best = head;
	StdTreeNode *node = head->parent; // root
	StdTreeNode *nil = g_HandleMapNil;

	while (node != nil) {
		if (node->key < key) {
			// node_key < search_key: go right
			node = node->right;
		} else {
			// node_key >= search_key: update best, go left
			best = node;
			node = node->left;
		}
	}
	return best;
}

/*
 * 0x004E7040 - std::_Tree::_Dec (predecessor)
 *
 * Moves to the previous node in key order. Head detection: if node[0x14]
 * == 0 and node->parent->parent == node, this is the head node (end
 * iterator) and we return rightmost.
 *
 * The binary brackets the walk in the std::_Lockit critical-section
 * guard, 0x005C0310 on the way in and 0x005C03D0 on the way out, twice
 * over. That is MSVC CRT locking and the server is single-threaded, so
 * it is not reproduced.
 */
static void
StdTree_Dec(StdTreeNode **iter)
{
	StdTreeNode *node = *iter;
	StdTreeNode *nil = g_HandleMapNil;

	// Head detection: color==0 and parent->parent==node means this
	// is the head sentinel (end iterator). Return rightmost.
	if (node->color == 0 && node->parent->parent == node) {
		*iter = node->right;
		return;
	}

	// If left child exists, find rightmost in left subtree.
	if (node->left != nil) {
		StdTreeNode *p = node->left;
		StdTreeNode *r = p->right;
		while (r != nil) {
			p = r;
			r = p->right;
		}
		*iter = p;
		return;
	}

	// Walk up while we're the left child.
	StdTreeNode *parent = node->parent;
	if (*iter == parent->left) {
		do {
			*iter = parent;
			parent = parent->parent;
		} while (*iter == parent->left);
	}
	*iter = parent;
}

/*
 * 0x004E7100 - std::_Tree::_Inc (successor)
 *
 * Advances to the next node in key order. If right child exists, finds
 * leftmost in right subtree. Otherwise walks up until we're no longer a
 * right child.
 *
 * The binary brackets the walk in the std::_Lockit critical-section
 * guard, 0x005C0310 on the way in and 0x005C03D0 on the way out, twice
 * over. That is MSVC CRT locking and the server is single-threaded, so
 * it is not reproduced.
 */
static void
StdTree_Inc(StdTreeNode **iter)
{
	StdTreeNode *node = *iter;
	StdTreeNode *nil = g_HandleMapNil;
	StdTreeNode *right = node->right;

	if (right != nil) {
		// Find leftmost in right subtree.
		StdTreeNode *p = right->left;
		while (p != nil) {
			right = p;
			p = right->left;
		}
		*iter = right;
	} else {
		// Walk up while we're the right child.
		StdTreeNode *parent = node->parent;
		while (*iter == parent->right) {
			*iter = parent;
			parent = parent->parent;
		}
		// Final check: if current->right != parent, update.
		// This handles the root-to-head transition.
		if ((*iter)->right != parent)
			*iter = parent;
	}
}
