#ifndef WOMBAT_H_
#define WOMBAT_H_

/*
 * Wombat script system: bytecode tokenizer, parser, and VM. Compiled .m
 * files are a stream of 2-byte tokens; each type has up to 5 variant
 * encodings via g_TokenVariantTable (0x00610B40). Text-based trigger
 * names (0x42-0x88) compare as strings. T_STR/T_ID carry a 2-byte sdb
 * index; T_BYTE/T_WORD/T_DWORD carry 3/4/6 bytes inline.
 */

#include <stdint.h>
#include <stdio.h>

#include "cstring.h"
#include "ustring.h"
#include "location.h"
#include "streambuf.h"

/*
 * Bytecode token type IDs (138 entries, 0x00-0x89). The variant-encoded
 * range uses g_TokenVariantTable; the trigger-name range (0x42-0x88) is
 * compared by string.
 */
enum {
	/* Punctuation / symbols (variant-encoded) */
	SM_LPAREN = 0x02,   /* ( */
	SM_RPAREN = 0x03,   /* ) */
	SM_COMMA = 0x04,    /* , */
	SM_SEMI = 0x06,     /* ; */
	SM_LBRACE = 0x07,   /* { */
	SM_RBRACE = 0x08,   /* } */
	SM_LBRACKET = 0x09, /* [ */
	SM_RBRACKET = 0x0A, /* ] */

	/* Operators (variant-encoded) */
	OP_NOT = 0x0B,    /* ! */
	OP_ADD = 0x0C,    /* + */
	OP_SUB = 0x0D,    /* - */
	OP_MUL = 0x0E,    /* * */
	OP_DIV = 0x0F,    /* / */
	OP_MOD = 0x10,    /* % */
	OP_ISEQ = 0x11,   /* == */
	OP_ISNEQ = 0x12,  /* != */
	OP_LT = 0x13,     /* < */
	OP_GT = 0x14,     /* > */
	OP_LTEQ = 0x15,   /* <= */
	OP_GTEQ = 0x16,   /* >= */
	OP_ASSIGN = 0x17, /* = */
	OP_LOGAND = 0x18, /* && */
	OP_LOGOR = 0x19,  /* || */
	OP_XOR = 0x1A,    /* ^ */
	OP_INC = 0x1B,    /* ++ */
	OP_DEC = 0x1C,    /* -- */

	/* Type keywords (variant-encoded) */
	TK_INT = 0x1D,
	TK_STRING = 0x1E,
	TK_USTRING = 0x1F, /* unsigned string */
	TK_LOC = 0x20,
	TK_OBJ = 0x21,
	TK_LIST = 0x22,
	TK_VOID = 0x23,

	/* Misc (variant-encoded) */
	T_OFFSET = 0x24,

	/* Control flow (variant-encoded) */
	TK_IF = 0x25,
	TK_ELSE = 0x26,
	TK_ENDIF = 0x27,
	TK_WHILE = 0x28,
	TK_ENDWHILE = 0x29,
	TK_FOR = 0x2A,
	TK_ENDFOR = 0x2B,
	TK_CONTINUE = 0x2C,
	TK_BREAK = 0x2D,
	TK_GOTO = 0x2E,
	TK_SWITCH = 0x2F,
	TK_ENDSWITCH = 0x30,
	TK_CASE = 0x31,
	TK_DEFAULT = 0x32,
	TK_RETURN = 0x33,

	/* Top-level declarations (variant-encoded) */
	TK_FUNCTION = 0x34,
	TK_TRIGGER = 0x35,
	TK_MEMBER = 0x36,
	TK_INHERITS = 0x37,
	TK_FORWARD = 0x38,

	/* Data tokens (variant-encoded) */
	T_STR = 0x3A,   /* string literal: 2-byte sdb index follows */
	T_BYTE = 0x3C,  /* 3 bytes inline data follows */
	T_WORD = 0x3D,  /* 4 bytes inline data follows */
	T_DWORD = 0x3E, /* 6 bytes inline data follows */
	T_ID = 0x41,    /* identifier: 2-byte sdb index follows */

	/* Text-based trigger names (string comparison, 0x42..0x88) */
	TR_SPEECH = 0x42,
	TR_GOTATTACKED = 0x43,
	TR_KILLEDTARGET = 0x44,
	TR_AVERSION = 0x45,
	TR_DEATH = 0x46,
	TR_SAWDEATH = 0x47,
	TR_FIGHTPULSE = 0x48,
	TR_WASHIT = 0x49,
	TR_FAILFOOD = 0x4A,
	TR_FAILDESIRE = 0x4B,
	TR_FAILSHELTER = 0x4C,
	TR_FOUNDFOOD = 0x4D,
	TR_FOUNDDESIRE = 0x4E,
	TR_FOUNDSHELTER = 0x4F,
	TR_TIME = 0x50,
	TR_CREATION = 0x51,
	TR_ENTERRANGE = 0x52,
	TR_LEAVERANGE = 0x53,
	TR_LOITER = 0x54,
	TR_SEEKFOOD = 0x55,
	TR_SEEKDESIRE = 0x56,
	TR_SEEKSHELTER = 0x57,
	TR_MESSAGE = 0x58,
	TR_USE = 0x59,
	TR_TARGETOBJ = 0x5A,
	TR_TARGETLOC = 0x5B,
	TR_WEATHER = 0x5C,
	TR_WASDROPPED = 0x5D,
	TR_LOOKEDAT = 0x5E,
	TR_GIVE = 0x5F,
	TR_WASGOTTEN = 0x60,
	TR_PATHFOUND = 0x61,
	TR_PATHNOTFOUND = 0x62,
	TR_CALLBACK = 0x63,
	TR_ISHITTING = 0x64,
	TR_CONVOFUNC = 0x65,
	TR_TYPESELECTED = 0x66,
	TR_HUESELECTED = 0x67,
	TR_MOON = 0x68,
	TR_MINRANGEATTACK = 0x69,
	TR_MINRANGEDEFEND = 0x6A,
	TR_MAXRANGEATTACK = 0x6B,
	TR_MAXRANGEDEFEND = 0x6C,
	TR_DESTROYED = 0x6D,
	TR_EQUIP = 0x6E,
	TR_UNEQUIP = 0x6F,
	TR_ISSTACKABLEON = 0x70,
	TR_STACKONTO = 0x71,
	TR_MULTIRECYCLE = 0x72,
	TR_DECAY = 0x73,
	TR_SERVERSWITCH = 0x74,
	TR_OORUSE = 0x75,
	TR_ACQUIREDESIRE = 0x76,
	TR_LOGOUT = 0x77,
	TR_OBJECTLOADED = 0x78,
	TR_GENERICGUMP = 0x79,
	TR_OORTARGETOBJ = 0x7A,
	TR_PKPOST = 0x7B,
	TR_TEXTENTRY = 0x7C,
	TR_SHOP = 0x7D,
	TR_STOLENFROM = 0x7E,
	TR_OBJACCESS = 0x7F,
	TR_ISHEALTHY = 0x80,
	TR_ONLINE = 0x81,
	TR_TRANSACCOUNTCHECK = 0x82,
	TR_TRANSRESPONSE = 0x83,
	TR_CANBUY = 0x84,
	TR_MOBISHITTING = 0x85,
	TR_FAMECHANGED = 0x86,
	TR_KARMACHANGED = 0x87,
	TR_MURDERCOUNTCHANGED = 0x88,

