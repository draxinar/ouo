/*
 * Wombat scripting engine - compile / load-time pipeline.
 *
 * Classifies statements, resolves handlers, pre-computes constant
 * expressions, and walks statement blocks into the tree form the
 * runtime executor consumes.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "container.h"
#include "convo.h"
#include "egg.h"
#include "filemanager.h"
#include "io.h"
#include "multi.h"
#include "nodepool.h"
#include "region.h"
#include "taglist.h"
#include "vg_pool.h"
#include "world.h"

// 0x0063E124 - g_ScriptParserState (only written, never read by binary)
static int g_ScriptParserState;

// Built-in handler table (moved before LookupHandler which references it)
#include "wombat_compile.h"
#include "packet_handler.h"
#include "wombat_builtins.inc"
#include "objvar.h"

static const char *ParseForward(const char *stream); // 0x0042809E
static const char *ParseInherits(const char *stream); // 0x00427FB9
static const char *ParseMember(const char *stream, int isForwardDecl); // 0x00427EEF
static const char *ParseTrigger(const char *stream); // 0x00427436
static const char *ParseFunction(const char *stream); // 0x0042715B
static void ScriptStringDB_InitWrapper(void); // 0x0042618B
static void CScriptManager_Destructor(CScriptManager *this); // 0x004260D8
static int CoerceToInt(ResultNode **chainPtr); // 0x004285DD
static int CoerceToStr(ResultNode **chainPtr); // 0x004289EE
static int CoerceToUStr(ResultNode **chainPtr); // 0x00428B57
static int CoerceToLoc(ResultNode **chainPtr); // 0x00428CC4
static int CoerceToObj(ResultNode **chainPtr); // 0x00428E11
static int CoerceToList(ResultNode **chainPtr); // 0x00428F5E
static int CoerceToUnknown(ResultNode **chainPtr); // 0x0042907B
static int TypeDispatch(char sigChar, ResultNode **chainPtr); // 0x0042916F
static int StoreTypesFromSig_Inner(const char *sig, ResultNode **chainPtr); // 0x0042924B
static int ClassifyStatement(const char *tokenBuf, CFuncScope *scope); // 0x0042A5E0
static CScriptCompiler *CScriptCompiler_ScalarDelete(CScriptCompiler *compiler, int flags); // 0x0042B180
static void ResultNode_Constructor(ResultNode *node); // 0x0042B301
static void ResultNode_Clear(ResultNode *node); // 0x0042B30C
static int ExtractEventParams(int eventType, va_list ap, EventParam *params); // 0x0042B951
static int DispatchEvent(CItem *entity, int eventType, va_list ap); // 0x0042B951
static int CoerceCheckVarRef(ResultNode **chainPtr, int targetType);
static int CoerceCheckHandler(ResultNode **chainPtr, int targetType, const char *oprlistSig, const char *getObjVarSig);
static int CoerceCheckFunc(ResultNode **chainPtr, int targetType);

/*
 * 0x00406A30 - CEntity::GetBodyType
 *
 * Returns the entity's bodyType (graphic ID).
 */
uint16_t
CEntity_GetBodyType(CItem *item)
{
	return item->resourceEntity.entity.bodyType;
}

/*
 * 0x00406A50 - Script_FireTransferEvent
 *
 * Fires Transfer event (0x41) on entity with target's serial and flag 0.
 */
void
Script_FireTransferEvent(CItem *entity, CItem *target)
{
	Entity_ExecuteEvent(&entity->resourceEntity.entity, Transfer, target->serial, 0);
}

/*
 * 0x00406A6C - Script_FireCheckTransferEvent
 *
 * Fires CheckTransfer event (0x40) on entity with target's serial and flag 1.
 */
void
Script_FireCheckTransferEvent(CItem *entity, CItem *target)
{
	Entity_ExecuteEvent(&entity->resourceEntity.entity, CheckTransfer, target->serial, 1);
}

/*
 * 0x00406AA0 - StoreSemiResult
 *
 * Appends a type-10 ResultNode delimiter to chain, marking the end
 * of a statement or expression for case-dispatch coercion.
 */
void
StoreSemiResult(ResultNode **chain)
{
	ResultNode *n;
	ResultNode **cur = chain;

	while (*cur != NULL)
		cur = &(*cur)->next;
	n = AllocResultNode();
	*cur = n;
	n->next = NULL;
	n->type = 10;
	n->value = 0;
	n->extra = 0;
}

/*
 * AppendResultNode - walk to end of chain, allocate and append a new node.
 *
 * This is the common pattern used by all Store* functions in the binary:
 * walk **chain until *cur == NULL, then allocate and link a new node.
 */
ResultNode *
AppendResultNode(ResultNode **chain)
{
	ResultNode **cur = chain;
	while (*cur != NULL)
		cur = &(*cur)->next;
	*cur = AllocResultNode();
	(*cur)->next = NULL;
	return *cur;
}

/*
 * 0x00406AFC - StoreTriggerVarResult
 *
 * Appends a type-2 (trigger var rvalue) node referring to value.
 */
void
StoreTriggerVarResult(ResultNode **chain, uintptr_t value)
{
	ResultNode *n = AppendResultNode(chain);
	n->type = 2;
	n->value = value;
	n->extra = 0;
}

/*
 * 0x00406B57 - StoreLVarRef
 *
 * Appends a type-3 (local var lvalue) node referring to value.
 * Used by ProcessAssignment for the LHS of an assignment.
 */
void
StoreLVarRef(ResultNode **chain, uintptr_t value)
{
	ResultNode *n = AppendResultNode(chain);
	n->type = 3;
	n->value = value;
	n->extra = 0;
}

/*
 * 0x00406BB2 - StoreLocalVarResult
 *
 * Appends a type-4 (local var rvalue) node referring to value.
 */
void
StoreLocalVarResult(ResultNode **chain, uintptr_t value)
{
	ResultNode *n = AppendResultNode(chain);
	n->type = 4;
	n->value = value;
	n->extra = 0;
}

/*
 * 0x00406C0D - StoreTVarRef
 *
 * Appends a type-5 (trigger var lvalue) node referring to value.
 * Used by ProcessAssignment for the LHS of a trigger-var assignment.
 */
void
StoreTVarRef(ResultNode **chain, uintptr_t value)
{
	ResultNode *n = AppendResultNode(chain);
	n->type = 5;
	n->value = value;
	n->extra = 0;
}

/*
 * 0x00406C68 - StoreIntLiteral
 *
 * Appends a type-6 (int literal) node carrying value.
 */
void
StoreIntLiteral(ResultNode **chain, uintptr_t value)
{
	ResultNode *n = AppendResultNode(chain);
	n->type = 6;
	n->value = value;
	n->extra = 0;
}

/*
 * 0x00406CC3 - StoreIdResult
 *
 * Appends a type-7 (string literal) node carrying a heap-allocated
 * CString built from tokenBuf. Strips the surrounding quotes when
 * present.
 *
 * FIXED: the binary reads buf[len-1] without first checking len > 0,
 * which loads buf[-1] on an empty string; we guard with len > 0.
 */
void
StoreIdResult(ResultNode **chain, const char *tokenBuf)
{
	ResultNode *n;
	char buf[2048];
	int len;
	CString *cs;

	if (tokenBuf[0] == '"')
		strcpy(buf, tokenBuf + 1);
	else
		strcpy(buf, tokenBuf);

	len = strlen(buf);
	if (len > 0 && buf[len - 1] == '"')
		buf[len - 1] = '\0';

	n = AppendResultNode(chain);
	n->type = 7;
	cs = (CString *)malloc(sizeof(CString));
	if (cs != NULL)
		CString_Constructor(cs, buf);
	n->value = (uintptr_t)cs;
	n->extra = 0;
}

/*
 * 0x00406E05 - StoreMemberResult
 *
 * Appends a type-8 (unicode string / member ref) node carrying a
 * heap-allocated CUString built from tokenBuf. Strips a leading
 * '"' or 'L"' and a trailing '"'.
 *
 * FIXED: the binary reads buf[len-1] without first checking len > 0,
 * which loads buf[-1] on an empty string; we guard with len > 0.
 */
void
StoreMemberResult(ResultNode **chain, const char *tokenBuf)
{
	ResultNode *n;
	char buf[2048];
	unsigned short wbuf[2048];
	int len;
	CUString *cus;

	// Strip leading '"' or 'L"'
	if (tokenBuf[0] == '"')
		strcpy(buf, tokenBuf + 1);
	else if (tokenBuf[0] == 'L' && tokenBuf[1] == '"')
		strcpy(buf, tokenBuf + 2);
	else
		strcpy(buf, tokenBuf);

	// Strip trailing '"'
	len = strlen(buf);
	if (len > 0 && buf[len - 1] == '"')
		buf[len - 1] = '\0';

	Hex2Wchar(buf, wbuf);

	n = AppendResultNode(chain);
	n->type = 8;
	cus = (CUString *)malloc(sizeof(CUString));
	if (cus != NULL)
		CUString_Constructor(cus, wbuf);
	n->value = (uintptr_t)cus;
	n->extra = 0;
}

/*
 * 0x00406F90 - StoreHandlerResult
 *
 * Compile-time helper invoked by ParseStatementBlock. Appends a
 * type-0 handler-reference node to the chain, then applies the
 * coercion needed by control-flow handlers (CASE, SWITCH, IF,
 * WHILE, ENDFOR, BREAK, ...) to rearrange the surrounding nodes.
 */
int
StoreHandlerResult(ResultNode **chain, const BuiltinHandlerEntry *handler, ResultNode *argChain)
{
	ResultNode **tailPtr;
	ResultNode *scan;
	ResultNode *walkPtr;
	int matchCount;
	uintptr_t *tmpNode;
	int idx;
	uintptr_t *walkNode;
	uintptr_t savedNode;
	const BuiltinHandlerEntry *nodeHandler;
	int loopVar;
	ResultNode *matches[130];

	tailPtr = chain;
	while (*tailPtr != NULL)
		tailPtr = &(*tailPtr)->next;

	*tailPtr = AllocResultNode();
	(*tailPtr)->next = NULL;
	(*tailPtr)->type = 0;
	(*tailPtr)->value = (uintptr_t)handler;

	// TK_CASE coercion (0x00406FE0-0x004070F0)
	// Scans chain for TK_SWITCH/TK_ENDSWITCH nodes, rearranges for
	// case/switch nesting.
	if (handler->handler == (uintptr_t)Handler_SWITCH) {
		matchCount = 0;
		scan = *chain;
		while (scan != NULL) {
			if (scan->type != 0) {
				scan = scan->next;
				continue;
			}
			nodeHandler = (const BuiltinHandlerEntry *)(uintptr_t)scan->value;
			if (nodeHandler->handler == (uintptr_t)Handler_CASE || nodeHandler->handler == (uintptr_t)Handler_ENDIF2) {
				matches[matchCount] = scan;
				matchCount++;
			}
			if (scan == *tailPtr)
				break;
			if (nodeHandler->handler == (uintptr_t)Handler_ENDIF2)
				matchCount -= 2;
			scan = scan->next;
		}

		if (matchCount == 0)
			return 0;

		tmpNode = (uintptr_t *)malloc(3 * sizeof(uintptr_t));
		tmpNode[0] = argChain->value;
		tmpNode[1] = (uintptr_t)*tailPtr;
		tmpNode[2] = ((ResultNode *)(uintptr_t)matches[matchCount - 1]->extra)->next->value;

		((ResultNode *)(uintptr_t)matches[matchCount - 1]->extra)->next->value = (uintptr_t)tmpNode;

		FreeResultNode(argChain);
		(*tailPtr)->extra = 0;
		return 1;
	}

	// TK_IF / TK_WHILE / TK_BREAK (0x004070F5-0x00407127)
	// Append type=6 (int literal 0) to argChain.
	if (handler->handler == (uintptr_t)Handler_IF || handler->handler == (uintptr_t)Handler_WHILE || handler->handler == (uintptr_t)Handler_FOR_BODY) {
		StoreIntLiteral(&argChain, 0);
		goto check_endwhile_continue;
	}

	// TK_SWITCH (0x0040712C-0x00407144)
	// Append type=10 (semi) node to argChain.
	if (handler->handler == (uintptr_t)Handler_CASE) {
		StoreSemiResult(&argChain);
		goto check_endwhile_continue;
	}

	// TK_ENDFOR coercion (0x00407149-0x00407267)
	// Looks up TK_ENDWHILE handler, replaces node's value,
	// scans for matching TK_WHILE/TK_ENDWHILE nodes,
	// rearranges chain.
	if (handler->handler == (uintptr_t)Handler_ENDFOR_NOP) {
		handler = LookupHandler("TK_ENDWHILE", "v");
		(*tailPtr)->value = (uintptr_t)handler;

		matchCount = 0;
		scan = *chain;
		while (scan != NULL) {
			if (scan->type != 0) {
				scan = scan->next;
				continue;
			}
			nodeHandler = (const BuiltinHandlerEntry *)(uintptr_t)scan->value;
			if (nodeHandler->handler == (uintptr_t)Handler_WHILE || nodeHandler->handler == (uintptr_t)Handler_ENDWHILE) {
				matches[matchCount] = scan;
				matchCount++;
			}
			if (scan == *tailPtr)
				break;
			if (nodeHandler->handler == (uintptr_t)Handler_ENDWHILE)
				matchCount -= 2;
			scan = scan->next;
		}

		// Move node after matches[matchCount-2] to just before
		// matches[matchCount-1]
		scan = matches[matchCount - 2]->next;
		matches[matchCount - 2]->next = scan->next;

		walkPtr = matches[matchCount - 2];
		do {
			walkPtr = walkPtr->next;
		} while (walkPtr->next != matches[matchCount - 1]);

		scan->next = walkPtr->next;
		walkPtr->next = scan;
		tailPtr = &scan->next;
	}

check_endwhile_continue:
	// TK_ENDWHILE / TK_CONTINUE coercion (0x0040726A-0x0040745A)
	if (handler->handler == (uintptr_t)Handler_ENDWHILE || handler->handler == (uintptr_t)Handler_ENDFOR) {
		matchCount = 0;
		scan = *chain;
		while (scan != NULL) {
			if (scan->type != 0) {
				scan = scan->next;
				continue;
			}
			nodeHandler = (const BuiltinHandlerEntry *)(uintptr_t)scan->value;
			if (nodeHandler->handler == (uintptr_t)Handler_WHILE || nodeHandler->handler == (uintptr_t)Handler_ENDWHILE ||
			        nodeHandler->handler == (uintptr_t)Handler_CASE || nodeHandler->handler == (uintptr_t)Handler_ENDIF2) {
				matches[matchCount] = scan;
				matchCount++;
			} else if (nodeHandler->handler == (uintptr_t)Handler_FOR_BODY && handler->handler == (uintptr_t)Handler_ENDWHILE) {
				matches[matchCount] = scan;
				matchCount++;
			} else if (scan == *tailPtr) {
				matches[matchCount] = scan;
				matchCount++;
			}
			if (scan == *tailPtr)
				break;
			if (nodeHandler->handler == (uintptr_t)Handler_ENDWHILE || nodeHandler->handler == (uintptr_t)Handler_ENDIF2) {
				matchCount -= 2;
				while (matchCount != 0 && ((const BuiltinHandlerEntry *)(uintptr_t)matches[matchCount]->value)->handler == (uintptr_t)Handler_FOR_BODY) {
					matchCount--;
				}
			}
			scan = scan->next;
		}

		if (handler->handler == (uintptr_t)Handler_ENDWHILE) {
			// TK_ENDWHILE post-processing
			idx = matchCount - 2;
			while (((const BuiltinHandlerEntry *)(uintptr_t)matches[idx]->value)->handler == (uintptr_t)Handler_FOR_BODY)
				idx--;

			StoreIntLiteral(&argChain, 0);
			argChain->value = (uintptr_t)matches[idx];

			((ResultNode *)(uintptr_t)matches[idx]->extra)->next->value = (uintptr_t)*tailPtr;

			idx++;
			while (idx < matchCount - 1) {
				((ResultNode *)(uintptr_t)matches[idx]->extra)->value = (uintptr_t)*tailPtr;
				idx++;
			}
		} else {
			// TK_CONTINUE post-processing
			StoreIntLiteral(&argChain, 0);
			argChain->value = (uintptr_t)matches[matchCount - 2];
		}
		goto epilogue;
	}

	// TK_ENDSWITCH coercion (0x0040745F-0x00407634)
	if (handler->handler == (uintptr_t)Handler_ENDIF2) {
		matchCount = 0;
		scan = *chain;
		while (scan != NULL) {
			if (scan->type != 0) {
				scan = scan->next;
				continue;
			}
			nodeHandler = (const BuiltinHandlerEntry *)(uintptr_t)scan->value;
			if (nodeHandler->handler == (uintptr_t)Handler_CASE || nodeHandler->handler == (uintptr_t)Handler_ENDIF2 ||
			        nodeHandler->handler == (uintptr_t)Handler_WHILE || nodeHandler->handler == (uintptr_t)Handler_ENDWHILE ||
			        nodeHandler->handler == (uintptr_t)Handler_FOR_BODY) {
				matches[matchCount] = scan;
				matchCount++;
			}
			if (scan == *tailPtr)
				break;
			if (nodeHandler->handler == (uintptr_t)Handler_ENDIF2 || nodeHandler->handler == (uintptr_t)Handler_ENDWHILE) {
				matchCount -= 2;
				while (matchCount != 0 && ((const BuiltinHandlerEntry *)(uintptr_t)matches[matchCount]->value)->handler == (uintptr_t)Handler_FOR_BODY) {
					matchCount--;
				}
			}
			scan = scan->next;
		}

		// Skip TK_BREAK entries from the end
		loopVar = 2;
		while (((const BuiltinHandlerEntry *)(uintptr_t)matches[matchCount - loopVar]->value)->handler == (uintptr_t)Handler_FOR_BODY) {
			((ResultNode *)(uintptr_t)matches[matchCount - loopVar]->extra)->value = (uintptr_t)*tailPtr;
			loopVar++;
		}

		// Get saved chain pointer
		savedNode = ((ResultNode *)(uintptr_t)matches[matchCount - loopVar]->extra)->next->value;
		walkNode = (uintptr_t *)savedNode;

		// Walk sentinel chain looking for -666 marker
		while (walkNode != NULL) {
			if (walkNode[0] == 0xFFFFFD66)
				break;
			walkNode = (uintptr_t *)walkNode[2];
		}

		if (walkNode == NULL) {
			// Allocate new sentinel node
			walkNode = (uintptr_t *)malloc(3 * sizeof(uintptr_t));
			walkNode[2] = savedNode;
			walkNode[0] = 0xFFFFFD66;
			walkNode[1] = (uintptr_t)matches[matchCount - 1];
			((ResultNode *)(uintptr_t)matches[matchCount - loopVar]->extra)->next->value = (uintptr_t)walkNode;
		}

		goto epilogue;
	}

	// TK_ELSE / TK_ENDIF coercion (0x00407639-0x00407797)
	if (handler->handler == (uintptr_t)Handler_ELSE || handler->handler == (uintptr_t)Handler_ENDIF) {
		matchCount = 0;
		scan = *chain;
		while (scan != NULL) {
			if (scan->type != 0) {
				scan = scan->next;
				continue;
			}
			nodeHandler = (const BuiltinHandlerEntry *)(uintptr_t)scan->value;
			if (nodeHandler->handler == (uintptr_t)Handler_IF || nodeHandler->handler == (uintptr_t)Handler_ELSE || nodeHandler->handler == (uintptr_t)Handler_ENDIF) {
				matches[matchCount] = scan;
				matchCount++;
			}
			if (scan == *tailPtr)
				break;
			if (nodeHandler->handler == (uintptr_t)Handler_ENDIF) {
				matchCount -= 2;
				if (((const BuiltinHandlerEntry *)(uintptr_t)matches[matchCount]->value)->handler == (uintptr_t)Handler_ELSE)
					matchCount--;
			}
			scan = scan->next;
		}

		if (matchCount < 2)
			return 0;

		if (handler->handler == (uintptr_t)Handler_ELSE) {
			// TK_ELSE
			StoreIntLiteral(&argChain, 0);
			((ResultNode *)(uintptr_t)matches[matchCount - 2]->extra)->next->value = (uintptr_t)*tailPtr;
		} else {
			// TK_ENDIF
			nodeHandler = (const BuiltinHandlerEntry *)(uintptr_t)matches[matchCount - 2]->value;
			if (nodeHandler->handler == (uintptr_t)Handler_ELSE) {
				((ResultNode *)(uintptr_t)matches[matchCount - 2]->extra)->value = (uintptr_t)*tailPtr;
			} else {
				((ResultNode *)(uintptr_t)matches[matchCount - 2]->extra)->next->value = (uintptr_t)*tailPtr;
			}
		}
		goto epilogue;
	}

epilogue:
	(*tailPtr)->extra = (uintptr_t)argChain;
	return 1;
}

