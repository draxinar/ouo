/*
 * Weapon and armor definitions.
 *
 * Loads weapons/weapon.%d files into CWeaponDef entries (damage dice,
 * speed, range, armor class) and serves the per-item lookups used when
 * items are equipped or used in combat.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "dat.h"
#include "egg.h"
#include "filemanager.h"
#include "io.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "utils.h"
#include "vtable.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "world.h"

static void CWeaponDef_AddType(CWeaponDef *def, const char *type); // 0x004DE7D2
static void CWeaponDef_SetHandedness(CWeaponDef *def, const char *hand); // 0x004DE891
static int CItem_CalcAC_Inner(int curDur, int baseDur, int baseValue); // 0x004DFAE8

/*
 * Row of the equipment-layer to handedness table used by CItem_CanWield.
 */
struct WeaponLayerEntry {
	uint32_t layer;
	uint32_t handedness;
};
// 0x005F0078 - layer-to-handedness lookup for armor/shields (14 entries)
static const struct WeaponLayerEntry g_WeaponLayerHandednessTable[14];

CWeaponManager g_WeaponManager;

/*
 * 0x004DE4BE - CArmorArray::CArmorArray
 *
 * Empty constructor.
 */
CArmorArray *
CArmorArray_Constructor(CArmorArray *this)
{
	return this;
}

/*
 * 0x004DE4CC - CArmorArray::~CArmorArray
 *
 * Empty destructor.
 */
void
CArmorArray_Destructor(CArmorArray *this)
{
	USED(this);
}

/*
 * 0x004DE540 - CWeaponDef::CWeaponDef
 *
 * Constructs the three dice fields (weapon class, armor class, hit points)
 * and resets all other fields to defaults.
 */
CWeaponDef *
CWeaponDef_Constructor(CWeaponDef *def)
{
	CDiceRoll_Constructor(&def->weaponClass);
	CDiceRoll_Constructor(&def->armorClass);
	CDiceRoll_Constructor(&def->hitPoints);
	CWeaponDef_InitDefaults(def);
	return def;
}

/*
 * 0x004DE577 - CWeaponDef::CWeaponDef (copy construct in place)
 *
 * Default-constructs the three CDiceRoll members and the scalar
 * defaults, then copies every field from other.
 */
static __attribute__((unused)) CWeaponDef *
CWeaponDef_ConstructCopy(CWeaponDef *def, CWeaponDef *other)
{
	CDiceRoll_Constructor(&def->weaponClass);
	CDiceRoll_Constructor(&def->armorClass);
	CDiceRoll_Constructor(&def->hitPoints);
	CWeaponDef_InitDefaults(def);
	CWeaponDef_CopyConstructor(def, other);
	return def;
}

/*
 * 0x004DE5BC - CWeaponDef::~CWeaponDef
 *
 * Empty destructor.
 */
void
CWeaponDef_Destructor(CWeaponDef *def)
{
	USED(def);
}

/*
 * 0x004DE5C7 - CWeaponDef::CWeaponDef (copy constructor)
 *
 * Copies every field from src into this and returns this. The
 * binary copies typeFlags twice; reproduced exactly.
 */
CWeaponDef *
CWeaponDef_CopyConstructor(CWeaponDef *this, CWeaponDef *src)
{
	this->id = src->id;
	CDiceRoll_Copy(&this->weaponClass, &src->weaponClass);
	this->strengthNeeded = src->strengthNeeded;
	CDiceRoll_Copy(&this->armorClass, &src->armorClass);
	CDiceRoll_Copy(&this->hitPoints, &src->hitPoints);
	this->range = src->range;
	this->typeFlags = src->typeFlags;
	this->handedness = src->handedness;
	this->ammoType = src->ammoType;
	this->bow = src->bow;
	this->speed = src->speed;
	this->minRange = src->minRange;
	this->hitSfx = src->hitSfx;
	this->missSfx = src->missSfx;
	this->typeFlags = src->typeFlags; // redundant copy in binary
	return this;
}

/*
 * 0x004DE6A1 - CWeaponDef::InitDefaults
 *
 * Resets the weapon definition: id=0xFF, dice cleared, every numeric
 * field zeroed, reserved=0xFFFF.
 */
void
CWeaponDef_InitDefaults(CWeaponDef *def)
{
	def->id = 0xFF;
	CDiceRoll_Clear(&def->weaponClass);
	CDiceRoll_Clear(&def->armorClass);
	CDiceRoll_Clear(&def->hitPoints);
	def->strengthNeeded = 0;
	def->range = 0;
	def->handedness = 0;
	def->typeFlags = 0;
	def->ammoType = 0;
	def->bow = 0;
	def->speed = 0;
	def->minRange = 0;
	def->missSfx = 0;
	def->hitSfx = 0;
	def->reserved = 0xFFFF;
}

/*
 * 0x004DE729 - CWeaponManager::CWeaponManager
 *
 * Constructs the underlying weapon array with capacity 255.
 */
void
CWeaponManager_Constructor(CWeaponManager *mgr)
{
	CWeaponArray_Constructor(mgr, 0xFF);
}

/*
 * 0x004DE744 - CWeaponArray scalar deleting destructor
 *
 * Calls the array destructor without freeing the storage.
 */
static __attribute__((unused)) void
CWeaponArray_ScalarDtor(CWeaponManager *this)
{
	CWeaponArray_Destructor(this);
}

/*
 * 0x004DE757 - CWeaponManager::LoadAll
 *
 * Loads every weapon definition from disk by calling LoadWeaponDef
 * for each id 0..254.
 */
void
CWeaponManager_LoadAll(CWeaponManager *mgr)
{
	int i;

	for (i = 0; i < MAX_WEAPON_DEFS; i++)
		CWeaponManager_LoadWeaponDef(mgr, i);
}

/*
 * 0x004DE78D - CWeaponManager::GetWeapon
 *
 * Looks up the weapon definition by id and applies its template to
 * the item. Returns 0 if no entry exists for id.
 */
int
CWeaponManager_GetWeapon(CWeaponManager *mgr, int id, CItem *item)
{
	CWeaponDef *def;

	if (!CWeaponArray_Exists(mgr, id & 0xFF))
		return 0;
	def = CWeaponArray_GetAt(mgr, id & 0xFF);
	return CWeaponManager_ApplyWeaponTemplate(mgr, def, item);
}

/*
 * 0x004DE7D2 - CWeaponDef::AddType
 *
 * Maps a damage-type keyword ("SLASHING", "PIERCING", "BASHING",
 * "RANGED", "SHIELD") to its WeaponType_* flag and ORs it into
 * typeFlags. Comparisons are case-insensitive.
 */
