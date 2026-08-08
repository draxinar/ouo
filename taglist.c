/*
 * Tag list - per-entity script attachment table and trigger dispatch.
 *
 * A CTagListManager hangs off each CEntity carrying (tagKey, value) pairs
 * and attached Wombat scripts. Triggers fire scripts matching an event
 * type, and the add/remove paths keep the serial list in sync with each
 * script instance's own attachment chain.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "item.h"
#include "log.h"
#include "nodepool.h"
#include "objvar.h"
#include "region.h"
#include "taglist.h"
#include "time.h"
#include "vg_pool.h"
#include "wombat.h"
#include "wombat_compile.h"
#include "wombat_exec.h"
#include "world.h"

static void CScriptInstance_DetachFromThreads(ScriptAttachNode *node); // 0x00424AB1

/*
 * 0x004245B3 - CScriptInstance::CScriptInstance
 *
 * Constructs a script attach node: allocates member scope buffer,
 * initializes STRING/USTRING/LIST variables, and links into the
 * global CScriptInstance list.
 */
void
CScriptInstance_Constructor(ScriptAttachNode *inst, CScript *scriptClass)
{
	int i;
	CNamedScopeEntry *entries;

	inst->scriptClassPtr = scriptClass;
	inst->field04 = 0;
	inst->entity = NULL;
	inst->next = NULL;

	if (scriptClass->namedScope.count > 0) {
		int totalSize = scriptClass->namedScope.totalSize;
		inst->memberScope = calloc(1, totalSize);

		entries = (CNamedScopeEntry *)scriptClass->namedScope.entries;
		for (i = 0; i < scriptClass->namedScope.count; i++) {
			int typeId = entries[i].typeId;
			int offset = entries[i].offset;
			if (typeId == WTYPE_STRING) {
				CString *cs = (CString *)malloc(sizeof(CString));
				if (cs != NULL)
					CString_Constructor(cs, "");
				*(void **)((char *)inst->memberScope + offset) = cs;
			} else if (typeId == WTYPE_USTRING) {
				CUString *cus = (CUString *)malloc(sizeof(CUString));
				if (cus != NULL)
					CUString_Constructor(cus, NULL);
				*(void **)((char *)inst->memberScope + offset) = cus;
			} else if (typeId == WTYPE_LIST) {
				CList *lst = (CList *)malloc(sizeof(CList));
				if (lst != NULL)
					CList_Constructor(lst);
				*(void **)((char *)inst->memberScope + offset) = lst;
			}
		}
	} else {
		inst->memberScope = NULL;
	}

	// Link into global CScriptInstance list (0x0063D8CC)
	inst->globalNext = g_scriptInstanceListHead;
	inst->globalPrev = NULL;
	if (g_scriptInstanceListHead != NULL)
		g_scriptInstanceListHead->globalPrev = inst;
	g_scriptInstanceListHead = inst;
}
/*
 * 0x00424818 - CScriptInstance::Clear
 *
 * Full cleanup of a ScriptAttachNode: detaches from threads, destructs
 * STRING/USTRING/LIST member vars, frees scope buffer, purges pending
 * events, unregisters timers and light subscriptions, and removes from
 * block tracking, region, and entity's script list.
 */
void
CScriptInstance_Clear(ScriptAttachNode *node)
{
	CScript *sc;
	CNamedScopeEntry *entries;
	int i;
	int type;
	void *val;
	CTrigger *trig;

	CScriptInstance_DetachFromThreads(node);

	if (node->memberScope != NULL) {
		sc = (CScript *)node->scriptClassPtr;
		entries = (CNamedScopeEntry *)sc->namedScope.entries;

		for (i = 0; i < sc->namedScope.count; i++) {
			type = entries[i].typeId;

			if (type == 1) {
				memcpy(&val, (char *)node->memberScope + entries[i].offset, sizeof(void *));
				if (val != NULL)
					CString_ScalarDelete((CString *)val, 1);
			} else if (type == 2) {
				memcpy(&val, (char *)node->memberScope + entries[i].offset, sizeof(void *));
				if (val != NULL)
					CUString_ScalarDelete((CUString *)val, 1);
			} else if (type == 5) {
				memcpy(&val, (char *)node->memberScope + entries[i].offset, sizeof(void *));
				if (val != NULL)
					CList_ScalarDelete((CList *)val, 1);
			}
		}

		OperatorDelete(node->memberScope);
		node->memberScope = NULL;
	}

	sc = (CScript *)node->scriptClassPtr;
	if (sc->trigHandlers[14] != NULL)
		EventRingBuffer_PurgeScriptEvents(node);

	trig = (CTrigger *)sc->trigHandlers[14];
	while (trig != NULL) {
		CTimeManager_UnregisterTimedEvent(node, (const char *)trig->filterData);
		trig = trig->next;
	}

	trig = (CTrigger *)sc->trigHandlers[38];
	while (trig != NULL) {
		CLightManager_UnregisterScript(node);
		trig = trig->next;
	}

	Block_RemoveTrackingNode((CItem *)node->entity);
	UpdateRegion((CItem *)node->entity);
	CResourceEntity_RemoveScriptNode((CItem *)node->entity, node);

	node->entity = NULL;
	node->memberScope = (void *)(uintptr_t)0xABCD;
	node->scriptClassPtr = (void *)(uintptr_t)0xABCD;
}

