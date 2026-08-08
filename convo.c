/*
 * NPC conversation system - speech-triggered dialogue handling.
 *
 * Loads define.lst, fragment.lst, and the per-NPC .frg fragment
 * files into the conversation manager. When a player speaks, fans the
 * event out to every nearby mobile in distance order; the first NPC
 * whose fragment handler consumes the speech stops propagation.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "container.h"
#include "convo.h"
#include "filemanager.h"
#include "io.h"
#include "log.h"
#include "multi.h"
#include "npc.h"
#include "player.h"
#include "region.h"
#include "utils.h"
#include "vtable.h"
#include "wombat_compile.h"

static void CConvoLinkedNode_Constructor(CConvoLinkedNode *this); // 0x0044A820
static CConvoLinkedNode *CConvoLinkedNode_Int_Constructor(CConvoLinkedNode *this, int value, CConvoLinkedNode **listHead); // 0x0044A834
static CConvoLinkedNode *CConvoLinkedNode_String_Constructor(CConvoLinkedNode *this, const char *str, CConvoLinkedNode **listHead); // 0x0044A871
static void CConvoLinkedNode_String_Destructor(CConvoLinkedNode *this); // 0x0044A903
static void CFragment_AddResponse(CFragment *frag, const char *key, const char *text, int soph, int att, int not); // 0x0044AB2D
static void CFragment_AddResponseToGrid(CFragment *frag, const char *responseText, int depth, int *typeStack, CConvoLinkedNode **responseLists); // 0x0044AC7C
static void StaticInit_StringMatcher(void); // 0x0044B148
static int KeyMatchesSpeech(const char *keyPattern); // 0x0044B17F
static void *CConvoLinkedNode_ScalarDelete(CConvoLinkedNode *this, int flags); // 0x0044B2F0
static CConvoLinkedNode *CConvoLinkedNode_BaseConstructor(CConvoLinkedNode *this); // 0x0044B350
static void CConvoLinkedNode_DestructorWrapper(CConvoLinkedNode *this); // 0x0044B370
static void *CConvoLinkedNode_String_ScalarDelete(CConvoLinkedNode *this, int flags); // 0x0044B390
static CConversationManager *CConvoLinkedNode_ClearFields(CConversationManager *this); // 0x0044B4C7
static void CConversationManager_Cleanup(CConversationManager *this); // 0x0044B4F3

// MSVC vtable array from UoDemo.exe (used only in unused decompiled code)
static __attribute__((unused)) uintptr_t g_vt_CConvoLinkedNode[] = { 0 }; /* 0x005EEA40 */
static char *CConversationManager_SubstituteResponse(
        CConversationManager *this, char *defineKey, CItem *npc, CItem *speaker, CItem *about, char *text, char *responseText); // 0x0044CCA0
static int ComputeSophistication(CMobile *npc); // 0x0044D38A
static int ComputeAttitude(CMobile *npc); // 0x0044D3C4
static int Convo_ComputeNotoriety(CPlayer *speaker); // 0x0044D410
static void FreeConvoNodeList(CConvoLinkedNode *head);

// Global conversation manager singleton at 0x0064b338.
CConversationManager g_ConvoMgr;

// Response collection globals.
int g_responseCount;              // 0x00644990
const char *g_speechText;         // 0x00642988
char *g_responseTexts[0x800];     // 0x00642990
char *g_responseKeys[0x800];      // 0x0063f988

// Global string matcher for conversation pattern matching.
CStringMatcher g_StringMatcher;   // 0x0063F978

// Static output buffers for CConversationManager_SubstituteResponse.
// 0x00641988 - SubstituteResponse scratch buffer
static char g_substituteOutBuf[0x2000];
// 0x00645998 - sentinel empty string returned when speaker is not an NPC
static char g_emptyStr1;
// 0x0064599C - sentinel empty string returned when text is too short
static char g_emptyStr2;
// 0x006459A0 - sentinel empty string returned when no response matches
static char g_emptyStr3;

/*
 * 0x004210A0 - CConversationManager::MatchSpeech
 *
 * Forwards to InternalFindResponse with target as both speaker and about.
 */
char *
CConversationManager_MatchSpeech(CConversationManager *this, CItem *npc, CItem *target, char *text)
{
	USED(this);
	return CConversationManager_InternalFindResponse(npc, target, target, text);
}

/*
 * 0x00431240 - CConversationManager::Init
 *
 * Delegates to CConversationManager_startup to load the conversation
 * definitions.
 */
void
CConversationManager_Init(CConversationManager *this)
{
	CConversationManager_startup(this);
}

/*
 * 0x00431382 - SaveAll_MultiManager
 *
 * Writes the multi manager's state back to multi.mul.
 */
void
SaveAll_MultiManager(void)
{
	CMultiManager_SaveToMul(&g_MultiManager);
}

/*
 * 0x0044A820 - CConvoListNode base destructor
 *
 * Resets the vtable pointer to the base class.
 */
static void
CConvoLinkedNode_Constructor(CConvoLinkedNode *this)
{
	this->type = 0;
}

/*
 * 0x0044A834 - CConvoLinkedNode_Int::CConvoLinkedNode_Int
 *
 * Links an int-valued CSophLevel node into the list head.
 */
static CConvoLinkedNode *
CConvoLinkedNode_Int_Constructor(CConvoLinkedNode *this, int value, CConvoLinkedNode **listHead)
{
	CConvoLinkedNode_BaseConstructor(this);
	this->next = *listHead;
	*listHead = this;
	this->u.value = value;
	return this;
}

/*
 * 0x0044A871 - CConvoLinkedNode_String::CConvoLinkedNode_String
 *
 * Links a CKey node holding a duplicated pattern string.
 */