static void
CWeaponDef_AddType(CWeaponDef *def, const char *type)
{
	if (strcasecmp(type, "SLASHING") == 0)
		def->typeFlags |= WeaponType_Slashing;
	if (strcasecmp(type, "PIERCING") == 0)
		def->typeFlags |= WeaponType_Piercing;
	if (strcasecmp(type, "BASHING") == 0)
		def->typeFlags |= WeaponType_Bashing;
	if (strcasecmp(type, "RANGED") == 0)
		def->typeFlags |= WeaponType_Ranged;
	if (strcasecmp(type, "SHIELD") == 0)
		def->typeFlags |= WeaponType_Shield;
}

/*
 * 0x004DE891 - CWeaponDef::SetHandedness
 *
 * Maps a handedness keyword ("LEFTHAND", "RIGHTHAND", "TWOHANDED")
 * case-insensitively to the corresponding Handedness_* value.
 */
static void
CWeaponDef_SetHandedness(CWeaponDef *def, const char *hand)
{
	if (strcasecmp(hand, "LEFTHAND") == 0)
		def->handedness = Handedness_Lefthand;
	if (strcasecmp(hand, "RIGHTHAND") == 0)
		def->handedness = Handedness_Righthand;
	if (strcasecmp(hand, "TWOHANDED") == 0)
		def->handedness = Handedness_Twohanded;
}

/*
 * 0x004DE8F2 - CWeaponManager::LoadWeaponDef
 *
 * Loads "weapon.<id>" into a freshly constructed CWeaponDef by
 * parsing one named field per line, then replaces any existing
 * entry in the manager array.
 */
int
CWeaponManager_LoadWeaponDef(CWeaponManager *mgr, int id)
{
	char filename[256];
	char line[512];
	char strBuf[176];
	char type1[160];
	char type2[192];
	char type3[168];
	int intBuf;
	int nMatched;
	CWeaponDef *def;
	CWeaponDice diceTemp;
	FILE *f;

	id = id & 0xFF;
	if (id == 0xFF)
		return 0;

	sprintf(filename, "weapon.%d", id & 0xFF);

	f = FileManager_OpenByType(0x33, filename, "r");
	if (f == NULL)
		return 0;

	def = (CWeaponDef *)malloc(sizeof(CWeaponDef));
	if (def != NULL)
		CWeaponDef_Constructor(def);

	intBuf = 0;
	nMatched = 0;

	// CWeaponDef_SetId
	CWeaponDef_SetId(def, (uint8_t)id);

	// Line 1: Name
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Name: %[^\n]s\n", strBuf);

	// Line 2: Weapon Class
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Weapon Class: %s\n", strBuf);
	if (nMatched == 1) {
		CDiceRoll_InitParse(&diceTemp, strBuf);
		CWeaponDef_SetDamageDice(def, &diceTemp);
	}

	// Line 3: Armor Class
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Armor Class: %s\n", strBuf);
	if (nMatched == 1) {
		CDiceRoll_InitParse(&diceTemp, strBuf);
		CWeaponDef_SetACDice(def, &diceTemp);
	}

	// Line 4: Hit Points
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Hit Points: %s\n", strBuf);
	if (nMatched == 1) {
		CDiceRoll_InitParse(&diceTemp, strBuf);
		CWeaponDef_SetDurabilityDice(def, &diceTemp);
	}

	// Line 5: Speed
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Speed: %d\n", &intBuf);
	if (nMatched == 1)
		CWeaponDef_SetSpeed(def, (uint8_t)intBuf);

	// Line 6: Range
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Range: %d\n", &intBuf);
	if (nMatched == 1)
		CWeaponDef_SetRange(def, (uint8_t)intBuf);

	// Line 7: Min Range
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Min Range: %d\n", &intBuf);
	if (nMatched == 1)
		CWeaponDef_SetMinRange(def, (uint8_t)intBuf);

	// Line 8: Type (up to 3 space-separated types)
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Type: %s %s %s\n", type1, type2, type3);
	if (nMatched >= 1)
		CWeaponDef_AddType(def, type1);
	if (nMatched >= 2)
		CWeaponDef_AddType(def, type2);
	if (nMatched >= 3)
		CWeaponDef_AddType(def, type3);

	// Line 9: Handedness
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Handedness: %s\n", strBuf);
	if (nMatched == 1)
		CWeaponDef_SetHandedness(def, strBuf);

	// Line 10: Strength Needed
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Strength Needed: %d\n", &intBuf);
	if (nMatched == 1)
		CWeaponDef_SetStrengthNeeded(def, (uint8_t)intBuf);

	// Line 11: Ammo Type
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Ammo Type: %d\n", &intBuf);
	if (nMatched == 1)
		CWeaponDef_SetAmmoType(def, (uint16_t)intBuf);

	// Line 12: Bow
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "Bow: %d\n", &intBuf);
	if (nMatched == 1)
		CWeaponDef_SetBow(def, (uint8_t)intBuf);

	// Line 13: HitFx
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "HitFx: %d\n", &intBuf);
	if (nMatched == 1)
		CWeaponDef_SetHitSfx(def, (uint16_t)intBuf);

	// Line 14: MissFx - binary calls CResourceEntity_SetTemplateIndex
	// which writes a word at this+0x16 (same offset as CWeaponDef.missSfx)
	fgets_ServerSide(line, 0x1FF, f);
	nMatched = sscanf(line, "MissFx: %d\n", &intBuf);
	if (nMatched == 1)
		def->missSfx = (uint16_t)intBuf;

	fclose_ServerSide(f);

	// CArray::Remove old entry at this index
	CWeaponArray_Remove(mgr, id & 0xFF);

	// CArray::SetAt - insert-not-overwrite semantics
	if (!CWeaponArray_SetAt(mgr, id & 0xFF, def)) {
		// SetAt failed (slot still occupied) - destroy new def
		if (def != NULL)
			CWeaponDef_ScalarDeletingDtor(def, 1);
		return 0;
	}
	return 1;
}

/*
 * 0x004DEFA4 - CWeaponManager::ApplyWeaponTemplate
 *
 * Stamps a weapon item with values from def: copies the damage dice,
 * rolls AC and durability dice to set max AC, max HP and current HP.
 * Returns 0 if SetWeaponDef rejects the id.
 */
