/*
 * NPC template instantiation.
 *
 * Turns a CTemplate entry into a live CNPC: creates the mobile, rolls
 * its stats, attaches skills and equipment, and runs the template's
 * on-spawn Wombat trigger.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "dat.h"

#include "container.h"
#include "egg.h"
#include "filemanager.h"
#include "io.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "random.h"
#include "region.h"
#include "shopkeeper.h"
#include "template.h"
#include "utils.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static void CTemplate_Destructor(NPCTemplate *tmpl); // 0x004C2A10
static CSearchCtx *CTemplateManager_FindFieldByLabel(CResManager *rm, CSearchCtx *output, CString *label); // 0x004C1B60
static CResListNode **CStringPairList_AddWeighted(CStringPairList *sl, CResListNode **output, int weight, int direction); // 0x004C1220
static CResListNode **CStringPairList_GetWeightedRandom(CStringPairList *sl, CResListNode **output); // 0x004C10C0
static NPCTemplate *CTemplateData_Constructor(NPCTemplate *this); // 0x004C05C0
static int CTemplateManager_ValidateVendorStock(CItem *vendor); // 0x004C01DE
static int CTemplateManager_ParseDelimitedField(const char *src, char *dst, int maxlen); // 0x004C013E
static void CTemplateManager_SetupHumanNPC(CItem *npc, CLocation *loc); // 0x004BFC4C
static void CTemplateManager_Destructor(CTemplateManager *this); // 0x004BF7B8
static void CTemplateManager_Constructor(CTemplateManager *this); // 0x004BF71F
static int CTemplateManager_LoadTemplates(void); // 0x004BE297
static CItem *CTemplateManager_CreateEntity(uint16_t templateId, CLocation *loc, int *amount, int restockFlag, CItem *existingMob, int depth); // 0x004BD7C6
static void Template_LoadNamedValue(CMobile *mob, char *dataCursor); // 0x004BD5DF
static void CTemplateManager_AppendStringList(CStringList *list, char *data); // 0x004BD38C
static int CTemplateManager_CreateMobOrItem(CItem **out, const char *subtypeBuf, uint16_t bodyType, CLocation *loc); // 0x004BD165
static int CTemplateManager_SetTypeOnEntity(CItem *entity, const char *subtypeBuf, uint16_t bodyType, CLocation *loc); // 0x004BD04D
static void CTemplateManager_ParseStamina(CItem *entity, char *data); // 0x004BCEA7
static void CTemplateManager_ParseStrength(CItem *entity, char *data); // 0x004BCE58
static void CTemplateManager_ParseKarma(CItem *entity, char *data); // 0x004BCE09
static void CTemplateManager_ParseFame(CItem *entity, char *data); // 0x004BCDBA
static void CTemplateManager_ParseNotoriety(CItem *entity, char *data); // 0x004BCD6C
static void CTemplateManager_ParseMana(CItem *entity, char *data); // 0x004BCC8B
static void CTemplateManager_ParseIntelligence(CItem *entity, char *data); // 0x004BCC3C
static void CTemplateManager_ParseHueColor(CItem *entity, char *data, int *colorState, int isPartialHue); // 0x004BC7B9
static void CTemplateManager_ParseHitPoints(CItem *entity, char *data); // 0x004BC6D8
static void CTemplateManager_ParseDexterity(CItem *entity, char *data); // 0x004BC686
static void CTemplateManager_ParseAttitude(CItem *entity, char *data); // 0x004BC635
static uint8_t CTemplateManager_ParseDamageType(const char *name); // 0x004BC4B1
static void CTemplateManager_naturalwc(CItem *entity, char *data); // 0x004BC459
static void CTemplateManager_ParseNaturalAC(CItem *entity, const char *data); // 0x004BC41B
static int CTemplateManager_ParseQuantity(CItem *entity, const char *data); // 0x004BC3CF
static void CTemplateManager_ParseQuality(CItem *entity, const char *data); // 0x004BC3A9
static int CTemplateManager_AttachMulti(CItem *entity, CLocation *loc, const char *data); // 0x004BC2CF
static void CTemplateManager_ParseMoveType(CItem *entity, const char *data); // 0x004BC293
static void CTemplateManager_ParseScript(CItem *entity, char *data); // 0x004BC1E9
static void CTemplateManager_AddConvoTitle(CItem *npc, const char *title); // 0x004BC153
static void CTemplateManager_ParseSfx(CItem *entity, const char *data, int fieldType); // 0x004BC081
static char *CTemplateManager_ParseBracedDefineList(char *dataCursor, CStringList *list); // 0x004BA9B6
static void CTemplateManager_LoadObjVar(CItem *entity, char *data); // 0x004BA865
static void CTemplate_LoadSkillNameStr(CMobile *mob, char *dataCursor); // 0x004BA7D6
static void CTemplateManager_ParseResource(NPCTemplate *tmpl, char *data); // 0x004BA62A
static int CTemplateManager_ResolveDefines(CString *input, CString *output); // 0x004BA44B
static int CTemplateManager_LoadDefines(void); // 0x004BA2AA
static void CTemplateManager_ClearDefines(CTemplateManager *this); // 0x004BA291
static void CTemplateManager_ClearResManager2(CTemplateManager *this); // 0x004B9C4F
static void CTemplate_LoadSkillName(CMobile *mob, char *dataCursor); // 0x004B9B77
static void CTemplateResManager_Destructor(CTemplateResManager *rm); // 0x004752FA
static void CTemplateResManager_Constructor(CTemplateResManager *rm); // 0x00475287

/*
 * 0x00475287 - CTemplateResManager::CTemplateResManager
 *
 * Constructs the two CResManagers held by CTemplateResManager.
 */
static __attribute__((unused)) void
CTemplateResManager_Constructor(CTemplateResManager *rm)
{
	CResManager_Constructor(&rm->primary, 1);
	CResManager_Constructor(&rm->secondary, 1);
}
/*
 * 0x004752FA - CTemplateResManager::~CTemplateResManager
 *
 * Destroys the two CResManagers in reverse construction order.
 */
static __attribute__((unused)) void
CTemplateResManager_Destructor(CTemplateResManager *rm)
{
	CResManager_ClearMultiB_Thunk(&rm->secondary);
	CResManager_ClearMultiA_Thunk(&rm->primary);
}

/*
 * 0x004B9B77 - CTemplate::LoadSkillName
 *
 * Parses a "sex" template value (MALE/FEMALE/OTHER/RANDOM) and sets
 * the mob's sex field. Defaults to OTHER on no match.
 */
static void
CTemplate_LoadSkillName(CMobile *mob, char *dataCursor)
{
	char buf[128];

	if (!VT_IsMobile((CItem *)mob))
		return;

	dataCursor = GetValue(dataCursor, buf);
	USED(dataCursor);

	if (strcasecmp(buf, "MALE") == 0) {
		mob->sex = 0;
	} else if (strcasecmp(buf, "FEMALE") == 0) {
		mob->sex = 1;
	} else if (strcasecmp(buf, "OTHER") == 0) {
		mob->sex = 2;
	} else if (strcasecmp(buf, "RANDOM") == 0) {
		mob->sex = (rand() & 1) ? 0 : 1;
	} else {
		mob->sex = 2;
	}
}

/*
 * 0x004B9C4F - CTemplateManager::ClearResManager2
 *
 * Clears the name table CResManager.
 */
static void
CTemplateManager_ClearResManager2(CTemplateManager *this)
{
	USED(this);
	CResManager_Clear_NameTable(&g_NameTableRM);
}

/*
 * 0x004B9C68 - CTemplateManager::LoadNameTable
 *
 * Loads "namestable.dat" (or split "names.%d" files), parsing
 * male/female/other NPC name entries into the name table CResManager.
 */
int
CTemplateManager_LoadNameTable(void)
{
	FILE *mainFile;
	FILE *currentFile;
	char *line;
	char buf[128];
	char filename[256];
	int isMultiFile;
	int nameTableIdx;
	int maxEntries;
	CNameEntry *nameEntry;

	isMultiFile = 0;
	mainFile = FileManager_OpenByType(0x32, "namestable.dat", "r");
	if (mainFile == NULL) {
		isMultiFile = 1;
		mainFile = FileManager_OpenByType(0x32, "namestable.dat", "w");
	}

	nameTableIdx = 0;
	maxEntries = 0x1000;
	currentFile = mainFile;

	while (nameTableIdx < maxEntries) {
		if (isMultiFile) {
			sprintf(filename, "names.%d", nameTableIdx);
			currentFile = FileManager_OpenByType(0x32, filename, "r");
			if (currentFile == NULL)
				goto advance;
		} else {
			line = GetLine(currentFile);
			if (line == NULL)
				break;
			nameTableIdx = atoi(line);
		}

		nameEntry = (CNameEntry *)OperatorNew(sizeof(CNameEntry));
		if (nameEntry != NULL)
			CResManager_Constructor_Templates((CResManager *)nameEntry);

		while (1) {
			CString tmpStr;

			line = GetLine(currentFile);
			if (line == NULL)
				break;

			line = GetValue(line, buf);

			while (*line != '\0' && isspace((unsigned char)*line))
				line++;

			if (strcasecmp(buf, "male") == 0) {
				CString_Constructor(&tmpStr, line);
				CResList_InsertTailStr(&nameEntry->lists[0], &tmpStr);
				CString_Destructor(&tmpStr);
			} else if (strcasecmp(buf, "female") == 0) {
				CString_Constructor(&tmpStr, line);
				CResList_InsertTailStr(&nameEntry->lists[1], &tmpStr);
				CString_Destructor(&tmpStr);
			} else if (strcasecmp(buf, "other") == 0) {
				CString_Constructor(&tmpStr, line);
				CResList_InsertTailStr(&nameEntry->lists[2], &tmpStr);
				CString_Destructor(&tmpStr);
			}
		}

		if (isMultiFile)
			fclose_ServerSide(currentFile);

		{
			uint32_t key = (uint32_t)nameTableIdx;
			CResManager_InsertIntEntryG(&g_NameTableRM, &key, nameEntry);
		}

advance:
		nameTableIdx++;
	}

	fclose_ServerSide(mainFile);
	return 0;
}

/*
 * 0x004B9FE3 - CTemplateManager::setRealNameFromTemplate
 *
 * Assigns the mob a random name from the name table entry matching
 * the template's name table ID and the mob's sex.
 */
void
CTemplateManager_SetRealNameFromTemplate(CItem *entity, char *dataCursor)
{
	CMobile *mob;
	char buf[128];
	int nameTableIdx;
	int genderIdx;
	CNameEntry *entry;
	CResList *genderList;
	int count;
	int randIdx;
	CResListNode *iter;

	if (!VT_IsMobile(entity))
		return;

	mob = (CMobile *)entity;

	dataCursor = GetValue(dataCursor, buf);
	USED(dataCursor);
	nameTableIdx = atoi(buf);

	switch (mob->sex) {
	case 0:
		genderIdx = 0;
		break;
	case 1:
		genderIdx = 1;
		break;
	case 2:
		genderIdx = 2;
		break;
	default:
		genderIdx = -1;
		break;
	}

	if (genderIdx == -1) {
		CMobile_SetName(mob, "error");
		return;
	}

	entry = (CNameEntry *)CResManager_FindInt(&g_NameTableRM, (uint32_t)nameTableIdx);
	if (entry == NULL) {
		CMobile_SetName(mob, "error");
		return;
	}

	genderList = &entry->lists[genderIdx];
	count = genderList->count;
	if (count < 1) {
		CMobile_SetName(mob, "error");
		return;
	}

	randIdx = GetRandomRange(0, count - 1);

	iter = CResList_Begin(genderList);

	while (randIdx > 0) {
		if (!CResList_IsValid(genderList, iter)) {
			CMobile_SetName(mob, "error");
			return;
		}
		iter = CResList_Next(genderList, iter);
		randIdx--;
	}

	{
		CString *nameData = (CString *)CResList_GetData(genderList, iter);
		CMobile_SetName(mob, CString_GetBuffer(nameData));
	}
}

/*
 * 0x004BA291 - CTemplateManager::ClearDefines
 *
 * Clears the defines CResManager.
 */
static void
CTemplateManager_ClearDefines(CTemplateManager *this)
{
	USED(this);
	CResManager_Clear_Defines(&g_DefinesRM);
}