	TOKEN_TYPE_COUNT = 0x8A, /* g_MaxTokenType + 1 */
};

/*
 * TokenTypeEntry
 *
 * One row of g_TokenTypeTable (12 bytes): maps a script token id to its
 * name and a flag indicating whether matching goes through the variant
 * table or falls back to a direct string compare. The table itself
 * (g_TokenTypeTable at 0x00611318) is 138 entries; the C source uses
 * g_TriggerNames as a derived view of the .str column for rows
 * 0x42..0x88.
 */
__extension__ typedef struct TokenTypeEntry TokenTypeEntry;
struct TokenTypeEntry {
	int isValid;     // always 1 for valid entries
	int hasVariants; // nonzero = use variant table; 0 = use string compare
	const char *str; // trigger name string (for text-based tokens)
};

/*
 * One implicit trigger parameter (name + WTYPE_* id).
 */
__extension__ typedef struct TrigParamDef TrigParamDef;
struct TrigParamDef {
	const char *name;
	int typeId;
};

#define MAX_TRIG_PARAMS 4

/*
 * One row of the per-event parameter table at 0x005EE45C
 * (71 entries, one per trigger event).
 */
__extension__ typedef struct TriggerParamTableEntry TriggerParamTableEntry;
struct TriggerParamTableEntry {
	int count;
	TrigParamDef params[MAX_TRIG_PARAMS];
};

#define TRIGGER_EVENT_COUNT 71

/*
 * 0x0063D8E8 - g_ScriptStringDB
 *
 * Global script string database, loaded from sdb.txt on first script load.
 * Binary: standalone CScriptStringDB at 0x0063D8E8.
 */

#define BUILTIN_HANDLER_COUNT 768

/*
 * One row of the built-in handler table at 0x00606EA0 (16 bytes, 768
 * entries, NULL-terminated). Rows 0-19 are control-flow tokens, 20-53
 * arithmetic/logic operators, 54-59 typed assignments, 60-767 game API.
 *
 * typeSig format: first char is return type (v=void, lowercase = value).
 * Remaining chars are params: lowercase = rvalue (i/s/q/c/o/l); uppercase
 * = lvalue ref (I/S/Q/C/O); | = optional-param separator; * = rest;
 * u = any. E.g. "iii" is int op(int,int); "vIi" is assignint.
 */
__extension__ typedef struct BuiltinHandlerEntry BuiltinHandlerEntry;
struct BuiltinHandlerEntry {
	const char *name;    // +0x00
	uintptr_t handler;   // +0x04
	uint32_t opcode;     // +0x08
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;     // 64-bit alignment pad
#endif
	const char *typeSig; // +0x0C
};

/*
 * 0x0040CF6E - LookupHandler
 *
 * Searches the built-in handler table for a matching name and type signature.
 * For entries 0-19 (control flow), matches by token type comparison.
 * For all others, matches by strcmp on name, then signature matching:
 *   - 'u' in caller sig = wildcard (match any)
 *   - '*' = match rest of args
 *   - Otherwise: character-by-character comparison
 *
 * Returns pointer to the matching BuiltinHandlerEntry, or NULL.
 */

/*
 * 0x004290EF - SigCharToTypeId
 *
 * Maps a script type signature character (i/s/q/c/o/l/v/u) to its
 * WTYPE_* index. Returns WTYPE_COUNT (8) when the character is not
 * recognised.
 */

/*
 * 0x0042912B - GetTypeId
 *
 * Maps a type keyword in tokenBuf to a type index. Type codes:
 *   0 = int ('i'),  1 = string ('s'), 2 = ustring ('q'),
 *   3 = loc ('c'),  4 = obj ('o'),    5 = list ('l'),
 *   6 = void ('v'), 7 = unknown ('u')
 * Returns 8 (WTYPE_COUNT) when the token does not match any type.
 */
#define WTYPE_INT     0
#define WTYPE_STRING  1
#define WTYPE_USTRING 2
#define WTYPE_LOC     3
#define WTYPE_OBJ     4
#define WTYPE_LIST    5
#define WTYPE_VOID    6
#define WTYPE_UNKNOWN 7
#define WTYPE_COUNT   8

/* Type data char codes (binary: byte at 0x005EE1F4 + type*0x14) */
extern const char g_WombatTypeCodes[WTYPE_COUNT]; /* i, s, q, c, o, l, v, u */

/* Type sizes in bytes (binary: int at 0x005EE1F8 + type*0x14) */
extern const int g_WombatTypeSizes[WTYPE_COUNT]; /* 8, 8, 8, 6, 8, 8, 0, 0 (64-bit) */

#define WOMBAT_ARRAY_MAX_DIM   1024
#define WOMBAT_ARRAY_MAX_ID    256
#define WOMBAT_ARRAY_TYPE_INT  0
#define WOMBAT_ARRAY_TYPE_STR  1
#define WOMBAT_ARRAY_TYPE_USTR 2

/*
 * Scripted 2D array (12 bytes). Held in std::map<int, WombatArray*> at
 * 0x0063D840. Data is a flat buffer of width*(height+1) uintptr_t; row 0
 * stores per-column type tags, rows 1..height hold the values. Max
 * dimensions 1024x1024.
 */