static CConvoLinkedNode *
CConvoLinkedNode_String_Constructor(CConvoLinkedNode *this, const char *str, CConvoLinkedNode **listHead)
{
	CConvoLinkedNode_BaseConstructor(this);
	this->type = 1;
	this->next = *listHead;
	*listHead = this;
	this->u.pattern = OperatorNew(strlen(str) + 1);
	strcpy(this->u.pattern, str);
	return this;
}

/*
 * 0x0044A903 - CConvoLinkedNode_String::~CConvoLinkedNode_String
 *
 * Frees the pattern string and resets the node type.
 */
static void
CConvoLinkedNode_String_Destructor(CConvoLinkedNode *this)
{
	this->type = 1;
	OperatorDelete(this->u.pattern);
	this->type = 0;
}

/*
 * 0x0044A936 - CFragment::Init
 *
 * Prepends to g_ConvoMgr.fragmentList and allocates the 4x6x6 response
 * grid with a zeroed slot at every cell.
 */
CFragment *
CFragment_Init(CFragment *this, const char *name)
{
	int i, j, k;

	this->next = g_ConvoMgr.fragmentList;
	g_ConvoMgr.fragmentList = this;

	this->name = OperatorNew(strlen(name) + 1);
	strcpy(this->name, name);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 6; j++) {
			for (k = 0; k < 6; k++) {
				this->data[i][j][k] = OperatorNew(sizeof(uintptr_t));
				*this->data[i][j][k] = 0;
			}
		}
	}

	this->dedupList = NULL;
	return this;
}

/*
 * 0x0044AA3A - CFragment::Destroy
 *
 * Frees the name, every grid cell array, and the dedup list.
 */
void
CFragment_Destroy(CFragment *this)
{
	int i, j, k;
	CFragDedupNode *node;

	OperatorDelete(this->name);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 6; j++) {
			for (k = 0; k < 6; k++) {
				OperatorDelete(this->data[i][j][k]);
			}
		}
	}

	while (this->dedupList != NULL) {
		node = this->dedupList;
		this->dedupList = node->next;
		OperatorDelete(node->name);
		OperatorDelete(node);
	}
}

/*
 * 0x0044AB2D - CFragment::AddResponse
 *
 * Appends a (key, text) pair to data[soph][att][not], a zero-terminated
 * array of dedup-node pointers.
 */
static void
CFragment_AddResponse(CFragment *frag, const char *key, const char *text, int soph, int att, int not)
{
	uintptr_t *oldArr;
	uintptr_t *newArr;
	int count;

	oldArr = frag->data[soph][att][not];

	count = 0;
	while (oldArr[count] != 0)
		count++;

	newArr = OperatorNew((count + 3) * sizeof(uintptr_t));

	count = 0;
	while (oldArr[count] != 0) {
		newArr[count] = oldArr[count];
		count++;
	}

	newArr[count] = (uintptr_t)key;
	count++;
	newArr[count] = (uintptr_t)text;
	count++;
	newArr[count] = 0;

	OperatorDelete(oldArr);
	frag->data[soph][att][not] = newArr;
}

/*
 * 0x0044AC7C - CFragment::AddResponseToGrid
 *
 * Dedupes response/key strings and calls AddResponse for every
 * (key, soph, att, not) combination collected from the type stack.
 */
static void
CFragment_AddResponseToGrid(CFragment *frag, const char *responseText, int depth, int *typeStack, CConvoLinkedNode **responseLists)
{
	CFragDedupNode *dedupNode;
	CFragDedupNode *keyNode;
	CConvoLinkedNode *nodeIter;
	char *keyArray[64];
	int sophArray[32];
	int attArray[32];
	int notArray[32];
	int sophCount, attCount, notCount, keyCount;
	int i;
	int sophIdx, attIdx, notIdx, keyIdx;

	dedupNode = frag->dedupList;
	while (dedupNode != NULL) {
		if (strcasecmp(dedupNode->name, responseText) == 0)
			break;
		dedupNode = dedupNode->next;
	}
	if (dedupNode == NULL) {
		dedupNode = OperatorNew(sizeof(CFragDedupNode));
		dedupNode->next = frag->dedupList;
		frag->dedupList = dedupNode;
		dedupNode->name = OperatorNew(strlen(responseText) + 1);
		strcpy(dedupNode->name, responseText);
	}

	sophCount = 0;
	attCount = 0;
	notCount = 0;
	keyCount = 0;
	for (i = 1; i < depth; i++) {
		switch (typeStack[i]) {
		case 1:
			nodeIter = responseLists[i];
			while (nodeIter != NULL) {
				char *keyStr = nodeIter->u.pattern;
				keyNode = frag->dedupList;
				while (keyNode != NULL) {
					if (strcasecmp(keyNode->name, keyStr) == 0)
						break;
					keyNode = keyNode->next;
				}
				if (keyNode == NULL) {
					keyNode = OperatorNew(sizeof(CFragDedupNode));
					keyNode->next = frag->dedupList;
					frag->dedupList = keyNode;
					keyNode->name = OperatorNew(strlen(keyStr) + 1);
					strcpy(keyNode->name, keyStr);
				}
				keyArray[keyCount] = keyNode->name;
				keyCount++;
				nodeIter = nodeIter->next;
			}
			break;
		case 2:
			sophArray[sophCount] = responseLists[i]->u.value;
			sophCount++;
			break;
		case 3:
			nodeIter = responseLists[i];
			while (nodeIter != NULL) {
				attArray[attCount] = nodeIter->u.value;
				attCount++;
				nodeIter = nodeIter->next;
			}
			break;
		case 4:
			nodeIter = responseLists[i];
			while (nodeIter != NULL) {
				notArray[notCount] = nodeIter->u.value;
				notCount++;
				nodeIter = nodeIter->next;
			}
			break;
		}
	}

	if (sophCount == 0) {
		sophCount = 1;
		sophArray[0] = 0;
	}
	if (attCount == 0) {
		attCount = 1;
		attArray[0] = 0;
	}
	if (notCount == 0) {
		notCount = 1;
		notArray[0] = 0;
	}

	for (sophIdx = 0; sophIdx < sophCount; sophIdx++) {
		for (attIdx = 0; attIdx < attCount; attIdx++) {
			for (notIdx = 0; notIdx < notCount; notIdx++) {
				for (keyIdx = 0; keyIdx < keyCount; keyIdx++) {
					CFragment_AddResponse(frag, keyArray[keyIdx], dedupNode->name, sophArray[sophIdx], attArray[attIdx], notArray[notIdx]);
				}
			}
		}
	}
}

