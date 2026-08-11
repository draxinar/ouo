#ifndef MULTI_H_
#define MULTI_H_

#include <stddef.h>
#include <stdint.h>

#include "wombat_exec.h"

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CResList CResList;
__extension__ typedef struct CResListNode CResListNode;
__extension__ typedef struct CResManager CResManager;
__extension__ typedef struct CSmartPtr CSmartPtr;
__extension__ typedef struct CVector CVector;
/*
 * Multi system - houses, boats, and multi-tile objects. A CItem with
 * multiPtr != NULL has a CMultiComponent attached; owners (houses,
 * boats) use the CMultiSlave subclass, slaves/pieces use the base.
 * Dispatch goes through a shared 4-slot vtable shown below.
 */

// CMultiComponent vtable slot indices.
#define MC_VT_DTOR     0  // scalar deleting destructor
#define MC_VT_IS_OWNER 1  // IsOwner()
#define MC_VT_CLEANUP  2  // NotifyOwnerRemoval / RemoveAllComponents
#define MC_VT_SAVE     3  // Save()

__extension__ typedef struct CMultiComponent CMultiComponent;
__extension__ typedef struct CMultiSlave CMultiSlave;

/*
 * Non-owner multi piece attached to a CItem (0x14 bytes, pool-allocated
 * from g_MultiComponentPool at 0x00699AF8). vtable slots dispatch
 * between component and slave behavior.
 */
struct CMultiComponent {
	void **vtable;          // 0x00
	uint32_t serial;        // 0x04 - save: "multislave"
	CLocation offset;       // 0x08 - save: "multioffset"
	uint8_t flags;          // 0x0E - save: "multiflags"; bit 0 valid
	uint8_t _pad0F;         // 0x0F
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4];      // 64-bit alignment pad
#endif
	CItem *ownerItem;       // 0x10
};

/*
 * Owner multi - house or boat root (0x30 bytes, extends CMultiComponent).
 * Owns the list of component serials and the multi-type metadata.
 */
struct CMultiSlave {
	CMultiComponent base;   // 0x00
	CVector components;     // 0x14 - uint32_t serials
	uint32_t carry;         // 0x24 - default 1
	int32_t typeId;         // 0x28 - default -1
	uint16_t range;         // 0x2C
	uint16_t _pad2E;        // 0x2E
};

/*
 * One tile in a multi-definition (0x1C bytes). Each CMultiDef owns a
 * vector of these describing the shape loaded from the multi data files.
 */
__extension__ typedef struct CMultiComponentDef CMultiComponentDef;
struct CMultiComponentDef {
	uint16_t bodyType;      // 0x00
	CLocation offset;       // 0x02
	uint32_t scriptHead;    // 0x08 - CVector allocator slot
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;        // 64-bit alignment pad
#endif
	void *scriptBegin;      // 0x0C - CVector _Myfirst
	void *scriptEnd;        // 0x10 - CVector _Mylast
	void *scriptCapEnd;     // 0x14 - CVector _Myend
	int32_t invisible;      // 0x18
};

/*
 * Multi-type definition value stored in g_MultiManager (0x1C bytes).
 * Holds the min/max bounding box plus the component table.
 */
__extension__ typedef struct CMultiDef CMultiDef;
struct CMultiDef {
	CLocation minExtent;    // 0x00
	CLocation maxExtent;    // 0x06
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4];      // 64-bit alignment pad
#endif
	CVector components;     // 0x0C
};

/*
 * Running bounding box used while loading a CMultiDef (0x24 bytes).
 * Each axis has an init flag; the first Update seeds min=max=loc,
 * subsequent calls extend toward the extremes.
 */
__extension__ typedef struct MultiExtentTracker MultiExtentTracker;
struct MultiExtentTracker {
	CLocation minExtent;    // 0x00
	CLocation maxExtent;    // 0x06
	int32_t minXSet;        // 0x0C
	int32_t minYSet;        // 0x10
	int32_t minZSet;        // 0x14
	int32_t maxXSet;        // 0x18
	int32_t maxYSet;        // 0x1C
	int32_t maxZSet;        // 0x20
};