__extension__ typedef struct WombatArray WombatArray;
struct WombatArray {
	int width;       // +0x00
	int height;      // +0x04
	uintptr_t *data; // +0x08
};

/*
 * Trigger event names - binary table at 0x005EE45C.
 * 71 entries (0..70), lowercase strings, 8 bytes per entry.
 * These match the bytecode trigger name tokens (0x42..0x88)
 * but use lowercase names without TR_ prefix.
 */

#define OPERATOR_COUNT 19

/*
 * One row of the 19-entry operator table mapping token types to built-in
 * operator handler names (16 bytes). Read by ClassifyStatement (0x0042A5E0)
 * and the expression evaluator.
 */
__extension__ typedef struct OperatorEntry OperatorEntry;
struct OperatorEntry {
	int tokenType;    // +0x00
#if __SIZEOF_POINTER__ == 8
	int _pad64;       // 64-bit alignment pad
#endif
	const char *name; // +0x04 (handler name in g_BuiltInFuncs)
	int oprId;        // +0x08 (1-18, 0 for oprlist)
};

__extension__ typedef struct WombatExecCtx WombatExecCtx;

#define RNODE_LOC_RESULT 10
#define RNODE_OBJ_RESULT 11

/*
 * One link in an expression-evaluation chain (16 bytes). Allocated from
 * g_NodePool (0x0063E150); AllocResultNode at 0x0042B317, free at 0x0042B326.
 *
 * type codes (binary 0-9, plus 10/11 added for semantic tracking):
 *   0  handler result       value = BuiltinHandlerEntry *
 *   1  script function ref  value = function index
 *   2  trigger var rvalue   value = WombatVar *   - StoreTriggerVarResult
 *   3  local var lvalue     value = WombatVar *   - StoreLVarRef
 *   4  local var rvalue     value = WombatVar *   - StoreLocalVarResult
 *   5  trigger var lvalue   value = WombatVar *   - StoreTVarRef
 *   6  integer literal      value = int
 *   7  string literal       value = CString *
 *   8  ustring/member ref   value = CUString *
 *   9  goto label           value = char * (malloc'd name)
 *  10  location result      value = packed x|y, extra = z
 *  11  object result        value = serial
 *
 * ResolveResultType flags types 3 and 5 as lvalues; assign handlers
 * (assignint, assignstr, ...) require an lvalue on the LHS. Types 10-11
 * carry overwritten raw bytes and fall through to WTYPE_UNKNOWN.
 */
__extension__ typedef struct ResultNode ResultNode;
struct ResultNode {
	uint32_t type;    // +0x00
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;  // 64-bit alignment pad
#endif
	uintptr_t value;  // +0x04
	ResultNode *next; // +0x08
	uintptr_t extra;  // +0x0C
};

/*
 * ResultNode pool allocator (singleton g_NodePool at 0x0063E150, batch
 * size 0x1000). Free list runs through ResultNode.next; empty batches
 * allocate [4-byte count][count * ResultNode], returning node 0 and
 * chaining the rest.
 */
__extension__ typedef struct NodePool NodePool;
struct NodePool {
	ResultNode *head; // +0x00
	int batchSize;    // +0x04
	int flag;         // +0x08 (alloc-in-progress)
};

extern NodePool g_NodePool; /* 0x0063E150 */

/*
 * 0x0042929A - StoreTypesFromSig
 *
 * Walks the parameter portion of a type signature (skipping the
 * leading return-type character) and annotates each ResultNode in
 * the chain with the matching parameter type. Stops at NUL or '|',
 * leaving optional parameters at default types.
 */
struct CFunction; /* forward declaration */

/*
 * Flat runtime-variable byte buffer (48 bytes). CNamedScope supplies
 * the name-to-offset-to-type mappings; CScope stores the raw values.
 * The three child vectors own the CString/CUString/Result5 heap objects
 * referenced from the buffer.
 */
__extension__ typedef struct CScope CScope;
struct CScope {
	int capacity;       // +0x00
	int usedBytes;      // +0x04
	char *data;         // +0x08
	void **children1;   // +0x0C (CString *)
	int childCount1;    // +0x10
	int childCapacity1; // +0x14
	void **children2;   // +0x18 (CUString *)
	int childCount2;    // +0x1C
	int childCapacity2; // +0x20
	void **children3;   // +0x24 (Result5 *)
	int childCount3;    // +0x28
	int childCapacity3; // +0x2C
};

/* 0x00409A41 - CScope_StoreValue: pop data from scope buffer end
 * into dest, then decrement usedBytes */

/* 0x004093F0 - CScope_PushValue: insert value before scope tail,
 * shifting the last 4 bytes forward and growing if needed */

/* 0x00408D6A - CScope_Append: append raw data to end of scope buffer,
 * growing if needed. Unlike PushValue (inserts before tail) and
 * StoreValue (pops from end), this is a simple forward append. */

#include "list.h"

/*
 * Stack-on-dynarray used for both handler and result stacks inside
 * CExecThread (12 bytes). Starts with a NULL sentinel at arr[0].
 */
__extension__ typedef struct CNodeList CNodeList;
struct CNodeList {
	int capacity;   // +0x00
	int count;      // +0x04
	uintptr_t *arr; // +0x08
};

/*
 * Binary execution-thread record (108 bytes). Linked into two separate
 * doubly-linked lists: global (all threads, 0x0063D82C) via globalNext/
 * globalPrev, and per-manager active via activeNext/activePrev.
 */
__extension__ typedef struct CExecThread CExecThread;
struct CExecThread {
	char *stream;            // +0x00 (bytecode position)
	void *scriptRef;         // +0x04 (CScript** or entity ref)
	CScope scope;            // +0x08
	CNodeList hstack;        // +0x38 (handler stack)
	CNodeList rstack;        // +0x44 (result stack)
	uint32_t finished;       // +0x50
	int returnVal;           // +0x54
	int defaultReturn;       // +0x58 (init 1)
#if __SIZEOF_POINTER__ == 8
	int _pad64;              // 64-bit alignment pad
#endif
	CExecThread *globalNext; // +0x5C
	CExecThread *globalPrev; // +0x60
	CExecThread *activeNext; // +0x64
	CExecThread *activePrev; // +0x68
};

