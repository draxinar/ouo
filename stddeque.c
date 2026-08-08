/*
 * std::deque arena - time-event queue backing store and iterator helpers.
 *
 * Page-aligned arena blocks carve fixed-size slots for CTimeEvent entries
 * so the scheduler can enqueue from any thread without touching the CRT
 * heap. Matches the MSVC deque layout the binary used for its pending-task
 * queue.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"
#include "io.h"
#include "region.h"
#include "stddeque.h"
#include "terrain.h"
#include "wombat.h"

/*
 * Deque arena-page header (0x10 bytes on 32-bit) tracking both the raw and
 * aligned base pointers so the page can be freed through the original malloc.
 */
typedef struct CDequeBlock {
	void *aligned; // +0x00
	void *raw;     // +0x04
	uint32_t size; // +0x08
	uint32_t used; // +0x0C
} CDequeBlock;

static void *CDequeBlock_AllocFrom(CDequeBlock *this, uint32_t size); // 0x0046C91F
static uint32_t CDequeBlock_GetRemaining(CDequeBlock *this); // 0x0046C908
static CDequeBlock *CDequeBlock_Alloc(CDequeBlock *this, uint32_t size); // 0x0046C890
static void StdDeque_Clear(StdDeque_TimeEvent *self); // 0x004D8FE0
static void StdDeque_Buyback(StdDeque_TimeEvent *self); // 0x004D9250
static void StdDeque_PopFront(StdDeque_TimeEvent *self); // 0x004D9430
static void StdDequeIter_Inc(StdDequeIter *self); // 0x004D9500
static int StdDequeIter_Equal(StdDequeIter *self, StdDequeIter *other); // 0x004D9570
static void StdDeque_Freeback(StdDeque_TimeEvent *self); // 0x004D9590
static void StdDeque_DestroyBack(StdDeque_TimeEvent *self, uint8_t **entry); // 0x004D95C0
static void StdDeque_AllocPages(StdDeque_TimeEvent *self); // 0x004D95E0
static uint8_t **StdDeque_Growmap(StdDeque_TimeEvent *self, uint32_t newSize); // 0x004D9610
static void StdDeque_MapStore(StdDeque_TimeEvent *self, uint8_t **dest, uint8_t *value); // 0x004D9690
static uint8_t *StdDeque_AllocPage(StdDeque_TimeEvent *self, int count, int hint); // 0x004D96B0
static StdDequeIter *StdDequeIter_InitFromBlock(StdDequeIter *self, uint8_t *curPos, uint8_t **mapEntry); // 0x004D96D0
static void StdDeque_Dealloc(StdDeque_TimeEvent *self, void *ptr, uint32_t count);

// Deque constants matching MSVC std::deque<TimeEvent>
#define DEQUE_ELEM_SIZE  sizeof(TimeEvent) // 12 on 32-bit, 16 on 64-bit
#define DEQUE_PAGE_ELEMS (4096 / DEQUE_ELEM_SIZE)
#define DEQUE_PAGE_BYTES (DEQUE_PAGE_ELEMS * DEQUE_ELEM_SIZE)

// 0x006F0518
StdDeque_TimeEvent g_eventRingBuffer;

/*
 * 0x00404660 - std::deque<TimeEvent>::_Unget
 *
 * Walks backward through buf calling ungetc for each character.
 * Returns 1 if all characters were ungotten, 0 otherwise.
 */
static __attribute__((unused)) int
CDeque_Unget(char *buf, FILE *stream, unsigned int count)
{
	char *ptr;

	ptr = buf;
	ptr += count;
	while (count > 0) {
		ptr--;
		if (ungetc((unsigned char)*ptr, stream) == -1)
			break;
		count--;
	}
	if (count == 0)
		return 1;
	// dead recovery loop (count is always >= 1 here)
	while (count < 1) {
		fgetc_ServerSide(stream);
		count++;
	}
	return 0;
}

/*
 * 0x0046C796 - ArenaAllocator_Alloc
 *
 * Allocates size bytes from the arena page pool. If the last page
 * has enough remaining space, bump-allocates from it. Otherwise
 * allocates a new page (minimum 0x8000 bytes, 8KB-aligned) and
 * appends it to the global arena page vector.
 */