/*
 * Two-corner bounding rect plus rotation argument used by
 * CMultiManager::RecycleMultiCheckRotate (0x10 bytes).
 */
__extension__ typedef struct CMultiRotateRect CMultiRotateRect;
__extension__ typedef struct CMultiRotateRect CLocationPair;
struct CMultiRotateRect {
	CLocation loc1;         // 0x00
	CLocation loc2;         // 0x06
	uint32_t rotation;      // 0x0C
};

/*
 * Fixed-point 16.16 cos/sin lookup for the 8 UO rotation directions
 * (0x40 bytes). Global instance at 0x00699b08 in the binary, filled
 * by the constructor at 0x00478610 and indexed by RotateLocation.
 */
__extension__ typedef struct CRotationTable CRotationTable;
struct CRotationTable {
	int32_t cos[8];         // 0x00 - cosine by direction 0-7
	int32_t sin[8];         // 0x20 - sine by direction 0-7
};

extern CRotationTable g_RotationTable;  // 0x00699B08
void CRotationTable_Constructor(CRotationTable *this);  // 0x00478610

// Helper - CMultiComponent_Destroy: vtable[0] scalar deleting destructor.
// flags & 1: free the object after destruction.
#define CMultiComponent_Destroy(mc, flags) ((void *(*)(CMultiComponent *, int))(uintptr_t)(mc)->vtable[MC_VT_DTOR])((mc), (flags))

// Helper - CMultiComponent_IsOwner: vtable[1] IsOwner dispatch.
// Returns 0 for CMultiComponent (non-owner), 1 for CMultiSlave (owner).
#define CMultiComponent_IsOwner(mc) ((int (*)(CMultiComponent *))(uintptr_t)(mc)->vtable[MC_VT_IS_OWNER])((mc))

/*
 * 0x004212E0 - CItem::GetMulti
 *
 * Returns item->multiPtr (CMultiComponent*).
 */
static inline CMultiComponent *
CItem_GetMulti(CItem *item)
{
	return (CMultiComponent *)item->multiPtr;
}

/*
 * 0x00472F80 - CMultiSlave::GetTypeId
 *
 * Returns the slave's typeId field.
 */
static inline int32_t
CMultiSlave_GetTypeId(CMultiSlave *ms)
{
	return ms->typeId;
}

/*
 * 0x00472FA0 - CMultiComponent::GetIterator
 *
 * Stores the slave-component vector's end pointer into *out via CIterCtx_Set.
 */
static inline void *
CMultiComponent_GetIterator(CVector *vec, void *out)
{
	return CIterCtx_Set(out, vec->end);
}

/*
 * 0x0047450E - CMulti::GetSerial
 *
 * Returns the serial field of a CMultiComponent.
 */
static inline uint32_t
CMulti_GetSerial(CMultiComponent *mc)
{
	return mc->serial;
}

/*
 * 0x00474535 - CMulti::GetOffset
 *
 * Returns pointer to the offset field of a CMultiComponent.
 */
static inline CLocation *
CMulti_GetOffset(CMultiComponent *mc)
{
	return &mc->offset;
}

/*
 * 0x00477F51 - CMultiComponent::GetStaticFlag
 *
 * Returns bit 0 of the flags byte.
 */
static inline int
CMultiComponent_GetStaticFlag(CMultiComponent *mc)
{
	return mc->flags & 1;
}

/*
 * 0x00477F9C - CMultiComponent::GetSendSlave
 *
 * Returns mc->flags & 0x02 (either 0 or 2).
 */
static inline int
CMultiComponent_GetSendSlave(CMultiComponent *mc)
{
	return mc->flags & 0x02;
}

/*
 * 0x00478FE0 - MultiExtentTracker::GetMinExtent
 *
 * Copies minExtent into *out.
 */
static inline void
MultiExtentTracker_GetMinExtent(MultiExtentTracker *t, CLocation *out)
{
	CLocation_SetLoc(out, &t->minExtent);
}