/*
 * Active execution-thread list head (8 bytes). Threads are chained via
 * CExecThread.activeNext/activePrev.
 */
__extension__ typedef struct ThreadList ThreadList;
struct ThreadList {
	CExecThread *head; // +0x00
	int count;         // +0x04
};

/*
 * Function list embedded in CScript at +0x04 (8 bytes). array is a
 * heap-allocated CFunction[count] (16 bytes each).
 */
__extension__ typedef struct CFuncList CFuncList;
struct CFuncList {
	int count;   // +0x00
#if __SIZEOF_POINTER__ == 8
	int _pad64;  // 64-bit alignment pad
#endif
	void *array; // +0x04
};

/*
 * One name/type/offset declaration (12 bytes). name points into the SDB
 * string pool - not malloc'd.
 */
__extension__ typedef struct CNamedScopeEntry CNamedScopeEntry;
struct CNamedScopeEntry {
	char *name; // +0x00
	int typeId; // +0x04 (WTYPE_*)
	int offset; // +0x08 (byte offset in CScope buffer)
};

/*
 * Variable-declaration scope (12 bytes). Holds a CNamedScopeEntry array
 * and the running total of aligned type sizes needed in the paired
 * CScope byte buffer.
 */
__extension__ typedef struct CNamedScope CNamedScope;
struct CNamedScope {
	int count;     // +0x00
	int totalSize; // +0x04
	void *entries; // +0x08
};

/*
 * Name-only linked-list node (8 bytes) used in CScript.trigListHead.
 * name is malloc'd.
 */
__extension__ typedef struct CNameNode CNameNode;
struct CNameNode {
	CNameNode *next; // +0x00
	char *name;      // +0x04
};

/*
 * Name+type linked-list node (12 bytes) used in CScript.memberListHead.
 */
__extension__ typedef struct CMemberNode CMemberNode;
struct CMemberNode {
	CMemberNode *next; // +0x00
	char *name;        // +0x04 (malloc'd)
	int typeId;        // +0x08 (WTYPE_*)
};

/*
 * Thiscall receiver for CScriptVar::AppendName (0x00408AF3).
 * Just a name pointer.
 */
__extension__ typedef struct CScriptVar CScriptVar;
struct CScriptVar {
	char *name; // +0x00
};

/*
 * Two-level receiver for CScriptString::Append (0x0040CC55) and
 * ::AppendInner (0x0040CC2E).
 */
__extension__ typedef struct CScriptString CScriptString;
struct CScriptString {
	CScriptVar *var;      // +0x00 (AppendInner reads here)
	CScriptString *inner; // +0x04 (Append follows this)
};

#include "stl.h"

__extension__ typedef struct CString CString;
__extension__ typedef struct ScriptAttachNode ScriptAttachNode;
#define BINARY_TRIGGER_COUNT 71 /* 0x47 */

__extension__ typedef struct CFuncScope CFuncScope;

/*
 * Loaded script object (0x148 bytes). Constructor 0x004084E8. Owns three
 * linked lists (functions, triggers, members) plus 71 per-event trigger
 * handler chain heads, and is linked into the CScriptManager loaded
 * list via nextLoaded.
 */
__extension__ typedef struct CScript CScript;
// clang-format off
struct CScript {
	char *name;                               // +0x00
	CFuncList funcList;                       // +0x04
	CNamedScope namedScope;                   // +0x0C
	void *trigHandlers[BINARY_TRIGGER_COUNT]; // +0x18
	void *funcListHead;                       // +0x134
	void *trigListHead;                       // +0x138
	void *memberListHead;                     // +0x13C
	CScript *parent;                          // +0x140
	CScript *nextLoaded;                      // +0x144
};
// clang-format on

/*
 * One trigger-handler node in a CScript.trigHandlers[eventIndex] chain
 * (24 bytes). Constructor 0x0040835E, created by AddTrigger (0x00408A54).
 *
 * filterData is strdup'd for string-filter events (0, 14, 22, 35),
 * atoi-converted for numeric-filter events (16, 17, 31, 32, 33, 36, 37,
 * 55, 58, 61), and NULL otherwise.
 */
__extension__ typedef struct CTrigger CTrigger;
struct CTrigger {
	int filter;        // +0x00 (0x3E8 by default)
	int flags;         // +0x04
	int eventIndex;    // +0x08 (0..70)
#if __SIZEOF_POINTER__ == 8
	int _pad64;        // 64-bit alignment pad
#endif
	void *filterData;  // +0x0C
	CTrigger *next;    // +0x10
	CFuncScope *scope; // +0x14
};

/*
 * One CFuncList entry (16 bytes). sig uses the BuiltinHandlerEntry
 * encoding; paramSize accumulates the parameter byte size derived from sig.
 */
__extension__ typedef struct CFunction CFunction;
struct CFunction {
	void *scope;     // +0x00 (CNamedScope/CScope)
	char *name;      // +0x04 (SDB string)
	char *sig;       // +0x08
	void *paramSize; // +0x0C
};

/*
 * Parser-context stack frame (0x54 bytes). The doubly-linked stack
 * handles nested compilations (e.g. parent scripts pulled in by
 * ParseInherits); g_ScriptCompiler (0x0063E128) is the top.
 */
__extension__ typedef struct CScriptCompiler CScriptCompiler;
struct CScriptCompiler {
	CScriptCompiler *next; // +0x00 (deeper)
	CScriptCompiler *prev; // +0x04 (shallower, NULL at top)
	char *bytecode;        // +0x08
	char name[64];         // +0x0C
	int flags;             // +0x4C
#if __SIZEOF_POINTER__ == 8
	int _pad64;            // 64-bit alignment pad
#endif
	CScript *script;       // +0x50
};

/*
 * Singleton at g_ScriptManager (0x00698988): head of the loaded-script
 * list (linked via CScript.nextLoaded) plus a first-char intern table
 * for script-name strings.
 */
__extension__ typedef struct CScriptManager CScriptManager;
struct CScriptManager {
	CScript *head;            // +0x00
	void *internBuckets[256]; // +0x04 (first-char intern hash)
};

/*
 * Function-local scope (20 bytes). Hangs off CFunction.scope. Owns the
 * parameter/local declarations plus the compiled body's result chain.
 * Constructor 0x0040CD72, destructor 0x0040CDE1.
 */
