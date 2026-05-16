/*
 * CPlayer - behaviour specific to player characters.
 *
 * Login and logout bookkeeping, skill and stat advancement, death and
 * resurrection, GM flag handling, equipment hot-links for client display,
 * and the save/load hooks that persist a player to a profile file.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "account.h"
#include "anim.h"
#include "combat.h"
#include "container.h"
#include "corpse.h"
#include "egg.h"
#include "entitymanager.h"
#include "feature.h"
#include "filemanager.h"
#include "help_queue.h"
#include "io.h"
#include "main.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "skill.h"
#include "taglist.h"
#include "time.h"
#include "timer.h"
#include "trade.h"
#include "usersock.h"
#include "utils.h"
#include "vtable.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "world.h"

static void CPlayer_BeginSetColors(CPlayer *this); // 0x0044F970
static void CPlayer_EndSetColors(CPlayer *this); // 0x0044F97B
static CPlayerList *CPlayerList_Constructor(CPlayerList *this); // 0x0044FA07
static void CPlayerList_DestructorEmpty(CPlayerList *this); // 0x0044FA3F
static void ValidateSkinColor(uint32_t *color); // 0x004503B6
static void ValidateHairColor(uint32_t *color); // 0x004503DA
static void GetHairInfo(uint32_t *style, int genre); // 0x004503FE
static void GetBeardInfo(uint32_t *style, int genre); // 0x00450460
static CPlayer *CPlayer_FindByName(CPlayer *head, char *name); // 0x00450B20
static void CPlayer_RegenTick(CPlayer *this); // 0x0045119A
static void CPlayer_EffectChecker(CPlayer *this); // 0x00451407
static int CPlayer_ShouldDropOnDeath(CMobile *mob, CItem *item); // 0x00451882
static void CPlayer_ProcessDeathItems(CMobile *mob, CItem *container, CItem *corpse, CVector *keptItems, int doMove); // 0x00451970
static int CPlayer_ShouldLogOut(CPlayer *this); // 0x00452D92
static int CPlayer_CheckHouseDoor(CPlayer *this); // 0x00452DF7
static void CPlayerList_SendToAllInRange(CResList *list, uint8_t *buf); // 0x0045EC66
static void CPlayerList_Destructor(CResList *this); // 0x0045F2E0
static void CPlayerList_DestructorWrapper(CResList *this); // 0x00469510
static int CPlayer_IsFriendAllowed(CPlayer *this, CPlayer *other); // 0x0045489D
static void CPlayer_ToggleWarMode(CPlayer *this, int warFlag); // 0x00454905
static void CollectContainerScripts(CItem *container, CVector *vec); // 0x00455654
static void CollectMovementVisibilityExclude(
        CVector *removeList, CVector *insertList, CVector *overlapList, int newX, int newY, int oldX, int oldY, int range, CItem *exclude); // 0x00455A65
static void StaticInit_LoginScriptList(void); // 0x0045A733
static int CItem_GetMurderCount(CItem *entity); // 0x0048FFAA

// 0x00647CD8
CPlayerList g_PlayerList;

// 0x00699B6C - last player created by CPlayer_Constructor
CPlayer *g_LastCreatedPlayer;

// 0x0068B374 - total player creation count
uint32_t g_PlayerCreateCount;

// 0x00645B30 - Login scripts loaded from loginscr.txt.
// Binary: std::list<char*> populated by LoadAll_LoginScriptEntries.
StdPtrList g_loginScriptList;

// 0x006999A0 - GM player list (CResList base of CEditorObj).
// Used by AddToGMCallQueue/RemoveFromGMCallQueue to track online GMs.
CResList g_GMPlayerList;

/*
 * Three-parameter distribution (advA^x * advB + advC), matching CSkillDef
 * at +0xB8. Used by the lazy-initialized murder-penalty curve.
 */
typedef struct Double3 {
	double advA; // +0x00
	double advB; // +0x08
	double advC; // +0x10
} Double3;

// 0x00645AD0 - murder penalty distribution, lazy-initialized
static Double3 g_murderPenaltyDist;             // 0x00645AD0
static uint8_t g_murderPenaltyInitFlag;         // 0x006459B0

// 0x006459B8 - 256-byte buffer for CPlayer_SpeakSysMsg_VT title+name string
char g_PlayerSpeakBuf[256];

// 0x00617270 - newbie script classes checked during death drop (1 entry: "starteq")
static char *g_DeathDropScripts[1] = { "starteq" };

// 0x00617274 - newbie tag names checked during death drop (1 entry: "valueless")
static char *g_DeathDropTags[1] = { "valueless" };

// 0x00645B08 - newbie tag types (1 entry: 0 = WTYPE_INT)
static int g_DeathDropTagTypes[1] = { 0 };

// 0x00699A4C - currently moving player (for shove mechanic)
CItem *g_MoveCurrentPlayer;
// 0x00699A50 - blocking mobile during movement
CItem *g_MoveBlocker;

// 0x00645B0C - entity being processed by DoMove (cleared on exit)
static CItem *g_DoMoveEntity;

/*
 * 0x004243C0
 */
int
CPlayer_IsPlayer(CPlayer *this)
{
	USED(this);
	return 1;
}

/*
 * 0x006173B0 - reputation title lookup table
 *
 * Indexed as g_ReputationTitles[5 - karmaLevel][fameLevel].
 * karmaLevel 0-5 (from CMobile_GetKarmaLevel), fameLevel 0-4
 * (from CMobile_GetFameLevel). NULL entries produce no title prefix.
 */
const char *g_ReputationTitles[6][5] = {
	// karma 5 (row 0 = 5 - 5)
	{ "Trustworthy", "Estimable", "Great", "Glorious", "Glorious" },
	// karma 4 (row 1 = 5 - 4)
	{ "Honest", "Commendable", "Famed", "Illustrious", "Illustrious" },
	// karma 3 (row 2 = 5 - 3)
	{ "Good", "Honorable", "Admirable", "Noble", "Noble" },
	// karma 2 (row 3 = 5 - 2)
	{ "Fair", "Respectable", "Proper", "Eminent", "Eminent" },
	// karma 1 (row 4 = 5 - 1)
	{ NULL, "Upstanding", "Reputable", "Distinguished", "Distinguished" },
	// karma 0 (row 5 = 5 - 0)
	{ NULL, NULL, "Notable", "Renowned", "" },
};

/*
 * 0x0044F970 - CPlayer::BeginSetColors
 *
 * No-op stub called before SetColors writes the colors field.
 */
static void
CPlayer_BeginSetColors(CPlayer *this)
{
	USED(this);
}

/*
 * 0x0044F97B - CPlayer::EndSetColors
 *
 * No-op stub called after SetColors writes the colors field.
 */
static void
CPlayer_EndSetColors(CPlayer *this)
{
	USED(this);
}

/*
 * 0x0044F986 - CPlayer::SetColors
 *
 * Sets the shirt+pants color dword at CPlayer offset 0x3FC.
 * Binary calls no-op stubs 0x0044F970 and 0x0044F97B before and after.
 */
void
CPlayer_SetColors(CPlayer *this, uint32_t colors)
{
	CPlayer_BeginSetColors(this);
	this->accountNum = colors;
	CPlayer_EndSetColors(this);
}

/*
 * 0x0044F9AF - CPlayerList::FindBySerial
 *
 * Walks the global player linked list comparing serial fields.
 */
CPlayer *
CPlayerList_FindBySerial(uint32_t serial)
{
	CPlayer *p;

	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		if (p->mobile.container.item.serial == serial)
			return p;
	}
	return NULL;
}

/*
 * 0x0044FA07 - CPlayerList::CPlayerList
 *
 * Zeroes head, field004, field008, and savedIterNext.
 */
static __attribute__((unused)) CPlayerList *
CPlayerList_Constructor(CPlayerList *this)
{
	this->savedIterNext = NULL;
	this->head = NULL;
	this->field004 = 0;
	this->field008 = 0;
	return this;
}

/*
 * 0x0044FA3F - CPlayerList::~CPlayerList (empty)
 *
 * No-op destructor for g_PlayerList.
 */
static __attribute__((unused)) void
CPlayerList_DestructorEmpty(CPlayerList *this)
{
	USED(this);
}

/*
 * 0x0044FA4A - CPlayer::SetIsLoaded
 *
 * Sets PlayerIsLoaded bit in pflags.
 */
void
CPlayer_SetIsLoaded(CPlayer *this)
{
	this->pflags |= PlayerIsLoaded;
}

/*
 * 0x0044FA6C - CPlayerList::Heartbeat
 *
 * Ticks each online player, saving the next pointer so a player can
 * remove itself from the list during heartbeat without breaking iteration.
 */
void
CPlayerList_Heartbeat(void)
{
	CPlayer *p;

	for (p = g_PlayerList.head; p != NULL; p = g_PlayerList.savedIterNext) {
		g_PlayerList.savedIterNext = p->next;
		if (p->mobile.container.item.resourceEntity.entity.removedFromWorld)
			continue;
		CPlayer_Heartbeat(p);
	}
	g_PlayerList.savedIterNext = NULL;
}

/*
 * 0x0044FB0B - CPlayerList::GetCount
 *
 * Returns (g_PlayerCreateCount - entity manager list size) masked to 16 bits.
 */
int
CPlayerList_GetCount(void)
{
	int count = 0;

	count = (int)g_PlayerCreateCount;
	count -= EntityManager_GetListPtr()->size;
	return count & 0xFFFF;
}

/*
 * 0x0044FB3F - CPlayerList::CountNearBorder
 *
 * Counts players whose current tile is within the map border band.
 */
int16_t
CPlayerList_CountNearBorder(void)
{
	CPlayer *p;
	int count;
	CLocation *loc;

	count = 0;
	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		loc = CEntity_GetLocation(&p->mobile.container.item.resourceEntity.entity);
		if (CBlockManager_IsNearBorder(&g_SpatialGrid, loc->x, loc->y))
			count++;
	}
	return (int16_t)count;
}

/*
 * 0x0044FE3D - SendPacketInRange
 *
 * Sends a packet to every player within range of loc.
 */
void
SendPacketInRange(uint8_t *buf, CLocation *loc, int range)
{
	CVector list;
	char typeFlag[16] = { 0 };

	CVector_Constructor(&list, typeFlag);
	CEntityMap_RangeQuery(g_ItemMap, &list, loc->x, loc->y, range);
	SendToClientList(&list, buf);
	CVector_Destructor(&list);
}

/*
 * 0x0044FEBF - BroadcastToNearbyWithLOS
 *
 * Sends a packet to every player within range of loc that has
 * line-of-sight from its eye position (z+8) to loc.
 */
void
BroadcastToNearbyWithLOS(uint8_t *buf, CLocation *loc, int range)
{
	CVector nearby, filtered;
	char typeFlag1[16] = { 0 }, typeFlag2[16] = { 0 };
	uintptr_t *iter;
	CLocation eyeLoc, locCopy, delta;

	CVector_Constructor(&nearby, typeFlag1);
	CVector_Constructor(&filtered, typeFlag2);

	CEntityMap_RangeQuery(g_ItemMap, &nearby, loc->x, loc->y, range);

	iter = (uintptr_t *)CSearchCtx_GetBucket((CSearchCtx *)&nearby);
	while (iter != (uintptr_t *)nearby.end) {
		CLocation_SetLoc(&locCopy, loc);
		CLocation_Constructor3D(&delta, 0, 0, 8);
		CLocation_AddWrapped(CEntity_GetLocation((CEntity *)(uintptr_t)*iter), &eyeLoc, &delta);
		if (CTerrainManager_LOSRaycast(&eyeLoc, &locCopy, 1))
			CVector_PushBack(&filtered, *iter);
		iter++;
	}

	SendToClientList(&filtered, buf);

	CVector_Destructor(&filtered);
	CVector_Destructor(&nearby);
}

/*
 * 0x0044FFD8 - AnimSequence_BroadcastNearby
 *
 * Sends packet to each online player that is within range
 * (Chebyshev distance) of any location in locList.
 */
void
AnimSequence_BroadcastNearby(uint8_t *packet, SeqLocNode *locList, int range)
{
	CPlayer *player;
	SeqLocNode *loc;

	player = g_PlayerList.head;
	while (player != NULL) {
		loc = locList;
		while (loc != NULL) {
			CLocation *playerLoc = CEntity_GetLocation(&player->mobile.container.item.resourceEntity.entity);
			int dist = CLocation_ChebyshevDistance(playerLoc, &loc->loc);
			if (dist <= range) {
				SendToClient(&player->mobile.container.item, packet, -1);
				break;
			}
			loc = loc->next;
		}
		player = player->next;
	}
}

/*
 * 0x0045004C - CPlayerList::BroadcastInRange
 *
 * Sends a packet to every player within range of loc, skipping
 * excludePlayer.
 */
void
CPlayerList_BroadcastInRange(uint8_t *buf, CLocation *loc, int range, CPlayer *excludePlayer)
{
	CVector list;
	char typeFlag = '\x01';

	CVector_Constructor(&list, &typeFlag);
	GetNearbyPlayersExclude(&list, loc, range, (CItem *)excludePlayer);

	SendToClientList(&list, buf);
	CVector_Destructor(&list);
}

/*
 * 0x004500D2 - CPlayerList::BroadcastToTwoLocs
 *
 * Sends a packet to every player within range of loc1 or loc2,
 * skipping exclude. Merges the two ranges with duplicate removal so
 * each player receives the packet at most once.
 */
void
CPlayerList_BroadcastToTwoLocs(uint8_t *buf, CLocation *loc1, CLocation *loc2, int range, CItem *exclude)
{
	CVector list1, list2, merged;
	char typeFlag1[16] = { 0 }, typeFlag2[16] = { 0 }, typeFlagM[16] = { 0 };
	uintptr_t *ptr1, *ptr2;

	CVector_Constructor(&list1, typeFlag1);
	CVector_Constructor(&list2, typeFlag2);

	GetNearbyPlayersExclude(&list1, loc1, range, exclude);
	GetNearbyPlayersExclude(&list2, loc2, range, exclude);

	Vector_SortRaw(list1.begin, list1.end);
	Vector_SortRaw(list2.begin, list2.end);

	CVector_Constructor(&merged, typeFlagM);

	ptr1 = (uintptr_t *)list1.begin;
	ptr2 = (uintptr_t *)list2.begin;

	// 0x004501B9: two-pointer merge with duplicate elimination
	while (ptr1 < (uintptr_t *)list1.end && ptr2 < (uintptr_t *)list2.end) {
		if (*ptr1 < *ptr2) {
			CVector_PushBack(&merged, *ptr1);
			ptr1++;
		} else if (*ptr1 > *ptr2) {
			CVector_PushBack(&merged, *ptr2);
			ptr2++;
		} else {
			// 0x00450219: equal - add once, skip both
			CVector_PushBack(&merged, *ptr1);
			ptr1++;
			ptr2++;
		}
	}

	while (ptr1 < (uintptr_t *)list1.end) {
		CVector_PushBack(&merged, *ptr1);
		ptr1++;
	}

	while (ptr2 < (uintptr_t *)list2.end) {
		CVector_PushBack(&merged, *ptr2);
		ptr2++;
	}

	SendToClientList(&merged, buf);

	CVector_Destructor(&merged);
	CVector_Destructor(&list2);
	CVector_Destructor(&list1);
}

/*
 * 0x004502C8 - CPlayerList::SendPacketToAll
 *
 * Sends a packet to every player via SendToClientList.
 */
void
CPlayerList_SendPacketToAll(uint8_t *buf)
{
	CVector list;
	char typeFlag;
	CPlayer *p;

	typeFlag = 0;
	CVector_Constructor(&list, &typeFlag);

	for (p = g_PlayerList.head; p != NULL; p = p->next)
		CVector_PushBack(&list, (uintptr_t)p);

	SendToClientList(&list, buf);
	CVector_Destructor(&list);
}

/*
 * 0x004503B6
 */
static void
ValidateSkinColor(uint32_t *color)
{
	if (*color < 0x3EA || *color >= 0x423)
		*color = 0x3EA;
}

/*
 * 0x004503DA
 */
static void
ValidateHairColor(uint32_t *color)
{
	if (*color < 0x44E || *color >= 0x47E)
		*color = 0x44E;
}

/*
 * 0x004503FE
 */
static void
GetHairInfo(uint32_t *style, int genre)
{
	if (*style == 0)
		return;
	if (*style >= 0x203B && *style <= 0x203D)
		return;
	if (*style >= 0x2044 && *style <= 0x204A) {
		// Style 0x2046 is male-only
		if (*style == 0x2046 && genre == 0)
			*style = 0;
		return;
	}
	*style = 0;
}

/*
 * 0x00450460
 */
static void
GetBeardInfo(uint32_t *style, int genre)
{
	if (*style == 0)
		return;
	// Females cannot have beards
	if (genre == 1) {
		*style = 0;
		return;
	}
	if (*style >= 0x203E && *style <= 0x2041)
		return;
	if (*style >= 0x204B && *style <= 0x204D)
		return;
	*style = 0;
}

/*
 * 0x004504B9 - NewPlayer
 *
 * Creates a new player character with validated stats, colors, hair,
 * beard, and starting backpack, then registers it with the world.
 */
CPlayer *
NewPlayer(char *name, uint16_t locX, uint16_t locY, uint8_t locZ, uint8_t genre, uint8_t strength, uint8_t dexterity, uint8_t intelligence, uint8_t skill1Number,
        int8_t skill1Value, uint8_t skill2Number, int8_t skill2Value, uint8_t skill3Number, int8_t skill3Value, uint16_t skinColor, uint16_t hairStyle, uint16_t hairColor,
        uint16_t facialHairStyle, uint16_t facialHairColor, uint32_t clientIP, uint32_t colors)
{
	CPlayer *player;
	CEntity *ent;
	CMobile *mob;
	CLocation tmpLoc;
	CItem *hair;
	CItem *beard;
	CItem *backpack;
	int equipResult;
	int i;
	uint32_t dwSkinColor;
	uint32_t dwHairColor;
	uint32_t dwFacialHairColor;
	uint32_t dwHairStyle;
	uint32_t dwFacialHairStyle;

	CLocation_Init(&tmpLoc);

	player = malloc(sizeof(*player));
	if (player == NULL)
		return NULL;

	CPlayer_Constructor(player);
	mob = &player->mobile;
	ent = &mob->container.item.resourceEntity.entity;

	// Serial assigned by CItem_Constructor via CWorld::AllocSerial,
	// called through CPlayer_Constructor -> CMobile_Constructor ->
	// CContainer_Constructor chain. Hash insertion also handled there.

	if (player == NULL)
		return NULL;

	if (locX == 0xFFFF) {
		locX = (uint16_t)g_ConfigStartX + (uint16_t)g_Config.x;
		locY = (uint16_t)g_ConfigStartY + (uint16_t)g_Config.y;
		locZ = (uint8_t)g_ConfigStartZ;
	}

	if (genre != 0)
		genre = 1;

	if (strength + dexterity + intelligence > 100 || strength > 50 || strength < 10 || dexterity > 50 || dexterity < 10 || intelligence > 50 || intelligence < 10) {
		strength = 34;
		dexterity = 33;
		intelligence = 33;
	}

	if (skill1Value + skill2Value + skill3Value > 100 || skill1Value > 50 || skill1Value < 0 || skill2Value > 50 || skill2Value < 0 || skill3Value > 50 || skill3Value < 0) {
		skill1Value = 34;
		skill2Value = 33;
		skill3Value = 33;
	}

	dwSkinColor = skinColor;
	dwHairColor = hairColor;
	dwFacialHairColor = facialHairColor;
	ValidateSkinColor(&dwSkinColor);
	ValidateHairColor(&dwHairColor);
	ValidateHairColor(&dwFacialHairColor);

	CLocation_Constructor3D(&tmpLoc, locX, locY, (int8_t)locZ);
	CPlayer_SetLocation(player, &tmpLoc);

	CPlayer_SetBodyType(player, 0x190 + genre);

	player->bodyType = CResourceEntity_GetBodyType(&mob->container.item);

	CMobile_SetName(mob, name);

	mob->maxHp = strength;
	mob->hp = mob->maxHp;

	mob->maxMana = intelligence;
	mob->mana = mob->maxMana;

	mob->maxStamina = dexterity;
	mob->stamina = mob->maxStamina;

	for (i = 0; i < 50; i++)
		mob->skills[i] = 0;

	((void (*)(void *, int))VT_FN(&mob->container.item, VT_SET_NOTORIETY))(mob, 0);

	mob->hunger = 25;
	mob->attackMode = 0;

	mob->sex = genre;

	mob->baseStr = strength;
	mob->baseDex = dexterity;
	mob->baseInt = intelligence;

	ent->color = (uint16_t)(dwSkinColor | 0x8000);

	CMobile_SetSkill(mob, skill1Number, skill1Value * 10);
	CMobile_SetSkill(mob, skill2Number, skill2Value * 10);
	CMobile_SetSkill(mob, skill3Number, skill3Value * 10);

	player->characterNum = clientIP;

	CPlayer_SetColors(player, colors);
	dwHairStyle = hairStyle;
	GetHairInfo(&dwHairStyle, genre);
	if ((int32_t)dwHairStyle > 0) {
		hair = CWorld_CreateItem(g_World, (uint16_t)dwHairStyle);
		if (hair != NULL) {
			hair->resourceEntity.entity.color = (uint16_t)dwHairColor;
			equipResult = ((int (*)(void *, void *, int))VT_FN(hair, VT_EQUIP_ON_MOBILE))(hair, player, 0x0B);
			if (equipResult == 1) {
				CItem_Setup(hair, 1, CEntity_GetLocation(ent), 0, 1);
				if (!ValidateInWorld(hair))
					hair = NULL;
			} else {
				if (hair != NULL)
					((void (*)(void *))VT_FN(hair, VT_DELETE))(hair);
				hair = NULL;
			}
		}
	}

	dwFacialHairStyle = facialHairStyle;
	GetBeardInfo(&dwFacialHairStyle, genre);
	if ((int32_t)dwFacialHairStyle > 0) {
		beard = CWorld_CreateItem(g_World, (uint16_t)dwFacialHairStyle);
		if (beard != NULL) {
			beard->resourceEntity.entity.color = (uint16_t)dwFacialHairColor;
			equipResult = ((int (*)(void *, void *, int))VT_FN(beard, VT_EQUIP_ON_MOBILE))(beard, player, 0x10);
			if (equipResult == 1) {
				CItem_Setup(beard, 1, CEntity_GetLocation(ent), 0, 1);
				if (!ValidateInWorld(beard))
					beard = NULL;
			} else {
				if (beard != NULL)
					((void (*)(void *))VT_FN(beard, VT_DELETE))(beard);
				beard = NULL;
			}
		}
	}

	backpack = CWorld_CreateContainerItem(g_World, 0x0E75);
	if (backpack != NULL) {
		equipResult = ((int (*)(void *, void *, int))VT_FN(backpack, VT_EQUIP_ON_MOBILE))(backpack, player, 0x15);
		if (equipResult == 1) {
			CItem_Setup(backpack, 1, CEntity_GetLocation(ent), 0, 1);
			if (!ValidateInWorld(backpack))
				backpack = NULL;
			if (backpack != NULL)
				CItem_DecayProcess(backpack);
		} else {
			if (backpack != NULL)
				((void (*)(void *))VT_FN(backpack, VT_DELETE))(backpack);
			backpack = NULL;
		}
	}

	CPlayer_InitStartingEquipment(player);

	// Custom: -test starter kit (5000 gold + filled spellbook + reagent bag)
	// goes into the backpack we just created.
	if (g_DebugTest && backpack != NULL)
		CPlayer_AddTestCenterKit(player, backpack);

	// MODIFIED 0x00450996: binary uses (g_Config.x, g_Config.y, 0) which ignores
	// the client's city selection; use locX/locY/locZ to honor the start
	// location chosen during character creation in newer clients.
	CLocation_Set(&tmpLoc, locX, locY, (int8_t)locZ);
	CPlayer_SetLocation(player, &tmpLoc);

	Player_RegisterEntity(player);

	// Custom: apply client-selected shirt/pants colors to template
	// equipment. Player_RegisterEntity applies the body template
	// (2998/2999) which creates shirt, pants/skirt, shoes, etc.
	// with random template colors. Override with client selection.
	if (feat(FEAT_CREATION_COLORS) && colors != 0) {
		// Mask 0x3FFF to strip any partial-hue (0x8000) or translucent
		// (0x4000) flag bits a client might OR in; we only want the
		// raw dye-tub hue index.
		uint16_t shirtHue = (uint16_t)((colors >> 16) & 0x3FFF);
		uint16_t pantsHue = (uint16_t)(colors & 0x3FFF);
		if (shirtHue != 0 && mob->equipment[5] != NULL)
			mob->equipment[5]->resourceEntity.entity.color = shirtHue;
		if (pantsHue != 0) {
			if (mob->equipment[4] != NULL)
				mob->equipment[4]->resourceEntity.entity.color = pantsHue;
			if (mob->equipment[23] != NULL)
				mob->equipment[23]->resourceEntity.entity.color = pantsHue;
		}
	}

	((void (*)(void *, CLocation *))VT_FN(&mob->container.item, VT_SET_LOCATION))(player, &ent->location);

	player->creationTime = g_GameTick;

	CPlayer_AttachStartupScripts(player);

	return player;
}

