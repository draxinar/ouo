#ifndef CONVO_H_
#define CONVO_H_

#include <stdint.h>

/*
 * NPC Conversation System.
 * Binary: CConversationManager singleton at 0x0064b338.
 * Dispatch: 0x0048D7E6 (speechNotifyNearby) -> 0x0042B92F (fireEvent).
 */

#include "cstring.h"

__extension__ typedef struct CStringMatcher CStringMatcher;

/*
 * Pattern matcher capture state (0x10 bytes; g_StringMatcher at
 * 0x0063F978).
 */
struct CStringMatcher {
	char **bufArray;        // 0x00 - array of capture buffer pointers
	int numBuffers;         // 0x04
	int bufLimit;           // 0x08 - max chars per capture buffer
	int counter;            // 0x0C - buffer index during matching
};

__extension__ typedef struct CFragListNode CFragListNode;
__extension__ typedef struct CFragmentList CFragmentList;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CConversationManager CConversationManager;
__extension__ typedef struct CDefine CDefine;
__extension__ typedef struct CFragment CFragment;

/*
 * std::list<CString> node (0x18 bytes). Doubly-linked circular list
 * entry holding one CString payload at offset 0x08.
 */
struct CFragListNode {
	CFragListNode *next;            // 0x00
	CFragListNode *prev;            // 0x04
	CString str;                    // 0x08
};

/*
 * Doubly-linked list of CString fragments (0x0C bytes). Heap-allocated
 * and hung off CNPC+0x408; NULL when the NPC has no fragments.
 */
struct CFragmentList {
	uint8_t flag;                   // 0x00 - unused comparator byte
	uint8_t _pad_flag[3];           // 0x01
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;                // 64-bit alignment pad
#endif
	CFragListNode *header;          // 0x04 - sentinel; self-referential when empty
	int32_t count;                  // 0x08
};

/*
 * Macro definition node (12 bytes). Singly-linked list at
 * g_ConvoMgr.defineList.
 */
// clang-format off
struct CDefine {
	char *name;                     // 0x00
	char *value;                    // 0x04
	CDefine *next;                  // 0x08
};
// clang-format on

/*
 * Deduplicated string node (8 bytes). CFragment's AddResponseToGrid
 * hangs copies of response text and key patterns off CFragment.dedupList.
 */
__extension__ typedef struct CFragDedupNode CFragDedupNode;
// clang-format off
struct CFragDedupNode {
	char *name;                     // 0x00
	CFragDedupNode *next;           // 0x04
};
// clang-format on

/*
 * One conversation fragment (0x24C bytes). Singly-linked off
 * g_ConvoMgr.fragmentList. data is indexed by [topic][response][entry]
 * with strides 0x90 / 0x18 / 4.
 */
// clang-format off
struct CFragment {
	char *name;                     // 0x00
	CFragment *next;                // 0x04
	uintptr_t *data[4][6][6];       // 0x08
	CFragDedupNode *dedupList;      // 0x248
};
// clang-format on

/*
 * Singleton conversation manager (0x18 bytes; g_ConvoMgr at 0x0064b338).
 * Holds parser state and the heads of the fragment and define lists.
 */
// clang-format off
struct CConversationManager {
	int lineNumber;                 // 0x00 - parser: current line
#if __SIZEOF_POINTER__ == 8
	int _pad64;                     // 64-bit alignment pad
#endif
	char *lineStartPtr;             // 0x04 - parser: start of current line
	char *filename;                 // 0x08 - parser: current filename
	CFragment *defaultFrag;         // 0x0C - "Britannia_Default"
	CFragment *fragmentList;        // 0x10
	CDefine *defineList;            // 0x14
};
// clang-format on

/*
 * Base parser helper node for CKey and CSophLevel (12 bytes; vtable at
 * 0x005eea40).
 */
__extension__ typedef struct CConvoLinkedNode CConvoLinkedNode;
// clang-format off
struct CConvoLinkedNode {
	int type;                       // 0x00 - 0: CSophLevel, 1: CKey
#if __SIZEOF_POINTER__ == 8
	int _pad64;                     // 64-bit alignment pad
#endif
	CConvoLinkedNode *next;         // 0x04
	union {
		int value;              // 0x08 - CSophLevel: numeric level
		char *pattern;          // 0x08 - CKey: heap-allocated pattern
	} u;
};
// clang-format on

extern char *g_responseTexts[];   // 0x00642990 (max 0x800 entries)
extern char *g_responseKeys[];    // 0x0063f988 (max 0x800 entries)

extern CStringMatcher g_StringMatcher; // 0x0063F978
extern char *g_responseKeys[0x800]; // 0x0063f988
extern const char *g_speechText; // 0x00642988
extern char *g_responseTexts[0x800]; // 0x00642990
extern int g_responseCount; // 0x00644990
extern CConversationManager g_ConvoMgr; // 0x0064b338

char *CConversationManager_MatchSpeech(CConversationManager *this, CItem *npc, CItem *target, char *text); // 0x004210A0
void CConversationManager_Init(CConversationManager *this); // 0x00431240
void SaveAll_MultiManager(void); // 0x00431382
CFragment *CFragment_Init(CFragment *this, const char *name); // 0x0044A936
void CFragment_Destroy(CFragment *this); // 0x0044AA3A
void CFragment_CollectKeyResponses(CFragment *this, int soph, int attitude, int notoriety); // 0x0044B1BD
void CFragment_CollectResponses(CFragment *this, int soph, int attitude, int notoriety); // 0x0044B250
CDefine *CDefine_Init(CDefine *this, const char *name, const char *value); // 0x0044B3C0
void CDefine_Destructor(CDefine *this); // 0x0044B445
void CDefine_Substitute(CDefine *this, char *outBuf, int maxLen); // 0x0044B47B
void CConversationManager_startup(CConversationManager *this); // 0x0044B590
CFragment *CConversationManager_FindOrCreateFragment(CConversationManager *this, const char *name); // 0x0044B5C6
char *CConversationManager_ReadToken(CConversationManager *this, char *pos, char *outBuf, int maxLen); // 0x0044B659
char *CConversationManager_ReadFileToBuffer(CConversationManager *this, const char *filename); // 0x0044BB10
int CConversationManager_LoadDefines(CConversationManager *this); // 0x0044BBC4
int CConversationManager_LoadFragments(CConversationManager *this); // 0x0044BDE2
int CConversationManager_LoadFragment(CConversationManager *this, const char *fragName); // 0x0044C0BB
CFragment *CConversationManager_FindFragByName(CConversationManager *this, const char *name); // 0x0044CC58
char *CConversationManager_InternalFindResponse(CItem *npc, CItem *speaker, CItem *about, char *text); // 0x0044D45C

#endif /* CONVO_H_ */
