/*
 * CContainer - inventory container behaviour.
 *
 * Manages the doubly-linked child list under CContainer.contents, weight
 * and volume accumulation as items are added/removed, recursive parent
 * walks, and send-to-client serialization of contents.
 */

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// mincore is a Linux syscall but not in POSIX; declare for _POSIX_C_SOURCE.
int mincore(void *, size_t, unsigned char *);

#include "dat.h"

#include "mobile.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "region.h"
#include "stl.h"
#include "trade.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static int seh_ptr_readable(const void *addr, size_t len);
static void seh_predelete_handler(int sig);
static uint32_t Container_WeightExcludingGold(CItem *item); // 0x0044A52D
static CItem *CContainer_FindItemBySerial_48DE7B(CContainer *this, uint32_t serial, CItem *cur); // 0x0048DE7B

// Binary: 0x0068B3A0, total CContainer count.
int g_ContainerCount;

/*
 * 0x00448760 - CContainer::CContainer
 *
 * Default constructor: runs the CItem base constructor, installs the
 * CContainer vtable, clears the contents list, lockOwner, and
 * storedWeight, and bumps the global container count.
 */
void
CContainer_Constructor(CContainer *cont)
{
	CItem_Constructor((CItem *)cont);
	cont->item.resourceEntity.entity.vtable = &g_vtable_CContainer;
	g_ContainerCount++;
	cont->contents = NULL;
	cont->lockOwner = NULL;
	cont->storedWeight = 0;
}

/*
 * 0x004487AB - CContainer::CContainer(bodyType, location)
 *
 * Variant ctor taking body type and initial location. Dead code in
 * the binary.
 */
void
CContainer_ConstructorWithArgs(CContainer *cont, uint16_t bodyType, CLocation *loc)
{
	CItem_Constructor((CItem *)cont);
	cont->item.resourceEntity.entity.vtable = &g_vtable_CContainer;
	g_ContainerCount++;
	cont->contents = NULL;
	cont->lockOwner = NULL;
	CEntity_SetBodyType(&cont->item, bodyType);
	cont->item.resourceEntity.entity.removedFromWorld = 1;
	CItem_SetLocationDropAtFeet(&cont->item, loc);
}

/*
 * 0x0044883C - CContainer::ExcludedAmount
 *
 * Returns 1 for vendor-corpse body types so their weight is excluded
 * from container totals.
 */
int
CContainer_ExcludedAmount_VT(CItem *self)
{
	if ((CEntity_GetBodyType(self) & 0xFFFF) == 0x0EFA)
		return 1;
	if ((CEntity_GetBodyType(self) & 0xFFFF) == 0x0E3B)
		return 1;
	return 0;
}

/*
 * 0x0044887F - CContainer::HasAccessibleContents
 *
 * Returns 0 when the container is locked, fake, a vendor corpse, or a
 * book-family body; 1 otherwise.
 */
int
CContainer_HasAccessibleContents_VT(CItem *item)
{
	int val;
	uint16_t bodyType;

	if (CItem_GetTagInt(item, "isLocked", &val)) {
		if (val > 0)
			return 0;
	}

	if (CItem_GetTagInt(item, "fakeCont", &val))
		return 0;

	if (((int (*)(void *))VT_FN(item, VT_EXCLUDED_AMOUNT))(item))
		return 0;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	if (bodyType == 0x14F2 || bodyType == 0x1011 || (bodyType >= 0x1769 && bodyType <= 0x176B))
		return 0;

	return 1;
}

/*
 * SEH emulation for CContainer_PreDeleteCleanup: binary uses an SEH frame
 * (fs:[0] handler at 0x005E534C) to catch access violations from corrupted
 * contents chain pointers. On Linux, emulate with two layers:
 * 1. mincore() checks whether the address page is mapped.
 * 2. sigsetjmp/siglongjmp SIGSEGV handler as backup.
 */
// Custom - cached sysconf(_SC_PAGESIZE) for the SEH emulation mincore check
static long s_seh_page_size;
// Custom - SIGSEGV longjmp target for CContainer_PreDeleteCleanup SEH emulation
static sigjmp_buf s_seh_predelete_jmpbuf;

/*
 * 0x00448926 - CItem::CancelTrade
 *
 * Closes any trade session whose container1 or container2 is this
 * container. Returns 1 if one was found.
 */
int
CItem_CancelTrade(CItem *container)
{
	CTradeSession *session;
	CTradeSession *nextSession;
	CTradeSession *sess2;
	CTradeSession *sess3;
	CTradeSession *result;

	session = g_TradeSessionList;
	while (session != NULL) {
		nextSession = session->next;
		if (session->container1 == container || session->container2 == container) {
			sess2 = session;
			sess3 = sess2;
			if (sess3 != NULL) {
				result = CloseTrade(sess3, 1);
			} else {
				result = NULL;
			}
			USED(result);
			return 1;
		}
		session = nextSession;
	}
	return 0;
}