struct CFuncScope {
	CNamedScope namedScope; // +0x00
	void *resultListHead;   // +0x0C
	void *bodyStream;       // +0x10
};

/*
 * These are called as BuiltinHandlerEntry function pointers from the exec loop.
 * They modify the current exec thread's stream pointer to implement control flow.
 * All use g_activeThreadList (0x006482A0) via ThreadList_GetCurrent.
 */

/*
 * One WombatScope variable. Extends the binary's 12-byte entry with
 * value2 so loc-type vars can carry the z coordinate alongside x|y.
 */
__extension__ typedef struct WombatVar WombatVar;
struct WombatVar {
	char name[128];
	int typeId; // WTYPE_*
	int value;  // int, obj serial, or string pointer cast to int
	int value2; // loc z, otherwise unused
};

#define MAX_SCOPE_VARS 128

/*
 * Fixed-capacity variable array (our replacement for the binary's
 * linked CScope entry chain walked by 0x0040821E).
 */
__extension__ typedef struct WombatScope WombatScope;
struct WombatScope {
	WombatVar vars[MAX_SCOPE_VARS];
	int count;
};

/*
 * Statement types returned by ClassifyStatement (0x0042A5E0).
 *
 * Binary: the classifier returns 0-11, used as index into a byte table
 * at 0x00429F3B which maps to 6 jump table entries at 0x00429F23.
 *
 * Binary classification order (0x0042A5E0):
 *   1. FindVar in local scope → 1
 *   2. FindVar in trigger scope (g_ScriptCompiler->script+0xC) → 2
 *   3. Find in member/func scope (g_ScriptCompiler->script+0x4) → 7
 *   4. Loop i=0x25..0x33: CompareTokenType → 6 (control flow tokens)
 *   5. Loop g_BuiltInFuncs[]: strcmp → 6 (built-in function names)
 *   6. CompareTokenType(SM_SEMI) → 9 (no-op, skip)
 *   7. CompareTokenType(SM_RBRACE) → 6 (control flow end!)
 *   8. CompareTokenType(TK_MEMBER) → 0
 *   9. Loop type keywords (TK_INT..TK_VOID) → 0
 *  10. CompareTokenType(T_BYTE/T_WORD/T_DWORD) → 3
 *  11. tokenBuf[0] == '"' → 4
 *  12. tokenBuf[0] == 'L' && tokenBuf[1] == '"' → 5
 *  13. CompareTokenType(TK_GOTO) → 10
 *  14. CompareTokenType(TK_RETURN) → 6 (redundant with step 4)
 *  15. Loop g_OperatorTable[]: CompareTokenType → 8
 *  16. Default → 11
 *
 * Byte table at 0x00429F3B maps stmtType to jump index:
 *   [0]=0, [1]=1, [2]=1, [3]=5, [4]=5, [5]=5,
 *   [6]=2, [7]=3, [8]=5, [9]=5, [10]=4
 * (indices 3-5,8-9 all map to jump[5] = loop restart = skip)
 */
#define STMT_VAR_DECL     0     /* type keyword or TK_MEMBER */
#define STMT_LOCAL_VAR    1    /* local variable name */
#define STMT_TRIGGER_VAR  2  /* trigger/context variable */
#define STMT_INT_LITERAL  3  /* T_BYTE, T_WORD, T_DWORD */
#define STMT_STR_LITERAL  4  /* starts with '"' */
#define STMT_USTR_LITERAL 5 /* starts with 'L"' */
#define STMT_BUILTIN_CALL 6 /* control flow token, built-in name, OR SM_RBRACE */
#define STMT_MEMBER_VAR   7   /* member variable reference */
#define STMT_OPERATOR     8     /* operator from g_OperatorTable */
#define STMT_SEMI         9         /* SM_SEMI (no-op, skip) */
#define STMT_GOTO         10        /* TK_GOTO label */
#define STMT_UNKNOWN      11     /* unrecognized */

#define MAX_HANDLER_DEPTH 32

/*
 * One active control-flow block on the parser's handler stack. We store
 * a typed record; the binary stored BuiltinHandlerEntry pointers.
 */
__extension__ typedef struct HandlerStackEntry HandlerStackEntry;
struct HandlerStackEntry {
	int handlerType;       // TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_SWITCH
	int condResult;        // condition truthiness
	const char *loopStart; // saved stream for while/for loopback
	const char *incrStart; // for-loop: start of increment expr
	const char *incrEnd;   // for-loop: end of increment expr (body start)
	int switchValue;       // switch: expression value to match
	int caseMatched;       // switch: 1 once a case has matched (fall-through)
};

/*
 * Handler-stack backing the ParseStatementBlock (0x00429340) loop.
 * While depth > 1, SM_RBRACE folds into STMT_BUILTIN_CALL (pop) rather
 * than closing the outer block.
 */
__extension__ typedef struct HandlerStack HandlerStack;
struct HandlerStack {
	HandlerStackEntry entries[MAX_HANDLER_DEPTH];
	int count;
};

/*
 * Our trigger-body execution context. Functionally redundant with
 * CExecThread (the binary's 0x6C-byte thread record) and currently
 * dead code - retained because downstream code still references the type.
 */
struct WombatExecCtx {
	const char *stream;
	const char *bytecodeBase;
	void *script;
	uint32_t entitySerial;
	WombatScope localScope;
	WombatScope trigScope;   // set by DispatchEvent
	WombatScope memberScope;
	int returnVal;
	int defaultReturn;       // 1 = not consumed
	int done;                // 1 once a return has fired
#if __SIZEOF_POINTER__ == 8
	int _pad64;              // 64-bit alignment pad
#endif
	HandlerStack hstack;
};

/*
 * 0x00429F46 - EvaluateExpression
 *
 * Compile-time expression evaluator. Reads tokens from the bytecode stream,
 * builds a chain of ResultNode elements representing the expression value.
 * Uses StoreHandlerResult (not ExecuteHandler) to build handler nodes.
 */

/*
 * 0x0042A847 - ProcessAssignment
 *
 * Compile-time assignment handler. Handles var=expr, var++, var--.
 * Builds assignment handler result nodes in scope->resultListHead.
 */

/*
 * 0x0042B11B - ResolveVarValue
 *
 * Compile-time variable lookup. Searches the function scope first,
 * then falls back to the script scope (g_ScriptCompiler->script+0x0C).
 */

