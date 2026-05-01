#ifndef WEAPON_H_
#define WEAPON_H_

#include <stdint.h>

/*
 * Weapon/armor definition system backed by g_WeaponManager
 * (0x006933F0), a CArray<CWeaponDef*> of capacity 255. Definitions are
 * loaded one per weapon.%d file by CWeaponManager_LoadAll (0x004DE757).
 */

__extension__ typedef struct CWeaponDice CWeaponDice;
#define MAX_WEAPON_DEFS 255

/*
 * Bitmask of damage categories set by CWeaponDef_AddType (0x004DE7D2).
 */
enum {
	WeaponType_Slashing = 0x01,
	WeaponType_Piercing = 0x02,
	WeaponType_Bashing = 0x04,
	WeaponType_Ranged = 0x08,
	WeaponType_Shield = 0x10,
};

/*
 * Grip requirement set by CWeaponDef_SetHandedness (0x004DE891).
 */
enum {
	Handedness_None = 0,
	Handedness_Lefthand = 1,
	Handedness_Righthand = 2,
	Handedness_Twohanded = 4,
};

/*
 * Template for one weapon/armor type (28 bytes). Constructor
 * CWeaponDef_Constructor (0x004DE540), defaults set by
 * CWeaponDef_InitDefaults (0x004DE6A1).
 */
__extension__ typedef struct CWeaponDef CWeaponDef;
struct CWeaponDef {
	uint8_t id;              // +0x00
	CWeaponDice weaponClass; // +0x01 (damage dice NdF+B)
	CWeaponDice armorClass;  // +0x04 (armor rating dice)
	CWeaponDice hitPoints;   // +0x07 (durability dice)
	uint8_t range;           // +0x0A
	uint8_t typeFlags;       // +0x0B (WeaponType_* bitmask)
	uint8_t handedness;      // +0x0C (Handedness_*)
	uint8_t bow;             // +0x0D (0=none, 2=crossbow)
	uint16_t ammoType;       // +0x0E
	uint8_t strengthNeeded;  // +0x10
	uint8_t speed;           // +0x11
	uint8_t minRange;        // +0x12
	uint8_t _pad13;          // +0x13
	uint16_t hitSfx;         // +0x14
	uint16_t missSfx;        // +0x16
	uint32_t reserved;       // +0x18 (init 0xFFFF)
};

/*
 * CArray<CWeaponDef*> at g_WeaponManager (0x006933F0): a heap pointer
 * array plus its slot count.
 */
__extension__ typedef struct CWeaponManager CWeaponManager;
struct CWeaponManager {
	CWeaponDef **data; // +0x00
	int capacity;      // +0x04
};

__extension__ typedef struct CArmorArray CArmorArray;
__extension__ typedef struct CItem CItem;

extern CWeaponManager g_WeaponManager;

