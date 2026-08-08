/*
 * Combat resolution - swing dispatch, damage, and guard reactions.
 *
 * Runs CMobile swing timing, rolls hit / miss, applies armor absorption
 * and durability loss, fires combat messages to the clients in range,
 * and drives the witness / guard-spawn logic triggered when a crime is
 * observed nearby.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "combat.h"
#include "container.h"
#include "dat.h"
#include "dynamic.h"
#include "egg.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "skill.h"
#include "template.h"
#include "timer.h"
#include "utils.h"
#include "vtable.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "world.h"

static int Combat_ConsumeAmmo(CMobile *mob, CItem *weapon); // 0x0044335C
static int Combat_CheckStrength(CMobile *mob); // 0x004433A7
static int Combat_GetSwingAnimType(CItem *weapon); // 0x004434C0
static void Combat_FireProjectile(CMobile *attacker, CMobile *target); // 0x00443626
static void Combat_PlayRangedMissEffects(CItem *attacker, CItem *target, CItem *weapon); // 0x004436DC
static void Combat_HitEffects(CMobile *attacker, CMobile *target, CItem *weapon); // 0x00443A19
static CItem *CMobile_GetBestArmorInGroup(CMobile *mob, int groupIdx); // 0x00444434
static void Combat_AppendDamageType(CString *buf, int count, CString *typeName); // 0x004448AF
static void Combat_BuildPhysicalDamageMsg(CString *buf, CString *weaponName, int dmgDelta, uint32_t dmgTypes, int isThirdPerson); // 0x004448DC
static void Combat_BuildElementalDamageBody(CString *buf, int dmgDelta, uint32_t elemType, int hasName); // 0x00444A80
static void Combat_BuildElementalDamageMsg(CString *buf, CString *weaponName, int dmgDelta, uint32_t elemType, int perspective); // 0x00444B26
static void Combat_DestroyEquippedItem(CMobile *player, CItem *item, int isOpponent); // 0x00444BBF
static int Combat_ApplyDurabilityLoss(CItem *attacker, CItem *defender, CItem **weapon_ptr, CItem **armor_ptr, CItem **shield_ptr, int *out_totalAC, int *out_armorAbsorb,
        int *out_shieldAbsorb, uint32_t damageType, int incomingDamage); // 0x00444C75
static void Combat_CalcArmorAbsorption(CItem *attacker, CItem *defender, CItem **weapon_ptr, int *damage_ptr, uint32_t damageType); // 0x004452D8
static CCombatEventNode *CCombatEventNode_Constructor(CCombatEventNode *node, CMobile *mob, const char *name); // 0x0046072D
static void CCombatEventNode_Destructor(CCombatEventNode *node); // 0x00460776
static CMobile *CCombatEventList_FindSubByMob(CCombatEventNode *node, CMobile *mob); // 0x00460781
static int CCombatEventNode_AddSub(CCombatEventNode *node, CMobile *mob); // 0x004607EB
static CCombatSubNode *CCombatSubNode_Constructor(CCombatSubNode *node); // 0x00460934
static void *CCombatEventNode_ScalarDelete(CCombatEventNode *node, int flags); // 0x00460970
static int ScoreWitness(CMobile *witness, CLocation *crimeLoc, CMobile *criminal, CMobile *victim, int range); // 0x00460BD4
static int CombatManager_SpawnGuards(CItem *mob, int count, int range); // 0x00461568
static int CombatManager_RecruitGuard(CMobile *guard, CItem *mob, int range); // 0x004616C6
static int CombatManager_SpawnGuardsAtLoc(CLocation *loc, int count, int range, CItem *mob, int flag); // 0x00461B7B
static void CombatManager_InitGuard(CMobile *guard); // 0x00461CA9
static int IsInDungeonRange(CLocation *loc); // 0x00462370
static void StaticInit_CombatEventList(void); // 0x00467911

/*
 * One row of the weapon-type to skill-id table at 0x00614F98. Entry 0
 * carries the unarmed fallback (typeFlags = 0x00010000 -> Wrestling).
 */
struct WeaponSkillEntry {
	uint32_t mask;
	uint32_t skillId;
};

// 0x00614F98 - weapon-type mask to skill-id (entry 0 is unarmed fallback)
static struct WeaponSkillEntry weaponSkillTable[5] = {
	{ 0x10000, 43 }, // unarmed -> Wrestling
	{ 0x01, 40 },    // Slashing -> Swordsmanship
	{ 0x04, 41 },    // Bashing -> Mace Fighting
	{ 0x02, 42 },    // Piercing -> Fencing
	{ 0x08, 31 },    // Ranged -> Archery
};

/*
 * 0x00443093 - Combat_ConsumeTypeZero
 *
 * Consumes one item of body type 0 from mob's equipment, but only when
 * it carries any. The body type comes from a local the function zeroes
 * and never changes, so the query is always for type 0. Returns 1 when
 * the consume ran, 0 when nothing matched.
 */
static __attribute__((unused)) int
Combat_ConsumeTypeZero(CMobile *mob)
{
	uint16_t bodyType;
	uint16_t quantity;

	bodyType = 0;
	quantity = (uint16_t)CMobile_GetTotalQuantityOfType(mob, bodyType);
	if (quantity == 0)
		return 0;

	CMobile_FindItemInEquipment(mob, bodyType, 1);
	return 1;
}

/*
 * 0x004430D9 - IsCoordInDiamond
 *
 * Returns 1 when (x, y) falls outside the Manhattan diamond inscribed in
 * the given bounding rectangle.
 */
int
IsCoordInDiamond(int x, int y, int x1, int y1, int x2, int y2)
{
	int halfRange, midX, midY, dx, dy;

	halfRange = x2 - x1;
	midX = (x1 + x2) >> 1;
	dx = x - midX;
	midY = (y1 + y2) >> 1;
	dy = y - midY;
	if (abs(dx) + abs(dy) < halfRange)
		return 0;
	return 1;
}

/*
 * 0x00443138 - IsCoordInRect
 *
 * Returns 1 when (x, y) lies within the inclusive rectangle
 * (x1, y1)-(x2, y2).
 */
int
IsCoordInRect(int x, int y, int x1, int y1, int x2, int y2)
{
	if (x < x1)
		return 0;
	if (x > x2)
		return 0;
	if (y < y1)
		return 0;
	if (y > y2)
		return 0;
	return 1;
}

/*
 * 0x00443166 - Mobile_IsFacingEntity
 *
 * Returns 1 when mob1 is facing toward mob2, within a one-octant tolerance.
 */
int
Mobile_IsFacingEntity(CMobile *mob1, CMobile *mob2)
{
	CLocation *loc1, *loc2;
	int dirToTarget, facing;

	loc1 = &mob1->container.item.resourceEntity.entity.location;
	loc2 = &mob2->container.item.resourceEntity.entity.location;

	if (loc1->x == loc2->x && loc1->y == loc2->y)
		return 1;

	dirToTarget = CalcDirection(loc1, loc2);
	facing = mob1->direction & 0x07;

	if (facing == dirToTarget)
		return 1;
	if (((facing + 1) & 7) == dirToTarget)
		return 1;
	if (((facing - 1) & 7) == dirToTarget)
		return 1;
	return 0;
}

/*
 * 0x0044320E - RadianToUODir
 *
 * Converts a radian angle to UO direction units: angle * 128 / pi.
 */
int
RadianToUODir(float angle)
{
	return (int)(angle * 128.0f / 3.14152);
}

/*
 * 0x00443227 - Combat_TurnPerpendicularTo
 *
 * Turns a non-player mobile a quarter circle off the bearing toward
 * other, then detaches and re-drops it at its own location so nearby
 * clients see the new facing. No-op when either entity has left the
 * world or self is a player.
 *
 * The bearing is wrapped by subtracting 0x100 only when it exceeds
 * 0x100, so a bearing of exactly 0x100 survives and stores direction 8,
 * one past the 0-7 range. Reproduced as it stands.
 */
static __attribute__((unused)) void
Combat_TurnPerpendicularTo(CItem *self, CItem *other)
{
	CLocation loc;
	float angle;
	int dir, dx, dy;

	if (self->resourceEntity.entity.removedFromWorld != 0)
		return;
	if (other->resourceEntity.entity.removedFromWorld != 0)
		return;
	if (((int (*)(void *))VT_FN(self, VT_IS_PLAYER))(self))
		return;

	dx = (int16_t)((CLocation * (*)(void *)) VT_FN(other, VT_GET_LOCATION))(other)->x - (int16_t)((CLocation * (*)(void *)) VT_FN(self, VT_GET_LOCATION))(self)->x;
	dy = (int16_t)((CLocation * (*)(void *)) VT_FN(other, VT_GET_LOCATION))(other)->y - (int16_t)((CLocation * (*)(void *)) VT_FN(self, VT_GET_LOCATION))(self)->y;

	angle = (float)(atan2((float)dy, (float)dx) + 6.2831853072);
	dir = RadianToUODir(angle);

	dir += 0x40;
	if (dir > 0x100)
		dir -= 0x100;
	dir = dir / 32;

	((void (*)(void *))VT_FN(self, VT_DETACH_SPATIAL))(self);
	CLocation_SetLoc(&loc, ((CLocation * (*)(void *)) VT_FN(self, VT_GET_LOCATION))(self));
	((CMobile *)self)->direction = (uint8_t)dir;
	((void (*)(void *, CLocation *))VT_FN(self, VT_DROP_AT_FEET))(self, &loc);
}

/*
 * 0x0044335C - Combat_ConsumeAmmo
 *
 * Removes one unit of the weapon's ammo type from mob's equipment.
 * Returns 1 if any was consumed, 0 otherwise.
 */
static int
Combat_ConsumeAmmo(CMobile *mob, CItem *weapon)
{
	uint16_t ammoType;
	CItem *ammo;

	ammoType = CItem_GetAmmoType(weapon);
	ammo = CMobile_FindItemInEquipment(mob, ammoType, 1);
	if (ammo == NULL)
		return 0;
	if (ammo != NULL)
		((void (*)(void *))VT_FN(ammo, VT_DELETE))(ammo);
	return 1;
}

/*
 * 0x004433A7 - Combat_CheckStrength
 *
 * Stub - always returns 0 so the swing is never aborted.
 */
static int
Combat_CheckStrength(CMobile *mob)
{
	USED(mob);
	return 0;
}

// 0x0061BB28 - direction offset X table (N, NE, E, SE, S, SW, W, NW)
static const int32_t g_DirOffsetX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
// 0x0061BB50 - direction offset Y table (N, NE, E, SE, S, SW, W, NW)
static const int32_t g_DirOffsetY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

#define ARMOR_GROUP_COUNT   14
#define ARMOR_SLOTS_PER_GRP 4
// 0x00614FC0 - armor group to item-id slots (14 groups x 4 slots)
static const uint8_t armorGroupTable[ARMOR_GROUP_COUNT][ARMOR_SLOTS_PER_GRP] = {
	{ 6, 0, 0, 0 },    //  0: head (front) - helm
	{ 6, 0, 0, 0 },    //  1: head (back) - helm
	{ 10, 0, 0, 0 },   //  2: neck/gorget - neck
	{ 13, 5, 22, 20 }, //  3: chest (1) - chest, shirt, robe, cloak
	{ 13, 5, 22, 20 }, //  4: chest (2)
	{ 13, 5, 22, 20 }, //  5: chest (3)
	{ 13, 5, 22, 20 }, //  6: chest (4)
	{ 13, 5, 22, 20 }, //  7: chest (5)
	{ 24, 4, 3, 23 },  //  8: legs (1) - leggings, pants, shoes, skirt
	{ 24, 4, 3, 23 },  //  9: legs (2)
	{ 24, 4, 3, 23 },  // 10: legs (3)
	{ 19, 5, 0, 0 },   // 11: arms (1) - arms, shirt
	{ 19, 5, 0, 0 },   // 12: arms (2)
	{ 7, 0, 0, 0 },    // 13: hands - gloves
};

/*
 * Durability system.
 */

/*
 * 0x004433AE - CMobile::GetWeaponSkill
 *
 * Iterates the weapon type → skill table, picking the skill with the
 * highest current value. Default is Wrestling (43) when unarmed.
 */
int
CMobile_GetWeaponSkill(CMobile *mob, CItem *weapon)
{
	uint32_t typeFlags;
	int bestSkillId;
	int bestSkillVal;
	int i;
	int val;

	// Default type flags: 0x00010000 for unarmed (matches entry 0 = Wrestling)
	typeFlags = 0x10000;
	if (weapon != NULL)
		typeFlags = CItem_GetWeaponDamageType(weapon) & 0xff;

	bestSkillVal = -1000;
	bestSkillId = 43; // Wrestling default

	for (i = 0; i < 5; i++) {
		if ((typeFlags & weaponSkillTable[i].mask) == 0)
			continue;
		val = CMobile_GetSkillValue(mob, (int8_t)weaponSkillTable[i].skillId, 0);
		if (val > bestSkillVal) {
			bestSkillVal = val;
			bestSkillId = (int)weaponSkillTable[i].skillId;
		}
	}

	return bestSkillId;
}

/*
 * 0x00443449 - CMobile::CalcDamage
 *
 * damage = (tactics + STR*2 + 500) * weaponDamage / 1000, minimum 1.
 */
int
CMobile_CalcDamage(CMobile *attacker, CItem *weapon)
{
	int weaponDamage;
	int str;
	int tactics;
	int result;

	if (weapon != NULL)
		weaponDamage = CItem_RollWeaponDamage(weapon);
	else
		weaponDamage = CMobile_GetNaturalDamage(attacker);

	str = (int)(uint16_t)attacker->baseStr;
	tactics = CMobile_GetSkillValue(attacker, 0x1B, 0); // skill 27 = Tactics

	result = (tactics + str * 2 + 500) * weaponDamage / 1000;

	if (result < 1)
		result = 1;
	return result;
}

/*
 * 0x004434C0 - Combat_GetSwingAnimType
 *
 * Returns the swing animation subtype for a weapon: 0=slash, 1=thrust,
 * 2=chop. Slashing+piercing weapons pick slash or chop at random.
 */
static int
Combat_GetSwingAnimType(CItem *weapon)
{
	if (CItem_IsSlashing(weapon) && CItem_IsPiercing(weapon)) {
		if (GetRandomRange(0, 100) > 50)
			return 0; /* slash */
		return 2;         /* chop */
	}

	if (CItem_IsSlashing(weapon))
		return 0; /* slash */

	if (CItem_IsBashing(weapon))
		return 1; /* thrust */

	if (CItem_IsPiercing(weapon))
		return 2; /* chop */

	return 0; /* default: slash */
}

/*
 * 0x0044352A - Combat_FireProjectilePastTarget
 *
 * Fires the shooter's ammo graphic at a point twice the shooter-to-
 * target offset beyond the target, at the target's own z. No-op when
 * the shooter has no weapon.
 */
