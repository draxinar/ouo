#ifndef TEMPLATE_H_
#define TEMPLATE_H_

#include <stdint.h>

#include "resbank.h"

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CResList CResList;
__extension__ typedef struct CResListNode CResListNode;
__extension__ typedef struct CResManager CResManager;
__extension__ typedef struct CString CString;
__extension__ typedef struct CStringPairList CStringPairList;
__extension__ typedef struct NPCTemplate NPCTemplate;

#define TEMPLATE_NAME_SLOTS 0x1000

/*
 * Paired CResManager used for template/name storage (constructor
 * 0x00475287).
 */
__extension__ typedef struct CTemplateResManager CTemplateResManager;
struct CTemplateResManager {
	CResManager primary;   // +0x000
	CResManager secondary; // +0x218
};

/*
 * Three-manager template system at g_TemplateManager (0x006933F8),
 * constructor 0x004BF71F: raw template bodies, the name-to-ID table, and
 * preprocessor defines, plus the interned-name slot array.
 */
__extension__ typedef struct CTemplateManager CTemplateManager;
struct CTemplateManager {
	CResManager templates;            // +0x000
	CResManager nameTable;            // +0x218
	CResManager defines;              // +0x430
	char *names[TEMPLATE_NAME_SLOTS]; // +0x648
};

/*
 * Parsed template payload (0x1060 bytes), constructor 0x004C05C0. A large
 * flat data blob followed by three CStringLists, creature metadata, and
 * a name CString.
 */
__extension__ typedef struct CTemplateData CTemplateData;
struct CTemplateData {
	uint32_t count;                 // 0x0000
	uint8_t data[0x1000];           // 0x0004
	uint32_t resourceNodes;         // 0x1004
	uint32_t field1008;             // 0x1008
	uint32_t frequency;             // 0x100C
	CStringList stringList1;        // 0x1010
	CStringList stringList2;        // 0x1020
	CStringList stringList3;        // 0x1030
	int32_t creatureHeight;         // 0x1040
	uint32_t alignment;             // 0x1044
	CString str;                    // 0x1048
	uint8_t fleeval;                // 0x1058
	uint8_t npcType;                // 0x1059
	uint8_t _pad105A[2];            // 0x105A
	uint32_t createsnpcsTemplateId; // 0x105C
};

int CTemplateManager_LoadNameTable(void); // 0x004B9C68
void CTemplateManager_SetRealNameFromTemplate(CItem *entity, char *dataCursor); // 0x004B9FE3
char *CTemplateManager_ParseColorExpression(char *dataCursor, char *buf1, char *buf2); // 0x004BC83E
char *CTemplateManager_ParseColor(char *dataCursor, int *colorOut, int *colorPtr); // 0x004BCB35
void CTemplateManager_ResetMobile(CMobile *mob); // 0x004BCF88
char *CTemplateManager_GetNextEqValue(char *dataCursor, CString *out); // 0x004BD456
void Template_SetRealName(CItem *item, char *text); // 0x004BD67A
void CTemplateManager_startup(void); // 0x004BF86F
void CTemplateManager_Shutdown(void); // 0x004BF892
CItem *CTemplateManager_CreateFromTemplate(uint16_t templateId, CLocation *loc, int restockFlag, int depth, CItem *existingMob); // 0x004BF8B5
NPCTemplate *CResManager_GetTemplateByID(uint16_t id); // 0x004BFBAA
int CTemplateManager_GetTemplateColorCount(uint16_t bodyType); // 0x004BFBD7
void CTemplateManager_ApplyTemplate(int isRestock, CItem *entity, int amount, CLocation *loc, int depth, int zero); // 0x004C0397
int CTemplateManager_HasAnimationVariant(int bodyId); // 0x004C0462
int CTemplateManager_HasAnimation(uint16_t templateId); // 0x004C04BB
int CTemplateManager_ValidateOwner(uint32_t serial, CItem *mob); // 0x004C0508
int CTemplateManager_ApplyLabel(CString *label, CItem **entity); // 0x004C0F70
void CStringPairList_Init(CStringPairList *sl); // 0x004C10A0
void CResManager_ApplyTemplateData(CResList *list, CResListNode *valNode, CItem **entity); // 0x004C1CF0
void *CTemplate_ScalarDelete(NPCTemplate *this, int flags); // 0x004C29E0

#endif /* TEMPLATE_H_ */