/*
 * 0x004BA2AA - CTemplateManager::LoadDefines
 *
 * Loads the "defines" file, parsing each line into a name -> value
 * entry in the defines CResManager.
 */
static int
CTemplateManager_LoadDefines(void)
{
	FILE *fp;
	char lineBuf[1024];
	char tokenBuf[2056];
	char valueBuf[1024];
	char *cursor;
	int len;
	CString key;
	CString value;

	CString_DefaultConstructor(&key);
	CString_DefaultConstructor(&value);

	fp = FileManager_OpenByType(0x32, "defines", "r");
	if (fp == NULL)
		goto cleanup;

	while (!feof_ServerSide(fp)) {
		fgets_ServerSide(lineBuf, 0x3FF, fp);
		cursor = lineBuf;

		cursor = GetValue(cursor, tokenBuf);
		strcpy(valueBuf, cursor);

		len = (int)strlen(valueBuf);
		if (len > 0 && valueBuf[len - 1] == '\n')
			valueBuf[len - 1] = '\0';

		CString_AssignCStr(&key, tokenBuf);
		CString_AssignCStr(&value, valueBuf);
		CResManager_InsertStrEntry_Defines(&g_DefinesRM, &key, &value);
	}

	fclose_ServerSide(fp);

cleanup:
	CString_Destructor(&value);
	CString_Destructor(&key);
	return 0;
}

/*
 * 0x004BA44B - CTemplateManager::ResolveDefines
 *
 * Tokenizes input on whitespace and substitutes each token against the
 * defines hash, writing the resolved string to output. Returns nonzero
 * if any substitution occurred.
 */
static int
CTemplateManager_ResolveDefines(CString *input, CString *output)
{
	int firstToken;
	int tokenStart;
	CString token;
	CString delimiters;
	CString emptyStr;
	CString newlineStr;
	int resolved;
	CSearchCtx ctx;
	int isFirst;

	firstToken = 1;
	tokenStart = 0;
	CString_DefaultConstructor(&token);
	CString_Constructor(&delimiters, " \t\n");
	CString_Constructor(&emptyStr, "");
	CString_Constructor(&newlineStr, "\n");
	resolved = 0;
	CSearchCtx_Constructor(&ctx);
	isFirst = 1;

	CString_Clear(output);

	while (1) {
		CSearchCtx findCtx;

		if (!CString_Tokenize(input, &tokenStart, &firstToken, &token, &delimiters, &emptyStr, 1))
			break;

		if (!isFirst)
			CString_AppendCStr(output, " ");
		else
			isFirst = 0;

		CResManager_FindByStr_Defines(&g_DefinesRM, &findCtx, &token, 1);
		CSearchCtx_Add(&ctx, &findCtx);

		if (CSearchCtx_Find(&ctx)) {
			CString *val = (CString *)CResManager_GetResultCtx(&g_DefinesRM, &ctx);
			CString_ConcatCString(output, val);
			resolved = 1;
		} else {
			CString_ConcatCString(output, &token);
		}
	}

	CString_Destructor(&newlineStr);
	CString_Destructor(&emptyStr);
	CString_Destructor(&delimiters);
	CString_Destructor(&token);

	return resolved;
}

/*
 * 0x004BA62A - CTemplateManager::ParseResource
 *
 * Parses a "resource" template field (TYPE VAL1 VAL2 RESOURCE_NAME)
 * and prepends a CResourceNode to the template's resource list.
 * Resource type: food=0, shelter=1, desire=2, production=3.
 */
static void
CTemplateManager_ParseResource(NPCTemplate *tmpl, char *data)
{
	char tokenBuf[16];
	int resType;
	CResourceNode *node;
	int val1;
	int val2;
	CResourceType *rt;

	data = GetValue(data, tokenBuf);

	if (strcasecmp(tokenBuf, "food") == 0)
		resType = 0;
	else if (strcasecmp(tokenBuf, "shelter") == 0)
		resType = 1;
	else if (strcasecmp(tokenBuf, "desire") == 0)
		resType = 2;
	else if (strcasecmp(tokenBuf, "production") == 0)
		resType = 3;
	else
		return;

	node = ResourceNode_AllocFromPool();

	node->type = (uint8_t)resType;

	data = GetValue(data, tokenBuf);
	val1 = atoi(tokenBuf);
	node->value1 = val1;

	data = GetValue(data, tokenBuf);
	val2 = atoi(tokenBuf);
	node->value2 = val2;

	node->value3 = node->value1;

	while (*data != '\0' && isspace((unsigned char)*data))
		data++;

	// Binary passes the remaining string directly, not via GetValue.
	rt = CResourceTypeManager_FindByName(data);
	if (rt == NULL) {
		ResourceNode_ReturnToPool(node);
		return;
	}

	node->id = (uint16_t)rt->typeId;

	node->next = tmpl->resourceNodes;
	if (tmpl->resourceNodes != NULL)
		tmpl->resourceNodes->prev = node;
	node->prev = NULL;
	tmpl->resourceNodes = node;
}

/*
 * 0x004BA7D6 - CTemplate::LoadSkillNameStr
 *
 * Parses a "sk"/"skill" template field (skill ID followed by a dice
 * expression) and stores the rolled value in mob->skills.
 */
static void
CTemplate_LoadSkillNameStr(CMobile *mob, char *dataCursor)
{
	char buf[128];
	uint16_t skillId;
	int16_t rollVal;

	if (!VT_IsMobile((CItem *)mob))
		return;

	dataCursor = GetValue(dataCursor, buf);
	skillId = (uint16_t)atoi(buf);

	dataCursor = GetValue(dataCursor, buf);
	USED(dataCursor);
	rollVal = CRandom_ParseAndRoll(buf);

	mob->skills[skillId] = rollVal;
}

/*
 * 0x004BA865 - CTemplateManager::LoadObjVar
 *
 * Parses an "objvar TYPE NAME VALUE" template field and stores the
 * variable on the entity. TYPE is "int" (dice-rolled) or "string".
 */
static void
CTemplateManager_LoadObjVar(CItem *entity, char *data)
{
	CString valueStr;
	char typeBuf[32];
	char nameBuf[64];
	char valueBuf[128];
	int parsedInt;

	CString_DefaultConstructor(&valueStr);

	data = GetValue(data, typeBuf);

	data = ObjVar_ParseToken(data, nameBuf);

	data = SCommand_ParseToken(data, valueBuf);

	USED(data);

	if (strcasecmp(typeBuf, "int") == 0) {
		parsedInt = CRandom_ParseAndRoll(valueBuf);
		{
			CString nameStr;
			CString_Constructor(&nameStr, nameBuf);
			ObjVar_SetStr(entity, &nameStr, 0, (uint32_t)parsedInt);
		}
	} else if (strcasecmp(typeBuf, "string") == 0) {
		CString_AssignCStr(&valueStr, valueBuf);
		{
			CString nameStr;
			CString_Constructor(&nameStr, nameBuf);
			ObjVar_SetStr(entity, &nameStr, 1, (uintptr_t)&valueStr);
		}
	}

	CString_Destructor(&valueStr);
}

/*
 * 0x004BA9B6 - ParseBracedDefineList
 *
 * Parses a brace-enclosed weighted list "{ token1 w1 token2 w2 ... }"
 * into a CStringList. Returns the updated data cursor.
 *
 * FIXED: Binary bug. ResolveDefines is a flat token substitution, so
 * data lines like `<eq { random_jewel 1 0 1 } ...>` end up as
 * `{ { 3855 1 ... 3878 1 } 1 0 1 } ...` after the random_jewel define
 * (a `{ ... }` brace-list value) expands. The binary's flat
 * (token, weight) pair loop treats nested `{` as a regular token,
 * mis-aligning the pairs and producing bodyType=0 (no item) or
 * bodyType=1 (NODRAW, fails CWorld_LookupItemResource) at the eq
 * lookup. As a result, every `{ random_X 1 0 N }` line in
 * templatestable.dat (orc/dragon jewel drops, Goodie/Pouch loot
 * templates) silently produces nothing. Restore the data author's
 * intent: when the next token is `{`, recursively parse the sub-list
 * and pick a weighted random winner at parse time; that winner
 * becomes the outer pair's token, with the outer weight still read
 * from the next token after the sub-list's closing `}`.
 */
static char *
CTemplateManager_ParseBracedDefineList(char *dataCursor, CStringList *list)
{
	char tokenBuf[128];
	char weightBuf[128];
	CString nameStr;
	CResListNode *iterCtx;
	int weight;

	CString_Constructor(&nameStr, "");

	while (1) {
		dataCursor = GetValue(dataCursor, tokenBuf);
		if (*dataCursor == '\0')
			break;

		CString_AssignCStr(&nameStr, tokenBuf);
		if (CString_CompareStr(&nameStr, "}"))
			break;

		// FIXED: nested-brace recursion (see function header).
		// The binary falls through and treats `{` as a token, which
		// shifts the (token, weight) pairs by one and corrupts the
		// outer list.
		if (CString_CompareStr(&nameStr, "{")) {
			CStringList subList;
			CResListNode *subMainCtx;
			CResListNode *subTempCtx;
			void *subData;

			CStringList_Init(&subList);
			CResBankMagicCtx_DefaultConstructor(&subMainCtx);

			dataCursor = CTemplateManager_ParseBracedDefineList(dataCursor, &subList);

			CStringList_GetWeightedRandom(&subList, &subTempCtx);
			CResBankMagicCtx_CopyConstructor(&subMainCtx, &subTempCtx);
			CResBankMagicCtx_PostInit(&subTempCtx);

			if (CStringList_HasEntries((CStringList *)&subMainCtx)) {
				subData = CStringList_GetNodeData(&subList, &subMainCtx);
				CString_Assign(&nameStr, (CString *)subData);
			} else {
				CString_AssignCStr(&nameStr, "0");
			}

			CResBankMagicCtx_PostInit(&subMainCtx);
			CStringList_Destroy(&subList);
		}

		dataCursor = GetValue(dataCursor, weightBuf);
		if (*dataCursor == '\0')
			break;

		weight = atoi(weightBuf);

		CStringList_AddWeighted(list, &iterCtx, &nameStr, weight, 1);
		CResBankMagicCtx_PostInit(&iterCtx);
	}

	CString_Destructor(&nameStr);

	return dataCursor;
}

/*
 * 0x004BC081 - CTemplateManager::ParseSfx
 *
 * Sets one of the mobile's five sound effect fields based on the
 * sfx field type (notice/idle/hit/washit/die).
 */
static void __attribute__((unused))
CTemplateManager_ParseSfx(CItem *entity, const char *data, int fieldType)
{
	int val;

	if (!VT_IsMobile(entity))
		return;

	val = fieldType;
	val -= 0x16;
	if (val > 4)
		return;

	switch (val) {
	case 0:
		((CMobile *)entity)->sfxNotice = (uint16_t)atoi(data);
		break;
	case 2:
		((CMobile *)entity)->sfxHit = (uint16_t)atoi(data);
		break;
	case 4:
		((CMobile *)entity)->sfxDie = (uint16_t)atoi(data);
		break;
	case 3:
		((CMobile *)entity)->sfxWasHit = (uint16_t)atoi(data);
		break;
	case 1:
		((CMobile *)entity)->sfxIdle = (uint16_t)atoi(data);
		break;
	}
}

/*
 * 0x004BC153 - CTemplateManager::AddConvoTitle
 *
 * Adds a conversation fragment title to an NPC (after skipping
 * leading whitespace).
 */
static void
CTemplateManager_AddConvoTitle(CItem *npc, const char *title)
{
	CString fragStr;

	if (!VT_IsNPC(npc))
		return;
	while (*title != '\0' && isspace((unsigned char)*title))
		title++;
	CString_Constructor(&fragStr, title);
	CNPC_AddFragment((CNPC *)npc, &fragStr);
	CString_Destructor(&fragStr);
}

/*
 * 0x004BC1E9 - CTemplateManager::ParseScript
 *
 * Parses "script NAME PROBABILITY" and attaches the script to the
 * entity if the probability roll passes (0 and 100 attach unconditionally).
 */
static void __attribute__((unused))
CTemplateManager_ParseScript(CItem *entity, char *data)
{
	char nameBuf[512];
	int probability;
	char probBuf[128];

	probability = 0;
	data = GetValue(data, nameBuf);
	data = GetValue(data, probBuf);
	USED(data);

	probability = atoi(probBuf);
	if (probability == 0 || probability == 100 || GetRandomRange(1, 100) <= probability) {
		Entity_AttachScript(entity, nameBuf, 1);
	}
}