/*
 * 0x0044899A - CContainer::PreDeleteCleanup
 *
 * Walks the contents chain, collects child serials into a vector, and
 * either places each back in the world (when g_WorldActive2 is set) or
 * deletes it.
 *
 * FIXED: the binary wraps the walk in an SEH frame that catches access
 * violations from corrupted chain pointers. Linux emulates this with a
 * mincore() readability check plus a sigsetjmp/siglongjmp SIGSEGV
 * handler as a backup.
 */
__attribute__((no_sanitize("address"))) void
CContainer_PreDeleteCleanup(CItem *item)
{
	CVector serialsVec;
	char typeFlag = 0;
	CItem *walk;
	uintptr_t *ptr;
	CItem *child;
	struct sigaction sa, old_sa;

	CVector_Constructor(&serialsVec, &typeFlag);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = seh_predelete_handler;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, &old_sa);

	walk = ((CContainer *)item)->contents;
	if (sigsetjmp(s_seh_predelete_jmpbuf, 1) == 0) {
		while (walk != NULL) {
			// Check 0x24 bytes: serial (0x04) and spatialNext (0x20).
			if (!seh_ptr_readable(walk, 0x24))
				break;
			if (CWorld_FindBySerial(g_World, walk->serial) != walk)
				break;
			CVector_PushBack(&serialsVec, walk->serial);
			walk = walk->spatialNext;
		}
	}

	sigaction(SIGSEGV, &old_sa, NULL);

	ptr = (uintptr_t *)serialsVec.begin;
	while (ptr != (uintptr_t *)serialsVec.end) {
		child = CWorld_FindBySerial(g_World, (uint32_t)*ptr);
		if (child != NULL) {
			if (g_WorldActive2 != 0) {
				CItem_PlaceInWorld(child, 1);
			} else {
				if (child != NULL)
					((void (*)(void *))VT_FN(child, VT_DELETE))(child);
			}
		}
		ptr++;
	}

	CVector_Destructor(&serialsVec);
}

/*
 * 0x00448A94 - CContainer::~CContainer
 *
 * Cancels any active trade first; then, for a container still indexed in
 * the world hash, hides it, clears scripts/tags, and decrements the
 * global count before chaining to CItem's dtor.
 */
void
CContainer_Destructor(CItem *item)
{
	item->resourceEntity.entity.vtable = &g_vtable_CContainer;

	if (CItem_CancelTrade(item))
		goto chain;

	if (CWorld_FindBySerial(g_World, item->serial) != item)
		goto chain;

	if (item->resourceEntity.entity.removedFromWorld == 0)
		CItem_HideVT(item);

	CItem_ClearScriptsAndTags(item);

	g_ContainerCount--;

chain:
	CItem_Destructor(item);
}

/*
 * 0x00448B3C - CContainer::CountItems
 *
 * Counts items in the contents chain, recursing into non-excluded
 * sub-containers. Returns 0 when the top container is excluded.
 */
int
CContainer_CountItems(CContainer *this, int recursive)
{
	CItem *iter;
	int count;

	if (CItem_IsNonCountable(&this->item))
		return 0;

	count = 0;
	for (iter = this->contents; iter != NULL; iter = iter->spatialNext) {
		count++;
		if (recursive) {
			// vtable[0xD4]: check if child has contents
			if (VT_IsMobile2(iter)) {
				if (!CItem_IsNonCountable(iter)) {
					count += CContainer_CountItems((CContainer *)iter, 1);
				}
			}
		}
	}
	return count;
}

/*
 * 0x00448BC0 - CContainer::ContainsEntity
 *
 * Returns 1 if target appears in this container (or, when recursive is
 * set, any mobile-backed sub-container).
 */
int
CContainer_ContainsEntity(CContainer *this, CItem *target, int recursive)
{
	CItem *walk;

	for (walk = this->contents; walk != NULL; walk = walk->spatialNext) {
		if (walk == target)
			return 1;
		if (recursive) {
			if (VT_IsMobile2(walk)) {
				if (CContainer_ContainsEntity((CContainer *)walk, target, recursive) == 1)
					return 1;
			}
		}
	}
	return 0;
}

/*
 * 0x00448C2E - CContainer::GetValue
 *
 * Sums the container's own value and each child's virtual GetValue,
 * optionally normalizing the total.
 */
int
CContainer_GetValue_VT(CItem *self, int useResource, int normalize)
{
	int value;
	CItem *child;

	value = 0;
	if (!CItem_IsValueless(self))
		value += CItem_GetValue_VT(self, useResource, 0);

	child = ((CContainer *)self)->contents;
	while (child != NULL) {
		value += ((int (*)(void *, int, int))VT_FN(child, VT_GET_VALUE))(child, useResource, 0);
		child = child->spatialNext;
	}

	if (!CItem_IsValueless(self) && normalize != 0)
		value = CItem_NormalizeValue(self, value);

	return value;
}

