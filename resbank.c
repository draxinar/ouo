/*
 * CTemplateManager - NPC template catalogue backing the spawn system.
 *
 * Parses templatestable.dat into a lookup table of templates used by
 * egg.c to spawn NPCs, resolves per-template overrides, and exposes
 * the helpers that mix in stat, skill, and equipment defaults.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "container.h"
#include "defcon.h"
#include "egg.h"
#include "feature.h"
#include "filemanager.h"
#include "io.h"
#include "load.h"
#include "magicfactory.h"
#include "magiclist.h"
#include "main.h"
#include "multi.h"
#include "player.h"
#include "random.h"
#include "region.h"
#include "shopkeeper.h"
#include "template.h"
#include "time.h"
#include "utils.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

__extension__ typedef struct CResBankDistrib CResBankDistrib;

static CResBankRegion *CResBankDistrib_FindRegion(CResBankDistrib *this, int x, int y); // 0x004331D0
static int CResBankDistrib_MatchPrefix(CResBankDistrib *this, char **pos, const char *prefix); // 0x0043325F
static void CResBankDistrib_SkipWhitespace(CResBankDistrib *this, char **pos); // 0x004332DA
static int CResBankDistrib_ParseWord(CResBankDistrib *this, char **pos, char *outBuf, int maxLen); // 0x0043331D
static int CResBankDistrib_ParseInt(CResBankDistrib *this, char **pos, int *outVal); // 0x004333B0
static int CResBankDistrib_ParseChar(CResBankDistrib *this, char **pos, char *outChar, const char *charSet); // 0x0043345B
static void CResBankDistrib_ClampToRange(CResBankDistrib *this, int *value, int idx); // 0x004334BA
static void CResBankDistrib_ClampNonNeg(CResBankDistrib *this, int *value, int idx); // 0x0043350D
static void CResBankDistrib_ResourceNodeOp(CResBankDistrib *this, void *node); // 0x00433538
static void CResBankDistrib_ProcessEntity(CResBankDistrib *this, CItem *entity); // 0x00433545
static void CResBankDistrib_LoadBankDefs(CResBankDistrib *this); // 0x0043361F
static void CResBankManager_LoadFallback(void); // 0x004AD5BA
static char *GetTemplateName(int templateId); // 0x004AD1DE
static int CheckTemplateOverLimit(int templateId); // 0x004AD1D4
static int IsSpecialTemplateId(int templateId); // 0x004AD1B0
static void StaticInit_CResBankManager(void); // 0x004A83EF
static void CResBankManager_WriteInt32(CResBankManager *this, char **destPtr, int32_t *sizePtr, int32_t value); // 0x004ADDDB
static void CResBankManager_ReadInt32(CResBankManager *this, char **destPtr, int32_t *sizePtr, int32_t value); // 0x004ADE16
static void CResBankManager_WriteByte(CResBankManager *this, char **destPtr, int32_t *sizePtr, uint8_t value); // 0x004ADE51
static int ValidateResId(int resId, const char *sourceFile, int lineNumber); // 0x004AEE0F
static void CResBankRegion_Destructor(CResBankRegion *region); // 0x004AEDFC
static void CResBankManager_EnsureCurrent(void); // 0x004AE9E1
static void CResBankManager_SplitOverlappingRegions(CResBankRegion *newRegion); // 0x004AE55A
static CResBankRegion *CResBankManager_CreateRegion(const char *name, int32_t x1, int32_t y1, int32_t x2, int32_t y2); // 0x004AE48B
static int GetElapsedMilliseconds(const int32_t *start, const int32_t *end); // 0x004AE112
static void AccumulateEggCounts(CItem *entity, int *maxCounts, int *curCounts); // 0x004B122C
static void ProcessStaticTiles(void); // 0x004B0C31
static void AddResourceNodesByCategory(CItem *eggEntity, int categoryIdx, int count); // 0x004B0AA5
static int CResBankRegion_GetExactPosition(CResBankRegion *region, NPCTemplate *tmpl); // 0x004B03EF
static NPCTemplate *GetSpawnLocation(ResSpawnEntry *entry, int conditional); // 0x004B0384
static int CResBankRegion_SpawnForTemplate(CResBankRegion *region, int templateFilter); // 0x004B021D
static CItem *CResBankRegion_SpawnInSubRegion(CResBankRegion *region, ResSpawnEntry *entry, int noWander); // 0x004AFC4B
static int CountMobilesInBox(uint16_t templateId, int x1, int y1, int z1, int x2, int y2, int z2); // 0x004AFBC8
static CItem *CResBankRegion_SpawnAtPointInBox(CResBankRegion *region, uint16_t templateId, SubRegion *sub); // 0x004AFA80
static uint32_t CResBankRegion_SpawnAtPoint(CResBankRegion *region, uint16_t templateId, uint16_t x, uint16_t y, int16_t z, int noWander); // 0x004AF97C
static int CanSpawnHere(CLocation *loc); // 0x004AF8E3
static int CResBankRegion_CanSpawnTemplate(CResBankRegion *region, uint16_t templateId); // 0x004AF57A
static int ClassifyTemplateItems(int templateId); // 0x004AF4C4
static char *CResBankRegion_GetName(CResBankRegion *region); // 0x004AF4B1
static void CClassification_InsertEntry(CResBankRegion *region, int entryIdx, uint16_t templateId, int frequency); // 0x004B1BB0
static void ProcessBlockItemsForSpawn(CItem *head); // 0x004B955A
static void ProcessDynamicItemForSpawn(CItem *item); // 0x004B91B2
static void *CResBankRegion_ScalarDelete(CResBankRegion *region, int flags); // 0x004B3150
static CResListNode **CStringList_FindByName(CResList *list, CResListNode **outIter, CString *name, CResListNode *inIterNode, int direction); // 0x004B31B0
static void ClassifyTemplateBody(int templateId); // 0x004B2873
static void RegisterTemplateInRegion(uint16_t templateId, int regionIndex); // 0x004B277D
static int ResolveRegionName(const char *name); // 0x004B26C9
static void FinalizeClassification(void); // 0x004B241C
static int CResBankRegion_AddTemplateDbEntry(CResBankRegion *region, int x1, int y1, int x2, int y2, int z1, int z2, int ni, const char *name); // 0x004B225A
static void CResBankRegion_AddSpawnEntry(CResBankRegion *region, uint16_t templateId, uint16_t subRegionIdx, uint16_t regionNameIdx); // 0x004B2013
static void CResBankRegion_AddSubRegionToEntry(CResBankRegion *region, ResSpawnEntry *entry, uint16_t subRegionIdx, uint16_t regionNameIdx); // 0x004B1DC0

#define CRandom() rand()

#define MAX_RESOURCE_TYPES 102

// ValidateTemplateId macro matching binary calling convention.
#define ValidateTemplateId(idx) ValidateResId((idx), "D:\\TornadoAlley\\Projects\\UltimaOnline\\area\\resbank.cxx", __LINE__)

/*
 * Stack-local accumulator (0x398 bytes) built by LoadAll_ResBankDistrib
 * from bankdefs.txt and then applied to CResBankRegion.maxSpawns.
 */
__extension__ typedef struct CResBankDistrib CResBankDistrib;
// clang-format off
struct CResBankDistrib {
	uint8_t positiveFlags[RESBANK_MAX_TEMPLATES]; // +0x00
	uint16_t _pad66;                              // +0x66
	int32_t maxVals[RESBANK_MAX_TEMPLATES];       // +0x68
	int32_t minVals[RESBANK_MAX_TEMPLATES];       // +0x200
};
// clang-format on

/*
 * 0x00697A50 - g_MagicItemFactory: singleton CMagicItemFactory object
 * (0x884 bytes). Populated by the startup script executor.
 */
CMagicItemFactory g_MagicItemFactory;

/*
 * CResManager instances matching binary's CTemplateManager at 0x006933F8.
 * Binary offsets: templates at +0x000, names at +0x218, defines at +0x430.
 * Types and functions are in searchCtx.h / searchCtx.c.
 */
CResManager g_TemplatesRM;  // +0x000: integer-keyed (templateId -> NPCTemplate*)
CResManager g_NameTableRM;  // +0x218: integer-keyed (entryId -> CNameEntry*)
CResManager g_DefinesRM;    // +0x430: string-keyed (name -> DefineEntry*)

// 0x006E7674 - spawn counter for templateId==0xC
int g_SpawnType12Count;
// 0x006E7678 - total spawn counter
int g_SpawnTotalCount;

// 0x006EFECC - Vendor (shopkeeper) linked list head.
// Vendors are linked via prevNPC (0x478) as next and npcSfx (0x47C) area as prev.
CNPC *g_VendorListHead;

/*
 * CNameEntry - binary: 0x24 bytes (constructor 0x004C06B0).
 * Three plain CList objects (0x0C each): [0]=male, [1]=female, [2]=other.
 * Each CList node holds a heap-allocated CString as data.
 */
// Append a new node with given data to a CResList (binary: CList::AddTail).
void
CResList_AddTail(CResList *list, void *data)
{
	CResListNode *node = (CResListNode *)malloc(sizeof(CResListNode));
	node->next = NULL;
	node->prev = NULL;
	node->data = data;
	if (list->tail == NULL) {
		list->head = node;
		list->tail = node;
	} else {
		node->prev = list->tail;
		list->tail->next = node;
		list->tail = node;
	}
	list->count++;
}

// Binary's case table at 0x004BE254 (48 bytes).
// Maps (fieldType - 1) to case index (0-30), where 0x1e = default.
const uint8_t g_FieldCaseTable[48] = {
	0x00,
	0x1e,
	0x01,
	0x02,
	0x03,
	0x04,
	0x05,
	0x06, // types 1-8
	0x07,
	0x08,
	0x09,
	0x0a,
	0x0b,
	0x1e,
	0x0c,
	0x0d, // types 9-16
	0x0e,
	0x0f,
	0x10,
	0x10,
	0x1e,
	0x11,
	0x11,
	0x11, // types 17-24
	0x11,
	0x11,
	0x12,
	0x13,
	0x14,
	0x15,
	0x1e,
	0x1e, // types 25-32
	0x1e,
	0x16,
	0x17,
	0x1e,
	0x1e,
	0x18,
	0x1e,
	0x1e, // types 33-40
	0x1e,
	0x19,
	0x1a,
	0x1e,
	0x1b,
	0x1e,
	0x1c,
	0x1d, // types 41-48
};

/*
 * Spawn execution - create NPCs from templates
 */

/*
 * Template parser. g_TemplateManager is at 0x006933F8.
 */

// 0x0068B3A8 - per-template entity chain heads.
CItem *g_TemplateChain[TEMPLATE_CHAIN_SIZE];
// 0x0068F3A8 - per-template entity counts.
int g_TemplateChainCount[TEMPLATE_CHAIN_SIZE];
// 0x00693A40 - per-template name pointers (indexed by templateId).
char *g_TemplateNames[TEMPLATE_CHAIN_SIZE];

/*
 * 0x004331D0 - CResBankDistrib::FindRegion
 *
 * Returns the CResBankRegion containing point (x, y), or NULL.
 */
static CResBankRegion *
CResBankDistrib_FindRegion(CResBankDistrib *this, int x, int y)
{
	int i;

	USED(this);

	if (x < 0 && y < 0)
		return NULL;

	for (i = 0; i < g_ResBankManager.regionCount; i++) {
		CResBankRegion *region = g_ResBankManager.hashTable[i];
		if (x >= region->x1 && y >= region->y1 && x <= region->x2 && y <= region->y2)
			return region;
	}

	return NULL;
}

/*
 * 0x0043325F - CResBankDistrib::MatchPrefix
 *
 * Advances *pos to the next occurrence of prefix and past it. Returns
 * 1 on match, 0 at end-of-string.
 */
static int
CResBankDistrib_MatchPrefix(CResBankDistrib *this, char **pos, const char *prefix)
{
	int prefixLen;

	USED(this);

	prefixLen = strlen(prefix);

	while (**pos != '\0') {
		if (**pos != prefix[0]) {
			(*pos)++;
			continue;
		}
		if (strncmp(*pos, prefix, prefixLen) == 0) {
			*pos += prefixLen;
			return 1;
		}
		(*pos)++;
	}

	return 0;
}

/*
 * 0x004332DA - CResBankDistrib::SkipWhitespace
 *
 * Advances *pos past space, newline, and carriage return (not tab).
 */
static void
CResBankDistrib_SkipWhitespace(CResBankDistrib *this, char **pos)
{
	USED(this);

	while (**pos == ' ' || **pos == '\n' || **pos == '\r')
		(*pos)++;
}

/*
 * 0x0043331D - CResBankDistrib::ParseWord
 *
 * Reads a whitespace-delimited word from *pos into outBuf. Returns 1
 * if any characters were copied.
 */
static int
CResBankDistrib_ParseWord(CResBankDistrib *this, char **pos, char *outBuf, int maxLen)
{
	int found;

	CResBankDistrib_SkipWhitespace(this, pos);

	found = 0;
	while (found < maxLen) {
		if (**pos == '\0' || **pos == '\n' || **pos == '\r' || **pos == ' ')
			break;
		*outBuf = **pos;
		outBuf++;
		(*pos)++;
		found = 1;
	}
	*outBuf = '\0';

	return found;
}

/*
 * 0x004333B0 - CResBankDistrib::ParseInt
 *
 * Parses a decimal integer from *pos into *outVal. Returns 1 if any
 * digits were parsed.
 */
static int
CResBankDistrib_ParseInt(CResBankDistrib *this, char **pos, int *outVal)
{
	int found;

	CResBankDistrib_SkipWhitespace(this, pos);

	*outVal = 0;
	found = 0;
	while (**pos != '\0' && **pos != '\n' && **pos != '\r') {
		if (**pos < '0' || **pos > '9')
			break;
		*outVal = *outVal * 10 + (**pos - '0');
		(*pos)++;
		found = 1;
	}

	return found;
}

/*
 * 0x0043345B - CResBankDistrib::ParseChar
 *
 * Reads one character from *pos and returns 1 if it is in charSet.
 */
static int
CResBankDistrib_ParseChar(CResBankDistrib *this, char **pos, char *outChar, const char *charSet)
{
	int len;

	CResBankDistrib_SkipWhitespace(this, pos);

	*outChar = **pos;
	(*pos)++;

	len = strlen(charSet);
	if (memchr(charSet, *outChar, len) == NULL)
		return 0;

	return 1;
}

/*
 * 0x004334BA - CResBankDistrib::ClampToRange
 *
 * Clamps *value to [minVals[idx], maxVals[idx]].
 */
static void
CResBankDistrib_ClampToRange(CResBankDistrib *this, int *value, int idx)
{
	if (*value > this->maxVals[idx])
		*value = this->maxVals[idx];
	if (*value < this->minVals[idx])
		*value = this->minVals[idx];
}

/*
 * 0x0043350D - CResBankDistrib::ClampNonNeg
 *
 * If positiveFlags[idx] is set and *value < 0, clamps *value to 0.
 */
static void
CResBankDistrib_ClampNonNeg(CResBankDistrib *this, int *value, int idx)
{
	if (this->positiveFlags[idx] != 0 && *value < 0)
		*value = 0;
}

/*
 * 0x00433538 - CResBankDistrib::ResourceNodeOp
 *
 * No-op.
 */
static void
CResBankDistrib_ResourceNodeOp(CResBankDistrib *this, void *node)
{
	USED(this);
	USED(node);
}

/*
 * 0x00433545 - CResBankDistrib::ProcessEntity
 *
 * Walks entity's resource node list, calls ResourceNodeOp on type==3
 * nodes with nonzero amount. Recurses into container contents and
 * mobile equipment slots.
 */
static void
CResBankDistrib_ProcessEntity(CResBankDistrib *this, CItem *entity)
{
	CResourceNode *node;
	int i;

	if (entity == NULL)
		return;

	node = entity->resourceEntity.firstChild;
	while (node != NULL) {
		if (node->id != 0 && node->type == 3)
			CResBankDistrib_ResourceNodeOp(this, node);
		node = node->next;
	}

	if (VT_IsMobile2(entity)) {
		CItem *child = ((CContainer *)entity)->contents;
		while (child != NULL) {
			CResBankDistrib_ProcessEntity(this, child);
			child = child->spatialNext;
		}
	}

	if (VT_IsMobile(entity)) {
		CMobile *mob = (CMobile *)entity;
		for (i = 0; i < 30; i++)
			CResBankDistrib_ProcessEntity(this, mob->equipment[i]);
	}
}

/*
 * 0x0043361F - CResBankDistrib::LoadBankDefs
 *
 * Main distribution loader. Clears maxSpawns on all regions, walks
 * blocks and players to process entities, parses bankdefs.txt for
 * factor/positive/max/min/set directives, computes per-region spawn
 * distributions from area-based sqrt formula.
 *
 * Fixed binary bug: when fileSize == 0, the binary returns without
 * calling fclose_ServerSide, leaking the ContainerHandle. Added
 * fclose_ServerSide call on that path.
 */
static void
CResBankDistrib_LoadBankDefs(CResBankDistrib *this)
{
	CResBankRegion *region;
	CPlayer *player;
	CString filename;
	FILE *fp;
	int fileSize;
	char *buf;
	char *pos;
	int factor;
	int i, j;
	int numResTypes;
	int regionCount;

	region = g_ResBankManager.first;
	while (region != NULL) {
		memset(&region->maxSpawns, 0, 0x198);
		region = region->next;
	}

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		CItem *item = g_SpatialGrid.cells[i].itemHead;
		while (item != NULL) {
			CResBankDistrib_ProcessEntity(this, item);
			item = item->spatialNext;
		}
	}

	player = g_PlayerList.head;
	while (player != NULL) {
		if ((player->pflags & 0x10000) && player->mobile.container.item.resourceEntity.entity.removedFromWorld != 0) {
			CResBankDistrib_ProcessEntity(this, (CItem *)player);
		}
		player = player->next;
	}

	CString_Constructor(&filename, "../.rundir/bankdefs.txt");
	fp = fopen_ServerSide(CString_GetBuffer(&filename), "r");
	if (fp == NULL) {
		CString_Destructor(&filename);
		return;
	}

	fseek_ServerSide(fp, 0, SEEK_END);
	fileSize = ftell_ServerSide(fp);
	buf = NULL;
	if (fileSize == 0) {
		fclose_ServerSide(fp); // FIXED: binary omits this.
		CString_Destructor(&filename);
		return;
	}

	fseek_ServerSide(fp, 0, SEEK_SET);
	buf = malloc(fileSize + 1);
	fread_ServerSide(buf, 1, fileSize, fp);
	fclose_ServerSide(fp);
	buf[fileSize] = '\0';

	factor = 1;
	pos = buf;
	if (CResBankDistrib_MatchPrefix(this, &pos, "factor ")) {
		if (!CResBankDistrib_ParseInt(this, &pos, &factor))
			factor = 1;
	}

	memset(this, 0, 0x66);
	for (i = 0; i < 102; i++) {
		this->maxVals[i] = 0x7FFFFFFF;
		this->minVals[i] = -0x7FFFFFFF;
	}

	pos = buf;
	while (*pos != '\0') {
		char nameBuf[128];
		CResourceType *resType;

		if (!CResBankDistrib_MatchPrefix(this, &pos, "positive "))
			break;
		if (!CResBankDistrib_ParseWord(this, &pos, nameBuf, 128))
			break;
		resType = CResourceTypeManager_FindByName(nameBuf);
		if (resType == NULL)
			break;
		if ((unsigned int)resType->typeId >= 102)
			break;
		this->positiveFlags[resType->typeId] = 1;
	}

	pos = buf;
	while (*pos != '\0') {
		char nameBuf[128];
		int val;
		CResourceType *resType;

		if (!CResBankDistrib_MatchPrefix(this, &pos, "max "))
			break;
		CResBankDistrib_ParseWord(this, &pos, nameBuf, 128);
		if (!CResBankDistrib_ParseInt(this, &pos, &val))
			break;
		resType = CResourceTypeManager_FindByName(nameBuf);
		if (resType == NULL)
			break;
		if ((unsigned int)resType->typeId >= 102)
			break;
		this->maxVals[resType->typeId] = val;
	}

	pos = buf;
	while (*pos != '\0') {
		char nameBuf[128];
		int val;
		CResourceType *resType;

		if (!CResBankDistrib_MatchPrefix(this, &pos, "min "))
			break;
		CResBankDistrib_ParseWord(this, &pos, nameBuf, 128);
		if (!CResBankDistrib_ParseInt(this, &pos, &val))
			break;
		resType = CResourceTypeManager_FindByName(nameBuf);
		if (resType == NULL)
			break;
		if ((unsigned int)resType->typeId >= 102)
			break;
		this->minVals[resType->typeId] = val;
	}

	regionCount = g_ResBankManager.regionCount;
	numResTypes = g_ResourceTypeCount;
	for (i = 0; i < regionCount; i++) {
		CResBankRegion *rgn = g_ResBankManager.hashTable[i];
		int width = rgn->x2 - rgn->x1;
		int height = rgn->y2 - rgn->y1;
		int area = width * height;
		int sqrtArea = (int)sqrt((double)area);
		int *maxSpawnsPtr;

		sqrtArea *= factor;
		maxSpawnsPtr = &rgn->maxSpawns[0];

		for (j = 0; j < numResTypes; j++) {
			int val = sqrtArea;
			int diff;
			CResBankDistrib_ClampToRange(this, &val, j);
			diff = val - *maxSpawnsPtr;
			CResBankDistrib_ClampNonNeg(this, &diff, j);
			CResBankRegion_SetTemplate(rgn, j, diff);
			maxSpawnsPtr++;
		}
	}

	pos = buf;
	while (*pos != '\0') {
		int x, y;
		char nameBuf[128];
		char opChar;
		int operand;
		CResBankRegion *targetRegion;

		if (!CResBankDistrib_MatchPrefix(this, &pos, "set "))
			break;
		if (!CResBankDistrib_ParseInt(this, &pos, &x))
			break;
		if (!CResBankDistrib_ParseInt(this, &pos, &y))
			break;
		if (!CResBankDistrib_ParseWord(this, &pos, nameBuf, 128))
			break;
		if (!CResBankDistrib_ParseChar(this, &pos, &opChar, "+-/*"))
			break;
		if (!CResBankDistrib_ParseInt(this, &pos, &operand))
			break;

		targetRegion = CResBankDistrib_FindRegion(this, x, y);
		if (targetRegion == NULL)
			continue;

		if (strcasecmp(nameBuf, "all") == 0) {
			for (j = 0; j < numResTypes; j++) {
				int val = CResBankRegion_GetQuantity(targetRegion, j);
				int diff;
				val += targetRegion->maxSpawns[j];
				switch (opChar) {
				case '*':
					val *= operand;
					break;
				case '+':
					val += operand;
					break;
				case '-':
					val -= operand;
					break;
				case '/':
					val /= operand;
					break;
				default:
					break;
				}
				CResBankDistrib_ClampToRange(this, &val, j);
				diff = val - targetRegion->maxSpawns[j];
				CResBankDistrib_ClampNonNeg(this, &diff, j);
				CResBankRegion_SetTemplate(targetRegion, j, diff);
			}
		} else {
			CResourceType *resType = CResourceTypeManager_FindByName(nameBuf);
			int bucket;
			int val;
			int diff;

			if (resType == NULL)
				break;
			if ((unsigned int)resType->typeId >= 102)
				break;
			bucket = resType->typeId;

			val = CResBankRegion_GetQuantity(targetRegion, bucket);
			val += targetRegion->maxSpawns[bucket];
			switch (opChar) {
			case '*':
				val *= operand;
				break;
			case '+':
				val += operand;
				break;
			case '-':
				val -= operand;
				break;
			case '/':
				val /= operand;
				break;
			default:
				break;
			}
			CResBankDistrib_ClampToRange(this, &val, bucket);
			diff = val - targetRegion->maxSpawns[bucket];
			CResBankDistrib_ClampNonNeg(this, &diff, bucket);
			CResBankRegion_SetTemplate(targetRegion, bucket, diff);
		}
	}

	free(buf);
	buf = NULL;
	region = g_ResBankManager.first;
	while (region != NULL) {
		int *maxPtr = &region->maxSpawns[0];
		for (j = 0; j < numResTypes; j++) {
			*maxPtr += CResBankRegion_GetQuantity(region, j);
			maxPtr++;
		}
		region->nospawn = 1;
		region = region->next;
	}

	CString_Destructor(&filename);
}