static __attribute__((unused)) void
Combat_FireProjectilePastTarget(CItem *shooter, CItem *target)
{
	CLocation dest;
	CItem *weapon;
	int dx, dy;
	uint16_t ammoType;

	CLocation_Init(&dest);

	dx = (int16_t)((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target)->x - (int16_t)((CLocation * (*)(void *)) VT_FN(shooter, VT_GET_LOCATION))(shooter)->x;
	dy = (int16_t)((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target)->y - (int16_t)((CLocation * (*)(void *)) VT_FN(shooter, VT_GET_LOCATION))(shooter)->y;
	dx *= 2;
	dy *= 2;

	dest.x = (uint16_t)((int16_t)((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target)->x + dx);
	dest.y = (uint16_t)((int16_t)((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target)->y + dy);
	dest.z = ((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target)->z;

	weapon = CMobile_GetWeapon((CMobile *)shooter);
	if (weapon == NULL)
		return;

	ammoType = CItem_GetAmmoType(weapon);
	Script_doMissile_Mob2Loc(shooter->serial, &dest, ammoType + 3, 5, 0, 0);
}

/*
 * 0x00443626 - Combat_FireProjectile
 *
 * Plays the moving-projectile effect from attacker to target for a
 * ranged weapon.
 */
static void
Combat_FireProjectile(CMobile *attacker, CMobile *target)
{
	CItem *weapon;
	uint16_t ammoType;

	weapon = CMobile_GetWeapon(attacker);
	if (weapon == NULL)
		return;

	ammoType = CItem_GetAmmoType(weapon);
	Script_doMissile_Mob2Mob(attacker->container.item.serial, target->container.item.serial, ammoType + 3, 9, 0, 0);
}

/*
 * 0x00443674 - Combat_PlayWeaponHitSfx
 *
 * Plays the weapon's hit SFX at the target's location.
 */
void
Combat_PlayWeaponHitSfx(CItem *unused, CItem *target, CItem *weapon)
{
	CLocation *loc;

	USED(unused);

	if (weapon == NULL)
		return;

	loc = ((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target);
	PlaySoundAtLocation(loc, CItem_GetHitSfx(weapon), 0);
}

/*
 * 0x004436A8 - Combat_PlayMeleeMissSfx
 *
 * Plays the weapon's miss sound effect at the attacker's location.
 * No-op when no weapon is supplied. The middle argument is unused.
 */
void
Combat_PlayMeleeMissSfx(CItem *attacker, CItem *unused, CItem *weapon)
{
	CLocation *loc;

	USED(unused);

	if (weapon == NULL)
		return;

	loc = ((CLocation * (*)(void *)) VT_FN(attacker, VT_GET_LOCATION))(attacker);
	PlaySoundAtLocation(loc, CItem_GetMissSfx(weapon), 0);
}

/*
 * 0x004436DC - Combat_PlayRangedMissEffects
 *
 * On a ranged miss, 50% of the time drops an ammo item one step past
 * the target and plays a body-type-dependent flinch animation.
 */
static void
Combat_PlayRangedMissEffects(CItem *attacker, CItem *target, CItem *weapon)
{
	CLocation delta;
	CLocation impactLoc;
	CLocation *targetLoc;
	int magnitude;
	CItem *newAmmo;
	int blockIdx;
	CItem *iter;
	int bodyType;
	int rand2;

	if (weapon == NULL)
		goto part2;
	if (!CItem_IsRangedWeapon(weapon))
		goto part2;
	if (GetRandomRange(0, 100) >= 50)
		goto part2;

	CLocation_ComputeDelta(CEntity_GetLocation(&target->resourceEntity.entity), &delta, CEntity_GetLocation(&attacker->resourceEntity.entity));

	magnitude = CLocation_ChebyshevMagnitude(&delta);

	if (magnitude > 0) {
		delta.x = (int16_t)((int)(int16_t)delta.x / magnitude);
		delta.y = (int16_t)((int)(int16_t)delta.y / magnitude);
		delta.z = (int16_t)((int)(int16_t)delta.z / magnitude);
	}

	targetLoc = ((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target);
	CLocation_AddWrapped(targetLoc, &impactLoc, &delta);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)impactLoc.x, (int)(int16_t)impactLoc.y)) {
		targetLoc = ((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target);
		CLocation_SetLoc(&impactLoc, targetLoc);
	} else {
		impactLoc.z = (int16_t)CTerrainManager_CanWalkWrapper(impactLoc, (int)(int16_t)impactLoc.z, (int)(int16_t)impactLoc.z + 16, 1, 0, NULL, 0);

		targetLoc = ((CLocation * (*)(void *)) VT_FN(target, VT_GET_LOCATION))(target);
		if ((int)(int16_t)impactLoc.z < (int)(int16_t)targetLoc->z - 4) {
			CLocation_SetLoc(&impactLoc, targetLoc);
		}
	}

	newAmmo = CWorld_CreateItem(g_World, CItem_GetAmmoType(weapon));
	CItem_Setup(newAmmo, 0, &impactLoc, 0, 1);

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &impactLoc, 0);
	iter = ((MapBlock *)g_SpatialGrid.cells)[blockIdx].itemHead;
	while (iter != NULL) {
		CLocation *iterLoc;

		iterLoc = CEntity_GetLocation(&iter->resourceEntity.entity);
		if (CLocation_IsEqualXYZ(iterLoc, &impactLoc)) {
			if (((int (*)(void *, void *))VT_FN(newAmmo, VT_MERGE_INTO))(newAmmo, iter)) {
				CItem_MergeIntoWrapper(newAmmo, iter);
				newAmmo = NULL;
				break;
			}
		}
		iter = iter->spatialNext;
	}

	if (newAmmo != NULL) {
		((void (*)(void *, CLocation *))VT_FN(newAmmo, VT_DROP_AT_FEET))(newAmmo, &impactLoc);
	}

part2:
	bodyType = CEntity_GetBodyType(target) & 0xFFFF;

	if (bodyType >= 0x96)
		return;
	if (bodyType < 0x190)
		return;
	if (bodyType == 8)
		return;
	if (bodyType == 5)
		return;
	if (bodyType == 6)
		return;
	if (bodyType == 0x16)
		return;

	rand2 = GetRandomRange(0, 100);
	if (rand2 >= 0x21)
		return;

	if (bodyType == 0x15) {
		BroadcastAnimation((CMobile *)target, (rand2 >= 0x11) ? 16 : 15, 3, 1, 0, 1, 0);
	} else if (bodyType < 0x96) {
		BroadcastAnimation((CMobile *)target, (rand2 >= 0x11) ? 16 : 15, 3, 1, 0, 0, 0);
	} else {
		if (VT_IsPlayer(target) && CMobile_IsMounted((CMobile *)target))
			return;
		BroadcastAnimation((CMobile *)target, 30, 5, 1, 0, 0, 0);
	}
}

/*
 * 0x00443A19 - Combat_HitEffects
 *
 * Plays the NPC hit sounds, may drop ammo in the target's container for
 * ranged hits, triggers the body-type flinch animation, and spawns blood
 * items at the target (plus a 50% chance of a splatter on an adjacent tile).
 */
static void
Combat_HitEffects(CMobile *attacker, CMobile *target, CItem *weapon)
{
	CLocation *targetLoc;
	CLocation splatLoc;
	uint16_t tgtBody;
	CItem *blood;
	int dir;

	if (VT_IsNPC((CItem *)target) && target->sfxWasHit != 0) {
		targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)target, VT_GET_LOCATION))(target);
		PlaySoundAtLocation(targetLoc, target->sfxWasHit, 0);
	}

	if (VT_IsNPC((CItem *)attacker) && attacker->sfxWasHit != 0) {
		CLocation *attackerLoc;
		attackerLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)attacker, VT_GET_LOCATION))(attacker);
		PlaySoundAtLocation(attackerLoc, attacker->sfxHit, 0);
	}

	if (weapon != NULL && CItem_IsRangedWeapon(weapon) && !VT_IsPlayer((CItem *)target) && GetRandomRange(0, 100) < 25) {
		CItem *ammo;
		CLocation tempLoc;
		ammo = CWorld_CreateItem(g_World, CItem_GetAmmoType(weapon));
		targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)target, VT_GET_LOCATION))(target);
		CItem_Setup(ammo, 0, targetLoc, 0, 1);
		CLocation_Constructor3D(&tempLoc, -1, -1, 0);
		((void (*)(void *, CItem *, CLocation *))VT_FN(ammo, 0xB4))(ammo, (CItem *)target, &tempLoc);
	}

	tgtBody = CEntity_GetBodyType((CItem *)target) & 0xFFFF;

	if (tgtBody < 0x96) {
		if (VT_IsNPC((CItem *)target) && ((CNPC *)target)->aiByte3 == 1) {
			BroadcastAnimation(target, 0x15, 7, 1, 0, 0, 0);
		} else if ((CEntity_GetBodyType((CItem *)target) & 0xFFFF) == 0x15) {
			BroadcastAnimation(target, 0x0A, 4, 1, 0, 1, 0);
		} else if ((CEntity_GetBodyType((CItem *)target) & 0xFFFF) == 0x06) {
			// body 0x06 has no get-hit animation
		} else if ((CEntity_GetBodyType((CItem *)target) & 0xFFFF) == 0x16) {
			// body 0x16 has no get-hit animation
		} else {
			BroadcastAnimation(target, 0x0A, 4, 1, 0, 0, 0);
		}
	} else if (tgtBody < 0xC8) {
		BroadcastAnimation(target, 0x07, 5, 1, 0, 0, 0);
	} else if (tgtBody < 0x190) {
		BroadcastAnimation(target, 0x07, 5, 1, 0, 0, 0);
	} else {
		if (VT_IsPlayer((CItem *)target) && CMobile_IsMounted(target))
			goto blood_item;
		BroadcastAnimation(target, 0x14, 5, 1, 0, 0, 0);
	}

blood_item:
	blood = CWorld_CreateItem(g_World, 0x1645);
	if (blood != NULL) {
		targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)target, VT_GET_LOCATION))(target);
		((void (*)(void *, CLocation *))VT_FN(blood, VT_DROP_AT_FEET))(blood, targetLoc);
		blood->itemFlags |= ItemFlag_Poisoned;
		CItem_Setup(blood, 0, targetLoc, 0, 1);
		if (!ValidateInWorld(blood))
			blood = NULL;
		if (blood != NULL)
			ScheduleEvent(0x14, blood->serial, 1, 0, 0);
	}

	if (GetRandomRange(0, 2) != 1)
		return;
	dir = GetRandomRange(0, 7);
	targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)target, VT_GET_LOCATION))(target);
	splatLoc = *targetLoc;
	splatLoc.x += g_DirOffsetX[dir];
	splatLoc.y += g_DirOffsetY[dir];
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)splatLoc.x, (int16_t)splatLoc.y))
		return;
	blood = CWorld_CreateItem(g_World, 0x1645);
	if (blood == NULL)
		return;
	((void (*)(void *, CLocation *))VT_FN(blood, VT_DROP_AT_FEET))(blood, &splatLoc);
	blood->itemFlags |= ItemFlag_Poisoned;
	CItem_Setup(blood, 0, &splatLoc, 0, 1);
	if (!ValidateInWorld(blood))
		blood = NULL;
	if (blood != NULL)
		ScheduleEvent(0x14, blood->serial, 1, 0, 0);
}

/*
 * 0x00443EBC - Combat_PlaySwingAnimation
 *
 * Orients the attacker toward the target, sends a SWING packet for
 * players, and broadcasts a body-type/weapon-specific ANIM_ACTION to the
 * union of both mobs' nearby-player sets.
 */
void
Combat_PlaySwingAnimation(CMobile *mob, CItem *weapon, CMobile *target)
{
	uint16_t animId;
	uint16_t frameCount;
	uint16_t repeatCount;
	uint8_t backward;
	uint8_t repeat;
	uint8_t pktBuf[300];
	CLocation *attackerLoc;
	CLocation *targetLoc;
	int dx, dy;
	int i;

	animId = 0;
	frameCount = 0;
	repeatCount = 0;
	backward = 0;
	repeat = 0;

	if (mob != NULL && target != NULL)
		CEntity_GetBodyType((CItem *)mob); // result unused in binary

	if (mob == NULL)
		return;
	if (CWorld_FindBySerial(g_World, mob->container.item.serial) != (CItem *)mob)
		return;
	if (mob->container.item.resourceEntity.entity.removedFromWorld)
		return;

	if (VT_IsNPC((CItem *)mob)) {
		targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)target, VT_GET_LOCATION))(target);
		dx = (int)(int16_t)targetLoc->x;
		attackerLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)mob, VT_GET_LOCATION))(mob);
		dx -= (int)(int16_t)attackerLoc->x;

		targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)target, VT_GET_LOCATION))(target);
		dy = (int)(int16_t)targetLoc->y;
		attackerLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)mob, VT_GET_LOCATION))(mob);
		dy -= (int)(int16_t)attackerLoc->y;

		if (dx < 0)
			dx = -1;
		else if (dx > 0)
			dx = 1;
		if (dy < 0)
			dy = -1;
		else if (dy > 0)
			dy = 1;

		for (i = 0; i < 7; i++) {
			if (g_DirOffsetX[i] == dx && g_DirOffsetY[i] == dy)
				break;
		}

		if (i != (int)mob->direction) {
			CVector playerList;
			char typeFlag[16] = { 0 };

			CMobile_SetDirection((CItem *)mob, (uint32_t)i);
			CVector_Constructor(&playerList, typeFlag);
			attackerLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)mob, VT_GET_LOCATION))(mob);
			GetNearbyPlayers(&playerList, attackerLoc, 0x12);
			((void (*)(void *, CVector *, int))VT_FN((CItem *)mob, VT_NOTIFY_NEARBY))(mob, &playerList, 0);
			CVector_Destructor(&playerList);
		}
	} else {
		PacketManager_MakePacket_SWING(pktBuf, 0, mob->container.item.serial, target->container.item.serial);
		SendToClient((CItem *)mob, pktBuf, -1);
	}

	repeat = 0;
	repeatCount = 1;
	frameCount = 4;

	if ((CEntity_GetBodyType((CItem *)mob) & 0xFFFF) == 0x0C) {
		int r = GetRandomRange(1, 4);
		repeat = 1;
		switch (r) {
		case 1:
			animId = 4;
			break;
		case 2:
			animId = 5;
			break;
		case 3:
			animId = 6;
			break;
		case 4:
			animId = 12;
			frameCount = 15;
			repeat = 0;
			break;
		}
	} else if ((CEntity_GetBodyType((CItem *)mob) & 0xFFFF) == 5) {
		animId = 4;
	} else if ((CEntity_GetBodyType((CItem *)mob) & 0xFFFF) < 0x96) {
		int r = GetRandomRange(1, 3);
		switch (r) {
		case 1:
			animId = 4;
			break;
		case 2:
			animId = 5;
			break;
		case 3:
			animId = 6;
			break;
		}
	} else if ((CEntity_GetBodyType((CItem *)mob) & 0xFFFF) < 0xC8) {
		if ((CEntity_GetBodyType((CItem *)mob) & 0xFFFF) == 0x97)
			return;
		{
			int r = GetRandomRange(1, 2);
			switch (r) {
			case 1:
				animId = 5;
				frameCount = 15;
				break;
			case 2:
				animId = 6;
				frameCount = 20;
				break;
			}
		}
	} else if ((CEntity_GetBodyType((CItem *)mob) & 0xFFFF) < 0x190) {
		int r = GetRandomRange(1, 2);
		switch (r) {
		case 1:
			animId = 5;
			break;
		case 2:
			animId = 6;
			break;
		}
		frameCount = 5;
	} else {
		if (CMobile_IsMounted(mob)) {
			if (weapon != NULL && CItem_IsRangedWeapon(weapon)) {
				animId = 0x1B;
				frameCount = 5;
			} else if (weapon != NULL && CItem_IsTwoHandedWeapon(weapon)) {
				animId = 0x1D;
				frameCount = 5;
			} else {
				animId = 0x1A;
				frameCount = 5;
			}
		} else if (weapon == NULL) {
			animId = 0x1F;
			frameCount = 7;
		} else if (CItem_IsTwoHandedWeapon(weapon)) {
			if (CItem_IsRangedWeapon(weapon)) {
				animId = 0x12;
				frameCount = 7;
				Combat_FireProjectile(mob, target);
			} else {
				int swingType = Combat_GetSwingAnimType(weapon);
				switch (swingType) {
				case 0:
					animId = 0x0D;
					frameCount = 7;
					break;
				case 1:
					animId = 0x0C;
					frameCount = 7;
					break;
				case 2:
					animId = 0x0E;
					frameCount = 7;
					break;
				}
			}
		} else {
			int swingType = Combat_GetSwingAnimType(weapon);
			switch (swingType) {
			case 0:
				animId = 0x09;
				frameCount = 7;
				break;
			case 1:
				animId = 0x0B;
				frameCount = 7;
				break;
			case 2:
				animId = 0x0A;
				frameCount = 7;
				break;
			}
		}
	}

	PacketManager_MakePacket_ANIM(pktBuf, mob->container.item.serial, animId, frameCount, repeatCount, backward, repeat, 1);
	targetLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)target, VT_GET_LOCATION))(target);
	attackerLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)mob, VT_GET_LOCATION))(mob);
	CPlayerList_BroadcastToTwoLocs(pktBuf, attackerLoc, targetLoc, 0x12, NULL);
}

