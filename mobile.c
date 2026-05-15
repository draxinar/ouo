/*
 * CMobile - base behaviour for every moving creature (NPC and player).
 *
 * Stats and skills, walking and facing, swing timer, equipment slots,
 * notoriety and aggro tracking, corpse production on death, food and
 * decay tick handling, plus vendor stock bookkeeping for shopkeepers.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "dat.h"

#include "combat.h"
#include "container.h"
#include "corpse.h"
#include "egg.h"
#include "feature.h"
#include "log.h"
#include "main.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "random.h"
#include "region.h"
#include "skill.h"
#include "taglist.h"
#include "template.h"
#include "timer.h"
#include "utils.h"
#include "vtable.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "world.h"

__extension__ typedef struct CSkillDecayManager CSkillDecayManager;

static void *EventLogger_Constructor(EventLogger *this); // 0x0046CD70
static void EventLogger_InitWrapper(void); // 0x0046CDC1
static void CMobile_PruneCombatLists(CMobile *mob); // 0x0046D812
static int CMobile_IsWalkableBodyType(CNPC *npc); // 0x0046DF8A
static int CheckWalkDir_IsSelf(CNPC *npc, uint32_t serial); // 0x0046E0F9
static int IsDoorBodyType(uint16_t bodyType); // 0x0046E11A
static int CheckWalkDir_TryOpenDoor(CNPC *npc, CItem *door); // 0x0046E35F
static void CSkillDecayManager_PopulateVector(CSkillDecayManager *mgr); // 0x0046E48B
static void CSkillDecayManager_ProcessVector(CSkillDecayManager *mgr); // 0x0046E4E1
static void InsertItemIntoCorpse(CCorpse *corpse, CItem *item);
static void DropItemAtMobLocation(CItem *item, CLocation *mobLoc);
static CCorpse *FindCorpseBySerial(uint32_t serial);
static int CalcDecayChance(int totalWeight); // 0x00471EAE
static uint16_t CMobile_GetMountBodyType(CMobile *mount); // 0x00472C4C
static void DecaySkill(CMobile *mob, int8_t skillId, int delta); // 0x00473511
static int VendorStock_GetResourceRatio(CItem *item); // 0x004C3930
static void VendorStock_RemoveResources(CItem *item, int resourceParam, CLocation *vendorLoc); // 0x004C393D
static void CMobile_VendorStock_Buy(CMobile *vendor, CItem *item, int buyCount, int resourceParam, int *goldPtr); // 0x004C39F9
static void CMobile_VendorStock_Restock(CMobile *vendor, CItem *item, int count, int *feePtr, int gold); // 0x004C3AA6
static int VendorStock_GetStockDelta(CItem *item, int subtractAmount); // 0x004C3BB6
static void VendorStock_SetStockTarget(CItem *item, int value); // 0x004C3BFF
static int Mobile_GetMeleeRange(CMobile *mob); // 0x004D764E

// Global mobile list head (binary: 0x00698850).
CMobile *g_MobileListHead;

// Mobile creation count (binary: 0x0068B39C). Incremented in CMobile_Constructor.
uint32_t g_MobileCount;

/*
 * NPC AI Combat
 */

/*
 * Recursion guard for NPC combat AI (binary: 0x006DC234, 0x006DBCE0, 0x006DBC60).
 * Prevents infinite loops when CombatInitiate triggers followers defending
 * owners, which could recursively call CNPC_StartCombatAI again.
 */
#define NPC_COMBAT_AI_MAX_DEPTH 32
int g_CombatAIDepth;
uint32_t g_CombatAINPCStack[NPC_COMBAT_AI_MAX_DEPTH];
uint32_t g_CombatAITargetStack[NPC_COMBAT_AI_MAX_DEPTH];

/*
 * 0x0061DAF8 - Guild war bitmask matrix
 *
 * Five-entry dword table that guild-war attitude logic indexes into
 * to derive the appropriate "at war with" bit for cross-guild relations.
 */
const uint32_t g_GuildWarMatrix[5] = { 0x10, 0x10, 0x18, 0x14, 0x00 };

/*
 * 0x00420E30 - CMobile::GetSerial
 *
 * Returns the mobile's entity serial.
 */
uint32_t
CMobile_GetSerial(CMobile *this)
{
	return this->container.item.serial;
}

/*
 * 0x00420FC0 - CMobile::GetFame
 *
 * Returns fame zero-extended from int16_t.
 */
int
CMobile_GetFame(CMobile *this)
{
	return (uint16_t)this->fame;
}

/*
 * 0x00420FE0
 */
int
CMobile_GetKarma(CMobile *this)
{
	return this->karma;
}

/*
 * 0x00421080 - CMobile::GetWaitStateAction
 *
 * Returns this->waitStateAction.
 */
uint32_t
CMobile_GetWaitStateAction(CMobile *this)
{
	return this->waitStateAction;
}

/*
 * 0x004493A7 - CMobile::DecayContainerContents
 *
 * Calls CItem_DecayProcess on each child in the container's list,
 * dispatching the equip-decay tick for mobiles. Reads spatialNext
 * after processing (not saved before, matching the binary).
 */
void
CMobile_DecayContainerContents(CItem *containerItem)
{
	CItem *child;

	child = ((CContainer *)containerItem)->contents;
	while (child != NULL) {
		if (((int (*)(void *))VT_FN(child, VT_IS_MOBILE2))(child))
			((void (*)(void *))VT_FN(child, VT_EQUIP_DECAY_TICK))(child);
		CItem_DecayProcess(child);
		child = child->spatialNext;
	}
}
/*
 * 0x0045E2A0 - Add a follower to owner's follower list
 *
 * Inserts at the TAIL of the doubly-linked list.
 * Sets follower->isFollower=1, follower->owner=owner,
 * owner->hasFollowers=1. Can't add self.
 */
void
CMobile_AddFollower(CMobile *owner, CMobile *follower)
{
	if (owner == follower)
		return;

	follower->prevFollower = owner->lastFollower;
	if (follower->prevFollower != NULL)
		follower->prevFollower->nextFollower = follower;

	owner->lastFollower = follower;

	if (owner->firstFollower == NULL)
		owner->firstFollower = follower;

	follower->nextFollower = NULL;

	follower->isFollower = 1;
	follower->owner = owner;
	owner->hasFollowers = 1;
}

/*
 * 0x0045E323 - Remove a follower from owner's follower list
 *
 * Standard doubly-linked list unlink. Clears follower's isFollower and
 * owner.
 *
 * FIXED: Binary does not clear owner->hasFollowers when the last follower
 * is removed, leaving the flag stale. This causes CMobile_IsInnocent
 * (0x0046D613) to check the follower list on every call even when empty.
 * Fix: clear hasFollowers when firstFollower becomes NULL.
 */
void
CMobile_RemoveFollower(CMobile *owner, CMobile *follower)
{
	if (owner->lastFollower == follower)
		owner->lastFollower = follower->prevFollower;

	if (owner->firstFollower == follower)
		owner->firstFollower = follower->nextFollower;

	if (follower->prevFollower != NULL)
		follower->prevFollower->nextFollower = follower->nextFollower;
	if (follower->nextFollower != NULL)
		follower->nextFollower->prevFollower = follower->prevFollower;

	follower->prevFollower = NULL;
	follower->nextFollower = NULL;
	follower->isFollower = 0;
	follower->owner = NULL;

	if (owner->firstFollower == NULL)
		owner->hasFollowers = 0;
}

/*
 * 0x0045E3BC - CMobile::ReleaseAllFollowers
 *
 * Removes every follower, saving the next pointer first since
 * RemoveFollower unlinks it.
 */
void
CMobile_ReleaseAllFollowers(CMobile *owner)
{
	CMobile *cur, *next;

	cur = owner->firstFollower;
	while (cur != NULL) {
		next = cur->nextFollower;
		CMobile_RemoveFollower(owner, cur);
		cur = next;
	}
}

/*
 * 0x0045E3F7 - Count followers in owner's follower list
 *
 * Walks from firstFollower to lastFollower via nextFollower.
 */
int
CMobile_CountFollowers(CMobile *owner)
{
	CMobile *cur;
	int count;

	count = 0;
	for (cur = owner->firstFollower; cur != NULL; cur = cur->nextFollower)
		count++;
	return count;
}

/*
 * 0x0045E433 - CMobile::HandleFollowerMovement
 *
 * Iterates the owner's follower linked list (offset 0x6C head, 0x64 chain).
 * For each follower, calls vtable[0x208] WalkCheck(targetLoc, 0), then on
 * success calls vtable[0x20C] DoWalk(targetLoc, -128).
 */
void
CMobile_HandleFollowerMovement(CMobile *owner, int direction)
{
	CMobile *follower;

	for (follower = owner->firstFollower; follower != NULL; follower = follower->nextFollower) {
		if (!((int (*)(void *, int, int))VT_FN((CItem *)follower, VT_WALK_CHECK))(follower, direction, 0))
			continue;
		((void (*)(void *, int, int))VT_FN((CItem *)follower, VT_DO_WALK))(follower, direction, -128);
	}
}

/*
 * 0x0046CD70 - EventLogger::EventLogger
 *
 * Thiscall constructor. Sets enabled flag to 1. Returns this.
 */
static void *
EventLogger_Constructor(EventLogger *this)
{
	this->enabled = 1;
	return this;
}

/*
 * 0x0046CD9C - EventLogger::Log
 *
 * Stub in the demo binary - always returns 0 regardless of field0 state.
 * Takes 7 arguments but does nothing.
 *
 * MODIFIED: prints a timestamped log line to stdout. For player-context
 * events (name non-empty), the name is included in the output. Format:
 *   2026-04-07 14:30:22 category: message
 *   2026-04-07 14:30:22 category: 'name' message
 */
int
EventLogger_Log(EventLogger *logger, uint32_t accountNum, uint32_t charNum, uint32_t serial, const char *name, const char *category, const char *subcategory, const char *message)
{
	time_t t;
	struct tm *tm;
	char ts[32];

	USED(charNum);
	USED(serial);
	USED(subcategory);
	if (logger->enabled == 0)
		return 0;

	t = time(NULL);
	tm = localtime(&t);
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

	if (accountNum != 0 && name[0] != '\0')
		printf("%s %s: '%s' %s\n", ts, category, name, message);
	else
		printf("%s %s: %s\n", ts, category, message);

	return 0;
}

/*
 * 0x0046CDC1 - EventLogger init wrapper
 *
 * Static-init wrapper that constructs the global g_EventLogger.
 */
static __attribute__((unused)) void
EventLogger_InitWrapper(void)
{
	EventLogger_Constructor(&g_EventLogger);
}

/*
 * Follower list management (doubly-linked list on owner)
 */

/*
 * 0x0046CDDB - Get effective stat (base + bonus), clamped to [0, 65000]
 *
 * Overflow (>65000) returns 0 - same sentinel as GetTotalSkill.
 */
int
CMobile_GetStat(CMobile *this, int type)
{
	int result;

	result = (int)(int16_t)CMobile_GetBaseStat(this, type) + (int)(int16_t)CMobile_GetStatBonus(this, type);
	if (result < 0)
		result = 0;
	else if (result > 65000)
		result = 0;
	return result;
}

/*
 * 0x0046CE32 - Get base stat value by type index
 *
 * type: 0=STR, 1=DEX, 2=INT. Returns uint16_t.
 */
uint16_t
CMobile_GetBaseStat(CMobile *this, int type)
{
	switch (type) {
	case 0:
		return this->baseStr;
	case 1:
		return this->baseDex;
	case 2:
		return this->baseInt;
	default:
		return 0;
	}
}

/*
 * 0x0046CE82 - CMobile::SetBaseStat
 *
 * Sets base STR/DEX/INT and calls the matching per-stat recalc
 * (AddMaxHp/Stamina/Mana) with the delta, then NotifyStatChange.
 */
int16_t
CMobile_SetBaseStat(CMobile *this, int type, uint16_t value)
{
	int delta;

	delta = (int)(int16_t)value - (int)(int16_t)CMobile_GetBaseStat(this, type);

	switch (type) {
	case 0:
		this->baseStr = value;
		((int (*)(void *, int))VT_FN((CItem *)this, VT_ADD_MAX_HP))(this, delta);
		break;
	case 1:
		this->baseDex = value;
		((int (*)(void *, int))VT_FN((CItem *)this, VT_ADD_MAX_STAMINA))(this, delta);
		break;
	case 2:
		this->baseInt = value;
		((int (*)(void *, int))VT_FN((CItem *)this, VT_ADD_MAX_MANA))(this, delta);
		break;
	default:
		break;
	}

	CMobile_NotifyStatChange(this, type, delta);
	return CMobile_GetBaseStat(this, type);
}

/*
 * 0x0046CF45 - CMobile::AddToBaseStat
 *
 * Adds delta to the base stat; returns the actual change applied.
 */
int16_t
CMobile_AddToBaseStat(CMobile *this, int type, int16_t delta)
{
	int old;
	int16_t result;

	old = (int)(int16_t)CMobile_GetBaseStat(this, type);
	result = CMobile_SetBaseStat(this, type, (uint16_t)(old + (int)delta));
	return (int16_t)((int)result - old);
}

/*
 * 0x0046CF88 - CMobile::GetStatBonus
 *
 * Returns the equipment bonus for STR/DEX/INT (type 0/1/2).
 */
int16_t
CMobile_GetStatBonus(CMobile *this, int type)
{
	switch (type) {
	case 0:
		return this->strBonus;
	case 1:
		return this->dexBonus;
	case 2:
		return this->intBonus;
	default:
		return 0;
	}
}

/*
 * 0x0046CFD8 - CMobile::AddToStatBonus
 *
 * Adds delta to the equipment stat bonus; returns the actual change.
 */
int16_t
CMobile_AddToStatBonus(CMobile *this, int type, int16_t delta)
{
	int old;
	int16_t result;

	old = (int)(int16_t)CMobile_GetStatBonus(this, type);
	result = CMobile_SetStatBonus(this, type, (int16_t)(old + (int)delta));
	return (int16_t)((int)result - old);
}

/*
 * 0x0046D01B - CMobile::SetStatBonus
 *
 * Sets the STR/DEX/INT equipment bonus and calls the matching
 * per-stat recalc with the delta, then NotifyStatChange.
 */
int16_t
CMobile_SetStatBonus(CMobile *this, int type, int16_t value)
{
	int delta;

	delta = (int)(int16_t)value - (int)(int16_t)CMobile_GetStatBonus(this, type);

	switch (type) {
	case 0:
		this->strBonus = value;
		((int (*)(void *, int))VT_FN((CItem *)this, VT_ADD_MAX_HP))(this, delta);
		break;
	case 1:
		this->dexBonus = value;
		((int (*)(void *, int))VT_FN((CItem *)this, VT_ADD_MAX_STAMINA))(this, delta);
		break;
	case 2:
		this->intBonus = value;
		((int (*)(void *, int))VT_FN((CItem *)this, VT_ADD_MAX_MANA))(this, delta);
		break;
	default:
		break;
	}

	CMobile_NotifyStatChange(this, type, delta);
	return CMobile_GetStatBonus(this, type);
}

/*
 * 0x0046D0DE - CMobile::NotifyStatChange
 *
 * Called after SetBaseStat/SetStatBonus: sends a status update to
 * the player and, on STR loss, re-checks equipment wieldability.
 */
void
CMobile_NotifyStatChange(CMobile *this, int statType, int delta)
{
	if (!VT_IsPlayer((CItem *)this))
		return;

	SendStatusToPlayer(this, (CPlayer *)this, this->container.item.serial, 1);

	if (statType != 0)
		return;
	if (delta >= 0)
		return;

	if (!CMobile_CheckAllEquipmentWieldable(this)) {
		Entity_AttachScript(&this->container.item, "checkeq", 1);
	}
}

/*
 * 0x0046D13E - CMobile::CheckAllEquipmentWieldable
 *
 * Iterates equipment layers 1..0x19; for each equipped weapon, calls
 * CItem_CanWield. Returns 0 if any weapon cannot be wielded, else 1.
 */
int
CMobile_CheckAllEquipmentWieldable(CMobile *this)
{
	int layer;
	CItem *item;

	for (layer = 1; layer < 0x1A; layer++) {
		if (this->equipment[layer] == NULL)
			continue;
		item = this->equipment[layer];
		if (!VT_IsWeapon(item))
			continue;
		if (!CItem_CanWield(item, (CItem *)this, (uint8_t)layer))
			return 0;
	}
	return 1;
}

/*
 * 0x0046D1C8 - CMobile::AddMaxHP (vtable[0x1DC], 50 bytes)
 *
 * Called when STR changes. Gets current maxHP, adds delta,
 * sets new maxHP via vtable[0x1C4] (SetMaxHP). Returns actual change.
 */
int
CMobile_AddMaxHP(CMobile *this, int delta)
{
	int oldMaxHP;

	oldMaxHP = CMobile_GetMaxHp(this);
	return ((int (*)(void *, int))VT_FN((CItem *)this, VT_SET_MAX_HP))(this, oldMaxHP + delta) - oldMaxHP;
}

/*
 * 0x0046D1FA - CMobile::AddMaxStamina (vtable[0x1E4], 50 bytes)
 *
 * Called when DEX changes. Gets current maxStamina, adds delta,
 * sets new maxStamina via vtable[0x1CC] (SetMaxStamina). Returns actual change.
 */
int
CMobile_AddMaxStamina(CMobile *this, int delta)
{
	int oldMaxStamina;

	oldMaxStamina = CMobile_GetMaxStamina(this);
	return ((int (*)(void *, int))VT_FN((CItem *)this, VT_SET_MAX_STAMINA))(this, oldMaxStamina + delta) - oldMaxStamina;
}

/*
 * 0x0046D22C - CMobile::AddMaxMana (vtable[0x1EC], 50 bytes)
 *
 * Called when INT changes. Gets current maxMana, adds delta,
 * sets new maxMana via vtable[0x1D4] (SetMaxMana). Returns actual change.
 */
int
CMobile_AddMaxMana(CMobile *this, int delta)
{
	int oldMaxMana;

	oldMaxMana = CMobile_GetMaxMana(this);
	return ((int (*)(void *, int))VT_FN((CItem *)this, VT_SET_MAX_MANA))(this, oldMaxMana + delta) - oldMaxMana;
}

/*
 * 0x0046D25E - CMobile::AddHP (vtable[0x1DC+?], 78 bytes)
 *
 * Adjusts current HP by delta. Gets current HP, calls SetHP via
 * vtable[0x1C0] with (oldHP + delta, 0). If actual change > 0,
 * calls vtable[0x22C] (SendHPUpdate). Returns actual change.
 */
int
CMobile_AddHP(CMobile *this, int delta)
{
	int oldHP, change;

	oldHP = CMobile_GetHp(this);
	change = ((int (*)(void *, int, int))VT_FN((CItem *)this, VT_SET_HP))(this, oldHP + delta, 0) - oldHP;
	if (change > 0)
		((void (*)(void *))VT_FN((CItem *)this, VT_SEND_HP_UPDATE))(this);
	return change;
}

/*
 * 0x0046D2AC - CMobile::AddStamina (vtable[0x1E0], 50 bytes)
 *
 * Adjusts current stamina by delta. Gets current stamina, adds delta,
 * sets via vtable[0x1C8] (SetStamina). Returns actual change.
 */
int
CMobile_AddStamina(CMobile *this, int delta)
{
	int oldStamina;

	oldStamina = CMobile_GetStamina(this);
	return ((int (*)(void *, int))VT_FN((CItem *)this, VT_SET_STAMINA))(this, oldStamina + delta) - oldStamina;
}

/*
 * 0x0046D2DE - CMobile::AddMana (vtable[0x1E8], 50 bytes)
 *
 * Adjusts current mana by delta. Gets current mana, adds delta,
 * sets via vtable[0x1D0] (SetMana). Returns actual change.
 */
int
CMobile_AddMana(CMobile *this, int delta)
{
	int oldMana;

	oldMana = CMobile_GetMana(this);
	return ((int (*)(void *, int))VT_FN((CItem *)this, VT_SET_MANA))(this, oldMana + delta) - oldMana;
}

/*
 * 0x0046D310 - CMobile::IsPet
 *
 * Tag-based: checks "isPet" resource tag on the entity.
 * Returns 1 if tag exists and value is non-zero, 0 otherwise.
 */
int
CMobile_IsPet(CMobile *this)
{
	int val;

	if (!CResourceEntity_HasTag((CItem *)this, "isPet", 0))
		return 0;
	CResourceEntity_GetTagInt((CItem *)this, "isPet", &val);
	return val != 0;
}

/*
 * 0x0046D34E - CMobile::HasBoss
 *
 * Returns 1 when the mobile's "myBoss" tag list exists and has at least
 * one entry, else 0.
 */
int
CMobile_HasBoss(CMobile *this)
{
	CList *list;

	list = CResourceEntity_GetTagEntity(&this->container.item, "myBoss");
	if (list == NULL)
		return 0;
	if (CList_GetCount(list) > 0)
		return 1;
	return 0;
}

/*
 * 0x0046D38A - CMobile::CheckOwner
 *
 * Returns 1 when candidateSerial is present in the mobile's "myBoss" tag
 * list, else 0.
 */
int
CMobile_CheckOwner(CMobile *this, uint32_t candidateSerial)
{
	CList *list;

	list = CResourceEntity_GetTagEntity(&this->container.item, "myBoss");
	if (list == NULL)
		return 0;
	return CList_Find(list, 4, candidateSerial);
}

/*
 * 0x0046D3C1 - CMobile::IncrementLifeclock
 *
 * Bumps the mobile's lifeclock counter by one.
 */
void
CMobile_IncrementLifeclock(CMobile *mob)
{
	mob->lifeclock++;
}

/*
 * 0x0046D3E1 - CMobile::AddToCombatTargetList
 *
 * Adds serial to combatTargetList (ignoring self); NPCs also enter
 * war mode.
 */
int
CMobile_AddToCombatTargetList(CMobile *this, uint32_t serial)
{
	if (serial == this->container.item.serial)
		return 0;

	if (VT_IsNPC((CItem *)this))
		CMobile_SetMobileFlag(this, MobileFlag_WarMode);

	return CSerialList_Add(&this->combatTargetList, serial);
}

/*
 * 0x0046D42B - CMobile::GetNaturalDamage
 *
 * Rolls the natural-damage dice tied to armorRating.
 */
int
CMobile_GetNaturalDamage(CMobile *this)
{
	return CDiceRoll_Roll((CWeaponDice *)&this->armorRating);
}

/*
 * 0x0046D444 - CMobile::AddToAttackerList
 *
 * Adds serial to the attackerList (the follower's owner if this mob
 * is a follower), ignoring self-serials.
 */
int
CMobile_AddToAttackerList(CMobile *this, uint32_t serial)
{
	CMobile *owner;

	if (serial == this->container.item.serial)
		return 0;

	if (this->isFollower) {
		owner = this->owner;
		return CSerialList_Add(&owner->attackerList, serial);
	}
	return CSerialList_Add(&this->attackerList, serial);
}

/*
 * 0x0046D498 - CMobile::StopFightWith
 *
 * Drops serial from combatTargetList (and optionally attackerList),
 * clearing war mode when an NPC ends up with no targets.
 */
void
CMobile_StopFightWith(CMobile *this, uint32_t serial, int removeFromAttackerList)
{
	if (serial == this->container.item.serial)
		return;

	if (removeFromAttackerList)
		CSerialList_Remove(&this->attackerList, serial);

	CSerialList_Remove(&this->combatTargetList, serial);

	if (VT_IsNPC((CItem *)this) && this->combatTargetList.count == 0)
		CMobile_ClearMobileFlag(this, MobileFlag_WarMode);
}

