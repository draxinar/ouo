/*
 * Magic item factory - property table for magical weapons, scrolls,
 * and wands.
 *
 * Loads the magic-effect definitions from data files and assembles
 * CMagicList property bundles when a magic item is generated, consumed,
 * or identified.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "container.h"
#include "dat.h"
#include "filemanager.h"
#include "io.h"
#include "log.h"
#include "magicfactory.h"
#include "magiclist.h"
#include "utils.h"
#include "wombat_compile.h"

static CString *CItemEffectDef_GetSubcatStr(CItemEffectDef *def); // 0x00467040
static CString *CItemEffectDef_GetCategoryStr(CItemEffectDef *def); // 0x00467020
static CItemEffectDef *CItemEffectDef_Destructor(CItemEffectDef *def); // 0x00466199
static CItemEffectDef *CItemEffectDef_Constructor(CItemEffectDef *def); // 0x00466040
static void CEffectTableEntry_Destructor(CEffectTableEntry *this); // 0x00441C70
static CEffectTableEntry *CEffectTableEntry_DestructorB(CEffectTableEntry *this); // 0x00441C50
static CResList *CItemEffectDef_GetList2(CItemEffectDef *def); // 0x0043EA10
static CResList *CItemEffectDef_GetList1(CItemEffectDef *def); // 0x0043E9F0
static void CItemEffectDef_SetMaxVal(CItemEffectDef *def, int maxVal); // 0x0043E9D0
static void CItemEffectDef_SetMinVal(CItemEffectDef *def, int minVal); // 0x0043E9B0
static int CItemEffectDef_GetMaxVal(CItemEffectDef *def); // 0x0043E8B0
static int CItemEffectDef_GetMinVal(CItemEffectDef *def); // 0x0043E870
static void CItemEffectDef_SetSubcat(CItemEffectDef *def, CString *name); // 0x0043E850
static void CItemEffectDef_SetCategory(CItemEffectDef *def, CString *name); // 0x0043E830
static void CItemEffectDef_SetName(CItemEffectDef *def, CString *name); // 0x0043E810
static int CMagicItemFactory_CountRemovalsInternal(CMagicItemFactory *factory, CResManager *effectsMap); // 0x0043CEF9
static int CMagicItemFactory_DeepResolve(CMagicItemFactory *factory, CResManager *eRM, int randomLevel, int maxDelta, CResManager *pRM, int flag); // 0x0043CC46
static int CMagicItemFactory_ProcessPropsB(CMagicItemFactory *factory, CResManager *eRM, CResList *effList, CResManager *pRM); // 0x0043C9C4
static int CMagicItemFactory_ProcessPropsA(CMagicItemFactory *factory, CResManager *eRM, CResList *keys); // 0x0043C7AF
static void CMagicItemFactory_ExpandIncludes660(CMagicItemFactory *factory, int flag); // 0x0043C733
static void CMagicItemFactory_ExpandIncludes18(CMagicItemFactory *factory, int flag); // 0x0043C6C0
static int CMagicItemFactory_FindNameInList(CMagicItemFactory *factory, CString *name, CStringList *list); // 0x0043C514
static int CMagicItemFactory_FindNameInKeys(CMagicItemFactory *factory, CString *name, CResList *list); // 0x0043C364
static uint16_t CMagicItemFactory_ResolveItemId(CMagicItemFactory *factory, CStringList *list, CResManager *incRM); // 0x0043C19C
static int CMagicItemFactory_CountRemovals(CMagicItemFactory *factory, CResManager *effectsRM); // 0x0043C168
static int CMagicItemFactory_PruneInvalidEntries(CMagicItemFactory *factory, CStringList *list, CResManager *includeRM); // 0x0043BFF4
static int CMagicItemFactory_ResolveRandomValue(CMagicItemFactory *factory, CString *propName, int randomLevel, int maxDelta, int *doneFlag); // 0x0043BEFD
static intptr_t CMagicItemFactory_ResolveMainProp(CMagicItemFactory *factory, CString *propName, CString *mainStr, CResManager *effectsRM); // 0x0043BD5E
static int CMagicItemFactory_ReadAndExecuteFile(CMagicItemFactory *factory, CString *filename); // 0x0043BC3A
static int CMagicItemFactory_ReadFileContents(CMagicItemFactory *factory, CString *output, CString *filename); // 0x0043BAC1
static int CMagicItemFactory_ReadInteger(
        CMagicItemFactory *factory, CString *outStr, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043B649
static int CMagicItemFactory_ReadIdentifier(
        CMagicItemFactory *factory, CString *outStr, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043B5D6
static int CMagicItemFactory_ParseSubBlock(
        CMagicItemFactory *factory, CResList *list, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043B3DE
static int CMagicItemFactory_ProcessWeightedList(
        CMagicItemFactory *factory, CStringList *list, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043B079
static int CMagicItemFactory_ProcessEffectTable(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043A616
static int CMagicItemFactory_ProcessRunFile(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043A3A8
static int CMagicItemFactory_ProcessDebug(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043A14B
static int CMagicItemFactory_ProcessReset(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043A02A
static int CMagicItemFactory_ProcessGlobalScripts(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x00439DEA
static int CMagicItemFactory_ProcessEffectList(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x0043990B
static int CMagicItemFactory_ProcessItemList(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x00439432
static int CMagicItemFactory_ProcessItemEffect(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x00438868
static int CMagicItemFactory_ReadToken(
        CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *outToken, CString *delimiters, CString *whitespace, CString *lineDelimiter); // 0x00438783
static void CMagicItemFactory_LogInfo(CMagicItemFactory *factory, const char *msg, int lineNum); // 0x004386AC
static void CMagicItemFactory_LogError(CMagicItemFactory *factory, const char *msg, int lineNum); // 0x0043869F
static void CMagicItemFactory_Reset(CMagicItemFactory *factory); // 0x00438632
static void CMagicItemFactory_Destructor(CMagicItemFactory *factory); // 0x004385A2

/*
 * 0x004384E0 - CMagicItemFactory::CMagicItemFactory
 *
 * Initializes the two CResLists, four CResManagers, and min/max
 * effect IDs on the CMagicItemFactory singleton.
 */
void
CMagicItemFactory_Constructor(CMagicItemFactory *factory)
{
	CResListNode_Constructor_bin((CResListNode *)&factory->globalScripts);
	CResListNode_Constructor_bin((CResListNode *)&factory->itemList);
	CResManager_Constructor(&factory->includeRM, 1);
	CResManager_Constructor(&factory->keysRM, 0);
	CResManager_Constructor(&factory->propsRM, 0);
	CResManager_Constructor(&factory->effectTblRM, 0);
	factory->debugFlag = 0;
	factory->minEffectId = -1;
	factory->maxEffectId = -1;
}

/*
 * 0x004385A2 - CMagicItemFactory::~CMagicItemFactory
 *
 * Tears down the CResManagers and CResLists in reverse construction
 * order.
 */
static __attribute__((unused)) void
CMagicItemFactory_Destructor(CMagicItemFactory *factory)
{
	CResManager_ClearT(&factory->effectTblRM);
	CResManager_ClearC(&factory->propsRM);
	CResManager_ClearS(&factory->keysRM);
	CResManager_ClearI(&factory->includeRM);
	CResList_DestructorA((CResList *)&factory->itemList);
	CResList_DestructorA(&factory->globalScripts);
}

/*
 * 0x00438632 - CMagicItemFactory::Reset
 *
 * Empties all factory CResLists/CResManagers and clears the min/max
 * effect IDs.
 */
static void
CMagicItemFactory_Reset(CMagicItemFactory *factory)
{
	CResList_ClearInternal(&factory->globalScripts);
	CResList_ClearInternal((CResList *)&factory->itemList);
	CResManager_ClearI(&factory->includeRM);
	CResManager_ClearS(&factory->keysRM);
	CResManager_ClearC(&factory->propsRM);
	CResManager_ClearT(&factory->effectTblRM);
	factory->minEffectId = -1;
	factory->maxEffectId = -1;
}

/*
 * 0x0043869F - CMagicItemFactory::LogError
 *
 * Binary no-op error handler.
 *
 * MODIFIED: with LOG_VERBOSE, routes the message through EventLogger_Log
 * under the "magicfactory" category.
 */
static void
CMagicItemFactory_LogError(CMagicItemFactory *factory, const char *msg, int lineNum)
{
#ifdef LOG_VERBOSE
	char buf[512];
	USED(factory);
	snprintf(buf, sizeof(buf), "error: line %d: %s", lineNum, msg);
	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "magicfactory", "error", buf);
#else
	USED(factory);
	USED(msg);
	USED(lineNum);
#endif
}

/*
 * 0x004386AC - CMagicItemFactory::LogInfo
 *
 * Binary no-op info handler.
 *
 * MODIFIED: with LOG_VERBOSE, routes the message through EventLogger_Log
 * under the "magicfactory" category.
 */
static void
CMagicItemFactory_LogInfo(CMagicItemFactory *factory, const char *msg, int lineNum)
{
#ifdef LOG_VERBOSE
	char buf[512];
	USED(factory);
	snprintf(buf, sizeof(buf), "info: line %d: %s", lineNum, msg);
	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "magicfactory", "info", buf);
#else
	USED(factory);
	USED(msg);
	USED(lineNum);
#endif
}

/*
 * 0x00438783 - CMagicItemFactory::ReadToken
 *
 * Reads the next non-comment token via CString_Tokenize, skipping
 * line and nested block comments in the source text.
 */