// Helper: free a linked list of CConvoLinkedNode
static void
FreeConvoNodeList(CConvoLinkedNode *head)
{
	CConvoLinkedNode *next;
	while (head != NULL) {
		next = head->next;
		if (head->type == 1 && head->u.pattern != NULL)
			free(head->u.pattern);
		free(head);
		head = next;
	}
}

/*
 * 0x0044B148 - StaticInit_StringMatcher
 *
 * Initializes the global g_StringMatcher with capacity 10 x 0x200.
 */
static __attribute__((unused)) void
StaticInit_StringMatcher(void)
{
	CStringMatcher_Init(&g_StringMatcher, 10, 0x200);
}

/*
 * 0x0044B17F - KeyMatchesSpeech
 *
 * Returns 0 when keyPattern matches g_speechText ('@' prefix means exact,
 * otherwise wildcard via g_StringMatcher).
 */
static int
KeyMatchesSpeech(const char *keyPattern)
{
	if (keyPattern[0] == '@') {
		return strcasecmp(g_speechText, keyPattern);
	} else {
		int r = CStringMatcher_Match(&g_StringMatcher, g_speechText, keyPattern);
		return r ? 0 : 1;
	}
}

/*
 * 0x0044B1BD - CFragment::CollectKeyResponses
 *
 * For each entry in data[soph][attitude][notoriety] whose key matches
 * g_speechText, appends to g_responseTexts/g_responseKeys.
 */
void
CFragment_CollectKeyResponses(CFragment *this, int soph, int attitude, int notoriety)
{
	uintptr_t *ptr;

	ptr = this->data[soph][attitude][notoriety];

	while (*ptr != 0) {
		if (g_responseCount == 0x800)
			return;

		if (KeyMatchesSpeech((const char *)ptr[0]) == 0) {
			g_responseTexts[g_responseCount] = (char *)ptr[0];
			g_responseKeys[g_responseCount] = (char *)ptr[1];
			g_responseCount++;
		}
		ptr += 2;
	}
}

/*
 * 0x0044B250 - CFragment::CollectResponses
 *
 * Tries all 8 combinations of exact/wildcard(0) across the three filter
 * dimensions, from most specific to least.
 */
void
CFragment_CollectResponses(CFragment *this, int soph, int attitude, int notoriety)
{
	CFragment_CollectKeyResponses(this, soph, attitude, notoriety);
	CFragment_CollectKeyResponses(this, soph, attitude, 0);
	CFragment_CollectKeyResponses(this, soph, 0, notoriety);
	CFragment_CollectKeyResponses(this, soph, 0, 0);
	CFragment_CollectKeyResponses(this, 0, attitude, notoriety);
	CFragment_CollectKeyResponses(this, 0, attitude, 0);
	CFragment_CollectKeyResponses(this, 0, 0, notoriety);
	CFragment_CollectKeyResponses(this, 0, 0, 0);
}

/*
 * 0x0044B2F0 - CConvoLinkedNode::`scalar deleting destructor'
 *
 * ORPHANED: zero callers in the binary.
 */