/*
 * 0x00450A17 - CPlayer::AttachStartupScripts
 *
 * Thiscall wrapper. Forwards player item pointer to the inner
 * iterator at 0x0045A8B3.
 */
void
CPlayer_AttachStartupScripts(CPlayer *this)
{
	Player_AttachStartupScripts_Inner(&this->mobile.container.item);
}

/*
 * 0x00450B20
 */
static CPlayer *
CPlayer_FindByName(CPlayer *head, char *name)
{
	CPlayer *p;

	for (p = head; p != NULL; p = p->next) {
		char *pname = ((char *(*)(void *))VT_FN(&p->mobile.container.item, VT_GET_NAME))(p);
		if (strcasecmp(pname, name) == 0)
			return p;
	}
	return NULL;
}

/*
 * 0x00450A2E - NewCharacter
 *
 * Allocates a new CPlayer via NewPlayer, stores the password, links it
 * to the connection, and binds it to the socket's account.
 */
CPlayer *
NewCharacter(CUserSock *this, uint16_t locX, uint16_t locY, uint8_t locZ, char *name, char *password, uint8_t genre, uint8_t strength, uint8_t dexterity, uint8_t intelligence,
        uint8_t skill1Number, uint8_t skill1Value, uint8_t skill2Number, uint8_t skill2Value, uint8_t skill3Number, uint8_t skill3Value, uint16_t skinColor, uint16_t hairStyle,
        uint16_t hairColor, uint16_t facialHairStyle, uint16_t facialHairColor, uint32_t clientIP, uint32_t colors)
{
	CPlayer *player;

	player = NewPlayer(name, locX, locY, locZ, genre, strength, dexterity, intelligence, skill1Number, skill1Value, skill2Number, skill2Value, skill3Number, skill3Value,
	        skinColor, hairStyle, hairColor, facialHairStyle, facialHairColor, clientIP, colors);
	strcpy(player->password, password);
	player->usersock = this;
	this->player = player;

	// Custom: bind character to account
	if (this->account != NULL) {
		CVector charVec;
		char typeFlag = '\x01';
		CVector_Constructor(&charVec, &typeFlag);
		player->accountNum = this->account->accountNum;
		CPlayerList_CollectByAccountID(&charVec, this->account->accountNum);
		player->characterNum = CVector_GetCount(&charVec) - 1;
		CVector_Destructor(&charVec);
	}

	return player;
}

/*
 * 0x00450B70 - FindPlayer
 *
 * Thiscall on PlayerManager, 1 arg (name). Calls CPlayer_FindByName
 * (0x00450B20) first. If NULL, falls back to EntityManager_FindByName
 * (0x00491CC4) which iterates 0x006CA928 comparing via vtable[0x34] GetName.
 */
CPlayer *
FindPlayer(CPlayer *head, char *characterName)
{
	CPlayer *p;

	p = CPlayer_FindByName(head, characterName);
	if (p != NULL)
		return p;
	return (CPlayer *)EntityManager_FindByName(characterName);
}

/*
 * 0x00450BA5 - CPlayerList::CollectByAccountID
 *
 * Collects players matching accountId, then delegates to
 * EntityManager for non-player entities.
 */
void
CPlayerList_CollectByAccountID(CVector *results, uint32_t accountId)
{
	CPlayer *p;

	p = g_PlayerList.head;
	while (p != NULL) {
		if (p->accountNum == accountId) {
			CVector_PushBack(results, (uintptr_t)p);
		}
		p = p->next;
	}
	EntityManager_CollectByAccountID(results, accountId);
}

/*
 * 0x00450BFC - CPlayer::CPlayer
 *
 * Constructs a CPlayer on top of CMobile, installs the CPlayer vtable,
 * zeroes player fields, registers on g_PlayerList, and applies the
 * default starting stats (hp/maxHp 50, notorietyChangeTimer 3600,
 * combat byte 50).
 */
void
CPlayer_Constructor(CPlayer *this)
{
	CMobile *mob;
	CEntity *ent;
	int i;
	uint32_t tickBase;

	mob = &this->mobile;
	CMobile_Constructor(mob);
	ent = &mob->container.item.resourceEntity.entity;

	this->next = NULL;
	this->prev = NULL;
	this->pflags = 0;
	this->bodyType = 0;
	CLocation_Init(&this->startLocation);
	this->npcTimer = 0;
	this->aiScheduleTimer = 0;
	this->actionTarget = 0;
	this->targetCallback = NULL;
	this->targetSerial = 0;
	this->friendCount = 0;
	this->friendAllowCount = 0;
	this->friendList = NULL;
	this->friendAllowList = NULL;
	this->pingTimer = 0;
	this->npcAIState = 0;
	this->lastSeason = 0xFFFF;
	this->lastWeather = 0xFFFF;
	this->lastLightLevel = 0;
	this->guardZoneSerial = 0;
	this->stepCounter = 0;
	CLocation_Init(&this->lastValidLocation);
	this->accountNum = 0;
	this->characterNum = 0;
	this->movementIndex = 0;
	this->creationTime = 0;
	this->deathCount = 0;
	this->deathCountTimestamp = 0;
	this->playAge = 0;
	this->effectTickCounter = 0;
	this->combatTargetSerial = 0;

	CEntity_SetType(ent, ETYPE_PLAYER);

	for (i = 0; i < 8; i++)
		this->targetHistory[i] = 0;

	g_LastCreatedPlayer = this;
	g_PlayerCreateCount++;

	CPlayerList_AddPlayer(this);

	this->cooldownCounter = 0;
	memset(this->skillLocks, 0, sizeof(this->skillLocks));
	mob->combatByte3 = 50;
	mob->maxHp = 50;
	mob->hp = 50;
	mob->notorietyChangeTimer = 3600;
	this->password[0] = '\0';
	this->usersock = NULL;

	tickBase = CTimeManager_GetTickCount() - 1000;
	for (i = 0; i < 5; i++)
		this->movementTimers[i] = tickBase;
}

/*
 * 0x00450EAD - CPlayer::~CPlayer
 *
 * Player destructor: hides the entity, cancels trades, removes from
 * the player and GM lists, frees friend lists, and chains to
 * CMobile_Destructor.
 */
void
CPlayer_Destructor(CPlayer *this)
{
	CEntity *ent;

	ent = &this->mobile.container.item.resourceEntity.entity;

	CEntity_SetType(ent, ETYPE_PLAYER);

	if (!ent->removedFromWorld) {
		((void (*)(void *))VT_FN(&this->mobile.container.item, VT_HIDE))(&this->mobile.container.item);
	}

	CItem_ClearScriptsAndTags(&this->mobile.container.item);

	if (g_LastCreatedPlayer == this)
		g_LastCreatedPlayer = NULL;

	if (CPlayer_IsGameMaster(this))
		CPlayer_RemoveFromGMCallQueue(this);

	g_PlayerCreateCount--;

	if (g_PlayerList.savedIterNext == this)
		g_PlayerList.savedIterNext = this->next;

	CPlayer_CancelTrade(this);

	CPlayerList_RemovePlayer(this);

	// No-op call matching binary
	CPlayer_BeginSetColors(this);

	if (this->friendList != NULL) {
		free(this->friendList);
		this->friendList = NULL;
	}

	if (this->friendAllowList != NULL) {
		free(this->friendAllowList);
		this->friendAllowList = NULL;
	}

	CMobile_Destructor(&this->mobile);
}

/*
 * 0x00450FDF - CPlayer vtable[0x140] SetSerial
 *
 * Delegates to CItem_SetSerial.
 */
void
CPlayer_SetSerial_VT(CItem *self, uint32_t newSerial)
{
	CItem_SetSerial(self, newSerial);
}

/*
 * 0x00450FF8 - CPlayer::HandleMovement
 *
 * Validates fatigue and movement rate, drains stamina, then asks
 * CTerrainManager_MovePlayer to perform the step. Returns 1 on
 * success, 0 when the move is rejected.
 */
int
CPlayer_HandleMovement(CPlayer *this, uint8_t direction, uint8_t sequence)
{
	CItem *item;
	int speedRate;
	uint32_t elapsed;
	int moveRate;
	int staminaCost;

	item = &this->mobile.container.item;

	elapsed = CTimeManager_GetTickCount() - this->movementTimers[this->movementIndex];

	// 0x0045102b: vtable[0x214] GetSpeed, compute 500/speed (dead code -
	// result stored in local but never used)
	speedRate = 500 / ((int (*)(void *))VT_FN(item, VT_GET_SPEED))(item);
	USED(speedRate);

	if (!VT_IsDead(item)) {
		if (((int (*)(void *))VT_FN(item, VT_GET_STAMINA))(item) == 1) {
			if (CMobile_IsMounted(&this->mobile))
				CPlayer_SystemMessage(this, "Your horse is too fatigued to move.");
			else
				CPlayer_SystemMessage(this, "You are too fatigued to move.");
			CPlayer_SendMoveDeny(this, sequence);
			return 0;
		}
	}

	// 0x0045109e: mov ecx,1; test ecx,ecx; je (always-true dead code)
	if (1) {
		if ((sequence & 0xFF) == 0)
			CPlayer_SetMovePrevented(this, 0);

		if (CPlayer_IsMovePrevented(this)) {
			CPlayer_SendMoveDeny(this, sequence);
			return 0;
		}

		if (elapsed == 0)
			moveRate = 500;
		else
			moveRate = 500 / elapsed;

		if (!VT_IsDead(item)) {
			staminaCost = 100;
			if (moveRate > 125)
				staminaCost = 400;
			((void (*)(void *, int))VT_FN(item, VT_HANDLE_STAM_DRAIN))(item, staminaCost);
		}

		CTerrainManager_MovePlayer(item, (int)(direction & 0xFF), sequence);

		this->movementTimers[this->movementIndex] = CTimeManager_GetTickCount();
		this->movementIndex = (this->movementIndex + 1) % 5;
		return 1;
	}

	// 0x00451186: deny path (unreachable due to if(1) above)
	CPlayer_SendMoveDeny(this, sequence);
	return 0;
}

/*
 * 0x0045119A - CPlayer::RegenTick
 *
 * Per-tick regen for an online player: ticks lifeclock, cooldown,
 * action state, mana/HP/stamina regen, and excess-stat drain. Dead
 * players skip all regen.
 */
static void
CPlayer_RegenTick(CPlayer *this)
{
	CMobile *mob;
	int hunger;

	mob = &this->mobile;

	CMobile_IncrementLifeclock(mob);

	if (this->cooldownCounter > 0)
		this->cooldownCounter--;

	if (mob->actionState > 0)
		mob->actionState--;

	// Binary dead code at 0x004511ED: fame decay (fame -= fame >> 7) never executes.

	if (VT_IsDead((CItem *)this))
		return;

	if ((int32_t)mob->notorietyChangeTimer > 0)
		mob->notorietyChangeTimer--;

	mob->manaRegenTimer--;

	if (!CPlayer_IsEditing(this) && mob->mana > mob->maxMana)
		((uint32_t (*)(void *, int))VT_FN((CItem *)mob, VT_SET_MANA))(mob, (int)mob->mana - 1);

	if (mob->manaRegenTimer < 0) {
		if (feat(FEAT_SKILL_MEDITATION)) {
			int meditSkill = CMobile_GetSkillValue(mob, 46, 0);
			int timer = 20 - meditSkill / 100;
			if (timer < 10)
				timer = 10;
			mob->manaRegenTimer = timer;
		} else {
			mob->manaRegenTimer = 20;
		}
		if (mob->mana < mob->maxMana) {
			if (feat(FEAT_SKILL_MEDITATION)) {
				int gain = CResourceEntity_HasTag((CItem *)mob, "meditating", 7) ? 2 : 1;
				((uint32_t (*)(void *, int))VT_FN((CItem *)mob, VT_SET_MANA))(mob, (int)mob->mana + gain);
				if (mob->mana > mob->maxMana)
					((uint32_t (*)(void *, int))VT_FN((CItem *)mob, VT_SET_MANA))(mob, (int)mob->maxMana);
				if (mob->mana >= mob->maxMana && CResourceEntity_HasTag((CItem *)mob, "meditating", 7)) {
					Entity_ExecuteEvent(&((CItem *)mob)->resourceEntity.entity, 0x16, (int)((CItem *)mob)->serial, "manaFull", "v");
				}
			} else {
				((uint32_t (*)(void *, int))VT_FN((CItem *)mob, VT_SET_MANA))(mob, (int)mob->mana + 1);
			}
		}
		if (feat(FEAT_SKILL_MEDITATION)) {
			if (!CResourceEntity_HasTag((CItem *)mob, "meditating", 7) && CMobile_GetSkillValue(mob, 46, 0) > 0 && mob->mana < mob->maxMana) {
				CMobile_TestSkillInternal(mob, 46, 50, 0);
			}
		}
	}

	// HP regen timer (0x27C)
	mob->hpRegenTimer--;

	if (!CPlayer_IsEditing(this) && mob->hp > mob->maxHp)
		((uint32_t (*)(void *, int, int))VT_FN((CItem *)mob, VT_SET_HP))(mob, (int)mob->hp - 1, 0);

	if (mob->hpRegenTimer < 0) {
		hunger = mob->hunger;
		if (hunger > 25) {
			hunger = 25;
			mob->hunger = 25;
		}

		// Binary computes: 50 - hunger*2 + 30 = 80 - hunger*2
		mob->hpRegenTimer = 80 - hunger * 2;

		// 1/20 chance to decrease hunger each HP regen cycle
		if (hunger > 0 && GetRandom(20) == 1)
			mob->hunger--;

		if (mob->hp < mob->maxHp) {
			((uint32_t (*)(void *, int, int))VT_FN((CItem *)mob, VT_SET_HP))(mob, (int)mob->hp + 1, 0);
			((void (*)(void *))VT_FN((CItem *)this, VT_SEND_HP_UPDATE))(this);
		}
	}

	((void (*)(void *))VT_FN((CItem *)mob, VT_STAM_REGEN))(mob);
}

/*
 * 0x00451407 - Effect checker
 *
 * Checks if "poisoned" and "curse" tags have expired, and clears the
 * corresponding statusFlags bits if so. The binary does not deal poison
 * damage here - damage is handled entirely by scripts (which the demo
 * doesn't ship). Called every 120 ticks via effectTickCounter.
 */
static void
CPlayer_EffectChecker(CPlayer *this)
{
	CScript *script;

	script = CScriptManager_FindOrLoad(&g_ScriptManager, "poisoned");
	if (!CResourceEntity_HasScriptClass(&this->mobile.container.item, script)) {
		if (CMobile_GetMobileFlags(&this->mobile) & 0x04)
			Script_setPoisoned(this->mobile.container.item.serial, 0);
	}
	script = CScriptManager_FindOrLoad(&g_ScriptManager, "curse");
	if (!CResourceEntity_HasScriptClass(&this->mobile.container.item, script)) {
		if (CMobile_GetMobileFlags(&this->mobile) & 0x08)
			Script_setCursed(this->mobile.container.item.serial, 0);
	}
}

/*
 * 0x004514A2 - CPlayer::Heartbeat
 *
 * Top-level per-player tick. Called once per server tick for each online
 * player. Handles regen, effect checking, and spatial grid light/season/
 * weather change detection.
 */
void
CPlayer_Heartbeat(CPlayer *this)
{
	int blockIdx;
	int lightLevel;
	uint16_t blockSeason, blockWeather;
	CBlock *blk;
	uint8_t obuf[16];

	CPlayer_RegenTick(this);

	this->effectTickCounter++;
	if (this->effectTickCounter > 0x78) {
		CPlayer_EffectChecker(this);
		this->effectTickCounter = 0;
	}

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &this->mobile.container.item.resourceEntity.entity.location, 0);

	blk = &g_SpatialGrid.cells[blockIdx];
	lightLevel = (int)blk->lightLevel;
	if (lightLevel == 0)
		lightLevel = g_globalLightLevel;

	if (lightLevel != (int)this->lastLightLevel) {
		this->lastLightLevel = (uint16_t)lightLevel;
		PacketManager_MakePacket_SUNLIGHT(obuf, (uint8_t)this->lastLightLevel);
		Entity_BroadcastPacket((CItem *)this, this->mobile.container.item.serial, obuf);
	}

	blockSeason = blk->weatherSeason;
	if (blockSeason != this->lastSeason) {
		this->lastSeason = blockSeason;
		PlayMusicToEntity((CItem *)this, this->lastSeason);
	}

	if (CTimeManager_IsDaytime()) {
		blockWeather = blk->weatherDay;
		if (blockWeather != this->lastWeather) {
			this->lastWeather = blockWeather;
			SendSoundAtEntity((CItem *)this, this->lastWeather, 0);
		}
	} else {
		blockWeather = blk->weatherNight;
		if (blockWeather != this->lastWeather) {
			this->lastWeather = blockWeather;
			SendSoundAtEntity((CItem *)this, this->lastWeather, 0);
		}
	}

	// 0x004516B3-0x004516BE: call 0x00454ADD (empty stub in binary).
	CPlayer_HeartbeatCleanup(this);
}

/*
 * 0x004516BF - CPlayer::SystemMessage
 *
 * Sends a hue 0x3B2 system-speech packet to the player.
 */
void
CPlayer_SystemMessage(CPlayer *this, const char *message)
{
	uint8_t obuf[0x42C];

	PacketManager_MakePacket_TEXT(&obuf[0], NULL, (CItem *)&this->mobile.container.item, 6, message, 0x03B2, 3);
	SendToClient((CItem *)&this->mobile.container.item, obuf, -1);
}

/*
 * 0x00451711 - CPlayer::SystemMessage_Unicode
 *
 * Sends a hue 0x3B2 unicode system-speech packet to the player.
 */
void
CPlayer_SystemMessage_Unicode(CPlayer *player, uint16_t *text)
{
	uint8_t buf[0x830];

	PacketManager_MakePacket_TEXT_UNICODE(buf, NULL, (CItem *)player, 6, text, 0x3B2, 3, 0);
	SendToClient((CItem *)player, buf, -1);
}

/*
 * 0x00451765 - CPlayer::SendFormattedMessage
 *
 * Sends a TEXT packet with caller-supplied hue, font, and speech type.
 */
void
CPlayer_SendFormattedMessage(CPlayer *player, char *text, int hue, int font, int speechType)
{
	uint8_t buf[0x42C];

	PacketManager_MakePacket_TEXT(buf, NULL, (CItem *)&player->mobile.container.item, (uint8_t)speechType, text, (uint16_t)hue, (uint16_t)font);
	SendToClient((CItem *)&player->mobile.container.item, buf, -1);
}

// 0x00617238 - PreDeleteClean lookup (14 entries for bodyTypes 0-13)
static const uint32_t g_PreDeleteCleanTable[14] = { 6, 5, 3, 2, 2, 8, 4, 5, 8, 7, 3, 7, 7, 8 };

/*
 * 0x004517BC - CPlayer vtable[0x1BC] PreDeleteClean
 *
 * Returns a byte value from a lookup table indexed by bodyType,
 * or 0 if bodyType >= 14.
 */
int
CPlayer_PreDeleteClean_VT(CPlayer *self)
{
	uint16_t bodyType;
	uint32_t val;

	bodyType = CResourceEntity_GetBodyType((CItem *)self) & 0xFFFF;
	if (bodyType >= 14) {
		val = 0;
	} else {
		bodyType = CResourceEntity_GetBodyType((CItem *)self) & 0xFFFF;
		val = g_PreDeleteCleanTable[bodyType];
	}
	return (uint8_t)val;
}

/*
 * 0x004517FE - CPlayer::RestoreOldBodyType
 *
 * Restores player body type and hue from saved tags "oldBodyType" and
 * "oldHue". After restoring, validates the body type is in the human
 * range (0x190-0x193); if not, forces it to 0x190 (male human).
 */
void
CPlayer_RestoreOldBodyType(CPlayer *this)
{
	int oldBody, oldHue;
	int bt;

	if (CItem_GetTagInt((CItem *)this, "oldBodyType", &oldBody) == 1)
		CPlayer_SetBodyType(this, (uint16_t)oldBody);

	if (CItem_GetTagInt((CItem *)this, "oldHue", &oldHue) == 1)
		this->mobile.container.item.resourceEntity.entity.color = (uint16_t)oldHue;

	bt = CResourceEntity_GetBodyType((CItem *)this) & 0xFFFF;
	if (bt < 0x190 || bt > 0x193)
		CPlayer_SetBodyType(this, 0x190);
}

/*
 * 0x00451882 - CPlayer::ShouldDropOnDeath
 *
 * Returns 1 if an item should drop to the corpse on death. Items
 * with ExcludedAmount, newbie starteq/valueless items on non-murderers,
 * and items in excluded parents are kept.
 */
static int
CPlayer_ShouldDropOnDeath(CMobile *mob, CItem *item)
{
	int i;
	CScript *script;

	if (((int (*)(void *))VT_FN(item, VT_EXCLUDED_AMOUNT))(item))
		return 0;

	if (!CMobile_IsMurderer(mob)) {
		for (i = 0; i < 1; i++) {
			script = CScriptManager_FindOrLoad(&g_ScriptManager, g_DeathDropScripts[i]);
			if (script != NULL) {
				if (CResourceEntity_HasScriptClass(item, script))
					return 0;
			}
		}

		for (i = 0; i < 1; i++) {
			if (CResourceEntity_HasTag(item, g_DeathDropTags[i], g_DeathDropTagTypes[i]))
				return 0;
		}
	}

	if (item->parent != NULL) {
		if (((int (*)(void *))VT_FN(item->parent, VT_EXCLUDED_AMOUNT))(item->parent))
			return 0;
	}

	return 1;
}

/*
 * 0x00451970 - CPlayer::ProcessDeathItems
 *
 * Walks a container recursively on death, moving droppable items to
 * the corpse and collecting kept items in the retained-items vector.
 */
static void
CPlayer_ProcessDeathItems(CMobile *mob, CItem *container, CItem *corpse, CVector *keptItems, int doMove)
{
	CLocation tmpLoc;
	CItem *child;
	CItem *next;

	CLocation_Init(&tmpLoc);

	child = ((CContainer *)container)->contents;
	while (child != NULL) {
		next = child->spatialNext;

		if (VT_IsMobile2(child))
			CPlayer_ProcessDeathItems(mob, child, corpse, keptItems, 0);

		if (CPlayer_ShouldDropOnDeath(mob, child)) {
			if (doMove) {
				if (child->resourceEntity.entity.removedFromWorld == 0)
					((void (*)(void *))VT_FN(child, VT_HIDE))(child);
				CLocation_Set(&tmpLoc, -1, -1, 0);
				((void (*)(void *, void *, void *))VT_FN(child, VT_ADD_TO_CONTAINER))(child, corpse, &tmpLoc);
			}
		} else {
			if (child->parent != NULL && ((int (*)(void *))VT_FN(child->parent, VT_EXCLUDED_AMOUNT))(child->parent)) {
				// skip VT_HIDE, go directly to push
			} else {
				if (child->resourceEntity.entity.removedFromWorld == 0)
					((void (*)(void *))VT_FN(child, VT_HIDE))(child);
			}

			CVector_PushBack(keptItems, (uintptr_t)child);
		}

		child = next;
	}
}

/*
 * 0x00451A6F - CPlayer::OnDeath
 *
 * CPlayer vtable[0x1B8] override. Handles player death: creates corpse,
 * transfers equipment, fires death events, converts player to ghost.
 * arg1 (attacker) may be NULL. arg2 (flag): 1=drop loot, 0=no loot.
 *
 * MODIFIED: binary sets statusFlags bit 0x02 (ghost/frozen) and relies
 * on the client sending packet 0x2C to clear it via ProcessDeath. Clients
 * 1.26.4b+ removed the death gump and packet 0x2C entirely, leaving the
 * ghost permanently frozen. Added ScheduleEvent for TIMER_EVENT_UNFREEZE
 * (type 14) with a 3-tick delay to auto-clear the flag.
 */
