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
#include "stddeque.h"
#include "terrain.h"

#include "weapon.h"

static StdPtrIterFull *StdPtrIter_Begin(StdPtrIterFull *this, StdPtrNode *sentinel); // 0x00420E50
static StdPtrIterFull *StdPtrIter_Next(StdPtrIterFull *this); // 0x00420E80
static StdPtrIterFull *StdPtrIter_Prev(StdPtrIterFull *this); // 0x00420EA0
static int StdPtrIter_IsValid(StdPtrIterFull *this); // 0x00420F40
static void StdMap_Destructor(StdMapTree *this); // 0x00421450
static void *StdTree_Ucopy_19D0(StdAllocator *this, void *first, void *last, void *dest); // 0x004219D0
static void StdTree_UfillN_1A10(StdAllocator *this, void *dest, uint32_t count, void *valuePtr); // 0x00421A10
static void *StdMap_Init(StdMapTree *this, void *arg1, int unused, void *arg3); // 0x00421A50
void *StdMap_Begin(StdMapTree *this, void *outIter); // 0x00421A90
static void *StdTree_Lbound(StdMapTree *this, void *outIter, void *keyPtr); // 0x00421AC0
static void *StdTree_RBInsert_Wombat(StdMapTree *this, void *outIter, void *addLeft, void *parent, void *pair); // 0x00422970
static void StdMap_TreeErase(StdMapTree *this, void *outIter, void *pos); // 0x00421CC0
static void *StdMap_Insert(StdMapTree *this, void *outIter, void *beginIter, void *endIter); // 0x00422510
static void *StdMap_FindImpl(StdMapTree *this, void *outIter, void *keyPtr); // 0x00422650
static void Destroy4_Range_Terrain(CVector *self, void *first, void *last); // 0x00422740
static void vector_DestroyRange(void *first, void *last); // 0x00422740
static uintptr_t SortMulti_NoOp(uintptr_t arg); // 0x00422770
static int StdMap_IntKeyLess(StdMapTree *this, void *a, void *b); // 0x00422780
static void *StdTreeNode_Key(StdTreeNode *node); // 0x004E3550
static void StdTree_Inc(StdTreeNode **iter); // 0x004E7100
static void StdTree_Dec(StdTreeNode **iter); // 0x004E7040
static StdTreeNode *StdTree_RBInsert(StdMapTree *tree, int addLeft, StdTreeNode *parent, uintptr_t key, uintptr_t value); // 0x004E6CD0
static void StdTree_EraseSubtree(StdMapTree *tree, StdTreeNode *node); // 0x004E6C40
static void StdTree_EraseRange(StdMapTree *tree, StdTreeNode *first, StdTreeNode *last); // 0x004E6A30
static void *StdMap_Find(StdMapTree *this, void *outIter, void *keyPtr); // 0x004227B0
static void StdMap_EraseRecursive(StdMapTree *this, void *node); // 0x004227E0
static void StdMap_TreeInit(StdMapTree *this); // 0x00422890
static void *StdMap_GetHead(StdMapTree *this); // 0x00422E30
static void StdMap_TreeSplice(StdMapTree *this, void *node); // 0x00422E50
static void *StdMap_TreeMin(void *node); // 0x00422FC0
static void *StdMap_TreeMax(void *node); // 0x00423040
static void *StdMap_GetRoot(StdMapTree *this); // 0x004230C0
static void StdMap_TreeSplice2(StdMapTree *this, void *node); // 0x004230E0
static void *StdMap_IterPostDec(void *iter, void *outIter, int dummy); // 0x00423270
static void *StdMap_IterDec(void *iter); // 0x004232A0
static void *StdTree_Buynode(StdMapTree *this, void *parentNode, int value); // 0x004235B0
static void StdTree_ConstructorPair(StdAllocator *this, void *node, void *pair); // 0x00423600
static void *StdMap_LboundWrapper(void *iter); // 0x00423620
static void StdMap_TreeDec(void *iter); // 0x00423640
static void *StdAllocator_Ucopy(StdAllocator *this, void *first, void *last, void *dest); // 0x00423750
static void StdAllocator_UfillN(StdAllocator *this, void *dest, uint32_t count, void *valuePtr); // 0x00423790
static void *StdMap_GetMost(StdMapTree *this); // 0x004237D0
static void *Vector_AllocElements(int count); // 0x00423960
static void *StdTree_ConstructorNode(void *node, void *pair); // 0x00423990
static void SortByZ_Main(uintptr_t *begin, uintptr_t *end, char typeTag, int depth); // 0x00423A10
static void SortByType_Entry(uintptr_t *begin, uintptr_t *end, uint8_t typeTag, int depth); // 0x00423AB0
static void SortByZ_InsertionEntry(uintptr_t *begin, uintptr_t *end, char typeTag); // 0x00423B90
static void SortByZ_Quicksort(uintptr_t *begin, uintptr_t *end, char typeTag, int depth); // 0x00423BC0
static void SortByZ_UnguardedInsert(uintptr_t *pos, uintptr_t val, char typeTag); // 0x00423C90
static int SortByZ_Compare(uintptr_t a, uintptr_t b); // 0x00423CE0
static int SortByType_Compare(uintptr_t a, uintptr_t b); // 0x00423E60
static void SortByType_MoveBackward(uintptr_t *pos, uintptr_t value, uint8_t typeTag); // 0x00423E10
static void SortByType_Quicksort(uintptr_t *begin, uintptr_t *end, uint8_t typeTag, int depth); // 0x00423D40
static void SortByType_InsertionEntry(uintptr_t *begin, uintptr_t *end, uint8_t typeTag); // 0x00423D10
static void SortByZ_Insertion(uintptr_t *begin, uintptr_t *end, char typeTag, int depth); // 0x00423F60
static uintptr_t SortByZ_Median3(uintptr_t a, uintptr_t b, uintptr_t c, char typeTag); // 0x00423FF0
static uintptr_t *SortByZ_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, char typeTag); // 0x004240C0
static uintptr_t *SortByType_Partition(uintptr_t *lo, uintptr_t *hi, uintptr_t pivot, uint8_t typeTag); // 0x004242A0
static uintptr_t SortByType_MedianOfThree(uintptr_t a, uintptr_t b, uintptr_t c, uint8_t typeTag); // 0x004241D0
static void SortByType_InsertionSort(uintptr_t *begin, uintptr_t *end, uint8_t typeTag, int depth); // 0x00424140
static void vector_SwapImpl(uintptr_t *a, uintptr_t *b); // 0x00424340
// 0x00424DD0 - StdPtrList_ScalarDelete_4DD0 exposed in stl.h
static void StdPtrList_ClearAndDestroy_4D70(StdPtrList *list); // 0x00424D70
static void StdPtrList_Insert(StdPtrList *this, void *resultIter, void *searchPos, void *value); // 0x00424EC0
static void StdPtrList_DoInsert_424FC0(StdPtrList *this, void *dest, void *source); // 0x00424FC0
static void *CVector_ConstructorWrapper(CVector *this); // 0x00426440
static void *CVector_Ucopy_67A0(CVector *this, void *first, void *last, void *dest); // 0x004267A0
static void CVector_UfillN_67E0(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x004267E0
static void *StdPtrList_EraseRange_6DB0(StdPtrList *list, void *resultIter, StdPtrNode *beginNode, StdPtrNode *endNode); // 0x00426DB0
static void StdPtrList_ClearAndDestroy_6C40(StdPtrList *list); // 0x00426C40
static void *CVector_Ucopy_E990(CVector *this, void *first, void *last, void *dest); // 0x0042E990
static void CVector_UfillN_E9D0(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x0042E9D0
static void *CVector_Ucopy_EA10(CVector *this, void *first, void *last, void *dest); // 0x0042EA10
static void CVector_UfillN_EA50(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x0042EA50
static void *CVector_Ucopy_EA90(CVector *this, void *first, void *last, void *dest); // 0x0042EA90
static void CVector_UfillN_EAD0(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x0042EAD0
static uintptr_t *CVector_Ucopy(void *alloc, uintptr_t *begin, uintptr_t *end, uintptr_t *dest); // 0x004301E0
static void CVector_UcopyN(void *alloc, uintptr_t *first, uint32_t count, uintptr_t *value); // 0x00430220
static void *CMapNode_ScalarDtor(CFragment *this, int flags); // 0x0044D710
static void *CMapIterator_ScalarDtor(CDefine *this, int flags); // 0x0044D740
static void SortRaw_Main(uintptr_t *begin, uintptr_t *end, int depth); // 0x00457FC0
static void SortRaw_InsertionEntry(uintptr_t *begin, uintptr_t *end); // 0x00458040
static void SortRaw_Quicksort(uintptr_t *begin, uintptr_t *end, int depth); // 0x00458070
static void SortRaw_UnguardedInsert(uintptr_t *pos, uintptr_t val); // 0x00458130
static void SortRaw_Insertion(uintptr_t *begin, uintptr_t *end, int depth); // 0x00458170
static uintptr_t SortRaw_Median3(uintptr_t a, uintptr_t b, uintptr_t c); // 0x004581E0
static uintptr_t *SortRaw_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot); // 0x00458260
static int SortRaw_SkipN(const char *str, int count); // 0x004582C0
static int CRT_atoi(char **pStr); // 0x00458854
static void StdPtrList_EraseRangeLogin(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last); // 0x0045AC30
static void StdPtrList_DestructorLogin(StdPtrList *list); // 0x0045AAF0
static void *StdPtrList_InitLogin(StdPtrList *this, const void *init); // 0x0045AAB0
static void *CFileEntry_ScalarDelete(CFileEntry *self, int flags); // 0x00459290
static void *CFileEntry_CopyConstructor(CFileEntry *self, CFileEntry *src); // 0x00459220
static void *FileEntry_Constructor(void *dst, void *src); // 0x004591E0
static void *FileEntry_DestructorWrapper(void *entry); // 0x004591D0
static void StdFileList_ConstructorWrapper(void *list, void *dst, void *src); // 0x004591B0
static void StdFileList_Destroy(void *list, void *element); // 0x00459190
static void StdFileList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x004590E0
static void StdFileList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos); // 0x00459020
static void StdFileList_PushBack(StdPtrList *list, void *value); // 0x00458FF0
static void *StdFileList_Init(StdPtrList *list, void *src); // 0x00458FB0
static void *CFileEntry_Constructor(CFileEntry *self, const char *name, int nameLength, int fileOffset, int fileSize); // 0x00458F20
static uint32_t CRT_divmod(uint32_t *pValue, uint32_t divisor); // 0x00458EF0
static void *StdPtrList_Init_EntityMap(StdPtrList *list, void *typeBytePtr); // 0x00461FB0
static StdPtrNode **StdPtrList_EraseRangeGrid(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last); // 0x00462150
static void StdPtrList_DoInsertGrid(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x004620A0
static void StdPtrList_PushBackGrid(StdPtrList *list, void *value); // 0x00462070
static void Vector_SortByDistPair(uintptr_t *begin, uintptr_t *end, CLocation refLoc); // 0x00462390
static void *CVector_Ucopy_BA0(CVector *this, void *first, void *last, void *dest); // 0x00462BA0
static void CVector_UfillN_BE0(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x00462BE0
static void *CVector_Ucopy_C20(CVector *this, void *first, void *last, void *dest); // 0x00462C20
static void CVector_UfillN_C60(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x00462C60
static void SortByDist_Main_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x00462CA0
static void SortByDist_Main_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x00462DB0
static void SortByDist_InsertionEntry_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc); // 0x00462EC0
static void SortByDist_Quicksort_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x00462F30
static void SortByDist_UnguardedInsert_Pair(uintptr_t *pos, uintptr_t val, CLocation refLoc); // 0x00463080
static void SortByDist_InsertionEntry_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc); // 0x00463110
static void SortByDist_Quicksort_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x00463180
static void SortByDist_UnguardedInsert_Pair2(uintptr_t *pos, uintptr_t val, CLocation refLoc); // 0x004632D0
static int SortByDist_Compare(CLocation *refLoc, uintptr_t a, uintptr_t b); // 0x00463360
static void SortByDist_Insertion_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x004633B0
static uintptr_t SortByDist_Median3_Pair(uintptr_t a, uintptr_t b, uintptr_t c, CLocation refLoc); // 0x00463480
static uintptr_t *SortByDist_Partition_Pair(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation refLoc); // 0x004635A0
static void SortByDist_Insertion_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth); // 0x00463660
static uintptr_t SortByDist_Median3_Pair2(uintptr_t a, uintptr_t b, uintptr_t c, CLocation refLoc); // 0x00463730
static uintptr_t *SortByDist_Partition_Pair2(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation refLoc); // 0x00463850
static void *CRT_PrintlInit(uint32_t *this); // 0x0046CCA0
static void CVector_UfillN_CC60(CVector *this, uint32_t *dest, uint32_t count, uint32_t *src); // 0x0046CC60
static uint32_t *CVector_Ucopy_CC20(CVector *this, uint32_t *first, uint32_t *last, uint32_t *dest); // 0x0046CC20
static void CVector_Insert_CA00(CVector *this, uint32_t *pos, uint32_t count, uint32_t *value); // 0x0046CA00
static void *CVector_Insert_C990(CVector *this, uint32_t *pos, uint32_t *value); // 0x0046C990
static void CVector_PushBack_C960(CVector *this, uint32_t *value); // 0x0046C960
static void SortSurface_SwapImpl(SurfaceInfo *a, SurfaceInfo *b); // 0x0046C700
static void SortSurface_Swap(SurfaceInfo *a, SurfaceInfo *b); // 0x0046C6D0
static SurfaceInfo *SortSurface_Partition(SurfaceInfo *begin, SurfaceInfo *end, SurfaceInfo *pivot, char typeTag); // 0x0046C650
static SurfaceInfo *SortSurface_Median3(SurfaceInfo *result, SurfaceInfo *a, SurfaceInfo *b, SurfaceInfo *c, char typeTag); // 0x0046C540
static void SortSurface_Insertion(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth); // 0x0046C490
static void SortSurface_Quicksort(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth); // 0x0046C260
static void SortSurface_InsertionEntry(SurfaceInfo *begin, SurfaceInfo *end, char typeTag); // 0x0046C230
static void SortSurface_Main(SurfaceInfo *begin, SurfaceInfo *end, char typeTag, int depth); // 0x0046C170
static void SortSurface_Entry(SurfaceInfo *begin, SurfaceInfo *end, char typeTag); // 0x0046BEA0
static void CVector_UfillNSI(CVector *this, SurfaceInfo *dest, uint32_t count, SurfaceInfo *src); // 0x0046BE40
static SurfaceInfo *CVector_UcopySI(CVector *this, SurfaceInfo *first, SurfaceInfo *last, SurfaceInfo *dest); // 0x0046BE00
static uint32_t CVector_GetCountC(CVector *list); // 0x0046BDC0
static void CVector_InsertSI(CVector *this, SurfaceInfo *pos, uint32_t count, SurfaceInfo *value); // 0x0046BB80
static void *CVector_InsertAtSI(CVector *this, uint32_t index, SurfaceInfo *value); // 0x0046BB00
static void CVector_PushBackSI(CVector *this, SurfaceInfo *value); // 0x0046BA80
static void CVector_DestructorSI(CVector *this); // 0x0046BA10
static void *CVector_Allocate4(CVector *this, uint32_t count); // 0x0047A350
static void *Destroy1C_Range2(CVector *this, int count, int unused_arg); // 0x00479B50
static CVector *CVector_AssignOp4(CVector *this, CVector *src); // 0x004791F0
static CVector *CVector_CopyConstruct4(CVector *this, CVector *src); // 0x00479180
static void *SortMultiDist_Partition(void *first, void *last, uintptr_t pivot, CLocation cmpLoc); // 0x0047D3E0
static uintptr_t SortMultiDist_Median3(uintptr_t a, uintptr_t b, uintptr_t c, CLocation cmpLoc); // 0x0047D310
static void *SortMultiInt_Partition(void *first, void *last, uintptr_t pivot, int cmpVal); // 0x0047D200
static uintptr_t SortMultiInt_Median3(uintptr_t a, uintptr_t b, uintptr_t c, int cmpVal); // 0x0047D130
static void SortMultiDist_Quicksort(void *first, void *last, CLocation cmpLoc, int unused); // 0x0047C970
static void SortMultiInt_Quicksort(void *first, void *last, uint8_t cmpVal, int unused); // 0x0047C810
static void SortByDist_Main(uintptr_t *begin, uintptr_t *end, CLocation *refLoc, int depth); // 0x0047F3F0
static void SortByDist_InsertionEntry(uintptr_t *begin, uintptr_t *end, CLocation *refLoc); // 0x0047F500
static void SortByDist_Quicksort(uintptr_t *begin, uintptr_t *end, CLocation *refLoc, int depth); // 0x0047F570
static void SortByDist_UnguardedInsert(uintptr_t *pos, uintptr_t val, CLocation *refLoc); // 0x0047F6C0
static void SortByDist_Insertion(uintptr_t *begin, uintptr_t *end, CLocation *refLoc, int depth); // 0x0047F750
static uintptr_t SortByDist_Median3(uintptr_t a, uintptr_t b, uintptr_t c, CLocation *refLoc); // 0x0047F820
static uintptr_t *SortByDist_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation *refLoc); // 0x0047F940
static void *CVector_VecDestructor_Region(CVector *this, int flags); // 0x004A6410
static void StdPtrList_DestructorWrapper_EntityMgr(void); // 0x00491DF0
static void StdPtrList_EraseRange16(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last); // 0x00484B40
static void StdPtrList16_DestroyAll(StdPtrList *list); // 0x00484A00
static void *StdPtrList_Constructor_NPC(StdPtrList *this, const void *init); // 0x004849C0
static void StdPtrList16_EraseAll(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last); // 0x004848D0
static void StdPtrList_Insert16(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x00484820
static void StdPtrList16_InsertEnd(StdPtrList *list, void *value); // 0x00484680
static void *StdPtrList16_Constructor(StdPtrList *this, const void *init); // 0x004845E0
static void UninitFillN_CRegionPtr(void **first, uint32_t count, void **value); // 0x004A6830
static void **UninitCopy_CRegionPtr(void **first, void **last, void **dest); // 0x004A67F0
static void *CVector_Ucopy_F490(CVector *this, void *first, void *last, void *dest); // 0x004CF490
static void CVector_UfillN_F4D0(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x004CF4D0
static void *CVector_Ucopy_F510(CVector *this, void *first, void *last, void *dest); // 0x004CF510
static void CVector_UfillN_F550(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x004CF550
static void *CVector_Ucopy_F590(CVector *this, void *first, void *last, void *dest); // 0x004CF590
static void CVector_UfillN_F5D0(CVector *this, void *dest, uint32_t count, void *valuePtr); // 0x004CF5D0
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
 * 0x004019C0 - CVector::GetCount (16-byte elements, 52 bytes)
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

		void *newbuf = malloc(total * sizeof(CString));

		// Copy [begin, pos) to newbuf
		void *newpos = Construct_Range16(this, this->begin, pos, newbuf);

		// Fill count elements at newpos with val
		ConstructN_16((StdAllocator *)this, newpos, count, val);

		// Copy [pos, end) after filled region
		Construct_Range16(this, pos, this->end, (char *)newpos + count * sizeof(CString));

		// Destroy old elements
		Destroy_Range16((StdAllocator *)this, this->begin, this->end);

		// Free old buffer (binary: 0x0046c9e0 deallocator)
		free(this->begin);

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
			Destroy_RangeFwd16(pos, this->end, val);

			// Update end
			this->end = (char *)this->end + count * sizeof(CString);
		} else if (count > 0) {
			// tail >= count, count > 0
			// Copy last count elements past end
			Construct_Range16(this, (char *)this->end - count * sizeof(CString), this->end, this->end);

			// Shift [pos, end - count*16) backward toward end
			Destroy_RangeBwd16(pos, (char *)this->end - count * sizeof(CString), this->end);

			// Fill [pos, pos + count*16) with val
			Destroy_RangeFwd16(pos, (char *)pos + count * sizeof(CString), val);

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
CVector_Allocate16(CVector *this, uint32_t count)
{
	USED(this);
	return Allocate16_Inner(count);
}

/*
 * 0x00404610 - std::_Tree::_Size accessor
 *
 * Returns the _Size field of the tree subobject.
 */
int
StdTree_GetSize(StdMapTree *tree)
{
	return (int)tree->size;
}

/*
 * 0x004046E0 - std::_Constructor range copy (copy 16-byte elements)
 *
 * Iterates from first to last in steps of 16, calling
 * String_CopyAssignDispatch on each with dest pointer. Returns final dest
 * pointer.
 */
__attribute__((unused)) void *
Construct_RangeCopy16(void *first, void *last, void *dest)
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
 * 0x00404730 - std::_Destroy range (forward, 16-byte elements)
 *
 * Iterates from first to last in steps of 16, calling
 * String_CopyAssignDispatch on each with value.
 */
void
Destroy_RangeFwd16(void *first, void *last, void *value)
{
	CSdbStr *f = (CSdbStr *)first;
	CSdbStr *l = (CSdbStr *)last;
	while (f != l) {
		String_CopyAssignDispatch(f, value);
		f++;
	}
}

/*
 * 0x00404760 - std::_Destroy range (backward, 16-byte elements)
 *
 * Iterates backward from last to first in steps of 16, calling
 * String_CopyAssignDispatch on each with dest-16. Returns final dest
 * pointer.
 */
void *
Destroy_RangeBwd16(void *first, void *last, void *dest)
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
 * 0x00404CA0 - std::_Tree::_Multi accessor
 *
 * Returns the _Multi flag, indicating whether duplicate keys are
 * allowed.
 */
char
StdTree_GetMulti(StdMapTree *tree)
{
	return (char)tree->multi;
}

/*
 * 0x004066F0 - std::list<void*>::iterator constructor
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
 * 0x00406A00 - std::fill (uint32_t forward fill, 36 bytes)
 *
 * Fills [first, last) with the dword value pointed to by value_ptr,
 * stepping by 4 bytes.
 */
void
vector_Fill(void *first, void *last, void *value)
{
	uint32_t *p = first;
	uint32_t *e = last;

	while (p != e) {
		*p = *(uint32_t *)value;
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
static __attribute__((unused)) StdPtrIterFull *
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
static __attribute__((unused)) StdPtrIterFull *
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
 * 0x004213D0 - std::map::lower_bound wrapper
 *
 * Creates a local iterator, calls StdTree_Lbound on the key, then copies
 * the result to output.
 */
void *
StdMap_LowerBound(StdMapTree *this, void *outIter, void *keyPtr)
{
	uintptr_t localIter[2];

	StdTree_Lbound(this, localIter, keyPtr);
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
 * Gets begin/end iterators, runs StdMap_Insert to clear the tree, frees
 * the head node, and decrements the global tree reference count.
 */
static void
StdMap_Destructor(StdMapTree *this)
{
	void *endIter, *beginIter, *insertIter;

	StdPtrList_End((StdPtrList *)this, (StdPtrNode **)&endIter);
	StdMap_Begin(this, &beginIter);
	StdMap_Insert(this, &insertIter, *(uint32_t **)&beginIter, *(uint32_t **)&endIter);
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
 * 0x004219D0 - std::_Ucopy range (std::_Tree instantiation 1)
 *
 * Copies 4-byte elements from [first, last) to dest by calling construct
 * (0x00424FC0) per element. Returns pointer past the last written
 * destination element.
 */
static __attribute__((unused)) void *
StdTree_Ucopy_19D0(StdAllocator *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x00421A10 - std::_Ufill_n (std::_Tree instantiation 1)
 *
 * Fills count 4-byte elements at dest by calling construct (0x00424FC0)
 * per element with the value pointed to by valuePtr.
 */
static __attribute__((unused)) void
StdTree_UfillN_1A10(StdAllocator *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
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
StdTree_Lbound(StdMapTree *this, void *outIter, void *keyPtr)
{
	StdMapTree *tree = this;
	void *where = tree->head;
	void *node;
	uint8_t addLeft = 1;

	node = *(void **)StdMap_GetMost(this);

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

	tmp = StdMap_IterPostDec(&pos, &postDecOut, 0);
	y = *(void **)tmp;
	z = y;

	if (*(void **)StdPtrNode_GetNext((StdPtrNode *)y) == g_StdTreeNilNode) {
		x = *(void **)StdPtrNode_GetValue((StdPtrNode *)y);
	} else if (*(void **)StdPtrNode_GetValue((StdPtrNode *)y) == g_StdTreeNilNode) {
		x = *(void **)StdPtrNode_GetNext((StdPtrNode *)y);
	} else {
		y = StdMap_TreeMax(*(void **)StdPtrNode_GetValue((StdPtrNode *)y));
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

		if (*(void **)StdMap_GetMost(this) == z) {
			*(void **)StdMap_GetMost(this) = y;
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

		vector_SwapImpl((uintptr_t *)StdTreeNode_Value(y), (uintptr_t *)StdTreeNode_Value(z));

		y = z;
	} else {
		{
			void *zPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)z);
			void *xPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)x);
			*(void **)xPrevAddr = *(void **)zPrevAddr;
		}

		if (*(void **)StdMap_GetMost(this) == z) {
			*(void **)StdMap_GetMost(this) = x;
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
				*(void **)StdMap_GetHead(this) = StdMap_TreeMax(x);
			}
		}

		if (*(void **)StdMap_GetRoot(this) == z) {
			if (*(void **)StdPtrNode_GetNext((StdPtrNode *)z) == g_StdTreeNilNode) {
				void *zPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)z);
				*(void **)StdMap_GetRoot(this) = *(void **)zPrevAddr;
			} else {
				*(void **)StdMap_GetRoot(this) = StdMap_TreeMin(x);
			}
		}
	}

	if (*(int *)StdTreeNode_Value(y) != 1)
		goto done;

	for (;;) {
		if (x == *(void **)StdMap_GetMost(this))
			break;
		if (*(int *)StdTreeNode_Value(x) != 1)
			break;

		{
			void *xParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
			if (x == *(void **)StdPtrNode_GetNext((StdPtrNode *)xParent)) {
				w = *(void **)StdPtrNode_GetValue((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));

				if (*(int *)StdTreeNode_Value(w) == 0) {
					*(int *)StdTreeNode_Value(w) = 1;
					*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 0;
					StdMap_TreeSplice(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
					w = *(void **)StdPtrNode_GetValue((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				if (*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) == 1 &&
				        *(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Value(w) = 0;
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					continue;
				}

				if (*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) = 1;
					*(int *)StdTreeNode_Value(w) = 0;
					StdMap_TreeSplice2(this, w);
					w = *(void **)StdPtrNode_GetValue((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				*(int *)StdTreeNode_Value(w) = *(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) = 1;
				StdMap_TreeSplice(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				break;
			} else {
				w = *(void **)StdPtrNode_GetNext((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));

				if (*(int *)StdTreeNode_Value(w) == 0) {
					*(int *)StdTreeNode_Value(w) = 1;
					*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 0;
					StdMap_TreeSplice2(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
					w = *(void **)StdPtrNode_GetNext((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				if (*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) == 1 &&
				        *(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Value(w) = 0;
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					continue;
				}

				if (*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) == 1) {
					*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetValue((StdPtrNode *)w)) = 1;
					*(int *)StdTreeNode_Value(w) = 0;
					StdMap_TreeSplice(this, w);
					w = *(void **)StdPtrNode_GetNext((StdPtrNode *)*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				}

				*(int *)StdTreeNode_Value(w) = *(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetNext((StdPtrNode *)w)) = 1;
				StdMap_TreeSplice2(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)x));
				break;
			}
		}
	}

	*(int *)StdTreeNode_Value(x) = 1;

done: {
	void *kvPtr = StdTreeNode_KeyValuePtr(y);
	CVector_Destroy6_Single((CVector *)this, kvPtr);
}
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
StdMap_Insert(StdMapTree *this, void *outIter, void *beginIter, void *endIter)
{
	uintptr_t localBegin, localEnd;
	uintptr_t postDec;
	uintptr_t eraseOut;

	if (CSearchCtx_GetValNode((CSearchCtx *)this) == 0)
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

		mostPtr = StdMap_GetMost(this);
		StdMap_EraseRecursive(this, *(void **)mostPtr);

		mostPtr = StdMap_GetMost(this);
		*(void **)mostPtr = g_StdTreeNilNode;

		this->size = 0;

		{
			void *headPtr = StdMap_GetHead(this);
			*(StdTreeNode **)headPtr = this->head;
		}
		{
			void *rootPtr = StdMap_GetRoot(this);
			*(StdTreeNode **)rootPtr = this->head;
		}

		StdMap_Begin(this, outIter);
		return outIter;
	}

erase_loop:
	while (StdPtrIter_Neq((StdPtrNode **)&beginIter, (StdPtrNode **)&endIter) & 0xFF) {
		void *decResult;

		decResult = StdMap_IterPostDec(&beginIter, &postDec, 0);
		StdMap_TreeErase(this, &eraseOut, *(void **)decResult);
	}
	*(uintptr_t *)outIter = *(uintptr_t *)&beginIter;
	return outIter;
}

/*
 * 0x00422650 - std::map::find implementation
 *
 * Calls StdMap_Find (0x004227B0) to get lower bound iterator. If found !=
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

	StdMap_Find(this, &findResult, keyPtr);

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
 * Destroys range of 4-byte elements. For POD types this is a no-op.
 */
static void
Destroy4_Range_Terrain(CVector *self, void *first, void *last)
{
	USED(self);
	USED(first);
	USED(last);
}

/*
 * 0x00422740 - _Destroy range
 *
 * Iterates [first, last) calling allocator.destroy (0x00479FF0) per
 * element, which calls the element destructor (0x0045ACC0). For uint32_t
 * the destructor is empty.
 */
static void
vector_DestroyRange(void *first, void *last)
{
	uintptr_t *p = first;
	uintptr_t *e = last;

	while (p != e) {
		// 0x00479FF0 -> 0x0045ACC0 (empty function for uintptr_t)
		p++;
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
 * 0x004227A0 - std::_Tree::_Myval
 *
 * Returns pointer to the key/value data area within a red-black tree
 * node.
 */
void *
StdTreeNode_Value(StdTreeNode *node)
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
StdMap_Find(StdMapTree *this, void *outIter, void *keyPtr)
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
 * node: recurse on left child, then follow the right child iteratively.
 * Frees each node after destroying its key/value.
 */
static void
StdMap_EraseRecursive(StdMapTree *this, void *node)
{
	void *nextNode;

	for (;;) {
		if (node == g_StdTreeNilNode)
			return;

		// Recurse on left child
		{
			void **valPtr = StdPtrNode_GetValue((StdPtrNode *)node);
			StdMap_EraseRecursive(this, *(void **)valPtr);
		}

		// Save right child, then free current node
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
		void *rootPtr = StdMap_GetRoot(this);
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
		*(void **)StdMap_GetMost(this) = node;
		*(void **)StdMap_GetRoot(this) = node;
	} else {
		if (parent == *(void **)StdMap_GetHead(this))
			*(void **)StdMap_GetHead(this) = node;
	}
	goto start_fixup;

link_right:
	*(void **)StdPtrNode_GetValue((StdPtrNode *)parent) = node;
	if (parent == *(void **)StdMap_GetRoot(this))
		*(void **)StdMap_GetRoot(this) = node;

start_fixup:
	x = node;

	while (x != *(void **)StdMap_GetMost(this)) {
		void *xParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
		if (*(int *)StdTreeNode_Value(xParent) != 0)
			break;

		xParent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
		void *grandparent = *(void **)StdPtrNode_GetPrev((StdPtrNode *)xParent);

		if (xParent == *(void **)StdPtrNode_GetNext((StdPtrNode *)grandparent)) {
			uncle = *(void **)StdPtrNode_GetValue((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))));

			if (*(int *)StdTreeNode_Value(uncle) == 0) {
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Value(uncle) = 1;
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)));
			} else {
				if (x == *(void **)StdPtrNode_GetValue((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) {
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					StdMap_TreeSplice(this, x);
				}
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				StdMap_TreeSplice2(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x))));
			}
		} else {
			uncle = *(void **)StdPtrNode_GetNext((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))));

			if (*(int *)StdTreeNode_Value(uncle) == 0) {
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Value(uncle) = 1;
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)));
			} else {
				if (x == *(void **)StdPtrNode_GetNext((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) {
					x = *(void **)StdPtrNode_GetPrev((StdPtrNode *)x);
					StdMap_TreeSplice2(this, x);
				}
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)) = 1;
				*(int *)StdTreeNode_Value(*(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x)))) = 0;
				StdMap_TreeSplice(this, *(void **)StdPtrNode_GetPrev((StdPtrNode *)(*(void **)StdPtrNode_GetPrev((StdPtrNode *)x))));
			}
		}
	}

	*(int *)StdTreeNode_Value(*(void **)StdMap_GetMost(this)) = 1;

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
 * 0x00422E50 - std::_Tree::_Splice
 *
 * Relinks a node within the red-black tree during erase/rebalance. Swaps
 * the node with its left child's successor. Updates parent/child/prev
 * pointers and handles the _Most (rightmost) tracking.
 */
static void
StdMap_TreeSplice(StdMapTree *this, void *node)
{
	void *savedChild;
	void *childNextAddr;

	// Get left child of node
	savedChild = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);

	// Copy savedChild's next into node's value (left child slot)
	childNextAddr = StdPtrNode_GetNext((StdPtrNode *)savedChild);
	*(void **)StdPtrNode_GetValue((StdPtrNode *)node) = *(void **)childNextAddr;

	// If savedChild's next is not sentinel, fix its prev pointer
	if (*(void **)StdPtrNode_GetNext((StdPtrNode *)savedChild) != g_StdTreeNilNode) {
		void *childNext = *(void **)StdPtrNode_GetNext((StdPtrNode *)savedChild);
		*(void **)StdPtrNode_GetPrev((StdPtrNode *)childNext) = node;
	}

	// Copy node's prev into savedChild's prev
	{
		void *nodePrevAddr = StdPtrNode_GetPrev((StdPtrNode *)node);
		void *childPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)savedChild);
		*(void **)childPrevAddr = *(void **)nodePrevAddr;
	}

	// Update parent's pointer to node -> savedChild
	if (node == *(void **)StdMap_GetMost(this)) {
		*(void **)StdMap_GetMost(this) = savedChild;
	} else {
		void *parentNode = *(void **)StdPtrNode_GetPrev((StdPtrNode *)node);
		void *parentNextAddr = StdPtrNode_GetNext((StdPtrNode *)parentNode);
		if (node == *(void **)parentNextAddr) {
			*(void **)StdPtrNode_GetNext((StdPtrNode *)parentNode) = savedChild;
		} else {
			*(void **)StdPtrNode_GetValue((StdPtrNode *)parentNode) = savedChild;
		}
	}

	// Link: savedChild->next = node, node->prev = savedChild
	*(void **)StdPtrNode_GetNext((StdPtrNode *)savedChild) = node;
	*(void **)StdPtrNode_GetPrev((StdPtrNode *)node) = savedChild;
}

