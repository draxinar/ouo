/*
 * Serial list - ordered (serial, flags) pairs used for group membership.
 *
 * Instantiates the std::list template over CSerialValue entries so a
 * CEntity can carry friend/foe tables, guild rosters, and trigger lists
 * without allocating per-entry nodes at call time. Specialized copy and
 * splice paths preserve the binary's exact list layout.
 */

#include <stdint.h>
#include <stdlib.h>

#include "dat.h"
#include "mobile.h"
#include "multi.h"
#include "region.h"
#include "vtable.h"
#include "world.h"

static void CSerialList_InsertBefore(CSerialList *list, CSerialNode *node); // 0x004429FA
static void CSerialValue_SetFlags(CSerialValue *self, int16_t flags); // 0x00446A50
static void *StdSerialList_InitCopy(StdPtrList *list, StdPtrList *src); // 0x00446AB0
static void *StdSerialList_Assign(StdPtrList *list, StdPtrList *src); // 0x00446B80
static CSerialValue *CSerialValue_CopyConstructor(CSerialValue *this, CSerialValue *src); // 0x00446C50
static void StdSerialList_Splice(StdPtrList *this, StdPtrNode *pos, StdPtrList *srcList, StdPtrNode *first, StdPtrNode *last); // 0x00446D70
static void StdSerialList_RemoveMatching(StdPtrList *list, void *value); // 0x00446DF0
static int CSerialValue_EqualBySerial(CSerialValue *this, CSerialValue *other); // 0x00446E70
static void *StdSerialList_Begin(StdPtrList *list, StdPtrNode **outIter); // 0x00446EA0
static void *StdSerialList_End2(StdPtrList *list, StdPtrNode **outIter); // 0x00446ED0
static void StdSerialList_Insert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x00446EF0
static void StdSerialList_InsertRange(StdPtrList *list, StdPtrNode *pos, StdPtrNode *first, StdPtrNode *last); // 0x00446FA0
static StdPtrNode *StdSerialList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint); // 0x00447040
static void StdSerialList_SpliceOrInsert(StdPtrList *this, StdPtrNode *pos, StdPtrList *srcList, StdPtrNode *first, StdPtrNode *last); // 0x004470B0
static void StdSerialList_DestroyWrapper(void *list, void *element); // 0x00447210
static void StdSerialList_ConstructorWrapper(void *list, void *dst, void *src); // 0x00447230
static void *StdSerialList_SetIter(void *list, void *node); // 0x00447250
static StdPtrNode **StdSerialList_Find(StdPtrNode **result, StdPtrNode *begin, StdPtrNode *end, CSearchCtx *ctx); // 0x00447270

/*
 * 0x00442960 - CSerialNode::DecrementTTL
 *
 * Thiscall on CSerialNode value. If flags (TTL) is zero, returns 0.
 * Otherwise decrements flags by 1 and returns 1.
 */
int
CSerialNode_DecrementTTL(CSerialNode *node)
{
	if (node->flags == 0)
		return 0;
	node->flags -= 1;
	return 1;
}

/*
 * 0x00442993 - CSerialList::CSerialList
 *
 * Thiscall constructor. Calls _Init to allocate the sentinel and zero
 * the count.
 *
 * MODIFIED: the binary hands _Init the address of a four-byte stack slot
 * it never writes, and _Init copies that slot's first byte into the
 * list's allocator field. The slot is zeroed here so the build stays free
 * of -Wuninitialized; nothing reads the field back.
 */
void
CSerialList_Constructor(CSerialList *list)
{
	uint32_t allocByte = 0;

	CSerialList_Init(list, &allocByte);
}

/*
 * 0x004429E7 - CSerialList::~CSerialList
 *
 * Frees every node and the sentinel, then resets data/count.
 */
void
CSerialList_Destructor(CSerialList *list)
{
	CSerialNode *sentinel, *node, *next;

	sentinel = list->data;
	for (node = sentinel->next; node != sentinel; node = next) {
		next = node->next;
		free(node);
	}
	free(sentinel);
	list->data = NULL;
	list->count = 0;
}

/*
 * 0x004429FA - CSerialList::InsertBefore
 *
 * Refreshes a combat-list entry's TTL by setting flags back to 0x78, then
 * splices the entry in front of begin. The splice range is [node, node),
 * which is empty, so it does nothing; the binary issues the call anyway.
 */