static __attribute__((unused)) void *
CConvoLinkedNode_ScalarDelete(CConvoLinkedNode *this, int flags)
{
	CConvoLinkedNode_Constructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x0044B350 - CConvoLinkedNode::CConvoLinkedNode (base constructor)
 *
 * Clears the node's type field and returns this.
 */
static CConvoLinkedNode *
CConvoLinkedNode_BaseConstructor(CConvoLinkedNode *this)
{
	this->type = 0;
	return this;
}

/*
 * 0x0044B370 - CConvoLinkedNode::~CConvoLinkedNode wrapper
 *
 * Forwards to the base-class "constructor" which just resets the type
 * field; no resources are owned at this level.
 */
static __attribute__((unused)) void
CConvoLinkedNode_DestructorWrapper(CConvoLinkedNode *this)
{
	CConvoLinkedNode_Constructor(this);
}

/*
 * 0x0044B390 - CConvoLinkedNode_String::`scalar deleting destructor'
 *
 * ORPHANED: zero callers in the binary.
 */
static __attribute__((unused)) void *
CConvoLinkedNode_String_ScalarDelete(CConvoLinkedNode *this, int flags)
{
	CConvoLinkedNode_String_Destructor(this);
	if (flags & 1)
		OperatorDelete(this);
	return NULL;
}

/*
 * 0x0044B3C0 - CDefine::Init
 *
 * Prepends a new CDefine node to g_ConvoMgr.defineList.
 */
CDefine *
CDefine_Init(CDefine *this, const char *name, const char *value)
{
	this->next = g_ConvoMgr.defineList;
	g_ConvoMgr.defineList = this;
	this->name = OperatorNew(strlen(name) + 1);
	strcpy(this->name, name);
	this->value = OperatorNew(strlen(value) + 1);
	strcpy(this->value, value);
	return this;
}

/*
 * 0x0044B445 - CDefine::~CDefine
 *
 * Frees the define's name and value strings.
 */
void
CDefine_Destructor(CDefine *this)
{
	OperatorDelete(this->name);
	OperatorDelete(this->value);
}

/*
 * 0x0044B47B - CDefine::Substitute
 *
 * If outBuf matches this define's name, replaces it with the value.
 */
void
CDefine_Substitute(CDefine *this, char *outBuf, int maxLen)
{
	if (strcasecmp(outBuf, this->name) == 0) {
		if ((int)strlen(this->value) < maxLen)
			strcpy(outBuf, this->value);
	}
}

/*
 * 0x0044B4C7 - CConvoLinkedNode::ClearFields
 *
 * Clears the fragment, define, and default-fragment pointers.
 */
static __attribute__((unused)) CConversationManager *
CConvoLinkedNode_ClearFields(CConversationManager *this)
{
	this->fragmentList = NULL;
	this->defineList = NULL;
	this->defaultFrag = NULL;
	return this;
}

/*
 * 0x0044B4F3 - CConversationManager::Cleanup
 *
 * Destroys and frees every fragment and define node.
 */
static __attribute__((unused)) void
CConversationManager_Cleanup(CConversationManager *this)
{
	CFragment *frag;
	CDefine *def;

	while (this->fragmentList != NULL) {
		frag = this->fragmentList;
		this->fragmentList = frag->next;
		if (frag != NULL)
			CMapNode_ScalarDtor(frag, 1);
	}

	while (this->defineList != NULL) {
		def = this->defineList;
		this->defineList = def->next;
		if (def != NULL)
			CMapIterator_ScalarDtor(def, 1);
	}
}

/*
 * 0x0044B590 - CConversationManager::startup
 *
 * Loads defines and fragments, then caches the default fallback fragment.
 */
void
CConversationManager_startup(CConversationManager *this)
{
	if (CConversationManager_LoadDefines(this) != 0)
		return;
	if (CConversationManager_LoadFragments(this) != 0)
		return;
	this->defaultFrag = CConversationManager_FindFragByName(this, "Britannia_Default");
}

/*
 * 0x0044B5C6 - CConversationManager::FindOrCreateFragment
 *
 * Returns the existing fragment with that name or allocates and
 * initializes a new one when absent.
 */
CFragment *
CConversationManager_FindOrCreateFragment(CConversationManager *this, const char *name)
{
	CFragment *frag;
	void *mem;

	frag = CConversationManager_FindFragByName(this, name);
	if (frag != NULL)
		return frag;

	mem = OperatorNew(sizeof(CFragment));
	if (mem != NULL)
		frag = CFragment_Init(mem, name);
	else
		frag = NULL;
	return frag;
}

/*
 * 0x0044B659 - CConversationManager::ReadToken
 *
 * Reads the next token (string, brace/comma, number, or identifier),
 * skipping whitespace and comments, and expands #define macros.
 * Returns the updated position or NULL on EOF/error.
 */
char *
CConversationManager_ReadToken(CConversationManager *this, char *pos, char *outBuf, int maxLen)
{
	int len;
	int savedLine;
	char *savedPos;
	CDefine *def;

	if (pos == NULL)
		return NULL;

restart:
	while (*pos != '\0' && isspace((unsigned char)*pos)) {
		if (*pos == '\n') {
			this->lineNumber++;
			this->lineStartPtr = pos + 1;
		}
		pos++;
	}

	if (*pos == '/' && *(pos + 1) == '/') {
		while (*pos != '\0' && *pos != '\n')
			pos++;
		goto restart;
	}

	if (*pos == '/' && *(pos + 1) == '*') {
		savedLine = this->lineNumber;
		savedPos = pos;
		pos += 2;
		while (*pos != '\0') {
			if (*pos == '*' && *(pos + 1) == '/')
				break;
			if (*pos == '\n') {
				this->lineNumber++;
				this->lineStartPtr = pos + 1;
			}
			pos++;
		}
		if (*pos == '\0') {
			this->lineNumber = savedLine;
			this->lineStartPtr = savedPos;
			return NULL;
		}
		pos += 2;
		goto restart;
	}

	len = 0;

	if (*pos == '"') {
		savedLine = this->lineNumber;
		savedPos = pos;
		outBuf[len] = *pos;
		len++;
		pos++;
		while (*pos != '\0') {
			if (*pos == '"' && *(pos - 1) != '\\')
				break;
			if (*pos == '\n') {
				this->lineNumber++;
				this->lineStartPtr = pos + 1;
			}
			if (len == maxLen - 1)
				return NULL;
			// Binary keeps spaces but drops tabs/CRs inside strings.
			if (!isspace((unsigned char)*pos) || *pos == ' ') {
				outBuf[len] = *pos;
				len++;
			}
			pos++;
		}
		if (*pos == '\0') {
			this->lineNumber = savedLine;
			this->lineStartPtr = savedPos;
			return NULL;
		}
		outBuf[len] = *pos;
		len++;
		pos++;
		outBuf[len] = '\0';
		return pos;
	}

	if (*pos == '{' || *pos == '}' || *pos == ',') {
		outBuf[0] = *pos;
		pos++;
		outBuf[1] = '\0';
		return pos;
	}

	if (*pos == '-' || isdigit((unsigned char)*pos)) {
		outBuf[len] = *pos;
		len++;
		pos++;
		while (*pos != '\0' && isdigit((unsigned char)*pos)) {
			if (len == maxLen - 1)
				return NULL;
			outBuf[len] = *pos;
			len++;
			pos++;
		}
		outBuf[len] = '\0';
		return pos;
	}

	if (*pos == '#') {
		outBuf[len] = *pos;
		len++;
		pos++;
	}
	while (*pos != '\0' && (isalnum((unsigned char)*pos) || *pos == '_')) {
		if (len == maxLen - 1)
			return NULL;
		outBuf[len] = *pos;
		len++;
		pos++;
	}

	if (len == 0) {
		if (*pos == '\0' || *pos == 0x1A)
			return NULL;
		// Binary formats an error into a discarded local buffer.
		{
			char errBuf[128];
			sprintf(errBuf, "Unrecognized character '%c' (%d)", *pos, *pos);
		}
		return NULL;
	}

	outBuf[len] = '\0';

	def = this->defineList;
	while (def != NULL) {
		CDefine_Substitute(def, outBuf, maxLen);
		def = def->next;
	}
	return pos;
}

/*
 * 0x0044BB10 - CConversationManager::ReadFileToBuffer
 *
 * Reads an entire file into a heap buffer. Returns NULL if missing or empty.
 */
char *
CConversationManager_ReadFileToBuffer(CConversationManager *this, const char *filename)
{
	FILE *fp;
	int size;
	char *buf;

	USED(this);

	fp = FileManager_OpenByType(0x30, filename, "rb");
	if (fp == NULL)
		return NULL;
	fseek_ServerSide(fp, 0, SEEK_END);
	size = ftell_ServerSide(fp);
	if (size == 0) {
		fclose_ServerSide(fp);
		return NULL;
	}
	fseek_ServerSide(fp, 0, SEEK_SET);
	buf = OperatorNew(size + 1);
	fread_ServerSide(buf, 1, size, fp);
	fclose_ServerSide(fp);
	buf[size] = '\0';
	return buf;
}

/*
 * 0x0044BBC4 - CConversationManager::LoadDefines
 *
 * Parses define.lst entries of the form "#define NAME VALUE" with VALUE
 * in 1..5. Returns 0 on success, 1 on error.
 */
int
CConversationManager_LoadDefines(CConversationManager *this)
{
	char *fileData;
	char *pos;
	int err;
	char buf[128];
	char buf2[128];
	int val;
	CDefine *node;

	fileData = CConversationManager_ReadFileToBuffer(this, "define.lst");
	if (fileData == NULL)
		return 1;

	this->lineNumber = 1;
	this->lineStartPtr = fileData;
	this->filename = "define.lst";

	pos = fileData;
	err = 0;

	while (pos != NULL) {
		pos = CConversationManager_ReadToken(this, pos, buf, 128);
		if (pos == NULL)
			break;
		if (strcasecmp(buf, "#define") != 0) {
			err = 1;
			break;
		}

		pos = CConversationManager_ReadToken(this, pos, buf, 128);
		pos = CConversationManager_ReadToken(this, pos, buf2, 128);
		if (pos == NULL) {
			err = 1;
			break;
		}

		if (!isalnum((unsigned char)buf[0])) {
			err = 1;
			break;
		}
		if (buf2[0] != '-' && !isdigit((unsigned char)buf2[0])) {
			err = 1;
			break;
		}

		val = atoi(buf2);
		if (val < 1 || val > 5) {
			err = 1;
			break;
		}

		node = OperatorNew(sizeof(CDefine));
		if (node != NULL)
			CDefine_Init(node, buf, buf2);
	}

	OperatorDelete(fileData);
	return err;
}

/*
 * 0x0044BDE2 - CConversationManager::LoadFragments
 *
 * Parses fragment.lst (up to 127 entries) and loads each referenced .frg
 * file. Returns 0 on success, 1 on error.
 *
 * MODIFIED: the binary aborts the entire conversation load on the first
 * malformed .frg file, so one bad file silently disables all NPC
 * conversation. Instead, a fragment that fails to parse is logged and
 * skipped, and the remaining fragments still load.
 */
int
CConversationManager_LoadFragments(CConversationManager *this)
{
	char *fileData;
	char *pos;
	int err;
	int fragCount;
	char *fragNames[128];
	char buf[128];
	int i;

	fileData = CConversationManager_ReadFileToBuffer(this, "fragment.lst");
	if (fileData == NULL)
		return 1;

	this->lineNumber = 1;
	this->lineStartPtr = fileData;
	this->filename = "fragment.lst";

	pos = fileData;
	err = 0;
	fragCount = 0;

	while (pos != NULL) {
		pos = CConversationManager_ReadToken(this, pos, buf, 128);
		if (pos == NULL)
			break;
		if (strcasecmp(buf, "#fragment") != 0) {
			err = 1;
			break;
		}
		pos = CConversationManager_ReadToken(this, pos, buf, 128);
		if (pos == NULL) {
			err = 1;
			break;
		}
		if (!isalnum((unsigned char)buf[0])) {
			err = 1;
			break;
		}
		if (fragCount == 127) {
			err = 1;
			break;
		}
		fragNames[fragCount] = OperatorNew(strlen(buf) + 1);
		strcpy(fragNames[fragCount], buf);
		fragCount++;
	}

	OperatorDelete(fileData);

	if (err != 0) {
		for (i = 0; i < fragCount; i++)
			OperatorDelete(fragNames[i]);
		return 1;
	}

	for (i = 0; i < fragCount; i++) {
		if (CConversationManager_LoadFragment(this, fragNames[i]) != 0) {
			// MODIFIED: log the malformed fragment; the loop keeps
			// loading the remaining fragments.
			char errMsg[160];
			snprintf(errMsg, sizeof(errMsg), "fragment '%s' failed to parse near line %d - skipping", fragNames[i], this->lineNumber);
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "convo", "misc", errMsg);
		}
	}

	for (i = 0; i < fragCount; i++)
		OperatorDelete(fragNames[i]);

	return err;
}