/*
 * 0x00424A68 - ScriptAttachNode::UnlinkFromGlobal
 *
 * Unlinks a ScriptAttachNode from the global instance list.
 */
void
ScriptAttachNode_UnlinkFromGlobal(ScriptAttachNode *node)
{
	if (node->globalNext != NULL)
		node->globalNext->globalPrev = node->globalPrev;
	if (node->globalPrev != NULL)
		node->globalPrev->globalNext = node->globalNext;
	else
		g_scriptInstanceListHead = node->globalNext;
}

/*
 * 0x00424AB1 - CScriptInstance::DetachFromThreads
 *
 * Stops any active thread referencing this instance and clears the
 * scriptRef on every global thread that points at it.
 */
static void
CScriptInstance_DetachFromThreads(ScriptAttachNode *node)
{
	CExecThread *t;

	ThreadList_StopBySerial(&g_activeThreadList, (uintptr_t)node);
	for (t = g_globalThreadHead; t != NULL; t = t->globalNext) {
		if (t->scriptRef == (void *)node) {
			t->scriptRef = NULL;
		}
	}
}

/*
 * 0x00424AFD - CScriptInstance::AttachToEntity
 *
 * Attaches a script instance to an entity: registers timed events and
 * light subscriptions, refreshes block tracking, and writes the entity
 * serial into the instance's "this" member variable.
 */
void
CScriptInstance_AttachToEntity(ScriptAttachNode *inst, CItem *entity)
{
	CScript *sc;
	CTrigger *trig;
	uint32_t serial;
	CNamedScopeEntry *thisVar;

	CItem_AddScript(entity, inst);

	sc = (CScript *)inst->scriptClassPtr;
	trig = (CTrigger *)sc->trigHandlers[14];
	while (trig != NULL) {
		CTimeManager_RegisterTimedEvent(inst, (const char *)trig->filterData);
		trig = trig->next;
	}

	trig = (CTrigger *)sc->trigHandlers[38];
	while (trig != NULL) {
		CLightManager_RegisterScript(inst);
		trig = trig->next;
	}

	if (!entity->resourceEntity.entity.removedFromWorld) {
		Block_RemoveTrackingNode(entity);
		UpdateRegion(entity);
	}

	serial = entity->serial;
	thisVar = (CNamedScopeEntry *)CNamedScope_FindVar(&sc->namedScope, "this");
	memcpy((char *)inst->memberScope + thisVar->offset, &serial, 4);
}

// 0x006EF6B8 - CTagListManager pool free list (link via mgr->head at +0x04)
static CTagListManager *g_tagListMgrFreeList;
/*
 * 0x004CC33B - wom_scr load handler (inside LoadDynamic0_ParseBlock)
 *
 * Parses "scriptName numVars [typeName varName value]..." from a
 * wom_scr= line, attaches the script to the entity, and writes each
 * parsed variable value into the instance's member scope at the
 * varName's offset in the script class's namedScope.
 */
