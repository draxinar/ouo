/*
 * CResManager - generic hashed key-to-value tables for loaded resources.
 *
 * Every subsystem that mines tokens out of the .dat files (items,
 * mobiles, regions, animations) builds a CResManager of 66 buckets with
 * parallel CList chains for keys and values, and probes them through a
 * CSearchCtx iterator. The data structure is the binary's answer to a
 * typed std::map and its layout is load-bearing.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "container.h"
#include "magicfactory.h"
#include "magiclist.h"
#include "multi.h"
#include "player.h"
#include "region.h"
#include "template.h"
#include "vtable.h"
#include "world.h"

static void *CResList_RemoveVal_ByNameAll(CResList *list, uintptr_t valNode, void **outErased, uint32_t direction); // 0x00423E90
static void CResList_ClearAll_ByNameAllVal(CResList *list); // 0x004239D0
static void SortByLocation_Entry(uintptr_t *first, uintptr_t *last, char cmp); // 0x004302B0
static void *CResList_InsertBefore_ByNameAll(CResList *list, CResListNode *position); // 0x00430720
static void *CResManager_NextIterErase_ByNameAll(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction); // 0x004305D0
static void *CResManager_FindNextBucket_ByNameAll(CResManager *rm, CSearchCtx *out, uint32_t bucket, uint32_t direction); // 0x004304D0
static void SortByLocation_Main(uintptr_t *first, uintptr_t *last, char cmp); // 0x004308C0
static void SortBySerial_Main(uintptr_t *first, uintptr_t *last, char cmp); // 0x00430950
static void *CResList_InsertAfter_ByNameAll(CResList *list, CResListNode *position); // 0x004309E0
static void SortByLocation_InsertionEntry(uintptr_t *first, uintptr_t *last, char cmp); // 0x00430AD0
static void SortByLocation_Quicksort(uintptr_t *first, uintptr_t *last, char cmp, int depth); // 0x00430B00
static void SortByLocation_LinearInsert(uintptr_t *pos, uintptr_t value, char cmp); // 0x00430BD0
static void SortBySerial_InsertionEntry(uintptr_t *first, uintptr_t *last, char cmp); // 0x00430CD0
static void SortBySerial_Quicksort(uintptr_t *first, uintptr_t *last, char cmp, int depth); // 0x00430D00
static void SortBySerial_LinearInsert(uintptr_t *pos, uintptr_t value, char cmp); // 0x00430DD0
static void SortByLocation_Insertion(uintptr_t *first, uintptr_t *last, char cmp, int depth); // 0x00430E80
static uintptr_t SortByLocation_Median3(uintptr_t a, uintptr_t b, uintptr_t c, char cmp); // 0x00430F10
static uintptr_t *SortByLocation_Partition(uintptr_t *left, uintptr_t *right, uintptr_t pivot, char cmp); // 0x00430FE0
static void SortBySerial_Insertion(uintptr_t *first, uintptr_t *last, char cmp, int depth); // 0x00431060
static uintptr_t SortBySerial_Median3(uintptr_t a, uintptr_t b, uintptr_t c, char cmp); // 0x004310F0
static uintptr_t *SortBySerial_Partition(uintptr_t *left, uintptr_t *right, uintptr_t pivot, char cmp); // 0x004311C0
static CResListNode *CStringList_InsertEntry(CStringList *sl, void *data, int direction); // 0x0043F3D0
static void CResList_DestructorSLN(CResList *list); // 0x0043F3B0
static CStringListNode *CStringListEntry_Constructor(CStringListNode *this, CString *name, int weight); // 0x0043F2F0
static CResList *CResList_ScalarDeleteT(CResList *list, int flags); // 0x0043F2A0
static CResList *CResList_ScalarDeleteC(CResList *list, int flags); // 0x0043F270
static CResList *CResList_ScalarDeleteS(CResList *list, int flags); // 0x0043F240
static CResList *CResList_ScalarDeleteI(CResList *list, int flags); // 0x0043F210
static void CStringListNode_Destructor(CStringListNode *node); // 0x0043F1F0
static void CStringList_DestructorEffects(CStringList *sl); // 0x0043EDF0
static void CResList_SortedInsert(CResList *list, void *data, int direction, int orderedFlag); // 0x0043F5B0
static void CResList_DestructorI(CResList *list); // 0x0043FC10
static void CResList_SetValueOnNodeI(CResList *list, CResListNode *node, void *value); // 0x0043FBF0
static CResListNode *CResList_AllocTailNodeI(CResList *list); // 0x0043FB40
static void CResManager_SearchBucketD(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction); // 0x0043FC50
static void CResList_DestructorS(CResList *list); // 0x0043FDD0
static void CResList_SetValueOnNodeS(CResList *list, CResListNode *node, void *value); // 0x0043FDB0
static CResListNode *CResList_AllocTailNodeS(CResList *list); // 0x0043FD00
static void CResManager_SearchBucketC(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction); // 0x0043FDF0
static void CResList_DestructorC(CResList *list); // 0x0043FF70
static void CResList_SetValueOnNodeC(CResList *list, CResListNode *node, void *value); // 0x0043FF50
static CResListNode *CResList_AllocTailNodeC(CResList *list); // 0x0043FEA0
static void CResList_DestructorT(CResList *list); // 0x00440360
static void CResList_SetValueOnNodeEffects(CResList *list, CResListNode *node, void *srcData); // 0x00440340
static CResListNode *CResList_AllocTailNodeE(CResList *list); // 0x00440290
static CResListNode *CResList_GetOrCreateNodeBin(CResList *list, void *data); // 0x00440490
static CResListNode *CResList_InsertDirectionE(CResList *list, void *srcData, int direction); // 0x004403F0
static void CResList_DestructorE(CResList *list); // 0x004403D0
static int *CStringList_GetWeightEffects(CStringList *sl, CResListNode **nodePtr); // 0x004403B0
static void CResList_InsertAtNodeStr(CResList *list, CResListNode *node, void *data, int direction); // 0x00440690
static void CResList_InsertAtEndStr(CResList *list, void *data, int direction); // 0x004407A0
static void *CResManager_NextEntryA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x00440900
static void CResList_ClearInternalI(CResList *list); // 0x00440AD0
static CResListNode *CResList_RecycleNodeI(CResList *list, CResListNode *afterNode); // 0x004409E0
static void CResListNode_ReplaceDataI(CResListNode *node, void *newData); // 0x00440B40
static void *CResManager_NextEntryD(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x00440BA0
static void CResListNode_ReplaceDataS(CResListNode *node, void *newData); // 0x00440DB0
static void CResList_ClearInternalS(CResList *list); // 0x00440D70
static CResListNode *CResList_RecycleNodeS(CResList *list, CResListNode *afterNode); // 0x00440C80
static void *CResManager_NextEntryC(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x00440E10
static void CResListNode_ReplaceData(CResListNode *node, void *newData); // 0x00441020
static void CResList_ClearInternalC(CResList *list); // 0x00440FE0
static void *CResManager_NextEntryB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x00441080
static CResListNode *CResList_AllocBackwardE(CResList *list, void *srcData); // 0x00441360
static CResListNode *CResList_AllocForwardE(CResList *list, void *srcData); // 0x00441330
static void CResList_ClearInternalE(CResList *list); // 0x004412F0
static void CResListNode_SetDataSLE(CResListNode *node, void *srcData); // 0x00441290
static void CResList_ClearInternalT(CResList *list); // 0x00441250
static CResListNode *CResList_RecycleNodeE(CResList *list, CResListNode *afterNode); // 0x00441160
static CResListNode *CResListNode_ScalarDeleteStr(CResListNode *node, int flags); // 0x00441390
static void CResListNode_SetDataNP(CResListNode *node, CNamedProperty *src); // 0x00441580
static CResListNode *CResList_FreeNodeSLN(CResList *list, CResListNode *node, int direction); // 0x00441520
static CResListNode *CResList_GetOrAllocTailNP(CResList *list); // 0x00441470
static CResListNode *CResList_GetOrAllocHeadNP(CResList *list); // 0x004413C0
static CResListNode *CResList_AppendStr(CResList *list, void *data); // 0x004417B0
static CResListNode *CResList_PrependStr(CResList *list, void *data); // 0x00441780
static CResListNode *CResList_InsertAfterStr(CResList *list, CResListNode *afterNode, void *data); // 0x00441740
static CResListNode *CResList_InsertBeforeStr(CResList *list, CResListNode *beforeNode, void *data); // 0x00441700
static void CResListNode_DestructorStr(CResListNode *node); // 0x004417E0
static void CResListNodeE_Destructor(CResListNode *node); // 0x00442910
static void CResListNodeT_Destructor(CResListNode *node); // 0x004428C0
static void CResListNodeC_Destructor(CResListNode *node); // 0x00442870
static void CResListNodeS_Destructor(CResListNode *node); // 0x00442820
static void CResListNodeI_Destructor(CResListNode *node); // 0x004427D0
static void CResListNodeSLN_Destructor(CResListNode *node); // 0x00442780
static CResListNode *CResListNodeE_ScalarDelete(CResListNode *node, int flags); // 0x00442750
static CResListNode *CResListNodeT_ScalarDelete(CResListNode *node, int flags); // 0x00442720
static CResListNode *CResListNodeC_ScalarDelete(CResListNode *node, int flags); // 0x004426F0
static CResListNode *CResListNodeS_ScalarDelete(CResListNode *node, int flags); // 0x004426C0
static CResListNode *CResListNodeI_ScalarDelete(CResListNode *node, int flags); // 0x00442690
static CResListNode *CResListNodeSLN_ScalarDelete(CResListNode *node, int flags); // 0x00442660
static CResListNode *CResList_UnlinkNodeE(CResList *list, CResListNode *node, void **outData, int direction); // 0x00442590
static CResListNode *CResList_InsertBeforeHeadE(CResList *list, CResListNode *afterNode); // 0x004424A0
static CResListNode *CResList_InsertAfterTailE(CResList *list, CResListNode *beforeNode); // 0x004423B0
static CResListNode *CResList_UnlinkNodeT(CResList *list, CResListNode *node, void **outData, int direction); // 0x004422E0
static CResListNode *CResList_UnlinkNodeC(CResList *list, CResListNode *node, void **outData, int direction); // 0x00442210
static CResListNode *CResList_UnlinkNodeS(CResList *list, CResListNode *node, void **outData, int direction); // 0x00442140
static CResListNode *CResList_UnlinkNodeI(CResList *list, CResListNode *node, void **outData, int direction); // 0x00442070
static void CResListNode_SetDataStr(CResListNode *node, void *srcData); // 0x00442010
static CResListNode *CResList_UnlinkNodeSLN(CResList *list, CResListNode *node, void **outData, int direction); // 0x00441F40
static CResListNode *CResList_InsertBeforeHeadNP(CResList *list, CResListNode *tailNode); // 0x00441E50
static CResListNode *CResList_InsertAfterTailNP(CResList *list, CResListNode *headNode); // 0x00441D60
static CNamedProperty *CNamedProperty_Copy(CNamedProperty *this, CNamedProperty *src); // 0x00441D20
static CNamedProperty *CNamedProperty_Constructor(CNamedProperty *this, CNamedProperty *src); // 0x00441CC0
static CStringListNode *CStringListNode_ScalarDelete(CStringListNode *node, int flags); // 0x00441BF0
static void CResList_SetDataE(CResListNode *node, void *srcData); // 0x00441B90
static CResListNode *CResList_FreeNodeE(CResList *list, CResListNode *node, int direction); // 0x00441B30
static CResListNode *CResList_GetOrAllocTailE(CResList *list); // 0x00441A80
static CResListNode *CResList_GetOrAllocHeadE(CResList *list); // 0x004419D0
static CResListNode *CResList_FreeNodeT(CResList *list, CResListNode *node, int direction); // 0x00441970
static CResListNode *CResList_FreeNodeC(CResList *list, CResListNode *node, int direction); // 0x00441910
static CResListNode *CResList_FreeNodeS(CResList *list, CResListNode *node, int direction); // 0x004418B0
static CResListNode *CResList_FreeNodeI(CResList *list, CResListNode *node, int direction); // 0x00441850
static CResListNode *CResList_AllocAndSetData(CResList *list, uint32_t *valuePtr); // 0x0045F300
static CResListNode *CResList_AllocNode(CResList *list); // 0x0045F3A0
static CResListNode *CResList_PushFront_SpawnLocal(CResList *list, CResListNode *afterNode); // 0x0045F5D0
static void CResManager_Clear_Hint(CResManager *this); // 0x00464830
static void *CResManager_SearchBucketHint(CResManager *rm, CSearchCtx *output, uint32_t *key, CSearchCtx *startCtx, int direction); // 0x00464680
static void CResList_ValNodeDestructor_HintVariant(CResListNode *this); // 0x00465400
static void CResList_ClearAll_HintVal(CResList *this); // 0x004653C0
static CResListNode *CResList_PushFront_ResManager2(CResList *list, CResListNode *position); // 0x004652D0
static void CResList_ClearAll_HintKey(CResList *this); // 0x00465290
static CResListNode *CResList_Erase_ResManager2(CResList *list, CResListNode *node, void **outData, int direction); // 0x004651C0
static CResListNode *CResList_PushFront_ResManager(CResList *list, CResListNode *position); // 0x004650D0
static void *CResListValNode_ScalarDelete_HintVar(CResListNode *this, int flags); // 0x004650A0
static void CResListNode_SetDataHint(CResListNode *node, void *data); // 0x00465040
static CResListNode *CResList_Erase_ResManager(CResList *list, CResListNode *node, void **outData, int direction); // 0x00464F50
static CResListNode *CResList_AllocTailNodeHintKey(CResList *list); // 0x00464EA0
static void *CResManager_NextEntryHint(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x00464CA0
static CResListNode *CResList_EraseAndFree_Hint(CResList *list, CResListNode *node, int direction); // 0x00465480
static void CResList_ClearInternal_MagicItemList(CResList *this); // 0x00466CA0
static void CResList_Destructor_MagicItemList(CResList *this); // 0x00466CE0
static CResListNode *CResList_Erase_Region(CResList *list, CResListNode *node, void **outData, int direction); // 0x00466ED0
static CResListNode *CResList_AllocAndAppend(CResList *list); // 0x00473F50
static CResListNode *CResList_InsertAfterNode(CResList *list, CResListNode *afterNode); // 0x00474140
static CResListNode *CResList_RemoveNode_Bin(CResList *list, CResListNode *node, void **outData, int direction); // 0x00474000
static void CResManager_ClearMultiA(CResManager *rm); // 0x0047A510
static void CResList_ConstructorEmpty(CResList *this); // 0x00474473
static CResListNode *CResList_EraseAndFree_MultiVal(CResList *list, CResListNode *node, int direction); // 0x0047D040
static CResListNode *CResList_PushFront_MultiC(CResList *list, CResListNode *afterNode); // 0x0047CF50
static CResListNode *CResList_EraseAndFree_MultiB(CResList *list, CResListNode *node, int direction); // 0x0047CEA0
static CResListNode *CResList_PushFront_MultiB(CResList *list, CResListNode *afterNode); // 0x0047CDB0
static CResListNode *CResList_PushFront_MultiA(CResList *list, CResListNode *afterNode); // 0x0047CBF0
static void CResList_ReplaceData_C(CResListNode *this, void *data); // 0x0047C780
static void CResList_ClearAll_MultiVal(CResList *list); // 0x0047C6F0
static void *CResList_BeginIter_MultiC(CResList *list); // 0x0047C640
static CSearchCtx *CResManager_NextIter_KeyVal_B(CResManager *this, CSearchCtx *searchCtx, CSearchCtx *output, int direction); // 0x0047C560
static void CResManager_BeginIterInternalMultiB(CResManager *rm, CSearchCtx *output, int startBucket, int direction); // 0x0047C460
static void CResList_ReplaceData_B(CResListNode *this, void *data); // 0x0047C400
static void CResList_ClearAll_MultiB(CResList *list); // 0x0047C3C0
static CResListNode *CResList_Erase_MultiKey(CResList *list, CResListNode *node, void **outData, int direction); // 0x0047C2F0
static CResListNode *CResList_BeginIter_MultiB(CResList *list); // 0x0047C240
static void CResList_ClearAll_MultiA(CResList *list); // 0x0047C200
static CResListNode *CResList_BeginIter_MultiA(CResList *list); // 0x0047C100
static CSearchCtx *CResManager_NextIter_KeyVal_A(CResManager *this, CSearchCtx *searchCtx, CSearchCtx *output, int direction); // 0x0047C020
static void *CResList_PrependData_C(CResList *this, void *data); // 0x0047BE70
static CResListNode *CResList_Erase_MultiVal(CResList *list, CResListNode *node, void **outData, int direction); // 0x0047BD80
static void *CResManager_NextIterInternalMultiB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x0047BC30
static void CResManager_ClearMultiB(CResManager *rm); // 0x0047BB50
static CSearchCtx *CResManager_FindByKey_B2(CResManager *this, CSearchCtx *output, void *keyPtr, CSearchCtx *searchCtx, int direction); // 0x0047BAA0
static void *CResList_PrependData_B(CResList *this, void *data); // 0x0047BA70
static void *CResList_PrependInt_A(CResList *this, uint32_t *valuePtr); // 0x0047BA20
static void *CResManager_EraseMultiA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, void **outData, int direction); // 0x0047B880
static void *CResManager_NextIterInternalMultiA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x0047B730
static void CResManager_BeginIterInternalMultiA(CResManager *rm, CSearchCtx *output, int startBucket, int direction); // 0x0047B630
static CSearchCtx *CResManager_FindByKey_B(CResManager *this, CSearchCtx *output, void *keyPtr, CSearchCtx *searchCtx, int direction); // 0x0047B580
static int CResManager_FindOrInsertMultiB(CResManager *rm, uint32_t *keyPtr, void *valuePtr); // 0x0047A930
static CSearchCtx *CResManager_CreateOrFind_R(CResManager *this, CSearchCtx *output, CSearchCtx *current, int direction); // 0x0047A840
static void CResListNode_FreeData(CResListNode *node); // 0x0047D460
static void *CResList_GetHead(CResListNode *this); // 0x004A6FD0
static void *CResList_StoreVal_ByFile(CResList *list, void *value); // 0x004A7E10
static void *CResList_AdvanceValIter(CResListNode *this, uint32_t direction); // 0x004A7D70
static void CResList_Destructor_ByFileVal(CResList *this); // 0x004A7D50
static void *CResList_RemoveVal_ByFile(CResList *list, uintptr_t valNode, void **outErased, uint32_t direction); // 0x004A7C80
static void *CResList_InsertAfter_ByFile(CResList *list, CResListNode *position); // 0x004A7B90
static void *CResManager_NextIterErase_ByFile(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction); // 0x004A7A40
static void *CResManager_NextIterStep_ByFile(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction); // 0x004A7960
static void *ScalarDestructor_ByFileVal(CResList *this, int flag); // 0x004A7900
static CSearchCtx *CResManager_InternalErase_ByNameAll(CResManager *rm, CSearchCtx *eraseOut, CSearchCtx *current, void **outErased, uint32_t direction); // 0x004A7780
static CSearchCtx *CResManager_FindOrCreate_ByNameAll(CResManager *rm, CSearchCtx *findCtx, uint32_t *keyPtr); // 0x004A75F0
static void CResManager_StrKeyDestructor_Clear_ByNameAll(CResManager *rm); // 0x004A7510
static void *CResManager_BeginIterInternal_ByNameAll(CResManager *rm, CSearchCtx *out, uint32_t *keyPtr, CSearchCtx *localCtx, uint32_t direction); // 0x004A7460
static void *CResList_InsertVal_ByFile(CResList *list, void *value); // 0x004A7430
static void CResList_NextIter_ByFile(CResList *list, uintptr_t valNode, uint32_t direction); // 0x004A7410
static void *CResList_GetTailVal_ByFile(CResList *list); // 0x004A7360
static CSearchCtx *CResManager_InternalErase_ByFile(CResManager *rm, CSearchCtx *eraseOut, CSearchCtx *current, void **outErased, uint32_t direction); // 0x004A71E0
static void CResManager_IntKeyDestructor_Clear_ByFile(CResManager *rm); // 0x004A7100
static void *CResManager_BeginIterInternal_ByFile(CResManager *rm, CSearchCtx *out, uint32_t *keyPtr, CSearchCtx *localCtx, uint32_t direction); // 0x004A7050
static void *CResListNode_GetData(CResListNode *node); // 0x004A7E70
static void *CResManager_NextIterStep_ByNameAll(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction); // 0x004A7E90
static void CResList_ClearAll_ByFileVal(CResList *this); // 0x004A80A0
static void *CResManager_FindNextBucket_ByFile(CResManager *rm, CSearchCtx *out, uint32_t bucket, uint32_t direction); // 0x004A7FA0
static void CResList_ValNodeDestructor_Region(CResListNode *this); // 0x004A80E0
static void CSearchCtx_FreeKeyVal(CSearchCtx *this); // 0x004A864D
static void *CResList_EraseAndFree_ByFileVal(CResList *list, uintptr_t valNode, uint32_t direction); // 0x004A8150
static void *CResManager_FindByInt_Templates(CResManager *rm, CSearchCtx *output, void *keyPtr, int direction); // 0x004C0C40
static int CResManager_InsertIntEntryG(CResManager *rm, void *keyPtr, void *valPtr); // 0x004C0AF0
static void CResManager_ClearJ_Thunk(CResManager *this); // 0x004C0F50
static void CResListNode_SetStringIfValid(CResListNode *this, void *node, void *src); // 0x004C1500
static void CResList_Destructor_TemplatesVal(CResList *this); // 0x004C1620
static CResListNode *CResList_AllocForwardF(CResList *list, void *srcData); // 0x004C1640
static void *CResManager_SearchBucket_TemplatesInt(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction); // 0x004C1690
static void CResList_Destructor_NameTableVal(CResList *this); // 0x004C1740
static void CStringPairList_Destructor(CResList *this); // 0x004C1D10
static void CResManager_ClearJ(CResManager *rm); // 0x004C1A80
static CSearchCtx *CResManager_FindOrInsertH(CResManager *rm, CSearchCtx *output, CString *key); // 0x004C1840
static void *CResManager_SearchBucket_DefinesStr(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction); // 0x004C1790
static CResListNode *CResList_AllocForwardG(CResList *list, void *srcData); // 0x004C1760
static CResListNode *CResList_BeginIterF(CResList *list); // 0x004C2330
static void *CResManager_NextIterInternal_TemplatesB(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction); // 0x004C2250
static void *CResList_ScalarDelete_TemplatesVar(CResList *this, int flags); // 0x004C2220
static void CResList_ClearInternal_TemplatesVal(CResList *this); // 0x004C23E0
static CResListNode *CResList_BeginIterG(CResList *list); // 0x004C2560
static void *CResManager_NextIterInternal_NameTable(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction); // 0x004C2480
static void CResListNode_ReplaceDataF(CResListNode *node, void *newData); // 0x004C2420
static void CResList_ClearInternal_NameTableVal(CResList *this); // 0x004C2610
static CResListNode *CResList_EraseAndFree_K(CResList *list, CResListNode *node, int direction); // 0x004C3390
static CResListNode *CResList_UnlinkNodeG(CResList *list, CResListNode *node, void **outData, int direction); // 0x004C32C0
static CResListNode *CResList_UnlinkNodeF(CResList *list, CResListNode *node, void **outData, int direction); // 0x004C31F0
static void CStringPairListNode_FieldDestructor(CStringPairListNode *this); // 0x004C31A0
static void CStringPairListNode_Destructor(CStringPairListNode *this); // 0x004C3180
static void CResList_AllocForward_LabelsWrapper(CResListNode *node, void *newData); // 0x004C30F0
static CResListNode *CResList_EraseAndFree_Labels(CResList *list, CResListNode *node, int direction); // 0x004C3090
static CResListNode *CResList_AllocTailNode_LabelsVal(CResList *list); // 0x004C2FE0
static CResListNode *CResList_AllocTailNode_TemplatesKey(CResList *list); // 0x004C2F30
static void CResList_ClearAll_NameTable(CResList *this); // 0x004C2EF0
static CResListNode *CResList_RecycleNodeJ(CResList *list, CResListNode *afterNode); // 0x004C2E00
static CResListNode *CResList_RecycleNodeG(CResList *list, CResListNode *afterNode); // 0x004C2CB0
static CResListNode *CResList_RecycleNodeF(CResList *list, CResListNode *afterNode); // 0x004C2B60
static void CResList_ValNodeDestructor_NameTable(CNameEntry *this); // 0x004C2B40
static void *CResListValNode_ScalarDelete_NameTbl(CNameEntry *this, int flags); // 0x004C2B10
static void CResList_RemoveAll(CResList *this); // 0x004C2940
static void CResList_Destructor_TemplatesVariant(CResList *this); // 0x004C2920
static void *CResManager_NextIterInternal_Labels(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction); // 0x004C2790
static void *CResManager_NextIterInternal_Defines(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction); // 0x004C26B0
static void CResListNode_ReplaceDataG(CResListNode *node, void *newData); // 0x004C2650
static void CResListNodeJ2_Destructor(CResListNode *node); // 0x004C38B0
static CResListNode *CResList_UnlinkNode_K(CResList *list, CResListNode *node, void **outData, int direction); // 0x004C37E0
static void CResListNodeG_Destructor(CResListNode *node); // 0x004C3790
static void CResListNodeF_Destructor(CResListNode *node); // 0x004C3740
static CResListNode *CResListNodeJ2_ScalarDelete(CResListNode *node, int flags); // 0x004C3710
static CResListNode *CResListNodeG_ScalarDelete(CResListNode *node, int flags); // 0x004C36E0
static CResListNode *CResListNodeF_ScalarDelete(CResListNode *node, int flags); // 0x004C36B0
static CResListNode *CResList_UnlinkNodeJ2(CResList *list, CResListNode *node, void **outData, int direction); // 0x004C35E0
static CResListNode *CResList_AllocTailNodeK(CResList *list, CResListNode *afterNode); // 0x004C34F0
static CResListNode *CResList_InsertBeforeJ(CResList *list, CResListNode *afterNode); // 0x004C3400

// CRandom - macro mapping to rand() (binary thiscall on CRandom singleton).
#define CRandom() rand()

/*
 * 0x00401450 - CSearchCtx::GetBucket
 *
 * Returns bucket field (+0x04). Binary function is shared with
 * CString::GetLength (same offset, different type).
 */
uintptr_t
CSearchCtx_GetBucket(CSearchCtx *ctx)
{
	return ctx->bucket;
}

/*
 * 0x00421000 - CSearchCtx::Find
 *
 * Returns entity field (non-zero if found).
 */
int
CSearchCtx_Find(CSearchCtx *ctx)
{
	return (int)ctx->entity;
}

/*
 * 0x00423250 - CIterCtx::Set
 *
 * Stores arg into *this and returns this.
 */
void *
CIterCtx_Set(void *ctx, void *arg)
{
	*(void **)ctx = arg;
	return ctx;
}

/*
 * 0x004238D0 - CResList::begin
 *
 * Returns pointer to head node (iterator start).
 */
CResListNode *
CResList_Begin(CResList *list)
{
	return list->head;
}

/*
 * 0x004238E0 - CResList::~CResList (ByName/All vals)
 *
 * Destructor for the ByName/All val list: walks the list and frees
 * each entry via CResList_ClearAll_ByNameAllVal.
 */
void
CResList_Destructor_ByNameAllVal(CResList *this)
{
	CResList_ClearAll_ByNameAllVal(this);
}

/*
 * 0x004239D0 - std::_Tree::clear (ByName/All vals)
 *
 * Walks the list from begin, erasing and freeing each node for the
 * ByName/All val variant.
 */
static void
CResList_ClearAll_ByNameAllVal(CResList *list)
{
	CResListNode *iter;

	iter = CResList_Begin(list);
	while (CResList_IsValid(list, iter)) {
		iter = (CResListNode *)CResList_EraseAndFree_ByNameAllVal(list, iter, 1);
	}
}

/*
 * 0x00423B40 - std::_Tree::erase and free (ByName/All vals)
 *
 * Unlinks the node via CResList_RemoveVal_ByNameAll and frees its data.
 */
void *
CResList_EraseAndFree_ByNameAllVal(CResList *list, CResListNode *node, int direction)
{
	void *outData;
	CResListNode *result;

	outData = NULL;
	result = (CResListNode *)CResList_RemoveVal_ByNameAll(list, (uintptr_t)node, &outData, (uint32_t)direction);
	if (outData != NULL)
		OperatorDelete(outData);
	return result;
}

/*
 * 0x00423E90 - CResList::RemoveVal (ByName/All vals variant)
 *
 * Unlinks valNode, hands its data to *outErased, frees the node, and returns
 * the neighbor chosen by direction.
 */