/*
 * 0x0044C0BB - CConversationManager::LoadFragment
 *
 * Parses one .frg file (#fragment / #key / #sophistication / #attitude /
 * #notoriety directives and response strings). Returns 0 on success.
 */
int
CConversationManager_LoadFragment(CConversationManager *this, const char *fragName)
{
	char path[0x300];
	char *fileData;
	char *pos;
	int err;
	int depth;

	char tokenBuf[1024];
	char nameBuf[128];
	char comma1[16];
	char comma2[16];
	char lookupNameBuf[128];
	char fragNameBuf[128];
	char levelBuf[16];

	int typeStack[0x40];
	int lineStack[0x40]; // Binary writes but never reads these entries.
	CConvoLinkedNode *responseLists[0x40];

	CFragment *curFrag;
	int i;

	sprintf(path, "%s.frg", fragName);

	fileData = CConversationManager_ReadFileToBuffer(this, path);
	if (fileData == NULL)
		return 1;

	this->lineNumber = 1;
	this->lineStartPtr = fileData;
	this->filename = path;

	pos = fileData;
	err = 0;
	depth = 0;
	curFrag = NULL;

	memset(responseLists, 0, sizeof(responseLists));

	while (pos != NULL) {
		pos = CConversationManager_ReadToken(this, pos, tokenBuf, 1024);
		if (pos == NULL)
			break;

		if (depth == 0) {
			if (strcasecmp(tokenBuf, "#fragment") != 0) {
				err = 1;
				break;
			}
			pos = CConversationManager_ReadToken(this, pos, nameBuf, 128);
			pos = CConversationManager_ReadToken(this, pos, comma1, 16);
			pos = CConversationManager_ReadToken(this, pos, lookupNameBuf, 128);
			pos = CConversationManager_ReadToken(this, pos, comma2, 16);
			pos = CConversationManager_ReadToken(this, pos, fragNameBuf, 128);
			pos = CConversationManager_ReadToken(this, pos, levelBuf, 16);
			if (pos == NULL) {
				err = 1;
				break;
			}
			if (comma1[0] != ',' || comma2[0] != ',') {
				err = 1;
				break;
			}
			if (levelBuf[0] != '{') {
				err = 1;
				break;
			}
			if (!isalnum((unsigned char)nameBuf[0]) || !isalnum((unsigned char)lookupNameBuf[0]) || !isalnum((unsigned char)fragNameBuf[0])) {
				err = 1;
				break;
			}

			typeStack[depth] = 0;
			lineStack[depth] = this->lineNumber;
			depth++;

			curFrag = CConversationManager_FindOrCreateFragment(this, fragNameBuf);

		} else if (tokenBuf[0] == '}') {
			depth--;
			FreeConvoNodeList(responseLists[depth]);
			responseLists[depth] = NULL;

		} else if (tokenBuf[0] == ',') {
			continue;

		} else if (tokenBuf[0] == '#') {
			if (strcasecmp(tokenBuf, "#key") == 0) {
				typeStack[depth] = 1;

				while (1) {
					pos = CConversationManager_ReadToken(this, pos, nameBuf, 128);
					if (pos == NULL) {
						err = 1;
						break;
					}
					if (nameBuf[0] == ',')
						continue;
					if (nameBuf[0] == '{')
						break;
					if (nameBuf[0] != '"') {
						err = 1;
						break;
					}
					nameBuf[strlen(nameBuf) - 1] = '\0';

					{
						CConvoLinkedNode *node;
						node = OperatorNew(sizeof(CConvoLinkedNode));
						if (node != NULL)
							CConvoLinkedNode_String_Constructor(node, &nameBuf[1], &responseLists[depth]);
					}
				}
				if (err)
					break;

				if (responseLists[depth] == NULL) {
					err = 1;
					break;
				}

			} else if (strcasecmp(tokenBuf, "#sophistication") == 0) {
				typeStack[depth] = 2;

				pos = CConversationManager_ReadToken(this, pos, nameBuf, 128);
				if (pos == NULL) {
					err = 1;
					break;
				}

				{
					CConvoLinkedNode *node;
					int val;

					if (strcasecmp(nameBuf, "low") == 0)
						val = 1;
					else if (strcasecmp(nameBuf, "medium") == 0)
						val = 2;
					else if (strcasecmp(nameBuf, "high") == 0)
						val = 3;
					else {
						err = 1;
						break;
					}

					node = OperatorNew(sizeof(CConvoLinkedNode));
					if (node != NULL)
						CConvoLinkedNode_Int_Constructor(node, val, &responseLists[depth]);
				}

				pos = CConversationManager_ReadToken(this, pos, levelBuf, 16);
				if (pos == NULL || levelBuf[0] != '{') {
					err = 1;
					break;
				}

			} else if (strcasecmp(tokenBuf, "#attitude") == 0 || strcasecmp(tokenBuf, "#notoriety") == 0) {
				int type = (tolower((unsigned char)tokenBuf[1]) != 'a') ? 4 : 3;
				typeStack[depth] = type;

				while (1) {
					pos = CConversationManager_ReadToken(this, pos, nameBuf, 128);
					if (pos == NULL) {
						err = 1;
						break;
					}

					if (nameBuf[0] != '-' && !isdigit((unsigned char)nameBuf[0])) {
						err = 1;
						break;
					}

					{
						CConvoLinkedNode *node;
						node = OperatorNew(sizeof(CConvoLinkedNode));
						if (node != NULL)
							CConvoLinkedNode_Int_Constructor(node, atoi(nameBuf), &responseLists[depth]);
					}

					pos = CConversationManager_ReadToken(this, pos, nameBuf, 128);
					if (nameBuf[0] == '{')
						break;
					if (nameBuf[0] != ',') {
						err = 1;
						break;
					}
				}
				if (err)
					break;

			} else {
				err = 1;
				break;
			}

			for (i = 1; i < depth; i++) {
				if (typeStack[i] == typeStack[depth]) {
					err = 1;
					break;
				}
			}
			if (err)
				break;

			lineStack[depth] = this->lineNumber;
			depth++;
			if (depth == 0x3F) {
				err = 1;
				break;
			}

		} else if (tokenBuf[0] == '"') {
			for (i = 1; i < depth; i++) {
				if (typeStack[i] == 1)
					break;
			}
			if (i == depth) {
				err = 1;
				break;
			}

			tokenBuf[strlen(tokenBuf) - 1] = '\0';

			CFragment_AddResponseToGrid(curFrag, &tokenBuf[1], depth, typeStack, responseLists);

		} else {
			err = 1;
			break;
		}
	}

	if (err == 0 && depth != 0) {
		char errBuf[128];
		sprintf(errBuf, "Unclosed block starting line %d.", lineStack[depth - 1]);
	}

	for (i = 1; i < 0x40; i++)
		FreeConvoNodeList(responseLists[i]);

	OperatorDelete(fileData);
	return err;
}

