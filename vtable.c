/*
 * Entity virtual-dispatch tables.
 *
 * Defines one static CEntity_vtable per entity class (CItem, CContainer,
 * CMobile, CNPC, CPlayer, CMulti, CEgg, ...) and populates each with the
 * per-class method pointers the entity hierarchy dispatches through.
 */

#include <string.h>

#include "bboard.h"
#include "combat.h"
#include "container.h"
#include "dynamic.h"
#include "egg.h"
#include "multi.h"
#include "npc.h"
#include "player.h"
#include "shopkeeper.h"
#include "signpost.h"
#include "vtable.h"
#include "weapon.h"

static void vt_fill_stubs(CEntity_vtable *vt);
static void vt_copy(CEntity_vtable *dst, CEntity_vtable *src);

// StaticEntity vtable functions (item.c)

/*
 * Binary vtable stub functions.
 */

// Static vtable instances for each entity class.
CEntity_vtable g_vtable_CEntity;
CEntity_vtable g_vtable_CResourceEntity;
CEntity_vtable g_vtable_StaticEntity;
CEntity_vtable g_vtable_CItem;
CEntity_vtable g_vtable_CContainer;
CEntity_vtable g_vtable_CWeapon;
CEntity_vtable g_vtable_CMobile;
CEntity_vtable g_vtable_CResourceMobile;
CEntity_vtable g_vtable_CNPC;
CEntity_vtable g_vtable_CPlayer;
CEntity_vtable g_vtable_CShopkeeper;
CEntity_vtable g_vtable_CMulti;
CEntity_vtable g_vtable_CEgg;
CEntity_vtable g_vtable_CSignpost;
CEntity_vtable g_vtable_CGuard;
CEntity_vtable g_vtable_CBoard;

/*
 * VT_ForType - map ETYPE_* enum value to vtable pointer.
 *
 * Used by CEntity_SetType() macro to assign the correct vtable
 * during entity construction.
 */
CEntity_vtable *
VT_ForType(int etype)
{
	switch (etype) {
	case ETYPE_ITEM:
		return &g_vtable_CItem;
	case ETYPE_CONTAINER:
		return &g_vtable_CContainer;
	case ETYPE_WEAPON:
		return &g_vtable_CWeapon;
	case ETYPE_MOBILE:
		return &g_vtable_CMobile;
	case ETYPE_NPC:
		return &g_vtable_CResourceMobile;
	case ETYPE_PLAYER:
		return &g_vtable_CPlayer;
	case ETYPE_SHOPKEEPER:
		return &g_vtable_CShopkeeper;
	case ETYPE_GUARD:
		return &g_vtable_CGuard;
	case ETYPE_MULTI:
		return &g_vtable_CMulti;
	case ETYPE_EGG:
		return &g_vtable_CEgg;
	case ETYPE_SIGNPOST:
		return &g_vtable_CSignpost;
	case ETYPE_BOAT:
		return &g_vtable_CBoard;
	default:
		return &g_vtable_CEntity;
	}
}

/*
 * VT_GetType - map vtable pointer back to ETYPE_* enum value.
 *
 * Used by CEntity_GetType() macro to determine entity type
 * from its vtable pointer.
 */
int
VT_GetType(CEntity_vtable *vt)
{
	if (vt == &g_vtable_CItem)
		return ETYPE_ITEM;
	if (vt == &g_vtable_CContainer)
		return ETYPE_CONTAINER;
	if (vt == &g_vtable_CWeapon)
		return ETYPE_WEAPON;
	if (vt == &g_vtable_CMobile)
		return ETYPE_MOBILE;
	if (vt == &g_vtable_CResourceMobile)
		return ETYPE_NPC;
	if (vt == &g_vtable_CPlayer)
		return ETYPE_PLAYER;
	if (vt == &g_vtable_CShopkeeper)
		return ETYPE_SHOPKEEPER;
	if (vt == &g_vtable_CGuard)
		return ETYPE_GUARD;
	if (vt == &g_vtable_CMulti)
		return ETYPE_MULTI;
	if (vt == &g_vtable_CEgg)
		return ETYPE_EGG;
	if (vt == &g_vtable_CSignpost)
		return ETYPE_SIGNPOST;
	if (vt == &g_vtable_CBoard)
		return ETYPE_BOAT;
	return 0;
}

// Helper: fill all slots in a vtable with vt_stub_return_0.
static void
vt_fill_stubs(CEntity_vtable *vt)
{
	int i;

	for (i = 0; i < VTABLE_NUM_SLOTS; i++)
		vt->fn[i] = (vfunc_t)vt_stub_return_0;
}

// Helper: copy all slots from src to dst, then apply overrides.
static void
vt_copy(CEntity_vtable *dst, CEntity_vtable *src)
{
	memcpy(dst->fn, src->fn, sizeof(dst->fn));
}

/*
 * VT_Init - initialize all vtable instances.
 *
 * Populates vtable slots matching the binary's .rdata vtable layout.
 * Each class starts from its parent's vtable and overrides specific slots.
 * Call once during server startup before any entities are created.
 *
 * Binary vtable addresses:
 *   CEntity:         0x005EF448    CItem:   0x005EF620
 *   CContainer:      0x005EE888    CMobile: 0x005EEF48
 *   CNPC:            0x005EF1F8    CPlayer: 0x005EEA50
 *
 * Slot assignments verified against UoDemo.exe vtable dumps:
 *   VT_IS_PLAYER (0x18):  CPlayer=1, others=0
 *   VT_IS_CONTAINER(0x1C): CItem+=1 (all CItem-derived classes)
 *   VT_IS_MOBILE (0xD0):  CMobile+=1
 *   VT_IS_NPC (0xE4):     CNPC+=1
 *   VT_IS_VENDOR (0xE8):  binary=0 for all (demo), our CShopkeeper=1
 *   VT_IS_DEAD (0xF4):    class-specific
 */