static void
CSerialList_InsertBefore(CSerialList *list, CSerialNode *node)
{
	StdPtrNode *beginIter;

	CSerialValue_SetFlags((CSerialValue *)StdPtrIter_Deref((StdPtrNode **)&node), 0x78);
	StdSerialList_Splice((StdPtrList *)list, *StdPtrList_Begin((StdPtrList *)list, &beginIter), (StdPtrList *)list, (StdPtrNode *)node, (StdPtrNode *)node);
}

/*
 * 0x00442A3D - CSerialList::AddFromSet
 *
 * Refreshes the TTL of an existing serial in the list. Does not
 * insert anything new when the serial is absent.
 */
void
CSerialList_AddFromSet(CSerialList *list, uint32_t serial)
{
	CSearchCtx key;
	StdPtrNode *beginIter, *endIter, *tmpIter;
	StdPtrNode *begin, *end;
	StdPtrNode *result;

	CMultiRotateRect_SetRotation((CMultiRotateRect *)&key, serial);
	end = *StdPtrList_End((StdPtrList *)list, &endIter);
	begin = *StdPtrList_Begin((StdPtrList *)list, &beginIter);
	StdSerialList_Find(&result, begin, end, &key);

	if (StdPtrIter_Neq(&result, StdPtrList_End((StdPtrList *)list, &tmpIter)) & 0xFF)
		CSerialList_InsertBefore(list, (CSerialNode *)result);
}

/*
 * 0x00442AAF - CSerialList::Add
 *
 * Adds serial with TTL 0x78 to the list. If the serial is already
 * present, refreshes its TTL and returns 0; otherwise pushes a new
 * entry and returns 1.
 */
int
CSerialList_Add(CSerialList *list, uint32_t serial)
{
	CSearchCtx key;
	StdPtrNode *beginIter, *endIter, *tmpIter;
	StdPtrNode *begin, *end;
	StdPtrNode *result;
	CSerialValue val;

	CMultiRotateRect_SetRotation((CMultiRotateRect *)&key, serial);
	end = *StdPtrList_End((StdPtrList *)list, &endIter);
	begin = *StdPtrList_Begin((StdPtrList *)list, &beginIter);
	StdSerialList_Find(&result, begin, end, &key);

	if (StdPtrIter_Neq(&result, StdPtrList_End((StdPtrList *)list, &tmpIter)) & 0xFF) {
		CSerialList_InsertBefore(list, (CSerialNode *)result);
		return 0;
	}

	CSerialValue_Init(&val, serial, 0x78);
	StdSerialList_PushBack((StdPtrList *)list, &val);
	return 1;
}

/*
 * 0x00442B82 - check if a serial exists in the list
 *
 * Linear scan returning 1 when serial appears in the list.
 */
int
CSerialList_Contains(CSerialList *list, uint32_t serial)
{
	CSearchCtx key;
	StdPtrNode *beginIter, *endIter, *tmpIter;
	StdPtrNode *begin, *end;
	StdPtrNode *found, *it, *endIt;

	CMultiRotateRect_SetRotation((CMultiRotateRect *)&key, serial);
	end = *StdPtrList_End((StdPtrList *)list, &endIter);
	begin = *StdPtrList_Begin((StdPtrList *)list, &beginIter);
	StdPtrIter_CopyConstructor(&it, StdSerialList_Find(&found, begin, end, &key));
	StdPtrIter_CopyConstructor(&endIt, StdPtrList_End((StdPtrList *)list, &tmpIter));

	return (StdPtrIter_Neq(&it, &endIt) & 0xFF) ? 1 : 0;
}

/*
 * 0x00442C06 - remove a serial from the list
 *
 * Builds a key value and erases every entry matching it. The binary sets
 * no return value, so this is void; no caller reads one.
 */
void
CSerialList_Remove(CSerialList *list, uint32_t serial)
{
	CSerialValue val;

	CSerialValue_Init(&val, serial, 0);
	StdSerialList_RemoveMatching((StdPtrList *)list, &val);
}

/*
 * 0x00442C64 - CSerialList::PruneExpired
 *
 * Decrements the TTL of every entry; entries that hit zero are erased.
 * The iterator advances past the entry before the erase, so the walk
 * survives the node being destroyed. Drives combat-list aging.
 */
