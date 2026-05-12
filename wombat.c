/*
 * Wombat scripting engine - bytecode loader and shared infrastructure.
 *
 * Wombat is the in-engine scripting language used for all NPC, item,
 * and region game logic. This module owns the string database, the
 * tokenizer, script load / cache, and the trigger-fire entry point
 * shared by the compile-time and runtime halves.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "anim.h"
#include "container.h"
#include "dat.h"
#include "io.h"
#include "main.h"
#include "region.h"
#include "weapon.h"

// From location.h (not included directly to avoid CLocation redefinition)
#include "multi.h"
#include "convo.h"
#include "stddeque.h"
#include "taglist.h"
#include "wombat_compile.h"
#include "wombat_stl.h"

static void CFuncList_Constructor(CFuncList *list); // 0x00407B30
static void StaticInit_ScriptManager(void); // 0x004678D2
static void *CScript_ScalarDelete(CScript *this, int flags); // 0x004B9AB0

CScriptStringDB g_ScriptStringDB;

/* Binary: 0x006482A0 - g_ScriptExecContext */
WombatExecCtx *g_CurrentExecCtx;

/*
 * Global scratch: 0x0063D828 in binary.
 * Set by AddVarToScope before calling CNamedScope_Add, cleared after.
 * Used by CNamedScope_Add to walk trigger handler chains for scope patching.
 */
CScript *g_currentCompileScript = NULL;

/*
 * 0x0063E100 - g_ScriptLoadCount
 *
 * Incremented by FindOrLoadScript when recursively loading scripts.
 */
int g_ScriptLoadCount = 0;

/*
 * 0x0063E104 - g_SdbLoaded
 *
 * Counter tracking SDB load state. When 0, LoadScript loads the SDB
 * on first call. Incremented by each LoadScript call.
 */
int g_SdbLoaded = 0;

/*
 * 0x006482A4 - g_ScriptRecursionDepth
 *
 * Current depth of nested script invocations; the interpreter aborts
 * when it exceeds 0x40 (64) to prevent runaway recursion.
 */
int g_ScriptRecursionDepth = 0;

/*
 * Per-event trigger parameter table.
 *
 * Extracted from the binary's ParseTrigger (0x00427436) switch. The
 * original uses a 68-entry dispatch byte table at 0x00427EAB to pick
 * from a 45-entry jump table at 0x00427DF7; each handler calls
 * AddVarToScope with the trigger's variables and types.
 *
 * Type codes: 0=int, 1=string, 2=ustring, 3=loc, 4=obj, 5=list
 *
 * Each entry below lists the trigger variables registered for that event
 * during parsing. These become the trigger scope variables available to
 * the script body.
 */

/* Maximum trigger parameters per event (binary max is 4 for genericgump) */

const TriggerParamTableEntry g_TriggerParamTable[TRIGGER_EVENT_COUNT] = {
	/* 0x00 speech */ { 2, { { "speaker", 4 }, { "arg", 1 } } },
	/* 0x01 gotattacked */ { 1, { { "attacker", 4 } } },
	/* 0x02 killedtarget */ { 1, { { "attacker", 4 } } },
	/* 0x03 aversion */ { 1, { { "target", 4 } } },
	/* 0x04 death */ { 2, { { "attacker", 4 }, { "corpse", 4 } } },
	/* 0x05 sawdeath */ { 3, { { "attacker", 4 }, { "victim", 4 }, { "corpse", 4 } } },
	/* 0x06 fightpulse */ { 1, { { "target", 4 } } },
	/* 0x07 washit */ { 2, { { "attacker", 4 }, { "damamt", 0 } } },
	/* 0x08 failfood */ { 0, { { 0 } } },
	/* 0x09 faildesire */ { 0, { { 0 } } },
	/* 0x0A failshelter */ { 0, { { 0 } } },
	/* 0x0B foundfood */ { 1, { { "target", 4 } } },
	/* 0x0C founddesire */ { 1, { { "target", 4 } } },
	/* 0x0D foundshelter */ { 1, { { "target", 4 } } },
	/* 0x0E time */ { 0, { { 0 } } },
	/* 0x0F creation */ { 0, { { 0 } } },
	/* 0x10 enterrange */ { 1, { { "target", 4 } } },
	/* 0x11 leaverange */ { 1, { { "target", 4 } } },
	/* 0x12 loiter */ { 0, { { 0 } } },
	/* 0x13 seekfood */ { 0, { { 0 } } },
	/* 0x14 seekdesire */ { 0, { { 0 } } },
	/* 0x15 seekshelter */ { 0, { { 0 } } },
	/* 0x16 message */ { 2, { { "sender", 4 }, { "args", 5 } } },
	/* 0x17 use */ { 1, { { "user", 4 } } },
	/* 0x18 targetobj */ { 2, { { "user", 4 }, { "usedon", 4 } } },
	/* 0x19 targetloc */ { 3, { { "user", 4 }, { "place", 3 }, { "objtype", 0 } } },
	/* 0x1A weather */ { 0, { { 0 } } },
	/* 0x1B wasdropped */ { 1, { { "dropper", 4 } } },
	/* 0x1C lookedat */ { 1, { { "looker", 4 } } },
	/* 0x1D give */ { 2, { { "giver", 4 }, { "givenobj", 4 } } },
	/* 0x1E wasgotten */ { 1, { { "getter", 4 } } },
	/* 0x1F pathfound */ { 0, { { 0 } } },
	/* 0x20 pathnotfound */ { 0, { { 0 } } },
	/* 0x21 callback */ { 0, { { 0 } } },
	/* 0x22 ishitting */ { 2, { { "victim", 4 }, { "damamt", 0 } } },
	/* 0x23 convofunc */ { 2, { { "talker", 4 }, { "arg", 1 } } },
	/* 0x24 typeselected */ { 4, { { "user", 4 }, { "listindex", 0 }, { "objtype", 0 }, { "objhue", 0 } } },
	/* 0x25 hueselected */ { 2, { { "user", 4 }, { "objhue", 0 } } },
	/* 0x26 moon */ { 2, { { "trammelchange", 0 }, { "feluccachange", 0 } } },
	/* 0x27 minrangeattack */ { 1, { { "defender", 4 } } },
	/* 0x28 minrangedefend */ { 1, { { "attacker", 4 } } },
	/* 0x29 maxrangeattack */ { 1, { { "defender", 4 } } },
	/* 0x2A maxrangedefend */ { 1, { { "attacker", 4 } } },
	/* 0x2B destroyed */ { 0, { { 0 } } },
	/* 0x2C equip */ { 1, { { "equippedon", 4 } } },
	/* 0x2D unequip */ { 1, { { "unequippedfrom", 4 } } },
	/* 0x2E isstackableon */ { 1, { { "stackon", 4 } } },
	/* 0x2F stackonto */ { 1, { { "stackon", 4 } } },
	/* 0x30 multirecycle */ { 2, { { "oldtype", 0 }, { "newtype", 0 } } },
	/* 0x31 decay */ { 2, { { "oldvalue", 0 }, { "newvalue", 0 } } },
	/* 0x32 serverswitch */ { 0, { { 0 } } },
	/* 0x33 ooruse */ { 1, { { "user", 4 } } },
	/* 0x34 acquiredesire */ { 1, { { "target", 4 } } },
	/* 0x35 logout */ { 0, { { 0 } } },
	/* 0x36 objectloaded */ { 0, { { 0 } } },
	/* 0x37 genericgump */ { 4, { { "user", 4 }, { "closeId", 0 }, { "selectList", 5 }, { "entryList", 5 } } },
	/* 0x38 oortargetobj */ { 2, { { "user", 4 }, { "usedon", 4 } } },
	/* 0x39 pkpost */ { 2, { { "killer", 4 }, { "killee", 4 } } },
	/* 0x3A textentry */ { 3, { { "sender", 4 }, { "button", 0 }, { "text", 1 } } },
	/* 0x3B shop */ { 1, { { "func", 0 } } },
	/* 0x3C stolenfrom */ { 1, { { "stealer", 4 } } },
	/* 0x3D objaccess */ { 2, { { "user", 4 }, { "usedon", 4 } } },
	/* 0x3E ishealthy */ { 0, { { 0 } } },
	/* 0x3F online */ { 0, { { 0 } } },
	/* 0x40 transaccountcheck */ { 2, { { "target", 4 }, { "transok", 0 } } },
	/* 0x41 transresponse */ { 2, { { "target", 4 }, { "transok", 0 } } },
	/* 0x42 canbuy */ { 3, { { "buyer", 4 }, { "seller", 4 }, { "quantity", 0 } } },
	/* 0x43 mobishitting */ { 2, { { "victim", 4 }, { "damage", 0 } } },
	/* 0x44 famechanged */ { 0, { { 0 } } },
	/* 0x45 karmachanged */ { 0, { { 0 } } },
	/* 0x46 murdercountchanged */ { 0, { { 0 } } },
};

/*
 * 0x0063D840 - Global array table
 *
 * Binary: std::map<int, CArray*> (StdMapTree).
 */
StdMapTree g_WombatArrays;
uint8_t g_WombatArraysDestructorFlag; // 0x0063D850

/*
 * 0x0063D854 / 0x0063D858 - Script return value globals
 *
 * Hold the integer return value set by a script's return statement and
 * the flag that indicates the value has been set; read by the caller
 * after dispatch returns.
 */
int g_ScriptReturnValue;
int g_ScriptReturnFlag;

/*
 * 0x00644998 - Conversation return string buffer
 *
 * Set by setConvoRet handler, read by conversation dispatch.
 * Binary: standalone global char[256] at 0x00644998.
 */
char g_ConvoReturnStr[256];

/*
 * Hint/Rumor System (binary: g_HintSystem at 0x00698E90)
 *
 * Stores information about interesting items in the world (magic artifacts,
 * etc.) that NPCs can relay as rumors/hints to players. The system is
 * populated by updateHint() and queried by getHint().
 *
 * Binary uses a linked list with hash lookup; we use a simple array.
 */
CHintEntry *g_HintEntries;
int g_NumHints;
int g_HintCapacity;

int g_TrigCmdDepth;   /* 0x0063D860 */
int g_TrigCmdRecurse; /* 0x0063D864 */

int g_GMCallStatus; /* 0x006999AC */