static int
CMagicItemFactory_ReadToken(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *outToken, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	int commentDepth = 0;

	for (;;) {
		if (!CString_Tokenize(buf, pos, lineCount, outToken, delimiters, whitespace, 1))
			return 0;

		if (CString_CompareStr(outToken, "//")) {
			if (!CString_SkipToDelimiter(buf, pos, lineCount, lineDelimiter, 1))
				return 0;
			continue;
		}

		if (CString_CompareStr(outToken, "/*")) {
			commentDepth++;
			continue;
		}

		if (CString_CompareStr(outToken, "*/")) {
			commentDepth--;
			if (commentDepth < 0)
				CMagicItemFactory_LogError(factory, "Unexpected */", *lineCount);
			continue;
		}

		if (commentDepth == 0)
			return 1;
	}
}

/*
 * 0x00438868 - CMagicItemFactory::ProcessItemEffect
 *
 * Parses an "itemeffect" definition of the form
 *   NAME = { fieldname category subcategory min max { list2 } { list1 } } ;
 * into a new CItemEffectDef inserted into propsRM, updating the
 * factory's min/max effect IDs.
 */
static int
CMagicItemFactory_ProcessItemEffect(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString nameStr, tokenStr, logStr;
	CString errStr;
	CItemEffectDef *def;
	CSearchCtx ctx;
	int result;
	int val;
	CResManager *propsRM = &factory->propsRM;

	CString_DefaultConstructor(&nameStr);
	CString_DefaultConstructor(&tokenStr);

	if (!CMagicItemFactory_ReadIdentifier(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Constructor(&errStr, "Expected identifier, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup;
	}

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter) || CString_CompareNoCase(&tokenStr, "=") != 0) {
		CString_Constructor(&errStr, "Expecting =, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup;
	}

	def = (CItemEffectDef *)malloc(sizeof(CItemEffectDef));
	if (def != NULL)
		def = CItemEffectDef_Constructor(def);

	CResManager_FindOrInsertC(propsRM, &ctx, &nameStr);
	if (!CSearchCtx_Find(&ctx)) {
		if (def != NULL)
			CItemEffectDef_ScalarDelete(def, 1);
		CString_Constructor(&errStr, "Itemeffect ");
		CString_ConcatCString(&errStr, &nameStr);
		CString_AppendCStr(&errStr, " already exists");
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup;
	}

	CResManager_InsertValueAtCtxC(propsRM, &ctx, def);

	CString_Constructor(&logStr, "Created itemeffect ");
	CString_ConcatCString(&logStr, &nameStr);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		result = 0;
		goto cleanup_log;
	}
	if (CString_CompareNoCase(&tokenStr, "{") != 0) {
		CString_Constructor(&errStr, "Expected {, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		result = 0;
		CString_Destructor(&errStr);
		goto cleanup_log;
	}

	if (!CMagicItemFactory_ReadIdentifier(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Constructor(&errStr, "Expected identifier, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup_log;
	}
	CItemEffectDef_SetName(def, &nameStr);

	if (!CMagicItemFactory_ReadIdentifier(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Constructor(&errStr, "Expected identifier, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup_log;
	}
	CItemEffectDef_SetCategory(def, &nameStr);

	if (!CMagicItemFactory_ReadIdentifier(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Constructor(&errStr, "Expected identifier, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup_log;
	}
	CItemEffectDef_SetSubcat(def, &nameStr);

	if (!CMagicItemFactory_ReadInteger(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Constructor(&errStr, "Expected integer, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup_log;
	}
	val = CString_GetString(&nameStr);
	CItemEffectDef_SetMinVal(def, val);

	if (factory->minEffectId == -1 || CString_GetString(&nameStr) < factory->minEffectId)
		factory->minEffectId = CString_GetString(&nameStr);

	if (!CMagicItemFactory_ReadInteger(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Constructor(&errStr, "Expected integer, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		result = 0;
		goto cleanup_log;
	}
	val = CString_GetString(&nameStr);
	CItemEffectDef_SetMaxVal(def, val);

	if (factory->maxEffectId == -1 || CString_GetString(&nameStr) > factory->maxEffectId)
		factory->maxEffectId = CString_GetString(&nameStr);

	if (!CMagicItemFactory_ParseSubBlock(factory, CItemEffectDef_GetList2(def), buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		result = 0;
		goto cleanup_log;
	}

	if (!CMagicItemFactory_ParseSubBlock(factory, CItemEffectDef_GetList1(def), buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		result = 0;
		goto cleanup_log;
	}

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		result = 0;
		goto cleanup_log;
	}
	if (CString_CompareNoCase(&tokenStr, "}") != 0) {
		CString_Constructor(&errStr, "Expected }, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		result = 0;
		CString_Destructor(&errStr);
		goto cleanup_log;
	}

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		result = 0;
		goto cleanup_log;
	}
	if (CString_CompareNoCase(&tokenStr, ";") != 0) {
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		result = 0;
		CString_Destructor(&errStr);
		goto cleanup_log;
	}

	result = 1;

cleanup_log:
	CString_Destructor(&logStr);
cleanup:
	CString_Destructor(&tokenStr);
	CString_Destructor(&nameStr);
	return result;
}

/*
 * 0x00439432 - CMagicItemFactory::ProcessItemList
 *
 * Handles the "itemlist" command: allocates a CStringList, inserts
 * it into includeRM, fills it via ProcessWeightedList, and consumes
 * the trailing ";".
 */
static int
CMagicItemFactory_ProcessItemList(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString nameStr, tokenStr;
	CSearchCtx searchCtx;
	CStringList *newList;
	CString errStr, logStr;
	int readResult;
	CResManager *itemsRM = &factory->includeRM;

	CString_DefaultConstructor(&nameStr);
	CString_DefaultConstructor(&tokenStr);

	readResult = CMagicItemFactory_ReadIdentifier(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter);
	if (!readResult) {
		CString_Constructor(&errStr, "Expected identifier, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	readResult = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter);
	if (readResult == 0 || CString_CompareNoCase(&tokenStr, "=") != 0) {
		CString_Constructor(&errStr, "Expecting =, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	newList = (CStringList *)malloc(sizeof(CStringList));
	if (newList != NULL)
		CStringList_Init(newList);

	CResManager_FindOrInsertInclude(itemsRM, &searchCtx, &nameStr);
	if (!CSearchCtx_Find(&searchCtx)) {
		if (newList != NULL)
			CStringList_ScalarDelete(newList, 1);
		CString_Constructor(&errStr, "Itemlist ");
		CString_ConcatCString(&errStr, &nameStr);
		CString_AppendCStr(&errStr, " already exists");
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	CResManager_InsertValueAtCtxI(itemsRM, &searchCtx, newList);

	CString_Constructor(&logStr, "Created itemlist ");
	CString_ConcatCString(&logStr, &nameStr);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);

	readResult = CMagicItemFactory_ProcessWeightedList(factory, newList, buf, pos, lineCount, delimiters, whitespace, lineDelimiter);
	if (readResult == 0) {
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	readResult = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter);
	if (readResult == 0) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}
	if (CString_CompareNoCase(&tokenStr, ";") != 0) {
		// Binary constructs error string but does not log it.
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CString_Destructor(&errStr);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	CString_Destructor(&logStr);
	CString_Destructor(&tokenStr);
	CString_Destructor(&nameStr);
	return 1;
}

/*
 * 0x0043990B - CMagicItemFactory::ProcessEffectList
 *
 * Handles the "effectlist" command: allocates a CResList, inserts
 * it into keysRM, fills it via ParseSubBlock, and consumes the
 * trailing ";".
 */
static int
CMagicItemFactory_ProcessEffectList(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString nameStr, tokenStr;
	CSearchCtx searchCtx;
	CResList *newList;
	CString errStr, logStr;
	int readResult;
	CResManager *effectsListRM = &factory->keysRM;

	CString_DefaultConstructor(&nameStr);
	CString_DefaultConstructor(&tokenStr);

	readResult = CMagicItemFactory_ReadIdentifier(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter);
	if (!readResult) {
		CString_Constructor(&errStr, "Expected identifier, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	readResult = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter);
	if (readResult == 0 || CString_CompareNoCase(&tokenStr, "=") != 0) {
		CString_Constructor(&errStr, "Expecting =, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	newList = (CResList *)malloc(sizeof(CResList));
	if (newList != NULL)
		CResListNode_Constructor_bin((CResListNode *)newList);

	CResManager_FindOrInsertSecond(effectsListRM, &searchCtx, &nameStr);
	if (!CSearchCtx_Find(&searchCtx)) {
		if (newList != NULL)
			CResList_ScalarDeleteA(newList, 1);
		CString_Constructor(&errStr, "effectlist ");
		CString_ConcatCString(&errStr, &nameStr);
		CString_AppendCStr(&errStr, " already exists");
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	CResManager_InsertValueAtCtxS(effectsListRM, &searchCtx, newList);

	CString_Constructor(&logStr, "Created effectlist ");
	CString_ConcatCString(&logStr, &nameStr);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);

	readResult = CMagicItemFactory_ParseSubBlock(factory, newList, buf, pos, lineCount, delimiters, whitespace, lineDelimiter);
	if (readResult == 0) {
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	readResult = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter);
	if (readResult == 0) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}
	if (CString_CompareNoCase(&tokenStr, ";") != 0) {
		// Binary constructs error string but does not log it.
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CString_Destructor(&errStr);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	CString_Destructor(&logStr);
	CString_Destructor(&tokenStr);
	CString_Destructor(&nameStr);
	return 1;
}

/*
 * 0x00439DEA - CMagicItemFactory::ProcessGlobalScripts
 *
 * Handles the "globalscripts" command: clears the globalScripts list
 * and refills it via ParseSubBlock, then consumes the trailing ";".
 */
static int
CMagicItemFactory_ProcessGlobalScripts(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString tokenStr;
	CString tokenStr2;
	CString errStr;

	CString_DefaultConstructor(&tokenStr);
	CString_DefaultConstructor(&tokenStr2);

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr2, delimiters, whitespace, lineDelimiter) || CString_CompareNoCase(&tokenStr2, "=") != 0) {
		CString_Constructor(&errStr, "Expecting =, found ");
		CString_ConcatCString(&errStr, &tokenStr2);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr2);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CResList_ClearInternal(&factory->globalScripts);

	if (!CMagicItemFactory_ParseSubBlock(factory, &factory->globalScripts, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Destructor(&tokenStr2);
		CString_Destructor(&tokenStr);
		return 0;
	}

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr2, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&tokenStr2);
		CString_Destructor(&tokenStr);
		return 0;
	}

	if (CString_CompareNoCase(&tokenStr2, ";") != 0) {
		// Binary bug: builds error string but never logs it
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr2);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr2);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CString_Destructor(&tokenStr2);
	CString_Destructor(&tokenStr);
	return 1;
}

/*
 * 0x0043A02A - CMagicItemFactory::ProcessReset
 *
 * Handles the "reset" command: logs it, calls
 * CMagicItemFactory_Reset, and consumes the trailing ";".
 *
 * The binary builds an "Expected ;, found ..." error string but
 * never logs it; preserved verbatim.
 */
static int
CMagicItemFactory_ProcessReset(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString tokenStr;
	CString errStr;

	CString_DefaultConstructor(&tokenStr);

	CMagicItemFactory_LogError(factory, "Reset", *lineCount);

	CMagicItemFactory_Reset(factory);

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&tokenStr);
		return 0;
	}

	if (CString_CompareNoCase(&tokenStr, ";") != 0) {
		// Build error string but never log it (binary bug)
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CString_Destructor(&tokenStr);
	return 1;
}

/*
 * 0x0043A14B - CMagicItemFactory::ProcessDebug
 *
 * Handles the "debug on|off" command and logs "Debugging <state>".
 *
 * The binary builds an "Expected ;, found ..." error string for the
 * trailing-token mismatch but never logs it; preserved verbatim.
 */
static int
CMagicItemFactory_ProcessDebug(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString tokenStr;
	CString logStr;
	CString errStr;

	CString_DefaultConstructor(&tokenStr);

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&tokenStr);
		return 0;
	}

	if (CString_CompareStr(&tokenStr, "on")) {
		factory->debugFlag = 1;
	} else if (CString_CompareStr(&tokenStr, "off")) {
		factory->debugFlag = 0;
	} else {
		CString_Constructor(&errStr, "Expected on or off, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CString_Constructor(&logStr, "Debugging ");
	CString_ConcatCString(&logStr, &tokenStr);
	CMagicItemFactory_LogError(factory, CString_GetBuffer(&logStr), *lineCount);

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		return 0;
	}

	if (CString_CompareNoCase(&tokenStr, ";") != 0) {
		// Build error string but never log it (binary bug)
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CString_Destructor(&errStr);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CString_Destructor(&logStr);
	CString_Destructor(&tokenStr);
	return 1;
}

/*
 * 0x0043A3A8 - CMagicItemFactory::ProcessRunFile
 *
 * Handles the "runfile NAME;" command: reads the filename, runs it
 * through ReadAndExecuteFile, and consumes the trailing ";".
 */
static int
CMagicItemFactory_ProcessRunFile(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString tokenStr;
	CString logStr;
	CString errStr;
	int readResult;

	CString_DefaultConstructor(&tokenStr);

	readResult = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter);
	if (!readResult) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CString_Constructor(&logStr, "Starting runfile ");
	CString_ConcatCString(&logStr, &tokenStr);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);

	readResult = CMagicItemFactory_ReadAndExecuteFile(factory, &tokenStr);
	if (!readResult) {
		CString errStr2;
		CString_Constructor(&errStr2, "Error on runfile ");
		CString_ConcatCString(&errStr2, &tokenStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr2), *lineCount);
		CString_Destructor(&errStr2);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CString_AssignCStr(&logStr, "Completed runfile ");
	CString_ConcatCString(&logStr, &tokenStr);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);

	readResult = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter);
	if (!readResult) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		return 0;
	}

	if (CString_CompareNoCase(&tokenStr, ";") != 0) {
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CString_Destructor(&errStr);
		CString_Destructor(&logStr);
		CString_Destructor(&tokenStr);
		return 0;
	}

	CString_Destructor(&logStr);
	CString_Destructor(&tokenStr);
	return 1;
}

/*
 * 0x0043A616 - CMagicItemFactory::ProcessEffectTable
 *
 * Parses an "effecttable NAME = { entries } ;" block. Each entry is
 * weight + ("effecttable" or "effect") + name; "effect" entries
 * chain to a nested ProcessWeightedList.
 */
static int
CMagicItemFactory_ProcessEffectTable(CMagicItemFactory *factory, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString nameStr, tokenStr, debugStr;
	CString logStr;
	CString errStr;
	CSearchCtx searchCtx;
	CStringList *listPtr;
	CResListNode *nodePtr;
	CResManager *effectsRM = &factory->effectTblRM;
	int readResult;
	int weight;
	int direction;
	void *data;

	CString_DefaultConstructor(&nameStr);
	CString_DefaultConstructor(&tokenStr);
	CString_DefaultConstructor(&debugStr);

	if (!CMagicItemFactory_ReadIdentifier(factory, &nameStr, buf, pos, lineCount, delimiters, whitespace, lineDelimiter)) {
		CString_Constructor(&errStr, "Expected identifier, found ");
		CString_ConcatCString(&errStr, &nameStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	readResult = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter);
	if (readResult == 0 || CString_CompareNoCase(&tokenStr, "=") != 0) {
		CString_Constructor(&errStr, "Expecting =, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	listPtr = (CStringList *)malloc(sizeof(CStringList));
	if (listPtr != NULL)
		CStringList_Init(listPtr);

	CResManager_FindOrInsertEffects(effectsRM, &searchCtx, &nameStr);
	if (!CSearchCtx_Find(&searchCtx)) {
		if (listPtr != NULL)
			CStringList_ScalarDeleteEffects(listPtr, 1);
		CString_Constructor(&errStr, "effecttable ");
		CString_ConcatCString(&errStr, &nameStr);
		CString_AppendCStr(&errStr, " already exists");
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		CString_Destructor(&errStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	CResManager_InsertValueAtCtxEffects(effectsRM, &searchCtx, listPtr);

	CString_Constructor(&logStr, "Created effecttable ");
	CString_ConcatCString(&logStr, &nameStr);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&logStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}
	if (CString_CompareNoCase(&tokenStr, "{") != 0) {
		CString_Constructor(&errStr, "Expected {, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CString_Destructor(&errStr);
		CString_Destructor(&logStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	for (;;) {
		if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
			CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
			CString_Destructor(&logStr);
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&nameStr);
			return 0;
		}

		if (CString_CompareStr(&tokenStr, "}") != 0)
			break;

		if (!CString_IsNumeric(&tokenStr)) {
			CString_Constructor(&errStr, "Expected number, found ");
			CString_ConcatCString(&errStr, &tokenStr);
			CString_Destructor(&errStr);
			CString_Destructor(&logStr);
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&nameStr);
			return 0;
		}

		weight = CString_GetString(&tokenStr);

		if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
			CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
			CString_Destructor(&logStr);
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&nameStr);
			return 0;
		}

		if (CString_CompareStr(&tokenStr, "effecttable") != 0) {
			direction = 1;
		} else if (CString_CompareStr(&tokenStr, "effect") != 0) {
			direction = 2;
		} else {
			CString_Constructor(&errStr, "Expected effecttable or effect, found ");
			CString_ConcatCString(&errStr, &tokenStr);
			CString_Destructor(&errStr);
			CString_Destructor(&logStr);
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&nameStr);
			return 0;
		}

		if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
			CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
			CString_Destructor(&logStr);
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&nameStr);
			return 0;
		}

		CString_Assign(&debugStr, &tokenStr);

		CStringList_AllocIterNode(listPtr, &nodePtr, 0, 1);

		data = CStringList_GetNodeData(listPtr, &nodePtr);
		((CEffectTableEntry *)data)->type = direction;

		data = CStringList_GetNodeData(listPtr, &nodePtr);
		CString_Assign(&((CEffectTableEntry *)data)->name, &debugStr);

		CStringList_SetWeight(listPtr, &nodePtr, weight);

		if (direction == 2) {
			data = CStringList_GetNodeData(listPtr, &nodePtr);
			readResult =
			        CMagicItemFactory_ProcessWeightedList(factory, &((CEffectTableEntry *)data)->entries, buf, pos, lineCount, delimiters, whitespace, lineDelimiter);
			if (readResult == 0) {
				CMagicItemFactory_LogError(factory, "Could not read weighted list", *lineCount);
				CResBankMagicCtx_PostInit(&nodePtr);
				CString_Destructor(&logStr);
				CString_Destructor(&debugStr);
				CString_Destructor(&tokenStr);
				CString_Destructor(&nameStr);
				return 0;
			}
		}

		CString_AssignCStr(&logStr, "Added effecttable entry for ");
		CString_ConcatCString(&logStr, &debugStr);
		CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);

		CResBankMagicCtx_PostInit(&nodePtr);
	}

	if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
		CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
		CString_Destructor(&logStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}
	if (CString_CompareNoCase(&tokenStr, ";") != 0) {
		CString_Constructor(&errStr, "Expected ;, found ");
		CString_ConcatCString(&errStr, &tokenStr);
		CString_Destructor(&errStr);
		CString_Destructor(&logStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&nameStr);
		return 0;
	}

	CString_Destructor(&logStr);
	CString_Destructor(&debugStr);
	CString_Destructor(&tokenStr);
	CString_Destructor(&nameStr);
	return 1;
}

/*
 * 0x0043B079 - CMagicItemFactory::ProcessWeightedList
 *
 * Parses a brace-delimited list of "name weight" pairs into list
 * via CStringList_AddWeighted + CResBankMagicCtx_PostInit.
 */
static int
CMagicItemFactory_ProcessWeightedList(
        CMagicItemFactory *factory, CStringList *list, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString token;
	CString tokenStr;
	CString debugStr;
	CString errStr;
	CResListNode *localCtx;
	int result;
	int weight;

	CString_DefaultConstructor(&token);
	CString_DefaultConstructor(&tokenStr);
	CString_DefaultConstructor(&debugStr);

	CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &token, delimiters, whitespace, lineDelimiter);

	if (CString_CompareNoCase(&token, "{") != 0) {
		CString_Constructor(&errStr, "Expected {, found ");
		CString_ConcatCString(&errStr, &token);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
		result = 0;
		CString_Destructor(&errStr);
		CString_Destructor(&debugStr);
		CString_Destructor(&tokenStr);
		CString_Destructor(&token);
		return result;
	}

	while (1) {
		if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &tokenStr, delimiters, whitespace, lineDelimiter)) {
			CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
			result = 0;
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&token);
			return result;
		}

		if (CString_CompareStr(&tokenStr, "}")) {
			result = 1;
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&token);
			return result;
		}

		if (!CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &token, delimiters, whitespace, lineDelimiter)) {
			CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
			result = 0;
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&token);
			return result;
		}

		if (CString_IsNumeric(&token)) {
			weight = CString_GetString(&token);
		} else {
			CString_Constructor(&errStr, "Expected integer, found ");
			CString_ConcatCString(&errStr, &token);
			CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), *lineCount);
			result = 0;
			CString_Destructor(&errStr);
			CString_Destructor(&debugStr);
			CString_Destructor(&tokenStr);
			CString_Destructor(&token);
			return result;
		}

		CStringList_AddWeighted(list, &localCtx, &tokenStr, weight, 0);
		CResBankMagicCtx_PostInit(&localCtx);

		CString_AssignCStr(&debugStr, "Added to list: ");
		CString_ConcatCString(&debugStr, &tokenStr);
		CString_AppendCStr(&debugStr, " ");
		CString_ConcatInt(&debugStr, weight);
		CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&debugStr), *lineCount);
	}

	CString_Destructor(&debugStr);
	CString_Destructor(&tokenStr);
	CString_Destructor(&token);
	return 0;
}

