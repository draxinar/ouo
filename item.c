/*
 * CItem - base item behaviour shared by every placeable object.
 *
 * Creation, serial allocation, world insertion, decay timers, light
 * sources, article-prefixed name formatting, and the vtable entries
 * (GetName, Save, GetVolume, etc.) used by CWeapon, CCorpse, CMulti,
 * and CContainer.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "combat.h"
#include "container.h"
#include "corpse.h"
#include "egg.h"
#include "feature.h"
#include "entitymanager.h"
#include "main.h"
#include "multi.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "taglist.h"
#include "template.h"
#include "time.h"
#include "timer.h"
#include "trade.h"
#include "utils.h"
#include "vg_pool.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static void *CEditorObj_Constructor(CEditorObj *self); // 0x0045EBC0
static void CItem_CollectSurfaceItems(CItem *this, CVector *outList); // 0x0045FF20
static void CItemTracking_Init(CItemTracking *tr); // 0x004674A0
static int CItem_GetResourceNodeValue(CItem *item, CResourceNode *node, int valueIndex, int *resultOut); // 0x00485E28
static void StaticEntity_Destructor(CItem *self); // 0x00486850
static void CItem_ProcessMultiDeleteItems(CItem *self, CVector *source, CVector *dest); // 0x00486EE5
static int Item_IsSpecialBodyType(CItem *item); // 0x00487A09
static void CItem_ClearCampfireNearby(CItem *item); // 0x00487BF5
static void CItem_ScanNearbyCampfire(CItem *item); // 0x00488FFE
static int CItem_MultiContainerCheck(CItem *this, CItem *container); // 0x004891A5
static int CItem_GetSpellId(CItem *item); // 0x00490DA4
static int CItem_GetSortKeyQty(CItem *item); // 0x00490DB7
static int CItem_GetSortKey(CItem *item); // 0x00490C6D
static int Hint_WakeCarriedMagic(CItem *item); // Custom (FEAT_HINT_RUMORS)

// 0x006CA900 - recursion guard for weight subtraction propagation
static uint32_t g_weightSubtractCount;

// 0x006CA904 - recursion guard for weight propagation
static uint32_t g_weightUpdateCount;

// 0x006CA928 - Entity manager list (std::list<void*>).
StdPtrList g_entityMgrList;

// 0x006CA934 - Archive flag: when set, RemoveFromWorld inserts into archive hash.
uint32_t g_ArchiveFlag;

// 0x006CA938 - Archive hash table (0x4000 buckets, serial & 0x3FFF).
CItem *g_ArchiveHash[0x4000];

/*
 * 0x004322B0 - CItem::GetMovementType (vtable[0x94])
 *
 * Movement-type getter. Base entities cannot walk, so returns 0. CMobile
 * and CNPC override with their own getters.
 */
int
CItem_GetMovementType_VT(CItem *item)
{
	USED(item);
	return 0;
}

/*
 * 0x004322C0 - CItem vtable[0x30] GetFlags
 *
 * Returns the tiledata flags for the item's bodyType (offset by the
 * stackable/open flag).
 */
int
CItem_GetFlags_VT(CItem *item)
{
	uint16_t bodyType;
	int offset;

	bodyType = CEntity_GetBodyType(item);
	offset = CItem_HasStackableFlag(item);
	return (int)g_ItemTileData[bodyType + offset].flags;
}

/*
 * 0x00449D9D - CItem::AddToContainer (vtable[0xB4])
 *
 * Default container add: merges stackables, picks a random position when
 * loc.x is -1, prepends to the child list, fixes up weights, and emits
 * the OBJ_TO_OBJ packet (or a trade-session broadcast for secured
 * containers).
 *
 * FIXED: emits DESTROY_OBJECT (0x1D) instead of OBJ_TO_OBJ (0x25)
 * when the contained entity is a mobile. The binary callers are
 * Script_putMobContainer (stables.m: the stablemaster IS the
 * container) and CMobile_Mount (the rider IS the container). The
 * binary's 0x25 packet carries the mobile's bodyType in the graphic
 * field; client builds from 3.0.6e forward dispatch the 0x25 handler
 * by tiledata.mul flags at that graphic ID, and pet body IDs
 * collide with wall/arch tile IDs (0xC8="stone wall",
 * 0xE2/0xE4="stone arch", 0x190/0x191="sandstone arch", etc.) - the
 * post-3.0.6e fallback paints the collided art tile at the entity's
 * stored coordinates. Substituting 0x1D is safe because the pet/
 * mount is being made invisible from the world view from this point
 * on, and is also necessary: the earlier VT_HIDE broadcast at the
 * entity's pre-container location can miss the only player that
 * needs the cleanup, because CMobile_Mount detaches the rider from
 * the spatial map before HIDE fires. Without the second 0x1D from
 * this branch, on every client era the rider keeps a stale copy of
 * the mount mob at its dismount-time location through every
 * re-mount cycle. See CUSTOM_SYSTEMS.md ("OBJ_TO_OBJ Broadcast For
 * Contained Mobiles").
 */
void
CItem_AddToContainerVT(CItem *item, CItem *container, CLocation *loc)
{
	CLocation tempLoc;
	CItem *iter;
	int dimX, dimY;
	int boundsMinX, boundsMaxX, boundsMinY, boundsMaxY;
	uint8_t pktBuf[0x18];
	CTradeSession *secureCtx;
	CItem *topMob;

	if (((int (*)(void *))VT_FN(item, VT_ITEM_CHECK_9C))(item)) {
		return;
	}

	if (!CItem_MultiContainerCheck(item, container)) {
		return;
	}

	CLocation_Init(&tempLoc);

	if (CEntity_GetBodyType(container) == 0x2AF8) {
		((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONT_B8))(item, container, loc);
		CItem_AddWeightToParent(item, container);
		return;
	}

	if (((int (*)(void *))VT_FN(container, VT_EXCLUDED_AMOUNT))(container)) {
		((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONT_BC))(item, container, loc);
		CItem_AddWeightToParent(item, container);
		return;
	}

	item->decayCount = 0;

	if ((int16_t)loc->x == -1) {
		if (((int (*)(void *))VT_FN(item, VT_HAS_RESOURCE_FLAG))(item)) {
			iter = ((CContainer *)container)->contents;
			while (iter != NULL) {
				if (((int (*)(void *, CItem *))VT_FN(item, VT_MERGE_INTO))(item, iter)) {
					CItem_MergeIntoWrapper(item, iter);
					return;
				}
				iter = iter->spatialNext;
			}
		}

		((void (*)(void *, int *, int *))VT_FN(item, VT_GET_CONTAINER_DIM))(item, &dimX, &dimY);

		CContainer_GetContainerBounds(container, &boundsMinX, &boundsMaxX, &boundsMinY, &boundsMaxY);

		loc->x = (uint16_t)GetRandomRange(boundsMinX, boundsMaxX - (dimX & 0xFFFF));
		loc->y = (uint16_t)GetRandomRange(boundsMinY, boundsMaxY - (dimY & 0xFFFF));
	}

	item->spatialNext = ((CContainer *)container)->contents;
	if (item->spatialNext != NULL)
		item->spatialNext->spatialPrev = item;
	((CContainer *)container)->contents = item;
	item->parent = container;

	if (VT_IsMobile2(item)) {
		if (CItem_IsInBankBox(item))
			CContainer_RecalcStoredWeight(item);
	}

	CItem_AddWeightToParent(item, container);

	CLocation_CopyFrom(&item->resourceEntity.entity.location, loc);

	item->resourceEntity.entity.removedFromWorld = 0;

	if (CItem_HasSecuredAncestor(item)) {
		secureCtx = CItem_FindSecuredContainer(item);

		SetTradeAcceptState(secureCtx, 0, 0);

		PacketManager_MakePacket_OBJ_TO_OBJ(pktBuf, item, container);
		SendToClient((CItem *)secureCtx->player1, pktBuf, -1);
		SendToClient((CItem *)secureCtx->player2, pktBuf, -1);
	} else if (!g_World->isLoading) {
		CLocation *itemLoc = ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item);

		CLocation_SetLoc(&tempLoc, itemLoc);

		if (VT_IsMobile(item)) {
			PacketManager_MakePacket_DESTROY_OBJECT(pktBuf, item->serial);
		} else {
			PacketManager_MakePacket_OBJ_TO_OBJ(pktBuf, item, container);
		}
		SendPacketInRange(pktBuf, &tempLoc, 0x12);
	}

	topMob = CItem_FindTopContainerMobile(item);
	if (topMob != NULL) {
		if (VT_IsPlayer(topMob)) {
			SendStatusToPlayer((CMobile *)topMob, (CPlayer *)topMob, topMob->serial, 1);
		}
	}

	if (!CItem_HasSecuredAncestor(item))
		CItem_ReleaseTracking(item);
}

/*
 * 0x0044A0A4 - CItem vtable[0xB8] AddToContB8
 *
 * Sorted insert into the container by ascending X position, then the
 * usual OBJ_TO_OBJ broadcast (or trade-session routing if either end
 * is locked).
 */
void
CItem_AddToContB8_VT(CItem *self, CItem *container, CLocation *loc)
{
	CLocation tmpLoc;
	int dimX, dimY, boundsMinX, boundsMaxX, boundsMinY, boundsMaxY;
	uint8_t pktBuf[20];
	CTradeSession *tradeSession;

	CLocation_Init(&tmpLoc);

	if (self->multiPtr != NULL) {
		((int (*)(CItem *))VT_FN(self, VT_RETURN_TO_TRACKED))(self);
		return;
	}

	if ((int16_t)loc->x == -1) {
		((void (*)(CItem *, uint16_t *, uint16_t *))VT_FN(self, VT_GET_CONTAINER_DIM))(self, (uint16_t *)&dimX, (uint16_t *)&dimY);
		CContainer_GetContainerBounds(container, &boundsMinX, &boundsMaxX, &boundsMinY, &boundsMaxY);
		loc->x = (int16_t)GetRandomRange(boundsMinX, boundsMaxX - (dimX & 0xFFFF));
		loc->y = (int16_t)GetRandomRange(boundsMinY, boundsMaxY - (dimY & 0xFFFF));
	}

	// Find insertion point: walk list while loc->x > cur->location.x
	{
		CItem *cur = ((CContainer *)container)->contents;
		CItem *prev = NULL;
		while (cur != NULL) {
			if ((int16_t)loc->x <= (int16_t)cur->resourceEntity.entity.location.x)
				break;
			prev = cur;
			cur = cur->spatialNext;
		}
		self->spatialNext = cur;
		if (self->spatialNext != NULL)
			self->spatialNext->spatialPrev = self;
		self->spatialPrev = prev;
		if (self->spatialPrev != NULL)
			self->spatialPrev->spatialNext = self;
		if (((CContainer *)container)->contents == cur)
			((CContainer *)container)->contents = self;
	}

	self->parent = container;
	CLocation_CopyFrom(&self->resourceEntity.entity.location, loc);
	self->resourceEntity.entity.removedFromWorld = 0;
	self->decayCount = 0;

	{
		CLocation *entLoc = ((CLocation * (*)(CItem *)) VT_FN(self, VT_GET_LOCATION))(self);
		CLocation_SetLoc(&tmpLoc, entLoc);
	}

	if (((CContainer *)container)->lockOwner == NULL) {
		if (VT_IsMobile2(self)) {
			if (((CContainer *)self)->lockOwner == NULL)
				goto broadcast;
		} else {
			goto broadcast;
		}
	}

	tradeSession = (CTradeSession *)(((CContainer *)container)->lockOwner != NULL ? ((CContainer *)container)->lockOwner : ((CContainer *)self)->lockOwner);
	SetTradeAcceptState(tradeSession, 0, 0);
	PacketManager_MakePacket_OBJ_TO_OBJ(pktBuf, self, container);
	SendToClient((CItem *)tradeSession->player1, pktBuf, -1);
	SendToClient((CItem *)tradeSession->player2, pktBuf, -1);
	goto done;

broadcast:
	PacketManager_MakePacket_OBJ_TO_OBJ(pktBuf, self, container);
	SendPacketInRange(pktBuf, &tmpLoc, 0x12);

done:
	if (!CItem_HasSecuredAncestor(self))
		CItem_ReleaseTracking(self);
}

/*
 * 0x0044A2E5 - CItem vtable[0xBC] AddToContBC
 *
 * Sorted insert into container child list by sort key (ascending).
 * Uses 0x0044A7E0 helper to compute sort key per item.
 * Otherwise identical structure to AddToContB8.
 */
void
CItem_AddToContBC_VT(CItem *self, CItem *container, CLocation *loc)
{
	CLocation tmpLoc;
	int dimX, dimY, boundsMinX, boundsMaxX, boundsMinY, boundsMaxY;
	uint8_t pktBuf[20];
	CTradeSession *tradeSession;

	CLocation_Init(&tmpLoc);

	if (self->multiPtr != NULL) {
		((void (*)(CItem *))VT_FN(self, VT_RETURN_TO_TRACKED))(self);
		return;
	}

	if ((int16_t)loc->x == -1) {
		((void (*)(CItem *, uint16_t *, uint16_t *))VT_FN(self, VT_GET_CONTAINER_DIM))(self, (uint16_t *)&dimX, (uint16_t *)&dimY);
		CContainer_GetContainerBounds(container, &boundsMinX, &boundsMaxX, &boundsMinY, &boundsMaxY);
		loc->x = (int16_t)GetRandomRange(boundsMinX, boundsMaxX - (dimX & 0xFFFF));
		loc->y = (int16_t)GetRandomRange(boundsMinY, boundsMaxY - (dimY & 0xFFFF));
	}

	// Find insertion point: walk list while selfKey > cur sort key
	// Binary: recomputes selfKey each iteration (not hoisted)
	{
		CItem *cur = ((CContainer *)container)->contents;
		CItem *prev = NULL;
		while (cur != NULL) {
			if (CItem_GetSortKey_VT(self) <= CItem_GetSortKey_VT(cur))
				break;
			prev = cur;
			cur = cur->spatialNext;
		}
		self->spatialNext = cur;
		if (self->spatialNext != NULL)
			self->spatialNext->spatialPrev = self;
		self->spatialPrev = prev;
		if (self->spatialPrev != NULL)
			self->spatialPrev->spatialNext = self;
		if (((CContainer *)container)->contents == cur)
			((CContainer *)container)->contents = self;
	}

	self->parent = container;
	CLocation_CopyFrom(&self->resourceEntity.entity.location, loc);
	self->resourceEntity.entity.removedFromWorld = 0;
	self->decayCount = 0;

	{
		CLocation *entLoc = ((CLocation * (*)(CItem *)) VT_FN(self, VT_GET_LOCATION))(self);
		CLocation_SetLoc(&tmpLoc, entLoc);
	}

	if (((CContainer *)container)->lockOwner == NULL) {
		if (VT_IsMobile2(self)) {
			if (((CContainer *)self)->lockOwner == NULL)
				goto bc2;
		} else {
			goto bc2;
		}
	}

	tradeSession = (CTradeSession *)(((CContainer *)container)->lockOwner != NULL ? ((CContainer *)container)->lockOwner : ((CContainer *)self)->lockOwner);
	SetTradeAcceptState(tradeSession, 0, 0);
	PacketManager_MakePacket_OBJ_TO_OBJ(pktBuf, self, container);
	SendToClient((CItem *)tradeSession->player1, pktBuf, -1);
	SendToClient((CItem *)tradeSession->player2, pktBuf, -1);
	goto done2;

bc2:
	PacketManager_MakePacket_OBJ_TO_OBJ(pktBuf, self, container);
	SendPacketInRange(pktBuf, &tmpLoc, 0x12);

done2:
	if (!CItem_HasSecuredAncestor(self))
		CItem_ReleaseTracking(self);
}

/*
 * 0x0044A7D0 - returns 0
 *
 * Default stub used as a vtable filler for slots a class does not override.
 */
int
vt_stub_return_0(CItem *self)
{
	USED(self);
	return 0;
}

/*
 * 0x0044A7E0 - CItem sort key helper
 *
 * Returns the minimum resource ratio for resource items, or the tiledata
 * miscData quantity otherwise.
 */
int
CItem_GetSortKey_VT(CItem *self)
{
	if (((int (*)(void *))VT_FN(self, VT_HAS_RESOURCE_FLAG))(self))
		return CItem_GetMinResourceRatio(self);
	return CItem_GetSortKey(self) & 0xFFFF;
}

/*
 * 0x004592C0 - CItem::GetDecayCount
 *
 * Returns the item's decayCount field.
 */
uint8_t
CItem_GetDecayCount(CItem *item)
{
	return item->decayCount;
}

/*
 * 0x004592D1 - CItem::GetGlobalDecayMax
 *
 * Tail-call to CWorld::GetDecayMax; the this pointer is ignored.
 */
uint8_t
CItem_GetGlobalDecayMax(CItem *item)
{
	USED(item);
	return CWorld_GetDecayMax();
}

/*
 * 0x004592E6 - CItem::SetDecayCount
 *
 * Stores count into the item's decayCount field.
 */
void
CItem_SetDecayCount(CItem *item, uint8_t count)
{
	item->decayCount = count;
}

/*
 * 0x004592FC - CItem::HasMultiDeleteTag
 *
 * Returns 1 if the item carries a "multiDelete" integer tag.
 */
int
CItem_HasMultiDeleteTag(CItem *item)
{
	int val;

	val = 0;
	if (CItem_GetTagInt(item, "multiDelete", &val))
		return 1;
	return 0;
}

/*
 * 0x0045932E - CItem::DecayTick (vtable[0x144])
 *
 * Per-item tick called by CWorld::ScanAndDecay. Applies the non-home
 * decay rate after filtering out entities that are removed, equipped,
 * contained, at home, or opted out via decayCount 0xFF. Fires the decay
 * script event first; the script may suppress decay or override the
 * increment via g_ScriptReturnFlag.
 *
 * MODIFIED under FEAT_AUTOFILL_CITY: the binary's IsAtHome gate runs
 * before Entity_ExecuteEvent, so fillcontainer.m's "trigger decay"
 * never fires for chests in city/justice regions and shop containers
 * stay empty forever. The feature moves the at-home check to after the
 * event fire (still before decayCount accumulation), restoring the
 * 1997-98 live-shard behavior. The same feature also lets at-home heavy
 * containers (weight >= 0xFF, e.g. bookcases) past the weight-exemption
 * gate so their fill scripts can run without requiring the dynfill
 * overloadedWeight workaround.
 *
 * CUSTOM under FEAT_HINT_RUMORS: the binary skips the decay event for
 * worn and contained items, so magic items carried by NPCs never fire
 * hintupdater.m's "trigger decay" and the rumor database (getHint) stays
 * empty. For carried items that hold the hintupdater script, fire the
 * decay event so the hint is recorded, then keep the binary-faithful
 * "carried items do not accumulate decay" behavior. See
 * Hint_WakeCarriedMagic.
 */
void
CItem_DecayTick(CItem *item)
{
	int etype;
	int increment;
	int newDecay;
	uint32_t serial;
	int eventResult;

	etype = CItem_ClassifyEntityByVtable(item);

	switch (etype) {
	case 2:
	case 4:
	case 8:
	case 0x1000:
		break;
	default:
		return;
	}

	if (CItem_HasMulti(item)) {
		if (!CItem_IsMultiOwner(item))
			return;
	}

	if (item->decayCount == 0xFF)
		return;

	if (!CItem_IsMultiOwner(item)) {
		// CUSTOM (FEAT_CLOSED_ECONOMY): the binary's weight gate
		// excludes items whose CItem_GetDecayMax score is >= 0xFF,
		// intended to skip statues / multis / houses. Sheep and other
		// NPC corpses land at exactly 0xFF because CContainer is
		// Mobile2 (so the gate adds GetStoredWeight) and the corpse's
		// transient stored weight + per-amount overhead reaches 255.
		// The corpse.m Wombat script has no decay callback, so the
		// effect is corpses never decay - the closed-loop refund
		// never fires either. Under FEAT_CLOSED_ECONOMY we skip the
		// gate for corpses (bodyType == CORPSE_BODYTYPE) so they go
		// through CItem_PlaceInWorld and refund the bank. With the
		// flag off we keep the binary's persistent-corpse behavior.
		int isCorpse = (item->resourceEntity.entity.bodyType == CORPSE_BODYTYPE);
		if (!isCorpse || !feat(FEAT_CLOSED_ECONOMY)) {
			int w = ((int (*)(void *))VT_FN(item, VT_GET_WEIGHT))(item);
			if (w >= 0xFF) {
				// CUSTOM (FEAT_AUTOFILL_CITY): heavy fillable
				// containers (bookcases, weight-255 chests) at
				// their home location in a city or justice
				// region still need the decay event so their
				// attached fill script can run. The post-event
				// at-home gate below zeroes decayCount before
				// any accumulation, preserving the homed-item
				// invariant. Items without home= short-circuit
				// inside CEntity_IsAtHome at the tag lookup, so
				// statues, walls, and other heavy decoration
				// keep the binary-faithful early exit.
				if (!feat(FEAT_AUTOFILL_CITY) || !CEntity_IsAtHome(item))
					return;
			}
		}
	}

	if (VT_IsRemoved(item)) {
		item->decayCount = 0;
		return;
	}

	if (VT_IsEquipped(item)) {
		if (feat(FEAT_HINT_RUMORS) && !Hint_WakeCarriedMagic(item))
			return;
		item->decayCount = 0;
		return;
	}

	if (((int (*)(void *))VT_FN(item, VT_HAS_CONTAINER))(item)) {
		if (feat(FEAT_HINT_RUMORS) && !Hint_WakeCarriedMagic(item))
			return;
		item->decayCount = 0;
		return;
	}

	if (!feat(FEAT_AUTOFILL_CITY)) {
		// Binary-faithful path: items at their home location inside a
		// city or justice region skip the decay event entirely.
		if (CEntity_IsAtHome(item)) {
			item->decayCount = 0;
			return;
		}
	}

	increment = (int)CWorld_GetNonHomeDecayRate();

	serial = CMobile_GetSerial((CMobile *)item);

	eventResult = (int)(intptr_t)Entity_ExecuteEvent(&item->resourceEntity.entity, 0x31, (uint32_t)item->decayCount, (uint32_t)((int)item->decayCount - increment));

	if (eventResult == 0)
		return;

	// Script may have deleted the entity; re-lookup by serial before use.
	if (CWorld_FindBySerial(g_World, serial) != (CItem *)item)
		return;

	// CUSTOM (FEAT_AUTOFILL_CITY): with the binary-faithful gate above
	// disabled, the script (e.g. fillcontainer.m) has already run; now
	// suppress decay accumulation for at-home items so the chest is not
	// decayed away by PlaceInWorld. nodecay.m already returns 0 from the
	// decay trigger for these chests and would short-circuit at the
	// eventResult==0 check above, but a homed item without nodecay
	// attached would otherwise fall through to PlaceInWorld - this gate
	// preserves the binary-faithful "homed items never accumulate decay"
	// invariant.
	if (feat(FEAT_AUTOFILL_CITY) && CEntity_IsAtHome(item)) {
		item->decayCount = 0;
		return;
	}

	if (g_ScriptReturnFlag == 1) {
		increment = g_ScriptReturnValue;
		g_ScriptReturnFlag = 0;
		g_ScriptReturnValue = 0;
	}

	newDecay = (int)item->decayCount + increment;

	if (newDecay >= (int)CItem_GetGlobalDecayMax(item)) {
		// CUSTOM (FEAT_CLOSED_ECONOMY): refund undrained type-3 production
		// nodes to the regional bank before the decay path deletes the
		// item. The helper's value3<=0 guard skips nodes that harvest
		// scripts have already drained, so a butchered-then-decayed
		// corpse is not double-credited. Homeless items (typical corpses)
		// reach this path once and are deleted via CItem_PlaceInWorld;
		// the rare homed item with type-3 nodes can re-enter the decay
		// cycle after being bounced back home, and would re-credit
		// undrained nodes - shipped data has no such case. NPC corpses
		// with type-3 meat/hide/wool nodes flow through here when no one
		// harvested them, closing Raph Koster's loop without a separate
		// corpse-decay hook.
		if (feat(FEAT_CLOSED_ECONOMY))
			RefundResourceNodesToBank(item);
		CItem_PlaceInWorld(item, 0);
	} else {
		item->decayCount = (uint8_t)newDecay;
	}
}

/*
 * 0x004594D4 - CItem::PlaceInWorld
 *
 * Final-stage decay handler. Homed items optionally run VT_DECAY_PLACE
 * and return home; homeless items run VT_DECAY_PLACE (placeFlag 0) or
 * VT_DECAY_CLEANUP (placeFlag non-zero), then are deleted unless the
 * entity is a player.
 */