static void *
CResList_RemoveVal_ByNameAll(CResList *list, uintptr_t valNode, void **outErased, uint32_t direction)
{
	CResListNode *prev, *next;

	*outErased = NULL;
	if (valNode == 0)
		return NULL;

	prev = CResList_GetTail((CResList *)valNode);
	next = CResList_Begin((CResList *)valNode);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outErased = CResListNode_SwapData((CResListNode *)valNode, NULL);

	if ((CResListNode *)valNode != NULL)
		CResListNode_ScalarDelete((CResListNode *)valNode, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x00424320 - CResListNode::SetNext
 *
 * Stores next into node->next.
 */
void
CResListNode_SetNext(CResListNode *node, CResListNode *next)
{
	node->next = next;
}

/*
 * 0x0042F854 - EntityMap_SortAndValidate
 *
 * Sorts the entity range by location, then runs the Z validation pass
 * over it. Always returns 1; the validation result is stored to a local
 * and dropped.
 *
 * MODIFIED: both calls take their comparator byte from an uninitialised
 * stack slot - the binary never writes EBP-0x4 or EBP-0xc before
 * pushing them. The locals are zeroed here so the build stays free of
 * -Wuninitialized; the value is only a scratch accumulator whose result
 * this function discards, and nothing calls this.
 */
static __attribute__((unused)) int
EntityMap_SortAndValidate(void *container)
{
	char cmpSort = 0;
	char cmpValidate = 0;
	char unused;

	SortByLocation_Entry((uintptr_t *)CSearchCtx_GetBucket((CSearchCtx *)container), (uintptr_t *)(uintptr_t)StdList_GetSize((StdPtrList *)container), cmpSort);
	unused = EntityMap_ForEachValidateZ(
	        (uintptr_t *)CSearchCtx_GetBucket((CSearchCtx *)container), (uintptr_t *)(uintptr_t)StdList_GetSize((StdPtrList *)container), cmpValidate);
	USED(unused);
	return 1;
}

/*
 * 0x00430260 - CResManager::BeginIterWrapper
 *
 * Seeds output at the first non-empty bucket (forward direction).
 */
CSearchCtx *
CResManager_BeginIterWrapper(CResManager *rm, CSearchCtx *output)
{
	CResManager_FindNextBucket_ByNameAll(rm, output, 0, 1);
	return output;
}

/*
 * 0x00430280 - CResManager::NextIterWrapper
 *
 * Advances current forward via CResManager_NextIterErase_ByNameAll.
 */
CSearchCtx *
CResManager_NextIterWrapper(CResManager *rm, CSearchCtx *output, CSearchCtx *current)
{
	CResManager_NextIterErase_ByNameAll(rm, output, current, 1);
	return output;
}

/*
 * 0x004302B0 - std::sort entry wrapper (location variant)
 *
 * Delegates to SortByLocation_Main. Calls GameCentMon_GetPlayerCount for the
 * (ignored) recursion-depth argument that the binary's sort driver expects.
 */
static __attribute__((unused)) void
SortByLocation_Entry(uintptr_t *first, uintptr_t *last, char cmp)
{
	int depth;

	depth = GameCentMon_GetPlayerCount();
	USED(depth);
	SortByLocation_Main(first, last, cmp);
}

/*
 * 0x004302E0 - EntityMap_ForEachValidateZ
 *
 * Calls CEntityMap_ValidateEntityZ on each entity pointer in [first, end).
 */
char
EntityMap_ForEachValidateZ(uintptr_t *first, uintptr_t *end, char cmp)
{
	while (first != end) {
		CEntityMap_ValidateEntityZ((CEntityMap *)&cmp, (CItem *)*first);
		first++;
	}
	return cmp;
}

/*
 * 0x00430310 - CEntityMap::ValidateEntityZ
 *
 * Corrects an entity's Z against terrain. Skips contained entities. If terrain
 * has a valid Z, drops the entity there; otherwise sends players home and
 * places loose items into the world.
 */
void
CEntityMap_ValidateEntityZ(CEntityMap *this, CItem *entity)
{
	CLocation loc;
	int validZ;

	USED(this);

	if (((int (*)(CItem *))VT_FN(entity, VT_HAS_CONTAINER))(entity))
		return;

	CLocation_Init(&loc);
	validZ = 0;

	CLocation_SetLoc(&loc, CEntity_GetLocation(&entity->resourceEntity.entity));

	if (CTerrainManager_GetValidZAtEntity(entity, &validZ, ((uint32_t (*)(CItem *))VT_FN(entity, VT_GET_HEIGHT))(entity))) {
		loc.z = (int16_t)validZ;
		((void (*)(CItem *))VT_FN(entity, VT_HIDE))(entity);
		((void (*)(CItem *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, &loc);
	} else {
		if (((int (*)(CItem *))VT_FN(entity, VT_IS_PLAYER))(entity)) {
			CPlayer_ReturnToHome((CPlayer *)entity);
		} else {
			CItem_PlaceInWorld(entity, 1);
		}
	}
}

/*
 * 0x004303C0 - std::sort entry wrapper (serial variant)
 *
 * Serial-key counterpart of SortByLocation_Entry.
 */
void
SortBySerial_Entry(uintptr_t *first, uintptr_t *last, char cmp)
{
	int depth;

	depth = GameCentMon_GetPlayerCount();
	USED(depth);
	SortBySerial_Main(first, last, cmp);
}

/*
 * 0x004303F0 - EntityMap_ForEachValidateSerialZ
 *
 * Calls CEntityMap_ValidateSerialZ on each serial in [first, end).
 */
char
EntityMap_ForEachValidateSerialZ(uintptr_t *first, uintptr_t *end, char cmp)
{
	while (first != end) {
		CEntityMap_ValidateSerialZ((CEntityMap *)&cmp, (uint32_t)*first);
		first++;
	}
	return cmp;
}

/*
 * 0x00430420 - CEntityMap::ValidateSerialZ
 *
 * Resolves serial to an entity and validates its Z.
 */
void
CEntityMap_ValidateSerialZ(CEntityMap *this, uint32_t serial)
{
	CItem *entity;

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity != NULL)
		CEntityMap_ValidateEntityZ(this, entity);
}

/*
 * 0x00430460 - CResList InsertBefore+StoreData for ByName/All vals
 *
 * Inserts a new node before position and stores *valuePtr in it.
 */
void *
CResList_InsertAndStore_ByNameAll(CResList *list, CResListNode *position, uint32_t *valuePtr)
{
	CResListNode *newNode;

	newNode = (CResListNode *)CResList_InsertBefore_ByNameAll(list, position);
	if (newNode != NULL)
		CResListNode_SetDataInt(newNode, valuePtr);
	return newNode;
}

/*
 * 0x004304A0 - CResList AppendTail+StoreData for ByName/All vals
 *
 * Appends a tail node and stores *valuePtr in it.
 */
void *
CResList_AppendAndStore_ByNameAll(CResList *list, uint32_t *valuePtr)
{
	CResListNode *newNode;

	newNode = (CResListNode *)CResList_GetOrCreateTail_ByNameAll(list);
	if (newNode != NULL)
		CResListNode_SetDataInt(newNode, valuePtr);
	return newNode;
}

/*
 * 0x004304D0 - Find next non-empty bucket for ByName/All RM
 *
 * Same as CResManager_FindNextBucket_ByFile for the ByName/All RM.
 */
static void *
CResManager_FindNextBucket_ByNameAll(CResManager *rm, CSearchCtx *out, uint32_t bucket, uint32_t direction)
{
	CSearchCtx ctx;

	CSearchCtx_Constructor(&ctx);

	if (bucket >= 0x42) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	for (;;) {
		if (rm->keys[bucket] != NULL) {
			CSearchCtx_SetBucket(&ctx, bucket);
			CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));
			CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));
			CSearchCtx_SetEntity(&ctx, 1);
			CResManager_CreateBucket(out, &ctx);
			return out;
		}

		if (bucket == 0 && direction == 0) {
			CResManager_CreateBucket(out, &ctx);
			return out;
		}
		if (bucket == 0x41 && direction != 0) {
			CResManager_CreateBucket(out, &ctx);
			return out;
		}

		if (direction == 1)
			bucket += 1;
		else
			bucket -= 1;
	}
}

/*
 * 0x004305D0 - NextIter step for ByName/All RM erase path
 *
 * Same as CResManager_NextIterErase_ByFile but uses
 * CResManager_FindNextBucket_ByNameAll for next-bucket search.
 */
static void *
CResManager_NextIterErase_ByNameAll(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *nextKey;
	CResListNode *nextVal;
	int valid;
	uint32_t newBucket;

	CSearchCtx_Constructor(&ctx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	bucket = CSearchCtx_GetBucket(current);
	CSearchCtx_SetBucket(&ctx, bucket);

	nextKey = (CResListNode *)CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, (int)direction);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)nextKey);

	nextVal = (CResListNode *)CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), (int)direction);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)nextVal);

	valid = CResList_IsValid(rm->keys[bucket], nextKey);
	if (valid) {
		CSearchCtx_SetEntity(&ctx, 1);
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	newBucket = CSearchCtx_GetBucket(&ctx);
	if (newBucket == 0 && direction == 0)
		goto end;
	if (newBucket == 0x41 && direction != 0)
		goto end;

	if (direction == 1)
		newBucket++;
	else
		newBucket--;

	CResManager_FindNextBucket_ByNameAll(rm, out, newBucket, direction);
	return out;

end:
	CResManager_CreateBucket(out, &ctx);
	return out;
}

/*
 * 0x00430720 - CResList::InsertBefore (ByName/All val)
 *
 * Allocates a new node and splices it in before position, updating head/tail.
 */
static void *
CResList_InsertBefore_ByNameAll(CResList *list, CResListNode *position)
{
	CResListNode *prevNode;
	CResListNode *newNode;
	void *raw;

	if (position == NULL)
		return NULL;

	prevNode = CResList_GetTail((CResList *)position);

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	CResListNode_SetPrev(newNode, prevNode);
	CResListNode_SetNext(newNode, position);
	CResListNode_SetPrev(position, newNode);

	if (prevNode != NULL) {
		CResListNode_SetNext(prevNode, newNode);
	} else {
		list->head = newNode;
		if (list->tail == NULL)
			list->tail = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x00430810 - Get/create tail (ByName/All vals)
 *
 * Appends a new tail via CResList_InsertAfter_ByNameAll when the list is
 * non-empty, or allocates a fresh head/tail node otherwise.
 */
void *
CResList_GetOrCreateTail_ByNameAll(CResList *list)
{
	CResListNode *newNode;
	void *raw;

	if (list->tail != NULL) {
		return CResList_InsertAfter_ByNameAll(list, list->tail);
	}

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	list->tail = newNode;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x004308C0 - std::sort entry for CEntityMap location sort
 *
 * If count <= 16, runs insertion sort only. Otherwise quicksorts to
 * partitions of <= 16, runs insertion sort over the first 16, then does an
 * unguarded linear insert for the rest. The comparator is a 1-byte
 * std::less<> functor.
 */
static void
SortByLocation_Main(uintptr_t *first, uintptr_t *last, char cmp)
{
	int count;
	uintptr_t *cur;

	count = (int)(last - first);
	if (count <= 16) {
		SortByLocation_InsertionEntry(first, last, cmp);
		return;
	}
	SortByLocation_Quicksort(first, last, cmp, 0);
	SortByLocation_InsertionEntry(first, first + 16, cmp);
	cur = first + 16;
	while (cur != last) {
		SortByLocation_LinearInsert(cur, *cur, cmp);
		cur++;
	}
}

/*
 * 0x00430950 - std::sort entry for CEntityMap serial sort
 *
 * Same structure as location sort but uses serial comparator chain.
 */
static void
SortBySerial_Main(uintptr_t *first, uintptr_t *last, char cmp)
{
	int count;
	uintptr_t *cur;

	count = (int)(last - first);
	if (count <= 16) {
		SortBySerial_InsertionEntry(first, last, cmp);
		return;
	}
	SortBySerial_Quicksort(first, last, cmp, 0);
	SortBySerial_InsertionEntry(first, first + 16, cmp);
	cur = first + 16;
	while (cur != last) {
		SortBySerial_LinearInsert(cur, *cur, cmp);
		cur++;
	}
}

/*
 * 0x004309E0 - CResList::InsertAfter (ByName/All val)
 *
 * Allocates a new node and splices it in after position, updating head/tail.
 */
static void *
CResList_InsertAfter_ByNameAll(CResList *list, CResListNode *position)
{
	CResListNode *afterNode;
	CResListNode *newNode;
	void *raw;

	if (position == NULL)
		return NULL;

	afterNode = CResList_Begin((CResList *)position);

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	CResListNode_SetNext(newNode, afterNode);
	CResListNode_SetPrev(newNode, position);
	CResListNode_SetNext(position, newNode);

	if (afterNode != NULL) {
		CResListNode_SetPrev(afterNode, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x00430AD0 - std::sort insertion entry (location variant, 38 bytes)
 *
 * Calls GameCentMon_GetPlayerCount (returns 0, arg ignored),
 * then delegates to insertion sort with depth as 4th arg (unused).
 */
static void
SortByLocation_InsertionEntry(uintptr_t *first, uintptr_t *last, char cmp)
{
	int depth;

	depth = GameCentMon_GetPlayerCount();
	SortByLocation_Insertion(first, last, cmp, depth);
}

/*
 * 0x00430B00 - std::sort quicksort for CEntityMap location sort
 *
 * Recursive introsort. Partitions array, recurses on smaller half,
 * iterates on larger half. Uses median-of-3 pivot selection.
 * Stops when partition <= 16 elements (handled by insertion sort).
 * 4th arg (depth) is always 0 (GameCentMon_GetPlayerCount returns 0).
 */
static void
SortByLocation_Quicksort(uintptr_t *first, uintptr_t *last, char cmp, int depth)
{
	int count;
	uintptr_t pivot;
	uintptr_t *mid;
	int leftCount, rightCount;

	USED(depth);

	for (;;) {
		count = (int)(last - first);
		if (count <= 16)
			return;

		pivot = SortByLocation_Median3(*first, first[((int)(last - first)) / 2], *(last - 1), cmp);
		mid = SortByLocation_Partition(first, last, pivot, cmp);

		rightCount = (int)(last - mid);
		leftCount = (int)(mid - first);

		if (rightCount <= leftCount) {
			SortByLocation_Quicksort(mid, last, cmp, GameCentMon_GetPlayerCount());
			last = mid;
		} else {
			SortByLocation_Quicksort(first, mid, cmp, GameCentMon_GetPlayerCount());
			first = mid;
		}
	}
}

/*
 * 0x00430BD0 - std::sort unguarded linear insert (location variant, 78 bytes)
 *
 * Inserts value at correct position by shifting elements right.
 * Walks backward from pos, comparing value against each element
 * using CompareLocation. No bounds check (sentinel guaranteed).
 */
static void
SortByLocation_LinearInsert(uintptr_t *pos, uintptr_t value, char cmp)
{
	uintptr_t *cursor;

	cursor = pos;
	for (;;) {
		cursor--;
		if (!CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)value, (CItem *)*cursor))
			break;
		*pos = *cursor;
		pos = cursor;
	}
	*pos = value;
}

/*
 * 0x00430C20 - CEntityMap::CompareLocation
 *
 * Returns 1 iff a sorts before b by (x, y, z) location.
 */
int
CEntityMap_CompareLocation(CEntityMap *this, CItem *a, CItem *b)
{
	int xA, xB, yA, yB, zA, zB;

	USED(this);

	xA = (int)CEntity_GetLocation(&a->resourceEntity.entity)->x;
	xB = (int)CEntity_GetLocation(&b->resourceEntity.entity)->x;
	if (xA != xB) {
		xA = (int)CEntity_GetLocation(&a->resourceEntity.entity)->x;
		xB = (int)CEntity_GetLocation(&b->resourceEntity.entity)->x;
		return xA < xB;
	}

	yA = (int)CEntity_GetLocation(&a->resourceEntity.entity)->y;
	yB = (int)CEntity_GetLocation(&b->resourceEntity.entity)->y;
	if (yA != yB) {
		yA = (int)CEntity_GetLocation(&a->resourceEntity.entity)->y;
		yB = (int)CEntity_GetLocation(&b->resourceEntity.entity)->y;
		return yA < yB;
	}

	zA = (int)CEntity_GetLocation(&a->resourceEntity.entity)->z;
	zB = (int)CEntity_GetLocation(&b->resourceEntity.entity)->z;
	return zA < zB;
}

/*
 * 0x00430CD0 - std::sort insertion entry (serial variant, 38 bytes)
 *
 * Calls GameCentMon_GetPlayerCount (returns 0, arg ignored),
 * then delegates to insertion sort with depth as 4th arg (unused).
 */
static void
SortBySerial_InsertionEntry(uintptr_t *first, uintptr_t *last, char cmp)
{
	int depth;

	depth = GameCentMon_GetPlayerCount();
	SortBySerial_Insertion(first, last, cmp, depth);
}

/*
 * 0x00430D00 - std::sort quicksort for CEntityMap serial sort
 *
 * Same structure as location quicksort but uses serial comparator chain.
 */
static void
SortBySerial_Quicksort(uintptr_t *first, uintptr_t *last, char cmp, int depth)
{
	int count;
	uintptr_t pivot;
	uintptr_t *mid;
	int leftCount, rightCount;

	USED(depth);

	for (;;) {
		count = (int)(last - first);
		if (count <= 16)
			return;

		pivot = SortBySerial_Median3(*first, first[((int)(last - first)) / 2], *(last - 1), cmp);
		mid = SortBySerial_Partition(first, last, pivot, cmp);

		rightCount = (int)(last - mid);
		leftCount = (int)(mid - first);

		if (rightCount <= leftCount) {
			SortBySerial_Quicksort(mid, last, cmp, GameCentMon_GetPlayerCount());
			last = mid;
		} else {
			SortBySerial_Quicksort(first, mid, cmp, GameCentMon_GetPlayerCount());
			first = mid;
		}
	}
}

/*
 * 0x00430DD0 - std::sort unguarded linear insert (serial variant, 78 bytes)
 *
 * Same as location variant but uses CompareBySerial comparator.
 */
static void
SortBySerial_LinearInsert(uintptr_t *pos, uintptr_t value, char cmp)
{
	uintptr_t *cursor;

	cursor = pos;
	for (;;) {
		cursor--;
		if (!CEntityMap_CompareBySerial((CEntityMap *)&cmp, value, *cursor))
			break;
		*pos = *cursor;
		pos = cursor;
	}
	*pos = value;
}

/*
 * 0x00430E20 - CEntityMap::CompareBySerial
 *
 * Resolves both serials in g_World and delegates to CompareLocation. Returns
 * 0 if either serial no longer exists.
 */
int
CEntityMap_CompareBySerial(CEntityMap *this, uint32_t serialA, uint32_t serialB)
{
	CItem *a, *b;

	a = CWorld_FindBySerial(g_World, serialA);
	b = CWorld_FindBySerial(g_World, serialB);
	if (a == NULL || b == NULL)
		return 0;
	return CEntityMap_CompareLocation(this, a, b);
}

/*
 * 0x00430E80 - std::sort insertion sort (location variant)
 *
 * Insertion sort of a CItem* range using CompareLocation.
 */
static void
SortByLocation_Insertion(uintptr_t *first, uintptr_t *last, char cmp, int depth)
{
	uintptr_t *cursor;
	uintptr_t saved;

	USED(depth);

	if (first == last)
		return;
	cursor = first;
	for (;;) {
		cursor++;
		if (cursor == last)
			return;
		saved = *cursor;
		if (!(CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)saved, (CItem *)*first) & 0xFF)) {
			SortByLocation_LinearInsert(cursor, saved, cmp);
		} else {
			vector_CopyBackward(first, cursor, cursor + 1);
			*first = saved;
		}
	}
}

/*
 * 0x00430F10 - std::sort median-of-3 for XY location sort
 *
 * Selects the median of three values using the CompareLocation comparator
 * (standard introsort pivot).
 */
static uintptr_t
SortByLocation_Median3(uintptr_t a, uintptr_t b, uintptr_t c, char cmp)
{
	if (CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)a, (CItem *)b)) {
		if (CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)b, (CItem *)c))
			return b;
		if (CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)a, (CItem *)c))
			return c;
		return a;
	}
	if (CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)a, (CItem *)c))
		return a;
	if (CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)b, (CItem *)c))
		return c;
	return b;
}

/*
 * 0x00430FE0 - std::sort partition (location variant, 125 bytes)
 *
 * Hoare partition. Advances left while *left < pivot, decrements right
 * while pivot < *right. Swaps when both stop, returns partition point.
 */
static uintptr_t *
SortByLocation_Partition(uintptr_t *left, uintptr_t *right, uintptr_t pivot, char cmp)
{
	for (;;) {
		while (CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)*left, (CItem *)pivot))
			left++;
		do {
			right--;
		} while (CEntityMap_CompareLocation((CEntityMap *)&cmp, (CItem *)pivot, (CItem *)*right));
		if (right <= left)
			return left;
		vector_SwapWrapper(left, right);
		left++;
	}
}

/*
 * 0x00431060 - std::sort insertion sort (serial variant, 131 bytes)
 *
 * Same as location variant but uses CompareBySerial comparator.
 */
static void
SortBySerial_Insertion(uintptr_t *first, uintptr_t *last, char cmp, int depth)
{
	uintptr_t *cursor;
	uintptr_t saved;

	USED(depth);

	if (first == last)
		return;
	cursor = first;
	for (;;) {
		cursor++;
		if (cursor == last)
			return;
		saved = *cursor;
		if (!(CEntityMap_CompareBySerial((CEntityMap *)&cmp, saved, *first) & 0xFF)) {
			SortBySerial_LinearInsert(cursor, saved, cmp);
		} else {
			vector_CopyBackward(first, cursor, cursor + 1);
			*first = saved;
		}
	}
}

/*
 * 0x004310F0 - std::sort median-of-3 for serial sort
 *
 * Same as location variant but uses CompareBySerial comparator.
 */
static uintptr_t
SortBySerial_Median3(uintptr_t a, uintptr_t b, uintptr_t c, char cmp)
{
	if (CEntityMap_CompareBySerial((CEntityMap *)&cmp, a, b)) {
		if (CEntityMap_CompareBySerial((CEntityMap *)&cmp, b, c))
			return b;
		if (CEntityMap_CompareBySerial((CEntityMap *)&cmp, a, c))
			return c;
		return a;
	}
	if (CEntityMap_CompareBySerial((CEntityMap *)&cmp, a, c))
		return a;
	if (CEntityMap_CompareBySerial((CEntityMap *)&cmp, b, c))
		return c;
	return b;
}

/*
 * 0x004311C0 - std::sort partition (serial variant, 125 bytes)
 *
 * Same as location variant but uses CompareBySerial comparator.
 */
static uintptr_t *
SortBySerial_Partition(uintptr_t *left, uintptr_t *right, uintptr_t pivot, char cmp)
{
	for (;;) {
		while (CEntityMap_CompareBySerial((CEntityMap *)&cmp, *left, pivot))
			left++;
		do {
			right--;
		} while (CEntityMap_CompareBySerial((CEntityMap *)&cmp, pivot, *right));
		if (right <= left)
			return left;
		vector_SwapWrapper(left, right);
		left++;
	}
}

/*
 * 0x0043D580 - CStringList::ScalarDelete (items variant, 46 bytes)
 *
 * Destroys the list and frees it when flags&1.
 */
CStringList *
CStringList_ScalarDelete(CStringList *sl, int flags)
{
	CStringList_Destroy(sl);
	if (flags & 1)
		free(sl);
	return NULL;
}

/*
 * 0x0043D5B0 - CResList::~CResList (scalar deleting destructor) (key variant, 46 bytes)
 *
 * Destroys the key-variant list and frees it when flags&1.
 */
CResList *
CResList_ScalarDeleteA(CResList *list, int flags)
{
	CResList_DestructorA(list);
	if (flags & 1)
		free(list);
	return NULL;
}

/*
 * 0x0043D5E0 - CStringList::ScalarDelete (effects variant, 46 bytes)
 *
 * Destroys the effects list and frees it when flags&1.
 */
CStringList *
CStringList_ScalarDeleteEffects(CStringList *sl, int flags)
{
	CStringList_DestructorEffects(sl);
	if (flags & 1)
		free(sl);
	return NULL;
}

/*
 * 0x0043D610 - CStringList::HasEntries
 *
 * Returns non-zero if the list has any entries.
 */
int
CStringList_HasEntries(CStringList *sl)
{
	return sl->list.head != NULL;
}

/*
 * 0x0043D630 - CStringList::CStringList
 *
 * Zeroes the CResList base and totalWeight.
 */
void
CStringList_Init(CStringList *sl)
{
	CResListNode_Constructor_bin((CResListNode *)&sl->list);
	sl->totalWeight = 0;
}

/*
 * 0x0043D650 - CStringList::~CStringList
 *
 * Delegates to CResList_EraseAllSLN.
 */
void
CStringList_Destroy(CStringList *sl)
{
	CResList_EraseAllSLN(&sl->list);
}

/*
 * 0x0043D670 - CStringList::BeginIter
 *
 * Writes the list's head node into *output.
 */
void *
CStringList_BeginIter(CStringList *sl, CResListNode **output)
{
	CResListNode *begin;
	int flag;

	flag = 0;
	begin = CResList_Begin(&sl->list);
	CIterCtx_Set(output, begin);
	flag |= 1;
	USED(flag);
	return output;
}

/*
 * 0x0043D6B0 - CStringList::AdvanceIter
 *
 * Advances *iterNode via CResList_Next and writes the result into *outNode.
 * Dead code: an unused local flag is set/ORed.
 */
void *
CStringList_AdvanceIter(CStringList *sl, CResListNode **outNode, CResListNode **iterNode)
{
	CResListNode *next;
	int flag;

	flag = 0;
	next = CResList_Next(&sl->list, *iterNode);
	CIterCtx_Set(outNode, next);
	flag |= 1;
	USED(flag);
	return outNode;
}

/*
 * 0x0043D6F0 - CStringList::GetNodeData
 *
 * Returns the entry stored at *nodePtr.
 */
void *
CStringList_GetNodeData(CStringList *sl, CResListNode **nodePtr)
{
	return CResList_GetData(&sl->list, *nodePtr);
}

/*
 * 0x0043D710 - CStringList::AddWeighted
 *
 * Inserts a new {str, weight} entry at the chosen direction and bumps
 * totalWeight when the list is non-empty. The inserted position is copied
 * into *output.
 */
void *
CStringList_AddWeighted(CStringList *sl, CResListNode **output, CString *str, int weight, int direction)
{
	CStringListNode tempNode;
	CStringListNode *constructed;
	void *srcData;
	CResListNode *node;
	CResListNode *pos;
	int flag;

	flag = 0;
	constructed = CStringListEntry_Constructor(&tempNode, str, weight);
	srcData = constructed;
	node = CStringList_InsertEntry(sl, srcData, direction);
	CIterCtx_Set(&pos, node);

	CStringListNode_Destructor(&tempNode);

	if (CStringList_HasEntries((CStringList *)&pos))
		sl->totalWeight += weight;

	CResBankMagicCtx_Copy(output, &pos);
	flag |= 1;
	CResBankMagicCtx_PostInit(&pos);
	return output;

	USED(flag);
}

/*
 * 0x0043D7D0 - CStringList::GetType (include variant, 28 bytes)
 *
 * Returns the entry's include-flag byte.
 */
int
CStringList_GetTypeInclude(CStringList *sl, CResListNode **nodePtr)
{
	CStringListNode *node = (CStringListNode *)CResList_GetData(&sl->list, *nodePtr);
	return node->type;
}

/*
 * 0x0043D7F0 - CStringList::SetType (include variant)
 *
 * Flips an entry's type; when the type changes, adds or subtracts its
 * weight from totalWeight accordingly.
 */
void
CStringList_SetTypeInclude(CStringList *sl, CResListNode **nodePtr, int newType)
{
	int oldType = ((CStringListNode *)CResList_GetData(&sl->list, *nodePtr))->type;

	if (newType != oldType) {
		if (newType == 1)
			sl->totalWeight += *(int *)CStringList_GetValueEffects(sl, nodePtr);
		else
			sl->totalWeight -= *(int *)CStringList_GetValueEffects(sl, nodePtr);
	}
	((CStringListNode *)CResList_GetData(&sl->list, *nodePtr))->type = newType;
}

/*
 * 0x0043D870 - CStringList::SetAllTypes (include variant)
 *
 * Flips every entry's type, updating totalWeight as each entry gains or
 * loses the include (type==1) flag.
 */
void
CStringList_SetAllTypesInclude(CStringList *sl, int newType)
{
	CResListNode *pos;
	int oldType;

	pos = CResList_Begin(&sl->list);
	while (CResList_IsValid(&sl->list, pos)) {
		CStringListNode *node = (CStringListNode *)CResList_GetData(&sl->list, pos);
		oldType = node->type;
		if (oldType != newType) {
			if (newType == 1)
				sl->totalWeight += node->weight;
			else
				sl->totalWeight -= node->weight;
		}
		node->type = newType;
		pos = CResList_Next(&sl->list, pos);
	}
}

/*
 * 0x0043D920 - CStringList::GetWeightedRandom
 *
 * Picks a type==1 node by weight-proportional roulette over totalWeight
 * and stores it in *output. If totalWeight <= 0 (or the scan ends without
 * a hit), *output is left pointing at the null default context.
 */
void *
CStringList_GetWeightedRandom(CStringList *sl, void *output)
{
	CResListNode *localCtx = NULL;
	int cumWeight = 0;
	int pick;
	CResListNode *pos;

	CResBankMagicCtx_DefaultConstructor(&localCtx);

	if (sl->totalWeight <= 0) {
		CResBankMagicCtx_Copy(output, &localCtx);
		CResBankMagicCtx_PostInit(&localCtx);
		return output;
	}

	pick = CRandom() % sl->totalWeight + 1;
	pos = CResList_Begin(&sl->list);
	while (CResList_IsValid(&sl->list, pos)) {
		CStringListNode *node = (CStringListNode *)CResList_GetData(&sl->list, pos);
		if (node->type == 1) {
			node = (CStringListNode *)CResList_GetData(&sl->list, pos);
			if (node->weight != 0) {
				node = (CStringListNode *)CResList_GetData(&sl->list, pos);
				cumWeight += node->weight;
				if (pick <= cumWeight) {
					localCtx = pos;
					CResBankMagicCtx_Copy(output, &localCtx);
					CResBankMagicCtx_PostInit(&localCtx);
					return output;
				}
			}
		}
		pos = CResList_Next(&sl->list, pos);
	}

	CResBankMagicCtx_Copy(output, &localCtx);
	CResBankMagicCtx_PostInit(&localCtx);
	return output;
}

/*
 * 0x0043DA80 - CResList::ClearInternal
 *
 * Frees every node and its CString payload.
 */
void
CResList_ClearInternal(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_FreeNodeA(list, node, 1);
	}
}

/*
 * 0x0043DAC0 - CResList::Clear (wrapper)
 *
 * Public entry point that delegates to CResList_ClearInternal.
 */
void
CResList_DestructorA(CResList *list)
{
	CResList_ClearInternal(list);
}

/*
 * 0x0043DAE0 - CResList::InsertTailStr
 *
 * Appends a new tail node holding a copy of name.
 */
CResListNode *
CResList_InsertTailStr(CResList *list, CString *name)
{
	CResListNode *node;

	node = CResList_AllocTailNodeA(list);
	if (node != NULL)
		CResListNode_SetString(node, name);
	return node;
}

/*
 * 0x0043DB10 - CResList::AddResultEntry
 *
 * Appends name via InsertAfterTailA when flag==1, else InsertTailStr.
 */
void
CResList_AddResultEntry(CResList *list, CString *name, int flag)
{
	if (flag == 1)
		CResList_InsertAfterTailA(list, name);
	else
		CResList_InsertTailStr(list, name);
}

/*
 * 0x0043DB40 - CResList: insert CString copy into sorted list
 *
 * Allocates a heap CString copy of str and inserts it via SortedInsert.
 */
void
CResList_InsertStrCopy(CResList *list, CString *str, int direction, int orderedFlag)
{
	CString *copy;

	copy = (CString *)malloc(sizeof(CString));
	if (copy != NULL) {
		CString_CopyConstructor(copy, str);
	}

	CResList_SortedInsert(list, copy, direction, orderedFlag);
}

/*
 * 0x0043DBD0 - CResList: find CString in sorted list
 *
 * Walks from startNode (or DirectionBegin if NULL) and returns the node
 * whose data equals key, exiting early past the key's sorted position.
 */
CResListNode *
CResList_FindByStrSorted(CResList *list, CString *key, CResListNode *startNode, int direction, int orderedFlag)
{
	CResListNode *node;
	int cmpResult;

	if (startNode == NULL)
		node = CResList_DirectionBegin(list, direction);
	else
		node = startNode;

	while (CResList_IsValid(list, node)) {
		if (CString_EqualCString2((CString *)CResList_GetData(list, node), key))
			return node;

		if (orderedFlag == 1)
			cmpResult = CString_LessThan(key, (CString *)CResList_GetData(list, node));
		else
			cmpResult = CString_GreaterThan(key, (CString *)CResList_GetData(list, node));

		if (cmpResult == 1)
			return NULL;

		node = CResList_DirectionNext(list, node, direction);
	}

	return NULL;
}

/*
 * 0x0043DC80 - CResList::InsertTailDataB
 *
 * Tail-allocates a node in the magic effects list and copies name into it.
 */
void
CResList_InsertTailDataB(CResList *magicList, CMagicItemListNode *name)
{
	CResListNode *node;

	node = CResList_AllocTailNodeB(magicList);
	if (node != NULL)
		CResListNode_InsertDataB(node, name);
}

/*
 * 0x0043DCB0 - CResBook scalar dtor wrapper
 *
 * Thiscall: calls CResManager_ClearI to destroy the book CResManager.
 */
void
CResBook_ScalarDtor(CResManager *this)
{
	CResManager_ClearI(this);
}

/*
 * 0x0043DCD0 - CResManager::Clear (include variant)
 *
 * Frees every key/val list across 66 buckets and resets count.
 */
