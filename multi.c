/*
 * CMulti - composite structures (houses and boats).
 *
 * A master CMulti item owns a chain of static component items linked
 * through the slave list; this module handles construction from multi
 * templates, position updates that move every component together, and
 * the click / door interactions specific to multis.
 */

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "item.h"
#include "multi.h"

#include "world.h"
#include "packet_manager.h"
#include "player.h"
#include "vtable.h"
#include "io.h"
#include "filemanager.h"
#include "dynamic.h"
#include "corpse.h"
#include "vg_pool.h"
#include "packet_handler.h"
#include "main.h"
#include "utils.h"
#include "stddeque.h"
#include "wombat_compile.h"
#include "feature.h"

static void CMultiComponent_Constructor(CMultiComponentDef *def); // 0x0047424E
static CMultiComponentDef *CMultiComponent_CopyConstructor(CMultiComponentDef *def, CMultiComponentDef *src); // 0x0047428A
static CMultiComponentDef *CMultiComponent_CopyFrom(CMultiComponentDef *def, CMultiComponentDef *src); // 0x004742EF
static void CMultiComponent_Reset(CMultiComponentDef *def); // 0x0047433B
static void CMultiComponent_Destructor(CMultiComponentDef *def); // 0x0047438C
static void CMultiComponent_Constructor_base(CMultiComponent *mc); // 0x00474414
static void CMultiComponent_Destructor_base(CMultiComponent *mc); // 0x0047444C
static int CMultiComponent_IsOwner_VF(CMultiComponent *mc); // 0x00474501
static void CMultiSlave_Destructor(CMultiSlave *ms); // 0x004746BC
static int CMultiSlave_IsOwner_VF(CMultiSlave *ms); // 0x00474710
static int CMultiSlave_ValidateMove(CMultiSlave *slave, CVector *itemList, CLocation *loc, int checkFlag, int unused1, CItem *unused2); // 0x00474FF1
static int CMultiSlave_CheckCarriedItemMoves(CMultiSlave *slave, CVector *itemList, CLocation *loc, int checkFlag, CItem *unused); // 0x004751A1
static void CResManager_ConstructorBase(CResManager *this); // 0x004752DC
static CItem *CEntityMap_SpatialNext(CEntityMap *this, CItem *item); // 0x00475DA2
static void CMultiSlave_CollectEntities(
        CEntityMap *this, CItem *startItem, int x1, int y1, int x2, int y2, CVector *outSerials, CLocation *maxExtent, CLocation *minExtent, int *minZ); // 0x00475DDF
static int CMultiManager_MoveMultiInternal(CResManager *this, uint16_t typeId, CMultiSlave *slave, CLocation *loc, CItem *owner); // 0x00476F03
static int CMultiManager_ValidateComponents(CResManager *this, CVector *itemList, int moveType, CItem *unused); // 0x00477A4A
static void CMultiDef_CalcBounds(CMultiDef *def, CLocation *loc); // 0x00477C0E
static void CMultiDef_SetAllSerial(CMultiDef *def, uint32_t serial); // 0x00477C6E
static void CMulti_BroadcastSlaveMove(CMultiSlave *slave, CLocation *loc); // 0x00477E1F
static void CMultiComponent_SendItemUpdate(CMultiComponent *mc, CItem *player); // 0x00477EB9
static void *CMultiManager_GetComponentsNetwork(CResManager *this, int typeId, int *outSize); // 0x00477FE7
static void CMulti_SendMultiInfo(CResManager *this, uint32_t serial); // 0x004781D4
static void *CMultiSlave_ScalarDtor(CMultiSlave *ms, int flags); // 0x00478560
static void *CMultiDef_SetExtents(CMultiDef *def, CLocation *minLoc, CLocation *maxLoc); // 0x00478590
static void *CMultiDef_SetMinExtent(CMultiDef *def, CLocation *loc); // 0x004785F0
static void CLocation_Add(CLocation *dst, CLocation *src); // 0x004786C0
static CMultiRotateRect *CMultiRotateRect_SetRotation(CMultiRotateRect *rect, uintptr_t value); // 0x00478750
static void *CDeque16_FindByHash(CVector *this, void *element); // 0x00478AB0
static void *CDeque1C_FindByHash(CVector *this, void *element); // 0x00478BE0
static void *CMultiComponentPool_Alloc(void); // 0x00478C50
static void CMultiDef_Destructor(CMultiDef *def); // 0x00478D70
static void CMultiComponentPool_Return(CMultiComponent *mc); // 0x00478DB0
static void *CMultiComponent_ScalarDtor(CMultiComponent *mc, int flags); // 0x00479120
static void *CDeque16_Insert1(CVector *this, void *pos, void *element); // 0x00479400
static void *CDeque1C_Insert1(CVector *this, void *pos, void *element); // 0x004794C0
static void CDeque16_Insert(CVector *this, void *pos, uint32_t count, void *element); // 0x00479660
static void CopyFrom16(CVector *this, void *src, void *dst); // 0x00479890
static void CDeque1C_Insert(CVector *this, void *pos, uint32_t count, void *element); // 0x004798D0
static void *CVector_Uninit_Copy1C_Fwd2(CVector *this, void *first, void *last, void *dest); // 0x00479B10
static void CopyFrom1C(CVector *this, void *src, void *dst); // 0x00479B70
static void CDeque6_Insert(void *deque, void *pos, int count, void *element); // 0x0047A010
static void Uninit_FillN_16(CVector *this, void *ptr, uint32_t count, void *source); // 0x0047A250
static void Uninit_FillN_1C(CVector *this, void *ptr, uint32_t count, void *source); // 0x0047A290
static void *Uninit_Copy4_Fwd(CVector *this, void *first, void *last, void *dest); // 0x0047A2D0
static void Uninit_FillN_4(CVector *this, void *ptr, uint32_t count, void *source); // 0x0047A310
static void Uninit_FillN_4v2(CVector *this, void *ptr, uint32_t count, void *source); // 0x0047A3B0
static void *Uninit_Copy6_Fwd(CVector *this, void *first, void *last, void *dest); // 0x0047A430
static void Uninit_FillN_6(CVector *this, void *ptr, uint32_t count, void *source); // 0x0047A470
static void CLocation_CopySingle(CVector *this, CLocation *dest, CLocation *source); // 0x0047A4D0
static void HideItemsInVector_Raw(uintptr_t *first, uintptr_t *last, uint8_t dummy); // 0x0047ACF0
static void VT_HIDE_Single(StdAllocator *this, CItem *item); // 0x0047AD20
static void *RelocateItems_Raw(void *output, uintptr_t *first, uintptr_t *last, CLocation newLoc); // 0x0047AD40
static void RelocateItem_Single(CLocationPair *this, CItem *item); // 0x0047AD80
static void RestoreItems_Raw(uintptr_t *first, uintptr_t *last, uint8_t dummy); // 0x0047ADE0
static void RestoreItem_Single(StdAllocator *this, CItem *item); // 0x0047AE10
static void SaveMulti_SortInt(void *first, void *last, uint8_t cmpVal); // 0x0047AE40
static void CollectEntities_SortDist(void *first, void *last, CLocation cmpLoc); // 0x0047AE70
static void *AttachScriptsFromVector(void *output, char *first, char *last, uintptr_t entity); // 0x0047AEB0
static void AttachScript_Single(uintptr_t *this, CString *element); // 0x0047AEF0
static void *RotateAndRelocateItems(void *output, uintptr_t *first, uintptr_t *last, CLocationPair *params); // 0x0047AF20
static void RotateAndRelocateItem_Single(CLocationPair *this, CItem *item); // 0x0047AF60
static void RotateLocation(CRotationTable *this, int16_t *x, int16_t *y, int rotation); // 0x0047AFD0
static void *SetLocationsFromVector(void *output, uintptr_t *first, uintptr_t *last, uintptr_t data); // 0x0047B040
static void SetLocation_Single(uintptr_t *this, CItem *item); // 0x0047B080
static void *SetDirectionsFromVector(void *output, uintptr_t *first, uintptr_t *last, uintptr_t data); // 0x0047B0B0
static void ComputeAndSetDirection(uintptr_t *this, CItem *entity); // 0x0047B0F0
static void FillCopy16_Fwd(void *first, void *last, void *element); // 0x0047B200
static void *CopyBackward16(void *first, void *last, void *dest); // 0x0047B230
static void CopyFrom16_Inner(void *src, void *dst); // 0x0047B260
static void Destroy16_Inner(void *element); // 0x0047B2A0
static void FillCopy1C_Fwd(void *first, void *last, void *element); // 0x0047B2B0
static void *CopyBackward1C(void *first, void *last, void *dest); // 0x0047B2E0
static void CopyFrom1C_Inner(void *src, void *dst); // 0x0047B310
static void Destroy1C_Inner(void *element); // 0x0047B350
static void FillCopy6_Fwd(void *first, void *last, void *element); // 0x0047B360
static void *CopyBackward6(void *first, void *last, void *dest); // 0x0047B390
static void *Allocate6_Inner(int count); // 0x0047B3C0
static void CopySingle6_Inner(void *dest, void *source); // 0x0047B3F0
static void *CLocationPair_CopyAssign(CLocationPair *dst, CLocationPair *src); // 0x0047B4C0
static void *CLocation_CopyFromSrc(CLocation *this, CLocation *src); // 0x0047B4F0
static void *CLocationPair_CopyFromSrc(CLocationPair *this, CLocationPair *src); // 0x0047B510
static void *CMultiComponent_ScalarDelete(CMultiComponentDef *this, int flags); // 0x0047B550
static void SmartPtr_Destructor_CVector(CSmartPtr *self); // 0x0047C730
static void SmartPtr_Destructor_CMultiDef(CSmartPtr *self); // 0x0047CF00
static void InsertionSort_Int(void *first, void *last, int cmpVal, int unused); // 0x0047D0A0
static void InsertionSort_Dist(void *first, void *last, CLocation cmpLoc); // 0x0047D280
static CMultiDef *CMultiManager_FindType(CResManager *rm, int typeId);
static void HideItemsInVector(CVector *list);
static void RestoreItemsInVector(CVector *list);
static void RelocateItemsInVector(CVector *list, CLocation *oldLoc, CLocation *newLoc);
static int CMultiSlave_MapSwitchMove_Wrap(CMultiSlave *slave, CLocation *loc);

// 0x005EF190 - CMultiComponent vtable (4 function pointers)
static void *g_vtbl_CMultiComponent[4] = {
	(void *)(uintptr_t)CMultiComponent_ScalarDtor,         // [0] 0x00479120
	(void *)(uintptr_t)CMultiComponent_IsOwner_VF,         // [1] 0x00474501
	(void *)(uintptr_t)CMultiComponent_NotifyOwnerRemoval, // [2] 0x00474546
	(void *)(uintptr_t)CMultiComponent_Save,               // [3] 0x004C5716 (Save)
};

// 0x005EF1A0 - CMultiSlave vtable (4 function pointers)
static void *g_vtbl_CMultiSlave[4] = {
	(void *)(uintptr_t)CMultiSlave_ScalarDtor,             // [0] 0x00478560
	(void *)(uintptr_t)CMultiSlave_IsOwner_VF,             // [1] 0x00474710
	(void *)(uintptr_t)CMultiSlave_RemoveAllComponents,    // [2] 0x004749BC
	(void *)(uintptr_t)CMultiSlave_Save,                   // [3] 0x004C5844 (Save)
};

/*
 * Pool allocator state at g_multiComponentPool (0x00699AF8). Free entries
 * are threaded through CMultiComponent.ownerItem (+0x10).
 */
__extension__ typedef struct CMultiComponentPool CMultiComponentPool;
struct CMultiComponentPool {
	CMultiComponent *freeHead; // +0x00
	uint32_t blockSize;        // +0x04
	uint32_t allocated;        // +0x08
};

// 0x00699AF8
static CMultiComponentPool g_multiComponentPool;

// 0x0061B594 - path to multi.txt
static char *g_multiTxtPath = "../.rundir/multi.txt";
// 0x0061B598 - path to multi.mul
static char *g_multiMulPath = "../.rundir/multi.mul";
// 0x0061B59C - path to multi.idx
static char *g_multiIdxPath = "../.rundir/multi.idx";

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
 * 0x00464E30 - CResList::RemoveKeyNode
 *
 * Unlinks a node from the multi component list, frees the extracted data
 * when non-NULL, and returns the next/prev node along the iteration.
 */
CResListNode *
CResList_RemoveKeyNode_Multi(CResList *list, CResListNode *node, int direction)
{
	void *data = NULL;
	CResListNode *result;

	result = CResList_Erase_MultiC(list, node, &data, direction);
	if (data != NULL)
		free(data);
	return result;
}

/*
 * 0x0047423A - MultiComponentPool_Init
 *
 * Initializes the CMultiComponent pool with a 0x1000-element block.
 */
void
MultiComponentPool_Init(void)
{
	g_multiComponentPool.freeHead = NULL;
	g_multiComponentPool.blockSize = 0x1000;
	g_multiComponentPool.allocated = 0;
	VG_CREATE_POOL(&g_multiComponentPool);
}

/*
 * Custom - Multi_DestroyPools
 *
 * Server-shutdown cleanup. Walks the multi-component freelist
 * marking each node defined so valgrind can read its next pointer,
 * then ends pool tracking. No binary equivalent. The VG_* macros
 * are no-ops when VALGRIND is not defined.
 */
void
Multi_DestroyPools(void)
{
	CMultiComponent *cur, *next;

	if (g_multiComponentPool.freeHead == NULL)
		return;
	for (cur = g_multiComponentPool.freeHead; cur != NULL; cur = next) {
		VG_MAKE_DEFINED(cur, sizeof(*cur));
		next = (CMultiComponent *)cur->ownerItem;
	}
	VG_DESTROY_POOL(&g_multiComponentPool);
}

/*
 * 0x0047424E - CMultiComponentDef::CMultiComponentDef
 *
 * Initializes offset, script vector, bodyType, and invisible fields.
 */
static void
CMultiComponent_Constructor(CMultiComponentDef *def)
{
	char typeFlag = 0;
	CLocation_Init(&def->offset);
	CVector_Constructor((CVector *)&def->scriptHead, &typeFlag);
	def->bodyType = 0;
	def->invisible = 0;
}

/*
 * 0x0047428A - CMultiComponentDef::CMultiComponentDef (copy)
 *
 * Copy constructor: runs the base ctor, then copies fields from source.
 */
static CMultiComponentDef *
CMultiComponent_CopyConstructor(CMultiComponentDef *def, CMultiComponentDef *src)
{
	CMultiComponent_Constructor(def);
	CMultiComponent_CopyFrom(def, src);
	return def;
}

/*
 * 0x004742EF - CMultiComponentDef::CopyFrom
 *
 * Copies bodyType, offset, script vector, and invisible field from source.
 */
static CMultiComponentDef *
CMultiComponent_CopyFrom(CMultiComponentDef *def, CMultiComponentDef *src)
{
	def->bodyType = src->bodyType;
	CLocation_SetLoc(&def->offset, &src->offset);
	CVector_AssignOp16((CVector *)&def->scriptHead, (CVector *)&src->scriptHead);
	def->invisible = src->invisible;
	return def;
}

/*
 * 0x0047433B - CMultiComponentDef::Reset
 *
 * Resets bodyType, offset (to -1,-1,-1), script vector, and invisible.
 */
static void
CMultiComponent_Reset(CMultiComponentDef *def)
{
	CVector *scriptVec = (CVector *)&def->scriptHead;

	def->bodyType = 0;
	CLocation_Set(&def->offset, (int16_t)0xFFFF, (int16_t)0xFFFF, (int16_t)0xFFFF);
	CVector_Resize16(scriptVec, scriptVec->begin, scriptVec->end);
	def->invisible = 0;
}

/*
 * 0x0047438C - CMultiComponentDef::~CMultiComponentDef
 *
 * Destroys the script-names vector.
 */
static void
CMultiComponent_Destructor(CMultiComponentDef *def)
{
	CVector_ClearAndFree16((CVector *)&def->scriptHead);
}

/*
 * 0x004743A2 - MultiComponent_AllocInit3
 *
 * Allocates a CMultiComponent from the pool and initializes it with
 * serial, owner item, and location.
 */
CMultiComponent *
MultiComponent_AllocInit3(uint32_t serial, CItem *ownerItem, CLocation *loc)
{
	CMultiComponent *mc = CMultiComponentPool_Alloc();
	CMultiComponent_Init3(mc, serial, ownerItem, loc);
	return mc;
}

/*
 * 0x004743CE - MultiComponent_AllocInit2
 *
 * Allocates a CMultiComponent from the pool and initializes it with
 * serial and owner item (no offset).
 */
CMultiComponent *
MultiComponent_AllocInit2(uint32_t serial, CItem *ownerItem)
{
	CMultiComponent *mc = CMultiComponentPool_Alloc();
	CMultiComponent_Init2(mc, serial, ownerItem);
	return mc;
}

/*
 * 0x004743F6 - MultiComponent_ReturnToPool
 *
 * Returns a CMultiComponent to the pool after invoking its IsOwner vf.
 */
void
MultiComponent_ReturnToPool(CMultiComponent *mc)
{
	CMultiComponent_IsOwner(mc);
	CMultiComponentPool_Return(mc);
}

/*
 * 0x00474414 - CMultiComponent::CMultiComponent (base)
 *
 * Base constructor: initializes offset and installs the component vtable.
 * The binary also runs a guarded CResList_ConstructorEmpty on this when
 * g_multiComponentPool.allocated is zero, but both CResList_GetHead and
 * CResList_ConstructorEmpty are no-op stubs so the guard has no
 * observable effect and is omitted here.
 */
static void
CMultiComponent_Constructor_base(CMultiComponent *mc)
{
	CLocation_Init(&mc->offset);
	mc->vtable = g_vtbl_CMultiComponent;
}

/*
 * 0x0047444C - CMultiComponent::~CMultiComponent (base)
 *
 * Base destructor: restores the component vtable.
 */
static void
CMultiComponent_Destructor_base(CMultiComponent *mc)
{
	mc->vtable = g_vtbl_CMultiComponent;
	CResList_DestructorEmpty((CResList *)mc);
}

/*
 * 0x0047447E - CMultiComponent::Init3
 *
 * Populates serial, ownerItem, offset, and the valid/sendSlave flags.
 */
void
CMultiComponent_Init3(CMultiComponent *mc, uint32_t serial, CItem *ownerItem, CLocation *loc)
{
	mc->serial = serial;
	mc->ownerItem = ownerItem;
	CLocation_SetLoc(&mc->offset, loc);
	mc->flags = 0;
	CMultiComponent_SetValid(mc, 1);
	CMultiComponent_SetSendSlave(mc, 0);
}

/*
 * 0x004744C7 - CMultiComponent::Init2
 *
 * Populates serial and ownerItem and the valid/sendSlave flags; offset
 * is left untouched.
 */
void
CMultiComponent_Init2(CMultiComponent *mc, uint32_t serial, CItem *ownerItem)
{
	mc->serial = serial;
	mc->ownerItem = ownerItem;
	mc->flags = 0;
	CMultiComponent_SetValid(mc, 1);
	CMultiComponent_SetSendSlave(mc, 0);
}

/*
 * 0x00474501 - CMultiComponent::IsOwner
 *
 * Default IsOwner vf - returns 0 (not an owner).
 */
static int
CMultiComponent_IsOwner_VF(CMultiComponent *mc)
{
	USED(mc);
	return 0;
}

/*
 * 0x0047451F - SetSlaveSerial
 *
 * Sets the serial field of a CMultiComponent.
 */
void
CMultiComponent_SetSerial(CMultiComponent *mc, uint32_t serial)
{
	mc->serial = serial;
}

/*
 * 0x00474546 - CMultiComponent::NotifyOwnerRemoval
 *
 * Removes the given component serial from the owning multi's slave
 * component list.
 */
void
CMultiComponent_NotifyOwnerRemoval(CMultiComponent *mc, uint32_t serial)
{
	CItem *owner;
	CMultiSlave *slave;

	owner = CWorld_FindBySerial(g_World, mc->serial);
	if (owner == NULL)
		return;
	if (!CItem_IsMultiOwner(owner))
		return;
	slave = CItem_GetMultiSlave(owner);
	CMultiSlave_NotifyComponentCount(slave, &serial);
}

/*
 * 0x0047458E - CMultiSlave::CMultiSlave
 *
 * Constructs a CMultiSlave for the given owner item and offset, defaulting
 * carry=1, typeId=-1, range=0.
 */
void
CMultiSlave_Constructor_args(CMultiSlave *ms, CItem *ownerItem, CLocation *offset)
{
	char typeFlag = 0;
	CMultiComponent_Constructor_base(&ms->base);
	CVector_Constructor(&ms->components, &typeFlag);
	ms->base.vtable = g_vtbl_CMultiSlave;
	CMultiComponent_Init3(&ms->base, ownerItem->serial, ownerItem, offset);
	ms->carry = 1;
	ms->typeId = -1;
	ms->range = 0;
}

/*
 * 0x00474627 - CMultiSlave::CMultiSlave
 *
 * Constructs a CMultiSlave for the given owner item, defaulting carry=1,
 * typeId=-1, range=0.
 */
