#ifndef RES_H_
#define RES_H_

#include <stdint.h>

__extension__ typedef struct CEntityMap CEntityMap;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CString CString;
__extension__ typedef struct CStringList CStringList;
__extension__ typedef struct CStringPairListNode CStringPairListNode;
__extension__ typedef struct CMagicItemListNode CMagicItemListNode;
/*
 * Iterator/lookup cursor for CResManager (0x10 bytes, ctor 0x0043ED50).
 * CSearchCtx_Find (0x00421000) reads the entity/found pointer.
 */
__extension__ typedef struct CSearchCtx CSearchCtx;
struct CSearchCtx {
	uint32_t entity;   // +0x00
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;   // 64-bit alignment pad
#endif
	uintptr_t bucket;  // +0x04
	uintptr_t keyNode; // +0x08
	uintptr_t valNode; // +0x0C
};

/*
 * Doubly-linked node used by CResManager chains (0x0C bytes, ctor
 * 0x00440B10). Distinct from CListNode in list.h, which has an extra
 * typeTag/value pair.
 */
__extension__ typedef struct CResListNode CResListNode;
struct CResListNode {
	CResListNode *next;  // +0x00
	CResListNode *prev;  // +0x04
	void *data;          // +0x08
};

/*
 * List head for CResListNode chains (0x0C bytes).
 */
__extension__ typedef struct CResList CResList;
struct CResList {
	CResListNode *head;  // +0x00
	CResListNode *tail;  // +0x04
	int count;           // +0x08
};

#define CRESMANAGER_BUCKETS 66

/*
 * Hash table with 66 parallel key/value bucket chains (0x218 bytes).
 */
__extension__ typedef struct CResManager CResManager;
// clang-format off
struct CResManager {
	int flags;                              // +0x00
	int count;                              // +0x04
	CResList *keys[CRESMANAGER_BUCKETS];    // +0x08
	CResList *vals[CRESMANAGER_BUCKETS];    // +0x110
};
// clang-format on

// Forward declare CString for CResManager_FindContainer.
struct CString;