void
CResManager_ClearI(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDeleteA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDeleteI(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x0043DDB0 - CResManager::FindOrInsert (include variant)
 *
 * Hashes key and ensures a bucket exists. If flags==1 and key already present,
 * returns an empty ctx; otherwise inserts new key and tail-allocates a value
 * node via CResList_AllocTailNodeI.
 */
CSearchCtx *
CResManager_FindOrInsertInclude(CResManager *rm, CSearchCtx *output, CString *key)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *keyNode, *valNode;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashStrA(key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		CResListNode *found = CResList_FindByString(rm->keys[bucket], key, NULL, 1);
		if (found != NULL) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
	}

	keyNode = CResList_InsertTailStr(rm->keys[bucket], key);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)keyNode);

	valNode = CResList_AllocTailNodeI(rm->vals[bucket]);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)valNode);

	CSearchCtx_SetBucket(&ctx, bucket);
	CSearchCtx_SetEntity(&ctx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0043DF40 - CResManager::FindByStrCtx (include variant)
 *
 * Seeds a CSearchCtx for key and delegates to CResManager_SearchBucketA.
 * Empty bucket returns an empty ctx.
 */
void *
CResManager_FindByStrCtxA(CResManager *rm, CSearchCtx *output, const char *key, int direction)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashStrA((CString *)key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	localCtx.entity = 1;
	localCtx.bucket = bucket;
	localCtx.keyNode = (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction);
	localCtx.valNode = (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction);

	CResManager_SearchBucketA(rm, output, key, &localCtx, direction);
	return output;
}

/*
 * 0x0043E000 - CResManager::InsertValueAtCtxI (include variant, 63 bytes)
 *
 * Stores value on the val node pointed to by ctx.
 */
void
CResManager_InsertValueAtCtxI(CResManager *rm, CSearchCtx *ctx, void *value)
{
	uint32_t bucket;

	if (!CSearchCtx_Find(ctx))
		return;

	bucket = CSearchCtx_GetBucket(ctx);
	CResList_SetValueOnNodeI(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(ctx), value);
}

/*
 * 0x0043E040 - CResManager::BeginIter (include variant, 32 bytes)
 *
 * Delegates to BeginIterInternalA with forward direction.
 */
void
CResManager_BeginIterA(CResManager *rm, CSearchCtx *output)
{
	CResManager_BeginIterInternalA(rm, output, 0, 1);
}

/*
 * 0x0043E060 - CResManager::NextIter (include variant, 34 bytes)
 *
 * Delegates to NextIterInternalA with forward direction.
 */
void
CResManager_NextIterA(CResManager *rm, CSearchCtx *output, CSearchCtx *current)
{
	CResManager_NextIterInternalA(rm, output, current, 1);
}

/*
 * 0x0043E090 - CResManager::FindEntry (include variant, 42 bytes)
 *
 * Looks up name and returns the stored value, or NULL.
 */
void *
CResManager_FindEntryA(CResManager *rm, CString *name)
{
	CSearchCtx localCtx;
	CResManager_FindByStrCtxA(rm, &localCtx, (const char *)name, 1);
	return CResManager_GetResult(rm, &localCtx);
}

/*
 * 0x0043E0C0 - CResManager::CResManager
 *
 * Stores flags, zeroes count, and initializes the bucket tables.
 */
void
CResManager_Constructor(CResManager *rm, int flags)
{
	rm->flags = flags;
	rm->count = 0;
	CResManager_InitTables(rm);
}

/*
 * 0x0043E0F0 - CResManagerS scalar dtor
 *
 * Thiscall: calls CResManager_ClearS to destroy the keys-variant CResManager.
 */
void
CResManagerS_ScalarDtor(CResManager *this)
{
	CResManager_ClearS(this);
}

/*
 * 0x0043E110 - CResManager::ClearS (keys variant, 213 bytes)
 *
 * Frees every key/val list across 66 buckets (val uses ScalarDeleteS)
 * and resets count.
 */
void
CResManager_ClearS(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDeleteA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDeleteS(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x0043E1F0 - CResManager::FindOrInsert (second variant, 394 bytes)
 *
 * Returns a ctx for key in the keys-variant table, inserting a new
 * key+value pair when absent (value list uses AllocTailNodeS).
 */
CSearchCtx *
CResManager_FindOrInsertSecond(CResManager *rm, CSearchCtx *output, CString *key)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *keyNode, *valNode;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashStrA(key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		CResListNode *found = CResList_FindByString(rm->keys[bucket], key, NULL, 1);
		if (found != NULL) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
	}

	keyNode = CResList_InsertTailStr(rm->keys[bucket], key);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)keyNode);

	valNode = CResList_AllocTailNodeS(rm->vals[bucket]);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)valNode);

	CSearchCtx_SetBucket(&ctx, bucket);
	CSearchCtx_SetEntity(&ctx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0043E380 - CResManager::FindByStrCtx (keys variant)
 *
 * COMDAT copy of include variant (0x0043DF40) but calls SearchBucketD.
 * Used by CMagicItemFactory_FindNameInKeys for keysRM (+0x230) lookups.
 */
void *
CResManager_FindByStrCtxD(CResManager *rm, CSearchCtx *output, const char *key, int direction)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashStrA((CString *)key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	localCtx.entity = 1;
	localCtx.bucket = bucket;
	localCtx.keyNode = (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction);
	localCtx.valNode = (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction);

	CResManager_SearchBucketD(rm, output, key, &localCtx, direction);
	return output;
}

/*
 * 0x0043E440 - CResManager::InsertValueAtCtxS (second variant, 63 bytes)
 *
 * Stores value on the val node pointed to by ctx (keys variant).
 */
void
CResManager_InsertValueAtCtxS(CResManager *rm, CSearchCtx *ctx, void *value)
{
	uint32_t bucket;

	if (!CSearchCtx_Find(ctx))
		return;

	bucket = CSearchCtx_GetBucket(ctx);
	CResList_SetValueOnNodeS(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(ctx), value);
}

/*
 * 0x0043E480 - CResManagerC scalar dtor
 *
 * Thiscall: calls CResManager_ClearC to destroy the props-variant CResManager.
 */
void
CResManagerC_ScalarDtor(CResManager *this)
{
	CResManager_ClearC(this);
}

/*
 * 0x0043E4A0 - CResManager::ClearC (props variant, 213 bytes)
 *
 * Frees every key/val list across 66 buckets (val uses ScalarDeleteC)
 * and resets count.
 */
void
CResManager_ClearC(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDeleteA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDeleteC(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x0043E580 - CResManager::FindOrInsertC
 *
 * Returns a ctx for key in the props-variant table, inserting a new
 * key+value pair when absent.
 */
CSearchCtx *
CResManager_FindOrInsertC(CResManager *rm, CSearchCtx *output, CString *key)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *keyNode, *valNode;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashStrA(key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		CResListNode *found = CResList_FindByString(rm->keys[bucket], key, NULL, 1);
		if (found != NULL) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
	}

	keyNode = CResList_InsertTailStr(rm->keys[bucket], key);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)keyNode);

	valNode = CResList_AllocTailNodeC(rm->vals[bucket]);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)valNode);

	CSearchCtx_SetBucket(&ctx, bucket);
	CSearchCtx_SetEntity(&ctx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0043E710 - CResManager::FindByStrCtx (props variant)
 *
 * Seeds a ctx for key and delegates to SearchBucketC (props variant).
 */
void *
CResManager_FindByStrCtxC(CResManager *rm, CSearchCtx *output, const char *key, int direction)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashStrA((CString *)key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	localCtx.entity = 1;
	localCtx.bucket = bucket;
	localCtx.keyNode = (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction);
	localCtx.valNode = (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction);

	CResManager_SearchBucketC(rm, output, key, &localCtx, direction);
	return output;
}

/*
 * 0x0043E7D0 - CResManager::InsertValueAtCtxC
 *
 * Stores value on the val node pointed to by ctx (props variant).
 */
void
CResManager_InsertValueAtCtxC(CResManager *rm, CSearchCtx *ctx, void *value)
{
	uint32_t bucket;

	if (!CSearchCtx_Find(ctx))
		return;

	bucket = CSearchCtx_GetBucket(ctx);
	CResList_SetValueOnNodeC(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(ctx), value);
}

/*
 * 0x0043E890 - CResManagerT scalar dtor
 *
 * Thiscall: calls CResManager_ClearT to destroy the effectTbl-variant CResManager.
 */
void
CResManagerT_ScalarDtor(CResManager *this)
{
	CResManager_ClearT(this);
}

/*
 * 0x0043E8D0 - CResManager::ClearT (effectTbl variant, 213 bytes)
 *
 * Frees every key/val list across 66 buckets (val uses ScalarDeleteT)
 * and resets count.
 */
void
CResManager_ClearT(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDeleteA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDeleteT(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x0043EA30 - CResManager::FindOrInsert (effects variant, 394 bytes)
 *
 * Returns a ctx for key in the effects-variant table, inserting a new
 * key+value pair when absent (value list uses AllocTailNodeE).
 */
CSearchCtx *
CResManager_FindOrInsertEffects(CResManager *rm, CSearchCtx *output, CString *key)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *keyNode, *valNode;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashStrA(key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		CResListNode *found = CResList_FindByString(rm->keys[bucket], key, NULL, 1);
		if (found != NULL) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
	}

	keyNode = CResList_InsertTailStr(rm->keys[bucket], key);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)keyNode);

	valNode = CResList_AllocTailNodeE(rm->vals[bucket]);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)valNode);

	CSearchCtx_SetBucket(&ctx, bucket);
	CSearchCtx_SetEntity(&ctx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0043EC00 - CResManager::FindByStrCtx (magic variant)
 *
 * Seeds a ctx for key and delegates to SearchBucketB (magic variant).
 */
void *
CResManager_FindByStrCtxB(CResManager *rm, CSearchCtx *output, const char *key, int direction)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashStrA((CString *)key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	localCtx.entity = 1;
	localCtx.bucket = bucket;
	localCtx.keyNode = (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction);
	localCtx.valNode = (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction);

	CResManager_SearchBucketB(rm, output, key, &localCtx, direction);
	return output;
}

/*
 * 0x0043ECC0 - CResManager::InsertValueAtCtx (effects variant, 63 bytes)
 *
 * Stores value on the val node pointed to by ctx (effects variant).
 */
void
CResManager_InsertValueAtCtxEffects(CResManager *rm, CSearchCtx *ctx, void *value)
{
	uint32_t bucket;

	if (!CSearchCtx_Find(ctx))
		return;

	bucket = CSearchCtx_GetBucket(ctx);
	CResList_SetValueOnNodeEffects(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(ctx), value);
}

/*
 * 0x0043ED00 - CResManager::BeginIter (magic variant, 32 bytes)
 *
 * Delegates to BeginIterInternalB with forward direction.
 */
void
CResManager_BeginIterB(CResManager *rm, CSearchCtx *output)
{
	CResManager_BeginIterInternalB(rm, output, 0, 1);
}

/*
 * 0x0043ED20 - CResManager::NextIter (magic variant, 34 bytes)
 *
 * Delegates to NextIterInternalB with forward direction.
 */
void
CResManager_NextIterB(CResManager *rm, CSearchCtx *output, CSearchCtx *current)
{
	CResManager_NextIterInternalB(rm, output, current, 1);
}

/*
 * 0x0043ED50 - CSearchCtx::CSearchCtx
 *
 * Zeroes all 4 fields of the 16-byte CSearchCtx struct.
 */
void
CSearchCtx_Constructor(CSearchCtx *ctx)
{
	memset(ctx, 0, sizeof(CSearchCtx));
}

/*
 * 0x0043ED90 - CSearchCtx::Add
 *
 * Copies all four ctx fields from src to dst.
 */
void
CSearchCtx_Add(CSearchCtx *dst, CSearchCtx *src)
{
	dst->entity = src->entity;
	dst->bucket = src->bucket;
	dst->keyNode = src->keyNode;
	dst->valNode = src->valNode;
}

/*
 * 0x0043EDF0 - CStringList::~CStringList (effects variant, 19 bytes)
 *
 * Destroys the effects list by freeing every CEffectTableEntry node.
 */
static void
CStringList_DestructorEffects(CStringList *sl)
{
	CResList_DestructorE(&sl->list);
}

/*
 * 0x0043EE10 - CStringList::SetWeight (effects variant, 67 bytes)
 *
 * Updates the entry's weight and adjusts the list's totalWeight by the
 * difference.
 */
void
CStringList_SetWeight(CStringList *sl, CResListNode **nodePtr, int weight)
{
	CEffectTableEntry *entry;
	int oldWeight;

	entry = (CEffectTableEntry *)CResList_GetData(&sl->list, *nodePtr);
	oldWeight = entry->weight;
	sl->totalWeight += weight - oldWeight;
	entry = (CEffectTableEntry *)CResList_GetData(&sl->list, *nodePtr);
	entry->weight = weight;
}

/*
 * 0x0043EE60 - CStringList::GetType (effects variant, 30 bytes)
 *
 * Returns the entry's direction/type field.
 */
int
CStringList_GetTypeEffects(CStringList *sl, CResListNode **nodePtr)
{
	CEffectTableEntry *entry = (CEffectTableEntry *)CResList_GetData(&sl->list, *nodePtr);
	return entry->direction;
}

/*
 * 0x0043EE80 - CStringList::SetType (effects variant, 123 bytes)
 *
 * Flips an effects entry's type, updating totalWeight as the include
 * flag (type==1) is gained or lost.
 */
void
CStringList_SetTypeEffects(CStringList *sl, CResListNode **nodePtr, int newType)
{
	CEffectTableEntry *entry;
	int oldType;

	entry = (CEffectTableEntry *)CResList_GetData(&sl->list, *nodePtr);
	oldType = entry->direction;
	if (newType != oldType) {
		if (newType == 1) {
			int *wp = CStringList_GetWeightEffects(sl, nodePtr);
			sl->totalWeight += *wp;
		} else {
			int *wp = CStringList_GetWeightEffects(sl, nodePtr);
			sl->totalWeight -= *wp;
		}
	}
	entry = (CEffectTableEntry *)CResList_GetData(&sl->list, *nodePtr);
	entry->direction = newType;
}

/*
 * 0x0043EF00 - CStringList::SetAllTypes (effects variant, 168 bytes)
 *
 * Flips every effects entry's type, updating totalWeight as each entry
 * gains or loses the include (type==1) flag.
 */
void
CStringList_SetAllTypesEffects(CStringList *sl, int newType)
{
	CResListNode *pos;
	int oldType;

	pos = CResList_Begin(&sl->list);
	while (CResList_IsValid(&sl->list, pos)) {
		CEffectTableEntry *entry = (CEffectTableEntry *)CResList_GetData(&sl->list, pos);
		oldType = entry->direction;
		if (oldType != newType) {
			if (newType == 1) {
				entry = (CEffectTableEntry *)CResList_GetData(&sl->list, pos);
				sl->totalWeight += entry->weight;
			} else {
				entry = (CEffectTableEntry *)CResList_GetData(&sl->list, pos);
				sl->totalWeight -= entry->weight;
			}
		}
		entry = (CEffectTableEntry *)CResList_GetData(&sl->list, pos);
		entry->direction = newType;
		pos = CResList_Next(&sl->list, pos);
	}
}

/*
 * 0x0043EFB0 - CStringList::GetWeightedRandomEntry (effects, 341 bytes)
 *
 * Picks a type==1 effects entry by weight-proportional roulette; stores
 * the picked node (or a null ctx) into *output.
 */
void *
CStringList_GetWeightedRandomEntryEffects(CStringList *sl, void *output)
{
	int localCtxValue = 0;
	CResListNode *localCtx = NULL;
	int cumWeight = 0;
	int pick;
	CResListNode *pos;

	CResBankMagicCtx_DefaultConstructor(&localCtx);

	if (sl->totalWeight <= 0) {
		CResBankMagicCtx_Copy(output, &localCtx);
		CResBankMagicCtx_PostInit(&localCtx);
		return output;
	}

	pick = CRandom() % sl->totalWeight + 1;
	pos = CResList_Begin(&sl->list);
	while (CResList_IsValid(&sl->list, pos)) {
		CEffectTableEntry *entry = (CEffectTableEntry *)CResList_GetData(&sl->list, pos);
		if (entry->direction == 1) {
			entry = (CEffectTableEntry *)CResList_GetData(&sl->list, pos);
			if (entry->weight != 0) {
				entry = (CEffectTableEntry *)CResList_GetData(&sl->list, pos);
				cumWeight += entry->weight;
				if (pick <= cumWeight) {
					localCtx = pos;
					CResBankMagicCtx_Copy(output, &localCtx);
					CResBankMagicCtx_PostInit(&localCtx);
					return output;
				}
			}
		}
		pos = CResList_Next(&sl->list, pos);
	}

	CResBankMagicCtx_Copy(output, &localCtx);
	CResBankMagicCtx_PostInit(&localCtx);
	return output;

	USED(localCtxValue);
}

/*
 * 0x0043F110 - CStringList::AllocIterNode (effects variant)
 *
 * Allocates a CEffectTableEntry with weight, inserts it at the chosen
 * direction, bumps totalWeight when the list is non-empty, and returns the
 * inserted position in *output.
 */
void *
CStringList_AllocIterNode(CStringList *sl, CResListNode **output, int weight, int direction)
{
	CEffectTableEntry *entry;
	void *constructed;
	void *srcData;
	CResListNode *node;
	CResListNode *pos;
	int flag;

	flag = 0;
	entry = (CEffectTableEntry *)malloc(sizeof(CEffectTableEntry));
	if (entry != NULL)
		constructed = CEffectTableEntry_ConstructorWeight(entry, weight);
	else
		constructed = NULL;

	srcData = constructed;
	node = CResList_InsertDirectionE(&sl->list, srcData, direction);
	CIterCtx_Set(&pos, node);

	if (CStringList_HasEntries((CStringList *)&pos))
		sl->totalWeight += weight;

	CResBankMagicCtx_Copy(output, &pos);
	flag |= 1;
	CResBankMagicCtx_PostInit(&pos);
	return output;

	USED(flag);
}

/*
 * 0x0043F1F0 - CStringListNode::~CStringListNode
 *
 * Destroys the embedded CString.
 */
static void
CStringListNode_Destructor(CStringListNode *node)
{
	CString_Destructor(&node->str);
}

/*
 * 0x0043F210 - CResList::ScalarDelete (include variant, 46 bytes)
 *
 * Destroys the include-variant list and frees it when flags&1.
 */
static CResList *
CResList_ScalarDeleteI(CResList *list, int flags)
{
	CResList_DestructorI(list);
	if (flags & 1)
		free(list);
	return NULL;
}

/*
 * 0x0043F240 - CResList::ScalarDelete (keys variant, 46 bytes)
 *
 * Destroys the keys-variant list and frees it when flags&1.
 */
static CResList *
CResList_ScalarDeleteS(CResList *list, int flags)
{
	CResList_DestructorS(list);
	if (flags & 1)
		free(list);
	return NULL;
}

/*
 * 0x0043F270 - CResList::ScalarDelete (props variant, 46 bytes)
 *
 * Destroys the props-variant list and frees it when flags&1.
 */
static CResList *
CResList_ScalarDeleteC(CResList *list, int flags)
{
	CResList_DestructorC(list);
	if (flags & 1)
		free(list);
	return NULL;
}

/*
 * 0x0043F2A0 - CResList::ScalarDelete (effectTbl variant, 46 bytes)
 *
 * Destroys the effectTbl-variant list and frees it when flags&1.
 */
static CResList *
CResList_ScalarDeleteT(CResList *list, int flags)
{
	CResList_DestructorT(list);
	if (flags & 1)
		free(list);
	return NULL;
}

/*
 * 0x0043F2F0 - CStringListEntry::CStringListEntry
 *
 * Initializes a CStringListNode with name (copied) and weight, type=1.
 */
static CStringListNode *
CStringListEntry_Constructor(CStringListNode *this, CString *name, int weight)
{
	CString_DefaultConstructor(&this->str);
	this->type = 1;
	CString_Assign(&this->str, name);
	this->weight = weight;
	return this;
}

/*
 * 0x0043F390 - CStringList::GetValue (effects variant, 30 bytes)
 *
 * Returns a pointer to the entry's weight/value field.
 */
void *
CStringList_GetValueEffects(CStringList *sl, CResListNode **nodePtr)
{
	CStringListNode *node = (CStringListNode *)CResList_GetData(&sl->list, *nodePtr);
	return &node->weight;
}

/*
 * 0x0043F3B0 - CResList::~CResList (CStringListNode variant, 19 bytes)
 *
 * Frees every node via CResList_EraseAllSLN.
 */
static __attribute__((unused)) void
CResList_DestructorSLN(CResList *list)
{
	CResList_EraseAllSLN(list);
}

/*
 * 0x0043F3D0 - CStringList::InsertEntry
 *
 * Prepends on direction==1, appends otherwise.
 */
static CResListNode *
CStringList_InsertEntry(CStringList *sl, void *data, int direction)
{
	if (direction == 1)
		return CResList_GetOrCreateNodeBin(&sl->list, data);
	return CResList_AllocAndSetDataNP(&sl->list, data);
}

/*
 * 0x0043F400 - CResList::AllocTailNodeA
 *
 * If tail exists, inserts new node after tail via RecycleNodeA.
 * Otherwise allocates a fresh CResListNode and sets it as head and tail.
 */
CResListNode *
CResList_AllocTailNodeA(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeA(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0043F4B0 - CResList::FreeNodeA
 *
 * Unlinks node, releases its CString payload, and returns the neighbor.
 */
CResListNode *
CResList_FreeNodeA(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_UnlinkNode(list, node, &data, direction);
	if (data != NULL)
		CString_ScalarDelete((CString *)data, 1);
	return result;
}

/*
 * 0x0043F510 - CResList::InsertAfterTailA
 *
 * Allocates node via RecycleNodeB, then copies CString data onto
 * the node via SetString.
 */
CResListNode *
CResList_InsertAfterTailA(CResList *list, CString *str)
{
	CResListNode *node;

	node = CResList_RecycleNodeB(list);
	if (node != NULL)
		CResListNode_SetString(node, str);
	return node;
}

/*
 * 0x0043F540 - CResList::FindByString
 *
 * Iterates a CResList from startNode (or head if NULL), comparing each
 * node's data (CString) to keyStr via CString_EqualCString2.
 * Returns matching node, or NULL if not found.
 */
CResListNode *
CResList_FindByString(CResList *list, void *keyStr, CResListNode *startNode, int direction)
{
	CResListNode *node = startNode;

	if (node == NULL)
		node = CResList_DirectionBegin(list, direction);

	while (CResList_IsValid(list, node)) {
		void *data = CResList_GetData(list, node);
		if (CString_EqualCString2((CString *)data, (CString *)keyStr))
			return node;
		node = CResList_DirectionNext(list, node, direction);
	}
	return NULL;
}

/*
 * 0x0043F5B0 - CResList: sorted insert
 *
 * Walks the list in direction, comparing data as CString* by LessThan or
 * GreaterThan per orderedFlag, and inserts at the first non-matching node
 * (or appends if the end is reached). Insertion uses the inverted direction.
 */
static void
CResList_SortedInsert(CResList *list, void *data, int direction, int orderedFlag)
{
	CResListNode *node;
	int cmpResult;
	int invertedDir;

	node = CResList_DirectionBegin(list, direction);

	while (CResList_IsValid(list, node)) {
		if (orderedFlag == 1)
			cmpResult = CString_LessThan((CString *)data, (CString *)CResList_GetData(list, node));
		else
			cmpResult = CString_GreaterThan((CString *)data, (CString *)CResList_GetData(list, node));

		if (cmpResult == 1) {
			invertedDir = (direction == 0) ? 1 : 0;
			CResList_InsertAtNodeStr(list, node, data, invertedDir);
			return;
		}

		node = CResList_DirectionNext(list, node, direction);
	}

	invertedDir = (direction == 0) ? 1 : 0;
	CResList_InsertAtEndStr(list, data, invertedDir);
}

/*
 * 0x0043F670 - CResListNode::SetString
 *
 * Lazily allocates node->data as a new CString copy of src, or assigns src
 * into an existing CString.
 */
void
CResListNode_SetString(CResListNode *node, CString *src)
{
	if (node->data == NULL) {
		CString *s = (CString *)malloc(sizeof(CString));
		if (s != NULL)
			CString_CopyConstructor(s, src);
		node->data = s;
	} else {
		CString_Assign((CString *)node->data, src);
	}
}

/*
 * 0x0043F700 - CResList::AllocTailNodeB
 *
 * Allocates a tail node for the CMagicItemList variant.
 */
CResListNode *
CResList_AllocTailNodeB(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeC(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0043F7B0 - CResListNode::InsertDataB
 *
 * Lazily allocates node->data as a CMagicItemListNode copy of srcData, or
 * assigns srcData into an existing node.
 */
void
CResListNode_InsertDataB(CResListNode *node, void *srcData)
{
	if (node->data == NULL) {
		CMagicItemListNode *d = (CMagicItemListNode *)malloc(sizeof(CMagicItemListNode));
		if (d != NULL)
			CMagicItemListNode_CopyConstructor(d, (CMagicItemListNode *)srcData);
		node->data = d;
	} else {
		CMagicItemListNode_SetData((CMagicItemListNode *)node->data, (CMagicItemListNode *)srcData);
	}
}

/*
 * 0x0043F840 - CResManager::SearchBucket (include variant, 176 bytes)
 *
 * Walks the bucket from startCtx looking for key; writes the matching
 * ctx or an empty ctx into *output.
 */
void
CResManager_SearchBucketA(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(startCtx)) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	CSearchCtx_Add(&localCtx, startCtx);

	while (CSearchCtx_Find(&localCtx)) {
		if (CString_EqualCString((CString *)CResManager_GetKeyAtPos(rm, &localCtx), (CString *)key)) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		CSearchCtx_Add(&localCtx, CResManager_NextEntryA(rm, &nextCtx, &localCtx, direction));
	}

	CSearchCtx_SetEntity(&localCtx, 0);
	CResManager_CreateBucket(output, &localCtx);
}

/*
 * 0x0043F8F0 - CResManager::BeginIterInternal (include variant, 243 bytes)
 *
 * Scans buckets from startBucket to find the first non-empty one and
 * writes its head ctx into *output.
 */
void
CResManager_BeginIterInternalA(CResManager *rm, CSearchCtx *output, int startBucket, int direction)
{
	CSearchCtx localCtx;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (startBucket >= 0x42) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	bucket = startBucket;
	for (;;) {
		if (rm->keys[bucket] != NULL) {
			localCtx.bucket = bucket;
			localCtx.keyNode = (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction);
			localCtx.valNode = (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction);
			localCtx.entity = 1;
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (bucket == 0 && direction == 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		if (bucket == 0x41 && direction != 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		// Advance bucket: direction==1 -> +1, direction==0 -> -1
		if (direction == 1)
			bucket += 1;
		else
			bucket -= 1;
	}
}

/*
 * 0x0043F9F0 - CResManager::NextIterInternal (include variant, 330 bytes)
 *
 * Advances the ctx to the next entry; crosses bucket boundaries via
 * BeginIterInternalA when the current bucket is exhausted.
 */
void
CResManager_NextIterInternalA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	// Copy bucket from current to localCtx
	bucket = CSearchCtx_GetBucket(current);
	CSearchCtx_SetBucket(&localCtx, bucket);

	// Advance key node
	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, direction);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)nextKey);

	// Advance val node
	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), direction);
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)nextVal);

	// Check if new key node is valid
	if (CResList_IsValid(rm->keys[CSearchCtx_GetBucket(&localCtx)], (CResListNode *)localCtx.keyNode)) {
		CSearchCtx_SetEntity(&localCtx, 1);
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	// Scan to next non-empty bucket
	bucket = CSearchCtx_GetBucket(&localCtx);
	if (bucket == 0 && direction == 0) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}
	if (bucket == 0x41 && direction != 0) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	// Advance bucket and recurse via BeginIterInternal
	if (direction == 1)
		bucket += 1;
	else
		bucket -= 1;
	CResManager_BeginIterInternalA(rm, output, bucket, direction);
}

/*
 * 0x0043FB40 - CResList::AllocTailNodeI (include variant, 170 bytes)
 *
 * If tail exists, inserts new node after tail via RecycleNodeI.
 * Otherwise allocates fresh CResListNode, sets as head and tail.
 */
static CResListNode *
CResList_AllocTailNodeI(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeI(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0043FBF0 - CResList::SetValueOnNodeI (include variant, 31 bytes)
 *
 * If node is non-null, delegates to CResListNode_ReplaceDataI.
 */
static void
CResList_SetValueOnNodeI(CResList *list, CResListNode *node, void *value)
{
	USED(list);
	if (node != NULL)
		CResListNode_ReplaceDataI(node, value);
}

/*
 * 0x0043FC10 - CResList::~CResList (include variant, 19 bytes)
 *
 * Destroys the include-variant list by delegating to ClearInternalI.
 */
static void
CResList_DestructorI(CResList *list)
{
	CResList_ClearInternalI(list);
}

/*
 * 0x0043FC30 - CResManager::CreateBucket
 *
 * Copies source CSearchCtx to dst via CSearchCtx_Add.
 */
void *
CResManager_CreateBucket(CSearchCtx *dst, CSearchCtx *src)
{
	CSearchCtx_Add(dst, src);
	return dst;
}

/*
 * 0x0043FC50 - CResManager::SearchBucket (keys variant, 176 bytes)
 *
 * Linear scan of the bucket for key, advancing via NextEntryD.
 */
static void __attribute__((unused))
CResManager_SearchBucketD(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(startCtx)) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	CSearchCtx_Add(&localCtx, startCtx);

	while (CSearchCtx_Find(&localCtx)) {
		if (CString_EqualCString((CString *)CResManager_GetKeyAtPos(rm, &localCtx), (CString *)key)) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		CSearchCtx_Add(&localCtx, CResManager_NextEntryD(rm, &nextCtx, &localCtx, direction));
	}

	CSearchCtx_SetEntity(&localCtx, 0);
	CResManager_CreateBucket(output, &localCtx);
}

/*
 * 0x0043FD00 - CResList::AllocTailNodeS (second variant, 170 bytes)
 *
 * If tail exists, inserts new node after tail via RecycleNodeS.
 * Otherwise allocates fresh CResListNode, sets as head and tail.
 */
static CResListNode *
CResList_AllocTailNodeS(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeS(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0043FDB0 - CResList::SetValueOnNodeS (second variant, 31 bytes)
 *
 * If node is non-null, delegates to CResListNode_ReplaceDataS.
 */
static void
CResList_SetValueOnNodeS(CResList *list, CResListNode *node, void *value)
{
	USED(list);
	if (node != NULL)
		CResListNode_ReplaceDataS(node, value);
}

/*
 * 0x0043FDD0 - CResList::~CResList (keys variant, 19 bytes)
 *
 * Destroys the keys-variant list by delegating to ClearInternalS.
 */
static void
CResList_DestructorS(CResList *list)
{
	CResList_ClearInternalS(list);
}

/*
 * 0x0043FDF0 - CResManager::SearchBucket (props variant, 176 bytes)
 *
 * Linear scan of the bucket for key, advancing via NextEntryC.
 */
static void __attribute__((unused))
CResManager_SearchBucketC(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(startCtx)) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	CSearchCtx_Add(&localCtx, startCtx);

	while (CSearchCtx_Find(&localCtx)) {
		if (CString_EqualCString((CString *)CResManager_GetKeyAtPos(rm, &localCtx), (CString *)key)) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		CSearchCtx_Add(&localCtx, CResManager_NextEntryC(rm, &nextCtx, &localCtx, direction));
	}

	CSearchCtx_SetEntity(&localCtx, 0);
	CResManager_CreateBucket(output, &localCtx);
}

/*
 * 0x0043FEA0 - CResList::AllocTailNodeC
 *
 * If tail exists, inserts new node after tail via RecycleNodeD.
 * Otherwise allocates a fresh CResListNode and sets it as head and tail.
 */
static CResListNode *
CResList_AllocTailNodeC(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeD(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0043FF50 - CResList::SetValueOnNodeC (props variant, 31 bytes)
 *
 * If node is non-null, delegates to CResListNode_ReplaceData.
 */
static void
CResList_SetValueOnNodeC(CResList *list, CResListNode *node, void *value)
{
	USED(list);
	if (node != NULL)
		CResListNode_ReplaceData(node, value);
}

/*
 * 0x0043FF70 - CResList::~CResList (props variant, 19 bytes)
 *
 * Destroys the props-variant list by delegating to ClearInternalC.
 */
static void
CResList_DestructorC(CResList *list)
{
	CResList_ClearInternalC(list);
}

/*
 * 0x0043FF90 - CResManager::SearchBucket (magic variant, 176 bytes)
 *
 * Linear scan of the bucket for key, advancing via NextEntryB.
 */
void
CResManager_SearchBucketB(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(startCtx)) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	CSearchCtx_Add(&localCtx, startCtx);

	while (CSearchCtx_Find(&localCtx)) {
		if (CString_EqualCString((CString *)CResManager_GetKeyAtPos(rm, &localCtx), (CString *)key)) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		CSearchCtx_Add(&localCtx, CResManager_NextEntryB(rm, &nextCtx, &localCtx, direction));
	}

	CSearchCtx_SetEntity(&localCtx, 0);
	CResManager_CreateBucket(output, &localCtx);
}

/*
 * 0x00440040 - CResManager::BeginIterInternal (magic variant, 243 bytes)
 *
 * Identical to include variant (0x0043F8F0).
 */
void
CResManager_BeginIterInternalB(CResManager *rm, CSearchCtx *output, int startBucket, int direction)
{
	CResManager_BeginIterInternalA(rm, output, startBucket, direction);
}

/*
 * 0x00440140 - CResManager::NextIterInternal (magic variant, 330 bytes)
 *
 * Advances ctx to the next entry, scanning buckets via BeginIterInternalB
 * when the current bucket is exhausted.
 */
void
CResManager_NextIterInternalB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	// Copy bucket from current to localCtx
	bucket = CSearchCtx_GetBucket(current);
	CSearchCtx_SetBucket(&localCtx, bucket);

	// Advance key node
	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, direction);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)nextKey);

	// Advance val node
	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), direction);
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)nextVal);

	// Check if new key node is valid
	if (CResList_IsValid(rm->keys[CSearchCtx_GetBucket(&localCtx)], (CResListNode *)localCtx.keyNode)) {
		CSearchCtx_SetEntity(&localCtx, 1);
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	// Scan to next non-empty bucket
	bucket = CSearchCtx_GetBucket(&localCtx);
	if (bucket == 0 && direction == 0) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}
	if (bucket == 0x41 && direction != 0) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	// Advance bucket and recurse via BeginIterInternalB
	if (direction == 1)
		bucket += 1;
	else
		bucket -= 1;
	CResManager_BeginIterInternalB(rm, output, bucket, direction);
}