void
CMultiSlave_Constructor(CMultiSlave *ms, CItem *ownerItem)
{
	char typeFlag = 0;
	CMultiComponent_Constructor_base(&ms->base);
	CVector_Constructor(&ms->components, &typeFlag);
	ms->base.vtable = g_vtbl_CMultiSlave;
	CMultiComponent_Init2(&ms->base, ownerItem->serial, ownerItem);
	ms->carry = 1;
	ms->typeId = -1;
	ms->range = 0;
}

/*
 * 0x004746BC - CMultiSlave::~CMultiSlave
 *
 * Destroys the components vector and chains to the component base dtor.
 */
static void
CMultiSlave_Destructor(CMultiSlave *ms)
{
	ms->base.vtable = g_vtbl_CMultiSlave;
	CVector_Destructor(&ms->components);
	CMultiComponent_Destructor_base(&ms->base);
}

/*
 * 0x00474710 - CMultiSlave::IsOwner
 *
 * Slave IsOwner override - always returns 1.
 */
static int
CMultiSlave_IsOwner_VF(CMultiSlave *ms)
{
	USED(ms);
	return 1;
}

/*
 * 0x00474720 - CMultiSlave::AddComponent
 *
 * Appends a component serial to the slave's component vector.
 */
void
CMultiSlave_AddComponent(CMultiSlave *ms, uint32_t serial)
{
	CVector_PushBack(&ms->components, serial);
}

/*
 * 0x0047473C - CMultiSlave::NotifyComponentCount
 *
 * Removes the given serial from the component vector, unless it matches
 * the slave's own base serial.
 */
void
CMultiSlave_NotifyComponentCount(CMultiSlave *ms, uint32_t *serialRef)
{
	uintptr_t *pos;
	uintptr_t serialVal = *serialRef;

	pos = Vector_Find(ms->components.begin, ms->components.end, &serialVal);

	if (pos == (uintptr_t *)ms->components.end)
		return;

	if (*pos == ms->base.serial)
		return;

	CVector_EraseSingle(&ms->components, pos);
}

/*
 * 0x0047479E - CMultiSlave::AddComponents
 *
 * Shows all component items (iterates with hideFlag=1).
 */
void
CMultiSlave_AddComponents(CMultiSlave *ms, int skipOwnerSerial)
{
	CMultiSlave_IterateComponents(ms, 1, skipOwnerSerial);
}

/*
 * 0x004747B9 - CMultiSlave::RemoveComponents
 *
 * Detaches all component items from the spatial grid (hideFlag=0).
 */
void
CMultiSlave_RemoveComponents(CMultiSlave *ms, int skipOwnerSerial)
{
	CMultiSlave_IterateComponents(ms, 0, skipOwnerSerial);
}

/*
 * 0x004747D4 - CMultiSlave::IterateComponents
 *
 * Hides (hideFlag=1) or detaches (hideFlag=0) each component item,
 * skipping the slave's own serial when skipOwnerSerial is set.
 */
void
CMultiSlave_IterateComponents(CMultiSlave *ms, int hideFlag, int skipOwnerSerial)
{
	uintptr_t *iter;
	uint32_t serial;
	CItem *ent;

	iter = (uintptr_t *)ms->components.begin;

	while (iter != (uintptr_t *)ms->components.end) {
		serial = (uint32_t)*iter;
		if (skipOwnerSerial && serial == ms->base.serial) {
			iter++;
			continue;
		}
		ent = CWorld_FindBySerial(g_World, serial);
		if (ent == NULL) {
			iter++;
			continue;
		}
		if (ent->resourceEntity.entity.removedFromWorld) {
			iter++;
			continue;
		}
		if (hideFlag)
			((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);
		else
			((void (*)(void *))VT_FN(ent, VT_DETACH_SPATIAL))(ent);
		iter++;
	}
}

/*
 * 0x00474864 - CMultiSlave::AddComponentItems
 *
 * Hides each component item and, if removed from the world, drops it
 * into the given container at (-1,-1,-1).
 */
void
CMultiSlave_AddComponentItems(CMultiSlave *slave, CItem *container, int flag)
{
	uintptr_t *iter;
	CLocation loc;

	loc.x = -1;
	loc.y = -1;
	loc.z = -1;

	iter = (uintptr_t *)slave->components.begin;

	while (iter != (uintptr_t *)slave->components.end) {
		uint32_t serial;
		CItem *ent;

		serial = (uint32_t)*iter;
		if (flag == 1 && serial == slave->base.serial) {
			iter++;
			continue;
		}

		ent = CWorld_FindBySerial(g_World, serial);
		if (ent == NULL) {
			iter++;
			continue;
		}

		if (!ent->resourceEntity.entity.removedFromWorld)
			((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);

		if (ent->resourceEntity.entity.removedFromWorld)
			((void (*)(void *, CItem *, CLocation *))VT_FN(ent, VT_ADD_TO_CONTAINER))(ent, container, &loc);

		iter++;
	}
}

/*
 * 0x00474914 - CMultiSlave::UpdateComponents
 *
 * Repositions each component item to loc plus its multi offset.
 */
void
CMultiSlave_UpdateComponents(CMultiSlave *slave, CLocation *loc, int flag)
{
	uintptr_t *iter;

	iter = (uintptr_t *)slave->components.begin;

	while (iter != (uintptr_t *)slave->components.end) {
		uint32_t serial;
		CItem *ent;
		CLocation tmpLoc;

		serial = (uint32_t)*iter;
		if (flag == 1 && serial == slave->base.serial) {
			iter++;
			continue;
		}

		ent = CWorld_FindBySerial(g_World, serial);
		if (ent == NULL) {
			iter++;
			continue;
		}

		tmpLoc = *loc;

		if (CItem_HasMulti_Filter(ent)) {
			CMultiComponent *mc = CItem_GetMulti(ent);
			CLocation_Add(&tmpLoc, CMulti_GetOffset(mc));

			((void (*)(void *, CLocation *))VT_FN(ent, VT_DROP_AT_FEET))(ent, &tmpLoc);
		}

		iter++;
	}
}

/*
 * 0x004749BC - CMultiSlave::RemoveAllComponents
 *
 * Walks the component vector in reverse, detaching and deleting each
 * component (other than the owner) and popping it off the vector.
 */
void
CMultiSlave_RemoveAllComponents(CMultiSlave *slave)
{
	uintptr_t *iter;
	CItem *comp;
	uint32_t serial;

	iter = (uintptr_t *)slave->components.end;

	for (;;) {
		if (iter == (uintptr_t *)slave->components.begin)
			break;

		serial = (uint32_t)*(iter - 1);

		if (serial != slave->base.serial) {
			serial = (uint32_t)*(iter - 1);
			comp = CWorld_FindBySerial(g_World, serial);
			if (comp != NULL) {
				CItem_DetachMulti(comp);
				if (comp != NULL)
					((void (*)(void *))VT_FN(comp, VT_DELETE))(comp);
			}
		}

		iter--;

		CVector_EraseBack(&slave->components);
	}
}

/*
 * 0x00474A69 - CMultiSlave::GetItems
 *
 * Collects items resting on the multi's component tiles into outList,
 * skipping the multi's own components and items above the tile height.
 * Early-out when carry is 0.
 */
void
CMultiSlave_GetItems(CMultiSlave *slave, CVector *outList)
{
	uintptr_t *iter;
	CItem *compItem;
	CLocation compLoc;
	int16_t maxZ;
	CVector tmpList;
	char typeFlag;
	uintptr_t *tp;
	int surfaceH;
	uint32_t flags;

	if (slave->carry == 0)
		return;

	typeFlag = 0;
	CVector_Constructor(&tmpList, &typeFlag);

	maxZ = 0;
	iter = (uintptr_t *)slave->components.begin;

	for (;;) {
		if (iter == (uintptr_t *)slave->components.end)
			break;

		compItem = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (compItem == NULL)
			goto next_comp;

		// Get component item's location
		CLocation_CopyFrom(&compLoc, &compItem->resourceEntity.entity.location);

		// Get surface height via vtable[0x2C]
		surfaceH = ((int (*)(void *))VT_FN(compItem, VT_GET_SURFACE_H))(compItem);
		maxZ = compLoc.z + (int16_t)surfaceH;

		// Validate coordinates
		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)compLoc.x, (int)compLoc.y)) {
			iter++;
			continue;
		}

		// Check vtable[0x30] flags for surface bit 0x200
		flags = ((uint32_t (*)(void *))VT_FN(compItem, VT_GET_FLAGS))(compItem);
		if (!(flags & 0x200)) {
			iter++;
			continue;
		}

		// Get all items at this XY with z >= compLoc.z
		CVector_Erase(&tmpList, tmpList.begin, tmpList.end);
		CBlockManager_GetItemsAtLocationXYZ(&g_SpatialGrid, &tmpList, &compLoc);

		Vector_SortByZ(tmpList.begin, tmpList.end, tmpList.type);

		// Iterate found items
		for (tp = (uintptr_t *)tmpList.begin; tp != (uintptr_t *)tmpList.end; tp++) {
			CItem *found = (CItem *)*tp;
			int16_t foundZ;
			uint32_t foundSerial;
			uintptr_t *findResult;
			uintptr_t findSerialVal;

			// Check if this item is a component of our multi
			foundSerial = found->serial;
			findSerialVal = foundSerial;
			findResult = Vector_Find(slave->components.begin, slave->components.end, &findSerialVal);
			if (findResult != (uintptr_t *)slave->components.end) {
				// Found in component list - skip
				goto next_item;
			}

			// Check z bound: items above maxZ are done
			foundZ = found->resourceEntity.entity.location.z;
			if (foundZ > maxZ)
				break;

			// Update maxZ with this item's surface height
			surfaceH = ((int (*)(void *))VT_FN(found, VT_GET_SURFACE_H))(found);
			maxZ = foundZ + (int16_t)surfaceH;

			// Check if already in output list
			findResult = Vector_Find(outList->begin, outList->end, tp);
			if (findResult != outList->end) {
				// Already in output list
				goto next_item;
			}

			// Check vtable[0x11C] - skip if returns nonzero
			if (((int (*)(void *))VT_FN(found, VT_IS_IN_WORLD))(found))
				goto next_item;

			CVector_PushBack(outList, (uintptr_t)found);
next_item:;
		}
next_comp:
		iter++;
	}

	CVector_Destructor(&tmpList);
}

/*
 * 0x00474CD4 - CMultiSlave::CollectItemSerials
 *
 * Fills outSerials with the serial of each item carried on this multi.
 */
void
CMultiSlave_CollectItemSerials(CMultiSlave *slave, CVector *outSerials)
{
	CVector itemPtrs;
	char typeFlag = 0;
	uintptr_t *p;
	uint32_t serial;

	CVector_Constructor(&itemPtrs, &typeFlag);
	CMultiSlave_GetItems(slave, &itemPtrs);

	p = (uintptr_t *)itemPtrs.begin;
	for (;;) {
		if (p == (uintptr_t *)itemPtrs.end)
			break;
		serial = CMobile_GetSerial((CMobile *)*p);
		CVector_PushBack(outSerials, serial);
		p++;
	}

	CVector_Destructor(&itemPtrs);
}

/*
 * 0x00474D6E - CMultiSlave::Move
 *
 * Moves the multi owner to loc and relocates carried items to maintain
 * their relative positions.
 */
int
CMultiSlave_Move(CMultiSlave *slave, CLocation *loc)
{
	CVector itemList;
	CLocation savedLoc;
	CItem *ownerItem;
	char typeFlag = 0;

	CVector_Constructor(&itemList, &typeFlag);

	CLocation_Init(&savedLoc);

	CMultiSlave_GetItems(slave, &itemList);

	HideItemsInVector(&itemList);

	ownerItem = (CItem *)slave->base.ownerItem;
	CLocation_CopyFrom(&savedLoc, &ownerItem->resourceEntity.entity.location);

	ownerItem = (CItem *)slave->base.ownerItem;
	if (!ownerItem->resourceEntity.entity.removedFromWorld) {
		((void (*)(void *))VT_FN(ownerItem, VT_HIDE))(ownerItem);
	}

	((void (*)(void *, void *))VT_FN(ownerItem, VT_DROP_AT_FEET))(ownerItem, loc);

	// 0x00474E1B-0x00474E4B: relocate carried items
	RelocateItemsInVector(&itemList, &savedLoc, loc);

	CVector_Destructor(&itemList);

	return 1;
}

/*
 * 0x00474E77 - CMultiSlave::MoveCheck
 *
 * MODIFIED: like Move, but validates the destination first; on failure
 * restores the owner and carried items to their previous positions.
 * With FEAT_BOAT_MAPSWITCH a Felucca-side wrap-teleport (Q5CP wrapped
 * by CLocation_MoveDir to the opposite world edge) is detected before
 * validation and rerouted through CMultiSlave_MapSwitchMove_Wrap; this
 * keeps boats wrapping correctly even when the playable area extends
 * past Felucca's 0x1400 / 0x1000 thresholds (as it must in the demo,
 * which uses x>=0x1400 for dungeon coordinates).
 */
int
CMultiSlave_MoveCheck(CMultiSlave *slave, CLocation *loc, int checkFlag)
{
	CVector itemList;
	CLocation savedLoc;
	CItem *ownerItem;
	int result;
	char typeFlag = 0;

	if (feat(FEAT_BOAT_MAPSWITCH) && slave->carry != 0) {
		CItem *owner = (CItem *)slave->base.ownerItem;
		if (owner != NULL) {
			CLocation *ownerLoc = &owner->resourceEntity.entity.location;
			if ((int16_t)ownerLoc->x < 0x1400) {
				int dx = (int16_t)loc->x - (int16_t)ownerLoc->x;
				int dy = (int16_t)loc->y - (int16_t)ownerLoc->y;
				if (dx < 0)
					dx = -dx;
				if (dy < 0)
					dy = -dy;
				// Mirror CLocation_ComputeDelta's wrap markers: a
				// naive distance past Felucca's half-width / -height
				// is the signature of moveDir having wrapped Q5CP
				// across the seam.
				if (dx > 0x0A00 || dy > 0x0800)
					return CMultiSlave_MapSwitchMove_Wrap(slave, loc);
			}
		}
	}

	CVector_Constructor(&itemList, &typeFlag);

	CLocation_Init(&savedLoc);

	CMultiSlave_GetItems(slave, &itemList);

	HideItemsInVector(&itemList);

	ownerItem = (CItem *)slave->base.ownerItem;
	CLocation_CopyFrom(&savedLoc, &ownerItem->resourceEntity.entity.location);

	ownerItem = (CItem *)slave->base.ownerItem;
	if (!ownerItem->resourceEntity.entity.removedFromWorld) {
		((void (*)(void *))VT_FN(ownerItem, VT_HIDE))(ownerItem);
	}

	// 0x00474F0F-0x00474F27: validate move via 0x00474FF1
	result = CMultiSlave_ValidateMove(slave, &itemList, loc, checkFlag, 0, 0);

	if (result <= 0) {
		// 0x00474F30-0x00474F7B: check failed, restore everything
		ownerItem = (CItem *)slave->base.ownerItem;
		((void (*)(void *, void *))VT_FN(ownerItem, VT_DROP_AT_FEET))(ownerItem, &savedLoc);

		// Restore carried items at their original positions
		RestoreItemsInVector(&itemList);

		CVector_Destructor(&itemList);
		return result;
	}

	// 0x00474F80-0x00474FDE: check passed, commit the move
	ownerItem = (CItem *)slave->base.ownerItem;
	((void (*)(void *, void *))VT_FN(ownerItem, VT_DROP_AT_FEET))(ownerItem, loc);

	// Relocate carried items to new positions
	RelocateItemsInVector(&itemList, &savedLoc, loc);

	CVector_Destructor(&itemList);
	return 1;
}

/*
 * 0x00474FF1 - CMultiSlave::ValidateMove
 *
 * Returns >0 if the multi can exist at loc and every carried item's
 * new position is valid; otherwise propagates the failing check's code.
 */
static int
CMultiSlave_ValidateMove(CMultiSlave *slave, CVector *itemList, CLocation *loc, int checkFlag, int unused1, CItem *unused2)
{
	int result;

	// 0x00474ffa-0x0047500f: CanExistAt(slave, loc, checkFlag, unused1, 0, unused2)
	result = CMultiSlave_CanExistAt(slave, loc, checkFlag, unused1, 0, unused2);
	if (result <= 0)
		return result;

	// 0x00475022-0x0047503a: CMultiSlave_CheckCarriedItemMoves(slave, itemList, loc, checkFlag, unused2)
	result = CMultiSlave_CheckCarriedItemMoves(slave, itemList, loc, checkFlag, unused2);
	if (result <= 0)
		return result;

	return 1;
}

/*
 * 0x00475053 - CMultiSlave::CanExistAtWrapper
 *
 * Thin wrapper that calls CanExistAt with terrainFlags=0 and clamps
 * any positive result to 1.
 */
int
CMultiSlave_CanExistAtWrapper(CMultiSlave *ms, CLocation *loc, int moveType, int unused, CItem *item)
{
	int result = CMultiSlave_CanExistAt(ms, loc, moveType, unused, 0, item);
	if (result > 0)
		result = 1;
	return result;
}

/*
 * 0x0047508F - CMultiSlave::CanExistAt
 *
 * Checks every component of the multi for valid coordinates and
 * passable terrain at its absolute position (loc + component offset).
 * Returns 1 if all components fit, 0 if terrain blocks, -1 if out of
 * bounds.
 */
int
CMultiSlave_CanExistAt(CMultiSlave *ms, CLocation *loc, int moveType, int unused, int terrainFlags, CItem *item)
{
	CLocation ownerLoc;
	CLocation compLoc;
	uintptr_t *iter, *end;
	CItem *compItem;
	CMultiComponent *multi;
	CLocation *offset;
	int height;

	USED(unused);
	USED(item);

	CLocation_CopyFrom(&ownerLoc, &((CItem *)ms->base.ownerItem)->resourceEntity.entity.location);

	iter = (uintptr_t *)ms->components.begin;

	CLocation_Init(&compLoc);

	for (;;) {
		end = (uintptr_t *)ms->components.end;
		if (iter == end)
			break;

		compItem = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (compItem == NULL)
			goto next;

		if (!CItem_HasMulti_Filter(compItem))
			goto next;

		CLocation_CopyFrom(&compLoc, loc);

		multi = CItem_GetMulti(compItem);
		offset = CMulti_GetOffset(multi);
		CLocation_Add(&compLoc, offset);

		height = ((int (*)(void *))VT_FN(compItem, VT_GET_HEIGHT))(compItem);

		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)compLoc.x, (int16_t)compLoc.y))
			return -1;

		if (!(CTerrainManager_CheckMoveBlocked(compLoc, height, moveType, NULL, terrainFlags) & 4))
			return 0;

next:
		iter++;
	}

	return 1;
}

/*
 * 0x004751A1 - CMultiSlave::CheckCarriedItemMoves
 *
 * Validates every carried item's new position (delta from owner's old
 * location plus the target). Returns 1 on success, 0 if terrain blocks,
 * -1 if coordinates are out of range.
 */
static int
CMultiSlave_CheckCarriedItemMoves(CMultiSlave *slave, CVector *itemList, CLocation *loc, int checkFlag, CItem *unused)
{
	USED(unused);
	CLocation savedLoc;
	CLocation compLoc;
	uintptr_t *iter;
	int height;

	CLocation_CopyFrom(&savedLoc, &((CItem *)slave->base.ownerItem)->resourceEntity.entity.location);

	iter = (uintptr_t *)itemList->begin;

	CLocation_Init(&compLoc);

	for (;;) {
		CItem *item;

		if (iter == (uintptr_t *)itemList->end)
			break;

		item = (CItem *)*iter;

		CLocation_CopyFrom(&compLoc, &item->resourceEntity.entity.location);

		CLocation_ComputeDelta(&compLoc, &compLoc, &savedLoc);

		CLocation_Add(&compLoc, loc);

		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)compLoc.x, (int16_t)compLoc.y))
			return -1;

		height = ((int (*)(void *))VT_FN(item, VT_GET_HEIGHT))(item);
		if (!(CTerrainManager_CheckMoveBlocked(compLoc, height, checkFlag, NULL, 0) & 4))
			return 0;

		iter++;
	}

	return 1;
}

/*
 * 0x004752DC - CResManager::CResManager (base no-op)
 *
 * Base-class no-op constructor.
 */
static void
CResManager_ConstructorBase(CResManager *this)
{
	USED(this);
}

/*
 * 0x004752E7 - CMultiManager::CMultiManager
 *
 * Forwards to the base-class constructor (itself a no-op).
 */
void
CMultiManager_Constructor(CResManager *this)
{
	CResManager_ConstructorBase(this);
}

/*
 * 0x00475348 - CMultiManager::SaveToMul
 *
 * Rebuilds multi.idx/multi.mul from the in-memory CResManager: collects
 * and sorts keys, packs each type's components as 12-byte records,
 * gap-fills empty slots up to 0x1000 entries, and repacks the indexed
 * files. The multi.txt file is opened with "w" but otherwise unused.
 */