/*
 * 0x004341BD - LoadAll_ResBankDistrib
 *
 * Checks if any CResBankRegion has nospawn == 0. If so, constructs
 * a stack-local CResBankDistrib and calls LoadBankDefs to parse
 * bankdefs.txt and compute resource distributions.
 */
void
LoadAll_ResBankDistrib(void)
{
	int needsDistrib = 0;
	CResBankRegion *region = g_ResBankManager.first;

	while (region != NULL) {
		if (region->nospawn == 0) {
			needsDistrib = 1;
			break;
		}
		region = region->next;
	}

	if (needsDistrib) {
		CResBankDistrib distrib;
		CResBankDistrib_LoadBankDefs(&distrib);
	}
}

/*
 * 0x00434220 - CResBankRegion::GetQuantity
 *
 * Returns quantities[index] from the region.
 */
int
CResBankRegion_GetQuantity(CResBankRegion *this, int index)
{
	return this->quantities[index];
}

/*
 * Helper - CResBankRegion_HasSpawnEntry
 *
 * Returns 1 if region has a direct spawn entry for templateId. Used by
 * the FEAT_PERNPC_RESPAWN per-NPC respawn queue (npc.c death path) to
 * skip indirectly-spawned templates - e.g. liches that have no direct
 * cemetery entry and only appear via the Undead Group parent spawner
 * (template 1579 + poi_cleanup script). Without this gate, enqueueing
 * per-NPC respawns for indirectly-spawned mobs runs in parallel with
 * the parent's spawn mechanism and over-spawns monotonically over
 * hours, accumulating dozens of liches in a cemetery.
 */
int
CResBankRegion_HasSpawnEntry(CResBankRegion *region, uint16_t templateId)
{
	int i;

	if (region == NULL)
		return 0;
	for (i = 0; i < region->entryCount; i++) {
		if (region->spawnEntries[i].templateId == templateId)
			return 1;
	}
	return 0;
}

/*
 * 0x0043F2D0 - CResBankMagicCtx::Copy
 *
 * Copies a pointer from *src to *dst.
 */
void *
CResBankMagicCtx_Copy(CResListNode **dst, CResListNode **src)
{
	*dst = *src;
	return dst;
}

/*
 * 0x00461F80 - CResBankMagicCtx::PostInit
 *
 * No-op.
 */
void
CResBankMagicCtx_PostInit(CResListNode **obj)
{
	USED(obj);
}

/*
 * 0x004A83EF - StaticInit_CResBankManager
 *
 * Static-init wrapper that calls CResBankManager_Init.
 */
static __attribute__((unused)) void
StaticInit_CResBankManager(void)
{
	CResBankManager_Init();
}
/*
 * 0x004AD1B0 - IsSpecialTemplateId
 *
 * Returns 1 if templateId matches either of the two special template
 * IDs (g_ResTypeId_Meat, g_ResTypeId_CarnivoreMeat).
 */
__attribute__((unused)) static int
IsSpecialTemplateId(int templateId)
{
	if (templateId == g_ResTypeId_Meat)
		return 1;
	if (templateId == g_ResTypeId_CarnivoreMeat)
		return 1;
	return 0;
}

/*
 * 0x004AD1D4 - CheckTemplateOverLimit
 *
 * Binary stub that always returns 1, disabling the per-region
 * quantities[] budget system in CResBankRegion_CanSpawnTemplate.
 * The stub is deliberate: OSI turned the budget off pre-1998
 * because the closed-loop economy was already breaking ("There was
 * no longer a closed loop economy by 1998", Raph Koster).
 *
 * MODIFIED (FEAT_CLOSED_ECONOMY): returns 0 to re-activate the
 * per-region budget once resbank.mul coordinates are remapped to
 * match the 9 regions in resregions.txt. Not a bug fix - the stub
 * was intentional - but a feature-flagged restoration of the
 * pre-disable behavior.
 */
static int
CheckTemplateOverLimit(int templateId)
{
	if (feat(FEAT_CLOSED_ECONOMY)) {
		USED(templateId);
		return 0;
	}
	USED(templateId);
	return 1;
}

/*
 * 0x004AD1DE - GetTemplateName
 *
 * Returns g_TemplateNames[templateId], or "Template #N" formatted
 * into a shared static buffer if the slot is empty.
 */
__attribute__((unused)) static char *
GetTemplateName(int templateId)
{
	if (g_TemplateNames[templateId] != NULL)
		return g_TemplateNames[templateId];

	sprintf(g_TemplateNameBuf, "Template #%d", templateId);
	return g_TemplateNameBuf;
}

/*
 * 0x004AD217 - CResBankManager::Init
 *
 * Resets g_ResBankManager: clears the hash table, allocates the
 * "No Region" default region, and initializes list pointers and
 * respawn counters.
 */
void
CResBankManager_Init(void)
{
	int i;

	g_ResBankManager.regionCount = 0;

	for (i = 0; i < 256; i++)
		g_ResBankManager.hashTable[i] = NULL;

	g_ResBankManager.noRegion = malloc(sizeof(CResBankRegion));
	if (g_ResBankManager.noRegion != NULL)
		CResBankRegion_Constructor(g_ResBankManager.noRegion);

	strcpy(g_ResBankManager.noRegion->name, "No Region");

	g_ResBankManager.first = NULL;
	g_ResBankManager.current = NULL;
	g_ResBankManager.listCount = 0;
	g_ResBankManager.maxPerTick = 0x20; // 32
	g_ResBankManager.respawnChunkTimer = -1;
	g_ResBankManager.respawnDelay = 100;
}

/*
 * 0x004AD326 - CResBankManager::ResetCounters
 *
 * Zeroes the seven spawn statistics fields. In the binary these live inside
 * CResBankManager at offsets 0x41C-0x434; we store them as separate globals
 * for convenience.
 */
void
CResBankManager_ResetCounters(void)
{
	g_SpawnAttemptCount = 0;        // 0x41C
	g_SpawnNoTemplateCount = 0;     // 0x420
	g_SpawnSkipCount = 0;           // 0x424
	g_SpawnDensityRejectCount = 0;  // 0x428
	g_SpawnCanSpawnRejectCount = 0; // 0x42C
	g_SpawnLocationFailCount = 0;   // 0x430
	g_SpawnCreateFailCount = 0;     // 0x434
}

/*
 * 0x004AD38C - CResBankManager::PrintSpawnStatistics
 *
 * Logs a per-region spawn attempt/success/failure summary to the
 * "resbank"/"misc" event log category.
 */
void
CResBankManager_PrintSpawnStatistics(void)
{
	CString str;
	int totalFailed;
	int successful;

	totalFailed = g_SpawnNoTemplateCount + g_SpawnSkipCount + g_SpawnDensityRejectCount + g_SpawnCanSpawnRejectCount + g_SpawnLocationFailCount + g_SpawnCreateFailCount;

	successful = g_SpawnAttemptCount - totalFailed;

	CString_DefaultConstructor(&str);
	CString_ConcatInt(&str, g_SpawnAttemptCount);
	CString_AppendCStr(&str, " creations tried, ");
	CString_ConcatInt(&str, successful);
	CString_AppendCStr(&str, " successful, ");
	CString_ConcatInt(&str, totalFailed);
	CString_AppendCStr(&str, " failed (");
	CString_ConcatInt(&str, g_SpawnNoTemplateCount);
	CString_AppendCStr(&str, " pick, ");
	CString_ConcatInt(&str, g_SpawnSkipCount);
	CString_AppendCStr(&str, " cap, ");
	CString_ConcatInt(&str, g_SpawnDensityRejectCount);
	CString_AppendCStr(&str, " limit, ");
	CString_ConcatInt(&str, g_SpawnCanSpawnRejectCount);
	CString_AppendCStr(&str, " res, ");
	CString_ConcatInt(&str, g_SpawnLocationFailCount);
	CString_AppendCStr(&str, " spot, ");
	CString_ConcatInt(&str, g_SpawnCreateFailCount);
	CString_AppendCStr(&str, " generate) on ");
	CString_AppendCStr(&str, g_LVNDestBuf);

	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "resbank", "misc", CString_GetBuffer(&str));

	CString_Destructor(&str);
}

/*
 * 0x004AD55E - CResBankManager::ClearAllRegions
 *
 * Destroys the noRegion default and every region in the linked list.
 */
void
CResBankManager_ClearAllRegions(void)
{
	if (g_ResBankManager.noRegion != NULL)
		CResBankRegion_ScalarDelete(g_ResBankManager.noRegion, 1);

	while (g_ResBankManager.first != NULL) {
		CResBankManager_RemoveRegion(g_ResBankManager.first, 1);
	}

	g_ResBankManager.current = NULL;
}

/*
 * 0x004AD5BA - CResBankManager::LoadFallback
 *
 * Loads ../.rundir/resbank.mul in the older pre-magic format. Used
 * when LoadResBank fails to open the server-specific file or finds
 * an invalid magic. Supports the 0xF000..0xF002 version headers
 * (adds numTemplates and nospawn byte in later versions).
 */
static void
CResBankManager_LoadFallback(void)
{
	FILE *f;
	int32_t numRegions;
	uint16_t numTemplates;
	int hasNospawn;
	int32_t val32;
	int i, j, k;
	char nameBuf[80];
	CResBankRegion *search;

	if (g_ResBankManager.first == NULL)
		return;

	f = FileManager_OpenByType(0x3D, NULL, "rb");
	if (f == NULL)
		return;

	fread_ServerSide(&g_SpawnEnabled, 4, 1, f);
	SwapEndian(&g_SpawnEnabled);

	numRegions = 0;
	fread_ServerSide(&numRegions, 2, 1, f);
	SwapEndian(&numRegions);

	numTemplates = 0x4000;

	// Version check - numRegions in [0xF000..0xF002].
	if ((numRegions & 0xFFFF) < 0xF000 || (numRegions & 0xFFFF) > 0xF002)
		goto read_regions;

	if ((numRegions & 0xFFFF) >= 0xF001) {
		fread_ServerSide(&numTemplates, 2, 1, f);
		SwapEndian(&numTemplates);
	}

	hasNospawn = 0;
	if ((numRegions & 0xFFFF) >= 0xF002)
		hasNospawn = 1;

	fread_ServerSide(&numRegions, 2, 1, f);
	SwapEndian(&numRegions);

	for (i = 0; i < (numRegions & 0xFFFF); i++) {
		fread_ServerSide(&val32, 4, 1, f);
		SwapEndian(&val32);
		g_ResBankManager.hashTable[i]->minRespawnTime = val32;

		fread_ServerSide(&val32, 4, 1, f);
		SwapEndian(&val32);
		g_ResBankManager.hashTable[i]->maxRespawnTime = val32;

		if (hasNospawn) {
			fread_ServerSide(&g_ResBankManager.hashTable[i]->nospawn, 1, 1, f);
		}
	}

read_regions:
	for (j = 0; j < (numRegions & 0xFFFF); j++) {
		fread_ServerSide(nameBuf, 0x50, 1, f);

		search = g_ResBankManager.first;
		while (search != NULL) {
			if (strcasecmp(search->name, nameBuf) == 0)
				break;
			search = search->next;
		}

		if (search == NULL) {
			search = g_ResBankManager.first;
			if (search == NULL) {
				fclose_ServerSide(f);
				return;
			}
		}

		for (k = 0; k < (numTemplates & 0xFFFF); k++) {
			if (k >= RESBANK_MAX_TEMPLATES) {
				fread_ServerSide(&val32, 4, 1, f);
				fread_ServerSide(&val32, 4, 1, f);
			} else {
				fread_ServerSide(&val32, 4, 1, f);
				SwapEndian(&val32);
				CResBankRegion_SetTemplate(search, k, val32);

				fread_ServerSide(&val32, 4, 1, f);
				SwapEndian(&val32);
				search->maxSpawns[k] = val32;
			}
		}
	}

	fclose_ServerSide(f);
}

/*
 * 0x004AD8EC - CResBankManager::LoadResBank
 *
 * Loads ../.rundir/<server>/resbank.mul. Verifies the RESBANK_MAGIC
 * header, then reads per-region records (bounds, respawn timers,
 * template quantities). Falls back to LoadFallback on open failure
 * or bad magic. Regions not matching an existing entry are read
 * into a scratch buffer and discarded.
 */
void
CResBankManager_LoadResBank(void)
{
	FILE *f;
	CString pathStr;
	uint32_t magic;
	uint32_t flags;
	uint8_t recordType;
	int32_t x1, y1, x2, y2, minRespawn, maxRespawn;
	uint8_t nospawn;
	int32_t numTemplates;
	CResBankRegion *region, *search;
	int found;
	int remaining;
	CResBankRegion scratchRegion;
	int32_t templateVal, maxSpawn;
	int i;

	if (g_ResBankManager.first == NULL)
		return;

	CString_Constructor(&pathStr, "../.rundir/");
	CString_AppendCStr(&pathStr, g_Config.serverName);
	CString_AppendCStr(&pathStr, "/resbank.mul");

	f = fopen_ServerSide(CString_GetBuffer(&pathStr), "rb");
	if (f == NULL) {
		CResBankManager_LoadFallback();
		CString_Destructor(&pathStr);
		return;
	}

	fread_ServerSide(&magic, 4, 1, f);
	SwapEndian(&magic);
	if (magic != RESBANK_MAGIC) {
		fclose_ServerSide(f);
		CResBankManager_LoadFallback();
		CString_Destructor(&pathStr);
		return;
	}

	fread_ServerSide(&flags, 4, 1, f);
	SwapEndian(&flags);
	g_SpawnEnabled = (int)flags;
	g_SpawnEnabled = 1;

	remaining = g_ResBankManager.regionCount;

	// Scratch region on stack used as discard target when no match found.
	CResBankRegion_Constructor(&scratchRegion);

	while (1) {
		fread_ServerSide(&recordType, 1, 1, f);
		if (recordType == 0)
			break;
		if (recordType != 0x7F)
			break;

		fread_ServerSide(&x1, 4, 1, f);
		SwapEndian(&x1);
		fread_ServerSide(&y1, 4, 1, f);
		SwapEndian(&y1);
		fread_ServerSide(&x2, 4, 1, f);
		SwapEndian(&x2);
		fread_ServerSide(&y2, 4, 1, f);
		SwapEndian(&y2);
		fread_ServerSide(&minRespawn, 4, 1, f);
		SwapEndian(&minRespawn);
		fread_ServerSide(&maxRespawn, 4, 1, f);
		SwapEndian(&maxRespawn);
		fread_ServerSide(&nospawn, 1, 1, f);
		fread_ServerSide(&numTemplates, 4, 1, f);
		SwapEndian(&numTemplates);

		found = 0;
		region = NULL;
		for (search = g_ResBankManager.first; search != NULL; search = search->next) {
			if (search->x1 == x1 && search->y1 == y1 && search->x2 == x2 && search->y2 == y2) {
				found = 1;
				region = search;
				remaining--;
				break;
			}
		}

		if (!found)
			region = &scratchRegion;

		region->minRespawnTime = minRespawn;
		region->maxRespawnTime = maxRespawn;
		region->nospawn = nospawn;

		for (i = 0; i < numTemplates; i++) {
			fread_ServerSide(&templateVal, 4, 1, f);
			SwapEndian(&templateVal);
			CResBankRegion_SetTemplate(region, i, templateVal);

			fread_ServerSide(&maxSpawn, 4, 1, f);
			SwapEndian(&maxSpawn);
			region->maxSpawns[i] = maxSpawn;
		}
	}

	fclose_ServerSide(f);
	CResBankRegion_Destructor(&scratchRegion);
	CString_Destructor(&pathStr);
	USED(remaining);
}

/*
 * 0x004ADDD0 - CResBankManager::NoOp
 *
 * No-op.
 */
void
CResBankManager_NoOp(CResBankManager *this)
{
	USED(this);
}

/*
 * 0x004ADDDB - CResBankManager::WriteInt32
 *
 * Writes 4 bytes of value at *destPtr and advances both *destPtr
 * and *sizePtr by 4.
 */
static __attribute__((unused)) void
CResBankManager_WriteInt32(CResBankManager *this, char **destPtr, int32_t *sizePtr, int32_t value)
{
	USED(this);
	memcpy(*destPtr, &value, 4);
	*destPtr += 4;
	*sizePtr += 4;
}

/*
 * 0x004ADE16 - CResBankManager::ReadInt32
 *
 * Identical body to WriteInt32 (the binary deduplicates at two
 * addresses). Writes 4 bytes and advances the cursors.
 */
static __attribute__((unused)) void
CResBankManager_ReadInt32(CResBankManager *this, char **destPtr, int32_t *sizePtr, int32_t value)
{
	USED(this);
	memcpy(*destPtr, &value, 4);
	*destPtr += 4;
	*sizePtr += 4;
}

/*
 * 0x004ADE51 - CResBankManager::WriteByte
 *
 * Writes 1 byte of value at *destPtr and advances both *destPtr
 * and *sizePtr by 1.
 */
static __attribute__((unused)) void
CResBankManager_WriteByte(CResBankManager *this, char **destPtr, int32_t *sizePtr, uint8_t value)
{
	USED(this);
	memcpy(*destPtr, &value, 1);
	*destPtr += 1;
	*sizePtr += 1;
}

/*
 * 0x004ADE8C - CResBankManager::SaveResBank
 *
 * Serializes the resbank to ../.rundir/<server>/resbank.mul: magic,
 * g_SpawnEnabled, then per-region records (0x7F marker, bounds,
 * respawn timers, nospawn byte, 102 template quantity/maxSpawn
 * pairs), terminated by 0x00.
 *
 * The binary writes to an in-memory buffer via WriteInt32/WriteByte;
 * we write directly to file.
 */
void
CResBankManager_SaveResBank(void)
{
	FILE *f;
	char path[256];
	uint32_t magic;
	uint32_t flags;
	uint8_t recordType;
	CResBankRegion *region;
	int32_t numTemplates;
	int i;
	char lowerName[128];

	// Platform adaptation: build file path (binary uses buffer pointer arg).
	{
		int j;
		for (j = 0; j < (int)sizeof(lowerName) - 1 && g_Config.serverName[j]; j++)
			lowerName[j] = (g_Config.serverName[j] >= 'A' && g_Config.serverName[j] <= 'Z') ? g_Config.serverName[j] + 32 : g_Config.serverName[j];
		lowerName[j] = '\0';
	}
	snprintf(path, sizeof(path), "../.rundir/%s/resbank.mul", lowerName);

	f = fopen_ServerSide(path, "wb");
	if (f == NULL)
		return;

	magic = RESBANK_MAGIC;
	SwapEndian(&magic);
	fwrite_ServerSide(&magic, 4, 1, f);

	flags = g_SpawnEnabled;
	SwapEndian(&flags);
	fwrite_ServerSide(&flags, 4, 1, f);

	for (region = g_ResBankManager.first; region != NULL; region = region->next) {
		int32_t val;

		recordType = 0x7F;
		fwrite_ServerSide(&recordType, 1, 1, f);

		val = region->x1;
		SwapEndian(&val);
		fwrite_ServerSide(&val, 4, 1, f);
		val = region->y1;
		SwapEndian(&val);
		fwrite_ServerSide(&val, 4, 1, f);
		val = region->x2;
		SwapEndian(&val);
		fwrite_ServerSide(&val, 4, 1, f);
		val = region->y2;
		SwapEndian(&val);
		fwrite_ServerSide(&val, 4, 1, f);

		val = region->minRespawnTime;
		SwapEndian(&val);
		fwrite_ServerSide(&val, 4, 1, f);
		val = region->maxRespawnTime;
		SwapEndian(&val);
		fwrite_ServerSide(&val, 4, 1, f);
		fwrite_ServerSide(&region->nospawn, 1, 1, f);

		numTemplates = RESBANK_MAX_TEMPLATES;
		SwapEndian(&numTemplates);
		fwrite_ServerSide(&numTemplates, 4, 1, f);

		for (i = 0; i < RESBANK_MAX_TEMPLATES; i++) {
			val = CResBankRegion_GetQuantity(region, i);
			SwapEndian(&val);
			fwrite_ServerSide(&val, 4, 1, f);
			val = region->maxSpawns[i];
			SwapEndian(&val);
			fwrite_ServerSide(&val, 4, 1, f);
		}
	}

	recordType = 0;
	fwrite_ServerSide(&recordType, 1, 1, f);

	fclose_ServerSide(f);
}

