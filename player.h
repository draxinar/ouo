#ifndef PLAYER_H_
#define PLAYER_H_

#include <stdint.h>

__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CPlayerList CPlayerList;
__extension__ typedef struct CUserSock CUserSock;
#include "stl.h"
#include "mobile.h"

/*
 * Target-cursor reply callback stored at CPlayer+0x3C0. Called from
 * HandlePacket_TARGET with (player, cursorType, serial, x, y, z).
 */
typedef void (*TargetCB)(CPlayer *, uint8_t, uint32_t, uint16_t, uint16_t, uint16_t);

/*
 * Sixth rung of the hierarchy: CEntity -> CResourceEntity -> CItem ->
 * CContainer -> CMobile -> CPlayer (0x458 bytes, sibling of CNPC). Linked
 * into g_PlayerList (0x00647CD8). The tail of the struct (skillLocks)
 * lives beyond the binary's 0x458 and is a CUSTOM per-player extension.
 */
struct CPlayer {
	CMobile mobile;               // 0x000-0x37B
	CPlayer *next;                // 0x37C
	CPlayer *prev;                // 0x380 (doubly-linked player list)
	char password[32];            // 0x384-0x3A3
	CUserSock *usersock;          // 0x3A4 (CUserSock*)
	uint32_t pflags;              // 0x3A8
	uint16_t bodyType;            // 0x3AC
	CLocation startLocation;      // 0x3AE-0x3B3 (spawn/creation location)
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64_npc[4];        // 64-bit alignment pad
#endif
	uintptr_t npcTimer;           // 0x3B4 (NPC: init 10, stores CItem* for player death corpse)
	uint32_t aiScheduleTimer;     // 0x3B8 (NPC AI scheduling)
	uint32_t actionTarget;        // 0x3BC (NPC action/targeting)
	TargetCB targetCallback;      // 0x3C0 (callback for TARGET response)
	uint32_t targetSerial;        // 0x3C4 (entity serial of current target)
	uint32_t friendCount;         // 0x3C8
	uint32_t friendAllowCount;    // 0x3CC
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64_fl;           // 64-bit alignment pad
#endif
	uint32_t *friendList;         // 0x3D0 (dynamic array of serials)
	uint32_t *friendAllowList;    // 0x3D4 (dynamic array of serials)
	uint32_t pingTimer;           // 0x3D8
	uint32_t npcAIState;          // 0x3DC (NPC AI state)
	uint8_t npcAIMode;            // 0x3E0 (NPC: init 4)
	uint8_t _pl_pad4a[0x03];      // 0x3E1-0x3E3
	uint32_t isWalking;           // 0x3E4 (walking-in-progress flag)
	uint8_t regionIndex;          // 0x3E8
	uint8_t regionActive;         // 0x3E9
	uint16_t lastSeason;          // 0x3EA (cached season for change detection)
	uint16_t lastWeather;         // 0x3EC (cached weather for change detection)
	uint16_t lastLightLevel;      // 0x3EE (cached light level for change detection)
	uint32_t guardZoneSerial;     // 0x3F0 (guard zone, sets protection msg)
	uint8_t stepCounter;          // 0x3F4 (player: guard zone check every 10 steps; NPC: init random 1-5)
	uint8_t _pl_pad4b;            // 0x3F5
	CLocation lastValidLocation;  // 0x3F6-0x3FB (save: "lastvalidloc")
	uint32_t accountNum;          // 0x3FC (binary: colors/shirt+pants; OSI: accountNum)
	uint32_t characterNum;        // 0x400 (binary: clientIP; OSI: characterNum)
	uint8_t movementIndex;        // 0x404
	uint8_t _pl_pad5[0x03];       // 0x405-0x407
	uint32_t movementTimers[5];   // 0x408-0x41B
	uint32_t creationTime;        // 0x41C (tick count at character creation)
	uint32_t targetHistory[8];    // 0x420-0x43F (recent target serials, [0]=most recent)
	uint32_t deathCount;          // 0x440 (save: "dc=%d", anti-grief death counter)
	uint32_t deathCountTimestamp; // 0x444 (game tick of last death/decay; NPC: "job" string)
	uint32_t playAge;             // 0x448 (save: "playage=%u", total play time counter)
	uint8_t cooldownCounter;      // 0x44C (tick-down timer)
	uint8_t _pl_pad6a[0x03];      // 0x44D-0x44F
	uint32_t effectTickCounter;   // 0x450 (0-120, poison/curse check at 120)
	uint32_t combatTargetSerial;  // 0x454 (current fight target, pkt 0xAA on change)
	uint8_t skillLocks[50];       // CUSTOM (past 0x458): 0=Up, 1=Down, 2=Locked
};