/*
 * 0x00698840 - g_AnimSequence
 *
 * Binary: global animation sequence queue. When active (state=1),
 * effect/animation builtins queue commands instead of broadcasting
 * immediately. endSequence() processes all queued commands as a batch,
 * framed by SEQUENCE(0) and SEQUENCE(actionId) packets.
 *
 * Functions:
 *   beginSequence (0x00418AAF): sets state = 1
 *   endSequence   (0x00418AC9): calls Process + Clear
 *   Process       (0x004CF68E): sends SEQUENCE(0), processes commands, sends SEQUENCE(actionId)
 *   Clear         (0x004D0099): frees lists, sets state = 0
 *   AddLocation   (0x004D013D): adds broadcast location (deduped)
 *   AddCommand    (0x004D024D): queues a command node
 */

AnimSequence g_AnimSequence; /* 0x00698840 */

/*
 * 0x0040107D - CScriptStringDB::Load
 *
 * Reloads the DB from path: clears the existing string vector then
 * reads each line (stripping trailing CR/LF) and appends it as a
 * CSdbStr entry. Returns 1 when the file cannot be opened, 0 on
 * success.
 */
int
CScriptStringDB_Load(CScriptStringDB *db, const char *path)
{
	FILE *f;
	char line[1024];
	int len;
	CSdbStr temp;

	f = fopen_ServerSide((char *)path, "r");
	if (f == NULL)
		return 1;

	CScriptStringDB_Free(db);

	while (!feof_ServerSide(f)) {
		fgets_ServerSide(line, 0x400, f);

		// FIXED: binary reads line[-1] when len==0 (harmless no-op
		// on Windows stack, but ASAN catches the UB).
		len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		len = strlen(line);
		if (len > 0 && line[len - 1] == '\r')
			line[len - 1] = '\0';

		CSdbStr_Init(&temp, line);
		CScriptStringDB_PushBack(db, &temp);
		CSdbStr_Destructor(&temp);
	}

	fclose_ServerSide(f);
	return 0;
}

/*
 * 0x00401381 - CScriptStringDB::Get
 *
 * Returns the C-string for the index-th SDB entry. The caller must
 * ensure index is in range; no bounds check is performed.
 */
const char *
CScriptStringDB_Get(CScriptStringDB *db, int index)
{
	return CSdbStr_c_str((CSdbStr *)CSdbStrVector_At(db, (uint32_t)index));
}

/*
 * 0x00403980 - EventRingBuffer::Count
 *
 * Returns the current number of buffered events.
 */
uint32_t
EventRingBuffer_Count(void)
{
	return g_eventRingBuffer.size;
}

/*
 * 0x00407B30 - CFuncList::CFuncList (CFuncList constructor)
 *
 * Initialises an empty function list (count and array set to zero).
 */
static void
CFuncList_Constructor(CFuncList *list)
{
	list->count = 0;
	list->array = NULL;
}

/*
 * 0x00407B51 - CFuncList::~CFuncList (CFuncList destructor)
 *
 * Frees the function array.
 */
void
CFuncList_Destructor(CFuncList *list)
{
	if (list->array != NULL)
		OperatorDelete(list->array);
}

/*
 * 0x00407B7C - CFuncList::Copy (CFuncList copy)
 *
 * Replaces this list's contents with a copy of src's function array.
 */
void
CFuncList_Copy(CFuncList *dst, const CFuncList *src)
{
	// Free existing array
	if (dst->array != NULL)
		OperatorDelete(dst->array);

	// Copy count
	dst->count = src->count;

	// Allocate and copy entries
	if (dst->count > 0) {
		size_t sz = (size_t)dst->count * sizeof(CFunction);
		dst->array = OperatorNew(sz);
		memcpy(dst->array, src->array, sz);
	}
}

/*
 * 0x00407BF1 - PatchScopeRefs
 *
 * Recursively walks a ResultNode chain and patches value pointers that fall
 * within the old entries array to the corresponding offset inside the new
 * entries array. Only types 2-5 (var-ref types) are patched. Used after a
 * CNamedScope reallocation.
 */
void
PatchScopeRefs(ResultNode *node, void *oldEntries, void *newEntries, int count)
{
	while (node != NULL) {
		// Check if this node type is a var ref (types 2,3,4,5)
		if (node->type == 3 || node->type == 2 || node->type == 5 || node->type == 4) {
			// Check if value ptr falls within old entries range
			uintptr_t oldBase = (uintptr_t)oldEntries;
			uintptr_t oldEnd = oldBase + (count - 1) * sizeof(CNamedScopeEntry);
			uintptr_t val = node->value;

			if (val >= oldBase && val <= oldEnd) {
				// Patch: compute offset within old, apply to new
				uintptr_t off = val - oldBase;
				node->value = (uintptr_t)newEntries + off;
			}
		}

		// Recurse on child chain (node->extra at offset 0x0C)
		PatchScopeRefs((ResultNode *)(uintptr_t)node->extra, oldEntries, newEntries, count);

		// Iterate: node = node->next (offset 0x08)
		node = node->next;
	}
}

/*
 * 0x00407C7A - CFuncList::AddEntry (CFuncList::AddEntry)
 *
 * Returns the function entry for (name, sig), creating it on first
 * use. Re-registering name with a different signature is reported
 * as an overload conflict and returns NULL. New entries take an SDB
 * pointer for name and a malloc'd copy of sig, with the cumulative
 * parameter size computed from the signature.
 */
CFunction *
CFuncList_AddEntry(CFuncList *list, const char *name, const char *sig)
{
	CFunction *funcs;
	CFunction *entry;
	int i;
	const char *p;

	funcs = (CFunction *)list->array;

	// Search for existing entry by name
	for (i = 0; i < list->count; i++) {
		if (strcmp(name, funcs[i].name) == 0)
			break;
	}

	if (i < list->count) {
		// Found existing entry - check sig match
		entry = &funcs[i];
		if (strcmp(sig, entry->sig) != 0) {
			// Signature mismatch - overload error
			char buf[264];
			sprintf(buf, "overloaded function '%s' first seen as '%s', now seen as '%s'.", name, entry->sig, sig);
			return NULL;
		}
		return entry;
	}

	// Not found - grow array
	{
		void *newArr = OperatorNew((size_t)(list->count + 1) * sizeof(CFunction));

		if (list->count > 0) {
			memcpy(newArr, list->array, (size_t)list->count * sizeof(CFunction));
			OperatorDelete(list->array);
		}
		list->array = newArr;
	}

	// Fill new entry at position [count]
	funcs = (CFunction *)list->array;
	entry = &funcs[list->count];
	entry->name = (char *)name;
	entry->scope = NULL;

	// Copy sig string
	entry->sig = (char *)OperatorNew(strlen(sig) + 1);
	strcpy(entry->sig, sig);

	// Initialize field0C, then accumulate parameter sizes from sig
	entry->paramSize = NULL;
	p = sig + 1;
	while (*p != '\0') {
		int typeId = SigCharToTypeId(*p);
		entry->paramSize = (void *)((uintptr_t)entry->paramSize + (uintptr_t)g_WombatTypeSizes[typeId]);
		p++;
	}

	list->count++;
	return &funcs[list->count - 1];
}

/*
 * 0x00407EEF - CFuncList::FindFunc (FindFunc)
 *
 * Linear-scans the function array for an entry whose name matches.
 * On a match, stores the index in *outIndex (when non-NULL) and
 * returns the entry pointer; returns NULL otherwise.
 */
CFunction *
CFuncList_FindFunc(CFuncList *list, const char *name, int *outIndex)
{
	CFunction *funcs = (CFunction *)list->array;
	int i;

	for (i = 0; i < list->count; i++) {
		if (strcmp(funcs[i].name, name) == 0) {
			if (outIndex != NULL)
				*outIndex = i;
			return &funcs[i];
		}
	}
	return NULL;
}

/*
 * 0x00407F5D - CNamedScope::CNamedScope (CNamedScope constructor)
 *
 * Initialises an empty named scope.
 */
void
CNamedScope_Constructor(CNamedScope *scope)
{
	scope->count = 0;
	scope->entries = NULL;
	scope->totalSize = 0;
}

/*
 * 0x00407F88 - CNamedScope::~CNamedScope (CNamedScope destructor)
 *
 * Frees the entries array.
 */
void
CNamedScope_Destructor(CNamedScope *scope)
{
	if (scope->entries != NULL)
		OperatorDelete(scope->entries);
}

/*
 * 0x00407FB3 - CNamedScope::Copy (CNamedScope copy)
 *
 * Replaces this scope's contents with a copy of src's entries and
 * total size.
 */
void
CNamedScope_Copy(CNamedScope *dst, const CNamedScope *src)
{
	// Free existing entries
	if (dst->entries != NULL)
		OperatorDelete(dst->entries);

	// Copy count
	dst->count = src->count;

	// Allocate and copy entries
	if (dst->count > 0) {
		size_t sz = (size_t)dst->count * sizeof(CNamedScopeEntry);
		dst->entries = OperatorNew(sz);
		memcpy(dst->entries, src->entries, sz);
		// Copy totalSize
		dst->totalSize = src->totalSize;
	}
}

/*
 * 0x00408034 - CNamedScope::Add (CNamedScope::Add)
 *
 * Adds a new (name, typeId) entry to the scope. Returns 1 when a
 * duplicate name exists. On success, reallocates the entries array,
 * updates the in-flight script's function and trigger scope
 * references, and grows totalSize by the type's aligned size.
 */
int
CNamedScope_Add(CNamedScope *scope, const char *name, int typeId, CFuncList *funcList)
{
	int i;
	char *newArr;
	int alignedSize;

	// Step 1: Check for duplicate
	for (i = 0; i < scope->count; i++) {
		char *entryName = ((CNamedScopeEntry *)scope->entries)[i].name;
		if (strcmp(name, entryName) == 0)
			break;
	}
	if (i < scope->count)
		return 1; /* duplicate */

	// Step 2: Allocate new entries array
	newArr = (char *)OperatorNew((scope->count + 1) * sizeof(CNamedScopeEntry));

	// Step 3: Copy old entries and patch scope references
	if (scope->count > 0) {
		memcpy(newArr, scope->entries, scope->count * sizeof(CNamedScopeEntry));

		// 3a: Walk funcList, patch scope refs in each function's scope
		for (i = 0; i < funcList->count; i++) {
			CFunction *func = (CFunction *)((char *)funcList->array + i * sizeof(CFunction));
			CFuncScope *fscope = (CFuncScope *)func->scope;
			if (fscope != NULL) {
				PatchScopeRefs((ResultNode *)fscope->bodyStream, scope->entries, newArr, scope->count);
			}
		}

		// 3b: Walk trigger handler chains in current compile script
		if (g_currentCompileScript != NULL) {
			for (i = 0; i < BINARY_TRIGGER_COUNT; i++) {
				CTrigger *trig = (CTrigger *)g_currentCompileScript->trigHandlers[i];
				while (trig != NULL) {
					if (trig->scope != NULL) {
						PatchScopeRefs((ResultNode *)trig->scope->bodyStream, scope->entries, newArr, scope->count);
					}
					trig = trig->next;
				}
			}
		}

		// Free old entries
		OperatorDelete(scope->entries);
	}

	// Step 4: Fill in new entry at index scope->count
	{
		CNamedScopeEntry *entry = (CNamedScopeEntry *)(newArr + scope->count * sizeof(CNamedScopeEntry));
		entry->name = (char *)name;
		entry->typeId = typeId;
		entry->offset = scope->totalSize;
	}

	// Step 5: Update entries pointer
	scope->entries = newArr;

	// Step 6: Add aligned type size to totalSize
	alignedSize = (g_WombatTypeSizes[typeId] + 3) & ~3; /* 4-byte align */
	scope->totalSize += alignedSize;

	// Step 7: Increment count
	scope->count++;

	return 0;
}