/*
 * 0x00444434 - CMobile::GetBestArmorInGroup
 *
 * Returns the item with the highest armor rating among the armor group's
 * candidate equipment slots.
 */
static CItem *
CMobile_GetBestArmorInGroup(CMobile *mob, int groupIdx)
{
	int bestAC;
	CItem *bestItem;
	int j;

	bestAC = 0;
	bestItem = NULL;

	for (j = 0; j < ARMOR_SLOTS_PER_GRP; j++) {
		uint8_t slotId = armorGroupTable[groupIdx][j];
		CItem *item;
		int ac;

		if (slotId == 0)
			break;

		item = mob->equipment[slotId];
		if (item == NULL)
			continue;

		if (!VT_IsWeapon(item))
			continue;

		ac = CItem_GetArmorRating(item) & 0xFF;
		if (ac > bestAC) {
			bestAC = ac;
			bestItem = item;
		}
	}
	return bestItem;
}

/*
 * 0x004444EB - Combat_CalcArmorClass
 *
 * Averages the best armor rating across the 14 armor groups, adds the
 * mobile's bonus AC and a Parrying-scaled shield contribution, and
 * clamps the result to [0, 9999].
 */
int
Combat_CalcArmorClass(CMobile *mob)
{
	int totalAR;
	int i;
	CItem *best;
	CItem *shield;
	int shieldAR;
	int parrying;
	int shieldBonus;

	totalAR = 0;
	for (i = 0; i < ARMOR_GROUP_COUNT; i++) {
		best = CMobile_GetBestArmorInGroup(mob, i);
		if (best != NULL)
			totalAR += CItem_GetArmorRating(best) & 0xFF;
	}
	totalAR /= ARMOR_GROUP_COUNT;

	totalAR += CMobile_GetBonusAC(mob);

	shield = mob->equipment[2];
	if (shield != NULL && VT_IsWeapon(shield) && CItem_IsShield(shield)) {
		shieldAR = CItem_GetArmorRating(shield) & 0xFF;
		parrying = (CMobile_GetSkillValue(mob, 5, 0) + 1) / 2; // skill 5 = Parrying
		if (parrying > 1000)
			parrying = 1000;
		shieldBonus = (shieldAR * parrying + 999) / 1000;
		totalAR += shieldBonus;
	}

	if (totalAR < 0)
		totalAR = 0;
	if (totalAR > 9999)
		totalAR = 9999;
	return totalAR;
}

/*
 * 0x00444610 - Combat_GetMaterialModifiers
 *
 * Writes three percent-style modifiers for a (kind, resource type)
 * pair. Only kinds 4 and 2 are recognised, and within them only wood,
 * cloth, leather and metal; every other combination leaves all three
 * outputs untouched. Kind 4 gives bonuses, kind 2 mostly penalties.
 */
static __attribute__((unused)) void
Combat_GetMaterialModifiers(int kind, int resType, int *out1, int *out2, int *out3)
{
	if (kind == 4) {
		if (resType == g_ResTypeId_Wood) {
			*out1 = 40;
			*out2 = 40;
			*out3 = 40;
		} else if (resType == g_ResTypeId_Cloth) {
			*out1 = 40;
			*out2 = 40;
			*out3 = 40;
		} else if (resType == g_ResTypeId_Leather) {
			*out1 = 20;
			*out3 = 20;
		} else if (resType == g_ResTypeId_Metal) {
			*out2 = 20;
			*out3 = 20;
		}
	} else if (kind == 2) {
		if (resType == g_ResTypeId_Cloth) {
			*out1 = -40;
			*out2 = -20;
		} else if (resType == g_ResTypeId_Leather) {
			*out1 = -20;
			*out2 = -40;
		} else if (resType == g_ResTypeId_Metal) {
			*out1 = -20;
			*out2 = 40;
			*out3 = 40;
		}
	}
}

/*
 * 0x00444717 - Combat_AverageMaterialBonus
 *
 * Adds a bonus to *inOut for each damage-type bit the material answers
 * to - 20 for cloth, leather and metal, 40 for bone and wood - then
 * divides the running total by how many bits matched. Bits 2 and 8
 * count as one. Leaves *inOut untouched when nothing matched.
 */
static __attribute__((unused)) void
Combat_AverageMaterialBonus(uint32_t flags, int resType, int *inOut)
{
	int count;

	count = 0;

	if (resType == g_ResTypeId_Cloth) {
		if (flags & 1) {
			count++;
			*inOut += 20;
		}
		if ((flags & 2) || (flags & 8)) {
			count++;
			*inOut += 20;
		}
	}

	if (resType == g_ResTypeId_Leather) {
		if (flags & 1) {
			count++;
			*inOut += 20;
		}
		if ((flags & 2) || (flags & 8)) {
			count++;
			*inOut += 20;
		}
	}

	if (resType == g_ResTypeId_Metal) {
		if (flags & 1) {
			count++;
			*inOut += 20;
		}
		if ((flags & 2) || (flags & 8)) {
			count++;
			*inOut += 20;
		}
		if (flags & 4) {
			count++;
			*inOut += 20;
		}
	}

	if (resType == g_ResTypeId_Bone && (flags & 4)) {
		count++;
		*inOut += 40;
	}

	if (resType == g_ResTypeId_Wood && (flags & 4)) {
		count++;
		*inOut += 40;
	}

	if (count != 0)
		*inOut /= count;
}

/*
 * 0x004448AF - Combat_AppendDamageType
 *
 * Appends typeName to buf, prefixed with "/" when count > 0. Note the
 * caller passes count by value so the increment is local - the caller
 * tracks the real count.
 */
static void
Combat_AppendDamageType(CString *buf, int count, CString *typeName)
{
	count = count + 1;
	if (count > 1)
		CString_AppendCStr(buf, "/");
	CString_ConcatCString(buf, typeName);
}

/*
 * 0x004448DC - Combat_BuildPhysicalDamageMsg
 *
 * Builds a physical-damage message for a weapon, e.g. "Your sword takes
 * much more damage than usual from the slashing/piercing attack."
 */
static void
Combat_BuildPhysicalDamageMsg(CString *buf, CString *weaponName, int dmgDelta, uint32_t dmgTypes, int isThirdPerson)
{
	CString typeStr;
	int count;

	if (isThirdPerson)
		CString_AssignCStr(buf, "Their ");
	else
		CString_AssignCStr(buf, "Your ");

	CString_ConcatCString(buf, weaponName);

	if (dmgDelta > 0)
		CString_AppendCStr(buf, " takes");
	else
		CString_AppendCStr(buf, " resists");

	if (abs(dmgDelta) > 0) {
		if (abs(dmgDelta) > 1)
			CString_AppendCStr(buf, " much");
		CString_AppendCStr(buf, " more damage than usual from the ");
	} else {
		CString_AppendCStr(buf, " damage from the ");
	}

	count = 0;

	if (dmgTypes & 1) {
		CString_Constructor(&typeStr, "slashing");
		Combat_AppendDamageType(buf, count, &typeStr);
		CString_Destructor(&typeStr);
	}

	if ((dmgTypes & 2) || (dmgTypes & 8)) {
		CString_Constructor(&typeStr, "piercing");
		Combat_AppendDamageType(buf, count, &typeStr);
		CString_Destructor(&typeStr);
	}

	if (dmgTypes & 4) {
		CString_Constructor(&typeStr, "bashing");
		Combat_AppendDamageType(buf, count, &typeStr);
		CString_Destructor(&typeStr);
	}

	CString_AppendCStr(buf, " attack.");
}

/*
 * 0x00444A80 - Combat_BuildElementalDamageBody
 *
 * Appends the verb, magnitude and element ("fire" or "energy") of the
 * elemental damage message.
 */
static void
Combat_BuildElementalDamageBody(CString *buf, int dmgDelta, uint32_t elemType, int hasName)
{
	if (dmgDelta > 0)
		CString_AppendCStr(buf, " take");
	else
		CString_AppendCStr(buf, " resist");

	if (hasName)
		CString_AppendCStr(buf, "s");

	if (abs(dmgDelta) > 0) {
		if (abs(dmgDelta) > 1)
			CString_AppendCStr(buf, " much");
		CString_AppendCStr(buf, " more damage than usual from the ");
	} else {
		CString_AppendCStr(buf, " damage from the ");
	}

	if (elemType == 4)
		CString_AppendCStr(buf, "fire attack.");
	else
		CString_AppendCStr(buf, "energy attack.");
}

/*
 * 0x00444B26 - Combat_BuildElementalDamageMsg
 *
 * Writes the perspective-dependent subject prefix ("Your <weapon>",
 * "Their <weapon>", "You", "They") and appends the elemental body.
 */
static void
Combat_BuildElementalDamageMsg(CString *buf, CString *weaponName, int dmgDelta, uint32_t elemType, int perspective)
{
	int hasName;

	hasName = 0;

	if (perspective == 0) {
		CString_AssignCStr(buf, "Your ");
		CString_ConcatCString(buf, weaponName);
		hasName = 1;
	} else if (perspective == 1) {
		CString_AssignCStr(buf, "Their ");
		CString_ConcatCString(buf, weaponName);
		hasName = 1;
	} else if (perspective == 2) {
		CString_AssignCStr(buf, "You");
	} else {
		CString_AssignCStr(buf, "They");
	}

	Combat_BuildElementalDamageBody(buf, dmgDelta, elemType, hasName);
}

/*
 * 0x00444BBF - Combat_DestroyEquippedItem
 *
 * Sends "Your/Their <item> is destroyed by the attack." to the player;
 * the caller deletes the item.
 */
static void
Combat_DestroyEquippedItem(CMobile *player, CItem *item, int isOpponent)
{
	CString str;
	char *name;

	if (player == NULL)
		return;
	if (!VT_IsPlayer((CItem *)player))
		return;

	CString_DefaultConstructor(&str);

	if (isOpponent)
		CString_AssignCStr(&str, "Their ");
	else
		CString_AssignCStr(&str, "Your ");

	name = ((char *(*)(void *, int))VT_FN(item, VT_SPEAK_SYS_MSG))(item, 0);
	CString_AppendCStr(&str, name);
	CString_AppendCStr(&str, " is destroyed by the attack.");

	CPlayer_SystemMessage((CPlayer *)player, CString_GetBuffer(&str));

	CString_Destructor(&str);
}

/*
 * 0x00444C75 - Combat_ApplyDurabilityLoss
 *
 * Absorbs damage through defender armor and shield (melee uses the full
 * AC/parry path; type 2/4 take a dead-code percentage path; type 8 skips
 * absorption) and then applies durability loss to the weapon, armor and
 * shield, destroying any piece that breaks. Returns the damage that
 * remains after absorption.
 */
