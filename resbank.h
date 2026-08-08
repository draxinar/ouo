#ifndef RESBANK_H_
#define RESBANK_H_

#include <stdint.h>

/*
 * NPC template system. g_TemplatesRM (0x006933F8) parses namestable.dat,
 * defines, and templatestable.dat into the template table that drives
 * the RESBANK spawn path in egg.c.
 */

#include "cstring.h"

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMagicItemFactory CMagicItemFactory;
__extension__ typedef struct CNPC CNPC;
__extension__ typedef struct CResBankManager CResBankManager;
__extension__ typedef struct CResBankRegion CResBankRegion;
__extension__ typedef struct CResList CResList;
__extension__ typedef struct CResManager CResManager;
__extension__ typedef struct ResSpawnEntry ResSpawnEntry;
struct CNPC;
__extension__ typedef struct TemplateDice TemplateDice;
__extension__ typedef struct TemplateEquip TemplateEquip;
__extension__ typedef struct TemplateSkill TemplateSkill;
__extension__ typedef struct NPCTemplate NPCTemplate;
__extension__ typedef struct CNameEntry CNameEntry;

/*
 * CStringList payload node, wrapped in a CResListNode (0x18 bytes, ctor
 * 0x0043F2F0).
 */
__extension__ typedef struct CStringListNode CStringListNode;
struct CStringListNode {
	CString str;      // +0x00
	int weight;       // +0x10
	int type;         // +0x14 (1 = valid/enabled)
};

struct CResourceNode;

/*
 * Weighted string list (0x10 bytes, ctor 0x0043D630). Used for the
 * region/regionlimit/friends fields of a template.
 */
__extension__ typedef struct CStringList CStringList;
struct CStringList {
	CResList list;    // +0x00
	int totalWeight;  // +0x0C
};

/*
 * Dice roll wide enough for template values like "1d250+550".
 */
struct TemplateDice {
	int16_t num;
	int16_t faces;
	int16_t bonus;
};

/*
 * Placement qualifier for <eq> entries, parsed from SELFCONTAINED /
 * SELLABLE / BUYABLE / CONTAINED / INVENT tokens at 0x004BB4C3.
 */
#define EQ_QUAL_WEAR          0 // worn equipment (default)
#define EQ_QUAL_SELFCONTAINED 1 // carried loot
#define EQ_QUAL_SELLABLE      2 // vendor stock container (equipment[26])
#define EQ_QUAL_BUYABLE       3 // vendor buy-list container (equipment[28])
#define EQ_QUAL_CONTAINED     5 // first container in equipment[0..25]
#define EQ_QUAL_INVENT        6 // vendor offered container (equipment[27])

/*
 * Parsed <eq> line (template field 0x05). bodyTypes[] are resolved from
 * defines with a weighted pick at spawn time; values >= 300000 name a
 * sub-template (id = value - 300000).
 */
struct TemplateEquip {
	uint32_t bodyTypes[8];
	uint8_t bodyTypeCount;
	uint16_t colorMin, colorMax;
	uint8_t count;
	uint8_t qualifier;
};

/*
 * Parsed <sk> line (template field 0x13).
 */
struct TemplateSkill {
	uint8_t skillId;
	TemplateDice dice;
};

__extension__ typedef struct CItemEffectDef CItemEffectDef;
__extension__ typedef struct CEffectTableEntry CEffectTableEntry;
__extension__ typedef struct CNamedProperty CNamedProperty;

/*
 * Two-string entry for a CStringPairList (0x28 bytes).
 */
__extension__ typedef struct CStringPairListNode {
	CString first;    // +0x00
	CString second;   // +0x10
	int weight;       // +0x20
	int type;         // +0x24 (1 = valid/enabled)
} CStringPairListNode;

/*
 * Weighted string-pair list (0x10 bytes).
 */
__extension__ typedef struct CStringPairList {
	CResList list;    // +0x00
	int totalWeight;  // +0x0C
} CStringPairList;

/*
 * Name-pool bucket (0x24 bytes, ctor 0x004C06B0): three CResLists
 * (male/female/other) of heap-allocated CString payloads.
 */
struct CNameEntry {
	CResList lists[3]; // +0x00 male, +0x0C female, +0x18 other
};