/*
 * 0x0040821E - CScope::FindVar
 *
 * Walks the variable array comparing names with strcmp (0x004E8910).
 * Returns pointer to the matching WombatVar, or NULL.
 */
WombatVar *
CScope_FindVar(WombatScope *scope, const char *name)
{
	int i;

	for (i = 0; i < scope->count; i++) {
		if (strcmp(scope->vars[i].name, name) == 0)
			return &scope->vars[i];
	}
	return NULL;
}

/*
 * 0x0040821E - CNamedScope::FindVar (CScope::FindVar)
 *
 * Returns the entry whose name matches, or NULL when none does.
 */
void *
CNamedScope_FindVar(CNamedScope *scope, const char *name)
{
	int i;
	CNamedScopeEntry *entries = (CNamedScopeEntry *)scope->entries;

	for (i = 0; i < scope->count; i++) {
		if (strcmp(entries[i].name, name) == 0)
			return &entries[i];
	}
	return NULL;
}

/*
 * 0x0040827D - InitNameEntry
 *
 * Initialises a name-node by storing a heap copy of the name
 * string. Used for CFunction and CTrigger linked-list nodes.
 */
CNameNode *
InitNameEntry(CNameNode *entry, const char *name)
{
	size_t len = strlen(name) + 1;

	entry->name = (char *)OperatorNew(len);
	strcpy(entry->name, name);
	return entry;
}

/*
 * 0x004082BE - CNameNode::~CNameNode (CName destructor)
 *
 * Frees the heap-allocated name string held by an 8-byte name node.
 */
void
CNameNode_Destructor(CNameNode *node)
{
	if (node->name != NULL)
		OperatorDelete(node->name);
}

/*
 * 0x004082E9 - InitVarEntry
 *
 * Initialises a CMember linked-list node from name and typeId,
 * storing a heap copy of the name.
 */
CMemberNode *
InitVarEntry(CMemberNode *entry, const char *name, int typeId)
{
	size_t len = strlen(name) + 1;

	entry->name = (char *)OperatorNew(len);
	strcpy(entry->name, name);
	entry->typeId = typeId;
	return entry;
}

/*
 * 0x00408333 - CMemberNode::~CMemberNode (CMember destructor)
 *
 * Frees the name string held by a CMember node.
 */
void
CMemberNode_Destructor(CMemberNode *node)
{
	if (node->name != NULL)
		OperatorDelete(node->name);
}

/*
 * 0x0040835E - InitTrigger (CTrigger constructor)
 *
 * Initialises a CTrigger from (filter, eventIndex, flags, filterStr).
 * The filter string is stored according to the event: strdup'd for
 * string-filter events, parsed via atoi for numeric-filter events,
 * and NULL for the remaining events.
 */
CTrigger *
InitTrigger(CTrigger *trig, int filter, int eventIndex, int flags, const char *filterStr)
{
	trig->filter = filter;
	trig->flags = flags;
	trig->eventIndex = eventIndex;

	/*
	 * Dispatch filter data handling based on event index.
	 * Binary has a 62-entry byte dispatch table and 11-entry jump table
	 * that maps to three handlers: strdup, atoi, or NULL. */
	switch (eventIndex) {
	// String filter: strdup the filterStr
	case 0:
	case 14:
	case 22:
	case 35:
		trig->filterData = OperatorNew(strlen(filterStr) + 1);
		strcpy((char *)trig->filterData, filterStr);
		break;

	// Numeric filter: atoi the filterStr
	case 16:
	case 17:
	case 31:
	case 32:
	case 33:
	case 36:
	case 37:
	case 55:
	case 58:
	case 61:
		trig->filterData = (void *)(intptr_t)atoi(filterStr);
		break;

	// No filter data
	default:
		trig->filterData = NULL;
		break;
	}

	trig->scope = NULL;
	return trig;
}

/*
 * 0x0040846D - CTrigger::~CTrigger (CTrigger destructor)
 *
 * Frees filterData only for the events that allocated a string
 * filter; numeric-filter events store an integer in the pointer
 * field and do not need freeing.
 */
void
CTrigger_Destructor(CTrigger *trig)
{
	if (trig->eventIndex > 35)
		return;

	switch (trig->eventIndex) {
	case 0:
	case 14:
	case 22:
	case 35:
		// These events used strdup - free the malloc'd string
		OperatorDelete(trig->filterData);
		break;
	default:
		// atoi events and NULL events: nothing to free
		break;
	}
}

/*
 * 0x004084E8 - CScript::CScript (CScript constructor)
 *
 * Initialises a script: stores a heap copy of name, constructs the
 * embedded CFuncList and CNamedScope, and clears every per-trigger
 * handler slot and bookkeeping field.
 */
void
CScript_Constructor(CScript *script, const char *name)
{
	int i;
	size_t len;

	// Init function list at +0x04
	CFuncList_Constructor(&script->funcList);

	// Init named scope at +0x0C
	CNamedScope_Constructor(&script->namedScope);

	// Copy name
	len = strlen(name) + 1;
	script->name = (char *)OperatorNew(len);
	strcpy(script->name, name);

	// Zero linked list heads
	script->funcListHead = NULL;
	script->trigListHead = NULL;
	script->memberListHead = NULL;
	script->parent = NULL;
	script->nextLoaded = NULL;

	// Zero trigger handler array (71 entries)
	for (i = 0; i < BINARY_TRIGGER_COUNT; i++)
		script->trigHandlers[i] = NULL;
}

/*
 * 0x004085D9 - CScript::~CScript (CScript destructor)
 *
 * Tears down a script: frees the name, walks and frees the func,
 * trigger, and member node lists, releases each owned trigger
 * handler chain (stopping at any parent's chain head so inherited
 * triggers aren't freed twice), poisons the bookkeeping fields,
 * and destructs the embedded scope and func list.
 */
void
CScript_Destructor(CScript *script)
{
	void *node, *next;
	int i;

	// Step 0: Walk g_scriptInstanceListHead (empty traversal)
	{
		void *inst = g_scriptInstanceListHead;
		while (inst != NULL)
			inst = *(void **)((char *)inst + 0x14);
	}

	// Step 1: Free name string
	OperatorDelete(script->name);

	// Step 2: Walk funcListHead (0x134) - just OperatorDelete each node
	node = script->funcListHead;
	while (node != NULL) {
		next = *(void **)node; /* node->next at offset 0 */
		OperatorDelete(node);
		node = next;
	}

	// Step 3: Walk trigListHead (0x138) - CNameNode_ScalarDtor
	{
		CNameNode *nameNode = (CNameNode *)script->trigListHead;
		while (nameNode != NULL) {
			CNameNode *nameNext = nameNode->next;
			CNameNode_ScalarDtor(nameNode, 1);
			nameNode = nameNext;
		}
	}

	// Step 4: Walk memberListHead - CMemberNode_ScalarDtor
	{
		CMemberNode *memNode = (CMemberNode *)script->memberListHead;
		while (memNode != NULL) {
			CMemberNode *memNext = memNode->next;
			CMemberNode_ScalarDtor(memNode, 1);
			memNode = memNext;
		}
	}

	/*
	 * Step 5: Walk all 71 trigHandler slots.
	 * Stop at parent's chain head - inherited triggers belong to parent. */
	for (i = 0; i < BINARY_TRIGGER_COUNT; i++) {
		void *parentHead;

		if (script->parent != NULL) {
			CScript *parent = script->parent;
			parentHead = parent->trigHandlers[i];
		} else {
			parentHead = NULL;
		}

		node = script->trigHandlers[i];
		while (node != parentHead) {
			next = (void *)((CTrigger *)node)->next;
			if (node != NULL)
				CTrigger_ScalarDtor((CTrigger *)node, 1);
			node = next;
		}
	}

	// Step 6: Poison debug markers
	script->name = (char *)(intptr_t)0xABCD;
	script->trigListHead = (void *)(intptr_t)0xABCD;
	script->memberListHead = (void *)(intptr_t)0xABCD;

	// Step 7: Destroy namedScope at +0x0C
	CNamedScope_Destructor(&script->namedScope);

	// Step 8: Destroy funcList at +0x04
	CFuncList_Destructor(&script->funcList);
}

/*
 * 0x0040887A - CScript::AddFunction (CScript::AddFunction)
 *
 * Records the function name in the script's name list and adds it
 * to the embedded CFuncList. Returns the new (or existing) entry
 * pointer, or NULL when AddEntry rejects the signature.
 */
CFunction *
CScript_AddFunction(CScript *script, const char *name, const char *sig)
{
	CFunction *result;

	// Allocate name node: 2 pointers (next + name)
	CNameNode *node = (CNameNode *)OperatorNew(sizeof(CNameNode));
	if (node != NULL)
		InitNameEntry(node, name);

	// Insert at head of trigListHead (0x138) linked list
	node->next = (CNameNode *)script->trigListHead;
	script->trigListHead = node;

	// Set g_currentCompileScript before AddEntry
	g_currentCompileScript = script;

	// Pass node->name (the strdup'd copy) to AddEntry
	result = CFuncList_AddEntry(&script->funcList, node->name, sig);

	// Clear g_currentCompileScript
	g_currentCompileScript = NULL;

	return result;
}

/*
 * 0x00408936 - CScript::FindFunction (CScript::FindFunction)
 *
 * Looks up a function by name in the script's CFuncList.
 */
CFunction *
CScript_FindFunction(CScript *script, const char *name)
{
	return CFuncList_FindFunc(&script->funcList, name, NULL);
}

/*
 * 0x00408954 - CScript::AddVar (CScript::AddVar / RegisterDecl)
 *
 * Registers a member variable (name, typeId) in the script's named
 * scope and threads the matching CMemberNode onto the member list.
 * Returns 0 on success, 1 on duplicate name.
 */