/*
 * 0x0044CC58 - CConversationManager::FindFragByName
 *
 * Case-insensitive linear scan of the fragment list for the named
 * fragment, or NULL if not found.
 */
CFragment *
CConversationManager_FindFragByName(CConversationManager *this, const char *name)
{
	CFragment *node;

	node = this->fragmentList;
	while (node != NULL) {
		if (strcasecmp(node->name, name) == 0)
			return node;
		node = node->next;
	}
	return NULL;
}

/*
 * 0x0044CCA0 - CConversationManager::SubstituteResponse
 *
 * Expands a response template into g_substituteOutBuf, handling %0 text
 * expansion, _name_ variables, #opt1/opt2# NPC-gender and $opt1/opt2$
 * about-gender selectors, [var] script callouts, and '\\' escapes.
 *
 * FIXED: The binary passes NULL to strcat when an NPC has no _town_ or
 * _job_ assigned; NULL checks guard those strcats.
 */
static char *
CConversationManager_SubstituteResponse(CConversationManager *this, char *defineKey, CItem *npc, CItem *speaker, CItem *about, char *text, char *responseText)
{
	char *out;
	char *p;
	char localBuf[128];
	char *lp;
	int ch;

	USED(this);
	out = g_substituteOutBuf;

	while (*defineKey != '\0') {
		ch = (signed char)*defineKey;
		if (ch < 0x23 || ch > 0x5F)
			goto copy_literal;

		switch (ch) {
		case '#': {
			uint8_t sex = ((CMobile *)npc)->sex;
			if (sex != 0) {
				defineKey++;
				while (*defineKey != '\0' && *defineKey != '/' && *defineKey != '#')
					defineKey++;
				if (*defineKey == '/') {
					defineKey++;
					while (*defineKey != '\0' && *defineKey != '#') {
						*out++ = *defineKey;
						defineKey++;
					}
				}
				if (*defineKey != '\0')
					defineKey++;
			} else {
				defineKey++;
				while (*defineKey != '\0' && *defineKey != '/' && *defineKey != '#') {
					*out++ = *defineKey;
					defineKey++;
				}
				while (*defineKey != '\0') {
					if (*defineKey == '#')
						break;
					defineKey++;
				}
				if (*defineKey != '\0')
					defineKey++;
			}
			break;
		}
		case '$': {
			uint8_t sex = ((CMobile *)about)->sex;
			if (sex != 0) {
				defineKey++;
				while (*defineKey != '\0' && *defineKey != '/' && *defineKey != '$')
					defineKey++;
				if (*defineKey == '/') {
					defineKey++;
					while (*defineKey != '\0' && *defineKey != '$') {
						*out++ = *defineKey;
						defineKey++;
					}
				}
				if (*defineKey != '\0')
					defineKey++;
			} else {
				defineKey++;
				while (*defineKey != '\0' && *defineKey != '/' && *defineKey != '$') {
					*out++ = *defineKey;
					defineKey++;
				}
				while (*defineKey != '\0') {
					if (*defineKey == '$')
						break;
					defineKey++;
				}
				if (*defineKey != '\0')
					defineKey++;
			}
			break;
		}
		case '%':
			defineKey++;
			if (*defineKey == '0') {
				defineKey++;
				p = responseText;
				while (*p != '\0' && (isalpha((unsigned char)*p) || *p == '*'))
					p++;
				while (*p != '\0') {
					if (*p != '*')
						*out++ = *p;
					p++;
				}
			} else {
				*out++ = '%';
			}
			break;
		case '[': {
			defineKey++;
			lp = localBuf;
			while (*defineKey != '\0' && *defineKey != ']') {
				*lp++ = *defineKey;
				defineKey++;
			}
			if (*defineKey != '\0')
				defineKey++;
			*lp = '\0';

			g_ConvoReturnStr[0] = '\0';

			Entity_AttachScript((CItem *)npc, "convo", 0);

			Entity_ExecuteEvent(&npc->resourceEntity.entity, 0x23, localBuf, speaker->serial, text);

			CResourceEntity_RemoveScript((CItem *)npc, "convo");

			*out = '\0';
			strcpy(out, g_ConvoReturnStr);
			out += strlen(out);
			break;
		}
		case '\\':
			defineKey++;
			*out = *defineKey;
			out++;
			defineKey++;
			break;
		case '_': {
			defineKey++;
			lp = localBuf;
			while (*defineKey != '\0' && *defineKey != '_') {
				*lp++ = *defineKey;
				defineKey++;
			}
			if (*defineKey != '\0')
				defineKey++;
			*lp = '\0';
			*out = '\0';

			if (strcasecmp(localBuf, "name") == 0) {
				const char *name;
				name = ((const char *(*)(void *))VT_ENT_FN(&about->resourceEntity.entity, VT_GET_NAME))(about);
				strcat(out, name);
			} else if (strcasecmp(localBuf, "myname") == 0) {
				const char *name;
				name = ((const char *(*)(void *))VT_ENT_FN(&npc->resourceEntity.entity, VT_GET_NAME))(npc);
				strcat(out, name);
			} else if (strcasecmp(localBuf, "toname") == 0) {
				const char *name;
				name = ((const char *(*)(void *))VT_ENT_FN(&speaker->resourceEntity.entity, VT_GET_NAME))(speaker);
				strcat(out, name);
			} else if (strcasecmp(localBuf, "town") == 0) {
				char *town = ((CNPC *)npc)->npcTown;
				if (town != NULL)
					strcat(out, town);
			} else if (strcasecmp(localBuf, "job") == 0) {
				char *job = ((CNPC *)npc)->npcJob;
				if (job != NULL)
					strcat(out, job);
			}
			out += strlen(out);
			break;
		}
		default:
copy_literal:
			*out = *defineKey;
			out++;
			defineKey++;
			break;
		}
	}

	*out = '\0';
	return g_substituteOutBuf;
}

