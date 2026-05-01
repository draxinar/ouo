#ifndef COMBAT_H_
#define COMBAT_H_

#include <stdint.h>

#include "cstring.h"

__extension__ typedef struct CDataBuffer CDataBuffer;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;

/*
 * Terrain lookup cache (0x808 bytes): count, 0x200 uint32_t entries,
 * trailing dword.
 */
__extension__ typedef struct CTerrainLookup CTerrainLookup;
struct CTerrainLookup {
	uint32_t count;                 // 0x000
	uint32_t entries[0x200];        // 0x004
	uint32_t field_804;             // 0x804
};

/*
 * CWeaponItem layout (extends CItem at offset 0x50). The binary subclasses
 * CItem for equippable items; since CContainer reuses the same region for
 * container fields, only non-container items store weapon data here.
 *
 *   0x50: weaponDefId - index into g_WeaponManager
 *   0x51-0x53: damage dice (CDiceRoll) - per-item override
 *   0x54: quality - item quality modifier
 *   0x55: durability - decremented by combat (0x004DF71F)
 *   0x56: baseAC - base armor class / max durability denominator
 */

/*
 * Aggression context (0x20 bytes). Filled on the stack by vtable[0x19C]
 * FillAggroInfo and passed to vtable[0x188] CanBeFreelyAggressed.
 */
__extension__ typedef struct AggroInfo {
	CItem *entity;          // 0x00 - resolved entity (CPlayer* at runtime)
	uint32_t serial;        // 0x04 - player or controller serial
	uint32_t guildstoneId;  // 0x08 - guildstoneId or controllerGuild
	int guildType;          // 0x0C - guildType or controllerGuildType
#if __SIZEOF_POINTER__ == 8
	int _pad64;             // 64-bit alignment pad
#endif
	CString name;           // 0x10
} AggroInfo;

/*
 * Inner node: one target a combatant is currently tracking.
 */
__extension__ typedef struct CCombatSubNode CCombatSubNode;
struct CCombatSubNode {
	CCombatSubNode *next;   // 0x00
	CCombatSubNode *prev;   // 0x04
	CMobile *mob;           // 0x08
};

/*
 * Outer node: one combatant and the sub-list of targets it is engaged with.
 */
__extension__ typedef struct CCombatEventNode CCombatEventNode;
struct CCombatEventNode {
	CCombatSubNode *subHead;        // 0x00
	CCombatEventNode *next;         // 0x04
	CCombatEventNode *prev;         // 0x08
	CMobile *mob;                   // 0x0C
	char name[84];                  // 0x10
};

/*
 * Head of the combat event scheduler (g_CombatEventList at 0x006990A8).
 * A doubly-linked list of CCombatEventNode combatants, each owning a
 * doubly-linked list of CCombatSubNode targets. CMobile_Destructor
 * removes all references to the dying mob.
 */
__extension__ typedef struct CCombatEventList CCombatEventList;
struct CCombatEventList {
	CCombatEventNode *head;         // 0x00
};

extern CCombatEventList g_CombatEventList; // 0x006990A8

int IsCoordInDiamond(int x, int y, int x1, int y1, int x2, int y2); // 0x004430D9
int IsCoordInRect(int x, int y, int x1, int y1, int x2, int y2); // 0x00443138
int Mobile_IsFacingEntity(CMobile *mob1, CMobile *mob2); // 0x00443166
int RadianToUODir(float angle); // 0x0044320E
int CMobile_GetWeaponSkill(CMobile *mob, CItem *weapon); // 0x004433AE
int CMobile_CalcDamage(CMobile *attacker, CItem *weapon); // 0x00443449
void Combat_PlayWeaponHitSfx(CItem *unused, CItem *target, CItem *weapon); // 0x00443674
void Combat_PlayMeleeMissSfx(CItem *attacker, CItem *unused, CItem *weapon); // 0x004436A8
void Combat_PlaySwingAnimation(CMobile *mob, CItem *weapon, CMobile *target); // 0x00443EBC
int Combat_CalcArmorClass(CMobile *mob); // 0x004444EB
int Combat_DamageResolve(CMobile *attacker, CMobile *target, int damage, CItem *weapon, int flag); // 0x004453A2
int CMobile_GetWeaponSpeed(CMobile *mob); // 0x00445901
int CMobile_GetSwingState(CMobile *this); // 0x0044596D
int CMobile_AdvanceSwingState(CMobile *mob); // 0x00445981
void CMobile_SetSwingState(CMobile *mob, int state); // 0x00445AB0
void CMobile_SwingResolve(CMobile *mob); // 0x00445AF5
void CombatInitiate(CMobile *attacker, CMobile *target, int flag); // 0x004466D9
int CCombatEventList_AddNode(CCombatEventList *list, CMobile *mob, const char *name); // 0x004603B9
int CCombatEventList_RemoveNode(CCombatEventList *list, CMobile *mob); // 0x004604AB
int CCombatEventList_ToggleSub(CCombatEventList *list, CMobile *ownerMob, CMobile *targetMob); // 0x00460559
CCombatEventNode *CCombatEventList_FindByMob(CCombatEventList *list, CMobile *mob); // 0x004605B8
int CCombatEventList_RemoveSubByOwner(CCombatEventList *list, CMobile *ownerMob, CMobile *targetMob); // 0x004605FA
CMobile *CCombatEventList_FindAttackerOf(CCombatEventList *list, CMobile *mob); // 0x00460648
void CCombatEventList_RemoveMob(CCombatEventList *list, CMobile *mob); // 0x004606A3
int CCombatEventNode_RemoveSub(CCombatEventNode *node, CMobile *mob); // 0x004608A6
void CDataBuffer_Constructor(CDataBuffer *b); // 0x004609A0
void CDataBuffer_Destructor(CDataBuffer *b); // 0x004609D4
void CDataBuffer_Append(CDataBuffer *b, const void *src, int srcLen); // 0x004609F5
int ProcessCrimeWitness(CLocation *crimeLoc, CMobile *criminal, CMobile *victim, const char *callbackName, int delay, int priority, int crimeType); // 0x00460C62
int CombatManager_InitiateCombat(CMobile *attacker, CMobile *target); // 0x0046121C
int CombatManager_CallGuards(CItem *mob, CLocation *loc, CItem *target, int range); // 0x004613E6
int CMobile_CanBeFreelyAggressedBy(CMobile *victim, CMobile *attacker); // 0x0048F6D6
int CMobile_CanBeFreelyAggressed_Impl(CMobile *victim, CMobile *attacker, AggroInfo *info); // 0x0048F75D

#endif /* COMBAT_H_ */