/*
 * 0x0046D50A - CMobile::DisengageAttackers
 *
 * For each attacker, removes that attacker's own combat/attacker list
 * entries (keyed on the attacker's serial, matching the binary) and
 * drops war mode on NPCs whose target list becomes empty.
 */
void
CMobile_DisengageAttackers(CMobile *this)
{
	CSerialNode *sentinel, *node;
	CItem *entity;
	CMobile *otherMob;

	sentinel = this->attackerList.data;
	for (node = sentinel->next; node != sentinel; node = node->next) {

		entity = CWorld_FindBySerial(g_World, node->serial);
		if (entity == NULL)
			continue;

		if (!VT_IsMobile(entity))
			continue;

		otherMob = (CMobile *)entity;

		CSerialList_Remove(&otherMob->combatTargetList, node->serial);

		if (VT_IsNPC((CItem *)otherMob) && otherMob->combatTargetList.count == 0)
			CMobile_ClearMobileFlag(otherMob, MobileFlag_WarMode);

		CSerialList_Remove(&otherMob->attackerList, node->serial);
	}
}

/*
 * 0x0046D61C - CMobile::StopCombat
 *
 * Clears combat lists; NPCs with no targets drop war mode.
 */
void
CMobile_StopCombat(CMobile *this)
{
	CSerialList_Clear(&this->combatTargetList);

	if (VT_IsNPC((CItem *)this) && this->combatTargetList.count == 0)
		CMobile_ClearMobileFlag(this, MobileFlag_WarMode);

	CSerialList_Clear(&this->attackerList);
}

/*
 * 0x0046D6CF - CMobile::ClearCombatTargets
 *
 * Clears combatTargetList only; NPCs with no targets drop war mode.
 */
void
CMobile_ClearCombatTargets(CMobile *this)
{
	CSerialList_Clear(&this->combatTargetList);

	if (VT_IsNPC((CItem *)this) && this->combatTargetList.count == 0)
		CMobile_ClearMobileFlag(this, MobileFlag_WarMode);
}

/*
 * 0x0046D746 - CMobile::GetSpeed
 *
 * Returns min(dex * 40 / 100 + 35, 163); callers compute the tick
 * interval as 500 / speed.
 */
int
CMobile_GetSpeed(CMobile *this)
{
	int dex, speed;

	dex = (int16_t)CMobile_GetStat(this, 1); // DEX
	speed = dex * 40 / 100 + 35;
	if (speed > 163)
		speed = 163;
	return speed;
}

/*
 * 0x0046D784 - CMobile::PaperdollTitle (vtable)
 *
 * Assigns the mob's name into title. CNPC overrides to add a job.
 */
void
CMobile_PaperdollTitle_VT(CMobile *mob, CString *title)
{
	const char *name;

	name = ((const char *(*)(void *))VT_FN((CItem *)mob, VT_GET_NAME))(mob);
	CString_AssignCStr(title, name);
}

/*
 * 0x0046D7A5 - CMobile::GetWeapon
 *
 * Returns the right-hand weapon, or the left-hand weapon if it is
 * a non-shield CWeapon, or NULL when unarmed.
 */
CItem *
CMobile_GetWeapon(CMobile *this)
{
	CItem *item;

	// Right hand (slot 1)
	item = this->equipment[1];
	if (item != NULL && VT_IsWeapon(item))
		return item;

	// Left hand (slot 2) - must be a weapon and not a shield
	item = this->equipment[2];
	if (item != NULL && VT_IsWeapon(item) && !CItem_IsShield(item))
		return item;

	return NULL;
}

/*
 * 0x0046D812 - CMobile::PruneCombatLists
 *
 * Drops TTL-expired entries from attackerList and combatTargetList.
 * Invoked every 4 ticks from CombatHeartBeat.
 */
static void
CMobile_PruneCombatLists(CMobile *mob)
{
	CSerialList_PruneExpired(&mob->attackerList);
	CSerialList_PruneExpired(&mob->combatTargetList);
}

/*
 * 0x0046D839 - CMobileManager::CombatHeartBeat
 *
 * Runs SwingResolve on every live mobile each tick (skipping
 * multi-slaves and the all-frozen-NPC case), and prunes combat
 * lists every fourth tick.
 */
void
CMobileManager_CombatHeartBeat(uint32_t tickCount)
{
	CMobile *mob;
	int skipCombat;

	if (g_nextCombatMobile != NULL) {
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "badness", "error", "CMobileManager::CombatHeartBeat() nextCombatMobile not NULL!");
	}

	for (mob = g_MobileListHead; mob != NULL; mob = g_nextCombatMobile) {
		g_nextCombatMobile = mob->nextMobile;

		if (VT_IsRemoved((CItem *)mob))
			continue;

		if (CMultiSlave_GetTypeId((CMultiSlave *)mob))
			continue;

		skipCombat = 0;
		if (VT_IsNPC((CItem *)mob)) {
			CNPC *npc = (CNPC *)mob;
			if (CNPC_IsFrozen(npc) && npc->frozenCombatFlag != 0) {
				StdPtrNode *fvmIter, *fvmEndTemp;
				CItem *tgt;

				CSerialList_FindFirstValidMobile((StdPtrList *)&mob->combatTargetList, &fvmIter);
				if (StdPtrIter_Neq(&fvmIter, StdPtrList_End((StdPtrList *)&mob->combatTargetList, &fvmEndTemp)) & 0xFF) {
					uint32_t ts = (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&fvmIter));
					tgt = CWorld_FindBySerial(g_World, ts);
					if (tgt != NULL && VT_IsNPC(tgt) && CNPC_IsFrozen((CNPC *)tgt))
						skipCombat = 1;
				}
			}
		}

		if (skipCombat)
			continue;

		if (tickCount & 4)
			CMobile_PruneCombatLists(mob);
		CMobile_SwingResolve(mob);
	}
	g_nextCombatMobile = NULL;
}

/*
 * 0x0046D9B2 - CMobile::OnDeath_Wrap
 *
 * Dispatches the virtual OnDeath(attacker, deathFlag).
 */
void
CMobile_OnDeath_Wrap(CMobile *this, uintptr_t attacker, int deathFlag)
{
	((void (*)(void *, uintptr_t, int))VT_FN((CItem *)this, VT_ON_DEATH))(this, attacker, deathFlag);
}

/*
 * 0x0046D9D5 - CMobile::SpeakSysMsg (vtable)
 *
 * Returns mob->name; ignores the flag argument.
 */
char *
CMobile_SpeakSysMsg_VT(CItem *self, int flag)
{
	CMobile *mob = (CMobile *)self;

	USED(flag);
	return mob->name;
}

/*
 * 0x0046D9EB - CMobile::GetDirection
 *
 * Returns this->direction.
 */
uint32_t
CMobile_GetDirection(CMobile *this)
{
	return this->direction;
}

/*
 * 0x0046D9FF - CMobile::SetDirection
 *
 * Updates the facing direction. Ships (multi owners) rotate the
 * multi via CanExistAt/MoveMulti; other mobs just store dir.
 */
void
CMobile_SetDirection(CItem *mob, uint32_t dir)
{
	CMobile *m = (CMobile *)mob;

	if (m->direction == dir)
		return;

	if (CItem_IsMultiOwner(mob) == 1) {
		CMultiSlave *slave;
		int typeId, group, baseType, newType;
		uint8_t moveType;

		slave = CItem_GetMultiSlave(mob);
		typeId = CMultiSlave_GetTypeId(slave);
		// Signed division by 8 with rounding toward negative infinity
		group = (typeId - 0x7d0);
		if (group < 0)
			group += 7;
		group >>= 3;
		baseType = group * 8 + 0x7d0;
		newType = baseType + (dir & 0x7f);

		moveType = ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF;

		if (CMultiManager_CanExistAt(&g_MultiManager, (uint16_t)newType, &mob->resourceEntity.entity.location, moveType, mob)) {
			m->direction = dir;
			CMultiManager_MoveMulti(&g_MultiManager, (uint16_t)newType, CItem_GetMultiSlave(mob), &mob->resourceEntity.entity.location, mob);
		}
	} else {
		m->direction = dir;
	}
}

/*
 * 0x0046DAD9 - CMobile::SetName
 *
 * Copies newName into the name buffer and refreshes the player's
 * status bar.
 */
void
CMobile_SetName(CMobile *this, char *newName)
{
	StringAssign(&this->name, newName);

	if (VT_IsPlayer((CItem *)this))
		SendStatusToPlayer(this, (CPlayer *)this, this->container.item.serial, 1);
}

/*
 * 0x0046DB24 - CMobile::SetArmorRating
 *
 * Copies dice into armorRating and refreshes a player's status bar.
 */
void
CMobile_SetArmorRating(CMobile *this, CWeaponDice *dice)
{
	CDiceRoll_Copy((CWeaponDice *)&this->armorRating, dice);
	if (VT_IsPlayer((CItem *)this))
		SendStatusToPlayer(this, (CPlayer *)this, this->container.item.serial, 1);
}

/*
 * 0x0046DB6B - CMobile::GetArmorRating
 *
 * Returns &this->armorRating.
 */
CWeaponDice *
CMobile_GetArmorRating(CMobile *this)
{
	return (CWeaponDice *)&this->armorRating;
}

/*
 * 0x0046DB7E - CMobile::SetBonusAC
 *
 * Sets bonusAC and refreshes a player's status bar.
 */
void
CMobile_SetBonusAC(CMobile *this, int ac)
{
	this->bonusAC = ac;
	if (VT_IsPlayer((CItem *)this))
		SendStatusToPlayer(this, (CPlayer *)this, this->container.item.serial, 1);
}

/*
 * 0x0046DBBF - CMobile::GetBonusAC
 *
 * Returns the equipment/effects bonus AC (not the armorRating dice,
 * which is only used by GetNaturalDamage).
 */
int
CMobile_GetBonusAC(CMobile *this)
{
	return this->bonusAC;
}

/*
 * 0x0046DBD3 - CMobile::GetMovementType
 *
 * Returns this->movementType.
 */
uint8_t
CMobile_GetMovementType(CMobile *this)
{
	return this->movementType;
}

/*
 * 0x0046DBE7 - CMobile::SetMovementType
 *
 * Stores the movementType (1=normal, 3=medium, 5=fast).
 */
void
CMobile_SetMovementType(CMobile *this, uint8_t type)
{
	this->movementType = type;
}

/*
 * 0x0046DC00 - CMobile::ModifySkillBonus
 *
 * Adds delta to skillBonuses[skillId], pushes the skill update, and
 * returns the change applied.
 */
int
CMobile_ModifySkillBonus(CMobile *this, int8_t skillId, int delta)
{
	int old;
	int newVal;

	if (!CSkillManager_HasSkill(&g_SkillManager, (int)skillId))
		return 0;
	old = (int)(int16_t)this->skillBonuses[(int)(int8_t)skillId];
	newVal = old + delta;
	this->skillBonuses[(int)(int8_t)skillId] = (int16_t)newVal;
	CSkillManager_SendSkillUpdate(this, skillId);
	return newVal - old;
}

/*
 * 0x0046DC72 - CMobile::AddToSkill
 *
 * Adds delta to skills[skillId] clamped to [0, 0xFFFF], sends the
 * skill update, and returns the actual change.
 */
int
CMobile_AddToSkill(CMobile *this, int8_t skillId, int delta)
{
	int oldSkill, newSkill;

	if (!CSkillManager_HasSkill(&g_SkillManager, skillId))
		return 0;

	oldSkill = (int)(uint16_t)this->skills[skillId];
	newSkill = oldSkill + delta;
	if (newSkill < 0)
		newSkill = 0;
	if (newSkill > 0xFFFF)
		newSkill = 0xFFFF;

	this->skills[skillId] = (uint16_t)newSkill;

	CSkillManager_SendSkillUpdate(this, skillId);

	return newSkill - oldSkill;
}

/*
 * 0x0046DCFD
 */
uint16_t
CMobile_SetSkill(CMobile *this, int8_t skillNumber, uint16_t value)
{
	if (CSkillManager_HasSkill(&g_SkillManager, (int)skillNumber) != 1)
		return 0;
	this->skills[(int)(int8_t)skillNumber] = value;
	return this->skills[(int)(int8_t)skillNumber];
}

/*
 * 0x0046DD40 - CMobile::SetSkillBonus
 *
 * Sets skillBonuses[skillId] = value if skill exists.
 * Returns new bonus value, or 0 if skill invalid.
 */
int16_t
CMobile_SetSkillBonus(CMobile *this, int8_t skillId, int16_t value)
{
	if (CSkillManager_HasSkill(&g_SkillManager, (int)skillId) != 1)
		return 0;
	this->skillBonuses[(int)(int8_t)skillId] = value;
	return this->skillBonuses[(int)(int8_t)skillId];
}

/*
 * 0x0046DD88 - CMobile::GetSkillBonus
 *
 * Get skill bonus value only.
 */
int
CMobile_GetSkillBonus(CMobile *this, int8_t skillId)
{
	if (CSkillManager_HasSkill(&g_SkillManager, (int)skillId) != 1)
		return 0;
	return (int16_t)this->skillBonuses[(int)(int8_t)skillId];
}

/*
 * 0x0046DDBD - CMobile::GetTotalSkill
 *
 * Get total skill value (base + bonus). Resets to 0 if out of [0, 65000].
 */
int
CMobile_GetTotalSkill(CMobile *this, int8_t skillId)
{
	int total;

	if (CSkillManager_HasSkill(&g_SkillManager, (int)skillId) != 1)
		return 0;
	total = (int)(uint16_t)this->skills[(int)(int8_t)skillId] + (int)(int16_t)this->skillBonuses[(int)(int8_t)skillId];
	if (total < 0)
		total = 0;
	if (total > 65000)
		total = 0; // binary: overflow clamp resets to 0
	return total;
}

/*
 * 0x0046DE28 - CMobile::GetBaseSkill
 *
 * Get base skill value only (no bonuses).
 */
int
CMobile_GetBaseSkill(CMobile *this, int8_t skillId)
{
	if (CSkillManager_HasSkill(&g_SkillManager, (int)skillId) != 1)
		return 0;
	return (uint16_t)this->skills[(int)(int8_t)skillId];
}

/*
 * 0x0046DE5B - GetCreatureHeight
 *
 * Returns the template's creatureHeight, falling back to the
 * g_TemplateFlags bits (0/1/2 -> 1/3/5). Returns 1 for 0xFFFF.
 */
int
GetCreatureHeight(uint16_t templateId)
{
	NPCTemplate *tmpl;
	int height;
	int flag;

	if (templateId == 0xFFFF)
		return 1;

	if (CResManager_HasByInt(Spawn_GetTemplatesRM(), (uint32_t)templateId)) {
		tmpl = CResManager_GetTemplateByID(templateId);
		height = tmpl->creatureHeight;
		if (height != -1)
			return height;
	}

	flag = g_TemplateFlags[templateId] & 3;
	switch (flag) {
	case 0:
		return 1;
	case 1:
		return 3;
	case 2:
		return 5;
	default:
		return 1;
	}
}

/*
 * 0x0046DEED - CMobile::InitMovementTypeFromTemplate
 *
 * Stores movementType from the template's creature-height value.
 */
void
CMobile_InitMovementTypeFromTemplate(CMobile *this)
{
	uint16_t tmplIdx;

	tmplIdx = (uint16_t)CResourceEntity_GetTemplateIndex((CItem *)this);
	this->movementType = (uint8_t)GetCreatureHeight(tmplIdx);
}

/*
 * 0x0046DF17 - CMobile::FindInTagList (vtable)
 *
 * Returns the first equipped item whose tag list contains value,
 * or NULL.
 */
CItem *
CMobile_FindInTagList_VT(CMobile *self, CString *name, uint32_t value)
{
	int i;
	CItem *result;

	for (i = 0; i < 0x1A; i++) {
		if (self->equipment[i] == NULL)
			continue;
		result = ((CItem * (*)(CItem *, CString *, uint32_t)) VT_FN(self->equipment[i], VT_FIND_IN_TAG_LIST))(self->equipment[i], name, value);
		if (result != NULL)
			return result;
	}

	return NULL;
}

/*
 * 0x0046DF8A - CMobile::IsWalkableBodyType
 *
 * Returns 1 if the entity's bodyType can walk, 0 otherwise.
 */
static int
CMobile_IsWalkableBodyType(CNPC *npc)
{
	uint16_t bt = npc->mobile.container.item.resourceEntity.entity.bodyType;
	uint32_t val = bt;

	if (val <= 7) {
		if (val == 7 || val == 1)
			return 1;
		if (val <= 2)
			return 0;
		if (val <= 4)
			return 1;
		return 0;
	}
	if (val <= 0x18) {
		if (val == 0x18 || val == 0x0A)
			return 1;
		return 0;
	}
	if (val <= 0x24) {
		if (val >= 0x23)
			return 1;
		if (val < 0x1D)
			return 0;
		if (val <= 0x1E)
			return 1;
		return 0;
	}
	if (val <= 0x39) {
		if (val >= 0x36)
			return 1;
		if (val < 0x27 || val > 0x2D)
			return 0;
		// Switch on val - 0x27 (0=0x27, 1=0x28, ..., 6=0x2D)
		switch (val - 0x27) {
		case 0:
			return 1; // 0x27
		case 1:
			return 0; // 0x28
		case 2:
			return 1; // 0x29
		case 3:
			return 0; // 0x2A
		case 4:
			return 0; // 0x2B
		case 5:
			return 1; // 0x2C
		case 6:
			return 1; // 0x2D
		}
	}
	if (val >= 0x190 && val <= 0x191)
		return 1;
	return 0;
}

/*
 * 0x0046E0F9 - CheckWalkDir_IsSelf
 *
 * Verifies the NPC's serial still resolves to itself after door use.
 */
static int
CheckWalkDir_IsSelf(CNPC *npc, uint32_t serial)
{
	CItem *found = CWorld_FindBySerial(g_World, serial);
	if (found == (CItem *)npc)
		return 1;
	return 0;
}

/*
 * 0x0046E11A - IsDoorBodyType
 *
 * Returns 1 when bodyType lies in one of the six door graphic ranges.
 */
static int
IsDoorBodyType(uint16_t bodyType)
{
	uint32_t val = bodyType & 0xFFFF;

	if (val >= 0x334 && val <= 0x342)
		return 1;
	if (val >= 0x314 && val <= 0x322)
		return 1;
	if (val >= 0x324 && val <= 0x332)
		return 1;
	if (val >= 0x354 && val <= 0x362)
		return 1;
	if (val >= 0x344 && val <= 0x352)
		return 1;
	if (val >= 0xE8 && val <= 0xF6)
		return 1;
	return 0;
}

/*
 * 0x0046E1F4 - CMobile::CheckWalkDir
 *
 * Tests whether an NPC can walk in dir. Returns 0=proceed, 1=door
 * opened (retry), 2=blocked/locked, 3=self-check failed after door.
 */
int
CMobile_CheckWalkDir(CNPC *npc, int dir)
{
	CLocation loc;
	int16_t minZ, maxZ;
	int height;
	uint32_t serial;
	CItem *found;

	if (!CMobile_IsWalkableBodyType(npc))
		return 0;

	CLocation_SetLoc(&loc, &npc->mobile.container.item.resourceEntity.entity.location);
	CLocation_MoveDir(&loc, dir);

	if (CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &loc, 0) == -1)
		return 2;

	// Binary calls vtable[0x28] (GetHeight) twice: once for minZ, once for maxZ
	height = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_HEIGHT))(npc);
	minZ = loc.z - (int16_t)(height / 2);
	height = ((int (*)(void *))VT_FN((CItem *)npc, VT_GET_HEIGHT))(npc);
	maxZ = loc.z + (int16_t)(height / 2);
	serial = CMobile_GetSerial(&npc->mobile);

	found = CTerrainManager_FindDoorAtLoc(&loc, minZ, maxZ);

	if (found == NULL)
		return 0;

	if (((int (*)(void *))VT_FN(found, VT_IS_DOOR_NS))(found) != 1)
		return 0;

	if (IsDoorBodyType(CEntity_GetBodyType(found))) {
		if (((int (*)(void *))VT_FN((CItem *)npc, VT_CHECK_EC))(npc))
			return 0;
		if (VT_IsNPC((CItem *)npc))
			return 0;
	}

	if (CItem_IsLocked(found))
		return 2;

	if (CheckWalkDir_TryOpenDoor(npc, found)) {
		if (CheckWalkDir_IsSelf(npc, serial))
			return 1;
		return 3;
	}
	return 2;
}

/*
 * 0x0046E35F - CheckWalkDir_TryOpenDoor
 *
 * Fires door event 0x17 with the NPC's serial and, on approval,
 * calls UseDoor. Returns 1 if the door was opened.
 */
static int
CheckWalkDir_TryOpenDoor(CNPC *npc, CItem *door)
{
	uint32_t npcSerial = CMobile_GetSerial(&npc->mobile);
	char *result;

	result = Entity_ExecuteEvent(&door->resourceEntity.entity, 0x17, npcSerial);
	if (result != NULL) {
		UseDoor(door);
		return 1;
	}
	return 0;
}

/*
 * 0x0046E3B6 - CMobile::BecomeTemplate
 *
 * Re-applies templateId. When flag is set, first strips equipment
 * (keeping the backpack), runs DeleteCheck2, and resets stats.
 * Returns 1 on success, 0 on failure.
 */
int
CMobile_BecomeTemplate(CMobile *mob, int templateId, int flag)
{
	CMobile *baseMob;
	int i;
	CItem *item;
	CLocation *loc;

	baseMob = mob;

	if (flag != 0) {
		// Strip equipment (slots 0-29, skip slot 0x15 = backpack)
		for (i = 0; i < 0x1e; i++) {
			if (i == 0x15)
				continue;
			if (mob->equipment[i] == NULL)
				continue;
			item = mob->equipment[i];
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
			if (item == NULL)
				continue;
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);
		}

		if (!CItem_DeleteCheck2((CItem *)mob))
			return 0;

		CTemplateManager_ResetMobile(baseMob);
	}

	loc = &mob->container.item.resourceEntity.entity.location;

	{
		CItem *createResult = CTemplateManager_CreateFromTemplate(templateId, loc, 0, 1, (CItem *)mob);
		if (createResult == NULL)
			return 0;
		if (createResult != (CItem *)baseMob)
			return 0;
	}

	return 1;
}

// Orphaned skill/stat decay manager struct (functions at 0x0046E48B, 0x0046E4E1).
// Neither function has callers in the binary - compiled but never executed.
__extension__ typedef struct CSkillDecayManager {
	CMobile *mobHead;       // 0x00: head of CMobile linked list
	CVector serials;        // 0x04: vector of mob serials to process
} CSkillDecayManager;

/*
 * 0x0046E48B - CSkillDecayManager::PopulateVector
 *
 * Fills the serial vector from the mob list when empty. Orphaned
 * dead code in the binary.
 */
static void __attribute__((unused))
CSkillDecayManager_PopulateVector(CSkillDecayManager *mgr)
{
	CMobile *cur;
	uint32_t serial;

	if (!CVector_IsEmpty(&mgr->serials))
		return;

	cur = mgr->mobHead;
	while (cur != NULL) {
		serial = CMobile_GetSerial(cur);
		CVector_PushBack(&mgr->serials, serial);
		cur = cur->nextMobile;
	}
}