/*
 * 0x00448CBD - CContainer::DecayPlace
 *
 * Snapshots contents, then hides each child and drops it at loc.
 */
void
CContainer_DecayPlace(CItem *self, CLocation *loc)
{
	CVector vec;
	char typeFlag = 0;
	CItem *iter;
	uintptr_t *ptr;

	CVector_Constructor(&vec, &typeFlag);

	iter = ((CContainer *)self)->contents;
	while (iter != NULL) {
		CVector_PushBack(&vec, (uintptr_t)iter);
		iter = iter->spatialNext;
	}

	ptr = (uintptr_t *)vec.begin;
	while (ptr != (uintptr_t *)vec.end) {
		CItem *child = (CItem *)*ptr;
		((void (*)(void *))VT_FN(child, VT_HIDE))(child);
		((void (*)(void *, CLocation *))VT_FN(child, VT_DROP_AT_FEET))(child, loc);
		ptr++;
	}

	CVector_Destructor(&vec);
}

/*
 * 0x00448D7C - CContainer::DecayReturnHome
 *
 * Like DecayPlace, but children with a home are returned to it before
 * being dropped at loc.
 */
void
CContainer_DecayReturnHome(CItem *self, CLocation *loc)
{
	CVector vec;
	char typeFlag = 0;
	CItem *iter;
	uintptr_t *ptr;

	CVector_Constructor(&vec, &typeFlag);

	iter = ((CContainer *)self)->contents;
	while (iter != NULL) {
		CVector_PushBack(&vec, (uintptr_t)iter);
		iter = iter->spatialNext;
	}

	ptr = (uintptr_t *)vec.begin;
	while (ptr != (uintptr_t *)vec.end) {
		CItem *child = (CItem *)*ptr;
		if (CItem_HasHome(child)) {
			CItem_ReturnToHome(child);
		} else {
			((void (*)(void *))VT_FN(child, VT_HIDE))(child);
			((void (*)(void *, CLocation *))VT_FN(child, VT_DROP_AT_FEET))(child, loc);
		}
		ptr++;
	}

	CVector_Destructor(&vec);
}

/*
 * 0x00448E55 - CContainer::DecayCleanup
 *
 * Returns each child with a home and recursively decays + deletes the
 * rest. No-op when the container itself is no longer indexed.
 */
void
CContainer_DecayCleanup(CItem *self)
{
	CVector vec;
	char typeFlag = 0;
	CItem *iter;
	uintptr_t *ptr;

	CVector_Constructor(&vec, &typeFlag);

	if (!World_IsEntityInHash(self)) {
		CVector_Destructor(&vec);
		return;
	}

	iter = ((CContainer *)self)->contents;
	while (iter != NULL) {
		CVector_PushBack(&vec, (uintptr_t)iter);
		iter = iter->spatialNext;
	}

	ptr = (uintptr_t *)vec.begin;
	while (ptr != (uintptr_t *)vec.end) {
		CItem *child = (CItem *)*ptr;
		if (!World_IsEntityInHash(child)) {
			ptr++;
			continue;
		}
		if (CItem_HasHome(child)) {
			CItem_ReturnToHome(child);
		} else {
			((void (*)(void *))VT_FN(child, VT_DECAY_CLEANUP))(child);
			if (child != NULL)
				((void (*)(void *))VT_FN(child, VT_DELETE))(child);
		}
		ptr++;
	}

	CVector_Destructor(&vec);
}

/*
 * 0x00448F6E - CContainer::SendContainerContents
 *
 * Broadcasts an unfiltered MULTI_OBJ_TO_OBJ packet for this container.
 */
void
CContainer_SendContainerContents(CContainer *this, CItem *entity, uint32_t serial, int sellMode)
{
	uint8_t buf[0x20018];

	PacketManager_MakePacket_MULTI_OBJ_TO_OBJ(buf, this, 0, sellMode);
	Entity_BroadcastPacket(entity, serial, buf);
}

/*
 * 0x00448FBA - CContainer::SendFilteredContents
 *
 * Filtered variant of SendContainerContents used by the buy window.
 */
void
CContainer_SendFilteredContents(CContainer *this, CItem *entity, uint32_t serial, int sellMode)
{
	uint8_t buf[0x20018];

	PacketManager_MakePacket_MULTI_OBJ_TO_OBJ(buf, this, 1, sellMode);
	Entity_BroadcastPacket(entity, serial, buf);
}

/*
 * 0x00449006 - CContainer::FindItemBySerial
 *
 * Walks the contents chain recursively, and for mobile containers also
 * searches equipment slots.
 */