int
CWeaponManager_ApplyWeaponTemplate(CWeaponManager *mgr, CWeaponDef *def, CItem *item)
{
	uint8_t hp;

	USED(mgr);

	if (!CWeapon_SetWeaponDef(item, CWeaponDef_GetId(def)))
		return 0;

	CWeapon_SetDamageDice(item, CWeaponDef_GetDamageDicePtr(def));
	CWeapon_SetMaxAC(item, (uint8_t)CDiceRoll_Roll(CWeaponDef_GetACDicePtr(def)));

	hp = (uint8_t)CDiceRoll_Roll(CWeaponDef_GetDurabilityDicePtr(def));
	CWeapon_SetMaxHP(item, hp);
	CWeapon_SetCurHP(item, hp);

	return 1;
}

/*
 * 0x004DF037 - CWeapon::CWeapon (no-args variant)
 *
 * Constructs a weapon item without setting the body type, used by
 * the save loader where the body type comes from the save data.
 */
CItem *
CWeapon_ConstructorFromItem(CItem *item)
{
	CContainer *cont = (CContainer *)item;

	CItem_Constructor(item);
	CDiceRoll_Constructor(&cont->weaponDamage);
	CEntity_SetType(&item->resourceEntity.entity, ETYPE_WEAPON);
	CWeapon_InitFields(item);
	return item;
}

/*
 * 0x004DF096 - CWeapon::InitFields
 *
 * Resets the weapon-specific fields: weaponClass=0xFF, damage dice
 * cleared, hitPoints/maxHP/currentHP zeroed.
 */
void
CWeapon_InitFields(CItem *item)
{
	CContainer *cont = (CContainer *)item;

	cont->weaponClass = 0xFF;
	CDiceRoll_Clear(&cont->weaponDamage);
	cont->weaponHitPoints = 0;
	cont->weaponMaxHP = 0;
	cont->weaponCurrentHP = 0;
}

/*
 * 0x004DF0C8 - CWeapon::CWeapon
 *
 * Constructs a weapon item with bodyType, building on top of CItem.
 */
CItem *
CWeapon_Constructor(CItem *item, uint16_t bodyType)
{
	CContainer *cont = (CContainer *)item;

	CItem_Constructor(item);
	CDiceRoll_Constructor(&cont->weaponDamage);
	CEntity_SetType(&item->resourceEntity.entity, ETYPE_WEAPON);
	CWeapon_InitFields(item);
	CEntity_SetBodyType(item, bodyType);
	return item;
}

/*
 * 0x004DF136 - CWeapon::LoadFromManager
 *
 * Stamps the item with weapon-definition values from the global
 * weapon manager.
 */
int
CWeapon_LoadFromManager(CItem *item, uint8_t id)
{
	return CWeaponManager_GetWeapon(&g_WeaponManager, id, item);
}

/*
 * 0x004DF155 - CItem::CanWield
 *
 * Verifies that mob can equip the weapon in the given layer: enforces
 * strength requirement, two-handed/one-handed slot conflicts, and the
 * "single weapon" rule. Sends a system message describing the failure
 * and returns 0; returns 1 when the wield is allowed. World loading and
 * non-player mobiles bypass the checks.
 */
int
CItem_CanWield(CItem *item, CItem *mob, uint8_t layer)
{
	uint16_t str;
	CItem *weapon;
	uint8_t handedness;
	CMobile *m = (CMobile *)mob;

	if ((layer & 0xFF) == 0)
		return 1;
	if (g_World->isLoading)
		return 1;

	str = m->baseStr;
	if (str < (CItem_GetStrengthNeeded(item) & 0xFF)) {
		Entity_SendSystemMessage(mob, mob->serial, "You are not strong enough to equip that.");
		return 0;
	}

	if (!VT_IsPlayer(mob))
		return 1;

	weapon = CMobile_GetWeapon(m);

	if (m->equipment[layer & 0xFF] == item)
		return 1;

	if ((layer & 0xFF) == 1 || (layer & 0xFF) == 2) {
		if (weapon != NULL && CItem_IsTwoHandedWeapon(weapon)) {
			Entity_SendSystemMessage(mob, mob->serial, "You already have something in both hands.");
			return 0;
		}
	}

	if (CItem_IsReallyWeapon(item) && weapon != NULL) {
		Entity_SendSystemMessage(mob, mob->serial, "You can only wield one weapon at a time.");
		return 0;
	}

	if (!CItem_IsReallyWeapon(item))
		return 1;

	handedness = CItem_GetHandedness(item);

	if ((handedness & 0xFF) == 4) {
		if (m->equipment[1] != NULL || m->equipment[2] != NULL) {
			Entity_SendSystemMessage(mob, mob->serial, "You must have both hands free to wield this weapon.");
			return 0;
		}
	}

	if ((handedness & 0xFF) == 2) {
		if (m->equipment[1] != NULL) {
			Entity_SendSystemMessage(mob, mob->serial, "Your right hand must be free to wield this weapon.");
			return 0;
		}
	}

	if ((handedness & 0xFF) == 1) {
		if (m->equipment[2] != NULL) {
			Entity_SendSystemMessage(mob, mob->serial, "Your left hand must be free to wield this weapon.");
			return 0;
		}
		if (m->equipment[1] != NULL) {
			if (VT_IsWeapon(m->equipment[1])) {
				if (CItem_IsTwoHandedWeapon(m->equipment[1])) {
					Entity_SendSystemMessage(mob, mob->serial, "You are already wielding a two handed weapon.");
				}
			}
		}
	}

	return 1;
}

/*
 * 0x004DF3AD - CItem::IsRangedWeapon
 *
 * Returns 1 when the weapon's damage-type flags include
 * WeaponType_Ranged.
 */
int
CItem_IsRangedWeapon(CItem *item)
{
	return ((CItem_GetWeaponDamageType(item) & 0xFF) & 0x08) ? 1 : 0;
}

/*
 * 0x004DF3D5 - CItem::IsSlashing
 *
 * Returns the slashing bit from the weapon's damage-type flags.
 */
int
CItem_IsSlashing(CItem *item)
{
	return (CItem_GetWeaponDamageType(item) & 0xFF) & 1;
}

/*
 * 0x004DF3F0 - CItem::IsBashing
 *
 * Returns the bashing bit from the weapon's damage-type flags.
 */
int
CItem_IsBashing(CItem *item)
{
	return (CItem_GetWeaponDamageType(item) & 0xFF) & 4;
}

/*
 * 0x004DF40B - CItem::IsPiercing
 *
 * Returns the piercing bit from the weapon's damage-type flags.
 */
int
CItem_IsPiercing(CItem *item)
{
	return (CItem_GetWeaponDamageType(item) & 0xFF) & 2;
}

/*
 * 0x004DF426 - CItem::IsShield
 *
 * Returns the shield bit from the weapon's damage-type flags.
 */