void *
ArenaAllocator_Alloc(uint32_t size, void **outPtr)
{
	uint32_t count;
	uint32_t pageSize;
	CDequeBlock *page;
	CDequeBlock *newPage;
	void *result;

	count = CVector_GetCount(&g_arenaPageVec);

	if (count != 0) {
		page = (CDequeBlock *)((uintptr_t *)g_arenaPageVec.begin)[count - 1];
		if (CDequeBlock_GetRemaining(page) >= size)
			goto alloc_from_page;
	}

	pageSize = 0x8000;
	if (size > 0x8000)
		pageSize = (size + 0x1FFF) & 0xFFFFE000;

	newPage = (CDequeBlock *)OperatorNew(sizeof(CDequeBlock));
	if (newPage != NULL)
		page = CDequeBlock_Alloc(newPage, pageSize);
	else
		page = NULL;

	CVector_PushBack(&g_arenaPageVec, (uintptr_t)page);

alloc_from_page:
	result = CDequeBlock_AllocFrom(page, size);
	*outPtr = result;
	return page;
}

/*
 * 0x0046C890 - CDeque block allocator
 *
 * Allocates a deque block with 0x2000-boundary alignment.
 */
static CDequeBlock *
CDequeBlock_Alloc(CDequeBlock *this, uint32_t size)
{
	void *raw;

	this->used = 0;
	raw = malloc(size + 0x3FFF);
	this->raw = raw;
	this->aligned = (void *)(((uintptr_t)raw + 0x1FFF) & ~(uintptr_t)0x1FFF);
	this->size = size;
	return this;
}

/*
 * 0x0046C908 - CDeque get remaining
 *
 * Returns size - used.
 */
static uint32_t
CDequeBlock_GetRemaining(CDequeBlock *this)
{
	return this->size - this->used;
}

/*
 * 0x0046C91F - CDeque alloc from block
 *
 * Returns pointer at the block's current position, then advances
 * used by size rounded up to a pointer boundary.
 *
 * The binary rounds to 4, its pointer width. Rounding to sizeof(void *)
 * keeps the bump pointer-aligned on 64-bit, where callers such as
 * CScriptManager::InternString store a pointer at the start of the block
 * they receive; on 32-bit the expression is the binary's (size + 3) & ~3.
 */
static void *
CDequeBlock_AllocFrom(CDequeBlock *this, uint32_t size)
{
	void *ptr;

	ptr = (void *)((char *)this->aligned + this->used);
	this->used += (size + (sizeof(void *) - 1)) & ~(uint32_t)(sizeof(void *) - 1);
	return ptr;
}

/*
 * 0x0046C9E0 - StdDeque_Dealloc
 *
 * Deallocator wrapper for std::allocator::deallocate. Calls operator delete
 * on ptr; count is ignored.
 */
static void
StdDeque_Dealloc(StdDeque_TimeEvent *self, void *ptr, uint32_t count)
{
	USED(self);
	USED(count);
	OperatorDelete(ptr);
}

/*
 * 0x0046C9E0 - StdDeque_Dealloc (SurfaceInfo allocator variant)
 *
 * Allocator deallocate - ignores self and count, frees ptr.
 */
void
StdDeque_DeallocSI(CVector *self, void *ptr, int count)
{
	USED(self);
	USED(count);
	OperatorDelete(ptr);
}

/*
 * 0x004D8F60 - TimeEvent::TimeEvent
 *
 * Constructs a TimeEvent with serial, eventType, and param.
 */
TimeEvent *
TimeEvent_Constructor(TimeEvent *self, uint32_t serial, uint32_t eventType, uintptr_t param)
{
	self->serial = serial;
	self->eventType = eventType;
	self->param = param;
	return self;
}

/*
 * 0x004D8F90 - std::deque<TimeEvent>::deque constructor
 *
 * Copies allocator byte, zero-initializes iterators and size fields.
 */
void
StdDeque_Constructor(StdDeque_TimeEvent *self, uint8_t *allocSrc)
{
	self->allocByte = *allocSrc;
	StdDequeIter_InitZero(&self->front);
	StdDequeIter_InitZero(&self->back);
	self->map = NULL;
	self->mapSize = 0;
	self->size = 0;
}

/*
 * 0x004D8FE0 - std::deque<TimeEvent>::clear
 *
 * Pops elements until empty.
 */
static __attribute__((unused)) void
StdDeque_Clear(StdDeque_TimeEvent *self)
{
	while (!(EventRingBuffer_IsEmpty() & 0xFF))
		EventRingBuffer_Pop();
	USED(self);
}

/*
 * 0x004D9010 - Copy front iterator
 *
 * Copies the deque's front iterator to dest.
 */
StdDequeIter *
StdDeque_CopyFrontIter(StdDeque_TimeEvent *self, StdDequeIter *dest)
{
	dest->first = self->front.first;
	dest->last = self->front.last;
	dest->cur = self->front.cur;
	dest->node = self->front.node;
	return dest;
}

