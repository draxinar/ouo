#ifndef MAGICLIST_H_
#define MAGICLIST_H_

#include <stdint.h>

__extension__ typedef struct CEffectTableEntry CEffectTableEntry;
__extension__ typedef struct CItem CItem;

/*
 * Name/weight payload stored in a CMagicItemList (0x14 bytes).
 */
__extension__ typedef struct CMagicItemListNode CMagicItemListNode;
struct CMagicItemListNode {
	CString str;    // 0x00
	int value;      // 0x10
};

/*
 * CResList of CMagicItemListNode entries with the running result item
 * that the factory is building (0x18 bytes).
 */
__extension__ typedef struct CMagicItemList CMagicItemList;
struct CMagicItemList {
	CResList list;          // 0x00
	uint32_t mlCount;       // 0x0C
	uint16_t itemId;        // 0x10
	uint16_t pad;           // 0x12
	uint32_t result;        // 0x14
};

CMagicItemListNode *CMagicItemListNode_SetData(CMagicItemListNode *node, CMagicItemListNode *src); // 0x00440420
CEffectTableEntry *CEffectTableEntry_Constructor(CEffectTableEntry *this); // 0x00466211
CMagicItemListNode *CMagicItemListNode_CopyConstructor(CMagicItemListNode *node, CMagicItemListNode *src); // 0x004662DB
void CMagicItemList_Constructor(CMagicItemList *obj); // 0x00466303
void CMagicItemList_Destructor(CMagicItemList *obj); // 0x00466390
CItem *CMagicItemList_GetResult(CMagicItemList *obj); // 0x004663D3
void CMagicItemList_AddItem(CMagicItemList *obj, CItem *entity); // 0x00466612
void *CMagicItemList_InitAndCopyStr(CResList *this, CResList *src); // 0x00466BB0
void SmartPtr_Destructor_CString(CSmartPtr *self); // 0x00466FD0

#endif /* MAGICLIST_H_ */
