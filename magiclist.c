/*
 * CMagicList - property bundle attached to a magic item.
 *
 * Ordered list of (effect, value) pairs with copy / assignment
 * semantics, iterated when computing an item's effective stats,
 * rendering its name, or serialising it to disk.
 */

#include <stddef.h>
#include <stdint.h>

#include "container.h"
#include "dat.h"
#include "egg.h"
#include "magicfactory.h"
#include "magiclist.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "wombat_exec.h"
#include "world.h"

static CResManager *CMagicItemFactory_GetPropsRM(CMagicItemFactory *self); // 0x00467060
static void *CMagicItemList_CopyEffectNames(CResList *dst, CResList *src); // 0x00466E30
static void *CMagicItemList_CopyIntList(CResList *dst, CResList *src); // 0x00466D80
static void *CMagicItemList_InitAndCopyInt(CResList *this, CResList *src); // 0x00466D40
static void *CMagicItemList_FindValue(CResManager *rm, CSearchCtx *ctx); // 0x00466D00
static void *CMagicItemList_InitAndCopyEffects(CResList *this, CResList *src); // 0x00466C40
static void *CMagicItemList_CopyStrList(CResList *dst, CResList *src); // 0x00466BF0
static void *CMagicItemList_CopyConstructor(CMagicItemList *this, CMagicItemList *src); // 0x00466B80
static void CMagicItemList_Init(CMagicItemList *obj); // 0x004663A3

/*
 * 0x00440420 - CMagicItemListNode::SetData
 *
 * Copies the CString and int payload from src.
 */
CMagicItemListNode *
CMagicItemListNode_SetData(CMagicItemListNode *node, CMagicItemListNode *src)
{
	CString_Assign(&node->str, &src->str);
	node->value = src->value;
	return node;
}

/*
 * 0x00466211 - CEffectTableEntry::CEffectTableEntry
 *
 * Zeros type, default-constructs name, initializes entries list.
 * weight and direction are left uninitialized (operator new zeros
 * them, or the ConstructorWeight caller fills them in).
 */
CEffectTableEntry *
CEffectTableEntry_Constructor(CEffectTableEntry *this)
{
	this->type = 0;
	CString_Constructor(&this->name, "");
	CStringList_Init(&this->entries);
	return this;
}

/*
 * 0x00466270 - CEffectTableEntry::CEffectTableEntry (copy constructor)
 *
 * Copies the type, the name string and the entry list. The binary
 * reaches the list copy through CMagicItemList's copy constructor,
 * which MSVC folded with CStringList's because both are CResList
 * copies. weight and direction are left uncopied.
 */
static __attribute__((unused)) CEffectTableEntry *
CEffectTableEntry_CopyConstructor(CEffectTableEntry *this, CEffectTableEntry *src)
{
	this->type = src->type;
	CString_CopyConstructor(&this->name, &src->name);
	CMagicItemList_CopyConstructor((CMagicItemList *)&this->entries, (CMagicItemList *)&src->entries);
	return this;
}

/*
 * 0x004662DB - CMagicItemListNode::CMagicItemListNode (copy constructor)
 *
 * Copy-constructs the CString and copies the int payload.
 */
CMagicItemListNode *
CMagicItemListNode_CopyConstructor(CMagicItemListNode *node, CMagicItemListNode *src)
{
	CString_CopyConstructor(&node->str, &src->str);
	node->value = src->value;
	return node;
}

/*
 * 0x00466303 - CMagicItemList::CMagicItemList
 *
 * Runs the base CResListNode ctor then Init.
 */
void
CMagicItemList_Constructor(CMagicItemList *obj)
{
	CResListNode_Constructor_bin((CResListNode *)obj);
	CMagicItemList_Init(obj);
}
/*
 * 0x0046634E - CMagicItemList::CopyFrom
 *
 * Copies the effect entries from src, then the three scalar fields.
 */