/*
 * 0x004D9040 - Copy end iterator
 *
 * Copies the deque's back iterator to dest.
 */
StdDequeIter *
StdDeque_CopyEndIter(StdDeque_TimeEvent *self, StdDequeIter *dest)
{
	dest->first = self->back.first;
	dest->last = self->back.last;
	dest->cur = self->back.cur;
	dest->node = self->back.node;
	return dest;
}

/*
 * 0x004D9070 - EventRingBuffer::Front
 *
 * Returns pointer to the front TimeEvent.
 */
TimeEvent *
EventRingBuffer_Front(void)
{
	StdDequeIter localIter;
	StdDequeIter *result;

	result = StdDeque_CopyFrontIter(&g_eventRingBuffer, &localIter);
	return StdDequeIter_Deref(result);
}

/*
 * 0x004D9090 - EventRingBuffer::Pop
 *
 * Advances the front iterator past the current element and
 * decrements size. If the deque becomes empty or the page
 * boundary is reached, calls _PopFront to release the page.
 */
void
EventRingBuffer_Pop(void)
{
	StdDeque_TimeEvent *self = &g_eventRingBuffer;
	uint8_t *oldCur;

	oldCur = self->front.cur;
	self->front.cur = self->front.cur + DEQUE_ELEM_SIZE;
	// Binary: call CVector_Destroy6_Single (0x00479FF0) - no-op destructor
	USED(oldCur);

	self->size = self->size - 1;

	if (EventRingBuffer_IsEmpty() & 0xFF) {
		StdDeque_PopFront(self);
	} else {
		if (self->front.cur == self->front.last)
			StdDeque_PopFront(self);
	}
}

/*
 * 0x004D9100 - std::deque<TimeEvent>::push_back
 *
 * If empty or back page full, calls _Buyback to allocate a new
 * page. Copies src into the back slot, advances back.cur, and
 * increments size.
 */
void
StdDeque_push_back(StdDeque_TimeEvent *self, TimeEvent *src)
{
	uint8_t *dest;
	TimeEvent *d;
	TimeEvent *s;

	if (EventRingBuffer_IsEmpty() & 0xFF) {
		StdDeque_Buyback(self);
	} else if (self->back.cur == self->back.last) {
		StdDeque_Buyback(self);
	}

	dest = self->back.cur;
	self->back.cur = self->back.cur + DEQUE_ELEM_SIZE;

	// Binary: call SurfaceInfo_ConstructorAlloc (0x0046BE80) -> AllocNode (0x004D9710)
	// -> StdKfn_Identity (placement new) -> 12-byte copy.
	d = (TimeEvent *)dest;
	s = src;
	d->serial = s->serial;
	d->eventType = s->eventType;
	d->param = s->param;

	self->size = self->size + 1;
}

/*
 * 0x004D9170 - std::deque iterator init to zeros
 *
 * Zeros all iterator fields.
 */
StdDequeIter *
StdDequeIter_InitZero(StdDequeIter *self)
{
	self->first = NULL;
	self->last = NULL;
	self->cur = NULL;
	self->node = NULL;
	return self;
}

/*
 * 0x004D91B0 - std::deque iterator post-increment
 *
 * Saves current state, advances, returns the saved state via out.
 * Used by PurgeScriptEvents for loop iterator advance.
 */
StdDequeIter *
StdDequeIter_PostInc(StdDequeIter *self, StdDequeIter *out, int unused)
{
	StdDequeIter saved;

	USED(unused);

	saved.first = self->first;
	saved.last = self->last;
	saved.cur = self->cur;
	saved.node = self->node;

	StdDequeIter_Inc(self);

	out->first = saved.first;
	out->last = saved.last;
	out->cur = saved.cur;
	out->node = saved.node;

	return out;
}

/*
 * 0x004D9200 - std::deque iterator not-equal
 *
 * Returns 1 when the two deque iterators point at different positions.
 */
int
StdDequeIter_NotEqual(StdDequeIter *self, StdDequeIter *other)
{
	int eq = StdDequeIter_Equal(self, other);
	return (eq & 0xFF) ? 0 : 1;
}

/*
 * 0x004D9230 - EventRingBuffer::IsEmpty
 *
 * Returns 1 if count is zero, 0 otherwise.
 */
int
EventRingBuffer_IsEmpty(void)
{
	uint32_t c = EventRingBuffer_Count();
	return c == 0 ? 1 : 0;
}