/*
 * 0x004AE112 - GetElapsedMilliseconds
 *
 * Returns end - start in milliseconds. Each pointer points to a
 * {seconds, microseconds} pair.
 */
static int __attribute__((unused))
GetElapsedMilliseconds(const int32_t *start, const int32_t *end)
{
	int endMs, startMs;

	endMs = end[0] * 1000 + end[1] / 1000;
	startMs = start[0] * 1000 + start[1] / 1000;
	return endMs - startMs;
}

/*
 * 0x004AE153 - CResBankRegion::SpawnAttempt
 *
 * Tries up to maxAttempts spawns (10 normal, 50 initial). Stops on
 * first success or, outside initial spawn, after 75 ms. Returns 1
 * on any success.
 */
int
CResBankRegion_SpawnAttempt(CResBankRegion *region, int spawnFilter)
{
	int maxAttempts;
	int result;
	uint32_t startTime;

	startTime = GetTickCount_UO();

	maxAttempts = 10;
	if (g_IsInitialSpawn)
		maxAttempts = 50;

	result = 0;
	while (maxAttempts > 0) {
		int ret = CResBankRegion_TrySpawn(region, spawnFilter);
		if (ret != 0) {
			result = 1;
			break;
		}

		if (!g_IsInitialSpawn) {
			uint32_t now = GetTickCount_UO();
			if (now - startTime > 75)
				break;
		}

		maxAttempts--;
	}

	return result;
}

/*
 * 0x004AE1D5 - CResBankManager::SpawnTick
 *
 * Advances the round-robin region cursor up to maxPerTick times per
 * tick, running SpawnAttempt on each. Stops after 200 (initial) /
 * 40 successes, or after 100 ms outside the initial spawn.
 * spawnFilter 0 = all types, nonzero = guard/shopkeeper only.
 * Does not clear g_IsInitialSpawn; that is an admin command.
 */
void
CResBankManager_SpawnTick(int arg1, int spawnFilter)
{
	int maxAttempts;
	int remaining;
	int spawnCount;
	uint32_t startTime;

	USED(arg1);

	if (!g_SpawnEnabled)
		return;

	startTime = GetTickCount_UO();

	maxAttempts = 40;
	if (g_IsInitialSpawn)
		maxAttempts = 200;

	remaining = g_ResBankManager.maxPerTick;
	spawnCount = 0;

	while (remaining > 0) {
		if (g_ResBankManager.current == NULL)
			break;

		if (CResBankRegion_SpawnAttempt(g_ResBankManager.current, spawnFilter))
			spawnCount++;

		remaining--;

		g_ResBankManager.current = g_ResBankManager.current->next;

		if (g_ResBankManager.current == NULL)
			g_ResBankManager.current = g_ResBankManager.first;

		if (spawnCount >= maxAttempts)
			break;

		if (!g_IsInitialSpawn) {
			uint32_t now = GetTickCount_UO();
			if (now - startTime > 100)
				break;
		}
	}
}

/*
 * 0x004AE2B0 - CResBankManager::RespawnTimerCheck
 *
 * Starts a new respawn cycle if idle, or logs a warning that the
 * previous cycle still has chunks queued.
 */
void
CResBankManager_RespawnTimerCheck(void)
{
	if (g_ResBankManager.respawnChunkTimer == -1) {
		CResBankManager_InitRespawn();
	} else {
		CString str;
		int remaining;

		CString_Constructor(&str, "Warning: ");
		remaining = g_SpatialGrid.totalBlocks - g_ResBankManager.respawnChunkTimer;
		CString_ConcatInt(&str, remaining);
		CString_AppendCStr(&str, " chunks still queued to regrow "
		                         "when regrow was to restart");
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "resbank", "error", CString_GetBuffer(&str));
		CString_Destructor(&str);
	}
}

/*
 * 0x004AE362 - CResBankManager::ProcessRespawnChunks
 *
 * Called each tick; advances an in-progress respawn cycle by one
 * chunk.
 */
void
CResBankManager_ProcessRespawnChunks(void)
{
	if (g_ResBankManager.respawnChunkTimer != -1)
		CResBankManager_ProcessRespawnChunk(g_ResBankManager.respawnDelay);
}

/*
 * 0x004AE38F - CResBankManager::RebuildHash
 *
 * Rebuilds hashTable[] from the region linked list, up to 256
 * entries, assigning each region its index.
 */
void
CResBankManager_RebuildHash(void)
{
	CResBankRegion *region;

	region = g_ResBankManager.first;
	g_ResBankManager.regionCount = 0;

	while (region != NULL && g_ResBankManager.regionCount < 256) {
		region->regionIndex = g_ResBankManager.regionCount;
		g_ResBankManager.hashTable[g_ResBankManager.regionCount] = region;
		g_ResBankManager.regionCount++;
		region = region->next;
	}
}

/*
 * 0x004AE40E - CResBankManager::InsertRegion
 *
 * Appends region to the doubly-linked list and calls EnsureCurrent.
 */
void
CResBankManager_InsertRegion(CResBankRegion *region)
{
	region->prev = g_ResBankManager.last;
	if (region->prev != NULL)
		region->prev->next = region;
	g_ResBankManager.last = region;
	if (g_ResBankManager.first == NULL)
		g_ResBankManager.first = region;
	region->next = NULL;
	g_ResBankManager.listCount++;

	CResBankManager_EnsureCurrent();
}

/*
 * 0x004AE48B - CResBankManager::CreateRegion
 *
 * Allocates and constructs a named CResBankRegion with the given
 * bounds, appends it, and splits any overlapping existing regions.
 */
static CResBankRegion *
CResBankManager_CreateRegion(const char *name, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
	CResBankRegion *region;

	region = malloc(sizeof(CResBankRegion));
	if (region == NULL)
		return NULL;

	CResBankRegion_Constructor(region);
	strcpy(region->name, name);
	region->x1 = x1;
	region->y1 = y1;
	region->x2 = x2;
	region->y2 = y2;

	CResBankManager_InsertRegion(region);

	CResBankManager_SplitOverlappingRegions(region);

	return region;
}

/*
 * 0x004AE55A - CResBankManager::SplitOverlappingRegions
 *
 * Removes each region overlapping newRegion (by area, the smaller
 * one is removed) and re-creates the non-overlapping margins as
 * sub-regions when they extend at least 8 tiles past the boundary.
 */
static void
CResBankManager_SplitOverlappingRegions(CResBankRegion *newRegion)
{
	int i;
	CResBankRegion *existing, *smaller;
	int existingArea, newArea;
	int existingW, newW, existingH, newH;
	int leftMargin, topMargin, rightMargin, bottomMargin;

	for (i = 0; i < g_ResBankManager.regionCount; i++) {
		existing = g_ResBankManager.hashTable[i];

		if (!ValidateRegionBounds(newRegion->x1, newRegion->y1, newRegion->x2, newRegion->y2, existing->x1, existing->y1, existing->x2, existing->y2))
			continue;

		smaller = newRegion;
		existingArea = (existing->x2 - existing->x1) * (existing->y2 - existing->y1);
		newArea = (smaller->x2 - smaller->x1) * (smaller->y2 - smaller->y1);

		if (newArea < existingArea) {
			smaller = existing;
			existing = newRegion;
		}

		CResBankManager_RemoveRegion(smaller, 0);

		existingW = existing->x2 - existing->x1;
		newW = smaller->x2 - smaller->x1;
		existingH = existing->y2 - existing->y1;
		newH = smaller->y2 - smaller->y1;
		USED(existingW);
		USED(newW);
		USED(existingH);
		USED(newH);
		leftMargin = smaller->x1 - existing->x1;
		topMargin = smaller->y1 - existing->y1;

		// Margins under 8 tiles are absorbed into the smaller region.
		if (leftMargin < -7) {
			CResBankManager_CreateRegion(smaller->name, smaller->x1, smaller->y1, existing->x1 - 1, smaller->y2);
			smaller->x1 = existing->x1;
		}

		if (topMargin < -7) {
			CResBankManager_CreateRegion(smaller->name, smaller->x1, smaller->y1, smaller->x2, existing->y1 - 1);
			smaller->y1 = existing->y1;
		}

		rightMargin = smaller->x2 - existing->x2;
		bottomMargin = smaller->y2 - existing->y2;

		if (rightMargin > 7) {
			CResBankManager_CreateRegion(smaller->name, existing->x2 + 1, smaller->y1, smaller->x2, smaller->y2);
			smaller->x2 = existing->x2;
		}

		if (bottomMargin > 7) {
			CResBankManager_CreateRegion(smaller->name, smaller->x1, existing->y2 + 1, smaller->x2, smaller->y2);
		}

		CResBankRegion_Cleanup(smaller);
		free(smaller);
		break;
	}
}

/*
 * 0x004AE895 - CResBankManager::AddRegion
 *
 * Inserts the region, subdivides overlaps, and rebuilds the hash.
 */
void
CResBankManager_AddRegion(CResBankRegion *region)
{
	CResBankManager_InsertRegion(region);
	CResBankManager_SplitOverlappingRegions(region);
	CResBankManager_RebuildHash();
}

/*
 * 0x004AE8C2 - CResBankManager::RemoveRegion
 *
 * Unlinks region from the list (optionally destroying it), clears the
 * hash slot, rebuilds the hash, and refreshes the round-robin cursor.
 */
void
CResBankManager_RemoveRegion(CResBankRegion *region, int freeMemory)
{
	if (g_ResBankManager.current == region)
		g_ResBankManager.current = NULL;

	if (g_ResBankManager.last == region)
		g_ResBankManager.last = region->prev;

	if (g_ResBankManager.first == region)
		g_ResBankManager.first = region->next;

	if (region->prev != NULL)
		region->prev->next = region->next;

	if (region->next != NULL)
		region->next->prev = region->prev;

	region->prev = NULL;
	region->next = NULL;

	if (freeMemory) {
		CResBankRegion_Cleanup(region);
		free(region);
	}

	// Binary (0x004AE9A2): clear slot at [this + regionCount*4 + 0xC].
	// noRegion (0x0C) is contiguous with hashTable (0x10), so
	// (&noRegion)[regionCount] == hashTable[regionCount - 1].
	(&g_ResBankManager.noRegion)[g_ResBankManager.regionCount] = NULL;

	CResBankManager_RebuildHash();

	g_ResBankManager.listCount--;

	CResBankManager_EnsureCurrent();
}

/*
 * 0x004AE9E1 - CResBankManager::EnsureCurrent
 *
 * Ensures the round-robin cursor has a starting region.
 */
static void
CResBankManager_EnsureCurrent(void)
{
	if (g_ResBankManager.current == NULL)
		g_ResBankManager.current = g_ResBankManager.first;
}

/*
 * 0x004AEA00 - CResBankRegion::CResBankRegion
 *
 * Zeroes per-template arrays and hash table, sets non-zero defaults
 * (maxRespawnTime=9999, needsRebuild=1). Leaves coordinates, link
 * pointers, and name[] uninitialized.
 */
void
CResBankRegion_Constructor(CResBankRegion *region)
{
	int i;

	region->regionIndex = 0;
	region->minRespawnTime = 0;
	region->maxRespawnTime = 0x270F; // 9999
	region->needsRebuild = 1;

	for (i = 0; i < 0x4000; i++)
		region->competitionStockCache[i] = 0;

	region->templateDb = NULL;
	region->templateDbCapacity = 0;
	region->templateDbCount = 0;
	region->spawnEntries = NULL;
	region->spawnEntryCapacity = 0;
	region->entryCount = 0;
	region->totalFrequency = 0;
	region->field14924 = 0;
	region->field14928 = 0;

	for (i = 0; i < RESBANK_MAX_TEMPLATES; i++) {
		region->quantities[i] = 0;
		region->spawnedCounts[i] = 0;
		region->respawnTemplateId[i] = 0xFFFF;
		region->respawnAmount[i] = 0;
		region->respawnCountdown[i] = 0;
		region->field103D8[i] = 0;
		region->classEntryIndices[i] = 0;
		region->classTotalFrequency[i] = 0;
		region->classCapacity[i] = 0;
		region->classCount[i] = 0;
		region->maxSpawns[i] = 0;
	}

	for (i = 0; i < 0x1000; i++)
		region->templateHash[i] = 0;

	region->nospawn = 0;
	region->noWander = 0;
}

/*
 * 0x004AEBF9 - CResBankRegion::Cleanup
 *
 * Frees the region's templateDb, per-entry subRegionIds/scalingWts,
 * spawnEntries array, and per-template classEntryIndices, then zeros
 * the corresponding counts.
 */
void
CResBankRegion_Cleanup(CResBankRegion *region)
{
	int i;

	if (region->templateDb != NULL) {
		free(region->templateDb);
		region->templateDb = NULL;
	}

	if (region->spawnEntries != NULL) {
		for (i = 0; i < region->entryCount; i++) {
			ResSpawnEntry *entry = &region->spawnEntries[i];
			if (entry->subRegionIds != NULL) {
				free(entry->subRegionIds);
				entry->subRegionIds = NULL;
			}
			if (entry->scalingWts != NULL) {
				free(entry->scalingWts);
				entry->scalingWts = NULL;
			}
		}
		free(region->spawnEntries);
		region->spawnEntries = NULL;
	}

	region->templateDbCapacity = 0;
	region->templateDbCount = 0;
	region->spawnEntryCapacity = 0;
	region->entryCount = 0;
	region->totalFrequency = 0;

	for (i = 0; i < RESBANK_MAX_TEMPLATES; i++) {
		if (region->classEntryIndices[i] != 0) {
			free((void *)region->classEntryIndices[i]);
			region->classEntryIndices[i] = 0;
		}
		region->classTotalFrequency[i] = 0;
		region->classCapacity[i] = 0;
		region->classCount[i] = 0;
	}
}

/*
 * 0x004AEDFC - CResBankRegion::~CResBankRegion
 *
 * Delegates to Cleanup to free all region-owned allocations.
 */
static void
CResBankRegion_Destructor(CResBankRegion *region)
{
	CResBankRegion_Cleanup(region);
}

/*
 * 0x004AEE0F - ValidateResId
 *
 * Logs an overflow error and returns 0 when resId >= 102, else
 * returns 1. Used as a precondition guard for per-template arrays.
 */
static int
ValidateResId(int resId, const char *sourceFile, int lineNumber)
{
	char buf[256];

	if ((unsigned)resId >= RESBANK_MAX_TEMPLATES) {
		sprintf(buf, "%s: %d (type %u)", sourceFile, lineNumber, (unsigned)resId);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "residcheck", "error", buf);
		return 0;
	}
	return 1;
}

/*
 * 0x004AEE71 - CResBankRegion::SetTemplate
 *
 * Stores the initial quantity for a template slot.
 */
void
CResBankRegion_SetTemplate(CResBankRegion *region, int index, int32_t value)
{
	if (!ValidateTemplateId(index))
		return;
	region->quantities[index] = value;
}

/*
 * 0x004AEEA6 - CResBankRegion::AddToQuantity
 *
 * Adds amount to the per-template quantity pool at index, skipping on
 * an invalid template id.
 */
void
CResBankRegion_AddToQuantity(CResBankRegion *region, int index, int32_t amount)
{
	if (!ValidateTemplateId(index))
		return;
	region->quantities[index] += amount;
}

/*
 * 0x004AEEE4 - CResBankRegion::SubtractFromQuantity
 *
 * Subtracts amount from the per-template quantity pool at index,
 * skipping on an invalid template id.
 */
void
CResBankRegion_SubtractFromQuantity(CResBankRegion *region, int index, int32_t amount)
{
	if (!ValidateTemplateId(index))
		return;
	region->quantities[index] -= amount;
}

/*
 * 0x004AEF22 - CResBankRegion::AddToSpawnedCount
 *
 * Adds amount to the per-template spawned count at index, skipping on
 * an invalid template id.
 */
void
CResBankRegion_AddToSpawnedCount(CResBankRegion *region, int index, int32_t amount)
{
	if (!ValidateTemplateId(index))
		return;
	region->spawnedCounts[index] += amount;
}

/*
 * 0x004AEF68 - CResBankRegion::SubtractFromSpawnedCount
 *
 * Subtracts amount from the per-template spawned count at index,
 * skipping on an invalid template id.
 */
void
CResBankRegion_SubtractFromSpawnedCount(CResBankRegion *region, int index, int32_t amount)
{
	if (!ValidateTemplateId(index))
		return;
	region->spawnedCounts[index] -= amount;
}

/*
 * 0x004AEFAE - CResBankRegion::SubtractFromField103D8
 *
 * Subtracts amount from region->field103D8[index], skipping on an
 * invalid template id.
 */
void
CResBankRegion_SubtractFromField103D8(CResBankRegion *region, int index, int32_t amount)
{
	if (!ValidateTemplateId(index))
		return;
	region->field103D8[index] -= amount;
}

/*
 * 0x004AEFF4 - CResBankRegion::AddToField103D8
 *
 * Adds amount to region->field103D8[index], skipping on an invalid
 * template id.
 */
void
CResBankRegion_AddToField103D8(CResBankRegion *region, int index, int32_t amount)
{
	if (!ValidateTemplateId(index))
		return;
	region->field103D8[index] += amount;
}

/*
 * 0x004AF03A - ResBankLimitCheck
 *
 * Binary stub: 7 bytes at 0x004AF040 overwrote the argument-to-locals
 * initialization with `mov eax, 1; jmp 0x004AF0A5 (done)`, making the
 * CResourceNode walk at 0x004AF047-0x004AF0A0 unreachable. The dead
 * code tested each type==3 node's template against CheckTemplateOverLimit
 * and the region count table, returning 0 when over-limit. OSI did
 * the overwrite intentionally to disable the budget gate pre-1998
 * (same closed-economy retirement that stubbed CheckTemplateOverLimit).
 *
 * MODIFIED (FEAT_CLOSED_ECONOMY): restore the overwritten path so
 * the per-region `quantities[]` budget actually gates vendor stock
 * and other item creation through CTemplateManager_SpawnVendorStock
 * (caller at resbank.c:5248). When the feature is off we keep the
 * binary stub. Not a bug fix - the stub was intentional.
 */
int
ResBankLimitCheck(void *arg1, void *arg2)
{
	if (feat(FEAT_CLOSED_ECONOMY)) {
		CItem *entity = (CItem *)arg1;
		CLocation *loc = (CLocation *)arg2;
		CResBankRegion *region;
		CResourceNode *node;

		if (entity == NULL || loc == NULL)
			return 1;

		region = CResBankManager_GetRegionByLocation((int16_t)loc->x, (int16_t)loc->y);
		if (region == NULL || region == g_ResBankManager.noRegion)
			return 1;

		for (node = entity->resourceEntity.firstChild; node != NULL; node = node->next) {
			if (node->id == 0)
				continue;
			if ((int8_t)node->type != 3)
				continue;
			if (CheckTemplateOverLimit(node->id))
				continue;
			if (region->quantities[node->id] < 0)
				return 0;
		}
		return 1;
	}

	USED(arg1);
	USED(arg2);
	return 1;
}

/*
 * 0x004AF0A9 - HasTemplateInManager
 *
 * Returns 1 if the template id is registered in g_TemplatesRM.
 */
int
HasTemplateInManager(int templateId)
{
	return CResManager_HasByInt(&g_TemplatesRM, templateId);
}

/*
 * 0x004AF0CE - ResBankMagicCheck
 *
 * Binary stub that always returns 1 (magic enchantment always allowed).
 */
int
ResBankMagicCheck(CLocation *loc, int resTypeId)
{
	USED(loc);
	USED(resTypeId);
	return 1;
}

/*
 * 0x004AF0DD - ResBankResourceCheck
 *
 * Binary stub that always returns 1.
 */
int
ResBankResourceCheck(CLocation *loc, int resTypeId, int amount)
{
	USED(loc);
	USED(resTypeId);
	USED(amount);
	return 1;
}

/*
 * 0x004AF0EC - CResBankManager::ScheduleRespawnForTemplate
 *
 * Overwrite-on-priority. newPriority = amount * 0x3A; if newPriority >
 * currentPriority (respawnAmount * respawnCountdown), overwrite the slot and
 * set respawnCountdown = 0x3A. Otherwise drop the new reservation. Called from
 * Script_addConsumer; the slot is read back by GetRespawnTimer /
 * Script_whoIsLargestConsumer.
 */
void
CResBankManager_ScheduleRespawnForTemplate(CLocation *loc, int templateIndex, int16_t amount, int16_t templateValue)
{
	CResBankRegion *region;
	int32_t currentPriority, newPriority;

	if (!ValidateTemplateId(templateIndex))
		return;

	region = CResBankManager_GetRegionByLocation(loc->x, loc->y);
	if (region == NULL)
		return;

	if (region == g_ResBankManager.noRegion)
		return;

	// Binary uses movsx for respawnAmount (sign-extend int16) and movzx for
	// respawnCountdown (zero-extend uint8), then signed imul by the literal
	// 0x3A (0x004AF165) and stores 0x3A into respawnCountdown (0x004AF19D).
	currentPriority = (int32_t)(int16_t)region->respawnAmount[templateIndex] * (int32_t)region->respawnCountdown[templateIndex];
	newPriority = (int32_t)amount * 0x3A;

	if (newPriority <= currentPriority)
		return;

	region->respawnTemplateId[templateIndex] = (uint16_t)templateValue;
	region->respawnAmount[templateIndex] = (uint16_t)amount;
	region->respawnCountdown[templateIndex] = 0x3A;
}