/*
 * 0x0043B3DE - CMagicItemFactory::ParseSubBlock
 *
 * Reads either a single token or a "{ ... }" token list into list
 * via CResList_InsertTailStr.
 */
static int
CMagicItemFactory_ParseSubBlock(
        CMagicItemFactory *factory, CResList *list, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	CString token;
	CString logStr;
	int result;

	CString_DefaultConstructor(&token);
	CString_DefaultConstructor(&logStr);

	result = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &token, delimiters, whitespace, lineDelimiter);

	if (CString_CompareStr(&token, "{")) {
		for (;;) {
			result = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, &token, delimiters, whitespace, lineDelimiter);
			if (result == 0) {
				CMagicItemFactory_LogError(factory, "Unexpected end of file", *lineCount);
				CString_Destructor(&logStr);
				CString_Destructor(&token);
				return 0;
			}

			if (CString_CompareStr(&token, "}")) {
				CString_Destructor(&logStr);
				CString_Destructor(&token);
				return 1;
			}

			CResList_InsertTailStr(list, &token);
			CString_AssignCStr(&logStr, "Added to list: ");
			CString_ConcatCString(&logStr, &token);
			CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);
		}
	} else {
		CResList_InsertTailStr(list, &token);
		CString_AssignCStr(&logStr, "Added to list: ");
		CString_ConcatCString(&logStr, &token);
		CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), *lineCount);
		CString_Destructor(&logStr);
		CString_Destructor(&token);
		return 1;
	}
}