/*
 * 0x004077AB - StoreFuncCallResult
 *
 * Appends a type-1 (script function call) node carrying funcIndex
 * and argChain. The argument chain becomes owned by the new node
 * and is consumed when the function is later executed.
 */
void
StoreFuncCallResult(ResultNode **chain, int funcIndex, ResultNode *argChain)
{
	ResultNode *n = AppendResultNode(chain);
	n->type = 1; /* function reference */
	n->value = (uint32_t)funcIndex;
	n->extra = (uintptr_t)argChain;
}

/*
 * 0x00407805 - StoreGotoResult
 *
 * Appends a type=9 node for goto labels. Allocates a copy of the
 * label string (malloc + strcpy). Used by STMT_GOTO processing.
 */
void
StoreGotoResult(ResultNode **chain, const char *label)
{
	ResultNode *n = AppendResultNode(chain);

	n->type = 9;
	n->value = (uintptr_t)malloc(strlen(label) + 1);
	strcpy((char *)n->value, label);
	n->extra = 0;
}

/*
 * 0x0040788A - ResolveGotoLabels
 *
 * Post-compilation pass that resolves goto/label nodes in a ResultNode chain.
 * Walks the chain collecting type=9 nodes into parallel arrays of name strings
 * and node pointers. Label definitions (names ending with ':') get their value
 * cleared. Goto references get converted to type=0 handler nodes with a
 * TK_GOTO handler and a type=6 child node pointing to the target label node.
 *
 * Returns 1 if all gotos resolved, 0 if any label not found.
 */
int
ResolveGotoLabels(ResultNode *head)
{
	char *values[128]; // ebp-0x400
	ResultNode *nodes[128]; // ebp-0x200
	int count; // ebp-0x484
	int i; // ebp-0x488
	int j; // ebp-0x48C
	char buf[128]; // ebp-0x480
	char *p; // ebp-0x490

	// Pass 1: collect all type=9 (goto/label) nodes
	count = 0;
	while (head != NULL) {
		if (head->type == 9) {
			values[count] = (char *)head->value;
			nodes[count] = head;
			count++;
		}
		head = head->next;
	}

	// Pass 2: resolve each entry
	for (i = 0; i < count; i++) {
		// Check if last char is ':' (label definition)
		int len = strlen(values[i]);
		if (values[i][len - 1] == ':') {
			// Label definition: clear the value field
			nodes[i]->value = 0;
			continue;
		}

		// Goto reference: build "name:" and search for matching label
		strcpy(buf, values[i]);
		strcat(buf, ":");

		for (j = 0; j < count; j++) {
			if (strcmp(values[j], buf) == 0)
				break;
		}

		if (j == count) {
			// Label not found
			sprintf(buf, "bad label '%s'", values[i]);
			break;
		}

		// Resolve: convert type=9 node to type=0 (handler) with TK_GOTO
		nodes[i]->type = 0;
		nodes[i]->value = (uintptr_t)LookupHandler("TK_GOTO", "vi");

		// Allocate child node: type=6 (int literal) pointing to label node
		nodes[i]->extra = (uintptr_t)AllocResultNode();
		{
			ResultNode *child = (ResultNode *)(uintptr_t)nodes[i]->extra;
			child->type = 6;
			child->value = (uintptr_t)nodes[j];
			child->next = NULL;
			child->extra = 0;
		}
	}

	// Cleanup: free all collected label name strings
	for (j = 0; j < count; j++) {
		p = values[j];
		free(p);
	}

	// Return 1 if all resolved (i reached count), 0 otherwise
	if (i == count)
		return 1;
	return 0;
}

/*
 * 0x00425CC0 - ScriptCreation_BeginDefer
 *
 * Sets g_deferScriptCreation = 1, resets count and list head to 0.
 */
void
ScriptCreation_BeginDefer(void)
{
	g_deferScriptCreation = 1;
	g_scriptCreationCount = 0;
	g_scriptCreationListHead = NULL;
}

/*
 * Coercion helper - check var ref type (cases 2-5).
 *
 * Returns 0 when the variable referenced by the current node has
 * typeId == targetType, advancing *chainPtr; returns 1 on mismatch.
 */
static int
CoerceCheckVarRef(ResultNode **chainPtr, int targetType)
{
	CNamedScopeEntry *var = (CNamedScopeEntry *)(uintptr_t)(*chainPtr)->value;
	if (var->typeId == targetType) {
		*chainPtr = (*chainPtr)->next;
		return 0;
	}
	return 1;
}

/*
 * Coercion helper - check handler result type (case 0) with
 * oprlist/getObjVar specialization for WTYPE_UNKNOWN handlers.
 *
 * Returns 0 when the handler's return type matches targetType.
 * For WTYPE_UNKNOWN handlers, rebinds Opr_listuni and
 * Script_getObjVar to the typed variants matching the requested
 * sigs and treats them as a match. Returns 1 on a real mismatch.
 */
static int
CoerceCheckHandler(ResultNode **chainPtr, int targetType, const char *oprlistSig, const char *getObjVarSig)
{
	const BuiltinHandlerEntry *handler;
	int typeId;

	handler = (const BuiltinHandlerEntry *)(uintptr_t)(*chainPtr)->value;
	typeId = GetVarType(handler);

	if (typeId == targetType) {
		*chainPtr = (*chainPtr)->next;
		return 0;
	}
	if (typeId == WTYPE_UNKNOWN) {
		// oprlist: binary 0x0040E5F6 = Opr_listuni
		if (handler->handler == (uintptr_t)Opr_listuni) {
			(*chainPtr)->value = (uintptr_t)LookupHandler("oprlist", oprlistSig);
		}
		// getObjVar: binary 0x004115D2 = Script_getObjVar
		else if (getObjVarSig != NULL && handler->handler == (uintptr_t)Script_getObjVar) {
			(*chainPtr)->value = (uintptr_t)LookupHandler("getObjVar", getObjVarSig);
		}
		*chainPtr = (*chainPtr)->next;
		return 0;
	}
	return 1;
}

/*
 * Coercion helper - check script function return type (case 1).
 *
 * Returns 0 when the function referenced by the current node has
 * return type == targetType, advancing *chainPtr; returns 1 on
 * mismatch.
 */
static int
CoerceCheckFunc(ResultNode **chainPtr, int targetType)
{
	CScript *script;
	CFunction *func;
	int funcIdx;
	int retType;

	script = g_ScriptCompiler->script;
	funcIdx = (int)(*chainPtr)->value;
	func = (CFunction *)((char *)script->funcList.array + funcIdx * (int)sizeof(CFunction));
	retType = GetFuncRetType(func);
	if (retType != targetType)
		return 1;
	*chainPtr = (*chainPtr)->next;
	return 0;
}

/*
 * 0x00425CE3 - ScriptCreation_FlushDeferred
 *
 * Clears g_deferScriptCreation. If g_scriptCreationListHead is NULL,
 * returns. Otherwise collects all blocks from the linked list into a
 * CVector (newest-first), then iterates entries 0..count-1 starting
 * from the oldest block (last in vector). For each entry, looks up
 * the entity by serial and fires Entity_ExecuteEvent(entity, 0x0F,
 * param). Every 256 entries, frees the exhausted block and advances
 * to the next one. Clears g_scriptCreationListHead, destroys vector.
 *
 * FIXED: the binary only frees a block on the `(i & 0xFF) == 0xFF`
 * boundary, so when `g_scriptCreationCount` is not a multiple of 256
 * the final partial block is processed but never released. Free the
 * remaining block (if any) after the loop.
 */
void
ScriptCreation_FlushDeferred(void)
{
	CVector vec;
	static const char vecType = 0;
	ScriptCreationBlock *node;
	int idx;
	int i;
	CItem *entity;
	ScriptCreationBlock *oldBlock;
	int slot;

	g_deferScriptCreation = 0;

	if (g_scriptCreationListHead == NULL)
		return;

	CVector_Constructor(&vec, &vecType);

	node = g_scriptCreationListHead;
	while (node != NULL) {
		CVector_PushBack(&vec, (uintptr_t)node);
		node = node->next;
	}

	idx = (int)CVector_GetCount(&vec) - 1;
	node = (ScriptCreationBlock *)((uintptr_t *)vec.begin)[idx];

	for (i = 0; i < g_scriptCreationCount; i++) {
		slot = i & 0xFF;

		entity = CWorld_FindBySerial(g_World, node->serials[slot]);

		if (entity != NULL) {
			Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x0F, node->params[slot]);
		}

		if ((i & 0xFF) == 0xFF) {
			oldBlock = (ScriptCreationBlock *)((uintptr_t *)vec.begin)[idx];
			free(oldBlock);
			idx--;
			node = (ScriptCreationBlock *)((uintptr_t *)vec.begin)[idx];
		}
	}

	if (idx >= 0) {
		oldBlock = (ScriptCreationBlock *)((uintptr_t *)vec.begin)[idx];
		free(oldBlock);
	}

	g_scriptCreationListHead = NULL;
	CVector_Destructor(&vec);
}

/*
 * 0x00425E4B - ScriptCreation_RecordDeferred
 *
 * Records an entity serial and script instance for deferred creation
 * event firing. Allocates 0x804-byte blocks as needed (every 256
 * entries), links them into g_scriptCreationListHead. Stores the
 * entity's serial at block->serials[slot] and scriptInstance at
 * block->params[slot].
 */
void
ScriptCreation_RecordDeferred(CItem *entity, uint32_t scriptInstance)
{
	int slot;
	ScriptCreationBlock *block;

	if ((g_scriptCreationCount & 0xFF) == 0) {
		block = (ScriptCreationBlock *)OperatorNew(sizeof(ScriptCreationBlock));
		block->next = g_scriptCreationListHead;
		g_scriptCreationListHead = block;
	}

	slot = g_scriptCreationCount & 0xFF;
	g_scriptCreationListHead->serials[slot] = entity->serial;
	g_scriptCreationListHead->params[slot] = scriptInstance;
	g_scriptCreationCount++;
}

/*
 * 0x00425ECE - TriggerEdit_SetStringProp
 *
 * Cdecl wrapper: calls CResourceEntity::DetachScript (0x004CDEAC)
 * which removes named tag from entity->tagList.
 */
void
TriggerEdit_SetStringProp(CItem *ent, const char *data)
{
	CResourceEntity_DetachScript(ent, data);
}

/*
 * 0x00425EDF - ObjVar_SetStr
 *
 * Sets (type, value) under name on entity, then destroys name. The
 * binary takes name by value; the C port passes a pointer that the
 * callee destructs to keep the same ownership semantics.
 */
void
ObjVar_SetStr(CItem *entity, CString *name, int type, uintptr_t value)
{
	const char *nameStr;

	nameStr = CString_GetData(name);
	CEntity_SetObjVar(entity, nameStr, type, value);
	CString_Destructor(name);
}

/*
 * 0x00425F34 - Entity_AttachScript
 *
 * Loads (or finds) the named script class, allocates an instance, binds it
 * to entity, and either records the creation event for deferred firing or
 * fires it immediately. Returns NULL on success or an error message string
 * on failure.
 */
const char *
Entity_AttachScript(CItem *entity, const char *scriptName, int fireCreation)
{
	CScript *scriptClass;
	ScriptAttachNode *instance;

	scriptClass = CScriptManager_FindOrLoad(&g_ScriptManager, scriptName);
	if (scriptClass == NULL)
		return "Failed to get script class";

	if (CResourceEntity_HasScriptClass(entity, scriptClass))
		return "Script already attached of class";

	instance = CScriptManager_CreateInstance(&g_ScriptManager, scriptClass);
	if (instance == NULL)
		return "Failed to get script instance";

	CScriptInstance_AttachToEntity(instance, entity);

#ifdef DEBUG_UPDATEREGION
	fprintf(stderr, "AttachScript: 0x%08X '%s' -> OK (trigHandlers[0x10]=%p)\n", entity->serial, scriptName, scriptClass->trigHandlers[0x10]);
#endif

	if (fireCreation) {
		if (g_deferScriptCreation) {
			ScriptCreation_RecordDeferred(entity, (uintptr_t)instance);
		} else {
			Entity_ExecuteEvent(&entity->resourceEntity.entity, 0x0F, (uintptr_t)instance);
		}
	}

	return NULL;
}

/*
 * 0x00425FD2 - ValidateInWorld
 *
 * For body types flagged in g_ValidateInWorldBitmap, attaches a
 * script named after the body type id and fires its creation
 * event. Returns 0 when the script destroyed the entity, 1
 * otherwise. The demo build leaves the bitmap empty, so this
 * always returns 1.
 */
int
ValidateInWorld(CItem *item)
{
	uint32_t serial;
	uint16_t artID;
	char nameBuf[16];
	CScript *script;

	serial = item->serial;
	artID = item->resourceEntity.entity.bodyType;

	// Check bitmap: if bit not set, item is valid
	if ((g_ValidateInWorldBitmap[artID >> 5] & (1 << (artID & 0x1F))) == 0)
		return 1;

	// Bit is set: look up script by artID string
	sprintf(nameBuf, "%d", artID);
	script = CScriptManager_FindOrLoad(&g_ScriptManager, nameBuf);
	if (script == NULL)
		return 1;

	if (CResourceEntity_HasScriptClass(item, script))
		return 1;

	// Attach script with fireCreation = 1
	Entity_AttachScript(item, nameBuf, 1);

	// Re-validate: script may have destroyed the entity
	if (CWorld_FindBySerial(g_World, serial) != (CItem *)item)
		return 0;

	return 1;
}

/*
 * 0x00426097 - CScriptManager::Init
 *
 * Resets the script manager: clears the loaded-script list head,
 * zeroes the validate-in-world bitmap, and clears the intern
 * string hash buckets.
 */
void
CScriptManager_Init(CScriptManager *this)
{
	this->head = NULL;
	memset(g_ValidateInWorldBitmap, 0, 0x800);
	memset(this->internBuckets, 0, sizeof(this->internBuckets));
}

/*
 * 0x004260D8 - CScriptManager::~CScriptManager
 *
 * No-op destructor (registered via atexit but does nothing).
 */
static __attribute__((unused)) void
CScriptManager_Destructor(CScriptManager *this)
{
	USED(this);
}

/*
 * 0x004260E3 - CScriptManager::AddScript
 *
 * Inserts script at the head of the manager's loaded-scripts list.
 */
void
CScriptManager_AddScript(CScriptManager *mgr, CScript *script)
{
	script->nextLoaded = mgr->head;
	mgr->head = script;
}

/*
 * 0x00426106 - CScriptManager::FindOrLoadScript
 *
 * Returns the loaded script with the given name, loading it from
 * disk on first use. Tracks recursion depth in g_ScriptLoadCount.
 */
CScript *
CScriptManager_FindOrLoad(CScriptManager *mgr, const char *name)
{
	CScript *script;

	// Walk linked list searching by name
	for (script = mgr->head; script != NULL; script = script->nextLoaded) {
		if (strcmp(script->name, name) == 0)
			return script;
	}

	// Not found - load from file
	g_ScriptLoadCount++;
	script = CScriptManager_LoadScript(mgr, name);
	g_ScriptLoadCount--;

	return script;
}

/*
 * 0x0042618B - ScriptStringDB_InitWrapper
 *
 * Static initializer that constructs the global script string
 * database vector.
 */
static __attribute__((unused)) void
ScriptStringDB_InitWrapper(void)
{
	char typeByte = 0;
	CVector_Constructor((CVector *)&g_ScriptStringDB, &typeByte);
}

/*
 * 0x004261BB - CScriptManager::LoadScript
 *
 * Reads "<name>.m" from the script directory, parses it via
 * ParseScriptOuter, and registers the result with the manager. On
 * the first load it also primes the SDB by reading sdb.txt.
 * Returns NULL on read or parse failure.
 */