void
CItem_PlaceInWorld(CItem *item, int placeFlag)
{
	item->decayCount = 0;

	if (!World_IsEntityInHash(item))
		return;

	if (CItem_HasHome(item)) {
		if (placeFlag == 0 && item->resourceEntity.entity.removedFromWorld == 0) {
			if (VT_IsMobile2(item)) {
				if (!VT_IsMobile(item)) {
					if (!Item_IsSpecialBodyType(item)) {
						void *loc = ((void *(*)(void *))VT_FN(item, VT_GET_LOCATION))(item);
						((void (*)(void *, void *))VT_FN(item, VT_DECAY_PLACE))(item, loc);
					}
				}
			}
		}
		CItem_ReturnToHome(item);
		return;
	}

	if (placeFlag == 0) {
		if (item->resourceEntity.entity.removedFromWorld == 0) {
			if (VT_IsMobile2(item)) {
				if (!VT_IsMobile(item)) {
					if (!Item_IsSpecialBodyType(item)) {
						void *loc = ((void *(*)(void *))VT_FN(item, VT_GET_LOCATION))(item);
						((void (*)(void *, void *))VT_FN(item, VT_DECAY_PLACE))(item, loc);
					}
				}
			}
		}
	} else {
		((void (*)(void *))VT_FN(item, VT_DECAY_CLEANUP))(item);
	}

	if (!VT_IsPlayer(item)) {
		if (item != NULL)
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);
	}
}

/*
 * 0x0045B850 - CItem::TryEquipOnMobile
 *
 * Validates that the item has a resource entry, the TF_EQUIPPABLE flag,
 * and a layer below 30, then delegates to vtable[0xC0] EquipOnMobile.
 */
int
CItem_TryEquipOnMobile(CItem *item, CItem *mob)
{
	int layer;

	if (!CWorld_LookupItemResource(CEntity_GetBodyType(item)))
		return 0;

	if (!(g_ItemTileData[CEntity_GetBodyType(item) & 0xFFFF].flags & TF_EQUIPPABLE))
		return 0;

	layer = CItem_GetEquipSlot(item) & 0xFF;
	if (layer >= 0x1E)
		return 0;

	return ((int (*)(void *, void *, int))VT_FN(item, VT_EQUIP_ON_MOBILE))(item, mob, (uint8_t)layer);
}

/*
 * 0x0045E57D - CItem::HasResourceFlag (vtable[0x20])
 *
 * True when the body type is below 0x4000, stackable, and satisfies the
 * resource recipe from g_ResEntitySlots.
 */
int
CItem_HasResourceFlag(CItem *item)
{
	uint16_t bodyType;
	uint32_t tileFlags;

	bodyType = CEntity_GetBodyType(item);
	if (bodyType >= 0x4000)
		return 0;

	tileFlags = g_ItemTileData[bodyType].flags;
	if (!(tileFlags & TF_STACKABLE))
		return 0;

	if (!CItem_HasResourceRecipe(item))
		return 0;
	return 1;
}

/*
 * 0x0045E5D4 - CResourceEntity::GetResourceAmount (vtable[0xA0])
 *
 * Sums value1 across all type-3 resource nodes matching resourceId. Used
 * by CItem_HasPositiveResources, which dispatches via the template slot's
 * vtable: slot lookups hit this overload and report the template's
 * required quantity (value1), while item lookups hit the CItem variant
 * below and report the live stack quantity (value3).
 */
int
CResourceEntity_GetResourceAmount(CItem *item, uint16_t resourceId)
{
	CResourceNode *node;
	int total;

	total = 0;
	node = item->resourceEntity.firstChild;
	while (node != NULL) {
		if (node->type == 3 && node->id == resourceId)
			total += node->value1;
		node = node->next;
	}
	return total;
}

/*
 * 0x0045E636 - CItem::GetResourceAmount (vtable[0xA0])
 *
 * Sums value3 across all type-3 resource nodes matching resourceId.
 */
int
CItem_GetResourceAmount(CItem *item, uint16_t resourceId)
{
	CResourceNode *node;
	int total;

	total = 0;
	node = item->resourceEntity.firstChild;
	while (node != NULL) {
		if (node->type == 3 && node->id == resourceId)
			total += node->value3;
		node = node->next;
	}
	return total;
}

/*
 * 0x0045E698 - CItem::HasPositiveResources
 *
 * Returns 1 only when every type-3 resource node reports a strictly
 * positive amount against the item's template slot.
 */
int
CItem_HasPositiveResources(CItem *self)
{
	uint16_t bodyType;
	ResEntitySlot *slot;
	void *node;
	int8_t nodeType;
	int result;

	bodyType = CEntity_GetBodyType((CItem *)self);
	slot = &g_ResEntitySlots[(bodyType & 0xFFFF)];
	node = (void *)self->resourceEntity.firstChild;

	while (node != NULL) {
		CResourceNode *rnode = (CResourceNode *)node;
		nodeType = rnode->type;
		if (nodeType == 3) {
			uint16_t resTypeId = rnode->id;
			vfunc_t *slotVtable = *(vfunc_t **)slot;
			result = ((int (*)(ResEntitySlot *, uint16_t))slotVtable[VT_GET_RESOURCE_AMT / 4])(slot, resTypeId);
			if (result <= 0)
				return 0;
		}
		node = (void *)rnode->next;
	}
	return 1;
}

/*
 * 0x0045E70F - HasResourceRecipe
 *
 * True when every type-3 requirement on the template slot is satisfied
 * by the item's GetResourceAmount.
 */
int
CItem_HasResourceRecipe(CItem *item)
{
	uint16_t bodyType;
	ResEntitySlot *slot;
	CResourceNode *node;

	bodyType = CEntity_GetBodyType(item);
	slot = &g_ResEntitySlots[bodyType];
	node = slot->nodeHead;
	while (node != NULL) {
		if (node->type == 3) {
			int amount = ((int (*)(void *, uint16_t))VT_FN(item, VT_GET_RESOURCE_AMT))(item, node->id);
			if (amount < node->value1)
				return 0;
		}
		node = node->next;
	}
	return 1;
}

/*
 * 0x0045E785 - GetMinResourceRatio
 *
 * Minimum of item_amount / required_amount across the template's type-3
 * requirements; 0 if any resource is missing entirely.
 */
int
CItem_GetMinResourceRatio(CItem *item)
{
	uint16_t bodyType;
	ResEntitySlot *slot;
	CResourceNode *node;
	int result;

	result = 0;
	bodyType = CEntity_GetBodyType(item);
	slot = &g_ResEntitySlots[bodyType];
	node = slot->nodeHead;
	while (node != NULL) {
		if (node->type == 3) {
			int amount = ((int (*)(void *, uint16_t))VT_FN(item, VT_GET_RESOURCE_AMT))(item, node->id);
			int ratio = amount / node->value1;
			if (result == 0 || ratio < result)
				result = ratio;
		}
		node = node->next;
	}
	return result;
}

/*
 * 0x0045E820 - CItem::HasEnoughResources
 *
 * True when the item can supply amount copies of every type-3
 * requirement from the given template slot.
 */
int
CItem_HasEnoughResources(CItem *item, ResEntitySlot *slot, int amount)
{
	CResourceNode *node;
	int ratio;

	node = slot->nodeHead;
	while (node != NULL) {
		if (node->type == 3) {
			ratio = ((int (*)(void *, uint16_t))VT_FN(item, VT_GET_RESOURCE_AMT))(item, node->id) / node->value1;
			if (ratio < amount)
				return 0;
		}
		node = node->next;
	}
	return 1;
}

/*
 * 0x0045E895 - CItem::ConsumeResource
 *
 * Subtracts up to amount of resourceId from the item's type-3
 * resource nodes, unlinking and freeing any node fully consumed.
 */
void
CItem_ConsumeResource(CItem *item, uint16_t resourceId, int amount)
{
	CResourceNode *node, *next;
	CLocation loc;

	if (!item->resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPreModify(item);

	node = item->resourceEntity.firstChild;
	while (node != NULL) {
		next = node->next;

		if (node->type != 3 || node->id != resourceId) {
			node = next;
			continue;
		}

		if (node->value3 > amount) {
			// Partial consume
			node->value3 -= amount;
			node->value1 -= amount;
			CLocation_Init(&loc);
			Spawn_ScheduleRespawn(&loc, node->id, amount);
			amount = 0;
			break;
		}

		// Consume entire node
		amount -= node->value3;
		ResourceEntity_NotifyNodeRemoval(node);
		CResourceEntity_RemoveNode(item, node);
		ResourceNode_ReturnToPool(node);

		if (amount == 0)
			break;
		node = next;
	}

	if (!item->resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPostModifyIfActive(item);
}

/*
 * 0x0045E9AD - CItem::FinalizeConsume
 *
 * Trims each template resource down to newRatio * required, where
 * newRatio = GetMinResourceRatio() - amountToReduce. Deletes the item
 * and returns 0 if newRatio hits zero.
 */
int
CItem_FinalizeConsume(CItem *item, int amountToReduce)
{
	int newRatio;
	uint16_t bodyType;
	ResEntitySlot *slot;
	CResourceNode *node;
	int proportionalAmount, currentAmount;

	newRatio = CItem_GetMinResourceRatio(item) - amountToReduce;
	if (newRatio < 0)
		newRatio = 0;

	bodyType = CEntity_GetBodyType(item);
	slot = &g_ResEntitySlots[bodyType];

	node = slot->nodeHead;
	while (node != NULL) {
		if (node->type == 3) {
			proportionalAmount = node->value1 * newRatio;
			currentAmount = ((int (*)(void *, uint16_t))VT_FN(item, VT_GET_RESOURCE_AMT))(item, node->id);
			if (currentAmount > proportionalAmount) {
				CItem_ConsumeResource(item, node->id, currentAmount - proportionalAmount);
			}
		}
		node = node->next;
	}

	if (newRatio == 0) {
		if (item != NULL)
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);
		return 0;
	}
	return 1;
}

/*
 * 0x0045EA86 - CItem::ConsumeAmount
 *
 * Transfers each type-3 requirement of source's template from dest into
 * source at value1 * amount, then runs FinalizeConsume(0) on dest to
 * normalize the residual.
 */
int
CItem_ConsumeAmount(CItem *dest, CItem *source, int amount)
{
	uint16_t bodyType;
	ResEntitySlot *slot;
	CResourceNode *node;

	if (amount <= 0)
		goto finalize;

	bodyType = CEntity_GetBodyType(source);
	slot = &g_ResEntitySlots[bodyType];

	CItem_HasEnoughResources(dest, slot, amount);

	node = slot->nodeHead;
	while (node != NULL) {
		if (node->type == 3) {
			CResourceEntity_TransferResources(source, dest, node->value1 * amount, node->id);
		}
		node = node->next;
	}

finalize:
	return CItem_FinalizeConsume(dest, 0);
}

/*
 * 0x0045EB18 - CItem::TransferResourcesByTemplate
 *
 * Transfers value1 * amount of each type-3 resource from dest into
 * source, keyed by source's template slot. The first two
 * CEntity_GetBodyType calls are dead code in the binary - preserved to
 * match disassembly.
 */
void
CItem_TransferResourcesByTemplate(CItem *source, CItem *dest, int amount)
{
	uint16_t bodyType;
	ResEntitySlot *slot;
	CResourceNode *node;

	// Dead-code calls preserved to match the binary.
	CEntity_GetBodyType(dest);
	CEntity_GetBodyType(source);

	bodyType = CEntity_GetBodyType(source);

	slot = &g_ResEntitySlots[bodyType & 0xFFFF];

	node = slot->nodeHead;
	while (node != NULL) {
		if (node->type == 3) {
			CResourceEntity_TransferResources(dest, source, node->value1 * amount, node->id);
		}
		node = node->next;
	}
}

/*
 * 0x0045EB9A - CItem::FinalizeConsume
 *
 * Trampoline that forwards this and amountToReduce to CItem_FinalizeConsume.
 */
int
CItem_FinalizeConsume_VT(CItem *this, int amountToReduce)
{
	return CItem_FinalizeConsume(this, amountToReduce);
}

/*
 * 0x0045EBC0 - CEditorObj::CEditorObj
 *
 * Global CEditorObj constructor: initializes the base CResListNode and
 * clears gmCallStatus.
 */
static __attribute__((unused)) void *
CEditorObj_Constructor(CEditorObj *self)
{
	CResListNode_Constructor_bin(&self->node);
	CEditorObj_SetGMCallStatus(self, 0);
	return self;
}

/*
 * 0x0045F196 - CEditorObj::HandleGMSingle
 *
 * GM editor delegate stub that always returns 0.
 */
int
CEditorObj_HandleGMSingle(CEditorObj *this, void *arg)
{
	USED(this);
	USED(arg);
	return 0;
}

/*
 * 0x0045F1A5 - CEditorObj::SetGMCallStatus
 *
 * Stores value into the editor object's gmCallStatus. Aliased to
 * g_GMCallStatus when invoked on g_GMPlayerList.
 */
void
CEditorObj_SetGMCallStatus(CEditorObj *this, int value)
{
	this->gmCallStatus = value;
}

/*
 * 0x0045F1BB - CEditorObj::GetGMCallStatus
 *
 * Returns the editor object's gmCallStatus. Aliased to g_GMCallStatus
 * when invoked on g_GMPlayerList.
 */
int
CEditorObj_GetGMCallStatus(CEditorObj *this)
{
	return this->gmCallStatus;
}

/*
 * 0x0045FF20 - CItem::CollectSurfaceItems
 *
 * Walks the spatial grid at (entity X, entity Y, z + surfaceHeight) and
 * appends each not-in-world item above the surface to outList, skipping
 * self and duplicates. Inner helper for GetContainerItems.
 */
static void
CItem_CollectSurfaceItems(CItem *this, CVector *outList)
{
	CVector localVec;
	char typeFlag;
	CLocation loc;
	int maxZ;
	uintptr_t *iter;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CLocation_Init(&loc);
	CLocation_SetLoc(&loc, &this->resourceEntity.entity.location);

	// Surface height narrows to int16 before sign-extending into maxZ.
	loc.z = (int16_t)((int16_t)loc.z + (int16_t)((int (*)(void *))VT_FN(this, VT_GET_SURFACE_H))(this));
	maxZ = (int)(int16_t)loc.z;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc.x, (int16_t)loc.y))
		goto cleanup;

	if (!(((int (*)(void *))VT_FN(this, VT_GET_FLAGS))(this) & 0x200))
		goto cleanup;

	CBlockManager_GetItemsAtLocationXYZ(&g_SpatialGrid, &localVec, &loc);

	Vector_SortByZ(localVec.begin, localVec.end, localVec.type);

	iter = (uintptr_t *)localVec.begin;
	while (iter != (uintptr_t *)localVec.end) {
		CItem *found = (CItem *)*iter;
		int16_t foundZ = found->resourceEntity.entity.location.z;

		if ((int)foundZ > maxZ)
			break;

		maxZ = (int)foundZ + ((int (*)(void *))VT_FN(found, VT_GET_SURFACE_H))(found);

		if (Vector_Find(outList->begin, outList->end, iter) != outList->end) {
			iter++;
			continue;
		}

		if (found == this) {
			iter++;
			continue;
		}

		if (!((int (*)(void *))VT_FN(found, VT_IS_IN_WORLD))(found))
			CVector_PushBack(outList, (uintptr_t)found);

		iter++;
	}

cleanup:
	CVector_Destructor(&localVec);
}

/*
 * 0x004600E3 - CItem::GetContainerItems
 *
 * Collects surface items under this entity and pushes their serials
 * into list.
 */
void
CItem_GetContainerItems(CItem *ent, CVector *list)
{
	CVector localVec;
	char typeFlag;
	uintptr_t *iter;

	typeFlag = 0;
	CVector_Constructor(&localVec, &typeFlag);

	CItem_CollectSurfaceItems(ent, &localVec);

	iter = (uintptr_t *)localVec.begin;
	while (iter != (uintptr_t *)localVec.end) {
		CItem *item = (CItem *)*iter;
		uint32_t serial = CMobile_GetSerial((CMobile *)item);
		CVector_PushBack(list, serial);
		iter++;
	}

	CVector_Destructor(&localVec);
}

/*
 * 0x00467080 - CItem::UpdateContainInfo
 *
 * Refreshes the lazily-allocated CItemTracking struct so it reflects
 * the item's current equipment slot, parent container, or ground
 * location. forceUpdate controls whether equipment[0] membership short
 * circuits the refresh.
 */
void
CItem_UpdateContainInfo(CItem *item, int forceUpdate)
{
	int isOnGround;
	int i;
	CItemTracking *tr;
	CMobile *mob;
	CLocation *topLoc;

	if (item->tracking == NULL)
		item->tracking = Item_AllocContainInfo();

	isOnGround = 0;

	if (!((int (*)(void *))VT_FN(item, VT_IS_EQUIPPED_ITEM))(item)) {
		if (item->parent == NULL || (item->parent != NULL && ((CContainer *)item->parent)->lockOwner == NULL))
			isOnGround = 1;
		goto check_ground;
	}

	if (!forceUpdate) {
		mob = (CMobile *)item->parent;
		if (mob->equipment[0] == item)
			goto check_ground;
	}

	tr = item->tracking;
	mob = (CMobile *)item->parent;
	tr->lastMob = CMobile_GetSerial(mob);
	tr->lastMobEqPos = 0x1A;

	i = forceUpdate ? 0 : 1;
	for (; i < 30; i++) {
		if (mob->equipment[i] == item) {
			tr->lastMobEqPos = (uint16_t)i;
			break;
		}
	}

	tr->lastCont = 0;
	CLocation_Invalidate(&tr->lastContLoc);

	topLoc = ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item);
	CLocation_SetLoc(&tr->lastLoc, topLoc);

check_ground:
	if (!isOnGround)
		return;

	tr = item->tracking;
	tr->lastMob = 0;
	tr->lastMobEqPos = 0x1A;

	if (item->parent != NULL) {
		tr->lastCont = CMobile_GetSerial((CMobile *)item->parent);
		CLocation_SetLoc(&tr->lastContLoc, &item->resourceEntity.entity.location);
		topLoc = ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item);
		CLocation_SetLoc(&tr->lastLoc, topLoc);
	} else {
		tr->lastCont = 0;
		CLocation_Invalidate(&tr->lastContLoc);
		topLoc = ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item);
		CLocation_SetLoc(&tr->lastLoc, topLoc);
	}
}

// 0x00647090 - CItemTracking pool free list head
static CItemTracking *g_TrackingFreeList;

/*
 * 0x00467248 - Item_AllocContainInfo
 *
 * Pool allocator for CItemTracking: pops g_TrackingFreeList, refilling
 * it from a fresh 0x1000-entry block when empty, and returns a zeroed
 * entry.
 */
CItemTracking *
Item_AllocContainInfo(void)
{
	static int poolCreated;
	CItemTracking *result;
	CItemTracking *block;
	int i;

	if (!poolCreated) {
		VG_CREATE_POOL(&g_TrackingFreeList);
		poolCreated = 1;
	}

	if (g_TrackingFreeList != NULL) {
		result = g_TrackingFreeList;
		VG_POOL_ALLOC(&g_TrackingFreeList, result, sizeof(CItemTracking));
		VG_MAKE_DEFINED(&result->freeNext, sizeof(result->freeNext));
		g_TrackingFreeList = result->freeNext;
	} else {
		block = (CItemTracking *)OperatorNew(sizeof(CItemTracking) * 0x1000);
		if (block != NULL) {
			ArrayIterator_ForEach(block, sizeof(CItemTracking), 0x1000, (void (*)(void *))CItemTracking_Init);
			result = block;
		} else {
			result = NULL;
		}

		for (i = 0xFFF; i >= 1; i--) {
			result[i].freeNext = g_TrackingFreeList;
			g_TrackingFreeList = &result[i];
		}
		VG_POOL_ALLOC(&g_TrackingFreeList, result, sizeof(CItemTracking));
	}

	CLocation_Invalidate(&result->lastLoc);
	CLocation_Invalidate(&result->lastContLoc);
	result->lastCont = 0;
	result->lastMob = 0;
	result->lastMobEqPos = 0x1A;

	return result;
}

/*
 * 0x00467357 - CItem::ReleaseTracking
 *
 * Frees the item's container-tracking struct via FreeContainInfo and
 * clears the pointer.
 */
void
CItem_ReleaseTracking(CItem *item)
{
	if (item->tracking != NULL) {
		FreeContainInfo(item->tracking);
		item->tracking = NULL;
	}
}

/*
 * 0x00467384 - FreeContainInfo
 *
 * Returns a CItemTracking node to the free list.
 */
void
FreeContainInfo(CItemTracking *tr)
{
	tr->freeNext = g_TrackingFreeList;
	g_TrackingFreeList = tr;
	VG_POOL_FREE(&g_TrackingFreeList, tr);
}

/*
 * 0x0046739E - CItem::SetLastContainer
 *
 * Records the last-seen parent serial on the item's tracking struct,
 * allocating it lazily.
 */
void
CItem_SetLastContainer(CItem *item, uint32_t serial)
{
	if (item->tracking == NULL)
		item->tracking = Item_AllocContainInfo();
	item->tracking->lastCont = serial;
}

/*
 * 0x004673CB - CItem::SetLastMobile
 *
 * Records the last-seen owner mobile serial on the item's tracking
 * struct, allocating it lazily.
 */
void
CItem_SetLastMobile(CItem *item, uint32_t serial)
{
	if (item->tracking == NULL)
		item->tracking = Item_AllocContainInfo();
	item->tracking->lastMob = serial;
}

/*
 * 0x004673F8 - CItem::SetLastMobileEquipPos
 *
 * Records the last-seen equipment slot on the item's tracking struct,
 * allocating it lazily.
 */
void
CItem_SetLastMobileEquipPos(CItem *item, uint16_t pos)
{
	if (item->tracking == NULL)
		item->tracking = Item_AllocContainInfo();
	item->tracking->lastMobEqPos = pos;
}

/*
 * 0x00467427 - CItem::SetLastLocation
 *
 * Records the last-seen world location on the item's tracking struct,
 * allocating it lazily.
 */
void
CItem_SetLastLocation(CItem *item, int16_t x, int16_t y, int16_t z)
{
	if (item->tracking == NULL)
		item->tracking = Item_AllocContainInfo();
	CLocation_Set(&item->tracking->lastLoc, x, y, z);
}

/*
 * 0x00467462 - CItem::SetLastContainerLocation
 *
 * Records the last-seen container-local location on the item's tracking
 * struct, allocating it lazily.
 */
void
CItem_SetLastContainerLocation(CItem *item, int16_t x, int16_t y, int16_t z)
{
	if (item->tracking == NULL)
		item->tracking = Item_AllocContainInfo();
	CLocation_Set(&item->tracking->lastContLoc, x, y, z);
}

/*
 * 0x004674A0 - CItemTracking::Init (static)
 *
 * Per-element initializer invoked by ArrayIterator_ForEach when populating
 * a fresh block of tracking nodes.
 */
static void
CItemTracking_Init(CItemTracking *tr)
{
	CLocation_Init(&tr->lastLoc);
	CLocation_Init(&tr->lastContLoc);
}

/*
 * 0x00472F50 - CMobile::`scalar deleting destructor' (vtable[0])
 *
 * Runs ~CMobile (which chains to ~CItem) and frees the mobile when
 * flags & 1.
 */
void *
CMobile_ScalarDelete(CMobile *mob, int flags)
{
	CMobile_Destructor(mob);
	if (flags & 1)
		free(mob);
	return NULL;
}

/*
 * 0x00484CF0 - CItem::DeleteCheck1
 *
 * Runs PrepareDelete, then returns 1 only if the entity is still
 * reachable via the serial hash - i.e. the delete event did not already
 * destroy it.
 */
int
CItem_DeleteCheck1(CItem *item)
{
	uint32_t serial;

	serial = item->serial;
	CItem_PrepareDelete(item);
	return (CWorld_FindBySerial(g_World, serial) == item) ? 1 : 0;
}

/*
 * 0x00484D28 - CItem::DeleteCheck2
 *
 * Runs vtable[0x1AC] PreDeleteCleanup, then returns 1 only if the
 * entity survived and is still reachable via the serial hash.
 */
int
CItem_DeleteCheck2(CItem *item)
{
	uint32_t serial;

	serial = item->serial;
	((void (*)(void *))VT_FN(item, VT_DELETE_CONTENTS))(item);
	return (CWorld_FindBySerial(g_World, serial) == item) ? 1 : 0;
}

/*
 * 0x00484D66 - CItem::PrepareDelete
 *
 * Marks the item for deletion and fires the DeleteEntity script event,
 * unless it is already deleted or already flagged.
 */
void
CItem_PrepareDelete(CItem *item)
{
	if (CItem_IsDeleted(item))
		return;
	if (CItem_HasDeleteFlag(item))
		return;
	CItem_SetDeleteFlag(item, 1);
	Entity_ExecuteEvent(&item->resourceEntity.entity, DeleteEntity);
}

/*
 * 0x00484E8B - CItem::Delete (vtable[0x90])
 *
 * Runs DeleteCheck1 + DeleteCheck2, increments the item-destroyed
 * metric, then invokes the scalar deleting destructor (vtable[0](1)).
 */
void
CItem_Delete(CItem *item)
{
	if (!CItem_DeleteCheck1(item))
		return;
	if (!CItem_DeleteCheck2(item))
		return;

	if (item != NULL)
		((void *(*)(void *, int))VT_FN(item, VT_DTOR))(item, 1);
}

/*
 * 0x00484FC7 - CBoard::Delete (vtable[0x90])
 *
 * Same shape as CItem::Delete but without the metric increment.
 */