#define MAX_EVENT_PARAMS 8

/*
 * One named trigger-event parameter. The binary packs raw bytes into a
 * flat buffer (see EventParamBlock) keyed by bytecode offset; we carry
 * the compiler's name alongside the value.
 */
__extension__ typedef struct EventParam EventParam;
struct EventParam {
	const char *name; // e.g. "user", "speaker"
	int type;         // WTYPE_*
#if __SIZEOF_POINTER__ == 8
	int _pad64a;      // 64-bit alignment pad
#endif
	uintptr_t ival;   // int/obj, or packed loc x|y
	int ival2;        // loc z, otherwise unused
#if __SIZEOF_POINTER__ == 8
	int _pad64b;      // 64-bit alignment pad
#endif
	const char *sval; // WTYPE_STRING value, else NULL
};

/*
 * Flat parameter buffer passed to ExecuteTrigger (0xB4 bytes on 32-bit).
 * Constructor 0x0042DD90, destructor 0x0042DE10, AddParam 0x0042DF40.
 * The three CVectors own the heap objects referenced from data.
 */
__extension__ typedef struct EventParamBlock EventParamBlock;
struct EventParamBlock {
	uint32_t byteCount;     // +0x00
	char data[256];         // +0x04 (64-bit build widens from 128)
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;        // 64-bit alignment pad
#endif
	CVector stringVec;      // +0x84 (CString *)
	CVector ustringVec;     // +0x94 (CUString *)
	CVector listVec;        // +0xA4 (CList *)
};

__extension__ typedef struct CItem CItem;

/*
 * 0x0042B92F - FireTrigger
 *
 * Fires a script event on an entity.
 */

/*
 * 0x0042D8FA - ExecuteTrigger
 *
 * Runs the trigger handler for a script attachment, storing the
 * ScriptAttachNode* on thread->scriptRef so handler bindings can
 * reach the host entity during execution.
 */

/*
 * Helper - EventParamBlock_BuildFromEventParams
 *
 * Converts EventParam[] array to a binary-format EventParamBlock.
 * Bridges ExtractEventParams (custom) with ExecuteTrigger (binary).
 */

/*
 * 0x0042EDBF - CheckWalkPassability
 *
 * Collects tracking nodes from nearby spatial grid blocks (range 7),
 * fires Entity_ExecuteEvent for each to check enterrange/leaverange.
 * Returns 1 if passable, 0 if blocked.
 */

/*
 * Per-entity script attachment functions:
 *   CScriptManager::AttachScript    (0x00425F34)
 *   CResourceEntity::HasScript      (0x004CDF4B)
 *   CResourceEntity::DetachScript   (0x004CDEAC)
 *
 * Binary: each CResourceEntity has a linked list of script instances
 * at field 0x48. Scripts are attached/detached dynamically at runtime
 * via the attachScript/detachScript built-in functions.
 */

struct ScriptAttachNode;

/*
 * One block of the deferred script-creation queue (0x804 bytes): 256
 * serial/param pairs, chained via next at +0x800.
 */
__extension__ typedef struct ScriptCreationBlock ScriptCreationBlock;
struct ScriptCreationBlock {
	uint32_t serials[256];     // +0x000
	uint32_t params[256];      // +0x400
	ScriptCreationBlock *next; // +0x800
};

struct CVector;

/*
 * ObjVar persistence helpers - save/load wom_var lines.
 * Binary format: "wom_var=<type3> <name> <value>"
 * Type abbreviations: int, str, loc, obj (matching binary table at 0x005EFD48).
 * Strings use backslash-escape encoding (0x004C45D6/0x004C4643):
 *   space -> "\ ", backslash -> "\\"; space terminates unescaped value.
 */

/*
 * Binary: 0x006482A0 - g_ScriptExecContext
 * Points to the currently executing CExecThread.
 * Used by built-in handler functions to access the execution context.
 */

/*
 * Binary: 0x0063D854 - g_ScriptReturnValue
 *         0x0063D858 - g_ScriptReturnFlag
 *
 * Mechanism for scripts to pass typed return values back to callers.
 * Used by combat (washit damage override), vendor pricing, etc.
 *
 * Protocol:
 *   1. Caller saves and clears both globals
 *   2. Fires event (e.g., Entity_ExecuteEvent)
 *   3. Script calls intRet(value) → sets flag=1, value=arg
 *   4. Caller checks flag; if set, uses value and restores saved state
 *
 * Binary built-in: intRet (0x0041D875) - "vi" signature
 */
extern int g_ScriptReturnValue; /* 0x0063D854 */
extern int g_ScriptReturnFlag;  /* 0x0063D858 */

__extension__ typedef struct SeqLocNode SeqLocNode;
__extension__ typedef struct SeqCmdNode SeqCmdNode;
__extension__ typedef struct AnimSequence AnimSequence;
__extension__ typedef struct CStringMatcher CStringMatcher;

/*
 * One row of the NPC-hint table used by wombat.c hint built-ins.
 */
typedef struct {
	int32_t priority;
	uint32_t targetObj;
	int32_t value;
	char name1[64];
	char name2[128];
	int16_t locX;
	int16_t locY;
	int16_t locZ;
	uint32_t sourceObj;
	char name3[32];
	int32_t flags;
} CHintEntry;

__extension__ typedef struct EventLogger EventLogger;

/*
 * CEScript interpreter context (0x141C bytes). Holds the open source
 * file, the CEScriptVar pointer table, and the loop/line bookkeeping
 * driven by the per-line executor.
 */
__extension__ typedef struct CEScript CEScript;
struct CEScript {
	FILE *fp;             // +0x0000
	int numVars;          // +0x0004
#if __SIZEOF_POINTER__ == 8
	int _pad64;           // 64-bit alignment pad
#endif
	void *vars[1024];     // +0x0008
	CItem *player;        // +0x1008
	char scriptName[508]; // +0x100C
	int loopLines[129];   // +0x1208
	int loopDepth;        // +0x140C
	int lineNo;           // +0x1410
	int field1414;        // +0x1414
	int errorFlag;        // +0x1418
};

/*
 * CEScriptVar base class (24 bytes). type tags the variable flavor:
 * '%' for int subclasses, '$' for string subclasses.
 */