CScript *
CScriptManager_LoadScript(CScriptManager *mgr, const char *name)
{
	char filename[1024];
	FILE *fp;
	long fileSize;
	char *bytecode;
	CScript *script;

	int oldSdbLoaded = g_SdbLoaded;
	g_SdbLoaded++;
	if (oldSdbLoaded == 0) {
		CScriptStringDB_Load(&g_ScriptStringDB, "../.rundir/scripts/sdb.txt");
	}

	sprintf(filename, "%s.m", name);
	fp = FileManager_OpenByType(0x31, filename, "rb");
	if (fp == NULL)
		return NULL;

	fseek_ServerSide(fp, 0, SEEK_END);
	fileSize = ftell_ServerSide(fp);
	fseek_ServerSide(fp, 0, SEEK_SET);

	if (fileSize == -1) {
		fclose_ServerSide(fp);
		return NULL;
	}

	bytecode = (char *)OperatorNew(fileSize + 1);
	fread_ServerSide(bytecode, 1, (int)fileSize, fp);
	bytecode[fileSize] = '\0';
	fclose_ServerSide(fp);

	script = ParseScriptOuter(bytecode, name);

	OperatorDelete(bytecode);

	if (script != NULL) {
		CScriptManager_AddScript(mgr, script);
		return script;
	}

	return NULL;
}

/*
 * 0x00426344 - CScriptManager::CreateInstance
 *
 * Allocates a fresh ScriptAttachNode from the script-node pool and
 * binds it to scriptClass. Returns the new instance.
 */
ScriptAttachNode *
CScriptManager_CreateInstance(CScriptManager *mgr, CScript *scriptClass)
{
	USED(mgr);
	return ScriptNodePool_AllocAndConstruct(scriptClass);
}

/*
 * 0x0042635D - CScriptManager::InternString
 *
 * Returns a stable pointer to a copy of s, deduplicating against
 * a 256-bucket hash table indexed by the string's first character.
 */
const char *
CScriptManager_InternString(CScriptManager *mgr, const char *s)
{
	struct InternNode {
		struct InternNode *next;
		char str[4];
	} *node;
	int bucket;
	size_t slen;

	bucket = (signed char)s[0];
	node = (struct InternNode *)mgr->internBuckets[bucket];

	// Walk linked list searching for match
	while (node != NULL) {
		if (strcmp(node->str, s) == 0)
			return node->str;
		node = node->next;
	}

	// Not found - allocate new node: next ptr + strlen + 1 (NUL)
	slen = strlen(s);
	node = (struct InternNode *)malloc(sizeof(void *) + slen + 1);
	node->next = (struct InternNode *)mgr->internBuckets[bucket];
	strcpy(node->str, s);
	mgr->internBuckets[bucket] = node;

	return node->str;
}

/*
 * 0x00426E20 - CScriptCompiler::CScriptCompiler (CScriptCompiler constructor)
 *
 * Pushes the compiler onto the global compiler stack with empty
 * script and bytecode references.
 */
void
CScriptCompiler_Constructor(CScriptCompiler *compiler)
{
	compiler->next = g_ScriptCompiler;
	compiler->prev = NULL;
	if (compiler->next != NULL)
		compiler->next->prev = compiler;
	g_ScriptCompiler = compiler;
	compiler->script = NULL;
	compiler->flags = 1;
	compiler->bytecode = NULL;
}

/*
 * 0x00426E7D - CScriptCompiler::~CScriptCompiler (CScriptCompiler destructor)
 *
 * Unlinks the compiler from the global compiler stack.
 */
void
CScriptCompiler_Destructor(CScriptCompiler *compiler)
{
	if (compiler->next != NULL)
		compiler->next->prev = compiler->prev;
	if (compiler->prev != NULL)
		compiler->prev->next = compiler->next;
	else
		g_ScriptCompiler = compiler->next;
}

/*
 * 0x00426EC1 - ParseScriptOuter (CScriptManager::ParseScript wrapper)
 *
 * Forwards to ParseScriptInner.
 */
CScript *
ParseScriptOuter(const char *bytecode, const char *name)
{
	return ParseScriptInner(bytecode, name);
}

/*
 * 0x00426ED6 - ParseScriptInner (CScriptManager::ParseScriptInner)
 *
 * Compiles a script's bytecode into a freshly allocated CScript by
 * dispatching each top-level declaration (function, trigger,
 * member, inherits, forward) until the tokenizer drains. Pushes a
 * CScriptCompiler onto the global compiler stack for the duration.
 */
CScript *
ParseScriptInner(const char *bytecode, const char *name)
{
	CScriptCompiler *compiler;
	CScript *script;
	char tokenBuf[400];

	compiler = (CScriptCompiler *)OperatorNew(sizeof(CScriptCompiler));
	if (compiler != NULL)
		CScriptCompiler_Constructor(compiler);

	g_ScriptCompiler->flags = 1;
	g_ScriptCompiler->bytecode = (char *)bytecode;
	strcpy(g_ScriptCompiler->name, name);

	script = (CScript *)OperatorNew(sizeof(CScript));
	if (script != NULL)
		CScript_Constructor(script, name);

	g_ScriptCompiler->script = script;
	g_ScriptParserState = 0;
	CScript_AddVar(script, "this", WTYPE_OBJ);

	for (;;) {
		bytecode = ScriptTokenizer_ReadToken(bytecode, tokenBuf);

		if (CompareTokenType(tokenBuf, TK_FUNCTION)) {
			bytecode = ParseFunction(bytecode);
		} else if (CompareTokenType(tokenBuf, TK_TRIGGER)) {
			bytecode = ParseTrigger(bytecode);
		} else if (CompareTokenType(tokenBuf, TK_MEMBER)) {
			bytecode = ParseMember(bytecode, 0);
		} else if (CompareTokenType(tokenBuf, TK_INHERITS)) {
			bytecode = ParseInherits(bytecode);
		} else if (CompareTokenType(tokenBuf, TK_FORWARD)) {
			bytecode = ParseForward(bytecode);
		} else if (tokenBuf[0] == '\0') {
			script = g_ScriptCompiler->script;
			if (g_ScriptCompiler != NULL)
				CScriptCompiler_ScalarDelete(g_ScriptCompiler, 1);
			return script;
		}
	}
}

/*
 * 0x0042715B - ParseFunction
 *
 * Parses a function declaration: return type, name, parameter list,
 * then registers each parameter in a fresh CFuncScope and parses
 * the body. Returns the cursor past the function. The compiler also
 * computes (and discards) a needNewScope flag that the binary
 * never reads.
 */
static const char *
ParseFunction(const char *stream)
{
	char typeBuf[128];
	char nameBuf[128];
	char sigBuf[128];
	char paramNamesBuf[132];
	char lbraceBuf[128];
	char paramName[128];
	CFunction *func;
	CFuncScope *scope;
	CFunction *parentFunc;
	const char *namePtr;
	const char *sigPtr;
	int paramLen;
	int typeId;
	int paramTypeId;
	int needNewScope;

	stream = ScriptTokenizer_ReadToken(stream, typeBuf);
	typeId = GetTypeId(typeBuf);
	sigBuf[0] = g_WombatTypeCodes[typeId];
	stream = ScriptTokenizer_ReadToken(stream, nameBuf);
	stream = ParseFunctionParams(stream, sigBuf, paramNamesBuf);
	stream = ScriptTokenizer_ReadToken(stream, lbraceBuf);

	func = CScript_AddFunction(g_ScriptCompiler->script, nameBuf, sigBuf);
	if (func == NULL)
		return NULL;

	// Dead computation - needNewScope is set but never read
	if (func->scope != NULL) {
		needNewScope = 0;
		if (g_ScriptCompiler->script->parent == NULL) {
			needNewScope = 1;
		} else {
			parentFunc = CScript_FindFunction(g_ScriptCompiler->script->parent, nameBuf);
			if (parentFunc == NULL) {
				needNewScope = 1;
			} else if (parentFunc->scope != func->scope) {
				needNewScope = 1;
			}
		}
		USED(needNewScope);
	}

	scope = (CFuncScope *)OperatorNew(sizeof(CFuncScope));
	if (scope != NULL)
		CFuncScope_Constructor(scope);

	func->scope = scope;

	namePtr = paramNamesBuf;
	sigPtr = (const char *)func->sig + 1;
	while (*sigPtr != '\0') {
		paramTypeId = SigCharToTypeId(*sigPtr);
		paramLen = 0;
		while (*namePtr != '\0' && *namePtr != '|') {
			paramName[paramLen++] = *namePtr++;
		}
		if (*namePtr == '|')
			namePtr++;
		paramName[paramLen] = '\0';
		AddVarToScope(func->scope, paramTypeId, paramName);
		sigPtr++;
	}

	stream = ParseStatementBlock(stream, func->scope, GetFuncRetType(func));
	return stream;
}

/*
 * 0x00427436 - ParseTrigger
 *
 * Parses a trigger declaration: an optional numeric filter, the
 * event name, an optional parenthesised filter string, then the
 * body. Registers per-event parameter variables (driven by
 * g_TriggerParamTable, which mirrors the binary's switch payloads)
 * in a fresh CFuncScope and parses the statement block.
 */
static const char *
ParseTrigger(const char *stream)
{
	char nameBuf[128];
	char tokenBuf[128];
	char filterStr[128];
	int filter;
	int eventIndex;
	int flags;
	CTrigger *trig;
	CFuncScope *scope;
	int i;
	int val;
	size_t len;

	stream = ScriptTokenizer_ReadToken(stream, nameBuf);

	if (CompareTokenType(nameBuf, T_BYTE) || CompareTokenType(nameBuf, T_WORD) || CompareTokenType(nameBuf, T_DWORD)) {
		filter = GetInlineInt(nameBuf);
		stream = ScriptTokenizer_ReadToken(stream, nameBuf);
	} else {
		filter = 0x3E8;
	}

	flags = 0;
	for (i = 0; i < TRIGGER_EVENT_COUNT; i++) {
		if (strcmp(nameBuf, g_TriggerEventNames[i]) == 0)
			break;
	}
	eventIndex = i;

	stream = ScriptTokenizer_ReadToken(stream, tokenBuf);

	if (CompareTokenType(tokenBuf, SM_LPAREN)) {
		stream = ScriptTokenizer_ReadToken(stream, filterStr);

		if (CompareTokenType(filterStr, T_BYTE) || CompareTokenType(filterStr, T_WORD) || CompareTokenType(filterStr, T_DWORD)) {
			val = GetInlineInt(filterStr);
			sprintf(filterStr, "%d", val);
		}

		stream = ScriptTokenizer_ReadToken(stream, tokenBuf);
		stream = ScriptTokenizer_ReadToken(stream, tokenBuf);
	} else {
		filterStr[0] = '\0';
	}

	if (filterStr[0] != '\0' && filterStr[0] == '"') {
		len = strlen(filterStr);
		filterStr[len - 1] = '\0';
		trig = CScript_AddTrigger(g_ScriptCompiler->script, filter, flags, eventIndex, filterStr + 1);
	} else {
		trig = CScript_AddTrigger(g_ScriptCompiler->script, filter, flags, eventIndex, filterStr);
	}

	if (trig == NULL)
		return NULL;

	scope = (CFuncScope *)OperatorNew(sizeof(CFuncScope));
	if (scope != NULL)
		CFuncScope_Constructor(scope);

	trig->scope = scope;

	// Binary: 71-case switch dispatching to AddVarToScope sequences.
	// Replaced by g_TriggerParamTable lookup which holds the same
	// (name, typeId) sequences encoded as data.
	if (eventIndex < TRIGGER_EVENT_COUNT) {
		for (i = 0; i < g_TriggerParamTable[eventIndex].count; i++) {
			AddVarToScope(trig->scope, g_TriggerParamTable[eventIndex].params[i].typeId, g_TriggerParamTable[eventIndex].params[i].name);
		}
	}

	stream = ParseStatementBlock(stream, trig->scope, 0);

	return stream;
}

/*
 * 0x00427EEF - ParseMember
 *
 * Parses a member declaration of the form "type name;". When
 * isForwardDecl is zero, registers the variable on the current
 * compile script; otherwise just consumes the tokens.
 */
static const char *
ParseMember(const char *stream, int isForwardDecl)
{
	char typeBuf[128];
	char nameBuf[128];
	char semiBuf[128];
	int typeId;

	stream = ScriptTokenizer_ReadToken(stream, typeBuf);

	for (typeId = 0; typeId < 7; typeId++) {
		if (CompareTokenType(typeBuf, g_TypeTokenIds[typeId]))
			break;
	}

	stream = ScriptTokenizer_ReadToken(stream, nameBuf);
	stream = ScriptTokenizer_ReadToken(stream, semiBuf);

	if (isForwardDecl == 0)
		CScript_AddVar(g_ScriptCompiler->script, nameBuf, typeId);

	return stream;
}

/*
 * 0x00427FB9 - ParseInherits
 *
 * Parses an "inherits ParentName;" clause: loads the parent script,
 * copies its function list, named scope, and 71 trigger-handler
 * slots into the current script, and stores it as the parent.
 */
static const char *
ParseInherits(const char *stream)
{
	char parentName[256];
	char semiBuf[128];
	CScript *parent;
	CScript *current;
	int i;

	stream = ScriptTokenizer_ReadToken(stream, parentName);
	stream = ScriptTokenizer_ReadToken(stream, semiBuf);
	parent = CScriptManager_FindOrLoad(&g_ScriptManager, parentName);
	current = g_ScriptCompiler->script;

	CFuncList_Copy(&current->funcList, &parent->funcList);
	CNamedScope_Copy(&current->namedScope, &parent->namedScope);

	for (i = 0; i < BINARY_TRIGGER_COUNT; i++)
		current->trigHandlers[i] = parent->trigHandlers[i];

	current->parent = parent;

	return stream;
}

/*
 * 0x0042809E - ParseForward
 *
 * Parses a forward declaration "type name(params);" and registers
 * the function prototype on the current compile script.
 */
static const char *
ParseForward(const char *stream)
{
	char typeBuf[128];
	char nameBuf[128];
	char sigBuf[128];
	char semiBuf[128];
	int typeId;

	stream = ScriptTokenizer_ReadToken(stream, typeBuf);
	typeId = GetTypeId(typeBuf);
	sigBuf[0] = g_WombatTypeCodes[typeId];
	stream = ScriptTokenizer_ReadToken(stream, nameBuf);
	stream = ParseParamSignature(stream, sigBuf);
	stream = ScriptTokenizer_ReadToken(stream, semiBuf);
	CScript_AddFunction(g_ScriptCompiler->script, nameBuf, sigBuf);
	return stream;
}

/*
 * 0x00428149 - ParseFunctionParams
 *
 * Like ParseParamSignature, but also captures parameter names into
 * a pipe-delimited string. Used by ParseFunction to build both the
 * type signature and the parameter name list.
 *
 * For "function int foo(int x, string y)":
 *   sigBuf   = "iis"  (return 'i', param 'i', param 's')
 *   namesBuf = "x|y|"
 */
const char *
ParseFunctionParams(const char *stream, char *sigBuf, char *paramNamesBuf)
{
	char lparenBuf[128];
	char typeBuf[128];
	char paramNameBuf[128];
	char delimBuf[128];
	int paramIndex = 0;
	int typeId;

	// Initialize names buffer to empty
	*paramNamesBuf = '\0';

	// Read SM_LPAREN '('
	stream = ScriptTokenizer_ReadToken(stream, lparenBuf);

	while (stream != NULL && *stream != '\0') {
		// Read type keyword token
		stream = ScriptTokenizer_ReadToken(stream, typeBuf);

		// Check for SM_RPAREN - no params or end
		if (CompareTokenType(typeBuf, SM_RPAREN)) {
			sigBuf[paramIndex + 1] = '\0';
			return stream;
		}

		// Read parameter name
		stream = ScriptTokenizer_ReadToken(stream, paramNameBuf);

		// Read delimiter (SM_COMMA or SM_RPAREN)
		stream = ScriptTokenizer_ReadToken(stream, delimBuf);

		// Convert type keyword to sig char
		typeId = GetTypeId(typeBuf);
		if (typeId < 7) /* binary uses < 7, not < WTYPE_COUNT */
			sigBuf[paramIndex + 1] = g_WombatTypeCodes[typeId];

		// Append name + "|" to paramNamesBuf
		strcat(paramNamesBuf, paramNameBuf);
		strcat(paramNamesBuf, "|");
		paramIndex++;

		// Check if delimiter was SM_RPAREN - end of params
		if (CompareTokenType(delimBuf, SM_RPAREN)) {
			sigBuf[paramIndex + 1] = '\0';
			return stream;
		}
		// Otherwise SM_COMMA - continue
	}

	return NULL;
}

/*
 * 0x0042829B - ParseParamSignature
 *
 * Reads parameter types from bytecode stream and builds a type
 * signature string. The sigBuf[0] is already set to the return type
 * char; this function appends parameter type chars starting at sigBuf[1].
 *
 * Bytecode layout: SM_LPAREN type1 name1 SM_COMMA type2 name2 SM_RPAREN
 * Variable names are read and discarded. Type keywords are converted
 * to sig chars via GetTypeId + g_WombatTypeCodes.
 *
 * Returns updated stream pointer, or NULL on error.
 */
const char *
ParseParamSignature(const char *stream, char *sigBuf)
{
	char lparenBuf[128];
	char typeBuf[128];
	char paramBuf[128];
	int paramIndex = 0;
	int typeId;

	// Read SM_LPAREN '('
	stream = ScriptTokenizer_ReadToken(stream, lparenBuf);

	// Main loop: read type, optional name, until SM_RPAREN
	while (stream != NULL && *stream != '\0') {
		// Read type keyword token
		stream = ScriptTokenizer_ReadToken(stream, typeBuf);

		// Check for SM_RPAREN - end of params
		if (CompareTokenType(typeBuf, SM_RPAREN)) {
			sigBuf[paramIndex + 1] = '\0';
			return stream;
		}

		// Read next token (could be var name or comma/rparen)
		stream = ScriptTokenizer_ReadToken(stream, paramBuf);

		/*
		 * If it's not SM_COMMA(4) or SM_RPAREN(3), it's a var name.
		 * Skip it and read the actual delimiter. */
		if (!CompareTokenType(paramBuf, SM_COMMA) && !CompareTokenType(paramBuf, SM_RPAREN)) {
			stream = ScriptTokenizer_ReadToken(stream, paramBuf);
		}

		// Convert type keyword to sig char
		typeId = GetTypeId(typeBuf);
		sigBuf[paramIndex + 1] = g_WombatTypeCodes[typeId];
		paramIndex++;

		// Check if delimiter was SM_RPAREN - end of params
		if (CompareTokenType(paramBuf, SM_RPAREN)) {
			sigBuf[paramIndex + 1] = '\0';
			return stream;
		}
		// Otherwise was SM_COMMA - continue
	}

	return NULL;
}