void
CPlayer_OnDeath(CPlayer *this, CItem *attacker, int flag)
{
	CLocation tmpLoc;
	uint8_t deathPacket[4];
	CLocation savedLoc;
	uint32_t corpseSerial;
	CItem *corpse;
	CItem *item;
	uint32_t attackerSerial;
	uint8_t deathAnimPacket[16];
	int deathAnimFlag;
	int lockedContSerial;
	CLocation lockedContLoc;
	CItem *lockedContTarget;
	CItem *tempFind;
	int i;
	CVector keptItems;
	uintptr_t *keptIter;
	CItem *keptItem;
	CItem *origItem;
	CLocation itemLoc;
	CItem *ghostItem;
	uint16_t bodyType;
	CLocation *mobLoc;

	if (VT_IsDead((CItem *)this))
		return;

	if (CMobile_IsMounted(&this->mobile))
		CMobile_Dismount(&this->mobile);

	CLocation_Init(&tmpLoc);
	{
		CCorpse *mem = (CCorpse *)malloc(sizeof(CCorpse));
		if (mem != NULL) {
			CCorpse_Constructor(mem);
			corpse = (CItem *)mem;
		} else {
			corpse = NULL;
		}
	}

	CEntity_SetBodyType(corpse, CORPSE_BODYTYPE);
	CCorpse_SetCorpseBodyType((CCorpse *)corpse, CResourceEntity_GetBodyType((CItem *)this));
	CItem_SetDirectionVT(corpse, (int)(uint8_t)this->mobile.direction);

	corpse->resourceEntity.entity.color = ((CItem *)this)->resourceEntity.entity.color;

	CMobile_SetCorpseLookAtText(&this->mobile, corpse);
	deathAnimFlag = 0;
	if (corpse != NULL) {
		if (((int (*)(void *))VT_FN(corpse, VT_GET_DIRECTION))(corpse) & 0x80)
			deathAnimFlag = 1;
	}

	PacketManager_MakePacket_DEATH_ANIM(deathAnimPacket, ((CItem *)this)->serial, corpse->serial, deathAnimFlag);
	CPlayerList_BroadcastInRange(deathAnimPacket, &((CItem *)this)->resourceEntity.entity.location, 0x12, (CPlayer *)this);

	if (this->mobile.equipment[0] != NULL) {
		item = this->mobile.equipment[0];

		((void (*)(void *))VT_FN(item, VT_HIDE))(item);

		lockedContSerial = 0;
		if (CItem_GetTagInt(item, "lockedContainer", &lockedContSerial)) {
			CLocation_Init(&lockedContLoc);
			CLocation_Set(&lockedContLoc, -1, -1, 0);
			lockedContTarget = CWorld_FindBySerial(g_World, (uint32_t)lockedContSerial);
			if (lockedContTarget != NULL && VT_IsMobile2(lockedContTarget)) {
				((void (*)(void *, void *, void *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, lockedContTarget, &lockedContLoc);
			} else {
				mobLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)this, VT_GET_LOCATION))(this);
				((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, mobLoc);
			}
		} else {
			mobLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)this, VT_GET_LOCATION))(this);
			((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, mobLoc);
		}
	}

	CMobile_StopCombat(&this->mobile);
	CMobile_RemoveFromAllCombatLists(&this->mobile);

	((void (*)(void *))VT_FN((CItem *)this, VT_CANCEL_TRADE))(this);
	if (((CItem *)this)->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPreModify((CItem *)this);
	CPlayer_TransferResourcesToCorpse(this, corpse);
	if (((CItem *)this)->resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPostModifyIfActive((CItem *)this);

	corpseSerial = 0;
	if (corpse != NULL) {
		corpseSerial = corpse->serial;

		mobLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)this, VT_GET_LOCATION))(this);
		((void (*)(void *, void *))VT_FN(corpse, VT_SET_LOCATION))(corpse, mobLoc);

		Entity_AttachScript(corpse, "corpse", 0);
		tempFind = CWorld_FindBySerial(g_World, corpseSerial);
		if (tempFind != NULL && ((int (*)(void *))VT_FN(tempFind, VT_HAS_CORPSE_EQ))(tempFind))
			corpse = tempFind;
		else
			corpse = NULL;
	}

	if (attacker != NULL)
		attackerSerial = attacker->serial;
	else
		attackerSerial = 0;

	if (!Entity_ExecuteEvent(&((CItem *)this)->resourceEntity.entity, 0x04, attackerSerial, corpseSerial)) {
		tempFind = CWorld_FindBySerial(g_World, corpseSerial);
		if (tempFind != NULL && ((int (*)(void *))VT_FN(tempFind, VT_HAS_CORPSE_EQ))(tempFind))
			corpse = tempFind;
		else
			corpse = NULL;

		// 0x00451e5f: delete corpse (binary has redundant NULL check)
		if (corpse != NULL) {
			if (corpse != NULL)
				((void (*)(void *))VT_FN(corpse, VT_DELETE))(corpse);
		}
		return;
	}

	if (corpse != NULL) {
		Entity_ExecuteEvent(&corpse->resourceEntity.entity, 0x04, attackerSerial, ((CItem *)this)->serial);

		tempFind = CWorld_FindBySerial(g_World, corpseSerial);
		if (tempFind != NULL && ((int (*)(void *))VT_FN(tempFind, VT_HAS_CORPSE_EQ))(tempFind))
			corpse = tempFind;
		else
			corpse = NULL;
	}

	mobLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)this, VT_GET_LOCATION))(this);
	BroadcastEventToNearby(mobLoc, 8, 5, attackerSerial, ((CItem *)this)->serial, corpseSerial);

	item = CWorld_FindBySerial(g_World, corpseSerial);
	if (item != NULL && ((int (*)(void *))VT_FN(item, VT_HAS_CORPSE_EQ))(item))
		corpse = item;
	else
		corpse = NULL;

	if (corpse == NULL)
		goto final_section;

	if (flag == 0)
		goto post_loop;

	this->npcTimer = (uintptr_t)corpse;
	CLocation_SetLoc(&this->startLocation, CEntity_GetLocation(&((CItem *)this)->resourceEntity.entity));

	ScheduleEvent(0x690, corpse->serial, 2, 0, 0);

	for (i = 0; i < 0x1A; i++) {
		((CCorpse *)corpse)->equipSlots[i] = 0;

		if (this->mobile.equipment[i] == NULL)
			continue;

		item = this->mobile.equipment[i];
		bodyType = CEntity_GetBodyType(item) & 0xFFFF;

		if (bodyType == 0xE75) {
			if (!VT_IsMobile2(item))
				continue;
			CVector_Constructor(&keptItems, "");
			CPlayer_ProcessDeathItems(&this->mobile, item, corpse, &keptItems, 1);

			keptIter = (uintptr_t *)keptItems.begin;
			while (keptIter != (uintptr_t *)keptItems.end) {
				keptItem = (CItem *)(uintptr_t)*keptIter;
				if (keptItem->parent != NULL && ((int (*)(void *))VT_FN(keptItem->parent, VT_EXCLUDED_AMOUNT))(keptItem->parent)) {
					keptIter++;
					continue;
				}

				if (keptItem->resourceEntity.entity.removedFromWorld == 0)
					((void (*)(void *))VT_FN(keptItem, VT_HIDE))(keptItem);
				CLocation_Set(&tmpLoc, -1, -1, 0);
				((void (*)(void *, void *, void *))VT_FN(keptItem, VT_ADD_TO_CONTAINER))(keptItem, item, &tmpLoc);
				keptIter++;
			}

			CVector_Destructor(&keptItems);
			continue;
		}

		if (i == 0xB || i == 0x10) {
			origItem = item;
			item = CWorld_CreateItem(g_World, CEntity_GetBodyType(origItem));
			if (item != NULL) {
				item->resourceEntity.entity.color = origItem->resourceEntity.entity.color;
				CItem_Setup(item, 1, CEntity_GetLocation(&((CItem *)this)->resourceEntity.entity), 0, 1);
				mobLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)this, VT_GET_LOCATION))(this);
				CLocation_SetLoc(&itemLoc, mobLoc);
				((void (*)(void *, void *))VT_FN(item, VT_SET_LOCATION))(item, &itemLoc);
				if (!ValidateInWorld(item))
					item = NULL;
			}
		}

		if (item != NULL && CPlayer_ShouldDropOnDeath(&this->mobile, item)) {
			((CCorpse *)corpse)->equipSlots[i] = item->serial;
			if (item->resourceEntity.entity.removedFromWorld == 0)
				((void (*)(void *))VT_FN(item, VT_HIDE))(item);
			CLocation_Set(&tmpLoc, -1, -1, 0);
			((void (*)(void *, void *, void *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, corpse, &tmpLoc);
		} else {
			if (this->mobile.equipment[21] != NULL && VT_IsMobile2(this->mobile.equipment[21])) {
				if (item->resourceEntity.entity.removedFromWorld == 0)
					((void (*)(void *))VT_FN(item, VT_HIDE))(item);
				CLocation_Set(&tmpLoc, -1, -1, 0);
				((void (*)(void *, void *, void *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, this->mobile.equipment[21], &tmpLoc);
			}
		}
	}

post_loop:
	((void (*)(void *))VT_FN(corpse, VT_DETACH_SPATIAL))(corpse);
	mobLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)this, VT_GET_LOCATION))(this);
	((void (*)(void *, void *))VT_FN(corpse, VT_DROP_AT_FEET))(corpse, mobLoc);

final_section:
	CMobile_SetStatusFlag(&this->mobile, 2, 1);

	CPlayer_DeathCountDecay(this);

	PacketManager_MakePacket_DEATH(deathPacket, 0);
	SendToClient((CItem *)this, deathPacket, -1);

	// Custom: auto-unfreeze ghost after 3 ticks if client does not
	// send 0x2C. Idempotent - if the client responds first,
	// ProcessDeath clears the flag and the timer becomes a no-op.
	ScheduleEvent(3, ((CItem *)this)->serial, TIMER_EVENT_UNFREEZE, 0, 0);

	ghostItem = CWorld_CreateItem(g_World, 0x204E);
	if (ghostItem != NULL) {
		ghostItem->resourceEntity.entity.color = 0x8FD;
		if (((int (*)(void *, void *, int))VT_FN(ghostItem, VT_EQUIP_ON_MOBILE))(ghostItem, this, 0x16) == 1) {
			CItem_Setup(ghostItem, 1, CEntity_GetLocation(&((CItem *)this)->resourceEntity.entity), 0, 1);
			if (ValidateInWorld(ghostItem))
				CItem_DecayProcess(ghostItem);
			else
				ghostItem = NULL;
		} else {
			if (ghostItem != NULL)
				((void (*)(void *))VT_FN(ghostItem, VT_DELETE))(ghostItem);
			ghostItem = NULL;
		}
	}

	mobLoc = ((CLocation * (*)(void *)) VT_FN((CItem *)this, VT_GET_LOCATION))(this);
	CLocation_SetLoc(&savedLoc, mobLoc);

	((void (*)(void *))VT_FN((CItem *)this, VT_HIDE))(this);
	CPlayer_RestoreOldBodyType(this);

	// 0x004523e7: convert to ghost body type. Binary calls GetBodyType twice.
	if ((uint16_t)CResourceEntity_GetBodyType((CItem *)this) == 0x190)
		CPlayer_SetBodyType(this, 0x192);
	if ((uint16_t)CResourceEntity_GetBodyType((CItem *)this) == 0x191)
		CPlayer_SetBodyType(this, 0x193);

	this->pflags &= ~0x20;

	((void (*)(void *, void *))VT_FN((CItem *)this, VT_DROP_AT_FEET))((CItem *)this, &savedLoc);

	CMobile_SetAllCurrentStats(&this->mobile, 0);

	// 0x00452472: if flag==0, delete corpse (binary has redundant NULL check)
	if (flag == 0) {
		if (corpse != NULL) {
			if (corpse != NULL)
				((void (*)(void *))VT_FN(corpse, VT_DELETE))(corpse);
		}
	}
}

/*
 * 0x004524AF - CPlayer::IsLoaded
 *
 * Returns 1 when PlayerIsLoaded (pflags & 0x08) is set, 0 otherwise.
 */
int
CPlayer_IsLoaded(CPlayer *this)
{
	return (this->pflags & PlayerIsLoaded) != 0;
}

/*
 * 0x004524CC - CPlayer vtable[0x210] OnDeathWrap
 *
 * If not dead, calls vtable OnDeath with attacker args, then plays
 * death sound based on sex (male=0x151, female=0x15A).
 */
void
CPlayer_OnDeathWrap_VT(CPlayer *self, uintptr_t attacker, int deathFlag)
{
	if (VT_IsDead((CItem *)self))
		return;
	((void (*)(void *, uintptr_t, int))VT_FN((CItem *)self, VT_ON_DEATH))(self, attacker, deathFlag);
	if (self->mobile.sex == 0) {
		PlaySoundAtEntity((CItem *)self, 0x15A, 0);
	} else {
		PlaySoundAtEntity((CItem *)self, 0x151, 0);
	}
}

/*
 * 0x0045253A - CPlayer vtable[0x1C0] SetHP
 *
 * If dead, returns current HP. Otherwise sets HP, clamps to
 * [0, maxHp], broadcasts if changed. Returns new HP.
 */
uint32_t
CPlayer_SetHP_VT(CPlayer *self, int value)
{
	uint32_t old;

	if (VT_IsDead((CItem *)self))
		return self->mobile.hp;
	old = self->mobile.hp;
	self->mobile.hp = (uint32_t)value;
	if ((int32_t)self->mobile.hp < 0)
		self->mobile.hp = 0;
	if (self->mobile.hp > self->mobile.maxHp)
		self->mobile.hp = self->mobile.maxHp;
	if (old != self->mobile.hp)
		CMobile_BroadcastStatUpdate(&self->mobile, 0);
	return self->mobile.hp;
}

/*
 * 0x004525DE - CPlayer vtable[0x1C4] SetMaxHP
 *
 * Sets maxHp with 60000 cap, clamps to >= 0, broadcasts if changed.
 */
uint32_t
CPlayer_SetMaxHP_VT(CPlayer *self, int value)
{
	uint32_t old;

	old = self->mobile.maxHp;
	if (value < 60000)
		self->mobile.maxHp = (uint32_t)value;
	if ((int32_t)self->mobile.maxHp < 0)
		self->mobile.maxHp = 0;
	if (old != self->mobile.maxHp)
		CMobile_BroadcastStatUpdate(&self->mobile, 0);
	return self->mobile.maxHp;
}

/*
 * 0x00452648 - CPlayer vtable[0x1D0] SetMana
 *
 * Sets mana, clamps to [0, maxMana], broadcasts if changed.
 * Binary checks arg < 0 before clamping to max (different order
 * from CMobile).
 */
uint32_t
CPlayer_SetMana_VT(CPlayer *self, int value)
{
	uint32_t old;

	old = self->mobile.mana;
	self->mobile.mana = (uint32_t)value;
	if (value < 0)
		self->mobile.mana = 0;
	if (self->mobile.mana > self->mobile.maxMana)
		self->mobile.mana = self->mobile.maxMana;
	if (old != self->mobile.mana)
		CMobile_BroadcastStatUpdate(&self->mobile, 1);
	return self->mobile.mana;
}

/*
 * 0x004526C9 - CPlayer vtable[0x1D4] SetMaxMana
 *
 * Sets maxMana with 60000 cap, clamps to >= 0, broadcasts if changed.
 */
uint32_t
CPlayer_SetMaxMana_VT(CPlayer *self, int value)
{
	uint32_t old;

	old = self->mobile.maxMana;
	if (value < 60000)
		self->mobile.maxMana = (uint32_t)value;
	if ((int32_t)self->mobile.maxMana < 0)
		self->mobile.maxMana = 0;
	if (old != self->mobile.maxMana)
		CMobile_BroadcastStatUpdate(&self->mobile, 1);
	return self->mobile.maxMana;
}

/*
 * 0x00452733 - CPlayer vtable[0x1C8] SetStamina
 *
 * Sets stamina, clamps to [0, maxStamina], broadcasts if changed.
 */
uint32_t
CPlayer_SetStamina_VT(CPlayer *self, int value)
{
	uint32_t old;

	old = self->mobile.stamina;
	self->mobile.stamina = (uint32_t)value;
	if ((int32_t)self->mobile.stamina > (int32_t)self->mobile.maxStamina)
		self->mobile.stamina = self->mobile.maxStamina;
	if ((int32_t)self->mobile.stamina < 0)
		self->mobile.stamina = 0;
	if (old != self->mobile.stamina)
		CMobile_BroadcastStatUpdate(&self->mobile, 2);
	return self->mobile.stamina;
}

/*
 * 0x004527BA - CPlayer vtable[0x1CC] SetMaxStamina
 *
 * Sets maxStamina, clamps to >= 0, broadcasts if changed.
 * No 60000 cap (unlike SetMaxHP/SetMaxMana).
 */
uint32_t
CPlayer_SetMaxStamina_VT(CPlayer *self, int value)
{
	uint32_t old;

	old = self->mobile.maxStamina;
	self->mobile.maxStamina = (uint32_t)value;
	if ((int32_t)self->mobile.maxStamina < 0)
		self->mobile.maxStamina = 0;
	if (old != self->mobile.maxStamina)
		CMobile_BroadcastStatUpdate(&self->mobile, 2);
	return self->mobile.maxStamina;
}

/*
 * 0x0045281B - CPlayer::SendDeathIfGhost
 *
 * If the player is dead and a ghost, broadcasts a DEATH packet
 * (flag=0) to nearby clients. Returns 1 if broadcast, 0 otherwise.
 */
int
CPlayer_SendDeathIfGhost(CPlayer *this)
{
	uint8_t buf[4];

	if (!VT_IsDead((CItem *)this))
		return 0;
	if (!CPlayer_IsGhost(this))
		return 0;

	PacketManager_MakePacket_DEATH(buf, 0);
	SendToClient((CItem *)this, buf, -1);
	return 1;
}

/*
 * 0x0045286F - CPlayer::SendAppearance
 *
 * Places the player into the world at login. Validates the location
 * via terrain check, fires LoginAppearance (0x3F), handles death
 * state, startup scripts, editing state, and help-queue notification.
 * During world load, skips the terrain check and just broadcasts to
 * nearby players.
 */
void
CPlayer_SendAppearance(CPlayer *this)
{
	CLocation loc;
	CItem *self = &this->mobile.container.item;
	CEntity *ent = &self->resourceEntity.entity;
	int moveType, height;
	int16_t newZ;

	// Restores player (and all equipment/contents) from the entity
	// manager archive back into the world (serial hash, spatial grid,
	// mobile list, template chain). No-op if not archived.
	EntityManager_RestoreBySerial(self->serial);

	CLocation_SetLoc(&loc, CEntity_GetLocation(ent));

	if ((int16_t)loc.x == -1) {
		loc.x = (uint16_t)g_mapStartX;
		loc.y = (uint16_t)g_mapStartY;
		loc.z = 0;
		CLocation_SetLoc(&ent->location, &loc);
	}

	if (!ent->removedFromWorld) {
		((void (*)(void *))VT_FN(self, VT_DETACH_SPATIAL))(self);
	}

	if (g_isLoadingWorld) {
		((void (*)(void *, CLocation *))VT_FN(self, VT_SET_LOCATION))(self, &loc);

		Entity_ExecuteEvent(ent, LoginAppearance);

		CPlayer_SendDeathIfGhost(this);
		{
			CVector vec;
			CVector_Constructor(&vec, "\x01");
			GetNearbyPlayers(&vec, &loc, 0x12);
			((void (*)(void *, CVector *, int))VT_FN(self, VT_NOTIFY_NEARBY))(self, &vec, 1);
			CVector_Destructor(&vec);
		}
	}

	if (g_isLoadingWorld && !g_hasLoadedWorld) {
		goto common_end;
	}

	if (!ent->removedFromWorld) {
		((void (*)(void *))VT_FN(self, VT_DETACH_SPATIAL))(self);
	}

	moveType = ((int (*)(void *))VT_FN(self, VT_GET_MOVEMENT_TYPE))(self) & 0xFF;
	height = ((int (*)(void *))VT_FN(self, VT_GET_HEIGHT))(self);

	newZ = (int16_t)CTerrainManager_CanWalkWrapper(loc, (int16_t)loc.z - 8, (int16_t)loc.z + 8, height, moveType, self, 0);
	loc.z = newZ;

	if (newZ != -128) {
		// Binary calls CEntity_GetLocation here but discards result
		CEntity_GetLocation(ent);

		((void (*)(void *, CLocation *))VT_FN(self, VT_DROP_AT_FEET))(self, &loc);

		if (!g_isLoadingWorld) {
			CPlayer_AttachStartupScripts(this);
		}

		Entity_ExecuteEvent(ent, LoginAppearance);

		CPlayer_SendDeathIfGhost(this);
	} else {
		((void (*)(void *, CLocation *))VT_FN(self, VT_SET_LOCATION))(self, &loc);

		Entity_ExecuteEvent(ent, LoginAppearance);

		CPlayer_SendDeathIfGhost(this);

		((void (*)(void *))VT_FN(self, VT_DETACH_SPATIAL))(self);

		CPlayer_ReturnToHome(this);
	}

common_end:
	CEntity_RemoveTimer(self, 3, 0);

	this->pflags |= PlayerIsOnline;
	this->pflags &= ~0x10800;

	if (CPlayer_IsEditing(this) && !g_isLoadingWorld) {
		CPlayer_DisableEditing(this);
	}

	if (CPlayer_IsCounselor(this)) {
		CHelpQueue_NotifyLogin(&g_HelpQueue, this);
	}

	g_isLoadingWorld = 0;

	this->pingTimer = 0;
}

/*
 * 0x00452B0A - SavePlayerToFile (binary name)
 *
 * Player logout handler. Iterates all players to send FRIENDNOTIFY for snoop
 * cleanup, cancels trade sessions, clears the online flag, restores the
 * held item (equipment[0]) to a locked container or drops it at feet,
 * clears the GM/counselor/editing flags, then disconnects.
 */