static __attribute__((unused)) CMagicItemList *
CMagicItemList_CopyFrom(CMagicItemList *obj, CMagicItemList *src)
{
	CMagicItemList_InitAndCopyEffects(&obj->list, &src->list);
	obj->mlCount = src->mlCount;
	obj->itemId = src->itemId;
	obj->result = src->result;
	return obj;
}

/*
 * 0x00466390 - CMagicItemList::~CMagicItemList
 *
 * Clears the embedded CResList.
 */
void
CMagicItemList_Destructor(CMagicItemList *obj)
{
	CResList_Destructor_MagicItemList(&obj->list);
}

/*
 * 0x004663A3 - CMagicItemList::Init
 *
 * Zeros itemId, mlCount, result and clears the embedded list.
 */
static void
CMagicItemList_Init(CMagicItemList *obj)
{
	obj->itemId = 0;
	obj->mlCount = 0;
	CResList_ClearInternal_MagicItemList_rb(&obj->list);
	obj->result = 0;
}

/*
 * 0x004663D3 - CMagicItemList::GetResult
 *
 * Creates and returns a new CItem of type obj->itemId.
 */
CItem *
CMagicItemList_GetResult(CMagicItemList *obj)
{
	return CWorld_CreateItem(g_World, obj->itemId);
}

/*
 * 0x004663F0 - CMagicItemList::PlaceInContainer
 *
 * Takes the factory's pending result and adds it to the container with
 * the given serial at (-1, -1, 0), then records it in the list.
 * Returns the placed entity's serial, or 0 when there is no result, the
 * serial resolves to nothing, or the target is not a container.
 */
static __attribute__((unused)) int
CMagicItemList_PlaceInContainer(CMagicItemList *obj, uint32_t serial)
{
	CItem *result;
	CItem *container;
	CLocation loc;

	result = CMagicItemList_GetResult(obj);
	if (result != NULL && ValidateInWorld(result) == 0)
		result = NULL;
	if (result == NULL)
		return 0;

	container = CWorld_FindBySerial(g_World, serial);
	if (container == NULL)
		return 0;

	if (((int (*)(void *))VT_FN(container, VT_IS_MOBILE2))(container) == 0)
		return 0;

	CLocation_Init(&loc);
	CLocation_Set(&loc, -1, -1, 0);
	((void (*)(void *, void *, CLocation *))VT_FN(result, VT_ADD_TO_CONTAINER))(result, container, &loc);

	CItem_Setup(result, 0, ((CLocation * (*)(void *)) VT_FN(container, VT_GET_LOCATION))(container), 0, 1);
	CMagicItemList_AddItem(obj, result);
	return (int)result->serial;
}

/*
 * 0x004664C0 - CMagicItemList::PlaceAtLocation
 *
 * Takes the factory's pending result, drops it at loc and records it in
 * the list. Returns the placed entity's serial, or 0 when there is no
 * result or it no longer validates against the world.
 */
static __attribute__((unused)) int
CMagicItemList_PlaceAtLocation(CMagicItemList *obj, CLocation *loc)
{
	CItem *result;

	result = CMagicItemList_GetResult(obj);
	if (result != NULL && ValidateInWorld(result) == 0)
		result = NULL;
	if (result == NULL)
		return 0;

	CItem_Setup(result, 0, loc, 0, 1);
	((void (*)(void *, void *))VT_FN(result, VT_DROP_AT_FEET))(result, loc);
	CMagicItemList_AddItem(obj, result);
	return (int)result->serial;
}

/*
 * 0x00466534 - CMagicItemList::EquipOnMobile
 *
 * Takes the factory's pending result and equips it on the mobile with
 * the given serial. Returns the equipped entity's serial, or 0 when
 * there is no result, the serial resolves to nothing, or the target is
 * not a mobile. A failed equip deletes the result.
 */