/*
 * 0x004283E4 - ScriptTokenizer_ReadToken
 *
 * Reads the next token from the bytecode stream, decodes it into a
 * human-readable form in outBuf, and returns the cursor past the
 * consumed token.
 *
 * Token decoding:
 *   - NULL stream pointer: returns NULL.
 *   - NULL byte at stream: EOF, zero-fills outBuf (8 bytes).
 *   - T_STR (0x3A): string literal from sdb - writes a leading '"'
 *     and concatenates one or more sdb entries, dropping each
 *     entry's leading '"' so only the final closing '"' remains.
 *   - T_ID (0x41): identifier from sdb - copies the named entry.
 *   - T_BYTE / T_WORD / T_DWORD: copy 3 / 4 / 6 bytes of inline data.
 *   - Default: 16-bit word copy of two raw bytes.
 *
 * The binary's dead "copy bytes until null or space" loop is
 * unreachable and omitted.
 */
const char *
ScriptTokenizer_ReadToken(const char *stream, char *outBuf)
{
	uint16_t sdbIndex;
	const char *str;
	char *writePos;
	int len;

	if (stream == NULL)
		return NULL;

	// EOF: null byte at current position
	if (*stream == '\0') {
		memset(outBuf, 0, 8);
		return stream;
	}

	// T_STR (0x3A): string literal reference - chainable
	// Writes '"' at outBuf[0], advances writePos to outBuf+1. For
	// each T_STR token, reads sdb index, calls CScriptStringDB_Get,
	// does strcpy(writePos, str + 1) to skip the leading '"' from the
	// SDB entry. The trailing '"' from the last SDB entry serves as
	// the closing quote. For chained T_STR tokens, backs up 1 to
	// overwrite the trailing '"' before appending the next entry.
	if (ScriptTokenizer_MatchToken(stream, T_STR)) {
		outBuf[0] = '"';
		writePos = outBuf + 1;
		for (;;) {
			stream += 2; /* skip token */
			memcpy(&sdbIndex, stream, 2);
			stream += 2; /* skip sdb index */
			str = CScriptStringDB_Get(&g_ScriptStringDB, sdbIndex);
			strcpy(writePos, str + 1);
			if (!ScriptTokenizer_MatchToken(stream, T_STR))
				break;
			len = strlen(writePos);
			writePos = writePos + len - 1;
		}
		return stream;
	}

	// T_ID (0x41): identifier reference
	if (ScriptTokenizer_MatchToken(stream, T_ID)) {
		stream += 2; /* skip token */
		memcpy(&sdbIndex, stream, 2);
		stream += 2; /* skip sdb index */
		str = CScriptStringDB_Get(&g_ScriptStringDB, sdbIndex);
		strcpy(outBuf, str);
		return stream;
	}

	// T_BYTE (0x3C): 3 bytes inline data
	if (ScriptTokenizer_MatchToken(stream, T_BYTE)) {
		memcpy(outBuf, stream, 3);
		return stream + 3;
	}

	// T_WORD (0x3D): 4 bytes inline data
	if (ScriptTokenizer_MatchToken(stream, T_WORD)) {
		memcpy(outBuf, stream, 4);
		return stream + 4;
	}

	// T_DWORD (0x3E): 6 bytes inline data
	if (ScriptTokenizer_MatchToken(stream, T_DWORD)) {
		memcpy(outBuf, stream, 6);
		return stream + 6;
	}

	// Default: 16-bit word copy of 2 raw bytes
	memcpy(outBuf, stream, 2);
	return stream + 2;
}

/*
 * 0x004285DD - CoerceToInt
 *
 * Coerces the current ResultNode to WTYPE_INT. Int literals (type 6)
 * always pass; handler/func/var nodes pass when their declared type
 * matches; other literals fail.
 */
static int
CoerceToInt(ResultNode **chainPtr)
{
	ResultNode *node;
	if (*chainPtr == NULL)
		return 1;
	node = *chainPtr;
	switch (node->type) {
	case 0:
		return CoerceCheckHandler(chainPtr, WTYPE_INT, "ili", "ios");
	case 1:
		return CoerceCheckFunc(chainPtr, WTYPE_INT);
	case 2:
	case 3:
	case 4:
	case 5:
		return CoerceCheckVarRef(chainPtr, WTYPE_INT);
	case 6: /* int literal - always succeeds */
		*chainPtr = (*chainPtr)->next;
		return 0;
	default:
		return 1;
	}
}

/*
 * 0x004289EE - CoerceToStr
 *
 * Coerces the current ResultNode to WTYPE_STRING. String literals
 * (type 7) always pass; handler/func/var nodes pass when their
 * declared type matches; int literals and other types fail.
 */
static int
CoerceToStr(ResultNode **chainPtr)
{
	ResultNode *node;
	if (*chainPtr == NULL)
		return 1;
	node = *chainPtr;
	switch (node->type) {
	case 0:
		return CoerceCheckHandler(chainPtr, WTYPE_STRING, "sli", "sos");
	case 1:
		return CoerceCheckFunc(chainPtr, WTYPE_STRING);
	case 2:
	case 3:
	case 4:
	case 5:
		return CoerceCheckVarRef(chainPtr, WTYPE_STRING);
	case 7: /* string literal - always succeeds */
		*chainPtr = (*chainPtr)->next;
		return 0;
	default:
		return 1;
	}
}

/*
 * 0x00428B57 - CoerceToUStr
 *
 * Coerces the current ResultNode to WTYPE_USTRING. Type-8 unicode
 * literals always pass; handler/func/var nodes pass when their
 * declared type matches; literal types and unknown types fail.
 */
static int
CoerceToUStr(ResultNode **chainPtr)
{
	ResultNode *node;
	if (*chainPtr == NULL)
		return 1;
	node = *chainPtr;
	switch (node->type) {
	case 0:
		return CoerceCheckHandler(chainPtr, WTYPE_USTRING, "qli", "qos");
	case 1:
		return CoerceCheckFunc(chainPtr, WTYPE_USTRING);
	case 2:
	case 3:
	case 4:
	case 5:
		return CoerceCheckVarRef(chainPtr, WTYPE_USTRING);
	case 8: /* ustr member - always succeeds */
		*chainPtr = (*chainPtr)->next;
		return 0;
	default:
		return 1;
	}
}

/*
 * 0x00428CC4 - CoerceToLoc
 *
 * Coerces the current ResultNode to WTYPE_LOC. Handler/func/var
 * nodes pass when their declared type is location; literal types
 * fail.
 */
static int
CoerceToLoc(ResultNode **chainPtr)
{
	ResultNode *node;
	if (*chainPtr == NULL)
		return 1;
	node = *chainPtr;
	switch (node->type) {
	case 0:
		return CoerceCheckHandler(chainPtr, WTYPE_LOC, "cli", "cos");
	case 1:
		return CoerceCheckFunc(chainPtr, WTYPE_LOC);
	case 2:
	case 3:
	case 4:
	case 5:
		return CoerceCheckVarRef(chainPtr, WTYPE_LOC);
	default:
		return 1;
	}
}

/*
 * 0x00428E11 - CoerceToObj
 *
 * Coerces the current ResultNode to WTYPE_OBJ. Handler/func/var
 * nodes pass when their declared type is object; literal types
 * fail.
 */
static int
CoerceToObj(ResultNode **chainPtr)
{
	ResultNode *node;
	if (*chainPtr == NULL)
		return 1;
	node = *chainPtr;
	switch (node->type) {
	case 0:
		return CoerceCheckHandler(chainPtr, WTYPE_OBJ, "oli", "oos");
	case 1:
		return CoerceCheckFunc(chainPtr, WTYPE_OBJ);
	case 2:
	case 3:
	case 4:
	case 5:
		return CoerceCheckVarRef(chainPtr, WTYPE_OBJ);
	default:
		return 1;
	}
}

/*
 * 0x00428F5E - CoerceToList
 *
 * Coerces the current ResultNode to WTYPE_LIST. Handler/func/var
 * nodes pass when their declared type is list; literal types fail.
 * Unknown handlers can be specialised to oprlist but not to
 * getObjVar (no list-typed getObjVar variant exists).
 */
static int
CoerceToList(ResultNode **chainPtr)
{
	ResultNode *node;
	if (*chainPtr == NULL)
		return 1;
	node = *chainPtr;
	switch (node->type) {
	case 0:
		return CoerceCheckHandler(chainPtr, WTYPE_LIST, "lli", NULL);
	case 1:
		return CoerceCheckFunc(chainPtr, WTYPE_LIST);
	case 2:
	case 3:
	case 4:
	case 5:
		return CoerceCheckVarRef(chainPtr, WTYPE_LIST);
	default:
		return 1;
	}
}

/*
 * 0x0042907B - CoerceToUnknown
 *
 * Accepts any concrete-type ResultNode (1-8). Handler results
 * (type 0) are accepted only when their return type is concrete
 * (not WTYPE_UNKNOWN). Out-of-range types fail.
 */
static int
CoerceToUnknown(ResultNode **chainPtr)
{
	ResultNode *node;
	if (*chainPtr == NULL)
		return 1;
	node = *chainPtr;

	if (node->type == 0) {
		// Handler result: check that return type is concrete
		const BuiltinHandlerEntry *handler;
		handler = (const BuiltinHandlerEntry *)(uintptr_t)node->value;
		if (GetVarType(handler) == WTYPE_UNKNOWN)
			return 1;
		*chainPtr = (*chainPtr)->next;
		return 0;
	}
	if (node->type > 0 && node->type <= 8) {
		*chainPtr = (*chainPtr)->next;
		return 0;
	}
	return 1;
}

/*
 * 0x004290EF - SigCharToTypeId
 *
 * Maps a script type signature character to its WTYPE_* index.
 * Returns WTYPE_COUNT (8) when the character is not recognised.
 */
int
SigCharToTypeId(char c)
{
	int i;

	for (i = 0; i < WTYPE_COUNT; i++) {
		if (g_WombatTypeCodes[i] == c)
			return i;
	}
	return WTYPE_COUNT;
}

/*
 * 0x0042912B - GetTypeId
 *
 * Returns the WTYPE_* index for the type keyword in tokenBuf, or
 * WTYPE_COUNT (8) when the token does not match any type.
 */
int
GetTypeId(const char *tokenBuf)
{
	int i;

	for (i = 0; i < WTYPE_COUNT; i++) {
		if (CompareTokenType(tokenBuf, g_TypeTokenIds[i]))
			return i;
	}
	return WTYPE_COUNT;
}

/*
 * 0x0042916F - TypeDispatch
 *
 * Routes a single signature character to the matching CoerceTo*
 * function. 'i'/'s'/'q'/'c'/'o'/'l'/'u' map to int/str/ustr/loc/
 * obj/list/unknown coercion (case-insensitive: uppercase marks
 * lvalue parameters but uses the same coercion). 'v' fails because
 * void cannot be coerced to; unrecognised sig chars pass through.
 */
static int
TypeDispatch(char sigChar, ResultNode **chainPtr)
{
	int c;

	c = tolower((unsigned char)sigChar);

	switch (c) {
	case 'i':
		return CoerceToInt(chainPtr);
	case 's':
		return CoerceToStr(chainPtr);
	case 'q':
		return CoerceToUStr(chainPtr);
	case 'c':
		return CoerceToLoc(chainPtr);
	case 'o':
		return CoerceToObj(chainPtr);
	case 'l':
		return CoerceToList(chainPtr);
	case 'u':
		return CoerceToUnknown(chainPtr);
	case 'v':
		return 1; /* can't coerce to void */
	default:
		return 0; /* unknown sig char, no-op */
	}
}

/*
 * 0x0042924B - StoreTypesFromSig_Inner
 *
 * Coerces each ResultNode in *chainPtr against the parameter
 * portion of sig (skipping the leading return type). Stops at NUL
 * or '|'. Returns 0 on success, 1 when a coercion fails or the
 * chain runs out before the signature does.
 */
static int
StoreTypesFromSig_Inner(const char *sig, ResultNode **chainPtr)
{
	sig += 1; /* skip return type char */

	while (*sig != '\0' && *sig != '|') {
		if (TypeDispatch(*sig, chainPtr) != 0)
			return 1; /* coercion failed or no more nodes */
		sig++;
	}
	return 0;
}

/*
 * 0x0042929A - StoreTypesFromSig
 *
 * Coerces the argument chain against funcEntry's signature. Used
 * for script-defined function calls to annotate argument nodes.
 */
void
StoreTypesFromSig(CFunction *funcEntry, ResultNode *chain)
{
	StoreTypesFromSig_Inner(funcEntry->sig, &chain);
}

/*
 * ConsumeItemsFromContainer - static helper for destroyGeneric (0x004163AF)
 *
 * Recursively searches a container for items matching bodyType and consumes
 * up to 'remaining' quantity. Reduces stack amounts or destroys items
 * entirely. Returns number actually consumed.
 *
 * The binary equivalent does recursive container search plus stack
 * splitting; our version modifies amounts directly and broadcasts changes.
 */

/*
 * 0x004292B2 - BuildSignature
 *
 * Writes a type signature string to sigBuf describing chain: the
 * leading character is 'u' (unknown return type) and each
 * subsequent character is the type code resolved for one argument
 * node. Returns sigBuf.
 */
char *
BuildSignature(char *sigBuf, ResultNode *chain)
{
	int pos = 1;
	int flag;

	sigBuf[0] = 'u';

	while (chain != NULL) {
		int typeIdx = ResolveResultType(g_ScriptCompiler->script, chain, &flag);
		sigBuf[pos] = g_WombatTypeCodes[typeIdx];
		chain = chain->next;
		pos++;
	}

	sigBuf[pos] = '\0';
	return sigBuf;
}

/*
 * 0x00429328 - StoreHandlerArgNodes
 *
 * Coerces the argument chain against the handler's type signature
 * to type-annotate each argument node.
 */
int
StoreHandlerArgNodes(const BuiltinHandlerEntry *handler, ResultNode *nodes)
{
	return StoreTypesFromSig_Inner(handler->typeSig, &nodes);
}

/*
 * 0x00429340 - ParseStatementBlock (parse-time compiler)
 *
 * Compiles statements until a closing brace into the function or
 * trigger scope. Each token is classified and routed: variable
 * declarations and assignments through ProcessAssignment, builtin
 * and control-flow handlers through StoreHandlerResult, script
 * function calls through StoreFuncCallResult, goto labels through
 * StoreGotoResult. After the loop, ResolveGotoLabels patches up
 * label references. Returns the cursor past the block.
 *
 * FIXED: the binary crashes when LookupHandler returns NULL for an
 * unknown builtin name; we guard the StoreHandlerArgNodes call.
 */
const char *
ParseStatementBlock(const char *stream, CFuncScope *scope, int retTypeId)
{
	char tokenBuf[128];
	char tokenBuf2[128];
	char gotoNextBuf[128];
	CNodeList handlerStack;
	int stmtType;
	const BuiltinHandlerEntry *closingHandler;
	ResultNode *resultChain;
	char sigBuf[16];
	int typeLoop;
	const BuiltinHandlerEntry *poppedHandler;
	char returnSig[4];
	int subStmtType;
	CFunction *findResult;
	int funcIdx;
	int isMember;

	CNodeList_Constructor(&handlerStack);
	stream = ScriptTokenizer_ReadToken(stream, tokenBuf);

	while (stream != NULL) {
		if (handlerStack.count <= 1 && CompareTokenType(tokenBuf, SM_RBRACE))
			break;

		stmtType = ClassifyStatement(tokenBuf, scope);

		if (stmtType > STMT_GOTO)
			goto loop_restart;

		switch (stmtType) {
		case STMT_VAR_DECL:
			isMember = 1;

			if (CompareTokenType(tokenBuf, TK_MEMBER)) {
				isMember = 0;
				stream = ScriptTokenizer_ReadToken(stream, tokenBuf);
			}

			for (typeLoop = 0; typeLoop < 7; typeLoop++) {
				if (CompareTokenType(tokenBuf, g_TypeTokenIds[typeLoop]))
					break;
			}
			if (typeLoop >= 7)
				goto loop_restart;

			stream = ScriptTokenizer_ReadToken(stream, tokenBuf);

			if (isMember == 0) {
				CScript_AddVar(g_ScriptCompiler->script, tokenBuf, typeLoop);
			} else {
				AddVarToScope(scope, typeLoop, tokenBuf);
			}
			__attribute__((fallthrough));

		case STMT_LOCAL_VAR:
		case STMT_TRIGGER_VAR:
			stream = ProcessAssignment(scope, stream, tokenBuf);
			if (stream == NULL)
				goto cleanup;
			goto loop_restart;

		case STMT_BUILTIN_CALL:
			closingHandler = NULL;

			if (CompareTokenType(tokenBuf, TK_FOR)) {
				// read '('
				stream = ScriptTokenizer_ReadToken(stream, tokenBuf2);
				// read first init token
				stream = ScriptTokenizer_ReadToken(stream, tokenBuf2);

				subStmtType = ClassifyStatement(tokenBuf2, scope);

				if (subStmtType == STMT_VAR_DECL) {
					for (typeLoop = 0; typeLoop < 7; typeLoop++) {
						if (CompareTokenType(tokenBuf2, g_TypeTokenIds[typeLoop]))
							break;
					}
					stream = ScriptTokenizer_ReadToken(stream, tokenBuf2);
					AddVarToScope(scope, typeLoop, tokenBuf2);
				} else if (subStmtType != STMT_LOCAL_VAR) {
					// skip ProcessAssignment (binary: jmp 0x004296C7)
					goto for_condition;
				}

				stream = ProcessAssignment(scope, stream, tokenBuf2);
				if (stream == NULL)
					goto cleanup;

for_condition:
				resultChain = NULL;
				stream = EvaluateExpression(scope, stream, &resultChain, SM_SEMI);
				if (stream == NULL)
					goto cleanup;

				if (resultChain == NULL) {
					resultChain = AllocResultNode();
					resultChain->type = 6;
					resultChain->next = NULL;
					resultChain->extra = 0;
					resultChain->value = 1;
				}

				if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, LookupHandler("TK_WHILE", "vi"), resultChain))
					goto cleanup;

				stream = ScriptTokenizer_ReadToken(stream, tokenBuf2);

				if (!CompareTokenType(tokenBuf2, SM_RPAREN)) {
					ClassifyStatement(tokenBuf2, scope);
					stream = ProcessAssignment(scope, stream, tokenBuf2);
					if (stream == NULL)
						goto cleanup;
				}

				stream = ScriptTokenizer_ReadToken(stream, tokenBuf2);

				CNodeList_Push(&handlerStack, (uintptr_t)LookupHandler("TK_FOR", "vii"));
				goto loop_restart;
			}

			if (CompareTokenType(tokenBuf, TK_RETURN)) {
				// build return type signature "v" + typeCode
				returnSig[0] = 'v';
				returnSig[1] = g_WombatTypeCodes[retTypeId];
				returnSig[2] = '\0';
				closingHandler = LookupHandler(tokenBuf, returnSig);
				goto generic_handler;
			}

			if (CompareTokenType(tokenBuf, TK_CASE)) {
				stream = ScriptTokenizer_ReadToken(stream, tokenBuf2);
				resultChain = AllocResultNode();
				resultChain->extra = 0;
				resultChain->next = NULL;
				resultChain->type = 6;
				resultChain->value = GetInlineInt(tokenBuf2);
				if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, LookupHandler("TK_CASE", "v"), resultChain))
					goto cleanup;
				goto loop_restart;
			}

			if (CompareTokenType(tokenBuf, TK_DEFAULT)) {
				resultChain = AllocResultNode();
				resultChain->extra = 0;
				resultChain->next = NULL;
				resultChain->type = 6;
				resultChain->value = 0xFFFFFD66;
				if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, LookupHandler("TK_CASE", "v"), resultChain))
					goto cleanup;
				goto loop_restart;
			}