CItem *
CContainer_FindItemBySerial(CContainer *this, uint32_t serial)
{
	CItem *iter, *found;

	for (iter = this->contents; iter != NULL; iter = iter->spatialNext) {
		if (iter->serial == serial)
			return iter;

		if (VT_IsMobile2(iter)) {
			found = CContainer_FindItemBySerial((CContainer *)iter, serial);
			if (found != NULL)
				return found;
		}
	}

	if (VT_IsMobile(&this->item)) {
		found = CMobile_FindEquippedItem((CMobile *)this, serial);
		if (found != NULL)
			return found;
	}

	return NULL;
}

/*
 * 0x0044909B - CContainer::GetTotalQuantity
 *
 * Sums virtual GetItemAmount across every child whose body matches
 * bodyType, descending into accessible sub-containers.
 */
int
CContainer_GetTotalQuantity(CContainer *this, uint16_t bodyType)
{
	CItem *iter;
	int total;

	total = 0;
	for (iter = this->contents; iter != NULL; iter = iter->spatialNext) {
		if (((int (*)(void *))VT_FN(iter, VT_HAS_ACCESSIBLE_CONTENTS))(iter)) {
			total += CContainer_GetTotalQuantity((CContainer *)iter, bodyType);
		} else {
			if ((CEntity_GetBodyType(iter) & 0xFFFF) == bodyType) {
				total += ((int (*)(void *))VT_FN(iter, VT_GET_ITEM_AMOUNT))(iter);
			}
		}
	}
	return total;
}

/*
 * 0x00449129 - CContainer::GetMaxWeight
 *
 * Returns the tiledata miscData field for the container's body, which
 * encodes its weight capacity.
 */
int
CContainer_GetMaxWeight(CItem *self)
{
	uint16_t bodyType;

	bodyType = CEntity_GetBodyType(self) & 0xFFFF;
	return (int)g_ItemTileData[bodyType].miscData;
}

/*
 * 0x0044914E - CContainer::GetStoredWeight
 *
 * Returns the cached contents weight, or 0 for non-countable items.
 */
uint32_t
CContainer_GetStoredWeight(CItem *item)
{
	if (CItem_IsNonCountable(item) == 1)
		return 0;
	return (uint32_t)((CContainer *)item)->storedWeight;
}

/*
 * 0x00449175 - CContainer::ConsumeResources
 *
 * Recursively pulls targetAmount worth of bodyType resources out of
 * this container, folding them into result (created on first hit).
 * Returns the accumulated result, or NULL when nothing matched.
 */
CItem *
CContainer_ConsumeResources(CContainer *container, uint16_t bodyType, int targetAmount, CItem *result)
{
	CVector list;
	char typeFlag;
	CItem *child;
	uintptr_t *iter;
	int remaining, childRatio;
	int needReinsert;
	CItem *newItem;

	typeFlag = 0;
	CVector_Constructor(&list, &typeFlag);

	child = container->contents;
	while (child != NULL) {
		if (((int (*)(void *))VT_FN(child, VT_HAS_ACCESSIBLE_CONTENTS))(child)) {
			CVector_PushBack(&list, (uintptr_t)child);
		} else if ((CEntity_GetBodyType(child) & 0xFFFF) == (bodyType & 0xFFFF)) {
			CVector_PushBack(&list, (uintptr_t)child);
		}
		child = child->spatialNext;
	}

	iter = (uintptr_t *)list.begin;
	while (iter != (uintptr_t *)list.end) {
		child = (CItem *)*iter;

		if (((int (*)(void *))VT_FN(child, VT_HAS_ACCESSIBLE_CONTENTS))(child)) {
			result = CContainer_ConsumeResources((CContainer *)child, bodyType, targetAmount, result);
			goto check_done;
		}

		if (result == NULL) {
			void *mem = OperatorNew(sizeof(CItem));
			if (mem != NULL)
				newItem = CItem_Constructor(mem);
			else
				newItem = NULL;
			result = newItem;
			CEntity_SetBodyType(result, bodyType);
		}

		if (result->resourceEntity.entity.removedFromWorld != 0) {
			needReinsert = 0;
		} else {
			needReinsert = 1;
			((void (*)(void *))VT_FN(result, VT_DETACH_SPATIAL))(result);
		}

		remaining = targetAmount - CItem_GetMinResourceRatio(result);
		childRatio = CItem_GetMinResourceRatio(child);

		if (childRatio >= remaining) {
			CItem_UpdateContainInfo(child, 1);
			((void (*)(void *))VT_FN(child, VT_HIDE))(child);

			if (CItem_ConsumeAmount(child, result, remaining)) {
				((void (*)(void *))VT_FN(child, VT_RETURN_TO_TRACKED))(child);
			}
		} else {
			CItem_ConsumeAmount(child, result, childRatio);
		}

		if (needReinsert)
			((void (*)(void *))VT_FN(result, VT_RETURN_TO_TRACKED))(result);

check_done:
		if (result != NULL && CItem_GetMinResourceRatio(result) == targetAmount) {
			CVector_Destructor(&list);
			return result;
		}

		iter++;
	}

	CVector_Destructor(&list);
	return result;
}