static int
Combat_ApplyDurabilityLoss(CItem *attacker, CItem *defender, CItem **weapon_ptr, CItem **armor_ptr, CItem **shield_ptr, int *out_totalAC, int *out_armorAbsorb,
        int *out_shieldAbsorb, uint32_t damageType, int incomingDamage)
{
	CString defenderMsg;
	CString attackerMsg;
	CString tempStr;
	char *armorName;
	int bonusAC;
	int armorAR;
	int magicMelee = 0;
	int magicRanged;
	int parryDiff;
	int totalAbsorb;
	int weaponResult;
	int armorResult;
	int shieldResult;

	CString_DefaultConstructor(&defenderMsg);
	CString_DefaultConstructor(&attackerMsg);

	if (*armor_ptr != NULL) {
		CItem_ResolveMaterialType(*armor_ptr); // result unused in binary
	}

	bonusAC = CMobile_GetBonusAC((CMobile *)defender);
	if (bonusAC > 0)
		bonusAC = GetRandomRange(bonusAC / 2, bonusAC);

	if (*armor_ptr != NULL)
		armorAR = CItem_GetArmorRating(*armor_ptr) & 0xFF;
	else
		armorAR = 0;
	if (armorAR > 0)
		armorAR = GetRandomRange(armorAR / 4, (armorAR + 1) / 2);

	if (damageType == 1) {
		*out_totalAC = bonusAC + armorAR;
		*out_armorAbsorb = armorAR;
		magicMelee = bonusAC;

		if (*shield_ptr != NULL) {
			if (GetRandom(100) > 50) {
				if (attacker != NULL && VT_IsPlayer(attacker))
					parryDiff = 50;
				else
					parryDiff = 100;

				if (CMobile_SkillCheck((CMobile *)defender, 5, incomingDamage * 25, 50, 0, parryDiff, 1) > 0) {
					*out_shieldAbsorb = (CItem_GetArmorRating(*shield_ptr) & 0xFF) / 2;
				}
			}
		}

		totalAbsorb = *out_armorAbsorb + *out_shieldAbsorb + magicMelee;
		if (incomingDamage > totalAbsorb)
			totalAbsorb = totalAbsorb;
		else
			totalAbsorb = incomingDamage;
		incomingDamage -= totalAbsorb;

		// Dead code in binary: magicMelee is always 0 below.
		magicMelee = 0;
		if (magicMelee != 0) {
			*out_armorAbsorb = *out_armorAbsorb * (magicMelee + 100) / 100;
		}
		if (magicMelee != 0 && *armor_ptr != NULL && VT_IsPlayer(defender)) {
			armorName = ((char *(*)(void *, int))VT_FN(*armor_ptr, VT_SPEAK_SYS_MSG))(*armor_ptr, 0);
			CString_Constructor(&tempStr, armorName);
			Combat_BuildPhysicalDamageMsg(&defenderMsg, &tempStr, magicMelee, damageType, 0);
			CString_Destructor(&tempStr);
		}
		if (magicMelee != 0 && *armor_ptr != NULL && attacker != NULL && VT_IsPlayer(attacker)) {
			armorName = ((char *(*)(void *, int))VT_FN(*armor_ptr, VT_SPEAK_SYS_MSG))(*armor_ptr, 0);
			CString_Constructor(&tempStr, armorName);
			Combat_BuildPhysicalDamageMsg(&attackerMsg, &tempStr, magicMelee, damageType, 1);
			CString_Destructor(&tempStr);
		}
	} else if (damageType == 8) {
		// no absorption
	} else if (damageType == 4 || damageType == 2) {
		magicRanged = 0;

		if (*armor_ptr != NULL) {
			// Dead code in binary: magicRanged is always 0.
			if (magicRanged != 0) {
				int baseDmg;
				if (incomingDamage > armorAR)
					baseDmg = incomingDamage - armorAR;
				else
					baseDmg = incomingDamage;
				*out_armorAbsorb = baseDmg * (magicRanged + 100) / 100;
				magicMelee = bonusAC;
			}
			if (magicRanged != 0 && VT_IsPlayer(defender)) {
				armorName = ((char *(*)(void *, int))VT_FN(*armor_ptr, VT_SPEAK_SYS_MSG))(*armor_ptr, 0);
				CString_Constructor(&tempStr, armorName);
				Combat_BuildElementalDamageMsg(&defenderMsg, &tempStr, magicRanged, damageType, 0);
				CString_Destructor(&tempStr);
			}
			if (magicRanged != 0 && attacker != NULL && VT_IsPlayer(attacker)) {
				armorName = ((char *(*)(void *, int))VT_FN(*armor_ptr, VT_SPEAK_SYS_MSG))(*armor_ptr, 0);
				CString_Constructor(&tempStr, armorName);
				Combat_BuildElementalDamageMsg(&attackerMsg, &tempStr, magicRanged, damageType, 1);
				CString_Destructor(&tempStr);
			}
		}

		totalAbsorb = *out_armorAbsorb + magicMelee;
		if (incomingDamage > totalAbsorb)
			incomingDamage -= totalAbsorb;
		else
			incomingDamage = 0;
	}

	if (*out_totalAC != 0 && *weapon_ptr != NULL) {
		weaponResult = CItem_DamageDurability(*weapon_ptr, *out_totalAC, incomingDamage, (intptr_t)*armor_ptr, (intptr_t)defender, -1);
		if (weaponResult == 0) {
			Combat_DestroyEquippedItem((CMobile *)defender, *weapon_ptr, 0);
			Combat_DestroyEquippedItem((CMobile *)attacker, *weapon_ptr, 1);
			if (*weapon_ptr != NULL)
				((void (*)(void *))VT_FN(*weapon_ptr, VT_DELETE))(*weapon_ptr);
			*weapon_ptr = NULL;
		}
	}

	if (*out_armorAbsorb != 0 && *armor_ptr != NULL) {
		armorResult = CItem_DamageDurability(*armor_ptr, *out_armorAbsorb, incomingDamage, (intptr_t)*weapon_ptr, 0, -1);
		if (armorResult < 2) {
			if (!CString_IsEmpty(&defenderMsg) && VT_IsPlayer(defender))
				CPlayer_SystemMessage((CPlayer *)defender, CString_GetBuffer(&defenderMsg));
			if (!CString_IsEmpty(&attackerMsg) && attacker != NULL && VT_IsPlayer(attacker))
				CPlayer_SystemMessage((CPlayer *)attacker, CString_GetBuffer(&attackerMsg));
		}
		if (armorResult == 0) {
			Combat_DestroyEquippedItem((CMobile *)defender, *armor_ptr, 0);
			Combat_DestroyEquippedItem((CMobile *)attacker, *armor_ptr, 1);
			if (*armor_ptr != NULL)
				((void (*)(void *))VT_FN(*armor_ptr, VT_DELETE))(*armor_ptr);
			*armor_ptr = NULL;
		}
	}

	if (*out_shieldAbsorb != 0 && *shield_ptr != NULL) {
		shieldResult = CItem_DamageDurability(*shield_ptr, *out_shieldAbsorb, incomingDamage, (intptr_t)*weapon_ptr, 0, -1);
		if (shieldResult == 0) {
			Combat_DestroyEquippedItem((CMobile *)defender, *shield_ptr, 0);
			Combat_DestroyEquippedItem((CMobile *)attacker, *shield_ptr, 1);
			if (*shield_ptr != NULL)
				((void (*)(void *))VT_FN(*shield_ptr, VT_DELETE))(*shield_ptr);
			*shield_ptr = NULL;
		}
	}

	CString_Destructor(&attackerMsg);
	CString_Destructor(&defenderMsg);
	return incomingDamage;
}

/*
 * 0x004452D8 - Combat_CalcArmorAbsorption
 *
 * Picks a random armor group, locates the shield in slot 2, and defers
 * to Combat_ApplyDurabilityLoss.
 */
static void
Combat_CalcArmorAbsorption(CItem *attacker, CItem *defender, CItem **weapon_ptr, int *damage_ptr, uint32_t damageType)
{
	CItem *shield;
	CItem *bestArmor;
	int armorGroup;
	int out_totalAC;
	int out_armorAbsorb;
	int out_shieldAbsorb;

	shield = NULL;

	if (damageType == 0)
		damageType = 1;

	{
		CItem *shieldItem;
		shieldItem = ((CMobile *)defender)->equipment[2];
		if (shieldItem != NULL) {
			if (VT_IsWeapon(shieldItem)) {
				if (CItem_IsShield(shieldItem))
					shield = shieldItem;
			}
		}
	}

	armorGroup = GetRandomRange(0, ARMOR_GROUP_COUNT - 1);
	bestArmor = CMobile_GetBestArmorInGroup((CMobile *)defender, armorGroup);

	out_totalAC = 0;
	out_armorAbsorb = 0;
	out_shieldAbsorb = 0;

	*damage_ptr = Combat_ApplyDurabilityLoss(attacker, defender, weapon_ptr, &bestArmor, &shield, &out_totalAC, &out_armorAbsorb, &out_shieldAbsorb, damageType, *damage_ptr);
}

/*
 * 0x004453A2 - Combat_DamageResolve
 *
 * Resolves one hit: validates the combatants, applies armor absorption,
 * halves damage, fires the washit script event and handles HP/stamina
 * drain or death. Returns 0 when target died, 1 otherwise.
 */
