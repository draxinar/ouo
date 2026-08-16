/*
 * Wombat scripting engine - runtime execution.
 *
 * Walks compiled script trees: evaluates expressions, dispatches
 * handlers and operators, runs the Script_* built-ins, and fires the
 * per-trigger entry point from the game loop.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "anim.h"
#include "bboard.h"
#include "book.h"
#include "combat.h"
#include "container.h"
#include "convo.h"
#include "dynamic.h"
#include "egg.h"
#include "feature.h"
#include "filemanager.h"
#include "gmedit.h"
#include "help_queue.h"
#include "io.h"
#include "main.h"
#include "multi.h"
#include "npc.h"
#include "objvar.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "shopkeeper.h"
#include "signpost.h"
#include "skill.h"
#include "taglist.h"
#include "template.h"
#include "time.h"
#include "timer.h"
#include "utils.h"
#include "vtable.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "wombat_escript.h"
#include "world.h"

static void classifyCreature(uint16_t bodyType, int *outType, int *outClass, int *outDiff); // 0x00405130
static void closeTracking(CItem *player, CList *trackList); // 0x00405EBD
static void CExecThread_StoreResult(CExecThread *thread, int index, void *data, int size); // 0x0040A608
static CItem *CScriptInstance_GetEntity(ScriptAttachNode *ref); // 0x0040CC1D
static void ResultNode_FreeTree(ResultNode *node); // 0x0040CD9C
static CItem *FindEntityValidated(uint32_t serial, const char *caller); // 0x0040DB67
static CItem *FindWeaponValidated(uint32_t serial, const char *caller); // 0x0040DBDA
static CItem *FindMobileValidated(uint32_t serial, const char *caller); // 0x0040DC5F
static CItem *FindMobileEntityValidated(uint32_t serial, const char *caller); // 0x0040DCE4
static CItem *FindContainerValidated(uint32_t serial, const char *caller); // 0x0040DD69
static CItem *FindPlayerValidated(uint32_t serial, const char *caller); // 0x0040DDEE
static CItem *FindCorpseValidated(uint32_t serial, const char *caller); // 0x0040DE70
static CItem *FindMapItem(uint32_t serial, const char *caller); // 0x0040DEF5
static CItem *FindBookValidated(uint32_t serial, const char *caller); // 0x0040DF7A
static int sortList_cmpInt(CListNode *a, CListNode *b); // 0x0040E0E0
static int sortList_cmpStr(CListNode *a, CListNode *b); // 0x0040E115
static int sortList_cmpObj(CListNode *a, CListNode *b); // 0x0040E150
static CListNode *List_GetAt_Typed(CList *list, int index, int expectedType); // 0x0040E609
static int CArray_Init(WombatArray *arr, int width, int height, CList *typeList); // 0x0040EAAE
static void CArray_Free(WombatArray *arr); // 0x0040EBE8
static uintptr_t CArray_GetIntElem(WombatArray *arr, int x, int y); // 0x0040ED38
static void *CArray_GetStrElem(WombatArray *arr, int x, int y); // 0x0040ED6B
static void *CArray_GetUStrElem(WombatArray *arr, int x, int y); // 0x0040ED9E
static void CArray_SetIntElem(WombatArray *arr, int x, int y, uintptr_t val); // 0x0040EDD1
static void CArray_SetStrElem(WombatArray *arr, int x, int y, void *src); // 0x0040EE05
static void CArray_SetUStrElem(WombatArray *arr, int x, int y, void *src); // 0x0040EEA9
static uintptr_t *CArray_ElemLookup(WombatArray *arr, int x, int y, uintptr_t expectedType); // 0x0040EF4D
static WombatArray *CArray_Constructor(WombatArray *arr); // 0x0040EA83
static void WombatArrays_AtexitCallback(void); // 0x0040EFE7
static WombatArray *ArrayCreate(int id); // 0x0040F013
static WombatArray *ArrayLookup(int id); // 0x0040F0D8
static void ArrayDelete(int id); // 0x0040F12E
static int GetDistanceInTiles_Internal(const CLocation *loc1, const CLocation *loc2); // 0x0040FA3F
static void selectTypeImpl(uint32_t playerSerial, uint32_t serial, uint32_t dialogId, CString *title, CList *list, uint32_t withHue); // 0x00410831
static int DispatchMessage(uint32_t callerSerial, uint32_t targetSerial, CString *msgName, intptr_t listArgs); // 0x00410AD2
static void printList_recursive(CList *list, int depth); // 0x0041103A
static int Script_checkEntity(uint32_t serial, int (*checker)(CItem *), const char *name); // 0x00411319
static int Script_checkMobile(uint32_t serial, int (*checker)(CItem *), const char *name); // 0x00411389
static int Script_checkPlayer(uint32_t serial, int (*checker)(CItem *), const char *name); // 0x0041148D
static int check_GetWeight(CItem *ent); // 0x0041384E
static int check_IsVirtueGuard(CItem *ent); // 0x004724AE
static int check_IsUsingVirtueShield(CItem *ent); // 0x00472476
static int check_IsOrderGuard(CItem *ent); // 0x004722C7
static int check_IsChaosGuard(CItem *ent); // 0x0047235A
static int check_GetCanCarry(CItem *ent); // 0x00413F5D
static int check_GetSkillTotal(CItem *ent); // 0x00413F78
static int hasObj_search(CItem *container, uint32_t serial); // 0x00419989
static int hasObjType_search(CItem *container, int type); // 0x004199EE
static CItem *containsObj_search(CItem *item, CItem *target); // 0x00419C51
static CItem *containsObjType_search(CItem *item, int type); // 0x00419CC6
static void GetObjectsOfType_Recursive(CList *list, CItem *container, int typeId); // 0x0041A0DF
static int CArray_IsValid(WombatArray *arr); // 0x00420D50
static void CheckGoldLimit(CMobile *mob); // 0x0042F8FA
static int BM_CaseInsensitiveSearch(const char *pattern, const char *text); // 0x00432EA0
static void BM_BuildBadCharTable(const char *pattern); // 0x00432FAC
static void BM_BuildGoodSuffixTable(const char *pattern); // 0x0043303A
static int BM_Max(int a, int b); // 0x004331A0
static CHintItem *CHintItem_Constructor(CHintItem *self); // 0x00463A80
static void CHintItem_SetName1(CHintItem *self, const char *name); // 0x00463C8C
static void CHintItem_SetName2(CHintItem *self, const char *name); // 0x00463CC8
static void CHintItem_SetName3(CHintItem *self, const char *name); // 0x00463D0A
static int CHintItem_Deserialize(CHintItem *self, const char *buf, int size); // 0x00463D4E
static CHintItem *CHintItem_Serialize(CHintItem *self); // 0x00463EB5
static int CHintItem_GetSerializedSize(CHintItem *self); // 0x004640A4
static void *CHintManager_Constructor(CResManager *self); // 0x004640C0
static void CHintManager_Destructor(CResManager *self); // 0x004640D8
static int CHintManager_Find(CResManager *manager, uint32_t serial, int flags, CHintItem *outHint); // 0x004640EB
static void CHintManager_Add(CResManager *manager, CHintItem *item); // 0x00464201
static CSearchCtx *CHintManager_Next(CResManager *manager, CSearchCtx *output, CSearchCtx *ctx); // 0x00464650
static CSearchCtx *CHintManager_Begin(CResManager *manager, CSearchCtx *output); // 0x00464630
static CSearchCtx *CHintManager_Erase(CResManager *manager, CSearchCtx *output, CSearchCtx *ctx, int flag); // 0x004645B0
static int CHintManager_CreateBucketPair(CResManager *rm, void *keyPtr, void *valuePtr); // 0x004643A0
static void StaticInit_CommandManager(void); // 0x00467815
static void DispatchMultiBySerial(uint32_t callerSerial, uint8_t *buf, int totalSize); // 0x0047D4C0
static void DispatchMultiByLoc(uint32_t callerSerial, uint8_t *buf, int totalSize); // 0x0047D75E
static void DispatchMultiByRange(uint32_t callerSerial, uint8_t *buf, int totalSize); // 0x0047D98E
static void SendMultiMessageToLoc(CLocation *loc, uint32_t callerSerial, CString *msgName, intptr_t listArgs); // 0x0047DC1E
static void SendMultiMessageToRange(CLocation *loc, int range, uint32_t callerSerial, CString *msgName, intptr_t listArgs); // 0x0047DD93
static int CItem_GetSortKey(CItem *ent); // 0x00490C6D
static int CheckWordList(const char *text, const char **wordList); // 0x00491400
static int CheckProfanity(const char *text); // 0x00491445
static int IsNameSeparator(char c); // 0x0049149F
static int IsInvalidNameChar(char c); // 0x004914E6
static int VT_GetFlags(CItem *ent);
static int VT_GetEffectiveHeight(CItem *ent);
static CLocation *VT_GetLocation(CItem *ent);
static int check_IsGuard(CItem *ent);
static int check_IsSpellbook(CItem *ent);
static int check_IsRealContainer(CItem *ent);
static int check_IsInContainer(CItem *ent);
static const char *VT_GetName(CItem *ent);
static int check_GetMurderCount(CItem *ent);
static CItem *VT_FindItemByName(CItem *mob, CString *name, uint32_t containerSerial);
static int check_IsPlayer(CItem *ent);
static int check_IsContainer(CItem *ent);
static int check_IsNPC(CItem *ent);
static int check_IsShopkeeper(CItem *ent);
static int check_IsDead(CItem *ent);
static int check_IsMap(CItem *ent);
static int check_IsEquipped(CItem *ent);
static int check_IsWeapon(CItem *ent);

// SCommandManager global at 0x00699910.
SCommandManager g_SCommandManager; // 0x00699910
static int g_bNoSpatialUpdate; // 0x0063D85C

static int g_BMBadChar[256]; // 0x0063E300
static int g_BMGoodSuffix[100]; // 0x0063E170

/*
 * 0x00405130 - classifyCreature
 *
 * Classifies a creature body type for the tracking skill.
 * outType: 1=animal, 2=monster, 3=person.
 * outClass: cliloc class ID.
 * outDiff: difficulty rating (0-3, default 4=untrackable).
 * Uses two jump tables: bodyType 1-151 and bodyType 201-401.
 *
 * FIXED: binary's default case wrote *outClass=0 and *outDiff=4 but left
 * *outType uninitialized, causing the tracking filter to compare against
 * stack residue. The default now clears *outType=0 so unknown body types
 * never match a valid class.
 */
static void
classifyCreature(uint16_t bodyType, int *outType, int *outClass, int *outDiff)
{
	switch (bodyType) {
	case 1:
		*outClass = 0x20df;
		*outDiff = 2;
		*outType = 2;
		break;
	case 2:
		*outClass = 0x20d8;
		*outDiff = 2;
		*outType = 2;
		break;
	case 3:
		*outClass = 0x20ec;
		*outDiff = 2;
		*outType = 2;
		break;
	case 4:
		*outClass = 0x20d9;
		*outDiff = 2;
		*outType = 2;
		break;
	case 5:
		*outClass = 0x211d;
		*outDiff = 3;
		*outType = 1;
		break;
	case 6:
		*outClass = 0x211a;
		*outDiff = 3;
		*outType = 1;
		break;
	case 7:
	case 17:
	case 41:
		*outClass = 0x20e0;
		*outDiff = 2;
		*outType = 2;
		break;
	case 8:
		*outClass = 0x20d2;
		*outDiff = 3;
		*outType = 2;
		break;
	case 9:
	case 10:
		*outClass = 0x20d3;
		*outDiff = 2;
		*outType = 2;
		break;
	case 12:
	case 59:
		*outClass = 0x20d6;
		*outDiff = 0;
		*outType = 2;
		break;
	case 13:
		*outClass = 0x20ed;
		*outDiff = 3;
		*outType = 2;
		break;
	case 14:
		*outClass = 0x20d7;
		*outDiff = 2;
		*outType = 2;
		break;
	case 15:
		*outClass = 0x20f3;
		*outDiff = 3;
		*outType = 2;
		break;
	case 16:
		*outClass = 0x210b;
		*outDiff = 2;
		*outType = 2;
		break;
	case 18:
		*outClass = 0x20d8;
		*outDiff = 1;
		*outType = 2;
		break;
	case 21:
		*outClass = 0x20fc;
		*outDiff = 2;
		*outType = 1;
		break;
	case 22:
		*outClass = 0x20f4;
		*outDiff = 3;
		*outType = 2;
		break;
	case 24:
		*outClass = 0x20f8;
		*outDiff = 2;
		*outType = 2;
		break;
	case 26:
		*outClass = 0x2109;
		*outDiff = 3;
		*outType = 2;
		break;
	case 28:
		*outClass = 0x20fd;
		*outDiff = 2;
		*outType = 1;
		break;
	case 29:
		*outClass = 0x20f5;
		*outDiff = 2;
		*outType = 1;
		break;
	case 30:
		*outClass = 0x20dc;
		*outDiff = 2;
		*outType = 2;
		break;
	case 31:
		*outClass = 0x210a;
		*outDiff = 2;
		*outType = 2;
		break;
	case 33:
	case 34:
	case 35:
	case 36:
	case 37:
	case 38:
		*outClass = 0x20de;
		*outDiff = 2;
		*outType = 2;
		break;
	case 39:
		*outClass = 0x20f9;
		*outDiff = 2;
		*outType = 2;
		break;
	case 42:
	case 44:
	case 45:
		*outClass = 0x20e3;
		*outDiff = 2;
		*outType = 2;
		break;
	case 47:
		*outClass = 0x20fa;
		*outDiff = 2;
		*outType = 2;
		break;
	case 48:
		*outClass = 0x20e4;
		*outDiff = 1;
		*outType = 1;
		break;
	case 49:
		*outClass = 0x20e5;
		*outDiff = 1;
		*outType = 2;
		break;
	case 50:
	case 56:
	case 57:
		*outClass = 0x20e7;
		*outDiff = 2;
		*outType = 2;
		break;
	case 51:
		*outClass = 0x20e8;
		*outDiff = 3;
		*outType = 2;
		break;
	case 52:
		*outClass = 0x20fe;
		*outDiff = 3;
		*outType = 1;
		break;
	case 53:
	case 54:
	case 55:
		*outClass = 0x20e9;
		*outDiff = 2;
		*outType = 2;
		break;
	case 58:
		*outClass = 0x2100;
		*outDiff = 3;
		*outType = 2;
		break;
	case 60:
	case 61:
		*outClass = 0x20d6;
		*outDiff = 1;
		*outType = 2;
		break;
	case 151:
		*outClass = 0x20f1;
		*outDiff = 2;
		*outType = 1;
		break;
	case 200:
		*outClass = 0x2120;
		*outDiff = 1;
		*outType = 1;
		break;
	case 201:
		*outClass = 0x211b;
		*outDiff = 3;
		*outType = 1;
		break;
	case 202:
		*outClass = 0x20da;
		*outDiff = 1;
		*outType = 1;
		break;
	case 203:
	case 290:
		*outClass = 0x2101;
		*outDiff = 2;
		*outType = 1;
		break;
	case 204:
	case 228:
		*outClass = 0x2121;
		*outDiff = 1;
		*outType = 1;
		break;
	case 205:
		*outClass = 0x20e2;
		*outDiff = 3;
		*outType = 1;
		break;
	case 207:
		*outClass = 0x20eb;
		*outDiff = 2;
		*outType = 1;
		break;
	case 208:
		*outClass = 0x20d1;
		*outDiff = 3;
		*outType = 1;
		break;
	case 209:
		*outClass = 0x2108;
		*outDiff = 2;
		*outType = 1;
		break;
	case 211:
		*outClass = 0x20cf;
		*outDiff = 1;
		*outType = 1;
		break;
	case 212:
		*outClass = 0x211e;
		*outDiff = 1;
		*outType = 1;
		break;
	case 213:
		*outClass = 0x20e1;
		*outDiff = 1;
		*outType = 1;
		break;
	case 214:
		*outClass = 0x2102;
		*outDiff = 2;
		*outType = 1;
		break;
	case 215:
	case 238:
		*outClass = 0x20d0;
		*outDiff = 3;
		*outType = 1;
		break;
	case 216:
	case 231:
	case 232:
	case 233:
		*outClass = 0x2103;
		*outDiff = 1;
		*outType = 1;
		break;
	case 217:
	case 262:
		*outClass = 0x211c;
		*outDiff = 2;
		*outType = 1;
		break;
	case 220:
		*outClass = 0x20f6;
		*outDiff = 2;
		*outType = 1;
		break;
	case 221:
		*outClass = 0x20ff;
		*outDiff = 2;
		*outType = 1;
		break;
	case 223:
		*outClass = 0x20e6;
		*outDiff = 2;
		*outType = 1;
		break;
	case 225:
		*outClass = 0x2122;
		*outDiff = 2;
		*outType = 1;
		break;
	case 226:
		*outClass = 0x211f;
		*outDiff = 1;
		*outType = 1;
		break;
	case 234:
	case 237:
		*outClass = 0x20d4;
		*outDiff = 2;
		*outType = 1;
		break;
	case 400:
		*outClass = 0x2106;
		*outDiff = 2;
		*outType = 3;
		break;
	case 401:
		*outClass = 0x2107;
		*outDiff = 2;
		*outType = 3;
		break;
	default:
		*outClass = 0;
		*outDiff = 4;
		*outType = 0;
		break;
	}
}

/*
 * 0x00405EBD - closeTracking
 *
 * Closes tracking state for the player. Called when tracking type selection
 * is cancelled, invalid, or the result is empty/crowded. Clears the CList,
 * and if the player has the "useristracking" script class attached,
 * schedules event type 5 (extra1=0x51) after 1 tick.
 */
static void
closeTracking(CItem *player, CList *trackList)
{
	CScript *scriptClass;

	CList_Clear((CList *)trackList);
	if (player == NULL)
		return;
	scriptClass = CScriptManager_FindOrLoad(&g_ScriptManager, "useristracking");
	if (CResourceEntity_HasScriptClass(player, scriptClass))
		ScheduleEvent(1, player->serial, 5, 0x51, 0);
}

// CEntityMap_RangeQuery now implemented in blockmanager.c

/*
 * 0x00405F0C - trackingTypeSelected [732]
 *
 * Handles tracking type selection UI callback. Called when the player
 * selects a creature type (animal/monster/person) in the tracking
 * type picker dialog. Queries both NPC and item spatial maps within
 * a skill-scaled range of the player's location, filters by the selected
 * type and the player's tracking skill difficulty, excludes owned pets
 * (myBoss list), then sends a creature picker dialog or a message.
 *
 * filter: CLocation* - pointer to the player's location, passed as
 * WTYPE_LOC (6 bytes on the scope stack). The wombat dispatcher stores
 * the location in a slot and passes &slot.value.
 *
 * trackType: 0 = cancel (close tracking dialog).
 * categoryTypeId: OBJPICKER response typeId encoding the creature
 *        category the player picked (0x2122=animal, 0x20D8=monster,
 *        0x2106=person).
 *
 * MODIFIED (FEAT_SKILL_TRACKING): the binary's 200-tile radius and
 * tier cutoffs at raw skill 20/40 (display 2.0/4.0) are demo values
 * tuned for the tutwisp dragon-tracking step. With the feature on,
 * range becomes a flat 20 tiles and the cutoffs become 200/400
 * (display 20.0/40.0). These are plausible production-style values
 * but arbitrary: no authoritative Origin source documents what the
 * non-demo numbers should be.
 */
void
Script_trackingTypeSelected(CList *list, uint32_t serial, int trackType, int categoryTypeId, CLocation *filter)
{
	CItem *player;
	CList resultList;
	CVector entityList;
	char typeFlag;
	uintptr_t *iter;
	CItem *entity;
	int typeClass;
	const char *typeName;
	const char *typePlural;
	int skillValue;
	int difficulty;
	int range;
	int tier2Cutoff, tier3Cutoff;
	int outCreatureType, outDifficulty, outCreatureClass;
	int dir;
	CString *nameStr;
	CList *bossList;
	CListNode *bossNode;
	char buf[512];
	CString titleStr;

	// Find player, validate, handle cancel.
	player = FindPlayerValidated(serial, "trackingTypeSelected");
	if (trackType == 0 || player == NULL) {
		closeTracking(player, list);
		return;
	}

	// Init result list.
	CList_Constructor(&resultList);

	// Determine creature type class from OBJPICKER category typeId.
	switch ((uint32_t)categoryTypeId) {
	case 0x2122:
		typeClass = 1;
		typeName = "animal";
		typePlural = "animals";
		break;
	case 0x20D8:
		typeClass = 2;
		typeName = "monster";
		typePlural = "monsters";
		break;
	case 0x2106:
		typeClass = 3;
		typeName = "person";
		typePlural = "people";
		break;
	default:
		closeTracking(player, list);
		CList_Destructor(&resultList);
		return;
	}

	// Init entity list vector.
	typeFlag = 0;
	CVector_Constructor(&entityList, &typeFlag);

	// Get tracking skill value (skill 0x26 = 38 = Tracking).
	skillValue = CMobile_GetTotalSkill((CMobile *)player, 0x26);

	// Pick range and tier cutoffs (binary: range=200, tiers at 20/40).
	if (feat(FEAT_SKILL_TRACKING)) {
		range = 20;
		tier2Cutoff = 200;
		tier3Cutoff = 400;
	} else {
		range = 200;
		tier2Cutoff = 20;
		tier3Cutoff = 40;
	}

	// Range query on NPC and item maps.
	CEntityMap_RangeQuery(g_NPCMap, &entityList, (int16_t)filter->x, (int16_t)filter->y, range);
	CEntityMap_RangeQuery(g_ItemMap, &entityList, (int16_t)filter->x, (int16_t)filter->y, range);

	// Compute difficulty threshold from skill level.
	difficulty = 1;
	if (skillValue > tier3Cutoff)
		difficulty = 3;
	else if (skillValue > tier2Cutoff)
		difficulty = 2;

	// Iterate entity list, filter, add to output and result lists.
	iter = (uintptr_t *)entityList.begin;
	while (iter != (uintptr_t *)entityList.end) {
		entity = (CItem *)*iter;
		iter++;

		// Skip the player themselves.
		if (entity == player)
			continue;

		// Skip players in editing mode (vtable[0x18] = IsPlayer).
		if (VT_IsPlayer(entity) && CPlayer_IsEditing((CPlayer *)entity))
			continue;

		// Classify creature by body type via CEntity_GetBodyType.
		classifyCreature(CEntity_GetBodyType(entity) & 0xFFFF, &outCreatureType, &outCreatureClass, &outDifficulty);

		// Skip if wrong creature type class.
		if (outCreatureType != typeClass)
			continue;

		// Skip if creature class exceeds player's difficulty rating.
		if (outCreatureClass > difficulty)
			continue;

		// Skip owned pets: manual CList iteration on myBoss tag.
		bossList = CResourceEntity_GetTagEntity(entity, "myBoss");
		if (bossList != NULL) {
			bossNode = bossList->head;
			while (bossNode != bossList->tail) {
				if (bossNode->value == player->serial)
					goto next_entity;
				bossNode = bossNode->next;
			}
		}

		// Append entity serial (type 4=OBJ) to output list.
		CList_Append(list, 4, entity->serial);

		// Append creature class (type 0=INT) to result list.
		CList_Append(&resultList, 0, outCreatureClass);

		// Append empty string (type 1=STRING, value 0) to result list.
		// CListNode_Constructor with type 1 and value 0 allocates a CString("").
		CList_Append(&resultList, 1, 0);

		// Get the CString* from the tail node's value.
		nameStr = (CString *)resultList.tail->value;

		// Assign entity name via vtable[0x4C] dispatch (arg=1).
		CString_AssignCStr(nameStr, VT_GetName(entity));

		// Compute direction from player to entity (location at +0x0A).
		dir = CalcDirection(&player->resourceEntity.entity.location, &entity->resourceEntity.entity.location);

		// Append direction string to the name CString.
		switch (dir) {
		case 0:
			CString_AppendCStr(nameStr, " to the North.");
			break;
		case 1:
			CString_AppendCStr(nameStr, " to the Northeast.");
			break;
		case 2:
			CString_AppendCStr(nameStr, " to the East.");
			break;
		case 3:
			CString_AppendCStr(nameStr, " to the Southeast.");
			break;
		case 4:
			CString_AppendCStr(nameStr, " to the South.");
			break;
		case 5:
			CString_AppendCStr(nameStr, " to the Southwest.");
			break;
		case 6:
			CString_AppendCStr(nameStr, " to the West.");
			break;
		case 7:
			CString_AppendCStr(nameStr, " to the Northwest.");
			break;
		default:
			CString_AppendCStr(nameStr, " in some direction.");
			break;
		}
next_entity:
		continue;
	}

	// Check count on output list and send dialog or message.
	if (list->count > 40) {
		sprintf(buf, "This area is too crowded to track any individual %s.", typeName);
		CPlayer_SystemMessage((CPlayer *)player, buf);
		closeTracking(player, list);
		CVector_Destructor(&entityList);
		CList_Destructor(&resultList);
		return;
	}

	if (list->count < 1) {
		sprintf(buf, "You see no evidence of %s in the area.", typePlural);
		CPlayer_SystemMessage((CPlayer *)player, buf);
		closeTracking(player, list);
		CVector_Destructor(&entityList);
		CList_Destructor(&resultList);
		return;
	}

	// Build and send creature picker dialog.
	sprintf(buf, "Which %s do you wish to track?", typeName);
	CString_Constructor(&titleStr, buf);
	selectTypeImpl(serial, serial, 0x29, &titleStr, &resultList, 0);
	CString_Destructor(&titleStr);
	CVector_Destructor(&entityList);
	CList_Destructor(&resultList);
}

/*
 * 0x00409DD2 - ResolveResultType
 *
 * Returns the WTYPE_* code for a ResultNode based on its type tag:
 * handler results query GetVarType, function calls use the script's
 * funcList return type, var refs read the variable's typeId, and the
 * literal cases return WTYPE_INT/STRING/USTRING. Sets *outFlag=1 for
 * lvalue refs (cases 3 and 5).
 */
int
ResolveResultType(CScript *script, ResultNode *node, int *outFlag)
{
	*outFlag = 0;

	switch (node->type) {
	case 0: /* handler result - calls GetVarType (thiscall on BuiltinHandlerEntry*) */
		return GetVarType((const BuiltinHandlerEntry *)(uintptr_t)node->value);
	case 1: { /* function ref - binary reads return type from script function table.
		   * node->value is a function index; script+8 is the function list.
		   * Binary: script->funcList.array + node->value*16, calls GetFuncRetType. */
		CFunction *func = (CFunction *)((char *)script->funcList.array + (int)node->value * (int)sizeof(CFunction));
		return GetFuncRetType(func);
	}
	case 2:   /* trigger var ref - reads typeId, flag stays 0 */
	case 4: { /* local var ref - reads typeId, flag stays 0 */
		CNamedScopeEntry *var = (CNamedScopeEntry *)(uintptr_t)node->value;
		return var->typeId;
	}
	case 3:   /* local var lvalue ref - sets flag=1, reads typeId */
	case 5: { /* trigger var lvalue ref - sets flag=1, reads typeId */
		CNamedScopeEntry *var = (CNamedScopeEntry *)(uintptr_t)node->value;
		*outFlag = 1;
		return var->typeId;
	}
	case 6: /* integer literal */
		return WTYPE_INT;
	case 7: /* string literal */
		return WTYPE_STRING;
	case 8: /* ustring literal */
		return WTYPE_USTRING;
	default:
		return WTYPE_UNKNOWN;
	}
}

/*
 * 0x00409E6F - CalcResultChainSize (TreeEvaluator_GetNodeTypeSize)
 *
 * Sums the runtime scope size needed for every node in a ResultNode
 * chain. Each node's contribution depends on its type: handler/func
 * results consult g_WombatTypeSizes via the resolved typeId, var
 * rvalues use the variable's typeId, lvalues and literals pay one
 * pointer-sized slot, and goto labels cost nothing. Each entry is
 * rounded up to 4 bytes before being added to the total.
 */
int
CalcResultChainSize(CScript *script, ResultNode *chain)
{
	int size = 0;
	int total = 0;

	while (chain != NULL) {
		switch ((unsigned)chain->type) {
		case 0: { /* handler result - GetVarType → typeId → size table */
			int typeId = GetVarType((const BuiltinHandlerEntry *)(uintptr_t)chain->value);
			size = g_WombatTypeSizes[typeId];
			break;
		}
		case 1: { /* func call ref - GetFuncRetType → typeId → size table */
			CFunction *func = (CFunction *)((char *)script->funcList.array + (int)chain->value * (int)sizeof(CFunction));
			int typeId = GetFuncRetType(func);
			size = g_WombatTypeSizes[typeId];
			break;
		}
		case 2: /* trig var rvalue - reads typeId → size table */
		case 4: { /* local var rvalue - reads typeId → size table */
			CNamedScopeEntry *var = (CNamedScopeEntry *)(uintptr_t)chain->value;
			int typeId = var->typeId;
			size = g_WombatTypeSizes[typeId];
			break;
		}
		case 3: /* local lvalue */
		case 5: { /* trig lvalue */
			size = 4;
			break;
		}
		case 6: /* int literal */
			size = sizeof(void *);
			break;
		case 7: /* string literal */
		case 8: /* ustr/member */
			size = sizeof(void *);
			break;
		case 10: /* semi */
			size = sizeof(void *);
			break;
		case 9: /* goto label - no runtime data */
			size = 0;
			break;
		default: /* >10: size retains previous value (binary behavior) */
			break;
		}
		// Align to 4 bytes: (size + 3) & 0xFC
		total += (size + 3) & ~3;
		chain = chain->next;
	}
	return total;
}

/*
 * 0x00409F5C - TreeEvaluator
 *
 * Single-step evaluator for compiled ResultNode chains, called in a
 * loop by ExecuteTrigger. Processes one node per call: dispatches
 * handlers, makes function calls, pushes variable rvalues/lvalues
 * and literals onto the scope, or advances past goto labels. When
 * stream is NULL it pops a saved stream off the scope to resume
 * argument evaluation, or finalises the return value when the
 * thread is finished. Returns 1 to keep going, 0 when done.
 */
int
TreeEvaluator(CExecThread *thread)
{
	int wasHandler;
	int calcSize;
	CNamedScopeEntry *varDef;
	intptr_t addr;
	int savedUsedBytes;
	uintptr_t retval;
	uint32_t nodeType;

	// Validate script reference
	if (thread->scriptRef == NULL) {
		return 0;
	}
	if ((uintptr_t)((ScriptAttachNode *)thread->scriptRef)->memberScope == 0xABCD) {
		return 0;
	}

	wasHandler = 0;

	// If stream is NULL, we're returning from arg evaluation or done
	if (thread->stream == NULL) {
		wasHandler = 1;
		// Pop saved stream pointer from scope back into thread->stream
		{
			CScope_StoreValue(&thread->scope, thread, sizeof(void *));
		}

		if (thread->stream == NULL) {
			// Thread finished: extract return value from scope tail
			memcpy(&retval, thread->scope.data + thread->scope.usedBytes - sizeof(uintptr_t), sizeof(uintptr_t));
			thread->returnVal = (int)retval;
			return 0;
		}
	}

	// Read node type from current stream position
	nodeType = ((ResultNode *)thread->stream)->type;
	if (nodeType > 10)
		goto advance;

	switch (nodeType) {
	case 0: { // handler reference
		BuiltinHandlerEntry *handlerEntry;
		handlerEntry = (BuiltinHandlerEntry *)(uintptr_t)((ResultNode *)thread->stream)->value;
		if (wasHandler) {
			// Returning from arg evaluation
			if (thread->finished != 0)
				return 1;
			// Pop calcSize from rstack
			savedUsedBytes = (int)CNodeList_Pop(&thread->rstack);
			// Push (usedBytes - calcSize) to hstack as frame base
			CNodeList_Push(&thread->hstack, (uint32_t)(thread->scope.usedBytes - savedUsedBytes));
			thread->defaultReturn = 1;
			DispatchHandler(thread, handlerEntry);
			// Discard frame base from hstack
			CNodeList_Pop(&thread->hstack);
			if (thread->defaultReturn == 0)
				return 1;
			goto advance;
		}
		// First time: calculate argument chain size
		calcSize = CalcResultChainSize(*(CScript **)thread->scriptRef, (ResultNode *)(uintptr_t)((ResultNode *)thread->stream)->extra);
		if (calcSize != 0) {
			// Has arguments: save stream, follow arg chain
			CScope_Append(&thread->scope, thread, sizeof(void *));
			CNodeList_Push(&thread->rstack, (uint32_t)calcSize);
			thread->stream = (char *)((ResultNode *)thread->stream)->extra;
			return 1;
		}
		// No arguments: execute handler directly
		thread->defaultReturn = 1;
		DispatchHandler(thread, handlerEntry);
		if (thread->defaultReturn == 0)
			return 1;
		goto advance;
	}

	case 1: // script function call
		if (wasHandler) {
			// Returning from arg evaluation: check expected size
			calcSize = (int)thread->rstack.arr[thread->rstack.count - 1];
			savedUsedBytes = (int)thread->rstack.arr[thread->rstack.count - 2];
			if (thread->scope.usedBytes - savedUsedBytes == calcSize) {
				// Size matches: set up function call
				HandleFuncReturn(thread, savedUsedBytes, calcSize);
				return 1;
			}
			// Mismatch: adjust and continue
			HandleReturnMismatch(thread);
			goto advance;
		}
		// First time: calculate arg chain size and push context
		calcSize = CalcResultChainSize(*(CScript **)thread->scriptRef, (ResultNode *)(uintptr_t)((ResultNode *)thread->stream)->extra);
		// Push scope.usedBytes to rstack (frame pointer)
		CNodeList_Push(&thread->rstack, (uint32_t)thread->scope.usedBytes);
		// Push calcSize to rstack
		CNodeList_Push(&thread->rstack, (uint32_t)calcSize);
		// Save stream pointer to scope (return address)
		CScope_Append(&thread->scope, thread, sizeof(void *));
		// Follow arg chain
		thread->stream = (char *)((ResultNode *)thread->stream)->extra;
		return 1;

	case 2: { // trigger var rvalue
		int typeSize;
		varDef = (CNamedScopeEntry *)(uintptr_t)((ResultNode *)thread->stream)->value;
		addr = (intptr_t)CNodeList_Peek(&thread->hstack) + varDef->offset;
		typeSize = g_WombatTypeSizes[varDef->typeId];
#ifdef DEBUG_TELEPORT
		if (varDef->typeId == 4 /* WTYPE_OBJ */) {
			uint32_t v = *(uint32_t *)(thread->scope.data + addr);
			fprintf(stderr, "VARREAD[2]: name=%s offset=%d hstack=%td addr=%td val=0x%X used=%d\n", varDef->name ? varDef->name : "?", varDef->offset,
			        (ptrdiff_t)CNodeList_Peek(&thread->hstack), addr, v, thread->scope.usedBytes);
		}
#endif
		CScope_PushValue(&thread->scope, thread->scope.data + addr, typeSize);
		goto advance;
	}

	case 3: { // local var lvalue
		int typeSize;
		varDef = (CNamedScopeEntry *)(uintptr_t)((ResultNode *)thread->stream)->value;
		addr = (intptr_t)CNodeList_Peek(&thread->hstack) + varDef->offset;
		typeSize = g_WombatTypeSizes[varDef->typeId];
#ifdef DEBUG_TELEPORT
		if (varDef->typeId == 4 /* WTYPE_OBJ */) {
			uint32_t v = *(uint32_t *)(thread->scope.data + addr);
			fprintf(stderr, "VARREAD[3]: name=%s offset=%d hstack=%td addr=%td val=0x%X used=%d\n", varDef->name ? varDef->name : "?", varDef->offset,
			        (ptrdiff_t)CNodeList_Peek(&thread->hstack), addr, v, thread->scope.usedBytes);
		}
#endif
		CScope_PushValue(&thread->scope, thread->scope.data + addr, typeSize);
		goto advance;
	}

	case 4: // local var rvalue
	case 5: { // trigger var lvalue (rvalue access)
		int typeSize;
		char *base;
		varDef = (CNamedScopeEntry *)(uintptr_t)((ResultNode *)thread->stream)->value;
		typeSize = g_WombatTypeSizes[varDef->typeId];
		base = (char *)((ScriptAttachNode *)thread->scriptRef)->memberScope;
#ifdef DEBUG_TELEPORT
		if (varDef->typeId == 4 /* WTYPE_OBJ */) {
			uint32_t v = *(uint32_t *)(base + varDef->offset);
			fprintf(stderr, "VARREAD[%d]: name=%s offset=%d base=%p val=0x%X used=%d\n", nodeType, varDef->name ? varDef->name : "?", varDef->offset, (void *)base, v,
			        thread->scope.usedBytes);
		}
#endif
		CScope_PushValue(&thread->scope, base + varDef->offset, typeSize);
		goto advance;
	}

	case 6: // int literal
	case 7: // string literal
	case 8: // ustring/member ref
	case 10: // semi result
		CScope_PushValue(&thread->scope, &((ResultNode *)thread->stream)->value, sizeof(void *));
		goto advance;

	case 9: // goto label - no runtime data
	default:
		break;
	}

advance:
	// Advance to next node in chain
	if (thread->stream != NULL)
		thread->stream = (char *)((ResultNode *)thread->stream)->next;
	return 1;
}

/*
 * 0x0040A2F1 - HandleFuncReturn
 *
 * Sets up a script-function call. Records the caller's scope size on
 * hstack, resolves the function scope from the script's funcList,
 * pushes default values for any parameters the caller did not
 * supply, saves the current stream onto the scope, and jumps to the
 * function's bytecode entry point.
 */
void
HandleFuncReturn(CExecThread *thread, int savedUsedBytes, int expectedSize)
{
	CScript *script;
	int funcIndex;
	CFuncScope *funcScope;

	CNodeList_Push(&thread->hstack, (uint32_t)savedUsedBytes);

	script = *(CScript **)thread->scriptRef;
	funcIndex = ((ResultNode *)thread->stream)->value;
	funcScope = (CFuncScope *)((CFunction *)script->funcList.array)[funcIndex].scope;

	if (funcScope == NULL) {
		ThreadList_FinishThread(&g_activeThreadList, thread, 0);
		return;
	}

	// Initialize defaults for unprovided parameters
	if (expectedSize < funcScope->namedScope.totalSize) {
		CNamedScopeEntry *entries;
		int i;

		entries = (CNamedScopeEntry *)funcScope->namedScope.entries;

		i = 0;
		while (entries[i].offset != expectedSize)
			i++;

		for (; i < funcScope->namedScope.count; i++) {
			int typeId = entries[i].typeId;
			switch (typeId) {
			case 1:
				CScope_PushDefaultString(&thread->scope);
				break;
			case 2:
				CScope_PushDefaultUString(&thread->scope);
				break;
			case 5:
				CScope_PushDefaultList(&thread->scope);
				break;
			default:
				CScope_Resize(&thread->scope, g_WombatTypeSizes[typeId]);
				break;
			}
		}
	}

	CScope_Append(&thread->scope, &thread->stream, sizeof(void *));
	thread->stream = (char *)funcScope->bodyStream;
}

/*
 * 0x0040A434 - HandleReturnMismatch
 *
 * Cleans up the scope after a function returns when the actual
 * return-value size differs from the caller's expectation. Slides
 * the return value over the saved return address, restores the
 * scope's usedBytes from the hstack frame, then pops one hstack
 * entry and rewinds rstack by two.
 */
void
HandleReturnMismatch(CExecThread *thread)
{
	CScript *script;
	int funcIndex;
	CFunction *funcEntry;
	CFuncScope *funcScope;
	int retType, typeSize, aligned;
	intptr_t peek;
	char *base;
	char saved[sizeof(void *)];

	script = *(CScript **)thread->scriptRef;

	// Get function entry from script's funcList
	funcIndex = ((ResultNode *)thread->stream)->value;
	funcEntry = &((CFunction *)script->funcList.array)[funcIndex];

	// Get return type and size
	retType = GetFuncRetType(funcEntry);
	typeSize = g_WombatTypeSizes[retType];

	// Get function scope
	funcScope = (CFuncScope *)funcEntry->scope;

	// Walk stream->extra past namedScope.count entries (linked via +8)
	/*
	 * This skips the parameter ResultNode chain to find where the
	 * actual body variables start. Binary does this to validate
	 * scope cleanup but we don't need the walked pointer. */
	{
		ResultNode *cur = (ResultNode *)(uintptr_t)((ResultNode *)thread->stream)->extra;
		int i;
		for (i = 0; i < funcScope->namedScope.count; i++) {
			if (cur != NULL)
				cur = cur->next;
		}
	}

	aligned = (typeSize + 3) & ~3;

	if (typeSize > 0) {
		// Peek hstack for saved scope position
		peek = (intptr_t)CNodeList_Peek(&thread->hstack);
		base = (char *)thread->scope.data + peek - sizeof(void *);

		// Save return address at base
		memcpy(saved, base, sizeof(void *));

		// Move return value data into place
		memmove(base, (char *)thread->scope.data + thread->scope.usedBytes - aligned, typeSize);

		// Restore saved return address after return value
		memcpy(base + aligned, saved, sizeof(void *));
	}

	// Restore scope usedBytes = hstack peek + aligned
	peek = (intptr_t)CNodeList_Peek(&thread->hstack);
	CScope_SetUsedBytes(&thread->scope, (int)(peek + aligned));

	// Pop hstack
	CNodeList_Pop(&thread->hstack);

	// Decrement rstack.count by 2 (binary: [this+0x48] -= 2)
	thread->rstack.count -= 2;
}

/*
 * 0x0040A565 - ExecAbortCurrent
 *
 * Terminates the running script thread with return value 0.
 * Called from the exec loop on an unrecoverable error.
 */
void
ExecAbortCurrent(void)
{
	ThreadList_FinishCurrent(&g_activeThreadList, 0);
}

/*
 * 0x0040A576 - CExecThread::StoreResultFirst
 *
 * StoreResult without the index walk: always stores into the first node
 * of the argument chain.
 */
static __attribute__((unused)) void
CExecThread_StoreResultFirst(CExecThread *thread, void *data, int size)
{
	ResultNode *node;
	int varType;
	char *base;
	CNamedScopeEntry *varDef;

	node = (ResultNode *)(uintptr_t)((ResultNode *)thread->stream)->extra;

	varType = (int)node->type - 2;
	if ((unsigned int)varType > 3)
		return;

	base = NULL;
	switch (varType) {
	case 0:
	case 1:
		// Types 2-3: base = scope.data + hstack frame base
		base = thread->scope.data + (int)thread->hstack.arr[thread->hstack.count - 2];
		break;
	case 2:
	case 3:
		// Types 4-5: base = scriptRef->data (at offset +8)
		base = (char *)((ScriptAttachNode *)thread->scriptRef)->memberScope;
		break;
	}

	varDef = (CNamedScopeEntry *)(uintptr_t)((ResultNode *)(uintptr_t)((ResultNode *)thread->stream)->extra)->value;
	memcpy(base + varDef->offset, data, size);
}

/*
 * 0x0040A608 - CExecThread::StoreResult
 *
 * Walks the argument chain (from stream->extra) to the index-th node,
 * determines the base address from the node type (2-3 = local scope
 * frame, 4-5 = script scope), then memcpys data to base + varDef offset.
 */
static void
CExecThread_StoreResult(CExecThread *thread, int index, void *data, int size)
{
	ResultNode *node;
	int varType;
	char *base;
	CNamedScopeEntry *varDef;

	node = (ResultNode *)(uintptr_t)((ResultNode *)thread->stream)->extra;
	while (index > 0) {
		node = node->next;
		index--;
	}

	varType = (int)node->type - 2;
	if ((unsigned int)varType > 3)
		return;

	switch (varType) {
	case 0:
	case 1:
		// Types 2-3: base = scope.data + hstack frame base
		base = thread->scope.data + (int)thread->hstack.arr[thread->hstack.count - 2];
		break;
	case 2:
	case 3:
		// Types 4-5: base = scriptRef->data (at offset +8)
		base = (char *)((ScriptAttachNode *)thread->scriptRef)->memberScope;
		break;
	}

	varDef = (CNamedScopeEntry *)(uintptr_t)node->value;
	memcpy(base + varDef->offset, data, size);
}

/*
 * 0x0040A6B5 - DispatchHandler
 *
 * Runtime dispatcher for builtin script handlers. Parses the handler's
 * type signature to extract parameters from the scope, invokes the
 * handler body via a 170-case switch on handler->opcode, then stores
 * any out-parameters and the return value back into the scope.
 */
void
DispatchHandler(CExecThread *thread, const BuiltinHandlerEntry *handler)
{
	// Slot array: 16-byte entries at ebp + N*16 - 0xD0
	// slot[0].value = retObj (var_d0h), slots 1-11 = parameters
	struct {
		uintptr_t value;  // +0x00
		uintptr_t field4; // +0x04
		uintptr_t addr;   // +0x08
		uintptr_t typeId; // +0x0C
	} slot[12];

#define retObj slot[0].value    // var_d0h = slot[0].value in binary
	// clang-format off
	int paramCount;         // var_dch
	const char *sigScan;    // var_e0h - forward scanner
	const char *sigPtr;     // var_d4h - backward extraction cursor
	int retType;            // var_d8h - from GetVarType
	uintptr_t tmp;          // var_10h - temp for scope operations
	int opcode;             // var_1f0h - handler->opcode
	int ch;                 // var_1e0h - tolower result
	int subType;            // var_1e4h - for 'u' complex type sub-switch
	ResultNode *scopeEntry; // var_ech - for 'u' type chain walk
	int loopIdx;            // var_e8h - for 'u' type loop counter
	int outFlag;            // var_f0h - ResolveResultType output flag
	// clang-format on

	// Lookup table for parameter type extraction (0x0040C8BA, 26 bytes)
	// Maps (tolower(ch) - 'c') to case number for the 10-case switch
	static const uint8_t paramTypeLookup[26] = { 0, 9, 9, 9, 9, 9, 1, 2, 9, 3, 9, 9, 4, 9, 5, 9, 6, 9, 7, 9, 9, 9, 9, 9, 9, 8 };

	// Lookup table for result storage (0x0040CBA8, 58 bytes)
	// Maps (uppercase ch - 'C') to case number for the 5-case switch
	static const uint8_t resultTypeLookup[58] = { 0, 4, 4, 4, 4, 4, 1, 4, 4, 4, 4, 4, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
		4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3 };

	retObj = 0;

	// Section 1: Signature scan - count non-'|' chars after first char
	sigScan = handler->typeSig + 1;
	paramCount = 0;
	while (*sigScan != '\0') {
		if (*sigScan != '|')
			paramCount++;
		sigScan++;
	}
	sigScan--;

	// paramCount = total non-'|' signature chars (excluding return type)
	// sigPtr starts at last char of signature, moves backward
	sigPtr = sigScan;

	// Section 2: Parameter extraction loop (backward through signature)
	while (paramCount > 0) {
		ch = tolower((unsigned char)*sigPtr);
		ch -= 'c'; // 0x63

		if (ch > 25) {
			// Default: decrement and continue
			paramCount--;
			sigPtr--;
			continue;
		}

		switch (paramTypeLookup[ch]) {
		case 1: // 'i' - int value
			CScope_StoreValue(&thread->scope, &tmp, sizeof(void *));
			slot[paramCount].value = tmp;
			break;
		case 4: // 'o' - object value
			CScope_StoreValue(&thread->scope, &tmp, sizeof(void *));
			slot[paramCount].value = tmp;
			break;
		case 6: // 's' - string value (pointer)
			CScope_StoreValue(&thread->scope, &tmp, sizeof(void *));
			slot[paramCount].value = tmp;
			break;
		case 5: // 'q' - unicode string value (pointer)
			CScope_StoreValue(&thread->scope, &tmp, sizeof(void *));
			slot[paramCount].value = tmp;
			break;
		case 3: // 'l' - list value (pointer)
			CScope_StoreValue(&thread->scope, &tmp, sizeof(void *));
			slot[paramCount].value = tmp;
			break;
		case 2: // 'j' - secondary int
			CScope_StoreValue(&thread->scope, &tmp, sizeof(void *));
			slot[paramCount].value = tmp;
			break;
		case 0: // 'c' - location (6 bytes)
			CScope_StoreValue(&thread->scope, &slot[paramCount], 6);
			break;
		case 7: { // 'u' - complex/unresolved type
			// Walk the trigger's compiled param list to find the Nth entry
			scopeEntry = (ResultNode *)(uintptr_t)((ResultNode *)thread->stream)->extra;
			loopIdx = paramCount;
			while (loopIdx > 1) {
				scopeEntry = scopeEntry->next;
				loopIdx--;
			}

			// Resolve the actual type
			subType = ResolveResultType(*(CScript **)thread->scriptRef, scopeEntry, &outFlag);
			slot[paramCount].typeId = subType;

			switch (subType) {
			case 0: // int
			case 4: // obj
				slot[paramCount].addr = 0;
				CScope_StoreValue(&thread->scope, &slot[paramCount].addr, sizeof(void *));
				break;
			case 1: // string
			case 2: // ustring
			case 5: // list
				CScope_StoreValue(&thread->scope, &slot[paramCount].addr, sizeof(void *));
				break;
			case 3: // location (6 bytes)
				CScope_StoreValue(&thread->scope, &slot[paramCount].value, 6);
				slot[paramCount].addr = (uintptr_t)&slot[paramCount].value;
				break;
			default:
				ExecAbortCurrent();
				break;
			}
			break;
		}
		case 8: // '|' - separator, skip without decrementing paramCount
			sigPtr--;
			continue;
		default: // case 9 - unknown char
			break;
		}

		paramCount--;
		sigPtr--;
	}

	// Section 3: Return type setup
	retType = GetVarType(handler);

	if (retType == WTYPE_STRING) {
		// Allocate CString: operator new(16) + CString::CString("")
		void *strObj = OperatorNew(sizeof(CString));
		if (strObj != NULL)
			CString_Constructor(strObj, "");
		retObj = (uintptr_t)strObj;
	} else if (retType == WTYPE_USTRING) {
		// Allocate CUString: operator new(16) + CUString::CUString(L"")
		void *ustrObj = OperatorNew(sizeof(CString));
		if (ustrObj != NULL)
			CUString_Constructor(ustrObj, NULL);
		retObj = (uintptr_t)ustrObj;
	}

	// Section 4: 170-case dispatch switch on handler->opcode
	opcode = handler->opcode;
	switch (opcode) {
	case 0:
		((uintptr_t (*)(void))handler->handler)();
		break;
	case 1:
		retObj = ((uintptr_t (*)(void))handler->handler)();
		break;
	case 2:
		((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 3:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 4:
		((uintptr_t (*)(uintptr_t))handler->handler)((uintptr_t)&slot[1].value);
		break;
	case 5:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 6:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 7:
		((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 8:
		((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 9:
	case 11:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 10:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 12:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 13:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 14:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 15:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 16:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 17:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value, slot[7].value);
		break;
	case 18:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 19:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 20:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 21:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 23:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 24: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpStr, slot[1].value, slot[2].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 25: {
		uint8_t tmpUStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpUStr, slot[1].value, slot[2].value);
		CUString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CUString_Destructor((CUString *)tmpUStr);
	} break;
	case 26: {
		uint8_t tmpLoc[8];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpLoc, slot[1].value, slot[2].value);
		CLocation_SetLoc((CLocation *)&retObj, (CLocation *)(uintptr_t)ret);
	} break;
	case 27:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 28: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpStr, slot[1].value, slot[2].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 29: {
		uint8_t tmpLoc[8];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpLoc, slot[1].value, slot[2].value);
		CLocation_SetLoc((CLocation *)&retObj, (CLocation *)(uintptr_t)ret);
	} break;
	case 30:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 31:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].typeId, slot[2].addr);
		break;
	case 32:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].typeId, slot[2].addr, slot[3].value);
		break;
	case 33:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 34:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 35:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].typeId, slot[2].addr);
		break;
	case 36:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 37:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 38:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 39:
#ifdef DEBUG_TELEPORT
		fprintf(stderr, "CASE39: handler=%s sig=%s slot1=0x%X slot2=0x%X,%X,%X\n", handler->name, handler->typeSig, slot[1].value, slot[2].value, slot[2].field4,
		        slot[2].addr);
#endif
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 40:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value);
		break;
	case 41:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value);
		break;
	case 42:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value, slot[4].value);
		break;
	case 43:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 44:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 45:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 46:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 47:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 48:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 49:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 50:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 51:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 52:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 53:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 54:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, (uintptr_t)&slot[2].value, (uintptr_t)&slot[3].value, (uintptr_t)&slot[4].value, (uintptr_t)&slot[5].value);
		break;
	case 55:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 56:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value, slot[7].value, slot[8].value);
		break;
	case 57:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value);
		break;
	case 58:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 59:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value);
		break;
	case 60:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 61:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 62:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 63:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 64:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value);
		break;
	case 65:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, (uintptr_t)&slot[5].value, slot[6].value, slot[7].value, slot[8].value);
		break;
	case 66:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 67:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 68:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 69:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 70:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 71: {
		uint8_t tmpLoc[8];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpLoc, (uintptr_t)&slot[1].value, (uintptr_t)&slot[2].value);
		CLocation_SetLoc((CLocation *)&retObj, (CLocation *)(uintptr_t)ret);
	} break;
	case 72:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 73:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 74:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 75:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 76:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 77:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 78:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 79: {
		        // Binary: handler writes location into tmpLoc and returns
		        // tmpLoc pointer in EAX. CLocation_SetLoc copies from that pointer.
		uint8_t tmpLoc[8];
		((void (*)(void *, uintptr_t))handler->handler)(tmpLoc, slot[1].value);
		CLocation_SetLoc((CLocation *)&retObj, (CLocation *)tmpLoc);
	} break;
	case 80: {
		uint8_t tmpLoc[8];
		((void (*)(void *, uintptr_t))handler->handler)(tmpLoc, slot[1].value);
		CLocation_SetLoc((CLocation *)&retObj, (CLocation *)tmpLoc);
	} break;
	case 81:
		((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 82:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 83:
		retObj = ((uintptr_t (*)(void))handler->handler)();
		break;
	case 84:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 85:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)((uintptr_t)&slot[1].value);
		break;
	case 86:
		((uintptr_t (*)(uintptr_t))handler->handler)((uintptr_t)&slot[1].value);
		break;
	case 87:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 88:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value);
		break;
	case 89:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value, slot[4].value);
		break;
	case 90:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value, slot[4].value);
		break;
	case 91:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 92:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 93: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpStr, (uintptr_t)&slot[1].value, (uintptr_t)&slot[2].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 94: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t))handler->handler)(tmpStr, slot[1].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 95: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpStr, slot[1].value, slot[2].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 96:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value, slot[7].value);
		break;
	case 97:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 98:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 99:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 100:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, (uintptr_t)&slot[5].value);
		break;
	case 101:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 102:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 103:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value);
		break;
	case 104:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 105:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 106:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 107:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].typeId, slot[3].addr);
		break;
	case 108:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].typeId, slot[3].addr);
		break;
	case 110:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)(slot[1].value);
		break;
	case 111:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 112:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 113:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 114:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, (uintptr_t)&slot[2].value, slot[3].value, slot[4].value);
		break;
	case 115:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, (uintptr_t)&slot[5].value);
		break;
	case 116:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 117:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 118:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 119:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 120:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 121:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 122: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t, uintptr_t))handler->handler)(tmpStr, slot[1].value, slot[2].value, slot[3].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 123: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		uintptr_t s;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t, uintptr_t))handler->handler)(tmpStr, slot[1].value, slot[2].value, slot[3].value);
		s = CString_GetString((void *)(uintptr_t)ret);
		CUString_AssignStr((void *)(uintptr_t)retObj, s);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 124:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 125:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value);
		break;
	case 126: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t))handler->handler)(tmpStr, slot[1].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 127:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 128:
		((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 129:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value);
		break;
	case 130:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 131:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 132:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 133:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, (uintptr_t)&slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 134:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 135:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, (uintptr_t)&slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 136:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 137:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 138:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 139:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value, slot[7].value, slot[8].value);
		break;
	case 140:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 141:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 142:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 143:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        (uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value, slot[7].value);
		break;
	case 144:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 145:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value);
		break;
	case 146:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value);
		break;
	case 147:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 148:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, (uintptr_t)&slot[6].value, slot[7].value, slot[8].value, slot[9].value);
		break;
	case 149:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, (uintptr_t)&slot[3].value, (uintptr_t)&slot[4].value, (uintptr_t)&slot[5].value, slot[6].value, slot[7].value,
		        (uintptr_t)&slot[8].value, (uintptr_t)&slot[9].value, slot[10].value, (uintptr_t)&slot[11].value);
		break;
	case 150: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpStr, slot[1].value, slot[2].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 151: {
		uint8_t tmpStr[sizeof(CString)];
		uintptr_t ret;
		ret = ((uintptr_t (*)(void *, uintptr_t, uintptr_t))handler->handler)(tmpStr, slot[1].value, (uintptr_t)&slot[2].value);
		CString_Assign((void *)(uintptr_t)retObj, (void *)(uintptr_t)ret);
		CString_Destructor((CString *)tmpStr);
	} break;
	case 152:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, (uintptr_t)&slot[2].value, (uintptr_t)&slot[3].value, (uintptr_t)&slot[4].value);
		break;
	case 153:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 154:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value, slot[6].value);
		break;
	case 155:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, (uintptr_t)&slot[3].value, (uintptr_t)&slot[4].value, slot[5].value, (uintptr_t)&slot[6].value);
		break;
	case 156:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, slot[2].value, slot[3].value, slot[4].value, slot[5].value);
		break;
	case 157:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, (uintptr_t)&slot[3].value);
		break;
	case 158:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(
		        slot[1].value, (uintptr_t)&slot[2].value, slot[3].value, (uintptr_t)&slot[4].value, slot[5].value);
		break;
	case 159:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 160:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 161:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value);
		break;
	case 162:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 163:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value);
		break;
	case 164:
		retObj = ((uintptr_t (*)(uintptr_t))handler->handler)((uintptr_t)&slot[1].value);
		break;
	case 165:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value, slot[3].value);
		break;
	case 166:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t))handler->handler)(slot[1].value, (uintptr_t)&slot[2].value);
		break;
	case 167:
		((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 168:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)((uintptr_t)&slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 169:
		retObj = ((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))handler->handler)(slot[1].value, slot[2].value, slot[3].value, slot[4].value);
		break;
	case 22:
	case 109:
	default:
		break;
	}

	// Section 5: Result storage loop
	// Re-scan signature with UPPERCASE chars to store output parameters
	paramCount = 1;
	sigPtr = handler->typeSig + 1;

	while (*sigPtr != '\0') {
		ch = (unsigned char)*sigPtr;
		ch -= 'C'; // 0x43

		if (ch <= 57) {
			switch (resultTypeLookup[ch]) {
			case 0: // 'C' - location output (6 bytes)
				CExecThread_StoreResult(thread, paramCount - 1, &slot[paramCount].value, 6);
				paramCount++;
				break;
			case 1: // 'I' - int output
				tmp = slot[paramCount].value;
				CExecThread_StoreResult(thread, paramCount - 1, &tmp, sizeof(void *));
				paramCount++;
				break;
			case 2: // 'O' - object output
				tmp = slot[paramCount].value;
				CExecThread_StoreResult(thread, paramCount - 1, &tmp, sizeof(void *));
				paramCount++;
				break;
			case 3: // '|' - separator
				break;
			default: // any other char
				paramCount++;
				break;
			}
		} else {
			paramCount++;
		}

		sigPtr++;
	}

	// Section 6: Return dispatch
	// Push return value to scope based on return type
	switch (retType) {
	case 0: // int/obj
		tmp = retObj;
		CScope_PushValue(&thread->scope, &tmp, sizeof(void *));
		break;
	case 1: // string - push CString
		CScope_PushCString(&thread->scope, retObj);
		break;
	case 2: // ustring - push CUString
		CScope_PushCUString(&thread->scope, retObj);
		break;
	case 3: // location - push 6-byte value
		CScope_PushValue(&thread->scope, &retObj, 6);
		break;
	case 4: // same as case 0
		tmp = retObj;
		CScope_PushValue(&thread->scope, &tmp, sizeof(void *));
		break;
	case 5: // special push
		CScope_PushResult5(&thread->scope, retObj);
		break;
	default:
		break;
	}
#undef retObj
}

/*
 * 0x0040CBFA - CExecThread::GetEntity
 *
 * If thread->scriptRef is non-null, returns the entity from the
 * script instance (scriptRef+0x0C). Otherwise returns NULL.
 */
CItem *
CExecThread_GetEntity(CExecThread *thread)
{
	if (thread->scriptRef == NULL)
		return NULL;
	return CScriptInstance_GetEntity(thread->scriptRef);
}

/*
 * 0x0040CC1D - CScriptInstance::GetEntity
 *
 * Returns the entity pointer that scriptRef points at.
 */
static CItem *
CScriptInstance_GetEntity(ScriptAttachNode *ref)
{
	return (CItem *)ref->entity;
}

/*
 * 0x0040CC2E - CScriptString::AppendInner
 *
 * Appends the underlying var's name into dest via CScriptVar_AppendName,
 * or returns 0 when there is no var.
 */
static int
CScriptString_AppendInner(CScriptString *this, CString *dest)
{
	if (this->var != NULL)
		return CScriptVar_AppendName(this->var, dest);
	return 0;
}

/*
 * 0x0040CC55 - CScriptString::Append
 *
 * Forwards to CScriptString_AppendInner on this->inner, or returns 0 when
 * inner is NULL.
 */
static int
CScriptString_Append(CScriptString *this, CString *dest)
{
	if (this->inner != NULL)
		return CScriptString_AppendInner(this->inner, dest);
	return 0;
}

/*
 * 0x0040CCE0 - CLocation::SetLoc (scope location copy)
 *
 * Copies the (x, y, z) of src into dst.
 */
void
CLocation_SetLoc(CLocation *dst, const CLocation *src)
{
	CLocation_Set(dst, src->x, src->y, src->z);
}

/*
 * 0x0040CD10 - GetFuncRetType (CFunction::GetReturnType)
 *
 * Returns the WTYPE_* code for the function's return type, decoded
 * from the leading character of its signature string.
 */
int
GetFuncRetType(CFunction *func)
{
	char c = func->sig[0];
	return SigCharToTypeId(c);
}

/*
 * Control Flow Handlers (0x0040D1B2..0x0040D31F)
 *
 * These functions are installed as BuiltinHandlerEntry function pointers
 * in the handler table. ExecuteHandler (0x00406F90) calls them to implement
 * control flow by modifying the current exec thread's stream pointer.
 */

/*
 * 0x0040CD30 - GetVarType
 *
 * Returns the WTYPE_* code for the handler's return type, decoded
 * from the leading character of its signature string.
 */
int
GetVarType(const BuiltinHandlerEntry *entry)
{
	return SigCharToTypeId(entry->typeSig[0]);
}

/*
 * 0x0040CD72 - CFuncScope::CFuncScope (function-local scope constructor)
 *
 * Initializes the embedded CNamedScope and clears resultListHead and
 * bodyStream. Used for function parameter and local variable scopes.
 */
void
CFuncScope_Constructor(CFuncScope *scope)
{
	CNamedScope_Constructor(&scope->namedScope);
	scope->resultListHead = NULL;
	scope->bodyStream = NULL;
}

/*
 * 0x0040CD9C - ResultNode::FreeTree
 *
 * Recursively frees a ResultNode chain along with any nested chain
 * referenced via the extra field.
 */
static void __attribute__((unused))
ResultNode_FreeTree(ResultNode *node)
{
	ResultNode *next;

	while (node != NULL) {
		next = node->next;
		if (node->extra != 0)
			ResultNode_FreeTree((ResultNode *)(uintptr_t)node->extra);
		FreeResultNode(node);
		node = next;
	}
}

/*
 * 0x0040CDE1 - CFuncScope::~CFuncScope (function-local scope destructor)
 *
 * Frees every CMemberNode on the resultList chain and tears down
 * the embedded named scope.
 */
void
CFuncScope_Destructor(CFuncScope *scope)
{
	CMemberNode *node, *next, *tmp;

	for (node = (CMemberNode *)scope->resultListHead; node != NULL; node = next) {
		next = node->next;
		tmp = node;
		if (tmp != NULL)
			CMemberNode_ScalarDtor(tmp, 1);
	}

	CNamedScope_Destructor(&scope->namedScope);
}

/*
 * 0x0040CE6C - AddVarToScope
 *
 * Registers a typed variable (name, typeId) in the function scope.
 * Allocates a CMemberNode, calls CNamedScope_Add against the current
 * compiler script's funcList, and on success threads the node onto
 * resultListHead. Returns 0 on success, 1 on duplicate.
 */
int
AddVarToScope(CFuncScope *scope, int typeId, const char *name)
{
	CMemberNode *node;
	int ret;

	node = (CMemberNode *)OperatorNew(sizeof(CMemberNode));
	if (node != NULL)
		InitVarEntry(node, name, typeId);
	else
		node = NULL;

	g_currentCompileScript = g_ScriptCompiler->script;

	ret = CNamedScope_Add(&scope->namedScope, node->name, typeId, &g_ScriptCompiler->script->funcList);

	if (ret == 0) {
		node->next = (CMemberNode *)scope->resultListHead;
		scope->resultListHead = node;
		g_currentCompileScript = NULL;
		return 0;
	} else {
		g_currentCompileScript = NULL;
		if (node != NULL)
			CMemberNode_ScalarDtor(node, 1);
		return 1;
	}
}

/*
 * 0x0040CF6E - LookupHandler
 *
 * Searches g_BuiltInFuncs for an entry whose name and type signature
 * match the requested ones. Names match either by token-type compare
 * (for the first 20 keyword-style entries) or strcmp; the signature
 * match is case-insensitive and supports 'u' as a wildcard and '*'
 * as a "match the rest" terminator. Returns the entry or NULL.
 */
const BuiltinHandlerEntry *
LookupHandler(const char *name, const char *typeSig)
{
	int i;
	const BuiltinHandlerEntry *entry;
	char msgBuf[256];

	for (i = 0;; i++) {
		const char *p, *q;
		int match;
		int nameMatch;

		entry = &g_BuiltInFuncs[i];
		if (entry->name == NULL)
			break;

		nameMatch = 0;

		/*
		 * For entries 0..19: CompareTokenType (0x0040CF9D).
		 * Computes tokenType = i + 0x25, clamped to max 0x33.
		 */
		if (i < 0x14) {
			int tokenType = i + 0x25;
			if (tokenType > 0x33)
				tokenType = 0x33;
			if (CompareTokenType(name, tokenType))
				nameMatch = 1;
		}

		// All entries: strcmp on name (0x0040CFDE)
		if (!nameMatch) {
			if (strcmp(entry->name, name) != 0)
				continue;
		}

		// Name matched - check type signature (0x0040CFFF)
		p = typeSig;
		q = entry->typeSig;

		// First char check (0x0040D01A)
		if (*p != 'u' && *p != *q)
			continue;
		p++;
		q++;

		// Remaining chars (0x0040D060 loop)
		match = 1;
		while (match) {
			if (*p == '*')
				return entry;
			if (*q == '\0' || *q == '|') {
				if (*p == '\0' || *p == 'v')
					return entry;
				match = 0;
				break;
			}
			if (*p == '\0') {
				match = 0;
				break;
			}
			if (*p == 'u' || *q == 'u') {
				p++;
				q++;
				continue;
			}
			if (tolower(*p) != tolower(*q)) {
				match = 0;
				break;
			}
			p++;
			q++;
		}
	}

	// Dead code - result unused
	sprintf(msgBuf, "Could not find function with name '%s' and args '%s'", name ? name : "(null)", typeSig ? typeSig : "(null)");
	return NULL;
}

/*
 * 0x0040D1B2 - Handler_FOR
 *
 * No-op marker; FOR loop control is implemented by ExecuteHandler.
 */
void
Handler_FOR(int arg1, int arg2)
{
	USED(arg1);
	USED(arg2);
}

/*
 * 0x0040D1C3 - Handler_ENDFOR_NOP
 *
 * No-op marker.
 */
void
Handler_ENDFOR_NOP(int arg1)
{
	USED(arg1);
}

/*
 * 0x0040D1CE - Handler_IF
 *
 * Jumps the current thread's stream to jumpTarget when the condition
 * is false, skipping past the if-body to the else/endif.
 */
void
Handler_IF(int condition, char *jumpTarget)
{
	if (condition == 0) {
		CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
		if (thread != NULL)
			thread->stream = jumpTarget;
	}
}

/*
 * 0x0040D1F7 - Handler_ELSE
 *
 * Unconditionally jumps the current thread to jumpTarget, skipping
 * the ELSE body when the matching IF already ran.
 */
void
Handler_ELSE(char *jumpTarget)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL)
		thread->stream = jumpTarget;
}

/*
 * 0x0040D21A - Handler_ENDIF
 *
 * No-op marker.
 */
void
Handler_ENDIF(void)
{
}

/*
 * 0x0040D21F - Handler_ENDIF2
 *
 * No-op marker.
 */
void
Handler_ENDIF2(void)
{
}

/*
 * 0x0040D224 - Handler_SWITCH: no-op marker
 *
 * The actual switch dispatch is done by ExecuteHandler (0x00406F90).
 */
void
Handler_SWITCH(void)
{
}

/*
 * 0x0040D229 - Handler_SWITCH2
 *
 * No-op marker.
 */
void
Handler_SWITCH2(void)
{
}

/*
 * 0x0040D22E - Handler_CASE
 *
 * Walks the case chain to find an entry whose value matches
 * caseValue and jumps to its target. Falls through to the default
 * branch (marked with sentinel value -666) when no match is found.
 */
void
Handler_CASE(int caseValue, ResultNode *caseChain)
{
	CExecThread *thread;
	ResultNode *node;
	char *defaultTarget = NULL;

	thread = ThreadList_GetCurrent(&g_activeThreadList);

	node = (ResultNode *)caseChain;
	while (node != NULL) {
		if ((int)node->type == caseValue) {
			// Match found - jump to case body
			if (thread != NULL)
				thread->stream = (char *)node->value;
			return;
		}
		if (node->type == 0xFFFFFD66) { /* -666 = default case marker */
			defaultTarget = (char *)node->value;
		}
		node = node->next; /* next in chain */
	}

	// No match - jump to default (or fall through if no default)
	if (thread != NULL)
		thread->stream = defaultTarget;
}

/*
 * 0x0040D29C - Handler_WHILE
 *
 * Jumps past the loop body to endwhile when the condition is false.
 */
void
Handler_WHILE(int condition, char *jumpTarget)
{
	if (condition == 0) {
		CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
		if (thread != NULL)
			thread->stream = jumpTarget;
	}
}

/*
 * 0x0040D2C5 - Handler_ENDWHILE
 *
 * Jumps back to the while condition and clears the break flag so the
 * loop can re-test on the next iteration.
 */
void
Handler_ENDWHILE(char *jumpTarget)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL) {
		thread->stream = jumpTarget;
		thread->defaultReturn = 0;
	}
}

/*
 * 0x0040D2F2 - Handler_ENDFOR
 *
 * Like Handler_ENDWHILE: jumps back to the loop top and clears the
 * break flag.
 */
void
Handler_ENDFOR(char *jumpTarget)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL) {
		thread->stream = jumpTarget;
		thread->defaultReturn = 0;
	}
}

/*
 * 0x0040D31F - Handler_FOR_BODY
 *
 * Unconditional jump used to skip the for-loop body.
 */
void
Handler_FOR_BODY(char *jumpTarget)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL)
		thread->stream = jumpTarget;
}

/*
 * Return Value Handlers (0x0040D342..0x0040D40D)
 *
 * These handlers restore the scope to its saved state (from rstack),
 * write the return value into the scope buffer, and set stream to NULL
 * to signal that the current function/trigger has completed.
 */

/*
 * 0x0040D342 - Handler_GOTO2
 *
 * Unconditional jump to jumpTarget.
 */
void
Handler_GOTO2(char *jumpTarget)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL)
		thread->stream = jumpTarget;
}

/*
 * 0x0040D365 - Handler_RETURN_INT
 *
 * Restores the scope to the saved frame, pushes a 4-byte integer
 * return value, and nulls the stream to end execution.
 */
void
Handler_RETURN_INT(int value)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL) {
		uint32_t saved = CNodeList_Peek(&thread->hstack);
		CScope_SetUsedBytes(&thread->scope, saved);
		uintptr_t val = value;
		CScope_PushValue(&thread->scope, &val, sizeof(void *));
		thread->stream = NULL;
	}
}

/*
 * 0x0040D3B9 - Handler_RETURN_INT2
 *
 * Identical to Handler_RETURN_INT; emitted for object-type returns
 * which also fit in 4 bytes.
 */
void
Handler_RETURN_INT2(int value)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL) {
		uint32_t saved = CNodeList_Peek(&thread->hstack);
		CScope_SetUsedBytes(&thread->scope, saved);
		uintptr_t val = value;
		CScope_PushValue(&thread->scope, &val, sizeof(void *));
		thread->stream = NULL;
	}
}

/*
 * 0x0040D40D - Handler_RETURN_LOC
 *
 * Restores the scope, pushes a 6-byte location return value, and
 * nulls the stream to end execution.
 */
void
Handler_RETURN_LOC(CLocation *locPtr)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL) {
		uint32_t saved = CNodeList_Peek(&thread->hstack);
		CScope_SetUsedBytes(&thread->scope, saved);
		CScope_PushValue(&thread->scope, locPtr, 6);
		thread->stream = NULL;
	}
}

/*
 * 0x0040D459 - Handler_RETURN_STR (TK_RETURN string variant)
 *
 * Heap-copies the string, restores the scope to its saved position,
 * pushes the CString pointer onto the scope, and nulls the stream.
 */
void
Handler_RETURN_STR(CString *str)
{
	CExecThread *thread;
	CString *newStr;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread == NULL)
		return;

	newStr = (CString *)OperatorNew(sizeof(CString));
	if (newStr != NULL)
		CString_CopyConstructor(newStr, (CString *)str);

	// Restore scope to saved position (peek hstack at +0x38)
	uint32_t saved = CNodeList_Peek(&thread->hstack);
	CScope_SetUsedBytes(&thread->scope, saved);

	// Push CString pointer into scope
	CScope_PushCString(&thread->scope, (uintptr_t)newStr);

	// Terminate execution
	thread->stream = NULL;
}

/*
 * 0x0040D509 - Handler_RETURN_VOID
 *
 * Pushes a 4-byte zero placeholder onto the scope and nulls the
 * stream to terminate a void-returning function or trigger.
 */
void
Handler_RETURN_VOID(void)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL) {
		uintptr_t zero = 0;
		CScope_PushValue(&thread->scope, &zero, sizeof(void *));
		thread->stream = NULL;
	}
}

/*
 * 0x0040D547 - split
 *
 * Tokenises str on whitespace, keeping only alnum characters within
 * each token (capped at 126 chars), and appends each token to list
 * as WTYPE_STRING.
 */
void
Script_split(CList *list, CString *str)
{
	char *ptr;
	char buf[128];
	int tokenLen;
	CString tmpStr;

	CString_DefaultConstructor(&tmpStr);

	ptr = CString_GetData(str);
	CList_Clear(list);

	while (*ptr != '\0') {
		tokenLen = 0;

		// Skip whitespace
		while (*ptr != '\0' && isspace((unsigned char)*ptr))
			ptr++;

		// Collect non-whitespace chars, filtering to alnum
		while (*ptr != '\0' && !isspace((unsigned char)*ptr)) {
			if (isalnum((unsigned char)*ptr) && tokenLen < 0x7E)
				buf[tokenLen++] = *ptr;
			ptr++;
		}

		buf[tokenLen] = '\0';
		if (tokenLen > 0) {
			CString_AssignCStr(&tmpStr, buf);
			CList_Append(list, 1, (uintptr_t)&tmpStr);
		}
	}

	CString_Destructor(&tmpStr);
}

/*
 * 0x0040D68E - wordWrap
 *
 * Word-wraps str into lines of at most wrapWidth characters and
 * appends each line to list as WTYPE_STRING. Break points are
 * located by scanning backwards from the wrap column for whitespace.
 * Returns the number of lines produced.
 */
int
Script_wordWrap(CList *list, CString *str, int wrapWidth)
{
	int result;
	int strLen;
	int offset;
	int chunkLen;
	CString *midResult;
	CString tmpStr;

	result = 0;
	strLen = CString_GetLength(str);
	offset = 0;

	while (offset < strLen) {
		chunkLen = wrapWidth - 1;

		if (offset + chunkLen > strLen) {
			// Clamp: remaining string is shorter than wrap width
			chunkLen = strLen - offset;
		} else {
			// Scan backwards for whitespace break point
			while (chunkLen >= 0) {
				char ch = *CString_CharAt(str, (unsigned int)(offset + chunkLen));
				if (isspace((unsigned char)ch))
					break;
				chunkLen--;
			}
		}

		// If scan found space at offset+0 (or scan didn't run),
		// force break at full width
		if (chunkLen == 0)
			chunkLen = wrapWidth;

		midResult = CString_Mid(str, offset, chunkLen);
		CString_CopyConstructor(&tmpStr, midResult);
		CList_Append(list, 1, (uintptr_t)&tmpStr);

		result++;
		offset += chunkLen;

		// Skip whitespace after chunk
		while (offset < strLen) {
			char ch = *CString_CharAt(str, (unsigned int)offset);
			if (!isspace((unsigned char)ch))
				break;
			offset++;
		}

		CString_Destructor(&tmpStr);

		if (offset >= strLen)
			break;
	}

	return result;
}

/*
 * 0x0040D7E4 - textSubstitute
 *
 * Thin wrapper around CString_Replace. Returns 0 if src == dest
 * (self-assignment guard) or if find string is empty. Otherwise
 * delegates to CString_Replace which handles the '%'-triggered
 * substitution.
 */
int
Script_textSubstitute(CString *dest, CString *src, CString *find, CString *replace)
{
	if (src == dest)
		return 0;
	if (CString_GetLength(find) == 0)
		return 0;
	return CString_Replace(dest, src, find, replace);
}

/*
 * 0x0040D819 - splitCommaDelimitedString
 *
 * Splits str on commas (with leading whitespace stripped from each
 * field) and appends each token to list as WTYPE_STRING. Tokens are
 * preserved verbatim and capped at 126 characters.
 */
void
Script_splitCommaDelimitedString(CList *list, CString *str)
{
	char *ptr;
	char buf[128];
	int tokenLen;
	CString tmpStr;

	CString_DefaultConstructor(&tmpStr);

	ptr = CString_GetData(str);
	CList_Clear(list);

	while (*ptr != '\0') {
		tokenLen = 0;

		// Skip leading whitespace
		while (*ptr != '\0' && isspace((unsigned char)*ptr))
			ptr++;

		// Collect until '\0' or ','
		while (*ptr != '\0' && *ptr != ',') {
			if (tokenLen < 0x7E)
				buf[tokenLen++] = *ptr;
			ptr++;
		}

		// If stopped at comma, advance past it
		if (*ptr == ',')
			ptr++;

		buf[tokenLen] = '\0';
		if (tokenLen > 0) {
			CString_AssignCStr(&tmpStr, buf);
			CList_Append(list, 1, (uintptr_t)&tmpStr);
		}
	}

	CString_Destructor(&tmpStr);
}

/*
 * 0x0040D96F - barkstr
 *
 * Debug stub, no-op.
 */
void
Opr_barkstr(void)
{
}

/*
 * 0x0040D974 - barkint
 *
 * Debug stub, no-op.
 */
void
Opr_barkint(void)
{
}

/*
 * 0x0040D979 - strtoi
 *
 * Returns the integer value parsed from str via atoi.
 */
int
Script_strtoi(CString *str)
{
	char *s = CString_GetData(str);
	return atoi(s);
}

/*
 * 0x0040D98F - strlen
 *
 * Returns the length of str.
 */
int
Script_strlen(CString *str)
{
	return CString_GetLength(str);
}

/*
 * 0x0040D99C - toUpper
 *
 * Uppercases the character range [start, end) in str. The walk
 * stops early at the first NUL byte and is skipped entirely if
 * start is past the string length.
 */
void
Script_toUpper(CString *str, int start, int end)
{
	char *buf = CString_GetData(str);
	int len, i;

	len = strlen(buf);
	if (len < start)
		return;
	for (i = start; i < end; i++) {
		if (buf[i] == '\0')
			break;
		buf[i] = toupper(buf[i]);
	}
}

/*
 * 0x0040DA08 - removePrefix
 *
 * Strips prefix from the start of str when present.
 */
void
Script_removePrefix(CString *str, CString *prefix)
{
	char *buf = CString_GetData(str);
	char *pfx = CString_GetData(prefix);
	int pfxLen = CString_GetLength(prefix);

	if (strncmp(buf, pfx, pfxLen) == 0)
		CString_AssignCStr(str, buf + pfxLen);
}

/*
 * 0x0040DA53 - append
 *
 * Appends a (typeTag, value) element to the end of list.
 */
void
Script_append(CList *list, uintptr_t typeTag, uintptr_t value)
{
	CList_Append(list, typeTag, value);
}

/*
 * 0x0040DA68 - concatList
 *
 * Appends every element from src to dst, leaving dst's existing
 * entries in place.
 */
void
Script_concatList(CList *dst, CList *src)
{
	CListNode *cur;

	cur = src->head;
	while (cur != NULL) {
		CList_Append(dst, cur->typeTag, cur->value);
		cur = cur->next;
	}
}

/*
 * 0x0040DAA0 - prepend
 *
 * Inserts a (typeTag, value) element at the head of list.
 */
void
Script_prepend(CList *list, uintptr_t typeTag, uintptr_t value)
{
	CList_Prepend(list, typeTag, value);
}

/*
 * 0x0040DAB5 - setitem
 *
 * Replaces list[index] with (typeTag, value). Aborts the current
 * thread when index is out of range.
 */
void
Script_setitem(CList *list, uintptr_t typeTag, uintptr_t value, int index)
{
	if (CList_GetCount(list) < index) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}
	CList_RemoveAt(list, index);
	CList_InsertAt(list, typeTag, value, index);
}

/*
 * 0x0040DAF5 - setLocItem
 *
 * Replaces list[index] with a location entry (type tag 3). Aborts
 * the current thread when index is out of range.
 */
void
Script_setLocItem(CList *list, uintptr_t locPtr, int index)
{
	if (CList_GetCount(list) < index) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}
	CList_RemoveAt(list, index);
	CList_InsertAt(list, 3, locPtr, index);
}

/*
 * 0x0040DB33 - insert
 *
 * Inserts (typeTag, value) into list at index. Aborts the current
 * thread when index is out of range.
 */
void
Script_insert(CList *list, uintptr_t typeTag, uintptr_t value, int index)
{
	if (CList_GetCount(list) < index) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}
	CList_InsertAt(list, typeTag, value, index);
}

/*
 * 0x0040DB67 - FindEntityValidated
 *
 * Finds entity by serial, checks not NULL and not removed.
 * On failure, formats "%s: object id is invalid.\n" via sprintf
 * to a local buffer (discarded) if caller is non-NULL.
 * Returns entity or NULL.
 */
static CItem *
FindEntityValidated(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto fail;
	if (!ent->resourceEntity.entity.removedFromWorld)
		return ent;
fail:
	if (caller != NULL) {
		const char *name = caller != NULL ? caller : "(null)";
		sprintf(buf, "%s: object id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DBDA - FindWeaponValidated
 *
 * Finds entity by serial, checks IsWeapon (vtable 0xF8) and not removed.
 * On failure, formats "%s: weapon id is invalid.\n" via sprintf
 * to a local buffer (discarded) if caller is non-NULL.
 * Returns entity or NULL.
 */
static CItem *
FindWeaponValidated(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto fail;
	if (!VT_IsWeapon(ent))
		goto fail;
	if (!ent->resourceEntity.entity.removedFromWorld)
		return ent;
fail:
	if (caller != NULL) {
		const char *name = caller != NULL ? caller : "(null)";
		sprintf(buf, "%s: weapon id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DC5F - FindMobileValidated
 *
 * Finds entity by serial, checks IsMobile (vtable 0xD0) and not removed.
 * On failure, formats "%s: mobile id is invalid.\n" via sprintf
 * to a local buffer (discarded) if caller is non-NULL.
 * Returns entity or NULL.
 */
static CItem *
FindMobileValidated(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto fail;
	if (!VT_IsMobile(ent))
		goto fail;
	if (!ent->resourceEntity.entity.removedFromWorld)
		return ent;
fail:
	if (caller != NULL) {
		const char *name = caller != NULL ? caller : "(null)";
		sprintf(buf, "%s: mobile id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DCE4 - FindMobileEntityValidated
 *
 * Finds entity by serial via CWorld_FindBySerial, checks vtable[0xE4]
 * (IsNPC) and byte[6] (removedFromWorld). On failure, formats
 * "%s: NPC id is invalid.\n" via sprintf to a local buffer (discarded)
 * if caller is non-NULL. Returns entity or NULL.
 *
 * FIXED: Binary uses VT_IsNPC (vtable[0xE4]) which rejects players,
 * making mobileHasObjWithListObjOfObj unable to search player
 * inventories. This breaks Q4X6 house ownership checks for players
 * (only NPCs pass). Changed to VT_IsMobile (vtable[0xD0]) so the
 * function accepts both players and NPCs, matching the intent of
 * script callers like Q4X6 which pass player serials.
 */
static CItem *
FindMobileEntityValidated(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto fail;
	if (!VT_IsMobile(ent))
		goto fail;
	if (!ent->resourceEntity.entity.removedFromWorld)
		return ent;
fail:
	if (caller != NULL) {
		const char *name = caller != NULL ? caller : "(null)";
		sprintf(buf, "%s: NPC id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DD69 - FindContainerValidated
 *
 * Finds entity by serial, checks vtable[0xD4] and not removed.
 * On failure, formats "%s: container id is invalid.\n" via sprintf
 * to a local buffer (discarded) if caller is non-NULL.
 * Returns entity or NULL.
 */
static CItem *
FindContainerValidated(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto fail;
	if (!VT_IsMobile2(ent))
		goto fail;
	if (!ent->resourceEntity.entity.removedFromWorld)
		return ent;
fail:
	if (caller != NULL) {
		const char *name = caller != NULL ? caller : "(null)";
		sprintf(buf, "%s: container id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DDEE - FindPlayerValidated
 *
 * Finds entity by serial, checks IsPlayer (vtable 0x18) and not removed.
 * On failure, formats "%s: player id is invalid.\n" via sprintf
 * to a local buffer (discarded) if caller is non-NULL.
 * Returns entity or NULL.
 */
static CItem *
FindPlayerValidated(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto fail;
	if (!VT_IsPlayer(ent))
		goto fail;
	if (!ent->resourceEntity.entity.removedFromWorld)
		return ent;
fail:
	if (caller != NULL) {
		const char *name = caller != NULL ? caller : "(null)";
		sprintf(buf, "%s: player id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DE70 - FindCorpseValidated
 *
 * Resolves serial to entity via CWorld_FindBySerial, checks
 * vtable[0xFC] (HasCorpseEq) and removedFromWorld == 0.
 * Returns entity pointer on success, NULL on failure.
 * If caller is non-NULL and validation fails, formats error
 * "%s: id is invalid.\n" via sprintf to local buffer (discarded).
 */
static CItem *
FindCorpseValidated(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *entity;
	const char *name;

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		goto fail;
	if (!((int (*)(void *))VT_FN(entity, VT_HAS_CORPSE_EQ))(entity))
		goto fail;
	if (!entity->resourceEntity.entity.removedFromWorld)
		return entity;
fail:
	if (caller != NULL) {
		if (caller != NULL)
			name = caller;
		else
			name = "(null)";
		sprintf(buf, "%s: id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DEF5 - FindMapItem
 *
 * Resolves serial to CSignpost entity via CWorld_FindBySerial, checks
 * IsMap (vtable[0xE0]) and removedFromWorld. Formats error message
 * via sprintf to local buffer (discarded) if invalid.
 */
static CItem *
FindMapItem(uint32_t serial, const char *caller)
{
	char buf[256];
	CItem *entity;
	const char *name;

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		goto fail;
	if (!check_IsMap(entity))
		goto fail;
	if (!entity->resourceEntity.entity.removedFromWorld)
		return entity;
fail:
	if (caller != NULL) {
		if (caller != NULL)
			name = caller;
		else
			name = "(null)";
		sprintf(buf, "%s: map id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DF7A - FindBookValidated
 *
 * Finds entity by serial, checks it's a writable book (graphic
 * 0xFF1-0xFF2) and not deleted. Logs error with caller name on
 * failure. Returns entity or NULL.
 */
static CItem *
FindBookValidated(uint32_t serial, const char *caller)
{
	CItem *ent;
	char buf[256];
	const char *name;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto fail;
	if (!CItem_IsWritableBook(ent))
		goto fail;
	if (!ent->resourceEntity.entity.removedFromWorld)
		return ent;
fail:
	if (caller != NULL) {
		if (caller != NULL)
			name = caller;
		else
			name = "(null)";
		sprintf(buf, "%s: RWBook id is invalid.\n", name);
	}
	return NULL;
}

/*
 * 0x0040DFF9 - numinlist
 *
 * Returns the number of elements in list.
 */
int
Script_numinlist(CList *list)
{
	return CList_GetCount(list);
}

/*
 * 0x0040E006 - isinlist
 *
 * Returns 1 when (type, value) is present in list.
 */
int
Script_isinlist(CList *list, uintptr_t type, uintptr_t value)
{
	return CList_Find(list, type, value);
}

/*
 * 0x0040E01B - removeitem
 *
 * Removes list[index]. Aborts the current thread when index is
 * out of range.
 */
void
Script_removeitem(CList *list, int index)
{
	if (CList_GetCount(list) < index) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}
	CList_RemoveAt(list, index);
}

/*
 * 0x0040E047 - truncateList
 *
 * Drops every entry at index and beyond, leaving the first index
 * elements in place.
 */
void
Script_truncateList(CList *list, int index)
{
	if (index < 0)
		return;
	while (CList_GetCount(list) > index)
		CList_RemoveAt(list, index);
}

/*
 * 0x0040E06F - removespecificitem
 *
 * Removes the first list entry that matches (typeTag, value).
 */
void
Script_removespecificitem(CList *list, uintptr_t typeTag, uintptr_t value)
{
	CList_RemoveSpecific(list, typeTag, value);
}

/*
 * 0x0040E084 - clearlist
 *
 * Removes every element from list.
 */
void
Script_clearlist(CList *list)
{
	CList_Clear(list);
}

/*
 * 0x0040E091 - getListItemType
 *
 * Returns the WTYPE_* tag of list[index], or WTYPE_UNKNOWN when
 * index is out of range.
 */
int
Script_getListItemType(CList *list, int index)
{
	CListNode *cur;
	int i;

	cur = list->head;
	for (i = 0; cur != NULL && i < index; i++)
		cur = cur->next;

	if (cur == NULL)
		return 7; /* WTYPE_UNKNOWN */
	return (int)cur->typeTag;
}

/*
 * 0x0040E0E0 - sortList int comparator
 *
 * Compares two nodes by int value (value). Aborts if either
 * node's type is not 0 (INT).
 */
static int
sortList_cmpInt(CListNode *a, CListNode *b)
{
	if ((int)a->typeTag != 0 || (int)b->typeTag != 0) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return 0;
	}
	return (int)a->value - (int)b->value;
}

/*
 * 0x0040E115 - sortList string comparator
 *
 * Compares two nodes by string contents via strcmp.
 * Returns 0 if either node's type is not 1 (STRING).
 */
static int
sortList_cmpStr(CListNode *a, CListNode *b)
{
	if ((int)a->typeTag != 1 || (int)b->typeTag != 1)
		return 0;
	return strcmp(CString_GetData((void *)(uintptr_t)a->value), CString_GetData((void *)(uintptr_t)b->value));
}

/*
 * 0x0040E150 - sortList object comparator
 *
 * Orders two object entries by their CItem sort key (tiledata
 * quantity). Returns 0 if either entry is missing or invalid.
 */
static int
sortList_cmpObj(CListNode *a, CListNode *b)
{
	CItem *entA, *entB;
	uint16_t keyA, keyB;

	if ((int)a->typeTag != 4 || (int)b->typeTag != 4) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return 0;
	}

	entA = FindEntityValidated(a->value, "sortList");
	entB = FindEntityValidated(b->value, "sortList");
	if (entA == NULL || entB == NULL)
		return 0;

	keyA = (uint16_t)CItem_GetSortKey(entA);
	keyB = (uint16_t)CItem_GetSortKey(entB);
	return (int)keyA - (int)keyB;
}

/*
 * 0x0040E1DE - sortList
 *
 * Bubble-sorts list. The low bit of flags reverses the order; the
 * remaining bits select the comparator (0=int, 2=string, 4=object).
 * Aborts the current thread on an unsupported sort type.
 */
void
Script_sortList(CList *list, int flags)
{
	int reverse, sortType, swapped, result;
	int (*cmpfn)(CListNode *, CListNode *);
	CListIterator iter;
	CListNode *node1, *node2;

	if (list->count < 2)
		return;

	reverse = flags & 1;
	sortType = flags & ~1;

	switch (sortType) {
	case 0:
		cmpfn = sortList_cmpInt;
		break;
	case 2:
		cmpfn = sortList_cmpStr;
		break;
	case 4:
		cmpfn = sortList_cmpObj;
		break;
	default:
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}

	do {
		swapped = 0;
		iter.list = list;
		iter.current = list->head;

		while (1) {
			node1 = iter.current;
			StdPtrIter_Next((StdPtrIterFull *)&iter);
			if (iter.current == NULL)
				break;
			node2 = iter.current;

			if (reverse)
				result = cmpfn(node2, node1);
			else
				result = cmpfn(node1, node2);

			if (result > 0) {
				StdPtrIter_Prev((StdPtrIterFull *)&iter);
				CListIterator_Remove(&iter);
				StdPtrIter_Next((StdPtrIterFull *)&iter);
				CListIterator_InsertBefore(&iter, node1);
				swapped = 1;
			}
		}
	} while (swapped);
}

/*
 * Array Handlers
 *
 * Binary uses std::map<int, CArray*> (StdMapTree) at 0x0063D840.
 * CArray layout (12 bytes on 32-bit):
 *   +0x00 int       width  - number of columns
 *   +0x04 int       height - number of data rows
 *   +0x08 uint32_t *data   - flat buffer, width*(height+1) elements
 *
 * Row 0 of data stores per-column type tags (0=int, 1=str, 2=ustr).
 * Rows 1..height store actual data.
 * Element addressing: data[x + (y+1) * width].
 */

/*
 * Operator Handlers (0x0040E2F4..0x0040E4E4)
 *
 * Builtin operators registered in the handler table at 0x00606EA0.
 * Each implements a script-level assignment or arithmetic operator.
 */

/*
 * 0x0040E2F4 - assignint
 *
 * Stores value through dst.
 */
void
Opr_assignint(int *dst, int value)
{
	*dst = value;
}

/*
 * 0x0040E301 - assignobj
 *
 * Stores serial through dst (object handles are 32-bit serials).
 */
void
Opr_assignobj(int *dst, int serial)
{
	*dst = serial;
}

/*
 * 0x0040E30E - assignstr
 *
 * Copies src into dst.
 */
void
Opr_assignstr(CString *dst, CString *src)
{
	CString_Assign(dst, src);
}

/*
 * 0x0040E31F - assignust
 *
 * Copies the unicode src into dst.
 */
void
Opr_assignust(CUString *dst, CUString *src)
{
	CUString_Assign(dst, src);
}

/*
 * 0x0040E330 - concat
 *
 * Appends src to dst in place.
 */
void
Opr_concat(CString *dst, CString *src)
{
	CString_ConcatCString(dst, src);
}

/*
 * 0x0040E341 - assignloc
 *
 * Copies the 6-byte location from src to dst.
 */
void
Opr_assignloc(CLocation *dst, const CLocation *src)
{
	CLocation_SetLoc(dst, src);
}

/*
 * 0x0040E352 - assignlist
 *
 * Replaces the contents of dst with a shallow copy of every entry
 * from src.
 */
void
Opr_assignlist(CList *dst, CList *src)
{
	CListNode *cur;

	CList_Clear(dst);

	cur = src->head;
	while (cur != NULL) {
		CList_Append(dst, cur->typeTag, cur->value);
		cur = cur->next;
	}
}

/*
 * 0x0040E392 - oprnull
 *
 * Identity passthrough.
 */
int
Opr_null(int a)
{
	return a;
}

/*
 * 0x0040E39A - oprplus (int variant)
 *
 * Returns a + b.
 */
int
Opr_plus_int(int a, int b)
{
	return a + b;
}

/*
 * 0x0040E3A5 - oprminus
 *
 * Returns a - b.
 */
int
Opr_minus(int a, int b)
{
	return a - b;
}

/*
 * 0x0040E3B0 - oprmult
 *
 * Returns a * b.
 */
int
Opr_mult(int a, int b)
{
	return a * b;
}

/*
 * 0x0040E3BC - oprdiv
 *
 * Returns a / b, or INT_MAX when b is zero.
 */
int
Opr_div(int a, int b)
{
	if (b == 0)
		return 0x7FFFFFFF;
	return a / b;
}

/*
 * 0x0040E3E0 - oprand
 *
 * Logical AND: returns 1 when both operands are non-zero.
 */
int
Opr_and(int a, int b)
{
	return (a != 0 && b != 0) ? 1 : 0;
}

/*
 * 0x0040E407 - opror
 *
 * Logical OR: returns 1 when either operand is non-zero.
 */
int
Opr_or(int a, int b)
{
	return (a == 0 && b == 0) ? 0 : 1;
}

/*
 * 0x0040E42E - oprxor
 *
 * Returns a ^ b.
 */
int
Opr_xor(int a, int b)
{
	return a ^ b;
}

/*
 * 0x0040E439 - oprequiv (int variant)
 *
 * Returns a == b.
 */
int
Opr_equiv_int(int a, int b)
{
	return a == b;
}

/*
 * 0x0040E44B - oprnequiv (int variant)
 *
 * Returns a != b.
 */
int
Opr_nequiv_int(int a, int b)
{
	return a != b;
}

/*
 * 0x0040E45D - oprgt
 *
 * Returns a > b.
 */
int
Opr_gt(int a, int b)
{
	return a > b;
}

/*
 * 0x0040E46F - oprlt
 *
 * Returns a < b.
 */
int
Opr_lt(int a, int b)
{
	return a < b;
}

/*
 * 0x0040E481 - oprmod
 *
 * Returns a % b.
 */
int
Opr_mod(int a, int b)
{
	return a % b;
}

/*
 * 0x0040E48F - oprgteq
 *
 * Returns a >= b.
 */
int
Opr_gteq(int a, int b)
{
	return a >= b;
}

/*
 * 0x0040E4A1 - oprlteq
 *
 * Returns a <= b.
 */
int
Opr_lteq(int a, int b)
{
	return a <= b;
}

/*
 * 0x0040E4B3 - oprnot
 *
 * Returns the logical NOT of a.
 */
int
Opr_not(int a)
{
	return a == 0;
}

/*
 * 0x0040E4C1 - oprinc
 *
 * Increments *ptr in place.
 */
void
Opr_inc(int *ptr)
{
	(*ptr)++;
}

/*
 * 0x0040E4D3 - oprdec
 *
 * Decrements *ptr in place.
 */
void
Opr_dec(int *ptr)
{
	(*ptr)--;
}

/*
 * 0x0040E4E5 - oprplus (string+string variant, "sss")
 *
 * Concatenates a and b into out and returns out.
 */
CString *
Opr_plus_str_str(CString *out, CString *a, CString *b)
{
	CString *tmp = CString_OpPlusCString(a, b);
	CString_CopyConstructor(out, tmp);
	return out;
}

/*
 * 0x0040E515 - oprequiv (string variant)
 *
 * Returns 1 when a and b match case-insensitively.
 */
int
Opr_equiv_str(CString *a, CString *b)
{
	char *sa = CString_GetData(a);
	char *sb = CString_GetData(b);
	return strcasecmp(sa, sb) == 0 ? 1 : 0;
}

/*
 * 0x0040E539 - oprnequiv (string variant)
 *
 * Returns a case-insensitive strcmp of a vs b (non-zero when
 * the strings differ).
 */
int
Opr_nequiv_str(CString *a, CString *b)
{
	char *sa = CString_GetData(a);
	char *sb = CString_GetData(b);
	return strcasecmp(sa, sb);
}

/*
 * 0x0040E558 - oprequiv (loc variant)
 *
 * Returns 1 when loc1 and loc2 share the same x,y (z is ignored).
 */
int
Opr_equiv_loc(const CLocation *loc1, const CLocation *loc2)
{
	return (loc1->x == loc2->x && loc1->y == loc2->y) ? 1 : 0;
}

/*
 * 0x0040E595 - oprnequiv (loc variant)
 *
 * Returns 1 when loc1 and loc2 differ in x or y (z is ignored).
 */
int
Opr_nequiv_loc(const CLocation *loc1, const CLocation *loc2)
{
	return (loc1->x == loc2->x && loc1->y == loc2->y) ? 0 : 1;
}

/*
 * 0x0040E5D2 - oprequiv (obj variant)
 *
 * Returns 1 when the two object serials match.
 */
int
Opr_equiv_obj(int a, int b)
{
	return a == b;
}

/*
 * 0x0040E5E4 - oprnequiv (obj variant)
 *
 * Returns 1 when the two object serials differ.
 */
int
Opr_nequiv_obj(int a, int b)
{
	return a != b;
}

/*
 * 0x0040E5F6 - oprlist (universal variant)
 *
 * No-op stub returning 0.
 */
int
Opr_listuni(CList *list, int index)
{
	USED(list);
	USED(index);
	return 0;
}

/*
 * 0x0040E609 - List_GetAt_Typed
 *
 * Returns list[index] when its type tag matches expectedType. Aborts the
 * current thread when index is out of range or the type tag does not match.
 */
static CListNode *
List_GetAt_Typed(CList *list, int index, int expectedType)
{
	CListNode *cur;
	char buf[512];
	int i;

	cur = list->head;
	for (i = 0; cur != NULL && i < index; i++)
		cur = cur->next;

	if (cur == NULL) {
		sprintf(buf, "Tried to access #%d element in list with %d items\n", index, i);
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return NULL;
	}

	if ((int)cur->typeTag != expectedType) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return NULL;
	}

	return cur;
}

/*
 * 0x0040E698 - oprlist (list variant)
 *
 * Returns a freshly allocated CList that is a copy of the sublist
 * stored at list[index].
 */
CList *
Opr_listlist(CList *list, int index)
{
	CList *result;
	CListNode *node;

	result = (CList *)OperatorNew(sizeof(CList));
	if (result != NULL)
		CList_Constructor(result);

	node = List_GetAt_Typed(list, index, 5);
	if (node != NULL)
		Script_copylist(result, (CList *)(uintptr_t)node->value);

	return result;
}

/*
 * 0x0040E733 - oprlist (int variant)
 *
 * Returns the int value stored at list[index].
 */
int
Opr_listint(CList *list, int index)
{
	CListNode *node = List_GetAt_Typed(list, index, 0);
	if (node == NULL)
		return 0;
	return (int)node->value;
}

/*
 * 0x0040E760 - oprlist (string variant)
 *
 * Copies the CString stored at list[index] into dest, or stores an
 * empty string when the slot is missing. Returns dest.
 */
CString *
Opr_liststr(CString *dest, CList *list, int index)
{
	CListNode *node;

	node = List_GetAt_Typed(list, index, 1);
	if (node == NULL) {
		CString_Constructor((CString *)dest, "");
		return dest;
	}

	CString_CopyConstructor((CString *)dest, (CString *)(uintptr_t)node->value);
	return dest;
}

/*
 * 0x0040E7C2 - oprlist (ustring variant)
 *
 * Copies the CUString stored at list[index] into dest, or stores an
 * empty unicode string when the slot is missing. Returns dest.
 */
CUString *
Opr_listustr(CUString *dest, CList *list, int index)
{
	CListNode *node;

	node = List_GetAt_Typed(list, index, 2);
	if (node == NULL) {
		CUString tmpU;
		static const uint16_t emptyWStr[] = { 0 };
		CUString_Constructor(&tmpU, emptyWStr);
		CUString_CopyConstructor((CUString *)dest, &tmpU);
		CUString_Destructor(&tmpU);
		return dest;
	}

	CUString_CopyConstructor((CUString *)dest, (CUString *)(uintptr_t)node->value);
	return dest;
}

/*
 * 0x0040E863 - oprlist (loc variant)
 *
 * Copies the CLocation stored at list[index] into outLoc, falling
 * back to (-1, -1, 0) when the slot is missing. Returns outLoc.
 */
CLocation *
Opr_listloc(CLocation *outLoc, CList *list, int index)
{
	CLocation localLoc;
	CListNode *node = List_GetAt_Typed(list, index, 3);
	if (node == NULL) {
		CLocation_Init(&localLoc);
		CLocation_Set(&localLoc, -1, -1, 0);
		CLocation_SetLoc(outLoc, &localLoc);
		return outLoc;
	}
	CLocation_SetLoc(outLoc, (CLocation *)(uintptr_t)node->value);
	return outLoc;
}

/*
 * 0x0040E8C1 - oprlist (obj variant)
 *
 * Returns the object serial stored at list[index].
 */
int
Opr_listobj(CList *list, int index)
{
	CListNode *node = List_GetAt_Typed(list, index, 4);
	if (node == NULL)
		return 0;
	return (int)node->value;
}

/*
 * 0x0040E8EE - oprlist (string-from-string-index variant, "ssi")
 *
 * Stores the index-th character of source as a one-character
 * CString in dest. Returns dest.
 */
CString *
Opr_strindex(CString *dest, CString *source, int index)
{
	char ch;
	char *ptr;

	// CString::operator[] (0x004D37AE)
	ptr = CString_CharAt(source, (unsigned int)index);
	ch = *ptr;

	{
		CString tmp;
		CString_ConstructorFromChar(&tmp, ch);
		CString_CopyConstructor(dest, &tmp);
		CString_Destructor(&tmp);
	}

	return dest;
}

/*
 * 0x0040E95F - assignintstr
 *
 * Parses src with atoi and stores the result through dst.
 */
void
Opr_assignintstr(int *dst, CString *src)
{
	char *s = CString_GetData(src);
	*dst = atoi(s);
}

/*
 * 0x0040E97A - objtoint
 *
 * Returns the object serial unchanged (object handles are 32-bit ints).
 */
int
Opr_objtoint(int serial)
{
	return serial;
}

/*
 * 0x0040E982 - assignstrint
 *
 * Assigns the decimal representation of value to dst.
 */
void
Opr_assignstrint(CString *dst, int value)
{
	char buf[20];
	sprintf(buf, "%d", value);
	CString_AssignCStr(dst, buf);
}

/*
 * 0x0040E9AD - assignustint
 *
 * Assigns the decimal representation of value to the unicode dst.
 */
void
Opr_assignustint(CUString *dst, int value)
{
	CUString_AssignStr(dst, value);
}

/*
 * 0x0040E9BE - oprplus (string+int variant, "ssi")
 *
 * Stores str with value's decimal representation appended into out.
 */
CString *
Opr_plus_str_int(CString *out, CString *str, int value)
{
	char buf[20];
	CString *tmp;
	sprintf(buf, "%d", value);
	tmp = CString_OpPlusCStr(str, buf);
	CString_CopyConstructor(out, tmp);
	return out;
}

/*
 * 0x0040EA04 - oprplus (string+loc variant, "ssc")
 *
 * Stores str with the location formatted as "x,y,z" appended into
 * out.
 */
CString *
Opr_plus_str_loc(CString *out, CString *str, const CLocation *loc)
{
	char buf[36];
	CString *tmp;
	sprintf(buf, "%d,%d,%d", (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)loc->z);
	tmp = CString_OpPlusCStr(str, buf);
	CString_CopyConstructor(out, tmp);
	return out;
}

/*
 * 0x0040EA5E - assignlocint
 *
 * Builds a location at (x, y, z) and stores it in dst.
 */
void
Opr_assignlocint(CLocation *dst, int x, int y, int z)
{
	dst->x = (int16_t)x;
	dst->y = (int16_t)y;
	dst->z = (int16_t)z;
}

/*
 * 0x0040EA83 - CArray::CArray (default constructor)
 *
 * Zeroes width, height and data. Returns this.
 */
static WombatArray *
CArray_Constructor(WombatArray *arr)
{
	arr->height = 0;
	arr->width = 0;
	arr->data = NULL;
	return arr;
}

/*
 * 0x0040EAAE - CArray::Init
 *
 * Validates dimensions (1..1024 each), allocates data buffer
 * (width * (height+1) uint32_t's), zeroes it, then stores column
 * type tags from the type list into row 0. Only type 1 (str) and
 * type 2 (ustr) are stored; type 0 (int) remains as the default 0.
 * Returns 1 on success, 0 on failure.
 */
static int
CArray_Init(WombatArray *arr, int width, int height, CList *typeList)
{
	CListNode *node;
	int i;

	if (width < 1 || width > WOMBAT_ARRAY_MAX_DIM)
		return 0;
	if (height < 1 || height > WOMBAT_ARRAY_MAX_DIM)
		return 0;

	// If already initialized, free first
	if (arr->data != NULL)
		CArray_Free(arr);

	arr->width = width;
	arr->height = height;
	arr->data = (uintptr_t *)OperatorNew(width * (height + 1) * sizeof(uintptr_t));
	if (arr->data == NULL)
		return 0;
	memset(arr->data, 0, width * (height + 1) * sizeof(uintptr_t));

	// Store column type tags from type list into header row
	node = typeList->head;
	for (i = 0; i < width; i++) {
		arr->data[i] = 0;
	}
	node = typeList->head;
	for (i = 0; i < width && node != NULL; i++) {
		if (node->typeTag == 1 || node->typeTag == 2)
			arr->data[i] = node->typeTag;
		node = node->next;
	}
	return 1;
}

/*
 * 0x0040EBE8 - CArray::Free
 *
 * Releases the array's data buffer and resets dimensions to zero.
 *
 * FIXED: the binary's inner cleanup loop has `j > arr->height`
 * (always false at j=0), so the per-element CString/CUString delete
 * branch is dead code and every string stored via SetStrElem or
 * SetUStrElem leaks for the lifetime of the process. The intended
 * bound is `j < arr->height`.
 */
static void
CArray_Free(WombatArray *arr)
{
	int i, j;
	uintptr_t *elem;
	void *obj;

	for (i = 0; i < arr->width; i++) {
		if (arr->data[i] != 1 && arr->data[i] != 2)
			continue;
		for (j = 0; j < arr->height; j++) {
			elem = CArray_ElemLookup(arr, i, j, arr->data[i]);
			if (arr->data[i] == 1) {
				if (elem != NULL && *elem != 0) {
					obj = (void *)(uintptr_t)*elem;
					if (obj != NULL)
						CString_ScalarDelete((CString *)obj, 1);
				}
			} else {
				if (elem != NULL && *elem != 0) {
					obj = (void *)(uintptr_t)*elem;
					if (obj != NULL)
						CUString_ScalarDelete((CUString *)obj, 1);
				}
			}
		}
	}
	arr->width = 0;
	arr->height = 0;
	OperatorDelete(arr->data);
	arr->data = NULL;
}

/*
 * 0x0040ED38 - CArray::GetIntElem
 *
 * Looks up element at (x, y) with type 0 (int). Returns the value,
 * or 0 if out of bounds or wrong type.
 */
static uintptr_t
CArray_GetIntElem(WombatArray *arr, int x, int y)
{
	uintptr_t *elem;

	elem = CArray_ElemLookup(arr, x, y, WOMBAT_ARRAY_TYPE_INT);
	if (elem == NULL)
		return 0;
	return *elem;
}

/*
 * 0x0040ED6B - CArray::GetStrElem
 *
 * Looks up element at (x, y) with type 1 (str). Returns the stored
 * CString pointer (cast from uint32_t), or NULL if not found.
 */
static void *
CArray_GetStrElem(WombatArray *arr, int x, int y)
{
	uintptr_t *elem;

	elem = CArray_ElemLookup(arr, x, y, WOMBAT_ARRAY_TYPE_STR);
	if (elem == NULL)
		return NULL;
	return (void *)(uintptr_t)*elem;
}

/*
 * 0x0040ED9E - CArray::GetUStrElem
 *
 * Looks up element at (x, y) with type 2 (ustr). Returns the stored
 * CUString pointer (cast from uint32_t), or NULL if not found.
 */
static void *
CArray_GetUStrElem(WombatArray *arr, int x, int y)
{
	uintptr_t *elem;

	elem = CArray_ElemLookup(arr, x, y, WOMBAT_ARRAY_TYPE_USTR);
	if (elem == NULL)
		return NULL;
	return (void *)(uintptr_t)*elem;
}

/*
 * 0x0040EDD1 - CArray::SetIntElem
 *
 * Sets element at (x, y) with type 0 (int) to val.
 */
static void
CArray_SetIntElem(WombatArray *arr, int x, int y, uintptr_t val)
{
	uintptr_t *elem;

	elem = CArray_ElemLookup(arr, x, y, WOMBAT_ARRAY_TYPE_INT);
	if (elem == NULL)
		return;
	*elem = val;
}

/*
 * 0x0040EE05 - CArray::SetStrElem
 *
 * Sets element at (x, y) with type 1 (str). If the slot is empty,
 * allocates a new CString object (16 bytes). Assigns the string
 * value from src via CString::operator=.
 */
static void
CArray_SetStrElem(WombatArray *arr, int x, int y, void *src)
{
	uintptr_t *elem;
	void *obj;

	elem = CArray_ElemLookup(arr, x, y, WOMBAT_ARRAY_TYPE_STR);
	if (elem == NULL)
		return;
	if (*elem == 0) {
		obj = OperatorNew(sizeof(CString));
		if (obj == NULL)
			obj = NULL;
		else
			CString_DefaultConstructor((CString *)obj);
		*elem = (uintptr_t)obj;
	}
	CString_Assign((void *)(uintptr_t)*elem, src);
}

/*
 * 0x0040EEA9 - CArray::SetUStrElem
 *
 * Sets element at (x, y) with type 2 (ustr). If the slot is empty,
 * allocates a new CUString object (16 bytes). Assigns the unicode
 * string value from src via CUString::operator=.
 */
static void
CArray_SetUStrElem(WombatArray *arr, int x, int y, void *src)
{
	uintptr_t *elem;
	void *obj;

	elem = CArray_ElemLookup(arr, x, y, WOMBAT_ARRAY_TYPE_USTR);
	if (elem == NULL)
		return;
	if (*elem == 0) {
		obj = OperatorNew(sizeof(CString));
		if (obj == NULL)
			obj = NULL;
		else
			CUString_DefaultConstructor((CUString *)obj);
		*elem = (uintptr_t)obj;
	}
	CUString_Assign((void *)(uintptr_t)*elem, src);
}

/*
 * 0x0040EF4D - CArray::ElemLookup
 *
 * Validates x in [0, width), y in [0, height), and checks that the
 * column type tag (data[x]) equals expectedType. Returns pointer to
 * data[(y+1)*width + x], or NULL on failure.
 */
static uintptr_t *
CArray_ElemLookup(WombatArray *arr, int x, int y, uintptr_t expectedType)
{
	if (x < 0)
		return NULL;
	if (x >= arr->width)
		return NULL;
	if (y < 0)
		return NULL;
	if (y >= arr->height)
		return NULL;
	if (arr->data[x] != expectedType)
		return NULL;
	return &arr->data[x + (y + 1) * arr->width];
}

/*
 * 0x0040EFB9 - WombatArrays_StaticInit
 *
 * Initialises the WombatArrays std::map by issuing a single insert
 * call, which allocates the tree's head/nil sentinel nodes.
 */
void
WombatArrays_StaticInit(void)
{
	uintptr_t local1, local2;

	StdMap_InsertWrapper(&g_WombatArrays, &local1, &local2);
}

/*
 * 0x0040EFD5 - WombatArrays_RegisterDestructor
 *
 * Registers the atexit callback that tears down the WombatArrays map.
 */
static __attribute__((unused)) void
WombatArrays_RegisterDestructor(void)
{
	atexit(WombatArrays_AtexitCallback);
}

/*
 * 0x0040EFE7 - WombatArrays atexit callback
 *
 * Registered by WombatArrays_RegisterDestructor. Destroys the std::map at
 * 0x63D840 via StdPtrList_Destructor, guarded by a one-shot flag at 0x63D850
 * to prevent double destruction.
 */
static void
WombatArrays_AtexitCallback(void)
{
	if (g_WombatArraysDestructorFlag & 1)
		return;
	g_WombatArraysDestructorFlag |= 1;
	StdPtrList_Destructor((StdPtrList *)&g_WombatArrays);
}

/*
 * 0x0040F013 - ArrayCreate
 *
 * Returns the CArray bound to id, allocating and inserting one when
 * the map has no entry yet.
 */
static WombatArray *
ArrayCreate(int id)
{
	void *findIter, *endIter;
	uintptr_t pair[2];
	uintptr_t insertIter[2];
	WombatArray *arr;
	uintptr_t pairBuf[2];

	StdMap_FindWrapper(&g_WombatArrays, &findIter, &id);
	StdMap_End(&g_WombatArrays, &endIter);
	if (StdPtrIter_Eq((StdPtrNode **)&findIter, (StdPtrNode **)&endIter)) {
		// Not found - allocate and insert
		arr = (WombatArray *)OperatorNew(sizeof(WombatArray));
		if (arr != NULL)
			CArray_Constructor(arr);

		// Build key-value pair and insert
		pairBuf[0] = (uintptr_t)id;
		pairBuf[1] = (uintptr_t)arr;
		StdMap_PairConstructor(pair, &pairBuf[0], &pairBuf[1]);
		StdMap_LowerBound(&g_WombatArrays, insertIter, pair);
		findIter = *(void **)insertIter;
	}
	// Dereference iterator to get key-value pair, return value
	return (WombatArray *)((uintptr_t *)StdTreeIter_Deref(&findIter))[1];
}

/*
 * 0x0040F0D8 - ArrayLookup
 *
 * Returns the CArray registered under id, or NULL when no entry
 * exists.
 */
static WombatArray *
ArrayLookup(int id)
{
	void *findIter, *endIter;

	StdMap_FindWrapper(&g_WombatArrays, &findIter, &id);
	StdMap_End(&g_WombatArrays, &endIter);
	if (StdPtrIter_Neq((StdPtrNode **)&findIter, (StdPtrNode **)&endIter))
		return (WombatArray *)((uintptr_t *)StdTreeIter_Deref(&findIter))[1];
	return NULL;
}

/*
 * 0x0040F12E - ArrayDelete
 *
 * Frees the CArray registered under id and removes it from the map.
 *
 * FIXED: the binary calls CArray::Free (the destructor, which only
 * releases arr->data) then erases the map entry, but never invokes
 * operator delete on the CArray struct itself. Every script-driven
 * deleteArray therefore leaks one 16-byte WombatArray header. Add
 * OperatorDelete after the erase to release the struct.
 */
static void
ArrayDelete(int id)
{
	void *findIter, *endIter;
	void *eraseIter;
	void *node;
	WombatArray *arr;

	StdMap_FindWrapper(&g_WombatArrays, &findIter, &id);
	StdMap_End(&g_WombatArrays, &endIter);
	if (StdPtrIter_Eq((StdPtrNode **)&findIter, (StdPtrNode **)&endIter))
		return;
	arr = (WombatArray *)((uintptr_t *)StdTreeIter_Deref(&findIter))[1];
	CArray_Free(arr);
	node = *(void **)&findIter;
	StdMap_EraseWrapper(&g_WombatArrays, &eraseIter, node);
	OperatorDelete(arr);
}

/*
 * 0x0040F18E - isArrayInit
 *
 * Looks up array by ID, returns 1 if it exists and has been
 * initialized (data != NULL), 0 otherwise.
 */
int
Script_isArrayInit(int id)
{
	WombatArray *arr;

	arr = ArrayLookup(id);
	if (arr == NULL)
		return 0;
	if (!CArray_IsValid(arr))
		return 0;
	return 1;
}

/*
 * 0x0040F1CC - initArrayFromFile
 *
 * Opens a tab-delimited file. First line is a header with column
 * type names (parsed via GetTypeId). Remaining lines contain data
 * values. If width/height are <= 0, they are auto-detected from
 * the file. Creates the array, initializes it, then fills each
 * cell according to its column type (int, str, or ustr).
 */
void
Script_initArrayFromFile(int id, int width, int height, CString *filename)
{
	FILE *fp;
	char buf[4096];
	CList typeList;
	WombatArray *arr;
	int numCols, colTypes[256];
	int typeId;
	char *ptr;
	int tokLen;
	int row, col;
	CString tmpStr;
	CUString tmpUStr;
	unsigned short wbuf[512];

	fp = FileManager_OpenByType(0x31, CString_GetBuffer(filename), "r");
	if (fp == NULL) {
		sprintf(buf, "initArrayFromFile: Unable to open file '%s'.\n", CString_GetCStr2(filename));
		return;
	}

	CList_Constructor(&typeList);
	numCols = 0;

	// Read header line
	if (fgets_ServerSide(buf, 0xFFF, fp) == NULL) {
		numCols = 0;
		goto dimensions;
	}

	// Parse header tokens for column types
	ptr = buf;
	while (*ptr != '\0') {
		tokLen = strcspn(ptr, "\t\n\r");
		ptr[tokLen] = '\0';
		typeId = GetTypeId(ptr);
		colTypes[numCols] = typeId;
		if (typeId == 7)
			break;
		CList_Append(&typeList, typeId, 0);
		numCols++;
		if (numCols >= 256)
			break;
		ptr = ptr + tokLen + 1;
	}

dimensions:
	// Auto-detect width from header if not specified
	if (width <= 0)
		width = numCols;

	// Auto-detect height by counting data lines
	if (height <= 0) {
		height = -1;
		while (!feof_ServerSide(fp)) {
			if (fgets_ServerSide(buf, 0xFFF, fp) == NULL)
				break;
			height++;
		}
	}

	// Create array and initialize
	arr = ArrayCreate(id);
	if (CArray_Init(arr, width, height, &typeList) == 0) {
		fclose_ServerSide(fp);
		CList_Destructor(&typeList);
		return;
	}

	fseek_ServerSide(fp, 0, SEEK_SET);
	if (fgets_ServerSide(buf, 0xFFF, fp) == NULL)
		goto done;
	if (fgets_ServerSide(buf, 0xFFF, fp) == NULL)
		goto done;

	// Row loop
	for (row = 0; row < height; row++) {
		if (fgets_ServerSide(buf, 0xFFF, fp) == NULL)
			break;

		ptr = buf;
		for (col = 0; col < width; col++) {
			if (*ptr == '\0')
				break;
			tokLen = strcspn(ptr, "\t\n\r");
			ptr[tokLen] = '\0';
			if (tokLen <= 0)
				goto advance;

			switch (colTypes[col]) {
			case 0: // int
				CArray_SetIntElem(arr, col, row, atoi(ptr));
				break;
			case 1: // string
				CString_Constructor(&tmpStr, ptr);
				CArray_SetStrElem(arr, col, row, &tmpStr);
				CString_Destructor(&tmpStr);
				break;
			case 2: // unicode string
				Hex2Wchar(ptr, wbuf);
				CUString_Constructor(&tmpUStr, wbuf);
				CArray_SetUStrElem(arr, col, row, &tmpUStr);
				CUString_Destructor(&tmpUStr);
				break;
			default:
				goto advance;
			}
advance:
			ptr = ptr + tokLen + 1;
		}
	}

done:
	fclose_ServerSide(fp);
	CList_Destructor(&typeList);
}

/*
 * 0x0040F680 - initArray
 *
 * Creates or gets array by ID, then initializes it with the
 * given width, height, and column type list.
 */
void
Script_initArray(int id, int width, int height, CList *typeList)
{
	WombatArray *arr;

	arr = ArrayCreate(id);
	CArray_Init(arr, width, height, typeList);
}

/*
 * 0x0040F6AB - setArrayElems
 *
 * Walks a linked list of typed values, setting elements in a row.
 * Starting at column x, for each list node: if type 0 (int) calls
 * SetIntElem, if type 1 (str) calls SetStrElem, if type 2 (ustr)
 * calls SetUStrElem. Increments column index for each node.
 */
void
Script_setArrayElems(int id, int x, int y, CList *list)
{
	WombatArray *arr;
	CListNode *node;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr))
		return;

	node = list->head;
	while (node != NULL) {
		switch (node->typeTag) {
		case 0:
			CArray_SetIntElem(arr, x, y, node->value);
			break;
		case 1:
			CArray_SetStrElem(arr, x, y, (void *)(uintptr_t)node->value);
			break;
		case 2:
			CArray_SetUStrElem(arr, x, y, (void *)(uintptr_t)node->value);
			break;
		default:
			return;
		}
		x++;
		node = node->next;
	}
}

/*
 * 0x0040F76B - deleteArray
 *
 * Deletes the array with the given ID, freeing all data.
 */
void
Script_deleteArray(int id)
{
	ArrayDelete(id);
}

/*
 * 0x0040F77C - setArrayIntElem
 *
 * Looks up array by ID, validates it is initialized, then sets
 * the integer element at (x, y) to val.
 */
void
Script_setArrayIntElem(int id, int x, int y, int val)
{
	WombatArray *arr;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr))
		return;
	CArray_SetIntElem(arr, x, y, (uintptr_t)val);
}

/*
 * 0x0040F7BB - setArrayStrElem
 *
 * Looks up array by ID, validates it is initialized, then sets
 * the string element at (x, y) from the CString src.
 */
void
Script_setArrayStrElem(int id, int x, int y, CString *src)
{
	WombatArray *arr;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr))
		return;
	CArray_SetStrElem(arr, x, y, src);
}

/*
 * 0x0040F7FA - setArrayUStrElem
 *
 * Looks up array by ID, validates it is initialized, then sets
 * the unicode string element at (x, y) from the CUString src.
 */
void
Script_setArrayUStrElem(int id, int x, int y, CUString *src)
{
	WombatArray *arr;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr))
		return;
	CArray_SetUStrElem(arr, x, y, src);
}

/*
 * 0x0040F839 - getArrayIntElem
 *
 * Looks up array by ID, validates, returns integer element at (x, y).
 * Returns 0 if array not found or not initialized.
 */
int
Script_getArrayIntElem(int id, int x, int y)
{
	WombatArray *arr;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr))
		return 0;
	return (int)CArray_GetIntElem(arr, x, y);
}

/*
 * 0x0040F876 - getArrayStrElem
 *
 * Looks up array by ID. If not found or not initialized, returns
 * an empty CString. Otherwise gets the CString element at (x, y)
 * and copy-constructs it into retval. Returns empty CString if
 * element is NULL.
 */
CString *
Script_getArrayStrElem(CString *retval, int id, int x, int y)
{
	WombatArray *arr;
	void *elem;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr)) {
		CString_Constructor((CString *)retval, "");
		return retval;
	}
	elem = CArray_GetStrElem(arr, x, y);
	if (elem != NULL) {
		CString_CopyConstructor((CString *)retval, (CString *)elem);
	} else {
		CString_Constructor((CString *)retval, "");
	}
	return retval;
}

/*
 * 0x0040F90E - getArrayUStrElem
 *
 * Looks up array by ID. If not found or not initialized, returns
 * an empty CUString. Otherwise gets the CUString element at (x, y),
 * extracts its wchar data, and constructs a new CUString from it
 * into retval. Returns empty CUString if element is NULL.
 */
CUString *
Script_getArrayUStrElem(CUString *retval, int id, int x, int y)
{
	WombatArray *arr;
	void *elem;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr)) {
		CUString_Constructor((CUString *)retval, NULL);
		return retval;
	}
	elem = CArray_GetUStrElem(arr, x, y);
	if (elem != NULL) {
		CUString_Constructor(retval, (const void *)(uintptr_t)CUString_GetPtr((CUString *)elem));
	} else {
		CUString_Constructor((CUString *)retval, NULL);
	}
	return retval;
}

/*
 * 0x0040F9AC - getArrayHeight
 *
 * Returns the height (number of data rows) of array with given ID.
 * Returns 0 if not found or not initialized.
 */
int
Script_getArrayHeight(int id)
{
	WombatArray *arr;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr))
		return 0;
	return arr->height;
}

/*
 * 0x0040F9E1 - getArrayWidth
 *
 * Returns the width (number of columns) of array with given ID.
 * Returns 0 if not found or not initialized.
 */
int
Script_getArrayWidth(int id)
{
	WombatArray *arr;

	arr = ArrayLookup(id);
	if (arr == NULL || !CArray_IsValid(arr))
		return 0;
	return arr->width;
}

/*
 * Game API Handlers (0x0041030D..0x004111F2)
 *
 * Script-level handler functions for game operations: entity manipulation,
 * callbacks/timers, container queries, script attachment, and behavior
 * flags.
 *
 * Binary entity validation helpers at 0x0040DB67..0x0040DDEE call
 * CWorld_FindBySerial, check for NULL and deleted status, and optionally
 * check entity type via vtable.
 */

/*
 * VT_GetHeight - vtable[0x28] dispatch wrapper.
 */
int
VT_GetHeight(CItem *ent)
{
	return ((int (*)(void *))VT_FN(ent, VT_GET_HEIGHT))(ent);
}

/*
 * VT_GetFlags - emulate vtable[0x30] dispatch.
 * CItem (0x004322C0): tiledata flags = g_ItemTileData[bodyType + doorOffset].flags
 * CMobile (0x0044A7D0): returns 0.
 */
static int
VT_GetFlags(CItem *ent)
{
	uint16_t bodyType;
	int doorOffset;

	if (VT_IsMobile(ent))
		return 0;
	bodyType = ent->resourceEntity.entity.bodyType;
	doorOffset = (ent->itemFlags & ItemFlag_Open) ? 1 : 0;
	return (int)g_ItemTileData[bodyType + doorOffset].flags;
}

/*
 * VT_GetEffectiveHeight - emulate vtable[0x2C] dispatch.
 * CItem: height with bridge halving. Gets height via VT_GetHeight,
 * then if tiledata flags have bridge bit (0x400), divides by 2.
 */
static int
VT_GetEffectiveHeight(CItem *ent)
{
	int height;

	height = VT_GetHeight(ent);
	if (VT_GetFlags(ent) & 0x400)
		height /= 2;
	return height;
}

// VT_IsHair is provided by vtable.h macro (dispatches vtable[0x38]).

/*
 * VT_GetLocation - emulate vtable[0x80] dispatch.
 * CItem (0x0048A531): walks parent chain to root, returns &root.location.
 * If entity has no parent, returns &entity.location directly.
 */
static CLocation *
VT_GetLocation(CItem *ent)
{
	while (ent->parent != NULL)
		ent = ent->parent;
	return &ent->resourceEntity.entity.location;
}

/*
 * 0x0040FA16 - getCompileFlag
 *
 * Returns the compile-time feature flag for flagId. Flag 1 is
 * permanently set (UO Demo server) and every other flag returns 0.
 */
int
Script_getCompileFlag(int flagId)
{
	switch (flagId) {
	case 1:
		return 1;
	case 2:
		return 0;
	default:
		return 0;
	}
}

/*
 * 0x0040FA3F - GetDistanceInTiles_Internal
 *
 * Returns the Chebyshev distance between loc1 and loc2, applying
 * Felucca-map wrapping (5120 x 4096). Locations on different maps
 * return 9999.
 */
static int
GetDistanceInTiles_Internal(const CLocation *loc1, const CLocation *loc2)
{
	int map1, map2, dx, dy;

	// Check if on different maps (Felucca x < 0x1400 vs Trammel x >= 0x1400)
	map1 = (loc1->x >= 0x1400);
	map2 = (loc2->x >= 0x1400);
	if (map1 != map2)
		return 9999;

	// Absolute differences
	dx = loc1->x - loc2->x;
	if (dx < 0)
		dx = -dx;
	dy = loc1->y - loc2->y;
	if (dy < 0)
		dy = -dy;

	// World wrapping for Felucca (x < 0x1400)
	if (loc1->x < 0x1400) {
		if (dx > 0x1400 - dx)
			dx = 0x1400 - dx;
		if (dy > 0x1000 - dy)
			dy = 0x1000 - dy;
	}

	// Chebyshev distance = max(|dx|, |dy|)
	return (dx > dy) ? dx : dy;
}

/*
 * 0x0040FB2E - superTargetObj
 *
 * Sends a TARGET cursor to a player, or fires NPC target resolution
 * trigger events. For players (vtable[0x18]): clears targetCallback,
 * builds TARGET packet (type=0, object mode), sends to player.
 * For NPCs (vtable[0xE4]): reads "targetObj", "targetType", "targetLoc"
 * ObjVars, then fires trigger 0x18 (if targetObj exists) or 0x19
 * (location-only) via Entity_ExecuteEvent.
 */
void
Script_superTargetObj(uint32_t serial, uint32_t cursorId, int cursorType)
{
	CItem *ent;
	CPlayer *player;
	uint8_t buf[24];
	uint32_t targetObj;
	CLocation targetLoc;
	int targetType;
	CItem *callerEntity, *found;
	uint32_t callerSerial;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;

	if (VT_IsPlayer(ent)) {
		player = (CPlayer *)ent;
		player->targetCallback = NULL;
		PacketManager_MakePacket_TARGET(buf, 0, cursorId, (uint8_t)cursorType);
		SendToClient((CItem *)player, buf, -1);
		return;
	}

	if (!VT_IsNPC(ent))
		return;

	targetObj = 0;
	CLocation_Init(&targetLoc);
	targetType = 0;

	if (CResourceEntity_HasTag(ent, "targetObj", WTYPE_OBJ))
		CResourceEntity_GetTagObj(ent, "targetObj", &targetObj);

	if (CResourceEntity_HasTag(ent, "targetType", WTYPE_INT))
		CResourceEntity_GetTagInt(ent, "targetType", &targetType);

	if (CResourceEntity_HasTag(ent, "targetLoc", WTYPE_LOC))
		CResourceEntity_GetTagLoc(ent, "targetLoc", &targetLoc);

	callerEntity = GetCurrentThreadEntity();
	if (callerEntity != NULL)
		callerSerial = CMobile_GetSerial((CMobile *)callerEntity);
	else
		callerSerial = 0;

	if (targetObj != 0)
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x18, serial, targetObj);
	else
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x19, serial, &targetLoc, targetType);

	found = CWorld_FindBySerial(g_World, callerSerial);
	if (found != callerEntity)
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
}

/*
 * 0x0040FCD6 - sendToNearbyPlayers [117]
 *
 * Sends a message event to all nearby online players.
 * 164 bytes. Gets entity location, builds nearby player list within
 * range 18, then dispatches vtable[0x130] (SendToNearby) with flags.
 */
void
Script_sendToNearbyPlayers(uint32_t serial, int flags)
{
	CItem *ent;
	CLocation *rootLoc;
	CLocation localLoc;
	CVector list;
	char typeFlag = 0;

	ent = FindEntityValidated(serial, "sendToNearbyPlayers");
	if (ent == NULL)
		return;
	rootLoc = VT_GetLocation(ent);
	CLocation_SetLoc(&localLoc, rootLoc);
	CVector_Constructor(&list, &typeFlag);
	GetNearbyPlayers(&list, &localLoc, 18);
	((void (*)(CItem *, CVector *, int))VT_FN(ent, VT_NOTIFY_NEARBY))(ent, &list, flags);
	CVector_Destructor(&list);
}

/*
 * 0x0040FD7A - targetObj
 *
 * Thin wrapper: calls superTargetObj with cursorType=0.
 */
void
Script_targetObj(uint32_t serial, uint32_t cursorId)
{
	Script_superTargetObj(serial, cursorId, 0);
}

/*
 * 0x0040FD91 - superTargetLoc
 *
 * Same as superTargetObj but sends TARGET packet in location mode
 * (type=1) for the player path. NPC path is identical.
 * The multiId parameter (4th arg) is declared but unused by the binary.
 */
void
Script_superTargetLoc(uint32_t serial, uint32_t cursorId, int cursorType, int multiId)
{
	CItem *ent;
	CPlayer *player;
	uint8_t buf[24];
	uint32_t targetObj;
	CLocation targetLoc;
	int targetType;
	CItem *callerEntity, *found;
	uint32_t callerSerial;

	USED(multiId);

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;

	if (VT_IsPlayer(ent)) {
		player = (CPlayer *)ent;
		player->targetCallback = NULL;
		PacketManager_MakePacket_TARGET(buf, 1, cursorId, (uint8_t)cursorType);
		SendToClient((CItem *)player, buf, -1);
		return;
	}

	if (!VT_IsNPC(ent))
		return;

	targetObj = 0;
	CLocation_Init(&targetLoc);
	targetType = 0;

	if (CResourceEntity_HasTag(ent, "targetObj", WTYPE_OBJ))
		CResourceEntity_GetTagObj(ent, "targetObj", &targetObj);

	if (CResourceEntity_HasTag(ent, "targetType", WTYPE_INT))
		CResourceEntity_GetTagInt(ent, "targetType", &targetType);

	if (CResourceEntity_HasTag(ent, "targetLoc", WTYPE_LOC))
		CResourceEntity_GetTagLoc(ent, "targetLoc", &targetLoc);

	callerEntity = GetCurrentThreadEntity();
	if (callerEntity != NULL)
		callerSerial = CMobile_GetSerial((CMobile *)callerEntity);
	else
		callerSerial = 0;

	if (targetObj != 0)
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x18, serial, targetObj);
	else
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x19, serial, &targetLoc, targetType);

	found = CWorld_FindBySerial(g_World, callerSerial);
	if (found != callerEntity)
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
}

/*
 * 0x0040FF39 - targetLoc
 *
 * Thin wrapper: calls superTargetLoc with cursorType=0, multiId=0.
 */
void
Script_targetLoc(uint32_t serial, uint32_t cursorId)
{
	Script_superTargetLoc(serial, cursorId, 0, 0);
}

/*
 * 0x0040FF52 - targetLocMulti
 *
 * Sends a TARGET_MULTI cursor (packet 0x99) to a player for multi
 * placement, or fires NPC target resolution triggers. Same NPC path
 * as superTargetLoc. For players: clears targetCallback, builds
 * TARGET_MULTI packet with allowGround=1 and the multi parameters,
 * then sends to player.
 */
void
Script_targetLocMulti(uint32_t serial, uint32_t cursorId, int multiId, int xOff, int yOff, int facing)
{
	CItem *ent;
	CPlayer *player;
	uint8_t buf[32];
	uint32_t targetObj;
	CLocation targetLoc;
	int targetType;
	CItem *callerEntity, *found;
	uint32_t callerSerial;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;

	if (VT_IsPlayer(ent)) {
		player = (CPlayer *)ent;
		player->targetCallback = NULL;
		PacketManager_MakePacket_TARGET_MULTI(buf, 1, cursorId, (uint16_t)multiId, (uint16_t)xOff, (uint16_t)yOff, (uint16_t)facing);
		SendToClient((CItem *)player, buf, -1);
		return;
	}

	if (!VT_IsNPC(ent))
		return;

	targetObj = 0;
	CLocation_Init(&targetLoc);
	targetType = 0;

	if (CResourceEntity_HasTag(ent, "targetObj", WTYPE_OBJ))
		CResourceEntity_GetTagObj(ent, "targetObj", &targetObj);

	if (CResourceEntity_HasTag(ent, "targetType", WTYPE_INT))
		CResourceEntity_GetTagInt(ent, "targetType", &targetType);

	if (CResourceEntity_HasTag(ent, "targetLoc", WTYPE_LOC))
		CResourceEntity_GetTagLoc(ent, "targetLoc", &targetLoc);

	callerEntity = GetCurrentThreadEntity();
	if (callerEntity != NULL)
		callerSerial = CMobile_GetSerial((CMobile *)callerEntity);
	else
		callerSerial = 0;

	if (targetObj != 0)
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x18, serial, targetObj);
	else
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x19, serial, &targetLoc, targetType);

	found = CWorld_FindBySerial(g_World, callerSerial);
	if (found != callerEntity)
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
}

/*
 * 0x0041010A - targetLocObjList
 *
 * Sends a TARGET_OBJLIST cursor (packet 0xB4) to a player for multi
 * placement with an object type filter list, or fires NPC target
 * resolution triggers. Same NPC path as superTargetLoc.
 * For players: clears targetCallback, builds TARGET_OBJLIST packet
 * with allowGround=1 and the list of allowed type IDs, then sends.
 */
void
Script_targetLocObjList(uint32_t serial, uint32_t cursorId, int multiId, int xOff, int yOff, CList *list)
{
	CItem *ent;
	CPlayer *player;
	uint8_t buf[0x2018];
	uint32_t targetObj;
	CLocation targetLoc;
	int targetType;
	CItem *callerEntity, *found;
	uint32_t callerSerial;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;

	if (VT_IsPlayer(ent)) {
		player = (CPlayer *)ent;
		player->targetCallback = NULL;
		PacketManager_MakePacket_TARGET_OBJLIST(buf, 1, cursorId, (uint16_t)multiId, (uint16_t)xOff, (uint16_t)yOff, list);
		SendToClient((CItem *)player, buf, -1);
		return;
	}

	if (!VT_IsNPC(ent))
		return;

	targetObj = 0;
	CLocation_Init(&targetLoc);
	targetType = 0;

	if (CResourceEntity_HasTag(ent, "targetObj", WTYPE_OBJ))
		CResourceEntity_GetTagObj(ent, "targetObj", &targetObj);

	if (CResourceEntity_HasTag(ent, "targetType", WTYPE_INT))
		CResourceEntity_GetTagInt(ent, "targetType", &targetType);

	if (CResourceEntity_HasTag(ent, "targetLoc", WTYPE_LOC))
		CResourceEntity_GetTagLoc(ent, "targetLoc", &targetLoc);

	callerEntity = GetCurrentThreadEntity();
	if (callerEntity != NULL)
		callerSerial = CMobile_GetSerial((CMobile *)callerEntity);
	else
		callerSerial = 0;

	if (targetObj != 0)
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x18, serial, targetObj);
	else
		Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x19, serial, &targetLoc, targetType);

	found = CWorld_FindBySerial(g_World, callerSerial);
	if (found != callerEntity)
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
}

/*
 * 0x0041030D - addFrag
 *
 * Finds entity, checks IsNPC (vtable 0xe4) and not deleted,
 * then calls CNPC_AddFragment.
 */
void
Script_addFrag(uint32_t serial, CString *fragName)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	CNPC_AddFragment((CNPC *)ent, (CString *)fragName);
}

/*
 * 0x0041035E - removeFragment
 *
 * Identical to addFrag but calls CNPC_RemoveFragment.
 */
void
Script_removeFragment(uint32_t serial, CString *fragName)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	CNPC_RemoveFragment((CNPC *)ent, (CString *)fragName);
}

/*
 * 0x004103AF - hasScript
 *
 * Returns 1 when the entity identified by serial has scriptName
 * attached.
 */
int
Script_hasScript(uint32_t serial, CString *scriptName)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;
	return CItem_HasScript(ent, (CString *)scriptName);
}

/*
 * 0x004103EA - attachScript
 *
 * Finds entity, checks not deleted, calls Entity_AttachScript
 * with fireCreation=1. On failure (entity not found), logs error.
 */
void
Script_attachScript(uint32_t serial, CString *scriptName)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent != NULL && !ent->resourceEntity.entity.removedFromWorld) {
		Entity_AttachScript(ent, CString_GetData(scriptName), 1);
		return;
	}
	{
		CString logStr;
		CExecThread *thread;

		CString_DefaultConstructor(&logStr);
		CString_AssignCStr(&logStr, "Script \"");
		thread = ThreadList_GetCurrent(&g_activeThreadList);
		if (thread != NULL)
			CScriptString_Append((CScriptString *)thread, &logStr);
		CString_AppendCStr(&logStr, "\" tried to attach script \"");
		CString_ConcatCString(&logStr, (CString *)scriptName);
		CString_AppendCStr(&logStr, "\" to nonexistant object ");
		CString_ConcatUInt(&logStr, serial);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "script", "error", CString_GetBuffer(&logStr));
		CString_Destructor(&logStr);
	}
}

/*
 * 0x004104F6 - detachScript
 *
 * Validates entity via FindEntityValidated, then calls
 * ThreadList_DetachFromEntity to stop running threads for the
 * named script on this entity, then removes the script attachment
 * via CResourceEntity_RemoveScript.
 */
void
Script_detachScript(uint32_t serial, CString *scriptName)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "detachScript");
	if (ent == NULL)
		return;
	ThreadList_DetachFromEntity(&g_activeThreadList, ent, (CString *)scriptName);
	CResourceEntity_RemoveScript(ent, CString_GetData(scriptName));
}

/*
 * 0x0041053D - getcontents
 *
 * Clears the output list, finds entity, checks IsContainer (vtable
 * 0xd4) and not deleted. Walks CContainer.contents linked list via
 * spatialNext (+0x20), appending each child's serial to the list.
 */
void
Script_getcontents(CList *list, uint32_t serial)
{
	CItem *ent, *item;

	CList_Clear(list);
	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsMobile2(ent))
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	item = ((CContainer *)ent)->contents;
	while (item != NULL) {
		CList_Append(list, 4, item->serial);
		item = item->spatialNext;
	}
}

/*
 * 0x004105B1 - canHold
 *
 * Checks whether a container can hold an item. Validates container
 * via FindContainerValidated and item via FindEntityValidated (both
 * with NULL caller name), then calls vtable[0x1B4] (CanHold) with
 * (item, NULL). Returns 0 if either entity is invalid.
 */
int
Script_canHold(uint32_t containerSerial, uint32_t itemSerial)
{
	CItem *container;
	CItem *item;

	container = FindContainerValidated(containerSerial, NULL);
	item = FindEntityValidated(itemSerial, NULL);
	if (container == NULL)
		return 0;
	if (item == NULL)
		return 0;
	return ((int (*)(void *, void *, void *))VT_FN(container, VT_EQUIP_ITERATE))(container, item, NULL);
}

/*
 * 0x00410601 - getequipment
 *
 * Clears the output list, finds entity, checks IsMobile (vtable
 * 0xd0) and not deleted. Iterates 26 equipment slots at mob+0x290,
 * appending non-NULL item serials to the list.
 */
void
Script_getequipment(CList *list, uint32_t serial)
{
	CItem *ent;
	CMobile *mob;
	int i;

	CList_Clear(list);
	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsMobile(ent))
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	mob = (CMobile *)ent;
	for (i = 0; i < 26; i++) {
		if (mob->equipment[i] != NULL)
			CList_Append(list, 4, mob->equipment[i]->serial);
	}
}

/*
 * 0x0041068D - callback
 *
 * Schedules a callback event after delay seconds (converted to
 * server ticks via *4) on the entity, fired with type 5
 * (TIMER_EVENT_CALLBACK).
 */
void
Script_callback(uint32_t serial, int delay, int callbackId)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	ScheduleEvent(delay * 4, serial, 5, callbackId, 0);
}

/*
 * 0x004106D3 - hasCallback
 *
 * Returns 1 when the entity has a scheduled type-5 callback with
 * the given id.
 */
int
Script_hasCallback(uint32_t serial, int callbackId)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;
	return CEntity_HasTimerEx(ent, 5, callbackId);
}

/*
 * 0x00410710 - hasCallbackAdvanced
 *
 * Same as hasCallback but with caller-specified event type.
 */
int
Script_hasCallbackAdvanced(uint32_t serial, int eventType, int callbackId)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;
	return CEntity_HasTimerEx(ent, eventType, callbackId);
}

/*
 * 0x0041074F - shortcallback
 *
 * Same as callback but does NOT shift the delay.
 * Passes the raw delay directly to ScheduleEvent (no * 4).
 */
void
Script_shortcallback(uint32_t serial, int delay, int callbackId)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	ScheduleEvent(delay, serial, 5, callbackId, 0);
}

/*
 * 0x00410792 - callbackAdvanced
 *
 * Validates entity via FindEntityValidated, schedules event with
 * caller-specified event type and callback ID.
 */
void
Script_callbackAdvanced(uint32_t serial, int delay, int eventType, int callbackId)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "callBackAdvanced");
	if (ent == NULL)
		return;
	ScheduleEvent(delay, serial, eventType, callbackId, 0);
}

/*
 * 0x004107D0 - removeCallback
 *
 * Cancels the entity's scheduled type-5 callback with the given id.
 */
void
Script_removeCallback(uint32_t serial, int callbackId)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	CEntity_RemoveTimer(ent, 5, callbackId);
}

/*
 * 0x004107FD - removeCallbackAdvanced
 *
 * Validates entity via FindEntityValidated, removes scheduled
 * event with caller-specified event type and callback ID.
 */
void
Script_removeCallbackAdvanced(uint32_t serial, int eventType, int callbackId)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "removeCallbackAdvanced");
	if (ent == NULL)
		return;
	CEntity_RemoveTimer(ent, eventType, callbackId);
}

/*
 * 0x00410831 - selectType/selectTypeAndHue shared implementation
 *
 * Walks a CList extracting type IDs, optional hues, and optional
 * text names into an 8-byte entry array. Finds the player by serial,
 * builds an OBJPICKER packet (0x7C), and sends it.
 *
 * Entry layout: {uint16_t typeId (+0), uint16_t hue (+2), char *name (+4)}.
 * The list is consumed as: typeId node, [hue node if withHue], [name node
 * if string type]. Each entry consumes 1-3 list nodes. Max 256 entries.
 */
static void
selectTypeImpl(uint32_t playerSerial, uint32_t serial, uint32_t dialogId, CString *title, CList *list, uint32_t withHue)
{
	ObjPickerEntry entries[256];
	uint8_t buf[0x2014];     // packet buffer
	CListNode *node;
	CPlayer *player;
	int nodeCount; // dead: tracks consumed list nodes (var_80ch)
	int i;

	node = list->head;
	if (node == NULL) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}

	nodeCount = 0;
	for (i = 0; i < 256; i++) {
		// Type ID node (must be INT type = 0)
		if (node->typeTag != 0) {
			ThreadList_FinishCurrent(&g_activeThreadList, 0);
			return;
		}
		entries[i].typeId = (uint16_t)node->value;
		entries[i].hue = 0;
		entries[i].name = NULL;

		node = node->next;
		nodeCount++;
		if (node == NULL)
			break;

		// Hue node (if withHue)
		if (withHue) {
			if (node->typeTag != 0) {
				ThreadList_FinishCurrent(&g_activeThreadList, 0);
				return;
			}
			entries[i].hue = (uint16_t)node->value;
			node = node->next;
			nodeCount++;
			if (node == NULL)
				break;
		}

		// Name node (if STRING type = 1)
		if (node->typeTag == 1) {
			entries[i].name = CString_GetCStr((CString *)(uintptr_t)node->value);
			node = node->next;
			if (node == NULL)
				break;
			nodeCount++;
		}
	}

	i++; // count = i + 1 (binary does this after loop)

	USED(nodeCount);
	player = CPlayerList_FindBySerial(playerSerial);
	if (player == NULL) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}

	PacketManager_MakePacket_OBJPICKER(buf, serial, (uint16_t)dialogId, CString_GetCStr(title), (uint8_t)i, entries);
	SendToClient((CItem *)player, buf, -1);
}

/*
 * 0x00410A31 - selectType
 *
 * Wrapper for selectTypeImpl with withHue = 0.
 */
void
Script_selectType(uint32_t playerSerial, uint32_t serial, uint32_t dialogId, CString *title, CList *list)
{
	selectTypeImpl(playerSerial, serial, dialogId, title, list, 0);
}

/*
 * 0x00410A54 - selectTypeAndHue
 *
 * Wrapper for selectTypeImpl with withHue = 1.
 */
void
Script_selectTypeAndHue(uint32_t playerSerial, uint32_t serial, uint32_t dialogId, CString *title, CList *list)
{
	selectTypeImpl(playerSerial, serial, dialogId, title, list, 1);
}

/*
 * 0x00410A77 - selectHue
 *
 * Sends a Hue Picker packet (0x95) to a player. Finds the player
 * by serial via the player linked list. If not found, terminates
 * the script thread. Builds the 9-byte packet with the target
 * serial, type ID, and hue range, then sends to the player.
 */
void
Script_selectHue(uint32_t playerSerial, uint32_t serial, uint32_t typeID, uint32_t hue)
{
	CPlayer *player;
	uint8_t buf[12];

	player = CPlayerList_FindBySerial(playerSerial);

	if (player == NULL) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}

	PacketManager_MakePacket_HUEPICKER(buf, serial, (uint16_t)typeID, (uint16_t)hue);

	SendToClient((CItem *)player, buf, -1);
}

/*
 * 0x00410AD2 - DispatchMessage
 *
 * Internal helper for message/multimessage/messageret handlers.
 * Resolves target entity by serial, validates it exists and isn't
 * removed from world. Gets current thread entity for caller serial.
 * Fires event 0x16 via Entity_ExecuteEvent with caller serial,
 * message name, "x" signature, and list args. After the event,
 * verifies the caller entity still exists; if the pointer changed
 * (entity deleted during event), finishes the current thread.
 * Returns 0 if target not found, 1 otherwise.
 */
static int
DispatchMessage(uint32_t callerSerial, uint32_t targetSerial, CString *msgName, intptr_t listArgs)
{
	CItem *target, *callerEntity, *found;
	uint32_t callerSerial2;

	target = CWorld_FindBySerial(g_World, targetSerial);
	if (target == NULL)
		return 0;
	if (target->resourceEntity.entity.removedFromWorld)
		return 1;

	callerEntity = GetCurrentThreadEntity();
	if (callerEntity != NULL)
		callerSerial2 = CMobile_GetSerial((CMobile *)callerEntity);
	else
		callerSerial2 = 0;

	Entity_ExecuteEvent(&target->resourceEntity.entity, 0x16, callerSerial, CString_GetData(msgName), "x", listArgs);

	found = CWorld_FindBySerial(g_World, callerSerial2);
	if (found != callerEntity) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return 1;
	}

	return 1;
}

/*
 * 0x00410B89 - messageret
 *
 * Sends a message event (0x16) to a target entity and returns the result.
 * Looks up target by serial, validates it exists and isn't removed from
 * world. Gets current thread entity for caller serial (dead code in
 * binary - computed but never used). Fires event via Entity_ExecuteEvent
 * with sender serial, message name, "x" signature, and list args.
 * Returns Entity_ExecuteEvent's result, or 0 on failure.
 */
int
Script_messageret(uint32_t senderSerial, uint32_t targetSerial, CString *msgName, intptr_t listArgs)
{
	CItem *target, *callerEntity;
	uint32_t callerSerial;

	target = CWorld_FindBySerial(g_World, targetSerial);
	if (target == NULL)
		return 0;
	if (target->resourceEntity.entity.removedFromWorld)
		return 0;

	callerEntity = GetCurrentThreadEntity();
	if (callerEntity != NULL)
		callerSerial = CMobile_GetSerial((CMobile *)callerEntity);
	else
		callerSerial = 0;
	USED(callerSerial);

	return (int)(intptr_t)Entity_ExecuteEvent(&target->resourceEntity.entity, 0x16, senderSerial, CString_GetData(msgName), "x", listArgs);
}

/*
 * 0x00410C0B - message
 *
 * Sends a message event (0x16) to a target entity. Gets the current
 * thread, walks scriptRef->entity->serial to get the caller serial,
 * then calls DispatchMessage to fire the event.
 */
void
Script_message(uint32_t targetSerial, CString *msgName, intptr_t listArgs)
{
	CExecThread *thread;
	CItem *entity;
	uint32_t callerSerial;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread == NULL)
		return;

	entity = ((ScriptAttachNode *)thread->scriptRef)->entity;
	callerSerial = entity->serial;

	DispatchMessage(callerSerial, targetSerial, msgName, listArgs);
}

/*
 * 0x00410C4F - multimessage [160] / multiMessage [161]
 *
 * Sends a message to a target entity. Gets the current thread's entity
 * serial as caller. If serial is nonzero, tries DispatchMessage first
 * (fires event 0x16 on target). If serial is zero or DispatchMessage
 * returned 0, falls back to SendMultiMessage (0x0047D601) which
 * broadcasts to nearby entities.
 */
void
Script_multimessage(uint32_t serial, CString *msgName, intptr_t listArgs)
{
	CExecThread *thread;
	CItem *entity;
	uint32_t callerSerial;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread == NULL)
		return;

	entity = ((ScriptAttachNode *)thread->scriptRef)->entity;
	callerSerial = entity->serial;

	if (serial != 0) {
		if (DispatchMessage(callerSerial, serial, msgName, listArgs))
			return;
	}
	SendMultiMessage(serial, callerSerial, msgName, listArgs);
}

/*
 * 0x00410CB5 - multiMessageToLoc [162]
 *
 * Sends a message to entities at a specific location. Gets the
 * current thread's entity serial as caller, then calls
 * SendMultiMessageToLoc (0x0047DC1E).
 */
void
Script_multiMessageToLoc(CLocation *loc, CString *msgName, intptr_t listArgs)
{
	CExecThread *thread;
	CItem *entity;
	uint32_t callerSerial;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread == NULL)
		return;

	entity = ((ScriptAttachNode *)thread->scriptRef)->entity;
	callerSerial = entity->serial;

	SendMultiMessageToLoc(loc, callerSerial, msgName, listArgs);
}

/*
 * 0x00410CF9 - multiMessageToRange [163]
 *
 * Sends a message to entities within range of a location. Gets
 * the current thread's entity serial as caller, then calls
 * SendMultiMessageToRange (0x0047DD93).
 */
void
Script_multiMessageToRange(CLocation *loc, int range, CString *msgName, intptr_t listArgs)
{
	CExecThread *thread;
	CItem *entity;
	uint32_t callerSerial;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread == NULL)
		return;

	entity = ((ScriptAttachNode *)thread->scriptRef)->entity;
	callerSerial = entity->serial;

	SendMultiMessageToRange(loc, range, callerSerial, msgName, listArgs);
}

/*
 * 0x00410D41 - messageToRange
 *
 * Sends event 0x16 to all entities within Chebyshev distance of a
 * location. Gets nearby blocks via CBlockManager_GetNearbyBlocks,
 * collects mob serials per block into a CVector, then iterates
 * those serials checking distance. For each entity in range, fires
 * Entity_ExecuteEvent with the text/args and the current thread's
 * serial. After each event, verifies the sender entity still exists;
 * if destroyed during the event, finishes the current thread and
 * returns immediately.
 */
void
Script_messageToRange(CLocation *loc, int range, CString *text, CString *args)
{
	CExecThread *thread;
	uint32_t serial;
	int blockArray[0x400];
	int blockIdx;
	CVector list;
	char type;
	uintptr_t *iter;
	CItem *entity;
	CItem *sender;
	uint32_t senderSerial;
	uint32_t tempSerial;
	CItem *mob;
	CItem *senderCheck;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread == NULL)
		return;

	serial = ((CItem *)((ScriptAttachNode *)thread->scriptRef)->entity)->serial;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockArray, 0x400);

	for (blockIdx = 0; blockArray[blockIdx] != -1; blockIdx++) {
		type = 0;
		CVector_Constructor(&list, &type);

		// Walk entity chain in this block, collect serials
		mob = g_MapBlocks[blockArray[blockIdx]].itemHead;
		while (mob != NULL) {
			tempSerial = CMobile_GetSerial((CMobile *)mob);
			CVector_PushBack(&list, tempSerial);
			mob = mob->spatialNext;
		}

		// Iterate over collected serials
		iter = (uintptr_t *)list.begin;
		while (iter != (uintptr_t *)list.end) {
			entity = CWorld_FindBySerial(g_World, (uint32_t)*iter);
			if (entity != NULL) {
				if (CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&entity->resourceEntity.entity)) < range) {
					sender = GetCurrentThreadEntity();
					senderSerial = sender ? CMobile_GetSerial((CMobile *)sender) : 0;

					Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x16, serial, CString_GetData(text), "x", args);

					// Check if sender was destroyed
					senderCheck = CWorld_FindBySerial(g_World, senderSerial);
					if (senderCheck != sender) {
						ThreadList_FinishCurrent(&g_activeThreadList, 0);
						CVector_Destructor(&list);
						return;
					}
				}
			}
			iter++;
		}

		CVector_Destructor(&list);
	}
}

/*
 * 0x00410FA2 - changeLoc
 *
 * Adds (dx, dy, dz) to loc in place.
 */
void
Script_changeLoc(CLocation *loc, int dx, int dy, int dz)
{
	loc->x += (int16_t)dx;
	loc->y += (int16_t)dy;
	loc->z += (int16_t)dz;
}

/*
 * 0x00410FDB - NULL handler
 *
 * Returns 0.
 */
int
Script_nullHandler(void)
{
	return 0;
}

/*
 * 0x00410FE2 - copylist
 *
 * Replaces the contents of dst with a shallow copy of every entry
 * from src.
 */
void
Script_copylist(CList *dst, CList *src)
{
	CListNode *cur;

	CList_Clear(dst);

	cur = src->head;
	while (cur != NULL) {
		CList_Append(dst, cur->typeTag, cur->value);
		cur = cur->next;
	}
}

// CString_GetLength and CString_CharAt are in string.h

/*
 * 0x00411022 - printList_delayLoop
 *
 * Cdecl, 1 arg (count). Simple busy-wait loop that decrements count
 * until it reaches zero. No-op in practice (used by printList for
 * indentation timing).
 */
static void
printList_delayLoop(int count)
{
	while (count != 0)
		count--;
}

/*
 * 0x0041103A - printList recursive helper
 *
 * Walks list, recursing into sublists at depth+1. Produces no
 * output: each node only triggers a no-op delay loop.
 */
static void
printList_recursive(CList *list, int depth)
{
	CListNode *cur;
	int d;

	d = 0;
	cur = list->head;
	while (cur != NULL) {
		printList_delayLoop(depth);

		switch ((int)cur->typeTag) {
		case 0:
			break;
		case 5:
			printList_recursive((CList *)(uintptr_t)cur->value, depth + 1);
			break;
		}

		d++;
		cur = cur->next;
	}
	USED(d);
}

/*
 * 0x004110A9 - printList
 *
 * Entry point for the (no-op) printList walker, starting at depth 0.
 */
void
Script_printList(CList *list)
{
	printList_recursive(list, 0);
}

/*
 * Sort comparators for Script_sortList.
 */

/*
 * 0x004110BC - CalcDirection
 *
 * Returns the 8-direction compass code (0=N, 1=NE, ... 7=NW) for
 * the heading from loc1 to loc2. Felucca-map wrapping selects the
 * shorter dx/dy path, and the octant boundaries use a 2:1 ratio.
 */
int
CalcDirection(const CLocation *loc1, const CLocation *loc2)
{
	int dx, dy;

	dx = loc2->x - loc1->x;
	dy = loc2->y - loc1->y;

	// Felucca wrapping: use shorter path
	if (loc1->x < 0x1400) {
		if (dx > 0x1400 - dx)
			dx = 0x1400 - dx;
		if (dy > 0x1000 - dy)
			dy = 0x1000 - dy;
	}

	if (dy <= 0) {
		int ady = -dy;
		if (abs(dx) * 2 <= ady)
			return 0; /* N */
		if (abs(dx) >= ady * 2)
			return (dx >= 0) ? 2 : 6; /* E or W */
		return (dx >= 0) ? 1 : 7;         /* NE or NW */
	} else {
		if (abs(dx) * 2 <= dy)
			return 4; /* S */
		if (abs(dx) >= dy * 2)
			return (dx >= 0) ? 2 : 6; /* E or W */
		return (dx >= 0) ? 3 : 5;         /* SE or SW */
	}
}

/*
 * Direction delta tables for the 8-direction UO compass:
 * N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7.
 */
const int16_t g_DirDeltaX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
const int16_t g_DirDeltaY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

/*
 * 0x004111D7 - setConvoRet
 *
 * Copies str into the conversation return buffer.
 */
void
Script_setConvoRet(CString *str)
{
	char *s = CString_GetData(str);
	strcpy(g_ConvoReturnStr, s);
}

/*
 * 0x004111F2 - disableBehaviors
 *
 * Sets every behavior flag (0x0001003F) on the named NPC, disabling
 * its automatic AI behaviors.
 */
void
Script_disableBehaviors(uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	((CNPC *)ent)->behaviorFlags = 0x1003F;
}

/*
 * 0x0041123C - enableBehaviors
 *
 * Clears every behavior flag (0x0001003F) on the named NPC,
 * restoring automatic AI behaviors.
 */
void
Script_enableBehaviors(uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	((void (*)(void *, int))VT_FN(ent, VT_CLR_BEHAVIOR))(ent, 0x1003F);
}

/*
 * 0x0041128C - areBehaviorsEnabled
 *
 * Returns 1 when the NPC's behaviors are active (the disable bit
 * 0x08 is clear).
 */
int
Script_areBehaviorsEnabled(uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (!VT_IsNPC(ent))
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;
	return ((int (*)(void *, int))VT_FN(ent, VT_TEST_BEHAVIOR))(ent, 8);
}

/*
 * 0x004112DD - callGuards [190]
 *
 * Summons guards toward loc within range, attributed to the named
 * mobile, with no specific target.
 */
void
Script_callGuards(uint32_t serial, CLocation *loc, int range)
{
	CItem *mob;

	mob = FindMobileValidated(serial, "callGuards");
	if (mob == NULL)
		return;
	CombatManager_CallGuards(mob, loc, NULL, range);
}

/*
 * 0x00411319 - Script_checkEntity
 *
 * Resolves serial to a non-removed entity and runs checker against
 * it. Returns 0 when the entity is missing or removed.
 */
static int
Script_checkEntity(uint32_t serial, int (*checker)(CItem *), const char *name)
{
	CItem *ent;
	char buf[256];

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL || ent->resourceEntity.entity.removedFromWorld) {
		sprintf(buf, "%s: tried to check invalid object.\n", name ? name : "(null)");
		return 0;
	}
	return checker(ent);
}

/*
 * 0x00411389 - Script_checkMobile
 *
 * Resolves serial to a non-removed mobile and runs checker against
 * it. Returns 0 for non-mobiles or removed entities.
 */
static int
Script_checkMobile(uint32_t serial, int (*checker)(CItem *), const char *name)
{
	CItem *ent;
	char buf[256];

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL || !VT_IsMobile(ent) || ent->resourceEntity.entity.removedFromWorld) {
		sprintf(buf, "%s: tried to check invalid mobile.\n", name ? name : "(null)");
		return 0;
	}
	return checker(ent);
}

// Type check checker functions for vtable dispatch. Each is a 6-byte
// mov eax, [ecx]; jmp [eax + vt_offset] thunk emitted so a function
// pointer to the virtual method can be passed to Script_checkEntity /
// Script_checkMobile.

int
check_IsMobile(CItem *ent)
{
	return VT_IsMobile(ent);
} // 0x00424370
static int
check_IsDead(CItem *ent)
{
	return VT_IsDead(ent);
} // 0x00424380
static int
check_IsContainer(CItem *ent)
{
	return VT_IsMobile2(ent);
} // 0x00424390
static int
check_IsRealContainer(CItem *ent) // 0x004243B0
{
	return ((int (*)(void *))VT_FN(ent, VT_HAS_ACCESSIBLE_CONTENTS))(ent);
}
static int
check_IsPlayer(CItem *ent)
{
	return VT_IsPlayer(ent);
}
static int
check_IsMap(CItem *ent)
{
	return VT_IsSpatial(ent);
} // 0x004243E0
static int
check_IsSpellbook(CItem *ent) // 0x004243F0
{
	return ((int (*)(void *))VT_FN(ent, VT_EXCLUDED_AMOUNT))(ent);
}
static int
check_IsInContainer(CItem *ent) // 0x00424410
{
	return ((int (*)(void *))VT_FN(ent, VT_HAS_CONTAINER))(ent);
}
static int
check_IsNPC(CItem *ent)
{
	return VT_IsNPC(ent);
} // 0x00424420
static int
check_IsEquipped(CItem *ent)
{
	return VT_IsEquipped(ent);
} // 0x00424430
static int
check_IsShopkeeper(CItem *ent)
{
	return VT_IsVendor(ent);
} // 0x00424440
static int
check_IsGuard(CItem *ent) // 0x00424450
{
	return ((int (*)(void *))VT_FN(ent, VT_CHECK_EC))(ent);
}

/*
 * 0x0041140B - Script_checkContainer
 *
 * Resolves serial to a non-removed container and runs checker against
 * it. Returns 0 for non-containers or removed entities. Like its
 * checkMobile and checkPlayer siblings, the complaint is formatted into
 * a stack buffer and never printed.
 */
static __attribute__((unused)) int
Script_checkContainer(uint32_t serial, int (*checker)(CItem *), const char *name)
{
	CItem *ent;
	char buf[256];

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL || !VT_IsMobile2(ent) || ent->resourceEntity.entity.removedFromWorld) {
		sprintf(buf, "%s: tried to check invalid container.\n", name ? name : "(null)");
		return 0;
	}
	return checker(ent);
}

/*
 * 0x0041148D - Script_checkPlayer
 *
 * Resolves serial to a non-removed player and runs checker against
 * it. Returns 0 when serial does not refer to a live player.
 */
static int
Script_checkPlayer(uint32_t serial, int (*checker)(CItem *), const char *name)
{
	CItem *ent;
	char buf[256];

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		goto invalid;
	if (!VT_IsPlayer(ent))
		goto invalid;
	if (ent->resourceEntity.entity.removedFromWorld)
		goto invalid;
	return checker(ent);
invalid:
	sprintf(buf, "%s: tried to check invalid player.\n", name ? name : "(null)");
	return 0;
}

/*
 * 0x0041150C - seance
 *
 * Sets or clears the player's SpiritSpeak flag based on mode.
 */
void
Script_seance(uint32_t serial, int mode)
{
	CItem *ent;
	CPlayer *pl;

	ent = FindPlayerValidated(serial, "seance");
	if (ent == NULL)
		return;
	pl = (CPlayer *)ent;
	if (mode != 0)
		pl->pflags |= 0x0400;
	else
		pl->pflags &= ~0x0400u;
}

/*
 * 0x00411560 - hasObjVar
 *
 * Returns 1 when the entity has an ObjVar with the given name
 * (any type).
 */
int
Script_hasObjVar(uint32_t serial, CString *varname)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "hasObjVar");
	if (ent == NULL)
		return 0;
	return CResourceEntity_HasTag(ent, CString_GetData(varname), 7);
}

/*
 * 0x00411599 - hasObjListVar
 *
 * Returns 1 when the entity has a list-typed ObjVar with the given
 * name.
 */
int
Script_hasObjListVar(uint32_t serial, CString *varname)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "hasObjListVar");
	if (ent == NULL)
		return 0;
	return CResourceEntity_HasTag(ent, CString_GetData(varname), 5);
}

/*
 * 0x004115D2 - getObjVar (universal variant)
 *
 * Compile-time placeholder; the script compiler rewrites calls to
 * one of the typed getObjVar variants before execution.
 */
int
Script_getObjVar(uint32_t serial, CString *varname)
{
	USED(serial);
	CString_Assign(varname, varname);
	return 0;
}

/*
 * 0x004115EB - getObjVar (int variant)
 *
 * Returns the int ObjVar named varname on the entity, or 0 when
 * absent.
 */
int
Script_getObjVar_int(uint32_t serial, CString *varname)
{
	CItem *ent;
	int outVal;

	ent = FindEntityValidated(serial, "getObjVar");
	if (ent == NULL)
		return 0;
	if (!CResourceEntity_HasTag(ent, CString_GetData(varname), 0))
		return 0;
	CResourceEntity_GetTagInt(ent, CString_GetData(varname), &outVal);
	return outVal;
}

/*
 * 0x00411642 - getObjVar (string variant)
 *
 * Copies the string ObjVar named varname into retbuf (as a hidden
 * return-by-value parameter), or stores an empty string when the
 * ObjVar is absent. Returns retbuf.
 */
CString *
Script_getObjVar_str(CString *retbuf, uint32_t serial, CString *varname)
{
	CItem *ent;
	const char *name;
	CString *result;
	CString tmpStr;

	ent = FindEntityValidated(serial, "getObjVar");
	if (ent == NULL)
		goto not_found;
	name = CString_GetData(varname);
	if (!CResourceEntity_HasTag(ent, name, 1))
		goto not_found;
	name = CString_GetData(varname);
	result = CResourceEntity_GetTagString(ent, name);
	CString_CopyConstructor(retbuf, result);
	return retbuf;

not_found:
	CString_Constructor(&tmpStr, "");
	CString_CopyConstructor(retbuf, &tmpStr);
	CString_Destructor(&tmpStr);
	return retbuf;
}

/*
 * 0x00411709 - getObjVar (location variant)
 *
 * Copies the location ObjVar named varname into retbuf, falling
 * back to (-1, -1, 0) when the ObjVar is absent. Returns retbuf.
 */
CLocation *
Script_getObjVar_loc(CLocation *retbuf, uint32_t serial, CString *varname)
{
	CItem *ent;
	CLocation localVar;
	CLocation tmpLoc;

	ent = FindEntityValidated(serial, "getObjVar");
	if (ent == NULL)
		goto not_found;
	if (!CResourceEntity_HasTag(ent, CString_GetData(varname), 3))
		goto not_found;
	CLocation_Init(&localVar);
	CResourceEntity_GetTagLoc(ent, CString_GetData(varname), &localVar);
	CLocation_SetLoc(retbuf, &localVar);
	return retbuf;

not_found:
	CLocation_Init(&tmpLoc);
	CLocation_Set(&tmpLoc, -1, -1, 0);
	CLocation_SetLoc(retbuf, &tmpLoc);
	return retbuf;
}

/*
 * 0x00411797 - getObjVar (object variant)
 *
 * Returns the object-serial ObjVar named varname on the entity, or
 * 0 when absent.
 */
uint32_t
Script_getObjVar_obj(uint32_t serial, CString *varname)
{
	CItem *ent;
	uint32_t outVal;

	ent = FindEntityValidated(serial, "getObjVar");
	if (ent == NULL)
		return 0;
	if (!CResourceEntity_HasTag(ent, CString_GetData(varname), 4))
		return 0;
	CResourceEntity_GetTagObj(ent, CString_GetData(varname), &outVal);
	return outVal;
}

/*
 * 0x004117EE - getObjListVar
 *
 * Replaces list with the contents of the named list-typed ObjVar.
 * Aborts the current thread when the ObjVar is missing.
 */
void
Script_getObjListVar(CList *list, uint32_t serial, CString *varname)
{
	CItem *ent;
	CList *srcList;
	CListNode *ln;

	ent = CWorld_FindBySerial(g_World, serial);
	srcList = CResourceEntity_GetTagEntity(ent, CString_GetData(varname));
	CList_Clear(list);
	if (srcList == NULL) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}
	for (ln = srcList->head; ln != NULL; ln = ln->next)
		CList_Append(list, ln->typeTag, ln->value);
}

/*
 * 0x00411869 - setObjVar
 *
 * Stores (typeTag, value) under varname on the entity. Skipped
 * silently when the entity is missing or removed.
 */
void
Script_setObjVar(uint32_t serial, CString *varname, int typeTag, uintptr_t value)
{
	CItem *ent;
	const char *name;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	name = CString_GetData(varname);
	CEntity_SetObjVar(ent, name, typeTag, value);
}

/*
 * 0x004118AF - removeObjVar
 *
 * Removes the named ObjVar from the entity. No-op when the entity
 * is missing or removed.
 */
void
Script_removeObjVar(uint32_t serial, CString *varname)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (ent->resourceEntity.entity.removedFromWorld)
		return;
	CResourceEntity_DetachScript(ent, CString_GetData(varname));
}

/*
 * 0x004118ED - copyObjVar
 *
 * Copies the named ObjVar from src onto dest. No-op when either
 * entity is invalid or src and dest are the same.
 */
void
Script_copyObjVar(uint32_t destSerial, uint32_t srcSerial, CString *varname)
{
	CItem *dest, *src;

	dest = FindEntityValidated(destSerial, "copyobjvar (dest)");
	src = FindEntityValidated(srcSerial, "copyobjvar (source)");
	if (dest == NULL || src == NULL)
		return;
	if (dest == src)
		return;
	CItem_CopyObjVar(dest, src, CString_GetBuffer(varname), NULL);
}

/*
 * 0x0041194E - copyAllObjVars
 *
 * Copies every ObjVar from src onto dest. No-op when either entity
 * is invalid or src and dest are the same.
 */
void
Script_copyAllObjVars(uint32_t destSerial, uint32_t srcSerial)
{
	CItem *dest, *src;
	CVector vec;
	char typeFlag = 0;
	uintptr_t *iter;

	dest = FindEntityValidated(destSerial, "copyobjvar (dest)");
	src = FindEntityValidated(srcSerial, "copyobjvar (source)");
	if (dest == NULL || src == NULL)
		return;
	if (dest == src)
		return;
	CVector_Constructor(&vec, &typeFlag);
	CItem_GetTagDefListRaw(src, &vec);
	for (iter = (uintptr_t *)vec.begin; iter != (uintptr_t *)vec.end; iter++) {
		TagNode *node = (TagNode *)*iter;
		CEntity_SetObjVar(dest, node->name, node->type, node->value);
	}
	CVector_Destructor(&vec);
}

/*
 * 0x00411A2C - addToObjVarListSet
 *
 * Appends (typeTag, value) to the list-typed ObjVar named varname,
 * unless the value is already present. Returns 1 on insert,
 * 0 otherwise.
 */
int
Script_addToObjVarListSet(uint32_t serial, CString *varname, int typeTag, uintptr_t value)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;
	return CResourceEntity_AddToTagList(ent, CString_GetBuffer(varname), typeTag, value);
}

/*
 * 0x00411A74 - isInObjVarListSet
 *
 * Returns 1 when (typeTag, value) appears in the list-typed ObjVar
 * named varname.
 */
int
Script_isInObjVarListSet(uint32_t serial, CString *varname, int typeTag, uintptr_t value)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;
	return CResourceEntity_IsInTagList(ent, CString_GetBuffer(varname), typeTag, value);
}

/*
 * 0x00411ABC - setLastValidTerrainLoc
 *
 * Stores loc as the player's lastValidLocation, after confirming
 * the coordinates are within the map.
 */
void
Script_setLastValidTerrainLoc(uint32_t serial, CLocation *loc)
{
	CItem *ent;
	CPlayer *pl;

	ent = FindPlayerValidated(serial, "setLastValidTerrainLoc");
	if (ent == NULL)
		return;
	if (!CBlockManager_IsValidCoordAbsolute(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return;
	pl = (CPlayer *)ent;
	CLocation_SetLoc(&pl->lastValidLocation, loc);
}

/*
 * 0x00411B11 - teleport
 *
 * Moves the entity (and any contained items) to loc. Returns 0 on
 * an invalid destination, a removed entity, or a container that has
 * an active lock owner. For players, clears the bankOpenLoc ObjVar.
 */
int
Script_teleport(uint32_t serial, CLocation *loc)
{
	CItem *ent;
	CVector vec;
	char typeFlag = 0;

	ent = CWorld_FindBySerial(g_World, serial);
#ifdef DEBUG_TELEPORT
	fprintf(stderr, "TELEPORT: serial=0x%08X ent=%p dest=(%d,%d,%d)\n", serial, (void *)ent, (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)(int16_t)loc->z);
#endif
	if (ent == NULL)
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;

	if (VT_IsPlayer(ent)) {
		if (CResourceEntity_HasTag(ent, "bankOpenLoc", 3))
			CResourceEntity_DetachScript(ent, "bankOpenLoc");
	}

	// lockOwner (0x54) set: refuse
	if (VT_IsMobile2(ent) && ((CContainer *)ent)->lockOwner != NULL)
		return 0;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;

	g_bNoSpatialUpdate = 1;

	CVector_Constructor(&vec, &typeFlag);
	CItem_GetContainerItems(ent, &vec);
	((void (*)(void *, CLocation *))VT_FN(ent, VT_MOVE_TO))(ent, loc);
	CBlockManager_RestoreItems(&g_SpatialGrid, &vec);

	g_bNoSpatialUpdate = 0;
	CVector_Destructor(&vec);

	return 1;
}

/*
 * 0x00411C42 - teleportNoFall
 *
 * Like teleport, but skips the post-move spatial notifications.
 */
int
Script_teleportNoFall(uint32_t serial, CLocation *loc)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (ent->resourceEntity.entity.removedFromWorld)
		return 0;

	if (VT_IsPlayer(ent)) {
		if (CResourceEntity_HasTag(ent, "bankOpenLoc", 3))
			CResourceEntity_DetachScript(ent, "bankOpenLoc");
	}

	// lockOwner (0x54) set: refuse
	if (VT_IsMobile2(ent) && ((CContainer *)ent)->lockOwner != NULL)
		return 0;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;

	g_bNoSpatialUpdate = 1;
	((void (*)(void *, CLocation *))VT_FN(ent, VT_MOVE_TO))(ent, loc);
	g_bNoSpatialUpdate = 0;

	return 1;
}

/*
 * 0x00411D0E - random
 *
 * Returns a random integer in [min, max] inclusive, or 0 when
 * max < min.
 */
int
Script_random(int min, int max)
{
	return GetRandomRange(min, max);
}

/*
 * 0x00411D25 - dice
 *
 * Rolls numDice dice with numSides sides each and returns the sum.
 */
int
Script_dice(int numDice, int numSides)
{
	return CRandom_RollDice(numDice, numSides);
}

/*
 * 0x00411D3C - deleteObject
 *
 * Deletes the entity, refreshing nearby clients. Players are
 * protected: a player serial is silently ignored.
 */
void
Script_deleteObject(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "deleteObject");
	if (ent == NULL)
		return;

	// vtable[0xD0] && vtable[0x18]: protect players from deletion
	if (VT_IsMobile(ent) && VT_IsPlayer(ent))
		return;

	{
		CVector contItems;
		char typeFlag = 0;

		CVector_Constructor(&contItems, &typeFlag);

		if (!((int (*)(void *))VT_FN(ent, VT_HAS_CONTAINER))(ent))
			CItem_GetContainerItems(ent, &contItems);

		if (ent != NULL)
			((void (*)(void *))VT_FN(ent, VT_DELETE))(ent);

		CBlockManager_RestoreItems(&g_SpatialGrid, &contItems);
		CVector_Destructor(&contItems);
	}
}

/*
 * 0x00411E09 - deleteObjectNoFall
 *
 * Like deleteObject, but skips the post-delete spatial notifications.
 */
void
Script_deleteObjectNoFall(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "deleteObject");
	if (ent == NULL)
		return;

	// vtable[0xD0] && vtable[0x18]: protect players from deletion
	if (VT_IsMobile(ent) && VT_IsPlayer(ent))
		return;

	// Redundant NULL check (binary at 0x00411E4C)
	if (ent == NULL)
		return;

	((void (*)(void *))VT_FN(ent, VT_DELETE))(ent);
}

/*
 * 0x00411E64 - putObjContainer
 *
 * Moves the item into the named container. Refuses mobiles, locked
 * containers, and self-containment. Returns 1 on success, 0 on a
 * rejected move.
 */
int
Script_putObjContainer(uint32_t thingSerial, uint32_t containerSerial)
{
	CItem *thing, *container, *p;

	thing = FindEntityValidated(thingSerial, "putObjContainer (thing)");
	if (thing == NULL)
		return 0;

	// can't put mobiles in containers
	if (VT_IsMobile(thing))
		return 0;

	// lockOwner (0x54) set: refuse
	if (VT_IsMobile2(thing) && ((CContainer *)thing)->lockOwner != NULL)
		return 0;

	p = thing->parent;
	while (p != NULL) {
		if (p == thing)
			return 0;
		p = p->parent;
	}

	container = FindContainerValidated(containerSerial, "putObjContainer (container)");
	if (container == NULL)
		return 0;

	{
		CLocation tmpLoc;
		CVector contItems;
		char typeFlag = 0;

		CLocation_Init(&tmpLoc);
		CVector_Constructor(&contItems, &typeFlag);

		if (!((int (*)(void *))VT_FN(thing, VT_HAS_CONTAINER))(thing))
			CItem_GetContainerItems(thing, &contItems);

		((void (*)(void *))VT_FN(thing, VT_HIDE))(thing);

		CLocation_Set(&tmpLoc, -1, -1, 0);

		((void (*)(void *, CItem *, CLocation *))VT_FN(thing, VT_ADD_TO_CONTAINER))(thing, container, &tmpLoc);

		CBlockManager_RestoreItems(&g_SpatialGrid, &contItems);
		CVector_Destructor(&contItems);
	}

	return 1;
}

/*
 * 0x00411FC4 - toMobile
 *
 * Equips the item on the target mobile, applying the same checks
 * as putObjContainer. Returns 1 on success, 0 on a rejected move.
 */
int
Script_toMobile(uint32_t thingSerial, uint32_t mobileSerial)
{
	CItem *thing, *mobile, *p;

	thing = FindEntityValidated(thingSerial, "toMobile (thing)");
	if (thing == NULL)
		return 0;

	// can't equip mobiles
	if (VT_IsMobile(thing))
		return 0;

	// lockOwner (0x54) set: refuse
	if (VT_IsMobile2(thing) && ((CContainer *)thing)->lockOwner != NULL)
		return 0;

	p = thing->parent;
	while (p != NULL) {
		if (p == thing)
			return 0;
		p = p->parent;
	}

	mobile = FindMobileValidated(mobileSerial, "toMobile (mobile)");
	if (mobile == NULL)
		return 0;

	{
		CVector contItems;
		char typeFlag = 0;

		CVector_Constructor(&contItems, &typeFlag);

		if (!((int (*)(void *))VT_FN(thing, VT_HAS_CONTAINER))(thing))
			CItem_GetContainerItems(thing, &contItems);

		((void (*)(void *))VT_FN(thing, VT_HIDE))(thing);

		((void (*)(void *, CMobile *))VT_FN(thing, VT_ADD_TO_EQUIP))(thing, (CMobile *)mobile);

		CBlockManager_RestoreItems(&g_SpatialGrid, &contItems);
		CVector_Destructor(&contItems);
	}

	return 1;
}

/*
 * 0x00412107 - putMobContainer
 *
 * Moves a mobile into another mobile's container without the safety
 * checks performed by putObjContainer.
 */
int
Script_putMobContainer(uint32_t thingSerial, uint32_t containerSerial)
{
	CItem *thing, *container;

	thing = FindMobileEntityValidated(thingSerial, "putMobContainer (thing)");
	if (thing == NULL)
		return 0;

	container = FindMobileEntityValidated(containerSerial, "putMobContainer (container)");
	if (container == NULL)
		return 0;

	{
		CLocation tmpLoc;
		CLocation_Init(&tmpLoc);

		((void (*)(void *))VT_FN(thing, VT_HIDE))(thing);

		CLocation_Set(&tmpLoc, -1, -1, 0);
		((void (*)(void *, CItem *, CLocation *))VT_FN(thing, VT_ADD_TO_CONTAINER))(thing, container, &tmpLoc);
	}

	return 1;
}

/*
 * 0x00412189 - equipObj
 *
 * Equips the item on the target mobile at the given layer. On
 * failure the item is returned to its tracked location; on success
 * any nearby clients are notified.
 */
int
Script_equipObj(uint32_t itemSerial, uint32_t targetSerial, int layer)
{
	CItem *item;
	CItem *target;
	uint8_t layerByte;
	int result;
	CVector vec;
	char typeFlag = 0;

	item = CWorld_FindBySerial(g_World, itemSerial);
	target = CWorld_FindBySerial(g_World, targetSerial);

	if (target == NULL || !VT_IsMobile(target))
		return 0;
	if (item == NULL)
		return 0;

	layerByte = (uint8_t)layer;

	CVector_Constructor(&vec, &typeFlag);

	if (!((int (*)(void *))VT_FN(item, VT_HAS_CONTAINER))(item))
		CItem_GetContainerItems(item, &vec);

	((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	result = ((int (*)(CItem *, CMobile *, int))VT_FN(item, VT_EQUIP_ON_MOBILE))(item, (CMobile *)target, layerByte);

	if (result == 0) {
		((void (*)(void *))VT_FN(item, VT_RETURN_TO_TRACKED))(item);
	} else {
		// Equip succeeded: notify nearby entities
		CBlockManager_RestoreItems(&g_SpatialGrid, &vec);
	}

	CVector_Destructor(&vec);

	return result;
}

/*
 * 0x00412291 - dropObj
 *
 * Wrapper around Script_teleport that drops the item at loc.
 */
int
Script_dropObj(uint32_t serial, CLocation *loc)
{
	return Script_teleport(serial, loc);
}

/*
 * 0x004122A6 - walk [215]
 *
 * Steps the mobile one tile in dir (0..7). Returns 1 when the move
 * succeeds, 0 when the direction is invalid or WalkCheck refuses.
 */
int
Script_walk(uint32_t serial, int dir)
{
	CItem *mob;

	if (dir < 0 || dir > 7)
		return 0;
	mob = FindMobileValidated(serial, "walk");
	if (mob == NULL)
		return 0;
	if (!((int (*)(void *, int, int))VT_FN(mob, VT_WALK_CHECK))(mob, dir, 0))
		return 0;
	((void (*)(void *, int, int))VT_FN(mob, VT_DO_WALK))(mob, dir, -128);
	return 1;
}

/*
 * 0x00412311 - run [216]
 *
 * Runs a mobile one step in a direction. Same as walk but sets the
 * run bit (0x80) on the direction byte before calling DoWalk.
 * Binary pushes 0x80 which sign-extends to -128 (0xFFFFFF80).
 * Returns 1 on success, 0 on failure.
 */
int
Script_run(uint32_t serial, int dir)
{
	CItem *mob;

	if (dir < 0 || dir > 7)
		return 0;
	mob = FindMobileValidated(serial, "run");
	if (mob == NULL)
		return 0;
	if (!((int (*)(void *, int, int))VT_FN(mob, VT_WALK_CHECK))(mob, dir, 0))
		return 0;
	((void (*)(void *, int, int))VT_FN(mob, VT_DO_WALK))(mob, dir | 0x80, -128);
	return 1;
}

/*
 * 0x0041237F - setType
 *
 * Changes the entity's body type. Dead players are resurrected
 * first; resource entities that would lose their recipe are
 * re-seeded with the new body type's resource nodes scaled by the
 * old fill ratio. The entity is detached and re-attached so the
 * new appearance reaches clients.
 */
void
Script_setType(uint32_t serial, int typeID)
{
	CItem *entity;
	int hadRecipe;
	int minRatio;
	uint16_t bodyType;
	CResourceNode *node;

	entity = FindEntityValidated(serial, "setType");
	if (entity == NULL)
		return;
	if (typeID > 0x4000)
		return;

	if (VT_IsPlayer(entity)) {
		if (VT_IsDead(entity)) {
			if (!CPlayer_ApplyResurrection((CPlayer *)entity, 1))
				return;
		}
	}

	if (((int (*)(void *))VT_FN(entity, VT_HAS_RESOURCE_FLAG))(entity)) {
		// Resource entity path: handle recipe changes
		minRatio = CItem_GetMinResourceRatio(entity);
		hadRecipe = CItem_HasResourceRecipe(entity);

		((void (*)(void *))VT_FN(entity, VT_DETACH_SPATIAL))(entity);

		bodyType = (uint16_t)(CEntity_GetBodyType(entity) & 0xFFFF);
		USED(bodyType); // binary dead store
		CEntity_SetBodyType(entity, (uint16_t)typeID);

		if (hadRecipe && !CItem_HasResourceRecipe(entity)) {
			// Recipe lost - rebuild from new template
			CResourceEntity_RemoveAllNodes(entity, 1);
			node = g_ResEntitySlots[CEntity_GetBodyType(entity) & 0xFFFF].nodeHead;
			while (node != NULL) {
				CResourceEntity_AddNodeScaled(
				        entity, node->id, (int8_t)node->type, node->value1 * minRatio, node->value2 * minRatio, node->value1 * minRatio, 2, 1, 1);
				node = node->next;
			}
		}
	} else {
		// Non-resource entity: just detach and set type
		((void (*)(void *))VT_FN(entity, VT_DETACH_SPATIAL))(entity);

		CEntity_SetBodyType(entity, (uint16_t)typeID);
	}

	((void (*)(void *))VT_FN(entity, VT_RETURN_TO_TRACKED))(entity);
}

/*
 * 0x004124F3 - setHue
 *
 * Sets the entity's color and refreshes nearby clients.
 */
void
Script_setHue(uint32_t serial, int hue)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "setHue");
	if (ent == NULL)
		return;

	ent->resourceEntity.entity.color = (uint16_t)hue;

	((void (*)(CItem *))VT_FN(ent, VT_DETACH_SPATIAL))(ent);
	((void (*)(CItem *))VT_FN(ent, VT_RETURN_TO_TRACKED))(ent);
}

/*
 * 0x0041253B - setPartialHue
 *
 * Like setHue, but ORs in 0x8000 (the partial-hue flag) when the
 * target is a mobile.
 */
void
Script_setPartialHue(uint32_t serial, int hue)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "setHue");
	if (ent == NULL)
		return;

	if (VT_IsMobile(ent))
		hue |= 0x8000;

	ent->resourceEntity.entity.color = (uint16_t)hue;

	((void (*)(CItem *))VT_FN(ent, VT_DETACH_SPATIAL))(ent);
	((void (*)(CItem *))VT_FN(ent, VT_RETURN_TO_TRACKED))(ent);
}

/*
 * 0x0041259E - getDefaultTextHue
 *
 * Returns the mobile's default speech hue.
 */
int
Script_getDefaultTextHue(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getDefaultTextHue");
	if (ent == NULL)
		return 0;
	return CMobile_GetSpeechHue((CMobile *)ent);
}

/*
 * 0x004125D1 - setDefaultTextHue
 *
 * Updates the mobile's default speech hue.
 */
void
Script_setDefaultTextHue(uint32_t serial, int hue)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setDefaultTextHue");
	if (ent == NULL)
		return;
	CMobile_SetSpeechHue((CMobile *)ent, (uint16_t)hue);
}

/*
 * 0x00412602 - animateMobile
 *
 * Plays an animation on the mobile. Either records a type-7 command
 * into the active AnimSequence or, when no sequence is recording,
 * broadcasts an ANIM packet to nearby clients. Validates that the
 * action and frame count fit within the engine's clip limits.
 */
void
Script_animateMobile(uint32_t serial, int animType, int action, int frameCount, int repeat, int backwards)
{
	CItem *mob;
	CLocation tmpLoc;
	uint8_t seqData[30];
	uint8_t buf[14];
	uint8_t *ptr;

	mob = FindMobileValidated(serial, "animateMobile");
	if (mob == NULL)
		return;
	if (action > 0x14)
		return;
	if (action * 5 + frameCount > 0x3e8)
		return;

	if (g_AnimSequence.state) {
		CLocation_SetLoc(&tmpLoc, CEntity_GetLocation(&mob->resourceEntity.entity));
		AnimSequence_AddLocation(&tmpLoc);
		ptr = seqData;
		memcpy(ptr, &serial, 4);
		ptr += 4;
		memcpy(ptr, CEntity_GetLocation(&mob->resourceEntity.entity), 6);
		ptr += 6;
		memcpy(ptr, &animType, 4);
		ptr += 4;
		memcpy(ptr, &action, 4);
		ptr += 4;
		memcpy(ptr, &frameCount, 4);
		ptr += 4;
		memcpy(ptr, &repeat, 4);
		ptr += 4;
		memcpy(ptr, &backwards, 4);
		AnimSequence_AddCommand(7, seqData, 30);
	} else {
		PacketManager_MakePacket_ANIM(buf, serial, (uint16_t)animType, (uint16_t)action, (uint16_t)frameCount, (uint8_t)repeat, (uint8_t)backwards, 0);
		SendPacketInRange(buf, CEntity_GetLocation(&mob->resourceEntity.entity), 0x12);
	}
}

/*
 * 0x004127A4 - sfx
 *
 * Plays soundID at loc. Either records a type-8 sequence command
 * or broadcasts a SOUND packet to nearby players when no sequence
 * is active.
 */
void
Script_sfx(CLocation *loc, int soundID, int volume)
{
	CLocation tmpLoc;
	uint8_t seqData[14];

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return;
	if (soundID > 0x24b)
		return;

	if (g_AnimSequence.state) {
		tmpLoc = *loc;
		AnimSequence_AddLocation(&tmpLoc);
		memcpy(seqData, loc, 6);
		memcpy(seqData + 6, &soundID, 4);
		memcpy(seqData + 10, &volume, 4);
		AnimSequence_AddCommand(8, seqData, 14);
	} else {
		PlaySoundAtLocation(loc, (uint16_t)soundID, (uint16_t)volume);
	}
}

/*
 * 0x0041288A - sfxTo
 *
 * Plays soundID for one player. Either records a type-9 sequence
 * command or calls SendSoundToEntity when no sequence is active.
 */
void
Script_sfxTo(uint32_t serial, int soundID, int volume)
{
	CItem *ent;
	CLocation tmpLoc;
	uint8_t seqData[12];

	ent = FindPlayerValidated(serial, "sfxTo");
	if (ent == NULL)
		return;
	if (soundID > 0x24b)
		return;

	if (g_AnimSequence.state) {
		CLocation_SetLoc(&tmpLoc, CEntity_GetLocation(&ent->resourceEntity.entity));
		AnimSequence_AddLocation(&tmpLoc);
		memcpy(seqData, &ent, 4);
		memcpy(seqData + 4, &soundID, 4);
		memcpy(seqData + 8, &volume, 4);
		AnimSequence_AddCommand(9, seqData, 12);
	} else {
		SendSoundToEntity(ent, soundID, (uint16_t)volume);
	}
}

/*
 * 0x00412972 - musicTo
 *
 * Sends a MUSIC packet to the named player.
 */
void
Script_musicTo(uint32_t serial, int musicID)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "musicTo");
	if (ent == NULL)
		return;
	PlayMusicToEntity(ent, (uint16_t)musicID);
}

/*
 * 0x004129A7 - getDirection
 *
 * Stores a human-readable bearing string in dest ("to the north",
 * "to the southeast", ...) for the heading from loc1 to loc2.
 * Distances under 10 tiles are reported as "right here".
 */
CString *
Script_getDirection(CString *dest, CLocation *loc1, CLocation *loc2)
{
	int dist, dir;

	dist = GetDistanceInTiles_Internal(loc1, loc2);
	if (dist < 10) {
		CString_Constructor(dest, "right here");
		return dest;
	}

	dir = CalcDirection(loc1, loc2);
	switch (dir) {
	case 0:
		CString_Constructor(dest, "to the north");
		break;
	case 1:
		CString_Constructor(dest, "to the northeast");
		break;
	case 2:
		CString_Constructor(dest, "to the east");
		break;
	case 3:
		CString_Constructor(dest, "to the southeast");
		break;
	case 4:
		CString_Constructor(dest, "to the south");
		break;
	case 5:
		CString_Constructor(dest, "to the southwest");
		break;
	case 6:
		CString_Constructor(dest, "to the west");
		break;
	case 7:
		CString_Constructor(dest, "to the northwest");
		break;
	default:
		CString_Constructor(dest, "in some direction");
		break;
	}
	return dest;
}

/*
 * 0x00412B36 - getDistanceInTiles
 *
 * Returns the wrapped Chebyshev distance (in tiles) between loc1
 * and loc2.
 */
int
Script_getDistanceInTiles(const CLocation *loc1, const CLocation *loc2)
{
	int d = GetDistanceInTiles_Internal(loc1, loc2);
	return d;
}

/*
 * 0x00412B4B - getDistance
 *
 * Stores a human-readable distance phrase in dest ("right here",
 * "just a short way", "a fair distance", ...) for the gap between
 * loc1 and loc2, bucketed by tens of tiles via a 30-entry lookup.
 */
CString *
Script_getDistance(CString *dest, CLocation *loc1, CLocation *loc2)
{
	static const uint8_t distTable[30] = { 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 };
	int dist, quotient;
	unsigned int idx;

	dist = Script_getDistanceInTiles(loc1, loc2);
	quotient = dist / 10;
	idx = (unsigned int)(quotient - 1);
	if (idx > 29) {
		CString_Constructor(dest, "a long journey");
		return dest;
	}

	switch (distTable[idx]) {
	case 0:
		CString_Constructor(dest, "right here");
		break;
	case 1:
		CString_Constructor(dest, "just a short way");
		break;
	case 2:
		CString_Constructor(dest, "a ways");
		break;
	case 3:
		CString_Constructor(dest, "a fair distance");
		break;
	case 4:
		CString_Constructor(dest, "a long way");
		break;
	case 5:
		CString_Constructor(dest, "quite a long distance");
		break;
	default:
		CString_Constructor(dest, "a long journey");
		break;
	}
	return dest;
}

/*
 * 0x00412CDE - interpose
 *
 * Stores the midpoint between loc1 and loc2 (z taken from loc1)
 * in outLoc. As a side effect, loc1 is overwritten with the same
 * midpoint.
 */
CLocation *
Script_interpose(CLocation *outLoc, CLocation *loc1, const CLocation *loc2)
{
	int dx, dy;

	// Signed division by 2, rounds toward zero (cdq; sub eax,edx; sar eax,1)
	dx = ((int)loc1->x - (int)loc2->x);
	dx = (dx + ((unsigned)dx >> 31)) >> 1;
	dy = ((int)loc1->y - (int)loc2->y);
	dy = (dy + ((unsigned)dy >> 31)) >> 1;

	// Modify loc1 to hold midpoint
	loc1->x = (int16_t)((int)loc2->x + dx);
	loc1->y = (int16_t)((int)loc2->y + dy);

	// Copy result to output
	CLocation_SetLoc(outLoc, loc1);
	return outLoc;
}

/*
 * 0x00412D45 - getFacing
 *
 * Returns the mobile's current facing direction.
 */
int
Script_getFacing(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getFacing");
	if (ent == NULL)
		return 0;
	return ((CMobile *)ent)->direction;
}

/*
 * 0x00412D74 - getHue
 *
 * Returns the entity's color value.
 */
int
Script_getHue(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getHue");
	if (ent == NULL)
		return 0;
	return ent->resourceEntity.entity.color;
}

/*
 * 0x00412DA3 - isFacingPlace
 *
 * Returns 1 when the mobile's facing direction matches the bearing
 * from its position to loc.
 */
int
Script_isFacingPlace(uint32_t serial, CLocation *loc)
{
	CItem *ent;
	int dir;

	ent = FindMobileValidated(serial, "isFacingPlace");
	if (ent == NULL)
		return 0;
	dir = CalcDirection(&ent->resourceEntity.entity.location, loc);
	if (((CMobile *)ent)->direction == (uint8_t)dir)
		return 1;
	return 0;
}

/*
 * 0x00412DF2 - isFacingPerson
 *
 * Returns 1 when the mobile's facing direction matches the bearing
 * from its position to the target entity.
 */
int
Script_isFacingPerson(uint32_t serial, uint32_t targetSerial)
{
	CItem *mob;
	CItem *target;
	int dir;

	mob = FindMobileValidated(serial, "isFacingPerson (mobile)");
	if (mob == NULL)
		return 0;
	target = FindEntityValidated(targetSerial, "isFacingPerson (victim)");
	if (target == NULL)
		return 0;
	dir = CalcDirection(&mob->resourceEntity.entity.location, &target->resourceEntity.entity.location);
	if (((CMobile *)mob)->direction == (uint8_t)dir)
		return 1;
	return 0;
}

/*
 * 0x00412E66 - facingEachOther
 *
 * Returns 1 when both mobiles are facing each other along the line
 * between their positions. The original binary contains a NULL-check
 * quirk (it re-checks mob1 in place of mob2); reproduced exactly.
 */
int
Script_facingEachOther(uint32_t serial1, uint32_t serial2)
{
	CItem *mob1;
	CItem *mob2;
	int dir;

	mob1 = FindMobileValidated(serial1, "facingEachother (mobile)");
	if (mob1 == NULL)
		return 0;
	mob2 = FindMobileValidated(serial2, "facingEachother (victim)");
	// Binary checks mob1 again here (not mob2) - reproducing exactly.
	if (mob1 == NULL)
		return 0;
	dir = CalcDirection(&mob1->resourceEntity.entity.location, &mob2->resourceEntity.entity.location);
	if (((CMobile *)mob1)->direction != (uint8_t)dir)
		return 0;
	dir = CalcDirection(&mob2->resourceEntity.entity.location, &mob1->resourceEntity.entity.location);
	if (((CMobile *)mob2)->direction != (uint8_t)dir)
		return 0;
	return 1;
}

/*
 * 0x00412EFF - faceHere
 *
 * Sets the mobile's facing direction and refreshes nearby clients.
 * Rejects directions outside [0, 7].
 */
void
Script_faceHere(uint32_t serial, int dir)
{
	CItem *mob;

	mob = FindMobileValidated(serial, "faceHere");
	if (mob == NULL)
		return;
	if (dir < 0 || dir > 7)
		return;

	CMobile_SetDirection(mob, (uint32_t)dir);

	((void (*)(void *))VT_FN(mob, VT_DETACH_SPATIAL))(mob);
	((void (*)(void *))VT_FN(mob, VT_RETURN_TO_TRACKED))(mob);
}

/*
 * 0x00412F56 - getSex
 *
 * Returns 0 for male body type, 1 for female, 2 otherwise.
 */
int
Script_getSex(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getSex");
	if (ent == NULL)
		return 0;
	if (CResourceEntity_GetBodyType(ent) == 0x190)
		return 0;
	if (CResourceEntity_GetBodyType(ent) == 0x191)
		return 1;
	return 2;
}

/*
 * 0x00412FB4 - sameSex
 *
 * Returns 1 when both serials report the same sex.
 */
int
Script_sameSex(uint32_t serial1, uint32_t serial2)
{
	return Script_getSex(serial1) == Script_getSex(serial2);
}

/*
 * 0x00412FDE - isHuman
 *
 * Returns 1 when the mobile uses a human/creature body type.
 */
int
Script_isHuman(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, NULL);
	if (ent == NULL)
		return 0;
	return CMobile_IsCreatureBody((CMobile *)ent);
}

/*
 * 0x00413009 - isHumanBodyType
 *
 * Returns 1 when the mobile uses a human-shaped body type.
 */
int
Script_isHumanBodyType(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, NULL);
	if (ent == NULL)
		return 0;
	return CMobile_IsHumanBodyType((CMobile *)ent);
}

/*
 * 0x00413034 - isMobile
 *
 * Script binding: returns 1 if the entity is a mobile.
 */
int
Script_isMobile(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsMobile, "isMobile");
}

/*
 * 0x0041304F - isPlayer
 *
 * Script binding: returns 1 if the entity is a player.
 */
int
Script_isPlayer(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsPlayer, "isPlayer");
}

/*
 * 0x0041306A - isSpellbook
 *
 * Script binding: returns 1 if the entity is a spellbook.
 */
int
Script_isSpellbook(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsSpellbook, "isSpellbook");
}

/*
 * 0x00413085 - isContainer
 *
 * Script binding: returns 1 if the entity is a container (including
 * corpses and other container-derived types).
 */
int
Script_isContainer(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsContainer, "isContainer");
}

/*
 * 0x004130A0 - isRealContainer
 *
 * Script binding: returns 1 if the entity is a real CContainer, not
 * one of the container-derived subtypes.
 */
int
Script_isRealContainer(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsRealContainer, "isRealContainer");
}

/*
 * 0x004130BB - isMap
 *
 * Script binding: returns 1 if the entity is a map item.
 */
int
Script_isMap(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsMap, "isMap");
}

/*
 * 0x004130D6 - isNPC
 *
 * Script binding: returns 1 if the entity is an NPC.
 */
int
Script_isNPC(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsNPC, "isNPC");
}

/*
 * 0x004130F1 - isShopkeeper
 *
 * Script binding: returns 1 if the entity is a shopkeeper NPC.
 */
int
Script_isShopkeeper(uint32_t serial)
{
	int rc = Script_checkEntity(serial, check_IsShopkeeper, "isShopKeeper");
	return rc;
}

/*
 * 0x0041310C - isGuard
 *
 * Script binding: returns 1 if the entity is a guard NPC.
 */
int
Script_isGuard(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsGuard, "isGuard");
}

/*
 * 0x00413127 - isDead
 *
 * Script binding: returns 1 if the mobile is dead.
 */
int
Script_isDead(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsDead, "isDead");
}

/*
 * 0x00413142 - isInContainer
 *
 * Script binding: returns 1 if the entity is inside a container.
 */
int
Script_isInContainer(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsInContainer, "isInContainer");
}

/*
 * 0x0041315D - isEquipped
 *
 * Script binding: returns 1 if the entity is currently equipped on a
 * mobile.
 */
int
Script_isEquipped(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsEquipped, "isEquipped");
}

/*
 * 0x00413178 - isMoveable
 *
 * Returns 1 when the player is allowed to move the named entity.
 */
int
Script_isMoveable(uint32_t thingSerial, uint32_t playerSerial)
{
	CItem *ent;
	CItem *mob;

	ent = FindEntityValidated(thingSerial, "isMoveable");
	if (ent == NULL)
		return 0;
	mob = FindMobileValidated(playerSerial, "isMoveable");
	if (mob == NULL)
		return 0;
	return ((int (*)(void *, void *))VT_FN(ent, VT_IS_MOVEABLE))(ent, mob);
}

/*
 * 0x004131D0 - isFreelyUsable
 *
 * Returns 1 when the player can use the named entity without
 * additional permission checks.
 */
int
Script_isFreelyUsable(uint32_t thingSerial, uint32_t playerSerial)
{
	CItem *ent;
	CItem *mob;

	ent = FindEntityValidated(thingSerial, "isFreelyUsable");
	if (ent == NULL)
		return 0;
	mob = FindMobileValidated(playerSerial, "isFreelyUsable");
	if (mob == NULL)
		return 0;
	return ((int (*)(void *, void *))VT_FN(ent, VT_IS_FREELY_USABLE))(ent, mob);
}

/*
 * 0x00413228 - isFreelyViewable
 *
 * Returns 1 when the player can see the named entity without
 * additional permission checks.
 */
int
Script_isFreelyViewable(uint32_t thingSerial, uint32_t playerSerial)
{
	CItem *ent;
	CItem *mob;

	ent = FindEntityValidated(thingSerial, "isFreelyViewable");
	if (ent == NULL)
		return 0;
	mob = FindMobileValidated(playerSerial, "isFreelyViewable");
	if (mob == NULL)
		return 0;
	return ((int (*)(void *, void *))VT_FN(ent, VT_IS_FREELY_VIEWABLE))(ent, mob);
}

/*
 * 0x00413280 - isOnline
 *
 * Returns 1 when the serial belongs to a logged-in player.
 */
int
Script_isOnline(uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (!VT_IsPlayer(ent))
		return 0;
	if (((CPlayer *)ent)->pflags & 4)
		return 1;
	return 0;
}

/*
 * 0x004132C7 - removePlayerFromGame
 *
 * Disconnects an offline player slot. Returns 1 when the disconnect
 * happened, 0 if the player is online or not a player.
 */
int
Script_removePlayerFromGame(uint32_t serial)
{
	CItem *ent;
	CPlayer *player;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (!VT_IsPlayer(ent))
		return 0;
	player = (CPlayer *)ent;
	if (player->pflags & PlayerIsOnline)
		return 0;
	CPlayer_Disconnect(player);
	return 1;
}

/*
 * 0x00413316 - getEncumbrance
 *
 * Returns the mobile's carry-weight encumbrance as a percentage,
 * or -1 when the mobile is not found.
 */
int
Script_getEncumbrance(uint32_t serial)
{
	CMobile *mob;

	mob = (CMobile *)FindMobileValidated(serial, "getEncumbrance");
	if (mob == NULL)
		return -1;
	return CMobile_GetEncumbrancePercent(mob);
}

/*
 * 0x00413345 - containedBy
 *
 * Returns the parent container's serial when the entity sits inside
 * one, or 0 when it is on the ground or detached.
 */
uint32_t
Script_containedBy(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "containedBy");
	if (ent == NULL)
		return 0;
	if (((int (*)(void *))VT_FN(ent, VT_HAS_CONTAINER))(ent))
		return ent->parent->serial;
	return 0;
}

/*
 * 0x0041338A - isNoDrawType
 *
 * Returns 1 when bodyType refers to a tile flagged as non-rendered.
 */
int
Script_isNoDrawType(int bodyType)
{
	if (bodyType < 0 || bodyType >= 0x4000)
		return 0;
	return IsNoDrawType((uint16_t)bodyType);
}

/*
 * 0x004133AF - bark (CString)
 *
 * Speaks text on behalf of the entity. When the entity is held by
 * another player, the message goes to that player as a system
 * message; otherwise it is broadcast as ordinary speech.
 */
void
Script_bark(uint32_t serial, CString *text)
{
	CItem *ent;
	CItem *top;

	ent = FindEntityValidated(serial, "bark");
	if (ent == NULL)
		return;

	top = ent;
	while (top->parent != NULL)
		top = top->parent;

	if (VT_IsPlayer(top) && top != ent) {
		CPlayer_SystemMessage((CPlayer *)top, CString_GetData(text));
		return;
	}

	((void (*)(CItem *, char *, int, int, int))VT_FN(ent, VT_SAY_CSTRING))(ent, CString_GetData(text), -1, -1, -1);
}

/*
 * 0x00413433 - bark (CUString/unicode)
 *
 * Unicode counterpart of Script_bark: speaks unicode text on behalf
 * of the entity, sent as a system message when the entity is held
 * by another player.
 */
void
Script_barkUnicode(uint32_t serial, CUString *text)
{
	CItem *ent;
	CItem *top;

	ent = FindEntityValidated(serial, "bark");
	if (ent == NULL)
		return;

	top = ent;
	while (top->parent != NULL)
		top = top->parent;

	// only players receive system messages
	if (VT_IsPlayer(top) && top != ent) {
		CPlayer_SystemMessage_Unicode((CPlayer *)top, (uint16_t *)CUString_GetData(text));
		return;
	}

	((void (*)(CItem *, uint16_t *, int, int, int))VT_FN(ent, VT_SAY_CUSTRING))(ent, (uint16_t *)CUString_GetData(text), -1, -1, -1);
}

/*
 * 0x004134B7 - ebark (CString emote)
 *
 * Broadcasts text from the entity as an emote.
 */
void
Script_ebark(uint32_t serial, CString *text)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "bark");
	if (ent == NULL)
		return;

	((void (*)(CItem *, char *, int, int, int))VT_FN(ent, VT_EMOTE_CSTRING))(ent, CString_GetData(text), -1, -1, -1);
}

/*
 * 0x004134F5 - ebark (CUString/unicode emote)
 *
 * Broadcasts unicode text from the entity as an emote.
 */
void
Script_ebarkUnicode(uint32_t serial, CUString *text)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "bark");
	if (ent == NULL)
		return;

	((void (*)(CItem *, uint16_t *, int, int, int))VT_FN(ent, VT_EMOTE_CUSTRING))(ent, (uint16_t *)CUString_GetData(text), -1, -1, -1);
}

/*
 * 0x00413533 - barkTo (CString directed)
 *
 * Sends text as speech from the speaker directed at a single
 * target player, using the default speech hue.
 */
void
Script_barkTo(uint32_t speakerSerial, uint32_t targetSerial, CString *text)
{
	CItem *speaker;
	CItem *target;

	speaker = FindEntityValidated(speakerSerial, "barkTo (speaker)");
	if (speaker == NULL)
		return;

	target = FindPlayerValidated(targetSerial, "barkTo (spokenTo)");
	if (target == NULL)
		return;

	((void (*)(CItem *, CItem *, uint32_t, char *))VT_FN(speaker, VT_SAY_TO_ENTITY))(speaker, target, targetSerial, CString_GetData(text));
}

/*
 * 0x00413591 - barkTo (CUString/unicode directed)
 *
 * Unicode counterpart of Script_barkTo.
 */
void
Script_barkToUnicode(uint32_t speakerSerial, uint32_t targetSerial, CUString *text)
{
	CItem *speaker;
	CItem *target;

	speaker = FindEntityValidated(speakerSerial, "barkTo (speaker)");
	if (speaker == NULL)
		return;

	target = FindPlayerValidated(targetSerial, "barkTo (spokenTo)");
	if (target == NULL)
		return;

	((void (*)(CItem *, CItem *, uint32_t, uint16_t *))VT_FN(speaker, VT_SAY_TO_CUSTRING))(speaker, target, targetSerial, (uint16_t *)CUString_GetData(text));
}

/*
 * 0x004135EF - ebarkTo (CString directed emote)
 *
 * Sends text as a directed emote from the speaker to a single
 * target player, using the default emote hue.
 */
void
Script_ebarkTo(uint32_t speakerSerial, uint32_t targetSerial, CString *text)
{
	CItem *speaker;
	CItem *target;

	speaker = FindEntityValidated(speakerSerial, "barkTo (speaker)");
	if (speaker == NULL)
		return;

	target = FindPlayerValidated(targetSerial, "barkTo (spokenTo)");
	if (target == NULL)
		return;

	((void (*)(CItem *, CItem *, uint32_t, char *))VT_FN(speaker, VT_EMOTE_TO_ENTITY))(speaker, target, targetSerial, CString_GetData(text));
}

/*
 * 0x0041364D - ebarkTo (CUString/unicode directed emote)
 *
 * Unicode counterpart of Script_ebarkTo.
 */
void
Script_ebarkToUnicode(uint32_t speakerSerial, uint32_t targetSerial, CUString *text)
{
	CItem *speaker;
	CItem *target;

	speaker = FindEntityValidated(speakerSerial, "barkTo (speaker)");
	if (speaker == NULL)
		return;

	target = FindPlayerValidated(targetSerial, "barkTo (spokenTo)");
	if (target == NULL)
		return;

	((void (*)(CItem *, CItem *, uint32_t, uint16_t *))VT_FN(speaker, VT_EMOTE_TO_CUSTRING))(speaker, target, targetSerial, (uint16_t *)CUString_GetData(text));
}

/*
 * 0x004136AB - barkToHued (CString directed with explicit hue)
 *
 * Like Script_barkTo, but uses an explicit speech hue.
 */
void
Script_barkToHued(uint32_t speakerSerial, uint32_t targetSerial, int hue, CString *text)
{
	CItem *speaker;
	CItem *target;

	speaker = FindEntityValidated(speakerSerial, "barkTo (speaker)");
	if (speaker == NULL)
		return;

	target = FindPlayerValidated(targetSerial, "barkTo (spokenTo)");
	if (target == NULL)
		return;

	((void (*)(CItem *, CItem *, uint32_t, char *, uint16_t))VT_FN(speaker, VT_SAY_HUED_CSTRING))(speaker, target, targetSerial, CString_GetData(text), (uint16_t)hue);
}

/*
 * 0x0041370E - barkToHued (CUString/unicode directed with explicit hue)
 *
 * Unicode counterpart of Script_barkToHued.
 */
void
Script_barkToHuedUnicode(uint32_t speakerSerial, uint32_t targetSerial, int hue, CUString *text)
{
	CItem *speaker;
	CItem *target;

	speaker = FindEntityValidated(speakerSerial, "barkTo (speaker)");
	if (speaker == NULL)
		return;

	target = FindPlayerValidated(targetSerial, "barkTo (spokenTo)");
	if (target == NULL)
		return;

	((void (*)(CItem *, CItem *, uint32_t, uint16_t *, uint16_t))VT_FN(speaker, VT_SAY_HUED_CUSTRING))(
	        speaker, target, targetSerial, (uint16_t *)CUString_GetData(text), (uint16_t)hue);
}

/*
 * 0x00413771 - actionBark
 *
 * Sends text1 to the player as a self-message and broadcasts text2
 * to nearby observers, both at the given speech hue.
 */
void
Script_actionBark(uint32_t serial, int hue, CString *text1, CString *text2)
{
	CItem *player;

	player = FindPlayerValidated(serial, "actionBark");
	if (player == NULL)
		return;

	CMobile_ActionBark(player, hue, CString_GetBuffer(text1), CString_GetBuffer(text2));
}

/*
 * 0x004137B3 - getObjType
 *
 * Returns the entity's body type (graphic ID).
 */
int
Script_getObjType(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getObjType");
	if (ent == NULL)
		return 0;
	return CEntity_GetBodyType(ent);
}

/*
 * 0x004137E6 - getValue
 *
 * Returns the entity's normalized item value (price).
 */
int
Script_getValue(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getValue");
	if (ent == NULL)
		return 0;
	return ((int (*)(void *, int, int))VT_FN(ent, VT_GET_VALUE))(ent, 0, 1);
}

/*
 * 0x0041381B - makeValueless
 *
 * Marks the entity as decayed so it no longer carries item value.
 */
int
Script_makeValueless(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "makeValueless");
	if (ent == NULL)
		return 0;
	CItem_DecayProcess(ent);
	return 1;
}

/*
 * 0x0041384E - getWeight
 *
 * Returns the entity's weight (including any contained items).
 */
static int
check_GetWeight(CItem *ent) // 0x004243D0
{
	return ((int (*)(void *))VT_FN(ent, VT_GET_WEIGHT))(ent);
}

int
Script_getWeight(uint32_t serial)
{
	return Script_checkEntity(serial, check_GetWeight, "getWeight");
}

/*
 * 0x00413869 - isInCamp
 *
 * Returns 1 when the player stands near a campfire.
 */
int
Script_isInCamp(uint32_t serial)
{
	return Script_checkPlayer(serial, IsNearCampfire, "isInCamp");
}

/*
 * 0x00413884 - getLocation
 *
 * Stores the entity's effective location (walking the parent chain
 * for contained items) in retloc, falling back to (-1, -1, 0) when
 * the entity is missing. Returns retloc.
 */
CLocation *
Script_getLocation(CLocation *retloc, uint32_t serial)
{
	CItem *ent;
	CLocation loc;

	ent = FindEntityValidated(serial, "getLocation");
	if (ent == NULL) {
		CLocation_Init(&loc);
		CLocation_Set(&loc, -1, -1, 0);
		CLocation_SetLoc(retloc, &loc);
		return retloc;
	}
	// walks parent chain for contained items
	CLocation_SetLoc(retloc, ((CLocation * (*)(void *)) VT_FN(ent, VT_GET_LOCATION))(ent));
	return retloc;
}

/*
 * 0x004138E9 - getMasterObjLoc
 *
 * Stores the world location of the index-th master-object slot in
 * retloc, falling back to (-1, -1, 0) for out-of-range indices.
 */
CLocation *
Script_getMasterObjLoc(CLocation *retloc, int index)
{
	if (index < 0 || index >= 0x80) {
		CLocation_Constructor3D(retloc, -1, -1, 0);
		return retloc;
	}
	CLocation_Constructor3D(retloc, (int16_t)(g_mapStartX + index), (int16_t)g_mapStartY, 0);
	return retloc;
}

/*
 * 0x0041392E - getRelayLoc
 *
 * Returns the map origin location (g_mapStartX, g_mapStartY, 0).
 * Binary ignores the serial argument entirely.
 * Binary uses CLocation_Constructor3D and returns retloc pointer in EAX.
 */
CLocation *
Script_getRelayLoc(CLocation *retloc, uint32_t serial)
{
	USED(serial);
	CLocation_Constructor3D(retloc, (int16_t)g_mapStartX, (int16_t)g_mapStartY, 0);
	return retloc;
}

/*
 * 0x0041394F - whereIs
 *
 * Like Script_getLocation: stores the root container's world
 * location in retloc, falling back to (-1, -1, 0) when missing.
 */
CLocation *
Script_whereIs(CLocation *retloc, uint32_t serial)
{
	CItem *ent;
	CLocation tmpLoc;

	ent = FindEntityValidated(serial, "whereIs");
	if (ent == NULL) {
		CLocation_Init(&tmpLoc);
		CLocation_Set(&tmpLoc, -1, -1, 0);
		CLocation_SetLoc(retloc, &tmpLoc);
		return retloc;
	}
	CLocation_SetLoc(retloc, ((CLocation * (*)(void *)) VT_FN(ent, VT_GET_LOCATION))(ent));
	return retloc;
}

/*
 * 0x004139B4 - numInContainer
 *
 * Returns the number of items directly inside the container.
 */
int
Script_numInContainer(uint32_t serial)
{
	CItem *ent;

	ent = FindContainerValidated(serial, "numInContainer");
	if (ent == NULL)
		return 0;
	return CContainer_CountItems((CContainer *)ent, 0);
}

/*
 * 0x004139E4 - isVirtueGuard
 *
 * Returns 1 when the mobile is an order or a chaos guard.
 */
int
Script_isVirtueGuard(uint32_t serial)
{
	return Script_checkMobile(serial, check_IsVirtueGuard, "isVirtueGuard");
}

/*
 * 0x004139FF - isUsingVirtueShield
 *
 * Returns 1 when the mobile carries an order or a chaos shield.
 */
int
Script_isUsingVirtueShield(uint32_t serial)
{
	return Script_checkMobile(serial, check_IsUsingVirtueShield, "isUsingVirtueShield");
}

/*
 * 0x00413A1A - isOrderGuard
 *
 * Returns 1 when the mobile is an order guard.
 */
int
Script_isOrderGuard(uint32_t serial)
{
	return Script_checkMobile(serial, check_IsOrderGuard, "isOrderGuard");
}

/*
 * 0x00413A35 - isChaosGuard
 *
 * Returns 1 when the mobile is a chaos guard.
 */
int
Script_isChaosGuard(uint32_t serial)
{
	return Script_checkMobile(serial, check_IsChaosGuard, "isChaosGuard");
}

/*
 * 0x00413A50 - isRidable
 *
 * Returns 1 when the mobile can be mounted.
 */
int
Script_isRidable(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_IsRideable, "isRidable");
}

/*
 * 0x00413A6B - isRiding
 *
 * Returns 1 when the mobile is currently mounted.
 */
int
Script_isRiding(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_IsMounted, "isRiding");
}

/*
 * 0x00413A86 - unRide
 *
 * Dismounts the mobile from its current mount.
 */
int
Script_unRide(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_Dismount, "unRide");
}

/*
 * 0x00413AA1 - getAC
 *
 * Returns the mobile's combat armor class.
 */
int
Script_getAC(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))Combat_CalcArmorClass, "getAC");
}

/*
 * 0x00413ABC - getMovementType
 *
 * Returns the mobile's current movement type.
 */
int
Script_getMovementType(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getMovementType");
	if (ent == NULL)
		return 0;
	return ((int (*)(void *))VT_FN(ent, 0x94))(ent) & 0xFF;
}

/*
 * 0x00413AF5 - setMovementType
 *
 * Sets the mobile's movement type. Values outside [0, 9] are
 * silently ignored.
 */
void
Script_setMovementType(uint32_t serial, int moveType)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setMovementType");
	if (ent == NULL)
		return;
	if (moveType < 0 || moveType > 9)
		return;
	CMobile_SetMovementType((CMobile *)ent, (uint8_t)moveType);
}

/*
 * 0x00413B33 - getMoney
 *
 * Returns the total amount of gold (body type 0xEED) carried by
 * the mobile.
 */
int
Script_getMoney(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getMoney");
	if (ent == NULL)
		return 0;
	return CMobile_GetTotalQuantityOfType((CMobile *)ent, 0x0EED);
}

/*
 * 0x00413B66 - getGeneric
 *
 * Returns the total quantity of items of body type type carried by
 * the mobile.
 */
int
Script_getGeneric(uint32_t serial, int type)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getGeneric");
	if (ent == NULL)
		return 0;
	return CMobile_GetTotalQuantityOfType((CMobile *)ent, (uint16_t)type);
}

/*
 * 0x00413B99 - Script handler for gainFame [294]
 *
 * Adds amount to the mobile's notoriety (the script keyword says
 * "fame" but the binding wires through to notoriety). Values above
 * 0xFF are ignored.
 */
void
Script_gainFame(uint32_t serial, int amount)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "gainFame");
	if (ent == NULL)
		return;
	if (amount > 0xff)
		return;
	CMobile_GainNotoriety((CMobile *)ent, amount);
}

/*
 * 0x00413BD4 - returnObject
 *
 * Returns the entity to the location it was tracking before being
 * picked up: removes it from its current world position (when in
 * the world) and re-attaches it via VT_RETURN_TO_TRACKED.
 */
void
Script_returnObject(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "returnObject");
	if (ent == NULL)
		return;

	if (ent->resourceEntity.entity.removedFromWorld == 0) {
		((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);
	}

	((void (*)(void *))VT_FN(ent, VT_RETURN_TO_TRACKED))(ent);
}

/*
 * 0x00413C1D - loseFame
 *
 * Subtracts amount from the mobile's notoriety (the script keyword
 * says "fame" but the binding wires through to notoriety). Values
 * above 0xFF are ignored.
 */
void
Script_loseFame(uint32_t serial, int amount)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "loseFame");
	if (ent == NULL)
		return;
	if (amount > 0xff)
		return;
	CMobile_LoseNotoriety((CMobile *)ent, amount);
}

/*
 * 0x00413C58 - getNotoriety
 *
 * Returns the mobile's raw notoriety value.
 */
int
Script_getNotoriety(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetNotoriety, "getNotoriety");
}

/*
 * 0x00413C73 - getNotorietyLevel
 *
 * Returns the mobile's bucketed notoriety level.
 */
int
Script_getNotorietyLevel(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetNotoLevel, "getNotorietyLevel");
}

/*
 * 0x00413C8E - getNotorietyLevelByNot
 *
 * Returns the bucketed notoriety level for a raw notoriety value.
 */
int
Script_getNotorietyLevelByNot(int value)
{
	return NotoValueToLevel(value);
}

/*
 * 0x00413C9F - getFame
 *
 * Script binding: returns the mobile's fame value.
 */
int
Script_getFame(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetFame, "getFame");
}

/*
 * 0x00413CBA - getAdjFame
 *
 * Returns the mobile's adjusted (effective) fame value.
 */
int
Script_getAdjFame(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetAdjFame, "getAdjFame");
}

/*
 * 0x00413CD5 - getFameLevel
 *
 * Returns the bucketed fame level for the mobile.
 */
int
Script_getFameLevel(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetFameLevel, "getFameLevel");
}

/*
 * 0x00413CF0 - setFame
 *
 * Sets the mobile's fame to fame.
 */
void
Script_setFame(uint32_t serial, int fame)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getFame");
	if (ent == NULL)
		return;
	CMobile_SetFame((CMobile *)ent, fame);
}

/*
 * 0x00413D20 - changeFame
 *
 * Adjusts the mobile's fame by amount (signed).
 */
void
Script_changeFame(uint32_t serial, int amount)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "changeFame");
	if (ent == NULL)
		return;
	CMobile_ChangeFame((CMobile *)ent, amount);
}

/*
 * 0x00413D50 - getKarma
 *
 * Script binding: returns the mobile's karma value.
 */
int
Script_getKarma(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetKarma, "getKarma");
}

/*
 * 0x00413D6B - getAdjKarma
 *
 * Returns the mobile's adjusted (effective) karma value.
 */
int
Script_getAdjKarma(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetAdjKarma, "getAdjKarma");
}

/*
 * 0x00413D86 - getKarmaLevel
 *
 * Returns the bucketed karma level for the mobile.
 */
int
Script_getKarmaLevel(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetKarmaLevel, "getKarmaLevel");
}

/*
 * 0x00413DA1 - setKarma
 *
 * Sets the mobile's karma to karma.
 */
void
Script_setKarma(uint32_t serial, int karma)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getKarma");
	if (ent == NULL)
		return;
	CMobile_SetKarma((CMobile *)ent, karma);
}

/*
 * 0x00413DD1 - changeKarma
 *
 * Adjusts the mobile's karma by amount (signed).
 */
void
Script_changeKarma(uint32_t serial, int amount)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "changeKarma");
	if (ent == NULL)
		return;
	CMobile_ChangeKarma((CMobile *)ent, amount);
}

/*
 * 0x00413E01 - Script handler for addNotoriety [298]
 *
 * Adds amount to the mobile's notoriety; values above 0xFF are
 * ignored.
 */
void
Script_addNotoriety(uint32_t serial, int amount)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "addNotoriety");
	if (ent == NULL)
		return;
	if (amount > 0xff)
		return;
	CMobile_ChangeNotoriety((CMobile *)ent, amount);
}

/*
 * 0x00413E3C - Script handler for removeNotoriety [299]
 *
 * Subtracts amount from the mobile's notoriety; values above 0xFF
 * are ignored.
 */
void
Script_removeNotoriety(uint32_t serial, int amount)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "removeNotoriety");
	if (ent == NULL)
		return;
	if (amount > 0xff)
		return;
	CMobile_ChangeNotorietyNeg((CMobile *)ent, amount);
}

/*
 * 0x00413E77 - setNotoriety
 *
 * Sets the mobile's notoriety to not_val. Values outside [-127, 127]
 * are silently ignored.
 */
void
Script_setNotoriety(uint32_t serial, int not_val)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setNotoriety");
	if (ent == NULL)
		return;
	if (not_val < -127 || not_val > 127)
		return;
	((void (*)(void *, int))VT_FN(ent, VT_SET_NOTORIETY))(ent, not_val);
}

/*
 * 0x00413EBB - getCurHP
 *
 * Script binding: returns the mobile's current HP.
 */
int
Script_getCurHP(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetHp, "getCurHP");
}

/*
 * 0x00413ED6 - getMaxHP
 *
 * Script binding: returns the mobile's maximum HP.
 */
int
Script_getMaxHP(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetMaxHp, "getMaxHP");
}

/*
 * 0x00413EF1 - getCurFatigue
 *
 * Script binding: returns the mobile's current stamina.
 */
int
Script_getCurFatigue(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetStamina, "getCurFatigue");
}

/*
 * 0x00413F0C - getMaxFatigue
 *
 * Script binding: returns the mobile's maximum stamina.
 */
int
Script_getMaxFatigue(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetMaxStamina, "getMaxFatigue");
}

/*
 * 0x00413F27 - getCurMana
 *
 * Script binding: returns the mobile's current mana.
 */
int
Script_getCurMana(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetMana, "getCurMana");
}

/*
 * 0x00413F42 - getMaxMana
 *
 * Script binding: returns the mobile's maximum mana.
 */
int
Script_getMaxMana(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetMaxMana, "getMaxMana");
}

/*
 * 0x00413F5D - getCanCarry
 *
 * Returns the mobile's maximum carry weight.
 */
static int
check_GetCanCarry(CItem *ent) // 0x00424400
{
	return (int)CMobile_GetMaxWeight((CMobile *)ent);
}

int
Script_getCanCarry(uint32_t serial)
{
	return Script_checkMobile(serial, check_GetCanCarry, "getCanCarry");
}

/*
 * 0x00413F78 - getSkillTotal
 *
 * Returns the sum of every base skill on the mobile.
 */
static int
check_GetSkillTotal(CItem *ent)
{
	CMobile *mob = (CMobile *)ent;
	int total = 0;
	int i, maxSkills;

	maxSkills = CSkillManager_GetMaxSkills(&g_SkillManager);
	for (i = 0; i < maxSkills; i++)
		total += CMobile_GetBaseSkill(mob, (uint8_t)i);
	return total;
}

int
Script_getSkillTotal(uint32_t serial)
{
	return Script_checkMobile(serial, check_GetSkillTotal, "getSkillTotal");
}

/*
 * 0x00413F93 - getCappedSkillTotal
 *
 * Returns the mobile's total skill weight (sum of skills capped by
 * the per-skill weight curve).
 */
int
Script_getCappedSkillTotal(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_CalcTotalSkillWeight, "getCappedSkillTotal");
}

/*
 * 0x00413FAE - getHPLevel
 *
 * Returns the mobile's HP as a percentage of its maximum.
 */
int
Script_getHPLevel(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "getHPLevel");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	return (int)(CMobile_GetHp(mob) * 100 / CMobile_GetMaxHp(mob));
}

/*
 * 0x00413FF2 - getFatigueLevel
 *
 * Returns the mobile's stamina as a percentage of its maximum.
 */
int
Script_getFatigueLevel(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "getFatigueLevel");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	return (int)(CMobile_GetStamina(mob) * 100 / CMobile_GetMaxStamina(mob));
}

/*
 * 0x00414036 - getManaLevel
 *
 * Returns the mobile's mana as a percentage of its maximum.
 */
int
Script_getManaLevel(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "getManaLevel");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	return (int)(CMobile_GetMana(mob) * 100 / CMobile_GetMaxMana(mob));
}

/*
 * 0x0041407A - setCurHP
 *
 * Sets the mobile's current HP.
 */
void
Script_setCurHP(uint32_t serial, int hp)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setCurHP");
	if (ent == NULL)
		return;
	((uintptr_t (*)(void *, int, int))VT_FN(ent, VT_SET_HP))(ent, hp, 0);
}

/*
 * 0x004140B2 - setMaxHP
 *
 * Sets the mobile's maximum HP and sends a stat update.
 */
void
Script_setMaxHP(uint32_t serial, int maxhp)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setMaxHP");
	if (ent == NULL)
		return;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MAX_HP))(ent, maxhp);
	((void (*)(void *))VT_FN(ent, VT_SEND_HP_UPDATE))(ent);
}

/*
 * 0x004140F6 - handleHealthGain
 *
 * Sends a stat update notifying clients of the mobile's HP change.
 */
void
Script_handleHealthGain(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "handleHealthGain");
	if (ent == NULL)
		return;
	((void (*)(void *))VT_FN(ent, VT_SEND_HP_UPDATE))(ent);
}

/*
 * 0x00414128 - setCurFatigue
 *
 * Sets the mobile's current stamina (clamped to be non-negative).
 */
void
Script_setCurFatigue(uint32_t serial, int stamina)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setCurFatigue");
	if (ent == NULL)
		return;
	if (stamina < 0)
		stamina = 0;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_STAMINA))(ent, stamina);
}

/*
 * 0x0041416B - setMaxFatigue
 *
 * Sets the mobile's maximum stamina.
 */
void
Script_setMaxFatigue(uint32_t serial, int maxstam)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setMaxFatigue");
	if (ent == NULL)
		return;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MAX_STAMINA))(ent, maxstam);
}

/*
 * 0x004141A1 - setCurMana
 *
 * Sets the mobile's current mana.
 */
void
Script_setCurMana(uint32_t serial, int mana)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setCurMana");
	if (ent == NULL)
		return;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MANA))(ent, mana);
}

/*
 * 0x004141D7 - setMaxMana
 *
 * Sets the mobile's maximum mana.
 */
void
Script_setMaxMana(uint32_t serial, int maxmana)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setMaxMana");
	if (ent == NULL)
		return;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MAX_MANA))(ent, maxmana);
}

/*
 * 0x0041420D - addHP
 *
 * Adds amount to the mobile's current HP and sends a stat update.
 */
void
Script_addHP(uint32_t serial, int amount)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "addHP");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	((uintptr_t (*)(void *, int, int))VT_FN(ent, VT_SET_HP))(mob, CMobile_GetHp(mob) + amount, 0);
	((void (*)(void *))VT_FN((CItem *)mob, VT_SEND_HP_UPDATE))(mob);
}

/*
 * 0x00414263 - addMana
 *
 * Adds amount to the mobile's current mana.
 */
void
Script_addMana(uint32_t serial, int amount)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "addMana");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MANA))(mob, CMobile_GetMana(mob) + amount);
}

/*
 * 0x004142A9 - addFatigue
 *
 * Adds amount to the mobile's current stamina.
 */
void
Script_addFatigue(uint32_t serial, int amount)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "addFatigue");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_STAMINA))(mob, CMobile_GetStamina(mob) + amount);
}

/*
 * 0x004142EF - setNaturalAC
 *
 * Sets the mobile's bonus armor class. For players, also re-sends a
 * status packet so the new AC reaches the client.
 */
void
Script_setNaturalAC(uint32_t serial, int ac)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setNaturalAC");
	if (ent == NULL)
		return;
	CMobile_SetBonusAC((CMobile *)ent, ac);
	if (VT_IsPlayer(ent))
		SendStatusToPlayer((CMobile *)ent, (CPlayer *)ent, ent->serial, 1);
}

/*
 * 0x00414347 - getNaturalAC
 *
 * Returns the mobile's bonus armor class.
 */
int
Script_getNaturalAC(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_GetBonusAC, "getNaturalAC");
}

/*
 * 0x00414362 - doDamageCore (internal, 573 bytes)
 *
 * Shared body for the doDamage* script handlers. Resolves attacker
 * and defender, tracks aggression, fires the washit (type 7) event,
 * optionally starts combat (CombatInitiate) when flag is set, and
 * finally calls Combat_DamageResolve. Re-resolves the script's
 * caller entity between steps and aborts the thread when it goes
 * away. Original binary bug: the weapon lookup passes defenderSerial
 * instead of weaponSerial; reproduced exactly.
 */
void
doDamageCore(uint32_t attackerSerial, uint32_t defenderSerial, uint32_t weaponSerial, int damage, int damageType, int flag)
{
	CItem *defender;
	CItem *attacker;
	CItem *weapon;
	CItem *callerEntity;
	uint32_t callerSerial;

	defender = FindMobileValidated(defenderSerial, "doDamage (defender)");
	attacker = FindEntityValidated(attackerSerial, 0);

	if (defender == NULL) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}

	if (VT_IsDead(defender))
		return;

	((void (*)(void *, CItem *))VT_FN(defender, VT_ADD_TO_ATTACKER_LIST))(defender, attacker);

	callerEntity = GetCurrentThreadEntity();
	if (callerEntity != NULL)
		callerSerial = CMobile_GetSerial((CMobile *)callerEntity);
	else
		callerSerial = 0;

	Entity_ExecuteEvent(&defender->resourceEntity.entity, 7, attackerSerial, (uint32_t)damage);

	// Re-resolve caller entity after event
	{
		CItem *reCaller = CWorld_FindBySerial(g_World, callerSerial);
		if (reCaller != callerEntity) {
			ThreadList_FinishCurrent(&g_activeThreadList, 0);
			return;
		}
	}

	if (attacker == NULL)
		goto weaponresolve;

	{
		CItem *reAttacker = FindEntityValidated(attackerSerial, 0);
		CItem *reDefender = FindMobileValidated(defenderSerial, 0);
		if (attacker != reAttacker || defender != reDefender)
			return;
	}

	if (flag == 0)
		goto third_resolve;
	if (!VT_IsMobile(attacker))
		goto third_resolve;

	{
		CItem *callerEntity2 = GetCurrentThreadEntity();
		uint32_t callerSerial2;
		if (callerEntity2 != NULL)
			callerSerial2 = CMobile_GetSerial((CMobile *)callerEntity2);
		else
			callerSerial2 = 0;

		CombatInitiate((CMobile *)attacker, (CMobile *)defender, 1);

		{
			CItem *reCaller2 = CWorld_FindBySerial(g_World, callerSerial2);
			if (reCaller2 != callerEntity2) {
				ThreadList_FinishCurrent(&g_activeThreadList, 0);
				return;
			}
		}
	}

third_resolve: {
	CItem *reAttacker = FindEntityValidated(attackerSerial, 0);
	CItem *reDefender = FindMobileValidated(defenderSerial, 0);
	if (attacker != reAttacker || defender != reDefender)
		return;
}

	if (g_ScriptReturnFlag == 1) {
		damage = g_ScriptReturnValue;
		g_ScriptReturnFlag = 0;
		g_ScriptReturnValue = 0;
	}

weaponresolve:
	// Binary bug: passes defenderSerial (ebp+0xC) instead of weaponSerial
	if (weaponSerial != 0)
		weapon = FindWeaponValidated(defenderSerial, "doDamageWithWeapon (weapon)");
	else
		weapon = NULL;

	Combat_DamageResolve((CMobile *)attacker, (CMobile *)defender, damage, weapon, damageType);
}

/*
 * 0x0041459F - doDamageWithWeapon
 *
 * Applies damage from attacker to defender with the given weapon
 * and combat initiation enabled.
 */
void
Script_doDamageWithWeapon(uint32_t attacker, uint32_t defender, uint32_t weapon, int damage)
{
	doDamageCore(attacker, defender, weapon, damage, 0, 1);
}

/*
 * 0x004145C0 - doDamage
 *
 * Applies damage from attacker to defender (no specific weapon)
 * with combat initiation enabled.
 */
void
Script_doDamage(uint32_t attacker, uint32_t defender, int damage)
{
	doDamageCore(attacker, defender, 0, damage, 0, 1);
}

/*
 * 0x004145DF - doDamageFight
 *
 * Applies damage from attacker to defender; flag controls whether
 * the hit also starts combat between them.
 */
void
Script_doDamageFight(uint32_t attacker, uint32_t defender, int damage, int flag)
{
	doDamageCore(attacker, defender, 0, damage, 0, flag);
}

/*
 * 0x00414600 - doDamageType
 *
 * Applies typed damage from attacker to defender (no specific
 * weapon) with combat initiation enabled.
 */
void
Script_doDamageType(uint32_t attacker, uint32_t defender, int damage, int type)
{
	doDamageCore(attacker, defender, 0, damage, type, 1);
}

/*
 * 0x00414621 - loseHP
 *
 * Inflicts amount of unattributed damage on the mobile.
 */
void
Script_loseHP(uint32_t serial, int amount)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "loseHP");
	if (ent == NULL)
		return;
	Combat_DamageResolve(NULL, (CMobile *)ent, amount, NULL, 0);
}

/*
 * 0x0041465B - loseMana
 *
 * Subtracts amount from the mobile's current mana.
 */
void
Script_loseMana(uint32_t serial, int amount)
{
	CItem *ent;
	CMobile *mob;
	int newMana;

	ent = FindMobileValidated(serial, "loseMana");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	newMana = CMobile_GetMana(mob) - amount;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MANA))(mob, newMana);
}

/*
 * 0x004146A1 - loseFatigue
 *
 * Subtracts amount from the mobile's current stamina.
 */
void
Script_loseFatigue(uint32_t serial, int amount)
{
	CItem *ent;
	CMobile *mob;
	int newStamina;

	ent = FindMobileValidated(serial, "loseFatigue");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	newStamina = CMobile_GetStamina(mob) - amount;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_STAMINA))(mob, newStamina);
}

/*
 * 0x004146E7 - restoreMobile
 *
 * Refills the mobile's HP, mana, and stamina to their maximums.
 */
void
Script_restoreMobile(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "restoreMobile");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	((uintptr_t (*)(void *, int, int))VT_FN(ent, VT_SET_HP))(mob, (int)CMobile_GetMaxHp(mob), 0);
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MANA))(mob, (int)CMobile_GetMaxMana(mob));
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_STAMINA))(mob, (int)CMobile_GetMaxStamina(mob));
}

/*
 * 0x00414752 - restoreFatigue
 *
 * Refills the mobile's stamina to its maximum.
 */
void
Script_restoreFatigue(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "walk");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_STAMINA))(mob, (int)CMobile_GetMaxStamina(mob));
}

/*
 * 0x0041478D - restoreMana
 *
 * Refills the mobile's mana to its maximum.
 */
void
Script_restoreMana(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "restoreMana");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	((uintptr_t (*)(void *, int))VT_FN(ent, VT_SET_MANA))(mob, (int)CMobile_GetMaxMana(mob));
}

/*
 * 0x004147C8 - restoreHP
 *
 * Refills the mobile's HP to its maximum.
 */
void
Script_restoreHP(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "walk");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	((uintptr_t (*)(void *, int, int))VT_FN(ent, VT_SET_HP))(mob, (int)CMobile_GetMaxHp(mob), 0);
}

/*
 * 0x00414805 - getHeShe
 *
 * Stores "he", "she", or "it" in out based on the mobile's sex.
 * Returns out.
 */
CString *
Script_getHeShe(CString *out, uint32_t serial)
{
	int sex;

	sex = Script_getSex(serial);
	switch (sex) {
	case 0:
		CString_Constructor(out, "he");
		break;
	case 1:
		CString_Constructor(out, "she");
		break;
	default:
		CString_Constructor(out, "it");
		break;
	}
	return out;
}

/*
 * 0x00414887 - getHimHer
 *
 * Stores "him", "her", or "it" in out based on the mobile's sex.
 * Returns out.
 */
CString *
Script_getHimHer(CString *out, uint32_t serial)
{
	int sex;

	sex = Script_getSex(serial);
	switch (sex) {
	case 0:
		CString_Constructor(out, "him");
		break;
	case 1:
		CString_Constructor(out, "her");
		break;
	default:
		CString_Constructor(out, "it");
		break;
	}
	return out;
}

/*
 * 0x00414909 - getHisHer
 *
 * Stores "his", "her", or "its" in out based on the mobile's sex.
 * Returns out.
 */
CString *
Script_getHisHer(CString *out, uint32_t serial)
{
	int sex;

	sex = Script_getSex(serial);
	switch (sex) {
	case 0:
		CString_Constructor(out, "his");
		break;
	case 1:
		CString_Constructor(out, "her");
		break;
	default:
		CString_Constructor(out, "its");
		break;
	}
	return out;
}

/*
 * 0x0041498B - attack
 *
 * Starts combat between attacker and victim.
 */
void
Script_attack(uint32_t attackerSerial, uint32_t victimSerial)
{
	CItem *attacker;
	CItem *victim;

	attacker = FindMobileValidated(attackerSerial, "attack (attacker)");
	victim = FindMobileValidated(victimSerial, "attack (victim)");
	if (attacker == NULL || victim == NULL)
		return;
	CombatInitiate((CMobile *)attacker, (CMobile *)victim, 1);
}

/*
 * 0x004149DD - Script handler for peace [612]
 *
 * Disengages every attacker from the mobile and clears its own
 * combat targets, ending all combat the mobile participates in.
 */
void
Script_peace(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "peace");
	if (ent == NULL)
		return;
	CMobile_DisengageAttackers((CMobile *)ent);
	CMobile_StopCombat((CMobile *)ent);
}

/*
 * 0x00414A11 - stopAttack
 *
 * Clears the mobile's combat target list (it stops attacking, but
 * other mobiles may still be attacking it).
 */
void
Script_stopAttack(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "stopAttack");
	if (ent == NULL)
		return;
	CMobile_StopCombat((CMobile *)ent);
}

/*
 * 0x00414A3D - Script handler for stopFight [614]
 *
 * Removes mob2 from mob1's combat target list, leaving the
 * attacker list untouched.
 *
 * FIXED: the binary uses both lookups without testing either, so a
 * serial that names no mobile faults - first reading mob2's serial
 * field, then dispatching on mob1. Both are checked here.
 */
void
Script_stopFight(uint32_t serial1, uint32_t serial2)
{
	CItem *mob1, *mob2;

	mob1 = FindMobileValidated(serial1, "stopFight (one)");
	mob2 = FindMobileValidated(serial2, "stopFight (two)");
	if (mob1 == NULL || mob2 == NULL)
		return;
	CMobile_StopFightWith((CMobile *)mob1, CMobile_GetSerial((CMobile *)mob2), 0);
}

/*
 * 0x00414A82 - getAttackersNearby [620]
 *
 * Collects the serials of attackers tracked by every mobile within
 * range 8 of mob and appends them to list. Original binary bug: the
 * inner walk's end iterator references mob's own attacker list
 * instead of the nearby mobile's; reproduced exactly.
 */
void
Script_getAttackersNearby(CList *list, uint32_t serial)
{
	CItem *mob;
	CVector nearbyList;
	void *iter;
	char typeFlag = 0;
	CLocation *loc;

	CList_Clear(list);
	mob = FindMobileValidated(serial, "getAttackersNearby");
	if (mob == NULL)
		return;
	loc = CEntity_GetLocation(&mob->resourceEntity.entity);
	CVector_Constructor(&nearbyList, &typeFlag);
	CollectNearbyMobiles(&nearbyList, loc, 8);
	iter = nearbyList.begin;
	while (iter != nearbyList.end) {
		CItem *nearbyMob = *(CItem **)iter;
		StdPtrNode *siter, *sbegin, *send, *sendCopy, *spostInc, *scopy;
		StdPtrIter_Constructor(&siter);
		StdPtrIter_CopyConstructor(&scopy, StdPtrList_Begin((StdPtrList *)&((CMobile *)nearbyMob)->attackerList, &sbegin));
		siter = scopy;
		for (;;) {
			StdPtrIter_CopyConstructor(&sendCopy, StdPtrList_End((StdPtrList *)&((CMobile *)mob)->attackerList, &send));
			if (!(StdPtrIter_Neq(&siter, &sendCopy) & 0xFF))
				break;
			CList_Append(list, 4, (uintptr_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&siter)));
			StdPtrIter_PostInc(&siter, &spostInc, 0);
		}
		iter = (char *)iter + sizeof(uintptr_t);
	}
	CVector_Destructor(&nearbyList);
}

/*
 * 0x00414BC0 - getAttackers
 *
 * Appends every serial in the mobile's attacker list to list.
 */
void
Script_getAttackers(CList *list, uint32_t serial)
{
	CItem *ent;
	StdPtrNode *iter, *copyIter, *beginTemp, *endTemp, *endCopy, *postIncTemp;

	CList_Clear(list);
	ent = FindMobileValidated(serial, "getAttackers");
	if (ent == NULL)
		return;
	StdPtrIter_Constructor(&iter);
	StdPtrIter_CopyConstructor(&copyIter, StdPtrList_Begin((StdPtrList *)&((CMobile *)ent)->attackerList, &beginTemp));
	iter = copyIter;
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End((StdPtrList *)&((CMobile *)ent)->attackerList, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		CList_Append(list, 4, (uintptr_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&iter)));
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * 0x00414C76 - getNumTargets
 *
 * Returns the number of entries in the mobile's combat target list.
 */
int
Script_getNumTargets(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getNumTargets");
	if (ent == NULL)
		return 0;
	return ((CMobile *)ent)->combatTargetList.count;
}

/*
 * 0x00414CAA - getNumAttackers
 *
 * Returns the number of entries in the mobile's attacker list.
 */
int
Script_getNumAttackers(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getNumTargets");
	if (ent == NULL)
		return 0;
	return ((CMobile *)ent)->attackerList.count;
}

/*
 * 0x00414CDE - Script handler for getFirstTarget [623]
 *
 * Returns the serial of the first entry in the mobile's combat
 * target list, or 0 when the list is empty.
 */
uint32_t
Script_getFirstTarget(uint32_t serial)
{
	CItem *ent;
	StdPtrNode *beginIter, *endIter, *beginIter2;

	ent = FindMobileValidated(serial, "getFirstTarget");
	if (ent == NULL)
		return 0;
	StdPtrList_End((StdPtrList *)&((CMobile *)ent)->combatTargetList, &endIter);
	if (StdPtrIter_Eq(StdPtrList_Begin((StdPtrList *)&((CMobile *)ent)->combatTargetList, &beginIter), &endIter) & 0xFF)
		return 0;
	return CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(StdPtrList_Begin((StdPtrList *)&((CMobile *)ent)->combatTargetList, &beginIter2)));
}

/*
 * 0x00414D5F - getTargets
 *
 * Appends every serial in the mobile's combat target list to list.
 */
void
Script_getTargets(CList *list, uint32_t serial)
{
	CItem *ent;
	StdPtrNode *iter, *copyIter, *beginTemp, *endTemp, *endCopy, *postIncTemp;

	CList_Clear(list);
	ent = FindMobileValidated(serial, "getTargets");
	if (ent == NULL)
		return;
	StdPtrIter_Constructor(&iter);
	StdPtrIter_CopyConstructor(&copyIter, StdPtrList_Begin((StdPtrList *)&((CMobile *)ent)->combatTargetList, &beginTemp));
	iter = copyIter;
	for (;;) {
		StdPtrIter_CopyConstructor(&endCopy, StdPtrList_End((StdPtrList *)&((CMobile *)ent)->combatTargetList, &endTemp));
		if (!(StdPtrIter_Neq(&iter, &endCopy) & 0xFF))
			break;
		CList_Append(list, 4, (uintptr_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&iter)));
		StdPtrIter_PostInc(&iter, &postIncTemp, 0);
	}
}

/*
 * 0x00414E15 - hasObjEquipped
 *
 * Returns 1 when the mobile has the named item equipped in any slot.
 */
int
Script_hasObjEquipped(uint32_t mobSerial, uint32_t objSerial)
{
	CItem *ent;
	CMobile *mob;
	int i;

	ent = FindMobileValidated(mobSerial, "hasObjEquipped");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	ent = FindEntityValidated(objSerial, "hasObjEquipped");
	if (ent == NULL)
		return 0;
	for (i = 0; i < 0x1a; i++) {
		if (mob->equipment[i] != NULL) {
			if (mob->equipment[i]->serial == objSerial)
				return 1;
		}
	}
	return 0;
}

/*
 * 0x00414EA3 - hasObjTypeEquipped
 *
 * Returns 1 when the mobile has any item of the given body type
 * equipped.
 */
int
Script_hasObjTypeEquipped(uint32_t serial, int type)
{
	CItem *ent;
	CMobile *mob;
	int i;

	ent = FindMobileValidated(serial, "hasObjTypeEquipped");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	if (type > 0x4000)
		return 0;
	for (i = 0; i < 0x1a; i++) {
		if (mob->equipment[i] != NULL) {
			if ((CEntity_GetBodyType(mob->equipment[i]) & 0xFFFF) == type)
				return 1;
		}
	}
	return 0;
}

/*
 * 0x00414F27 - getEquipSlot
 *
 * Returns the entity's natural equipment layer; returns 0 for
 * mobiles.
 */
int
Script_getEquipSlot(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getEquipSlot");
	if (ent == NULL)
		return 0;
	if (VT_IsMobile(ent))
		return 0;
	return CItem_GetEquipSlot(ent) & 0xFF;
}

/*
 * 0x00414F70 - getItemAtSlot (internal helper)
 *
 * Returns the serial of the item at the given equipment slot on a
 * mobile. Used internally by putObjBank. Returns 0 if not found.
 */

/*
 * 0x00414F70 - getItemAtSlot
 *
 * Returns the serial of the item equipped on the mobile in slot,
 * or 0 when the slot is empty or out of range [1, 30].
 */
uint32_t
Script_getItemAtSlot(uint32_t serial, int slot)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "getItemAtSlot");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	if (slot < 1 || slot > 0x1e)
		return 0;
	if (mob->equipment[slot] == NULL)
		return 0;
	return mob->equipment[slot]->serial;
}

/*
 * 0x00414FCA - getWeapon
 *
 * Returns the serial of the weapon currently wielded by the mobile,
 * or 0 when none.
 */
uint32_t
Script_getWeapon(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;
	CItem *weapon;

	ent = FindMobileValidated(serial, "getWeapon");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	weapon = CMobile_GetWeapon(mob);
	if (weapon != NULL)
		return weapon->serial;
	return 0;
}

/*
 * 0x00415018 - getFreeHandSlot
 *
 * Returns 1 (right hand) or 2 (left hand) for the first empty hand
 * slot on the mobile, or 0 when both hands are occupied.
 */
int
Script_getFreeHandSlot(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;
	CItem *weapon2;

	ent = FindMobileValidated(serial, "getFreeHandSlot");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	weapon2 = mob->equipment[2];

	if (mob->equipment[1] == NULL) {
		if (weapon2 == NULL)
			return 1;
		if (!check_IsWeapon(weapon2))
			return 1;
		if (!CItem_IsReallyWeapon(weapon2))
			return 1;
		// fall through: slot 1 empty but slot 2 has real weapon
	}

	// slot 1 occupied (or slot 2 has real weapon): check slot 2
	if (weapon2 == NULL)
		return 2;
	return 0;
}

/*
 * 0x00415092 - isArmed
 *
 * Returns 1 when the mobile currently wields a weapon.
 */
int
Script_isArmed(uint32_t serial)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "isArmed");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	return CMobile_GetWeapon(mob) != NULL ? 1 : 0;
}

static int
check_IsWeapon(CItem *ent)
{
	return VT_IsWeapon(ent);
} // 0x004243A0

/*
 * 0x004150C6 - getYear
 *
 * Script binding: returns the in-game year counter.
 */
int
Script_getYear(void)
{
	return g_GameYear;
}

/*
 * 0x004150D0 - getMonth
 *
 * Script binding: returns the in-game month counter.
 */
int
Script_getMonth(void)
{
	return g_GameMonth;
}

/*
 * 0x004150DA - getWeek
 *
 * Script binding: returns the in-game week counter.
 */
int
Script_getWeek(void)
{
	return g_GameWeek;
}

/*
 * 0x004150E4 - getDay
 *
 * Script binding: returns the in-game day counter.
 */
int
Script_getDay(void)
{
	return g_GameDay;
}

/*
 * 0x004150EE - getHour
 *
 * Script binding: returns the in-game hour counter.
 */
int
Script_getHour(void)
{
	return g_GameHour;
}

/*
 * 0x004150F8 - getMinute
 *
 * Script binding: returns the in-game minute counter.
 */
int
Script_getMinute(void)
{
	return g_GameMinute;
}

/*
 * 0x00415102 - getSeconds
 *
 * Script binding: returns the in-game seconds counter.
 */
int
Script_getSeconds(void)
{
	return g_GameSeconds;
}

/*
 * 0x0041510C - isWeapon
 *
 * Returns 1 when the entity is a weapon item.
 */
int
Script_isWeapon(uint32_t serial)
{
	return Script_checkEntity(serial, check_IsWeapon, "isWeapon");
}

/*
 * 0x00415127 - isReallyWeapon
 *
 * Returns 1 only when the entity is a hand-slot weapon with melee
 * or ranged damage flags (excluding shields).
 */
int
Script_isReallyWeapon(uint32_t serial)
{
	CItem *ent;
	int result;

	ent = FindWeaponValidated(serial, NULL);
	if (ent == NULL)
		return 0;
	if (CItem_IsReallyWeapon(ent))
		result = 1;
	else
		result = 0;
	return result;
}

/*
 * 0x00415167 - handleWatchingSkill
 *
 * Awards passive observation gain on skillId for the mobile.
 */
void
Script_handleWatchingSkill(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "handleWatchingSkill");
	if (ent == NULL)
		return;
	CMobile_HandleWatchingSkill((CMobile *)ent, (int8_t)skillId, 100);
}

/*
 * 0x00415199 - testSkillReal
 *
 * Returns the raw skill-check result for the mobile using skillId.
 */
int
Script_testSkillReal(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "testSkill");
	if (ent == NULL)
		return 0;
	return CMobile_DirectUse((CMobile *)ent, (int8_t)skillId);
}

/*
 * 0x004151CF - testSkill
 *
 * Returns 1 when a default skill check on skillId succeeds for
 * the mobile, 0 otherwise.
 */
int
Script_testSkill(uint32_t serial, int skillId)
{
	CItem *ent;
	int result;

	ent = FindMobileValidated(serial, "testSkill");
	if (ent == NULL)
		return 0;
	result = CMobile_DirectUse((CMobile *)ent, (int8_t)skillId);
	return result > 0 ? 1 : 0;
}

/*
 * 0x0041520E - getSkillSuccessChance
 *
 * Returns the success chance for the mobile attempting skillId at
 * the given difficulty and range. Difficulty must be in [-1000,
 * 2000] and range in [10, 100], otherwise the result is clamped
 * to 0/1.
 */
int
Script_getSkillSuccessChance(uint32_t serial, int skillId, int difficulty, int range)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "testSkill");
	if (ent == NULL)
		return 0;
	if (difficulty < (int)0xfffffc18)
		return 1;
	if (difficulty > 0x7d0)
		return 0;
	if (range < 0xa || range > 0x64)
		return 0;
	if (!CSkillManager_HasSkill(&g_SkillManager, skillId))
		return 0;
	return CMobile_CalcChance((CMobile *)ent, (int8_t)skillId, difficulty, range);
}

/*
 * 0x0041528B - testAndLearnSkill
 *
 * Performs a learning skill check with the given difficulty and
 * range, awarding skill gain on use. Difficulty must be in [-1000,
 * 2000] and range in [10, 100].
 */
int
Script_testAndLearnSkill(uint32_t serial, int skillId, int difficulty, int range)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "testSkill");
	if (ent == NULL)
		return 0;
	if (difficulty < (int)0xfffffc18)
		return 1;
	if (difficulty > 0x7d0)
		return 0;
	if (range < 0xa || range > 0x64)
		return 0;
	if (!CSkillManager_HasSkill(&g_SkillManager, skillId))
		return 0;
	return CMobile_SkillCheck((CMobile *)ent, (int8_t)skillId, difficulty, range, 0, 100, 1);
}

/*
 * 0x0041530E - addSatiety
 *
 * Adds amount to the mobile's hunger byte unless it has already hit
 * the cap of 100.
 */
void
Script_addSatiety(uint32_t serial, int amount)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "addSatiety");
	if (ent == NULL)
		return;
	mob = (CMobile *)ent;
	if ((int)(unsigned char)mob->hunger >= 100)
		return;
	mob->hunger = (uint8_t)(mob->hunger + (uint8_t)amount);
}

/*
 * 0x00415357 - getSatiety
 *
 * Returns the mobile's hunger byte.
 */
int
Script_getSatiety(uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (!VT_IsMobile(ent))
		return 0;
	return (int)(((CMobile *)ent)->hunger);
}

/*
 * 0x00415399 - getRealStrength
 *
 * Returns the mobile's base strength stat.
 */
int
Script_getRealStrength(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getRealStrength");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetBaseStat((CMobile *)ent, 0);
}

/*
 * 0x004153CC - getRealDexterity
 *
 * Returns the mobile's base dexterity stat.
 */
int
Script_getRealDexterity(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getRealDexterity");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetBaseStat((CMobile *)ent, 1);
}

/*
 * 0x004153FF - getRealIntelligence
 *
 * Returns the mobile's base intelligence stat.
 */
int
Script_getRealIntelligence(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getRealIntelligence");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetBaseStat((CMobile *)ent, 2);
}

/*
 * 0x00415432 - getStrength
 *
 * Returns the mobile's effective strength stat (base + bonus).
 */
int
Script_getStrength(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getStrength");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetStat((CMobile *)ent, 0);
}

/*
 * 0x00415465 - getDexterity
 *
 * Returns the mobile's effective dexterity stat (base + bonus).
 */
int
Script_getDexterity(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getDexterity");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetStat((CMobile *)ent, 1);
}

/*
 * 0x00415498 - getIntelligence
 *
 * Returns the mobile's effective intelligence stat (base + bonus).
 */
int
Script_getIntelligence(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getIntelligence");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetStat((CMobile *)ent, 2);
}

/*
 * 0x004154CB - getStat
 *
 * Returns the mobile's effective value (base + bonus) for statId.
 */
int
Script_getStat(uint32_t serial, int statId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getStat");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetStat((CMobile *)ent, statId);
}

/*
 * 0x00415500 - getRealStat
 *
 * Returns the mobile's base value for statId.
 */
int
Script_getRealStat(uint32_t serial, int statId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getRealStat");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetBaseStat((CMobile *)ent, statId);
}

/*
 * 0x00415535 - getStatMod
 *
 * Returns the mobile's bonus modifier for statId.
 */
int
Script_getStatMod(uint32_t serial, int statId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getStatMod");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetStatBonus((CMobile *)ent, statId);
}

/*
 * 0x0041556A - modifyStat
 *
 * Adds value to the mobile's bonus modifier for statId and returns
 * the new bonus.
 */
int
Script_modifyStat(uint32_t serial, int statId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "modifyStat");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_AddToStatBonus((CMobile *)ent, statId, (int16_t)value);
}

/*
 * 0x004155AA - modifyRealStat
 *
 * Adds value to the mobile's base value for statId and returns the
 * new base.
 */
int
Script_modifyRealStat(uint32_t serial, int statId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "modifyRealStat");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_AddToBaseStat((CMobile *)ent, statId, (int16_t)value);
}

/*
 * 0x004155EA - setStatMod
 *
 * Sets the mobile's bonus modifier for statId to value and returns
 * the new bonus.
 */
int
Script_setStatMod(uint32_t serial, int statId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "modifyStat");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_SetStatBonus((CMobile *)ent, statId, (int16_t)value);
}

/*
 * 0x0041562A - setRealStat
 *
 * Sets the mobile's base value for statId to value and returns the
 * new base.
 */
int
Script_setRealStat(uint32_t serial, int statId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "modifyRealStat");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_SetBaseStat((CMobile *)ent, statId, (uint16_t)(int16_t)value);
}

/*
 * 0x0041566A - getStatAttributeMax
 *
 * Returns max HP, stamina, or mana for statId 0, 1, or 2 (anything
 * else returns 0).
 */
int
Script_getStatAttributeMax(uint32_t serial, int statId)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "getStatAttribute");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	switch (statId) {
	case 0:
		return (int)CMobile_GetMaxHp(mob);
	case 1:
		return (int)CMobile_GetMaxStamina(mob);
	case 2:
		return (int)CMobile_GetMaxMana(mob);
	default:
		return 0;
	}
}

/*
 * 0x004156CC - setStatAttributeMax
 *
 * Sets max HP, stamina, or mana for statId 0, 1, or 2, and returns
 * the value stored. Other statIds return 0.
 */
int
Script_setStatAttributeMax(uint32_t serial, int statId, int value)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(serial, "setStatAttribute");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	switch (statId) {
	case 0:
		return ((int (*)(void *, int))VT_FN(ent, VT_SET_MAX_HP))(mob, value);
	case 1:
		return ((int (*)(void *, int))VT_FN(ent, VT_SET_MAX_STAMINA))(mob, value);
	case 2:
		return ((int (*)(void *, int))VT_FN(ent, VT_SET_MAX_MANA))(mob, value);
	default:
		return 0;
	}
}

/*
 * 0x0041574C - setSkillLevel (value-returning twin)
 *
 * Sets the mobile's base value for skillId and returns what was
 * stored. Script_setSkillLevel at 0x004158FD is the same binding with
 * the result discarded; the two share the script name but not the
 * string literal, so this one keeps its address in the C name.
 */
static __attribute__((unused)) int
Script_setSkillLevel_41574C(uint32_t serial, int skillId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setSkillLevel");
	if (ent == NULL)
		return 0;
	return CMobile_SetSkill((CMobile *)ent, (int8_t)skillId, (uint16_t)value);
}

/*
 * 0x00415783 - addSkillLevel (value-returning twin)
 *
 * Adds delta to the mobile's base value for skillId and returns the
 * amount actually applied. Script_addSkillLevel at 0x00415932 is the
 * same binding with the result discarded.
 */
static __attribute__((unused)) int
Script_addSkillLevel_415783(uint32_t serial, int skillId, int delta)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "addSkillLevel");
	if (ent == NULL)
		return 0;
	return CMobile_AddToSkill((CMobile *)ent, (int8_t)skillId, delta);
}

/*
 * 0x004157B9 - loseSkillLevel (value-returning twin)
 *
 * Subtracts delta from the mobile's base value for skillId and returns
 * the amount actually removed, both negated so the caller passes and
 * receives a positive loss. Script_loseSkillLevel at 0x00415966 is the
 * same binding with the result discarded and no negation on the way
 * out.
 */
static __attribute__((unused)) int
Script_loseSkillLevel_4157B9(uint32_t serial, int skillId, int delta)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "loseSkillLevel");
	if (ent == NULL)
		return 0;
	return -CMobile_AddToSkill((CMobile *)ent, (int8_t)skillId, -delta);
}

/*
 * 0x004157F5 - getSkillLevel
 *
 * Returns the mobile's skill value for skillId divided by 10 (the
 * coarse "tens of skill" representation used by scripts).
 */
int
Script_getSkillLevel(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getSkillLevel");
	if (ent == NULL)
		return 0;
	return CMobile_GetSkillValue((CMobile *)ent, (int8_t)skillId, 0) / 10;
}

/*
 * 0x00415831 - getSkillLevelReal
 *
 * Returns the mobile's effective skill value for skillId without
 * scaling.
 */
int
Script_getSkillLevelReal(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getSkillLevel");
	if (ent == NULL)
		return 0;
	return CMobile_GetSkillValue((CMobile *)ent, (int8_t)skillId, 0);
}

/*
 * 0x00415865 - getSkillLevelRealStat
 *
 * Identical to getSkillLevelReal: returns the mobile's effective
 * skill value for skillId without scaling.
 */
int
Script_getSkillLevelRealStat(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getSkillLevel");
	if (ent == NULL)
		return 0;
	return CMobile_GetSkillValue((CMobile *)ent, (int8_t)skillId, 0);
}

/*
 * 0x00415899 - getSkillLevelNoStat
 *
 * Returns the mobile's skill value (base + bonus) without the
 * usual stat blending.
 */
int
Script_getSkillLevelNoStat(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getSkillNoLevel");
	if (ent == NULL)
		return 0;
	return CMobile_GetTotalSkill((CMobile *)ent, (int8_t)skillId);
}

/*
 * 0x004158CB - getSkillLevelNoStatNoMod
 *
 * Returns the mobile's raw base skill value with no bonuses or
 * stat blending applied.
 */
int
Script_getSkillLevelNoStatNoMod(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getSkillNoLevel");
	if (ent == NULL)
		return 0;
	return CMobile_GetBaseSkill((CMobile *)ent, (int8_t)skillId);
}

/*
 * 0x004158FD - setSkillLevel
 *
 * Sets the mobile's base value for skillId.
 */
void
Script_setSkillLevel(uint32_t serial, int skillId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setSkillLevel");
	if (ent == NULL)
		return;
	CMobile_SetSkill((CMobile *)ent, (int8_t)skillId, (uint16_t)value);
}

/*
 * 0x00415932 - addSkillLevel
 *
 * Adds delta to the mobile's base value for skillId.
 */
void
Script_addSkillLevel(uint32_t serial, int skillId, int delta)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "addSkillLevel");
	if (ent == NULL)
		return;
	CMobile_AddToSkill((CMobile *)ent, (int8_t)skillId, delta);
}

/*
 * 0x00415966 - loseSkillLevel
 *
 * Subtracts delta from the mobile's base value for skillId.
 */
void
Script_loseSkillLevel(uint32_t serial, int skillId, int delta)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "loseSkillLevel");
	if (ent == NULL)
		return;
	CMobile_AddToSkill((CMobile *)ent, (int8_t)skillId, delta * -1);
}

/*
 * 0x0041599D - getSkillMod
 *
 * Returns the mobile's bonus modifier for skillId.
 */
int
Script_getSkillMod(uint32_t serial, int skillId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getSkillMod");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_GetSkillBonus((CMobile *)ent, (int8_t)skillId);
}

/*
 * 0x004159D2 - setSkillMod
 *
 * Sets the mobile's bonus modifier for skillId and returns the new
 * bonus.
 */
int
Script_setSkillMod(uint32_t serial, int skillId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setSkillMod");
	if (ent == NULL)
		return 0;
	return (int)(int16_t)CMobile_SetSkillBonus((CMobile *)ent, (int8_t)skillId, (int16_t)value);
}

/*
 * 0x00415A0C - modifySkillMod
 *
 * Adds delta to the mobile's bonus modifier for skillId and returns
 * the new bonus.
 */
int
Script_modifySkillMod(uint32_t serial, int skillId, int delta)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "modifySkillMod");
	if (ent == NULL)
		return 0;
	return CMobile_ModifySkillBonus((CMobile *)ent, (int8_t)skillId, delta);
}

/*
 * 0x00415A42 - getSkillName
 *
 * Stores the display name of skillId in out and returns out.
 */
CString *
Script_getSkillName(CString *out, int skillId)
{
	const char *name;

	name = CSkillManager_GetSkillName(&g_SkillManager, (int8_t)skillId);
	CString_Constructor(out, name);
	return out;
}

/*
 * 0x00415A74 - getSkillNumber
 *
 * Looks up the skill index for skillName, returning -1 when no
 * skill matches.
 */
int
Script_getSkillNumber(CString *skillName)
{
	char *buf;

	buf = CString_GetBuffer(skillName);
	return SkillManager_GetSkillNumber(buf);
}

/*
 * 0x00415A8A - setMobFlag
 *
 * Sets or clears a status flag on the mobile.
 */
void
Script_setMobFlag(uint32_t serial, int flagId, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setMobFlag");
	if (ent == NULL)
		return;
	CMobile_SetStatusFlag((CMobile *)ent, (uint8_t)flagId, value);
}

/*
 * 0x00415ABE - getMobFlag
 *
 * Returns 1 when the named status flag is set on the mobile.
 */
int
Script_getMobFlag(uint32_t serial, int flagId)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "getMobFlag");
	if (ent == NULL)
		return 0;
	return CMobile_CheckStatusFlag((CMobile *)ent, (uint8_t)flagId);
}

/*
 * Logout, Status, Sequence, Map, Name, and Misc Handlers
 */

/*
 * 0x00415AF0 - canWield [583]
 *
 * Returns 1 when mob can equip the weapon in its natural slot.
 */
int
Script_canWield(uint32_t mobSerial, uint32_t itemSerial)
{
	CItem *mob;
	CItem *item;
	uint8_t layer;

	mob = FindMobileValidated(mobSerial, "canWield");
	if (mob == NULL)
		return 0;
	item = FindWeaponValidated(itemSerial, "canWield");
	if (item == NULL)
		return 0;
	layer = CItem_GetEquipSlot(item) & 0xFF;
	return CItem_CanWield(item, mob, layer);
}

/*
 * 0x00415B56 - isPiercing
 *
 * Checks weapon's typeFlags bit 0x02 (piercing) via weapon template.
 */
int
Script_isPiercing(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "isPiercing");

	if (ent == NULL)
		return 0;
	return CItem_IsPiercing(ent);
}

/*
 * 0x00415B84 - isSlashing
 *
 * Checks weapon's typeFlags bit 0x01 (slashing) via weapon template.
 */
int
Script_isSlashing(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "isSlashing");

	if (ent == NULL)
		return 0;
	return CItem_IsSlashing(ent);
}

/*
 * 0x00415BB2 - isBashing
 *
 * Returns 1 when the weapon's typeFlags include bashing.
 */
int
Script_isBashing(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "isBashing");
	if (ent == NULL)
		return 0;
	return CItem_IsBashing(ent);
}

/*
 * 0x00415BE0 - isRanged
 *
 * Returns 1 when the weapon is a ranged type (bow / crossbow).
 */
int
Script_isRanged(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "isRanged");

	if (ent == NULL)
		return 0;
	return CItem_IsRangedWeapon(ent);
}

/*
 * 0x00415C0E - getMaxArmorClass
 *
 * Returns the weapon's maximum armor rating, or -1 when invalid.
 */
int
Script_getMaxArmorClass(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getMaxArmorClass");
	if (ent == NULL)
		return -1;
	return (int)CWeapon_GetMaxAC(ent) & 0xff;
}

/*
 * 0x00415C42 - getCurArmorClass
 *
 * Returns the weapon's effective armor rating (scaled by current
 * durability), or -1 when invalid.
 */
int
Script_getCurArmorClass(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getCurArmorClass");
	if (ent == NULL)
		return -1;
	return (int)CItem_GetArmorRating(ent) & 0xff;
}

/*
 * 0x00415C76 - setMaxArmorClass
 *
 * Sets the weapon's maximum armor rating, returning 1 on success.
 */
int
Script_setMaxArmorClass(uint32_t serial, int maxAC)
{
	CItem *ent = FindWeaponValidated(serial, "setMaxArmorClass");
	if (ent == NULL)
		return 0;
	CWeapon_SetMaxAC(ent, (uint8_t)maxAC);
	return 1;
}

/*
 * 0x00415CAD - getWeaponCurHP
 *
 * Returns the weapon's current durability, or -1 when invalid.
 */
int
Script_getWeaponCurHP(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponCurHP");
	if (ent == NULL)
		return -1;
	return (int)CWeapon_GetCurHP(ent) & 0xff;
}

/*
 * 0x00415CE1 - setWeaponCurHP
 *
 * Sets the weapon's current durability, returning 1 on success.
 */
int
Script_setWeaponCurHP(uint32_t serial, int curHP)
{
	CItem *ent = FindWeaponValidated(serial, "setWeaponCurHP");
	if (ent == NULL)
		return 0;
	CWeapon_SetCurHP(ent, (uint8_t)curHP);
	return 1;
}

/*
 * 0x00415D18 - getWeaponMaxHP
 *
 * Returns the weapon's maximum durability, or -1 when invalid.
 */
int
Script_getWeaponMaxHP(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponMaxHP");
	if (ent == NULL)
		return -1;
	return (int)CWeapon_GetMaxHP(ent) & 0xff;
}

/*
 * 0x00415D4C - setWeaponMaxHP
 *
 * Sets the weapon's maximum durability, returning 1 on success.
 */
int
Script_setWeaponMaxHP(uint32_t serial, int maxHP)
{
	CItem *ent = FindWeaponValidated(serial, "setWeaponMaxHP");
	if (ent == NULL)
		return 0;
	CWeapon_SetMaxHP(ent, (uint8_t)maxHP);
	return 1;
}

/*
 * 0x00415D83 - Script handler for getWeaponMinStr [604]
 *
 * Returns the weapon's minimum strength requirement, or -1 when
 * invalid.
 */
int
Script_getWeaponMinStr(uint32_t serial)
{
	CItem *ent;

	ent = FindWeaponValidated(serial, "getWeaponMinStr");
	if (ent == NULL)
		return -1;
	return (int)CItem_GetStrengthNeeded(ent) & 0xff;
}

/*
 * 0x00415DB7 - getWeaponSpeed
 *
 * Returns the weapon's speed (uint8_t from weapon definition).
 * Returns -10 (0xFFFFFFF6) if weapon is invalid.
 */
int
Script_getWeaponSpeed(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponSpeed");
	if (ent == NULL)
		return -10;
	return (int)CItem_GetSpeed(ent) & 0xff;
}

/*
 * 0x00415DED - getWeaponHitSfx
 *
 * Returns the weapon's hit sound effect (uint16_t from weapon definition).
 * Returns 0 if weapon is invalid.
 */
int
Script_getWeaponHitSfx(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponHitSfx");
	if (ent == NULL)
		return 0;
	return (int)CItem_GetHitSfx(ent) & 0xffff;
}

/*
 * 0x00415E20 - getWeaponMissSfx
 *
 * Returns the weapon's miss sound effect (uint16_t from weapon definition).
 * Returns 0 if weapon is invalid.
 */
int
Script_getWeaponMissSfx(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponMissSfx");
	if (ent == NULL)
		return 0;
	return (int)CItem_GetMissSfx(ent) & 0xffff;
}

/*
 * 0x00415E53 - getWeaponMinRange
 *
 * Returns the weapon's minimum range (uint8_t from weapon definition).
 * Returns -1 if weapon is invalid.
 */
int
Script_getWeaponMinRange(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponMinRange");
	if (ent == NULL)
		return -1;
	return (int)CItem_GetMinRange(ent) & 0xff;
}

/*
 * 0x00415E87 - getAmmoType
 *
 * Returns the weapon's ammo type (uint16_t from weapon definition).
 * Returns -1 if weapon is invalid.
 */
int
Script_getAmmoType(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getAmmoType");
	if (ent == NULL)
		return -1;
	return (int)CItem_GetAmmoType(ent) & 0xffff;
}

/*
 * 0x00415EBB - Script handler for getBow [591]
 *
 * Returns the weapon's bow type code, or -1 when invalid.
 */
int
Script_getBow(uint32_t serial)
{
	CItem *ent;

	ent = FindWeaponValidated(serial, "getBow");
	if (ent == NULL)
		return -1;
	return (int)CItem_GetBow(ent) & 0xff;
}

/*
 * 0x00415EEF - getWeaponRange
 *
 * Returns the weapon's melee range (uint8_t from weapon definition).
 * Returns -1 if weapon is invalid.
 */
int
Script_getWeaponRange(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponRange");
	if (ent == NULL)
		return -1;
	return (int)CItem_GetMeleeRange(ent) & 0xff;
}

/*
 * 0x00415F23 - getWeaponHandedness
 *
 * Returns weapon's handedness value via weapon template.
 */
int
Script_getWeaponHandedness(uint32_t serial)
{
	CItem *ent = FindWeaponValidated(serial, "getWeaponHandedness");

	if (ent == NULL)
		return 0;
	return (int)CItem_GetHandedness(ent) & 0xff;
}

/*
 * 0x00415F56 - getWeaponName
 *
 * Stores the weapon's display name in out, or an empty string when
 * the weapon is invalid. Returns out.
 */
CString *
Script_getWeaponName(CString *out, uint32_t weaponSerial)
{
	CItem *weapon;

	weapon = FindWeaponValidated(weaponSerial, "getWeaponName");
	if (weapon == NULL) {
		CString_Constructor(out, "");
		return out;
	}
	CString_Constructor(out, VT_GetName(weapon));
	return out;
}

/*
 * 0x00415FBE - Script handler for getWeaponClass [598]
 *
 * Reads the damage dice from a weapon, or the armor-rating dice from
 * a mobile, and unpacks the numDice/diceFaces/bonus/pad components
 * into the four out parameters.
 */
void
Script_getWeaponClass(uint32_t serial, int *outNumDice, int *outDiceFaces, int *outBonus, int *outPad)
{
	CItem *ent;
	CDiceRoll dice;
	CWeaponDice *src;

	ent = FindEntityValidated(serial, "getWeaponClass");
	CDiceRoll_Constructor((CWeaponDice *)&dice);
	if (ent == NULL)
		goto output;
	if (VT_IsWeapon(ent)) {
		src = CWeapon_GetDamageDice(ent);
		CDiceRoll_Copy((CWeaponDice *)&dice, src);
		goto output;
	}
	if (VT_IsMobile(ent)) {
		src = CMobile_GetArmorRating((CMobile *)ent);
		CDiceRoll_Copy((CWeaponDice *)&dice, src);
	}
output:
	*outNumDice = (int)(int8_t)CDiceRoll_GetNumDice((CWeaponDice *)&dice);
	*outDiceFaces = CDiceRoll_GetDiceFaces((CWeaponDice *)&dice) & 0xFF;
	*outBonus = (int)(int8_t)CDiceRoll_GetBonus((CWeaponDice *)&dice);
	*outPad = CDiceRoll_GetField4((CWeaponDice *)&dice) & 0xFF;
}

/*
 * 0x00416076 - Script handler for setWeaponClass [599]
 *
 * Builds a CDiceRoll from numDice/diceFaces/bonus and stores it as
 * the weapon's damage dice or the mobile's armor-rating dice.
 */
void
Script_setWeaponClass(uint32_t serial, int numDice, int diceFaces, int bonus, int pad)
{
	CItem *ent;
	CDiceRoll dice;

	ent = FindEntityValidated(serial, "getWeaponClass");
	CDiceRoll_InitThiscall((CWeaponDice *)&dice, numDice, diceFaces, bonus, 0);
	if (ent == NULL)
		return;
	if (VT_IsWeapon(ent)) {
		CWeapon_SetDamageDice(ent, (CWeaponDice *)&dice);
		return;
	}
	if (VT_IsMobile(ent))
		CMobile_SetArmorRating((CMobile *)ent, (CWeaponDice *)&dice);
	USED(pad);
}

/*
 * 0x004160F0 - applyWeaponTemplate
 *
 * Stamps the weapon item with the values from weapon definition
 * templateId. Returns 0 when the template id is unknown.
 */
int
Script_applyWeaponTemplate(uint32_t serial, int templateId)
{
	CItem *weapon;

	weapon = FindWeaponValidated(serial, "applyWeaponTemplate");
	if (weapon == NULL)
		return 0;
	return CWeaponManager_GetWeapon(&g_WeaponManager, (uint8_t)templateId, weapon);
}

/*
 * 0x00416128 - Script handler for getAverageDamage [597]
 *
 * Returns the average damage of the weapon, derived from its
 * embedded NdF+B damage dice.
 */
int
Script_getAverageDamage(uint32_t serial)
{
	CItem *ent;
	CWeaponDice *dice;

	ent = FindWeaponValidated(serial, "getAverageDamage");
	if (ent == NULL)
		return 0;
	dice = CWeapon_GetDamageDice(ent);
	return CDiceRoll_Average(dice);
}

/*
 * 0x0041615D - getBackpack
 *
 * Returns the serial of the mobile's backpack (equipment slot 21)
 * when it has accessible contents, otherwise the first equipment
 * slot that does, or 0 when none qualify.
 */
uint32_t
Script_getBackpack(uint32_t serial)
{
	CItem *mob;
	CMobile *m;
	int i;

	mob = FindMobileValidated(serial, "getBackpack");
	if (mob == NULL)
		return 0;
	m = (CMobile *)mob;
	if (m->equipment[21] != NULL) {
		if (((int (*)(void *))VT_FN(m->equipment[21], VT_HAS_ACCESSIBLE_CONTENTS))(m->equipment[21]))
			return m->equipment[21]->serial;
	}
	for (i = 0; i < 26; i++) {
		if (m->equipment[i] != NULL) {
			if (((int (*)(void *))VT_FN(m->equipment[i], VT_HAS_ACCESSIBLE_CONTENTS))(m->equipment[i]))
				return m->equipment[i]->serial;
		}
	}
	return 0;
}

/*
 * 0x00416224 - transferGenericToContainer
 *
 * Removes count units of itemType from the mobile's equipment
 * containers and drops the resulting stack into container. Vendors
 * fall back to SubtractGold for itemType 0xEED. Returns the new
 * stack's serial, or 0 when the move fails.
 */
uint32_t
Script_transferGenericToContainer(uint32_t containerSerial, uint32_t mobileSerial, uint32_t itemType, uint32_t count)
{
	CItem *container;
	CItem *mob;
	CItem *result;
	uint32_t flags;

	if (count == 0)
		return 0;

	container = FindContainerValidated(containerSerial, "transferGenericToContainer");
	mob = FindMobileValidated(mobileSerial, "transferGenericToContainer");
	if (container == NULL || mob == NULL)
		return 0;

	// Check TF_STACKABLE flag (binary: no NULL/bounds check)
	flags = g_ItemTileData[itemType].flags;
	if (!(flags & TF_STACKABLE))
		return 0;

	result = CMobile_FindItemInEquipment((CMobile *)mob, (uint16_t)itemType, (int)count);

	// Gold fallback for vendors (vtable[0xE8] IsVendor)
	if (result == NULL) {
		if (VT_IsVendor(mob) && (uint16_t)itemType == 0xEED)
			result = CMobile_SubtractGold((CMobile *)mob, (int)count);
	}

	if (result == NULL)
		return 0;

	{
		CLocation tmpLoc;
		CLocation_Constructor3D(&tmpLoc, -1, -1, 0);
		((void (*)(void *, CItem *, CLocation *))VT_FN(result, VT_ADD_TO_CONTAINER))(result, container, &tmpLoc);
	}

	return result->serial;
}

/*
 * 0x0041630A - transferGenericToWorld
 *
 * Removes count units of itemType from the mobile's equipment
 * containers and drops the resulting stack at loc. Returns the new
 * stack's serial, or 0 when the move fails.
 */
uint32_t
Script_transferGenericToWorld(CLocation *loc, uint32_t mobileSerial, uint32_t itemType, uint32_t count)
{
	CItem *mob;
	CItem *result;
	uint32_t flags;

	if (count == 0)
		return 0;

	// Validate coordinates
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;

	mob = FindMobileValidated(mobileSerial, "transferGenericToWorld");
	if (mob == NULL)
		return 0;

	// Check TF_STACKABLE flag (binary: no NULL/bounds check)
	flags = g_ItemTileData[itemType].flags;
	if (!(flags & TF_STACKABLE))
		return 0;

	result = CMobile_FindItemInEquipment((CMobile *)mob, (uint16_t)itemType, (int)count);
	if (result == NULL)
		return 0;

	((void (*)(void *, CLocation *))VT_FN(result, VT_DROP_AT_FEET))(result, loc);

	return result->serial;
}

/*
 * 0x004163AF - destroyGeneric
 *
 * Removes count units of itemType from the mobile's equipment
 * containers and deletes the resulting stack outright.
 */
void
Script_destroyGeneric(uint32_t mobileSerial, uint32_t itemType, uint32_t count)
{
	CItem *mob;
	CItem *result;

	mob = FindMobileValidated(mobileSerial, "destroyGeneric");
	if (mob == NULL)
		return;

	result = CMobile_FindItemInEquipment((CMobile *)mob, (uint16_t)itemType, (int)count);
	if (result == NULL)
		return;
	// Redundant NULL check (binary quirk)
	if (result == NULL)
		return;

	((void (*)(void *))VT_FN(result, VT_DELETE))(result);
}

/*
 * 0x00416403 - giveItem
 *
 * Gives an item to a mobile by placing it in the first container
 * found in the mobile's equipment slots (0 through 0x19, 26 slots).
 * Skips non-container equipment and self-references. On success,
 * removes the item from its current world position and inserts it
 * into the found container. Returns the container's serial, or 0.
 */
uint32_t
Script_giveItem(uint32_t mobSerial, uint32_t itemSerial)
{
	CItem *mob;
	CItem *item;
	CMobile *target;
	CItem *equip;
	int i;
	int found;

	mob = FindMobileValidated(mobSerial, "giveItem");
	if (mob == NULL)
		return 0;
	item = FindEntityValidated(itemSerial, "giveItem");
	if (item == NULL)
		return 0;

	target = (CMobile *)mob;
	found = 0;
	for (i = 0; i < 0x1A; i++) {
		if (target->equipment[i] == NULL)
			continue;
		if (!((int (*)(void *))VT_FN(target->equipment[i], VT_HAS_ACCESSIBLE_CONTENTS))(target->equipment[i]))
			continue;
		if (target->equipment[i] == item)
			continue;
		found = 1;
		break;
	}
	if (!found)
		return 0;

	equip = target->equipment[i];

	((void (*)(void *))VT_FN(item, VT_HIDE))(item);

	{
		CLocation tmpLoc;
		CLocation_Init(&tmpLoc);
		CLocation_Set(&tmpLoc, -1, -1, 0);
		((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, equip, &tmpLoc);
	}

	return equip->serial;
}

/*
 * 0x0041651C - systemMessage
 *
 * Sends a default-coloured system message to the named player.
 */
void
Script_systemMessage(uint32_t serial, CString *str)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "systemMessage");
	if (ent == NULL)
		return;
	CPlayer_SystemMessage((CPlayer *)ent, CString_GetData(str));
}

/*
 * 0x00416551 - systemMessageHued
 *
 * Sends a system message (type 6, font 3) to the named player at
 * the given hue.
 */
void
Script_systemMessageHued(uint32_t serial, int hue, CString *str)
{
	CItem *ent;
	uint8_t obuf[0x430];

	ent = FindPlayerValidated(serial, "systemMessageHued");
	if (ent == NULL)
		return;
	PacketManager_MakePacket_TEXT(obuf, NULL, ent, 6, CString_GetData(str), (uint16_t)hue, 3);
	SendToClient(ent, obuf, -1);
}

/*
 * 0x004165B6 - getPlayAge
 *
 * Returns the player's accumulated play time counter, or 0 when the
 * serial is not a valid player.
 */
int
Script_getPlayAge(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "getPlayAge");
	if (ent == NULL)
		return 0;
	return CPlayer_GetPlayAge((CPlayer *)ent);
}

/*
 * 0x004165E4 - getAccountNum
 *
 * Returns the player's account number, or 0 when the serial is not
 * a valid player.
 */
int
Script_getAccountNum(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "getAccountNum");
	if (ent == NULL)
		return 0;
	return (int)((CPlayer *)ent)->accountNum;
}

/*
 * 0x00416613 - getCharacterNum
 *
 * Returns the player's character slot index within the account.
 */
int
Script_getCharacterNum(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "getCharacterNum");
	if (ent == NULL)
		return 0;
	return (int)((CPlayer *)ent)->characterNum;
}

/*
 * 0x00416642 - isGoldAccount
 *
 * Returns 1 when the player's account is flagged as gold-tier.
 */
int
Script_isGoldAccount(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "isGoldAccount");
	if (ent == NULL)
		return 0;
	return CPlayer_IsGoldAccount((CPlayer *)ent);
}

/*
 * 0x00416670 - getCombatMode
 *
 * Returns 1 when the player has war mode active.
 */
int
Script_getCombatMode(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "getCombatMode");
	if (ent == NULL)
		return 0;
	return CMobile_CheckMobileFlag((CMobile *)ent, 0x40);
}

/*
 * 0x004166A0 - isInvulnerable
 *
 * Returns 1 when the mobile has the invulnerable status flag set.
 */
int
Script_isInvulnerable(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "isInvulnerable");
	if (ent == NULL)
		return 0;
	return CMobile_IsInvulnerable((CMobile *)ent);
}

/*
 * 0x004166CE - makeInvulnerable
 *
 * Sets the mobile's invulnerable status flag.
 */
void
Script_makeInvulnerable(uint32_t serial)
{
	CMobile *mob;

	mob = (CMobile *)FindMobileValidated(serial, "makeInvulnerable");
	if (mob == NULL)
		return;
	((void (*)(void *))VT_FN((CItem *)mob, VT_SET_INVULN))(mob);
}

/*
 * 0x00416700 - makeVulnerable
 *
 * Clears the mobile's invulnerable status flag.
 */
void
Script_makeVulnerable(uint32_t serial)
{
	CMobile *mob;

	mob = (CMobile *)FindMobileValidated(serial, "makeVulnerable");
	if (mob == NULL)
		return;
	((void (*)(void *))VT_FN((CItem *)mob, VT_CLR_INVULN))(mob);
}

/*
 * 0x00416732 - isCorpse
 *
 * Returns 1 when the entity is a valid corpse object.
 */
int
Script_isCorpse(uint32_t serial)
{
	if (FindCorpseValidated(serial, "isCorpse") == NULL)
		return 0;
	return 1;
}

/*
 * 0x0041675D - getCorpseBodyType
 *
 * Calls FindCorpseValidated (0x0040DE70) with "getCorpseBodyType" context,
 * then calls CCorpse_GetCorpseBodyType (0x00489B97) which returns
 * [entity+0xC4] & 0xFFFF. Returns 0 if not a valid corpse.
 */
int
Script_getCorpseBodyType(uint32_t serial)
{
	CItem *ent;

	ent = FindCorpseValidated(serial, "getCorpseBodyType");
	if (ent == NULL)
		return 0;
	return CCorpse_GetCorpseBodyType((CCorpse *)ent) & 0xFFFF;
}

/*
 * 0x00416790 - textMessage
 *
 * Sends a formatted text message to the player at the given hue,
 * font, and speech type. Returns 1 on success.
 */
int
Script_textMessage(uint32_t serial, CString *text, int hue, int font, int speechType)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "textMessage");
	if (ent == NULL)
		return 0;

	CPlayer_SendFormattedMessage((CPlayer *)ent, CString_GetData(text), hue, font, speechType);
	return 1;
}

/*
 * 0x004167D8 - superBark
 *
 * Speaks text on behalf of the entity with explicit hue, speech
 * type, and font. Returns 1 on success.
 */
int
Script_superBark(uint32_t serial, CString *text, int hue, int type, int font)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "superBark");
	if (ent == NULL)
		return 0;

	((void (*)(CItem *, char *, int, int, int))VT_FN(ent, VT_SAY_CSTRING))(ent, CString_GetBuffer(text), hue, type, font);
	return 1;
}

/*
 * 0x00416823 - getMobsInRangeOld
 *
 * Appends every mobile within range of loc to list using the slow
 * spatial-grid walk. Superseded by Script_getMobsInRange.
 */
void
Script_getMobsInRangeOld(CList *list, CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;

	CList_Clear(list);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (VT_IsMobile(cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range)
					CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
			}
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x0041690A - getMobsInRange
 *
 * Appends every NPC and player within range of loc to list, using
 * the entity-map index for O(log n) lookup.
 */
void
Script_getMobsInRange(CList *list, CLocation *loc, int range)
{
	CList_Clear(list);
	CEntityMap_RangeQueryToList(g_NPCMap, list, (int)(int16_t)loc->x, (int)(int16_t)loc->y, range);
	CEntityMap_RangeQueryToList(g_ItemMap, list, (int)(int16_t)loc->x, (int)(int16_t)loc->y, range);
}

/*
 * 0x0041695B - objIsInRange
 *
 * Returns 1 when at least one non-mobile entity sits within range
 * of loc.
 */
int
Script_objIsInRange(CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (!VT_IsMobile(cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation((CEntity *)cur));
				if (dist < range)
					return 1;
			}
			cur = cur->spatialNext;
		}
	}

	return 0;
}

/*
 * 0x00416A2F - getObjectsInRange
 *
 * Appends every non-mobile entity within range of loc to list.
 */
void
Script_getObjectsInRange(CList *list, CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;

	CList_Clear(list);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (!VT_IsMobile(cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range)
					CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
			}
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x00416B16 - getObjectsInRangeWithFlags
 *
 * Appends every entity in the spatial blocks near loc whose
 * tiledata flags include all of flags. Filters by block proximity
 * only, no per-tile distance check.
 */
void
Script_getObjectsInRangeWithFlags(CList *list, CLocation *loc, int range, int flags)
{
	int blockIds[1024];
	int i;
	CItem *cur;

	CList_Clear(list);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if ((VT_GetFlags(cur) & flags) == flags)
				CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x00416BDD - getNumAllObjectsInRangeWithFlags
 *
 * Returns the number of entities (dynamic and static) within range
 * of loc whose tiledata flags include all of flags.
 */
int
Script_getNumAllObjectsInRangeWithFlags(CLocation *loc, int range, int flags)
{
	int blockIds[1024];
	int i;
	int count = 0;
	CItem *cur;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		// Walk itemHead chain (+0x104, spatialNext +0x20)
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if ((VT_GetFlags(cur) & flags) == flags) {
				int dist = CLocation_ChebyshevDistance(CEntity_GetLocation(&cur->resourceEntity.entity), loc);
				if (dist <= range)
					count++;
			}
			cur = cur->spatialNext;
		}

		// Walk staticHead chain (+0x100, nextInContainer +0x10)
		cur = g_MapBlocks[blockIds[i]].staticHead;
		while (cur != NULL) {
			if ((VT_GetFlags(cur) & flags) == flags) {
				int dist = CLocation_ChebyshevDistance(CEntity_GetLocation(&cur->resourceEntity.entity), loc);
				if (dist <= range)
					count++;
			}
			cur = (CItem *)cur->resourceEntity.nextInContainer;
		}
	}

	return count;
}

/*
 * 0x00416D4C - getTerrainFlags
 *
 * Returns the land-tile flags for tileID masked with mask, or 0
 * when tileID is out of range.
 */
int
Script_getTerrainFlags(int tileID, int mask)
{
	if (tileID < 0 || tileID > 0x3FFF)
		return 0;
	return g_LandTileData[tileID].flags & mask;
}

/*
 * 0x00416D76 - getObjectFlags
 *
 * Returns the entity's tiledata flags AND mask. Mobiles always
 * return 0.
 */
int
Script_getObjectFlags(uint32_t serial, int mask)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getFlags");
	if (ent == NULL)
		return 0;
	return VT_GetFlags(ent) & mask;
}

/*
 * 0x00416DAA - getObjectsInRangeOfType
 *
 * Appends every entity within range of loc whose body type matches
 * bodyType to list.
 */
void
Script_getObjectsInRangeOfType(CList *list, CLocation *loc, int range, int bodyType)
{
	int blockIds[1024];
	int i;
	CItem *ent;

	CList_Clear(list);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		ent = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (ent != NULL) {
			if ((CEntity_GetBodyType(ent) & 0xFFFF) == bodyType) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&ent->resourceEntity.entity));
				if (dist < range)
					CList_Append(list, WTYPE_OBJ, (uintptr_t)ent->serial);
			}
			ent = ent->spatialNext;
		}
	}
}

/*
 * 0x00416E8E - getObjectsInSpecRange
 *
 * Appends non-mobile entities whose distance from loc is strictly
 * between minRange and maxRange to list.
 */
void
Script_getObjectsInSpecRange(CList *list, CLocation *loc, int minRange, int maxRange)
{
	int blockIds[1024];
	int i;
	CItem *cur;

	CList_Clear(list);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, maxRange, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (!VT_IsMobile(cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist > minRange && dist < maxRange)
					CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
			}
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x00416F83 - getPlayersInRangeByBlock
 *
 * Appends every player strictly closer than range to loc to list,
 * walking the spatial grid blocks itself instead of going through the
 * entity map the way getPlayersInRange does.
 */
static __attribute__((unused)) void
Script_getPlayersInRangeByBlock(CList *list, CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;

	CList_Clear(list);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (((int (*)(void *))VT_FN(cur, VT_IS_PLAYER))(cur)) {
				if (CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity)) < range)
					CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
			}
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x00417067 - getPlayersInRange
 *
 * Appends every player within range of loc to list.
 */
void
Script_getPlayersInRange(CList *list, CLocation *loc, int range)
{
	CList_Clear(list);
	CEntityMap_RangeQueryToList(g_ItemMap, list, (int)(int16_t)loc->x, (int)(int16_t)loc->y, range);
}

/*
 * 0x00417096 - getClosestMobile
 *
 * Returns the serial of the mobile nearest loc within range, or 0
 * when none qualify.
 */
uint32_t
Script_getClosestMobile(CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;
	CItem *closest = NULL;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (VT_IsMobile(cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range) {
					if (closest == NULL) {
						closest = cur;
					} else {
						int distCur = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
						int distClosest = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&closest->resourceEntity.entity));
						if (distCur < distClosest)
							closest = cur;
					}
				}
			}
			cur = cur->spatialNext;
		}
	}

	if (closest == NULL)
		return 0;
	return CMobile_GetSerial((CMobile *)closest);
}

/*
 * 0x004171C1 - getClosestPlayer
 *
 * Returns the serial of the player nearest loc within range, or 0
 * when none qualify.
 */
uint32_t
Script_getClosestPlayer(CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;
	CItem *closest = NULL;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (VT_IsPlayer(cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range) {
					if (closest == NULL) {
						closest = cur;
					} else {
						int distCur = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
						int distClosest = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&closest->resourceEntity.entity));
						if (distCur < distClosest)
							closest = cur;
					}
				}
			}
			cur = cur->spatialNext;
		}
	}

	if (closest == NULL)
		return 0;
	return CMobile_GetSerial((CMobile *)closest);
}

/*
 * 0x004172E9 - getClosestOnlinePlayer
 *
 * Returns the serial of the online player nearest loc within range,
 * or 0 when none qualify.
 */
uint32_t
Script_getClosestOnlinePlayer(CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;
	CItem *closest = NULL;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (VT_IsPlayer(cur) && CPlayer_IsPlayerOnline((CPlayer *)cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range) {
					if (closest == NULL) {
						closest = cur;
					} else {
						int distCur = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
						int distClosest = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&closest->resourceEntity.entity));
						if (distCur < distClosest)
							closest = cur;
					}
				}
			}
			cur = cur->spatialNext;
		}
	}

	if (closest == NULL)
		return 0;
	return CMobile_GetSerial((CMobile *)closest);
}

/*
 * 0x00417424 - getClosestMobileOrOnlinePlayer
 *
 * Returns the serial of the closest online player or NPC mobile
 * within range of loc.
 */
uint32_t
Script_getClosestMobileOrOnlinePlayer(CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;
	CItem *closest = NULL;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			int accept = 0;

			if (VT_IsPlayer(cur) && CPlayer_IsPlayerOnline((CPlayer *)cur)) {
				accept = 1;
			} else if (VT_IsMobile(cur) && !VT_IsPlayer(cur)) {
				accept = 1;
			}

			if (accept) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range) {
					if (closest == NULL) {
						closest = cur;
					} else {
						int distCur = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
						int distClosest = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&closest->resourceEntity.entity));
						if (distCur < distClosest)
							closest = cur;
					}
				}
			}
			cur = cur->spatialNext;
		}
	}

	if (closest == NULL)
		return 0;
	return CMobile_GetSerial((CMobile *)closest);
}

/*
 * 0x0041758C - getClosestVisibleOnlinePlayer
 *
 * Like getClosestOnlinePlayer, but rejects hidden and dead players
 * unless they have the ghost-visible flag set.
 */
uint32_t
Script_getClosestVisibleOnlinePlayer(CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;
	CItem *closest = NULL;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (VT_IsPlayer(cur) && CPlayer_IsPlayerOnline((CPlayer *)cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range) {
					if (closest != NULL) {
						int distCur = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
						int distClosest = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&closest->resourceEntity.entity));
						if (distCur >= distClosest)
							goto next;
					}
					if (VT_IsHidden(cur))
						goto next;
					if (VT_IsDead(cur)) {
						// Dead: only visible if ghost flag set
						if (!(((CPlayer *)cur)->pflags & 0x20))
							goto next;
					}
					closest = cur;
				}
			}
next:
			cur = cur->spatialNext;
		}
	}

	if (closest == NULL)
		return 0;
	return CMobile_GetSerial((CMobile *)closest);
}

/*
 * 0x00417716 - getNPCsInRangeOld
 *
 * Appends every NPC within range of loc to list using the slow
 * spatial-grid walk. Superseded by Script_getNPCsInRange.
 */
void
Script_getNPCsInRangeOld(CList *list, CLocation *loc, int range)
{
	int blockIds[1024];
	int i;
	CItem *cur;

	CList_Clear(list);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockIds, 0x400);

	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if (VT_IsNPC(cur)) {
				int dist = CLocation_ChebyshevDistance(loc, CEntity_GetLocation(&cur->resourceEntity.entity));
				if (dist < range)
					CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
			}
			cur = cur->spatialNext;
		}
	}
}

/*
 * 0x004177FD - getNPCsInRange
 *
 * Appends every NPC within range of loc to list, using the
 * entity-map index for fast lookup.
 */
void
Script_getNPCsInRange(CList *list, CLocation *loc, int range)
{
	CList_Clear(list);
	CEntityMap_RangeQueryToList(g_NPCMap, list, (int)(int16_t)loc->x, (int)(int16_t)loc->y, range);
}

// Core type-check and getter/setter handlers (indices 203-767)

/*
 * 0x0041782C - getTileAt
 *
 * Returns the land tile id at loc, or 0 when the coordinates fall
 * outside the world.
 */
int
Script_getTileAt(CLocation *loc)
{
	int x, y;

	x = loc->x;
	y = loc->y;
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y))
		return 0;
	return Terrain_GetLandTileID(x, y);
}

/*
 * 0x0041786D - getMobsAt
 *
 * Appends every mobile sharing loc's exact x/y/z to list.
 */
void
Script_getMobsAt(CList *list, CLocation *loc)
{
	int blockIdx;
	CItem *cur;

	CList_Clear(list);

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return;

	cur = g_SpatialGrid.cells[blockIdx].itemHead;
	while (cur != NULL) {
		if (VT_IsMobile(cur)) {
			if (CLocation_IsNearXYZ(CEntity_GetLocation(&cur->resourceEntity.entity), loc))
				CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x00417900 - getNPCsAt
 *
 * Appends every NPC sharing loc's exact x/y/z to list.
 */
void
Script_getNPCsAt(CList *list, CLocation *loc)
{
	int blockIdx;
	CItem *cur;

	CList_Clear(list);

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return;

	cur = g_SpatialGrid.cells[blockIdx].itemHead;
	while (cur != NULL) {
		if (VT_IsNPC(cur)) {
			if (CLocation_IsNearXYZ(CEntity_GetLocation((CEntity *)cur), loc))
				CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x00417993 - getPlayersAt
 *
 * Appends every player sharing loc's exact x/y/z to list.
 */
void
Script_getPlayersAt(CList *list, CLocation *loc)
{
	int blockIdx;
	CItem *cur;

	CList_Clear(list);

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return;

	cur = g_SpatialGrid.cells[blockIdx].itemHead;
	while (cur != NULL) {
		if (VT_IsPlayer(cur)) {
			if (CLocation_IsNearXYZ(CEntity_GetLocation((CEntity *)cur), loc))
				CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x00417A23 - getObjectsAt
 *
 * Appends every non-mobile entity sharing loc's exact x/y/z to list.
 */
void
Script_getObjectsAt(CList *list, CLocation *loc)
{
	int blockIdx;
	CItem *cur;

	CList_Clear(list);

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return;

	cur = g_SpatialGrid.cells[blockIdx].itemHead;
	while (cur != NULL) {
		if (!VT_IsMobile(cur)) {
			if (CLocation_IsNearXYZ(CEntity_GetLocation((CEntity *)cur), loc))
				CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x00417AB6 - getObjectsAtInZRange
 *
 * Appends every non-mobile entity at loc's (x, y) whose z falls in
 * [zMin, zMax] to list.
 */
void
Script_getObjectsAtInZRange(CList *list, CLocation *loc, int zMin, int zMax)
{
	int blockIdx;
	CItem *cur;

	CList_Clear(list);

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return;

	cur = g_SpatialGrid.cells[blockIdx].itemHead;
	while (cur != NULL) {
		if (!VT_IsMobile(cur)) {
			if ((int16_t)CEntity_GetLocation((CEntity *)cur)->x == (int16_t)loc->x && (int16_t)CEntity_GetLocation((CEntity *)cur)->y == (int16_t)loc->y &&
			        (int16_t)CEntity_GetLocation((CEntity *)cur)->z >= zMin && (int16_t)CEntity_GetLocation((CEntity *)cur)->z <= zMax)
				CList_Append(list, WTYPE_OBJ, (uintptr_t)cur->serial);
		}
		cur = cur->spatialNext;
	}
}

/*
 * 0x00417B86 - getFirstObjectOfType
 *
 * Returns the serial of the first item near loc whose body type
 * matches bodyType, or 0 when none qualify.
 */
uint32_t
Script_getFirstObjectOfType(CLocation *loc, int bodyType)
{
	int blockIdx;
	CItem *ent;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return 0;

	ent = g_SpatialGrid.cells[blockIdx].itemHead;
	while (ent != NULL) {
		if ((CEntity_GetBodyType(ent) & 0xFFFF) == bodyType) {
			if (CLocation_IsNearXYZ(CEntity_GetLocation(&ent->resourceEntity.entity), loc))
				return ent->serial;
		}
		ent = ent->spatialNext;
	}

	return 0;
}

/*
 * 0x00417C0C - getNextObjectOfType
 *
 * Resumes the getFirstObjectOfType walk after lastSerial, returning
 * the next matching serial near loc, or 0 when none remain.
 */
uint32_t
Script_getNextObjectOfType(CLocation *loc, int bodyType, uint32_t lastSerial)
{
	CItem *lastObj;
	int blockIdx;
	CItem *cur;

	lastObj = CWorld_FindBySerial(g_World, lastSerial);
	if (lastObj == NULL)
		return 0;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return 0;

	cur = g_SpatialGrid.cells[blockIdx].itemHead;
	while (cur != NULL) {
		if (cur == lastObj)
			break;
		cur = cur->spatialNext;
	}

	if (cur == NULL)
		return 0;

	cur = cur->spatialNext;
	while (cur != NULL) {
		if ((CEntity_GetBodyType(cur) & 0xFFFF) == bodyType) {
			if (CLocation_IsNearXYZ(CEntity_GetLocation(&cur->resourceEntity.entity), loc))
				return cur->serial;
		}
		cur = cur->spatialNext;
	}

	return 0;
}

/*
 * 0x00417CDC - getQuantity
 *
 * Returns the entity's stack amount: 1 for plain items or the
 * minimum resource ratio for resource-flagged items.
 */
int
Script_getQuantity(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getQuantity");
	if (ent == NULL)
		return 0;
	return ((int (*)(void *))VT_FN(ent, VT_GET_ITEM_AMOUNT))(ent);
}

/*
 * 0x00417D10 - getMiscData
 *
 * FindEntityValidated, then CItem_GetSortKey masked to 16 bits.
 */
int
Script_getMiscData(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getMiscData");
	if (ent == NULL)
		return 0;
	return CItem_GetSortKey(ent) & 0xffff;
}

/*
 * 0x00417D43 - getQuality
 *
 * Returns the tiledata layer byte for the entity's body type
 * (despite the name, this is the equipment-layer value).
 */
int
Script_getQuality(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getQuality");
	if (ent == NULL)
		return 0;
	return (int)CItem_GetEquipSlot(ent) & 0xff;
}

/*
 * 0x00417D76 - getTileHeight
 *
 * Returns the tile height (tiledata quantity byte) for body type
 * type, or 0 when type is out of range.
 */
int
Script_getTileHeight(int type)
{
	char buf[128];

	if (type < 0 || type > 0x4000) {
		// Dead code: binary sprintf's error message into local buffer,
		// never uses it, then returns 0.
		sprintf(buf, "getTileHeight: Invalid type: %d\n", type);
		return 0;
	}
	return (int)g_ItemTileData[type].quantity;
}

/*
 * 0x00417DBE - getHeight
 *
 * Returns the entity's collision height: tiledata height for items,
 * a fixed 16 for mobiles.
 */
int
Script_getHeight(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getHeight");
	if (ent == NULL)
		return 0;
	return VT_GetHeight(ent);
}

/*
 * 0x00417DEF - getSurfaceHeight
 *
 * Returns the entity's walkable surface height (the offset clients
 * step onto when standing on the entity).
 */
int
Script_getSurfaceHeight(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getHeight");
	if (ent == NULL)
		return 0;
	return ((int (*)(void *))VT_FN(ent, VT_GET_SURFACE_H))(ent);
}

/*
 * 0x00417E20 - logOut
 *
 * FindPlayerValidated, then CPlayer_LogOut(player, 1).
 */
void
Script_logOut(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "logOut");
	if (ent == NULL)
		return;
	CPlayer_LogOut((CPlayer *)ent, 1);
}

/*
 * 0x00417E4E - safeLogOut
 *
 * FindPlayerValidated, then CPlayer_LogOut(player, 0).
 */
void
Script_safeLogOut(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "safeLogOut");
	if (ent == NULL)
		return;
	CPlayer_LogOut((CPlayer *)ent, 0);
}

/*
 * 0x00417E7C - isHidden
 *
 * Returns 1 when the entity has its hidden flag set.
 */
int
Script_isHidden(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "isHidden");
	if (ent == NULL)
		return 0;
	return VT_IsHidden(ent);
}

/*
 * 0x00417EB0 - setHidden
 *
 * Sets or clears the entity's hidden flag.
 */
void
Script_setHidden(uint32_t serial, int value)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "setHidden");
	if (ent == NULL)
		return;

	((void (*)(CItem *, int))VT_FN(ent, VT_SET_HIDDEN))(ent, value);
}

/*
 * 0x00417EE6 - openGump
 *
 * FindPlayerValidated, build OPEN_GUMP packet, send to player.
 * Special case: if gumpId == 0x1392, use serial 1 as gump serial.
 */
void
Script_openGump(uint32_t serial, int gumpId)
{
	CPlayer *player;
	uint32_t gumpSerial;
	uint8_t obuf[16];

	player = (CPlayer *)FindPlayerValidated(serial, "openGump");
	if (player == NULL)
		return;
	gumpSerial = serial;
	if (gumpId == 0x1392)
		gumpSerial = 1;
	PacketManager_MakePacket_OPEN_GUMP(obuf, gumpSerial, (uint16_t)gumpId);
	SendToClient((CItem *)player, obuf, -1);
}

/*
 * 0x00417F51 - closeGump
 *
 * FindPlayerValidated, build OPEN_GUMP packet, send to player.
 * Special case: if gumpId == 0x1392, use serial 0 as gump serial.
 */
void
Script_closeGump(uint32_t serial, int gumpId)
{
	CPlayer *player;
	uint32_t gumpSerial;
	uint8_t obuf[16];

	player = (CPlayer *)FindPlayerValidated(serial, "closeGump");
	if (player == NULL)
		return;
	gumpSerial = serial;
	if (gumpId == 0x1392)
		gumpSerial = 0;
	PacketManager_MakePacket_OPEN_GUMP(obuf, gumpSerial, (uint16_t)gumpId);
	SendToClient((CItem *)player, obuf, -1);
}

/*
 * 0x00417FBC - Script_doMissile_Loc2Loc
 *
 * Wombat handler [457]: moving missile effect between two locations.
 * AnimSequence path: type 3, 28-byte buffer.
 * Direct path: MakePacket_EFFECT type 0, CPlayerList_BroadcastToTwoLocs(18).
 */
void
Script_doMissile_Loc2Loc(CLocation *srcLoc, CLocation *dstLoc, int effectId, int speed, int duration, int hue)
{
	uint8_t seqData[28];
	uint8_t buf[28];
	uint8_t *ptr;

	if (g_AnimSequence.state) {
		AnimSequence_AddLocation(srcLoc);
		AnimSequence_AddLocation(dstLoc);
		ptr = seqData;
		memcpy(ptr, srcLoc, 6);
		ptr += 6;
		memcpy(ptr, dstLoc, 6);
		ptr += 6;
		memcpy(ptr, &effectId, 4);
		ptr += 4;
		memcpy(ptr, &speed, 4);
		ptr += 4;
		memcpy(ptr, &duration, 4);
		ptr += 4;
		memcpy(ptr, &hue, 4);
		AnimSequence_AddCommand(3, seqData, 28);
	} else {
		PacketManager_MakePacket_EFFECT(buf, 0, 0, 0, (uint16_t)effectId, srcLoc->x, srcLoc->y, (uint8_t)srcLoc->z, dstLoc->x, dstLoc->y, (uint8_t)dstLoc->z,
		        (uint8_t)speed, 0, 0, 0, (uint8_t)duration, (uint8_t)hue);
		CPlayerList_BroadcastToTwoLocs(buf, srcLoc, dstLoc, 0x12, NULL);
	}
}

/*
 * 0x00418125 - Script_doMissile_Loc2Mob
 *
 * Wombat handler [458]: moving missile from location to mobile.
 * AnimSequence path: type 4, 32-byte buffer.
 * Direct path: MakePacket_EFFECT type 0, CPlayerList_BroadcastToTwoLocs(18).
 */
void
Script_doMissile_Loc2Mob(CLocation *srcLoc, uint32_t dstSerial, int effectId, int speed, int duration, int hue)
{
	CItem *mob;
	CLocation dstLoc;
	uint8_t seqData[32];
	uint8_t buf[28];
	uint8_t *ptr;

	CLocation_Init(&dstLoc);
	mob = FindMobileValidated(dstSerial, "doMissile_Loc2Mob");
	if (mob == NULL)
		return;
	CLocation_SetLoc(&dstLoc, VT_GetLocation(mob));

	if (g_AnimSequence.state) {
		AnimSequence_AddLocation(srcLoc);
		AnimSequence_AddLocation(&dstLoc);
		ptr = seqData;
		memcpy(ptr, &dstSerial, 4);
		ptr += 4;
		memcpy(ptr, &effectId, 4);
		ptr += 4;
		memcpy(ptr, srcLoc, 6);
		ptr += 6;
		memcpy(ptr, &dstLoc, 6);
		ptr += 6;
		memcpy(ptr, &speed, 4);
		ptr += 4;
		memcpy(ptr, &duration, 4);
		ptr += 4;
		memcpy(ptr, &hue, 4);
		AnimSequence_AddCommand(4, seqData, 32);
	} else {
		PacketManager_MakePacket_EFFECT(buf, 0, 0, dstSerial, (uint16_t)effectId, srcLoc->x, srcLoc->y, (uint8_t)srcLoc->z, dstLoc.x, dstLoc.y, (uint8_t)dstLoc.z,
		        (uint8_t)speed, 0, 0, 0, (uint8_t)duration, (uint8_t)hue);
		CPlayerList_BroadcastToTwoLocs(buf, srcLoc, &dstLoc, 0x12, NULL);
	}
}

/*
 * 0x004182E3 - Script_doMissile_Mob2Loc
 *
 * Wombat handler [459]: moving missile from mobile to location.
 * AnimSequence path: type 5, 32-byte buffer.
 * Direct path: MakePacket_EFFECT type 0, CPlayerList_BroadcastToTwoLocs(18).
 */
void
Script_doMissile_Mob2Loc(uint32_t srcSerial, CLocation *dstLoc, int effectId, int speed, int duration, int hue)
{
	CItem *mob;
	CLocation srcLoc;
	uint8_t seqData[32];
	uint8_t buf[28];
	uint8_t *ptr;

	CLocation_Init(&srcLoc);
	mob = FindMobileValidated(srcSerial, "doMissile_Mob2Loc");
	if (mob == NULL)
		return;
	CLocation_SetLoc(&srcLoc, VT_GetLocation(mob));

	if (g_AnimSequence.state) {
		AnimSequence_AddLocation(&srcLoc);
		AnimSequence_AddLocation(dstLoc);
		ptr = seqData;
		memcpy(ptr, &srcSerial, 4);
		ptr += 4;
		memcpy(ptr, &effectId, 4);
		ptr += 4;
		memcpy(ptr, &srcLoc, 6);
		ptr += 6;
		memcpy(ptr, dstLoc, 6);
		ptr += 6;
		memcpy(ptr, &speed, 4);
		ptr += 4;
		memcpy(ptr, &duration, 4);
		ptr += 4;
		memcpy(ptr, &hue, 4);
		AnimSequence_AddCommand(5, seqData, 32);
	} else {
		PacketManager_MakePacket_EFFECT(buf, 0, srcSerial, 0, (uint16_t)effectId, srcLoc.x, srcLoc.y, (uint8_t)srcLoc.z, dstLoc->x, dstLoc->y, (uint8_t)dstLoc->z,
		        (uint8_t)speed, 0, 0, 0, (uint8_t)duration, (uint8_t)hue);
		CPlayerList_BroadcastToTwoLocs(buf, &srcLoc, dstLoc, 0x12, NULL);
	}
}

/*
 * 0x004184A1 - Script_doMissile_Mob2Mob
 *
 * Wombat handler [460]: moving missile between two mobiles.
 * AnimSequence path: type 6, 36-byte buffer.
 * Direct path: MakePacket_EFFECT type 0, CPlayerList_BroadcastToTwoLocs(18).
 */
void
Script_doMissile_Mob2Mob(uint32_t srcSerial, uint32_t dstSerial, int effectId, int speed, int duration, int hue)
{
	CItem *mob1, *mob2;
	CLocation srcLoc, dstLoc;
	uint8_t seqData[36];
	uint8_t buf[28];
	uint8_t *ptr;

	CLocation_Init(&srcLoc);
	CLocation_Init(&dstLoc);
	mob1 = FindMobileValidated(srcSerial, "doMissile_Mob2Mob (mobile 1)");
	if (mob1 == NULL)
		return;
	mob2 = FindMobileValidated(dstSerial, "doMissile_Mob2Mob (mobile 2)");
	if (mob2 == NULL)
		return;
	CLocation_SetLoc(&srcLoc, VT_GetLocation(mob1));
	CLocation_SetLoc(&dstLoc, VT_GetLocation(mob2));

	if (g_AnimSequence.state) {
		AnimSequence_AddLocation(&srcLoc);
		AnimSequence_AddLocation(&dstLoc);
		ptr = seqData;
		memcpy(ptr, &srcSerial, 4);
		ptr += 4;
		memcpy(ptr, &dstSerial, 4);
		ptr += 4;
		memcpy(ptr, &effectId, 4);
		ptr += 4;
		memcpy(ptr, &srcLoc, 6);
		ptr += 6;
		memcpy(ptr, &dstLoc, 6);
		ptr += 6;
		memcpy(ptr, &speed, 4);
		ptr += 4;
		memcpy(ptr, &duration, 4);
		ptr += 4;
		memcpy(ptr, &hue, 4);
		AnimSequence_AddCommand(6, seqData, 36);
	} else {
		PacketManager_MakePacket_EFFECT(buf, 0, srcSerial, dstSerial, (uint16_t)effectId, srcLoc.x, srcLoc.y, (uint8_t)srcLoc.z, dstLoc.x, dstLoc.y, (uint8_t)dstLoc.z,
		        (uint8_t)speed, 0, 0, 0, (uint8_t)duration, (uint8_t)hue);
		CPlayerList_BroadcastToTwoLocs(buf, &srcLoc, &dstLoc, 0x12, NULL);
	}
}

/*
 * 0x004186B4 - Script_doLocAnimation
 *
 * Wombat handler [461]: stationary effect at a location.
 * AnimSequence path: type 0, 26-byte buffer.
 * Direct path: MakePacket_EFFECT type 2, BroadcastToNearby(18).
 */
void
Script_doLocAnimation(CLocation *loc, int effectId, int speed, int duration, int hue, int renderMode)
{
	uint8_t seqData[26];
	uint8_t buf[28];
	uint8_t *ptr;

	if (g_AnimSequence.state) {
		AnimSequence_AddLocation(loc);
		ptr = seqData;
		memcpy(ptr, &effectId, 4);
		ptr += 4;
		memcpy(ptr, loc, 6);
		ptr += 6;
		memcpy(ptr, &speed, 4);
		ptr += 4;
		memcpy(ptr, &duration, 4);
		ptr += 4;
		memcpy(ptr, &hue, 4);
		ptr += 4;
		memcpy(ptr, &renderMode, 4);
		AnimSequence_AddCommand(0, seqData, 26);
	} else {
		PacketManager_MakePacket_EFFECT(buf, 2, 0, 0, (uint16_t)effectId, loc->x, loc->y, (uint8_t)loc->z, loc->x, loc->y, (uint8_t)loc->z, (uint8_t)speed,
		        (uint8_t)duration, (uint8_t)hue, (uint8_t)renderMode, 1, 0);
		SendPacketInRange(buf, loc, 0x12);
	}
}

/*
 * 0x00418801 - Script_doMobAnimation
 *
 * Wombat handler [462]: stationary effect at a mobile's location.
 * AnimSequence path: type 1, 30-byte buffer.
 * Direct path: MakePacket_EFFECT type 3, BroadcastToNearby(18).
 */
void
Script_doMobAnimation(uint32_t serial, int effectId, int speed, int duration, int hue, int renderMode)
{
	CItem *mob;
	CLocation mobLoc;
	uint8_t seqData[30];
	uint8_t buf[28];
	uint8_t *ptr;

	CLocation_Init(&mobLoc);
	mob = FindMobileValidated(serial, "doMobAnimation");
	if (mob == NULL)
		return;
	CLocation_SetLoc(&mobLoc, VT_GetLocation(mob));

	if (g_AnimSequence.state) {
		AnimSequence_AddLocation(&mobLoc);
		ptr = seqData;
		memcpy(ptr, &serial, 4);
		ptr += 4;
		memcpy(ptr, &effectId, 4);
		ptr += 4;
		memcpy(ptr, &mobLoc, 6);
		ptr += 6;
		memcpy(ptr, &speed, 4);
		ptr += 4;
		memcpy(ptr, &duration, 4);
		ptr += 4;
		memcpy(ptr, &hue, 4);
		ptr += 4;
		memcpy(ptr, &renderMode, 4);
		AnimSequence_AddCommand(1, seqData, 30);
	} else {
		PacketManager_MakePacket_EFFECT(buf, 3, serial, 0, (uint16_t)effectId, mobLoc.x, mobLoc.y, (uint8_t)mobLoc.z, mobLoc.x, mobLoc.y, (uint8_t)mobLoc.z, (uint8_t)speed,
		        (uint8_t)duration, (uint8_t)hue, (uint8_t)renderMode, 1, 0);
		SendPacketInRange(buf, &mobLoc, 0x12);
	}
}

/*
 * 0x004189A3 - Script_doLightning
 *
 * Wombat handler [463]: lightning bolt effect at a mobile.
 * AnimSequence path: type 2, 10-byte buffer.
 * Direct path: MakePacket_EFFECT type 1, SendPacketInRange(18).
 */
void
Script_doLightning(uint32_t serial)
{
	CItem *mob;
	CLocation mobLoc;
	uint8_t seqData[10];
	uint8_t buf[28];

	CLocation_Init(&mobLoc);
	mob = FindMobileValidated(serial, "doLighting");
	if (mob == NULL)
		return;
	CLocation_SetLoc(&mobLoc, VT_GetLocation(mob));

	if (g_AnimSequence.state) {
		AnimSequence_AddLocation(&mobLoc);
		memcpy(seqData, &serial, 4);
		memcpy(seqData + 4, &mobLoc, 6);
		AnimSequence_AddCommand(2, seqData, 10);
	} else {
		PacketManager_MakePacket_EFFECT(buf, 1, serial, 0, 0, mobLoc.x, mobLoc.y, (uint8_t)mobLoc.z, mobLoc.x, mobLoc.y, (uint8_t)mobLoc.z, 0, 0, 0, 0, 0, 0);
		SendPacketInRange(buf, &mobLoc, 0x12);
	}
}

/*
 * 0x00418AAF - beginSequence
 *
 * If g_AnimSequence.state is 0, set it to 1.
 */
void
Script_beginSequence(void)
{
	if (g_AnimSequence.state == 0)
		g_AnimSequence.state = 1;
}

/*
 * 0x00418AC9 - endSequence
 *
 * If g_AnimSequence.state is not 0, call AnimSequence_Process
 * with the action ID, then AnimSequence_Clear.
 */
void
Script_endSequence(int actionId)
{
	if (g_AnimSequence.state == 0)
		return;
	AnimSequence_Process((uint8_t)actionId);
	AnimSequence_Clear();
}

/*
 * 0x00418AF1 - waitState
 *
 * FindMobileValidated, then CMobile_GetWaitStateAction.
 */
int
Script_waitState(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "waitState");
	if (ent == NULL)
		return 0;
	return CMobile_GetWaitStateAction((CMobile *)ent);
}

/*
 * 0x00418B1F - setWaitState
 *
 * FindMobileValidated, then set mob->waitStateMax = (byte)value,
 * mob->waitStateTick = 0.
 */
void
Script_setWaitState(uint32_t serial, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setWaitState");
	if (ent == NULL)
		return;
	((CMobile *)ent)->waitStateMax = (uint8_t)value;
	((CMobile *)ent)->waitStateTick = 0;
}

/*
 * 0x00418B59 - isInvisible
 *
 * Returns 1 when the entity is hidden. Same vtable check as
 * Script_isHidden.
 */
int
Script_isInvisible(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "isInvisible");
	if (ent == NULL)
		return 0;
	return VT_IsHidden(ent);
}

/*
 * 0x00418B8D - setInvisible
 *
 * Aliases Script_setHidden: sets or clears the entity's hidden flag.
 */
void
Script_setInvisible(uint32_t serial, int value)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "setInvisible");
	if (ent == NULL)
		return;

	((void (*)(CItem *, int))VT_FN(ent, VT_SET_HIDDEN))(ent, value);
}

/*
 * 0x00418BC3 - setPoisoned
 *
 * FindMobileValidated, then SetMobileFlag(4) or ClearMobileFlag(4).
 */
void
Script_setPoisoned(uint32_t serial, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setPoisoned");
	if (ent == NULL)
		return;
	if (value)
		CMobile_SetMobileFlag((CMobile *)ent, 4);
	else
		CMobile_ClearMobileFlag((CMobile *)ent, 4);
}

/*
 * 0x00418C03 - setCursed
 *
 * FindMobileValidated, then SetMobileFlag(8) or ClearMobileFlag(8).
 */
void
Script_setCursed(uint32_t serial, int value)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "setCursed");
	if (ent == NULL)
		return;
	if (value)
		CMobile_SetMobileFlag((CMobile *)ent, 8);
	else
		CMobile_ClearMobileFlag((CMobile *)ent, 8);
}

/*
 * 0x00418C43 - getName
 *
 * Stores the entity's display name in out, or an empty string when
 * the entity is missing. Returns out.
 */
CString *
Script_getName(CString *out, uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getName");
	if (ent == NULL) {
		CString_Constructor(out, "");
		return out;
	}
	CString_Constructor(out, ((const char *(*)(void *, int))VT_FN(ent, 0x4C))(ent, 1));
	return out;
}

/*
 * 0x00418CAB - getRealName
 *
 * Stores the mobile's real (non-disguised) name in out, or an
 * empty string when the mobile is missing. Returns out.
 */
CString *
Script_getRealName(CString *out, uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, NULL);
	if (ent == NULL) {
		CString_Constructor(out, "");
		return out;
	}
	CString_Constructor(out, ((const char *(*)(void *))VT_FN(ent, 0x34))(ent));
	return out;
}

/*
 * 0x00418D0E - setRealName
 *
 * FindMobileValidated with NULL name (binary pushes 0), then
 * CString_GetData on the string arg, then CMobile_SetName.
 */
void
Script_setRealName(uint32_t serial, CString *str)
{
	CItem *ent;

	ent = FindMobileValidated(serial, NULL);
	if (ent == NULL)
		return;
	CMobile_SetName((CMobile *)ent, CString_GetData(str));
}

/*
 * 0x00418D40 - setRealNameFromTemplate
 *
 * Sets the mobile's real name to the one stored under the given
 * template id.
 */
void
Script_setRealNameFromTemplate(uint32_t serial, int templateId)
{
	CItem *ent;
	char buf[32];

	sprintf(buf, "%d", templateId);
	ent = FindMobileValidated(serial, NULL);
	if (ent == NULL)
		return;
	CTemplateManager_SetRealNameFromTemplate(ent, buf);
}

/*
 * 0x00418D8A - getTitledName
 *
 * Stores the player's paperdoll title (e.g. "Lord John Smith") in
 * out, or "Flobbitz" when the player is missing. Returns out.
 */
CString *
Script_getTitledName(CString *out, uint32_t serial)
{
	CItem *player;
	CString tempStr;

	player = FindPlayerValidated(serial, "getTitledName");
	if (player == NULL) {
		CString_Constructor(out, "Flobbitz");
		return out;
	}

	CString_DefaultConstructor(&tempStr);
	((void (*)(void *, CString *))VT_FN(player, VT_PAPERDOLL_TITLE))(player, &tempStr);
	CString_CopyConstructor(out, &tempStr);
	CString_Destructor(&tempStr);
	return out;
}

/*
 * 0x00418E34 - walkTo [477]
 *
 * Tells an NPC to pathfind toward loc within range. Aborts the
 * current thread when the serial is not an NPC.
 */
void
Script_walkTo(uint32_t serial, CLocation *loc, int range)
{
	CItem *mob;

	mob = CWorld_FindBySerial(g_World, serial);
	if (mob == NULL || !VT_IsNPC(mob)) {
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return;
	}
	CNPC_WalkToLocation(mob, range, loc, -1);
}

/*
 * 0x00418E8D - isInMap
 *
 * Returns 1 when loc lies inside the playable map.
 */
int
Script_isInMap(CLocation *loc)
{
	return CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y);
}

/*
 * 0x00418EAB - isInWorld
 *
 * Returns 1 when loc falls within the world's coordinate bounds.
 */
int
Script_isInWorld(CLocation *loc)
{
	return CBlockManager_IsValidCoordAbsolute(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y);
}

/*
 * 0x00418EC9 - getElevationAt
 *
 * Returns the average land Z at (x, y), or 0 when the coordinates
 * fall outside the map.
 */
int
Script_getElevationAt(int x, int y)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y))
		return 0;
	return Terrain_GetAvgLandZ(x, y);
}

/*
 * 0x00418EFA - findGoodSpotNear
 *
 * Finds a usable spawn position near loc within range using a zero
 * minimum elevation. Returns 1 on success.
 */
int
Script_findGoodSpotNear(CLocation *loc, int zMax, int range, int height)
{
	return FindSpawnSpot(loc, 0, zMax, range, height, 0);
}

/*
 * 0x00418F1D - findGoodSpotNearMin
 *
 * Finds a usable spawn position near loc within range, restricted
 * to z values in [zMin, zMax]. Returns 1 on success.
 */
int
Script_findGoodSpotNearMin(CLocation *loc, int zMin, int zMax, int range, int height)
{
	return FindSpawnSpot(loc, zMin, zMax, range, height, 0);
}

/*
 * 0x00418F42 - findGoodSpotNearWithElev
 *
 * Finds a usable spawn position near loc within an explicit
 * elevation band [minElev, maxElev] and z range up to zMax.
 */
int
Script_findGoodSpotNearWithElev(CLocation *loc, int minElev, int maxElev, int zMax, int range, int height)
{
	return CBlockManager_FindSpawnSpotExt(loc, minElev, maxElev, 0, zMax, range, height, 0);
}

/*
 * 0x00418F6D - findGoodZ
 *
 * Returns the resting Z for loc that a mobile of the given height
 * could walk onto from currentZ in direction. Returns -128 when
 * loc is outside the map.
 */
int
Script_findGoodZ(CLocation *loc, int currentZ, int direction, int stepHeight, int height)
{
	CLocation walkLoc;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return -128;
	CLocation_SetLoc(&walkLoc, loc);
	return CTerrainManager_CanWalkWrapper(walkLoc, currentZ, direction, stepHeight, height, NULL, 0);
}

/*
 * 0x00418FC5 - canExistAt
 *
 * Returns 1 when an entity of the given step height can stand at
 * loc starting from currentZ.
 */
int
Script_canExistAt(CLocation *loc, int currentZ, int stepHeight)
{
	CLocation checkLoc;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;
	CLocation_SetLoc(&checkLoc, loc);
	return CTerrainManager_CheckMoveBlocked(checkLoc, currentZ, stepHeight, NULL, 0);
}

/*
 * 0x00419012 - canSeeLoc
 *
 * Returns 1 when the entity at serial has line of sight to loc
 * (raycast from its eye height against terrain and roofs).
 */
int
Script_canSeeLoc(uint32_t serial, CLocation *loc)
{
	CItem *ent;
	CLocation localLoc;
	int halfHeight;

	ent = FindEntityValidated(serial, "canSeeLoc");
	if (ent == NULL)
		return 0;

	CLocation_SetLoc(&localLoc, CEntity_GetLocation(&ent->resourceEntity.entity));

	halfHeight = VT_GetHeight(ent) / 2;
	localLoc.z += (int16_t)halfHeight;

	return CTerrainManager_LOSRaycast(&localLoc, loc, 1);
}

/*
 * 0x00419094 - canSeeObj
 *
 * Returns 1 when entity A can see entity B, accounting for
 * hidden mobiles. Same-entity always returns 1.
 */
int
Script_canSeeObj(uint32_t serialA, uint32_t serialB)
{
	CItem *entA, *entB;

	entA = FindEntityValidated(serialA, "canSeeObj");
	if (entA == NULL)
		return 0;

	entB = FindEntityValidated(serialB, "canSeeObj");
	if (entB == NULL)
		return 0;

	if (VT_IsMobile(entB)) {
		if (VT_IsHidden(entB))
			return 0;
	}

	// Same entity check
	if (entB == entA)
		return 1;

	return CEntity_CanSee(entA, entB, 1);
}

/*
 * 0x0041911F - getVisableTargets [616]
 *
 * Appends every combat target the mobile can currently see to list.
 *
 * FIXED: the binary dereferences the exhausted STL end sentinel
 * when copying serials out; the C version reads from the CVector
 * iteration directly.
 */
void
Script_getVisableTargets(CList *list, uint32_t serial)
{
	CItem *mob;
	CVector tmpList;
	void *iter;
	char typeFlag = 0;

	CList_Clear(list);
	mob = FindMobileValidated(serial, "getTargets");
	if (mob == NULL)
		return;
	CVector_Constructor(&tmpList, &typeFlag);
	{
		StdPtrNode *siter, *sbegin, *send, *sendCopy, *spostInc, *scopy;
		StdPtrIter_Constructor(&siter);
		StdPtrIter_CopyConstructor(&scopy, StdPtrList_Begin((StdPtrList *)&((CMobile *)mob)->combatTargetList, &sbegin));
		siter = scopy;
		for (;;) {
			StdPtrIter_CopyConstructor(&sendCopy, StdPtrList_End((StdPtrList *)&((CMobile *)mob)->combatTargetList, &send));
			if (!(StdPtrIter_Neq(&siter, &sendCopy) & 0xFF))
				break;
			CVector_PushBack(&tmpList, (uintptr_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&siter)));
			StdPtrIter_PostInc(&siter, &spostInc, 0);
		}
	}
	iter = tmpList.begin;
	while (iter != tmpList.end) {
		if (Script_canSeeObj(serial, (uint32_t)*(uintptr_t *)iter))
			CList_Append(list, 4, (uint32_t)*(uintptr_t *)iter);
		iter = (char *)iter + sizeof(uintptr_t);
	}
	CVector_Destructor(&tmpList);
}

/*
 * 0x0041926F - getFirstVisableTarget [617]
 *
 * Returns the serial of the first non-hidden combat target the
 * mobile can see, or 0 when none qualify.
 */
uint32_t
Script_getFirstVisableTarget(uint32_t serial)
{
	CItem *mob, *target;
	CVector tmpList;
	void *iter;
	char typeFlag = 0;
	uint32_t targetSerial;

	mob = FindMobileValidated(serial, "getTargets");
	if (mob == NULL)
		return 0;
	CVector_Constructor(&tmpList, &typeFlag);
	{
		StdPtrNode *siter, *sbegin, *send, *sendCopy, *spostInc, *scopy;
		StdPtrIter_Constructor(&siter);
		StdPtrIter_CopyConstructor(&scopy, StdPtrList_Begin((StdPtrList *)&((CMobile *)mob)->combatTargetList, &sbegin));
		siter = scopy;
		for (;;) {
			StdPtrIter_CopyConstructor(&sendCopy, StdPtrList_End((StdPtrList *)&((CMobile *)mob)->combatTargetList, &send));
			if (!(StdPtrIter_Neq(&siter, &sendCopy) & 0xFF))
				break;
			target = CWorld_FindMobileBySerial(g_World, (uintptr_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&siter)));
			if (target != NULL)
				CVector_PushBack(&tmpList, (uintptr_t)target);
			StdPtrIter_PostInc(&siter, &spostInc, 0);
		}
	}
	iter = tmpList.begin;
	while (iter != tmpList.end) {
		target = *(CItem **)iter;
		if (!VT_IsHidden(target)) {
			targetSerial = CMobile_GetSerial((CMobile *)target);
			if (Script_canSeeObj(serial, targetSerial)) {
				CVector_Destructor(&tmpList);
				return targetSerial;
			}
		}
		iter = (char *)iter + sizeof(uintptr_t);
	}
	CVector_Destructor(&tmpList);
	return 0;
}

/*
 * 0x004193F6 - getFirstVisableTargetInRange [618]
 *
 * Returns the serial of the first visible, non-hidden combat target
 * within the given range. 445 bytes. Same as getFirstVisableTarget
 * but adds a CLocation_ChebyshevDistance check before canSeeObj.
 * Binary uses "getTargets" string (shared with getTargets handler).
 */
uint32_t
Script_getFirstVisableTargetInRange(uint32_t serial, int range)
{
	CItem *mob, *target;
	CVector tmpList;
	void *iter;
	char typeFlag = 0;
	uint32_t targetSerial;

	mob = FindMobileValidated(serial, "getTargets");
	if (mob == NULL)
		return 0;
	CVector_Constructor(&tmpList, &typeFlag);
	{
		StdPtrNode *siter, *sbegin, *send, *sendCopy, *spostInc, *scopy;
		StdPtrIter_Constructor(&siter);
		StdPtrIter_CopyConstructor(&scopy, StdPtrList_Begin((StdPtrList *)&((CMobile *)mob)->combatTargetList, &sbegin));
		siter = scopy;
		for (;;) {
			StdPtrIter_CopyConstructor(&sendCopy, StdPtrList_End((StdPtrList *)&((CMobile *)mob)->combatTargetList, &send));
			if (!(StdPtrIter_Neq(&siter, &sendCopy) & 0xFF))
				break;
			target = CWorld_FindMobileBySerial(g_World, (uintptr_t)CSearchCtx_Find((CSearchCtx *)StdPtrIter_Deref(&siter)));
			if (target != NULL)
				CVector_PushBack(&tmpList, (uintptr_t)target);
			StdPtrIter_PostInc(&siter, &spostInc, 0);
		}
	}
	iter = tmpList.begin;
	while (iter != tmpList.end) {
		target = *(CItem **)iter;
		if (!VT_IsHidden(target)) {
			if (CLocation_ChebyshevDistance(VT_GetLocation(mob), VT_GetLocation(target)) <= range) {
				targetSerial = CMobile_GetSerial((CMobile *)target);
				if (Script_canSeeObj(serial, targetSerial)) {
					CVector_Destructor(&tmpList);
					return targetSerial;
				}
			}
		}
		iter = (char *)iter + sizeof(uintptr_t);
	}
	CVector_Destructor(&tmpList);
	return 0;
}

/*
 * 0x004195B3 - isValid
 *
 * CWorld_FindBySerial, return 1 if found, 0 if not.
 */
int
Script_isValid(uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	return 1;
}

/*
 * 0x004195DB - getNameByType
 *
 * Stores the tiledata name for typeId in dest, or "BUG!" when
 * typeId is out of range. Returns dest.
 */
CString *
Script_getNameByType(CString *dest, int typeId)
{
	char buf[128];

	if (typeId < 0 || typeId > 0x4000) {
		sprintf(buf, "getNameByType: Invalid objectType: %d\n", typeId);
		CString_Constructor(dest, "BUG!");
		return dest;
	}
	CString_Constructor(dest, g_ItemTileData[typeId].name);
	return dest;
}

/*
 * Location Accessors (0x00419661..0x004196B2)
 *
 * Script-level functions for reading/writing components of the 6-byte
 * location struct: {int16_t x, int16_t y, int16_t z}. getX/Y/Z take a loc
 * pointer; setX/Y/Z take loc + value.
 */

/*
 * 0x00419661 - getX
 *
 * Returns the x component of loc, sign-extended.
 */
int
Script_getX(const CLocation *loc)
{
	return (int)(int16_t)loc->x;
}

/*
 * 0x0041966C - getY
 *
 * Returns the y component of loc, sign-extended.
 */
int
Script_getY(const CLocation *loc)
{
	return (int)(int16_t)loc->y;
}

/*
 * 0x00419678 - getZ
 *
 * Returns the z component of loc.
 */
int
Script_getZ(const CLocation *loc)
{
	return (int)loc->z;
}

/*
 * 0x00419684 - setX
 *
 * Stores value as the x component of loc.
 */
void
Script_setX(CLocation *loc, int value)
{
	loc->x = (int16_t)value;
}

/*
 * 0x00419693 - setY
 *
 * Stores value as the y component of loc.
 */
void
Script_setY(CLocation *loc, int value)
{
	loc->y = (int16_t)value;
}

/*
 * 0x004196A3 - setZ
 *
 * Stores value as the z component of loc.
 */
void
Script_setZ(CLocation *loc, int value)
{
	loc->z = (int16_t)value;
}

/*
 * 0x004196B3 - moveDir
 *
 * Steps loc one tile along compass direction dir.
 */
void
Script_moveDir(CLocation *loc, int dir)
{
	CLocation_MoveDir(loc, dir);
}

/*
 * 0x004196C4 - replyTo
 *
 * Looks up an NPC's conversation response to speechStr from target.
 * When a non-empty reply is found, schedules a speech event on the
 * NPC five ticks later carrying the response text.
 */
void
Script_replyTo(uint32_t npcSerial, uint32_t targetSerial, CString *speechStr)
{
	CItem *npc;
	CItem *target;
	const char *text;
	char *response;

	npc = FindMobileEntityValidated(npcSerial, "replyTo");
	if (npc == NULL)
		return;

	target = FindMobileValidated(targetSerial, "replyTo");
	if (target == NULL)
		return;

	text = CString_GetData(speechStr);

	response = CConversationManager_MatchSpeech(&g_ConvoMgr, npc, target, (char *)text);

	if (response[0] == '\0')
		return;

	ScheduleEvent(5, npc->serial, TIMER_EVENT_SPEECH, 0, (intptr_t)response);
}

/*
 * 0x00419747 - replyToMobAbout
 *
 * Like replyTo, but looks up a response that mentions a third
 * mobile (about) in the conversation. The reply, if any, is
 * scheduled five ticks later as a speech event on the speaker.
 */
void
Script_replyToMobAbout(uint32_t speakerSerial, uint32_t targetSerial, uint32_t aboutSerial, CString *speechStr)
{
	CItem *speaker;
	CItem *target;
	CItem *about;
	char *text;
	char *response;

	speaker = FindMobileEntityValidated(speakerSerial, "replyToMobAbout");
	if (speaker == NULL)
		return;

	target = FindMobileValidated(targetSerial, "replyToMobAbout");
	if (target == NULL)
		return;

	about = FindMobileValidated(aboutSerial, "replyToMobAbout");
	if (about == NULL)
		return;

	text = CString_GetData(speechStr);

	response = CConversationManager_InternalFindResponse(speaker, target, about, text);

	if (response[0] == '\0')
		return;

	ScheduleEvent(5, speaker->serial, TIMER_EVENT_SPEECH, 0, (intptr_t)response);
}

/*
 * 0x004197EA - getResourceTypeIdByName
 *
 * Looks up a resource type by name and stores its id in *retval.
 * Returns 1 on a hit, 0 when the name is not registered.
 */
int
Script_getResourceTypeIdByName(int *retval, CString *name)
{
	const char *str;
	CResourceType *resType;

	str = CString_GetCStr(name);
	resType = CResourceTypeManager_FindByName(str);
	if (resType != NULL) {
		*retval = resType->typeId;
		return 1;
	}
	return 0;
}

/*
 * 0x00419822 - hasResource
 *
 * Returns 1 when the entity carries any resource node of type
 * resourceTypeId.
 */
int
Script_hasResource(uint32_t serial, int resourceTypeId)
{
	CItem *ent;
	CResourceNode *node;

	ent = FindEntityValidated(serial, "hasResource");
	if (ent == NULL)
		return 0;
	node = ent->resourceEntity.firstChild;
	while (node != NULL) {
		if ((int)node->id == resourceTypeId)
			return 1;
		node = node->next;
	}
	return 0;
}

/*
 * 0x0041987D - getStaticObjectsAt / getStaticObjectsAtXY
 *
 * Appends the body type (art id) of every static item sharing
 * loc's (x, y) to list.
 */
void
Script_getStaticObjectsAt(CList *list, CLocation *loc)
{
	int blockIdx;
	CItem *ent;

	CList_Clear(list);
	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return;
	ent = g_MapBlocks[blockIdx].staticHead;
	while (ent != NULL) {
		if (CLocation_IsEqualXY(loc, CEntity_GetLocation(&ent->resourceEntity.entity))) {
			uint16_t artId = CEntity_GetBodyType(ent);
			CList_Append(list, 0, (uintptr_t)(artId & 0xFFFF));
		}
		ent = ent->resourceEntity.nextInContainer;
	}
}

/*
 * 0x00419903 - getStaticObjectsAtXYZ
 *
 * Like getStaticObjectsAt, but matches the full (x, y, z) triple.
 */
void
Script_getStaticObjectsAtXYZ(CList *list, CLocation *loc)
{
	int blockIdx;
	CItem *ent;

	CList_Clear(list);
	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (blockIdx < 0)
		return;
	ent = g_MapBlocks[blockIdx].staticHead;
	while (ent != NULL) {
		if (CLocation_IsEqualXYZ(loc, CEntity_GetLocation(&ent->resourceEntity.entity))) {
			uint16_t artId = CEntity_GetBodyType(ent);
			CList_Append(list, 0, (uintptr_t)(artId & 0xFFFF));
		}
		ent = ent->resourceEntity.nextInContainer;
	}
}

/*
 * 0x00419989 - hasObj_search
 *
 * Recursive helper for hasObj. Searches a container's contents for an item
 * with the given serial. If a child is itself a container (vtable[0xD4]),
 * recurses into it.
 */
static int
hasObj_search(CItem *container, uint32_t serial)
{
	CItem *cur;

	cur = ((CContainer *)container)->contents;
	while (cur != NULL) {
		if (cur->serial == serial)
			return 1;
		if (CItem_IsContainer(cur)) {
			if (hasObj_search(cur, serial))
				return 1;
		}
		cur = cur->spatialNext;
	}
	return 0;
}

/*
 * 0x004199EE - hasObjType_search
 *
 * Recursive helper for hasObjType/hasObjTypeInBank. Searches a container's
 * contents for an item with the given bodyType. If a child is itself a
 * container (vtable[0xD4]), recurses into it.
 */
static int
hasObjType_search(CItem *container, int type)
{
	CItem *cur;

	cur = ((CContainer *)container)->contents;
	while (cur != NULL) {
		if ((CEntity_GetBodyType(cur) & 0xFFFF) == type)
			return 1;
		if (CItem_IsContainer(cur)) {
			if (hasObjType_search(cur, type))
				return 1;
		}
		cur = cur->spatialNext;
	}
	return 0;
}

/*
 * 0x00419A5A - hasObj
 *
 * Checks if a mobile has an item (by serial) in equipment or nested inside
 * equipment containers. Iterates 26 equipment slots, compares serial, then
 * recurses into containers via hasObj_search.
 */
int
Script_hasObj(uint32_t mobSerial, uint32_t objSerial)
{
	CItem *ent;
	CMobile *mob;
	int i;

	ent = FindMobileValidated(mobSerial, "hasObj");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	for (i = 0; i < 0x1a; i++) {
		if (mob->equipment[i] != NULL) {
			if (mob->equipment[i]->serial == objSerial)
				return 1;
			if (CItem_IsContainer(mob->equipment[i])) {
				if (hasObj_search(mob->equipment[i], objSerial))
					return 1;
			}
		}
	}
	return 0;
}

/*
 * 0x00419B1B - hasObjType
 *
 * Checks if a mobile has an item of the given bodyType in equipment or
 * nested inside equipment containers. Iterates 26 equipment slots, compares
 * bodyType, then recurses into containers via hasObjType_search.
 */
int
Script_hasObjType(uint32_t mobSerial, int type)
{
	CItem *ent;
	CMobile *mob;
	int i;

	ent = FindMobileValidated(mobSerial, "hasObjType");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	for (i = 0; i < 0x1a; i++) {
		if (mob->equipment[i] != NULL) {
			if ((CEntity_GetBodyType(mob->equipment[i]) & 0xFFFF) == type)
				return 1;
			if (CItem_IsContainer(mob->equipment[i])) {
				if (hasObjType_search(mob->equipment[i], type))
					return 1;
			}
		}
	}
	return 0;
}

/*
 * 0x00419BE7 - hasObjTypeInBank
 *
 * Checks if a mobile's bank box (equipment[29]) contains an item of the
 * given bodyType. Validates bank box exists and is a container, then
 * searches recursively via hasObjType_search.
 */
int
Script_hasObjTypeInBank(uint32_t mobSerial, int type)
{
	CItem *ent;
	CMobile *mob;

	ent = FindMobileValidated(mobSerial, "hasObjTypeInBank");
	if (ent == NULL)
		return 0;
	mob = (CMobile *)ent;
	if (mob->equipment[29] == NULL)
		return 0;
	if (!CItem_IsContainer(mob->equipment[29]))
		return 0;
	return hasObjType_search(mob->equipment[29], type);
}

/*
 * 0x00419C51 - containsObj_search
 *
 * Recursive helper for containsObj/mobileContainsObj. Checks if target
 * equals item (pointer comparison), then if item is a container
 * (vtable[0xD4]), recurses through its contents.
 */
static CItem *
containsObj_search(CItem *item, CItem *target)
{
	CItem *cur, *result;

	if (item == NULL)
		return NULL;
	if (target == NULL)
		return NULL;
	if (target == item)
		return item;
	if (!CItem_IsContainer(item))
		return NULL;
	cur = ((CContainer *)item)->contents;
	while (cur != NULL) {
		result = containsObj_search(cur, target);
		if (result != NULL)
			return result;
		cur = cur->spatialNext;
	}
	return NULL;
}

/*
 * 0x00419CC6 - containsObjType_search
 *
 * Recursive helper for containsObjType/mobileContainsObjType. Checks if
 * item's bodyType matches, then if item is a container (vtable[0xD4]),
 * recurses through its contents. Returns matching item or NULL.
 */
static CItem *
containsObjType_search(CItem *item, int type)
{
	CItem *cur, *result;

	if (item == NULL)
		return NULL;
	if ((CEntity_GetBodyType(item) & 0xFFFF) == type)
		return item;
	if (!CItem_IsContainer(item))
		return NULL;
	cur = ((CContainer *)item)->contents;
	while (cur != NULL) {
		result = containsObjType_search(cur, type);
		if (result != NULL)
			return result;
		cur = cur->spatialNext;
	}
	return NULL;
}

/*
 * 0x00419D3F - containsObj
 *
 * Checks if a container contains a specific entity (by pointer match via
 * containsObj_search). Binary iterates container->contents but passes
 * the container itself (not the iterator) to the recursive helper each time.
 */
int
Script_containsObj(uint32_t containerSerial, uint32_t entitySerial)
{
	CItem *container, *target, *cur, *result;

	container = FindContainerValidated(containerSerial, "containsObj");
	target = FindEntityValidated(entitySerial, "containsObj");
	if (container == NULL)
		return 0;
	if (target == NULL)
		return 0;
	cur = ((CContainer *)container)->contents;
	while (cur != NULL) {
		result = containsObj_search(container, target);
		if (result != NULL)
			return 1;
		cur = cur->spatialNext;
	}
	return 0;
}

/*
 * 0x00419DBF - containsObjType
 *
 * Searches a container for an item of the given bodyType. Returns the
 * serial of the found item, or 0 if not found. Binary iterates
 * container->contents but passes the container itself to the recursive
 * helper each time.
 */
uint32_t
Script_containsObjType(uint32_t containerSerial, int type)
{
	CItem *container, *cur, *result;

	container = FindContainerValidated(containerSerial, "containsObjType");
	if (container == NULL)
		return 0;
	cur = ((CContainer *)container)->contents;
	while (cur != NULL) {
		result = containsObjType_search(container, type);
		if (result != NULL)
			return result->serial;
		cur = cur->spatialNext;
	}
	return 0;
}

/*
 * 0x00419E26 - getTopmostContainer
 *
 * Returns the serial of the entity at the top of the parent chain,
 * or 0 when the entity is not contained.
 */
uint32_t
Script_getTopmostContainer(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getTopmostContainer");
	if (ent == NULL)
		return 0;
	if (ent->parent == NULL)
		return 0;
	ent = ent->parent;
	while (ent->parent != NULL)
		ent = ent->parent;
	return ent->serial;
}

/*
 * 0x00419E80 - mobileContainsObj
 *
 * Checks if a mobile contains a specific entity. Uses CWorld_FindBySerial
 * for both args, validates IsMobile. Searches equipment slots (pointer
 * comparison + recursive container search), then iterates mob->contents.
 */
int
Script_mobileContainsObj(uint32_t mobSerial, uint32_t targetSerial)
{
	CItem *entity, *target, *cur, *result;
	CMobile *mob;
	int i;

	entity = CWorld_FindBySerial(g_World, mobSerial);
	target = CWorld_FindBySerial(g_World, targetSerial);
	if (entity == NULL)
		return 0;
	if (!VT_IsMobile(entity))
		return 0;
	if (target == NULL)
		return 0;
	mob = (CMobile *)entity;
	for (i = 0; i < 0x1a; i++) {
		if (mob->equipment[i] != NULL) {
			if (mob->equipment[i] == target)
				return 1;
			if (CItem_IsContainer(mob->equipment[i])) {
				result = containsObj_search(mob->equipment[i], target);
				if (result != NULL)
					return 1;
			}
		}
	}
	cur = ((CContainer *)entity)->contents;
	while (cur != NULL) {
		result = containsObj_search(entity, target);
		if (result != NULL)
			return 1;
		cur = cur->spatialNext;
	}
	return 0;
}

/*
 * 0x00419FA9 - mobileContainsObjType
 *
 * Searches a mobile for an item of the given bodyType. Uses
 * CWorld_FindBySerial, validates IsMobile. Searches equipment slots
 * (bodyType comparison + recursive container search), then iterates
 * mob->contents. Returns serial of found item, or 0 if not found.
 */
uint32_t
Script_mobileContainsObjType(uint32_t mobSerial, int type)
{
	CItem *entity, *cur, *result;
	CMobile *mob;
	int i;

	entity = CWorld_FindBySerial(g_World, mobSerial);
	if (entity == NULL)
		return 0;
	if (!VT_IsMobile(entity))
		return 0;
	mob = (CMobile *)entity;
	for (i = 0; i < 0x1a; i++) {
		if (mob->equipment[i] != NULL) {
			if ((CEntity_GetBodyType(mob->equipment[i]) & 0xFFFF) == type)
				return mob->equipment[i]->serial;
			if (CItem_IsContainer(mob->equipment[i])) {
				result = containsObjType_search(mob->equipment[i], type);
				if (result != NULL)
					return result->serial;
			}
		}
	}
	cur = ((CContainer *)entity)->contents;
	while (cur != NULL) {
		result = containsObjType_search(entity, type);
		if (result != NULL)
			return result->serial;
		cur = cur->spatialNext;
	}
	return 0;
}

/*
 * 0x0041A0DF - GetObjectsOfType_Recursive
 *
 * Recursive helper for getObjectsOfTypeIn. Iterates container contents
 * and adds serials of items whose bodyType matches typeId to the list.
 * Skips removed-from-world items. Recurses into sub-containers
 * (vtable[0xD4]). After contents, if the container is a mobile
 * (vtable[0xD0]), iterates equipment slots 0-25 with the same bodyType
 * check and container recursion.
 */
static void
GetObjectsOfType_Recursive(CList *list, CItem *container, int typeId)
{
	CItem *cur;
	CMobile *mob;
	int i;

	cur = ((CContainer *)container)->contents;
	while (cur != NULL) {
		if (!cur->resourceEntity.entity.removedFromWorld) {
			if ((CEntity_GetBodyType(cur) & 0xFFFF) == typeId)
				CList_Append(list, 4, cur->serial);
			if (VT_IsMobile2(cur))
				GetObjectsOfType_Recursive(list, cur, typeId);
		}
		cur = cur->spatialNext;
	}
	if (VT_IsMobile(container)) {
		mob = (CMobile *)container;
		for (i = 0; i < 0x1A; i++) {
			if (mob->equipment[i] != NULL) {
				if ((CEntity_GetBodyType(mob->equipment[i]) & 0xFFFF) == typeId)
					CList_Append(list, 4, mob->equipment[i]->serial);
				if (VT_IsMobile2(mob->equipment[i]))
					GetObjectsOfType_Recursive(list, mob->equipment[i], typeId);
			}
		}
	}
}

/*
 * 0x0041A224 - getObjectsOfTypeIn
 *
 * Finds all items of the given bodyType inside a container (recursively,
 * including nested containers and mobile equipment slots). Validates
 * container via FindContainerValidated, then delegates to the recursive
 * helper.
 */
void
Script_getObjectsOfTypeIn(CList *list, uint32_t containerSerial, int typeId)
{
	CItem *container;

	container = FindContainerValidated(containerSerial, "getObjectsOfTypeIn");
	if (container == NULL)
		return;
	GetObjectsOfType_Recursive(list, container, typeId);
}

/*
 * 0x0041A25C - getContainersOnMobile
 *
 * Appends the serial of every equipped container the mobile is
 * carrying to list.
 */
void
Script_getContainersOnMobile(CList *list, uint32_t serial)
{
	CItem *ent;
	CMobile *mob;
	int i;

	ent = CWorld_FindBySerial(g_World, serial);
	CList_Clear(list);
	if (ent == NULL)
		return;
	if (!VT_IsMobile(ent))
		return;

	mob = (CMobile *)ent;
	for (i = 0; i < 0x1A; i++) {
		CItem *eq = mob->equipment[i];
		if (eq == NULL)
			continue;
		if (!((int (*)(void *))VT_FN(eq, 0xD8))(eq))
			continue;
		CList_Append(list, 4, eq->serial);
	}
}

/*
 * 0x0041A30F - useItem
 *
 * Triggers a scripted "use" of item by player, firing the
 * UseObject and UseItem events and falling back to the default
 * tile-flag handler if neither is consumed.
 */
void
Script_useItem(uint32_t playerSerial, uint32_t itemSerial)
{
	CItem *player;
	CItem *item;

	player = CWorld_FindBySerial(g_World, playerSerial);
	item = CWorld_FindBySerial(g_World, itemSerial);

	if (player == NULL)
		return;
	if (item == NULL)
		return;
	if (!VT_IsMobile(player))
		return;

	UseItemDispatch(player, item);
}

/*
 * 0x0041A36F - getTemplate
 *
 * Returns the entity's template index, or 0 when the serial is
 * unknown.
 */
int
Script_getTemplate(uint32_t serial)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	return CResourceEntity_GetTemplateIndex(ent) & 0xFFFF;
}

/*
 * 0x0041A39F - becomeTemplate [486]
 *
 * Switches the mobile over to template templateId, applying the
 * template's body type, stats, and equipment.
 */
void
Script_becomeTemplate(uint32_t serial, int templateId)
{
	CItem *mob;

	mob = FindMobileValidated(serial, "becomeTemplate");
	if (mob == NULL)
		return;
	CMobile_BecomeTemplate((CMobile *)mob, templateId, 1);
}

/*
 * 0x0041A3D1 - getDefaultAlignment
 *
 * Returns the alignment value declared by template templateId, or
 * 0 when no such template exists.
 */
int
Script_getDefaultAlignment(int templateId)
{
	NPCTemplate *tmpl;

	if (!CResManager_HasByInt(&g_TemplatesRM, templateId))
		return 0;
	tmpl = CResManager_GetTemplateByID((uint16_t)templateId);
	return (int)tmpl->alignment;
}

/*
 * 0x0041A400 - Script_setAlignment
 *
 * Validates mobile by serial, range-checks alignment to [0,3],
 * then calls CMobile::SetAlignment to set resources and flags.
 */
void
Script_setAlignment(uint32_t serial, int alignment)
{
	CMobile *mob;

	mob = (CMobile *)FindMobileValidated(serial, "setAlignment");
	if (mob == NULL)
		return;
	if (alignment < 0)
		return;
	if (alignment > 3)
		return;
	CMobile_SetAlignment(mob, alignment);
}

/*
 * 0x0041A43E - isAtHome
 *
 * FindEntityValidated, then CEntity_IsAtHome (0x0048783D).
 */
int
Script_isAtHome(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "isAtHome");
	if (ent == NULL)
		return 0;
	return CEntity_IsAtHome(ent);
}

/*
 * 0x0041A46C - thinksItsAtHome
 *
 * Returns 1 when a non-mobile, non-contained item is sitting at
 * the location stored in its "home" ObjVar.
 */
int
Script_thinksItsAtHome(uint32_t serial)
{
	CItem *ent;
	CLocation homeLoc, curLoc;

	ent = FindEntityValidated(serial, "isAtHome");
	if (ent == NULL)
		return 0;
	if (((int (*)(void *))VT_FN(ent, VT_HAS_CONTAINER))(ent))
		return 0;
	if (VT_IsMobile(ent))
		return 0;

	CLocation_Init(&homeLoc);
	if (!CItem_GetHomeLocation(ent, &homeLoc))
		return 0;

	CLocation_SetLoc(&curLoc, ((CLocation * (*)(CItem *)) VT_FN(ent, VT_GET_LOCATION))(ent));

	if ((int16_t)curLoc.x == (int16_t)homeLoc.x && (int16_t)curLoc.y == (int16_t)homeLoc.y && (int16_t)curLoc.z == (int16_t)homeLoc.z)
		return 1;
	return 0;
}

/*
 * 0x0041A51B - hasHome
 *
 * Returns 1 when the NPC has the home-location behavior flag set.
 */
int
Script_hasHome(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileEntityValidated(serial, "hasHome");
	if (ent == NULL)
		return 0;
	if (((CNPC *)ent)->behaviorFlags & 0x800)
		return 1;
	return 0;
}

/*
 * 0x0041A55D - getHome
 *
 * Stores the NPC's home location in retloc, falling back to
 * (-1, -1, 0) when the serial is invalid. Returns retloc.
 */
CLocation *
Script_getHome(CLocation *retloc, uint32_t serial)
{
	CItem *ent;

	ent = FindMobileEntityValidated(serial, "getHome");
	if (ent == NULL) {
		CLocation_Constructor3D(retloc, -1, -1, 0);
		return retloc;
	}
	CLocation_SetLoc(retloc, &((CNPC *)ent)->homeLoc);
	return retloc;
}

/*
 * 0x0041A5A7 - setHome
 *
 * Sets the NPC's home location to loc and clears the auxiliary
 * homeInfo3 field.
 */
void
Script_setHome(uint32_t serial, CLocation *loc)
{
	CItem *ent;
	CNPC *npc;

	ent = FindMobileEntityValidated(serial, "setHome");
	if (ent == NULL)
		return;
	npc = (CNPC *)ent;
	CLocation_SetLoc(&npc->homeLoc, loc);
	npc->homeInfo3 = 0;
}

/*
 * 0x0041A5EA - getCreationLoc
 *
 * Stores the entity's creation-time location in retloc, falling
 * back to (-1, -1, 0) when the serial is invalid. Returns retloc.
 */
CLocation *
Script_getCreationLoc(CLocation *retloc, uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getLoc");
	if (ent == NULL) {
		CLocation_Constructor3D(retloc, -1, -1, 0);
		return retloc;
	}
	CLocation_SetLoc(retloc, (CLocation *)&ent->resourceEntity.nextInContainer);
	return retloc;
}

/*
 * 0x0041A631 - doLookAt [519]
 *
 * Performs a programmatic look-at on target from the player's
 * perspective.
 */
void
Script_doLookAt(uint32_t playerSerial, uint32_t targetSerial)
{
	CItem *player;

	player = FindPlayerValidated(playerSerial, "cf_dolookat");
	if (player == NULL)
		return;
	Handle_LookAt((CPlayer *)player, targetSerial);
}

/*
 * 0x0041A665 - getArticle
 *
 * Stores the article prefix for typeId in dest based on the
 * TF_ARTICLE_A / TF_ARTICLE_AN tiledata flags: both set -> "the",
 * only A -> "a", only AN -> "an", neither -> "". Out-of-range
 * typeId stores "BUG!".
 */
CString *
Script_getArticle(CString *dest, int typeId)
{
	char buf[128];
	uint32_t flags;

	if (typeId < 0 || typeId > 0x4000) {
		sprintf(buf, "cf_getarticle(): Invalid objectType: %d\n", typeId);
		CString_Constructor(dest, "BUG!");
		return dest;
	}
	flags = g_ItemTileData[typeId].flags;
	if (flags & TF_ARTICLE_A) {
		if (flags & TF_ARTICLE_AN) {
			CString_Constructor(dest, "the");
			return dest;
		}
	}
	if (flags & TF_ARTICLE_A) {
		CString_Constructor(dest, "a");
		return dest;
	}
	if (flags & TF_ARTICLE_AN) {
		CString_Constructor(dest, "an");
		return dest;
	}
	CString_Constructor(dest, "");
	return dest;
}

/*
 * 0x0041A7A9 - getResource [548]
 *
 * Stores in *outAmount the quantity of resource resName on the
 * entity. flags=0 reads from the entity's resource template slot;
 * flags=2 reads from the entity's ObjVar table. Returns 1 on
 * success, 0 for unknown resources or unsupported flags.
 */
int
Script_getResource(int *outAmount, uint32_t serial, CString *resName, int resType, int flags)
{
	CItem *ent;
	CResourceType *resDef;
	CItem *slot;

	*outAmount = 0;
	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	resDef = CResourceTypeManager_FindByName(CString_GetBuffer(resName));
	if (resDef == NULL)
		return 0;
	if (resType != 3)
		return 0;
	if (flags == 0) {
		slot = (CItem *)&g_ResEntitySlots[CEntity_GetBodyType(ent) & 0xFFFF];
		*outAmount = CItem_GetResourceAmount(slot, (uint16_t)resDef->typeId);
	} else if (flags == 2) {
		*outAmount = 0;
		CItem_GetObjVarResTypeInner(ent, outAmount, resDef, 3, 2);
	} else {
		return 0;
	}
	return 1;
}

/*
 * 0x0041A872 - getResourcesOnObj [549]
 *
 * Appends the names of every resource node on the entity whose
 * type matches flags to list, formatted as WTYPE_STRING entries.
 */
int
Script_getResourcesOnObj(uint32_t serial, int flags, CList *list)
{
	CItem *ent;
	CResourceNode *node;
	uint8_t tmpStr[sizeof(CString)];
	const char *name;

	CString_DefaultConstructor((CString *)tmpStr);
	CList_Clear(list);
	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	node = ent->resourceEntity.firstChild;
	while (node != NULL) {
		if ((int)node->type == flags) {
			if (CResourceNode_GetResourceDef(node) != NULL) {
				name = CResourceType_GetInternalName(CResourceNode_GetResourceDef(node));
				CString_AssignCStr((CString *)tmpStr, name);
				CList_Append(list, WTYPE_STRING, (uintptr_t)tmpStr);
			}
		}
		node = node->next;
	}
	CString_Destructor((CString *)tmpStr);
	return 1;
}

/*
 * 0x0041A954 - getResourceName [550]
 *
 * Looks up the resource type named inStr and stores its localized
 * name (food / generic / alt1 / alt2 selected by resId 0-3) in
 * outStr. Empty string for unknown resources or unsupported resIds.
 */
CString *
Script_getResourceName(CString *outStr, CString *inStr, int resId)
{
	CResourceType *resDef;
	uint8_t tmpStr[sizeof(CString)];

	CString_DefaultConstructor((CString *)tmpStr);
	resDef = CResourceTypeManager_FindByName(CString_GetCStr(inStr));
	if (resDef != NULL && (unsigned int)resId <= 3) {
		switch (resId) {
		case 0:
			CString_AssignCStr((CString *)tmpStr, CResourceType_GetFoodName(resDef));
			break;
		case 1:
			CString_AssignCStr((CString *)tmpStr, CResourceType_GetName1(resDef));
			break;
		case 2:
			CString_AssignCStr((CString *)tmpStr, CResourceType_GetName2(resDef));
			break;
		case 3:
			CString_AssignCStr((CString *)tmpStr, CResourceType_GetName3(resDef));
			break;
		}
	}
	CString_CopyConstructor(outStr, (CString *)tmpStr);
	CString_Destructor((CString *)tmpStr);
	return outStr;
}

/*
 * CHintItem helper stubs.
 * CHintItem is 0xFC (252) bytes. Binary layout:
 *   0x00: int hintId
 *   0x04: uint32_t serial
 *   0x08: int flags
 *   0x0C: char name1[] (embedded string buffer)
 *   0x4C: char name2[] (embedded string buffer)
 *   0xCC: CLocation location (6 bytes)
 *   0xD4: uint32_t objSerial
 *   0xD8: char name3[] (embedded string buffer)
 *   0xF8: int param
 */

// 0x00698E90 - g_HintManager (static CResManager, 0x218 bytes in binary)
CResManager g_HintManager;

/*
 * 0x0041AA42 - updateHint [574]
 *
 * Allocates and registers a CHintItem populated from the arguments
 * with the global hint manager.
 */
void
Script_updateHint(int hintId, uint32_t serial, int flags, CString *name1, CString *name2, CLocation *loc, uint32_t objSerial, CString *name3, int param)
{
	CHintItem *hint;

	hint = (CHintItem *)OperatorNew(sizeof(CHintItem));
	if (hint != NULL)
		CHintItem_Constructor(hint);

	hint->hintId = hintId;
	hint->serial = serial;
	hint->hintFlags = flags;
	CHintItem_SetName1(hint, CString_GetBuffer(name1));
	CHintItem_SetName2(hint, CString_GetBuffer(name2));
	CLocation_SetLoc(&hint->location, loc);
	hint->sourceObj = objSerial;
	CHintItem_SetName3(hint, CString_GetBuffer(name3));
	hint->flags = param;

	CHintManager_Add(&g_HintManager, hint);
}

/*
 * 0x0041AB34 - getHint [575]
 *
 * Looks up a hint by (serial, flags) and unpacks its fields into
 * the output parameters. Returns 1 on a hit, 0 otherwise.
 */
int
Script_getHint(uint32_t serial, int flags, int *outInt1, uint32_t *outInt2, int *outInt3, CString *outStr1, CString *outStr2, CLocation *outLoc, uint32_t *outObjSerial,
        CString *outStr3, int *outParam)
{
	CHintItem hint;

	CHintItem_Constructor(&hint);

	if (!CHintManager_Find(&g_HintManager, serial, flags, &hint)) {
		CHintItem_Destructor(&hint);
		return 0;
	}

	// Copy fields from hint to output args
	*outInt1 = hint.hintId;
	*outInt2 = hint.serial;
	*outInt3 = hint.hintFlags;
	CString_AssignCStr(outStr1, hint.name1);
	CString_AssignCStr(outStr2, hint.name2);
	CLocation_SetLoc(outLoc, &hint.location);
	*outObjSerial = hint.sourceObj;
	CString_AssignCStr(outStr3, hint.name3);
	*outParam = hint.flags;

	CHintItem_Destructor(&hint);
	return 1;
}

/*
 * 0x0041AC3F - shopKeeperOpenBusiness
 *
 * Opens the vendor's buy window for the player.
 */
void
Script_shopKeeperOpenBusiness(uint32_t npcSerial, uint32_t playerSerial)
{
	CItem *ent;
	CItem *player;

	ent = CWorld_FindBySerial(g_World, npcSerial);
	if (ent == NULL)
		return;
	if (!CMobile_IsVendor((CMobile *)ent))
		return;
	player = FindPlayerValidated(playerSerial, "shopKeeperOpenBusiness");
	if (player == NULL)
		return;
	CShopkeeper_OpenBuyWindow((CPlayer *)player, (CMobile *)ent);
}

/*
 * 0x0041AC9C - shopKeeperOpenBuying
 *
 * Opens the vendor's sell window for the player.
 */
void
Script_shopKeeperOpenBuying(uint32_t npcSerial, uint32_t playerSerial)
{
	CItem *ent;
	CItem *player;

	ent = CWorld_FindBySerial(g_World, npcSerial);
	if (ent == NULL)
		return;
	if (!CMobile_IsVendor((CMobile *)ent))
		return;
	player = FindPlayerValidated(playerSerial, "shopKeeperOpenBuying");
	if (player == NULL)
		return;
	CShopkeeper_OpenSellWindow((CPlayer *)player, (CMobile *)ent);
}

/*
 * 0x0041ACF9 - hasShopKeyword
 *
 * Returns 1 when list contains any of the shop-related keywords
 * (buy, trade, commerce, merchant, shop, purchase, business, open,
 * shopkeeper, trader, tradesman, shopkeep) ignoring case.
 */
int
Script_hasShopKeyword(CList *list)
{
	CListNode *node;
	node = list->head;
	while (node != NULL) {
		char *buf;

		buf = CString_GetData((CString *)(uintptr_t)node->value);
		if (strcasecmp(buf, "buy") == 0)
			return 1;
		if (strcasecmp(buf, "trade") == 0)
			return 1;
		if (strcasecmp(buf, "commerce") == 0)
			return 1;
		if (strcasecmp(buf, "merchant") == 0)
			return 1;
		if (strcasecmp(buf, "shop") == 0)
			return 1;
		if (strcasecmp(buf, "purchase") == 0)
			return 1;
		if (strcasecmp(buf, "business") == 0)
			return 1;
		if (strcasecmp(buf, "open") == 0)
			return 1;
		if (strcasecmp(buf, "shopkeeper") == 0)
			return 1;
		if (strcasecmp(buf, "trader") == 0)
			return 1;
		if (strcasecmp(buf, "tradesman") == 0)
			return 1;
		if (strcasecmp(buf, "shopkeep") == 0)
			return 1;
		node = node->next;
	}
	return 0;
}

/*
 * 0x0041AE99 - mobileWillBuy
 *
 * Returns 1 when the vendor NPC is willing to buy the named item.
 */
int
Script_mobileWillBuy(uint32_t npcSerial, uint32_t itemSerial)
{
	CItem *ent;
	CItem *item;

	ent = CWorld_FindBySerial(g_World, npcSerial);
	if (ent == NULL)
		return 0;
	if (!CMobile_IsVendor((CMobile *)ent))
		return 0;
	item = CWorld_FindBySerial(g_World, itemSerial);
	if (item == NULL)
		return 0;
	return CShopkeeper_mobileWillBuy((CMobile *)ent, item);
}

/*
 * 0x0041AEF7 - objToStr
 *
 * Stores serial as a decimal string in out and returns out.
 */
CString *
Script_objToStr(CString *out, uint32_t serial)
{
	char buf[32];
	CString tmpStr;

	sprintf(buf, "%u", serial);
	CString_Constructor(&tmpStr, buf);
	CString_CopyConstructor(out, &tmpStr);
	CString_Destructor(&tmpStr);
	return out;
}

/*
 * 0x0041AF8E - scoreToSpace
 *
 * Replaces all underscore characters in a CString with spaces.
 */
void
Script_scoreToSpace(CString *str)
{
	int len, i;

	len = CString_GetLength(str);
	for (i = 0; i < len; i++) {
		if ((char)*CString_CharAt(str, i) == '_')
			*CString_CharAt(str, i) = ' ';
	}
}

/*
 * 0x0041AFE2 - getLocalizedDesc [576]
 *
 * Returns the localized region description for the location range
 * [inLoc1, inLoc2], with the matched region's name in outStr and
 * its centre in outLoc.
 */
int
Script_getLocalizedDesc(CString *outStr, CLocation *outLoc, CLocation *inLoc1, CLocation *inLoc2)
{
	return RegionManager_GetLocalizedDesc(outStr, (int16_t *)outLoc, inLoc1, inLoc2);
}

/*
 * 0x0041B001 - isInRegionWithPrefix
 *
 * Returns 1 when loc lies in any region whose name starts with
 * prefix.
 */
int
Script_isInRegionWithPrefix(uintptr_t prefix_str, uint32_t *loc)
{
	const char *prefix;

	prefix = CString_GetBuffer((void *)(uintptr_t)prefix_str);
	return RegionManager_IsInRegion((CLocation *)loc, prefix);
}

/*
 * 0x0041B025 - getSmallestArea
 *
 * Stores the name of the smallest-area region containing loc in
 * outName_str. Returns 1 on a hit, 0 when no region contains the
 * location.
 */
int
Script_getSmallestArea(uintptr_t outName_str, uint32_t *loc)
{
	CResList sortedList;
	CResListNode *node;
	CRegion *region;

	CResListNode_Constructor_bin((CResListNode *)&sortedList);

	RegionManager_PopulateByLocation(&sortedList, (CLocation *)loc);

	node = CResList_Begin(&sortedList);
	if (!CResList_IsValid(&sortedList, node)) {
		CResList_Destructor_ByNameAllVal(&sortedList);
		return 0;
	}

	// First entry = smallest area
	region = *(CRegion **)CResList_GetData(&sortedList, node);

	CString_AssignCStr((CString *)(uintptr_t)outName_str, region->name);

	CResList_Destructor_ByNameAllVal(&sortedList);
	return 1;
}

/*
 * 0x0041B0DE - getTrammelPhase
 *
 * Returns the current Trammel moon phase (0-7).
 */
int
Script_getTrammelPhase(void)
{
	return CTimeManager_GetTrammelPhase();
}

/*
 * 0x0041B0ED - getFeluccaPhase
 *
 * Returns the current Felucca moon phase (0-7).
 */
int
Script_getFeluccaPhase(void)
{
	return CTimeManager_GetFeluccaPhase();
}

/*
 * 0x0041B0FC - getBookPages
 *
 * Returns the page count for a book entity (from its bookPages
 * ObjVar or tiledata fallback), or 0 when missing.
 */
int
Script_getBookPages(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getBookPages");
	if (ent == NULL)
		return 0;
	return CItem_GetBookPages(ent) & 0xFFFF;
}

/*
 * 0x0041B12F - setROBookNum
 *
 * Sets the entity's bookNum ObjVar to the lower 16 bits of bookNum.
 */
void
Script_setROBookNum(uint32_t serial, int bookNum)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "setROBookNum");
	if (ent == NULL)
		return;
	CItem_SetBookNum(ent, (uint16_t)bookNum);
}

/*
 * 0x0041B15E - getROBookTitle
 *
 * Stores the read-only book's title (looked up by bookNum) in out
 * and returns out.
 */
CString *
Script_getROBookTitle(CString *out, int bookNum)
{
	CString tmp;

	CString_DefaultConstructor(&tmp);
	CString_AssignCStr(&tmp, BookManager_GetTitle(bookNum));
	CString_CopyConstructor(out, &tmp);
	CString_Destructor(&tmp);
	return out;
}

/*
 * 0x0041B1D6 - getMoonPhaseStr
 *
 * Stores a one-character moon-phase glyph (char 0x80 + phase) in
 * out for valid phases 0-7, or " invalid phase " when out of range.
 */
CString *
Script_getMoonPhaseStr(CString *out, int phase)
{
	CString local;

	CString_DefaultConstructor(&local);
	if (phase < 0 || phase > 7) {
		CString_AssignCStr(&local, " invalid phase ");
	} else {
		CString_SetFromChar(&local, (char)(phase + 0x80));
	}
	CString_CopyConstructor(out, &local);
	CString_Destructor(&local);
	return out;
}

/*
 * 0x0041B26A - getMoonGateDest
 *
 * Returns the moongate destination index ((trammel - felucca + arg
 * + 8) modulo 8) for the requested gate slot.
 */
int
Script_getMoonGateDest(int arg)
{
	int diff;

	diff = CTimeManager_GetTrammelPhase() - CTimeManager_GetFeluccaPhase();
	return (arg + diff + 8) % 8;
}

/*
 * 0x0041B2A5 - splitDice
 *
 * Parses a dice expression "[+/-/!]NdF[+/-/!]B" into its sign
 * prefix, numDice, faces, separator, and bonus components.
 */
void
Script_splitDice(CString *diceStr, CString *prefixOut, int *numDiceOut, int *facesOut, CString *sepOut, int *bonusOut)
{
	char buf[128];
	char *ptr;
	int numDice, faces, bonus;

	strncpy(buf, CString_GetBuffer(diceStr), 127);
	buf[127] = '\0';
	ptr = buf;

	numDice = 0;
	CString_AssignCStr(sepOut, "");
	*bonusOut = 0;

	// Parse prefix operator
	if (*ptr == '+') {
		CString_SetFromChar(prefixOut, '+');
		ptr++;
	} else if (*ptr == '-') {
		CString_SetFromChar(prefixOut, '-');
		ptr++;
	} else if (*ptr == '!') {
		CString_SetFromChar(prefixOut, '!');
		ptr++;
	} else {
		CString_AssignCStr(prefixOut, "");
	}

	// Parse numDice digits
	while (*ptr >= '0' && *ptr <= '9') {
		numDice = numDice * 10 + (*ptr - '0');
		ptr++;
	}
	*numDiceOut = numDice;

	faces = 0;
	*facesOut = 0;

	if (*ptr != 'd')
		return;

	// Skip 'd', parse faces
	ptr++;
	while (*ptr >= '0' && *ptr <= '9') {
		faces = faces * 10 + (*ptr - '0');
		ptr++;
	}
	*facesOut = faces;

	// Parse separator
	bonus = 0;
	if (*ptr == '!') {
		ptr++;
		CString_SetFromChar(sepOut, '!');
	} else if (*ptr == '+') {
		ptr++;
		CString_SetFromChar(sepOut, '+');
	} else if (*ptr == '-') {
		ptr++;
		CString_SetFromChar(sepOut, '-');
	} else {
		return;
	}

	// Parse bonus digits
	while (*ptr >= '0' && *ptr <= '9') {
		bonus = bonus * 10 + (*ptr - '0');
		ptr++;
	}
	*bonusOut = bonus;
}

/*
 * 0x0041B4D1 - getLightTime
 *
 * Returns mob's light time remaining.
 */
int
Script_getLightTime(uint32_t serial)
{
	CItem *ent = FindMobileValidated(serial, "getLightTime");
	if (ent == NULL)
		return 0;
	return (int)((CMobile *)ent)->lightTime;
}

/*
 * 0x0041B502 - getLightVal
 *
 * Returns mob's light brightness value.
 */
int
Script_getLightVal(uint32_t serial)
{
	CItem *ent = FindMobileValidated(serial, "getLightVal");
	if (ent == NULL)
		return 0;
	return (int)((CMobile *)ent)->lightVal;
}

/*
 * 0x0041B533 - setLight
 *
 * Sets mob's light effect. Binary pushes args in order that
 * maps to CMobile_SetLight(mob, lightTime=arg2, lightVal=arg1).
 */
void
Script_setLight(uint32_t serial, int lightVal, int lightTime)
{
	CItem *ent = FindMobileValidated(serial, "setLight");
	if (ent == NULL)
		return;
	CMobile_SetLight((CMobile *)ent, (uint8_t)lightTime, (uint8_t)lightVal);
}

/*
 * 0x0041B567 - makeDice
 *
 * Builds a dice expression "<base><N>[d<F>][<sep><B>]" from the
 * given components into out. faces=0 omits the dice term and a
 * zero bonus or empty sep omits the bonus term.
 */
void
Script_makeDice(CString *out, CString *base, int numDice, int faces, CString *sep, int bonus)
{
	CString_Assign(out, base);
	CString_ConcatInt(out, numDice);
	if (faces != 0) {
		CString_ConcatChar(out, 'd');
		CString_ConcatInt(out, faces);
	}
	if (!CString_IsEmpty(sep) && bonus != 0) {
		CString_ConcatCString(out, sep);
		CString_ConcatInt(out, bonus);
	}
}

/*
 * 0x0041B5CA - makeMultiInst [624]
 *
 * Creates a multi of bodyType anchored at loc and returns its
 * serial (0 on failure). The flags script arg is ignored, matching
 * the binary.
 */
uint32_t
Script_makeMultiInst(CLocation *loc, uint32_t bodyType, int flags)
{
	CItem *result;

	USED(flags);

	result = CMultiManager_MakeMulti(&g_MultiManager, (int)bodyType, loc, 0);
	if (result == NULL)
		return 0;
	return CMobile_GetSerial((CMobile *)result);
}

/*
 * 0x0041B5FB - makeMultiInstCheck [625]
 *
 * Creates a multi instance with extended validation checks.
 * Calls CMultiManager::MakeMultiCheck (0x00476A14) with all args.
 * Binary pushes constant 1 and ownerItem=NULL as extra args.
 * Format: "ociiiIiii" - 5th arg (result) is I=int* output.
 * Returns multi serial or 0.
 */
uint32_t
Script_makeMultiInstCheck(CLocation *loc, uint32_t bodyType, int x, int y, intptr_t z, int checkNum, int artworkId, int validate, int flags)
{
	CItem *item;

	USED(flags);

	// Binary arg mapping from Script to MakeMultiCheck:
	// loc, bodyType, x, y, &result, checkNum, artworkId, validate, 1, NULL
	// The 5th script arg (z) is actually the result ptr (I format).
	item = CMultiManager_MakeMultiCheck(&g_MultiManager, (int)bodyType, loc, x, y, (int *)z, checkNum, artworkId, validate, 1, NULL);
	if (item == NULL)
		return 0;
	return CMobile_GetSerial((CMobile *)item);
}

/*
 * 0x0041B646 - moveMulti [626]
 *
 * Moves a multi entity by a direction vector. Validates
 * entity and checks IsMulti before moving.
 * Returns result of CItem::MoveMulti or 0.
 */
int
Script_moveMulti(uint32_t serial, CLocation *loc)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "moveMulti");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	return CItem_MoveMulti(ent, loc);
}

/*
 * 0x0041B688 - canMultiExistAt
 *
 * Checks if a multi can exist at a given location. Validates the entity
 * as a multi owner, gets the slave, and calls CanExistAtWrapper with
 * the location and moveType=0.
 */
int
Script_canMultiExistAt(uint32_t serial, CLocation *loc, int moveType)
{
	CItem *ent;
	CMultiSlave *slave;

	ent = FindEntityValidated(serial, "moveMulti");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	slave = CItem_GetMultiSlave(ent);
	return CMultiSlave_CanExistAtWrapper(slave, loc, moveType, 0, NULL);
}

/*
 * 0x0041B6D9 - moveMultiCheck [627]
 *
 * Moves a multi entity with a check flag. Validates entity
 * and checks IsMulti before moving.
 * Returns result of CItem::MoveMultiCheck or 0.
 */
int
Script_moveMultiCheck(uint32_t serial, CLocation *loc, int checkFlag)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "moveMulti");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	return CItem_MoveMultiCheck(ent, loc, checkFlag);
}

/*
 * 0x0041B71F - recycleMulti [628]
 *
 * Recycles a multi entity. Validates entity, checks IsMulti,
 * calls vtable[0x0C] (remove), gets multi slave, recycles via
 * CMultiManager, then calls vtable[4] (update) with old artwork.
 */
int
Script_recycleMulti(uint32_t serial, int bodyType)
{
	CItem *ent;
	int result;

	ent = FindEntityValidated(serial, "recycleMulti");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);
	result = CMultiManager_RecycleMulti(&g_MultiManager, bodyType, CItem_GetMultiSlave(ent), 0);
	((void (*)(void *, void *))VT_FN(ent, VT_DROP_AT_FEET))(ent, &ent->resourceEntity.entity.location);
	return result;
}

/*
 * 0x0041B791 - recycleMultiCheck [629]
 *
 * Recycles a multi with extended checks. Validates entity,
 * checks IsMulti. Calls CMultiManager::RecycleMultiCheck with
 * the entity's artwork, multi slave, and check flags.
 */
int
Script_recycleMultiCheck(uint32_t serial, int bodyType, int checkFlag)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "recycleMultiCheck");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	return CMultiManager_RecycleMultiCheck(&g_MultiManager, bodyType, CItem_GetMultiSlave(ent), &ent->resourceEntity.entity.location, checkFlag, 0);
}

/*
 * 0x0041B7F1 - recycleMultiCheckRotate [630]
 *
 * Recycles a multi with checks and rotation. Validates entity,
 * checks IsMulti. Calls CMultiManager::RecycleMultiCheckRotate
 * with rotation and check parameters.
 */
int
Script_recycleMultiCheckRotate(uint32_t serial, int bodyType, int rotation, int checkFlag)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "recycleMultiCheckRotate");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	return CMultiManager_RecycleMultiCheckRotate(&g_MultiManager, bodyType, CItem_GetMultiSlave(ent), &ent->resourceEntity.entity.location, rotation, checkFlag, 0);
}

/*
 * 0x0041B855 - moveMultiMapSwitch [631]
 *
 * Moves a multi entity to a different map. Validates entity,
 * checks IsMulti, gets multi slave, then calls
 * CMultiSlave::MapSwitchMove (0x004774A5).
 */
int
Script_moveMultiMapSwitch(uint32_t serial, CLocation *loc, int mapId)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "moveMulti");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	return CMultiSlave_MapSwitchMove(CItem_GetMultiSlave(ent), loc, mapId);
}

/*
 * 0x0041B8A2 - debugMessage
 *
 * Sends a system message to the entity's "debugger" player.
 * Gets the current thread's entity, reads the "debugger" ObjVar
 * (type OBJ = player serial), looks up the debugger player,
 * and sends the message text as a system message.
 */
void
Script_debugMessage(CString *message)
{
	CExecThread *thread;
	CItem *entity;
	uint32_t debuggerSerial;
	CItem *player;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread == NULL)
		return;

	entity = ((ScriptAttachNode *)thread->scriptRef)->entity;
	debuggerSerial = 0;
	CResourceEntity_GetTagObj(entity, "debugger", &debuggerSerial);

	player = FindPlayerValidated(debuggerSerial, NULL);
	if (player == NULL)
		return;

	CPlayer_SystemMessage((CPlayer *)player, CString_GetData(message));
}

/*
 * 0x0041B90B - scriptTrig
 *
 * Fires a script trigger event on a target entity. Validates both
 * target and arg entities via FindEntityValidated. Uses a switch on
 * trigType to determine if the trigger type is valid (15 specific
 * values). For valid types, gets the current thread entity, fires
 * the event via Entity_ExecuteEvent, then verifies the caller
 * entity still exists afterward. If the caller entity pointer
 * changed (entity deleted during trigger), finishes the current
 * thread. For invalid trigger types, finishes the thread immediately.
 */
void
Script_scriptTrig(uint32_t serial, int trigType, uint32_t objArgSerial)
{
	CItem *ent, *objArg, *callerEntity, *found;
	uint32_t callerSerial;

	ent = FindEntityValidated(serial, "scriptTrig (object)");
	if (ent == NULL)
		return;

	objArg = FindEntityValidated(objArgSerial, "scriptTrig (objArg)");
	if (objArg == NULL)
		return;

	switch (trigType) {
	case 1:
	case 2:
	case 7:
	case 11:
	case 12:
	case 13:
	case 16:
	case 17:
	case 23:
	case 28:
	case 39:
	case 40:
	case 41:
	case 42:
	case 60:
		callerEntity = GetCurrentThreadEntity();
		if (callerEntity != NULL)
			callerSerial = CMobile_GetSerial((CMobile *)callerEntity);
		else
			callerSerial = 0;

		Entity_ExecuteEvent(&ent->resourceEntity.entity, trigType, objArgSerial);

		found = CWorld_FindBySerial(g_World, callerSerial);
		if (found != callerEntity)
			ThreadList_FinishCurrent(&g_activeThreadList, 0);
		break;

	default:
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		break;
	}
}

/*
 * 0x0041BA50 - processTriggerCmds [524]
 *
 * Spatial trigger/event bus for dungeon levers, puzzles, and interactive
 * objects. Iterates ObjVars "cmd{type}0".."cmd{type}9" on the source
 * entity, where {type} is the first character of cmdStr ('a', 'c', 'd').
 * For each matching ObjVar, finds all entities within radius 10 (blocks)
 * and Chebyshev distance < 15 that also carry the same named ObjVar
 * (the link authorization check via CResourceEntity::CheckTag 0x004CDCAB),
 * then dispatches based on ObjVar name[5]:
 *   'a' - activate:   fires event 0x16 "activate" on each linked target
 *   'd' - deactivate: fires event 0x16 "deactivate" on each linked target
 *   'c' - conditional: parses ObjVar strVal as "varname=srcObjVarName";
 *         if srcObjVarName is empty, calls removeObjVar on each target;
 *         otherwise finds srcObjVarName on the source entity and copies
 *         it (with its type and value) to each linked target as varname.
 * Recursion is suppressed via g_TrigCmdRecurse; depth is capped at 0x20.
 *
 * Binary uses:
 *   CResourceEntity::FindTag (0x004CDC82): 5-char prefix tag lookup
 *   CResourceEntity::CheckTag (0x004CDCAB): tag membership test on nearby entity
 *   CResourceEntity::GetTagList (0x004CDBDA): iterate source entity tags
 *   CResourceEntity::SetTag (0x00425EDF): copy typed tag to target entity
 *   CBlockManager::GetNearbyBlocks (0x0042F2F7): radius-10 block query
 *   CLocation::ChebyshevDistance (0x00420EC0): distance filter < 15
 *   CEntity::ExecuteEvent (0x0042B92F): fire event 0x16 on targets
 *   strcmp (0x004E8910): match ObjVar name for the 'c' conditional case
 */
void
Script_processTriggerCmds(uint32_t serial, CString *cmdStr)
{
	char cmdKey[8];
	char trigChar;
	int blockBuf[0x400];
	uint32_t nearbySerials[0x400];
	int nearbyCount;
	int bi, ni, i;
	TagNode *cmdNode;
	CItem *srcEnt;
	CItem *tgtEnt;
	CItem *nearEnt;
	CItem *callerEntity;
	CItem *found;
	uint32_t callerSerial;
	char subType;
	const char *actionStr;
	char varBuf[256];
	const char *valPtr;
	TagNode *srcNode;
	int k;
	CVector vec;
	char typeFlag;
	uintptr_t *iter;
	TagNode *entry;
	CString varStr;

	if (g_TrigCmdRecurse != 0)
		return;
	if (g_TrigCmdDepth > 0x20) {
		g_TrigCmdRecurse = 1;
		return;
	}
	g_TrigCmdDepth++;

	// Build 5-char tag prefix: "cmd" + cmdByte + digit (digit set per iteration)
	cmdKey[0] = 'c';
	cmdKey[1] = 'm';
	cmdKey[2] = 'd';
	cmdKey[3] = CString_GetData(cmdStr)[0];
	cmdKey[5] = '\0';

	// Outer loop: iterate digit '0' to '9'
	for (trigChar = '0'; trigChar <= '9'; trigChar++) {
		cmdKey[4] = trigChar;

		srcEnt = CWorld_FindBySerial(g_World, serial);
		if (srcEnt == NULL) {
			g_TrigCmdDepth--;
			return;
		}

		cmdNode = CItem_FindTagByPrefix(srcEnt, cmdKey);
		if (cmdNode == NULL) {
			g_TrigCmdDepth--;
			return;
		}

		// Get nearby block indices (radius 10, max 0x400)
		CBlockManager_GetNearbyBlocks(&g_SpatialGrid, CEntity_GetLocation((CEntity *)srcEnt), 10, blockBuf, 0x400);

		// Collect serials of nearby entities within distance < 15
		// that also carry the linked name
		nearbyCount = 0;
		for (bi = 0; blockBuf[bi] != -1; bi++) {
			nearEnt = g_SpatialGrid.cells[blockBuf[bi]].itemHead;
			while (nearEnt != NULL && nearbyCount < 0x400) {
				int dist = CLocation_ChebyshevDistance(CEntity_GetLocation((CEntity *)srcEnt), CEntity_GetLocation((CEntity *)nearEnt));
				if (dist < 15 && CItem_HasLinkedName(srcEnt, cmdNode->name))
					nearbySerials[nearbyCount++] = nearEnt->serial;
				nearEnt = nearEnt->spatialNext;
			}
		}

		// Read action subType from ObjVar name at index 5
		subType = cmdNode->name[5];

		if (subType == 'a' || subType == 'd') {
			actionStr = (subType == 'a') ? "activate" : "deactivate";

			for (ni = 0; ni < nearbyCount; ni++) {
				tgtEnt = CWorld_FindBySerial(g_World, nearbySerials[ni]);
				if (tgtEnt == NULL)
					continue;

				callerEntity = GetCurrentThreadEntity();
				callerSerial = (callerEntity != NULL) ? CMobile_GetSerial((CMobile *)callerEntity) : 0;

				Entity_ExecuteEvent((CEntity *)tgtEnt, 0x16, serial, actionStr, "v");

				found = CWorld_FindBySerial(g_World, callerSerial);
				if (found != callerEntity) {
					ThreadList_FinishCurrent(&g_activeThreadList, 0);
					return;
				}
			}
		} else if (subType == 'c') {
			if (cmdNode->type != WTYPE_STRING)
				continue;

			// Scan for '=' in strVal (up to 255 chars)
			// Binary calls CString_GetCStr each iteration (redundantly)
			for (k = 0; k < 255; k++) {
				varBuf[k] = CString_GetCStr((CString *)(uintptr_t)cmdNode->value)[k];
				if (varBuf[k] == '\0')
					break;
				if (varBuf[k] == '=')
					break;
			}

			if (varBuf[k] == '\0')
				continue;

			varBuf[k] = '\0';
			valPtr = CString_GetCStr((CString *)(uintptr_t)cmdNode->value) + k + 1;

			if (*valPtr == '\0') {
				// Empty value: remove varBuf ObjVar from each target
				CString_Constructor(&varStr, varBuf);
				for (i = 0; i < nearbyCount; i++) {
					Script_removeObjVar(nearbySerials[i], &varStr);
				}
				CString_Destructor(&varStr);
			} else {
				// Non-empty: find ObjVar named valPtr on source,
				// copy it to each target under varBuf
				typeFlag = 0;
				CVector_Constructor(&vec, &typeFlag);
				CItem_GetTagDefListRaw(srcEnt, &vec);

				srcNode = NULL;
				for (iter = (uintptr_t *)vec.begin; iter != (uintptr_t *)vec.end; iter++) {
					entry = (TagNode *)*iter;
					if (strcmp(entry->name, valPtr) == 0) {
						srcNode = entry;
						break;
					}
				}

				if (srcNode == NULL) {
					CVector_Destructor(&vec);
					continue;
				}

				for (i = 0; i < nearbyCount; i++) {
					CItem *tgt = CWorld_FindBySerial(g_World, nearbySerials[i]);
					if (tgt != NULL) {
						CString nameStr;
						CString_Constructor(&nameStr, varBuf);
						ObjVar_SetStr(tgt, &nameStr, srcNode->type, srcNode->value);
					}
				}
				CVector_Destructor(&vec);
			}
		}
	}

	g_TrigCmdDepth--;
}

/*
 * 0x0041C022 - withdrawFromBank
 *
 * Script handler [203]. Validates mobile, subtracts gold from bank,
 * moves result to mobile's backpack via vtable[0xC4] (EquipOn).
 * Returns 1 on success, 0 on failure.
 */
int
Script_withdrawFromBank(uint32_t serial, uint32_t amount)
{
	CItem *mob;
	CItem *goldItem;

	mob = FindMobileValidated(serial, "withdrawFromBank");
	if (mob == NULL)
		return 0;

	goldItem = CMobile_SubtractGold((CMobile *)mob, (int)amount);
	if (goldItem == NULL)
		return 0;

	((void (*)(void *, CMobile *))VT_FN(goldItem, VT_ADD_TO_EQUIP))(goldItem, (CMobile *)mob);

	return 1;
}

/*
 * 0x0041C07A - withdrawAndDestroy
 *
 * Script handler [204]. Same as withdrawFromBank but destroys the
 * gold item instead of putting it in backpack.
 */
int
Script_withdrawAndDestroy(uint32_t serial, uint32_t amount)
{
	CItem *mob;
	CItem *goldItem;

	mob = FindMobileValidated(serial, "withdrawFromBank");
	if (mob == NULL)
		return 0;

	goldItem = CMobile_SubtractGold((CMobile *)mob, (int)amount);
	if (goldItem == NULL)
		return 0;

	if (goldItem != NULL)
		((void (*)(void *))VT_FN(goldItem, VT_DELETE))(goldItem);

	return 1;
}

/*
 * 0x0041C0D4 - depositIntoBank
 *
 * Script handler [206]. Deposits an item into a mobile's bank box.
 * Returns 2 on invalid args, passes through CMobile_PutMoneyInBank result.
 */
int
Script_depositIntoBank(uint32_t mobileSerial, uint32_t entitySerial, int amount)
{
	CItem *mob;
	CItem *entity;

	mob = FindMobileValidated(mobileSerial, "depositIntoBank");
	if (mob == NULL)
		return 2;
	entity = FindEntityValidated(entitySerial, "depositIntoBank");
	if (entity == NULL)
		return 2;
	return CMobile_PutMoneyInBank((CMobile *)mob, entity, amount);
}

/*
 * 0x0041C130 - openBank
 *
 * Script handler [205]. Opens bank gump for a player. Validates
 * player serial, calls CMobile_OpenBankGump with NULL second arg
 * (self-open), then sets bankOpenLoc tag to player's current location
 * via ObjVar_SetStr.
 */
void
Script_openBank(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "openBank");
	if (ent == NULL)
		return;

	CMobile_OpenBankGump((CMobile *)ent, NULL);

	{
		CString name;
		CString_Constructor(&name, "bankOpenLoc");
		ObjVar_SetStr(ent, &name, 3, (uintptr_t)CEntity_GetLocation(&ent->resourceEntity.entity));
	}
}

/*
 * 0x0041C18A - amtGoldInBank
 *
 * Returns the mobile's bank-box gold amount, or -1 when the serial does not
 * resolve.
 */
int
Script_amtGoldInBank(uint32_t mobileSerial)
{
	CItem *mob;

	mob = FindMobileValidated(mobileSerial, "amtGoldInBank");
	if (mob == NULL)
		return -1;
	return CMobile_AmountGoldInBank((CMobile *)mob);
}

/*
 * 0x0041C1B9 - getDecayCount
 *
 * Returns the entity's current decay counter, or -10 when the
 * serial is invalid.
 */
int
Script_getDecayCount(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getDecayCount");
	if (ent == NULL)
		return -10;
	return (int)(CItem_GetDecayCount(ent) & 0xFF);
}

/*
 * 0x0041C1EF - Script handler for getDecayMax [658]
 *
 * Returns the world's decay-counter cap, or -10 when the serial
 * is invalid.
 */
int
Script_getDecayMax(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getDecayMax");
	if (ent == NULL)
		return -10;
	return (int)(CItem_GetGlobalDecayMax(ent) & 0xFF);
}

/*
 * 0x0041C225 - setDecayCount
 *
 * Sets the entity's decay counter to decayCount (negative values
 * are clamped to 0xFF). Returns 1 on success, 0 when the serial
 * is invalid.
 */
int
Script_setDecayCount(uint32_t serial, int decayCount)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "setDecayCount");
	if (ent == NULL)
		return 0;
	if (decayCount < 0)
		decayCount = 0xFF;
	CItem_SetDecayCount(ent, (uint8_t)decayCount);
	return 1;
}

/*
 * 0x0041C269 - getHomeDecayRate
 *
 * Returns the home decay rate from the world.
 */
int
Script_getHomeDecayRate(void)
{
	return (int)CWorld_GetHomeDecayRate() & 0xFF;
}

/*
 * 0x0041C27D - getNonHomeDecayRate
 *
 * Returns the non-home decay rate from the world.
 */
int
Script_getNonHomeDecayRate(void)
{
	return (int)CWorld_GetNonHomeDecayRate() & 0xFF;
}

/*
 * 0x0041C291 - getDefaultDieDecay
 *
 * Returns the world's default decay-counter cap.
 */
int
Script_getDefaultDieDecay(void)
{
	return (int)CWorld_GetDecayMax() & 0xFF;
}

/*
 * 0x0041C2A5 - getDecayInterval
 *
 * Returns the per-bucket decay interval scaled by 2^16
 * (decayInterval << 16 / bucketsPerTick), or 0 when no buckets are
 * scanned per tick.
 */
int
Script_getDecayInterval(void)
{
	int count, base;

	count = (int)CWorld_GetDecayBucketsPerTick();
	if (count <= 0)
		return 0;
	base = (int)CWorld_GetDecayInterval();
	return (base << 16) / (int)CWorld_GetDecayBucketsPerTick();
}

/*
 * 0x0041C2DE - setLooksLikeTemplate
 *
 * Sets the mobile's altBodyType so it visually displays as
 * templateId.
 */
void
Script_setLooksLikeTemplate(uint32_t serial, int templateId)
{
	CItem *ent;
	CMobile *mob;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsMobile(ent))
		return;
	mob = (CMobile *)ent;
	mob->altBodyType = (uint16_t)templateId;
}

/*
 * 0x0041C325 - followNpc
 *
 * Configures an NPC to follow leaderSerial at the given range.
 * Sets the AI state to FOLLOWING and enables the follow-related
 * behavior flags. range=0 also disables the wander-radius flag.
 */
void
Script_followNpc(uint32_t serial, uint32_t leaderSerial, int range)
{
	CItem *ent;
	CNPC *npc;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;

	npc = (CNPC *)ent;
	npc->followObj1 = leaderSerial;
	npc->followObj2 = leaderSerial;
	npc->followObj3 = (uint32_t)range;
	((void (*)(void *, int))VT_FN((CItem *)npc, VT_SET_BEHAVIOR))(npc, 0x3002);
	npc->isWalking = 0;
	npc->aiState = NPC_STATE_FOLLOWING;

	if (range == 0)
		((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x2000);
}

/*
 * 0x0041C3CC - stopFollowing
 *
 * Stops an NPC from following its leader: clears the leader serial
 * and range, disables the follow behaviors, and returns the AI to
 * idle. Note that followObj2 is intentionally left unchanged so
 * the NPC remembers who it was following.
 */
void
Script_stopFollowing(uint32_t serial)
{
	CItem *ent;
	CNPC *npc;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;

	npc = (CNPC *)ent;
	npc->followObj1 = 0;
	npc->followObj3 = 0;
	((void (*)(void *, int))VT_FN((CItem *)npc, VT_CLR_BEHAVIOR))(npc, 0x3002);
	npc->aiState = NPC_STATE_IDLE;
}

/*
 * 0x0041C43F - getLeader
 *
 * Returns the serial of the NPC's current follow leader, or 0 if
 * the NPC is not following anyone.
 */
uint32_t
Script_getLeader(uint32_t serial)
{
	CItem *ent;
	CNPC *npc;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (!VT_IsNPC(ent))
		return 0;
	npc = (CNPC *)ent;
	if (!(npc->behaviorFlags & 0x1000))
		return 0;
	return npc->followObj1;
}

/*
 * 0x0041C498 - deleteIfValid
 *
 * Deletes the entity only when its body type matches the expected
 * bodyType. Refreshes any items that were resting on top of it.
 */
void
Script_deleteIfValid(uint32_t serial, int bodyType)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if ((CEntity_GetBodyType(ent) & 0xFFFF) != (uint32_t)bodyType)
		return;

	{
		CVector contItems;
		char typeFlag = 0;

		CVector_Constructor(&contItems, &typeFlag);

		if (!((int (*)(void *))VT_FN(ent, VT_HAS_CONTAINER))(ent))
			CItem_GetContainerItems(ent, &contItems);

		// Redundant NULL check (binary at 0x0041C50D)
		if (ent != NULL)
			((void (*)(void *))VT_FN(ent, VT_DELETE))(ent);

		CBlockManager_RestoreItems(&g_SpatialGrid, &contItems);
		CVector_Destructor(&contItems);
	}
}

/*
 * 0x0041C54C - deleteIfValidNoFall
 *
 * Like deleteIfValid, but does not refresh items that were resting
 * on top of the deleted entity.
 */
void
Script_deleteIfValidNoFall(uint32_t serial, int bodyType)
{
	CItem *ent;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if ((CEntity_GetBodyType(ent) & 0xFFFF) != (uint32_t)bodyType)
		return;
	// Redundant NULL check (binary at 0x0041C579)
	if (ent == NULL)
		return;
	((void (*)(void *))VT_FN(ent, VT_DELETE))(ent);
}

/*
 * 0x0041C591 - changeRange
 *
 * Searches an entity's ObjVar list for an entry whose value matches
 * oldValue and replaces it with newValue when found. isString=0 selects
 * int ObjVars (type 0x10); nonzero selects string ObjVars (type 0x11).
 * Returns 1 on success, 0 when the entity or matching value is missing.
 */
int
Script_changeRange(uint32_t serial, int isString, int oldValue, int newValue)
{
	CItem *entity;
	CVector list;
	char type;
	uintptr_t *iter;
	uintptr_t entry;

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		return 0;

	if (isString == 0)
		isString = 0x10;
	else
		isString = 0x11;

	type = 0;
	CVector_Constructor(&list, &type);

	CItem_PopulateObjVarList(entity, &list, isString);

	iter = (uintptr_t *)list.begin;
	while (iter != (uintptr_t *)list.end) {
		entry = *iter;
		if ((int)(intptr_t)((CTrigger *)entry)->filterData == oldValue) {
			((CTrigger *)entry)->filterData = (void *)(intptr_t)newValue;
			CVector_Destructor(&list);
			return 1;
		}
		iter++;
	}

	CVector_Destructor(&list);
	return 0;
}

/*
 * 0x0041C677 - isMultiComp
 *
 * FindEntityValidated, then CItem_HasMulti_Filter (0x00486C0E: multiPtr != 0).
 */
int
Script_isMultiComp(uint32_t serial)
{
	CItem *ent = FindEntityValidated(serial, "isMultiComp");
	if (ent == NULL)
		return 0;
	return CItem_HasMulti_Filter(ent);
}

/*
 * 0x0041C6A5 - isMultiSlave
 *
 * FindEntityValidated, then CItem_HasMulti_Filter (same check as isMultiComp).
 */
int
Script_isMultiSlave(uint32_t serial)
{
	CItem *ent = FindEntityValidated(serial, "isMultiSlave");
	if (ent == NULL)
		return 0;
	return CItem_HasMulti_Filter(ent);
}

/*
 * 0x0041C6D3 - getMultiSlaveId
 *
 * Returns the master serial of the multi the entity is a component
 * of, or 0 when the entity is not part of a multi.
 */
uint32_t
Script_getMultiSlaveId(uint32_t serial)
{
	CItem *ent;
	CMultiComponent *mc;

	ent = FindEntityValidated(serial, "getMultiSlaveId");
	if (ent == NULL)
		return 0;

	if (!CItem_HasMulti_Filter(ent))
		return 0;

	mc = CItem_GetMulti(ent);
	return CMulti_GetSerial(mc);
}

/*
 * 0x0041C714 - getMultiComponentOffset
 *
 * Stores in outLoc the entity's offset relative to its multi
 * master, or (-1, -1, -1) when the entity is not a multi component.
 * Returns outLoc.
 */
CLocation *
Script_getMultiComponentOffset(CLocation *outLoc, uint32_t serial)
{
	CItem *ent;
	CMultiComponent *mc;

	ent = FindEntityValidated(serial, "getMultiComponentOffset");
	if (ent == NULL || !CItem_HasMulti_Filter(ent)) {
		CLocation_Constructor3D(outLoc, -1, -1, -1);
		return outLoc;
	}

	mc = CItem_GetMulti(ent);
	CLocation_SetLoc(outLoc, CMulti_GetOffset(mc));
	return outLoc;
}

/*
 * 0x0041C770 - areObjectsOn [636]
 *
 * Checks if any objects exist at the same XYZ location as the
 * given entity. Gets entity location, adds effective height
 * (vtable[0x2C]) to z, queries spatial grid at that location,
 * sorts results, and checks if the first item's z matches.
 * Returns 1 if match found, 0 otherwise.
 */
int
Script_areObjectsOn(uint32_t serial)
{
	CItem *ent;
	CLocation *loc;
	CLocation localLoc;
	int height;
	CVector list;
	char typeFlag;
	CItem *item;
	CLocation *itemLoc;
	int result;

	ent = FindEntityValidated(serial, "areObjectsOn");
	if (ent == NULL)
		return 0;

	loc = &ent->resourceEntity.entity.location;
	CLocation_SetLoc(&localLoc, loc);

	height = VT_GetEffectiveHeight(ent);
	localLoc.z += (int16_t)height;

	typeFlag = 0;
	CVector_Constructor(&list, &typeFlag);

	CBlockManager_GetItemsAtLocationXYZ(&g_SpatialGrid, &list, &localLoc);
	Vector_SortByZ(list.begin, list.end, list.type);

	result = 0;
	if (list.begin != list.end) {
		item = *(CItem **)list.begin;
		itemLoc = &item->resourceEntity.entity.location;
		if (itemLoc->z == localLoc.z)
			result = 1;
	}

	CVector_Destructor(&list);
	return result;
}

/*
 * 0x0041C886 - getMultiExtents
 *
 * Returns the min/max extents for a multi type ID.
 */
int
Script_getMultiExtents(int typeId, CLocation *minLoc, CLocation *maxLoc)
{
	return CMultiManager_GetExtents(&g_MultiManager, typeId, minLoc, maxLoc);
}

/*
 * 0x0041C8A1 - isHousingOkay
 *
 * Returns 1 when loc lies in a region that allows housing.
 */
int
Script_isHousingOkay(CLocation *loc, int arg)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;
	return RegionManager_isHousingOkay((int16_t)loc->x, (int16_t)loc->y, (int16_t)loc->z, arg) == 1;
}

/*
 * 0x0041C8E1 - areSpellsOkay
 *
 * Returns 1 when loc lies in a region that permits spell casting.
 */
int
Script_areSpellsOkay(CLocation *loc)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;
	return RegionManager_areSpellsOkay((int16_t)loc->x, (int16_t)loc->y, (int16_t)loc->z) == 1;
}

/*
 * 0x0041C91D - inJusticeRegion
 *
 * Returns 1 when loc lies in a justice region.
 */
int
Script_inJusticeRegion(CLocation *loc)
{
	return RegionManager_inJusticeRegion((int16_t)loc->x, (int16_t)loc->y, (int16_t)loc->z) == 1;
}

/*
 * 0x0041C938 - isInCityRegion
 *
 * Returns 1 when loc lies in a city region.
 */
int
Script_isInCityRegion(CLocation *loc)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;
	return RegionManager_isInCityRegion((int16_t)loc->x, (int16_t)loc->y, (int16_t)loc->z) == 1;
}

/*
 * 0x0041C974 - getMultiType
 *
 * Returns the multi-type id for the entity when it is the multi
 * owner, or 0 otherwise.
 */
int
Script_getMultiType(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getMultiType");
	if (ent == NULL)
		return 0;
	if (!CItem_IsMultiOwner(ent))
		return 0;
	return CMultiSlave_GetTypeId(CItem_GetMultiSlave(ent));
}

/*
 * 0x0041C9B5 - resetMultiCarriedDecay [643]
 *
 * Resets the decay counter to zero on every item carried by the
 * multi the entity owns.
 */
void
Script_resetMultiCarriedDecay(uint32_t serial)
{
	CItem *ent;
	CMultiSlave *slave;
	CVector list;
	char typeFlag;
	uintptr_t *iter;

	ent = FindEntityValidated(serial, "getMultiType");
	if (ent == NULL)
		return;
	if (!CItem_IsMultiOwner(ent))
		return;

	typeFlag = 0;
	CVector_Constructor(&list, &typeFlag);

	slave = CItem_GetMultiSlave(ent);
	CMultiSlave_GetItems(slave, &list);

	iter = (uintptr_t *)list.begin;
	while (iter != (uintptr_t *)list.end) {
		CItem *item = (CItem *)*iter;
		CItem_SetDecayCount(item, 0);
		iter++;
	}

	CVector_Destructor(&list);
}

/*
 * 0x0041CA6C - getPlayersOnMulti [654]
 *
 * Fills a CList with serials of all players on a multi.
 * Validates entity as multi owner, gets multi slave, fills
 * a CVector with all items via CMultiSlave_GetItems,
 * then appends serials of player entities to the output list.
 */
void
Script_getPlayersOnMulti(CList *list, uint32_t serial)
{
	CItem *ent;
	CMultiSlave *slave;
	CVector tmpList;
	char typeFlag;
	uintptr_t *iter;

	ent = FindEntityValidated(serial, "getPlayersOnMulti");
	if (ent == NULL)
		return;
	if (!CItem_IsMultiOwner(ent))
		return;

	typeFlag = 0;
	CVector_Constructor(&tmpList, &typeFlag);

	slave = CItem_GetMultiSlave(ent);
	CMultiSlave_GetItems(slave, &tmpList);

	iter = (uintptr_t *)tmpList.begin;
	while (iter != (uintptr_t *)tmpList.end) {
		CItem *item = (CItem *)*iter;
		if (VT_IsPlayer(item)) {
			uint32_t s = CMobile_GetSerial((CMobile *)item);
			CList_Append(list, 4, s);
		}
		iter++;
	}

	CVector_Destructor(&tmpList);
}

/*
 * 0x0041CB42 - getObjectsOnMulti [655]
 *
 * Fills a CList with serials of all objects on a multi.
 * Validates entity as multi owner, gets multi slave, fills
 * a CVector with all items via CMultiSlave_GetItems,
 * then appends serials of all entities to the output list
 * (no player filter, unlike getPlayersOnMulti).
 */
void
Script_getObjectsOnMulti(CList *list, uint32_t serial)
{
	CItem *ent;
	CMultiSlave *slave;
	CVector tmpList;
	char typeFlag;
	uintptr_t *iter;

	ent = FindEntityValidated(serial, "getObjectsOnMulti");
	if (ent == NULL)
		return;
	if (!CItem_IsMultiOwner(ent))
		return;

	typeFlag = 0;
	CVector_Constructor(&tmpList, &typeFlag);

	slave = CItem_GetMultiSlave(ent);
	CMultiSlave_GetItems(slave, &tmpList);

	iter = (uintptr_t *)tmpList.begin;
	while (iter != (uintptr_t *)tmpList.end) {
		CItem *item = (CItem *)*iter;
		uint32_t s = CMobile_GetSerial((CMobile *)item);
		CList_Append(list, 4, s);
		iter++;
	}

	CVector_Destructor(&tmpList);
}

/*
 * 0x0041CC02 - sendPlayerZmoveStuff [656]
 *
 * Sends z-move update packets to nearby players for an entity.
 * Validates player, calls CMobile_NotifyNearbyPlayers, sends update
 * to clients in range, then gets nearby players within range 18 and
 * sends z-move update to each via vtable[0x4C] dispatch.
 */
void
Script_sendPlayerZmoveStuff(uint32_t serial)
{
	CItem *player;
	CLocation loc2;
	CLocation *rootLoc;
	CVector list;
	char typeFlag;

	player = FindPlayerValidated(serial, "sendPlayerZmoveStuff");
	if (player == NULL)
		return;

	CMobile_NotifyNearbyPlayers(player);

	{
		uint8_t obuf[0x100];
		PacketManager_MakePacket_ZMOVE(obuf, (CMobile *)player);
		SendToClient(player, obuf, -1);
	}

	rootLoc = VT_GetLocation(player);
	CLocation_SetLoc(&loc2, rootLoc);

	typeFlag = 0;
	CVector_Constructor(&list, &typeFlag);

	GetNearbyPlayers(&list, &loc2, 18);
	((void (*)(CItem *, CVector *, int))VT_FN(player, VT_NOTIFY_NEARBY))(player, &list, 1);

	CVector_Destructor(&list);
}

/*
 * 0x0041CCD2 - multiCanExistAt
 *
 * Checks if a multi of the given type can exist at the specified location.
 * Passes NULL for the item pointer (4th arg to CanExistAt).
 */
int
Script_multiCanExistAt(CLocation *loc, int typeId, int moveType)
{
	return CMultiManager_CanExistAt(&g_MultiManager, typeId, loc, moveType, NULL);
}

/*
 * 0x0041CCEF - mobileHasObjWithListObjOfObj [664]
 *
 * Searches a mobile's equipment for an item whose tag list contains
 * a given name with the target serial as value. Binary uses
 * FindMobileValidated with "mobileHasObjWithVarObj" error
 * string, then calls vtable[0x164] (CMobile::FindItemByType).
 * Returns the found item's serial, or 0 if not found.
 *
 * Wombat call convention: (mobSerial, name, containerSerial).
 * The builtin signature "OOCS" maps C=serial, S=string, so the
 * dispatcher passes name as the second arg and containerSerial
 * as the third.
 */
uint32_t
Script_mobileHasObjWithListObjOfObj(uint32_t mobSerial, CString *name, uint32_t containerSerial)
{
	CItem *mob;
	CItem *result;

	mob = FindMobileValidated(mobSerial, "mobileHasObjWithVarObj");
	if (mob == NULL)
		return 0;

	result = VT_FindItemByName(mob, name, containerSerial);
	if (result != NULL)
		return CMobile_GetSerial((CMobile *)result);
	return 0;
}

/*
 * 0x0041CD42 - getPulseNum
 *
 * Returns the current game tick count.
 */
int
Script_getPulseNum(void)
{
	return CTimeManager_GetTickCount();
}

/*
 * 0x0041CD51 - isValidMap
 *
 * Checks if a map entity has nonzero width and height extents.
 */
int
Script_isValidMap(uint32_t serial)
{
	CItem *entity;

	entity = FindMapItem(serial, "cf_setmapproperties");
	if (entity == NULL)
		return 0;
	if (((CSignpost *)entity)->mapExtent[4] == 0)
		return 0;
	if (((CSignpost *)entity)->mapExtent[5] == 0)
		return 0;
	return 1;
}

/*
 * 0x0041CD9A - setMapProperties
 *
 * Sets the 6 mapExtent values on a CSignpost entity.
 * Binary has 8 args (opcode 56): serial, unused, x1, y1, x2, y2, width, height.
 * The 2nd arg (slot[2]) is unused by the function body.
 */
void
Script_setMapProperties(uint32_t serial, int unused, int x1, int y1, int x2, int y2, int width, int height)
{
	CItem *entity;

	USED(unused);
	entity = FindMapItem(serial, "cf_setmapproperties");
	if (entity == NULL)
		return;
	((CSignpost *)entity)->mapExtent[0] = (int16_t)x1;
	((CSignpost *)entity)->mapExtent[1] = (int16_t)y1;
	((CSignpost *)entity)->mapExtent[2] = (int16_t)x2;
	((CSignpost *)entity)->mapExtent[3] = (int16_t)y2;
	((CSignpost *)entity)->mapExtent[4] = (int16_t)width;
	((CSignpost *)entity)->mapExtent[5] = (int16_t)height;
}

/*
 * 0x0041CE00 - getMapPoint
 *
 * Walks the VectNode linked list on a signpost to find the node
 * at the given index, converts its pin to world coordinates via
 * CSignpost_MapPinToWorldCoord, and copies to the output location.
 */
int
Script_getMapPoint(CLocation *retloc, uint32_t serial, int index)
{
	CItem *entity;
	VectNode *node;
	int i;

	entity = FindMapItem(serial, "getMapPoint");
	if (entity == NULL)
		return 0;
	node = ((CSignpost *)entity)->vectHead;
	i = 0;
	for (;;) {
		if (node == NULL)
			return 0;
		if (i == index) {
			CLocation_SetLoc(retloc, CSignpost_MapPinToWorldCoord((CSignpost *)entity, node));
			return 1;
		}
		node = node->next;
		i++;
	}
	return 0;
}

/*
 * 0x0041CE7A - copybook [676]
 *
 * Copies the source writable book's pages into dst. Returns 0 when
 * the books are the same, either is invalid, or the destination
 * has no writable bookStatus.
 */
int
Script_copybook(uint32_t srcSerial, uint32_t dstSerial)
{
	CItem *src;
	CItem *dst;

	src = FindBookValidated(srcSerial, "copybook");
	dst = FindBookValidated(dstSerial, "copybook");
	if (srcSerial == dstSerial)
		return 0;
	if (src == NULL || dst == NULL)
		return 0;
	if ((CItem_GetBookStatus(dst) & 0xFF) == 0)
		return 0;
	CopyBook(src, dst);
	return 1;
}

/*
 * 0x0041CEF2 - dropCheck
 *
 * Validates the entity exists, then checks if the location is a valid
 * drop position using CTerrainManager_GetDropZ. If valid, updates
 * loc->z with the found z value. The entity pointer itself is unused -
 * purely a validity check.
 */
int
Script_dropCheck(CLocation *loc, uint32_t serial, int stepHeight)
{
	CItem *ent;
	int outZ;
	int valid;

	ent = FindEntityValidated(serial, "dropCheck");
	if (ent == NULL)
		return 0;

	outZ = 0;
	valid = CTerrainManager_GetDropZ(loc, &outZ, stepHeight);
	if (valid == 1)
		(loc)->z = (int16_t)outZ;
	return (valid == 1) ? 1 : 0;
}

/*
 * 0x0041CF54 - isOnMulti
 *
 * Checks if an entity is at a location on top of a multi owned by
 * the specified serial. Gets entity location, calls
 * GetItemsAtLocation, iterates items looking for multi with
 * matching owner serial.
 */
int
Script_isOnMulti(uint32_t entitySerial, uint32_t multiSerial)
{
	CItem *entity;
	CVector list;
	char type;
	uintptr_t *iter;

	entity = FindEntityValidated(entitySerial, "isOnMulti");
	if (entity == NULL)
		return 0;

	type = 0;
	CVector_Constructor(&list, &type);

	CBlockManager_GetItemsAtLocation(&g_SpatialGrid, &list, CEntity_GetLocation(&entity->resourceEntity.entity));

	iter = (uintptr_t *)list.begin;
	while (iter != (uintptr_t *)list.end) {
		CItem *item = (CItem *)*iter;
		if (CItem_HasMulti_Filter(item)) {
			CMultiComponent *mc = CItem_GetMulti(item);
			if (CMulti_GetSerial(mc) == multiSerial) {
				CVector_Destructor(&list);
				return 1;
			}
		}
		iter++;
	}

	CVector_Destructor(&list);
	return 0;
}

/*
 * 0x0041D045 - isOnAnyMulti
 *
 * Checks if an entity is at a location on top of any multi.
 * Returns the multi's owner serial if found, 0 otherwise.
 */
uint32_t
Script_isOnAnyMulti(uint32_t entitySerial)
{
	CItem *entity;
	CVector list;
	char type;
	uintptr_t *iter;

	entity = FindEntityValidated(entitySerial, "isOnAnyMulti");
	if (entity == NULL)
		return 0;

	type = 0;
	CVector_Constructor(&list, &type);

	CBlockManager_GetItemsAtLocation(&g_SpatialGrid, &list, CEntity_GetLocation(&entity->resourceEntity.entity));

	iter = (uintptr_t *)list.begin;
	while (iter != (uintptr_t *)list.end) {
		CItem *item = (CItem *)*iter;
		if (CItem_HasMulti_Filter(item)) {
			CMultiComponent *mc = CItem_GetMulti(item);
			uint32_t serial = CMulti_GetSerial(mc);
			CVector_Destructor(&list);
			return serial;
		}
		iter++;
	}

	CVector_Destructor(&list);
	return 0;
}

/*
 * 0x0041D12D - isAnyMultiAt
 *
 * Checks if there is any multi at the given location (x,y match only).
 * Returns the multi's owner serial if found, 0 otherwise.
 */
uint32_t
Script_isAnyMultiAt(CLocation *loc)
{
	CVector list;
	char type;
	uintptr_t *iter;

	type = 0;
	CVector_Constructor(&list, &type);

	CBlockManager_GetItemsAtLocationXY(&g_SpatialGrid, &list, loc);

	iter = (uintptr_t *)list.begin;
	while (iter != (uintptr_t *)list.end) {
		CItem *item = (CItem *)*iter;
		if (CItem_HasMulti_Filter(item)) {
			CMultiComponent *mc = CItem_GetMulti(item);
			uint32_t serial = CMulti_GetSerial(mc);
			CVector_Destructor(&list);
			return serial;
		}
		iter++;
	}

	CVector_Destructor(&list);
	return 0;
}

/*
 * 0x0041D1EF - isAnyMultiBelow
 *
 * Checks if there is any multi at or below the given location
 * (x,y match, item z <= input z). Returns the multi's owner
 * serial if found, 0 otherwise.
 */
uint32_t
Script_isAnyMultiBelow(CLocation *loc)
{
	CVector list;
	char type;
	uintptr_t *iter;

	type = 0;
	CVector_Constructor(&list, &type);

	CBlockManager_GetItemsAtLocation(&g_SpatialGrid, &list, loc);

	iter = (uintptr_t *)list.begin;
	while (iter != (uintptr_t *)list.end) {
		CItem *item = (CItem *)*iter;
		if (CItem_HasMulti_Filter(item)) {
			CMultiComponent *mc = CItem_GetMulti(item);
			uint32_t serial = CMulti_GetSerial(mc);
			CVector_Destructor(&list);
			return serial;
		}
		iter++;
	}

	CVector_Destructor(&list);
	return 0;
}

/*
 * 0x0041D2B1 - openContainer
 *
 * Sends the player the container's gump and current contents.
 * Returns 1 on success, 0 when either entity is invalid.
 */
int
Script_openContainer(uint32_t playerSerial, uint32_t containerSerial)
{
	CItem *player;
	CItem *container;
	uint16_t gumpId;

	player = FindPlayerValidated(playerSerial, "openContainer");
	container = FindContainerValidated(containerSerial, "openContainer");
	if (player == NULL || container == NULL)
		return 0;

	gumpId = CItem_GetContainerGump(container);

	SendOpenGump((CPlayer *)player, CMobile_GetSerial((CMobile *)player), CMobile_GetSerial((CMobile *)container), gumpId);

	CContainer_SendContainerContents((CContainer *)container, player, CMobile_GetSerial((CMobile *)player), 0);

	return 1;
}

/*
 * 0x0041D336 - getNumInMultiType
 *
 * Returns the number of entries in a multi type definition.
 */
int
Script_getNumInMultiType(int typeId)
{
	int count;

	count = 0;
	CMultiManager_GetNumInType(&g_MultiManager, typeId, &count);
	return count;
}

/*
 * 0x0041D35A - multiCompSetSendSlave
 *
 * Sets or clears the sendSlave flag on the multi component the
 * entity belongs to.
 */
void
Script_multiCompSetSendSlave(uint32_t serial, int value)
{
	CItem *ent;
	CMultiComponent *mc;

	ent = FindEntityValidated(serial, "moveCompSetSendSlave");
	if (ent == NULL)
		return;

	if (!CItem_HasMulti_Filter(ent))
		return;

	mc = CItem_GetMulti(ent);
	CMultiComponent_SetSendSlave(mc, value != 0);
}

/*
 * 0x0041D3A5 - areMobilesInMultiArea
 *
 * Checks if there are mobiles in the specified multi's area.
 */
int
Script_areMobilesInMultiArea(int multiSerial, CLocation *loc)
{
	return CMultiManager_AreMobilesInArea(&g_MultiManager, multiSerial, loc);
}

/*
 * 0x0041D3BC - runAway
 *
 * Puts the NPC into flee state, targeting targetSerial.
 */
void
Script_runAway(uint32_t serial, uint32_t targetSerial)
{
	CItem *ent;
	CNPC *npc;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	npc = (CNPC *)ent;
	npc->actionTarget = targetSerial;
	npc->aiState = 7;
	npc->isWalking = 0;
}

/*
 * 0x0041D41B - getHungerLevel
 *
 * Returns the NPC's stomach as a percentage of hungerCapacity.
 * Returns 100 when the NPC has no hunger system, -1 when the
 * serial is not an NPC.
 */
int
Script_getHungerLevel(uint32_t serial)
{
	CItem *ent;
	CNPC *npc;
	int result;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return -1;
	if (!VT_IsNPC(ent))
		return -1;
	npc = (CNPC *)ent;
	if (npc->hungerCapacity == 0)
		result = 100;
	else
		result = (int)npc->mobile.stomach * 100 / (int)npc->hungerCapacity;
	return result;
}

/*
 * 0x0041D492 - eatObject
 *
 * Stub that always returns 0; the demo binary never implemented
 * NPC eating.
 */
int
Script_eatObject(uint32_t serial, uint32_t foodSerial)
{
	CItem *ent;
	CItem *food;
	CMobile *mob;

	ent = FindEntityValidated(serial, "eatObject");
	food = FindEntityValidated(foodSerial, "eatObject");
	if (ent == NULL)
		return 0;
	if (food == NULL)
		return 0;
	if (!VT_IsNPC(ent))
		return 0;
	mob = (CMobile *)ent;
	USED(mob);
	return 0;
}

/*
 * 0x0041D4EA - setBehavior
 *
 * Sets the given behavior flag bits on the NPC.
 */
void
Script_setBehavior(uint32_t serial, int flags)
{
	CItem *ent;
	CNPC *npc;

	ent = FindEntityValidated(serial, "setBehavior");
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	npc = (CNPC *)ent;
	npc->behaviorFlags |= flags;
}

/*
 * 0x0041D53B - clearBehavior
 *
 * Clears the given behavior flag bits on the NPC.
 */
void
Script_clearBehavior(uint32_t serial, int flags)
{
	CItem *ent;
	CNPC *npc;

	ent = FindEntityValidated(serial, "clearBehavior");
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	npc = (CNPC *)ent;
	npc->behaviorFlags &= ~flags;
}

/*
 * 0x0041D590 - loiter
 *
 * Tells the NPC to wander within range of its current location.
 */
void
Script_loiter(uint32_t serial, int range)
{
	CItem *ent;
	CNPC *npc;

	ent = FindEntityValidated(serial, "loiter");
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;

	npc = (CNPC *)ent;
	{
		CLocation loc;
		CLocation_SetLoc(&loc, &ent->resourceEntity.entity.location);
		CNPC_Loiter(npc, range, loc);
	}
}

/*
 * 0x0041D5EB - goLoiter
 *
 * Tells the NPC to wander within range of loc.
 */
void
Script_goLoiter(uint32_t serial, CLocation *loc, int range)
{
	CItem *ent;
	CNPC *npc;
	CLocation dest;

	ent = FindEntityValidated(serial, "loiter");
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;

	npc = (CNPC *)ent;
	CLocation_SetLoc(&dest, loc);
	CNPC_Loiter(npc, range, dest);
}

/*
 * 0x0041D641 - setDesireLevel
 *
 * Sets the NPC's desire level to desireLevel / 10.
 */
void
Script_setDesireLevel(uint32_t serial, int desireLevel)
{
	CItem *ent;
	CNPC *npc;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;
	npc = (CNPC *)ent;
	npc->npcInfo1_1 = desireLevel / 10;
}

/*
 * 0x0041D68E - setLoiterMode
 *
 * Toggles the NPC's permanent loiter behavior. Setting mode starts
 * loitering at its current location; clearing it returns the NPC
 * to idle.
 */
void
Script_setLoiterMode(uint32_t serial, int mode)
{
	CItem *ent;
	CNPC *npc;
	CLocation loc;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return;
	if (!VT_IsNPC(ent))
		return;

	npc = (CNPC *)ent;
	if (mode != 0) {
		npc->behaviorFlags |= 0x20000;
		CLocation_SetLoc(&loc, &npc->mobile.container.item.resourceEntity.entity.location);
		CNPC_Loiter(npc, 0x3E8, loc);
	} else {
		npc->behaviorFlags &= ~0x20000u;
		npc->aiState = NPC_STATE_IDLE;
	}
}

/*
 * VT_GetName - emulate vtable[0x4C] GetName dispatch.
 *
 * Returns the entity's name: mob->name for mobiles, or the
 * tiledata-derived item name (formatted into g_ItemNameBuf via the
 * binary's CItem::GetName).
 */
static const char *
VT_GetName(CItem *ent)
{
	if (VT_IsMobile(ent)) {
		CMobile *mob = (CMobile *)ent;
		return (mob->name != NULL) ? mob->name : "";
	}
	return CItem_GetNameString_VT(ent, 1);
}

/*
 * 0x0041D72C - getDesireLevel
 *
 * Returns the NPC's desire level scaled up by 10 (the inverse of
 * the value stored by setDesireLevel).
 */
int
Script_getDesireLevel(uint32_t serial)
{
	CItem *ent;
	CNPC *npc;

	ent = CWorld_FindBySerial(g_World, serial);
	if (ent == NULL)
		return 0;
	if (!VT_IsNPC(ent))
		return 0;
	npc = (CNPC *)ent;
	return (int)npc->npcInfo1_1 * 10;
}

/*
 * 0x0041D775 - setDecayTest
 *
 * Reinitialises the world decay system in the given mode.
 */
void
Script_setDecayTest(int mode)
{
	CWorld_InitDecay(mode);
}

/*
 * 0x0041D788 - setNPCState
 *
 * Sets the NPC's AI state to state (must be in [0, 13]). Returns
 * state on success, -1 on an invalid state or NPC.
 */
int
Script_setNPCState(uint32_t serial, int state)
{
	CItem *ent;

	if (state < 0 || state >= 0xe)
		return -1;
	ent = FindMobileEntityValidated(serial, "setNPCState");
	if (ent == NULL)
		return -1;
	CNPC_SetState((CNPC *)ent, (uint32_t)state);
	return state;
}

/*
 * 0x0041D7CC - isOwnedPet
 *
 * Returns 1 when the mobile has any owner registered in its
 * "myBoss" ObjVar list.
 */
int
Script_isOwnedPet(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileEntityValidated(serial, "isOwnedPet");
	if (ent == NULL)
		return 0;
	return CMobile_HasBoss((CMobile *)ent);
}

/*
 * 0x0041D7FA - getNPCState
 *
 * Returns the NPC's current AI state, or -1 when the serial is not
 * an NPC.
 */
int
Script_getNPCState(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileEntityValidated(serial, "setNPCState");
	if (ent == NULL)
		return -1;
	return ((CNPC *)ent)->aiState;
}

/*
 * 0x0041D82A - goSleep
 *
 * Tells the NPC to wander for steps ticks before transitioning to
 * AI state returnState (must be in [0, 13]). Returns 1 on success.
 */
int
Script_goSleep(uint32_t serial, int steps, int returnState)
{
	CItem *ent;

	if (returnState < 0 || returnState >= 0xe)
		return 0;
	ent = FindMobileEntityValidated(serial, "setNPCState");
	if (ent == NULL)
		return 0;
	CNPC_StartWander((CNPC *)ent, steps, returnState);
	return 1;
}

/*
 * 0x0041D875 - intRet
 *
 * Stores an int return value from a script back to the C side via
 * g_ScriptReturnValue/g_ScriptReturnFlag. Combat code saves and
 * restores these globals around event 0x22 so weapon scripts can
 * override the damage value.
 */
void
Script_intRet(int value)
{
	g_ScriptReturnValue = value;
	g_ScriptReturnFlag = 1;
}

/*
 * Script API Handlers (game-level functions from handler table)
 */

/*
 * 0x0041D88C - setDefaultReturn
 *
 * Stores value as the current thread's return value, used when the
 * trigger or function finishes without an explicit return.
 */
void
Script_setDefaultReturn(int value)
{
	CExecThread *thread = ThreadList_GetCurrent(&g_activeThreadList);
	if (thread != NULL)
		thread->returnVal = value;
}

/*
 * 0x0041D8B2 - addGlobalQuantity
 *
 * Adds quantity to a resource entity in "global" mode (no resbank
 * limit check). Detaches the entity, runs CItem_Setup with type=2,
 * then re-attaches it.
 */
void
Script_addGlobalQuantity(uint32_t serial, int quantity)
{
	CItem *entity;
	CLocation *loc;

	entity = FindEntityValidated(serial, "addGlobalQuantity");
	if (entity == NULL)
		return;

	((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);

	// vtable[0x80]: GetLocation (walks parent chain to root)
	loc = VT_GetLocation(entity);

	// CItem_Setup with type=2 (global quantity add)
	CItem_Setup(entity, 2, loc, 0, quantity);

	((void (*)(void *))VT_FN(entity, VT_RETURN_TO_TRACKED))(entity);
}

/*
 * 0x0041D90C - requestAddQuantity
 *
 * Adds quantity to a resource entity, capped by the resbank limit
 * check. Returns 1 on success, 0 when the limit would be exceeded.
 */
int
Script_requestAddQuantity(uint32_t serial, int quantity)
{
	CItem *entity;
	CLocation *loc;

	entity = FindEntityValidated(serial, "requestAddQuantity");
	if (entity == NULL)
		return 0;

	loc = VT_GetLocation(entity);
	if (!ResBankLimitCheck(entity, loc))
		return 0;

	((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);

	// vtable[0x80]: GetLocation (second call, for CItem_Setup)
	loc = VT_GetLocation(entity);

	// CItem_Setup with type=0 (request quantity add)
	CItem_Setup(entity, 0, loc, 0, quantity);

	((void (*)(void *))VT_FN(entity, VT_RETURN_TO_TRACKED))(entity);

	return 1;
}

/*
 * 0x0041D992 - transferAllResources [680]
 *
 * Moves every resource node from src into dst. No-op when src and
 * dst are the same entity or either side is invalid.
 */
void
Script_transferAllResources(uint32_t dstSerial, uint32_t srcSerial)
{
	CItem *dst, *src;

	if (dstSerial == srcSerial)
		return;
	dst = FindEntityValidated(dstSerial, "transferAllResources");
	src = FindEntityValidated(srcSerial, "transferAllResources");
	if (dst == NULL || src == NULL)
		return;
	((void (*)(void *))VT_FN(src, VT_HIDE))(src);
	((void (*)(void *))VT_FN(dst, VT_HIDE))(dst);
	// CResourceEntity::TransferAllResources (0x004857E1)
	CResourceEntity_TransferAllResources(dst, src);
	((void (*)(void *))VT_FN(src, VT_RETURN_TO_TRACKED))(src);
	((void (*)(void *))VT_FN(dst, VT_RETURN_TO_TRACKED))(dst);
}

/*
 * 0x0041DA18 - transferResources [681]
 *
 * Moves up to amount of resource resName from src to dst. After
 * the transfer, depleted source resource entities are cleaned up
 * via FinalizeConsume. No-op when src and dst are the same or any
 * argument is invalid.
 */
void
Script_transferResources(uint32_t dstSerial, uint32_t srcSerial, int amount, CString *resName)
{
	CItem *dst, *src;
	CResourceType *resDef;
	CVector list;
	char typeFlag;

	if (dstSerial == srcSerial)
		return;
	dst = FindEntityValidated(dstSerial, "transferResources");
	src = FindEntityValidated(srcSerial, "transferResources");
	resDef = CResourceTypeManager_FindByName(CString_GetCStr(resName));
	if (dst == NULL || src == NULL || resDef == NULL)
		return;

	typeFlag = 0;
	CVector_Constructor(&list, &typeFlag);

	if (!((int (*)(void *))VT_FN(src, VT_HAS_CONTAINER))(src))
		CItem_GetContainerItems(src, &list);

	((void (*)(void *))VT_FN(src, VT_HIDE))(src);
	((void (*)(void *))VT_FN(dst, VT_HIDE))(dst);

	// Transfer resources from src to dst filtered by type
	CResourceEntity_TransferResources(dst, src, (uint32_t)amount, resDef->typeId);

	if (((int (*)(void *))VT_FN(src, VT_HAS_RESOURCE_FLAG))(src)) {
		if (CItem_FinalizeConsume_VT(src, 0)) {
			((void (*)(void *))VT_FN(src, VT_RETURN_TO_TRACKED))(src);
		} else {
			CBlockManager_RestoreItems(&g_SpatialGrid, &list);
		}
	} else {
		((void (*)(void *))VT_FN(src, VT_RETURN_TO_TRACKED))(src);
	}

	((void (*)(void *))VT_FN(dst, VT_RETURN_TO_TRACKED))(dst);
	CVector_Destructor(&list);
}

/*
 * 0x0041DB70 - transferGeneric [682]
 *
 * Moves stackable items of body type bodyType from src into dst.
 * No-op when src and dst match or bodyType is zero. On a failed
 * transfer, the original dst contents are restored.
 */
void
Script_transferGeneric(uint32_t srcSerial, uint32_t dstSerial, int bodyType)
{
	CItem *src, *dst;
	CVector list;
	char typeFlag;
	int result;

	if (srcSerial == dstSerial || bodyType == 0)
		return;
	src = FindEntityValidated(srcSerial, "transferGeneric");
	dst = FindEntityValidated(dstSerial, "transferGeneric");
	if (src == NULL || dst == NULL)
		return;

	typeFlag = 0;
	CVector_Constructor(&list, &typeFlag);

	if (!((int (*)(void *))VT_FN(dst, VT_HAS_CONTAINER))(dst))
		CItem_GetContainerItems(dst, &list);

	((void (*)(void *))VT_FN(dst, VT_HIDE))(dst);
	((void (*)(void *))VT_FN(src, VT_HIDE))(src);

	// CItem_ConsumeAmount (0x0045EA86): transfer items of bodyType
	result = CItem_ConsumeAmount(dst, src, bodyType);

	if (result) {
		((void (*)(void *))VT_FN(dst, VT_RETURN_TO_TRACKED))(dst);
	} else {
		CBlockManager_RestoreItems(&g_SpatialGrid, &list);
	}

	((void (*)(void *))VT_FN(src, VT_RETURN_TO_TRACKED))(src);
	CVector_Destructor(&list);
}

/*
 * 0x0041DC7B - returnAllResourcesToBank [683]
 *
 * Strips every resource node off the entity and returns the entity
 * to its tracked location.
 */
void
Script_returnAllResourcesToBank(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "returnAllResourcesToBank");
	if (ent == NULL)
		return;
	((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);
	CResourceEntity_RemoveAllNodes(ent, 1);
	((void (*)(void *))VT_FN(ent, VT_RETURN_TO_TRACKED))(ent);
}

/*
 * 0x0041DCC0 - returnResourcesToBank [684]
 *
 * Drains amount of resource resName off the entity, suppressing
 * spatial notifications via lockdown / isLoading.
 *
 * MODIFIED (FEAT_CLOSED_ECONOMY): credits the matching
 * CResBankRegion.quantities[] slot with the amount actually
 * drained, closing the harvest half of Raph Koster's closed-loop
 * economy ("when the bank was overdrawn of MEAT, nothing that
 * used MEAT could spawn until some of the MEAT in the world was
 * destroyed and therefore returned to the bank", Raph Koster
 * 2006). The binary's drain path runs unchanged; the bank credit
 * lands after it using a pre-drain value3 snapshot of the first
 * matching node so the credited amount matches what
 * CResourceEntity_TransferResources actually moved (no
 * double-credit if the node had less than requested). Without
 * the flag the binary's temp-item drain runs as-is and no bank
 * credit fires.
 */
void
Script_returnResourcesToBank(uint32_t serial, int amount, CString *resName)
{
	CItem *mob;
	CResourceType *resDef;
	CItem *tempItem;
	void *rawMem;
	uint32_t savedLoading;
	CResBankRegion *region;
	CResourceNode *node;
	int creditAmount;

	mob = FindEntityValidated(serial, "returnResourcesToBank");
	resDef = CResourceTypeManager_FindByName(CString_GetBuffer(resName));
	if (mob == NULL || resDef == NULL)
		return;

	// CUSTOM (FEAT_CLOSED_ECONOMY): pre-compute how much the binary's
	// drain will actually move from the FIRST matching node. The
	// CResourceEntity_TransferResources path walks the node chain
	// and may drain across multiple matching nodes; we credit based
	// on the first one only, so an entity carrying multiple type-3
	// nodes of the same resource type would under-credit. In practice
	// the demo's resource entities carry at most one node per (id,
	// type) so this is exact for shipped data; a future template
	// that emits duplicate nodes would need this widened to a
	// sum-across-matching-nodes walk. Negative amounts and missing
	// nodes credit nothing.
	creditAmount = 0;
	if (feat(FEAT_CLOSED_ECONOMY) && amount > 0) {
		node = CResourceEntity_FindNode(mob, (uint16_t)resDef->typeId, 3);
		if (node != NULL)
			creditAmount = (amount < node->value3) ? amount : node->value3;
	}

	CItem_SetLockdown(mob, 1);

	CItem_UpdateContainInfo(mob, 1);

	((void (*)(void *))VT_FN(mob, VT_HIDE))(mob);

	// Save and set isLoading to suppress notifications
	savedLoading = g_World->isLoading;
	g_World->isLoading = 1;

	rawMem = OperatorNew(sizeof(CItem));
	if (rawMem != NULL)
		tempItem = CItem_Constructor(rawMem);
	else
		tempItem = NULL;

	// Transfer resources of type from mob to temp item
	CResourceEntity_TransferResources(tempItem, mob, (uint32_t)amount, resDef->typeId);

	if (tempItem != NULL)
		((void (*)(void *))VT_FN(tempItem, VT_DELETE))(tempItem);

	// Restore isLoading
	g_World->isLoading = savedLoading;

	((void (*)(void *))VT_FN(mob, VT_RETURN_TO_TRACKED))(mob);

	// Clear lockdown flag
	CItem_SetLockdown(mob, 0);

	// CUSTOM (FEAT_CLOSED_ECONOMY): credit the bank with the drained
	// amount. Runs after the binary path so the temp-item drain's
	// notify chain has already settled.
	if (creditAmount > 0) {
		region = CResBankManager_GetRegionByLocation(mob->resourceEntity.entity.location.x, mob->resourceEntity.entity.location.y);
		if (region != NULL && region != g_ResBankManager.noRegion)
			CResBankRegion_AddToQuantity(region, (int)resDef->typeId, creditAmount);
	}
}

/*
 * 0x0041DDE4 - createGlobalObjectAt
 *
 * Creates an item of body type artId at loc in global resource
 * mode, applies the tiledata default hue, and drops it into the
 * world. Returns the new serial, or 0 on validation failure.
 */
uint32_t
Script_createGlobalObjectAt(int artId, CLocation *loc)
{
	CItem *item;
	uint16_t bodyType;

	if (artId > 0x4000)
		return 0;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;

	item = CWorld_CreateItem(g_World, (uint16_t)artId);
	if (item == NULL)
		return 0;

	// CLocation_SetLoc(item + 0x10, loc) - store creation location
	CLocation_SetLoc((CLocation *)&item->resourceEntity.nextInContainer, loc);

	// CItem_Setup with type=2 (global resource mode)
	CItem_Setup(item, 2, loc, 0, 1);

	// Set hue from tiledata[bodyType].value2
	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	item->resourceEntity.entity.color = g_ItemTileData[bodyType].value2;

	((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);

	// ValidateInWorld
	if (!ValidateInWorld(item)) {
		item = NULL;
		return 0;
	}
	return item->serial;
}

/*
 * 0x0041DEAF - createGlobalObjectIn
 *
 * Creates an item of body type artId in container in global
 * resource mode, applies the tiledata default hue, and inserts it
 * into the container. Returns the new serial, or 0 on validation
 * failure.
 */
uint32_t
Script_createGlobalObjectIn(int artId, uint32_t containerSerial)
{
	CItem *item;
	CItem *container;
	CLocation tmpLoc;
	uint16_t bodyType;
	uint32_t savedSerial;

	if (artId > 0x4000)
		return 0;

	container = CWorld_FindBySerial(g_World, containerSerial);
	if (container == NULL)
		return 0;

	if (!VT_IsMobile2(container))
		return 0;

	item = CWorld_CreateItem(g_World, (uint16_t)artId);
	if (item == NULL)
		return 0;

	// CLocation_Init + CLocation_Set(-1, -1, 0)
	CLocation_Init(&tmpLoc);
	CLocation_Set(&tmpLoc, -1, -1, 0);

	// CItem_Setup with type=2, using container's location
	CItem_Setup(item, 2, VT_GetLocation(container), 0, 1);

	// Set hue from tiledata[bodyType].value2
	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	item->resourceEntity.entity.color = g_ItemTileData[bodyType].value2;

	// Save serial before AddToContainer
	savedSerial = item->serial;

	((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, container, &tmpLoc);

	// Verify item still exists at same address
	if (CWorld_FindBySerial(g_World, savedSerial) == item) {
		if (!ValidateInWorld(item)) {
			savedSerial = 0;
			item = NULL;
		}
	}

	return savedSerial;
}

/*
 * 0x0041DFC7 - createNoResObjectAt
 *
 * Like Script_createGlobalObjectAt, but skips the CItem_Setup
 * resource initialisation step.
 */
uint32_t
Script_createNoResObjectAt(int artId, CLocation *loc)
{
	CItem *item;
	uint16_t bodyType;

	if (artId > 0x4000)
		return 0;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;

	item = CWorld_CreateItem(g_World, (uint16_t)artId);
	if (item == NULL)
		return 0;

	// CLocation_SetLoc(item + 0x10, loc) - store creation location
	CLocation_SetLoc((CLocation *)&item->resourceEntity.nextInContainer, loc);

	// NO CItem_Setup - that's the "NoRes" difference

	// Set hue from tiledata[bodyType].value2
	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	item->resourceEntity.entity.color = g_ItemTileData[bodyType].value2;

	((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);

	// ValidateInWorld
	if (!ValidateInWorld(item)) {
		item = NULL;
		return 0;
	}
	return item->serial;
}

/*
 * 0x0041E07D - createNoResObjectIn
 *
 * Like Script_createGlobalObjectIn, but skips the CItem_Setup
 * resource initialisation step.
 */
uint32_t
Script_createNoResObjectIn(int artId, uint32_t containerSerial)
{
	CItem *item;
	CItem *container;
	CLocation tmpLoc;
	uint16_t bodyType;
	uint32_t savedSerial;

	if (artId > 0x4000)
		return 0;

	container = CWorld_FindBySerial(g_World, containerSerial);
	if (container == NULL)
		return 0;

	if (!VT_IsMobile2(container))
		return 0;

	item = CWorld_CreateItem(g_World, (uint16_t)artId);
	if (item == NULL)
		return 0;

	// CLocation_Init + CLocation_Set(-1, -1, 0)
	CLocation_Init(&tmpLoc);
	CLocation_Set(&tmpLoc, -1, -1, 0);

	// NO CItem_Setup - that's the "NoRes" difference

	// Set hue from tiledata[bodyType].value2
	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	item->resourceEntity.entity.color = g_ItemTileData[bodyType].value2;

	// Save serial before AddToContainer
	savedSerial = item->serial;

	((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, container, &tmpLoc);

	// Verify item still exists at same address
	if (CWorld_FindBySerial(g_World, savedSerial) == item) {
		if (!ValidateInWorld(item)) {
			savedSerial = 0;
			item = NULL;
		}
	}

	return savedSerial;
}

/*
 * 0x0041E178 - requestCreateObjectAt
 *
 * Like Script_createGlobalObjectAt, but routes through the
 * resource bank's per-region cap (CItem_Setup type=0). Returns 0
 * when the region's resource budget is full.
 */
uint32_t
Script_requestCreateObjectAt(int artId, CLocation *loc)
{
	CItem *item;
	ResEntitySlot *resSlot;
	uint16_t bodyType;

	if (artId > 0x4000)
		return 0;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return 0;

	// Resource bank limit check
	resSlot = &g_ResEntitySlots[artId];
	if (!ResBankLimitCheck(resSlot, loc))
		return 0;

	item = CWorld_CreateItem(g_World, (uint16_t)artId);
	if (item == NULL)
		return 0;

	// CLocation_SetLoc(item + 0x10, loc) - store creation location
	CLocation_SetLoc((CLocation *)&item->resourceEntity.nextInContainer, loc);

	// CItem_Setup with type=0 (request mode, not global)
	CItem_Setup(item, 0, loc, 0, 1);

	// Set hue from tiledata[bodyType].value2
	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	item->resourceEntity.entity.color = g_ItemTileData[bodyType].value2;

	((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);

	// ValidateInWorld
	if (!ValidateInWorld(item)) {
		item = NULL;
		return 0;
	}
	return item->serial;
}

/*
 * 0x0041E271 - requestCreateObjectIn
 *
 * Like Script_createGlobalObjectIn, but routes through the
 * resource bank's per-region cap. Returns 0 when the region's
 * resource budget is full.
 */
uint32_t
Script_requestCreateObjectIn(int artId, uint32_t containerSerial)
{
	CItem *item;
	CItem *container;
	CLocation tmpLoc;
	ResEntitySlot *resSlot;
	uint16_t bodyType;
	uint32_t savedSerial;

	if (artId > 0x4000)
		return 0;

	container = CWorld_FindBySerial(g_World, containerSerial);
	if (container == NULL)
		return 0;

	if (!VT_IsMobile2(container))
		return 0;

	// Resource bank limit check using container's location
	resSlot = &g_ResEntitySlots[artId];
	if (!ResBankLimitCheck(resSlot, VT_GetLocation(container)))
		return 0;

	item = CWorld_CreateItem(g_World, (uint16_t)artId);
	if (item == NULL)
		return 0;

	// CLocation_Init + CLocation_Set(-1, -1, 0)
	CLocation_Init(&tmpLoc);
	CLocation_Set(&tmpLoc, -1, -1, 0);

	// CItem_Setup with type=0, using container's location
	CItem_Setup(item, 0, VT_GetLocation(container), 0, 1);

	// Set hue from tiledata[bodyType].value2
	bodyType = CEntity_GetBodyType(item) & 0xFFFF;
	item->resourceEntity.entity.color = g_ItemTileData[bodyType].value2;

	// Save serial via GetSerial (binary uses CMobile_GetSerial, same effect)
	savedSerial = CMobile_GetSerial((CMobile *)item);

	((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, container, &tmpLoc);

	// Verify item still exists at same address
	if (CWorld_FindBySerial(g_World, savedSerial) == item) {
		if (!ValidateInWorld(item)) {
			savedSerial = 0;
			item = NULL;
		}
	}

	return savedSerial;
}

/*
 * 0x0041E3C2 - createGlobalNPCAtSpecificLoc
 *
 * Spawns the NPC template at loc (no spiral search). Returns the
 * new mobile's serial, or 0 when loc or the template is invalid.
 */
uint32_t
Script_createGlobalNPCAtSpecificLoc(int templateId, CLocation *loc)
{
	CItem *result;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y))
		return 0;
	result = CTemplateManager_CreateFromTemplate((uint16_t)templateId, loc, 0, 2, NULL);
	if (result == NULL)
		return 0;
	// CUSTOM (FEAT_CLOSED_ECONOMY): script-driven NPC spawns deduct
	// from the regional bank like the director / GM .spawn paths. The
	// helper early-returns when the flag is off, so binary behavior
	// is preserved.
	DeductSpawnFromBank((uint16_t)templateId, loc);
	return result->serial;
}

/*
 * 0x0041E416 - createGlobalNPCAt [685]
 *
 * Spawns the NPC template near loc using the resource-bank spiral
 * search. Returns the new mobile's serial, or 0 when loc or the
 * template is invalid.
 */
uint32_t
Script_createGlobalNPCAt(uint32_t templateId, CLocation *loc, int flags)
{
	CLocation resultLoc;
	CItem *result;

	CLocation_SetLoc(&resultLoc, loc);
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, loc->x, loc->y))
		return 0;
	if (!CResBankManager_RequestCreateNPC(&resultLoc, flags, templateId))
		return 0;
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, resultLoc.x, resultLoc.y))
		return 0;
	result = CTemplateManager_CreateFromTemplate((uint16_t)templateId, &resultLoc, 0, 2, NULL);
	if (result == NULL)
		return 0;
	// CUSTOM (FEAT_CLOSED_ECONOMY): script-driven NPC spawns deduct
	// from the regional bank like the director / GM .spawn paths.
	DeductSpawnFromBank((uint16_t)templateId, &resultLoc);
	return result->serial;
}

/*
 * 0x0041E4B0 - requestCreateNPCAt
 *
 * Spawns templateId at loc using the spawner's "find a good spot"
 * helper. The noWander flag suppresses initial wandering.
 */
uint32_t
Script_requestCreateNPCAt(int templateId, CLocation *loc, int noWander)
{
	return SpawnAtPointForLocation((uint16_t)templateId, loc->x, (int16_t)loc->y, (int16_t)loc->z, noWander);
}

/*
 * 0x0041E4DC - createGlobalObjectOn
 *
 * Creates an item next to the entity: in the entity's parent
 * container when the entity is contained but not equipped,
 * otherwise at the entity's world location.
 */
uint32_t
Script_createGlobalObjectOn(uint32_t entitySerial, int artId)
{
	CItem *entity;

	entity = FindEntityValidated(entitySerial, "createGlobalObjectOn");
	if (entity == NULL)
		return 0;

	// vtable[0x104]: HasContainer (parent != NULL)
	if (check_IsInContainer(entity)) {
		if (!check_IsEquipped(entity)) {
			// In container but not equipped - create in parent
			return Script_createGlobalObjectIn(artId, entity->parent->serial);
		}
	}

	// Not in container or equipped - create at entity's location
	return Script_createGlobalObjectAt(artId, VT_GetLocation(entity));
}

/*
 * 0x0041E559 - findClosestBBoard
 *
 * Returns the serial of the bulletin board closest to loc, or 0
 * when none exists.
 */
uint32_t
Script_findClosestBBoard(CLocation *loc)
{
	CItem *board;

	board = FindClosestBBoardItem(loc);
	if (board != NULL)
		return board->serial;
	return 0;
}

/*
 * 0x0041E58D - setPostTime
 *
 * Stamps the entity with the current in-game time as the postTime
 * ObjVar (in minutes since day 0) and the current game tick as
 * msgTime.
 */
void
Script_setPostTime(uint32_t serial)
{
	CItem *ent;
	int totalMinutes;

	ent = FindEntityValidated(serial, "setPostTime");
	if (ent == NULL)
		return;

	totalMinutes = g_GameDay * 24 * 60 + g_GameHour * 60 + g_GameMinute;
	{
		CString name;
		CString_Constructor(&name, "postTime");
		ObjVar_SetStr(ent, &name, 0, (uint32_t)totalMinutes);
	}
	CEntity_SetObjVar(ent, "msgTime", 0, (uint32_t)(int)g_GameTick);
}

/*
 * 0x0041E610 - getChunkEgg
 *
 * Returns the serial of the spawn-egg entity tied to loc's map
 * block, or 0 when none exists.
 */
uint32_t
Script_getChunkEgg(CLocation *loc)
{
	int idx;
	MapBlock *block;
	CItem *egg;

	idx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, loc, 0);
	if (idx < 0)
		return 0;
	block = &g_MapBlocks[idx];
	if (block == NULL)
		return 0;
	egg = block->eggHead;
	if (egg == NULL)
		return 0;
	return egg->serial;
}

/*
 * 0x0041E671 - defineResource
 *
 * Adds a resource node of the named type to the entity with the
 * given category type (0, 1, or 2) and the supplied value1 / value2
 * counters. Returns 0 on validation failure (always returns 0 even
 * on success, matching the binary).
 */
int
Script_defineResource(uint32_t serial, CString *resourceName, int type, int value1, int value2)
{
	CItem *entity;
	const char *str;
	CResourceType *resType;
	CLocation tmpLoc;

	entity = FindEntityValidated(serial, "defineResource");

	str = CString_GetCStr(resourceName);
	resType = CResourceTypeManager_FindByName(str);

	if (type != 0 && type != 1 && type != 2)
		return 0;
	if (entity == NULL)
		return 0;
	if (resType == NULL)
		return 0;

	// saves location, never used
	CLocation_Init(&tmpLoc);
	CLocation_SetLoc(&tmpLoc, VT_GetLocation(entity));

	((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);

	// value3 = value1 (not value2)
	CResourceEntity_AddNode(entity, (uint16_t)resType->typeId, (int8_t)type, value1, value2, value1, 1);

	((void (*)(void *))VT_FN(entity, VT_RETURN_TO_TRACKED))(entity);

	// vtable[0xE4]: IsNPC (result stored in local but never used)
	(void)VT_IsNPC(entity);

	return 0;
}

/*
 * 0x0041E736 - addConsumer
 *
 * Schedules a respawn of templateIndex (with amount and value
 * parameters) at loc on the resource bank manager.
 */
void
Script_addConsumer(CLocation *loc, int templateIndex, int amount, int templateValue)
{
	CResBankManager_ScheduleRespawnForTemplate(loc, templateIndex, (int16_t)amount, (int16_t)templateValue);
}

/*
 * 0x0041E755 - whoIsLargestConsumer
 *
 * Returns the resource bank's respawn timer for templateIndex at
 * loc.
 */
int
Script_whoIsLargestConsumer(CLocation *loc, int templateIndex)
{
	return CResBankManager_GetRespawnTimer(loc, templateIndex);
}

/*
 * 0x0041E76C - resourceTypeToId
 *
 * Returns the typeId for the named resource, or 0 when unknown.
 */
int
Script_resourceTypeToId(CString *name)
{
	const char *str;
	CResourceType *resType;

	str = CString_GetCStr(name);
	resType = CResourceTypeManager_FindByName(str);
	if (resType != NULL)
		return resType->typeId;
	return 0;
}

/*
 * 0x0041E79A - textEntry
 *
 * Sends a TEXTENTRY packet to the player to fill the named gump
 * field with text.
 */
void
Script_textEntry(uint32_t serial, uint32_t playerSerial, int gumpId, int parentId, CString *text)
{
	uint8_t buf[0x10014];
	CItem *player;

	player = FindPlayerValidated(playerSerial, "textEntry");
	if (player == NULL)
		return;

	PacketManager_MakePacket_TEXT_ENTRY(buf, serial, gumpId, parentId, CString_GetBuffer(text));
	SendToClient(player, buf, -1);
}

/*
 * 0x0041E800 - stringQuery
 *
 * Sends a STRINGQUERY packet to the player asking question with
 * the supplied cancel/style/length parameters and title.
 */
void
Script_stringQuery(uint32_t playerSerial, uint32_t serial, int type, CString *question, int cancel, int style, int maxLen, CString *title)
{
	uint8_t buf[0x218];
	CItem *player;

	player = FindPlayerValidated(playerSerial, "stringQuery");
	if (player == NULL)
		return;

	PacketManager_MakePacket_STRING_QUERY(buf, serial, (uint16_t)type, CString_GetBuffer(question), (uint8_t)cancel, (uint8_t)style, maxLen, CString_GetBuffer(title));
	SendToClient(player, buf, -1);
}

/*
 * 0x0041E874 - webBrowse
 *
 * Sends a WEB_BROWSE packet asking the player's client to open
 * the given URL.
 */
void
Script_webBrowse(uint32_t serial, CString *str)
{
	CItem *ent;
	uint8_t obuf[0x10008];

	ent = FindPlayerValidated(serial, "webBrowse");
	if (ent == NULL)
		return;
	PacketManager_MakePacket_WEB_BROWSE(obuf, CString_GetBuffer(str));
	SendToClient(ent, obuf, -1);
}

/*
 * 0x0041E8CE - Script handler for makeBeelineFailPathfind [716]
 *
 * Sets or clears the mobile's frozen status flag, which forces
 * pathfinding into a failed state and falls back to a beeline.
 */
void
Script_makeBeelineFailPathfind(uint32_t serial, int flag)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "makeBeelineFailPathfind");
	if (ent == NULL)
		return;
	CMobile_SetFrozenFlag((CMobile *)ent, flag);
}

/*
 * 0x0041E8FE - callGuards [556]
 *
 * Summons guards toward the first mobile's location, attributed
 * to it, with the second mobile as the named target.
 */
void
Script_callGuards2(uint32_t serial1, uint32_t serial2, int range)
{
	CItem *mob;
	CItem *target;

	mob = FindMobileValidated(serial1, "callGuards");
	target = FindMobileValidated(serial2, "callGuards");
	if (mob == NULL || target == NULL)
		return;
	CombatManager_CallGuards(mob, &mob->resourceEntity.entity.location, target, range);
}

/*
 * 0x0041E95B - criminalAct
 *
 * Validates the criminal and victim serials. With no victim, drops the
 * criminal's notoriety and returns. Otherwise forwards to the no-op
 * CriminalAct_Notify stub.
 */
void
Script_criminalAct(uint32_t criminalSerial, uint32_t victimSerial, int fameAmount, int crimeWeight)
{
	CMobile *criminal, *victim;

	criminal = (CMobile *)FindMobileValidated(criminalSerial, "criminalAct");
	victim = (CMobile *)FindMobileValidated(victimSerial, "criminalAct");
	if (criminal == NULL)
		return;
	if (victim == NULL) {
		CMobile_LoseNotoriety(criminal, fameAmount);
		return;
	}
	CriminalAct_Notify(criminal, victim, fameAmount, crimeWeight, 0x7F, 0);
}

/*
 * 0x0041E9C5 - criminalActAdvanced
 *
 * Same as criminalAct but passes additional args to the empty stub.
 * If victim is NULL, just CMobile_LoseNotoriety on criminal.
 */
void
Script_criminalActAdvanced(uint32_t criminalSerial, uint32_t victimSerial, int fameAmount, int crimeWeight, int bound, int flags)
{
	CMobile *criminal, *victim;

	criminal = (CMobile *)FindMobileValidated(criminalSerial, "criminalAct");
	victim = (CMobile *)FindMobileValidated(victimSerial, "criminalAct");
	if (criminal == NULL)
		return;
	if (victim == NULL) {
		CMobile_LoseNotoriety(criminal, fameAmount);
		return;
	}
	CriminalAct_Notify(criminal, victim, fameAmount, crimeWeight, bound, flags);
}

/*
 * 0x0041EA33 - NotorietyCompare
 *
 * Compares notoriety levels of two mobiles via Combat_NotorietyCompare.
 * Returns 0 if either mobile is invalid.
 */
int
Script_NotorietyCompare(uint32_t serial1, uint32_t serial2)
{
	CItem *mob1, *mob2;

	mob1 = FindMobileValidated(serial1, "NotorietyCompare");
	mob2 = FindMobileValidated(serial2, "NotorietyCompare");
	if (mob1 == NULL)
		return 0;
	if (mob2 == NULL)
		return 0;
	return Combat_NotorietyCompare((CMobile *)mob1, (CMobile *)mob2);
}

/*
 * 0x0041EA89 - witnessCrime
 *
 * FindMobileValidated for criminal, validate crimeType [0,5),
 * FindMobileValidated for victim (can be NULL with name=NULL),
 * then calls ProcessCrimeWitness.
 */
int
Script_witnessCrime(CLocation *loc, uint32_t criminalSerial, uint32_t victimSerial, CString *str, int delay, int priority, int crimeType)
{
	CMobile *criminal, *victim;
	CLocation crimeLoc;

	criminal = (CMobile *)FindMobileValidated(criminalSerial, "witnessCrime (criminal)");
	if (criminal == NULL)
		return 0;
	if (crimeType < 0 || crimeType >= 5)
		return 0;
	victim = (CMobile *)FindMobileValidated(victimSerial, NULL);
	CLocation_SetLoc(&crimeLoc, loc);
	return ProcessCrimeWitness(&crimeLoc, criminal, victim, CString_GetData(str), delay, priority, crimeType);
}

static int
check_GetMurderCount(CItem *ent)
{
	int val = 0;
	CResourceEntity_GetTagInt(ent, "murderCount", &val);
	return val;
}

/*
 * 0x0041EB07 - overloadWeight
 *
 * Sets the entity's overloadedWeight ObjVar (overriding the
 * tiledata weight) and refreshes the entity in place.
 */
void
Script_overloadWeight(uint32_t serial, int weight)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "overloadWeight");
	if (ent == NULL)
		return;

	((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);

	// CItem::SetWeight(weight) at 0x00490C37
	CItem_SetWeight(ent, weight);

	((void (*)(void *))VT_FN(ent, VT_RETURN_TO_TRACKED))(ent);
}

/*
 * 0x0041EB4E - recalcWeight
 *
 * Recomputes the entity's stored weight. Containers walk their
 * children to accumulate weight; plain items pull the base weight
 * from tiledata.
 *
 * MODIFIED: the C version skips the spatial-grid remove / re-insert
 * dance the binary uses to drive recalculation, computing the same
 * value directly.
 */
void
Script_recalcWeight(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "recalcWeight");
	if (ent == NULL)
		return;

	((void (*)(void *))VT_FN(ent, VT_WEIGHT_RELATED))(ent);
}

/*
 * 0x0041EB7E - canBeGeneric
 *
 * Returns 1 when the entity's tiledata flags include TF_STACKABLE.
 */
int
Script_canBeGeneric(uint32_t serial)
{
	CItem *ent;
	uint16_t bodyType;

	ent = FindEntityValidated(serial, "isGeneric");
	if (ent == NULL)
		return 0;
	bodyType = CEntity_GetBodyType(ent);
	if (g_ItemTileData[bodyType].flags & 0x800)
		return 1;
	return 0;
}

/*
 * 0x0041EBCC - isGeneric
 *
 * Returns 1 when the entity is currently a generic stackable
 * resource item (HasResourceFlag).
 */
int
Script_isGeneric(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "isGeneric");
	if (ent == NULL)
		return 0;
	if (CItem_HasResourceFlag(ent))
		return 1;
	return 0;
}

/*
 * 0x0041EC06 - getTimeSecs
 *
 * Returns the current millisecond tick counter.
 */
int
Script_getTimeSecs(void)
{
	return (int)GetTickCount_UO();
}

/*
 * 0x0041EC11 - Script handler for getCurrentTimeStr [743]
 *
 * Stores the current real-world timestamp ("YYYY-MM-DD HH:MM") in
 * out.
 */
void
Script_getCurrentTimeStr(CString *out)
{
	CTimeManager_FormatTimestamp(out);
}

/*
 * 0x0041EC24 - destroyOne
 *
 * Removes a single unit of the entity. Stackable resources have
 * one quantum consumed via FinalizeConsume; non-stackable items
 * are deleted outright. Items resting on the entity are refreshed.
 */
void
Script_destroyOne(uint32_t serial)
{
	CItem *ent;
	CVector contItems;
	char typeFlag = 0;

	ent = FindEntityValidated(serial, "destroyOne");
	if (ent == NULL)
		return;

	CVector_Constructor(&contItems, &typeFlag);

	if (!((int (*)(void *))VT_FN(ent, VT_HAS_CONTAINER))(ent))
		CItem_GetContainerItems(ent, &contItems);

	if (((int (*)(void *))VT_FN(ent, VT_HAS_RESOURCE_FLAG))(ent)) {
		// Stackable: vtable[0x0C] hide, consume 1, vtable[0xAC] return
		((void (*)(void *))VT_FN(ent, VT_HIDE))(ent);

		if (CItem_FinalizeConsume(ent, 1))
			((void (*)(void *))VT_FN(ent, VT_RETURN_TO_TRACKED))(ent);
		else
			CBlockManager_RestoreItems(&g_SpatialGrid, &contItems);
	} else {
		// Non-stackable: vtable[0x90] Delete
		if (ent != NULL)
			((void (*)(void *))VT_FN(ent, VT_DELETE))(ent);
	}

	CBlockManager_RestoreItems(&g_SpatialGrid, &contItems);
	CVector_Destructor(&contItems);
}

/*
 * 0x0041ED15 - removeLeadingWords
 *
 * Strips count leading whitespace-delimited words from str.
 */
void
Script_removeLeadingWords(CString *str, int count)
{
	char *ptr;
	int i;

	ptr = CString_GetData(str);
	i = 0;
	while (i < count) {
		while (*ptr != '\0' && !isspace((unsigned char)*ptr))
			ptr++;
		if (*ptr != '\0')
			ptr++;
		i++;
	}
	CString_AssignCStr(str, ptr);
}

/*
 * 0x0041ED8D - objectsNearby
 *
 * Returns 1 when at least one entity of every body type in list
 * exists within range of loc (searching both dynamic and static
 * items). When a matched entity's body type equals filterType,
 * loc is updated to that entity's position.
 */
int
Script_objectsNearby(CList *list, CLocation *loc, int range, int filterType)
{
	CLocation localLoc;
	int blockIds[16];
	int i, j;
	CItem *ent;
	CListNode *cur;

	CLocation_SetLoc(&localLoc, loc);

	cur = list->head;
	for (i = 0; i < list->count; i++) {
		int bodyType = (int)cur->value;

		CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &localLoc, range, blockIds, 16);

		for (j = 0; blockIds[j] != -1; j++) {
			// Walk dynamic item chain (block+0x104, spatialNext at +0x20)
			ent = g_MapBlocks[blockIds[j]].itemHead;
			while (ent != NULL) {
				if ((CEntity_GetBodyType(ent) & 0xFFFF) == bodyType) {
					if ((CEntity_GetBodyType(ent) & 0xFFFF) == filterType)
						CLocation_SetLoc(loc, &ent->resourceEntity.entity.location);
					goto next_node;
				}
				ent = ent->spatialNext;
			}

			// Walk static item chain (block+0x100, nextInContainer at +0x10)
			ent = g_MapBlocks[blockIds[j]].staticHead;
			while (ent != NULL) {
				if ((CEntity_GetBodyType(ent) & 0xFFFF) == bodyType) {
					if ((CEntity_GetBodyType(ent) & 0xFFFF) == filterType)
						CLocation_SetLoc(loc, &ent->resourceEntity.entity.location);
					goto next_node;
				}
				ent = ent->resourceEntity.nextInContainer;
			}
		}

		return 0;

next_node:
		cur = cur->next;
	}

	return 1;
}

/*
 * 0x0041EEF7 - isHair
 *
 * Returns 1 when the entity is a wearable item in the hair (0xB)
 * or facial-hair (0x10) layer. Mobiles always return 0.
 */
int
Script_isHair(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "isHair");
	if (ent == NULL)
		return 0;
	return VT_IsHair(ent);
}

/*
 * 0x0041EF28 - isEditing
 *
 * Returns 1 when the player is in editing (god) mode, 0 when the serial
 * does not resolve.
 */
int
Script_isEditing(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "isEditing");
	if (ent == NULL)
		return 0;
	return CPlayer_IsEditing((CPlayer *)ent);
}

/*
 * 0x0041EF56 - isCounselor
 *
 * Returns 1 when the player has the counselor flag, 0 when the serial does
 * not resolve.
 */
int
Script_isCounselor(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "isCounselor");
	if (ent == NULL)
		return 0;
	return CPlayer_IsCounselor((CPlayer *)ent);
}

/*
 * 0x0041EF84 - isGameMaster
 *
 * Returns 1 when the player has the GM flag, 0 when the serial does not
 * resolve.
 */
int
Script_isGameMaster(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "isGameMaster");
	if (ent == NULL)
		return 0;
	return CPlayer_IsGameMaster((CPlayer *)ent);
}

/*
 * 0x0041EFB2 - isManifesting
 *
 * Returns 1 when the player's manifesting flag (pflags & 0x20) is set,
 * 0 when the serial does not resolve.
 */
int
Script_isManifesting(uint32_t serial)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "isManifesting");
	if (ent == NULL)
		return 0;
	return (((CPlayer *)ent)->pflags & PlayerIsManifesting) != 0 ? 1 : 0;
}

/*
 * 0x0041EFEA - getPlayerBugStat [726]
 *
 * Fills a CList with serials of all online players whose
 * stat total (str+dex+int) is >= a threshold. Iterates the
 * g_PlayerList.head linked list, checks each player's bug stat,
 * adds qualifying players to the list, sorts, then appends
 * serials to the output list.
 */
int
Script_getPlayerBugStat(CList *list, int threshold)
{
	CPlayer *p;
	CVector tmpList;
	void *iter;
	char typeFlag = 0;

	CVector_Constructor(&tmpList, &typeFlag);
	for (p = g_PlayerList.head; p != NULL; p = p->next) {
		if (CPlayer_GetBugStat((CItem *)p) >= threshold)
			CVector_PushBack(&tmpList, (uintptr_t)p);
	}
	Vector_SortByType(tmpList.begin, tmpList.end, tmpList.type);
	iter = tmpList.begin;
	while (iter != tmpList.end) {
		uint32_t serial = CMobile_GetSerial((CMobile *)*(uintptr_t *)iter);
		CList_Append(list, 4, serial);
		iter = (char *)iter + sizeof(uintptr_t);
	}
	CVector_Destructor(&tmpList);
	return 1;
}

/*
 * 0x0041F0CF - doNPCHeartBeat
 *
 * Runs CNPC_Heartbeat on the mobile and returns 1, or 0 when the serial
 * does not resolve.
 */
int
Script_doNPCHeartBeat(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileEntityValidated(serial, "doNPCHeartBeat");
	if (ent == NULL)
		return 0;
	CNPC_Heartbeat((CNPC *)ent);
	return 1;
}

/*
 * 0x0041F102 - doNPCHandleStates
 *
 * Runs CNPC_HandleStates on the mobile and returns 1, or 0 when the serial
 * does not resolve. The validation tag string "doNPCHeartBeat" matches the
 * binary's copy-paste from doNPCHeartBeat.
 */
int
Script_doNPCHandleStates(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileEntityValidated(serial, "doNPCHeartBeat");
	if (ent == NULL)
		return 0;
	CNPC_HandleStates((CNPC *)ent);
	return 1;
}

/*
 * 0x0041F135 - addHelpRequestToQueue [730]
 *
 * Adds a help request to the global help queue.
 * Validates player, gets player name via vtable[0x34],
 * constructs CString from name, then calls CHelpQueue::Add or
 * CHelpQueue::AddWithLevel depending on the hasLevel argument.
 * Destructs the CString after the call.
 */
void
Script_addHelpRequestToQueue(uint32_t serial, int hasLevel, uint8_t level, CString *message)
{
	CItem *player;
	CString nameStr;

	player = FindPlayerValidated(serial, "addHelpRequestToQueue");
	if (player == NULL)
		return;
	if (hasLevel) {
		CString_Constructor(&nameStr, (const char *)VT_GetName(player));
		CHelpQueue_AddWithLevel(&g_HelpQueue, CMobile_GetSerial((CMobile *)player), &nameStr, level, message);
		CString_Destructor(&nameStr);
	} else {
		CString_Constructor(&nameStr, (const char *)VT_GetName(player));
		CHelpQueue_Add(&g_HelpQueue, CMobile_GetSerial((CMobile *)player), &nameStr, level, message);
		CString_Destructor(&nameStr);
	}
}

/*
 * 0x0041F217 - getGMCallStatus
 *
 * Returns the global GM call-queue status flag.
 */
int
Script_getGMCallStatus(void)
{
	return CEditorObj_GetGMCallStatus((CEditorObj *)&g_GMPlayerList);
}

/*
 * 0x0041F226 - setGMCallStatus
 *
 * Stores the global GM call-queue status flag.
 */
void
Script_setGMCallStatus(int value)
{
	CEditorObj_SetGMCallStatus((CEditorObj *)&g_GMPlayerList, value);
}

/*
 * 0x0041F239 - fixBank
 *
 * Ensures the mobile has a bank container in equipment slot 29,
 * creating one if missing.
 */
void
Script_fixBank(uint32_t serial)
{
	CItem *ent;

	ent = FindMobileValidated(serial, "fixBank");
	if (ent == NULL)
		return;
	FixBank((CMobile *)ent);
}

/*
 * 0x0041F267 - Script handler for setStatus [720]
 *
 * Sets (value != 0) or clears the named status bit on the entity's
 * item flags.
 */
void
Script_setStatus(uint32_t serial, int bit, int value)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "setStatus");
	if (ent == NULL)
		return;
	CItem_SetItemFlag(ent, (uint8_t)bit, value);
}

/*
 * 0x0041F299 - Script handler for getStatus [721]
 *
 * Returns 1 when the named status bit is set on the entity's item
 * flags.
 */
int
Script_getStatus(uint32_t serial, int bit)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "getStatus");
	if (ent == NULL)
		return 0;
	return CItem_HasItemFlag(ent, bit);
}

/*
 * 0x0041F2CB - putObjBank
 *
 * Puts thingSerial into the mobile's bank box, creating the bank
 * container first if necessary.
 */
int
Script_putObjBank(uint32_t mobileSerial, uint32_t thingSerial)
{
	uint32_t bankSerial;

	Script_fixBank(mobileSerial);
	bankSerial = Script_getItemAtSlot(mobileSerial, 29);
	return Script_putObjContainer(bankSerial, thingSerial);
}

/*
 * 0x0041F300 - getScripts [141]
 *
 * Appends the names of every script attached to the entity to list.
 */
void
Script_getScripts(CList *list, uint32_t serial)
{
	CItem *ent;
	CVector tmpList;
	char typeFlag;

	CList_Clear(list);

	ent = FindEntityValidated(serial, "getStatus");
	if (ent == NULL)
		return;

	if (!CItem_HasScripts(ent))
		return;

	typeFlag = 0;
	CVector_Constructor(&tmpList, &typeFlag);

	CItem_GetScriptListRaw(ent, &tmpList);

	{
		void **viter = (void **)tmpList.begin;
		while (viter != (void **)tmpList.end) {
			void *ptr = *viter;
			void *ptr2;
			char *name;

			if (ptr == NULL)
				goto next;
			ptr2 = *(void **)ptr;
			if (ptr2 == NULL)
				goto next;
			name = *(char **)ptr2;
			if (name == NULL)
				goto next;

			{
				CString nameStr;
				CString_Constructor(&nameStr, name);
				CList_Append(list, 1, (uintptr_t)&nameStr);
				CString_Destructor(&nameStr);
			}
next:
			viter++;
		}
	}

	CVector_Destructor(&tmpList);
}

/*
 * 0x0041F401 - doTakeMoney [733]
 *
 * Removes amount of items of body type bodyType from container
 * and returns the resulting stack's serial. Items withdrawn from
 * the world are dropped at the container's location. Returns 0
 * when the container is missing or has no matching items.
 */
uint32_t
Script_doTakeMoney(uint32_t containerSerial, int bodyType, int amount)
{
	CItem *container;
	CItem *consumed;
	CLocation loc;

	container = FindContainerValidated(containerSerial, "doTakeMoney");
	if (container == NULL)
		return 0;
	if (container->resourceEntity.entity.removedFromWorld)
		return 0;
	CLocation_SetLoc(&loc, ((CLocation * (*)(void *)) VT_FN(container, VT_GET_LOCATION))(container));
	consumed = (CItem *)CContainer_ConsumeResources((CContainer *)container, (uint16_t)bodyType, amount, 0);
	if (consumed == NULL)
		return 0;
	if (consumed->resourceEntity.entity.removedFromWorld) {
		((void (*)(void *, CLocation *))VT_FN(consumed, VT_DROP_AT_FEET))(consumed, &loc);
	}
	return consumed->serial;
}

/*
 * 0x0041F48D - doSCommand
 *
 * Runs str as an admin S-command on behalf of the named player.
 */
void
Script_doSCommand(uint32_t serial, CString *str)
{
	CItem *player;

	player = FindPlayerValidated(serial, "doSCommand");
	if (player == NULL)
		return;
	SCommandManager_Execute(&g_SCommandManager, player, CMobile_GetSerial((CMobile *)player), CString_GetCStr2(str), -1);
}

/*
 * 0x0041F4D1 - transferPlayer
 *
 * Fires the Transfer event (0x41) on the source entity to migrate
 * the named player to another shard. The serverStr argument is
 * dereferenced for validation but not consumed by the event.
 */
void
Script_transferPlayer(uint32_t sourceSerial, uint32_t targetSerial, CString *serverStr)
{
	CItem *entity;
	CItem *player;

	entity = FindEntityValidated(sourceSerial, NULL);
	player = FindPlayerValidated(targetSerial, NULL);
	if (entity == NULL)
		return;
	if (player == NULL)
		return;
	CString_GetData(serverStr);
	Script_FireTransferEvent(entity, player);
}

/*
 * 0x0041F522 - checkTransferAccount
 *
 * Fires the CheckTransfer event (0x40) on the source entity to
 * validate a cross-shard transfer. Only the target player must be
 * valid; the source entity may be NULL.
 */
void
Script_checkTransferAccount(uint32_t sourceSerial, uint32_t targetSerial)
{
	CItem *entity;
	CItem *player;

	entity = FindEntityValidated(sourceSerial, NULL);
	player = FindPlayerValidated(targetSerial, NULL);
	if (player == NULL)
		return;
	Script_FireCheckTransferEvent(entity, player);
}

/*
 * 0x0041F564 - resurrect
 *
 * Resurrects the player and returns the resurrection result, or 0
 * when the serial is not a valid player.
 */
int
Script_resurrect(uint32_t serial, int flag)
{
	CItem *ent;

	ent = FindPlayerValidated(serial, "doSCommand");
	if (ent == NULL)
		return 0;
	return CPlayer_ApplyResurrection((CPlayer *)ent, flag);
}

/*
 * 0x0041F596 - destroyContents
 *
 * Recursively deletes every item inside the container, broadcasting
 * DESTROY_OBJECT to nearby clients.
 */
void
Script_destroyContents(uint32_t containerSerial)
{
	CItem *container;

	container = FindContainerValidated(containerSerial, NULL);
	if (container == NULL)
		return;
	((void (*)(void *))VT_FN(container, VT_DELETE_CONTENTS))(container);
}

/*
 * 0x0041F5C3 - committedCrimeAt
 *
 * Marks the mobile as criminally punishable at loc for duration ticks.
 */
void
Script_committedCrimeAt(uint32_t serial, CLocation *loc, int duration)
{
	CItem *ent;

	ent = FindMobileValidated(serial, NULL);
	if (ent == NULL)
		return;
	CMobile_SetCriminalPunishable((CMobile *)ent, loc, duration);
}

/*
 * 0x0041F5F2 - setCriminal
 *
 * Sets the criminal flag on the mobile for the given duration.
 */
void
Script_setCriminal(uint32_t serial, int duration)
{
	CItem *ent;

	ent = FindMobileValidated(serial, NULL);
	if (ent == NULL)
		return;
	CMobile_SetCriminal((CMobile *)ent, duration);
}

/*
 * 0x0041F61D - isCriminal
 *
 * Returns 1 when the mobile has an active criminal flag.
 */
int
Script_isCriminal(uint32_t serial)
{
	return Script_checkMobile(serial, (int (*)(CItem *))CMobile_IsCriminal, "isCriminal");
}

/*
 * 0x0041F638 - isMurderer
 *
 * Returns 1 when the mobile is flagged as a murderer.
 */
int
Script_isMurderer(uint32_t serial)
{
	return Script_checkEntity(serial, (int (*)(CItem *))CMobile_IsMurderer, "isMurderer");
}

/*
 * 0x0041F653 - getMurderCount
 *
 * Returns the entity's murderCount ObjVar value.
 */
int
Script_getMurderCount(uint32_t serial)
{
	return Script_checkEntity(serial, check_GetMurderCount, "getMurderCount");
}

/*
 * 0x0041F66E - setMurderCount
 *
 * Sets the mobile's murder count, applying any karma threshold
 * adjustments triggered by the change.
 */
void
Script_setMurderCount(uint32_t serial, int count)
{
	CItem *ent;

	ent = FindMobileValidated(serial, 0);
	if (ent == NULL)
		return;
	((void (*)(void *, int))VT_FN(ent, VT_SET_MURDER_COUNT))(ent, count);
}

/*
 * 0x0041F69F - setResurrectionResources
 *
 * Stamps the player with the post-resurrection state (Meat/Humans
 * resource nodes, resist flags, and an incremented murder count).
 */
void
Script_setResurrectionResources(uint32_t serial)
{
	CItem *player;

	player = FindPlayerValidated(serial, NULL);
	if (player == NULL)
		return;
	CMobile_SetResurrectionResources((CMobile *)player);
}

/*
 * 0x0041F6C6 - canBeFreelyAggressedBy
 *
 * Returns 1 when victim allows attacker to act aggressively without
 * triggering criminal flags. Missing entities permissively return 1.
 */
int
Script_canBeFreelyAggressedBy(uint32_t victimSerial, uint32_t attackerSerial)
{
	CItem *attacker;
	CItem *victim;

	attacker = FindEntityValidated(attackerSerial, NULL);
	if (attacker == NULL)
		return 1;
	victim = FindEntityValidated(victimSerial, "receiveAggressionFrom (victim)");
	if (victim == NULL)
		return 1;
	return ((int (*)(void *, void *))VT_FN(victim, VT_CAN_BE_AGGRESSED))(victim, attacker);
}

/*
 * 0x0041F721 - refreshAggression
 *
 * Re-evaluates the entity's aggression / notoriety state.
 */
void
Script_refreshAggression(uint32_t serial)
{
	CItem *ent;

	ent = FindEntityValidated(serial, "refreshAggression");
	if (ent == NULL)
		return;
	((void (*)(CItem *))VT_FN(ent, VT_REFRESH_AGGRESSION))(ent);
}

/*
 * 0x0041F753 - receiveAggressionFrom
 *
 * Records aggressor on victim's attacker list, propagating the
 * aggression notification through the entity vtable.
 */
void
Script_receiveAggressionFrom(uint32_t victimSerial, uint32_t aggressorSerial)
{
	CItem *victim;
	CItem *aggressor;

	victim = FindEntityValidated(victimSerial, "receiveAggressionFrom (victim)");
	if (victim == NULL)
		return;

	aggressor = FindEntityValidated(aggressorSerial, NULL);

	((void (*)(void *, void *))VT_FN(victim, VT_ADD_TO_ATTACKER_LIST))(victim, aggressor);
}

/*
 * 0x0041F79C - receiveUnhealthyActionFrom
 *
 * Notifies victim that attacker performed a hostile (damaging)
 * action against it.
 */
void
Script_receiveUnhealthyActionFrom(uint32_t victimSerial, uint32_t attackerSerial)
{
	CItem *victim;
	CItem *attacker;

	victim = FindEntityValidated(victimSerial, "receiveUnhealthyActionFrom (victim)");
	if (victim == NULL)
		return;

	attacker = FindEntityValidated(attackerSerial, NULL);

	((void (*)(void *, void *, int))VT_FN(victim, VT_NOTIFY_DAMAGE))(victim, attacker, 1);
}

/*
 * 0x0041F7E7 - receiveHelpfulActionFrom
 *
 * Records helper aiding the player; if the player is a criminal
 * or murderer, the helper is flagged for criminal punishment.
 */
void
Script_receiveHelpfulActionFrom(uint32_t playerSerial, uint32_t helperSerial)
{
	CItem *player;
	CItem *helper;

	player = FindPlayerValidated(playerSerial, NULL);
	if (player == NULL)
		return;
	helper = FindEntityValidated(helperSerial, "receiveHelpfulActionFrom (helper)");
	if (helper == NULL)
		return;
	((void (*)(void *, void *))VT_FN(player, VT_CLR_BEHAVIOR))(player, helper);
}

/*
 * 0x0041F838 - copyControllerInfo
 *
 * Stamps the controlled entity with the controller's identity
 * (controller serial, controllerName, and optional controllerGuild
 * / controllerGuildType when the controller has a guild) and
 * detaches the "defensive" script from the controlled entity.
 */
void
Script_copyControllerInfo(uint32_t controlledSerial, uint32_t controllerSerial)
{
	CItem *controlled;
	CItem *controller;
	CString nameStr;
	uint32_t guildId;
	int guildType;

	controlled = FindEntityValidated(controlledSerial, "copyControllerInfo (controlled)");
	controller = FindPlayerValidated(controllerSerial, "copyControllerInfo (controller)");
	if (controlled == NULL || controller == NULL)
		return;

	// Set "controller" = controller's serial (WTYPE_OBJ)
	CEntity_SetObjVar(controlled, "controller", 4, controllerSerial);

	// Set "controllerName" = controller's name (WTYPE_STRING)
	{
		char *cname = ((char *(*)(void *))VT_FN(controller, VT_GET_NAME))(controller);
		CString_Constructor(&nameStr, cname);
		CEntity_SetObjVar(controlled, "controllerName", 1, (uintptr_t)&nameStr);
	}

	// Copy "guildstoneId" from controller to "controllerGuild"
	guildId = 0;
	CResourceEntity_GetTagObj(controller, "guildstoneId", &guildId);
	if (guildId != 0)
		CEntity_SetObjVar(controlled, "controllerGuild", 4, guildId);

	// Copy "guildType" from controller to "controllerGuildType"
	guildType = 0;
	CResourceEntity_GetTagInt(controller, "guildType", &guildType);
	if (guildType != 0)
		CEntity_SetObjVar(controlled, "controllerGuildType", 0, (uint32_t)guildType);

	// Detach "defensive" script from controlled entity
	CResourceEntity_DetachScript(controlled, "defensive");
	CString_Destructor(&nameStr);
}

/*
 * 0x0041F959 - isObscene
 *
 * Returns 1 when text contains profanity according to the server's
 * profanity filter.
 */
int
Script_isObscene(CString *text)
{
	return CheckProfanity(CString_GetCStr2(text));
}

/*
 * 0x0041F96F - logEntry
 *
 * Forwards (type, subtype, serial, str1..str4) to the global event
 * logger. The demo's logger is a no-op that ignores its arguments.
 */
void
Script_logEntry(int type, int subtype, uint32_t serial, CString *str1, CString *str2, CString *str3, CString *str4)
{
	char *s4 = CString_GetBuffer(str4);
	char *s3 = CString_GetBuffer(str3);
	char *s2 = CString_GetBuffer(str2);
	char *s1 = CString_GetBuffer(str1);
	EventLogger_Log(&g_EventLogger, (uint32_t)type, (uint32_t)subtype, serial, s1, s2, s3, s4);
}

/*
 * 0x0041F9AE - setTile
 *
 * Sets the land-tile id at loc, leaving the Z elevation untouched.
 * Out-of-range coordinates or tileIds are silently ignored.
 */
void
Script_setTile(CLocation *loc, int tileID)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return;
	if (tileID < 0)
		return;
	if (tileID >= 0x4000)
		return;
	SetTerrainTile(0, (int)(int16_t)loc->x, (int)(int16_t)loc->y, tileID, -666);
}

/*
 * 0x0041FA01 - getTile
 *
 * Returns the land-tile id at loc.
 */
int
Script_getTile(CLocation *loc)
{
	return Terrain_GetLandTileID(loc->x, loc->y);
}

/*
 * 0x0041FA21 - setElevation
 *
 * Sets the land-tile Z at loc, leaving the tile id untouched.
 * Coordinates outside the map or elevations outside [-128, 128)
 * are silently ignored.
 */
void
Script_setElevation(CLocation *loc, int elevation)
{
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return;
	if (elevation < -128)
		return;
	if (elevation >= 128)
		return;
	SetTerrainTile(0, (int)(int16_t)loc->x, (int)(int16_t)loc->y, -666, elevation);
}

/*
 * 0x0041FA74 - getElevation
 *
 * Returns the land Z height at loc.
 */
int
Script_getElevation(CLocation *loc)
{
	return Terrain_GetLandZ(loc->x, loc->y);
}

/*
 * 0x0041FA92 - createStatic
 *
 * Creates a new static entity at the given location with the
 * specified type ID (graphic). Validates coordinates and bounds-checks
 * typeID to [0, 0x4000). Allocates via CreateStaticEntity, sets
 * bodyType via CEntity_SetBodyType, then inserts into the block's
 * static chain and marks as in-world. Binary does not null-check
 * the CreateStaticEntity return value.
 */
void
Script_createStatic(CLocation *loc, int typeID)
{
	CItem *entity;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return;
	if (typeID < 0)
		return;
	if (typeID >= 0x4000)
		return;

	entity = CreateStaticEntity();

	// CEntity_SetBodyType at 0x00420F60
	CEntity_SetBodyType(entity, (uint16_t)typeID);

	((void (*)(CItem *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, loc);
}

/*
 * 0x0041FAEA - createStaticHued
 *
 * Same as createStatic but also sets the entity's hue/color.
 * Validates coordinates and typeID identically. After SetBodyType,
 * writes hue to entity->color (offset 0x08), then places in world.
 * Binary does not null-check the CreateStaticEntity return value.
 */
void
Script_createStaticHued(CLocation *loc, int typeID, int hue)
{
	CItem *entity;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)(int16_t)loc->x, (int)(int16_t)loc->y))
		return;
	if (typeID < 0)
		return;
	if (typeID >= 0x4000)
		return;

	entity = CreateStaticEntity();

	// CEntity_SetBodyType at 0x00420F60
	CEntity_SetBodyType(entity, (uint16_t)typeID);

	// Set hue (offset 0x08 = entity.color)
	entity->resourceEntity.entity.color = (uint16_t)hue;

	((void (*)(CItem *, CLocation *))VT_FN(entity, VT_DROP_AT_FEET))(entity, loc);
}

/*
 * 0x0041FB4D - updatesOn
 *
 * Restores entity update broadcasts by calling CWorld::RestoreUpdates
 * (0x004D7238). The inner function decrements g_UpdatesSuppressCount
 * and, when it reaches zero, re-enables updates, processes deferred
 * dirty block flags, and sends a global light packet to all players.
 */
void
Script_updatesOn(void)
{
	World_RestoreUpdates();
}

/*
 * 0x0041FB57 - updatesOff
 *
 * Suppresses entity update broadcasts by calling CWorld::SuppressUpdates
 * (0x004D721C). The inner function increments g_UpdatesSuppressCount
 * (0x006EFF44) and sets g_UpdatesEnabled (0x00624E3C) to 0.
 */
void
Script_updatesOff(void)
{
	World_SuppressUpdates();
}

/*
 * 0x0041FB61 - escript [765]
 *
 * Runs an escript file for a player. Validates player,
 * locks statics, formats the escript path, initializes and
 * runs the escript, then unlocks statics.
 */
void
Script_escript(uint32_t serial, CString *scriptName, CString *args)
{
	CItem *player;
	char path[256];
	CEScript ctx;

	player = FindPlayerValidated(serial, "editorEscript");
	if (player == NULL)
		return;
	Static_Lock();
	snprintf(path, sizeof(path), "../.rundir/escripts/%s.esc", CString_GetData(scriptName));
	CEScript_Init(&ctx);
	CEScript_Run(&ctx, path, player, CString_GetData(args));
	Static_Unlock();
}

/*
 * 0x0041FBDD - openGenericGump
 *
 * Wombat handler: builds and sends a generic gump (packet 0xB0) to a player.
 * Takes entity serial, player serial, gump ID, x/y position, and two lists
 * (command layout strings and text lines). Sends via SendToClient.
 */
void
Script_openGenericGump(uint32_t entitySerial, uint32_t playerSerial, int gumpId, int x, int y, CList *cmdList, CList *textList)
{
	uint8_t buf[0x1001C];
	CItem *entity;
	CItem *player;

	entity = FindEntityValidated(entitySerial, "openGenericGump");
	player = FindPlayerValidated(playerSerial, "openGenericGump");
	if (entity == NULL || player == NULL)
		return;
	PacketManager_MakePacket_GUMP_GENERIC(buf, entitySerial, gumpId, x, y, cmdList, textList);
	SendToClient(player, buf, -1);
}

/*
 * 0x0041FC5E - getRattishSyllable
 *
 * Stores the (index % 103)-th syllable from the Ratman language
 * table in out and returns out.
 */
CString *
Script_getRattishSyllable(CString *out, int index)
{
	static const char *table[] = {
		"chi",
		"cha",
		"cho",
		"chu",
		"chy",
		"che",
		"ach",
		"ech",
		"ich",
		"och",
		"uch",
		"ych",
		"itt",
		"att",
		"ott",
		"utt",
		"ytt",
		"ett",
		"tit",
		"tat",
		"tet",
		"tot",
		"tut",
		"tyt",
		"tti",
		"tta",
		"tte",
		"tto",
		"ttu",
		"tty",
		"tchi",
		"tcha",
		"tche",
		"tcho",
		"tchu",
		"tchy",
		"rik",
		"rak",
		"rek",
		"rok",
		"ruk",
		"ryk",
		"rich",
		"rach",
		"rech",
		"roch",
		"ruch",
		"rych",
		"rrup",
		"rrap",
		"rrep",
		"rrop",
		"rrup",
		"rryp",
		"it",
		"at",
		"et",
		"ot",
		"ut",
		"yt",
		"it",
		"ti",
		"it",
		"ti",
		"ch",
		"ch",
		"ik",
		"ak",
		"ek",
		"ok",
		"uk",
		"yk",
		"ccka",
		"ccke",
		"ccki",
		"ccko",
		"ccku",
		"ccky",
		"ka",
		"ke",
		"ki",
		"ko",
		"ku",
		"ky",
		"tak",
		"tek",
		"tik",
		"tok",
		"tuk",
		"tyk",
		"ack",
		"eck",
		"ick",
		"ock",
		"uck",
		"yck",
		"cka",
		"cke",
		"cki",
		"cko",
		"cku",
		"cky",
		"skrit",
	};
	int count = nelem(table);

	CString_Constructor(out, table[(unsigned int)index % count]);
	return out;
}

/*
 * 0x00420048 - getLizardishSyllable
 *
 * Returns a Lizardman syllable from a 73-entry table, selected by index % 73.
 */
CString *
Script_getLizardishSyllable(CString *out, int index)
{
	static const char *table[] = {
		"ss",
		"sth",
		"iss",
		"is",
		"ith",
		"kth",
		"sith",
		"this",
		"its",
		"sit",
		"tis",
		"tsi",
		"ssi",
		"sil",
		"lis",
		"sis",
		"lil",
		"thil",
		"lith",
		"sthi",
		"lish",
		"shi",
		"shas",
		"sal",
		"miss",
		"ra",
		"tha",
		"thes",
		"ses",
		"sas",
		"las",
		"les",
		"sath",
		"sia",
		"ais",
		"isa",
		"asi",
		"asth",
		"stha",
		"sthi",
		"isth",
		"asa",
		"ath",
		"tha",
		"als",
		"sla",
		"thth",
		"ci",
		"ce",
		"cy",
		"yss",
		"ys",
		"yth",
		"syth",
		"thys",
		"yts",
		"syt",
		"tys",
		"tsy",
		"ssy",
		"syl",
		"lys",
		"sys",
		"lyl",
		"thyl",
		"lyth",
		"sthy",
		"lysh",
		"shy",
		"myss",
		"ysa",
		"sthy",
		"ysth",
	};
	int count = nelem(table);

	CString_Constructor(out, table[(unsigned int)index % count]);
	return out;
}

/*
 * 0x00420306 - getWispishSyllable
 *
 * Returns a Wisp syllable from a 43-entry table, selected by index % 43.
 * Table contains single consonants and repeated consonants for Wisp speech.
 */
CString *
Script_getWispishSyllable(CString *out, int index)
{
	static const char *table[] = {
		"b",
		"c",
		"d",
		"f",
		"g",
		"h",
		"i",
		"j",
		"k",
		"l",
		"m",
		"n",
		"p",
		"q",
		"r",
		"s",
		"t",
		"v",
		"w",
		"x",
		"z",
		"c",
		"c",
		"x",
		"x",
		"x",
		"x",
		"x",
		"y",
		"y",
		"y",
		"y",
		"t",
		"t",
		"k",
		"k",
		"l",
		"l",
		"m",
		"m",
		"m",
		"m",
		"z",
	};
	int count = nelem(table);

	CString_Constructor(out, table[(unsigned int)index % count]);
	return out;
}

/*
 * 0x00420498 - getOrcishSyllable
 *
 * Returns an Orc syllable from a 215-entry table, selected by index % 215.
 */
CString *
Script_getOrcishSyllable(CString *out, int index)
{
	static const char *table[] = {
		"bu",
		"du",
		"fu",
		"ju",
		"gu",
		"ulg",
		"gug",
		"gub",
		"gur",
		"oog",
		"gub",
		"lug",
		"ru",
		"stu",
		"glu",
		"ug",
		"ud",
		"og",
		"log",
		"ro",
		"flu",
		"bo",
		"duf",
		"fun",
		"nog",
		"dun",
		"bog",
		"dug",
		"gh",
		"ghu",
		"gho",
		"nug",
		"ig",
		"igh",
		"ihg",
		"luh",
		"duh",
		"bug",
		"dug",
		"dru",
		"urd",
		"gurt",
		"grut",
		"grunt",
		"snarf",
		"urgle",
		"igg",
		"glu",
		"glug",
		"foo",
		"bar",
		"baz",
		"ghat",
		"ab",
		"ad",
		"gugh",
		"guk",
		"ag",
		"alm",
		"thu",
		"log",
		"bilge",
		"augh",
		"gha",
		"gig",
		"goth",
		"zug",
		"pig",
		"auh",
		"gan",
		"azh",
		"bag",
		"hig",
		"oth",
		"dagh",
		"gulg",
		"ugh",
		"ba",
		"bid",
		"gug",
		"bug",
		"rug",
		"hat",
		"brui",
		"gagh",
		"buad",
		"buil",
		"buim",
		"bum",
		"hug",
		"buo",
		"ma",
		"buor",
		"ghed",
		"buu",
		"ca",
		"guk",
		"clog",
		"thurg",
		"car",
		"cro",
		"thu",
		"da",
		"cuk",
		"gil",
		"cur",
		"dak",
		"dar",
		"deak",
		"der",
		"dil",
		"dit",
		"at",
		"ag",
		"dor",
		"gar",
		"dre",
		"tk",
		"dri",
		"gka",
		"rim",
		"eag",
		"egg",
		"ha",
		"rod",
		"eg",
		"lat",
		"eichel",
		"ek",
		"ep",
		"ka",
		"it",
		"ut",
		"ewk",
		"ba",
		"dagh",
		"faugh",
		"foz",
		"fog",
		"fid",
		"fruk",
		"gag",
		"fub",
		"fud",
		"fur",
		"bog",
		"fup",
		"hagh",
		"gaa",
		"kt",
		"rekk",
		"lub",
		"lug",
		"tug",
		"gna",
		"urg",
		"l",
		"gno",
		"gnu",
		"gol",
		"gom",
		"kug",
		"ukk",
		"jak",
		"jek",
		"rukk",
		"jja",
		"akt",
		"nuk",
		"hok",
		"hrol",
		"olm",
		"natz",
		"i",
		"i",
		"o",
		"u",
		"ikk",
		"ign",
		"juk",
		"kh",
		"kgh",
		"ka",
		"hig",
		"ke",
		"ki",
		"klap",
		"klu",
		"knod",
		"kod",
		"knu",
		"thu",
		"krug",
		"nug",
		"nar",
		"nag",
		"neg",
		"neh",
		"oag",
		"ob",
		"ogh",
		"oh",
		"om",
		"dud",
		"oo",
		"pa",
		"hrak",
		"qo",
		"qua",
		"quil",
		"ghig",
		"rur",
		"sag",
		"sah",
		"sg",
	};
	int count = nelem(table);

	CString_Constructor(out, table[(unsigned int)index % count]);
	return out;
}

/*
 * 0x00420CE2 - abs
 *
 * Returns the absolute value of value.
 */
int
Script_abs(int value)
{
	return abs(value);
}

/*
 * 0x00420D50 - CArray::IsValid
 *
 * Returns 1 if this->data != NULL, else 0.
 */
static int
CArray_IsValid(WombatArray *arr)
{
	return arr->data != NULL;
}

/*
 * 0x00420DA0 - CompareTokenType
 *
 * Returns 1 when the decoded tokenBuf matches tokenType. Variant-
 * encoded tokens compare the leading 16-bit word against any of
 * the type's permitted encodings; text-based tokens use strcmp
 * (vs the prefix memcmp in ScriptTokenizer_MatchToken, which works
 * on raw bytecode).
 */
int
CompareTokenType(const char *tokenBuf, int tokenType)
{
	if (tokenType < 0 || tokenType >= TOKEN_TYPE_COUNT)
		return 0;

	// Variant-encoded tokens: compare uint16 (same as MatchToken)
	if (g_TokenVariants[tokenType][0] != 0) {
		uint16_t streamVal;
		int i;
		memcpy(&streamVal, tokenBuf, 2);
		for (i = 0; i < 5; i++) {
			if (g_TokenVariants[tokenType][i] == streamVal)
				return 1;
		}
		return 0;
	}

	// String tokens: exact strcmp match (binary uses 0x004E8910)
	if (g_TriggerNames[tokenType] != NULL) {
		if (strcmp(tokenBuf, g_TriggerNames[tokenType]) == 0)
			return 1;
	}

	return 0;
}

/*
 * 0x0042E160 - CExecThread::~CExecThread (scalar deleting destructor)
 *
 * Runs the destructor and frees the thread when flags&1 is set.
 */
CExecThread *
CExecThread_ScalarDelete(CExecThread *thread, int flags)
{
	CExecThread_Destructor(thread);
	if (flags & 1)
		OperatorDelete(thread);
	return NULL;
}

/*
 * 0x0042E220 - CVector::operator[]
 *
 * Returns a pointer to the index-th pointer-sized element of vec.
 */
void *
CVector_At(CVector *vec, int index)
{
	uintptr_t base = (uintptr_t)CSearchCtx_GetBucket((CSearchCtx *)vec);
	return (void *)(base + index * sizeof(uintptr_t));
}
/*
 * 0x0042F8FA - CheckGoldLimit
 *
 * Checks if a mobile has >= 2000 gold (type 0xEED). If so,
 * calls CMobile_FindItemInEquipment to locate the gold item.
 */
static void __attribute__((unused))
CheckGoldLimit(CMobile *mob)
{
	int total;

	total = CMobile_GetTotalQuantityOfType(mob, 0xEED);
	if (total >= 0x7D0)
		CMobile_FindItemInEquipment(mob, 0xEED, total);
}
/*
 * 0x0042F95E - World_DeleteAllButFixtures
 *
 * Walks the whole serial hash and deletes every entity whose tiledata
 * flags carry none of TF_DOOR, TF_LIGHT or 0x00010000. The flags are
 * fetched through vtable[0x30] once per bit rather than once per
 * entity.
 */
static __attribute__((unused)) void
World_DeleteAllButFixtures(void)
{
	CItem *ent, *next;
	int i;

	for (i = 0; i < 0x10000; i++) {
		for (ent = g_World->hashTable[i]; ent != NULL; ent = next) {
			next = ent->hashNext;

			if (((uint32_t (*)(void *))VT_FN(ent, VT_GET_FLAGS))(ent)&TF_DOOR)
				continue;
			if (((uint32_t (*)(void *))VT_FN(ent, VT_GET_FLAGS))(ent)&TF_LIGHT)
				continue;
			if (((uint32_t (*)(void *))VT_FN(ent, VT_GET_FLAGS))(ent) & 0x00010000)
				continue;

			if (ent != NULL)
				((void (*)(void *))VT_FN(ent, VT_DELETE))(ent);
		}
	}
}

/*
 * 0x00432EA0 - BM_CaseInsensitiveSearch
 *
 * Boyer-Moore case-insensitive substring search. Returns the
 * offset in text where pattern starts, or -1 when no match.
 */
static int
BM_CaseInsensitiveSearch(const char *pattern, const char *text)
{
	int patLen, textLen;
	int patLast;
	int pos, j;
	int badShift, goodShift;

	patLen = (int)strlen(pattern);
	patLast = patLen - 1;
	textLen = (int)strlen(text);
	pos = patLast;

	BM_BuildBadCharTable(pattern);
	BM_BuildGoodSuffixTable(pattern);

	while (pos < textLen) {
		for (j = 0; j < patLen; j++) {
			if (tolower((unsigned char)text[pos - j]) != tolower((unsigned char)pattern[patLast - j]))
				break;
		}
		if (j == patLen)
			return pos - patLast;

		// Shift by max(bad char, good suffix)
		badShift = g_BMBadChar[tolower((unsigned char)text[pos - j])];
		goodShift = g_BMGoodSuffix[j];
		pos = BM_Max(pos - j + badShift, pos + goodShift);
	}

	return -1;
}

/*
 * 0x00432FAC - BM_BuildBadCharTable
 *
 * Builds the bad-character shift table for case-insensitive
 * Boyer-Moore from pattern. All 256 entries default to the
 * pattern length; characters that appear in pattern are set to
 * (len-1) - last-index.
 */
static void
BM_BuildBadCharTable(const char *pattern)
{
	int len, lastIdx, i;

	len = (int)strlen(pattern);
	lastIdx = len - 1;

	for (i = 0; i < 256; i++)
		g_BMBadChar[i] = len;

	for (i = 0; pattern[i] != '\0'; i++)
		g_BMBadChar[tolower((unsigned char)pattern[i])] = lastIdx - i;
}

/*
 * 0x0043303A - BM_BuildGoodSuffixTable
 *
 * Builds the good-suffix shift table for case-insensitive
 * Boyer-Moore from pattern. Three-pass algorithm: compute suffix
 * lengths, fill default shifts, then apply the suffix overrides.
 */
static void
BM_BuildGoodSuffixTable(const char *pattern)
{
	int len, lastIdx;
	int suffix[104];
	int i, j;
	int lastJ;

	len = (int)strlen(pattern);
	lastIdx = len - 1;

	// Pass 1: compute suffix match lengths
	for (i = 1; i < len; i++) {
		j = 0;
		while (j < len) {
			if (tolower((unsigned char)pattern[lastIdx - j]) != tolower((unsigned char)pattern[lastIdx - i - j]))
				break;
			j++;
		}
		suffix[i] = j;
	}

	// Pass 2: default all shifts to len
	g_BMGoodSuffix[0] = 1;
	for (i = 1; i < len; i++)
		g_BMGoodSuffix[i] = len;

	// Pass 3a: apply suffix -> shift table (reverse scan)
	for (i = lastIdx; i > 0; i--)
		g_BMGoodSuffix[suffix[i]] = i;

	// Pass 3b: final override pass
	lastJ = 0;
	for (i = 1; i < len; i++) {
		if (suffix[i] == lastIdx - i)
			lastJ = i;
		if (lastJ != 0)
			g_BMGoodSuffix[i] = lastJ;
	}
}

/*
 * 0x004331A0 - BM_Max
 *
 * Returns the larger of two integers.
 */
static int
BM_Max(int a, int b)
{
	if (a > b)
		return a;
	return b;
}

/*
 * NamedResource table (binary: 0x00615390, 52 entries + NULL terminator).
 * Each 12-byte entry maps a resource name to a file path.
 * The data field is populated at startup by NamedResource_LoadAll.
 */
NamedResource g_NamedResources[] = { { "login.txt", "../.rundir/login.txt", NULL }, { "update.txt", "../.rundir/update.txt", NULL },
	{ "tip0.txt", "../.rundir/tips/tip0.txt", NULL }, { "tip1.txt", "../.rundir/tips/tip1.txt", NULL }, { "tip2.txt", "../.rundir/tips/tip2.txt", NULL },
	{ "tip3.txt", "../.rundir/tips/tip3.txt", NULL }, { "tip4.txt", "../.rundir/tips/tip4.txt", NULL }, { "tip5.txt", "../.rundir/tips/tip5.txt", NULL },
	{ "tip6.txt", "../.rundir/tips/tip6.txt", NULL }, { "tip7.txt", "../.rundir/tips/tip7.txt", NULL }, { "tip8.txt", "../.rundir/tips/tip8.txt", NULL },
	{ "tip9.txt", "../.rundir/tips/tip9.txt", NULL }, { "tip10.txt", "../.rundir/tips/tip10.txt", NULL }, { "tip11.txt", "../.rundir/tips/tip11.txt", NULL },
	{ "tip12.txt", "../.rundir/tips/tip12.txt", NULL }, { "tip13.txt", "../.rundir/tips/tip13.txt", NULL }, { "tip14.txt", "../.rundir/tips/tip14.txt", NULL },
	{ "tip15.txt", "../.rundir/tips/tip15.txt", NULL }, { "tip16.txt", "../.rundir/tips/tip16.txt", NULL }, { "tip17.txt", "../.rundir/tips/tip17.txt", NULL },
	{ "tip18.txt", "../.rundir/tips/tip18.txt", NULL }, { "tip19.txt", "../.rundir/tips/tip19.txt", NULL }, { "tip20.txt", "../.rundir/tips/tip20.txt", NULL },
	{ "tip21.txt", "../.rundir/tips/tip21.txt", NULL }, { "tip22.txt", "../.rundir/tips/tip22.txt", NULL }, { "tip23.txt", "../.rundir/tips/tip23.txt", NULL },
	{ "tip24.txt", "../.rundir/tips/tip24.txt", NULL }, { "tip25.txt", "../.rundir/tips/tip25.txt", NULL }, { "tip26.txt", "../.rundir/tips/tip26.txt", NULL },
	{ "tip27.txt", "../.rundir/tips/tip27.txt", NULL }, { "tip28.txt", "../.rundir/tips/tip28.txt", NULL }, { "tip29.txt", "../.rundir/tips/tip29.txt", NULL },
	{ "tip30.txt", "../.rundir/tips/tip30.txt", NULL }, { "tip31.txt", "../.rundir/tips/tip31.txt", NULL }, { "tip32.txt", "../.rundir/tips/tip32.txt", NULL },
	{ "tip33.txt", "../.rundir/tips/tip33.txt", NULL }, { "tip34.txt", "../.rundir/tips/tip34.txt", NULL }, { "tip35.txt", "../.rundir/tips/tip35.txt", NULL },
	{ "tip36.txt", "../.rundir/tips/tip36.txt", NULL }, { "tip37.txt", "../.rundir/tips/tip37.txt", NULL }, { "tip38.txt", "../.rundir/tips/tip38.txt", NULL },
	{ "tip39.txt", "../.rundir/tips/tip39.txt", NULL }, { "tip40.txt", "../.rundir/tips/tip40.txt", NULL }, { "tip41.txt", "../.rundir/tips/tip41.txt", NULL },
	{ "tip42.txt", "../.rundir/tips/tip42.txt", NULL }, { "tip43.txt", "../.rundir/tips/tip43.txt", NULL }, { "tip44.txt", "../.rundir/tips/tip44.txt", NULL },
	{ "tip45.txt", "../.rundir/tips/tip45.txt", NULL }, { "tip46.txt", "../.rundir/tips/tip46.txt", NULL }, { "tip47.txt", "../.rundir/tips/tip47.txt", NULL },
	{ "tip48.txt", "../.rundir/tips/tip48.txt", NULL }, { "tip49.txt", "../.rundir/tips/tip49.txt", NULL }, { NULL, NULL, NULL } };

// SCommand table entry: {name, handler}
// Global table at 0x0063F968 in BSS.
SCommandEntry g_SCommandTable[128]; // 0x0063F968

// EventLogger global at 0x00699A40. Binary calls EventLogger_Constructor during
// static init to set field0 = 1 (enabled). We initialize directly.
EventLogger g_EventLogger = { 1 }; // 0x00699A40

int g_ConfigSpawnClient = 1;

uint16_t g_ConfigStartX = 0;
uint16_t g_ConfigStartY = 0;
uint16_t g_ConfigStartZ = 0;
/*
 * CEScript - escript file interpreter.
 *
 * The interpreter holds a CEScript context with the open file, up to
 * 1024 variable slots, the active player, current script/line state,
 * a loop-stack of starting line numbers, and an error flag. Variables
 * are polymorphic: '%' integer variables hold a single int; '$' string
 * variables hold up to 1024 comma-parsed ints and return one at random.
 */

// CEScript field offsets, struct, and accessor macros defined in wombat.h.

// Global line buffer for CEScript::ReadLine
char g_esLineBuf[4096]; // 0x00645B50

// Global CEScript context (binary: 0x00645C50, 0x141C bytes on 32-bit)
CEScript g_CEScriptCtx; // 0x00645C50

// Vtable function pointer types for CEScriptVar
typedef void *(*ESVarScalarDestructor)(void *, int);
typedef int (*ESVarGetValue)(void *);
typedef void (*ESVarSetFromInt)(void *, int);
typedef void (*ESVarSetFromString)(void *, const char *);
typedef void *(*ESVarClone)(void *);

// CEScriptIntVar vtable - binary: 0x005EECD8
void *g_CEScriptIntVar_vtable[] = {
	(void *)(uintptr_t)CEScriptIntVar_ScalarDtor,
	(void *)(uintptr_t)CEScriptIntVar_GetValue,
	(void *)(uintptr_t)CEScriptIntVar_SetFromInt,
	(void *)(uintptr_t)CEScriptIntVar_SetFromString,
	(void *)(uintptr_t)CEScriptIntVar_Clone,
};

// CEScriptStringVar vtable - binary: 0x005EECC0
void *g_CEScriptStringVar_vtable[] = {
	(void *)(uintptr_t)CEScriptStringVar_ScalarDtor,
	(void *)(uintptr_t)CEScriptStringVar_GetValue,
	(void *)(uintptr_t)CEScriptStringVar_SetFromInt,
	(void *)(uintptr_t)CEScriptStringVar_SetFromString,
	(void *)(uintptr_t)CEScriptStringVar_Clone,
};

/*
 * 0x00463A80 - CHintItem::CHintItem
 *
 * Initialises a hint with default fields: hintId = -1, all serials
 * and counters zeroed, name buffers cleared, and location set to
 * (0xFFFF, 0xFFFF, 0).
 */
static CHintItem *
CHintItem_Constructor(CHintItem *self)
{
	CLocation_Init(&self->location);
	self->hintId = -1;
	self->serial = 0;
	self->hintFlags = 0;
	memset(self->name1, 0, sizeof(self->name1));
	strcpy(self->name1, "");
	memset(self->name2, 0, sizeof(self->name2));
	strcpy(self->name2, "");
	self->location.x = 0xFFFF;
	self->location.y = 0xFFFF;
	self->location.z = 0;
	self->sourceObj = 0;
	memset(self->name3, 0, sizeof(self->name3));
	strcpy(self->name3, "");
	self->flags = 0;
	return self;
}

/*
 * 0x00463B9E - CHintItem::~CHintItem
 *
 * No-op destructor (CHintItem owns no heap allocations).
 */
void
CHintItem_Destructor(CHintItem *self)
{
	USED(self);
}

/*
 * 0x00463BA9 - CHintEntry::Copy
 *
 * Copies every field from src into this. Returns this.
 */
CHintItem *
CHintEntry_Copy(CHintItem *this, CHintItem *src)
{
	this->hintId = src->hintId;
	this->serial = src->serial;
	this->hintFlags = src->hintFlags;
	memcpy(this->name1, src->name1, sizeof(this->name1));
	memcpy(this->name2, src->name2, sizeof(this->name2));
	this->location.x = src->location.x;
	this->location.y = src->location.y;
	this->location.z = src->location.z;
	this->sourceObj = src->sourceObj;
	this->flags = src->flags;
	memcpy(this->name3, src->name3, sizeof(this->name3));
	return this;
}

/*
 * 0x00463C8C - CHintItem::SetName1
 *
 * Copies name into the hint's name1 field with NUL termination.
 */
static void
CHintItem_SetName1(CHintItem *self, const char *name)
{
	memset(self->name1, 0, sizeof(self->name1));
	strncpy(self->name1, name, sizeof(self->name1) - 1);
	self->name1[sizeof(self->name1) - 1] = '\0';
}

/*
 * 0x00463CC8 - CHintItem::SetName2
 *
 * Copies name into the hint's name2 field (0x80 bytes) with NUL termination.
 */
static void
CHintItem_SetName2(CHintItem *self, const char *name)
{
	memset(self->name2, 0, sizeof(self->name2));
	strncpy(self->name2, name, sizeof(self->name2) - 1);
	self->name2[sizeof(self->name2) - 1] = '\0';
}

/*
 * 0x00463D0A - CHintItem::SetName3
 *
 * Copies name into the hint's name3 field with NUL termination.
 */
static void
CHintItem_SetName3(CHintItem *self, const char *name)
{
	memset(self->name3, 0, sizeof(self->name3));
	strncpy(self->name3, name, sizeof(self->name3) - 1);
	self->name3[sizeof(self->name3) - 1] = '\0';
}

/*
 * 0x00463D4E - CHintItem::Deserialize
 *
 * Deserializes a CHintItem from buf. Returns 1 on success, 0 when size
 * disagrees with GetSerializedSize. The serialized format uses 4-byte
 * slots for x/y/z (only the low word is read) and fixed-size 0x40/0x80/
 * 0x20 string blocks copied with strncpy.
 */
static int __attribute__((unused))
CHintItem_Deserialize(CHintItem *self, const char *buf, int size)
{
	int offset;

	if (size != CHintItem_GetSerializedSize(self))
		return 0;

	offset = 0;

	// hintId (4 bytes)
	memcpy(&self->hintId, buf + offset, 4);
	offset += 4;

	// serial (4 bytes)
	memcpy(&self->serial, buf + offset, 4);
	offset += 4;

	// hintFlags (4 bytes)
	memcpy(&self->hintFlags, buf + offset, 4);
	offset += 4;

	// name1 (0x40 bytes)
	strncpy(self->name1, buf + offset, sizeof(self->name1));
	offset += 0x40;

	// name2 (0x80 bytes)
	strncpy(self->name2, buf + offset, sizeof(self->name2));
	offset += 0x80;

	// x (word from 4-byte slot)
	self->location.x = *(int16_t *)(buf + offset);
	offset += 4;

	// y (word from 4-byte slot)
	self->location.y = *(int16_t *)(buf + offset);
	offset += 4;

	// z (word from 4-byte slot)
	self->location.z = *(int16_t *)(buf + offset);
	offset += 4;

	// sourceObj (4 bytes)
	memcpy(&self->sourceObj, buf + offset, 4);
	offset += 4;

	// name3 (0x20 bytes)
	strncpy(self->name3, buf + offset, sizeof(self->name3));
	offset += 0x20;

	// flags (4 bytes)
	memcpy(&self->flags, buf + offset, 4);
	offset += 4;

	return 1;
}

/*
 * 0x00463EB5 - CHintItem::Serialize
 *
 * Allocates a GetSerializedSize-sized buffer and serializes every field
 * into it. String fields go in as fixed-size NUL-terminated blocks; x/y/z
 * are sign-extended from int16 to int32 before being stored in 4-byte slots.
 */
__attribute__((unused)) static CHintItem *
CHintItem_Serialize(CHintItem *self)
{
	char *buf;
	int offset;
	int32_t tmpX, tmpY, tmpZ;

	buf = (char *)OperatorNew(CHintItem_GetSerializedSize(self));
	offset = 0;

	// hintId (4 bytes)
	memcpy(buf + offset, &self->hintId, 4);
	offset += 4;

	// serial (4 bytes)
	memcpy(buf + offset, &self->serial, 4);
	offset += 4;

	// hintFlags (4 bytes)
	memcpy(buf + offset, &self->hintFlags, 4);
	offset += 4;

	// name1 (0x3F bytes, then null terminate)
	memcpy(buf + offset, self->name1, 0x3F);
	offset += 0x3F;
	buf[offset] = '\0';
	offset += 1;

	// name2 (0x7F bytes, then null terminate)
	memcpy(buf + offset, self->name2, 0x7F);
	offset += 0x7F;
	buf[offset] = '\0';
	offset += 1;

	// x/y/z: sign-extend int16 to int32
	tmpX = (int32_t)self->location.x;
	tmpY = (int32_t)self->location.y;
	tmpZ = (int32_t)self->location.z;

	memcpy(buf + offset, &tmpX, 4);
	offset += 4;

	memcpy(buf + offset, &tmpY, 4);
	offset += 4;

	memcpy(buf + offset, &tmpZ, 4);
	offset += 4;

	// sourceObj (4 bytes)
	memcpy(buf + offset, &self->sourceObj, 4);
	offset += 4;

	// name3 (0x1F bytes, then null terminate)
	memcpy(buf + offset, self->name3, 0x1F);
	offset += 0x1F;
	buf[offset] = '\0';
	offset += 1;

	// flags (4 bytes)
	memcpy(buf + offset, &self->flags, 4);
	offset += 4;

	return (CHintItem *)buf;
}

/*
 * 0x004640A4 - CHintItem::GetSerializedSize
 *
 * Returns the on-disk size of a CHintItem (256 bytes), used to
 * validate buffers before deserialisation.
 */
static int __attribute__((unused))
CHintItem_GetSerializedSize(CHintItem *self)
{
	USED(self);
	return 0x100;
}

/*
 * 0x004640C0 - CHintManager::CHintManager
 *
 * Initializes the underlying CResManager with flags=0.
 */
static __attribute__((unused)) void *
CHintManager_Constructor(CResManager *self)
{
	CResManager_Constructor(self, 0);
	return self;
}

/*
 * 0x004640D8 - CHintManager::~CHintManager
 *
 * Delegates to CResManager_Destructor_Hint, which deletes every key/value
 * list node across the 66 buckets.
 */
static __attribute__((unused)) void
CHintManager_Destructor(CResManager *self)
{
	CResManager_Destructor_Hint(self);
}

/*
 * 0x004640EB - CHintManager::Find
 *
 * Searches the hint manager for an entry near serial. Hashes serial into the
 * manager's count to pick a starting slot, then uses flags as a window
 * radius to compute a [startIdx, endIdx] range. Picks a random index in
 * that range, iterates forward that many positions through the CResManager,
 * then copies the matching entry via CHintEntry_Copy. Returns 1 on match,
 * 0 on miss.
 */
static int
CHintManager_Find(CResManager *manager, uint32_t serial, int flags, CHintItem *outHint)
{
	CResManager *rm = manager;
	unsigned int count;
	unsigned int slot;
	int startIdx, endIdx;
	int iterCount;
	CSearchCtx ctx;
	CSearchCtx tempBegin;
	CSearchCtx tempNext;
	int counter;
	void *result;

	count = (unsigned int)rm->count;
	if (count <= 0)
		return 0;

	// compiler redundancy: re-reads count
	count = (unsigned int)rm->count;
	slot = serial % count;
	startIdx = (int)slot;
	endIdx = (int)(slot + (unsigned int)flags);

	// compiler redundancy: re-reads count
	count = (unsigned int)rm->count;
	if (endIdx >= (int)count) {
		if ((int)slot - flags < 0) {
			// compiler redundancy: re-reads count
			count = (unsigned int)rm->count;
			endIdx = (int)(count - 1);
		} else {
			startIdx = (int)slot - flags;
			endIdx = (int)slot;
		}
	}

	iterCount = GetRandomRange(startIdx, endIdx);

	CSearchCtx_Constructor(&ctx);
	counter = 0;

	CSearchCtx_Add(&ctx, CHintManager_Begin(rm, &tempBegin));

	while (CSearchCtx_Find(&ctx)) {
		if (counter >= iterCount)
			break;
		CSearchCtx_Add(&ctx, CHintManager_Next(rm, &tempNext, &ctx));
		counter++;
	}

	if (!CSearchCtx_Find(&ctx))
		return 0;

	result = CResManager_GetResultCtx(rm, &ctx);
	CHintEntry_Copy(outHint, (CHintItem *)result);
	return 1;
}

/*
 * 0x00464201 - CHintManager::Add
 *
 * Adds a CHintItem to the hint manager, evicting any prior entry with the
 * same serial. When the manager already holds more than 1000 entries, the
 * incoming item is destroyed instead of being inserted.
 */
static void
CHintManager_Add(CResManager *manager, CHintItem *item)
{
	CResManager *rm = manager;
	CSearchCtx ctx;
	CSearchCtx tempIter;
	CSearchCtx eraseOutput;

	if (rm->count > 1000) {
		if (item != NULL)
			CHintItem_ScalarDelete(item, 1);
		return;
	}

	CSearchCtx_Constructor(&ctx);
	CSearchCtx_Add(&ctx, CResManager_BeginIterInternalHint(rm, &tempIter, (uint32_t *)&item->serial, 1));

	if (CSearchCtx_Find(&ctx))
		CHintManager_Erase(rm, &eraseOutput, &ctx, 1);

	CHintManager_CreateBucketPair(rm, &item->serial, item);
}

/*
 * 0x004642A0 - CHintManager::Tick
 *
 * Increments the flags counter on every hint and erases hints whose count
 * exceeds 6. Fired every 0x7FF ticks from CTimeManager::Update.
 */
void
CHintManager_Tick(void)
{
	CResManager *manager = &g_HintManager;
	CSearchCtx ctx;
	CSearchCtx tempBegin;
	CSearchCtx tempErase;
	CSearchCtx tempNext;
	CHintItem *result;

	CSearchCtx_Constructor(&ctx);
	CSearchCtx_Add(&ctx, CHintManager_Begin(manager, &tempBegin));

	for (;;) {
		if (!CSearchCtx_Find(&ctx))
			break;

		result = (CHintItem *)CResManager_GetResult(manager, &ctx);
		result->flags = result->flags + 1;

		result = (CHintItem *)CResManager_GetResult(manager, &ctx);
		if (result->flags > 6) {
			CSearchCtx_Add(&ctx, CHintManager_Erase(manager, &tempErase, &ctx, 1));
		} else {
			CSearchCtx_Add(&ctx, CHintManager_Next(manager, &tempNext, &ctx));
		}
	}
}

/*
 * 0x00464350 - CHintItem::ScalarDelete
 *
 * Runs CHintItem_Destructor, then conditionally frees.
 */
CHintItem *
CHintItem_ScalarDelete(CHintItem *self, int flags)
{
	CHintItem_Destructor(self);
	if (flags & 1)
		OperatorDelete(self);
	return NULL;
}

/*
 * 0x004643A0 - CHintManager::CreateBucketPair
 *
 * Inserts (key, value) into the bucket selected by hashing key. Lazily
 * allocates the bucket's key/value CResLists. When rm->flags==1 the call
 * is a no-op if the key already exists; otherwise the pair is appended
 * and the count is bumped.
 */
static int
CHintManager_CreateBucketPair(CResManager *rm, void *keyPtr, void *valuePtr)
{
	uint32_t bucket;
	CResList *newList;

	bucket = ResManager_HashInt(*(uint32_t *)keyPtr, 0x41);

	if (rm->keys[bucket] == NULL) {
		newList = (CResList *)OperatorNew(sizeof(CResList));
		if (newList != NULL)
			CResListNode_Constructor_bin((CResListNode *)newList);
		rm->keys[bucket] = newList;

		newList = (CResList *)OperatorNew(sizeof(CResList));
		if (newList != NULL)
			CResListNode_Constructor_bin((CResListNode *)newList);
		rm->vals[bucket] = newList;
	}

	if (rm->flags == 1) {
		if (CResList_FindByValue(rm->keys[bucket], keyPtr, NULL, 1))
			return 0;
	}

	CResList_KeyInsert(rm->keys[bucket], keyPtr);
	CResList_InsertOrSetDataHint(rm->vals[bucket], valuePtr);
	rm->count++;
	return 1;
}

/*
 * 0x004645B0 - CHintManager::Erase
 *
 * Erases the entry pointed to by ctx and stores the iterator that follows
 * it into *output. The removed CHintItem is destroyed.
 */
static CSearchCtx *
CHintManager_Erase(CResManager *manager, CSearchCtx *output, CSearchCtx *ctx, int flag)
{
	CSearchCtx tempCtx;
	CSearchCtx eraseOutput;
	void *deletedItem;

	CSearchCtx_Constructor(&tempCtx);

	CResManager_EraseEntry_Hint(manager, &eraseOutput, ctx, &deletedItem, flag);
	CSearchCtx_Add(&tempCtx, &eraseOutput);

	if (deletedItem != NULL)
		CHintItem_ScalarDelete((CHintItem *)deletedItem, 1);

	CResManager_CreateBucket(output, &tempCtx);
	return output;
}

/*
 * 0x00464630 - CHintManager::Begin
 *
 * Stores into *output the iterator at the first entry of the hint manager.
 */
static CSearchCtx *
CHintManager_Begin(CResManager *manager, CSearchCtx *output)
{
	CResManager_BeginSearchHint(manager, output, 0, 1);
	return output;
}

/*
 * 0x00464650 - CHintManager::Next
 *
 * Stores into *output the iterator following ctx.
 */
static CSearchCtx *
CHintManager_Next(CResManager *manager, CSearchCtx *output, CSearchCtx *ctx)
{
	CResManager_NextEntry_Hint(manager, output, ctx, 1);
	return output;
}

/*
 * 0x00467815 - Static init wrapper
 *
 * Static initializer for the global g_SCommandManager.
 */
static __attribute__((unused)) void
StaticInit_CommandManager(void)
{
	SCommandManager_Constructor(&g_SCommandManager);
}

/*
 * 0x004722C7 - check_IsOrderGuard
 *
 * Checker callback for Script_isOrderGuard (0x00413A1A).
 * If CMobile_IsUsingOrderShield, return 1. Else if has "order" tag:
 * if WombatTimer type 0x13 exists, return 1; else remove "order" tag.
 */
static int
check_IsOrderGuard(CItem *ent)
{
	if (CMobile_IsUsingOrderShield(ent))
		return 1;
	if (CResourceEntity_HasTag(ent, "order", 0)) {
		if (CEntity_HasTimerEx(ent, 0x13, 0))
			return 1;
		CResourceEntity_DetachScript(ent, "order");
	}
	return 0;
}

/*
 * 0x0047235A - check_IsChaosGuard
 *
 * Checker callback for Script_isChaosGuard (0x00413A35).
 * If IsUsingChaosShield, return 1. Else if has "chaos" tag:
 * if WombatTimer type 0x12 exists, return 1; else remove "chaos" tag.
 */
static int
check_IsChaosGuard(CItem *ent)
{
	if (IsUsingChaosShield(ent))
		return 1;
	if (CResourceEntity_HasTag(ent, "chaos", 0)) {
		if (CEntity_HasTimerEx(ent, 0x12, 0))
			return 1;
		CResourceEntity_DetachScript(ent, "chaos");
	}
	return 0;
}

/*
 * 0x00472476 - check_IsUsingVirtueShield
 *
 * Checker callback for Script_isUsingVirtueShield (0x004139FF).
 * Returns CMobile_IsUsingOrderShield || IsUsingChaosShield.
 */
static int
check_IsUsingVirtueShield(CItem *ent)
{
	if (CMobile_IsUsingOrderShield(ent))
		return 1;
	if (IsUsingChaosShield(ent))
		return 1;
	return 0;
}

/*
 * 0x004724AE - check_IsVirtueGuard
 *
 * Checker callback for Script_isVirtueGuard (0x004139E4).
 * Returns IsOrderGuard || IsChaosGuard.
 */
static int
check_IsVirtueGuard(CItem *ent)
{
	if (check_IsOrderGuard(ent))
		return 1;
	if (check_IsChaosGuard(ent))
		return 1;
	return 0;
}

/*
 * 0x0047D4C0 - DispatchMultiBySerial
 *
 * Inner dispatch helper for SendMultiMessage. Unpacks the flat buffer
 * (serial(4), nameLen(4), msgName(nameLen), listCount(4), serialized list
 * data), deserializes the CList, looks up the entity by serial, and fires
 * event 0x16 with the caller serial, message name, "x" signature, and
 * deserialized list args.
 */
static void
DispatchMultiBySerial(uint32_t callerSerial, uint8_t *buf, int totalSize)
{
	uint32_t serial;
	CList list;
	int nameLen;
	char *msgName;
	int listCount;
	int listDataSize;
	char *listDataPtr;
	CItem *entity;

	memcpy(&serial, buf, 4);

	CList_Constructor(&list);

	memcpy(&nameLen, buf + 4, 4);

	msgName = (char *)OperatorNew(nameLen);
	memcpy(msgName, buf + 8, nameLen);

	memcpy(&listCount, buf + nameLen + 8, 4);

	listDataSize = totalSize - 8 - nameLen - 4;
	USED(listDataSize);
	listDataPtr = (char *)(buf + nameLen + 0xc);

	// Skip leading spaces
	while (listDataPtr != NULL && *listDataPtr == ' ')
		listDataPtr++;

	List_DeserializeFromBuf(listDataPtr, &list, listCount);

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity != NULL) {
		Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x16, callerSerial, msgName, "x", &list);
	}

	OperatorDelete(msgName);
	CList_Destructor(&list);
}

/*
 * 0x0047D601 - SendMultiMessage
 *
 * Serializes the message name and CList args into a flat buffer via
 * CDataBuffer + List_SerializeToBuf (0x004C488C), then calls
 * DispatchMultiBySerial (0x0047D4C0) which deserializes and fires
 * event 0x16 on the target entity. Buffer layout:
 *   [0x00] uint32_t serial
 *   [0x04] uint32_t nameLen (strlen(msgName) + 1)
 *   [0x08] char[nameLen] msgName (NUL-terminated)
 *   [0x08+nameLen] uint32_t listCount
 *   [0x0C+nameLen] char[] serialized list data (NUL-terminated)
 */
void
SendMultiMessage(uint32_t serial, uint32_t callerSerial, CString *msgName, intptr_t listArgs)
{
	CDataBuffer buf;
	int nameLen;
	int listCount;
	int dataLen;
	int totalSize;
	uint8_t *flatBuf;
	static const char nul = '\0';

	CDataBuffer_Constructor(&buf);

	nameLen = (int)strlen(CString_GetBuffer(msgName)) + 1;
	listCount = CList_GetCount((CList *)(intptr_t)listArgs);

	// Serialize list args to CDataBuffer (0x004C488C)
	List_SerializeToBuf(&buf, (CList *)(intptr_t)listArgs);

	// Append NUL separator
	CDataBuffer_Append(&buf, &nul, 1);

	dataLen = (int)strlen((char *)buf.data) + 1;

	totalSize = nameLen + dataLen + 0xc;
	flatBuf = (uint8_t *)OperatorNew(totalSize);

	memcpy(flatBuf, &serial, 4);
	memcpy(flatBuf + 4, &nameLen, 4);
	memcpy(flatBuf + 8, CString_GetBuffer(msgName), nameLen);
	memcpy(flatBuf + 8 + nameLen, &listCount, 4);
	memcpy(flatBuf + nameLen + 0xc, buf.data, dataLen);

	DispatchMultiBySerial(callerSerial, flatBuf, totalSize);

	OperatorDelete(flatBuf);
	CDataBuffer_Destructor(&buf);
}

/*
 * 0x0047D75E - DispatchMultiByLoc
 *
 * Inner dispatch helper for SendMultiMessageToLoc. Unpacks flat buffer
 * containing x(2), y(2), nameLen(4), msgName(nameLen), listCount(4),
 * serialized list data. Deserializes the CList, validates coordinates
 * via CBlockManager_IsValidCoord, walks the spatial grid block at
 * (x,y), collects entity serials matching the exact tile into a
 * CVector, and fires event 0x16 on each.
 */
static void
DispatchMultiByLoc(uint32_t callerSerial, uint8_t *buf, int totalSize)
{
	int16_t x, y;
	CList list;
	int nameLen;
	char *msgName;
	int listCount;
	int listDataSize;
	char *listDataPtr;
	CVector vec;
	char type;
	int blockIdx;
	CItem *cur;
	uintptr_t *iter;
	CItem *entity;

	memcpy(&x, buf, 2);
	memcpy(&y, buf + 2, 2);

	CList_Constructor(&list);

	memcpy(&nameLen, buf + 4, 4);

	msgName = (char *)OperatorNew(nameLen);
	memcpy(msgName, buf + 8, nameLen);

	memcpy(&listCount, buf + nameLen + 8, 4);

	listDataSize = totalSize - 8 - nameLen - 4;
	USED(listDataSize);
	listDataPtr = (char *)(buf + nameLen + 0xc);

	// Skip leading spaces
	while (listDataPtr != NULL && *listDataPtr == ' ')
		listDataPtr++;

	List_DeserializeFromBuf(listDataPtr, &list, listCount);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)x, (int)y))
		goto cleanup;

	type = 0;
	CVector_Constructor(&vec, &type);

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)x, (int)y, 0);
	if (blockIdx == -1)
		goto done_collect;

	cur = g_MapBlocks[blockIdx].itemHead;
	while (cur != NULL) {
		if ((int16_t)cur->resourceEntity.entity.location.x == x && (int16_t)cur->resourceEntity.entity.location.y == y) {
			CVector_PushBack(&vec, cur->serial);
		}
		cur = cur->spatialNext;
	}

done_collect:
	iter = (uintptr_t *)vec.begin;
	while (iter != (uintptr_t *)vec.end) {
		entity = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (entity != NULL) {
			Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x16, callerSerial, msgName, "x", &list);
		}
		iter++;
	}

	CVector_Destructor(&vec);

cleanup:
	OperatorDelete(msgName);
	CList_Destructor(&list);
}

/*
 * 0x0047D98E - DispatchMultiByRange
 *
 * Inner dispatch helper for SendMultiMessageToRange. Unpacks flat
 * buffer containing x(2), y(2), range(4), nameLen(4),
 * msgName(nameLen), listCount(4), serialized list data. Deserializes
 * the CList, iterates all (cx,cy) in [y-range..y+range] x
 * [x-range..x+range], walks the spatial grid block at each tile,
 * collects entity serials matching the exact tile into a CVector,
 * and fires event 0x16 on each.
 */
static void
DispatchMultiByRange(uint32_t callerSerial, uint8_t *buf, int totalSize)
{
	int16_t x, y;
	int range;
	CList list;
	int nameLen;
	char *msgName;
	int listCount;
	int listDataSize;
	char *listDataPtr;
	CVector vec;
	char type;
	int cx, cy;
	int blockIdx;
	CItem *cur;
	uintptr_t *iter;
	CItem *entity;

	memcpy(&x, buf, 2);
	memcpy(&y, buf + 2, 2);
	memcpy(&range, buf + 4, 4);

	buf += 4;

	CList_Constructor(&list);

	memcpy(&nameLen, buf + 4, 4);

	msgName = (char *)OperatorNew(nameLen);
	memcpy(msgName, buf + 8, nameLen);

	memcpy(&listCount, buf + nameLen + 8, 4);

	listDataSize = totalSize - 8 - nameLen - 4;
	USED(listDataSize);
	listDataPtr = (char *)(buf + nameLen + 0xc);

	// Skip leading spaces
	while (listDataPtr != NULL && *listDataPtr == ' ')
		listDataPtr++;

	List_DeserializeFromBuf(listDataPtr, &list, listCount);

	type = 0;
	CVector_Constructor(&vec, &type);

	for (cy = (int)y - range; cy <= (int)y + range; cy++) {
		for (cx = (int)x - range; cx <= (int)x + range; cx++) {
			if (!CBlockManager_IsValidCoord(&g_SpatialGrid, cx, cy))
				continue;

			blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, cx, cy, 0);
			if (blockIdx == -1)
				continue;

			cur = g_MapBlocks[blockIdx].itemHead;
			while (cur != NULL) {
				if ((int16_t)cur->resourceEntity.entity.location.x == cx && (int16_t)cur->resourceEntity.entity.location.y == cy) {
					CVector_PushBack(&vec, cur->serial);
				}
				cur = cur->spatialNext;
			}
		}
	}

	iter = (uintptr_t *)vec.begin;
	while (iter != (uintptr_t *)vec.end) {
		entity = CWorld_FindBySerial(g_World, (uint32_t)*iter);
		if (entity != NULL) {
			Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x16, callerSerial, msgName, "x", &list);
		}
		iter++;
	}

	OperatorDelete(msgName);
	CVector_Destructor(&vec);
	CList_Destructor(&list);
}

/*
 * 0x0047DC1E - SendMultiMessageToLoc
 *
 * Serializes the location, message name, and CList args into a flat
 * buffer via CDataBuffer + List_SerializeToBuf (0x004C488C), then calls
 * DispatchMultiByLoc (0x0047D75E) which deserializes and fires event
 * 0x16 on entities at the location. Buffer layout:
 *   [0x00] int16_t x
 *   [0x02] int16_t y
 *   [0x04] uint32_t nameLen
 *   [0x08] char[nameLen] msgName
 *   [0x08+nameLen] uint32_t listCount
 *   [0x0C+nameLen] char[] serialized list data
 */
static void
SendMultiMessageToLoc(CLocation *loc, uint32_t callerSerial, CString *msgName, intptr_t listArgs)
{
	CDataBuffer buf;
	int nameLen;
	int listCount;
	int dataLen;
	int totalSize;
	uint8_t *flatBuf;
	static const char nul = '\0';

	CDataBuffer_Constructor(&buf);

	nameLen = (int)strlen(CString_GetBuffer(msgName)) + 1;
	listCount = CList_GetCount((CList *)(intptr_t)listArgs);

	List_SerializeToBuf(&buf, (CList *)(intptr_t)listArgs);
	CDataBuffer_Append(&buf, &nul, 1);

	dataLen = (int)strlen((char *)buf.data) + 1;

	totalSize = nameLen + dataLen + 0xc;
	flatBuf = (uint8_t *)OperatorNew(totalSize);

	memcpy(flatBuf, loc, 2);
	memcpy(flatBuf + 2, (uint8_t *)loc + 2, 2);
	memcpy(flatBuf + 4, &nameLen, 4);
	memcpy(flatBuf + 8, CString_GetBuffer(msgName), nameLen);
	memcpy(flatBuf + 8 + nameLen, &listCount, 4);
	memcpy(flatBuf + nameLen + 0xc, buf.data, dataLen);

	DispatchMultiByLoc(callerSerial, flatBuf, totalSize);

	OperatorDelete(flatBuf);
	CDataBuffer_Destructor(&buf);
}

/*
 * 0x0047DD93 - SendMultiMessageToRange
 *
 * Serializes the location, range, message name, and CList args into a
 * flat buffer via CDataBuffer + List_SerializeToBuf (0x004C488C), then
 * calls DispatchMultiByRange (0x0047D98E) which deserializes and fires
 * event 0x16 on entities within range. Buffer layout:
 *   [0x00] int16_t x
 *   [0x02] int16_t y
 *   [0x04] int32_t range
 *   [0x08] uint32_t nameLen
 *   [0x0C] char[nameLen] msgName
 *   [0x0C+nameLen] uint32_t listCount
 *   [0x10+nameLen] char[] serialized list data
 */
static void
SendMultiMessageToRange(CLocation *loc, int range, uint32_t callerSerial, CString *msgName, intptr_t listArgs)
{
	CDataBuffer buf;
	int nameLen;
	int listCount;
	int dataLen;
	int totalSize;
	uint8_t *flatBuf;
	static const char nul = '\0';

	CDataBuffer_Constructor(&buf);

	nameLen = (int)strlen(CString_GetBuffer(msgName)) + 1;
	listCount = CList_GetCount((CList *)(intptr_t)listArgs);

	List_SerializeToBuf(&buf, (CList *)(intptr_t)listArgs);
	CDataBuffer_Append(&buf, &nul, 1);

	dataLen = (int)strlen((char *)buf.data) + 1;

	totalSize = nameLen + dataLen + 0x10;
	flatBuf = (uint8_t *)OperatorNew(totalSize);

	memcpy(flatBuf, loc, 2);
	memcpy(flatBuf + 2, (uint8_t *)loc + 2, 2);
	memcpy(flatBuf + 4, &range, 4);
	memcpy(flatBuf + 8, &nameLen, 4);
	memcpy(flatBuf + 0xc, CString_GetBuffer(msgName), nameLen);
	memcpy(flatBuf + 0xc + nameLen, &listCount, 4);
	memcpy(flatBuf + nameLen + 0x10, buf.data, dataLen);

	DispatchMultiByRange(callerSerial, flatBuf, totalSize);

	OperatorDelete(flatBuf);
	CDataBuffer_Destructor(&buf);
}

/*
 * 0x00489522 - IsNearCampfire (inner function for isInCamp)
 *
 * Queries the spatial grid at the entity's location with radius 8 (capped
 * at 256 block results) and walks the itemHead chain looking for bodyType
 * 0x0B17 (campfire). Returns 1 on a match, 0 otherwise.
 */
int
IsNearCampfire(CItem *ent)
{
	int blockIds[256];
	int i;
	CItem *cur;

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, &ent->resourceEntity.entity.location, 8, blockIds, 256);
	for (i = 0; blockIds[i] != -1; i++) {
		cur = g_SpatialGrid.cells[blockIds[i]].itemHead;
		while (cur != NULL) {
			if ((CEntity_GetBodyType(cur) & 0xFFFF) == 0x0B17)
				return 1;
			cur = cur->spatialNext;
		}
	}
	return 0;
}

/*
 * 0x00490C6D - CItem::GetSortKey
 *
 * Thiscall -> CItem_GetTiledataQuantity.
 */
static int
CItem_GetSortKey(CItem *ent)
{
	return CItem_GetTiledataQuantity(ent);
}

/*
 * 0x00491400 - CheckWordList
 *
 * Returns 1 when any word in the NULL-terminated wordList appears
 * in text (case-insensitive substring match).
 *
 * FIXED: the binary reuses a single stack slot for the loop index
 * and the search result, so a miss leaves the loop reading
 * wordList[-1] (an out-of-bounds underrun); the C version uses a
 * separate loop counter.
 */
static int
CheckWordList(const char *text, const char **wordList)
{
	int i;

	for (i = 0; wordList[i] != NULL; i++) {
		if (BM_CaseInsensitiveSearch(wordList[i], text) != -1)
			return 1;
	}
	return 0;
}

static const char *g_ProfanityList1[] = { // 0x0061EB18
	"ass", "clit", "clitoris", "cock", "cocksucker", "cum", "cunnilingus", "chink", "chinc", "dildo", "dyke", "felatio", "lesbo", "lezbo", "piss", "prick", "spic", "tit",
	"kike", "kyke", "wop", "chigaboo", "jigaboo", NULL
};

static const char *g_ProfanityList2[] = { // 0x0061EAC0
	"fuck", "shit", "twat", "cunt", "snatch", "pussy", "dick", "asshole", "bitch", "blowjob", "fag", "goddamn", "jackoff", "jerkoff", "jism", "jiz", "kunt", "klit", "nigger",
	"nigga", "penis", NULL
};

static const char *g_ProfanityList3[] = { // 0x0061EAA8
	"Origin", "OSI", "invulnerable", "frozen", "squelched", NULL
};

static const char *g_ReservedTitles[] = { // 0x0061EA20
	"Adept", "Alchemist", "Apprentice", "Archer", "Assassin", "Bard", "Beggar", "Blackthorn", "Blackthorne", "Bowyer", "British", "Carpenter", "Chef", "Expert", "Fisherman",
	"Grandmaster", "Healer", "Journeyman", "LB", "Mage", "Master", "Medium", "Merchant", "Neophyte", "Novice", "Ranger", "Rogue", "Scholar", "Smith", "Tailor", "Tinker",
	"Warrior", NULL
};

/*
 * 0x00491445 - CheckProfanity
 *
 * Returns 1 when text contains any word from the three profanity
 * / reserved-word lists.
 */
static int
CheckProfanity(const char *text)
{
	if (CheckWordList(text, g_ProfanityList1))
		return 1;
	if (CheckWordList(text, g_ProfanityList2))
		return 1;
	if (CheckWordList(text, g_ProfanityList3))
		return 1;
	return 0;
}

/*
 * 0x0049149F - IsNameSeparator
 *
 * Returns 1 if character is a valid name separator: '-', '.', ' ', or '\''.
 */
static int
IsNameSeparator(char c)
{
	if (c == '-')
		return 1;
	if (c == '.')
		return 1;
	if (c == ' ')
		return 1;
	if (c == '\'')
		return 1;
	return 0;
}

/*
 * 0x004914E6 - IsInvalidNameChar
 *
 * Returns 1 if character is invalid for a name (not A-Z, a-z, or separator).
 */
static __attribute__((unused)) int
IsInvalidNameChar(char c)
{
	if (c >= 'A' && c <= 'Z')
		return 0;
	if (c >= 'a' && c <= 'z')
		return 0;
	if (IsNameSeparator(c))
		return 0;
	return 1;
}

/*
 * 0x00491530 - IsInvalidName
 *
 * Validates a player name, returning 1 when it must be rejected. The name
 * has to start with a letter, carry no invalid character and no two
 * adjacent separators, and may not begin with "Lord", "Lady", "GM",
 * "Counselor" or "Seer". It is then rejected when a reserved title or a
 * word from either whole-word profanity list appears in it delimited by
 * separators, and when any word from the second profanity list appears at
 * all, whole word or not.
 *
 * Nothing in the binary calls this function.
 */
static __attribute__((unused)) int
IsInvalidName(const char *name)
{
	int i, idx;

	if ((*name < 'a' || *name > 'z') && (*name < 'A' || *name > 'Z'))
		return 1;

	for (i = 0; i < (int)strlen(name); i++) {
		if (IsInvalidNameChar(name[i]))
			return 1;
		if (IsNameSeparator(name[i]) && i < (int)strlen(name) - 1 && IsNameSeparator(name[i + 1]))
			return 1;
	}

	if (BM_CaseInsensitiveSearch("Lord", name) == 0)
		return 1;
	if (BM_CaseInsensitiveSearch("Lady", name) == 0)
		return 1;
	if (BM_CaseInsensitiveSearch("GM", name) == 0)
		return 1;
	if (BM_CaseInsensitiveSearch("Counselor", name) == 0)
		return 1;
	if (BM_CaseInsensitiveSearch("Seer", name) == 0)
		return 1;

	for (i = 0; g_ReservedTitles[i] != NULL; i++) {
		idx = BM_CaseInsensitiveSearch(g_ReservedTitles[i], name);
		if (idx == -1)
			continue;
		if (idx != 0 && !IsNameSeparator(name[idx - 1]))
			continue;
		if (idx + (int)strlen(g_ReservedTitles[i]) == (int)strlen(name))
			return 1;
		if (IsNameSeparator(name[idx + (int)strlen(g_ReservedTitles[i])]))
			return 1;
	}

	for (i = 0; g_ProfanityList1[i] != NULL; i++) {
		idx = BM_CaseInsensitiveSearch(g_ProfanityList1[i], name);
		if (idx == -1)
			continue;
		if (idx != 0 && !IsNameSeparator(name[idx - 1]))
			continue;
		if (idx + (int)strlen(g_ProfanityList1[i]) == (int)strlen(name))
			return 1;
		if (IsNameSeparator(name[idx + (int)strlen(g_ProfanityList1[i])]))
			return 1;
	}

	for (i = 0; g_ProfanityList2[i] != NULL; i++) {
		idx = BM_CaseInsensitiveSearch(g_ProfanityList2[i], name);
		if (idx != -1)
			return 1;
	}

	for (i = 0; g_ProfanityList3[i] != NULL; i++) {
		idx = BM_CaseInsensitiveSearch(g_ProfanityList3[i], name);
		if (idx == -1)
			continue;
		if (idx != 0 && !IsNameSeparator(name[idx - 1]))
			continue;
		if (idx + (int)strlen(g_ProfanityList3[i]) == (int)strlen(name))
			return 1;
		if (IsNameSeparator(name[idx + (int)strlen(g_ProfanityList3[i])]))
			return 1;
	}

	return 0;
}

/*
 * 0x004A53A8 - isInArea
 *
 * CSearchCtx iteration on flag-selected CResManager (g_RegionRM for
 * flag=0, g_RegionByNameRM for flag=1). For each region, checks
 * isCoordInside first, then name prefix (strnicmp on region->name at
 * CRegion+0x04). Returns 1 on first match, 0 if none.
 */
int
Script_isInArea(uintptr_t namePrefix_str, uint32_t *loc, int flag)
{
	CSearchCtx ctx, temp;
	CResManager *rm;
	int prefixLen;

	CSearchCtx_Constructor(&ctx);

	if (flag == 0)
		rm = &g_RegionRM;
	else
		rm = &g_RegionByNameRM;

	CSearchCtx_Add(&ctx, CResManager_BeginIterWrapper(rm, &temp));

	prefixLen = strlen(CString_GetCStr((CString *)(uintptr_t)namePrefix_str));

	while (CSearchCtx_Find(&ctx)) {
		CRegion *region = *(CRegion **)CResManager_GetResultCtx(rm, &ctx);

		if (CRegion_isCoordInside(region, (CLocation *)loc)) {
			if (strncasecmp(region->name, CString_GetCStr((CString *)(uintptr_t)namePrefix_str), prefixLen) == 0)
				return 1;
		}

		CSearchCtx_Add(&ctx, CResManager_NextIterWrapper(rm, &temp, &ctx));
	}
	return 0;
}

/*
 * 0x004A546A - findClosestArea
 *
 * CSearchCtx iteration on flag-selected CResManager (g_RegionRM for
 * flag=0, g_RegionByNameRM for flag=1). Checks name prefix first,
 * then computes 3D squared distance to region center. Stores closest
 * match center in resultLoc and description (name2) in resultName.
 * Returns 1 if any match found, 0 otherwise.
 *
 * Overflow guard: skips regions where any squared component >= 0x04000000.
 */
int
Script_findClosestArea(uintptr_t resultName_str, uint32_t *resultLoc, uintptr_t namePrefix_str, uint32_t *fromLoc, int flag)
{
	CSearchCtx ctx, temp;
	CResManager *rm;
	int prefixLen;
	int16_t *from;
	int bestDist;

	CSearchCtx_Constructor(&ctx);

	if (flag == 0)
		rm = &g_RegionRM;
	else
		rm = &g_RegionByNameRM;

	CSearchCtx_Add(&ctx, CResManager_BeginIterWrapper(rm, &temp));

	prefixLen = strlen(CString_GetCStr((CString *)(uintptr_t)namePrefix_str));
	bestDist = 0x7FFFFFFF;
	from = (int16_t *)fromLoc;

	while (CSearchCtx_Find(&ctx)) {
		CRegion *region = *(CRegion **)CResManager_GetResultCtx(rm, &ctx);
		int centerX, centerY, centerZ;
		int dx, dy, dz;
		int dist;

		if (strncasecmp(region->name, CString_GetCStr((CString *)(uintptr_t)namePrefix_str), prefixLen) != 0)
			goto next;

		centerX = (int)region->x + (int)region->width / 2;
		centerY = (int)region->y + (int)region->height / 2;
		centerZ = (int)region->zMin + ((int)region->zMax - (int)region->zMin) / 2;

		dx = centerX - (int)from[0];
		dy = centerY - (int)from[1];
		dz = centerZ - (int)from[2];

		dx = dx * dx;
		dy = dy * dy;
		dz = dz * dz;

		if (dx >= 0x4000000)
			goto next;
		if (dy >= 0x4000000)
			goto next;
		if (dz >= 0x4000000)
			goto next;

		dist = dx + dy + dz;
		if (dist >= bestDist)
			goto next;

		bestDist = dist;
		CLocation_Set((CLocation *)resultLoc, (int16_t)centerX, (int16_t)centerY, (int16_t)centerZ);
		CString_AssignCStr((CString *)(uintptr_t)resultName_str, region->name2);

next:
		CSearchCtx_Add(&ctx, CResManager_NextIterWrapper(rm, &temp, &ctx));
	}

	return (bestDist != 0x7FFFFFFF) ? 1 : 0;
}

/*
 * VT_FindItemByName - vtable[0x164] dispatch.
 * CMobile::FindItemByType: searches mobile's equipment/contents
 * for an item matching the given name within a container.
 * Returns CItem* or NULL.
 */
static CItem *
VT_FindItemByName(CItem *mob, CString *name, uint32_t containerSerial)
{
	return ((CItem * (*)(void *, void *, uint32_t)) VT_FN(mob, VT_FIND_IN_TAG_LIST))(mob, name, containerSerial);
}

/*
 * String / Unicode String Handlers
 *
 * CString layout (16 bytes):
 *   +0x00 char *data      - heap-allocated NUL-terminated buffer
 *   +0x04 int   length    - strlen(data)
 *   +0x08 int   refCount  - always 1 in practice
 *   +0x0C int   capacity  - allocated buffer size (in chars)
 *
 * CUString is identical but stores unsigned short (wchar16) data.
 *
 * Binary helper addresses:
 *   0x004D3397 - CString::GetBuffer()       returns *(char **)this
 *   0x00401450 - CString::GetLength()       returns this->length
 *   0x004D33A7 - CString::Assign(const char *)   CString_AssignInternal
 *   0x004D3481 - CString::operator+=(const CString &)
 *   0x004D3289 - CString::ConcatCStr(const char *) internal concat
 */

/*
 * Game Clock Accessors (0x004150C6..0x0041510B)
 *
 * Read global game time variables. Each function is a simple global
 * read with no arguments. The game time is stored in:
 *   0x006482B8 = month, 0x006482BC = week, 0x006482C0 = day,
 *   0x006482C4 = hour,  0x006482C8 = minute, 0x006482CC = seconds,
 *   0x006482D0 = year.
 */
/*
 * Custom - Script_strContains
 *
 * Case-insensitive substring search. Returns 1 if needle is found
 * in haystack, 0 otherwise. Used by the kwdteleporter script to
 * match speech keywords.
 */
int
Script_strContains(CString *haystack, CString *needle)
{
	const char *h = CString_GetBuffer(haystack);
	const char *n = CString_GetBuffer(needle);
	int hlen = strlen(h);
	int nlen = strlen(n);
	int i;

	if (nlen == 0)
		return 1;
	if (nlen > hlen)
		return 0;
	for (i = 0; i <= hlen - nlen; i++) {
		if (strncasecmp(h + i, n, nlen) == 0)
			return 1;
	}
	return 0;
}

/*
 * Custom - Wombat_ShutdownArrays
 *
 * Server-shutdown cleanup walker. Drains g_WombatArrays by repeatedly
 * popping the leftmost entry and dispatching it through ArrayDelete,
 * which now releases both the data buffer (via CArray_Free) and the
 * WombatArray struct. No binary equivalent: the binary's process-exit
 * teardown leaks the map's values. Purpose is to keep the valgrind
 * shutdown report clean so real leaks remain visible. Uses size-loop
 * rather than tree iteration because std::map's tree iterator
 * increment is a separate primitive that the demo binary instantiates
 * only for its other map specialisations.
 */
void
Wombat_ShutdownArrays(void)
{
	StdPtrNode *iter;
	uintptr_t *pair;
	int id;

	while (g_WombatArrays.size > 0) {
		StdMap_Begin(&g_WombatArrays, &iter);
		pair = (uintptr_t *)StdTreeIter_Deref(&iter);
		id = (int)pair[0];
		ArrayDelete(id);
	}
}

/*
 * Helper - EventParamBlock_BuildFromEventParams
 *
 * Converts EventParam[] array to a binary-format EventParamBlock.
 * Bridges ExtractEventParams (custom) with ExecuteTrigger (binary).
 */
void
EventParamBlock_BuildFromEventParams(EventParamBlock *pb, EventParam *params, int numParams, CFuncScope *trigScope)
{
	CNamedScopeEntry *entries;
	int i;

	entries = (CNamedScopeEntry *)trigScope->namedScope.entries;
	for (i = 0; i < numParams && i < trigScope->namedScope.count; i++) {
		int typeId = entries[i].typeId;

		if (typeId == WTYPE_STRING && params[i].sval != NULL)
			EventParamBlock_AddParam(pb, WTYPE_STRING, (uintptr_t)params[i].sval);
		else if (typeId == WTYPE_LOC) {
			// 6-byte location: 4 bytes (x,y) + 2 bytes (z)
			memcpy(pb->data + pb->byteCount, &params[i].ival, 4);
			*(short *)(pb->data + pb->byteCount + 4) = (short)params[i].ival2;
			pb->byteCount += 8; // aligned to 4
		} else if (typeId == WTYPE_LIST) {
			// The block's listVec takes ownership of the CList that
			// ExtractEventParams allocated; zero ival so DispatchEvent
			// does not also free it (the param block destructor will).
			EventParamBlock_AddParam(pb, WTYPE_LIST, params[i].ival);
			params[i].ival = 0;
		} else
			EventParamBlock_AddParam(pb, typeId, params[i].ival);
	}
}