/*
 * CTemplate field type IDs assigned by LoadTemplates (0x004BE297).
 * CreateEntity dispatches on (fieldType - 1) through the case table at
 * 0x004BE254 / jump table at 0x004BE1D8. Note: "frequency", first
 * "movetype", and "alignment" all collapse to type 40 (0x28), which the
 * case table maps to the default (skip) arm. "sk" and "skill" both map
 * to type 19 - there is no type 20.
 */
enum {
	TMPL_FIELD_ATTITUDE = 1,
	TMPL_FIELD_CONVFRAG = 3,
	TMPL_FIELD_DEXTERITY = 4,
	TMPL_FIELD_EQ = 5,
	TMPL_FIELD_HP = 6,
	TMPL_FIELD_HUE = 7,
	TMPL_FIELD_INTELLIGENCE = 8,
	TMPL_FIELD_MANA = 9,
	TMPL_FIELD_NOTORIETY = 10,
	TMPL_FIELD_FAME = 11,
	TMPL_FIELD_KARMA = 12,
	TMPL_FIELD_NAME = 13,
	TMPL_FIELD_RESOURCE = 14,
	TMPL_FIELD_STRENGTH = 15,
	TMPL_FIELD_SEX = 16,
	TMPL_FIELD_STAMINA = 17,
	TMPL_FIELD_SCRIPT = 18,
	TMPL_FIELD_SKILL = 19,
	TMPL_FIELD_REGION = 21,
	TMPL_FIELD_SFXNOTICE = 22,
	TMPL_FIELD_SFXIDLE = 23,
	TMPL_FIELD_SFXHIT = 24,
	TMPL_FIELD_SFXWASHIT = 25,
	TMPL_FIELD_SFXDIE = 26,
	TMPL_FIELD_MULTI = 27,
	TMPL_FIELD_OBJVAR = 28,
	TMPL_FIELD_QUALITY = 29,
	TMPL_FIELD_QUANTITY = 30,
	TMPL_FIELD_HEIGHT = 31,
	TMPL_FIELD_WEIGHT = 32,
	TMPL_FIELD_NATURALAC = 34,
	TMPL_FIELD_NATURALWC = 35,
	TMPL_FIELD_RESISTANCES = 36,
	TMPL_FIELD_IMMUNITIES = 37,
	TMPL_FIELD_JOB = 38,
	TMPL_FIELD_REGIONLIMIT = 39,
	TMPL_FIELD_FREQUENCY = 40,
	TMPL_FIELD_FRIENDS = 41,
	TMPL_FIELD_MOVETYPE = 42, // dead code: second "movetype" match, unreachable
	TMPL_FIELD_PARTIALHUE = 43,
	TMPL_FIELD_CORPSENAME = 44,
	TMPL_FIELD_FLEEVAL = 45,
	TMPL_FIELD_CREATESNPCS = 46,
	TMPL_FIELD_REQ = 47,
	TMPL_FIELD_REALNAME = 48,
};

/*
 * One row of CTemplate's flat field array (type + interned data
 * string). LoadTemplates (0x004BE297) fills these; CreateEntity
 * (0x004BD7C6) iterates from index 1.
 */
__extension__ typedef struct TemplateField TemplateField;
struct TemplateField {
	uint32_t type;
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64; // 64-bit alignment pad
#endif
	char *data;
};

#define MAX_TEMPLATE_FIELDS 512

/*
 * NPC template (CTemplate, 0x1060 bytes, ctor 0x004C05C0). Head is a
 * flat field array (count + 512 {type, data} entries). Tail fields at
 * +0x1004..+0x105F carry resolved region/movetype/corpse metadata.
 *
 * The offsets below name the field's position in CTemplateData, the binary's
 * raw 0x1060-byte layout, NOT its offset in this struct - the declaration
 * order here differs and the two cannot be cast to one another. This struct
 * splits the binary's opaque data[0x1000] blob into 512 typed rawFields
 * entries, which is exactly 0x1000 bytes on 32-bit, holds resourceNodes as a
 * real pointer so it widens on 64-bit, and appends three CUSTOM fields.
 */