/*
 * 0x0046E4E1 - CSkillDecayManager::ProcessVector
 *
 * Processes up to 50 mobs from the CVector for skill/stat decay. For each
 * serial: finds the mob, runs CMobile_PostSkillGain(mob, 0x1388) and
 * CMobile_StatDecayCheck(mob, 0x1F4) on it, sends the full skill list to
 * the player when either reports a change, and pops the entry from the
 * back of the vector. Orphaned dead code (zero callers).
 */
static void __attribute__((unused))
CSkillDecayManager_ProcessVector(CSkillDecayManager *mgr)
{
	uint32_t *iter;
	uint32_t *beginIter;
	uintptr_t *popOut;
	uint32_t count;
	uint32_t *type;
	CItem *entity;
	CMobile *mob;
	int gainResult;
	int decayResult;

	if (CVector_IsEmpty(&mgr->serials))
		return;

	count = 0;
	CMultiComponent_GetIterator(&mgr->serials, &iter);

	for (;;) {
		CEntity_BeginIter((CSearchCtx *)&mgr->serials, &beginIter);
		if (CItem_IsSameType((CSearchCtx *)&iter, (CSearchCtx *)&beginIter) == 0)
			break;

		type = (uint32_t *)CItem_GetType((CItem *)&iter);
		entity = CWorld_FindBySerial(g_World, *type);

		if (entity != NULL && VT_IsMobile(entity)) {
			mob = (CMobile *)entity;
			gainResult = CMobile_PostSkillGain(mob, 0x1388);
			decayResult = CMobile_StatDecayCheck(mob, 0x1F4);

			if (decayResult || gainResult)
				CSkillManager_SendSkillList(&g_SkillManager, (CItem *)mob);

			count++;
		}

		CVector_PopBack(&iter, &popOut);
		CVector_EraseBack(&mgr->serials);

		if (count >= 50)
			break;
	}
}

/*
 * 0x0046E5E0 - CMobile::GetSpeechHue
 *
 * Returns the mobile's default speech hue.
 */
uint16_t
CMobile_GetSpeechHue(CMobile *this)
{
	return this->speechHue;
}

/*
 * 0x0046E5F5 - CMobile::SetSpeechHue
 *
 * Sets the default speech hue (offset 0x28C).
 */
void
CMobile_SetSpeechHue(CMobile *this, uint16_t hue)
{
	this->speechHue = hue;
}

/*
 * 0x0046E610 - CMobile::SayCString
 *
 * Builds and broadcasts a SPEECH packet within 18 tiles at the
 * speaker's head height. -1 defaults: font=0, hue=speechHue, type=3.
 * A player speaker also wakes nearby NPCs for speech awareness.
 */
void
CMobile_SayCString(CItem *ent, char *text, int hue, int type, int font)
{
	uint8_t obuf[0x42C];
	CLocation loc;
	CLocation delta;

	if (font == -1)
		font = 0;
	if (hue == -1)
		hue = ((CMobile *)ent)->speechHue;
	if (type == -1)
		type = 3;

	PacketManager_MakePacket_TEXT(obuf, ent, ent, (uint8_t)font, text, (uint16_t)hue, (uint16_t)type);

	CLocation_Constructor3D(&delta, 0, 0, VT_GetHeight(ent) / 2);
	CLocation_AddWrapped(CEntity_GetLocation(&ent->resourceEntity.entity), &loc, &delta);
	BroadcastToNearbyWithLOS(obuf, &loc, 18);

	if (VT_IsContainer(ent) && VT_IsPlayer(ent)) {
		CWorld_SpeechNotifyNearby(ent, ent->serial, &ent->resourceEntity.entity.location, text, 18);
	}
}

/*
 * 0x0046E72E - CMobile::EmoteCString
 *
 * Same broadcast path as SayCString but with emote defaults
 * (font=7) and always passing speechHue to the packet regardless
 * of the hue argument (matches the binary).
 */
void
CMobile_EmoteCString(CItem *ent, char *text, int hue, int type, int font)
{
	uint8_t obuf[0x42C];
	CLocation loc;
	CLocation delta;

	if (font == -1)
		font = 7;
	if (hue == -1)
		hue = ((CMobile *)ent)->speechHue;
	if (type == -1)
		type = 3;

	PacketManager_MakePacket_TEXT(obuf, ent, ent, (uint8_t)font, text, ((CMobile *)ent)->speechHue, (uint16_t)type);

	CLocation_Constructor3D(&delta, 0, 0, VT_GetHeight(ent) / 2);
	CLocation_AddWrapped(CEntity_GetLocation(&ent->resourceEntity.entity), &loc, &delta);
	BroadcastToNearbyWithLOS(obuf, &loc, 18);

	if (VT_IsContainer(ent) && VT_IsPlayer(ent)) {
		CWorld_SpeechNotifyNearby(ent, ent->serial, &ent->resourceEntity.entity.location, text, 18);
	}
}

/*
 * 0x0046E855 - CMobile vtable[0x6C] SayToEntity
 *
 * Delegates to SayHuedCString using mob->speechHue instead of the default
 * 0x3B2.
 */
void
CMobile_SayToEntity_VT(CMobile *self, CItem *target, uint32_t serial, char *text)
{
	// Binary calls fcn.0048d68a directly, which is the same function
	// as vtable[0x64] SayHuedCString. We use vtable dispatch.
	((void (*)(void *, CItem *, uint32_t, char *, uint16_t))VT_FN((CItem *)self, VT_SAY_HUED_CSTRING))(self, target, serial, text, self->speechHue);
}

/*
 * 0x0046E881 - CMobile::LoseNotoriety
 *
 * Moves the mobile's notoriety toward zero by the given amount, picking
 * ChangeNotorietyNeg when the current value is >= 0 and ChangeNotoriety
 * when it is < 0.
 *
 * FIXED: was previously decompiled as CMobile::LoseFame (operating on
 * fame); the binary actually calls the notoriety helpers
 * (CMobile_GetNotoriety, CMobile_ChangeNotorietyNeg,
 * CMobile_ChangeNotoriety).
 */
void
CMobile_LoseNotoriety(CMobile *mob, int amount)
{
	int noto = CMobile_GetNotoriety(mob);

	if (noto >= 0)
		CMobile_ChangeNotorietyNeg(mob, amount);
	else
		CMobile_ChangeNotoriety(mob, amount);
}

/*
 * 0x0046E8BB - CMobile::GainNotoriety
 *
 * Moves the mobile's notoriety away from zero by the given amount, picking
 * ChangeNotoriety when the current value is >= 0 and ChangeNotorietyNeg
 * when it is < 0.
 *
 * FIXED: was previously decompiled as CMobile::GainFame; the binary
 * actually calls the notoriety helpers (CMobile_GetNotoriety,
 * CMobile_ChangeNotoriety, CMobile_ChangeNotorietyNeg).
 */
void
CMobile_GainNotoriety(CMobile *mob, int amount)
{
	int noto = CMobile_GetNotoriety(mob);

	if (noto >= 0)
		CMobile_ChangeNotoriety(mob, amount);
	else
		CMobile_ChangeNotorietyNeg(mob, amount);
}

/*
 * 0x0046E8F5 - CMobile::ChangeNotoriety
 *
 * Adjusts notoriety by amount. If amount > 0 and notorietyChangeTimer
 * is non-zero, returns immediately (cooldown). Otherwise sets timer to
 * 3600 ticks. Clamps amount to [-50, 50], adds to current notoriety,
 * then calls vtable[0x200] (SetNotoriety) with the new value.
 */
void
CMobile_ChangeNotoriety(CMobile *this, int amount)
{
	int newNoto;

	if (amount > 0) {
		if (this->notorietyChangeTimer != 0)
			return;
		this->notorietyChangeTimer = 3600;
	}
	if (amount > 50)
		amount = 50;
	if (amount < -50)
		amount = -50;
	newNoto = CMobile_GetNotoriety(this) + amount;
	((void (*)(void *, int))VT_FN((CItem *)this, VT_SET_NOTORIETY))(this, newNoto);
}

/*
 * 0x0046E95F
 */
int
CMobile_GetAdjFame(CMobile *this)
{
	return CMobile_GetFame(this);
}

/*
 * 0x0046E97A
 */
int
CMobile_GetFameLevel(CMobile *this)
{
	int levelValue;
	int adjValue;
	int level;

	adjValue = CMobile_GetAdjFame(this);
	levelValue = 10000;
	for (level = 4; level > 0; level--) {
		if (adjValue >= levelValue)
			return level;
		levelValue /= 2;
	}
	return 0;
}

/*
 * 0x0046E9CD - Mobile_SendKarmaFameChangeMessage
 *
 * Tells the player "You have gained/lost [a little/some/a good
 * amount of/a lot of] <name>." based on the delta magnitude.
 */
void
Mobile_SendKarmaFameChangeMessage(CMobile *this, int oldValue, int newValue, char *name)
{
	char buf[256];
	char *direction;
	char *magnitude;
	int delta;

	if (!VT_IsPlayer((CItem *)this))
		return;
	if (oldValue == newValue)
		return;

	delta = newValue - oldValue;
	if (delta >= 0) {
		direction = "gained";
	} else {
		direction = "lost";
		delta = -delta;
	}

	if (delta > 40)
		magnitude = "a lot of";
	else if (delta > 20)
		magnitude = "a good amount of";
	else if (delta > 10)
		magnitude = "some";
	else
		magnitude = "a little";

	sprintf(buf, "You have %s %s %s.", direction, magnitude, name);
	CPlayer_SystemMessage((CPlayer *)this, buf);
}

/*
 * 0x0046EA87
 */
void *
CMobile_ChangeFame(CMobile *this, int fameChange)
{
	int currentFame;
	void *result;
	int newFame;

	if (fameChange >= 0) {
		currentFame = this->fame;
		result = (void *)((uintptr_t)fameChange - currentFame);
		fameChange -= currentFame;
		if (fameChange <= 0)
			return result;
		newFame = fameChange / 100 + this->fame;
		if (GetRandom(100) < fameChange % 100)
			newFame++;
	} else {
		if (fameChange <= -100)
			newFame = 0; // dead store in binary, overwritten below
		newFame = fameChange * this->fame / 100 + this->fame;
	}
	Mobile_SendKarmaFameChangeMessage(this, this->fame, newFame, "fame");
	CMobile_SetFame(this, newFame);
	return Entity_ExecuteEvent(&this->container.item.resourceEntity.entity, FameChanged);
}

/*
 * 0x0046EB75
 */
CMobile *
CMobile_SetFame(CMobile *this, int value)
{
	CMobile *result;

	if (value > 20000)
		value = 20000;
	if (value < 0)
		value = 0;
	result = this;
	this->fame = value;
	return result;
}

/*
 * 0x0046EBAD
 */
int
CMobile_GetAdjKarma(CMobile *this)
{
	int isMurder;
	int karma;

	karma = CMobile_GetKarma(this);
	isMurder = CMobile_IsMurderer(this);
	if (isMurder && karma > 0)
		karma = 0;
	return karma;
}

/*
 * 0x0046EBE1
 */
int
CMobile_GetKarmaLevel(CMobile *this)
{
	int result;
	int levelValue;
	int isPositive;
	int AdjValue;
	int level;

	AdjValue = CMobile_GetAdjKarma(this);
	levelValue = 10000;
	isPositive = 1;
	if (AdjValue < 0) {
		isPositive = 0;
		AdjValue = -this->karma;
	}
	for (level = 5;; level--) {
		if (level <= 0)
			return 0;
		if (AdjValue >= levelValue)
			break;
		levelValue /= 2;
	}
	if (isPositive)
		result = level;
	else
		result = -level;
	return result;
}

/*
 * 0x0046EC6D
 */
void *
CMobile_ChangeKarma(CMobile *this, int karmaChange)
{
	void *result;
	int newKarma;

	result = this;
	if (this->karma < 0) {
		if (karmaChange <= 0 && karmaChange >= this->karma)
			return result;
	} else if (karmaChange >= 0) {
		result = (void *)(uintptr_t)this->karma;
		if (karmaChange <= (int)(intptr_t)result)
			return result;
	}
	karmaChange -= this->karma;
	newKarma = karmaChange / 100 + this->karma;
	if (GetRandom(100) < karmaChange % 100)
		newKarma++;
	Mobile_SendKarmaFameChangeMessage(this, this->karma, newKarma, "karma");
	CMobile_SetKarma(this, newKarma);
	return Entity_ExecuteEvent(&this->container.item.resourceEntity.entity, KarmaChanged);
}

/*
 * 0x0046ED50
 */
CMobile *
CMobile_SetKarma(CMobile *this, int value)
{
	CMobile *result;

	if (value > 20000)
		value = 20000;
	if (value < -20000)
		value = -20000;
	result = this;
	this->karma = value;
	return result;
}

/*
 * 0x0046ED8B - CMobile::ChangeNotorietyNeg
 *
 * Calls ChangeNotoriety with the amount negated.
 */
void
CMobile_ChangeNotorietyNeg(CMobile *this, int amount)
{
	CMobile_ChangeNotoriety(this, -amount);
}

/*
 * 0x0046EDA6 - CMobile::SetNotoriety (vtable)
 *
 * Clamps value to [-127, 127], stores it, and broadcasts the change
 * to players within 0x12 tiles.
 */
void
CMobile_SetNotoriety_VT(CMobile *self, int value)
{
	CLocation loc;
	CVector vec;
	CLocation *entLoc;

	if (value > 127)
		value = 127;
	else if (value < -127)
		value = -127;

	if ((int8_t)self->notoriety == value)
		return;

	self->notoriety = (uint8_t)value;

	if (self->container.item.resourceEntity.entity.removedFromWorld)
		return;

	entLoc = ((CLocation * (*)(CItem *)) VT_FN((CItem *)self, VT_GET_LOCATION))((CItem *)self);
	loc = *entLoc;

	CVector_Constructor(&vec, "\x01");
	GetNearbyPlayers(&vec, &loc, 0x12);

	((void (*)(CItem *, CVector *, int))VT_FN((CItem *)self, VT_NOTIFY_NEARBY))((CItem *)self, &vec, 0);

	CVector_Destructor(&vec);
}

/*
 * 0x0046EE76 - CMobile::GetNotoriety
 *
 * Returns the signed notoriety byte.
 */
int
CMobile_GetNotoriety(CMobile *this)
{
	return (int)(int8_t)this->notoriety;
}

/*
 * 0x0046EE8B - NotoValueToLevel
 *
 * Converts a raw notoriety/karma value to a "level" by dividing
 * abs(value) by 24, preserving sign.
 */
int
NotoValueToLevel(int value)
{
	int level;

	level = abs(value) / 24;
	if (value < 0)
		return -level;
	return level;
}

/*
 * 0x0046EEC5 - CMobile::GetNotoLevel
 *
 * Returns the notoriety "level" for this mobile. Reads the notoriety
 * byte (offset 0x284) as signed, then converts via NotoValueToLevel.
 * For standard UO notoriety values (1-6), always returns 0.
 */
int
CMobile_GetNotoLevel(CMobile *this)
{
	int8_t noto;

	noto = (int8_t)this->notoriety;
	return NotoValueToLevel(noto);
}

/*
 * 0x0046EEE1 - CMobile::GetHp
 *
 * Returns this->hp.
 */
uint32_t
CMobile_GetHp(CMobile *this)
{
	return this->hp;
}

/*
 * 0x0046EEF5 - CMobile::GetMaxHp
 *
 * Returns this->maxHp.
 */
uint32_t
CMobile_GetMaxHp(CMobile *this)
{
	return this->maxHp;
}

/*
 * 0x0046EF09 - CMobile::GetStamina
 *
 * Returns the mobile's current stamina.
 */
uint32_t
CMobile_GetStamina(CMobile *this)
{
	return this->stamina;
}

/*
 * 0x0046EF1D - CMobile::GetMaxStamina
 *
 * Returns the mobile's max stamina.
 */
uint32_t
CMobile_GetMaxStamina(CMobile *this)
{
	return this->maxStamina;
}

/*
 * 0x0046EF31 - CMobile::GetMana
 *
 * Returns the mobile's current mana.
 */
uint32_t
CMobile_GetMana(CMobile *this)
{
	return this->mana;
}

/*
 * 0x0046EF45 - CMobile::GetMaxMana
 *
 * Returns the mobile's max mana.
 */
uint32_t
CMobile_GetMaxMana(CMobile *this)
{
	return this->maxMana;
}

/*
 * 0x0046EF59 - CMobile::SetHP (vtable[0x1C0])
 *
 * Clamps value to [0, maxHp]. If HP actually changed, calls
 * BroadcastStatUpdate(0) to update nearby players. Returns new HP.
 * Binary takes 2 args (newHP, flags) but flags arg is unused.
 */
uint32_t
CMobile_SetHP(CMobile *this, int newHP)
{
	uint32_t old;

	old = this->hp;
	if (newHP < 0)
		newHP = 0;
	this->hp = (uint32_t)newHP;
	if (this->hp > this->maxHp)
		this->hp = this->maxHp;

	if (old != this->hp)
		CMobile_BroadcastStatUpdate(this, 0);
	return this->hp;
}

/*
 * 0x0046EFD4 - CMobile::SetMaxHP (vtable[0x1C4])
 *
 * Sets maxHp, clamps to >= 0, and broadcasts a stat update when changed.
 * Returns the new maxHp.
 */
uint32_t
CMobile_SetMaxHP(CMobile *this, int newMaxHP)
{
	uint32_t old;

	old = this->maxHp;
	this->maxHp = (uint32_t)newMaxHP;
	if ((int32_t)this->maxHp < 0)
		this->maxHp = 0;

	if (old != this->maxHp)
		CMobile_BroadcastStatUpdate(this, 0);
	return this->maxHp;
}

/*
 * 0x0046F035 - CMobile::SetStamina (vtable[0x1C8])
 *
 * Clamps newStamina to [0, maxStamina] and broadcasts a stat update when
 * the value actually changed. Returns the new stamina.
 */
uint32_t
CMobile_SetStamina(CMobile *this, int newStamina)
{
	uint32_t old;

	old = this->stamina;
	this->stamina = (uint32_t)newStamina;
	if ((int32_t)this->stamina > (int32_t)this->maxStamina)
		this->stamina = this->maxStamina;
	if (newStamina < 0)
		this->stamina = 0;

	if (old != this->stamina)
		CMobile_BroadcastStatUpdate(this, 2);
	return this->stamina;
}

/*
 * 0x0046F0B6 - CMobile::SetMaxStamina (vtable[0x1CC])
 *
 * Sets maxStamina, clamps to >= 0, and broadcasts a stat update when
 * changed. Returns the new maxStamina.
 */
uint32_t
CMobile_SetMaxStamina(CMobile *this, int newMaxStamina)
{
	uint32_t old;

	old = this->maxStamina;
	this->maxStamina = (uint32_t)newMaxStamina;
	if ((int32_t)this->maxStamina < 0)
		this->maxStamina = 0;

	if (old != this->maxStamina)
		CMobile_BroadcastStatUpdate(this, 2);
	return this->maxStamina;
}

/*
 * 0x0046F117 - CMobile::SetMana (vtable[0x1D0])
 *
 * Clamps newMana to [0, maxMana] and broadcasts a stat update when the
 * value actually changed. Returns the new mana.
 */
uint32_t
CMobile_SetMana(CMobile *this, int newMana)
{
	uint32_t old;

	old = this->mana;
	this->mana = (uint32_t)newMana;
	if (this->mana > this->maxMana)
		this->mana = this->maxMana;
	if (newMana < 0)
		this->mana = 0;

	if (old != this->mana)
		CMobile_BroadcastStatUpdate(this, 1);
	return this->mana;
}

/*
 * 0x0046F198 - CMobile::SetMaxMana (vtable[0x1D4])
 *
 * Sets maxMana, clamps to >= 0, and broadcasts a stat update when changed.
 * Returns the new maxMana.
 */
uint32_t
CMobile_SetMaxMana(CMobile *this, int newMaxMana)
{
	uint32_t old;

	old = this->maxMana;
	this->maxMana = (uint32_t)newMaxMana;
	if ((int32_t)this->maxMana < 0)
		this->maxMana = 0;

	if (old != this->maxMana)
		CMobile_BroadcastStatUpdate(this, 1);
	return this->maxMana;
}

/*
 * 0x0046F206 - Drain HP when climbing steep terrain
 *
 * Called during movement when the Z height difference is significant.
 * Binary calls CMobile_GetHp then vtable[0x1C0] SetHP(newHP, 0).
 * If zDelta >= 22, drain = ((zDelta - 22) / 11 + 1) * 5 HP.
 * Skips for movementType==2 (flying/ghost), dead players.
 */
void
CMobile_DrainStaminaForClimb(CMobile *this, int zDelta)
{
	int tier, drain, newHP;

	if (CMobile_GetMovementType(this) == 2)
		return;

	if (VT_IsPlayer((CItem *)this)) {
		if (VT_IsDead((CItem *)this))
			return;
	}

	if (zDelta < 22)
		return;

	tier = (zDelta - 22) / 11;
	drain = (tier + 1) * 5;

	if (drain == 0)
		return;

	newHP = CMobile_GetHp(this) - drain;
	((uint32_t (*)(void *, int, int))VT_FN((CItem *)this, VT_SET_HP))(this, newHP, 0);
}

/*
 * 0x0046F291 - CNextMobileCtx constructor
 *
 * Thiscall. Calls CVector_Constructor on this+4 with a local type flag,
 * then sets this[0] = 0.
 */
void
CNextMobileCtx_Constructor(CNextMobileCtx *this)
{
	char typeFlag = 0;
	CVector_Constructor(&this->vector, &typeFlag);
	this->field_0 = 0;
}

/*
 * 0x0046F2B9 - CNextMobileCtx destructor
 *
 * Thiscall. Calls CVector_Destructor on this+4.
 */
void
CNextMobileCtx_Destructor(CNextMobileCtx *this)
{
	CVector_Destructor(&this->vector);
}

/*
 * 0x0046F2CF - CMobile::IsRideable
 *
 * Walks the mobile's resource node list and returns 1 when any node's
 * CResourceType has foodName == "RIDABLE" (case-insensitive).
 */
int
CMobile_IsRideable(CMobile *this)
{
	CResourceNode *node;

	for (node = this->container.item.resourceEntity.firstChild; node != NULL; node = node->next) {
		if (node->id == 0)
			continue;
		CResourceType *rt = CResourceTypeManager_GetId(node->id);
		if (strcasecmp("RIDABLE", CResourceType_GetFoodName(rt)) == 0)
			return 1;
	}
	return 0;
}

/*
 * 0x0046F32F - CMobile vtable[0x28] GetHeight
 *
 * Binary has a switch on body type that always returns 16 for
 * all cases. Cases 0 and 1 explicitly return 16; default also
 * returns 16.
 */
int
CMobile_GetHeight_VT(CMobile *self)
{
	int bodyType;

	bodyType = CResourceEntity_GetBodyType((CItem *)self) & 0xFFFF;
	switch (bodyType) {
	case 0:
	case 1:
		return 16;
	default:
		return 16;
	}
}

/*
 * 0x0046F366 - CMobile::SetLight
 *
 * Stores the global/personal light levels, pushing a LIGHTCHANGE
 * packet to a player whose personal light changed (pflags & 4).
 */
void
CMobile_SetLight(CMobile *mob, uint8_t lightTime, uint8_t lightVal)
{
	uint8_t buf[8];

	if (VT_IsPlayer((CItem *)mob)) {
		CPlayer *pl = (CPlayer *)mob;
		if ((pl->pflags & 4) && lightVal != mob->lightVal) {
			PacketManager_MakePacket_LIGHTCHANGE(buf, mob->container.item.serial, lightVal);
			SendToClient((CItem *)mob, buf, -1);
		}
	}
	mob->lightVal = lightVal;
	mob->lightTime = lightTime;
}

