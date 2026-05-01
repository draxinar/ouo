#ifndef SHOPKEEPER_H_
#define SHOPKEEPER_H_

#include <stdint.h>

__extension__ typedef struct CContainer CContainer;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CNPC CNPC;
__extension__ typedef struct CPlayer CPlayer;

// Binary: 0xC-byte packet structs for vendor buy/sell entries.
typedef struct {
	uint8_t layer;
	uint8_t _pad1[3];
	uint32_t serial;
	uint16_t qty;
	uint16_t _pad2;
} BuyEntry;

typedef struct {
	uint32_t _pad;
	uint32_t serial;
	uint16_t amount;
	uint16_t _pad2;
} SellEntry;

int Vendor_GetItemPrice(CItem *item); // 0x004D0780
void Vendor_MergeItemIntoMatch(CItem *srcItem, CItem *dstItem, int multiplier); // 0x004D078D
void CShopkeeper_ConstructorNoArgs(CNPC *npc); // 0x004D0889
void CShopkeeper_Constructor(CNPC *npc, uint16_t bodyType, CLocation *loc); // 0x004D0913
void CShopkeeper_UnlinkFromVendorList(CNPC *npc); // 0x004D09BB
void CShopkeeper_LinkToVendorList(CNPC *npc); // 0x004D0A43
void CShopkeeper_InitContainers(CNPC *npc); // 0x004D0AAF
void CShopkeeper_Destructor(CNPC *npc); // 0x004D0BB3
int Vendor_GetMarkupPercent(CMobile *vendor, CPlayer *player); // 0x004D0C7E
void CShopkeeper_OpenBuyWindow(CPlayer *player, CMobile *vendor); // 0x004D0C90
CItem *Vendor_SplitItem(CItem *srcItem, int amount); // 0x004D0E94
void CMobile_ProcessBuyList(CMobile *this, CPlayer *player, int numItems, BuyEntry *entries); // 0x004D0F7A
uint32_t CShopkeeper_GetBuyPrice(CMobile *vendor, CItem *item); // 0x004D1D63
void CShopkeeper_OpenSellWindow(CPlayer *player, CMobile *vendor); // 0x004D1FF1
int Vendor_MergeIntoStock(CItem *item, CContainer *stockCont); // 0x004D211F
void CMobile_ProcessSellOffer(CMobile *this, CPlayer *player, int itemCount, SellEntry *entries); // 0x004D21E6
int CShopkeeper_mobileWillBuy(CMobile *vendor, CItem *item); // 0x004D27EE
void CShopkeeper_SellItemFromPlayer(CNPC *npc, CItem *player, CItem *item); // 0x004D2982
void CMobile_VendorRestockTick(CMobile *this); // 0x004D2E6F
int CShopkeeper_CheckVendorHasStock(CMobile *vendor); // 0x004D2ECC
void *CShopkeeper_ScalarDelete(CNPC *npc, int flags); // 0x004D2F50

#endif /* SHOPKEEPER_H_ */