/*
 * 0x004493F8 - CItem::GetContainerGump
 *
 * Maps a container body type to its gump id, returning 9 (default) for
 * unknown bodies.
 */
uint16_t
CItem_GetContainerGump(CItem *item)
{
	uint32_t bodyType;

	bodyType = CEntity_GetBodyType(item);
	switch (bodyType) {
	case 0x0990: // bank box
	case 0x09AC: // metal chest (east)
	case 0x09B1: // metal chest (south)
		return 0x41;
	case 0x09A8: // metal box
	case 0x0E80: // metal box (alt)
		return 0x4B;
	case 0x09A9: // wooden box (east)
	case 0x0E3C: // large chest (east)
	case 0x0E3D: // large chest (south)
	case 0x0E3E: // large chest (east, alt)
	case 0x0E3F: // large chest (south, alt)
	case 0x0E7E: // large chest (alt)
		return 0x44;
	case 0x09AA: // wooden box (south)
	case 0x0E7D: // wooden box (alt)
		return 0x43;
	case 0x09AB: // metal chest (small)
	case 0x0E7C: // metal chest (small, alt)
		return 0x4A;
	case 0x09B0: // backpack (alt east)
	case 0x09B2: // backpack (alt south)
	case 0x0E75: // backpack
	case 0x0E79: // backpack (alt)
		return 0x3C;
	case 0x0E76: // leather bag
		return 0x3D;
	case 0x0E77: // barrel (small)
	case 0x0E78: // barrel (small, alt)
	case 0x0E7B: // bag (alt)
	case 0x0E7F: // crate (alt)
	case 0x0E83: // bag (alt)
	case 0x0FAB: // wooden box (small)
	case 0x0FAE: // wooden box (small, alt)
	case 0x1008: // wooden box (medium)
	case 0x154D: // barrel
	case 0x1940: // dresser
	case 0x1AD6: // pouch
	case 0x1AD7: // pouch (alt)
		return 0x3E;
	case 0x0E7A: // keg
		return 0x3F;
	case 0x0A30: // chest of drawers (east)
	case 0x0A31: // chest of drawers (east, alt)
	case 0x0A32: // chest of drawers (south)
	case 0x0A33: // chest of drawers (south, alt)
	case 0x0A38: // armoire (east)
	case 0x0A39: // armoire (east, open)
	case 0x0A3A: // armoire (south)
	case 0x0A3B: // armoire (south, open)
		return 0x48;
	case 0x0A2C: // wooden chest (east)
	case 0x0A2D: // wooden chest (east, alt)
	case 0x0A2E: // wooden chest (south)
	case 0x0A2F: // wooden chest (south, alt)
	case 0x0A34: // wooden shelf (east)
	case 0x0A35: // wooden shelf (east, alt)
	case 0x0A36: // wooden shelf (south)
	case 0x0A37: // wooden shelf (south, alt)
	case 0x0A3C: // crate (small, east)
	case 0x0A3D: // crate (small, south)
	case 0x0A3E: // crate (small, east, alt)
	case 0x0A3F: // crate (small, south, alt)
	case 0x0A40: // crate (small)
	case 0x0A41: // crate (small, alt)
	case 0x0A42: // crate (small)
	case 0x0A43: // crate (small, alt)
	case 0x0A44: // crate (small)
	case 0x0A45: // crate (small, alt)
	case 0x0A46: // crate (small)
	case 0x0A47: // crate (small, alt)
	case 0x0A48: // crate (small)
	case 0x0A49: // crate (small, alt)
	case 0x0A4A: // crate (small)
	case 0x0A4B: // crate (small, alt)
		return 0x51;
	case 0x0A4C: // fancy dresser (east)
	case 0x0A4D: // fancy dresser (east, alt)
	case 0x0A50: // fancy dresser (south)
	case 0x0A51: // fancy dresser (south, alt)
		return 0x4E;
	case 0x0A4E: // fancy dresser (east, alt 2)
	case 0x0A4F: // fancy dresser (east, alt 3)
	case 0x0A52: // fancy dresser (south, alt 2)
	case 0x0A53: // fancy dresser (south, alt 3)
		return 0x4F;
	case 0x0A97: // bookcase (east)
	case 0x0A98: // bookcase (east, alt)
	case 0x0A99: // bookcase (south)
	case 0x0A9A: // bookcase (south, alt)
	case 0x0A9B: // bookcase (south, full)
	case 0x0A9C: // bookcase (south, full, alt)
	case 0x0A9D: // bookcase (east, full)
	case 0x0A9E: // bookcase (east, full, alt)
		return 0x4D;
	case 0x0E40: // medium crate (east)
	case 0x0E41: // medium crate (east, alt)
		return 0x42;
	case 0x0E42: // medium crate (south)
	case 0x0E43: // medium crate (south, alt)
		return 0x49;
	case 0x0E1C: // scroll case
	case 0x0FAD: // scroll case (alt)
		return 0x92E;
	case 0x0FA6: // game board (also used as spellbook container)
		return 0x91A;
	case 0x1E5E: // coffin
		return 0x52;
	case 0x2AF8: // ship hold
		return 0x47;
	case 0x3E65: // secure trade
	case 0x3E93: // secure trade (alt)
	case 0x3EAE: // secure trade
	case 0x3EB9: // secure trade
		return 0x4C;
	default:
		return 9;
	}
}