/*
 * 0x0044D38A - ComputeSophistication
 *
 * Maps NPC baseInt to Low(1) / Medium(2) / High(3).
 */
static int
ComputeSophistication(CMobile *npc)
{
	uint16_t baseInt = npc->baseInt;
	if (baseInt < 34)
		return 1;
	if (baseInt < 67)
		return 2;
	return 3;
}

/*
 * 0x0044D3C4 - ComputeAttitude
 *
 * Maps an NPC's loyalty value onto the 1..5 attitude bucket used to
 * pick conversation response variants.
 */
static int
ComputeAttitude(CMobile *npc)
{
	int val = CNPC_GetLoyalty((CNPC *)npc);
	if (val < -90)
		return 1;
	if (val > 90)
		return 5;
	if (val < -30)
		return 2;
	if (val > 30)
		return 4;
	return 3;
}

/*
 * 0x0044D410 - ComputeNotoriety
 *
 * Maps a player's notoriety value onto the 1..5 bucket used to pick
 * conversation response variants.
 */
static int
Convo_ComputeNotoriety(CPlayer *speaker)
{
	int val = CMobile_GetNotoriety(&speaker->mobile);
	if (val < -90)
		return 1;
	if (val > 90)
		return 5;
	if (val < -30)
		return 2;
	if (val > 30)
		return 4;
	return 3;
}