void
CPlayer_LogOut(CPlayer *this, int save)
{
	CPlayer *p;
	uint32_t i;
	uint32_t serial;

	serial = this->mobile.container.item.serial;

	// Phase 1: Walk all players and send FRIENDNOTIFY (0x6A) to any player
	// whose friendList contains this player's serial, if this player allows
	// that observer. Binary: 0x00452B13-0x00452B9D.
	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		for (i = 0; i < p->friendCount; i++) {
			if (p->friendList[i] == serial) {
				if (CPlayer_IsFriendAllowed(this, p)) {
					uint8_t obuf[16];
					PacketManager_MakePacket_FRIENDNOTIFY(obuf, (uint8_t)i, 0);
					SendToClient((CItem *)p, obuf, -1);
				}
			}
		}
	}

	((void (*)(void *))VT_FN(&this->mobile.container.item, VT_CANCEL_TRADE))(&this->mobile.container.item);

	// Phase 3: Clear online flag (binary: pflags &= ~0x04)
	this->pflags &= ~PlayerIsOnline;

	// Phase 4: Restore held item (equipment[0]) to locked container or
	// drop at player's feet. Binary: 0x00452BC5-0x00452C89.
	if (this->mobile.equipment[0] != NULL) {
		CItem *entity = this->mobile.equipment[0];

		((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);
		int lockedSerial = 0;
		if (CItem_GetTagInt(entity, "lockedContainer", &lockedSerial)) {
			CLocation tmpLoc;
			CLocation_Init(&tmpLoc);
			CLocation_Set(&tmpLoc, -1, -1, 0);
			CItem *locked = CWorld_FindBySerial(g_World, (uint32_t)lockedSerial);
			if (locked != NULL && VT_IsMobile2(locked)) {
				((void (*)(void *, CItem *, CLocation *))VT_FN(entity, VT_ADD_TO_CONTAINER))(entity, locked, &tmpLoc);
			} else {
				((void (*)(void *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, CEntity_GetLocation(&this->mobile.container.item.resourceEntity.entity));
			}
		} else {
			((void (*)(void *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, CEntity_GetLocation(&this->mobile.container.item.resourceEntity.entity));
		}
	}

	CHelpQueue_UpdateLevel(&g_HelpQueue, CMobile_GetSerial(&this->mobile), 100);

	if (CPlayer_IsGameMaster(this)) {
		CPlayer_RemoveFromGMCallQueue(this);
		save = 0;
	}

	if (CPlayer_IsCounselor(this)) {
		CHelpQueue_OnLogout(&g_HelpQueue, this);
		CHelpQueue_DecrCounselors(&g_HelpQueue, (CPlayer *)this);
		save = 0;
	}

	if (CPlayer_IsEditing(this)) {
		EventLogger_Log(&g_EventLogger, this->accountNum, (uint32_t)(uint8_t)this->characterNum, CMobile_GetSerial(&this->mobile),
		        ((char *(*)(void *))VT_FN((CItem *)this, VT_GET_NAME))(this), "godmode", "misc", "god log out");
		CPlayer_DisableEditing(this);
		save = 0;
	}

	if (save == 1) {
		if (CPlayer_ShouldLogOut(this)) {
			CPlayer_Disconnect(this);
		} else {
			ScheduleEvent(0x4B0, this->mobile.container.item.serial, TIMER_EVENT_IDLE_DISCONNECT, 0, 0);
		}
	} else {
		CPlayer_Disconnect(this);
	}
}

/*
 * 0x00452D92 - CPlayer::ShouldLogOut
 *
 * Returns 1 if the player can log out instantly: in an inn/safe area,
 * at their own house door, or past the logout timer.
 */
static int
CPlayer_ShouldLogOut(CPlayer *this)
{
	CLocation *loc;

	loc = CEntity_GetLocation(&this->mobile.container.item.resourceEntity.entity);
	if (RegionManager_IsInSpecialArea(loc))
		return 1;

	if (this->targetSerial != 0) {
		if (GetTickCount_UO() - this->targetSerial > 15)
			return 1;
	}
	if (CPlayer_CheckHouseDoor(this))
		return 1;

	return 0;
}

/*
 * 0x00452DF7 - CPlayer::CheckHouseDoor
 *
 * Returns 1 if the player is standing on a multi door tagged
 * "myhousedoor" whose "whatIUnlock" list contains this player.
 */
static int
CPlayer_CheckHouseDoor(CPlayer *this)
{
	CVector localVec;
	uintptr_t *iter;
	CMultiComponent *mc;
	CItem *entity;
	uint32_t tagVal;
	CString keyStr;
	int found;
	CLocation *loc;

	loc = CEntity_GetLocation(&this->mobile.container.item.resourceEntity.entity);

	CVector_Constructor(&localVec, "\x04");

	CTerrainManager_FindEntitiesAtXYZ(loc->x, loc->y, loc->z, loc->z, &localVec, CItem_HasMulti_Filter);

	iter = (uintptr_t *)localVec.begin;
	while (iter != (uintptr_t *)localVec.end) {
		mc = CItem_GetMulti((CItem *)*iter);
		entity = CWorld_FindBySerial(g_World, CMulti_GetSerial(mc));

		if (entity != NULL) {
			if (CResourceEntity_HasTag(entity, "myhousedoor", 4)) {
				CResourceEntity_GetTagObj(entity, "myhousedoor", &tagVal);

				CString_Constructor(&keyStr, "whatIUnlock");
				found = ((int (*)(void *, CString *, uint32_t))VT_FN(&this->mobile.container.item, VT_FIND_IN_TAG_LIST))(
				        &this->mobile.container.item, &keyStr, tagVal);
				found = found != 0 ? 1 : 0;

				CString_Destructor(&keyStr);

				if (found) {
					CVector_Destructor(&localVec);
					return 1;
				}
			}
		}
		iter++;
	}

	CVector_Destructor(&localVec);
	return 0;
}

/*
 * 0x00453000 - CPlayer vtable[0x204] PaperdollTitle
 *
 * Builds the paperdoll title string, dispatching on role (GM,
 * counselor, murderer, or normal) and on human vs non-human body.
 */
void
CPlayer_PaperdollTitle_VT(CPlayer *this, CString *title)
{
	int bestSkill;
	int bestValue;
	int isCriminal;
	int fameLevel;
	int karmaLevel;
	const char *repTitle;
	int i;
	CString tempStr;
	const char *name;

	bestSkill = 0;
	bestValue = 0;
	isCriminal = 0;

	// 0x00453036: check notoriety sign (dead code: isCriminal is never read)
	if (CMobile_GetNotoriety(&this->mobile) < 0)
		isCriminal = 1;
	USED(isCriminal);

	if ((CResourceEntity_GetBodyType((CItem *)this) & 0xFFFF) != 0x190 && (CResourceEntity_GetBodyType((CItem *)this) & 0xFFFF) != 0x191 && !CPlayer_IsGMAndManifested(this) &&
	        !CPlayer_IsCounselorWithGMBody(this)) {
		name = ((const char *(*)(void *))VT_FN((CItem *)this, VT_GET_NAME))(this);
		CString_AssignCStr(title, name);
		return;
	}

	CString_AssignCStr(title, "");

	if (CPlayer_IsGMAndManifested(this)) {
		CString_AppendCStr(title, "GM ");
	} else if (CPlayer_IsCounselorWithGMBody(this)) {
		CString_DefaultConstructor(&tempStr);
		CPlayer_GetCounselorTitle(this, &tempStr);
		CString_ConcatCString(title, &tempStr);
		CString_AppendCStr(title, " ");
		CString_Destructor(&tempStr);
	} else if (CMobile_IsMurderer(&this->mobile)) {
		CString_AppendCStr(title, "The Murderer ");
	} else {
		fameLevel = CMobile_GetFameLevel(&this->mobile);
		karmaLevel = CMobile_GetKarmaLevel(&this->mobile);
		repTitle = g_ReputationTitles[5 - karmaLevel][fameLevel];
		if (repTitle != NULL) {
			CString_AppendCStr(title, "The ");
			if (*repTitle != '\0') {
				CString_AppendCStr(title, repTitle);
				CString_AppendCStr(title, " ");
			}
			if (fameLevel == 4) {
				if (this->mobile.sex == 1)
					CString_AppendCStr(title, "Lady ");
				else
					CString_AppendCStr(title, "Lord ");
			}
		}
	}

	name = ((const char *(*)(void *))VT_FN((CItem *)this, VT_GET_NAME))(this);
	CString_AppendCStr(title, name);

	if (CPlayer_IsGMAndManifested(this)) {
		CString_AppendCStr(title, ", Game Master");
		return;
	}

	if (CPlayer_IsCounselorWithGMBody(this)) {
		CString_DefaultConstructor(&tempStr);
		CPlayer_GetCounselorTitle(this, &tempStr);
		CString_AppendCStr(title, ", ");
		CString_ConcatCString(title, &tempStr);
		CString_Destructor(&tempStr);
		return;
	}

	for (i = 0; i < 50; i++) {
		if ((int)(uint16_t)this->mobile.skills[i] > bestValue) {
			bestSkill = i;
			bestValue = (int)(uint16_t)this->mobile.skills[i];
		}
	}

	if (bestValue < 250)
		return;

	bestValue = bestValue / 100;
	switch (bestValue) {
	case 0:
	case 1:
	case 2:
		CString_AppendCStr(title, ", ");
		break;
	case 3:
		CString_AppendCStr(title, ", Neophyte ");
		break;
	case 4:
		CString_AppendCStr(title, ", Novice ");
		break;
	case 5:
		CString_AppendCStr(title, ", Apprentice ");
		break;
	case 6:
		CString_AppendCStr(title, ", Journeyman ");
		break;
	case 7:
		CString_AppendCStr(title, ", Expert ");
		break;
	case 8:
		CString_AppendCStr(title, ", Adept ");
		break;
	case 9:
		CString_AppendCStr(title, ", Master ");
		break;
	default:
		CString_AppendCStr(title, ", Grandmaster ");
		break;
	}

	switch (bestSkill) {
	case 0:
		CString_AppendCStr(title, "Alchemist");
		break;
	case 1:
	case 17:
	case 39:
		CString_AppendCStr(title, "Healer");
		break;
	case 2:
	case 12:
	case 16:
	case 19:
		CString_AppendCStr(title, "Scholar");
		break;
	case 3:
		CString_AppendCStr(title, "Merchant");
		break;
	case 4:
	case 5:
	case 27:
		CString_AppendCStr(title, "Warrior");
		break;
	case 6:
		CString_AppendCStr(title, "Beggar");
		break;
	case 7:
		CString_AppendCStr(title, "Smith");
		break;
	case 8:
		CString_AppendCStr(title, "Bowyer");
		break;
	case 9:
	case 15:
	case 22:
	case 29:
		CString_AppendCStr(title, "Bard");
		break;
	case 10:
	case 14:
	case 20:
	case 35:
	case 38:
		CString_AppendCStr(title, "Ranger");
		break;
	case 11:
		CString_AppendCStr(title, "Carpenter");
		break;
	case 13:
	case 36:
		CString_AppendCStr(title, "Chef");
		break;
	case 18:
		CString_AppendCStr(title, "Fisherman");
		break;
	case 21:
	case 24:
	case 28:
	case 33:
		CString_AppendCStr(title, "Rogue");
		break;
	case 23:
	case 25:
	case 26:
		CString_AppendCStr(title, "Mage");
		break;
	case 30:
		CString_AppendCStr(title, "Assassin");
		break;
	case 31:
		CString_AppendCStr(title, "Archer");
		break;
	case 32:
		CString_AppendCStr(title, "Medium");
		break;
	case 34:
		CString_AppendCStr(title, "Tailor");
		break;
	case 37:
		CString_AppendCStr(title, "Tinker");
		break;
	case 40:
		if (this->mobile.sex == 1)
			CString_AppendCStr(title, "Swordswoman");
		else
			CString_AppendCStr(title, "Swordsman");
		break;
	case 41:
		if (this->mobile.sex == 1)
			CString_AppendCStr(title, "Armswoman");
		else
			CString_AppendCStr(title, "Armsman");
		break;
	case 42:
		CString_AppendCStr(title, "Fencer");
		break;
	case 43:
		CString_AppendCStr(title, "Wrestler");
		break;
	case 44:
		CString_AppendCStr(title, "Lumberjack");
		break;
	case 45:
		CString_AppendCStr(title, "Miner");
		break;
	default:
		CString_AppendCStr(title, "NO TITLE");
		break;
	}
}

/*
 * 0x0045366C - CPlayer::SetMovePrevented
 *
 * Sets or clears the PlayerIsMovePrevented flag (pflags bit 0x01).
 * arg=1 freezes the player, arg=0 unfreezes.
 */
void
CPlayer_SetMovePrevented(CPlayer *this, int flag)
{
	if (flag != 0)
		this->pflags |= PlayerIsMovePrevented;
	else
		this->pflags &= ~PlayerIsMovePrevented;
}

/*
 * 0x004536AB - CPlayer::IsMovePrevented
 *
 * Returns 1 if PlayerIsMovePrevented flag (pflags bit 0x01) is set.
 */
int
CPlayer_IsMovePrevented(CPlayer *this)
{
	int val;

	val = this->pflags & PlayerIsMovePrevented;
	return val != 0 ? 1 : 0;
}

/*
 * 0x004536C8 - CPlayer::CanMoveDirection
 *
 * Always returns 1. Takes a direction byte argument but ignores it.
 * Called from CTerrainManager_MovePlayer (0x0046AA95).
 */
int
CPlayer_CanMoveDirection(CPlayer *this, int direction)
{
	USED(this);
	USED(direction);
	return 1;
}

/*
 * 0x004536DA - CPlayer::IsEditing
 *
 * Returns 1 if PlayerIsEditing flag (pflags bit 0x02) is set.
 */
int
CPlayer_IsEditing(CPlayer *this)
{
	return (this->pflags & PlayerIsEditing) != 0;
}

/*
 * 0x0045370B - CPlayer::EnableEditing
 *
 * Enables god mode (editing). Checks PlayerIsLoaded first - only
 * sets PlayerIsEditing if the player data is loaded. Logs "god mode
 * enabled" via EventLogger_Log.
 */
void
CPlayer_EnableEditing(CPlayer *this)
{
	if (this->pflags & PlayerIsLoaded) {
		this->pflags |= PlayerIsEditing;
		EventLogger_Log(&g_EventLogger, this->accountNum, (uint32_t)(uint8_t)this->characterNum, CMobile_GetSerial(&this->mobile),
		        ((char *(*)(void *))VT_FN((CItem *)this, VT_GET_NAME))(this), "godmode", "misc", "god mode enabled");
	}
}

/*
 * 0x0045377C - CPlayer::DisableEditing
 *
 * Disables god mode (editing). Clears PlayerIsEditing flag and
 * logs "god mode disabled" via EventLogger_Log.
 */
void
CPlayer_DisableEditing(CPlayer *this)
{
	this->pflags &= ~PlayerIsEditing;
	EventLogger_Log(&g_EventLogger, this->accountNum, (uint32_t)(uint8_t)this->characterNum, CMobile_GetSerial(&this->mobile),
	        ((char *(*)(void *))VT_FN((CItem *)this, VT_GET_NAME))(this), "godmode", "misc", "god mode disabled");
}

/*
 * 0x004537DE - CPlayer::SetHiddenFlag
 *
 * Sends a WARMODE packet carrying the flag and toggles bit 0x10 in pflags
 * accordingly.
 */
void
CPlayer_SetHiddenFlag(CPlayer *this, int value)
{
	uint8_t obuf[8];

	PacketManager_MakePacket_WARMODE(obuf, (uint8_t)value);
	SendToClient((CItem *)this, obuf, -1);

	if (value != 0)
		this->pflags |= 0x10;
	else
		this->pflags &= ~0x10;
}

/*
 * 0x00453841 - CPlayer vtable[0x208] WalkCheck
 *
 * Copies entity.location, calls CMobile_DoWalkStep with direction, compares
 * result via CLocation_IsEqualXY. Returns 1 if walk succeeded (location changed),
 * 0 if blocked (location unchanged).
 */
int
CPlayer_WalkCheck_VT(CPlayer *self, int direction)
{
	CLocation startLoc;
	CLocation resultLoc;
	CLocation *result;

	startLoc = self->mobile.container.item.resourceEntity.entity.location;
	result = CMobile_DoWalkStep((CItem *)self, &resultLoc, startLoc, direction);
	return !CLocation_IsEqualXY(&self->mobile.container.item.resourceEntity.entity.location, result);
}

/*
 * 0x00453884 - CMobile::DoWalkStep
 *
 * Computes one step in direction dir from start. Applies the
 * ghost/dead/frozen checks, tile validity and wrap, the diagonal
 * blocked check (adjacent cardinals must be walkable), and the terrain
 * walk resolver. Returns start if blocked, otherwise the new tile.
 */
CLocation *
CMobile_DoWalkStep(CItem *mob, CLocation *result, CLocation start, int dir)
{
	CLocation movedLoc;
	int minZ, maxZ;
	CLocation tempResult1;
	CLocation tempResult2;

	// Check IsGhost - frozen ghosts can't move
	if (CPlayer_IsGhost((CPlayer *)mob)) {
		CPlayer_SystemMessage((CPlayer *)mob, "You are frozen and can not move.");
		CLocation_CopyFrom(result, &start);
		return result;
	}

	// Check IsDead (vtable[0xF4]) and frozen (vtable[0x21C])
	if (!VT_IsDead(mob)) {
		if (((int (*)(void *))VT_FN(mob, VT_GET_STAMINA))(mob)) {
			CLocation_CopyFrom(result, &start);
			return result;
		}
	}

	CLocation_CopyFrom(&movedLoc, &start);
	CLocation_MoveDir(&movedLoc, dir & 7);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)movedLoc.x, (int)(int16_t)movedLoc.y)) {
		// Invalid coords: try WrapCoord (always returns 0)
		if (CBlockManager_WrapCoord(&g_SpatialGrid, &movedLoc)) {
			CLocation_CopyFrom(result, &movedLoc);
			return result;
		}
		CLocation_CopyFrom(result, &start);
		return result;
	}

	// Diagonal blocked check: dir & 1 means diagonal
	if (dir & 1) {
		CMobile_DoWalkStep(mob, &tempResult1, start, dir - 1);
		if (CLocation_IsEqualXYZ(&start, &tempResult1)) {
			CMobile_DoWalkStep(mob, &tempResult2, start, (dir + 1) & 7);
			if (CLocation_IsEqualXYZ(&start, &tempResult2)) {
				CLocation_CopyFrom(result, &start);
				return result;
			}
		}
	}

	GetMinMaxZForEntity(mob, start, ((CMobile *)mob)->direction, &minZ, &maxZ);

	movedLoc.z = (int16_t)CMobile_GetWalkZ(mob, movedLoc);

	movedLoc.z = CTerrainManager_CanWalkWrapper(movedLoc, minZ, maxZ, VT_GetHeight(mob), ((int (*)(void *))VT_FN(mob, VT_GET_MOVEMENT_TYPE))(mob) & 0xFF, mob, 0);

	if ((int)(int16_t)movedLoc.z == -128) {
		CLocation_CopyFrom(result, &start);
		return result;
	}

	CLocation_CopyFrom(result, &movedLoc);
	return result;
}

/*
 * 0x00453AAF - DoTurn
 *
 * Direction-only turn with no spatial move. Sets direction, sends
 * OK_MOVE to the player, and notifies nearby players. Dead
 * non-manifesting entities notify only other dead players.
 */
void
DoTurn(CItem *this, int direction, int isPlayer, uint8_t sequence)
{
	CVector nearbyList;
	CVector deadList;
	uint8_t pktBuf[8];
	CItem *exclude;
	uintptr_t *iter;
	CItem *cur;

	exclude = NULL;

	CVector_Constructor(&nearbyList, "");

	CMobile_SetDirection(this, direction);

	if (isPlayer) {
		int noto = CheckNeedEquipUpdate(this, this);
		PacketManager_MakePacket_OK_MOVE(pktBuf, sequence, noto);
		SendToClient(this, pktBuf, -1);
		exclude = this;
	}

	GetNearbyPlayersExclude(&nearbyList, &this->resourceEntity.entity.location, 0x12, exclude);

	if (VT_IsDead(this)) {
		if (!(((CPlayer *)this)->pflags & PlayerIsManifesting)) {
			CVector_Constructor(&deadList, "");
			for (iter = (uintptr_t *)nearbyList.begin; iter != (uintptr_t *)nearbyList.end; iter++) {
				cur = (CItem *)*iter;
				if (VT_IsDead(cur))
					CVector_PushBack(&deadList, *iter);
			}
			((void (*)(void *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &deadList, 0);
			CVector_Destructor(&deadList);
			goto cleanup;
		}
	}

	((void (*)(void *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &nearbyList, 0);

cleanup:
	CVector_Destructor(&nearbyList);
}

/*
 * 0x00453C19 - CPlayer::SendMoveDeny
 *
 * Sends a BLOCKED_MOVE (0x21) packet to deny a movement request and sets
 * the PlayerIsMovePrevented flag.
 */
void
CPlayer_SendMoveDeny(CPlayer *this, uint8_t sequence)
{
	uint8_t obuf[0x10];
	CEntity *ent;

	ent = &this->mobile.container.item.resourceEntity.entity;
	PacketManager_MakePacket_BLOCKED_MOVE(&obuf[0], sequence, ent->location.x, ent->location.y, this->mobile.direction, ent->location.z);
	SendToClient((CItem *)this, &obuf[0], -1);
	CPlayer_SetMovePrevented(this, 1);
}

/*
 * 0x00453C83 - CPlayer::CheckGuardZone
 *
 * Updates guardZoneSerial based on city or justice region membership
 * at the player's current location, and sends an enter/leave system
 * message when the state flips.
 */
void
CPlayer_CheckGuardZone(CPlayer *this)
{
	CLocation *loc;
	uint32_t result;

	loc = &this->mobile.container.item.resourceEntity.entity.location;

	result = (uint32_t)RegionManager_isInCityRegion(loc->x, loc->y, loc->z);

	if (result == 0)
		result = (uint32_t)RegionManager_inJusticeRegion(loc->x, loc->y, loc->z);

	if (result != this->guardZoneSerial) {
		this->guardZoneSerial = result;

		if (result != 0)
			CPlayer_SystemMessage(this, "You are now under the protection of "
			                            "the town guards.");
		else
			CPlayer_SystemMessage(this, "You have left the protection of "
			                            "the town guards.");
	}
}

/*
 * 0x00453D00 - DoMove
 *
 * Executes one movement step: validates terrain, updates the spatial
 * grid, dispatches visibility changes, scans for NPC range triggers,
 * handles trade distance, ranged weapon state, guard zone, and
 * z-climb stamina drain.
 */
void
DoMove(CItem *this, int direction, int isPlayer, uint8_t sequence)
{
	CLocation oldLoc, newLoc;
	int oldBlock, newBlock;
	int walkZ_unused;
	CItem *exclude;
	uint8_t pktBuf[0x20];
	uint8_t destroyBuf[8];
	CVector removeList, insertList, overlapList;
	CVector deadRemove, deadInsert, deadOverlap;
	int blockBuf[0x100];
	int i;
	CItem *ent;
	CItem *entAsMob;
	CItem *weapon;
	CTradeSession *session;
	CTradeSession *nextSession;
	int16_t oldZ;
	int minZ, maxZ;
	int moveType;
	int bodyFlags;
	uint32_t serial;
	uintptr_t *iter;
	CItem *cur;

	CLocation_Init(&oldLoc);
	CLocation_Init(&newLoc);

	g_DoMoveEntity = this;

	CLocation_CopyFrom(&newLoc, CEntity_GetLocation(&this->resourceEntity.entity));
	CLocation_CopyFrom(&oldLoc, &newLoc);

	CLocation_MoveDir(&newLoc, direction & 7);

	if (direction != (int)((CMobile *)this)->direction)
		((CMobile *)this)->direction = direction;

	// Binary dead code: walkZ initialized to 0, never modified.
	walkZ_unused = 0;

	if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)newLoc.x, (int)(int16_t)newLoc.y)) {
		GetMinMaxZForEntity(this, oldLoc, ((CMobile *)this)->direction, &minZ, &maxZ);

		// We call CMobile_GetWalkZ directly (same function at 0x004818AA).
		newLoc.z = (int16_t)CMobile_GetWalkZ(this, newLoc);

		moveType = ((int (*)(void *))VT_FN(this, VT_GET_MOVEMENT_TYPE))(this) & 0xFF;
		bodyFlags = ((int (*)(void *))VT_FN(this, VT_GET_HEIGHT))(this);

		newLoc.z = (int16_t)CTerrainManager_CanWalkWrapper(newLoc, minZ, maxZ, bodyFlags, moveType, this, 0);
	}

	serial = CMobile_GetSerial((CMobile *)this);

	if (!CheckWalkPassability(this, &oldLoc, &newLoc)) {
		if (isPlayer) {
			if (CWorld_FindBySerial(g_World, serial) != this) {
				g_DoMoveEntity = NULL;
				return;
			}
			CPlayer_SendMoveDeny((CPlayer *)this, sequence);
		}
		g_DoMoveEntity = NULL;
		return;
	}

	// Spatial grid update - three cases based on block membership
	oldBlock = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, CEntity_GetLocation(&this->resourceEntity.entity), 0);
	newBlock = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &newLoc, 0);

	if (oldBlock != newBlock) {
		if (CLocation_IsEqualXYZ(&oldLoc, CEntity_GetLocation(&this->resourceEntity.entity))) {
			((void (*)(void *))VT_FN(this, VT_DETACH_SPATIAL))(this);
			((void (*)(void *, CLocation *))VT_FN(this, VT_SET_LOCATION))(this, &newLoc);
		} else {
			((void (*)(void *))VT_FN(this, VT_DETACH_SPATIAL))(this);
			((void (*)(void *, CLocation *))VT_FN(this, VT_DROP_AT_FEET))(this, &newLoc);
		}
	} else {
		CPlayer_SetLocation((CPlayer *)this, &newLoc);
		CPlayer_UpdateLastValidLocation((CPlayer *)this, &newLoc);
	}

	if (IsNearCampfire(this))
		CItem_SetCampfireTimestamp(this);
	else
		CPlayer_ClearTargetSerial((CPlayer *)this);

	if (VT_IsHidden(this)) {
		if (feat(FEAT_SKILL_STEALTH) && CResourceEntity_HasTag(this, "stealthSteps", 7)) {
			if (direction & 0x80) {
				Entity_ExecuteEvent(&this->resourceEntity.entity, 0x16, (uintptr_t)this->serial, "stealthBreak", "v");
				((void (*)(void *, int))VT_FN(this, VT_SET_HIDDEN))(this, 0);
				CPlayer_SystemMessage((CPlayer *)this, "You have been revealed!");
			} else {
				Entity_ExecuteEvent(&this->resourceEntity.entity, 0x16, (uintptr_t)this->serial, "stealthStep", "v");
				if (!VT_IsHidden(this))
					CPlayer_SystemMessage((CPlayer *)this, "You are no longer hidden.");
			}
		} else if (!CPlayer_IsEditing((CPlayer *)this)) {
			((void (*)(void *, int))VT_FN(this, VT_SET_HIDDEN))(this, 0);
		}
	}

	if (feat(FEAT_SKILL_MEDITATION)) {
		if (CResourceEntity_HasTag(this, "meditating", 7)) {
			Entity_ExecuteEvent(&this->resourceEntity.entity, 0x16, (uintptr_t)this->serial, "breakMeditation", "v");
			CPlayer_SystemMessage((CPlayer *)this, "You stop meditating.");
		}
	}

	exclude = NULL;

	// Binary dead code: walkZ_unused is always 0, so this branch
	// is never taken. Reproducing for exact match.
	if (walkZ_unused) {
		if (isPlayer) {
			int noto = CheckNeedEquipUpdate(this, this);
			PacketManager_MakePacket_OK_MOVE(pktBuf, sequence, noto);
			SendToClient(this, pktBuf, -1);
		}
	} else {
		if (isPlayer) {
			int noto = CheckNeedEquipUpdate(this, this);
			PacketManager_MakePacket_OK_MOVE(pktBuf, sequence, noto);
			SendToClient(this, pktBuf, -1);
			CMobile_HandleFollowerMovement((CMobile *)this, direction);
			exclude = this;
		}
	}

	CVector_Constructor(&removeList, "");
	CVector_Constructor(&insertList, "");
	CVector_Constructor(&overlapList, "");

	// Binary passes the post-move position first, pre-move second
	// (DoMove @ 0x00454113): insertList then holds players newly in
	// range and overlapList players no longer in range.
	CollectMovementVisibilityExclude(
	        &removeList, &insertList, &overlapList, (int)(int16_t)newLoc.x, (int)(int16_t)newLoc.y, (int)(int16_t)oldLoc.x, (int)(int16_t)oldLoc.y, 0x12, exclude);

	// Ghost visibility filtering
	if (VT_IsDead(this)) {
		if (!(((CPlayer *)this)->pflags & PlayerIsManifesting)) {
			CVector_Constructor(&deadRemove, "");
			CVector_Constructor(&deadInsert, "");
			CVector_Constructor(&deadOverlap, "");

			for (iter = (uintptr_t *)removeList.begin; iter != (uintptr_t *)removeList.end; iter++) {
				cur = (CItem *)*iter;
				if (!VT_IsDead(cur) && !CPlayer_IsGameMaster((CPlayer *)cur))
					continue;
				CVector_PushBack(&deadRemove, *iter);
			}
			for (iter = (uintptr_t *)insertList.begin; iter != (uintptr_t *)insertList.end; iter++) {
				cur = (CItem *)*iter;
				if (!VT_IsDead(cur) && !CPlayer_IsGameMaster((CPlayer *)cur))
					continue;
				CVector_PushBack(&deadInsert, *iter);
			}
			for (iter = (uintptr_t *)overlapList.begin; iter != (uintptr_t *)overlapList.end; iter++) {
				cur = (CItem *)*iter;
				if (!VT_IsDead(cur) && !CPlayer_IsGameMaster((CPlayer *)cur))
					continue;
				CVector_PushBack(&deadOverlap, *iter);
			}

			((void (*)(void *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &deadRemove, 0);
			((void (*)(void *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &deadInsert, 1);

			PacketManager_MakePacket_DESTROY_OBJECT(destroyBuf, this->serial);
			SendToClientList(&deadOverlap, destroyBuf);

			CVector_Destructor(&deadOverlap);
			CVector_Destructor(&deadInsert);
			CVector_Destructor(&deadRemove);
			goto entity_scan;
		}
	}

	((void (*)(void *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &removeList, 0);
	((void (*)(void *, CVector *, int))VT_FN(this, VT_NOTIFY_NEARBY))(this, &insertList, 1);

	PacketManager_MakePacket_DESTROY_OBJECT(destroyBuf, this->serial);
	SendToClientList(&overlapList, destroyBuf);

entity_scan:
	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, CEntity_GetLocation(&this->resourceEntity.entity), 0x13, blockBuf, 0x100);

	for (i = 0; blockBuf[i] != -1; i++) {
		CBlock *blk = &g_SpatialGrid.cells[blockBuf[i]];
		for (ent = blk->itemHead; ent != NULL; ent = ent->spatialNext) {
			if (CLocation_ChebyshevDistance(&newLoc, CEntity_GetLocation(&ent->resourceEntity.entity)) > 0x12)
				continue;
			if (CLocation_ChebyshevDistance(&oldLoc, CEntity_GetLocation(&ent->resourceEntity.entity)) <= 0x12)
				continue;

			if (VT_IsPlayer(ent)) {
				entAsMob = ent;
				if (VT_IsDead(entAsMob)) {
					if (!(((CPlayer *)entAsMob)->pflags & PlayerIsManifesting)) {
						if (!VT_IsDead(this))
							goto next_entity;
					}
				}
				((void (*)(void *, CItem *, int))VT_FN(ent, VT_SEND_ENTITY_UPDATE))(ent, this, 1);
			} else {
				((void (*)(void *, CItem *, int))VT_FN(ent, VT_SEND_ENTITY_UPDATE))(ent, this, 1);
				if (VT_IsNPC(ent))
					CNPC_OnPlayerEnteredRange((CMobile *)ent, (CMobile *)this);
			}
next_entity:;
		}
	}

	weapon = CMobile_GetWeapon((CMobile *)this);
	if (weapon != NULL) {
		if (CItem_IsRangedWeapon(weapon))
			CMobile_SetSwingState((CMobile *)this, 1);
	}

	((CPlayer *)this)->stepCounter++;
	if ((int)((CPlayer *)this)->stepCounter >= 10) {
		((CPlayer *)this)->stepCounter = 0;
		CPlayer_CheckGuardZone((CPlayer *)this);
	}

	// Trade session distance check
	if (((CMobile *)this)->container.contents != NULL) {
		if ((CEntity_GetBodyType(((CMobile *)this)->container.contents) & 0xFFFF) == 0x1E5E) {
			for (session = g_TradeSessionList; session != NULL; session = nextSession) {
				nextSession = session->next;
				if ((CItem *)session->player1 != this && (CItem *)session->player2 != this)
					continue;
				if (Location_WrappedChebyshevDistance(CEntity_GetLocation(&((CItem *)session->player1)->resourceEntity.entity),
				            CEntity_GetLocation(&((CItem *)session->player2)->resourceEntity.entity)) <= 2)
					continue;
				if (session != NULL)
					CloseTrade(session, 1);
			}
		}
	}

	oldZ = oldLoc.z;
	if ((int)oldZ > (int)CEntity_GetLocation(&this->resourceEntity.entity)->z) {
		int delta = (int)oldZ - (int)CEntity_GetLocation(&this->resourceEntity.entity)->z;
		CMobile_DrainStaminaForClimb((CMobile *)this, delta);
	}

	g_DoMoveEntity = NULL;

	CVector_Destructor(&overlapList);
	CVector_Destructor(&insertList);
	CVector_Destructor(&removeList);
}

/*
 * 0x004547B7
 */
int
CPlayer_IsPlayerOnline(CPlayer *this)
{
	return (this->pflags & PlayerIsOnline) != 0;
}

/*
 * 0x004547D4 - CPlayer::IsDead (vtable[0xF4])
 *
 * Returns 1 when the body type is one of the ghost forms (0x192/0x193).
 */
int
CPlayer_IsDead(CPlayer *this)
{
	if ((uint16_t)CResourceEntity_GetBodyType((CItem *)this) == 0x192)
		return 1;
	if ((uint16_t)CResourceEntity_GetBodyType((CItem *)this) == 0x193)
		return 1;
	return 0;
}

/*
 * 0x0045489D - CPlayer::IsFriendAllowed
 *
 * Checks whether other is allowed to observe this player's containers.
 * Returns 1 if pflags & 0x40 (allow all) or if other's serial is in
 * this player's friendAllowList.
 */
static int
CPlayer_IsFriendAllowed(CPlayer *this, CPlayer *other)
{
	uint32_t i;
	uint32_t serial;

	if (this->pflags & 0x40)
		return 1;

	serial = other->mobile.container.item.serial;
	for (i = 0; i < this->friendAllowCount; i++) {
		if (this->friendAllowList[i] == serial)
			return 1;
	}
	return 0;
}

/*
 * 0x00454905 - CPlayer::ToggleWarMode (inner, 230 bytes)
 *
 * Converts warFlag to boolean, stores in combatByte1 (0x33C).
 * Compares with current mobileFlags bit 0x40. If state changed:
 * entering war mode calls ExitCombat(1) and SetMobileFlag(0x40);
 * leaving war mode calls ExitCombat(0), ClearMobileFlag(0x40),
 * and StopCombat. Both paths gate IsDead -> Hide -> ReturnToTracked.
 */
static void
CPlayer_ToggleWarMode(CPlayer *this, int warFlag)
{
	int newWar, oldWar;

	// neg/sbb/neg: convert (warFlag & 0xFF) to 0 or 1
	newWar = (warFlag & 0xFF) != 0 ? 1 : 0;
	this->mobile.combatByte1 = (uint8_t)newWar;

	// Current war mode: mobileFlags bit 0x40 -> 0 or 1
	oldWar = (this->mobile.mobileFlags & 0x40) != 0 ? 1 : 0;

	if (oldWar == (int)(int8_t)this->mobile.combatByte1)
		return;

	if ((int)(int8_t)this->mobile.combatByte1 != 0) {
		if (VT_IsDead((CItem *)this)) {
			CMobile_ExitCombat(&this->mobile, 1);
			((void (*)(void *))VT_FN((CItem *)this, VT_HIDE))(this);
			((void (*)(void *))VT_FN((CItem *)this, VT_RETURN_TO_TRACKED))(this);
		}
		CMobile_SetMobileFlag(&this->mobile, 0x40);
	} else {
		if (VT_IsDead((CItem *)this)) {
			CMobile_ExitCombat(&this->mobile, 0);
			((void (*)(void *))VT_FN((CItem *)this, VT_HIDE))(this);
			((void (*)(void *))VT_FN((CItem *)this, VT_RETURN_TO_TRACKED))(this);
		}
		CMobile_ClearMobileFlag(&this->mobile, 0x40);
		CMobile_StopCombat(&this->mobile);
	}
}

/*
 * 0x004549EB - CPlayer::SetWarModeBroadcast
 *
 * For a living player, toggles war mode and broadcasts a COMBAT packet
 * derived from the player's combat fields and the war-mode mobile flag.
 */
void
CPlayer_SetWarModeBroadcast(CPlayer *this, int warFlag)
{
	uint8_t obuf[16];

	if (CPlayer_HasDeadFlag(this))
		return;

	CPlayer_ToggleWarMode(this, warFlag & 0xFF);

	PacketManager_MakePacket_COMBAT(obuf, CMobile_CheckMobileFlag(&this->mobile, 0x40), this->mobile.combatByte2, this->mobile.combatByte3, this->mobile.combatByte4);
	SendToClient((CItem *)this, obuf, -1);
}

/*
 * 0x00454A5B - CPlayer::SetWarMode
 *
 * Checks IsDead (no-op if dead), calls inner ToggleWarMode
 * (0x00454905), sets combat bytes 0x33D-0x33F, builds COMBAT
 * packet and sends via SendToClient.
 */
void
CPlayer_SetWarMode(CPlayer *this, int warFlag, int combatByte2, int combatByte3, int combatByte4)
{
	uint8_t obuf[16];

	if (CPlayer_HasDeadFlag(this))
		return;

	CPlayer_ToggleWarMode(this, warFlag);

	this->mobile.combatByte2 = (uint8_t)combatByte2;
	this->mobile.combatByte3 = (uint8_t)combatByte3;
	this->mobile.combatByte4 = (uint8_t)combatByte4;

	PacketManager_MakePacket_COMBAT(obuf, CMobile_CheckMobileFlag(&this->mobile, 0x40), (uint8_t)combatByte2, (uint8_t)combatByte3, (uint8_t)combatByte4);
	SendToClient((CItem *)this, obuf, -1);
}

/*
 * 0x00454ADD - CPlayer no-op stub
 *
 * Empty stub called at the end of CPlayer_Heartbeat.
 */
void
CPlayer_HeartbeatCleanup(CPlayer *this)
{
	USED(this);
}

/*
 * 0x00454AE8
 */
void
CPlayer_PingReply(CPlayer *this, uint8_t sequence)
{
	USED(sequence);
	this->pingTimer = 0;
}

/*
 * 0x00454B02 - CPlayer::ToggleDirectionFlag
 *
 * Toggles a direction bit flag. If the flag is currently set, clears it;
 * if clear, sets it. Uses the same byte/bit indexing as GetDirectionFlag.
 */
void
CPlayer_ToggleDirectionFlag(CPlayer *this, int direction)
{
	int byteIdx, bitIdx;
	uint8_t *flags;

	byteIdx = ((direction & 0xFF) >> 3) + 1;
	bitIdx = (direction & 0xFF) % 8;

	flags = (uint8_t *)&this->npcAIMode;
	if (CPlayer_GetDirectionFlag(this, direction)) {
		flags[byteIdx] &= ~(1 << bitIdx);
	} else {
		flags[byteIdx] |= (1 << bitIdx);
	}
}

/*
 * 0x00454B95 - CPlayer::GetDirectionFlag
 *
 * Tests a direction bit flag in the NPC AI byte array at offset 0x3E0.
 * The arg byte is split: bits [7:3] select the byte (1-based), bits [2:0]
 * select the bit within that byte. Returns nonzero if the bit is set.
 */
int
CPlayer_GetDirectionFlag(CPlayer *this, int direction)
{
	int byteIdx, bitIdx;
	uint8_t *flags;

	byteIdx = ((direction & 0xFF) >> 3) + 1;
	bitIdx = (direction & 0xFF) % 8;

	flags = (uint8_t *)&this->npcAIMode;
	return (int)(*(int8_t *)(flags + byteIdx)) & (1 << bitIdx);
}

/*
 * 0x00454BE5 - CPlayer::SpeakSysMsg vtable[0x04C] override
 *
 * Returns the player's decorated name with title prefix in a global
 * 256-byte buffer (0x006459B8). Prepends "GM " for manifested GMs,
 * counselor title + " " for counselors, or "Lord "/"Lady " for
 * fame level >= 4 with human body (0x190-0x193). Appends base name
 * via vtable[0x34] (GetName), truncated to 250 chars.
 */
char *
CPlayer_SpeakSysMsg_VT(CItem *self, int flag)
{
	CPlayer *this = (CPlayer *)self;
	USED(flag);

	g_PlayerSpeakBuf[0] = '\0';

	if (CPlayer_IsGMAndManifested(this)) {
		strcpy(g_PlayerSpeakBuf, "GM ");
	} else if (CPlayer_IsCounselorWithGMBody(this)) {
		CString tempStr;
		CString_DefaultConstructor(&tempStr);
		CPlayer_GetCounselorTitle(this, &tempStr);
		strcpy(g_PlayerSpeakBuf, CString_GetBuffer(&tempStr));
		strcat(g_PlayerSpeakBuf, " ");
		CString_Destructor(&tempStr);
	} else if ((CResourceEntity_GetBodyType((CItem *)this) & 0xFFFF) >= 0x190 && (CResourceEntity_GetBodyType((CItem *)this) & 0xFFFF) <= 0x193 &&
	           CMobile_GetFameLevel((CMobile *)this) >= 4) {
		if (this->mobile.sex == 0)
			strcpy(g_PlayerSpeakBuf, "Lord ");
		else
			strcpy(g_PlayerSpeakBuf, "Lady ");
	}

	char *name = ((char *(*)(void *))VT_FN(self, VT_GET_NAME))(self);
	strncat(g_PlayerSpeakBuf, name, 250);
	g_PlayerSpeakBuf[255] = '\0';

	return g_PlayerSpeakBuf;
}

/*
 * 0x00454D31 - CPlayer::AddToGMCallQueue
 *
 * Dispatches the GM single-call handler for a GM player.
 */
void
CPlayer_AddToGMCallQueue(CPlayer *this)
{
	CSkillUseCtx ctx;

	if (!CPlayer_IsGameMaster(this))
		return;

	CSkillUseCtx_Init(&ctx);
	ctx.type = 0;
	ctx.serial = CMobile_GetSerial(&this->mobile);
	ctx.field08 = 0;
	ctx.field0C = 0;

	CEditorObj_HandleGMSingle((CEditorObj *)&g_GMPlayerList, &ctx);
}

/*
 * 0x00454DC0 - CPlayer::RemoveFromGMCallQueue
 *
 * Clears the GameMaster flag and drops the player from the GM list.
 * Returns 1 if removed, 0 if the player was not a GM.
 */
int
CPlayer_RemoveFromGMCallQueue(CPlayer *this)
{
	if (!CPlayer_IsGameMaster(this))
		return 0;

	this->pflags &= ~PlayerIsGameMaster;

	CResList_RemoveByValue(&g_GMPlayerList, (uint32_t)(uintptr_t)this);

	return 1;
}

/*
 * 0x00454E03 - CPlayer::IsGameMaster
 * 0x00456703 (COMDAT)
 *
 * Checks if PlayerIsGameMaster flag (0x1000) is set in pflags.
 * Returns 1 if game master, 0 otherwise.
 */
int
CPlayer_IsGameMaster(CPlayer *this)
{
	int val;

	val = this->pflags & PlayerIsGameMaster;
	return val != 0 ? 1 : 0;
}

/*
 * 0x00454E22 - CPlayer::HasGMBody
 *
 * Returns 1 if player's bodyType is 0x3DB (GM body), 0 otherwise.
 * Binary calls CEntity_GetBodyType (0x00420FA0 -> 0x00406A30), masks to
 * uint16, compares to 0x3DB.
 */
int
CPlayer_HasGMBody(CPlayer *this)
{
	uint16_t bt;

	bt = this->mobile.container.item.resourceEntity.entity.bodyType;
	return bt == 0x3DB ? 1 : 0;
}

/*
 * 0x00454E46 - CPlayer::CancelTrade
 *
 * Closes any trade session involving this player.
 */
void
CPlayer_CancelTrade(CPlayer *player)
{
	CTradeSession *session;
	CTradeSession *nextSession;
	CTradeSession *sess2;
	CTradeSession *sess3;
	CTradeSession *result;

	session = g_TradeSessionList;
	while (session != NULL) {
		nextSession = session->next;
		if (session->player1 == player || session->player2 == player) {
			sess2 = session;
			sess3 = sess2;
			if (sess3 != NULL) {
				result = CloseTrade(sess3, 1);
			} else {
				result = NULL;
			}
		}
		session = nextSession;
	}
	USED(result);
}

/*
 * 0x00454EB4 - CPlayer::ApplyResurrection
 *
 * Resurrects a dead player: plays the appropriate flavor text for
 * deathCount, applies the murder-count stat/skill penalty, changes
 * fame, clears war mode, drops ghost resources, converts the ghost
 * body back to a living body, and swaps the death shroud for a robe.
 * Returns 1 on success, 0 if the player has died too many times.
 */
int
CPlayer_ApplyResurrection(CPlayer *this, int flag)
{
	int penalty;
	int murderCount;
	CLocation savedLoc;
	uint16_t bt;
	CItem *shroud;
	CItem *robe;
	int equipResult;

	if (!VT_IsDead(&this->mobile.container.item))
		return 1;

	if (flag) {
		CPlayer_DeathCountDecay(this);

		switch (this->deathCount) {
		case 0:
			CPlayer_SystemMessage(this, "Your spirit rejoins your body.");
			break;
		case 1:
			CPlayer_SystemMessage(this, "With some effort, you reunite your spirit and your body.");
			break;
		case 2:
			CPlayer_SystemMessage(this, "With great difficulty, you manage to bring your spirit and body together.");
			break;
		case 3:
			CPlayer_SystemMessage(this, "You barely manage to bring your spirit and body together--the connection is very tenuous.");
			break;
		default:
			CPlayer_SystemMessage(this, "The connection between your spirit and body is too weak to resurrect. You will have to wait a while.");
			return 0;
		}
	}

	CPlayer_IncrementDeathCount(this);

	penalty = 0;
	murderCount = 0;
	CResourceEntity_GetTagInt((CItem *)this, "murderCount", &murderCount);
	murderCount -= 5;
	if (murderCount >= 0) {
		// Lazy-init Double3 at 0x00645AD0 with (20.0, 5.0, 15.0, 18.0)
		if (!(g_murderPenaltyInitFlag & 1)) {
			g_murderPenaltyInitFlag |= 1;
			// Double3_Init(&g_murderPenaltyDist, 20.0, 5.0, 15.0, 18.0)
			// With these params: root1=1.0, advA=1.0, advB=inf, advC=-inf
			// The binary does not guard against degenerate cases.
			Double3_Init((CSkillDef *)&g_murderPenaltyDist, 20.0, 5.0, 15.0, 18.0);
		}
		penalty += (int)Double3_Eval((CSkillDef *)&g_murderPenaltyDist, (double)murderCount);
	}
	if (penalty > 100)
		penalty = 100;
	if (penalty > 0) {
		CMobile_StatRegenBonusCalc(&this->mobile, penalty);
		CMobile_SkillDecayFromRegen(&this->mobile, penalty);
	}

	CMobile_ChangeFame(&this->mobile, -10);

	if (CMobile_CheckMobileFlag(&this->mobile, 0x40))
		CPlayer_SetWarMode(this, 0, 0, 0, 0);

	CResourceEntity_RemoveAllNodes((CItem *)this, 1);

	bt = this->mobile.container.item.resourceEntity.entity.bodyType & 0xFFFF;
	if (bt != 0x192 && bt != 0x193)
		goto done;

	CPlayer_SetBodyType(this, (uint16_t)(bt - 2));

	savedLoc = this->mobile.container.item.resourceEntity.entity.location;

	if (this->mobile.container.item.resourceEntity.entity.removedFromWorld == 0)
		((void (*)(void *))VT_FN((CItem *)this, VT_HIDE))(this);

	CMobile_SetResurrectionResources(&this->mobile);

	if (this->mobile.container.item.resourceEntity.entity.removedFromWorld == 0)
		CResourceEntity_NotifyPostModifyIfActive((CItem *)this);

	((void (*)(void *, uint32_t))VT_FN((CItem *)this, VT_SET_STAMINA))(this, CMobile_GetMaxStamina(&this->mobile));

	((void (*)(void *, CLocation *))VT_FN((CItem *)this, VT_DROP_AT_FEET))(this, &savedLoc);

	if (this->mobile.equipment[22] == NULL)
		goto done;

	shroud = this->mobile.equipment[22];
	if ((shroud->resourceEntity.entity.bodyType & 0xFFFF) != 0x204E)
		goto done;

	// Binary has redundant NULL check
	if (this->mobile.equipment[22] != NULL)
		((void (*)(void *))VT_FN(this->mobile.equipment[22], VT_DELETE))(this->mobile.equipment[22]);

	robe = CWorld_CreateItem(g_World, 0x1F03);
	if (robe == NULL)
		goto done;

	robe->resourceEntity.entity.color = 0x8FD;

	equipResult = ((int (*)(void *, void *, int))VT_FN(robe, VT_EQUIP_ON_MOBILE))(robe, this, 0x16);
	if (equipResult == 1) {
		CItem_Setup(robe, 1, &this->mobile.container.item.resourceEntity.entity.location, 0, 1);
		if (!ValidateInWorld(robe))
			robe = NULL;
		if (robe != NULL)
			CItem_DecayProcess(robe);
	} else {
		if (robe != NULL)
			((void (*)(void *))VT_FN(robe, VT_DELETE))(robe);
		robe = NULL;
	}

done:
	return 1;
}

/*
 * 0x00455210 - CPlayer::InstantResurrect
 *
 * Sends 0x2C (DEATH_SCREEN) with flag=1 to dismiss the death dialog,
 * then calls ApplyResurrection(1). On success, applies stat regen
 * bonus and skill decay with rate=10.
 */
void
CPlayer_InstantResurrect(CPlayer *this)
{
	uint8_t obuf[16];

	PacketManager_MakePacket_DEATH(obuf, 1);
	Entity_BroadcastPacket((CItem *)this, this->mobile.container.item.serial, obuf);

	if (CPlayer_ApplyResurrection(this, 1)) {
		CMobile_StatRegenBonusCalc(&this->mobile, 10);
		CMobile_SkillDecayFromRegen(&this->mobile, 10);
	}
}

/*
 * 0x00455264 - CMobile::NotifyNearbyPlayers
 *
 * Announces this mobile to nearby entities within Chebyshev 18 and
 * triggers NPC range-enter callbacks. When not hidden, also broadcasts
 * an update to nearby players, filtering out live players if this
 * mobile is a non-ghost corpse.
 */
void
CMobile_NotifyNearbyPlayers(CItem *mob)
{
	int blocks[256];
	int i;
	CItem *entity;
	CPlayer *player;
	CVector nearbyList;
	CVector filteredList;
	uintptr_t *iter;

	// Part 1: walk spatial grid for all entities within range 18
	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &mob->resourceEntity.entity.location, 0x12, blocks, 256);

	for (i = 0; blocks[i] != -1; i++) {
		entity = g_MapBlocks[blocks[i]].itemHead;
		while (entity != NULL) {
			// Skip self
			if (entity == mob)
				goto next_entity;

			// Skip if Chebyshev distance > 18
			if (CLocation_ChebyshevDistance(&mob->resourceEntity.entity.location, &entity->resourceEntity.entity.location) > 0x12)
				goto next_entity;

			// Check if entity is a player (vtable[0x18])
			if (VT_IsPlayer(entity)) {
				// Player path
				player = (CPlayer *)entity;

				if (!VT_IsDead(entity))
					goto send_entity_update;

				if (player->pflags & 0x20)
					goto send_entity_update;

				if (VT_IsDead(mob))
					goto send_entity_update;

				if (CPlayer_IsEditing((CPlayer *)mob))
					goto send_entity_update;

				if (!CPlayer_IsGameMaster(player))
					goto next_entity;

send_entity_update:
				((void (*)(void *, CItem *, int))VT_FN(entity, VT_SEND_ENTITY_UPDATE))(entity, mob, 1);
			} else {
				((void (*)(void *, CItem *, int))VT_FN(entity, VT_SEND_ENTITY_UPDATE))(entity, mob, 1);

				if (VT_IsNPC(entity))
					CNPC_OnPlayerEnteredRange((CMobile *)entity, (CMobile *)mob);
			}

next_entity:
			entity = entity->spatialNext;
		}
	}

	// Part 2: send this mobile to nearby players (if not hidden)
	if (VT_IsHidden(mob))
		return;

	CVector_Constructor(&nearbyList, "\x01");
	GetNearbyPlayersExclude(&nearbyList, &mob->resourceEntity.entity.location, 0x12, mob);

	if (!VT_IsDead(mob)) {
		((void (*)(void *, CVector *, int))VT_FN(mob, VT_NOTIFY_NEARBY))(mob, &nearbyList, 1);
	} else if (((CPlayer *)mob)->pflags & 0x20) {
		((void (*)(void *, CVector *, int))VT_FN(mob, VT_NOTIFY_NEARBY))(mob, &nearbyList, 1);
	} else {
		// Dead + not ghost: filter to only dead players and GMs
		CVector_Constructor(&filteredList, "\x01");

		iter = (uintptr_t *)CSearchCtx_GetBucket((CSearchCtx *)&nearbyList);
		while (iter != (uintptr_t *)nearbyList.end) {
			CItem *p = (CItem *)(uintptr_t)*iter;
			if (VT_IsDead(p)) {
				CVector_PushBack(&filteredList, *iter);
			} else if (CPlayer_IsGameMaster((CPlayer *)p)) {
				CVector_PushBack(&filteredList, *iter);
			}
			iter++;
		}

		((void (*)(void *, CVector *, int))VT_FN(mob, VT_NOTIFY_NEARBY))(mob, &filteredList, 1);
		CVector_Destructor(&filteredList);
	}

	CVector_Destructor(&nearbyList);
}

/*
 * 0x00455573 - Player_RegisterEntity
 *
 * Validates entity coordinates (falling back to g_Config origin if invalid),
 * applies the body template via CMobile_BecomeTemplate(sex + 2998, 0),
 * applies skill templates for each non-zero skill (skillIndex + 3000), and
 * runs EquipDecayTick + DetachSpatial when the player is in the world.
 */
void
Player_RegisterEntity(CPlayer *this)
{
	CLocation loc;
	CEntity *ent;
	CMobile *mob;
	int i;

	mob = &this->mobile;
	ent = &mob->container.item.resourceEntity.entity;

	CLocation_SetLoc(&loc, &ent->location);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)loc.x, (int)loc.y)) {
		loc.x = (int16_t)g_Config.x;
		loc.y = (int16_t)g_Config.y;
	}

	if (ent->removedFromWorld != 0) {
		((void (*)(void *, CLocation *))VT_FN(&mob->container.item, VT_SET_LOCATION))(this, &loc);
	}

	for (i = 0; i < 50; i++) {
		if ((int)mob->skills[i] > 0)
			CMobile_BecomeTemplate(mob, i + 0xBB8, 0);
	}

	CMobile_BecomeTemplate(mob, (int)mob->sex + 0xBB6, 0);

	((void (*)(void *))VT_FN(&mob->container.item, VT_EQUIP_DECAY_TICK))(this);

	if (ent->removedFromWorld == 0) {
		((void (*)(void *))VT_FN(&mob->container.item, VT_DETACH_SPATIAL))(this);
	}
}

/*
 * 0x00455654 - CollectContainerScripts
 *
 * Recursively walks container children (via CContainer.contents and
 * CItem.spatialNext chain). For each child with scripts, pushes its
 * serial into the CVector. If the child is a container (vtable[0xD4]),
 * recurses into it.
 */
static void
CollectContainerScripts(CItem *container, CVector *vec)
{
	CItem *child;

	child = ((CContainer *)container)->contents;
	while (child != NULL) {
		if (CItem_HasScripts(child))
			CVector_PushBack(vec, child->serial);
		if (VT_IsMobile2(child))
			CollectContainerScripts(child, vec);
		child = child->spatialNext;
	}
}

/*
 * 0x004556B5 - CPlayer::Disconnect
 *
 * Disconnect handler: fires the logout event on the player and all
 * equipped scripts, broadcasts DESTROY_OBJECT to nearby clients, and
 * tears down the entity.
 */
void
CPlayer_Disconnect(CPlayer *this)
{
	CVector scriptVec;
	char typeFlag = 0;
	int i;
	uintptr_t *iter;
	CItem *found;
	uint8_t obuf[16];

	CVector_Constructor(&scriptVec, &typeFlag);

	if (CItem_HasScripts(&this->mobile.container.item))
		CVector_PushBack(&scriptVec, this->mobile.container.item.serial);

	for (i = 0; i < 0x1A; i++) {
		CItem *slot = this->mobile.equipment[i];
		if (slot == NULL)
			continue;
		if (CItem_HasScripts(slot))
			CVector_PushBack(&scriptVec, slot->serial);
		if (VT_IsMobile2(slot))
			CollectContainerScripts(slot, &scriptVec);
	}

	iter = (uintptr_t *)scriptVec.begin;
	while (iter != (uintptr_t *)scriptVec.end) {
		found = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (found != NULL)
			Entity_ExecuteEvent(&found->resourceEntity.entity, 0x35);
		iter++;
	}

	PacketManager_MakePacket_DESTROY_OBJECT(obuf, this->mobile.container.item.serial);
	SendPacketInRange(obuf, &this->mobile.container.item.resourceEntity.entity.location, 0x12);

	BroadcastDestroyAndRemove(&this->mobile.container.item);

	CVector_Destructor(&scriptVec);
}

/*
 * 0x0045580D - CPlayerList::RemovePlayer
 *
 * Unlinks a player from the global doubly-linked player list.
 * Adjusts next/prev pointers and updates g_PlayerList.head if needed.
 */
void
CPlayerList_RemovePlayer(CPlayer *this)
{
	if (this->next != NULL)
		this->next->prev = this->prev;

	if (this->prev != NULL) {
		this->prev->next = this->next;
	} else {
		if (g_PlayerList.head == this)
			g_PlayerList.head = this->next;
	}

	this->prev = NULL;
	this->next = NULL;
}

/*
 * 0x00455895 - CPlayerList::AddPlayer
 *
 * Removes the player from the list first (if linked), then inserts
 * at the head of the global player list.
 */
void
CPlayerList_AddPlayer(CPlayer *this)
{
	CPlayerList_RemovePlayer(this);
	this->next = g_PlayerList.head;
	if (this->next != NULL)
		this->next->prev = this;
	g_PlayerList.head = this;
}

/*
 * 0x004558DD - ItemMap_Init
 *
 * Allocates g_ItemMap and constructs it over the full map bounds with
 * blockShift 6.
 */
void
ItemMap_Init(void)
{
	CEntityMap *map;

	map = (CEntityMap *)malloc(sizeof(CEntityMap));
	if (map != NULL) {
		CEntityMap_Constructor(map, g_mapStartX, g_mapStartY, g_mapStartX + g_mapWidth - 1, g_mapStartY + g_mapHeight - 1, 6);
	}
	g_ItemMap = map;
}

/*
 * 0x00455978 - ItemMap_Insert
 *
 * Inserts entity into g_ItemMap at its current location.
 */
void
ItemMap_Insert(CItem *entity)
{
	CEntityMap_Insert(g_ItemMap, entity, (int)(int16_t)entity->resourceEntity.entity.location.x, (int)(int16_t)entity->resourceEntity.entity.location.y);
}

/*
 * 0x0045599C - ItemMap_Remove
 *
 * Removes entity from g_ItemMap at its current location.
 */
void
ItemMap_Remove(CItem *entity)
{
	CEntityMap_Remove(g_ItemMap, entity, (int)(int16_t)entity->resourceEntity.entity.location.x, (int)(int16_t)entity->resourceEntity.entity.location.y);
}

/*
 * 0x004559C0 - GetNearbyPlayers
 *
 * Fills list with players within range of loc.
 */
void
GetNearbyPlayers(CVector *list, CLocation *loc, int range)
{
	CEntityMap_RangeQuery(g_ItemMap, list, loc->x, loc->y, range);
}

/*
 * 0x004559E7 - CountPlayersInRange
 *
 * Returns the number of players within range of loc.
 */
int
CountPlayersInRange(CLocation *loc, int range)
{
	return CEntityMap_CountInRange(g_ItemMap, loc->x, loc->y, range);
}

/*
 * 0x00455A0A - GetNearbyPlayersExclude
 *
 * Fills list with players within range of loc, skipping exclude.
 */
void
GetNearbyPlayersExclude(CVector *list, CLocation *loc, int range, CItem *exclude)
{
	CEntityMap_RangeQueryExclude(g_ItemMap, list, loc->x, loc->y, range, exclude);
}

/*
 * 0x00455A35 - CollectMovementVisibility
 *
 * Classifies nearby players by visibility change during movement. The
 * mover's post-move position is passed first, pre-move second:
 * removeList stays in range, insertList enters, overlapList leaves.
 */
void
CollectMovementVisibility(CVector *removeList, CVector *insertList, CVector *overlapList, int newX, int newY, int oldX, int oldY, int range)
{
	CEntityMap_CollectMovementVisibility(g_ItemMap, removeList, insertList, overlapList, newX, newY, oldX, oldY, range);
}

/*
 * 0x00455A65 - CollectMovementVisibilityExclude
 *
 * Same as CollectMovementVisibility but skips exclude (typically the
 * moving player itself).
 */
static void
CollectMovementVisibilityExclude(CVector *removeList, CVector *insertList, CVector *overlapList, int newX, int newY, int oldX, int oldY, int range, CItem *exclude)
{
	CEntityMap_CollectMovementVisibilityExclude(g_ItemMap, removeList, insertList, overlapList, newX, newY, oldX, oldY, range, exclude);
}

/*
 * 0x00455A99 - CPlayer::BusyMessage
 *
 * Increments cooldownCounter and, for the first three hits, sends the
 * "wait a few moments" (busyType 0) or "wait to perform another action"
 * (busyType 1) system message.
 */
void
CPlayer_BusyMessage(CPlayer *this, int busyType)
{
	this->cooldownCounter++;
	if (this->cooldownCounter < 3) {
		switch (busyType) {
		case 0:
			CPlayer_SystemMessage(this, "You must wait a few moments to use another skill.");
			break;
		case 1:
			CPlayer_SystemMessage(this, "You must wait to perform another action.");
			break;
		}
	}
}

/*
 * 0x00455AFD - CPlayer::IsBusy
 *
 * Returns 1 if the player has an action in progress (sending a busy
 * message); otherwise clears cooldownCounter and returns 0.
 */
int
CPlayer_IsBusy(CPlayer *this)
{
	if (this->mobile.actionState > 0) {
		CPlayer_BusyMessage(this, 1);
		return 1;
	}
	this->cooldownCounter = 0;
	return 0;
}

/*
 * 0x00455B31 - CPlayer::HasTargetedSerial
 *
 * Returns 1 if serial appears in the player's 8-slot target history,
 * stopping at the first empty slot.
 */
int
CPlayer_HasTargetedSerial(CMobile *this, uint32_t serial)
{
	CPlayer *p = (CPlayer *)this;
	int i;

	for (i = 0; i < 8; i++) {
		if (p->targetHistory[i] == 0)
			break;
		if (p->targetHistory[i] == serial)
			return 1;
	}
	return 0;
}

/*
 * 0x00455B85 - CPlayer::SetLastTarget
 *
 * Pushes serial onto the 8-entry target history (used by GM .next /
 * .gotocur commands).
 */
void
CPlayer_SetLastTarget(CPlayer *this, uint32_t serial)
{
	int i;

	for (i = 7; i >= 1; i--)
		this->targetHistory[i] = this->targetHistory[i - 1];
	this->targetHistory[0] = serial;
}

/*
 * 0x00455BD6 - CMobile::ActionBark
 *
 * Sends text1 to the player directly and text2 to nearby players
 * with LOS (from the player's eye position), then restores the
 * player's spatial registration.
 */
void
CMobile_ActionBark(CItem *player, int hueVal, char *text1, char *text2)
{
	uint8_t obuf[0x42C];
	CLocation originalLoc;
	CLocation modifiedLoc;

	PacketManager_MakePacket_TEXT(obuf, player, player, 0, text1, (uint16_t)hueVal, 3);
	SendToClient(player, obuf, -1);

	PacketManager_MakePacket_TEXT(obuf, player, player, 0, text2, (uint16_t)hueVal, 3);

	CLocation_SetLoc(&originalLoc, CEntity_GetLocation(&player->resourceEntity.entity));
	CLocation_SetLoc(&modifiedLoc, &originalLoc);

	modifiedLoc.z += VT_GetHeight(player) / 2;

	((void (*)(void *))VT_FN(player, VT_DETACH_SPATIAL))(player);
	BroadcastToNearbyWithLOS(obuf, &modifiedLoc, 18);
	((void (*)(void *, CLocation *))VT_FN(player, VT_SET_LOCATION))(player, &originalLoc);
}

/*
 * 0x00455D26 - CPlayer::CheckWeight
 *
 * Returns 1 if the player is overweight, sending an "overloaded"
 * system message listing current and max weight.
 */
int
CPlayer_CheckWeight(CPlayer *this)
{
	int weight, maxWeight;
	CString msg;

	weight = ((int (*)(void *))VT_FN((CItem *)this, VT_GET_WEIGHT))(this);
	maxWeight = ((int (*)(void *))VT_FN((CItem *)this, VT_GET_MAX_WEIGHT))(this);

	if (weight > maxWeight) {
		CString_Constructor(&msg, "You are overloaded.  You can carry ");
		CString_ConcatInt(&msg, maxWeight);
		CString_AppendCStr(&msg, " stones, but are carrying ");
		CString_ConcatInt(&msg, weight);
		CString_AppendCStr(&msg, " stones.");
		CPlayer_SystemMessage(this, CString_GetBuffer(&msg));
		CString_Destructor(&msg);
		return 1;
	}
	return 0;
}

/*
 * 0x00455DF0 - CPlayer::ProcessDeath
 *
 * Clears the ghost statusFlag bit when a dead player is in ghost form.
 * Returns 1 on success, 0 if not dead or not a ghost.
 *
 * MODIFIED: also clears MovePrevented so modern clients (which don't
 * always reset the walk sequence) don't get stuck after a death-time
 * move-deny.
 */
int
CPlayer_ProcessDeath(CPlayer *this)
{
	if (!VT_IsDead((CItem *)this))
		return 0;
	if (!CPlayer_IsGhost(this))
		return 0;

	CMobile_SetStatusFlag(&this->mobile, 0x02, 0);
	CPlayer_SetMovePrevented(this, 0);
	return 1;
}

/*
 * 0x00455ECE - Combat_NotorietyCompare
 *
 * Compares two mobiles' notoriety, returning 1 if mob1 is
 * significantly less criminal than mob2, 0 for near-parity, or -1.
 */
int
Combat_NotorietyCompare(CMobile *mob1, CMobile *mob2)
{
	int noto2, noto1, diff;

	if (mob1 == NULL)
		return -1;
	if (mob2 == NULL)
		return -1;

	noto2 = CMobile_GetNotoLevel(mob2);

	if (noto2 >= 0) {
		if (CMobile_IsCreatureBody(mob2))
			return -1;
		if (noto2 <= 0)
			return 0;
		return -1;
	}

	// noto2 < 0 (mob2 is criminal/murderer)
	noto1 = CMobile_GetNotoLevel(mob1);
	if (noto1 > 0)
		noto1 = 0;

	diff = noto1 - noto2;
	if (diff >= 2)
		return 1;
	if (diff > 0)
		return 0;
	return -1;
}

/*
 * 0x00455FAB - CriminalAct_Notify
 *
 * Empty stub. Called from Script_criminalAct and Script_criminalActAdvanced
 * with criminal/victim mobiles and reputation parameters.
 */
void
CriminalAct_Notify(CMobile *criminal, CMobile *victim, int fameAmount, int crimeWeight, int bound, int flags)
{
	USED(criminal);
	USED(victim);
	USED(fameAmount);
	USED(crimeWeight);
	USED(bound);
	USED(flags);
}

/*
 * 0x00455FB0 - CPlayer::DeathCountDecay
 *
 * Decrements deathCount by 1 for every 1200 game ticks elapsed since
 * the last death/decay timestamp. Loops until deathCount reaches 0 or
 * not enough time has elapsed.
 * Called before resurrection checks - higher death count makes resurrection
 * harder (count > 3 denies resurrection entirely).
 */
void
CPlayer_DeathCountDecay(CPlayer *this)
{
	int elapsed;

	if (this->deathCount == 0)
		return;

	for (;;) {
		elapsed = g_GameTick - this->deathCountTimestamp;
		if (elapsed <= 0x4B0)
			return;
		this->deathCount--;
		this->deathCountTimestamp += 0x4B0;
		if (this->deathCount == 0)
			return;
	}
}

/*
 * 0x0045601B - CPlayer::IncrementDeathCount
 *
 * Called when a player dies. Increments deathCount and records the
 * current game tick as the timestamp.
 */
void
CPlayer_IncrementDeathCount(CPlayer *this)
{
	this->deathCount++;
	this->deathCountTimestamp = g_GameTick;
}

/*
 * 0x0045604A - CPlayer vtable[0x19C] FillAggroInfo
 *
 * Populates an AggroInfo with this player's entity, serial, guild tags,
 * and name.
 */
void
CPlayer_FillAggroInfo_VT(CPlayer *self, AggroInfo *info)
{
	const char *name;

	AggroInfo *ai = info;
	ai->entity = (CItem *)self;
	ai->serial = self->mobile.container.item.serial;
	ai->guildstoneId = 0;
	ai->guildType = 0;
	CResourceEntity_GetTagObj((CItem *)self, "guildstoneId", &ai->guildstoneId);
	CResourceEntity_GetTagInt((CItem *)self, "guildType", &ai->guildType);
	name = ((const char *(*)(void *))VT_FN((CItem *)self, VT_GET_NAME))(self);
	CString_AssignCStr(&ai->name, name);
}

/*
 * 0x004561D8 - CPlayer::NotifyDamage vtable[0x198]
 *
 * Damage notification for a player victim. Drives the crime/murder
 * reporting pipeline: aggressor list, lawful vs criminal marking, and
 * canReportIdList/canReportNameList accumulation.
 */
void
CPlayer_NotifyDamage_VT(CPlayer *this, CMobile *attacker, int flag)
{
	if (attacker == NULL)
		return;

	AggroInfo info;
	AggroInfo_Constructor(&info);

	((void (*)(void *, AggroInfo *))VT_FN((CItem *)attacker, VT_FILL_AGGRO_INFO))(attacker, &info);

	if (this->mobile.container.item.serial == info.serial) {
		AggroInfo_Destructor(&info);
		return;
	}

	((void (*)(void *, void *, CItem *, uint32_t))VT_FN((CItem *)this, VT_ADD_TO_AGGRESSOR_LIST))(this, attacker, info.entity, info.serial);

	int canBeAggressed = ((int (*)(void *, void *, AggroInfo *))VT_FN((CItem *)this, VT_CAN_BE_FREELY_AGGRESSED))(this, attacker, &info);

	if (canBeAggressed) {
		CResourceEntity_AddToTagList((CItem *)attacker, "lawfullyDamaged", 4, this->mobile.container.item.serial);
		ScheduleEvent(480, attacker->container.item.serial, 0x11, 2, 0);
		AggroInfo_Destructor(&info);
		return;
	}

	CMobile_SetCriminalPunishable(attacker, &this->mobile.container.item.resourceEntity.entity.location, 480);

	if (info.serial == 0) {
		AggroInfo_Destructor(&info);
		return;
	}

	if (info.serial != attacker->container.item.serial) {
		if (info.entity != NULL) {
			CMobile_SetCriminalPunishable((CMobile *)info.entity, &this->mobile.container.item.resourceEntity.entity.location, 480);
		} else {
			CList list;
			CList_Constructor(&list);
			CList_Append(&list, 3, (uintptr_t)&this->mobile.container.item.resourceEntity.entity.location);
			CString str;
			CString_Constructor(&str, "refreshCriminal");
			SendMultiMessage(info.serial, this->mobile.container.item.serial, &str, (intptr_t)&list);
			CString_Destructor(&str);
			CList_Destructor(&list);
		}
	}

	if (flag == 0) {
		AggroInfo_Destructor(&info);
		return;
	}

	int defVal = 0;
	CResourceEntity_GetTagInt((CItem *)attacker, "defensive", &defVal);
	if (defVal != 0) {
		AggroInfo_Destructor(&info);
		return;
	}

	CList *reportedList = CResourceEntity_GetTagEntity((CItem *)this, "recentlyReported");
	if (reportedList != NULL) {
		if (CList_Find(reportedList, 4, info.serial)) {
			AggroInfo_Destructor(&info);
			return;
		}
	}

	int added = CResourceEntity_AddToTagList((CItem *)this, "canReportIdList", 4, info.serial);

	if (added) {
		CList *nameList = CResourceEntity_GetTagEntity((CItem *)this, "canReportNameList");
		if (nameList == NULL) {
			TagNode *node = CEntity_SetObjVar((CItem *)this, "canReportNameList", 5, 0);
			nameList = (CList *)(uintptr_t)node->value;
		}
		CList_Append(nameList, 1, (uintptr_t)&info.name);
	}

	AggroInfo_Destructor(&info);
}

void
SendPacketToPlayer(CPlayer *player, uint8_t *buf, int size)
{
	if (player == NULL)
		return;
	if (player->usersock == NULL)
		return;
	if (size == -1)
		size = GetPacketOffset(buf);
	Socket_Copy_To_CSocketBuffer((CSocket *)player->usersock, buf, size);
}

/*
 * 0x0045646B - CPlayer::ReceiveHelpfulAction vtable[0x24C]
 *
 * When a criminal or murderer receives help from another player, flags
 * the helper as criminal-punishable at this player's location for
 * 480 ticks.
 */
void
CPlayer_ReceiveHelpfulAction(CPlayer *this, CItem *helper)
{
	if ((CItem *)&this->mobile.container.item == helper)
		return;
	if (!VT_IsPlayer(helper))
		return;
	if (!CMobile_IsCriminal((CMobile *)this) && !CMobile_IsMurderer((CMobile *)this))
		return;
	CMobile_SetCriminalPunishable((CMobile *)helper, &this->mobile.container.item.resourceEntity.entity.location, 0x1E0);
}

/*
 * 0x004564C1 - CPlayer vtable[0x1A0] FameKarmaChange
 *
 * Applies a fame and karma delta in sequence.
 */
void
CPlayer_FameKarmaChange_VT(CPlayer *self, int fame, int karma)
{
	CMobile_ChangeFame(&self->mobile, fame);
	CMobile_ChangeKarma(&self->mobile, karma);
}

/*
 * 0x004564E6 - CPlayer vtable[0x22C] murder report cleanup
 *
 * After HP regen, checks vtable[0x228] (VT_STAT_CHECK). If true
 * (murder report has expired), deletes "canReportIdList" and
 * "canReportNameList" ObjVar tags via CResourceEntity_DetachScript
 * (0x004CDEAC).
 */
void
CPlayer_MurderReportCleanup(CPlayer *this)
{
	if (((int (*)(void *))VT_FN((CItem *)this, VT_STAT_CHECK))(this)) {
		CResourceEntity_DetachScript(&this->mobile.container.item, "canReportIdList");
		CResourceEntity_DetachScript(&this->mobile.container.item, "canReportNameList");
	}
}

/*
 * 0x0045651D - CPlayer vtable[0x21C] GetStamina
 *
 * If player is editing, returns 0 (unlimited stamina).
 * Otherwise delegates to CMobile_GetStamina_VT.
 */
int
CPlayer_GetStamina_VT(CPlayer *self)
{
	if (CPlayer_IsEditing(self))
		return 0;
	return CMobile_GetStamina_VT((CMobile *)self);
}

/*
 * 0x00456540 - CPlayer::GetBugStat
 *
 * Sums 3 base stats (str+dex+int) for a player. Returns total.
 * Thiscall on CPlayer. Calls CMobile::GetBaseStat(i) for i=0,1,2.
 * The result is sign-extended from int16 before adding to the total.
 */
int
CPlayer_GetBugStat(CItem *player)
{
	CMobile *mob = (CMobile *)player;
	int total = 0;
	int i;

	for (i = 0; i < 3; i++)
		total += (int16_t)CMobile_GetBaseStat(mob, i);
	return total;
}

/*
 * 0x00456588 - CPlayerList::StatCheck
 *
 * Iterates all players. For each non-editing player whose stat total
 * (str+dex+int via GetBugStat) exceeds 275 (0x113), logs
 * "stat combo is <total>" via EventLogger and caps base stats to 50.
 * ORPHANED: zero callers, zero data refs in the binary.
 */
static __attribute__((unused)) void
CPlayerList_StatCheck(CPlayerList *this)
{
	CPlayer *player;

	USED(this);
	for (player = g_PlayerList.head; player != NULL; player = player->next) {
		if (CPlayer_IsEditing(player))
			continue;
		if (CPlayer_GetBugStat((CItem *)player) <= 0x113)
			continue;
		{
			CString str;
			CString_Constructor(&str, "stat combo is ");
			CString_ConcatInt(&str, CPlayer_GetBugStat((CItem *)player));
			EventLogger_Log(&g_EventLogger, player->accountNum, player->characterNum, CMobile_GetSerial(&player->mobile), CMobile_GetName_VT((CItem *)player),
			        "statcheck", "error", CString_GetBuffer(&str));
			CPlayer_CapBaseStats(player, 50);
			CString_Destructor(&str);
		}
	}
}

/*
 * 0x00456680 - CPlayer::CapBaseStats
 *
 * Iterates stats 0-2 (STR, DEX, INT). For each stat, if the current
 * base value exceeds the given cap, sets it to the cap.
 */
void
CPlayer_CapBaseStats(CPlayer *this, int cap)
{
	int i;
	uint16_t val;

	for (i = 0; i < 3; i++) {
		val = CMobile_GetBaseStat_Wrap(&this->mobile, i);
		if ((int)(val & 0xFFFF) > (int)(cap & 0xFFFF))
			CMobile_SetBaseStat_VT(&this->mobile, i, cap & 0xFFFF);
	}
}

/*
 * 0x004566E4 - CPlayer::HasPrivFlag4000
 *
 * Returns 1 if player+0x3A8 has bit 0x4000 set, 0 otherwise.
 * Used by IsFreelyUsable/IsFreelyViewable permission checks.
 */
int
CPlayer_HasDeadFlag(CPlayer *self)
{
	uint32_t flags;

	flags = self->pflags & 0x4000;
	return flags ? 1 : 0;
}

/*
 * 0x00456722 - CPlayer::IsGMAndManifested
 *
 * Returns 1 when the player has both the GM flag and the GM body
 * (bodyType == 0x3DB).
 */
int
CPlayer_IsGMAndManifested(CPlayer *this)
{
	if (!CPlayer_IsGameMaster(this))
		return 0;
	if (!CPlayer_HasGMBody(this))
		return 0;
	return 1;
}

/*
 * 0x0045674E - CPlayer::IsCounselor
 *
 * Checks if PlayerIsCounselor flag (0x8000) is set in pflags.
 * Returns 1 if counselor, 0 otherwise.
 */
int
CPlayer_IsCounselor(CPlayer *this)
{
	int val;

	val = this->pflags & PlayerIsCounselor;
	return val != 0 ? 1 : 0;
}

/*
 * 0x0045676D - CPlayer::IsCounselorWithGMBody
 *
 * Returns 1 if player is both a counselor and has a GM body type.
 */
int
CPlayer_IsCounselorWithGMBody(CPlayer *this)
{
	if (CPlayer_IsCounselor(this) && CPlayer_HasGMBody(this))
		return 1;
	return 0;
}

/*
 * 0x00456799 - CPlayer::GetCounselorTitle
 *
 * Reads "counType" tag from the player entity. Based on value:
 *   1 = "Seer", 2 = "Counselor", 3 = "Senior Counselor".
 * Assigns the title string to the output CString parameter.
 */
void
CPlayer_GetCounselorTitle(CPlayer *this, CString *out)
{
	int counType = 0;

	if (CResourceEntity_HasTag((CItem *)this, "counType", 0))
		CResourceEntity_GetTagInt((CItem *)this, "counType", &counType);

	switch (counType) {
	case 1:
		CString_AssignCStr(out, "Seer");
		break;
	case 2:
		CString_AssignCStr(out, "Counselor");
		break;
	case 3:
		CString_AssignCStr(out, "Senior Counselor");
		break;
	}
}

/*
 * 0x00456818 - sets the player's combat target serial
 *
 * If the target changed, updates combatTargetSerial (+0x454) and
 * sends a CURRENT_TARGET (0xAA) packet to the client.
 */
void
CPlayer_SetFightTarget(CPlayer *this, uint32_t targetSerial)
{
	uint8_t obuf[16];

	if (this->combatTargetSerial == targetSerial)
		return;
	this->combatTargetSerial = targetSerial;
	PacketManager_MakePacket_CURRENT_TARGET(obuf, this->combatTargetSerial);
	SendToClient((CItem *)this, obuf, -1);
}

/*
 * 0x00456869 - CPlayer::NotifySkillGain (vtable[0x240] override)
 *
 * Guards against editing/counselor/GM players, then calls the base
 * CMobile_NotifySkillGain for weight-based skill decay.
 */
void
CPlayer_NotifySkillGain(CPlayer *this, int8_t skillId, int actualGain)
{
	if (CPlayer_IsEditing(this))
		return;
	if (CPlayer_IsCounselor(this))
		return;
	if (CPlayer_IsGameMaster(this))
		return;
	CMobile_NotifySkillGain(&this->mobile, skillId, actualGain);
}

/*
 * 0x004568AE - CPlayer vtable[0x244] TestBehavior
 *
 * Skips stat-cap checks for editing/counselor/GM players. CPlayer
 * repurposes the TestBehavior slot for stat change gating.
 */
int
CPlayer_TestBehavior_VT(CPlayer *self, int statIdx, int delta)
{
	if (CPlayer_IsEditing(self))
		return 0;
	if (CPlayer_IsCounselor(self))
		return 0;
	if (CPlayer_IsGameMaster(self))
		return 0;
	return CMobile_OnStatChange_VT(&self->mobile, statIdx, delta);
}

/*
 * 0x004568F3 - CPlayer vtable[0x248] SetBehavior
 *
 * Sets the stat absolutely, except for editing/counselor/GM
 * players (who get their current base stat back unchanged).
 */
int
CPlayer_SetBehavior_VT(CPlayer *self, int statIdx, int value)
{
	if (CPlayer_IsEditing(self))
		return CMobile_GetBaseStat_Wrap(&self->mobile, statIdx);
	if (CPlayer_IsCounselor(self))
		return CMobile_GetBaseStat_Wrap(&self->mobile, statIdx);
	if (CPlayer_IsGameMaster(self))
		return CMobile_GetBaseStat_Wrap(&self->mobile, statIdx);
	return CMobile_SetStatAbs(&self->mobile, statIdx, value);
}

/*
 * 0x00456942 - CPlayer::GetPlayAge
 *
 * Returns the player's total play time counter (CPlayer+0x448).
 */
int
CPlayer_GetPlayAge(CPlayer *this)
{
	return (int)this->playAge;
}

/*
 * 0x00456956 - CPlayer::AddPlayAge
 *
 * Increments the player's total play time counter by amount.
 */
void
CPlayer_AddPlayAge(CPlayer *this, int amount)
{
	this->playAge += amount;
}

/*
 * 0x00456978 - CPlayerList::AddSunlight
 *
 * Iterates the player list and adds amount to each non-removed player's
 * playAge. The binary fires this every 0x7FF ticks with amount=1, so the
 * field accumulates online time.
 */
void
CPlayerList_AddSunlight(int amount)
{
	CPlayer *p;

	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		if (p->mobile.container.item.resourceEntity.entity.removedFromWorld)
			continue;
		CPlayer_AddPlayAge(p, amount);
	}
}