/*
 * 0x0043B5D6 - CMagicItemFactory::ReadIdentifier
 *
 * Reads a token and returns 0 unless it is a non-numeric identifier.
 * EOF sets outStr to "end of file".
 */
static int
CMagicItemFactory_ReadIdentifier(
        CMagicItemFactory *factory, CString *outStr, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	int result;

	result = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, outStr, delimiters, whitespace, lineDelimiter);
	if (result == 0) {
		CString_AssignCStr(outStr, "end of file");
		return 0;
	}
	if (CString_IsNumeric(outStr)) {
		if (CString_CompareNoCase(outStr, "") == 0)
			return 1;
		return 0;
	}
	return 1;
}

/*
 * 0x0043B649 - CMagicItemFactory::ReadInteger
 *
 * Reads a token and returns 0 unless it is numeric. EOF sets outStr
 * to "end of file".
 */
static int
CMagicItemFactory_ReadInteger(CMagicItemFactory *factory, CString *outStr, CString *buf, int *pos, int *lineCount, CString *delimiters, CString *whitespace, CString *lineDelimiter)
{
	int result;

	result = CMagicItemFactory_ReadToken(factory, buf, pos, lineCount, outStr, delimiters, whitespace, lineDelimiter);
	if (result == 0) {
		CString_AssignCStr(outStr, "end of file");
		return 0;
	}
	if (!CString_IsNumeric(outStr)) {
		if (CString_CompareNoCase(outStr, "") == 0)
			return 1;
		return 0;
	}
	return 1;
}

/*
 * 0x0043B6BC - CMagicItemFactory::ExecuteScript
 *
 * Tokenizes buf and dispatches itemlist / effectlist / effecttable /
 * itemeffect / globalscripts / runfile / debug / reset until EOF or
 * a handler returns failure.
 */
