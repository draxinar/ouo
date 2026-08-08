#ifndef ITEM_H_
#define ITEM_H_

#include <stdint.h>

#include "resource_entity.h"
#include "res.h"

__extension__ typedef struct CCorpse CCorpse;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMultiComponent CMultiComponent;
__extension__ typedef struct ResEntitySlot ResEntitySlot;
__extension__ typedef struct ScriptAttachNode ScriptAttachNode;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CItemTracking CItemTracking;
__extension__ typedef struct CTagListManager CTagListManager;
__extension__ typedef struct TimerNode TimerNode;
__extension__ typedef struct CVector CVector;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CDataBuffer CDataBuffer;
__extension__ typedef struct CString CString;
__extension__ typedef struct StdPtrList StdPtrList;
__extension__ typedef struct CResourceType CResourceType;
__extension__ typedef struct CResourceNode CResourceNode;
__extension__ typedef struct TagNode TagNode;

/*
 * Lazily-allocated per-item "last seen" state (0x18 bytes). Records
 * previous location/container so a dropped item can roll back if the
 * world rejects the new placement. Allocated from a free list whose
 * next-pointer reuses offset 0x18.
 */
struct CItemTracking {
	CLocation lastLoc;       // 0x00
	CLocation lastContLoc;   // 0x06
	uint32_t lastCont;       // 0x0C - last container serial
	uint32_t lastMob;        // 0x10 - last wearer serial
	uint16_t lastMobEqPos;   // 0x14 - default 0x1A
	uint16_t _tracking_pad;  // 0x16
	CItemTracking *freeNext; // 0x18 - free list link
};

/*
 * Concrete world-item entity (0x50 bytes). Third rung of the hierarchy
 * (CEntity -> CResourceEntity -> CItem), and the base for CContainer,
 * CMobile, CPlayer, CNPC, CCorpse, CSignpost, and CMulti variants.
 * Owns the spatial grid, hash, and template-chain links used by the
 * world-lookup paths.
 */
struct CItem {
	CResourceEntity resourceEntity; // 0x00
	TimerNode *timerHead;           // 0x1C - per-entity timer chain
	CItem *spatialNext;             // 0x20
	CItem *spatialPrev;             // 0x24
	CItem *parent;                  // 0x28 - parent container
	CItem *hashNext;                // 0x2C - serial hash chain
	CItem *hashPrev;                // 0x30
	CItemTracking *tracking;        // 0x34
	CItem *templateChainNext;       // 0x38 - g_TemplateChain at 0x0068B3A8
	CItem *templateChainPrev;       // 0x3C
	uint32_t serial;                // 0x40
	uint8_t itemFlags;              // 0x44 - see ItemFlag
	uint8_t decayCount;             // 0x45
	uint16_t amount;                // 0x46 - stack count
	CTagListManager *tagList;       // 0x48
	CMultiComponent *multiPtr;      // 0x4C
};

/*
 * Bit values for CItem.itemFlags. Bit 0x04 is dual-purpose: stackable
 * for items, open-state for doors and containers.
 */
enum ItemFlag {
	ItemFlag_Invisible = 0x02,     // bit 1: entity is hidden/invisible (0x00486B11)
	ItemFlag_Stackable = 0x04,     // bit 2: stackable item (0x00491260 → 0x00490EF9 → 0x00486B11)
	ItemFlag_Open = 0x04,          // bit 2: open state (doors/containers, 0x00490F0E)
	ItemFlag_Poisoned = 0x08,      // bit 3: weapon poison (runtime); also save skip flag (0x004C8B2D)
	ItemFlag_ServerOnly = 0x10,    // bit 4: server-only, not sent to clients (0x00486AB7)
	ItemFlag_DeletePending = 0x20, // bit 5: marked for deletion
	ItemFlag_Deleted = 0x40,       // bit 6: fully destroyed
};

/*
 * 0x00486AB7 - CItem::IsServerOnly
 *
 * Check if item is server-only (not sent to clients).
 * Tests itemFlags bit 0x10. Eggs/spawn points have stat=16 (0x10).
 */