/*
 * 0x00456ABC - exit combat mode
 *
 * Sets or clears bit 0x20 (PlayerIsManifesting) in pflags.
 * flag=1: entering war mode → set bit 0x20
 * flag=0: entering peace mode → clear bit 0x20
 */
void
CMobile_ExitCombat(CMobile *this, int flag)
{
	CPlayer *player;

	player = (CPlayer *)this;
	if (flag)
		player->pflags |= PlayerIsManifesting;
	else
		player->pflags &= ~PlayerIsManifesting;
}

/*
 * 0x00456B52 - InitStartingEquipment
 *
 * Seeds a new player with Meat/Humans/Good karma resource nodes
 * and the matching resist flags.
 */
void
CPlayer_InitStartingEquipment(CPlayer *this)
{
	CItem *item = &this->mobile.container.item;

	CResourceEntity_AddNodeScaled(item, (uint16_t)g_ResTypeId_Meat, 3, 0xa, 0, 0xa, 1, 1, 1);
	CResourceEntity_AddNodeScaled(item, (uint16_t)g_ResTypeId_Humans, 3, 0xa, 0, 0xa, 1, 1, 1);
	CResourceEntity_AddNodeScaled(item, (uint16_t)g_ResTypeId_Good, 3, 1, 0, 1, 1, 1, 1);

	// Resist flags: 1=meat/prey, 2=human, 8=good alignment
	CMobile_SetResistFlag(&this->mobile, 1, 1);
	CMobile_SetResistFlag(&this->mobile, 2, 1);
	CMobile_SetResistFlag(&this->mobile, 8, 1);
}