/*
 * 0x004497C1 - CContainer::GetContainerBounds
 *
 * Writes the container gump's item-placement rectangle through *minX,
 * *x (maxX), *minY, *y (maxY).
 */
void
CContainer_GetContainerBounds(CItem *container, int *minX, int *x, int *minY, int *y)
{
	int gumpId;

	gumpId = (int)(uint16_t)CItem_GetContainerGump(container);

	switch (gumpId) {
	case 0x07:
		*minX = 0x1e;
		*minY = 0x1e;
		*x = 0x10e;
		*y = 0xaa;
		break;
	case 0x09:
		*minX = 0x14;
		*minY = 0x55;
		*x = 0x7c;
		*y = 0xc4;
		break;
	case 0x3C:
		*minX = 0x2c;
		*minY = 0x41;
		*x = 0xba;
		*y = 0x9f;
		break;
	case 0x3D:
		*minX = 0x1d;
		*minY = 0x22;
		*x = 0x89;
		*y = 0x80;
		break;
	case 0x3E:
		*minX = 0x21;
		*minY = 0x24;
		*x = 0x8e;
		*y = 0x94;
		break;
	case 0x3F:
		*minX = 0x13;
		*minY = 0x2f;
		*x = 0xb6;
		*y = 0x7b;
		break;
	case 0x40:
		*minX = 0x10;
		*minY = 0x26;
		*x = 0x98;
		*y = 0x7d;
		break;
	case 0x41:
		*minX = 0x23;
		*minY = 0x26;
		*x = 0x91;
		*y = 0x74;
		break;
	case 0x42:
		*minX = 0x12;
		*minY = 0x69;
		*x = 0xa2;
		*y = 0xb2;
		break;
	case 0x43:
		*minX = 0x10;
		*minY = 0x33;
		*x = 0xb8;
		*y = 0x7c;
		break;
	case 0x44:
		*minX = 0x14;
		*minY = 0x0a;
		*x = 0xaa;
		*y = 0x64;
		break;
	case 0x47:
		*minX = 0x10;
		*minY = 0x0a;
		*x = 0x94;
		*y = 0x8a;
		break;
	case 0x48:
	case 0x51:
		*minX = 0x10;
		*minY = 0x0a;
		*x = 0x9a;
		*y = 0x5e;
		break;
	case 0x49:
		*minX = 0x12;
		*minY = 0x69;
		*x = 0xa2;
		*y = 0xb2;
		break;
	case 0x4A:
		*minX = 0x12;
		*minY = 0x69;
		*x = 0xa2;
		*y = 0xb2;
		break;
	case 0x4B:
		*minX = 0x10;
		*minY = 0x33;
		*x = 0xb8;
		*y = 0x7c;
		break;
	case 0x4C:
		*minX = 0x2e;
		*minY = 0x4a;
		*x = 0xc4;
		*y = 0xb8;
		break;
	case 0x4D:
		*minX = 0x4c;
		*minY = 0x0c;
		*x = 0x8c;
		*y = 0x44;
		break;
	case 0x4E:
	case 0x4F:
		*minX = 0x18;
		*minY = 0x60;
		*x = 0xc4;
		*y = 0x98;
		break;
	case 0x52:
		*minX = 0;
		*minY = 0;
		*x = 0x6e;
		*y = 0x3e;
		break;
	case 0x91A:
		*minX = 0x32;
		*minY = 0x32;
		*x = 0xe8;
		*y = 0xb4;
		break;
	case 0x92E:
		*minX = 0x32;
		*minY = 0x32;
		*x = 0xe8;
		*y = 0xa0;
		break;
	default:
		*minX = 0;
		*minY = 0;
		*x = 0xc8;
		*y = 0xc8;
		break;
	}
}

/*
 * 0x00449C46 - CContainer::AddStoredWeight
 *
 * Adds weight to storedWeight and propagates up the parent chain while
 * the current item is countable.
 */
