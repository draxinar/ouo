#ifndef LIST_H_
#define LIST_H_

#include <stdint.h>

/*
 * Tagged entry in a CList (0x10 bytes). typeTag selects how value
 * should be interpreted by g_tagTypeNames-aware consumers (WombatList,
 * EventParamBlock).
 */
__extension__ typedef struct CListNode CListNode;
struct CListNode {
	CListNode *next;        // 0x00
	CListNode *prev;        // 0x04
	uint32_t typeTag;       // 0x08
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;        // 64-bit alignment pad
#endif
	uintptr_t value;        // 0x0C
};

/*
 * Doubly-linked list head (0x0C bytes) used for tagged argument
 * payloads passed between Wombat scripts and the engine.
 */
__extension__ typedef struct CList CList;
struct CList {
	CListNode *head;        // 0x00
	CListNode *tail;        // 0x04
	int count;              // 0x08
};

__extension__ typedef struct CDataBuffer CDataBuffer;

/*
 * Stateful cursor into a CList (0x08 bytes).
 */
__extension__ typedef struct CListIterator CListIterator;
struct CListIterator {
	CList *list;            // 0x00
	CListNode *current;     // 0x04
};

extern const char *g_tagTypeNames[];

CList *CList_ScalarDelete(CList *list, int flags); // 0x0040CCB0
CListNode *CListNode_BaseConstructor(CListNode *this); // 0x00424FE0
void CListNode_Constructor(CListNode *node, uint32_t typeTag, uintptr_t value); // 0x00424FEE
void CListNode_Destructor(CListNode *node); // 0x00425280
int CListNode_Match(CListNode *a, CListNode *b); // 0x00425363
CList *CList_Constructor(CList *list); // 0x00425424
void CList_Destructor(CList *list); // 0x0042544F
void CList_Clear(CList *list); // 0x0042546D
void CList_Append(CList *list, uint32_t typeTag, uintptr_t value); // 0x004254E5
void CList_Prepend(CList *list, uint32_t typeTag, uintptr_t value); // 0x004255AF
void CList_InsertAt(CList *list, uint32_t typeTag, uintptr_t value, int index); // 0x00425677
int CList_GetCount(CList *list); // 0x0042578C
int CList_Find(CList *list, uint32_t type, uintptr_t value); // 0x004257A5
void CList_RemoveAt(CList *list, int index); // 0x00425901
void CList_RemoveSpecific(CList *list, uint32_t typeTag, uintptr_t value); // 0x004259D1
CListNode *CListIterator_Remove(CListIterator *iter); // 0x00425AE8
void CListIterator_InsertBefore(CListIterator *iter, CListNode *newNode); // 0x00425B98

#endif /* LIST_H_ */