/*
 * 0x004AF1AA - CResBankManager::GetRespawnTimer
 *
 * Returns respawnTemplateId[templateIndex] for the region at loc, or
 * -1 if the index is invalid or no region covers loc.
 */
int
CResBankManager_GetRespawnTimer(CLocation *loc, int templateIndex)
{
	CResBankRegion *region;

	if (!ValidateTemplateId(templateIndex))
		return -1;

	region = CResBankManager_GetRegionByLocation(loc->x, loc->y);
	if (region == NULL)
		return -1;

	if (region == g_ResBankManager.noRegion)
		return -1;

	return (int16_t)region->respawnTemplateId[templateIndex];
}

/*
 * 0x004AF217 - CResBankManager::InitRespawn
 *
 * Decrements each region's per-template countdown and clears expired
 * slots (respawnTemplateId=0xFFFF, respawnAmount=0). Resets
 * respawnChunkTimer so ProcessRespawnChunk can start walking blocks.
 */
void
CResBankManager_InitRespawn(void)
{
	CResBankRegion *region;
	int i;

	region = g_ResBankManager.first;
	while (region != NULL) {
		region->needsRebuild = 1;
		for (i = 0; i < RESBANK_MAX_TEMPLATES; i++) {
			if (region->respawnCountdown[i] == 0)
				continue;
			region->respawnCountdown[i]--;
			if (region->respawnCountdown[i] == 0) {
				region->respawnTemplateId[i] = 0xFFFF;
				region->respawnAmount[i] = 0;
			}
		}
		region = region->next;
	}

	g_ResBankManager.respawnChunkTimer = 0;
}

/*
 * 0x004AF2DB - CResBankManager::ProcessRespawnChunk
 *
 * Walks up to maxIterations map blocks from respawnChunkTimer, and for
 * each block's chunk egg adds the periodic respawn increment from each
 * type==3 resource node back into the region via SubtractFromQuantity.
 * Sets respawnChunkTimer = -1 when the last block is processed.
 */
int
CResBankManager_ProcessRespawnChunk(int maxIterations)
{
	int count;
	CBlock *block;
	CItem *egg;
	CResBankRegion *region;
	CResourceNode *node;
	int32_t amount;
	int spawned;

	count = 0;

	while (g_ResBankManager.respawnChunkTimer != -1 && g_ResBankManager.respawnChunkTimer < g_SpatialGrid.totalBlocks && count < maxIterations) {

		block = CBlockManager_GetBlock(&g_SpatialGrid, g_ResBankManager.respawnChunkTimer);
		g_ResBankManager.respawnChunkTimer++;
		count++;

		if (block == NULL)
			continue;

		egg = block->chunkEgg;
		if (egg == NULL)
			continue;

		region = CResBankManager_GetRegionByLocation(egg->resourceEntity.entity.location.x, egg->resourceEntity.entity.location.y);
		if (region == g_ResBankManager.noRegion)
			continue;

		spawned = 0;
		for (node = egg->resourceEntity.firstChild; node != NULL; node = node->next) {
			if (node->type != 3)
				continue;
			if (node->id == 0)
				continue;
			if (node->value2 == 0)
				continue;

			amount = node->value2;
			if (node->value3 + amount > node->value1)
				amount = node->value1 - node->value3;

			// Note: binary does NOT have an amount <= 0 check here.
			if (!CResBankRegion_CanSpawnTemplate(region, node->id))
				continue;

			CResBankRegion_SubtractFromQuantity(region, node->id, amount);

			if (!spawned) {
				spawned = 1;
				CResourceEntity_NotifyPreModify(egg);
			}

			node->value3 += amount;
		}

		if (spawned) {
			CResourceEntity_NotifyPostModify(egg);
			CResourceEntity_NotifyPostModifyIfActive(egg);
		}
	}

	if (g_ResBankManager.respawnChunkTimer >= g_SpatialGrid.totalBlocks)
		g_ResBankManager.respawnChunkTimer = -1;

	return count;
}

/*
 * 0x004AF4B1 - CResBankRegion::GetName
 *
 * Returns the region's name field.
 */
__attribute__((unused)) static char *
CResBankRegion_GetName(CResBankRegion *region)
{
	return region->name;
}

/*
 * 0x004AF4C4 - ClassifyTemplateItems
 *
 * Returns 0 if the template has no water resource (or is missing),
 * 1 if water is the only type-1 node, 2 if water and other type-1
 * nodes coexist.
 */
static int
ClassifyTemplateItems(int templateId)
{
	NPCTemplate *tmpl;
	CResourceNode *node;
	int hasSpecial = 0;
	int hasOther = 0;

	if (!CResManager_HasByInt(&g_TemplatesRM, (uint32_t)templateId))
		return 0;

	tmpl = CResManager_GetTemplateByID((uint16_t)templateId);
	node = tmpl->resourceNodes;

	while (node != NULL) {
		if (node->id == 0) {
			node = node->next;
			continue;
		}
		if ((int8_t)node->type != 1) {
			node = node->next;
			continue;
		}
		if (node->id == g_ResTypeId_Water)
			hasSpecial = 1;
		else
			hasOther = 1;
		node = node->next;
	}

	if (!hasSpecial)
		return 0;
	if (hasSpecial && !hasOther)
		return 1;
	return 2;
}

/*
 * 0x004AF57A - CResBankRegion::CanSpawnTemplate
 *
 * Returns 1 if every type==3 resource node in the template has budget
 * left in this region, 0 if any is depleted. During nospawn scavenge,
 * the threshold is maxSpawns[id] >> 4 instead of 0. In the shipped
 * demo, CheckTemplateOverLimit stubs this to a no-op.
 */
static int
CResBankRegion_CanSpawnTemplate(CResBankRegion *region, uint16_t templateId)
{
	NPCTemplate *tmpl;
	CResourceNode *node;

	if (!CResManager_HasByInt(&g_TemplatesRM, (uint32_t)templateId))
		return 0;

	tmpl = CResManager_GetTemplateByID(templateId);
	node = tmpl->resourceNodes;

	while (node != NULL) {
		uint16_t nodeId;
		int8_t nodeType;

		if (!ValidateResId(node->id, "D:\\TornadoAlley\\Projects\\UltimaOnline\\area\\resbank.cxx", 0x515)) {
			node = node->next;
			continue;
		}

		nodeId = node->id;
		nodeType = (int8_t)node->type;

		if (nodeId == 0) {
			node = node->next;
			continue;
		}

		if (nodeType != 3) {
			node = node->next;
			continue;
		}

		// Demo stub returns 1 (skip all nodes).
		if (CheckTemplateOverLimit(nodeId)) {
			node = node->next;
			continue;
		}

		// Dead in the demo (CheckTemplateOverLimit stub returns 1). Under
		// FEAT_CLOSED_ECONOMY the gate is live: the binary's initial-spawn
		// path keyed off `g_SpawningInProgress && region->nospawn` (verified
		// r2 @0x004AF61E), but our shipped resbank.mul carries nospawn=0, so
		// this is MODIFIED to also take the maxSpawns/16 path under the flag -
		// a resource stops spawning once its bank drops to 1/16 of capacity.
		if (g_SpawningInProgress && (region->nospawn || feat(FEAT_CLOSED_ECONOMY))) {
			int quota = region->maxSpawns[nodeId] >> 4;
			if (region->quantities[nodeId] > quota) {
				node = node->next;
				continue;
			}
		} else {
			if (region->quantities[nodeId] > 0) {
				node = node->next;
				continue;
			}
		}

		return 0;
	}

	return 1;
}

/*
 * 0x004AF695 - CResBankManager::RequestCreateNPC
 *
 * Spiral-searches outward from loc (up to maxDistance) for a walkable
 * spawn spot and returns 1 on success after updating loc->z from
 * CTerrainManager_CanWalkWrapper.
 */
int
CResBankManager_RequestCreateNPC(CLocation *loc, int maxDistance, uint32_t templateId)
{
	int origX, origY, z;
	int x, y;
	int height;
	int distance, direction;
	int limitX, limitY;
	int xStep, yStep;
	CLocation tmpLoc;
	int resultZ;

	origX = (int)(int16_t)loc->x;
	origY = (int)(int16_t)loc->y;
	z = (int)(int16_t)loc->z;
	x = origX;
	y = origY;
	distance = 0;
	direction = 0;
	limitX = 1;
	limitY = 1;
	xStep = 0;
	yStep = 0;

	height = GetCreatureHeight((uint16_t)templateId);

	for (;;) {
		CLocation_Init(&tmpLoc);
		CLocation_Set(&tmpLoc, (int16_t)x, (int16_t)y, (int16_t)z);
		resultZ = CTerrainManager_CanWalkWrapper(tmpLoc, -128, 127, 0x10, height, NULL, 0);
		if (resultZ != -128) {
			CLocation_Set(loc, (int16_t)x, (int16_t)y, (int16_t)resultZ);
			return 1;
		}

		if (distance > 0) {
			if ((x == limitX && xStep != 0) || (y == limitY && yStep != 0)) {
			} else {
				x += xStep;
				y += yStep;
				goto check_limit;
			}
		}

		if (distance == 0 || direction == 4) {
			distance++;
			direction = 0;
		}

		switch (direction) {
		case 0:
			x = origX - distance;
			y = origY - distance;
			limitX = origX + distance;
			xStep = 1;
			yStep = 0;
			direction++;
			break;
		case 1:
			x = origX - distance;
			y = origY + distance;
			limitX = origX + distance;
			xStep = 1;
			yStep = 0;
			direction++;
			break;
		case 2:
			x = origX - distance;
			y = origY - distance;
			limitY = origY + distance;
			xStep = 0;
			yStep = 1;
			direction++;
			break;
		case 3:
			x = origX + distance;
			y = origY - distance;
			limitY = origY + distance;
			xStep = 0;
			yStep = 1;
			direction++;
			break;
		}

check_limit:
		if (distance >= maxDistance)
			return 0;
	}
}

/*
 * 0x004AF8E3 - CanSpawnHere
 *
 * Returns 0 if loc is in a "nospawn" region or overlaps a multi,
 * otherwise 1.
 */
static int
CanSpawnHere(CLocation *loc)
{
	if (RegionManager_IsInRegion(loc, "nospawn"))
		return 0;

	if (Script_isAnyMultiAt(loc))
		return 0;

	return 1;
}

/*
 * 0x004AF91C - SpawnAtPointForLocation
 *
 * Looks up the region at (x, y) and delegates to
 * CResBankRegion_SpawnAtPoint.
 */
uint32_t
SpawnAtPointForLocation(uint16_t templateId, uint16_t x, uint16_t y, int16_t z, int noWander)
{
	CResBankRegion *region;

	region = CResBankManager_GetRegionByLocation(x, y);
	return CResBankRegion_SpawnAtPoint(region, templateId, x, y, z, noWander);
}

/*
 * 0x004AF97C - CResBankRegion::SpawnAtPoint
 *
 * Spawns templateId at (x, y, z) in region after passing budget,
 * CanSpawnHere, and FindSpawnSpot checks. Returns the new mob's
 * serial, or 0 with a failure counter incremented.
 */
static uint32_t
CResBankRegion_SpawnAtPoint(CResBankRegion *region, uint16_t templateId, uint16_t x, uint16_t y, int16_t z, int noWander)
{
	CLocation loc;
	int canCreate;
	int height;

	if (!CResBankRegion_CanSpawnTemplate(region, templateId)) {
		g_SpawnCanSpawnRejectCount++;
		return 0;
	}

	loc.x = x;
	loc.y = y;
	loc.z = z;

	if (!CanSpawnHere(&loc)) {
		g_SpawnLocationFailCount++;
		return 0;
	}

	height = GetCreatureHeight(templateId);

	if (!FindSpawnSpot(&loc, 0, noWander, 0x10, height, 0)) {
		g_SpawnLocationFailCount++;
		return 0;
	}

	canCreate = !CTemplateManager_HasAnimation(templateId);

	{
		CItem *result = CTemplateManager_CreateFromTemplate(templateId, &loc, canCreate, 0, NULL);
		if (result == NULL) {
			g_SpawnCreateFailCount++;
			return 0;
		}

		DeductSpawnFromBank(templateId, &loc);

		return result->serial;
	}
}

/*
 * 0x004AFA80 - CResBankRegion::SpawnAtPointInBox
 *
 * Finds a walkable spot inside the subregion box via FindSpawnSpotInBox
 * and spawns templateId there, returning the new mob or NULL.
 */
static CItem *
CResBankRegion_SpawnAtPointInBox(CResBankRegion *region, uint16_t templateId, SubRegion *sub)
{
	CItem *result;
	CLocation resultLoc;
	int height;
	int canCreate;

	result = NULL;

	if (!CResBankRegion_CanSpawnTemplate(region, templateId)) {
		g_SpawnCanSpawnRejectCount++;
		goto done;
	}

	// Binary: dead store.
	{
		int spawnFlags = g_TemplateFlags[templateId] & 3;
		USED(spawnFlags);
	}

	resultLoc.x = 0xFFFF;
	resultLoc.y = 0xFFFF;
	resultLoc.z = -1;

	height = GetCreatureHeight(templateId);

	if (!FindSpawnSpotInBox(&resultLoc, (int16_t)sub->x1, (int16_t)sub->y1, sub->z1, (int16_t)sub->x2, (int16_t)sub->y2, sub->z2, 10, 16, height, 0, CanSpawnHere)) {
		goto done;
	}

	canCreate = 1;
	if (CTemplateManager_HasAnimation(templateId))
		canCreate = 0;

	result = CTemplateManager_CreateFromTemplate(templateId, &resultLoc, canCreate, 0, NULL);

	if (result == NULL) {
		g_SpawnCreateFailCount++;
	} else {
		DeductSpawnFromBank(templateId, &resultLoc);
	}
done:
	return result;
}

/*
 * 0x004AFBC8 - CountMobilesInBox
 *
 * Counts entries in g_TemplateChain[templateId] whose creation location
 * falls inside the bounding box [x1..x2, y1..y2, z1..z2].
 */
static int
CountMobilesInBox(uint16_t templateId, int x1, int y1, int z1, int x2, int y2, int z2)
{
	CItem *cur;
	int count;
	int16_t mx, my, mz;

	count = 0;
	// Uses creation location (entity offsets 0x10/0x12/0x14), NOT current
	// location. The creation location is set once during spawn (CItem_Setup)
	// and never changes, so density checks work even if the NPC has wandered.
	for (cur = g_TemplateChain[templateId]; cur != NULL; cur = cur->templateChainNext) {
		int16_t *cloc = (int16_t *)&cur->resourceEntity.nextInContainer;
		mx = cloc[0];
		my = cloc[1];
		mz = cloc[2];
		if (mx >= x1 && my >= y1 && mz >= z1 && mx <= x2 && my <= y2 && mz <= z2)
			count++;
	}
	// CUSTOM (FEAT_PERNPC_RESPAWN): pending per-NPC respawn entries reserve
	// their slot in the density count - the entry's home tile counts as
	// occupied until PendingNPCRespawn_Tick fires and unlinks it. Without
	// this, SpawnTick's random walker would fill a dead NPC's slot within
	// 8 s and the per-NPC fire would be rejected by the density gate, so
	// the queue would be a no-op safety net instead of the authoritative
	// respawn clock. The walk is skipped when the queue is inactive.
	if (feat(FEAT_PERNPC_RESPAWN)) {
		PendingNPCRespawn *p;
		for (p = g_pendingNPCRespawnHead; p != NULL; p = p->next) {
			if (p->templateId != templateId)
				continue;
			if ((int)p->x >= x1 && (int)p->x <= x2 && (int)p->y >= y1 && (int)p->y <= y2 && (int)p->z >= z1 && (int)p->z <= z2)
				count++;
		}
	}
	return count;
}

/*
 * Helper - LocationInTemplateSubRegion
 *
 * CUSTOM: returns 1 if (x, y) lies inside a sub-region that directly
 * spawns templateId in the region. Identifies "direct-spawned" NPCs
 * (created by SpawnInSubRegion at a home tile in one of the template's
 * own sub-regions) versus "indirect" NPCs created by parent spawners
 * (e.g. Undead Group's lich child at CEMETERY_MOONGLOW's spawner spot,
 * outside any LICH_* bbox). Used by the per-NPC death-path gate so
 * indirect NPCs don't enter the per-NPC respawn queue at all - their
 * parent spawner handles respawn.
 */
int
LocationInTemplateSubRegion(uint16_t templateId, int16_t x, int16_t y)
{
	CResBankRegion *region;
	int i, j;
	ResSpawnEntry *entry;
	SubRegion *sub;

	region = CResBankManager_GetRegionByLocation(x, y);
	if (region == NULL || region == g_ResBankManager.noRegion)
		return 0;

	entry = NULL;
	for (i = 0; i < region->entryCount; i++) {
		if (region->spawnEntries[i].templateId == templateId) {
			entry = &region->spawnEntries[i];
			break;
		}
	}
	if (entry == NULL)
		return 0;

	for (j = 0; j < entry->numSubRegions; j++) {
		sub = (SubRegion *)((char *)region->templateDb + entry->subRegionIds[j] * sizeof(SubRegion));
		if ((int)x >= (int)sub->x1 && (int)x <= (int)sub->x2 && (int)y >= (int)sub->y1 && (int)y <= (int)sub->y2)
			return 1;
	}

	return 0;
}

/*
 * Helper - DensityAtCapForRespawn
 *
 * CUSTOM: returns 1 if the sub-region containing (x, y) is at or
 * above its density cap for templateId. The per-NPC respawn queue
 * (egg.c PendingNPCRespawn_Tick) uses this to gate
 * SpawnAtPointForLocation, which has no density check of its own
 * (it is the binary's "direct spawn here" API used by scripts);
 * without the gate the queue would compound spawns past the cap on
 * every cycle.
 *
 * Cap mirrors CResBankRegion_SpawnInSubRegion: regionlimit
 * (scalingWts) by default; overridden to area/2560 when the
 * sub-region's name contains "SCALING".
 */
int
DensityAtCapForRespawn(uint16_t templateId, int16_t x, int16_t y)
{
	CResBankRegion *region;
	int i, j;
	ResSpawnEntry *entry;
	SubRegion *sub;
	int existing;
	int cap;

	region = CResBankManager_GetRegionByLocation(x, y);
	if (region == NULL || region == g_ResBankManager.noRegion)
		return 0;

	entry = NULL;
	for (i = 0; i < region->entryCount; i++) {
		if (region->spawnEntries[i].templateId == templateId) {
			entry = &region->spawnEntries[i];
			break;
		}
	}
	if (entry == NULL)
		return 0;

	for (j = 0; j < entry->numSubRegions; j++) {
		sub = (SubRegion *)((char *)region->templateDb + entry->subRegionIds[j] * sizeof(SubRegion));
		if ((int)x < (int)sub->x1 || (int)x > (int)sub->x2)
			continue;
		if ((int)y < (int)sub->y1 || (int)y > (int)sub->y2)
			continue;

		cap = entry->scalingWts[j];
		if (strstr(sub->name, "SCALING") != NULL) {
			int dx = (int)sub->x2 - (int)sub->x1;
			int dy = (int)sub->y2 - (int)sub->y1;
			cap = (dx * dy) / 2560;
		}

		// Floor at 1 to match SpawnInSubRegion's cap==0 fix.
		if (cap == 0)
			cap = 1;

		existing = CountMobilesInBox(templateId, (int)sub->x1, (int)sub->y1, (int)sub->z1, (int)sub->x2, (int)sub->y2, (int)sub->z2);
		return existing >= cap;
	}

	return 0;
}

/*
 * 0x004AFC4B - CResBankRegion::SpawnInSubRegion
 *
 * Picks a random sub-region from entry, computes a density cap,
 * counts existing mobiles of templateId (and its paired id offset
 * 1000), and spawns a new mob at a random point inside the box (or
 * at the midpoint when noWander is set).
 *
 * Cap defaults to regionlimit (scalingWts); when the sub-region's
 * name contains the substring "SCALING", the cap is overridden to
 * area/2560.
 *
 * FIXED: shopkeeper multi-box sub-region density.
 * When noWander is set, count density across the union of all
 * same-named sub-regions instead of only the chosen box - shops
 * often span multiple adjacent boxes that the binary checks in
 * isolation.
 *
 * FIXED: count == 0 fall-through. For SCALING
 * sub-regions with area < 2560, the binary's density gate is
 * skipped. Floor count at 1 so the gate always engages.
 */