void
WomScr_LoadFromLine(uint32_t serial, const char *val)
{
	char scriptName[128];
	char typeName[8];
	char varName[128];
	int numVars;
	const char *p;
	CItem *item;
	ScriptAttachNode *foundScript;
	CScript *scriptDef;
	CVector scriptVec;
	static const char vecType = 0;
	uintptr_t *iter, *end;
	int varIdx;

	numVars = 0;
	if (sscanf(val, "%127s %d", scriptName, &numVars) < 1)
		return;
	if (scriptName[0] == '\0')
		return;

	p = val;
	while (*p != ' ')
		p++;
	p++;

	while (*p != '\0' && *p != ' ')
		p++;
	if (*p != '\0')
		p++;

	item = CWorld_FindBySerial(g_World, serial);
	if (item == NULL)
		return;
	Entity_AttachScript(item, scriptName, 0);

	if (item->tagList == NULL || !CTagListManager_HasScripts(item->tagList))
		return;

	CVector_Constructor(&scriptVec, &vecType);
	CItem_GetScriptListRaw(item, &scriptVec);

	foundScript = NULL;
	iter = (uintptr_t *)scriptVec.begin;
	end = (uintptr_t *)scriptVec.end;
	while (iter < end) {
		ScriptAttachNode *sn = (ScriptAttachNode *)*iter;
		CScript *sc = (CScript *)sn->scriptClassPtr;
		if (sc != NULL && sc->name != NULL && strcasecmp(sc->name, scriptName) == 0) {
			foundScript = sn;
			break;
		}
		iter++;
	}

	if (foundScript == NULL) {
		CVector_Destructor(&scriptVec);
		return;
	}

	scriptDef = (CScript *)foundScript->scriptClassPtr;

	for (varIdx = 0; varIdx < numVars; varIdx++) {
		int typeIdx;
		int dataOffset;
		CNamedScopeEntry *entry;
		CLocation loc;

		if (sscanf(p, "%7s %127s", typeName, varName) < 2)
			break;

		typeIdx = -1;
		for (int i = 0; i < 7; i++) {
			if (strncasecmp(typeName, g_womTypeNames[i], 3) == 0) {
				typeIdx = i;
				break;
			}
		}

		while (*p != '\0' && *p != ' ')
			p++;
		if (*p != '\0')
			p++;
		while (*p != '\0' && *p != ' ')
			p++;
		if (*p != '\0')
			p++;

		dataOffset = -1;
		entry = (CNamedScopeEntry *)CNamedScope_FindVar(&scriptDef->namedScope, varName);
		if (entry != NULL)
			dataOffset = entry->offset;

		CLocation_Init(&loc);

		if (typeIdx < 0 || typeIdx > 5)
			continue;

		switch (typeIdx) {
		case 0: { // int
			int intVal = 0;
			sscanf(p, "%d", &intVal);
			while (*p != '\0' && *p != ' ')
				p++;
			if (*p != '\0')
				p++;
			if (dataOffset != -1 && foundScript->memberScope != NULL)
				memcpy((char *)foundScript->memberScope + dataOffset, &intVal, 4);
			break;
		}
		case 1: { // str
			char decoded[256];
			p = ObjVar_UnescapeStr(p, decoded);
			if (*p != '\0')
				p++;
			if (dataOffset != -1 && foundScript->memberScope != NULL) {
				CString *cs;
				memcpy(&cs, (char *)foundScript->memberScope + dataOffset, sizeof(void *));
				if (cs != NULL)
					CString_AssignCStr(cs, decoded);
			}
			break;
		}
		case 2: { // ust (unicode string)
			uint16_t wbuf[256];
			p = Hex2Wchar((char *)p, wbuf);
			if (*p != '\0')
				p++;
			if (dataOffset != -1 && foundScript->memberScope != NULL) {
				CUString *cus;
				memcpy(&cus, (char *)foundScript->memberScope + dataOffset, sizeof(void *));
				if (cus != NULL)
					CUString_AssignCStr(cus, wbuf);
			}
			break;
		}
		case 3: { // loc
			int x = 0, y = 0, z = 0;
			sscanf(p, "%d %d %d", &x, &y, &z);
			for (int t = 0; t < 3; t++) {
				while (*p != '\0' && *p != ' ')
					p++;
				if (*p != '\0')
					p++;
			}
			CLocation_Set(&loc, (uint16_t)x, (uint16_t)y, (int16_t)z);
			if (dataOffset != -1 && foundScript->memberScope != NULL)
				memcpy((char *)foundScript->memberScope + dataOffset, &loc, 6);
			break;
		}
		case 4: { // obj (serial)
			unsigned int objVal = 0;
			sscanf(p, "%u", &objVal);
			while (*p != '\0' && *p != ' ')
				p++;
			if (*p != '\0')
				p++;
			if (dataOffset != -1 && foundScript->memberScope != NULL)
				memcpy((char *)foundScript->memberScope + dataOffset, &objVal, 4);
			break;
		}
		case 5: { // lis (list)
			int listCount = 0;
			CList *newList;

			sscanf(p, "%d", &listCount);
			while (*p != '\0' && *p != ' ')
				p++;
			if (*p != '\0')
				p++;

			newList = (CList *)OperatorNew(sizeof(CList));
			if (newList != NULL)
				CList_Constructor(newList);

			if (newList != NULL)
				p = List_DeserializeFromBuf(p, newList, listCount);

			if (dataOffset != -1 && foundScript->memberScope != NULL) {
				CList *oldList;
				memcpy(&oldList, (char *)foundScript->memberScope + dataOffset, sizeof(void *));
				if (oldList != NULL)
					CList_ScalarDelete(oldList, 1);
				memcpy((char *)foundScript->memberScope + dataOffset, &newList, sizeof(void *));
			} else {
				// No matching var - discard the list
				if (newList != NULL)
					CList_ScalarDelete(newList, 1);
			}
			break;
		}
		}
	}

	CVector_Destructor(&scriptVec);
}