/*
 * 0x00456BD7 - CPlayer::TransferResourcesToCorpse
 *
 * Moves non-alignment resource nodes from the player to a corpse,
 * freeing alignment and zero/type-mismatched nodes, then clears
 * resist flags 1, 2, 4, 8 on the player.
 */
void
CPlayer_TransferResourcesToCorpse(CPlayer *this, CItem *target)
{
	CItem *item = &this->mobile.container.item;
	CResourceNode *node;

	while (item->resourceEntity.firstChild != NULL) {
		node = item->resourceEntity.firstChild;
		CResourceEntity_RemoveNode(item, node);

		if (node->id == 0 || node->type != 3 || node->id == (uint16_t)g_ResTypeId_Humans || node->id == (uint16_t)g_ResTypeId_Good ||
		        node->id == (uint16_t)g_ResTypeId_Evil) {
			ResourceNode_ReturnToPool(node);
			continue;
		}

		CResourceEntity_InsertNode(target, node);
	}

	CMobile_SetResistFlag(&this->mobile, 1, 0);
	CMobile_SetResistFlag(&this->mobile, 2, 0);
	CMobile_SetResistFlag(&this->mobile, 8, 0);
	CMobile_SetResistFlag(&this->mobile, 4, 0);
}

/*
 * 0x00456C9E - CPlayer::KarmaHandler vtable[0x250]
 *
 * Swaps Good/Evil alignment resources when the player crosses the
 * murder threshold (mode 1 = became non-murderer, mode 2 = became
 * murderer).
 */