struct NPCTemplate {
	uint16_t id;
	char name[32];                  // CUSTOM: display name for packet_handler.c
	int16_t nameTableId;            // CUSTOM: convenience back-reference
	uint8_t type;                   // CTemplateData 0x1059 (0=unknown, 1=item, 2=normal, 3=guard, 4=shopkeeper)
	int32_t frequency;              // CTemplateData 0x100C
#if __SIZEOF_POINTER__ == 8
	uint32_t _tmpl_pad64;           // 64-bit alignment pad
#endif
	CResourceNode *resourceNodes;   // CTemplateData 0x1004 (uint32 there; a real pointer here)
	CStringList regionList;         // CTemplateData 0x1010
	CStringList regionlimitList;    // CTemplateData 0x1020
	CStringList friendsList;        // CTemplateData 0x1030
	int32_t creatureHeight;         // CTemplateData 0x1040 (movetype field, default -1)
	int32_t alignment;              // CTemplateData 0x1044 (0=neutral, 1=good, 2=evil, 3=chaotic)
	CString corpseName;             // CTemplateData 0x1048
	uint8_t fleeval;                // CTemplateData 0x1058 (default 0x10)
	uint32_t createsnpcsTemplateId; // CTemplateData 0x105C
	TemplateField rawFields[MAX_TEMPLATE_FIELDS]; // CTemplateData 0x0004, its data[0x1000] blob
	int numRawFields;               // CTemplateData 0x0000, its count
};

#define MAX_TEMPLATES 1024
#define MAX_DEFINES   1024

// 0x004B1CFA - CClassification::BuildTable
struct CResBankRegion;

/*
 * Global per-template doubly-linked list heads at g_TemplateChain
 * (0x0068B3A8, 4096 entries). Each bucket points to the first NPC
 * spawned from that template; chain links via
 * CItem.templateChainNext/Prev. CountMobilesInBox (0x004AFBC8) walks
 * these for O(k) spawn-density checks.
 */
#define TEMPLATE_CHAIN_SIZE 0x1000

extern CItem *g_TemplateChain[TEMPLATE_CHAIN_SIZE]; // 0x0068B3A8
extern int g_TemplateChainCount[TEMPLATE_CHAIN_SIZE]; // 0x0068F3A8
extern CResManager g_TemplatesRM; // 0x006933F8
extern char *g_TemplateNames[TEMPLATE_CHAIN_SIZE]; // 0x00693A40
extern CMagicItemFactory g_MagicItemFactory; // 0x00697A50
extern int g_SpawnType12Count; // 0x006E7674
extern int g_SpawnTotalCount; // 0x006E7678
extern CNPC *g_VendorListHead; // 0x006EFECC
extern CResManager g_NameTableRM;
extern CResManager g_DefinesRM;
extern const uint8_t g_FieldCaseTable[48];