int
CItem_IsShield(CItem *item)
{
	return (CItem_GetWeaponDamageType(item) & 0xFF) & 0x10;
}

/*
 * 0x004DF483 - CItem::IsTwoHandedWeapon
 *
 * Returns 1 when the weapon's handedness value is 4 (two-handed).
 */
int
CItem_IsTwoHandedWeapon(CItem *item)
{
	return (CItem_GetHandedness(item) & 0xFF) == 4;
}

/*
 * 0x004DF4A5 - CTemplateItem::~CTemplateItem
 *
 * Hides the template item from the world (when still present),
 * clears its scripts and tags, and runs the base CItem destructor.
 */
static __attribute__((unused)) void
CTemplateItem_Destructor(CItem *this)
{
	// vtable set: *(uint32_t *)this = 0x5F00F8;
	// Binary reads byte at this+6 (removedFromWorld)
	if (this->resourceEntity.entity.removedFromWorld == 0)
		CItem_HideVT(this);
	CItem_ClearScriptsAndTags(this);
	CItem_Destructor(this);
}

/*
 * 0x004DF50A - CItem::RollWeaponDamage
 *
 * Rolls the weapon's damage dice and scales the result by the
 * current/maximum durability ratio.
 */
int
CItem_RollWeaponDamage(CItem *item)
{
	CContainer *cont = (CContainer *)item;
	int roll;

	roll = CDiceRoll_Roll(&cont->weaponDamage);
	return CItem_CalcAC_Inner(cont->weaponCurrentHP, cont->weaponMaxHP, roll);
}

/*
 * 0x004DF58F - CWeapon::EquipOnMobile
 *
 * Like CItem::EquipOnMobile, but additionally enforces CanWield up
 * front, broadcasts EQUIP_ITEM unconditionally (no server-only
 * filter), and clears the wearer's swingProgress at the end.
 */
int
CWeapon_EquipOnMobile(CItem *self, CItem *mob, uint8_t layer)
{
	uint8_t buf[16];
	CItem *topMob;
	uint32_t savedSerial;

	// CItem_CanWield check (not in CItem_EquipOnMobile)
	if (!CItem_CanWield(self, mob, layer))
		return 0;

	// multiPtr check
	if (self->multiPtr != NULL) {
		((void (*)(void *))VT_FN(self, VT_RETURN_TO_TRACKED))(self);
		return 0;
	}

	// Slot occupied check
	if (((CMobile *)mob)->equipment[layer] != NULL)
		return 0;

	// Perform equip
	self->decayCount = 0;
	((CMobile *)mob)->equipment[layer] = self;
	self->parent = mob;
	self->resourceEntity.entity.removedFromWorld = 0;
	CItem_AddWeightToParent(self, mob);

	// Broadcast EQUIP_ITEM if not loading
	if (!g_World->isLoading && !mob->resourceEntity.entity.removedFromWorld && layer != 0) {
		// No CItem_IsServerOnly check (unlike CItem_EquipOnMobile)
		PacketManager_MakePacket_EQUIP_ITEM(buf, self, (CMobile *)mob, layer);
		SendPacketInRange(buf, &mob->resourceEntity.entity.location, 0x12);
	}

	// Send status update to top container player
	topMob = CItem_FindTopContainerMobile(self);
	if (topMob != NULL) {
		if (VT_IsPlayer(topMob)) {
			SendStatusToPlayer((CMobile *)topMob, (CPlayer *)topMob, topMob->serial, 1);
		}
	}

	// Fire Equip event
	if (!CItem_IsDeleted(self)) {
		if (CItem_CanFireEquipEvent(layer) == 1) {
			savedSerial = CMobile_GetSerial((CMobile *)self);
			Entity_ExecuteEvent((CEntity *)self, 0x2C, CMobile_GetSerial((CMobile *)mob));
			CWorld_FindBySerial(g_World, savedSerial);
		}
	}

	// Clear swing progress (not in CItem_EquipOnMobile)
	((CMobile *)mob)->swingProgress = 0;

	return 1;
}

/*
 * 0x004DF71F - CItem::DamageDurability
 *
 * Rolls 1d300 against the threshold (or overrideThreshold when set);
 * on a hit, decrements the weapon's current HP. Args 2-4 are unused.
 * Returns 0 if the item just broke, 1 if it was decremented, 2 if no
 * damage was applied.
 */
int
CItem_DamageDurability(CItem *item, int threshold, int arg2, intptr_t arg3, intptr_t arg4, int overrideThreshold)
{
	int effectiveThreshold;
	int shouldDamage;
	int roll;
	int curDur;

	USED(arg2);
	USED(arg3);
	USED(arg4);

	shouldDamage = 0;
	effectiveThreshold = 0;

	if (overrideThreshold == -1)
		effectiveThreshold = threshold;
	else
		effectiveThreshold = overrideThreshold;

	roll = GetRandomRange(1, 300);
	if (roll <= effectiveThreshold)
		shouldDamage = 1;

	if (!shouldDamage)
		return 2;

	curDur = ((CContainer *)item)->weaponCurrentHP;
	curDur--;

	if (curDur <= 0) {
		((CContainer *)item)->weaponCurrentHP = 0;
		return 0;
	}

	((CContainer *)item)->weaponCurrentHP = (uint8_t)curDur;
	return 1;
}

/*
 * 0x004DF7C0 - CItem::IsReallyWeapon
 *
 * Returns 1 only when the item sits in a hand slot (layer 1 or 2),
 * is not a shield, and carries one of the four melee/ranged damage
 * type flags.
 */
int
CItem_IsReallyWeapon(CItem *item)
{
	uint8_t layer;

	layer = CWorld_GetItemLayer(CEntity_GetBodyType(item));

	if (CItem_IsShield(item))
		return 0;

	if (layer != 2 && layer != 1)
		return 0;

	return ((CItem_GetWeaponDamageType(item) & 0xFF) & 0xF) != 0;
}

/*
 * 0x004DF819 - CWeapon::GetDamageDice
 *
 * Returns a pointer to the weapon's damage dice.
 */
CWeaponDice *
CWeapon_GetDamageDice(CItem *item)
{
	CContainer *cont = (CContainer *)item;

	return &cont->weaponDamage;
}

/*
 * 0x004DF82A - CWeapon::SetDamageDice
 *
 * Copies src into the weapon's damage dice.
 */
void
CWeapon_SetDamageDice(CItem *item, CWeaponDice *src)
{
	CContainer *cont = (CContainer *)item;

	CDiceRoll_Copy(&cont->weaponDamage, src);
}