void
VT_Init(void)
{
	// CEntity: base class, all stubs
	vt_fill_stubs(&g_vtable_CEntity);
	// SayCString: slot 21 (0x54) = 0x0048D192
	g_vtable_CEntity.fn[VT_SAY_CSTRING / 4] = (vfunc_t)CEntity_SayCString_VT;
	// ItemCheck9C: slot 39 (0x9C) = 0x004911C0
	g_vtable_CEntity.fn[VT_ITEM_CHECK_9C / 4] = (vfunc_t)CEntity_ItemCheck9C_VT;
	// CheckSurface: slot 17 (0x44) = 0x004866C7 (reuses CheckDC)
	g_vtable_CEntity.fn[VT_CHECK_SURFACE / 4] = (vfunc_t)CEntity_CheckDC_VT;
	// CheckSurfaceOf: slot 18 (0x48) = 0x004866E2
	g_vtable_CEntity.fn[VT_CHECK_SURFACE_OF / 4] = (vfunc_t)CEntity_CheckSurfaceOf_VT;
	// CheckDC: slot 55 (0xDC) = 0x004866C7
	g_vtable_CEntity.fn[VT_CHECK_DC / 4] = (vfunc_t)CEntity_CheckDC_VT;
	// SpeakSysMsg: slot 19 (0x4C) = 0x004911F0 (thunk to GetName)
	g_vtable_CEntity.fn[VT_SPEAK_SYS_MSG / 4] = (vfunc_t)CEntity_SpeakSysMsg_VT;
	// GetName: slot 13 (0x34) = 0x00432260 (returns tiledata name)
	g_vtable_CEntity.fn[VT_GET_NAME / 4] = (vfunc_t)CResourceEntity_CheckDC_VT;

	// CResourceEntity: inherits CEntity
	vt_copy(&g_vtable_CResourceEntity, &g_vtable_CEntity);
	// Dtor: slot 0 (0x00) = 0x00491340 CResourceEntity_ScalarDelete
	g_vtable_CResourceEntity.fn[VT_DTOR / 4] = (vfunc_t)CResourceEntity_ScalarDelete;
	// Delete: slot 36 (0x90) = 0x004850D3 CResourceEntity_Delete
	g_vtable_CResourceEntity.fn[VT_DELETE / 4] = (vfunc_t)CResourceEntity_Delete;
	// GetResourceAmount: slot 40 (0xA0) = 0x0045E5D4. Sums value1 (the
	// template's required quantity) rather than CItem's value3. Used when
	// CItem_HasPositiveResources dispatches via the template slot's vtable.
	g_vtable_CResourceEntity.fn[VT_GET_RESOURCE_AMT / 4] = (vfunc_t)CResourceEntity_GetResourceAmount;
	// CheckDC: slot 55 (0xDC) = 0x00432260 (overrides CEntity)
	g_vtable_CResourceEntity.fn[VT_CHECK_DC / 4] = (vfunc_t)CResourceEntity_CheckDC_VT;
	// CheckEC: slot 59 (0xEC) = 0x004866C7 (reuses CEntity CheckDC)
	g_vtable_CResourceEntity.fn[VT_CHECK_EC / 4] = (vfunc_t)CEntity_CheckDC_VT;

	// StaticEntity (binary vtable 0x005EF588): starts from CItem base,
	// overrides lifecycle slots with static-chain-specific functions.
	// Built after CItem so we can copy CItem's terrain-related slots.
	// Note: actual initialization is deferred to after CItem setup below.

	// CItem: inherits CResourceEntity
	vt_copy(&g_vtable_CItem, &g_vtable_CResourceEntity);
	// CItem overrides inherited slots back to stubs
	g_vtable_CItem.fn[VT_ITEM_CHECK_9C / 4] = (vfunc_t)vt_stub_return_0;
	g_vtable_CItem.fn[VT_CHECK_DC / 4] = (vfunc_t)vt_stub_return_0;
	g_vtable_CItem.fn[VT_CHECK_EC / 4] = (vfunc_t)vt_stub_return_0;
	// ScalarDeletingDestructor: slot 0 (0x00)
	g_vtable_CItem.fn[VT_DTOR / 4] = (vfunc_t)CItem_ScalarDelete;
	// DropAtFeet: slot 1 (0x04)
	g_vtable_CItem.fn[VT_DROP_AT_FEET / 4] = (vfunc_t)CItem_DropAtFeet;
	// SetLocation: slot 2 (0x08) = 0x00489432
	g_vtable_CItem.fn[VT_SET_LOCATION / 4] = (vfunc_t)CItem_SetLocation_VT;
	// Hide/Remove: slot 3 (0x0C)
	g_vtable_CItem.fn[VT_HIDE / 4] = (vfunc_t)CItem_HideVT;
	// DetachFromSpatial: slot 4 (0x10) = 0x0048840D (extended version)
	// Binary: CItem uses CResourceEntity_DetachFromSpatial which includes
	// unequip events and IsValueless checks, not the basic CItem_DetachFromSpatial.
	g_vtable_CItem.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)CResourceEntity_DetachFromSpatial;
	// IsContainer: slot 7 (0x1C) = 0x004E04C0 (return 1 for CItem+)
	// Binary: all CItem-derived classes return 1 here. Used by
	// CItem_ClassifyEntityByVtable to distinguish CItem+ from CEntity/CResourceEntity.
	g_vtable_CItem.fn[VT_IS_CONTAINER / 4] = (vfunc_t)vt_stub_return_1;
	// HasResourceFlag: slot 8 (0x20) = 0x0045E57D
	g_vtable_CItem.fn[VT_HAS_RESOURCE_FLAG / 4] = (vfunc_t)CItem_HasResourceFlag;
	// ReattachSpatial: slot 15 (0x3C)
	g_vtable_CItem.fn[VT_REATTACH_SPATIAL / 4] = (vfunc_t)CItem_ReattachSpatial_VT;
	// GetSurfaceFlags: slot 16 (0x40) = 0x004863D1
	g_vtable_CItem.fn[VT_GET_SURFACE_FLAGS / 4] = (vfunc_t)CItem_GetSurfaceFlags_VT;
	// SpeakSysMsg: slot 19 (0x4C) = 0x0048EAEB (lookAtText check)
	g_vtable_CItem.fn[VT_SPEAK_SYS_MSG / 4] = (vfunc_t)CItem_SpeakSysMsg_VT;
	// GetNameString: slot 75 (0x12C) = 0x0048735F
	g_vtable_CItem.fn[VT_GET_NAME_STRING / 4] = (vfunc_t)CItem_GetNameString_VT;
	// GetHeight: slot 10 (0x28) = 0x004910A7
	g_vtable_CItem.fn[VT_GET_HEIGHT / 4] = (vfunc_t)CItem_GetHeight_VT;
	// GetSurfaceHeight: slot 11 (0x2C) = bridge halving
	g_vtable_CItem.fn[VT_GET_SURFACE_H / 4] = (vfunc_t)CItem_GetSurfaceHeight_VT;
	// GetFlags: slot 12 (0x30) = 0x004322C0
	g_vtable_CItem.fn[VT_GET_FLAGS / 4] = (vfunc_t)CItem_GetFlags_VT;
	// IsHair: slot 14 (0x38) = 0x004904E5
	g_vtable_CItem.fn[VT_IS_HAIR / 4] = (vfunc_t)CItem_IsHair_VT;
	// GetLocation: slot 32 (0x80) = 0x0048A531
	g_vtable_CItem.fn[VT_GET_LOCATION / 4] = (vfunc_t)CItem_GetLocationVT;
	// IsDoor: slot 33 (0x84) = 0x00490400
	g_vtable_CItem.fn[VT_IS_DOOR / 4] = (vfunc_t)CItem_IsDoor_VT;
	// IsDoorNotStackable: slot 34 (0x88) = 0x0049045A
	g_vtable_CItem.fn[VT_IS_DOOR_NS / 4] = (vfunc_t)CItem_IsDoorNotStackable_VT;
	// GetRemovedFromWorld: slot 35 (0x8C) = 0x00432290
	g_vtable_CItem.fn[VT_IS_REMOVED / 4] = (vfunc_t)CEntity_GetRemovedFromWorld;
	// Delete: slot 36 (0x90) = 0x00485059 (Check1 only, no Check2)
	// Binary: identical logic to CSignpost_Delete (0x00484DF9).
	g_vtable_CItem.fn[VT_DELETE / 4] = (vfunc_t)CSignpost_Delete;
	// GetMovementType: slot 37 (0x94)
	g_vtable_CItem.fn[VT_GET_MOVEMENT_TYPE / 4] = (vfunc_t)CItem_GetMovementType_VT;
	// GetItemAmount: slot 38 (0x98) = 0x004854F2
	g_vtable_CItem.fn[VT_GET_ITEM_AMOUNT / 4] = (vfunc_t)CItem_GetItemAmount;
	// GetResourceAmount: slot 40 (0xA0) = 0x0045E636
	g_vtable_CItem.fn[VT_GET_RESOURCE_AMT / 4] = (vfunc_t)CItem_GetResourceAmount;
	// SaveText: slot 41 (0xA4) = 0x00487211
	g_vtable_CItem.fn[VT_SAVE_TEXT / 4] = (vfunc_t)CItem_SaveText_VT;
	// MoveTo: slot 42 (0xA8) = 0x00490F78
	g_vtable_CItem.fn[VT_MOVE_TO / 4] = (vfunc_t)CItem_ValidateInWorldVT;
	// ReturnToTracked: slot 43 (0xAC) = 0x0048991A
	g_vtable_CItem.fn[VT_RETURN_TO_TRACKED / 4] = (vfunc_t)CItem_ReturnToTracked;
	// TransferTo: slot 44 (0xB0) = 0x004906A6
	g_vtable_CItem.fn[VT_TRANSFER_TO / 4] = (vfunc_t)CItem_TransferTo;
	// HasContainer: slot 65 (0x104) = 0x00489EC7
	g_vtable_CItem.fn[VT_HAS_CONTAINER / 4] = (vfunc_t)CItem_HasContainerVT;
	// IsEquipped: slot 66 (0x108) = 0x00489EE0
	g_vtable_CItem.fn[VT_IS_EQUIPPED_ITEM / 4] = (vfunc_t)CItem_IsEquipped_VT;
	// GetLayer: slot 67 (0x10C) = 0x00489F46
	g_vtable_CItem.fn[VT_GET_LAYER / 4] = (vfunc_t)CItem_GetLayer_VT;
	// GetWeight/GetDecayMax: slot 72 (0x120) = 0x00489FAB
	g_vtable_CItem.fn[VT_GET_WEIGHT / 4] = (vfunc_t)CItem_GetDecayMax;
	// GetStoredWeight: slot 73 (0x124) = 0x00491280
	g_vtable_CItem.fn[VT_GET_STORED_WEIGHT / 4] = (vfunc_t)CItem_GetStoredWeightVT;
	// CanStackWith: slot 74 (0x128) = 0x0048A561
	g_vtable_CItem.fn[VT_MERGE_INTO / 4] = (vfunc_t)CItem_CanStackWith;
	// SendUpdateToList: slot 76 (0x130) = 0x00489D9E
	g_vtable_CItem.fn[VT_NOTIFY_NEARBY / 4] = (vfunc_t)CItem_SendUpdateToList;
	// GetDirection: slot 78 (0x138) = 0x0048A11F
	g_vtable_CItem.fn[VT_GET_DIRECTION / 4] = (vfunc_t)CItem_GetDirectionVT;
	// SetSerial: slot 80 (0x140) = 0x00487612
	g_vtable_CItem.fn[VT_SET_SERIAL / 4] = (vfunc_t)CItem_SetSerial;
	// DecayTick: slot 81 (0x144) = 0x0045932E
	g_vtable_CItem.fn[VT_DECAY_TICK / 4] = (vfunc_t)CItem_DecayTick;
	// GetAmount: slot 90 (0x168) = 0x00490DCA
	g_vtable_CItem.fn[VT_GET_AMOUNT / 4] = (vfunc_t)CItem_VT_GetAmount;
	// IsHidden: slot 93 (0x174)
	g_vtable_CItem.fn[VT_IS_HIDDEN / 4] = (vfunc_t)CItem_IsHidden_VT;
	// SetHidden: slot 94 (0x178)
	g_vtable_CItem.fn[VT_SET_HIDDEN / 4] = (vfunc_t)CItem_SetHidden_VT;
	// PreDeleteCleanup: slot 107 (0x1AC) - binary: 0x004906F3 (DropAtFeet).
	// Called from DeleteCheck2 with no stack args, but DropAtFeet expects
	// one arg → receives garbage and ret 4 is absorbed by caller's frame
	// restore. Effectively a no-op for CItem. Real cleanup (HideVT, hash
	// unlink, template detach) happens in the destructor chain.
	// No-op here: CContainer/CMobile overrides handle their children.
	g_vtable_CItem.fn[VT_DELETE_CONTENTS / 4] = (vfunc_t)CItem_DeleteContents_Nop;
	// AddToContainer: slot 45 (0xB4)
	g_vtable_CItem.fn[VT_ADD_TO_CONTAINER / 4] = (vfunc_t)CItem_AddToContainerVT;
	// EquipOnMobile: slot 48 (0xC0) = 0x00488D81
	g_vtable_CItem.fn[VT_EQUIP_ON_MOBILE / 4] = (vfunc_t)CItem_EquipOnMobile;
	// AddToEquip: slot 49 (0xC4) = 0x0048897A
	g_vtable_CItem.fn[VT_ADD_TO_EQUIP / 4] = (vfunc_t)CItem_AddToEquip_VT;
	// HasAccessibleContents: slot 54 (0xD8) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_HAS_ACCESSIBLE_CONTENTS / 4] = (vfunc_t)vt_stub_return_0;
	// Save: slot 50 (0xC8)
	g_vtable_CItem.fn[VT_SAVE / 4] = (vfunc_t)CItem_Save;
	// GetValue: slot 9 (0x24) = 0x00488ABC
	g_vtable_CItem.fn[VT_GET_VALUE / 4] = (vfunc_t)CItem_GetValue_VT;
	// SayCUString: slot 20 (0x50) = 0x0048D2A5
	g_vtable_CItem.fn[VT_SAY_CUSTRING / 4] = (vfunc_t)CItem_SayCUString_VT;
	// EmoteCUString: slot 22 (0x58) = 0x0048D4CF
	g_vtable_CItem.fn[VT_EMOTE_CUSTRING / 4] = (vfunc_t)CItem_EmoteCUString_VT;
	// EmoteCString: slot 23 (0x5C) = 0x0048D3BC
	g_vtable_CItem.fn[VT_EMOTE_CSTRING / 4] = (vfunc_t)CItem_EmoteCString_VT;
	// SayHuedCUString: slot 24 (0x60) = 0x0048D6E0
	g_vtable_CItem.fn[VT_SAY_HUED_CUSTRING / 4] = (vfunc_t)CItem_SayHuedCUString_VT;
	// SayHuedCString: slot 25 (0x64) = 0x0048D68A
	g_vtable_CItem.fn[VT_SAY_HUED_CSTRING / 4] = (vfunc_t)CItem_SayHuedCString_VT;
	// SayToCUString: slot 26 (0x68) = 0x0048D60F
	g_vtable_CItem.fn[VT_SAY_TO_CUSTRING / 4] = (vfunc_t)CItem_SayToCUString_VT;
	// SayToEntity: slot 27 (0x6C) = 0x0048D5E6
	g_vtable_CItem.fn[VT_SAY_TO_ENTITY / 4] = (vfunc_t)CItem_SayToEntity_VT;
	// EmoteHuedCUString: slot 28 (0x70) = 0x0048D78E
	g_vtable_CItem.fn[VT_EMOTE_HUED_CUSTRING / 4] = (vfunc_t)CItem_EmoteHuedCUString_VT;
	// EmoteHuedCString: slot 29 (0x74) = 0x0048D738
	g_vtable_CItem.fn[VT_EMOTE_HUED_CSTRING / 4] = (vfunc_t)CItem_EmoteHuedCString_VT;
	// EmoteToCUString: slot 30 (0x78) = 0x0048D661
	g_vtable_CItem.fn[VT_EMOTE_TO_CUSTRING / 4] = (vfunc_t)CItem_EmoteToCUString_VT;
	// EmoteToEntity: slot 31 (0x7C) = 0x0048D638
	g_vtable_CItem.fn[VT_EMOTE_TO_ENTITY / 4] = (vfunc_t)CItem_EmoteToEntity_VT;
	// GetMonetaryVal: slot 51 (0xCC) = 0x00488A6F
	g_vtable_CItem.fn[VT_GET_MONETARY_VAL / 4] = (vfunc_t)CItem_GetMonetaryVal_VT;
	// IsMoveable: slot 68 (0x110) = 0x0048A1B3
	g_vtable_CItem.fn[VT_IS_MOVEABLE / 4] = (vfunc_t)CItem_IsMoveable_VT;
	// IsFreelyUsable: slot 69 (0x114) = 0x0048A29E
	g_vtable_CItem.fn[VT_IS_FREELY_USABLE / 4] = (vfunc_t)CItem_IsFreelyUsable_VT;
	// IsFreelyViewable: slot 70 (0x118) = 0x0048A412
	g_vtable_CItem.fn[VT_IS_FREELY_VIEWABLE / 4] = (vfunc_t)CItem_IsFreelyViewable_VT;
	// IsInWorld: slot 71 (0x11C) = 0x0049064B
	g_vtable_CItem.fn[VT_IS_IN_WORLD / 4] = (vfunc_t)CItem_IsInWorld_VT;
	// SendEntityUpdate: slot 77 (0x134) = 0x00489C96
	g_vtable_CItem.fn[VT_SEND_ENTITY_UPDATE / 4] = (vfunc_t)CItem_SendEntityUpdate_VT;
	// GetContainerDim: slot 79 (0x13C) = 0x0048A170
	g_vtable_CItem.fn[VT_GET_CONTAINER_DIM / 4] = (vfunc_t)CItem_GetContainerDim_VT;
	// WeightRelated: slot 85 (0x154) = 0x004E110F (no-op for CItem)
	g_vtable_CItem.fn[VT_WEIGHT_RELATED / 4] = (vfunc_t)CItem_WeightRelated_VT;
	// ValidateRegistration: slot 86 (0x158) = 0x00490FFA
	g_vtable_CItem.fn[VT_VALIDATE_REG / 4] = (vfunc_t)CItem_ValidateRegistration_VT;
	// FindSelfInWorld: slot 87 (0x15C) = 0x00491030
	g_vtable_CItem.fn[VT_FIND_IN_WORLD / 4] = (vfunc_t)CItem_FindSelfInWorld_VT;
	// ConditionalValidate: slot 88 (0x160) = 0x00491056
	g_vtable_CItem.fn[VT_COND_VALIDATE / 4] = (vfunc_t)CItem_ConditionalValidate_VT;
	// FindInTagList: slot 89 (0x164) = 0x0048F26A (CResourceEntity_IsInTagListStr)
	g_vtable_CItem.fn[VT_FIND_IN_TAG_LIST / 4] = (vfunc_t)CResourceEntity_IsInTagListStr;
	// ApplyStatusFlags: slot 91 (0x16C) = 0x00490E73
	g_vtable_CItem.fn[VT_APPLY_STATUS_FLAGS / 4] = (vfunc_t)CItem_ApplyStatusFlags;
	// GetStatusFlags: slot 92 (0x170) = 0x00490E09
	g_vtable_CItem.fn[VT_GET_STATUS_FLAGS / 4] = (vfunc_t)CItem_GetStatusFlags_VT;
	// GetNotoriety: slot 96 (0x180) = 0x0048F4E1 (plain CMobile_ComputeNotoriety)
	g_vtable_CItem.fn[VT_GET_NOTORIETY / 4] = (vfunc_t)CMobile_ComputeNotoriety;
	// CanBeFreelyAggressed: slot 98 (0x188) = 0x0048F75D (base impl)
	g_vtable_CItem.fn[VT_CAN_BE_FREELY_AGGRESSED / 4] = (vfunc_t)CMobile_CanBeFreelyAggressed_Impl;
	// CanBeAggressed: slot 99 (0x18C) = 0x0048F6D6
	g_vtable_CItem.fn[VT_CAN_BE_AGGRESSED / 4] = (vfunc_t)CMobile_CanBeFreelyAggressedBy;
	// FillAggroInfo: slot 103 (0x19C) = 0x0048F429
	g_vtable_CItem.fn[VT_FILL_AGGRO_INFO / 4] = (vfunc_t)CItem_FillAggroInfo_VT;
	// AddToContB8: slot 46 (0xB8) = 0x0044A0A4
	g_vtable_CItem.fn[VT_ADD_TO_CONT_B8 / 4] = (vfunc_t)CItem_AddToContB8_VT;
	// AddToContBC: slot 47 (0xBC) = 0x0044A2E5
	g_vtable_CItem.fn[VT_ADD_TO_CONT_BC / 4] = (vfunc_t)CItem_AddToContBC_VT;
	// DecayPlace: slot 82 (0x148) = 0x0045E140 (no-op, 13 bytes)
	g_vtable_CItem.fn[VT_DECAY_PLACE / 4] = (vfunc_t)vt_stub_return_0;
	// DecayCleanup: slot 84 (0x150) = 0x00461F80 (no-op, 11 bytes)
	g_vtable_CItem.fn[VT_DECAY_CLEANUP / 4] = (vfunc_t)vt_stub_return_0;
	// CItem_SetMurderCount: slot 95 (0x17C) = 0x0048FF6F
	g_vtable_CItem.fn[VT_SET_MURDER_COUNT / 4] = (vfunc_t)CItem_SetMurderCount;
	// RefreshAggression: slot 97 (0x184) = 0x0048FA3A
	g_vtable_CItem.fn[VT_REFRESH_AGGRESSION / 4] = (vfunc_t)CEntity_RefreshAggression;
	// AddToAggressorList: slot 100 (0x190) = 0x0048FAC5
	g_vtable_CItem.fn[VT_ADD_TO_AGGRESSOR_LIST / 4] = (vfunc_t)CPlayer_AddToAggressorList_VT;
	// AddToAttackerList: slot 101 (0x194) = 0x0048F9BA
	g_vtable_CItem.fn[VT_ADD_TO_ATTACKER_LIST / 4] = (vfunc_t)CItem_AddToAttackerList_VT;
	// NotifyDamage: slot 102 (0x198) = 0x0048FC7B
	g_vtable_CItem.fn[VT_NOTIFY_DAMAGE / 4] = (vfunc_t)CMobile_NotifyDamage;
	// FameKarmaChange: slot 104 (0x1A0) = 0x00490281
	g_vtable_CItem.fn[VT_FAME_KARMA_CHANGE / 4] = (vfunc_t)CItem_FameKarmaChange_VT;
	// IsMobile: slot 52 (0xD0) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_IS_MOBILE / 4] = (vfunc_t)vt_stub_return_0;
	// IsMobile2: slot 53 (0xD4) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_IS_MOBILE2 / 4] = (vfunc_t)vt_stub_return_0;
	// IsSpatial: slot 56 (0xE0) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_IS_SPATIAL / 4] = (vfunc_t)vt_stub_return_0;
	// IsNPC: slot 57 (0xE4) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_IS_NPC / 4] = (vfunc_t)vt_stub_return_0;
	// IsVendor: slot 58 (0xE8) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_IS_VENDOR / 4] = (vfunc_t)vt_stub_return_0;
	// IsDead: slot 61 (0xF4) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_IS_DEAD / 4] = (vfunc_t)vt_stub_return_0;
	// IsWeapon: slot 62 (0xF8) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_IS_WEAPON / 4] = (vfunc_t)vt_stub_return_0;
	// HasCorpseEquipment: slot 63 (0xFC) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_HAS_CORPSE_EQ / 4] = (vfunc_t)vt_stub_return_0;
	// SayCString: slot 21 (0x54) = 0x0048D192 (inherited from CEntity)
	// DecayReturnHome: slot 83 (0x14C) = 0x0044A7D0 (return 0, no-op)
	g_vtable_CItem.fn[VT_DECAY_RETURN_HOME / 4] = (vfunc_t)vt_stub_return_0;
	// GetMaxWeight: slot 105 (0x1A4) = 0x0044A7D0 (return 0)
	g_vtable_CItem.fn[VT_GET_MAX_WEIGHT / 4] = (vfunc_t)vt_stub_return_0;

	// StaticEntity (binary vtable 0x005EF588): copy CItem base, then
	// override lifecycle/spatial slots with static-chain-specific functions.
	// Static entities are 0x18-byte terrain objects with doubly-linked
	// chain (next at 0x10, prev at 0x14) instead of CItem's spatial grid.
	vt_copy(&g_vtable_StaticEntity, &g_vtable_CItem);
	// Dtor: slot 0 (0x00) = 0x00491130
	g_vtable_StaticEntity.fn[VT_DTOR / 4] = (vfunc_t)StaticEntity_ScalarDelete;
	// DropAtFeet: slot 1 (0x04) = 0x00488EF5
	// Binary: SetLocation variant with SEH frame, flags=0.
	// Uses same chain-linking logic as SetLocation_VT.
	g_vtable_StaticEntity.fn[VT_DROP_AT_FEET / 4] = (vfunc_t)StaticEntity_SetLocation_VT;
	// SetLocation: slot 2 (0x08) = 0x004893B1
	g_vtable_StaticEntity.fn[VT_SET_LOCATION / 4] = (vfunc_t)StaticEntity_SetLocation_Link;
	// Hide/RemoveFromWorld: slot 3 (0x0C) = 0x00487AD9
	g_vtable_StaticEntity.fn[VT_HIDE / 4] = (vfunc_t)StaticEntity_Hide_VT;
	// DetachSpatial: slot 4 (0x10) = 0x00488382
	g_vtable_StaticEntity.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)StaticEntity_DetachSpatial_VT;
	// GetHeight: slot 10 (0x28) = 0x00491078 (no door offset)
	g_vtable_StaticEntity.fn[VT_GET_HEIGHT / 4] = (vfunc_t)StaticEntity_GetHeight_VT;
	// GetFlags: slot 12 (0x30) = 0x00491230 (no door offset)
	g_vtable_StaticEntity.fn[VT_GET_FLAGS / 4] = (vfunc_t)StaticEntity_GetFlags_VT;
	// GetLocation: slot 32 (0x80) = 0x00491210 (simple: this+0x0A)
	g_vtable_StaticEntity.fn[VT_GET_LOCATION / 4] = (vfunc_t)StaticEntity_GetLocation_VT;
	// IsContainer: slot 7 (0x1C) = 0x0044A7D0 (return 0 - statics not containers)
	g_vtable_StaticEntity.fn[VT_IS_CONTAINER / 4] = (vfunc_t)vt_stub_return_0;
	// SpeakSysMsg: slot 19 (0x4C) = 0x004911F0 (CEntity version)
	g_vtable_StaticEntity.fn[VT_SPEAK_SYS_MSG / 4] = (vfunc_t)CEntity_SpeakSysMsg_VT;
	// GetSurfaceFlags: slot 16 (0x40) = 0x004863D1 (same as CItem)
	// (already inherited from CItem copy)
	// Slot 5 (0x14) = 0x004E04C0 (return 1)
	g_vtable_StaticEntity.fn[VT_ATTACH_SPATIAL / 4] = (vfunc_t)vt_stub_return_1;
	// SayCString: slot 21 (0x54) = 0x0048D192 (CEntity base, override CItem NULL)
	g_vtable_StaticEntity.fn[VT_SAY_CSTRING / 4] = (vfunc_t)CEntity_SayCString_VT;
	// IsDoor: slot 33 (0x84) = 0x00490392 (direct tiledata check, no door offset)
	g_vtable_StaticEntity.fn[VT_IS_DOOR / 4] = (vfunc_t)StaticEntity_IsDoor_VT;
	// IsDoorNS: slot 34 (0x88) = 0x004903C9 (identical logic, separate binary fn)
	g_vtable_StaticEntity.fn[VT_IS_DOOR_NS / 4] = (vfunc_t)StaticEntity_IsDoor_VT;
	// Delete: slot 36 (0x90) = 0x0048509C (unconditional, no checks)
	// Binary: identical logic to CResourceEntity_Delete (0x004850D3).
	g_vtable_StaticEntity.fn[VT_DELETE / 4] = (vfunc_t)CResourceEntity_Delete;

	// CContainer: inherits CItem
	vt_copy(&g_vtable_CContainer, &g_vtable_CItem);
	// DetachFromSpatial: slot 4 (0x10) = 0x0048840D (extended version)
	g_vtable_CContainer.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)CResourceEntity_DetachFromSpatial;
	g_vtable_CContainer.fn[VT_IS_CONTAINER / 4] = (vfunc_t)vt_stub_return_1;
	// Binary: CContainer vtable[0xD4] = return_1 (0x004E04C0).
	// Used by CWorld_FindEntityInRange and CItem_PlaceInWorld to check
	// if an entity can contain items (CContainer, CMobile, CPlayer).
	g_vtable_CContainer.fn[VT_IS_MOBILE2 / 4] = (vfunc_t)vt_stub_return_1;
	// HasAccessibleContents: slot 54 (0xD8) = 0x0044887F
	g_vtable_CContainer.fn[VT_HAS_ACCESSIBLE_CONTENTS / 4] = (vfunc_t)CContainer_HasAccessibleContents_VT;
	// GetValue: slot 9 (0x24) = 0x00448C2E (CContainer override)
	g_vtable_CContainer.fn[VT_GET_VALUE / 4] = (vfunc_t)CContainer_GetValue_VT;
	// GetStoredWeight: slot 73 (0x124) = 0x0044914E
	g_vtable_CContainer.fn[VT_GET_STORED_WEIGHT / 4] = (vfunc_t)CContainer_GetStoredWeight;
	g_vtable_CContainer.fn[VT_SAVE / 4] = (vfunc_t)CContainer_Save;
	// ExcludedAmount: slot 64 (0x100) = 0x0044883C
	g_vtable_CContainer.fn[VT_EXCLUDED_AMOUNT / 4] = (vfunc_t)CContainer_ExcludedAmount_VT;
	// ValidateRegistration: slot 86 (0x158) = 0x0044A729 (CContainer override)
	g_vtable_CContainer.fn[VT_VALIDATE_REG / 4] = (vfunc_t)CContainer_ValidateRegistration_VT;
	// FindInTagList: slot 89 (0x164) = 0x0048F2E7 (CContainer override)
	g_vtable_CContainer.fn[VT_FIND_IN_TAG_LIST / 4] = (vfunc_t)CContainer_FindInTagList_VT;
	// CanHold: slot 109 (0x1B4) = 0x0044A5CD
	g_vtable_CContainer.fn[VT_EQUIP_ITERATE / 4] = (vfunc_t)CContainer_CanHold;
	// PreDeleteCleanup: slot 107 (0x1AC) = 0x0044899A
	g_vtable_CContainer.fn[VT_DELETE_CONTENTS / 4] = (vfunc_t)CContainer_PreDeleteCleanup;
	// Dtor: slot 0 (0x00) = 0x0044A770 CContainer_ScalarDelete
	g_vtable_CContainer.fn[VT_DTOR / 4] = (vfunc_t)CContainer_ScalarDelete;
	// SayCString: slot 21 (0x54) = 0x0048D192 (CEntity base version)
	g_vtable_CContainer.fn[VT_SAY_CSTRING / 4] = (vfunc_t)CEntity_SayCString_VT;
	// Delete: slot 36 (0x90) = 0x00484E3C
	g_vtable_CContainer.fn[VT_DELETE / 4] = (vfunc_t)CContainer_Delete;
	// DecayPlace: slot 82 (0x148) = 0x00448CBD
	g_vtable_CContainer.fn[VT_DECAY_PLACE / 4] = (vfunc_t)CContainer_DecayPlace;
	// DecayReturnHome: slot 83 (0x14C) = 0x00448D7C
	g_vtable_CContainer.fn[VT_DECAY_RETURN_HOME / 4] = (vfunc_t)CContainer_DecayReturnHome;
	// DecayCleanup: slot 84 (0x150) = 0x00448E55
	g_vtable_CContainer.fn[VT_DECAY_CLEANUP / 4] = (vfunc_t)CContainer_DecayCleanup;
	// WeightRelated: slot 85 (0x154) = 0x004E0FD5
	g_vtable_CContainer.fn[VT_WEIGHT_RELATED / 4] = (vfunc_t)CContainer_RefreshContents;
	// GetMaxWeight: slot 105 (0x1A4) = 0x00449129
	g_vtable_CContainer.fn[VT_GET_MAX_WEIGHT / 4] = (vfunc_t)CContainer_GetMaxWeight;
	// CanBeFreelyAggressed: slot 98 (0x188) = 0x0048F75D
	g_vtable_CContainer.fn[VT_CAN_BE_FREELY_AGGRESSED / 4] = (vfunc_t)CMobile_CanBeFreelyAggressed_Impl;
	// EquipmentDecayTick: slot 106 (0x1A8) = 0x00453792
	g_vtable_CContainer.fn[VT_EQUIP_DECAY_TICK / 4] = (vfunc_t)CMobile_DecayContainerContents;
	// CancelTrade: slot 108 (0x1B0) = 0x00448926
	g_vtable_CContainer.fn[VT_CANCEL_TRADE / 4] = (vfunc_t)CItem_CancelTrade;

	// CWeapon: inherits CItem (not CContainer)
	// Binary 0x005F00F8: 105 slots (same as CItem), 6 overrides
	vt_copy(&g_vtable_CWeapon, &g_vtable_CItem);
	// Dtor: slot 0 (0x00) = NULL in binary CWeapon vtable (demo never deletes
	// weapons directly). Keep CItem_ScalarDelete from inheritance to prevent
	// null-pointer crash when our server deletes weapons.
	// IsWeapon: slot 62 (0xF8) = 0x004E04C0 (return 1)
	g_vtable_CWeapon.fn[VT_IS_WEAPON / 4] = (vfunc_t)vt_stub_return_1;
	// Save: slot 50 (0xC8) = 0x004C6CC9 CWeapon_Save
	g_vtable_CWeapon.fn[VT_SAVE / 4] = (vfunc_t)CWeapon_Save;
	// GetValue: slot 9 (0x24) = 0x004DFB3F CWeapon_GetValue
	g_vtable_CWeapon.fn[VT_GET_VALUE / 4] = (vfunc_t)CWeapon_GetValue;
	// SayCString: slot 21 (0x54) = 0x0048D192 (CEntity base version)
	g_vtable_CWeapon.fn[VT_SAY_CSTRING / 4] = (vfunc_t)CEntity_SayCString_VT;
	// Delete: slot 36 (0x90) = 0x00485016 CWeapon_Delete
	g_vtable_CWeapon.fn[VT_DELETE / 4] = (vfunc_t)CWeapon_Delete;
	// EquipOnMobile: slot 48 (0xC0) = 0x004DF58F CWeapon_EquipOnMobile
	g_vtable_CWeapon.fn[VT_EQUIP_ON_MOBILE / 4] = (vfunc_t)CWeapon_EquipOnMobile;

	// CMobile: inherits CContainer
	// Binary 0x005EEF48: 150 slots verified from vtable dump
	vt_copy(&g_vtable_CMobile, &g_vtable_CContainer);
	// ScalarDeletingDestructor: slot 0 (0x00)
	g_vtable_CMobile.fn[VT_DTOR / 4] = (vfunc_t)CMobile_ScalarDelete;
	// SetLocation: slot 2 (0x08) = 0x0049070C (delegates to CItem_SetLocation_VT)
	g_vtable_CMobile.fn[VT_SET_LOCATION / 4] = (vfunc_t)CMobile_SetLocation_VT;
	// Hide: slot 3 (0x0C) = 0x00487E94 (CItem_HideVT, same as CItem/CContainer)
	// Binary: CMobile inherits CItem_HideVT. Only CNPC overrides with
	// CNPC_RemoveFromWorldGrid (via CNPC_Hide_VT thunk at 0x00461B3D).
	g_vtable_CMobile.fn[VT_HIDE / 4] = (vfunc_t)CItem_HideVT;
	// DetachSpatial: slot 4 (0x10) = 0x004885CB (CItem_DetachFromSpatial)
	// Binary: CMobile uses the basic CItem_DetachFromSpatial. Only CNPC
	// uses CMobile_DetachSpatial_VT (0x004838FF, with NPC count/hash)
	// via CNPC_DetachSpatial_VT thunk at 0x00461B5C.
	g_vtable_CMobile.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)CItem_DetachFromSpatial;
	g_vtable_CMobile.fn[VT_IS_MOBILE / 4] = (vfunc_t)vt_stub_return_1;
	g_vtable_CMobile.fn[VT_IS_MOBILE2 / 4] = (vfunc_t)vt_stub_return_1;
	// GetFlags: slot 12 (0x30) = 0x0044A7D0 (return 0, mobiles have no tile flags)
	g_vtable_CMobile.fn[VT_GET_FLAGS / 4] = (vfunc_t)vt_stub_return_0;
	// IsDoor: slot 33 (0x84) = 0x0046E39C (return 0, mobiles are never doors)
	g_vtable_CMobile.fn[VT_IS_DOOR / 4] = (vfunc_t)vt_stub_return_0;
	// IsDoorNS: slot 34 (0x88) = 0x0046E3A9 (return 0)
	g_vtable_CMobile.fn[VT_IS_DOOR_NS / 4] = (vfunc_t)vt_stub_return_0;
	// ExcludedAmount: slot 64 (0x100) = 0x0044A7D0 (return 0)
	g_vtable_CMobile.fn[VT_EXCLUDED_AMOUNT / 4] = (vfunc_t)vt_stub_return_0;
	// SendHPUpdate: slot 139 (0x22C) = 0x004723E9 (no-op for CMobile base)
	g_vtable_CMobile.fn[VT_SEND_HP_UPDATE / 4] = (vfunc_t)vt_stub_return_0;
	// GetSurfaceFlags: slot 16 (0x40) = 0x00486641
	g_vtable_CMobile.fn[VT_GET_SURFACE_FLAGS / 4] = (vfunc_t)CMobile_GetSurfaceFlags_VT;
	// CheckSurface: slot 17 (0x44) = 0x00471323 (uses movement type)
	g_vtable_CMobile.fn[VT_CHECK_SURFACE / 4] = (vfunc_t)CMobile_CheckSurface_VT;
	// CheckSurfaceOf: slot 18 (0x48) = 0x00471188 (mount/vendor/moveType check)
	g_vtable_CMobile.fn[VT_CHECK_SURFACE_OF / 4] = (vfunc_t)CMobile_CheckSurfaceOf_VT;
	// GetWalkZ: slot 134 (0x218) = 0x004818AA
	g_vtable_CMobile.fn[VT_WALK_Z / 4] = (vfunc_t)CMobile_GetWalkZ;
	// GetHeight: slot 10 (0x28) = 0x0046F32F (returns 16)
	g_vtable_CMobile.fn[VT_GET_HEIGHT / 4] = (vfunc_t)CMobile_GetHeight_VT;
	// GetValue: slot 9 (0x24) = 0x00470253 (CMobile override)
	g_vtable_CMobile.fn[VT_GET_VALUE / 4] = (vfunc_t)CMobile_GetValue_VT;
	// GetName: slot 13 (0x34) = 0x00471BAE CMobile::GetName
	g_vtable_CMobile.fn[VT_GET_NAME / 4] = (vfunc_t)CMobile_GetName_VT;
	// Save: slot 50 (0xC8) = 0x004C6FD1 CMobile_Save
	g_vtable_CMobile.fn[VT_SAVE / 4] = (vfunc_t)CMobile_Save;
	// Delete: slot 36 (0x90) = 0x00484E8B CItem_Delete
	g_vtable_CMobile.fn[VT_DELETE / 4] = (vfunc_t)CItem_Delete;
	// FindInTagList: slot 89 (0x164) = 0x0046DF17 (CMobile override)
	g_vtable_CMobile.fn[VT_FIND_IN_TAG_LIST / 4] = (vfunc_t)CMobile_FindInTagList_VT;
	// PreDeleteCleanup: slot 107 (0x1AC) = 0x00470D44
	g_vtable_CMobile.fn[VT_DELETE_CONTENTS / 4] = (vfunc_t)CMobile_PreDeleteCleanup;
	// GetStoredWeight: slot 73 (0x124) = 0x0047100F
	g_vtable_CMobile.fn[VT_GET_STORED_WEIGHT / 4] = (vfunc_t)CMobile_GetStoredWeight;
	// IsDead: slot 61 (0xF4) = 0x0044A7D0 (returns 0 for CMobile)
	// already default stub
	// EquipmentDecayTick: slot 106 (0x1A8) = 0x00471888
	g_vtable_CMobile.fn[VT_EQUIP_DECAY_TICK / 4] = (vfunc_t)CMobile_EquipmentDecayTick;
	// SetHP: slot 112 (0x1C0) = 0x0046EF59
	g_vtable_CMobile.fn[VT_SET_HP / 4] = (vfunc_t)CMobile_SetHP;
	// SetMaxHP: slot 113 (0x1C4) = 0x0046EFD4
	g_vtable_CMobile.fn[VT_SET_MAX_HP / 4] = (vfunc_t)CMobile_SetMaxHP;
	// SetStamina: slot 114 (0x1C8) = 0x0046F035
	g_vtable_CMobile.fn[VT_SET_STAMINA / 4] = (vfunc_t)CMobile_SetStamina;
	// SetMaxStamina: slot 115 (0x1CC) = 0x0046F0B6
	g_vtable_CMobile.fn[VT_SET_MAX_STAMINA / 4] = (vfunc_t)CMobile_SetMaxStamina;
	// SetMana: slot 116 (0x1D0) = 0x0046F117
	g_vtable_CMobile.fn[VT_SET_MANA / 4] = (vfunc_t)CMobile_SetMana;
	// SetMaxMana: slot 117 (0x1D4) = 0x0046F198
	g_vtable_CMobile.fn[VT_SET_MAX_MANA / 4] = (vfunc_t)CMobile_SetMaxMana;
	// SetBaseStat: slot 124 (0x1F0) = 0x0046CE82
	g_vtable_CMobile.fn[VT_SET_BASE_STAT / 4] = (vfunc_t)CMobile_SetBaseStat;
	// AddToBaseStat: slot 125 (0x1F4) = 0x0046CF45
	g_vtable_CMobile.fn[VT_ADD_BASE_STAT / 4] = (vfunc_t)CMobile_AddToBaseStat;
	// AddToStatBonus: slot 126 (0x1F8) = 0x0046CFD8
	g_vtable_CMobile.fn[VT_ADD_STAT_BONUS / 4] = (vfunc_t)CMobile_AddToStatBonus;
	// SetStatBonus: slot 127 (0x1FC) = 0x0046D01B
	g_vtable_CMobile.fn[VT_SET_STAT_BONUS / 4] = (vfunc_t)CMobile_SetStatBonus;
	// AddHP: slot 118 (0x1D8) = 0x0046D25E
	g_vtable_CMobile.fn[VT_ADD_HP / 4] = (vfunc_t)CMobile_AddHP;
	// AddMaxHP: slot 119 (0x1DC) = 0x0046D1C8
	g_vtable_CMobile.fn[VT_ADD_MAX_HP / 4] = (vfunc_t)CMobile_AddMaxHP;
	// AddStamina: slot 120 (0x1E0) = 0x0046D2AC
	g_vtable_CMobile.fn[VT_ADD_STAMINA / 4] = (vfunc_t)CMobile_AddStamina;
	// AddMaxStamina: slot 121 (0x1E4) = 0x0046D1FA
	g_vtable_CMobile.fn[VT_ADD_MAX_STAMINA / 4] = (vfunc_t)CMobile_AddMaxStamina;
	// AddMana: slot 122 (0x1E8) = 0x0046D2DE
	g_vtable_CMobile.fn[VT_ADD_MANA / 4] = (vfunc_t)CMobile_AddMana;
	// AddMaxMana: slot 123 (0x1EC) = 0x0046D22C
	g_vtable_CMobile.fn[VT_ADD_MAX_MANA / 4] = (vfunc_t)CMobile_AddMaxMana;
	// GetMovementType: slot 37 (0x94) = 0x0046DBD3 (overrides CItem::GetMovementType)
	g_vtable_CMobile.fn[VT_GET_MOVEMENT_TYPE / 4] = (vfunc_t)CMobile_GetMovementType;
	// GetSpeed: slot 133 (0x214) = 0x0046D746
	g_vtable_CMobile.fn[VT_GET_SPEED / 4] = (vfunc_t)CMobile_GetSpeed;
	// HandleStaminaDrain: slot 136 (0x220) = 0x004713C6
	g_vtable_CMobile.fn[VT_HANDLE_STAM_DRAIN / 4] = (vfunc_t)CMobile_HandleStaminaDrain;
	// StaminaRegenTick: slot 137 (0x224) = 0x0047152E
	g_vtable_CMobile.fn[VT_STAM_REGEN / 4] = (vfunc_t)CMobile_StaminaRegenTick;
	// SendHPUpdate: slot 139 (0x22C) = 0x004723E9 (no-op in CMobile)
	// already default stub (returns 0, acts as no-op)
	// SetStatAbs: slot 143 (0x23C) = 0x004720C7
	g_vtable_CMobile.fn[VT_SET_STAT_ABS / 4] = (vfunc_t)CMobile_SetStatAbs;
	// GetMaxWeight: slot 105 (0x1A4) = 0x00470FF1
	g_vtable_CMobile.fn[VT_GET_MAX_WEIGHT / 4] = (vfunc_t)CMobile_GetMaxWeight;
	// NotifySkillGain: slot 144 (0x240) = 0x0047379C
	g_vtable_CMobile.fn[VT_SKILL_GAIN_NOTIFY / 4] = (vfunc_t)CMobile_NotifySkillGain;
	// SpeakSysMsg: slot 19 (0x4C) = 0x0046D9D5 (return mob->name)
	g_vtable_CMobile.fn[VT_SPEAK_SYS_MSG / 4] = (vfunc_t)CMobile_SpeakSysMsg_VT;
	// SetNotoriety: slot 128 (0x200)
	g_vtable_CMobile.fn[VT_SET_NOTORIETY / 4] = (vfunc_t)CMobile_SetNotoriety_VT;
	// PaperdollTitle: slot 129 (0x204) = 0x0046D784
	g_vtable_CMobile.fn[VT_PAPERDOLL_TITLE / 4] = (vfunc_t)CMobile_PaperdollTitle_VT;
	// WalkCheck: slot 130 (0x208) = 0x004818FB (CMobile base, 632 bytes)
	g_vtable_CMobile.fn[VT_WALK_CHECK / 4] = (vfunc_t)CMobile_WalkCheck_VT;
	// DoWalk: slot 131 (0x20C) = 0x00481B73
	g_vtable_CMobile.fn[VT_DO_WALK / 4] = (vfunc_t)CMobile_DoWalk;
	// SayCString: slot 21 (0x54) = 0x0046E610
	g_vtable_CMobile.fn[VT_SAY_CSTRING / 4] = (vfunc_t)CMobile_SayCString;
	// EmoteCString: slot 23 (0x5C) = 0x0046E72E
	g_vtable_CMobile.fn[VT_EMOTE_CSTRING / 4] = (vfunc_t)CMobile_EmoteCString;
	// SayHuedCString: slot 25 (0x64) = 0x0048D68A (same as CItem)
	g_vtable_CMobile.fn[VT_SAY_HUED_CSTRING / 4] = (vfunc_t)CItem_SayHuedCString_VT;
	// SayToEntity: slot 27 (0x6C) = 0x0046E855
	g_vtable_CMobile.fn[VT_SAY_TO_ENTITY / 4] = (vfunc_t)CMobile_SayToEntity_VT;
	// SendUpdateToList: slot 76 (0x130) = 0x004731FF (CMobile override)
	g_vtable_CMobile.fn[VT_NOTIFY_NEARBY / 4] = (vfunc_t)CMobile_SendUpdateToList_VT;
	// SendEntityUpdate: slot 77 (0x134) = 0x004730E7
	g_vtable_CMobile.fn[VT_SEND_ENTITY_UPDATE / 4] = (vfunc_t)CMobile_SendEntityUpdate_VT;
	// WeightRelated: slot 85 (0x154) = 0x004E0EA0
	g_vtable_CMobile.fn[VT_WEIGHT_RELATED / 4] = (vfunc_t)CMobile_WeightRelated_VT;
	// IsInWorld: slot 71 (0x11C) = 0x0044A7D0 (return 0 for mobiles)
	g_vtable_CMobile.fn[VT_IS_IN_WORLD / 4] = (vfunc_t)vt_stub_return_0;
	// ApplyStatusFlags: slot 91 (0x16C) = 0x004726AF
	g_vtable_CMobile.fn[VT_APPLY_STATUS_FLAGS / 4] = (vfunc_t)CMobile_ApplyStatusFlags_VT;
	// GetStatusFlags: slot 92 (0x170) = 0x00472682
	g_vtable_CMobile.fn[VT_GET_STATUS_FLAGS / 4] = (vfunc_t)CMobile_GetStatusFlags_VT;
	// GetNotoriety: slot 96 (0x180) = 0x004723F4
	g_vtable_CMobile.fn[VT_GET_NOTORIETY / 4] = (vfunc_t)CMobile_GetNotoriety_VT;
	// GetStamina: slot 135 (0x21C) = 0x00471350
	g_vtable_CMobile.fn[VT_GET_STAMINA / 4] = (vfunc_t)CMobile_GetStamina_VT;
	// StatCheck: slot 138 (0x228) = 0x004723B1
	g_vtable_CMobile.fn[VT_STAT_CHECK / 4] = (vfunc_t)CMobile_StatCheck_VT;
	// OnStatChange: slot 142 (0x238) = 0x00471F62
	g_vtable_CMobile.fn[VT_ON_STAT_CHANGE / 4] = (vfunc_t)CMobile_OnStatChange_VT;
	// SetInvulnerable: slot 140 (0x230) = 0x00472B78
	g_vtable_CMobile.fn[VT_SET_INVULN / 4] = (vfunc_t)CMobile_SetInvulnerable_VT;
	// ClearInvulnerable: slot 141 (0x234) = 0x00472B8F
	g_vtable_CMobile.fn[VT_CLR_INVULN / 4] = (vfunc_t)CMobile_ClearInvulnerable_VT;
	// EquipIterate: slot 109 (0x1B4) = 0x004724F5
	g_vtable_CMobile.fn[VT_EQUIP_ITERATE / 4] = (vfunc_t)CMobile_EquipIterate_VT;
	// OnDeath: slot 110 (0x1B8) = 0x0046F9E9
	g_vtable_CMobile.fn[VT_ON_DEATH / 4] = (vfunc_t)CMobile_CreateCorpse;
	// OnDeathWrap: slot 132 (0x210) = 0x0046D9B2
	g_vtable_CMobile.fn[VT_ON_DEATH_WRAP / 4] = (vfunc_t)CMobile_OnDeath_Wrap;
	// GetDirection: slot 78 (0x138) = 0x0046D9EB
	g_vtable_CMobile.fn[VT_GET_DIRECTION / 4] = (vfunc_t)CMobile_GetDirection;
	// RefreshAggression: slot 97 (0x184) = 0x0048FA3A
	g_vtable_CMobile.fn[VT_REFRESH_AGGRESSION / 4] = (vfunc_t)CEntity_RefreshAggression;
	// NotifyDamage: slot 102 (0x198) = 0x0048FC7B
	g_vtable_CMobile.fn[VT_NOTIFY_DAMAGE / 4] = (vfunc_t)CMobile_NotifyDamage;
	// EquipOnMobile: slot 48 (0xC0) = 0x004724E6 (return 0)
	g_vtable_CMobile.fn[VT_EQUIP_ON_MOBILE / 4] = (vfunc_t)CMobile_EquipOnMobile_VT;
	// PreDeleteClean: slot 111 (0x1BC) = 0x0047022A (return 0)
	g_vtable_CMobile.fn[VT_PRE_DELETE_CLEAN / 4] = (vfunc_t)CMobile_PreDeleteClean_VT;
	// CanBeFreelyAggressed: slot 98 (0x188) = 0x0047243A (override CContainer)
	g_vtable_CMobile.fn[VT_CAN_BE_FREELY_AGGRESSED / 4] = (vfunc_t)CMobile_CanBeFreelyAggressed_VT;

	// CResourceMobile: inherits CMobile
	// Binary 0x005EF1F8: normal NPC vtable (used by CResourceMobile_Constructor)
	vt_copy(&g_vtable_CResourceMobile, &g_vtable_CMobile);
	// ScalarDeletingDestructor: slot 0 (0x00) = 0x00484350
	g_vtable_CResourceMobile.fn[VT_DTOR / 4] = (vfunc_t)CResourceMobile_ScalarDelete;
	// DropAtFeet: slot 1 (0x04) = 0x00483931
	g_vtable_CResourceMobile.fn[VT_DROP_AT_FEET / 4] = (vfunc_t)CResourceMobile_DropAtFeet_VT;
	// SetLocation: slot 2 (0x08) = 0x00483A1E
	g_vtable_CResourceMobile.fn[VT_SET_LOCATION / 4] = (vfunc_t)CResourceMobile_SetLocation_VT;
	// Hide: slot 3 (0x0C) = 0x00483762 (CNPC_RemoveFromWorldGrid)
	g_vtable_CResourceMobile.fn[VT_HIDE / 4] = (vfunc_t)CNPC_RemoveFromWorldGrid;
	// DetachSpatial: slot 4 (0x10) = 0x004838FF
	g_vtable_CResourceMobile.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)CMobile_DetachSpatial_VT;
	// Delete: slot 36 (0x90) = 0x00484EDA (same logic as CItem_Delete)
	g_vtable_CResourceMobile.fn[VT_DELETE / 4] = (vfunc_t)CItem_Delete;
	// GetMovementType: slot 37 (0x94) = 0x0048B596 (CNPC flying override)
	g_vtable_CResourceMobile.fn[VT_GET_MOVEMENT_TYPE / 4] = (vfunc_t)CNPC_GetMovementType_VT;
	// Save: slot 50 (0xC8) = 0x004C7D7E CNPC_Save (writes @=N)
	g_vtable_CResourceMobile.fn[VT_SAVE / 4] = (vfunc_t)CNPC_Save;
	// IsNPC: slot 57 (0xE4) = 0x004E04C0 (return 1)
	g_vtable_CResourceMobile.fn[VT_IS_NPC / 4] = (vfunc_t)vt_stub_return_1;
	// SetSerial: slot 80 (0x140) = 0x004835C8
	g_vtable_CResourceMobile.fn[VT_SET_SERIAL / 4] = (vfunc_t)CNPC_SetSerial;
	// PaperdollTitle: slot 129 (0x204) = 0x0048356D
	g_vtable_CResourceMobile.fn[VT_PAPERDOLL_TITLE / 4] = (vfunc_t)CNPC_PaperdollTitle_VT;
	// SetInvulnerable: slot 140 (0x230) = 0x004842D8
	g_vtable_CResourceMobile.fn[VT_SET_INVULN / 4] = (vfunc_t)CNPC_SetInvulnerable_VT;
	// ClearInvulnerable: slot 141 (0x234) = 0x004842FB
	g_vtable_CResourceMobile.fn[VT_CLR_INVULN / 4] = (vfunc_t)CNPC_ClearInvulnerable_VT;
	// TestBehavior: slot 145 (0x244)
	g_vtable_CResourceMobile.fn[VT_TEST_BEHAVIOR / 4] = (vfunc_t)CNPC_TestBehavior_VT;
	// SetBehavior: slot 146 (0x248) - ORs bits into behaviorFlags
	g_vtable_CResourceMobile.fn[VT_SET_BEHAVIOR / 4] = (vfunc_t)CNPC_SetBehavior_VT;
	// ClrBehavior: slot 147 (0x24C) - ANDs out bits from behaviorFlags
	g_vtable_CResourceMobile.fn[VT_CLR_BEHAVIOR / 4] = (vfunc_t)CNPC_ClrBehavior_VT;

	// CNPC: inherits CResourceMobile
	// Binary 0x005EECF0: guard NPC vtable (used by CNPC_Constructor and
	// CGuard_Constructor, both of which bump vtable from CResourceMobile).
	vt_copy(&g_vtable_CNPC, &g_vtable_CResourceMobile);
	// ScalarDeletingDestructor: slot 0 (0x00) = 0x00461DA0
	g_vtable_CNPC.fn[VT_DTOR / 4] = (vfunc_t)CNPC_ScalarDelete;
	// DropAtFeet: slot 1 (0x04) = 0x0046197C
	g_vtable_CNPC.fn[VT_DROP_AT_FEET / 4] = (vfunc_t)CNPC_DropAtFeet_VT;
	// SetLocation: slot 2 (0x08) = 0x00461A9C
	g_vtable_CNPC.fn[VT_SET_LOCATION / 4] = (vfunc_t)CNPC_SetLocation_VT;
	// Hide: slot 3 (0x0C) = 0x00461B3D
	g_vtable_CNPC.fn[VT_HIDE / 4] = (vfunc_t)CNPC_Hide_VT;
	// DetachSpatial: slot 4 (0x10) = 0x00461B5C
	g_vtable_CNPC.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)CNPC_DetachSpatial_VT;
	// Delete: slot 36 (0x90) = 0x00484EDA (same logic as CItem_Delete)
	g_vtable_CNPC.fn[VT_DELETE / 4] = (vfunc_t)CItem_Delete;
	// Save: slot 50 (0xC8) = 0x004C832B CGuard_Save (writes @=G)
	g_vtable_CNPC.fn[VT_SAVE / 4] = (vfunc_t)CGuard_Save;
	// CheckEC: slot 59 (0xEC) = 0x004E04C0 (return 1)
	g_vtable_CNPC.fn[VT_CHECK_EC / 4] = (vfunc_t)vt_stub_return_1;

	// CPlayer: inherits CMobile
	// Binary 0x005EEA50: slot 6 (0x18) = return 1
	vt_copy(&g_vtable_CPlayer, &g_vtable_CMobile);
	// ScalarDeletingDestructor: slot 0 (0x00) = 0x00456EE0 (CPlayer override)
	g_vtable_CPlayer.fn[VT_DTOR / 4] = (vfunc_t)CPlayer_ScalarDelete;
	g_vtable_CPlayer.fn[VT_IS_PLAYER / 4] = (vfunc_t)vt_stub_return_1;
	// SetLocation: slot 2 (0x08) = 0x004894B1 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_LOCATION / 4] = (vfunc_t)CPlayer_SetLocation_VT;
	// DropAtFeet: slot 1 (0x04) = 0x00489766 (CPlayer override)
	g_vtable_CPlayer.fn[VT_DROP_AT_FEET / 4] = (vfunc_t)CPlayer_DropAtFeet_VT;
	// Save: slot 50 (0xC8) = 0x004C842C CPlayer_Save
	g_vtable_CPlayer.fn[VT_SAVE / 4] = (vfunc_t)CPlayer_Save;
	// IsDead: slot 61 (0xF4) = 0x004547D4 CPlayer_IsDead
	g_vtable_CPlayer.fn[VT_IS_DEAD / 4] = (vfunc_t)CPlayer_IsDead;
	// CItem_SetMurderCount: slot 95 (0x17C) = 0x00456DF1
	g_vtable_CPlayer.fn[VT_SET_MURDER_COUNT / 4] = (vfunc_t)CPlayer_IncrementMurderCount;
	// KarmaHandler: slot 148 (0x250) = 0x00456C9E
	g_vtable_CPlayer.fn[VT_KARMA_HANDLER / 4] = (vfunc_t)CPlayer_KarmaHandler;
	// ReceiveHelpfulAction: slot 147 (0x24C) = 0x0045646B
	// CPlayer overrides CNPC's ClrBehavior with aggressor tracking
	g_vtable_CPlayer.fn[VT_CLR_BEHAVIOR / 4] = (vfunc_t)CPlayer_ReceiveHelpfulAction;
	// NotifySkillGain: slot 144 (0x240) = 0x00456869 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SKILL_GAIN_NOTIFY / 4] = (vfunc_t)CPlayer_NotifySkillGain;
	// GetStamina: slot 135 (0x21C) = 0x0045651D (CPlayer override)
	g_vtable_CPlayer.fn[VT_GET_STAMINA / 4] = (vfunc_t)CPlayer_GetStamina_VT;
	// SendHpUpdate: slot 139 (0x22C) = 0x004564E6 (murder report cleanup)
	g_vtable_CPlayer.fn[VT_SEND_HP_UPDATE / 4] = (vfunc_t)CPlayer_MurderReportCleanup;
	// FameKarmaChange: slot 104 (0x1A0) = 0x004564C1 (CPlayer override)
	g_vtable_CPlayer.fn[VT_FAME_KARMA_CHANGE / 4] = (vfunc_t)CPlayer_FameKarmaChange_VT;
	// CancelTrade: slot 108 (0x1B0) = 0x00454E46 CPlayer_CancelTrade
	g_vtable_CPlayer.fn[VT_CANCEL_TRADE / 4] = (vfunc_t)CPlayer_CancelTrade;
	// TestBehavior: slot 145 (0x244) = 0x004568AE (CPlayer override)
	g_vtable_CPlayer.fn[VT_TEST_BEHAVIOR / 4] = (vfunc_t)CPlayer_TestBehavior_VT;
	// SetBehavior: slot 146 (0x248) = 0x004568F3 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_BEHAVIOR / 4] = (vfunc_t)CPlayer_SetBehavior_VT;
	// AddToAggressorList: slot 100 (0x190) = 0x0048FAC5 (CPlayer override)
	g_vtable_CPlayer.fn[VT_ADD_TO_AGGRESSOR_LIST / 4] = (vfunc_t)CPlayer_AddToAggressorList_VT;
	// FillAggroInfo: slot 103 (0x19C) = 0x0045604A (CPlayer override)
	g_vtable_CPlayer.fn[VT_FILL_AGGRO_INFO / 4] = (vfunc_t)CPlayer_FillAggroInfo_VT;
	// PreDeleteClean: slot 111 (0x1BC) = 0x004517BC (CPlayer override)
	g_vtable_CPlayer.fn[VT_PRE_DELETE_CLEAN / 4] = (vfunc_t)CPlayer_PreDeleteClean_VT;
	// SetHP: slot 112 (0x1C0) = 0x0045253A (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_HP / 4] = (vfunc_t)CPlayer_SetHP_VT;
	// SetMaxHP: slot 113 (0x1C4) = 0x004525DE (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_MAX_HP / 4] = (vfunc_t)CPlayer_SetMaxHP_VT;
	// SetStamina: slot 114 (0x1C8) = 0x00452733 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_STAMINA / 4] = (vfunc_t)CPlayer_SetStamina_VT;
	// SetMaxStamina: slot 115 (0x1CC) = 0x004527BA (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_MAX_STAMINA / 4] = (vfunc_t)CPlayer_SetMaxStamina_VT;
	// SetMana: slot 116 (0x1D0) = 0x00452648 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_MANA / 4] = (vfunc_t)CPlayer_SetMana_VT;
	// SetMaxMana: slot 117 (0x1D4) = 0x004526C9 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_MAX_MANA / 4] = (vfunc_t)CPlayer_SetMaxMana_VT;
	// OnDeath: slot 110 (0x1B8) = 0x00451A6F (CPlayer override)
	g_vtable_CPlayer.fn[VT_ON_DEATH / 4] = (vfunc_t)CPlayer_OnDeath;
	// OnDeathWrap: slot 132 (0x210) = 0x004524CC (CPlayer override)
	g_vtable_CPlayer.fn[VT_ON_DEATH_WRAP / 4] = (vfunc_t)CPlayer_OnDeathWrap_VT;
	// WalkCheck: slot 130 (0x208) = 0x00453841 (CPlayer override)
	g_vtable_CPlayer.fn[VT_WALK_CHECK / 4] = (vfunc_t)CPlayer_WalkCheck_VT;
	// Delete: slot 36 (0x090) = 0x00484DA1 (CPlayer override, adds g_DeleteAllowed check)
	g_vtable_CPlayer.fn[VT_DELETE / 4] = (vfunc_t)CPlayer_Delete_VT;
	// GetMovementType: slot 37 (0x094) = 0x0048B5D7 (CPlayer override)
	g_vtable_CPlayer.fn[VT_GET_MOVEMENT_TYPE / 4] = (vfunc_t)CPlayer_GetMovementType_VT;
	// GetSurfaceFlags: slot 16 (0x040) = 0x0048667A (CPlayer override)
	g_vtable_CPlayer.fn[VT_GET_SURFACE_FLAGS / 4] = (vfunc_t)CPlayer_GetSurfaceFlags_VT;
	// CheckSurfaceOf: slot 18 (0x048) = 0x0047122A (CPlayer shove override)
	g_vtable_CPlayer.fn[VT_CHECK_SURFACE_OF / 4] = (vfunc_t)CPlayer_CheckSurfaceOf_VT;
	// SetSerial: slot 80 (0x140) = 0x00450FDF (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_SERIAL / 4] = (vfunc_t)CPlayer_SetSerial_VT;
	// Hide: slot 3 (0x0C) = 0x00488209 (CPlayer override)
	g_vtable_CPlayer.fn[VT_HIDE / 4] = (vfunc_t)CPlayer_HideVT;
	// DetachSpatial: slot 4 (0x10) = 0x004886DF (CPlayer override)
	g_vtable_CPlayer.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)CPlayer_DetachSpatial_VT;
	// PaperdollTitle: slot 129 (0x204) = 0x00453000 (CPlayer override)
	g_vtable_CPlayer.fn[VT_PAPERDOLL_TITLE / 4] = (vfunc_t)CPlayer_PaperdollTitle_VT;
	// SetNotoriety: slot 128 (0x200) = 0x00456E51 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SET_NOTORIETY / 4] = (vfunc_t)CPlayer_SetNotoriety_VT;
	// SpeakSysMsg: slot 19 (0x04C) = 0x00454BE5 (CPlayer override)
	g_vtable_CPlayer.fn[VT_SPEAK_SYS_MSG / 4] = (vfunc_t)CPlayer_SpeakSysMsg_VT;
	// NotifyDamage: slot 102 (0x198) = 0x004561D8 (CPlayer override)
	g_vtable_CPlayer.fn[VT_NOTIFY_DAMAGE / 4] = (vfunc_t)CPlayer_NotifyDamage_VT;

	// CShopkeeper: inherits CResourceMobile
	// Binary 0x005EFDE8: vendor NPC vtable
	vt_copy(&g_vtable_CShopkeeper, &g_vtable_CResourceMobile);
	// Dtor: slot 0 (0x00) = 0x004D2F50 CShopkeeper_ScalarDelete
	g_vtable_CShopkeeper.fn[VT_DTOR / 4] = (vfunc_t)CShopkeeper_ScalarDelete;
	// Delete: slot 36 (0x90) = 0x00484EDA (same logic as CItem_Delete)
	g_vtable_CShopkeeper.fn[VT_DELETE / 4] = (vfunc_t)CItem_Delete;
	// Save: slot 50 (0xC8) = 0x004C835B CShopkeeper_Save
	g_vtable_CShopkeeper.fn[VT_SAVE / 4] = (vfunc_t)CShopkeeper_Save;
	// IsVendor: slot 58 (0xE8) = 0x004E04C0 (return 1)
	g_vtable_CShopkeeper.fn[VT_IS_VENDOR / 4] = (vfunc_t)vt_stub_return_1;

	// CGuard: identical to CNPC in the binary.
	// Binary has no separate CGuard vtable - guards use the CNPC vtable at
	// 0x005EECF0. We keep g_vtable_CGuard as a copy for compatibility with
	// our NPC type system.
	vt_copy(&g_vtable_CGuard, &g_vtable_CNPC);

	// CMulti: inherits CContainer
	// Binary 0x005EF7C8: vtable[0xD4]=return_1, vtable[0x124]=CContainer_GetStoredWeight.
	vt_copy(&g_vtable_CMulti, &g_vtable_CContainer);
	// Dtor: slot 0 (0x00) = 0x004912C0 CCorpse_ScalarDelete
	g_vtable_CMulti.fn[VT_DTOR / 4] = (vfunc_t)CCorpse_ScalarDelete;
	// Binary 0xE0: VT_IS_SPATIAL = 0x0044A7D0 (return 0, inherited from CContainer)
	g_vtable_CMulti.fn[VT_HAS_CORPSE_EQ / 4] = (vfunc_t)vt_stub_return_1;
	// Save: slot 50 (0xC8) = 0x004C6A7F CMulti_Save
	g_vtable_CMulti.fn[VT_SAVE / 4] = (vfunc_t)CMulti_Save;
	// ExcludedAmount: slot 64 (0x100) = 0x0044A7D0 (return 0, no exclusion)
	g_vtable_CMulti.fn[VT_EXCLUDED_AMOUNT / 4] = (vfunc_t)vt_stub_return_0;
	// GetValue: slot 9 (0x24) = 0x00489A85 (CMulti override)
	g_vtable_CMulti.fn[VT_GET_VALUE / 4] = (vfunc_t)CMulti_GetValue_VT;
	// NotifyNearby: slot 76 (0x130) = 0x00489BDA (CMulti override)
	g_vtable_CMulti.fn[VT_NOTIFY_NEARBY / 4] = (vfunc_t)CMulti_NotifyNearby_VT;
	// SendEntityUpdate: slot 77 (0x134) = 0x00489AD5 (CMulti override)
	g_vtable_CMulti.fn[VT_SEND_ENTITY_UPDATE / 4] = (vfunc_t)CMulti_SendEntityUpdate_VT;
	// GetDirection: slot 78 (0x138) = 0x0048AB13
	g_vtable_CMulti.fn[VT_GET_DIRECTION / 4] = (vfunc_t)CMulti_GetDirection_VT;
	// GetAmount: slot 90 (0x168) = 0x00489BC7
	g_vtable_CMulti.fn[VT_GET_AMOUNT / 4] = (vfunc_t)CMulti_GetAmount_VT;
	// OnDeath/SetDecayTick: slot 110 (0x1B8) = 0x0048AB27
	g_vtable_CMulti.fn[VT_ON_DEATH / 4] = (vfunc_t)CMulti_OnDeath_VT;
	// PreDeleteClean: slot 111 (0x1BC) = NULL (no override in CMulti)
	g_vtable_CMulti.fn[VT_PRE_DELETE_CLEAN / 4] = NULL;

	// CEgg: inherits CItem
	// Binary 0x005EFB38: 105 slots (same count as CItem)
	vt_copy(&g_vtable_CEgg, &g_vtable_CItem);
	// Dtor: slot 0 (0x00) = 0x004913B0 CEgg_ScalarDelete
	g_vtable_CEgg.fn[VT_DTOR / 4] = (vfunc_t)CEgg_ScalarDelete;
	// DropAtFeet: slot 1 (0x04) = 0x004905B5
	g_vtable_CEgg.fn[VT_DROP_AT_FEET / 4] = (vfunc_t)CEgg_DropAtFeet;
	// SetLocation: slot 2 (0x08) = 0x004905EC
	g_vtable_CEgg.fn[VT_SET_LOCATION / 4] = (vfunc_t)CEgg_SetLocation;
	// DetachFromSpatial: slot 4 (0x10) = 0x004905A2 (thunk to 0x0048840D)
	g_vtable_CEgg.fn[VT_DETACH_SPATIAL / 4] = (vfunc_t)CResourceEntity_DetachFromSpatial;
	// GetSurfaceFlags: slot 16 (0x40) = 0x004913A0 (return 0, eggs have no surface)
	g_vtable_CEgg.fn[VT_GET_SURFACE_FLAGS / 4] = (vfunc_t)vt_stub_return_0;
	// SayCString: slot 21 (0x54) = 0x0048D192 (CEntity base version)
	g_vtable_CEgg.fn[VT_SAY_CSTRING / 4] = (vfunc_t)CEntity_SayCString_VT;
	// ItemCheck9C: slot 39 (0x9C) = 0x004E04C0 (return 1)
	g_vtable_CEgg.fn[VT_ITEM_CHECK_9C / 4] = (vfunc_t)vt_stub_return_1;
	// MoveTo: slot 42 (0xA8) = 0x00490FBB (CEgg uses RegisterLocation)
	g_vtable_CEgg.fn[VT_MOVE_TO / 4] = (vfunc_t)CItem_RegisterLocation;
	// Delete: slot 36 (0x90) = 0x0048510A (only during egg processing pass)
	g_vtable_CEgg.fn[VT_DELETE / 4] = (vfunc_t)CEgg_Delete;
	// AddToContainer: slot 45 (0xB4) = 0x00490573 (no-op, eggs can't go in containers)
	g_vtable_CEgg.fn[VT_ADD_TO_CONTAINER / 4] = (vfunc_t)vt_stub_return_0;
	// EquipOnMobile: slot 48 (0xC0) = 0x00490580 (return 0, eggs can't be equipped)
	g_vtable_CEgg.fn[VT_EQUIP_ON_MOBILE / 4] = (vfunc_t)vt_stub_return_0;
	// Save: slot 50 (0xC8) = 0x004C83D0 CEgg_Save
	g_vtable_CEgg.fn[VT_SAVE / 4] = (vfunc_t)CEgg_Save;

	// CSignpost: inherits CItem
	vt_copy(&g_vtable_CSignpost, &g_vtable_CItem);
	// Dtor: slot 0 (0x00) = 0x004912F0 CSignpost_ScalarDelete
	g_vtable_CSignpost.fn[VT_DTOR / 4] = (vfunc_t)CSignpost_ScalarDelete;
	g_vtable_CSignpost.fn[VT_IS_SPATIAL / 4] = (vfunc_t)vt_stub_return_1;
	// GetValue: slot 9 (0x24) = 0x0048B4B3 (CSignpost override)
	g_vtable_CSignpost.fn[VT_GET_VALUE / 4] = (vfunc_t)CSignpost_GetValue_VT;
	// SayCString: slot 21 (0x54) = 0x0048D192 (CEntity base version)
	g_vtable_CSignpost.fn[VT_SAY_CSTRING / 4] = (vfunc_t)CEntity_SayCString_VT;
	// Delete: slot 36 (0x90) = 0x00484DF9 (Check1 only, no Check2)
	g_vtable_CSignpost.fn[VT_DELETE / 4] = (vfunc_t)CSignpost_Delete;
	// Save: slot 50 (0xC8) = 0x004C6BA6 CSignpost_Save
	g_vtable_CSignpost.fn[VT_SAVE / 4] = (vfunc_t)CSignpost_Save;

	// CBoard: inherits CContainer
	// Binary 0x005EE690: 118 dwords (110 primary + 7 CChannel secondary + NULL)
	vt_copy(&g_vtable_CBoard, &g_vtable_CContainer);
	// Dtor: slot 0 (0x00) = 0x00432230 CBulletinBoard_ScalarDelete
	g_vtable_CBoard.fn[VT_DTOR / 4] = (vfunc_t)CBulletinBoard_ScalarDelete;
	// Save: slot 50 (0xC8) = 0x004C6B76 CBoard_Save
	g_vtable_CBoard.fn[VT_SAVE / 4] = (vfunc_t)CBoard_Save;
	// HasAccessibleContents: slot 54 (0xD8) = 0x0044A7D0 (return 0)
	g_vtable_CBoard.fn[VT_HAS_ACCESSIBLE_CONTENTS / 4] = (vfunc_t)vt_stub_return_0;
	// CheckDC: slot 55 (0xDC) = 0x004E04C0 (return 1)
	g_vtable_CBoard.fn[VT_CHECK_DC / 4] = (vfunc_t)vt_stub_return_1;
	// ExcludedAmount: slot 64 (0x100) = 0x0044A7D0 (return 0, no exclusion)
	g_vtable_CBoard.fn[VT_EXCLUDED_AMOUNT / 4] = (vfunc_t)vt_stub_return_0;
	// Delete: slot 36 (0x90) = 0x00484FC7 CBoard_Delete
	g_vtable_CBoard.fn[VT_DELETE / 4] = (vfunc_t)CBoard_Delete;
}