/*
 * 0x00422FC0 - std::_Tree::_Min (leftmost walk)
 *
 * Walks the tree from the given node following left-child (value)
 * pointers until reaching a node whose left child equals the sentinel.
 * Returns the last non-sentinel node found.
 */
static void *
StdMap_TreeMin(void *node)
{
	while (*(void **)StdPtrNode_GetValue((StdPtrNode *)node) != g_StdTreeNilNode) {
		node = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);
	}
	return node;
}

/*
 * 0x00423040 - std::_Tree::_Max (rightmost walk)
 *
 * Walks the tree from the given node following right-child (next)
 * pointers until reaching a node whose right child equals the sentinel.
 * Returns the last non-sentinel node found.
 */
static void *
StdMap_TreeMax(void *node)
{
	while (*(void **)StdPtrNode_GetNext((StdPtrNode *)node) != g_StdTreeNilNode) {
		node = *(void **)StdPtrNode_GetNext((StdPtrNode *)node);
	}
	return node;
}

/*
 * 0x004230C0 - std::_Tree::_Root accessor
 *
 * Returns pointer to the value field of the head node at this[+4]. In MSVC
 * STL the value field of the header node holds the root pointer.
 */
static void *
StdMap_GetRoot(StdMapTree *this)
{
	StdTreeNode *head = this->head;
	return StdPtrNode_GetValue((StdPtrNode *)head);
}