void
CMultiManager_SaveToMul(CResManager *this)
{
	CSearchCtx searchCtx;
	CIndexedFileManager indexedFile;
	FILE *txtFile;
	CVector keyVec;
	CSearchCtx iterBuf;
	CSearchCtx nextBuf;
	CSearchCtx findBuf;
	uint8_t emptyByte;
	int count;
	int lastKey;
	char *keyPtr;
	uint32_t multiId;
	int extra;
	uint32_t numComponents;
	int dataSize;
	uint8_t *dataBuf;
	uint32_t i;
	CMultiComponentDef *comp;
	int offset;
	char *subPtr;
	char typeFlag;
	int gapIdx;
	int padIdx;
	uint32_t *keyAtPos;
	CMultiDef *def;

	emptyByte = 0;
	count = 0;

	CSearchCtx_Constructor(&searchCtx);
	CIndexedFileManager_Constructor(&indexedFile);

	CIndexedFileManager_Open(&indexedFile, g_multiIdxPath, g_multiMulPath, "wb");

	txtFile = fopen_ServerSide(g_multiTxtPath, "w");
	if (txtFile == NULL) {
		CIndexedFileManager_Destructor(&indexedFile);
		return;
	}

	CVector_Constructor(&keyVec, &typeFlag);

	// Collect all keys from CResManager into keyVec
	CResManager_BeginIter_MultiA(this, &iterBuf);
	CSearchCtx_Add(&searchCtx, &iterBuf);

	while (CSearchCtx_Find(&searchCtx)) {
		keyAtPos = (uint32_t *)CResManager_GetKeyAtPos(this, &searchCtx);
		CVector_PushBack(&keyVec, *keyAtPos);

		CResManager_NextIter_MultiA(this, &nextBuf, &searchCtx);
		CSearchCtx_Add(&searchCtx, &nextBuf);
	}

	// Sort collected keys
	SaveMulti_SortInt(keyVec.begin, keyVec.end, 0);

	lastKey = -1;

	// Iterate sorted keys
	keyPtr = (char *)keyVec.begin;
	while (keyPtr != (char *)keyVec.end) {
		// Look up this key in CResManager
		CResManager_FindByKey_A(this, &findBuf, (uint32_t *)keyPtr, 1);
		CSearchCtx_Add(&searchCtx, &findBuf);

		if (!CSearchCtx_Find(&searchCtx)) {
			keyPtr += sizeof(uintptr_t);
			continue;
		}

		// Get multiId
		keyAtPos = (uint32_t *)CResManager_GetKeyAtPos(this, &searchCtx);
		multiId = *keyAtPos;
		extra = 0;

		// Get component count
		def = (CMultiDef *)CResManager_GetResultCtx(this, &searchCtx);
		numComponents = CVector_GetCount1C(&def->components);

		dataSize = numComponents * 12;
		dataBuf = (uint8_t *)malloc(dataSize);

		i = 0;

		// Get component data pointer
		def = (CMultiDef *)CResManager_GetResultCtx(this, &searchCtx);
		comp = (CMultiComponentDef *)def->components.begin;

		// Serialize each component
		while (comp != (CMultiComponentDef *)((CMultiDef *)CResManager_GetResultCtx(this, &searchCtx))->components.end) {
			offset = 0;

			// itemId (2 bytes)
			*(uint16_t *)(dataBuf + i * 12 + offset) = comp->bodyType;
			offset += 2;

			// dx (2 bytes)
			*(int16_t *)(dataBuf + i * 12 + offset) = comp->offset.x;
			offset += 2;

			// dy (2 bytes)
			*(int16_t *)(dataBuf + i * 12 + offset) = comp->offset.y;
			offset += 2;

			// dz (2 bytes)
			*(int16_t *)(dataBuf + i * 12 + offset) = comp->offset.z;
			offset += 2;

			// flags (4 bytes)
			*(int32_t *)(dataBuf + i * 12 + offset) = comp->invisible;
			offset += 4;

			// No-op sub-item loop (iterates script CVector, does nothing)
			subPtr = (char *)comp->scriptBegin;
			while (subPtr != (char *)comp->scriptEnd) {
				subPtr += 0x10;
			}

			i++;
			comp++;
		}

		// Gap-fill empty slots from lastKey+1 to multiId-1
		for (gapIdx = lastKey + 1; gapIdx < (int)multiId; gapIdx++) {
			CIndexedFileManager_WriteBlock(&indexedFile, gapIdx, &emptyByte, 0, -1);
		}

		lastKey = (int)multiId;

		// Write actual multi data
		CIndexedFileManager_WriteBlock(&indexedFile, (int)multiId, dataBuf, dataSize, extra);

		free(dataBuf);
		count++;

		keyPtr += sizeof(uintptr_t);
	}

	// Pad remaining slots to 0x1000
	for (padIdx = lastKey + 1; padIdx < 0x1000; padIdx++) {
		CIndexedFileManager_WriteBlock(&indexedFile, padIdx, &emptyByte, 0, -1);
	}

	CIndexedFileManager_Close(&indexedFile);
	fclose_ServerSide(txtFile);

	CIndexedFileManager_Repack(&indexedFile, g_multiIdxPath, g_multiMulPath);

	CVector_Destructor(&keyVec);

	CIndexedFileManager_Destructor(&indexedFile);
	USED(count);
}

/*
 * 0x00475767 - CMultiManager::LoadMultiData
 *
 * Parses multi.txt and populates the CResManager hash table with
 * CMultiDef entries (components with bodyType, offset, invisible flag,
 * and optional space-separated script names). Requires format version
 * >= 3; version 3 omits the invisible flag and parses a single script
 * name, later versions take an integer flag followed by a space-
 * separated list. Also computes each multi's min/max extents.
 */
void
CMultiManager_LoadMultiData(CResManager *this, int unused)
{
	FILE *fp;
	char lineBuf[512];
	char scriptBuf[512];
	uint16_t version;
	int typeId;
	int count;
	int i;
	int nfields;
	int extraCount;
	int visibleCount, invisibleCount;
	CVector var_434h;
	CMultiComponentDef var_474h;
	MultiExtentTracker var_458h;
	char typeFlag;

	USED(unused);

	version = 0;

	CResManager_ConstructorBase(this);

	fp = FileManager_OpenByType(0x2A, NULL, "r");
	if (fp == NULL)
		return;

	// Read format version
	if (fgets_ServerSide(lineBuf, 0x1FF, fp) == NULL)
		return;
	sscanf(lineBuf, "%hu", &version);

	if ((version & 0xFFFF) < 3)
		return;

	CVector_Constructor(&var_434h, &typeFlag);
	CMultiComponent_Constructor(&var_474h);
	MultiExtentTracker_Constructor(&var_458h);

	// Outer loop: read multi type entries
	while (fgets_ServerSide(lineBuf, 0x1FF, fp) != NULL) {
		CMultiDef *def;
		uintptr_t key;
		CMultiComponentDef *comp;

		def = (CMultiDef *)malloc(sizeof(CMultiDef));
		if (def != NULL)
			CMultiDef_Constructor(def);

		sscanf(lineBuf, "%d", &typeId);

		fgets_ServerSide(lineBuf, 0x1FF, fp);

		if ((version & 0xFFFF) > 5) {
			sscanf(lineBuf, "%d", &extraCount);
			fgets_ServerSide(lineBuf, 0x1FF, fp);
		} else {
			extraCount = -1;
		}

		if ((version & 0xFFFF) < 5) {
			sscanf(lineBuf, "%d", &visibleCount);
			fgets_ServerSide(lineBuf, 0x1FF, fp);
			sscanf(lineBuf, "%d", &invisibleCount);
			fgets_ServerSide(lineBuf, 0x1FF, fp);
		}

		sscanf(lineBuf, "%d", &count);
		nfields = 0;

		// Inner loop: read each component
		for (i = 0; i < count; i++) {
			fgets_ServerSide(lineBuf, 0x1FF, fp);

			if ((version & 0xFFFF) == 3) {
				nfields = sscanf(lineBuf, "%hu %hd %hd %hd %s", &var_474h.bodyType, (int16_t *)&var_474h.offset.x, (int16_t *)&var_474h.offset.y,
				        &var_474h.offset.z, scriptBuf);

				if (nfields >= 5) {
					CString tempStr;
					CString_Constructor(&tempStr, scriptBuf);
					CDeque16_FindByHash((CVector *)&var_474h.scriptHead, &tempStr);
					CString_Destructor(&tempStr);
				}
			} else {
				nfields = sscanf(lineBuf, "%hu %hd %hd %hd %d %[^\n]s", &var_474h.bodyType, (int16_t *)&var_474h.offset.x, (int16_t *)&var_474h.offset.y,
				        &var_474h.offset.z, &var_474h.invisible, scriptBuf);

				if (nfields >= 6) {
					CString inputStr;
					int pos;
					int lineCount;
					CString outToken;
					CString delimiters;
					CString whitespace;

					CString_Constructor(&inputStr, scriptBuf);
					pos = 0;
					lineCount = 0;
					CString_DefaultConstructor(&outToken);
					CString_Constructor(&delimiters, " ");
					CString_DefaultConstructor(&whitespace);

					while (CString_Tokenize(&inputStr, &pos, &lineCount, &outToken, &delimiters, &whitespace, 1)) {
						CDeque16_FindByHash((CVector *)&var_474h.scriptHead, &outToken);
					}

					CString_Destructor(&whitespace);
					CString_Destructor(&delimiters);
					CString_Destructor(&outToken);
					CString_Destructor(&inputStr);
				}
			}

			CDeque1C_FindByHash(&def->components, &var_474h);
			CMultiComponent_Reset(&var_474h);
		}

		// Iterate components for extent tracking
		comp = (CMultiComponentDef *)def->components.begin;
		while (comp != (CMultiComponentDef *)def->components.end) {
			MultiExtentTracker_Update(&var_458h, comp);
			comp++;
		}

		MultiExtentTracker_GetMinExtent(&var_458h, &def->minExtent);
		MultiExtentTracker_GetMaxExtent(&var_458h, &def->maxExtent);
		MultiExtentTracker_Reset(&var_458h);

		key = (uint32_t)typeId;
		if (!CResManager_FindOrInsertMultiA(this, (uint32_t *)&key, def)) {
			if (def != NULL)
				CMultiDef_ScalarDelete(def, 1);
		}
	}

	fclose_ServerSide(fp);

	CMultiComponent_Destructor(&var_474h);
	CVector_ClearAndFree1C(&var_434h);
}

/*
 * 0x00475D61 - IsLocationInBounds
 *
 * Inclusive bounding-rectangle test on a CLocation passed by value.
 */
int
IsLocationInBounds(CLocation loc, int x1, int y1, int x2, int y2)
{
	int x, y;

	x = (int)(int16_t)loc.x;
	y = (int)(int16_t)loc.y;

	if (x < x1)
		return 0;
	if (x > x2)
		return 0;
	if (y < y1)
		return 0;
	if (y > y2)
		return 0;
	return 1;
}

/*
 * 0x00475DA2 - CEntityMap::SpatialNext
 *
 * Advances a spatial-chain cursor: container items follow spatialNext,
 * other resource entities follow nextInContainer, anything else ends
 * the walk.
 */
static CItem *
CEntityMap_SpatialNext(CEntityMap *this, CItem *item)
{
	USED(this);
	if (((int (*)(void *))VT_FN(item, VT_IS_CONTAINER))(item))
		return (CItem *)item->spatialNext;
	if (((int (*)(void *))VT_FN(item, VT_ATTACH_SPATIAL))(item))
		return (CItem *)item->resourceEntity.nextInContainer;
	return NULL;
}

/*
 * 0x00475DDF - CMultiSlave::CollectEntities
 *
 * Walks the spatial chain from startItem, pushing items that lie within
 * the bounding rectangle and have a non-zero bodyType, skipping mobiles
 * and removed entities. Updates the running min/max extents and minZ
 * sentinel (0xFFFFFF00).
 */
static __attribute__((unused)) void
CMultiSlave_CollectEntities(CEntityMap *this, CItem *startItem, int x1, int y1, int x2, int y2, CVector *outSerials, CLocation *maxExtent, CLocation *minExtent, int *minZ)
{
	CItem *item;

	item = startItem;
	while (item != NULL) {
		// Skip entities removed from world or that are mobiles
		if (((int (*)(void *))VT_FN(item, VT_IS_CONTAINER))(item)) {
			if (VT_IsMobile(item)) {
				item = CEntityMap_SpatialNext(this, item);
				continue;
			}
		}

		// Check if entity is within bounding rectangle
		{
			CLocation loc;
			CLocation_SetLoc(&loc, &item->resourceEntity.entity.location);
			if (!IsLocationInBounds(loc, x1, y1, x2, y2)) {
				item = CEntityMap_SpatialNext(this, item);
				continue;
			}
		}

		// Skip entities with bodyType == 0
		if ((CEntity_GetBodyType(item) & 0xFFFF) == 0) {
			item = CEntityMap_SpatialNext(this, item);
			continue;
		}

		// Add entity pointer to output vector
		CVector_PushBack(outSerials, (uintptr_t)item);

		// Track max x
		if ((int16_t)maxExtent->x != -1) {
			if ((int16_t)maxExtent->x <= (int16_t)item->resourceEntity.entity.location.x)
				goto skip_max_x;
		}
		maxExtent->x = item->resourceEntity.entity.location.x;
skip_max_x:

		// Track max y
		if ((int16_t)maxExtent->y != -1) {
			if ((int16_t)maxExtent->y <= (int16_t)item->resourceEntity.entity.location.y)
				goto skip_max_y;
		}
		maxExtent->y = item->resourceEntity.entity.location.y;
skip_max_y:

		// Track min x
		if ((int16_t)minExtent->x != -1) {
			if ((int16_t)minExtent->x >= (int16_t)item->resourceEntity.entity.location.x)
				goto skip_min_x;
		}
		minExtent->x = item->resourceEntity.entity.location.x;
skip_min_x:

		// Track min y
		if ((int16_t)minExtent->y != -1) {
			if ((int16_t)minExtent->y >= (int16_t)item->resourceEntity.entity.location.y)
				goto skip_min_y;
		}
		minExtent->y = item->resourceEntity.entity.location.y;
skip_min_y:

		// Track min z
		if (*minZ != (int)0xFFFFFF00) {
			if ((int16_t)item->resourceEntity.entity.location.z >= *minZ)
				goto skip_min_z;
		}
		*minZ = (int)(int16_t)item->resourceEntity.entity.location.z;
skip_min_z:

		item = CEntityMap_SpatialNext(this, item);
	}
}

/*
 * 0x004765FC - CMultiManager::BuildComponents
 *
 * Instantiates every component of multi type typeId. The first
 * component becomes the owner - reusing existing when it is non-null -
 * and takes the multi slave; the rest attach to it as components. Each
 * gets the definition's scripts, and its server-only flag comes from
 * the definition unless serverOnly is set. Returns the owner, or 0 when
 * typeId is unknown.
 */
static __attribute__((unused)) CItem *
CMultiManager_BuildComponents(CResManager *this, uint32_t typeId, int serverOnly, CItem *existing)
{
	CSearchCtx ctx, found;
	CMultiDef *def;
	CMultiComponentDef *ptr;
	CItem *owner;
	CItem *item;
	int isFirst;
	int counter;
	void *iterCtx;
	void *scriptOutput;
	uintptr_t entity;

	owner = NULL;
	item = NULL;

	CSearchCtx_Constructor(&ctx);
	CSearchCtx_Add(&ctx, CResManager_FindByKey_A(this, &found, &typeId, 1));
	if (!CSearchCtx_Find(&ctx))
		return NULL;

	def = (CMultiDef *)CResManager_GetResult_Defines(this, &ctx);
	isFirst = 1;
	counter = 0;

	for (ptr = (CMultiComponentDef *)def->components.begin; ptr != (CMultiComponentDef *)def->components.end; ptr++) {
		if (isFirst == 1 && existing != NULL)
			item = existing;
		else
			item = CWorld_CreateItem(g_World, ptr->bodyType);

		counter++;

		if (isFirst) {
			isFirst = 0;
			owner = item;
			CItem_AttachMultiSlave(owner, &ptr->offset);
			CMultiSlave_SetTypeId(CItem_GetMultiSlave(owner), (int32_t)typeId);
		} else {
			CItem_AttachMultiComponent(item, CMobile_GetSerial((CMobile *)owner), &ptr->offset);
		}

		CMultiSlave_AddComponent(CItem_GetMultiSlave(owner), CMobile_GetSerial((CMobile *)item));

		CIterCtx_Set(&iterCtx, item);
		entity = (uintptr_t)*(void **)&iterCtx;
		AttachScriptsFromVector(&scriptOutput, (char *)ptr->scriptBegin, (char *)ptr->scriptEnd, entity);

		if (serverOnly == 0)
			CItem_SetServerOnly(item, ptr->invisible);
		else
			CItem_SetServerOnly(item, 1);
	}

	USED(counter);

	if (owner != NULL)
		CMultiSlave_ComputeRange(CItem_GetMultiSlave(owner), def);

	return owner;
}

/*
 * 0x004767A4 - CMultiManager::Create
 *
 * Instantiates a multi of the given typeId: the first component becomes
 * the CMultiSlave owner, subsequent components get a CMultiComponent.
 * Each item is placed at loc plus its offset, set invisible, dropped
 * at feet, and has its scripts attached. After placement, the owner
 * is detached from the spatial grid, marked valid, and ComputeRange
 * is invoked.
 */
CItem *
CMultiManager_Create(CResManager *this, int typeId, CLocation *loc, int flags, CItem *ownerItem)
{
	CSearchCtx ctx;
	CSearchCtx searchArea;
	CMultiDef *def;
	CItem *owner;
	CItem *item;
	CLocation worldLoc;
	int firstIter;
	int counter;
	CMultiComponentDef *ptr;
	uint32_t key;
	void *iterCtx;
	uintptr_t entity;
	void *scriptOutput;

	owner = NULL;
	item = NULL;

	CSearchCtx_Constructor(&ctx);

	key = (uint32_t)typeId;
	CResManager_FindByKey_A(this, &searchArea, &key, 1);
	CSearchCtx_Add(&ctx, &searchArea);

	if (!CSearchCtx_Find(&ctx))
		return NULL;

	def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);

	firstIter = 1;
	counter = 0;

	ptr = (CMultiComponentDef *)def->components.begin;
	while ((char *)ptr != (char *)def->components.end) {
		// First iteration: use provided ownerItem or create new
		if (firstIter == 1 && ownerItem != NULL) {
			item = ownerItem;
		} else {
			item = CWorld_CreateItem(g_World, ptr->bodyType);
		}

		// CItem_Setup condition:
		// Call Setup when flags == 0, or when not first iteration
		// and flags == 1. Skip if item == provided ownerItem.
		if (flags == 0 || (firstIter == 0 && flags == 1)) {
			if (item != ownerItem)
				CItem_Setup(item, 2, loc, 0, 1);
		}

		counter++;

		if (firstIter != 0) {
			firstIter = 0;
			owner = item;

			// Attach CMultiSlave to the owner item
			CItem_AttachMultiSlave(item, &ptr->offset);

			// Set typeId on the slave
			CMultiSlave_SetTypeId(CItem_GetMultiSlave(owner), typeId);

			// Clear valid bit on the slave
			CMultiComponent_SetValid(&CItem_GetMultiSlave(owner)->base, 0);
		} else {
			// Attach CMultiComponent to non-owner items
			CItem_AttachMultiComponent(item, CMobile_GetSerial((CMobile *)owner), &ptr->offset);
		}

		// Add this item's serial to the owner's slave component list
		CMultiSlave_AddComponent(CItem_GetMultiSlave(owner), CMobile_GetSerial((CMobile *)item));

		// Compute world position: loc + component offset
		CLocation_SetLoc(&worldLoc, loc);
		CLocation_Add(&worldLoc, CMulti_GetOffset(CItem_GetMulti(item)));

		// Hide from clients before drop
		CItem_SetServerOnly(item, 1);

		// VT_DROP_AT_FEET
		((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &worldLoc);

		// Attach scripts via CIterCtx_Set + AttachScriptsFromVector
		CIterCtx_Set(&iterCtx, item);
		entity = (uintptr_t)*(void **)&iterCtx;
		{
			char *_sfirst = (char *)ptr->scriptBegin;
			char *_slast = (char *)ptr->scriptEnd;
			AttachScriptsFromVector(&scriptOutput, _sfirst, _slast, entity);
		}

		// Apply component def's server-only flag
		CItem_SetServerOnly(item, ptr->invisible);

		ptr++;
	}

	// VT_DETACH_SPATIAL on owner (binary: no NULL check)
	((void (*)(void *))VT_FN(owner, VT_DETACH_SPATIAL))(owner);

	// Set valid bit on the owner's slave (binary: no NULL check)
	CMultiComponent_SetValid(&CItem_GetMultiSlave(owner)->base, 1);

	// Compute range from component extents
	if (owner != NULL)
		CMultiSlave_ComputeRange(CItem_GetMultiSlave(owner), def);

	USED(counter);
	return owner;
}

/*
 * 0x004769D0 - CMultiManager::MakeMulti
 *
 * Creates the multi and drops its owner at loc. Returns the owner or
 * NULL on failure.
 */