static CItem *
CResBankRegion_SpawnInSubRegion(CResBankRegion *region, ResSpawnEntry *entry, int noWander)
{
	int randomIndex;
	int subRegionIdx;
	int count;
	SubRegion *sub;
	int existingCount;

	randomIndex = GetRandomRange(0, entry->numSubRegions - 1);
	subRegionIdx = entry->subRegionIds[randomIndex];
	count = entry->scalingWts[randomIndex];
	sub = (SubRegion *)((char *)region->templateDb + subRegionIdx * sizeof(SubRegion));

	if (strstr(sub->name, "SCALING") != NULL) {
		int dx = (int)sub->x2 - (int)sub->x1;
		int dy = (int)sub->y2 - (int)sub->y1;
		count = (dx * dy) / 2560;
	}

	if (count == 0)
		count = 1;

	{
		int checkX1, checkY1, checkZ1, checkX2, checkY2, checkZ2;

		checkX1 = (int)sub->x1;
		checkY1 = (int)sub->y1;
		checkZ1 = (int)sub->z1;
		checkX2 = (int)sub->x2;
		checkY2 = (int)sub->y2;
		checkZ2 = (int)sub->z2;
		// FIXED: union bbox for shopkeepers (noWander) in small
		// sub-regions. Regular NPCs use individual sub-region bbox
		// matching the binary.
		if (noWander && ((int)sub->x2 - (int)sub->x1) * ((int)sub->y2 - (int)sub->y1) < 2560) {
			int si;
			for (si = 0; si < entry->numSubRegions; si++) {
				SubRegion *other = (SubRegion *)((char *)region->templateDb + entry->subRegionIds[si] * sizeof(SubRegion));
				if (strcmp(sub->name, other->name) != 0)
					continue;
				if ((int)other->x1 < checkX1)
					checkX1 = (int)other->x1;
				if ((int)other->y1 < checkY1)
					checkY1 = (int)other->y1;
				if ((int)other->z1 < checkZ1)
					checkZ1 = (int)other->z1;
				if ((int)other->x2 > checkX2)
					checkX2 = (int)other->x2;
				if ((int)other->y2 > checkY2)
					checkY2 = (int)other->y2;
				if ((int)other->z2 > checkZ2)
					checkZ2 = (int)other->z2;
			}
		}

		existingCount = CountMobilesInBox(entry->templateId, checkX1, checkY1, checkZ1, checkX2, checkY2, checkZ2);

		// Companion NPCs share density caps with their principal via
		// the paired templateId+1000 (e.g. shopkeeper + bodyguard).
		if (entry->templateId < 199) {
			existingCount += CountMobilesInBox(entry->templateId + 1000, checkX1, checkY1, checkZ1, checkX2, checkY2, checkZ2);
		} else if (entry->templateId >= 1000 && entry->templateId <= 1199) {
			existingCount += CountMobilesInBox(entry->templateId - 1000, checkX1, checkY1, checkZ1, checkX2, checkY2, checkZ2);
		}

		if (existingCount + 1 > count) {
			g_SpawnDensityRejectCount++;
			return NULL;
		}
	}

	if (noWander) {
		int midX, midY;
		int16_t z;
		uint32_t serial;
		CItem *mob;

		midX = ((int)sub->x2 - (int)sub->x1) / 2 + (int)sub->x1;
		midY = ((int)sub->y2 - (int)sub->y1) / 2 + (int)sub->y1;
		z = sub->z1;
		if (z > sub->z2)
			z = sub->z2;

		serial = CResBankRegion_SpawnAtPoint(region, entry->templateId, (uint16_t)midX, (uint16_t)midY, z, 6);

		if (serial == 0)
			return NULL;

		mob = CWorld_FindBySerial(g_World, serial);
		if (mob == NULL)
			return NULL;

		{
			int16_t mx, my, mz;
			mx = (int16_t)mob->resourceEntity.entity.location.x;
			my = (int16_t)mob->resourceEntity.entity.location.y;
			mz = mob->resourceEntity.entity.location.z;

			if (mx >= sub->x1 && mx < sub->x2 && my >= sub->y1 && my < sub->y2 && mz >= sub->z1 && mz < sub->z2)
				return mob;
		}

		if (mob != NULL)
			((void (*)(void *))VT_FN(mob, VT_DELETE))(mob);
		return NULL;
	} else {
		return CResBankRegion_SpawnAtPointInBox(region, entry->templateId, sub);
	}
}

/*
 * 0x004B01D4 - CResBankManager::SpawnForTemplate
 *
 * Walks all regions, delegating to CResBankRegion::SpawnForTemplate,
 * and returns the sum of spawn counts.
 */
int
CResBankManager_SpawnForTemplate(int templateFilter)
{
	int totalCount;
	CResBankRegion *region;

	totalCount = 0;

	region = g_ResBankManager.first;

	while (region != NULL) {
		totalCount += CResBankRegion_SpawnForTemplate(region, templateFilter);
		region = region->next;
	}

	return totalCount;
}

/*
 * 0x004B021D - CResBankRegion::SpawnForTemplate
 *
 * Spawns one mob per matching entry in the region. templateFilter == -1
 * matches any entry whose template has an animation; any other value
 * matches entries with that exact templateId. Returns the number of
 * spawn attempts made.
 */
static int
CResBankRegion_SpawnForTemplate(CResBankRegion *region, int templateFilter)
{
	int spawnCount;
	int totalFreq;
	int remaining;
	ResSpawnEntry *entry;
	NPCTemplate *tmpl;
	int noWander;

	spawnCount = 0;
	totalFreq = region->totalFrequency;
	USED(totalFreq);

	entry = region->spawnEntries;
	remaining = region->entryCount;

	while (entry != NULL && remaining > 0) {
		int match = 0;

		if (templateFilter == -1) {
			if (CTemplateManager_HasAnimation(entry->templateId))
				match = 1;
		}
		if (!match) {
			if ((int)entry->templateId == templateFilter)
				match = 1;
			else
				goto next;
		}

		tmpl = CResManager_GetTemplateByID(entry->templateId);
		if (tmpl == NULL)
			goto next;

		noWander = CResBankRegion_GetExactPosition(region, tmpl);

		g_SpawningInProgress = 1;
		CResBankRegion_SpawnInSubRegion(region, entry, noWander);
		g_SpawningInProgress = 0;

		spawnCount++;

next:
		entry++;
		remaining--;
	}

	return spawnCount;
}

/*
 * 0x004B030A - CResBankRegion::GetRandomTemplate
 *
 * Selects a spawn entry using frequency-weighted roulette across
 * region->spawnEntries.
 */
ResSpawnEntry *
CResBankRegion_GetRandomTemplate(CResBankRegion *region)
{
	int pick, remaining;
	ResSpawnEntry *entry;

	pick = GetRandomRange(0, region->totalFrequency);
	entry = region->spawnEntries;
	remaining = region->entryCount;

	for (;;) {
		if (pick <= (int)entry->frequency)
			break;
		if (remaining == 0)
			break;
		pick -= (int)entry->frequency;
		entry++;
		remaining--;
	}

	return entry;
}

/*
 * 0x004B0384 - GetSpawnLocation
 *
 * Returns the template for entry, or NULL in conditional mode when the
 * template is not a shopkeeper (type 4) and not a guard (type 3, with
 * a 25% spawn roll).
 */
static NPCTemplate *
GetSpawnLocation(ResSpawnEntry *entry, int conditional)
{
	NPCTemplate *tmpl;

	tmpl = CResManager_GetTemplateByID(entry->templateId);

	if (conditional) {
		// Type 3 (guard): 25% chance to spawn.
		if (tmpl->type == 3) {
			if (GetRandomRange(1, 4) != 2)
				return NULL;
		}
		// Type 4 (shopkeeper): always OK; all other types: skip.
		else if (tmpl->type != 4) {
			return NULL;
		}
	}

	return tmpl;
}

/*
 * 0x004B03EF - CResBankRegion::GetExactPosition
 *
 * Returns 1 if the template is a shopkeeper - the NPC must stay at its
 * spawn spot (spawned at the sub-region midpoint, density-checked with
 * the union bbox across same-named sub-regions).
 *
 * MODIFIED: the binary additionally required region->noWander==1, which
 * SetRegionWanderFlags only sets when the resbank region name starts
 * with "city". The shipped resregions.txt (a CUSTOM file in this
 * codebase, hand-authored, not from the demo's authoritative data) uses
 * bare names (Yew, Minoc, Britain, ...) without a city_ prefix, so the
 * binary's check would never fire. This isn't a binary bug; it's a
 * data-shape mismatch. Drop the region->noWander==1 half of the test as
 * a workaround so the shopkeeper-anchored behavior activates on the
 * shipped data.
 */
static int
CResBankRegion_GetExactPosition(CResBankRegion *region, NPCTemplate *tmpl)
{
	USED(region);
	if (tmpl->type == 4)
		return 1;
	return 0;
}

/*
 * 0x004B042F - CResBankRegion::TrySpawn
 *
 * Single spawn attempt in region. Returns 1 on success or when the
 * region has no data, 0 on any abort (DEFCON full, type-filter reject,
 * spawn failure). spawnFilter 0 spawns all types; any other value
 * restricts to guards and shopkeepers.
 */
int
CResBankRegion_TrySpawn(CResBankRegion *region, int spawnFilter)
{
	ResSpawnEntry *entry;
	NPCTemplate *tmpl;
	int noWander;
	CItem *result;

	if (region->totalFrequency == 0 || region->spawnEntries == NULL)
		return 1;

	g_SpawnAttemptCount++;

	entry = CResBankRegion_GetRandomTemplate(region);
	if (entry == NULL) {
		g_SpawnNoTemplateCount++;
		return 0;
	}
	// DEFCON server load gate: skip spawn if at NPC capacity.
	if (CDefcon_IsFull() == 1) {
		if (CTemplateManager_HasAnimationVariant(entry->templateId)) {
			g_SpawnSkipCount++;
			return 0;
		}
	}

	tmpl = GetSpawnLocation(entry, spawnFilter);
	if (tmpl == NULL)
		return 0;
	noWander = CResBankRegion_GetExactPosition(region, tmpl);

	g_SpawningInProgress = 1;
	result = CResBankRegion_SpawnInSubRegion(region, entry, noWander);
	g_SpawningInProgress = 0;

	return result != NULL;
}

/*
 * 0x004B0527 - CResBankRegion::ContainsPoint
 *
 * Returns 1 when (x, y) lies within the region's bounding rectangle.
 */
int
CResBankRegion_ContainsPoint(CResBankRegion *region, int16_t x, int16_t y)
{
	if ((int)x < region->x1)
		return 0;
	if ((int)x > region->x2)
		return 0;
	if ((int)y < region->y1)
		return 0;
	if ((int)y > region->y2)
		return 0;
	return 1;
}

/*
 * 0x004B0579 - CResBankManager::GetRegionByLocation
 *
 * Linear-scans the region list for one containing (x, y); falls back
 * to g_ResBankManager.noRegion on miss.
 */
CResBankRegion *
CResBankManager_GetRegionByLocation(int16_t x, int16_t y)
{
	CResBankRegion *r;

	for (r = g_ResBankManager.first; r != NULL; r = r->next) {
		if (CResBankRegion_ContainsPoint(r, x, y))
			return r;
	}
	return g_ResBankManager.noRegion;
}

/*
 * 0x004B05CB - CResBankManager::UpdateTemplateData
 *
 * Binary no-op.
 */
void
CResBankManager_UpdateTemplateData(CResBankManager *self, int templateId, const char *name, const char *data, uint16_t flags)
{
	USED(self);
	USED(templateId);
	USED(name);
	USED(data);
	USED(flags);
}

/*
 * 0x004B05D8 - CResBankManager::DeleteTemplateData
 *
 * Binary no-op.
 */
void
CResBankManager_DeleteTemplateData(CResBankManager *self, int templateId)
{
	USED(self);
	USED(templateId);
}

/*
 * 0x004B05E5 - RegisterEggInBlock
 *
 * Aggregates entity's template-level resource nodes into the block's
 * chunkEgg: allocates the chunkEgg on first use, merges each type==3
 * node into any existing matching node (summing value1/value2 and
 * resetting value3), or adds a new one. Wraps with PreModify /
 * PostModify / PostModifyIfActive notifications.
 */
void
RegisterEggInBlock(CItem *entity)
{
	int blockIdx;
	MapBlock *block;
	CItem *chunkEgg;
	CResourceNode *srcNode, *dstNode;
	uint16_t bodyType;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &entity->resourceEntity.entity.location, 0);

	block = &g_MapBlocks[blockIdx];
	chunkEgg = block->eggHead;

	if (chunkEgg == NULL) {
		chunkEgg = malloc(sizeof(CItem));
		if (chunkEgg != NULL)
			CEgg_Constructor(chunkEgg);

		block->eggHead = chunkEgg;
		chunkEgg = block->eggHead;
	}

	if (chunkEgg->resourceEntity.entity.removedFromWorld)
		((void (*)(void *, CLocation *))VT_FN(chunkEgg, VT_DROP_AT_FEET))(chunkEgg, &entity->resourceEntity.entity.location);

	CResourceEntity_NotifyPreModify(chunkEgg);

	bodyType = entity->resourceEntity.entity.bodyType;

	if (!CWorld_IsValidItemResource(bodyType))
		return;

	srcNode = g_ResEntitySlots[bodyType].nodeHead;

	while (srcNode != NULL) {
		if ((int8_t)srcNode->type != 3) {
			srcNode = srcNode->next;
			continue;
		}

		if (srcNode->id == 0) {
			srcNode = srcNode->next;
			continue;
		}

		dstNode = CResourceEntity_FindNode(chunkEgg, srcNode->id, 3);

		if (dstNode != NULL) {
			dstNode->value1 += srcNode->value1;
			dstNode->value2 += srcNode->value2;
			dstNode->value3 += srcNode->value1;
		} else {
			CResourceEntity_AddNode(chunkEgg, srcNode->id, (int8_t)srcNode->type, srcNode->value1, srcNode->value2, srcNode->value1, 0);
		}

		srcNode = srcNode->next;
	}

	CResourceEntity_NotifyPostModify(chunkEgg);
	CResourceEntity_NotifyPostModifyIfActive(chunkEgg);
}

/*
 * 0x004B07CB - IsAnimalBody
 *
 * Returns 1 if bodyId is in one of the hardcoded animal/wildlife
 * ranges.
 */
int
IsAnimalBody(int bodyId)
{
	if (bodyId >= 0xE4 && bodyId <= 0xE7)
		return 1; // deer, rabbit
	if (bodyId >= 0xF4 && bodyId <= 0xF7)
		return 1; // dogs
	if (bodyId >= 0x104 && bodyId <= 0x107)
		return 1; // birds
	if (bodyId >= 0x122 && bodyId <= 0x125)
		return 1; // pack animals
	if (bodyId >= 0x1D3 && bodyId <= 0x1DA)
		return 1; // cows, sheep
	if (bodyId >= 0x21F && bodyId <= 0x230)
		return 1; // wildlife
	if (bodyId >= 0x235 && bodyId <= 0x238)
		return 1; // wildlife
	if (bodyId >= 0x25A && bodyId <= 0x261)
		return 1; // wildlife
	if (bodyId >= 0x266 && bodyId <= 0x26D)
		return 1; // wildlife
	if (bodyId >= 0x2BC && bodyId <= 0x2CB)
		return 1; // wildlife
	if (bodyId >= 0x6DA && bodyId <= 0x6DD)
		return 1; // animals
	if (bodyId >= 0x6EB && bodyId <= 0x6FE)
		return 1; // animals
	if (bodyId >= 0x70D && bodyId <= 0x714)
		return 1; // animals
	if (bodyId >= 0x71D && bodyId <= 0x720)
		return 1; // animals
	if (bodyId >= 0x72B && bodyId <= 0x732)
		return 1; // animals
	if (bodyId >= 0x73B && bodyId <= 0x73E)
		return 1; // animals
	if (bodyId >= 0x749 && bodyId <= 0x750)
		return 1; // animals
	if (bodyId >= 0x759 && bodyId <= 0x75C)
		return 1; // animals
	if (bodyId >= 0x7C1 && bodyId <= 0x7C8)
		return 1; // animals
	if (bodyId >= 0x7D1 && bodyId <= 0x7D4)
		return 1; // animals
	return 0;
}

/*
 * 0x004B0971 - GetBodyType
 *
 * Classifies bodyId into one of 12 spawn categories (0=monsters,
 * 2=human, 4=animal, 7=equipment, 8=sea, 10=humanoid, etc.), or -1
 * when no range matches. Later matches overwrite earlier ones.
 */
int
GetBodyType(int bodyId)
{
	int result = -1;

	// Checks in order, later match overwrites earlier.
	if (bodyId >= 0x245 && bodyId <= 0x26D)
		result = 0;
	if (bodyId >= 0xC4 && bodyId <= 0xC7)
		result = 1;
	if (bodyId >= 0x03 && bodyId <= 0x06)
		result = 2;
	if (bodyId >= 0xAC && bodyId <= 0xAF)
		result = 3;
	if (IsAnimalBody(bodyId))
		result = 4;
	if (bodyId >= 0x16 && bodyId <= 0x19)
		result = 5;
	if (bodyId >= 0x11A && bodyId <= 0x11D)
		result = 6;
	if (bodyId >= 0x3DC0 && bodyId <= 0x3DF1)
		result = 7;
	if (IsWaterTile(bodyId))
		result = 8;
	if (bodyId >= 0x71 && bodyId <= 0x78)
		result = 9;
	if ((bodyId >= 0x09 && bodyId <= 0x15) || (bodyId >= 0x150 && bodyId <= 0x15C))
		result = 10;
	if (bodyId >= 0x1F4 && bodyId <= 0x1F7)
		result = 11;

	return result;
}

/*
 * 0x004B0AA5 - AddResourceNodesByCategory
 *
 * Looks up the body-type category's template via
 * g_BodyTypeSerials[categoryIdx] and merges each of its resource nodes
 * (scaled by count) into eggEntity, either by adding to an existing
 * matching node or creating a new one.
 */
static void
AddResourceNodesByCategory(CItem *eggEntity, int categoryIdx, int count)
{
	uint32_t resSn;
	ResEntitySlot *resData;
	CResourceNode *child;
	CResourceNode *found;
	int32_t v1, v2, v3;
	uint8_t nodeType;
	uint16_t nodeId;

	if (categoryIdx >= 0xd)
		return;

	resSn = g_BodyTypeSerials[categoryIdx];

	if ((resSn & 0xFFFF) >= 0x4000)
		return;

	resData = &g_ResEntitySlots[resSn];

	child = resData->nodeHead;
	while (child != NULL) {
		nodeId = child->id;
		if (nodeId == 0) {
			child = child->next;
			continue;
		}

		v1 = child->value1 * count;
		v2 = child->value2 * count;
		v3 = child->value1 * count; // binary: value3 = value1 * count

		nodeType = child->type;
		nodeId = child->id;

		found = CResourceEntity_FindNode(eggEntity, nodeId, 3);

		if (found != NULL) {
			found->value1 += v1;
			found->value2 += v2;
			found->value3 += v3;
		} else {
			CResourceEntity_AddNode(eggEntity, nodeId, (int8_t)nodeType, v1, v2, v3, 0);
		}

		child = child->next;
	}
}

/*
 * 0x004B0C31 - ProcessStaticTiles
 *
 * For each 8x8 map block, counts terrain tiles per body-type category
 * and aggregates their resource templates into a chunk egg placed at a
 * random land cell in the block. Creates eggs lazily the first time
 * any category has a nonzero count.
 *
 * FIXED: off-by-one at 0x004B0D77 used tileId > 0x4000 to skip
 * non-land tiles, letting tileId=0x4000 through into tileCount[0x4000]
 * which overflows the 0x4000-byte buffer. The second loop at
 * 0x004B0DEE correctly uses < 0x4000; matched that here by changing
 * the first loop's guard to >= 0x4000.
 */
static void
ProcessStaticTiles(void)
{
	int blockCol, blockRow;
	int blockIdx;
	uint8_t tileCount[0x4000];
	uint8_t cellHasLand[64];
	uint8_t categoryCounts[13];
	int cellY, cellX;
	int tileId;
	int bodyTypeIdx;
	CItem *eggEntity;
	CLocation loc;
	int randCellX, randCellY;
	int catCount;

	loc.x = 0xFFFF;
	loc.y = 0xFFFF;
	loc.z = (int16_t)0xFFFF;

	for (blockCol = 0; blockCol < g_SpatialGrid.gridWidth; blockCol++) {
		for (blockRow = 0; blockRow < g_SpatialGrid.gridHeight; blockRow++) {
			blockIdx = blockCol * g_SpatialGrid.gridHeight + blockRow;

			memset(tileCount, 0, 0x4000);
			memset(cellHasLand, 0, 0x40);

			for (cellX = 0; cellX < 8; cellX++) {
				for (cellY = 0; cellY < 8; cellY++) {
					tileId = g_MapBlocks[blockIdx].cells[cellX + cellY * 8].tileID;

					if (tileId >= 0x4000)
						continue;

					tileCount[tileId]++;
					cellHasLand[cellX + cellY * 8] = 1;
				}
			}

			memset(categoryCounts, 0, 0xd);

			for (tileId = 0; tileId < 0x4000; tileId++) {
				if (tileCount[tileId] <= 0)
					continue;
				bodyTypeIdx = GetBodyType(tileId);
				if (bodyTypeIdx < 0)
					continue;
				categoryCounts[bodyTypeIdx] += tileCount[tileId];
			}

			eggEntity = NULL;

			for (bodyTypeIdx = 0; bodyTypeIdx < 0xd; bodyTypeIdx++) {
				if (categoryCounts[bodyTypeIdx] <= 0)
					continue;

				if (eggEntity == NULL) {
					eggEntity = g_MapBlocks[blockIdx].eggHead;

					if (eggEntity == NULL) {
						CItem *newEgg = malloc(sizeof(CItem));
						if (newEgg != NULL)
							CEgg_Constructor(newEgg);
						g_MapBlocks[blockIdx].eggHead = newEgg;
						eggEntity = g_MapBlocks[blockIdx].eggHead;
					} else {
						if (eggEntity->resourceEntity.entity.removedFromWorld == 0)
							((void (*)(void *))VT_FN(eggEntity, 0x0C))(eggEntity);
					}

					randCellX = 4;
					randCellY = 4;
					while (!cellHasLand[randCellX + randCellY * 8]) {
						randCellX = GetRandomRange(0, 7);
						randCellY = GetRandomRange(0, 7);
					}

					CLocation_Set(&loc, (uint16_t)(g_mapStartX + blockCol * 8 + randCellX), (uint16_t)(g_mapStartY + blockRow * 8 + randCellY),
					        g_MapBlocks[blockIdx].cells[0].z);

					((void (*)(void *, CLocation *))VT_FN(eggEntity, 0x04))(eggEntity, &loc);

					CResourceEntity_NotifyPreModify(eggEntity);
				}

				catCount = (int)(int8_t)categoryCounts[bodyTypeIdx];
				if (catCount == 0)
					catCount = 1;
				AddResourceNodesByCategory(eggEntity, bodyTypeIdx, catCount);
			}

			if (eggEntity != NULL) {
				CResourceEntity_NotifyPostModify(eggEntity);
				CResourceEntity_NotifyPostModifyIfActive(eggEntity);
			}
		}
	}
}

/*
 * 0x004B10C7 - ResBankManager_ProcessDynamicEggEntities
 *
 * Walks every spatial-grid cell, subtracts the remaining (value1 -
 * value3) of each egg's type==3 resource nodes from the region's
 * quantities, and deletes the egg. g_ProcessingEggs gates CEgg's
 * Delete override so the same entities can't be freed during normal
 * gameplay.
 */