/*
 * 0x0046F6BD - FixBank
 *
 * Guarantees the bank box at equipment[29] is a CContainer with
 * bodyType 0xE7C, creating a replacement and moving any existing
 * item inside when needed.
 */
void
FixBank(CMobile *mob)
{
	CItem *bankBox;
	CItem *newBank;
	CItem *oldBank;
	CLocation tmpLoc;

	bankBox = mob->equipment[29];
	if (bankBox != NULL && VT_IsMobile2(bankBox)) {
		if ((CEntity_GetBodyType(bankBox) & 0xFFFF) != 0xE7C)
			CEntity_SetBodyType(bankBox, 0xE7C);
		return;
	}

	newBank = (CItem *)malloc(sizeof(CContainer));
	if (newBank != NULL)
		CContainer_Constructor((CContainer *)newBank);

	CEntity_SetBodyType(newBank, 0xE7C);
	CItem_SetServerOnly(newBank, 1);

	CLocation_Init(&tmpLoc);
	CLocation_Set(&tmpLoc, -1, -1, 0);

	oldBank = mob->equipment[29];
	if (oldBank != NULL) {
		((void (*)(void *))VT_FN(oldBank, VT_HIDE))(oldBank);
		CEntity_SetBodyType(oldBank, 0xEED);
		((void (*)(void *, CItem *, CLocation *))VT_FN(oldBank, VT_ADD_TO_CONTAINER))(oldBank, newBank, &tmpLoc);
	}

	((int (*)(void *, void *, int))VT_FN(newBank, VT_EQUIP_ON_MOBILE))(newBank, mob, 29);
}

/*
 * 0x0046F804 - CMobile::PutMoneyInBank
 *
 * Deposits either the whole stack or a split of size amount into
 * the mob's bank box (creating one if needed). Returns 1 for
 * negative amount and -2 when the resource ratio rejects the move.
 */
int
CMobile_PutMoneyInBank(CMobile *mob, CItem *item, int amount)
{
	CContainer *bankBox;
	CLocation loc;
	uint32_t savedSerial;
	CItem *splitItem;

	if (amount < 0)
		return 1;

	FixBank(mob);
	bankBox = (CContainer *)mob->equipment[29];
	if (bankBox == NULL)
		goto cleanup;

	CLocation_Init(&loc);
	loc.x = -1;
	loc.y = -1;
	loc.z = 0;

	savedSerial = item->serial;

	if (amount == CItem_GetMinResourceRatio(item)) {
		if (item->resourceEntity.entity.removedFromWorld == 0)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, (CItem *)bankBox, &loc);
		return -1;
	}

	splitItem = CItem_SplitByAmount(item, (uint16_t)amount);
	if (splitItem == NULL)
		return -1;

	((void (*)(void *, CItem *, CLocation *))VT_FN(splitItem, VT_ADD_TO_CONTAINER))(splitItem, (CItem *)bankBox, &loc);

	if (CWorld_FindBySerial(g_World, savedSerial) != item)
		return -1;

cleanup:
	if (CItem_GetMinResourceRatio(item) > 0)
		return -2;

	// Corruption check
	if ((uintptr_t)mob == (uintptr_t)-1)
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "badness", "error", "CMobile::PutMoneyInBank() this was 0xffffffff");

	if (item->resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	if (item != NULL)
		((void (*)(void *))VT_FN(item, VT_DELETE))(item);

	return -1;
}

/*
 * 0x0046F95F - CMobile::SubtractGold
 *
 * Subtracts gold from a mobile's bank box. Calls FixBank to ensure
 * bank exists, then CContainer_ConsumeResources with bodyType 0xEED
 * (gold). Returns the consumed gold item or NULL.
 */
CItem *
CMobile_SubtractGold(CMobile *mob, int amount)
{
	CItem *bankBox;
	CItem *result;

	if (mob->equipment[29] == NULL)
		return NULL;

	FixBank(mob);

	bankBox = mob->equipment[29];
	result = CContainer_ConsumeResources((CContainer *)bankBox, 0xEED, amount, NULL);
	return result;
}

/*
 * 0x0046F9AF - CMobile::AmountGoldInBank
 *
 * Thiscall: checks equipment[29] (bankBox), if NULL returns 0.
 * Else calls FixBank, then CContainer_GetTotalQuantity(bankBox, 0xEED).
 */
int
CMobile_AmountGoldInBank(CMobile *mob)
{
	if (mob->equipment[29] == NULL)
		return 0;
	FixBank(mob);
	return CContainer_GetTotalQuantity((CContainer *)mob->equipment[29], 0xEED);
}

/*
 * Helper: insert item into corpse container.
 * Binary uses vtable[0xB4] (InsertIntoContainer) with location (-1,-1,0).
 */
