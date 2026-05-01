/*
 * CList - doubly-linked list primitive.
 *
 * The binary's general-purpose linked list with head, tail, and count;
 * used by CResManager buckets, region lookups, and various lightweight
 * entity indexes.
 */

#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

#include "wombat.h"
#include "wombat_compile.h"

/*
 * 0x0040CCB0 - CList::~CList (scalar deleting destructor)
 *
 * Runs CList_Destructor and frees the list when flags & 1.
 */
CList *
CList_ScalarDelete(CList *list, int flags)
{
	CList_Destructor(list);
	if (flags & 1)
		free(list);
	return NULL;
}

/*
 * 0x00424FE0 - CListNode::CListNode (base constructor)
 *
 * Identity base-class constructor used by CList_RemoveSpecific.
 */
CListNode *
CListNode_BaseConstructor(CListNode *this)
{
	return this;
}

/*
 * 0x00424FEE - CListNode::CListNode
 *
 * Type-aware constructor: deep-copies CString/CUString/CLocation/CList
 * payloads, or stores raw value for int/obj nodes.
 */
void
CListNode_Constructor(CListNode *node, uint32_t typeTag, uintptr_t value)
{
	CString *cs;
	CUString *cus;
	CLocation *loc;
	CList *lst;
	CListNode *cur;

	node->typeTag = typeTag;

	switch (typeTag) {
	case 1: // STRING
		if (value != 0) {
			cs = (CString *)malloc(sizeof(CString));
			if (cs != NULL)
				CString_CopyConstructor(cs, (CString *)value);
		} else {
			cs = (CString *)malloc(sizeof(CString));
			if (cs != NULL)
				CString_Constructor(cs, "");
		}
		node->value = (uintptr_t)cs;
		break;
	case 2: // USTRING
		if (value != 0) {
			cus = (CUString *)malloc(sizeof(CUString));
			if (cus != NULL)
				CUString_CopyConstructor(cus, (CUString *)value);
		} else {
			cus = (CUString *)malloc(sizeof(CUString));
			if (cus != NULL)
				CUString_Constructor(cus, "");
		}
		node->value = (uintptr_t)cus;
		break;
	case 3: // LOC
		loc = (CLocation *)malloc(6);
		if (loc != NULL)
			CLocation_Init(loc);
		node->value = (uintptr_t)loc;
		CLocation_CopyFrom(loc, (CLocation *)value);
		break;
	case 5: // LIST
		lst = (CList *)malloc(sizeof(CList));
		if (lst != NULL)
			CList_Constructor(lst);
		// Walk source list and deep-copy each node via Append
		if (value != (uintptr_t)node) {
			cur = ((CList *)value)->head;
			while (cur != NULL) {
				CList_Append(lst, cur->typeTag, cur->value);
				cur = cur->next;
			}
		}
		node->value = (uintptr_t)lst;
		break;
	default: // INT (0), OBJ (4)
		node->value = value;
		break;
	}
}

/*
 * 0x00425280 - CListNode::~CListNode
 *
 * Type-aware destructor: frees the payload allocated by CListNode_Constructor.
 */
void
CListNode_Destructor(CListNode *node)
{
	switch (node->typeTag) {
	case 1: // STRING
		if (node->value != 0)
			CString_ScalarDelete((CString *)node->value, 1);
		break;
	case 2: // USTRING
		if (node->value != 0)
			CUString_ScalarDelete((CUString *)node->value, 1);
		break;
	case 3: // LOC
		free((void *)node->value);
		break;
	case 5: // LIST
		if (node->value != 0)
			CList_ScalarDelete((CList *)node->value, 1);
		break;
	default: // INT (0), OBJ (4)
		break;
	}
}

/*
 * 0x00425363 - CListNode::Match
 *
 * Type-aware comparator. Returns 0 on mismatched types or on the
 * LIST/THREAD tags (5,6,7) after unwinding the active thread list.
 */
int
CListNode_Match(CListNode *a, CListNode *b)
{
	if (a->typeTag != b->typeTag)
		return 0;

	switch (a->typeTag) {
	case 1:
		return CString_EqualCString2((CString *)a->value, (CString *)b->value);
	case 2:
		return CUString_EqualCUString((CUString *)a->value, (CUString *)b->value);
	case 3:
		return CLocation_IsEqualXYZ((CLocation *)a->value, (CLocation *)b->value);
	case 5:
	case 6:
	case 7:
		ThreadList_FinishCurrent(&g_activeThreadList, 0);
		return 0;
	default:
		return a->value == b->value;
	}
}