/*
 * 0x004BC293 - CTemplateManager::ParseMoveType
 *
 * Parses a movement type byte and applies it to the mobile.
 */
static void __attribute__((unused))
CTemplateManager_ParseMoveType(CItem *entity, const char *data)
{
	uint8_t val;

	if (!VT_IsMobile(entity))
		return;

	val = (uint8_t)atoi(data);
	CMobile_SetMovementType((CMobile *)entity, val);
}

/*
 * 0x004BC2CF - CTemplateManager::AttachMulti
 *
 * Spawns a multi (boat/house) on the mobile via CMultiManager, hiding
 * the mobile first if visible.
 */
static int
CTemplateManager_AttachMulti(CItem *entity, CLocation *loc, const char *data)
{
	int multiType;
	uint8_t graphicByte;
	int direction;
	int index;
	int placed;
	int result;
	CItem *item;

	if (!VT_IsMobile(entity))
		return 1;

	multiType = atoi(data);

	graphicByte = (uint8_t)((uint32_t (*)(void *))VT_FN(entity, VT_GET_MOVEMENT_TYPE))(entity);

	direction = ((int (*)(void *))VT_FN(entity, VT_GET_DIRECTION))(entity) & 0x7F;

	index = direction + multiType * 8 + 0x7D0;

	placed = 0;
	if (entity->resourceEntity.entity.removedFromWorld == 0) {
		((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);
		placed = 1;
	}
	USED(placed);

	result = 0;
	item = CMultiManager_MakeMultiCheck(&g_MultiManager, index, loc, graphicByte & 0xFF, 0, &result, -1, -1, -1, 0, entity);
	if (item == NULL)
		return 0;

	CMultiSlave_SetCarry(CItem_GetMultiSlave(entity), 0);
	return 1;
}

/*
 * 0x004BC3A9 - CTemplateManager::ParseQuality
 *
 * Effectively a no-op: parses the quality value and queries the entity
 * layer, but discards both results.
 */
static void __attribute__((unused))
CTemplateManager_ParseQuality(CItem *entity, const char *data)
{
	(void)atoi(data);
	(void)CItem_GetLayerThiscall(entity);
}

/*
 * 0x004BC3CF - CTemplateManager::ParseQuantity
 *
 * Parses a quantity dice expression, returning at least 1 (or 1 for
 * the special -1 sentinel).
 */
static int
CTemplateManager_ParseQuantity(CItem *entity, const char *data)
{
	int val;
	int result;

	USED(entity);
	result = 1;
	val = atoi(data);
	if (val == -1)
		return result;
	result = CRandom_ParseAndRoll(data);
	if (result <= 0)
		result = 1;
	return result;
}

/*
 * 0x004BC41B - CTemplateManager::ParseNaturalAC
 *
 * Rolls the naturalac dice expression and applies it as the mobile's
 * bonus armor class.
 */
static void __attribute__((unused))
CTemplateManager_ParseNaturalAC(CItem *entity, const char *data)
{
	int val;

	if (!VT_IsMobile(entity))
		return;

	val = CRandom_ParseAndRoll(data);
	CMobile_SetBonusAC((CMobile *)entity, val);
}

/*
 * 0x004BC459 - CTemplateManager::naturalwc
 *
 * Parses a dice expression token and stores it as the mobile's
 * armor/weapon rating dice.
 */
static void
CTemplateManager_naturalwc(CItem *entity, char *data)
{
	CWeaponDice dice;
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	CDiceRoll_InitParse(&dice, buf);
	CMobile_SetArmorRating((CMobile *)entity, &dice);
}

/*
 * 0x004BC4B1 - CTemplateManager::ParseDamageType
 *
 * Maps a damage type name to its bitmask (slashing=0x01, piercing=0x02,
 * bashing=0x04, ranged=0x08, magic=0x10, fire=0x20, ice=0x80, else 0).
 */
static uint8_t __attribute__((unused))
CTemplateManager_ParseDamageType(const char *name)
{
	if (strcasecmp(name, "slashing") == 0)
		return 0x01;
	if (strcasecmp(name, "bashing") == 0)
		return 0x04;
	if (strcasecmp(name, "piercing") == 0)
		return 0x02;
	if (strcasecmp(name, "ranged") == 0)
		return 0x08;
	if (strcasecmp(name, "magic") == 0)
		return 0x10;
	if (strcasecmp(name, "fire") == 0)
		return 0x20;
	if (strcasecmp(name, "ice") == 0)
		return 0x80;
	if (strcasecmp(name, "poison") == 0)
		return 0x00;
	return 0x00;
}

/*
 * 0x004BC58E - CTemplateManager::ParseDamageTypeList8
 *
 * Walks a whitespace-separated keyword list, mapping each through
 * ParseDamageType into a byte local. Nothing accumulates and nothing
 * reads the local afterwards, so the loop only consumes the string.
 * Reproduced as it stands - nothing calls this.
 */
static __attribute__((unused)) void
CTemplateManager_ParseDamageTypeList8(CTemplateData *tmpl, char *list)
{
	char buf[128];
	uint8_t value;

	USED(tmpl);
	value = 0;
	while (strlen(list) != 0) {
		list = GetValue(list, buf);
		value = CTemplateManager_ParseDamageType(buf);
	}
	USED(value);
}

/*
 * 0x004BC5E0 - CTemplateManager::ParseDamageTypeList32
 *
 * As 0x004BC58E but widens each mapped value into an int local. Also
 * discarded.
 */
static __attribute__((unused)) void
CTemplateManager_ParseDamageTypeList32(CTemplateData *tmpl, char *list)
{
	char buf[128];
	int value;

	USED(tmpl);
	value = 0;
	while (strlen(list) != 0) {
		list = GetValue(list, buf);
		value = (int)(int8_t)CTemplateManager_ParseDamageType(buf);
	}
	USED(value);
}

/*
 * 0x004BC635 - CTemplateManager::ParseAttitude
 *
 * Sets the mobile's attack mode from a rolled dice expression.
 */
static void __attribute__((unused))
CTemplateManager_ParseAttitude(CItem *entity, char *data)
{
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	USED(data);
	((CMobile *)entity)->attackMode = (uint8_t)CRandom_ParseAndRoll(buf);
}

/*
 * 0x004BC686 - CTemplateManager::ParseDexterity
 *
 * Sets the mobile's base dexterity from a rolled dice expression.
 */
static void __attribute__((unused))
CTemplateManager_ParseDexterity(CItem *entity, char *data)
{
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	USED(data);
	((CMobile *)entity)->baseDex = (uint16_t)CRandom_ParseAndRoll(buf);
}

/*
 * 0x004BC6D8 - CTemplateManager::ParseHitPoints
 *
 * Sets maxHp/hp from the hp field. If the first token is "STR", both
 * copy baseStr; otherwise both roll their own dice expressions (hp=0
 * falls back to maxHp).
 */
static void __attribute__((unused))
CTemplateManager_ParseHitPoints(CItem *entity, char *data)
{
	char buf[128];
	char *next;
	CMobile *mob;

	next = GetValue(data, buf);

	if (!VT_IsMobile(entity))
		return;

	mob = (CMobile *)entity;

	if (strcasecmp(buf, "STR") == 0) {
		mob->maxHp = (uint32_t)(uint16_t)mob->baseStr;
		mob->hp = (uint32_t)(uint16_t)mob->baseStr;
	} else {
		mob->maxHp = (uint32_t)CRandom_ParseAndRoll(buf);
		next = GetValue(next, buf);
		USED(next);
		mob->hp = (uint32_t)CRandom_ParseAndRoll(buf);
		if (mob->hp == 0)
			mob->hp = mob->maxHp;
	}
}

/*
 * 0x004BC7B9 - CTemplateManager::ParseHueColor
 *
 * Applies a parsed hue to the entity. For partial hues on mobiles,
 * sets the 0x8000 bit. Refreshes display if the entity is in world.
 */
static void __attribute__((unused))
CTemplateManager_ParseHueColor(CItem *entity, char *data, int *colorState, int isPartialHue)
{
	int colorVal;

	data = CTemplateManager_ParseColor(data, &colorVal, colorState);
	USED(data);

	if (colorVal == -1)
		return;

	entity->resourceEntity.entity.color = (uint16_t)colorVal;

	if (isPartialHue == 1) {
		if (VT_IsMobile(entity)) {
			colorVal |= 0x8000;
		}
	}

	entity->resourceEntity.entity.color = (uint16_t)colorVal;

	if (entity->resourceEntity.entity.removedFromWorld == 0) {
		((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);
		((void (*)(void *))VT_FN(entity, VT_RETURN_TO_TRACKED))(entity);
	}
}

/*
 * 0x004BC83E - CTemplateManager::ParseColorExpression
 *
 * Recursive weighted color expression parser. Parses
 * "{ min max weight ... }" (mins/maxes may themselves be nested braces),
 * picks one triple randomly, and writes the chosen min/max to buf1/buf2.
 */
char *
CTemplateManager_ParseColorExpression(char *dataCursor, char *buf1, char *buf2)
{
	CStringPairList list;
	CStringPairListNode *pairData;
	char weightBuf[128];
	int weight;

	CStringList_Init((CStringList *)&list);

	dataCursor = GetValue(dataCursor, buf1);

	while (1) {
		if (*dataCursor == '\0')
			break;
		if (strcasecmp(buf1, "}") == 0)
			break;

		if (strcasecmp(buf1, "{") == 0) {
			dataCursor = CTemplateManager_ParseColorExpression(dataCursor, buf1, buf2);
		} else {
			dataCursor = GetValue(dataCursor, buf2);
			if (*dataCursor == '\0')
				break;
		}

		dataCursor = GetValue(dataCursor, weightBuf);
		if (*dataCursor == '\0')
			break;

		weight = atoi(weightBuf);

		CResListNode *addOut, *addCtx;
		CStringPairList_AddWeighted(&list, &addOut, weight, 1);
		CResBankMagicCtx_DefaultConstructor(&addCtx);
		CResBankMagicCtx_CopyConstructor(&addCtx, &addOut);
		CResBankMagicCtx_PostInit(&addOut);
		if (CStringList_HasEntries((CStringList *)&addCtx)) {
			pairData = (CStringPairListNode *)CStringList_GetNodeData((CStringList *)&list, &addCtx);
			CString_AssignCStr(&pairData->first, buf1);
			CString_AssignCStr(&pairData->second, buf2);
		}
		CResBankMagicCtx_PostInit(&addCtx);

		dataCursor = GetValue(dataCursor, buf1);
	}

	CResListNode *randOut, *randCtx;
	CStringPairList_GetWeightedRandom(&list, &randOut);
	CResBankMagicCtx_DefaultConstructor(&randCtx);
	CResBankMagicCtx_CopyConstructor(&randCtx, &randOut);
	CResBankMagicCtx_PostInit(&randOut);
	if (CStringList_HasEntries((CStringList *)&randCtx)) {
		pairData = (CStringPairListNode *)CStringList_GetNodeData((CStringList *)&list, &randCtx);
		strcpy(buf1, CString_GetBuffer(&pairData->first));
		strcpy(buf2, CString_GetBuffer(&pairData->second));
	} else {
		strcpy(buf1, "0");
		strcpy(buf2, "0");
	}
	CResBankMagicCtx_PostInit(&randCtx);

	CStringPairList_Init(&list);
	return dataCursor;
}

/*
 * 0x004BCB35 - CTemplateManager::ParseColor
 *
 * Parses a color specification: "min max" for a random range,
 * "match_hair" to copy *colorPtr, "0 0" for none (-1), or "{ ... }"
 * for a weighted random selection via ParseColorExpression.
 */
char *
CTemplateManager_ParseColor(char *dataCursor, int *colorOut, int *colorPtr)
{
	char buf[32];
	char buf2[32];
	int colorMin, colorMax;

	dataCursor = GetValue(dataCursor, buf);

	if (strcasecmp(buf, "{") == 0) {
		dataCursor = CTemplateManager_ParseColorExpression(dataCursor, buf, buf2);
	} else if (strcasecmp(buf, "match_hair") == 0) {
		*colorOut = *colorPtr;
		return dataCursor;
	} else {
		dataCursor = GetValue(dataCursor, buf2);
	}

	if (strcasecmp(buf, "0") == 0 && strcasecmp(buf2, "0") == 0) {
		*colorOut = -1;
		return dataCursor;
	}

	colorMin = CRandom_ParseAndRoll(buf);
	colorMax = CRandom_ParseAndRoll(buf2);
	if (colorMax == 0)
		colorMax = colorMin;
	*colorOut = GetRandomRange(colorMin, colorMax);
	return dataCursor;
}

/*
 * 0x004BCC3C - CTemplateManager::ParseIntelligence
 *
 * Sets the mobile's base intelligence from a rolled dice expression.
 */
static void __attribute__((unused))
CTemplateManager_ParseIntelligence(CItem *entity, char *data)
{
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	USED(data);
	((CMobile *)entity)->baseInt = (uint16_t)CRandom_ParseAndRoll(buf);
}

/*
 * 0x004BCC8B - CTemplateManager::ParseMana
 *
 * Sets maxMana/mana from the mana field. If the first token is "INT",
 * both copy baseInt; otherwise both roll their own dice (mana=0 falls
 * back to maxMana).
 */
static void __attribute__((unused))
CTemplateManager_ParseMana(CItem *entity, char *data)
{
	char buf[128];
	char *next;
	CMobile *mob;

	if (!VT_IsMobile(entity))
		return;

	next = GetValue(data, buf);

	mob = (CMobile *)entity;

	if (strcasecmp(buf, "INT") == 0) {
		mob->maxMana = (uint32_t)(uint16_t)mob->baseInt;
		mob->mana = (uint32_t)(uint16_t)mob->baseInt;
	} else {
		mob->maxMana = (uint32_t)CRandom_ParseAndRoll(buf);
		next = GetValue(next, buf);
		USED(next);
		mob->mana = (uint32_t)CRandom_ParseAndRoll(buf);
		if (mob->mana == 0)
			mob->mana = mob->maxMana;
	}
}

/*
 * 0x004BCD6C - CTemplateManager::ParseNotoriety
 *
 * Sets the mobile's notoriety byte from a rolled dice expression.
 */
static void __attribute__((unused))
CTemplateManager_ParseNotoriety(CItem *entity, char *data)
{
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	USED(data);
	((CMobile *)entity)->notoriety = (uint8_t)CRandom_ParseAndRoll(buf);
}

/*
 * 0x004BCDBA - CTemplateManager::ParseFame
 *
 * Sets the mobile's fame from a rolled dice expression.
 */
static void __attribute__((unused))
CTemplateManager_ParseFame(CItem *entity, char *data)
{
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	USED(data);
	((CMobile *)entity)->fame = (int16_t)CRandom_ParseAndRoll(buf);
}

/*
 * 0x004BCE09 - CTemplateManager::ParseKarma
 *
 * Sets the mobile's karma from a rolled dice expression.
 */
static void __attribute__((unused))
CTemplateManager_ParseKarma(CItem *entity, char *data)
{
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	USED(data);
	((CMobile *)entity)->karma = (int16_t)CRandom_ParseAndRoll(buf);
}

/*
 * 0x004BCE58 - CTemplateManager::ParseStrength
 *
 * Sets the mobile's base strength from a rolled dice expression.
 */
static void __attribute__((unused))
CTemplateManager_ParseStrength(CItem *entity, char *data)
{
	char buf[128];

	if (!VT_IsMobile(entity))
		return;

	data = GetValue(data, buf);
	USED(data);
	((CMobile *)entity)->baseStr = (uint16_t)CRandom_ParseAndRoll(buf);
}

/*
 * 0x004BCEA7 - CTemplateManager::ParseStamina
 *
 * Sets maxStamina/stamina from the stamina field. If the first token
 * is "DEX", both copy baseDex; otherwise both roll their own dice
 * (stamina=0 falls back to maxStamina).
 */
static void __attribute__((unused))
CTemplateManager_ParseStamina(CItem *entity, char *data)
{
	char buf[128];
	char *next;
	CMobile *mob;

	if (!VT_IsMobile(entity))
		return;

	next = GetValue(data, buf);

	mob = (CMobile *)entity;

	if (strcasecmp(buf, "DEX") == 0) {
		mob->maxStamina = (uint32_t)(uint16_t)mob->baseDex;
		mob->stamina = (uint32_t)(uint16_t)mob->baseDex;
	} else {
		mob->maxStamina = (uint32_t)CRandom_ParseAndRoll(buf);
		next = GetValue(next, buf);
		USED(next);
		mob->stamina = (uint32_t)CRandom_ParseAndRoll(buf);
		if (mob->stamina == 0)
			mob->stamina = mob->maxStamina;
	}
}

/*
 * 0x004BCF88 - CTemplateManager::ResetMobile
 *
 * Resets a mob to blank state (clears color, template index, multi,
 * stats, notoriety/fame/karma, and all 50 skills). Called by
 * CMobile_BecomeTemplate before re-applying a new template.
 */
void
CTemplateManager_ResetMobile(CMobile *mob)
{
	int i;

	mob->container.item.resourceEntity.entity.color = 0;
	CResourceEntity_ResetTemplateIndex((CItem *)mob);
	CItem_DetachMulti((CItem *)mob);

	if (!VT_IsMobile((CItem *)mob))
		return;

	((void (*)(void *, int, int))VT_FN((CItem *)mob, VT_SET_BASE_STAT))(mob, 0, 0);
	((void (*)(void *, int, int))VT_FN((CItem *)mob, VT_SET_BASE_STAT))(mob, 1, 0);
	((void (*)(void *, int, int))VT_FN((CItem *)mob, VT_SET_BASE_STAT))(mob, 2, 0);

	((void (*)(void *, int))VT_FN((CItem *)mob, VT_SET_NOTORIETY))(mob, 0);

	CMobile_SetFame(mob, 0);
	CMobile_SetKarma(mob, 0);

	for (i = 0; i < 50; i++)
		CMobile_SetSkill(mob, (int8_t)i, 0);
}

/*
 * 0x004BD04D - CTemplateManager::SetTypeOnEntity
 *
 * For each recognized subtype (normal/guard/shopkeeper/item), sets the
 * entity's body type if the entity's mobility matches. Returns 1 for
 * recognized subtypes, 0 otherwise.
 */
static int
CTemplateManager_SetTypeOnEntity(CItem *entity, const char *subtypeBuf, uint16_t bodyType, CLocation *loc)
{
	USED(loc);
	if (strcasecmp(subtypeBuf, "normal") == 0) {
		if (VT_IsMobile(entity) == 1)
			CEntity_SetBodyType(entity, bodyType);
	} else if (strcasecmp(subtypeBuf, "guard") == 0) {
		if (VT_IsMobile(entity) == 1)
			CEntity_SetBodyType(entity, bodyType);
	} else if (strcasecmp(subtypeBuf, "shopkeeper") == 0) {
		if (VT_IsMobile(entity) == 1)
			CEntity_SetBodyType(entity, bodyType);
	} else if (strcasecmp(subtypeBuf, "item") == 0) {
		if (VT_IsMobile(entity) == 0)
			CEntity_SetBodyType(entity, bodyType);
	} else if (strcasecmp(subtypeBuf, "none") == 0) {
		// nothing
	} else {
		return 0;
	}
	return 1;
}

/*
 * 0x004BD165 - CTemplateManager::CreateMobOrItem
 *
 * Allocates an entity based on the subtype string (normal/guard/
 * shopkeeper/item/none), writing it to *out. Returns 1 for recognized
 * subtypes, 0 otherwise.
 *
 * MODIFIED: the ITEM branch sets itemFlags |= 0x08 to mark spawner items
 * as transient (not saved to dynamic0.mul), preventing ITEM group NPC
 * accumulation across server restarts.
 */
static int
CTemplateManager_CreateMobOrItem(CItem **out, const char *subtypeBuf, uint16_t bodyType, CLocation *loc)
{
	*out = NULL;

	if (strcasecmp(subtypeBuf, "normal") == 0) {
		CNPC *npc = calloc(1, sizeof(CNPC));
		if (npc != NULL)
			CResourceMobile_Constructor(&npc->mobile, bodyType, loc);
		*out = (CItem *)npc;
	} else if (strcasecmp(subtypeBuf, "guard") == 0) {
		CNPC *npc = calloc(1, sizeof(CNPC));
		if (npc != NULL)
			CNPC_Constructor(npc, bodyType, loc);
		*out = (CItem *)npc;
	} else if (strcasecmp(subtypeBuf, "shopkeeper") == 0) {
		CNPC *npc = calloc(1, sizeof(CNPC));
		if (npc != NULL) {
			CShopkeeper_Constructor(npc, bodyType, loc);
			*out = (CItem *)npc;
			if (*out != NULL)
				CShopkeeper_InitContainers(npc);
		}
	} else if (strcasecmp(subtypeBuf, "item") == 0) {
		CItem *item = CWorld_CreateItem(g_World, bodyType);
		*out = item;
		if (item != NULL) {
			// MODIFIED: mark ITEM spawner as transient so it is
			// not saved to dynamic0.mul (prevents accumulation
			// of companion NPCs across server restarts)
			item->itemFlags |= 0x08;
			((void (*)(void *, CLocation *))VT_FN(item, VT_DROP_AT_FEET))(item, loc);
		}
	} else if (strcasecmp(subtypeBuf, "none") == 0) {
		*out = NULL;
	} else {
		return 0;
	}
	return 1;
}

/*
 * 0x004BD38C - CTemplateManager::AppendStringList
 *
 * Appends a string list value. If the value starts with "{", parses
 * a weighted brace list; otherwise adds a single entry with weight 1.
 */
static void
CTemplateManager_AppendStringList(CStringList *list, char *data)
{
	char tokenBuf[128];
	CString nameStr;
	CResListNode *iterCtx;

	data = GetValue(data, tokenBuf);

	if (strcasecmp(tokenBuf, "{") == 0) {
		CTemplateManager_ParseBracedDefineList(data, list);
	} else {
		CString_Constructor(&nameStr, tokenBuf);
		CStringList_AddWeighted(list, &iterCtx, &nameStr, 1, 0);
		CResBankMagicCtx_PostInit(&iterCtx);
		CString_Destructor(&nameStr);
	}
}

/*
 * 0x004BD456 - CTemplateManager::GetNextEqValue
 *
 * Reads the next token from an eq data cursor. If the token is "{",
 * parses a weighted brace list and assigns a random pick to out
 * (falling back to "0" if the list is empty). Otherwise assigns the
 * token directly.
 */
char *
CTemplateManager_GetNextEqValue(char *dataCursor, CString *out)
{
	char buf[128];

	dataCursor = GetValue(dataCursor, buf);

	if (strcasecmp(buf, "{") == 0) {
		CStringList list;
		CResListNode *mainCtx;
		CResListNode *tempCtx;
		void *nodeData;

		CStringList_Init(&list);
		CResBankMagicCtx_DefaultConstructor(&mainCtx);

		dataCursor = CTemplateManager_ParseBracedDefineList(dataCursor, &list);

		CStringList_GetWeightedRandom(&list, &tempCtx);
		CResBankMagicCtx_CopyConstructor(&mainCtx, &tempCtx);
		CResBankMagicCtx_PostInit(&tempCtx);

		if (CStringList_HasEntries((CStringList *)&mainCtx)) {
			nodeData = CStringList_GetNodeData(&list, &mainCtx);
			CString_Assign(out, (CString *)nodeData);
		} else {
			CString_AssignCStr(out, "0");
		}

		CResBankMagicCtx_PostInit(&mainCtx);
		CStringList_Destroy(&list);
	} else {
		CString_AssignCStr(out, buf);
	}

	return dataCursor;
}

/*
 * 0x004BD5DF - Template_LoadNamedValue
 *
 * Parses an NPC "job" field: adds "BRITANNIA_<name>" as a conversation
 * fragment and stores the interned name as npc->npcJob.
 */
static void
Template_LoadNamedValue(CMobile *mob, char *dataCursor)
{
	char buf[128];
	CString fragStr;

	if (!VT_IsNPC((CItem *)mob))
		return;

	sprintf(buf, "BRITANNIA_%s", dataCursor);

	CString_Constructor(&fragStr, buf);
	CNPC_AddFragment((CNPC *)mob, &fragStr);
	CString_Destructor(&fragStr);

	((CNPC *)mob)->npcJob = (char *)CScriptManager_InternString(&g_ScriptManager, dataCursor);
}

/*
 * 0x004BD67A - Template_SetRealName
 *
 * Parses the next token from text and sets it as the mobile's name.
 */
void
Template_SetRealName(CItem *item, char *text)
{
	char nameBuf[128];

	if (!VT_IsMobile(item))
		return;

	text = SCommand_ParseToken(text, nameBuf);
	CMobile_SetName((CMobile *)item, nameBuf);
	USED(text);
}

/*
 * 0x004BD6C4 - CTemplateManager::GetTemplateFirstValue
 *
 * Looks a template up by id, skips the first key on its data line and
 * returns the following "=value" parsed as a number. Returns 0 when the
 * id is unknown. The key buffer is filled and discarded.
 */
static __attribute__((unused)) uint16_t
CTemplateManager_GetTemplateFirstValue(CResManager *this, uint32_t templateId)
{
	CSearchCtx ctx;
	NPCTemplate *tpl;
	CString value;
	char keyBuf[512];
	char *cursor;
	uint16_t result;

	CResManager_FindByIntCtx(this, &ctx, &templateId, 1);
	if (!CSearchCtx_Find(&ctx))
		return 0;

	tpl = (NPCTemplate *)CResManager_GetResultCtx(this, &ctx);
	cursor = tpl->rawFields[0].data;
	cursor = GetValue(cursor, keyBuf);

	CString_DefaultConstructor(&value);
	cursor = CTemplateManager_GetNextEqValue(cursor, &value);
	result = (uint16_t)CString_GetString(&value);
	CString_Destructor(&value);

	return result;
}

/*
 * 0x004BD7C6 - CTemplateManager::CreateEntity
 *
 * Inner entity creation called from CreateFromTemplate. Looks up the
 * template, reads the body type from field 0, creates or updates the
 * entity, then iterates the remaining fields through a case-table
 * dispatch to the individual Parse* helpers. Cases 3/15/18/28 also
 * validate the entity survived the handler call.
 */
static CItem *
CTemplateManager_CreateEntity(uint16_t templateId, CLocation *loc, int *amount, int restockFlag, CItem *existingMob, int depth)
{
	NPCTemplate *tmpl;
	CItem *entity;
	CItem *result;
	int i;
	int colorState;
	int eqCounter;
	int spawnResult;
	uint32_t savedSerial;
	CResManager eqRM;
	CSearchCtx tmplCtx;
	CString bodyTypeStr;
	char subtypeBuf[128];
	char *cursor;
	uint16_t bodyType;
	int isNone;

	eqCounter = 0;
	CResManager_Constructor(&eqRM, 1);
	colorState = -1;
	*amount = 1;

	{
		uint32_t tmplKey = (uint32_t)templateId;
		CResManager_FindByIntCtx(&g_TemplatesRM, &tmplCtx, &tmplKey, 1);
	}
	if (!CSearchCtx_Find(&tmplCtx)) {
		CResManager_ClearJ_Thunk(&eqRM);
		return NULL;
	}

	tmpl = (NPCTemplate *)CResManager_GetResultCtx(&g_TemplatesRM, &tmplCtx);

	cursor = GetValue(tmpl->rawFields[0].data, subtypeBuf);
	CString_DefaultConstructor(&bodyTypeStr);
	cursor = CTemplateManager_GetNextEqValue(cursor, &bodyTypeStr);
	bodyType = (uint16_t)atoi(CString_GetBuffer(&bodyTypeStr));
	USED(cursor);

	isNone = (strcasecmp(subtypeBuf, "none") == 0);
	if (!isNone && bodyType == 0 && existingMob == NULL) {
		result = NULL;
		goto cleanup;
	}
	// Binary has a second redundant guard (bodyType <= 0 vs == 0).
	if (!isNone && bodyType <= 0 && existingMob == NULL) {
		result = NULL;
		goto cleanup;
	}

	if (existingMob == NULL) {
		if (!CTemplateManager_CreateMobOrItem(&entity, subtypeBuf, bodyType, loc)) {
			result = NULL;
			goto cleanup;
		}
	} else {
		entity = existingMob;
		CTemplateManager_SetTypeOnEntity(entity, subtypeBuf, bodyType, loc);
	}

	for (i = 1; i < tmpl->numRawFields; i++) {
		uint32_t ft = tmpl->rawFields[i].type;
		char *data = tmpl->rawFields[i].data;
		uint8_t caseIdx;

		if (entity == NULL && ft != TMPL_FIELD_EQ && ft != TMPL_FIELD_REQ)
			continue;

		if (ft < 1 || ft > 48)
			continue;
		caseIdx = g_FieldCaseTable[ft - 1];

		switch (caseIdx) {
		case 0:
			CTemplateManager_ParseAttitude(entity, data);
			break;
		case 1:
			CTemplateManager_AddConvoTitle(entity, data);
			break;
		case 2:
			CTemplateManager_ParseDexterity(entity, data);
			break;
		case 3:
			if (entity != NULL)
				savedSerial = CMobile_GetSerial((CMobile *)entity);
			else
				savedSerial = 0;
			spawnResult = CTemplateManager_SpawnVendorStock(entity, data, &eqCounter, *loc, &eqRM, &colorState, (uint32_t)restockFlag, depth);
			if (!CTemplateManager_ValidateOwner(savedSerial, entity)) {
				result = NULL;
				goto cleanup;
			}
			if (spawnResult == -1 && existingMob == NULL) {
				if (entity != NULL)
					((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);
				result = NULL;
				goto cleanup;
			}
			break;
		case 4:
			CTemplateManager_ParseHitPoints(entity, data);
			break;
		case 5:
			CTemplateManager_ParseHueColor(entity, data, &colorState, 0);
			break;
		case 6:
			CTemplateManager_ParseIntelligence(entity, data);
			break;
		case 7:
			CTemplateManager_ParseMana(entity, data);
			break;
		case 8:
			CTemplateManager_ParseNotoriety(entity, data);
			break;
		case 9:
			CTemplateManager_ParseFame(entity, data);
			break;
		case 10:
			CTemplateManager_ParseKarma(entity, data);
			break;
		case 11:
			CTemplateManager_SetRealNameFromTemplate(entity, data);
			break;
		case 12:
			CTemplateManager_ParseStrength(entity, data);
			break;
		case 13:
			CTemplate_LoadSkillName((CMobile *)entity, data);
			break;
		case 14:
			CTemplateManager_ParseStamina(entity, data);
			break;
		case 15:
			if (existingMob != NULL)
				break;
			if (entity != NULL)
				savedSerial = CMobile_GetSerial((CMobile *)entity);
			else
				savedSerial = 0;
			CTemplateManager_ParseScript(entity, data);
			if (!CTemplateManager_ValidateOwner(savedSerial, entity)) {
				result = NULL;
				goto cleanup;
			}
			break;
		case 16:
			CTemplate_LoadSkillNameStr((CMobile *)entity, data);
			break;
		case 17:
			CTemplateManager_ParseSfx(entity, data, (int)ft);
			break;
		case 18:
			if (existingMob != NULL)
				break;
			if (entity != NULL)
				savedSerial = CMobile_GetSerial((CMobile *)entity);
			else
				savedSerial = 0;
			if (!CTemplateManager_AttachMulti(entity, loc, data)) {
				result = NULL;
				goto cleanup;
			}
			if (!CTemplateManager_ValidateOwner(savedSerial, entity)) {
				result = NULL;
				goto cleanup;
			}
			break;
		case 19:
			CTemplateManager_LoadObjVar(entity, data);
			break;
		case 20:
			CTemplateManager_ParseQuality(entity, data);
			break;
		case 21:
			*amount = CTemplateManager_ParseQuantity(entity, data);
			break;
		case 22:
			CTemplateManager_ParseNaturalAC(entity, data);
			break;
		case 23:
			CTemplateManager_naturalwc(entity, data);
			break;
		case 24:
			Template_LoadNamedValue((CMobile *)entity, data);
			break;
		case 25:
			CTemplateManager_ParseMoveType(entity, data);
			break;
		case 26:
			CTemplateManager_ParseHueColor(entity, data, &colorState, 1);
			break;
		case 27:
			if (VT_IsNPC(entity))
				CNPC_SetNPCFlee((CNPC *)entity, tmpl->fleeval);
			break;
		case 28:
			if (entity != NULL)
				savedSerial = CMobile_GetSerial((CMobile *)entity);
			else
				savedSerial = 0;
			spawnResult = CTemplateManager_SpawnVendorStock(entity, data, &eqCounter, *loc, &eqRM, &colorState, (uint32_t)restockFlag, depth);
			if (!CTemplateManager_ValidateOwner(savedSerial, entity)) {
				result = NULL;
				goto cleanup;
			}
			if (spawnResult <= 0) {
				result = entity;
				goto cleanup;
			}
			break;
		case 29:
			Template_SetRealName(entity, data);
			break;
		default:
			break;
		}
	}

	result = entity;

cleanup:
	CString_Destructor(&bodyTypeStr);
	CResManager_ClearJ_Thunk(&eqRM);
	return result;
}

/*
 * 0x004BE284 - CTemplateManager::ClearTemplateHash
 *
 * Clears the templates CResManager.
 */
static void
CTemplateManager_ClearTemplateHash(CTemplateManager *this)
{
	USED(this);
	CResManager_Clear_Templates(&g_TemplatesRM);
}

/*
 * 0x004BE297 - CTemplateManager::LoadTemplates
 *
 * Loads "templatestable.dat" (or split "template.%d" files). For each
 * template: caches the '#'-comment name, allocates an NPCTemplate, then
 * parses a stream of key/value field lines through a ~49-way stricmp
 * dispatch. Matched values are strdup'd (resolving defines first).
 * Tail-flagged fields (type, frequency, alignment, movetype, region,
 * regionlimit, friends, resource, corpsename, fleeval, createsnpcs)
 * get post-processed into their dedicated struct slots. Each template
 * is keyed by its file counter (up to 0x1000) in the templates
 * CResManager.
 *
 * Uses NPCTemplate in place of the binary's CTemplateData since all
 * downstream code depends on the former.
 */
static int
CTemplateManager_LoadTemplates(void)
{
	FILE *f;
	FILE *curFile;
	char *line;
	char key[96];
	NPCTemplate *tmpl;
	int splitFlag;
	int fileCounter;
	int numRawFields;
	int createVar;
	int fieldMatched;
	int doFrequency;
	int doMovetype;
	int doAlignment;
	int doRegion;
	int doRegionlimit;
	int doFriends;
	int doResource;
	int doCorpsename;
	int doFleeval;
	int doCreatesnpcs;
	CString inputStr;
	CString outputStr;
	int resolveResult;
	char lineBuf[80];
	long filePos;
	int lineLen;

	fieldMatched = 0;
	doFrequency = 0;
	doResource = 0;
	doRegion = 0;
	doRegionlimit = 0;
	doFriends = 0;
	doMovetype = 0;
	doAlignment = 0;
	doFleeval = 0;
	doCorpsename = 0;
	doCreatesnpcs = 0;
	createVar = 0;

	CString_DefaultConstructor(&outputStr);

	splitFlag = 0;
	f = FileManager_OpenByType(0x32, "templatestable.dat", "r");
	if (f == NULL) {
		splitFlag = 1;
		f = FileManager_OpenByType(0x32, "templatestable.dat", "w");
	}

	fileCounter = 0;
	curFile = f;

	for (;;) {
		if (splitFlag) {
			sprintf(key, "template.%d", fileCounter);
			curFile = FileManager_OpenByType(0x32, key, "r");
			if (curFile == NULL)
				goto next_template;
		} else {
			line = GetLine(curFile);
			if (line == NULL)
				goto cleanup;
			fileCounter = atoi(line);
		}

		// Peek at the first data line; if it starts with '#', cache
		// the template name in g_TemplateNames.
		filePos = ftell_ServerSide(curFile);
		fgets_ServerSide(lineBuf, 0x4F, curFile);
		fseek_ServerSide(curFile, filePos, 0);

		if (lineBuf[0] == '#') {
			lineLen = 0;
			while (lineBuf[lineLen] != '\r' && lineBuf[lineLen] != '\n' && lineBuf[lineLen] != '\0' && lineBuf[lineLen] != '<')
				lineLen++;

			if (g_TemplateNames[fileCounter] != NULL)
				OperatorDelete(g_TemplateNames[fileCounter]);

			lineBuf[lineLen] = '\0';
			g_TemplateNames[fileCounter] = (char *)OperatorNew(lineLen);
			strcpy(g_TemplateNames[fileCounter], &lineBuf[1]);
		}

		tmpl = (NPCTemplate *)OperatorNew(sizeof(NPCTemplate));
		if (tmpl != NULL) {
			CTemplateData_Constructor(tmpl);
			tmpl->id = (uint16_t)fileCounter;
			tmpl->nameTableId = -1;
			memset(tmpl->name, 0, sizeof(tmpl->name));
		}

		numRawFields = tmpl->numRawFields;

		for (;;) {
			char *p;
			int fieldType;

			// Binary checks both local and struct counts.
			if (numRawFields >= MAX_TEMPLATE_FIELDS)
				break;
			if (tmpl->numRawFields >= MAX_TEMPLATE_FIELDS)
				break;

			fieldMatched = 0;

			line = GetLine(curFile);
			if (line == NULL)
				goto inner_done;

			line = GetValue(line, key);

			fieldType = -1;

			if (strcasecmp(key, "type") == 0) {
				fieldType = 0;
			} else if (strcasecmp(key, "frequency") == 0) {
				fieldType = TMPL_FIELD_FREQUENCY;
				doFrequency = 1;
			} else if (strcasecmp(key, "movetype") == 0) {
				fieldType = TMPL_FIELD_FREQUENCY;
				doMovetype = 1;
			} else if (strcasecmp(key, "attitude") == 0) {
				fieldType = TMPL_FIELD_ATTITUDE;
			} else if (strcasecmp(key, "alignment") == 0) {
				doAlignment = 1;
				fieldType = TMPL_FIELD_FREQUENCY;
			} else if (strcasecmp(key, "convfrag") == 0) {
				fieldType = TMPL_FIELD_CONVFRAG;
			} else if (strcasecmp(key, "dexterity") == 0) {
				fieldType = TMPL_FIELD_DEXTERITY;
			} else if (strcasecmp(key, "eq") == 0) {
				fieldType = TMPL_FIELD_EQ;
			} else if (strcasecmp(key, "req") == 0) {
				fieldType = TMPL_FIELD_REQ;
			} else if (strcasecmp(key, "sk") == 0) {
				fieldType = TMPL_FIELD_SKILL;
			} else if (strcasecmp(key, "skill") == 0) {
				fieldType = TMPL_FIELD_SKILL;
			} else if (strcasecmp(key, "hp") == 0) {
				fieldType = TMPL_FIELD_HP;
			} else if (strcasecmp(key, "hue") == 0) {
				fieldType = TMPL_FIELD_HUE;
			} else if (strcasecmp(key, "partialhue") == 0) {
				fieldType = TMPL_FIELD_PARTIALHUE;
			} else if (strcasecmp(key, "intelligence") == 0) {
				fieldType = TMPL_FIELD_INTELLIGENCE;
			} else if (strcasecmp(key, "mana") == 0) {
				fieldType = TMPL_FIELD_MANA;
			} else if (strcasecmp(key, "notoriety") == 0) {
				fieldType = TMPL_FIELD_NOTORIETY;
			} else if (strcasecmp(key, "fame") == 0) {
				fieldType = TMPL_FIELD_FAME;
			} else if (strcasecmp(key, "karma") == 0) {
				fieldType = TMPL_FIELD_KARMA;
			} else if (strcasecmp(key, "name") == 0) {
				fieldType = TMPL_FIELD_NAME;
			} else if (strcasecmp(key, "realname") == 0) {
				fieldType = TMPL_FIELD_REALNAME;
			} else if (strcasecmp(key, "resource") == 0) {
				fieldType = TMPL_FIELD_RESOURCE;
				doResource = 1;
			} else if (strcasecmp(key, "corpsename") == 0) {
				fieldType = TMPL_FIELD_CORPSENAME;
				doCorpsename = 1;
			} else if (strcasecmp(key, "createsnpcs") == 0) {
				fieldType = TMPL_FIELD_CREATESNPCS;
				doCreatesnpcs = 1;
			} else if (strcasecmp(key, "fleeval") == 0) {
				fieldType = TMPL_FIELD_FLEEVAL;
				doFleeval = 1;
			} else if (strcasecmp(key, "strength") == 0) {
				fieldType = TMPL_FIELD_STRENGTH;
			} else if (strcasecmp(key, "sex") == 0) {
				fieldType = TMPL_FIELD_SEX;
			} else if (strcasecmp(key, "stamina") == 0) {
				fieldType = TMPL_FIELD_STAMINA;
			} else if (strcasecmp(key, "script") == 0) {
				fieldType = TMPL_FIELD_SCRIPT;
			} else if (strcasecmp(key, "region") == 0) {
				fieldType = TMPL_FIELD_REGION;
				doRegion = 1;
			} else if (strcasecmp(key, "sfxnotice") == 0) {
				fieldType = TMPL_FIELD_SFXNOTICE;
			} else if (strcasecmp(key, "sfxidle") == 0) {
				fieldType = TMPL_FIELD_SFXIDLE;
			} else if (strcasecmp(key, "sfxhit") == 0) {
				fieldType = TMPL_FIELD_SFXHIT;
			} else if (strcasecmp(key, "sfxwashit") == 0) {
				fieldType = TMPL_FIELD_SFXWASHIT;
			} else if (strcasecmp(key, "sfxdie") == 0) {
				fieldType = TMPL_FIELD_SFXDIE;
			} else if (strcasecmp(key, "objvar") == 0) {
				fieldType = TMPL_FIELD_OBJVAR;
			} else if (strcasecmp(key, "multi") == 0) {
				fieldType = TMPL_FIELD_MULTI;
			} else if (strcasecmp(key, "movetype") == 0) {
				// Unreachable: "movetype" is matched earlier.
				fieldType = TMPL_FIELD_MOVETYPE;
			} else if (strcasecmp(key, "quality") == 0) {
				fieldType = TMPL_FIELD_QUALITY;
			} else if (strcasecmp(key, "quantity") == 0) {
				fieldType = TMPL_FIELD_QUANTITY;
			} else if (strcasecmp(key, "height") == 0) {
				fieldType = TMPL_FIELD_HEIGHT;
			} else if (strcasecmp(key, "weight") == 0) {
				fieldType = TMPL_FIELD_WEIGHT;
			} else if (strcasecmp(key, "naturalwc") == 0) {
				fieldType = TMPL_FIELD_NATURALWC;
			} else if (strcasecmp(key, "naturalac") == 0) {
				fieldType = TMPL_FIELD_NATURALAC;
			} else if (strcasecmp(key, "resistances") == 0) {
				fieldType = TMPL_FIELD_RESISTANCES;
			} else if (strcasecmp(key, "immunities") == 0) {
				fieldType = TMPL_FIELD_IMMUNITIES;
			} else if (strcasecmp(key, "job") == 0) {
				fieldType = TMPL_FIELD_JOB;
			} else if (strcasecmp(key, "regionlimit") == 0) {
				fieldType = TMPL_FIELD_REGIONLIMIT;
				doRegionlimit = 1;
			} else if (strcasecmp(key, "friends") == 0) {
				fieldType = TMPL_FIELD_FRIENDS;
				doFriends = 1;
			}

			if (fieldType >= 0) {
				tmpl->rawFields[numRawFields].type = (uint32_t)fieldType;
				fieldMatched = 1;
			}

			// Binary inlines "type" subtype parsing here, before
			// the generic strdup/ResolveDefines step.
			if (fieldType == 0) {
				char subtypeBuf[128];
				GetValue(line, subtypeBuf);
				tmpl->type = 0;
				if (strcasecmp(subtypeBuf, "normal") == 0) {
					tmpl->type = 2;
					createVar = 1;
				} else if (strcasecmp(subtypeBuf, "guard") == 0) {
					tmpl->type = 3;
					createVar = 1;
				} else if (strcasecmp(subtypeBuf, "shopkeeper") == 0) {
					tmpl->type = 4;
					createVar = 1;
				} else if (strcasecmp(subtypeBuf, "item") == 0) {
					tmpl->type = 1;
				}
			}

			if (fieldMatched) {
				p = line;
				while (*p && isspace((unsigned char)*p))
					p++;

				tmpl->rawFields[numRawFields].data = strdup_ServerSide(p);

				CString_Constructor(&inputStr, tmpl->rawFields[numRawFields].data);
				resolveResult = CTemplateManager_ResolveDefines(&inputStr, &outputStr);
				CString_Destructor(&inputStr);

				if (resolveResult) {
					free(tmpl->rawFields[numRawFields].data);
					tmpl->rawFields[numRawFields].data = strdup_ServerSide(CString_GetBuffer(&outputStr));
				}

				p = tmpl->rawFields[numRawFields].data;
				numRawFields++;
				tmpl->numRawFields = numRawFields;
			}

			// Deferred tail field processing.
			if (doRegion) {
				CTemplateManager_AppendStringList(&tmpl->regionList, tmpl->rawFields[numRawFields - 1].data);
				doRegion = 0;
			}
			if (doRegionlimit) {
				CTemplateManager_AppendStringList(&tmpl->regionlimitList, tmpl->rawFields[numRawFields - 1].data);
				doRegionlimit = 0;
			}
			if (doFriends) {
				CTemplateManager_AppendStringList(&tmpl->friendsList, tmpl->rawFields[numRawFields - 1].data);
				doFriends = 0;
			}
			if (doFrequency) {
				tmpl->frequency = atoi(tmpl->rawFields[numRawFields - 1].data);
				doFrequency = 0;
			}
			if (doAlignment) {
				doAlignment = 0;
				tmpl->alignment = ParseAlignment(tmpl->rawFields[numRawFields - 1].data);
			}
			if (doMovetype) {
				tmpl->creatureHeight = atoi(tmpl->rawFields[numRawFields - 1].data);
				doMovetype = 0;
			}
			if (doResource) {
				doResource = 0;
				CTemplateManager_ParseResource(tmpl, tmpl->rawFields[numRawFields - 1].data);
			}
			if (doCorpsename) {
				SCommand_ParseToken(tmpl->rawFields[numRawFields - 1].data, key);
				CString_AssignCStr(&tmpl->corpseName, key);
				doCorpsename = 0;
			}
			if (doFleeval) {
				GetValue(tmpl->rawFields[numRawFields - 1].data, key);
				tmpl->fleeval = (uint8_t)atoi(key);
				doFleeval = 0;
			}

inner_done:
			if (doCreatesnpcs) {
				createVar = atoi(tmpl->rawFields[numRawFields - 1].data);
				doCreatesnpcs = 0;
			}

			if (line == NULL)
				break;
		}

		if (splitFlag)
			fclose_ServerSide(curFile);

		tmpl->createsnpcsTemplateId = (uint32_t)createVar;
		{
			uintptr_t keyBuf = (uint32_t)fileCounter;
			CResManager_InsertIntEntryF(&g_TemplatesRM, &keyBuf, tmpl);
		}

next_template:
		if (fileCounter++ >= 0x1000)
			break;
	}

cleanup:
	fclose_ServerSide(f);
	CString_Destructor(&outputStr);
	return 0;
}

/*
 * 0x004BF71F - CTemplateManager::CTemplateManager
 *
 * Constructs the templates, name table, and defines CResManagers and
 * zeroes the template name pointer cache.
 */
static __attribute__((unused)) void
CTemplateManager_Constructor(CTemplateManager *this)
{
	int i;

	CResManager_Constructor(&this->templates, 1);
	CResManager_Constructor(&this->nameTable, 1);
	CResManager_Constructor(&this->defines, 1);

	for (i = 0; i < TEMPLATE_NAME_SLOTS; i++) {
		this->names[i] = NULL;
	}
}

/*
 * 0x004BF7B8 - CTemplateManager::~CTemplateManager
 *
 * Frees cached template names, runs shutdown, then destroys the three
 * CResManagers in reverse order.
 */
static __attribute__((unused)) void
CTemplateManager_Destructor(CTemplateManager *this)
{
	int i;

	for (i = 0; i < TEMPLATE_NAME_SLOTS; i++) {
		if (this->names[i] != NULL)
			OperatorDelete(this->names[i]);
	}

	CTemplateManager_Shutdown();

	CResManager_Destructor_Defines(&this->defines);
	CResManager_Destructor_NameTable(&this->nameTable);
	CResManager_Destructor_Templates(&this->templates);
}

/*
 * 0x004BF86F - CTemplateManager::startup
 *
 * Loads the name table, defines, and templates in order.
 */
void
CTemplateManager_startup(void)
{
	CTemplateManager_LoadNameTable();
	CTemplateManager_LoadDefines();
	CTemplateManager_LoadTemplates();
}

/*
 * 0x004BF892 - CTemplateManager::Shutdown
 *
 * Clears the name table, defines, and templates CResManagers.
 */
void
CTemplateManager_Shutdown(void)
{
	CTemplateManager_ClearResManager2(NULL);
	CTemplateManager_ClearDefines(NULL);
	CTemplateManager_ClearTemplateHash(NULL);
}

/*
 * 0x004BF8B5 - CTemplateManager::CreateFromTemplate
 *
 * Top-level template spawn: validates the location, creates the entity,
 * applies the template the requested number of times, then runs
 * mobile/NPC/vendor post-setup (movement init, alignment, mask setup,
 * region placement, vendor stock validation).
 */
CItem *
CTemplateManager_CreateFromTemplate(uint16_t templateId, CLocation *loc, int restockFlag, int depth, CItem *existingMob)
{
	int amount;
	CItem *result;

	if (templateId == 0xC)
		g_SpawnType12Count++;

	g_SpawnTotalCount++;

	amount = 1;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int16_t)loc->x, (int16_t)loc->y)) {
		return NULL;
	}

	result = CTemplateManager_CreateEntity(templateId, loc, &amount, restockFlag, existingMob, depth);
	if (result == NULL)
		return NULL;

	if (existingMob != NULL) {
		if (result == existingMob)
			return existingMob;
	}

	CItem_AttachTemplate(result, templateId);

	if (!ValidateInWorld(result)) {
		result = NULL;
		return NULL;
	}

	if (restockFlag == 0) {
		CTemplateManager_ApplyTemplate(0, result, amount, loc, depth, 1);
	} else {
		if (!HasTemplateInManager(templateId)) {
			if (result != NULL)
				((void (*)(void *))VT_FN(result, VT_DELETE))(result);
			return NULL;
		}
		CTemplateManager_ApplyTemplate(1, result, amount, loc, depth, 1);
	}

	if (VT_IsMobile(result)) {
		CMobile *mob = (CMobile *)result;

		CMobile_InitMovementTypeFromTemplate(mob);

		if ((((int (*)(void *))VT_FN(result, VT_GET_MOVEMENT_TYPE))(result) & 0xFF) == 0)
			CMobile_SetStatusFlag(mob, 0x02, 1);

		{
			NPCTemplate *tmpl = CResManager_GetTemplateByID(templateId);
			CMobile_SetAlignment(mob, tmpl->alignment);
		}

		CMobile_SetupMasksFromObjVars(mob);
	}

	if (VT_IsNPC(result)) {
		CItem *npc = result;

		if (npc->resourceEntity.entity.removedFromWorld == 1)
			((void (*)(void *, CLocation *))VT_FN(npc, VT_DROP_AT_FEET))(npc, loc);

		if (npc->resourceEntity.entity.removedFromWorld == 0)
			CResourceEntity_NotifyPreModify(npc);

		CResourceEntity_NotifyPostModify(npc);

		if (npc->resourceEntity.entity.removedFromWorld == 0)
			CResourceEntity_NotifyPostModifyIfActive(npc);

		if ((CResourceEntity_GetBodyType(npc) & 0xFFFF) == 0x190 || (CResourceEntity_GetBodyType(npc) & 0xFFFF) == 0x191)
			CTemplateManager_SetupHumanNPC(npc, loc);

		CNPCManager_AddToActiveList(CMobile_GetSerial((CMobile *)npc));
	}

	if (VT_IsVendor(result) == 1) {
		CItem *vendor = result;

		if (!CTemplateManager_ValidateVendorStock(vendor)) {
			if (vendor != NULL)
				((void (*)(void *))VT_FN(vendor, VT_DELETE))(vendor);
			return NULL;
		}

		CMobile_VendorRestockTick((CMobile *)vendor);

		if (!CShopkeeper_CheckVendorHasStock((CMobile *)vendor)) {
			if (vendor != NULL)
				((void (*)(void *))VT_FN(vendor, VT_DELETE))(vendor);
			return NULL;
		}
	}

	return result;
}