static void
InsertItemIntoCorpse(CCorpse *corpse, CItem *item)
{
	CLocation tmpLoc;
	CLocation_Set(&tmpLoc, -1, -1, 0);
	((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, &corpse->container.item, &tmpLoc);
}

/*
 * Helper: drop item at mob's location on the ground.
 * Binary uses vtable[0x04] (SetLocation) with mob's CLocation.
 */
static void
DropItemAtMobLocation(CItem *item, CLocation *mobLoc)
{
	((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, mobLoc);
}

/*
 * Helper: look up an entity by serial and verify it's a corpse.
 * Binary pattern: CWorld_FindBySerial -> vtable[0xFC] (IsCorpse) check.
 * Returns the entity as CCorpse* if found and IsCorpse, else NULL.
 */
static CCorpse *
FindCorpseBySerial(uint32_t serial)
{
	CItem *ent;

	if (serial == 0)
		return NULL;
	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return NULL;
	if (!((int (*)(void *))VT_FN(ent, VT_HAS_CORPSE_EQ))(ent))
		return NULL;
	return (CCorpse *)ent;
}

/*
 * 0x0046F9E9 - CMobile::CreateCorpse
 *
 * On death, spawns a corpse container, moves equipment and loot
 * resource nodes onto it, runs death events and the corpse script,
 * and schedules decay. Returns the corpse, or NULL (pack animals,
 * script cancellation).
 */
CCorpse *
CMobile_CreateCorpse(CMobile *mob, CMobile *attacker, int dropLoot)
{
	CCorpse *corpse;
	CItem *item;
	CLocation *mobLoc;
	CLocation compLoc;
	int slot;
	uint8_t pktbuf[64];

	uint32_t corpseSerial;
	uint32_t attackerSerial;
	int isMulti;
	int ret;

	mobLoc = &mob->container.item.resourceEntity.entity.location;

	if (CMobile_IsMounted(mob))
		CMobile_Dismount(mob);

	// Only NPCs have sfxDie; for players this is skipped.
	if (VT_IsNPC(&mob->container.item)) {
		if (mob->sfxDie != 0) {
			PlaySoundAtEntity(&mob->container.item, mob->sfxDie, 0);
		}
	}

	if (attacker != NULL && ((int (*)(void *))VT_FN(&attacker->container.item, VT_CHECK_EC))(&attacker->container.item))
		dropLoot = 0;

	CLocation_Init(&compLoc);

	// Binary calls CResourceEntity_GetBodyType three separate times.
	if ((CResourceEntity_GetBodyType(&mob->container.item) & 0xFFFF) == 0x0D || (CResourceEntity_GetBodyType(&mob->container.item) & 0xFFFF) == 0x0F ||
	        (CResourceEntity_GetBodyType(&mob->container.item) & 0xFFFF) == 0x10) {
		corpse = NULL;
		corpseSerial = 0;
		goto death_anim;
	}

	corpse = (CCorpse *)malloc(sizeof(CCorpse));
	if (corpse == NULL)
		return NULL;
	CCorpse_Constructor(corpse);

	CEntity_SetBodyType(&corpse->container.item, CORPSE_BODYTYPE);

	CCorpse_SetCorpseBodyType(corpse, CResourceEntity_GetBodyType(&mob->container.item));

	CItem_SetDirectionVT(&corpse->container.item, (int)(uint8_t)mob->direction);

	corpse->container.item.resourceEntity.entity.color = mob->container.item.resourceEntity.entity.color;

	CMobile_SetCorpseLookAtText(mob, &corpse->container.item);

	corpseSerial = corpse->container.item.serial;

death_anim:
	isMulti = 0;
	if (corpse != NULL) {
		if (((int (*)(void *))VT_FN(&corpse->container.item, VT_GET_DIRECTION))(&corpse->container.item) & 0x80)
			isMulti = 1;
	}

	PacketManager_MakePacket_DEATH_ANIM(pktbuf, mob->container.item.serial, corpseSerial, isMulti);
	SendPacketInRange(pktbuf, mobLoc, 0x12);

	CMobile_RemoveFromAllCombatLists(mob);

	if (!mob->container.item.resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPreModify(&mob->container.item);

	if (corpse != NULL) {
		CResourceNode *node;

		if (!corpse->container.item.resourceEntity.entity.removedFromWorld)
			CResourceEntity_NotifyPreModify(&corpse->container.item);

		// 0x0046FC5B: Drain-from-head loop: binary re-reads firstChild
		// at the top of each iteration after removal.
		while (mob->container.item.resourceEntity.firstChild != NULL) {
			node = mob->container.item.resourceEntity.firstChild;

			CResourceEntity_RemoveNode(&mob->container.item, node);

			if (node->id == 0)
				goto free_node;

			if (node->type != 3)
				goto free_node;

			if (node->id == (uint16_t)g_ResTypeId_CarnivoreMeat) {
				int value3 = node->value3;
				CResourceEntity_AddNodeScaled(&corpse->container.item, (uint16_t)g_ResTypeId_Meat, 3, value3, 0, value3, 4, 1, 1);
				goto free_node;
			}

			if (node->id == (uint16_t)g_ResTypeId_Good)
				goto free_node;

			if (node->id == (uint16_t)g_ResTypeId_Evil)
				goto free_node;

			CResourceEntity_InsertNode(&corpse->container.item, node);
			continue;

free_node:
			ResourceNode_ReturnToPool(node);
		}

		CResourceEntity_NotifyPostModify(&corpse->container.item);
		if (!corpse->container.item.resourceEntity.entity.removedFromWorld)
			CResourceEntity_NotifyPostModifyIfActive(&corpse->container.item);
	}

	CResourceEntity_NotifyPostModify(&mob->container.item);
	if (!mob->container.item.resourceEntity.entity.removedFromWorld)
		CResourceEntity_NotifyPostModifyIfActive(&mob->container.item);

	corpseSerial = 0;
	if (corpse != NULL) {
		corpseSerial = corpse->container.item.serial;

		((void (*)(void *, CLocation *))VT_FN(&corpse->container.item, VT_SET_LOCATION))(
		        &corpse->container.item, CEntity_GetLocation(&mob->container.item.resourceEntity.entity));

		Entity_AttachScript(&corpse->container.item, "corpse", 0);

		// Script may have deleted/replaced the corpse.
		corpse = FindCorpseBySerial(corpseSerial);
	}

	attackerSerial = 0;
	if (attacker != NULL)
		attackerSerial = attacker->container.item.serial;

	ret = (int)(intptr_t)Entity_ExecuteEvent(&mob->container.item.resourceEntity.entity, 0x04, attackerSerial, corpseSerial);

	if (ret == 0) {
		// Binary has two consecutive NULL checks (0x0046FE72, 0x0046FE78).
		corpse = FindCorpseBySerial(corpseSerial);
		if (corpse != NULL)
			if (corpse != NULL)
				((void (*)(void *))VT_FN(&corpse->container.item, VT_DELETE))(&corpse->container.item);
		return NULL;
	}

	if (corpse != NULL) {
		Entity_ExecuteEvent(&corpse->container.item.resourceEntity.entity, 0x04, attackerSerial, mob->container.item.serial);

		// Re-lookup corpse after script execution.
		corpse = FindCorpseBySerial(corpseSerial);
	}

	CMobile_LoseNotoriety(mob, 10);

	BroadcastEventToNearby(mobLoc, 8, SawDeath, attackerSerial, mob->container.item.serial, corpseSerial);

	if (corpse != NULL)
		corpse = FindCorpseBySerial(corpseSerial);

	// 0x00470200: Binary has two consecutive NULL checks.
	if (!dropLoot) {
		if (corpse != NULL)
			if (corpse != NULL)
				((void (*)(void *))VT_FN(&corpse->container.item, VT_DELETE))(&corpse->container.item);
		return NULL;
	}

	for (slot = 0; slot < 26; slot++) {
		item = mob->equipment[slot];
		if (item == NULL)
			continue;

		if (CEntity_GetBodyType(item) == 0x1B7) {
			if (VT_IsMobile2(item)) {
				CItem *child, *cnext;

				child = ((CContainer *)item)->contents;
				while (child != NULL) {
					cnext = child->spatialNext;

					((void (*)(void *))VT_FN(child, VT_HIDE))(child);

					if (corpse != NULL) {
						InsertItemIntoCorpse(corpse, child);
					} else {
						DropItemAtMobLocation(child, mobLoc);
					}
					child = cnext;
				}
			}
		}

		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

		if (g_ItemTileData[CEntity_GetBodyType(item)].value1 == 0) {
			((void (*)(void *))VT_FN(item, VT_DELETE))(item);
			continue;
		}

		if (!item->resourceEntity.entity.removedFromWorld)
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);

		if (corpse != NULL) {
			// Binary writes compLoc.x/.y = -1 (redundant, already -1
			// from CLocation_Init), then uses compLoc with z=-1.
			compLoc.x = -1;
			compLoc.y = -1;
			((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, &corpse->container.item, &compLoc);

			corpse->equipSlots[slot] = item->serial;
		} else {
			DropItemAtMobLocation(item, mobLoc);
		}
	}

	{
		CItem *child, *cnext;

		child = mob->container.contents;
		while (child != NULL) {
			cnext = child->spatialNext;

			((void (*)(void *))VT_FN(child, VT_HIDE))(child);

			if (corpse != NULL) {
				InsertItemIntoCorpse(corpse, child);
			} else {
				DropItemAtMobLocation(child, mobLoc);
			}
			child = cnext;
		}
	}

	if (corpse != NULL) {
		((void (*)(void *))VT_FN(&corpse->container.item, VT_DETACH_SPATIAL))(&corpse->container.item);

		((void (*)(void *, CLocation *))VT_FN(&corpse->container.item, VT_DROP_AT_FEET))(
		        &corpse->container.item, ((CLocation * (*)(void *)) VT_FN(&mob->container.item, VT_GET_LOCATION))(&mob->container.item));

		// Schedules corpse decay at 0x690 = 1680 ticks.
		ScheduleEvent(0x690, corpse->container.item.serial, 1, 0, 0);
	}

	return corpse;
}

/*
 * 0x0047022A - CMobile vtable[0x1BC] PreDeleteClean
 *
 * Returns 0; the base CMobile needs no pre-delete cleanup. CPlayer overrides.
 */
int
CMobile_PreDeleteClean_VT(CItem *self)
{
	USED(self);
	return 0;
}

/*
 * 0x00470253 - CMobile vtable[0x24] GetValue
 *
 * Accumulates mobile value: base container value, stats, maxHP/stam/mana,
 * tame percentage modifier, all 50 skills/10, gold, armor dice average,
 * then equipment values via VT_GET_VALUE dispatch.
 */
int
CMobile_GetValue_VT(CItem *self, int useResource, int normalize)
{
	CMobile *mob = (CMobile *)self;
	int value;
	int statSum;
	int tamePct;
	int i;

	value = 0;

	if (!CItem_IsValueless(self)) {
		// Base container value (CContainer::GetValue)
		value = CContainer_GetValue_VT(self, useResource, 0);

		// Sum base stats: str + dex + int
		statSum = (unsigned)mob->baseStr + (unsigned)mob->baseDex + (unsigned)mob->baseInt;

		// Add max HP + max stamina + max mana
		statSum += CMobile_GetMaxHp(mob) + CMobile_GetMaxStamina(mob) + CMobile_GetMaxMana(mob);

		// Get petCanTame tag (default 50 if not set)
		tamePct = 0;
		if (!CItem_GetTagInt(self, "petCanTame", &tamePct))
			tamePct = 50;

		// value += (statSum / 3) * (tamePct / 10)
		value += (statSum / 3) * (tamePct / 10);

		// Add all 50 skills / 10 each (unsigned division)
		for (i = 0; i < 50; i++)
			value += (unsigned)CMobile_GetBaseSkill(mob, (int8_t)i) / 10;

		// Add bonusAC
		value += mob->bonusAC;

		// Add armor dice average
		value += CDiceRoll_Average((CWeaponDice *)&mob->armorRating);
	}

	// Equipment loop: always runs (even if valueless)
	for (i = 0; i < 26; i++) {
		if (mob->equipment[i] != NULL)
			value += ((int (*)(void *, int, int))VT_FN(mob->equipment[i], VT_GET_VALUE))(mob->equipment[i], useResource, 0);
	}

	if (!CItem_IsValueless(self) && normalize != 0)
		value = CItem_NormalizeValue(self, value);

	return value;
}

/*
 * 0x004703F8 - CMobile::FindEquippedItem
 *
 * Searches all 30 equipment slots by serial. If slot item doesn't match,
 * checks if it's a container and recurses via CContainer_FindItemBySerial.
 * Mutually recursive with CContainer_FindItemBySerial (0x00449006).
 */
CItem *
CMobile_FindEquippedItem(CMobile *this, uint32_t serial)
{
	int i;
	CItem *item, *found;

	for (i = 0; i < 30; i++) {
		item = this->equipment[i];
		if (item == NULL)
			continue;

		// Direct serial match
		if (item->serial == serial)
			return item;

		// If slot item is a container, search inside it
		if (CItem_IsContainer(item)) {
			found = CContainer_FindItemBySerial((CContainer *)item, serial);
			if (found != NULL)
				return found;
		}
	}
	return NULL;
}

/*
 * 0x004704B8 - NotifyNearby
 *
 * Gathers nearby players within range 0x12 of self and dispatches
 * vtable[0x130] (NotifyNearby) with the resulting vector and flag 1.
 */
void
NotifyNearby(CItem *self)
{
	CVector players;
	CVector_Constructor(&players, "\x01");
	GetNearbyPlayers(&players, &self->resourceEntity.entity.location, 0x12);
	((void (*)(void *, CVector *, int))VT_FN(self, VT_NOTIFY_NEARBY))(self, &players, 1);
	CVector_Destructor(&players);
}

/*
 * 0x00470531 - CMobile::GetTotalQuantityOfType
 *
 * Iterates all 26 equipment slots, checks IsContainer (vtable[0xD4]),
 * and sums CContainer_GetTotalQuantity for matching body type.
 */
int
CMobile_GetTotalQuantityOfType(CMobile *this, uint16_t bodyType)
{
	int total, i;

	total = 0;
	for (i = 0; i < 26; i++) {
		if (this->equipment[i] == NULL)
			continue;
		if (!CItem_IsContainer(this->equipment[i]))
			continue;
		total += CContainer_GetTotalQuantity((CContainer *)this->equipment[i], bodyType);
	}
	return total;
}

/*
 * 0x004705B9 - CMobile::FindItemInEquipment (ConsumeGeneric)
 *
 * Iterates equipment slots 0-25, checks each is a container
 * (vtable[0xD4]), calls CContainer_ConsumeResources to find/consume
 * items matching bodyType. Returns the result item when found with
 * sufficient resources, or NULL.
 */
CItem *
CMobile_FindItemInEquipment(CMobile *mob, uint16_t bodyType, int targetAmount)
{
	CItem *result;
	int i;

	result = NULL;
	for (i = 0; i < 0x1A; i++) {
		if (mob->equipment[i] == NULL)
			continue;
		if (!VT_IsMobile2(mob->equipment[i]))
			continue;
		result = CContainer_ConsumeResources((CContainer *)mob->equipment[i], bodyType, targetAmount, result);
		if (result == NULL)
			continue;
		if (CItem_GetMinResourceRatio(result) == targetAmount)
			return result;
	}
	return result;
}

/*
 * 0x0047065F - Set corpse lookAtText from mob template/name
 *
 * Picks the corpse's "look at" label in priority order: a template
 * corpseName if set; "a corpse of {name}" for human-bodied players;
 * or "human" for human-bodied NPCs.
 */
void
CMobile_SetCorpseLookAtText(CMobile *mob, CItem *corpse)
{
	uint16_t tmplIdx;
	NPCTemplate *tmpl;

	tmplIdx = CResourceEntity_GetTemplateIndex(&mob->container.item);

	// Path 1: template corpseName
	if ((tmplIdx & 0xFFFF) != 0xFFFF) {
		if (CResManager_HasByInt(&g_TemplatesRM, (uint32_t)(tmplIdx & 0xFFFF))) {
			tmpl = CResManager_GetTemplateByID(tmplIdx & 0xFFFF);
			if (!CString_IsEmpty(&tmpl->corpseName)) {
				tmpl = CResManager_GetTemplateByID(tmplIdx & 0xFFFF);
				CCorpse_SetLookAtText(corpse, &tmpl->corpseName);
				return;
			}
		}
	}

	// Path 2: player with human body type (0x190-0x193)
	if (VT_IsPlayer(&mob->container.item)) {
		if ((CResourceEntity_GetBodyType(&mob->container.item) & 0xFFFF) >= 0x190 && (CResourceEntity_GetBodyType(&mob->container.item) & 0xFFFF) <= 0x193) {
			CString nameStr;
			const char *name;
			name = ((const char *(*)(void *))VT_FN(&mob->container.item, VT_GET_NAME))(&mob->container.item);
			CString_Constructor(&nameStr, name);
			CCorpse_SetPlayerLookAtText(corpse, &nameStr);
			CString_Destructor(&nameStr);
			return;
		}
	}

	// Path 3: non-player NPC with human body type (0x190-0x193)
	if ((CResourceEntity_GetBodyType(&mob->container.item) & 0xFFFF) >= 0x190 && (CResourceEntity_GetBodyType(&mob->container.item) & 0xFFFF) <= 0x193) {
		CString humanStr;
		CString_Constructor(&humanStr, "human");
		CCorpse_SetLookAtText(corpse, &humanStr);
		CString_Destructor(&humanStr);
	}
}

/*
 * 0x004707D1 - CMobile::RemoveFromAllCombatLists
 *
 * Scrubs this mob's serial from every other mobile's combat/attacker
 * lists and drops war mode on NPCs that end up with no targets.
 */
void
CMobile_RemoveFromAllCombatLists(CMobile *this)
{
	CMobile *iter;

	for (iter = g_MobileListHead; iter != NULL; iter = iter->nextMobile) {
		if (VT_IsNPC((CItem *)iter)) {
			if (((CNPC *)iter)->actionTarget == this->container.item.serial)
				CNPC_ClearCombatTarget(iter, 0);
		}

		CSerialList_Remove(&iter->combatTargetList, this->container.item.serial);

		if (VT_IsNPC((CItem *)iter) && iter->combatTargetList.count == 0)
			CMobile_ClearMobileFlag(iter, MobileFlag_WarMode);

		CSerialList_Remove(&iter->attackerList, this->container.item.serial);
	}
}

/*
 * 0x0047088B - CMobile::CMobile
 *
 * Chains to CContainer_Constructor, zeroes mobile-specific fields, sets
 * default hues/sfx, and links this mob at the head of g_MobileListHead.
 */
void
CMobile_Constructor(CMobile *mob)
{
	int i;

	CContainer_Constructor(&mob->container);

	// Zero mobile-specific fields (0x5C onward) that are not set by
	// CContainer_Constructor. Binary sets each individually; we memset.
	memset((char *)mob + sizeof(CContainer), 0, sizeof(CMobile) - sizeof(CContainer));

	mob->speechHue = 0x03B2;

	mob->sfxNotice = 0xFFFF;
	mob->sfxIdle = 0xFFFF;
	mob->sfxHit = 0xFFFF;
	mob->sfxWasHit = 0xFFFF;
	mob->sfxDie = 0xFFFF;

	CSerialList_Init(&mob->combatTargetList);
	CSerialList_Init(&mob->attackerList);

	CDiceRoll_Constructor((CWeaponDice *)&mob->armorRating);

	// Our code uses CEntity_SetType; callers override with final type.

	g_MobileCount++;

	mob->nextMobile = g_MobileListHead;
	if (g_MobileListHead != NULL)
		g_MobileListHead->prevMobile = mob;
	g_MobileListHead = mob;
	mob->prevMobile = NULL;

	CItem_SetHidden_VT(&mob->container.item, 0);

	for (i = 0; i < 50; i++) {
		mob->skills[i] = 0;
		mob->skillBonuses[i] = 0;
		mob->skillTimers[i] = CRandom_GetDefaultSeed();
		mob->skillCounts[i] = 0;
	}

	for (i = 0; i < 30; i++)
		mob->equipment[i] = NULL;

	CMobile_SetDirection(&mob->container.item, 0);

	StringAssign(&mob->name, "");

	CMobile_SetBonusAC(mob, 0);

	CDiceRoll_Init((CWeaponDice *)&mob->armorRating, 1, 8, 0, 0);

	mob->notorietyChangeTimer = 0;

	mob->container.item.serial &= ~0x40000000u;

	CMobile_SetMovementType(mob, 1);

	CMobile_ClearResistFlags(mob);
	CMobile_ClearVulnFlags(mob);
}

/*
 * 0x00470D44 - CMobile::PreDeleteCleanup
 *
 * Drops every equipped item: places it in the world while active,
 * otherwise deletes it, then chains to CContainer::PreDeleteCleanup.
 */
void
CMobile_PreDeleteCleanup(CMobile *mob)
{
	int i;

	for (i = 0; i < 30; i++) {
		if (mob->equipment[i] == NULL)
			continue;
		if (g_WorldActive2 != 0) {
			// 0x00470D80: place in world with decay reset
			CItem_PlaceInWorld(mob->equipment[i], 1);
		} else {
			// 0x00470D94: delete via vtable[0x90]
			if (mob->equipment[i] != NULL)
				((void (*)(void *))VT_FN(mob->equipment[i], VT_DELETE))(mob->equipment[i]);
		}
	}

	CContainer_PreDeleteCleanup(&mob->container.item);
}

/*
 * 0x00470DD4 - CMobile::~CMobile
 *
 * Tears down a mobile: hide, stop combat, rebalance follower/master
 * links, unlink from global lists, free name, destroy serial lists,
 * chain to CContainer_Destructor.
 *
 * FIXED: When transferring followers to a new master (2+ followers path),
 * the binary sets newMaster->isFollower=0 but never clears newMaster->owner,
 * leaving a dangling pointer to the dying mob. Any subsequent access to
 * newMaster->owner dereferences freed memory. Fix: clear newMaster->owner.
 */
void
CMobile_Destructor(CMobile *mob)
{
	CMobile *newMaster;
	CMobile *cur, *next;

	// Set vtable to CMobile (0x005EEF48)

	// Hide if not removed from world
	if (mob->container.item.resourceEntity.entity.removedFromWorld == 0)
		CItem_HideVT(&mob->container.item);

	// Stop combat
	CMobile_StopCombat(mob);

	// Clear scripts and tags
	CItem_ClearScriptsAndTags(&mob->container.item);

	// If this mob is a follower, remove from master
	if (mob->isFollower != 0)
		CMobile_RemoveFollower(mob->owner, mob);

	// If this mob has followers, handle them
	if (mob->hasFollowers != 0) {
		if (CMobile_CountFollowers(mob) < 2) {
			CMobile_ReleaseAllFollowers(mob);
		} else {
			// 2+ followers: transfer to first follower as new master
			newMaster = mob->firstFollower;
			newMaster->isFollower = 0;
			newMaster->owner = NULL;
			cur = newMaster->nextFollower;
			while (cur != NULL) {
				next = cur->nextFollower;
				CMobile_RemoveFollower(mob, cur);
				CMobile_AddFollower(newMaster, cur);
				cur = next;
			}
		}
	}

	// Remove from combat event list
	CCombatEventList_RemoveMob(&g_CombatEventList, mob);

	// Advance g_nextCombatMobile if it points to us
	if (g_nextCombatMobile == mob)
		g_nextCombatMobile = mob->nextMobile;

	// Unlink from mobile doubly-linked list
	if (mob->nextMobile != NULL)
		mob->nextMobile->prevMobile = mob->prevMobile;
	if (mob->prevMobile != NULL)
		mob->prevMobile->nextMobile = mob->nextMobile;
	else if (g_MobileListHead == mob)
		g_MobileListHead = mob->nextMobile;

	// Free name via operator delete
	free(mob->name);
	mob->name = NULL;

	// Decrement mobile count
	g_MobileCount--;

	// Destroy serial lists (attackerList first, then combatTargetList)
	CSerialList_Destructor(&mob->attackerList);
	CSerialList_Destructor(&mob->combatTargetList);

	// Chain to CContainer destructor
	CContainer_Destructor(&mob->container.item);
}

/*
 * 0x00470FF1 - CMobile::GetMaxWeight (vtable[0x1A4])
 *
 * Max carry weight = baseStr * 4 + 30. Binary deliberately ignores
 * strBonus.
 */
uint32_t
CMobile_GetMaxWeight(CMobile *this)
{
	return (uint32_t)this->baseStr * 4 + 30;
}

/*
 * 0x0047100F - CMobile::GetStoredWeight (vtable[0x124], 22 bytes)
 *
 * Returns the mobile's stored weight unconditionally, skipping the
 * IsNonCountable filter that CContainer's override applies.
 */
uint32_t
CMobile_GetStoredWeight(CMobile *this)
{
	return (uint32_t)this->container.storedWeight;
}

/*
 * 0x00471025 - CMobile::GetRelativeWeight
 *
 * Encumbrance percentage scaled by hp: injured mobs feel heavier.
 */
int
CMobile_GetRelativeWeight(CMobile *this)
{
	int maxWeight, hpPercent, relWeight;

	maxWeight = (int)CMobile_GetMaxWeight(this);
	hpPercent = CMobile_GetHPPercent(this);
	relWeight = (hpPercent * maxWeight) / 100;
	if (relWeight == 0)
		relWeight = 1;
	return (int)CContainer_GetWeight((CItem *)this) * 100 / relWeight;
}

/*
 * 0x0047107C - CMobile::GetEncumbrancePercent
 *
 * (weight * 100 / maxWeight), with maxWeight clamped to 1.
 */
int
CMobile_GetEncumbrancePercent(CMobile *this)
{
	int maxWeight;

	maxWeight = (int)CMobile_GetMaxWeight(this);
	if (maxWeight == 0)
		maxWeight = 1;
	return (int)CContainer_GetWeight((CItem *)this) * 100 / maxWeight;
}

/*
 * 0x004710BC - CMobile::GetEncumbranceLimit
 *
 * Combined stat/skill "power" score used by pack merging: sum of base
 * stats + half of max vitals + each base skill / 100.
 */
int
CMobile_GetEncumbranceLimit(CMobile *this)
{
	int sum;
	int i;

	sum = 0;
	sum += (uint16_t)this->baseStr;
	sum += (uint16_t)this->baseInt;
	sum += (uint16_t)this->baseDex;
	sum += (int)this->hp / 2;
	sum += (int)this->mana / 2;
	sum += (int)this->stamina / 2;

	for (i = 0; i < 50; i++)
		sum += (unsigned int)CMobile_GetBaseSkill(this, (int8_t)i) / 100;

	return sum;
}

/*
 * 0x00471188 - CMobile::CheckSurfaceOf (vtable[0x048])
 *
 * Returns 0 if other is this mob's own follower (mount) or if a vendor
 * tries to stand on any mobile; otherwise delegates to
 * other->GetSurfaceFlags(movementType).
 */
int
CMobile_CheckSurfaceOf_VT(CItem *self, CItem *other)
{
	CMobile *mob = (CMobile *)self;

	if (mob->hasFollowers != 0) {
		if (VT_IsContainer(other) && VT_IsMobile(other)) {
			CMobile *otherMob = (CMobile *)other;
			if (otherMob->owner == (CMobile *)self)
				return 0;
		}
	}

	if (((int (*)(void *))VT_FN(self, VT_CHECK_EC))(self)) {
		if (VT_IsContainer(other) && VT_IsMobile(other))
			return 0;
	}

	int moveType = ((int (*)(void *))VT_FN(self, VT_GET_MOVEMENT_TYPE))(self) & 0xFF;
	return ((int (*)(void *, int))VT_FN(other, VT_GET_SURFACE_FLAGS))(other, moveType);
}

/*
 * 0x0047122A - CPlayer::CheckSurfaceOf (vtable[0x048])
 *
 * Adds the shove mechanic: a fully-rested moving player treats an
 * adjacent mobile within 8 z as passable, records it as g_MoveBlocker,
 * and returns 0. Editing players are exempt.
 */
int
CPlayer_CheckSurfaceOf_VT(CItem *self, CItem *other)
{
	int result;
	CItem *otherMob;

	result = CMobile_CheckSurfaceOf_VT(self, other);

	if (!(result & 0x40))
		return result;

	if (g_MoveCurrentPlayer != self)
		return result;

	if (!VT_IsContainer(other))
		return result;

	if (!VT_IsMobile(other))
		return result;

	otherMob = other;

	{
		int dist = Location_WrappedChebyshevDistance(CEntity_GetLocation(&self->resourceEntity.entity), CEntity_GetLocation(&otherMob->resourceEntity.entity));
		if (dist != 1)
			return result;
	}

	{
		int selfZ = (int)(int16_t)CEntity_GetLocation(&self->resourceEntity.entity)->z;
		int otherZ = (int)(int16_t)CEntity_GetLocation(&otherMob->resourceEntity.entity)->z;
		int dz = selfZ - otherZ;
		if (dz < 0)
			dz = -dz;
		if (dz >= 8)
			return result;
	}

	if (((CMobile *)self)->stamina != ((CMobile *)self)->maxStamina)
		return result;

	// Non-player or non-editing player: set blocker and clear flags
	if (!VT_IsPlayer(other)) {
		g_MoveBlocker = other;
		return 0;
	}
	if (!CPlayer_IsEditing((CPlayer *)other)) {
		g_MoveBlocker = other;
		return 0;
	}

	return result;
}

/*
 * 0x00471323 - CMobile::CheckSurface (vtable[0x044])
 *
 * Looks up land tile flags for tileID under this mob's movement type.
 */
int
CMobile_CheckSurface_VT(CItem *self, uint16_t tileID)
{
	int moveType;

	moveType = ((int (*)(void *))VT_FN(self, VT_GET_MOVEMENT_TYPE))(self) & 0xFF;
	return GetLandTileFlags(tileID, moveType);
}

/*
 * 0x00471350 - CMobile::GetStaminaBlocked (vtable[0x21C])
 *
 * Returns 1 if this mob or its mount still has stamina to move, 0
 * when fully depleted. Dismounts if the mount has disappeared.
 */
int
CMobile_GetStamina_VT(CMobile *self)
{
	int depleted;
	CMobile *mount;

	depleted = (CMobile_GetStamina(self) <= 0) ? 1 : 0;

	if (CMobile_IsMounted(self)) {
		mount = CMobile_GetMount(self);
		if (mount == NULL) {
			CMobile_Dismount(self);
		} else {
			// Recursive: check mount's stamina via vtable
			int mountStam = ((int (*)(CMobile *))VT_FN((CItem *)mount, VT_GET_STAMINA))(mount);
			if (mountStam == 0 && depleted == 0)
				return 0;
			return 1;
		}
	}
	return depleted;
}

/*
 * Combat list management
 */

/*
 * 0x004713C6 - CMobile::HandleStaminaDrain
 *
 * Charges per-step stamina based on movement weight plus encumbrance.
 * Mounted riders drain 1/3 and recursively drain the mount, warning
 * the rider the first time mount stamina crosses 10%.
 */
void
CMobile_HandleStaminaDrain(CMobile *this, int movementWeight)
{
	int drainAmount;
	int encPct;
	int weightAdd;

	drainAmount = movementWeight / 10;

	encPct = CMobile_GetEncumbrancePercent(this);
	if (encPct > 100)
		weightAdd = CMobile_GetRelativeWeight(this) * 10;
	else
		weightAdd = CMobile_GetRelativeWeight(this) / 10;
	drainAmount += weightAdd;

	if (CMobile_IsMounted(this)) {
		CMobile *mount = CMobile_GetMount(this);
		if (mount == NULL) {
			CMobile_Dismount(this);
		} else {
			int beforePct, afterPct;

			// GetStamina * 100 / GetMaxStamina (binary has no div-by-zero guard)
			beforePct = CMobile_GetStamina(mount) * 100 / CMobile_GetMaxStamina(mount);

			((void (*)(void *, int))VT_FN((CItem *)mount, VT_HANDLE_STAM_DRAIN))(mount, movementWeight);

			afterPct = CMobile_GetStamina(mount) * 100 / CMobile_GetMaxStamina(mount);

			// If crossed below 10%, warn rider via vtable[0x18] IsPlayer
			if (beforePct > 10 && afterPct <= 10) {
				if (VT_IsPlayer((CItem *)this))
					CPlayer_SystemMessage((CPlayer *)this, "Your horse is very fatigued.");
			}

			drainAmount = drainAmount / 3;
		}
	}

	this->staminaLossCounter += drainAmount;

	// While > 200, subtract 200 and call SetStamina(GetStamina()-1)
	while (this->staminaLossCounter > 200) {
		this->staminaLossCounter -= 200;
		((uint32_t (*)(void *, int))VT_FN((CItem *)this, VT_SET_STAMINA))(this, CMobile_GetStamina(this) - 1);
	}
}

/*
 * 0x0047152E - CMobile::StaminaRegenTick
 *
 * Counterweight to HandleStaminaDrain: decays staminaLossCounter and
 * grants +1 stamina for every -200 crossed.
 */
void
CMobile_StaminaRegenTick(CMobile *mob)
{
	mob->staminaLossCounter -= 18;
	while (mob->staminaLossCounter < -200) {
		mob->staminaLossCounter += 200;
		((uint32_t (*)(void *, int))VT_FN((CItem *)mob, VT_SET_STAMINA))(mob, CMobile_GetStamina(mob) + 1);
	}
}

/*
 * 0x00471591 - Stat regen bonus calculation
 *
 * If total base stats > 65, proportionally scales each stat down.
 * rate: decay intensity parameter (higher = more decay).
 * Formula: penalty = rate * 100 * (adjustedTotal - 35) / adjustedTotal
 *          scaleFactor = 10000 - penalty
 *          newBase[i] = ((base[i] - 10) * scaleFactor + 5000) / 10000 + 10
 * Called from resurrection with rate=10 or tag-computed rate.
 */
void
CMobile_StatRegenBonusCalc(CMobile *this, int rate)
{
	int total, i;
	int penalty, scaleFactor;
	int val, newVal;

	// Sum all 3 base stats
	total = 0;
	for (i = 0; i < 3; i++)
		total += (int)(int16_t)CMobile_GetBaseStat(this, i);

	// No decay if total stats <= 65
	if (total <= 65)
		return;

	total -= 30;
	penalty = rate * 100 * (total - 35) / total;
	scaleFactor = 10000 - penalty;

	// Apply proportional decay to each stat (minimum base 10)
	for (i = 0; i < 3; i++) {
		val = (int)(int16_t)CMobile_GetBaseStat(this, i) - 10;
		if (val < 0)
			val = 0;
		// Signed division with rounding: +5000 = half of 10000
		newVal = (val * scaleFactor + 5000) / 10000;
		CMobile_SetBaseStat(this, i, (uint16_t)(newVal + 10));
	}
}

/*
 * 0x00471679 - CMobile::SkillDecayFromRegen
 *
 * Same proportional-decay formula as the stat variant but applied to
 * all 50 skills once their total exceeds 1000.
 */
void
CMobile_SkillDecayFromRegen(CMobile *this, int rate)
{
	int total, i;
	int penalty, scaleFactor;
	unsigned int val, newVal;

	// Sum all 50 base skills
	total = 0;
	for (i = 0; i < 50; i++)
		total += CMobile_GetBaseSkill(this, (int8_t)i);

	// No decay if total skills <= 1000
	if (total <= 1000)
		return;

	// Same scaling formula: penalty based on excess over threshold
	penalty = rate * 100 * (total - 1000) / total;
	scaleFactor = 10000 - penalty;

	// Apply proportional decay to each skill (unsigned division)
	for (i = 0; i < 50; i++) {
		val = (unsigned int)CMobile_GetBaseSkill(this, (int8_t)i);
		newVal = (val * (unsigned int)scaleFactor + 5000u) / 10000u;
		CMobile_SetSkill(this, (int8_t)i, (uint16_t)newVal);
	}
}

/*
 * 0x00471747 - CMobile::SetAllCurrentStats
 *
 * Sets HP, stamina, and mana to the given value via vtable dispatch.
 */
void
CMobile_SetAllCurrentStats(CMobile *this, int value)
{
	((int (*)(void *, int, int))VT_FN((CItem *)this, VT_SET_HP))(this, value, 0);
	((int (*)(void *, int))VT_FN((CItem *)this, VT_SET_STAMINA))(this, value);
	((int (*)(void *, int))VT_FN((CItem *)this, VT_SET_MANA))(this, value);
}

/*
 * 0x0047178C - CMobile::IsInvulnerable
 *
 * Checks if mobile is invulnerable (statusFlags bit 1).
 */
int
CMobile_IsInvulnerable(CMobile *this)
{
	return CMobile_CheckStatusFlag(this, 1);
}

/*
 * 0x004717A1 - CMobile::IsSquelched
 *
 * Checks if mobile is squelched (statusFlags bit 4).
 */
int
CMobile_IsSquelched(CMobile *this)
{
	return CMobile_CheckStatusFlag(this, 4);
}

/*
 * 0x004717B6 - CPlayer::IsGhost
 *
 * Tests the ghost bit (statusFlags 0x02), set by OnDeath and cleared
 * by CPlayer_ProcessDeath.
 */
int
CPlayer_IsGhost(CPlayer *this)
{
	return CMobile_CheckStatusFlag(&this->mobile, 0x02);
}

/*
 * 0x004717CB - CMobile::CheckStatusFlag
 *
 * Returns 1 if any bit of flagBit is set in statusFlags.
 */
int
CMobile_CheckStatusFlag(CMobile *this, uint8_t flagBit)
{
	return (this->statusFlags & flagBit) ? 1 : 0;
}

/*
 * 0x004717F6 - CMobile::SetStatusFlag
 *
 * Sets or clears bits in statusFlags (server-side admin bits:
 * invulnerable/frozen/squelched/beeline). No client broadcast -
 * client-visible flags live in mobileFlags and use SetMobileFlag.
 */
void
CMobile_SetStatusFlag(CMobile *this, uint8_t mask, int set)
{
	if (set)
		this->statusFlags |= mask;
	else
		this->statusFlags &= ~mask;
}

/*
 * 0x0047183E - CMobile::SetStatusFlagsByte
 *
 * Overwrites the entire statusFlags byte; used by save restore.
 */
void
CMobile_SetStatusFlagsByte(CMobile *this, uint8_t flags)
{
	this->statusFlags = flags;
}

/*
 * 0x00471857 - CMobile::GetHPPercent
 *
 * hp * 100 / maxHp, or 0 when maxHp is 0.
 */
int
CMobile_GetHPPercent(CMobile *this)
{
	if (this->maxHp == 0)
		return 0;
	return (int)(this->hp * 100) / (int)this->maxHp;
}

/*
 * 0x00471888 - CMobile::EquipmentDecayTick
 *
 * Fires a "decay" script event on every equipped item (recursing into
 * containers, skipping the mount slot) and then on the mob's own
 * container contents.
 */
void
CMobile_EquipmentDecayTick(CMobile *this)
{
	int i;
	CItem *item;

	// Phase 1: equipment slots 0-25 (skip 25=mount)
	for (i = 0; i < 26; i++) {
		if (i == 25)
			continue;
		item = this->equipment[i];
		if (item == NULL)
			continue;
		if (CItem_IsContainer(item))
			CMobile_DecayContainerContents(item);
		CItem_DecayProcess(item); /* 0x0048F13A */
	}

	// Phase 2: walk container contents (backpack and sub-containers)
	CMobile_DecayContainerContents((CItem *)&this->container);
}

/*
 * 0x00471934 - CMobile::SetFrozenFlag
 *
 * Sets or clears the frozen bit (statusFlags 0x08).
 */
void
CMobile_SetFrozenFlag(CMobile *this, int value)
{
	CMobile_SetStatusFlag(this, 8, value);
}

/*
 * 0x0047194F - CMobile::DistXY
 *
 * Chebyshev distance from this mob to (x, y).
 */
int
CMobile_DistXY(CMobile *this, int x, int y)
{
	return ChebyshevDistXY(x, y, (int)(int16_t)this->container.item.resourceEntity.entity.location.x, (int)(int16_t)this->container.item.resourceEntity.entity.location.y);
}

/*
 * NPC AI Animation and State (0x004ACB07, 0x004AC9F5)
 */

/*
 * 0x0047197C - CMobile::BroadcastStatUpdate
 *
 * Sends an HP/mana/stamina/full-update packet to nearby players who
 * are either this mobile or have it in their target history. Skipped
 * when the mob is inside a container.
 */
void
CMobile_BroadcastStatUpdate(CMobile *this, int statType)
{
	char typeFlag;
	CVector allPlayers;
	CVector filteredList;
	uintptr_t *iter, *end;
	int count;
	uint8_t buf[32];

	if (this->container.item.parent != NULL)
		return;

	typeFlag = 0;
	CVector_Constructor(&allPlayers, &typeFlag);
	CVector_Constructor(&filteredList, &typeFlag);

	GetNearbyPlayers(&allPlayers, &this->container.item.resourceEntity.entity.location, 18);

	iter = (uintptr_t *)allPlayers.begin;
	end = (uintptr_t *)allPlayers.end;
	while (iter < end) {
		CMobile *player = (CMobile *)*iter;
		if (player == this || CPlayer_HasTargetedSerial(player, this->container.item.serial))
			CVector_PushBack(&filteredList, (uintptr_t)player);
		iter++;
	}

	count = filteredList.begin ? (int)(((char *)filteredList.end - (char *)filteredList.begin) / (ptrdiff_t)sizeof(uintptr_t)) : 0;
	if (count == 0)
		goto cleanup;

	switch (statType) {
	case 0:
		PacketManager_MakePacket_HP_HEALTH(buf, this->container.item.serial, (uint16_t)this->maxHp, (uint16_t)this->hp);
		SendToClientList(&filteredList, buf);
		break;
	case 1:
		PacketManager_MakePacket_MANA_HEALTH(buf, this->container.item.serial, (uint16_t)this->maxMana, (uint16_t)this->mana);
		SendToClientList(&filteredList, buf);
		break;
	case 2:
		PacketManager_MakePacket_FAT_HEALTH(buf, this->container.item.serial, (uint16_t)this->maxStamina, (uint16_t)this->stamina);
		SendToClientList(&filteredList, buf);
		break;
	case 3:
		PacketManager_MakePacket_HEALTH(buf, this->container.item.serial, (uint16_t)this->maxHp, (uint16_t)this->hp, (uint16_t)this->maxMana, (uint16_t)this->mana,
		        (uint16_t)this->maxStamina, (uint16_t)this->stamina);
		SendToClientList(&filteredList, buf);
		break;
	}

cleanup:
	CVector_Destructor(&filteredList);
	CVector_Destructor(&allPlayers);
}

/*
 * 0x00471BAE - CMobile::GetName (vtable[0x34])
 *
 * Returns mob->name or "(null)" if unset.
 */
char *
CMobile_GetName_VT(CItem *self)
{
	CMobile *mob = (CMobile *)self;

	if (mob->name != NULL)
		return mob->name;
	return "(null)";
}

/*
 * 0x00471BDF - CMobile::GetNameAndHue
 *
 * Writes the notoriety hue relative to player into *outHue and
 * returns this mobile's paperdoll-title name.
 */
char *
CMobile_GetNameAndHue(CItem *mobile, uint16_t *outHue, CItem *player)
{
	int notoriety;

	notoriety = ((int (*)(void *, void *))VT_FN(mobile, VT_GET_NOTORIETY))(mobile, player);
	*outHue = g_NotorietyHueTable[notoriety];
	return ((char *(*)(void *, int))VT_FN(mobile, VT_SPEAK_SYS_MSG))(mobile, 1);
}

/*
 * 0x00471C19 - CMobile::PickOtherStat
 *
 * Given a stat index (0=STR, 1=INT, 2=DEX), picks one of the other
 * two stats randomly using GetRandomRange(0, 1).
 */
int
CMobile_PickOtherStat(CMobile *self, int statIdx)
{
	int choice1, choice2;

	USED(self);
	choice1 = 0;
	choice2 = 2;
	switch (statIdx) {
	case 0:
		choice1 = 2;
		choice2 = 1;
		break;
	case 2:
		choice1 = 0;
		choice2 = 1;
		break;
	case 1:
		choice1 = 2;
		choice2 = 0;
		break;
	}
	if (GetRandomRange(0, 1))
		return choice1;
	return choice2;
}

/*
 * 0x00471CA7 - CMobile::StatAdjust
 *
 * Adjusts one of the other two base stats by delta - the "robbed" stat
 * when OnStatChange needs to free room under the cap.
 *
 * FIXED: earlier revision adjusted the same statIdx. Binary picks a
 * different stat via PickOtherStat before applying delta.
 */
void
CMobile_StatAdjust(CMobile *self, int statIdx, int delta)
{
	int otherIdx;
	int curVal;

	otherIdx = CMobile_PickOtherStat(self, statIdx);
	curVal = (int)(CMobile_GetBaseStat_Wrap(self, otherIdx) & 0xFFFF);
	CMobile_SetBaseStat_VT(self, otherIdx, curVal + delta);
}

/*
 * 0x00471CE6 - InitStatCurve
 *
 * Builds the two-segment quadratic probability curve used by
 * OnStatChange (breakpoints 65/170/225).
 */
void
InitStatCurve(int maxStats)
{
	double a, b, c, d, e, f;

	a = 65.0;
	b = 0.0;
	c = 170.0;
	d = (double)maxStats;
	e = 225.0;
	f = 1000.0;

	g_StatCurveBase = b;
	g_StatCurve.xOff1 = a;
	g_StatCurve.coeff1 = (d - b) / ((c - a) * (c - a));
	g_StatCurve.thresh = c;
	g_StatCurve.midY = d;
	g_StatCurve.xOff2 = c;
	g_StatCurve.coeff2 = (f - d) / ((e - c) * (e - c));
}

/*
 * 0x00471DCA - InitSkillCurve
 *
 * Two-segment quadratic probability curve for skill advancement
 * (breakpoints 1000/6000/7000).
 */
void
InitSkillCurve(int maxSkill)
{
	double a, b, c, d, e, f;

	a = 1000.0;
	b = 0.0;
	c = 6000.0;
	d = (double)maxSkill;
	e = 7000.0;
	f = 1000.0;

	g_SkillCurveBase = b;
	g_SkillCurve.xOff1 = a;
	g_SkillCurve.coeff1 = (d - b) / ((c - a) * (c - a));
	g_SkillCurve.thresh = c;
	g_SkillCurve.midY = d;
	g_SkillCurve.xOff2 = c;
	g_SkillCurve.coeff2 = (f - d) / ((e - c) * (e - c));
}

/*
 * 0x00471EAE - CalcDecayChance
 *
 * Per-1000 decay probability for NotifySkillGain, evaluated on the
 * two-segment curve set up by InitSkillCurve.
 */
static int
CalcDecayChance(int totalWeight)
{
	double w = (double)totalWeight;

	if (w <= g_SkillCurve.thresh) {
		double d = w - g_SkillCurve.xOff1;
		return (int)(d * d * g_SkillCurve.coeff1 + g_SkillCurveBase);
	} else {
		double d = w - g_SkillCurve.xOff2;
		return (int)(d * d * g_SkillCurve.coeff2 + g_SkillCurve.midY);
	}
}

/*
 * 0x00471F1A - CMobile::IsHumanBodyType
 *
 * True when bodyType is in the human range 0x190-0x193.
 */
int
CMobile_IsHumanBodyType(CMobile *this)
{
	// Binary calls GetBodyType twice (once per comparison).
	if ((uint16_t)CResourceEntity_GetBodyType((CItem *)this) >= 0x190 && (uint16_t)CResourceEntity_GetBodyType((CItem *)this) <= 0x193)
		return 1;
	return 0;
}

/*
 * 0x00471F62 - CMobile::OnStatChange (vtable[0x238])
 *
 * Enforces the 225 total-stat cap: hard-subtracts any excess, and on a
 * positive delta rolls the quadratic probability curve to randomly
 * reduce another stat.
 */
int
CMobile_OnStatChange_VT(CMobile *self, int statIdx, int delta)
{
	int totalStats;
	int i;
	int excess;
	double statD;
	int chance;
	int j;

	totalStats = 0;
	for (i = 0; i < 3; i++)
		totalStats += (int)(int16_t)CMobile_GetBaseStat(self, i);

	// Over cap: subtract excess from the given stat
	if (totalStats > 0xE1) {
		excess = totalStats - 0xE1;
		CMobile_StatAdjust(self, statIdx, -excess);
		return 1;
	}

	// delta <= 0: no random reduction needed
	if (delta <= 0)
		return 0;

	// Quadratic probability curve (parameters set by InitStatCurve)
	statD = (double)totalStats;
	if (statD <= g_StatCurve.thresh) {
		chance = (int)((statD - g_StatCurve.xOff1) * (statD - g_StatCurve.xOff1) * g_StatCurve.coeff1 + g_StatCurveBase);
	} else {
		chance = (int)((statD - g_StatCurve.xOff2) * (statD - g_StatCurve.xOff2) * g_StatCurve.coeff2 + g_StatCurve.midY);
	}

	// Try delta times to randomly reduce stat
	for (j = 0; j < delta; j++) {
		if (GetRandom(1000) < chance)
			CMobile_StatAdjust(self, statIdx, -1);
	}

	return 0;
}

/*
 * 0x0047208A - CMobile::SetBaseStat_VT
 *
 * Thin vtable[0x1F0] wrapper that truncates value to 16 bits before
 * dispatch.
 */
int16_t
CMobile_SetBaseStat_VT(CMobile *this, int statIndex, int value)
{
	return ((int16_t (*)(void *, int, uint16_t))VT_FN((CItem *)this, VT_SET_BASE_STAT))(this, statIndex, (uint16_t)value);
}

/*
 * 0x004720AE - CMobile::GetBaseStat_Wrap
 *
 * Trampoline to CMobile_GetBaseStat.
 */
int16_t
CMobile_GetBaseStat_Wrap(CMobile *this, int statIndex)
{
	return CMobile_GetBaseStat(this, statIndex);
}

/*
 * 0x004720C7 - CMobile::SetStatAbs (vtable[0x23C])
 *
 * Absolute stat set: writes newValue, then notifies OnStatChange with
 * the delta. If OnStatChange returns nonzero the binary re-reads the
 * stat but discards the value - a harmless binary quirk preserved here.
 */
int16_t
CMobile_SetStatAbs(CMobile *this, int type, int newValue)
{
	int old;
	int16_t result;
	int delta;

	old = (int)(CMobile_GetBaseStat_Wrap(this, type) & 0xFFFF);

	result = CMobile_SetBaseStat_VT(this, type, newValue);

	delta = newValue - old;

	if (((int (*)(void *, int, int))VT_FN((CItem *)this, VT_ON_STAT_CHANGE))(this, type, delta)) {
		// 0x00472115: re-GetBaseStat via wrapper (result stored but overwritten)
		result = CMobile_GetBaseStat_Wrap(this, type);
	}

	return result;
}

/*
 * 0x0047212F - CMobile::OpenBankGump
 *
 * Opens the bank box UI on the named mobile for a viewing player
 * (NULL means the mobile itself). Sends the bank's "weight/items"
 * status message, the bank container itself, the gump packet, and
 * the contents update.
 */
void
CMobile_OpenBankGump(CMobile *mob, CPlayer *player)
{
	CContainer *bankBox;
	int count;
	uint16_t weight;
	char buf[128];
	uint8_t obuf[0x42C];
	uint16_t gumpId;

	if (player == NULL) {
		if (VT_IsPlayer(&mob->container.item))
			player = (CPlayer *)mob;
	}
	if (player == NULL)
		return;

	FixBank(mob);
	bankBox = (CContainer *)mob->equipment[29];

	weight = bankBox->storedWeight;
	count = CContainer_CountItems(bankBox, 1);
	sprintf(buf, "Bank container has %d items, %d stones", count, weight);

	PacketManager_MakePacket_TEXT(obuf, (CItem *)player, (CItem *)player, 6, buf, 0x3B2, 3);
	SendToClient((CItem *)player, obuf, -1);

	// Clear itemFlags, send entity via vtable[0x134], restore flag
	bankBox->item.itemFlags = 0;
	((void (*)(void *, CItem *, int))VT_FN(&bankBox->item, VT_SEND_ENTITY_UPDATE))(bankBox, (CItem *)player, 1);
	bankBox->item.itemFlags |= ItemFlag_ServerOnly;

	gumpId = CItem_GetContainerGump(&bankBox->item);
	SendOpenGump(player, CMobile_GetSerial((CMobile *)player), bankBox->item.serial, gumpId);

	CContainer_SendContainerContents(bankBox, (CItem *)player, CMobile_GetSerial((CMobile *)player), 0);
}

/*
 * 0x00472277 - CMobile::IsUsingOrderShield
 *
 * True if equipment[2] is art 0x1BC4 or 0x1BC5.
 */
int
CMobile_IsUsingOrderShield(CItem *ent)
{
	CMobile *mob = (CMobile *)ent;
	CItem *shield = mob->equipment[2];

	if (shield == NULL)
		return 0;
	if ((uint16_t)CEntity_GetBodyType(shield) == 0x1BC4)
		return 1;
	if ((uint16_t)CEntity_GetBodyType(shield) == 0x1BC5)
		return 1;
	return 0;
}

/*
 * 0x0047231E - IsUsingChaosShield
 *
 * True if equipment[2] is art 0x1BC3.
 */
int
IsUsingChaosShield(CItem *ent)
{
	CMobile *mob = (CMobile *)ent;
	CItem *shield = mob->equipment[2];

	if (shield == NULL)
		return 0;
	if ((uint16_t)CEntity_GetBodyType(shield) == 0x1BC3)
		return 1;
	return 0;
}

/*
 * 0x004723B1 - CMobile::StatCheck (vtable[0x228])
 *
 * Full-hp only: fires script event 0x3E and returns its result.
 */
int
CMobile_StatCheck_VT(CMobile *self)
{
	if (self->hp < self->maxHp)
		return 0;
	return (int)(intptr_t)Entity_ExecuteEvent(&self->container.item.resourceEntity.entity, 0x3E, self->container.item.serial);
}

/*
 * 0x004723F4 - CMobile::GetNotoriety (vtable[0x180])
 *
 * Bumps an innocent result to grey for non-human, masterless mobs.
 */
int
CMobile_GetNotoriety_VT(CItem *self, CMobile *viewer)
{
	int noto;

	noto = CMobile_ComputeNotoriety((CMobile *)self, viewer);
	if (noto == 1) {
		if (!CMobile_IsHumanBodyType((CMobile *)self)) {
			if (!CMobile_HasBoss((CMobile *)self))
				return 3;
		}
	}
	return noto;
}

/*
 * 0x0047243A - CMobile::CanBeFreelyAggressed (vtable[0x188])
 *
 * Non-human wild creatures are always freely aggressable; everyone
 * else defers to CanBeFreelyAggressed_Impl.
 */
int
CMobile_CanBeFreelyAggressed_VT(CMobile *self, CMobile *attacker, AggroInfo *info)
{
	if (!CMobile_IsHumanBodyType(self)) {
		if (!CMobile_HasBoss(self))
			return 1;
	}
	return CMobile_CanBeFreelyAggressed_Impl(self, attacker, info);
}

/*
 * 0x004724E6 - CMobile::EquipOnMobile (vtable[0x0C0])
 *
 * Mobiles cannot be worn on another mobile: always returns 0.
 */
int
CMobile_EquipOnMobile_VT(CItem *self, CMobile *mob, int layer)
{
	USED(self);
	USED(mob);
	USED(layer);
	return 0;
}

/*
 * 0x004724F5 - CMobile::EquipIterate (vtable[0x1B4])
 *
 * Recurses into the first accessible-contents equipment slot and
 * returns its result, otherwise writes 3 to *result and returns 0.
 */
int
CMobile_EquipIterate_VT(CMobile *self, void *callback, int *result)
{
	int i;

	for (i = 0; i < 0x1A; i++) {
		if (self->equipment[i] == NULL)
			continue;
		if (!((int (*)(CItem *))VT_FN(self->equipment[i], VT_HAS_ACCESSIBLE_CONTENTS))(self->equipment[i]))
			continue;
		// Recursive call via vtable[0x1B4]
		return ((int (*)(CItem *, void *, int *))VT_FN(self->equipment[i], VT_EQUIP_ITERATE))(self->equipment[i], callback, result);
	}
	if (result != NULL)
		*result = 3;
	return 0;
}

/*
 * 0x00472591 - CMobile::SetMobileFlag
 *
 * Sets the bit in mobileFlags and broadcasts appearance if it changed.
 */
void
CMobile_SetMobileFlag(CMobile *this, uint8_t mask)
{
	if (this->mobileFlags & mask)
		return; // already set

	this->mobileFlags |= mask;

	CItem_NotifyNearbyUpdate((CItem *)this, 0);
}

/*
 * 0x004725D9 - CMobile::ClearMobileFlag
 *
 * Clears the bit in mobileFlags and broadcasts appearance if it
 * changed.
 */
void
CMobile_ClearMobileFlag(CMobile *this, uint8_t mask)
{
	if (!(this->mobileFlags & mask))
		return; // not set

	this->mobileFlags &= ~mask;

	CItem_NotifyNearbyUpdate((CItem *)this, 0);
}

/*
 * 0x0047262A - CMobile::SetMobileFlagsByte
 *
 * Overwrites the entire mobileFlags byte.
 */
void
CMobile_SetMobileFlagsByte(CMobile *this, uint8_t flags)
{
	this->mobileFlags = flags;
}

/*
 * 0x00472643 - CMobile::GetMobileFlags
 *
 * Returns the mobileFlags byte.
 */
uint8_t
CMobile_GetMobileFlags(CMobile *this)
{
	return this->mobileFlags;
}

/*
 * 0x00472657 - CMobile::CheckMobileFlag
 *
 * Returns 1 if any bit of flag (low byte) is set in mobileFlags.
 */
int
CMobile_CheckMobileFlag(CMobile *this, int flag)
{
	int val;

	val = (int)(this->mobileFlags) & (flag & 0xff);
	return val != 0 ? 1 : 0;
}

/*
 * 0x00472682 - CMobile::GetStatusFlags (vtable[0x170])
 *
 * Folds mobileFlags into *flags, then delegates to CItem version.
 */
void
CMobile_GetStatusFlags_VT(CItem *self, uint8_t *flags)
{
	uint8_t mf;

	mf = CMobile_GetMobileFlags((CMobile *)self);
	*flags |= mf;
	CItem_GetStatusFlags_VT(self, flags);
}

/*
 * 0x004726AF - CMobile::ApplyStatusFlags (vtable[0x16C])
 *
 * Restores mobileFlags bit 0 from flags, then chains to the CItem
 * version for the remaining bits.
 */
void
CMobile_ApplyStatusFlags_VT(CItem *self, int flags)
{
	CMobile_SetMobileFlagsByte((CMobile *)self, flags & 1);
	CItem_ApplyStatusFlags(self, flags);
}

/*
 * 0x004726EA - CollectNearbyMobiles
 *
 * Appends entities in range from g_NPCMap and g_ItemMap to list.
 */
void
CollectNearbyMobiles(CVector *list, CLocation *loc, int range)
{
	CEntityMap_RangeQuery(g_NPCMap, list, loc->x, loc->y, range);
	CEntityMap_RangeQuery(g_ItemMap, list, loc->x, loc->y, range);
}

/*
 * 0x0047273B - CMobile::ClearResistFlags
 *
 * Zeroes resistFlags.
 */
void
CMobile_ClearResistFlags(CMobile *this)
{
	this->resistFlags = 0;
}

/*
 * 0x00472750 - CMobile::GetResistFlags
 *
 * Returns resistFlags (offset 0x37A), zero-extended.
 */
int
CMobile_GetResistFlags(CMobile *this)
{
	return (int)(unsigned int)this->resistFlags;
}

/*
 * 0x0047279E - CMobile::SetResistFlag
 *
 * Sets (value != 0) or clears bits of flag in resistFlags.
 */
void
CMobile_SetResistFlag(CMobile *mob, int flag, int value)
{
	if (value)
		mob->resistFlags |= (uint8_t)flag;
	else
		mob->resistFlags &= ~(uint8_t)flag;
}

/*
 * 0x004727E1 - CMobile::ClearVulnFlags
 *
 * Zeroes vulnFlags.
 */
void
CMobile_ClearVulnFlags(CMobile *this)
{
	this->vulnFlags = 0;
}

/*
 * 0x004727F6 - CMobile::GetVulnFlags
 *
 * Returns vulnFlags zero-extended.
 */
int
CMobile_GetVulnFlags(CMobile *this)
{
	return (int)(unsigned int)this->vulnFlags;
}

/*
 * 0x00472844 - CMobile::SetVulnFlag
 *
 * Sets (value != 0) or clears bits of flag in vulnFlags.
 */
void
CMobile_SetVulnFlag(CMobile *mob, int flag, int value)
{
	if (value)
		mob->vulnFlags |= (uint8_t)flag;
	else
		mob->vulnFlags &= ~(uint8_t)flag;
}

/*
 * 0x00472887 - CMobile::SetupMasksFromObjVars
 *
 * Rebuilds resist/vuln bitmasks from the mob's Good/Evil/Humans/Meat/
 * CarnivoreMeat resource entries. Bits: 1=meat, 2=human, 4=good-aligned,
 * 8=evil-aligned.
 */
void
CMobile_SetupMasksFromObjVars(CMobile *mob)
{
	int evil, good, humans, meat, carnivoreMeat;

	evil = 0;
	good = 0;
	humans = 0;
	meat = 0;
	carnivoreMeat = 0;

	CItem_GetObjVarResType(&mob->container.item, &evil, g_ResTypeId_Evil, 3, 2);
	CItem_GetObjVarResType(&mob->container.item, &good, g_ResTypeId_Good, 3, 2);
	CItem_GetObjVarResType(&mob->container.item, &humans, g_ResTypeId_Humans, 3, 2);
	CItem_GetObjVarResType(&mob->container.item, &meat, g_ResTypeId_Meat, 3, 2);
	CItem_GetObjVarResType(&mob->container.item, &carnivoreMeat, g_ResTypeId_CarnivoreMeat, 3, 2);

	// ClearAllResistFlags (0x0047273B) + ClearAllVulnFlags (0x004727E1)
	CMobile_ClearResistFlags(mob);
	CMobile_ClearVulnFlags(mob);

	if (good > 0) {
		CMobile_SetResistFlag(mob, 8, 1);
		CMobile_SetVulnFlag(mob, 4, 1);
	}

	if (evil > 0) {
		CMobile_SetResistFlag(mob, 4, 1);
		CMobile_SetVulnFlag(mob, 8, 1);
		CMobile_SetVulnFlag(mob, 2, 1);
	}

	if (meat > 0 || carnivoreMeat > 0)
		CMobile_SetResistFlag(mob, 1, 1);

	if (meat < 0 || carnivoreMeat < 0)
		CMobile_SetVulnFlag(mob, 1, 1);

	if (humans > 0)
		CMobile_SetResistFlag(mob, 2, 1);

	if (humans < 0)
		CMobile_SetVulnFlag(mob, 2, 1);
}

/*
 * 0x004729D4 - CMobile::SetAlignment
 *
 * Clears existing Good/Evil resources and reseeds resources and
 * resist/vuln flags for 0=NONE, 1=GOOD, 2=EVIL, or 3=NEUTRAL. Non-EC
 * creature bodies also get CarnivoreMeat.
 */
void
CMobile_SetAlignment(CMobile *mob, int alignment)
{
	CItem *item = &mob->container.item;

	// Remove existing Good and Evil resources (up to 1000 each)
	CResourceEntity_RemoveResource(item, 1000, g_ResTypeId_Good);
	CResourceEntity_RemoveResource(item, 1000, g_ResTypeId_Evil);

	if (alignment == 1) {
		if (g_ResTypeId_Evil != 0 && g_ResTypeId_Good != 0) {
			CResourceEntity_AddNode(item, (uint16_t)g_ResTypeId_Good, 3, 1, 0, 1, 0);
			CResourceEntity_AddNode(item, (uint16_t)g_ResTypeId_Evil, 2, 1, -1, 0, 0);
			CMobile_SetResistFlag(mob, 8, 1);
			CMobile_SetResistFlag(mob, 4, 0);
			CMobile_SetVulnFlag(mob, 4, 1);
		}
	} else if (alignment == 2) {
		if (g_ResTypeId_Good != 0 && g_ResTypeId_Evil != 0) {
			CResourceEntity_AddNode(item, (uint16_t)g_ResTypeId_Evil, 3, 1, 0, 1, 0);
			CResourceEntity_AddNode(item, (uint16_t)g_ResTypeId_Good, 2, 1, -1, 0, 0);
			CResourceEntity_AddNode(item, (uint16_t)g_ResTypeId_Humans, 2, 1, -1, 0, 0);
			CMobile_SetResistFlag(mob, 4, 1);
			CMobile_SetResistFlag(mob, 8, 0);
			CMobile_SetVulnFlag(mob, 8, 1);
			CMobile_SetVulnFlag(mob, 2, 1);
		}
	} else if (alignment == 3) {
		CResourceEntity_AddNode(item, (uint16_t)g_ResTypeId_Meat, 2, 1, -3, 0, 0);
		CMobile_SetVulnFlag(mob, 1, 1);
	}

	// vtable[0xEC] (CheckEC) returns 0 for CMobile (stub 0x0044A7D0),
	// returns 1 for CNPC guards (0x004E04C0).
	if (CMobile_IsCreatureBody(mob)) {
		if (((int (*)(void *))VT_FN(item, VT_CHECK_EC))(item) == 0) {
			CResourceEntity_AddNode(item, (uint16_t)g_ResTypeId_CarnivoreMeat, 2, 1, -3, 0, 0);
		}
	}
}

/*
 * 0x00472B78 - CMobile::SetInvulnerable (vtable[0x230])
 *
 * Sets status flag 1 (invulnerable) on the mobile.
 */
void
CMobile_SetInvulnerable_VT(CMobile *mob)
{
	CMobile_SetStatusFlag(mob, 1, 1);
}

/*
 * 0x00472B8F - CMobile::ClearInvulnerable (vtable[0x234])
 *
 * Clears status flag 1 (invulnerable) on the mobile.
 */
void
CMobile_ClearInvulnerable_VT(CMobile *mob)
{
	CMobile_SetStatusFlag(mob, 1, 0);
}

/*
 * 0x00472BA6 - CMobile::IsMounted
 *
 * True if equipment slot 25 (mount) is filled.
 */
int
CMobile_IsMounted(CMobile *this)
{
	return this->equipment[25] != NULL;
}

/*
 * 0x00472BC6 - CMobile::GetMount
 *
 * Resolves savedRidingSerial to a mobile, NULL if missing or not a
 * mobile.
 */
CMobile *
CMobile_GetMount(CMobile *this)
{
	CItem *entity;

	entity = CWorld_FindBySerial(g_World, this->savedRidingSerial);
	if (entity == NULL)
		return NULL;
	if (!VT_IsMobile(entity))
		return NULL;
	return (CMobile *)entity;
}

/*
 * 0x00472C09 - CMobile::CanBeMounted
 *
 * Only human bodies (0x190, 0x191, 0x3DB) may ride.
 */
int
CMobile_CanBeMounted(CMobile *this)
{
	uint16_t bt;

	bt = (uint16_t)CResourceEntity_GetBodyType(&this->container.item);
	if (bt == 0x190 || bt == 0x191 || bt == 0x3DB)
		return 1;
	return 0;
}

/*
 * 0x00472C4C - CMobile_GetMountBodyType
 *
 * Maps the mount creature body to its mounted-art ID (horse/llama/
 * ostard variants).
 */
static uint16_t
CMobile_GetMountBodyType(CMobile *mount)
{
	uint16_t bt;
	int idx;

	bt = (uint16_t)CResourceEntity_GetBodyType(&mount->container.item);
	idx = (int)bt - 200;

	// Binary switch table at 0x00472CBC (29 entries, only 4 non-default):
	// body 200 (idx 0)  -> 0x3E9F (horse)
	// body 204 (idx 4)  -> 0x3EA2 (llama)
	// body 226 (idx 26) -> 0x3EA0 (ostard)
	// body 228 (idx 28) -> 0x3EA1
	// All others -> 0x3E9F (default)
	switch (idx) {
	case 4:  // body 204
		return 0x3EA2;
	case 26: // body 226
		return 0x3EA0;
	case 28: // body 228
		return 0x3EA1;
	default:
		return 0x3E9F;
	}
}

/*
 * 0x00472CD9 - CMobile::Mount
 *
 * Builds the mount art item, equips it to slot 25, relocates rider to
 * the mount's position and hides the mount mob inside the rider.
 */
int
CMobile_Mount(CMobile *rider, CMobile *mount)
{
	CItem *mountItem;
	CLocation tempLoc;
	CLocation *mountLoc;
	uint8_t dir;

	mountItem = CWorld_CreateItem(g_World, CMobile_GetMountBodyType(mount));
	if (mountItem == NULL)
		return 0;

	// Copy mount color to the visual item
	mountItem->resourceEntity.entity.color = mount->container.item.resourceEntity.entity.color;

	// Binary checks CanBeMounted AFTER creating item
	if (!CMobile_CanBeMounted(rider))
		goto cleanup;
	if (mountItem == NULL)
		goto cleanup;

	if (((int (*)(void *, void *, int))VT_FN(mountItem, VT_EQUIP_ON_MOBILE))(mountItem, rider, 25) != 1)
		goto cleanup;

	if (!ValidateInWorld(mountItem)) {
		mountItem = NULL;
		return 0;
	}

	CItem_Setup(mountItem, 1, CEntity_GetLocation(&rider->container.item.resourceEntity.entity), 0, 1);

	mountLoc = ((CLocation * (*)(void *)) VT_FN(&mount->container.item, VT_GET_LOCATION))(&mount->container.item);
	CLocation_SetLoc(&tempLoc, mountLoc);

	dir = ((int (*)(void *))VT_FN(&mount->container.item, VT_GET_DIRECTION))(&mount->container.item) & 0x07;

	((void (*)(void *))VT_FN(&rider->container.item, VT_DETACH_SPATIAL))(&rider->container.item);

	CMobile_SetDirection((CItem *)rider, (uint32_t)(dir & 0xFF));

	((void (*)(void *))VT_FN(&mount->container.item, VT_HIDE))(&mount->container.item);

	((void (*)(void *, void *))VT_FN(&rider->container.item, VT_DROP_AT_FEET))(&rider->container.item, &tempLoc);

	CLocation_Set(&tempLoc, -1, -1, -1);

	((void (*)(void *, void *, void *))VT_FN(&mount->container.item, VT_ADD_TO_CONTAINER))(&mount->container.item, &rider->container.item, &tempLoc);

	rider->savedRidingSerial = mount->container.item.serial;

	return 1;

cleanup:
	if (mountItem != NULL)
		((void (*)(void *))VT_FN(mountItem, VT_DELETE))(mountItem);
	mountItem = NULL;
	return 0;
}

/*
 * 0x00472E3D - CMobile::Dismount
 *
 * Re-spawns the mount mob at the rider's position facing the rider,
 * drops the mount art item, and clears savedRidingSerial. Returns 0
 * if the mount mob is missing - the art item is still destroyed.
 */
int
CMobile_Dismount(CMobile *rider)
{
	CItem *entity;
	CMobile *mountMob;
	CItem *equipItem;
	CLocation tempLoc;

	if (!CMobile_IsMounted(rider))
		return 0;

	// CWorld_FindBySerial(g_World, savedRidingSerial)
	entity = CWorld_FindBySerial(g_World, rider->savedRidingSerial);

	// Check entity exists and is a mobile via vtable[0xD0]
	if (entity == NULL || !VT_IsMobile(entity)) {
		// Delete equipment[25] via vtable[0x90], return 0
		// Binary has redundant NULL check (MSVC artifact)
		if (rider->equipment[25] != NULL) {
			if (rider->equipment[25] != NULL)
				((void (*)(void *))VT_FN(rider->equipment[25], VT_DELETE))(rider->equipment[25]);
		}
		return 0;
	}

	mountMob = (CMobile *)entity;

	CLocation_SetLoc(&tempLoc, ((CLocation * (*)(void *)) VT_FN((CItem *)mountMob, VT_GET_LOCATION))((CItem *)mountMob));

	equipItem = rider->equipment[25];
	if (equipItem != NULL)
		((void (*)(void *))VT_FN(equipItem, VT_DELETE))(equipItem);

	((void (*)(void *))VT_FN((CItem *)mountMob, VT_HIDE))((CItem *)mountMob);

	CMobile_SetDirection((CItem *)mountMob, ((int (*)(void *))VT_FN((CItem *)rider, VT_GET_DIRECTION))((CItem *)rider) & 7);

	((void (*)(void *, CLocation *))VT_FN((CItem *)mountMob, VT_DROP_AT_FEET))((CItem *)mountMob, &tempLoc);

	rider->savedRidingSerial = 0;

	return 1;
}

/*
 * 0x004730D0 - CheckNeedEquipUpdate
 *
 * Dispatches mob->GetNotoriety(viewer).
 */
int
CheckNeedEquipUpdate(CItem *mob, CItem *viewer)
{
	return ((int (*)(CItem *, CItem *))VT_FN(mob, VT_GET_NOTORIETY))(mob, viewer);
}

/*
 * 0x004730E7 - CMobile::SendEntityUpdate (vtable[0x134])
 *
 * Sends ZMOVE to self, otherwise EQUIPPED_MOB or NAKED_MOB to viewer
 * hued by notoriety. Skips ServerOnly, stale, or removed mobs.
 */
void
CMobile_SendEntityUpdate_VT(CItem *self, CItem *viewer, int withEquip)
{
	uint8_t pktBuf[0x2040];

	if (self->itemFlags & 0x10)
		return;

	if (CWorld_FindBySerial(g_World, self->serial) != self)
		return;
	if (self->resourceEntity.entity.removedFromWorld)
		return;

	// Self-update: send ZMOVE
	if (viewer == self) {
		PacketManager_MakePacket_ZMOVE(pktBuf, (CMobile *)self);
		SendToClient(viewer, pktBuf, -1);
		return;
	}

	// With equipment: send EQUIPPED_MOB
	if (withEquip) {
		int flag = CheckNeedEquipUpdate(self, viewer);
		PacketManager_MakePacket_EQUIPPED_MOB(pktBuf, (CMobile *)self, (uint8_t)flag);
		SendToClient(viewer, pktBuf, -1);
		return;
	}

	// Without equipment: send NAKED_MOB
	{
		int flag = CheckNeedEquipUpdate(self, viewer);
		PacketManager_MakePacket_NAKED_MOB(pktBuf, (CMobile *)self, (uint8_t)flag);
		SendToClient(viewer, pktBuf, -1);
	}
}

/*
 * 0x004731FF - CMobile::SendUpdateToList (vtable[0x130])
 *
 * Buckets recipients by notoriety (up to 7 groups) and sends one
 * EQUIPPED_MOB or NAKED_MOB packet per bucket.
 */
void
CMobile_SendUpdateToList_VT(CItem *self, CVector *players, int mode)
{
	uint8_t pktBuf[0x2040];
	CVector groups[7];
	int notoValues[7];
	int notoCount;
	uintptr_t *iter;
	int curNoto;
	int i, j;
	char typeFlags[7];

	// Skip server-only entities
	if (self->itemFlags & 0x10)
		return;

	if (CWorld_FindBySerial(g_World, self->serial) != self)
		return;
	if (self->resourceEntity.entity.removedFromWorld)
		return;

	// Initialize 7 CVector groups
	for (i = 0; i < 7; i++)
		CVector_Constructor(&groups[i], &typeFlags[i]);

	notoCount = 0;

	// Group players by notoriety
	for (iter = (uintptr_t *)players->begin; iter != (uintptr_t *)players->end; iter++) {
		curNoto = CheckNeedEquipUpdate(self, (CItem *)(uintptr_t)*iter) & 0xFF;

		// Search for existing group with this notoriety
		for (j = 0; j < notoCount; j++) {
			if (notoValues[j] == curNoto)
				break;
		}

		// New notoriety value: create group
		if (j == notoCount) {
			notoValues[notoCount] = curNoto;
			notoCount++;
		}

		CVector_PushBack(&groups[j], *iter);
	}

	// Send one packet per notoriety group
	for (i = 0; i < notoCount; i++) {
		if (mode != 0) {
			PacketManager_MakePacket_EQUIPPED_MOB(pktBuf, (CMobile *)self, (uint8_t)notoValues[i]);
		} else {
			PacketManager_MakePacket_NAKED_MOB(pktBuf, (CMobile *)self, (uint8_t)notoValues[i]);
		}
		SendToClientList(&groups[i], pktBuf);
	}

	for (i = 6; i >= 0; i--)
		CVector_Destructor(&groups[i]);
}

/*
 * 0x004734A4 - CMobile::CalcTotalSkillWeight
 *
 * Sum of baseSkill * skillWeight across all skills, driving the
 * weight-based decay check in NotifySkillGain.
 */
int
CMobile_CalcTotalSkillWeight(CMobile *mob)
{
	int total = 0;
	int numSkills = CSkillManager_GetMaxSkills(&g_SkillManager);
	int i;

	for (i = 0; i < numSkills; i++) {
		CSkillDef *def;
		int baseSkill;

		baseSkill = CMobile_GetBaseSkill(mob, (int8_t)i);
		def = CSkillManager_GetSkillEntry(&g_SkillManager, i);
		total += baseSkill * def->skillWeight;
	}
	return total;
}

/*
 * 0x00473511 - CMobile::DecaySkill
 *
 * Drains the skill-weight budget by delta; if the budget can't cover
 * it, time-weighted-randomly picks another skill to decay and credits
 * its freed weight back. Skill-lock feature excludes Up/Locked skills.
 */
static void
DecaySkill(CMobile *mob, int8_t skillId, int delta)
{
	CVector candidates;
	char typeFlag = 0;
	int numCandidates;
	int numSkills;
	int i;
	int totalTimePenalty;
	int target;
	int chosenSkillId;
	int weightDelta;
	CSkillDef *def;

	delta = -delta;

	if ((int)(mob->skillWeightBudget - (uint32_t)delta) >= 0) {
		mob->skillWeightBudget -= (uint32_t)delta;
		return;
	}

	CVector_Constructor(&candidates, &typeFlag);

	numSkills = CSkillManager_GetMaxSkills(&g_SkillManager);

	for (i = 0; i < numSkills; i++) {
		if (i == (int)skillId)
			continue;
		if (CMobile_GetBaseSkill(mob, (int8_t)i) < delta)
			continue;
		if (feat(FEAT_SKILL_LOCK)) {
			// Up or Locked skills should not decay
			if (VT_IsPlayer((CItem *)mob)) {
				uint8_t lock = ((CPlayer *)mob)->skillLocks[i];
				if (lock == 0 || lock == 2)
					continue;
			}
		}
		CVector_PushBack(&candidates, (uint32_t)i);
	}

	numCandidates = CVector_GetCount(&candidates);

	if (numCandidates == 0) {
		// 0x00473737: no candidates - decay the gained skill itself
		if (CMobile_GetBaseSkill(mob, skillId) >= delta)
			CMobile_AddToSkill(mob, skillId, -delta);
		else
			CMobile_SetSkill(mob, skillId, 0);
		CSkillManager_SendSkillUpdate(mob, skillId);
		CVector_Destructor(&candidates);
		return;
	}

	totalTimePenalty = 0;
	for (i = 0; i < numCandidates; i++) {
		uintptr_t *base = (uintptr_t *)candidates.begin;
		uint32_t timeDiff = g_GameTick - mob->skillTimers[(int)base[i]];
		totalTimePenalty += timeDiff / 60;
	}

	target = 0;
	if (totalTimePenalty > 0)
		target = GetRandomRange64(0, totalTimePenalty);

	for (i = 0; i < numCandidates - 1; i++) {
		uintptr_t *base = (uintptr_t *)candidates.begin;
		uint32_t timeDiff = g_GameTick - mob->skillTimers[(int)base[i]];
		target -= timeDiff / 60;
		if (target <= 0)
			break;
	}

	chosenSkillId = (int)((uintptr_t *)candidates.begin)[i];

	CMobile_GetBaseSkill(mob, (int8_t)chosenSkillId);
	CMobile_AddToSkill(mob, (int8_t)chosenSkillId, -delta);

	def = CSkillManager_GetSkillEntry(&g_SkillManager, chosenSkillId);
	weightDelta = def->skillWeight - delta;
	if (weightDelta > 0)
		mob->skillWeightBudget += (uint32_t)weightDelta;

	CSkillManager_SendSkillUpdate(mob, (int8_t)chosenSkillId);
	CVector_Destructor(&candidates);
}

/*
 * 0x0047379C - CMobile::NotifySkillGain (vtable[0x240])
 *
 * Runs weight-based skill decay after a gain: players above the 7000
 * cap decay excess weight deterministically; everyone else rolls the
 * probabilistic decay curve.
 */
void
CMobile_NotifySkillGain(CMobile *mob, int8_t skillId, int actualGain)
{
	int totalWeight;
	int isPlayer;

	totalWeight = CMobile_CalcTotalSkillWeight(mob);
	isPlayer = VT_IsPlayer((CItem *)mob);

	// 0x004737B8: Player path - deterministic excess decay
	if (isPlayer && totalWeight > 7000) {
		int excess = totalWeight - 7000;
		while (excess > 0) {
			DecaySkill(mob, skillId, -1);
			excess--;
		}
		return;
	}

	// 0x004737F9: Probabilistic decay path (players under 7000, all NPCs)
	if (actualGain > 0) {
		int decayChance = CalcDecayChance(totalWeight);
		int j;

		for (j = 0; j < actualGain; j++) {
			if (GetRandom(1000) < decayChance)
				DecaySkill(mob, skillId, -1);
		}
	}
}

/*
 * 0x00481815 - GetMinMaxZForEntity
 *
 * Computes the Z walk envelope for this mob around loc; floating mobs
 * (moveType 2) get a taller upward range.
 */
void
GetMinMaxZForEntity(CItem *mob, CLocation loc, int moveType, int *outMinZ, int *outMaxZ)
{
	int mt;

	*outMaxZ = (int)(int16_t)loc.z;
	*outMinZ = *outMaxZ;

	mt = ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF;
	CTerrainManager_GetMinMaxZ(outMinZ, outMaxZ, loc, moveType, mt, mob, 1);

	mt = ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF;
	if (mt == 2)
		*outMaxZ += 16;
	else
		*outMaxZ += 2;
}

/*
 * 0x004818FB - CMobile::WalkCheck (vtable[0x208])
 *
 * Returns 1 if the mob can step in direction, 0 if ghost/frozen/blocked.
 * Handles multi owners, edge wrapping, diagonal passability through
 * both cardinals, and terrain z-clearance. *outZ receives the chosen z.
 */
int
CMobile_WalkCheck_VT(CItem *self, int direction, int *outZ)
{
	CLocation curLoc;
	int minZ, maxZ;
	int16_t resultZ;
	int wasHidden;
	int moveType;
	int height;

	// Ghost check
	if (CPlayer_IsGhost((CPlayer *)self))
		return 0;

	// Dead + frozen check: if alive and stamina check returns nonzero,
	// movement is blocked (e.g. paralyzed).
	if (!VT_IsDead(self)) {
		if (((int (*)(void *))VT_FN(self, VT_GET_STAMINA))(self))
			return 0;
	}

	// Compute target location
	CLocation_SetLoc(&curLoc, CEntity_GetLocation(&self->resourceEntity.entity));
	CLocation_MoveDir(&curLoc, direction & 7);

	// Multi owner check: if this mobile owns a multi, validate the
	// new position against multi boundaries.
	if (CItem_IsMultiOwner(self) == 1) {
		wasHidden = 0;
		if (!self->resourceEntity.entity.removedFromWorld) {
			wasHidden = 1;
			((void (*)(CItem *))VT_FN(self, VT_DETACH_SPATIAL))(self);
		}
		moveType = ((int (*)(CItem *))VT_FN(self, VT_GET_MOVEMENT_TYPE))(self) & 0xFF;
		if (CMultiSlave_CanExistAtWrapper(CItem_GetMultiSlave(self), &curLoc, moveType, 0, NULL) <= 0) {
			if (wasHidden == 1)
				((void (*)(CItem *, CLocation *))VT_FN(self, VT_SET_LOCATION))(self, CEntity_GetLocation(&self->resourceEntity.entity));
			return 0;
		}
		if (wasHidden == 1)
			((void (*)(CItem *, CLocation *))VT_FN(self, VT_SET_LOCATION))(self, CEntity_GetLocation(&self->resourceEntity.entity));
		if (outZ != NULL)
			*outZ = -128;
		return 1;
	}

	// Valid coordinate check
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)curLoc.x, (int)(int16_t)curLoc.y)) {
		if (CBlockManager_WrapCoord(&g_SpatialGrid, &curLoc)) {
			if (outZ != NULL)
				*outZ = -128;
			return 1;
		}
		return 0;
	}

	// Diagonal direction check: for diagonal dirs (odd numbers), both
	// adjacent cardinal dirs must be walkable.
	if (direction & 1) {
		if (!((int (*)(CItem *, int, int *))VT_FN(self, VT_WALK_CHECK))(self, direction - 1, NULL)) {
			if (!((int (*)(CItem *, int, int *))VT_FN(self, VT_WALK_CHECK))(self, (direction + 1) & 7, NULL))
				return 0;
		}
	}

	// Terrain walk check
	GetMinMaxZForEntity(self, *CEntity_GetLocation(&self->resourceEntity.entity), direction, &minZ, &maxZ);
	resultZ = (int16_t)CMobile_GetWalkZ(self, curLoc);
	moveType = ((int (*)(CItem *))VT_FN(self, VT_GET_MOVEMENT_TYPE))(self) & 0xFF;
	height = VT_GetHeight(self);
	resultZ = CTerrainManager_CanWalkWrapper(curLoc, minZ, maxZ, height, moveType, self, 0);

	if (outZ != NULL)
		*outZ = (int)(int16_t)resultZ;
	return ((int16_t)resultZ != (int16_t)-128) ? 1 : 0;
}