void
ResBankManager_ProcessDynamicEggEntities(void)
{
	int i;
	g_ProcessingEggs = 1;

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		CItem *entity, *next;

		entity = g_SpatialGrid.cells[i].itemHead;
		g_SpatialGrid.cells[i].chunkEgg = NULL;

		while (entity != NULL) {
			CResourceNode *node;

			next = entity->spatialNext;

			// CEgg returns 1 (process), CItem returns 0 (skip).
			if (!((int (*)(void *))VT_FN(entity, VT_ITEM_CHECK_9C))(entity)) {
				entity = next;
				continue;
			}

			if (!entity->resourceEntity.entity.removedFromWorld)
				((void (*)(void *))VT_FN(entity, VT_HIDE))(entity);

			for (node = entity->resourceEntity.firstChild; node != NULL; node = node->next) {
				int amount;
				CResBankRegion *region;

				if (node->id == 0)
					continue;
				if (node->type != 3)
					continue;

				amount = node->value1 - node->value3;
				if (amount <= 0)
					continue;

				region = CResBankManager_GetRegionByLocation(entity->resourceEntity.entity.location.x, entity->resourceEntity.entity.location.y);

				CResBankRegion_SubtractFromQuantity(region, (int)node->id, amount);
			}

			g_SuppressRespawn = 1;

			if (entity != NULL)
				((void (*)(void *))VT_FN(entity, VT_DELETE))(entity);

			g_SuppressRespawn = 0;

			entity = next;
		}
	}

	g_ProcessingEggs = 0;
}

/*
 * 0x004B122C - AccumulateEggCounts
 *
 * For each type==3 resource node on entity, accumulates
 * maxCounts[id] += value1 and curCounts[id] += value3.
 */
static void
AccumulateEggCounts(CItem *entity, int *maxCounts, int *curCounts)
{
	CResourceNode *node;

	for (node = entity->resourceEntity.firstChild; node != NULL; node = node->next) {
		int templateId;

		if (node->id == 0)
			continue;
		templateId = (int)node->id;

		if (node->type != 3)
			continue;

		maxCounts[templateId] += node->value1;
		curCounts[templateId] += node->value3;
	}
}

/*
 * 0x004B12A6 - BuildSpawnEntries
 *
 * Rebuilds per-block egg entities: backs up existing eggHeads,
 * regenerates eggs from static tiles and statics, then merges each
 * new egg with its backup (adding missing nodes, returning excess
 * quantity to regions, deleting the new egg and restoring the
 * merged-into old one).
 */
void
BuildSpawnEntries(void)
{
	int i, j;
	int totalBlocks;
	CItem **savedEggs;
	int newMaxCounts[RESBANK_MAX_TEMPLATES];
	int newCurCounts[RESBANK_MAX_TEMPLATES];
	int oldMaxCounts[RESBANK_MAX_TEMPLATES];
	int oldCurCounts[RESBANK_MAX_TEMPLATES];

	totalBlocks = g_MapBlocksW * g_MapBlocksH;

	g_BuildingSpawnEntries = 1;

	savedEggs = (CItem **)malloc(totalBlocks * sizeof(CItem *));

	for (i = 0; i < totalBlocks; i++) {
		savedEggs[i] = g_MapBlocks[i].eggHead;
		g_MapBlocks[i].eggHead = NULL;
		if (savedEggs[i] != NULL)
			CResourceEntity_NotifyPreModify(savedEggs[i]);
	}

	ProcessStaticTiles();

	for (i = 0; i < totalBlocks; i++) {
		CItem *entity = g_MapBlocks[i].staticHead;
		while (entity != NULL) {
			RegisterEggInBlock(entity);
			entity = entity->resourceEntity.nextInContainer;
		}
	}

	for (i = 0; i < totalBlocks; i++) {
		CItem *newEgg = g_MapBlocks[i].eggHead;
		CItem *oldEgg = savedEggs[i];

		if (newEgg == NULL) {
			if (oldEgg != NULL) {
				CItem *toDelete = oldEgg;
				g_ProcessingEggs = 1;
				g_SuppressRespawn = 1;
				if (toDelete != NULL) {
					((void (*)(void *))VT_FN(toDelete, VT_DELETE))(toDelete);
				}
				g_SuppressRespawn = 0;
				g_ProcessingEggs = 0;
				savedEggs[i] = NULL;
			}
			continue;
		}

		if (oldEgg == NULL)
			continue;

		CResourceEntity_NotifyPreModify(newEgg);

		memset(newMaxCounts, 0, sizeof(newMaxCounts));
		memset(newCurCounts, 0, sizeof(newCurCounts));
		memset(oldMaxCounts, 0, sizeof(oldMaxCounts));
		memset(oldCurCounts, 0, sizeof(oldCurCounts));

		AccumulateEggCounts(newEgg, newMaxCounts, newCurCounts);
		AccumulateEggCounts(oldEgg, oldMaxCounts, oldCurCounts);

		for (j = 0; j < RESBANK_MAX_TEMPLATES; j++) {
			int delta;
			CResourceNode *node, *nextNode;
			CResourceNode *found;
			int excess;

			if (newMaxCounts[j] == oldMaxCounts[j])
				continue;

			delta = newMaxCounts[j] - oldMaxCounts[j];

			if (delta > 0) {
				found = CResourceEntity_FindNode(oldEgg, (uint16_t)j, 3);
				if (found != NULL) {
					found->value1 += delta;
					found->value3 += delta;
				} else {
					CResourceEntity_AddNode(oldEgg, (uint16_t)j, 3, delta, 1, delta, 0);
				}
			} else {
				delta = -delta;
				node = oldEgg->resourceEntity.firstChild;
				excess = 0;

				while (node != NULL && delta > 0) {
					nextNode = node->next;

					if (node->id == (uint16_t)j && node->type == 3) {
						if (delta >= node->value1) {
							excess += node->value1 - node->value3;
							delta -= node->value1;
							CResourceEntity_RemoveNode(oldEgg, node);
							ResourceNode_ReturnToPool(node);
						} else {
							node->value1 -= delta;
							if (node->value3 >= delta) {
								node->value3 -= delta;
							} else {
								excess += delta - node->value3;
								node->value3 = 0;
							}
							delta = 0;
						}
					}

					node = nextNode;
				}

				if (excess > 0) {
					CLocation loc;
					CResBankRegion *region;

					CLocation_Init(&loc);
					CLocation_Set(&loc, oldEgg->resourceEntity.entity.location.x, oldEgg->resourceEntity.entity.location.y, 0);

					region = CResBankManager_GetRegionByLocation((int16_t)loc.x, (int16_t)loc.y);
					CResBankRegion_SubtractFromQuantity(region, j, excess);
				}
			}
		}

		{
			CItem *toDelete = g_MapBlocks[i].eggHead;
			g_ProcessingEggs = 1;
			g_SuppressRespawn = 1;
			if (toDelete != NULL)
				((void (*)(void *))VT_FN(toDelete, VT_DELETE))(toDelete);
			g_SuppressRespawn = 0;
			g_ProcessingEggs = 0;

			g_MapBlocks[i].eggHead = savedEggs[i];
			savedEggs[i] = NULL;

			{
				CItem *restored = g_MapBlocks[i].eggHead;
				CResourceEntity_NotifyPostModify(restored);
				CResourceEntity_NotifyPostModifyIfActive(restored);
			}
		}
	}

	free(savedEggs);
	g_BuildingSpawnEntries = 0;
}

/*
 * 0x004B1982 - EnsureSpawnEntriesBuilt
 *
 * If any block already carries a chunk egg (loaded from a prior save),
 * the binary's spawn-entries are already built; return without rebuild.
 * Otherwise call BuildSpawnEntries to do the full pass.
 */
void
EnsureSpawnEntriesBuilt(void)
{
	int i;

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		if (g_MapBlocks[i].eggHead != NULL)
			return;
	}

	BuildSpawnEntries();
}

/*
 * 0x004B1BB0 - CClassification::InsertEntry
 *
 * Appends entryIdx to the classEntryIndices[templateId] array (growing
 * it by 8 slots when full) and adds frequency into classTotalFrequency.
 */
static void
CClassification_InsertEntry(CResBankRegion *region, int entryIdx, uint16_t templateId, int frequency)
{
	uint16_t count, capacity;
	uint16_t *newArr;

	count = region->classCount[templateId];
	capacity = region->classCapacity[templateId];

	if (count >= capacity) {
		region->classCapacity[templateId] += 8;

		newArr = (uint16_t *)malloc(region->classCapacity[templateId] * 2);

		if (region->classEntryIndices[templateId] != 0) {
			memcpy(newArr, (void *)region->classEntryIndices[templateId], (region->classCapacity[templateId] - 8) * 2);
			free((void *)region->classEntryIndices[templateId]);
		}

		region->classEntryIndices[templateId] = (uintptr_t)newArr;
	}

	((uint16_t *)region->classEntryIndices[templateId])[region->classCount[templateId]] = (uint16_t)entryIdx;
	region->classCount[templateId]++;
	region->classTotalFrequency[templateId] += frequency;
}

/*
 * 0x004B1CFA - CClassification::BuildTable
 *
 * For each spawn entry, indexes it by every type==0 resource-node id
 * on its template, populating the per-template classEntryIndices
 * arrays.
 *
 * FIXED: binary iterates region->entryCount without bounds-checking
 * the node id used as array index. Cap the loop at
 * RESBANK_MAX_TEMPLATES to avoid overflowing the fixed arrays.
 */
void
CClassification_BuildTable(CResBankRegion *region)
{
	int i;
	int limit;
	NPCTemplate *tmpl;
	CResourceNode *node;
	uint16_t templateId;
	int entryFreq;

	limit = region->entryCount;
	if (limit > RESBANK_MAX_TEMPLATES)
		limit = RESBANK_MAX_TEMPLATES;

	for (i = 0; i < limit; i++) {
		templateId = ((ResSpawnEntry *)region->spawnEntries)[i].templateId;
		tmpl = CResManager_GetTemplateByID(templateId);
		node = tmpl->resourceNodes;

		while (node != NULL) {
			if (node->id != 0 && node->type == 0) {
				entryFreq = ((ResSpawnEntry *)region->spawnEntries)[i].frequency;
				CClassification_InsertEntry(region, i, node->id, entryFreq);
			}

			node = node->next;
		}
	}
}

/*
 * 0x004B1DC0 - CResBankRegion::AddSubRegionToEntry
 *
 * Appends subRegionIdx (and the scaling weight looked up from the
 * template's regionlimitList under g_RegionNames[regionNameIdx]) to
 * the entry's subRegionIds/scalingWts arrays, growing them by 16
 * slots when full.
 */
static void
CResBankRegion_AddSubRegionToEntry(CResBankRegion *region, ResSpawnEntry *entry, uint16_t subRegionIdx, uint16_t regionNameIdx)
{
	uint16_t scalingWt;
	CString nameStr;
	NPCTemplate *tmpl;
	CResListNode *iterA;
	CResListNode *iterB;
	CResListNode *outBuf;
	CResListNode **retPtr;
	CResListNode *iterCopy;

	USED(region);

	CResBankMagicCtx_DefaultConstructor(&iterA);
	CResBankMagicCtx_DefaultConstructor(&iterB);
	CString_Constructor(&nameStr, g_RegionNames[regionNameIdx]);

	iterCopy = iterA;
	tmpl = CResManager_GetTemplateByID(entry->templateId);
	retPtr = CStringList_FindByName(&tmpl->regionlimitList.list, &outBuf, &nameStr, iterCopy, 1);

	CResBankMagicCtx_CopyConstructor(&iterB, retPtr);
	CResBankMagicCtx_PostInit(&outBuf);
	CString_Destructor(&nameStr);

	scalingWt = 0;
	if (CStringList_HasEntries((CStringList *)&iterB)) {
		tmpl = CResManager_GetTemplateByID(entry->templateId);
		void *valPtr = CStringList_GetValueEffects((CStringList *)&tmpl->regionlimitList.list, &iterB);
		scalingWt = (uint16_t)*(int *)valPtr;
	}

	if (entry->numSubRegions >= entry->subRegionCapacity) {
		uint16_t *newIds, *newWts;

		entry->subRegionCapacity += 0x10;

		newIds = (uint16_t *)malloc(entry->subRegionCapacity * sizeof(uint16_t));
		newWts = (uint16_t *)malloc(entry->subRegionCapacity * sizeof(uint16_t));

		if (entry->subRegionIds != NULL) {
			memcpy(newIds, entry->subRegionIds, (entry->subRegionCapacity - 0x10) * sizeof(uint16_t));
			free(entry->subRegionIds);

			memcpy(newWts, entry->scalingWts, (entry->subRegionCapacity - 0x10) * sizeof(uint16_t));
			free(entry->scalingWts);
		}

		entry->subRegionIds = newIds;
		entry->scalingWts = newWts;
	}

	entry->subRegionIds[entry->numSubRegions] = subRegionIdx;
	entry->scalingWts[entry->numSubRegions] = scalingWt;
	entry->numSubRegions++;

	CResBankMagicCtx_PostInit(&iterB);
	CResBankMagicCtx_PostInit(&iterA);
}

/*
 * 0x004B2013 - CResBankRegion::AddSpawnEntry
 *
 * Adds templateId's spawn entry to region if its frequency falls
 * within [minRespawnTime, maxRespawnTime]. Creates a new entry in the
 * spawnEntries array (growing by 64 slots as needed) on first use for
 * this template, then attaches subRegionIdx/regionNameIdx via
 * AddSubRegionToEntry.
 */
static void
CResBankRegion_AddSpawnEntry(CResBankRegion *region, uint16_t templateId, uint16_t subRegionIdx, uint16_t regionNameIdx)
{
	NPCTemplate *tmpl;
	int frequency;
	int i;
	ResSpawnEntry *entry;

	tmpl = CResManager_GetTemplateByID(templateId);
	frequency = (int)tmpl->frequency;

	if (frequency < region->minRespawnTime)
		return;
	if (frequency > region->maxRespawnTime)
		return;

	for (i = 0; i < region->entryCount; i++) {
		entry = &region->spawnEntries[i];
		if (entry->templateId == templateId) {
			CResBankRegion_AddSubRegionToEntry(region, entry, subRegionIdx, regionNameIdx);
			return;
		}
	}

	if (region->entryCount >= region->spawnEntryCapacity) {
		ResSpawnEntry *newBuf;

		region->spawnEntryCapacity += 0x40;

		newBuf = (ResSpawnEntry *)malloc(region->spawnEntryCapacity * sizeof(ResSpawnEntry));

		if (region->spawnEntries != NULL) {
			memcpy(newBuf, region->spawnEntries, (region->spawnEntryCapacity - 0x40) * sizeof(ResSpawnEntry));
			free(region->spawnEntries);
		}

		region->spawnEntries = newBuf;
	}

	entry = &region->spawnEntries[region->entryCount];
	entry->subRegionCapacity = 0;
	entry->numSubRegions = 0;
	entry->subRegionIds = NULL;
	entry->scalingWts = NULL;
	entry->frequency = (uint16_t)frequency;

	region->totalFrequency += frequency;

	entry->templateId = templateId;
	region->entryCount++;

	CResBankRegion_AddSubRegionToEntry(region, &region->spawnEntries[region->entryCount - 1], subRegionIdx, regionNameIdx);
}

/*
 * 0x004B225A - CResBankRegion::AddTemplateDbEntry
 *
 * Appends a SubRegion (bounds, z-range, name) to region->templateDb,
 * growing the array by 16 slots when full, and returns the new index.
 */
static int
CResBankRegion_AddTemplateDbEntry(CResBankRegion *region, int x1, int y1, int x2, int y2, int z1, int z2, int ni, const char *name)
{
	SubRegion *sub;

	if (region->templateDbCount >= region->templateDbCapacity) {
		void *newBuf;

		region->templateDbCapacity += 0x10;

		newBuf = malloc(region->templateDbCapacity * sizeof(SubRegion));

		if (region->templateDb != NULL) {
			memcpy(newBuf, region->templateDb, (region->templateDbCapacity - 0x10) * sizeof(SubRegion));
			free(region->templateDb);
		}

		region->templateDb = newBuf;
	}

	sub = (SubRegion *)((char *)region->templateDb + region->templateDbCount * sizeof(SubRegion));

	sub->x1 = (uint16_t)x1;
	sub->y1 = (uint16_t)y1;
	sub->x2 = (uint16_t)x2;
	sub->y2 = (uint16_t)y2;
	sub->z1 = (int16_t)z1;
	sub->z2 = (int16_t)z2;

	strcpy(sub->name, name);

	sub->regionNameIndex = (uint16_t)ni;

	region->templateDbCount++;
	return region->templateDbCount - 1;
}

/*
 * 0x004B241C - FinalizeClassification
 *
 * Walks the geographic region manager, matches each region name to
 * the registered template tags in g_RegionNames[], and for every
 * match clamps the region bounds to each CResBankRegion and adds a
 * sub-region plus the templates registered under that tag.
 *
 * FIXED (FEAT_SPAWN_STRICT_TAGS): the binary prefix-matches tags as
 * raw strncasecmp, so "ORC" matches ORC_2, ORCSMALL_*, ORCLARGE_*, ...
 * Under FEAT_SPAWN_STRICT_TAGS the character after the prefix must be
 * '_' or '\0' so tags behave as underscore-delimited tokens.
 */
static void
FinalizeClassification(void)
{
	int ni, ti;
	CSearchCtx iterCtx, beginCtx, nextCtx;

	CSearchCtx_Constructor(&iterCtx);
	CSearchCtx_Add(&iterCtx, CResManager_BeginIterWrapper(&g_RegionByNameRM, &beginCtx));
	while (CSearchCtx_Find(&iterCtx)) {
		CRegion *geoReg = *(CRegion **)CResManager_GetResultCtx(&g_RegionByNameRM, &iterCtx);

		CSearchCtx_Add(&iterCtx, CResManager_NextIterWrapper(&g_RegionByNameRM, &nextCtx, &iterCtx));

		for (ni = 0; ni < g_RegionNameCount; ni++) {
			CResBankRegion *region;
			int nameLen;
			char boundary;

			nameLen = (int)strlen(g_RegionNames[ni]);

			if (strncasecmp(geoReg->name, g_RegionNames[ni], nameLen) != 0)
				continue;

			if (feat(FEAT_SPAWN_STRICT_TAGS)) {
				boundary = geoReg->name[nameLen];
				if (boundary != '\0' && boundary != '_')
					continue;
			}

			region = g_ResBankManager.first;
			while (region != NULL) {
				int geoZ1, geoZ2;
				int cx1, cy1, cx2, cy2;

				cx1 = (int)geoReg->x;
				cy1 = (int)geoReg->y;
				cx2 = (int)geoReg->x + (int)geoReg->width;
				cy2 = (int)geoReg->y + (int)geoReg->height;
				geoZ1 = (int)geoReg->zMin;
				geoZ2 = (int)geoReg->zMax;

				if (cx1 < region->x1)
					cx1 = region->x1;
				if (cx2 < region->x1)
					cx2 = region->x1;
				if (cx1 > region->x2)
					cx1 = region->x2;
				if (cx2 > region->x2)
					cx2 = region->x2;
				if (cy1 < region->y1)
					cy1 = region->y1;
				if (cy2 < region->y1)
					cy2 = region->y1;
				if (cy1 > region->y2)
					cy1 = region->y2;
				if (cy2 > region->y2)
					cy2 = region->y2;

				if (cx1 != cx2 && cy1 != cy2) {
					int subIdx;

					subIdx = CResBankRegion_AddTemplateDbEntry(region, cx1, cy1, cx2, cy2, geoZ1, geoZ2, ni, geoReg->name);

					for (ti = 0; ti < (int)g_RegionTemplateCount[ni]; ti++) {
						uint16_t tmplId = g_RegionTemplateIds[ni][ti];
						CResBankRegion_AddSpawnEntry(region, tmplId, (uint16_t)subIdx, (uint16_t)ni);
					}
				}

				region = region->next;
			}
		}
	}
}

/*
 * 0x004B26C9 - ResolveRegionName
 *
 * Interns name in g_RegionNames[] and returns its index. If the table
 * is full (>= 0x800), returns the last index.
 */
static int
ResolveRegionName(const char *name)
{
	int i;
	int len;
	char *copy;

	for (i = 0; i < g_RegionNameCount; i++) {
		if (strcasecmp(name, g_RegionNames[i]) == 0)
			return i;
	}

	if (g_RegionNameCount >= 0x800)
		return g_RegionNameCount - 1;

	len = strlen(name) + 1;
	copy = (char *)malloc(len);
	g_RegionNames[g_RegionNameCount] = copy;
	strcpy(copy, name);
	g_RegionNameCount++;

	return g_RegionNameCount - 1;
}

/*
 * 0x004B277D - RegisterTemplateInRegion
 *
 * Appends templateId to g_RegionTemplateIds[regionIndex], growing the
 * array by 16 slots when full.
 */
static void
RegisterTemplateInRegion(uint16_t templateId, int regionIndex)
{
	int count;
	uint16_t *newBuf;

	count = g_RegionTemplateCount[regionIndex];

	if (count >= g_RegionTemplateCapacity[regionIndex]) {
		g_RegionTemplateCapacity[regionIndex] += 0x10;
		newBuf = (uint16_t *)malloc(g_RegionTemplateCapacity[regionIndex] * 2);

		if (g_RegionTemplateIds[regionIndex] != NULL) {
			memcpy(newBuf, g_RegionTemplateIds[regionIndex], (g_RegionTemplateCapacity[regionIndex] - 0x10) * 2);
			free(g_RegionTemplateIds[regionIndex]);
		}
		g_RegionTemplateIds[regionIndex] = newBuf;
	}

	g_RegionTemplateIds[regionIndex][count] = templateId;
	g_RegionTemplateCount[regionIndex]++;
}

/*
 * 0x004B2873 - ClassifyTemplateBody
 *
 * Parses up to 8 body-type ids from the template's friendsList and
 * stores them (as count + 8 uint16 slots) in
 * g_TemplateBodyTypes[templateId].
 */
