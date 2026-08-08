/*
 * CResourceEntity / CResourceNode - resource-bank tier of the entity
 * hierarchy.
 *
 * The CResourceEntity rung sits between CEntity and CItem, owning the
 * doubly-linked list of CResourceNode tag/resource records and the
 * template-index / tag-list ObjVar protocol used by every CItem subclass.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "blockmanager.h"
#include "container.h"
#include "egg.h"
#include "entity.h"
#include "io.h"
#include "item.h"
#include "multi.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "resbank.h"
#include "taglist.h"
#include "timer.h"
#include "utils.h"
#include "vg_pool.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

// 0x006DC23C - CResourceNode pool free list (ReturnToPool / AllocFromPool)
static CResourceNode *g_ResNodeFreeList;

static void ResourceEntity_NotifyNodeZero(CResourceNode *node); // 0x004853B3
static void CResourceNode_Init(CResourceNode *node); // 0x004A88BD
static CResourceNode *CResourceNode_ConstructorWrapper(CResourceNode *this); // 0x004A88FB
static void CResourceNode_Noop(CResourceNode *this); // 0x004A8A3B

/*
 * 0x00420FA0 - CResourceEntity::GetBodyType
 *
 * Forwards to CEntity_GetBodyType.
 */
uint16_t
CResourceEntity_GetBodyType(CItem *item)
{
	return CEntity_GetBodyType(item);
}

/*
 * 0x004211A0 - CResourceNode::GetResourceDef
 *
 * Resolves the node's id to its CResourceType in g_ResourceTypeManager.
 */
CResourceType *
CResourceNode_GetResourceDef(CResourceNode *node)
{
	return CResourceTypeManager_GetId(node->id);
}

/*
 * 0x00432260 - CResourceEntity::CheckDC (vtable[0xDC])
 *
 * Returns g_ItemTileData[bodyType].name.
 */
char *
CResourceEntity_CheckDC_VT(CItem *item)
{
	uint16_t bodyType;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	return g_ItemTileData[bodyType].name;
}

/*
 * 0x0045B42C - CResourceEntity::RemoveStatic
 *
 * For non-land tiles, finds and unlinks nodePtr from the resource slot for
 * artID and returns it to the node pool.
 */
void
CResourceEntity_RemoveStatic(uint16_t artID, CResourceNode *nodePtr)
{
	int isLandTile;
	CResourceNode *node;

	isLandTile = ((artID & 0xFFFF) & 0x8000) != 0;
	artID &= 0x7FFF;

	if (!isLandTile) {
		CWorld_Lock();
		node = CResourceEntity_FindNodeByPtr((CItem *)&g_ResEntitySlots[artID & 0xFFFF], nodePtr);
		CResourceEntity_RemoveNode((CItem *)&g_ResEntitySlots[artID & 0xFFFF], node);
		ResourceNode_ReturnToPool(node);
		CWorld_Unlock();
	}
}

/*
 * 0x004850D3 - CResourceEntity::Delete (vtable[0x90])
 *
 * Invokes the scalar deleting destructor.
 */
void
CResourceEntity_Delete(CItem *self)
{
	if (self != NULL)
		((void *(*)(void *, int))VT_FN(self, VT_DTOR))(self, 1);
}

/*
 * 0x00485261 - ResourceEntity_AttachTemplate
 *
 * Effectively a no-op: an unconditional jmp at function entry skips the
 * template-attachment loop, leaving it as dead code.
 */
void
ResourceEntity_AttachTemplate(CItem *entity)
{
	USED(entity);
}

/*
 * 0x004853B3 - ResourceEntity_NotifyNodeZero
 *
 * Empty. Called by CopyNodeScaled when zeroFlag == 0.
 */
static void
ResourceEntity_NotifyNodeZero(CResourceNode *node)
{
	USED(node);
}

/*
 * 0x004853B8 - ResourceEntity_NotifyNodeRemoval
 *
 * Empty. Per-node notification stub called before each node removal.
 */
void
ResourceEntity_NotifyNodeRemoval(CResourceNode *node)
{
	USED(node);
}

/*
 * 0x004854B2 - CResourceEntity::NotifyPostModify
 *
 * Empty. Called after resource node changes.
 */
void
CResourceEntity_NotifyPostModify(CItem *entity)
{
	USED(entity);
}