/*
 * 0x004DF846 - CWeapon::GetMaxAC
 *
 * Returns the weapon's max armor rating (stored in the hitPoints byte).
 */
uint8_t
CWeapon_GetMaxAC(CItem *item)
{
	CContainer *cont = (CContainer *)item;

	return cont->weaponHitPoints;
}

/*
 * 0x004DF857 - CWeapon::SetMaxAC
 *
 * Stores the weapon's max armor rating.
 */
void
CWeapon_SetMaxAC(CItem *item, uint8_t val)
{
	CContainer *cont = (CContainer *)item;

	cont->weaponHitPoints = val;
}

/*
 * 0x004DF86D - CItem::GetArmorRating
 *
 * Returns the effective armor rating, scaling the base AC by the
 * current/maximum durability ratio.
 */
uint8_t
CItem_GetArmorRating(CItem *item)
{
	CContainer *cont = (CContainer *)item;

	return (uint8_t)CItem_CalcAC_Inner(cont->weaponCurrentHP, cont->weaponMaxHP, cont->weaponHitPoints);
}

/*
 * 0x004DF8A3 - CWeapon::GetHandedness
 *
 * For an equipped non-weapon item, looks up the layer in a 14-entry table and
 * returns the matching handedness value, or 0 if nothing matches.
 */
uint8_t
CWeapon_GetHandedness(CItem *item)
{
	int i;

	if (CItem_IsReallyWeapon(item))
		return 0;

	for (i = 0; i < 14; i++) {
		if (g_WeaponLayerHandednessTable[i].layer == (CItem_GetLayerThiscall(item) & 0xFFu))
			return (uint8_t)g_WeaponLayerHandednessTable[i].handedness;
	}
	return 0;
}

/*
 * 0x004DF901 - CItem::CalcEffectiveRange
 *
 * Returns the weapon's melee range scaled by its current durability.
 */
uint8_t
CItem_CalcEffectiveRange(CItem *item)
{
	CContainer *cont = (CContainer *)item;
	int range;

	range = CItem_GetMeleeRange(item) & 0xFF;
	return (uint8_t)CItem_CalcAC_Inner(cont->weaponCurrentHP, cont->weaponMaxHP, range);
}

/*
 * 0x004DF93C - CItem::GetMeleeRange
 *
 * Returns the base melee range from the weapon definition.
 * FIXED: binary does not null-check the def pointer; we add a null check
 * returning 1 (default melee range) to avoid a crash.
 */
uint8_t
CItem_GetMeleeRange(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 1;
	return def->range;
}

/*
 * 0x004DF95F - CWeapon::GetMaxHP
 *
 * Returns the weapon's maximum durability.
 */
uint8_t
CWeapon_GetMaxHP(CItem *item)
{
	CContainer *cont = (CContainer *)item;

	return cont->weaponMaxHP;
}

/*
 * 0x004DF970 - CWeapon::SetMaxHP
 *
 * Stores the weapon's maximum durability.
 */
void
CWeapon_SetMaxHP(CItem *item, uint8_t val)
{
	CContainer *cont = (CContainer *)item;

	cont->weaponMaxHP = val;
}

/*
 * 0x004DF986 - CWeapon::GetCurHP
 *
 * Returns the weapon's current durability.
 */
uint8_t
CWeapon_GetCurHP(CItem *item)
{
	CContainer *cont = (CContainer *)item;

	return cont->weaponCurrentHP;
}

/*
 * 0x004DF997 - CWeapon::SetCurHP
 *
 * Stores the weapon's current durability.
 */
void
CWeapon_SetCurHP(CItem *item, uint8_t val)
{
	CContainer *cont = (CContainer *)item;

	cont->weaponCurrentHP = val;
}

/*
 * 0x004DF9AD - CItem::GetStrengthNeeded
 *
 * Reads weapon def's strengthNeeded field (uint8_t at CWeaponDef+0x10).
 * FIXED: binary does not null-check the def pointer; we add a null check
 * to avoid a crash when weaponDefId is not in the weapon table.
 */
uint8_t
CItem_GetStrengthNeeded(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 0;
	return def->strengthNeeded;
}

/*
 * 0x004DF9D0 - CItem::GetHandedness
 *
 * Thiscall on CItem. Reads weaponDefId at item+0x50, looks up via
 * CWeaponManager_LookupWeaponDef (0x004DFCE4), returns def->handedness.
 * Values: 1=left, 2=right, 4=two-handed.
 * FIXED: binary does not null-check the def pointer; we add a null check
 * returning 0 to avoid a crash.
 */
uint8_t
CItem_GetHandedness(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 0;
	return def->handedness;
}

/*
 * 0x004DF9F3 - CItem::GetWeaponDamageType
 *
 * Returns the weapon definition's damage-type flags.
 * FIXED: binary does not null-check the def pointer.
 */
uint8_t
CItem_GetWeaponDamageType(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 0;
	return def->typeFlags;
}

/*
 * 0x004DFA16 - CItem::GetMinRange
 *
 * Returns the weapon definition's minimum range.
 * FIXED: binary does not null-check the def pointer.
 */
uint8_t
CItem_GetMinRange(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 0;
	return def->minRange;
}

/*
 * 0x004DFA39 - CItem::GetBow
 *
 * Reads weapon def's bow field (uint8_t at CWeaponDef+0x0D).
 * FIXED: binary does not null-check the def pointer.
 */
uint8_t
CItem_GetBow(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 0;
	return def->bow;
}

/*
 * 0x004DFA5C - CItem::GetAmmoType
 *
 * Returns the weapon definition's ammo body type.
 * FIXED: binary does not null-check the def pointer.
 */
uint16_t
CItem_GetAmmoType(CItem *weapon)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(weapon));
	if (def == NULL)
		return 0;
	return def->ammoType;
}

/*
 * 0x004DFA7F - CItem::GetSpeed
 *
 * Thiscall on CItem. Returns def->speed.
 * FIXED: binary does not null-check the def pointer; we add a null check
 * returning 50 (default unarmed speed) to avoid a crash.
 */
uint8_t
CItem_GetSpeed(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 50;
	return def->speed;
}

/*
 * 0x004DFAA2 - CItem::GetHitSfx
 *
 * Thiscall on CItem. Returns def->hitSfx.
 * FIXED: binary does not null-check the def pointer.
 */
uint16_t
CItem_GetHitSfx(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 0;
	return def->hitSfx;
}

/*
 * 0x004DFAC5 - CItem::GetMissSfx
 *
 * Thiscall on CItem. Returns def->missSfx.
 * FIXED: binary does not null-check the def pointer.
 */