CItem *
CMultiManager_MakeMulti(CResManager *this, int typeId, CLocation *loc, int flags)
{
	CItem *result;

	result = CMultiManager_Create(this, typeId, loc, 0, (CItem *)(intptr_t)flags);
	if (result == NULL)
		return NULL;

	// VT_DROP_AT_FEET on the owner
	((void (*)(void *, void *))VT_FN(result, VT_DROP_AT_FEET))(result, loc);

	return result;
}

/*
 * 0x00476A14 - CMultiManager::MakeMultiCheck
 *
 * Creates a multi at loc only if it fits; writes a status code to
 * *result (2 on success, or a negative code at the first failing
 * step). On failure the partial multi is deleted.
 */
CItem *
CMultiManager_MakeMultiCheck(CResManager *this, int typeId, CLocation *loc, int moveType, int terrainFlags, int *result, int arg6, int arg7, int arg8, int arg9, CItem *ownerItem)
{
	CItem *item;
	CMultiSlave *slave;
	int canExist;

	USED(arg6);
	USED(arg7);
	USED(arg8);

	// 1. Check if multi can exist at location
	canExist = CMultiManager_CanExistAt(&g_MultiManager, typeId, loc, moveType, NULL);
	if (canExist <= 0) {
		*result = canExist;
		return NULL;
	}

	// 2. Create the multi
	item = CMultiManager_Create(this, typeId, loc, 0, ownerItem);
	if (item == NULL) {
		*result = -3;
		return NULL;
	}

	// 3. Get the slave
	slave = CItem_GetMultiSlave(item);
	if (slave == NULL) {
		*result = -4;
		if (item != NULL) {
			// VT_DELETE
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);
		}
		return NULL;
	}

	// 4. Check slave can exist at location
	canExist = CMultiSlave_CanExistAt(slave, loc, moveType, arg9, terrainFlags, ownerItem);
	if (canExist <= 0) {
		*result = -5;
		if (item != NULL) {
			// VT_DELETE
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);
		}
		return NULL;
	}

	// 5. Success: drop at feet and return
	((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
	*result = 2;
	return item;
}

/*
 * 0x00476B10 - CMultiManager::RecycleMulti
 *
 * Replaces the components of an existing multi with those of bodyType:
 * existing items are repurposed where possible, new items are spawned
 * for any extras, and any leftover old components are deleted.
 */
int
CMultiManager_RecycleMulti(CResManager *this, int bodyType, CMultiSlave *slave, int flags)
{
	CSearchCtx ctx;
	CSearchCtx searchArea;
	CVector oldSerials;
	CMultiDef *def;
	CItem *ownerItemResult;
	CItem *item;
	CLocation tmpLoc;
	int counter;
	uint32_t oldBodyType;
	CMultiComponentDef *defPtr;
	char *serialPtr;
	uint32_t key;
	void *iterCtx;
	uintptr_t entity;
	void *scriptOutput;
	char typeFlag;

	ownerItemResult = NULL;
	item = NULL;

	CSearchCtx_Constructor(&ctx);

	typeFlag = 0;
	CVector_Constructor(&oldSerials, &typeFlag);

	counter = 0;

	key = (uint32_t)bodyType;
	CResManager_FindByKey_A(this, &searchArea, &key, 1);
	CSearchCtx_Add(&ctx, &searchArea);

	if (!CSearchCtx_Find(&ctx)) {
		CVector_Destructor(&oldSerials);
		return 0;
	}

	def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);

	counter = 0;

	if (CVector_GetCount(&slave->components) < 2) {
		CVector_Destructor(&oldSerials);
		return 0;
	}

	CVector_Swap16(&oldSerials, &slave->components);

	// Get raw pointers for dual iteration
	defPtr = (CMultiComponentDef *)def->components.begin;
	serialPtr = (char *)oldSerials.begin;

	// Main loop: pointer-stride 0x1C for defs, pointer-sized for old serials
	while ((char *)defPtr != (char *)def->components.end) {
		item = NULL;
		if (serialPtr != (char *)oldSerials.end) {
			item = CWorld_FindBySerial(g_World, *(uint32_t *)serialPtr);
		}

		if (item == NULL) {
			// New item path: create item, no CItem_Setup
			item = CWorld_CreateItem(g_World, defPtr->bodyType);

			counter++;

			// Attach CMultiComponent
			CItem_AttachMultiComponent(item, CMobile_GetSerial((CMobile *)slave->base.ownerItem), &defPtr->offset);

			CMultiSlave_AddComponent(slave, CMobile_GetSerial((CMobile *)item));

			// Compute world position
			CLocation_SetLoc(&tmpLoc, CEntity_GetLocation(&((CItem *)slave->base.ownerItem)->resourceEntity.entity));
			CLocation_Add(&tmpLoc, CMulti_GetOffset(CItem_GetMulti(item)));

			CItem_SetServerOnly(item, 1);

			// VT_DROP_AT_FEET
			((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &tmpLoc);

			// Attach scripts via CIterCtx_Set + AttachScriptsFromVector
			CIterCtx_Set(&iterCtx, item);
			entity = (uintptr_t)*(void **)&iterCtx;
			AttachScriptsFromVector(&scriptOutput, (char *)defPtr->scriptBegin, (char *)defPtr->scriptEnd, entity);

			// VT_HIDE (binary calls hide after drop in recycle)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);

			CItem_SetServerOnly(item, defPtr->invisible);
		} else {
			// Existing item path
			CItem_HasMulti_Filter(item);

			oldBodyType = CEntity_GetBodyType(item) & 0xFFFF;

			// flags-based bodyType skip:
			// if flags != 0 and counter == 0, skip bodyType update
			if (flags == 0 || counter != 0)
				CEntity_SetBodyType(item, defPtr->bodyType);

			if (counter == 0 && CItem_IsMultiOwner(item) == 1)
				ownerItemResult = item;

			counter++;

			if (CItem_IsMultiOwner(item)) {
				CMultiSlave_SetTypeId(CItem_GetMultiSlave(item), bodyType);
			}

			CMultiComponent_SetOffset(CItem_GetMulti(item), &defPtr->offset);

			CMultiSlave_AddComponent(slave, CMobile_GetSerial((CMobile *)item));

			// Compute world position
			CLocation_SetLoc(&tmpLoc, CEntity_GetLocation(&((CItem *)slave->base.ownerItem)->resourceEntity.entity));
			CLocation_Add(&tmpLoc, CMulti_GetOffset(CItem_GetMulti(item)));

			CItem_SetServerOnly(item, 1);

			((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &tmpLoc);

			// Fire event 0x30
			Entity_ExecuteEvent((CEntity *)item, 0x30, (int)oldBodyType, (int)defPtr->bodyType);

			// VT_HIDE (binary calls hide after drop in recycle)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);

			CItem_SetServerOnly(item, defPtr->invisible);
		}

		defPtr++;
		if (serialPtr != (char *)oldSerials.end)
			serialPtr += sizeof(uintptr_t);
	}

	// Cleanup: delete excess old components
	while (serialPtr != (char *)oldSerials.end) {
		item = CWorld_FindBySerial(g_World, *(uint32_t *)serialPtr);
		if (item != NULL) {
			if (item != NULL) {
				((void (*)(void *))VT_FN(item, VT_DELETE))(item);
			}
		}
		serialPtr += sizeof(uintptr_t);
	}

	// Recalculate range
	if (ownerItemResult != NULL) {
		CMultiSlave_ComputeRange(CItem_GetMultiSlave(ownerItemResult), def);
	}

	CVector_Destructor(&oldSerials);
	return 1;
}

/*
 * 0x00476F03 - CMultiManager::MoveMultiInternal
 *
 * Worker for MoveMulti: reuses existing components where possible,
 * spawns new ones for extras, deletes leftover serials, then detaches
 * the owner from the spatial grid and recomputes its range.
 */
static int
CMultiManager_MoveMultiInternal(CResManager *this, uint16_t typeId, CMultiSlave *slave, CLocation *loc, CItem *owner)
{
	CSearchCtx ctx;
	CSearchCtx searchArea;
	CVector oldSerials;
	CMultiDef *def;
	CItem *ownerItemResult;
	CItem *item;
	CLocation tmpLoc;
	int counter;
	uint32_t oldBodyType;
	CMultiComponentDef *defPtr;
	char *serialPtr;
	uint32_t key;
	void *iterCtx;
	uintptr_t entity;
	void *scriptOutput;
	char typeFlag;

	ownerItemResult = NULL;
	item = NULL;

	CSearchCtx_Constructor(&ctx);

	typeFlag = 0;
	CVector_Constructor(&oldSerials, &typeFlag);

	counter = 0;

	key = (uint32_t)typeId;
	CResManager_FindByKey_A(this, &searchArea, &key, 1);
	CSearchCtx_Add(&ctx, &searchArea);

	if (!CSearchCtx_Find(&ctx)) {
		CVector_Destructor(&oldSerials);
		return 0;
	}

	def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);

	counter = 0;

	if (CVector_GetCount(&slave->components) < 2) {
		CVector_Destructor(&oldSerials);
		return 0;
	}

	CVector_Swap16(&oldSerials, &slave->components);

	// Get raw pointers for dual iteration
	defPtr = (CMultiComponentDef *)def->components.begin;
	serialPtr = (char *)oldSerials.begin;

	// Main loop: pointer-stride 0x1C for defs, pointer-sized for old serials
	while ((char *)defPtr != (char *)def->components.end) {
		item = NULL;
		if (serialPtr != (char *)oldSerials.end) {
			item = CWorld_FindBySerial(g_World, *(uint32_t *)serialPtr);
		}

		if (item == NULL) {
			// New item path: create item with bodyType from comp def
			item = CWorld_CreateItem(g_World, defPtr->bodyType);

			CItem_Setup(item, 2, loc, 0, 1);
			counter++;

			// Attach CMultiComponent linking to owner
			CItem_AttachMultiComponent(item, CMobile_GetSerial((CMobile *)slave->base.ownerItem), &defPtr->offset);

			CMultiSlave_AddComponent(slave, CMobile_GetSerial((CMobile *)item));

			// Compute world position
			CLocation_SetLoc(&tmpLoc, CEntity_GetLocation(&((CItem *)slave->base.ownerItem)->resourceEntity.entity));
			CLocation_Add(&tmpLoc, CMulti_GetOffset(CItem_GetMulti(item)));

			CItem_SetServerOnly(item, 1);

			// VT_DROP_AT_FEET
			((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &tmpLoc);

			// Attach scripts via CIterCtx_Set + AttachScriptsFromVector
			CIterCtx_Set(&iterCtx, item);
			entity = (uintptr_t)*(void **)&iterCtx;
			AttachScriptsFromVector(&scriptOutput, (char *)defPtr->scriptBegin, (char *)defPtr->scriptEnd, entity);

			CItem_SetServerOnly(item, defPtr->invisible);
		} else {
			// Existing item path
			CItem_GetMulti(item);

			oldBodyType = CEntity_GetBodyType(item) & 0xFFFF;

			// Skip bodyType update for first component when owner is provided
			if (owner == NULL || counter != 0)
				CEntity_SetBodyType(item, defPtr->bodyType);

			if (counter == 0 && CItem_IsMultiOwner(item) == 1)
				ownerItemResult = item;

			counter++;

			if (CItem_IsMultiOwner(item)) {
				CMultiSlave_SetTypeId(CItem_GetMultiSlave(item), (int32_t)typeId);
			}

			CMultiComponent_SetOffset(CItem_GetMulti(item), &defPtr->offset);

			CMultiSlave_AddComponent(slave, CMobile_GetSerial((CMobile *)item));

			// Compute world position
			CLocation_SetLoc(&tmpLoc, CEntity_GetLocation(&((CItem *)slave->base.ownerItem)->resourceEntity.entity));
			CLocation_Add(&tmpLoc, CMulti_GetOffset(CItem_GetMulti(item)));

			CItem_SetServerOnly(item, 1);

			((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &tmpLoc);

			// Fire event 0x30
			Entity_ExecuteEvent((CEntity *)item, 0x30, (int)oldBodyType, (int)defPtr->bodyType);

			CItem_SetServerOnly(item, defPtr->invisible);
		}

		defPtr++;
		if (serialPtr != (char *)oldSerials.end)
			serialPtr += sizeof(uintptr_t);
	}

	// Cleanup: delete excess old components
	while (serialPtr != (char *)oldSerials.end) {
		item = CWorld_FindBySerial(g_World, *(uint32_t *)serialPtr);
		if (item != NULL) {
			if (item != NULL) {
				((void (*)(void *))VT_FN(item, VT_DELETE))(item);
			}
		}
		serialPtr += sizeof(uintptr_t);
	}

	// VT_DETACH_SPATIAL on owner (binary: unconditional)
	((void (*)(void *))VT_FN(ownerItemResult, VT_DETACH_SPATIAL))(ownerItemResult);

	// Recalculate range
	if (ownerItemResult != NULL) {
		CMultiSlave_ComputeRange(CItem_GetMultiSlave(ownerItemResult), def);
	}

	CVector_Destructor(&oldSerials);
	return 1;
}

/*
 * 0x004772FD - CMultiManager::MoveMulti
 *
 * Hides the multi's owner, reshapes its components for typeId at loc
 * via MoveMultiInternal, then drops the owner at loc.
 */
int
CMultiManager_MoveMulti(CResManager *this, uint16_t typeId, CMultiSlave *slave, CLocation *loc, CItem *owner)
{
	CItem *ownerItem;
	int result;

	USED(this);

	ownerItem = (CItem *)slave->base.ownerItem;

	if (!ownerItem->resourceEntity.entity.removedFromWorld) {
		((void (*)(void *))VT_FN(ownerItem, VT_HIDE))(ownerItem);
	}

	result = CMultiManager_MoveMultiInternal(&g_MultiManager, typeId, slave, loc, owner);

	ownerItem = (CItem *)slave->base.ownerItem;
	((void (*)(void *, void *))VT_FN(ownerItem, VT_DROP_AT_FEET))(ownerItem, loc);

	return result;
}

/*
 * 0x0047736D - CMultiManager::RecycleMultiCheck
 *
 * Hides carried items and the owner, validates the target location,
 * and (on success) reshapes the multi's components via MoveMultiInternal
 * before dropping the owner and restoring carried items.
 */
int
CMultiManager_RecycleMultiCheck(CResManager *this, int bodyType, CMultiSlave *slave, CLocation *loc, int checkFlag, int flags)
{
	CVector itemList;
	char typeFlag;
	CItem *ownerItem;
	int result;

	USED(this);

	typeFlag = 0;
	CVector_Constructor(&itemList, &typeFlag);

	// If carry, collect and hide items
	if (slave->carry != 0) {
		CMultiSlave_GetItems(slave, &itemList);
		HideItemsInVector(&itemList);
	}

	// Hide ownerItem if not removed
	ownerItem = (CItem *)slave->base.ownerItem;
	if (!ownerItem->resourceEntity.entity.removedFromWorld) {
		((void (*)(void *))VT_FN(ownerItem, VT_HIDE))(ownerItem);
	}

	// Check if multi can exist at location
	result = CMultiManager_CanExistAt(&g_MultiManager, bodyType, loc, checkFlag, NULL);

	// If can exist, perform the move
	if (result > 0) {
		result = CMultiManager_MoveMultiInternal(&g_MultiManager, (uint16_t)bodyType, slave, loc, (CItem *)(intptr_t)flags);
	}

	// Drop ownerItem at feet
	ownerItem = (CItem *)slave->base.ownerItem;
	((void (*)(void *, void *))VT_FN(ownerItem, VT_DROP_AT_FEET))(ownerItem, loc);

	// Restore carried items
	if (slave->carry != 0) {
		RestoreItemsInVector(&itemList);
	}

	CVector_Destructor(&itemList);
	return result;
}

/*
 * 0x004774A5 - CMultiSlave::MapSwitchMove
 *
 * MODIFIED: returns 0 if loc is within the configured playable bounds;
 * otherwise the binary builds a throw-away itemList and returns 1
 * without actually moving the boat - an unfinished stub left in the
 * demo. With FEAT_BOAT_MAPSWITCH the call routes through
 * CMultiSlave_MapSwitchMove_Wrap, which wraps loc Felucca-style (only
 * when the boat's owner is on the Felucca side, matching
 * CLocation_AddWrapped), moves the multi via CMultiSlave_Move, and
 * fires the serverswitch script trigger (0x32) on each carried player
 * so shipstuff.m's shipnakedhack callback completes.
 */
int
CMultiSlave_MapSwitchMove(CMultiSlave *slave, CLocation *loc, int checkFlag)
{
	CVector itemList;
	USED(checkFlag);
	char typeFlag;
	uintptr_t *iter;
	CItem *compItem;
	int result;

	if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;

	if (feat(FEAT_BOAT_MAPSWITCH))
		return CMultiSlave_MapSwitchMove_Wrap(slave, loc);

	typeFlag = 0;
	CVector_Constructor(&itemList, &typeFlag);

	iter = (uintptr_t *)slave->components.begin;
	for (;;) {
		if (iter == (uintptr_t *)slave->components.end)
			break;
		compItem = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (compItem != NULL)
			CVector_PushBack(&itemList, (uintptr_t)compItem);
		iter++;
	}

	if (slave->carry == 1)
		CMultiSlave_GetItems(slave, &itemList);

	result = 1;
	CVector_Destructor(&itemList);
	return result;
}

/*
 * 0x00477592 - StaticInit_RotationTable
 *
 * MSVC C++ static initializer: constructs the global g_RotationTable. Called
 * by the MSVC __initterm glue at 0x00477588 before WinMain. Linux calls
 * CRotationTable_Constructor directly from main() instead, so this function
 * is unreachable in our build; retained for exact binary correspondence.
 */
void
StaticInit_RotationTable(void)
{
	CRotationTable_Constructor(&g_RotationTable);
}

/*
 * 0x004775A1 - CMultiManager::RecycleMultiCheckRotate
 *
 * Rotating variant of RecycleMultiCheck: rotates carried items around
 * the owner, validates components, and either recycles into bodyType
 * (applying the new direction to mobiles) or restores the saved
 * locations on failure.
 */
int
CMultiManager_RecycleMultiCheckRotate(CResManager *this, int bodyType, CMultiSlave *slave, CLocation *loc, int rotation, int checkFlag, int flags)
{
	CVector itemList;
	CLocation zeroLoc;
	CVector locList;
	CMultiRotateRect rect;
	CMultiRotateRect rotOutput;
	CItem *ownerItem;
	int result;
	char *ptr;
	char typeFlag;
	char locTypeFlag;
	uintptr_t ctx;
	uintptr_t restOutput;

	USED(this);

	typeFlag = 0;
	CVector_Constructor(&itemList, &typeFlag);

	if (slave->carry != 0)
		CMultiSlave_GetItems(slave, &itemList);

	CLocation_Init(&zeroLoc);

	locTypeFlag = 0;
	CVector_Constructor(&locList, &locTypeFlag);

	if (slave->carry != 0) {
		// Build location list from items
		ptr = (char *)itemList.begin;
		while (ptr != (char *)itemList.end) {
			CItem *item = *(CItem **)ptr;
			if (item != NULL) {
				CVector_PushBack6(&locList, &item->resourceEntity.entity.location);
			} else {
				CVector_PushBack6(&locList, &zeroLoc);
			}
			ptr += sizeof(uintptr_t);
		}

		// Hide items
		HideItemsInVector_Raw((uintptr_t *)itemList.begin, (uintptr_t *)itemList.end, 0);

		// Apply rotation transform
		ownerItem = (CItem *)slave->base.ownerItem;
		CMultiRotateRect_Init(&rect, &ownerItem->resourceEntity.entity.location, loc, (uint32_t)checkFlag);
		RotateAndRelocateItems(&rotOutput, (uintptr_t *)itemList.begin, (uintptr_t *)itemList.end, &rect);
	}

	// If ownerItem not removed from world, hide it
	ownerItem = (CItem *)slave->base.ownerItem;
	if (!ownerItem->resourceEntity.entity.removedFromWorld) {
		((void (*)(void *))VT_FN(ownerItem, VT_HIDE))(ownerItem);
	}

	// Validate components
	result = CMultiManager_ValidateComponents(&g_MultiManager, &itemList, rotation, NULL);

	// If valid, check if multi can exist at location
	if (result > 0) {
		result = CMultiManager_CanExistAt(&g_MultiManager, bodyType, loc, rotation, NULL);
	}

	// If can exist, perform the recycle
	if (result > 0) {
		result = CMultiManager_RecycleMulti(&g_MultiManager, bodyType, slave, flags);
	}

	// Drop ownerItem at feet
	ownerItem = (CItem *)slave->base.ownerItem;
	((void (*)(void *, void *))VT_FN(ownerItem, VT_DROP_AT_FEET))(ownerItem, loc);

	if (result <= 0) {
		// Failure: restore item locations from locList
		CMultiRotateRect_SetRotation((CMultiRotateRect *)&ctx, (uintptr_t)locList.begin);
		SetLocationsFromVector(&restOutput, (uintptr_t *)itemList.begin, (uintptr_t *)itemList.end, ctx);
	} else {
		// Success: rotate mobile directions
		CMultiRotateRect_SetRotation((CMultiRotateRect *)&ctx, (uintptr_t)checkFlag);
		SetDirectionsFromVector(&restOutput, (uintptr_t *)itemList.begin, (uintptr_t *)itemList.end, ctx);
	}

	// Restore carried items
	if (slave->carry != 0) {
		RestoreItems_Raw((uintptr_t *)itemList.begin, (uintptr_t *)itemList.end, 0);
	}

	CVector_ClearAndFree6(&locList);
	CVector_Destructor(&itemList);

	return result;
}

/*
 * 0x00477829 - CMultiManager::CanExistAt
 *
 * Checks whether a multi of typeId fits at loc: valid coordinates and
 * passable terrain at every component's absolute position. Returns 1
 * if all pass, 0 for terrain block, -1 out of bounds, -2 if typeId is
 * unknown.
 */
int
CMultiManager_CanExistAt(CResManager *this, int typeId, CLocation *loc, int moveType, CItem *item)
{
	CSearchCtx searchArea;
	CMultiDef *def;
	CLocation compLoc;
	int height;
	CMultiComponentDef *ptr;
	uint32_t key;

	USED(item);

	key = (uint32_t)typeId;
	CResManager_FindByKey_A(this, &searchArea, &key, 1);
	if (!CSearchCtx_Find(&searchArea))
		return -2;

	def = (CMultiDef *)CResManager_GetResultCtx(this, &searchArea);

	ptr = (CMultiComponentDef *)def->components.begin;

	CLocation_Init(&compLoc);

	while ((char *)ptr != (char *)def->components.end) {
		// Copy base location, add component offset
		CLocation_SetLoc(&compLoc, loc);
		CLocation_Add(&compLoc, &ptr->offset);

		// Validate coordinates are in bounds
		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)compLoc.x, (int16_t)compLoc.y))
			return -1;

		height = CWorld_GetItemTileQuantity(ptr->bodyType);
		{
			int _tcb = CTerrainManager_CheckMoveBlocked(compLoc, height, moveType, NULL, 0);
			if (!(_tcb & 4))
				return 0;
		}

		ptr++;
	}

	return 1;
}