static void
ClassifyTemplateBody(int templateId)
{
	NPCTemplate *tmpl;
	int count;
	int ids[8];
	uint8_t *bodyList;
	int j;
	char buf[256];
	int bodyId;
	CResListNode *iter;
	CResListNode *tempNode;
	CString *str;
	const char *strBuf;

	count = 0;

	CResBankMagicCtx_DefaultConstructor(&iter);

	tmpl = CResManager_GetTemplateByID((uint16_t)templateId);

	CStringList_BeginIter(&tmpl->friendsList, &tempNode);
	CResBankMagicCtx_CopyConstructor(&iter, &tempNode);
	CResBankMagicCtx_PostInit(&tempNode);

	while (CStringList_HasEntries((CStringList *)&iter)) {
		if (count == 8)
			break;

		str = (CString *)CStringList_GetNodeData(&tmpl->friendsList, &iter);
		strBuf = CString_GetBuffer(str);
		strcpy(buf, strBuf);

		CStringList_AdvanceIter(&tmpl->friendsList, &tempNode, &iter);
		CResBankMagicCtx_CopyConstructor(&iter, &tempNode);
		CResBankMagicCtx_PostInit(&tempNode);

		bodyId = 0;
		sscanf(buf, "%d", &bodyId);

		if (bodyId > 0 && bodyId < 0x1000) {
			ids[count] = bodyId;
			count++;
		}
	}

	if (count > 0) {
		bodyList = (uint8_t *)malloc(0x12);
		bodyList[0] = (uint8_t)count;

		for (j = 0; j < count; j++) {
			*(uint16_t *)(bodyList + 2 + j * 2) = (uint16_t)ids[j];
		}

		g_TemplateBodyTypes[templateId] = bodyList;
	}

	CResBankMagicCtx_PostInit(&iter);
}

/*
 * 0x004B2A6A - InitResourceTypeIds
 *
 * Scans CResourceTypeManager by name and caches the well-known
 * resource-type IDs (meat, water, gold, etc.) in the g_ResTypeId_*
 * globals. "bone" and "weapon" are checked unconditionally, not as
 * else-if branches of the primary chain.
 *
 * FIXED: binary uses strcmp(name, "CARNIVOREMEAT") but restypes.mul
 * contains lowercase "carnivoremeat", so g_ResTypeId_CarnivoreMeat
 * stayed 0. Use strcasecmp for that one entry.
 */
void
InitResourceTypeIds(void)
{
	int i;

	for (i = 1; i < g_ResourceTypeCount; i++) {
		CResourceType *rt = CResourceTypeManager_GetId(i);
		const char *name;
		if (rt == NULL)
			continue;
		name = rt->internalName;

		if (strcmp(name, "meat") == 0)
			g_ResTypeId_Meat = i;
		else if (strcmp(name, "water") == 0)
			g_ResTypeId_Water = i;
		else if (strcmp(name, "self") == 0)
			g_ResTypeId_Self = i;
		else if (strcmp(name, "humans") == 0)
			g_ResTypeId_Humans = i;
		else if (strcmp(name, "feathers") == 0)
			g_ResTypeId_Feathers = i;
		else if (strcmp(name, "hide") == 0)
			g_ResTypeId_Hide = i;
		else if (strcmp(name, "wool") == 0)
			g_ResTypeId_Wool = i;
		else if (strcmp(name, "gold") == 0)
			g_ResTypeId_Gold = i;
		else if (strcmp(name, "magic") == 0)
			g_ResTypeId_Magic = i;
		else if (strcmp(name, "darkness") == 0)
			g_ResTypeId_Darkness = i;
		else if (strcmp(name, "metal") == 0)
			g_ResTypeId_Metal = i;
		else if (strcmp(name, "cloth") == 0)
			g_ResTypeId_Cloth = i;
		else if (strcmp(name, "leather") == 0)
			g_ResTypeId_Leather = i;
		else if (strcmp(name, "wood") == 0)
			g_ResTypeId_Wood = i;
		else if (strcmp(name, "good") == 0)
			g_ResTypeId_Good = i;
		else if (strcmp(name, "evil") == 0)
			g_ResTypeId_Evil = i;
		else if (strcmp(name, "jewels") == 0)
			g_ResTypeId_Jewels = i;
		else if (strcmp(name, "danger") == 0)
			g_ResTypeId_Danger = i;
		else if (strcasecmp(name, "CARNIVOREMEAT") == 0)
			g_ResTypeId_CarnivoreMeat = i;

		if (strcmp(name, "bone") == 0)
			g_ResTypeId_Bone = i;
		if (strcmp(name, "weapon") == 0)
			g_ResTypeId_Weapon = i;
	}
}

/*
 * 0x004B2DDE - InitTemplateClassification
 *
 * Populates g_TemplateFlags, g_TemplateBodyTypes, and the per-region
 * template tables for every registered template, then finalizes the
 * classification (FinalizeClassification, SetRegionWanderFlags).
 */
void
InitTemplateClassification(void)
{
	int i;
	NPCTemplate *tmpl;
	CResListNode *iterCtx;
	CResListNode *beginOut;
	CResListNode *advanceOut;
	char nameBuf[256];
	int regionIndex;

	g_RegionNameCount = 0;

	for (i = 0; i < 0x800; i++) {
		g_RegionNames[i] = NULL;
		g_RegionTemplateIds[i] = NULL;
		g_RegionTemplateCapacity[i] = 0;
		g_RegionTemplateCount[i] = 0;
	}

	if (g_ResBankManager.first == NULL)
		return;

	for (i = 0; i < 0x1000; i++) {
		g_TemplateFlags[i] = 0;
		g_TemplateBodyTypes[i] = NULL;

		if (!CResManager_HasByInt(Spawn_GetTemplatesRM(), (uint32_t)i))
			continue;

		g_TemplateFlags[i] = (int8_t)ClassifyTemplateItems(i);

		ClassifyTemplateBody(i);

		CResBankMagicCtx_DefaultConstructor(&iterCtx);

		tmpl = CResManager_GetTemplateByID((uint16_t)i);

		CStringList_BeginIter(&tmpl->regionList, &beginOut);

		CResBankMagicCtx_CopyConstructor(&iterCtx, &beginOut);

		CResBankMagicCtx_PostInit(&beginOut);

		while (CStringList_HasEntries((CStringList *)&iterCtx)) {
			void *nodeData = CStringList_GetNodeData(&tmpl->regionList, (CResListNode **)&iterCtx);
			char *str = CString_GetBuffer((CString *)nodeData);

			strcpy(nameBuf, str);

			CStringList_AdvanceIter(&tmpl->regionList, &advanceOut, (CResListNode **)&iterCtx);

			CResBankMagicCtx_CopyConstructor(&iterCtx, &advanceOut);

			CResBankMagicCtx_PostInit(&advanceOut);

			regionIndex = ResolveRegionName(nameBuf);

			RegisterTemplateInRegion(i, regionIndex);
		}

		CResBankMagicCtx_PostInit(&iterCtx);
	}

	FinalizeClassification();

	CResBankManager_SetRegionWanderFlags();
}

/*
 * 0x004B3004 - CResBankManager::SetRegionWanderFlags
 *
 * Builds the classification table for each region and assigns the
 * noWander flag from the region name prefix ("city"=1,
 * "dungeon"/"dungn"=2, else 0). Extracted from the tail of
 * InitTemplateClassification in the binary.
 */
void
CResBankManager_SetRegionWanderFlags(void)
{
	CResBankRegion *region;

	region = g_ResBankManager.first;
	while (region != NULL) {
		CClassification_BuildTable(region);

		region->noWander = 0;

		if (strncasecmp(region->name, "city", 4) == 0) {
			region->noWander = 1;
		} else if (strncasecmp(region->name, "dungeon", 7) == 0 || strncasecmp(region->name, "dungn", 5) == 0) {
			region->noWander = 2;
		}

		region = region->next;
	}
}

/*
 * 0x004B30B3 - RefreshResourceRegions
 *
 * Releases per-region name and template tables, cleans up every
 * CResBankRegion, and rebuilds the classification from scratch.
 */
void
RefreshResourceRegions(void)
{
	int i;
	CResBankRegion *region;

	for (i = 0; i < 0x800; i++) {
		if (g_RegionNames[i] != NULL)
			free(g_RegionNames[i]);
		if (g_RegionTemplateIds[i] != NULL)
			free(g_RegionTemplateIds[i]);
	}

	region = g_ResBankManager.first;
	while (region != NULL) {
		CResBankRegion_Cleanup(region);
		region = region->next;
	}

	InitTemplateClassification();
}

/*
 * 0x004B3150 - CResBankRegion::ScalarDelete
 *
 * Scalar deleting destructor.
 */
static __attribute__((unused)) void *
CResBankRegion_ScalarDelete(CResBankRegion *region, int flags)
{
	CResBankRegion_Destructor(region);
	if (flags & 1)
		free(region);
	return NULL;
}

/*
 * 0x004B31B0 - CStringList::FindByName
 *
 * Walks the list (starting from inIter or from DirectionIterInit when
 * invalid) and writes an iterator pointing at the first node whose
 * string equals name, or an invalidated iterator if no match.
 */
static CResListNode **
CStringList_FindByName(CResList *this, CResListNode **outIter, CString *name, CResListNode *inIterNode, int direction)
{
	CResListNode *inIter;
	CResListNode *tmpNode;
	CResListNode *advNode;
	void *retPtr;

	inIter = inIterNode;

	if (!CStringList_HasEntries((CStringList *)&inIter)) {
		retPtr = CResList_DirectionIterInit(this, (void *)&tmpNode, direction);
		CResBankMagicCtx_CopyConstructor(&inIter, (CResListNode **)retPtr);
		CResBankMagicCtx_PostInit(&tmpNode);
	}

	while (CStringList_HasEntries((CStringList *)&inIter)) {
		CString *nodeStr;

		nodeStr = (CString *)CStringList_GetNodeData((CStringList *)this, &inIter);
		if (CString_EqualCString2(nodeStr, name)) {
			CResBankMagicCtx_Copy(outIter, &inIter);
			CResBankMagicCtx_PostInit(&inIter);
			return outIter;
		}
		retPtr = CStringList_DirectionAdvanceIter(this, &advNode, &inIter, direction);
		CResBankMagicCtx_CopyConstructor(&inIter, (CResListNode **)retPtr);
		CResBankMagicCtx_PostInit(&advNode);
	}

	CStringList_Invalidate((CStringList *)&inIter);
	CResBankMagicCtx_Copy(outIter, &inIter);
	CResBankMagicCtx_PostInit(&inIter);
	return outIter;
}

/*
 * 0x004B91B2 - ProcessDynamicItemForSpawn
 *
 * Synchronizes a dynamic-loaded item's resource nodes against its
 * template: recurses into containers, compares per-id accumulated
 * value3 between item and template, and on mismatch strips all nodes
 * and copies the template's type==3 nodes back in.
 */
static void
ProcessDynamicItemForSpawn(CItem *item)
{
	int i;
	int match;
	ResEntitySlot *tmplSlot;
	CResourceNode *node;

	int itemCounts[MAX_RESOURCE_TYPES];
	int tmplCounts[MAX_RESOURCE_TYPES];

	for (i = 0; g_HarvestableBodyTypes[i] != 0; i++) {
		if ((CEntity_GetBodyType(item) & 0xFFFF) == (uint32_t)g_HarvestableBodyTypes[i]) {
			CItem_ClearTagList(item);
			break;
		}
	}

	if (VT_IsMobile2(item)) {
		CItem *child = ((CContainer *)item)->contents;
		while (child != NULL) {
			ProcessDynamicItemForSpawn(child);
			child = child->spatialNext;
		}
	}

	tmplSlot = &g_ResEntitySlots[CEntity_GetBodyType(item) & 0xFFFF];

	if (item->resourceEntity.firstChild == NULL && tmplSlot->nodeHead == NULL)
		return;

	memset(itemCounts, 0, sizeof(itemCounts));
	memset(tmplCounts, 0, sizeof(tmplCounts));

	for (node = item->resourceEntity.firstChild; node != NULL; node = node->next) {
		if (node->id == 0)
			continue;
		if (node->type != 3)
			continue;
		itemCounts[node->id] += node->value3;
	}

	for (node = tmplSlot->nodeHead; node != NULL; node = node->next) {
		if (node->id == 0)
			continue;
		if (node->type != 3)
			continue;
		tmplCounts[node->id] += node->value3;
	}

	match = 1;
	for (i = 0; i < MAX_RESOURCE_TYPES; i++) {
		if (itemCounts[i] != tmplCounts[i]) {
			match = 0;
			(void)CResourceTypeManager_GetId(i);
		}
	}

	if (match)
		return;

	CResourceEntity_RemoveAllNodes(item, 1);

	for (node = tmplSlot->nodeHead; node != NULL; node = node->next) {
		if (node->id == 0)
			continue;
		if (node->type != 3)
			continue;
		CResourceEntity_CopyNodeScaled(item, node, 0, 1, 1);
	}
}

/*
 * 0x004B947D - ProcessDynamicItems
 *
 * Pass 1: synchronizes every non-egg, non-NPC item via
 * ProcessDynamicItemForSpawn. Pass 2: migrates/re-creates items per
 * block via ProcessBlockItemsForSpawn. Finally consumes egg spawn
 * quotas via ResBankManager_ProcessDynamicEggEntities.
 */
void
ProcessDynamicItems(void)
{
	int i;
	CItem *ent;

	g_BuildingSpawnEntries = 1;

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		ent = g_MapBlocks[i].itemHead;
		while (ent != NULL) {
			if (((int (*)(void *))VT_FN(ent, VT_ITEM_CHECK_9C))(ent)) {
				ent = ent->spatialNext;
				continue;
			}

			if (VT_IsMobile(ent)) {
				ent = ent->spatialNext;
				continue;
			}

			ProcessDynamicItemForSpawn(ent);

			ent = ent->spatialNext;
		}
	}

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++)
		ProcessBlockItemsForSpawn(g_MapBlocks[i].itemHead);

	ResBankManager_ProcessDynamicEggEntities();

	g_BuildingSpawnEntries = 0;
}

/*
 * 0x004B955A - ProcessBlockItemsForSpawn
 *
 * Walks a block's item chain (skipping NPCs and eggs) and, depending
 * on tiledata flags and whether the item is a container, either
 * rebuilds the item fresh via CWorld::CreateNewItem (restoring
 * location/hue/layer/parent) or recurses into containers. Afterwards
 * it normalizes door bodyTypes (0xDFA/0xDFB/0x1020-0x1021 -> 0x1BD1
 * "closed door"; 0x1022-0x1025 -> 0x1BD4 "open door") and reseats
 * out-of-bounds items in their parent.
 */
static void
ProcessBlockItemsForSpawn(CItem *head)
{
	CItem *cur, *next;
	uint32_t tdFlags;
	int isContainer, isWeapon;

	cur = head;

	while (cur != NULL) {
		next = cur->spatialNext;

		if (VT_IsMobile(cur)) {
			cur = next;
			continue;
		}

		if (((int (*)(void *))VT_FN(cur, VT_ITEM_CHECK_9C))(cur)) {
			cur = next;
			continue;
		}

		tdFlags = g_ItemTileData[(uint16_t)CEntity_GetBodyType(cur)].flags;

		isContainer = VT_IsMobile2(cur);
		isWeapon = VT_IsWeapon(cur);

		if (((tdFlags & 0x200000) && !isContainer) || (!(tdFlags & 0x200000) && isWeapon)) {
			CLocation savedLoc;
			uint32_t savedBody;
			uint32_t savedHue;
			uint32_t savedLayer;
			uint32_t savedAmount;
			CItem *savedParent;

			savedLoc = cur->resourceEntity.entity.location;
			savedBody = (uint16_t)CEntity_GetBodyType(cur);
			savedHue = cur->resourceEntity.entity.color;
			savedLayer = CWorld_GetItemLayer(CEntity_GetBodyType(cur)) & 0xFF;
			savedAmount = CItem_GetTiledataQuantity(cur) & 0xFFFF;
			savedParent = cur->parent;

			((void (*)(void *))VT_FN(cur, VT_HIDE))(cur);
			if (cur != NULL)
				((void (*)(void *))VT_FN(cur, VT_DELETE))(cur);

			cur = CWorld_CreateItem(g_World, (uint16_t)savedBody);

			{
				CLocation *resLoc = (CLocation *)&cur->resourceEntity.nextInContainer;
				*resLoc = savedLoc;
			}

			cur->resourceEntity.entity.color = (uint16_t)savedHue;
			CItem_Setup(cur, 0, &savedLoc, 0, 1);

			if (savedParent != NULL) {
				((void (*)(void *, CItem *, CLocation *))VT_FN(cur, VT_ADD_TO_CONTAINER))(cur, savedParent, &savedLoc);
			} else {
				((void (*)(void *, CLocation *))VT_FN(cur, VT_DROP_AT_FEET))(cur, &savedLoc);
			}

			if (!ValidateInWorld(cur))
				cur = NULL;

			USED(savedLayer);
			USED(savedAmount);

		} else if (((tdFlags & 0x8000002) && !isWeapon) || (!(tdFlags & 0x8000002) && isContainer)) {
			CLocation savedLoc;
			uint32_t savedBody;
			uint32_t savedHue;
			uint32_t savedLayer;
			uint32_t savedAmount;
			CItem *savedParent;

			savedLoc = cur->resourceEntity.entity.location;
			savedBody = (uint16_t)CEntity_GetBodyType(cur);
			savedHue = cur->resourceEntity.entity.color;
			savedLayer = CWorld_GetItemLayer(CEntity_GetBodyType(cur)) & 0xFF;
			savedAmount = CItem_GetTiledataQuantity(cur) & 0xFFFF;
			savedParent = cur->parent;

			((void (*)(void *))VT_FN(cur, VT_HIDE))(cur);
			if (cur != NULL)
				((void (*)(void *))VT_FN(cur, VT_DELETE))(cur);

			cur = CWorld_CreateItem(g_World, (uint16_t)savedBody);

			{
				CLocation *resLoc = (CLocation *)&cur->resourceEntity.nextInContainer;
				*resLoc = savedLoc;
			}
			cur->resourceEntity.entity.color = (uint16_t)savedHue;
			CItem_Setup(cur, 0, &savedLoc, 0, 1);

			if (savedParent != NULL) {
				((void (*)(void *, CItem *, CLocation *))VT_FN(cur, VT_ADD_TO_CONTAINER))(cur, savedParent, &savedLoc);
			} else {
				((void (*)(void *, CLocation *))VT_FN(cur, VT_DROP_AT_FEET))(cur, &savedLoc);
			}

			if (!ValidateInWorld(cur))
				cur = NULL;

			USED(savedLayer);
			USED(savedAmount);

		} else if (isContainer) {
			CContainer *cont = (CContainer *)cur;
			ProcessBlockItemsForSpawn(cont->contents);
		}

		{
			uint32_t bt = (uint16_t)CEntity_GetBodyType(cur);

			if ((bt >= 0x0DFA && bt <= 0x0DFB) || (bt >= 0x1020 && bt <= 0x1021)) {
				((void (*)(void *))VT_FN(cur, VT_DETACH_SPATIAL))(cur);
				CEntity_SetBodyType(cur, 0x1BD1);
				((void (*)(void *))VT_FN(cur, VT_RETURN_TO_TRACKED))(cur);
			} else if (bt >= 0x1022 && bt <= 0x1025) {
				((void (*)(void *))VT_FN(cur, VT_DETACH_SPATIAL))(cur);
				CEntity_SetBodyType(cur, 0x1BD4);
				((void (*)(void *))VT_FN(cur, VT_RETURN_TO_TRACKED))(cur);
			}

			if (cur->parent != NULL) {
				if (!VT_IsMobile(cur->parent)) {
					CLocation loc;
					int outOfBounds;
					int16_t itemX, itemY;
					int dimX, dimY;
					CItem *parentItem;
					int minX, maxX, minY, maxY;

					CLocation_Init(&loc);
					outOfBounds = 0;

					itemX = cur->resourceEntity.entity.location.x;
					itemY = cur->resourceEntity.entity.location.y;

					((void (*)(void *, int *, int *))VT_FN(cur, VT_GET_CONTAINER_DIM))(cur, &dimX, &dimY);

					parentItem = cur->parent;

					CContainer_GetContainerBounds(parentItem, &minX, &maxX, &minY, &maxY);

					if ((int)((uint16_t)itemX) < minX)
						outOfBounds = 1;

					if ((int)((uint16_t)itemY) < minY)
						outOfBounds = 1;

					if ((int)((uint16_t)itemX) + (int)((uint16_t)dimX) > maxX)
						outOfBounds = 1;

					if ((int)((uint16_t)itemY) + (int)((uint16_t)dimY) > maxY)
						outOfBounds = 1;

					if (outOfBounds) {
						CLocation_Set(&loc, -1, -1, 0);
						((void (*)(void *))VT_FN(cur, VT_HIDE))(cur);
						((void (*)(void *, CItem *, CLocation *))VT_FN(cur, VT_ADD_TO_CONTAINER))(cur, parentItem, &loc);
					}
				}
			}
		}

		cur = next;
	}
}

/*
 * 0x004B9AE0 - ParseAlignment
 *
 * Parses an alignment word ("GOOD", "NEUTRAL", "EVIL", "CHAOTIC")
 * into its id (1/0/2/3). Defaults to 0.
 */
int
ParseAlignment(char *text)
{
	char buf[128];

	text = GetValue(text, buf);
	USED(text);
	if (strcasecmp(buf, "GOOD") == 0)
		return 1;
	if (strcasecmp(buf, "NEUTRAL") == 0)
		return 0;
	if (strcasecmp(buf, "EVIL") == 0)
		return 2;
	if (strcasecmp(buf, "CHAOTIC") == 0)
		return 3;
	return 0;
}

/*
 * 0x004BAAF1 - CTemplateManager::SpawnVendorStock
 *
 * Processes one equipment entry from template data: parses body type,
 * color, amount and qualifier, creates the entity (item, NPC sub-template
 * or multi) and places it per qualifier (wear, selfcontained, contained,
 * sellable, buyable, invent, at, in). Returns 1 on success, 0 at end.
 *
 * MODIFIED: 0x186A0-range companion NPCs get itemFlags |= 0x08 so they
 * are transient (not saved to dynamic0.mul), preventing NPC accumulation
 * across server restarts.
 *
 * FIXED: the AT qualifier block falls through to script name parsing
 * like every other qualifier. The binary at 0x004BB837 jumps to
 * 0x004BB89B (isSelfContained check), not to after_placement; 308
 * AT-qualified entities (mostly companion NPCs) were losing their
 * poi_cleanup scripts and staying permanent.
 */