uint16_t
CItem_GetMissSfx(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, CItem_GetWeaponDefId(item));
	if (def == NULL)
		return 0;
	return def->missSfx;
}

/*
 * 0x004DFAE8 - CItem::CalcAC_Inner
 *
 * Shared durability/quality adjustment formula used by both
 * CItem::GetArmorRating and CItem::RollWeaponDamage.
 * Returns ceil((curDur / baseDur + 1.5) / 2.0 * baseValue).
 */
static int
CItem_CalcAC_Inner(int curDur, int baseDur, int baseValue)
{
	double result;

	if (baseDur <= 0)
		return 0;
	if (curDur < 0)
		curDur = 0;
	result = ((double)curDur / (double)baseDur + 1.5) / 2.0 * (double)baseValue;
	return (int)ceil(result);
}

/*
 * 0x004DFB3F - CWeapon::GetValue
 *
 * Computes a weapon/armor bonus on top of CItem::GetValue. Armor path
 * (maxAC > 0): bonus = maxAC * (handedness + 4) / 2. Weapon path
 * (maxAC == 0): bonus = rangeSpan * speed / 8. Both paths add curHP / 6.
 */
int
CWeapon_GetValue(CItem *self, int vendor, int normalize)
{
	int result = 0;
	int weaponValue = 1;
	int avg;
	int rangeSpan;

	if (CItem_IsValueless(self))
		return 0;

	if ((CWeapon_GetMaxAC(self) & 0xFF) > 0) {
		// Armor path
		weaponValue *= (CWeapon_GetMaxAC(self) & 0xFF);
		weaponValue *= (CWeapon_GetHandedness(self) + 4);
		weaponValue /= 2;
	} else {
		// Weapon path
		avg = CDiceRoll_Average(CWeapon_GetDamageDice(self));
		avg += ((CItem_GetMeleeRange(self) & 0xFF) > 1) ? 1 : 0;
		if (avg != 0)
			rangeSpan = (CItem_GetMeleeRange(self) & 0xFF) - (CItem_GetMinRange(self) & 0xFF);
		else
			rangeSpan = 0;
		weaponValue *= rangeSpan;
		weaponValue *= (CItem_GetSpeed(self) & 0xFF);
		weaponValue /= 8;
	}

	weaponValue += (CWeapon_GetCurHP(self) & 0xFF) / 6;

	result = CItem_GetValue_VT(self, vendor, 0);
	result += weaponValue;

	if (normalize)
		result = CItem_NormalizeValue(self, result);

	return result;
}

/*
 * 0x004DFC90 - CWeaponDef::SetDamageDice
 *
 * Copies the damage dice into the definition's weaponClass slot.
 */
void
CWeaponDef_SetDamageDice(CWeaponDef *def, CWeaponDice *src)
{
	CDiceRoll_Copy(&def->weaponClass, src);
}

/*
 * 0x004DFCAC - CWeaponDef::SetACDice
 *
 * Copies the AC dice into the definition's armorClass slot.
 */
void
CWeaponDef_SetACDice(CWeaponDef *def, CWeaponDice *src)
{
	CDiceRoll_Copy(&def->armorClass, src);
}

/*
 * 0x004DFCC8 - CWeaponDef::SetDurabilityDice
 *
 * Copies the durability dice into the definition's hitPoints slot.
 */
void
CWeaponDef_SetDurabilityDice(CWeaponDef *def, CWeaponDice *src)
{
	CDiceRoll_Copy(&def->hitPoints, src);
}

/*
 * 0x004DFCE4 - CWeaponManager::LookupWeaponDef
 *
 * Returns the CWeaponDef stored at slot id, or NULL if unset.
 */
CWeaponDef *
CWeaponManager_LookupWeaponDef(CWeaponManager *mgr, int id)
{
	CWeaponArray_Exists(mgr, id & 0xFF);
	return CWeaponArray_GetAt(mgr, id & 0xFF);
}

/*
 * 0x004DFD14 - CItem::ResolveMaterialType
 *
 * Looks up the weapon definition for the item and delegates to
 * CWeaponDef_ResolveMaterialType to find/cache the material resource type.
 */
uint32_t
CItem_ResolveMaterialType(CItem *item)
{
	CWeaponDef *def;

	def = CWeaponManager_LookupWeaponDef(&g_WeaponManager, ((CContainer *)item)->weaponClass);
	return CWeaponDef_ResolveMaterialType(def, item);
}

/*
 * 0x004DFD3B - CWeaponDef::ResolveMaterialType
 *
 * Returns the cached material resource type for the weapon. On first call
 * for an item, probes for bone, metal, wood, leather, then cloth and caches
 * the first hit in def->reserved; if none match, caches g_ResTypeId_Weapon
 * as the sentinel and returns 0xFFFF.
 */
uint32_t
CWeaponDef_ResolveMaterialType(CWeaponDef *def, CItem *item)
{
	if (def->reserved == (uint32_t)g_ResTypeId_Weapon)
		return 0xFFFF;

	if (def->reserved != 0xFFFF)
		return def->reserved;

	if (CItem_GetResourceAmountByName(item, g_ResTypeId_Bone) > 0) {
		def->reserved = g_ResTypeId_Bone;
		return def->reserved;
	}

	if (CItem_GetResourceAmountByName(item, g_ResTypeId_Metal) > 0) {
		def->reserved = g_ResTypeId_Metal;
		return def->reserved;
	}

	if (CItem_GetResourceAmountByName(item, g_ResTypeId_Wood) > 0) {
		def->reserved = g_ResTypeId_Wood;
		return def->reserved;
	}

	if (CItem_GetResourceAmountByName(item, g_ResTypeId_Leather) > 0) {
		def->reserved = g_ResTypeId_Leather;
		return def->reserved;
	}

	if (CItem_GetResourceAmountByName(item, g_ResTypeId_Cloth) > 0) {
		def->reserved = g_ResTypeId_Cloth;
		return def->reserved;
	}

	def->reserved = g_ResTypeId_Weapon;
	return 0xFFFF;
}

/*
 * 0x004DFE33 - CWeaponDef::GetDamageDicePtr
 *
 * Thiscall. Returns pointer to damage dice at this+1.
 */
CWeaponDice *
CWeaponDef_GetDamageDicePtr(CWeaponDef *def)
{
	return &def->weaponClass;
}

/*
 * 0x004DFE44 - CWeaponDef::GetACDicePtr
 *
 * Thiscall. Returns pointer to AC dice at this+4.
 */
CWeaponDice *
CWeaponDef_GetACDicePtr(CWeaponDef *def)
{
	return &def->armorClass;
}