/*
 * 0x00479000 - MultiExtentTracker::GetMaxExtent
 *
 * Copies maxExtent into *out.
 */
static inline void
MultiExtentTracker_GetMaxExtent(MultiExtentTracker *t, CLocation *out)
{
	CLocation_SetLoc(out, &t->maxExtent);
}

/*
 * 0x00486DED - CItem::GetMultiSlave
 *
 * Returns item->multiPtr as CMultiSlave* if IsMultiOwner, else NULL.
 */
static inline CMultiSlave *
CItem_GetMultiSlave(CItem *item)
{
	if (!CItem_IsMultiOwner(item))
		return NULL;
	return (CMultiSlave *)item->multiPtr;
}

/*
 * Custom - CItem_HasMulti
 *
 * Inline extraction of the multiPtr != NULL check that appears
 * at 0x00486C0E (CItem::HasMulti_Filter) and in many callers.
 */
static inline int
CItem_HasMulti(CItem *item)
{
	return item->multiPtr != NULL;
}

extern CResManager g_MultiManager; // 0x006470A0
extern CResManager g_MultiManagerSlaves; // 0x006472B8 (CMultiManager+0x218)

void *Allocate16_Inner(int count); // 0x0047B1A0
void CVector_Destroy4_Range(CVector *this, void *first, void *last); // 0x00422740
void MultiComponentPool_Init(void); // 0x0047423A
CMultiComponent *MultiComponent_AllocInit3(uint32_t serial, CItem *ownerItem, CLocation *loc); // 0x004743A2
CMultiComponent *MultiComponent_AllocInit2(uint32_t serial, CItem *ownerItem); // 0x004743CE
void MultiComponent_ReturnToPool(CMultiComponent *mc); // 0x004743F6
void CMultiComponent_Init3(CMultiComponent *mc, uint32_t serial, CItem *ownerItem, CLocation *loc); // 0x0047447E
void CMultiComponent_Init2(CMultiComponent *mc, uint32_t serial, CItem *ownerItem); // 0x004744C7
void CMultiComponent_SetSerial(CMultiComponent *mc, uint32_t serial); // 0x0047451F
void CMultiComponent_NotifyOwnerRemoval(CMultiComponent *mc, uint32_t serial); // 0x00474546
void CMultiSlave_Constructor_args(CMultiSlave *ms, CItem *ownerItem, CLocation *offset); // 0x0047458E
void CMultiSlave_Constructor(CMultiSlave *ms, CItem *ownerItem); // 0x00474627
void CMultiSlave_AddComponent(CMultiSlave *ms, uint32_t serial); // 0x00474720
void CMultiSlave_NotifyComponentCount(CMultiSlave *ms, uint32_t *serialRef); // 0x0047473C
void CMultiSlave_AddComponents(CMultiSlave *ms, int skipOwnerSerial); // 0x0047479E
void CMultiSlave_RemoveComponents(CMultiSlave *ms, int skipOwnerSerial); // 0x004747B9
void CMultiSlave_IterateComponents(CMultiSlave *ms, int hideFlag, int skipOwnerSerial); // 0x004747D4
void CMultiSlave_AddComponentItems(CMultiSlave *slave, CItem *container, int flag); // 0x00474864
void CMultiSlave_UpdateComponents(CMultiSlave *slave, CLocation *loc, int flag); // 0x00474914
void CMultiSlave_RemoveAllComponents(CMultiSlave *slave); // 0x004749BC
void CMultiSlave_GetItems(CMultiSlave *slave, CVector *outList); // 0x00474A69
void CMultiSlave_CollectItemSerials(CMultiSlave *slave, CVector *outSerials); // 0x00474CD4
int CMultiSlave_Move(CMultiSlave *slave, CLocation *loc); // 0x00474D6E
int CMultiSlave_MoveCheck(CMultiSlave *slave, CLocation *loc, int checkFlag); // 0x00474E77
int CMultiSlave_CanExistAtWrapper(CMultiSlave *ms, CLocation *loc, int moveType, int unused, CItem *item); // 0x00475053
int CMultiSlave_CanExistAt(CMultiSlave *ms, CLocation *loc, int moveType, int unused, int terrainFlags, CItem *item); // 0x0047508F
void CMultiManager_Constructor(CResManager *this); // 0x004752E7
void CMultiManager_SaveToMul(CResManager *this); // 0x00475348
void CMultiManager_LoadMultiData(CResManager *this, int unused); // 0x00475767
int IsLocationInBounds(CLocation loc, int x1, int y1, int x2, int y2); // 0x00475D61
CItem *CMultiManager_Create(CResManager *this, int typeId, CLocation *loc, int flags, CItem *ownerItem); // 0x004767A4
CItem *CMultiManager_MakeMulti(CResManager *this, int typeId, CLocation *loc, int flags); // 0x004769D0
CItem *CMultiManager_MakeMultiCheck(
        CResManager *this, int typeId, CLocation *loc, int moveType, int terrainFlags, int *result, int arg6, int arg7, int arg8, int arg9, CItem *ownerItem); // 0x00476A14