generic_handler:
			resultChain = NULL;

			if (CompareTokenType(tokenBuf, SM_RBRACE)) {
				poppedHandler = (BuiltinHandlerEntry *)(uintptr_t)CNodeList_Pop(&handlerStack);

				if (poppedHandler->handler == (uintptr_t)Handler_FOR) {
					closingHandler = LookupHandler("TK_ENDFOR", "v");
				} else if (poppedHandler->handler == (uintptr_t)Handler_WHILE) {
					closingHandler = LookupHandler("TK_ENDWHILE", "v");
				} else if (poppedHandler->handler == (uintptr_t)Handler_ELSE) {
					closingHandler = LookupHandler("TK_ENDIF", "v");
				} else if (poppedHandler->handler == (uintptr_t)Handler_CASE) {
					closingHandler = LookupHandler("TK_ENDSWITCH", "v");
				} else if (poppedHandler->handler == (uintptr_t)Handler_IF) {
					// Save g_ScriptCompiler state, peek ahead for TK_ELSE
					int savedFlags = g_ScriptCompiler->flags;
					char *savedBytecode = g_ScriptCompiler->bytecode;
					const char *readResult;

					readResult = ScriptTokenizer_ReadToken(stream, tokenBuf2);
					if (readResult == NULL)
						goto cleanup;

					g_ScriptCompiler->flags = savedFlags;
					g_ScriptCompiler->bytecode = savedBytecode;

					if (CompareTokenType(tokenBuf2, TK_ELSE)) {
						closingHandler = LookupHandler("TK_ELSE", "v");
						stream = readResult;
					} else {
						closingHandler = LookupHandler("TK_ENDIF", "v");
					}
				}
			} else {
				stream = EvaluateExpression(scope, stream, &resultChain, SM_RPAREN);
				if (stream == NULL)
					goto cleanup;
			}

			if (stream == NULL)
				goto cleanup;

			if (closingHandler == NULL) {
				BuildSignature(sigBuf, resultChain);
				closingHandler = LookupHandler(tokenBuf, sigBuf);
			}

			// Binary bug: no NULL check on closingHandler.
			// If LookupHandler fails (missing builtin), skip.
			if (closingHandler == NULL) {
				goto loop_restart;
			}
			StoreHandlerArgNodes(closingHandler, resultChain);

			if (stream != NULL) {
				if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, closingHandler, resultChain))
					goto cleanup;
			}

			if (closingHandler->handler == (uintptr_t)Handler_WHILE || closingHandler->handler == (uintptr_t)Handler_IF ||
			        closingHandler->handler == (uintptr_t)Handler_ELSE || closingHandler->handler == (uintptr_t)Handler_CASE) {
				stream = ScriptTokenizer_ReadToken(stream, tokenBuf2);
				CNodeList_Push(&handlerStack, (uintptr_t)closingHandler);
			}
			goto loop_restart;

		case STMT_MEMBER_VAR:
			resultChain = NULL;
			stream = EvaluateExpression(scope, stream, &resultChain, SM_RPAREN);
			if (stream == NULL)
				goto cleanup;

			findResult = CFuncList_FindFunc(&g_ScriptCompiler->script->funcList, tokenBuf, &funcIdx);

			StoreTypesFromSig(findResult, resultChain);

			if (stream != NULL) {
				StoreFuncCallResult((ResultNode **)&scope->bodyStream, funcIdx, resultChain);
			}
			goto loop_restart;

		case STMT_GOTO:
			if (CompareTokenType(tokenBuf, TK_GOTO)) {
				// read label name into tokenBuf
				stream = ScriptTokenizer_ReadToken(stream, tokenBuf);
				// read and discard next token
				stream = ScriptTokenizer_ReadToken(stream, gotoNextBuf);
			}
			StoreGotoResult((ResultNode **)&scope->bodyStream, tokenBuf);
			goto loop_restart;

		default:
			goto loop_restart;
		}

loop_restart:
		stream = ScriptTokenizer_ReadToken(stream, tokenBuf);
		continue;
	}

	ResolveGotoLabels((ResultNode *)(uintptr_t)scope->bodyStream);

	// Return type handler scan
	{
		int ifDepth = 0;
		char epilogSig[4];
		uint32_t returnHandlerAddr;
		ResultNode *node;
		BuiltinHandlerEntry *nodeHandler;

		epilogSig[0] = 'v';
		epilogSig[1] = g_WombatTypeCodes[retTypeId];
		epilogSig[2] = '\0';
		returnHandlerAddr = LookupHandler("TK_RETURN", epilogSig)->handler;

		node = (ResultNode *)(uintptr_t)scope->bodyStream;
		while (node != NULL) {
			if (node->type != 0) {
				node = (ResultNode *)(uintptr_t)node->next;
				continue;
			}
			nodeHandler = (BuiltinHandlerEntry *)(uintptr_t)node->value;
			if (nodeHandler->handler == returnHandlerAddr && ifDepth == 0)
				break;
			if (nodeHandler->handler == (uintptr_t)Handler_IF) {
				ifDepth++;
			} else if (nodeHandler->handler == (uintptr_t)Handler_ENDIF) {
				ifDepth--;
			}
			node = (ResultNode *)(uintptr_t)node->next;
		}
	}

	CNodeList_Destructor(&handlerStack);
	return stream;

cleanup:
	CNodeList_Destructor(&handlerStack);
	return NULL;
}

/*
 * 0x00429F46 - EvaluateExpression
 *
 * Compile-time expression evaluator. Reads tokens up to endToken,
 * classifies each, and appends matching ResultNodes to the chain:
 * variables, literals, builtin and script-function calls (which
 * recurse for their argument lists), and operators (unary OP_NOT,
 * grouping parentheses/brackets, or binary operators that detach
 * the previous node as the LHS). Returns the cursor past the
 * expression, or NULL on error.
 */
const char *
EvaluateExpression(CFuncScope *scope, const char *stream, ResultNode **chain, int endToken)
{
	char tokenBuf[2048]; // var_91ch
	int stmtType; // var_92ch
	int typeIdx; // var_928h
	const BuiltinHandlerEntry *binOpHandler; // var_924h
	int rtFlag; // var_920h
	int oprIndex; // var_11ch
	int prevEndToken; // var_118h
	int funcIdx; // var_114h
	ResultNode *tmpNode; // var_110h
	CFunction *findResult; // var_10ch
	const BuiltinHandlerEntry *handler; // var_108h
	char sigBuf[256]; // var_104h
	ResultNode *tmpPrev; // var_4h

	stream = ScriptTokenizer_ReadToken(stream, tokenBuf);

	if (CompareTokenType(tokenBuf, SM_SEMI))
		return stream;

	if (endToken == SM_RPAREN)
		stream = ScriptTokenizer_ReadToken(stream, tokenBuf);

	for (;;) {
		if (stream == NULL)
			break;

		if (CompareTokenType(tokenBuf, endToken))
			break;

		if (CompareTokenType(tokenBuf, SM_RPAREN))
			break;

		stmtType = ClassifyStatement(tokenBuf, scope);

		switch (stmtType - 1) {
		case 0: // stmtType=1 (STMT_LOCAL_VAR) → type=2 node
			StoreTriggerVarResult(chain, (uintptr_t)ResolveVarValue(scope, tokenBuf));
			break;

		case 1: // stmtType=2 (STMT_TRIGGER_VAR) → type=4 node
			StoreLocalVarResult(chain, (uintptr_t)ResolveVarValue(scope, tokenBuf));
			break;

		case 2: // stmtType=3 (STMT_INT_LITERAL)
			StoreIntLiteral(chain, (uint32_t)GetInlineInt(tokenBuf));
			break;

		case 3: // stmtType=4 (STMT_STR_LITERAL)
			StoreIdResult(chain, tokenBuf);
			break;

		case 4: // stmtType=5 (STMT_USTR_LITERAL)
			StoreMemberResult(chain, tokenBuf);
			break;

		case 5: // stmtType=6 (STMT_BUILTIN_CALL)
			tmpNode = NULL;
			stream = EvaluateExpression(scope, stream, &tmpNode, SM_RPAREN);
			if (stream == NULL)
				return NULL;

			BuildSignature(sigBuf, tmpNode);
			handler = LookupHandler(tokenBuf, sigBuf);
			if (handler == NULL)
				return NULL;

			StoreHandlerArgNodes(handler, tmpNode);
			if (stream != NULL) {
				if (!StoreHandlerResult(chain, handler, tmpNode))
					return NULL;
			}
			break;

		case 6: // stmtType=7 (STMT_MEMBER_VAR / script func call)
			tmpNode = NULL;
			stream = EvaluateExpression(scope, stream, &tmpNode, SM_RPAREN);
			if (stream == NULL)
				return NULL;

			findResult = CFuncList_FindFunc(&g_ScriptCompiler->script->funcList, tokenBuf, &funcIdx);

			StoreTypesFromSig(findResult, tmpNode);

			if (stream != NULL) {
				StoreFuncCallResult(chain, funcIdx, tmpNode);
			}
			break;

		case 7: // stmtType=8 (STMT_OPERATOR)
			prevEndToken = -1;

			if (CompareTokenType(tokenBuf, OP_NOT)) {
				tmpNode = NULL;
				stream = EvaluateExpression(scope, stream, &tmpNode, -1);
				if (stream == NULL)
					return NULL;

				handler = LookupHandler("oprnot", "ii");
				StoreHandlerArgNodes(handler, tmpNode);
				if (stream != NULL) {
					if (!StoreHandlerResult(chain, LookupHandler("oprnot", "ii"), tmpNode))
						return NULL;
				}
				break;
			}

			if (CompareTokenType(tokenBuf, SM_LBRACKET))
				prevEndToken = SM_RBRACKET;

			if (CompareTokenType(tokenBuf, SM_LPAREN)) {
				// Back up to re-find LPAREN
				stream--;
				while (!ScriptTokenizer_MatchToken(stream, SM_LPAREN))
					stream--;

				tmpNode = NULL;
				stream = EvaluateExpression(scope, stream, &tmpNode, SM_RPAREN);
				if (stream == NULL)
					return NULL;

				handler = LookupHandler("oprnull", "ii");
				StoreHandlerArgNodes(handler, tmpNode);
				if (stream != NULL) {
					if (!StoreHandlerResult(chain, handler, tmpNode))
						return NULL;
				}
				break;
			}

			// Binary operator - detach last node
			tmpNode = *chain;
			if (tmpNode == NULL) {
				// No LHS operand - skip this operator
				stream = EvaluateExpression(scope, stream, chain, endToken);
				break;
			}
			if (tmpNode->next == NULL) {
				*chain = NULL;
			} else {
				for (;;) {
					if (tmpNode->next == NULL)
						break;
					if (tmpNode->next->next == NULL) {
						tmpPrev = tmpNode;
						tmpNode = tmpNode->next;
						tmpPrev->next = NULL;
					} else {
						tmpNode = tmpNode->next;
					}
				}
			}

			stream = EvaluateExpression(scope, stream, &tmpNode, prevEndToken);
			if (stream == NULL)
				return NULL;

			for (oprIndex = 0; oprIndex < 0x13; oprIndex++) {
				if (CompareTokenType(tokenBuf, g_OperatorTable[oprIndex].tokenType))
					break;
			}

			if (stream == NULL)
				break;

			// Result unused
			typeIdx = ResolveResultType(g_ScriptCompiler->script, tmpNode, &rtFlag);
			USED(typeIdx);

			BuildSignature(sigBuf, tmpNode);
			binOpHandler = LookupHandler(g_OperatorTable[oprIndex].name, sigBuf);

			// Binary does NOT check for NULL here (passes directly
			// to StoreHandlerArgNodes). Safety check to avoid crash.
			if (binOpHandler == NULL)
				return NULL;

			StoreHandlerArgNodes(binOpHandler, tmpNode);
			if (!StoreHandlerResult(chain, binOpHandler, tmpNode))
				return NULL;
			break;

		default:
			break;
		}

		if (endToken == -1)
			return stream;

		stream = ScriptTokenizer_ReadToken(stream, tokenBuf);
		if (stream == NULL)
			break;

		if (CompareTokenType(tokenBuf, SM_COMMA))
			stream = ScriptTokenizer_ReadToken(stream, tokenBuf);
	}

	if (CompareTokenType(tokenBuf, endToken))
		return stream;
	if (CompareTokenType(tokenBuf, SM_RPAREN))
		return stream;
	return NULL;
}

/*
 * 0x0042A5E0 - ClassifyStatement (parse-time version)
 *
 * Returns the STMT_* category for tokenBuf during compilation.
 * Resolution order: local scope variable, script scope variable,
 * script-defined function, control-flow keyword, built-in function
 * name, semicolon, closing brace, member declarations and type
 * keywords, numeric / string / unicode literals, goto, return, and
 * finally operator tokens. Anything unrecognised classifies as
 * STMT_UNKNOWN.
 */
static int
ClassifyStatement(const char *tokenBuf, CFuncScope *scope)
{
	int i;

	// Step 1: check local scope (funcScope->namedScope)
	{
		void *found = CNamedScope_FindVar(&scope->namedScope, tokenBuf);
		if (found != NULL)
			return STMT_LOCAL_VAR;
	}

	// Step 2: check script scope (g_ScriptCompiler->script->namedScope)
	{
		void *found = CNamedScope_FindVar(&g_ScriptCompiler->script->namedScope, tokenBuf);
		if (found != NULL)
			return STMT_TRIGGER_VAR;
	}

	// Step 3: check script funcList for script-defined functions
	{
		CFunction *func = CFuncList_FindFunc(&g_ScriptCompiler->script->funcList, tokenBuf, NULL);
		if (func != NULL)
			return STMT_MEMBER_VAR;
	}

	// Step 4: control flow tokens TK_IF(0x25) through TK_RETURN(0x33)
	for (i = TK_IF; i <= TK_RETURN; i++) {
		if (CompareTokenType(tokenBuf, i))
			return STMT_BUILTIN_CALL;
	}

	// Step 5: built-in function names - strcmp against g_BuiltInFuncs
	{
		const BuiltinHandlerEntry *entry;
		for (entry = g_BuiltInFuncs; entry->name != NULL; entry++) {
			if (strcmp(tokenBuf, entry->name) == 0)
				return STMT_BUILTIN_CALL;
		}
	}

	// Step 6: SM_SEMI -> skip
	if (CompareTokenType(tokenBuf, SM_SEMI))
		return STMT_SEMI;

	// Step 7: SM_RBRACE -> builtin (pops control flow)
	if (CompareTokenType(tokenBuf, SM_RBRACE))
		return STMT_BUILTIN_CALL;

	// Step 8: TK_MEMBER -> var decl
	if (CompareTokenType(tokenBuf, TK_MEMBER))
		return STMT_VAR_DECL;

	// Step 9: type keywords (g_TypeTokenIds[0..6]) -> var decl
	for (i = 0; i < 7; i++) {
		if (CompareTokenType(tokenBuf, g_TypeTokenIds[i]))
			return STMT_VAR_DECL;
	}

	// Step 10: T_BYTE/T_WORD/T_DWORD -> int literal
	if (CompareTokenType(tokenBuf, T_BYTE) || CompareTokenType(tokenBuf, T_WORD) || CompareTokenType(tokenBuf, T_DWORD))
		return STMT_INT_LITERAL;

	// Step 11: string literal
	if (tokenBuf[0] == '"')
		return STMT_STR_LITERAL;

	// Step 12: unicode string literal
	if (tokenBuf[0] == 'L' && tokenBuf[1] == '"')
		return STMT_USTR_LITERAL;

	// Step 13: TK_GOTO
	if (CompareTokenType(tokenBuf, TK_GOTO))
		return STMT_GOTO;

	// Step 14: TK_RETURN (redundant with step 4, but binary checks again)
	if (CompareTokenType(tokenBuf, TK_RETURN))
		return STMT_BUILTIN_CALL;

	// Step 15: operator table
	for (i = 0; i < OPERATOR_COUNT; i++) {
		if (CompareTokenType(tokenBuf, g_OperatorTable[i].tokenType))
			return STMT_OPERATOR;
	}

	// Step 16: default
	return STMT_UNKNOWN;
}

/*
 * 0x0042A847 - ProcessAssignment (compile-time assignment handler)
 *
 * Compiles an assignment statement into ResultNode chains attached
 * to scope->bodyStream. ++ and -- emit oprinc / oprdec; otherwise
 * the RHS is evaluated and routed to a typed assignment handler
 * (assignint, assignstr, assignust, assignloc, assignlocint,
 * assignobj, assignlist, ...) selected from the LHS variable's
 * declared type and the resolved RHS type. List-typed LHS values
 * fall through to a clearlist/append sequence when the RHS is not
 * itself a list.
 */