void
CPlayer_KarmaHandler(CPlayer *this, int mode)
{
	CItem *item = &this->mobile.container.item;

	CResourceEntity_RemoveResource(item, 0x3E8, g_ResTypeId_Good);
	CResourceEntity_RemoveResource(item, 0x3E8, g_ResTypeId_Evil);

	if (mode == 1) {
		if (g_ResTypeId_Evil == 0 || g_ResTypeId_Good == 0)
			return;
		CResourceEntity_AddNodeScaled(item, (uint16_t)g_ResTypeId_Good, 3, 1, 0, 1, 1, 1, 1);
		CMobile_SetResistFlag(&this->mobile, 4, 0);
		CMobile_SetResistFlag(&this->mobile, 8, 1);
	} else if (mode == 2) {
		if (g_ResTypeId_Good == 0 || g_ResTypeId_Evil == 0)
			return;
		CResourceEntity_AddNodeScaled(item, (uint16_t)g_ResTypeId_Evil, 3, 1, 0, 1, 1, 1, 1);
		CMobile_SetResistFlag(&this->mobile, 8, 0);
		CMobile_SetResistFlag(&this->mobile, 4, 1);
	}
}

/*
 * 0x00456D6D - CMobile::SetResurrectionResources
 *
 * Adds Meat and Humans resource nodes, sets resist flags 1/2, and
 * invokes KarmaHandler with the current murder/non-murderer mode.
 */
void
CMobile_SetResurrectionResources(CMobile *mob)
{
	int val;

	CResourceEntity_AddNodeScaled(&mob->container.item, (uint16_t)g_ResTypeId_Meat, 3, 0xa, 0, 0xa, 1, 1, 1);

	CResourceEntity_AddNodeScaled(&mob->container.item, (uint16_t)g_ResTypeId_Humans, 3, 0xa, 0, 0xa, 1, 1, 1);

	CMobile_SetResistFlag(mob, 1, 1);
	CMobile_SetResistFlag(mob, 2, 1);

	val = CMobile_IsMurderer(mob) ? 2 : 1;
	((void (*)(void *, int))VT_FN((CItem *)mob, VT_KARMA_HANDLER))(mob, val);
}

/*
 * 0x00456DF1 - CPlayer::SetMurderCount vtable[0x17C]
 *
 * Updates the player's murder count and calls KarmaHandler when the
 * count crosses the 5-kill threshold in either direction.
 */
#define MURDER_THRESHOLD 5

void
CPlayer_IncrementMurderCount(CPlayer *this, int newCount)
{
	int oldCount;

	oldCount = CItem_GetMurderCount(&this->mobile.container.item);

	if (oldCount >= MURDER_THRESHOLD && newCount < MURDER_THRESHOLD) {
		((void (*)(void *, int))VT_FN((CItem *)this, VT_KARMA_HANDLER))(this, 1);
	} else if (oldCount < MURDER_THRESHOLD && newCount >= MURDER_THRESHOLD) {
		((void (*)(void *, int))VT_FN((CItem *)this, VT_KARMA_HANDLER))(this, 2);
	}

	CItem_SetMurderCount(&this->mobile.container.item, newCount);
}

/*
 * 0x00456E51 - CPlayer vtable[0x200] SetNotoriety
 *
 * Delegates to CMobile_SetNotoriety_VT.
 */
void
CPlayer_SetNotoriety_VT(CPlayer *self, int value)
{
	CMobile_SetNotoriety_VT((CMobile *)self, value);
}

/*
 * 0x00456E6A - CPlayer::IsGoldAccount
 *
 * Checks if PlayerIsGold flag (0x00020000) is set in pflags.
 * Returns 1 if gold account, 0 otherwise.
 */
int
CPlayer_IsGoldAccount(CPlayer *this)
{
	if (this->pflags & PlayerIsGold)
		return 1;
	return 0;
}

/*
 * 0x00456EE0 - CPlayer scalar deleting destructor
 *
 * Scalar deleting destructor: runs CPlayer_Destructor and frees the player
 * when flags & 1.
 */