__extension__ typedef struct CEScriptVar CEScriptVar;
struct CEScriptVar {
	void **vtable;   // +0x00
	char name[16];   // +0x04 (15 chars + null)
	char type;       // +0x14
	char _esv_pad[3];
};

/*
 * Integer-typed CEScriptVar (28 bytes).
 */
__extension__ typedef struct CEScriptIntVar CEScriptIntVar;
struct CEScriptIntVar {
	CEScriptVar base;
	int value; // +0x18
};

/*
 * String-typed CEScriptVar (0x101C bytes) - holds up to numValues
 * alternative strings in the packed 4096-byte buffer.
 */
__extension__ typedef struct CEScriptStringVar CEScriptStringVar;
struct CEScriptStringVar {
	CEScriptVar base;
	int numValues;     // +0x18
	char values[4096]; // +0x1C
};

// CEScriptVar vtable slot indices (5 slots).
#define ESVAR_VT_DTOR         0  // 0x00: scalar deleting destructor
#define ESVAR_VT_GET_VALUE    1  // 0x04: GetValue
#define ESVAR_VT_SET_FROM_INT 2  // 0x08: SetFromInt
#define ESVAR_VT_SET_FROM_STR 3  // 0x0C: SetFromString
#define ESVAR_VT_CLONE        4  // 0x10: Clone

// ESVar vtable accessor macros.
#define ESVarGetValue(var)         ((int (*)(void *))(((void **)(var))[ESVAR_VT_GET_VALUE]))
#define ESVarSetFromInt(var)       ((void (*)(void *, int))(((void **)(var))[ESVAR_VT_SET_FROM_INT]))
#define ESVarSetFromString(var)    ((void (*)(void *, const char *))(((void **)(var))[ESVAR_VT_SET_FROM_STR]))
#define ESVarClone(var)            ((void *(*)(void *))(((void **)(var))[ESVAR_VT_CLONE]))
#define ESVarScalarDestructor(var) ((void *(*)(void *, int))(((void **)(var))[ESVAR_VT_DTOR]))

__extension__ typedef struct SCommandManager SCommandManager;
__extension__ typedef struct SCommandEntry SCommandEntry;

extern const uint16_t g_TokenVariants[TOKEN_TYPE_COUNT][5]; // 0x00610B40
extern CScript *g_currentCompileScript; // 0x0063D828
extern CExecThread *g_globalThreadHead; // 0x0063D82C
extern StdMapTree g_WombatArrays; // 0x0063D840
extern uint8_t g_WombatArraysDestructorFlag; // 0x0063D850
extern int g_ScriptReturnValue; // 0x0063D854
extern int g_TrigCmdDepth; // 0x0063D860
extern int g_TrigCmdRecurse; // 0x0063D864
extern ScriptAttachNode *g_scriptInstanceListHead; // 0x0063D8CC
extern int g_deferScriptCreation; // 0x0063D8E0
extern int g_scriptCreationCount; // 0x0063E0F8
extern ScriptCreationBlock *g_scriptCreationListHead; // 0x0063E0FC
extern int g_ScriptLoadCount; // 0x0063E100
extern int g_SdbLoaded; // 0x0063E104
extern CScriptCompiler *g_ScriptCompiler; // 0x0063E128
extern NodePool g_NodePool; // 0x0063E150
extern char g_ConvoReturnStr[256]; // 0x00644998
extern WombatExecCtx *g_CurrentExecCtx; // 0x006482A0
extern ThreadList g_activeThreadList; // 0x006482A0
extern int g_ScriptRecursionDepth; // 0x006482A4
extern AnimSequence g_AnimSequence; // 0x00698840
extern CScriptManager g_ScriptManager; // 0x00698988
extern CHintEntry *g_HintEntries; // 0x00698E90
extern int g_GMCallStatus; // 0x006999AC
extern CScriptStringDB g_ScriptStringDB;
extern const TriggerParamTableEntry g_TriggerParamTable[TRIGGER_EVENT_COUNT];
extern const char *g_TriggerNames[TOKEN_TYPE_COUNT];
extern const int g_TypeTokenIds[WTYPE_COUNT];
extern const char g_WombatTypeCodes[WTYPE_COUNT];
extern const int g_WombatTypeSizes[WTYPE_COUNT];
extern const char *g_TriggerEventNames[TRIGGER_EVENT_COUNT]; // 0x005EE45C
extern const char *g_womTypeNames[];        // 0x005EFD48
extern const OperatorEntry g_OperatorTable[OPERATOR_COUNT];
extern int g_ScriptReturnFlag;
extern int g_NumHints;
extern int g_HintCapacity;