/*
 * 0x004BFBAA - CResManager::GetTemplateByID
 *
 * Looks up a template by numeric ID in the templates CResManager.
 */
NPCTemplate *
CResManager_GetTemplateByID(uint16_t id)
{
	return (NPCTemplate *)CResManager_FindInt(&g_TemplatesRM, (uint32_t)id);
}

/*
 * 0x004BFBD7 - CTemplateManager::GetTemplateColorCount
 *
 * Returns 1 if bodyType is an equippable hair/beard layer item (used to
 * drive match_hair color propagation during vendor stock generation).
 */
int
CTemplateManager_GetTemplateColorCount(uint16_t bodyType)
{
	CWorld_LookupItemResource(bodyType);

	if (!(g_ItemTileData[bodyType].flags & TF_EQUIPPABLE))
		return 0;

	if (CWorld_GetItemLayer(bodyType) == 0x0B)
		return 1;
	if (CWorld_GetItemLayer(bodyType) == 0x10)
		return 1;

	return 0;
}

/*
 * 0x004BFC4C - CTemplateManager::SetupHumanNPC
 *
 * For human NPCs (body 0x190/0x191), looks up CITY_ and TOWN_ regions
 * at the NPC's location, sets npcTown to the matching town name, and
 * adds "BRITANNIA_<City>" conversation title fragments.
 */