void
CBoard_Delete(CItem *self)
{
	if (!CItem_DeleteCheck1(self))
		return;
	if (!CItem_DeleteCheck2(self))
		return;
	if (self != NULL)
		((void *(*)(void *, int))VT_FN(self, VT_DTOR))(self, 1);
}

/*
 * 0x00485016 - CWeapon::Delete (vtable[0x90])
 *
 * Like CItem::Delete but skips DeleteCheck2.
 */
void
CWeapon_Delete(CItem *self)
{
	if (!CItem_DeleteCheck1(self))
		return;
	if (self != NULL)
		((void *(*)(void *, int))VT_FN(self, VT_DTOR))(self, 1);
}

/*
 * 0x004854F2 - CItem::GetItemAmount (vtable[0x98])
 *
 * Returns GetMinResourceRatio for resource items, or 1 otherwise.
 */
int
CItem_GetItemAmount(CItem *item)
{
	if (((int (*)(void *))VT_FN(item, VT_HAS_RESOURCE_FLAG))(item))
		return CItem_GetMinResourceRatio(item);
	return 1;
}

/*
 * 0x004854F2 - CItem::IsMovable (vtable[0x98])
 *
 * Direct-call alias of GetItemAmount used by wombat scripts. Returns 1
 * for non-resource items, otherwise the minimum resource ratio (0 when
 * under-stocked).
 */
int
CItem_IsMovable(CItem *item)
{
	if (!CItem_HasResourceFlag(item))
		return 1;
	return CItem_GetMinResourceRatio(item);
}

/*
 * 0x00485D14 - CItem::FindResourceNodeByIdAndType
 *
 * Returns the first resource node matching (resType->typeId, type), or
 * NULL. Nodes with id == 0 are skipped.
 */
CResourceNode *
CItem_FindResourceNodeByIdAndType(CItem *item, CResourceType *resType, int8_t type)
{
	CResourceNode *node;
	uint16_t nodeId;

	node = item->resourceEntity.firstChild;
	while (node != NULL) {
		nodeId = node->id;
		if (nodeId != 0) {
			if (nodeId == resType->typeId && (int8_t)type == (int8_t)node->type)
				return node;
		}
		node = node->next;
	}
	return NULL;
}

/*
 * 0x00485E28 - CItem::GetResourceNodeValue
 *
 * Writes node->value1/2/3 (selected by valueIndex 0..2) to *resultOut
 * and returns 1, or returns 0 when the index is out of range.
 */
static int
CItem_GetResourceNodeValue(CItem *item, CResourceNode *node, int valueIndex, int *resultOut)
{
	USED(item);

	if (valueIndex < 0 || valueIndex > 2)
		return 0;

	*resultOut = (&node->value1)[valueIndex];
	return 1;
}

/*
 * 0x00485E65 - CItem::AddToResourceNodeValue
 *
 * Adds delta to node->value1/2/3 (selected by valueIndex 0..2) and
 * returns 1, or returns 0 when the index is out of range. The item is
 * not used.
 */
static __attribute__((unused)) int
CItem_AddToResourceNodeValue(CItem *item, CResourceNode *node, int valueIndex, int delta)
{
	USED(item);

	if (valueIndex < 0 || valueIndex > 2)
		return 0;

	(&node->value1)[valueIndex] += delta;
	return 1;
}

/*
 * 0x00485EB9 - CItem::Setup
 *
 * Seeds the secondary location (when currently -1 and type is 0), then
 * copies scaled resource nodes from the item's template when the
 * container/flag path is taken, falling back to g_ResEntitySlots[body].
 */
void
CItem_Setup(CItem *item, int type, CLocation *loc, int flags, int expand)
{
	{
		int16_t *resLocX = (int16_t *)&item->resourceEntity.nextInContainer;
		if (*resLocX == -1 && type == 0)
			CLocation_SetLoc((CLocation *)&item->resourceEntity.nextInContainer, loc);
	}

	{
		CResourceNode *node;

		if (flags == 0)
			goto fallback;
		if (VT_IsContainer(item) == 0)
			goto fallback;
		{
			uint16_t tid = (uint16_t)(CResourceEntity_GetTemplateIndex(item) & 0xFFFF);
			if (tid == 0)
				goto fallback;
			if (!CResManager_HasByInt(Spawn_GetTemplatesRM(), tid))
				goto done;
			{
				NPCTemplate *tmpl = CResManager_GetTemplateByID(tid);
				node = tmpl->resourceNodes;
				if (node == NULL)
					goto fallback;
			}
			while (node != NULL) {
				CResourceEntity_CopyNodeScaled(item, node, type, 1, expand);
				node = node->next;
			}
			goto done;
		}
fallback: {
	uint16_t bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	node = g_ResEntitySlots[bodyType].nodeHead;
	while (node != NULL) {
		CResourceEntity_CopyNodeScaled(item, node, type, 1, expand);
		node = node->next;
	}
}
done:;
	}
}

/*
 * 0x004861AE - CItem::GetObjVarResTypeInner
 *
 * Returns GetResourceNodeValue(FindResourceNodeByIdAndType(...)); 0 if
 * the node isn't present.
 */
int
CItem_GetObjVarResTypeInner(CItem *item, int *resultOut, CResourceType *resType, int8_t type, int valueIndex)
{
	CResourceNode *node;

	node = CItem_FindResourceNodeByIdAndType(item, resType, type);
	if (node == NULL)
		return 0;
	return CItem_GetResourceNodeValue(item, node, valueIndex, resultOut);
}

/*
 * 0x004861F4 - CItem::GetObjVarResType
 *
 * Resolves resTypeId through g_ResourceTypeManager, then forwards to
 * GetObjVarResTypeInner; returns 0 when the type ID is unknown.
 */
int
CItem_GetObjVarResType(CItem *item, int *resultOut, int resTypeId, int8_t type, int valueIndex)
{
	CResourceType *resType;

	resType = CResourceTypeManager_GetId(resTypeId);
	if (resType == NULL)
		return 0;
	return CItem_GetObjVarResTypeInner(item, resultOut, resType, type, valueIndex);
}

/*
 * 0x00486236 - CItem::GetObjVarResTypeByName
 *
 * As CItem_GetObjVarResType but resolves the resource type by name
 * rather than by id. The name arrives as a by-value CString the caller
 * owns the buffer of and this function destroys.
 */
static __attribute__((unused)) int
CItem_GetObjVarResTypeByName(CItem *item, int *resultOut, CString resTypeName, int8_t type, int valueIndex)
{
	CResourceType *resType;
	int result;

	resType = CResourceTypeManager_FindByName(CString_GetBuffer(&resTypeName));
	if (resType == NULL) {
		result = 0;
		CString_Destructor(&resTypeName);
		return result;
	}

	result = CItem_GetObjVarResTypeInner(item, resultOut, resType, type, valueIndex);
	CString_Destructor(&resTypeName);
	return result;
}

/*
 * 0x004863D1 - CItem vtable[0x40] GetSurfaceFlags
 *
 * Maps tiledata flags to terrain-collision bits (0x40 surface, 0x200
 * impassable, 0x400 bridge) per movement type. Case 6 additionally
 * clears the surface bit for door/container body types that movement
 * type 6 treats as walkable.
 */
int
CItem_GetSurfaceFlags_VT(CItem *self, int moveType)
{
	int result;
	int flags;
	int bodyType;

	result = 0;
	flags = ((int (*)(void *))VT_FN(self, VT_GET_FLAGS))(self);

	switch (moveType) {
	case 0:
		if (flags & 0x240)
			result |= 0x40;
		if (flags & 0x200)
			result |= 0x200;
		if (flags & 0x400)
			result |= 0x400;
		break;
	case 1:
	case 6:
	case 8:
		if (flags & 0x240)
			result |= 0x40;
		if (!(flags & 0x40)) {
			if (flags & 0x200)
				result |= 0x200;
			if (flags & 0x400)
				result |= 0x400;
		}
		// moveType 6 shares case 1/8 but then clears the blocking bit
		// for doors, specific body types, and moveable containers.
		if (moveType == 6) {
			bodyType = CEntity_GetBodyType(self) & 0xFFFF;
			if (bodyType == 0x692 || (bodyType >= 0x6F5 && bodyType <= 0x6F6) || bodyType == 0x846 || bodyType == 0x873) {
				result &= ~0x40;
			} else {
				if (VT_IsContainer(self) && ((int (*)(void *, int))VT_FN(self, VT_IS_MOVEABLE))(self, 0)) {
					result &= ~0x40;
				} else if (flags & 0x20000000) {
					result &= ~0x40;
				}
			}
		}
		break;
	case 2:
		if (flags & 0x240)
			result |= 0x40;
		if (((int (*)(void *))VT_FN(self, VT_GET_HEIGHT))(self) < 0x14 && !(flags & 0x10))
			result |= 0x200;
		if (flags & 0x400)
			result |= 0x400;
		break;
	case 3:
	case 4:
		if (flags & 0x80)
			result |= 0x200;
		else
			result |= 0x40;
		break;
	case 5:
		if (flags & 0x240)
			result |= 0x40;
		if ((flags & 0x200) && !(flags & 0x40))
			result |= 0x200;
		if (flags & 0x400)
			result |= 0x400;
		if (flags & 0x80)
			result |= 0x600;
		break;
	case 7:
		return 0x200;
	default:
		break;
	}

	return result;
}

/*
 * 0x004866C7 - CEntity vtable[0xDC] / CResourceEntity vtable[0xEC]
 *
 * Shared slot that returns GetLandTileFlags(tileID, 0) and ignores
 * this.
 */
int
CEntity_CheckDC_VT(CItem *self, uint16_t tileID)
{
	USED(self);
	return GetLandTileFlags(tileID, 0);
}

/*
 * 0x004866E2 - CEntity/CItem vtable[0x48]
 *
 * Forwards to entity->GetSurfaceFlags(moveType=0) and ignores this.
 */
int
CEntity_CheckSurfaceOf_VT(CItem *self, CItem *entity)
{
	USED(self);
	return ((int (*)(void *, int))VT_FN(entity, VT_GET_SURFACE_FLAGS))(entity, 0);
}

/*
 * 0x00486706 - CreateStaticEntity
 *
 * Pool allocator for static entities: pops g_StaticFreeList and refills
 * it with a fresh 0x1000-entry block when empty. Returns an entity with
 * its prev/next links cleared.
 */
CItem *
CreateStaticEntity(void)
{
	static int poolCreated;
	CItem *entity;
	char *pool;
	int i;

	if (!poolCreated) {
		VG_CREATE_POOL(&g_StaticFreeList);
		poolCreated = 1;
	}

	if (g_StaticFreeList != NULL) {
		entity = g_StaticFreeList;
		VG_POOL_ALLOC(&g_StaticFreeList, entity, sizeof(CResourceEntity));
		VG_MAKE_DEFINED(entity, sizeof(CResourceEntity));
		g_StaticFreeList = entity->resourceEntity.nextInContainer;
	} else {
		pool = calloc(0x1000, sizeof(CResourceEntity));
		if (pool == NULL)
			return NULL;

		for (i = 0; i < 0x1000; i++) {
			CItem *ent = (CItem *)(pool + i * sizeof(CResourceEntity));
			StaticEntity_Constructor(ent);
		}

		entity = (CItem *)pool;

		for (i = 0xFFF; i >= 1; i--) {
			CItem *ent = (CItem *)(pool + i * sizeof(CResourceEntity));
			ent->resourceEntity.nextInContainer = g_StaticFreeList;
			g_StaticFreeList = ent;
		}
		VG_POOL_ALLOC(&g_StaticFreeList, entity, sizeof(CResourceEntity));
		VG_MAKE_DEFINED(entity, sizeof(CResourceEntity));
	}

	g_StaticItemCount++;
	entity->resourceEntity.staticPrev = NULL;
	entity->resourceEntity.nextInContainer = NULL;

	return entity;
}

/*
 * 0x00486831 - StaticEntity::StaticEntity
 *
 * Minimal ctor for static entities: runs CEntity_Constructor and
 * installs the StaticEntity vtable. Intentionally skips CLocation_Init,
 * firstChild setup, and the g_ResourceEntityCount bump that
 * CResourceEntity_Constructor performs.
 */
void
StaticEntity_Constructor(CItem *self)
{
	CEntity_Constructor(&self->resourceEntity.entity);
	self->resourceEntity.entity.vtable = &g_vtable_StaticEntity;
}

/*
 * 0x00486850 - StaticEntity destructor
 *
 * Sets vtable to StaticEntity and calls CEntity_Destructor.
 */
static void
StaticEntity_Destructor(CItem *self)
{
	self->resourceEntity.entity.vtable = &g_vtable_StaticEntity;
	CEntity_Destructor(&self->resourceEntity.entity);
}

/*
 * 0x0048686C - FreeStaticItem
 *
 * Locks statics, decrements g_StaticItemCount, removes the item from
 * its block's static chain via vtable[0x0C] (RemoveFromWorld), adds it
 * to the g_StaticFreeList for reuse, and unlocks statics.
 */
void
FreeStaticItem(CItem *item)
{
	Static_Lock();

	g_StaticItemCount--;

	if (item->resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	item->resourceEntity.nextInContainer = g_StaticFreeList;
	g_StaticFreeList = item;
	VG_POOL_FREE(&g_StaticFreeList, item);

	Static_Unlock();
}

/*
 * 0x004868BA - CItem::CItem (constructor)
 *
 * Constructs the base CItem on top of a CResourceEntity, allocates a fresh
 * serial, registers the entity in the world's serial hash, and zeroes every
 * CItem field. Increments g_DynamicItemCount.
 *
 * MODIFIED: hash table insertion uses CWorld_InsertEntity, and
 * CEntity_SetType replaces the binary's raw vtable assignment.
 */
CItem *
CItem_Constructor(CItem *mem)
{
	CItem *item = mem;

	CResourceEntity_Constructor(&item->resourceEntity);
	CEntity_SetType(&item->resourceEntity.entity, ETYPE_ITEM);

	g_DynamicItemCount++;

	item->serial = CWorld_AllocSerial(g_World);

	item->itemFlags = 0;
	item->spatialPrev = NULL;
	item->spatialNext = NULL;
	item->parent = NULL;

	CWorld_InsertEntity(g_World, item);

	item->tracking = NULL;
	CItem_SetDecayCount(item, 0);
	item->tagList = NULL;
	CItem_InitTemplateChainPtrs(item);
	CResourceEntity_ResetTemplateIndex(item);
	item->multiPtr = NULL;
	item->timerHead = NULL;
	return item;
}

/*
 * 0x004869DA - CItem vtable[0x174] IsHidden
 *
 * Returns the invisible bit from itemFlags.
 */
int
CItem_IsHidden_VT(CItem *item)
{
	return CItem_HasItemFlag(item, 2);
}

/*
 * 0x004869EF - CItem::NotifyNearbyUpdate
 *
 * Gathers players within range 0x12 of the item and dispatches
 * vtable[0x130] SendUpdateToList with the given mode. No-ops when the
 * item has been removed from the world.
 */
void
CItem_NotifyNearbyUpdate(CItem *item, int mode)
{
	CLocation loc;
	CVector players;

	if (item->resourceEntity.entity.removedFromWorld)
		return;

	CLocation_SetLoc(&loc, ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item));

	CVector_Constructor(&players, "\x01");
	GetNearbyPlayers(&players, &loc, 0x12);

	((void (*)(void *, CVector *, int))VT_FN(item, VT_NOTIFY_NEARBY))(item, &players, mode);

	CVector_Destructor(&players);
}

/*
 * 0x00486A8A - CItem vtable[0x178] SetHidden
 *
 * Toggles the invisible bit, then notifies nearby players with the
 * inverse mode.
 */
void
CItem_SetHidden_VT(CItem *item, int set)
{
	CItem_SetItemFlag(item, 2, set);
	CItem_NotifyNearbyUpdate(item, set == 0 ? 1 : 0);
}

/*
 * 0x00486AD5 - CItem::SetItemFlag
 *
 * Sets or clears the specified itemFlag bits on item.
 */
void
CItem_SetItemFlag(CItem *item, uint8_t flag, int setOrClear)
{
	if (setOrClear)
		item->itemFlags |= flag;
	else
		item->itemFlags &= ~flag;
}

/*
 * 0x00486B11 - CItem::HasItemFlag
 *
 * Returns non-zero when any of the specified flag bits is set on item.
 */
int
CItem_HasItemFlag(CItem *item, int flag)
{
	return (item->itemFlags & (flag & 0xFF)) != 0;
}

/*
 * 0x00486B39 - CItem::SetServerOnly (vtable[0x34])
 *
 * Toggles the ItemFlag_ServerOnly bit (0x10), which gates whether clients
 * see the item.
 */
void
CItem_SetServerOnly(CItem *item, int set)
{
	if (set)
		item->itemFlags |= 0x10;
	else
		item->itemFlags &= ~0x10;
}

/*
 * 0x00486BDB - CItem::SetLockdown
 *
 * Sets or clears the lockdown bit (0x40) on the item's itemFlags.
 */
void
CItem_SetLockdown(CItem *item, int set)
{
	if (set)
		item->itemFlags |= 0x40;
	else
		item->itemFlags &= ~0x40;
}

/*
 * 0x00486C0E - CItem::HasMulti_Filter
 *
 * CTerrainManager_FindEntitiesAtXYZ callback wrapper for HasMulti.
 */
int
CItem_HasMulti_Filter(CItem *item)
{
	return CItem_HasMulti(item);
}

/*
 * 0x00486C27 - CItem::IsMultiOwner
 *
 * True when the item's multi component is present and reports ownership.
 */
int
CItem_IsMultiOwner(CItem *item)
{
	CMultiComponent *mc;

	if (item->multiPtr == NULL)
		return 0;
	mc = item->multiPtr;
	return CMultiComponent_IsOwner(mc);
}

/*
 * 0x00486C65 - CItem::AttachMultiComponent
 *
 * Pool-allocates a CMultiComponent bound to (ownerSerial, item, offset)
 * and stores it on the item, unless a multi component is already present.
 */
int
CItem_AttachMultiComponent(CItem *item, uint32_t ownerSerial, CLocation *offset)
{
	CMultiComponent *mc;

	if (CItem_HasMulti_Filter(item))
		return 0;

	mc = MultiComponent_AllocInit3(ownerSerial, item, offset);
	item->multiPtr = mc;
	return 1;
}

/*
 * 0x00486CA1 - CItem::SetMultiComponent
 *
 * Pool-allocates a CMultiComponent bound to the item (serial=0) and
 * stores it on the item, unless a multi component is already present.
 */
int
CItem_SetMultiComponent(CItem *item)
{
	CMultiComponent *mc;

	if (CItem_HasMulti_Filter(item))
		return 0;

	mc = MultiComponent_AllocInit2(0, item);
	item->multiPtr = mc;
	return 1;
}

/*
 * 0x00486CD5 - CItem::AttachMultiSlave
 *
 * Allocates a CMultiSlave bound to (item, offset) and stores it on the
 * item, unless a multi component is already present.
 * MODIFIED: malloc replaces operator new.
 */
int
CItem_AttachMultiSlave(CItem *item, CLocation *offset)
{
	CMultiSlave *ms;

	if (CItem_HasMulti_Filter(item))
		return 0;

	ms = malloc(sizeof(CMultiSlave));
	if (ms != NULL)
		CMultiSlave_Constructor_args(ms, item, offset);

	item->multiPtr = (CMultiComponent *)ms;
	return 1;
}

/*
 * 0x00486D64 - CItem::SetMultiSlave
 *
 * Allocates a zero-offset CMultiSlave and stores it on the item, unless
 * a multi component is already present.
 * MODIFIED: malloc replaces operator new.
 */
int
CItem_SetMultiSlave(CItem *item)
{
	CMultiSlave *ms;

	if (CItem_HasMulti_Filter(item))
		return 0;

	ms = malloc(sizeof(CMultiSlave));
	if (ms != NULL)
		CMultiSlave_Constructor(ms, item);

	item->multiPtr = (CMultiComponent *)ms;
	return 1;
}

/*
 * 0x00486E0E - CItem::DetachMulti
 *
 * Owner multis go through their scalar deleting destructor; component
 * multis are returned to the pool. Either way, multiPtr is cleared.
 */
void
CItem_DetachMulti(CItem *item)
{
	CMultiComponent *mc;

	if (item->multiPtr == NULL)
		return;

	mc = item->multiPtr;
	if (CMultiComponent_IsOwner(mc)) {
		CMultiComponent_Destroy(mc, 1);
	} else {
		MultiComponent_ReturnToPool(mc);
	}
	item->multiPtr = NULL;
}

/*
 * 0x00486E81 - CItem::NotifyMultiServerOnly
 *
 * For server-only items with a multi component, forwards the location
 * change to CMulti_NotifyComponentLoc.
 */
void
CItem_NotifyMultiServerOnly(CItem *item, CLocation *loc)
{
	if (CItem_IsServerOnly(item) != 1)
		return;
	if (item->multiPtr == NULL)
		return;
	CMulti_NotifyComponentLoc(item->multiPtr, loc);
}

/*
 * 0x00486EB3 - CItem::SendServerOnlyUpdate
 *
 * For server-only items with a multi component, forwards to
 * CMulti::SendPlayerInfo.
 */
void
CItem_SendServerOnlyUpdate(CItem *item, CItem *player)
{
	if (CItem_IsServerOnly(item) != 1)
		return;
	if (item->multiPtr == NULL)
		return;
	CMulti_SendPlayerInfo(item->multiPtr, player);
}

/*
 * 0x00486EE5 - CItem::ProcessMultiDeleteItems
 *
 * Walks the source serial list: items tagged multiDelete are deleted
 * outright; the rest have their serial pushed into dest for later
 * processing.
 */
static void
CItem_ProcessMultiDeleteItems(CItem *self, CVector *source, CVector *dest)
{
	uintptr_t *p;
	CItem *ent;
	uint32_t serial;

	for (p = (uintptr_t *)source->begin; p != (uintptr_t *)source->end; p++) {
		ent = CWorld_FindBySerial(g_World, (uint32_t)*p);
		if (ent == NULL)
			continue;
		if (ent == self)
			continue;

		if (CItem_HasMultiDeleteTag(ent)) {
			// Redundant null-check preserved to match the binary.
			if (ent != NULL)
				((void (*)(void *))VT_FN(ent, VT_DELETE))(ent);
		} else {
			serial = CMobile_GetSerial((CMobile *)ent);
			CVector_PushBack(dest, serial);
		}
	}
}

/*
 * 0x00486F73 - CItem::~CItem
 *
 * Dismantles any attached multi (deleting tagged passengers on an
 * owner), hides the item if still in world, tears down timers,
 * scripts, tags, hash links, and template bindings, then chains to
 * CResourceEntity_Destructor.
 * MODIFIED: CMulti pool operations collapse into CMulti_Free.
 *
 * FIXED: the binary's destructor never releases the item's lazy
 * tracking pointer. `CItem_HideVT` calls `UpdateContainInfo`, which
 * allocates a fresh `CItemTracking` when `item->tracking` is NULL,
 * just to record a value the destructor immediately throws away.
 * Add a `CItem_ReleaseTracking` after the teardown calls so the pool
 * node returns to the free list. Also covers `CContainer_Destructor`,
 * which chains here unconditionally.
 */
void
CItem_Destructor(CItem *item)
{
	CVector serialsVec;
	char typeFlag1 = 0;
	char typeFlag2 = 0;
	int wasContained;
	CMultiComponent *mc;
	uint16_t hashIdx;

	CVector_Constructor(&serialsVec, &typeFlag1);

	wasContained = (item->parent != NULL) ? 1 : 0;

	if (item->multiPtr != NULL) {
		mc = item->multiPtr;

		if (CMultiComponent_IsOwner(mc) && !wasContained) {
			CVector tempVec;
			CMultiSlave *slave = (CMultiSlave *)mc;

			CVector_Constructor(&tempVec, &typeFlag2);

			CMultiSlave_CollectItemSerials(slave, &tempVec);
			CItem_ProcessMultiDeleteItems(item, &tempVec, &serialsVec);

			CVector_Destructor(&tempVec);
		}

		if (CMultiComponent_IsOwner(mc)) {
			CMultiSlave_RemoveAllComponents((CMultiSlave *)mc);
		} else {
			CMultiComponent_NotifyOwnerRemoval(mc, item->serial);
		}

		if (CMultiComponent_IsOwner(mc)) {
			CMulti_Free(mc);
		} else {
			CMulti_Free(mc);
		}
		item->multiPtr = NULL;
	}

	if (!item->resourceEntity.entity.removedFromWorld) {
		CItem_HideVT(item);
	}

	CEntity_RemoveAllTimers(item);
	CItem_ClearScriptsAndTags(item);
	CItem_ReleaseTracking(item);

	g_DynamicItemCount--;

	if (CVector_GetCount(&serialsVec) > 0)
		CEntityMap_RemoveFromGrid(&g_SpatialGrid, &serialsVec);

	if (item->hashNext != NULL) {
		item->hashNext->hashPrev = item->hashPrev;
	}
	if (item->hashPrev != NULL) {
		item->hashPrev->hashNext = item->hashNext;
	} else {
		hashIdx = item->serial & 0xFFFF;
		if (g_World->hashTable[hashIdx] == item) {
			g_World->hashTable[hashIdx] = item->hashNext;
		}
	}

	// The binary calls a no-op Noop_4851F0(serial) here when
	// g_ShutdownFlag is clear; omitted as it is a pure no-op.

	CItem_DetachTemplate(item);
	CVector_Destructor(&serialsVec);
	CResourceEntity_Destructor(&item->resourceEntity);
}