int
Combat_DamageResolve(CMobile *attacker, CMobile *target, int damage, CItem *weapon, int flag)
{
	int originalDamage;
	int prevHP;
	int attackerIsPlayer;
	int targetIsPlayer;

	if (damage < 0)
		damage = 0;
	originalDamage = damage;

	if (target == NULL)
		return 0;
	if (target->container.item.parent != NULL)
		return 0;

	if (VT_IsDead((CItem *)target))
		return 0;

	targetIsPlayer = VT_IsPlayer((CItem *)target);
	if (targetIsPlayer) {
		if (CPlayer_HasDeadFlag((CPlayer *)target))
			return 0;
	}

	attackerIsPlayer = 0;
	if (attacker != NULL) {
		attackerIsPlayer = VT_IsPlayer((CItem *)attacker);
		if (attackerIsPlayer) {
			if (CPlayer_HasDeadFlag((CPlayer *)attacker))
				return 0;
		}
	}

	if (CMobile_IsInvulnerable(target)) {
		if (attacker != NULL && VT_IsPlayer((CItem *)attacker)) {
			CString str;
			CString_DefaultConstructor(&str);
			((void (*)(void *, CString *))VT_FN((CItem *)target, VT_PAPERDOLL_TITLE))(target, &str);
			CString_AppendCStr(&str, " can not be harmed.");
			CPlayer_SystemMessage((CPlayer *)attacker, CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
		return 1;
	}

	if (attacker != NULL && VT_IsHidden((CItem *)attacker))
		((void (*)(void *, int))VT_FN((CItem *)attacker, VT_SET_HIDDEN))(attacker, 0);

	{
		CItem *armorAttacker = NULL;
		if (attacker != NULL && VT_IsMobile((CItem *)attacker))
			armorAttacker = (CItem *)attacker;

		if (damage != 0) {
			Combat_CalcArmorAbsorption(armorAttacker, (CItem *)target, &weapon, &damage, flag);
		}
	}

	prevHP = (int)CMobile_GetHp(target);

	damage = damage / 2;

	if (originalDamage > 0 && damage < 1)
		damage = 1;

	// Fire the washit script event (7); a script intRet() overrides
	// damage. Re-resolve target/attacker after the call in case the
	// script deletes them.
	{
		int savedRetVal = g_ScriptReturnValue;
		int savedRetFlag = g_ScriptReturnFlag;
		uint32_t targetSerial = target->container.item.serial;
		uint32_t attackerSerial = attacker ? attacker->container.item.serial : 0;

		g_ScriptReturnValue = 0;
		g_ScriptReturnFlag = 0;

		Entity_ExecuteEvent((CEntity *)target, 7, attackerSerial, (uint32_t)damage);

		if (g_ScriptReturnFlag == 1)
			damage = g_ScriptReturnValue;

		g_ScriptReturnValue = savedRetVal;
		g_ScriptReturnFlag = savedRetFlag;

		{
			CItem *tent = CWorld_FindBySerial(g_World, targetSerial);
			if (tent != (CItem *)target)
				return 1;
		}

		{
			CItem *aent = CWorld_FindBySerial(g_World, attackerSerial);
			attacker = (CMobile *)aent;
		}
	}

	if (damage > 0)
		((void (*)(CItem *, CMobile *, int))VT_FN((CItem *)target, VT_NOTIFY_DAMAGE))((CItem *)target, attacker, 1);

	if (prevHP - damage < 0) {
		// TARGET KILLED
		uint32_t tgtSer = target->container.item.serial;
		uint32_t atkSer;

		if (attacker != NULL)
			atkSer = attacker->container.item.serial;
		else
			atkSer = 0;

		if (attacker != NULL) {
			char *killRet;
			CItem *tmp;

			killRet = Entity_ExecuteEvent(&attacker->container.item.resourceEntity.entity, 2, tgtSer);

			attacker = (CMobile *)CWorld_FindBySerial(g_World, atkSer);

			if (killRet == NULL) {
				tmp = CWorld_FindBySerial(g_World, tgtSer);
				if (tmp == NULL || !VT_IsMobile(tmp) || tmp != (CItem *)target)
					return 0;
				((uint32_t (*)(void *, int, int))VT_FN((CItem *)target, VT_SET_HP))(target, 1, 0);
				return 1;
			}
		}

		{
			CItem *tgtRe = CWorld_FindBySerial(g_World, tgtSer);
			if (tgtRe == NULL || !VT_IsMobile(tgtRe) || tgtRe != (CItem *)target)
				return 0;
		}

		((uint32_t (*)(void *, int, int))VT_FN((CItem *)target, VT_SET_HP))(target, prevHP - damage, 0);

		if (attacker != NULL && target != NULL) {
			int victimFame, karmaGain;

			victimFame = CMobile_GetAdjFame(target);
			karmaGain = 0;
			if (!VT_IsPlayer((CItem *)target))
				karmaGain = -CMobile_GetAdjKarma(target);

			((void (*)(void *, int, int))VT_FN((CItem *)attacker, VT_FAME_KARMA_CHANGE))(attacker, victimFame, karmaGain);
		}

		if (attacker != NULL && VT_IsPlayer((CItem *)attacker) && target != NULL && VT_IsPlayer((CItem *)target)) {
			if (CMobile_GetNotoLevel(target) >= 0) {
				void *scriptClass = CScriptManager_FindOrLoad(&g_ScriptManager, "remnotor");
				if (scriptClass == NULL || !CResourceEntity_HasScriptClass((CItem *)target, scriptClass)) {
					int kills = 0;
					CString killsKey;
					CItem_GetTagInt((CItem *)attacker, "goodAndNeutralPlayerKills", &kills);
					kills++;
					CString_Constructor(&killsKey, "goodAndNeutralPlayerKills");
					ObjVar_SetStr((CItem *)attacker, &killsKey, 0, (uint32_t)kills);
				}
			}
		}

		{
			CMobile *attackerMob = NULL;
			if (attacker != NULL && VT_IsMobile((CItem *)attacker))
				attackerMob = attacker;

			((void (*)(void *, void *, int))VT_FN((CItem *)target, VT_ON_DEATH_WRAP))(target, attackerMob, 1);
		}

		if (VT_IsPlayer((CItem *)target))
			return 1;

		if (target != NULL)
			((void (*)(void *))VT_FN((CItem *)target, VT_DELETE))(target);
		return 0;
	}

	if (prevHP != 0) {
		int staminaDrain;
		int curStamina;

		curStamina = (int)CMobile_GetStamina(target);
		staminaDrain = curStamina * (prevHP - damage) / prevHP + 5;

		if ((int)CMobile_GetStamina(target) > staminaDrain) {
			((uint32_t (*)(void *, int))VT_FN((CItem *)target, VT_SET_STAMINA))(target, staminaDrain);
		}
	}

	((uint32_t (*)(void *, int, int))VT_FN((CItem *)target, VT_SET_HP))(target, prevHP - damage, 0);

	return 1;
}

/*
 * 0x00445901 - CMobile::GetWeaponSpeed
 *
 * adjusted = (DEX + 100) * weaponSpeed; returns 20 for the fastest
 * possible swing, otherwise 40000 / adjusted.
 */
int
CMobile_GetWeaponSpeed(CMobile *mob)
{
	CItem *weapon;
	int speed;
	int dex;
	int adjusted;

	weapon = CMobile_GetWeapon(mob);
	if (weapon != NULL)
		speed = CItem_GetSpeed(weapon) & 0xFF;
	else
		speed = 50;

	dex = (int)(uint16_t)mob->baseDex;
	adjusted = (dex + 100) * speed;

	if (adjusted < 2000)
		return 20;
	return 40000 / adjusted;
}

/*
 * 0x0044596D - CMobile::GetSwingState
 *
 * Returns the mobile's current swingState value.
 */
int
CMobile_GetSwingState(CMobile *this)
{
	return (int)this->swingState;
}

/*
 * 0x00445981 - CMobile::AdvanceSwingState
 *
 * Advances the idle / wind-up / swing phases based on swingProgress and
 * thresholds derived from GetWeaponSpeed. Returns 3 when a full swing
 * completed and the state reset to idle, otherwise the current state.
 */
int
CMobile_AdvanceSwingState(CMobile *mob)
{
	CItem *weapon;
	int totalDelay;
	int minDelay;
	int windUpDelay;
	int windUpThreshold;

	weapon = CMobile_GetWeapon(mob);
	totalDelay = CMobile_GetWeaponSpeed(mob) * 2;

	if (weapon != NULL && CItem_IsRangedWeapon(weapon))
		minDelay = 4;
	else
		minDelay = 6;

	windUpDelay = totalDelay > minDelay ? totalDelay - minDelay : 0;
	windUpThreshold = windUpDelay > 4 ? windUpDelay - 4 : 0;

	switch (mob->swingState) {
	case 0:
		if ((int)mob->swingProgress >= windUpThreshold) {
			mob->swingState = 1;
			mob->swingProgress = (uint32_t)windUpThreshold;
		}
		break;
	case 1:
		if ((int)mob->swingProgress >= windUpDelay) {
			mob->swingState = 2;
			mob->swingProgress = (uint32_t)windUpDelay;
		}
		break;
	default:
		if ((int)mob->swingProgress > totalDelay) {
			mob->swingState = 0;
			mob->swingProgress = 0;
			return 3;
		}
		break;
	}
	return (int)mob->swingState;
}

/*
 * 0x00445AB0 - CMobile::SetSwingState
 *
 * state=0 resets the swing. state>0 jumps to state-1 and re-evaluates
 * thresholds via AdvanceSwingState.
 */
void
CMobile_SetSwingState(CMobile *mob, int state)
{
	if (state == 0) {
		mob->swingProgress = 0;
		mob->swingState = 0;
	} else {
		mob->swingState = (uint32_t)(state - 1);
		CMobile_AdvanceSwingState(mob);
	}
}

/*
 * 0x00445AF5 - CMobile::SwingResolve
 *
 * Per-tick combat step: picks a target, checks range, advances the swing
 * state machine, rolls to hit, and resolves damage.
 */
void
CMobile_SwingResolve(CMobile *mob)
{
	uint32_t targetSerial;
	CMobile *target;
	CItem *weapon;
	int oldSwingState;
	int newSwingState;
	int weaponRange;
	int distance;
	int attackerSkillId;
	int defenderSkillId;
	int attackSkill;
	int defenseSkill;
	int hitChance;
	int damage;
	int roll;
	uint32_t attackerSerial;
	uint32_t tgtSerial;
	uint32_t weaponSerial;
	CLocation *attackerLoc;
	StdPtrNode *resultIter, *endTemp;
	StdPtrNode *eraseResult;

	if (mob->swingProgress < 1000)
		mob->swingProgress++;

	if (VT_IsDead((CItem *)mob))
		return;

	if (VT_IsPlayer((CItem *)mob)) {
		if (CPlayer_HasDeadFlag((CPlayer *)mob))
			return;
	}

	StdPtrIter_BaseConstructor(&resultIter);

	if (((int (*)(void *))VT_FN((CItem *)mob, VT_CHECK_EC))(mob)) {
		attackerLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)mob, VT_GET_LOCATION))(mob);
		CSerialList_FindNearestMobile((StdPtrList *)&mob->combatTargetList, &resultIter, attackerLoc);
	} else {
		attackerLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)mob, VT_GET_LOCATION))(mob);
		CSerialList_FindNearestTarget((StdPtrList *)&mob->combatTargetList, &resultIter, attackerLoc);
	}

	if (StdPtrIter_Eq(&resultIter, StdPtrList_End((StdPtrList *)&mob->combatTargetList, &endTemp)) & 0xFF) {
		if (VT_IsPlayer((CItem *)mob)) {
			CPlayer *pl = (CPlayer *)mob;
			if (pl->combatTargetSerial != 0)
				CPlayer_SetFightTarget(pl, 0);
		}
		return;
	}
	targetSerial = CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&resultIter));

	target = (CMobile *)CWorld_FindBySerial(g_World, targetSerial);

	if (VT_IsPlayer((CItem *)mob)) {
		CPlayer *pl = (CPlayer *)mob;
		if (pl->combatTargetSerial != (uint32_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&resultIter)))
			CPlayer_SetFightTarget(pl, CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&resultIter)));
	}

	if (target == NULL || !VT_IsMobile((CItem *)target)) {
		if (VT_IsNPC((CItem *)mob))
			CNPC_ClearCombatTarget(mob, 0);
		StdSerialList_Erase((StdPtrList *)&mob->combatTargetList, &eraseResult, resultIter);
		if (VT_IsNPC((CItem *)mob) && mob->combatTargetList.count == 0)
			CMobile_ClearMobileFlag(mob, MobileFlag_WarMode);
		return;
	}

	if (CMobile_IsInvulnerable(target) || (VT_IsPlayer((CItem *)target) && CPlayer_HasDeadFlag((CPlayer *)target))) {
		StdSerialList_Erase((StdPtrList *)&mob->combatTargetList, &eraseResult, resultIter);
		if (VT_IsPlayer((CItem *)mob) && CMobile_IsInvulnerable(target)) {
			CString str;
			CString_DefaultConstructor(&str);
			((void (*)(void *, CString *))VT_FN((CItem *)target, VT_PAPERDOLL_TITLE))(target, &str);
			CString_AppendCStr(&str, " can not be harmed.");
			CPlayer_SystemMessage((CPlayer *)mob, CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
		if (VT_IsNPC((CItem *)mob) && mob->combatTargetList.count == 0)
			CMobile_ClearMobileFlag(mob, MobileFlag_WarMode);
		return;
	}

	// Binary does NOT return here if dead - continues to weapon/swing logic.
	if (VT_IsDead((CItem *)target)) {
		if (VT_IsNPC((CItem *)mob))
			CNPC_ClearCombatTarget(mob, 0);
	}

	weapon = CMobile_GetWeapon(mob);
	oldSwingState = CMobile_GetSwingState(mob);
	newSwingState = CMobile_AdvanceSwingState(mob);

	if (oldSwingState == newSwingState || newSwingState < 2)
		return;

	if (weapon == NULL) {
		weaponRange = 1;
	} else if (CItem_IsRangedWeapon(weapon)) {
		weaponRange = CItem_CalcEffectiveRange(weapon);
	} else {
		weaponRange = CItem_GetMeleeRange(weapon);
	}

	distance = CItem_MultiAwareDistance((CItem *)mob, (CItem *)target);

	if (!((int (*)(void *))VT_FN((CItem *)mob, VT_CHECK_EC))(mob)) {
		if (distance > weaponRange) {
			attackerSerial = CMobile_GetSerial(mob);
			tgtSerial = CMobile_GetSerial(target);
			Entity_ExecuteEvent(&mob->container.item.resourceEntity.entity, 0x29, tgtSerial);
			{
				CItem *aent = CWorld_FindBySerial(g_World, attackerSerial);
				if (aent != NULL && aent == (CItem *)mob)
					Entity_ExecuteEvent(&target->container.item.resourceEntity.entity, 0x2A, attackerSerial);
			}
			return;
		}
	}

	if (weapon != NULL) {
		if ((CItem_GetMinRange(weapon) & 0xFF) > 0 && distance < (CItem_GetMinRange(weapon) & 0xFF)) {
			attackerSerial = CMobile_GetSerial(mob);
			tgtSerial = CMobile_GetSerial(target);
			Entity_ExecuteEvent(&mob->container.item.resourceEntity.entity, 0x27, tgtSerial);
			{
				CItem *aent = CContainer_FindItemBySerial((CContainer *)mob, attackerSerial);
				if (aent != NULL)
					Entity_ExecuteEvent(&target->container.item.resourceEntity.entity, 0x28, attackerSerial);
			}
			return;
		}
	}

	if (!((int (*)(void *))VT_FN((CItem *)mob, VT_CHECK_EC))(mob)) {
		if (!CEntity_CanSee((CItem *)mob, (CItem *)target, 1))
			return;
	}

	if (newSwingState == 2) {
		if (!((int (*)(void *))VT_FN((CItem *)mob, VT_CHECK_EC))(mob)) {
			if (weapon != NULL && CItem_IsRangedWeapon(weapon)) {
				if (!Combat_ConsumeAmmo(mob, weapon)) {
					CMobile_SetSwingState(mob, 0);
					return;
				}
				if (Combat_CheckStrength(mob))
					return;
			}
		}
	}

	CMobile_AddToAttackerList(target, CMobile_GetSerial(mob));
	CSerialList_AddFromSet(&mob->combatTargetList, CMobile_GetSerial(target));

	if (newSwingState == 2) {
		Combat_PlaySwingAnimation(mob, weapon, target);
		if (weapon != NULL && CItem_IsRangedWeapon(weapon))
			Combat_PlayMeleeMissSfx((CItem *)mob, (CItem *)target, weapon);
		return;
	}

	attackerSkillId = CMobile_GetWeaponSkill(mob, weapon);
	defenderSkillId = CMobile_GetWeaponSkill(target, CMobile_GetWeapon(target));

	attackSkill = CMobile_GetSkillValue(mob, (int8_t)attackerSkillId, 0);
	defenseSkill = CMobile_GetSkillValue(target, (int8_t)defenderSkillId, 0);

	if (attackSkill > 1000)
		attackSkill = 1000;
	if (defenseSkill > 1000)
		defenseSkill = 1000;
	if (attackSkill < 1)
		attackSkill = 1;
	if (defenseSkill < 1)
		defenseSkill = 1;

	hitChance = (attackSkill + 500) * 50 / (defenseSkill + 500);
	if (hitChance < 2)
		hitChance = 2;

	if (VT_IsHidden((CItem *)mob))
		((void (*)(void *, int))VT_FN((CItem *)mob, VT_SET_HIDDEN))(mob, 0);

	roll = GetRandomRange(1, 100);

	if (roll >= hitChance)
		goto miss_handler;

	{
		int hitGainPct = VT_IsPlayer((CItem *)target) ? 50 : 100;

		CMobile_SkillCheck(mob, (int8_t)attackerSkillId, defenseSkill, 50, 1, hitGainPct, 1);

		damage = CMobile_CalcDamage(mob, weapon);

		CMobile_SkillCheck(mob, 0x1B, CMobile_GetSkillValue(target, 0x1B, 0), 50, 1, hitGainPct, 1);
	}

	tgtSerial = ((CItem *)target)->serial;
	weaponSerial = weapon != NULL ? weapon->serial : 0;
	attackerSerial = ((CItem *)mob)->serial;

	// Fire the ishitting (0x22) weapon event. Binary does not
	// save/restore the script-return globals; it zeroes them only when
	// the flag was set.
	if (weapon != NULL) {
		Entity_ExecuteEvent((CEntity *)weapon, 0x22, tgtSerial, (uint32_t)damage);

		{
			CItem *aent = CWorld_FindBySerial(g_World, attackerSerial);
			if (aent == NULL || aent != (CItem *)mob || !VT_IsMobile(aent))
				return;
		}
		{
			CItem *went = CWorld_FindBySerial(g_World, weaponSerial);
			if (went == NULL || went != weapon || !VT_IsWeapon(went))
				weapon = NULL;
		}
		{
			CItem *tent = CWorld_FindBySerial(g_World, tgtSerial);
			if (tent == NULL || tent != (CItem *)target || !VT_IsMobile(tent))
				return;
		}

		if (g_ScriptReturnFlag == 1) {
			damage = g_ScriptReturnValue;
			g_ScriptReturnFlag = 0;
			g_ScriptReturnValue = 0;
		}
	}

	{
		CItem *aent = CWorld_FindBySerial(g_World, attackerSerial);
		if (aent == NULL || aent != (CItem *)mob || !VT_IsMobile(aent))
			return;
	}
	{
		CItem *tent = CWorld_FindBySerial(g_World, tgtSerial);
		if (tent == NULL || tent != (CItem *)target || !VT_IsMobile(tent))
			return;
	}
	{
		CItem *went = CWorld_FindBySerial(g_World, weaponSerial);
		if (went == NULL || went != weapon || !VT_IsWeapon(went))
			weapon = NULL;
	}

	if (damage == 0)
		goto miss_handler;

	if (!Entity_ExecuteEvent((CEntity *)target, 0x43, attackerSerial, (uint32_t)damage))
		return;

	{
		CItem *aent = CWorld_FindBySerial(g_World, attackerSerial);
		if (aent == NULL || aent != (CItem *)mob || !VT_IsMobile(aent))
			return;
	}
	{
		CItem *tent = CWorld_FindBySerial(g_World, tgtSerial);
		if (tent == NULL || tent != (CItem *)target || !VT_IsMobile(tent))
			return;
	}
	{
		CItem *went = CWorld_FindBySerial(g_World, weaponSerial);
		if (went == NULL || went != weapon || !VT_IsWeapon(went))
			weapon = NULL;
	}

	Combat_PlayWeaponHitSfx((CItem *)mob, (CItem *)target, weapon);

	if (CMobile_GetSwingState(target) != 2) {
		Combat_HitEffects(mob, target, weapon);
	}

	if (Combat_DamageResolve(mob, target, damage, weapon, 0) == 0) {
		CSerialList_Remove(&mob->combatTargetList, tgtSerial);
		if (VT_IsNPC((CItem *)mob) && mob->combatTargetList.count == 0)
			CMobile_ClearMobileFlag(mob, MobileFlag_WarMode);
	}
	return;

miss_handler: {
	int missGainPct = VT_IsPlayer((CItem *)mob) ? 50 : 100;
	CMobile_SkillCheck(target, (int8_t)defenderSkillId, attackSkill, 50, 1, missGainPct, 1);
}

	if (weapon == NULL || !CItem_IsRangedWeapon(weapon))
		Combat_PlayMeleeMissSfx((CItem *)mob, (CItem *)target, weapon);
	Combat_PlayRangedMissEffects((CItem *)mob, (CItem *)target, weapon);
}

/*
 * 0x0044669C - Combat_BarkTaunt
 *
 * Speaks one of three taunts. The roll is GetRandomRange(0, 3), which
 * is inclusive, but the table at 0x00614F68 holds three pointers and a
 * null terminator - so one call in four says a null string.
 * Reproduced as it stands.
 */
static __attribute__((unused)) void
Combat_BarkTaunt(CItem *speaker)
{
	// 0x00614F68 - three taunts and a null terminator
	static const char *taunts[4] = {
		"Vultures will pick thy bones!",
		"Thou wilt regret ever meeting me.",
		"Die!",
		NULL,
	};
	const char *line;

	line = taunts[GetRandomRange(0, 3)];
	((void (*)(void *, const char *, int, int, int))VT_FN(speaker, VT_SAY_CSTRING))(speaker, line, -1, -1, -1);
}

/*
 * 0x004466D9 - CombatInitiate
 *
 * Starts combat between attacker and target: notifies the target of
 * damage, rejects the pairing when either is dead or target is
 * invulnerable, calls CombatManager::InitiateCombat when flag is set,
 * fires the target's ongetattacked (1) event, and updates the NPC AI
 * and attacker/target lists on both sides.
 */
void
CombatInitiate(CMobile *attacker, CMobile *target, int flag)
{
	CPlayer *attackerPlayer;
	uint32_t attackerSerial;
	uint32_t targetSerial;
	CItem *found;

	((void (*)(void *, CMobile *, int))VT_FN((CItem *)target, VT_NOTIFY_DAMAGE))(target, attacker, 0);

	if (VT_IsDead((CItem *)attacker))
		return;
	if (VT_IsDead((CItem *)target))
		return;

	if (VT_IsPlayer((CItem *)attacker)) {
		if (CPlayer_HasDeadFlag((CPlayer *)attacker))
			return;
	}
	if (VT_IsPlayer((CItem *)target)) {
		if (CPlayer_HasDeadFlag((CPlayer *)target))
			return;
	}

	if (CMobile_IsInvulnerable(target)) {
		if (VT_IsPlayer((CItem *)attacker)) {
			CString str;
			attackerPlayer = (CPlayer *)attacker;
			CString_DefaultConstructor(&str);
			((void (*)(void *, CString *))VT_FN((CItem *)target, VT_PAPERDOLL_TITLE))(target, &str);
			CString_AppendCStr(&str, " can not be harmed.");
			CPlayer_SystemMessage(attackerPlayer, CString_GetBuffer(&str));
			CString_Destructor(&str);
		}
		return;
	}

	if (attacker != NULL && target != NULL && flag != 0)
		CombatManager_InitiateCombat(attacker, target);

	CMobile_ClearCombatTargets(attacker);

	attackerSerial = CMobile_GetSerial(attacker);
	targetSerial = CMobile_GetSerial(target);

	if (CMobile_AddToCombatTargetList(attacker, targetSerial) == 1) {
		Entity_ExecuteEvent(&target->container.item.resourceEntity.entity, 1, attackerSerial);

		found = CWorld_FindBySerial(g_World, attackerSerial);
		if (found == NULL || found != (CItem *)attacker)
			return;
		if (!VT_IsMobile((CItem *)attacker))
			return;

		found = CWorld_FindBySerial(g_World, targetSerial);
		if (found == NULL || found != (CItem *)target)
			return;
		if (!VT_IsMobile(found))
			return;
	}

	if (VT_IsNPC((CItem *)attacker)) {
		CNPC *aNpc = (CNPC *)attacker;
		aNpc->aiState = NPC_STATE_ATTACK_TARGET;
		aNpc->actionTarget = targetSerial;
		aNpc->isWalking = 0;
	}

	if (VT_IsNPC((CItem *)target)) {
		CNPC *tNpc = (CNPC *)target;
		if (tNpc->aiState != NPC_STATE_ATTACK_TARGET) {
			tNpc->aiState = NPC_STATE_ATTACK_TARGET;
			tNpc->actionTarget = attackerSerial;
			tNpc->isWalking = 0;
		}
	}

	CMobile_AddToAttackerList(target, attackerSerial);

	if (!CSerialList_Contains(&target->combatTargetList, attackerSerial)) {
		CMobile_AddToCombatTargetList(target, attackerSerial);

		if (VT_IsNPC((CItem *)target))
			CMobile_NPC_AI_CombatStart(target);
	}
}

CCombatEventList g_CombatEventList; // 0x006990A8

/*
 * 0x004601A2 - CCombatEventList::ToggleGroupMember
 *
 * Adds or removes target from the group led by whoever currently has
 * player as a member. With no existing group, player targeting itself
 * forms one ("Group formed and member added"), and player targeting
 * someone else forms one and adds them both. Inside a group, a leader
 * targeting itself disbands it; a member targeting itself first drops
 * out of its old node. The toggle then reports "I've left a group." or
 * "I've joined a group." and both parties get a GROUPS packet.
 *
 * Called with either argument null it sends an empty GROUPS packet
 * instead - and dereferences player's vtable to do it, so a null player
 * faults. Always returns 0.
 */
static __attribute__((unused)) int
CCombatEventList_ToggleGroupMember(CCombatEventList *this, CMobile *player, CMobile *target)
{
	uint8_t obuf[12];
	CMobile *leader;
	CCombatEventNode *node;

	if (player == NULL || target == NULL) {
		if (((int (*)(void *))VT_FN((CItem *)player, VT_IS_PLAYER))(player)) {
			PacketManager_MakePacket_GROUPS(obuf, 0, 0);
			SendToClient((CItem *)player, obuf, -1);
		}
		return 0;
	}

	if (!((int (*)(void *))VT_FN((CItem *)player, VT_IS_PLAYER))(player))
		return 0;

	leader = CCombatEventList_FindAttackerOf(this, player);

	if (leader == NULL) {
		if (player == target) {
			CCombatEventList_AddNode(this, player, "Bad doodz");
			CCombatEventList_ToggleSub(this, player, player);
			Entity_SendSystemMessage((CItem *)player, player->container.item.serial, "Group formed and member added");
			PacketManager_MakePacket_GROUPS(obuf, player->container.item.serial, target->container.item.serial);
			SendToClient((CItem *)player, obuf, -1);
			return 0;
		}

		CCombatEventList_AddNode(this, player, "Bad doodz");
		CCombatEventList_ToggleSub(this, player, player);
		CCombatEventList_ToggleSub(this, player, target);
	} else {
		if (leader == player) {
			if (player == target) {
				node = CCombatEventList_FindByMob(this, player);
				CCombatEventNode_RemoveSub(node, player);
				CCombatEventList_RemoveNode(this, player);
				((void (*)(void *, const char *, int, int, int))VT_FN((CItem *)player, VT_SAY_CSTRING))(player, "Group disbanded!", -1, -1, -1);
				return 0;
			}
		} else if (player == target) {
			node = CCombatEventList_FindByMob(this, leader);
			CCombatEventNode_RemoveSub(node, player);
		}

		if (CCombatEventList_ToggleSub(this, leader, target)) {
			Entity_SendSystemMessage((CItem *)target, target->container.item.serial, "I've left a group.");
			return 0;
		}

		Entity_SendSystemMessage((CItem *)target, target->container.item.serial, "I've joined a group.");
	}

	PacketManager_MakePacket_GROUPS(obuf, player->container.item.serial, target->container.item.serial);
	SendToClient((CItem *)player, obuf, -1);
	return 0;
}

/*
 * 0x004603B9 - CCombatEventList::AddNode
 *
 * Adds a new CCombatEventNode(mob, name) to the list. Returns 0 when the
 * list was empty and the node becomes the head, or -1 when the node is
 * appended after an existing tail. -1 is also returned, with the node
 * left unlinked, if the 10000-iteration walk guard trips.
 */
int
CCombatEventList_AddNode(CCombatEventList *list, CMobile *mob, const char *name)
{
	CCombatEventNode *node;
	CCombatEventNode *walk;
	int count;

	node = OperatorNew(sizeof(CCombatEventNode));
	if (node != NULL)
		CCombatEventNode_Constructor(node, mob, name);

	if (list->head == NULL) {
		list->head = node;
		list->head->prev = NULL;
		list->head->next = NULL;
		return 0;
	}

	walk = list->head;
	count = 0;
	while (walk->next != NULL) {
		walk = walk->next;
		count++;
		if (count > 10000)
			return -1;
	}
	walk->next = node;
	node->prev = walk;
	return -1;
}

/*
 * 0x004604AB - CCombatEventList::RemoveNode
 *
 * Removes and frees the node for mob. Returns 0 on success, -1 if not
 * found.
 */
int
CCombatEventList_RemoveNode(CCombatEventList *list, CMobile *mob)
{
	CCombatEventNode *walk, *next;

	walk = list->head;
	while (walk != NULL) {
		next = walk->next;
		if (walk->mob == mob) {
			if (walk->prev != NULL)
				walk->prev->next = walk->next;
			else
				list->head = walk->next;
			if (walk->next != NULL)
				walk->next->prev = walk->prev;
			CCombatEventNode_ScalarDelete(walk, 1);
			return 0;
		}
		walk = next;
	}
	return -1;
}

/*
 * 0x00460559 - CCombatEventList::ToggleSub
 *
 * Adds or removes targetMob under ownerMob's sub-list, returning 0 on
 * add, 1 on remove and -1 when ownerMob is not found.
 */
int
CCombatEventList_ToggleSub(CCombatEventList *list, CMobile *ownerMob, CMobile *targetMob)
{
	CCombatEventNode *node;
	CMobile *found;

	node = CCombatEventList_FindByMob(list, ownerMob);
	if (node == NULL)
		return -1;

	found = CCombatEventList_FindSubByMob(node, targetMob);
	if (found == NULL) {
		CCombatEventNode_AddSub(node, targetMob);
		return 0;
	}
	CCombatEventNode_RemoveSub(node, targetMob);
	return 1;
}

/*
 * 0x004605B8 - CCombatEventList::FindByMob
 *
 * Linear scan of the combat event list for a node whose mob matches,
 * or NULL when absent.
 */
CCombatEventNode *
CCombatEventList_FindByMob(CCombatEventList *list, CMobile *mob)
{
	CCombatEventNode *walk;
	CCombatEventNode *next;

	walk = list->head;
	while (walk != NULL) {
		next = walk->next;
		if (walk->mob == mob)
			return walk;
		walk = next;
	}
	return NULL;
}

/*
 * 0x004605FA - CCombatEventList::RemoveSubByOwner
 *
 * Removes targetMob from ownerMob's sub-list. Returns 0 on success,
 * -1 when ownerMob is not in the list.
 */
int
CCombatEventList_RemoveSubByOwner(CCombatEventList *list, CMobile *ownerMob, CMobile *targetMob)
{
	CCombatEventNode *walk, *next;

	walk = list->head;
	while (walk != NULL) {
		next = walk->next;
		if (walk->mob == ownerMob) {
			CCombatEventNode_RemoveSub(walk, targetMob);
			return 0;
		}
		walk = next;
	}
	return -1;
}

/*
 * 0x00460648 - CCombatEventList::FindAttackerOf
 *
 * Returns the outer combatant whose sub-list contains mob, or NULL.
 */
CMobile *
CCombatEventList_FindAttackerOf(CCombatEventList *list, CMobile *mob)
{
	CCombatEventNode *walk;
	CCombatEventNode *next;

	if (list->head == NULL)
		return NULL;
	walk = list->head;
	while (walk != NULL) {
		next = walk->next;
		if (CCombatEventList_FindSubByMob(walk, mob) != NULL)
			return walk->mob;
		walk = next;
	}
	return NULL;
}

/*
 * 0x004606A3 - CCombatEventList::RemoveMob
 *
 * Removes every reference to mob, whether as an outer node or as a
 * sub-entry under another combatant.
 */
void
CCombatEventList_RemoveMob(CCombatEventList *list, CMobile *mob)
{
	CCombatEventNode *walk, *next;
	CCombatSubNode *sub, *subNext;

	walk = list->head;
	while (walk != NULL) {
		next = walk->next;
		if (walk->mob == mob) {
			CCombatEventList_RemoveNode(list, mob);
		} else {
			sub = walk->subHead;
			while (sub != NULL) {
				subNext = sub->next;
				if (sub->mob == mob) {
					CCombatEventList_RemoveSubByOwner(list, walk->mob, sub->mob);
				}
				sub = subNext;
			}
		}
		walk = next;
	}
}

/*
 * 0x0046072D - CCombatEventNode::CCombatEventNode
 *
 * Initializes a combat event node with the event name and mob owner,
 * clearing the sub-list and linked-list pointers.
 */
static CCombatEventNode *
CCombatEventNode_Constructor(CCombatEventNode *node, CMobile *mob, const char *name)
{
	strcpy(node->name, name);
	node->mob = mob;
	node->subHead = NULL;
	node->next = NULL;
	node->prev = NULL;
	return node;
}

/*
 * 0x00460776 - CCombatEventNode::~CCombatEventNode
 *
 * No-op destructor: the sub-list walker frees the sub-nodes separately.
 */
static void
CCombatEventNode_Destructor(CCombatEventNode *node)
{
	USED(node);
}

/*
 * 0x00460781 - CCombatEventNode::FindSubByMob
 *
 * Returns the matching sub-node's mob, or NULL. The binary caps the
 * search at 10 iterations as a safety guard.
 */
static CMobile *
CCombatEventList_FindSubByMob(CCombatEventNode *node, CMobile *mob)
{
	CCombatSubNode *walk;
	CCombatSubNode *next;
	int count;

	if (node->subHead == NULL)
		return NULL;

	count = 0;
	walk = node->subHead;
	while (walk != NULL) {
		count++;
		if (count > 10)
			return NULL;
		next = walk->next;
		if (walk->mob == mob)
			return walk->mob;
		walk = next;
	}
	return NULL;
}

/*
 * 0x004607EB - CCombatEventNode::AddSub
 *
 * Appends a new sub-node tracking mob.
 */
static int
CCombatEventNode_AddSub(CCombatEventNode *node, CMobile *mob)
{
	CCombatSubNode *sub;
	CCombatSubNode *walk;

	sub = OperatorNew(sizeof(CCombatSubNode));
	if (sub != NULL)
		sub = CCombatSubNode_Constructor(sub);
	else
		sub = NULL;

	sub->mob = mob;
	if (node->subHead == NULL) {
		node->subHead = sub;
		return 0;
	}

	walk = node->subHead;
	while (walk->next != NULL)
		walk = walk->next;
	walk->next = sub;
	sub->prev = walk;
	return 0;
}

/*
 * 0x004608A6 - CCombatEventNode::RemoveSub
 *
 * Splices the sub-node for mob out of the list. Returns 0 on hit,
 * -1 on miss. Matches the binary by not freeing the removed node.
 */
int
CCombatEventNode_RemoveSub(CCombatEventNode *node, CMobile *mob)
{
	CCombatSubNode *walk, *next;

	if (node->subHead == NULL)
		return -1;

	walk = node->subHead;
	while (walk != NULL) {
		next = walk->next;
		if (walk->mob == mob) {
			if (walk->prev != NULL)
				walk->prev->next = walk->next;
			if (walk->next != NULL)
				walk->next->prev = walk->prev;
			if (node->subHead == walk)
				node->subHead = walk->next;
			return 0;
		}
		walk = next;
	}
	return -1;
}

/*
 * 0x00460934 - CCombatSubNode::CCombatSubNode
 *
 * Zero-initializes a combat sub-node.
 */
static CCombatSubNode *
CCombatSubNode_Constructor(CCombatSubNode *node)
{
	node->mob = NULL;
	node->next = NULL;
	node->prev = NULL;
	return node;
}

/*
 * 0x00460970 - CCombatEventNode::`scalar deleting destructor'
 *
 * Scalar deleting destructor: runs the destructor and frees the node when
 * flags & 1.
 */
static __attribute__((unused)) void *
CCombatEventNode_ScalarDelete(CCombatEventNode *node, int flags)
{
	CCombatEventNode_Destructor(node);
	if (flags & 1)
		OperatorDelete(node);
	return NULL;
}

/*
 * Crime witness system - tables driving ProcessCrimeWitness (0x00460C62).
 *
 * Each crime type maps to: broadcast range, an array of NPC shout phrases,
 * and printf-style templates used for player system messages with/without
 * a named victim.
 */

/*
 * 0x004609A0 - CDataBuffer::CDataBuffer
 *
 * Allocates a 1 KiB data buffer with len=0 and cap=0x400.
 */
void
CDataBuffer_Constructor(CDataBuffer *b)
{
	b->data = OperatorNew(0x400);
	b->len = 0;
	b->cap = 0x400;
}

/*
 * 0x004609D4 - CDataBuffer::~CDataBuffer
 *
 * Frees the buffer's underlying heap allocation.
 */
void
CDataBuffer_Destructor(CDataBuffer *b)
{
	OperatorDelete(b->data);
}

/*
 * 0x004609F5 - CDataBuffer::Append
 *
 * FIXED: the binary's doubling loop uses a signed int shift; once cap
 * reaches 0x80000000 it wraps negative, the jle stays true, the next
 * shift yields 0 and the loop spins forever. Capped at 0x40000000 (1 GB)
 * to bail out gracefully.
 */
void
CDataBuffer_Append(CDataBuffer *b, const void *src, int srcLen)
{
	if (b->len + srcLen > b->cap) {
		while (b->len + srcLen > b->cap) {
			if (b->cap >= 0x40000000) {
				fprintf(stderr, "CDataBuffer_Append: cap overflow (len=%d srcLen=%d cap=%d)\n", b->len, srcLen, b->cap);
				return;
			}
			b->cap <<= 1;
		}
		uint8_t *newbuf = OperatorNew(b->cap);
		memcpy(newbuf, b->data, b->len);
		OperatorDelete(b->data);
		b->data = newbuf;
	}
	memcpy(b->data + b->len, src, srcLen);
	b->len += srcLen;
}

// 0x006197D8 - Crime type 0 phrases: Murder/Attack (21 entries)
static const char *g_CrimePhrases_Murder[] = {
	"A murderer!",
	"Help, a murderer!",
	"They're killing people here!",
	"'Tis a bloodbath!",
	"Save us!",
	"Protect me!",
	"Ahh!",
	"Murder is being done!",
	"Vile murderer!",
	"'Tis awful! Death! Ah!",
	"Help! Murder!",
	"A scoundrel is committing murder!",
	"Help! They're dying!",
	"They're dying! Come quickly!",
	"Help! Come quickly!",
	"Hurry, 'tis a murder!",
	"Make haste!",
	"Make haste, 'tis a murder!",
	"A crime! A murder!",
	"I pay my taxes, and no guards?",
	"Where are the guards! Help!",
};

// 0x00619830 - Crime type 1 phrases: Theft (21 entries)
static const char *g_CrimePhrases_Theft[] = {
	"A thief!",
	"Help, a thief!",
	"They're stealing things here!",
	"'Tis a robbery!",
	"'Tis a crime!",
	"Stop them!",
	"Ahh!",
	"A theft is being committed!",
	"Pathetic thief!",
	"'Tis awful! A robbery! Ah!",
	"Help! Robbers!",
	"A scoundrel is committing robbery!",
	"Help! They're stealing!",
	"They're stealing! Come quickly!",
	"Help! Come quickly!",
	"Hurry, 'tis a theft!",
	"Make haste!",
	"Make haste, 'tis a robbery!",
	"A crime! A thief!",
	"I pay my taxes, and no guards?",
	"Where are the guards! Help!",
};

// 0x00619888 - Crime type 2 phrases: Snooping/Peek (12 entries)
static const char *g_CrimePhrases_Snoop[] = {
	"Stop that suspicious scoundrel!",
	"Help, a rogue!",
	"That's susposed to be private!",
	"'Tis a crime!",
	"Stop them!",
	"Ahh!",
	"Come quickly!",
	"Make haste!",
	"A crime!",
	"Where art thou?Where art thou when needed?",
	"I pay my taxes, and no guards?",
	"Where are the guards! Help!",
};

// 0x006198B8 - Crime type 3 phrases: Provocation (20 entries)
static const char *g_CrimePhrases_Provoke[] = {
	"They're starting a fight!",
	"Help, a rogue bard!",
	"They're trying to start a riot!",
	"'Tis a riot!",
	"'Tis a crime!",
	"Stop them!",
	"Ahh!",
	"They're inciting a riot!",
	"A renegade musician!",
	"'Tis awful! Provoking a fight! Ah!",
	"Help! 'Twill be a bloodbath!",
	"A scoundrel is talking folk into fighting!",
	"Help! They're about to fight!",
	"They're going to kill each other! Come quickly!",
	"Help! Come quickly!",
	"Hurry, they're provoking a fight!",
	"Make haste!",
	"Make haste, 'tis a riot!",
	"A crime! A fight is being provoked!",
	"Where are the guards! Help!",
};

// 0x00619908 - Crime type 4 phrases: Generic (10 entries)
static const char *g_CrimePhrases_Generic[] = {
	"Help!",
	"'Tis a crime!",
	"Stop them!",
	"Ahh!",
	"Come quickly!",
	"Make haste!",
	"A crime!",
	"Where art thou?Where art thou when needed?",
	"I pay my taxes, and no guards?",
	"Where are the guards! Help!",
};

/*
 * One row of the five-entry witness-phrase table at 0x00619930, selecting
 * the witness range, the random shout list, and the sysmessage formats
 * used when a bystander reports the crime.
 */
struct CrimeType {
	int range;
	int numPhrases;
	const char **phrases;
	const char *fmtNoVictim;
	const char *fmtWithVictim;
	const char *fmtYouVictim;
};

// 0x00619930 - five-entry witness-phrase table (range, count, phrases, fmts)
static const struct CrimeType g_CrimeTypes[5] = {
	// 0: Murder/Attack - range 9, 21 phrases
	{ 9, 21, g_CrimePhrases_Murder, NULL, "You see %s attacking %s!", NULL },
	// 1: Theft - range 7, 21 phrases
	{ 7, 21, g_CrimePhrases_Theft, "You notice %s trying to steal something.", "You notice %s trying to steal from %s.", "You notice %s attempting to steal from you!" },
	// 2: Snooping - range 4, 12 phrases
	{ 4, 12, g_CrimePhrases_Snoop, NULL, "You notice %s trying to peek into %s's belongings.", "You notice %s peeking into your belongings!" },
	// 3: Provocation - range 9, 20 phrases
	{ 9, 20, g_CrimePhrases_Provoke, NULL, "You notice %s provoking %s to attack.", NULL },
	// 4: Generic - range 7, 10 phrases
	{ 7, 10, g_CrimePhrases_Generic, "You notice %s committing a crime.", "You notice %s doing something illegal to %s.", "You notice %s doing something illegal to you." }
};

/*
 * 0x00460BA5 - CTerrainLookup::CTerrainLookup
 *
 * Zero-initializes a CTerrainLookup's count and field_804.
 */
void
CTerrainLookup_Init(CTerrainLookup *this)
{
	this->count = 0;
	this->field_804 = 0;
}

/*
 * 0x00460BC9 - CTerrainLookup::~CTerrainLookup
 *
 * No-op destructor: the struct owns nothing that needs releasing.
 */
void
CTerrainLookup_Destructor(CTerrainLookup *this)
{
	USED(this);
}

/*
 * 0x00460BD4 - ScoreWitness (witness scoring helper)
 *
 * Returns a proximity-based score for how well a witness notices a crime.
 * Score 0 = not a witness. Higher = more likely to detect.
 * The victim always gets score 10.
 *
 * Binary checks CMobile_IsCreatureBody (0x004AB24F) on witness - must have
 * valid creature body type. After distance scoring, adds +2 if
 * CanSee(witness, criminal, 1) returns true (LOS check at 0x0046B1E7).
 */
static int
ScoreWitness(CMobile *witness, CLocation *crimeLoc, CMobile *criminal, CMobile *victim, int range)
{
	int dist, score;

	if (witness == NULL || witness == criminal)
		return 0;
	if (!CMobile_IsCreatureBody(witness))
		return 0;

	if (witness == victim)
		return 10;

	dist = Location_WrappedChebyshevDistance(crimeLoc, CEntity_GetLocation(&witness->container.item.resourceEntity.entity));

	score = range / 3 + 1 - dist / 3;

	if (CEntity_CanSee(&witness->container.item, &criminal->container.item, 1))
		score += 2;

	return score;
}

/*
 * 0x00460C62 - CombatManager::ProcessCrimeWitness
 *
 * Scans nearby NPCs and players as witnesses to a crime. NPC witnesses
 * shout (prefixing "Guards!" inside guard zones); player witnesses get
 * system messages. Flags the perpetrator criminal and calls guards if
 * any witness detected the act. Returns the accumulated witness score.
 */
int
ProcessCrimeWitness(CLocation *crimeLoc, CMobile *criminal, CMobile *victim, const char *callbackName, int delay, int priority, int crimeType)
{
	const struct CrimeType *ct;
	int inGuardZone;
	uint8_t calledGuards;
	uint8_t criedOut;
	int totalScore;
	int chance;
	int score;
	int npcAwareness;
	char buf[256];
	CVector npcList;
	CVector playerList;
	char npcTypeFlag;
	char playerTypeFlag;
	uintptr_t *iter;
	CMobile *witness;
	CItem *playerEnt;
	const char *phrase;
	const char *criminalName;

	AggroInfo aggroInfo;

	if (victim != NULL) {
		if (((int (*)(void *, void *))VT_FN((CItem *)victim, VT_CAN_BE_AGGRESSED))(victim, criminal))
			priority = 0;
	}

	ct = &g_CrimeTypes[crimeType];

	if (!CItem_GetTagInt((CItem *)criminal, "npcAwareness", &npcAwareness))
		npcAwareness = 100;

	inGuardZone = !IsInDungeonRange(crimeLoc);

	chance = delay * npcAwareness / 100;
	if (chance < 1)
		return 0;

	calledGuards = 0;
	criedOut = 0;
	totalScore = 0;

	if (priority > 0) {
		npcTypeFlag = 0;
		CVector_Constructor(&npcList, &npcTypeFlag);

		CEntityMap_RangeQuery(g_NPCMap, &npcList, crimeLoc->x, crimeLoc->y, ct->range);

		Vector_SortByDistPair((uintptr_t *)npcList.begin, (uintptr_t *)npcList.end, *crimeLoc);

		for (iter = (uintptr_t *)npcList.begin; iter != (uintptr_t *)npcList.end; iter++) {
			witness = (CMobile *)*iter;

			score = ScoreWitness(witness, crimeLoc, criminal, victim, ct->range);
			totalScore += score;
			if (score == 0)
				continue;

			if (criedOut & 0xFF)
				continue;

			if (GetRandom(1000) >= chance * score)
				continue;

			phrase = ct->phrases[GetRandom(ct->numPhrases)];

			if (inGuardZone) {
				sprintf(buf, "Guards! %s", phrase);
				((void (*)(void *, const char *, int, int, int))VT_FN((CItem *)witness, VT_EMOTE_CSTRING))(witness, buf, -1, -1, -1);
			} else {
				((void (*)(void *, const char *, int, int, int))VT_FN((CItem *)witness, VT_EMOTE_CSTRING))(witness, phrase, -1, -1, -1);
			}

			calledGuards = 1;
			criedOut = 1;
		}

		CVector_Destructor(&npcList);
	}

	playerTypeFlag = 1;
	CVector_Constructor(&playerList, &playerTypeFlag);

	CEntityMap_RangeQuery(g_ItemMap, &playerList, crimeLoc->x, crimeLoc->y, ct->range);

	for (iter = (uintptr_t *)playerList.begin; iter != (uintptr_t *)playerList.end; iter++) {
		playerEnt = (CItem *)*iter;

		score = ScoreWitness((CMobile *)playerEnt, crimeLoc, criminal, victim, ct->range);
		totalScore += score;
		if (score == 0)
			continue;

		if (GetRandom(1000) >= chance * score)
			continue;

		if (victim != NULL) {
			if (!VT_IsMobile((CItem *)victim))
				goto no_victim_msg;
		}

		if (victim == NULL) {
no_victim_msg:
			if (ct->fmtNoVictim != NULL) {
				criminalName = ((const char *(*)(void *))VT_FN((CItem *)criminal, VT_GET_NAME))(criminal);
				sprintf(buf, ct->fmtNoVictim, criminalName, callbackName);
				CPlayer_SystemMessage((CPlayer *)playerEnt, buf);
			}
		} else if (victim == (CMobile *)playerEnt) {
			if (ct->fmtYouVictim != NULL) {
				criminalName = ((const char *(*)(void *))VT_FN((CItem *)criminal, VT_GET_NAME))(criminal);
				sprintf(buf, ct->fmtYouVictim, criminalName, callbackName);
				CPlayer_SystemMessage((CPlayer *)playerEnt, buf);
			}
		} else {
			if (ct->fmtWithVictim != NULL) {
				const char *victimName;
				// Binary pushes victimName as 5th sprintf arg (unused
				// by the 2-%s format but present on the stack).
				victimName = ((const char *(*)(void *))VT_FN((CItem *)victim, VT_GET_NAME))(victim);
				criminalName = ((const char *(*)(void *))VT_FN((CItem *)criminal, VT_GET_NAME))(criminal);
				sprintf(buf, ct->fmtWithVictim, criminalName, callbackName, victimName);
				CPlayer_SystemMessage((CPlayer *)playerEnt, buf);
			}
		}

		if (priority > 0 && !(criedOut & 0xFF) && (playerTypeFlag & 0xFF))
			calledGuards = 1;
	}

	if (calledGuards & 0xFF) {
		CMobile_SetCriminalPunishable(criminal, crimeLoc, 480);
	}

	if (inGuardZone && (criedOut & 0xFF)) {
		CombatManager_CallGuards((CItem *)criminal, crimeLoc, (CItem *)victim, priority);

		AggroInfo_Constructor(&aggroInfo);
		((void (*)(void *, void *))VT_FN((CItem *)criminal, VT_FILL_AGGRO_INFO))(criminal, &aggroInfo);

		if (aggroInfo.entity != NULL) {
			CombatManager_CallGuards(aggroInfo.entity, crimeLoc, (CItem *)victim, priority);
		}
		AggroInfo_Destructor(&aggroInfo);
	}

	if (totalScore != 0) {
		CString _n;

		npcAwareness = npcAwareness * (totalScore * 3 + 100) / 100;
		if (npcAwareness > 1000)
			npcAwareness = 1000;
		CString_Constructor(&_n, "npcAwareness");
		ObjVar_SetStr(&criminal->container.item, &_n, 0, (uint32_t)npcAwareness);
		Entity_AttachScript(&criminal->container.item, "crimeaware", 1);
	}

	CVector_Destructor(&playerList);
	return totalScore;
}

/*
 * 0x0046121C - CombatManager::InitiateCombat
 *
 * Called by CombatInitiate when its flag arg is set. Emits PvP
 * "attacking you"/"attacking <name>" system messages, detects hostile
 * targets, and hands off to ProcessCrimeWitness to schedule guard
 * intervention. Returns the ProcessCrimeWitness score.
 */
int
CombatManager_InitiateCombat(CMobile *attacker, CMobile *target)
{
	int isHostile;
	uint8_t obuf[512];
	char buf[256];

	if (attacker == target)
		return 0;

	// vtable[0xEC] stub is always 0 in the demo; kept to match the binary.
	if (((int (*)(void *))VT_FN((CItem *)attacker, VT_CHECK_EC))(attacker))
		return 0;

	if (CSerialList_Contains(&attacker->attackerList, target->container.item.serial) == 1)
		return 0;

	if (CSerialList_Contains(&target->attackerList, attacker->container.item.serial) == 1)
		return 0;

	isHostile = CMobile_IsCreatureBody(target);
	if (!isHostile)
		isHostile = CMobile_HasBoss(target);
	if (!isHostile)
		return 0;

	if (VT_IsPlayer((CItem *)attacker) && CMobile_HasBoss(target))
		CPlayer_SystemMessage((CPlayer *)attacker, "Attacking a pet!");

	if (attacker != NULL && VT_IsPlayer((CItem *)attacker) && VT_IsPlayer((CItem *)target)) {
		char *name;

		name = ((char *(*)(void *))VT_FN((CItem *)attacker, VT_GET_NAME))(attacker);
		sprintf(buf, "%s is attacking you!", name);
		PacketManager_MakePacket_TEXT(obuf, (CItem *)target, (CItem *)target, 0, buf, 0x22, 3);
		SendToClient((CItem *)target, obuf, -1);

		name = ((char *(*)(void *))VT_FN((CItem *)target, VT_GET_NAME))(target);
		sprintf(buf, "You are attacking %s!", name);
		PacketManager_MakePacket_TEXT(obuf, (CItem *)attacker, (CItem *)attacker, 0, buf, 0x22, 3);
		SendToClient((CItem *)attacker, obuf, -1);
	}

	{
		int pcwDelay;
		if (((int (*)(void *, void *))VT_FN((CItem *)target, VT_CAN_BE_AGGRESSED))(target, attacker))
			pcwDelay = 0;
		else
			pcwDelay = 1000;
		return ProcessCrimeWitness(&attacker->container.item.resourceEntity.entity.location, attacker, target, "", pcwDelay, 10, 0);
	}
}

/*
 * 0x004613E6 - CombatManager::CallGuards
 *
 * Dispatches guard reinforcements against mob after validating that
 * neither participant is dead, the target coordinate is on the map,
 * and the call does not originate from within a guarded region.
 */
int
CombatManager_CallGuards(CItem *mob, CLocation *loc, CItem *target, int range)
{
	CLocation localLoc;
	CLocation *targetLoc;
	int guardCount;
	int spawned;
	int numFailed;
	int moreSpawned;

	if (range < 1)
		return 0;

	if (mob != NULL) {
		if (VT_IsDead(mob))
			return 0;
		if (target != NULL) {
			if (VT_IsDead(target))
				return 0;
		}
	} else {
		return 0;
	}

	if (mob == NULL)
		return 0;

	if (target != NULL)
		targetLoc = &target->resourceEntity.entity.location;
	else
		targetLoc = loc;

	CLocation_SetLoc(&localLoc, targetLoc);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, localLoc.x, localLoc.y))
		return 0;

	if (RegionManager_isGuardedRegion(mob->resourceEntity.entity.location.x, mob->resourceEntity.entity.location.y, mob->resourceEntity.entity.location.z)) {
		if (RegionManager_isGuardedRegion(localLoc.x, localLoc.y, localLoc.z))
			return 0;
	}

	guardCount = 1;
	if (guardCount < 1)
		guardCount = 1;
	if (guardCount > 5)
		guardCount = 5;

	spawned = CombatManager_SpawnGuards(mob, guardCount, range);

	if (!RegionManager_inJusticeRegion(mob->resourceEntity.entity.location.x, mob->resourceEntity.entity.location.y, mob->resourceEntity.entity.location.z)) {
		if (!RegionManager_inJusticeRegion(localLoc.x, localLoc.y, localLoc.z))
			return 0;
	}

	numFailed = guardCount - spawned;
	moreSpawned = 0;
	if (numFailed > 0) {
		moreSpawned = CombatManager_SpawnGuardsAtLoc(&mob->resourceEntity.entity.location, numFailed, range, mob, 1);
	}

	return (spawned + moreSpawned) > 0 ? 1 : 0;
}