/*
 * 0x004838FF - CMobile::DetachSpatial (vtable[0x10])
 *
 * Decrements the normal-NPC counter when removing a top-level mob,
 * unregisters from the NPC spatial map, then chains to the CItem
 * detach.
 */
void
CMobile_DetachSpatial_VT(CItem *self)
{
	if (self->parent == NULL)
		CMobile_DecrNormalNPCCount((CMobile *)self);

	CNPCManager_RemoveFromSpatialMap(self);

	CItem_DetachFromSpatial(self);
}

/*
 * 0x004852D6 - Entity_BroadcastPacket
 *
 * Null-guarded SendToClient with unlimited range. The serial arg is
 * unused.
 */
void
Entity_BroadcastPacket(CItem *entity, uint32_t serial, uint8_t *buf)
{
	if (entity == NULL)
		return;
	SendToClient(entity, buf, -1);
	USED(serial);
}

/*
 * 0x004852F3 - Entity_SendSystemMessage
 *
 * Sends a standard system SPEECH packet (hue 0x3B2, font 3) on behalf
 * of entity; no-op for non-player, non-NULL entities.
 */
void
Entity_SendSystemMessage(CItem *entity, uint32_t serial, char *message)
{
	uint8_t obuf[0x42C];

	if (entity != NULL) {
		if (!VT_IsPlayer(entity))
			return;
	}

	PacketManager_MakePacket_TEXT(obuf, entity, entity, 0, message, 0x03B2, 3);
	Entity_BroadcastPacket(entity, serial, obuf);
}