/*
 * 0x004CDA50 - TagListManager_New (MODIFIED)
 *
 * Allocates a CTagListManager, popping from the free list if non-empty
 * or mallocing a new one. The binary uses a block allocator
 * (0x1000 entries of 8 bytes each); we use malloc + free list instead.
 */
CTagListManager *
TagListManager_New(void)
{
	static int poolCreated;
	CTagListManager *mgr;

	if (!poolCreated) {
		VG_CREATE_POOL(&g_tagListMgrFreeList);
		poolCreated = 1;
	}

	if (g_tagListMgrFreeList != NULL) {
		mgr = g_tagListMgrFreeList;
		VG_POOL_ALLOC(&g_tagListMgrFreeList, mgr, sizeof(CTagListManager));
		VG_MAKE_DEFINED(&mgr->head, sizeof(mgr->head));
		g_tagListMgrFreeList = (CTagListManager *)mgr->head;
	} else {
		mgr = (CTagListManager *)OperatorNew(sizeof(CTagListManager));
		VG_POOL_ALLOC(&g_tagListMgrFreeList, mgr, sizeof(CTagListManager));
	}
	CTagListManager_Init(mgr);
	return mgr;
}

/*
 * 0x004CDBFF - CWombatManager::GetScriptsForObj
 *
 * Returns the count of triggers on the entity's scripts that match the
 * given event type, populating scriptVec and trigVec. Returns 0 if the
 * entity has no tag list.
 */
int
CWombatManager_GetScriptsForObj(CItem *entity, CVector *scriptVec, CVector *trigVec, int eventType)
{
	CTagListManager *tagList;

	tagList = entity->tagList;
	if (tagList == NULL)
		return 0;
	return CTagListManager_GetTriggers(tagList, entity, scriptVec, trigVec, eventType);
}
/*
 * 0x004CDEEA - CEntity::SetObjVar
 *
 * Allocates the entity's tagList if needed, then sets the named tag.
 */
TagNode *
CEntity_SetObjVar(CItem *entity, const char *name, int type, uintptr_t value)
{
	if (entity->tagList == NULL)
		entity->tagList = TagListManager_New();
	return TagList_SetTag(entity->tagList, name, type, value);
}

/*
 * 0x004CE0B2 - CTagListManager::Init
 *
 * Zeros the scriptList and head fields after allocation.
 */
void
CTagListManager_Init(CTagListManager *mgr)
{
	mgr->scriptList = NULL;
	mgr->head = NULL;
}

/*
 * 0x004CE0DB - CTagListManager::GetHead
 *
 * Returns the tag list head pointer.
 */
TagNode *
CTagListManager_GetHead(CTagListManager *mgr)
{
	return mgr->head;
}

/*
 * 0x004CE0EC - CTagListManager::Destroy
 *
 * Pops all tag nodes and script nodes from front of their lists,
 * returning each to its pool, then returns the manager itself to the
 * free list. The pop-from-front pattern avoids re-entrancy issues
 * from ReturnToPool -> Clear -> CResourceEntity_RemoveScriptNode.
 *
 * FIXED: Binary free-list return has a bug - it stores mgr->head
 * in g_freelist instead of mgr, leaking each manager. We store
 * mgr directly so it can be reused.
 */
void
CTagListManager_Destroy(CTagListManager *mgr)
{
	TagNode *node;
	ScriptAttachNode *snode;

	CTagListManager_Lock(mgr);
	CTagListManager_Unlock(mgr);
	// Binary does NOT free node->name (names are interned via
	// CScriptManager_InternString and live forever).
	while (mgr->head != NULL) {
		node = mgr->head;
		mgr->head = node->next;
		TagNode_ReturnToPool(node);
	}

	while (mgr->scriptList != NULL) {
		snode = mgr->scriptList;
		mgr->scriptList = snode->next;
		CScriptInstance_ReturnToPool(snode);
	}

	// Redundant after loops, but matches binary
	mgr->scriptList = NULL;
	mgr->head = NULL;

	mgr->head = (TagNode *)g_tagListMgrFreeList;
	g_tagListMgrFreeList = mgr;
	VG_POOL_FREE(&g_tagListMgrFreeList, mgr);
}