const char *
ProcessAssignment(CFuncScope *scope, const char *stream, char *varTokenBuf)
{
	char tokenBuf[48];
	ResultNode *lhsChain;
	void *varPtr;
	const BuiltinHandlerEntry *handler;
	int stmtType;
	int rhsType;
	int outFlag;
	ResultNode *lhsCopy;

	stream = ScriptTokenizer_ReadToken(stream, tokenBuf);

	if (!CompareTokenType(tokenBuf, SM_SEMI) && !CompareTokenType(tokenBuf, SM_RPAREN))
		goto do_assign;

	return stream;

do_assign:
	lhsChain = NULL;
	stmtType = ClassifyStatement(varTokenBuf, scope);

	if (stmtType == 1) {
		varPtr = ResolveVarValue(scope, varTokenBuf);
		StoreLVarRef(&lhsChain, (uintptr_t)varPtr);
	} else if (stmtType == 2) {
		varPtr = ResolveVarValue(scope, varTokenBuf);
		StoreTVarRef(&lhsChain, (uintptr_t)varPtr);
	}

	if (CompareTokenType(tokenBuf, 0x1b)) {
		handler = LookupHandler("oprinc", "vI");
		if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
			return NULL;
		if (StoreHandlerArgNodes(handler, lhsChain) != 0)
			return NULL;
		stream = ScriptTokenizer_ReadToken(stream, varTokenBuf);
		return stream;
	}

	if (CompareTokenType(tokenBuf, 0x1c)) {
		handler = LookupHandler("oprdec", "vI");
		if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
			return NULL;
		if (StoreHandlerArgNodes(handler, lhsChain) != 0)
			return NULL;
		stream = ScriptTokenizer_ReadToken(stream, varTokenBuf);
		return stream;
	}

	stream = EvaluateExpression(scope, stream, &lhsChain, SM_SEMI);
	if (stream == NULL)
		return NULL;

	if (stream == NULL)
		goto done;
	rhsType = ((CNamedScopeEntry *)varPtr)->typeId;
	if ((unsigned int)rhsType > 5)
		goto done;

	switch (rhsType) {
	case 0: // INT
		rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next, &outFlag);
		if (rhsType == 0 || rhsType == 7) {
			handler = LookupHandler("assignint", "vIi");
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsChain) != 0)
				return NULL;
		} else if (rhsType == 1) {
			handler = LookupHandler("assignintstr", "vIs");
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsChain) != 0)
				return NULL;
		}
		goto done;

	case 1: // STRING
		rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next, &outFlag);
		if (rhsType == 1 || rhsType == 7) {
			handler = LookupHandler("assignstr", "vSs");
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsChain) != 0)
				return NULL;
		} else if (rhsType == 0) {
			handler = LookupHandler("assignstrint", "vSi");
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsChain) != 0)
				return NULL;
		}
		goto done;

	case 2: // USTRING
		rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next, &outFlag);
		if (rhsType == 2 || rhsType == 7) {
			handler = LookupHandler("assignust", "vQq");
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsChain) != 0)
				return NULL;
		} else if (rhsType == 0) {
			handler = LookupHandler("assignustint", "vQi");
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsChain) != 0)
				return NULL;
		}
		goto done;

	case 3: // LOC
		if (lhsChain->next->next == NULL) {
			rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next, &outFlag);
			if (rhsType == 3 || rhsType == 7) {
				handler = LookupHandler("assignloc", "vCc");
				if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
					return NULL;
				if (StoreHandlerArgNodes(handler, lhsChain) != 0)
					return NULL;
			}
			goto done;
		}
		if (lhsChain->next->next->next == NULL)
			goto list_case;
		if (lhsChain->next->next->next->next != NULL)
			goto list_case;
		// exactly 3 components - check all are INT
		rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next, &outFlag);
		if (rhsType != 0)
			goto list_case;
		rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next->next, &outFlag);
		if (rhsType != 0)
			goto list_case;
		rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next->next->next, &outFlag);
		if (rhsType != 0)
			goto list_case;
		// all 3 are INT: assignlocint
		handler = LookupHandler("assignlocint", "vCiii");
		if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
			return NULL;
		if (StoreHandlerArgNodes(handler, lhsChain) != 0)
			return NULL;
		goto done;

	case 4: // OBJ
		rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next, &outFlag);
		if (rhsType == 4 || rhsType == 7) {
			handler = LookupHandler("assignobj", "vOo");
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsChain) != 0)
				return NULL;
		}
		goto done;

	case 5: // LIST
list_case:
		if (lhsChain->next->next == NULL) {
			rhsType = ResolveResultType(g_ScriptCompiler->script, lhsChain->next, &outFlag);
			if (rhsType == 5) {
				handler = LookupHandler("assignlist", "vll");
				if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
					return NULL;
				if (StoreHandlerArgNodes(handler, lhsChain) != 0)
					return NULL;
				goto done;
			}
		}
		lhsCopy = AllocResultNode();
		lhsCopy->type = lhsChain->type;
		lhsCopy->value = lhsChain->value;
		lhsCopy->next = lhsChain->next;
		lhsCopy->extra = lhsChain->extra;
		lhsCopy->next = NULL;
		handler = LookupHandler("clearlist", "vl");
		if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsCopy))
			return NULL;
		if (StoreHandlerArgNodes(handler, lhsCopy) != 0)
			return NULL;
		// append loop
		handler = LookupHandler("append", "vlu");
		for (;;) {
			if (lhsChain->next->next == NULL)
				break;
			// not last: copy node, detach first child
			lhsCopy = AllocResultNode();
			lhsCopy->type = lhsChain->type;
			lhsCopy->value = lhsChain->value;
			lhsCopy->next = lhsChain->next;
			lhsCopy->extra = lhsChain->extra;
			// advance lhsChain past first child
			lhsChain->next = lhsChain->next->next;
			// null-terminate the detached child
			lhsCopy->next->next = NULL;
			if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsCopy))
				return NULL;
			if (StoreHandlerArgNodes(handler, lhsCopy) != 0)
				return NULL;
		}
		// final element: store lhsChain directly
		if (!StoreHandlerResult((ResultNode **)&scope->bodyStream, handler, lhsChain))
			return NULL;
		if (StoreHandlerArgNodes(handler, lhsChain) != 0)
			return NULL;
		goto done;
	}

done:
	return stream;
}

/*
 * 0x0042B11B - ResolveVarValue
 *
 * Looks up tokenBuf as a variable name in the function scope first
 * and the script's global scope second. Returns the matching
 * CNamedScopeEntry, or NULL when nothing matches.
 */
CNamedScopeEntry *
ResolveVarValue(CFuncScope *scope, const char *tokenBuf)
{
	CNamedScopeEntry *found;

	found = CNamedScope_FindVar(&scope->namedScope, tokenBuf);
	if (found != NULL)
		return found;

	return CNamedScope_FindVar(&g_ScriptCompiler->script->namedScope, tokenBuf);
}

/*
 * 0x0042B161 - ResolveVarValue_AtexitGuard
 *
 * MSVC atexit guard for static initialization of a local variable in
 * ResolveVarValue. MSVC CRT glue - OMITTED.
 */

/*
 * 0x0042B180 - CScriptCompiler::~CScriptCompiler (scalar deleting destructor)
 *
 * Runs the destructor and optionally frees the object.
 */
static __attribute__((unused)) CScriptCompiler *
CScriptCompiler_ScalarDelete(CScriptCompiler *compiler, int flags)
{
	CScriptCompiler_Destructor(compiler);
	if (flags & 1)
		OperatorDelete(compiler);
	return compiler;
}

/*
 * 0x0042B1B0 - ScriptTokenizer_MatchToken
 *
 * Returns 1 when the bytecode at stream matches tokenType. Variant-
 * encoded tokens compare the leading 16-bit word against any of the
 * type's five permitted encodings; text-based tokens (trigger names)
 * compare by string prefix.
 */
int
ScriptTokenizer_MatchToken(const char *stream, int tokenType)
{
	uint16_t streamVal;
	int i;

	if (tokenType < 0 || tokenType >= TOKEN_TYPE_COUNT)
		return 0;

	// Check if this token type has variant encoding
	if (g_TokenVariants[tokenType][0] != 0) {
		// Variant-encoded: compare uint16 at stream against all 5 variants
		memcpy(&streamVal, stream, 2);
		for (i = 0; i < 5; i++) {
			if (g_TokenVariants[tokenType][i] == streamVal)
				return 1;
		}
		return 0;
	}

	// Text-based token (trigger names): compare string
	if (g_TriggerNames[tokenType] != NULL) {
		int len = strlen(g_TriggerNames[tokenType]);
		if (memcmp(stream, g_TriggerNames[tokenType], len) == 0)
			return 1;
	}

	return 0;
}

/*
 * 0x0042B260 - GetInlineInt
 *
 * Extracts an integer value from inline data token buffers.
 * The token buffer has been populated by ReadToken which copies
 * the raw bytes (including the 2-byte token prefix).
 *
 * T_BYTE (0x3C): 3 bytes total - token[2] is uint8 value
 * T_WORD (0x3D): 4 bytes total - token[2..3] is uint16 value
 * T_DWORD (0x3E): 6 bytes total - token[2..5] is uint32 value
 *
 * Returns -1 if the buffer doesn't match any inline data token.
 */
int
GetInlineInt(const char *tokenBuf)
{
	if (CompareTokenType(tokenBuf, T_BYTE))
		return (unsigned char)tokenBuf[2];

	if (CompareTokenType(tokenBuf, T_WORD)) {
		uint16_t val;
		memcpy(&val, tokenBuf + 2, 2);
		return val;
	}

	if (CompareTokenType(tokenBuf, T_DWORD)) {
		uint32_t val;
		memcpy(&val, tokenBuf + 2, 4);
		return (int)val;
	}

	return -1;
}

/*
 * 0x0042B2CA - GetInlineInt_InitNodePool
 *
 * Static-init wrapper for one-time initialisation of the ResultNode pool
 * allocator. Omitted (CRT glue).
 */

/*
 * 0x0042B301 - ResultNode::ResultNode
 *
 * No-op constructor invoked by the pool allocator's batch
 * initialiser; the binary leaves the node uninitialised.
 */
static __attribute__((unused)) void
ResultNode_Constructor(ResultNode *node)
{
	USED(node);
}

/*
 * 0x0042B30C - ResultNode::Clear
 *
 * No-op called by NodePool_Push before the node returns to the
 * free list.
 */
static __attribute__((unused)) void
ResultNode_Clear(ResultNode *node)
{
	USED(node);
}

/*
 * 0x0042B317 - AllocResultNode
 *
 * Returns a fresh ResultNode from the global pool.
 */
ResultNode *
AllocResultNode(void)
{
	return NodePool_Pop(&g_NodePool);
}
/*
 * 0x0042B326 - FreeResultNode
 *
 * Returns node to the global ResultNode pool's free list.
 */
void
FreeResultNode(ResultNode *node)
{
	NodePool_Push(&g_NodePool, node);
}

/*
 * 0x0042B340 - NodePool::Init
 *
 * Initialises an empty ResultNode pool with the given batch size
 * (number of nodes allocated per refill).
 */
NodePool *
NodePool_Init(NodePool *pool, int batchSize)
{
	pool->head = NULL;
	pool->batchSize = batchSize;
	pool->flag = 0;
	VG_CREATE_POOL(pool);
	return pool;
}

/*
 * 0x0042B370 - NodePool::Pop
 *
 * Returns a ResultNode from the pool. Pops the free-list head when
 * available; otherwise allocates a batch of batchSize nodes laid
 * out as [count header][nodes...], returns node 0, and threads the
 * remaining nodes onto the free list.
 */
ResultNode *
NodePool_Pop(NodePool *pool)
{
	static int poolCreated;
	ResultNode *node;

	if (!poolCreated) {
		VG_CREATE_POOL(pool);
		poolCreated = 1;
	}

	if (pool->head != NULL) {
		// Pop from free list
		node = pool->head;
		VG_POOL_ALLOC(pool, node, sizeof(ResultNode));
		VG_MAKE_DEFINED(&node->next, sizeof(node->next));
		pool->head = node->next;
	} else {
		int i;
		int batchSize = pool->batchSize;
		char *block;
		ResultNode *batch;

		pool->flag = 1;

		// Binary: OperatorNew(batchSize * 0x10 + 4)
		block = (char *)malloc(batchSize * sizeof(ResultNode) + sizeof(uintptr_t));
		if (block == NULL)
			return NULL;

		*(uint32_t *)block = batchSize;

		// Binary: batch = block + 4
		batch = (ResultNode *)(block + sizeof(uintptr_t));

		// Batch constructor (0x004E9090) calls per-element ctor
		// (0x0042B2DE) which is a no-op (reads pool->flag, discards).

		pool->flag = 0;

		// Link nodes 1..batchSize-1 into free list (reverse order)
		for (i = batchSize - 1; i >= 1; i--) {
			batch[i].next = pool->head;
			pool->head = &batch[i];
		}

		// Node 0 is returned
		node = &batch[0];
		VG_POOL_ALLOC(pool, node, sizeof(ResultNode));
	}

	// 0x0042B301: per-node init, no-op for ResultNode

	return node;
}

/*
 * 0x0042B490 - NodePool::Push
 *
 * Pushes node onto the pool's free list.
 */
void
NodePool_Push(NodePool *pool, ResultNode *node)
{
	ResultNode_Clear(node);
	node->next = pool->head;
	pool->head = node;
	VG_POOL_FREE(pool, node);
}

/*
 * 0x0042B4C0 - NodePool::NodePool
 *
 * Default-constructs an empty pool (head NULL, batchSize 0).
 */
NodePool *
NodePool_Constructor(NodePool *pool)
{
	pool->head = NULL;
	pool->batchSize = 0;
	return pool;
}

/*
 * 0x0042B4E1 - NodePool::~NodePool
 *
 * No-op destructor.
 */
void
NodePool_Destructor(NodePool *pool)
{
	USED(pool);
}

/*
 * 0x0042B4EC - ThreadList::GetCurrent (GetCurrentExecThread)
 *
 * Returns the head exec thread (the one currently running).
 */
CExecThread *
ThreadList_GetCurrent(ThreadList *list)
{
	return list->head;
}

/*
 * 0x0042B4FC - ThreadList::Unlink (UnlinkThread)
 *
 * Removes thread from the active thread list, decrementing the
 * count and clearing thread's link pointers.
 */
void
ThreadList_Unlink(ThreadList *list, CExecThread *thread)
{
	// Check if thread is actually in this list
	if (thread->activeNext != NULL || thread->activePrev != NULL || list->head == thread) {
		list->count--;
	}

	// Unlink from doubly-linked list
	if (thread->activeNext != NULL) {
		thread->activeNext->activePrev = thread->activePrev;
	}

	if (thread->activePrev != NULL) {
		thread->activePrev->activeNext = thread->activeNext;
	} else {
		// Thread is head (no prev)
		if (list->head == thread)
			list->head = thread->activeNext;
	}

	// Clear links
	thread->activePrev = NULL;
	thread->activeNext = NULL;
}

/*
 * 0x0042B58F - ThreadList::Push (PushThread)
 *
 * Pushes thread onto the head of the active thread list.
 */
void
ThreadList_Push(ThreadList *list, CExecThread *thread)
{
	list->count++;

	thread->activeNext = list->head;
	if (list->head != NULL)
		list->head->activePrev = thread;

	list->head = thread;
	thread->activePrev = NULL;
}

/*
 * 0x0042B5DB - ThreadList::MoveToHead (MoveToHead)
 *
 * Re-links thread to the head of the active thread list.
 */
void
ThreadList_MoveToHead(ThreadList *list, CExecThread *thread)
{
	ThreadList_Unlink(list, thread);
	ThreadList_Push(list, thread);
}

/*
 * 0x0042B600 - ThreadList::Remove
 *
 * Removes thread from the active thread list.
 */
void
ThreadList_Remove(ThreadList *list, CExecThread *thread)
{
	ThreadList_Unlink(list, thread);
}

/*
 * 0x0042B619 - ThreadList::PopHead
 *
 * Unlinks the head thread from the active thread list, if any.
 */
void
ThreadList_PopHead(ThreadList *list)
{
	if (list->head != NULL)
		ThreadList_Unlink(list, list->head);
}

/*
 * 0x0042B63A - ThreadList::FinishCurrent
 *
 * Terminates the active thread (the head of g_activeThreadList).
 */
void
ThreadList_FinishCurrent(ThreadList *list, int arg)
{
	CExecThread *thread;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	ThreadList_FinishThread(list, thread, arg);
}

/*
 * 0x0042B666 - ThreadList::StopByScriptField
 *
 * Finishes every active thread whose scriptRef points to a node
 * whose entity field equals value.
 */
void
ThreadList_StopByScriptField(ThreadList *list, uintptr_t value)
{
	CExecThread *cur, *next;

	cur = list->head;
	while (cur != NULL) {
		next = cur->activeNext;
		if (cur->scriptRef != NULL) {
			ScriptAttachNode *node = (ScriptAttachNode *)cur->scriptRef;
			if ((uintptr_t)node->entity == value) {
				ThreadList_FinishThread(list, cur, 1);
			}
		}
		cur = next;
	}
}

/*
 * 0x0042B6BB - ThreadList::StopBySerial
 *
 * Finishes every active thread whose scriptRef equals value.
 */
void
ThreadList_StopBySerial(ThreadList *list, uintptr_t value)
{
	CExecThread *cur, *next;

	cur = list->head;
	while (cur != NULL) {
		next = cur->activeNext;
		if ((uintptr_t)cur->scriptRef == value) {
			ThreadList_FinishThread(list, cur, 1);
		}
		cur = next;
	}
}

/*
 * 0x0042B704 - ThreadList::DetachFromEntity
 *
 * Finishes every active thread that is running scriptName on
 * entity.
 */
void
ThreadList_DetachFromEntity(ThreadList *list, CItem *entity, CString *scriptName)
{
	CExecThread *cur, *next;
	ScriptAttachNode *ref;
	CScript *script;

	cur = list->head;
	while (cur != NULL) {
		next = cur->activeNext;
		if (cur->scriptRef != NULL) {
			ref = (ScriptAttachNode *)cur->scriptRef;
			if (ref->entity == (void *)entity) {
				script = (CScript *)ref->scriptClassPtr;
				if (strcmp(script->name, CString_GetBuffer(scriptName)) == 0) {
					ThreadList_FinishThread(list, cur, 1);
				}
			}
		}
		cur = next;
	}
}

/*
 * 0x0042B779 - ThreadList::FinishThread
 *
 * Terminates a thread: appends its return value and a zero sentinel
 * to the scope, nulls the stream, marks the thread finished, and
 * clears the script reference.
 */
void
ThreadList_FinishThread(ThreadList *list, CExecThread *thread, int unused)
{
	uint32_t retVal;
	uint32_t zero;
	const char *scriptName;
	void *ref;
	void *p;

	USED(list);
	USED(unused);

	// Dead code: get script name for debugging (never used)
	scriptName = "unknown";
	if (thread != NULL && thread->scriptRef != NULL) {
		ref = thread->scriptRef;
		p = *(void **)ref;
		if (p != NULL && *(void **)p != NULL)
			scriptName = (const char *)*(void **)p;
	}
	USED(scriptName);

	if (thread == NULL)
		return;

	retVal = (uint32_t)thread->returnVal;
	CScope_Append(&thread->scope, &retVal, 4);

	zero = 0;
	CScope_Append(&thread->scope, &zero, 4);

	thread->stream = NULL;
	thread->defaultReturn = 0;
	thread->finished = 1;
	thread->scriptRef = NULL;
}