static void
CTemplateManager_SetupHumanNPC(CItem *npc, CLocation *loc)
{
	int i;
	CNPC *cnpc = (CNPC *)npc;
	CResManager tempRM;
	CSearchCtx iterCtx;
	CSearchCtx nextCtx;
	CResList cityList, townList;
	CResListNode *node;
	char field[40];
	char nameBuf[768];
	CString titleStr;
	CString tempStr;

	CResListNode_Constructor_bin((CResListNode *)&cityList);
	CResListNode_Constructor_bin((CResListNode *)&townList);

	CResManager_Constructor(&tempRM, 1);

	RegionManager_SearchByPrefix(&cityList, loc, "CITY_");

	RegionManager_SearchByPrefix(&townList, loc, "TOWN_");

	node = CResList_Begin(&cityList);
	while (CResList_IsValid(&cityList, node)) {
		void *data = CResList_GetData(&cityList, node);
		CRegion *r = *(CRegion **)data;

		if (CTemplateManager_ParseDelimitedField(r->name, field, 40) == 1) {
			CString fieldStr;
			CString_Constructor(&fieldStr, field);
			CResManager_InsertStrEntry(&tempRM, &fieldStr, CResList_GetData(&cityList, node));
			CString_Destructor(&fieldStr);
		}
		node = CResList_Next(&cityList, node);
	}

	node = CResList_Begin(&townList);
	while (CResList_IsValid(&townList, node)) {
		CRegion *r = *(CRegion **)CResList_GetData(&townList, node);

		if (CTemplateManager_ParseDelimitedField(r->name, field, 40) == 1) {
			CString_DefaultConstructor(&tempStr);
			CString_AssignCStr(&tempStr, field);
			cnpc->npcTown = (char *)CScriptManager_InternString(&g_ScriptManager, CString_GetBuffer(&tempStr));
			CString_Destructor(&tempStr);
		}
		node = CResList_Next(&townList, node);
	}

	CString_DefaultConstructor(&titleStr);

	CResManager_BeginIter(&tempRM, &iterCtx);

	while (CSearchCtx_Find(&iterCtx)) {
		CString *keyStr;

		keyStr = (CString *)CResManager_GetKeyAtPos(&tempRM, &iterCtx);
		strcpy(nameBuf, CString_GetData(keyStr));
		nameBuf[0] = (char)toupper((unsigned char)nameBuf[0]);

		for (i = 1; nameBuf[i] != '\0'; i++) {
			if (nameBuf[i] == '@') {
				nameBuf[i] = ' ';
			} else if (nameBuf[i - 1] == ' ') {
				nameBuf[i] = (char)toupper((unsigned char)nameBuf[i]);
			} else {
				nameBuf[i] = (char)tolower((unsigned char)nameBuf[i]);
			}
		}

		CString_Constructor(&tempStr, nameBuf);
		cnpc->npcTown = (char *)CScriptManager_InternString(&g_ScriptManager, CString_GetBuffer(&tempStr));

		CString_AssignCStr(&titleStr, "BRITANNIA_");

		for (i = 0; nameBuf[i] != '\0'; i++) {
			if (nameBuf[i] == ' ') {
				nameBuf[i] = '\0';
				break;
			}
		}

		CString_AppendCStr(&titleStr, nameBuf);

		CTemplateManager_AddConvoTitle(npc, CString_GetCStr(&titleStr));

		CResManager_NextIter(&tempRM, &nextCtx, &iterCtx);
		CSearchCtx_Add(&iterCtx, &nextCtx);

		CString_Destructor(&tempStr);
	}

	CString_Destructor(&titleStr);
	CResManager_Destructor(&tempRM);
	CResList_Destructor_ByNameAllVal(&townList);
	CResList_Destructor_ByNameAllVal(&cityList);
}