/*
 * Bits of CPlayer.pflags. Checked by CPlayer_IsGameMaster,
 * CPlayer_IsMovePrevented, etc.
 */
enum PlayerFlag {
	PlayerIsMovePrevented = 0x0001, // 0x004536AB: IsMovePrevented(). Set by scripts/spells to freeze.
	PlayerIsEditing = 0x0002,
	PlayerIsOnline = 0x0004,
	PlayerIsLoaded = 0x0008,
	PlayerIsManifesting = 0x0020,
	PlayerSpiritSpeak = 0x0400,
	PlayerIsGameMaster = 0x1000,
	PlayerFlag_0x2000 = 0x2000,
	PlayerIsDead = 0x4000,
	PlayerIsCounselor = 0x8000,
	PlayerIsGold = 0x20000,
	PlayerIsTestCenter = 0x40000, // CUSTOM: -test flag (narrow self-admin tier)
};

/*
 * Global player list head at g_PlayerList (0x00647CD8). Mostly
 * dead space in the binary: only head and savedIterNext are used;
 * field004 and field008 are zeroed in the constructor and never
 * read again. Size 0x5A0 on 32-bit.
 */
struct CPlayerList {
	CPlayer *head;                  // 0x000
	uint32_t field004;              // 0x004
	uint32_t field008;              // 0x008
	uint8_t _pad00C[0x590];         // 0x00C
	CPlayer *savedIterNext;         // 0x59C
};

__extension__ typedef struct CDataBuffer CDataBuffer;

/*
 * Skill-use context / GM page entry (0x3C bytes on 32-bit), used to
 * serialize an in-progress skill action or queued GM help page.
 */
__extension__ typedef struct CSkillUseCtx CSkillUseCtx;
struct CSkillUseCtx {
	uint8_t type;          // 0x00
	uint8_t _pad01[3];     // 0x01
	uint32_t serial;       // 0x04
	uint32_t field08;      // 0x08
	uint32_t field0C;      // 0x0C
	CLocation location;    // 0x10 (6 bytes)
	uint8_t _pad16[2];     // 0x16
	uint32_t field18;      // 0x18
	uint8_t field1C;       // 0x1C
	char name[31];         // 0x1D
};

// 0x00645B30 - Login script list (binary: std::list<char*>).

__extension__ typedef struct AggroInfo AggroInfo;
__extension__ typedef struct CEntityMap CEntityMap;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CResList CResList;
__extension__ typedef struct CString CString;
__extension__ typedef struct CUserSock CUserSock;
__extension__ typedef struct CVector CVector;
__extension__ typedef struct SeqLocNode SeqLocNode;
__extension__ typedef struct StdPtrList StdPtrList;
extern const char *g_ReputationTitles[6][5]; // 0x006173B0
extern char g_PlayerSpeakBuf[256]; // 0x006459B8
extern StdPtrList g_loginScriptList; // 0x00645B30
extern CPlayerList g_PlayerList; // 0x00647CD8
extern uint32_t g_PlayerCreateCount; // 0x0068B374
extern CResList g_GMPlayerList; // 0x006999A0
extern CItem *g_MoveCurrentPlayer; // 0x00699A4C
extern CItem *g_MoveBlocker; // 0x00699A50
extern CPlayer *g_LastCreatedPlayer; // 0x00699B6C

