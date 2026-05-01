/*
 * CShopkeeper - NPC vendor behaviour.
 *
 * Stock tracking and restock timers, buy / sell price computation,
 * per-item resource-ratio pricing, and the shop list packet served when
 * a player opens a vendor backpack.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "container.h"
#include "egg.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "shopkeeper.h"
#include "taglist.h"
#include "template.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static int CShopkeeper_VendorAdjustment(CMobile *vendor, CItem *item, int baseQuarter); // 0x004D2CF2
static int Shopkeeper_ScanBackpack(CMobile *vendor, CContainer *container, int count, CVendorSellEntry *sellBuf, int maxEntries); // 0x004D1E72
static int CShopkeeper_GetStockTarget(CMobile *vendor, uint16_t bodyType); // 0x004D1CC3
static int CShopkeeper_GetCurrentStock(CMobile *vendor, uint16_t bodyType); // 0x004D1BC8

/*
 * 0x004D0780 - Vendor_GetItemPrice
 *
 * Thunk that returns CItem_GetMinResourceRatio.
 */
int
Vendor_GetItemPrice(CItem *item)
{
	return CItem_GetMinResourceRatio(item);
}

/*
 * 0x004D078D - Vendor_MergeItemIntoMatch
 *
 * Transfers resource nodes from srcItem to dstItem using the
 * g_ResEntitySlots template for the source bodyType. Each resource
 * node's quantity is multiplied by the multiplier arg. Handles
 * lockdown/hide/restore for both items if they are active.
 */
void
Vendor_MergeItemIntoMatch(CItem *srcItem, CItem *dstItem, int multiplier)
{
	ResEntitySlot *slot;
	CResourceNode *node;
	int srcHidden, dstHidden;

	slot = &g_ResEntitySlots[CEntity_GetBodyType(srcItem)];
	srcHidden = 0;
	dstHidden = 0;

	if (srcItem->resourceEntity.entity.removedFromWorld == 0) {
		CItem_SetLockdown(srcItem, 1);
		((void (*)(void *))VT_FN(srcItem, VT_HIDE))(srcItem);
		srcHidden = 1;
	}

	if (dstItem->resourceEntity.entity.removedFromWorld == 0) {
		CItem_SetLockdown(dstItem, 1);
		((void (*)(void *))VT_FN(dstItem, VT_HIDE))(dstItem);
		dstHidden = 1;
	}

	node = slot->nodeHead;
	while (node != NULL) {
		CResourceEntity_TransferResources(dstItem, srcItem, (uint32_t)(node->value1 * multiplier), node->id);
		node = node->next;
	}

	if (srcHidden) {
		((void (*)(void *))VT_FN(srcItem, VT_RETURN_TO_TRACKED))(srcItem);
		CItem_SetLockdown(srcItem, 0);
	}

	if (dstHidden) {
		((void (*)(void *))VT_FN(dstItem, VT_RETURN_TO_TRACKED))(dstItem);
		CItem_SetLockdown(dstItem, 0);
	}
}

/*
 * 0x004D0889 - CShopkeeper::CShopkeeper (no-args ctor)
 *
 * Used when loading shopkeepers from save files. Calls
 * CResourceMobile_Init, clears vendor fields, links into vendor list,
 * increments g_NPCSubCount2.
 */
void
CShopkeeper_ConstructorNoArgs(CNPC *npc)
{
	CMobile *mob = &npc->mobile;

	CResourceMobile_Init(mob);

	CEntity_SetType(&mob->container.item.resourceEntity.entity, ETYPE_SHOPKEEPER);

	npc->restockCounter = 0;
	npc->vendorNext = NULL;
	npc->prevNPC = NULL;

	CShopkeeper_LinkToVendorList(npc);

	g_NPCSubCount2++;
}

/*
 * 0x004D0913 - CShopkeeper::CShopkeeper
 *
 * Constructor for shopkeeper NPCs. Calls CResourceMobile_Constructor,
 * clears vendor fields, links into vendor list, increments g_NPCSubCount2.
 */
void
CShopkeeper_Constructor(CNPC *npc, uint16_t bodyType, CLocation *loc)
{
	CMobile *mob = &npc->mobile;

	CResourceMobile_Constructor(mob, bodyType, loc);

	CEntity_SetType(&mob->container.item.resourceEntity.entity, ETYPE_SHOPKEEPER);

	npc->restockCounter = 0;
	npc->vendorNext = NULL;
	npc->prevNPC = NULL;

	CShopkeeper_LinkToVendorList(npc);

	g_NPCSubCount2++;
}

/*
 * 0x004D09BB - CShopkeeper::UnlinkFromVendorList
 *
 * Unlinks this vendor NPC from the g_VendorListHead doubly-linked list.
 * Zeroes both list pointers after unlink.
 */
void
CShopkeeper_UnlinkFromVendorList(CNPC *npc)
{
	CNPC *prev = npc->prevNPC;
	CNPC *next = npc->vendorNext;

	if (prev != NULL)
		prev->vendorNext = next;

	if (next != NULL) {
		next->prevNPC = prev;
	} else if (g_VendorListHead == npc) {
		g_VendorListHead = prev;
	}

	npc->vendorNext = NULL;
	npc->prevNPC = NULL;
}

/*
 * 0x004D0A43 - CShopkeeper::LinkToVendorList
 *
 * Prepends the vendor NPC to the g_VendorListHead doubly-linked list.
 */
void
CShopkeeper_LinkToVendorList(CNPC *npc)
{
	if (npc->prevNPC != NULL || npc->vendorNext != NULL) {
		CShopkeeper_UnlinkFromVendorList(npc);
	}

	npc->prevNPC = g_VendorListHead;
	if (g_VendorListHead != NULL) {
		g_VendorListHead->vendorNext = npc;
	}
	npc->vendorNext = NULL;
	g_VendorListHead = npc;
}

/*
 * 0x004D0AAF - CShopkeeper::InitContainers
 *
 * Creates 3 vendor containers (equipment[26..28]) with bodyType 0x2AF8.
 * For each empty slot: create container, try VT_EQUIP_ON_MOBILE; if
 * equip fails, VT_DELETE the container.
 */
void
CShopkeeper_InitContainers(CNPC *npc)
{
	CMobile *mob = &npc->mobile;
	CItem *cont;
	int slot;

	for (slot = 26; slot <= 28; slot++) {
		if (mob->equipment[slot] != NULL)
			continue;

		cont = CWorld_CreateContainerItem(g_World, 0x2AF8);
		if (cont == NULL)
			continue;

		if (((int (*)(void *, void *, int))VT_FN(cont, VT_EQUIP_ON_MOBILE))(cont, &mob->container.item, slot) != 0)
			continue;

		if (cont != NULL)
			((void (*)(void *))VT_FN(cont, VT_DELETE))(cont);
	}
}