/*
 * 0x004DFE55 - CWeaponDef::GetDurabilityDicePtr
 *
 * Thiscall. Returns pointer to durability dice at this+7.
 */
CWeaponDice *
CWeaponDef_GetDurabilityDicePtr(CWeaponDef *def)
{
	return &def->hitPoints;
}

/*
 * 0x004DFE66 - CWeaponManager::IsValidId
 *
 * Returns 1 when id is in the legal weapon-id range [0, 255).
 */
int
CWeaponManager_IsValidId(CWeaponManager *mgr, int id)
{
	USED(mgr);
	if (id < 0)
		return 0;
	if (id < 0xFF)
		return 1;
	return 0;
}

/*
 * 0x004DFE8B - CItem::LoadWeaponDef
 *
 * Re-derives the weapon def id from the item's tiledata quantity and
 * binds it via CWeapon_SetWeaponDef. The arg is pushed by the dynamic
 * loader but ignored here.
 */
int
CItem_LoadWeaponDef(CItem *item, int arg)
{
	int id;

	USED(arg);
	id = CItem_GetWordProp(item) & 0xFFFF;
	if (!CWeaponManager_IsValidId(&g_WeaponManager, id))
		return 0;
	return CWeapon_SetWeaponDef(item, (uint8_t)id);
}

/*
 * 0x004DFECC - CWeapon::SetWeaponDef
 *
 * Binds the weapon to a definition id, or stores 0xFF and returns 0
 * if no entry for id exists in g_WeaponManager.
 */
int
CWeapon_SetWeaponDef(CItem *item, uint8_t id)
{
	CContainer *cont = (CContainer *)item;

	if (!CWeaponManager_WeaponDefExists(&g_WeaponManager, id)) {
		cont->weaponClass = 0xFF;
		return 0;
	}
	cont->weaponClass = id;
	return 1;
}

/*
 * 0x004DFF04 - CWeaponManager::WeaponDefExists
 *
 * Returns 1 when slot id holds a loaded weapon definition.
 */
int
CWeaponManager_WeaponDefExists(CWeaponManager *mgr, int id)
{
	return CWeaponArray_Exists(mgr, id & 0xFF);
}

/*
 * 0x004DFF22 - CWeapon::Create
 *
 * Allocates and constructs a CWeapon for bodyType, then loads its stats from
 * the global weapon manager using its word property as the def ID. Returns
 * NULL (and tears the item down) when loading fails.
 */
CItem *
CWeapon_Create(uint16_t bodyType)
{
	CItem *item;

	item = (CItem *)malloc(sizeof(CContainer));
	if (item != NULL)
		item = CWeapon_Constructor(item, bodyType);
	else
		item = NULL;
	if (item != NULL) {
		if (!CWeapon_LoadFromManager(item, CItem_GetWordProp(item))) {
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);
			item = NULL;
		}
	}
	return item;
}

/*
 * 0x004DFFD0 - CWeaponDef::scalar deleting destructor
 *
 * Runs the destructor and, when flags&1 is set, frees the storage.
 * Standard scalar-deleting-destructor pattern.
 */
CWeaponDef *
CWeaponDef_ScalarDeletingDtor(CWeaponDef *def, int flags)
{
	CWeaponDef_Destructor(def);
	if (flags & 1)
		free(def);
	return NULL;
}

/*
 * 0x004E0030 - CArray<CWeaponDef*>::CArray
 *
 * Allocates the backing pointer array of the given capacity and
 * zeroes every slot.
 */
CWeaponManager *
CWeaponArray_Constructor(CWeaponManager *mgr, int capacity)
{
	int i;

	mgr->data = NULL;
	mgr->capacity = capacity;
	mgr->data = (CWeaponDef **)malloc(capacity * sizeof(CWeaponDef *));
	for (i = 0; i < mgr->capacity; i++)
		mgr->data[i] = NULL;
	return mgr;
}

/*
 * 0x004E00A0 - CArray<CWeaponDef*>::GetAt
 *
 * Returns data[index] after a (discarded) Exists check.
 */
CWeaponDef *
CWeaponArray_GetAt(CWeaponManager *mgr, int id)
{
	CWeaponArray_Exists(mgr, id);
	return mgr->data[id];
}

/*
 * 0x004E00D0 - CArray<CWeaponDef*>::Exists
 *
 * Returns non-zero when index is in range and data[index] is set.
 */
int
CWeaponArray_Exists(CWeaponManager *mgr, int id)
{
	if (!CWeaponArray_EnsureCapacity(mgr, id))
		return 0;
	return mgr->data[id] != NULL;
}

/*
 * 0x004E0110 - CArray<CWeaponDef*>::Remove
 *
 * Frees the entry at index via the scalar deleting destructor and
 * clears the slot. Returns 0 if the slot was empty.
 */
int
CWeaponArray_Remove(CWeaponManager *mgr, int id)
{
	CWeaponDef *def;

	if (!CWeaponArray_Exists(mgr, id))
		return 0;
	def = mgr->data[id];
	if (def != NULL)
		CWeaponDef_ScalarDeletingDtor(def, 1);
	mgr->data[id] = NULL;
	return 1;
}

/*
 * 0x004E0180 - CArray<CWeaponDef*>::RemoveAll
 *
 * Removes every weapon definition in the array.
 */
void
CWeaponArray_RemoveAll(CWeaponManager *mgr)
{
	int i;

	for (i = 0; i < mgr->capacity; i++)
		CWeaponArray_Remove(mgr, i);
}

/*
 * 0x004E01C0 - CArray<CWeaponDef*>::SetAt
 *
 * Stores def at index when the slot is empty. Returns 0 if a value
 * is already present (insert-only, never overwrite).
 */
int
CWeaponArray_SetAt(CWeaponManager *mgr, int id, CWeaponDef *def)
{
	CWeaponArray_EnsureCapacity(mgr, id);
	if (CWeaponArray_Exists(mgr, id))
		return 0;
	mgr->data[id] = def;
	return 1;
}

/*
 * 0x004E0200 - CArray<CWeaponDef*>::~CArray
 *
 * Removes every weapon definition, then frees the backing array.
 */
void
CWeaponArray_Destructor(CWeaponManager *mgr)
{
	CWeaponArray_RemoveAll(mgr);
	free(mgr->data);
}

/*
 * 0x004E0230 - CArray<CWeaponDef*>::EnsureCapacity
 *
 * Returns 1 when index is in [0, capacity). The fixed-size array
 * never grows, so the name is historical.
 */
int
CWeaponArray_EnsureCapacity(CWeaponManager *mgr, int id)
{
	if (id < 0)
		return 0;
	if (id >= mgr->capacity)
		return 0;
	return 1;
}