CArmorArray *CArmorArray_Constructor(CArmorArray *this); // 0x004DE4BE
void CArmorArray_Destructor(CArmorArray *this); // 0x004DE4CC (no-op)
CWeaponDef *CWeaponDef_Constructor(CWeaponDef *def); // 0x004DE540
void CWeaponDef_Destructor(CWeaponDef *def); // 0x004DE5BC
CWeaponDef *CWeaponDef_CopyConstructor(CWeaponDef *this, CWeaponDef *src); // 0x004DE5C7
void CWeaponDef_InitDefaults(CWeaponDef *def); // 0x004DE6A1
void CWeaponManager_Constructor(CWeaponManager *mgr); // 0x004DE729
void CWeaponManager_LoadAll(CWeaponManager *mgr); // 0x004DE757
int CWeaponManager_GetWeapon(CWeaponManager *mgr, int id, CItem *item); // 0x004DE78D
int CWeaponManager_LoadWeaponDef(CWeaponManager *mgr, int id); // 0x004DE8F2
int CWeaponManager_ApplyWeaponTemplate(CWeaponManager *mgr, CWeaponDef *def, CItem *item); // 0x004DEFA4
CItem *CWeapon_ConstructorFromItem(CItem *item); // 0x004DF037
void CWeapon_InitFields(CItem *item); // 0x004DF096
CItem *CWeapon_Constructor(CItem *item, uint16_t bodyType); // 0x004DF0C8
int CWeapon_LoadFromManager(CItem *item, uint8_t id); // 0x004DF136
int CItem_CanWield(CItem *item, CItem *mob, uint8_t layer); // 0x004DF155
int CItem_IsRangedWeapon(CItem *item); // 0x004DF3AD
int CItem_IsSlashing(CItem *item); // 0x004DF3D5
int CItem_IsBashing(CItem *item); // 0x004DF3F0
int CItem_IsPiercing(CItem *item); // 0x004DF40B
int CItem_IsShield(CItem *item); // 0x004DF426
int CItem_IsTwoHandedWeapon(CItem *item); // 0x004DF483
int CItem_RollWeaponDamage(CItem *item); // 0x004DF50A
int CWeapon_EquipOnMobile(CItem *self, CItem *mob, uint8_t layer); // 0x004DF58F
int CItem_DamageDurability(CItem *item, int threshold, int arg2, intptr_t arg3, intptr_t arg4, int overrideThreshold); // 0x004DF71F
int CItem_IsReallyWeapon(CItem *item); // 0x004DF7C0
CWeaponDice *CWeapon_GetDamageDice(CItem *item); // 0x004DF819
void CWeapon_SetDamageDice(CItem *item, CWeaponDice *src); // 0x004DF82A
uint8_t CWeapon_GetMaxAC(CItem *item); // 0x004DF846
void CWeapon_SetMaxAC(CItem *item, uint8_t val); // 0x004DF857
uint8_t CItem_GetArmorRating(CItem *item); // 0x004DF86D
uint8_t CWeapon_GetHandedness(CItem *item); // 0x004DF8A3
uint8_t CItem_CalcEffectiveRange(CItem *item); // 0x004DF901
uint8_t CItem_GetMeleeRange(CItem *item); // 0x004DF93C
uint8_t CWeapon_GetMaxHP(CItem *item); // 0x004DF95F
void CWeapon_SetMaxHP(CItem *item, uint8_t val); // 0x004DF970
uint8_t CWeapon_GetCurHP(CItem *item); // 0x004DF986
void CWeapon_SetCurHP(CItem *item, uint8_t val); // 0x004DF997
uint8_t CItem_GetStrengthNeeded(CItem *item); // 0x004DF9AD
uint8_t CItem_GetHandedness(CItem *item); // 0x004DF9D0
uint8_t CItem_GetWeaponDamageType(CItem *item); // 0x004DF9F3
uint8_t CItem_GetMinRange(CItem *item); // 0x004DFA16
uint8_t CItem_GetBow(CItem *item); // 0x004DFA39
uint16_t CItem_GetAmmoType(CItem *weapon); // 0x004DFA5C
uint8_t CItem_GetSpeed(CItem *item); // 0x004DFA7F
uint16_t CItem_GetHitSfx(CItem *item); // 0x004DFAA2
uint16_t CItem_GetMissSfx(CItem *item); // 0x004DFAC5
int CWeapon_GetValue(CItem *self, int vendor, int normalize); // 0x004DFB3F
void CWeaponDef_SetDamageDice(CWeaponDef *def, CWeaponDice *src); // 0x004DFC90
void CWeaponDef_SetACDice(CWeaponDef *def, CWeaponDice *src); // 0x004DFCAC
void CWeaponDef_SetDurabilityDice(CWeaponDef *def, CWeaponDice *src); // 0x004DFCC8
CWeaponDef *CWeaponManager_LookupWeaponDef(CWeaponManager *mgr, int id); // 0x004DFCE4
uint32_t CItem_ResolveMaterialType(CItem *item); // 0x004DFD14
uint32_t CWeaponDef_ResolveMaterialType(CWeaponDef *def, CItem *item); // 0x004DFD3B
CWeaponDice *CWeaponDef_GetDamageDicePtr(CWeaponDef *def); // 0x004DFE33
CWeaponDice *CWeaponDef_GetACDicePtr(CWeaponDef *def); // 0x004DFE44
CWeaponDice *CWeaponDef_GetDurabilityDicePtr(CWeaponDef *def); // 0x004DFE55
int CWeaponManager_IsValidId(CWeaponManager *mgr, int id); // 0x004DFE66
int CItem_LoadWeaponDef(CItem *item, int arg); // 0x004DFE8B
int CWeapon_SetWeaponDef(CItem *item, uint8_t id); // 0x004DFECC
int CWeaponManager_WeaponDefExists(CWeaponManager *mgr, int id); // 0x004DFF04
CItem *CWeapon_Create(uint16_t bodyType); // 0x004DFF22
CWeaponDef *CWeaponDef_ScalarDeletingDtor(CWeaponDef *def, int flags); // 0x004DFFD0
CWeaponManager *CWeaponArray_Constructor(CWeaponManager *mgr, int capacity); // 0x004E0030
CWeaponDef *CWeaponArray_GetAt(CWeaponManager *mgr, int id); // 0x004E00A0
int CWeaponArray_Exists(CWeaponManager *mgr, int id); // 0x004E00D0
int CWeaponArray_Remove(CWeaponManager *mgr, int id); // 0x004E0110
void CWeaponArray_RemoveAll(CWeaponManager *mgr); // 0x004E0180
int CWeaponArray_SetAt(CWeaponManager *mgr, int id, CWeaponDef *def); // 0x004E01C0
void CWeaponArray_Destructor(CWeaponManager *mgr); // 0x004E0200
int CWeaponArray_EnsureCapacity(CWeaponManager *mgr, int id); // 0x004E0230
uint8_t CWeaponDef_GetId(CWeaponDef *def); // 0x004E0270
void CWeaponDef_SetId(CWeaponDef *def, uint8_t val); // 0x004E0280
uint16_t CWeaponDef_GetAmmoType(CWeaponDef *def); // 0x004E02A0
void CWeaponDef_SetAmmoType(CWeaponDef *def, uint16_t val); // 0x004E02C0
uint8_t CWeaponDef_GetBow(CWeaponDef *def); // 0x004E02E0
void CWeaponDef_SetBow(CWeaponDef *def, uint8_t val); // 0x004E0300
uint8_t CWeaponDef_GetHandedness(CWeaponDef *def); // 0x004E0320
uint8_t CWeaponDef_GetTypeFlags(CWeaponDef *def); // 0x004E0340
uint8_t CWeaponDef_GetRange(CWeaponDef *def); // 0x004E0360
void CWeaponDef_SetRange(CWeaponDef *def, uint8_t val); // 0x004E0380
uint8_t CWeaponDef_GetStrengthNeeded(CWeaponDef *def); // 0x004E03A0
void CWeaponDef_SetStrengthNeeded(CWeaponDef *def, uint8_t val); // 0x004E03C0
uint8_t CWeaponDef_GetSpeed(CWeaponDef *def); // 0x004E03E0
void CWeaponDef_SetSpeed(CWeaponDef *def, uint8_t val); // 0x004E0400
uint8_t CWeaponDef_GetMinRange(CWeaponDef *def); // 0x004E0420
void CWeaponDef_SetMinRange(CWeaponDef *def, uint8_t val); // 0x004E0440
uint16_t CWeaponDef_GetHitSfx(CWeaponDef *def); // 0x004E0460
void CWeaponDef_SetHitSfx(CWeaponDef *def, uint16_t val); // 0x004E0480
uint16_t CWeaponDef_GetMissSfx(CWeaponDef *def); // 0x004E04A0
int vt_stub_return_1(CItem *self); // 0x004E04C0
uint8_t CItem_GetWeaponDefId(CItem *item);

#endif /* WEAPON_H_ */