/*
 * 0x004D0BB3 - CShopkeeper::~CShopkeeper
 *
 * Removes from world grid if needed, clears scripts/tags, inlines the
 * vendor list unlink, decrements g_NPCSubCount2, chains to the
 * CResourceMobile dtor. The inline unlink's else branch unconditionally
 * sets g_VendorListHead = prev, unlike the standalone Unlink function.
 */
void
CShopkeeper_Destructor(CNPC *npc)
{
	CItem *item = &npc->mobile.container.item;
	CNPC *prev;
	CNPC *next;

	if (item->resourceEntity.entity.removedFromWorld == 0)
		CNPC_RemoveFromWorldGrid(item);

	CItem_ClearScriptsAndTags(item);

	prev = npc->prevNPC;
	next = npc->vendorNext;

	if (prev != NULL)
		prev->vendorNext = next;

	if (next != NULL)
		next->prevNPC = prev;
	else
		g_VendorListHead = prev;

	g_NPCSubCount2--;

	CResourceMobile_Destructor(npc);
}

/*
 * 0x004D0C7E - Vendor_GetMarkupPercent
 *
 * Always returns 100. Called from CShopkeeper_OpenBuyWindow and
 * CMobile_ProcessBuyList.
 */
int
Vendor_GetMarkupPercent(CMobile *vendor, CPlayer *player)
{
	USED(vendor);
	USED(player);
	return 100;
}

/*
 * 0x004D0C90 - CShopkeeper::OpenBuyWindow
 *
 * Sends greeting, then CONTAINERCONTENTS + SHOP_DATA for equipment[26]
 * (stock) and equipment[27] (offered), then OPEN_GUMP for vendor gump
 * type 0x30, then MOBILESTAT for player gold display.
 *
 * The stock item loop calls Vendor_GetItemPrice then CItem_SetSortKey
 * (a no-op) - effectively dead code, reproduced for binary fidelity.
 */
void
CShopkeeper_OpenBuyWindow(CPlayer *player, CMobile *vendor)
{
	uint8_t obuf[25012];
	CContainer *stockCont;
	CContainer *offeredCont;
	CItem *item;
	int markupPercent;

	Vendor_SayTo(player, vendor, "Greetings.  Have a look around.");

	markupPercent = Vendor_GetMarkupPercent(vendor, player);

	if (vendor->equipment[26] == NULL)
		goto offered_section;

	if (!((int (*)(void *))VT_FN(vendor->equipment[26], VT_HAS_ACCESSIBLE_CONTENTS))(vendor->equipment[26]))
		goto offered_section;

	stockCont = (CContainer *)vendor->equipment[26];

	// Binary calls Vendor_GetItemPrice then CItem_SetSortKey (a no-op);
	// the loop has no effect but is reproduced for fidelity.
	for (item = stockCont->contents; item != NULL; item = item->spatialNext) {
		CItem_SetSortKey(item, Vendor_GetItemPrice(item));
	}

	CContainer_SendFilteredContents(stockCont, (CItem *)player, player->mobile.container.item.serial, 1);

	PacketManager_MakePacket_SHOP_DATA(obuf, vendor, stockCont, markupPercent, 1);
	SendToClient((CItem *)player, obuf, -1);

offered_section:
	if (vendor->equipment[27] == NULL)
		goto open_gump;

	if (!((int (*)(void *))VT_FN(vendor->equipment[27], VT_IS_MOBILE2))(vendor->equipment[27]))
		goto open_gump;

	offeredCont = (CContainer *)vendor->equipment[27];

	CContainer_SendContainerContents(offeredCont, (CItem *)player, player->mobile.container.item.serial, 1);

	PacketManager_MakePacket_SHOP_DATA(obuf, vendor, offeredCont, markupPercent, 0);
	SendToClient((CItem *)player, obuf, -1);

open_gump:
	SendOpenGump(player, player->mobile.container.item.serial, vendor->container.item.serial, 0x30);

	SendStatusToPlayer(&player->mobile, player, player->mobile.container.item.serial, 1);
}

/*
 * 0x004D0E94 - Vendor_SplitItem
 *
 * Creates a new item of the same bodyType, copies color and tracking
 * info (lastContainer, lastLocation), then consumes the requested amount
 * from source to populate the new item. If source was visible, hides it
 * with lockdown before consuming and restores it after. Returns the
 * newly created item.
 */
CItem *
Vendor_SplitItem(CItem *srcItem, int amount)
{
	CItem *newItem;
	CLocation tmpLoc;
	CLocation *srcLoc;
	int wasHidden;

	newItem = CWorld_CreateItem(g_World, CEntity_GetBodyType(srcItem));

	newItem->resourceEntity.entity.color = srcItem->resourceEntity.entity.color;

	CItem_ReleaseTracking(newItem);
	CItem_SetLastContainer(newItem, srcItem->serial);

	srcLoc = ((CLocation * (*)(void *)) VT_FN(srcItem, VT_GET_LOCATION))(srcItem);
	CLocation_SetLoc(&tmpLoc, srcLoc);
	CItem_SetLastLocation(newItem, (int16_t)tmpLoc.x, (int16_t)tmpLoc.y, tmpLoc.z);

	wasHidden = 0;
	if (srcItem->resourceEntity.entity.removedFromWorld == 0) {
		CItem_SetLockdown(srcItem, 1);
		CItem_UpdateContainInfo(srcItem, 1);
		((void (*)(void *))VT_FN(srcItem, VT_HIDE))(srcItem);
		wasHidden = 1;
	}

	{
		int consumeResult;
		consumeResult = CItem_ConsumeAmount(srcItem, newItem, amount);

		if (consumeResult != 0 && wasHidden != 0) {
			((void (*)(void *))VT_FN(srcItem, VT_RETURN_TO_TRACKED))(srcItem);
			CItem_SetLockdown(srcItem, 0);
		}
	}

	return newItem;
}

/*
 * 0x004D0F7A - CMobile::ProcessBuyList
 *
 * Processes a buy list from a player. Validates items in vendor
 * equipment containers, computes total price, checks player gold
 * (backpack or bank), deducts gold, creates bought items (from
 * template or bodyType), handles pet purchase (myBoss/isPet/myLoyalty
 * ObjVars), transfers items to player backpack or drops at feet,
 * attaches afterShopScript.
 */