/*
 * 0x004CE186 - CTagListManager::HasTagDefs
 *
 * Returns 1 if the tag list head is non-NULL, 0 otherwise.
 */
int
CTagListManager_HasTagDefs(CTagListManager *mgr)
{
	return (mgr->head != NULL);
}

/*
 * 0x004CE19F - CTagListManager::HasScripts
 *
 * Returns 1 if the script list is non-NULL, 0 otherwise.
 */
int
CTagListManager_HasScripts(CTagListManager *mgr)
{
	return (mgr->scriptList != NULL);
}

/*
 * 0x004CE1B7 - CTagListManager::WalkScriptNodes
 *
 * Pushes each ScriptAttachNode in the script list into the vector.
 */
void
CTagListManager_WalkScriptNodes(CTagListManager *mgr, CVector *list)
{
	ScriptAttachNode *node;

	CTagListManager_Unlock(mgr);
	node = mgr->scriptList;
	while (node != NULL) {
		CVector_PushBack(list, (uintptr_t)node);
		node = node->next;
	}
}

/*
 * 0x004CE1F5 - CTagListManager::GetTagDefList
 *
 * Pushes each TagNode in the tag list into the vector.
 */
void
CTagListManager_GetTagDefList(CTagListManager *mgr, CVector *list)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		CVector_PushBack(list, (uintptr_t)node);
		node = node->next;
	}
}

/*
 * Helper - FindLoadedScriptByPtr
 *
 * Diagnostic helper for CTagListManager_GetTriggers. Walks the loaded
 * script list and returns the matching CScript's name pointer if target
 * is still a live CScript. Returns NULL when target was destroyed and
 * freed (so dereferencing its name field is unsafe).
 */
static const char *
FindLoadedScriptByPtr(const CScript *target)
{
	CScript *sc;

	if (target == NULL || (uintptr_t)target == 0xABCD)
		return NULL;
	for (sc = g_ScriptManager.head; sc != NULL; sc = sc->nextLoaded) {
		if (sc == target)
			return sc->name;
	}
	return NULL;
}

/*
 * 0x004CE234 - CTagListManager::GetTriggers
 *
 * Walks each attached script's trigger chain for the given event type.
 * Triggers with filter==0x3E8 always match; others use a probability
 * check ((rand() & 0x3FF) < filter). Matching (script, trigger) pairs
 * are pushed into the output vectors. Returns the match count.
 *
 * FIXED: Binary signature is (tagList, scriptVec, trigVec, eventType)
 * and dereferences node->scriptClassPtr->trigHandlers[eventType]
 * unconditionally. If a stale ScriptAttachNode lingers in the
 * scriptList (CScriptInstance_Clear poisons scriptClassPtr to 0xABCD
 * before pool return, or the underlying CScript was freed via
 * TriggerEdit reload), the deref of trigHandlers[eventType] or the
 * resulting trig pointer SIGSEGVs. Fix: skip and log nodes whose
 * scriptClassPtr is NULL, poisoned, or no longer in
 * g_ScriptManager.head, and thread the owning entity through from
 * CWombatManager_GetScriptsForObj so the log line names the entity
 * (serial + bodyType). Mirrors the CTimeManager_DispatchEventList
 * stale-subscription guard.
 */
int
CTagListManager_GetTriggers(CTagListManager *tagList, CItem *entity, CVector *scriptVec, CVector *trigVec, int eventType)
{
	ScriptAttachNode *node;
	CScript *bsc;
	CTrigger *trig;
	int count;

	CTagListManager_Unlock(tagList);
	count = 0;
	for (node = tagList->scriptList; node != NULL; node = node->next) {
		bsc = (CScript *)node->scriptClassPtr;
		if (bsc == NULL || (uintptr_t)bsc == 0xABCD || FindLoadedScriptByPtr(bsc) == NULL) {
			char msg[320];
			int poisoned = (uintptr_t)bsc == 0xABCD;
			uint32_t entSerial = entity != NULL ? entity->serial : 0;
			uint32_t entBody = entity != NULL ? entity->resourceEntity.entity.bodyType : 0;
			snprintf(msg, sizeof(msg), "stale ScriptAttachNode entity=%p serial=0x%08X bodyType=0x%04X tagList=%p node=%p scriptClassPtr=%p eventType=%d%s",
			        (void *)entity, entSerial, entBody, (void *)tagList, (void *)node, (void *)bsc, eventType,
			        poisoned ? " (Clear ran, not removed from scriptList)" : " (CScript not in g_ScriptManager)");
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "taglist", "stale", msg);
			continue;
		}
		trig = (CTrigger *)bsc->trigHandlers[eventType];
		while (trig != NULL) {
			if (trig->filter != 0x3E8) {
				if ((rand() & 0x3FF) >= trig->filter) {
					trig = trig->next;
					continue;
				}
			}
			CVector_PushBack(scriptVec, (uintptr_t)node);
			CVector_PushBack(trigVec, (uintptr_t)trig);
			count++;
			trig = trig->next;
		}
	}
	return count;
}