int CMultiManager_RecycleMulti(CResManager *this, int bodyType, CMultiSlave *slave, int flags); // 0x00476B10
int CMultiManager_MoveMulti(CResManager *this, uint16_t typeId, CMultiSlave *slave, CLocation *loc, CItem *owner); // 0x004772FD
int CMultiManager_RecycleMultiCheck(CResManager *this, int bodyType, CMultiSlave *slave, CLocation *loc, int checkFlag, int flags); // 0x0047736D
int CMultiSlave_MapSwitchMove(CMultiSlave *slave, CLocation *loc, int checkFlag); // 0x004774A5
int CMultiManager_RecycleMultiCheckRotate(CResManager *this, int bodyType, CMultiSlave *slave, CLocation *loc, int rotation, int checkFlag, int flags); // 0x004775A1
int CMultiManager_CanExistAt(CResManager *this, int typeId, CLocation *loc, int moveType, CItem *item); // 0x00477829
int CMultiManager_AreMobilesInArea(CResManager *this, int typeId, CLocation *loc); // 0x00477921
int CMultiManager_GetExtents(CResManager *this, int typeId, CLocation *minLoc, CLocation *maxLoc); // 0x00477AED
int CMultiManager_GetNumInType(CResManager *this, int typeId, int *outCount); // 0x00477B50
void CMulti_NotifyComponentLoc(CMultiComponent *mc, CLocation *loc); // 0x00477E64
void CMulti_SendPlayerInfo(CMultiComponent *mc, CItem *player); // 0x00477EFC
void CMultiComponent_SetValid(CMultiComponent *mc, int value); // 0x00477F69
void CMultiComponent_SetSendSlave(CMultiComponent *mc, int value); // 0x00477FB4
void CMultiSlave_ComputeRange(CMultiSlave *ms, CMultiDef *def); // 0x00478292
int CMultiSlave_MinDistToEntity(CMultiSlave *slave, CItem *target); // 0x004783B2
int TriggerEdit_MultiUpdate(CItem *ent, CLocation *loc); // 0x0047850D
void *CMultiDef_ScalarDelete(CMultiDef *this, int flags); // 0x004785C0
CMultiRotateRect *CMultiRotateRect_Init(CMultiRotateRect *this, CLocation *loc1, CLocation *loc2, uint32_t rotation); // 0x00478710
CMultiRotateRect *CMultiRotateRect_SetRotation(CMultiRotateRect *rect, uintptr_t value); // 0x00478750
void CEntity_SetLocationByDelta(CItem *this, CLocation *delta); // 0x004CD910
void CMultiDef_Constructor(CMultiDef *def); // 0x00478C10
void CMultiComponent_SetOffset(CMultiComponent *mc, CLocation *loc); // 0x00478D90
void CMultiSlave_SetTypeId(CMultiSlave *ms, int32_t typeId); // 0x00478DE0
void MultiExtentTracker_Constructor(MultiExtentTracker *t); // 0x00478E00
void MultiExtentTracker_Reset(MultiExtentTracker *t); // 0x00478E30
void MultiExtentTracker_Update(MultiExtentTracker *t, CMultiComponentDef *comp); // 0x00478EA0
void *CVector_Uninit_Copy16_Fwd2(CVector *this, void *first, void *last, void *dest); // 0x00479480
void *CDeque6_InsertAtEnd(CVector *this, void *endPos, void *element); // 0x004795E0
void CVector_Destroy16_Single(CVector *this, void *element); // 0x004798B0
void CVector_Destroy1C_Single(CVector *this, void *element); // 0x00479B90
void CVector_Destroy6_Single(CVector *this, void *element); // 0x00479FF0
void *Uninit_Copy4_Fwd2(CVector *this, void *first, void *last, void *dest); // 0x0047A370
void *Uninit_Copy16_Fwd(void *first, void *last, void *dest); // 0x0047B160
void *Allocate1C(int count); // 0x0047B1D0
void *CResList_ScalarDelete_MultiA(CResList *this, int flags); // 0x0047B430
void *CResList_ScalarDelete_MultiB(CResList *this, int flags); // 0x0047B460
void *CResList_ScalarDelete_MultiVal(CResList *this, int flags); // 0x0047B490
void *SmartPtr_CVector_ScalarDelete(CSmartPtr *this, int flags); // 0x0047BFF0
void InsertionSort_Int_Small(void *first, void *last, uint8_t cmpVal); // 0x0047C7E0
void InsertionSort_Int_ShiftDown(void *pos, uintptr_t value, uint8_t cmpVal); // 0x0047C8E0
void InsertionSort_Dist_Small(void *first, void *last, CLocation cmpLoc); // 0x0047C930
void InsertionSort_Dist_ShiftDown(void *pos, uintptr_t value, CLocation cmpLoc); // 0x0047CA70
int CLocation_DistanceComparator2D(CLocation *this, CItem *a, CItem *b); // 0x0047CAC0
void *SmartPtr_CMultiDef_ScalarDelete(CSmartPtr *this, int flags); // 0x0047CB90
void *CVector_ScalarDelete(CVector *this, int flags); // 0x0047CBC0
int CMulti_GetValue_VT(CItem *self, int useResource, int normalize); // 0x00489A85
void CMulti_SendEntityUpdate_VT(CItem *self, CItem *viewer, int unused); // 0x00489AD5
uint16_t CMulti_GetAmount_VT(CItem *self); // 0x00489BC7
void CMulti_NotifyNearby_VT(CItem *self, CVector *playerList, int unused); // 0x00489BDA
int CMulti_GetDirection_VT(CItem *self); // 0x0048AB13
void CMulti_OnDeath_VT(CItem *self, int value); // 0x0048AB27
void CMultiSlave_SetCarry(CMultiSlave *ms, uint32_t carry); // 0x004C0560
void CMultiComponent_SetFlags(CMultiComponent *mc, uint8_t flags); // 0x004CD940
uint32_t CMultiSlave_GetCarry(CMultiSlave *ms); // 0x004CD960
uint16_t CMultiSlave_GetRange(CMultiSlave *ms); // 0x004CD980
void CMultiSlave_SetRange(CMultiSlave *ms, uint16_t range); // 0x004CD9A0
struct CSearchCtx;
void *CEntity_BeginIter(struct CSearchCtx *this, void *out); // 0x004CD9E0
void *CItem_GetType(CItem *this); // 0x004CDA10
int CItem_IsSameType(struct CSearchCtx *a, struct CSearchCtx *b); // 0x004CDA30
void CVector_ClearFreeRaw(void *ptr, int count);
void CMulti_Free(CMultiComponent *mc);
int IntLessThan(void *a, void *b, int cmp);

void Multi_DestroyPools(void); // CUSTOM

#endif /* MULTI_H_ */