uintptr_t CSearchCtx_GetBucket(CSearchCtx *ctx); // 0x00401450
int CSearchCtx_Find(CSearchCtx *ctx); // 0x00421000
void *CIterCtx_Set(void *ctx, void *arg); // 0x00423250
CResListNode *CResList_Begin(CResList *list); // 0x004238D0
void CResList_Destructor_ByNameAllVal(CResList *this); // 0x004238E0
void CResListNode_SetNext(CResListNode *node, CResListNode *next); // 0x00424320
CSearchCtx *CResManager_BeginIterWrapper(CResManager *rm, CSearchCtx *output); // 0x00430260
CSearchCtx *CResManager_NextIterWrapper(CResManager *rm, CSearchCtx *output, CSearchCtx *current); // 0x00430280
void CResManager_Destructor_Hint(CResManager *this); // 0x00464380
void *CResManager_BeginIterInternalHint(CResManager *rm, CSearchCtx *output, uint32_t *key, int direction); // 0x004644F0
void CResManager_BeginSearchHint(CResManager *rm, CSearchCtx *output, int startBucket, int direction); // 0x00464730
void *CResManager_NextEntry_Hint(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x00464910
void *CResManager_EraseEntry_Hint(CResManager *rm, CSearchCtx *output, CSearchCtx *current, void *outErased, int direction); // 0x00464A60
char EntityMap_ForEachValidateZ(uintptr_t *first, uintptr_t *end, char cmp); // 0x004302E0
void CEntityMap_ValidateEntityZ(CEntityMap *this, CItem *entity); // 0x00430310
void SortBySerial_Entry(uintptr_t *first, uintptr_t *last, char cmp); // 0x004303C0
char EntityMap_ForEachValidateSerialZ(uintptr_t *first, uintptr_t *end, char cmp); // 0x004303F0
void CEntityMap_ValidateSerialZ(CEntityMap *this, uint32_t serial); // 0x00430420
void *CResList_EraseAndFree_ByNameAllVal(CResList *list, CResListNode *node, int direction); // 0x00423B40
void *CResList_InsertAndStore_ByNameAll(CResList *list, CResListNode *position, uint32_t *valuePtr); // 0x00430460
void *CResList_AppendAndStore_ByNameAll(CResList *list, uint32_t *valuePtr); // 0x004304A0
void *CResList_GetOrCreateTail_ByNameAll(CResList *list); // 0x00430810
int CEntityMap_CompareLocation(CEntityMap *this, CItem *a, CItem *b); // 0x00430C20
int CEntityMap_CompareBySerial(CEntityMap *this, uint32_t serialA, uint32_t serialB); // 0x00430E20
CStringList *CStringList_ScalarDelete(CStringList *sl, int flags); // 0x0043D580
CResList *CResList_ScalarDeleteA(CResList *list, int flags); // 0x0043D5B0
CStringList *CStringList_ScalarDeleteEffects(CStringList *sl, int flags); // 0x0043D5E0
int CStringList_HasEntries(CStringList *sl); // 0x0043D610
void CStringList_Init(CStringList *sl); // 0x0043D630
void CStringList_Destroy(CStringList *sl); // 0x0043D650
void *CStringList_BeginIter(CStringList *sl, CResListNode **output); // 0x0043D670
void *CStringList_AdvanceIter(CStringList *sl, CResListNode **outNode, CResListNode **iterNode); // 0x0043D6B0
void *CStringList_GetNodeData(CStringList *sl, CResListNode **nodePtr); // 0x0043D6F0
void *CStringList_AddWeighted(CStringList *sl, CResListNode **output, CString *str, int weight, int direction); // 0x0043D710
int CStringList_GetTypeInclude(CStringList *sl, CResListNode **nodePtr); // 0x0043D7D0
void CStringList_SetTypeInclude(CStringList *sl, CResListNode **nodePtr, int newType); // 0x0043D7F0
void CStringList_SetAllTypesInclude(CStringList *sl, int newType); // 0x0043D870
void *CStringList_GetWeightedRandom(CStringList *sl, void *output); // 0x0043D920
void CResList_ClearInternal(CResList *list); // 0x0043DA80
void CResList_DestructorA(CResList *list); // 0x0043DAC0
CResListNode *CResList_InsertTailStr(CResList *list, CString *name); // 0x0043DAE0
void CResList_AddResultEntry(CResList *list, CString *name, int flag); // 0x0043DB10
void CResList_InsertStrCopy(CResList *list, CString *str, int direction, int orderedFlag); // 0x0043DB40
CResListNode *CResList_FindByStrSorted(CResList *list, CString *key, CResListNode *startNode, int direction, int orderedFlag); // 0x0043DBD0
void CResList_InsertTailDataB(CResList *magicList, CMagicItemListNode *name); // 0x0043DC80
void CResBook_ScalarDtor(CResManager *this); // 0x0043DCB0
void CResManagerS_ScalarDtor(CResManager *this); // 0x0043E0F0
void CResManagerC_ScalarDtor(CResManager *this); // 0x0043E480
void CResManagerT_ScalarDtor(CResManager *this); // 0x0043E890
void CResList_DestructorSLN(CResList *list); // 0x0043F3B0
void CResManager_ClearJ_Thunk(CResManager *this); // 0x004C0F50
void CResManager_ClearI(CResManager *rm); // 0x0043DCD0
CSearchCtx *CResManager_FindOrInsertInclude(CResManager *rm, CSearchCtx *output, CString *key); // 0x0043DDB0
void *CResManager_FindByStrCtxA(CResManager *rm, CSearchCtx *output, const char *key, int direction); // 0x0043DF40
void CResManager_InsertValueAtCtxI(CResManager *rm, CSearchCtx *ctx, void *value); // 0x0043E000
void CResManager_BeginIterA(CResManager *rm, CSearchCtx *output); // 0x0043E040
void CResManager_NextIterA(CResManager *rm, CSearchCtx *output, CSearchCtx *current); // 0x0043E060
void *CResManager_FindEntryA(CResManager *rm, CString *name); // 0x0043E090
void CResManager_Constructor(CResManager *rm, int flags); // 0x0043E0C0
void CResManager_ClearS(CResManager *rm); // 0x0043E110
CSearchCtx *CResManager_FindOrInsertSecond(CResManager *rm, CSearchCtx *output, CString *key); // 0x0043E1F0
void *CResManager_FindByStrCtxD(CResManager *rm, CSearchCtx *output, const char *key, int direction); // 0x0043E380
void CResManager_InsertValueAtCtxS(CResManager *rm, CSearchCtx *ctx, void *value); // 0x0043E440
void CResManager_ClearC(CResManager *rm); // 0x0043E4A0
CSearchCtx *CResManager_FindOrInsertC(CResManager *rm, CSearchCtx *output, CString *key); // 0x0043E580
void *CResManager_FindByStrCtxC(CResManager *rm, CSearchCtx *output, const char *key, int direction); // 0x0043E710
void CResManager_InsertValueAtCtxC(CResManager *rm, CSearchCtx *ctx, void *value); // 0x0043E7D0
void CResManager_ClearT(CResManager *rm); // 0x0043E8D0
CSearchCtx *CResManager_FindOrInsertEffects(CResManager *rm, CSearchCtx *output, CString *key); // 0x0043EA30
void *CResManager_FindByStrCtxB(CResManager *rm, CSearchCtx *output, const char *key, int direction); // 0x0043EC00
void CResManager_InsertValueAtCtxEffects(CResManager *rm, CSearchCtx *ctx, void *value); // 0x0043ECC0
void CResManager_BeginIterB(CResManager *rm, CSearchCtx *output); // 0x0043ED00
void CResManager_NextIterB(CResManager *rm, CSearchCtx *output, CSearchCtx *current); // 0x0043ED20
void CSearchCtx_Constructor(CSearchCtx *ctx); // 0x0043ED50
void CSearchCtx_Add(CSearchCtx *dst, CSearchCtx *src); // 0x0043ED90
void CStringList_SetWeight(CStringList *sl, CResListNode **nodePtr, int weight); // 0x0043EE10
int CStringList_GetTypeEffects(CStringList *sl, CResListNode **nodePtr); // 0x0043EE60
void CStringList_SetTypeEffects(CStringList *sl, CResListNode **nodePtr, int newType); // 0x0043EE80
void CStringList_SetAllTypesEffects(CStringList *sl, int newType); // 0x0043EF00
void *CStringList_GetWeightedRandomEntryEffects(CStringList *sl, void *output); // 0x0043EFB0
void *CStringList_AllocIterNode(CStringList *sl, CResListNode **output, int weight, int direction); // 0x0043F110
void *CStringList_GetValueEffects(CStringList *sl, CResListNode **nodePtr); // 0x0043F390
CResListNode *CResList_AllocTailNodeA(CResList *list); // 0x0043F400
CResListNode *CResList_FreeNodeA(CResList *list, CResListNode *node, int direction); // 0x0043F4B0
CResListNode *CResList_InsertAfterTailA(CResList *list, CString *str); // 0x0043F510
CResListNode *CResList_FindByString(CResList *list, void *keyStr, CResListNode *startNode, int direction); // 0x0043F540
void CResListNode_SetString(CResListNode *node, CString *src); // 0x0043F670
CResListNode *CResList_AllocTailNodeB(CResList *list); // 0x0043F700
void CResListNode_InsertDataB(CResListNode *node, void *srcData); // 0x0043F7B0
void CResManager_SearchBucketA(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction); // 0x0043F840
void CResManager_BeginIterInternalA(CResManager *rm, CSearchCtx *output, int startBucket, int direction); // 0x0043F8F0
void CResManager_NextIterInternalA(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x0043F9F0
void *CResManager_CreateBucket(CSearchCtx *dst, CSearchCtx *src); // 0x0043FC30
void CResManager_SearchBucketB(CResManager *rm, CSearchCtx *output, const char *key, CSearchCtx *startCtx, int direction); // 0x0043FF90
void CResManager_BeginIterInternalB(CResManager *rm, CSearchCtx *output, int startBucket, int direction); // 0x00440040
void CResManager_NextIterInternalB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x00440140
CResListNode *CResList_DirectionBegin(CResList *list, int direction); // 0x00440380
void CResList_EraseAllSLN(CResList *list); // 0x00440450
CResListNode *CResList_AllocAndSetDataNP(CResList *list, void *data); // 0x004404C0
CResListNode *CResList_RecycleNodeA(CResList *list, CResListNode *afterNode); // 0x004404F0
CResListNode *CResList_RecycleNodeB(CResList *list); // 0x004405E0
CResListNode *CResList_UnlinkNode(CResList *list, CResListNode *node, void **outData, int direction); // 0x004406D0
CResListNode *CResList_RecycleNodeC(CResList *list, CResListNode *afterNode); // 0x004407D0
void *CResManager_GetKeyAtPos(CResManager *rm, CSearchCtx *ctx); // 0x004408C0
CResListNode *CResListNode_Constructor_bin(CResListNode *node); // 0x00440B10
CResListNode *CResList_RecycleNodeD(CResList *list, CResListNode *afterNode); // 0x00440EF0
CResListNode *CResList_InsertBeforeNode(CResList *list, CResListNode *beforeNode); // 0x00441610
CResListNode *CResList_Prev(CResList *list, CResListNode *node); // 0x00441830
void CResManager_InsertByRef(CResList *list, uintptr_t val); // 0x0045EC0D
void CResList_RemoveByValue(CResList *list, uintptr_t value); // 0x0045EC26
CResListNode *CResList_EraseAndFree_Spawn(CResList *list, CResListNode *node, int flag); // 0x0045F290
CResListNode *CResList_FindByValue(CResList *list, void *keyPtr, CResListNode *startNode, int direction); // 0x0045F330
CResListNode *CResList_Erase_Spawn(CResList *list, CResListNode *node, void **outData, int direction); // 0x0045F450
int CResManager_InsertIntEntryG(CResManager *rm, void *keyPtr, void *valPtr); // 0x004C0AF0
void CResManager_InsertInt(CResManager *rm, uint32_t key, void *value); // 0x0045F560
void CResListNode_SetDataInt(CResListNode *node, void *srcPtr); // 0x0045F560
uint32_t ResManager_HashInt(uint32_t key, uint32_t bucketCount); // 0x004639C0
uint32_t ResManager_HashStr(const char *str, uint32_t bucketCount); // 0x004639D4
uint32_t ResManager_HashStrA(CString *str, int bucketCount); // 0x00463A5B
CResListNode *CResList_KeyInsert(CResList *list, void *data); // 0x00464BE0
void *CResList_InsertOrSetDataHint(CResList *list, void *data); // 0x00464C10
void *ScalarDestructor_KeyNode(CResList *this, int flag); // 0x00464C40
void *CResListValNode_ScalarDelete_Hint(CResList *this, int flags); // 0x00464C70
CResListNode *CResList_AllocTailNodeHintVal(CResList *list); // 0x00464D80
void *CResList_RemoveKeyNode(CResList *list, uintptr_t keyNode, uint32_t direction); // 0x00464E30
void CResList_Destructor_HintKey(CResList *this); // 0x00464E80
void CResList_ValNodeDestructor_Hint(CResList *this); // 0x00465020
CResListNode *CResListNode_ScalarDelete(CResListNode *node, int flags); // 0x00465450
CResListNode *CResList_Next(CResList *list, CResListNode *node); // 0x00466C80
void CResList_ClearInternal_MagicItemList_rb(CResList *this); // 0x00466CA0
void CResList_Destructor_MagicItemList(CResList *this); // 0x00466CE0
void CStringPairList_Destructor(CResList *this); // 0x004C1D10
CResListNode *CResList_EraseAndFree_MagicStr(CResList *list, CResListNode *node, int direction); // 0x00466DD0
void *CResList_GetHeadIfNotNull(CResList *this, CResListNode *list); // 0x00466E80
int SearchCtx_IsEqual(CSearchCtx *a, CSearchCtx *b); // 0x004730A0
CResListNode *CResList_RemoveAndFree(CResList *list, CResListNode *node, int direction); // 0x00473EB0
void CResList_Destructor(CResList *list); // 0x00473F00
void CResList_InsertBack(CResList *list, void *dataPtr); // 0x00473F20
void CResList_EraseAll(CResList *list); // 0x004740D0
CResListNode *CResList_DirectionNext(CResList *list, CResListNode *node, int direction); // 0x00474110
void CResList_DestructorEmpty(CResList *this); // 0x00474468
void CResManager_ClearMultiA_Thunk(CResManager *rm); // 0x0047A4F0
int CResManager_FindOrInsertMultiA(CResManager *rm, uint32_t *keyPtr, void *valuePtr); // 0x0047A5F0
CSearchCtx *CResManager_FindByKey_A(CResManager *this, CSearchCtx *output, uint32_t *keyPtr, int direction); // 0x0047A740
void *CResManager_GetResultCtx(CResManager *rm, CSearchCtx *ctx); // 0x0047A800
void *CResManager_GetResult(CResManager *rm, CSearchCtx *ctx); // 0x0047A800
CSearchCtx *CResManager_CreateOrFind_R(CResManager *this, CSearchCtx *output, CSearchCtx *current, int direction); // 0x0047A840
CSearchCtx *CResManager_BeginIter_MultiA(CResManager *this, CSearchCtx *output); // 0x0047A8C0
CSearchCtx *CResManager_NextIter_MultiA(CResManager *this, CSearchCtx *output, CSearchCtx *current); // 0x0047A8E0
void CResManager_ClearMultiB_Thunk(CResManager *rm); // 0x0047A910
CSearchCtx *CResManager_FindOrInsert_B(CResManager *this, CSearchCtx *output, void *keyPtr, int direction); // 0x0047AA80
void *CResManager_EraseMultiB(CResManager *rm, CSearchCtx *output, CSearchCtx *current, void **outData, int direction); // 0x0047AB40
void CResList_Destructor_MultiA(CResList *list); // 0x0047BA00
void CResList_Destructor_MultiB(CResList *list); // 0x0047BA50
void CResList_Destructor_MultiVal(CResList *list); // 0x0047BE50
CResListNode *CResList_EraseAndFree_MultiA(CResList *list, CResListNode *node, int direction); // 0x0047C1B0
CResListNode *CResList_Erase_MultiC(CResList *list, CResListNode *node, void **outData, int direction); // 0x0047CCE0
void CResListNode_SetPrev(CResListNode *node, CResListNode *prev); // 0x0047D4A0
void CResManager_IntKeyDestructor_ByFile(CResManager *rm); // 0x004A6870
int CResManager_Insert_ByFile(CResManager *rm, uint32_t *keyPtr, void *value); // 0x004A6890
CSearchCtx *CResManager_FindByKey_ByFile(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr); // 0x004A69E0
CSearchCtx *CResList_BeginIter_ByFile(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr, uint32_t direction); // 0x004A6B70
void CResList_GetResultNextIter_ByFile(CResManager *rm, CSearchCtx *iterCtx, uint32_t direction); // 0x004A6C30
CSearchCtx *CResList_EraseAndFree_ByFile(CResManager *rm, CSearchCtx *output, CSearchCtx *current, uint32_t direction); // 0x004A6C70
void CResManager_StrKeyDestructor_ByNameAll(CResManager *rm); // 0x004A6CF0
int CResList_Insert_ByNameAll(CResManager *rm, uint32_t *keyPtr, void **valuePtr); // 0x004A6D10
CSearchCtx *CResList_BeginIter_ByNameAll(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr, uint32_t direction); // 0x004A6D80
CSearchCtx *CResList_EraseAndFree_ByNameAll(CResManager *rm, CSearchCtx *output, CSearchCtx *current, uint32_t direction); // 0x004A6E40
uintptr_t CSearchCtx_GetValNode(CSearchCtx *ctx); // 0x004A6FF0
void CSearchCtx_SetEntity(CSearchCtx *ctx, uint32_t val); // 0x004A7010
void CSearchCtx_SetValNode(CSearchCtx *ctx, uintptr_t val); // 0x004A7030
void *ScalarDestructor_ByNameAllVal(CResList *this, int flag); // 0x004A7930
void *CResListValNode_ScalarDelete_Region(CResListNode *this, int flags); // 0x004A7F70
CResListNode *CResList_GetTail(CResList *list); // 0x004A8130
void *CResList_DirectionIterInit(CResList *list, void *iterOut, int direction); // 0x004B3310
void CStringList_Invalidate(CStringList *sl); // 0x004B32F0
void *CStringList_DirectionAdvanceIter(CResList *list, CResListNode **outNode, CResListNode **iterNode, int direction); // 0x004B3350
int CResManager_HasByInt(CResManager *rm, uint32_t key); // 0x004BFB81
void CResManager_Destructor_Templates(CResManager *this); // 0x004C06E0
void CResManager_FindByIntCtx(CResManager *rm, CSearchCtx *output, uint32_t *keyPtr, int direction); // 0x004C0930
void CResManager_Destructor_NameTable(CResManager *this); // 0x004C09F0
void CResManager_Clear_NameTable(CResManager *this); // 0x004C0A10
void CResManager_Destructor_Defines(CResManager *this); // 0x004C0D20
void CResManager_Clear_Defines(CResManager *this); // 0x004C0D40
void CResManager_Clear_Templates(CResManager *this); // 0x004C0700
CSearchCtx *CResManager_FindContainer(CResManager *rm, CSearchCtx *output, struct CString *name, int flag); // 0x004C0FE0
void CResManager_Destructor(CResManager *rm); // 0x004C1300
int CResManager_InsertStrEntry(CResManager *rm, CString *key, void *value); // 0x004C1320
void CResManager_BeginIter(CResManager *rm, CSearchCtx *output); // 0x004C1390
void CResManager_NextIter(CResManager *rm, CSearchCtx *output, CSearchCtx *current); // 0x004C13B0
void *CResList_ScalarDelete_TemplatesVal(CResList *this, int flags); // 0x004C13E0
void *CResList_ScalarDelete_NameTableVal(CResList *this, int flags); // 0x004C1410
void CSearchCtx_SetKeyNode(CSearchCtx *ctx, uintptr_t val); // 0x004C1440
void CSearchCtx_SetBucket(CSearchCtx *ctx, uint32_t val); // 0x004C1460
void *CResManager_NextIterInternal_Templates(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction); // 0x004C1520
void CResManager_InitTables(CResManager *rm); // 0x004C15D0
void *CResManager_GetResult_Defines(CResManager *this, CSearchCtx *ctx); // 0x004C0D00
void *CResList_GetData(CResList *list, CResListNode *node); // 0x004C1670
void *CResManager_SearchBucket_DefinesStrB(CResManager *rm, CSearchCtx *output, void *searchKey, CSearchCtx *ctx, int direction); // 0x004C19D0
void CResManager_BeginIterInternalK(CResManager *rm, CSearchCtx *output, int startBucket, int direction); // 0x004C1D60
void CResManager_ClearK(CResManager *rm); // 0x004C1E60
CSearchCtx *CResManager_FindOrInsertK(CResManager *rm, CSearchCtx *output, CString *key); // 0x004C1F40
void *CResManager_NextIterInternalK(CResManager *rm, CSearchCtx *output, CSearchCtx *current, int direction); // 0x004C20D0
CResListNode *CResList_BeginIterJ(CResList *list); // 0x004C2870
CResListNode *CResList_FindOrAddPair(CResList *list, void *srcData); // 0x004C2980
CResListNode *CResList_AllocForward_Labels(CResList *list, void *srcData); // 0x004C29B0
CResListNode *CResList_ReplaceDataJ2(CResList *list, CResListNode *node, int direction); // 0x004C2C50
CResListNode *CResList_ReplaceData_G(CResList *list, CResListNode *node, int direction); // 0x004C2DA0
void *CStringPairListNode_ScalarDelete(CStringPairListNode *this, int flags); // 0x004C3150
int CResList_IsValid(CResList *list, CResListNode *node); // 0x004C33E0
void *CResListNode_SwapData(CResListNode *node, void *newData); // 0x004C3900
void CResList_PrependNode(CResList *list, void *data);
int CResManager_InsertStrEntry_Defines(CResManager *rm, CString *key, void *value); // 0x004C0E20
void *CResManager_FindByStr_Defines(CResManager *rm, CSearchCtx *output, CString *key, int direction); // 0x004C0E90
void *CResManager_Constructor_Templates(CResManager *rm); // 0x004C06B0
int CResManager_InsertIntEntryF(CResManager *rm, void *keyPtr, void *valPtr); // 0x004C07E0
void *CResManager_FindInt(CResManager *rm, uint32_t key);
uintptr_t CSearchCtx_GetKeyNode(CSearchCtx *ctx);

#endif /* RES_H_ */