/*
 * 0x004230E0 - std::_Tree::_Splice2 (tree relink)
 *
 * Second variant of _Splice. Relinks a node with its right child's
 * predecessor. Mirror of StdMap_TreeSplice using next/value swapped.
 */
static void
StdMap_TreeSplice2(StdMapTree *this, void *node)
{
	void *savedChild;
	void *childValAddr;

	// Get right child of node
	savedChild = *(void **)StdPtrNode_GetNext((StdPtrNode *)node);

	// Copy savedChild's value (left) into node's next (right child slot)
	childValAddr = StdPtrNode_GetValue((StdPtrNode *)savedChild);
	*(void **)StdPtrNode_GetNext((StdPtrNode *)node) = *(void **)childValAddr;

	// If savedChild's value is not sentinel, fix its prev pointer
	if (*(void **)StdPtrNode_GetValue((StdPtrNode *)savedChild) != g_StdTreeNilNode) {
		void *childVal = *(void **)StdPtrNode_GetValue((StdPtrNode *)savedChild);
		*(void **)StdPtrNode_GetPrev((StdPtrNode *)childVal) = node;
	}

	// Copy node's prev into savedChild's prev
	{
		void *nodePrevAddr = StdPtrNode_GetPrev((StdPtrNode *)node);
		void *childPrevAddr = StdPtrNode_GetPrev((StdPtrNode *)savedChild);
		*(void **)childPrevAddr = *(void **)nodePrevAddr;
	}

	// Update parent's pointer to node -> savedChild
	if (node == *(void **)StdMap_GetMost(this)) {
		*(void **)StdMap_GetMost(this) = savedChild;
	} else {
		void *parentNode = *(void **)StdPtrNode_GetPrev((StdPtrNode *)node);
		void *parentValAddr = StdPtrNode_GetValue((StdPtrNode *)parentNode);
		if (node == *(void **)parentValAddr) {
			*(void **)StdPtrNode_GetValue((StdPtrNode *)parentNode) = savedChild;
		} else {
			*(void **)StdPtrNode_GetNext((StdPtrNode *)parentNode) = savedChild;
		}
	}

	// Link: savedChild->value = node, node->prev = savedChild
	*(void **)StdPtrNode_GetValue((StdPtrNode *)savedChild) = node;
	*(void **)StdPtrNode_GetPrev((StdPtrNode *)node) = savedChild;
}

/*
 * 0x00423270 - std::_Tree iterator post-decrement
 *
 * Saves the current node, decrements the iterator, and writes the saved
 * node into *outIter. Returns outIter.
 */
static void *
StdMap_IterPostDec(void *iter, void *outIter, int dummy)
{
	uintptr_t saved;

	USED(dummy);
	saved = *(uintptr_t *)iter;
	StdMap_LboundWrapper(iter);
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
 * Sets node = root via StdMap_GetMost+deref, where = head node (tree+4).
 * Loops while node is not the sentinel (g_StdTreeNilNode). If node key <
 * search key, go right (via StdPtrNode_GetValue). Otherwise update where
 * and go left (via StdPtrNode_GetNext). Returns where.
 */
void *
StdTree_FindInsertPos_Int(StdMapTree *tree, int *keyPtr)
{
	void *node;
	void *where;

	node = *(void **)StdMap_GetMost(tree);
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
	*(int *)StdTreeNode_Value(node) = value;
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
 * 0x00423620 - std::_Tree::_Lbound wrapper
 *
 * Calls StdTree_LowerBound_Int (0x004237F0) to perform lower-bound search.
 * Returns this.
 */
static void *
StdMap_LboundWrapper(void *iter)
{
	StdTree_LowerBound_Int(iter);
	return iter;
}

/*
 * 0x00423640 - std::_Tree::_Dec (iterator decrement, 264 bytes)
 *
 * Moves the iterator to the predecessor node in the red-black tree. MSVC
 * STL _Dec logic: handles three cases: 1. If node has color==0 and
 * parent's parent == node (header node), moves to header's right
 * (rightmost). 2. If node has a left child (GetNext != sentinel), finds
 * the rightmost node in the left subtree via StdMap_TreeMin. 3. Otherwise,
 * walks up the tree via parent pointers until finding a node that is not a
 * left child of its parent.
 */
static void
StdMap_TreeDec(void *iter)
{
	void **pNode = (void **)iter;
	void *node = *pNode;

	// Case 1: check if header node (color==0 and parent->parent==node)
	if (*(uint32_t *)StdTreeNode_Value(node) == 0) {
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
			*pNode = StdMap_TreeMin(leftChild);
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
 * 0x00423750 - std::_Ucopy range (std::_Tree instantiation 2, 62 bytes)
 *
 * Copies 4-byte elements from [first, last) to dest by calling construct
 * (0x00424FC0) per element. Returns pointer past the last written
 * destination element.
 */
static __attribute__((unused)) void *
StdAllocator_Ucopy(StdAllocator *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x00423790 - std::_Ufill_n (std::_Tree instantiation 2)
 *
 * Fills count 4-byte elements at dest by calling construct (0x00424FC0)
 * per element with the value pointed to by valuePtr.
 */
static __attribute__((unused)) void
StdAllocator_UfillN(StdAllocator *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x004237D0 - std::_Tree::_Most accessor
 *
 * Returns pointer to the prev field of the head node at this[+4]. In MSVC
 * STL the prev field of the header node holds the rightmost (max) node
 * pointer.
 */
static void *
StdMap_GetMost(StdMapTree *this)
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
 * the leftmost node of the right subtree via StdMap_TreeMax. Otherwise,
 * walks up via parent pointers while the current node is the right child
 * of its parent. Final guard: if the resulting node's right child does not
 * equal the last parent climbed, store parent.
 */
void
StdTree_LowerBound_Int(void *iter)
{
	void **pNode = (void **)iter;
	void *node;
	void *rightChild;
	void *parent;

	node = *pNode;
	rightChild = *(void **)StdPtrNode_GetValue((StdPtrNode *)node);

	if (rightChild != g_StdTreeNilNode) {
		*pNode = StdMap_TreeMax(rightChild);
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
 * Sort entry point for CVector of entity pointers. Calls
 * GameCentMon_GetPlayerCount (always returns 0) for depth limit, then
 * delegates to the main sort function.
 *
 * Callers: Script_areObjectsOn, CItem_CollectSurfaceItems,
 * CMultiSlave_GetItems.
 */
void
Vector_SortByZ(void *begin, void *end, char typeTag)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByZ_Main((uintptr_t *)begin, (uintptr_t *)end, typeTag, depth);
}

/*
 * 0x00423930 - Vector_SortByType
 *
 * Dispatches to SortByType_Entry, passing the unused depth-limit returned
 * by GameCentMon_GetPlayerCount.
 */
void
Vector_SortByType(void *begin, void *end, uint8_t typeTag)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByType_Entry((uintptr_t *)begin, (uintptr_t *)end, typeTag, depth);
}

/*
 * 0x00423960 - CVector element allocator
 *
 * Clamps count to >= 0, then allocates count * 4 bytes via operator new
 * (0x004E84C0). Returns pointer to allocated buffer.
 */
static __attribute__((unused)) void *
Vector_AllocElements(int count)
{
	if (count < 0)
		count = 0;
	return malloc((uint32_t)count * sizeof(uintptr_t));
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
 * 0x00423A10 - std::sort main
 *
 * If count <= 16, insertion sort directly. Otherwise, quicksort loop to
 * partitions of <=16 elements, then insertion sort the first 16, then
 * unguarded linear insertion on the rest.
 */
static void
SortByZ_Main(uintptr_t *begin, uintptr_t *end, char typeTag, int depth)
{
	int count;
	uintptr_t *cur;

	USED(depth);
	count = (int)(end - begin);
	if (count <= 16) {
		SortByZ_InsertionEntry(begin, end, typeTag);
		return;
	}
	SortByZ_Quicksort(begin, end, typeTag, 0);
	SortByZ_InsertionEntry(begin, begin + 16, typeTag);
	cur = begin + 16;
	while (cur != end) {
		SortByZ_UnguardedInsert(cur, *cur, typeTag);
		cur++;
	}
}

/*
 * 0x00423AA0 - GameCentMon::GetPlayerCount
 *
 * Binary stub: always returns 0. Called from std::sort wrappers (which
 * pass the begin pointer as an arg the function ignores) and from
 * BroadcastAll case 0 for server status packets.
 */
int
GameCentMon_GetPlayerCount(void)
{
	return 0;
}

/*
 * 0x00423AB0 - std::sort entry for Vector_SortByType
 *
 * Introsort dispatch: if count <= 16, uses insertion sort directly.
 * Otherwise, quicksort first, then insertion sort the first 16 elements,
 * then unguarded insertion sort on the rest. typeTag is passed through but
 * unused by comparison functions. The depth argument is passed by the
 * caller but ignored by this entry.
 */
static void
SortByType_Entry(uintptr_t *begin, uintptr_t *end, uint8_t typeTag, int depth)
{
	int count;
	uintptr_t *cur;

	USED(depth);
	count = (int)(end - begin);
	if (count <= 16) {
		SortByType_InsertionEntry(begin, end, typeTag);
		return;
	}
	SortByType_Quicksort(begin, end, typeTag, 0);
	SortByType_InsertionEntry(begin, begin + 16, typeTag);
	cur = begin + 16;
	while (cur != end) {
		SortByType_MoveBackward(cur, *cur, typeTag);
		cur++;
	}
}

/*
 * 0x00423B90 - std::sort insertion entry
 *
 * Wrapper for insertion sort. Calls GameCentMon_GetPlayerCount (returns 0)
 * for depth, then delegates to the actual insertion sort.
 */
static void
SortByZ_InsertionEntry(uintptr_t *begin, uintptr_t *end, char typeTag)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByZ_Insertion(begin, end, typeTag, depth);
}

/*
 * 0x00423BC0 - std::sort quicksort loop
 *
 * Quicksort with median-of-three pivot. Recurses on smaller half, iterates
 * on larger. Stops at partitions of <=16 elements.
 */
static void
SortByZ_Quicksort(uintptr_t *begin, uintptr_t *end, char typeTag, int depth)
{
	uintptr_t pivot;
	uintptr_t *mid;
	int rightCount, leftCount;

	USED(depth);
	for (;;) {
		if ((int)(end - begin) <= 16)
			return;
		pivot = SortByZ_Median3(*begin, begin[(int)(end - begin) / 2], *(end - 1), typeTag);
		mid = SortByZ_Partition(begin, end, pivot, typeTag);
		rightCount = (int)(end - mid);
		leftCount = (int)(mid - begin);
		if (rightCount > leftCount) {
			SortByZ_Quicksort(begin, mid, typeTag, GameCentMon_GetPlayerCount());
			begin = mid;
		} else {
			SortByZ_Quicksort(mid, end, typeTag, GameCentMon_GetPlayerCount());
			end = mid;
		}
	}
}

/*
 * 0x00423C90 - std::sort unguarded linear insert
 *
 * Shifts elements right from pos while val.z < preceding element.z, then
 * places val at the correct sorted position.
 */
static void
SortByZ_UnguardedInsert(uintptr_t *pos, uintptr_t val, char typeTag)
{
	uintptr_t *cur = pos;

	USED(typeTag);
	for (;;) {
		cur--;
		if (!SortByZ_Compare(val, *cur))
			break;
		*pos = *cur;
		pos = cur;
	}
	*pos = val;
}

/*
 * 0x00423CE0 - sort comparator
 *
 * Returns 1 when a.z < b.z (ascending z order).
 */
static int
SortByZ_Compare(uintptr_t a, uintptr_t b)
{
	int zA, zB;

	zA = CEntity_GetLocation((CEntity *)a)->z;
	zB = CEntity_GetLocation((CEntity *)b)->z;
	return zA < zB;
}

/*
 * 0x00423D10 - std::sort insertion entry for SortByType
 *
 * Calls GameCentMon_GetPlayerCount (returns 0) for depth limit, then
 * delegates to SortByType_InsertionSort. typeTag and depth are passed
 * through to InsertionSort in the binary but unused.
 */
static void __attribute__((unused))
SortByType_InsertionEntry(uintptr_t *begin, uintptr_t *end, uint8_t typeTag)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByType_InsertionSort(begin, end, typeTag, depth);
}

/*
 * 0x00423D40 - _Quicksort
 *
 * Recursive quicksort with median-of-three pivot. Stops at partitions of
 * 16 or fewer elements. Tail-call optimized: recurses on smaller half,
 * loops on larger.
 */
static void
SortByType_Quicksort(uintptr_t *begin, uintptr_t *end, uint8_t typeTag, int depth)
{
	uintptr_t pivot;
	uintptr_t *mid;
	int rightCount, leftCount;

	USED(depth);
	for (;;) {
		if ((int)(end - begin) <= 16)
			return;
		pivot = SortByType_MedianOfThree(*begin, begin[(int)(end - begin) / 2], *(end - 1), typeTag);
		mid = SortByType_Partition(begin, end, pivot, typeTag);
		rightCount = (int)(end - mid);
		leftCount = (int)(mid - begin);
		if (rightCount > leftCount) {
			SortByType_Quicksort(begin, mid, typeTag, GameCentMon_GetPlayerCount());
			begin = mid;
		} else {
			SortByType_Quicksort(mid, end, typeTag, GameCentMon_GetPlayerCount());
			end = mid;
		}
	}
}

/*
 * 0x00423E10 - _Move_backward
 *
 * Shifts elements right from pos, inserting value at the correct sorted
 * position. Walks backward while compare(value, *(pos-1)).
 */
static void
SortByType_MoveBackward(uintptr_t *pos, uintptr_t value, uint8_t typeTag)
{
	uintptr_t *cur;

	USED(typeTag);

	cur = pos;
	while (SortByType_Compare(value, *(cur - 1))) {
		*cur = *(cur - 1);
		cur--;
	}
	*cur = value;
}

/*
 * 0x00423E60 - sort comparator
 *
 * Returns 1 when GetBugStat(a) > GetBugStat(b) (descending order).
 */
static int
SortByType_Compare(uintptr_t a, uintptr_t b)
{
	return CPlayer_GetBugStat((CItem *)a) > CPlayer_GetBugStat((CItem *)b);
}

/*
 * 0x00423F60 - std::sort insertion sort
 *
 * Walks forward from begin+1. If new element < first, shifts all elements
 * right via CopyBackward and places it at begin. Otherwise, uses unguarded
 * linear insert (safe because begin acts as sentinel).
 */
static void
SortByZ_Insertion(uintptr_t *begin, uintptr_t *end, char typeTag, int depth)
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
		if (SortByZ_Compare(saved, *begin)) {
			vector_CopyBackward(begin, cur, cur + 1);
			*begin = saved;
		} else {
			SortByZ_UnguardedInsert(cur, saved, typeTag);
		}
	}
}

/*
 * 0x00423FF0 - std::sort median of three
 *
 * Returns the median value of three entity pointers using the z-coordinate
 * comparator for pivot selection.
 */
static uintptr_t
SortByZ_Median3(uintptr_t a, uintptr_t b, uintptr_t c, char typeTag)
{
	USED(typeTag);
	if (SortByZ_Compare(a, b)) {
		if (SortByZ_Compare(b, c))
			return b;
		else if (SortByZ_Compare(a, c))
			return c;
		else
			return a;
	} else {
		if (SortByZ_Compare(a, c))
			return a;
		else if (SortByZ_Compare(b, c))
			return c;
		else
			return b;
	}
}

/*
 * 0x004240C0 - std::sort partition
 *
 * Hoare partition scheme. Scans forward for elements >= pivot, backward
 * for elements <= pivot, swaps them. Returns partition point.
 */
static uintptr_t *
SortByZ_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, char typeTag)
{
	USED(typeTag);
	for (;;) {
		while (SortByZ_Compare(*begin, pivot))
			begin++;
		end--;
		while (SortByZ_Compare(pivot, *end))
			end--;
		if (end <= begin)
			return begin;
		vector_SwapWrapper(begin, end);
		begin++;
	}
}

/*
 * 0x00424140 - _Unguarded_insert (insertion sort)
 *
 * Walks forward from begin to end, inserting each element into its sorted
 * position via MoveBackward or CopyBackward.
 */
static void
SortByType_InsertionSort(uintptr_t *begin, uintptr_t *end, uint8_t typeTag, int depth)
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
		if (SortByType_Compare(saved, *begin)) {
			vector_CopyBackward(begin, cur, cur + 1);
			*begin = saved;
		} else {
			SortByType_MoveBackward(cur, saved, typeTag);
		}
	}
}