/*
 * 0x00425424 - CList::CList
 *
 * Zeroes head, tail, and count.
 */
CList *
CList_Constructor(CList *list)
{
	list->head = NULL;
	list->tail = NULL;
	list->count = 0;
	return list;
}

/*
 * 0x0042544F - CList::~CList
 *
 * Forwards to CList_Clear.
 */
void
CList_Destructor(CList *list)
{
	CList_Clear(list);
}

/*
 * 0x0042546D - CList::Clear
 *
 * Destroys every node and resets head/tail/count.
 */
void
CList_Clear(CList *list)
{
	CListNode *cur;

	while (list->head != NULL) {
		cur = list->head;
		list->head = cur->next;
		CListNode_Destructor(cur);
		free(cur);
	}
	list->tail = NULL;
	list->head = NULL;
	list->count = 0;
}

/*
 * 0x004254E5 - CList::Append
 *
 * Allocates a new node via CListNode_Constructor and links it at the tail.
 */
void
CList_Append(CList *list, uint32_t typeTag, uintptr_t value)
{
	CListNode *node;

	node = (CListNode *)malloc(sizeof(CListNode));
	if (node == NULL)
		return;

	CListNode_Constructor(node, typeTag, value);

	node->prev = list->tail;
	node->next = NULL;
	if (list->tail != NULL)
		list->tail->next = node;
	else
		list->head = node;
	list->tail = node;
	list->count++;
}

/*
 * 0x004255AF - CList::Prepend
 *
 * Allocates a new node via CListNode_Constructor and links it at the head.
 */
void
CList_Prepend(CList *list, uint32_t typeTag, uintptr_t value)
{
	CListNode *node;

	node = (CListNode *)malloc(sizeof(CListNode));
	if (node == NULL)
		return;

	CListNode_Constructor(node, typeTag, value);

	node->next = list->head;
	node->prev = NULL;
	if (list->head != NULL)
		list->head->prev = node;
	else
		list->tail = node;
	list->head = node;
	list->count++;
}

/*
 * 0x00425677 - CList::InsertAt
 *
 * Splices a new node before the node at index, or appends when index is
 * at or past the end.
 */
void
CList_InsertAt(CList *list, uint32_t typeTag, uintptr_t value, int index)
{
	CListNode *node, *cur;
	int i;

	if (index >= list->count) {
		CList_Append(list, typeTag, value);
		return;
	}

	cur = list->head;
	for (i = 0; i < index; i++)
		cur = cur->next;

	node = (CListNode *)malloc(sizeof(CListNode));
	if (node == NULL)
		return;

	CListNode_Constructor(node, typeTag, value);

	node->next = cur;
	node->prev = cur->prev;
	cur->prev = node;
	if (node->prev != NULL)
		node->prev->next = node;
	else
		list->head = node;
	list->count++;
}

/*
 * 0x0042578C - CList::GetCount
 *
 * Returns the list's cached count.
 */
int
CList_GetCount(CList *list)
{
	return list->count;
}

/*
 * 0x004257A5 - CList::Find
 *
 * Returns 1 when any node matches (type, value). STRING/USTRING compare
 * the payload data with case-insensitive string compare; LOC uses
 * CLocation_IsEqualXYZ; other types compare by raw value.
 */
int
CList_Find(CList *list, uint32_t type, uintptr_t value)
{
	CListNode *cur;
	char *searchStr;

	if (type == 1) {
		searchStr = CString_GetData((void *)value);
		for (cur = list->head; cur != NULL; cur = cur->next) {
			if (cur->typeTag == 1) {
				if (strcasecmp(CString_GetData((void *)cur->value), searchStr) == 0)
					return 1;
			}
		}
		return 0;
	}

	if (type == 2) {
		searchStr = CUString_GetData((void *)value);
		for (cur = list->head; cur != NULL; cur = cur->next) {
			if (cur->typeTag == 2) {
				if (ucscmp((uint16_t *)CUString_GetData((void *)cur->value), (uint16_t *)searchStr) == 0)
					return 1;
			}
		}
		return 0;
	}

	if (type == 3) {
		for (cur = list->head; cur != NULL; cur = cur->next) {
			if (cur->typeTag == 3) {
				if (CLocation_IsEqualXYZ((CLocation *)cur->value, (CLocation *)value))
					return 1;
			}
		}
		return 0;
	}

	for (cur = list->head; cur != NULL; cur = cur->next) {
		if (cur->typeTag == type && cur->value == value)
			return 1;
	}
	return 0;
}