/*
 * 0x0042B822 - GetCurrentThreadEntity
 *
 * Returns the entity hosting the currently running script thread,
 * or NULL when no thread is active.
 */
CItem *
GetCurrentThreadEntity(void)
{
	CExecThread *thread;

	thread = ThreadList_GetCurrent(&g_activeThreadList);
	return CExecThread_GetEntity(thread);
}

/*
 * 0x0042B850 - BroadcastEventToNearby
 *
 * Fires eventType (with the variadic event args) on every entity
 * within range of loc. Returns 0 as soon as any handler cancels
 * the event by returning 0; returns 1 when all handlers succeed.
 */
int
BroadcastEventToNearby(CLocation *loc, int range, int eventType, ...)
{
	int blockBuf[1024];
	CItem *entity;
	int i;
	va_list ap;

	va_start(ap, eventType);

	CBlockManager_GetNearbyBlocks(&g_SpatialGrid, loc, range, blockBuf, 1024);

	for (i = 0; blockBuf[i] != -1; i++) {
		entity = g_SpatialGrid.cells[blockBuf[i]].itemHead;

		while (entity != NULL) {
			CLocation *entLoc = CEntity_GetLocation(&entity->resourceEntity.entity);
			int dist = CLocation_ChebyshevDistance(loc, entLoc);

			if (dist < range) {
				va_list ap_copy;
				va_copy(ap_copy, ap);
				int ret = CWombatManager_FireTrigger_va(entity, eventType, ap_copy);
				va_end(ap_copy);

				if (ret == 0) {
					va_end(ap);
					return 0;
				}
			}

			// Binary reads spatialNext after event fires
			entity = entity->spatialNext;
		}
	}

	va_end(ap);
	return 1;
}

/*
 * 0x0042B951 - DispatchEvent / ExtractEventParams
 *
 * Pulls event-specific arguments out of ap into params and returns
 * the parameter count. The parameter shape (names and types) is
 * implicit per event type - scripts don't declare them - and
 * matches what the Wombat compiler bakes into each trigger's
 * scope. See the case bodies for the per-event layouts.
 */
static int
ExtractEventParams(int eventType, va_list ap, EventParam *params)
{
	int n = 0;

	switch (eventType) {
	case 0x17: /* use - binary: 0x0042C9B2 reads 1 obj arg */
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x18: /* targetobj - binary: 0x0042CE44 reads 2 obj args */
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "usedon";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x19: /* targetloc - binary: 0x0042CE83 reads obj + CLocation* + objtype */
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		{
			// Binary reads CLocation* via va_arg, then memcpy 6 bytes
			CLocation *locPtr = va_arg(ap, CLocation *);
			params[n].name = "place";
			params[n].type = WTYPE_LOC;
			memcpy(&params[n].ival, locPtr, 4);
			params[n].ival2 = locPtr->z;
			params[n].sval = NULL;
			n++;
		}
		params[n].name = "objtype";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x00: /* speech - binary: 0x0042BAFB reads obj + string */
		params[n].name = "speaker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		/*
		 * Speech text available as "arg" variable in trigger scope.
		 * Binary: ParseTrigger 0x00427795 defines param name "arg";
		 * DispatchEvent 0x0042BB18 reads string, stores in param block. */
		{
			const char *text = va_arg(ap, const char *);
			params[n].name = "arg";
			params[n].type = WTYPE_STRING;
			params[n].ival = 0;
			params[n].ival2 = 0;
			params[n].sval = text;
			n++;
		}
		break;

	case 0x0F: /* creation - binary: 0x0042D011 reads 1 obj arg
		    * (scriptInstance). Not added as a trigger param - used
		    * only for scriptArr filtering. */
		params[n].name = "_scriptInstance";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x10: /* enterrange - binary: 0x0042D0FA reads 4 args */
	case 0x11: /* leaverange - binary: 0x0042D271 reads 4 args */
		params[n].name = "_trigPtr";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "_mob";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "_oldLoc";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "_newLoc";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x01: /* gotattacked - binary: 0x0042CC47 reads 1 obj arg */
		params[n].name = "attacker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x04: /* death - binary: 0x0042CD2B reads 2 obj args */
		params[n].name = "attacker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "corpse";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x02: /* killedtarget - binary: 0x0042CC47 reads 1 OBJ arg */
		params[n].name = "attacker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x05: /* sawdeath - binary: 0x0042CD6A reads 3 OBJ args */
		params[n].name = "attacker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "victim";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "corpse";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x07: /* washit - binary: 0x0042CCEC reads 2 args (obj + int).
		    * Disassembly: AddParam(type=4/OBJ, arg1), AddParam(type=0/INT, arg2).
		    * Scripts reference "attacker" (obj) and "damamt" (int).
		    * Script can call intRet(newDamage) to override damage. */
		params[n].name = "attacker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "damamt";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x06: /* fightpulse - binary: 0x0042CC8B reads 1 obj arg */
		params[n].name = "target";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x1C: /* lookedat - binary: 0x0042CA18 reads 1 obj arg */
		params[n].name = "looker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x1B: /* wasdropped - binary: 0x0042C9D4 reads 1 obj arg */
		params[n].name = "dropper";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x1E: /* wasgotten - binary: 0x0042C9F6 reads 1 obj arg */
		params[n].name = "getter";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x2C: /* equip - binary: 0x0042D470 reads 1 obj arg */
		params[n].name = "equippedon";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x2D: /* unequip - binary: 0x0042D4B4 reads 1 obj arg */
		params[n].name = "unequippedfrom";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x3C: /* stolenfrom - binary: 0x0042CA3A reads 1 obj arg */
		params[n].name = "stealer";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x38: /* oortargetobj - binary: reads 2 obj args (user, usedon) */
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "usedon";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x33: /* ooruse - binary: reads 1 obj arg (user) */
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x2B: /* destroyed - binary: 0x0042D6A9 (no params) */
		break;

	case 0x1D: /* give - binary: 0x0042CE05 reads 2 obj args */
		params[n].name = "giver";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "givenobj";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x16: { /* message - binary: 0x0042C286.
		    * Reads: obj sender, string msgName, string formatStr,
		    * then parses formatStr to read typed args into a list.
		    * Params: sender (OBJ), _msgName (STRING for filter),
		    * args (LIST from format string parsing).
		    * Format chars: c=LOC, i=INT, o=OBJ, s=STRING, p=USTRING,
		    * x=LIST copy. v/other = skip. */
		const char *msgName;
		const char *fmt;
		CList *argsList;

		params[n].name = "sender";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		msgName = va_arg(ap, const char *);
		params[n].name = "_msgName";
		params[n].type = WTYPE_STRING;
		params[n].ival = 0;
		params[n].ival2 = 0;
		params[n].sval = msgName;
		n++;
		argsList = (CList *)malloc(sizeof(CList));
		if (argsList != NULL)
			CList_Constructor(argsList);
		fmt = va_arg(ap, const char *);
		if (fmt != NULL && argsList != NULL) {
			while (*fmt) {
				switch (*fmt) {
				case 'i': {
					int val = va_arg(ap, int);
					CList_Append(argsList, WTYPE_INT, (uint32_t)val);
					break;
				}
				case 'o': {
					uint32_t val = va_arg(ap, uintptr_t);
					CList_Append(argsList, WTYPE_OBJ, val);
					break;
				}
				case 's':
				case 'p': {
					const char *sval = va_arg(ap, const char *);
					CList_Append(argsList, WTYPE_STRING, (uintptr_t)sval);
					break;
				}
				case 'c': {
					uint32_t val = va_arg(ap, uintptr_t);
					CList_Append(argsList, WTYPE_LOC, val);
					break;
				}
				case 'x': {
					CList *srcList = va_arg(ap, CList *);
					if (srcList != NULL) {
						CListNode *node = srcList->head;
						while (node != NULL) {
							CList_Append(argsList, node->typeTag, node->value);
							node = node->next;
						}
					}
					break;
				}
				default:
					break;
				}
				fmt++;
			}
		}
		params[n].name = "args";
		params[n].type = WTYPE_LIST;
		params[n].ival = (uintptr_t)argsList;
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;
	}

	case 0x22: /* ishitting - binary: 0x0042CCAD reads 2 args (obj + int).
		    * Fires on weapon when it hits. Scripts use "victim" (obj) and
		    * optionally damage amount (int). */
		params[n].name = "victim";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "damamt";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x43: /* mobishitting - binary: 0x0042D66F reads 2 args (obj + int).
		    * Fires on TARGET mob when hit. "victim" is attacker serial,
		    * "damage" is amount. */
		params[n].name = "victim";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "damage";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x24: /* typeselected - binary: 0x0042BCD6 reads filter + 4 args.
		    * First va_arg is dialogId/menuId used for filterData matching,
		    * NOT passed to script. Remaining 4 are trigger-visible params. */
	{
		uint32_t filterVal = va_arg(ap, uintptr_t);
		params[n].name = "_filterValue";
		params[n].type = WTYPE_INT;
		params[n].ival = filterVal;
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
	}
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "listindex";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "objtype";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "objhue";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x0B: /* foundfood - binary: 0x0042CA7E reads 1 obj arg */
	case 0x0C: /* founddesire - binary: 0x0042CAA0 */
	case 0x0D: /* foundshelter - binary: 0x0042CAC2 */
		params[n].name = "target";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x3A: /* textentry - binary: 0x0042CAE4 reads filter + obj + int + string */
	{
		uint32_t promptId = va_arg(ap, uintptr_t);
		params[n].name = "_filterValue";
		params[n].type = WTYPE_INT;
		params[n].ival = promptId;
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
	}
		params[n].name = "sender";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "button";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		{
			const char *text = va_arg(ap, const char *);
			params[n].name = "text";
			params[n].type = WTYPE_STRING;
			params[n].ival = 0;
			params[n].ival2 = 0;
			params[n].sval = text;
			n++;
		}
		break;

	case 0x25: /* hueselected - binary: 0x0042BD85 reads filter + 2 args.
		    * First va_arg is itemID used for filterData matching,
		    * NOT passed to script. Remaining 2 are trigger-visible params. */
	{
		uint32_t filterVal = va_arg(ap, uintptr_t);
		params[n].name = "_filterValue";
		params[n].type = WTYPE_INT;
		params[n].ival = filterVal;
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
	}
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "objhue";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x37: /* genericgump - binary: 0x0042C6FB.
		    * arg1 = gumpID (stored as filter/return value, NOT AddParam).
		    * arg2 = user serial (AddParam type=4/OBJ).
		    * arg3 = button ID (AddParam type=0/INT).
		    * Binary also builds a CList for checkbox/text field data
		    * but demo scripts only use user + buttonid. */
	{
		uint32_t gumpId = va_arg(ap, uintptr_t);
		// Store gumpID as filter value for trigger matching
		params[n].name = "_filterValue";
		params[n].type = WTYPE_INT;
		params[n].ival = gumpId;
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
	}
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "buttonid";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x44: /* famechanged - binary: events >= 0x44 are beyond jump table,
		    * DispatchEvent skips to script loop with no params. */
	case 0x45: /* karmachanged */
	case 0x46: /* murdercountchanged */
		// No parameters - these events fire with no trigger variables.
		break;

	case 0x1F: /* pathfound - binary: 0x0042C001 reads 1 int arg (filter).
		    * Used only for filterData matching, no AddParam. */
	case 0x20: /* pathnotfound - binary: 0x0042C0D8 reads 1 int arg (filter).
		    * Used only for filterData matching, no AddParam. */
		params[n].name = "_filterValue";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x21: /* callback - binary: 0x0042C1AF reads 1 int arg (callback ID).
		    * Used only for filterData matching, no AddParam. */
		params[n].name = "_callbackId";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x3D: /* objaccess - binary: 0x0042BF06 reads int filter + 2 obj args.
		    * Filter value (sub-type) is matched against trigger filterData.
		    * Sub-types: 4=drop-into, 5=pickup-from, 7=drop-into-alt,
		    *            8=container-open, 9=snooping. */
	{
		int filterVal = va_arg(ap, int);
		params[n].name = "_filterValue";
		params[n].type = WTYPE_INT;
		params[n].ival = filterVal;
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
	}
		params[n].name = "user";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "usedon";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x3B: /* shop - binary: 0x0042D576 reads 1 int arg */
		params[n].name = "func";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x42: /* canbuy - binary: 0x0042D616 reads 2 obj + 1 int args */
		params[n].name = "buyer";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "seller";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		params[n].name = "quantity";
		params[n].type = WTYPE_INT;
		params[n].ival = va_arg(ap, int);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		break;

	case 0x23: /* convofunc - binary: 0x0042CEEB reads string filter + obj + string.
		    * 1st va_arg = function name (strcmp filter, NOT AddParam).
		    * 2nd va_arg = talker serial (AddParam type=4/OBJ).
		    * 3rd va_arg = arg string (AddParam type=1/STRING). */
	{
		const char *funcName = va_arg(ap, const char *);
		params[n].name = "_funcName";
		params[n].type = WTYPE_STRING;
		params[n].ival = 0;
		params[n].ival2 = 0;
		params[n].sval = funcName;
		n++;
	}
		params[n].name = "talker";
		params[n].type = WTYPE_OBJ;
		params[n].ival = va_arg(ap, uintptr_t);
		params[n].ival2 = 0;
		params[n].sval = NULL;
		n++;
		{
			const char *text = va_arg(ap, const char *);
			params[n].name = "arg";
			params[n].type = WTYPE_STRING;
			params[n].ival = 0;
			params[n].ival2 = 0;
			params[n].sval = text;
			n++;
		}
		break;

	default:
		/*
		 * Table-driven fallback for events with only OBJ/INT params.
		 * Binary dispatch jump table at 0x0042D7B8 reads params matching
		 * g_TriggerParamTable. Events with STRING (1), LOC (3), or
		 * LIST (5) params must have explicit cases above. */
		if (eventType >= 0 && eventType < TRIGGER_EVENT_COUNT) {
			int i;
			for (i = 0; i < g_TriggerParamTable[eventType].count; i++) {
				int typeId = g_TriggerParamTable[eventType].params[i].typeId;
				params[n].name = g_TriggerParamTable[eventType].params[i].name;
				params[n].ival2 = 0;
				params[n].sval = NULL;
				if (typeId == 4) { /* OBJ */
					params[n].type = WTYPE_OBJ;
					params[n].ival = va_arg(ap, uintptr_t);
				} else if (typeId == 0) { /* INT */
					params[n].type = WTYPE_INT;
					params[n].ival = va_arg(ap, int);
				} else {
					break; /* STRING/LOC/LIST: needs explicit case */
				}
				n++;
			}
		}
		break;
	}

	return n;
}

/*
 * 0x0042B951 - DispatchEvent
 *
 * Fires eventType on entity, walking every attached script and
 * running its matching triggers. Speech, message, convofunc,
 * enter/leave-range and a handful of integer-filter events filter
 * the trigger set against filterData before execution. Recursion
 * is capped at 0x40. Returns 0 when a handler consumed the event,
 * 1 otherwise.
 *
 * FIXED: ExtractEventParams stores filter-only args with '_'-
 * prefixed names so the filter switch can read them; we strip
 * those out before invoking each trigger so the remaining params
 * align with the trigger's scope.
 *
 * FIXED: The binary leaks the CList allocated for message event
 * (0x16) format-string args; we free LIST params after the loop.
 */