/*
 * 0x004D9250 - std::deque<TimeEvent>::_Buyback
 *
 * Allocates a new page. Three cases:
 * 1. Empty deque: allocate page map (2 entries), place page at map[1],
 *    init both iterators to midpoint.
 * 2. Room in map after back: advance back.node, store page, init back
 *    iterator to page start.
 * 3. Map full: grow map, reposition entries, init iterators from new
 *    map positions.
 */
static void
StdDeque_Buyback(StdDeque_TimeEvent *self)
{
	uint8_t *newPage;
	uint32_t mapHalf;
	uint32_t usedEntries;
	uint8_t **newFrontNode;
	StdDequeIter localIter;
	StdDequeIter *iterResult;

	newPage = StdDeque_AllocPage(self, DEQUE_PAGE_ELEMS, 0);

	if (EventRingBuffer_IsEmpty() & 0xFF) {
		// Case 1: empty deque - first page allocation
		self->mapSize = 2;
		mapHalf = self->mapSize >> 1;
		StdDeque_AllocPages(self);

		StdDeque_MapStore(self, &self->map[mapHalf], newPage);

		iterResult = StdDequeIter_InitFromBlock(&localIter, newPage + (DEQUE_PAGE_ELEMS / 2) * DEQUE_ELEM_SIZE, &self->map[mapHalf]);

		self->front.first = iterResult->first;
		self->front.last = iterResult->last;
		self->front.cur = iterResult->cur;
		self->front.node = iterResult->node;

		self->back.first = self->front.first;
		self->back.last = self->front.last;
		self->back.cur = self->front.cur;
		self->back.node = self->front.node;
	} else if (self->back.node < &self->map[self->mapSize - 1]) {
		// Case 2: room in map after back.node
		self->back.node = self->back.node + 1;

		StdDeque_MapStore(self, self->back.node, newPage);

		iterResult = StdDequeIter_InitFromBlock(&localIter, newPage, self->back.node);

		self->back.first = iterResult->first;
		self->back.last = iterResult->last;
		self->back.cur = iterResult->cur;
		self->back.node = iterResult->node;
	} else {
		// Case 3: map full, need to grow
		usedEntries = (uint32_t)(self->back.node - self->front.node) + 1;
		newFrontNode = StdDeque_Growmap(self, usedEntries * 2);

		StdDeque_MapStore(self, &newFrontNode[usedEntries], newPage);

		iterResult = StdDequeIter_InitFromBlock(&localIter, self->front.cur, newFrontNode);
		self->front.first = iterResult->first;
		self->front.last = iterResult->last;
		self->front.cur = iterResult->cur;
		self->front.node = iterResult->node;

		iterResult = StdDequeIter_InitFromBlock(&localIter, newPage, &newFrontNode[usedEntries]);
		self->back.first = iterResult->first;
		self->back.last = iterResult->last;
		self->back.cur = iterResult->cur;
		self->back.node = iterResult->node;
	}
}

/*
 * 0x004D9430 - std::deque<TimeEvent>::_PopFront
 *
 * Called after Pop when the front page is consumed or the deque
 * becomes empty. Advances front.node, frees the old page, and
 * either zeroes both iterators (and frees the page map) or
 * re-initializes the front iterator from the new page.
 */
static void
StdDeque_PopFront(StdDeque_TimeEvent *self)
{
	uint8_t **oldNode;
	StdDequeIter localIter;
	StdDequeIter *iterResult;

	oldNode = self->front.node;
	self->front.node = self->front.node + 1;

	StdDeque_DestroyBack(self, oldNode);

	if (EventRingBuffer_IsEmpty() & 0xFF) {
		iterResult = StdDequeIter_InitZero(&localIter);
		self->front.first = iterResult->first;
		self->front.last = iterResult->last;
		self->front.cur = iterResult->cur;
		self->front.node = iterResult->node;

		self->back.first = self->front.first;
		self->back.last = self->front.last;
		self->back.cur = self->front.cur;
		self->back.node = self->front.node;

		StdDeque_Freeback(self);
	} else {
		iterResult = StdDequeIter_InitFromBlock(&localIter, *self->front.node, self->front.node);
		self->front.first = iterResult->first;
		self->front.last = iterResult->last;
		self->front.cur = iterResult->cur;
		self->front.node = iterResult->node;
	}
}

/*
 * 0x004D9500 - std::deque iterator increment
 *
 * Advances cur by one element. On page boundary, advances node
 * and re-initializes first/last/cur from the new page.
 */
static void
StdDequeIter_Inc(StdDequeIter *self)
{
	self->cur = self->cur + DEQUE_ELEM_SIZE;
	if (self->cur == self->last) {
		self->node = self->node + 1;
		self->first = *self->node;
		self->last = self->first + DEQUE_PAGE_BYTES;
		self->cur = self->first;
	}
}