int CPlayer_IsPlayer(CPlayer *this); // 0x004243C0
void CPlayer_SetColors(CPlayer *this, uint32_t colors); // 0x0044F986
CPlayer *CPlayerList_FindBySerial(uint32_t serial); // 0x0044F9AF
void CPlayer_SetIsLoaded(CPlayer *this); // 0x0044FA4A
void CPlayerList_Heartbeat(void); // 0x0044FA6C
int CPlayerList_GetCount(void); // 0x0044FB0B
int16_t CPlayerList_CountNearBorder(void); // 0x0044FB3F
void SendPacketInRange(uint8_t *buf, CLocation *loc, int range); // 0x0044FE3D
void BroadcastToNearbyWithLOS(uint8_t *buf, CLocation *loc, int range); // 0x0044FEBF
void AnimSequence_BroadcastNearby(uint8_t *packet, SeqLocNode *locList, int range); // 0x0044FFD8
void CPlayerList_BroadcastInRange(uint8_t *buf, CLocation *loc, int range, CPlayer *excludePlayer); // 0x0045004C
void CPlayerList_BroadcastToTwoLocs(uint8_t *buf, CLocation *loc1, CLocation *loc2, int range, CItem *exclude); // 0x004500D2
void CPlayerList_SendPacketToAll(uint8_t *buf); // 0x004502C8
CPlayer *NewPlayer(char *name, uint16_t locX, uint16_t locY, uint8_t locZ, uint8_t genre, uint8_t strength, uint8_t dexterity, uint8_t intelligence, uint8_t skill1Number,
        int8_t skill1Value, uint8_t skill2Number, int8_t skill2Value, uint8_t skill3Number, int8_t skill3Value, uint16_t skinColor, uint16_t hairStyle, uint16_t hairColor,
        uint16_t facialHairStyle, uint16_t facialHairColor, uint32_t clientIP, uint32_t colors); // 0x004504B9
void CPlayer_AttachStartupScripts(CPlayer *this); // 0x00450A17
CPlayer *NewCharacter(CUserSock *this, uint16_t locX, uint16_t locY, uint8_t locZ, char *name, char *password, uint8_t genre, uint8_t strength, uint8_t dexterity,
        uint8_t intelligence, uint8_t skill1Number, uint8_t skill1Value, uint8_t skill2Number, uint8_t skill2Value, uint8_t skill3Number, uint8_t skill3Value, uint16_t skinColor,
        uint16_t hairStyle, uint16_t hairColor, uint16_t facialHairStyle, uint16_t facialHairColor, uint32_t clientIP, uint32_t colors); // 0x00450A2E