void
CSerialList_PruneExpired(CSerialList *list)
{
	StdPtrList *l = (StdPtrList *)list;
	StdPtrNode *iter, *endTemp, *postIncTemp, *eraseResult;

	StdPtrList_Begin(l, &iter);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(l, &endTemp)) & 0xFF))
			break;
		// The binary derefs first and hands DecrementTTL the value at
		// node+8; our CSerialNode merges node and value, so the TTL sits
		// at +0x0C from the node itself and the node is what we pass.
		if (CSerialNode_DecrementTTL((CSerialNode *)iter)) {
			StdPtrIter_PostInc(&iter, &postIncTemp, 0);
		} else {
			StdSerialList_Erase(l, &eraseResult, *StdPtrIter_PostInc(&iter, &postIncTemp, 0));
		}
	}
}

/*
 * 0x00442CDD - CSerialList::FindFirstValidTarget
 *
 * Finds the first list entry that resolves to a living, visible
 * mobile in the world. Stores end() into outIter when none qualify.
 */
void
CSerialList_FindFirstValidTarget(StdPtrList *list, StdPtrNode **outIter)
{
	StdPtrNode *iter, *beginTemp, *endTemp, *postIncTemp;
	CItem *ent;

	StdPtrIter_BaseConstructor(&iter);
	iter = *StdPtrList_Begin(list, &beginTemp);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(list, &endTemp)) & 0xFF))
			break;
		ent = CWorld_FindBySerial(g_World, (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&iter)));
		if (ent == NULL)
			goto next;
		if (!VT_IsMobile(ent))
			goto next;
		if (ent->resourceEntity.entity.removedFromWorld)
			goto next;
		if (VT_IsDead(ent))
			goto next;
		if (VT_IsHidden(ent))
			goto next;
		break;
next:
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
	*outIter = iter;
}

/*
 * 0x00442DB0 - CSerialList::FindFirstValidMobile
 *
 * Like FindFirstValidTarget but accepts hidden mobiles. Stores
 * end() into outIter when no live mobile is found.
 */
void
CSerialList_FindFirstValidMobile(StdPtrList *list, StdPtrNode **outIter)
{
	StdPtrNode *iter, *beginTemp, *endTemp, *postIncTemp;
	CItem *ent;

	StdPtrIter_BaseConstructor(&iter);
	iter = *StdPtrList_Begin(list, &beginTemp);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(list, &endTemp)) & 0xFF))
			break;
		ent = CWorld_FindBySerial(g_World, (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&iter)));
		if (ent == NULL)
			goto next;
		if (!VT_IsMobile(ent))
			goto next;
		if (ent->resourceEntity.entity.removedFromWorld)
			goto next;
		if (VT_IsDead(ent))
			goto next;
		break;
next:
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
	*outIter = iter;
}

/*
 * 0x00442E71 - CSerialList::FindNearestMobile
 *
 * Picks the live mobile closest to loc by wrapped Chebyshev distance.
 * Stores end() into outIter when no candidate qualifies.
 */
void
CSerialList_FindNearestMobile(StdPtrList *list, StdPtrNode **outIter, CLocation *loc)
{
	StdPtrNode *iter, *beginTemp, *endTemp, *postIncTemp;
	StdPtrNode *bestIter;
	int bestDist;
	CItem *ent;
	CMobile *mob;
	CLocation *entLoc;
	int dist;

	bestDist = 0xFFFF;
	StdPtrList_End(list, &bestIter);
	StdPtrIter_BaseConstructor(&iter);
	iter = *StdPtrList_Begin(list, &beginTemp);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(list, &endTemp)) & 0xFF))
			break;
		ent = CWorld_FindBySerial(g_World, (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&iter)));
		if (ent == NULL)
			goto next;
		if (!VT_IsMobile(ent))
			goto next;
		mob = (CMobile *)ent;
		USED(mob);
		if (ent->resourceEntity.entity.removedFromWorld)
			goto next;
		if (VT_IsDead(ent))
			goto next;
		entLoc = ((CLocation * (*)(void *)) VT_FN(ent, VT_GET_LOCATION))(ent);
		dist = Location_WrappedChebyshevDistance(entLoc, loc);
		if (dist < bestDist) {
			bestDist = dist;
			bestIter = iter;
		}
next:
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
	*outIter = bestIter;
}

/*
 * 0x00442F79 - CSerialList::FindNearestTarget
 *
 * Like FindNearestMobile, but also rejects hidden mobiles.
 */