int CScriptStringDB_Load(CScriptStringDB *db, const char *path); // 0x0040107D
void CScriptStringDB_BuildIndex(CScriptStringDB *db, void *arg); // 0x004011D7
const char *CScriptStringDB_Get(CScriptStringDB *db, int index); // 0x00401381
void IosBase_SetState(CIosBase *this, uint32_t state, char excflag); // 0x00401730
unsigned int char_traits_to_int_type(const char *p); // 0x00401E20
char char_traits_deref(const char *p); // 0x00401F50
char *String_Nullstr(void); // 0x00402C60
void *locale_facet_Constructor(void *facet, int refs); // 0x00402E70
void char_traits_assign(char *dest, const char *src); // 0x00403630
char *CString_Refcnt(char *data); // 0x00403830
uint32_t EventRingBuffer_Count(void); // 0x00403980
void Destroy_Single16(StdAllocator *this, void *elem); // 0x00403CA0
void ConstructN_16(StdAllocator *this, void *dest, uint32_t count, void *value); // 0x00404250
void Allocator_CopyConstruct16(StdAllocator *this, void *dest, void *src); // 0x00404380
void *ScriptStringDB_Insert(void *setObj, const char *str); // 0x004043A0
void String_CopyAssignDispatch(CSdbStr *this, void *src); // 0x00404C80
void locale_facet_Init(void *facet); // 0x00404E00
void CFuncList_Destructor(CFuncList *list); // 0x00407B51
void CFuncList_Copy(CFuncList *dst, const CFuncList *src); // 0x00407B7C
void PatchScopeRefs(ResultNode *node, void *oldEntries, void *newEntries, int count); // 0x00407BF1
CFunction *CFuncList_AddEntry(CFuncList *list, const char *name, const char *sig); // 0x00407C7A
CFunction *CFuncList_FindFunc(CFuncList *list, const char *name, int *outIndex); // 0x00407EEF
void CNamedScope_Constructor(CNamedScope *scope); // 0x00407F5D
void CNamedScope_Destructor(CNamedScope *scope); // 0x00407F88
void CNamedScope_Copy(CNamedScope *dst, const CNamedScope *src); // 0x00407FB3
int CNamedScope_Add(CNamedScope *scope, const char *name, int typeId, CFuncList *funcList); // 0x00408034
WombatVar *CScope_FindVar(WombatScope *scope, const char *name); // 0x0040821E
void *CNamedScope_FindVar(CNamedScope *scope, const char *name); // 0x0040821E
CNameNode *InitNameEntry(CNameNode *entry, const char *name); // 0x0040827D
void CNameNode_Destructor(CNameNode *node); // 0x004082BE
CMemberNode *InitVarEntry(CMemberNode *entry, const char *name, int typeId); // 0x004082E9
void CMemberNode_Destructor(CMemberNode *node); // 0x00408333
CTrigger *InitTrigger(CTrigger *trig, int filter, int eventIndex, int flags, const char *filterStr); // 0x0040835E
void CTrigger_Destructor(CTrigger *trig); // 0x0040846D
void CScript_Constructor(CScript *script, const char *name); // 0x004084E8
void CScript_Destructor(CScript *script); // 0x004085D9
CFunction *CScript_AddFunction(CScript *script, const char *name, const char *sig); // 0x0040887A
CFunction *CScript_FindFunction(CScript *script, const char *name); // 0x00408936
int CScript_AddVar(CScript *script, const char *name, int typeId); // 0x00408954
CTrigger *CScript_AddTrigger(CScript *script, int filter, int flags, int eventIndex, const char *filterStr); // 0x00408A54
int CScriptVar_AppendName(CScriptVar *this, CString *dest); // 0x00408AF3
CNameNode *CNameNode_ScalarDtor(CNameNode *this, int flags); // 0x00408B30
CMemberNode *CMemberNode_ScalarDtor(CMemberNode *this, int flags); // 0x00408B60
CTrigger *CTrigger_ScalarDtor(CTrigger *this, int flags); // 0x00408B90
void CScope_Constructor(CScope *scope); // 0x00408BC0
void CScope_Destructor(CScope *scope); // 0x00408C4E
void CScope_Append(CScope *scope, const void *source, int size); // 0x00408D6A
void CScope_Resize(CScope *scope, int newSize); // 0x00408E51
void CScope_PushDefaultString(CScope *scope); // 0x00408F12
void CScope_PushDefaultUString(CScope *scope); // 0x004090B3
void CScope_PushDefaultList(CScope *scope); // 0x00409254
void CScope_PushValue(CScope *scope, const void *source, int size); // 0x004093F0
void CScope_PushCString(CScope *scope, uintptr_t cstrPtr); // 0x00409537
void CScope_PushCUString(CScope *scope, uintptr_t custrPtr); // 0x004096E5
void CScope_PushResult5(CScope *scope, uintptr_t value); // 0x00409893
void CScope_StoreValue(CScope *scope, void *dest, int size); // 0x00409A41
void CScope_SetUsedBytes(CScope *scope, int usedBytes); // 0x00409A8A
void CNodeList_Constructor(CNodeList *list); // 0x00409AA0
void CNodeList_Destructor(CNodeList *list); // 0x00409ADD
void CNodeList_Push(CNodeList *list, uintptr_t value); // 0x00409AFF
uintptr_t CNodeList_Pop(CNodeList *list); // 0x00409B9B
uintptr_t CNodeList_Peek(CNodeList *list); // 0x00409BC4
void CExecThread_Constructor(CExecThread *thread, void *trigHandler, void *scriptInstance); // 0x00409BDF
void CExecThread_Destructor(CExecThread *thread); // 0x00409CF4
void WombatArrays_StaticInit(void); // 0x0040EFB9
void CStringMatcher_Init(CStringMatcher *sm, int numBuffers, int bufSize); // 0x004D78AE
void CStringMatcher_Destroy(CStringMatcher *sm); // 0x004D792C
int CStringMatcher_Match(CStringMatcher *sm, const char *text, const char *pattern); // 0x004D79B3
void CSdbStr_Init(CSdbStr *s, const char *src);
const char *CSdbStr_c_str(CSdbStr *s);
void CSdbStr_Destructor(CSdbStr *s);
void CScriptStringDB_PushBack(CScriptStringDB *db, CSdbStr *elem);
void CScriptStringDB_Free(CScriptStringDB *db);
void FreeResultChain(ResultNode *chain);

/*
 * Documentation-only address annotations for binary structures that the
 * C source does not expose as a single variable. These have no
 * declaration here (intentionally) so the r2 names extractor uses the
 * comment name and not a nearby declaration.
 */

/*
 * 0x005EE1EC - g_TypeTable
 *
 * Type table: 8 entries x 20 bytes. Columns:
 *   tokenType (+0x00) -> g_TypeTokenIds[]
 *   name      (+0x04) -> entries point into g_womTypeNames
 *   code      (+0x08) -> g_WombatTypeCodes[]
 *   size      (+0x0C) -> g_WombatTypeSizes[]
 *   index     (+0x10) -> implicit (0..7)
 */

/*
 * 0x00611318 - g_TokenTypeTable
 *
 * Token-type table: 138 entries of TokenTypeEntry (12 bytes each). The
 * .str field (offset +0x08) for rows 0x42..0x88 is exposed by the C
 * source as g_TriggerNames.
 */

/*
 * 0x00427DF7 - g_ParseTriggerJumpTable
 *
 * Inline switch jump table used by ParseTrigger (0x00427436): 45 dword
 * entries indexed via the dispatch byte table at 0x00427EAB. Each
 * handler calls AddVarToScope for the trigger's implicit parameter
 * variables. The C source reconstructs the per-event variable lists
 * as g_TriggerParamTable.
 */

/*
 * 0x00427EAB - g_ParseTriggerDispatch
 *
 * Inline dispatch byte table used by ParseTrigger (0x00427436): 68
 * one-byte entries mapping event index (0..67) to a handler slot in
 * g_ParseTriggerJumpTable at 0x00427DF7.
 */

#endif /* WOMBAT_H_ */