/*
 * 0x00486641 - CMobile::GetSurfaceFlags (vtable[0x40])
 *
 * Mobiles are TF_IMPASSABLE (0x40), except when g_IgnoreMobiles is
 * set or the traveller moves as a ghost/projectile (moveType 6-7).
 */
int
CMobile_GetSurfaceFlags_VT(CMobile *mob, int moveType)
{
	USED(mob);
	if (g_IgnoreMobiles != 0)
		return 0;
	if (moveType >= 6 && moveType <= 7)
		return 0;
	return 0x40;
}

/*
 * 0x0048F35B - CMobile::IsMurderer
 *
 * True when the murderCount ObjVar is at least 5 - drives red noto.
 */
int
CMobile_IsMurderer(CMobile *this)
{
	int murderCount;

	murderCount = 0;
	CResourceEntity_GetTagInt((CItem *)this, "murderCount", &murderCount);
	return murderCount >= 5;
}

/*
 * 0x0048F389 - IsGuildEnemy
 *
 * True if viewer's guildType is at war with subject's guildType
 * (g_GuildWarMatrix) or subject's "opposingGuilds" list contains
 * viewer's guildstoneId.
 */
int
IsGuildEnemy(CMobile *subject, uint32_t viewerGuildstoneId, int viewerGuildType)
{
	int subjectGuildType = 0;
	CList *list;

	CResourceEntity_GetTagInt((CItem *)subject, "guildType", &subjectGuildType);

	if (subjectGuildType < 0 || subjectGuildType >= 5)
		subjectGuildType = 0;

	if (g_GuildWarMatrix[viewerGuildType] & (1 << subjectGuildType))
		return 1;

	if (viewerGuildstoneId == 0)
		return 0;

	list = CResourceEntity_GetTagEntity((CItem *)subject, "opposingGuilds");
	if (list != NULL) {
		if (CList_Find(list, 4, viewerGuildstoneId))
			return 1;
	}
	return 0;
}

