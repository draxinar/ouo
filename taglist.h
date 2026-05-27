#ifndef TAGLIST_H_
#define TAGLIST_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CScript CScript;
__extension__ typedef struct CVector CVector;
__extension__ typedef struct CString CString;
__extension__ typedef struct TagNode TagNode;
__extension__ typedef struct CTagListManager CTagListManager;

/*
 * Per-entity tag storage node (16 bytes), allocated from the pool at
 * 0x0063E108 by TagNode_Constructor (0x00426983). The value field is
 * interpreted by type: INT/OBJ store the integer directly; STRING/USTRING
 * hold a heap-owned CString/CUString; LOC holds a 6-byte CLocation; LIST
 * holds a deep-copied CList. Names are interned via
 * CScriptManager_InternString, not strdup'd.
 */
struct TagNode {
	uint32_t type;   // +0x00 (WTYPE_* constant)
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64; // 64-bit alignment pad
#endif
	uintptr_t value; // +0x04
	char *name;      // +0x08 (interned, do not free)
	TagNode *next;   // +0x0C
};

/*
 * CScriptInstance (28 bytes), the per-entity node carrying a script
 * attachment. Allocated from the pool at 0x0063D8B0 by
 * CScriptInstance_Constructor (0x004245B3) and linked into the owner by
 * AttachToEntity (0x00424AFD). TreeEvaluator uses the CExecThread+0x04
 * scriptRef as a pointer here, and treats memberScope == 0xABCD as the
 * destroyed sentinel.
 */
__extension__ typedef struct ScriptAttachNode ScriptAttachNode;
struct ScriptAttachNode {
	void *scriptClassPtr;         // +0x00 (CScript*)
	uint32_t field04;             // +0x04
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;              // 64-bit alignment pad
#endif
	void *memberScope;            // +0x08
	void *entity;                 // +0x0C (CItem* back-pointer)
	ScriptAttachNode *next;       // +0x10 (per-entity list)
	ScriptAttachNode *globalNext; // +0x14 (global list at 0x0063D8CC)
	ScriptAttachNode *globalPrev; // +0x18
};

/*
 * Per-entity tag/script list header (8 bytes), allocated from the block
 * pool at 0x006EF6B8 and init'd by CTagListManager_Init (0x004CE0B2).
 * Lives at CItem+0x48.
 */
struct CTagListManager {
	ScriptAttachNode *scriptList; // +0x00
	TagNode *head;                // +0x04
};

struct CVector;
__extension__ typedef struct CList CList;
struct CItem;

void CScriptInstance_Constructor(ScriptAttachNode *inst, CScript *scriptClass); // 0x004245B3
void CScriptInstance_Clear(ScriptAttachNode *node); // 0x00424818
void ScriptAttachNode_UnlinkFromGlobal(ScriptAttachNode *node); // 0x00424A68
void CScriptInstance_AttachToEntity(ScriptAttachNode *inst, CItem *entity); // 0x00424AFD
void WomScr_LoadFromLine(uint32_t serial, const char *val); // 0x004CC33B
CTagListManager *TagListManager_New(void); // 0x004CDA50
int CWombatManager_GetScriptsForObj(CItem *entity, CVector *scriptVec, CVector *trigVec, int eventType); // 0x004CDBFF
TagNode *CEntity_SetObjVar(CItem *entity, const char *name, int type, uintptr_t value); // 0x004CDEEA
void CTagListManager_Init(CTagListManager *mgr); // 0x004CE0B2
TagNode *CTagListManager_GetHead(CTagListManager *mgr); // 0x004CE0DB
void CTagListManager_Destroy(CTagListManager *mgr); // 0x004CE0EC
int CTagListManager_HasTagDefs(CTagListManager *mgr); // 0x004CE186
int CTagListManager_HasScripts(CTagListManager *mgr); // 0x004CE19F
void CTagListManager_WalkScriptNodes(CTagListManager *mgr, CVector *list); // 0x004CE1B7
void CTagListManager_GetTagDefList(CTagListManager *mgr, CVector *list); // 0x004CE1F5
int CTagListManager_GetTriggers(CTagListManager *tagList, CItem *entity, CVector *scriptVec, CVector *trigVec, int eventType); // 0x004CE234
void ObjVarData_CollectEntries(CTagListManager *objVarData, CVector *list, int type); // 0x004CE2CF
TagNode *TagList_FindByName(CTagListManager *mgr, const char *name); // 0x004CE32F
TagNode *TagList_FindByPrefix(CTagListManager *mgr, const char *prefix); // 0x004CE380
int TagList_HasLinkedName(CTagListManager *mgr, const char *name); // 0x004CE3E7
int CTagListManager_RemoveScriptNode(CTagListManager *mgr, ScriptAttachNode *target); // 0x004CE464
int CTagListManager_RemoveScript(CTagListManager *mgr, const char *scriptName); // 0x004CE4D1
int TagList_RemoveByName(CTagListManager *mgr, const char *name); // 0x004CE556
int CTagListManager_HasScriptByName(CTagListManager *mgr, const char *name); // 0x004CE5DD
int CTagListManager_HasScript(CTagListManager *mgr, void *scriptClassPtr); // 0x004CE662
void CTagListManager_PrependScript(CTagListManager *mgr, ScriptAttachNode *node); // 0x004CE6A7
TagNode *TagList_SetTag(CTagListManager *mgr, const char *name, int type, uintptr_t value); // 0x004CE6C7
int TagList_HasTag(CTagListManager *mgr, const char *name, int type); // 0x004CEA00
void TagList_GetTagInt(CTagListManager *mgr, const char *name, int *outVal); // 0x004CEA63
void TagList_GetTagObj(CTagListManager *mgr, const char *name, uint32_t *outVal); // 0x004CEAC2
void TagList_GetTagLoc(CTagListManager *mgr, const char *name, CLocation *outLoc); // 0x004CEB21
CString *TagList_GetTagString(CTagListManager *mgr, const char *name); // 0x004CEB84
char *TagList_GetTagUString(CTagListManager *mgr, const char *name); // 0x004CEBE0
CList *CTagListManager_GetListEntry(CTagListManager *mgr, const char *name); // 0x004CEC3C
void CTagListManager_Lock(CTagListManager *mgr); // 0x004CEC98
void CTagListManager_Unlock(CTagListManager *mgr); // 0x004CECA3

void TagList_DestroyPools(void); // CUSTOM

#endif /* TAGLIST_H_ */