/*
 * 0x004CE2CF - ObjVarData::CollectEntries
 *
 * For each attached script, walks the trigger chain at trigHandlers[type]
 * and pushes each entry pointer into the output vector.
 */
void
ObjVarData_CollectEntries(CTagListManager *mgr, CVector *list, int type)
{
	ScriptAttachNode *node;
	CScript *sub;
	CTrigger *entry;

	CTagListManager_Unlock(mgr);
	node = mgr->scriptList;
	while (node != NULL) {
		sub = (CScript *)node->scriptClassPtr;
		entry = (CTrigger *)sub->trigHandlers[type];
		while (entry != NULL) {
			CVector_PushBack(list, (uintptr_t)entry);
			entry = entry->next;
		}
		node = node->next;
	}
}

/*
 * 0x004CE32F - TagList::FindByName
 *
 * Returns the tag node whose name matches (case-insensitive), or NULL.
 */
TagNode *
TagList_FindByName(CTagListManager *mgr, const char *name)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (strcasecmp(node->name, name) == 0)
			return node;
		node = node->next;
	}
	return NULL;
}

/*
 * 0x004CE380 - TagList::FindByPrefix
 *
 * Returns the first tag node whose name begins with the given 5-char
 * prefix (case-insensitive) and has length >= 7, or NULL.
 */
TagNode *
TagList_FindByPrefix(CTagListManager *mgr, const char *prefix)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (strncasecmp(node->name, prefix, 5) == 0) {
			if (strlen(node->name) >= 7)
				return node;
		}
		node = node->next;
	}
	return NULL;
}

/*
 * 0x004CE3E7 - TagList::HasLinkedName
 *
 * Returns 1 if any STRING tag whose name starts with "link" has a
 * value equal to name+6 (case-insensitive), 0 otherwise.
 */
int
TagList_HasLinkedName(CTagListManager *mgr, const char *name)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (node->type == 1) {
			if (strncasecmp("link", node->name, 4) == 0) {
				if (strcasecmp(name + 6, CString_GetData((CString *)(uintptr_t)node->value)) == 0)
					return 1;
			}
		}
		node = node->next;
	}
	return 0;
}

/*
 * 0x004CE464 - CTagListManager::RemoveScriptNode
 *
 * Unlinks the matching script node and returns it to the pool.
 * Returns 0 if both lists are empty after removal, 1 otherwise.
 */
int
CTagListManager_RemoveScriptNode(CTagListManager *mgr, ScriptAttachNode *target)
{
	ScriptAttachNode **pp;

	CTagListManager_Unlock(mgr);
	pp = &mgr->scriptList;
	while (*pp != NULL) {
		if (*pp == target) {
			*pp = target->next;
			CScriptInstance_ReturnToPool(target);
			if (mgr->scriptList == NULL && mgr->head == NULL)
				return 0;
			return 1;
		}
		pp = &(*pp)->next;
	}
	return 1;
}

/*
 * 0x004CE4D1 - CTagListManager::RemoveScript
 *
 * Unlinks the script node whose class name matches (case-insensitive)
 * and returns it to the pool. Returns 0 if both lists are empty after
 * removal, 1 otherwise (including when no match is found).
 */
int
CTagListManager_RemoveScript(CTagListManager *mgr, const char *scriptName)
{
	ScriptAttachNode **pp;
	ScriptAttachNode *node;
	char *nodeName;

	CTagListManager_Unlock(mgr);
	pp = &mgr->scriptList;
	while (*pp != NULL) {
		nodeName = *(char **)(*pp)->scriptClassPtr;
		if (strcasecmp(nodeName, scriptName) == 0) {
			node = *pp;
			*pp = node->next;
			CScriptInstance_ReturnToPool(node);
			if (mgr->scriptList == NULL && mgr->head == NULL)
				return 0;
			return 1;
		}
		pp = &(*pp)->next;
	}
	return 1;
}

/*
 * 0x004CE556 - TagList::RemoveByName
 *
 * Unlinks the tag node whose name matches (case-insensitive) and
 * returns it to the pool. Returns 0 if both lists are empty after
 * removal, 1 otherwise.
 */