void
CSerialList_FindNearestTarget(StdPtrList *list, StdPtrNode **outIter, CLocation *loc)
{
	StdPtrNode *iter, *beginTemp, *endTemp, *postIncTemp;
	StdPtrNode *bestIter;
	int bestDist;
	CItem *ent;
	CMobile *mob;
	CLocation *entLoc;
	int dist;

	bestDist = 0xFFFF;
	StdPtrList_End(list, &bestIter);
	StdPtrIter_BaseConstructor(&iter);
	iter = *StdPtrList_Begin(list, &beginTemp);
	for (;;) {
		if (!(StdPtrIter_Neq(&iter, StdPtrList_End(list, &endTemp)) & 0xFF))
			break;
		ent = CWorld_FindBySerial(g_World, (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&iter)));
		if (ent == NULL)
			goto next;
		if (!VT_IsMobile(ent))
			goto next;
		mob = (CMobile *)ent;
		USED(mob);
		if (ent->resourceEntity.entity.removedFromWorld)
			goto next;
		if (VT_IsDead(ent))
			goto next;
		if (VT_IsHidden(ent))
			goto next;
		entLoc = ((CLocation * (*)(void *)) VT_FN(ent, VT_GET_LOCATION))(ent);
		dist = Location_WrappedChebyshevDistance(entLoc, loc);
		if (dist < bestDist) {
			bestDist = dist;
			bestIter = iter;
		}
next:
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
	*outIter = bestIter;
}

/*
 * 0x00446A20 - CSerialValue::CSerialValue
 *
 * Stores serial and flags into the value pair.
 */
CSerialValue *
CSerialValue_Init(CSerialValue *self, uint32_t serial, int16_t flags)
{
	self->serial = serial;
	self->flags = flags;
	return self;
}

/*
 * 0x00446A50 - CSerialValue::SetFlags
 *
 * Updates the value's flags word.
 */
static __attribute__((unused)) void
CSerialValue_SetFlags(CSerialValue *self, int16_t flags)
{
	self->flags = flags;
}

/*
 * 0x00446A70 - std::list<CSerialValue>::_Init
 *
 * Copies the allocator byte out of alloc, buys the self-referencing
 * sentinel node and clears the count.
 */
CSerialList *
CSerialList_Init(CSerialList *list, void *alloc)
{
	list->type = *(uint8_t *)alloc;
	list->data = (CSerialNode *)StdSerialList_Buynode((StdPtrList *)list, NULL, NULL);
	list->count = 0;
	return list;
}

/*
 * 0x00446AB0 - std::list<CSerialValue>::_Init (copy)
 *
 * Initializes this list as a copy of src: stamps the first byte from src,
 * allocates the sentinel via _Buynode, then InsertRange-copies every
 * element from src.
 */
static __attribute__((unused)) void *
StdSerialList_InitCopy(StdPtrList *list, StdPtrList *src)
{
	StdPtrNode *srcEnd, *srcBegin, *thisBegin;

	*(uint8_t *)list = *(uint8_t *)src;
	list->head = StdSerialList_Buynode(list, NULL, NULL);
	list->size = 0;
	StdSerialList_End2(src, &srcEnd);
	StdSerialList_Begin(src, &srcBegin);
	StdPtrList_Begin(list, &thisBegin);
	StdSerialList_InsertRange(list, thisBegin, srcBegin, srcEnd);
	return list;
}

/*
 * 0x00446B80 - std::list<CSerialValue>::operator=
 *
 * Copy-assigns src into this. Walks both lists in parallel copying values,
 * then erases any extra trailing elements in this or inserts the leftover
 * elements from src.
 */
static __attribute__((unused)) void *
StdSerialList_Assign(StdPtrList *list, StdPtrList *src)
{
	StdPtrNode *thisIter, *thisEnd, *srcIter, *srcEnd;
	StdPtrNode *eraseResult;

	if (list == src)
		return list;

	StdPtrList_Begin(list, &thisIter);
	StdPtrList_End(list, &thisEnd);
	StdSerialList_Begin(src, &srcIter);
	StdSerialList_End2(src, &srcEnd);

	while ((StdPtrIter_Neq(&thisIter, &thisEnd) & 0xFF) && (StdPtrIter_Neq(&srcIter, &srcEnd) & 0xFF)) {
		CSerialValue_CopyConstructor((CSerialValue *)StdPtrIter_Deref(&thisIter), (CSerialValue *)StdPtrIter_Deref(&srcIter));
		StdPtrIter_Inc(&thisIter);
		StdPtrIter_Inc(&srcIter);
	}

	StdSerialList_EraseRange(list, &eraseResult, thisIter, thisEnd);
	StdSerialList_InsertRange(list, thisEnd, srcIter, srcEnd);
	return list;
}