/*
 * 0x0048F429 - CItem::FillAggroInfo (vtable[0x19C])
 *
 * Populates AggroInfo from the entity's controller* tags, resolving
 * the controller serial to a CPlayer entity when possible.
 */
void
CItem_FillAggroInfo_VT(CItem *self, AggroInfo *info)
{
	CString *name;

	AggroInfo *ai = info;
	ai->entity = NULL;
	ai->guildstoneId = 0;
	ai->serial = 0;
	ai->guildType = 0;

	CResourceEntity_GetTagObj(self, "controller", &ai->serial);

	CResourceEntity_GetTagObj(self, "controllerGuild", &ai->guildstoneId);

	CResourceEntity_GetTagInt(self, "controllerGuildType", &ai->guildType);

	name = CResourceEntity_GetTagString(self, "controllerName");
	if (name != NULL)
		CString_Assign(&ai->name, name);

	if (ai->serial != 0) {
		ai->entity = (CItem *)CPlayerList_FindBySerial(ai->serial);
	}
}

/*
 * 0x0048F4E1 - CMobile::ComputeNotoriety
 *
 * Classifies subject from viewer's perspective:
 *   1 innocent, 2 ally, 3 attackable, 4 criminal, 5 enemy, 6 murderer.
 */
int
CMobile_ComputeNotoriety(CMobile *subject, CMobile *viewer)
{
	uint32_t viewerGuildstoneId = 0;
	int viewerGuildType = 0;
	CList *list;

	if (CMobile_IsMurderer(subject))
		return 6;

	if (CMobile_IsCriminal(subject))
		return 4;

	CResourceEntity_GetTagObj((CItem *)viewer, "guildstoneId", &viewerGuildstoneId);

	CResourceEntity_GetTagInt((CItem *)viewer, "guildType", &viewerGuildType);

	if (IsGuildEnemy(subject, viewerGuildstoneId, viewerGuildType))
		return 5;

	list = CResourceEntity_GetTagEntity((CItem *)subject, "aggressionVictimList");
	if (list != NULL) {
		if (CList_Find(list, 4, viewer->container.item.serial))
			return 3;
	}

	list = CResourceEntity_GetTagEntity((CItem *)subject, "crimeVictimList");
	if (list != NULL) {
		if (CList_Find(list, 4, viewer->container.item.serial))
			return 3;
	}

	if (VT_IsPlayer((CItem *)subject)) {
		uint32_t subjectGuildstoneId = 0;
		CResourceEntity_GetTagObj((CItem *)subject, "guildstoneId", &subjectGuildstoneId);
		if (subjectGuildstoneId != 0 && subjectGuildstoneId == viewerGuildstoneId)
			return 2;

		int subjectGuildType = 0;
		CResourceEntity_GetTagInt((CItem *)subject, "guildType", &subjectGuildType);
		if (subjectGuildType > 1 && subjectGuildType == viewerGuildType)
			return 2;
	} else {
		uint32_t controllerGuild = 0;
		CResourceEntity_GetTagObj((CItem *)subject, "controllerGuild", &controllerGuild);
		if (controllerGuild != 0 && controllerGuild == viewerGuildstoneId)
			return 2;

		int controllerGuildType = 0;
		CResourceEntity_GetTagInt((CItem *)subject, "controllerGuildType", &controllerGuildType);
		if (controllerGuildType > 1 && controllerGuildType == viewerGuildType)
			return 2;
	}

	list = CResourceEntity_GetTagEntity((CItem *)viewer, "lawfullyDamaged");
	if (list != NULL) {
		if (CList_Find(list, 4, subject->container.item.serial))
			return 3;
	}

	return 1;
}

/*
 * 0x0048FC7B - CMobile::NotifyDamage (vtable[0x198])
 *
 * Records the attacker in the victim's aggressor list, then either
 * marks the attacker "lawfullyDamaged" or flags the attacker (and
 * their controller) as criminally punishable.
 */
void
CMobile_NotifyDamage(CMobile *target, CMobile *attacker)
{
	CItem *self = &target->container.item;
	CItem *atk = &attacker->container.item;
	AggroInfo info;
	CLocation *loc;
	CList localList;
	CString localStr;

	if (atk == NULL)
		return;

	AggroInfo_Constructor(&info);

	((void (*)(CItem *, AggroInfo *))VT_FN(atk, VT_FILL_AGGRO_INFO))(atk, &info);

	if (self->serial == info.serial) {
		AggroInfo_Destructor(&info);
		return;
	}

	((void (*)(CItem *, CItem *, CItem *, uint32_t))VT_FN(self, VT_ADD_TO_AGGRESSOR_LIST))(self, atk, info.entity, info.serial);

	if (((int (*)(CItem *, CItem *, AggroInfo *))VT_FN(self, VT_CAN_BE_FREELY_AGGRESSED))(self, atk, &info)) {
		CResourceEntity_AddToTagList(atk, "lawfullyDamaged", 4, self->serial);
		ScheduleEvent(0x1E0, atk->serial, 0x11, 2, 0);
		AggroInfo_Destructor(&info);
		return;
	}

	loc = &target->container.item.resourceEntity.entity.location;
	CMobile_SetCriminalPunishable((CMobile *)atk, loc, 0x1E0);

	if (info.serial == 0) {
		AggroInfo_Destructor(&info);
		return;
	}

	if (info.serial != atk->serial) {
		if (info.entity != NULL) {
			CMobile_SetCriminalPunishable((CMobile *)info.entity, loc, 0x1E0);
		} else {
			CList_Constructor(&localList);
			CList_Append(&localList, 3, (uintptr_t)loc);
			CString_Constructor(&localStr, "refreshCriminal");
			SendMultiMessage(info.serial, self->serial, &localStr, (intptr_t)&localList);
			CString_Destructor(&localStr);
			CList_Destructor(&localList);
		}
	}

	AggroInfo_Destructor(&info);
}

/*
 * 0x0048FE31 - CMobile::IsCriminal
 *
 * True if the "criminal" ObjVar tag is present.
 */
int
CMobile_IsCriminal(CMobile *mob)
{
	return CResourceEntity_HasTag((CItem *)mob, "criminal", 0);
}

/*
 * 0x0048FE4B - CMobile::SetCriminalPunishable
 *
 * Marks the mob criminal and, if either the mob or the crime
 * location is in a justice region, attaches the "punishable" script.
 */
void
CMobile_SetCriminalPunishable(CMobile *mob, CLocation *crimeLoc, int duration)
{
	CLocation *mobLoc;

	CMobile_SetCriminal(mob, duration);

	mobLoc = &mob->container.item.resourceEntity.entity.location;

	if (RegionManager_inJusticeRegion(mobLoc->x, mobLoc->y, mobLoc->z) || RegionManager_inJusticeRegion(crimeLoc->x, crimeLoc->y, crimeLoc->z)) {
		Entity_AttachScript((CItem *)mob, "punishable", 1);
		Entity_ExecuteEvent(&mob->container.item.resourceEntity.entity, 0x16, 0, "punishableReset", "v");
	}
}

/*
 * 0x0048FEB8 - CMobile::SetCriminal
 *
 * Re-arms the criminal expiry timer. On the first transition to
 * criminal, sets the ObjVar and broadcasts the appearance change.
 */
void
CMobile_SetCriminal(CMobile *mob, int duration)
{
	uint32_t serial;

	serial = mob->container.item.serial;

	ScheduleEvent(duration, serial, TIMER_EVENT_CRIMINAL, 0, 0);

	if (CMobile_IsCriminal(mob))
		return;

	CEntity_SetObjVar((CItem *)mob, "criminal", 0, (uint32_t)1);

	{
		CVector vec;
		CLocation *loc;

		loc = ((CLocation * (*)(CItem *)) VT_FN((CItem *)mob, VT_GET_LOCATION))((CItem *)mob);

		CVector_Constructor(&vec, "\x01");
		GetNearbyPlayers(&vec, loc, 0x12);

		((void (*)(CItem *, CVector *, int))VT_FN((CItem *)mob, VT_NOTIFY_NEARBY))((CItem *)mob, &vec, 0);

		CVector_Destructor(&vec);
	}
}

/*
 * 0x004C3930 - VendorStock_GetResourceRatio
 *
 * Trampoline for CItem::GetMinResourceRatio.
 */
static int
VendorStock_GetResourceRatio(CItem *item)
{
	return CItem_GetMinResourceRatio(item);
}

/*
 * 0x004C393D - VendorStock_RemoveResources
 *
 * Scales the item's resource nodes down by (ratio - resourceParam),
 * hiding and locking the item while editing if it was visible.
 */
static void
VendorStock_RemoveResources(CItem *item, int resourceParam, CLocation *vendorLoc)
{
	int ratio;
	CResourceNode *node;
	int wasHidden;
	int amount;

	USED(vendorLoc);

	ratio = VendorStock_GetResourceRatio(item) - resourceParam;

	node = g_ResEntitySlots[CEntity_GetBodyType(item) & 0xFFFF].nodeHead;

	wasHidden = 0;
	if (!item->resourceEntity.entity.removedFromWorld) {
		CItem_SetLockdown(item, 1);
		((void (*)(void *))VT_FN(item, VT_HIDE))(item);
		wasHidden = 1;
	}

	while (node != NULL) {
		amount = node->value1 * ratio;
		CResourceEntity_RemoveResource(item, amount, node->id);
		node = node->next;
	}

	if (wasHidden) {
		((void (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item);
		CItem_SetLockdown(item, 0);
	}
}

/*
 * 0x004C39F9 - CMobile::VendorStock_Buy
 *
 * Deposits generated gold in the vendor's bank for the removed
 * resources and accumulates the cost into *goldPtr.
 */
static void
CMobile_VendorStock_Buy(CMobile *vendor, CItem *item, int buyCount, int resourceParam, int *goldPtr)
{
	int cost;
	CItem *goldItem;

	cost = ((int (*)(void *, int, int))VT_FN(item, VT_GET_VALUE))(item, 1, 1);
	cost *= buyCount;

	if (!ResBankMagicCheck(&vendor->container.item.resourceEntity.entity.location, g_ResTypeId_Gold))
		return;

	goldItem = CWorld_CreateItem(g_World, 0xEED);
	if (goldItem == NULL)
		return;

	CResourceEntity_AddNodeScaled(goldItem, (uint16_t)g_ResTypeId_Gold, 3, cost, 0, cost, 0, 1, 1);

	CMobile_PutMoneyInBank(vendor, goldItem, cost);

	VendorStock_RemoveResources(item, resourceParam, &vendor->container.item.resourceEntity.entity.location);

	*goldPtr += cost;
}

/*
 * 0x004C3AA6 - CMobile::VendorStock_Restock
 *
 * Re-setups the item up to count times around the vendor, billing
 * each unit against gold and stopping once the budget would overflow
 * or the res-bank cap is hit.
 */
static void
CMobile_VendorStock_Restock(CMobile *vendor, CItem *item, int count, int *feePtr, int gold)
{
	ResEntitySlot *resSlot;
	int itemPrice;
	int i;
	int wasHidden;

	resSlot = &g_ResEntitySlots[item->resourceEntity.entity.bodyType & 0xFFFF]; // result unused
	USED(resSlot);

	itemPrice = ((int (*)(void *, int, int))VT_FN(item, VT_GET_VALUE))(item, 1, 1);

	for (i = 0; i < count; i++) {
		if ((item->resourceEntity.entity.bodyType & 0xFFFF) == 0)
			continue;

		if (*feePtr + itemPrice > gold)
			break;

		if (!ResBankLimitCheck(item, &vendor->container.item.resourceEntity.entity.location))
			break;

		*feePtr += itemPrice;

		wasHidden = 0;
		if (!item->resourceEntity.entity.removedFromWorld) {
			CItem_SetLockdown(item, 1);
			((void (*)(void *))VT_FN(item, VT_HIDE))(item);
			wasHidden = 1;
		}

		CItem_Setup(item, 0, &vendor->container.item.resourceEntity.entity.location, 0, 1);

		if (wasHidden) {
			((void (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item);
			CItem_SetLockdown(item, 0);
		}
	}
}

/*
 * 0x004C3BB6 - VendorStock_GetStockDelta
 *
 * max(0, item's stockTarget - subtractAmount).
 */
static int
VendorStock_GetStockDelta(CItem *item, int subtractAmount)
{
	int stockTarget;

	stockTarget = 0;
	if (CResourceEntity_HasTag(item, "stockTarget", 0)) {
		CResourceEntity_GetTagInt(item, "stockTarget", &stockTarget);
	}

	if (stockTarget - subtractAmount > 0)
		return stockTarget - subtractAmount;
	return 0;
}

/*
 * 0x004C3BFF - VendorStock_SetStockTarget
 *
 * Writes value to the item's "stockTarget" ObjVar.
 */
static void
VendorStock_SetStockTarget(CItem *item, int value)
{
	CString name;

	CString_Constructor(&name, "stockTarget");
	ObjVar_SetStr(item, &name, 0, (uint32_t)value);
}

/*
 * 0x004C3C30 - CMobile::ComputeVendorFee
 *
 * Reconciles the vendor's stock container in two passes: restocks
 * non-resource items, then buys/sells resource items to meet their
 * stockTarget. Returns the total fee to charge against the vendor.
 */
int
CMobile_ComputeVendorFee(CMobile *this, int gold)
{
	int totalFee;
	int ratio;
	int stockDelta;
	CItem *child;
	CVector vec1, vec2;
	uintptr_t *iter;

	totalFee = 0;

	if (this->equipment[26] == NULL)
		return 0;
	if (!VT_IsMobile2(this->equipment[26]))
		return 0;

	// Get contents head
	child = ((CContainer *)this->equipment[26])->contents;

	CVector_Constructor(&vec1, "");
	while (child != NULL) {
		CVector_PushBack(&vec1, (uintptr_t)child);
		child = child->spatialNext;
	}

	// Pass 1: non-resource items - restock
	for (iter = (uintptr_t *)vec1.begin; iter != (uintptr_t *)vec1.end; iter++) {
		if (VendorStock_GetResourceRatio((CItem *)*iter) == 0) {
			CMobile_VendorStock_Restock(this, (CItem *)*iter, 1, &totalFee, gold);
		}
	}

	CVector_Constructor(&vec2, "");
	child = ((CContainer *)this->equipment[26])->contents;
	while (child != NULL) {
		CVector_PushBack(&vec2, (uintptr_t)child);
		child = child->spatialNext;
	}

	// Pass 2: resource items - check stock delta, buy or sell
	for (iter = (uintptr_t *)vec2.begin; iter != (uintptr_t *)vec2.end; iter++) {
		child = (CItem *)*iter;

		ratio = VendorStock_GetResourceRatio(child);

		stockDelta = VendorStock_GetStockDelta(child, ratio - 10);
		if (stockDelta < 1)
			stockDelta = 1;

		if (stockDelta < ratio) {
			CMobile_VendorStock_Buy(this, child, ratio - stockDelta, stockDelta, &gold);
		} else if (stockDelta > ratio) {
			CMobile_VendorStock_Restock(this, child, stockDelta - ratio, &totalFee, gold);
		}

		VendorStock_SetStockTarget(child, VendorStock_GetResourceRatio(child));
	}

	// Add sold items fee
	gold += VendorStock_ComputeSoldItemsFee(this);

	CVector_Destructor(&vec2);
	CVector_Destructor(&vec1);
	return totalFee;
}

/*
 * 0x004C3E65 - VendorStock_ComputeSoldItemsFee
 *
 * Sells off roughly 3 out of every 4 items in the vendor's offered
 * container, depositing gold in the vendor's bank and deleting each
 * source item.
 *
 * FIXED: binary dereferences child when NULL after FindBySerial
 * (crash at 0x004C3F4C). Added NULL guard before parent check.
 */
int
VendorStock_ComputeSoldItemsFee(CMobile *vendor)
{
	CContainer *offeredCont;
	CItem *child;
	CItem *goldItem;
	int totalFee;
	int itemValue;
	CVector vec;
	uintptr_t *iter;

	totalFee = 0;

	CVector_Constructor(&vec, "");

	if (vendor->equipment[27] == NULL)
		goto done;

	offeredCont = (CContainer *)vendor->equipment[27];

	// Collect item serials with 3/4 probability
	child = offeredCont->contents;
	while (child != NULL) {
		if (GetRandomRange(0, 3) != 0)
			CVector_PushBack(&vec, child->serial);
		child = child->spatialNext;
	}

	// Iterate CVector of serials
	for (iter = (uintptr_t *)vec.begin; iter != (uintptr_t *)vec.end; iter++) {
		child = CWorld_FindBySerial(g_World, (uint32_t)*iter);

		// FIXED: binary dereferences child at 0x004C3F4C when NULL.
		if (child == NULL)
			continue;

		// Check item->parent is vendor or offered container
		if (child->parent != NULL && child->parent == (CItem *)vendor)
			goto parent_ok;
		if (child->parent != vendor->equipment[27])
			goto next_item;

parent_ok:
		// ResBankMagicCheck(vendor+0x0A, g_ResTypeId_Gold)
		if (!ResBankMagicCheck(&vendor->container.item.resourceEntity.entity.location, g_ResTypeId_Gold))
			goto check_delete;

		// vtable[0x24](child, 0, 1) - GetValue
		itemValue = ((int (*)(void *, int, int))VT_FN(child, VT_GET_VALUE))(child, 0, 1);

		// Create gold item (bodyType 0xEED)
		goldItem = CWorld_CreateItem(g_World, 0xEED);
		if (goldItem == NULL)
			continue;

		CResourceEntity_AddNodeScaled(goldItem, (uint16_t)g_ResTypeId_Gold, 3, itemValue, 0, itemValue, 0, 1, 1);

		CMobile_PutMoneyInBank(vendor, goldItem, itemValue);

		totalFee += itemValue;

check_delete:
		if (child != NULL)
			((void (*)(void *))VT_FN(child, VT_DELETE))(child);
		continue;

next_item:;
	}

done:
	CVector_Destructor(&vec);
	return totalFee;
}

/*
 * 0x004D764E - Mobile_GetMeleeRange
 *
 * Equipped weapon's melee range, or 1 when unarmed.
 */
static __attribute__((unused)) int
Mobile_GetMeleeRange(CMobile *mob)
{
	CItem *weapon;

	weapon = CMobile_GetWeapon(mob);
	if (weapon != NULL)
		return CItem_GetMeleeRange(weapon) & 0xFF;
	return 1;
}

/*
 * 0x004E0EA0 - CMobile::WeightRelated (vtable[0x154])
 *
 * Takes a snapshot of equipment, locks + hides + recurses, refreshes
 * the container contents, and re-equips or deletes each item.
 */
void
CMobile_WeightRelated_VT(CMobile *self)
{
	CItem *saved[0x1E];
	int i;

	// Save equipment snapshot
	for (i = 0; i < 0x1E; i++)
		saved[i] = self->equipment[i];

	// Lock down and hide each equipment item
	for (i = 0; i < 0x1E; i++) {
		if (saved[i] == NULL)
			continue;
		CItem_SetLockdown(saved[i], 1);
		if (!saved[i]->resourceEntity.entity.removedFromWorld)
			((void (*)(CItem *))VT_FN(saved[i], VT_HIDE))(saved[i]);
		((void (*)(CItem *))VT_FN(saved[i], VT_WEIGHT_RELATED))(saved[i]);
	}

	// Refresh container contents (binary: fcn.004e0fd5)
	CContainer_RefreshContents(self);

	// Re-equip and unlock
	for (i = 0; i < 0x1E; i++) {
		if (saved[i] == NULL)
			continue;
		if (!((int (*)(CItem *, CItem *, uint8_t))VT_FN(saved[i], VT_EQUIP_ON_MOBILE))(saved[i], (CItem *)self, (uint8_t)i)) {
			CItem_SetLockdown(saved[i], 0);
			if (saved[i] != NULL)
				((void (*)(CItem *))VT_FN(saved[i], VT_DELETE))(saved[i]);
		} else {
			CItem_SetLockdown(saved[i], 0);
		}
	}
}

/*
 * 0x004E0FD5 - CContainer_RefreshContents
 *
 * Two-pass recomputation of a container's stored weight: lock+hide+
 * recurse across the non-multi-owner contents, zero storedWeight, then
 * show and unlock.
 *
 * FIXED: binary passes an uninitialized stack byte as the CVector type
 * flag. Initialized to 0 to avoid undefined behavior.
 */
void
CContainer_RefreshContents(CMobile *self)
{
	CVector vec;
	uintptr_t *iter;
	CItem *cur;
	char typeFlag = 0;

	CVector_Constructor(&vec, &typeFlag);

	// Collect non-multi-owner items from container contents
	cur = self->container.contents;
	while (cur != NULL) {
		if (!CItem_HasMulti(cur) || CItem_IsMultiOwner(cur))
			CVector_PushBack(&vec, (uintptr_t)cur);
		cur = cur->spatialNext;
	}

	// Pass 1: lock, hide, recursive WeightRelated
	for (iter = (uintptr_t *)vec.begin; iter != (uintptr_t *)vec.end; iter++) {
		CItem_SetLockdown((CItem *)*iter, 1);
		if (!((CItem *)*iter)->resourceEntity.entity.removedFromWorld)
			((void (*)(CItem *))VT_FN((CItem *)*iter, VT_HIDE))((CItem *)*iter);
		((void (*)(CItem *))VT_FN((CItem *)*iter, VT_WEIGHT_RELATED))((CItem *)*iter);
	}

	// Clear stored weight
	self->container.storedWeight = 0;

	// Pass 2: show and unlock
	for (iter = (uintptr_t *)vec.begin; iter != (uintptr_t *)vec.end; iter++) {
		((void (*)(CItem *))VT_FN((CItem *)*iter, VT_RETURN_TO_TRACKED))((CItem *)*iter);
		CItem_SetLockdown((CItem *)*iter, 0);
	}

	CVector_Destructor(&vec);
}

/*
 * Custom - CMobile_GetGold
 *
 * Sum gold coins (bodyType 0xEED) in backpack, recursing into
 * sub-containers and using vtable GetItemAmount for stack sizes.
 */
uint32_t
CMobile_GetGold(CMobile *this)
{
	CItem *backpack;

	backpack = this->equipment[21];
	if (backpack == NULL)
		return 0;

	return (uint32_t)CContainer_GetTotalQuantity((CContainer *)backpack, 0xEED);
}

/*
 * Helper - CMobile_IsVendor
 *
 * Dispatches vtable[0xE8] (VT_IS_VENDOR): true for CShopkeeper only.
 */
int
CMobile_IsVendor(CMobile *this)
{
	return VT_IsVendor((CItem *)this);
}