/*
 * 0x00477921 - CMultiManager::AreMobilesInArea
 *
 * Returns 1 if any mobile is present at any component tile of the
 * multi (z range [-128,+128]).
 *
 * FIXED: the binary queries the spatial grid at loc itself instead of
 * loc + component offset, missing every component past the origin.
 * We query each component's actual world position.
 *
 * MODIFIED: the spatial query is inlined as a direct g_SpatialGrid
 * block walk with a VT_IsMobile filter.
 */
int
CMultiManager_AreMobilesInArea(CResManager *this, int typeId, CLocation *loc)
{
	CMultiDef *def;
	CMultiComponentDef *comp;
	CMultiComponentDef *compBase;
	int componentCount;
	CLocation compLoc;
	int i;
	int blockIdx;
	CItem *cur;
	CLocation *eloc;
	int count;

	// 0x0047793f-0x0047794c: map lookup
	def = CMultiManager_FindType(this, typeId);
	if (def == NULL)
		return 0;

	compBase = (CMultiComponentDef *)def->components.begin;
	componentCount = (int)(((char *)def->components.end - (char *)def->components.begin) / (ptrdiff_t)sizeof(CMultiComponentDef));

	CLocation_Init(&compLoc);

	for (i = 0; i < componentCount; i++) {
		comp = &compBase[i];

		CLocation_CopyFrom(&compLoc, loc);
		CLocation_Add(&compLoc, &comp->offset);

		// Inlined spatial query at compLoc with z [-128, +128] and IsMobile filter.
		count = 0;
		blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)compLoc.x, (int)compLoc.y, 0);
		if (blockIdx >= 0) {
			cur = g_SpatialGrid.cells[blockIdx].itemHead;
			while (cur != NULL) {
				eloc = &cur->resourceEntity.entity.location;
				if ((int)compLoc.x == (int)eloc->x && (int)compLoc.y == (int)eloc->y && (int)eloc->z >= -128 && (int)eloc->z <= 128 && VT_IsMobile(cur))
					count++;
				cur = cur->spatialNext;
			}
		}

		if (count > 0)
			return 1;
	}

	return 0;
}

/*
 * 0x00477A4A - CMultiManager::ValidateComponents
 *
 * Returns 1 if every item in itemList has valid coordinates and
 * passable terrain, 0 for terrain block, -1 out of bounds.
 */
static int
CMultiManager_ValidateComponents(CResManager *this, CVector *itemList, int moveType, CItem *unused)
{
	char *ptr;
	int height;

	USED(this);
	USED(unused);

	ptr = (char *)itemList->begin;
	while (ptr != (char *)itemList->end) {
		CItem *item = *(CItem **)ptr;

		if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)item->resourceEntity.entity.location.x, (int16_t)item->resourceEntity.entity.location.y))
			return -1;

		height = ((int (*)(void *))VT_FN(item, VT_GET_HEIGHT))(item);
		if (!(CTerrainManager_CheckMoveBlocked(item->resourceEntity.entity.location, height, moveType, NULL, 0) & 4))
			return 0;

		ptr += sizeof(uintptr_t);
	}

	return 1;
}

/*
 * 0x00477AED - CMultiManager::GetExtents
 *
 * Copies the multi's min/max extents into minLoc/maxLoc. Returns 1 on
 * success, 0 if typeId is unknown.
 */
int
CMultiManager_GetExtents(CResManager *this, int typeId, CLocation *minLoc, CLocation *maxLoc)
{
	CSearchCtx ctx;
	uint32_t key = (uint32_t)typeId;

	CResManager_FindByKey_A(this, &ctx, &key, 1);
	if (!CSearchCtx_Find(&ctx))
		return 0;

	CMultiDef *def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);
	CLocation_SetLoc(minLoc, &def->minExtent);
	CLocation_SetLoc(maxLoc, &def->maxExtent);
	return 1;
}

/*
 * 0x00477B50 - CMultiManager::GetNumInType
 *
 * Writes the multi's component count into *outCount. Returns 1 on
 * success, 0 if typeId is unknown.
 */
int
CMultiManager_GetNumInType(CResManager *this, int typeId, int *outCount)
{
	CSearchCtx ctx;
	uint32_t key = (uint32_t)typeId;

	CResManager_FindByKey_A(this, &ctx, &key, 1);
	if (!CSearchCtx_Find(&ctx))
		return 0;

	*outCount = CVector_GetCount1C(&((CMultiDef *)CResManager_GetResultCtx(this, &ctx))->components);
	return 1;
}

/*
 * 0x00477BA1 - CMultiManager::GetSize
 *
 * Writes the multi's extent spans - max minus min on each axis, without
 * the +1 CMobile_OffsetPastMulti adds - and returns 1. Returns 0 and
 * leaves the outputs alone when typeId is unknown.
 */
static __attribute__((unused)) int
CMultiManager_GetSize(CResManager *this, int typeId, int *outWidth, int *outHeight, int *outDepth)
{
	CLocation minLoc, maxLoc;

	CLocation_Init(&minLoc);
	CLocation_Init(&maxLoc);

	if (!CMultiManager_GetExtents(this, typeId, &minLoc, &maxLoc))
		return 0;

	*outWidth = (int16_t)maxLoc.x - (int16_t)minLoc.x;
	*outHeight = (int16_t)maxLoc.y - (int16_t)minLoc.y;
	*outDepth = maxLoc.z - minLoc.z;
	return 1;
}

/*
 * 0x00477C0E - CMultiDef::CalcBounds
 *
 * Adds loc to every component's offset in place. The per-iteration
 * local copy of the offset is dead code in the binary.
 */
static __attribute__((unused)) void
CMultiDef_CalcBounds(CMultiDef *def, CLocation *loc)
{
	CLocation localLoc;
	CMultiComponentDef *iter;

	CLocation_Init(&localLoc);

	iter = (CMultiComponentDef *)def->components.begin;
	while ((char *)iter != (char *)def->components.end) {
		CLocation_SetLoc(&localLoc, &iter->offset);

		CLocation_Add(&iter->offset, loc);

		iter++;
	}
}

/*
 * 0x00477C6E - CMultiDef::SetAllSerial
 *
 * Writes the given serial into every component's invisible field.
 */
static __attribute__((unused)) void
CMultiDef_SetAllSerial(CMultiDef *def, uint32_t serial)
{
	CLocation localLoc;
	CMultiComponentDef *iter;

	CLocation_Init(&localLoc);

	iter = (CMultiComponentDef *)def->components.begin;
	while ((char *)iter != (char *)def->components.end) {
		iter->invisible = (int32_t)serial;

		iter++;
	}
}

/*
 * 0x00477CB9 - CMultiManager::OffsetDef
 *
 * Looks the multi definition for typeId up, shifts every component
 * offset in it by loc, seeks the map file to the type's block and
 * broadcasts the multi info. Returns 0 when typeId is unknown.
 */
static __attribute__((unused)) int
CMultiManager_OffsetDef(CResManager *this, uint32_t typeId, CLocation *loc)
{
	CSearchCtx ctx;
	CMultiDef *def;

	CResManager_FindByKey_A(this, &ctx, &typeId, 1);
	if (!CSearchCtx_Find(&ctx))
		return 0;

	def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);
	CMultiDef_CalcBounds(def, loc);

	MapFileManager_SeekBlock(g_PoolBaseField_C4 + typeId);
	CMulti_SendMultiInfo(this, typeId);
	return 1;
}

/*
 * 0x00477D26 - CMultiManager::SetDefSerial
 *
 * As CMultiManager_OffsetDef but stamps serial onto every component of
 * the definition instead of shifting it.
 */
static __attribute__((unused)) int
CMultiManager_SetDefSerial(CResManager *this, uint32_t typeId, uint32_t serial)
{
	CSearchCtx ctx;
	CMultiDef *def;

	CResManager_FindByKey_A(this, &ctx, &typeId, 1);
	if (!CSearchCtx_Find(&ctx))
		return 0;

	def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);
	CMultiDef_SetAllSerial(def, serial);

	MapFileManager_SeekBlock(g_PoolBaseField_C4 + typeId);
	CMulti_SendMultiInfo(this, typeId);
	return 1;
}

/*
 * 0x00477DB6 - CMultiManager::EraseSlaveRecord
 *
 * Looks key up in the manager's second CResManager and erases the
 * matching key/value pair, handing the value back through outData.
 * Returns 0 when the key is not there.
 */
static __attribute__((unused)) int
CMultiManager_EraseSlaveRecord(CResManager *this, uint32_t key, void **outData)
{
	CSearchCtx ctx, found, erased;

	USED(this);

	CSearchCtx_Constructor(&ctx);
	CSearchCtx_Add(&ctx, CResManager_FindOrInsert_B(&g_MultiManagerSlaves, &found, &key, 1));

	if (!CSearchCtx_Find(&ctx))
		return 0;

	CResManager_EraseMultiB(&g_MultiManagerSlaves, &erased, &ctx, outData, 1);
	return 1;
}

/*
 * 0x00477E1F - CMulti::BroadcastSlaveMove
 *
 * Broadcasts a MOVE packet for the slave's owner to clients near loc,
 * unless the owner is server-only.
 */
static void
CMulti_BroadcastSlaveMove(CMultiSlave *slave, CLocation *loc)
{
	CItem *ownerItem;
	uint8_t buf[0x14];

	ownerItem = (CItem *)slave->base.ownerItem;
	if (CItem_IsServerOnly(ownerItem))
		return;
	PacketManager_MakePacket_MOVE(buf, ownerItem);
	SendPacketInRange(buf, loc, 0x12);
}

/*
 * 0x00477E64 - CMulti::NotifyComponentLoc
 *
 * Broadcasts a MOVE packet for the component's multi owner at loc. The
 * binary's (flags & 2) != 1 check is always true (the getter returns
 * 0 or 2), making this a no-op - our check against the boolean mirrors
 * the binary's effect.
 */
void
CMulti_NotifyComponentLoc(CMultiComponent *mc, CLocation *loc)
{
	CItem *item;

	item = CWorld_FindBySerial(g_World, mc->serial);

	if (CMultiComponent_GetSendSlave(mc) != 1)
		return;
	if (item == NULL)
		return;
	if (!CItem_IsMultiOwner(item))
		return;

	CMulti_BroadcastSlaveMove(CItem_GetMultiSlave(item), loc);
}

/*
 * 0x00477EB9 - CMultiComponent::SendItemUpdate
 *
 * Sends a MOVE packet for the component's owner item to a specific
 * client, unless the item is server-only.
 */
static void
CMultiComponent_SendItemUpdate(CMultiComponent *mc, CItem *player)
{
	CItem *item;
	uint8_t buf[0x14];

	item = mc->ownerItem;
	if (CItem_IsServerOnly(item))
		return;

	PacketManager_MakePacket_MOVE(buf, item);
	SendToClient(player, buf, -1);
}

/*
 * 0x00477EFC - CMulti::SendPlayerInfo
 *
 * Sends a MOVE packet for the component's multi owner to the given
 * player, provided the sendSlave flag is set.
 *
 * FIXED: the binary tests (flags & 2) != 1, which is always true
 * (the expression yields 0 or 2) and makes the whole function a
 * no-op. We test == 0 so the packet is actually sent when the
 * flag is set.
 */
void
CMulti_SendPlayerInfo(CMultiComponent *mc, CItem *player)
{
	CItem *item;

	item = CWorld_FindBySerial(g_World, mc->serial);

	if (CMultiComponent_GetSendSlave(mc) == 0)
		return;

	if (item == NULL)
		return;

	if (!CItem_IsMultiOwner(item))
		return;

	CMultiComponent_SendItemUpdate((CMultiComponent *)CItem_GetMultiSlave(item), player);
}

/*
 * 0x00477F69 - CMultiComponent::SetValid
 *
 * Sets or clears the valid bit (bit 0) of the flags byte.
 */
void
CMultiComponent_SetValid(CMultiComponent *mc, int value)
{
	if (value)
		mc->flags |= 0x01;
	else
		mc->flags &= ~0x01;
}

/*
 * 0x00477FB4 - CMultiComponent::SetSendSlave
 *
 * Sets or clears bit 1 (0x02) of the flags byte.
 */
void
CMultiComponent_SetSendSlave(CMultiComponent *mc, int value)
{
	if (value)
		mc->flags |= 0x02;
	else
		mc->flags &= ~0x02;
}

/*
 * 0x00477FE7 - CMultiManager::GetComponentsNetwork
 *
 * Serializes the multi's components as a big-endian network blob of
 * 12 bytes per entry (bodyType:u16, offset x/y/z:i16, invisible:i32).
 * Returns a malloc'd buffer and its size via *outSize, or NULL/0 if
 * typeId is unknown.
 */
static void *
CMultiManager_GetComponentsNetwork(CResManager *this, int typeId, int *outSize)
{
	CSearchCtx ctx;
	uint32_t key;
	CMultiDef *def;
	int count;
	int entrySize;
	int totalSize;
	uint8_t *buf;
	int i;
	CMultiComponentDef *iter;
	int offset;
	uint16_t tmp16;
	uint32_t tmp32;

	key = (uint32_t)typeId;
	CResManager_FindByKey_A(this, &ctx, &key, 1);
	if (!CSearchCtx_Find(&ctx)) {
		*outSize = 0;
		return NULL;
	}

	def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);
	count = CVector_GetCount1C(&def->components);

	entrySize = 0x0C;
	totalSize = count * entrySize;
	buf = (uint8_t *)malloc(totalSize);

	i = 0;
	def = (CMultiDef *)CResManager_GetResultCtx(this, &ctx);
	iter = (CMultiComponentDef *)def->components.begin;
	while (iter != (CMultiComponentDef *)((CMultiDef *)CResManager_GetResultCtx(this, &ctx))->components.end) {
		offset = 0;

		tmp16 = htons(iter->bodyType);
		memcpy(buf + i * entrySize + offset, &tmp16, 2);
		offset += 2;

		tmp16 = htons(iter->offset.x);
		memcpy(buf + i * entrySize + offset, &tmp16, 2);
		offset += 2;

		tmp16 = htons(iter->offset.y);
		memcpy(buf + i * entrySize + offset, &tmp16, 2);
		offset += 2;

		tmp16 = htons(iter->offset.z);
		memcpy(buf + i * entrySize + offset, &tmp16, 2);
		offset += 2;

		tmp32 = htonl((uint32_t)iter->invisible);
		memcpy(buf + i * entrySize + offset, &tmp32, 4);
		offset += 4;

		i += 1;
		iter++;
	}

	*outSize = totalSize;
	return buf;
}

/*
 * 0x004781D4 - CMulti::SendMultiInfo
 *
 * Serializes the components for serial, packs them into a REVISION
 * packet (or a 1-byte empty REVISION if the type is unknown), and
 * writes it to the map file at g_PoolBaseField_C4 + serial.
 */
static __attribute__((unused)) void
CMulti_SendMultiInfo(CResManager *this, uint32_t serial)
{
	uint8_t buf[0x10018];
	int count = 0;
	char flag = 0;
	void *result;

	result = CMultiManager_GetComponentsNetwork(this, serial, &count);
	if (result == NULL) {
		PacketManager_MakePacket_REVISION(buf, serial, &flag, -1);
	} else {
		PacketManager_MakePacket_REVISION(buf, serial, (char *)result, count);
		free(result);
	}
	MapFileManager_WriteBlock(g_PoolBaseField_C4 + serial, buf, GetPacketOffset(buf) & 0xFFFF);
}

/*
 * 0x00478292 - CMultiSlave::ComputeRange
 *
 * Sets slave->range to the maximum Chebyshev XY distance of any
 * component offset from the origin.
 */
void
CMultiSlave_ComputeRange(CMultiSlave *ms, CMultiDef *def)
{
	CMultiComponentDef *ptr;
	int dist;

	ms->range = 0;

	ptr = (CMultiComponentDef *)def->components.begin;
	while ((char *)ptr != (char *)def->components.end) {
		dist = ChebyshevDistXY(0, 0, ptr->offset.x, ptr->offset.y);

		if (dist > (int)ms->range)
			ms->range = (uint16_t)dist;

		ptr++;
	}
}

/*
 * 0x0047830D - CMultiSlave::MinDistToLocation
 *
 * Returns the minimum 3D distance from any of the multi's components to
 * loc, defaulting to 0xFF when nothing compares. The location-only
 * counterpart of CMultiSlave_MinDistToEntity.
 */
static __attribute__((unused)) int
CMultiSlave_MinDistToLocation(CMultiSlave *slave, CLocation *loc)
{
	int bestDist, dist;
	uintptr_t *iter;
	CItem *comp;

	bestDist = 0xFF;

	for (iter = (uintptr_t *)slave->components.begin; iter != (uintptr_t *)slave->components.end; iter++) {
		comp = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (comp == NULL)
			continue;

		dist = Location_Distance3D((int16_t)comp->resourceEntity.entity.location.x, (int16_t)comp->resourceEntity.entity.location.y, comp->resourceEntity.entity.location.z,
		        (int16_t)loc->x, (int16_t)loc->y, loc->z);
		if (dist < bestDist)
			bestDist = dist;
	}

	return bestDist;
}

/*
 * 0x004783B2 - CMultiSlave::MinDistToEntity
 *
 * Returns the minimum 3D distance from any of the multi's components
 * to target (or any of target's components if target is itself a
 * multi owner). Defaults to 0xFF when nothing compares.
 */
int
CMultiSlave_MinDistToEntity(CMultiSlave *slave, CItem *target)
{
	int bestDist;
	uintptr_t *iter;
	CItem *comp;

	bestDist = 0xFF;

	iter = (uintptr_t *)slave->components.begin;
	while (iter != (uintptr_t *)slave->components.end) {
		comp = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (comp == NULL) {
			iter++;
			continue;
		}

		if (CItem_IsMultiOwner(target)) {
			CMultiSlave *targetSlave;
			uintptr_t *tIter;

			targetSlave = CItem_GetMultiSlave(target);
			tIter = (uintptr_t *)targetSlave->components.begin;
			while (tIter != (uintptr_t *)targetSlave->components.end) {
				CItem *tComp;
				int d;

				tComp = CWorld_FindBySerial(g_World, (uint32_t)*tIter);
				if (tComp == NULL) {
					tIter++;
					continue;
				}

				d = Location_Distance3D((int16_t)comp->resourceEntity.entity.location.x, (int16_t)comp->resourceEntity.entity.location.y,
				        comp->resourceEntity.entity.location.z, (int16_t)tComp->resourceEntity.entity.location.x, (int16_t)tComp->resourceEntity.entity.location.y,
				        tComp->resourceEntity.entity.location.z);
				if (d < bestDist)
					bestDist = d;

				tIter++;
			}
		} else {
			int d;

			d = Location_Distance3D((int16_t)comp->resourceEntity.entity.location.x, (int16_t)comp->resourceEntity.entity.location.y,
			        comp->resourceEntity.entity.location.z, (int16_t)target->resourceEntity.entity.location.x, (int16_t)target->resourceEntity.entity.location.y,
			        target->resourceEntity.entity.location.z);
			if (d < bestDist)
				bestDist = d;
		}
		iter++;
	}

	return bestDist;
}

/*
 * 0x0047850D - TriggerEdit_MultiUpdate
 *
 * Moves a multi to loc (checkFlag=7), falling back to MapSwitchMove
 * if the standard move fails. Returns 0 (not a multi owner), 1 (moved),
 * or 2 (map switch).
 */