int
TagList_RemoveByName(CTagListManager *mgr, const char *name)
{
	TagNode **pp;
	TagNode *node;

	CTagListManager_Lock(mgr);
	pp = &mgr->head;
	while (*pp != NULL) {
		if (strcasecmp((*pp)->name, name) == 0) {
			node = *pp;
			*pp = node->next;
			TagNode_ReturnToPool(node);
			if (mgr->scriptList == NULL && mgr->head == NULL)
				return 0;
			return 1;
		}
		pp = &(*pp)->next;
	}
	return 1;
}

/*
 * 0x004CE5DD - CTagListManager::HasScriptByName
 *
 * Returns 1 if any attached script's class name matches the given
 * name as a prefix (strlen(name) chars, case-insensitive). Skips
 * nodes with NULL or 0xABCD (freed-marker) class name pointers.
 */
int
CTagListManager_HasScriptByName(CTagListManager *mgr, const char *name)
{
	int nameLen;
	ScriptAttachNode *node;
	char *scriptName;

	nameLen = (int)strlen(name);
	CTagListManager_Unlock(mgr);
	node = mgr->scriptList;
	while (node != NULL) {
		if (node->scriptClassPtr != NULL) {
			scriptName = *(char **)node->scriptClassPtr;
			if (scriptName != NULL) {
				if ((uintptr_t)scriptName != 0xABCD) {
					if (strncasecmp(scriptName, name, nameLen) == 0)
						return 1;
				}
			}
		}
		node = node->next;
	}
	return 0;
}

/*
 * 0x004CE662 - CTagListManager::HasScript
 *
 * Returns 1 if any attached script's scriptClassPtr matches the given
 * pointer, 0 otherwise.
 */
int
CTagListManager_HasScript(CTagListManager *mgr, void *scriptClassPtr)
{
	ScriptAttachNode *node;

	CTagListManager_Unlock(mgr);
	node = mgr->scriptList;
	while (node != NULL) {
		if (node->scriptClassPtr == scriptClassPtr)
			return 1;
		node = node->next;
	}
	return 0;
}

/*
 * 0x004CE6A7 - CTagListManager::PrependScript
 *
 * Prepends the script node to the front of the script list.
 */
void
CTagListManager_PrependScript(CTagListManager *mgr, ScriptAttachNode *node)
{
	node->next = mgr->scriptList;
	mgr->scriptList = node;
}

/*
 * 0x004CE6C7 - TagList::SetTag
 *
 * Sets a named tag: if a tag with the given name exists, destroys its
 * old value; otherwise allocates a new node at the head. The value is
 * stored by type (STRING/USTRING/LOC/LIST are deep-copied to heap;
 * INT/OBJ are stored inline). Returns the node.
 */
TagNode *
TagList_SetTag(CTagListManager *mgr, const char *name, int type, uintptr_t value)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (strcasecmp(node->name, name) == 0)
			break;
		node = node->next;
	}

	if (node != NULL) {
		tagnode_destroy_value(node);
	} else {
		node = TagNodePool_AllocA();
		node->next = mgr->head;
		mgr->head = node;
	}

	node->type = (uint32_t)type;
	node->name = (char *)CScriptManager_InternString(&g_ScriptManager, name);
	switch (type) {
	case 1: { // WTYPE_STRING
		CString *newStr = (CString *)OperatorNew(sizeof(CString));
		if (newStr != NULL)
			CString_CopyConstructor(newStr, (CString *)(uintptr_t)value);
		node->value = (uintptr_t)newStr;
		break;
	}
	case 2: { // WTYPE_USTRING
		CUString *newUStr = (CUString *)OperatorNew(sizeof(CUString));
		if (newUStr != NULL)
			CUString_CopyConstructor(newUStr, (CUString *)(uintptr_t)value);
		node->value = (uintptr_t)newUStr;
		break;
	}
	case 3: { // WTYPE_LOC
		CLocation *loc = (CLocation *)OperatorNew(6);
		if (loc != NULL)
			CLocation_Init(loc);
		node->value = (uintptr_t)loc;
		CLocation_CopyFrom((CLocation *)(uintptr_t)node->value, (CLocation *)(uintptr_t)value);
		break;
	}
	case 5: { // WTYPE_LIST
		CList *newList;
		CListNode *walk;
		newList = (CList *)OperatorNew(sizeof(CList));
		if (newList != NULL)
			CList_Constructor(newList);
		node->value = (uintptr_t)newList;
		if ((CList *)(uintptr_t)value != NULL) {
			walk = ((CList *)(uintptr_t)value)->head;
			while (walk != NULL) {
				CList_Append((CList *)(uintptr_t)node->value, walk->typeTag, walk->value);
				walk = walk->next;
			}
		}
		break;
	}
	default:
		// WTYPE_INT, WTYPE_OBJ: store inline
		node->value = value;
		break;
	}
	return node;
}