/*
 * 0x00487188 - CItem::SetShopScript
 *
 * Stores the CString as the "afterShopScript" string ObjVar when it is
 * non-empty.
 */
void
CItem_SetShopScript(CItem *item, CString *scriptStr)
{
	if (CString_IsEmpty(scriptStr))
		return;
	CEntity_SetObjVar(item, "afterShopScript", 1, (uintptr_t)scriptStr);
}

/*
 * 0x004871B7 - CItem::GetAfterShopScript
 *
 * Copies the "afterShopScript" tag into out. Returns 1 when present
 * and non-empty; detaches and returns 0 for an empty tag.
 */
int
CItem_GetAfterShopScript(CItem *item, CString *out)
{
	CString *val;

	val = CResourceEntity_GetTagString(item, "afterShopScript");
	if (val == NULL)
		return 0;
	CString_Assign(out, val);
	if (CString_CompareStr(out, "")) {
		CResourceEntity_DetachScript(item, "afterShopScript");
		return 0;
	}
	return 1;
}

/*
 * 0x00487211 - CItem vtable[0xA4] SaveText
 *
 * Appends the item's save-file fields (serial, location, bodyType,
 * quality, container, home) to buf, optionally prefixed with
 * "Dynamic\n".
 */
void
CItem_SaveText_VT(CItem *item, char *buf, int dynamicFlag)
{
	CLocation homeLoc;

	if (dynamicFlag != 0)
		strcat(buf, "Dynamic\n");

	sprintf(buf + strlen(buf), "id=%u", item->serial);

	sprintf(buf + strlen(buf), " loc=(%d %d %d)", (int)item->resourceEntity.entity.location.x, (int)item->resourceEntity.entity.location.y,
	        (int)item->resourceEntity.entity.location.z);

	sprintf(buf + strlen(buf), " type=%d", CEntity_GetBodyType(item) & 0xFFFF);

	if ((CWorld_GetItemLayer(item->resourceEntity.entity.bodyType) & 0xFF) != 0) {
		sprintf(buf + strlen(buf), " qual=%d", CWorld_GetItemLayer(item->resourceEntity.entity.bodyType) & 0xFF);
	}

	if (item->parent != NULL)
		strcat(buf, " contained");

	CLocation_Init(&homeLoc);
	if (CItem_GetHomeLocation(item, &homeLoc)) {
		sprintf(buf + strlen(buf), " home=(%d %d %d)", (int)homeLoc.x, (int)homeLoc.y, (int)homeLoc.z);
	}
}

/*
 * 0x0048735F - CItem::GetName (vtable[0x12C])
 *
 * Builds the item's display string into the shared g_ItemNameBuf:
 * optional amount or article prefix, then the tiledata name expanding
 * the %plural/singular% markers. Returns the buffer.
 */
char g_ItemNameBuf[256]; // 0x006BA7B0

char *
CItem_GetNameString_VT(CItem *item, int withAmount)
{
	int amount;
	int plural;
	uint32_t flags;
	int bodyIdx;
	char *src;
	char *dst;

	g_ItemNameBuf[0] = '\0';
	plural = 0;

	if (withAmount == 0)
		goto do_name;

	amount = ((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);

	if (amount == 0) {
		plural = 1;
		sprintf(g_ItemNameBuf, "%d ", amount);
		goto do_name;
	}
	if (amount > 1) {
		plural = 1;
		sprintf(g_ItemNameBuf, "%d ", amount);
		goto do_name;
	}

	plural = 0;
	flags = ((uint32_t (*)(void *))VT_FN(item, VT_GET_FLAGS))(item);
	flags &= 0xC000;
	switch (flags) {
	case 0x4000:
		strcat(g_ItemNameBuf, "a ");
		break;
	case 0x8000:
		strcat(g_ItemNameBuf, "an ");
		break;
	case 0xC000:
		strcat(g_ItemNameBuf, "the ");
		break;
	default:
		g_ItemNameBuf[0] = '\0';
		break;
	}

do_name:
	bodyIdx = (CEntity_GetBodyType(item) & 0xFFFF) + (CItem_IsStackable(item) & 0xFFFF);
	src = &g_ItemTileData[bodyIdx].name[0];
	dst = g_ItemNameBuf + strlen(g_ItemNameBuf);

	while (*src != '\0') {
		if (*src == '%') {
			if (plural) {
				src++;
				while (*src != '\0' && *src != '/' && *src != '%')
					*dst++ = *src++;
				while (*src != '\0' && *src != '%')
					src++;
				if (*src == '%')
					src++;
			} else {
				src++;
				while (*src != '\0' && *src != '/' && *src != '%')
					src++;
				if (*src == '/') {
					src++;
					while (*src != '\0' && *src != '%')
						*dst++ = *src++;
				}
				if (*src == '%')
					src++;
			}
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
	return g_ItemNameBuf;
}

/*
 * 0x00487612 - CItem::SetSerial (vtable[0x140])
 *
 * Moves the item between serial-hash buckets: unlinks from its current
 * bucket, writes the new serial, and prepends it to the new bucket.
 */
void
CItem_SetSerial(CItem *item, uint32_t newSerial)
{
	uint16_t bucket;

	ResourceEntity_AttachTemplate(item);

	if (item->hashNext != NULL)
		item->hashNext->hashPrev = item->hashPrev;

	if (item->hashPrev != NULL) {
		item->hashPrev->hashNext = item->hashNext;
	} else {
		bucket = item->serial & 0xFFFF;
		g_World->hashTable[bucket] = item->hashNext;
	}

	// The binary calls a no-op Noop_4851F0(serial) here when the serial
	// actually changes; omitted since it has no observable effect.

	CItem_SetSerialField(item, newSerial);

	bucket = newSerial & 0xFFFF;
	item->hashNext = g_World->hashTable[bucket];
	if (item->hashNext != NULL)
		item->hashNext->hashPrev = item;
	item->hashPrev = NULL;
	g_World->hashTable[bucket] = item;
}

/*
 * 0x004876F5 - CItem::DetachHomeScript
 *
 * Detaches the "home" objvar script from the item.
 */
void
CItem_DetachHomeScript(CItem *item)
{
	CResourceEntity_DetachScript(item, "home");
}

/*
 * 0x0048770D - CItem::HasHome
 *
 * Returns 1 when the item carries a "home" location tag.
 */
int
CItem_HasHome(CItem *item)
{
	CLocation tmpLoc;

	CLocation_Init(&tmpLoc);
	return CItem_GetHomeLocation(item, &tmpLoc);
}

/*
 * 0x0048772E - CItem::GetHomeLocation
 *
 * Copies the "home" location tag into outLoc and returns 1, or
 * returns 0 if the tag is absent.
 */
int
CItem_GetHomeLocation(CItem *item, CLocation *outLoc)
{
	if (!CResourceEntity_HasTag(item, "home", 3))
		return 0;
	CResourceEntity_GetTagLoc(item, "home", outLoc);
	return 1;
}

/*
 * 0x00487768 - CItem::SetHome
 *
 * Invalid locations detach the "home" tag; valid ones store it as a
 * type-3 location ObjVar.
 */
void
CItem_SetHome(CItem *item, CLocation *loc)
{
	CString tag;

	if (CLocation_IsInvalid(loc)) {
		CItem_DetachHomeScript(item);
		return;
	}

	CString_Constructor(&tag, "home");
	ObjVar_SetStr(item, &tag, 3, (uintptr_t)loc);
}

/*
 * 0x004877D0 - CItem::ReturnToHome
 *
 * If the item has a valid "home" location, hides it from its current
 * spot (when still in world) and drops it at home.
 */
void
CItem_ReturnToHome(CItem *item)
{
	CLocation tmpLoc;

	if (!CItem_HasHome(item))
		return;

	CLocation_Init(&tmpLoc);
	CItem_GetHomeLocation(item, &tmpLoc);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, tmpLoc.x, tmpLoc.y))
		return;

	if (item->resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, &tmpLoc);
}

/*
 * 0x0048783D - CEntity::IsAtHome
 *
 * True when the entity is not contained, not a mobile, sits exactly at
 * its "home" tag location, and that location is inside a city or
 * justice region.
 */
int
CEntity_IsAtHome(CItem *item)
{
	CLocation homeLoc, curLoc;

	if (((int (*)(CItem *))VT_FN(item, VT_HAS_CONTAINER))(item))
		return 0;

	if (VT_IsMobile(item))
		return 0;

	CLocation_Init(&homeLoc);
	if (!CItem_GetHomeLocation(item, &homeLoc))
		return 0;

	CLocation_SetLoc(&curLoc, ((CLocation * (*)(CItem *)) VT_FN(item, VT_GET_LOCATION))(item));

	if ((int16_t)curLoc.x != (int16_t)homeLoc.x)
		return 0;
	if ((int16_t)curLoc.y != (int16_t)homeLoc.y)
		return 0;
	if ((int16_t)curLoc.z != (int16_t)homeLoc.z)
		return 0;

	if (RegionManager_isInCityRegion(curLoc.x, curLoc.y, curLoc.z))
		return 1;
	if (RegionManager_inJusticeRegion(curLoc.x, curLoc.y, curLoc.z))
		return 1;

	return 0;
}

/*
 * 0x004878F5 - CItem::IsInBankBox
 *
 * Walks the parent chain and returns 1 if any equipped ancestor sits in
 * its owner's bank box (equipment slot 29).
 */
int
CItem_IsInBankBox(CItem *item)
{
	CItem *walk;

	if (VT_IsMobile2(item))
		walk = item;
	else
		walk = item->parent;

	while (walk != NULL) {
		if (VT_IsEquipped(walk)) {
			CMobile *mob = (CMobile *)walk->parent;
			if (mob->equipment[29] == walk)
				return 1;
		}
		walk = walk->parent;
	}

	return 0;
}

/*
 * 0x00487962 - CItem::HasSecuredAncestor
 *
 * Returns 1 when any container in the parent chain (starting at the
 * item itself for mobiles) has a non-NULL lockOwner.
 */
int
CItem_HasSecuredAncestor(CItem *this)
{
	CItem *walk;

	if (VT_IsMobile2(this))
		walk = this;
	else
		walk = this->parent;

	while (walk != NULL) {
		if (((CContainer *)walk)->lockOwner != NULL)
			return 1;
		walk = walk->parent;
	}
	return 0;
}

/*
 * 0x004879B5 - CItem::FindSecuredContainer
 *
 * Returns the first non-NULL lockOwner found walking up the parent
 * chain (starting at the item for mobiles), or NULL.
 */
void *
CItem_FindSecuredContainer(CItem *item)
{
	CItem *walk;

	if (VT_IsMobile2(item))
		walk = item;
	else
		walk = item->parent;

	while (walk != NULL) {
		if (((CContainer *)walk)->lockOwner != NULL)
			return ((CContainer *)walk)->lockOwner;
		walk = walk->parent;
	}
	return NULL;
}

/*
 * 0x00487A09 - Item_IsSpecialBodyType
 *
 * True for body types 0x0E1C, 0x0FA6, 0x0FAD - the decay exemptions.
 */
static int
Item_IsSpecialBodyType(CItem *item)
{
	uint16_t bt = CEntity_GetBodyType(item) & 0xFFFF;

	if (bt == 0x0E1C || bt == 0x0FA6 || bt == 0x0FAD)
		return 1;
	return 0;
}

/*
 * 0x00487A47 - Item_IsBodyTypeInList_487A47
 *
 * True when the item's body type is one of the twelve at 0x0061DA88.
 * Nothing in the binary names the list, so it keeps its address.
 */
static __attribute__((unused)) int
Item_IsBodyTypeInList_487A47(CItem *item)
{
	// 0x0061DA88
	static const uint32_t bodyTypes[12] = { 0x12C, 0x121, 0x120, 0x122, 0x123, 0x39, 0x2FF, 0x11, 0x136, 0x137, 0x2F9, 0x2FA };
	int i;

	for (i = 0; i < 12; i++) {
		if (bodyTypes[i] == (uint32_t)(CEntity_GetBodyType(item) & 0xFFFF))
			return 1;
	}
	return 0;
}

/*
 * 0x00487A90 - Item_IsBodyTypeInList_487A90
 *
 * True when the item's body type is one of the eight at 0x0061DAB8.
 * Nothing in the binary names the list, so it keeps its address.
 */
static __attribute__((unused)) int
Item_IsBodyTypeInList_487A90(CItem *item)
{
	// 0x0061DAB8
	static const uint32_t bodyTypes[8] = { 0x3D5D, 0x3D5E, 0x3D5F, 0x3D60, 0x3D54, 0x3D55, 0x3D56, 0x3D57 };
	int i;

	for (i = 0; i < 8; i++) {
		if (bodyTypes[i] == (uint32_t)(CEntity_GetBodyType(item) & 0xFFFF))
			return 1;
	}
	return 0;
}

/*
 * 0x00487AD9 - StaticEntity vtable[0x0C] Hide/RemoveFromWorld
 *
 * Unlinks the static entity from its block's chain, marks it removed,
 * and rewrites the block's static-data file entry.
 */
void
StaticEntity_Hide_VT(CItem *self)
{
	int blockIdx;
	uint8_t buf[0x10000];
	uint16_t len;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &self->resourceEntity.entity.location, 0);

	if (self->resourceEntity.nextInContainer != NULL) {
		self->resourceEntity.nextInContainer->resourceEntity.staticPrev = self->resourceEntity.staticPrev;
	}

	if (self->resourceEntity.staticPrev != NULL) {
		self->resourceEntity.staticPrev->resourceEntity.nextInContainer = self->resourceEntity.nextInContainer;
	} else {
		g_SpatialGrid.cells[blockIdx].staticHead = self->resourceEntity.nextInContainer;
	}

	self->resourceEntity.staticPrev = NULL;
	self->resourceEntity.nextInContainer = NULL;
	self->resourceEntity.entity.removedFromWorld = 1;

	MapFileManager_SeekBlock(g_PoolBaseField_FC + blockIdx);
	PacketManager_MakePacket_STATIC_DATA(buf, blockIdx);
	len = GetPacketOffset(buf) & 0xFFFF;
	MapFileManager_WriteBlock(g_PoolBaseField_FC + blockIdx, buf, len);
}

/*
 * 0x00487BF5 - CItem::ClearCampfireNearby
 *
 * Clears the campfire target serial on every player within 8 blocks.
 * Called when a campfire (bodyType 0xB17) is hidden.
 */
static void
CItem_ClearCampfireNearby(CItem *item)
{
	int blockBuf[256];
	int i;
	CItem *cur;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &item->resourceEntity.entity.location, 8, blockBuf, 256);

	for (i = 0; blockBuf[i] != -1; i++) {
		cur = g_MapBlocks[blockBuf[i]].itemHead;
		while (cur != NULL) {
			if (((int (*)(void *))VT_ENT_FN(&cur->resourceEntity.entity, VT_IS_PLAYER))(cur)) {
				CPlayer_ClearTargetSerial((CPlayer *)cur);
			}
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x00487C94 - CItem::IsNonCountable
 *
 * Returns 1 when the item must be excluded from player weight/counts:
 * spellbooks, special containers, book body types, high-layer
 * equipment, mobiles, flag 0x80, or gold sitting in a bank box.
 */
int
CItem_IsNonCountable(CItem *item)
{
	int layer;
	CItem *walk;
	int count;

	layer = ((int (*)(void *))VT_FN(item, VT_GET_LAYER))(item);

	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x2AF8)
		return 1;

	if (((int (*)(void *))VT_FN(item, VT_EXCLUDED_AMOUNT))(item))
		return 1;

	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x1011)
		return 1;
	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x1769)
		return 1;
	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x176A)
		return 1;
	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x176B)
		return 1;

	if (layer >= 0x19)
		return 1;

	if (VT_IsMobile(item))
		return 1;

	if (CItem_HasItemFlag(item, 0x80))
		return 1;

	// Gold in bank: walk parents for an ancestor equipped at layer 0x1D.
	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x0EED) {
		walk = item->parent;
		count = 0;
		while (walk != NULL) {
			count++;
			layer = ((int (*)(void *))VT_FN(walk, VT_GET_LAYER))(walk);
			if (layer == 0x1D)
				return 1;
			walk = walk->parent;
		}
		USED(count);
	}

	return 0;
}

/*
 * 0x00487DBA - CItem::AdjustParentWeight
 *
 * Subtracts the item's weight from the parent container's stored
 * weight, guarded by g_weightSubtractCount to prevent re-entry.
 * Returns the item's equipment layer.
 */
int
CItem_AdjustParentWeight(CItem *item, CItem *parentArg)
{
	int layer = -1;

	if (parentArg == NULL)
		return layer;

	layer = ((int (*)(void *))VT_FN(item, VT_GET_LAYER))(item);

	if (CItem_IsNonCountable(item))
		return layer;

	g_weightSubtractCount++;
	int weight = ((int (*)(void *))VT_FN(item, VT_GET_WEIGHT))(item);
	CContainer_SubtractStoredWeight(parentArg, weight);
	g_weightSubtractCount--;

	return layer;
}

/*
 * 0x00487E27 - CItem::AddWeightToParent
 *
 * Adds the item's weight to its parent container/mobile when the item is
 * countable. Uses g_weightUpdateCount as a recursion guard. Returns the
 * item's prior layer, or -1 when there is no parent.
 */
int
CItem_AddWeightToParent(CItem *item, CItem *parent)
{
	int layer = -1;

	if (parent == NULL)
		return layer;

	layer = ((int (*)(void *))VT_FN(item, VT_GET_LAYER))(item);

	if (CItem_IsNonCountable(item))
		return layer;

	g_weightUpdateCount++;
	int weight = ((int (*)(void *))VT_FN(item, VT_GET_WEIGHT))(item);
	CContainer_AddStoredWeight(parent, weight);
	g_weightUpdateCount--;

	return layer;
}

/*
 * 0x00487E94 - CItem::Hide (vtable[0x0C])
 *
 * Full removal from the world: weight fixups, Unequip event, multi
 * detach, decay/tracking cleanup, campfire timestamp clearing, then
 * DESTROY_OBJECT broadcast (secured-ancestor or spatial) and the final
 * unlink from equipment/container/spatial grid.
 */
void
CItem_HideVT(CItem *item)
{
	int equipSlot;
	int bankFlag;
	CItem *savedParent;
	CItem *topContainerMobile;
	CLocation localLoc;
	uint8_t buf[16];
	CTradeSession *securedObj;
	CItem *parentMob;
	int blockIdx;

	equipSlot = CItem_AdjustParentWeight(item, item->parent);

	bankFlag = 0;

	if (VT_IsMobile2(item)) {
		if (CItem_IsInBankBox(item)) {
			bankFlag = 1;
		}
	}

	savedParent = item->parent;

	if (!CItem_IsDeleted(item)) {
		if (savedParent != NULL) {
			if (CItem_CanFireEquipEvent(equipSlot) == 1) {
				uint32_t itemSerial;
				itemSerial = CMobile_GetSerial((CMobile *)item);

				Entity_ExecuteEvent(&item->resourceEntity.entity, Unequip, CMobile_GetSerial((CMobile *)savedParent));

				// Re-find item (may have been deleted by event)
				CWorld_FindBySerial(g_World, itemSerial);
			}
		}
	}

	CItem_NotifyMultiDetach(item, 1);

	topContainerMobile = CItem_FindTopContainerMobile(item);

	if (CItem_IsValueless(item)) {
		CEntity_RemoveTimer(item, 8, 0);
	}

	CResourceEntity_NotifyPreModify(item);
	Block_RemoveTrackingNode(item);
	CItem_UpdateContainInfo(item, 0);

	if ((item->resourceEntity.entity.bodyType & 0xFFFF) == 0x0B17) {
		CItem_ClearCampfireNearby(item);
	}

	if (CItem_HasSecuredAncestor(item)) {
		securedObj = CItem_FindSecuredContainer(item);
		if (securedObj != NULL) {
			SetTradeAcceptState(securedObj, 0, 0);

			if (!CItem_IsServerOnly(item)) {
				PacketManager_MakePacket_DESTROY_OBJECT(buf, item->serial);

				SendToClient((CItem *)securedObj->player1, buf, -1);

				SendToClient((CItem *)securedObj->player2, buf, -1);
			}
		}
	} else {
		{
			CLocation *srcLoc;
			srcLoc = ((CLocation * (*)(void *)) VT_FN(item, VT_GET_LOCATION))(item);
			CLocation_SetLoc(&localLoc, srcLoc);
		}

		if (g_WorldActive != 0) {
			if (!CItem_IsServerOnly(item)) {
				PacketManager_MakePacket_DESTROY_OBJECT(buf, item->serial);

				SendPacketInRange(buf, &localLoc, 0x12);
			}
		}

		if (equipSlot != -1) {
			parentMob = item->parent;

			((CMobile *)parentMob)->equipment[equipSlot] = NULL;

			item->parent = NULL;

			item->resourceEntity.entity.removedFromWorld = 1;

			if (topContainerMobile != NULL) {
				if (VT_IsPlayer(topContainerMobile)) {
					SendStatusToPlayer((CMobile *)topContainerMobile, (CPlayer *)topContainerMobile, topContainerMobile->serial, 1);
				}
			}
			return;
		}
	}

	if (item->spatialNext != NULL)
		item->spatialNext->spatialPrev = item->spatialPrev;
	if (item->spatialPrev != NULL) {
		item->spatialPrev->spatialNext = item->spatialNext;
	}

	if (item->parent != NULL) {
		CContainer *parentCont = (CContainer *)item->parent;
		if (parentCont->contents == item)
			parentCont->contents = item->spatialNext;
	} else {
		blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &item->resourceEntity.entity.location, 0);
		if (blockIdx == -1) {
			item->spatialPrev = NULL;
			item->spatialNext = NULL;
			item->parent = NULL;
			item->resourceEntity.entity.removedFromWorld = 1;
			return;
		}

		if (g_MapBlocks[blockIdx].itemHead == item) {
			g_MapBlocks[blockIdx].itemHead = item->spatialNext;
		}
	}
	item->spatialPrev = NULL;
	item->spatialNext = NULL;

	item->parent = NULL;
	item->resourceEntity.entity.removedFromWorld = 1;

	if (bankFlag) {
		CContainer_RecalcStoredWeight((CItem *)item);
	}

	if (topContainerMobile != NULL) {
		if (VT_IsPlayer(topContainerMobile)) {
			SendStatusToPlayer((CMobile *)topContainerMobile, (CPlayer *)topContainerMobile, topContainerMobile->serial, 1);
		}
	}
}

/*
 * 0x00488382 - StaticEntity vtable[0x10] DetachSpatial
 *
 * Unlinks a static entity from its block's staticHead chain in O(1)
 * via the staticPrev back-pointer, and marks removedFromWorld.
 */
void
StaticEntity_DetachSpatial_VT(CItem *self)
{
	CItem *next = self->resourceEntity.nextInContainer;
	CItem *prev = self->resourceEntity.staticPrev;

	if (next != NULL)
		next->resourceEntity.staticPrev = prev;

	if (prev != NULL) {
		prev->resourceEntity.nextInContainer = next;
	} else {
		int blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &self->resourceEntity.entity.location, 0);
		g_SpatialGrid.cells[blockIdx].staticHead = next;
	}

	self->resourceEntity.staticPrev = NULL;
	self->resourceEntity.nextInContainer = NULL;
	self->resourceEntity.entity.removedFromWorld = 1;
}

/*
 * 0x004885CB - CItem::DetachFromSpatial (vtable[0x10])
 *
 * Plain-item detach: adjusts parent weight, notifies the multi system,
 * unlinks from the spatial/container doubly-linked list, fixes the
 * container or block head pointer, and marks removedFromWorld.
 */
void
CItem_DetachFromSpatial(CItem *item)
{
	CContainer *parentCont;
	int blockIdx;

	CItem_AdjustParentWeight(item, item->parent);
	CItem_NotifyMultiDetach(item, 0);
	CResourceEntity_NotifyPreModify(item);
	Block_RemoveTrackingNode(item);
	CItem_UpdateContainInfo(item, 0);

	if (item->spatialNext != NULL)
		item->spatialNext->spatialPrev = item->spatialPrev;
	if (item->spatialPrev != NULL) {
		item->spatialPrev->spatialNext = item->spatialNext;
	}

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

/*
 * 0x004887FD - CItem::FindTopContainerMobile
 *
 * Walks the parent chain to the topmost ancestor; returns it when it
 * is a mobile (VT_IsMobile), NULL otherwise.
 */
CItem *
CItem_FindTopContainerMobile(CItem *item)
{
	CItem *parent, *saved;

	parent = item->parent;
	if (parent == NULL)
		return NULL;

	while (parent != NULL) {
		saved = parent;
		parent = parent->parent;
	}

	if (VT_IsMobile(saved))
		return saved;
	return NULL;
}

/*
 * 0x004888DD - CItem::ReattachSpatial (vtable[0x3C])
 *
 * Moves an item from the head of its spatial block's item list to the
 * tail, so a freshly dropped item sits behind existing ones.
 */
void
CItem_ReattachSpatial_VT(CItem *item)
{
	CItem *cur;
	int blockIdx;

	if (item->parent != NULL)
		return;

	if (item->spatialNext == NULL)
		return;

	cur = item->spatialNext;
	while (cur->spatialNext != NULL)
		cur = cur->spatialNext;

	item->spatialNext->spatialPrev = NULL;

	item->spatialPrev = cur;
	cur->spatialNext = item;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &item->resourceEntity.entity.location, 0);
	g_MapBlocks[blockIdx].itemHead = item->spatialNext;

	item->spatialNext = NULL;
}