/*
 * 0x004854BD - CResourceEntity::NotifyPostModifyIfActive
 *
 * Empty. Called after NotifyPostModify when entity is still in the world.
 */
void
CResourceEntity_NotifyPostModifyIfActive(CItem *entity)
{
	USED(entity);
}

/*
 * 0x004854C8 - CResourceEntity::NotifyPreModify
 *
 * Empty. Called before resource node changes for entities still in the world.
 */
void
CResourceEntity_NotifyPreModify(CItem *entity)
{
	USED(entity);
}

/*
 * 0x00485523 - CResourceEntity::TransferResources
 *
 * Moves up to transferAmount from source's type-3 resource nodes (optionally
 * filtered by resTypeFilter) to dest. Whole nodes are relocated when they
 * fit; otherwise the surplus is split off. Resource-entity sources with
 * non-zero value2 are copied instead of moved.
 */
void
CResourceEntity_TransferResources(CItem *dest, CItem *source, uint32_t transferAmount, int resTypeFilter)
{
	CResourceNode *node, *next;

	if (transferAmount == 0 || source == dest)
		return;

	if (dest->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPreModify(dest);
	if (source->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPreModify(source);

	node = source->resourceEntity.firstChild;
	while (node != NULL) {
		next = node->next;

		if (node->id == 0) {
			node = next;
			continue;
		}

		if (resTypeFilter >= 0 && node->id != (uint16_t)resTypeFilter) {
			node = next;
			continue;
		}

		if (node->type != 3 || node->value3 <= 0) {
			node = next;
			continue;
		}

		if ((uint32_t)node->value3 <= transferAmount) {
			transferAmount -= node->value3;

			// If value2 != 0 AND vtable[0x9C](source)
			// returns 1 (CEgg returns 1, CItem returns 0),
			// copy instead of move.
			if (node->value2 != 0 && ((int (*)(void *))VT_FN(source, VT_ITEM_CHECK_9C))(source)) {
				CResourceEntity_AddNode(dest, node->id, 3, node->value3, 0, node->value3, 0);
				node->value3 = 0;
			} else {
				CResourceEntity_RemoveNode(source, node);
				CResourceEntity_InsertNode(dest, node);
			}

			if (transferAmount == 0)
				break;
		} else {
			CResourceEntity_AddNode(dest, node->id, 3, (int32_t)transferAmount, 0, (int32_t)transferAmount, 0);
			node->value3 -= transferAmount;
			break;
		}

		node = next;
	}

	CResourceEntity_NotifyPostModify(dest);
	if (dest->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPostModifyIfActive(dest);
	CResourceEntity_NotifyPostModify(source);
	if (source->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPostModifyIfActive(source);
}

/*
 * 0x004856C8 - CResourceEntity::RemoveResource
 *
 * Consumes up to amount of resTypeId from the entity's type-3 resource
 * nodes. For CEgg sources with value2 != 0 the node is kept with value3=0;
 * otherwise the node is removed when fully consumed.
 */
void
CResourceEntity_RemoveResource(CItem *entity, int amount, int resTypeId)
{
	CResourceNode *cur, *next;

	if (amount == 0)
		return;

	if (entity->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPreModify(entity);

	cur = entity->resourceEntity.firstChild;
	while (cur != NULL) {
		next = cur->next;

		if (cur->id == 0)
			goto cont;

		if (cur->id != (uint16_t)resTypeId)
			goto cont;

		if ((int8_t)cur->type != 3)
			goto cont;

		if (cur->value3 <= 0)
			goto cont;

		if (cur->value3 > amount) {
			cur->value3 -= amount;
			break;
		}

		amount -= cur->value3;

		if (cur->value2 != 0 && ((int (*)(void *))VT_FN(entity, VT_ITEM_CHECK_9C))(entity)) {
			cur->value3 = 0;
		} else {
			CResourceEntity_RemoveNode(entity, cur);
			ResourceNode_ReturnToPool(cur);
		}

		if (amount == 0)
			break;

cont:
		cur = next;
	}

	CResourceEntity_NotifyPostModify(entity);
	if (entity->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPostModifyIfActive(entity);
}

/*
 * 0x004857E1 - CResourceEntity::TransferAllResources
 *
 * Moves every non-empty type-3 resource node from source to dest.
 */
void
CResourceEntity_TransferAllResources(CItem *dest, CItem *source)
{
	CResourceNode *node, *next;

	if (source == dest)
		return;

	if (!dest->resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPreModify(dest);
	if (!source->resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPreModify(source);

	node = source->resourceEntity.firstChild;
	while (node != NULL) {
		next = node->next;

		if (node->id == 0) {
			node = next;
			continue;
		}
		if (node->type != 3) {
			node = next;
			continue;
		}

		CResourceEntity_RemoveNode(source, node);
		CResourceEntity_InsertNode(dest, node);

		node = next;
	}

	CResourceEntity_NotifyPostModify(dest);
	if (!dest->resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPostModifyIfActive(dest);

	CResourceEntity_NotifyPostModify(source);
	if (!source->resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPostModifyIfActive(source);
}

/*
 * 0x0048592F - CResourceEntity::FindNodeByPtr
 *
 * Returns target if it is in the entity's resource node chain, else NULL.
 */
CResourceNode *
CResourceEntity_FindNodeByPtr(CItem *this, CResourceNode *target)
{
	CResourceNode *node;

	node = this->resourceEntity.firstChild;
	while (node != NULL) {
		if (node == target)
			return node;
		node = node->next;
	}
	return NULL;
}

/*
 * 0x00485969 - CResourceEntity::CResourceEntity
 *
 * Chains to CEntity, installs the CResourceEntity vtable, clears
 * firstChild, and bumps g_ResourceEntityCount. The nextInContainer field
 * at offset 0x10 is overlaid as a CLocation in the binary's union, and
 * the constructor zeroes it as one.
 */
CResourceEntity *
CResourceEntity_Constructor(CResourceEntity *this)
{
	CEntity_Constructor(&this->entity);
	CLocation_Init((CLocation *)&this->nextInContainer);
	this->entity.vtable = &g_vtable_CResourceEntity;
	this->firstChild = NULL;
	g_ResourceEntityCount++;
	CLocation_Invalidate((CLocation *)&this->nextInContainer);
	return this;
}

/*
 * 0x004859E4 - CResourceEntity::RemoveAllNodes
 *
 * Returns every node on the entity to the pool. When flag != 0 pre/post
 * modify notifications are also delivered.
 */
void
CResourceEntity_RemoveAllNodes(CItem *entity, int flag)
{
	CResourceNode *node, *next;

	if (entity->resourceEntity.entity.removedFromWorld == 0 && flag != 0)
		CResourceEntity_NotifyPreModify(entity);

	node = entity->resourceEntity.firstChild;
	while (node != NULL) {
		next = node->next;
		ResourceEntity_NotifyNodeRemoval(node);
		CResourceEntity_RemoveNode(entity, node);
		ResourceNode_ReturnToPool(node);
		node = next;
	}

	if (flag != 0) {
		CResourceEntity_NotifyPostModify(entity);
		if (entity->resourceEntity.entity.removedFromWorld == 0)
			CResourceEntity_NotifyPostModifyIfActive(entity);
	}
}

/*
 * 0x00485A73 - CResourceEntity::~CResourceEntity
 *
 * Restores the vtable, strips all nodes, then chains to CEntity_Destructor.
 */
void
CResourceEntity_Destructor(CResourceEntity *this)
{
	this->entity.vtable = &g_vtable_CResourceEntity;
	g_ResourceEntityCount--;
	CResourceEntity_NotifyPreModify((CItem *)this);
	CResourceEntity_RemoveAllNodes((CItem *)this, 0);
	CEntity_Destructor(&this->entity);
}

/*
 * 0x00485ADD - CResourceEntity::SendMusicToPlayer
 *
 * Streams a RESOURCETILEDATA_Region header followed by one mode-1 packet
 * per node. Demo regions have empty music lists so the loop is inert.
 */
void
CResourceEntity_SendMusicToPlayer(CItem *resEntity, CPlayer *player, uint16_t regionIndex)
{
	uint8_t buf[0x20];
	uint32_t count;
	CResourceNode *cur;

	count = 0;
	PacketManager_MakePacket_RESOURCETILEDATA_Region(buf, regionIndex, 0, 0, NULL);
	SendToClient((CItem *)player, buf, -1);

	cur = ((ResEntitySlot *)resEntity)->nodeHead;
	while (cur != NULL) {
		count++;
		PacketManager_MakePacket_RESOURCETILEDATA_Region(buf, regionIndex, 1, count, cur);
		SendToClient((CItem *)player, buf, -1);
		cur = cur->next;
	}
}

/*
 * 0x00485B6F - CResourceEntity::InsertNode
 *
 * Prepends node, then merges any duplicate (same id and type) into it by
 * accumulating value1/value2/value3 and freeing the old node.
 */
void
CResourceEntity_InsertNode(CItem *entity, CResourceNode *node)
{
	CResourceNode *cur, *next;

	node->next = entity->resourceEntity.firstChild;
	if (entity->resourceEntity.firstChild != NULL)
		entity->resourceEntity.firstChild->prev = node;
	node->prev = NULL;
	entity->resourceEntity.firstChild = node;

	cur = entity->resourceEntity.firstChild;
	while (cur != NULL) {
		next = cur->next;

		if (cur == node) {
			cur = next;
			continue;
		}

		if (cur->id == node->id && cur->type == node->type) {
			node->value1 += cur->value1;
			node->value2 += cur->value2;
			node->value3 += cur->value3;

			CResourceEntity_RemoveNode(entity, cur);
			ResourceNode_ReturnToPool(cur);
			break;
		}

		cur = next;
	}
}

/*
 * 0x00485C53 - CResourceEntity::RemoveNode
 *
 * Unlinks node from the doubly-linked resource node list and clears its
 * prev/next pointers.
 */
void
CResourceEntity_RemoveNode(CItem *entity, CResourceNode *node)
{
	if (entity->resourceEntity.firstChild == node)
		entity->resourceEntity.firstChild = node->next;

	if (node->prev != NULL)
		node->prev->next = node->next;

	if (node->next != NULL)
		node->next->prev = node->prev;

	node->prev = NULL;
	node->next = NULL;
}

/*
 * 0x00485CBB - CResourceEntity::FindNode
 *
 * Returns the first node with the given id and type, or NULL.
 */
CResourceNode *
CResourceEntity_FindNode(CItem *entity, uint16_t id, int8_t type)
{
	CResourceNode *node;

	for (node = entity->resourceEntity.firstChild; node != NULL; node = node->next) {
		if (node->id == 0)
			continue;

		if (node->id != id)
			continue;

		if (node->type != type)
			continue;

		return node;
	}
	return NULL;
}

/*
 * 0x00485D78 - CResourceEntity::FindOrAddNode
 *
 * Returns the existing node for (resType->typeId, type), allocating and
 * inserting one when none is present.
 */
static __attribute__((unused)) CResourceNode *
CResourceEntity_FindOrAddNode(CItem *this, CResourceType *resType, int8_t type)
{
	CResourceNode *node;

	node = CItem_FindResourceNodeByIdAndType(this, resType, type);
	if (node == NULL)
		node = CResourceEntity_AddOrFindNode(this, resType, type);
	return node;
}

/*
 * 0x00485DB6 - CResourceEntity::AddOrFindNode
 *
 * Returns NULL if a matching node already exists; otherwise allocates a
 * new node carrying type and resType->typeId and inserts it.
 */
CResourceNode *
CResourceEntity_AddOrFindNode(CItem *this, CResourceType *resType, int8_t type)
{
	CResourceNode *node;

	if (CItem_FindResourceNodeByIdAndType(this, resType, type) != NULL)
		return NULL;

	node = ResourceNode_AllocFromPool();
	node->value1 = 0;
	node->value2 = 0;
	node->value3 = 0;
	node->type = type;
	node->id = (uint16_t)resType->typeId;
	CResourceEntity_InsertNode(this, node);
	return node;
}

/*
 * 0x00485FBD - CResourceEntity::AddNode
 *
 * Allocates a new CResourceNode with the given fields and prepends it to
 * the entity's resource list.
 */
void
CResourceEntity_AddNode(CItem *entity, uint16_t id, int8_t type, int32_t value1, int32_t value2, int32_t value3, int flag)
{
	CResourceNode *node;

	if (entity->resourceEntity.entity.removedFromWorld == 0 && flag != 0)
		CResourceEntity_NotifyPreModify(entity);

	node = ResourceNode_AllocFromPool();

	node->value1 = value1;
	node->value2 = value2;
	node->value3 = value3;
	node->type = (uint8_t)type;
	node->id = id;

	CResourceEntity_InsertNode(entity, node);

	if (flag != 0) {
		CResourceEntity_NotifyPostModify(entity);
		if (entity->resourceEntity.entity.removedFromWorld == 0)
			CResourceEntity_NotifyPostModifyIfActive(entity);
	}
}

/*
 * 0x0048604A - CResourceEntity::AddNodeScaled
 *
 * AddNode with values multiplied by scaleFactor. zeroFlag=0 triggers
 * NotifyNodeZero; notifyFlag!=0 drives Pre/PostModify notifications.
 */
void
CResourceEntity_AddNodeScaled(CItem *entity, uint16_t id, int8_t type, int32_t value1, int32_t value2, int32_t value3, int zeroFlag, int notifyFlag, int scaleFactor)
{
	CResourceNode *node;

	if (entity->resourceEntity.entity.removedFromWorld == 0 && notifyFlag != 0)
		CResourceEntity_NotifyPreModify(entity);

	node = ResourceNode_AllocFromPool();

	node->value1 = value1 * scaleFactor;
	node->value2 = value2 * scaleFactor;
	node->value3 = value3 * scaleFactor;
	node->type = (uint8_t)type;
	node->id = id;

	if (zeroFlag == 0)
		ResourceEntity_NotifyNodeZero(node);

	CResourceEntity_InsertNode(entity, node);

	if (notifyFlag != 0) {
		CResourceEntity_NotifyPostModify(entity);
		if (entity->resourceEntity.entity.removedFromWorld == 0)
			CResourceEntity_NotifyPostModifyIfActive(entity);
	}
}

/*
 * 0x004860F5 - CResourceEntity::CopyNodeScaled
 *
 * Copies src into a fresh node with value1/2 scaled. Binary quirk: value3
 * also copies src->value1 (not src->value3) and is scaled.
 */
void
CResourceEntity_CopyNodeScaled(CItem *entity, CResourceNode *src, int zeroFlag, int notifyFlag, int scaleFactor)
{
	CResourceNode *node;

	if (entity->resourceEntity.entity.removedFromWorld == 0 && notifyFlag != 0)
		CResourceEntity_NotifyPreModify(entity);

	node = ResourceNode_AllocFromPool();

	node->value1 = src->value1 * scaleFactor;
	node->value2 = src->value2 * scaleFactor;
	// Binary quirk: value3 copies src->value1, not src->value3.
	node->value3 = src->value1 * scaleFactor;
	node->type = src->type;
	node->id = src->id;

	if (zeroFlag == 0)
		ResourceEntity_NotifyNodeZero(node);

	CResourceEntity_InsertNode(entity, node);

	if (notifyFlag != 0) {
		CResourceEntity_NotifyPostModify(entity);
		if (entity->resourceEntity.entity.removedFromWorld == 0)
			CResourceEntity_NotifyPostModifyIfActive(entity);
	}
}

/*
 * 0x0048840D - CResourceEntity::DetachFromSpatial (vtable[0x10])
 *
 * Container/mobile detach: CItem_DetachFromSpatial plus Unequip (0x2D)
 * dispatch, decay-timer removal for valueless items, and clearing the
 * parent mobile's equipment slot when the item was equipped.
 */
void
CResourceEntity_DetachFromSpatial(CItem *item)
{
	int eqSlot;
	CItem *parent;
	uint32_t entitySerial;
	CContainer *parentCont;
	int blockIdx;

	eqSlot = CItem_AdjustParentWeight(item, item->parent);

	parent = item->parent;

	if (!CItem_IsDeleted(item) && parent != NULL) {
		if (CItem_CanFireEquipEvent(eqSlot) == 1) {
			entitySerial = item->serial;
			Entity_ExecuteEvent((CEntity *)item, 0x2D, parent->serial);
			CWorld_FindBySerial(g_World, entitySerial);
		}
	}

	if (CItem_IsValueless(item)) {
		CEntity_RemoveTimer(item, 8, 0);
	}

	CItem_NotifyMultiDetach(item, 0);
	CResourceEntity_NotifyPreModify(item);
	Block_RemoveTrackingNode(item);
	CItem_UpdateContainInfo(item, 0);

	if (eqSlot != -1) {
		parent = item->parent;
		((CMobile *)parent)->equipment[eqSlot] = NULL;
		item->parent = NULL;
		item->resourceEntity.entity.removedFromWorld = 1;
	} else {
		if (item->spatialNext != NULL)
			item->spatialNext->spatialPrev = item->spatialPrev;
		if (item->spatialPrev != NULL)
			item->spatialPrev->spatialNext = item->spatialNext;

		if (item->parent != NULL) {
			parentCont = (CContainer *)item->parent;
			if (parentCont->contents == item)
				parentCont->contents = item->spatialNext;
		} else {
			blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &item->resourceEntity.entity.location, 0);
			if (g_SpatialGrid.cells[blockIdx].itemHead == item)
				g_SpatialGrid.cells[blockIdx].itemHead = item->spatialNext;
		}

		item->spatialPrev = NULL;
		item->spatialNext = NULL;
		item->parent = NULL;
		item->resourceEntity.entity.removedFromWorld = 1;
	}
}

/*
 * 0x0048F1B0 - CResourceEntity::AddToTagList
 *
 * Appends (type, value) to the CList tag if not already present. Returns 1
 * when appended, 0 when it was already there.
 */
int
CResourceEntity_AddToTagList(CItem *entity, const char *name, int type, uintptr_t value)
{
	CList *list;
	TagNode *node;

	list = CResourceEntity_GetTagEntity(entity, name);
	if (list == NULL) {
		node = CEntity_SetObjVar(entity, name, 5, 0);
		list = (CList *)(uintptr_t)node->value;
	} else {
		if (CList_Find(list, type, value))
			return 0;
	}
	CList_Append(list, type, value);
	return 1;
}

/*
 * 0x0048F21F - CResourceEntity::IsInTagList
 *
 * Returns 1 if (type, value) is present in the CList tag.
 */
int
CResourceEntity_IsInTagList(CItem *entity, const char *name, int type, uintptr_t value)
{
	CList *list;

	list = CResourceEntity_GetTagEntity(entity, name);
	if (list == NULL)
		return 0;
	if (CList_Find(list, type, value))
		return 1;
	return 0;
}

/*
 * 0x0048F26A - CResourceEntity::IsInTagListStr (CString overload)
 *
 * Returns entity if the obj (type 4) is present in the list named by name.
 */
CItem *
CResourceEntity_IsInTagListStr(CItem *entity, CString *name, uint32_t value)
{
	const char *nameStr;

	nameStr = CString_GetBuffer(name);
	if (CResourceEntity_IsInTagList(entity, nameStr, 4, value))
		return entity;
	return NULL;
}

/*
 * 0x004912A0 - CResourceEntity::ResetTemplateIndex
 *
 * Sets templateIndex to 0xFFFF.
 */
void
CResourceEntity_ResetTemplateIndex(CItem *item)
{
	item->resourceEntity.templateIndex = 0xFFFF;
}

/*
 * 0x00491340 - CResourceEntity scalar/vector deleting destructor
 *
 * flags & 2 performs a vector delete (backwards); otherwise scalar delete.
 */
void *
CResourceEntity_ScalarDelete(CResourceEntity *self, int flags)
{
	if (flags & 2) {
		uint32_t count = *(uint32_t *)((char *)self - sizeof(uintptr_t));
		uint32_t i;
		CResourceEntity *arr = self;
		CResourceEntity *cur = (CResourceEntity *)((char *)arr + count * sizeof(CResourceEntity));
		for (i = 0; i < count; i++) {
			cur = (CResourceEntity *)((char *)cur - sizeof(CResourceEntity));
			CResourceEntity_Destructor(cur);
		}
		free((char *)self - sizeof(uintptr_t));
	} else {
		CResourceEntity_Destructor(self);
		if (flags & 1)
			free(self);
	}
	return NULL;
}

/*
 * 0x004913E0 - CResourceEntity::SetTemplateIndex
 *
 * Stores index into the entity's templateIndex field.
 */
void
CResourceEntity_SetTemplateIndex(CItem *item, uint16_t index)
{
	item->resourceEntity.templateIndex = index;
}

/*
 * 0x004A88BD - CResourceNode::CResourceNode
 *
 * Zero-initialises the node and sets type=3.
 */
static void
CResourceNode_Init(CResourceNode *node)
{
	memset(node, 0, sizeof(CResourceNode));
	node->id = 0;
	node->type = 3;
	node->next = NULL;
	node->prev = NULL;
}

/*
 * 0x004A88FB - CResourceNode_ConstructorWrapper
 *
 * Thiscall wrapper that forwards to CResourceNode_Init.
 */
static __attribute__((unused)) CResourceNode *
CResourceNode_ConstructorWrapper(CResourceNode *this)
{
	CResourceNode_Init(this);
	return this;
}

/*
 * 0x004A8911 - ResourceNode_AllocFromPool
 *
 * Pops a node from the free list, growing it by 4096 nodes when empty.
 */
CResourceNode *
ResourceNode_AllocFromPool(void)
{
	static int poolCreated;
	CResourceNode *node;
	int i;

	if (!poolCreated) {
		VG_CREATE_POOL(&g_ResNodeFreeList);
		poolCreated = 1;
	}

	if (g_ResNodeFreeList == NULL) {
		CResourceNode *batch = malloc(0x1000 * sizeof(CResourceNode));
		if (batch != NULL) {
			for (i = 0; i < 0x1000; i++) {
				batch[i].next = g_ResNodeFreeList;
				g_ResNodeFreeList = &batch[i];
			}
		}
	}
	node = g_ResNodeFreeList;
	VG_POOL_ALLOC(&g_ResNodeFreeList, node, sizeof(CResourceNode));
	VG_MAKE_DEFINED(&node->next, sizeof(node->next));
	g_ResNodeFreeList = node->next;
	CResourceNode_Init(node);
	return node;
}

/*
 * 0x004A8A22 - ResourceNode_ReturnToPool
 *
 * Pushes node onto the free list.
 */
void
ResourceNode_ReturnToPool(CResourceNode *node)
{
	node->next = g_ResNodeFreeList;
	g_ResNodeFreeList = node;
	VG_POOL_FREE(&g_ResNodeFreeList, node);
}

/*
 * 0x004A8A3B - CResourceNode::Noop
 *
 * Empty destructor referenced by the CResourceNode pool vtable and by
 * AddResourceNodesByCategory.
 */
static __attribute__((unused)) void
CResourceNode_Noop(CResourceNode *this)
{
	USED(this);
}

/*
 * 0x004A8A46 - CResourceNode::Save
 *
 * Writes a CResourceNode to itemres.mul as:
 *   value1(4) + value2(4) + value3(4) + type(1) + 16 bytes of zero padding
 *   + id as dword (or 0 if id==0), all ints big-endian.
 */
void
CResourceNode_Save(CResourceNode *this, FILE *fp)
{
	int zero = 0;
	int tmp;

	tmp = this->value1;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = this->value2;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	tmp = this->value3;
	SwapEndian(&tmp);
	fwrite_ServerSide(&tmp, 4, 1, fp);

	fwrite_ServerSide(this, 1, 1, fp);

	fwrite_ServerSide(&zero, 4, 1, fp);
	fwrite_ServerSide(&zero, 4, 1, fp);
	fwrite_ServerSide(&zero, 4, 1, fp);
	fwrite_ServerSide(&zero, 4, 1, fp);

	if (this->id != 0) {
		tmp = (int)(uint16_t)this->id;
		SwapEndian(&tmp);
		fwrite_ServerSide(&tmp, 4, 1, fp);
	} else {
		fwrite_ServerSide(&zero, 4, 1, fp);
	}
}

/*
 * 0x004CDCD4 - CResourceEntity::HasTag
 *
 * Forwards to TagList_HasTag. Type 7 (WTYPE_UNKNOWN) is a wildcard.
 */
int
CResourceEntity_HasTag(CItem *entity, const char *name, int type)
{
	if (entity->tagList == NULL)
		return 0;
	return TagList_HasTag(entity->tagList, name, type);
}

/*
 * 0x004CDD01 - CResourceEntity::GetTagInt
 *
 * Looks up the integer tag named name on entity, writing the value to
 * outVal. No-op when entity has no tag list.
 */
void
CResourceEntity_GetTagInt(CItem *entity, const char *name, int *outVal)
{
	if (entity->tagList != NULL)
		TagList_GetTagInt(entity->tagList, name, outVal);
}

/*
 * 0x004CDD2A - CResourceEntity::GetTagObj
 *
 * Thiscall wrapper: if entity has tagList, delegates to TagList_GetTagObj.
 */
void
CResourceEntity_GetTagObj(CItem *entity, const char *name, uint32_t *outVal)
{
	if (entity->tagList != NULL)
		TagList_GetTagObj(entity->tagList, name, outVal);
}

/*
 * 0x004CDD53 - CResourceEntity::GetTagLoc
 *
 * Thiscall wrapper: if entity has tagList, delegates to TagList_GetTagLoc.
 */
void
CResourceEntity_GetTagLoc(CItem *entity, const char *name, CLocation *outLoc)
{
	if (entity->tagList != NULL)
		TagList_GetTagLoc(entity->tagList, name, outLoc);
}

/*
 * 0x004CDD7C - CResourceEntity::GetTagString
 *
 * Returns tag string value by name, or NULL if no taglist.
 */
CString *
CResourceEntity_GetTagString(CItem *self, const char *name)
{
	if (self->tagList == NULL)
		return NULL;
	return TagList_GetTagString(self->tagList, name);
}

/*
 * 0x004CDDCE - CResourceEntity::GetTagEntity
 *
 * Returns the CList* for a type-5 (LIST) tag, or NULL.
 */
CList *
CResourceEntity_GetTagEntity(CItem *entity, const char *name)
{
	if (entity->tagList == NULL)
		return NULL;
	return CTagListManager_GetListEntry(entity->tagList, name);
}

/*
 * 0x004CDDF7 - CResourceEntity::RemoveScript
 *
 * Removes the named script and frees the tag-list manager when it is empty.
 */
void
CResourceEntity_RemoveScript(CItem *entity, const char *scriptName)
{
	if (entity->tagList == NULL)
		return;
	if (CTagListManager_RemoveScript(entity->tagList, scriptName) == 0) {
		CTagListManager_Destroy(entity->tagList);
		entity->tagList = NULL;
	}
}

/*
 * 0x004CDE35 - CResourceEntity::RemoveScriptNode
 *
 * Removes the script-attach node and frees the tag-list manager when empty.
 */
void
CResourceEntity_RemoveScriptNode(CItem *entity, ScriptAttachNode *node)
{
	if (entity->tagList == NULL)
		return;
	if (CTagListManager_RemoveScriptNode(entity->tagList, node) == 0) {
		CTagListManager_Destroy(entity->tagList);
		entity->tagList = NULL;
	}
}

/*
 * 0x004CDEAC - CResourceEntity::DetachScript
 *
 * Removes the tag by name and frees the tag-list manager when empty.
 */
void
CResourceEntity_DetachScript(CItem *entity, const char *name)
{
	if (entity->tagList == NULL)
		return;
	if (TagList_RemoveByName(entity->tagList, name) == 0) {
		CTagListManager_Destroy(entity->tagList);
		entity->tagList = NULL;
	}
}

/*
 * 0x004CDF4B - CResourceEntity::HasScriptClass
 *
 * Returns 1 if the tag-list manager has any node referring to scriptClassPtr.
 */
int
CResourceEntity_HasScriptClass(CItem *entity, void *scriptClassPtr)
{
	if (entity->tagList == NULL)
		return 0;
	return CTagListManager_HasScript(entity->tagList, scriptClassPtr);
}

/*
 * 0x004E04A0 - CResourceEntity::GetTemplateIndex
 *
 * Returns the entity's templateIndex.
 */
int
CResourceEntity_GetTemplateIndex(CItem *ent)
{
	return ent->resourceEntity.templateIndex;
}

/*
 * Custom - ResourceEntity_DestroyPools
 *
 * Server-shutdown cleanup. Walks the resource-node freelist
 * marking each node defined so valgrind can read its next pointer,
 * then ends pool tracking. The VG_* macros are no-ops when
 * VALGRIND is not defined.
 */
void
ResourceEntity_DestroyPools(void)
{
	CResourceNode *cur, *next;

	if (g_ResNodeFreeList == NULL)
		return;
	for (cur = g_ResNodeFreeList; cur != NULL; cur = next) {
		VG_MAKE_DEFINED(cur, sizeof(*cur));
		next = cur->next;
	}
	VG_DESTROY_POOL(&g_ResNodeFreeList);
}