/*
 * 0x00425901 - CList::RemoveAt
 *
 * Unlinks and destroys the node at index.
 */
void
CList_RemoveAt(CList *list, int index)
{
	CListNode *node;
	int i;

	if (index >= list->count)
		return;

	node = list->head;
	for (i = 0; i < index; i++)
		node = node->next;

	if (node->prev != NULL)
		node->prev->next = node->next;
	else
		list->head = node->next;

	if (node->next != NULL)
		node->next->prev = node->prev;
	else
		list->tail = node->prev;

	CListNode_Destructor(node);
	free(node);
	list->count--;
}

/*
 * 0x004259D1 - CList::RemoveSpecific
 *
 * Removes the first node matching (typeTag, value) using CListNode_Match
 * against a stack-built probe node.
 */
void
CList_RemoveSpecific(CList *list, uint32_t typeTag, uintptr_t value)
{
	CListNode temp;
	CListNode *cur;

	temp.typeTag = typeTag;
	temp.value = value;

	for (cur = list->head; cur != NULL; cur = cur->next) {
		if (CListNode_Match(&temp, cur)) {
			if (cur->prev != NULL)
				cur->prev->next = cur->next;
			else
				list->head = cur->next;

			if (cur->next != NULL)
				cur->next->prev = cur->prev;
			else
				list->tail = cur->prev;

			CListNode_Destructor(cur);
			free(cur);
			list->count--;
			break;
		}
	}

	temp.typeTag = 0;
	CListNode_Destructor(&temp);
}

/*
 * 0x00425AE8 - CListIterator::Remove
 *
 * Unlinks the iterator's current node, advances past it, and returns the
 * detached node for the caller to free.
 */
CListNode *
CListIterator_Remove(CListIterator *iter)
{
	CListNode *node = iter->current;

	if (node->prev == NULL)
		iter->list->head = node->next;
	else
		node->prev->next = node->next;

	if (node->next == NULL)
		iter->list->tail = node->prev;
	else
		node->next->prev = node->prev;

	iter->list->count--;
	iter->current = node->next;
	return node;
}

/*
 * 0x00425B98 - CListIterator::InsertBefore
 *
 * Splices a pre-allocated node in front of the iterator's current node
 * (or appends to the tail when the iterator is past the end) and points
 * the iterator at it.
 */
void
CListIterator_InsertBefore(CListIterator *iter, CListNode *newNode)
{
	iter->list->count++;
	newNode->next = iter->current;

	if (iter->current == NULL) {
		newNode->prev = iter->list->tail;
		iter->list->tail->next = newNode;
		iter->list->tail = newNode;
		if (iter->list->head == NULL)
			iter->list->head = newNode;
		iter->current = newNode;
	} else {
		if (iter->current->prev == NULL) {
			iter->list->head = newNode;
			newNode->prev = NULL;
		} else {
			newNode->prev = iter->current->prev;
			iter->current->prev->next = newNode;
		}
		iter->current->prev = newNode;
		iter->current = newNode;
	}
}

/*
 * 0x00425C90 - CListNode scalar deleting destructor
 *
 * Runs CListNode_Destructor and frees the node when flags & 1.
 */
CListNode *
CListNode_ScalarDtor(CListNode *node, int flags)
{
	CListNode_Destructor(node);
	if (flags & 1)
		free(node);
	return NULL;
}

/*
 * Tag type name strings from binary table at 0x005EFD48.
 * Types: 0=int, 1=str, 2=ust, 3=loc, 4=obj, 5=lis.
 */
const char *g_tagTypeNames[] = { "int", "str", "ust", "loc", "obj", "lis", "voi" };