/*
 * 0x004C013E - CTemplateManager::ParseDelimitedField
 *
 * Extracts the substring between the first two '_' characters of src
 * into dst (capped at maxlen-1). Returns 1 if a field was extracted.
 */
static int __attribute__((unused))
CTemplateManager_ParseDelimitedField(const char *src, char *dst, int maxlen)
{
	int count;

	count = 0;

	while (*src != '_') {
		if (*src == '\0')
			return 0;
		src++;
	}

	src++;

	while (*src != '_') {
		if (*src == '\0')
			break;
		dst[count] = *src;
		count++;
		src++;
		if (count >= maxlen - 1)
			break;
	}

	if (count == 0)
		return 0;

	dst[count] = '\0';
	return 1;
}

/*
 * 0x004C01DE - CTemplateManager::ValidateVendorStock
 *
 * Walks the vendor's stock container, computes a weighted gold total
 * (items > 2000 gp weigh 1x, > 1000 gp weigh 2x, else 9x), creates a
 * gold item of that value, and deposits it in the vendor's bank.
 * Returns 0 if the vendor has no valid stock.
 */
static int
CTemplateManager_ValidateVendorStock(CItem *vendor)
{
	int totalValue;
	CItem *stockContainer;
	CItem *cur;
	int goldAmount;
	int itemValue;
	int weight;
	void *newMem;
	CItem *goldItem;

	totalValue = 0;

	if (((CMobile *)vendor)->equipment[26] == NULL)
		return 0;

	if (!VT_IsMobile2(((CMobile *)vendor)->equipment[26]))
		return 0;

	stockContainer = ((CMobile *)vendor)->equipment[26];

	cur = ((CContainer *)stockContainer)->contents;
	while (cur != NULL) {
		itemValue = ((int (*)(CItem *, int, int))VT_FN(cur, VT_GET_VALUE))(cur, 0, 1);

		weight = 9;
		if (itemValue > 2000)
			weight = 1;
		else if (itemValue > 1000)
			weight = 2;

		totalValue += itemValue * weight;

		cur = cur->spatialNext;
	}

	goldAmount = totalValue;

	if ((unsigned int)goldAmount <= 0u)
		return 0;

	{
		CLocation *loc = CEntity_GetLocation(&vendor->resourceEntity.entity);
		if (!ResBankResourceCheck(loc, g_ResTypeId_Gold, goldAmount))
			return 0;
	}

	newMem = OperatorNew(sizeof(CItem));
	if (newMem != NULL)
		goldItem = CItem_Constructor(newMem);
	else
		goldItem = NULL;

	if (goldItem == NULL)
		return 0;

	CEntity_SetBodyType(goldItem, 0xEED);

	CResourceEntity_AddNodeScaled(goldItem, (uint16_t)g_ResTypeId_Gold, 3, goldAmount, 0, goldAmount, 0, 1, 1);

	{
		int result = CMobile_PutMoneyInBank((CMobile *)vendor, goldItem, goldAmount);

		if (result != -1 && goldItem != NULL) {
			((void (*)(CItem *))VT_FN(goldItem, VT_DELETE))(goldItem);
		}
	}

	return 1;
}