void
CMobile_ProcessBuyList(CMobile *this, CPlayer *player, int numItems, BuyEntry *entries)
{
	BuyEntry *ent = entries;
	CItem *item;
	CItem *newItem;
	CItem *backpack;
	CItem *goldItem;
	int totalPrice;
	int markupPercent;
	int playerGold;
	int useBank;
	int i, j;
	int stockAmount;
	int putResult;
	int resRatio;
	CLocation loc;
	char msgbuf[256];
	uint8_t obuf[0x708];

	newItem = NULL;
	item = NULL;
	backpack = NULL;
	totalPrice = 0;

	CLocation_Init(&loc);

	markupPercent = Vendor_GetMarkupPercent(this, player);
	USED(markupPercent);

	for (i = 0; i < numItems; i++) {
		item = CContainer_FindItemBySerial((CContainer *)this->equipment[ent[i].layer & 0xFF], ent[i].serial);

		if (item == NULL) {
			((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
			        this, (CItem *)player, player->mobile.container.item.serial, "Your order cannot be fulfilled, please try again.");
			return;
		}

		if ((ent[i].layer & 0xFF) == 0x1a) {
			stockAmount = Vendor_GetItemPrice(item);
			if (stockAmount < (int)(ent[i].qty & 0xFFFF)) {
				sprintf(msgbuf, "%s is not available in that quantity, please try again.", g_ItemTileData[item->resourceEntity.entity.bodyType & 0xFFFF].name);
				((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
				        this, (CItem *)player, player->mobile.container.item.serial, msgbuf);
				return;
			}
		}

		{
			uint32_t buyQty;
			uint32_t vendorSerial;
			uint32_t playerSerial;
			char *result;

			buyQty = (uint32_t)(ent[i].qty & 0xFFFF);
			vendorSerial = CMobile_GetSerial(this);
			playerSerial = CMobile_GetSerial((CMobile *)player);
			result = Entity_ExecuteEvent((CEntity *)item, CanBuy, playerSerial, vendorSerial, buyQty);
			if (result == NULL)
				return;
		}

		{
			int itemPrice;
			int qty;

			itemPrice = ((int (*)(void *))VT_FN(item, VT_GET_MONETARY_VAL))(item);
			USED(itemPrice);

			qty = (int)(ent[i].qty & 0xFFFF);
			totalPrice += qty * (int)CShopkeeper_GetBuyPrice(this, item);
		}
	}

	if (totalPrice == 0) {
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
		        this, (CItem *)player, player->mobile.container.item.serial, "Thou hast bought nothing!");

		PacketManager_MakePacket_OFFERACCEPT(obuf, this->container.item.serial, 0);
		SendToClient((CItem *)player, obuf, -1);
		return;
	}

	useBank = 0;
	if (totalPrice >= 0x7D0) {
		playerGold = CMobile_GetTotalQuantityOfType((CMobile *)player, 0xEED);
		if (playerGold < totalPrice) {
			useBank = 1;
			playerGold = CMobile_AmountGoldInBank((CMobile *)player);
		}
	} else {
		playerGold = CMobile_GetTotalQuantityOfType((CMobile *)player, 0xEED);
	}

	if (playerGold < totalPrice)
		goto cannot_afford;

	for (j = 25; j >= 0; j--) {
		if (player->mobile.equipment[j] == NULL)
			continue;
		if (((int (*)(void *))VT_FN(player->mobile.equipment[j], VT_HAS_ACCESSIBLE_CONTENTS))(player->mobile.equipment[j]))
			backpack = player->mobile.equipment[j];
	}

	loc.x = 0xFFFF;
	loc.y = 0xFFFF;
	loc.z = 0;

	for (i = 0; i < numItems; i++) {
		item = CContainer_FindItemBySerial((CContainer *)this->equipment[ent[i].layer & 0xFF], ent[i].serial);

		if ((ent[i].layer & 0xFF) == 0x1b) {
			if (((int (*)(void *))VT_FN(item, VT_HAS_RESOURCE_FLAG))(item)) {
				newItem = Vendor_SplitItem(item, ent[i].qty);

				loc.x = 0xFFFF;
				loc.y = 0xFFFF;
				loc.z = 0;

				{
					uint32_t savedSerial = newItem->serial;

					if (backpack != NULL && ((int (*)(void *, CItem *, int))VT_FN(backpack, VT_EQUIP_ITERATE))(backpack, newItem, 0)) {
						((void (*)(void *, CItem *, CLocation *))VT_FN(newItem, VT_ADD_TO_CONTAINER))(newItem, backpack, &loc);
					} else {
						CLocation *playerLoc = CItem_GetLocationVT((CItem *)player);
						((void (*)(void *, CLocation *))VT_FN(newItem, VT_DROP_AT_FEET))(newItem, playerLoc);
					}
					USED(savedSerial);
				}
			} else {
				((void (*)(void *))VT_FN(item, VT_HIDE))(item);

				if (VT_IsMobile(item)) {
					CLocation *playerLoc = &player->mobile.container.item.resourceEntity.entity.location;
					((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, playerLoc);

					{
						struct TagNode *ov;
						CList *list;

						ov = CEntity_SetObjVar(item, "myBoss", 5, 0);
						list = (CList *)(uintptr_t)ov->value;
						if (list != NULL)
							CList_Append(list, 4, player->mobile.container.item.serial);
					}

					{
						CString name;
						CString_Constructor(&name, "myLoyalty");
						ObjVar_SetStr(item, &name, 0, 100);
					}

					{
						CString name;
						CString_Constructor(&name, "isPet");
						ObjVar_SetStr(item, &name, 0, 1);
					}
				} else {
					loc.x = 0xFFFF;
					loc.y = 0xFFFF;
					loc.z = 0;

					if (backpack != NULL && ((int (*)(void *, CItem *, int))VT_FN(backpack, VT_EQUIP_ITERATE))(backpack, item, 0)) {
						((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, backpack, &loc);
					} else {
						CLocation *playerLoc = CItem_GetLocationVT((CItem *)player);
						((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, playerLoc);
					}
				}
			}
			continue;
		}

		if (((int (*)(void *))VT_FN(item, VT_HAS_RESOURCE_FLAG))(item)) {
			((int (*)(void *))VT_FN(item, VT_GET_FLAGS))(item);

			newItem = CItem_SplitByResources(item, ent[i].qty);

			loc.x = 0xFFFF;
			loc.y = 0xFFFF;
			loc.z = 0;

			{
				uint32_t savedSerial = newItem->serial;

				if (backpack != NULL && ((int (*)(void *, CItem *, int))VT_FN(backpack, VT_EQUIP_ITERATE))(backpack, newItem, 0)) {
					((void (*)(void *, CItem *, CLocation *))VT_FN(newItem, VT_ADD_TO_CONTAINER))(newItem, backpack, &loc);
				} else {
					CLocation *playerLoc = CItem_GetLocationVT((CItem *)player);
					((void (*)(void *, CLocation *))VT_FN(newItem, VT_DROP_AT_FEET))(newItem, playerLoc);
				}

				if (CWorld_FindBySerial(g_World, savedSerial) != newItem) {
					newItem = NULL;
				} else if (!ValidateInWorld(newItem)) {
					newItem = NULL;
				}

				if (newItem != NULL) {
					CString scriptStr;
					CString_DefaultConstructor(&scriptStr);
					if (CItem_GetAfterShopScript(item, &scriptStr))
						Entity_AttachScript(newItem, CString_GetBuffer(&scriptStr), 1);
					CString_Destructor(&scriptStr);
				}
			}
		} else {
			int qty;
			qty = (int)(ent[i].qty & 0xFFFF);
			for (j = 0; j < qty; j++) {
				uint16_t templateIdx;

				templateIdx = (uint16_t)CResourceEntity_GetTemplateIndex(item);
				if ((templateIdx & 0xFFFF) != 0xFFFF) {
					CLocation *playerLoc = &player->mobile.container.item.resourceEntity.entity.location;
					newItem = CTemplateManager_CreateFromTemplate(templateIdx & 0xFFFF, playerLoc, 0, 0, NULL);
					if (newItem == NULL)
						continue;
				} else {
					newItem = CWorld_CreateItem(g_World, item->resourceEntity.entity.bodyType);
				}

				if (VT_IsNPC(newItem)) {
					if (newItem->resourceEntity.entity.removedFromWorld != 0) {
						CLocation *pLoc = &player->mobile.container.item.resourceEntity.entity.location;
						((void (*)(void *, CLocation *))VT_FN(newItem, VT_DROP_AT_FEET))(newItem, pLoc);
					}

					{
						struct TagNode *ov;
						CList *list;

						ov = CEntity_SetObjVar(newItem, "myBoss", 5, 0);
						list = (CList *)(uintptr_t)ov->value;
						if (list != NULL)
							CList_Append(list, 4, player->mobile.container.item.serial);
					}

					{
						CString name;
						CString_Constructor(&name, "myLoyalty");
						ObjVar_SetStr(newItem, &name, 0, 100);
					}

					{
						CString name;
						CString_Constructor(&name, "isPet");
						ObjVar_SetStr(newItem, &name, 0, 1);
					}
				} else {
					newItem->resourceEntity.entity.color = item->resourceEntity.entity.color;

					if (newItem->resourceEntity.entity.removedFromWorld == 0)
						((void (*)(void *))VT_FN(newItem, VT_HIDE))(newItem);

					Vendor_MergeItemIntoMatch(item, newItem, 1);

					loc.x = 0xFFFF;
					loc.y = 0xFFFF;
					loc.z = 0;

					if (backpack != NULL && ((int (*)(void *, CItem *, int))VT_FN(backpack, VT_EQUIP_ITERATE))(backpack, newItem, 0)) {
						((void (*)(void *, CItem *, CLocation *))VT_FN(newItem, VT_ADD_TO_CONTAINER))(newItem, backpack, &loc);
					} else {
						CLocation *pLoc = CItem_GetLocationVT((CItem *)player);
						((void (*)(void *, CLocation *))VT_FN(newItem, VT_DROP_AT_FEET))(newItem, pLoc);
					}
				}

				{
					CString scriptStr;
					CString_DefaultConstructor(&scriptStr);
					if (CItem_GetAfterShopScript(item, &scriptStr))
						Entity_AttachScript(newItem, CString_GetBuffer(&scriptStr), 1);
					CString_Destructor(&scriptStr);
				}

				if (!ValidateInWorld(newItem))
					newItem = NULL;
			}
		}
	}

	if (playerGold < totalPrice)
		goto cannot_afford;

	if (useBank) {
		goldItem = CMobile_SubtractGold((CMobile *)player, totalPrice);
	} else {
		goldItem = CMobile_FindItemInEquipment((CMobile *)player, 0xEED, totalPrice);
	}

	putResult = -1;
	resRatio = 0;
	if (goldItem != NULL)
		CItem_GetMinResourceRatio(goldItem);
	resRatio = CItem_GetMinResourceRatio(goldItem);
	putResult = CMobile_PutMoneyInBank(this, goldItem, totalPrice);

	// CMobile_PutMoneyInBank returns -1 on success; anything else is error.
	if (putResult != -1) {
		sprintf(msgbuf, "An internal error has occured (%d,%d,%d).", resRatio, totalPrice, putResult);
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(this, (CItem *)player, player->mobile.container.item.serial, msgbuf);
		return;
	}

	if (useBank) {
		sprintf(msgbuf, "The total of thy purchase is %d gold, which has been drawn from your bank account.  My thanks for the patronage.", totalPrice);
	} else {
		sprintf(msgbuf, "The total of thy purchase is %d gold.  My thanks for the patronage.", totalPrice);
	}

	((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(this, (CItem *)player, player->mobile.container.item.serial, msgbuf);

	if (totalPrice <= 1) {
		SendSoundToEntity((CItem *)player, 0x35, 0);
	} else if (totalPrice <= 5) {
		SendSoundToEntity((CItem *)player, 0x36, 0);
	} else {
		SendSoundToEntity((CItem *)player, 0x37, 0);
	}

	PacketManager_MakePacket_OFFERACCEPT(obuf, this->container.item.serial, 0);
	SendToClient((CItem *)player, obuf, -1);
	return;

cannot_afford:
	if (useBank) {
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
		        this, (CItem *)player, player->mobile.container.item.serial, "Begging thy pardon, but thy bank account lacks these funds.");
	} else {
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
		        this, (CItem *)player, player->mobile.container.item.serial, "Begging thy pardon, but thou canst not afford that.");
	}
}

/*
 * 0x004D1BC8 - CShopkeeper::GetCurrentStock
 *
 * Iterates equipment[26] (stock) children matching bodyType, summing
 * CItem_GetMinResourceRatio for each. Then iterates equipment[27]
 * (offered) children, summing VT_GET_ITEM_AMOUNT for each.
 */
static int
CShopkeeper_GetCurrentStock(CMobile *vendor, uint16_t bodyType)
{
	CItem *cont, *item;
	int total = 0;

	cont = vendor->equipment[26];
	if (cont != NULL) {
		if (VT_IsMobile2(cont)) {
			for (item = ((CContainer *)cont)->contents; item != NULL; item = item->spatialNext) {
				if ((CEntity_GetBodyType(item) & 0xFFFF) == bodyType)
					total += CItem_GetMinResourceRatio(item);
			}
		}
	}

	cont = vendor->equipment[27];
	if (cont != NULL) {
		if (VT_IsMobile2(cont)) {
			for (item = ((CContainer *)cont)->contents; item != NULL; item = item->spatialNext) {
				if ((CEntity_GetBodyType(item) & 0xFFFF) == bodyType)
					total += ((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);
			}
		}
	}
	return total;
}

/*
 * 0x004D1CC3 - CShopkeeper::GetStockTarget
 *
 * Iterates equipment[26] (stock) children matching bodyType, reads the
 * "stockTarget" tag. Returns stockTarget for the first matching item,
 * 0 if none found.
 */
static int
CShopkeeper_GetStockTarget(CMobile *vendor, uint16_t bodyType)
{
	CItem *stockCont, *item;
	int result = 0;

	stockCont = vendor->equipment[26];
	if (stockCont == NULL)
		return 0;
	if (!VT_IsMobile2(stockCont))
		return 0;

	for (item = ((CContainer *)stockCont)->contents; item != NULL; item = item->spatialNext) {
		if ((item->resourceEntity.entity.bodyType & 0xFFFF) != bodyType)
			continue;
		if (!CResourceEntity_HasTag(item, "stockTarget", 0))
			continue;
		CResourceEntity_GetTagInt(item, "stockTarget", &result);
		return result;
	}
	return result;
}

/*
 * 0x004D1D63 - CShopkeeper::GetBuyPrice
 *
 * Computes the vendor buy price:
 *   basePriceAdj = max(baseVal + VendorAdjustment(vendor, item, baseVal/4), baseVal)
 *   surplus = CurrentStock - StockTarget
 *   markupPct = clamp((-surplus)/StockTarget * 100, 0, 100)   (0 if StockTarget <= 0)
 *   price = max(basePriceAdj + basePriceAdj * markupPct / 100, basePriceAdj, 1)
 *   if price > price*2: price *= 2   (overflow guard)
 */
uint32_t
CShopkeeper_GetBuyPrice(CMobile *vendor, CItem *item)
{
	uint16_t bodyType;
	int baseVal, quarterVal;
	int basePriceAdj;
	int stockTarget, actualStock, surplus;
	float markupFloat;
	int markupPct;
	int price;
	int doubled;

	baseVal = ((int (*)(void *))VT_FN(item, VT_GET_MONETARY_VAL))(item);
	quarterVal = baseVal / 4;

	basePriceAdj = baseVal + CShopkeeper_VendorAdjustment(vendor, item, quarterVal);
	if (basePriceAdj < baseVal)
		basePriceAdj = baseVal;

	price = basePriceAdj;

	bodyType = (item->resourceEntity.entity.bodyType & 0xFFFF);
	stockTarget = CShopkeeper_GetStockTarget(vendor, bodyType);
	actualStock = CShopkeeper_GetCurrentStock(vendor, bodyType);
	surplus = actualStock - stockTarget;

	markupFloat = 0.0f;
	if (stockTarget > 0) {
		markupFloat = (float)(0 - surplus) / (float)stockTarget * 100.0f;
	}
	markupPct = (int)markupFloat;
	if (markupPct > 100)
		markupPct = 100;

	price = price + price * markupPct / 100;

	if (price < basePriceAdj)
		price = basePriceAdj;
	if (price < 1)
		price = 1;

	// Overflow guard
	doubled = price << 1;
	if (price > doubled)
		price = price << 1;

	return (uint32_t)price;
}

/*
 * 0x004D1E72 - Shopkeeper_ScanBackpack
 *
 * Recursive scanner that builds a pre-formatted sell buffer of
 * CVendorSellEntry records (serial, bodyType, color, amount,
 * price = GetBuyPrice >> 1, malloc'd name copy).
 *
 * For each child, if it has accessible contents and is not excluded
 * by amount, recurses. Then falls through to CShopkeeper_mobileWillBuy
 * on every item (including containers just recursed into).
 */
static int
Shopkeeper_ScanBackpack(CMobile *vendor, CContainer *container, int count, CVendorSellEntry *sellBuf, int maxEntries)
{
	CItem *item;

	for (item = container->contents; item != NULL; item = item->spatialNext) {
		if (((int (*)(void *))VT_FN(item, VT_HAS_ACCESSIBLE_CONTENTS))(item) && !((int (*)(void *))VT_FN(item, VT_EXCLUDED_AMOUNT))(item)) {
			count = Shopkeeper_ScanBackpack(vendor, (CContainer *)item, count, sellBuf, maxEntries);
			if (count >= maxEntries)
				return count;
		}

		if (!CShopkeeper_mobileWillBuy(vendor, item))
			continue;

		{
			CVendorSellEntry *entry = &sellBuf[count];
			uint16_t bodyType;
			const char *name;
			char *nameCopy;
			int nameLen;

			entry->serial = item->serial;

			bodyType = CEntity_GetBodyType(item);
			entry->bodyType = bodyType;

			entry->color = item->resourceEntity.entity.color;

			entry->amount = (uint16_t)((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);

			// Sell price is half the buy price.
			entry->price = (uint16_t)((int)CShopkeeper_GetBuyPrice(vendor, item) >> 1);

			name = ((const char *(*)(void *, int))VT_FN(item, VT_SPEAK_SYS_MSG))(item, 0);
			nameLen = strlen(name) + 1;
			nameCopy = (char *)OperatorNew(nameLen);
			strcpy(nameCopy, name);
			entry->name = nameCopy;
		}
		count++;
		if (count >= maxEntries)
			return count;
	}
	return count;
}

/*
 * 0x004D1FF1 - CShopkeeper::OpenSellWindow
 *
 * Scans the player's backpack for items the vendor will buy, builds
 * the SHOP_SELL packet, and sends it. If no items match, uses
 * g_VendorSawMatch to decide between "can't afford" vs "nothing I want".
 */
void
CShopkeeper_OpenSellWindow(CPlayer *player, CMobile *vendor)
{
	uint8_t obuf[0x10000];
	CVendorSellEntry *sellBuf;
	int count;

	count = 0;

	sellBuf = (CVendorSellEntry *)malloc(250 * sizeof(CVendorSellEntry));

	g_VendorSawMatch = 0;

	if (player->mobile.equipment[21] == NULL)
		goto check_count;

	if (!((int (*)(void *))VT_FN(player->mobile.equipment[21], VT_HAS_ACCESSIBLE_CONTENTS))(player->mobile.equipment[21]))
		goto check_count;

	count = Shopkeeper_ScanBackpack(vendor, (CContainer *)player->mobile.equipment[21], 0, sellBuf, 250);

check_count:
	if (count != 0) {
		PacketManager_MakePacket_SHOP_SELL(obuf, vendor, (uint16_t)count, sellBuf);
		SendToClient((CItem *)player, obuf, -1);
	} else if (g_VendorSawMatch) {
		Vendor_SayTo(player, vendor, "I can't afford any more of that right now.  You might try other adventurers.");
	} else {
		Vendor_SayTo(player, vendor, "You have nothing I would be interested in.");
	}

	free(sellBuf);
}

/*
 * 0x004D211F - Vendor_MergeIntoStock
 *
 * Tries to merge an item into the vendor's stock container by finding
 * a matching child (same bodyType, same color - compiler artifact
 * self-compare, same script classes). If found, merges via
 * Vendor_MergeItemIntoMatch and deletes the source. Returns 1 if
 * merged, 0 otherwise.
 */
int
Vendor_MergeIntoStock(CItem *item, CContainer *stockCont)
{
	int found;
	int itemPrice;
	CItem *child;

	found = 0;

	itemPrice = ((int (*)(void *, int, int))VT_FN(item, VT_GET_VALUE))(item, 0, 1);
	USED(itemPrice);

	child = stockCont->contents;
	while (child != NULL) {
		uint16_t childBody, itemBody;

		childBody = CEntity_GetBodyType(child);
		itemBody = CEntity_GetBodyType(item);
		if ((childBody & 0xFFFF) == (itemBody & 0xFFFF)) {
			// Self-compare of child->color is a compiler artifact
			// in the binary; always true. Preserved for fidelity.
			uint16_t c1, c2;
			c1 = child->resourceEntity.entity.color;
			c2 = child->resourceEntity.entity.color;
			if (c1 == c2) {
				if (CItem_CompareScriptClasses(item, child)) {
					found = 1;
					break;
				}
			}
		}
		child = child->spatialNext;
	}

	if (found) {
		Vendor_MergeItemIntoMatch(item, child, 1);

		if (item != NULL)
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);

		return 1;
	}

	return 0;
}

/*
 * 0x004D21E6 - CMobile::ProcessSellOffer
 *
 * Processes a sell offer from a player. Validates items, computes
 * prices, transfers gold, fires shop events.
 */
void
CMobile_ProcessSellOffer(CMobile *this, CPlayer *player, int itemCount, SellEntry *entries)
{
	CContainer *backpack;
	uint16_t totalPrice;
	int totalItems;
	int i;
	CItem *item;
	int itemAmount;
	int16_t sellPrice;
	CItem *goldItem;
	uint32_t savedGoldSerial;
	CLocation loc;
	char msgbuf[600];
	uint8_t obuf[0x708];
	SellEntry *ent = entries;

	CLocation_Init(&loc);

	backpack = (CContainer *)player->mobile.equipment[21];
	if (backpack == NULL)
		return;

	totalPrice = 0;
	totalItems = 0;
	for (i = 0; i < itemCount; i++)
		totalItems += (int)(ent[i].amount & 0xFFFF);

	if (totalItems > 5) {
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
		        this, (CItem *)player, player->mobile.container.item.serial, "You can only sell 5 items at a time!");
		return;
	}

	for (i = 0; i < itemCount; i++) {
		item = CContainer_FindItemBySerial(backpack, ent[i].serial);
		if (item == NULL) {
			((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
			        this, (CItem *)player, player->mobile.container.item.serial, "Unable to complete your request.");
			return;
		}

		if (VT_IsMobile2(item)) {
			if (((CContainer *)item)->contents != NULL) {
				((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
				        this, (CItem *)player, player->mobile.container.item.serial, "You may not sell containers unless they are empty.");
				return;
			}
		}

		itemAmount = ((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);

		if ((int)(ent[i].amount & 0xFFFF) > itemAmount) {
			CString str;
			const char *itemName;

			CString_Constructor(&str, "You do not have that many ");
			itemName = ((const char *(*)(void *, int))VT_FN(item, VT_SPEAK_SYS_MSG))(item, 1);
			CString_AppendCStr(&str, itemName);
			CString_AppendCStr(&str, ".");
			((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
			        this, (CItem *)player, player->mobile.container.item.serial, CString_GetBuffer(&str));
			CString_Destructor(&str);
			return;
		}

		sellPrice = (int16_t)(CShopkeeper_GetBuyPrice(this, item) >> 1);

		totalPrice += (uint16_t)((ent[i].amount & 0xFFFF) * (sellPrice & 0xFFFF));
	}

	if ((totalPrice & 0xFFFF) == 0) {
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
		        this, (CItem *)player, player->mobile.container.item.serial, "Thou hast offered nothing!");
		return;
	}

	goldItem = NULL;
	if ((int)(totalPrice & 0xFFFF) > CMobile_AmountGoldInBank(this)) {
		sprintf(msgbuf, "Sorry, I cannot afford but %d gold at this time.", CMobile_AmountGoldInBank(this));
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(this, (CItem *)player, player->mobile.container.item.serial, msgbuf);
		return;
	}

	// Binary artifact: re-lookup all items (result discarded).
	for (i = 0; i < itemCount; i++)
		item = CContainer_FindItemBySerial(backpack, ent[i].serial);

	goldItem = CMobile_SubtractGold(this, (int)(totalPrice & 0xFFFF));
	if (goldItem == NULL) {
		((void (*)(void *, CItem *, uint32_t, const char *))VT_FN((CItem *)this, VT_SAY_TO_ENTITY))(
		        this, (CItem *)player, player->mobile.container.item.serial, "Unable to complete your request.");
		return;
	}

	loc.x = 0xFFFF;
	loc.y = 0xFFFF;
	savedGoldSerial = goldItem->serial;
	((void (*)(void *, CItem *, CLocation *))VT_FN(goldItem, VT_ADD_TO_CONTAINER))(goldItem, (CItem *)backpack, &loc);

	if (CWorld_FindBySerial(g_World, savedGoldSerial) == (CItem *)goldItem) {
		if (!ValidateInWorld(goldItem))
			goldItem = NULL;
	}

	if ((int)(totalPrice & 0xFFFF) <= 1) {
		SendSoundToEntity((CItem *)player, 0x35, 0);
	} else if ((int)(totalPrice & 0xFFFF) <= 5) {
		SendSoundToEntity((CItem *)player, 0x36, 0);
	} else {
		SendSoundToEntity((CItem *)player, 0x37, 0);
	}

	for (i = 0; i < itemCount; i++) {
		item = CContainer_FindItemBySerial(backpack, ent[i].serial);

		Entity_ExecuteEvent((CEntity *)item, 0x3B, 1);

		if (CWorld_FindBySerial(g_World, ent[i].serial) != item)
			continue;

		itemAmount = ((int (*)(void *))VT_FN(item, VT_GET_ITEM_AMOUNT))(item);

		if ((int)(ent[i].amount & 0xFFFF) == itemAmount) {
			if (item->resourceEntity.entity.removedFromWorld == 0)
				((void (*)(void *))VT_FN(item, VT_HIDE))(item);

			if (!Vendor_MergeIntoStock(item, (CContainer *)this->equipment[26])) {
				loc.x = 0xFFFF;
				loc.y = 0xFFFF;
				((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, this->equipment[27], &loc);
			}
		} else {
			CItem *splitItem;
			splitItem = CItem_SplitByAmount(item, ent[i].amount);

			if (!Vendor_MergeIntoStock(splitItem, (CContainer *)this->equipment[26])) {
				loc.x = 0xFFFF;
				loc.y = 0xFFFF;
				((void (*)(void *, CItem *, CLocation *))VT_FN(splitItem, VT_ADD_TO_CONTAINER))(splitItem, this->equipment[27], &loc);
			}
		}
	}

	PacketManager_MakePacket_OFFERACCEPT(obuf, this->container.item.serial, 0);
	SendToClient((CItem *)player, obuf, -1);
}

/*
 * 0x004D27EE - CShopkeeper::mobileWillBuy
 *
 * Returns 1 if vendor will buy this item, 0 otherwise. Requires
 * baseVal > 0, non-empty-container, not isLocked (containers only),
 * a matching bodyType in equipment[28] (the buy-list). On match,
 * sets g_VendorSawMatch, enforces stock cap (target*1000/buyPrice,
 * min 10) and vendor gold affordability.
 */
int
CShopkeeper_mobileWillBuy(CMobile *vendor, CItem *item)
{
	int baseVal;
	int buyPrice;
	int isLockedVal;
	int vendorGold;
	CContainer *buyContainer;
	CItem *buyTemplate;
	int currentStock;
	int maxStock;

	baseVal = ((int (*)(void *))VT_FN(item, VT_GET_MONETARY_VAL))(item);

	buyPrice = (int)CShopkeeper_GetBuyPrice(vendor, item);

	if (baseVal <= 0)
		return 0;

	if (VT_IsMobile2(item) && ((CContainer *)item)->contents != NULL)
		return 0;

	isLockedVal = 0;
	if (VT_IsMobile2(item)) {
		if (CItem_GetTagInt(item, "isLocked", &isLockedVal))
			return 0;
	}

	vendorGold = CMobile_AmountGoldInBank(vendor);

	buyContainer = (CContainer *)vendor->equipment[28];

	for (buyTemplate = buyContainer->contents; buyTemplate != NULL; buyTemplate = buyTemplate->spatialNext) {
		if (buyTemplate->resourceEntity.entity.bodyType != item->resourceEntity.entity.bodyType)
			continue;

		g_VendorSawMatch = 1;

		buyPrice = (int)CShopkeeper_GetBuyPrice(vendor, item);
		if (buyPrice < 1)
			buyPrice = 1;

		currentStock = CShopkeeper_GetCurrentStock(vendor, item->resourceEntity.entity.bodyType);
		maxStock = CShopkeeper_GetStockTarget(vendor, item->resourceEntity.entity.bodyType) * 1000 / buyPrice;
		if (maxStock < 10)
			maxStock = 10;
		if (currentStock >= maxStock)
			return 0;

		if ((int)CShopkeeper_GetBuyPrice(vendor, item) > vendorGold)
			return 0;

		return 1;
	}

	return 0;
}

/*
 * 0x004D2982 - CShopkeeper::SellItemFromPlayer
 *
 * Handles a player dropping an item on a vendor to sell it. If item
 * has a "home" tag, returns it home. Otherwise checks if vendor will
 * buy and computes price; if vendor can't afford, rejects. On accept,
 * subtracts vendor gold, moves item to equipment[27], moves gold to
 * the player's container, plays coin sound, computes sold-items fee.
 */
void
CShopkeeper_SellItemFromPlayer(CNPC *npc, CItem *player, CItem *item)
{
	CMobile *vendor = &npc->mobile;
	CLocation loc;
	int templateFound;
	uint16_t price;
	CItem *goldItem;
	CItem *foundContainer;
	int slot;
	char buf[512];

	CLocation_Init(&loc);
	templateFound = 0;
	price = 0;

	if (CItem_HasHome(item)) {
		((void (*)(void *, void *, uint32_t, const char *))VT_FN((CItem *)vendor, VT_SAY_TO_ENTITY))(vendor, player, player->serial, "Ahh!  We've been looking for that!");
		CItem_ReturnToHome(item);
		return;
	}

	templateFound = CShopkeeper_mobileWillBuy(vendor, item);
	if (templateFound)
		price = (uint16_t)CShopkeeper_GetBuyPrice(vendor, item);

	if (templateFound == 0 || price <= 0) {
		((void (*)(void *, void *, uint32_t, const char *))VT_FN((CItem *)vendor, VT_SAY_TO_ENTITY))(vendor, player, player->serial, "I have no need for that.");
		if (item->resourceEntity.entity.removedFromWorld == 0)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		((void (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item);
		return;
	}

	if ((int)(price & 0xFFFF) > CMobile_AmountGoldInBank(vendor)) {
		((void (*)(void *, void *, uint32_t, const char *))VT_FN((CItem *)vendor, VT_SAY_TO_ENTITY))(vendor, player, player->serial,
		        "I do not need that item.  Perhaps you could try "
		        "another shopkeeper.");
		if (item->resourceEntity.entity.removedFromWorld == 0)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		((void (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item);
		return;
	}

	sprintf(buf, "I will give thee %d gold for that.  Here it is.", (int)(price & 0xFFFF));
	((void (*)(void *, void *, uint32_t, const char *))VT_FN((CItem *)vendor, VT_SAY_TO_ENTITY))(vendor, player, player->serial, buf);

	goldItem = CMobile_SubtractGold(vendor, (int)(price & 0xFFFF));

	if (item->resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	loc.x = (uint16_t)0xFFFF;
	loc.y = (uint16_t)0xFFFF;
	((void (*)(void *, void *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, vendor->equipment[27], &loc);

	if (goldItem == NULL) {
		if (item->resourceEntity.entity.removedFromWorld == 0)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		((void (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item);
		return;
	}

	foundContainer = NULL;
	for (slot = 25; slot >= 0; slot--) {
		if (((CMobile *)player)->equipment[slot] == NULL)
			continue;
		if (!((int (*)(void *))VT_FN(((CMobile *)player)->equipment[slot], VT_HAS_ACCESSIBLE_CONTENTS))(((CMobile *)player)->equipment[slot]))
			continue;
		foundContainer = ((CMobile *)player)->equipment[slot];
	}

	if (foundContainer != NULL) {
		loc.x = (uint16_t)0xFFFF;
		loc.y = (uint16_t)0xFFFF;
		((void (*)(void *, void *, CLocation *))VT_FN(goldItem, VT_ADD_TO_CONTAINER))(goldItem, foundContainer, &loc);
	} else {
		((void (*)(void *, CLocation *))VT_FN(goldItem, VT_DROP_AT_FEET))(goldItem, &((CItem *)player)->resourceEntity.entity.location);
	}

	if (!ValidateInWorld(goldItem))
		goldItem = NULL;

	if ((int)(price & 0xFFFF) <= 1)
		SendSoundToEntity(player, 0x35, 0);
	else if ((int)(price & 0xFFFF) <= 5)
		SendSoundToEntity(player, 0x36, 0);
	else
		SendSoundToEntity(player, 0x37, 0);

	VendorStock_ComputeSoldItemsFee(vendor);
}

/*
 * 0x004D2CF2 - CShopkeeper::VendorAdjustment
 *
 * Looks up the vendor's location in the resource bank region. If no
 * region is found, returns baseQuarter unchanged. Otherwise iterates
 * g_VendorListHead to sum competing vendors' stock of the same
 * bodyType within the region (caching the result in
 * region->competitionStockCache[bodyType]) and computes:
 *   myStock = GetCurrentStock(vendor, bodyType) + 1
 *   ratio   = myStock / (myStock + competitionStock) + 1.0
 *   return (int)(baseQuarter * ratio)
 */
static int
CShopkeeper_VendorAdjustment(CMobile *vendor, CItem *item, int baseQuarter)
{
	uint16_t bodyType;
	CResBankRegion *region;
	int competitionStock;
	CNPC *npc;
	int myStock;
	int totalStock;
	float ratio;

	bodyType = CEntity_GetBodyType(item) & 0xFFFF;

	region = CResBankManager_GetRegionByLocation(vendor->container.item.resourceEntity.entity.location.x, vendor->container.item.resourceEntity.entity.location.y);

	if (region == NULL)
		return baseQuarter;

	competitionStock = 0;

	if (region->needsRebuild == 0) {
		competitionStock = region->competitionStockCache[bodyType];
	} else {
		region->needsRebuild = 0;
		for (npc = g_VendorListHead; npc != NULL; npc = npc->prevNPC) {
			if ((CMobile *)npc == vendor)
				continue;
			if (npc->mobile.container.item.resourceEntity.entity.removedFromWorld)
				continue;
			if ((int)(int16_t)npc->mobile.container.item.resourceEntity.entity.location.x < region->x1)
				continue;
			if ((int)(int16_t)npc->mobile.container.item.resourceEntity.entity.location.x >= region->x2)
				continue;
			if ((int)(int16_t)npc->mobile.container.item.resourceEntity.entity.location.y < region->y1)
				continue;
			if ((int)(int16_t)npc->mobile.container.item.resourceEntity.entity.location.y >= region->y2)
				continue;
			competitionStock += CShopkeeper_GetCurrentStock((CMobile *)npc, bodyType);
		}
		region->competitionStockCache[bodyType] = competitionStock;
	}

	myStock = CShopkeeper_GetCurrentStock(vendor, bodyType) + 1;
	totalStock = myStock + competitionStock;
	ratio = (float)myStock / (float)totalStock + 1.0f;
	return (int)((float)baseQuarter * ratio);
}

/*
 * 0x004D2E6F - CMobile::VendorRestockTick
 *
 * Resets restockCounter to 200. If the vendor has any gold in bank,
 * computes the upkeep fee, subtracts it, and deletes the returned
 * gold item.
 */
void
CMobile_VendorRestockTick(CMobile *this)
{
	int gold;
	int fee;
	CItem *goldItem;

	((CNPC *)this)->restockCounter = 200;

	gold = CMobile_AmountGoldInBank(this);
	if (gold <= 0)
		return;

	fee = CMobile_ComputeVendorFee(this, gold);
	goldItem = CMobile_SubtractGold(this, fee);
	if (goldItem != NULL)
		((void (*)(void *))VT_FN(goldItem, VT_DELETE))(goldItem);
}

/*
 * 0x004D2ECC - CShopkeeper::CheckVendorHasStock
 *
 * Returns 1 if the vendor's stock container (equipment[26]) has any
 * item with amount > 0, 0 otherwise.
 */
int
CShopkeeper_CheckVendorHasStock(CMobile *vendor)
{
	CItem *cur;

	if (vendor->equipment[26] == NULL)
		return 0;

	if (!VT_IsMobile2(vendor->equipment[26]))
		return 0;

	cur = ((CContainer *)vendor->equipment[26])->contents;
	while (cur != NULL) {
		if (((int (*)(CItem *))VT_FN(cur, VT_GET_ITEM_AMOUNT))(cur) > 0)
			return 1;
		cur = cur->spatialNext;
	}

	return 0;
}

/*
 * 0x004D2F50 - CShopkeeper scalar deleting destructor
 *
 * Runs the destructor body and frees the object when flags & 1.
 */
void *
CShopkeeper_ScalarDelete(CNPC *npc, int flags)
{
	CShopkeeper_Destructor(npc);
	if (flags & 1)
		free(npc);
	return NULL;
}