static inline int
CItem_IsServerOnly(CItem *item)
{
	return (item->itemFlags & ItemFlag_ServerOnly) ? 1 : 0;
}

/*
 * 0x00486B6C - CItem::HasDeleteFlag
 *
 * Check if item is flagged for pending deletion.
 */
static inline int
CItem_HasDeleteFlag(CItem *item)
{
	return (item->itemFlags & ItemFlag_DeletePending) ? 1 : 0;
}

/*
 * 0x00486B8A - CItem::SetDeleteFlag
 *
 * Set or clear the delete-pending flag (bit 0x20).
 */
static inline void
CItem_SetDeleteFlag(CItem *item, int set)
{
	if (set)
		item->itemFlags |= ItemFlag_DeletePending;
	else
		item->itemFlags &= ~ItemFlag_DeletePending;
}

/*
 * 0x00486BBD - CItem::IsDeleted
 *
 * Check if item has been fully deleted.
 */
static inline int
CItem_IsDeleted(CItem *item)
{
	return (item->itemFlags & ItemFlag_Deleted) ? 1 : 0;
}

/*
 * One node in a CSignpost's linked list of direction offsets.
 */
__extension__ typedef struct VectNode VectNode;
struct VectNode {
	int16_t x;       // 0x00
	int16_t y;       // 0x02
	int16_t z;       // 0x04
	int16_t _pad06;  // 0x06
	VectNode *next;  // 0x08
};

/*
 * Signpost / static-multi spatial entity (@=Z, 0x64 bytes, vtable
 * 0x005EF988). Extends CItem, repurposing the CContainer-range slots
 * 0x50-0x57 for the VectNode chain and adding a bounding box at 0x58.
 */
__extension__ typedef struct CSignpost CSignpost;
struct CSignpost {
	CItem item;           // 0x00
	VectNode *vectHead;   // 0x50
	CPlayer *lockOwner;   // 0x54 - map editor owner
	int16_t mapExtent[6]; // 0x58 - x1, y1, x2, y2, z1, z2
};

struct CVector;

extern StdPtrList g_entityMgrList; // 0x006CA928
extern uint32_t g_ArchiveFlag; // 0x006CA934
extern CItem *g_ArchiveHash[0x4000]; // 0x006CA938

int CItem_GetMovementType_VT(CItem *item); // 0x004322B0
int CItem_GetFlags_VT(CItem *item); // 0x004322C0
void CItem_AddToContainerVT(CItem *item, CItem *container, CLocation *loc); // 0x00449D9D
void CItem_AddToContB8_VT(CItem *self, CItem *container, CLocation *loc); // 0x0044A0A4
void CItem_AddToContBC_VT(CItem *self, CItem *container, CLocation *loc); // 0x0044A2E5
int vt_stub_return_0(CItem *self); // 0x0044A7D0
int CItem_GetSortKey_VT(CItem *self); // 0x0044A7E0
uint8_t CItem_GetDecayCount(CItem *item); // 0x004592C0
uint8_t CItem_GetGlobalDecayMax(CItem *item); // 0x004592D1
void CItem_SetDecayCount(CItem *item, uint8_t count); // 0x004592E6
int CItem_HasMultiDeleteTag(CItem *item); // 0x004592FC
void CItem_DecayTick(CItem *item); // 0x0045932E
void CItem_PlaceInWorld(CItem *item, int placeFlag); // 0x004594D4
int CItem_TryEquipOnMobile(CItem *item, CItem *mob); // 0x0045B850
int CItem_HasResourceFlag(CItem *item); // 0x0045E57D
int CResourceEntity_GetResourceAmount(CItem *item, uint16_t resourceId); // 0x0045E5D4
int CItem_GetResourceAmount(CItem *item, uint16_t resourceId); // 0x0045E636
int CItem_HasPositiveResources(CItem *self); // 0x0045E698
int CItem_HasResourceRecipe(CItem *item); // 0x0045E70F
int CItem_GetMinResourceRatio(CItem *item); // 0x0045E785
void CItem_SetSortKey(CItem *item, int value); // 0x00490CA6
int CItem_HasEnoughResources(CItem *item, ResEntitySlot *slot, int amount); // 0x0045E820
void CItem_ConsumeResource(CItem *item, uint16_t resourceId, int amount); // 0x0045E895
int CItem_FinalizeConsume(CItem *item, int amountToReduce); // 0x0045E9AD
int CItem_ConsumeAmount(CItem *dest, CItem *source, int amount); // 0x0045EA86
void CItem_TransferResourcesByTemplate(CItem *source, CItem *dest, int amount); // 0x0045EB18
int CItem_FinalizeConsume_VT(CItem *this, int amountToReduce); // 0x0045EB9A
/*
 * GM call queue entry (0x10 bytes). Extends CResListNode with the
 * GM-call status int. The single instance lives at g_GMPlayerList
 * (0x006999A0); gmCallStatus aliases the g_GMCallStatus global at
 * 0x006999AC and is read/written by the getGMCallStatus/setGMCallStatus
 * script handlers.
 */