/*
 * 0x00461568 - CombatManager::SpawnGuards
 *
 * Sweeps nearby mobiles, counting NPCs already attacking mob and
 * recruiting fresh ones until count guards have been assigned.
 */
static int
CombatManager_SpawnGuards(CItem *mob, int count, int range)
{
	CVector list;
	char typeFlag;
	uintptr_t *iter;
	CMobile *npc;
	int spawned;
	int ret;

	spawned = 0;
	typeFlag = 0;

	CVector_Constructor(&list, &typeFlag);

	CEntityMap_RangeQuery(g_MobileMap, &list, mob->resourceEntity.entity.location.x, mob->resourceEntity.entity.location.y, 0x14);

	{
		CLocation *refLoc = CEntity_GetLocation(&mob->resourceEntity.entity);
		Vector_SortByDistPairEntry((uintptr_t *)list.begin, (uintptr_t *)list.end, *refLoc);
	}

	for (iter = (uintptr_t *)list.begin; iter != (uintptr_t *)list.end; iter++) {
		npc = (CMobile *)*iter;

		if (((CNPC *)npc)->aiState == NPC_STATE_ATTACK_TARGET && ((CNPC *)npc)->actionTarget == CMobile_GetSerial((CMobile *)mob)) {
			spawned++;
			goto check_count;
		}

		if (((CNPC *)npc)->aiState != NPC_STATE_ATTACK_TARGET) {
			ret = CombatManager_RecruitGuard(npc, mob, range);
			if (ret == 1) {
				spawned++;
			} else {
				break;
			}
		}
check_count:
		if (spawned >= count)
			break;
	}

	CVector_Destructor(&list);
	return spawned;
}