CPlayer *FindPlayer(CPlayer *head, char *characterName); // 0x00450B70
void CPlayerList_CollectByAccountID(CVector *results, uint32_t accountId); // 0x00450BA5
void CPlayer_Constructor(CPlayer *this); // 0x00450BFC
void CPlayer_Destructor(CPlayer *this); // 0x00450EAD
void CPlayer_SetSerial_VT(CItem *self, uint32_t newSerial); // 0x00450FDF
int CPlayer_HandleMovement(CPlayer *this, uint8_t direction, uint8_t sequence); // 0x00450FF8
void CPlayer_Heartbeat(CPlayer *this); // 0x004514A2
void CPlayer_SystemMessage(CPlayer *this, const char *message); // 0x004516BF
void CPlayer_SystemMessage_Unicode(CPlayer *player, uint16_t *text); // 0x00451711
void CPlayer_SendFormattedMessage(CPlayer *player, char *text, int hue, int font, int speechType); // 0x00451765
int CPlayer_PreDeleteClean_VT(CPlayer *self); // 0x004517BC
void CPlayer_RestoreOldBodyType(CPlayer *this); // 0x004517FE
void CPlayer_OnDeath(CPlayer *this, CItem *attacker, int flag); // 0x00451A6F
int CPlayer_IsLoaded(CPlayer *this); // 0x004524AF
void CPlayer_OnDeathWrap_VT(CPlayer *self, uintptr_t attacker, int deathFlag); // 0x004524CC
uint32_t CPlayer_SetHP_VT(CPlayer *self, int value); // 0x0045253A
uint32_t CPlayer_SetMaxHP_VT(CPlayer *self, int value); // 0x004525DE
uint32_t CPlayer_SetMana_VT(CPlayer *self, int value); // 0x00452648
uint32_t CPlayer_SetMaxMana_VT(CPlayer *self, int value); // 0x004526C9
uint32_t CPlayer_SetStamina_VT(CPlayer *self, int value); // 0x00452733
uint32_t CPlayer_SetMaxStamina_VT(CPlayer *self, int value); // 0x004527BA
int CPlayer_SendDeathIfGhost(CPlayer *this); // 0x0045281B
void CPlayer_SendAppearance(CPlayer *this); // 0x0045286F
void CPlayer_LogOut(CPlayer *this, int save); // 0x00452B0A
void CPlayer_PaperdollTitle_VT(CPlayer *this, CString *title); // 0x00453000
void CPlayer_SetMovePrevented(CPlayer *this, int flag); // 0x0045366C
int CPlayer_IsMovePrevented(CPlayer *this); // 0x004536AB
int CPlayer_CanMoveDirection(CPlayer *this, int direction); // 0x004536C8
int CPlayer_IsEditing(CPlayer *this); // 0x004536DA
void CPlayer_EnableEditing(CPlayer *this); // 0x0045370B
void CPlayer_DisableEditing(CPlayer *this); // 0x0045377C
void CPlayer_SetHiddenFlag(CPlayer *this, int value); // 0x004537DE
int CPlayer_WalkCheck_VT(CPlayer *self, int direction); // 0x00453841
CLocation *CMobile_DoWalkStep(CItem *mob, CLocation *result, CLocation start, int dir); // 0x00453884
void DoTurn(CItem *this, int direction, int isPlayer, uint8_t sequence); // 0x00453AAF
void CPlayer_SendMoveDeny(CPlayer *this, uint8_t sequence); // 0x00453C19
void CPlayer_CheckGuardZone(CPlayer *this); // 0x00453C83
void DoMove(CItem *this, int direction, int isPlayer, uint8_t sequence); // 0x00453D00
int CPlayer_IsPlayerOnline(CPlayer *this); // 0x004547B7
int CPlayer_IsDead(CPlayer *this); // 0x004547D4
void CPlayer_SetWarModeBroadcast(CPlayer *this, int warFlag); // 0x004549EB
void CPlayer_SetWarMode(CPlayer *this, int warFlag, int combatByte2, int combatByte3, int combatByte4); // 0x00454A5B
void CPlayer_HeartbeatCleanup(CPlayer *this); // 0x00454ADD
void CPlayer_PingReply(CPlayer *this, uint8_t sequence); // 0x00454AE8
void CPlayer_ToggleDirectionFlag(CPlayer *this, int direction); // 0x00454B02
int CPlayer_GetDirectionFlag(CPlayer *this, int direction); // 0x00454B95
char *CPlayer_SpeakSysMsg_VT(CItem *self, int flag); // 0x00454BE5
void CPlayer_AddToGMCallQueue(CPlayer *this); // 0x00454D31
int CPlayer_RemoveFromGMCallQueue(CPlayer *this); // 0x00454DC0
int CPlayer_IsGameMaster(CPlayer *this); // 0x00454E03
int CPlayer_HasGMBody(CPlayer *this); // 0x00454E22
void CPlayer_CancelTrade(CPlayer *player); // 0x00454E46
int CPlayer_ApplyResurrection(CPlayer *this, int flag); // 0x00454EB4
void CPlayer_InstantResurrect(CPlayer *this); // 0x00455210
void CMobile_NotifyNearbyPlayers(CItem *mob); // 0x00455264
void Player_RegisterEntity(CPlayer *this); // 0x00455573
void CPlayer_Disconnect(CPlayer *this); // 0x004556B5
void CPlayerList_RemovePlayer(CPlayer *this); // 0x0045580D
void CPlayerList_AddPlayer(CPlayer *this); // 0x00455895
void ItemMap_Init(void); // 0x004558DD
void ItemMap_Insert(CItem *entity); // 0x00455978
void ItemMap_Remove(CItem *entity); // 0x0045599C
void GetNearbyPlayers(CVector *list, CLocation *loc, int range); // 0x004559C0
int CountPlayersInRange(CLocation *loc, int range); // 0x004559E7
void GetNearbyPlayersExclude(CVector *list, CLocation *loc, int range, CItem *exclude); // 0x00455A0A
void CollectMovementVisibility(CVector *removeList, CVector *insertList, CVector *overlapList, int oldX, int oldY, int newX, int newY, int range); // 0x00455A35
void CPlayer_BusyMessage(CPlayer *this, int busyType); // 0x00455A99
int CPlayer_IsBusy(CPlayer *this); // 0x00455AFD
int CPlayer_HasTargetedSerial(CMobile *this, uint32_t serial); // 0x00455B31
void CPlayer_SetLastTarget(CPlayer *this, uint32_t serial); // 0x00455B85
void CMobile_ActionBark(CItem *player, int hueVal, char *text1, char *text2); // 0x00455BD6
int CPlayer_CheckWeight(CPlayer *this); // 0x00455D26
int CPlayer_ProcessDeath(CPlayer *this); // 0x00455DF0
int Combat_NotorietyCompare(CMobile *mob1, CMobile *mob2); // 0x00455ECE
void CriminalAct_Notify(CMobile *criminal, CMobile *victim, int fameAmount, int crimeWeight, int bound, int flags); // 0x00455FAB
void CPlayer_DeathCountDecay(CPlayer *this); // 0x00455FB0
void CPlayer_IncrementDeathCount(CPlayer *this); // 0x0045601B
void CPlayer_FillAggroInfo_VT(CPlayer *self, AggroInfo *info); // 0x0045604A
void CPlayer_NotifyDamage_VT(CPlayer *this, CMobile *attacker, int flag); // 0x004561D8
void CPlayer_ReceiveHelpfulAction(CPlayer *this, CItem *helper); // 0x0045646B
void CPlayer_FameKarmaChange_VT(CPlayer *self, int fame, int karma); // 0x004564C1
void CPlayer_MurderReportCleanup(CPlayer *this); // 0x004564E6
int CPlayer_GetStamina_VT(CPlayer *self); // 0x0045651D
int CPlayer_GetBugStat(CItem *player); // 0x00456540
void CPlayer_CapBaseStats(CPlayer *this, int cap); // 0x00456680
int CPlayer_HasDeadFlag(CPlayer *self); // 0x004566E4
int CPlayer_IsGMAndManifested(CPlayer *this); // 0x00456722
int CPlayer_IsCounselor(CPlayer *this); // 0x0045674E
int CPlayer_IsCounselorWithGMBody(CPlayer *this); // 0x0045676D
void CPlayer_GetCounselorTitle(CPlayer *this, CString *out); // 0x00456799
void CPlayer_SetFightTarget(CPlayer *this, uint32_t targetSerial); // 0x00456818
void CPlayer_NotifySkillGain(CPlayer *this, int8_t skillId, int actualGain); // 0x00456869
int CPlayer_TestBehavior_VT(CPlayer *self, int statIdx, int delta); // 0x004568AE
int CPlayer_SetBehavior_VT(CPlayer *self, int statIdx, int value); // 0x004568F3
int CPlayer_GetPlayAge(CPlayer *this); // 0x00456942
void CPlayer_AddPlayAge(CPlayer *this, int amount); // 0x00456956
void CPlayerList_AddSunlight(int amount); // 0x00456978
void CMobile_ExitCombat(CMobile *this, int flag); // 0x00456ABC
void CPlayer_InitStartingEquipment(CPlayer *this); // 0x00456B52
void CPlayer_TransferResourcesToCorpse(CPlayer *this, CItem *target); // 0x00456BD7
void CPlayer_KarmaHandler(CPlayer *this, int mode); // 0x00456C9E
void CMobile_SetResurrectionResources(CMobile *mob); // 0x00456D6D
void CPlayer_IncrementMurderCount(CPlayer *this, int newCount); // 0x00456DF1
void CPlayer_SetNotoriety_VT(CPlayer *self, int value); // 0x00456E51
int CPlayer_IsGoldAccount(CPlayer *this); // 0x00456E6A
void *CPlayer_ScalarDelete(CPlayer *this, int flags); // 0x00456EE0
void CPlayer_SetLocation(CPlayer *this, CLocation *loc); // 0x00456F10
void CPlayer_SetBodyType(CPlayer *this, uint16_t bodyType); // 0x00456F30
int CTimeManager_IsDaytime(void); // 0x00456F50
CSkillUseCtx *CSkillUseCtx_Init(CSkillUseCtx *this); // 0x00456FD0
void AggroInfo_Constructor(AggroInfo *info); // 0x00457030
void AggroInfo_Destructor(AggroInfo *info); // 0x00457050
CEntityMap *CEntityMap_Constructor(CEntityMap *this, int startX, int startY, int endX, int endY, int blockShift); // 0x00457070
void CEntityMap_Insert(CEntityMap *this, CItem *entity, int x, int y); // 0x004571B0
void CEntityMap_Remove(CEntityMap *this, CItem *entity, int x, int y); // 0x004571F0
int CEntityMap_CountInRange(CEntityMap *this, int16_t x, int16_t y, int range); // 0x004572B0
void CEntityMap_RangeQueryExclude(CEntityMap *this, CVector *list, int16_t x, int16_t y, int range, CItem *exclude); // 0x00457480
void CEntityMap_CollectMovementVisibility(
        CEntityMap *this, CVector *removeList, CVector *insertList, CVector *overlapList, int oldX, int oldY, int newX, int newY, int range); // 0x00457660