/*
 * 0x004C0397 - CTemplateManager::ApplyTemplate
 *
 * Runs CItem_Setup on the entity amount times. For restocks, gates
 * each iteration on ResBankLimitCheck or HasTemplateInManager depending
 * on the zero flag. Hides the entity during the loop if it was visible.
 */
void
CTemplateManager_ApplyTemplate(int isRestock, CItem *entity, int amount, CLocation *loc, int depth, int zero)
{
	int wasVisible;
	unsigned int i;

	if (amount == 0)
		amount = 1;

	wasVisible = 0;

	if (entity->resourceEntity.entity.removedFromWorld == 0) {
		((void (*)(CItem *))VT_FN(entity, VT_HIDE))(entity);
		wasVisible = 1;
	}

	for (i = 0; i < (unsigned int)amount; i++) {
		if (isRestock != 0) {
			if (zero == 0) {
				if (!ResBankLimitCheck(entity, loc))
					break;
			} else {
				uint16_t tmplIdx = (uint16_t)CResourceEntity_GetTemplateIndex(entity);
				if (!HasTemplateInManager(tmplIdx))
					break;
			}
		}

		CItem_Setup(entity, depth, loc, zero, 1);
	}

	if (wasVisible != 0) {
		((void (*)(CItem *))VT_FN(entity, VT_RETURN_TO_TRACKED))(entity);
	}
}