int
CMagicItemFactory_ExecuteScript(CMagicItemFactory *factory, CString *buf)
{
	CString tokenStr;
	CString whitespace;
	CString delimiters;
	CString lineDelimiter;
	CString errStr;
	int lineCount;
	int pos;
	int result;
	int readResult;

	lineCount = 1;
	pos = 0;

	CString_DefaultConstructor(&tokenStr);
	CString_Constructor(&delimiters, " \t\n");
	CString_Constructor(&whitespace, ";,");
	CString_Constructor(&lineDelimiter, "\n");

	result = 1;

	for (;;) {
		readResult = CMagicItemFactory_ReadToken(factory, buf, &pos, &lineCount, &tokenStr, &delimiters, &whitespace, &lineDelimiter);
		if (!readResult) {
			CString_Destructor(&lineDelimiter);
			CString_Destructor(&whitespace);
			CString_Destructor(&delimiters);
			CString_Destructor(&tokenStr);
			return 1;
		}

		if (CString_CompareStr(&tokenStr, "itemlist")) {
			result = CMagicItemFactory_ProcessItemList(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else if (CString_CompareStr(&tokenStr, "effectlist")) {
			result = CMagicItemFactory_ProcessEffectList(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else if (CString_CompareStr(&tokenStr, "effecttable")) {
			result = CMagicItemFactory_ProcessEffectTable(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else if (CString_CompareStr(&tokenStr, "itemeffect")) {
			result = CMagicItemFactory_ProcessItemEffect(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else if (CString_CompareStr(&tokenStr, "globalscripts")) {
			result = CMagicItemFactory_ProcessGlobalScripts(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else if (CString_CompareStr(&tokenStr, "runfile")) {
			result = CMagicItemFactory_ProcessRunFile(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else if (CString_CompareStr(&tokenStr, "debug")) {
			result = CMagicItemFactory_ProcessDebug(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else if (CString_CompareStr(&tokenStr, "reset")) {
			result = CMagicItemFactory_ProcessReset(factory, buf, &pos, &lineCount, &delimiters, &whitespace, &lineDelimiter);
		} else {
			CString_DefaultConstructor(&errStr);
			CString_AssignCStr(&errStr, "Unrecognized token \"");
			CString_ConcatCString(&errStr, &tokenStr);
			CString_AppendCStr(&errStr, "\"");
			CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), lineCount);
			result = 0;
			CString_Destructor(&errStr);
		}

		if (result == 0) {
			CString_DefaultConstructor(&errStr);
			CString_AssignCStr(&errStr, "Terminated parsing");
			CMagicItemFactory_LogError(factory, CString_GetBuffer(&errStr), lineCount);
			CString_Destructor(&errStr);
			CString_Destructor(&lineDelimiter);
			CString_Destructor(&whitespace);
			CString_Destructor(&delimiters);
			CString_Destructor(&tokenStr);
			return 0;
		}
	}
}

/*
 * 0x0043BAC1 - CMagicItemFactory::ReadFileContents
 *
 * Reads filename via FileManager_OpenByType(0x2F) and appends every
 * line into output. Returns 0 when the file cannot be opened.
 */
static int
CMagicItemFactory_ReadFileContents(CMagicItemFactory *factory, CString *output, CString *filename)
{
	CString logStr;
	FILE *fp;
	char buf[128];

	CString_DefaultConstructor(&logStr);

	fp = FileManager_OpenByType(0x2F, CString_GetBuffer(filename), "r");
	if (fp == NULL) {
		CString_AssignCStr(&logStr, "Could not net_openfile ");
		CString_ConcatCString(&logStr, filename);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&logStr), -1);
		CString_Destructor(&logStr);
		return 0;
	}

	CString_AssignCStr(&logStr, "Reading file ");
	CString_ConcatCString(&logStr, filename);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), -1);

	while (fgets_ServerSide(buf, 0x80, fp) != NULL) {
		CString_AppendCStr(output, buf);
	}

	fclose_ServerSide(fp);

	CString_AssignCStr(&logStr, "Finished reading file ");
	CString_ConcatCString(&logStr, filename);
	CMagicItemFactory_LogInfo(factory, CString_GetBuffer(&logStr), -1);

	CString_Destructor(&logStr);
	return 1;
}

/*
 * 0x0043BC3A - CMagicItemFactory::ReadAndExecuteFile
 *
 * Reads filename (refusing circular includes via itemList) and feeds
 * its contents to ExecuteScript.
 *
 * MODIFIED: uses a linear CResList scan instead of the binary's sorted
 * CResList search.
 */
static int
CMagicItemFactory_ReadAndExecuteFile(CMagicItemFactory *factory, CString *filename)
{
	CString fileContents;
	CString logStr;
	CResList *readFiles = &factory->itemList;
	CResListNode *node;
	int result;

	CString_DefaultConstructor(&fileContents);

	node = CResList_FindByStrSorted(readFiles, filename, NULL, 0, 1);
	if (CResList_IsValid(readFiles, node)) {
		CString_DefaultConstructor(&logStr);
		CString_AssignCStr(&logStr, "Already read file ");
		CString_ConcatCString(&logStr, filename);
		CMagicItemFactory_LogError(factory, CString_GetBuffer(&logStr), -1);
		CString_Destructor(&logStr);
		CString_Destructor(&fileContents);
		return 0;
	}

	if (!CMagicItemFactory_ReadFileContents(factory, &fileContents, filename)) {
		CString_Destructor(&fileContents);
		return 0;
	}

	CResList_InsertStrCopy(readFiles, filename, 1, 1);

	result = CMagicItemFactory_ExecuteScript(factory, &fileContents);

	CString_Destructor(&fileContents);
	return result;
}

/*
 * 0x0043BD5E - CMagicItemFactory::ResolveMainProp
 *
 * Picks a weighted-random entry from effectsRM[mainStr], following
 * type-1 include indirections until reaching a concrete effect. The
 * resolved name is written to propName and the node data returned.
 */
static intptr_t
CMagicItemFactory_ResolveMainProp(CMagicItemFactory *factory, CString *propName, CString *mainStr, CResManager *effectsRM)
{
	CString keyCopy;
	CResListNode *magicCtx = NULL;
	CSearchCtx searchCtx;
	CSearchCtx tempCtx;
	CResListNode *tempNode;
	CStringList *val;
	int *data;

	USED(factory);

	CString_CopyConstructor(&keyCopy, mainStr);
	CResBankMagicCtx_DefaultConstructor(&magicCtx);
	CSearchCtx_Constructor(&searchCtx);

retry:
	CResManager_FindByStrCtxB(effectsRM, &tempCtx, (const char *)&keyCopy, 1);
	CSearchCtx_Add(&searchCtx, &tempCtx);

	if (!CSearchCtx_Find(&searchCtx)) {
		CResBankMagicCtx_PostInit(&magicCtx);
		CString_Destructor(&keyCopy);
		return 0;
	}

	val = (CStringList *)CResManager_GetResultCtx(effectsRM, &searchCtx);
	CStringList_GetWeightedRandomEntryEffects(val, &tempNode);
	CResBankMagicCtx_CopyConstructor(&magicCtx, &tempNode);
	CResBankMagicCtx_PostInit(&tempNode);

	if (!CStringList_HasEntries((CStringList *)&magicCtx)) {
		CResBankMagicCtx_PostInit(&magicCtx);
		CString_Destructor(&keyCopy);
		return 0;
	}

	val = (CStringList *)CResManager_GetResultCtx(effectsRM, &searchCtx);
	data = CStringList_GetNodeData(val, &magicCtx);
	CString_Assign(&keyCopy, &((CEffectTableEntry *)data)->name);

	val = (CStringList *)CResManager_GetResultCtx(effectsRM, &searchCtx);
	data = CStringList_GetNodeData(val, &magicCtx);
	if (*data == 1)
		goto retry;

	CString_Assign(propName, &keyCopy);

	val = (CStringList *)CResManager_GetResultCtx(effectsRM, &searchCtx);
	data = CStringList_GetNodeData(val, &magicCtx);

	CResBankMagicCtx_PostInit(&magicCtx);
	CString_Destructor(&keyCopy);
	return (intptr_t)data;
}

/*
 * 0x0043BEFD - CMagicItemFactory::ResolveRandomValue
 *
 * Returns GetRandomRange over propsRM[propName]'s min/max, clamped
 * by randomLevel/maxDelta. Sets *doneFlag=1 when the prop is missing.
 */
static int
CMagicItemFactory_ResolveRandomValue(CMagicItemFactory *factory, CString *propName, int randomLevel, int maxDelta, int *doneFlag)
{
	CResManager *rangesRM = &factory->propsRM;
	CSearchCtx searchCtx;
	CSearchCtx tempCtx;
	int lo = 0, hi = 0;
	int tmp;
	char *result;

	CSearchCtx_Constructor(&searchCtx);

	*doneFlag = 0;

	CResManager_FindByStrCtxC(rangesRM, &tempCtx, (const char *)propName, 1);
	CSearchCtx_Add(&searchCtx, &tempCtx);

	if (!CSearchCtx_Find(&searchCtx)) {
		*doneFlag = 1;
		return 0;
	}

	result = (char *)CResManager_GetResultCtx(rangesRM, &searchCtx);
	lo = ((CItemEffectDef *)result)->minVal;

	result = (char *)CResManager_GetResultCtx(rangesRM, &searchCtx);
	hi = ((CItemEffectDef *)result)->maxVal;

	if (lo > 0 && randomLevel > lo)
		lo = randomLevel;

	if (hi > 0 && maxDelta < hi)
		hi = maxDelta;

	if (lo > hi) {
		tmp = lo;
		lo = hi;
		hi = tmp;
	}

	return GetRandomRange(lo, hi);
}

/*
 * 0x0043BFF4 - CMagicItemFactory::PruneInvalidEntries
 *
 * Walks list; for each type-1 include whose target is missing from
 * includeRM (or whose sub-list is empty) clears the node type to 0.
 * Returns list->totalWeight after pruning.
 */
static int
CMagicItemFactory_PruneInvalidEntries(CMagicItemFactory *factory, CStringList *list, CResManager *includeRM)
{
	CResListNode *magicCtx = NULL;
	CSearchCtx searchCtx;
	CSearchCtx tempCtx;
	CResListNode *tempNode;
	int nodeType;
	int *nodeData;

	CResBankMagicCtx_DefaultConstructor(&magicCtx);
	CSearchCtx_Constructor(&searchCtx);

	CStringList_BeginIter(list, &tempNode);
	CResBankMagicCtx_CopyConstructor(&magicCtx, &tempNode);
	CResBankMagicCtx_PostInit(&tempNode);

	while (CStringList_HasEntries((CStringList *)&magicCtx)) {
		nodeType = CStringList_GetTypeInclude(list, &magicCtx);
		if (nodeType != 1)
			goto advance;

		nodeData = CStringList_GetNodeData(list, &magicCtx);
		if (CString_IsNumeric((CString *)nodeData))
			goto advance;

		CResManager_FindByStrCtxA(includeRM, &tempCtx, (const char *)CStringList_GetNodeData(list, &magicCtx), 1);
		CSearchCtx_Add(&searchCtx, &tempCtx);

		if (!CSearchCtx_Find(&searchCtx)) {
			CStringList_SetTypeInclude(list, &magicCtx, 0);
		} else {
			CStringList *subList = (CStringList *)CResManager_GetResultCtx(includeRM, &searchCtx);
			int subResult = CMagicItemFactory_PruneInvalidEntries(factory, subList, includeRM);
			if (subResult <= 0)
				CStringList_SetTypeInclude(list, &magicCtx, 0);
		}

advance:
		CStringList_AdvanceIter(list, &tempNode, &magicCtx);
		CResBankMagicCtx_CopyConstructor(&magicCtx, &tempNode);
		CResBankMagicCtx_PostInit(&tempNode);
	}

	CResBankMagicCtx_PostInit(&magicCtx);
	return list->totalWeight;
}

/*
 * 0x0043C168 - CMagicItemFactory::CountRemovals
 *
 * Runs CountRemovalsInternal until it stops reporting changes and
 * returns the number of iterations.
 */
static int
CMagicItemFactory_CountRemovals(CMagicItemFactory *factory, CResManager *effectsRM)
{
	int count = 0;

	while (CMagicItemFactory_CountRemovalsInternal(factory, effectsRM))
		count++;

	return count;
}

/*
 * 0x0043C19C - CMagicItemFactory::ResolveItemId
 *
 * Prunes invalid entries from list, then picks a weighted-random
 * chain through incRM until a numeric leaf is reached, returning
 * that item ID (or 0).
 */
static uint16_t
CMagicItemFactory_ResolveItemId(CMagicItemFactory *factory, CStringList *list, CResManager *incRM)
{
	CSearchCtx ctxA;
	CSearchCtx tempCtx;
	CResListNode *magicCtx;
	CResListNode *tempMagicCtx;
	CString nameStr;
	int validCount;
	void *nodeData;
	CStringList *subList;
	uint16_t result;

	validCount = CMagicItemFactory_PruneInvalidEntries(factory, list, incRM);
	if (validCount <= 0)
		return 0;

	CSearchCtx_Constructor(&ctxA);

	CStringList_GetWeightedRandom(list, &magicCtx);

	// CStringList_HasEntries (0x0043d610): return (*(uint32_t*)this != 0)
	if (!CStringList_HasEntries((CStringList *)&magicCtx))
		return 0;

	nodeData = CStringList_GetNodeData(list, &magicCtx);
	CString_CopyConstructor(&nameStr, (CString *)nodeData);

	while (!CString_IsNumeric(&nameStr)) {
		// FindByStrCtxA (0x0043df40): binary passes CString* directly
		CResManager_FindByStrCtxA(incRM, &tempCtx, (const char *)&nameStr, 1);
		CSearchCtx_Add(&ctxA, &tempCtx);

		if (!CSearchCtx_Find(&ctxA)) {
			CString_Destructor(&nameStr);
			return 0;
		}

		subList = (CStringList *)CResManager_FindEntryA(incRM, &nameStr);
		CStringList_GetWeightedRandom(subList, &tempMagicCtx);
		CResBankMagicCtx_CopyConstructor(&magicCtx, &tempMagicCtx);
		CResBankMagicCtx_PostInit(&tempMagicCtx);

		if (!CStringList_HasEntries((CStringList *)&magicCtx)) {
			CString_Destructor(&nameStr);
			return 0;
		}

		subList = (CStringList *)CResManager_FindEntryA(incRM, &nameStr);
		nodeData = CStringList_GetNodeData(subList, &magicCtx);
		CString_Assign(&nameStr, (CString *)nodeData);
	}

	result = (uint16_t)CString_GetString(&nameStr);
	CString_Destructor(&nameStr);
	return result;
}

/*
 * 0x0043C364 - CMagicItemFactory::FindNameInKeys
 *
 * Recursively searches list for name, following includes through
 * keysRM. Returns 1 on hit.
 */
static int
CMagicItemFactory_FindNameInKeys(CMagicItemFactory *factory, CString *name, CResList *list)
{
	CSearchCtx searchCtx, tempCtx;
	CResListNode *node;
	void *data;

	CSearchCtx_Constructor(&searchCtx);

	node = CResList_Begin(list);
	while (CResList_IsValid(list, node)) {
		data = CResList_GetData(list, node);

		if (CString_EqualCString2((CString *)data, name))
			return 1;

		CResManager_FindByStrCtxD(&factory->keysRM, &tempCtx, (const char *)CResList_GetData(list, node), 1);
		CSearchCtx_Add(&searchCtx, &tempCtx);

		if (CSearchCtx_Find(&searchCtx)) {
			CStringList *subList = (CStringList *)CResManager_GetResultCtx(&factory->keysRM, &searchCtx);
			if (CMagicItemFactory_FindNameInKeys(factory, name, (CResList *)subList))
				return 1;
		}

		node = CResList_Next(list, node);
	}
	return 0;
}

/*
 * 0x0043C514 - CMagicItemFactory::FindNameInList
 *
 * Recursively searches a CStringList for name, following type-1
 * non-numeric includes through includeRM. Returns 1 on hit.
 */
static int
CMagicItemFactory_FindNameInList(CMagicItemFactory *factory, CString *name, CStringList *list)
{
	CSearchCtx searchCtx;
	CResListNode *magicCtx;
	CResListNode *tempNode;
	void *nodeData;

	CSearchCtx_Constructor(&searchCtx);

	CStringList_BeginIter(list, &tempNode);
	magicCtx = tempNode;

	while (magicCtx != NULL) {
		int type = CStringList_GetTypeInclude(list, &magicCtx);
		if (type != 1)
			goto advance;

		nodeData = CStringList_GetNodeData(list, &magicCtx);
		if (CString_EqualCString2((CString *)nodeData, name))
			return 1;

		nodeData = CStringList_GetNodeData(list, &magicCtx);
		if (CString_IsNumeric((CString *)nodeData))
			goto advance;

		nodeData = CStringList_GetNodeData(list, &magicCtx);
		CSearchCtx tempCtx;
		CResManager_FindByStrCtxA(&factory->includeRM, &tempCtx, (const char *)nodeData, 1);
		CSearchCtx_Add(&searchCtx, &tempCtx);

		if (CSearchCtx_Find(&searchCtx)) {
			CStringList *subList = (CStringList *)CResManager_GetResultCtx(&factory->includeRM, &searchCtx);
			if (CMagicItemFactory_FindNameInList(factory, name, subList))
				return 1;
		}

advance:
		CStringList_AdvanceIter(list, &tempNode, &magicCtx);
		magicCtx = tempNode;
	}
	return 0;
}

/*
 * 0x0043C6C0 - CMagicItemFactory::ExpandIncludes18
 *
 * Sets every entry type in every includeRM CStringList to flag.
 */
static void
CMagicItemFactory_ExpandIncludes18(CMagicItemFactory *factory, int flag)
{
	CResManager *includeRM = &factory->includeRM;
	CSearchCtx iter;
	CSearchCtx tempCtx;
	CStringList *val;

	CSearchCtx_Constructor(&iter);

	CResManager_BeginIterA(includeRM, &tempCtx);
	CSearchCtx_Add(&iter, &tempCtx);

	while (CSearchCtx_Find(&iter)) {
		val = (CStringList *)CResManager_GetResultCtx(includeRM, &iter);
		CStringList_SetAllTypesInclude(val, flag);

		CResManager_NextIterA(includeRM, &tempCtx, &iter);
		CSearchCtx_Add(&iter, &tempCtx);
	}
}

/*
 * 0x0043C733 - CMagicItemFactory::ExpandIncludes660
 *
 * Sets every entry type in every effectTblRM CStringList to flag.
 */
static void
CMagicItemFactory_ExpandIncludes660(CMagicItemFactory *factory, int flag)
{
	CResManager *effectsRM = &factory->effectTblRM;
	CSearchCtx iter;
	CSearchCtx tempCtx;
	CStringList *val;

	CSearchCtx_Constructor(&iter);

	CResManager_BeginIterB(effectsRM, &tempCtx);
	CSearchCtx_Add(&iter, &tempCtx);

	while (CSearchCtx_Find(&iter)) {
		val = (CStringList *)CResManager_GetResultCtx(effectsRM, &iter);
		CStringList_SetAllTypesEffects(val, flag);

		CResManager_NextIterB(effectsRM, &tempCtx, &iter);
		CSearchCtx_Add(&iter, &tempCtx);
	}
}

/*
 * 0x0043C7AF - CMagicItemFactory::ProcessPropsA
 *
 * Walks every effectsRM CStringList; for each active type-1 entry
 * of subtype 2, disables the entry if any name in keys is missing
 * from its sub-list. Returns 1 if anything was disabled.
 */
static int
CMagicItemFactory_ProcessPropsA(CMagicItemFactory *factory, CResManager *eRM, CResList *keys)
{
	int changed = 0;
	CSearchCtx iter;
	CSearchCtx tempCtx;
	CResListNode *magicCtx;
	CResListNode *tempNode;
	CStringList *val;
	CEffectTableEntry *data;
	CResListNode *innerNode;

	CSearchCtx_Constructor(&iter);

	CResManager_BeginIterB(eRM, &tempCtx);
	CSearchCtx_Add(&iter, &tempCtx);

	while (CSearchCtx_Find(&iter)) {
		val = (CStringList *)CResManager_GetResultCtx(eRM, &iter);
		if (val->totalWeight == 0)
			goto next_outer;

		CStringList_BeginIter(val, &tempNode);
		magicCtx = tempNode;

		while (magicCtx != NULL) {
			int type = CStringList_GetTypeEffects(val, &magicCtx);
			if (type != 1)
				goto advance_entry;

			data = (CEffectTableEntry *)CStringList_GetNodeData(val, &magicCtx);
			if (data->type != 2)
				goto advance_entry;

			innerNode = CResList_Begin(keys);
			while (CResList_IsValid(keys, innerNode)) {
				void *keysData = CResList_GetData(keys, innerNode);
				data = (CEffectTableEntry *)CStringList_GetNodeData(val, &magicCtx);
				if (!CMagicItemFactory_FindNameInList(factory, (CString *)keysData, &data->entries)) {
					CStringList_SetTypeEffects(val, &magicCtx, 0);
					changed = 1;
					goto advance_entry;
				}
				innerNode = CResList_Next(keys, innerNode);
			}

advance_entry:
			CStringList_AdvanceIter(val, &tempNode, &magicCtx);
			magicCtx = tempNode;
		}

next_outer:
		CResManager_NextIterB(eRM, &tempCtx, &iter);
		CSearchCtx_Add(&iter, &tempCtx);
	}

	return changed;
}

/*
 * 0x0043C9C4 - CMagicItemFactory::ProcessPropsB
 *
 * For every type-1 subtype-2 entry in eRM's CStringLists, disables
 * the entry when its name is missing from pRM, or when any name in
 * effList appears in the prop's sublist1. Returns 1 if anything
 * was disabled.
 */
static int
CMagicItemFactory_ProcessPropsB(CMagicItemFactory *factory, CResManager *eRM, CResList *effList, CResManager *pRM)
{
	int changed = 0;
	CSearchCtx outerIter;
	CSearchCtx innerCtx;
	CSearchCtx tempCtx;
	CResListNode *magicCtx;
	CResListNode *tempNode;
	CStringList *val;
	CEffectTableEntry *data;
	CResListNode *node;

	CSearchCtx_Constructor(&outerIter);
	CSearchCtx_Constructor(&innerCtx);

	CResManager_BeginIterB(eRM, &tempCtx);
	CSearchCtx_Add(&outerIter, &tempCtx);

	while (CSearchCtx_Find(&outerIter)) {
		val = (CStringList *)CResManager_GetResultCtx(eRM, &outerIter);
		if (val->totalWeight == 0)
			goto next_outer;

		CStringList_BeginIter(val, &tempNode);
		magicCtx = tempNode;

		while (magicCtx != NULL) {
			int type = CStringList_GetTypeEffects(val, &magicCtx);
			if (type != 1)
				goto advance_entry;

			data = (CEffectTableEntry *)CStringList_GetNodeData(val, &magicCtx);
			if (data->type != 2)
				goto advance_entry;

			data = (CEffectTableEntry *)CStringList_GetNodeData(val, &magicCtx);
			CResManager_FindByStrCtxC(pRM, &tempCtx, (const char *)&data->name, 1);
			CSearchCtx_Add(&innerCtx, &tempCtx);

			if (!CSearchCtx_Find(&innerCtx)) {
				CStringList_SetTypeEffects(val, &magicCtx, 0);
				changed = 1;
				goto advance_entry;
			}

			{
				CItemEffectDef *propsResult = (CItemEffectDef *)CResManager_GetResultCtx(pRM, &innerCtx);
				CResList *propsStr = &propsResult->sublist1;

				node = CResList_Begin(effList);
				while (CResList_IsValid(effList, node)) {
					if (CMagicItemFactory_FindNameInKeys(factory, (CString *)CResList_GetData(effList, node), propsStr)) {
						CStringList_SetTypeEffects(val, &magicCtx, 0);
						changed = 1;
						goto advance_entry;
					}
					node = CResList_Next(effList, node);
				}
			}

advance_entry:
			CStringList_AdvanceIter(val, &tempNode, &magicCtx);
			magicCtx = tempNode;
		}

next_outer:
		CResManager_NextIterB(eRM, &tempCtx, &outerIter);
		CSearchCtx_Add(&outerIter, &tempCtx);
	}

	return changed;
}

/*
 * 0x0043CC46 - CMagicItemFactory::DeepResolve
 *
 * For every type-1 subtype-2 entry in eRM, disables it when the
 * prop is missing from pRM, or when its min/max range lies entirely
 * outside [randomLevel, maxDelta] (or is fully negative with flag=1).
 * Returns 1 if anything was disabled.
 */
static int
CMagicItemFactory_DeepResolve(CMagicItemFactory *factory, CResManager *eRM, int randomLevel, int maxDelta, CResManager *pRM, int flag)
{
	int changed = 0;
	USED(factory);
	CSearchCtx outerIter;
	CSearchCtx innerCtx;
	CSearchCtx tempCtx;
	CResListNode *magicCtx;
	CResListNode *tempNode;
	CStringList *val;
	CEffectTableEntry *data;
	int lo, hi;

	CSearchCtx_Constructor(&outerIter);
	CSearchCtx_Constructor(&innerCtx);

	CResManager_BeginIterB(eRM, &tempCtx);
	CSearchCtx_Add(&outerIter, &tempCtx);

	while (CSearchCtx_Find(&outerIter)) {
		val = (CStringList *)CResManager_GetResultCtx(eRM, &outerIter);
		if (val->totalWeight == 0)
			goto next_outer;

		CStringList_BeginIter(val, &tempNode);
		magicCtx = tempNode;

		while (magicCtx != NULL) {
			int type = CStringList_GetTypeEffects(val, &magicCtx);
			if (type != 1)
				goto advance_entry;

			data = (CEffectTableEntry *)CStringList_GetNodeData(val, &magicCtx);
			if (data->type != 2)
				goto advance_entry;

			data = (CEffectTableEntry *)CStringList_GetNodeData(val, &magicCtx);
			CResManager_FindByStrCtxC(pRM, &tempCtx, (const char *)&data->name, 1);
			CSearchCtx_Add(&innerCtx, &tempCtx);

			if (!CSearchCtx_Find(&innerCtx)) {
				CStringList_SetTypeEffects(val, &magicCtx, 0);
				changed = 1;
				goto advance_entry;
			}

			{
				CItemEffectDef *propsResult = (CItemEffectDef *)CResManager_GetResultCtx(pRM, &innerCtx);
				lo = propsResult->minVal;
				hi = propsResult->maxVal;
			}

			if (flag == 1 && lo < 0 && hi < 0) {
				CStringList_SetTypeEffects(val, &magicCtx, 0);
				changed = 1;
				goto advance_entry;
			}

			if ((lo < randomLevel && hi < randomLevel) || (lo > maxDelta && hi > maxDelta)) {
				CStringList_SetTypeEffects(val, &magicCtx, 0);
				changed = 1;
			}

advance_entry:
			CStringList_AdvanceIter(val, &tempNode, &magicCtx);
			magicCtx = tempNode;
		}

next_outer:
		CResManager_NextIterB(eRM, &tempCtx, &outerIter);
		CSearchCtx_Add(&outerIter, &tempCtx);
	}

	return changed;
}

/*
 * 0x0043CEF9 - CMagicItemFactory::CountRemovalsInternal
 *
 * Disables any type-1 entry in effectsMap whose referenced sibling
 * list is missing or empty. Returns 1 if anything was disabled.
 */
static int
CMagicItemFactory_CountRemovalsInternal(CMagicItemFactory *factory, CResManager *effectsMap)
{
	int result = 0;
	CSearchCtx outerCtx;
	CSearchCtx innerCtx;
	CResListNode *magicCtx = NULL;
	CSearchCtx tempCtx;
	CStringList *val;
	CResListNode *tempNode;

	USED(factory);

	CSearchCtx_Constructor(&outerCtx);
	CSearchCtx_Constructor(&innerCtx);
	CResBankMagicCtx_DefaultConstructor(&magicCtx);

	CResManager_BeginIterB(effectsMap, &tempCtx);
	CSearchCtx_Add(&outerCtx, &tempCtx);

	while (CSearchCtx_Find(&outerCtx)) {
		val = (CStringList *)CResManager_GetResultCtx(effectsMap, &outerCtx);
		if (val->totalWeight == 0)
			goto next_outer;

		CStringList_BeginIter(val, &tempNode);
		CResBankMagicCtx_CopyConstructor(&magicCtx, &tempNode);
		CResBankMagicCtx_PostInit(&tempNode);

		while (CStringList_HasEntries((CStringList *)&magicCtx)) {
			int type;
			int *data;

			val = (CStringList *)CResManager_GetResultCtx(effectsMap, &outerCtx);
			type = CStringList_GetTypeEffects(val, &magicCtx);
			if (type != 1)
				goto advance;

			val = (CStringList *)CResManager_GetResultCtx(effectsMap, &outerCtx);
			data = CStringList_GetNodeData(val, &magicCtx);
			if (*data != 1)
				goto advance;

			val = (CStringList *)CResManager_GetResultCtx(effectsMap, &outerCtx);
			data = CStringList_GetNodeData(val, &magicCtx);
			CResManager_FindByStrCtxB(effectsMap, &tempCtx, (const char *)&((CEffectTableEntry *)data)->name, 1);
			CSearchCtx_Add(&innerCtx, &tempCtx);

			if (CSearchCtx_Find(&innerCtx)) {
				CStringList *innerVal = (CStringList *)CResManager_GetResultCtx(effectsMap, &innerCtx);
				if (innerVal->totalWeight != 0)
					goto advance;
			}

			val = (CStringList *)CResManager_GetResultCtx(effectsMap, &outerCtx);
			CStringList_SetTypeEffects(val, &magicCtx, 0);
			result = 1;

advance:
			val = (CStringList *)CResManager_GetResultCtx(effectsMap, &outerCtx);
			CStringList_AdvanceIter(val, &tempNode, &magicCtx);
			CResBankMagicCtx_CopyConstructor(&magicCtx, &tempNode);
			CResBankMagicCtx_PostInit(&tempNode);
		}

next_outer:
		CResManager_NextIterB(effectsMap, &tempCtx, &outerCtx);
		CSearchCtx_Add(&outerCtx, &tempCtx);
	}

	CResBankMagicCtx_PostInit(&magicCtx);
	return result;
}

/*
 * 0x0043D10D - CMagicItemFactory::Create
 *
 * Populates magicList with a random magic item: resolves an item ID
 * from searchCtx, then iteratively picks effects via the factory's
 * props/keys/effects tables until the level window [minLevel,maxLevel]
 * fills or maxEffects is reached. Returns 0 when no item resolves.
 */
int
CMagicItemFactory_Create(CMagicItemList *magicList, int minLevel, int maxLevel, void *searchCtx, int forceThreshold, int maxEffects)
{
	CMagicItemFactory *factory = &g_MagicItemFactory;
	CString itemIdStr;
	CResList localKeysA;
	CResList localKeysB;
	CString propName;
	CMagicItemListNode effectName;
	int done;
	uint16_t itemId;
	int randomLevel;
	int maxDelta;
	int levelDelta;
	intptr_t resolvedValue;

	CString_DefaultConstructor(&itemIdStr);
	memset(&localKeysA, 0, sizeof(CResList));
	memset(&localKeysB, 0, sizeof(CResList));
	CString_DefaultConstructor(&propName);
	CString_DefaultConstructor(&effectName.str);
	done = 0;

	itemId = CMagicItemFactory_ResolveItemId(factory, searchCtx, (void *)&factory->includeRM);

	if ((itemId & 0xFFFF) == 0) {
		CString_Destructor(&effectName.str);
		CString_Destructor(&propName);
		CResList_DestructorA(&localKeysB);
		CResList_DestructorA(&localKeysA);
		CString_Destructor(&itemIdStr);
		return 0;
	}

	CString_SetFromInt(&itemIdStr, (int)(itemId & 0xFFFF));
	magicList->itemId = (uint16_t)CString_GetString(&itemIdStr);

	CResList_InsertTailStr(&localKeysA, &itemIdStr);

	for (;;) {
		CMagicItemFactory_ExpandIncludes18(factory, 1);
		CMagicItemFactory_ExpandIncludes660(factory, 1);

		CMagicItemFactory_ProcessPropsA(factory, (void *)&factory->effectTblRM, &localKeysA);
		CMagicItemFactory_ProcessPropsB(factory, (void *)&factory->effectTblRM, &localKeysB, (void *)&factory->propsRM);

		levelDelta = minLevel - (int32_t)magicList->mlCount;
		if (levelDelta < 1)
			levelDelta = 1;
		randomLevel = GetRandomRange(1, levelDelta);
		maxDelta = maxLevel - (int32_t)magicList->mlCount;

		if ((int32_t)magicList->result >= forceThreshold) {
			CMagicItemFactory_DeepResolve(factory, (void *)&factory->effectTblRM, randomLevel, maxDelta, (void *)&factory->propsRM, 0);
		} else {
			CMagicItemFactory_DeepResolve(factory, (void *)&factory->effectTblRM, randomLevel, maxDelta, (void *)&factory->propsRM, 1);
		}

		CMagicItemFactory_CountRemovals(factory, (void *)&factory->effectTblRM);

		{
			CString mainStr;
			CString_Constructor(&mainStr, "main");
			resolvedValue = CMagicItemFactory_ResolveMainProp(factory, &propName, &mainStr, (void *)&factory->effectTblRM);
			CString_Destructor(&mainStr);
		}

		if (resolvedValue == 0)
			break;

		CString_Assign(&effectName.str, &propName);

		resolvedValue = CMagicItemFactory_ResolveRandomValue(factory, &propName, randomLevel, maxDelta, &done);

		if (done == 1) {
			done = 0;
			continue;
		}

		effectName.value = (int)resolvedValue;

		magicList->mlCount += resolvedValue;

		if (resolvedValue < 1)
			magicList->result += 1;

		CResList_InsertTailDataB(&magicList->list, &effectName);

		CResList_AddResultEntry(&localKeysB, &propName, 1);

		{
			int effectCount, lvl, r;

			effectCount = magicList->list.count;
			if (effectCount >= maxEffects)
				break;

			lvl = (int32_t)magicList->mlCount;
			if (minLevel > lvl || lvl > maxLevel) {
				lvl = (int32_t)magicList->mlCount;
				if (lvl <= maxLevel)
					continue;
				break;
			}

			r = GetRandomRange(1, maxEffects);
			effectCount = magicList->list.count;
			if (r > effectCount)
				continue;
			break;
		}
	}

	CString_Destructor(&effectName.str);
	CString_Destructor(&propName);
	CResList_DestructorA(&localKeysB);
	CResList_DestructorA(&localKeysA);
	CString_Destructor(&itemIdStr);
	return 1;
}

/*
 * 0x0043D4CE - CMagicItemFactory::AttachGlobalScripts
 *
 * Attaches every script in factory->globalScripts to entity via
 * Entity_AttachScript. Returns 0 if any script failed to attach.
 */
int
CMagicItemFactory_AttachGlobalScripts(CMagicItemFactory *factory, CItem *entity)
{
	CResList *globalScripts = &factory->globalScripts;
	CResListNode *iter;
	int result = 1;
	const char *err;

	iter = CResList_Begin(globalScripts);
	while (CResList_IsValid(globalScripts, iter)) {
		err = Entity_AttachScript(entity, CString_GetBuffer((CString *)CResList_GetData(globalScripts, iter)), 1);
		if (err != NULL)
			result = 0;
		iter = CResList_Next(globalScripts, iter);
	}
	return result;
}

/*
 * 0x0043D550 - CItemEffectDef::~CItemEffectDef (scalar deleting destructor)
 *
 * Runs CItemEffectDef_Destructor and frees the def when flags & 1.
 */
CItemEffectDef *
CItemEffectDef_ScalarDelete(CItemEffectDef *def, int flags)
{
	CItemEffectDef_Destructor(def);
	if (flags & 1)
		free(def);
	return NULL;
}

/*
 * 0x0043E810 - CItemEffectDef::SetName
 *
 * Assigns name into this->name.
 */
static void
CItemEffectDef_SetName(CItemEffectDef *def, CString *name)
{
	CString_Assign(&def->name, name);
}

/*
 * 0x0043E830 - CItemEffectDef::SetCategory
 *
 * Assigns name into this->category.
 */
static void
CItemEffectDef_SetCategory(CItemEffectDef *def, CString *name)
{
	CString_Assign(&def->category, name);
}

/*
 * 0x0043E850 - CItemEffectDef::SetSubcat
 *
 * Assigns name into this->subcat.
 */
static void
CItemEffectDef_SetSubcat(CItemEffectDef *def, CString *name)
{
	CString_Assign(&def->subcat, name);
}

/*
 * 0x0043E870 - CItemEffectDef::GetMinVal
 *
 * Returns this->minVal.
 */
static int __attribute__((unused))
CItemEffectDef_GetMinVal(CItemEffectDef *def)
{
	return def->minVal;
}

/*
 * 0x0043E8B0 - CItemEffectDef::GetMaxVal
 *
 * Returns this->maxVal.
 */
static int __attribute__((unused))
CItemEffectDef_GetMaxVal(CItemEffectDef *def)
{
	return def->maxVal;
}

/*
 * 0x0043E9B0 - CItemEffectDef::SetMinVal
 *
 * Sets this->minVal.
 */
static void
CItemEffectDef_SetMinVal(CItemEffectDef *def, int minVal)
{
	def->minVal = minVal;
}

/*
 * 0x0043E9D0 - CItemEffectDef::SetMaxVal
 *
 * Sets this->maxVal.
 */
static void
CItemEffectDef_SetMaxVal(CItemEffectDef *def, int maxVal)
{
	def->maxVal = maxVal;
}

/*
 * 0x0043E9F0 - CItemEffectDef::GetList1
 *
 * Returns &this->sublist1.
 */
static CResList *
CItemEffectDef_GetList1(CItemEffectDef *def)
{
	return &def->sublist1;
}

/*
 * 0x0043EA10 - CItemEffectDef::GetList2
 *
 * Returns &this->sublist2.
 */
static CResList *
CItemEffectDef_GetList2(CItemEffectDef *def)
{
	return &def->sublist2;
}

/*
 * 0x0043F360 - CEffectTableEntry::CEffectTableEntry (weight arg)
 *
 * Constructs via CEffectTableEntry_Constructor, then sets direction=1 and
 * weight=weight.
 */
CEffectTableEntry *
CEffectTableEntry_ConstructorWeight(CEffectTableEntry *this, int weight)
{
	CEffectTableEntry_Constructor(this);
	this->direction = 1;
	this->weight = weight;
	return this;
}

/*
 * 0x00441C20 - CEffectTableEntry::ScalarDelete
 *
 * Runs CEffectTableEntry_DestructorB and frees the entry when flags & 1.
 */
CEffectTableEntry *
CEffectTableEntry_ScalarDeleteE(CEffectTableEntry *this, int flags)
{
	CEffectTableEntry_DestructorB(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x00441C50 - CEffectTableEntry::~CEffectTableEntry (wrapper B)
 *
 * Thin wrapper around CEffectTableEntry_Destructor.
 */
static CEffectTableEntry *
CEffectTableEntry_DestructorB(CEffectTableEntry *this)
{
	CEffectTableEntry_Destructor(this);
	return this;
}

/*
 * 0x00441C70 - CEffectTableEntry::~CEffectTableEntry
 *
 * Destroys the CStringList entries and the CString name.
 */
static void
CEffectTableEntry_Destructor(CEffectTableEntry *this)
{
	CStringList_Destroy(&this->entries);
	CString_Destructor(&this->name);
}

/*
 * 0x00466040 - CItemEffectDef::CItemEffectDef
 *
 * Default-initializes the three CStrings and two CResLists, and
 * sets minVal/maxVal to -1.
 */
static CItemEffectDef *
CItemEffectDef_Constructor(CItemEffectDef *def)
{
	CString_DefaultConstructor(&def->name);
	CString_DefaultConstructor(&def->category);
	CString_DefaultConstructor(&def->subcat);
	CResListNode_Constructor_bin((CResListNode *)&def->sublist1);
	CResListNode_Constructor_bin((CResListNode *)&def->sublist2);
	def->minVal = -1;
	def->maxVal = -1;
	return def;
}

/*
 * 0x00466199 - CItemEffectDef::~CItemEffectDef
 *
 * Tears down the two sublists and three CStrings in reverse
 * construction order.
 */
static CItemEffectDef *
CItemEffectDef_Destructor(CItemEffectDef *def)
{
	CResList_DestructorA(&def->sublist2);
	CResList_DestructorA(&def->sublist1);
	CString_Destructor(&def->subcat);
	CString_Destructor(&def->category);
	CString_Destructor(&def->name);
	return def;
}

/*
 * 0x00466EA0 - CString::ScalarDelete (magicfactory vtable)
 *
 * Runs CString_DestructorWrap and frees the string when flags & 1.
 */
void *
CString_ScalarDelete_MF(CString *this, int flags)
{
	CString_DestructorWrap(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x00466FA0 - CSmartPtr<CString>::ScalarDelete (magicfactory vtable)
 *
 * Runs SmartPtr_Destructor_CString and frees the smart pointer when
 * flags & 1.
 */
__attribute__((unused)) void *
SmartPtr_CString_ScalarDelete(CSmartPtr *this, int flags)
{
	SmartPtr_Destructor_CString(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * 0x00467020 - CItemEffectDef::GetCategoryStr
 *
 * Returns &this->category.
 */
static __attribute__((unused)) CString *
CItemEffectDef_GetCategoryStr(CItemEffectDef *def)
{
	return &def->category;
}

/*
 * 0x00467040 - CItemEffectDef::GetSubcatStr
 *
 * Returns &this->subcat.
 */
static __attribute__((unused)) CString *
CItemEffectDef_GetSubcatStr(CItemEffectDef *def)
{
	return &def->subcat;
}