int ChebyshevDistXY(int x1, int y1, int x2, int y2); // 0x004578B0
int CEntityMap_GetBlockIdx(CEntityMap *this, int x, int y); // 0x00457C80
void CEntityMap_RangeQuery(CEntityMap *this, CVector *list, int16_t x, int16_t y, int range); // 0x00457DC0
void LoadAll_LoginScriptEntries(void); // 0x0045A76A
void Player_AttachStartupScripts_Inner(CItem *item); // 0x0045A8B3
void CPlayerList_DisconnectAll(CResList *list); // 0x0045F520
void MobileMap_Init(void); // 0x00460AB0
void MobileMap_Insert(CItem *entity); // 0x00460B4B
void MobileMap_Remove(CItem *entity); // 0x00460B78
void CPlayer_Delete_VT(CItem *item); // 0x00484DA1
int CPlayer_GetSurfaceFlags_VT(CPlayer *self, int moveType); // 0x0048667A
void CPlayer_HideVT(CItem *self); // 0x00488209
void CPlayer_DetachSpatial_VT(CItem *self); // 0x004886DF
void CPlayer_SetLocation_VT(CItem *self, CLocation *loc); // 0x004894B1
void CPlayer_ClearTargetSerial(CPlayer *this); // 0x004895ED
int CPlayer_ReturnToHome(CPlayer *this); // 0x00489605
void CPlayer_UpdateLastValidLocation(CPlayer *this, CLocation *loc); // 0x004896B1
void CPlayer_DropAtFeet_VT(CItem *self, CLocation *loc); // 0x00489766
uint8_t CPlayer_GetMovementType_VT(CPlayer *self); // 0x0048B5D7
void CPlayer_AddToAggressorList_VT(CItem *this, CItem *attacker, CItem *controller, uint32_t controllerSerial); // 0x0048FAC5
void CItem_SetMurderCount(CItem *entity, int newCount); // 0x0048FF6F
void SendPacketToPlayer(CPlayer *player, uint8_t *buf, int size);

int CPlayer_IsTestCenter(CPlayer *this); // CUSTOM
void CPlayer_AddTestCenterKit(CPlayer *this, CItem *backpack); // CUSTOM

#endif /* PLAYER_H_ */