/*
 * 0x00446C50 - CSerialValue::CSerialValue (copy constructor)
 *
 * Copies the serial and flags fields from src into this.
 */
static CSerialValue *
CSerialValue_CopyConstructor(CSerialValue *this, CSerialValue *src)
{
	this->serial = src->serial;
	this->flags = src->flags;
	return this;
}

/*
 * 0x00446C80 - std::list<CSerialValue>::push_back
 *
 * Inserts value just before the sentinel.
 */
void
StdSerialList_PushBack(StdPtrList *list, void *value)
{
	StdPtrNode *endIter;
	StdPtrNode *result;

	StdPtrList_End(list, &endIter);
	StdSerialList_Insert(list, &result, endIter, value);
}

/*
 * 0x00446CB0 - std::list<CSerialValue>::erase
 *
 * Post-increments the iterator, unlinks pos, runs the destroy callback on
 * its value, frees the node, and decrements size.
 */
void
StdSerialList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos)
{
	StdPtrNode *nextNode;

	nextNode = pos->next;
	pos->prev->next = pos->next;
	pos->next->prev = pos->prev;
	StdSerialList_DestroyWrapper(list, StdPtrNode_GetValue(pos));
	free(pos);
	list->size--;
	*result = nextNode;
}

/*
 * 0x00446D70 - std::list<CSerialValue>::splice
 *
 * Splices [first, last) from srcList in front of pos. When the range is
 * non-empty and the lists differ, sizes are adjusted from a distance count
 * before delegating to the in-place relink.
 */
static __attribute__((unused)) void
StdSerialList_Splice(StdPtrList *this, StdPtrNode *pos, StdPtrList *srcList, StdPtrNode *first, StdPtrNode *last)
{
	int count;

	if (!(StdPtrIter_Neq(&first, &last) & 0xFF))
		return;

	if (srcList != this) {
		count = 0;
		StdSerialList_Distance(first, last, &count);
		this->size = this->size + count;
		srcList->size = srcList->size - count;
	}

	StdSerialList_SpliceOrInsert(this, pos, srcList, first, last);
}

/*
 * 0x00446DF0 - std::list<CSerialValue>::remove
 *
 * Erases every list element whose serial matches *value.
 */
static void
StdSerialList_RemoveMatching(StdPtrList *list, void *value)
{
	StdPtrNode *endIter;
	StdPtrNode *curIter;
	StdPtrNode *postIncResult;
	StdPtrNode *eraseResult;

	StdPtrList_End(list, &endIter);
	StdPtrList_Begin(list, &curIter);

	while (StdPtrIter_Neq(&curIter, &endIter) & 0xFF) {
		void *elemVal = StdPtrIter_Deref(&curIter);
		if (CSerialValue_EqualBySerial(elemVal, value)) {
			StdPtrIter_PostInc(&curIter, &postIncResult, 0);
			StdSerialList_Erase(list, &eraseResult, *(StdPtrNode **)&postIncResult);
		} else {
			StdPtrIter_Inc(&curIter);
		}
	}
}

/*
 * 0x00446E70 - CSerialValue::EqualBySerial
 *
 * Returns 1 when this->serial equals other->serial.
 */
static int
CSerialValue_EqualBySerial(CSerialValue *this, CSerialValue *other)
{
	return (other->serial == this->serial) ? 1 : 0;
}

/*
 * 0x00446EA0 - std::list<CSerialValue>::begin
 *
 * Stores head->next into *outIter and returns it.
 */
static void *
StdSerialList_Begin(StdPtrList *list, StdPtrNode **outIter)
{
	StdPtrNode *headNode;

	headNode = list->head;
	StdPtrNode *ref = StdPtrNode_GetNext(headNode);
	StdSerialList_SetIter(outIter, ref->next);
	return outIter;
}

/*
 * 0x00446ED0 - std::list<CSerialValue>::end
 *
 * Stores the sentinel head into *outIter and returns it.
 */
static void *
StdSerialList_End2(StdPtrList *list, StdPtrNode **outIter)
{
	StdPtrNode *headNode;

	headNode = list->head;
	StdSerialList_SetIter(outIter, headNode);
	return outIter;
}