/*
 * 0x0048897A - CItem::AddToEquip (vtable[0xC4])
 *
 * Places the item inside the first of mob's equipped containers with
 * accessible contents; falls back to DropAtFeet at the mob's location.
 */
void
CItem_AddToEquip_VT(CItem *self, CMobile *mob)
{
	CLocation tempLoc;
	int i;

	CLocation_Init(&tempLoc);
	CLocation_Set(&tempLoc, -1, -1, 0);

	for (i = 0; i < 0x1A; i++) {
		if (mob->equipment[i] == NULL)
			continue;
		if (!((int (*)(CItem *))VT_FN(mob->equipment[i], VT_HAS_ACCESSIBLE_CONTENTS))(mob->equipment[i]))
			continue;
		((void (*)(CItem *, CItem *, CLocation *))VT_FN(self, VT_ADD_TO_CONTAINER))(self, mob->equipment[i], &tempLoc);
		return;
	}
	CLocation_CopyFrom(&tempLoc, ((CLocation * (*)(CItem *)) VT_FN((CItem *)mob, VT_GET_LOCATION))((CItem *)mob));
	((void (*)(CItem *, CLocation *))VT_FN(self, VT_DROP_AT_FEET))(self, &tempLoc);
}

/*
 * 0x00488A37 - CItem::GetTagInt
 *
 * Returns 1 and fills *outVal when the named int tag exists, else 0.
 */
int
CItem_GetTagInt(CItem *item, const char *name, int *outVal)
{
	if (!CResourceEntity_HasTag(item, name, 0))
		return 0;
	CResourceEntity_GetTagInt(item, name, outVal);
	return 1;
}

/*
 * 0x00488A6F - CItem::GetMonetaryVal (vtable[0xCC])
 *
 * Dispatches to GetValue, pulling the resource-backed value when the
 * item has positive resources and the binary flag.
 */
int
CItem_GetMonetaryVal_VT(CItem *self)
{
	int useResource;

	useResource = 1;
	if (((int (*)(CItem *))VT_FN(self, VT_HAS_RESOURCE_FLAG))(self)) {
		if (!CItem_HasPositiveResources(self))
			useResource = 0;
	}
	return ((int (*)(CItem *, int, int))VT_FN(self, VT_GET_VALUE))(self, useResource, 1);
}

/*
 * 0x00488ABC - CItem::GetValue (vtable[0x24])
 *
 * Sums mybasevalue, weighted resource amounts (magic, gold, jewels*20,
 * metal*2, cloth, wood, water, leather, meat), satiety, and tiledata
 * height, optionally passing the result through NormalizeValue.
 */
int
CItem_GetValue_VT(CItem *self, int useResource, int normalize)
{
	int value;
	int resAmt;
	CItem *src;

	value = 0;
	if (CItem_IsValueless(self))
		return value;

	// Check "mybasevalue" tag
	{
		int tagVal;
		if (CItem_GetTagInt(self, "mybasevalue", &tagVal) == 1)
			value += tagVal;
	}

	// Determine resource source
	src = self;
	if (useResource) {
		uint16_t bt = self->resourceEntity.entity.bodyType;
		src = (CItem *)&g_ResEntitySlots[bt & 0xFFFF];
	}

	// Accumulate resource amounts
	resAmt = 0;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Magic, 3, 2))
		value += resAmt;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Gold, 3, 2))
		value += resAmt;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Jewels, 3, 2))
		value += resAmt * 0x14;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Metal, 3, 2))
		value += resAmt * 2;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Cloth, 3, 2))
		value += resAmt;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Wood, 3, 2))
		value += resAmt;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Water, 3, 2))
		value += resAmt;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Leather, 3, 2))
		value += resAmt;
	if (CItem_GetObjVarResType(src, &resAmt, g_ResTypeId_Meat, 3, 2))
		value += resAmt;

	// Check "satiety" tag
	{
		int tagVal;
		if (CItem_GetTagInt(self, "satiety", &tagVal) == 1)
			value += tagVal;
	}

	// If value still <= 1, use amount-based fallback
	if (value <= 1) {
		int fallback;
		if (useResource) {
			fallback = 2;
		} else {
			fallback = ((int (*)(CItem *))VT_FN(self, VT_GET_ITEM_AMOUNT))(self) << 1;
		}
		value = fallback;
	}

	{
		uint16_t bt = self->resourceEntity.entity.bodyType;
		int stackOff = CItem_IsStackable(self) & 0xFFFF;
		int tdIdx = (bt & 0xFFFF) + stackOff;
		value += g_ItemTileData[tdIdx].height;
	}

	if (normalize)
		value = CItem_NormalizeValue(self, value);

	return value;
}

/*
 * 0x00488D19 - CItem::NormalizeValue
 *
 * Falls back to the tiledata height when the raw value is non-positive,
 * and clamps the result to at least 1.
 */
int
CItem_NormalizeValue(CItem *self, int value)
{
	int result;

	result = 0;
	if (value > 0) {
		result = value;
	} else {
		uint16_t bt = self->resourceEntity.entity.bodyType;
		int stackOff = CItem_IsStackable(self) & 0xFFFF;
		int tdIdx = (bt & 0xFFFF) + stackOff;
		result = g_ItemTileData[tdIdx].height;
	}
	if (result <= 0)
		result = 1;
	return result;
}

/*
 * 0x00488D81 - CItem::EquipOnMobile (vtable[0xC0])
 *
 * Attempts to equip the item on mob at layer: bails on multi items or
 * when the slot is occupied, otherwise parents to the mob, updates
 * weight, broadcasts EQUIP_ITEM, refreshes the top player's status,
 * and fires the Equip event.
 */
