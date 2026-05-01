#ifndef CONTAINER_H_
#define CONTAINER_H_

#include <stdint.h>

__extension__ typedef struct CContainer CContainer;
__extension__ typedef struct CPlayer CPlayer;
struct CMobile;
struct CString;

#include "item.h"
#include "dice.h"

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CString CString;

/*
 * Extends CItem with child-item storage and (for equippable items) an
 * overlapping weapon/armor state region. Size: 0x5C bytes.
 *
 * The 0x50 and 0x54 regions are dual-purpose: container roles use the
 * pointer fields; non-container equippable items reuse the same bytes
 * for weapon class + damage dice and durability HP. The binary reads
 * these bytes directly through their offsets.
 */
// clang-format off
struct CContainer {
	CItem item;                              // 0x00
	__extension__ union {
		CItem *contents;                 // 0x50 - first child when used as a container
		__extension__ struct {
			uint8_t weaponClass;      // 0x50 - damage type flags when used as a weapon
			CWeaponDice weaponDamage; // 0x51 - NdF+B damage dice
#if __SIZEOF_POINTER__ == 8
			uint8_t _pad64_wpn[4];    // 64-bit alignment pad
#endif
		};
	};
	__extension__ union {
		void *lockOwner;                 // 0x54 - map editor / mob follower owner
		__extension__ struct {
			uint8_t weaponHitPoints; // 0x54 - rolled from def->hitPoints dice
			uint8_t weaponCurrentHP; // 0x55 - rolled from def->armorClass dice
			uint8_t weaponMaxHP;     // 0x56 - rolled from def->armorClass dice
			uint8_t _weapon_pad57;   // 0x57
#if __SIZEOF_POINTER__ == 8
			uint8_t _pad64_hp[4];    // 64-bit alignment pad
#endif
		};
	};
	uint16_t storedWeight;                   // 0x58 - cached total contents weight
	uint8_t _cont_pad[0x02];                 // 0x5A
};
// clang-format on

/*
 * Bulletin board (@=B, 0x64 bytes). Extends CContainer with doubly-linked
 * list pointers that chain every board through the global list headed at
 * 0x006933A8.
 */
__extension__ typedef struct CBulletinBoard CBulletinBoard;
struct CBulletinBoard {
	CContainer container;                   // 0x00
	CBulletinBoard *bbNext;                 // 0x5C
	CBulletinBoard *bbPrev;                 // 0x60
};

extern int g_ContainerCount; // 0x0068B3A0

void CContainer_Constructor(CContainer *cont); // 0x00448760
void CContainer_ConstructorWithArgs(CContainer *cont, uint16_t bodyType, CLocation *loc); // 0x004487AB
int CContainer_ExcludedAmount_VT(CItem *self); // 0x0044883C
int CContainer_HasAccessibleContents_VT(CItem *item); // 0x0044887F
int CItem_CancelTrade(CItem *container); // 0x00448926
void CContainer_PreDeleteCleanup(CItem *item); // 0x0044899A
void CContainer_Destructor(CItem *item); // 0x00448A94
int CContainer_CountItems(CContainer *this, int recursive); // 0x00448B3C
int CContainer_ContainsEntity(CContainer *this, CItem *target, int recursive); // 0x00448BC0
int CContainer_GetValue_VT(CItem *self, int useResource, int normalize); // 0x00448C2E
void CContainer_DecayPlace(CItem *self, CLocation *loc); // 0x00448CBD
void CContainer_DecayReturnHome(CItem *self, CLocation *loc); // 0x00448D7C
void CContainer_DecayCleanup(CItem *self); // 0x00448E55
void CContainer_SendContainerContents(CContainer *this, CItem *entity, uint32_t serial, int sellMode); // 0x00448F6E
void CContainer_SendFilteredContents(CContainer *this, CItem *entity, uint32_t serial, int sellMode); // 0x00448FBA
CItem *CContainer_FindItemBySerial(CContainer *this, uint32_t serial); // 0x00449006
int CContainer_GetTotalQuantity(CContainer *this, uint16_t bodyType); // 0x0044909B
int CContainer_GetMaxWeight(CItem *self); // 0x00449129
uint32_t CContainer_GetStoredWeight(CItem *item); // 0x0044914E
CItem *CContainer_ConsumeResources(CContainer *container, uint16_t bodyType, int targetAmount, CItem *result); // 0x00449175
uint16_t CItem_GetContainerGump(CItem *item); // 0x004493F8
void CContainer_GetContainerBounds(CItem *container, int *minX, int *x, int *minY, int *y); // 0x004497C1
void CContainer_AddStoredWeight(CItem *this, int weight); // 0x00449C46
int CContainer_CheckSubtractWeight(CItem *this, int weight); // 0x00449C8A
void CContainer_SubtractStoredWeight(CItem *this, int weight); // 0x00449CC1
void CContainer_RecalcStoredWeight(CItem *this); // 0x00449D16
int CContainer_CanHold(CContainer *this, CItem *item, int *reason); // 0x0044A5CD
void CContainer_ValidateRegistration_VT(CContainer *self); // 0x0044A729
void *CContainer_ScalarDelete(CItem *self, int flags); // 0x0044A770
void CContainer_Delete(CItem *self); // 0x00484E3C
CItem *CContainer_FindInTagList_VT(CContainer *self, CString *name, uint32_t value); // 0x0048F2E7
uint32_t CContainer_GetWeight(CItem *item);

#endif /* CONTAINER_H_ */