int
CScript_AddVar(CScript *script, const char *name, int typeId)
{
	CMemberNode *node;
	int ret;

	// Allocate 0xC-byte member node (new CMember(name, typeId))
	node = (CMemberNode *)OperatorNew(sizeof(CMemberNode));
	if (node != NULL)
		InitVarEntry(node, name, typeId);

	// Set g_currentCompileScript before CNamedScope_Add
	g_currentCompileScript = script;

	// Pass node->name (the strdup'd copy) to CNamedScope_Add
	ret = CNamedScope_Add(&script->namedScope, node->name, typeId, &script->funcList);

	if (ret == 0) {
		// Success: link into memberListHead (0x13C)
		node->next = (CMemberNode *)script->memberListHead;
		script->memberListHead = node;
		g_currentCompileScript = NULL;
		return 0;
	} else {
		// Failure (duplicate): clear global, dtor+free node
		g_currentCompileScript = NULL;
		if (node != NULL)
			CMemberNode_ScalarDtor(node, 1);
		return 1;
	}
}

/*
 * 0x00408A54 - CScript::AddTrigger (CScript::AddTrigger)
 *
 * Allocates and initialises a CTrigger node and threads it onto
 * trigHandlers[eventIndex]. Returns the new node.
 */
CTrigger *
CScript_AddTrigger(CScript *script, int filter, int flags, int eventIndex, const char *filterStr)
{
	CTrigger *node;

	// Allocate 0x18-byte trigger node
	node = (CTrigger *)OperatorNew(sizeof(CTrigger));
	if (node != NULL)
		node = InitTrigger(node, filter, eventIndex, flags, filterStr);
	else
		node = NULL;

	// Link into trigHandlers[eventIndex] chain at head
	node->next = (CTrigger *)script->trigHandlers[eventIndex];
	script->trigHandlers[eventIndex] = node;

	return node;
}

/*
 * 0x00408AF3 - CScriptVar::AppendName
 *
 * Thiscall on CScriptVar (first field is a char* name pointer), 1 stack
 * arg (CString* dest). If name is non-NULL, appends it to dest via
 * CString_AppendCStr. Returns 1 if name was non-NULL, 0 otherwise.
 */
int
CScriptVar_AppendName(CScriptVar *this, CString *dest)
{
	if (this->name != NULL)
		CString_AppendCStr(dest, this->name);

	return (this->name != NULL) ? 1 : 0;
}

/*
 * 0x00408B30 - CNameNode::~CNameNode (scalar deleting destructor)
 *
 * Runs the destructor and frees the object when flags&1 is set.
 */