int
TriggerEdit_MultiUpdate(CItem *ent, CLocation *loc)
{
	int result;

	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;

	result = CItem_MoveMultiCheck(ent, loc, 7);
	if (result >= 0)
		return 1;

	CMultiSlave_MapSwitchMove(CItem_GetMultiSlave(ent), loc, 7);
	return 2;
}

/*
 * 0x00478560 - CMultiSlave::`scalar deleting destructor'
 *
 * Runs ~CMultiSlave and, if flags&1, deallocates the object.
 */
static void *
CMultiSlave_ScalarDtor(CMultiSlave *ms, int flags)
{
	CMultiSlave_Destructor(ms);
	if (flags & 1)
		free(ms);
	return NULL;
}

/*
 * 0x00478590 - CMultiDef::SetExtents
 *
 * Copies min/max extent CLocations into the def.
 */
static __attribute__((unused)) void *
CMultiDef_SetExtents(CMultiDef *def, CLocation *minLoc, CLocation *maxLoc)
{
	CLocation_SetLoc(&def->minExtent, minLoc);
	CLocation_SetLoc(&def->maxExtent, maxLoc);
	return def;
}

/*
 * 0x004785C0 - CMultiDef::`scalar deleting destructor'
 *
 * Runs ~CMultiDef and, if flags&1, frees the object.
 */