/*
 * 0x00440290 - CResList::AllocTailNodeE
 *
 * If tail exists, inserts new node after tail via RecycleNodeE.
 * Otherwise allocates fresh CResListNode, sets as head and tail.
 */
static CResListNode *
CResList_AllocTailNodeE(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeE(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x00440340 - CResList::SetValueOnNodeEffects (effectTbl variant, 31 bytes)
 *
 * If node is non-NULL, calls CResListNode_SetDataSLE to set data.
 */
static void
CResList_SetValueOnNodeEffects(CResList *list, CResListNode *node, void *srcData)
{
	USED(list);
	if (node != NULL)
		CResListNode_SetDataSLE(node, srcData);
}

/*
 * 0x00440360 - CResList::~CResList (effectTbl variant, 19 bytes)
 *
 * Destroys the effectTbl-variant list by delegating to ClearInternalT.
 */
static void
CResList_DestructorT(CResList *list)
{
	CResList_ClearInternalT(list);
}

/*
 * 0x00440380 - CResList::DirectionBegin
 *
 * Returns the head when direction==1, otherwise the tail.
 */
CResListNode *
CResList_DirectionBegin(CResList *list, int direction)
{
	if (direction == 1)
		return CResList_Begin(list);
	return CResList_GetTail(list);
}

/*
 * 0x004403B0 - CStringList::GetWeight (effects variant, 30 bytes)
 *
 * Returns a pointer to the entry's weight field.
 */
static int *
CStringList_GetWeightEffects(CStringList *sl, CResListNode **nodePtr)
{
	CEffectTableEntry *entry = (CEffectTableEntry *)CResList_GetData(&sl->list, *nodePtr);
	return &entry->weight;
}

/*
 * 0x004403D0 - CResList::~CResList (wrapper E)
 *
 * Destroys the effects-variant list by delegating to ClearInternalE.
 */
static void
CResList_DestructorE(CResList *list)
{
	CResList_ClearInternalE(list);
}

/*
 * 0x004403F0 - CResList::InsertDirectionE
 *
 * Dispatches to AllocForwardE on direction==1, else AllocBackwardE.
 */
static CResListNode *
CResList_InsertDirectionE(CResList *list, void *srcData, int direction)
{
	if (direction == 1)
		return CResList_AllocForwardE(list, srcData);
	return CResList_AllocBackwardE(list, srcData);
}

/*
 * 0x00440450 - CResList::EraseAll (CStringListNode variant, 59 bytes)
 *
 * Iterates from head, freeing every node via FreeNodeSLN.
 */
void
CResList_EraseAllSLN(CResList *list)
{
	CResListNode *pos = CResList_Begin(list);
	while (CResList_IsValid(list, pos))
		pos = CResList_FreeNodeSLN(list, pos, 1);
}

/*
 * 0x00440490 - CResList::GetOrCreateNodeBin
 *
 * Gets or allocates a head node and sets data on it via SetDataNP.
 */
static CResListNode *
CResList_GetOrCreateNodeBin(CResList *list, void *data)
{
	CResListNode *node;

	node = CResList_GetOrAllocHeadNP(list);
	if (node != NULL)
		CResListNode_SetDataNP(node, (CNamedProperty *)data);
	return node;
}

/*
 * 0x004404C0 - CResList::AllocAndSetDataNP
 *
 * Gets or allocates a tail node and sets data on it via SetDataNP.
 */
CResListNode *
CResList_AllocAndSetDataNP(CResList *list, void *data)
{
	CResListNode *node;

	node = CResList_GetOrAllocTailNP(list);
	if (node != NULL)
		CResListNode_SetDataNP(node, (CNamedProperty *)data);
	return node;
}

/*
 * 0x004404F0 - CResList::RecycleNodeA
 *
 * Allocates a new CResListNode and inserts it after afterNode in the
 * doubly-linked list. Updates head/tail as needed.
 */
CResListNode *
CResList_RecycleNodeA(CResList *list, CResListNode *afterNode)
{
	CResListNode *next, *newNode;

	if (afterNode == NULL)
		return NULL;

	next = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, next);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (next != NULL) {
		CResListNode_SetPrev(next, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x004405E0 - CResList::RecycleNodeB
 *
 * If list has a head, inserts a new node before head via InsertBeforeNode.
 * Otherwise allocates a fresh CResListNode and sets it as sole head+tail.
 */
CResListNode *
CResList_RecycleNodeB(CResList *list)
{
	CResListNode *node;

	if (list->head != NULL)
		return CResList_InsertBeforeNode(list, list->head);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->head = node;
	list->tail = list->head;
	list->count++;
	return list->head;
}

/*
 * 0x00440690 - CResList: positional insert dispatch
 *
 * Inserts data after node on direction==1, else before it. Used by
 * CResList_SortedInsert at the insertion point.
 */
static void
CResList_InsertAtNodeStr(CResList *list, CResListNode *node, void *data, int direction)
{
	if (direction == 1)
		CResList_InsertAfterStr(list, node, data);
	else
		CResList_InsertBeforeStr(list, node, data);
}

/*
 * 0x004406D0 - CResList::UnlinkNode
 *
 * Unlinks node from doubly-linked list, extracts data into *outData
 * (sets node->data to NULL via SwapData), frees the node via
 * CResListNode_ScalarDeleteStr, decrements count.
 * Returns neighbor (next if direction=1, prev if direction=0).
 */
CResListNode *
CResList_UnlinkNode(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNode_ScalarDeleteStr(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004407A0 - CResList: end-of-list insert dispatch
 *
 * Prepends at head on direction==1, else appends at tail. Used by
 * CResList_SortedInsert when the end of the list is reached.
 */
static void
CResList_InsertAtEndStr(CResList *list, void *data, int direction)
{
	if (direction == 1)
		CResList_PrependStr(list, data);
	else
		CResList_AppendStr(list, data);
}

/*
 * 0x004407D0 - CResList::RecycleNodeC
 *
 * Identical to RecycleNodeA. Used for CMagicItemList nodes.
 */
CResListNode *
CResList_RecycleNodeC(CResList *list, CResListNode *afterNode)
{
	CResListNode *next, *newNode;

	if (afterNode == NULL)
		return NULL;

	next = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, next);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (next != NULL) {
		CResListNode_SetPrev(next, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x004408C0 - CResManager::GetKeyAtPos
 *
 * Returns the key data at the ctx's key list position.
 */
void *
CResManager_GetKeyAtPos(CResManager *rm, CSearchCtx *ctx)
{
	CResList *keyList;

	USED(rm);
	(void)CSearchCtx_Find(ctx);
	keyList = rm->keys[ctx->bucket];
	return CResList_GetData(keyList, (CResListNode *)(uintptr_t)ctx->keyNode);
}

/*
 * 0x00440900 - CResManager::NextEntry (include variant, 212 bytes)
 *
 * Advances key and val node pointers via DirectionNext and populates
 * output CSearchCtx with the new node positions.
 */
static void *
CResManager_NextEntryA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	// Advance key and val nodes in parallel
	bucket = current->bucket;
	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, direction);
	localCtx.keyNode = (uintptr_t)nextKey;

	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)current->valNode, direction);
	localCtx.valNode = (uintptr_t)nextVal;

	localCtx.bucket = current->bucket;

	// Check if new key node is valid
	localCtx.entity = CResList_IsValid(rm->keys[localCtx.bucket], (CResListNode *)localCtx.keyNode);

	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x004409E0 - CResList::RecycleNodeI (include variant, 234 bytes)
 *
 * Allocates a new CResListNode and inserts it after afterNode.
 * Uses CResListNode_SetNext/SetPrev helpers and CResList_Begin
 * to read afterNode->next.
 */
static CResListNode *
CResList_RecycleNodeI(CResList *list, CResListNode *afterNode)
{
	CResListNode *head, *newNode;

	if (afterNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00440AD0 - CResList::ClearInternal (include variant, 59 bytes)
 *
 * Iterates Begin/IsValid/FreeNodeI to free every node.
 */
static void
CResList_ClearInternalI(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node))
		node = CResList_FreeNodeI(list, node, 1);
}

/*
 * 0x00440B10 - CResListNode::CResListNode
 *
 * Binary ctor. Zeros next, prev, data. Returns this.
 */
CResListNode *
CResListNode_Constructor_bin(CResListNode *node)
{
	node->next = NULL;
	node->prev = NULL;
	node->data = NULL;
	return node;
}

/*
 * 0x00440B40 - CResListNode::ReplaceData (include variant, 86 bytes)
 *
 * Frees existing CStringList data (if any) before storing newData.
 */
static void
CResListNode_ReplaceDataI(CResListNode *node, void *newData)
{
	if (node->data != NULL) {
		CStringList_ScalarDelete((CStringList *)node->data, 1);
		node->data = NULL;
	}
	node->data = newData;
}

/*
 * 0x00440BA0 - CResManager::NextEntry (keys variant, 212 bytes)
 *
 * Identical to include variant (0x00440900). Used by SearchBucketD.
 */
static void *
CResManager_NextEntryD(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	return CResManager_NextEntryA(rm, output, current, direction);
}

/*
 * 0x00440C80 - CResList::RecycleNodeS (second variant, 234 bytes)
 *
 * Allocates a new CResListNode and inserts it after afterNode.
 * Uses CResListNode_SetNext/SetPrev helpers and CResList_Begin
 * to read afterNode->next.
 */
static CResListNode *
CResList_RecycleNodeS(CResList *list, CResListNode *afterNode)
{
	CResListNode *head, *newNode;

	if (afterNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00440D70 - CResList::ClearInternal (keys variant, 59 bytes)
 *
 * Iterates Begin/IsValid/FreeNodeS to free every node.
 */
static void
CResList_ClearInternalS(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node))
		node = CResList_FreeNodeS(list, node, 1);
}

/*
 * 0x00440DB0 - CResListNode::ReplaceData (second variant, 86 bytes)
 *
 * Frees existing CResList data (if any) before storing newData.
 */
static void
CResListNode_ReplaceDataS(CResListNode *node, void *newData)
{
	if (node->data != NULL) {
		CResList_ScalarDeleteA((CResList *)node->data, 1);
		node->data = NULL;
	}
	node->data = newData;
}

/*
 * 0x00440E10 - CResManager::NextEntry (props variant, 212 bytes)
 *
 * Identical to include variant (0x00440900). Used by SearchBucketC.
 */
static void *
CResManager_NextEntryC(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	return CResManager_NextEntryA(rm, output, current, direction);
}

/*
 * 0x00440EF0 - CResList::RecycleNodeD
 *
 * Allocates a new CResListNode and inserts it after afterNode.
 */
CResListNode *
CResList_RecycleNodeD(CResList *list, CResListNode *afterNode)
{
	CResListNode *head, *newNode;

	if (afterNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00440FE0 - CResList::ClearInternal (props variant, 59 bytes)
 *
 * Iterates Begin/IsValid/FreeNodeC to free every node.
 */
static void
CResList_ClearInternalC(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node))
		node = CResList_FreeNodeC(list, node, 1);
}

/*
 * 0x00441020 - CResListNode: replace data
 *
 * Frees existing CItemEffectDef data (if any) before storing newData.
 */
static void
CResListNode_ReplaceData(CResListNode *node, void *newData)
{
	if (node->data != NULL) {
		CItemEffectDef_ScalarDelete((CItemEffectDef *)node->data, 1);
		node->data = NULL;
	}
	node->data = newData;
}

/*
 * 0x00441080 - CResManager::NextEntry (magic variant, 212 bytes)
 *
 * Identical to include variant (0x00440900).
 */
static void *
CResManager_NextEntryB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	return CResManager_NextEntryA(rm, output, current, direction);
}

/*
 * 0x00441160 - CResList::RecycleNodeE
 *
 * Allocates a new CResListNode and inserts it after afterNode.
 */
static CResListNode *
CResList_RecycleNodeE(CResList *list, CResListNode *afterNode)
{
	CResListNode *head, *newNode;

	if (afterNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00441250 - CResList::ClearInternal (effectTbl variant, 59 bytes)
 *
 * Iterates Begin/IsValid/FreeNodeT to free every node.
 */
static void
CResList_ClearInternalT(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node))
		node = CResList_FreeNodeT(list, node, 1);
}

/*
 * 0x00441290 - CResListNode: set CStringList* data with ownership
 *
 * Frees existing CStringList data (if any) before storing srcData.
 */
static void
CResListNode_SetDataSLE(CResListNode *node, void *srcData)
{
	if (node->data != NULL) {
		CStringList_ScalarDeleteEffects((CStringList *)node->data, 1);
		node->data = NULL;
	}
	node->data = srcData;
}

/*
 * 0x004412F0 - CResList::ClearInternalE
 *
 * Iterates list from head, calling FreeNodeE(node, 1) on each node
 * to unlink and free the CEffectTableEntry data.
 */
static void
CResList_ClearInternalE(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_FreeNodeE(list, node, 1);
	}
}

/*
 * 0x00441330 - CResList::AllocForwardE
 *
 * Allocates a head node via GetOrAllocHeadE and sets data via SetDataE.
 */
static CResListNode *
CResList_AllocForwardE(CResList *list, void *srcData)
{
	CResListNode *node;

	node = CResList_GetOrAllocHeadE(list);
	if (node != NULL)
		CResList_SetDataE(node, srcData);
	return node;
}

/*
 * 0x00441360 - CResList::AllocBackwardE
 *
 * Allocates a tail node via GetOrAllocTailE and sets data via SetDataE.
 */
static CResListNode *
CResList_AllocBackwardE(CResList *list, void *srcData)
{
	CResListNode *node;

	node = CResList_GetOrAllocTailE(list);
	if (node != NULL)
		CResList_SetDataE(node, srcData);
	return node;
}

/*
 * 0x00441390 - CResListNode::ScalarDelete (CString* data variant, 46 bytes)
 *
 * Destroys the node and frees it when flags&1.
 */
static CResListNode *
CResListNode_ScalarDeleteStr(CResListNode *node, int flags)
{
	CResListNode_DestructorStr(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x004413C0 - CResList::GetOrAllocHead (NamedProperty variant, 166 bytes)
 *
 * If head exists, inserts a new node before it via InsertAfterTailNP.
 * Otherwise allocates a fresh CResListNode and sets it as sole head+tail.
 */
static CResListNode *
CResList_GetOrAllocHeadNP(CResList *list)
{
	CResListNode *node;

	if (list->head != NULL)
		return CResList_InsertAfterTailNP(list, list->head);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->head = node;
	list->tail = list->head;
	list->count++;
	return list->head;
}

/*
 * 0x00441470 - CResList::GetOrAllocTail (NamedProperty variant, 170 bytes)
 *
 * If tail exists, inserts a new node after it via InsertBeforeHeadNP.
 * Otherwise allocates a fresh CResListNode and sets it as sole head+tail.
 */
static CResListNode *
CResList_GetOrAllocTailNP(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_InsertBeforeHeadNP(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x00441520 - CResList::FreeNode (CStringListNode variant, 87 bytes)
 *
 * Unlinks node and destroys the extracted CStringListNode data.
 */
static CResListNode *
CResList_FreeNodeSLN(CResList *list, CResListNode *node, int direction)
{
	void *data;
	CResListNode *result;

	result = CResList_UnlinkNodeSLN(list, node, &data, direction);
	if (data != NULL)
		CStringListNode_ScalarDelete((CStringListNode *)data, 1);
	return result;
}

/*
 * 0x00441580 - CResListNode::SetDataNP (NamedProperty data setter, 144 bytes)
 *
 * Updates existing CNamedProperty data via Copy, or allocates and
 * constructs a fresh one when node->data is NULL.
 */
static void
CResListNode_SetDataNP(CResListNode *node, CNamedProperty *src)
{
	if (node->data != NULL) {
		CNamedProperty_Copy((CNamedProperty *)node->data, src);
		return;
	}

	CNamedProperty *prop = (CNamedProperty *)malloc(sizeof(CNamedProperty));
	if (prop != NULL)
		CNamedProperty_Constructor(prop, src);

	node->data = prop;
}

/*
 * 0x00441610 - CResList::InsertBeforeNode
 *
 * Allocates a new CResListNode and inserts it before beforeNode,
 * updating head/tail as needed.
 */
CResListNode *
CResList_InsertBeforeNode(CResList *list, CResListNode *beforeNode)
{
	CResListNode *prev, *newNode;

	if (beforeNode == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)beforeNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetPrev(newNode, prev);
	CResListNode_SetNext(newNode, beforeNode);
	CResListNode_SetPrev(beforeNode, newNode);

	if (prev != NULL) {
		CResListNode_SetNext(prev, newNode);
	} else {
		list->head = newNode;
		if (list->tail == NULL)
			list->tail = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00441700 - CResList: insert before node with CString* data
 *
 * Inserts a new node before beforeNode and sets its CString data.
 */
static CResListNode *
CResList_InsertBeforeStr(CResList *list, CResListNode *beforeNode, void *data)
{
	CResListNode *node;

	node = CResList_InsertBeforeNode(list, beforeNode);
	if (node != NULL)
		CResListNode_SetDataStr(node, data);
	return node;
}

/*
 * 0x00441740 - CResList: insert after node with CString* data
 *
 * Inserts a new node after afterNode and sets its CString data.
 */
static CResListNode *
CResList_InsertAfterStr(CResList *list, CResListNode *afterNode, void *data)
{
	CResListNode *node;

	node = CResList_RecycleNodeA(list, afterNode);
	if (node != NULL)
		CResListNode_SetDataStr(node, data);
	return node;
}

/*
 * 0x00441780 - CResList: prepend at head with CString* data
 *
 * Prepends a new node at head and sets its CString data.
 */
static CResListNode *
CResList_PrependStr(CResList *list, void *data)
{
	CResListNode *node;

	node = CResList_RecycleNodeB(list);
	if (node != NULL)
		CResListNode_SetDataStr(node, data);
	return node;
}

/*
 * 0x004417B0 - CResList: append at tail with CString* data
 *
 * Appends a new node at tail and sets its CString data.
 */
static CResListNode *
CResList_AppendStr(CResList *list, void *data)
{
	CResListNode *node;

	node = CResList_AllocTailNodeA(list);
	if (node != NULL)
		CResListNode_SetDataStr(node, data);
	return node;
}

/*
 * 0x004417E0 - CResListNode::~CResListNode (CString* data variant, 75 bytes)
 *
 * Destroys the node's CString payload if present.
 */
static void
CResListNode_DestructorStr(CResListNode *node)
{
	if (node->data != NULL) {
		CString_ScalarDelete((CString *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x00441830 - CResList::Prev
 *
 * Returns node->prev, or NULL if node is NULL.
 */
CResListNode *
CResList_Prev(CResList *list, CResListNode *node)
{
	USED(list);
	if (node == NULL)
		return NULL;
	return CResList_GetTail((CResList *)node);
}

/*
 * 0x00441850 - CResList::FreeNode (include variant, 87 bytes)
 *
 * Unlinks node and destroys the extracted CStringList data.
 */
static CResListNode *
CResList_FreeNodeI(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_UnlinkNodeI(list, node, &data, direction);
	if (data != NULL)
		CStringList_ScalarDelete((CStringList *)data, 1);
	return result;
}

/*
 * 0x004418B0 - CResList::FreeNode (keys variant, 87 bytes)
 *
 * Unlinks node and destroys the extracted CResList data.
 */
static CResListNode *
CResList_FreeNodeS(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_UnlinkNodeS(list, node, &data, direction);
	if (data != NULL)
		CResList_ScalarDeleteA((CResList *)data, 1);
	return result;
}

/*
 * 0x00441910 - CResList::FreeNode (props variant, 87 bytes)
 *
 * Unlinks node and destroys the extracted CItemEffectDef data.
 */
static CResListNode *
CResList_FreeNodeC(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_UnlinkNodeC(list, node, &data, direction);
	if (data != NULL)
		CItemEffectDef_ScalarDelete((CItemEffectDef *)data, 1);
	return result;
}

/*
 * 0x00441970 - CResList::FreeNode (effectTbl variant, 87 bytes)
 *
 * Unlinks node and destroys the extracted CStringList data.
 */
static CResListNode *
CResList_FreeNodeT(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_UnlinkNodeT(list, node, &data, direction);
	if (data != NULL)
		CStringList_ScalarDeleteEffects((CStringList *)data, 1);
	return result;
}

/*
 * 0x004419D0 - CResList::GetOrAllocHeadE
 *
 * If head exists, inserts new node before head via InsertAfterTailE.
 * Otherwise allocates fresh CResListNode, sets as sole head+tail.
 */
static CResListNode *
CResList_GetOrAllocHeadE(CResList *list)
{
	CResListNode *node;

	if (list->head != NULL)
		return CResList_InsertAfterTailE(list, list->head);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->head = node;
	list->tail = list->head;
	list->count++;
	return list->head;
}

/*
 * 0x00441A80 - CResList::GetOrAllocTailE
 *
 * If tail exists, inserts new node after tail via InsertBeforeHeadE.
 * Otherwise allocates fresh CResListNode, sets as sole head+tail.
 */
static CResListNode *
CResList_GetOrAllocTailE(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_InsertBeforeHeadE(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x00441B30 - CResList::FreeNodeE
 *
 * Unlinks node and destroys the extracted CEffectTableEntry data.
 * Returns the neighbor (next if direction=1, prev if direction=0).
 */
static CResListNode *
CResList_FreeNodeE(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_UnlinkNodeE(list, node, &data, direction);
	if (data != NULL)
		CEffectTableEntry_ScalarDeleteE((CEffectTableEntry *)data, 1);
	return result;
}

/*
 * 0x00441B90 - CResList::SetDataE
 *
 * Frees existing CEffectTableEntry data (if any) before storing srcData.
 */
static void
CResList_SetDataE(CResListNode *node, void *srcData)
{
	if (node->data != NULL) {
		CEffectTableEntry_ScalarDeleteE((CEffectTableEntry *)node->data, 1);
		node->data = NULL;
	}
	node->data = srcData;
}

/*
 * 0x00441BF0 - CStringListNode::ScalarDelete
 *
 * Destroys the node and frees it when flags&1.
 */
static CStringListNode *
CStringListNode_ScalarDelete(CStringListNode *node, int flags)
{
	CStringListNode_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x00441CC0 - CNamedProperty::CNamedProperty
 *
 * Default-constructs the embedded CString name, then copies fields from
 * source via CNamedProperty_Copy.
 */
static CNamedProperty *
CNamedProperty_Constructor(CNamedProperty *this, CNamedProperty *src)
{
	CString_DefaultConstructor(&this->name);
	CNamedProperty_Copy(this, src);
	return this;
}

/*
 * 0x00441D20 - CNamedProperty::Copy
 *
 * Copies the name and the two scalar fields from src to this.
 */
static CNamedProperty *
CNamedProperty_Copy(CNamedProperty *this, CNamedProperty *src)
{
	CString_Assign(&this->name, &src->name);
	this->field_10 = src->field_10;
	this->field_14 = src->field_14;
	return this;
}

/*
 * 0x00441D60 - CResList::InsertAfterTailNP (NamedProperty variant, 235 bytes)
 *
 * Allocates a new CResListNode and inserts it before headNode
 * (i.e. after its tail/prev).
 */
static CResListNode *
CResList_InsertAfterTailNP(CResList *list, CResListNode *headNode)
{
	CResListNode *tail, *newNode;

	if (headNode == NULL)
		return NULL;

	tail = CResList_GetTail((CResList *)headNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetPrev(newNode, tail);
	CResListNode_SetNext(newNode, headNode);
	CResListNode_SetPrev(headNode, newNode);

	if (tail != NULL) {
		CResListNode_SetNext(tail, newNode);
	} else {
		list->head = newNode;
		if (list->tail == NULL)
			list->tail = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00441E50 - CResList::InsertBeforeHeadNP (NamedProperty variant, 234 bytes)
 *
 * Allocates a new CResListNode and inserts it after tailNode
 * (i.e. before its head/next).
 */
static CResListNode *
CResList_InsertBeforeHeadNP(CResList *list, CResListNode *tailNode)
{
	CResListNode *head, *newNode;

	if (tailNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)tailNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, tailNode);
	CResListNode_SetNext(tailNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00441F40 - CResList::UnlinkNode (CStringListNode variant, 200 bytes)
 *
 * Unlinks node from the doubly-linked list, extracts data into *outData,
 * frees the node wrapper, and returns the neighbor in the given direction.
 */
static CResListNode *
CResList_UnlinkNodeSLN(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeSLN_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x00442010 - CResListNode: set CString* data with ownership
 *
 * Frees existing CString data (if any) before storing srcData.
 */
static void
CResListNode_SetDataStr(CResListNode *node, void *srcData)
{
	if (node->data != NULL) {
		CString_ScalarDelete((CString *)node->data, 1);
		node->data = NULL;
	}
	node->data = srcData;
}

/*
 * 0x00442070 - CResList::UnlinkNode (include variant, 200 bytes)
 *
 * Unlinks node from the doubly-linked list, extracts data into *outData,
 * frees the node wrapper, and returns the neighbor in the given direction.
 */
static CResListNode *
CResList_UnlinkNodeI(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeI_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x00442140 - CResList::UnlinkNode (keys variant, 200 bytes)
 *
 * Unlinks node from the doubly-linked list, extracts data into *outData,
 * frees the node wrapper, and returns the neighbor in the given direction.
 */
static CResListNode *
CResList_UnlinkNodeS(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeS_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x00442210 - CResList::UnlinkNode (props variant, 200 bytes)
 *
 * Unlinks node from the doubly-linked list, extracts data into *outData,
 * frees the node wrapper, and returns the neighbor in the given direction.
 */
static CResListNode *
CResList_UnlinkNodeC(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeC_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004422E0 - CResList::UnlinkNode (effectTbl variant, 200 bytes)
 *
 * Unlinks node from the doubly-linked list, extracts data into *outData,
 * frees the node wrapper, and returns the neighbor in the given direction.
 */
static CResListNode *
CResList_UnlinkNodeT(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeT_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004423B0 - CResList::InsertAfterTailE
 *
 * Allocates new CResListNode, inserts before beforeNode.
 * Uses CResListNode_SetNext/SetPrev helpers.
 */
static CResListNode *
CResList_InsertAfterTailE(CResList *list, CResListNode *beforeNode)
{
	CResListNode *prev, *newNode;

	if (beforeNode == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)beforeNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetPrev(newNode, prev);
	CResListNode_SetNext(newNode, beforeNode);
	CResListNode_SetPrev(beforeNode, newNode);

	if (prev != NULL) {
		CResListNode_SetNext(prev, newNode);
	} else {
		list->head = newNode;
		if (list->tail == NULL)
			list->tail = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x004424A0 - CResList::InsertBeforeHeadE
 *
 * Allocates new CResListNode, inserts after afterNode.
 * Uses CResListNode_SetNext/SetPrev helpers.
 */
static CResListNode *
CResList_InsertBeforeHeadE(CResList *list, CResListNode *afterNode)
{
	CResListNode *next, *newNode;

	if (afterNode == NULL)
		return NULL;

	next = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, next);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (next != NULL) {
		CResListNode_SetPrev(next, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x00442590 - CResList::UnlinkNodeE
 *
 * Unlinks node from doubly-linked list, extracts data into *outData
 * (sets node->data to NULL), frees the node via scalar delete,
 * decrements count. Returns neighbor (next if direction=1, prev=0).
 */
static CResListNode *
CResList_UnlinkNodeE(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeE_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x00442660 - CResListNodeSLN::ScalarDelete (CStringListNode variant, 46 bytes)
 *
 * Destroys the node and frees it when flags&1.
 */
static CResListNode *
CResListNodeSLN_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeSLN_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x00442690 - CResListNodeI::ScalarDelete (include variant, 46 bytes)
 *
 * Destroys the node and frees it when flags&1.
 */
static CResListNode *
CResListNodeI_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeI_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x004426C0 - CResListNodeS::ScalarDelete (keys variant, 46 bytes)
 *
 * Destroys the node and frees it when flags&1.
 */
static CResListNode *
CResListNodeS_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeS_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x004426F0 - CResListNodeC::ScalarDelete (props variant, 46 bytes)
 *
 * Destroys the node and frees it when flags&1.
 */
static CResListNode *
CResListNodeC_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeC_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x00442720 - CResListNodeT::ScalarDelete (effectTbl variant, 46 bytes)
 *
 * Destroys the node and frees it when flags&1.
 */
static CResListNode *
CResListNodeT_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeT_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x00442750 - CResListNodeE::ScalarDelete
 *
 * Destroys the node and frees it when flags&1.
 */
static CResListNode *
CResListNodeE_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeE_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x00442780 - CResListNodeSLN::~CResListNodeSLN (CStringListNode variant, 75 bytes)
 *
 * Destroys the node's CStringListNode payload if present.
 */
static void
CResListNodeSLN_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CStringListNode_ScalarDelete((CStringListNode *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x004427D0 - CResListNodeI::~CResListNodeI (include variant, 75 bytes)
 *
 * Destroys the node's CStringList payload if present.
 */
static void
CResListNodeI_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CStringList_ScalarDelete((CStringList *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x00442820 - CResListNodeS::~CResListNodeS (keys variant, 75 bytes)
 *
 * Destroys the node's CResList payload if present.
 */
static void
CResListNodeS_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CResList_ScalarDeleteA((CResList *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x00442870 - CResListNodeC::~CResListNodeC (props variant, 75 bytes)
 *
 * Destroys the node's CItemEffectDef payload if present.
 */
static void
CResListNodeC_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CItemEffectDef_ScalarDelete((CItemEffectDef *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x004428C0 - CResListNodeT::~CResListNodeT (effectTbl variant, 75 bytes)
 *
 * Destroys the node's CStringList payload if present.
 */
static void
CResListNodeT_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CStringList_ScalarDeleteEffects((CStringList *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x00442910 - CResListNodeE::~CResListNodeE
 *
 * Destroys the node's CEffectTableEntry payload if present.
 */
static void
CResListNodeE_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CEffectTableEntry_ScalarDeleteE((CEffectTableEntry *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x0045EC0D - CResManager::InsertByRef
 *
 * Inserts value into list via CResList_AllocAndSetData.
 */
void
CResManager_InsertByRef(CResList *list, uint32_t val)
{
	CResList_AllocAndSetData(list, &val);
}

/*
 * 0x0045EC26 - CResList::RemoveByValue
 *
 * Finds value in list and erases the matching node, freeing its data.
 */
void
CResList_RemoveByValue(CResList *list, uint32_t value)
{
	CResListNode *node;

	node = CResList_FindByValue(list, &value, NULL, 1);
	if (CResList_IsValid(list, node)) {
		CResList_EraseAndFree_Spawn(list, node, 1);
	}
}

/*
 * 0x0045F290 - CResList::EraseAndFree (spawn variant)
 *
 * Erases node from the list and frees its extracted data. Returns the
 * next node when flag=1, prev when flag=0.
 */
CResListNode *
CResList_EraseAndFree_Spawn(CResList *list, CResListNode *node, int flag)
{
	CResListNode *result;
	void *outData;

	outData = NULL;
	result = CResList_Erase_Spawn(list, node, &outData, flag);
	if (outData != NULL)
		free(outData);
	return result;
}

/*
 * 0x0045F300 - CResList::AllocAndSetData
 *
 * Allocates a new node and copies *valuePtr into its data via SetDataInt.
 */
static CResListNode *
CResList_AllocAndSetData(CResList *list, uint32_t *valuePtr)
{
	CResListNode *node;

	node = CResList_AllocNode(list);
	if (node != NULL) {
		CResListNode_SetDataInt(node, valuePtr);
	}
	return node;
}

/*
 * 0x0045F330 - CResList::FindByValue
 *
 * Linear search for a node whose data uint32 matches *keyPtr. Starts from
 * startNode, or from the head/tail when NULL, honoring direction.
 */
CResListNode *
CResList_FindByValue(CResList *list, void *keyPtr, CResListNode *startNode, int direction)
{
	CResListNode *node;

	if (startNode == NULL) {
		node = CResList_DirectionBegin(list, direction);
	} else {
		node = startNode;
	}

	while (CResList_IsValid(list, node)) {
		if (*(uint32_t *)CResList_GetData(list, node) == *(uint32_t *)keyPtr)
			return node;
		node = CResList_DirectionNext(list, node, direction);
	}
	return NULL;
}

/*
 * 0x0045F3A0 - CResList::AllocNode
 *
 * When the list is non-empty, delegates to PushFront_SpawnLocal to insert
 * before tail. Otherwise allocates a fresh node and sets it as sole
 * head+tail.
 */
static CResListNode *
CResList_AllocNode(CResList *list)
{
	CResListNode *newNode;
	void *raw;

	if (list->tail != NULL) {
		return CResList_PushFront_SpawnLocal(list, list->tail);
	}

	// Allocate new node
	raw = malloc(sizeof(CResListNode));
	if (raw != NULL)
		newNode = CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	list->tail = newNode;
	list->head = list->tail;
	list->count = list->count + 1;
	return list->tail;
}

/*
 * 0x0045F450 - CResList::Erase template instantiation for spawn lists
 *
 * Unlinks node, extracts its data into *outData, destroys the node,
 * decrements count, and returns the next or prev neighbor per direction.
 */
CResListNode *
CResList_Erase_Spawn(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNode_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x0045F560 - CResListNode::SetDataInt
 *
 * Stores *srcPtr into node->data, allocating the slot on first use.
 *
 * 64-bit: binary stores a 4-byte DWORD; callers pass pointer-sized
 * values, so we use uintptr_t to preserve the full pointer.
 */
void
CResListNode_SetDataInt(CResListNode *node, void *srcPtr)
{
	if (node->data == NULL) {
		uintptr_t *newData = (uintptr_t *)malloc(sizeof(uintptr_t));
		if (newData != NULL)
			*newData = *(uintptr_t *)srcPtr;
		node->data = newData;
	} else {
		*(uintptr_t *)node->data = *(uintptr_t *)srcPtr;
	}
}

/*
 * 0x0045F5D0 - CResList::PushFront (spawn variant)
 *
 * Inserts a new node immediately after afterNode, updating head/tail
 * if the list was empty. Returns the new node, or NULL if afterNode
 * is NULL.
 */
static CResListNode *
CResList_PushFront_SpawnLocal(CResList *list, CResListNode *afterNode)
{
	CResListNode *firstNode;
	CResListNode *newNode;
	void *raw;

	if (afterNode == NULL)
		return NULL;

	firstNode = CResList_Begin((CResList *)afterNode);

	raw = malloc(sizeof(CResListNode));
	if (raw != NULL)
		newNode = CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	CResListNode_SetNext(newNode, firstNode);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (firstNode != NULL) {
		CResListNode_SetPrev(firstNode, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count = list->count + 1;
	return newNode;
}

/*
 * 0x004639C0 - ResManager_HashInt
 *
 * Returns key % (bucketCount + 1). Always called with bucketCount=65.
 */
uint32_t
ResManager_HashInt(uint32_t key, uint32_t bucketCount)
{
	return key % (bucketCount + 1);
}

/*
 * 0x004639D4 - ResManager_HashStr
 *
 * Case-insensitive ELF/PJW hash of str, modulo (bucketCount + 1).
 */
uint32_t
ResManager_HashStr(const char *str, uint32_t bucketCount)
{
	uint32_t h = 0, g;

	while (*str) {
		h = (h << 4) + (uint8_t)tolower((unsigned char)*str);
		g = h & 0xF0000000;
		if (g) {
			h ^= g >> 24;
			h ^= g;
		}
		str++;
	}
	return h % (bucketCount + 1);
}

/*
 * 0x00463A5B - ResManager_HashStrA
 *
 * CString-taking wrapper around ResManager_HashStr.
 */
uint32_t
ResManager_HashStrA(CString *str, int bucketCount)
{
	return ResManager_HashStr(CString_GetBuffer(str), bucketCount);
}

/*
 * 0x00464380 - CResManager::~CResManager (hint variant)
 *
 * Delegates to CResManager_Clear_Hint.
 */
__attribute__((unused)) void
CResManager_Destructor_Hint(CResManager *this)
{
	CResManager_Clear_Hint(this);
}
/*
 * 0x004644F0 - CResManager::BeginIterInternal (hint variant)
 *
 * Hashes an int key, seeds a CSearchCtx at that bucket's head, and
 * delegates to SearchBucketHint. Returns an empty ctx when the bucket
 * is empty.
 */
__attribute__((unused)) void *
CResManager_BeginIterInternalHint(CResManager *rm, CSearchCtx *output, uint32_t *key, int direction)
{
	CSearchCtx localCtx;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	bucket = ResManager_HashInt(*key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CSearchCtx_SetEntity(&localCtx, 1);
	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));

	CResManager_SearchBucketHint(rm, output, key, &localCtx, direction);
	return output;
}

/*
 * 0x00464680 - CResManager::SearchBucket (hint variant)
 *
 * Walks the bucket starting at startCtx looking for a key matching
 * *key. Writes the matching ctx to *output, or an empty ctx when the
 * bucket is exhausted.
 */
static void *
CResManager_SearchBucketHint(CResManager *rm, CSearchCtx *output, uint32_t *key, CSearchCtx *startCtx, int direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;
	uint32_t *keyData;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(startCtx)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CSearchCtx_Add(&localCtx, startCtx);

	while (CSearchCtx_Find(&localCtx)) {
		keyData = (uint32_t *)CResManager_GetKeyAtPos(rm, &localCtx);
		if (*keyData == *key) {
			CResManager_CreateBucket(output, &localCtx);
			return output;
		}
		CResManager_NextEntryHint(rm, &nextCtx, &localCtx, direction);
		CSearchCtx_Add(&localCtx, &nextCtx);
	}

	CSearchCtx_SetEntity(&localCtx, 0);
	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x00464730 - CResManager::BeginSearch_Hint
 *
 * Finds the first non-empty bucket at or after startBucket (direction
 * 1 ascending, 0 descending) and writes its head ctx to *output.
 */
void
CResManager_BeginSearchHint(CResManager *rm, CSearchCtx *output, int startBucket, int direction)
{
	CSearchCtx localCtx;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (startBucket >= 0x42) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	bucket = startBucket;
	for (;;) {
		if (rm->keys[bucket] != NULL) {
			CSearchCtx_SetBucket(&localCtx, bucket);
			CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));
			CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));
			CSearchCtx_SetEntity(&localCtx, 1);
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (bucket == 0 && direction == 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		if (bucket == 0x41 && direction != 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (direction == 1)
			bucket += 1;
		else
			bucket -= 1;
	}
}

/*
 * 0x00464830 - CResManager::Clear (hint variant)
 *
 * Releases every bucket's key and value list.
 */
static void
CResManager_Clear_Hint(CResManager *this)
{
	CResManager *rm = this;
	uint32_t i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			ScalarDestructor_KeyNode(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResListValNode_ScalarDelete_Hint(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x00464910 - CResManager::NextEntry (hint variant B)
 *
 * Advances current to the next key/val pair in the same bucket, or
 * falls through to BeginSearchHint on the adjacent bucket.
 */
void *
CResManager_NextEntry_Hint(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	uint32_t bucket;
	int nextBucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	bucket = CSearchCtx_GetBucket(current);
	CSearchCtx_SetBucket(&localCtx, bucket);

	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)CSearchCtx_GetKeyNode(current), direction);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)nextKey);

	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), direction);
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)nextVal);

	if (CResList_IsValid(rm->keys[CSearchCtx_GetBucket(&localCtx)], (CResListNode *)CSearchCtx_GetKeyNode(&localCtx))) {
		CSearchCtx_SetEntity(&localCtx, 1);
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	bucket = CSearchCtx_GetBucket(&localCtx);

	if (bucket == 0 && direction == 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}
	if (bucket == 0x41 && direction != 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	nextBucket = (int)bucket + (direction ? 1 : -1);

	CResManager_BeginSearchHint(rm, output, nextBucket, direction);
	return output;
}

/*
 * 0x00464A60 - CResManager::EraseEntry (hint variant)
 *
 * Removes the key/val pair at current, freeing the bucket's lists if
 * they become empty, and writes the successor ctx to *output.
 */
void *
CResManager_EraseEntry_Hint(CResManager *rm, CSearchCtx *output, CSearchCtx *current, void *outErased, int direction)
{
	CSearchCtx localCtx, nextCtx;
	uint32_t bucket;
	CResList *ptr;

	CSearchCtx_Constructor(&localCtx);

	*(uint32_t *)outErased = 0;

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CResManager_NextEntry_Hint(rm, &nextCtx, current, direction);
	CSearchCtx_Add(&localCtx, &nextCtx);

	bucket = CSearchCtx_GetBucket(current);
	CResList_RemoveKeyNode(rm->keys[bucket], current->keyNode, direction);

	bucket = CSearchCtx_GetBucket(current);
	CResList_Erase_ResManager(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), (void **)outErased, direction);

	bucket = CSearchCtx_GetBucket(current);
	if (rm->keys[bucket]->count == 0) {
		bucket = CSearchCtx_GetBucket(current);
		ptr = rm->keys[bucket];
		if (ptr != NULL)
			ScalarDestructor_KeyNode(ptr, 1);

		bucket = CSearchCtx_GetBucket(current);
		ptr = rm->vals[bucket];
		if (ptr != NULL)
			CResListValNode_ScalarDelete_Hint(ptr, 1);

		bucket = CSearchCtx_GetBucket(current);
		rm->keys[bucket] = NULL;
		bucket = CSearchCtx_GetBucket(current);
		rm->vals[bucket] = NULL;
	}

	rm->count--;
	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x00464BE0 - CResList key list insert
 *
 * Appends a new key node carrying data.
 */
CResListNode *
CResList_KeyInsert(CResList *list, void *data)
{
	CResListNode *node;

	node = CResList_AllocTailNodeHintVal(list);
	if (node != NULL)
		CResListNode_SetDataInt(node, (uint32_t *)data);
	return node;
}

/*
 * 0x00464C10 - CResList::InsertOrSetData (hint variant)
 *
 * Appends a new hint-key node carrying data.
 */
void *
CResList_InsertOrSetDataHint(CResList *list, void *data)
{
	CResListNode *node;

	node = CResList_AllocTailNodeHintKey(list);
	if (node != NULL)
		CResListNode_SetDataHint(node, data);
	return node;
}

/*
 * 0x00464C40 - Scalar deleting destructor for CResList key nodes
 *
 * Runs the hint-key list destructor and frees the list when flag & 1.
 */
void *
ScalarDestructor_KeyNode(CResList *this, int flag)
{
	CResList_Destructor_HintKey(this);
	if (flag & 1)
		free(this);
	return NULL;
}

/*
 * 0x00464C70 - CResManager val list ScalarDelete (hint val variant)
 *
 * Scalar deleting destructor.
 */
__attribute__((unused)) void *
CResListValNode_ScalarDelete_Hint(CResList *this, int flags)
{
	CResList_ValNodeDestructor_Hint(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x00464CA0 - CResManager::NextEntry (hint variant)
 *
 * Advances current to the next key/val pair in its bucket, or to the
 * next non-empty bucket.
 */
static void *
CResManager_NextEntryHint(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	bucket = CSearchCtx_GetBucket(current);
	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)CSearchCtx_GetKeyNode(current), direction);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)nextKey);

	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), direction);
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)nextVal);

	CSearchCtx_SetBucket(&localCtx, CSearchCtx_GetBucket(current));

	CSearchCtx_SetEntity(&localCtx, CResList_IsValid(rm->keys[CSearchCtx_GetBucket(&localCtx)], (CResListNode *)CSearchCtx_GetKeyNode(&localCtx)));

	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x00464D80 - CResList::AllocTailNode (hint val variant)
 *
 * Recycles a tail slot, or allocates the first node when empty.
 */
CResListNode *
CResList_AllocTailNodeHintVal(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_PushFront_ResManager(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x00464E30 - Remove key node from keys list
 *
 * Erases keyNode and frees its owned data. Returns the successor.
 */
void *
CResList_RemoveKeyNode(CResList *list, uintptr_t keyNode, uint32_t direction)
{
	void *outData;
	CResListNode *result;

	result = CResList_Erase_ResManager2(list, (CResListNode *)keyNode, &outData, direction);
	if (outData != NULL)
		free(outData);
	return result;
}

/*
 * 0x00464E80 - CResList::~CResList (hint key variant)
 *
 * Destructor for the hint-key list: calls CResList_ClearAll_HintKey.
 */
void
CResList_Destructor_HintKey(CResList *this)
{
	CResList_ClearAll_HintKey(this);
}

/*
 * 0x00464EA0 - CResList::AllocTailNode (hint key variant)
 *
 * Recycles a tail slot, or allocates the first node when empty.
 */
static CResListNode *
CResList_AllocTailNodeHintKey(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_PushFront_ResManager2(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x00464F50 - CResList::Erase (hint/CResManager variant)
 *
 * Unlinks node, frees its data slot, and returns the successor
 * (direction=1) or predecessor (direction=0).
 */
static CResListNode *
CResList_Erase_ResManager(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListValNode_ScalarDelete_HintVar(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x00465020 - CResList::~CResList (hint val variant)
 *
 * Destructor for the hint-val list: calls CResList_ClearAll_HintVal.
 */
void
CResList_ValNodeDestructor_Hint(CResList *this)
{
	CResList_ClearAll_HintVal(this);
}

/*
 * 0x00465040 - CResListNode::SetData (hint variant)
 *
 * Replaces node->data with the new pointer, releasing the old
 * CHintItem via its scalar deleting destructor when present.
 */
static void
CResListNode_SetDataHint(CResListNode *node, void *data)
{
	void *oldData;

	if (node->data != NULL) {
		oldData = node->data;
		if (oldData != NULL)
			CHintItem_ScalarDelete(oldData, 1);
		node->data = NULL;
	}
	node->data = data;
}

/*
 * 0x004650A0 - CResManager val list ScalarDelete (hint variant)
 *
 * Scalar deleting destructor.
 */
static void *
CResListValNode_ScalarDelete_HintVar(CResListNode *this, int flags)
{
	CResList_ValNodeDestructor_HintVariant(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004650D0 - CResList::InsertAfter (hint val variant)
 *
 * Allocates a new CResListNode and inserts it after position.
 */
static CResListNode *
CResList_PushFront_ResManager(CResList *list, CResListNode *position)
{
	CResListNode *newNode, *next;
	void *raw;

	if (position == NULL)
		return NULL;

	next = CResList_Begin((CResList *)position);

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	CResListNode_SetNext(newNode, next);
	CResListNode_SetPrev(newNode, position);
	CResListNode_SetNext(position, newNode);

	if (next != NULL) {
		CResListNode_SetPrev(next, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x004651C0 - CResList::Erase (hint key variant, 200 bytes)
 *
 * Unlinks node, extracts its data into *outData, destroys the node,
 * decrements count, and returns the next or prev neighbor per direction.
 */
static CResListNode *
CResList_Erase_ResManager2(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNode_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x00465290 - CResList clear-all for hint keys
 *
 * Iterates and frees all key list nodes. Called by CResList_Destructor_HintKey.
 */
static void
CResList_ClearAll_HintKey(CResList *this)
{
	CResList *list = this;
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_RemoveKeyNode(list, (uintptr_t)node, 1);
	}
}

/*
 * 0x004652D0 - CResList::InsertAfter (hint key variant)
 *
 * Allocates a new CResListNode and inserts it after position.
 */
static CResListNode *
CResList_PushFront_ResManager2(CResList *list, CResListNode *position)
{
	CResListNode *newNode, *next;
	void *raw;

	if (position == NULL)
		return NULL;

	next = CResList_Begin((CResList *)position);

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	CResListNode_SetNext(newNode, next);
	CResListNode_SetPrev(newNode, position);
	CResListNode_SetNext(position, newNode);

	if (next != NULL) {
		CResListNode_SetPrev(next, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x004653C0 - CResList clear-all for hint vals
 *
 * Iterates and frees all value list nodes. Called by CResList_ValNodeDestructor_Hint.
 */
static void
CResList_ClearAll_HintVal(CResList *this)
{
	CResList *list = this;
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_EraseAndFree_Hint(list, node, 1);
	}
}

/*
 * 0x00465400 - CResList val node dtor (hint variant, 75 bytes)
 *
 * Destroys the node's CHintItem payload if present.
 */
static void
CResList_ValNodeDestructor_HintVariant(CResListNode *this)
{
	CResListNode *node = this;
	void *data;
	void *tmp;

	if (node->data != NULL) {
		data = node->data;
		tmp = data;
		if (tmp != NULL)
			CHintItem_ScalarDelete(tmp, 1);
		node->data = NULL;
	}
}

/*
 * 0x00465450 - CResListNode::~CResListNode (scalar deleting destructor)
 *
 * Destroys the node and frees it when flags&1.
 */
CResListNode *
CResListNode_ScalarDelete(CResListNode *node, int flags)
{
	CResListNode_FreeData(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x00465480 - CResList::EraseAndFree (hint/CResManager variant, 87 bytes)
 *
 * Erases node from the list and destroys its extracted CHintItem data.
 */
static CResListNode *
CResList_EraseAndFree_Hint(CResList *list, CResListNode *node, int direction)
{
	void *outData = NULL;
	CResListNode *result;

	result = CResList_Erase_ResManager(list, node, &outData, direction);

	if (outData != NULL) {
		CHintItem_ScalarDelete(outData, 1);
	}

	return result;
}

/*
 * 0x00466C80 - CResList::Next
 *
 * Returns node->next, or NULL if node is NULL.
 */
CResListNode *
CResList_Next(CResList *list, CResListNode *node)
{
	USED(list);
	if (node == NULL)
		return NULL;
	return CResList_Begin((CResList *)node);
}

/*
 * 0x00466CA0 - CResList::ClearInternal (MagicItemList variant)
 *
 * Iterates from head, freeing every node via EraseAndFree_MagicStr.
 */
void
CResList_ClearInternal_MagicItemList_rb(CResList *this)
{
	CResListNode *node;

	node = CResList_Begin(this);
	while (CResList_IsValid(this, node)) {
		node = CResList_EraseAndFree_MagicStr(this, node, 1);
	}
}

/*
 * 0x00466CA0 - CResList::ClearInternal (MagicItemList variant, 59 bytes)
 *
 * Iterates from head, freeing every node via EraseAndFree_MagicStr.
 */
static void
CResList_ClearInternal_MagicItemList(CResList *this)
{
	CResListNode *node;

	node = CResList_Begin(this);
	while (CResList_IsValid(this, node)) {
		node = (CResListNode *)CResList_EraseAndFree_MagicStr(this, node, 1);
	}
}

/*
 * 0x00466CE0 - CResList::~CResList (MagicItemList variant, 19 bytes)
 *
 * Destroys the list by freeing every node via ClearInternal_MagicItemList.
 */
static __attribute__((unused)) void
CResList_Destructor_MagicItemList(CResList *this)
{
	CResList_ClearInternal_MagicItemList(this);
}

/*
 * 0x00466DD0 - CResList::EraseAndFree (MagicItemFactory CString variant, 87 bytes)
 *
 * Erases node from the list and destroys its extracted CString data.
 */
CResListNode *
CResList_EraseAndFree_MagicStr(CResList *list, CResListNode *node, int direction)
{
	void *outData = NULL;
	CResListNode *result;

	result = CResList_Erase_Region(list, node, &outData, direction);

	if (outData != NULL) {
		CString_ScalarDelete_MF(outData, 1);
	}

	return result;
}

/*
 * 0x00466E80 - CResList::GetHeadIfNotNull
 *
 * Safe head-of-list accessor: returns list->data or NULL when list is NULL.
 */
void *
CResList_GetHeadIfNotNull(CResList *this, CResListNode *list)
{
	USED(this);
	if (list != NULL)
		return list->data;
	return NULL;
}

/*
 * 0x00466ED0 - CResList::Erase (region/MagicItemFactory variant, 200 bytes)
 *
 * Unlinks node, extracts its data into *outData, destroys the node via
 * SmartPtr_CString_ScalarDelete, and returns the next/prev neighbor.
 */
static CResListNode *
CResList_Erase_Region(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		SmartPtr_CString_ScalarDelete((CSmartPtr *)node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004730A0 - SearchCtx_IsEqual
 *
 * Returns 1 when a and b have the same Find() result.
 */
int
SearchCtx_IsEqual(CSearchCtx *a, CSearchCtx *b)
{
	int va = CSearchCtx_Find(a);
	int vb = CSearchCtx_Find(b);
	return va == vb;
}

/*
 * 0x00473EB0 - CResList::RemoveAndFree
 *
 * Removes node from the list and frees its extracted data. Returns the
 * next node when direction=1, prev when direction=0.
 */
CResListNode *
CResList_RemoveAndFree(CResList *list, CResListNode *node, int direction)
{
	void *outData = NULL;
	CResListNode *result;

	result = CResList_RemoveNode_Bin(list, node, &outData, direction);
	if (outData != NULL)
		free(outData);
	return result;
}

/*
 * 0x00473F00 - CResList::~CResList
 *
 * Destroys the list by freeing every node via CResList_EraseAll.
 */
void
CResList_Destructor(CResList *list)
{
	CResList_EraseAll(list);
}

/*
 * 0x00473F20 - CResList::InsertBack
 *
 * Appends a new node to the list and copies *dataPtr into its data cell.
 */
void
CResList_InsertBack(CResList *list, void *dataPtr)
{
	CResListNode *node;

	node = CResList_AllocAndAppend(list);
	if (node != NULL)
		CResListNode_SetDataInt(node, dataPtr);
}

/*
 * 0x00473F50 - CResList::AllocAndAppend
 *
 * If tail exists, inserts a new node after it. Otherwise allocates a
 * fresh CResListNode and sets it as sole head+tail.
 */
static CResListNode *
CResList_AllocAndAppend(CResList *list)
{
	CResListNode *node;
	void *mem;

	if (list->tail != NULL) {
		return CResList_InsertAfterNode(list, list->tail);
	}

	mem = malloc(sizeof(CResListNode));
	if (mem != NULL)
		node = CResListNode_Constructor_bin(mem);
	else
		node = NULL;

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x00474000 - CResList::RemoveNode
 *
 * Unlinks node, extracts its data into *outData, destroys the node,
 * decrements count, and returns the next/prev neighbor per direction.
 */
static CResListNode *
CResList_RemoveNode_Bin(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next, *tmp;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	// CResList_GetTail on node: returns node->prev (shared +4 offset)
	prev = CResList_GetTail((CResList *)node);
	// CResList_Begin on node: returns node->next (shared +0 offset)
	next = CResList_Begin((CResList *)node);

	// Unlink: fix prev->next or head
	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	// Unlink: fix next->prev or tail
	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	// Extract data (swap node->data with NULL)
	*outData = CResListNode_SwapData(node, NULL);

	// Delete node via scalar deleting destructor (flags=1: dtor + free)
	tmp = node;
	if (tmp != NULL)
		CResListNode_ScalarDelete(tmp, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004740D0 - CResList::EraseAll
 *
 * Iterates and frees every node via CResList_RemoveAndFree.
 */
void
CResList_EraseAll(CResList *list)
{
	CResListNode *iter;

	iter = CResList_Begin(list);
	while (CResList_IsValid(list, iter))
		iter = CResList_RemoveAndFree(list, iter, 1);
}

/*
 * 0x00474110 - CResList::DirectionNext
 *
 * Returns CResList_Next on direction==1, else CResList_Prev.
 */
CResListNode *
CResList_DirectionNext(CResList *list, CResListNode *node, int direction)
{
	if (direction == 1)
		return CResList_Next(list, node);
	return CResList_Prev(list, node);
}

/*
 * 0x00474140 - CResList::AllocAndInsertAfter
 *
 * Allocates a new node and splices it in after afterNode.
 */
static CResListNode *
CResList_InsertAfterNode(CResList *list, CResListNode *afterNode)
{
	CResListNode *newNode;
	CResListNode *nextNode;
	void *raw;

	if (afterNode == NULL)
		return NULL;

	nextNode = CResList_Begin((CResList *)afterNode);

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	CResListNode_SetNext(newNode, nextNode);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (nextNode != NULL) {
		CResListNode_SetPrev(nextNode, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count += 1;
	return newNode;
}

/*
 * 0x00474468 - CResList::~CResList (empty)
 *
 * No-op. Paired with CResList_ConstructorEmpty at 0x00474473.
 */
void
CResList_DestructorEmpty(CResList *this)
{
	USED(this);
}

/*
 * 0x00474473 - CResList::CResList (empty)
 *
 * No-op. Paired with CResList_DestructorEmpty at 0x00474468.
 */
static __attribute__((unused)) void
CResList_ConstructorEmpty(CResList *this)
{
	USED(this);
}

/*
 * 0x0047A4F0 - CResManager::Clear wrapper (variant A)
 *
 * Delegates to CResManager_ClearMultiA.
 */
__attribute__((unused)) void
CResManager_ClearMultiA_Thunk(CResManager *rm)
{
	CResManager_ClearMultiA(rm);
}

/*
 * 0x0047A510 - CResManager::Clear template (variant A)
 *
 * Frees every key/val list across 66 buckets and resets count.
 */
static void
CResManager_ClearMultiA(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDelete_MultiA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDelete_MultiB(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x0047A5F0 - CResManager::FindOrInsert template (variant A)
 *
 * Hashes *keyPtr and ensures the bucket exists. If flags==1 and key already
 * present, returns 0; otherwise prepends a heap copy of *keyPtr and valuePtr.
 */
int
CResManager_FindOrInsertMultiA(CResManager *rm, uint32_t *keyPtr, void *valuePtr)
{
	uint32_t bucket;
	CResList *keyList, *valList;

	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		if (CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1) != NULL)
			return 0;
	}

	CResList_PrependInt_A(rm->keys[bucket], keyPtr);
	CResList_PrependData_B(rm->vals[bucket], valuePtr);
	rm->count++;
	return 1;
}

/*
 * 0x0047A740 - CResManager::FindByKey (variant A)
 *
 * Hashes *keyPtr, seeds output at the bucket head, and delegates to
 * CResManager_FindByKey_B. Writes an empty ctx on an empty bucket.
 */
__attribute__((unused)) CSearchCtx *
CResManager_FindByKey_A(CResManager *this, CSearchCtx *output, uint32_t *keyPtr, int direction)
{
	CSearchCtx ctx;
	uint32_t bucket;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (this->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}

	CSearchCtx_SetEntity(&ctx, 1);
	CSearchCtx_SetBucket(&ctx, bucket);

	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionBegin(this->keys[bucket], direction));
	CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionBegin(this->vals[bucket], direction));

	CResManager_FindByKey_B(this, output, keyPtr, &ctx, direction);
	return output;
}

/*
 * 0x0047A800 - CResManager::GetResult
 *
 * Returns the value data at the ctx position.
 */
void *
CResManager_GetResultCtx(CResManager *rm, CSearchCtx *ctx)
{
	uintptr_t valNode;
	uint32_t bucket;

	CSearchCtx_Find(ctx);
	valNode = CSearchCtx_GetValNode(ctx);
	bucket = CSearchCtx_GetBucket(ctx);
	return CResList_GetData(rm->vals[bucket], (CResListNode *)valNode);
}

/*
 * 0x0047A800 - CResManager::GetResult
 *
 * Returns the value data at the ctx position.
 */
void *
CResManager_GetResult(CResManager *rm, CSearchCtx *ctx)
{
	return CResManager_GetResultCtx(rm, ctx);
}

/*
 * 0x0047A840 - CResManager::CreateOrFind_R
 *
 * Erases the entry at current (freeing any owned CMultiDef) and writes
 * the successor ctx to *output.
 */
static __attribute__((unused)) CSearchCtx *
CResManager_CreateOrFind_R(CResManager *this, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx ctx;
	void *erasedData = NULL;
	void *result;
	void *temp;

	CSearchCtx_Constructor(&ctx);
	result = CResManager_EraseMultiA(this, output, current, &erasedData, direction);
	CSearchCtx_Add(&ctx, result);
	if (erasedData != NULL) {
		temp = erasedData;
		if (temp != NULL)
			CMultiDef_ScalarDelete(temp, 1);
	}
	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0047A8C0 - CResManager::BeginIter_MultiA wrapper
 *
 * Starts a forward iteration from bucket 0 via BeginIterInternalMultiA.
 */
CSearchCtx *
CResManager_BeginIter_MultiA(CResManager *this, CSearchCtx *output)
{
	CResManager_BeginIterInternalMultiA(this, output, 0, 1);
	return output;
}

/*
 * 0x0047A8E0 - CResManager::NextIter_MultiA wrapper
 *
 * Advances the iterator forward via NextIterInternalMultiA.
 */
CSearchCtx *
CResManager_NextIter_MultiA(CResManager *this, CSearchCtx *output, CSearchCtx *current)
{
	CResManager_NextIterInternalMultiA(this, output, current, 1);
	return output;
}

/*
 * 0x0047A910 - CResManager::Clear wrapper (variant B)
 *
 * Delegates to CResManager_ClearMultiB.
 */
__attribute__((unused)) void
CResManager_ClearMultiB_Thunk(CResManager *rm)
{
	CResManager_ClearMultiB(rm);
}

/*
 * 0x0047A930 - CResManager::FindOrInsert template (variant B)
 *
 * Hashes *keyPtr, ensures the bucket exists, and inserts *keyPtr in order
 * with valuePtr prepended to the value list unless dedup mode finds an
 * existing match.
 */
static __attribute__((unused)) int
CResManager_FindOrInsertMultiB(CResManager *rm, uint32_t *keyPtr, void *valuePtr)
{
	uint32_t bucket;
	CResList *keyList, *valList;

	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		if (CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1) != NULL)
			return 0;
	}

	CResList_KeyInsert(rm->keys[bucket], keyPtr);
	CResList_PrependData_C(rm->vals[bucket], valuePtr);
	rm->count++;
	return 1;
}

/*
 * 0x0047AA80 - CResManager::FindOrInsert_B
 *
 * Seeds a ctx at the bucket head for key and delegates to FindByKey_B2.
 */
CSearchCtx *
CResManager_FindOrInsert_B(CResManager *this, CSearchCtx *output, void *keyPtr, int direction)
{
	CSearchCtx ctx;
	uint32_t bucket;

	CSearchCtx_Constructor(&ctx);
	bucket = ResManager_HashInt(*(uint32_t *)keyPtr, 0x41);
	if (this->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}
	CSearchCtx_SetEntity(&ctx, 1);
	CSearchCtx_SetBucket(&ctx, bucket);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionBegin(this->keys[bucket], direction));
	CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionBegin(this->vals[bucket], direction));
	CResManager_FindByKey_B2(this, output, keyPtr, &ctx, direction);
	return output;
}

/*
 * 0x0047AB40 - CResManager::Erase template (variant B, 372 bytes)
 *
 * Erases the key/val pair at current, dropping the owning bucket lists
 * when they become empty, and writes the successor ctx to *output.
 */
void *
CResManager_EraseMultiB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, void **outData, int direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;
	uint32_t bucket;
	CResList *ptr;

	CSearchCtx_Constructor(&localCtx);
	*outData = NULL;

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CResManager_NextIterInternalMultiB(rm, &nextCtx, current, direction);
	CSearchCtx_Add(&localCtx, &nextCtx);

	// Erase key node via CResList_RemoveKeyNode (0x00464E30)
	bucket = CSearchCtx_GetBucket(current);
	CResList_RemoveKeyNode_Multi(rm->keys[bucket], (CResListNode *)current->keyNode, direction);

	// Erase val node via CResList_Erase_MultiVal (0x0047BD80)
	bucket = CSearchCtx_GetBucket(current);
	CResList_Erase_MultiVal(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), outData, direction);

	// Check if bucket is empty (CResList_GetHead returns count at +8)
	bucket = CSearchCtx_GetBucket(current);
	if (rm->keys[bucket]->count == 0) {
		// ScalarDestructor_KeyNode(keys[bucket], 1) - 0x00464C40
		bucket = CSearchCtx_GetBucket(current);
		ptr = rm->keys[bucket];
		if (ptr != NULL) {
			CResList_ScalarDelete_MultiA(ptr, 1);
		}

		// CResList_ScalarDelete_MultiVal(vals[bucket], 1) - 0x0047B490
		bucket = CSearchCtx_GetBucket(current);
		ptr = rm->vals[bucket];
		if (ptr != NULL) {
			CResList_ScalarDelete_MultiVal(ptr, 1);
		}

		bucket = CSearchCtx_GetBucket(current);
		rm->keys[bucket] = NULL;
		bucket = CSearchCtx_GetBucket(current);
		rm->vals[bucket] = NULL;
	}

	rm->count--;
	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x0047B580 - CResManager::FindByKey_B
 *
 * Walks searchCtx's key/val pairs looking for a matching int key.
 */
static CSearchCtx *
CResManager_FindByKey_B(CResManager *this, CSearchCtx *output, void *keyPtr, CSearchCtx *searchCtx, int direction)
{
	CSearchCtx ctx;

	CSearchCtx_Constructor(&ctx);
	if (!CSearchCtx_Find(searchCtx)) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}
	CSearchCtx_Add(&ctx, searchCtx);
	for (;;) {
		if (!CSearchCtx_Find(&ctx))
			break;
		int *keyAtPos = CResManager_GetKeyAtPos(this, &ctx);
		if (*(int *)keyAtPos == *(int *)keyPtr) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
		CSearchCtx *next = CResManager_NextIter_KeyVal_A(this, &ctx, output, direction);
		CSearchCtx_Add(&ctx, next);
	}
	CSearchCtx_SetEntity(&ctx, 0);
	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0047B630 - CResManager::BeginIterInternal template (variant A)
 *
 * Scans from startBucket (in direction) for the first non-empty bucket and
 * seeds *output with its first key/val nodes.
 */
static void
CResManager_BeginIterInternalMultiA(CResManager *rm, CSearchCtx *output, int startBucket, int direction)
{
	CSearchCtx localCtx;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (startBucket >= 0x42) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	bucket = startBucket;
	for (;;) {
		if (rm->keys[bucket] != NULL) {
			localCtx.bucket = bucket;
			localCtx.keyNode = (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction);
			localCtx.valNode = (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction);
			localCtx.entity = 1;
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (bucket == 0 && direction == 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		if (bucket == 0x41 && direction != 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (direction == 1)
			bucket += 1;
		else
			bucket -= 1;
	}
}

/*
 * 0x0047B730 - CResManager::NextIterInternal template (variant A)
 *
 * Advances current's key/val nodes one step. When the bucket runs out, falls
 * back to CResManager_BeginIterInternalMultiA for the next non-empty bucket.
 */
static void *
CResManager_NextIterInternalMultiA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	// Copy bucket, advance key and val
	bucket = CSearchCtx_GetBucket(current);
	localCtx.bucket = bucket;

	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, direction);
	localCtx.keyNode = (uintptr_t)nextKey;

	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), direction);
	localCtx.valNode = (uintptr_t)nextVal;

	// Check if new key node is valid
	if (CResList_IsValid(rm->keys[localCtx.bucket], (CResListNode *)localCtx.keyNode)) {
		localCtx.entity = 1;
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	// Scan to next non-empty bucket
	bucket = CSearchCtx_GetBucket(&localCtx);
	if (bucket == 0 && direction == 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}
	if (bucket == 0x41 && direction != 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	if (direction == 1)
		bucket += 1;
	else
		bucket -= 1;
	CResManager_BeginIterInternalMultiA(rm, output, bucket, direction);
	return output;
}

/*
 * 0x0047B880 - CResManager::Erase template (variant A)
 *
 * Erases the key/val pair at current (passing the removed value via
 * *outData), collapses an emptied bucket, and writes the successor ctx
 * to *output.
 */
static void *
CResManager_EraseMultiA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, void **outData, int direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;
	uint32_t bucket;
	CResList *ptr;

	CSearchCtx_Constructor(&localCtx);
	*outData = NULL;

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CResManager_NextIterInternalMultiA(rm, &nextCtx, current, direction);
	CSearchCtx_Add(&localCtx, &nextCtx);

	// Erase key node via CResList_EraseAndFree_MultiA (0x0047C1B0)
	bucket = CSearchCtx_GetBucket(current);
	CResList_EraseAndFree_MultiA(rm->keys[bucket], (CResListNode *)current->keyNode, direction);

	// Erase val node via CResList_Erase_MultiKey (0x0047C2F0)
	bucket = CSearchCtx_GetBucket(current);
	CResList_Erase_MultiKey(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), outData, direction);

	// Check if bucket is empty (CResList_GetHead returns count at +8)
	bucket = CSearchCtx_GetBucket(current);
	if (rm->keys[bucket]->count == 0) {
		// CResList_ScalarDelete_MultiA(keys[bucket], 1)
		bucket = CSearchCtx_GetBucket(current);
		ptr = rm->keys[bucket];
		if (ptr != NULL)
			CResList_ScalarDelete_MultiA(ptr, 1);

		// CResList_ScalarDelete_MultiB(vals[bucket], 1)
		bucket = CSearchCtx_GetBucket(current);
		ptr = rm->vals[bucket];
		if (ptr != NULL)
			CResList_ScalarDelete_MultiB(ptr, 1);

		bucket = CSearchCtx_GetBucket(current);
		rm->keys[bucket] = NULL;
		bucket = CSearchCtx_GetBucket(current);
		rm->vals[bucket] = NULL;
	}

	rm->count--;
	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x0047BA00 - CResList::~CResList (multi key variant A)
 *
 * Delegates to CResList_ClearAll_MultiA.
 */
void
CResList_Destructor_MultiA(CResList *list)
{
	CResList_ClearAll_MultiA(list);
}

/*
 * 0x0047BA20 - CResList::PrependInt_A
 *
 * Allocates a node via CResList_BeginIter_MultiA and stores *valuePtr in it.
 */
static void *
CResList_PrependInt_A(CResList *this, uint32_t *valuePtr)
{
	CResListNode *node;

	node = CResList_BeginIter_MultiA(this);
	if (node != NULL)
		CResListNode_SetDataInt(node, valuePtr);
	return node;
}

/*
 * 0x0047BA50 - CResList::~CResList template (multi key variant B, 19 bytes)
 *
 * Destroys the list by freeing every node via ClearAll_MultiB.
 */
void
CResList_Destructor_MultiB(CResList *list)
{
	CResList_ClearAll_MultiB(list);
}

/*
 * 0x0047BA70 - CResList::PrependData_B
 *
 * Opens a new leading node on this list and stores data into it.
 */
static void *
CResList_PrependData_B(CResList *this, void *data)
{
	void *node;

	node = CResList_BeginIter_MultiB(this);
	if (node != NULL)
		CResList_ReplaceData_B((CResListNode *)node, data);
	return node;
}

/*
 * 0x0047BAA0 - CResManager::FindByKey_B2
 *
 * Int-keyed lookup via NextIter_KeyVal_B. Returns the bucket position for the
 * matching key, or an empty ctx if not found.
 */
static CSearchCtx *
CResManager_FindByKey_B2(CResManager *this, CSearchCtx *output, void *keyPtr, CSearchCtx *searchCtx, int direction)
{
	CSearchCtx ctx;

	CSearchCtx_Constructor(&ctx);
	if (!CSearchCtx_Find(searchCtx)) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}
	CSearchCtx_Add(&ctx, searchCtx);
	for (;;) {
		if (!CSearchCtx_Find(&ctx))
			break;
		int *keyAtPos = CResManager_GetKeyAtPos(this, &ctx);
		if (*(int *)keyAtPos == *(int *)keyPtr) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
		CSearchCtx *next = CResManager_NextIter_KeyVal_B(this, &ctx, output, direction);
		CSearchCtx_Add(&ctx, next);
	}
	CSearchCtx_SetEntity(&ctx, 0);
	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0047BB50 - CResManager::Clear (variant B)
 *
 * Releases all 66 key/value buckets using the MultiVal scalar delete.
 */
static void
CResManager_ClearMultiB(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			ScalarDestructor_KeyNode(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDelete_MultiVal(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x0047BC30 - CResManager::NextIterInternal (variant B)
 *
 * Advances key and val iterators to the next matched pair within the current
 * bucket, or seeks into the next/prev bucket via BeginIterInternalMultiB.
 */
static void *
CResManager_NextIterInternalMultiB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	bucket = CSearchCtx_GetBucket(current);
	localCtx.bucket = bucket;

	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, direction);
	localCtx.keyNode = (uintptr_t)nextKey;

	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), direction);
	localCtx.valNode = (uintptr_t)nextVal;

	if (CResList_IsValid(rm->keys[localCtx.bucket], (CResListNode *)localCtx.keyNode)) {
		localCtx.entity = 1;
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	bucket = CSearchCtx_GetBucket(&localCtx);
	if (bucket == 0 && direction == 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}
	if (bucket == 0x41 && direction != 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	if (direction == 1)
		bucket += 1;
	else
		bucket -= 1;
	CResManager_BeginIterInternalMultiB(rm, output, bucket, direction);
	return output;
}

/*
 * 0x0047BD80 - CResList::Erase (multi value variant)
 *
 * Unlinks node, deletes its CVector smart pointer, hands the stored data back
 * to the caller, and returns the next/prev sibling per direction.
 */
static CResListNode *
CResList_Erase_MultiVal(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = node->prev;
	next = node->next;

	if (prev != NULL)
		prev->next = next;
	else
		list->head = next;

	if (next != NULL)
		next->prev = prev;
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	SmartPtr_CVector_ScalarDelete((CSmartPtr *)node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x0047BE50 - CResList::~CResList (multi value variant)
 *
 * Destructor for the multi-val list: calls CResList_ClearAll_MultiVal.
 */
void
CResList_Destructor_MultiVal(CResList *list)
{
	CResList_ClearAll_MultiVal(list);
}

/*
 * 0x0047BE70 - CResList::PrependData_C
 *
 * Opens a new leading node on this list and stores data into it.
 */
static void *
CResList_PrependData_C(CResList *this, void *data)
{
	void *node;

	node = CResList_BeginIter_MultiC(this);
	if (node != NULL)
		CResList_ReplaceData_C((CResListNode *)node, data);
	return node;
}

/*
 * 0x0047C020 - CResManager::NextIter_KeyVal_A
 *
 * Steps key and val iterators forward/backward within the current bucket and
 * reports whether the new key node is still valid.
 */
static CSearchCtx *
CResManager_NextIter_KeyVal_A(CResManager *this, CSearchCtx *searchCtx, CSearchCtx *output, int direction)
{
	CSearchCtx ctx;
	CSearchCtx *sctx = searchCtx;

	CSearchCtx_Constructor(&ctx);
	if (!CSearchCtx_Find(sctx)) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionNext(this->keys[CSearchCtx_GetBucket(sctx)], (CResListNode *)(uintptr_t)sctx->keyNode, direction));
	CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionNext(this->vals[CSearchCtx_GetBucket(sctx)], (CResListNode *)CSearchCtx_GetValNode(sctx), direction));
	CSearchCtx_SetBucket(&ctx, CSearchCtx_GetBucket(sctx));
	CSearchCtx_SetEntity(&ctx, CResList_IsValid(this->keys[CSearchCtx_GetBucket(&ctx)], (CResListNode *)(uintptr_t)ctx.keyNode));
	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0047C100 - CResList::BeginIter (multi variant A)
 *
 * Returns the tail after ensuring there is one: pushes a new node after tail
 * if the list already has entries, otherwise creates the single node.
 */
static CResListNode *
CResList_BeginIter_MultiA(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_PushFront_MultiA(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0047C1B0 - CResList::EraseAndFree (multi variant A)
 *
 * Unlinks node via CResList_Erase_MultiC and frees the detached data.
 */
CResListNode *
CResList_EraseAndFree_MultiA(CResList *list, CResListNode *node, int direction)
{
	void *deletedPtr = NULL;
	CResListNode *result;

	result = (CResListNode *)CResList_Erase_MultiC(list, (void *)node, &deletedPtr, direction);
	if (deletedPtr != NULL)
		free(deletedPtr);
	return result;
}

/*
 * 0x0047C200 - CResList::ClearAll (multi variant A)
 *
 * Erases and frees every node in the multi-A variant list.
 */
static void
CResList_ClearAll_MultiA(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node))
		node = CResList_EraseAndFree_MultiA(list, node, 1);
}

/*
 * 0x0047C240 - CResList::BeginIter (multi variant B)
 *
 * Same as variant A but uses CResList_PushFront_MultiB.
 */
static CResListNode *
CResList_BeginIter_MultiB(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_PushFront_MultiB(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0047C2F0 - CResList::Erase (multi key variant)
 *
 * Same as CResList_Erase_MultiVal but deletes via the CMultiDef smart pointer.
 */
static CResListNode *
CResList_Erase_MultiKey(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = node->prev;
	next = node->next;

	// Unlink node from list
	if (prev != NULL)
		prev->next = next;
	else
		list->head = next;

	if (next != NULL)
		next->prev = prev;
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	SmartPtr_CMultiDef_ScalarDelete((CSmartPtr *)node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x0047C3C0 - CResList::ClearAll (multi variant B)
 *
 * Erases and frees every node in the multi-B variant list.
 */
static void
CResList_ClearAll_MultiB(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node))
		node = CResList_EraseAndFree_MultiB(list, node, 1);
}

/*
 * 0x0047C400 - CResList::ReplaceData_B
 *
 * Replaces the CMultiDef owned by this node.
 */
static void
CResList_ReplaceData_B(CResListNode *this, void *data)
{
	if (this->data != NULL) {
		CMultiDef_ScalarDelete(this->data, 1);
		this->data = NULL;
	}
	this->data = data;
}

/*
 * 0x0047C460 - CResManager::BeginIterInternal (variant B)
 *
 * Finds the first populated bucket at or after startBucket (direction==1) or
 * at or before it (direction==0), returning an iterator at that bucket's head.
 */
static void
CResManager_BeginIterInternalMultiB(CResManager *rm, CSearchCtx *output, int startBucket, int direction)
{
	CSearchCtx localCtx;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (startBucket >= 0x42) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	bucket = startBucket;
	for (;;) {
		if (rm->keys[bucket] != NULL) {
			localCtx.bucket = bucket;
			localCtx.keyNode = (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction);
			localCtx.valNode = (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction);
			localCtx.entity = 1;
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (bucket == 0 && direction == 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		if (bucket == 0x41 && direction != 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (direction == 1)
			bucket += 1;
		else
			bucket -= 1;
	}
}

/*
 * 0x0047C560 - CResManager::NextIter_KeyVal_B
 *
 * Same as CResManager_NextIter_KeyVal_A.
 */
static CSearchCtx *
CResManager_NextIter_KeyVal_B(CResManager *this, CSearchCtx *searchCtx, CSearchCtx *output, int direction)
{
	CSearchCtx ctx;
	CSearchCtx *sctx = searchCtx;

	CSearchCtx_Constructor(&ctx);
	if (!CSearchCtx_Find(sctx)) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionNext(this->keys[CSearchCtx_GetBucket(sctx)], (CResListNode *)(uintptr_t)sctx->keyNode, direction));
	CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionNext(this->vals[CSearchCtx_GetBucket(sctx)], (CResListNode *)CSearchCtx_GetValNode(sctx), direction));
	CSearchCtx_SetBucket(&ctx, CSearchCtx_GetBucket(sctx));
	CSearchCtx_SetEntity(&ctx, CResList_IsValid(this->keys[CSearchCtx_GetBucket(&ctx)], (CResListNode *)(uintptr_t)ctx.keyNode));
	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x0047C640 - CResList::BeginIter_MultiC
 *
 * Returns the tail after ensuring there is one: pushes a new node after tail
 * if the list already has entries, otherwise creates the single node.
 */
static void *
CResList_BeginIter_MultiC(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL) {
		return CResList_PushFront_MultiC(list, list->tail);
	}
	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);
	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x0047C6F0 - CResList::ClearAll (multi value variant)
 *
 * Erases and frees every node in the multi-val variant list.
 */
static void
CResList_ClearAll_MultiVal(CResList *list)
{
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node))
		node = CResList_EraseAndFree_MultiVal(list, node, 1);
}

/*
 * 0x0047C780 - CResList::ReplaceData_C
 *
 * Replaces the CVector owned by this node.
 */
static void
CResList_ReplaceData_C(CResListNode *this, void *data)
{
	if (this->data != NULL) {
		void *owned = this->data;
		if (owned != NULL)
			CVector_ScalarDelete(owned, 1);
		this->data = NULL;
	}
	this->data = data;
}

/*
 * 0x0047CBF0 - CResList::PushFront (variant A)
 *
 * Inserts a new node immediately after afterNode, fixing up head/tail if the
 * list was empty or afterNode was the tail.
 */
static CResListNode *
CResList_PushFront_MultiA(CResList *list, CResListNode *afterNode)
{
	CResListNode *oldNext;
	CResListNode *newNode;

	if (afterNode == NULL)
		return NULL;

	oldNext = afterNode->next;

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	newNode->next = oldNext;
	newNode->prev = afterNode;
	afterNode->next = newNode;

	if (oldNext != NULL) {
		oldNext->prev = newNode;
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x0047CCE0 - CResList::Erase (variant C)
 *
 * Unlinks node and scalar-deletes it with the generic node destructor.
 */
CResListNode *
CResList_Erase_MultiC(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = node->prev;
	next = node->next;

	if (prev != NULL)
		prev->next = next;
	else
		list->head = next;

	if (next != NULL)
		next->prev = prev;
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNode_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x0047CDB0 - CResList::PushFront template (variant B)
 *
 * Inserts a new node immediately after afterNode.
 */
static CResListNode *
CResList_PushFront_MultiB(CResList *list, CResListNode *afterNode)
{
	CResListNode *oldNext;
	CResListNode *newNode;

	if (afterNode == NULL)
		return NULL;

	oldNext = afterNode->next;

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	newNode->next = oldNext;
	newNode->prev = afterNode;
	afterNode->next = newNode;

	if (oldNext != NULL) {
		oldNext->prev = newNode;
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x0047CEA0 - CResList::EraseAndFree template (multi variant B)
 *
 * Unlinks node and destroys its owned CMultiDef.
 */
static CResListNode *
CResList_EraseAndFree_MultiB(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_Erase_MultiKey(list, node, &data, direction);
	if (data != NULL) {
		if (data != NULL)
			CMultiDef_ScalarDelete(data, 1);
	}
	return result;
}

/*
 * 0x0047CF50 - CResList::PushFront template (variant C)
 *
 * Inserts a new node immediately after afterNode.
 */
static CResListNode *
CResList_PushFront_MultiC(CResList *list, CResListNode *afterNode)
{
	CResListNode *oldNext;
	CResListNode *newNode;

	if (afterNode == NULL)
		return NULL;

	oldNext = afterNode->next;

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	newNode->next = oldNext;
	newNode->prev = afterNode;
	afterNode->next = newNode;

	if (oldNext != NULL) {
		oldNext->prev = newNode;
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x0047D040 - CResList::EraseAndFree template (multi value variant)
 *
 * Unlinks node and destroys its owned CVector.
 */
static CResListNode *
CResList_EraseAndFree_MultiVal(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_Erase_MultiVal(list, node, &data, direction);
	if (data != NULL) {
		if (data != NULL)
			CVector_ScalarDelete(data, 1);
	}
	return result;
}

/*
 * 0x0047D460 - CResListNode::FreeData
 *
 * Frees the node's data pointer and clears it.
 */
static void
CResListNode_FreeData(CResListNode *node)
{
	if (node->data != NULL) {
		free(node->data);
		node->data = NULL;
	}
}

/*
 * 0x0047D4A0 - CResListNode::SetPrev
 *
 * Stores prev into node->prev.
 */
void
CResListNode_SetPrev(CResListNode *node, CResListNode *prev)
{
	node->prev = prev;
}

/*
 * 0x004A6870 - CResManager int-key dtor wrapper for ByFile RM
 *
 * Delegates to CResManager_IntKeyDestructor_Clear_ByFile.
 */
void
CResManager_IntKeyDestructor_ByFile(CResManager *rm)
{
	CResManager_IntKeyDestructor_Clear_ByFile(rm);
}

/*
 * 0x004A6890 - CResManager<int,CRegion*>::Insert for ByFile RM
 *
 * Inserts (key, value) into the int-keyed map. If flags==1 and key already
 * present, returns 0 without inserting. Returns 1 on success.
 */
__attribute__((unused)) int
CResManager_Insert_ByFile(CResManager *rm, uint32_t *keyPtr, void *value)
{
	uint32_t bucket;
	CResListNode *keyNode;
	CResListNode *valNode;
	void *raw;

	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		raw = OperatorNew(sizeof(CResListNode));
		if (raw != NULL)
			keyNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
		else
			keyNode = NULL;
		rm->keys[bucket] = (CResList *)keyNode;

		raw = OperatorNew(sizeof(CResListNode));
		if (raw != NULL)
			valNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
		else
			valNode = NULL;
		rm->vals[bucket] = (CResList *)valNode;
	}

	if (rm->flags == 1) {
		if (CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1))
			return 0;
	}

	CResList_KeyInsert(rm->keys[bucket], keyPtr);
	CResList_InsertVal_ByFile(rm->vals[bucket], value);

	rm->count++;
	return 1;
}

/*
 * 0x004A69E0 - CResManager::FindByKey for ByFile RM
 *
 * Finds or creates an entry for *keyPtr and writes its ctx to *output.
 * Returns an empty ctx when dedup mode rejects an existing key.
 */
__attribute__((unused)) CSearchCtx *
CResManager_FindByKey_ByFile(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr)
{
	CSearchCtx localCtx;
	uint32_t bucket;
	CResListNode *keyNode;
	CResListNode *valNode;
	void *raw;
	CResListNode *newKeyNode;
	void *tailValNode;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		raw = OperatorNew(sizeof(CResListNode));
		if (raw != NULL)
			keyNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
		else
			keyNode = NULL;
		rm->keys[bucket] = (CResList *)keyNode;

		raw = OperatorNew(sizeof(CResListNode));
		if (raw != NULL)
			valNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
		else
			valNode = NULL;
		rm->vals[bucket] = (CResList *)valNode;
	}

	if (rm->flags == 1) {
		if (CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1)) {
			CResManager_CreateBucket(output, &localCtx);
			return output;
		}
	}

	newKeyNode = CResList_KeyInsert(rm->keys[bucket], keyPtr);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)newKeyNode);

	tailValNode = CResList_GetTailVal_ByFile((CResList *)rm->vals[bucket]);
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)tailValNode);

	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetEntity(&localCtx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x004A6B70 - CResList<int,CRegion*>::BeginIter for ByFile RM
 *
 * Seeds output at the bucket head for *keyPtr and delegates to the
 * internal begin-iter helper. Writes an empty ctx on an empty bucket.
 */
CSearchCtx *
CResList_BeginIter_ByFile(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr, uint32_t direction)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CSearchCtx_SetEntity(&localCtx, 1);
	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], (int)direction));
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], (int)direction));

	CResManager_BeginIterInternal_ByFile(rm, output, keyPtr, &localCtx, direction);
	return output;
}

/*
 * 0x004A6C30 - CResList<int,CRegion*>::GetResult+NextIter for ByFile RM
 *
 * Advances iterCtx to the next value in its bucket.
 */
__attribute__((unused)) void
CResList_GetResultNextIter_ByFile(CResManager *rm, CSearchCtx *iterCtx, uint32_t direction)
{
	uintptr_t valNode;
	uint32_t bucket;

	if (!CSearchCtx_Find(iterCtx))
		return;

	valNode = CSearchCtx_GetValNode(iterCtx);
	bucket = CSearchCtx_GetBucket(iterCtx);
	CResList_NextIter_ByFile(rm->vals[bucket], valNode, direction);
}

/*
 * 0x004A6C70 - CResList<int,CRegion*>::EraseAndFree for ByFile RM
 *
 * Erases the entry at current (destroying the owned CRegion) and writes
 * the successor ctx to *output.
 */
__attribute__((unused)) CSearchCtx *
CResList_EraseAndFree_ByFile(CResManager *rm, CSearchCtx *output, CSearchCtx *current, uint32_t direction)
{
	CSearchCtx localCtx;
	CSearchCtx eraseOut;
	void *erased;
	void *erased2;

	CSearchCtx_Constructor(&localCtx);
	CSearchCtx_Add(&localCtx, (CSearchCtx *)CResManager_InternalErase_ByFile(rm, &eraseOut, current, &erased, direction));

	if (erased != NULL) {
		erased2 = erased;
		if (erased2 != NULL)
			CRegion_ScalarDelete(erased2, 1);
	}

	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x004A6CF0 - CResManager str-key dtor wrapper for ByName/All RM
 *
 * Delegates to CResManager_StrKeyDestructor_Clear_ByNameAll.
 */
void
CResManager_StrKeyDestructor_ByNameAll(CResManager *rm)
{
	CResManager_StrKeyDestructor_Clear_ByNameAll(rm);
}

/*
 * 0x004A6D10 - CResList<int,CRegion*>::Insert for ByName/All RM
 *
 * Finds or creates the key entry and stores *valuePtr into its value
 * slot. Returns 1 on success, 0 if the entry could not be resolved.
 */
__attribute__((unused)) int
CResList_Insert_ByNameAll(CResManager *rm, uint32_t *keyPtr, void **valuePtr)
{
	CSearchCtx localCtx;
	CSearchCtx findCtx;
	uintptr_t valNode;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	CSearchCtx_Add(&localCtx, (CSearchCtx *)CResManager_FindOrCreate_ByNameAll(rm, &findCtx, keyPtr));

	if (!CSearchCtx_Find(&localCtx))
		return 0;

	valNode = CSearchCtx_GetValNode(&localCtx);
	bucket = CSearchCtx_GetBucket(&localCtx);
	CResManager_ApplyTemplateData(rm->vals[bucket], (CResListNode *)valNode, (CItem **)valuePtr);
	return 1;
}

/*
 * 0x004A6D80 - CResList<int,CRegion*>::BeginIter for ByName/All RM
 *
 * Like CResList_BeginIter_ByFile but delegates to the ByName/All
 * internal begin-iter helper.
 */
__attribute__((unused)) CSearchCtx *
CResList_BeginIter_ByNameAll(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr, uint32_t direction)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CSearchCtx_SetEntity(&localCtx, 1);
	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], (int)direction));
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], (int)direction));

	CResManager_BeginIterInternal_ByNameAll(rm, output, keyPtr, &localCtx, direction);
	return output;
}

/*
 * 0x004A6E40 - CResList<int,CRegion*>::EraseAndFree for ByName/All RM
 *
 * Erases the entry at current and releases the erased value with
 * operator delete.
 */
__attribute__((unused)) CSearchCtx *
CResList_EraseAndFree_ByNameAll(CResManager *rm, CSearchCtx *output, CSearchCtx *current, uint32_t direction)
{
	CSearchCtx localCtx;
	CSearchCtx eraseOut;
	void *erased;
	void *erased2;

	CSearchCtx_Constructor(&localCtx);
	CSearchCtx_Add(&localCtx, (CSearchCtx *)CResManager_InternalErase_ByNameAll(rm, &eraseOut, current, &erased, direction));

	if (erased != NULL) {
		erased2 = erased;
		OperatorDelete(erased2);
	}

	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x004A6FD0 - CResList::GetHead
 *
 * Returns the node's data pointer.
 */
static __attribute__((unused)) void *
CResList_GetHead(CResListNode *this)
{
	return this->data;
}

/*
 * 0x004A6FF0 - CSearchCtx::GetValNode
 *
 * Returns the search context's current valNode.
 */
uintptr_t
CSearchCtx_GetValNode(CSearchCtx *ctx)
{
	return ctx->valNode;
}

/*
 * 0x004A7010 - CSearchCtx::SetEntity
 *
 * Stores val into the search context's entity field.
 */
void
CSearchCtx_SetEntity(CSearchCtx *ctx, uint32_t val)
{
	ctx->entity = val;
}

/*
 * 0x004A7030 - CSearchCtx::SetValNode
 *
 * Stores val into the search context's valNode field.
 */
void
CSearchCtx_SetValNode(CSearchCtx *ctx, uintptr_t val)
{
	ctx->valNode = val;
}

/*
 * 0x004A7050 - BeginIter internal for ByFile RM
 *
 * Scans from localCtx until the bucket position matches *keyPtr, writing the
 * found ctx into *out. Writes a no-entity ctx if the scan runs off the end.
 */
static void *
CResManager_BeginIterInternal_ByFile(CResManager *rm, CSearchCtx *out, uint32_t *keyPtr, CSearchCtx *localCtx, uint32_t direction)
{
	CSearchCtx ctx;
	CSearchCtx nextCtx;
	void *keyPos;

	CSearchCtx_Constructor(&ctx);

	if (!CSearchCtx_Find(localCtx)) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}
	CSearchCtx_Add(&ctx, localCtx);

	for (;;) {
		if (!CSearchCtx_Find(&ctx)) {
			CSearchCtx_SetEntity(&ctx, 0);
			CResManager_CreateBucket(out, &ctx);
			return out;
		}
		keyPos = CResManager_GetKeyAtPos(rm, &ctx);
		if (*(uint32_t *)keyPos == *keyPtr) {
			CResManager_CreateBucket(out, &ctx);
			return out;
		}
		CResManager_NextIterStep_ByFile(rm, &nextCtx, &ctx, direction);
		CSearchCtx_Add(&ctx, &nextCtx);
	}
}

/*
 * 0x004A7100 - CResManager int-key dtor for ByFile RM
 *
 * Destroys all 66 key/value bucket lists and zeros the count.
 */
static void
CResManager_IntKeyDestructor_Clear_ByFile(CResManager *rm)
{
	int i;
	void *node;

	for (i = 0; i < 0x42; i++) {
		if (rm->keys[i] != NULL) {
			node = rm->keys[i];
			if (node != NULL)
				ScalarDestructor_KeyNode(node, 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			node = rm->vals[i];
			if (node != NULL)
				ScalarDestructor_ByFileVal(node, 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x004A71E0 - Internal erase for ByFile RM
 *
 * Removes the key/val pair at current, freeing the bucket's lists if they
 * become empty, and writes the successor ctx to *eraseOut.
 */
static CSearchCtx *
CResManager_InternalErase_ByFile(CResManager *rm, CSearchCtx *eraseOut, CSearchCtx *current, void **outErased, uint32_t direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;
	uint32_t bucket;
	void *keyList;
	void *valList;

	CSearchCtx_Constructor(&localCtx);
	*outErased = NULL;

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(eraseOut, &localCtx);
		return eraseOut;
	}

	CSearchCtx_Add(&localCtx, (CSearchCtx *)CResManager_NextIterErase_ByFile(rm, &nextCtx, current, direction));

	bucket = CSearchCtx_GetBucket(current);
	CResList_RemoveKeyNode(rm->keys[bucket], current->keyNode, direction);

	CResList_RemoveVal_ByFile(rm->vals[bucket], CSearchCtx_GetValNode(current), outErased, direction);

	if (!rm->keys[bucket]->count) {
		keyList = rm->keys[bucket];
		if (keyList != NULL)
			ScalarDestructor_KeyNode(keyList, 1);
		valList = rm->vals[bucket];
		if (valList != NULL)
			ScalarDestructor_ByFileVal(valList, 1);
		rm->keys[bucket] = NULL;
		rm->vals[bucket] = NULL;
	}

	rm->count--;
	CResManager_CreateBucket(eraseOut, &localCtx);
	return eraseOut;
}

/*
 * 0x004A7360 - Get tail val node for ByFile vals list
 *
 * Returns the tail after ensuring there is one: pushes a new node after tail
 * if the list already has entries, otherwise creates the single node.
 */
static void *
CResList_GetTailVal_ByFile(CResList *list)
{
	CResListNode *newNode;
	void *raw;

	if (list->tail != NULL) {
		return CResList_InsertAfter_ByFile(list, list->tail);
	}

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	list->tail = newNode;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x004A7410 - NextIter helper for ByFile vals list
 *
 * Advances valNode in its list when non-NULL. The list argument is unused
 * (a template artifact).
 */
static void
CResList_NextIter_ByFile(CResList *list, uintptr_t valNode, uint32_t direction)
{
	USED(list);
	if (valNode == 0)
		return;
	CResList_AdvanceValIter((CResListNode *)valNode, direction);
}

/*
 * 0x004A7430 - Insert value into vals list for ByFile RM
 *
 * Appends a tail node carrying value.
 */
static void *
CResList_InsertVal_ByFile(CResList *list, void *value)
{
	void *tailNode;

	tailNode = CResList_GetTailVal_ByFile(list);
	if (tailNode != NULL)
		CResList_StoreVal_ByFile(tailNode, value);
	return tailNode;
}

/*
 * 0x004A7460 - BeginIter internal for ByName/All RM
 *
 * Walks the bucket starting at localCtx looking for a key matching
 * *keyPtr. Writes the matching ctx or an empty ctx to *out.
 */
static void *
CResManager_BeginIterInternal_ByNameAll(CResManager *rm, CSearchCtx *out, uint32_t *keyPtr, CSearchCtx *localCtx, uint32_t direction)
{
	CSearchCtx ctx;
	CSearchCtx nextCtx;
	void *keyPos;

	CSearchCtx_Constructor(&ctx);

	if (!CSearchCtx_Find(localCtx)) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}
	CSearchCtx_Add(&ctx, localCtx);

	for (;;) {
		if (!CSearchCtx_Find(&ctx)) {
			CSearchCtx_SetEntity(&ctx, 0);
			CResManager_CreateBucket(out, &ctx);
			return out;
		}
		keyPos = CResManager_GetKeyAtPos(rm, &ctx);
		if (*(uint32_t *)keyPos == *keyPtr) {
			CResManager_CreateBucket(out, &ctx);
			return out;
		}
		CResManager_NextIterStep_ByNameAll(rm, &nextCtx, &ctx, direction);
		CSearchCtx_Add(&ctx, &nextCtx);
	}
}

/*
 * 0x004A7510 - CResManager str-key dtor for ByName/All RM
 *
 * Releases every bucket's key and value list.
 */
static void
CResManager_StrKeyDestructor_Clear_ByNameAll(CResManager *rm)
{
	int i;
	void *node;

	for (i = 0; i < 0x42; i++) {
		if (rm->keys[i] != NULL) {
			node = rm->keys[i];
			if (node != NULL)
				ScalarDestructor_KeyNode(node, 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			node = rm->vals[i];
			if (node != NULL)
				ScalarDestructor_ByNameAllVal(node, 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x004A75F0 - Find-or-create for ByName/All RM Insert
 *
 * Returns the ctx of the entry for *keyPtr, creating the bucket slot
 * when missing. Dedup mode (flags==1) reuses an existing key.
 */
static CSearchCtx *
CResManager_FindOrCreate_ByNameAll(CResManager *rm, CSearchCtx *findCtx, uint32_t *keyPtr)
{
	CSearchCtx localCtx;
	uint32_t bucket;
	CResListNode *keyNode;
	CResListNode *valNode;
	void *raw;
	CResListNode *found;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		raw = OperatorNew(sizeof(CResListNode));
		if (raw != NULL)
			keyNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
		else
			keyNode = NULL;
		rm->keys[bucket] = (CResList *)keyNode;

		raw = OperatorNew(sizeof(CResListNode));
		if (raw != NULL)
			valNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
		else
			valNode = NULL;
		rm->vals[bucket] = (CResList *)valNode;
	}

	if (rm->flags == 1) {
		found = CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1);
		if (found) {
			CResManager_CreateBucket(findCtx, &localCtx);
			return findCtx;
		}
	}

	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_KeyInsert(rm->keys[bucket], keyPtr));
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_GetOrCreateTail_ByNameAll(rm->vals[bucket]));
	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetEntity(&localCtx, 1);
	rm->count++;

	CResManager_CreateBucket(findCtx, &localCtx);
	return findCtx;
}

/*
 * 0x004A7780 - Internal erase for ByName/All RM
 *
 * Removes the key/val pair at current, freeing the bucket's lists if
 * they become empty, and writes the successor ctx to *eraseOut.
 */
static CSearchCtx *
CResManager_InternalErase_ByNameAll(CResManager *rm, CSearchCtx *eraseOut, CSearchCtx *current, void **outErased, uint32_t direction)
{
	CSearchCtx localCtx;
	CSearchCtx nextCtx;
	uint32_t bucket;
	void *keyList;
	void *valList;

	CSearchCtx_Constructor(&localCtx);
	*outErased = NULL;

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(eraseOut, &localCtx);
		return eraseOut;
	}

	CSearchCtx_Add(&localCtx, (CSearchCtx *)CResManager_NextIterErase_ByNameAll(rm, &nextCtx, current, direction));

	bucket = CSearchCtx_GetBucket(current);
	CResList_RemoveKeyNode(rm->keys[bucket], current->keyNode, direction);

	CResList_RemoveVal_ByNameAll(rm->vals[bucket], CSearchCtx_GetValNode(current), outErased, direction);

	if (!rm->keys[bucket]->count) {
		keyList = rm->keys[bucket];
		if (keyList != NULL)
			ScalarDestructor_KeyNode(keyList, 1);
		valList = rm->vals[bucket];
		if (valList != NULL)
			ScalarDestructor_ByNameAllVal(valList, 1);
		rm->keys[bucket] = NULL;
		rm->vals[bucket] = NULL;
	}

	rm->count--;
	CResManager_CreateBucket(eraseOut, &localCtx);
	return eraseOut;
}

/*
 * 0x004A7900 - Scalar deleting destructor for ByFile vals nodes
 *
 * Runs CResList_Destructor_ByFileVal and frees the list when flag & 1.
 */
static void *
ScalarDestructor_ByFileVal(CResList *this, int flag)
{
	CResList_Destructor_ByFileVal(this);
	if (flag & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x004A7930 - Scalar deleting destructor for ByName/All vals nodes
 *
 * Runs CResList_Destructor_ByNameAllVal and frees the list when flag & 1.
 */
void *
ScalarDestructor_ByNameAllVal(CResList *this, int flag)
{
	CResList_Destructor_ByNameAllVal(this);
	if (flag & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x004A7960 - NextIter step for ByFile RM
 *
 * Advances current to the next key/val pair in its bucket.
 */
static void *
CResManager_NextIterStep_ByFile(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *nextKey;
	CResListNode *nextVal;
	int valid;

	CSearchCtx_Constructor(&ctx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	bucket = CSearchCtx_GetBucket(current);
	CSearchCtx_SetBucket(&ctx, bucket);

	nextKey = (CResListNode *)CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, (int)direction);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)nextKey);

	nextVal = (CResListNode *)CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), (int)direction);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)nextVal);

	valid = CResList_IsValid(rm->keys[bucket], nextKey);
	CSearchCtx_SetEntity(&ctx, valid);

	CResManager_CreateBucket(out, &ctx);
	return out;
}

/*
 * 0x004A7A40 - NextIter step for ByFile RM erase path
 *
 * Advances current past a bucket boundary by delegating to
 * CResManager_FindNextBucket_ByFile when the next slot is invalid.
 */
static void *
CResManager_NextIterErase_ByFile(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *nextKey;
	CResListNode *nextVal;
	int valid;
	uint32_t newBucket;

	CSearchCtx_Constructor(&ctx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	bucket = CSearchCtx_GetBucket(current);
	CSearchCtx_SetBucket(&ctx, bucket);

	nextKey = (CResListNode *)CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, (int)direction);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)nextKey);

	nextVal = (CResListNode *)CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), (int)direction);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)nextVal);

	valid = CResList_IsValid(rm->keys[bucket], nextKey);
	if (valid) {
		CSearchCtx_SetEntity(&ctx, 1);
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	newBucket = CSearchCtx_GetBucket(&ctx);
	if (newBucket == 0 && direction == 0)
		goto end;
	if (newBucket == 0x41 && direction != 0)
		goto end;

	if (direction == 1)
		newBucket++;
	else
		newBucket--;

	CResManager_FindNextBucket_ByFile(rm, out, newBucket, direction);
	return out;

end:
	CResManager_CreateBucket(out, &ctx);
	return out;
}

/*
 * 0x004A7B90 - CResList::InsertAfter (ByFile val)
 *
 * Allocates a new node and splices it in after position, updating head/tail.
 */
static void *
CResList_InsertAfter_ByFile(CResList *list, CResListNode *position)
{
	CResListNode *afterNode;
	CResListNode *newNode;
	void *raw;

	if (position == NULL)
		return NULL;

	afterNode = CResList_Begin((CResList *)position);

	raw = OperatorNew(sizeof(CResListNode));
	if (raw != NULL)
		newNode = (CResListNode *)CResListNode_Constructor_bin((CResListNode *)raw);
	else
		newNode = NULL;

	CResListNode_SetNext(newNode, afterNode);
	CResListNode_SetPrev(newNode, position);
	CResListNode_SetNext(position, newNode);

	if (afterNode != NULL) {
		CResListNode_SetPrev(afterNode, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x004A7C80 - CResList::RemoveVal (ByFile vals)
 *
 * Unlinks valNode, hands its data to *outErased, frees the node, and returns
 * the neighbor chosen by direction (1 = next, else prev).
 */
static void *
CResList_RemoveVal_ByFile(CResList *list, uintptr_t valNode, void **outErased, uint32_t direction)
{
	CResListNode *prev, *next;

	*outErased = NULL;
	if (valNode == 0)
		return NULL;

	prev = CResList_GetTail((CResList *)valNode);
	next = CResList_Begin((CResList *)valNode);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outErased = CResListNode_SwapData((CResListNode *)valNode, NULL);

	if ((CResListNode *)valNode != NULL)
		CResListValNode_ScalarDelete_Region((void *)valNode, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004A7D50 - CResList::~CResList (ByFile vals)
 *
 * Delegates to CResList_ClearAll_ByFileVal.
 */
static void
CResList_Destructor_ByFileVal(CResList *this)
{
	CResList_ClearAll_ByFileVal(this);
}

/*
 * 0x004A7D70 - CResList::AdvanceValIter (ByFile)
 *
 * Lazily allocates this->data as a CRegion copy of src, or copies src over
 * an existing CRegion.
 */
static void *
CResList_AdvanceValIter(CResListNode *this, uint32_t direction)
{
	CRegion *src = (CRegion *)(uintptr_t)direction;
	CRegion *newRegion;
	void *raw;

	if (this->data == NULL) {
		raw = OperatorNew(0x8C);
		if (raw != NULL)
			newRegion = CRegion_Assign((CRegion *)raw, src);
		else
			newRegion = NULL;
		this->data = newRegion;
	} else {
		CRegion_CopyFrom((CRegion *)this->data, src);
	}
	return NULL;
}

/*
 * 0x004A7E10 - CResList::StoreVal (ByFile variant)
 *
 * Replaces the CRegion owned by this node's data slot.
 */
static void *
CResList_StoreVal_ByFile(CResList *list, void *value)
{
	CResListNode *node = (CResListNode *)list;
	void *oldData;
	void *tmp;

	if (node->data != NULL) {
		oldData = node->data;
		tmp = oldData;
		if (tmp != NULL)
			CRegion_ScalarDelete(tmp, 1);
		node->data = NULL;
	}
	node->data = value;
	return value;
}

/*
 * 0x004A7E70 - CResListNode::GetData
 *
 * Returns the node's data pointer.
 */
static __attribute__((unused)) void *
CResListNode_GetData(CResListNode *node)
{
	return node->data;
}

/*
 * 0x004A7E90 - NextIter step for ByName/All RM
 *
 * Same structure as CResManager_NextIterStep_ByFile.
 */
static void *
CResManager_NextIterStep_ByNameAll(CResManager *rm, CSearchCtx *out, CSearchCtx *current, uint32_t direction)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *nextKey;
	CResListNode *nextVal;
	int valid;

	CSearchCtx_Constructor(&ctx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	bucket = CSearchCtx_GetBucket(current);

	nextKey = (CResListNode *)CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, (int)direction);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)nextKey);

	nextVal = (CResListNode *)CResList_DirectionNext(rm->vals[bucket], (CResListNode *)CSearchCtx_GetValNode(current), (int)direction);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)nextVal);

	CSearchCtx_SetBucket(&ctx, bucket);

	valid = CResList_IsValid(rm->keys[bucket], nextKey);
	CSearchCtx_SetEntity(&ctx, valid);

	CResManager_CreateBucket(out, &ctx);
	return out;
}

/*
 * 0x004A7F70 - CResManager val list ScalarDelete (region ByName variant)
 *
 * Scalar deleting destructor.
 */
__attribute__((unused)) void *
CResListValNode_ScalarDelete_Region(CResListNode *this, int flags)
{
	CResList_ValNodeDestructor_Region(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004A7FA0 - Find next non-empty bucket for ByFile RM
 *
 * Scans from bucket in direction for the next populated key/val pair, writing
 * its ctx to *out, or writes an empty ctx when the scan hits an edge.
 */
static void *
CResManager_FindNextBucket_ByFile(CResManager *rm, CSearchCtx *out, uint32_t bucket, uint32_t direction)
{
	CSearchCtx ctx;

	CSearchCtx_Constructor(&ctx);

	if (bucket >= 0x42) {
		CResManager_CreateBucket(out, &ctx);
		return out;
	}

	for (;;) {
		if (rm->keys[bucket] != NULL) {
			CSearchCtx_SetBucket(&ctx, bucket);
			CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));
			CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));
			CSearchCtx_SetEntity(&ctx, 1);
			CResManager_CreateBucket(out, &ctx);
			return out;
		}

		if (bucket == 0 && direction == 0) {
			CResManager_CreateBucket(out, &ctx);
			return out;
		}
		if (bucket == 0x41 && direction != 0) {
			CResManager_CreateBucket(out, &ctx);
			return out;
		}

		if (direction == 1)
			bucket += 1;
		else
			bucket -= 1;
	}
}

/*
 * 0x004A80A0 - CResList clear-all for ByFile val nodes
 *
 * Erases and frees every node in the ByFile val list.
 */
static void
CResList_ClearAll_ByFileVal(CResList *this)
{
	CResList *list = this;
	CResListNode *node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = (CResListNode *)CResList_EraseAndFree_ByFileVal(list, (uintptr_t)node, 1);
	}
}

/*
 * 0x004A80E0 - CResList val node dtor (region ByName variant)
 *
 * Releases the CRegion owned by this node.
 */
static void
CResList_ValNodeDestructor_Region(CResListNode *this)
{
	CResListNode *node = this;
	void *data;
	void *tmp;

	if (node->data != NULL) {
		data = node->data;
		tmp = data;
		if (tmp != NULL)
			CRegion_ScalarDelete(tmp, 1);
		node->data = NULL;
	}
}

/*
 * 0x004A8130 - CResList::GetTail
 *
 * Returns the list's tail node.
 */
CResListNode *
CResList_GetTail(CResList *list)
{
	return list->tail;
}

/*
 * 0x004A8150 - CResList erase+free for ByFile val nodes
 *
 * Unlinks the valNode and deletes its owned CRegion.
 */
static void *
CResList_EraseAndFree_ByFileVal(CResList *list, uintptr_t valNode, uint32_t direction)
{
	void *erased = NULL;
	void *result;

	result = CResList_RemoveVal_ByFile(list, valNode, &erased, direction);
	if (erased != NULL) {
		CRegion_ScalarDelete(erased, 1);
	}
	return result;
}

/*
 * 0x004A864D - CSearchCtx free key+val pointers
 *
 * Frees the bucket and keyNode pointers held by the search context.
 */
static __attribute__((unused)) void
CSearchCtx_FreeKeyVal(CSearchCtx *this)
{
	CSearchCtx *ctx = this;
	OperatorDelete((void *)ctx->bucket);
	OperatorDelete((void *)ctx->keyNode);
}

/*
 * 0x004B32F0 - CStringList::Invalidate
 *
 * Resets list->head to NULL.
 */
void
CStringList_Invalidate(CStringList *sl)
{
	sl->list.head = NULL;
}

/*
 * 0x004B3310 - CResList::DirectionIterInit
 *
 * Seeds iterOut with the first node in the given direction.
 */
void *
CResList_DirectionIterInit(CResList *list, void *iterOut, int direction)
{
	CResListNode *begin;
	int flag;

	flag = 0;
	begin = CResList_DirectionBegin(list, direction);
	CIterCtx_Set(iterOut, begin);
	flag |= 1;
	USED(flag);
	return iterOut;
}

/*
 * 0x004B3350 - CStringList::DirectionAdvanceIter
 *
 * Advances *iterNode in direction and writes the result into *outNode.
 */
void *
CStringList_DirectionAdvanceIter(CResList *list, CResListNode **outNode, CResListNode **iterNode, int direction)
{
	CResListNode *next;
	int flag;

	flag = 0;
	next = CResList_DirectionNext(list, *iterNode, direction);
	CIterCtx_Set(outNode, next);
	flag |= 1;
	USED(flag);
	return outNode;
}

/*
 * 0x004BFB81 - CResManager::HasByInt
 *
 * Returns non-zero if key is present in the int-keyed table.
 */
int
CResManager_HasByInt(CResManager *rm, uint32_t key)
{
	CSearchCtx ctx;

	CResManager_FindByIntCtx(rm, &ctx, &key, 1);
	return CSearchCtx_Find(&ctx);
}

/*
 * 0x004C06B0 - CResManager::CResManager (templates variant, 37 bytes)
 *
 * Constructs the three contiguous CResLists of a CNameEntry
 * (male/female/other name lists).
 */
void *
CResManager_Constructor_Templates(CResManager *this)
{
	CResList *lists = (CResList *)this;
	int i;
	for (i = 0; i < 3; i++) {
		lists[i].head = NULL;
		lists[i].tail = NULL;
		lists[i].count = 0;
	}
	return this;
}

/*
 * 0x004C06E0 - CResManager::~CResManager (templates variant)
 *
 * Destructor for the templates variant: clears every bucket.
 */
void
CResManager_Destructor_Templates(CResManager *this)
{
	CResManager_Clear_Templates(this);
}

/*
 * 0x004C0700 - CResManager::Clear (templates variant F)
 *
 * Destroys all 66 key/value bucket lists and zeros the count.
 */
void
CResManager_Clear_Templates(CResManager *this)
{
	CResManager *rm = this;
	uint32_t i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDelete_MultiA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDelete_TemplatesVal(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x004C07E0 - CResManager::InsertIntEntry (templates variant F)
 *
 * Inserts (key, value) into the int-keyed map. If flags==1 and key already
 * present, returns 0 without inserting. Returns 1 on success.
 */
int
CResManager_InsertIntEntryF(CResManager *rm, void *keyPtr, void *valPtr)
{
	uint32_t bucket;
	CResListNode *found;

	bucket = ResManager_HashInt(*(uint32_t *)keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		found = CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1);
		if (found != NULL)
			return 0;
	}

	CResList_PrependInt_A(rm->keys[bucket], (uint32_t *)keyPtr);

	CResList_AllocForwardF(rm->vals[bucket], valPtr);

	rm->count++;
	return 1;
}

/*
 * 0x004C0930 - CResManager::FindByInt
 *
 * Seeds a CSearchCtx for integer key *keyPtr and walks the bucket via
 * CResManager_NextIterInternal_Templates. Empty bucket returns an empty ctx.
 */
void
CResManager_FindByIntCtx(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr, int direction)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashInt(*keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	CSearchCtx_SetEntity(&localCtx, 1);
	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));
	CResManager_NextIterInternal_Templates(rm, output, keyPtr, &localCtx, direction);
}

/*
 * 0x004C09F0 - CResManager::~CResManager (name table)
 *
 * Delegates to CResManager_Clear_NameTable.
 */
void
CResManager_Destructor_NameTable(CResManager *this)
{
	CResManager_Clear_NameTable(this);
}

/*
 * 0x004C0A10 - CResManager::Clear (name table)
 *
 * Frees every key/val list across 66 buckets and resets count.
 */
void
CResManager_Clear_NameTable(CResManager *this)
{
	CResManager *rm = this;
	uint32_t i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDelete_MultiA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDelete_NameTableVal(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x004C0AF0 - CResManager::InsertIntEntry (name table variant G)
 *
 * Same as variant F but uses CResList_AllocForwardG for the value insert.
 */
static __attribute__((unused)) int
CResManager_InsertIntEntryG(CResManager *rm, void *keyPtr, void *valPtr)
{
	uint32_t bucket;
	CResListNode *found;

	bucket = ResManager_HashInt(*(uint32_t *)keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		found = CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1);
		if (found != NULL)
			return 0;
	}

	CResList_PrependInt_A(rm->keys[bucket], (uint32_t *)keyPtr);
	CResList_AllocForwardG(rm->vals[bucket], valPtr);

	rm->count++;
	return 1;
}

/*
 * 0x004C0C40 - CResManager::FindByInt (templates)
 *
 * Seeds a CSearchCtx for key *keyPtr and delegates to
 * CResManager_SearchBucket_TemplatesInt. Empty bucket returns an empty ctx.
 */
static __attribute__((unused)) void *
CResManager_FindByInt_Templates(CResManager *rm, CSearchCtx *output, void *keyPtr, int direction)
{
	CSearchCtx ctx;
	uint32_t bucket;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashInt(*(uint32_t *)keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}

	CSearchCtx_SetEntity(&ctx, 1);
	CSearchCtx_SetBucket(&ctx, bucket);

	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));

	CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));

	return CResManager_SearchBucket_TemplatesInt(rm, output, keyPtr, &ctx, direction);
}

/*
 * 0x004C0D00 - CResManager::GetResult (defines variant)
 *
 * Delegates to CResManager_GetResultCtx with the defines-variant
 * search context.
 */
void *
CResManager_GetResult_Defines(CResManager *this, CSearchCtx *ctx)
{
	return CResManager_GetResultCtx(this, ctx);
}

/*
 * 0x004C0D20 - CResManager::~CResManager (defines variant)
 *
 * Destructor for the defines variant: clears every bucket.
 */
void
CResManager_Destructor_Defines(CResManager *this)
{
	CResManager_Clear_Defines(this);
}

/*
 * 0x004C0D40 - CResManager::Clear (defines variant H)
 *
 * Destroys all 66 key/value bucket lists and zeros the count.
 */
void
CResManager_Clear_Defines(CResManager *this)
{
	CResManager *rm = this;
	uint32_t i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDeleteA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDeleteA(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x004C0E20 - CResManager::InsertStrEntry (defines variant)
 *
 * Resolves or creates the entry for key, then stores value into its val node.
 */
int
CResManager_InsertStrEntry_Defines(CResManager *rm, CString *key, void *value)
{
	CSearchCtx ctx, findCtx;

	CSearchCtx_Constructor(&ctx);

	CResManager_FindOrInsertH(rm, &findCtx, key);
	CSearchCtx_Add(&ctx, &findCtx);

	if (!CSearchCtx_Find(&ctx))
		return 0;

	CResListNode *valNode = (CResListNode *)CSearchCtx_GetValNode(&ctx);
	uint32_t bucket = CSearchCtx_GetBucket(&ctx);

	CResListNode_SetStringIfValid((CResListNode *)rm->vals[bucket], valNode, value);

	return 1;
}

/*
 * 0x004C0E90 - CResManager::FindByStr (defines variant)
 *
 * Seeds a CSearchCtx for key and delegates to CResManager_SearchBucket_DefinesStr.
 * Empty bucket returns an empty ctx.
 */
void *
CResManager_FindByStr_Defines(CResManager *rm, CSearchCtx *output, CString *key, int direction)
{
	CSearchCtx ctx;
	uint32_t bucket;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashStrA(key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &ctx);
		return output;
	}

	CSearchCtx_SetEntity(&ctx, 1);
	CSearchCtx_SetBucket(&ctx, bucket);

	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));

	CSearchCtx_SetValNode(&ctx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));

	return CResManager_SearchBucket_DefinesStr(rm, output, key, &ctx, direction);
}

/*
 * 0x004C0F50 - CResManager::Clear thunk (template RM variant)
 *
 * Thin wrapper forwarding to CResManager_ClearJ.
 */
static __attribute__((unused)) void
CResManager_ClearJ_Thunk(CResManager *this)
{
	CResManager_ClearJ(this);
}

/*
 * 0x004C0FE0 - CResManager::FindContainer
 *
 * Seeds a CSearchCtx for name and delegates to
 * CResManager_SearchBucket_DefinesStrB. Empty bucket returns an empty ctx.
 */
CSearchCtx *
CResManager_FindContainer(CResManager *rm, CSearchCtx *output, struct CString *name, int flag)
{
	CSearchCtx localCtx;
	uint32_t bucket;

	CSearchCtx_Constructor(&localCtx);
	bucket = ResManager_HashStrA(name, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	CSearchCtx_SetEntity(&localCtx, 1);
	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], flag));
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], flag));
	CResManager_SearchBucket_DefinesStrB(rm, output, name, &localCtx, flag);
	return output;
}

/*
 * 0x004C1300 - CResManager::~CResManager
 *
 * Delegates to CResManager_ClearK.
 */
void
CResManager_Destructor(CResManager *rm)
{
	CResManager_ClearK(rm);
}

/*
 * 0x004C1320 - CResManager::InsertStrEntry
 *
 * Inserts or reuses a key via CResManager_FindOrInsertK and stores value
 * through CResManager_ApplyTemplateData. Returns 1 on success, 0 otherwise.
 */
int
CResManager_InsertStrEntry(CResManager *rm, CString *key, void *value)
{
	CSearchCtx localCtx;
	CSearchCtx tmpCtx;

	CSearchCtx_Constructor(&localCtx);
	CSearchCtx_Add(&localCtx, CResManager_FindOrInsertK(rm, &tmpCtx, key));

	if (!CSearchCtx_Find(&localCtx))
		return 0;

	CResManager_ApplyTemplateData(rm->vals[CSearchCtx_GetBucket(&localCtx)], (CResListNode *)CSearchCtx_GetValNode(&localCtx), (CItem **)value);
	return 1;
}

/*
 * 0x004C1390 - CResManager::Begin
 *
 * Delegates to CResManager_BeginIterInternalK with bucket 0, direction 1.
 */
void
CResManager_BeginIter(CResManager *rm, CSearchCtx *output)
{
	CResManager_BeginIterInternalK(rm, output, 0, 1);
}

/*
 * 0x004C13B0 - CResManager::Next
 *
 * Delegates to CResManager_NextIterInternalK with direction 1.
 */
void
CResManager_NextIter(CResManager *rm, CSearchCtx *output, CSearchCtx *current)
{
	CResManager_NextIterInternalK(rm, output, current, 1);
}

/*
 * 0x004C13E0 - CResManager val list ScalarDelete (templates)
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
__attribute__((unused)) void *
CResList_ScalarDelete_TemplatesVal(CResList *this, int flags)
{
	CResList_Destructor_TemplatesVal(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004C1410 - CResManager val list ScalarDelete (name table)
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
__attribute__((unused)) void *
CResList_ScalarDelete_NameTableVal(CResList *this, int flags)
{
	CResList_Destructor_NameTableVal(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004C1440 - CSearchCtx::SetKeyNode
 *
 * Sets keyNode field (+0x08).
 */
void
CSearchCtx_SetKeyNode(CSearchCtx *ctx, uintptr_t val)
{
	ctx->keyNode = val;
}

/*
 * 0x004C1460 - CSearchCtx::SetBucket
 *
 * Sets bucket field (+0x04).
 */
void
CSearchCtx_SetBucket(CSearchCtx *ctx, uint32_t val)
{
	ctx->bucket = val;
}

/*
 * 0x004C1500 - CResListNode::SetStringIfValid
 *
 * Null-checked wrapper around CResListNode_SetString.
 */
static void
CResListNode_SetStringIfValid(CResListNode *this, void *node, void *src)
{
	USED(this);
	if (node != NULL)
		CResListNode_SetString((CResListNode *)node, (CString *)src);
}

/*
 * 0x004C1520 - CResManager::NextIterInternal (templates)
 *
 * Walks key/val node pairs in ctx looking for a matching int key.
 */
void *
CResManager_NextIterInternal_Templates(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	CSearchCtx_Add(&result, ctx);

	while (CSearchCtx_Find(&result)) {
		void *keyData = CResManager_GetKeyAtPos(rm, &result);
		if (*(uint32_t *)keyData == *(uint32_t *)searchKey) {
			CResManager_CreateBucket(output, &result);
			return output;
		}
		CSearchCtx tmpResult;
		CResManager_NextIterInternal_TemplatesB(rm, &tmpResult, &result, direction);
		CSearchCtx_Add(&result, &tmpResult);
	}

	CSearchCtx_SetEntity(&result, 0);
	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C15D0 - CResManager::InitTables
 *
 * Zeroes the keys[] and vals[] arrays.
 */
void
CResManager_InitTables(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		rm->keys[i] = NULL;
		rm->vals[i] = NULL;
	}
}

/*
 * 0x004C1620 - CResList::~CResList (templates val)
 *
 * Delegates to CResList_ClearInternal_TemplatesVal.
 */
static void
CResList_Destructor_TemplatesVal(CResList *this)
{
	CResList_ClearInternal_TemplatesVal(this);
}

/*
 * 0x004C1640 - CResList::AllocForward (templates)
 *
 * Allocates a node via CResList_BeginIterF and stores srcData in it.
 */
static CResListNode *
CResList_AllocForwardF(CResList *list, void *srcData)
{
	CResListNode *node;

	node = CResList_BeginIterF(list);
	if (node != NULL)
		CResListNode_ReplaceDataF(node, srcData);
	return node;
}

/*
 * 0x004C1670 - CResList::GetData
 *
 * Returns node->data. The list pointer is unused.
 */
void *
CResList_GetData(CResList *list, CResListNode *node)
{
	USED(list);
	return node->data;
}

/*
 * 0x004C1690 - CResManager::SearchBucket (templates int)
 *
 * Walks key/val node pairs from ctx; returns the CSearchCtx at the first
 * matching int key, or an empty ctx when none match.
 */
static void *
CResManager_SearchBucket_TemplatesInt(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	CSearchCtx_Add(&result, ctx);

	while (CSearchCtx_Find(&result)) {
		void *keyData = CResManager_GetKeyAtPos(rm, &result);
		if (*(uint32_t *)keyData == *(uint32_t *)searchKey) {
			CResManager_CreateBucket(output, &result);
			return output;
		}
		CSearchCtx tmpResult;
		CResManager_NextIterInternal_NameTable(rm, &tmpResult, &result, direction);
		CSearchCtx_Add(&result, &tmpResult);
	}

	CSearchCtx_SetEntity(&result, 0);
	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C1740 - CResList::~CResList (name table val)
 *
 * Delegates to CResList_ClearInternal_NameTableVal.
 */
static void
CResList_Destructor_NameTableVal(CResList *this)
{
	CResList_ClearInternal_NameTableVal(this);
}

/*
 * 0x004C1760 - CResList::AllocForward (name table)
 *
 * Allocates a node via CResList_BeginIterG and stores srcData in it.
 */
static CResListNode *
CResList_AllocForwardG(CResList *list, void *srcData)
{
	CResListNode *node;

	node = CResList_BeginIterG(list);
	if (node != NULL)
		CResListNode_ReplaceDataG(node, srcData);
	return node;
}

/*
 * 0x004C1790 - CResManager::SearchBucket (defines str)
 *
 * Walks key/val node pairs from ctx looking for a matching CString key.
 */
static void *
CResManager_SearchBucket_DefinesStr(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	CSearchCtx_Add(&result, ctx);

	while (CSearchCtx_Find(&result)) {
		void *keyData = CResManager_GetKeyAtPos(rm, &result);
		if (CString_EqualCString((CString *)keyData, (CString *)searchKey)) {
			CResManager_CreateBucket(output, &result);
			return output;
		}
		CSearchCtx tmpResult;
		CResManager_NextIterInternal_Defines(rm, &tmpResult, &result, direction);
		CSearchCtx_Add(&result, &tmpResult);
	}

	CSearchCtx_SetEntity(&result, 0);
	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C1840 - CResManager::FindOrInsert (defines variant H)
 *
 * Hashes key and ensures a bucket exists. If flags==1 and key already present,
 * returns an empty ctx; otherwise inserts new key and tail-allocates a value
 * node via CResList_AllocTailNodeA.
 */
static CSearchCtx *
CResManager_FindOrInsertH(CResManager *rm, CSearchCtx *output, CString *key)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *keyNode, *valNode;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashStrA(key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		CResListNode *found = CResList_FindByString(rm->keys[bucket], key, NULL, 1);
		if (found != NULL) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
	}

	keyNode = CResList_InsertTailStr(rm->keys[bucket], key);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)keyNode);

	valNode = CResList_AllocTailNodeA(rm->vals[bucket]);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)valNode);

	CSearchCtx_SetBucket(&ctx, bucket);
	CSearchCtx_SetEntity(&ctx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x004C19D0 - CResManager::SearchBucket (defines str variant B, 176 bytes)
 *
 * Same as CResManager_SearchBucket_DefinesStr but advances via
 * CResManager_NextIterInternal_Labels.
 */
void *
CResManager_SearchBucket_DefinesStrB(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	CSearchCtx_Add(&result, ctx);

	while (CSearchCtx_Find(&result)) {
		void *keyData = CResManager_GetKeyAtPos(rm, &result);
		if (CString_EqualCString((CString *)keyData, (CString *)searchKey)) {
			CResManager_CreateBucket(output, &result);
			return output;
		}
		CSearchCtx tmpResult;
		CResManager_NextIterInternal_Labels(rm, &tmpResult, &result, direction);
		CSearchCtx_Add(&result, &tmpResult);
	}

	CSearchCtx_SetEntity(&result, 0);
	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C1A80 - CResManager::Clear (labels variant J)
 *
 * Frees every key/val list across 66 buckets and resets count.
 */
static void
CResManager_ClearJ(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDeleteA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			CResList_ScalarDelete_TemplatesVar(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x004C1D10 - CStringPairList::~CStringPairList
 *
 * Delegates to CResList_RemoveAll.
 */
static __attribute__((unused)) void
CStringPairList_Destructor(CResList *this)
{
	CResList_RemoveAll(this);
}

/*
 * 0x004C1D60 - CResManager::BeginIterInternal (default variant K)
 *
 * Scans from startBucket (in direction) for the first non-empty bucket and
 * seeds *output with its first key/val nodes.
 */
void
CResManager_BeginIterInternalK(CResManager *rm, CSearchCtx *output, int startBucket, int direction)
{
	CSearchCtx localCtx;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (startBucket >= 0x42) {
		CResManager_CreateBucket(output, &localCtx);
		return;
	}

	bucket = startBucket;
	for (;;) {
		if (rm->keys[bucket] != NULL) {
			CSearchCtx_SetBucket(&localCtx, bucket);
			CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->keys[bucket], direction));
			CSearchCtx_SetValNode(&localCtx, (uintptr_t)CResList_DirectionBegin(rm->vals[bucket], direction));
			CSearchCtx_SetEntity(&localCtx, 1);
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (bucket == 0 && direction == 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}
		if (bucket == 0x41 && direction != 0) {
			CResManager_CreateBucket(output, &localCtx);
			return;
		}

		if (direction == 1)
			bucket += 1;
		else
			bucket -= 1;
	}
}

/*
 * 0x004C1E60 - CResManager::Clear (default RM variant K)
 *
 * Frees every key/val list across 66 buckets and resets count.
 */
void
CResManager_ClearK(CResManager *rm)
{
	int i;

	for (i = 0; i < CRESMANAGER_BUCKETS; i++) {
		if (rm->keys[i] != NULL) {
			CResList_ScalarDeleteA(rm->keys[i], 1);
			rm->keys[i] = NULL;
		}
		if (rm->vals[i] != NULL) {
			ScalarDestructor_ByNameAllVal(rm->vals[i], 1);
			rm->vals[i] = NULL;
		}
	}
	rm->count = 0;
}

/*
 * 0x004C1F40 - CResManager::FindOrInsert (default RM variant K)
 *
 * Hashes key and ensures a bucket exists. If flags==1 and key already present,
 * returns an empty ctx; otherwise inserts new key and tail-allocates a value
 * node via CResList_GetOrCreateTail_ByNameAll.
 */
CSearchCtx *
CResManager_FindOrInsertK(CResManager *rm, CSearchCtx *output, CString *key)
{
	CSearchCtx ctx;
	uint32_t bucket;
	CResListNode *keyNode, *valNode;

	CSearchCtx_Constructor(&ctx);

	bucket = ResManager_HashStrA(key, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)malloc(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)malloc(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		CResListNode *found = CResList_FindByString(rm->keys[bucket], key, NULL, 1);
		if (found != NULL) {
			CResManager_CreateBucket(output, &ctx);
			return output;
		}
	}

	keyNode = CResList_InsertTailStr(rm->keys[bucket], key);
	CSearchCtx_SetKeyNode(&ctx, (uintptr_t)keyNode);

	valNode = (CResListNode *)CResList_GetOrCreateTail_ByNameAll(rm->vals[bucket]);
	CSearchCtx_SetValNode(&ctx, (uintptr_t)valNode);

	CSearchCtx_SetBucket(&ctx, bucket);
	CSearchCtx_SetEntity(&ctx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &ctx);
	return output;
}

/*
 * 0x004C20D0 - CResManager::NextIterInternal (default variant K)
 *
 * Advances current's key/val nodes one step. When the bucket runs out, falls
 * back to CResManager_BeginIterInternalK to locate the next non-empty one.
 */
void *
CResManager_NextIterInternalK(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction)
{
	CSearchCtx localCtx;
	CResListNode *nextKey, *nextVal;
	int bucket;

	CSearchCtx_Constructor(&localCtx);

	if (!CSearchCtx_Find(current)) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	bucket = current->bucket;
	CSearchCtx_SetBucket(&localCtx, bucket);

	nextKey = CResList_DirectionNext(rm->keys[bucket], (CResListNode *)current->keyNode, direction);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)nextKey);

	nextVal = CResList_DirectionNext(rm->vals[bucket], (CResListNode *)current->valNode, direction);
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)nextVal);

	bucket = localCtx.bucket;
	if (CResList_IsValid(rm->keys[bucket], (CResListNode *)localCtx.keyNode)) {
		CSearchCtx_SetEntity(&localCtx, 1);
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	if (bucket == 0 && direction == 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}
	if (bucket == 0x41 && direction != 0) {
		CResManager_CreateBucket(output, &localCtx);
		return output;
	}

	if (direction == 1)
		bucket += 1;
	else
		bucket -= 1;

	CResManager_BeginIterInternalK(rm, output, bucket, direction);
	return output;
}

/*
 * 0x004C2220 - CResManager val list ScalarDelete (templates variant)
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
static void *
CResList_ScalarDelete_TemplatesVar(CResList *this, int flags)
{
	CResList_Destructor_TemplatesVariant(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004C2250 - CResManager::NextIterInternal (templates variant)
 *
 * Advances ctx's key/val nodes one step within the same bucket.
 */
static void *
CResManager_NextIterInternal_TemplatesB(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	uint32_t bucket = CSearchCtx_GetBucket(ctx);

	CResListNode *keyNode = (CResListNode *)CSearchCtx_GetKeyNode(ctx);
	keyNode = CResList_DirectionNext(rm->keys[bucket], keyNode, direction);
	CSearchCtx_SetKeyNode(&result, (uintptr_t)keyNode);

	CResListNode *valNode = (CResListNode *)CSearchCtx_GetValNode(ctx);
	valNode = CResList_DirectionNext(rm->vals[bucket], valNode, direction);
	CSearchCtx_SetValNode(&result, (uintptr_t)valNode);

	bucket = CSearchCtx_GetBucket(ctx);
	CSearchCtx_SetBucket(&result, bucket);

	CResListNode *checkKey = (CResListNode *)CSearchCtx_GetKeyNode(&result);
	bucket = CSearchCtx_GetBucket(&result);
	int valid = CResList_IsValid(rm->keys[bucket], checkKey);
	CSearchCtx_SetEntity(&result, valid);

	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C2330 - CResList::BeginIter (templates val)
 *
 * Returns the tail, recycling it via CResList_RecycleNodeF, or allocates a
 * fresh node as head/tail when the list is empty.
 */
static CResListNode *
CResList_BeginIterF(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeF(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x004C23E0 - CResList::ClearInternal (templates val)
 *
 * Iterates and erases every node via CResList_ReplaceDataJ2.
 */
static void
CResList_ClearInternal_TemplatesVal(CResList *this)
{
	CResList *list = this;
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_ReplaceDataJ2(list, node, 1);
	}
}

/*
 * 0x004C2420 - CResListNode::ReplaceData (templates val)
 *
 * Frees any existing CTemplate in node->data and stores newData.
 */
static void
CResListNode_ReplaceDataF(CResListNode *node, void *newData)
{
	if (node->data != NULL) {
		CTemplate_ScalarDelete(node->data, 1);
		node->data = NULL;
	}
	node->data = newData;
}

/*
 * 0x004C2480 - CResManager::NextIterInternal (name table)
 *
 * Advances ctx's key/val nodes one step, like
 * CResManager_NextIterInternal_TemplatesB.
 */
static void *
CResManager_NextIterInternal_NameTable(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	uint32_t bucket = CSearchCtx_GetBucket(ctx);

	CResListNode *keyNode = (CResListNode *)CSearchCtx_GetKeyNode(ctx);
	keyNode = CResList_DirectionNext(rm->keys[bucket], keyNode, direction);
	CSearchCtx_SetKeyNode(&result, (uintptr_t)keyNode);

	CResListNode *valNode = (CResListNode *)CSearchCtx_GetValNode(ctx);
	valNode = CResList_DirectionNext(rm->vals[bucket], valNode, direction);
	CSearchCtx_SetValNode(&result, (uintptr_t)valNode);

	bucket = CSearchCtx_GetBucket(ctx);
	CSearchCtx_SetBucket(&result, bucket);

	CResListNode *checkKey = (CResListNode *)CSearchCtx_GetKeyNode(&result);
	bucket = CSearchCtx_GetBucket(&result);
	int valid = CResList_IsValid(rm->keys[bucket], checkKey);
	CSearchCtx_SetEntity(&result, valid);

	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C2560 - CResList::BeginIter (name table val)
 *
 * Returns the tail, recycling it via CResList_RecycleNodeG, or allocates a
 * fresh node as head/tail when the list is empty.
 */
static CResListNode *
CResList_BeginIterG(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeG(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x004C2610 - CResList::ClearInternal (name table val)
 *
 * Iterates and erases every node via CResList_ReplaceData_G.
 */
static void
CResList_ClearInternal_NameTableVal(CResList *this)
{
	CResList *list = this;
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_ReplaceData_G(list, node, 1);
	}
}

/*
 * 0x004C2650 - CResListNode::ReplaceData (name table val)
 *
 * Frees any existing value in node->data and stores newData.
 */
static void
CResListNode_ReplaceDataG(CResListNode *node, void *newData)
{
	if (node->data != NULL) {
		CResListValNode_ScalarDelete_NameTbl((CNameEntry *)node->data, 1);
		node->data = NULL;
	}
	node->data = newData;
}

/*
 * 0x004C26B0 - CResManager::NextIterInternal (defines)
 *
 * Advances ctx's key/val nodes one step.
 */
static void *
CResManager_NextIterInternal_Defines(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	uint32_t bucket = CSearchCtx_GetBucket(ctx);

	CResListNode *keyNode = (CResListNode *)CSearchCtx_GetKeyNode(ctx);
	keyNode = CResList_DirectionNext(rm->keys[bucket], keyNode, direction);
	CSearchCtx_SetKeyNode(&result, (uintptr_t)keyNode);

	CResListNode *valNode = (CResListNode *)CSearchCtx_GetValNode(ctx);
	valNode = CResList_DirectionNext(rm->vals[bucket], valNode, direction);
	CSearchCtx_SetValNode(&result, (uintptr_t)valNode);

	bucket = CSearchCtx_GetBucket(ctx);
	CSearchCtx_SetBucket(&result, bucket);

	CResListNode *checkKey = (CResListNode *)CSearchCtx_GetKeyNode(&result);
	bucket = CSearchCtx_GetBucket(&result);
	int valid = CResList_IsValid(rm->keys[bucket], checkKey);
	CSearchCtx_SetEntity(&result, valid);

	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C2790 - CResManager::NextIterInternal (labels)
 *
 * Advances ctx's key/val nodes one step.
 */
static void *
CResManager_NextIterInternal_Labels(CResManager *rm, CSearchCtx *output, CSearchCtx *ctx, int direction)
{
	CSearchCtx result;

	CSearchCtx_Constructor(&result);

	if (!CSearchCtx_Find(ctx)) {
		CResManager_CreateBucket(output, &result);
		return output;
	}

	uint32_t bucket = CSearchCtx_GetBucket(ctx);

	CResListNode *keyNode = (CResListNode *)CSearchCtx_GetKeyNode(ctx);
	keyNode = CResList_DirectionNext(rm->keys[bucket], keyNode, direction);
	CSearchCtx_SetKeyNode(&result, (uintptr_t)keyNode);

	CResListNode *valNode = (CResListNode *)CSearchCtx_GetValNode(ctx);
	valNode = CResList_DirectionNext(rm->vals[bucket], valNode, direction);
	CSearchCtx_SetValNode(&result, (uintptr_t)valNode);

	bucket = CSearchCtx_GetBucket(ctx);
	CSearchCtx_SetBucket(&result, bucket);

	CResListNode *checkKey = (CResListNode *)CSearchCtx_GetKeyNode(&result);
	bucket = CSearchCtx_GetBucket(&result);
	int valid = CResList_IsValid(rm->keys[bucket], checkKey);
	CSearchCtx_SetEntity(&result, valid);

	CResManager_CreateBucket(output, &result);
	return output;
}

/*
 * 0x004C2870 - CResList::BeginIter (labels val)
 *
 * Returns the tail, recycling it via CResList_RecycleNodeJ, or allocates a
 * fresh node as head/tail when the list is empty.
 */
CResListNode *
CResList_BeginIterJ(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_RecycleNodeJ(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x004C2920 - CResList::~CResList (templates)
 *
 * Delegates to CResList_ClearAll_NameTable.
 */
static void
CResList_Destructor_TemplatesVariant(CResList *this)
{
	CResList_ClearAll_NameTable(this);
}

/*
 * 0x004C2940 - CResList::RemoveAll
 *
 * Iterates the list and frees every node via CResList_EraseAndFree_Labels.
 */
static void
CResList_RemoveAll(CResList *this)
{
	CResList *list = this;
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_EraseAndFree_Labels(list, node, 1);
	}
}

/*
 * 0x004C2980 - CResList::FindOrAddPair
 *
 * Tail-allocates a node and stores srcData in it.
 */
CResListNode *
CResList_FindOrAddPair(CResList *list, void *srcData)
{
	CResListNode *node;

	node = CResList_AllocTailNode_TemplatesKey(list);
	if (node != NULL)
		CResList_AllocForward_LabelsWrapper(node, srcData);
	return node;
}

/*
 * 0x004C29B0 - CResList::AllocForward (labels)
 *
 * Allocates a tail node and stores srcData in it.
 */
CResListNode *
CResList_AllocForward_Labels(CResList *list, void *srcData)
{
	CResListNode *node;

	node = CResList_AllocTailNode_LabelsVal(list);
	if (node != NULL)
		CResList_AllocForward_LabelsWrapper(node, srcData);
	return node;
}

/*
 * 0x004C2B10 - CResManager val list ScalarDelete (name table)
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
static void *
CResListValNode_ScalarDelete_NameTbl(CNameEntry *this, int flags)
{
	CResList_ValNodeDestructor_NameTable(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004C2B40 - CNameEntry::~CNameEntry
 *
 * Destructs the three embedded CResLists (male/female/other name lists).
 * The binary uses the vector destructor helper across 3 x 12-byte elements.
 */
static void
CResList_ValNodeDestructor_NameTable(CNameEntry *this)
{
	CResList_DestructorA(&this->lists[2]);
	CResList_DestructorA(&this->lists[1]);
	CResList_DestructorA(&this->lists[0]);
}

/*
 * 0x004C2B60 - CResList::RecycleNode (templates val)
 *
 * Allocates a new node and splices it in after afterNode.
 */
static CResListNode *
CResList_RecycleNodeF(CResList *list, CResListNode *afterNode)
{
	CResListNode *head, *newNode;

	if (afterNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x004C2C50 - CResList::EraseAndFree (templates val)
 *
 * Unlinks node via CResList_UnlinkNodeF, deletes its CTemplate data, and
 * returns the next node.
 */
CResListNode *
CResList_ReplaceDataJ2(CResList *list, CResListNode *node, int direction)
{
	void *outData;
	CResListNode *result;

	result = CResList_UnlinkNodeF(list, node, &outData, direction);
	if (outData != NULL)
		CTemplate_ScalarDelete(outData, 1);
	return result;
}

/*
 * 0x004C2CB0 - CResList::RecycleNode (name table val)
 *
 * Allocates a new node and splices it in after afterNode.
 */
static CResListNode *
CResList_RecycleNodeG(CResList *list, CResListNode *afterNode)
{
	CResListNode *head, *newNode;

	if (afterNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x004C2DA0 - CResList::EraseAndFree (name table val)
 *
 * Unlinks node via CResList_UnlinkNodeG, deletes its value-node data, and
 * returns the next node.
 */
CResListNode *
CResList_ReplaceData_G(CResList *list, CResListNode *node, int direction)
{
	void *outData;
	CResListNode *result;

	result = CResList_UnlinkNodeG(list, node, &outData, direction);
	if (outData != NULL)
		CResListValNode_ScalarDelete_NameTbl((CNameEntry *)outData, 1);
	return result;
}

/*
 * 0x004C2E00 - CResList::RecycleNode (labels val)
 *
 * Allocates a new node and splices it in after afterNode.
 */
static CResListNode *
CResList_RecycleNodeJ(CResList *list, CResListNode *afterNode)
{
	CResListNode *head, *newNode;

	if (afterNode == NULL)
		return NULL;

	head = CResList_Begin((CResList *)afterNode);

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, head);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (head != NULL) {
		CResListNode_SetPrev(head, newNode);
	} else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}
	list->count++;
	return newNode;
}

/*
 * 0x004C2EF0 - CResList clear-all for name table
 *
 * Iterates and erases every node via CResList_EraseAndFree_K.
 */
static void
CResList_ClearAll_NameTable(CResList *this)
{
	CResList *list = this;
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_EraseAndFree_K(list, node, 1);
	}
}

/*
 * 0x004C2F30 - CResList::AllocTailNode (templates key)
 *
 * Inserts a new node before head, or creates the first node when empty.
 */
static CResListNode *
CResList_AllocTailNode_TemplatesKey(CResList *list)
{
	CResListNode *node;

	if (list->head != NULL)
		return CResList_InsertBeforeJ(list, list->head);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->head = node;
	list->tail = list->head;
	list->count++;
	return list->head;
}

/*
 * 0x004C2FE0 - CResList::AllocTailNode (labels val)
 *
 * Recycles tail via CResList_AllocTailNodeK, or creates the first node when
 * empty.
 */
static CResListNode *
CResList_AllocTailNode_LabelsVal(CResList *list)
{
	CResListNode *node;

	if (list->tail != NULL)
		return CResList_AllocTailNodeK(list, list->tail);

	node = (CResListNode *)malloc(sizeof(CResListNode));
	if (node != NULL)
		CResListNode_Constructor_bin(node);

	list->tail = node;
	list->head = list->tail;
	list->count++;
	return list->tail;
}

/*
 * 0x004C3090 - CResList::EraseAndFree (labels)
 *
 * Unlinks node via CResList_UnlinkNodeJ2, deletes its CStringPairListNode
 * data, and returns the neighbor chosen by direction.
 */
static CResListNode *
CResList_EraseAndFree_Labels(CResList *list, CResListNode *node, int direction)
{
	void *outData = NULL;
	CResListNode *result;

	result = CResList_UnlinkNodeJ2(list, node, &outData, direction);

	if (outData != NULL)
		CStringPairListNode_ScalarDelete(outData, 1);

	return result;
}

/*
 * 0x004C30F0 - CResList::AllocForward (labels wrapper)
 *
 * Frees any existing CStringPairListNode in node->data and stores newData.
 */
static void
CResList_AllocForward_LabelsWrapper(CResListNode *node, void *newData)
{
	if (node->data != NULL) {
		CStringPairListNode_ScalarDelete(node->data, 1);
		node->data = NULL;
	}
	node->data = newData;
}

/*
 * 0x004C3150 - CStringPairListNode ScalarDelete
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
__attribute__((unused)) void *
CStringPairListNode_ScalarDelete(CStringPairListNode *this, int flags)
{
	CStringPairListNode_Destructor(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004C3180 - CStringPairListNode::~CStringPairListNode
 *
 * Delegates to CStringPairListNode_FieldDestructor.
 */
static void
CStringPairListNode_Destructor(CStringPairListNode *this)
{
	CStringPairListNode_FieldDestructor(this);
}

/*
 * 0x004C31A0 - CStringPairListNode field destructor
 *
 * Destroys second then first CString fields.
 */
static void
CStringPairListNode_FieldDestructor(CStringPairListNode *this)
{
	CString_Destructor(&this->second);
	CString_Destructor(&this->first);
}

/*
 * 0x004C31F0 - CResList::UnlinkNode (templates val)
 *
 * Unlinks node, hands its data to *outData, frees the node, and returns the
 * neighbor chosen by direction.
 */
static CResListNode *
CResList_UnlinkNodeF(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeF_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004C32C0 - CResList::UnlinkNode (name table val)
 *
 * Unlinks node, hands its data to *outData, frees via
 * CResListNodeG_ScalarDelete, and returns the neighbor chosen by direction.
 */
static CResListNode *
CResList_UnlinkNodeG(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeG_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004C3390 - CResList::EraseAndFree (default variant K)
 *
 * Unlinks node via CResList_UnlinkNode_K and frees its data.
 */
static CResListNode *
CResList_EraseAndFree_K(CResList *list, CResListNode *node, int direction)
{
	void *outData = NULL;
	CResListNode *result;

	result = CResList_UnlinkNode_K(list, node, &outData, direction);

	if (outData != NULL)
		free(outData);

	return result;
}

/*
 * 0x004C33E0 - CResList::IsValid
 *
 * Returns 1 if node is non-NULL (iterator not past end).
 */
int
CResList_IsValid(CResList *list, CResListNode *node)
{
	USED(list);
	return node != NULL;
}

/*
 * 0x004C3400 - CResList::InsertBefore (labels)
 *
 * Allocates a new node and splices it in before afterNode.
 */
static CResListNode *
CResList_InsertBeforeJ(CResList *list, CResListNode *afterNode)
{
	CResListNode *newNode, *prev;

	if (afterNode == NULL)
		return NULL;

	prev = afterNode->prev;

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetPrev(newNode, prev);
	CResListNode_SetNext(newNode, afterNode);
	CResListNode_SetPrev(afterNode, newNode);

	if (prev != NULL)
		CResListNode_SetNext(prev, newNode);
	else {
		list->head = newNode;
		if (list->tail == NULL)
			list->tail = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x004C34F0 - CResList::AllocTailNode (default variant K)
 *
 * Allocates a new node and splices it in after afterNode.
 */
static CResListNode *
CResList_AllocTailNodeK(CResList *list, CResListNode *afterNode)
{
	CResListNode *newNode, *next;

	if (afterNode == NULL)
		return NULL;

	next = afterNode->next;

	newNode = (CResListNode *)malloc(sizeof(CResListNode));
	if (newNode != NULL)
		CResListNode_Constructor_bin(newNode);

	CResListNode_SetNext(newNode, next);
	CResListNode_SetPrev(newNode, afterNode);
	CResListNode_SetNext(afterNode, newNode);

	if (next != NULL)
		CResListNode_SetPrev(next, newNode);
	else {
		list->tail = newNode;
		if (list->head == NULL)
			list->head = newNode;
	}

	list->count++;
	return newNode;
}

/*
 * 0x004C35E0 - CResList::UnlinkNode (labels val)
 *
 * Unlinks node, hands its data to *outData, frees via
 * CResListNodeJ2_ScalarDelete, and returns the neighbor chosen by direction.
 */
static CResListNode *
CResList_UnlinkNodeJ2(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = CResList_GetTail((CResList *)node);
	next = CResList_Begin((CResList *)node);

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	if (node != NULL)
		CResListNodeJ2_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004C36B0 - CResListNode::ScalarDelete (variant F)
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
static CResListNode *
CResListNodeF_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeF_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x004C36E0 - CResListNode::ScalarDelete (variant G)
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
static CResListNode *
CResListNodeG_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeG_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x004C3710 - CResListNode::ScalarDelete (variant J2)
 *
 * Scalar deleting destructor. Calls dtor then conditionally frees.
 */
static CResListNode *
CResListNodeJ2_ScalarDelete(CResListNode *node, int flags)
{
	CResListNodeJ2_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * 0x004C3740 - CResListNode::~CResListNode (variant F)
 *
 * Frees node->data as a CTemplate when non-NULL.
 */
static void
CResListNodeF_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CTemplate_ScalarDelete(node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x004C3790 - CResListNode::~CResListNode (variant G)
 *
 * Frees node->data as a name-table value node when non-NULL.
 */
static void
CResListNodeG_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CResListValNode_ScalarDelete_NameTbl((CNameEntry *)node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x004C37E0 - CResList::UnlinkNode (default variant K)
 *
 * Unlinks node, hands its data to *outData, frees via
 * CResListNode_ScalarDelete, and returns the neighbor chosen by direction.
 */
static CResListNode *
CResList_UnlinkNode_K(CResList *list, CResListNode *node, void **outData, int direction)
{
	CResListNode *prev, *next;

	*outData = NULL;
	if (node == NULL)
		return NULL;

	prev = node->prev;
	next = node->next;

	if (prev != NULL)
		CResListNode_SetNext(prev, next);
	else
		list->head = next;

	if (next != NULL)
		CResListNode_SetPrev(next, prev);
	else
		list->tail = prev;

	*outData = CResListNode_SwapData(node, NULL);

	CResListNode_ScalarDelete(node, 1);

	list->count--;

	if (direction == 1)
		return next;
	return prev;
}

/*
 * 0x004C38B0 - CResListNode::~CResListNode (variant J2)
 *
 * Frees node->data as a CStringPairListNode when non-NULL.
 */
static void
CResListNodeJ2_Destructor(CResListNode *node)
{
	if (node->data != NULL) {
		CStringPairListNode_ScalarDelete(node->data, 1);
		node->data = NULL;
	}
}

/*
 * 0x004C3900 - CResListNode::SwapData
 *
 * Swaps node->data with newData and returns the old pointer.
 */
void *
CResListNode_SwapData(CResListNode *node, void *newData)
{
	void *old = node->data;
	node->data = newData;
	return old;
}

/*
 * Helper - CResManager_InsertInt
 *
 * Hash key, ensure bucket CLists exist (heap-alloc on first use),
 * malloc 4-byte key block, prepend to key chain, prepend value to val chain.
 * Note: 0x0045F560 is CResListNode::SetDataInt, not this function.
 */
void
CResManager_InsertInt(CResManager *rm, uint32_t key, void *value)
{
	uint32_t bucket = ResManager_HashInt(key, 0x41);
	uint32_t *keyCopy;

	if (rm->keys[bucket] == NULL) {
		rm->keys[bucket] = (CResList *)calloc(1, sizeof(CResList));
		rm->vals[bucket] = (CResList *)calloc(1, sizeof(CResList));
	}

	keyCopy = (uint32_t *)malloc(sizeof(uint32_t));
	*keyCopy = key;
	CResList_PrependNode(rm->keys[bucket], keyCopy);
	CResList_PrependNode(rm->vals[bucket], value);
	rm->count++;
}

/*
 * Helper - CResList_PrependNode
 *
 * Prepends a new node with data to the front of a CResList.
 */
void
CResList_PrependNode(CResList *list, void *data)
{
	CResListNode *node = (CResListNode *)malloc(sizeof(CResListNode));
	node->next = NULL;
	node->prev = NULL;
	node->data = data;
	if (list->head == NULL) {
		list->head = node;
		list->tail = node;
	} else {
		node->next = list->head;
		list->head->prev = node;
		list->head = node;
	}
	list->count++;
}

/*
 * Helper - CResManager_FindInt
 *
 * Hashes key, walks the bucket's key/val chains in lockstep, and returns
 * the value for the first matching int key.
 */
void *
CResManager_FindInt(CResManager *rm, uint32_t key)
{
	uint32_t bucket = ResManager_HashInt(key, 0x41);
	CResListNode *kn, *vn;

	if (rm->keys[bucket] == NULL)
		return NULL;

	kn = rm->keys[bucket]->head;
	vn = rm->vals[bucket]->head;
	while (kn != NULL) {
		if (*(uint32_t *)kn->data == key)
			return vn->data;
		kn = kn->next;
		vn = vn->next;
	}
	return NULL;
}

/*
 * Helper - CSearchCtx::GetKeyNode
 *
 * Gets keyNode field (+0x08). Symmetric to the binary's
 * CSearchCtx::GetBucket / CSearchCtx::GetValNode accessors; decomposes
 * the inlined ctx->keyNode reads scattered through res.c iterators.
 */
uintptr_t
CSearchCtx_GetKeyNode(CSearchCtx *ctx)
{
	return ctx->keyNode;
}