CNameNode *
CNameNode_ScalarDtor(CNameNode *this, int flags)
{
	CNameNode_Destructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x00408B60 - CMemberNode::~CMemberNode (scalar deleting destructor)
 *
 * Runs the destructor and frees the object when flags&1 is set.
 */
CMemberNode *
CMemberNode_ScalarDtor(CMemberNode *this, int flags)
{
	CMemberNode_Destructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x00408B90 - CTrigger::~CTrigger (scalar deleting destructor)
 *
 * Runs the destructor and frees the object when flags&1 is set.
 */
CTrigger *
CTrigger_ScalarDtor(CTrigger *this, int flags)
{
	CTrigger_Destructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return this;
}

/*
 * 0x00408BC0 - CScope::CScope (CScope constructor)
 *
 * Allocates an initial 256-byte data buffer and zeroes the
 * three child-tracking arrays.
 */
void
CScope_Constructor(CScope *scope)
{
	scope->capacity = 0x100;
	scope->data = (char *)OperatorNew(0x100);
	scope->usedBytes = 0;
	scope->children1 = NULL;
	scope->childCount1 = 0;
	scope->childCapacity1 = 0;
	scope->children2 = NULL;
	scope->childCount2 = 0;
	scope->childCapacity2 = 0;
	scope->children3 = NULL;
	scope->childCount3 = 0;
	scope->childCapacity3 = 0;
}

/*
 * 0x00408C4E - CScope::~CScope (CScope destructor)
 *
 * Frees the data buffer and destroys the CString and CList child
 * objects tracked in children1 / children3. The binary leaks the
 * CUString children2 array; reproduced exactly.
 */
void
CScope_Destructor(CScope *scope)
{
	int i;

	// Step 1: Free data buffer
	OperatorDelete(scope->data);

	// Step 2: Destroy children1 array (CString, 0x0C/0x10)
	for (i = 0; i < scope->childCount1; i++) {
		if (scope->children1[i] != NULL)
			CString_ScalarDelete((CString *)scope->children1[i], 1);
	}
	if (scope->children1 != NULL)
		OperatorDelete(scope->children1);

	// Step 3: Destroy children3 array (CList/Result5, 0x24/0x28)
	for (i = 0; i < scope->childCount3; i++) {
		if (scope->children3[i] != NULL)
			CList_ScalarDelete((CList *)scope->children3[i], 1);
	}
	if (scope->children3 != NULL)
		OperatorDelete(scope->children3);
}

/*
 * 0x00408D6A - CScope::Append (CScope::Append)
 *
 * Appends size bytes from source to the end of the scope buffer,
 * growing the backing storage when needed. Distinct from PushValue
 * (insert-before-tail) and StoreValue (pop-into-dest).
 */
void
CScope_Append(CScope *scope, const void *source, int size)
{
	int aligned = (size + 3) & ~3;

	if (scope->usedBytes + aligned > scope->capacity) {
		// Need to grow
		int newCap = (scope->usedBytes + aligned + 0x3F) & ~0x3F;
		char *newBuf = OperatorNew(newCap);

		// Copy existing data
		memcpy(newBuf, scope->data, scope->usedBytes);

		// Write new data at end
		memcpy(newBuf + scope->usedBytes, source, size);

		// Free old buffer, update
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
		scope->usedBytes += aligned;
	} else {
		// Enough space - just write at end
		memcpy(scope->data + scope->usedBytes, source, size);
		scope->usedBytes += aligned;
	}
}

/*
 * 0x00408E51 - CScope::Resize (CScope::Resize)
 *
 * Reserves newSize additional bytes in the scope (rounded up to 4),
 * zero-fills them, and advances usedBytes. The buffer is grown to
 * the next 64-byte boundary when capacity is exceeded.
 */
void
CScope_Resize(CScope *scope, int newSize)
{
	int aligned;
	int newCap;
	char *newBuf;

	if (newSize == 0)
		return;

	aligned = (newSize + 3) & ~3; /* align to 4 bytes */

	if (scope->usedBytes + aligned > scope->capacity) {
		// Grow: align new capacity to 64-byte boundary
		newCap = (scope->usedBytes + aligned + 0x3F) & ~0x3F;
		newBuf = (char *)OperatorNew(newCap);
		memcpy(newBuf, scope->data, scope->usedBytes);
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
	}

	// Zero the new area
	memset(scope->data + scope->usedBytes, 0, aligned);
	scope->usedBytes += aligned;
}

/*
 * 0x00408F12 - CScope::PushDefaultString
 *
 * Pushes an empty heap-allocated CString onto the scope and
 * records it in children1 so the destructor will free it.
 */
void
CScope_PushDefaultString(CScope *scope)
{
	CString *str;

	int ptrSize = sizeof(void *);
	// Grow scope buffer if needed
	if (scope->usedBytes + ptrSize > scope->capacity) {
		int newCap = (scope->usedBytes + ptrSize - 1 + 0x40) & ~0x3F;
		char *newBuf = OperatorNew(newCap);
		memcpy(newBuf, scope->data, scope->usedBytes);
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
	}

	// Allocate and construct CString
	str = (CString *)OperatorNew(sizeof(CString));
	if (str != NULL)
		CString_Constructor(str, "");

	// Append pointer to scope buffer
	memcpy(scope->data + scope->usedBytes, &str, ptrSize);
	scope->usedBytes += ptrSize;

	// Track in children1 array
	if (scope->childCount1 == scope->childCapacity1) {
		scope->childCapacity1 += 8;
		void **newArr = OperatorNew(scope->childCapacity1 * sizeof(void *));
		if (scope->childCount1 > 0) {
			memcpy(newArr, scope->children1, scope->childCount1 * sizeof(void *));
			OperatorDelete(scope->children1);
		}
		scope->children1 = newArr;
	}
	scope->children1[scope->childCount1] = str;
	scope->childCount1++;
}

/*
 * 0x004090B3 - CScope::PushDefaultUString
 *
 * Pushes an empty heap-allocated CUString onto the scope and
 * records it in children2 so the destructor will free it.
 */
void
CScope_PushDefaultUString(CScope *scope)
{
	CUString *ustr;

	int ptrSize = sizeof(void *);
	// Grow scope buffer if needed
	if (scope->usedBytes + ptrSize > scope->capacity) {
		int newCap = (scope->usedBytes + ptrSize - 1 + 0x40) & ~0x3F;
		char *newBuf = OperatorNew(newCap);
		memcpy(newBuf, scope->data, scope->usedBytes);
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
	}

	// Allocate and construct CUString
	ustr = (CUString *)OperatorNew(sizeof(CUString));
	if (ustr != NULL) {
		static const uint16_t emptyWStr[] = { 0 };
		CUString_Constructor(ustr, emptyWStr);
	}

	// Append pointer to scope buffer
	memcpy(scope->data + scope->usedBytes, &ustr, ptrSize);
	scope->usedBytes += ptrSize;

	// Track in children2 array
	if (scope->childCount2 == scope->childCapacity2) {
		scope->childCapacity2 += 8;
		void **newArr = OperatorNew(scope->childCapacity2 * sizeof(void *));
		if (scope->childCount2 > 0) {
			memcpy(newArr, scope->children2, scope->childCount2 * sizeof(void *));
			OperatorDelete(scope->children2);
		}
		scope->children2 = newArr;
	}
	scope->children2[scope->childCount2] = ustr;
	scope->childCount2++;
}

/*
 * 0x00409254 - CScope::PushDefaultList
 *
 * Pushes an empty heap-allocated CList onto the scope and records
 * it in children3 so the destructor will free it.
 */
void
CScope_PushDefaultList(CScope *scope)
{
	CList *list;

	int ptrSize = sizeof(void *);
	// Grow scope buffer if needed
	if (scope->usedBytes + ptrSize > scope->capacity) {
		int newCap = (scope->usedBytes + ptrSize - 1 + 0x40) & ~0x3F;
		char *newBuf = OperatorNew(newCap);
		memcpy(newBuf, scope->data, scope->usedBytes);
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
	}

	// Allocate and construct CList
	list = (CList *)OperatorNew(sizeof(CList));
	if (list != NULL)
		CList_Constructor(list);

	// Append pointer to scope buffer
	memcpy(scope->data + scope->usedBytes, &list, ptrSize);
	scope->usedBytes += ptrSize;

	// Track in children3 array
	if (scope->childCount3 == scope->childCapacity3) {
		scope->childCapacity3 += 8;
		void **newArr = OperatorNew(scope->childCapacity3 * sizeof(void *));
		if (scope->childCount3 > 0) {
			memcpy(newArr, scope->children3, scope->childCount3 * sizeof(void *));
			OperatorDelete(scope->children3);
		}
		scope->children3 = newArr;
	}
	scope->children3[scope->childCount3] = list;
	scope->childCount3++;
}

/*
 * 0x004093F0 - CScope::PushValue (CScope::PushValue)
 *
 * Inserts size bytes from source into the scope just before the
 * tail sentinel, shifting the trailing 4 bytes forward. Used by
 * the return handlers to deposit a return value behind the saved
 * stream pointer.
 */
void
CScope_PushValue(CScope *scope, const void *source, int size)
{
	int aligned = (size + 3) & ~3;
	int retSize = sizeof(uintptr_t); // return value placeholder at end

	if (scope->usedBytes + aligned > scope->capacity) {
		// Need to grow buffer
		int newCap = (scope->usedBytes + aligned + 0x3F) & ~0x3F;
		char *newBuf = OperatorNew(newCap);

		// Copy existing data
		memcpy(newBuf, scope->data, scope->usedBytes);

		// Shift return value forward to make room
		memcpy(newBuf + scope->usedBytes + aligned - retSize, newBuf + scope->usedBytes - retSize, retSize);

		// Write new data at the insert point
		memcpy(newBuf + scope->usedBytes - retSize, source, size);

		// Free old, update
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
		scope->usedBytes += aligned;
	} else {
		// Enough space - shift return value and write
		memcpy(scope->data + scope->usedBytes + aligned - retSize, scope->data + scope->usedBytes - retSize, retSize);
		memcpy(scope->data + scope->usedBytes - retSize, source, size);
		scope->usedBytes += aligned;
	}
}

/*
 * 0x00409537 - CScope::PushCString
 *
 * Inserts a 4-byte CString pointer before the scope tail (shifting
 * the last 4 bytes forward), growing the data buffer if needed.
 * Then appends the pointer to the children1 array, growing it
 * if childCount1 == childCapacity1.
 */
void
CScope_PushCString(CScope *scope, uintptr_t cstrPtr)
{
	int ptrSize = sizeof(void *);
	if (scope->usedBytes + ptrSize > scope->capacity) {
		int newCap = (scope->usedBytes + ptrSize - 1 + 0x40) & ~0x3F;
		char *newBuf = (char *)OperatorNew(newCap);
		memcpy(newBuf, scope->data, scope->usedBytes);
		memcpy(newBuf + scope->usedBytes, newBuf + scope->usedBytes - ptrSize, ptrSize);
		memcpy(newBuf + scope->usedBytes - ptrSize, &cstrPtr, ptrSize);
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
	} else {
		memcpy(scope->data + scope->usedBytes, scope->data + scope->usedBytes - ptrSize, ptrSize);
		memcpy(scope->data + scope->usedBytes - ptrSize, &cstrPtr, ptrSize);
	}
	scope->usedBytes += ptrSize;

	if (scope->childCount1 == scope->childCapacity1) {
		scope->childCapacity1 += 8;
		void **newArr = (void **)OperatorNew(scope->childCapacity1 * sizeof(void *));
		if (scope->childCount1 > 0) {
			memcpy(newArr, scope->children1, scope->childCount1 * sizeof(void *));
			OperatorDelete(scope->children1);
		}
		scope->children1 = newArr;
	}
	scope->children1[scope->childCount1] = (void *)(uintptr_t)cstrPtr;
	scope->childCount1++;
}

/*
 * 0x004096E5 - CScope::PushCUString
 *
 * Same structure as PushCString but for the children2 array.
 *
 * FIXED: binary updates childCapacity1 instead of childCapacity2 when
 * growing the children2 array. This means childCapacity2 never grows,
 * so every subsequent push re-allocates at the old (too-small) size
 * and eventually writes past the allocated buffer. Fixed to update
 * childCapacity2.
 */
void
CScope_PushCUString(CScope *scope, uintptr_t custrPtr)
{
	int ptrSize = sizeof(void *);
	if (scope->usedBytes + ptrSize > scope->capacity) {
		int newCap = (scope->usedBytes + ptrSize - 1 + 0x40) & ~0x3F;
		char *newBuf = (char *)OperatorNew(newCap);
		memcpy(newBuf, scope->data, scope->usedBytes);
		memcpy(newBuf + scope->usedBytes, newBuf + scope->usedBytes - ptrSize, ptrSize);
		memcpy(newBuf + scope->usedBytes - ptrSize, &custrPtr, ptrSize);
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
	} else {
		memcpy(scope->data + scope->usedBytes, scope->data + scope->usedBytes - ptrSize, ptrSize);
		memcpy(scope->data + scope->usedBytes - ptrSize, &custrPtr, ptrSize);
	}
	scope->usedBytes += ptrSize;

	if (scope->childCount2 == scope->childCapacity2) {
		scope->childCapacity2 += 8;
		void **newArr = (void **)OperatorNew(scope->childCapacity2 * sizeof(void *));
		if (scope->childCount2 > 0) {
			memcpy(newArr, scope->children2, scope->childCount2 * sizeof(void *));
			OperatorDelete(scope->children2);
		}
		scope->children2 = newArr;
	}
	scope->children2[scope->childCount2] = (void *)(uintptr_t)custrPtr;
	scope->childCount2++;
}

/*
 * 0x00409893 - CScope::PushResult5
 *
 * Same structure as PushCString but for the children3 array.
 */
void
CScope_PushResult5(CScope *scope, uintptr_t value)
{
	int ptrSize = sizeof(void *);
	if (scope->usedBytes + ptrSize > scope->capacity) {
		int newCap = (scope->usedBytes + ptrSize - 1 + 0x40) & ~0x3F;
		char *newBuf = (char *)OperatorNew(newCap);
		memcpy(newBuf, scope->data, scope->usedBytes);
		memcpy(newBuf + scope->usedBytes, newBuf + scope->usedBytes - ptrSize, ptrSize);
		memcpy(newBuf + scope->usedBytes - ptrSize, &value, ptrSize);
		OperatorDelete(scope->data);
		scope->capacity = newCap;
		scope->data = newBuf;
	} else {
		memcpy(scope->data + scope->usedBytes, scope->data + scope->usedBytes - ptrSize, ptrSize);
		memcpy(scope->data + scope->usedBytes - ptrSize, &value, ptrSize);
	}
	scope->usedBytes += ptrSize;

	if (scope->childCount3 == scope->childCapacity3) {
		scope->childCapacity3 += 8;
		void **newArr = (void **)OperatorNew(scope->childCapacity3 * sizeof(void *));
		if (scope->childCount3 > 0) {
			memcpy(newArr, scope->children3, scope->childCount3 * sizeof(void *));
			OperatorDelete(scope->children3);
		}
		scope->children3 = newArr;
	}
	scope->children3[scope->childCount3] = (void *)(uintptr_t)value;
	scope->childCount3++;
}

/*
 * 0x00409A41 - CScope::StoreValue (CScope::StoreValue)
 *
 * Pops size bytes off the top of the scope into dest. The pop
 * advance is rounded up to 4 bytes. Used by TreeEvaluator to
 * restore the saved stream pointer and by DispatchHandler to read
 * parameter values.
 */
void
CScope_StoreValue(CScope *scope, void *dest, int size)
{
	int aligned = (size + 3) & ~3;
	char *src = scope->data + scope->usedBytes - aligned;

	memcpy(dest, src, size);
	scope->usedBytes -= aligned;
}

/*
 * 0x00409A8A - CScope::SetUsedBytes (CScope::SetUsedBytes)
 *
 * Sets the scope's usedBytes counter to newUsedBytes.
 */
void
CScope_SetUsedBytes(CScope *scope, int usedBytes)
{
	scope->usedBytes = usedBytes;
}

/*
 * 0x00409AA0 - CNodeList::CNodeList (ResultNodeList constructor)
 *
 * Initialises a dynamic uint32_t stack with capacity 4 and a NULL
 * sentinel at index 0 (so peek/pop on an empty stack returns 0).
 */
void
CNodeList_Constructor(CNodeList *list)
{
	list->capacity = 4;
	list->count = 1;
	list->arr = (uintptr_t *)OperatorNew(4 * sizeof(uintptr_t));
	list->arr[0] = 0; /* NULL sentinel */
}

/*
 * 0x00409ADD - CNodeList::~CNodeList (ResultNodeList destructor)
 *
 * Frees the heap-allocated array.
 */
void
CNodeList_Destructor(CNodeList *list)
{
	OperatorDelete(list->arr);
}

/*
 * 0x00409AFF - CNodeList::Push (ResultNodeList::Push)
 *
 * Pushes value onto the stack, growing the backing array by 4 slots
 * when needed.
 */
void
CNodeList_Push(CNodeList *list, uintptr_t value)
{
	if (list->count == list->capacity) {
		uintptr_t *newBuf;
		newBuf = (uintptr_t *)OperatorNew((list->capacity + 4) * sizeof(uintptr_t));
		memcpy(newBuf, list->arr, list->capacity * sizeof(uintptr_t));
		OperatorDelete(list->arr);
		list->arr = newBuf;
		list->capacity += 4;
	}
	list->arr[list->count] = value;
	list->count++;
}

/*
 * 0x00409B9B - CNodeList::Pop (ResultNodeList::Pop)
 *
 * Removes and returns the top stack element.
 */
uintptr_t
CNodeList_Pop(CNodeList *list)
{
	list->count--;
	return list->arr[list->count];
}

/*
 * 0x00409BC4 - CNodeList::Peek (ResultNodeList::Peek)
 *
 * Returns the top stack element without popping it.
 */
uintptr_t
CNodeList_Peek(CNodeList *list)
{
	return list->arr[list->count - 1];
}

/*
 * 0x00409BDF - CExecThread::CExecThread (CExecThread constructor)
 *
 * Constructs an exec thread for trigHandler bound to scriptInstance.
 * Builds the embedded CScope and result/handler stacks, sets stream
 * to the trigger's bytecode entry, and links the thread into the
 * global thread list.
 */
void
CExecThread_Constructor(CExecThread *thread, void *trigHandler, void *scriptInstance)
{
	CFuncScope *fs = (CFuncScope *)trigHandler;

	thread->scriptRef = scriptInstance;
	CScope_Constructor(&thread->scope);
	CNodeList_Constructor(&thread->hstack);
	CNodeList_Constructor(&thread->rstack);
	thread->finished = 0;
	thread->returnVal = 0;
	thread->defaultReturn = 1;
	thread->activeNext = NULL;
	thread->activePrev = NULL;
	thread->stream = (char *)fs->bodyStream; // trigHandler->field_0x10

	// Push NULL sentinel onto handler stack (binary: +0x38)
	CNodeList_Push(&thread->hstack, 0);

	// Push scope size onto result stack (binary: +0x44)
	CNodeList_Push(&thread->rstack, fs->namedScope.totalSize + sizeof(uintptr_t));

	// Resize scope buffer for trigger parameters
	CScope_Resize(&thread->scope, fs->namedScope.totalSize + sizeof(uintptr_t));

	// Link into global thread list at head (0x0063D82C)
	thread->globalNext = g_globalThreadHead;
	thread->globalPrev = NULL;
	if (thread->globalNext != NULL)
		thread->globalNext->globalPrev = thread;
	g_globalThreadHead = thread;
}

/*
 * Active thread list - 0x006482A0 in binary.
 * ThreadList struct (head, count) managing the active execution queue.
 */
ThreadList g_activeThreadList = { NULL, 0 };

/*
 * 0x00409CF4 - CExecThread::~CExecThread (CExecThread destructor)
 *
 * Tears down an exec thread: unlinks it from the global and active
 * thread lists, poisons stream/serial with 0xABCD, and destructs
 * the embedded handler/result stacks and scope.
 */
void
CExecThread_Destructor(CExecThread *thread)
{
	// Step 1: Unlink from global thread list
	if (thread->globalNext != NULL)
		thread->globalNext->globalPrev = thread->globalPrev;
	if (thread->globalPrev != NULL)
		thread->globalPrev->globalNext = thread->globalNext;
	else
		g_globalThreadHead = thread->globalNext;

	// Step 2: Unlink from active thread list (0x006482A0)
	ThreadList_Unlink(&g_activeThreadList, thread);

	// Step 3: Poison fields
	thread->stream = (char *)(intptr_t)0xABCD;
	thread->scriptRef = (void *)(uintptr_t)0xABCD;

	// Step 4: Clear execution state
	thread->finished = 0;
	thread->defaultReturn = 0;
	thread->returnVal = 0;

	// Step 5: Destroy result stack (+0x44, binary: reverse construction order)
	CNodeList_Destructor(&thread->rstack);

	// Step 6: Destroy handler stack (+0x38)
	CNodeList_Destructor(&thread->hstack);

	// Step 7: Destroy scope
	CScope_Destructor(&thread->scope);
}

/*
 * Token variant table - extracted from UoDemo.exe at 0x00610B40.
 * Binary: 0xA (10) bytes per token type, 5 x uint16 little-endian.
 * Each token type can be encoded with any of its 5 variant values
 * in the compiled bytecode. MatchToken checks all 5.
 *
 * Only token types with hasVariants=1 in g_TokenTypeTable use this.
 * Text-based tokens (trigger names, 0x42..0x88) use string comparison.
 */
const uint16_t g_TokenVariants[TOKEN_TYPE_COUNT][5] = {
	[SM_LPAREN] = { 0x390C, 0x0F3E, 0x0199, 0x0124, 0x305E },
	[SM_RPAREN] = { 0x39B3, 0x2D12, 0x26A6, 0x5D03, 0x1238 },
	[SM_COMMA] = { 0x3B25, 0x1E1F, 0x1AD4, 0x7F96, 0x7FF5 },
	[SM_SEMI] = { 0x0732, 0x0120, 0x5CFD, 0x3E12, 0x3BF6 },
	[SM_LBRACE] = { 0x3A9E, 0x0DDC, 0x5E14, 0x2E40, 0x1CD0 },
	[SM_RBRACE] = { 0x7EB7, 0x6032, 0x2C3B, 0x15A1, 0x3EF6 },
	[SM_LBRACKET] = { 0x409D, 0x12E1, 0x121F, 0x26CA, 0x3699 },
	[SM_RBRACKET] = { 0x0902, 0x7BB9, 0x139D, 0x187E, 0x16C5 },
	[OP_NOT] = { 0x3CD5, 0x13E9, 0x4080, 0x5DB2, 0x33EA },
	[OP_ADD] = { 0x23C9, 0x60BF, 0x3CD6, 0x0FBF, 0x2F14 },
	[OP_SUB] = { 0x047E, 0x368E, 0x2FFF, 0x288F, 0x7DD1 },
	[OP_MUL] = { 0x261E, 0x5E9D, 0x1916, 0x32E6, 0x401D },
	[OP_DIV] = { 0x0384, 0x18D7, 0x0FC9, 0x0E12, 0x2833 },
	[OP_MOD] = { 0x249E, 0x2B0C, 0x11F4, 0x5DD5, 0x127E },
	[OP_ISEQ] = { 0x0135, 0x07CF, 0x1AF4, 0x0ECC, 0x01D3 },
	[OP_ISNEQ] = { 0x0E90, 0x3A2D, 0x37E6, 0x19D9, 0x252A },
	[OP_LT] = { 0x37E5, 0x1DC0, 0x1481, 0x4087, 0x2B01 },
	[OP_GT] = { 0x16D4, 0x3A8D, 0x7FBE, 0x0C7B, 0x0C15 },
	[OP_LTEQ] = { 0x3807, 0x0633, 0x251F, 0x1D18, 0x3492 },
	[OP_GTEQ] = { 0x19DA, 0x39CE, 0x3BB1, 0x3004, 0x1796 },
	[OP_ASSIGN] = { 0x1F16, 0x182F, 0x2CF7, 0x5ED0, 0x1316 },
	[OP_LOGAND] = { 0x5D24, 0x0588, 0x7CFE, 0x2725, 0x0DE5 },
	[OP_LOGOR] = { 0x13D3, 0x29D8, 0x0A28, 0x09CE, 0x3960 },
	[OP_XOR] = { 0x263D, 0x3B97, 0x4027, 0x138A, 0x282D },
	[OP_INC] = { 0x5CCD, 0x0940, 0x293B, 0x40A5, 0x1D11 },
	[OP_DEC] = { 0x2528, 0x0EA9, 0x3F0B, 0x3087, 0x3F97 },
	[TK_INT] = { 0x30F1, 0x3295, 0x01C1, 0x0CE1, 0x3EE9 },
	[TK_STRING] = { 0x3F9A, 0x30A7, 0x2DB5, 0x169A, 0x2FE7 },
	[TK_USTRING] = { 0x10D9, 0x0390, 0x2A38, 0x0728, 0x5C5E },
	[TK_LOC] = { 0x01E1, 0x1030, 0x1BD9, 0x159F, 0x2BA5 },
	[TK_OBJ] = { 0x28E2, 0x2F0C, 0x1289, 0x3382, 0x36C2 },
	[TK_LIST] = { 0x26B1, 0x1CDF, 0x27DA, 0x0E29, 0x113E },
	[TK_VOID] = { 0x2E39, 0x1D3F, 0x1D5E, 0x1FF1, 0x7E0E },
	[T_OFFSET] = { 0x06E3, 0x36A1, 0x0C1E, 0x2120, 0x1DCB },
	[TK_IF] = { 0x12C2, 0x1003, 0x0607, 0x0784, 0x2B0F },
	[TK_ELSE] = { 0x3305, 0x32E7, 0x212C, 0x018E, 0x3308 },
	[TK_ENDIF] = { 0x1EDC, 0x20A8, 0x37BE, 0x01EB, 0x123B },
	[TK_WHILE] = { 0x3106, 0x018C, 0x357E, 0x0A87, 0x5D2B },
	[TK_ENDWHILE] = { 0x03FA, 0x0AF0, 0x0786, 0x2332, 0x1295 },
	[TK_FOR] = { 0x7DAA, 0x2F0B, 0x1BFC, 0x13F5, 0x1ECA },
	[TK_ENDFOR] = { 0x0D9F, 0x388A, 0x15FD, 0x7CB8, 0x1AF6 },
	[TK_CONTINUE] = { 0x017B, 0x6014, 0x0E99, 0x33CD, 0x27D3 },
	[TK_BREAK] = { 0x7F0D, 0x04F0, 0x183A, 0x1FB4, 0x13A6 },
	[TK_GOTO] = { 0x190B, 0x3605, 0x20AD, 0x32CF, 0x2CD5 },
	[TK_SWITCH] = { 0x04B0, 0x1927, 0x08FF, 0x31D8, 0x0914 },
	[TK_ENDSWITCH] = { 0x13F4, 0x3A27, 0x387C, 0x32C1, 0x198C },
	[TK_CASE] = { 0x3223, 0x17B8, 0x3895, 0x248D, 0x342D },
	[TK_DEFAULT] = { 0x5D3D, 0x3260, 0x32DE, 0x2780, 0x31AD },
	[TK_RETURN] = { 0x5DE9, 0x5EA5, 0x11D5, 0x199F, 0x2F15 },
	[TK_FUNCTION] = { 0x0E01, 0x19FE, 0x3821, 0x0B93, 0x0A2F },
	[TK_TRIGGER] = { 0x09B3, 0x038F, 0x328A, 0x08AF, 0x5CCA },
	[TK_MEMBER] = { 0x0C95, 0x7CBE, 0x7C27, 0x5D2A, 0x2FA1 },
	[TK_INHERITS] = { 0x31BE, 0x15B4, 0x07C9, 0x27C0, 0x1B32 },
	[TK_FORWARD] = { 0x2934, 0x3E09, 0x012C, 0x2CC6, 0x7FA6 },
	[T_STR] = { 0x5D17, 0x0A1D, 0x3B29, 0x2BFA, 0x2BB8 },
	[T_BYTE] = { 0x17BD, 0x21EB, 0x2015, 0x5DB8, 0x15E1 },
	[T_WORD] = { 0x5B60, 0x3D8F, 0x0FF4, 0x275B, 0x3C8A },
	[T_DWORD] = { 0x188F, 0x5D27, 0x7F5C, 0x01F7, 0x093B },
	[T_ID] = { 0x3510, 0x0B9B, 0x06E9, 0x3B9E, 0x0B31 },
	[0x89] = { 0x2AEA, 0x0860, 0x403E, 0x3925, 0x16F2 },
};

/*
 * Trigger name strings - text-based token names. In UoDemo.exe these
 * live as the .str field (offset +0x08) of g_TokenTypeTable's 12-byte
 * entries at 0x00611318, rows 0x42..0x88. The C source exposes them
 * as a flat array indexed by token type. They appear literally in
 * compiled bytecodes; MatchToken uses strncmp.
 */
const char *g_TriggerNames[TOKEN_TYPE_COUNT] = {
	[TR_SPEECH] = "TR_SPEECH",
	[TR_GOTATTACKED] = "TR_GOTATTACKED",
	[TR_KILLEDTARGET] = "TR_KILLEDTARGET",
	[TR_AVERSION] = "TR_AVERSION",
	[TR_DEATH] = "TR_DEATH",
	[TR_SAWDEATH] = "TR_SAWDEATH",
	[TR_FIGHTPULSE] = "TR_FIGHTPULSE",
	[TR_WASHIT] = "TR_WASHIT",
	[TR_FAILFOOD] = "TR_FAILFOOD",
	[TR_FAILDESIRE] = "TR_FAILDESIRE",
	[TR_FAILSHELTER] = "TR_FAILSHELTER",
	[TR_FOUNDFOOD] = "TR_FOUNDFOOD",
	[TR_FOUNDDESIRE] = "TR_FOUNDDESIRE",
	[TR_FOUNDSHELTER] = "TR_FOUNDSHELTER",
	[TR_TIME] = "TR_TIME",
	[TR_CREATION] = "TR_CREATION",
	[TR_ENTERRANGE] = "TR_ENTERRANGE",
	[TR_LEAVERANGE] = "TR_LEAVERANGE",
	[TR_LOITER] = "TR_LOITER",
	[TR_SEEKFOOD] = "TR_SEEKFOOD",
	[TR_SEEKDESIRE] = "TR_SEEKDESIRE",
	[TR_SEEKSHELTER] = "TR_SEEKSHELTER",
	[TR_MESSAGE] = "TR_MESSAGE",
	[TR_USE] = "TR_USE",
	[TR_TARGETOBJ] = "TR_TARGETOBJ",
	[TR_TARGETLOC] = "TR_TARGETLOC",
	[TR_WEATHER] = "TR_WEATHER",
	[TR_WASDROPPED] = "TR_WASDROPPED",
	[TR_LOOKEDAT] = "TR_LOOKEDAT",
	[TR_GIVE] = "TR_GIVE",
	[TR_WASGOTTEN] = "TR_WASGOTTEN",
	[TR_PATHFOUND] = "TR_PATHFOUND",
	[TR_PATHNOTFOUND] = "TR_PATHNOTFOUND",
	[TR_CALLBACK] = "TR_CALLBACK",
	[TR_ISHITTING] = "TR_ISHITTING",
	[TR_CONVOFUNC] = "TR_CONVOFUNC",
	[TR_TYPESELECTED] = "TR_TYPESELECTED",
	[TR_HUESELECTED] = "TR_HUESELECTED",
	[TR_MOON] = "TR_MOON",
	[TR_MINRANGEATTACK] = "TR_MINRANGEATTACK",
	[TR_MINRANGEDEFEND] = "TR_MINRANGEDEFEND",
	[TR_MAXRANGEATTACK] = "TR_MAXRANGEATTACK",
	[TR_MAXRANGEDEFEND] = "TR_MAXRANGEDEFEND",
	[TR_DESTROYED] = "TR_DESTROYED",
	[TR_EQUIP] = "TR_EQUIP",
	[TR_UNEQUIP] = "TR_UNEQUIP",
	[TR_ISSTACKABLEON] = "TR_ISSTACKABLEON",
	[TR_STACKONTO] = "TR_STACKONTO",
	[TR_MULTIRECYCLE] = "TR_MULTIRECYCLE",
	[TR_DECAY] = "TR_DECAY",
	[TR_SERVERSWITCH] = "TR_SERVERSWITCH",
	[TR_OORUSE] = "TR_OORUSE",
	[TR_ACQUIREDESIRE] = "TR_ACQUIREDESIRE",
	[TR_LOGOUT] = "TR_LOGOUT",
	[TR_OBJECTLOADED] = "TR_OBJECTLOADED",
	[TR_GENERICGUMP] = "TR_GENERICGUMP",
	[TR_OORTARGETOBJ] = "TR_OORTARGETOBJ",
	[TR_PKPOST] = "TR_PKPOST",
	[TR_TEXTENTRY] = "TR_TEXTENTRY",
	[TR_SHOP] = "TR_SHOP",
	[TR_STOLENFROM] = "TR_STOLENFROM",
	[TR_OBJACCESS] = "TR_OBJACCESS",
	[TR_ISHEALTHY] = "TR_ISHEALTHY",
	[TR_ONLINE] = "TR_ONLINE",
	[TR_TRANSACCOUNTCHECK] = "TR_TRANSACCOUNTCHECK",
	[TR_TRANSRESPONSE] = "TR_TRANSRESPONSE",
	[TR_CANBUY] = "TR_CANBUY",
	[TR_MOBISHITTING] = "TR_MOBISHITTING",
	[TR_FAMECHANGED] = "TR_FAMECHANGED",
	[TR_KARMACHANGED] = "TR_KARMACHANGED",
	[TR_MURDERCOUNTCHANGED] = "TR_MURDERCOUNTCHANGED",
};

ScriptAttachNode *g_scriptInstanceListHead; /* 0x0063D8CC */

/*
 * Deferred script creation event system.
 *
 * During world loading, script creation events (0x0F) are deferred
 * until all entities are loaded. The binary uses a chunked block
 * linked list: 0x804-byte blocks holding 256 serial/param pairs each.
 *
 * Globals:
 *   0x0063D8E0 - g_deferScriptCreation: recording flag
 *   0x0063E0F8 - g_scriptCreationCount: total recorded entries
 *   0x0063E0FC - g_scriptCreationListHead: linked list head
 */

// 0x0063D8E0
int g_deferScriptCreation;
// 0x0063E0F8
int g_scriptCreationCount;
// 0x0063E0FC
ScriptCreationBlock *g_scriptCreationListHead;

/*
 * Global thread list head - 0x0063D82C in binary.
 * All CExecThread objects are linked via globalNext/globalPrev (0x5C/0x60).
 */
CExecThread *g_globalThreadHead = NULL;

/*
 * Type table - extracted from UoDemo.exe at 0x005EE1EC.
 * Binary: 8 entries of 0x14 (20) bytes each.
 *
 * Each entry:
 *   offset 0x00: int tokenType (e.g., 0x1D for TK_INT)
 *   offset 0x04: char* name    (3-char abbreviation: "int", "str", etc.)
 *   offset 0x08: char code     (single-char type code: i, s, q, c, o, l, v, u)
 *   offset 0x0C: int size      (size in bytes: 4, 4, 4, 6, 4, 4, 0, 0)
 *   offset 0x10: int index     (0..7)
 *
 * Token types per entry:
 *   [0] TK_INT=0x1D   [1] TK_STRING=0x1E   [2] TK_USTRING=0x1F
 *   [3] TK_LOC=0x20   [4] TK_OBJ=0x21      [5] TK_LIST=0x22
 *   [6] TK_VOID=0x23  [7] sentinel (tokenType=-1)
 */
const int g_TypeTokenIds[WTYPE_COUNT] = { TK_INT, TK_STRING, TK_USTRING, TK_LOC, TK_OBJ, TK_LIST, TK_VOID, -1 };

const char g_WombatTypeCodes[WTYPE_COUNT] = { 'i', 's', 'q', 'c', 'o', 'l', 'v', 'u' };
const int g_WombatTypeSizes[WTYPE_COUNT] = {
	sizeof(void *),         /* WTYPE_INT (binary: 4; widened for 64-bit scope alignment) */
	sizeof(void *),         /* WTYPE_STRING (pointer) */
	sizeof(void *),         /* WTYPE_USTRING (pointer) */
	6,        /* WTYPE_LOC (3 x uint16_t) */
	sizeof(void *),         /* WTYPE_OBJ (binary: 4; widened for 64-bit scope alignment) */
	sizeof(void *),         /* WTYPE_LIST (pointer) */
	0,        /* WTYPE_VOID */
	0         /* WTYPE_UNKNOWN */
};

/*
 * Global compiler context stack head - 0x0063E128 in binary.
 * Points to the current (top-of-stack) compiler context.
 */
CScriptCompiler *g_ScriptCompiler = NULL;

/*
 * Global CScriptManager - 0x00698988 in binary.
 * Linked list of loaded CScript objects.
 * Used by FindOrLoadScript (0x00426106) and AddScript (0x004260E3).
 */
CScriptManager g_ScriptManager = { NULL };

/*
 * Trigger event names - extracted from binary at 0x005EE45C.
 * 71 entries, lowercase names matching the Wombat source trigger syntax.
 * Index 0 = "speech", index 70 = "murdercountchanged".
 *
 * The parser (0x00427436) uses strcmp against this table to find
 * the trigger event type index. DispatchEvent (0x0042B951) uses
 * the same indices for its jump table at 0x0042D7B8.
 */
const char *g_TriggerEventNames[TRIGGER_EVENT_COUNT] = {
	"speech",             /* 0x00 */
	"gotattacked",        /* 0x01 */
	"killedtarget",       /* 0x02 */
	"aversion",           /* 0x03 */
	"death",              /* 0x04 */
	"sawdeath",           /* 0x05 */
	"fightpulse",         /* 0x06 */
	"washit",             /* 0x07 */
	"failfood",           /* 0x08 */
	"faildesire",         /* 0x09 */
	"failshelter",        /* 0x0A */
	"foundfood",          /* 0x0B */
	"founddesire",        /* 0x0C */
	"foundshelter",       /* 0x0D */
	"time",               /* 0x0E */
	"creation",           /* 0x0F */
	"enterrange",         /* 0x10 */
	"leaverange",         /* 0x11 */
	"loiter",             /* 0x12 */
	"seekfood",           /* 0x13 */
	"seekdesire",         /* 0x14 */
	"seekshelter",        /* 0x15 */
	"message",            /* 0x16 */
	"use",         /* 0x17 */
	"targetobj",          /* 0x18 */
	"targetloc",          /* 0x19 */
	"weather",            /* 0x1A */
	"wasdropped",         /* 0x1B */
	"lookedat",           /* 0x1C */
	"give",               /* 0x1D */
	"wasgotten",          /* 0x1E */
	"pathfound",          /* 0x1F */
	"pathnotfound",       /* 0x20 */
	"callback",           /* 0x21 */
	"ishitting",          /* 0x22 */
	"convofunc",          /* 0x23 */
	"typeselected",       /* 0x24 */
	"hueselected",        /* 0x25 */
	"moon",               /* 0x26 */
	"minrangeattack",     /* 0x27 */
	"minrangedefend",     /* 0x28 */
	"maxrangeattack",     /* 0x29 */
	"maxrangedefend",     /* 0x2A */
	"destroyed",          /* 0x2B */
	"equip",              /* 0x2C */
	"unequip",            /* 0x2D */
	"isstackableon",      /* 0x2E */
	"stackonto",          /* 0x2F */
	"multirecycle",       /* 0x30 */
	"decay",              /* 0x31 */
	"serverswitch",       /* 0x32 */
	"ooruse",             /* 0x33 */
	"acquiredesire",      /* 0x34 */
	"logout",             /* 0x35 */
	"objectloaded",       /* 0x36 */
	"genericgump",        /* 0x37 */
	"oortargetobj",       /* 0x38 */
	"pkpost",             /* 0x39 */
	"textentry",          /* 0x3A */
	"shop",               /* 0x3B */
	"stolenfrom",         /* 0x3C */
	"objaccess",          /* 0x3D */
	"ishealthy",          /* 0x3E */
	"online",             /* 0x3F */
	"transaccountcheck",  /* 0x40 */
	"transresponse",      /* 0x41 */
	"canbuy",             /* 0x42 */
	"mobishitting",       /* 0x43 */
	"famechanged",        /* 0x44 */
	"karmachanged",       /* 0x45 */
	"murdercountchanged", /* 0x46 */
};

/*
 * Global ResultNode pool - 0x0063E150 in binary.
 * Initialized at startup with NodePool_Init(&g_NodePool, 0x1000).
 */
NodePool g_NodePool = { NULL, 0x1000, 0 };

/*
 * FreeResultChain - free all nodes in a result chain.
 */
void
FreeResultChain(ResultNode *chain)
{
	while (chain) {
		ResultNode *next = chain->next;
		FreeResultNode(chain);
		chain = next;
	}
}
/*
 * 0x004678D2 - Static init wrapper
 *
 * Static initializer that constructs g_ScriptManager (zeroes the script list
 * head and initializes two internal buffers).
 */
static __attribute__((unused)) void
StaticInit_ScriptManager(void)
{
	CScriptManager_Init(&g_ScriptManager);
}

/*
 * 0x004B9AB0 - CScript::ScalarDelete
 *
 * Thiscall on CScript. Calls CScript_Destructor, then if flags & 1,
 * frees the CScript. Returns this.
 */
static __attribute__((unused)) void *
CScript_ScalarDelete(CScript *this, int flags)
{
	CScript_Destructor(this);
	if (flags & 1)
		free(this);
	return NULL;
}

/*
 * Type name table from binary at 0x005EFD48.
 * 7 entries, stride 0x14. Index matches WTYPE_* constants.
 */
const char *g_womTypeNames[] = { "int", "str", "ust", "loc", "obj", "lis", "voi" };

/*
 * 0x004D78AE - CStringMatcher::CStringMatcher
 *
 * Constructor. Allocates numBuffers capture buffers of bufSize bytes each.
 */
void
CStringMatcher_Init(CStringMatcher *sm, int numBuffers, int bufSize)
{
	int i;

	sm->numBuffers = numBuffers;
	sm->bufLimit = bufSize;
	sm->counter = 0;
	sm->bufArray = (char **)malloc(numBuffers * sizeof(char *));
	for (i = 0; i < sm->numBuffers; i++)
		sm->bufArray[i] = (char *)malloc(sm->bufLimit);
}

/*
 * 0x004D792C - CStringMatcher::~CStringMatcher
 *
 * Destructor. Frees all capture buffers and the buffer array.
 */
void
CStringMatcher_Destroy(CStringMatcher *sm)
{
	int i;

	for (i = 0; i < sm->numBuffers; i++)
		free(sm->bufArray[i]);
	free(sm->bufArray);
}

/*
 * 0x004D79B3 - CStringMatcher::Match
 *
 * Thiscall method. Case-insensitive wildcard pattern matching.
 * '*' wildcards capture matched text into sm->bufArray buffers.
 * '@' prefix on text enables exact-match mode (disables '*').
 * Returns 1 if match, 0 if no match.
 */
int
CStringMatcher_Match(CStringMatcher *sm, const char *text, const char *pattern)
{
	const char *savedPatternAfterStar = NULL;
	const char *savedTextPos = NULL;
	char *bufPtr = NULL;
	int result = 0;
	int backtrackCount = 0;
	int exactMode = 0;

	if (text == NULL)
		return 0;
	if (pattern == NULL)
		return 0;

	sm->counter = 0;
	exactMode = 0;

	if (*text == '@')
		exactMode = 1;

	for (;;) {
		if (exactMode == 0 && *pattern == '*') {
			backtrackCount = 0;
			pattern++;
			savedPatternAfterStar = pattern;
			savedTextPos = text;
			// Grab capture buffer from bufArray
			bufPtr = sm->bufArray[sm->counter];
			sm->counter++;
			*bufPtr = '\0';
			continue;
		}

		if (tolower((unsigned char)*text) == tolower((unsigned char)*pattern)) {
			if (*pattern == '\0') {
				result = 1;
				break;
			}
			pattern++;
			text++;
			continue;
		}

		if (*text == '\0')
			break;
		if (savedPatternAfterStar == NULL)
			break;

		pattern = savedPatternAfterStar;
		if (backtrackCount < sm->bufLimit) {
			*bufPtr = *savedTextPos;
			bufPtr++;
		}
		backtrackCount++;
		*bufPtr = '\0';
		savedTextPos++;
		text = savedTextPos;
	}

	return result;
}

/*
 * 0x005EE290 - g_OperatorTable[19]
 *
 * Maps token types to operator handler names.
 * Binary: 0x10 bytes per entry; we only store the 3 fields we need.
 * Used by ClassifyStatement (0x0042A5E0) to detect operators (returns STMT_OPERATOR).
 */
const OperatorEntry g_OperatorTable[OPERATOR_COUNT] = {
	/* [ 0] */ { .tokenType = OP_INC, .name = "oprinc", .oprId = 1 },
	/* [ 1] */ { .tokenType = OP_DEC, .name = "oprdec", .oprId = 2 },
	/* [ 2] */ { .tokenType = OP_ADD, .name = "oprplus", .oprId = 3 },
	/* [ 3] */ { .tokenType = OP_SUB, .name = "oprminus", .oprId = 4 },
	/* [ 4] */ { .tokenType = OP_MUL, .name = "oprmult", .oprId = 5 },
	/* [ 5] */ { .tokenType = OP_DIV, .name = "oprdiv", .oprId = 6 },
	/* [ 6] */ { .tokenType = OP_LOGAND, .name = "oprand", .oprId = 7 },
	/* [ 7] */ { .tokenType = OP_LOGOR, .name = "opror", .oprId = 8 },
	/* [ 8] */ { .tokenType = OP_XOR, .name = "oprxor", .oprId = 9 },
	/* [ 9] */ { .tokenType = OP_ISEQ, .name = "oprequiv", .oprId = 10 },
	/* [10] */ { .tokenType = OP_ISNEQ, .name = "oprnequiv", .oprId = 11 },
	/* [11] */ { .tokenType = OP_GT, .name = "oprgt", .oprId = 12 },
	/* [12] */ { .tokenType = OP_LT, .name = "oprlt", .oprId = 13 },
	/* [13] */ { .tokenType = OP_MOD, .name = "oprmod", .oprId = 14 },
	/* [14] */ { .tokenType = OP_GTEQ, .name = "oprgteq", .oprId = 15 },
	/* [15] */ { .tokenType = OP_LTEQ, .name = "oprlteq", .oprId = 16 },
	/* [16] */ { .tokenType = OP_NOT, .name = "oprnot", .oprId = 17 },
	/* [17] */ { .tokenType = SM_LPAREN, .name = "oprnull", .oprId = 18 },
	/* [18] */ { .tokenType = SM_LBRACKET, .name = "oprlist", .oprId = 0 },
};

/*
 * Helper - CSdbStr_Init
 *
 * Construct CSdbStr from C string. Matches binary 0x004014C0 semantics:
 * allocates data buffer, copies string, sets length/capacity.
 * Binary uses C++ string ctor with ref-counted data; we use malloc.
 */
void
CSdbStr_Init(CSdbStr *s, const char *src)
{
	int len = strlen(src);
	int cap = len < 15 ? 15 : len; // binary's C++ string uses SSO (16-byte inline buf)
	s->allocField = 0;
	s->data = (char *)calloc(cap + 1, 1);
	memcpy(s->data, src, len + 1);
	s->length = len;
	s->capacity = cap;
}

/*
 * Helper - CSdbStr_c_str
 *
 * Return C string pointer. Matches binary 0x00401510:
 * if data == NULL, return ""; else return data.
 */
const char *
CSdbStr_c_str(CSdbStr *s)
{
	if (s->data == NULL)
		return "";
	return s->data;
}

/*
 * Helper - CSdbStr_Destructor
 *
 * Destroy string. Matches binary 0x004014F0 -> 0x00402C70:
 * free data, zero fields.
 */
void
CSdbStr_Destructor(CSdbStr *s)
{
	free(s->data);
	s->data = NULL;
	s->length = 0;
	s->capacity = 0;
}

/*
 * Helper - CScriptStringDB_PushBack
 *
 * Append a CSdbStr element to the vector. Matches binary 0x00401A20
 * which calls insert-at-end (0x004032A0 -> 0x00403A70). Deep-copies the
 * element's string data so the caller can safely destroy its temp.
 * Growth strategy: double capacity when full.
 */
void
CScriptStringDB_PushBack(CScriptStringDB *db, CSdbStr *elem)
{
	if (db->last == db->end) {
		int oldCount = db->last - db->first;
		int oldCap = db->end - db->first;
		int newCap = oldCap == 0 ? 16 : oldCap * 2;
		CSdbStr *newArr = (CSdbStr *)realloc(db->first, newCap * sizeof(CSdbStr));
		memset(newArr + oldCap, 0, (newCap - oldCap) * sizeof(CSdbStr));
		db->first = newArr;
		db->last = newArr + oldCount;
		db->end = newArr + newCap;
	}
	// Deep copy: allocate own data buffer
	CSdbStr_Init(db->last, elem->data);
	db->last++;
}

/*
 * Helper - CScriptStringDB_Free
 *
 * Destroy all CSdbStr elements in the vector and free the array.
 * Matches binary 0x00401A50 (vector::clear via erase range)
 * followed by deallocation.
 */
void
CScriptStringDB_Free(CScriptStringDB *db)
{
	CSdbStr *p;

	if (db->first != NULL) {
		for (p = db->first; p != db->last; p++)
			CSdbStr_Destructor(p);
		free(db->first);
	}
	db->first = NULL;
	db->last = NULL;
	db->end = NULL;
}