/*
 * 0x004E0270 - CWeaponDef::GetId
 *
 * Thiscall. Returns byte at this+0 (weapon def ID).
 */
uint8_t
CWeaponDef_GetId(CWeaponDef *def)
{
	return def->id;
}

/*
 * 0x004E0280 - CWeaponDef::SetId
 */
void
CWeaponDef_SetId(CWeaponDef *def, uint8_t val)
{
	def->id = val;
}

/*
 * 0x004E02A0 - CWeaponDef::GetAmmoType
 */
uint16_t
CWeaponDef_GetAmmoType(CWeaponDef *def)
{
	return def->ammoType;
}

/*
 * 0x004E02C0 - CWeaponDef::SetAmmoType
 */
void
CWeaponDef_SetAmmoType(CWeaponDef *def, uint16_t val)
{
	def->ammoType = val;
}

/*
 * 0x004E02E0 - CWeaponDef::GetBow
 */
uint8_t
CWeaponDef_GetBow(CWeaponDef *def)
{
	return def->bow;
}

/*
 * 0x004E0300 - CWeaponDef::SetBow
 */
void
CWeaponDef_SetBow(CWeaponDef *def, uint8_t val)
{
	def->bow = val;
}

/*
 * 0x004E0320 - CWeaponDef::GetHandedness
 */
uint8_t
CWeaponDef_GetHandedness(CWeaponDef *def)
{
	return def->handedness;
}

/*
 * 0x004E0340 - CWeaponDef::GetTypeFlags
 *
 * Returns the weapon-type bitmask.
 */
uint8_t
CWeaponDef_GetTypeFlags(CWeaponDef *def)
{
	return def->typeFlags;
}

/*
 * 0x004E0360 - CWeaponDef::GetRange
 */
uint8_t
CWeaponDef_GetRange(CWeaponDef *def)
{
	return def->range;
}

/*
 * 0x004E0380 - CWeaponDef::SetRange
 */
void
CWeaponDef_SetRange(CWeaponDef *def, uint8_t val)
{
	def->range = val;
}

/*
 * 0x004E03A0 - CWeaponDef::GetStrengthNeeded
 */
uint8_t
CWeaponDef_GetStrengthNeeded(CWeaponDef *def)
{
	return def->strengthNeeded;
}

/*
 * 0x004E03C0 - CWeaponDef::SetStrengthNeeded
 */
void
CWeaponDef_SetStrengthNeeded(CWeaponDef *def, uint8_t val)
{
	def->strengthNeeded = val;
}

/*
 * 0x004E03E0 - CWeaponDef::GetSpeed
 */
uint8_t
CWeaponDef_GetSpeed(CWeaponDef *def)
{
	return def->speed;
}

/*
 * 0x004E0400 - CWeaponDef::SetSpeed
 */
void
CWeaponDef_SetSpeed(CWeaponDef *def, uint8_t val)
{
	def->speed = val;
}

/*
 * 0x004E0420 - CWeaponDef::GetMinRange
 */
uint8_t
CWeaponDef_GetMinRange(CWeaponDef *def)
{
	return def->minRange;
}

/*
 * 0x004E0440 - CWeaponDef::SetMinRange
 */
void
CWeaponDef_SetMinRange(CWeaponDef *def, uint8_t val)
{
	def->minRange = val;
}

/*
 * 0x004E0460 - CWeaponDef::GetHitSfx
 */
uint16_t
CWeaponDef_GetHitSfx(CWeaponDef *def)
{
	return def->hitSfx;
}

/*
 * 0x004E0480 - CWeaponDef::SetHitSfx
 */
void
CWeaponDef_SetHitSfx(CWeaponDef *def, uint16_t val)
{
	def->hitSfx = val;
}

/*
 * 0x004E04A0 - CWeaponDef::GetMissSfx
 */
uint16_t
CWeaponDef_GetMissSfx(CWeaponDef *def)
{
	return def->missSfx;
}

/*
 * 0x004E04C0 - returns 1 (true)
 *
 * Type-check stub: returns true for IsPlayer, IsMobile, IsContainer, etc.
 * on classes that should report true for that check.
 */
int
vt_stub_return_1(CItem *self)
{
	USED(self);
	return 1;
}

/*
 * Header of the (never instantiated) 72-byte-entry weapon-def table from
 * the binary's unused CWeaponDefTable ctor.
 */
__extension__ typedef struct CWeaponDefTable CWeaponDefTable;
struct CWeaponDefTable {
	uint32_t entrySize; // +0x00
	void *entries;      // +0x04
};

/*
 * 0x004E04D0 - CWeaponDefTable::CWeaponDefTable
 *
 * Initializes the table with a 72-byte entry size and a NULL entries pointer.
 */
static __attribute__((unused)) CWeaponDefTable *
CWeaponDefTable_Init(CWeaponDefTable *this)
{
	this->entrySize = 0x48;
	this->entries = NULL;
	return this;
}

/*
 * 0x004E04F1 - CWeaponDefTable::~CWeaponDefTable
 *
 * No-op destructor.
 */
static __attribute__((unused)) void
CWeaponDefTable_Destructor(CWeaponDefTable *this)
{
	USED(this);
}

// 0x005F0078 - layer-to-handedness lookup for armor/shields (14 entries)
static const struct WeaponLayerEntry g_WeaponLayerHandednessTable[14] = {
	{ 6, 2 }, // helm -> righthand
	{ 10, 1 }, // neck -> lefthand
	{ 13, 5 }, // chest armor -> 5
	{ 5, 5 }, // shirt -> 5
	{ 22, 5 }, // robe -> 5
	{ 20, 5 }, // cloak -> 5
	{ 24, 3 }, // legs -> 3
	{ 4, 3 }, // pants -> 3
	{ 3, 3 }, // shoes -> 3
	{ 23, 3 }, // skirt -> 3
	{ 19, 2 }, // arms -> righthand
	{ 5, 2 }, // shirt (duplicate) -> righthand
	{ 7, 1 }, // gloves -> lefthand
	{ 2, 3 }, // left hand -> 3
};

/*
 * Helper - CItem_GetWeaponDefId
 *
 * Equipment items (weapons/armor) store a weaponDefId at CItem+0x50. For
 * non-container items, the bytes at offset 0x50 onward contain weapon-
 * specific data rather than CContainer.contents. The binary inlines this
 * read at every call site.
 */
uint8_t
CItem_GetWeaponDefId(CItem *item)
{
	CContainer *cont = (CContainer *)item;
	return cont->weaponClass;
}