/*
 * 0x00446EF0 - std::list<CSerialValue>::_Insert
 *
 * Allocates a new node via _Buynode, links it before pos, copy-constructs
 * value into it, bumps size, and stores the iterator into *result.
 */
static void
StdSerialList_Insert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *node;
	StdPtrNode *newNode;

	node = pos;

	newNode = StdSerialList_Buynode(list, node, *StdPtrNode_GetPrev(node));
	*StdPtrNode_GetPrev(node) = newNode;
	node = *StdPtrNode_GetPrev(node);
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;

	StdSerialList_ConstructorWrapper(list, StdPtrNode_GetValue(node), value);

	list->size = list->size + 1;
	CIterCtx_Set(result, node);
}

/*
 * 0x00446FA0 - std::list<CSerialValue>::_Insert_range
 *
 * Inserts each element in [first, last) before pos.
 */
static void
StdSerialList_InsertRange(StdPtrList *list, StdPtrNode *pos, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *result;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		void *val = StdPtrIter_Deref(&first);
		StdSerialList_Insert(list, &result, pos, val);
		StdPtrIter_Inc(&first);
	}
}

/*
 * 0x00446FF0 - std::list<CSerialValue>::erase range
 *
 * Erases every element in [first, last). Stores the resulting iterator into
 * *result.
 */
void
StdSerialList_EraseRange(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncResult;
	StdPtrNode *eraseResult;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc(&first, &postIncResult, 0);
		StdSerialList_Erase(list, &eraseResult, *(StdPtrNode **)&postIncResult);
	}
	*result = first;
}

/*
 * 0x00447040 - std::list<CSerialValue>::_Buynode
 *
 * Allocates a CSerialNode and seeds its next/prev from the supplied hints
 * (self-referencing when NULL).
 */