/*
 * 0x004D9570 - std::deque iterator equal
 *
 * Compares cur fields. Returns 1 if equal, 0 otherwise.
 */
static int
StdDequeIter_Equal(StdDequeIter *self, StdDequeIter *other)
{
	return self->cur == other->cur ? 1 : 0;
}

/*
 * 0x004D9590 - std::deque<TimeEvent>::_Freeback
 *
 * Frees the page map via the deallocator.
 */
static void
StdDeque_Freeback(StdDeque_TimeEvent *self)
{
	StdDeque_Dealloc(self, self->map, self->mapSize);
}

/*
 * 0x004D95C0 - std::deque<TimeEvent>::_Destroy_back
 *
 * Frees the page data at *entry via the deallocator.
 */
static void
StdDeque_DestroyBack(StdDeque_TimeEvent *self, uint8_t **entry)
{
	StdDeque_Dealloc(self, *entry, DEQUE_PAGE_ELEMS);
}

/*
 * 0x004D95E0 - std::deque<TimeEvent>::_Alloc_pages
 *
 * Allocates the page map with mapSize slots.
 */
static void
StdDeque_AllocPages(StdDeque_TimeEvent *self)
{
	self->map = (uint8_t **)StdPtrList_Charalloc((StdPtrList *)self, self->mapSize * sizeof(uint8_t *));
}

/*
 * 0x004D9610 - std::deque<TimeEvent>::_Growmap
 *
 * Allocates a new page map, copies existing entries to the
 * quarter-point of the new map, frees the old map. Returns a
 * pointer to the new front position.
 */
static uint8_t **
StdDeque_Growmap(StdDeque_TimeEvent *self, uint32_t newSize)
{
	uint8_t **newMap;
	uint32_t frontOffset;
	uint8_t **destStart;

	newMap = (uint8_t **)StdPtrList_Charalloc((StdPtrList *)self, newSize * sizeof(uint8_t *));

	frontOffset = newSize >> 2;
	destStart = &newMap[frontOffset];

	{
		uint8_t **src = self->front.node;
		uint8_t **end = self->back.node + 1;
		uint8_t **dst = destStart;
		while (src != end) {
			*dst = *src;
			dst++;
			src++;
		}
	}

	StdDeque_Dealloc(self, self->map, self->mapSize);

	self->map = newMap;
	self->mapSize = newSize;

	return &newMap[frontOffset];
}

/*
 * 0x004D9690 - Map store
 *
 * Writes value to *dest.
 */
static void
StdDeque_MapStore(StdDeque_TimeEvent *self, uint8_t **dest, uint8_t *value)
{
	USED(self);
	*dest = value;
}

/*
 * 0x004D96B0 - Allocate deque page
 *
 * Allocates a page via SurfaceInfo_AllocateN. Hint is unused.
 */
static uint8_t *
StdDeque_AllocPage(StdDeque_TimeEvent *self, int count, int hint)
{
	USED(self);
	USED(hint);
	return (uint8_t *)SurfaceInfo_AllocateN(count);
}

/*
 * 0x004D96B0 - StdDeque_AllocPage (SurfaceInfo allocator variant)
 *
 * Allocates a SurfaceInfo page via SurfaceInfo_AllocateN.
 */
void *
StdDeque_AllocPageSI(CVector *self, int count, int hint)
{
	USED(self);
	USED(hint);
	return SurfaceInfo_AllocateN(count);
}

/*
 * 0x004D96D0 - Init iterator from block
 *
 * Initializes iterator fields from the page map entry:
 * first = page start, last = page end, cur = curPos, node = mapEntry.
 */
static StdDequeIter *
StdDequeIter_InitFromBlock(StdDequeIter *self, uint8_t *curPos, uint8_t **mapEntry)
{
	self->first = *mapEntry;
	self->last = *mapEntry + DEQUE_PAGE_BYTES;
	self->cur = curPos;
	self->node = mapEntry;
	return self;
}

/*
 * 0x004D9710 - StdDeque_AllocNode (SurfaceInfo variant)
 *
 * Copies a SurfaceInfo from src to dest if dest is non-NULL.
 */
void *
StdDeque_AllocNodeSI(void *dest, void *src)
{
	if (dest == NULL)
		return NULL;

	memcpy(dest, src, sizeof(SurfaceInfo));
	return dest;
}

// Iterator dereference - returns cur pointer.
TimeEvent *
StdDequeIter_Deref(StdDequeIter *self)
{
	return (TimeEvent *)self->cur;
}