__extension__ typedef struct CEditorObj CEditorObj;
struct CEditorObj {
	CResListNode node;  // 0x00
	int gmCallStatus;   // 0x0C - aliased as g_GMCallStatus at 0x006999AC
};

int CEditorObj_HandleGMSingle(CEditorObj *this, void *arg); // 0x0045F196
void CEditorObj_SetGMCallStatus(CEditorObj *this, int value); // 0x0045F1A5
int CEditorObj_GetGMCallStatus(CEditorObj *this); // 0x0045F1BB
void CItem_GetContainerItems(CItem *ent, CVector *list); // 0x004600E3
void CItem_UpdateContainInfo(CItem *item, int forceUpdate); // 0x00467080
CItemTracking *Item_AllocContainInfo(void); // 0x00467248
void CItem_ReleaseTracking(CItem *item); // 0x00467357
void FreeContainInfo(CItemTracking *tr); // 0x00467384
void CItem_SetLastContainer(CItem *item, uint32_t serial); // 0x0046739E
void CItem_SetLastMobile(CItem *item, uint32_t serial); // 0x004673CB
void CItem_SetLastMobileEquipPos(CItem *item, uint16_t pos); // 0x004673F8
void CItem_SetLastLocation(CItem *item, int16_t x, int16_t y, int16_t z); // 0x00467427
void CItem_SetLastContainerLocation(CItem *item, int16_t x, int16_t y, int16_t z); // 0x00467462
int CItem_DeleteCheck1(CItem *item); // 0x00484CF0
int CItem_DeleteCheck2(CItem *item); // 0x00484D28
void CItem_PrepareDelete(CItem *item); // 0x00484D66
void CItem_Delete(CItem *item); // 0x00484E8B
void CBoard_Delete(CItem *self); // 0x00484FC7
void CWeapon_Delete(CItem *self); // 0x00485016
int CItem_GetItemAmount(CItem *item); // 0x004854F2
int CItem_IsMovable(CItem *item); // 0x004854F2
CResourceNode *CItem_FindResourceNodeByIdAndType(CItem *item, CResourceType *resType, int8_t type); // 0x00485D14
void CItem_Setup(CItem *item, int type, CLocation *loc, int flags, int expand); // 0x00485EB9
int CItem_GetObjVarResTypeInner(CItem *item, int *resultOut, CResourceType *resType, int8_t type, int valueIndex); // 0x004861AE
int CItem_GetObjVarResType(CItem *item, int *resultOut, int resTypeId, int8_t type, int valueIndex); // 0x004861F4
int CItem_GetSurfaceFlags_VT(CItem *self, int moveType); // 0x004863D1
int CEntity_CheckDC_VT(CItem *self, uint16_t tileID); // 0x004866C7
int CEntity_CheckSurfaceOf_VT(CItem *self, CItem *entity); // 0x004866E2
CItem *CreateStaticEntity(void); // 0x00486706
void StaticEntity_Constructor(CItem *self); // 0x00486831
void FreeStaticItem(CItem *item); // 0x0048686C
CItem *CItem_Constructor(CItem *mem); // 0x004868BA
int CItem_IsHidden_VT(CItem *item); // 0x004869DA
void CItem_NotifyNearbyUpdate(CItem *item, int mode); // 0x004869EF
void CItem_SetHidden_VT(CItem *item, int set); // 0x00486A8A
void CItem_SetItemFlag(CItem *item, uint8_t flag, int setOrClear); // 0x00486AD5
int CItem_HasItemFlag(CItem *item, int flag); // 0x00486B11
void CItem_SetServerOnly(CItem *item, int set); // 0x00486B39
void CItem_SetLockdown(CItem *item, int set); // 0x00486BDB
int CItem_HasMulti_Filter(CItem *item); // 0x00486C0E
int CItem_IsMultiOwner(CItem *item); // 0x00486C27
int CItem_AttachMultiComponent(CItem *item, uint32_t ownerSerial, CLocation *offset); // 0x00486C65
int CItem_SetMultiComponent(CItem *item); // 0x00486CA1
int CItem_AttachMultiSlave(CItem *item, CLocation *offset); // 0x00486CD5
int CItem_SetMultiSlave(CItem *item); // 0x00486D64
void CItem_DetachMulti(CItem *item); // 0x00486E0E
void CItem_NotifyMultiServerOnly(CItem *item, CLocation *loc); // 0x00486E81
void CItem_SendServerOnlyUpdate(CItem *item, CItem *player); // 0x00486EB3
void CItem_Destructor(CItem *item); // 0x00486F73
void CItem_SetShopScript(CItem *item, CString *scriptStr); // 0x00487188
int CItem_GetAfterShopScript(CItem *item, CString *out); // 0x004871B7
void CItem_SaveText_VT(CItem *item, char *buf, int dynamicFlag); // 0x00487211
char *CItem_GetNameString_VT(CItem *item, int withAmount); // 0x0048735F
void CItem_SetSerial(CItem *item, uint32_t newSerial); // 0x00487612
void CItem_DetachHomeScript(CItem *item); // 0x004876F5
int CItem_HasHome(CItem *item); // 0x0048770D
int CItem_GetHomeLocation(CItem *item, CLocation *outLoc); // 0x0048772E
void CItem_SetHome(CItem *item, CLocation *loc); // 0x00487768
void CItem_ReturnToHome(CItem *item); // 0x004877D0
int CEntity_IsAtHome(CItem *item); // 0x0048783D
int CItem_IsInBankBox(CItem *item); // 0x004878F5
int CItem_HasSecuredAncestor(CItem *this); // 0x00487962
void *CItem_FindSecuredContainer(CItem *item); // 0x004879B5
void StaticEntity_Hide_VT(CItem *self); // 0x00487AD9
int CItem_IsNonCountable(CItem *item); // 0x00487C94
int CItem_AdjustParentWeight(CItem *item, CItem *parentArg); // 0x00487DBA
int CItem_AddWeightToParent(CItem *item, CItem *parent); // 0x00487E27
void CItem_HideVT(CItem *item); // 0x00487E94
void StaticEntity_DetachSpatial_VT(CItem *self); // 0x00488382
void CItem_DetachFromSpatial(CItem *item); // 0x004885CB
CItem *CItem_FindTopContainerMobile(CItem *item); // 0x004887FD
void CItem_ReattachSpatial_VT(CItem *item); // 0x004888DD
void CItem_AddToEquip_VT(CItem *self, CMobile *mob); // 0x0048897A
int CItem_GetTagInt(CItem *item, const char *name, int *outVal); // 0x00488A37
int CItem_GetMonetaryVal_VT(CItem *self); // 0x00488A6F
int CItem_GetValue_VT(CItem *self, int useResource, int normalize); // 0x00488ABC
int CItem_NormalizeValue(CItem *self, int value); // 0x00488D19
int CItem_EquipOnMobile(CItem *item, CMobile *mob, int layer); // 0x00488D81
void StaticEntity_SetLocation_VT(CItem *self, CLocation *loc); // 0x00488EF5
void CItem_InternalMove(CItem *item, CLocation *loc, int flag); // 0x0048909D
void CItem_NotifyMultiDetach(CItem *item, int flag); // 0x00489158
void CItem_DropAtFeet(CItem *item, CLocation *loc); // 0x004891F4
void StaticEntity_SetLocation_Link(CItem *self, CLocation *loc); // 0x004893B1
void CItem_SetLocation_VT(CItem *self, CLocation *loc); // 0x00489432
void CItem_SetCampfireTimestamp(CItem *entity); // 0x004895C7
int CItem_ReturnToTracked(CItem *item); // 0x0048991A
uint16_t CCorpse_GetCorpseBodyType(CCorpse *this); // 0x00489B97
void CCorpse_SetCorpseBodyType(CCorpse *this, uint16_t bodyType); // 0x00489BAC
void CItem_SendEntityUpdate_VT(CItem *self, CItem *viewer, int withEquip); // 0x00489C96
void CItem_SendUpdateToList(CItem *item, CVector *players, int mode); // 0x00489D9E
int CItem_HasContainerVT(CItem *item); // 0x00489EC7
int CItem_IsEquipped_VT(CItem *item); // 0x00489EE0
int CItem_GetLayer_VT(CItem *item); // 0x00489F46
int CItem_GetDecayMax(CItem *item); // 0x00489FAB
int CItem_GetDirectionVT(CItem *item); // 0x0048A11F
void CItem_GetContainerDim_VT(CItem *self, uint16_t *outWidth, uint16_t *outHeight); // 0x0048A170
int CItem_IsMoveable_VT(CItem *self, CItem *viewer); // 0x0048A1B3
int CItem_IsFreelyUsable_VT(CItem *self, CItem *viewer); // 0x0048A29E
int CItem_IsFreelyViewable_VT(CItem *self, CItem *viewer); // 0x0048A412
CLocation *CItem_GetLocationVT(CItem *item); // 0x0048A531
int CItem_CanStackWith(CItem *this, CItem *target); // 0x0048A561
int CItem_MergeInto(CItem *source, CItem *target); // 0x0048A63D
int CItem_MergeIntoWrapper(CItem *this, CItem *target); // 0x0048A7B6
CItem *CItem_SplitByAmount(CItem *this, uint16_t amount); // 0x0048A7D4
CItem *CItem_SplitByResources(CItem *this, uint16_t amount); // 0x0048A933
void CCorpse_Constructor(CCorpse *this); // 0x0048AA58
void CCorpse_Destructor(CCorpse *this); // 0x0048AAAE
void CCorpse_SetLookAtText(CItem *corpse, CString *name); // 0x0048AB40
void CCorpse_SetPlayerLookAtText(CItem *corpse, CString *playerName); // 0x0048ACC2
void CItem_ClearScriptsAndTags(CItem *self); // 0x0048B625
void CEntity_SayCString_VT(CItem *self, char *text, int hue, int type, int font); // 0x0048D192
void CItem_SayCUString_VT(CItem *self, uint16_t *text, int hue, int type, int font); // 0x0048D2A5
void CItem_EmoteCString_VT(CItem *self, char *text, int hue, int type, int font); // 0x0048D3BC
void CItem_EmoteCUString_VT(CItem *self, uint16_t *text, int hue, int type, int font); // 0x0048D4CF
void CItem_SayToEntity_VT(CItem *self, CItem *target, uint32_t serial, char *text); // 0x0048D5E6
void CItem_SayToCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text); // 0x0048D60F
void CItem_EmoteToEntity_VT(CItem *self, CItem *target, uint32_t serial, char *text); // 0x0048D638
void CItem_EmoteToCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text); // 0x0048D661
void CItem_SayHuedCString_VT(CItem *self, CItem *target, uint32_t serial, char *text, uint16_t hue); // 0x0048D68A
void CItem_SayHuedCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text, uint16_t hue); // 0x0048D6E0
void CItem_EmoteHuedCString_VT(CItem *self, CItem *target, uint32_t serial, char *text, uint16_t hue); // 0x0048D738
void CItem_EmoteHuedCUString_VT(CItem *self, CItem *target, uint32_t serial, uint16_t *text, uint16_t hue); // 0x0048D78E
int Convo_NotifyNearbyNPCs(uint32_t speakerSerial, CLocation *loc, const char *text, int range); // 0x0048D7E6
void CWorld_SpeechNotifyNearby(CItem *self, uint32_t serial, CLocation *loc, const char *text, int range); // 0x0048D9D7
char *CItem_SpeakSysMsg_VT(CItem *item, int flag); // 0x0048EAEB
int CItem_MoveMulti(CItem *item, CLocation *loc); // 0x0048EB39
int CItem_MoveMultiCheck(CItem *item, CLocation *loc, int checkFlag); // 0x0048EB98
int CItem_MultiAwareDistance(CItem *self, CItem *target); // 0x0048EC7D
void CItem_DecayProcess(CItem *item); // 0x0048F13A
int CItem_IsValueless(CItem *item); // 0x0048F17D
int CItem_CopyObjVar(CItem *dest, CItem *source, const char *tagName, const char *destName); // 0x0048F295
void CItem_AddToAttackerList_VT(CItem *this, CItem *attacker); // 0x0048F9BA
void CEntity_RefreshAggression(CItem *ent); // 0x0048FA3A
void CItem_FameKarmaChange_VT(CItem *self, int fame, int karma); // 0x00490281
int StaticEntity_IsDoor_VT(CItem *item); // 0x00490392
int CItem_IsDoor_VT(CItem *item); // 0x00490400
int CItem_IsDoorNotStackable_VT(CItem *item); // 0x0049045A
int CItem_IsLocked(CItem *item); // 0x004904C5
int CItem_IsHair_VT(CItem *item); // 0x004904E5
int CItem_IsGold(CItem *item); // 0x00490623
int CItem_IsInWorld_VT(CItem *item); // 0x0049064B
int CItem_CanFireEquipEvent(int layer); // 0x00490684
int CItem_TransferTo(CItem *item, CLocation *loc); // 0x004906A6
void CItem_SetLocationDropAtFeet(CItem *item, CLocation *loc); // 0x004906F3
void CMobile_SetLocation_VT(CItem *self, CLocation *loc); // 0x0049070C
int CItem_HasScript(CItem *item, CString *scriptName); // 0x00490780
int CItem_GetSurfaceHeight_VT(CItem *item); // 0x004907C4
void CItem_SetMsgId(CItem *item, uint32_t msgId); // 0x00490801
uint32_t CBulletinBoard_GetMsgId(CItem *post); // 0x00490821
void CItem_SetMsgOwner(CItem *item, uint32_t owner); // 0x0049085C
uint32_t CItem_GetMsgOwner(CItem *item); // 0x0049087C
void CItem_SetMsgDay(CItem *item, uint32_t day); // 0x004908B7
void CItem_InitTemplateChainPtrs(CItem *item); // 0x00490912
void CItem_DetachTemplate(CItem *item); // 0x00490931
void CItem_AttachTemplate(CItem *item, uint16_t templateId); // 0x00490951
void TemplateChain_Insert(CItem *item); // 0x00490973
void TemplateChain_Remove(CItem *item); // 0x004909FB
int CItem_GetWordProp(CItem *item); // 0x00490AB0
uint8_t CItem_GetLayerThiscall(CItem *entity); // 0x00490AD6
uint8_t CWorld_GetItemLayer(uint16_t bodyType); // 0x00490AF4
uint8_t CItem_GetEquipSlot(CItem *item); // 0x00490B24
void CItem_SetBookStatus(CItem *item, int status); // 0x00490B37
int CItem_GetBookStatus(CItem *ent); // 0x00490B64
int CItem_IsWritableBook(CItem *ent); // 0x00490B9F
int CItem_IsRunebook(CItem *item); // 0x00490BD9
void CItem_SetDirectionVT(CItem *item, int dir); // 0x00490C13
void CItem_SetWeight(CItem *item, int weight); // 0x00490C37
int CItem_GetTiledataQuantity(CItem *item); // 0x00490C80
int CItem_GetBookNum(CItem *item); // 0x00490CB3
void CItem_SetBookNum(CItem *item, int num); // 0x00490CEF
int CItem_GetSpellId(CItem *item); // 0x00490DA4
int CItem_GetBookPages(CItem *item); // 0x00490D1C
int GetBookPages(CItem *ent); // 0x00490D1C
void CItem_SetBookPages(CItem *item, int count); // 0x00490D77
int CItem_VT_GetAmount(CItem *item); // 0x00490DCA
void CItem_GetStatusFlags_VT(CItem *self, uint8_t *flags); // 0x00490E09
uint8_t GetStatusFlagsWrapper(CItem *item); // 0x00490E4D
void CItem_ApplyStatusFlags(CItem *item, int flags); // 0x00490E73
int CItem_HasMovableFlag(CItem *item); // 0x00490EBA
void CItem_CheckOverloadedWeight(CItem *item); // 0x00490ECF
int CItem_HasStackableFlag(CItem *item); // 0x00490EF9
void CItem_SetOpen(CItem *item, int setOrClear); // 0x00490F0E
int CItem_GetResourceAmountByName(CItem *self, int resTypeId); // 0x00490F29
int CItem_ValidateInWorldVT(CItem *self, CLocation *loc); // 0x00490F78
int CItem_RegisterLocation(CItem *self, CLocation *loc); // 0x00490FBB
void CItem_ValidateRegistration_VT(CItem *self); // 0x00490FFA
CItem *CItem_FindSelfInWorld_VT(CItem *self); // 0x00491030
void CItem_ConditionalValidate_VT(CItem *self); // 0x00491056
int StaticEntity_GetHeight_VT(CItem *item); // 0x00491078
int CItem_GetHeight_VT(CItem *item); // 0x004910A7
CItem *StaticEntity_ScalarDelete(CItem *self, int flags); // 0x00491130
void *CItem_ScalarDelete(CItem *item, int flags); // 0x00491190
void CEntity_ItemCheck9C_VT(CItem *self, uint16_t *out); // 0x004911C0
char *CEntity_SpeakSysMsg_VT(CItem *self, int flag); // 0x004911F0
CLocation *StaticEntity_GetLocation_VT(CItem *item); // 0x00491210
int StaticEntity_GetFlags_VT(CItem *item); // 0x00491230
int CItem_IsStackable(CItem *item); // 0x00491260
int CItem_GetStoredWeightVT(CItem *item); // 0x00491280
void *CCorpse_ScalarDelete(CCorpse *corpse, int flags); // 0x004912C0
void CItem_ClearTagList(CItem *item); // 0x004CDB46
int CItem_HasScripts(CItem *ent); // 0x004CDB6F
int CItem_HasTagDefs(CItem *ent); // 0x004CDB92
void CItem_GetScriptListRaw(CItem *ent, CVector *list); // 0x004CDBB5
void CItem_GetTagDefListRaw(CItem *ent, CVector *list); // 0x004CDBDA
void CItem_PopulateObjVarList(CItem *item, CVector *list, int type); // 0x004CDC30
TagNode *CItem_GetTagNodeRaw(CItem *item, const char *name); // 0x004CDC59
TagNode *CItem_FindTagByPrefix(CItem *item, const char *prefix); // 0x004CDC82
int CItem_HasLinkedName(CItem *item, const char *name); // 0x004CDCAB
void CItem_AddScript(CItem *this, ScriptAttachNode *script); // 0x004CDE73
int CItem_HasScriptByName(CItem *item, const char *name); // 0x004CDF22
int CItem_CompareScriptClasses(CItem *this, CItem *other); // 0x004CDF74
void CItem_WeightRelated_VT(CItem *self); // 0x004E110F
void EntityManager_EraseEntity(CItem *entity);
void CItem_DeleteContents_Nop(CItem *item);
int CItem_IsContainer(CItem *item);
void CTemplateManager_SpawnVendorStock_DebugBreak(void);
void *CMobile_ScalarDelete(CMobile *mob, int flags);

void Item_DestroyPools(void); // CUSTOM

#endif /* ITEM_H_ */