static StdPtrNode *
StdSerialList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint)
{
	StdPtrNode *newNode;
	StdPtrNode *nextVal;
	StdPtrNode *prevVal;

	newNode = (StdPtrNode *)StdPtrList_Charalloc(list, sizeof(CSerialNode));

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
 * 0x004470B0 - std::list<CSerialValue>::_Splice
 *
 * Moves the range [first, last) before pos. When this and srcList
 * share an allocator (always, in this binary) the move is done in
 * place by relinking node pointers; otherwise the range is copy-
 * inserted into this and erased from srcList.
 */
static void
StdSerialList_SpliceOrInsert(StdPtrList *this, StdPtrNode *pos, StdPtrList *srcList, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *savedPrev;

	if (StdAllocator_Equal()) {
		// In-place splice: move nodes [first, last) before pos.
		// last->prev->next = pos
		StdPtrNode_GetNext(*StdPtrNode_GetPrev(last))->next = pos;
		// first->prev->next = last (detach range tail from source)
		StdPtrNode_GetNext(*StdPtrNode_GetPrev(first))->next = last;
		// pos->prev->next = first (attach range head at dest)
		StdPtrNode_GetNext(*StdPtrNode_GetPrev(pos))->next = first;

		// Swap prev pointers
		savedPrev = *StdPtrNode_GetPrev(pos);
		*StdPtrNode_GetPrev(pos) = *StdPtrNode_GetPrev(last);
		*StdPtrNode_GetPrev(last) = *StdPtrNode_GetPrev(first);
		*StdPtrNode_GetPrev(first) = savedPrev;
	} else {
		// Different allocator: copy-insert then erase source
		StdPtrNode *eraseResult;
		StdSerialList_InsertRange(this, pos, first, last);
		StdSerialList_EraseRange(srcList, &eraseResult, first, last);
	}
}

/*
 * 0x00447210 - std::list<CSerialValue>::_Destroy_wrapper
 *
 * Erase destroy callback that runs the value-type destructor.
 */
static void
StdSerialList_DestroyWrapper(void *list, void *element)
{
	USED(list);
	StdSerialValue_DestructorWrapper(element);
}

/*
 * 0x00447230 - std::list<CSerialValue>::_Construct_wrapper
 *
 * Insert construct callback: copy-constructs a CSerialValue at dst from src.
 */
static void
StdSerialList_ConstructorWrapper(void *list, void *dst, void *src)
{
	USED(list);
	StdSerialValue_Constructor(dst, src);
}

/*
 * 0x00447250 - std::list<CSerialValue>::_Set_iter
 *
 * Stores node into the iterator context list and returns it.
 */
static void *
StdSerialList_SetIter(void *list, void *node)
{
	return CIterCtx_Set(list, node);
}

/*
 * 0x00447270 - std::list<CSerialValue>::find
 *
 * Stores into *result the first iterator in [begin, end) whose value matches
 * ctx (via StdSerialValue_Equal), or end when there is no match.
 */
static StdPtrNode **
StdSerialList_Find(StdPtrNode **result, StdPtrNode *begin, StdPtrNode *end, CSearchCtx *ctx)
{
	while (StdPtrIter_Neq(&begin, &end) & 0xFF) {
		void *elem = StdPtrIter_Deref(&begin);
		if (StdSerialValue_Equal((uint32_t *)elem, ctx) & 0xFF)
			break;
		StdPtrIter_Inc(&begin);
	}
	*result = begin;
	return result;
}

/*
 * 0x004472C0 - std::list<CSerialValue>::_Equal_to
 *
 * Returns 1 when *self equals the serial extracted from ctx via
 * CSearchCtx_Find.
 */
int
StdSerialValue_Equal(uint32_t *self, CSearchCtx *ctx)
{
	uint32_t val;

	val = CSearchCtx_Find(ctx);
	return (val == *self) ? 1 : 0;
}

/*
 * 0x004472F0 - std::list<CSerialValue>::_Distance
 *
 * Counts the number of elements in [begin, end) into *countOut.
 * MSVC STL _Distance template for list iterators.
 */
void
StdSerialList_Distance(StdPtrNode *beginIter, StdPtrNode *endIter, int *countOut)
{
	uint8_t initByte;

	StdSerialValue_ValInit(&initByte);
	StdSerialIter_CountRange(beginIter, endIter, countOut);
}

/*
 * 0x00447320 - std::list<CSerialValue>::node_Destructor_wrapper
 *
 * Erase callback that runs the value destructor without freeing
 * the node (the caller frees the node memory itself).
 */
void *
StdSerialValue_DestructorWrapper(void *node)
{
	return StdSerialValue_ScalarDelete((CSerialValue *)node, 0);
}

/*
 * 0x00447330 - std::list<CSerialValue>::_Constructor
 *
 * Placement-new wrapper that allocates 8 bytes at dst and copy-constructs
 * a CSerialValue from src into it.
 */
void *
StdSerialValue_Constructor(void *dst, void *src)
{
	void *ptr;

	ptr = (void *)StdKfn_Identity(8, (uintptr_t)dst);
	if (ptr == NULL)
		return NULL;
	return StdSerialValue_CopyConstructor((CSerialValue *)ptr, (CSerialValue *)src);
}

/*
 * 0x00447370 - std::list<CSerialValue>::copy_Constructor
 *
 * Copy-constructs *self from *src by forwarding to the value-type
 * constructor.
 */
void *
StdSerialValue_CopyConstructor(CSerialValue *self, CSerialValue *src)
{
	CSerialValue_CopyConstructor(self, src);
	return self;
}

/*
 * 0x00447390 - std::list<CSerialValue>::scalar_deleting_destructor
 *
 * The value type has a trivial destructor; frees the node when flags&1.
 */
void *
StdSerialValue_ScalarDelete(CSerialValue *self, int flags)
{
	// No-op: value type has trivial destructor.

	if (flags & 1)
		OperatorDelete(self);
	return NULL;
}

/*
 * 0x004473C0 - std::list<CSerialValue>::_Val_init
 *
 * Distance counter init helper. The binary stores the low byte of a stack
 * address (effectively garbage) into *dst; the value gets overwritten by
 * the counting loop, so we just write 0.
 */
void *
StdSerialValue_ValInit(void *dst)
{
	// In practice, this initializes the distance counter.
	// The actual value is irrelevant since the counter loop
	// starts fresh. Storing 0 matches the intended semantics.
	*(uint8_t *)dst = 0;
	return dst;
}

/*
 * 0x004473E0 - std::list<CSerialValue>::_Distance_loop
 *
 * Counts the elements in [begin, end) into *countOut.
 */
void
StdSerialIter_CountRange(StdPtrNode *beginIter, StdPtrNode *endIter, int *countOut)
{
	while (StdPtrIter_Neq(&beginIter, &endIter) & 0xFF) {
		*countOut = *countOut + 1;
		StdPtrIter_Inc(&beginIter);
	}
}