int
CItem_EquipOnMobile(CItem *item, CMobile *mob, int layer)
{
	uint8_t buf[20];
	CItem *topMob;
	uint32_t savedSerial;

	if (item->multiPtr != NULL) {
		((void (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item);
		return 0;
	}

	if (mob->equipment[layer & 0xFF] != NULL)
		return 0;

	item->decayCount = 0;

	mob->equipment[layer & 0xFF] = item;

	item->parent = (CItem *)mob;

	item->resourceEntity.entity.removedFromWorld = 0;

	CItem_AddWeightToParent(item, (CItem *)mob);

	if (!g_World->isLoading) {
		if (((CItem *)mob)->resourceEntity.entity.removedFromWorld == 0) {
			if ((layer & 0xFF) != 0) {
				if (!CItem_IsServerOnly(item)) {
					PacketManager_MakePacket_EQUIP_ITEM(buf, item, mob, (uint8_t)(layer & 0xFF));
					SendPacketInRange(buf, &mob->container.item.resourceEntity.entity.location, 0x12);
				}
			}
		}
	}

	topMob = CItem_FindTopContainerMobile(item);
	if (topMob != NULL) {
		if (VT_IsPlayer(topMob)) {
			SendStatusToPlayer((CMobile *)topMob, (CPlayer *)topMob, topMob->serial, 1);
		}
	}

	if (!CItem_IsDeleted(item)) {
		if (CItem_CanFireEquipEvent(layer & 0xFF) == 1) {
			savedSerial = item->serial;
			Entity_ExecuteEvent(&item->resourceEntity.entity, Equip, CMobile_GetSerial(mob));
			CWorld_FindBySerial(g_World, savedSerial);
		}
	}

	return 1;
}

/*
 * 0x00488EF5 - StaticEntity::SetLocation (vtable[0x08])
 *
 * Prepends the static entity onto its block's staticHead chain at the
 * given location, then re-serializes the block through the map file
 * manager so the on-disk copy matches.
 */
void
StaticEntity_SetLocation_VT(CItem *self, CLocation *loc)
{
	int blockIdx;
	uint8_t buf[0x10000];
	uint16_t len;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);

	self->resourceEntity.nextInContainer = g_SpatialGrid.cells[blockIdx].staticHead;

	if (self->resourceEntity.nextInContainer != NULL)
		self->resourceEntity.nextInContainer->resourceEntity.staticPrev = self;

	g_SpatialGrid.cells[blockIdx].staticHead = self;

	CLocation_CopyFrom(&self->resourceEntity.entity.location, loc);
	self->resourceEntity.entity.removedFromWorld = 0;

	MapFileManager_SeekBlock(g_PoolBaseField_FC + blockIdx);
	PacketManager_MakePacket_STATIC_DATA(buf, blockIdx);
	len = GetPacketOffset(buf) & 0xFFFF;
	MapFileManager_WriteBlock(g_PoolBaseField_FC + blockIdx, buf, len);
}

/*
 * 0x00488FFE - CItem::ScanNearbyCampfire
 *
 * Stamps every nearby player (range 8 blocks) with a campfire timestamp
 * so they receive the warm-by-fire treatment.
 */
static void
CItem_ScanNearbyCampfire(CItem *item)
{
	int blockBuf[256];
	int i;
	CItem *cur;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &item->resourceEntity.entity.location, 8, blockBuf, 256);

	for (i = 0; blockBuf[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockBuf[i]].itemHead;
		while (cur != NULL) {
			if (VT_IsPlayer(cur))
				CItem_SetCampfireTimestamp(cur);
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x0048909D - CItem::InternalMove
 *
 * Head-inserts the item into the spatial grid at loc and updates
 * entity.location; propagates the move to multi components when this
 * item owns a multi. The old block chain is left intact - range
 * queries run off CEntityMap, not this chain.
 *
 * MODIFIED: Diagnostic precondition check added ahead of the prepend.
 * The binary does not enforce that item is detached, relying on every
 * caller to run VT_HIDE (or an equivalent DetachSpatial path) first.
 * When a caller violates that contract, item->spatialNext can end up
 * pointing at item, or the old block's chain keeps referencing an item
 * that now lives in a different block - faults that only surface much
 * later in Terrain_BuildSurfaceList / CBlockManager_GetItemsAtLocation*
 * as an infinite loop. The check is pure instrumentation to name the
 * guilty caller in a coredump and should be removed once the real
 * culprit is identified and fixed.
 */
void
CItem_InternalMove(CItem *item, CLocation *loc, int flag)
{
	int blockIdx;
	CBlock *blk;

	if (item->multiPtr != NULL) {
		CMultiComponent *mc = item->multiPtr;
		if (CMultiComponent_IsOwner(mc)) {
			if (CMultiComponent_GetStaticFlag(mc)) {
				CMultiSlave_UpdateComponents((CMultiSlave *)mc, loc, 1);
			}
		}
	}

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, flag);
	blk = &g_SpatialGrid.cells[blockIdx];

	if (item->spatialNext != NULL || item->spatialPrev != NULL || blk->itemHead == item) {
		fprintf(stderr,
		        "CItem_InternalMove: item %08x (body=%04x) not "
		        "detached - spatialNext=%p spatialPrev=%p "
		        "blk[%d].itemHead=%p (at %d,%d,%d -> %d,%d,%d); "
		        "aborting for coredump\n",
		        item->serial, (unsigned)(CEntity_GetBodyType(item) & 0xFFFF), (void *)item->spatialNext, (void *)item->spatialPrev, blockIdx, (void *)blk->itemHead,
		        (int)(int16_t)item->resourceEntity.entity.location.x, (int)(int16_t)item->resourceEntity.entity.location.y,
		        (int)(int16_t)item->resourceEntity.entity.location.z, (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)(int16_t)loc->z);
		fflush(stderr);
		abort();
	}

	item->spatialNext = blk->itemHead;
	if (item->spatialNext != NULL)
		item->spatialNext->spatialPrev = item;
	blk->itemHead = item;

	CLocation_CopyFrom(&item->resourceEntity.entity.location, loc);
}

/*
 * 0x00489158 - CItem::NotifyMultiDetach
 *
 * For multi owners, hides (flag != 0) or detaches (flag == 0) every
 * slave component when the owner leaves the world.
 */
void
CItem_NotifyMultiDetach(CItem *item, int flag)
{
	CMultiSlave *ms;

	if (item->multiPtr == NULL)
		return;
	ms = (CMultiSlave *)item->multiPtr;
	if (!CMultiComponent_IsOwner(&ms->base))
		return;
	if (flag)
		CMultiSlave_AddComponents(ms, 1);
	else
		CMultiSlave_RemoveComponents(ms, 1);
}

/*
 * 0x004891A5 - CItem::MultiContainerCheck
 *
 * When the item owns a multi, containerizes every slave component
 * into the supplied container. Always returns 1.
 */
static int
CItem_MultiContainerCheck(CItem *this, CItem *container)
{
	if (this->multiPtr != NULL) {
		CMultiComponent *mc = this->multiPtr;
		if (CMultiComponent_IsOwner(mc)) {
			CMultiSlave_AddComponentItems((CMultiSlave *)this->multiPtr, container, 1);
		}
		return 1;
	}
	return 1;
}

/*
 * 0x004891F4 - CItem::DropAtFeet (vtable[0x04])
 *
 * Drops the item at loc: caches home location, schedules decay for
 * valueless items, places into the spatial grid, broadcasts MOVE (or
 * CORPSE_EQ + MULTI_OBJ_TO_OBJ for corpses), handles campfire
 * warming, and releases any tracking data.
 */
void
CItem_DropAtFeet(CItem *item, CLocation *loc)
{
	uint8_t moveBuf[0x18];
	uint8_t corpseBuf[0xD0];
	uint8_t multiBuf[0x20018];

	if (*(int16_t *)&item->resourceEntity.nextInContainer == -1)
		CLocation_SetLoc((CLocation *)&item->resourceEntity.nextInContainer, loc);

	if (CItem_IsValueless(item)) {
		ScheduleEvent(0xF0, item->serial, 8, 0, 0);
	}

	CItem_InternalMove(item, loc, 0);
	UpdateRegion(item);
	item->resourceEntity.entity.removedFromWorld = 0;

	if ((CEntity_GetBodyType(item) & 0xFFFF) == 0x0B17)
		CItem_ScanNearbyCampfire(item);

	if (!CItem_IsServerOnly(item)) {
		PacketManager_MakePacket_MOVE(moveBuf, item);
		SendPacketInRange(moveBuf, &item->resourceEntity.entity.location, 0x12);
	} else {
		CItem_NotifyMultiServerOnly(item, &item->resourceEntity.entity.location);
	}

	if (((int (*)(void *))VT_FN(item, VT_HAS_CORPSE_EQ))(item)) {
		if (!CItem_IsServerOnly(item)) {
			uint32_t *equipSlots = ((CCorpse *)item)->equipSlots;

			PacketManager_MakePacket_CORPSE_EQ(corpseBuf, item->serial, equipSlots);
			SendPacketInRange(corpseBuf, &item->resourceEntity.entity.location, 0x12);

			PacketManager_MakePacket_MULTI_OBJ_TO_OBJ(multiBuf, (CContainer *)item, 0, 0);
			SendPacketInRange(multiBuf, &item->resourceEntity.entity.location, 0x12);
		}
	}

	CResourceEntity_NotifyPostModifyIfActive(item);
	CItem_ReleaseTracking(item);
}

/*
 * 0x004893B1 - StaticEntity::SetLocation_Link
 *
 * Load-time variant of StaticEntity::SetLocation: prepends the static
 * to its block's staticHead chain without re-serializing the block.
 */
void
StaticEntity_SetLocation_Link(CItem *self, CLocation *loc)
{
	int blockIdx;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 1);

	self->resourceEntity.nextInContainer = g_SpatialGrid.cells[blockIdx].staticHead;

	if (self->resourceEntity.nextInContainer != NULL)
		self->resourceEntity.nextInContainer->resourceEntity.staticPrev = self;

	g_SpatialGrid.cells[blockIdx].staticHead = self;

	CLocation_CopyFrom(&self->resourceEntity.entity.location, loc);
	self->resourceEntity.entity.removedFromWorld = 0;
}

/*
 * 0x00489432 - CItem::SetLocation (vtable[0x08])
 *
 * Places the item at loc on the spatial grid, caching the home
 * location on first placement and scheduling decay when valueless.
 */
void
CItem_SetLocation_VT(CItem *self, CLocation *loc)
{
	if (*(int16_t *)&self->resourceEntity.nextInContainer == -1)
		CLocation_SetLoc((CLocation *)&self->resourceEntity.nextInContainer, loc);

	if (CItem_IsValueless(self))
		ScheduleEvent(0xF0, self->serial, 8, 0, 0);

	CItem_InternalMove(self, loc, 1);
	self->resourceEntity.entity.removedFromWorld = 0;
	UpdateRegion(self);
	CResourceEntity_NotifyPostModifyIfActive(self);
	CItem_ReleaseTracking(self);
}

/*
 * 0x004895C7 - CItem::SetCampfireTimestamp
 *
 * Records the current tick as the player's campfire stamp if none is
 * set yet. The field aliases CPlayer::targetSerial.
 */
void
CItem_SetCampfireTimestamp(CItem *entity)
{
	CPlayer *player = (CPlayer *)entity;

	if (player->targetSerial == 0)
		player->targetSerial = GetTickCount_UO();
}

/*
 * 0x0048991A - CItem::ReturnToTracked
 *
 * Restores an item to its remembered placement: first tries to re-equip
 * on the tracked mobile, then to place inside the tracked container
 * (merging for non-container owners), and finally falls back to
 * dropping at the tracked world coordinates.
 */
int
CItem_ReturnToTracked(CItem *item)
{
	CItemTracking *tr;
	int succeeded;
	int ret;
	CItem *mob;
	CItem *owner;

	succeeded = 0;
	ret = 1;

	tr = item->tracking;
	if (tr == NULL)
		return 0;

	if (tr->lastMob != 0) {
		mob = CWorld_FindBySerial(g_World, tr->lastMob);
		if (mob == NULL)
			goto check_succeeded;
		if (!VT_IsMobile(mob))
			goto check_succeeded;
		succeeded = ((int (*)(void *, void *, int))VT_FN(item, VT_EQUIP_ON_MOBILE))(item, mob, (uint8_t)tr->lastMobEqPos);
		goto check_succeeded;
	}

	if (tr->lastCont == 0)
		goto check_succeeded;

	owner = CWorld_FindBySerial(g_World, tr->lastCont);
	if (owner == NULL)
		goto check_succeeded;

	if (VT_IsMobile2(owner)) {
		((void (*)(void *, void *, void *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, owner, &tr->lastContLoc);
		succeeded = 1;
		goto check_succeeded;
	}

	// Dead load of item->serial at 0x00489A07 in the binary.
	(void)item->serial;
	if (!((int (*)(void *, void *))VT_FN(item, VT_MERGE_INTO))(item, owner))
		goto check_succeeded;
	if (!CItem_MergeInto(item, owner))
		goto check_succeeded;
	succeeded = 1;

check_succeeded:
	if (succeeded != 0)
		return ret;

	if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)tr->lastLoc.x, (int16_t)tr->lastLoc.y)) {
		((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, tr);
	} else {
		ret = 0;
	}

	return ret;
}

/*
 * 0x00489B97 - CCorpse::GetCorpseBodyType
 *
 * Returns the corpseBodyType field.
 */
uint16_t
CCorpse_GetCorpseBodyType(CCorpse *this)
{
	return this->corpseBodyType;
}

/*
 * 0x00489BAC - CCorpse::SetCorpseBodyType
 *
 * Sets the corpseBodyType field.
 */
void
CCorpse_SetCorpseBodyType(CCorpse *this, uint16_t bodyType)
{
	this->corpseBodyType = bodyType;
}

/*
 * 0x00489C96 - CItem::SendEntityUpdate (vtable[0x134])
 *
 * Sends the right update packet for the item's state to viewer:
 * EQUIP_ITEM for equipped, OBJ_TO_OBJ for contained, MOVE for ground,
 * delegating server-only items to the multi helper.
 */
void
CItem_SendEntityUpdate_VT(CItem *self, CItem *viewer, int withEquip)
{
	uint8_t pktBuf[0x48];
	CMobile *parentMob;
	int slot;

	USED(withEquip);

	if (CItem_IsServerOnly(self) == 1) {
		CItem_SendServerOnlyUpdate(self, viewer);
		return;
	}

	if (self->parent != NULL) {
		parentMob = (CMobile *)self->parent;
		if (VT_IsMobile(self->parent)) {
			for (slot = 0; slot < 0x1E; slot++) {
				if (parentMob->equipment[slot] == self)
					break;
			}
			if (slot < 0x1E) {
				PacketManager_MakePacket_EQUIP_ITEM(pktBuf, self, parentMob, (uint8_t)slot);
				SendToClient(viewer, pktBuf, -1);
			}
			return;
		}
		PacketManager_MakePacket_OBJ_TO_OBJ(pktBuf, self, self->parent);
		SendToClient(viewer, pktBuf, -1);
		return;
	}

	PacketManager_MakePacket_MOVE(pktBuf, self);
	SendToClient(viewer, pktBuf, -1);
}

/*
 * 0x00489D9E - CItem::SendUpdateToList (vtable[0x130])
 *
 * Broadcasts the item to a prebuilt player list: EQUIP_ITEM for
 * equipped, OBJ_TO_OBJ for contained, MOVE for ground. Server-only
 * items take the per-viewer multi path instead.
 */
void
CItem_SendUpdateToList(CItem *item, CVector *players, int mode)
{
	uint8_t buf[512];
	CMobile *mob;
	int i;
	CItem **ptr;

	USED(mode);

	if (CItem_IsServerOnly(item) == 1) {
		ptr = (CItem **)players->begin;
		while (ptr != (CItem **)players->end) {
			CItem_SendServerOnlyUpdate(item, *ptr);
			ptr++;
		}
		return;
	}

	if (item->parent != NULL) {
		if (VT_IsMobile(item->parent)) {
			mob = (CMobile *)item->parent;
			for (i = 0; i < 0x1E; i++) {
				if (mob->equipment[i] == item)
					break;
			}
			if (i < 0x1E) {
				PacketManager_MakePacket_EQUIP_ITEM(buf, item, mob, (uint8_t)i);
				SendToClientList(players, buf);
			}
		} else {
			PacketManager_MakePacket_OBJ_TO_OBJ(buf, item, item->parent);
			SendToClientList(players, buf);
		}
	} else {
		PacketManager_MakePacket_MOVE(buf, item);
		SendToClientList(players, buf);
	}
}

/*
 * 0x00489EC7 - CItem::HasContainer (vtable[0x104])
 *
 * Returns whether the item has a parent.
 */
int
CItem_HasContainerVT(CItem *item)
{
	return (item->parent != NULL) ? 1 : 0;
}

/*
 * 0x00489EE0 - CItem::IsEquipped (vtable[0x108])
 *
 * Returns 1 when the item sits in its parent mobile's equipment array.
 */
int
CItem_IsEquipped_VT(CItem *item)
{
	int i;
	CMobile *mob;

	if (item->parent == NULL)
		return 0;
	if (!VT_IsMobile(item->parent))
		return 0;
	mob = (CMobile *)item->parent;
	for (i = 0; i < 0x1E; i++) {
		if (mob->equipment[i] == item)
			return 1;
	}
	return 0;
}

/*
 * 0x00489F46 - CItem::GetLayer (vtable[0x10C])
 *
 * Returns the item's equipment slot (0-29), or -1 when not equipped.
 */
int
CItem_GetLayer_VT(CItem *item)
{
	int i;

	if (item->parent == NULL)
		return -1;
	if (!VT_IsMobile(item->parent))
		return -1;
	for (i = 0; i < 0x1E; i++) {
		if (((CMobile *)item->parent)->equipment[i] == item)
			return i;
	}
	return -1;
}

/*
 * 0x00489FAB - CItem::GetDecayMax (vtable[0x120])
 *
 * Weight-based decay score: bodyType 0 returns 0xFF (never decays);
 * otherwise combines the item's resource weight, amount, and any
 * stored weight into a single score used by CItem_DecayTick.
 */
int
CItem_GetDecayMax(CItem *item)
{
	uint16_t bodyType;
	int overWeight;
	int amount;

	bodyType = item->resourceEntity.entity.bodyType;

	if (bodyType == 0)
		return 0xFF;

	overWeight = 0;

	if (CItem_HasMovableFlag(item)) {
		if (CItem_GetTagInt(item, "overloadedWeight", &overWeight))
			goto got_weight;
	}

	if (VT_IsMobile(item)) {
		overWeight = CMobile_GetMaxHp((CMobile *)item) / 2;
	} else {
		if (CWorld_LookupItemResource(bodyType + (uint16_t)CItem_IsStackable(item)))
			overWeight = CWorld_GetResourceWeight(bodyType + (uint16_t)CItem_IsStackable(item));
	}

got_weight:
	amount = (int)((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);
	if (amount <= 0)
		amount = 1;

	if (overWeight == 0) {
		if (CItem_IsGold(item))
			overWeight = (amount + 49) / 50;
		else
			overWeight = (amount + 9) / 10;
	} else {
		overWeight = overWeight * amount;
	}

	if (VT_IsMobile2(item) || VT_IsMobile(item)) {
		overWeight += ((int (*)(void *))VT_FN(item, VT_GET_STORED_WEIGHT))(item);
	}

	return overWeight;
}

/*
 * 0x0048A11F - CItem::GetDirection (vtable[0x138])
 *
 * Returns the tiledata layer byte for TF_LIGHT tiles or corpse-equip
 * items, otherwise 0.
 */
int
CItem_GetDirectionVT(CItem *item)
{
	int flags;

	flags = ((int (*)(void *))VT_FN(item, VT_GET_FLAGS))(item);
	if (flags & 0x800000) {
		return CWorld_GetItemLayer(item->resourceEntity.entity.bodyType) & 0xFF;
	}
	if (((int (*)(void *))VT_FN(item, VT_HAS_CORPSE_EQ))(item)) {
		return CWorld_GetItemLayer(item->resourceEntity.entity.bodyType) & 0xFF;
	}
	return 0;
}

/*
 * 0x0048A170 - CItem::GetContainerDim (vtable[0x13C])
 *
 * Copies the gump width and height for this item's bodyType out of
 * g_GumpDimTable.
 */
void
CItem_GetContainerDim_VT(CItem *self, uint16_t *outWidth, uint16_t *outHeight)
{
	uint16_t bt;

	bt = self->resourceEntity.entity.bodyType;
	*outWidth = g_GumpDimTable[bt].width;
	*outHeight = g_GumpDimTable[bt].height;
}

/*
 * 0x0048A1B3 - CItem::IsMoveable (vtable[0x110])
 *
 * Move permission: GM players bypass the check; otherwise rejects
 * multi-locked, no-draw, or corpse-equipped items, and anything with
 * tiledata weight >= 91 (or overloadedWeight >= 91 when tagged).
 */
int
CItem_IsMoveable_VT(CItem *self, CItem *viewer)
{
	uint16_t bt;
	int stackOff, idx;

	if (CItem_HasMulti_Filter(self))
		return 0;

	if (viewer != NULL) {
		if (VT_IsPlayer(viewer)) {
			if (((CPlayer *)viewer)->pflags & 2)
				return 1;
		}
	}

	if (IsNoDrawType(CEntity_GetBodyType(self)))
		return 0;

	if (((int (*)(CItem *))VT_FN(self, VT_HAS_CORPSE_EQ))(self))
		return 0;

	if (CItem_HasMovableFlag(self)) {
		int tagVal;
		if (CItem_GetTagInt(self, "overloadedWeight", &tagVal)) {
			return (tagVal < 0x5B) ? 1 : 0;
		}
	}

	bt = CEntity_GetBodyType(self);
	stackOff = CItem_IsStackable(self) & 0xFFFF;
	idx = (bt & 0xFFFF) + stackOff;
	if (g_ItemTileData[idx].weight < 0x5B)
		return 1;
	return 0;
}

/*
 * 0x0048A29E - CItem::IsFreelyUsable (vtable[0x114])
 *
 * Use permission: GM/editing players bypass; dead players fail.
 * Rejects no-draw tiles, items inside inaccessible containers, items
 * owned by a different mobile (with a 0x3D permission event hook),
 * anything further than 3 tiles, and anything CanSee blocks.
 */
int
CItem_IsFreelyUsable_VT(CItem *self, CItem *viewer)
{
	CPlayer *player;
	CItem *cur, *lastItem;
	CMobile *mob;

	// Player viewer checks
	if (VT_IsPlayer(viewer)) {
		player = (CPlayer *)viewer;
		if (CPlayer_IsEditing(player))
			return 1;
		if (CPlayer_HasDeadFlag(player))
			return 0;
	}

	// No-draw type
	if (IsNoDrawType(CEntity_GetBodyType(self)))
		return 0;

	mob = NULL;
	lastItem = self;
	cur = self->parent;
	while (cur != NULL) {
		if (VT_IsMobile(cur)) {
			mob = (CMobile *)cur;
			if (lastItem == mob->equipment[21])
				goto skip_mob;
			if (!VT_IsEquipped(self))
				return 0;
			goto skip_mob;
		}
		if (!((int (*)(CItem *))VT_FN(cur, VT_HAS_ACCESSIBLE_CONTENTS))(cur))
			return 0;
		lastItem = cur;
		cur = cur->parent;
	}

skip_mob:
	if (mob != NULL && mob != (CMobile *)viewer) {
		uint32_t selfSerial = CMobile_GetSerial((CMobile *)self);
		uint32_t viewerSerial = CMobile_GetSerial((CMobile *)viewer);
		if (Entity_ExecuteEvent((CEntity *)mob, 0x3D, 9, viewerSerial, selfSerial) != NULL)
			return 0;
	}

	{
		CLocation *viewerLoc = ((CLocation * (*)(CItem *)) VT_FN(viewer, VT_GET_LOCATION))(viewer);
		CLocation *selfLoc = ((CLocation * (*)(CItem *)) VT_FN(self, VT_GET_LOCATION))(self);
		if (Location_WrappedChebyshevDistance(selfLoc, viewerLoc) > 3)
			return 0;
	}

	if (!CEntity_CanSee(viewer, self, 1))
		return 0;

	return 1;
}

/*
 * 0x0048A412 - CItem::IsFreelyViewable (vtable[0x118])
 *
 * View permission: like IsFreelyUsable but only inspects containers
 * along the parent chain and does not apply the mobile-ownership
 * event.
 */
int
CItem_IsFreelyViewable_VT(CItem *self, CItem *viewer)
{
	CPlayer *player;
	CItem *cur;

	if (VT_IsPlayer(viewer)) {
		player = (CPlayer *)viewer;
		if (CPlayer_IsEditing(player))
			return 1;
		if (CPlayer_HasDeadFlag(player))
			return 0;
	}

	if (IsNoDrawType(CEntity_GetBodyType(self)))
		return 0;

	cur = self->parent;
	while (cur != NULL) {
		if (!((int (*)(void *))VT_FN(cur, VT_HAS_ACCESSIBLE_CONTENTS))(cur))
			return 0;
		cur = cur->parent;
	}

	if (self->parent != NULL) {
		if (VT_IsMobile(self->parent)) {
			if (!VT_IsEquipped(self))
				return 0;
		}
	}

	{
		CLocation *viewerLoc = ((CLocation * (*)(CItem *)) VT_FN(viewer, VT_GET_LOCATION))(viewer);
		CLocation *selfLoc = ((CLocation * (*)(CItem *)) VT_FN(self, VT_GET_LOCATION))(self);
		if (Location_WrappedChebyshevDistance(selfLoc, viewerLoc) > 3)
			return 0;
	}

	if (!CEntity_CanSee(viewer, self, 1))
		return 0;

	return 1;
}

/*
 * 0x0048A531 - CItem::GetLocation (vtable[0x80])
 *
 * Returns the containing parent's location when the item is nested,
 * otherwise the item's own entity location.
 */
CLocation *
CItem_GetLocationVT(CItem *item)
{
	if (item->parent != NULL)
		return ((CLocation * (*)(void *)) VT_FN(item->parent, VT_GET_LOCATION))(item->parent);
	return &item->resourceEntity.entity.location;
}

/*
 * 0x0048A561 - CItem::CanStackWith (vtable[0x128])
 *
 * Reports whether source may stack onto target: both must be resource
 * items with matching bodyType and color, positive amounts whose sum
 * stays below 60000, and the IsStackableOn script event must succeed.
 */
int
CItem_CanStackWith(CItem *this, CItem *target)
{
	int thisAmount;
	int targetAmount;

	if (!((int (*)(void *))VT_FN(this, VT_HAS_RESOURCE_FLAG))(this))
		return 0;
	if (!((int (*)(void *))VT_FN(target, VT_HAS_RESOURCE_FLAG))(target))
		return 0;

	if (!CItem_HasResourceRecipe(this))
		return 0;
	if (!CItem_HasResourceRecipe(target))
		return 0;

	thisAmount = CItem_GetMinResourceRatio(this);
	targetAmount = CItem_GetMinResourceRatio(target);

	if ((CEntity_GetBodyType(this) & 0xFFFF) != (CEntity_GetBodyType(target) & 0xFFFF))
		return 0;

	if ((uint16_t)this->resourceEntity.entity.color != (uint16_t)target->resourceEntity.entity.color)
		return 0;

	if (thisAmount <= 0)
		return 0;
	if (targetAmount <= 0)
		return 0;

	if (thisAmount + targetAmount > 0xEA60)
		return 0;

	return Entity_ExecuteEvent(&this->resourceEntity.entity, IsStackableOn, target->serial) != 0;
}

/*
 * 0x0048A63D - CItem::MergeInto
 *
 * Merges source into target: fires StackOnto, hides both items,
 * transfers the valueless tag, consumes resources, re-places target
 * via AddToContainer (secured parents) or ReturnToTracked, and
 * deletes source when fully consumed.
 */
int
CItem_MergeInto(CItem *source, CItem *target)
{
	CItem *savedParentContainer;
	CItem *savedParent;
	CLocation tempLoc;
	uint32_t savedSourceSerial;
	int consumed;

	savedParentContainer = NULL;

	CLocation_Init(&tempLoc);

	savedParent = target->parent;
	if (target->parent != NULL && ((CContainer *)target->parent)->lockOwner != NULL) {
		savedParentContainer = target->parent;
		CLocation_SetLoc(&tempLoc, &target->resourceEntity.entity.location);
	}

	savedSourceSerial = source->serial;

	Entity_ExecuteEvent(&source->resourceEntity.entity, StackOnto, target->serial);

	if (World_ValidateEntity(source, savedSourceSerial) == NULL)
		return 1;

	if (target->resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN(target, VT_HIDE))(target);

	CItem_SetDecayCount(target, 0);

	if (source->resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN(source, VT_HIDE))(source);

	if (CResourceEntity_HasTag(target, "valueless", 0)) {
		if (!CResourceEntity_HasTag(source, "valueless", 0)) {
			CResourceEntity_DetachScript(target, "valueless");
		}
	}

	consumed = CItem_ConsumeAmount(source, target, CItem_GetMinResourceRatio(source));

	if (savedParentContainer != NULL) {
		((void (*)(void *, void *, void *))VT_FN(target, VT_ADD_TO_CONTAINER))(target, savedParentContainer, &tempLoc);
	} else {
		if (!((int (*)(void *))VT_FN(target, VT_RETURN_TO_TRACKED))(target))
			return 0;
	}

	if (savedParent != NULL)
		((void (*)(void *))VT_FN(target, VT_REATTACH_SPATIAL))(target);

	if (consumed != 0 && source != NULL)
		((void (*)(void *))VT_FN(source, VT_DELETE))(source);

	return 1;
}

/*
 * 0x0048A7B6 - CItem::MergeIntoWrapper
 *
 * Forwards to CItem_MergeInto.
 */
int
CItem_MergeIntoWrapper(CItem *this, CItem *target)
{
	return CItem_MergeInto(this, target);
}

/*
 * 0x0048A7D4 - CItem::SplitByAmount (FIXED)
 *
 * Spawns a clone of this with matching color/tracking and consumes the
 * requested amount into it, copying the valueless tag when set, then
 * re-places the source via AddToContainer or ReturnToTracked.
 *
 * FIXED: the binary accesses this after ConsumeAmount may have
 * deleted it when amount equals the remaining resources; we skip the
 * post-consume code when ConsumeAmount reports deletion.
 */
CItem *
CItem_SplitByAmount(CItem *this, uint16_t amount)
{
	CItem *newItem;
	CItem *savedParent;
	CLocation loc;
	CLocation tmpLoc;
	CLocation *srcLoc;

	savedParent = NULL;

	CLocation_Init(&loc);

	if (((int (*)(void *))VT_FN(this, VT_HAS_RESOURCE_FLAG))(this))
		CItem_GetMinResourceRatio(this);

	if (this->parent != NULL) {
		if (((CContainer *)this->parent)->lockOwner != NULL) {
			savedParent = this->parent;
			srcLoc = CEntity_GetLocation(&this->resourceEntity.entity);
			CLocation_SetLoc(&loc, srcLoc);
		}
	}

	if (this->resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN(this, VT_HIDE))(this);

	this->decayCount = 0;

	newItem = CWorld_CreateItem(g_World, CEntity_GetBodyType(this));

	newItem->resourceEntity.entity.color = this->resourceEntity.entity.color;

	CItem_ReleaseTracking(newItem);

	CItem_SetLastContainer(newItem, CMobile_GetSerial((CMobile *)this));

	srcLoc = ((CLocation * (*)(void *)) VT_FN(this, VT_GET_LOCATION))(this);
	CLocation_SetLoc(&tmpLoc, srcLoc);
	CItem_SetLastLocation(newItem, (int16_t)tmpLoc.x, (int16_t)tmpLoc.y, tmpLoc.z);

	if (CResourceEntity_HasTag(this, "valueless", 0)) {
		CEntity_SetObjVar(newItem, "valueless", 0, 1);
	}

	CItem_ConsumeAmount(this, newItem, amount & 0xFFFF);

	if (savedParent != NULL) {
		((void (*)(void *, CItem *, CLocation *))VT_FN(this, VT_ADD_TO_CONTAINER))(this, savedParent, &loc);
	} else {
		((void (*)(void *))VT_FN(this, VT_RETURN_TO_TRACKED))(this);
	}

	return newItem;
}

/*
 * 0x0048A933 - CItem::SplitByResources
 *
 * Like SplitByAmount but transfers resources through the source's
 * template (TransferResourcesByTemplate) instead of the destination's.
 */
CItem *
CItem_SplitByResources(CItem *this, uint16_t amount)
{
	CItem *newItem;
	CItem *savedParent;
	CLocation loc;
	CLocation tmpLoc;
	CLocation *srcLoc;

	savedParent = NULL;

	CLocation_Init(&loc);

	if (((int (*)(void *))VT_FN(this, VT_HAS_RESOURCE_FLAG))(this))
		CItem_GetMinResourceRatio(this);

	if (this->parent != NULL) {
		if (((CContainer *)this->parent)->lockOwner != NULL) {
			savedParent = this->parent;
			srcLoc = CEntity_GetLocation(&this->resourceEntity.entity);
			CLocation_SetLoc(&loc, srcLoc);
		}
	}

	((void (*)(void *))VT_FN(this, VT_HIDE))(this);

	newItem = CWorld_CreateItem(g_World, CEntity_GetBodyType(this));

	newItem->resourceEntity.entity.color = this->resourceEntity.entity.color;

	CItem_ReleaseTracking(newItem);

	CItem_SetLastContainer(newItem, CMobile_GetSerial((CMobile *)this));

	srcLoc = ((CLocation * (*)(void *)) VT_FN(this, VT_GET_LOCATION))(this);
	CLocation_SetLoc(&tmpLoc, srcLoc);
	CItem_SetLastLocation(newItem, (int16_t)tmpLoc.x, (int16_t)tmpLoc.y, tmpLoc.z);

	CItem_TransferResourcesByTemplate(this, newItem, amount & 0xFFFF);

	if (savedParent != NULL) {
		((void (*)(void *, CItem *, CLocation *))VT_FN(this, VT_ADD_TO_CONTAINER))(this, savedParent, &loc);
	} else {
		((void (*)(void *))VT_FN(this, VT_RETURN_TO_TRACKED))(this);
	}

	this->decayCount = 0;

	return newItem;
}

/*
 * 0x0048AA58 - CCorpse::CCorpse
 *
 * Corpse/multi constructor: chains to CContainer, installs the CMulti
 * vtable, and zeros the equipment slots and decay timer.
 */
void
CCorpse_Constructor(CCorpse *this)
{
	int i;

	CContainer_Constructor(&this->container);
	this->container.item.resourceEntity.entity.vtable = &g_vtable_CMulti;
	this->decayTick = 0;
	for (i = 0; i < 26; i++)
		this->equipSlots[i] = 0;
}

/*
 * 0x0048AAAE - CCorpse::~CCorpse
 *
 * Corpse destructor: resets the vtable to CMulti, hides the corpse if
 * it is still in the world, clears scripts and tags, and chains to the
 * CContainer destructor.
 */
void
CCorpse_Destructor(CCorpse *this)
{
	CItem *item = &this->container.item;

	item->resourceEntity.entity.vtable = &g_vtable_CMulti;

	if (!item->resourceEntity.entity.removedFromWorld)
		CItem_HideVT(item);

	CItem_ClearScriptsAndTags(item);

	CContainer_Destructor(item);
}

/*
 * 0x0048AB40 - CCorpse::SetLookAtText
 *
 * Builds "a/an {name} corpse" using the g_articleLookup table for the
 * article and stores the result in the lookAtText ObjVar.
 */
void
CCorpse_SetLookAtText(CItem *corpse, CString *name)
{
	char buf[160];
	CString lookAtName;
	CString tag;
	int firstChar;
	int idx;

	buf[0] = '\0';
	if (!CString_IsEmpty(name)) {
		strcat(buf, CString_GetBuffer(name));
		strcat(buf, " ");
	}
	strcat(buf, "corpse");

	g_CorpseNameBuf[0] = '\0';

	firstChar = (int)(signed char)buf[0];
	idx = firstChar - 0x61;
	if ((unsigned int)idx <= 0x14) {
		if (g_articleLookup[idx] <= 4)
			strcat(g_CorpseNameBuf, "an ");
		else
			strcat(g_CorpseNameBuf, "a ");
	} else {
		strcat(g_CorpseNameBuf, "a ");
	}
	strcat(g_CorpseNameBuf, buf);

	CString_Constructor(&lookAtName, g_CorpseNameBuf);
	CString_Constructor(&tag, "lookAtText");
	ObjVar_SetStr(corpse, &tag, 1, (uintptr_t)&lookAtName);
	CString_Destructor(&lookAtName);
}

/*
 * 0x0048ACC2 - CCorpse::SetPlayerLookAtText
 *
 * Stores "a corpse of {playerName}" in the corpse's lookAtText ObjVar.
 */
void
CCorpse_SetPlayerLookAtText(CItem *corpse, CString *playerName)
{
	CString lookAtName;
	CString tag;

	CString_Constructor(&lookAtName, "a corpse of ");
	CString_ConcatCString(&lookAtName, playerName);

	CString_Constructor(&tag, "lookAtText");
	ObjVar_SetStr(corpse, &tag, 1, (uintptr_t)&lookAtName);
	CString_Destructor(&lookAtName);
}

/*
 * 0x0048B625 - CItem::ClearScriptsAndTags
 *
 * Stops any active script threads bound to this entity and tears down
 * its tag list.
 */
void
CItem_ClearScriptsAndTags(CItem *self)
{
	if (self->tagList != NULL) {
		ThreadList_StopByScriptField(&g_activeThreadList, (uintptr_t)self);
		CTagListManager_Destroy(self->tagList);
		self->tagList = NULL;
	}
}

/*
 * 0x0048D192 - CEntity::SayCString (vtable[0x54])
 *
 * Broadcasts an ASCII SPEECH packet at the speaker's head height to
 * everyone in range 18 with line-of-sight. For player containers it
 * also triggers the nearby-NPC speech hook. CMobile overrides this.
 */
void
CEntity_SayCString_VT(CItem *self, char *text, int hue, int type, int font)
{
	uint8_t obuf[0x42C];
	CLocation loc;
	CLocation delta;
	int height;

	if (font == -1)
		font = 0;
	if (hue == -1)
		hue = 0x3B2;
	if (type == -1)
		type = 3;

	PacketManager_MakePacket_TEXT(obuf, self, self, (uint8_t)font, text, (uint16_t)hue, (uint16_t)type);

	height = ((int (*)(void *))VT_FN(self, VT_GET_HEIGHT))(self);
	CLocation_Constructor3D(&delta, 0, 0, (int16_t)(height / 2));
	CLocation_AddWrapped(CEntity_GetLocation(&self->resourceEntity.entity), &loc, &delta);
	BroadcastToNearbyWithLOS(obuf, &loc, 18);

	// Dead code for CEntity/CContainer: CPlayer overrides slot 0x54.
	if (VT_IsContainer(self) && VT_IsPlayer(self)) {
		CWorld_SpeechNotifyNearby(self, self->serial, &self->resourceEntity.entity.location, text, 18);
	}
}

/*
 * 0x0048D2A5 - CItem::SayCUString (vtable[0x50])
 *
 * Unicode SayCString: identical flow with a TEXT_UNICODE packet.
 */
void
CItem_SayCUString_VT(CItem *self, uint16_t *text, int hue, int type, int font)
{
	uint8_t obuf[0x830];
	CLocation loc;
	CLocation delta;
	int height;

	if (font == -1)
		font = 0;
	if (hue == -1)
		hue = 0x3B2;
	if (type == -1)
		type = 3;

	PacketManager_MakePacket_TEXT_UNICODE(obuf, self, self, (uint8_t)font, text, (uint16_t)hue, (uint16_t)type, 0);

	height = ((int (*)(void *))VT_FN(self, VT_GET_HEIGHT))(self);
	CLocation_Constructor3D(&delta, 0, 0, (int16_t)(height / 2));
	CLocation_AddWrapped(CEntity_GetLocation(&self->resourceEntity.entity), &loc, &delta);
	BroadcastToNearbyWithLOS(obuf, &loc, 18);

	if (VT_IsContainer(self) && VT_IsPlayer(self)) {
		CWorld_SpeechNotifyNearbyUnicode(g_World, self->serial, &self->resourceEntity.entity.location, text, 0, 18);
	}
}

/*
 * 0x0048D3BC - CItem::EmoteCString (vtable[0x5C])
 *
 * Emote variant of SayCString, defaulting the font to 7.
 */
void
CItem_EmoteCString_VT(CItem *self, char *text, int hue, int type, int font)
{
	uint8_t obuf[0x42C];
	CLocation loc;
	CLocation delta;
	int height;

	if (font == -1)
		font = 7;
	if (hue == -1)
		hue = 0x3B2;
	if (type == -1)
		type = 3;

	PacketManager_MakePacket_TEXT(obuf, self, self, (uint8_t)font, text, (uint16_t)hue, (uint16_t)type);

	height = ((int (*)(void *))VT_FN(self, VT_GET_HEIGHT))(self);
	CLocation_Constructor3D(&delta, 0, 0, (int16_t)(height / 2));
	CLocation_AddWrapped(CEntity_GetLocation(&self->resourceEntity.entity), &loc, &delta);
	BroadcastToNearbyWithLOS(obuf, &loc, 18);

	if (VT_IsContainer(self) && VT_IsPlayer(self)) {
		CWorld_SpeechNotifyNearby(self, self->serial, &self->resourceEntity.entity.location, text, 18);
	}
}

/*
 * 0x0048D4CF - CItem::EmoteCUString (vtable[0x58])
 *
 * Unicode emote variant, font 7 by default.
 */
void
CItem_EmoteCUString_VT(CItem *self, uint16_t *text, int hue, int type, int font)
{
	uint8_t obuf[0x830];
	CLocation loc;
	CLocation delta;
	int height;

	if (font == -1)
		font = 7;
	if (hue == -1)
		hue = 0x3B2;
	if (type == -1)
		type = 3;

	PacketManager_MakePacket_TEXT_UNICODE(obuf, self, self, (uint8_t)font, text, (uint16_t)hue, (uint16_t)type, 0);

	height = ((int (*)(void *))VT_FN(self, VT_GET_HEIGHT))(self);
	CLocation_Constructor3D(&delta, 0, 0, (int16_t)(height / 2));
	CLocation_AddWrapped(CEntity_GetLocation(&self->resourceEntity.entity), &loc, &delta);
	BroadcastToNearbyWithLOS(obuf, &loc, 18);

	if (VT_IsContainer(self) && VT_IsPlayer(self)) {
		CWorld_SpeechNotifyNearbyUnicode(g_World, self->serial, &self->resourceEntity.entity.location, text, 0, 18);
	}
}

/*
 * 0x0048D5E6 - CItem::SayToEntity (vtable[0x6C])
 *
 * Thin wrapper that calls SayHuedCString with the default 0x3B2 hue.
 */
void
CItem_SayToEntity_VT(CItem *self, CItem *target, uint32_t serial, char *text)
{
	((void (*)(void *, CItem *, uint32_t, char *, uint16_t))VT_FN(self, VT_SAY_HUED_CSTRING))(self, target, serial, text, 0x3B2);
}

/*
 * 0x0048D60F - CItem::SayToCUString (vtable[0x68])
 *
 * Unicode version: forwards to SayHuedCUString with hue 0x3B2.
 */
void
CItem_SayToCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text)
{
	((void (*)(void *, CItem *, uint32_t, uint16_t *, uint16_t))VT_FN(self, VT_SAY_HUED_CUSTRING))(self, target, serial, text, 0x3B2);
}

/*
 * 0x0048D638 - CItem::EmoteToEntity (vtable[0x7C])
 *
 * Emote counterpart: forwards to EmoteHuedCString with hue 0x3B2.
 */
void
CItem_EmoteToEntity_VT(CItem *self, CItem *target, uint32_t serial, char *text)
{
	((void (*)(void *, CItem *, uint32_t, char *, uint16_t))VT_FN(self, VT_EMOTE_HUED_CSTRING))(self, target, serial, text, 0x3B2);
}

/*
 * 0x0048D661 - CItem::EmoteToCUString (vtable[0x78])
 *
 * Unicode emote: forwards to EmoteHuedCUString with hue 0x3B2.
 */
void
CItem_EmoteToCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text)
{
	((void (*)(void *, CItem *, uint32_t, uint16_t *, uint16_t))VT_FN(self, VT_EMOTE_HUED_CUSTRING))(self, target, serial, text, 0x3B2);
}