static __attribute__((unused)) int
CMagicItemList_EquipOnMobile(CMagicItemList *obj, uint32_t serial, uint8_t layer)
{
	CItem *result;
	CItem *mobile;

	result = CMagicItemList_GetResult(obj);
	if (result != NULL && ValidateInWorld(result) == 0)
		result = NULL;
	if (result == NULL)
		return 0;

	mobile = CWorld_FindBySerial(g_World, serial);
	if (mobile == NULL)
		return 0;

	if (((int (*)(void *))VT_FN(mobile, VT_IS_MOBILE))(mobile) == 0)
		return 0;

	if (((int (*)(void *, void *, int))VT_FN(result, VT_EQUIP_ON_MOBILE))(result, mobile, layer) != 1) {
		if (result != NULL)
			((void (*)(void *))VT_FN(result, VT_DELETE))(result);
		result = NULL;
		return 0;
	}

	CItem_Setup(result, 0, ((CLocation * (*)(void *)) VT_FN(mobile, VT_GET_LOCATION))(mobile), 0, 1);
	CMagicItemList_AddItem(obj, result);
	return (int)result->serial;
}

/*
 * 0x00466612 - CMagicItemList::AddItem
 *
 * Enchants an item with the effects in this list: attaches each
 * effect's scripts, records its objvar, concatenates display name
 * and suffix, then stores MagicItemName and EffectObjVars on the
 * entity and applies the factory's global scripts.
 */