/*
 * 0x0044D45C - CConversationManager::InternalFindResponse
 *
 * Collects matching responses from each of the NPC's fragments (with
 * broadening passes when attitude/notoriety sit on boundary values),
 * falls back to the default fragment, picks one at random, and runs it
 * through SubstituteResponse.
 */
char *
CConversationManager_InternalFindResponse(CItem *npc, CItem *speaker, CItem *about, char *text)
{
	CFragmentList *container;
	CFragment *frag;
	int soph, attitude, notoriety;
	int attFlag, notFlag, filterMask;
	int attBroadened, notBroadened;
	int savedCount;
	int idx;

	if (!VT_IsNPC(npc))
		return &g_emptyStr1;
	if ((int)strlen(text) < 3)
		return &g_emptyStr1;

	g_responseCount = 0;
	g_speechText = text;

	soph = ComputeSophistication((CMobile *)npc);
	attitude = ComputeAttitude((CMobile *)npc);
	notoriety = Convo_ComputeNotoriety((CPlayer *)speaker);

	attFlag = 0;
	if (attitude == 2 || attitude == 4)
		attFlag = 1;
	notFlag = 0;
	if (notoriety == 2 || notoriety == 4)
		notFlag = 2;
	filterMask = attFlag | notFlag;

	attBroadened = attitude + attitude - 3;
	notBroadened = notoriety + notoriety - 3;

	container = ((CNPC *)npc)->convoFragList;
	if (container == NULL)
		return &g_emptyStr2;

	{
		StdPtrNode *stlIter, *stlBeginTemp, *stlEndTemp;
		StdPtrNode *stlEndCopy, *stlPostIncTemp;

		StdPtrIter_CopyConstructor(&stlIter, StdPtrList_Begin((StdPtrList *)container, &stlBeginTemp));
		for (;;) {
			StdPtrIter_CopyConstructor(&stlEndCopy, StdPtrList_End((StdPtrList *)container, &stlEndTemp));
			if (!(StdPtrIter_Neq(&stlIter, &stlEndCopy) & 0xFF))
				break;
			frag = CConversationManager_FindFragByName(&g_ConvoMgr, CString_GetData((CString *)StdPtrIter_Deref(&stlIter)));
			if (frag == NULL)
				goto next;

			savedCount = g_responseCount;

			CFragment_CollectResponses(frag, soph, attitude, notoriety);

			if (filterMask == 0)
				goto next;

			if (savedCount != g_responseCount)
				goto next;

			if (filterMask == 1) {
				CFragment_CollectResponses(frag, soph, attBroadened, notoriety);
			} else if (filterMask == 2) {
				CFragment_CollectResponses(frag, soph, attitude, notBroadened);
			} else if (filterMask == 3) {
				CFragment_CollectResponses(frag, soph, attBroadened, notoriety);
				CFragment_CollectResponses(frag, soph, attitude, notBroadened);
				CFragment_CollectResponses(frag, soph, attBroadened, notBroadened);
			}

next:
			StdPtrIter_PostInc(&stlIter, &stlPostIncTemp, 0);
		}
	}

	if (g_responseCount == 0 && g_ConvoMgr.defaultFrag != NULL)
		CFragment_CollectResponses(g_ConvoMgr.defaultFrag, soph, attitude, notoriety);

	if (g_responseCount == 0)
		return &g_emptyStr3;

	idx = GetRandomRange(0, g_responseCount - 1);

	return CConversationManager_SubstituteResponse(&g_ConvoMgr, g_responseKeys[idx], npc, speaker, about, text, g_responseTexts[idx]);
}
