#ifndef MOBILE_H_
#define MOBILE_H_

#include <stdint.h>

#include "serial_list.h"
#include "container.h"

__extension__ typedef struct AggroInfo AggroInfo;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CWeaponDice CWeaponDice;
__extension__ typedef struct EventLogger EventLogger;
#define NPC_COMBAT_AI_MAX_DEPTH 32

__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CCorpse CCorpse;
__extension__ typedef struct CSignpost CSignpost;
__extension__ typedef struct CFragListNode CFragListNode;
__extension__ typedef struct CFragmentList CFragmentList;

__extension__ typedef struct CDiceRoll CDiceRoll;

/*
 * Animate entity with stats, skills, equipment, and combat state
 * (0x37C bytes). Fifth rung of the hierarchy: CEntity -> CResourceEntity
 * -> CItem -> CContainer -> CMobile, specialized by CNPC and CPlayer.
 * Linked into the global mobile list at g_MobileListHead (0x00698850).
 */
// clang-format off
struct CMobile {
	CContainer container;           // 0x00
	CMobile *nextMobile;            // 0x5C
	CMobile *prevMobile;            // 0x60
	CMobile *nextFollower;          // 0x64
	CMobile *prevFollower;          // 0x68
	CMobile *firstFollower;         // 0x6C
	CMobile *lastFollower;          // 0x70
	uint16_t skills[50];            // 0x74
	int16_t skillBonuses[50];       // 0xD8 - total = skills[i] + skillBonuses[i]
	uint32_t skillTimers[50];       // 0x13C - last-use tick, anti-macro
	uint8_t skillCounts[50];        // 0x204 - anti-macro counter, 0-100
	uint8_t _mob_pad1_align[0x02];  // 0x236
	uint32_t skillWeightBudget;     // 0x238 - decay budget
	uint16_t baseStr;               // 0x23C
	uint16_t baseDex;               // 0x23E
	uint16_t baseInt;               // 0x240
	int16_t strBonus;               // 0x242
	int16_t dexBonus;               // 0x244
	int16_t intBonus;               // 0x246
	uint16_t spawnX;                // 0x248
	uint16_t spawnY;                // 0x24A
	uint16_t spawnTemplateId;       // 0x24C
	uint16_t _mob_reserved24E;      // 0x24E
	uint8_t _mob_reserved250;       // 0x250
	uint8_t _mob_pad1a_align[0x03]; // 0x251
	uint32_t hp;                    // 0x254
	uint32_t maxHp;                 // 0x258
	uint32_t stamina;               // 0x25C
	uint32_t maxStamina;            // 0x260
	uint32_t mana;                  // 0x264
	uint32_t maxMana;               // 0x268
	uint32_t lifeclock;             // 0x26C - save: "lifeclock"
	uint32_t staminaRegenTimer;     // 0x270 - save: "clocks" clock[0]
	int32_t staminaLossCounter;     // 0x274 - clock[1]
	int32_t manaRegenTimer;         // 0x278
	int32_t hpRegenTimer;           // 0x27C
	uint32_t actionState;           // 0x280
	uint8_t notoriety;              // 0x284 - save: "not"
	uint8_t _mob_pad1d_align;       // 0x285
	int16_t fame;                   // 0x286
	int16_t karma;                  // 0x288
	uint8_t hunger;                 // 0x28A - 0-25, scales HP regen
	uint8_t attackMode;             // 0x28B - save: "att"
	uint16_t speechHue;             // 0x28C
	uint8_t _mob_pad2a2[0x02];      // 0x28E
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64_eq[4];           // 64-bit alignment pad
#endif
	CItem *equipment[30];           // 0x290
	uint8_t direction;              // 0x308
	uint8_t _mob_pad_dir[0x03];     // 0x309
	uint32_t statClock;             // 0x30C - save: "stclk"
	uint32_t notorietyChangeTimer;  // 0x310 - transient
	uint32_t swingProgress;         // 0x314
	uint32_t swingState;            // 0x318 - 0/1/2
	uint8_t stomach;                // 0x31C - save: "stom"
	uint8_t sex;                    // 0x31D
	uint8_t lightTime;              // 0x31E - save: "lt"
	uint8_t lightVal;               // 0x31F - save: "lv"
	uint32_t isFollower;            // 0x320
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64_own;            // 64-bit alignment pad
#endif
	CMobile *owner;                 // 0x324 - save: "master"
	uint32_t hasFollowers;          // 0x328
	uint16_t sfxNotice;             // 0x32C - init 0xFFFF
	uint16_t sfxIdle;               // 0x32E - init 0xFFFF
	uint16_t sfxHit;                // 0x330 - init 0xFFFF
	uint16_t sfxWasHit;             // 0x332 - init 0xFFFF
	uint16_t sfxDie;                // 0x334 - init 0xFFFF
	uint8_t waitStateMax;           // 0x336
	uint8_t waitStateTick;          // 0x337
	uint32_t waitStateAction;       // 0x338
	uint8_t combatByte1;            // 0x33C
	uint8_t combatByte2;            // 0x33D
	uint8_t combatByte3;            // 0x33E
	uint8_t combatByte4;            // 0x33F
	uint8_t _mob_pad3a[0x04];       // 0x340
#if __SIZEOF_POINTER__ == 8
	uint32_t _mob_pad64_344;        // 64-bit alignment pad
#endif
	CSerialList combatTargetList;   // 0x344 - save: "attacking"
	CSerialList attackerList;       // 0x350 - save: "attackedby"
	uint32_t savedRidingSerial;     // 0x35C - save: "savedriding"
	uint16_t altBodyType;           // 0x360 - morph body type
	uint16_t _mob_pad3b;            // 0x362
	char *name;                     // 0x364
	CDiceRoll armorRating;          // 0x368 - NdF+B dice
	int32_t bonusAC;                // 0x36C
	uint8_t movementType;           // 0x370 - save: "movetype"; 1/3/5
	uint8_t _mob_pad4a[0x03];       // 0x371
	uint32_t moveSpeedAccum;        // 0x374 - fires at >= 100
	uint8_t statusFlags;            // 0x378
	uint8_t mobileFlags;            // 0x379
	uint8_t resistFlags;            // 0x37A - 1=meat, 2=human, 4=good, 8=evil
	uint8_t vulnFlags;              // 0x37B - categories this NPC attacks
};
// clang-format on