/*
 * 0x004CEA00 - TagList::HasTag
 *
 * Returns 1 if a tag with the given name and type exists. Type 7
 * (WTYPE_UNKNOWN) is a wildcard that matches any type.
 */
int
TagList_HasTag(CTagListManager *mgr, const char *name, int type)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (strcasecmp(node->name, name) == 0) {
			if (node->type == (uint32_t)type || type == 7)
				return 1;
		}
		node = node->next;
	}
	return 0;
}

/*
 * 0x004CEA63 - TagList::GetTagInt
 *
 * Writes the value of the named INT tag to *outVal, if present.
 */
void
TagList_GetTagInt(CTagListManager *mgr, const char *name, int *outVal)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (node->type == 0) {
			if (strcasecmp(node->name, name) == 0) {
				*outVal = (int)node->value;
				return;
			}
		}
		node = node->next;
	}
}

/*
 * 0x004CEAC2 - TagList::GetTagObj
 *
 * Writes the value of the named OBJ (serial) tag to *outVal, if present.
 */
void
TagList_GetTagObj(CTagListManager *mgr, const char *name, uint32_t *outVal)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (node->type == 4) {
			if (strcasecmp(node->name, name) == 0) {
				*outVal = node->value;
				return;
			}
		}
		node = node->next;
	}
}

/*
 * 0x004CEB21 - TagList::GetTagLoc
 *
 * Copies the CLocation value of the named LOC tag into *outLoc, if present.
 */
void
TagList_GetTagLoc(CTagListManager *mgr, const char *name, CLocation *outLoc)
{
	TagNode *node;
	CLocation *loc;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (node->type == 3) {
			if (strcasecmp(node->name, name) == 0) {
				loc = (CLocation *)(uintptr_t)node->value;
				CLocation_SetLoc(outLoc, loc);
				return;
			}
		}
		node = node->next;
	}
}

/*
 * 0x004CEB84 - TagList::GetTagString
 *
 * Returns the CString value of the named STRING tag, or NULL.
 */
CString *
TagList_GetTagString(CTagListManager *mgr, const char *name)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (node->type == 1) {
			if (strcasecmp(node->name, name) == 0)
				return (CString *)(uintptr_t)node->value;
		}
		node = node->next;
	}
	return NULL;
}

/*
 * 0x004CEBE0 - TagList::GetTagUString
 *
 * Returns the CUString value of the named USTRING tag, or NULL.
 */
char *
TagList_GetTagUString(CTagListManager *mgr, const char *name)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (node->type == 2) {
			if (strcasecmp(node->name, name) == 0)
				return (char *)node->value;
		}
		node = node->next;
	}
	return NULL;
}

/*
 * 0x004CEC3C - CTagListManager::GetListEntry
 *
 * Returns the CList value of the named LIST tag, or NULL.
 */
CList *
CTagListManager_GetListEntry(CTagListManager *mgr, const char *name)
{
	TagNode *node;

	CTagListManager_Lock(mgr);
	node = mgr->head;
	while (node != NULL) {
		if (node->type == 5) {
			if (strcasecmp(node->name, name) == 0)
				return (CList *)(uintptr_t)node->value;
		}
		node = node->next;
	}
	return NULL;
}

/*
 * 0x004CEC98 - CTagListManager::Lock
 *
 * No-op in the binary (stub for a threading primitive).
 */
void
CTagListManager_Lock(CTagListManager *mgr)
{
	USED(mgr);
}

/*
 * 0x004CECA3 - CTagListManager::Unlock
 *
 * No-op in the binary (stub for a threading primitive).
 */
void
CTagListManager_Unlock(CTagListManager *mgr)
{
	USED(mgr);
}

/*
 * Custom - TagList_DestroyPools
 *
 * Server-shutdown cleanup. Walks the tag-list manager freelist
 * marking each node defined so valgrind can read its next pointer
 * (stored in `head`), then ends pool tracking. The VG_* macros are
 * no-ops when VALGRIND is not defined.
 */
void
TagList_DestroyPools(void)
{
	CTagListManager *cur, *next;

	if (g_tagListMgrFreeList == NULL)
		return;
	for (cur = g_tagListMgrFreeList; cur != NULL; cur = next) {
		VG_MAKE_DEFINED(cur, sizeof(*cur));
		next = (CTagListManager *)cur->head;
	}
	VG_DESTROY_POOL(&g_tagListMgrFreeList);
}