void
CMagicItemList_AddItem(CMagicItemList *obj, CItem *entity)
{
	CString article, effectsStr, suffixStr, nameStr;
	CSearchCtx searchCtx, localCtx;
	void *effectEntry;
	CList objVarList;
	CResListNode *iter, *innerIter;
	CLocation *loc;
	CResManager *factoryRM;
	int32_t effectLevel;
	void *resNodeData;
	uint32_t effectWeight;
	CString *effObjvarName, *effDisplayName, *effSuffix;
	CResList *effScripts;
	CString vowelStr, articleResult, tmpKey;
	uint16_t itemId;

	CString_Constructor(&article, "");
	CString_Constructor(&effectsStr, "");
	CString_Constructor(&suffixStr, "");
	CString_DefaultConstructor(&nameStr);
	CSearchCtx_Constructor(&searchCtx);
	effectEntry = NULL;
	CList_Constructor(&objVarList);

	loc = ((CLocation * (*)(void *)) VT_FN(entity, VT_GET_LOCATION))(entity);
	if (!ResBankMagicCheck(loc, g_ResTypeId_Magic)) {
		CList_Destructor(&objVarList);
		CString_Destructor(&nameStr);
		CString_Destructor(&suffixStr);
		CString_Destructor(&effectsStr);
		CString_Destructor(&article);
		return;
	}

	effectLevel = (int32_t)obj->mlCount;
	CResourceEntity_AddNodeScaled(entity, g_ResTypeId_Magic, 3, effectLevel, 0, effectLevel, 0, 1, 1);

	factoryRM = CMagicItemFactory_GetPropsRM(&g_MagicItemFactory);

	iter = CResList_Begin(&obj->list);
	while (CResList_IsValid(&obj->list, iter)) {
		resNodeData = CResList_GetData(&obj->list, iter);

		// Binary passes CString*; HashStrA calls GetBuffer internally
		CResManager_FindByStrCtxC(factoryRM, &localCtx, (const char *)resNodeData, 1);
		CSearchCtx_Add(&searchCtx, &localCtx);

		if (CSearchCtx_Find(&searchCtx))
			effectEntry = CMagicItemList_FindValue(factoryRM, &searchCtx);

		// Second CSearchCtx_Find check is redundant
		if (!CSearchCtx_Find(&searchCtx) || effectEntry == NULL)
			goto next_effect;

		effectWeight = (uint32_t)((CMagicItemListNode *)resNodeData)->value;
		USED(effectWeight); // binary dead store at ebp-0x78

		effScripts = &((CItemEffectDef *)effectEntry)->sublist2;
		innerIter = CResList_Begin(effScripts);
		while (CResList_IsValid(effScripts, innerIter)) {
			Entity_AttachScript(entity, CString_GetBuffer((CString *)CResList_GetData(effScripts, innerIter)), 1);
			innerIter = CResList_Next(effScripts, innerIter);
		}

		effObjvarName = &((CItemEffectDef *)effectEntry)->subcat;
		if (!CString_IsEmpty(effObjvarName)) {
			// Binary re-reads resNodeData[+0x10] via CResList_GetData
			resNodeData = CResList_GetData(&obj->list, iter);
			ObjVar_SetStr(entity, effObjvarName, 0, (uint32_t)((CMagicItemListNode *)resNodeData)->value);
			CList_Append(&objVarList, 1, (uintptr_t)effObjvarName);
		}

		effDisplayName = (CString *)effectEntry;
		if (!CString_IsEmpty(effDisplayName)) {
			if (!CString_IsEmpty(&effectsStr))
				CString_AppendCStr(&effectsStr, ", ");
			CString_ConcatCString(&effectsStr, effDisplayName);
		}

		effSuffix = &((CItemEffectDef *)effectEntry)->category;
		if (!CString_IsEmpty(effSuffix)) {
			if (CString_IsEmpty(&suffixStr))
				CString_AppendCStr(&suffixStr, "of ");
			else
				CString_AppendCStr(&suffixStr, " and ");
			CString_ConcatCString(&suffixStr, effSuffix);
		}

next_effect:
		iter = CResList_Next(&obj->list, iter);
	}

	if (obj->list.count <= 0)
		goto cleanup;

	CString_Constructor(&vowelStr, "aeiouAEIOU");
	itemId = obj->itemId;
	Script_getArticle(&articleResult, itemId);
	CString_Assign(&article, &articleResult);
	CString_Destructor(&articleResult);

	// Binary compares article with "an" then "a" - second compare
	// is dead code (if article=="an" it can never =="a").
	if (CString_CompareStr(&article, "an") != 0)
		goto vowel_check;
	if (CString_CompareStr(&article, "a") == 0)
		goto skip_vowel;

vowel_check:
	if (!CString_IsEmpty(&effectsStr)) {
		char firstChar = *CString_CharAt(&effectsStr, 0);
		if (CString_Contains(&vowelStr, firstChar))
			CString_AssignCStr(&article, "an");
		else
			CString_AssignCStr(&article, "a");
	}

skip_vowel:
	if (!CString_IsEmpty(&article)) {
		CString_Assign(&nameStr, &article);
		CString_AppendCStr(&nameStr, " ");
	}

	if (!CString_IsEmpty(&effectsStr)) {
		CString_ConcatCString(&nameStr, &effectsStr);
		CString_AppendCStr(&nameStr, " ");
	}

	CString_AppendCStr(&nameStr, g_ItemTileData[itemId].name);

	if (!CString_IsEmpty(&suffixStr)) {
		CString_AppendCStr(&nameStr, " ");
		CString_ConcatCString(&nameStr, &suffixStr);
	}

	CString_Constructor(&tmpKey, "MagicItemName");
	ObjVar_SetStr(entity, &tmpKey, 1, (uintptr_t)&nameStr);
	CString_Destructor(&tmpKey);

	CString_Constructor(&tmpKey, "EffectObjVars");
	ObjVar_SetStr(entity, &tmpKey, 5, (uintptr_t)&objVarList);
	CString_Destructor(&tmpKey);

	CMagicItemFactory_AttachGlobalScripts(&g_MagicItemFactory, entity);

	CString_Destructor(&vowelStr);

cleanup:
	CList_Destructor(&objVarList);
	CString_Destructor(&nameStr);
	CString_Destructor(&suffixStr);
	CString_Destructor(&effectsStr);
	CString_Destructor(&article);
}

/*
 * 0x00466B80 - CMagicItemList::CMagicItemList (copy constructor)
 *
 * Init-and-copy the list, then copy mlCount.
 */
static __attribute__((unused)) void *
CMagicItemList_CopyConstructor(CMagicItemList *this, CMagicItemList *src)
{
	CMagicItemList_InitAndCopyInt(&this->list, &src->list);
	this->mlCount = src->mlCount;
	return this;
}

/*
 * 0x00466BB0 - CMagicItemList::InitAndCopyStr
 *
 * Zeros the list head/tail/count and copies entries from src.
 */
