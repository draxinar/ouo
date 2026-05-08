#ifndef RESOURCE_ENTITY_H_
#define RESOURCE_ENTITY_H_

#include <stdint.h>
#include <stdio.h>

#include "entity.h"

__extension__ typedef struct CResourceEntity CResourceEntity;
__extension__ typedef struct CResourceNode CResourceNode;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CResourceType CResourceType;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CString CString;
__extension__ typedef struct ScriptAttachNode ScriptAttachNode;

/*
 * Resource-bank node (0x18 bytes). Allocated at 0x004A8911 and linked at
 * 0x00485B6F, populated from "r=%d %d %d %d %d" lines in dynamic0.mul.
 * Doubly-linked off CResourceEntity.firstChild.
 */
struct CResourceNode {
	uint8_t type;        // 0x00
	uint8_t _pad01;      // 0x01
	uint16_t id;         // 0x02
	int32_t value1;      // 0x04
	int32_t value2;      // 0x08
	int32_t value3;      // 0x0C
	CResourceNode *next; // 0x10
	CResourceNode *prev; // 0x14
};

/*
 * Second rung of the hierarchy: CEntity -> CResourceEntity (0x1C bytes),
 * extended by CItem. nextInContainer drives the container child chain;
 * terrain.c reuses it for the static item chain and free list. The
 * anonymous union at 0x14 is templateIndex for dynamic items and
 * staticPrev for static map entities.
 */
struct CResourceEntity {
	CEntity entity;
	CItem *nextInContainer;      // 0x10
	__extension__ union {
		__extension__ struct {
			uint8_t _resent_pad14[0x02]; // 0x14
			uint16_t templateIndex;      // 0x16 (CResourceEntity_GetTemplateIndex, 0x004E04A0)
#if __SIZEOF_POINTER__ == 8
			uint8_t _pad64_tpl[4]; // 64-bit alignment pad
#endif
		};
		CItem *staticPrev;           // 0x14 (static entity doubly-linked chain)
	};
	CResourceNode *firstChild;   // 0x18
};

uint16_t CResourceEntity_GetBodyType(CItem *item); // 0x00420FA0
CResourceType *CResourceNode_GetResourceDef(CResourceNode *node); // 0x004211A0
char *CResourceEntity_CheckDC_VT(CItem *item); // 0x00432260
void CResourceEntity_RemoveStatic(uint16_t artID, CResourceNode *nodePtr); // 0x0045B42C
void CResourceEntity_Delete(CItem *self); // 0x004850D3
void ResourceEntity_AttachTemplate(CItem *entity); // 0x00485261
void ResourceEntity_NotifyNodeRemoval(CResourceNode *node); // 0x004853B8
void CResourceEntity_NotifyPostModify(CItem *entity); // 0x004854B2
void CResourceEntity_NotifyPostModifyIfActive(CItem *entity); // 0x004854BD
void CResourceEntity_NotifyPreModify(CItem *entity); // 0x004854C8
void CResourceEntity_TransferResources(CItem *dest, CItem *source, uint32_t transferAmount, int resTypeFilter); // 0x00485523
void CResourceEntity_RemoveResource(CItem *entity, int amount, int resTypeId); // 0x004856C8
void CResourceEntity_TransferAllResources(CItem *dest, CItem *source); // 0x004857E1
CResourceNode *CResourceEntity_FindNodeByPtr(CItem *this, CResourceNode *target); // 0x0048592F
CResourceEntity *CResourceEntity_Constructor(CResourceEntity *this); // 0x00485969
void CResourceEntity_RemoveAllNodes(CItem *entity, int flag); // 0x004859E4
void CResourceEntity_Destructor(CResourceEntity *this); // 0x00485A73
void CResourceEntity_SendMusicToPlayer(CItem *resEntity, CPlayer *player, uint16_t regionIndex); // 0x00485ADD
void CResourceEntity_InsertNode(CItem *entity, CResourceNode *node); // 0x00485B6F
void CResourceEntity_RemoveNode(CItem *entity, CResourceNode *node); // 0x00485C53
CResourceNode *CResourceEntity_FindNode(CItem *entity, uint16_t id, int8_t type); // 0x00485CBB
CResourceNode *CResourceEntity_AddOrFindNode(CItem *this, CResourceType *resType, int8_t type); // 0x00485DB6
void CResourceEntity_AddNode(CItem *entity, uint16_t id, int8_t type, int32_t value1, int32_t value2, int32_t value3, int flag); // 0x00485FBD
void CResourceEntity_AddNodeScaled(
        CItem *entity, uint16_t id, int8_t type, int32_t value1, int32_t value2, int32_t value3, int zeroFlag, int notifyFlag, int scaleFactor); // 0x0048604A
void CResourceEntity_CopyNodeScaled(CItem *entity, CResourceNode *src, int zeroFlag, int notifyFlag, int scaleFactor); // 0x004860F5
void CResourceEntity_DetachFromSpatial(CItem *item); // 0x0048840D
int CResourceEntity_AddToTagList(CItem *entity, const char *name, int type, uintptr_t value); // 0x0048F1B0
int CResourceEntity_IsInTagList(CItem *entity, const char *name, int type, uintptr_t value); // 0x0048F21F
CItem *CResourceEntity_IsInTagListStr(CItem *entity, CString *name, uint32_t value); // 0x0048F26A
void CResourceEntity_ResetTemplateIndex(CItem *item); // 0x004912A0
void *CResourceEntity_ScalarDelete(CResourceEntity *self, int flags); // 0x00491340
void CResourceEntity_SetTemplateIndex(CItem *item, uint16_t index); // 0x004913E0
CResourceNode *ResourceNode_AllocFromPool(void); // 0x004A8911
void ResourceNode_ReturnToPool(CResourceNode *node); // 0x004A8A22
void CResourceNode_Save(CResourceNode *this, FILE *fp); // 0x004A8A46
int CResourceEntity_HasTag(CItem *entity, const char *name, int type); // 0x004CDCD4
void CResourceEntity_GetTagInt(CItem *entity, const char *name, int *outVal); // 0x004CDD01
void CResourceEntity_GetTagObj(CItem *entity, const char *name, uint32_t *outVal); // 0x004CDD2A
void CResourceEntity_GetTagLoc(CItem *entity, const char *name, CLocation *outLoc); // 0x004CDD53
CString *CResourceEntity_GetTagString(CItem *self, const char *name); // 0x004CDD7C
CList *CResourceEntity_GetTagEntity(CItem *entity, const char *name); // 0x004CDDCE
void CResourceEntity_RemoveScript(CItem *entity, const char *scriptName); // 0x004CDDF7
void CResourceEntity_RemoveScriptNode(CItem *entity, ScriptAttachNode *node); // 0x004CDE35
void CResourceEntity_DetachScript(CItem *entity, const char *name); // 0x004CDEAC
int CResourceEntity_HasScriptClass(CItem *entity, void *scriptClassPtr); // 0x004CDF4B
int CResourceEntity_GetTemplateIndex(CItem *ent); // 0x004E04A0

#endif /* RESOURCE_ENTITY_H_ */