/*
 * 0x004616C6 - CombatManager::RecruitGuard
 *
 * Guard shouts the threat line and engages mob. Always returns 1.
 */
static int
CombatManager_RecruitGuard(CMobile *guard, CItem *mob, int range)
{
	USED(range);

	((void (*)(void *, const char *, int, int, int))VT_FN((CItem *)guard, VT_EMOTE_CSTRING))(guard, "Thou wilt regret thine actions, swine!", -1, -1, -1);

	CombatInitiate(guard, (CMobile *)mob, 1);
	return 1;
}

/*
 * 0x00461B7B - CombatManager::SpawnGuardsAtLoc
 *
 * Spawns up to count guard NPCs near loc, optionally playing the
 * teleport animation and sound when flag is set, and hands each off
 * to RecruitGuard against mob.
 */
static int
CombatManager_SpawnGuardsAtLoc(CLocation *loc, int count, int range, CItem *mob, int flag)
{
	int i;
	uint16_t templateId;
	int height;
	CLocation spawnLoc;
	CMobile *guard;

	i = 0;
	while (i < count) {
		if (GetRandomRange(0, 1) == 0)
			templateId = 0x3EE;
		else
			templateId = 6;

		CLocation_SetLoc(&spawnLoc, loc);

		height = GetCreatureHeight(templateId);

		if (!CBlockManager_FindSpawnSpotExt(&spawnLoc, (int)loc->z - 2, (int)loc->z + 2, 3, 6, 0x10, height, 0))
			break;

		guard = (CMobile *)CTemplateManager_CreateFromTemplate(templateId, &spawnLoc, 0, 3, NULL);
		if (guard == NULL)
			break;

		((int (*)(void *))VT_FN((CItem *)guard, VT_CHECK_EC))(guard);

		if (flag) {
			Script_doLocAnimation(&spawnLoc, 0x3728, 10, 10, 0, 0);
			Script_sfx(&spawnLoc, 0x1FE, 0);
		}

		i++;
		CombatManager_InitGuard(guard);

		if (!CombatManager_RecruitGuard(guard, mob, range))
			break;
	}

	return i;
}