/*
 * 0x004241D0 - _Median_of_three
 *
 * Returns the median value of three elements for pivot selection.
 */
static uintptr_t
SortByType_MedianOfThree(uintptr_t a, uintptr_t b, uintptr_t c, uint8_t typeTag)
{
	USED(typeTag);
	if (SortByType_Compare(a, b)) {
		if (SortByType_Compare(b, c))
			return b;
		else if (SortByType_Compare(a, c))
			return c;
		else
			return a;
	} else {
		if (SortByType_Compare(a, c))
			return a;
		else if (SortByType_Compare(b, c))
			return c;
		else
			return b;
	}
}

/*
 * 0x004242A0 - _Partition (Hoare partition scheme)
 *
 * Partitions [lo, hi) around pivot. Returns partition point.
 */
static uintptr_t *
SortByType_Partition(uintptr_t *lo, uintptr_t *hi, uintptr_t pivot, uint8_t typeTag)
{
	USED(typeTag);
	for (;;) {
		while (SortByType_Compare(*lo, pivot))
			lo++;
		hi--;
		while (SortByType_Compare(pivot, *hi))
			hi--;
		if (hi <= lo)
			return lo;
		vector_SwapWrapper(lo, hi);
		lo++;
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
 * 0x00424D30 - std::list<void*> constructor with type byte
 *
 * 0x00426C00 (COMDAT)
 *
 * Copies type byte from arg, calls _Buynode(0, 0) to allocate sentinel,
 * sets head and size=0. This is the binary's std::list constructor.
 */
__attribute__((unused)) void
StdPtrList_ConstructorWithType(StdPtrList *list, void *typeBytePtr)
{
	*(uint8_t *)list = *(uint8_t *)typeBytePtr;
	list->head = StdPtrList_Buynode(list, NULL, NULL);
	list->size = 0;
}

/*
 * 0x00424D70 - std::list<void*> clear and destroy
 *
 * Gets begin/end iterators, calls StdPtrList_EraseRange_4F70 to remove all
 * elements, then frees the sentinel node via operator delete and zeroes
 * head/size.
 */
static __attribute__((unused)) void
StdPtrList_ClearAndDestroy_4D70(StdPtrList *list)
{
	StdPtrNode *endIter, *beginIter;
	StdPtrNode *result;

	StdPtrList_End(list, &endIter);
	beginIter = *StdPtrList_Begin(list, &(StdPtrNode *){ NULL });
	StdPtrList_EraseRange_4F70(list, &result, beginIter, endIter);
	free(list->head);
	list->head = NULL;
	list->size = 0;
}

/*
 * 0x00424DD0 - std::list<void*>::push_back (scalar deleting instantiation)
 *
 * COMDAT twin of StdPtrList_PushBack (0x00426CD0). Gets end iterator,
 * calls the local StdPtrList_Insert (0x00424EC0) to insert before it.
 */
void
StdPtrList_ScalarDelete_4DD0(StdPtrList *list, void *value)
{
	StdPtrNode *endIter;
	StdPtrNode *result;

	StdPtrList_End(list, &endIter);
	StdPtrList_Insert(list, &result, endIter, value);
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
 * 0x00424EC0 - StdPtrList::Insert (FIXED)
 *
 * Finds insertion point via CSearchCtx_Find. Allocates node with
 * StdPtrList_Buynode. Links into doubly-linked list. Calls
 * StdPtrList_DoInsert to copy value. Increments size. Sets result.
 *
 * Binary bug fix: MSVC STL's _Insert takes _Val by const reference
 * (const _Ty&), which lowers to a pointer-to-pointer in x86 ABI.
 * DoInsert_4CF610 dereferences its source argument to read the value.
 * The binary's caller (e.g. CScriptInstance_ReturnToPool at 0x00424549)
 * spills the raw pointer into a local then pushes &local. In the C port,
 * the top-level API passes a raw void* so we synthesize the ref here by
 * passing &value. Without this, DoInsert_4CF610 reads the first 4 bytes
 * of the pointed-to object instead of the pointer itself - triggering a
 * crash when CScriptInstance_Clear has poisoned those bytes to 0xABCD.
 */
static void
StdPtrList_Insert(StdPtrList *this, void *resultIter, void *searchPos, void *value)
{
	StdPtrList *list = this;
	StdPtrNode *pos;
	StdPtrNode *node;
	StdPtrNode *newNode;

	pos = *(StdPtrNode **)searchPos;

	newNode = StdPtrList_Buynode(list, pos, *StdPtrNode_GetPrev(pos));
	*StdPtrNode_GetPrev(pos) = newNode;
	node = *StdPtrNode_GetPrev(pos);
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;

	StdPtrList_DoInsert_424FC0(this, StdPtrNode_GetValue(node), &value);

	list->size = list->size + 1;
	CIterCtx_Set(resultIter, node);
}

/*
 * 0x00424F70 - std::list<void*> erase range
 *
 * Erases all elements from begin to end iterators. Loops while begin !=
 * end, calling post-increment (0x00484990) to advance and erase
 * (0x00424E00) to remove each node. Stores final iterator in result.
 */
void *
StdPtrList_EraseRange_4F70(StdPtrList *list, void *resultIter, StdPtrNode *beginNode, StdPtrNode *endNode)
{
	StdPtrNode *result;

	while (StdPtrIter_Neq(&beginNode, &endNode) & 0xFF) {
		StdPtrNode *next;
		StdPtrIter_PostInc(&beginNode, &next, 0);
		StdPtrList_Erase(list, &result, *(StdPtrNode **)&next);
	}
	*(StdPtrNode **)resultIter = beginNode;
	return resultIter;
}

/*
 * 0x00424FC0 - StdPtrList_DoInsert (first inlined copy)
 *
 * Forwards to the real implementation at 0x004CF610 (the second copy).
 */
static void
StdPtrList_DoInsert_424FC0(StdPtrList *this, void *dest, void *source)
{
	USED(this);
	StdPtrList_DoInsert_4CF610(dest, source);
}

/*
 * 0x00426440 - CVector::CVector wrapper
 *
 * Wrapper that passes a local (uninitialized) type byte pointer to
 * CVector_Constructor and returns this.
 */
static __attribute__((unused)) void *
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
void
CVector_Constructor(CVector *list, const char *typeFlag)
{
	list->type = *typeFlag;
	list->begin = NULL;
	list->end = NULL;
	list->capacity = NULL;
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
	vector_DestroyRange(list->begin, list->end);
	free(list->begin);

	list->begin = NULL;
	list->end = NULL;
	list->capacity = NULL;
}

/*
 * 0x004267A0 - std::_Ucopy range (CVector, 62 bytes)
 *
 * Copies 4-byte elements from [first, last) to dest by calling construct
 * (0x00424FC0) per element. Returns pointer past the last written
 * destination element.
 */
static __attribute__((unused)) void *
CVector_Ucopy_67A0(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x004267E0 - std::_Ufill_n (CVector, 57 bytes)
 *
 * Fills count 4-byte elements at dest by calling construct (0x00424FC0)
 * per element with the value pointed to by valuePtr. Returns void (no
 * return value used).
 */
static __attribute__((unused)) void
CVector_UfillN_67E0(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x00426820 - std::copy_backward (uint32_t, 46 bytes)
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
 * 0x00426C40 - std::list<void*> clear and destroy (TagNode pool)
 *
 * Gets begin/end iterators, runs the erase loop to remove all elements,
 * then frees the sentinel node and zeroes head/size. Template instantiation
 * for TagNode pool's deferred free list.
 */
static __attribute__((unused)) void
StdPtrList_ClearAndDestroy_6C40(StdPtrList *list)
{
	StdPtrNode *endIter, *beginIter;
	StdPtrNode *result;

	StdPtrList_End(list, &endIter);
	beginIter = *StdPtrList_Begin(list, &(StdPtrNode *){ NULL });
	StdPtrList_EraseRange_6DB0(list, &result, beginIter, endIter);
	free(list->head);
	list->head = NULL;
	list->size = 0;
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
 * 0x00426CD0 - std::list<void*>::push_back
 *
 * 0x0045AB50, 0x00457C50, 0x00484A60 (COMDAT)
 *
 * Appends value to end of list. Calls end() to get sentinel, then
 * _Insert() to insert before sentinel.
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
 * 0x00426D00 - std::list<void*>::_Insert
 *
 * 0x0045AB80, 0x00457CC0, 0x00484A90 (COMDAT)
 *
 * Inserts a new node before pos with the given value. Uses _Buynode to
 * allocate node with next/prev hints, then links into list via
 * _Prevnode/_Nextnode accessors, copies value via direct assignment
 * (binary uses _Constructor + placement new with reference semantics;
 * adapted to C value semantics), increments size.
 */
void
StdPtrList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *node;
	StdPtrNode *newNode;

	node = pos;

	// _Buynode(list, pos, pos->prev) - alloc with next=pos, prev=pos->prev
	newNode = StdPtrList_Buynode(list, node, *StdPtrNode_GetPrev(node));

	// pos->prev = newNode
	*StdPtrNode_GetPrev(node) = newNode;

	// Read back pos->prev (= newNode), overwrite node variable
	node = *StdPtrNode_GetPrev(node);

	// prevNode->next = newNode (via _Nextnode identity accessor)
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;

	// Copy value (binary: _Constructor -> _PlacementCopy with ref semantics)
	*StdPtrNode_GetValue(node) = value;

	list->size = list->size + 1;

	CIterCtx_Set(result, node);
}

/*
 * 0x00426DB0 - std::list<void*> erase range (TagNode pool)
 *
 * Erases every element in [begin, end), advancing via post-increment then
 * removing each node. Stores the final iterator into *resultIter. Template
 * instantiation for TagNode pool's deferred free list.
 */
static void *
StdPtrList_EraseRange_6DB0(StdPtrList *list, void *resultIter, StdPtrNode *beginNode, StdPtrNode *endNode)
{
	StdPtrNode *result;

	while (StdPtrIter_Neq(&beginNode, &endNode) & 0xFF) {
		StdPtrNode *next;
		StdPtrIter_PostInc(&beginNode, &next, 0);
		StdPtrList_Erase(list, &result, *(StdPtrNode **)&next);
	}
	*(StdPtrNode **)resultIter = beginNode;
	return resultIter;
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
 * 0x0042E990 - std::_Ucopy range (CVector instantiation 3)
 *
 * Copies 4-byte elements from [first, last) to dest by calling construct
 * (0x00424FC0) per element. Returns pointer past the last written
 * destination element. Template instantiation for CBlockManager CVector
 * operations.
 */
static __attribute__((unused)) void *
CVector_Ucopy_E990(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x0042E9D0 - std::_Ufill_n (CVector instantiation 3)
 *
 * Fills count 4-byte elements at dest by calling construct (0x00424FC0)
 * per element with the value pointed to by valuePtr.
 */
static __attribute__((unused)) void
CVector_UfillN_E9D0(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x0042EA10 - std::_Ucopy range (CVector instantiation 4)
 *
 * Copies 4-byte elements from [first, last) to dest by calling construct
 * (0x00424FC0) per element. Returns pointer past the last written
 * destination element. Template instantiation for CBlockManager CVector
 * operations.
 */
static __attribute__((unused)) void *
CVector_Ucopy_EA10(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x0042EA50 - std::_Ufill_n (CVector instantiation 4)
 *
 * Fills count 4-byte elements at dest by calling construct (0x00424FC0)
 * per element with the value pointed to by valuePtr.
 */
static __attribute__((unused)) void
CVector_UfillN_EA50(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x0042EA90 - std::_Ucopy range (CVector instantiation 5)
 *
 * Copies 4-byte elements from [first, last) to dest by calling construct
 * (0x00424FC0) per element. Returns pointer past the last written
 * destination element. Template instantiation for CBlockManager CVector
 * operations.
 */
static __attribute__((unused)) void *
CVector_Ucopy_EA90(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x0042EAD0 - std::_Ufill_n (CVector instantiation 5)
 *
 * Fills count 4-byte elements at dest by calling construct (0x00424FC0)
 * per element with the value pointed to by valuePtr.
 */
static __attribute__((unused)) void
CVector_UfillN_EAD0(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x0042FF00 - CVector::PushBack
 *
 * 0x004066C0, 0x00421550, 0x00426500, 0x0042E190, 0x0042E1C0 (COMDAT)
 * 0x0042E1F0, 0x004367A0, 0x00462640, 0x00462670, 0x00473860 (COMDAT)
 * 0x00479020, 0x00479050, 0x004A6490, 0x004CECB0, 0x004CECE0 (COMDAT)
 * 0x004CED10 (COMDAT)
 *
 * Calls End() to get insert position, then vector insert at end. This is
 * STL vector<uint32>::push_back via insert-at-end chain: 0x0042FF00 ->
 * 0x0042FF30 -> 0x0042FF80 (vector::insert).
 */
void
CVector_PushBack(CVector *list, uintptr_t value)
{
	uintptr_t *end = (uintptr_t *)list->end;
	uintptr_t *cap = (uintptr_t *)list->capacity;

	if (end < cap) {
		*end = value;
		list->end = end + 1;
	} else {
		uintptr_t *begin = (uintptr_t *)list->begin;
		uint32_t count = end - begin;
		uint32_t newcap = count == 0 ? 1 : count * 2;
		uintptr_t *newbuf = realloc(begin, newcap * sizeof(uintptr_t));

		if (newbuf == NULL)
			return;
		newbuf[count] = value;
		list->begin = newbuf;
		list->end = newbuf + count + 1;
		list->capacity = newbuf + newcap;
	}
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
 * 0x004301E0 - std::_Ucopy
 *
 * Copies uint32_t elements from [begin, end) to dest via a construct
 * callback. Returns pointer past last written element. Used by
 * CVector::insert to copy existing elements to a new buffer.
 */
static __attribute__((unused)) uintptr_t *
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
 * 0x00430220 - std::_Ucopy_n
 *
 * Copies count uint32_t elements from src to dest. Used by
 * CVector::insert to copy newly inserted elements.
 */
static __attribute__((unused)) void
CVector_UcopyN(void *alloc, uintptr_t *first, uint32_t count, uintptr_t *value)
{
	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)alloc, first, value);
		count--;
		first++;
	}
}

/*
 * 0x00436A40 - std::deque copy forward
 *
 * Copies 4-byte elements from [first, last) into dest. Returns the
 * final dest pointer.
 */
static __attribute__((unused)) uintptr_t *
StdDeque_CopyForward(void *alloc, uintptr_t *first, uintptr_t *last, uintptr_t *dest)
{
	while (first != last) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)alloc, dest, first);
		dest++;
		first++;
	}
	return dest;
}

/*
 * 0x00436A80 - std::deque copy N
 *
 * Fills count 4-byte elements at dest with *value.
 */
static __attribute__((unused)) void
StdDeque_CopyN(void *alloc, uintptr_t *dest, uint32_t count, uintptr_t *value)
{
	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)alloc, dest, value);
		dest++;
		count--;
	}
}

/*
 * 0x0044D710 - CMapNode scalar deleting destructor
 *
 * Scalar deleting destructor: runs CFragment_Destroy and frees the memory
 * when flags & 1. Returns this.
 */
static __attribute__((unused)) void *
CMapNode_ScalarDtor(CFragment *this, int flags)
{
	CFragment_Destroy(this);

	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x0044D740 - CMapIterator scalar deleting destructor
 *
 * Scalar deleting destructor: runs CDefine_Destructor and frees the memory
 * when flags & 1. Returns this.
 */
static __attribute__((unused)) void *
CMapIterator_ScalarDtor(CDefine *this, int flags)
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
 *
 * 0x00484990 is a second template instantiation of the same function where
 * the compiler inlined the CopyConstructor into a single dword store
 * (*outIter = saved). Semantically identical; both are mapped here.
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
 * Allocator wrapper. In C: malloc(size).
 */
void *
StdPtrList_Charalloc(StdPtrList *list, uint32_t size)
{
	USED(list);
	return malloc(size);
}

/*
 * 0x00457BB0 - StdPtrList_Init
 *
 * Copies allocator byte from *init, allocates sentinel via Buynode(0, 0),
 * sets size=0. Returns this.
 */
void
StdPtrList_Init(StdPtrList *list, const void *init)
{
	*(uint8_t *)list = *(const uint8_t *)init;
	list->head = StdPtrList_Buynode(list, NULL, NULL);
	list->size = 0;
}

/*
 * 0x00457BF0 - StdPtrList_Clear
 *
 * Gets begin/end iterators, calls StdPtrList_EraseRange to erase all,
 * frees the sentinel node, sets head=NULL and size=0.
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
 * 0x00457D70 - StdPtrList_EraseRange
 *
 * Template instantiation of erase range for entity manager list. While
 * first != last: post-increments first (0x00484990), extracts value,
 * erases old position from list. Stores final iterator to *result.
 */
StdPtrNode **
StdPtrList_EraseRange(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;
	StdPtrNode *eraseResult;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc(&first, &postIncTemp, 0);
		StdPtrList_Erase(list, &eraseResult, *(StdPtrNode **)&postIncTemp);
	}
	*result = first;
	return result;
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
	buf = (char *)malloc(len + 1);
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
		free(str);
}

/*
 * 0x00458FB0 - std::list<CFileEntry>::_Init
 *
 * Copies allocator byte from source, allocates sentinel via Buynode,
 * and zeroes the count. Returns self.
 */
static __attribute__((unused)) void *
StdFileList_Init(StdPtrList *list, void *src)
{
	*(uint8_t *)list = *(uint8_t *)src;
	list->head = StdFileList_Buynode(list, NULL, NULL);
	list->size = 0;
	return list;
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
	buf = (char *)malloc(len + 1);
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
		free(self);
	return NULL;
}

/*
 * 0x0045AAB0 - std::list<void*>::_Init (login script list, 54 bytes)
 *
 * Copies byte from *arg to this[0], calls _Buynode(0, 0) to create
 * self-referencing sentinel at this+4, sets this+8 (count) = 0. Template
 * instantiation of std::list constructor for g_loginScriptList.
 */
static __attribute__((unused)) void *
StdPtrList_InitLogin(StdPtrList *this, const void *init)
{
	*(uint8_t *)this = *(const uint8_t *)init;
	this->head = StdPtrList_Buynode(this, NULL, NULL);
	this->size = 0;
	return this;
}

/*
 * 0x0045AAF0 - std::list<void*>::~list (login script list, 90 bytes)
 *
 * Gets begin and end iterators, erases the entire range, frees the
 * sentinel node, and zeroes head and size.
 */
static __attribute__((unused)) void
StdPtrList_DestructorLogin(StdPtrList *list)
{
	StdPtrNode *endNode, *beginNode, *result;

	StdPtrList_End(list, &endNode);
	StdPtrList_Begin(list, &beginNode);
	StdPtrList_EraseRangeLogin(list, &result, beginNode, endNode);
	free(list->head);
	list->head = NULL;
	list->size = 0;
}

/*
 * 0x0045AC30 - std::list<void*>::erase range (login script list, 78 bytes)
 *
 * While first != last: post-increments first (0x00484990), erases old
 * position via StdPtrList_Erase. Stores final iterator to *result.
 */
static void
StdPtrList_EraseRangeLogin(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc(&first, &postIncTemp, 0);
		StdPtrList_Erase(list, &postIncTemp, *(StdPtrNode **)&postIncTemp);
	}
	*result = first;
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
void
StdPtrIter_Inc(StdPtrNode **iter)
{
	StdPtrNode *node;
	StdPtrNode *ref;

	node = *iter;
	ref = StdPtrNode_GetNext(node);
	*iter = ref->next;
}

/*
 * 0x00461FB0 - StdPtrList::Init (EntityMap variant)
 *
 * Copies byte from arg to this[0]. Calls StdPtrList_Buynode(this, 0, 0) to
 * allocate sentinel, stores in this->head. Sets this->size = 0. Returns
 * this.
 */
static __attribute__((unused)) void *
StdPtrList_Init_EntityMap(StdPtrList *list, void *typeBytePtr)
{
	*(uint8_t *)list = *(uint8_t *)typeBytePtr;
	list->head = StdPtrList_Buynode(list, NULL, NULL);
	list->size = 0;
	return list;
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
 * 0x00462070 - std::list<void*>::push_back (grid variant, 46 bytes)
 *
 * Template instantiation of push_back for CEntityMap grid blocks. Called
 * from CEntityMap_GridInsert (0x00461F10). Identical logic to
 * StdPtrList_PushBack (0x00426CD0): calls end() then _Insert().
 */
static __attribute__((unused)) void
StdPtrList_PushBackGrid(StdPtrList *list, void *value)
{
	StdPtrNode *endIter;
	StdPtrNode *result;

	StdPtrList_End(list, &endIter);
	StdPtrList_DoInsertGrid(list, &result, endIter, value);
}

/*
 * 0x004620A0 - std::list<void*>::_Insert (grid variant, 171 bytes)
 *
 * Template instantiation of _Insert for CEntityMap grid blocks. Called
 * from StdPtrList_PushBackGrid (0x00462070). Identical logic to
 * StdPtrList_DoInsert (0x00426D00): allocates node via _Buynode, links
 * into list, copies value, increments size.
 */
static void
StdPtrList_DoInsertGrid(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *node;
	StdPtrNode *newNode;

	node = pos;

	newNode = StdPtrList_Buynode(list, node, *StdPtrNode_GetPrev(node));
	*StdPtrNode_GetPrev(node) = newNode;
	node = *StdPtrNode_GetPrev(node);
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;
	*StdPtrNode_GetValue(node) = value;
	list->size = list->size + 1;
	CIterCtx_Set(result, node);
}

/*
 * 0x00462150 - std::list<void*>::erase range (grid variant, 78 bytes)
 *
 * Template instantiation of erase range for CEntityMap grid blocks. While
 * first != last: post-increments first (0x00484990), erases old position
 * via StdPtrList_Erase. Stores final iterator to *result.
 */
static __attribute__((unused)) StdPtrNode **
StdPtrList_EraseRangeGrid(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;
	StdPtrNode *eraseResult;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc(&first, &postIncTemp, 0);
		StdPtrList_Erase(list, &eraseResult, *(StdPtrNode **)&postIncTemp);
	}
	*result = first;
	return result;
}

/*
 * 0x00462390 - std::sort by distance for pair arrays
 *
 * Sort entry point for entity query pair arrays. Calls
 * GameCentMon_GetPlayerCount for the depth limit, then delegates to
 * SortByDist_Main_Pair. Called from ProcessCrimeWitness.
 */
static void __attribute__((unused))
Vector_SortByDistPair(uintptr_t *begin, uintptr_t *end, CLocation refLoc)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByDist_Main_Pair(begin, end, refLoc, depth);
}

/*
 * 0x004625D0 - std::sort by distance for pair arrays (entry)
 *
 * Sort entry point. Calls GameCentMon_GetPlayerCount for depth, then
 * delegates to SortByDist_Main_Pair2. Called from
 * CombatManager_SpawnGuards.
 */
void
Vector_SortByDistPairEntry(uintptr_t *begin, uintptr_t *end, CLocation refLoc)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByDist_Main_Pair2(begin, end, refLoc, depth);
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
 * 0x00462BA0 - std::_Ucopy (CVector instantiation 6, 62 bytes)
 *
 * Copies 4-byte elements from [first, last) to dest via construct
 * callback. Returns pointer past last written element.
 */
static __attribute__((unused)) void *
CVector_Ucopy_BA0(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x00462BE0 - std::_Ufill_n (CVector instantiation 6, 57 bytes)
 *
 * Fills count 4-byte elements at dest by calling construct per element
 * with the value pointed to by valuePtr.
 */
static __attribute__((unused)) void
CVector_UfillN_BE0(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x00462C20 - std::_Ucopy (CVector instantiation 7, 62 bytes)
 *
 * Copies 4-byte elements from [first, last) to dest via construct
 * callback. Returns pointer past last written element.
 */
static __attribute__((unused)) void *
CVector_Ucopy_C20(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x00462C60 - std::_Ufill_n (CVector instantiation 7, 57 bytes)
 *
 * Fills count 4-byte elements at dest by calling construct per element
 * with the value pointed to by valuePtr.
 */
static __attribute__((unused)) void
CVector_UfillN_C60(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}
/*
 * 0x00462CA0 - std::sort main for pair array distance sort
 *
 * Introsort dispatch for entity query pair arrays: if count <= 16,
 * insertion sort; otherwise quicksort + insertion sort.
 */
static void
SortByDist_Main_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
{
	int count;
	uintptr_t *cur;

	USED(depth);
	count = (int)(end - begin);
	if (count <= 16) {
		SortByDist_InsertionEntry_Pair(begin, end, refLoc);
		return;
	}
	SortByDist_Quicksort_Pair(begin, end, refLoc, 0);
	SortByDist_InsertionEntry_Pair(begin, begin + 16, refLoc);
	cur = begin + 16;
	while (cur != end) {
		SortByDist_UnguardedInsert_Pair(cur, *cur, refLoc);
		cur++;
	}
}

/*
 * 0x00462DB0 - std::sort main for pair arrays (variant 2)
 *
 * Introsort dispatch. Called from Vector_SortByDistPairEntry. If count <=
 * 16, insertion sort; otherwise quicksort + insertion sort.
 */
static void
SortByDist_Main_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
{
	int count;
	uintptr_t *cur;

	USED(depth);
	count = (int)(end - begin);
	if (count <= 16) {
		SortByDist_InsertionEntry_Pair2(begin, end, refLoc);
		return;
	}
	SortByDist_Quicksort_Pair2(begin, end, refLoc, 0);
	SortByDist_InsertionEntry_Pair2(begin, begin + 16, refLoc);
	cur = begin + 16;
	while (cur != end) {
		SortByDist_UnguardedInsert_Pair2(cur, *cur, refLoc);
		cur++;
	}
}

/*
 * 0x00462EC0 - std::sort insertion entry for pair arrays
 *
 * Wrapper. Reads the unused depth limit from GameCentMon_GetPlayerCount
 * and delegates to SortByDist_Insertion_Pair.
 */
static void
SortByDist_InsertionEntry_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByDist_Insertion_Pair(begin, end, refLoc, depth);
}

/*
 * 0x00462F30 - std::sort quicksort for pair arrays
 *
 * Quicksort with median-of-three pivot. Recurses on smaller half, iterates
 * on larger. Stops at partitions of <=16 elements.
 */
static void
SortByDist_Quicksort_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
{
	uintptr_t pivot;
	uintptr_t *mid;
	int rightCount, leftCount;

	USED(depth);
	for (;;) {
		if ((int)(end - begin) <= 16)
			return;
		pivot = SortByDist_Median3_Pair(*begin, begin[(int)(end - begin) / 2], *(end - 1), refLoc);
		mid = SortByDist_Partition_Pair(begin, end, pivot, refLoc);
		rightCount = (int)(end - mid);
		leftCount = (int)(mid - begin);
		if (rightCount > leftCount) {
			SortByDist_Quicksort_Pair(begin, mid, refLoc, GameCentMon_GetPlayerCount());
			begin = mid;
		} else {
			SortByDist_Quicksort_Pair(mid, end, refLoc, GameCentMon_GetPlayerCount());
			end = mid;
		}
	}
}

/*
 * 0x00463080 - Unguarded linear insert for pair arrays
 *
 * Shifts elements right from pos while val is closer than the preceding
 * element, then places val at correct position.
 */
static void
SortByDist_UnguardedInsert_Pair(uintptr_t *pos, uintptr_t val, CLocation refLoc)
{
	uintptr_t *cur = pos;

	for (;;) {
		cur--;
		if (!SortByDist_Compare(&refLoc, val, *cur))
			break;
		*pos = *cur;
		pos = cur;
	}
	*pos = val;
}

/*
 * 0x00463110 - std::sort insertion entry for pair arrays (variant 2)
 *
 * Wrapper. Reads the unused depth limit from GameCentMon_GetPlayerCount
 * and delegates to SortByDist_Insertion_Pair2.
 */
static void
SortByDist_InsertionEntry_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByDist_Insertion_Pair2(begin, end, refLoc, depth);
}

/*
 * 0x00463180 - std::sort quicksort for pair arrays (variant 2)
 *
 * Quicksort with median-of-three pivot. Recurses on smaller half.
 */
static void
SortByDist_Quicksort_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
{
	uintptr_t pivot;
	uintptr_t *mid;
	int rightCount, leftCount;

	USED(depth);
	for (;;) {
		if ((int)(end - begin) <= 16)
			return;
		pivot = SortByDist_Median3_Pair2(*begin, begin[(int)(end - begin) / 2], *(end - 1), refLoc);
		mid = SortByDist_Partition_Pair2(begin, end, pivot, refLoc);
		rightCount = (int)(end - mid);
		leftCount = (int)(mid - begin);
		if (rightCount > leftCount) {
			SortByDist_Quicksort_Pair2(begin, mid, refLoc, GameCentMon_GetPlayerCount());
			begin = mid;
		} else {
			SortByDist_Quicksort_Pair2(mid, end, refLoc, GameCentMon_GetPlayerCount());
			end = mid;
		}
	}
}

/*
 * 0x004632D0 - Unguarded linear insert for pair arrays (variant 2)
 *
 * Shifts elements right from pos while val is closer than the preceding
 * element, then places val at correct position.
 */
static void
SortByDist_UnguardedInsert_Pair2(uintptr_t *pos, uintptr_t val, CLocation refLoc)
{
	uintptr_t *cur = pos;

	for (;;) {
		cur--;
		if (!SortByDist_Compare(&refLoc, val, *cur))
			break;
		*pos = *cur;
		pos = cur;
	}
	*pos = val;
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
 * 0x004633B0 - Insertion sort for pair arrays
 *
 * Standard insertion sort for entity pointer arrays. If new element is
 * closer than first, shifts all right via CopyBackward. Otherwise uses
 * unguarded insert.
 */
static void
SortByDist_Insertion_Pair(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
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
		if (SortByDist_Compare(&refLoc, saved, *begin)) {
			vector_CopyBackward(begin, cur, cur + 1);
			*begin = saved;
		} else {
			SortByDist_UnguardedInsert_Pair(cur, saved, refLoc);
		}
	}
}

/*
 * 0x00463480 - Median of three for pair arrays
 *
 * Returns median of three values using distance comparator.
 */
static uintptr_t
SortByDist_Median3_Pair(uintptr_t a, uintptr_t b, uintptr_t c, CLocation refLoc)
{
	if (SortByDist_Compare(&refLoc, a, b)) {
		if (SortByDist_Compare(&refLoc, b, c))
			return b;
		else if (SortByDist_Compare(&refLoc, a, c))
			return c;
		else
			return a;
	} else {
		if (SortByDist_Compare(&refLoc, a, c))
			return a;
		else if (SortByDist_Compare(&refLoc, b, c))
			return c;
		else
			return b;
	}
}

/*
 * 0x004635A0 - Partition for pair arrays
 *
 * Hoare partition using distance comparator.
 */
static uintptr_t *
SortByDist_Partition_Pair(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation refLoc)
{
	for (;;) {
		while (SortByDist_Compare(&refLoc, *begin, pivot))
			begin++;
		end--;
		while (SortByDist_Compare(&refLoc, pivot, *end))
			end--;
		if (end <= begin)
			return begin;
		vector_SwapWrapper(begin, end);
		begin++;
	}
}

/*
 * 0x00463660 - Insertion sort for pair arrays (variant 2)
 *
 * Standard insertion sort for entity pointer arrays.
 */
static void
SortByDist_Insertion_Pair2(uintptr_t *begin, uintptr_t *end, CLocation refLoc, int depth)
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
		if (SortByDist_Compare(&refLoc, saved, *begin)) {
			vector_CopyBackward(begin, cur, cur + 1);
			*begin = saved;
		} else {
			SortByDist_UnguardedInsert_Pair2(cur, saved, refLoc);
		}
	}
}

/*
 * 0x00463730 - Median of three for pair arrays (variant 2)
 *
 * Returns median of three values using distance comparator.
 */
static uintptr_t
SortByDist_Median3_Pair2(uintptr_t a, uintptr_t b, uintptr_t c, CLocation refLoc)
{
	if (SortByDist_Compare(&refLoc, a, b)) {
		if (SortByDist_Compare(&refLoc, b, c))
			return b;
		else if (SortByDist_Compare(&refLoc, a, c))
			return c;
		else
			return a;
	} else {
		if (SortByDist_Compare(&refLoc, a, c))
			return a;
		else if (SortByDist_Compare(&refLoc, b, c))
			return c;
		else
			return b;
	}
}

/*
 * 0x00463850 - Partition for pair arrays (variant 2)
 *
 * Hoare partition using distance comparator.
 */
static uintptr_t *
SortByDist_Partition_Pair2(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation refLoc)
{
	for (;;) {
		while (SortByDist_Compare(&refLoc, *begin, pivot))
			begin++;
		end--;
		while (SortByDist_Compare(&refLoc, pivot, *end))
			end--;
		if (end <= begin)
			return begin;
		vector_SwapWrapper(begin, end);
		begin++;
	}
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
 * 0x0046BA10 - CVector destructor (SurfaceInfo variant, 99 bytes)
 *
 * Calls CVector_DestroyRangeSI on [begin, end], then computes (capacity -
 * begin) / 12 and deallocates via StdDeque_Dealloc (which is just free).
 * Zeroes begin/end/capacity.
 */
static __attribute__((unused)) void
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
 * 0x0046BA80 - CVector::push_back (SurfaceInfo variant, 34 bytes)
 *
 * Calls StdList_GetSize (returns end offset) then CVector_InsertAt.
 */
static __attribute__((unused)) void
CVector_PushBackSI(CVector *this, SurfaceInfo *value)
{
	uintptr_t count = (uintptr_t)((CVector *)this)->end;
	CVector_InsertAtSI(this, (uint32_t)count, value);
}

/*
 * 0x0046BB00 - CVector::InsertAt (SurfaceInfo variant, 75 bytes)
 *
 * Computes offset from CSearchCtx_GetBucket, calls _Insert with count=1,
 * then recomputes offset from bucket base.
 */
static void *
CVector_InsertAtSI(CVector *this, uint32_t index, SurfaceInfo *value)
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
 * 0x0046BB50 - Destroy range (SurfaceInfo variant, 46 bytes)
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
 * 0x0046BDC0 - CVector::GetCount (SurfaceInfo variant, 57 bytes)
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
 * 0x0046BE00 - std::_Ucopy (SurfaceInfo variant, 62 bytes)
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
 * 0x0046BE40 - std::_Ufill_n (SurfaceInfo variant, 57 bytes)
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
 * 0x0046BEA0 - std::sort entry (SurfaceInfo, 38 bytes)
 *
 * Computes depth from GameCentMon_GetPlayerCount, then calls sort main.
 */
static __attribute__((unused)) void
SortSurface_Entry(SurfaceInfo *begin, SurfaceInfo *end, char typeTag)
{
	int depth = GameCentMon_GetPlayerCount();
	SortSurface_Main(begin, end, typeTag, depth);
}

/*
 * 0x0046C170 - std::sort main (SurfaceInfo, 192 bytes)
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
 * 0x0046C230 - std::sort insertion entry (SurfaceInfo, 38 bytes)
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
 * 0x0046C260 - std::sort quicksort (SurfaceInfo, 364 bytes)
 *
 * Quicksort loop: while range > 16, choose median-of-3 pivot, partition,
 * recurse on smaller half. Falls through to insertion sort entry when
 * range <= 16.
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
 * 0x0046C3D0 - Unguarded linear insert (SurfaceInfo, 82 bytes)
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
 * 0x0046C490 - Insertion sort (SurfaceInfo, 170 bytes)
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
 * 0x0046C540 - Median of three (SurfaceInfo, 267 bytes)
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
 * 0x0046C650 - Partition (SurfaceInfo, 121 bytes)
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
 * 0x0046C6D0 - Swap with depth wrapper (SurfaceInfo, 34 bytes)
 *
 * Computes depth via GameCentMon_GetPlayerCount, then calls
 * SortSurface_SwapImpl.
 */
static void
SortSurface_Swap(SurfaceInfo *a, SurfaceInfo *b)
{
	int depth = GameCentMon_GetPlayerCount();
	USED(depth);
	SortSurface_SwapImpl(a, b);
}

/*
 * 0x0046C700 - Swap implementation (SurfaceInfo, 54 bytes)
 *
 * Swaps two SurfaceInfo values using a local temp.
 */
static void
SortSurface_SwapImpl(SurfaceInfo *a, SurfaceInfo *b)
{
	SurfaceInfo temp;

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
 * 0x0046C960 - CVector::push_back (uint32_t variant, 34 bytes)
 *
 * Calls StdList_GetSize (returns end offset) then CVector_Insert_C990.
 */
static __attribute__((unused)) void
CVector_PushBack_C960(CVector *this, uint32_t *value)
{
	void *endPtr = ((CVector *)this)->end;
	CVector_Insert_C990(this, (uint32_t *)endPtr, value);
}

/*
 * 0x0046C990 - CVector::Insert (uint32_t variant, 66 bytes)
 *
 * Computes offset from CSearchCtx_GetBucket, calls _Insert with count=1,
 * then recomputes offset from bucket base.
 */
static void *
CVector_Insert_C990(CVector *this, uint32_t *pos, uint32_t *value)
{
	uintptr_t bucket;
	int offset;

	bucket = (uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)this);
	offset = ((uintptr_t)pos - bucket) >> 2;
	CVector_Insert_CA00(this, pos, 1, value);
	bucket = (uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)this);
	return (void *)(bucket + offset * 4);
}

/*
 * 0x0046CA00 - CVector::_Insert (uint32_t variant)
 *
 * Complex insert with reallocation logic matching MSVC CVector::_Insert.
 */
static void
CVector_Insert_CA00(CVector *this, uint32_t *pos, uint32_t count, uint32_t *value)
{
	uint32_t curCapacity;
	uint32_t totalCapacity;
	uint32_t *newBuf;
	uint32_t *newEnd;

	curCapacity = ((uintptr_t)this->capacity - (uintptr_t)this->end) >> 2;

	if (curCapacity < count) {
		// Reallocation path
		uint32_t curCount = CVector_GetCount(this);
		uint32_t growTo;

		if (count < curCount)
			growTo = curCount;
		else
			growTo = count;

		totalCapacity = curCount + growTo;

		// CVector_Allocate4 (0x0047A350)
		newBuf = (uint32_t *)CVector_Allocate4_Terrain(this, totalCapacity, 0);

		// Copy elements before pos
		newEnd = CVector_Ucopy_CC20(this, (uint32_t *)this->begin, pos, newBuf);

		// Fill new elements
		CVector_UfillN_CC60(this, newEnd, count, value);

		// Copy elements after pos
		CVector_Ucopy_CC20(this, pos, (uint32_t *)this->end, newEnd + count);

		// CVector_Destroy4_Range (0x00422740) - no-op for POD
		Destroy4_Range_Terrain(this, this->begin, this->end);
		// StdDeque_Dealloc (0x0046C9E0)
		{
			uint32_t oldCount = ((uintptr_t)this->capacity - (uintptr_t)this->begin) >> 2;
			StdDeque_DeallocSI(this, this->begin, oldCount);
		}

		// Set new pointers
		this->capacity = (void *)(newBuf + totalCapacity);
		this->end = (void *)(newBuf + CVector_GetCount(this) + count);
		this->begin = newBuf;
	} else {
		// In-place insert
		uint32_t afterCount = ((uintptr_t)this->end - (uintptr_t)pos) >> 2;

		if (afterCount < count) {
			// Copy existing tail past new elements
			CVector_Ucopy_CC20(this, pos, (uint32_t *)this->end, pos + count);

			// Fill gap with value
			CVector_UfillN_CC60(this, (uint32_t *)this->end, count - afterCount, value);

			// Fill existing range with value
			vector_Fill(pos, this->end, value);

			this->end = (void *)((uint32_t *)this->end + count);
		} else if (count > 0) {
			// Copy backward to make room
			CVector_Ucopy_CC20(this, (uint32_t *)this->end - count, (uint32_t *)this->end, (uint32_t *)this->end);

			vector_CopyBackward(pos, (uint32_t *)this->end - count, (uint32_t *)this->end);

			vector_Fill(pos, pos + count, value);

			this->end = (void *)((uint32_t *)this->end + count);
		}
	}
}

/*
 * 0x0046CC20 - std::_Ucopy (uint32_t variant, 62 bytes)
 *
 * Copies 4-byte elements from [first, last) to dest via construct callback
 * (0x00424FC0) per element. Returns pointer past last written element.
 */
static uint32_t *
CVector_Ucopy_CC20(CVector *this, uint32_t *first, uint32_t *last, uint32_t *dest)
{
	USED(this);
	while (first != last) {
		*dest = *first;
		dest++;
		first++;
	}
	return dest;
}

/*
 * 0x0046CC60 - std::_Ufill_n (uint32_t variant, 57 bytes)
 *
 * Fills count 4-byte elements at dest via construct callback (0x00424FC0)
 * per element with the value pointed to by src.
 */
static void
CVector_UfillN_CC60(CVector *this, uint32_t *dest, uint32_t count, uint32_t *src)
{
	USED(this);
	while (count > 0) {
		*dest = *src;
		count--;
		dest++;
	}
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
 * value into *out, decrements the slot by one element, and returns out.
 * Call sites pass &local_var where local_var was initialized by
 * CMultiComponent_GetIterator (via CIterCtx_Set) to vec->end.
 */
uintptr_t **
CVector_PopBack(uint32_t **iter, uintptr_t **out)
{
	*out = (uintptr_t *)*iter;
	*iter = (uint32_t *)((char *)*iter - 4);
	return out;
}

/*
 * 0x00473040 - CVector::EraseSingle
 *
 * Copies [pos+1, end) to pos via vector_Copy, calls vector_DestroyRange on
 * the vacated last element, decrements end. Returns pos.
 */
void *
CVector_EraseSingle(CVector *vec, void *pos)
{
	vector_Copy((uintptr_t *)pos + 1, vec->end, pos);
	vector_DestroyRange((uintptr_t *)vec->end - 1, vec->end);
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
	CVector_ClearFreeRaw(vec->begin, count);
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
			CVector_ClearFreeRaw(this->begin, ((char *)this->capacity - (char *)this->begin) / sizeof(CString));
			this->begin = CVector_Allocate16(this, srcCount);
			CVector_Uninit_Copy16_Fwd2(this, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
			this->end = this->begin ? (void *)((char *)this->begin + srcCount * sizeof(CString)) : NULL;
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
	CVector_ClearFreeRaw(vec->begin, count);
	vec->begin = NULL;
	vec->end = NULL;
	vec->capacity = NULL;
}

/*
 * 0x00478BA0 - CVector::GetCount (CMultiComponent variant, 57 bytes)
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
	CVector_ClearFreeRaw(this->begin, count);
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
 * 0x00479180 - CVector::CopyConstruct (4-byte elements)
 *
 * Copies the type byte, allocates a buffer sized to src's count, copies
 * the elements in, and sets end = capacity.
 */
static CVector *
CVector_CopyConstruct4(CVector *this, CVector *src)
{
	this->type = src->type;
	this->begin = CVector_Allocate4(this, CVector_GetCount(src));
	this->end = Uninit_Copy4_Fwd2(this, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
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
		memcpy(this->begin, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), srcCount * 4);
		mid = (char *)this->begin + srcCount * 4;
		CVector_Destroy4_Range(this, mid, this->end);
		this->end = (char *)this->begin + srcCount * 4;
	} else {
		dstCap = CVector_GetCapacity(this);
		if (srcCount <= dstCap) {
			void *splitPt = (char *)CSearchCtx_GetBucket((CSearchCtx *)src) + dstCount * 4;
			memcpy(this->begin, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), dstCount * 4);
			Uninit_Copy4_Fwd2(this, splitPt, src->end, this->end);
			this->end = (char *)this->begin + srcCount * 4;
		} else {
			CVector_Destroy4_Range(this, this->begin, this->end);
			CVector_ClearFreeRaw(this->begin, ((char *)this->capacity - (char *)this->begin) >> 2);
			this->begin = CVector_Allocate4(this, srcCount);
			this->end = Uninit_Copy4_Fwd2(this, (void *)(uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)src), src->end, this->begin);
			this->capacity = this->end;
		}
	}
	return this;
}

/*
 * 0x00479380 - CVector::GetCapacity (4-byte elements, 52 bytes)
 *
 * Returns allocated capacity: (capacity - begin) / 4, or 0 if begin is
 * NULL.
 */
uint32_t
CVector_GetCapacity(CVector *list)
{
	if (list->begin == NULL)
		return 0;
	return ((uint32_t *)list->capacity - (uint32_t *)list->begin);
}

/*
 * 0x004793C0 - CVector::GetCapacity (16-byte elements, 52 bytes)
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
 * 0x00479B50 - Destroy1C_Range2
 *
 * Calls Allocate1C (0x0047B1D0) with (count) to allocate new buffer.
 */
static __attribute__((unused)) void *
Destroy1C_Range2(CVector *this, int count, int unused_arg)
{
	USED(this);
	USED(unused_arg);
	return Allocate1C(count);
}

/*
 * 0x0047A350 - CVector::Allocate (4-byte elements, 27 bytes)
 *
 * Thiscall wrapper that forwards to Vector_AllocElements(count, 0),
 * which saturates count to zero and allocates count*4 bytes via
 * OperatorNew.
 */
static void *
CVector_Allocate4(CVector *this, uint32_t count)
{
	USED(this);
	if ((int32_t)count < 0)
		count = 0;
	return malloc(count * sizeof(uintptr_t));
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
CVector_Allocate6(CVector *this, uint32_t count)
{
	USED(this);
	if ((int)count < 0)
		count = 0;
	return malloc(count * sizeof(CLocation));
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
		CLocation_SetLoc(&tmp, &cmpLoc);
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
		CLocation_SetLoc(&tmp1, &cmpLoc);
		CLocation_SetLoc(&tmp2, &cmpLoc);
		pivot = SortMultiDist_Median3(*(uintptr_t *)first, *(uintptr_t *)((char *)first + (count / 2) * sizeof(uintptr_t)), *((uintptr_t *)last - 1), tmp1);
		part = SortMultiDist_Partition(first, last, pivot, tmp2);
		if (((char *)last - (char *)part) / sizeof(uintptr_t) <= ((char *)part - (char *)first) / sizeof(uintptr_t)) {
			CLocation tmp3;
			CLocation_SetLoc(&tmp3, &cmpLoc);
			SortMultiDist_Quicksort(part, last, tmp3, GameCentMon_GetPlayerCount());
			last = part;
		} else {
			CLocation tmp4;
			CLocation_SetLoc(&tmp4, &cmpLoc);
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
 * Sort entry point for CVector of entity pointers, ordering by Chebyshev
 * distance from a reference location (ascending). Used by BroadcastToNearby
 * and BroadcastToRangeWithLOS.
 */
void
Vector_SortByDist(void *begin, void *end, CLocation *refLoc)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByDist_Main((uintptr_t *)begin, (uintptr_t *)end, refLoc, depth);
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
 * 0x0047F3F0 - std::sort main for distance sort
 *
 * If count <= 16, insertion sort directly. Otherwise, quicksort loop to
 * partitions of <=16 elements, then insertion sort the first 16, then
 * unguarded linear insertion on the rest.
 */
static void
SortByDist_Main(uintptr_t *begin, uintptr_t *end, CLocation *refLoc, int depth)
{
	int count;
	uintptr_t *cur;

	USED(depth);
	count = (int)(end - begin);
	if (count <= 16) {
		SortByDist_InsertionEntry(begin, end, refLoc);
		return;
	}
	SortByDist_Quicksort(begin, end, refLoc, 0);
	SortByDist_InsertionEntry(begin, begin + 16, refLoc);
	cur = begin + 16;
	while (cur != end) {
		SortByDist_UnguardedInsert(cur, *cur, refLoc);
		cur++;
	}
}

/*
 * 0x0047F500 - std::sort insertion entry for distance sort
 *
 * Wrapper that reads the unused depth limit from GameCentMon_GetPlayerCount
 * and delegates to the actual insertion sort.
 */
static void
SortByDist_InsertionEntry(uintptr_t *begin, uintptr_t *end, CLocation *refLoc)
{
	int depth = GameCentMon_GetPlayerCount();
	SortByDist_Insertion(begin, end, refLoc, depth);
}

/*
 * 0x0047F570 - std::sort quicksort loop for distance sort
 *
 * Quicksort with median-of-three pivot. Recurses on smaller half,
 * iterates on larger. Stops at partitions of <=16 elements.
 */
static void
SortByDist_Quicksort(uintptr_t *begin, uintptr_t *end, CLocation *refLoc, int depth)
{
	uintptr_t pivot;
	uintptr_t *mid;
	int rightCount, leftCount;

	USED(depth);
	for (;;) {
		if ((int)(end - begin) <= 16)
			return;
		pivot = SortByDist_Median3(*begin, begin[(int)(end - begin) / 2], *(end - 1), refLoc);
		mid = SortByDist_Partition(begin, end, pivot, refLoc);
		rightCount = (int)(end - mid);
		leftCount = (int)(mid - begin);
		if (rightCount > leftCount) {
			SortByDist_Quicksort(begin, mid, refLoc, GameCentMon_GetPlayerCount());
			begin = mid;
		} else {
			SortByDist_Quicksort(mid, end, refLoc, GameCentMon_GetPlayerCount());
			end = mid;
		}
	}
}

/*
 * 0x0047F6C0 - std::sort unguarded linear insert for distance sort
 *
 * Shifts elements right from pos while val is closer than the preceding
 * element, then places val at the correct sorted position.
 */
static void
SortByDist_UnguardedInsert(uintptr_t *pos, uintptr_t val, CLocation *refLoc)
{
	uintptr_t *cur = pos;

	for (;;) {
		cur--;
		if (!SortByDist_Compare(refLoc, val, *cur))
			break;
		*pos = *cur;
		pos = cur;
	}
	*pos = val;
}

/*
 * 0x0047F750 - std::sort insertion sort for distance sort
 *
 * Walks forward from begin+1. If new element is closer than first, shifts
 * all elements right via CopyBackward and places it at begin. Otherwise,
 * uses unguarded linear insert.
 */
static void
SortByDist_Insertion(uintptr_t *begin, uintptr_t *end, CLocation *refLoc, int depth)
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
		if (SortByDist_Compare(refLoc, saved, *begin)) {
			vector_CopyBackward(begin, cur, cur + 1);
			*begin = saved;
		} else {
			SortByDist_UnguardedInsert(cur, saved, refLoc);
		}
	}
}

/*
 * 0x0047F820 - std::sort median of three for distance sort
 *
 * Returns the median value of three entity pointers using the distance
 * comparator for pivot selection.
 */
static uintptr_t
SortByDist_Median3(uintptr_t a, uintptr_t b, uintptr_t c, CLocation *refLoc)
{
	if (SortByDist_Compare(refLoc, a, b)) {
		if (SortByDist_Compare(refLoc, b, c))
			return b;
		else if (SortByDist_Compare(refLoc, a, c))
			return c;
		else
			return a;
	} else {
		if (SortByDist_Compare(refLoc, a, c))
			return a;
		else if (SortByDist_Compare(refLoc, b, c))
			return c;
		else
			return b;
	}
}

/*
 * 0x0047F940 - std::sort partition for distance sort
 *
 * Hoare partition scheme using distance comparator. Scans forward for
 * elements not closer than pivot, backward for elements not farther than
 * pivot, swaps them. Returns partition point.
 */
static uintptr_t *
SortByDist_Partition(uintptr_t *begin, uintptr_t *end, uintptr_t pivot, CLocation *refLoc)
{
	for (;;) {
		while (SortByDist_Compare(refLoc, *begin, pivot))
			begin++;
		end--;
		while (SortByDist_Compare(refLoc, pivot, *end))
			end--;
		if (end <= begin)
			return begin;
		vector_SwapWrapper(begin, end);
		begin++;
	}
}

/*
 * 0x004845E0 - StdPtrList16::StdPtrList16
 *
 * Copies the allocator byte from init, allocates a sentinel via Buynode,
 * and initializes an empty 16-byte element std::list.
 */
static __attribute__((unused)) void *
StdPtrList16_Constructor(StdPtrList *this, const void *init)
{
	*(uint8_t *)this = *(const uint8_t *)init;
	this->head = StdFileList_Buynode(this, NULL, NULL);
	this->size = 0;
	return this;
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
static __attribute__((unused)) void
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

	StdPtrIter_PostInc(&pos, &postIncTemp, 0);
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
 * MODIFIED: the element count is read at its native width, where the MSVC
 * CRT stores it as a uint32 ahead of the slab.
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
			StdPtrList16_DestroyAll(&arr[i]);
		free((char *)this - sizeof(uintptr_t));
	} else {
		StdPtrList16_DestroyAll(this);
		if (flags & 1)
			free(this);
	}
	return NULL;
}

/*
 * 0x00484820 - std::list<16-byte>::_Insert
 *
 * Allocates a new node, links it before pos, copy-constructs the 16-byte
 * value into it, bumps the count, and stores the iterator into *result.
 *
 * MODIFIED: the value is memcpy'd into the node where the binary
 * copy-constructs it.
 */
static void
StdPtrList_Insert16(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *newNode;
	StdPtrNode *nextVal, *prevVal;

	newNode = (StdPtrNode *)StdPtrList_Charalloc(list, 2 * sizeof(void *) + sizeof(CSdbStr));
	nextVal = (pos != NULL) ? pos : newNode;
	newNode->next = nextVal;
	prevVal = (pos->prev != NULL) ? pos->prev : newNode;
	newNode->prev = prevVal;

	pos->prev = newNode;

	newNode = pos->prev;
	newNode->prev->next = newNode;

	memcpy(&newNode->value, value, sizeof(CSdbStr));

	list->size++;
	*result = newNode;
}

/*
 * 0x004848D0 - StdPtrList16::EraseAll
 *
 * While first != last, post-increments first (0x00484990), extracts value,
 * erases old position from list. Copies final iterator to result[0]. This
 * is a loop wrapper around Erase16.
 */
static void
StdPtrList16_EraseAll(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;
	StdPtrNode *tmp;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc(&first, &postIncTemp, 0);
		tmp = *(StdPtrNode **)&postIncTemp;
		StdPtrList_Erase16(list, &postIncTemp, tmp);
	}
	*result = first;
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
 * 0x004849C0 - StdPtrList::StdPtrList
 *
 * Copies the allocator byte from init, allocates a sentinel via
 * StdPtrList_Buynode, and initializes an empty std::list.
 */
static __attribute__((unused)) void *
StdPtrList_Constructor_NPC(StdPtrList *this, const void *init)
{
	*(uint8_t *)this = *(const uint8_t *)init;
	this->head = StdPtrList_Buynode(this, NULL, NULL);
	this->size = 0;
	return this;
}

/*
 * 0x00484A00 - StdPtrList16::DestroyAll
 *
 * Gets Begin and End iterators, erases entire range via
 * StdPtrList_EraseRange16, frees sentinel node, zeroes head/size.
 */
static void
StdPtrList16_DestroyAll(StdPtrList *list)
{
	StdPtrNode *endNode, *beginNode, *result;

	StdPtrList_End(list, &endNode);
	StdPtrList_Begin(list, &beginNode);
	StdPtrList_EraseRange16(list, &result, beginNode, endNode);
	free(list->head);
	list->head = NULL;
	list->size = 0;
}

/*
 * 0x00484B40 - std::list<16-byte>::erase range
 *
 * Erases nodes from first to last (exclusive). While first != last,
 * post-increments first and erases the old node via StdPtrList_Erase.
 * Stores final iterator to *result.
 */
static void
StdPtrList_EraseRange16(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc(&first, &postIncTemp, 0);
		StdPtrList_Erase(list, &postIncTemp, *(StdPtrNode **)&postIncTemp);
	}
	*result = first;
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
static __attribute__((unused)) void *
CVector_VecDestructor_Region(CVector *this, int flags)
{
	if (flags & 2) {
		int i;
		// Custom: 64-bit - sizeof(uintptr_t) header for alignment
		char *alloc = (char *)this - sizeof(uintptr_t);
		int count = *(uint32_t *)alloc;
		for (i = count - 1; i >= 0; i--)
			CVector_Destructor(&this[i]);
		free(alloc);
	} else {
		CVector_Destructor(this);
		if (flags & 1)
			free(this);
	}
	return NULL;
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
	vector_DestroyRange(newEnd, vec->end);
	vec->end = newEnd;

	return first;
}

/*
 * 0x004A67F0 - std::_Uninit_copy for CVector<CRegion*>
 *
 * Forward copy of 4-byte pointer elements from [first, last) to dest.
 * Template instantiation for CVector<CRegion*> used in RegionGrid. Returns
 * pointer past last copied element.
 */
static __attribute__((unused)) void **
UninitCopy_CRegionPtr(void **first, void **last, void **dest)
{
	while (first != last) {
		*dest = *first;
		dest++;
		first++;
	}
	return dest;
}

/*
 * 0x004A6830 - std::_Uninit_fill_n for CVector<CRegion*>
 *
 * Fills count 4-byte pointer slots starting at first with *value. Template
 * instantiation for CVector<CRegion*> used in RegionGrid.
 */
static __attribute__((unused)) void
UninitFillN_CRegionPtr(void **first, uint32_t count, void **value)
{
	while (count > 0) {
		*first = *value;
		first++;
		count--;
	}
}

/*
 * 0x004CF490 - std::_Ucopy (AnimSequence CVector variant A, 62 bytes)
 *
 * Copies 4-byte elements from [first, last) to dest via construct
 * (0x00424FC0). Returns pointer past last dest element.
 */
static __attribute__((unused)) void *
CVector_Ucopy_F490(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x004CF4D0 - std::_Ufill_n (AnimSequence CVector variant A, 57 bytes)
 *
 * Fills count 4-byte elements at dest by calling construct (0x00424FC0)
 * per element.
 */
static __attribute__((unused)) void
CVector_UfillN_F4D0(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x004CF510 - std::_Ucopy (AnimSequence CVector variant B, 62 bytes)
 *
 * Identical to 0x004CF490.
 */
static __attribute__((unused)) void *
CVector_Ucopy_F510(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x004CF550 - std::_Ufill_n (AnimSequence CVector variant B, 57 bytes)
 *
 * Identical to 0x004CF4D0.
 */
static __attribute__((unused)) void
CVector_UfillN_F550(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
}

/*
 * 0x004CF590 - std::_Ucopy (AnimSequence CVector variant C, 62 bytes)
 *
 * Identical to 0x004CF490.
 */
static __attribute__((unused)) void *
CVector_Ucopy_F590(CVector *this, void *first, void *last, void *dest)
{
	uintptr_t *src = (uintptr_t *)first;
	uintptr_t *end = (uintptr_t *)last;
	uintptr_t *dst = (uintptr_t *)dest;

	while (src != end) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x004CF5D0 - std::_Ufill_n (AnimSequence CVector variant C, 57 bytes)
 *
 * Identical to 0x004CF4D0.
 */
static __attribute__((unused)) void
CVector_UfillN_F5D0(CVector *this, void *dest, uint32_t count, void *valuePtr)
{
	uintptr_t *dst = (uintptr_t *)dest;

	while (count > 0) {
		StdPtrList_DoInsert_424FC0((StdPtrList *)this, dst, valuePtr);
		count--;
		dst++;
	}
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
 * 0x004E3500 - std::_Tree iterator copy (MSVC CRT, 32 bytes)
 *
 * Copies iterator value from src to dest. MSVC CRT internal - stub for
 * compilation.
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
	StdTree_EraseRange(tree, head->left, head);

	// Free head node.
	free(head);
	tree->head = NULL;
	tree->size = 0;

	// Decrement nil sentinel refcount.
	g_HandleMapNilRef--;
	if (g_HandleMapNilRef == 0) {
		free(g_HandleMapNil);
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

	// Walk tree to find insertion point (binary: 0x004E643D..0x004E6462).
	while (x != nil) {
		addLeft = key < x->key;
		y = x;
		if (addLeft)
			x = x->left;
		else
			x = x->right;
	}

	// Check for duplicate key (binary: 0x004E646D..0x004E64FC).
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
 * 0x004E6500 - std::_Tree::erase (RB delete with fixup, 1321 bytes)
 *
 * Erases a single node from the tree. Handles all red-black rebalancing
 * cases. Updates head's leftmost/rightmost. Decrements size. Frees node.
 */
void
StdTree_RBErase(StdMapTree *tree, StdTreeNode *z)
{
	StdTreeNode *head = tree->head;
	StdTreeNode *nil = g_HandleMapNil;
	StdTreeNode *y;  // node to splice out
	StdTreeNode *x;  // child of spliced node
	StdTreeNode *xp; // parent of x (saved because x might be nil)

	// Determine which node to splice out.
	if (z->left == nil) {
		y = z;
		x = z->right;
	} else if (z->right == nil) {
		y = z;
		x = z->left;
	} else {
		// Two children: find successor.
		y = z->right;
		while (y->left != nil)
			y = y->left;
		x = y->right;
	}

	// Splice y out of the tree.
	if (y != z) {
		// y is z's successor (not z itself).
		// Relink y in z's position.
		z->left->parent = y;
		y->left = z->left;

		if (y == z->right) {
			xp = y;
		} else {
			xp = y->parent;
			if (x != nil)
				x->parent = y->parent;
			y->parent->left = x;
			y->right = z->right;
			z->right->parent = y;
		}

		if (z == head->parent)
			head->parent = y;
		else if (z == z->parent->left)
			z->parent->left = y;
		else
			z->parent->right = y;

		y->parent = z->parent;

		// Swap colors of y and z.
		{
			int tmp = y->color;
			y->color = z->color;
			z->color = tmp;
		}
		y = z; // y now points to the node to free
	} else {
		// y == z: splice z out directly.
		xp = z->parent;
		if (x != nil)
			x->parent = z->parent;

		if (z == head->parent)
			head->parent = x;
		else if (z == z->parent->left)
			z->parent->left = x;
		else
			z->parent->right = x;

		// Update leftmost/rightmost.
		if (z == head->left) {
			if (z->right == nil)
				head->left = z->parent;
			else {
				StdTreeNode *m = x;
				while (m->left != nil)
					m = m->left;
				head->left = m;
			}
		}
		if (z == head->right) {
			if (z->left == nil)
				head->right = z->parent;
			else {
				StdTreeNode *m = x;
				while (m->right != nil)
					m = m->right;
				head->right = m;
			}
		}
	}

	// Red-black fixup if the spliced node was black.
	if (y->color == 1) {
		while (x != head->parent && (x == nil || x->color == 1)) {
			if (x == xp->left) {
				StdTreeNode *w = xp->right;
				if (w->color == 0) {
					// Case 1: sibling is red.
					w->color = 1;
					xp->color = 0;
					// Left rotate at xp.
					{
						StdTreeNode *r = xp->right;
						xp->right = r->left;
						if (r->left != nil)
							r->left->parent = xp;
						r->parent = xp->parent;
						if (xp == head->parent)
							head->parent = r;
						else if (xp == xp->parent->left)
							xp->parent->left = r;
						else
							xp->parent->right = r;
						r->left = xp;
						xp->parent = r;
					}
					w = xp->right;
				}
				if ((w->left == nil || w->left->color == 1) && (w->right == nil || w->right->color == 1)) {
					// Case 2: sibling's children are both black.
					w->color = 0;
					x = xp;
					xp = xp->parent;
				} else {
					if (w->right == nil || w->right->color == 1) {
						// Case 3: sibling's right child is black.
						if (w->left != nil)
							w->left->color = 1;
						w->color = 0;
						// Right rotate at w.
						{
							StdTreeNode *r = w->left;
							w->left = r->right;
							if (r->right != nil)
								r->right->parent = w;
							r->parent = w->parent;
							if (w == head->parent)
								head->parent = r;
							else if (w == w->parent->left)
								w->parent->left = r;
							else
								w->parent->right = r;
							r->right = w;
							w->parent = r;
						}
						w = xp->right;
					}
					// Case 4: sibling's right child is red.
					w->color = xp->color;
					xp->color = 1;
					if (w->right != nil)
						w->right->color = 1;
					// Left rotate at xp.
					{
						StdTreeNode *r = xp->right;
						xp->right = r->left;
						if (r->left != nil)
							r->left->parent = xp;
						r->parent = xp->parent;
						if (xp == head->parent)
							head->parent = r;
						else if (xp == xp->parent->left)
							xp->parent->left = r;
						else
							xp->parent->right = r;
						r->left = xp;
						xp->parent = r;
					}
					break;
				}
			} else {
				// Mirror: x is right child.
				StdTreeNode *w = xp->left;
				if (w->color == 0) {
					w->color = 1;
					xp->color = 0;
					// Right rotate at xp.
					{
						StdTreeNode *r = xp->left;
						xp->left = r->right;
						if (r->right != nil)
							r->right->parent = xp;
						r->parent = xp->parent;
						if (xp == head->parent)
							head->parent = r;
						else if (xp == xp->parent->right)
							xp->parent->right = r;
						else
							xp->parent->left = r;
						r->right = xp;
						xp->parent = r;
					}
					w = xp->left;
				}
				if ((w->right == nil || w->right->color == 1) && (w->left == nil || w->left->color == 1)) {
					w->color = 0;
					x = xp;
					xp = xp->parent;
				} else {
					if (w->left == nil || w->left->color == 1) {
						if (w->right != nil)
							w->right->color = 1;
						w->color = 0;
						// Left rotate at w.
						{
							StdTreeNode *r = w->right;
							w->right = r->left;
							if (r->left != nil)
								r->left->parent = w;
							r->parent = w->parent;
							if (w == head->parent)
								head->parent = r;
							else if (w == w->parent->right)
								w->parent->right = r;
							else
								w->parent->left = r;
							r->left = w;
							w->parent = r;
						}
						w = xp->left;
					}
					w->color = xp->color;
					xp->color = 1;
					if (w->left != nil)
						w->left->color = 1;
					// Right rotate at xp.
					{
						StdTreeNode *r = xp->left;
						xp->left = r->right;
						if (r->right != nil)
							r->right->parent = xp;
						r->parent = xp->parent;
						if (xp == head->parent)
							head->parent = r;
						else if (xp == xp->parent->right)
							xp->parent->right = r;
						else
							xp->parent->left = r;
						r->right = xp;
						xp->parent = r;
					}
					break;
				}
			}
		}
		if (x != nil)
			x->color = 1;
	}

	free(y);
	tree->size--;
}

/*
 * 0x004E6A30 - std::_Tree::erase range
 *
 * Erases all nodes in [first, last). If range covers the entire tree, uses
 * the fast _EraseSubtree path. Otherwise erases one-by-one.
 */
static void
StdTree_EraseRange(StdMapTree *tree, StdTreeNode *first, StdTreeNode *last)
{
	StdTreeNode *head = tree->head;

	if (tree->size != 0 && first == head->left && last == head) {
		// Full range: bulk erase.
		StdTree_EraseSubtree(tree, head->parent);
		head->parent = g_HandleMapNil;
		tree->size = 0;
		head->left = head;
		head->right = head;
	} else {
		// Partial range: erase one-by-one.
		while (first != last) {
			StdTreeNode *next = first;
			StdTree_Inc(&next);
			StdTree_RBErase(tree, first);
			first = next;
		}
	}
}

/*
 * 0x004E6C40 - std::_Tree::_Erase (recursive subtree destruction)
 *
 * Recursively erases the right subtree, then iteratively follows left
 * children, freeing each node. Stops at the nil sentinel.
 */
static void
StdTree_EraseSubtree(StdMapTree *tree, StdTreeNode *node)
{
	USED(tree);
	while (node != g_HandleMapNil) {
		StdTree_EraseSubtree(tree, node->right);
		StdTreeNode *next = node->left;
		free(node);
		node = next;
	}
}

/*
 * 0x004E6CD0 - std::_Tree::_Insert (RB insert with fixup, 789 bytes)
 *
 * Allocates a new 0x18-byte node, links it as a child of parent, and
 * performs red-black rebalancing. Updates head's leftmost/rightmost/root.
 */
static StdTreeNode *
StdTree_RBInsert(StdMapTree *tree, int addLeft, StdTreeNode *parent, uintptr_t key, uintptr_t value)
{
	StdTreeNode *head = tree->head;
	StdTreeNode *nil = g_HandleMapNil;
	StdTreeNode *node;
	StdTreeNode *x;

	// Allocate and initialize new node (binary: 0x004E6CF9..0x004E6D3A).
	node = malloc(sizeof(StdTreeNode));
	node->parent = parent;
	node->color = 0; // red
	node->left = nil;
	node->right = nil;
	node->key = key;
	node->value = value;

	tree->size++;

	// Link node into tree (binary: 0x004E6D3D..0x004E6D92).
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

	// Red-black fixup (binary: 0x004E6DA5..0x004E6FA8).
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
 * 0x004E7040 - std::_Tree::_Dec (predecessor, 181 bytes)
 *
 * Moves to the previous node in key order. Head detection: if node[0x14]
 * == 0 and node->parent->parent == node, this is the head node (end
 * iterator) and we return rightmost.
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
 * 0x004E7100 - std::_Tree::_Inc (successor, 167 bytes)
 *
 * Advances to the next node in key order. If right child exists, finds
 * leftmost in right subtree. Otherwise walks up until we're no longer a
 * right child.
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