/*
 * 0x0048D68A - CItem::SayHuedCString (vtable[0x64])
 *
 * Sends a directed ASCII TEXT speech packet (font 3) with the given
 * hue to target via Entity_BroadcastPacket.
 */
void
CItem_SayHuedCString_VT(CItem *self, CItem *target, uint32_t serial, char *text, uint16_t hue)
{
	uint8_t obuf[0x42C];

	PacketManager_MakePacket_TEXT(obuf, self, self, 0, text, hue, 3);
	Entity_BroadcastPacket(target, serial, obuf);
}

/*
 * 0x0048D6E0 - CItem::SayHuedCUString (vtable[0x60])
 *
 * Unicode version of SayHuedCString.
 */
void
CItem_SayHuedCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text, uint16_t hue)
{
	uint8_t obuf[0x830];

	PacketManager_MakePacket_TEXT_UNICODE(obuf, self, self, 0, text, hue, 3, 0);
	Entity_BroadcastPacket(target, serial, obuf);
}

/*
 * 0x0048D738 - CItem::EmoteHuedCString (vtable[0x74])
 *
 * Emote counterpart of SayHuedCString (speech type 7).
 */
void
CItem_EmoteHuedCString_VT(CItem *self, CItem *target, uint32_t serial, char *text, uint16_t hue)
{
	uint8_t obuf[0x42C];

	PacketManager_MakePacket_TEXT(obuf, self, self, 7, text, hue, 3);
	Entity_BroadcastPacket(target, serial, obuf);
}

/*
 * 0x0048D78E - CItem::EmoteHuedCUString (vtable[0x70])
 *
 * Unicode EmoteHuedCString.
 */
void
CItem_EmoteHuedCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text, uint16_t hue)
{
	uint8_t obuf[0x830];

	PacketManager_MakePacket_TEXT_UNICODE(obuf, self, self, 7, text, hue, 3, 0);
	Entity_BroadcastPacket(target, serial, obuf);
}

/*
 * 0x0048D7E6 - Convo_NotifyNearbyNPCs
 *
 * Collects scripted entities in nearby blocks, sorts them by Chebyshev
 * distance to loc, and fires script event 0 on each nearest-first.
 * Returns 0 once a handler consumes the event (text starting with '@'
 * is also consumed up front).
 *
 * FIXED: The binary breaks out of the collection loop when the 251-slot
 * buffer fills, so scripted items in later-scanned blocks are dropped
 * even when they are closer than items already collected. Replaced the
 * overflow break with a K-nearest replacement: when full, swap the
 * farthest entry for the incoming item if it is closer.
 */
int
Convo_NotifyNearbyNPCs(uint32_t speakerSerial, CLocation *loc, const char *text, int range)
{
	int blockBuf[256];
	CItem *scriptItems[256];
	int scriptDist[256];
	int count = 0;
	int i, j, bestIdx, bestDist, dist;
	int maxDist = 0;
	int maxIdx = 0;
	CItem *tmp;

	if (text[0] == '@')
		return 0;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockBuf, 256);

	for (i = 0; blockBuf[i] != -1; i++) {
		CBlock *block = CBlockManager_GetBlock(&g_SpatialGrid, blockBuf[i]);
		CItem *item = block->itemHead;
		while (item != NULL) {
			if (item->tagList != NULL && CTagListManager_HasScripts(item->tagList)) {
				dist = CLocation_ChebyshevDistance(&item->resourceEntity.entity.location, loc);
				if (count <= 250) {
					scriptItems[count] = item;
					scriptDist[count] = dist;
					if (count == 0 || dist > maxDist) {
						maxDist = dist;
						maxIdx = count;
					}
					count++;
				} else if (dist < maxDist) {
					scriptItems[maxIdx] = item;
					scriptDist[maxIdx] = dist;
					maxDist = scriptDist[0];
					maxIdx = 0;
					for (j = 1; j <= 250; j++) {
						if (scriptDist[j] > maxDist) {
							maxDist = scriptDist[j];
							maxIdx = j;
						}
					}
				}
			}
			item = item->spatialNext;
		}
	}

	for (i = 0; i < count - 1; i++) {
		bestIdx = i;
		bestDist = CLocation_ChebyshevDistance(&scriptItems[i]->resourceEntity.entity.location, loc);
		for (j = i + 1; j < count; j++) {
			dist = CLocation_ChebyshevDistance(&scriptItems[j]->resourceEntity.entity.location, loc);
			if (dist < bestDist) {
				bestDist = dist;
				bestIdx = j;
			}
		}
		if (bestIdx != i) {
			tmp = scriptItems[i];
			scriptItems[i] = scriptItems[bestIdx];
			scriptItems[bestIdx] = tmp;
		}
	}

	for (i = 0; i < count; i++) {
		if (!Entity_ExecuteEvent(&scriptItems[i]->resourceEntity.entity, 0, speakerSerial, text))
			return 0;
	}
	return 1;
}

/*
 * 0x0048D9D7 - CWorld::SpeechNotifyNearby
 *
 * Thiscall shim that forwards to Convo_NotifyNearbyNPCs.
 */
void
CWorld_SpeechNotifyNearby(CItem *self, uint32_t serial, CLocation *loc, const char *text, int range)
{
	USED(self);
	Convo_NotifyNearbyNPCs(serial, loc, text, range);
}
/*
 * 0x0048EAEB - CItem::SpeakSysMsg (vtable[0x4C])
 *
 * Returns the item's display name, preferring the lookAtText ObjVar
 * over the virtual GetName fallback.
 */
char *
CItem_SpeakSysMsg_VT(CItem *item, int flag)
{
	CString *text;

	if (CResourceEntity_HasTag(item, "lookAtText", 1)) {
		text = CResourceEntity_GetTagString(item, "lookAtText");
		return CString_GetData(text);
	}
	return ((char *(*)(void *, int))VT_FN(item, VT_GET_NAME_STRING))(item, flag);
}

/*
 * 0x0048EB39 - CItem::MoveMulti
 *
 * Moves the multi owned by this item to loc after validating the
 * target coordinates; returns 0 if the item owns no multi or the
 * coordinates are invalid.
 */
int
CItem_MoveMulti(CItem *item, CLocation *loc)
{
	CMultiComponent *mc;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;

	if (item->multiPtr == NULL)
		return 0;

	mc = item->multiPtr;
	if (!CMultiComponent_IsOwner(mc))
		return 0;

	return CMultiSlave_Move((CMultiSlave *)mc, loc);
}

/*
 * 0x0048EB98 - CItem::MoveMultiCheck
 *
 * Check-only variant of MoveMulti that forwards to
 * CMultiSlave_MoveCheck with the extra flag.
 */
int
CItem_MoveMultiCheck(CItem *item, CLocation *loc, int checkFlag)
{
	CMultiComponent *mc;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;

	if (item->multiPtr == NULL)
		return 0;

	mc = item->multiPtr;
	if (!CMultiComponent_IsOwner(mc))
		return 0;

	return CMultiSlave_MoveCheck((CMultiSlave *)mc, loc, checkFlag);
}

/*
 * 0x0048EBFB - CItem::HasHarvestableResource
 *
 * True when the item carries a type 3 resource node with a non-zero id
 * and a positive value3. Items with no such node still qualify when
 * their body type falls in 0x2008 to 0x20CF.
 */
static __attribute__((unused)) int
CItem_HasHarvestableResource(CItem *item)
{
	CResourceNode *node;

	for (node = item->resourceEntity.firstChild; node != NULL; node = node->next) {
		if (node->id != 0 && node->type == 3 && node->value3 > 0)
			return 1;
	}

	if (CEntity_GetBodyType(item) > 0x2007 && CEntity_GetBodyType(item) < 0x20D0)
		return 1;
	return 0;
}

/*
 * 0x0048EC7D - CItem::MultiAwareDistance
 *
 * 3D distance between self and target, measured to the nearest multi
 * component when either side owns a multi, otherwise plain wrapped
 * Location_Distance3D.
 */
int
CItem_MultiAwareDistance(CItem *self, CItem *target)
{
	if (CItem_IsMultiOwner(self)) {
		return CMultiSlave_MinDistToEntity(CItem_GetMultiSlave(self), target);
	}

	if (CItem_IsMultiOwner(target)) {
		return CMultiSlave_MinDistToEntity(CItem_GetMultiSlave(target), self);
	}

	return Location_Distance3D((int16_t)self->resourceEntity.entity.location.x, (int16_t)self->resourceEntity.entity.location.y, self->resourceEntity.entity.location.z,
	        (int16_t)target->resourceEntity.entity.location.x, (int16_t)target->resourceEntity.entity.location.y, target->resourceEntity.entity.location.z);
}

/*
 * 0x0048F13A - CItem::DecayProcess
 *
 * Marks the item as valueless and seeds its decayCount with the
 * world's global decay max.
 */
void
CItem_DecayProcess(CItem *item)
{
	CString _n;

	CString_Constructor(&_n, "valueless");
	ObjVar_SetStr(item, &_n, 0, (uint32_t)1);
	CItem_SetDecayCount(item, CItem_GetGlobalDecayMax(item));
}

/*
 * 0x0048F17D - CItem::IsValueless
 *
 * Returns 1 when the item has the valueless int tag.
 */
int
CItem_IsValueless(CItem *item)
{
	int val = 0;

	if (CItem_GetTagInt(item, "valueless", &val) == 1)
		return 1;
	return 0;
}

/*
 * 0x0048F295 - CItem::CopyObjVar
 *
 * Copies a single ObjVar from source to dest, optionally renaming it.
 * Returns 1 on success, 0 if the tag is missing on source.
 */
int
CItem_CopyObjVar(CItem *dest, CItem *source, const char *tagName, const char *destName)
{
	TagNode *node;

	node = CItem_GetTagNodeRaw(source, tagName);
	if (node == NULL)
		return 0;

	if (destName == NULL)
		destName = tagName;

	CEntity_SetObjVar(dest, destName, node->type, node->value);
	return 1;
}
/*
 * 0x0048F9BA - CItem::AddToAttackerList (vtable[0x194])
 *
 * Wraps AddToAggressorList: fills an AggroInfo from attacker and
 * threads it into the aggressor list for this entity.
 */
void
CItem_AddToAttackerList_VT(CItem *this, CItem *attacker)
{
	AggroInfo aggroInfo;

	if (attacker == NULL)
		return;

	AggroInfo_Constructor(&aggroInfo);

	((void (*)(void *, AggroInfo *))VT_FN(attacker, VT_FILL_AGGRO_INFO))(attacker, &aggroInfo);

	((void (*)(void *, CItem *, CItem *, uint32_t))VT_FN(this, VT_ADD_TO_AGGRESSOR_LIST))(this, attacker, aggroInfo.entity, aggroInfo.serial);

	AggroInfo_Destructor(&aggroInfo);
}

/*
 * 0x0048FA3A - CEntity::RefreshAggression (vtable[0x184])
 *
 * Schedules the criminal-flag timer (480 ticks), a second lawfully-
 * damaged timer when the matching tag is set, and a controller
 * timeout when the controllerTimeout tag is positive.
 */
void
CEntity_RefreshAggression(CItem *ent)
{
	int controllerTimeout;

	ScheduleEvent(0x1E0, ent->serial, 0x11, 1, 0);

	if (CResourceEntity_HasTag(ent, "lawfullyDamaged", 5))
		ScheduleEvent(0x1E0, ent->serial, 0x11, 2, 0);

	controllerTimeout = 0;
	CResourceEntity_GetTagInt(ent, "controllerTimeout", &controllerTimeout);
	if (controllerTimeout > 0)
		ScheduleEvent(controllerTimeout, ent->serial, 0x14, 0, 0);
}

/*
 * 0x00490281 - CItem::FameKarmaChange (vtable[0x1A0])
 *
 * Redirects fame/karma deltas to the controller entity (when in-world)
 * or sends a changeReputation multi-message otherwise. Overridden by
 * CPlayer to update the player directly.
 */
void
CItem_FameKarmaChange_VT(CItem *self, int fame, int karma)
{
	AggroInfo aggroInfo;

	AggroInfo_Constructor(&aggroInfo);

	((void (*)(void *, AggroInfo *))VT_FN(self, VT_FILL_AGGRO_INFO))(self, &aggroInfo);

	if (aggroInfo.serial == 0) {
		AggroInfo_Destructor(&aggroInfo);
		return;
	}

	if (aggroInfo.entity != NULL) {
		((void (*)(void *, int, int))VT_FN(aggroInfo.entity, VT_FAME_KARMA_CHANGE))(aggroInfo.entity, fame, karma);
		AggroInfo_Destructor(&aggroInfo);
		return;
	}

	{
		CList list;
		CString msgName;

		CList_Constructor(&list);
		CList_Append(&list, 0, (uint32_t)fame);
		CList_Append(&list, 0, (uint32_t)karma);
		CString_Constructor(&msgName, "changeReputation");

		SendMultiMessage(aggroInfo.serial, self->serial, &msgName, (intptr_t)&list);

		CString_Destructor(&msgName);
		CList_Destructor(&list);
	}

	AggroInfo_Destructor(&aggroInfo);
}

/*
 * 0x00490392 - StaticEntity::IsDoor (vtable[0x84])
 *
 * Checks the TF_DOOR tiledata flag directly on the static's bodyType
 * (no stackable offset - statics have no itemFlags).
 */
int
StaticEntity_IsDoor_VT(CItem *item)
{
	uint16_t bodyType;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	if (g_ItemTileData[bodyType].flags & TF_DOOR)
		return 1;
	return 0;
}

/*
 * 0x00490400 - CItem::IsDoor (vtable[0x84])
 *
 * Checks TF_DOOR in the stackable-adjusted tiledata entry.
 */
int
CItem_IsDoor_VT(CItem *item)
{
	uint16_t idx;

	idx = (uint16_t)(CEntity_GetBodyType(item) + (CItem_IsStackable(item) & 0xFFFF));
	if (g_ItemTileData[idx].flags & TF_DOOR)
		return 1;
	return 0;
}

/*
 * 0x0049045A - CItem::IsDoorNotStackable (vtable[0x88])
 *
 * True when IsDoor fires and the item is not currently stackable.
 */
int
CItem_IsDoorNotStackable_VT(CItem *item)
{
	uint16_t idx;

	idx = (uint16_t)(CEntity_GetBodyType(item) + (CItem_IsStackable(item) & 0xFFFF));
	if (!(g_ItemTileData[idx].flags & TF_DOOR))
		return 0;
	if (CItem_IsStackable(item) & 0xFFFF)
		return 0;
	return 1;
}

/*
 * 0x004904C5 - CItem::IsLocked
 *
 * Returns 1 when the item has the isLocked tag.
 */
int
CItem_IsLocked(CItem *item)
{
	return CResourceEntity_HasTag(item, "isLocked", 0) != 0;
}

/*
 * 0x004904E5 - CItem::IsHair (vtable[0x38])
 *
 * True for wearable items at layer 0x0B (hair) or 0x10 (beard).
 */
int
CItem_IsHair_VT(CItem *item)
{
	if (!CWorld_LookupItemResource(CEntity_GetBodyType(item)))
		return 0;
	if (!(g_ItemTileData[CEntity_GetBodyType(item) & 0xFFFF].flags & 0x00400000))
		return 0;
	if ((CItem_GetLayerThiscall(item) & 0xFF) == 0x0B)
		return 1;
	if ((CItem_GetLayerThiscall(item) & 0xFF) == 0x10)
		return 1;
	return 0;
}

/*
 * 0x00490623 - CItem::IsGold
 *
 * Returns 1 when bodyType == 0x0EED (gold pile).
 */
int
CItem_IsGold(CItem *item)
{
	return (CEntity_GetBodyType(item) & 0xFFFF) == 0x0EED;
}

/*
 * 0x0049064B - CItem::IsInWorld (vtable[0x11C])
 *
 * True for items attached to a multi or for which the 0x9C hook
 * returns non-zero. Mobiles/NPCs/players override to return 0.
 */
int
CItem_IsInWorld_VT(CItem *item)
{
	if (CItem_HasMulti_Filter(item))
		return 1;
	if (((int (*)(void *))VT_FN(item, VT_ITEM_CHECK_9C))(item))
		return 1;
	return 0;
}

/*
 * 0x00490684 - CItem::CanFireEquipEvent
 *
 * Gate for Equip/Unequip events: only layers 1..24 are eligible.
 */
int
CItem_CanFireEquipEvent(int layer)
{
	if (layer < 1)
		return 0;
	if (layer > 0x18)
		return 0;
	return 1;
}

/*
 * 0x004906A6 - CItem::TransferTo (vtable[0xB0])
 *
 * Teleports the item to loc by hiding and re-dropping at feet;
 * returns 0 when the coordinates are off-world.
 */
int
CItem_TransferTo(CItem *item, CLocation *loc)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;
	((void (*)(CItem *))VT_FN(item, VT_HIDE))(item);
	((void (*)(CItem *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
	return 1;
}

/*
 * 0x004906F3 - CItem::SetLocationDropAtFeet
 *
 * Thin forwarder to CItem_DropAtFeet.
 */
void
CItem_SetLocationDropAtFeet(CItem *item, CLocation *loc)
{
	CItem_DropAtFeet(item, loc);
}

/*
 * 0x0049070C - CMobile::SetLocation (vtable[0x08])
 *
 * Inherited wrapper that just forwards to CItem_SetLocation_VT.
 */
void
CMobile_SetLocation_VT(CItem *self, CLocation *loc)
{
	CItem_SetLocation_VT(self, loc);
}

/*
 * 0x00490725 - CWorld::CollectAllSerials
 *
 * Appends the serial of every entity in the world's serial hash to out.
 * The binary reads the table through its absolute address rather than
 * through the passed world pointer, which it never touches.
 */
static __attribute__((unused)) void
CWorld_CollectAllSerials(CWorld *world, CVector *out)
{
	CItem *ent;
	int i;

	USED(world);

	for (i = 0; i < 0x10000; i++) {
		for (ent = g_World->hashTable[i]; ent != NULL; ent = ent->hashNext)
			CVector_PushBack(out, ent->serial);
	}
}

/*
 * 0x00490780 - CItem::HasScript
 *
 * Loads scriptName through the script manager and reports whether
 * this entity's script list references that script.
 */
int
CItem_HasScript(CItem *item, CString *scriptName)
{
	char *name;
	void *scriptClass;

	name = CString_GetData(scriptName);
	scriptClass = CScriptManager_FindOrLoad(&g_ScriptManager, name);
	if (scriptClass == NULL)
		return 0;
	if (CResourceEntity_HasScriptClass(item, scriptClass))
		return 1;
	return 0;
}

/*
 * 0x004907C4 - CItem::GetSurfaceHeight (vtable[0x2C])
 *
 * Returns GetHeight, halved when the bridge flag (0x400) is set in
 * tiledata flags.
 */
int
CItem_GetSurfaceHeight_VT(CItem *item)
{
	int height;

	height = ((int (*)(void *))VT_FN(item, VT_GET_HEIGHT))(item);
	if (((int (*)(void *))VT_FN(item, VT_GET_FLAGS))(item) & 0x400)
		height /= 2;
	return height;
}

/*
 * 0x00490801 - CItem::SetMsgId
 *
 * Stores msgId in the msgId ObjVar (type 4, object).
 */
void
CItem_SetMsgId(CItem *item, uint32_t msgId)
{
	CEntity_SetObjVar(item, "msgId", 4, msgId);
}

/*
 * 0x00490821 - CBulletinBoard::GetMsgId
 *
 * Reads a post item's msgId ObjVar, or 0 when absent.
 */
uint32_t
CBulletinBoard_GetMsgId(CItem *post)
{
	uint32_t val = 0;

	if (CResourceEntity_HasTag(post, "msgId", 4)) {
		CResourceEntity_GetTagObj(post, "msgId", &val);
	}
	return val;
}

/*
 * 0x0049085C - CItem::SetMsgOwner
 *
 * Stores the owner serial in the msgOwner ObjVar.
 */
void
CItem_SetMsgOwner(CItem *item, uint32_t owner)
{
	CEntity_SetObjVar(item, "msgOwner", 4, owner);
}

/*
 * 0x0049087C - CItem::GetMsgOwner
 *
 * Reads the msgOwner ObjVar, or 0 when unset.
 */
uint32_t
CItem_GetMsgOwner(CItem *item)
{
	uint32_t val;

	val = 0;
	if (CResourceEntity_HasTag(item, "msgOwner", 4))
		CResourceEntity_GetTagObj(item, "msgOwner", &val);
	return val;
}

/*
 * 0x004908B7 - CItem::SetMsgDay
 *
 * Stores the day value in the msgDay int ObjVar.
 */
void
CItem_SetMsgDay(CItem *item, uint32_t day)
{
	CEntity_SetObjVar(item, "msgDay", 0, day);
}

/*
 * 0x004908D7 - CItem::GetMsgDay
 *
 * Reads the msgDay ObjVar, or 0 when unset.
 */
static __attribute__((unused)) uint32_t
CItem_GetMsgDay(CItem *item)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(item, "msgDay", 0))
		CResourceEntity_GetTagInt(item, "msgDay", &val);
	return (uint32_t)val;
}

/*
 * 0x00490912 - CItem::InitTemplateChainPtrs
 *
 * Zeroes the template chain links. Called from the constructor.
 */
void
CItem_InitTemplateChainPtrs(CItem *item)
{
	item->templateChainPrev = NULL;
	item->templateChainNext = NULL;
}

/*
 * 0x00490931 - CItem::DetachTemplate
 *
 * Unlinks the item from its template chain and resets the template
 * index to 0xFFFF. Called from the destructor.
 */
void
CItem_DetachTemplate(CItem *item)
{
	TemplateChain_Remove(item);
	CResourceEntity_SetTemplateIndex(item, 0xFFFF);
}

/*
 * 0x00490951 - CItem::AttachTemplate
 *
 * Assigns the template index and links the item into the chain.
 */
void
CItem_AttachTemplate(CItem *item, uint16_t templateId)
{
	CResourceEntity_SetTemplateIndex(item, templateId);
	TemplateChain_Insert(item);
}

/*
 * 0x00490973 - TemplateChain_Insert
 *
 * Prepends the item onto g_TemplateChain for its template index and
 * bumps the per-template count.
 */
void
TemplateChain_Insert(CItem *item)
{
	int tid;

	tid = CResourceEntity_GetTemplateIndex(item) & 0xFFFF;
	if (tid == 0xFFFF)
		return;

	item->templateChainNext = g_TemplateChain[tid];
	if (item->templateChainNext != NULL)
		item->templateChainNext->templateChainPrev = item;
	item->templateChainPrev = NULL;
	g_TemplateChain[tid] = item;

	if (tid >= 0 && tid < TEMPLATE_CHAIN_SIZE)
		g_TemplateChainCount[tid]++;
}

/*
 * 0x004909FB - TemplateChain_Remove
 *
 * Unlinks the item from its template chain and decrements the
 * per-template count.
 */
void
TemplateChain_Remove(CItem *item)
{
	int tid;

	tid = CResourceEntity_GetTemplateIndex(item) & 0xFFFF;
	if (tid == 0xFFFF)
		return;

	if (item->templateChainNext != NULL)
		item->templateChainNext->templateChainPrev = item->templateChainPrev;
	if (item->templateChainPrev != NULL)
		item->templateChainPrev->templateChainNext = item->templateChainNext;
	else if (g_TemplateChain[tid] == item)
		g_TemplateChain[tid] = item->templateChainNext;

	item->templateChainNext = NULL;
	item->templateChainPrev = NULL;

	if (tid >= 0 && tid < TEMPLATE_CHAIN_SIZE)
		g_TemplateChainCount[tid]--;
}

/*
 * 0x00490AB0 - CItem::GetWordProp
 *
 * Thunk returning CItem_GetTiledataQuantity. Paired with GetByteProp.
 */
int
CItem_GetWordProp(CItem *item)
{
	return CItem_GetTiledataQuantity(item);
}

/*
 * 0x00490AD6 - CItem::GetLayer
 *
 * Looks up the tiledata layer for the item's bodyType.
 */
uint8_t
CItem_GetLayerThiscall(CItem *entity)
{
	return CWorld_GetItemLayer(CEntity_GetBodyType(entity));
}

/*
 * Stub - 0x004BAAEC - DebugTrap
 *
 * 5-byte int3/nop pad before CTemplateManager_SpawnVendorStock, invoked
 * on unexpected vendor-container state.
 */
void
CTemplateManager_SpawnVendorStock_DebugBreak(void)
{
}