/*
 * 0x00461CA9 - CombatManager::InitGuard
 *
 * Sets npcSfx to the guard despawn timer value (720 ticks).
 */
static void
CombatManager_InitGuard(CMobile *guard)
{
	((CNPC *)guard)->npcSfx = 0x2D0;
}

/*
 * 0x00462370 - IsInDungeonRange
 *
 * Returns 1 when loc->x lies past the dungeon threshold (0x1400), i.e.
 * outside any guarded town region.
 */
static int
IsInDungeonRange(CLocation *loc)
{
	return loc->x > 0x1400;
}

/*
 * 0x00467911 - StaticInit_CombatEventList
 *
 * Static initializer for g_CombatEventList.
 */
static __attribute__((unused)) void
StaticInit_CombatEventList(void)
{
	g_CombatEventList.head = NULL;
}

/*
 * 0x0048F6D6 - CMobile::CanBeFreelyAggressedBy
 *
 * Fills an AggroInfo from attacker and defers to victim's virtual
 * CanBeFreelyAggressed. Returns 1 when attacker is NULL.
 */
int
CMobile_CanBeFreelyAggressedBy(CMobile *victim, CMobile *attacker)
{
	AggroInfo info;
	int result;

	if (attacker == NULL)
		return 1;

	AggroInfo_Constructor(&info);

	((void (*)(void *, void *))VT_FN((CItem *)attacker, VT_FILL_AGGRO_INFO))(attacker, &info);

	result = ((int (*)(void *, void *, void *))VT_FN((CItem *)victim, VT_CAN_BE_FREELY_AGGRESSED))(victim, attacker, &info);

	AggroInfo_Destructor(&info);
	return result;
}

/*
 * 0x0048F75D - CMobile::CanBeFreelyAggressed
 *
 * Decides whether victim may be struck without incurring a crime flag.
 * Returns 1 for self, same-serial, murderers, criminals, crime victims,
 * guild/order/chaos allies, pets of attacker, lawfully-damaged entries,
 * and aggression-list hits; 0 otherwise. The NPC controllerGuild branch
 * is dead code in the binary (the local stays zero), reproduced as-is.
 */
int
CMobile_CanBeFreelyAggressed_Impl(CMobile *victim, CMobile *attacker, AggroInfo *info)
{
	CList *list;

	if (victim == attacker)
		return 1;

	if (victim->container.item.serial == info->serial)
		return 1;

	if (CMobile_IsMurderer(victim))
		return 1;

	if (CMobile_IsCriminal(victim))
		return 1;

	if (CResourceEntity_HasTag((CItem *)victim, "crimeVictimList", 5))
		return 1;

	if (VT_IsPlayer((CItem *)victim)) {
		if (victim->container.item.serial == info->serial)
			return 1;

		{
			uint32_t guildstoneId = 0;
			CResourceEntity_GetTagObj((CItem *)victim, "guildstoneId", &guildstoneId);
			if (guildstoneId != 0 && guildstoneId == info->guildstoneId)
				return 1;
		}

		{
			int guildType = 0;
			CResourceEntity_GetTagInt((CItem *)victim, "guildType", &guildType);
			if (guildType > 1 && guildType == info->guildType)
				return 1;
		}
	} else {
		{
			uint32_t controller = 0;
			CResourceEntity_GetTagObj((CItem *)victim, "controller", &controller);
			if (controller != 0 && info->serial == controller)
				return 1;
		}

		// Binary bug reproduced: var_18h stays zero while the tag read
		// lands in var_10h, so this guildstone check is dead code.
		{
			uint32_t controllerGuild_unused = 0;
			uint32_t controllerGuild_out = 0;
			USED(controllerGuild_unused);
			CResourceEntity_GetTagObj((CItem *)victim, "controllerGuild", &controllerGuild_out);
			if (controllerGuild_unused != 0 && controllerGuild_unused == info->guildstoneId)
				return 1;
		}

		{
			int controllerGuildType = 0;
			CResourceEntity_GetTagInt((CItem *)victim, "controllerGuildType", &controllerGuildType);
			if (controllerGuildType > 1 && controllerGuildType == info->guildType)
				return 1;
		}
	}

	list = CResourceEntity_GetTagEntity((CItem *)attacker, "lawfullyDamaged");
	if (list != NULL) {
		if (CList_Find(list, 4, victim->container.item.serial))
			return 1;
	}

	list = CResourceEntity_GetTagEntity((CItem *)victim, "aggressionVictimList");
	if (list != NULL) {
		if (CList_Find(list, 4, attacker->container.item.serial))
			return 1;
		if (info->serial != 0) {
			if (CList_Find(list, 4, info->serial))
				return 1;
		}
	}

	if (IsGuildEnemy(victim, info->guildstoneId, info->guildType))
		return 1;

	return 0;
}