void LoadAll_ResBankDistrib(void); // 0x004341BD
int CResBankRegion_GetQuantity(CResBankRegion *this, int index); // 0x00434220
int CResBankRegion_HasSpawnEntry(CResBankRegion *region, uint16_t templateId); // Helper
int LocationInTemplateSubRegion(uint16_t templateId, int16_t x, int16_t y); // Helper
int DensityAtCapForRespawn(uint16_t templateId, int16_t x, int16_t y); // Helper
void *CResBankMagicCtx_Copy(CResListNode **dst, CResListNode **src); // 0x0043F2D0
void CResBankMagicCtx_PostInit(CResListNode **obj); // 0x00461F80
void CResBankManager_Init(void); // 0x004AD217
void CResBankManager_ResetCounters(void); // 0x004AD326
void CResBankManager_PrintSpawnStatistics(void); // 0x004AD38C
void CResBankManager_ClearAllRegions(void); // 0x004AD55E
void CResBankManager_LoadResBank(void); // 0x004AD8EC
void CResBankManager_NoOp(CResBankManager *this); // 0x004ADDD0
void CResBankManager_SaveResBank(CResBankManager *this, char **destPtr); // 0x004ADE8C
void ResBank_SaveToFile(void);
int CResBankRegion_SpawnAttempt(CResBankRegion *region, int spawnFilter); // 0x004AE153
void CResBankManager_SpawnTick(int arg1, int spawnFilter); // 0x004AE1D5
void CResBankManager_RespawnTimerCheck(void); // 0x004AE2B0
void CResBankManager_ProcessRespawnChunks(void); // 0x004AE362
void CResBankManager_RebuildHash(void); // 0x004AE38F
void CResBankManager_InsertRegion(CResBankRegion *region); // 0x004AE40E
void CResBankManager_AddRegion(CResBankRegion *region); // 0x004AE895
void CResBankManager_RemoveRegion(CResBankRegion *region, int freeMemory); // 0x004AE8C2
void CResBankRegion_Constructor(CResBankRegion *region); // 0x004AEA00
void CResBankRegion_Cleanup(CResBankRegion *region); // 0x004AEBF9
void CResBankRegion_SetTemplate(CResBankRegion *region, int index, int32_t value); // 0x004AEE71
void CResBankRegion_AddToQuantity(CResBankRegion *region, int index, int32_t amount); // 0x004AEEA6
void CResBankRegion_SubtractFromQuantity(CResBankRegion *region, int index, int32_t amount); // 0x004AEEE4
void CResBankRegion_AddToSpawnedCount(CResBankRegion *region, int index, int32_t amount); // 0x004AEF22
void CResBankRegion_SubtractFromSpawnedCount(CResBankRegion *region, int index, int32_t amount); // 0x004AEF68
void CResBankRegion_SubtractFromField103D8(CResBankRegion *region, int index, int32_t amount); // 0x004AEFAE
void CResBankRegion_AddToField103D8(CResBankRegion *region, int index, int32_t amount); // 0x004AEFF4
int ResBankLimitCheck(void *arg1, void *arg2); // 0x004AF03A
int HasTemplateInManager(int templateId); // 0x004AF0A9
int ResBankMagicCheck(CLocation *loc, int resTypeId); // 0x004AF0CE
int ResBankResourceCheck(CLocation *loc, int resTypeId, int amount); // 0x004AF0DD
void CResBankManager_ScheduleRespawnForTemplate(CLocation *loc, int templateIndex, int16_t amount, int16_t templateValue); // 0x004AF0EC
int CResBankManager_GetRespawnTimer(CLocation *loc, int templateIndex); // 0x004AF1AA
void CResBankManager_InitRespawn(void); // 0x004AF217
int CResBankManager_ProcessRespawnChunk(int maxIterations); // 0x004AF2DB
int CResBankManager_RequestCreateNPC(CLocation *loc, int maxDistance, uint32_t templateId); // 0x004AF695
uint32_t SpawnAtPointForLocation(uint16_t templateId, uint16_t x, uint16_t y, int16_t z, int noWander); // 0x004AF91C
int CResBankManager_SpawnForTemplate(int templateFilter); // 0x004B01D4
ResSpawnEntry *CResBankRegion_GetRandomTemplate(CResBankRegion *region); // 0x004B030A
int CResBankRegion_TrySpawn(CResBankRegion *region, int spawnFilter); // 0x004B042F
int CResBankRegion_ContainsPoint(CResBankRegion *region, int16_t x, int16_t y); // 0x004B0527
CResBankRegion *CResBankManager_GetRegionByLocation(int16_t x, int16_t y); // 0x004B0579
void CResBankManager_UpdateTemplateData(CResBankManager *self, int templateId, const char *name, const char *data, uint16_t flags); // 0x004B05CB
void CResBankManager_DeleteTemplateData(CResBankManager *self, int templateId); // 0x004B05D8
void RegisterEggInBlock(CItem *entity); // 0x004B05E5
int IsAnimalBody(int bodyId); // 0x004B07CB
int GetBodyType(int bodyId); // 0x004B0971
void ResBankManager_ProcessDynamicEggEntities(void); // 0x004B10C7
void BuildSpawnEntries(void); // 0x004B12A6
void EnsureSpawnEntriesBuilt(void); // 0x004B1982
void CClassification_BuildTable(CResBankRegion *region); // 0x004B1CFA
void InitResourceTypeIds(void); // 0x004B2A6A
void InitTemplateClassification(void); // 0x004B2DDE
void CResBankManager_SetRegionWanderFlags(void); // 0x004B3004
void RefreshResourceRegions(void); // 0x004B30B3
void ProcessDynamicItems(void); // 0x004B947D
int ParseAlignment(char *text); // 0x004B9AE0
int CTemplateManager_SpawnVendorStock(CItem *mob, char *dataCursor, int *counter, CLocation loc, void *manager, int *colorPtr, uint32_t limit, int depth); // 0x004BAAF1
void *CResBankMagicCtx_DefaultConstructor(CResListNode **obj); // 0x004C0580
void *CResBankMagicCtx_CopyConstructor(CResListNode **obj, CResListNode **src); // 0x004C05A0
CResManager *Spawn_GetTemplatesRM(void);
void CResList_AddTail(CResList *list, void *data);

#endif /* RESBANK_H_ */