void *
CMultiDef_ScalarDelete(CMultiDef *this, int flags)
{
	CMultiDef_Destructor(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x004785F0 - CMultiDef::SetMinExtent
 *
 * Copies loc into the def's minExtent.
 */
static __attribute__((unused)) void *
CMultiDef_SetMinExtent(CMultiDef *def, CLocation *loc)
{
	CLocation_SetLoc(&def->minExtent, loc);
	return def;
}

// 0x00699B08 - singleton rotation table, filled by CRotationTable_Constructor
CRotationTable g_RotationTable;

/*
 * 0x00478610 - CRotationTable::CRotationTable
 *
 * Fills the 0x40-byte rotation table with 16.16 fixed-point cos/sin
 * values for the 8 UO directions. Called once at static init against
 * the singleton at 0x00699B08.
 */
void
CRotationTable_Constructor(CRotationTable *this)
{
	this->cos[0] = 0x00010000;              // cos  1.0     (N,  dir 0)
	this->cos[1] = 0x0000B504;              // cos  0.7071  (NE, dir 1)
	this->cos[2] = 0x00000000;              // cos  0.0     (E,  dir 2)
	this->cos[3] = (int32_t)0xFFFF4AFC;     // cos -0.7071  (SE, dir 3)
	this->cos[4] = (int32_t)0xFFFF0000;     // cos -1.0     (S,  dir 4)
	this->cos[5] = (int32_t)0xFFFF4AFC;     // cos -0.7071  (SW, dir 5)
	this->cos[6] = 0x00000000;              // cos  0.0     (W,  dir 6)
	this->cos[7] = 0x0000B504;              // cos  0.7071  (NW, dir 7)
	this->sin[0] = 0x00000000;              // sin  0.0     (N,  dir 0)
	this->sin[1] = 0x0000B504;              // sin  0.7071  (NE, dir 1)
	this->sin[2] = 0x00010000;              // sin  1.0     (E,  dir 2)
	this->sin[3] = 0x0000B504;              // sin  0.7071  (SE, dir 3)
	this->sin[4] = 0x00000000;              // sin  0.0     (S,  dir 4)
	this->sin[5] = (int32_t)0xFFFF4AFC;     // sin -0.7071  (SW, dir 5)
	this->sin[6] = (int32_t)0xFFFF0000;     // sin -1.0     (W,  dir 6)
	this->sin[7] = (int32_t)0xFFFF4AFC;     // sin -0.7071  (NW, dir 7)
}

/*
 * 0x004786C0 - CLocation::Add
 *
 * Adds src into dst componentwise.
 */
static void
CLocation_Add(CLocation *dst, CLocation *src)
{
	dst->x += src->x;
	dst->y += src->y;
	dst->z += src->z;
}

/*
 * 0x00478710 - CMultiRotateRect::Init
 *
 * Initializes a rotation rectangle from two reference locations and a
 * rotation value.
 */
CMultiRotateRect *
CMultiRotateRect_Init(CMultiRotateRect *this, CLocation *loc1, CLocation *loc2, uint32_t rotation)
{
	CLocation_SetLoc(&this->loc1, loc1);
	CLocation_SetLoc(&this->loc2, loc2);
	this->rotation = rotation;
	return this;
}

/*
 * 0x00478750 - CMultiRotateRect::SetRotation
 *
 * Generic single-word setter shared with CSerialList helpers.
 */
static CMultiRotateRect *
CMultiRotateRect_SetRotation(CMultiRotateRect *rect, uintptr_t value)
{
	*(uintptr_t *)rect = value;
	return rect;
}

/*
 * 0x00478AB0 - CDeque<CMultiCell16>::FindByHash
 *
 * Appends a 16-byte element to the deque.
 */
static void *
CDeque16_FindByHash(CVector *this, void *element)
{
	return CDeque16_Insert1(this, this->end, element);
}

/*
 * 0x00478BE0 - CDeque<CMultiCell1C>::FindByHash
 *
 * Appends a 0x1C-byte element to the deque.
 */
static void *
CDeque1C_FindByHash(CVector *this, void *element)
{
	return CDeque1C_Insert1(this, this->end, element);
}

/*
 * 0x00478C10 - CMultiDef::CMultiDef
 *
 * Initializes min/max extents and the component vector.
 */
void
CMultiDef_Constructor(CMultiDef *def)
{
	char typeFlag[4] = { 0 };

	CLocation_Init(&def->minExtent);
	CLocation_Init(&def->maxExtent);
	CVector_Constructor(&def->components, typeFlag);
}

/*
 * 0x00478C50 - CMultiComponentPool::Alloc
 *
 * Pops a CMultiComponent from the free list (threaded through the ownerItem
 * field). When empty, allocates a blockSize-sized slab, runs the base ctor
 * on each node, and links nodes 1..N-1 onto the free list.
 */
static void *
CMultiComponentPool_Alloc(void)
{
	CMultiComponent *mc;
	char *block;
	CMultiComponent *nodes;
	int i;

	if (g_multiComponentPool.freeHead != NULL) {
		mc = g_multiComponentPool.freeHead;
		VG_POOL_ALLOC(&g_multiComponentPool, mc, sizeof(CMultiComponent));
		VG_MAKE_DEFINED(mc, sizeof(CMultiComponent));
		g_multiComponentPool.freeHead = (CMultiComponent *)mc->ownerItem;
		return mc;
	}

	g_multiComponentPool.allocated = 1;
	// Custom: 64-bit - sizeof(uintptr_t) header for alignment
	block = (char *)malloc(g_multiComponentPool.blockSize * sizeof(CMultiComponent) + sizeof(uintptr_t));
	if (block != NULL) {
		*(uint32_t *)block = g_multiComponentPool.blockSize;
		nodes = (CMultiComponent *)(block + sizeof(uintptr_t));
		for (i = 0; i < (int)g_multiComponentPool.blockSize; i++)
			CMultiComponent_Constructor_base(&nodes[i]);
	} else {
		nodes = NULL;
	}

	g_multiComponentPool.allocated = 0;

	// Link nodes 1..N-1 into free list via ownerItem at +0x10
	for (i = (int)g_multiComponentPool.blockSize - 1; i >= 1; i--) {
		nodes[i].ownerItem = (CItem *)g_multiComponentPool.freeHead;
		g_multiComponentPool.freeHead = &nodes[i];
	}

	VG_POOL_ALLOC(&g_multiComponentPool, &nodes[0], sizeof(CMultiComponent));
	VG_MAKE_DEFINED(&nodes[0], sizeof(CMultiComponent));
	return &nodes[0];
}

/*
 * 0x00478D70 - CMultiDef::~CMultiDef
 *
 * Tears down the components vector.
 */
static void
CMultiDef_Destructor(CMultiDef *def)
{
	CVector_ClearAndFree1C(&def->components);
}

/*
 * 0x00478D90 - CMultiComponent::SetOffset
 *
 * Copies loc into the component's offset field.
 */
void
CMultiComponent_SetOffset(CMultiComponent *mc, CLocation *loc)
{
	CLocation_SetLoc(&mc->offset, loc);
}

/*
 * 0x00478DB0 - CMultiComponentPool::Return
 *
 * Returns a component to the pool by pushing it onto the free list
 * (threaded via the ownerItem field).
 */
static void
CMultiComponentPool_Return(CMultiComponent *mc)
{
	mc->ownerItem = (CItem *)g_multiComponentPool.freeHead;
	g_multiComponentPool.freeHead = mc;
	VG_POOL_FREE(&g_multiComponentPool, mc);
}

/*
 * 0x00478DE0 - CMultiSlave::SetTypeId
 *
 * Stores typeId on the slave.
 */
void
CMultiSlave_SetTypeId(CMultiSlave *ms, int32_t typeId)
{
	ms->typeId = typeId;
}

/*
 * 0x00478E00 - MultiExtentTracker::MultiExtentTracker
 *
 * Initializes the extent locations and resets all per-axis flags.
 */
void
MultiExtentTracker_Constructor(MultiExtentTracker *t)
{
	CLocation_Init(&t->minExtent);
	CLocation_Init(&t->maxExtent);
	MultiExtentTracker_Reset(t);
}

/*
 * 0x00478E30 - MultiExtentTracker::Reset
 *
 * Clears the six per-axis flags and resets min/max to (-1,-1,0).
 */
void
MultiExtentTracker_Reset(MultiExtentTracker *t)
{
	t->minXSet = 0;
	t->minYSet = 0;
	t->minZSet = 0;
	t->maxXSet = 0;
	t->maxYSet = 0;
	t->maxZSet = 0;
	CLocation_Set(&t->minExtent, (int16_t)0xFFFF, (int16_t)0xFFFF, (int16_t)0xFF00);
	CLocation_Set(&t->maxExtent, (int16_t)0xFFFF, (int16_t)0xFFFF, (int16_t)0xFF00);
}

/*
 * 0x00478EA0 - MultiExtentTracker::Update
 *
 * Extends the six running extents with the component's offset, lazy-
 * initializing each axis on first call.
 */
void
MultiExtentTracker_Update(MultiExtentTracker *t, CMultiComponentDef *comp)
{
	// Min X
	if (!t->minXSet || (int16_t)t->minExtent.x > (int16_t)comp->offset.x) {
		t->minExtent.x = comp->offset.x;
		t->minXSet = 1;
	}

	// Min Y
	if (!t->minYSet || (int16_t)t->minExtent.y > (int16_t)comp->offset.y) {
		t->minExtent.y = comp->offset.y;
		t->minYSet = 1;
	}

	// Min Z
	if (!t->minZSet || (int16_t)t->minExtent.z > (int16_t)comp->offset.z) {
		t->minExtent.z = comp->offset.z;
		t->minZSet = 1;
	}

	// Max X
	if (!t->maxXSet || (int16_t)t->maxExtent.x < (int16_t)comp->offset.x) {
		t->maxExtent.x = comp->offset.x;
		t->maxXSet = 1;
	}

	// Max Y
	if (!t->maxYSet || (int16_t)t->maxExtent.y < (int16_t)comp->offset.y) {
		t->maxExtent.y = comp->offset.y;
		t->maxYSet = 1;
	}

	// Max Z
	if (!t->maxZSet || (int16_t)t->maxExtent.z < (int16_t)comp->offset.z) {
		t->maxExtent.z = comp->offset.z;
		t->maxZSet = 1;
	}
}

int
IntLessThan(void *a, void *b, int cmp)
{
	USED(cmp);
	return *(int *)a < *(int *)b;
}

static void RelocateItem_Single(CLocationPair *this, CItem *item);

/*
 * 0x00479120 - CMultiComponent::`scalar/vector deleting destructor'
 *
 * If flags&2, runs the per-element dtor across the preceding-count array
 * and frees the slab. Otherwise runs the scalar dtor and (when flags&1)
 * frees the single object.
 *
 * MODIFIED: the MSVC __vec_Destructor helper is inlined as a manual reverse
 * loop; OperatorDelete is replaced with free().
 */
static void *
CMultiComponent_ScalarDtor(CMultiComponent *mc, int flags)
{
	if (flags & 2) {
		// Custom: 64-bit - sizeof(uintptr_t) header for alignment
		uint32_t count = *(uint32_t *)((char *)mc - sizeof(uintptr_t));
		int i;
		for (i = (int)count - 1; i >= 0; i--) {
			CMultiComponent_Destructor_base((CMultiComponent *)((uint8_t *)mc + i * sizeof(CMultiComponent)));
		}
		free((char *)mc - sizeof(uintptr_t));
	} else {
		CMultiComponent_Destructor_base(mc);
		if (flags & 1)
			free(mc);
	}
	return NULL;
}

/*
 * 0x00479400 - CDeque16::Insert1
 *
 * Inserts a single 16-byte element at pos via CDeque16_Insert.
 */
static void *
CDeque16_Insert1(CVector *this, void *pos, void *element)
{
	int idx;
	idx = ((char *)pos - (char *)CSearchCtx_GetBucket((CSearchCtx *)this)) / sizeof(CString);
	CDeque16_Insert(this, pos, 1, element);
	return (char *)CSearchCtx_GetBucket((CSearchCtx *)this) + (idx * sizeof(CString));
}

/*
 * 0x00479480 - CVector::Uninit_Copy16_Fwd2
 *
 * Forward-copies 16-byte elements from [first, last) to dest.
 */
void *
CVector_Uninit_Copy16_Fwd2(CVector *this, void *first, void *last, void *dest)
{
	char *src = (char *)first;
	char *end = (char *)last;
	char *dst = (char *)dest;

	while (src != end) {
		CopyFrom16(this, dst, src);
		dst += sizeof(CString);
		src += sizeof(CString);
	}
	return dst;
}

/*
 * 0x004794C0 - CDeque1C::Insert1
 *
 * Inserts a single 0x1C-byte element at pos via CDeque1C_Insert.
 */
static void *
CDeque1C_Insert1(CVector *this, void *pos, void *element)
{
	int idx;
	idx = ((char *)pos - (char *)CSearchCtx_GetBucket((CSearchCtx *)this)) / sizeof(CMultiComponentDef);
	CDeque1C_Insert(this, pos, 1, element);
	return (char *)CSearchCtx_GetBucket((CSearchCtx *)this) + idx * sizeof(CMultiComponentDef);
}

/*
 * 0x004795E0 - CDeque6::InsertAtEnd
 *
 * Appends a single CLocation (6-byte) element to the deque.
 */
void *
CDeque6_InsertAtEnd(CVector *this, void *endPos, void *element)
{
	int idx;
	idx = ((char *)endPos - (char *)CSearchCtx_GetBucket((CSearchCtx *)this)) / sizeof(CLocation);
	CDeque6_Insert(this, endPos, 1, element);
	return (char *)CSearchCtx_GetBucket((CSearchCtx *)this) + idx * sizeof(CLocation);
}

/*
 * 0x00479660 - CDeque16::Insert
 *
 * MSVC STL deque insert for 16-byte elements with the three standard
 * branches (grow, shift-right, copy-backward).
 */
static void
CDeque16_Insert(CVector *this, void *pos, uint32_t count, void *element)
{
	uint32_t cap, curCount, newTotal;
	void *newBuf, *mid;

	cap = ((char *)this->capacity - (char *)this->end) / sizeof(CString);
	if (cap < count) {
		curCount = CVector_GetCount16(this);
		newTotal = (count >= curCount) ? count : curCount;
		newTotal += curCount;
		newBuf = CVector_Allocate16(NULL, newTotal);
		mid = CVector_Uninit_Copy16_Fwd2(this, this->begin, pos, newBuf);
		Uninit_FillN_16(this, mid, count, element);
		mid = (char *)mid + (count * sizeof(CString));
		CVector_Uninit_Copy16_Fwd2(this, pos, this->end, mid);
		Destroy16_Range(this, this->begin, this->end);
		CVector_ClearFreeRaw(this->begin, ((char *)this->capacity - (char *)this->begin) / sizeof(CString));
		this->capacity = (char *)newBuf + (newTotal * sizeof(CString));
		this->end = (char *)newBuf + (CVector_GetCount16(this) * sizeof(CString)) + (count * sizeof(CString));
		this->begin = newBuf;
		return;
	}

	if ((int32_t)(((char *)this->end - (char *)pos) / sizeof(CString)) < (int32_t)count) {
		CVector_Uninit_Copy16_Fwd2(this, pos, this->end, (char *)pos + (count * sizeof(CString)));
		Uninit_FillN_16(this, this->end, count - (int32_t)(((char *)this->end - (char *)pos) / sizeof(CString)), element);
		FillCopy16_Fwd(pos, this->end, element);
		this->end = (char *)this->end + (count * sizeof(CString));
		return;
	}

	if (count <= 0)
		return;

	CVector_Uninit_Copy16_Fwd2(this, (char *)this->end - (count * sizeof(CString)), this->end, this->end);
	CopyBackward16(pos, (char *)this->end - (count * sizeof(CString)), this->end);
	FillCopy16_Fwd(pos, (char *)pos + (count * sizeof(CString)), element);
	this->end = (char *)this->end + (count * sizeof(CString));
}

/*
 * 0x00479890 - CopyFrom16
 *
 * Copies a single 16-byte element (CString) via CopyFrom16_Inner.
 */
static void
CopyFrom16(CVector *this, void *src, void *dst)
{
	USED(this);
	CopyFrom16_Inner(src, dst);
}

/*
 * 0x004798B0 - CVector::Destroy16_Single
 *
 * Destroys a single 16-byte element.
 */
void
CVector_Destroy16_Single(CVector *this, void *element)
{
	USED(this);
	Destroy16_Inner(element);
}

/*
 * 0x004798D0 - CDeque1C::Insert
 *
 * MSVC STL deque insert for 0x1C-byte elements with the three standard
 * branches (grow, shift-right, copy-backward).
 */
static void
CDeque1C_Insert(CVector *this, void *pos, uint32_t count, void *element)
{
	uint32_t cap, curCount, newTotal;
	void *newBuf, *mid;

	cap = ((char *)this->capacity - (char *)this->end);
	cap = (int32_t)cap / sizeof(CMultiComponentDef);
	if ((uint32_t)cap < count) {
		curCount = CVector_GetCount1C(this);
		newTotal = (count >= curCount) ? count : curCount;
		newTotal += curCount;
		newBuf = Allocate1C(newTotal);
		mid = CVector_Uninit_Copy1C_Fwd2(this, this->begin, pos, newBuf);
		Uninit_FillN_1C(this, mid, count, element);
		mid = (char *)mid + count * sizeof(CMultiComponentDef);
		CVector_Uninit_Copy1C_Fwd2(this, pos, this->end, mid);
		Destroy1C_Range(this, this->begin, this->end);
		CVector_ClearFreeRaw(this->begin, (int32_t)((char *)this->capacity - (char *)this->begin) / sizeof(CMultiComponentDef));
		this->capacity = (char *)newBuf + newTotal * sizeof(CMultiComponentDef);
		this->end = (char *)newBuf + CVector_GetCount1C(this) * sizeof(CMultiComponentDef) + count * sizeof(CMultiComponentDef);
		this->begin = newBuf;
		return;
	}

	if ((int32_t)(((char *)this->end - (char *)pos) / (int)sizeof(CMultiComponentDef)) < (int32_t)count) {
		CVector_Uninit_Copy1C_Fwd2(this, pos, this->end, (char *)pos + count * sizeof(CMultiComponentDef));
		Uninit_FillN_1C(this, this->end, count - (int32_t)((char *)this->end - (char *)pos) / sizeof(CMultiComponentDef), element);
		FillCopy1C_Fwd(pos, this->end, element);
		this->end = (char *)this->end + count * sizeof(CMultiComponentDef);
		return;
	}

	if (count <= 0)
		return;

	CVector_Uninit_Copy1C_Fwd2(this, (char *)this->end - count * sizeof(CMultiComponentDef), this->end, this->end);
	CopyBackward1C(pos, (char *)this->end - count * sizeof(CMultiComponentDef), this->end);
	FillCopy1C_Fwd(pos, (char *)pos + count * sizeof(CMultiComponentDef), element);
	this->end = (char *)this->end + count * sizeof(CMultiComponentDef);
}

/*
 * 0x00479B10 - CVector::Uninit_Copy1C_Fwd2
 *
 * Forward-copies 0x1C-byte elements from [first, last) to dest.
 */
static void *
CVector_Uninit_Copy1C_Fwd2(CVector *this, void *first, void *last, void *dest)
{
	CMultiComponentDef *src = (CMultiComponentDef *)first;
	CMultiComponentDef *end = (CMultiComponentDef *)last;
	CMultiComponentDef *dst = (CMultiComponentDef *)dest;

	while (src != end) {
		CopyFrom1C(this, dst, src);
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x00479B70 - CopyFrom1C
 *
 * Copies a single 0x1C-byte element (CMultiComponentDef) via
 * CopyFrom1C_Inner.
 */
static void
CopyFrom1C(CVector *this, void *src, void *dst)
{
	USED(this);
	CopyFrom1C_Inner(src, dst);
}

/*
 * 0x00479B90 - CVector::Destroy1C_Single
 *
 * Destroys a single 0x1C-byte element.
 */
void
CVector_Destroy1C_Single(CVector *this, void *element)
{
	USED(this);
	Destroy1C_Inner(element);
}

/*
 * 0x00479FF0 - CVector::Destroy6_Single
 *
 * Calls SwapEndian on the single element pointer.
 */
void
CVector_Destroy6_Single(CVector *this, void *element)
{
	USED(this);
	SwapEndian(element);
}

/*
 * 0x0047A010 - std::deque::_Insert for 6-byte elements
 *
 * Inserts count CLocation copies of element at pos, reallocating
 * when capacity is exhausted.
 */
static void
CDeque6_Insert(void *deque, void *pos, int count, void *element)
{
	CVector *this = (CVector *)deque;
	int avail, suffix, minCount, totalCount, oldCount;
	void *newBuf, *mid;

	avail = ((char *)this->capacity - (char *)this->end) / sizeof(CLocation);
	if (avail < count) {
		int curCount = CVector_GetCount6(this);
		if (count < curCount)
			minCount = curCount;
		else
			minCount = count;
		totalCount = CVector_GetCount6(this) + minCount;
		newBuf = CVector_Allocate6(this, totalCount);
		mid = Uninit_Copy6_Fwd(this, this->begin, pos, newBuf);
		Uninit_FillN_6(this, mid, count, element);
		Uninit_Copy6_Fwd(this, pos, this->end, (char *)mid + count * sizeof(CLocation));
		Destroy6_Range(this, this->begin, this->end);
		oldCount = ((char *)this->capacity - (char *)this->begin) / sizeof(CLocation);
		StdDeque_DeallocSI(this, this->begin, oldCount);
		this->capacity = (char *)newBuf + totalCount * sizeof(CLocation);
		this->end = (char *)newBuf + CVector_GetCount6(this) * sizeof(CLocation) + count * sizeof(CLocation);
		this->begin = newBuf;
	} else {
		suffix = ((char *)this->end - (char *)pos) / sizeof(CLocation);
		if (suffix < count) {
			Uninit_Copy6_Fwd(this, pos, this->end, (char *)pos + count * sizeof(CLocation));
			Uninit_FillN_6(this, this->end, count - suffix, element);
			FillCopy6_Fwd(pos, this->end, element);
			this->end = (char *)this->end + count * sizeof(CLocation);
		} else if (count > 0) {
			Uninit_Copy6_Fwd(this, (char *)this->end - count * sizeof(CLocation), this->end, this->end);
			CopyBackward6(pos, (char *)this->end - count * sizeof(CLocation), this->end);
			FillCopy6_Fwd(pos, (char *)pos + count * sizeof(CLocation), element);
			this->end = (char *)this->end + count * sizeof(CLocation);
		}
	}
}

/*
 * 0x0047A250 - std::_Uninit_fill_n for 16-byte elements
 *
 * Fills count CString slots at ptr with a copy of source.
 */
static void
Uninit_FillN_16(CVector *this, void *ptr, uint32_t count, void *source)
{
	char *p = (char *)ptr;
	while (count > 0) {
		CopyFrom16(this, p, source);
		count--;
		p += sizeof(CString);
	}
}

/*
 * 0x0047A290 - std::_Uninit_fill_n for 28-byte elements
 *
 * Fills count CMultiComponentDef slots at ptr with a copy of source.
 */
static void
Uninit_FillN_1C(CVector *this, void *ptr, uint32_t count, void *source)
{
	CMultiComponentDef *p = (CMultiComponentDef *)ptr;
	USED(this);
	while (count > 0) {
		CopyFrom1C_Inner(p, source);
		count--;
		p++;
	}
}

/*
 * 0x0047A2D0 - std::_Uninit_copy for 4-byte elements (forward)
 *
 * Copies 4-byte elements from [first, last) to dest; returns end.
 */
static __attribute__((unused)) void *
Uninit_Copy4_Fwd(CVector *this, void *first, void *last, void *dest)
{
	uint32_t *src = (uint32_t *)first;
	uint32_t *end = (uint32_t *)last;
	uint32_t *dst = (uint32_t *)dest;
	USED(this);
	while (src != end) {
		*dst = *src;
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x0047A310 - std::_Uninit_fill_n for 4-byte elements
 *
 * Fills count 4-byte slots at ptr with the value at source.
 */
static __attribute__((unused)) void
Uninit_FillN_4(CVector *this, void *ptr, uint32_t count, void *source)
{
	uint32_t *p = (uint32_t *)ptr;
	uint32_t val = *(uint32_t *)source;
	USED(this);
	while (count > 0) {
		*p = val;
		count--;
		p++;
	}
}

/*
 * 0x0047A370 - std::_Uninit_copy for 4-byte elements (forward, variant 2)
 *
 * Second instantiation of Uninit_Copy4_Fwd; same body.
 */
void *
Uninit_Copy4_Fwd2(CVector *this, void *first, void *last, void *dest)
{
	uint32_t *src = (uint32_t *)first;
	uint32_t *end = (uint32_t *)last;
	uint32_t *dst = (uint32_t *)dest;
	USED(this);
	while (src != end) {
		*dst = *src;
		dst++;
		src++;
	}
	return dst;
}

/*
 * 0x0047A3B0 - std::_Uninit_fill_n for 4-byte elements (variant 2)
 *
 * Second instantiation of Uninit_FillN_4; identical body.
 */
static __attribute__((unused)) void
Uninit_FillN_4v2(CVector *this, void *ptr, uint32_t count, void *source)
{
	uint32_t *p = (uint32_t *)ptr;
	uint32_t val = *(uint32_t *)source;
	USED(this);
	while (count > 0) {
		*p = val;
		count--;
		p++;
	}
}

/*
 * 0x0047A430 - std::_Uninit_copy for 6-byte elements (forward)
 *
 * Copies CLocations from [first, last) to dest; returns end.
 */
static void *
Uninit_Copy6_Fwd(CVector *this, void *first, void *last, void *dest)
{
	char *src = (char *)first;
	char *end = (char *)last;
	char *dst = (char *)dest;
	USED(this);
	while (src != end) {
		CLocation_SetLoc((CLocation *)dst, (CLocation *)src);
		dst += 6;
		src += 6;
	}
	return dst;
}

/*
 * 0x0047A470 - std::_Uninit_fill_n for 6-byte elements
 *
 * Fills count CLocation slots at ptr with a copy of source.
 */
static void
Uninit_FillN_6(CVector *this, void *ptr, uint32_t count, void *source)
{
	char *p = (char *)ptr;
	USED(this);
	while (count > 0) {
		CLocation_SetLoc((CLocation *)p, (CLocation *)source);
		count--;
		p += sizeof(CLocation);
	}
}

/*
 * 0x0047A4D0 - CLocation::CopySingle
 *
 * Copies a single CLocation via CLocation_SetLoc.
 */
static __attribute__((unused)) void
CLocation_CopySingle(CVector *this, CLocation *dest, CLocation *source)
{
	USED(this);
	CLocation_SetLoc(dest, source);
}

/*
 * 0x0047ACF0 - HideItemsInVector_Raw
 *
 * Calls VT_HIDE on every CItem in [first, last).
 */
static void
HideItemsInVector_Raw(uintptr_t *first, uintptr_t *last, uint8_t dummy)
{
	USED(dummy);
	while (first != last) {
		CItem *item = (CItem *)*first;
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		first++;
	}
}

/*
 * 0x0047AD20 - VT_HIDE caller
 *
 * Calls VT_HIDE on a single item.
 */
static __attribute__((unused)) void
VT_HIDE_Single(StdAllocator *this, CItem *item)
{
	USED(this);
	((void (*)(CItem *))VT_FN(item, VT_HIDE))(item);
}

/*
 * 0x0047AD40 - RelocateItems_Raw
 *
 * Relocates every CItem in [first, last) relative to newLoc.
 */
static __attribute__((unused)) void *
RelocateItems_Raw(void *output, uintptr_t *first, uintptr_t *last, CLocation newLoc)
{
	while (first != last) {
		RelocateItem_Single((CLocationPair *)&newLoc, (CItem *)*first);
		first++;
	}
	CLocationPair_CopyAssign((CLocationPair *)output, (CLocationPair *)&newLoc);
	return output;
}

/*
 * 0x0047AD80 - RelocateItem_Single
 *
 * Shifts one item by (newLoc - oldLoc) and drops it via VT_DROP_AT_FEET.
 */
static void
RelocateItem_Single(CLocationPair *this, CItem *item)
{
	CLocation localLoc;
	CLocation delta;

	CLocation_SetLoc(&localLoc, &item->resourceEntity.entity.location);
	CLocation_ComputeDelta(&localLoc, &delta, &this->loc1);
	CLocation_SetLoc(&localLoc, &delta);
	CLocation_Add(&localLoc, &this->loc2);
	((void (*)(CItem *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, &localLoc);
}

/*
 * 0x0047ADE0 - RestoreItems_Raw
 *
 * Drops every CItem in [first, last) at its own location.
 */
static void
RestoreItems_Raw(uintptr_t *first, uintptr_t *last, uint8_t dummy)
{
	USED(dummy);
	while (first != last) {
		RestoreItem_Single((StdAllocator *)&dummy, (CItem *)*first);
		first++;
	}
}

/*
 * 0x0047AE10 - RestoreItem_Single
 *
 * Drops one item at its current entity location via VT_DROP_AT_FEET.
 */
static void
RestoreItem_Single(StdAllocator *this, CItem *item)
{
	USED(this);
	CLocation *loc = CEntity_GetLocation(&item->resourceEntity.entity);
	((void (*)(CItem *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
}

/*
 * 0x0047AE40 - SaveMulti_SortInt
 *
 * Sorts a range of ints via SortInt_Dispatch.
 */
static void
SaveMulti_SortInt(void *first, void *last, uint8_t cmpVal)
{
	int size = GameCentMon_GetPlayerCount();
	SortInt_Dispatch(first, last, cmpVal, size);
}

/*
 * 0x0047AE70 - CollectEntities_SortDist
 *
 * Sorts a range of entities by distance to cmpLoc.
 */
static __attribute__((unused)) void
CollectEntities_SortDist(void *first, void *last, CLocation cmpLoc)
{
	int size = GameCentMon_GetPlayerCount();
	USED(size);
	CLocation tmp;
	CLocation_SetLoc(&tmp, &cmpLoc);
	SortDist_Dispatch(first, last, tmp);
}

/*
 * 0x0047AEB0 - AttachScriptsFromVector
 *
 * Attaches every script name in [first, last) to entity.
 */
static void *
AttachScriptsFromVector(void *output, char *first, char *last, uintptr_t entity)
{
	while (first != last) {
		AttachScript_Single(&entity, (CString *)first);
		first += sizeof(CString);
	}
	*(uintptr_t *)output = entity;
	return output;
}

/*
 * 0x0047AEF0 - AttachScript_Single
 *
 * Attaches one script (by name) to the current entity with fireCreation=1.
 */
static void
AttachScript_Single(uintptr_t *this, CString *element)
{
	const char *buf;

	buf = CString_GetBuffer(element);
	Entity_AttachScript((CItem *)*this, buf, 1);
}

/*
 * 0x0047AF20 - RotateAndRelocateItems
 *
 * Iterates a CVector of CItem pointers. For each, calls
 * RotateAndRelocateItem_Single to rotate and relocate. Copies result to output
 * via CLocationPair_CopyFromSrc.
 */
static void *
RotateAndRelocateItems(void *output, uintptr_t *first, uintptr_t *last, CLocationPair *params)
{
	while (first != last) {
		RotateAndRelocateItem_Single(params, (CItem *)*first);
		first++;
	}
	CLocationPair_CopyFromSrc((CLocationPair *)output, params);
	return output;
}

/*
 * 0x0047AF60 - RotateAndRelocateItem_Single
 *
 * Thiscall on params struct. For an item, copies its location,
 * computes delta from base, applies rotation via RotateLocation,
 * adds offset, and writes back to item location.
 */
static void
RotateAndRelocateItem_Single(CLocationPair *this, CItem *item)
{
	CLocation localLoc;
	CLocation delta;
	int16_t rotX, rotY;

	CLocation_SetLoc(&localLoc, &item->resourceEntity.entity.location);
	CLocation_ComputeDelta(&localLoc, &delta, &this->loc1);
	CLocation_SetLoc(&localLoc, &delta);
	rotX = localLoc.x;
	rotY = localLoc.y;
	RotateLocation(&g_RotationTable, &rotX, &rotY, this->rotation);
	localLoc.x = rotX;
	localLoc.y = rotY;
	CLocation_Add(&localLoc, &this->loc2);
	CLocation_SetLoc(&item->resourceEntity.entity.location, &localLoc);
}

/*
 * 0x0047AFD0 - CRotationTable::RotateLocation
 *
 * Thiscall on a CRotationTable. Rotates (x, y) in place by the given
 * rotation index (0-7) using the fixed-point 16.16 cos/sin values
 * this->cos[rotation] and this->sin[rotation].
 */
static void
RotateLocation(CRotationTable *this, int16_t *x, int16_t *y, int rotation)
{
	int32_t ix, iy;
	int32_t cosVal, sinVal;
	int32_t newX, newY;

	ix = (int32_t)*x;
	iy = (int32_t)*y;
	sinVal = this->sin[rotation];
	cosVal = this->cos[rotation];

	newX = (ix * cosVal - iy * sinVal) >> 16;
	*x = (int16_t)newX;

	newY = (ix * sinVal + iy * cosVal) >> 16;
	*y = (int16_t)newY;
}

/*
 * 0x0047B040 - SetLocationsFromVector
 *
 * Assigns consecutive CLocations to every item in [first, last).
 */
static void *
SetLocationsFromVector(void *output, uintptr_t *first, uintptr_t *last, uintptr_t data)
{
	while (first != last) {
		SetLocation_Single(&data, (CItem *)*first);
		first++;
	}
	*(uintptr_t *)output = data;
	return output;
}

/*
 * 0x0047B080 - SetLocation_Single
 *
 * Stores a CLocation into one item and advances the source cursor.
 */
static void
SetLocation_Single(uintptr_t *this, CItem *item)
{
	CLocation_SetLoc(&item->resourceEntity.entity.location, (CLocation *)*this);
	*this = *this + sizeof(CLocation);
}

/*
 * 0x0047B0B0 - SetDirectionsFromVector
 *
 * Applies a rotation delta to every mobile in [first, last).
 */
static void *
SetDirectionsFromVector(void *output, uintptr_t *first, uintptr_t *last, uintptr_t data)
{
	while (first != last) {
		ComputeAndSetDirection(&data, (CItem *)*first);
		first++;
	}
	*(uintptr_t *)output = data;
	return output;
}

/*
 * 0x0047B0F0 - ComputeAndSetDirection
 *
 * Rotates a mobile's facing by *this and clamps modulo 8 (sign-preserving).
 */
static void
ComputeAndSetDirection(uintptr_t *this, CItem *entity)
{
	int facing;
	int rotated;

	if (!((int (*)(CItem *))VT_FN(entity, VT_IS_MOBILE))(entity))
		return;

	facing = ((int (*)(CItem *))VT_FN(entity, VT_GET_DIRECTION))(entity);
	rotated = facing + (int)*this;
	// Signed modulo 8: (abs(rotated) & 7) * sign
	if (rotated < 0)
		rotated = -((-rotated) & 7);
	else
		rotated = rotated & 7;
	CMobile_SetDirection(entity, rotated);
}

/*
 * 0x0047B160 - Uninit_Copy16_Fwd
 *
 * Copies CStrings from [first, last) to dest; returns end.
 */
void *
Uninit_Copy16_Fwd(void *first, void *last, void *dest)
{
	while (first != last) {
		CString_Assign((CString *)dest, (CString *)first);
		dest = (char *)dest + sizeof(CString);
		first = (char *)first + sizeof(CString);
	}
	return dest;
}

/*
 * 0x0047B1A0 - Allocate16_Inner
 *
 * Allocates count CString slots; clamps negative to 0.
 */
void *
Allocate16_Inner(int count)
{
	if (count < 0)
		count = 0;
	return malloc(count * sizeof(CString));
}

/*
 * 0x0047B1D0 - Allocate1C
 *
 * Allocates count CMultiComponentDef slots; clamps negative to 0.
 */
void *
Allocate1C(int count)
{
	if (count < 0)
		count = 0;
	return malloc((uint32_t)count * sizeof(CMultiComponentDef));
}

/*
 * 0x0047B200 - FillCopy16_Fwd
 *
 * Assigns element to every CString slot in [first, last).
 */
static void
FillCopy16_Fwd(void *first, void *last, void *element)
{
	char *p = (char *)first;

	while (p != (char *)last) {
		CString_Assign((CString *)p, (CString *)element);
		p += sizeof(CString);
	}
}

/*
 * 0x0047B230 - CopyBackward16
 *
 * Copies CStrings from [first, last) backward into dest.
 */
static void *
CopyBackward16(void *first, void *last, void *dest)
{
	char *src = (char *)last;
	char *dst = (char *)dest;

	while ((char *)first != src) {
		src -= sizeof(CString);
		dst -= sizeof(CString);
		CString_Assign((CString *)dst, (CString *)src);
	}
	return dst;
}

/*
 * 0x0047B260 - CopyFrom16_Inner
 *
 * Placement copy-construct of one CString from src into dst.
 */
static void
CopyFrom16_Inner(void *src, void *dst)
{
	void *ptr = (void *)StdKfn_Identity(sizeof(CString), (uintptr_t)src);
	if (ptr != NULL)
		CString_CopyConstructor((CString *)ptr, (CString *)dst);
}

/*
 * 0x0047B2A0 - Destroy16_Inner
 *
 * Destructs one CString slot without freeing it.
 */
static void
Destroy16_Inner(void *element)
{
	CString_ScalarDelete((CString *)element, 0);
}

/*
 * 0x0047B2B0 - FillCopy1C_Fwd
 *
 * Assigns element to every CMultiComponentDef slot in [first, last).
 */
static void
FillCopy1C_Fwd(void *first, void *last, void *element)
{
	char *p = (char *)first;

	while (p != (char *)last) {
		CMultiComponent_CopyFrom((CMultiComponentDef *)p, element);
		p += sizeof(CMultiComponentDef);
	}
}

/*
 * 0x0047B2E0 - CopyBackward1C
 *
 * Copies CMultiComponentDefs from [first, last) backward into dest.
 */
static void *
CopyBackward1C(void *first, void *last, void *dest)
{
	char *src = (char *)last;
	char *dst = (char *)dest;

	while ((char *)first != src) {
		src -= sizeof(CMultiComponentDef);
		dst -= sizeof(CMultiComponentDef);
		CMultiComponent_CopyFrom((CMultiComponentDef *)dst, (CMultiComponentDef *)src);
	}
	return dst;
}

/*
 * 0x0047B310 - CopyFrom1C_Inner
 *
 * Placement copy-construct of one CMultiComponentDef from src into dst.
 */
static void
CopyFrom1C_Inner(void *src, void *dst)
{
	void *ptr = (void *)StdKfn_Identity(sizeof(CMultiComponentDef), (uintptr_t)src);
	if (ptr != NULL)
		CMultiComponent_CopyConstructor((CMultiComponentDef *)ptr, (CMultiComponentDef *)dst);
}

/*
 * 0x0047B350 - Destroy1C_Inner
 *
 * Destructs one CMultiComponentDef slot without freeing it.
 */
static void
Destroy1C_Inner(void *element)
{
	CMultiComponent_ScalarDelete(element, 0);
}

/*
 * 0x0047B360 - FillCopy6_Fwd
 *
 * Assigns element to every CLocation slot in [first, last).
 */
static void
FillCopy6_Fwd(void *first, void *last, void *element)
{
	char *p = (char *)first;

	while (p != (char *)last) {
		CLocation_SetLoc((CLocation *)p, (CLocation *)element);
		p += sizeof(CLocation);
	}
}

/*
 * 0x0047B390 - CopyBackward6
 *
 * Copies CLocations from [first, last) backward into dest.
 */
static void *
CopyBackward6(void *first, void *last, void *dest)
{
	char *src = (char *)last;
	char *dst = (char *)dest;

	while ((char *)first != src) {
		src -= 6;
		dst -= 6;
		CLocation_SetLoc((CLocation *)dst, (CLocation *)src);
	}
	return dst;
}

/*
 * 0x0047B3C0 - Allocate6_Inner
 *
 * Allocates count CLocation slots; clamps negative to 0.
 */
static __attribute__((unused)) void *
Allocate6_Inner(int count)
{
	if (count < 0)
		count = 0;
	return malloc(count * sizeof(CLocation));
}

/*
 * 0x0047B3F0 - CopySingle6_Inner
 *
 * Placement copy of one CLocation from source into dest.
 */
static __attribute__((unused)) void
CopySingle6_Inner(void *dest, void *source)
{
	void *valid;

	valid = (void *)StdKfn_Identity(6, (uintptr_t)dest);
	if (valid != NULL)
		CLocation_SetLoc(valid, source);
}

/*
 * 0x0047B430 - CResList scalar deleting destructor (multi key variant A)
 *
 * Calls CResList_Destructor_MultiA and optionally frees.
 */
__attribute__((unused)) void *
CResList_ScalarDelete_MultiA(CResList *this, int flags)
{
	CResList_Destructor_MultiA(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047B460 - CResList scalar deleting destructor (multi key variant B)
 *
 * Calls CResList_Destructor_MultiB and optionally frees.
 */
__attribute__((unused)) void *
CResList_ScalarDelete_MultiB(CResList *this, int flags)
{
	CResList_Destructor_MultiB(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047B490 - CResList scalar deleting destructor (multi value variant)
 *
 * Calls CResList_Destructor_MultiVal and optionally frees.
 */
__attribute__((unused)) void *
CResList_ScalarDelete_MultiVal(CResList *this, int flags)
{
	CResList_Destructor_MultiVal(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047B4C0 - CLocationPair::operator=
 *
 * Assigns both min/max CLocations of a CLocationPair (multi extent pair).
 */
static void *
CLocationPair_CopyAssign(CLocationPair *dst, CLocationPair *src)
{
	CLocation_SetLoc(&dst->loc1, &src->loc1);
	CLocation_SetLoc(&dst->loc2, &src->loc2);
	return dst;
}

/*
 * 0x0047B4F0 - CLocation::CopyFromSrc
 *
 * Copies a CLocation from src into this.
 */
static void *
CLocation_CopyFromSrc(CLocation *this, CLocation *src)
{
	CLocation_SetLoc(this, src);
	return this;
}

/*
 * 0x0047B510 - CLocationPair::CopyFromSrc
 *
 * Copies both CLocations and the rotation field from src into this.
 */
static void *
CLocationPair_CopyFromSrc(CLocationPair *this, CLocationPair *src)
{
	CLocation_SetLoc(&this->loc1, &src->loc1);
	CLocation_SetLoc(&this->loc2, &src->loc2);
	this->rotation = src->rotation;
	return this;
}

/*
 * 0x0047B550 - CMultiComponentDef scalar deleting destructor
 *
 * Calls CMultiComponent_Destructor and optionally frees.
 */
static void *
CMultiComponent_ScalarDelete(CMultiComponentDef *this, int flags)
{
	CMultiComponent_Destructor(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047BFF0 - Smart pointer scalar deleting destructor (CVector variant)
 *
 * Calls SmartPtr_Destructor_CVector and optionally frees.
 */
__attribute__((unused)) void *
SmartPtr_CVector_ScalarDelete(CSmartPtr *this, int flags)
{
	SmartPtr_Destructor_CVector(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047C730 - Smart pointer destructor (CVector variant)
 *
 * Destroys the owned CVector and clears the pointer.
 */
static void
SmartPtr_Destructor_CVector(CSmartPtr *this)
{
	if (this->owned != NULL) {
		CVector *owned = (CVector *)this->owned;
		if (owned != NULL)
			CVector_ScalarDelete(owned, 1);
		this->owned = NULL;
	}
}

/*
 * 0x0047C7E0 - InsertionSort_Int_Small
 *
 * Insertion sort entry for small int arrays.
 */
void
InsertionSort_Int_Small(void *first, void *last, uint8_t cmpVal)
{
	int size = GameCentMon_GetPlayerCount();
	InsertionSort_Int(first, last, cmpVal, size);
}

/*
 * 0x0047C8E0 - InsertionSort_Int_ShiftDown
 *
 * Shifts elements greater than value right, then inserts value.
 */
void
InsertionSort_Int_ShiftDown(void *pos, uintptr_t value, uint8_t cmpVal)
{
	uintptr_t *cur = (uintptr_t *)pos;

	for (;;) {
		cur--;
		if (!(IntLessThan(&value, cur, cmpVal) & 0xFF)) {
			*(cur + 1) = value;
			return;
		}
		*((uintptr_t *)pos) = *cur;
		pos = cur;
	}
}

/*
 * 0x0047C930 - InsertionSort_Dist_Small
 *
 * Insertion sort entry for small arrays sorted by distance to cmpLoc.
 */
void
InsertionSort_Dist_Small(void *first, void *last, CLocation cmpLoc)
{
	int size = GameCentMon_GetPlayerCount();
	USED(size);
	CLocation tmp;
	CLocation_CopyFromSrc(&tmp, &cmpLoc);
	InsertionSort_Dist(first, last, tmp);
}

/*
 * 0x0047CA70 - InsertionSort_Dist_ShiftDown
 *
 * Shifts elements farther from cmpLoc right, then inserts value.
 */
void
InsertionSort_Dist_ShiftDown(void *pos, uintptr_t value, CLocation cmpLoc)
{
	uintptr_t *cur = (uintptr_t *)pos;

	for (;;) {
		cur--;
		if (!(CLocation_DistanceComparator2D(&cmpLoc, (CItem *)value, (CItem *)*cur) & 0xFF)) {
			*(cur + 1) = value;
			return;
		}
		*((uintptr_t *)pos) = *cur;
		pos = cur;
	}
}

/*
 * 0x0047CAC0 - CLocation::DistanceComparator2D
 *
 * Returns 1 if entity a is closer to the reference point than b
 * in 2D, breaking ties on absolute Z distance.
 */
int
CLocation_DistanceComparator2D(CLocation *this, CItem *a, CItem *b)
{
	CLocation *ref = this;
	int distA, distB;
	int zdiffA, zdiffB;

	distA = Location_Distance2D(a->resourceEntity.entity.location.x, a->resourceEntity.entity.location.y, ref->x, ref->y);
	distB = Location_Distance2D(b->resourceEntity.entity.location.x, b->resourceEntity.entity.location.y, ref->x, ref->y);
	if (distA != distB)
		return distA < distB;

	zdiffA = a->resourceEntity.entity.location.z - ref->z;
	zdiffB = b->resourceEntity.entity.location.z - ref->z;
	if (zdiffA < 0)
		zdiffA = -zdiffA;
	if (zdiffB < 0)
		zdiffB = -zdiffB;
	return zdiffA < zdiffB;
}

/*
 * 0x0047CB90 - Smart pointer scalar deleting destructor (CMultiDef variant)
 *
 * Calls SmartPtr_Destructor_CMultiDef and optionally frees.
 */
__attribute__((unused)) void *
SmartPtr_CMultiDef_ScalarDelete(CSmartPtr *this, int flags)
{
	SmartPtr_Destructor_CMultiDef(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047CBC0 - CVector scalar deleting destructor
 *
 * Calls CVector_Destructor and optionally frees.
 */
__attribute__((unused)) void *
CVector_ScalarDelete(CVector *this, int flags)
{
	CVector_Destructor(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x0047CF00 - Smart pointer destructor (CMultiDef variant)
 *
 * Destroys the owned CMultiDef and clears the pointer.
 */
static void
SmartPtr_Destructor_CMultiDef(CSmartPtr *this)
{
	if (this->owned != NULL) {
		CMultiDef *owned = (CMultiDef *)this->owned;
		if (owned != NULL)
			CMultiDef_ScalarDelete(owned, 1);
		this->owned = NULL;
	}
}

/*
 * 0x0047D0A0 - InsertionSort_Int
 *
 * Insertion sort over [first, last) using IntLessThan as comparator.
 */
static void
InsertionSort_Int(void *first, void *last, int cmpVal, int unused)
{
	USED(unused);
	uintptr_t *pos;
	uintptr_t val;

	if (first == last)
		return;

	pos = (uintptr_t *)first;
	for (;;) {
		pos++;
		if (pos == (uintptr_t *)last)
			return;
		val = *pos;
		if (IntLessThan(&val, first, cmpVal) & 0xFF) {
			vector_CopyBackward(first, pos, pos + 1);
			*(uintptr_t *)first = val;
		} else {
			InsertionSort_Int_ShiftDown(pos, val, cmpVal);
		}
	}
}

/*
 * 0x0047D280 - InsertionSort_Dist
 *
 * Insertion sort over [first, last) using CLocation_DistanceComparator2D.
 */
static void
InsertionSort_Dist(void *first, void *last, CLocation cmpLoc)
{
	uintptr_t *pos;
	uintptr_t val;

	if (first == last)
		return;

	pos = (uintptr_t *)first;
	for (;;) {
		pos++;
		if (pos == (uintptr_t *)last)
			return;
		val = *pos;
		if (CLocation_DistanceComparator2D(&cmpLoc, (CItem *)val, (CItem *)*(uintptr_t *)first) & 0xFF) {
			vector_CopyBackward(first, pos, pos + 1);
			*(uintptr_t *)first = val;
		} else {
			InsertionSort_Dist_ShiftDown(pos, val, cmpLoc);
		}
	}
}

/*
 * 0x00489A85 - CMulti::GetValue (VT_GET_VALUE override)
 *
 * Sums contained item value and optionally normalizes it.
 */
int
CMulti_GetValue_VT(CItem *self, int useResource, int normalize)
{
	int value;

	value = 0;
	if (!CItem_IsValueless(self)) {
		value += CContainer_GetValue_VT(self, useResource, 0);
		if (normalize != 0)
			value = CItem_NormalizeValue(self, value);
	}
	return value;
}

/*
 * 0x00489AD5 - CMulti::SendEntityUpdate (VT_SEND_ENTITY_UPDATE override)
 *
 * Sends MOVE, CORPSE_EQ, and MULTI_OBJ_TO_OBJ packets for a corpse
 * to a single viewer. No-op if ServerOnly is set.
 */
void
CMulti_SendEntityUpdate_VT(CItem *self, CItem *viewer, int unused)
{
	uint8_t pktBuf[0x18];
	uint8_t corpsePkt[0xD0];
	uint8_t contPkt[0x20018];

	USED(unused);

	if (self->itemFlags & ItemFlag_ServerOnly)
		return;

	PacketManager_MakePacket_MOVE(pktBuf, self);
	SendToClient(viewer, pktBuf, -1);

	PacketManager_MakePacket_CORPSE_EQ(corpsePkt, self->serial, ((CCorpse *)self)->equipSlots);
	SendToClient(viewer, corpsePkt, -1);

	PacketManager_MakePacket_MULTI_OBJ_TO_OBJ(contPkt, (CContainer *)self, 0, 0);
	SendToClient(viewer, contPkt, -1);
}

/*
 * 0x00489BC7 - CMulti::GetAmount (VT_GET_AMOUNT override)
 *
 * Returns the corpse body type.
 */
uint16_t
CMulti_GetAmount_VT(CItem *self)
{
	return CCorpse_GetCorpseBodyType((CCorpse *)self);
}

/*
 * 0x00489BDA - CMulti::NotifyNearby (VT_NOTIFY_NEARBY override)
 *
 * Sends MOVE, CORPSE_EQ, and MULTI_OBJ_TO_OBJ packets for a corpse
 * to a player list. No-op if ServerOnly is set.
 */
void
CMulti_NotifyNearby_VT(CItem *self, CVector *playerList, int unused)
{
	uint8_t pktBuf[0x18];
	uint8_t corpsePkt[0xD0];
	uint8_t contPkt[0x20018];

	USED(unused);

	if (self->itemFlags & ItemFlag_ServerOnly)
		return;

	PacketManager_MakePacket_MOVE(pktBuf, self);
	SendToClientList(playerList, pktBuf);

	PacketManager_MakePacket_CORPSE_EQ(corpsePkt, self->serial, ((CCorpse *)self)->equipSlots);
	SendToClientList(playerList, corpsePkt);

	PacketManager_MakePacket_MULTI_OBJ_TO_OBJ(contPkt, (CContainer *)self, 0, 0);
	SendToClientList(playerList, contPkt);
}

/*
 * 0x0048AB13 - CMulti::GetDirection (VT_GET_DIRECTION override)
 *
 * Returns CCorpse.decayTick - corpses repurpose that slot for direction.
 */
int
CMulti_GetDirection_VT(CItem *self)
{
	return ((CCorpse *)self)->decayTick;
}

/*
 * 0x0048AB27 - CMulti::OnDeath (vtable[0x1B8] override)
 *
 * Stores value in CCorpse.decayTick.
 */
void
CMulti_OnDeath_VT(CItem *self, int value)
{
	((CCorpse *)self)->decayTick = value;
}

/*
 * 0x004C0560 - SetMultiCarry
 *
 * Sets the carry field of a CMultiSlave.
 */
void
CMultiSlave_SetCarry(CMultiSlave *ms, uint32_t carry)
{
	ms->carry = carry;
}

/*
 * 0x004CD8D7 - GetBodyType_Wrapper
 *
 * Cdecl shim for CEntity_GetBodyType.
 */
uint16_t
GetBodyType_Wrapper(CItem *item)
{
	return CEntity_GetBodyType(item);
}

/*
 * 0x004CD8F0 - CSerialList::GetFlags
 *
 * Returns CSerialValue.flags.
 */
int
CSerialList_GetFlags(CSerialValue *this)
{
	return this->flags;
}

/*
 * 0x004CD910 - CEntity::SetLocationByDelta
 *
 * Adds delta to the entity location, wrapping the world boundary.
 */
void
CEntity_SetLocationByDelta(CItem *this, CLocation *delta)
{
	CLocation result;

	CLocation_AddWrapped(&this->resourceEntity.entity.location, &result, delta);
	CLocation_SetLoc(&this->resourceEntity.entity.location, &result);
}

/*
 * 0x004CD940 - CMultiComponent::SetFlags
 *
 * Sets CMultiComponent.flags.
 */
void
CMultiComponent_SetFlags(CMultiComponent *mc, uint8_t flags)
{
	mc->flags = flags;
}

/*
 * 0x004CD960 - CMultiSlave::GetCarry
 *
 * Returns the slave's carry field.
 */
uint32_t
CMultiSlave_GetCarry(CMultiSlave *ms)
{
	return ms->carry;
}

/*
 * 0x004CD980 - CMultiSlave::GetRange
 *
 * Returns the slave's range field.
 */
uint16_t
CMultiSlave_GetRange(CMultiSlave *ms)
{
	return ms->range;
}

/*
 * 0x004CD9A0 - CMultiSlave::SetRange
 *
 * Stores range into the slave's range field.
 */
void
CMultiSlave_SetRange(CMultiSlave *ms, uint16_t range)
{
	ms->range = range;
}

/*
 * 0x004CD9E0 - CEntity::BeginIter
 *
 * Initializes an iteration context for this search context's bucket.
 */
void *
CEntity_BeginIter(CSearchCtx *this, void *out)
{
	void *bucket = (void *)(uintptr_t)CSearchCtx_GetBucket(this);
	CIterCtx_Set(out, bucket);
	return out;
}

/*
 * 0x004CDA10 - CItem::GetType
 *
 * Returns the type slot (vtable pointer minus one slot).
 */
void *
CItem_GetType(CItem *this)
{
	return (void *)(*(uintptr_t *)this - sizeof(void *));
}

/*
 * 0x004CDA30 - CItem::IsSameType
 *
 * Returns 1 if two search contexts differ, 0 if they match.
 */
int
CItem_IsSameType(CSearchCtx *a, CSearchCtx *b)
{
	int result = SearchCtx_IsEqual(a, b);
	return (result & 0xFF) ? 0 : 1;
}

/*
 * 0x006470A0 - g_MultiManager
 *
 * Global multi placement manager (a CResManager: 66-bucket hash table
 * keyed by typeId).
 */
CResManager g_MultiManager; /* 0x006470A0 */

/*
 * 0x006472B8 - the second CResManager the binary's CMultiManager
 * carries at +0x218, immediately after the first. Only the two
 * orphan slave-record helpers below reach it, through pointer
 * arithmetic on the manager; here it is a separate global.
 */
CResManager g_MultiManagerSlaves;

/*
 * Helper - CVector_ClearFreeRaw
 *
 * Frees a buffer allocated by CVector (operator delete[]).
 */
void
CVector_ClearFreeRaw(void *ptr, int count)
{
	USED(count);
	free(ptr);
}

/*
 * Helper - CMultiManager_FindType
 *
 * Looks up a CMultiDef in the CResManager hash table by typeId.
 */
static CMultiDef *
CMultiManager_FindType(CResManager *rm, int typeId)
{
	uint32_t key = (uint32_t)typeId;
	uint32_t bucket = ResManager_HashInt(key, 0x41);
	CResList *keyList, *valList;
	CResListNode *keyNode, *valNode;

	keyList = rm->keys[bucket];
	valList = rm->vals[bucket];
	if (keyList == NULL)
		return NULL;

	keyNode = keyList->head;
	valNode = valList->head;
	while (keyNode != NULL) {
		if (*(uint32_t *)keyNode->data == key)
			return (CMultiDef *)valNode->data;
		keyNode = keyNode->next;
		valNode = valNode->next;
	}
	return NULL;
}

/*
 * Helper - CMulti_Free
 *
 * Releases a multi: slaves go through their scalar deleting destructor,
 * components are returned to the pool. Decomposes the inlined CMulti
 * cleanup pattern repeated by CItem_Destructor.
 */
void
CMulti_Free(CMultiComponent *mc)
{
	if (mc == NULL)
		return;

	if (CMultiComponent_IsOwner(mc))
		CMultiComponent_Destroy(mc, 1);
	else
		CMultiComponentPool_Return(mc);
}

/*
 * Helper - HideItemsInVector
 *
 * Calls VT_HIDE on every CItem in the vector.
 */
static void
HideItemsInVector(CVector *list)
{
	uintptr_t *p;

	for (p = (uintptr_t *)list->begin; p != (uintptr_t *)list->end; p++) {
		CItem *item = (CItem *)*p;
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);
	}
}

/*
 * Helper - RestoreItemsInVector
 *
 * Drops every CItem in the vector at its own entity location.
 */
static void
RestoreItemsInVector(CVector *list)
{
	uintptr_t *p;

	for (p = (uintptr_t *)list->begin; p != (uintptr_t *)list->end; p++) {
		CItem *item = (CItem *)*p;
		CLocation *loc = &item->resourceEntity.entity.location;
		((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
	}
}

/*
 * Helper - RelocateItemsInVector
 *
 * Shifts every item's drop location by (newLoc - oldLoc) with world
 * wrapping, preserving relative offsets when a multi moves.
 */
static void
RelocateItemsInVector(CVector *list, CLocation *oldLoc, CLocation *newLoc)
{
	uintptr_t *p;

	for (p = (uintptr_t *)list->begin; p != (uintptr_t *)list->end; p++) {
		CItem *item = (CItem *)*p;
		CLocation localLoc;
		CLocation delta;

		CLocation_CopyFrom(&localLoc, &item->resourceEntity.entity.location);

		CLocation_ComputeDelta(&localLoc, &delta, oldLoc);
		CLocation_CopyFrom(&localLoc, &delta);

		CLocation_Add(&localLoc, newLoc);

		((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &localLoc);
	}
}

/*
 * Custom - CMultiSlave_MapSwitchMove_Wrap
 *
 * FEAT_BOAT_MAPSWITCH completion of CMultiSlave::MapSwitchMove. The
 * binary's out-of-bounds branch is an unfinished stub; this helper
 * wraps loc Felucca-style via CLocation_AddWrapped (only when the
 * boat's owner is on the Felucca side - x < 0x1400 - matching the
 * binary's own wrap gate), moves the multi via CMultiSlave_Move so
 * carried items and players follow, then fires the serverswitch
 * script trigger (0x32) on each carried player so shipnakedhack.m
 * can schedule its sendPlayerZmoveStuff callback. Returns 1, the
 * same value as the binary stub.
 */
static int
CMultiSlave_MapSwitchMove_Wrap(CMultiSlave *slave, CLocation *loc)
{
	CItem *ownerItem;
	CLocation *ownerLoc;
	CLocation delta;
	CLocation wrappedLoc;
	CVector tmpItems;
	CVector playerSerials;
	char typeFlag = 0;
	uintptr_t *p;

	ownerItem = (CItem *)slave->base.ownerItem;
	if (ownerItem == NULL)
		return 1;

	ownerLoc = &ownerItem->resourceEntity.entity.location;
	if ((int16_t)ownerLoc->x >= 0x1400)
		return 1;

	delta.x = (uint16_t)((int16_t)loc->x - (int16_t)ownerLoc->x);
	delta.y = (uint16_t)((int16_t)loc->y - (int16_t)ownerLoc->y);
	delta.z = (int16_t)((int16_t)loc->z - (int16_t)ownerLoc->z);
	CLocation_AddWrapped(ownerLoc, &wrappedLoc, &delta);

	CVector_Constructor(&playerSerials, &typeFlag);
	CVector_Constructor(&tmpItems, &typeFlag);
	CMultiSlave_GetItems(slave, &tmpItems);
	for (p = (uintptr_t *)tmpItems.begin; p != (uintptr_t *)tmpItems.end; p++) {
		CItem *item = (CItem *)*p;
		if (VT_IsPlayer(item))
			CVector_PushBack(&playerSerials, (uintptr_t)CMobile_GetSerial((CMobile *)item));
	}
	CVector_Destructor(&tmpItems);

	CMultiSlave_Move(slave, &wrappedLoc);

	for (p = (uintptr_t *)playerSerials.begin; p != (uintptr_t *)playerSerials.end; p++) {
		CItem *player = CWorld_FindBySerial(g_World, (uint32_t)*p);
		if (player != NULL)
			Entity_ExecuteEvent(&player->resourceEntity.entity, 0x32);
	}

	CVector_Destructor(&playerSerials);
	return 1;
}