/*
 * 0x00490AF4 - CWorld::GetItemLayer
 *
 * Calls CWorld_LookupItemResource for its side effect, then returns
 * g_ItemTileData[bodyType].layer.
 */
uint8_t
CWorld_GetItemLayer(uint16_t bodyType)
{
	CWorld_LookupItemResource(bodyType);
	return g_ItemTileData[bodyType].layer;
}

/*
 * 0x00490B24 - CItem::GetEquipSlot
 *
 * Forwarding thunk to CItem_GetLayerThiscall.
 */
uint8_t
CItem_GetEquipSlot(CItem *item)
{
	return CItem_GetLayerThiscall(item);
}

/*
 * 0x00490B37 - CItem::SetBookStatus
 *
 * Writes the low byte of status to the "bookStatus" int ObjVar.
 */
void
CItem_SetBookStatus(CItem *item, int status)
{
	int val;

	val = status & 0xFF;
	CEntity_SetObjVar(item, "bookStatus", 0, val);
}

/*
 * 0x00490B64 - CItem::GetBookStatus
 *
 * Returns the low byte of the "bookStatus" int tag, or 0 when absent.
 */
int
CItem_GetBookStatus(CItem *ent)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(ent, "bookStatus", 0)) {
		CResourceEntity_GetTagInt(ent, "bookStatus", &val);
	}
	return val & 0xFF;
}

/*
 * 0x00490B9F - CItem::IsWritableBook
 *
 * Returns 1 when the bodyType is in the writable-book range [0xFF1, 0xFF2].
 */
int
CItem_IsWritableBook(CItem *ent)
{
	int graphic;

	graphic = CEntity_GetBodyType(ent) & 0xFFFF;
	if (graphic >= 0xFF1 && graphic <= 0xFF2)
		return 1;
	return 0;
}

/*
 * 0x00490BD9 - CItem::IsRunebook
 *
 * Returns 1 if item's bodyType is in range [0xFEF, 0xFF0].
 */
int
CItem_IsRunebook(CItem *item)
{
	int bodyType;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	if (bodyType < 0xFEF)
		return 0;
	if (bodyType > 0xFF0)
		return 0;
	return 1;
}

/*
 * 0x00490C13 - CItem::SetDirectionVT
 *
 * Dispatches the low byte of dir through vtable[0x1B8] (VT_ON_DEATH).
 */
void
CItem_SetDirectionVT(CItem *item, int dir)
{
	int val;

	val = dir & 0xFF;
	((void (*)(void *, int))VT_FN(item, VT_ON_DEATH))(item, val);
}

/*
 * 0x00490C37 - CItem::SetWeight
 *
 * Stores weight on the "overloadedWeight" ObjVar, flags the item movable,
 * and broadcasts a nearby-update.
 */
void
CItem_SetWeight(CItem *item, int weight)
{
	CEntity_SetObjVar(item, "overloadedWeight", 0, weight);
	CItem_SetItemFlag(item, 1, 1);
	CItem_NotifyNearbyUpdate(item, 0);
}

/*
 * 0x00490C6D - CItem::GetSortKey
 *
 * Thiscall -> CItem_GetTiledataQuantity.
 */
static int
CItem_GetSortKey(CItem *item)
{
	return CItem_GetTiledataQuantity(item);
}

/*
 * 0x00490C80 - CItem::GetTiledataQuantity
 *
 * Returns the signed int16 miscData field from g_ItemTileData for the
 * item's bodyType.
 */
int
CItem_GetTiledataQuantity(CItem *item)
{
	uint32_t bodyType;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	return (int16_t)g_ItemTileData[bodyType].miscData;
}

/*
 * 0x00490CA6 - CItem::SetSortKey
 *
 * Binary no-op stub retained only as a callee of CShopkeeper_OpenBuyWindow.
 */
void
CItem_SetSortKey(CItem *item, int value)
{
	USED(item);
	USED(value);
}

/*
 * 0x00490CB3 - CItem::GetBookNum
 *
 * Returns "bookNum" tag value as uint16_t, or 0 if not present.
 */
int
CItem_GetBookNum(CItem *item)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(item, "bookNum", 0))
		CResourceEntity_GetTagInt(item, "bookNum", &val);
	return (uint16_t)val;
}

/*
 * 0x00490CEF - CItem::SetBookNum
 *
 * Writes the low word of num to the "bookNum" int ObjVar.
 */
void
CItem_SetBookNum(CItem *item, int num)
{
	int val;

	val = num & 0xFFFF;
	CEntity_SetObjVar(item, "bookNum", 0, val);
}

/*
 * 0x00490D1C - CItem::GetBookPages
 *
 * Returns "bookPages" tag as uint16_t, or falls back to
 * g_ItemTileData[bodyType].miscData (dword read, returned as uint16).
 */
int
CItem_GetBookPages(CItem *item)
{
	int val;

	val = 0;
	if (CResourceEntity_HasTag(item, "bookPages", 0))
		CResourceEntity_GetTagInt(item, "bookPages", &val);
	else
		val = (int)g_ItemTileData[CEntity_GetBodyType(item) & 0xFFFF].miscData;
	return (uint16_t)val;
}

/*
 * 0x00490D1C - GetBookPages (inner helper)
 *
 * Inlined variant that reads the entity's tagList directly.
 */
int
GetBookPages(CItem *ent)
{
	int pages = 0;

	if (ent->tagList != NULL && TagList_HasTag(ent->tagList, "bookPages", 7))
		TagList_GetTagInt(ent->tagList, "bookPages", &pages);
	else
		pages = (int)g_ItemTileData[ent->resourceEntity.entity.bodyType].miscData;
	return (uint16_t)pages;
}

/*
 * 0x00490D77 - CItem::SetBookPages
 *
 * Writes the low word of count to the "bookPages" int ObjVar.
 */
void
CItem_SetBookPages(CItem *item, int count)
{
	CEntity_SetObjVar(item, "bookPages", 0, (uint32_t)(count & 0xFFFF));
}

/*
 * 0x00490DA4 - CItem::GetSpellId
 *
 * Forwarding thunk to CItem_GetSortKey.
 */
static __attribute__((unused)) int
CItem_GetSpellId(CItem *item)
{
	return CItem_GetSortKey(item);
}

/*
 * 0x00490DB7 - CItem::GetSortKeyQty
 *
 * Duplicate of CItem_GetSpellId at a distinct binary address.
 */
static __attribute__((unused)) int
CItem_GetSortKeyQty(CItem *item)
{
	return CItem_GetSortKey(item);
}

/*
 * 0x00490DCA - CItem::GetAmount (vtable[0x168])
 *
 * Returns bookNum for runebooks, bookPages for writable books, otherwise
 * the tiledata miscData quantity.
 */
int
CItem_VT_GetAmount(CItem *item)
{
	if (CItem_IsRunebook(item))
		return CItem_GetBookNum(item);
	if (CItem_IsWritableBook(item))
		return CItem_GetBookPages(item);
	return CItem_GetSortKey(item);
}

/*
 * 0x00490E09 - CItem::GetStatusFlags (vtable[0x170])
 *
 * Maps itemFlags Invisible (0x02) to 0x80 and Movable (0x01) to 0x20 in
 * the output byte.
 */
void
CItem_GetStatusFlags_VT(CItem *self, uint8_t *flags)
{
	if (self->itemFlags & 0x02)
		*flags |= 0x80;
	if (self->itemFlags & 0x01)
		*flags |= 0x20;
}

/*
 * 0x00490E4D - GetStatusFlagsWrapper
 *
 * Convenience wrapper returning the status byte produced by vtable[0x170].
 */
uint8_t
GetStatusFlagsWrapper(CItem *item)
{
	uint8_t flags = 0;
	((void (*)(void *, uint8_t *))VT_FN(item, VT_GET_STATUS_FLAGS))(item, &flags);
	return flags;
}

/*
 * 0x00490E73 - CItem::ApplyStatusFlags
 *
 * Maps status byte bits 0x20 and 0x80 back to ItemFlag Movable and
 * Invisible respectively.
 */
void
CItem_ApplyStatusFlags(CItem *item, int flags)
{
	if ((flags & 0xFF) & 0x20)
		CItem_SetItemFlag(item, 1, 1);
	if ((flags & 0xFF) & 0x80)
		CItem_SetItemFlag(item, 2, 1);
}

/*
 * 0x00490EBA - CItem::HasMovableFlag
 *
 * Thin wrapper over CItem_HasItemFlag for the Movable bit.
 */
int
CItem_HasMovableFlag(CItem *item)
{
	return CItem_HasItemFlag(item, 1);
}

/*
 * 0x00490ECF - CItem::CheckOverloadedWeight
 *
 * Sets the Movable ItemFlag whenever an "overloadedWeight" tag is present.
 */
void
CItem_CheckOverloadedWeight(CItem *item)
{
	if (CResourceEntity_HasTag(item, "overloadedWeight", 0))
		CItem_SetItemFlag(item, 1, 1);
}

/*
 * 0x00490EF9 - HasStackableFlag
 *
 * Returns 1 when the item has the open / stackable flag (bit 0x04)
 * set.
 */
int
CItem_HasStackableFlag(CItem *item)
{
	return CItem_HasItemFlag(item, 4);
}

/*
 * 0x00490F0E - CItem::SetOpen
 *
 * Sets or clears the open-state flag (bit 0x04) on doors and
 * containers.
 */
void
CItem_SetOpen(CItem *item, int setOrClear)
{
	CItem_SetItemFlag(item, 4, setOrClear);
}

/*
 * 0x00490F29 - CItem::GetResourceAmountByName
 *
 * Returns the amount of resource resTypeId stored in the item, or 0
 * when the resource type is not registered.
 */
int
CItem_GetResourceAmountByName(CItem *self, int resTypeId)
{
	int result;
	CResourceType *resType;

	result = 0;
	resType = CResourceTypeManager_GetId(resTypeId);
	if (resType == NULL)
		return 0;
	result = 0;
	CItem_GetObjVarResTypeInner(self, &result, resType, 3, 2);
	return result;
}

/*
 * 0x00490F78 - CItem::ValidateInWorld
 *
 * Hides the entity first if it is already in world, then drops it at loc.
 */
int
CItem_ValidateInWorldVT(CItem *self, CLocation *loc)
{
	if (!(VT_IsRemoved(self) & 0xFF))
		((void (*)(void *))VT_FN(self, VT_HIDE))(self);
	((void (*)(void *, CLocation *))VT_FN(self, VT_DROP_AT_FEET))(self, loc);
	return 1;
}

/*
 * 0x00490FBB - CItem::RegisterLocation
 *
 * Re-places the entity via ValidateInWorld when its stored creation
 * location is invalid or matches loc.
 */
int
CItem_RegisterLocation(CItem *self, CLocation *loc)
{
	// The binary reads this+0x10 (creation location), not entity.location.
	CLocation *cloc = (CLocation *)&self->resourceEntity.nextInContainer;
	if (CLocation_IsInvalid(cloc) || CLocation_IsEqualXYZ(loc, cloc)) {
		return CItem_ValidateInWorldVT(self, loc);
	}
	return 0;
}

/*
 * 0x00490FFA - CItem::ValidateRegistration (vtable[0x158])
 *
 * Invokes vtable[0x15C] (FindSelfInWorld) on self and, if set, on parent.
 */
void
CItem_ValidateRegistration_VT(CItem *self)
{
	((CItem * (*)(CItem *)) VT_FN(self, VT_FIND_IN_WORLD))(self);

	if (self->parent != NULL) {
		((CItem * (*)(CItem *)) VT_FN(self->parent, VT_FIND_IN_WORLD))(self->parent);
	}
}

/*
 * 0x00491030 - CItem::FindSelfInWorld (vtable[0x15C])
 *
 * Looks up the entity's serial in the world hash to confirm registration.
 */
CItem *
CItem_FindSelfInWorld_VT(CItem *self)
{
	uint32_t serial;

	serial = CMobile_GetSerial((CMobile *)self);
	return CWorld_FindBySerial(g_World, serial);
}

/*
 * 0x00491056 - CItem::ConditionalValidate (vtable[0x160])
 *
 * Calls FindSelfInWorld unless the world is mid-load.
 */
void
CItem_ConditionalValidate_VT(CItem *self)
{
	if (g_World->isLoading)
		return;
	((CItem * (*)(CItem *)) VT_FN(self, VT_FIND_IN_WORLD))(self);
}

/*
 * 0x00491078 - StaticEntity::GetHeight (vtable[0x28])
 *
 * Returns tiledata quantity for the entity's bodyType; statics have no
 * door-offset path since they lack itemFlags.
 */
int
StaticEntity_GetHeight_VT(CItem *item)
{
	uint16_t bodyType;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	return (int)(uint8_t)g_ItemTileData[bodyType].quantity;
}

/*
 * 0x004910A7 - CItem::GetHeight (vtable[0x28])
 *
 * Returns tiledata quantity for the item's bodyType, reading bodyType+1
 * when the Open itemFlag is set.
 */
int
CItem_GetHeight_VT(CItem *item)
{
	uint16_t bodyType;
	int doorOffset;

	bodyType = CEntity_GetBodyType(item);
	doorOffset = CItem_HasStackableFlag(item);
	return (int)(uint8_t)g_ItemTileData[bodyType + doorOffset].quantity;
}

/*
 * 0x00491130 - StaticEntity scalar deleting destructor
 *
 * Dead during normal operation since statics go back through FreeStaticItem.
 */
CItem *
StaticEntity_ScalarDelete(CItem *self, int flags)
{
	if (flags & 2) {
		// Vector delete: binary stride 0x18 (32-bit CResourceEntity
		// without firstChild). Our 64-bit allocation pads the header
		// to sizeof(uintptr_t).
		uint32_t count = *(uint32_t *)((char *)self - sizeof(uintptr_t));
		uint32_t i;
		CItem *cur = (CItem *)((char *)self + count * sizeof(CResourceEntity));
		for (i = 0; i < count; i++) {
			cur = (CItem *)((char *)cur - sizeof(CResourceEntity));
			StaticEntity_Destructor(cur);
		}
		free((char *)self - sizeof(uintptr_t));
		return NULL;
	}
	StaticEntity_Destructor(self);
	if (flags & 1)
		free(self);
	return NULL;
}

/*
 * 0x00491190 - CItem scalar deleting destructor (vtable[0])
 *
 * Runs ~CItem and frees the item when flags & 1.
 */
void *
CItem_ScalarDelete(CItem *item, int flags)
{
	CItem_Destructor(item);
	if (flags & 1)
		free(item);
	return NULL;
}

/*
 * 0x004911C0 - CEntity::ItemCheck9C (vtable[0x9C])
 *
 * Writes 0 to *out; CItem and subclasses keep the stub.
 */
void
CEntity_ItemCheck9C_VT(CItem *self, uint16_t *out)
{
	USED(self);
	*out = 0;
}

/*
 * 0x004911F0 - CEntity::SpeakSysMsg (vtable[0x4C])
 *
 * Delegates to vtable[0x34] (GetName), ignoring the flag argument.
 */
char *
CEntity_SpeakSysMsg_VT(CItem *self, int flag)
{
	USED(flag);
	return ((char *(*)(void *))VT_FN(self, VT_GET_NAME))(self);
}

/*
 * 0x00491210 - StaticEntity::GetLocation (vtable[0x80])
 *
 * Returns the entity's own CLocation; statics have no parent path.
 */
CLocation *
StaticEntity_GetLocation_VT(CItem *item)
{
	return &item->resourceEntity.entity.location;
}

/*
 * 0x00491230 - StaticEntity::GetFlags (vtable[0x30])
 *
 * Returns tiledata flags for the bodyType; statics have no door-offset.
 */
int
StaticEntity_GetFlags_VT(CItem *item)
{
	uint16_t bodyType;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	return (int)g_ItemTileData[bodyType].flags;
}

/*
 * 0x00491260 - CItem::IsStackable
 *
 * Tests the Stackable ItemFlag set at creation from the tiledata.
 */
int
CItem_IsStackable(CItem *item)
{
	return CItem_HasStackableFlag(item);
}

/*
 * 0x00491280 - CItem::GetStoredWeight (vtable[0x124])
 *
 * Forwards to vtable[0x120] (GetWeight). CContainer and CMobile override.
 */
int
CItem_GetStoredWeightVT(CItem *item)
{
	return ((int (*)(void *))VT_FN(item, VT_GET_WEIGHT))(item);
}

/*
 * 0x004912C0 - CCorpse scalar deleting destructor (vtable[0])
 *
 * Runs ~CCorpse and frees the corpse when flags & 1.
 */
void *
CCorpse_ScalarDelete(CCorpse *corpse, int flags)
{
	CCorpse_Destructor(corpse);
	if (flags & 1)
		free(corpse);
	return NULL;
}

/*
 * 0x004CDB46 - CItem::ClearTagList
 *
 * Destroys the item's CTagListManager and clears the pointer.
 */
void
CItem_ClearTagList(CItem *item)
{
	if (item->tagList != NULL) {
		CTagListManager_Destroy(item->tagList);
		item->tagList = NULL;
	}
}

/*
 * 0x004CDB6F - CItem::HasScripts
 *
 * Returns nonzero when the tagList holds any script nodes.
 */
int
CItem_HasScripts(CItem *ent)
{
	if (ent->tagList == NULL)
		return 0;
	return CTagListManager_HasScripts(ent->tagList);
}

/*
 * 0x004CDB92 - CItem::HasTagDefs
 *
 * Returns nonzero when the entity has any ObjVar tag definitions.
 */
int
CItem_HasTagDefs(CItem *ent)
{
	if (ent->tagList == NULL)
		return 0;
	return CTagListManager_HasTagDefs(ent->tagList);
}

/*
 * 0x004CDBB5 - CItem::GetScriptListRaw
 *
 * Appends every script node from the tagList into the output vector.
 */
void
CItem_GetScriptListRaw(CItem *ent, CVector *list)
{
	if (ent->tagList != NULL)
		CTagListManager_WalkScriptNodes(ent->tagList, list);
}

/*
 * 0x004CDBDA - CItem::GetTagDefListRaw
 *
 * Appends the tagList's ObjVar definitions into the output vector.
 */
void
CItem_GetTagDefListRaw(CItem *ent, CVector *list)
{
	if (ent->tagList != NULL)
		CTagListManager_GetTagDefList(ent->tagList, list);
}

/*
 * 0x004CDC30 - CItem::PopulateObjVarList
 *
 * Collects ObjVar entries of the requested type into list.
 */
void
CItem_PopulateObjVarList(CItem *item, CVector *list, int type)
{
	if (item->tagList == NULL)
		return;
	ObjVarData_CollectEntries(item->tagList, list, type);
}

/*
 * 0x004CDC59 - CItem::GetTagNodeRaw
 *
 * Returns the tagList node matching name, or NULL.
 */
TagNode *
CItem_GetTagNodeRaw(CItem *item, const char *name)
{
	if (item->tagList == NULL)
		return NULL;
	return TagList_FindByName(item->tagList, name);
}

/*
 * 0x004CDC82 - CItem::FindTagByPrefix
 *
 * Returns the first tagList node whose name matches the 5-char prefix.
 */
TagNode *
CItem_FindTagByPrefix(CItem *item, const char *prefix)
{
	if (item->tagList == NULL)
		return NULL;
	return TagList_FindByPrefix(item->tagList, prefix);
}

/*
 * 0x004CDCAB - CItem::HasLinkedName
 *
 * Returns 1 when a "link"-prefixed STRING tag matches name+6.
 */
int
CItem_HasLinkedName(CItem *item, const char *name)
{
	if (item->tagList == NULL)
		return 0;
	return TagList_HasLinkedName(item->tagList, name);
}

/*
 * 0x004CDE73 - CItem::AddScript
 *
 * Prepends script to the entity's tagList (creating it on demand) and
 * wires the back-pointer.
 */
void
CItem_AddScript(CItem *this, ScriptAttachNode *script)
{
	if (this->tagList == NULL)
		this->tagList = TagListManager_New();
	CTagListManager_PrependScript(this->tagList, script);
	script->entity = this;
}

/*
 * 0x004CDF22 - CItem::HasScriptByName
 *
 * Returns 1 when a script whose name matches the given prefix is attached.
 */
int
CItem_HasScriptByName(CItem *item, const char *name)
{
	if (item->tagList == NULL)
		return 0;
	return CTagListManager_HasScriptByName(item->tagList, name);
}

/*
 * 0x004CDF74 - CItem::CompareScriptClasses
 *
 * Returns 1 when this and other carry the same set of script attachment
 * classes.
 */
int
CItem_CompareScriptClasses(CItem *this, CItem *other)
{
	CVector vec1, vec2;
	uint32_t count1;
	uintptr_t *iter;

	CVector_Constructor(&vec1, "");
	CVector_Constructor(&vec2, "");

	CItem_GetScriptListRaw(other, &vec1);
	CItem_GetScriptListRaw(this, &vec2);

	count1 = CVector_GetCount(&vec1);
	if (count1 != CVector_GetCount(&vec2)) {
		CVector_Destructor(&vec2);
		CVector_Destructor(&vec1);
		return 0;
	}

	for (iter = (uintptr_t *)vec1.begin; iter != (uintptr_t *)vec1.end; iter++) {
		ScriptAttachNode *node = (ScriptAttachNode *)*iter;
		if (!CResourceEntity_HasScriptClass(this, node->scriptClassPtr)) {
			CVector_Destructor(&vec2);
			CVector_Destructor(&vec1);
			return 0;
		}
	}

	CVector_Destructor(&vec2);
	CVector_Destructor(&vec1);
	return 1;
}

/*
 * 0x004E110F - CItem::WeightRelated (vtable[0x154])
 *
 * No-op for plain items.
 */
void
CItem_WeightRelated_VT(CItem *self)
{
	USED(self);
}

/*
 * Custom - EntityManager_EraseEntity
 *
 * Removes a disconnected entity from g_entityMgrList and g_ArchiveHash
 * during character deletion, which the binary never implemented.
 */
void
EntityManager_EraseEntity(CItem *entity)
{
	StdPtrList *list;
	StdPtrNode *iter, *beginTemp, *endTemp, *endCopy, *postIncTemp;
	StdPtrNode *eraseResult;

	ArchiveHash_Remove(entity);

	list = &g_entityMgrList;
	StdPtrIter_CopyConstructor(&iter, StdPtrList_Begin(list, &beginTemp));
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End(list, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		if (*(CItem **)StdPtrIter_Deref(&iter) == entity) {
			StdPtrList_Erase(list, &eraseResult, iter);
			return;
		}
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * Helper - CItem_DeleteContents_Nop
 *
 * vtable[0x1AC] no-op for CItem; real cleanup happens in the destructor
 * chain. CContainer and CMobile override this slot.
 */
void
CItem_DeleteContents_Nop(CItem *item)
{
	USED(item);
}

/*
 * Helper - CItem_IsContainer
 *
 * Wraps the vtable[0xD4] container-check dispatch.
 */
int
CItem_IsContainer(CItem *item)
{
	return VT_IsMobile2(item);
}

/*
 * Custom - Item_DestroyPools
 *
 * Server-shutdown cleanup. Walks the per-item tracking and static-
 * item freelists, marking each node defined so valgrind can read
 * its next pointer, then ends pool tracking on each. The VG_*
 * macros are no-ops when VALGRIND is not defined.
 */
void
Item_DestroyPools(void)
{
	CItemTracking *trCur, *trNext;
	CItem *staticCur, *staticNext;

	if (g_TrackingFreeList != NULL) {
		for (trCur = g_TrackingFreeList; trCur != NULL; trCur = trNext) {
			VG_MAKE_DEFINED(trCur, sizeof(*trCur));
			trNext = trCur->freeNext;
		}
		VG_DESTROY_POOL(&g_TrackingFreeList);
	}
	if (g_StaticFreeList != NULL) {
		for (staticCur = g_StaticFreeList; staticCur != NULL; staticCur = staticNext) {
			VG_MAKE_DEFINED(staticCur, sizeof(CResourceEntity));
			staticNext = (CItem *)staticCur->resourceEntity.nextInContainer;
		}
		VG_DESTROY_POOL(&g_StaticFreeList);
	}
}

/*
 * Custom (FEAT_HINT_RUMORS) - Hint_WakeCarriedMagic
 *
 * The binary's CItem_DecayTick skips the decay script event for worn and
 * contained items, so magic items carried by NPCs never fire hintupdater.m's
 * "trigger decay" and the rumor database (getHint) stays empty. For an item
 * that carries the hintupdater script (every factory magic item, via the
 * classifier globalscripts), fire the decay event so the hint is recorded.
 * The carried item is not otherwise decayed: the caller still zeroes
 * decayCount and returns. Returns 0 if the script deleted the entity (the
 * caller must not touch it afterwards), 1 otherwise (including non-magic
 * items, which are left untouched).
 */
static int
Hint_WakeCarriedMagic(CItem *item)
{
	uint32_t serial;

	if (!CItem_HasScriptByName(item, "hintupdater"))
		return 1;

	serial = CMobile_GetSerial((CMobile *)item);
	Entity_ExecuteEvent(&item->resourceEntity.entity, 0x31, (uint32_t)item->decayCount, (uint32_t)item->decayCount);
	return CWorld_FindBySerial(g_World, serial) == (CItem *)item;
}