void *
CPlayer_ScalarDelete(CPlayer *this, int flags)
{
	CPlayer_Destructor(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x00456F10 - CPlayer::SetLocation
 *
 * Copies a CLocation to the player's entity location via CLocation_SetLoc.
 */
void
CPlayer_SetLocation(CPlayer *this, CLocation *loc)
{
	CLocation_SetLoc(&this->mobile.container.item.resourceEntity.entity.location, loc);
}

/*
 * 0x00456F30 - CPlayer::SetBodyType
 *
 * Sets the player's body type via CEntity_SetBodyType.
 */
void
CPlayer_SetBodyType(CPlayer *this, uint16_t bodyType)
{
	CEntity_SetBodyType(&this->mobile.container.item, bodyType);
}

/*
 * 0x00456F50 - CTimeManager::IsDaytime
 *
 * Returns 1 if the current hour is between 6 and 18 inclusive.
 */
int
CTimeManager_IsDaytime(void)
{
	if (g_TimeManager.hour >= 6 && g_TimeManager.hour <= 18)
		return 1;
	return 0;
}

/*
 * 0x00456F90 - Double3_Init wrapper
 *
 * Forwards to Double3_Init and returns this.
 */
static __attribute__((unused)) CSkillDef *
Double3_Init_Wrapper(CSkillDef *this, double maxSkill, double lower, double middle, double higher)
{
	Double3_Init(this, maxSkill, lower, middle, higher);
	return this;
}

/*
 * 0x00456FD0 - CSkillUseCtx::CSkillUseCtx
 *
 * Zero-initializes a CSkillUseCtx with an empty name.
 */
CSkillUseCtx *
CSkillUseCtx_Init(CSkillUseCtx *this)
{
	this->type = 0;
	this->serial = 0;
	this->field08 = 0;
	this->field0C = 0;
	CLocation_Init(&this->location);
	strcpy(this->name, "");
	return this;
}

/*
 * 0x00457030 - AggroInfo ctor
 *
 * Thiscall on AggroInfo. Calls CString_DefaultConstructor on the
 * CString at +0x10.
 */
void
AggroInfo_Constructor(AggroInfo *info)
{
	CString_DefaultConstructor(&info->name);
}

/*
 * 0x00457050 - AggroInfo dtor
 *
 * Thiscall on AggroInfo. Calls CString_Destructor on the CString
 * at +0x10.
 */
void
AggroInfo_Destructor(AggroInfo *info)
{
	CString_Destructor(&info->name);
}

/*
 * 0x00457070 - CEntityMap::CEntityMap
 * 0x00461DD0 (COMDAT)
 * 0x00484420 (COMDAT)
 *
 * Builds the spatial grid for [startX,endX] x [startY,endY] at the
 * given blockShift and allocates an empty StdPtrList per block.
 */
CEntityMap *
CEntityMap_Constructor(CEntityMap *this, int startX, int startY, int endX, int endY, int blockShift)
{
	int totalBlocks;
	int i;
	uint8_t *raw;

	this->blockShift = blockShift;

	this->originX = startX >> blockShift;

	this->endX = (endX + (1 << blockShift) - 1) >> blockShift;

	this->originY = startY >> blockShift;

	this->endY = (endY + (1 << blockShift) - 1) >> blockShift;

	this->gridW = this->endX - this->originX + 1;

	this->gridH = this->endY - this->originY + 1;

	totalBlocks = this->gridW * this->gridH;

	// Custom: 64-bit - sizeof(uintptr_t) header for alignment
	raw = (uint8_t *)malloc(totalBlocks * sizeof(StdPtrList) + sizeof(uintptr_t));
	*(uint32_t *)raw = totalBlocks;
	this->blocks = (StdPtrList *)(raw + sizeof(uintptr_t));

	{
		uint8_t initByte = 0;
		for (i = 0; i < totalBlocks; i++)
			StdPtrList_Init(&this->blocks[i], &initByte);
	}

	this->count = 0;

	return this;
}

/*
 * 0x004571B0 - CEntityMap::Insert
 * 0x00461F10 (COMDAT)
 * 0x004845A0 (COMDAT)
 *
 * Appends entity to the block at (x, y) and increments count.
 */
void
CEntityMap_Insert(CEntityMap *this, CItem *entity, int x, int y)
{
	int idx;
	StdPtrNode *endIter;
	StdPtrNode *result;

	idx = CEntityMap_GetBlockIdx(this, x, y);

	StdPtrList_End(&this->blocks[idx], &endIter);
	StdPtrList_DoInsert(&this->blocks[idx], &result, endIter, entity);

	this->count++;
}

/*
 * 0x004571F0 - CEntityMap::Remove
 *
 * Removes the first node matching entity from the block at (x, y)
 * and decrements count.
 */
void
CEntityMap_Remove(CEntityMap *this, CItem *entity, int x, int y)
{
	int idx;
	StdPtrNode *iter;
	StdPtrNode *beginTemp;
	StdPtrNode *endNode;
	StdPtrNode *postIncTemp;
	StdPtrNode *resultIter;

	StdPtrIter_BaseConstructor(&iter);

	idx = CEntityMap_GetBlockIdx(this, x, y);

	// Get begin iterator via temp + copy
	StdPtrList_Begin(&this->blocks[idx], &beginTemp);
	iter = beginTemp;

	while (1) {
		// Get end iterator
		StdPtrList_End(&this->blocks[idx], &endNode);

		// Check iter != end
		if (!StdPtrIter_Neq(&iter, &endNode))
			break;

		// Deref and compare
		if (*StdPtrIter_Deref(&iter) == (void *)entity) {
			// Found - erase and decrement count
			StdPtrList_Erase(&this->blocks[idx], &resultIter, iter);
			this->count--;
			return;
		}

		// Advance iterator
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * 0x004572B0 - CEntityMap::CountInRange
 *
 * Returns the number of entities within Chebyshev distance range of
 * (x, y).
 */
int
CEntityMap_CountInRange(CEntityMap *this, int16_t x, int16_t y, int range)
{
	int startBlockX, endBlockX, startBlockY, endBlockY;
	int blockIdx, rowWidth;
	int bx, by;
	int count;
	StdPtrNode *iter, *endNode, *copyIter, *tmpIter;

	count = 0;

	startBlockX = ((int)x - range) >> this->blockShift;
	startBlockX -= this->originX;
	endBlockX = ((int)x + range) >> this->blockShift;
	endBlockX -= this->originX;

	startBlockY = ((int)y - range) >> this->blockShift;
	startBlockY -= this->originY;
	endBlockY = ((int)y + range) >> this->blockShift;
	endBlockY -= this->originY;

	if (startBlockX < 0)
		startBlockX = 0;
	if (endBlockX >= this->gridW)
		endBlockX = this->gridW - 1;
	if (startBlockY < 0)
		startBlockY = 0;
	if (endBlockY >= this->gridH)
		endBlockY = this->gridH - 1;

	blockIdx = startBlockY * this->gridW + startBlockX;
	rowWidth = endBlockX - startBlockX + 1;

	for (by = startBlockY; by <= endBlockY; by++) {
		for (bx = startBlockX; bx <= endBlockX; bx++) {
			StdPtrIter_Constructor(&iter);
			StdPtrList_Begin(&this->blocks[blockIdx], &copyIter);
			iter = copyIter;

			while (1) {
				StdPtrList_End(&this->blocks[blockIdx], &endNode);
				StdPtrIter_CopyConstructor(&tmpIter, &endNode);
				if (!StdPtrIter_Neq(&iter, &tmpIter))
					break;

				{
					void *entity = *StdPtrIter_Deref(&iter);
					if (CMobile_DistXY(entity, (int)x, (int)y) <= range)
						count++;
				}

				StdPtrIter_PostInc(&iter, &tmpIter, 0);
			}

			blockIdx++;
		}
		blockIdx += this->gridW - rowWidth;
	}

	return count;
}

/*
 * 0x00457480 - CEntityMap::RangeQueryExclude
 *
 * Like RangeQuery but skips exclude.
 */
void
CEntityMap_RangeQueryExclude(CEntityMap *this, CVector *list, int16_t x, int16_t y, int range, CItem *exclude)
{
	int startBlockX, endBlockX, startBlockY, endBlockY;
	int blockIdx, rowWidth;
	int bx, by;
	StdPtrNode *iter, *endNode, *copyIter, *tmpIter;

	startBlockX = ((int)x - range) >> this->blockShift;
	startBlockX -= this->originX;
	endBlockX = ((int)x + range) >> this->blockShift;
	endBlockX -= this->originX;

	startBlockY = ((int)y - range) >> this->blockShift;
	startBlockY -= this->originY;
	endBlockY = ((int)y + range) >> this->blockShift;
	endBlockY -= this->originY;

	if (startBlockX < 0)
		startBlockX = 0;
	if (endBlockX >= this->gridW)
		endBlockX = this->gridW - 1;
	if (startBlockY < 0)
		startBlockY = 0;
	if (endBlockY >= this->gridH)
		endBlockY = this->gridH - 1;

	blockIdx = startBlockY * this->gridW + startBlockX;
	rowWidth = endBlockX - startBlockX + 1;

	for (by = startBlockY; by <= endBlockY; by++) {
		for (bx = startBlockX; bx <= endBlockX; bx++) {
			StdPtrIter_Constructor(&iter);
			StdPtrList_Begin(&this->blocks[blockIdx], &copyIter);
			iter = copyIter;

			while (1) {
				StdPtrList_End(&this->blocks[blockIdx], &endNode);
				StdPtrIter_CopyConstructor(&tmpIter, &endNode);
				if (!StdPtrIter_Neq(&iter, &tmpIter))
					break;

				{
					void *entity = *StdPtrIter_Deref(&iter);
					if (entity != (void *)exclude) {
						void *e2 = *StdPtrIter_Deref(&iter);
						if (CMobile_DistXY(e2, (int)x, (int)y) <= range) {
							void *e3 = *StdPtrIter_Deref(&iter);
							CVector_PushBack(list, (uintptr_t)e3);
						}
					}
				}

				StdPtrIter_PostInc(&iter, &tmpIter, 0);
			}

			blockIdx++;
		}
		blockIdx += this->gridW - rowWidth;
	}
}

/*
 * 0x00457660 - CEntityMap::CollectMovementVisibility
 *
 * Classifies entities by visibility change for a mover stepping to a
 * new position from an old one. Callers pass the mover's post-move
 * position first and pre-move position second. removeList: in range
 * of both (stays visible). insertList: in range of the new position
 * only (enters visibility). overlapList: in range of the old position
 * only (leaves visibility).
 */
void
CEntityMap_CollectMovementVisibility(CEntityMap *this, CVector *removeList, CVector *insertList, CVector *overlapList, int newX, int newY, int oldX, int oldY, int range)
{
	int startBlockX, endBlockX, startBlockY, endBlockY;
	int blockIdx, rowWidth;
	int bx, by;
	int extent;
	StdPtrNode *iter, *endNode, *copyIter, *tmpIter;

	extent = range + ChebyshevDistXY(newX, newY, oldX, oldY);

	startBlockX = (newX - extent) >> this->blockShift;
	startBlockX -= this->originX;
	endBlockX = (newX + extent) >> this->blockShift;
	endBlockX -= this->originX;

	startBlockY = (newY - extent) >> this->blockShift;
	startBlockY -= this->originY;
	endBlockY = (newY + extent) >> this->blockShift;
	endBlockY -= this->originY;

	// Clamp to grid bounds
	if (startBlockX < 0)
		startBlockX = 0;
	if (endBlockX >= this->gridW)
		endBlockX = this->gridW - 1;
	if (startBlockY < 0)
		startBlockY = 0;
	if (endBlockY >= this->gridH)
		endBlockY = this->gridH - 1;

	blockIdx = startBlockY * this->gridW + startBlockX;
	rowWidth = endBlockX - startBlockX + 1;

	for (by = startBlockY; by <= endBlockY; by++) {
		for (bx = startBlockX; bx <= endBlockX; bx++) {
			StdPtrIter_Constructor(&iter);
			StdPtrList_Begin(&this->blocks[blockIdx], &copyIter);
			iter = copyIter;

			while (1) {
				StdPtrList_End(&this->blocks[blockIdx], &endNode);
				StdPtrIter_CopyConstructor(&tmpIter, &endNode);
				if (!StdPtrIter_Neq(&iter, &tmpIter))
					break;

				{
					void *entity = *StdPtrIter_Deref(&iter);
					int distNew;

					distNew = CMobile_DistXY(entity, newX, newY);

					if (distNew <= range) {
						int distOld;

						distOld = CMobile_DistXY(entity, oldX, oldY);

						if (distOld <= range) {
							void *e = *StdPtrIter_Deref(&iter);
							CVector_PushBack(removeList, (uintptr_t)e);
						} else {
							void *e = *StdPtrIter_Deref(&iter);
							CVector_PushBack(insertList, (uintptr_t)e);
						}
					} else {
						int distOld;

						distOld = CMobile_DistXY(entity, oldX, oldY);

						if (distOld <= range) {
							void *e = *StdPtrIter_Deref(&iter);
							CVector_PushBack(overlapList, (uintptr_t)e);
						}
					}
				}

				StdPtrIter_PostInc(&iter, &tmpIter, 0);
			}

			blockIdx++;
		}
		blockIdx += this->gridW - rowWidth;
	}
}

/*
 * 0x004578B0 - ChebyshevDistXY
 *
 * Returns max(|x1-x2|, |y1-y2|).
 */
int
ChebyshevDistXY(int x1, int y1, int x2, int y2)
{
	int dx = x1 - x2;
	int dy = y1 - y2;

	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	return dx > dy ? dx : dy;
}

/*
 * 0x00457C80 - CEntityMap::GetBlockIdx
 *
 * Maps world (x, y) to a flat block index in the spatial grid.
 */
int
CEntityMap_GetBlockIdx(CEntityMap *this, int x, int y)
{
	int blockY, blockX;

	blockY = (y >> this->blockShift) - this->originY;
	blockX = (x >> this->blockShift) - this->originX;
	return blockY * this->gridW + blockX;
}

/*
 * 0x00457DC0 - CEntityMap::RangeQuery
 * 0x004064E0 (COMDAT)
 * 0x004621A0 (COMDAT)
 * 0x00462400 (COMDAT)
 *
 * Appends every entity within Chebyshev distance range of (x, y)
 * to list.
 */
void
CEntityMap_RangeQuery(CEntityMap *this, CVector *list, int16_t x, int16_t y, int range)
{
	int startBlockX, endBlockX, startBlockY, endBlockY;
	int blockIdx, rowWidth;
	int bx, by;
	StdPtrNode *iter, *endNode, *copyIter, *tmpIter;

	// Compute block bounds from coordinate range
	startBlockX = ((int)x - range) >> this->blockShift;
	startBlockX -= this->originX;
	endBlockX = ((int)x + range) >> this->blockShift;
	endBlockX -= this->originX;

	startBlockY = ((int)y - range) >> this->blockShift;
	startBlockY -= this->originY;
	endBlockY = ((int)y + range) >> this->blockShift;
	endBlockY -= this->originY;

	// Clamp to grid bounds
	if (startBlockX < 0)
		startBlockX = 0;
	if (endBlockX >= this->gridW)
		endBlockX = this->gridW - 1;
	if (startBlockY < 0)
		startBlockY = 0;
	if (endBlockY >= this->gridH)
		endBlockY = this->gridH - 1;

	// Compute initial block index and row width
	blockIdx = startBlockY * this->gridW + startBlockX;
	rowWidth = endBlockX - startBlockX + 1;

	for (by = startBlockY; by <= endBlockY; by++) {
		for (bx = startBlockX; bx <= endBlockX; bx++) {
			// Init iterator, get begin
			StdPtrIter_Constructor(&iter);
			StdPtrList_Begin(&this->blocks[blockIdx], &copyIter);
			iter = copyIter;

			while (1) {
				// Get end, compare
				StdPtrList_End(&this->blocks[blockIdx], &endNode);
				StdPtrIter_CopyConstructor(&tmpIter, &endNode);
				if (!StdPtrIter_Neq(&iter, &tmpIter))
					break;

				// Distance check via CMobile_DistXY
				{
					void *entity = *StdPtrIter_Deref(&iter);
					if (CMobile_DistXY(entity, (int)x, (int)y) <= range) {
						void *e2 = *StdPtrIter_Deref(&iter);
						CVector_PushBack(list, (uintptr_t)e2);
					}
				}

				// Post-increment
				StdPtrIter_PostInc(&iter, &tmpIter, 0);
			}

			blockIdx++;
		}
		// Skip to next row: advance by gridW - rowWidth
		blockIdx += this->gridW - rowWidth;
	}
}

/*
 * 0x0045A733 - StaticInit_LoginScriptList
 *
 * Constructs g_loginScriptList as an empty StdPtrList at startup.
 */
static __attribute__((unused)) void
StaticInit_LoginScriptList(void)
{
	uint8_t initByte = 0;
	StdPtrList_Init(&g_loginScriptList, &initByte);
}

/*
 * 0x0045A76A - LoadAll_LoginScriptEntries
 *
 * Repopulates g_loginScriptList from loginscr.txt, freeing any prior
 * entries first.
 */
void
LoadAll_LoginScriptEntries(void)
{
	FILE *f;
	char line[512];
	char name[512];
	int ret;
	StdPtrNode *beginIter, *endIter, *eraseResult;
	char *str;
	int len;

	// Phase 1: clear existing entries (binary: while begin != end, free + erase).
	for (;;) {
		StdPtrList_Begin(&g_loginScriptList, &beginIter);
		StdPtrList_End(&g_loginScriptList, &endIter);
		if (StdPtrIter_Eq(&beginIter, &endIter))
			break;
		str = *(char **)StdPtrIter_Deref(&beginIter);
		free(str);
		StdPtrList_Erase(&g_loginScriptList, &eraseResult, beginIter);
	}

	// Phase 2: open loginscr.txt, read script names.
	f = FileManager_OpenByType(0x37, NULL, "r");
	if (f == NULL)
		return;
	while (fgets_ServerSide(line, 0x1FF, f) != NULL) {
		ret = sscanf(line, "%s", name);
		if (ret != 1)
			continue;
		len = strlen(name);
		str = (char *)malloc(len + 1);
		strcpy(str, name);
		StdPtrList_PushBack(&g_loginScriptList, str);
	}
	fclose_ServerSide(f);
}

/*
 * 0x0045A8B3 - Player_AttachStartupScripts_Inner
 *
 * Attaches each script name in g_loginScriptList to item.
 */
void
Player_AttachStartupScripts_Inner(CItem *item)
{
	StdPtrNode *iter, *beginTemp, *copyTemp, *endTemp;
	StdPtrNode *endCopy, *postIncTemp;

	StdPtrIter_Constructor(&iter);
	StdPtrIter_CopyConstructor(&copyTemp, StdPtrList_Begin(&g_loginScriptList, &beginTemp));
	iter = copyTemp;
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End(&g_loginScriptList, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		Entity_AttachScript(item, *(char **)StdPtrIter_Deref(&iter), 1);
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}
/*
 * 0x0045EC66 - CPlayerList::SendToAllInRange
 *
 * Sends buf to every online player stored in list.
 */
static __attribute__((unused)) void
CPlayerList_SendToAllInRange(CResList *list, uint8_t *buf)
{
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		if (CPlayer_IsPlayerOnline(*(CPlayer **)CResList_GetData(list, node))) {
			SendToClient((CItem *)*(CPlayer **)CResList_GetData(list, node), buf, -1);
		}
		node = CResList_Next(list, node);
	}
}

/*
 * 0x0045F2E0 - CPlayerList destructor
 *
 * Thiscall. Calls CPlayerList_DisconnectAll.
 */
static void
CPlayerList_Destructor(CResList *this)
{
	CPlayerList_DisconnectAll(this);
}

/*
 * 0x0045F520 - CPlayerList::DisconnectAll
 *
 * Empties a CResList of players, erasing and freeing each node.
 */
void
CPlayerList_DisconnectAll(CResList *list)
{
	CResListNode *node;

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		node = CResList_EraseAndFree_Spawn(list, node, 1);
	}
}

/*
 * 0x00460AB0 - MobileMap_Init
 *
 * Allocates g_MobileMap and constructs it over the full map bounds
 * with blockShift 6.
 */
void
MobileMap_Init(void)
{
	CEntityMap *map;

	map = (CEntityMap *)malloc(sizeof(CEntityMap));
	if (map != NULL) {
		CEntityMap_Constructor(map, g_mapStartX, g_mapStartY, g_mapStartX + g_mapWidth - 1, g_mapStartY + g_mapHeight - 1, 6);
	}
	g_MobileMap = map;
}

/*
 * 0x00460B4B - MobileMap_Insert
 *
 * Inserts a top-level entity into g_MobileMap at its current
 * location. Entities with a parent are skipped.
 */
void
MobileMap_Insert(CItem *entity)
{
	if (entity->parent != NULL)
		return;
	CEntityMap_Insert(g_MobileMap, entity, (int)(int16_t)entity->resourceEntity.entity.location.x, (int)(int16_t)entity->resourceEntity.entity.location.y);
}

/*
 * 0x00460B78 - MobileMap_Remove
 *
 * Removes a top-level entity from g_MobileMap at its current location.
 * Entities with a parent are skipped.
 */
void
MobileMap_Remove(CItem *entity)
{
	if (entity->parent != NULL)
		return;
	CEntityMap_Remove(g_MobileMap, entity, (int)(int16_t)entity->resourceEntity.entity.location.x, (int)(int16_t)entity->resourceEntity.entity.location.y);
}

/*
 * 0x00469510 - CPlayerList dtor wrapper
 *
 * Thiscall wrapper. Delegates to CPlayerList_Destructor which in turn
 * calls CPlayerList_DisconnectAll. Registered via atexit for
 * global CPlayerList at 0x006999A0.
 */
static __attribute__((unused)) void
CPlayerList_DestructorWrapper(CResList *this)
{
	CPlayerList_Destructor(this);
}

/*
 * 0x00484DA1 - CPlayer vtable[0x090] Delete
 *
 * Same as CItem_Delete but with an additional guard: checks
 * g_DeleteAllowed (binary 0x00645B04) before proceeding.
 */
void
CPlayer_Delete_VT(CItem *item)
{
	if (!g_DeleteAllowed)
		return;
	if (!CItem_DeleteCheck1(item))
		return;
	if (!CItem_DeleteCheck2(item))
		return;
	if (item != NULL)
		((void *(*)(void *, int))VT_FN(item, VT_DTOR))(item, 1);
}

/*
 * 0x0048667A - CPlayer vtable[0x040] GetSurfaceFlags
 *
 * Returns 0 if dead, or if editing and hidden (player can pass through
 * terrain). Otherwise delegates to CMobile_GetSurfaceFlags_VT.
 */
int
CPlayer_GetSurfaceFlags_VT(CPlayer *self, int moveType)
{
	if (VT_IsDead((CItem *)self))
		return 0;
	if (!CPlayer_IsEditing(self))
		goto fallthrough;
	if (!VT_IsHidden((CItem *)self))
		goto fallthrough;
	return 0;
fallthrough:
	return CMobile_GetSurfaceFlags_VT(&self->mobile, moveType);
}

/*
 * 0x00488209 - CPlayer vtable[0x00C] Hide
 *
 * CPlayer override of VT_HIDE. Removes the player from the entity map
 * (no-op in our C implementation which uses spatial grid filtering),
 * notifies multi detach, removes tracking node, updates contain info,
 * stops combat, cancels trade, builds and broadcasts DESTROY_OBJECT
 * packet, then unlinks from spatial/container chains and marks removed.
 */
void
CPlayer_HideVT(CItem *self)
{
	CContainer *parentCont;
	uint8_t pktBuf[16];
	int blockIdx;

	CItem_AdjustParentWeight(self, self->parent);

	ItemMap_Remove(self);

	CItem_NotifyMultiDetach(self, 1);
	CResourceEntity_NotifyPreModify(self);
	Block_RemoveTrackingNode(self);
	CItem_UpdateContainInfo(self, 0);
	CMobile_StopCombat((CMobile *)self);
	((void (*)(void *))VT_FN(self, VT_CANCEL_TRADE))(self);
	PacketManager_MakePacket_DESTROY_OBJECT(pktBuf, self->serial);

	if (g_WorldActive && !CItem_IsServerOnly(self)) {
		CPlayerList_BroadcastInRange(pktBuf, &self->resourceEntity.entity.location, 0x12, (CPlayer *)self);
	}

	// Unlink from spatial doubly-linked list
	if (self->spatialNext != NULL)
		self->spatialNext->spatialPrev = self->spatialPrev;
	if (self->spatialPrev != NULL)
		self->spatialPrev->spatialNext = self->spatialNext;

	if (self->parent != NULL) {
		// In a container: update parent's contents head if needed
		parentCont = (CContainer *)self->parent;
		if (parentCont->contents == self)
			parentCont->contents = self->spatialNext;
	} else {
		// On ground: update spatial grid block head if needed
		blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &self->resourceEntity.entity.location, 0);
		if (g_SpatialGrid.cells[blockIdx].itemHead == self)
			g_SpatialGrid.cells[blockIdx].itemHead = self->spatialNext;
	}

	self->spatialPrev = NULL;
	self->spatialNext = NULL;
	self->parent = NULL;
	self->resourceEntity.entity.removedFromWorld = 1;
}

/*
 * 0x004886DF - CPlayer vtable[0x010] DetachSpatial
 *
 * CPlayer override of VT_DETACH_SPATIAL. Same as CItem_DetachFromSpatial
 * but first removes the player from the entity map (no-op in our C
 * implementation which uses spatial grid filtering instead of std::multiset).
 */
void
CPlayer_DetachSpatial_VT(CItem *self)
{
	CContainer *parentCont;
	int blockIdx;

	CItem_AdjustParentWeight(self, self->parent);

	ItemMap_Remove(self);

	CItem_NotifyMultiDetach(self, 0);
	CResourceEntity_NotifyPreModify(self);
	Block_RemoveTrackingNode(self);
	CItem_UpdateContainInfo(self, 0);

	// Unlink from spatial doubly-linked list
	if (self->spatialNext != NULL)
		self->spatialNext->spatialPrev = self->spatialPrev;
	if (self->spatialPrev != NULL)
		self->spatialPrev->spatialNext = self->spatialNext;

	if (self->parent != NULL) {
		// In a container: update parent's contents head if needed
		parentCont = (CContainer *)self->parent;
		if (parentCont->contents == self)
			parentCont->contents = self->spatialNext;
	} else {
		// On ground: update spatial grid block head if needed
		blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &self->resourceEntity.entity.location, 0);
		if (g_SpatialGrid.cells[blockIdx].itemHead == self)
			g_SpatialGrid.cells[blockIdx].itemHead = self->spatialNext;
	}

	self->spatialPrev = NULL;
	self->spatialNext = NULL;
	self->parent = NULL;
	self->resourceEntity.entity.removedFromWorld = 1;
}
/*
 * 0x004894B1 - CPlayer vtable[0x08] SetLocation
 *
 * CPlayer override of VT_SET_LOCATION. Unlike CItem_SetLocation_VT,
 * this version calls CPlayer_UpdateLastValidLocation before InternalMove
 * and inserts the player into g_ItemMap via ItemMap_Insert after.
 * Omits the IsValueless/decay check (players are never valueless).
 */
void
CPlayer_SetLocation_VT(CItem *self, CLocation *loc)
{
	if ((int16_t)self->resourceEntity.entity.location.x == -1)
		CLocation_SetLoc(&self->resourceEntity.entity.location, loc);

	CPlayer_UpdateLastValidLocation((CPlayer *)self, loc);

	CItem_InternalMove(self, loc, 1);

	ItemMap_Insert(self);

	self->resourceEntity.entity.removedFromWorld = 0;

	UpdateRegion(self);

	CResourceEntity_NotifyPostModifyIfActive(self);

	CItem_ReleaseTracking(self);
}

/*
 * 0x004895ED - CPlayer::ClearTargetSerial
 *
 * Zeroes the player's targetSerial.
 */
void
CPlayer_ClearTargetSerial(CPlayer *this)
{
	this->targetSerial = 0;
}

/*
 * 0x00489605 - CPlayer::ReturnToHome
 *
 * Teleports the player to lastValidLocation (or the map origin if
 * invalid) via VT_HIDE then VT_DROP_AT_FEET. Always returns 1.
 */
int
CPlayer_ReturnToHome(CPlayer *this)
{
	CLocation loc;
	CLocation tempLoc;

	if (!this->mobile.container.item.resourceEntity.entity.removedFromWorld) {
		((void (*)(void *))VT_FN((CItem *)this, VT_HIDE))(this);
	}

	CLocation_Init(&loc);

	if (CLocation_IsInvalid(&this->lastValidLocation)) {
		CLocation_Init(&tempLoc);
		CLocation_Set(&tempLoc, g_mapStartX, g_mapStartY, 0);
		CLocation_SetLoc(&loc, &tempLoc);
	} else {
		CLocation_SetLoc(&loc, &this->lastValidLocation);
	}

	if (CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc.x, (int16_t)loc.y)) {
		((void (*)(void *, CLocation *))VT_FN((CItem *)this, VT_DROP_AT_FEET))(this, &loc);
	}

	return 1;
}

/*
 * 0x004896B1 - CPlayer::UpdateLastValidLocation
 *
 * Records the location as lastValidLocation if the terrain tile is
 * non-water and matches the location's z.
 */
void
CPlayer_UpdateLastValidLocation(CPlayer *this, CLocation *loc)
{
	int blockIdx;
	CBlock *block;
	int cellOff;
	int8_t cellZ;
	uint16_t tileId;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	block = &g_SpatialGrid.cells[blockIdx];

	// Each cell in the 8x8 grid is 4 bytes at offset (y&7)*32 + (x&7)*4
	cellOff = ((loc->y & 7) << 5) + ((loc->x & 7) << 2);
	cellZ = (int8_t)block->pad00[cellOff + 2];
	tileId = *(uint16_t *)&block->pad00[cellOff];

	if (IsWaterTile(tileId))
		return;

	if (cellZ != loc->z)
		return;

	CLocation_SetLoc(&this->lastValidLocation, loc);
}

/*
 * 0x00489766 - CPlayer vtable[0x04] DropAtFeet
 *
 * Places the player at loc, updating home/last-valid location,
 * re-inserting into the spatial grid, and pushing ZMOVE/SUNLIGHT/
 * LIGHTCHANGE to the client. Skips packets during world load.
 */
void
CPlayer_DropAtFeet_VT(CItem *self, CLocation *loc)
{
	CPlayer *player = (CPlayer *)self;
	uint8_t zmoveBuf[0x18];
	uint8_t sunlightBuf[4];
	uint8_t lightBuf[8];
	uint8_t zmoveBuf2[0x18];
	int blockIdx;
	int lightLevel;

	if (*(int16_t *)&self->resourceEntity.nextInContainer == -1)
		CLocation_SetLoc((CLocation *)&self->resourceEntity.nextInContainer, loc);

	CPlayer_UpdateLastValidLocation(player, loc);

	if (self->resourceEntity.entity.removedFromWorld) {
		CItem_InternalMove(self, loc, 0);
		ItemMap_Insert(self);
		self->resourceEntity.entity.removedFromWorld = 0;
		UpdateRegion(self);
	}

	if (!g_World->isLoading) {
		PacketManager_MakePacket_ZMOVE(zmoveBuf, (CMobile *)self);
		SendToClient(self, zmoveBuf, -1);

		CPlayer_SetMovePrevented(player, 1);

		blockIdx =
		        CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)(int16_t)self->resourceEntity.entity.location.x, (int)(int16_t)self->resourceEntity.entity.location.y, 0);
		lightLevel = (int)(int16_t)g_SpatialGrid.cells[blockIdx].lightLevel;
		if (lightLevel == 0)
			lightLevel = g_globalLightLevel;

		player->lastLightLevel = (uint16_t)lightLevel;
		PacketManager_MakePacket_SUNLIGHT(sunlightBuf, (int)(int16_t)player->lastLightLevel);
		SendToClient(self, sunlightBuf, -1);

		PacketManager_MakePacket_LIGHTCHANGE(lightBuf, self->serial, ((CMobile *)self)->lightVal);
		SendToClient(self, lightBuf, -1);

		CMobile_NotifyNearbyPlayers(self);
		PacketManager_MakePacket_ZMOVE(zmoveBuf2, (CMobile *)self);
		SendToClient(self, zmoveBuf2, -1);
	}

	if (IsNearCampfire(self))
		CItem_SetCampfireTimestamp(self);
	else
		CPlayer_ClearTargetSerial(player);

	CResourceEntity_NotifyPostModifyIfActive(self);
	CItem_ReleaseTracking(self);
}

/*
 * 0x0048B5D7 - CPlayer vtable[0x094] GetMovementType
 *
 * Returns 7 if pflags has both bits 0x02 and 0x10 set (hidden+invuln),
 * 6 if dead or counselor, otherwise delegates to CMobile_GetMovementType.
 * moveType 6 makes specific items passable (doors, certain body types,
 * containers) via CItem_GetSurfaceFlags_VT case 6 handling.
 */
uint8_t
CPlayer_GetMovementType_VT(CPlayer *self)
{
	if ((self->pflags & 0x12) == 0x12)
		return 7;
	if (VT_IsDead((CItem *)self))
		return 6;
	if (CPlayer_IsCounselor(self))
		return 6;
	return CMobile_GetMovementType(&self->mobile);
}

/*
 * 0x0048FAC5 - CPlayer::AddToAggressorList (vtable[0x190])
 *
 * Player override of vtable[0x190]. Tracks aggression by adding the
 * victim serial to the attacker's "aggressionVictimList" tag, refreshes
 * aggression timers on the attacker, and handles controller entity
 * criminal flagging. CMobile's base implementation returns 0.
 */
void
CPlayer_AddToAggressorList_VT(CItem *this, CItem *attacker, CItem *controller, uint32_t controllerSerial)
{
	CList *victimList;
	CList *controllerList;
	int defensive;
	CList localList;
	CString localStr;

	if (this->serial == controllerSerial)
		return;
	victimList = CResourceEntity_GetTagEntity(this, "aggressionVictimList");
	if (victimList != NULL) {
		if (CList_Find(victimList, 4, attacker->serial)) {
			((void (*)(CItem *))VT_FN(this, VT_REFRESH_AGGRESSION))(this);
			return;
		}
	}

	if (CResourceEntity_AddToTagList(attacker, "aggressionVictimList", 4, this->serial)) {
		if (VT_IsPlayer(this)) {
			((void (*)(CItem *, CItem *, int))VT_FN(attacker, VT_SEND_ENTITY_UPDATE))(attacker, this, 0);
		}
	}

	((void (*)(CItem *))VT_FN(attacker, VT_REFRESH_AGGRESSION))(attacker);

	if (controllerSerial == 0)
		return;

	defensive = 0;
	CResourceEntity_GetTagInt(attacker, "defensive", &defensive);
	if (defensive != 0)
		return;

	if (controllerSerial == attacker->serial)
		return;

	// Redundant check - attacker already verified above
	controllerList = CResourceEntity_GetTagEntity(this, "aggressionVictimList");
	if (controllerList != NULL) {
		if (CList_Find(controllerList, 4, attacker->serial))
			return;
	}

	if (controller != NULL) {
		CResourceEntity_AddToTagList(controller, "aggressionVictimList", 4, this->serial);
		((void (*)(CItem *))VT_FN(controller, VT_REFRESH_AGGRESSION))(controller);
	} else {
		CList_Constructor(&localList);
		CString_Constructor(&localStr, "refreshAggression");
		SendMultiMessage(controllerSerial, this->serial, &localStr, (intptr_t)&localList);
		CString_Destructor(&localStr);
		CList_Destructor(&localList);
	}
}

/*
 * 0x0048FF6F - CItem::SetMurderCount
 *
 * Stores murderCount (clamped to >= 0) as an ObjVar tag and fires
 * the MurderCountChanged event.
 */
void
CItem_SetMurderCount(CItem *entity, int newCount)
{
	if (newCount < 0)
		newCount = 0;
	CEntity_SetObjVar(entity, "murderCount", 0, (uint32_t)newCount);
	Entity_ExecuteEvent(&entity->resourceEntity.entity, MurderCountChanged);
}

/*
 * 0x0048FFAA - CItem::GetMurderCount
 *
 * Returns the entity's "murderCount" ObjVar tag, or 0 if unset.
 */
static int
CItem_GetMurderCount(CItem *entity)
{
	int count = 0;

	CResourceEntity_GetTagInt(entity, "murderCount", &count);
	return count;
}

/*
 * Custom - CPlayer_IsTestCenter
 *
 * Returns 1 if PlayerIsTestCenter flag (pflags bit 0x40000) is set.
 * Granted at login when the server runs with -test.
 */
int
CPlayer_IsTestCenter(CPlayer *this)
{
	return (this->pflags & PlayerIsTestCenter) != 0;
}

/*
 * Custom - CPlayer_AddTestCenterKit
 *
 * Drops the Test Center starter kit on the new character: 10000 gold in
 * the bank box, plus a spellbook filled with all 64 spells and a bag
 * containing 100 of each of the 8 standard reagents in the backpack.
 * Called from NewPlayer right after the backpack is created and
 * InitStartingEquipment runs, only when g_DebugTest is set.
 */
void
CPlayer_AddTestCenterKit(CPlayer *this, CItem *backpack)
{
	static const uint16_t reagents[] = { 0x0F7A, 0x0F7B, 0x0F84, 0x0F85, 0x0F86, 0x0F88, 0x0F8C, 0x0F8D };
	CItem *bank;
	uint32_t goldSer, bookSer, bagSer;
	int g, i;

	// 10000 gold pile into the bank box (FixBank creates it if needed)
	FixBank(&this->mobile);
	bank = this->mobile.equipment[29];
	if (bank != NULL) {
		goldSer = Script_createGlobalObjectIn(0x0EED, bank->serial);
		if (goldSer != 0)
			Script_addGlobalQuantity(goldSer, 9999);
	}

	// Filled spellbook (graphic 0x0EFA, spells 0x1F2D..0x1F6C - 64 spells)
	bookSer = Script_createGlobalObjectIn(0x0EFA, backpack->serial);
	if (bookSer != 0) {
		for (g = 0x1F2D; g <= 0x1F6C; g++)
			Script_createGlobalObjectIn(g, bookSer);
	}

	// Reagent bag (100 of each)
	bagSer = Script_createGlobalObjectIn(0x0E76, backpack->serial);
	if (bagSer != 0) {
		for (i = 0; i < (int)(sizeof(reagents) / sizeof(reagents[0])); i++) {
			uint32_t ser = Script_createGlobalObjectIn(reagents[i], bagSer);
			if (ser != 0)
				Script_addGlobalQuantity(ser, 99);
		}
	}
}