void *
CMagicItemList_InitAndCopyStr(CResList *this, CResList *src)
{
	this->head = NULL;
	this->tail = NULL;
	this->count = 0;

	CMagicItemList_CopyStrList(this, src);
	return this;
}

/*
 * 0x00466BF0 - CMagicItemList::CopyStrList
 *
 * Clears dst, then appends each CString node from src.
 */
static void *
CMagicItemList_CopyStrList(CResList *dst, CResList *src)
{
	CResListNode *iter;

	CResList_ClearInternal(dst);
	iter = CResList_Begin(src);
	while (iter != NULL) {
		CResList_InsertTailStr(dst, (CString *)CResList_GetData(src, iter));
		iter = CResList_Next(src, iter);
	}
	return dst;
}

/*
 * 0x00466C40 - CMagicItemList::InitAndCopyEffects
 *
 * Zeros head/tail/count and copies effect-name entries from src.
 */
static __attribute__((unused)) void *
CMagicItemList_InitAndCopyEffects(CResList *this, CResList *src)
{
	this->head = NULL;
	this->tail = NULL;
	this->count = 0;

	CMagicItemList_CopyEffectNames(this, src);
	return this;
}

/*
 * 0x00466D00 - CMagicItemList::FindValue
 *
 * Returns the head value of the search ctx's matched bucket, or
 * NULL when the ctx did not find a match.
 */
static void *
CMagicItemList_FindValue(CResManager *rm, CSearchCtx *ctx)
{
	void *valNode;
	uintptr_t bucket;

	if (!CSearchCtx_Find(ctx))
		return NULL;

	valNode = (void *)(uintptr_t)CSearchCtx_GetValNode(ctx);
	bucket = (uintptr_t)CSearchCtx_GetBucket(ctx);
	return CResList_GetHeadIfNotNull(rm->vals[bucket], valNode);
}

/*
 * 0x00466D40 - CMagicItemList::InitAndCopyInt
 *
 * Zeros head/tail/count and copies int-payload entries from src.
 */
static void *
CMagicItemList_InitAndCopyInt(CResList *this, CResList *src)
{
	this->head = NULL;
	this->tail = NULL;
	this->count = 0;

	CMagicItemList_CopyIntList(this, src);
	return this;
}

/*
 * 0x00466D80 - CMagicItemList::CopyIntList
 *
 * Clears dst, then appends each int-payload node from src.
 */
static void *
CMagicItemList_CopyIntList(CResList *dst, CResList *src)
{
	CResListNode *iter;

	CResList_EraseAllSLN(dst);
	iter = CResList_Begin(src);
	while (iter != NULL) {
		CResList_AllocAndSetDataNP(dst, CResList_GetData(src, iter));
		iter = CResList_Next(src, iter);
	}
	return dst;
}

/*
 * 0x00466E30 - CMagicItemList::CopyEffectNames
 *
 * Clears dst, then appends each effect-name node from src.
 */
static void *
CMagicItemList_CopyEffectNames(CResList *dst, CResList *src)
{
	CResListNode *iter;

	CResList_ClearInternal_MagicItemList_rb(dst);
	iter = CResList_Begin(src);
	while (iter != NULL) {
		CResList_InsertTailDataB(dst, (CMagicItemListNode *)CResList_GetData(src, iter));
		iter = CResList_Next(src, iter);
	}
	return dst;
}

/*
 * 0x00466FD0 - SmartPtr::~SmartPtr (CString variant)
 *
 * Destroys and frees the owned CString, then nulls the pointer.
 */
void
SmartPtr_Destructor_CString(CSmartPtr *this)
{
	void *owned = this->owned;

	if (owned != NULL) {
		if (owned != NULL)
			CString_ScalarDelete_MF(owned, 1);
		this->owned = NULL;
	}
}

/*
 * 0x00467060 - CMagicItemFactory::GetPropsRM
 *
 * Returns &self->propsRM.
 */
static CResManager *
CMagicItemFactory_GetPropsRM(CMagicItemFactory *self)
{
	return &self->propsRM;
}