void
CContainer_AddStoredWeight(CItem *this, int weight)
{
	((CContainer *)this)->storedWeight += (int16_t)weight;
	if (this->parent != NULL) {
		if (!CItem_IsNonCountable(this)) {
			CContainer_AddStoredWeight(this->parent, weight);
		}
	}
}

/*
 * 0x00449C8A - CContainer::CheckSubtractWeight
 *
 * Returns 1 when weight can safely be subtracted from storedWeight.
 * On underflow clamps storedWeight to 0 and returns 0.
 */
int
CContainer_CheckSubtractWeight(CItem *this, int weight)
{
	int diff = (int)(uint16_t)((CContainer *)this)->storedWeight - (int)((unsigned)weight & 0xFFFF);
	if (diff < 0) {
		((CContainer *)this)->storedWeight = 0;
		return 0;
	}
	return 1;
}

/*
 * 0x00449CC1 - CContainer::SubtractStoredWeight
 *
 * Subtracts weight when safe and propagates the adjustment up the
 * parent chain while the item is countable.
 */
void
CContainer_SubtractStoredWeight(CItem *this, int weight)
{
	if (CContainer_CheckSubtractWeight(this, weight))
		((CContainer *)this)->storedWeight -= (int16_t)weight;
	if (this->parent != NULL) {
		if (!CItem_IsNonCountable(this)) {
			CContainer_SubtractStoredWeight(this->parent, weight);
		}
	}
}

/*
 * 0x00449D16 - CContainer::RecalcStoredWeight
 *
 * Rebuilds storedWeight from the contents chain, calling virtual
 * GetStoredWeight on container children and GetWeight (when countable)
 * on plain items.
 */
void
CContainer_RecalcStoredWeight(CItem *this)
{
	int total = 0;
	CItem *cur;

	cur = ((CContainer *)this)->contents;
	while (cur != NULL) {
		if (VT_IsMobile2(cur)) {
			total += ((int (*)(void *))VT_FN(cur, VT_GET_STORED_WEIGHT))(cur);
		} else {
			if (!CItem_IsNonCountable(cur))
				total += ((int (*)(void *))VT_FN(cur, VT_GET_WEIGHT))(cur);
		}
		cur = cur->spatialNext;
	}

	((CContainer *)this)->storedWeight = (uint16_t)total;
}

/*
 * 0x0044A52D - Container_WeightExcludingGold
 *
 * Computes total weight while skipping gold (body 0xEED); sub-containers
 * recurse so their own gold contributions drop out.
 */
static uint32_t
Container_WeightExcludingGold(CItem *item)
{
	uint32_t w;
	CItem *child;

	w = CContainer_GetWeight(item);
	w -= ((uint32_t (*)(void *))VT_FN(item, VT_GET_STORED_WEIGHT))(item);

	for (child = ((CContainer *)item)->contents; child != NULL; child = child->spatialNext) {
		if (VT_IsMobile2(child)) {
			w += Container_WeightExcludingGold(child);
		} else {
			if ((CEntity_GetBodyType(child) & 0xFFFF) != 0x0EED)
				w += CContainer_GetWeight(child);
		}
	}

	return w;
}

/*
 * 0x0044A5CD - CContainer::CanHold
 *
 * Walks to the topmost non-mobile container and delegates to it if
 * it's not this. Enforces a 400-unit weight cap (gold is free in
 * bank boxes) and a 125-item cap; *reason is set to 1 for weight and
 * 2 for count when rejection occurs.
 */
int
CContainer_CanHold(CContainer *this, CItem *item, int *reason)
{
	CItem *walker, *topmost;
	uint32_t itemWeight;
	int newItems, totalItems;

	walker = &this->item;
	while (walker != NULL && walker->parent != NULL) {
		if (VT_IsMobile(walker->parent))
			break;
		walker = walker->parent;
	}
	topmost = walker;

	if (topmost != &this->item)
		return ((int (*)(void *, CItem *, int *))VT_FN(topmost, VT_EQUIP_ITERATE))(topmost, item, reason);

	if (CItem_IsInBankBox(&this->item)) {
		if ((uint16_t)CEntity_GetBodyType(item) == 0x0EED) {
			itemWeight = 0;
		} else if (VT_IsMobile2(item)) {
			itemWeight = Container_WeightExcludingGold(item);
		} else {
			itemWeight = CContainer_GetWeight(item);
		}
	} else {
		itemWeight = CContainer_GetWeight(item);
	}

	if (this->storedWeight + itemWeight > 0x190) {
		if (reason)
			*reason = 1;
		return 0;
	}

	if (VT_IsMobile2(item)) {
		newItems = CContainer_CountItems((CContainer *)item, 1) + 1;
	} else {
		newItems = 1;
	}

	totalItems = CContainer_CountItems(this, 1) + newItems;
	if (totalItems > 0x7D) {
		if (reason)
			*reason = 2;
		return 0;
	}

	return 1;
}