/*
 * 0x004C0462 - CTemplateManager::HasAnimationVariant
 *
 * Returns 1 for built-in body IDs (0..0x5DB). For higher IDs, looks up
 * the template and returns its createsnpcsTemplateId field.
 */
int
CTemplateManager_HasAnimationVariant(int bodyId)
{
	CSearchCtx ctx;
	NPCTemplate *tmpl;

	if (bodyId >= 0 && bodyId <= 0x5DB)
		return 1;
	CResManager_FindByIntCtx(&g_TemplatesRM, &ctx, (uint32_t *)&bodyId, 1);
	if (!CSearchCtx_Find(&ctx))
		return 0;
	tmpl = (NPCTemplate *)CResManager_GetResultCtx(&g_TemplatesRM, &ctx);
	return tmpl->createsnpcsTemplateId;
}

/*
 * 0x004C04BB - CTemplateManager::HasAnimation
 *
 * Returns 1 if the template exists and has type==4 (shopkeeper).
 */
int
CTemplateManager_HasAnimation(uint16_t templateId)
{
	CSearchCtx ctx;
	NPCTemplate *tmpl;
	uint32_t key;

	key = (uint32_t)templateId;
	CResManager_FindByIntCtx(&g_TemplatesRM, &ctx, &key, 1);
	if (!CSearchCtx_Find(&ctx))
		return 0;

	tmpl = (NPCTemplate *)CResManager_GetResultCtx(&g_TemplatesRM, &ctx);
	return tmpl->type == 4;
}

/*
 * 0x004C0508 - CTemplateManager::ValidateOwner
 *
 * Confirms the mob at the given serial still matches the expected
 * pointer. Logs an error and returns 0 if the mob was deleted.
 */
int
CTemplateManager_ValidateOwner(uint32_t serial, CItem *mob)
{
	CItem *found;

	found = CWorld_FindBySerial(g_World, serial);
	if (found == mob)
		return 1;

	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "restemplate", "error", "In-use template object deleted");
	return 0;
}

/*
 * 0x004C05C0 - CTemplateData::CTemplateData
 *
 * Default-constructs a template: three empty string lists, an empty
 * corpse-name CString, and the scalar defaults.
 *
 * Takes NPCTemplate rather than CTemplateData, for the same reason
 * CTemplateManager::LoadTemplates does. CTemplateData mirrors the binary's
 * raw 0x1060-byte layout and nothing reads a field of it; NPCTemplate is the
 * struct the rest of the server uses, and it holds resourceNodes as a real
 * pointer so it widens correctly on 64-bit. The binary's field at 0x1008 has
 * no NPCTemplate counterpart and nothing reads it, so it is not set here.
 */
static NPCTemplate *
CTemplateData_Constructor(NPCTemplate *this)
{
	CStringList_Init(&this->regionList);
	CStringList_Init(&this->regionlimitList);
	CStringList_Init(&this->friendsList);

	CString_DefaultConstructor(&this->corpseName);

	this->numRawFields = 0;
	this->resourceNodes = NULL;
	this->frequency = 0;
	this->creatureHeight = -1;
	this->alignment = 0;
	this->fleeval = 0x10;
	this->type = 0;
	this->createsnpcsTemplateId = 0;

	return this;
}

/*
 * 0x004C0F70 - CTemplateManager::ApplyLabel
 *
 * Finds-or-inserts a label entry and stores *entity as its value.
 * Returns 0 on the dedup path (unreachable in practice since
 * g_TemplatesRM.flags==0).
 */
int
CTemplateManager_ApplyLabel(CString *label, CItem **entity)
{
	CSearchCtx searchCtx;
	CSearchCtx output;
	CSearchCtx *result;
	CResListNode *valNode;
	uint32_t bucket;

	CSearchCtx_Constructor(&searchCtx);

	result = CTemplateManager_FindFieldByLabel(&g_TemplatesRM, &output, label);
	CSearchCtx_Add(&searchCtx, result);

	if (!CSearchCtx_Find(&searchCtx))
		return 0;

	valNode = (CResListNode *)CSearchCtx_GetValNode(&searchCtx);
	bucket = CSearchCtx_GetBucket(&searchCtx);
	CResManager_ApplyTemplateData(g_TemplatesRM.vals[bucket], valNode, entity);

	return 1;
}

/*
 * 0x004C10A0 - CStringPairList::~CStringPairList (dtor wrapper)
 *
 * Forwards to the destructor, which removes every node.
 */
void
CStringPairList_Init(CStringPairList *sl)
{
	CStringPairList_Destructor((CResList *)sl);
}

/*
 * 0x004C10C0 - CStringPairList::GetWeightedRandom
 *
 * Picks a node weighted by the stored weights and copies its iterator
 * into *output. Returns a default (empty) iterator when the list is
 * empty or no node has nonzero weight.
 */
static CResListNode **
CStringPairList_GetWeightedRandom(CStringPairList *sl, CResListNode **output)
{
	CResListNode *localCtx;
	int cumWeight = 0;
	int target;
	CResListNode *pos;
	int flag;

	flag = 0;
	CResBankMagicCtx_DefaultConstructor(&localCtx);

	if (sl->totalWeight <= 0) {
		CResBankMagicCtx_Copy(output, &localCtx);
		flag |= 1;
		CResBankMagicCtx_PostInit(&localCtx);
		return output;
	}

	target = rand() % sl->totalWeight + 1;
	pos = CResList_Begin(&sl->list);
	while (CResList_IsValid(&sl->list, pos)) {
		CStringPairListNode *data = (CStringPairListNode *)CResList_GetData(&sl->list, pos);
		if (data->type == 1) {
			data = (CStringPairListNode *)CResList_GetData(&sl->list, pos);
			if (data->weight != 0) {
				data = (CStringPairListNode *)CResList_GetData(&sl->list, pos);
				cumWeight += data->weight;
				if (target <= cumWeight) {
					localCtx = pos;
					CResBankMagicCtx_Copy(output, &localCtx);
					flag |= 1;
					CResBankMagicCtx_PostInit(&localCtx);
					return output;
				}
			}
		}
		pos = CResList_Next(&sl->list, pos);
	}

	CResBankMagicCtx_Copy(output, &localCtx);
	flag |= 1;
	CResBankMagicCtx_PostInit(&localCtx);
	return output;

	USED(flag);
}

/*
 * 0x004C1220 - CStringPairList::AddWeighted
 *
 * Allocates a new CStringPairListNode (default CStrings, given weight)
 * and inserts it - appended when direction==1, otherwise unordered.
 * Updates totalWeight and writes the node's iterator to *output.
 */
static CResListNode **
CStringPairList_AddWeighted(CStringPairList *sl, CResListNode **output, int weight, int direction)
{
	CStringPairListNode *raw;
	void *constructed;
	CResListNode *node;
	CResListNode *pos;
	int flag;

	flag = 0;
	raw = (CStringPairListNode *)OperatorNew(sizeof(CStringPairListNode));
	if (raw != NULL) {
		CString_DefaultConstructor(&raw->first);
		CString_DefaultConstructor(&raw->second);
		raw->type = 1;
		raw->weight = weight;
		constructed = raw;
	} else {
		constructed = NULL;
	}

	if (direction == 1)
		node = CResList_FindOrAddPair(&sl->list, constructed);
	else
		node = CResList_AllocForward_Labels(&sl->list, constructed);
	CIterCtx_Set(&pos, node);

	if (CStringList_HasEntries((CStringList *)&pos))
		sl->totalWeight += weight;

	CResBankMagicCtx_Copy(output, &pos);
	flag |= 1;
	CResBankMagicCtx_PostInit(&pos);
	return output;

	USED(flag);
}

/*
 * 0x004C1B60 - CTemplateManager::FindFieldByLabel
 *
 * Find-or-insert on a CResManager keyed by case-insensitive label.
 * Hashes into 66 buckets, lazily allocates the bucket lists, skips
 * insertion with an empty iterator when flags==1 and the label already
 * exists (dedup), otherwise inserts key+value and returns the new
 * iterator.
 */
static CSearchCtx *
CTemplateManager_FindFieldByLabel(CResManager *rm, CSearchCtx *output, CString *label)
{
	CSearchCtx localCtx;
	uint32_t bucket;
	CResListNode *keyNode, *valNode;

	CSearchCtx_Constructor(&localCtx);

	bucket = ResManager_HashStrA(label, 0x41);

	if (rm->keys[bucket] == NULL) {
		CResList *keyList = (CResList *)OperatorNew(sizeof(CResList));
		if (keyList != NULL)
			CResListNode_Constructor_bin((CResListNode *)keyList);
		rm->keys[bucket] = keyList;

		CResList *valList = (CResList *)OperatorNew(sizeof(CResList));
		if (valList != NULL)
			CResListNode_Constructor_bin((CResListNode *)valList);
		rm->vals[bucket] = valList;
	}

	if (rm->flags == 1) {
		CResListNode *found = CResList_FindByString(rm->keys[bucket], label, NULL, 1);
		if (found != NULL) {
			CResManager_CreateBucket(output, &localCtx);
			return output;
		}
	}

	keyNode = CResList_InsertTailStr(rm->keys[bucket], label);
	CSearchCtx_SetKeyNode(&localCtx, (uintptr_t)keyNode);

	valNode = CResList_BeginIterJ(rm->vals[bucket]);
	CSearchCtx_SetValNode(&localCtx, (uintptr_t)valNode);

	CSearchCtx_SetBucket(&localCtx, bucket);
	CSearchCtx_SetEntity(&localCtx, 1);

	rm->count++;

	CResManager_CreateBucket(output, &localCtx);
	return output;
}

/*
 * 0x004C1CF0 - CResManager::ApplyTemplateData
 *
 * Stores *entity into valNode->data if valNode is non-NULL.
 */
void
CResManager_ApplyTemplateData(CResList *list, CResListNode *valNode, CItem **entity)
{
	USED(list);
	if (valNode == NULL)
		return;
	CResListNode_SetDataInt(valNode, entity);
}

/*
 * 0x004C29E0 - CTemplate::ScalarDelete
 *
 * Scalar deleting destructor: runs CTemplate_Destructor and frees the
 * object when flags & 1.
 */
__attribute__((unused)) void *
CTemplate_ScalarDelete(NPCTemplate *this, int flags)
{
	CTemplate_Destructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x004C2A10 - CTemplate::~CTemplate
 *
 * Releases the resource node list to the pool, frees raw field data,
 * and destroys the CString and CStringList members.
 */
static void
CTemplate_Destructor(NPCTemplate *tmpl)
{
	CResourceNode *node;
	CResourceNode *nextNode;
	int i;

	node = tmpl->resourceNodes;
	while (node != NULL) {
		nextNode = node->next;
		node->next = NULL;
		node->prev = NULL;
		ResourceNode_ReturnToPool(node);
		node = nextNode;
	}

	for (i = 0; i < tmpl->numRawFields; i++) {
		OperatorDelete(tmpl->rawFields[i].data);
	}

	CString_Destructor(&tmpl->corpseName);

	CStringList_Destroy(&tmpl->friendsList);

	CStringList_Destroy(&tmpl->regionlimitList);

	CStringList_Destroy(&tmpl->regionList);
}