/*
 * Bits in CMobile.mobileFlags (offset 0x379).
 */
enum MobileFlag {
	MobileFlag_WarMode = 0x40,
};

__extension__ typedef struct CNPC CNPC;
__extension__ typedef struct CDataBuffer CDataBuffer;
__extension__ typedef struct CString CString;
__extension__ typedef struct CVector CVector;

extern const uint32_t g_GuildWarMatrix[5]; // 0x0061DAF8
extern uint32_t g_MobileCount; // 0x0068B39C
extern CMobile *g_MobileListHead; // 0x00698850
extern int g_CombatAIDepth;
extern uint32_t g_CombatAINPCStack[NPC_COMBAT_AI_MAX_DEPTH];
extern uint32_t g_CombatAITargetStack[NPC_COMBAT_AI_MAX_DEPTH];

uint32_t CMobile_GetSerial(CMobile *this); // 0x00420E30
int CMobile_GetFame(CMobile *this); // 0x00420FC0
int CMobile_GetKarma(CMobile *this); // 0x00420FE0
uint32_t CMobile_GetWaitStateAction(CMobile *this); // 0x00421080
void CMobile_DecayContainerContents(CItem *containerItem); // 0x004493A7
void CMobile_AddFollower(CMobile *owner, CMobile *follower); // 0x0045E2A0
void CMobile_RemoveFollower(CMobile *owner, CMobile *follower); // 0x0045E323
void CMobile_ReleaseAllFollowers(CMobile *owner); // 0x0045E3BC
int CMobile_CountFollowers(CMobile *owner); // 0x0045E3F7
void CMobile_HandleFollowerMovement(CMobile *owner, int direction); // 0x0045E433
int CMobile_GetStat(CMobile *this, int type); // 0x0046CDDB
uint16_t CMobile_GetBaseStat(CMobile *this, int type); // 0x0046CE32
int16_t CMobile_SetBaseStat(CMobile *this, int type, uint16_t value); // 0x0046CE82
int16_t CMobile_AddToBaseStat(CMobile *this, int type, int16_t delta); // 0x0046CF45
int16_t CMobile_GetStatBonus(CMobile *this, int type); // 0x0046CF88
int16_t CMobile_AddToStatBonus(CMobile *this, int type, int16_t delta); // 0x0046CFD8
int16_t CMobile_SetStatBonus(CMobile *this, int type, int16_t value); // 0x0046D01B
void CMobile_NotifyStatChange(CMobile *this, int statType, int delta); // 0x0046D0DE
int CMobile_CheckAllEquipmentWieldable(CMobile *this); // 0x0046D13E
int CMobile_AddMaxHP(CMobile *this, int delta); // 0x0046D1C8
int CMobile_AddMaxStamina(CMobile *this, int delta); // 0x0046D1FA
int CMobile_AddMaxMana(CMobile *this, int delta); // 0x0046D22C
int CMobile_AddHP(CMobile *this, int delta); // 0x0046D25E
int CMobile_AddStamina(CMobile *this, int delta); // 0x0046D2AC
int CMobile_AddMana(CMobile *this, int delta); // 0x0046D2DE
int CMobile_IsPet(CMobile *this); // 0x0046D310
int CMobile_HasBoss(CMobile *this); // 0x0046D34E
int CMobile_CheckOwner(CMobile *this, uint32_t candidateSerial); // 0x0046D38A
void CMobile_IncrementLifeclock(CMobile *mob); // 0x0046D3C1
int CMobile_AddToCombatTargetList(CMobile *this, uint32_t serial); // 0x0046D3E1
int CMobile_GetNaturalDamage(CMobile *this); // 0x0046D42B
int CMobile_AddToAttackerList(CMobile *this, uint32_t serial); // 0x0046D444
void CMobile_StopFightWith(CMobile *this, uint32_t serial, int removeFromAttackerList); // 0x0046D498
void CMobile_DisengageAttackers(CMobile *this); // 0x0046D50A
void CMobile_StopCombat(CMobile *this); // 0x0046D61C
void CMobile_ClearCombatTargets(CMobile *this); // 0x0046D6CF
int CMobile_GetSpeed(CMobile *this); // 0x0046D746
void CMobile_PaperdollTitle_VT(CMobile *mob, CString *title); // 0x0046D784
CItem *CMobile_GetWeapon(CMobile *this); // 0x0046D7A5
void CMobileManager_CombatHeartBeat(uint32_t tickCount); // 0x0046D839
void CMobile_OnDeath_Wrap(CMobile *this, uintptr_t attacker, int deathFlag); // 0x0046D9B2
char *CMobile_SpeakSysMsg_VT(CItem *self, int flag); // 0x0046D9D5
uint32_t CMobile_GetDirection(CMobile *this); // 0x0046D9EB
void CMobile_SetDirection(CItem *mob, uint32_t dir); // 0x0046D9FF
void CMobile_SetName(CMobile *this, char *newName); // 0x0046DAD9
void CMobile_SetArmorRating(CMobile *this, CWeaponDice *dice); // 0x0046DB24
CWeaponDice *CMobile_GetArmorRating(CMobile *this); // 0x0046DB6B
void CMobile_SetBonusAC(CMobile *this, int ac); // 0x0046DB7E
int CMobile_GetBonusAC(CMobile *this); // 0x0046DBBF
uint8_t CMobile_GetMovementType(CMobile *this); // 0x0046DBD3
void CMobile_SetMovementType(CMobile *this, uint8_t type); // 0x0046DBE7
int CMobile_ModifySkillBonus(CMobile *this, int8_t skillId, int delta); // 0x0046DC00
int CMobile_AddToSkill(CMobile *this, int8_t skillId, int delta); // 0x0046DC72
uint16_t CMobile_SetSkill(CMobile *this, int8_t skillNumber, uint16_t value); // 0x0046DCFD
int16_t CMobile_SetSkillBonus(CMobile *this, int8_t skillId, int16_t value); // 0x0046DD40
int CMobile_GetSkillBonus(CMobile *this, int8_t skillId); // 0x0046DD88
int CMobile_GetTotalSkill(CMobile *this, int8_t skillId); // 0x0046DDBD
int CMobile_GetBaseSkill(CMobile *this, int8_t skillId); // 0x0046DE28
int GetCreatureHeight(uint16_t templateId); // 0x0046DE5B
void CMobile_InitMovementTypeFromTemplate(CMobile *this); // 0x0046DEED
CItem *CMobile_FindInTagList_VT(CMobile *self, CString *name, uint32_t value); // 0x0046DF17
int CMobile_CheckWalkDir(CNPC *npc, int dir); // 0x0046E1F4
int CMobile_BecomeTemplate(CMobile *mob, int templateId, int flag); // 0x0046E3B6
uint16_t CMobile_GetSpeechHue(CMobile *this); // 0x0046E5E0
void CMobile_SetSpeechHue(CMobile *this, uint16_t hue); // 0x0046E5F5
void CMobile_SayCString(CItem *ent, char *text, int hue, int type, int font); // 0x0046E610
void CMobile_EmoteCString(CItem *ent, char *text, int hue, int type, int font); // 0x0046E72E
void CMobile_SayToEntity_VT(CMobile *self, CItem *target, uint32_t serial, char *text); // 0x0046E855
void CMobile_LoseNotoriety(CMobile *mob, int amount); // 0x0046E881
void CMobile_GainNotoriety(CMobile *mob, int amount); // 0x0046E8BB
void CMobile_ChangeNotoriety(CMobile *this, int amount); // 0x0046E8F5
int CMobile_GetAdjFame(CMobile *this); // 0x0046E95F
int CMobile_GetFameLevel(CMobile *this); // 0x0046E97A
void Mobile_SendKarmaFameChangeMessage(CMobile *this, int oldValue, int newValue, char *name); // 0x0046E9CD
void *CMobile_ChangeFame(CMobile *this, int fameChange); // 0x0046EA87
CMobile *CMobile_SetFame(CMobile *this, int value); // 0x0046EB75
int CMobile_GetAdjKarma(CMobile *this); // 0x0046EBAD
int CMobile_GetKarmaLevel(CMobile *this); // 0x0046EBE1
void *CMobile_ChangeKarma(CMobile *this, int karmaChange); // 0x0046EC6D
CMobile *CMobile_SetKarma(CMobile *this, int value); // 0x0046ED50
void CMobile_ChangeNotorietyNeg(CMobile *this, int amount); // 0x0046ED8B
void CMobile_SetNotoriety_VT(CMobile *self, int value); // 0x0046EDA6
int CMobile_GetNotoriety(CMobile *this); // 0x0046EE76
int NotoValueToLevel(int value); // 0x0046EE8B
int CMobile_GetNotoLevel(CMobile *this); // 0x0046EEC5
uint32_t CMobile_GetHp(CMobile *this); // 0x0046EEE1
uint32_t CMobile_GetMaxHp(CMobile *this); // 0x0046EEF5
uint32_t CMobile_GetStamina(CMobile *this); // 0x0046EF09
uint32_t CMobile_GetMaxStamina(CMobile *this); // 0x0046EF1D
uint32_t CMobile_GetMana(CMobile *this); // 0x0046EF31
uint32_t CMobile_GetMaxMana(CMobile *this); // 0x0046EF45
uint32_t CMobile_SetHP(CMobile *this, int newHP); // 0x0046EF59
uint32_t CMobile_SetMaxHP(CMobile *this, int newMaxHP); // 0x0046EFD4
uint32_t CMobile_SetStamina(CMobile *this, int newStamina); // 0x0046F035
uint32_t CMobile_SetMaxStamina(CMobile *this, int newMaxStamina); // 0x0046F0B6
uint32_t CMobile_SetMana(CMobile *this, int newMana); // 0x0046F117
uint32_t CMobile_SetMaxMana(CMobile *this, int newMaxMana); // 0x0046F198
void CMobile_DrainStaminaForClimb(CMobile *this, int zDelta); // 0x0046F206
int CMobile_IsRideable(CMobile *this); // 0x0046F2CF
int CMobile_GetHeight_VT(CMobile *self); // 0x0046F32F
void CMobile_SetLight(CMobile *mob, uint8_t lightTime, uint8_t lightVal); // 0x0046F366
void FixBank(CMobile *mob); // 0x0046F6BD
int CMobile_PutMoneyInBank(CMobile *mob, CItem *item, int amount); // 0x0046F804
CItem *CMobile_SubtractGold(CMobile *mob, int amount); // 0x0046F95F
int CMobile_AmountGoldInBank(CMobile *mob); // 0x0046F9AF
CCorpse *CMobile_CreateCorpse(CMobile *mob, CMobile *attacker, int dropLoot); // 0x0046F9E9
int CMobile_PreDeleteClean_VT(CItem *self); // 0x0047022A
int CMobile_GetValue_VT(CItem *self, int useResource, int normalize); // 0x00470253
CItem *CMobile_FindEquippedItem(CMobile *this, uint32_t serial); // 0x004703F8
void NotifyNearby(CItem *self); // 0x004704B8
int CMobile_GetTotalQuantityOfType(CMobile *this, uint16_t bodyType); // 0x00470531
CItem *CMobile_FindItemInEquipment(CMobile *mob, uint16_t bodyType, int targetAmount); // 0x004705B9
void CMobile_SetCorpseLookAtText(CMobile *mob, CItem *corpse); // 0x0047065F
void CMobile_RemoveFromAllCombatLists(CMobile *this); // 0x004707D1
void CMobile_Constructor(CMobile *mob); // 0x0047088B
void CMobile_PreDeleteCleanup(CMobile *mob); // 0x00470D44
void CMobile_Destructor(CMobile *mob); // 0x00470DD4
uint32_t CMobile_GetMaxWeight(CMobile *this); // 0x00470FF1
uint32_t CMobile_GetStoredWeight(CMobile *this); // 0x0047100F
int CMobile_GetRelativeWeight(CMobile *this); // 0x00471025
int CMobile_GetEncumbrancePercent(CMobile *this); // 0x0047107C
int CMobile_GetEncumbranceLimit(CMobile *this); // 0x004710BC
int CMobile_CheckSurfaceOf_VT(CItem *self, CItem *other); // 0x00471188
int CPlayer_CheckSurfaceOf_VT(CItem *self, CItem *other); // 0x0047122A
int CMobile_CheckSurface_VT(CItem *self, uint16_t tileID); // 0x00471323
int CMobile_GetStamina_VT(CMobile *self); // 0x00471350
void CMobile_HandleStaminaDrain(CMobile *this, int movementWeight); // 0x004713C6
void CMobile_StaminaRegenTick(CMobile *mob); // 0x0047152E
void CMobile_StatRegenBonusCalc(CMobile *this, int rate); // 0x00471591
void CMobile_SkillDecayFromRegen(CMobile *this, int rate); // 0x00471679
void CMobile_SetAllCurrentStats(CMobile *this, int value); // 0x00471747
int CMobile_IsInvulnerable(CMobile *this); // 0x0047178C
int CMobile_IsSquelched(CMobile *this); // 0x004717A1
int CPlayer_IsGhost(CPlayer *this); // 0x004717B6
int CMobile_CheckStatusFlag(CMobile *this, uint8_t flagBit); // 0x004717CB
void CMobile_SetStatusFlag(CMobile *this, uint8_t mask, int set); // 0x004717F6
void CMobile_SetStatusFlagsByte(CMobile *this, uint8_t flags); // 0x0047183E
int CMobile_GetHPPercent(CMobile *this); // 0x00471857
void CMobile_EquipmentDecayTick(CMobile *this); // 0x00471888
void CMobile_SetFrozenFlag(CMobile *this, int value); // 0x00471934
int CMobile_DistXY(CMobile *this, int x, int y); // 0x0047194F
void CMobile_BroadcastStatUpdate(CMobile *this, int statType); // 0x0047197C
char *CMobile_GetName_VT(CItem *self); // 0x00471BAE
char *CMobile_GetNameAndHue(CItem *mobile, uint16_t *outHue, CItem *player); // 0x00471BDF
int CMobile_PickOtherStat(CMobile *self, int statIdx); // 0x00471C19
void CMobile_StatAdjust(CMobile *self, int statIdx, int delta); // 0x00471CA7
void InitStatCurve(int maxStats); // 0x00471CE6
void InitSkillCurve(int maxSkill); // 0x00471DCA
int CMobile_IsHumanBodyType(CMobile *this); // 0x00471F1A
int CMobile_OnStatChange_VT(CMobile *self, int statIdx, int delta); // 0x00471F62
int16_t CMobile_SetBaseStat_VT(CMobile *this, int statIndex, int value); // 0x0047208A
int16_t CMobile_GetBaseStat_Wrap(CMobile *this, int statIndex); // 0x004720AE
int16_t CMobile_SetStatAbs(CMobile *this, int type, int newValue); // 0x004720C7
void CMobile_OpenBankGump(CMobile *mob, CPlayer *player); // 0x0047212F
int CMobile_IsUsingOrderShield(CItem *ent); // 0x00472277
int IsUsingChaosShield(CItem *ent); // 0x0047231E
int CMobile_StatCheck_VT(CMobile *self); // 0x004723B1
int CMobile_GetNotoriety_VT(CItem *self, CMobile *viewer); // 0x004723F4
int CMobile_CanBeFreelyAggressed_VT(CMobile *self, CMobile *attacker, AggroInfo *info); // 0x0047243A
int CMobile_EquipOnMobile_VT(CItem *self, CMobile *mob, int layer); // 0x004724E6
int CMobile_EquipIterate_VT(CMobile *self, void *callback, int *result); // 0x004724F5
void CMobile_SetMobileFlag(CMobile *this, uint8_t mask); // 0x00472591
void CMobile_ClearMobileFlag(CMobile *this, uint8_t mask); // 0x004725D9
void CMobile_SetMobileFlagsByte(CMobile *this, uint8_t flags); // 0x0047262A
uint8_t CMobile_GetMobileFlags(CMobile *this); // 0x00472643
int CMobile_CheckMobileFlag(CMobile *this, int flag); // 0x00472657
void CMobile_GetStatusFlags_VT(CItem *self, uint8_t *flags); // 0x00472682
void CMobile_ApplyStatusFlags_VT(CItem *self, int flags); // 0x004726AF
void CollectNearbyMobiles(CVector *list, CLocation *loc, int range); // 0x004726EA
void CMobile_ClearResistFlags(CMobile *this); // 0x0047273B
int CMobile_GetResistFlags(CMobile *this); // 0x00472750
void CMobile_SetResistFlag(CMobile *mob, int flag, int value); // 0x0047279E
void CMobile_ClearVulnFlags(CMobile *this); // 0x004727E1
int CMobile_GetVulnFlags(CMobile *this); // 0x004727F6
void CMobile_SetVulnFlag(CMobile *mob, int flag, int value); // 0x00472844
void CMobile_SetupMasksFromObjVars(CMobile *mob); // 0x00472887
void CMobile_SetAlignment(CMobile *mob, int alignment); // 0x004729D4
void CMobile_SetInvulnerable_VT(CMobile *mob); // 0x00472B78
void CMobile_ClearInvulnerable_VT(CMobile *mob); // 0x00472B8F
int CMobile_IsMounted(CMobile *this); // 0x00472BA6
CMobile *CMobile_GetMount(CMobile *this); // 0x00472BC6
int CMobile_CanBeMounted(CMobile *this); // 0x00472C09
int CMobile_Mount(CMobile *rider, CMobile *mount); // 0x00472CD9
int CMobile_Dismount(CMobile *rider); // 0x00472E3D
int CheckNeedEquipUpdate(CItem *mob, CItem *viewer); // 0x004730D0
void CMobile_SendEntityUpdate_VT(CItem *self, CItem *viewer, int withEquip); // 0x004730E7
void CMobile_SendUpdateToList_VT(CItem *self, CVector *players, int mode); // 0x004731FF
int CMobile_CalcTotalSkillWeight(CMobile *mob); // 0x004734A4
void CMobile_NotifySkillGain(CMobile *mob, int8_t skillId, int actualGain); // 0x0047379C
void GetMinMaxZForEntity(CItem *mob, CLocation loc, int moveType, int *outMinZ, int *outMaxZ); // 0x00481815
int CMobile_WalkCheck_VT(CItem *self, int direction, int *outZ); // 0x004818FB
void CMobile_DetachSpatial_VT(CItem *self); // 0x004838FF
void Entity_BroadcastPacket(CItem *entity, uint32_t serial, uint8_t *buf); // 0x004852D6
void Entity_SendSystemMessage(CItem *entity, uint32_t serial, char *message); // 0x004852F3
int CMobile_GetSurfaceFlags_VT(CMobile *mob, int moveType); // 0x00486641
int CMobile_IsMurderer(CMobile *this); // 0x0048F35B
int IsGuildEnemy(CMobile *subject, uint32_t viewerGuildstoneId, int viewerGuildType); // 0x0048F389
void CItem_FillAggroInfo_VT(CItem *self, AggroInfo *info); // 0x0048F429
int CMobile_ComputeNotoriety(CMobile *subject, CMobile *viewer); // 0x0048F4E1
void CMobile_NotifyDamage(CMobile *target, CMobile *attacker); // 0x0048FC7B
int CMobile_IsCriminal(CMobile *mob); // 0x0048FE31
void CMobile_SetCriminalPunishable(CMobile *mob, CLocation *crimeLoc, int duration); // 0x0048FE4B
void CMobile_SetCriminal(CMobile *mob, int duration); // 0x0048FEB8
int CMobile_ComputeVendorFee(CMobile *this, int gold); // 0x004C3C30
int VendorStock_ComputeSoldItemsFee(CMobile *vendor); // 0x004C3E65
void CMobile_WeightRelated_VT(CMobile *self); // 0x004E0EA0
void CContainer_RefreshContents(CMobile *self); // 0x004E0FD5
int CMobile_IsVendor(CMobile *this);
uint32_t CMobile_GetGold(CMobile *this);

#endif /* MOBILE_H_ */