/*
 * 0x0044A729 - CContainer::ValidateRegistration
 *
 * Validates the container via CItem's base check, then recursively
 * validates every child in the contents chain.
 */
void
CContainer_ValidateRegistration_VT(CContainer *self)
{
	CItem *child;

	CItem_ValidateRegistration_VT(&self->item);

	child = self->contents;
	while (child != NULL) {
		((void (*)(CItem *))VT_FN(child, VT_VALIDATE_REG))(child);
		child = child->spatialNext;
	}
}

/*
 * 0x0044A770 - CContainer::`scalar deleting destructor'
 *
 * Scalar deleting destructor: runs CContainer_Destructor and frees the
 * object when flags & 1.
 */
void *
CContainer_ScalarDelete(CItem *self, int flags)
{
	CContainer_Destructor(self);
	if (flags & 1)
		free(self);
	return NULL;
}

/*
 * 0x00484E3C - CContainer::Delete
 *
 * Runs the two CItem delete checks and invokes the scalar deleting
 * destructor with flags=1 to free the container.
 */
void
CContainer_Delete(CItem *self)
{
	if (!CItem_DeleteCheck1(self))
		return;
	if (!CItem_DeleteCheck2(self))
		return;
	if (self != NULL)
		((void *(*)(void *, int))VT_FN(self, VT_DTOR))(self, 1);
}
/*
 * 0x0048DE7B - CContainer::FindItemBySerial
 *
 * Orphaned alternate search: walks the contents chain, descending into
 * mobile equipment before falling back to sub-container contents.
 * No callers in the binary.
 */
static __attribute__((unused)) CItem *
CContainer_FindItemBySerial_48DE7B(CContainer *this, uint32_t serial, CItem *cur)
{
	int i;
	CItem *sub;

	while (cur != NULL) {
		if (cur->serial == serial)
			return cur;

		if (VT_IsMobile2(cur)) {
			if (VT_IsMobile(cur)) {
				for (i = 0; i < 0x1A; i++) {
					sub = ((CMobile *)cur)->equipment[i];
					if (sub == NULL)
						continue;
					if (sub->serial == serial)
						return sub;
					if (VT_IsMobile2(sub)) {
						sub = (CItem *)CContainer_FindItemBySerial_48DE7B(this, serial, ((CContainer *)sub)->contents);
						if (sub != NULL)
							return sub;
					}
				}
			}
			sub = (CItem *)CContainer_FindItemBySerial_48DE7B(this, serial, ((CContainer *)cur)->contents);
			if (sub != NULL)
				return sub;
		}

		cur = cur->spatialNext;
	}
	return NULL;
}

/*
 * 0x0048F2E7 - CContainer::FindInTagList
 *
 * Returns the first entity in this container (or recursive descendants)
 * whose named tag list contains value, or NULL.
 */
CItem *
CContainer_FindInTagList_VT(CContainer *self, CString *name, uint32_t value)
{
	CItem *result;
	CItem *child;

	result = CResourceEntity_IsInTagListStr(&self->item, name, value);
	if (result != NULL)
		return result;

	child = self->contents;
	while (child != NULL) {
		result = ((CItem * (*)(CItem *, CString *, uint32_t)) VT_FN(child, VT_FIND_IN_TAG_LIST))(child, name, value);
		if (result != NULL)
			return result;
		child = child->spatialNext;
	}

	return NULL;
}

/*
 * Custom - seh_ptr_readable
 *
 * Returns 1 if [addr, addr+len) is backed by mapped pages, 0 otherwise,
 * without faulting on invalid pointers.
 */
static int
seh_ptr_readable(const void *addr, size_t len)
{
	unsigned char vec;
	uintptr_t first_page, last_page;

	if (s_seh_page_size == 0)
		s_seh_page_size = sysconf(_SC_PAGESIZE);
	first_page = (uintptr_t)addr & ~(s_seh_page_size - 1);
	last_page = ((uintptr_t)addr + len - 1) & ~(s_seh_page_size - 1);
	if (mincore((void *)first_page, s_seh_page_size, &vec) != 0)
		return 0;
	if (last_page != first_page && mincore((void *)last_page, s_seh_page_size, &vec) != 0)
		return 0;
	return 1;
}

/*
 * Custom - seh_predelete_handler
 *
 * SIGSEGV handler that longjmps back to the PreDeleteCleanup save point.
 */
static void
seh_predelete_handler(int sig)
{
	USED(sig);
	siglongjmp(s_seh_predelete_jmpbuf, 1);
}

/*
 * Helper - CContainer_GetWeight
 *
 * Wraps the inline virtual GetWeight dispatch used throughout this file.
 */
uint32_t
CContainer_GetWeight(CItem *item)
{
	return (uint32_t)((int (*)(void *))VT_FN(item, VT_GET_WEIGHT))(item);
}