int
CTemplateManager_SpawnVendorStock(CItem *mob, char *dataCursor, int *counter, CLocation loc, void *manager, int *colorPtr, uint32_t limit, int depth)
{
	USED(manager);
	uint32_t bodyType;
	char *savedCursor;
	CItem *newEntity;
	CItem *targetContainer;
	int amount;
	int isMagic;
	int isWear, isSelfContained, isContained;
	int isSellable, isInvent, isBuyable;
	int isAt, isIn;
	CString valueStr;
	CString labelStr;
	char valueBuf[128];
	char scriptName[40];
	char sellScriptName[40];
	CMagicItemList magicList;
	int colorOut;
	int magicChance, magicMin, magicMax;

	bodyType = 0;
	savedCursor = 0;
	CMagicItemList_Constructor(&magicList);
	isMagic = 0;
	isWear = 0;
	isContained = 0;
	isSelfContained = 0;
	isSellable = 0;
	isInvent = 0;
	isBuyable = 0;
	isAt = 0;
	isIn = 0;
	targetContainer = NULL;
	CString_DefaultConstructor(&valueStr);
	CString_DefaultConstructor(&labelStr);

	dataCursor = CTemplateManager_GetNextEqValue(dataCursor, &valueStr);

	// "0" terminates the equipment list.
	if (CString_CompareStr(&valueStr, "0")) {
		CString_Destructor(&labelStr);
		CString_Destructor(&valueStr);
		CMagicItemList_Destructor(&magicList);
		return 0;
	}

	savedCursor = dataCursor;

	dataCursor = GetValue(dataCursor, valueBuf);

	if (strcasecmp(valueBuf, "magic") == 0) {
		dataCursor = GetValue(dataCursor, valueBuf);
		magicChance = atoi(valueBuf);
		dataCursor = GetValue(dataCursor, valueBuf);
		magicMin = atoi(valueBuf);
		dataCursor = GetValue(dataCursor, valueBuf);
		magicMax = atoi(valueBuf);

		if (magicChance < GetRandomRange(0, 99)) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}

		{
			CStringList searchCtx;
			CResList searchOutput;

			CStringList_Init(&searchCtx);

			CStringList_AddWeighted(&searchCtx, (CResListNode **)&searchOutput, &valueStr, 1, 1);
			CResBankMagicCtx_PostInit((CResListNode **)&searchOutput);

			if (!CMagicItemFactory_Create(&magicList, magicMin, magicMax, &searchCtx, 2, 5)) {
				CStringList_Destroy(&searchCtx);
				CString_Destructor(&labelStr);
				CString_Destructor(&valueStr);
				CMagicItemList_Destructor(&magicList);
				return 0;
			}

			newEntity = CMagicItemList_GetResult(&magicList);
			if (newEntity == NULL) {
				CStringList_Destroy(&searchCtx);
				CString_Destructor(&labelStr);
				CString_Destructor(&valueStr);
				CMagicItemList_Destructor(&magicList);
				return 0;
			}

			isMagic = 1;
			CStringList_Destroy(&searchCtx);
		}

		savedCursor = dataCursor;
		goto parse_color;
	}

	bodyType = (uint32_t)atoi(CString_GetBuffer(&valueStr));

	if (bodyType >= 0x186A0 && bodyType < 0x30D40) {
		uint32_t ownerSerial;

		if (mob != NULL)
			ownerSerial = mob->serial;
		else
			ownerSerial = 0;

		newEntity = CTemplateManager_CreateFromTemplate((uint16_t)(bodyType - 0x186A0), &loc, 1, 0, NULL);

		if (!CTemplateManager_ValidateOwner(ownerSerial, mob)) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}
		if (newEntity == NULL) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}

		// MODIFIED: mark companion NPC as transient so it is
		// not saved to dynamic0.mul (prevents accumulation
		// of ITEM group NPCs across server restarts)
		newEntity->itemFlags |= 0x08;

		if (newEntity->resourceEntity.entity.removedFromWorld != 1) {
			((void (*)(void *))VT_FN(newEntity, VT_HIDE))(newEntity);
		}
		goto parse_color;
	}

	if (bodyType >= 0x30D40 && bodyType < 0x493E0) {
		uint32_t ownerSerial;

		if (CMultiManager_CanExistAt(&g_MultiManager, (int)(bodyType - 0x30D40), &loc, 1, 0) <= 0) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}

		if (mob != NULL)
			ownerSerial = mob->serial;
		else
			ownerSerial = 0;

		newEntity = CMultiManager_Create(&g_MultiManager, (int)(bodyType - 0x30D40), &loc, 1, NULL);

		if (!CTemplateManager_ValidateOwner(ownerSerial, mob)) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}
		if (newEntity == NULL) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}
		goto parse_color;
	}

	if (bodyType >= 0x493E0 && bodyType < 0x61A80) {
		uint32_t ownerSerial;

		if (mob != NULL)
			ownerSerial = mob->serial;
		else
			ownerSerial = 0;

		newEntity = CTemplateManager_CreateFromTemplate((uint16_t)(bodyType - 0x493E0), &loc, 1, 0, mob);

		if (!CTemplateManager_ValidateOwner(ownerSerial, mob)) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}

		if (newEntity == NULL) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}

		if (newEntity == mob) {
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 1;
		}

		((void (*)(void *))VT_FN(newEntity, VT_DELETE))(newEntity);
		CString_Destructor(&labelStr);
		CString_Destructor(&valueStr);
		CMagicItemList_Destructor(&magicList);
		return 0;
	}

	if (bodyType == 0 || !CWorld_LookupItemResource((uint16_t)bodyType)) {
		CString_Destructor(&labelStr);
		CString_Destructor(&valueStr);
		CMagicItemList_Destructor(&magicList);
		return 0;
	}

	newEntity = CWorld_CreateItem(g_World, (uint16_t)bodyType);
	if (newEntity == NULL) {
		CString_Destructor(&labelStr);
		CString_Destructor(&valueStr);
		CMagicItemList_Destructor(&magicList);
		return 0;
	}

parse_color:
	dataCursor = CTemplateManager_ParseColor(savedCursor, &colorOut, colorPtr);

	if (colorOut != -1)
		newEntity->resourceEntity.entity.color = (uint16_t)colorOut;

	if (*colorPtr == -1) {
		uint16_t bt = newEntity->resourceEntity.entity.bodyType;
		if (CTemplateManager_GetTemplateColorCount(bt) == 1)
			*colorPtr = colorOut;
	}

	amount = 1;
	dataCursor = GetValue(dataCursor, valueBuf);
	if (strcasecmp(valueBuf, "-1") != 0) {
		amount = CRandom_ParseAndRoll(valueBuf);
	}
	if (amount <= 0)
		amount = 1;

	scriptName[0] = '\0';
	dataCursor = GetValue(dataCursor, valueBuf);

	if (strcasecmp(valueBuf, "wear") == 0) {
		isWear = 1;
	} else if (strcasecmp(valueBuf, "selfcontained") == 0) {
		isSelfContained = 1;
	} else if (strcasecmp(valueBuf, "contained") == 0) {
		isContained = 1;
	} else if (strcasecmp(valueBuf, "sellable") == 0) {
		isSellable = 1;
	} else if (strcasecmp(valueBuf, "buyable") == 0) {
		isBuyable = 1;
	} else if (strcasecmp(valueBuf, "invent") == 0) {
		isInvent = 1;
	} else if (strcasecmp(valueBuf, "at") == 0) {
		isAt = 1;
	} else if (strcasecmp(valueBuf, "in") == 0) {
		isIn = 1;
	} else {
		isWear = 1;
	}

	if (isIn == 1) {
		CString inNameStr;
		CSearchCtx inSearchCtx;
		CItem *foundContainer;
		void *ptr;
		CSearchCtx inOutput;

		CString_DefaultConstructor(&inNameStr);
		CSearchCtx_Constructor(&inSearchCtx);

		dataCursor = GetValue(dataCursor, valueBuf);
		CString_AssignInternal(&inNameStr, valueBuf);

		CResManager_FindContainer(&g_TemplatesRM, &inOutput, &inNameStr, 1);
		CSearchCtx_Add(&inSearchCtx, &inOutput);

		if (CSearchCtx_Find(&inSearchCtx)) {
			ptr = CResManager_GetResult(&g_TemplatesRM, &inSearchCtx);
			if (ptr != NULL) {
				foundContainer = *(CItem **)ptr;
				if (foundContainer != NULL) {
					if (VT_IsMobile2(foundContainer)) {
						targetContainer = foundContainer;
					}
				}
			}
		}

		CString_Destructor(&inNameStr);
	}

	if (isAt == 1) {
		int atXOffset, atYOffset, atZOffset;
		CLocation atLoc;

		CLocation_Init(&atLoc);

		dataCursor = GetValue(dataCursor, valueBuf);
		atXOffset = atoi(valueBuf);
		dataCursor = GetValue(dataCursor, valueBuf);
		atYOffset = atoi(valueBuf);
		dataCursor = GetValue(dataCursor, valueBuf);
		atZOffset = atoi(valueBuf);

		CLocation_CopyFrom(&atLoc, &loc);
		atLoc.x += (int16_t)atXOffset;
		atLoc.y += (int16_t)atYOffset;
		atLoc.z += (int16_t)atZOffset;

		{
			int h = ((int (*)(void *))VT_FN(newEntity, VT_GET_HEIGHT))(newEntity);

			if (FindSpawnSpot(&atLoc, 0, 3, h, 0, newEntity)) {
				if (newEntity->resourceEntity.entity.removedFromWorld != 1) {
					((void (*)(void *))VT_FN(newEntity, VT_HIDE))(newEntity);
				}
				((void (*)(void *, CLocation *))VT_FN(newEntity, VT_DROP_AT_FEET))(newEntity, &atLoc);
			} else {
				if (newEntity != NULL) {
					((void (*)(void *))VT_FN(newEntity, VT_DELETE))(newEntity);
				}
				CString_Destructor(&labelStr);
				CString_Destructor(&valueStr);
				CMagicItemList_Destructor(&magicList);
				return 0;
			}
		}
	}

	if (isSelfContained == 1) {
		if (mob != NULL) {
			if (VT_IsMobile2(mob)) {
				targetContainer = mob;
			} else {
				isSelfContained = 0;
			}
		} else {
			isSelfContained = 0;
		}
	}

	if (isWear == 1) {
		if (mob == NULL)
			goto wear_failed;
		if (!VT_IsMobile(mob))
			goto wear_failed;

		{
			uint16_t bt = newEntity->resourceEntity.entity.bodyType;
			if (!CWorld_LookupItemResource(bt))
				goto wear_failed;
		}

		{
			uint16_t bt = newEntity->resourceEntity.entity.bodyType;
			if (!(g_ItemTileData[bt].flags & TF_EQUIPPABLE))
				goto wear_failed;
		}

		{
			uint8_t layer = CItem_GetLayerThiscall(newEntity);
			if (layer == 0)
				goto wear_no_fallback;
			if (layer >= 30)
				goto wear_no_fallback;
		}

		{
			uint8_t layer = CItem_GetLayerThiscall(newEntity);
			int equipResult;
			equipResult = ((int (*)(void *, void *, int))VT_FN(newEntity, VT_EQUIP_ON_MOBILE))(newEntity, mob, layer);
			if (equipResult == 1)
				goto wear_no_fallback;
		}

		isWear = 0;
		isContained = 1;
		goto wear_no_fallback;

wear_failed:
		isWear = 0;
		isContained = 1;

wear_no_fallback:;
	}

	if (isSellable == 1) {
		if (mob == NULL)
			goto sellable_failed;
		if (!VT_IsVendor(mob))
			goto sellable_failed;

		targetContainer = ((CMobile *)mob)->equipment[26];
		if (targetContainer == NULL)
			CTemplateManager_SpawnVendorStock_DebugBreak();
		if (((CContainer *)targetContainer)->lockOwner != NULL)
			CTemplateManager_SpawnVendorStock_DebugBreak();
		goto sellable_done;

sellable_failed:
		isSellable = 0;

sellable_done:;
	}

	if (isInvent == 1) {
		if (mob == NULL)
			goto invent_failed;
		if (!VT_IsVendor(mob))
			goto invent_failed;

		targetContainer = ((CMobile *)mob)->equipment[27];
		goto invent_done;

invent_failed:
		isInvent = 0;

invent_done:;
	}

	if (isBuyable == 1) {
		if (mob == NULL)
			goto buyable_failed;
		if (!VT_IsVendor(mob))
			goto buyable_failed;

		targetContainer = ((CMobile *)mob)->equipment[28];
		goto buyable_done;

buyable_failed:
		isBuyable = 0;

buyable_done:;
	}

	if (isContained == 1) {
		if (mob == NULL || !VT_IsMobile(mob))
			goto contained_done;

		{
			int i;
			for (i = 0; i < 0x1A; i++) {
				if (((CMobile *)mob)->equipment[i] == NULL)
					continue;
				{
					CItem *eq = ((CMobile *)mob)->equipment[i];
					if (((int (*)(void *))VT_FN(eq, VT_HAS_ACCESSIBLE_CONTENTS))(eq)) {
						targetContainer = eq;
					}
				}
			}
		}

contained_done:;
	}

	dataCursor = GetValue(dataCursor, valueBuf);
	if (strcasecmp(valueBuf, "label") == 0) {
		dataCursor = GetValue(dataCursor, valueBuf);
		CString_AssignInternal(&labelStr, valueBuf);
		dataCursor = GetValue(dataCursor, valueBuf);
	}

	if (isSellable != 0) {
		strcpy(sellScriptName, valueBuf);
		scriptName[0] = '\0';
	} else {
		strcpy(scriptName, valueBuf);
		sellScriptName[0] = '\0';
	}

	if (targetContainer != NULL) {
		CLocation contLoc;

		CLocation_Init(&contLoc);

		if (CItem_IsMultiOwner(newEntity)) {
			if (newEntity != NULL) {
				((void (*)(void *))VT_FN(newEntity, VT_DELETE))(newEntity);
			}
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}

		if (isSellable != 0) {
			if (sellScriptName[0] != '\0' && strcmp(sellScriptName, "") != 0) {
				CString shopStr;
				CString_Constructor(&shopStr, sellScriptName);
				CItem_SetShopScript(newEntity, &shopStr);
				CString_Destructor(&shopStr);
			}
		}

		if (targetContainer->resourceEntity.entity.bodyType == 0x2AF8) {
			int minX, x, minY, y;
			int contX, contY;

			CContainer_GetContainerBounds(targetContainer, &minX, &x, &minY, &y);

			contX = minX + *counter;
			*counter = *counter + 1;
			contY = y;

			CLocation_Set(&contLoc, (int16_t)contX, (int16_t)contY, 0);
		} else {
			CLocation_Set(&contLoc, -1, -1, 0);
		}

		((void (*)(void *, void *, CLocation *))VT_FN(newEntity, VT_ADD_TO_CONTAINER))(newEntity, targetContainer, &contLoc);
	} else if (isAt == 0 && isWear == 0) {
		if (newEntity != NULL) {
			((void (*)(void *))VT_FN(newEntity, VT_DELETE))(newEntity);
		}
		CString_Destructor(&labelStr);
		CString_Destructor(&valueStr);
		CMagicItemList_Destructor(&magicList);
		return 0;
	}

	// after_placement (binary convergence point 0x004BBDFD)
	if (!CString_IsEmpty(&labelStr)) {
		CTemplateManager_ApplyLabel(&labelStr, &newEntity);
	}

	if (bodyType < 0x186A0) {
		if (!ValidateInWorld(newEntity)) {
			newEntity = NULL;
			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return 0;
		}
	}

	if (strlen(scriptName) != 0) {
		Entity_AttachScript(newEntity, scriptName, 1);
	}

	if (isMagic == 1) {
		if (ResBankMagicCheck(&loc, g_ResTypeId_Magic)) {
			CResourceEntity_NotifyPreModify(newEntity);
			CMagicItemList_AddItem(&magicList, newEntity);
			CResourceEntity_NotifyPostModifyIfActive(newEntity);
		}
	}

	if ((bodyType >= 0x186A0 && bodyType < 0x30D40) || bodyType >= 0x493E0) {
		goto success;
	}

	if (limit == 0) {
		CTemplateManager_ApplyTemplate(0, newEntity, amount, &loc, depth, 0);
	} else {
		if (!ResBankLimitCheck(newEntity, &loc)) {
			int equipped;

			equipped = 0;
			if (VT_IsEquipped(newEntity))
				equipped = -1;

			if (newEntity != NULL) {
				((void (*)(void *))VT_FN(newEntity, VT_DELETE))(newEntity);
			}

			CString_Destructor(&labelStr);
			CString_Destructor(&valueStr);
			CMagicItemList_Destructor(&magicList);
			return equipped;
		}

		CTemplateManager_ApplyTemplate(1, newEntity, amount, &loc, depth, 0);
	}

success:
	CString_Destructor(&labelStr);
	CString_Destructor(&valueStr);
	CMagicItemList_Destructor(&magicList);
	return 1;
}

/*
 * 0x004C0580 - CResBankMagicCtx::CResBankMagicCtx (default constructor)
 *
 * Zero-initializes the single-pointer context.
 */
void *
CResBankMagicCtx_DefaultConstructor(CResListNode **obj)
{
	*obj = NULL;
	return obj;
}

/*
 * 0x004C05A0 - CResBankMagicCtx::CResBankMagicCtx (copy constructor)
 *
 * Copies *src into *obj.
 */
void *
CResBankMagicCtx_CopyConstructor(CResListNode **obj, CResListNode **src)
{
	*obj = *src;
	return obj;
}

/*
 * Helper - Spawn_GetTemplatesRM
 *
 * Returns the CResManager holding all loaded templates.
 */
CResManager *
Spawn_GetTemplatesRM(void)
{
	return &g_TemplatesRM;
}

/*
 * Helper - DeductSpawnFromBank
 *
 * Walks templateId's type-3 production nodes and subtracts each value1
 * from the bank at loc via SubtractFromQuantity + AddToSpawnedCount.
 * Called from every NPC spawn path under FEAT_CLOSED_ECONOMY:
 *   - CResBankRegion_SpawnAtPoint / SpawnAtPointInBox (binary director spawns)
 *   - GM_TargetSpawnNPC (the GM .spawn command)
 *   - Script_createGlobalNPCAtSpecificLoc (Wombat builtin 684)
 *   - Script_createGlobalNPCAt (Wombat builtin 685)
 * Without this helper the GM and script paths would create NPCs
 * whose nodes are never accounted for, letting an operator or a
 * Wombat <spawn> call silently undo the bank's spawn-side gate.
 */
void
DeductSpawnFromBank(uint16_t templateId, CLocation *loc)
{
	CResBankRegion *region;
	NPCTemplate *tmpl;
	CResourceNode *nd;

	if (!feat(FEAT_CLOSED_ECONOMY))
		return;
	if (loc == NULL)
		return;
	region = CResBankManager_GetRegionByLocation(loc->x, loc->y);
	if (region == NULL || region == g_ResBankManager.noRegion)
		return;
	tmpl = CResManager_GetTemplateByID(templateId);
	if (tmpl == NULL)
		return;
	for (nd = tmpl->resourceNodes; nd != NULL; nd = nd->next) {
		int32_t avail;
		int32_t take;

		if (nd->type != 3 || nd->id == 0)
			continue;

		// CUSTOM (FEAT_CLOSED_ECONOMY): a resource bank cannot drop below
		// empty. CanSpawnTemplate floor-gates the director spawn, but it is a
		// pre-check - a single debit can overshoot a near-empty resource past
		// zero - and the per-NPC respawn queue (PendingNPCRespawn_Tick ->
		// SpawnAtPointForLocation) spawns with g_SpawningInProgress clear, so
		// its CanSpawnTemplate check uses the looser >0 threshold instead of
		// the maxSpawns>>4 floor. Either path can drive a thin resource (meat,
		// carnivoremeat) negative, and a negative balance makes
		// ResBankLimitCheck reject meat-vendor stock, which culls the stockless
		// vendor (CTemplateManager_CreateFromTemplate). Clamp the debit at zero
		// so the closed economy starves the spawn, not the shopkeeper.
		avail = CResBankRegion_GetQuantity(region, nd->id);
		take = nd->value1;
		if (take > avail)
			take = (avail > 0) ? avail : 0;

		CResBankRegion_SubtractFromQuantity(region, nd->id, take);
		CResBankRegion_AddToSpawnedCount(region, nd->id, nd->value1);
	}
}

/*
 * Helper - RefundResourceNodesToBank
 *
 * For each non-empty type-3 production node on item, credits the regional bank
 * at item->location directly via CResBankRegion_AddToQuantity. Called from
 * destruction sites (CItem_DecayTick path) under FEAT_CLOSED_ECONOMY, mirroring
 * the harvest-side credit in Script_returnResourcesToBank: a decayed corpse
 * returns its resources synchronously. The credit lands in quantities[], which
 * SaveResBank persists, so nothing is left in flight to drop on restart. The
 * value3<=0 guard skips nodes already drained by harvest scripts
 * (Script_returnResourcesToBank) so the bank is not double-credited.
 */
void
RefundResourceNodesToBank(CItem *item)
{
	CResourceNode *nd;
	CLocation *loc;
	CResBankRegion *region;

	if (item == NULL)
		return;

	loc = &item->resourceEntity.entity.location;
	region = CResBankManager_GetRegionByLocation(loc->x, loc->y);
	if (region == NULL || region == g_ResBankManager.noRegion)
		return;

	for (nd = item->resourceEntity.firstChild; nd != NULL; nd = nd->next) {
		if (nd->type != 3 || nd->id == 0 || nd->value3 <= 0)
			continue;
		CResBankRegion_AddToQuantity(region, nd->id, nd->value3);
	}
}