static int
DispatchEvent(CItem *entity, int eventType, va_list ap)
{
	CVector scriptVec, trigVec;
	int numTriggers;
	int i, matchCount;
	EventParam params[MAX_EVENT_PARAMS];
	int numParams;
	uintptr_t *scriptArr;
	uintptr_t *trigArr;
	int result;

	if (entity == NULL)
		return 1;

	if (g_ScriptRecursionDepth > 0x40) {
		char buf[792];
		const char *str1 = "";
		const char *str2 = "";
		if (g_CurrentExecCtx != NULL) {
			CExecThread *cur = (CExecThread *)(void *)g_CurrentExecCtx;
			if (cur->scriptRef != NULL) {
				const char **pp = (const char **)cur->scriptRef;
				if (*pp != NULL)
					str2 = *(const char **)*pp;
			}
			if (cur->globalNext != NULL && cur->globalNext->scriptRef != NULL) {
				const char **pp = (const char **)cur->globalNext->scriptRef;
				if (*pp != NULL)
					str1 = *(const char **)*pp;
			}
		}
		sprintf(buf, "Too many recursive thread calls [%s, %s]", str2, str1);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "wombat", "error", buf);
		return 1;
	}

	if (eventType < 0 || eventType >= TRIGGER_EVENT_COUNT)
		return 1;

	CVector_Constructor(&scriptVec, "");
	CVector_Constructor(&trigVec, "");

	numTriggers = CWombatManager_GetScriptsForObj(entity, &scriptVec, &trigVec, eventType);

	if (numTriggers > 0x40)
		numTriggers = 0x40;

	if (numTriggers == 0) {
		CVector_Destructor(&scriptVec);
		CVector_Destructor(&trigVec);
		return 1;
	}

	memset(params, 0, sizeof(params));
	numParams = ExtractEventParams(eventType, ap, params);

	g_ScriptRecursionDepth++;

	scriptArr = (uintptr_t *)scriptVec.begin;
	trigArr = (uintptr_t *)trigVec.begin;

	// Binary: per-event filterData matching inside switch cases.
	// Each case compacts matching triggers to front and updates numTriggers.
	switch (eventType) {
	case 0: {
		// CStringMatcher_Match includes when filterData is NULL (returns 0 = no match).
		const char *text = NULL;
		for (i = 0; i < numParams; i++) {
			if (params[i].type == WTYPE_STRING && params[i].sval != NULL) {
				text = params[i].sval;
				break;
			}
		}
		matchCount = 0;
		for (i = 0; i < numTriggers; i++) {
			CTrigger *btrig = (CTrigger *)trigArr[i];
			int m = CStringMatcher_Match(&g_StringMatcher, text, (const char *)btrig->filterData);
			if (m) {
				if (matchCount < i) {
					scriptArr[matchCount] = scriptArr[i];
					trigArr[matchCount] = trigArr[i];
				}
				matchCount++;
			}
		}
		numTriggers = matchCount;
		break;
	}
	case 22: {
		const char *text = NULL;
		for (i = 0; i < numParams; i++) {
			if (params[i].type == WTYPE_STRING && params[i].sval != NULL) {
				text = params[i].sval;
				break;
			}
		}
		matchCount = 0;
		for (i = 0; i < numTriggers; i++) {
			CTrigger *btrig = (CTrigger *)trigArr[i];
			if (btrig->filterData != NULL && text != NULL && strcmp((const char *)btrig->filterData, text) == 0) {
				if (matchCount < i) {
					scriptArr[matchCount] = scriptArr[i];
					trigArr[matchCount] = trigArr[i];
				}
				matchCount++;
			}
		}
		numTriggers = matchCount;
		break;
	}
	case 31:
	case 32:
	case 33:
	case 36:
	case 37:
	case 55:
	case 58:
	case 61: {
		int filterValue = 0;
		for (i = 0; i < numParams; i++) {
			if (params[i].name != NULL && params[i].name[0] == '_') {
				filterValue = (int)params[i].ival;
				break;
			}
		}
		matchCount = 0;
		for (i = 0; i < numTriggers; i++) {
			CTrigger *btrig = (CTrigger *)trigArr[i];
			if ((int)(intptr_t)btrig->filterData == filterValue) {
				if (matchCount < i) {
					scriptArr[matchCount] = scriptArr[i];
					trigArr[matchCount] = trigArr[i];
				}
				matchCount++;
			}
		}
		numTriggers = matchCount;
		break;
	}
	case 35: {
		const char *text = NULL;
		for (i = 0; i < numParams; i++) {
			if (params[i].type == WTYPE_STRING && params[i].sval != NULL) {
				text = params[i].sval;
				break;
			}
		}
		matchCount = 0;
		for (i = 0; i < numTriggers; i++) {
			CTrigger *btrig = (CTrigger *)trigArr[i];
			if (btrig->filterData != NULL && text != NULL && strcmp((const char *)btrig->filterData, text) == 0) {
				scriptArr[0] = scriptArr[i];
				trigArr[0] = trigArr[i];
				matchCount = 1;
				break;
			}
		}
		numTriggers = matchCount;
		break;
	}
	case 0x0F: {
		uintptr_t scriptInstance = 0;
		for (i = 0; i < numParams; i++) {
			if (params[i].name != NULL && strcmp(params[i].name, "_scriptInstance") == 0) {
				scriptInstance = (uintptr_t)params[i].ival;
				break;
			}
		}
		matchCount = 0;
		for (i = 0; i < numTriggers; i++) {
			if (scriptArr[i] == scriptInstance) {
				if (matchCount < i) {
					scriptArr[matchCount] = scriptArr[i];
					trigArr[matchCount] = trigArr[i];
				}
				matchCount++;
			}
		}
		numTriggers = matchCount;
		break;
	}
	case 0x10: {
		CTrigger *trigPtr = NULL;
		CItem *rangeMob = NULL;
		CLocation *rangeOldLoc = NULL;
		CLocation *rangeNewLoc = NULL;
		CLocation *entityLoc;
		int oldDist, newDist, range;

		for (i = 0; i < numParams; i++) {
			if (params[i].name == NULL)
				continue;
			if (strcmp(params[i].name, "_trigPtr") == 0)
				trigPtr = (CTrigger *)(uintptr_t)params[i].ival;
			else if (strcmp(params[i].name, "_mob") == 0)
				rangeMob = (CItem *)(uintptr_t)params[i].ival;
			else if (strcmp(params[i].name, "_oldLoc") == 0)
				rangeOldLoc = (CLocation *)(uintptr_t)params[i].ival;
			else if (strcmp(params[i].name, "_newLoc") == 0)
				rangeNewLoc = (CLocation *)(uintptr_t)params[i].ival;
		}
		if (trigPtr == NULL || rangeMob == NULL || rangeOldLoc == NULL || rangeNewLoc == NULL) {
			numTriggers = 0;
			break;
		}
		range = (int)(intptr_t)trigPtr->filterData;
		entityLoc = CEntity_GetLocation(&entity->resourceEntity.entity);
		oldDist = CLocation_ChebyshevDistance(rangeOldLoc, entityLoc);
#ifdef DEBUG_SPEECH
		fprintf(stderr,
		        "ENTERRANGE: entity=0x%08X at (%d,%d,%d) "
		        "range=%d oldDist=%d\n",
		        entity->serial, (int)(int16_t)entityLoc->x, (int)(int16_t)entityLoc->y, (int)(int16_t)entityLoc->z, range, oldDist);
#endif
		if (oldDist <= range) {
			numTriggers = 0;
			break;
		}
		newDist = CLocation_ChebyshevDistance(rangeNewLoc, entityLoc);
#ifdef DEBUG_SPEECH
		fprintf(stderr, "ENTERRANGE: newDist=%d -> %s\n", newDist, newDist <= range ? "PASS" : "FAIL");
#endif
		if (newDist > range) {
			numTriggers = 0;
			break;
		}
		numParams = 0;
		params[0].name = "target";
		params[0].type = WTYPE_OBJ;
		params[0].ival = rangeMob->serial;
		params[0].ival2 = 0;
		params[0].sval = NULL;
		numParams = 1;
		for (i = 0; i < numTriggers; i++) {
			if ((CTrigger *)trigArr[i] == trigPtr) {
				if (i > 0) {
					uintptr_t tmpS = scriptArr[0];
					uintptr_t tmpT = trigArr[0];
					scriptArr[0] = scriptArr[i];
					trigArr[0] = trigArr[i];
					scriptArr[i] = tmpS;
					trigArr[i] = tmpT;
				}
				break;
			}
		}
		if (i == numTriggers)
			numTriggers = 0;
		else
			numTriggers = 1;
		break;
	}
	case 0x11: {
		CTrigger *trigPtr = NULL;
		CItem *rangeMob = NULL;
		CLocation *rangeOldLoc = NULL;
		CLocation *rangeNewLoc = NULL;
		CLocation *entityLoc;
		int oldDist, newDist, range;

		for (i = 0; i < numParams; i++) {
			if (params[i].name == NULL)
				continue;
			if (strcmp(params[i].name, "_trigPtr") == 0)
				trigPtr = (CTrigger *)(uintptr_t)params[i].ival;
			else if (strcmp(params[i].name, "_mob") == 0)
				rangeMob = (CItem *)(uintptr_t)params[i].ival;
			else if (strcmp(params[i].name, "_oldLoc") == 0)
				rangeOldLoc = (CLocation *)(uintptr_t)params[i].ival;
			else if (strcmp(params[i].name, "_newLoc") == 0)
				rangeNewLoc = (CLocation *)(uintptr_t)params[i].ival;
		}
		if (trigPtr == NULL || rangeMob == NULL || rangeOldLoc == NULL || rangeNewLoc == NULL) {
			numTriggers = 0;
			break;
		}
		range = (int)(intptr_t)trigPtr->filterData;
		entityLoc = CEntity_GetLocation(&entity->resourceEntity.entity);
		oldDist = CLocation_ChebyshevDistance(rangeOldLoc, entityLoc);
		if (oldDist > range) {
			numTriggers = 0;
			break;
		}
		newDist = CLocation_ChebyshevDistance(rangeNewLoc, entityLoc);
		if (newDist <= range) {
			numTriggers = 0;
			break;
		}
		numParams = 0;
		params[0].name = "target";
		params[0].type = WTYPE_OBJ;
		params[0].ival = rangeMob->serial;
		params[0].ival2 = 0;
		params[0].sval = NULL;
		numParams = 1;
		for (i = 0; i < numTriggers; i++) {
			if ((CTrigger *)trigArr[i] == trigPtr) {
				if (i > 0) {
					uintptr_t tmpS = scriptArr[0];
					uintptr_t tmpT = trigArr[0];
					scriptArr[0] = scriptArr[i];
					trigArr[0] = trigArr[i];
					scriptArr[i] = tmpS;
					trigArr[i] = tmpT;
				}
				break;
			}
		}
		if (i == numTriggers)
			numTriggers = 0;
		else
			numTriggers = 1;
		break;
	}
	default:
		// All other events: no filterData matching in the binary.
		// All triggers pass through to the execution loop.
		break;
	}

	// Binary: some event va_args are read only for filterData matching
	// and are NOT passed to AddParam (0x0042DF40). Our ExtractEventParams
	// stores them with '_'-prefixed names (_scriptInstance, _callbackId,
	// _filterValue, _msgName, _funcName). Strip them so the remaining
	// real params align correctly with trigger scope entries.
	{
		int dst = 0;
		for (i = 0; i < numParams; i++) {
			if (params[i].name != NULL && params[i].name[0] == '_')
				continue;
			if (dst != i)
				params[dst] = params[i];
			dst++;
		}
		numParams = dst;
	}

	result = 1;
#ifdef DEBUG_SPEECH
	fprintf(stderr, "DISPATCH: event=0x%02X entity=0x%08X numTriggers=%d numParams=%d\n", eventType, entity->serial, numTriggers, numParams);
	for (i = 0; i < numParams; i++)
		fprintf(stderr, "  param[%d]: name=%s type=%d ival=0x%X\n", i, params[i].name ? params[i].name : "(null)", params[i].type, (unsigned)params[i].ival);
#endif

	// Build binary-format EventParamBlock from EventParam array
	{
		EventParamBlock pb;
		EventParamBlock_Constructor(&pb);

		// Find the trigger scope from the first trigger to determine
		// entry types for param conversion
		if (numTriggers > 0 && trigArr[0] != 0) {
			CTrigger *firstTrig = (CTrigger *)trigArr[0];
			if (firstTrig->scope != NULL)
				EventParamBlock_BuildFromEventParams(&pb, params, numParams, firstTrig->scope);
		}

		for (i = 0; i < numTriggers; i++) {
			ScriptAttachNode *script = (ScriptAttachNode *)scriptArr[i];
			if ((uintptr_t)script->memberScope == 0xABCD) {
#ifdef DEBUG_SPEECH
				fprintf(stderr, "DISPATCH: trigger %d SKIP (marker 0xABCD)\n", i);
#endif
				continue;
			}

#ifdef DEBUG_SPEECH
			fprintf(stderr, "DISPATCH: trigger %d ExecuteTrigger...\n", i);
#endif
			result = ExecuteTrigger((void *)scriptArr[i], (CTrigger *)trigArr[i], &pb);
#ifdef DEBUG_SPEECH
			fprintf(stderr, "DISPATCH: trigger %d result=%d\n", i, result);
#endif

			if (result == 0)
				break;
		}

		// EventParamBlock dtor handles cleanup of tracked STRING/LIST objects
		EventParamBlock_Destructor(&pb);
	}

	g_ScriptRecursionDepth--;
	CVector_Destructor(&scriptVec);
	CVector_Destructor(&trigVec);
	return result;
}

/*
 * 0x0042B951 - DispatchEvent (va_list entry point)
 *
 * Non-variadic version that takes an existing va_list.
 * Used by Entity_ExecuteEvent to forward the caller's va_list
 * directly, matching the binary's architecture where
 * Entity_ExecuteEvent (0x0042B92F) passes entity pointer and
 * &first_vararg to DispatchEvent (0x0042B951).
 */
int
CWombatManager_FireTrigger_va(CItem *entity, int eventType, va_list ap)
{
	return DispatchEvent(entity, eventType, ap);
}

/*
 * 0x0042D8FA - ExecuteTrigger
 *
 * Runs one trigger:
 *   1. Read the trigger scope from CTrigger->scope.
 *   2. Allocate and construct a CExecThread.
 *   3. memcpy the param block data into the scope.
 *   4. For scope entries past the param data, copy-construct empty
 *      CString/CUString/CList from globals.
 *   5. Append a 4-byte zero sentinel to the scope.
 *   6. Run the TreeEvaluator loop.
 *   7. Destroy the scope locals and ScalarDelete the thread.
 */
int
ExecuteTrigger(ScriptAttachNode *scriptNode, CTrigger *trigger, EventParamBlock *paramBlock)
{
	CFuncScope *trigScope;
	CNamedScopeEntry *entries;
	CExecThread *thread;
	int result;
	int i;
	uintptr_t zero;
	void *strObj;
	void *ustrObj;
	void *listObj;

	trigScope = trigger->scope;
	entries = (CNamedScopeEntry *)trigScope->namedScope.entries;

	thread = (CExecThread *)malloc(sizeof(CExecThread));
	if (thread != NULL)
		CExecThread_Constructor(thread, trigScope, scriptNode);

	if (paramBlock->byteCount != 0) {
		memcpy(thread->scope.data, paramBlock->data, paramBlock->byteCount);
	}

	if ((int)paramBlock->byteCount >= trigScope->namedScope.totalSize)
		goto skip_init;

	i = 0;
	while (entries[i].offset != (int)paramBlock->byteCount)
		i++;

	// Allocate empty objects for locals beyond the param block
	for (; i < trigScope->namedScope.count; i++) {
		int typeId = entries[i].typeId;
		switch (typeId) {
		case WTYPE_STRING: {
			CString *str = (CString *)malloc(sizeof(CString));
			if (str != NULL)
				CString_CopyConstructor(str, &g_EmptyCString);
			strObj = str;
			memcpy(thread->scope.data + entries[i].offset, &strObj, sizeof(void *));
			break;
		}
		case WTYPE_USTRING: {
			CUString *ustr = (CUString *)malloc(sizeof(CUString));
			if (ustr != NULL)
				CUString_CopyConstructor(ustr, &g_EmptyCUString);
			ustrObj = ustr;
			memcpy(thread->scope.data + entries[i].offset, &ustrObj, sizeof(void *));
			break;
		}
		case WTYPE_LIST: {
			CList *list = (CList *)malloc(sizeof(CList));
			if (list != NULL)
				CList_Constructor(list);
			listObj = list;
			memcpy(thread->scope.data + entries[i].offset, &listObj, sizeof(void *));
			break;
		}
		}
	}

skip_init:
	zero = 0;
	CScope_Append(&thread->scope, &zero, sizeof(void *));

	do {
		if (ThreadList_GetCurrent(&g_activeThreadList) != thread)
			ThreadList_MoveToHead(&g_activeThreadList, thread);
		result = TreeEvaluator(thread);
	} while (result != 0);

	ThreadList_Remove(&g_activeThreadList, thread);
	result = thread->returnVal;

	if ((int)paramBlock->byteCount >= trigScope->namedScope.totalSize)
		goto skip_cleanup;

	i = 0;
	while (entries[i].offset != (int)paramBlock->byteCount)
		i++;

	// Cleanup loop - destroy locals
	for (; i < trigScope->namedScope.count; i++) {
		int typeId = entries[i].typeId;
		switch (typeId) {
		case WTYPE_STRING:
			memcpy(&strObj, thread->scope.data + entries[i].offset, sizeof(void *));
			if (strObj != NULL)
				CString_ScalarDelete((CString *)strObj, 1);
			break;
		case WTYPE_USTRING:
			memcpy(&ustrObj, thread->scope.data + entries[i].offset, sizeof(void *));
			if (ustrObj != NULL)
				CUString_ScalarDelete((CUString *)ustrObj, 1);
			break;
		case WTYPE_LIST:
			memcpy(&listObj, thread->scope.data + entries[i].offset, sizeof(void *));
			if (listObj != NULL)
				CList_ScalarDelete((CList *)listObj, 1);
			break;
		}
	}

skip_cleanup:
	CExecThread_ScalarDelete(thread, 1);

	return result;
}

/*
 * 0x0042DD90 - EventParamBlock constructor
 *
 * Initializes byteCount to 0 and constructs three CVector tracking
 * vectors for STRING, USTRING, and LIST heap objects.
 */
void
EventParamBlock_Constructor(EventParamBlock *pb)
{
	pb->byteCount = 0;
	CVector_Constructor(&pb->stringVec, "");
	CVector_Constructor(&pb->ustringVec, "");
	CVector_Constructor(&pb->listVec, "");
}

/*
 * 0x0042DE10 - EventParamBlock destructor
 *
 * Iterates string and list tracking vectors, calling ScalarDelete on
 * each tracked object. Destroys all three CVector objects.
 */
void
EventParamBlock_Destructor(EventParamBlock *pb)
{
	int i, count;
	uintptr_t *base;

	// Destroy tracked CString objects
	count = CVector_GetCount(&pb->stringVec);
	base = (uintptr_t *)pb->stringVec.begin;
	for (i = 0; i < count; i++) {
		CString *str = (CString *)base[i];
		if (str != NULL)
			CString_ScalarDelete(str, 1);
	}

	// Destroy tracked CList objects
	count = CVector_GetCount(&pb->listVec);
	base = (uintptr_t *)pb->listVec.begin;
	for (i = 0; i < count; i++) {
		CList *list = (CList *)base[i];
		if (list != NULL)
			CList_ScalarDelete(list, 1);
	}

	CVector_Destructor(&pb->listVec);
	CVector_Destructor(&pb->ustringVec);
	CVector_Destructor(&pb->stringVec);
}

/*
 * 0x0042DF40 - EventParamBlock::AddParam
 *
 * Appends a typed value to the param block's data buffer.
 * STRING/USTRING/LIST values are tracked in their respective vectors
 * for cleanup by the destructor.
 */
void
EventParamBlock_AddParam(EventParamBlock *pb, int type, uintptr_t value)
{
	int size;

	switch (type) {
	case WTYPE_INT:
	case WTYPE_OBJ: {
		// Widened to sizeof(void*) to match scope variable layout on 64-bit
		uintptr_t v = value;
		memcpy(pb->data + pb->byteCount, &v, sizeof(void *));
		pb->byteCount += sizeof(void *);
		break;
	}
	case WTYPE_STRING: {
		// Heap-allocate CString via copy ctor from const char*
		CString *str = (CString *)malloc(sizeof(CString));
		if (str != NULL)
			CString_Constructor(str, (const char *)value);
		CVector_PushBack(&pb->stringVec, (uintptr_t)str);
		memcpy(pb->data + pb->byteCount, &str, sizeof(void *));
		pb->byteCount += sizeof(void *);
		break;
	}
	case WTYPE_USTRING: {
		// Heap-allocate CUString via copy ctor
		CUString *ustr = (CUString *)malloc(sizeof(CUString));
		if (ustr != NULL)
			CUString_CopyConstructor(ustr, (CUString *)value);
		CVector_PushBack(&pb->ustringVec, (uintptr_t)ustr);
		memcpy(pb->data + pb->byteCount, &ustr, sizeof(void *));
		pb->byteCount += sizeof(void *);
		break;
	}
	case WTYPE_LIST: {
		// Track CList pointer
		CList *list = (CList *)value;
		CVector_PushBack(&pb->listVec, (uintptr_t)list);
		memcpy(pb->data + pb->byteCount, &list, sizeof(void *));
		pb->byteCount += sizeof(void *);
		break;
	}
	default:
		// Generic: use type descriptor table for size
		size = g_WombatTypeSizes[type];
		memcpy(pb->data + pb->byteCount, &value, size);
		pb->byteCount += (size + 3) & ~3;
		break;
	}
}
