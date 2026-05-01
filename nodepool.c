/*
 * Node pools - slab allocators for TagNode, ScriptAttachNode, and friends.
 *
 * Each pool preallocates a slab of fixed-size records and threads freed
 * entries through their next pointer. Used by taglist.c and the Wombat
 * runtime to keep per-entity attachment updates off the CRT heap.
 */

#include <stdint.h>
#include <stdlib.h>

#include "dat.h"

#include "nodepool.h"
#include "stdptrlist.h"
#include "taglist.h"
#include "vg_pool.h"
#include "wombat.h"

__extension__ typedef struct TagNodePool TagNodePool;
__extension__ typedef struct ScriptNodePool ScriptNodePool;

static void ScriptNodePool_Init(void); // 0x0042446A
static void PendingReturnB_Init(void); // 0x0042448D
static void StdTreeNode_Constructor(ScriptAttachNode *this); // 0x004245A8
static ScriptAttachNode *StdTree_NodePool_Grow(ScriptNodePool *this); // 0x00424BE0
static void ScriptNodePool_Free(ScriptAttachNode *node); // 0x00424D00
static void PendingFreeA_Init(void); // 0x004268AF
static void TagNode_PoolInit(TagNode *node); // 0x00426983
static void TagNode_Destructor(TagNode *node); // 0x004269B5
static TagNode *NodePool_Alloc(TagNodePool *pool); // 0x00426AB0
static void NodePool_Free(TagNodePool *pool, TagNode *node); // 0x00426BD0

/*
 * Pool allocator state at g_tagNodePoolA (0x0063E108). Free TagNodes are
 * threaded through their next pointer.
 */
__extension__ typedef struct TagNodePool TagNodePool;
struct TagNodePool {
	TagNode *freeHead;  // +0x00
	uint32_t blockSize; // +0x04
	uint32_t allocated; // +0x08
};

/*
 * Pool allocator state at g_scriptNodePool (0x0063D8B0). Same shape as
 * TagNodePool but threads ScriptAttachNodes through their next pointer.
 */
struct ScriptNodePool {
	ScriptAttachNode *freeHead; // +0x00
	uint32_t blockSize;         // +0x04
	uint32_t allocated;         // +0x08
};

// 0x0063D8B0
static ScriptNodePool g_scriptNodePool;

// 0x0063E108
static TagNodePool g_tagNodePoolA;

// 0x0063E118 - Binary: std::list<void*> for deferred TagNode free.
static StdPtrList g_pendingFreeA;

// 0x0063D8C0 - Binary: std::list<void*> for deferred ScriptAttachNode return.
static StdPtrList g_pendingReturnB;

/*
 * 0x0042446A - ScriptNodePool::Init
 *
 * Initializes the global ScriptAttachNode pool (4096 nodes per block).
 * Binary calls NodePool_Init(&g_scriptNodePool, 0x1000); inlined here
 * to avoid a cross-module cast between the near-identical pool structs.
 */
static void
ScriptNodePool_Init(void)
{
	g_scriptNodePool.freeHead = NULL;
	g_scriptNodePool.blockSize = 0x1000;
	g_scriptNodePool.allocated = 0;
}

/*
 * 0x0042448D - PendingReturnB::Init
 *
 * Constructs the deferred-return list for ScriptAttachNodes.
 */
static void
PendingReturnB_Init(void)
{
	char typeByte;
	StdPtrList_ConstructorWithType(&g_pendingReturnB, &typeByte);
}

/*
 * 0x004244C4 - TagNodePool::FlushB
 *
 * Drains g_pendingReturnB by freeing each queued ScriptAttachNode.
 */
void
TagNodePool_FlushB(void)
{
	StdPtrNode *it, *endIt, *eraseResult, *tmp;

	StdPtrList_Begin(&g_pendingReturnB, &it);
	while (1) {
		StdPtrList_End(&g_pendingReturnB, &endIt);
		if (!StdPtrIter_Neq(&it, &endIt))
			break;
		ScriptAttachNode *node = (ScriptAttachNode *)*StdPtrIter_Deref(&it);
		ScriptNodePool_Free(node);
		StdPtrList_Erase(&g_pendingReturnB, &eraseResult, it);
		it = *StdPtrList_Begin(&g_pendingReturnB, &tmp);
	}
}

/*
 * 0x00424538 - CScriptInstance::ReturnToPool
 *
 * Clears the script instance and queues it on g_pendingReturnB for
 * later release by TagNodePool_FlushB.
 */
void
CScriptInstance_ReturnToPool(ScriptAttachNode *node)
{
	CScriptInstance_Clear(node);
	StdPtrList_ScalarDelete_4DD0(&g_pendingReturnB, node);
}

/*
 * 0x00424561 - ScriptNodePool::AllocAndConstruct
 *
 * Allocates a ScriptAttachNode and constructs it for scriptClass.
 */
ScriptAttachNode *
ScriptNodePool_AllocAndConstruct(CScript *scriptClass)
{
	ScriptAttachNode *node;

	node = StdTree_NodePool_Grow(&g_scriptNodePool);
	CScriptInstance_Constructor(node, scriptClass);
	return node;
}

/*
 * 0x004245A8 - StdTreeNode::StdTreeNode
 *
 * No-op default constructor for ScriptAttachNode pool elements. The MSVC
 * STL allocator template instantiates this; ScriptAttachNode is POD so
 * it does nothing visible.
 */
static void
StdTreeNode_Constructor(ScriptAttachNode *this)
{
	USED(this);
}

/*
 * 0x00424BE0 - StdTree::NodePool_Grow
 *
 * Pool allocator for ScriptAttachNode. Pops a node from the free list,
 * or allocates a new block of blockSize nodes, default-constructs each
 * with StdTreeNode_Constructor (no-op), threads all but the first onto
 * the free list via the `next` field, and returns the first.
 */
static ScriptAttachNode *
StdTree_NodePool_Grow(ScriptNodePool *this)
{
	ScriptAttachNode *node;
	ScriptAttachNode *firstNode;
	char *block;
	ScriptAttachNode *arr;
	uint32_t count;
	int32_t i;

	if (this->freeHead != NULL) {
		node = this->freeHead;
		VG_POOL_ALLOC(this, node, sizeof(ScriptAttachNode));
		VG_MAKE_DEFINED(&node->next, sizeof(node->next));
		this->freeHead = node->next;
	} else {
		this->allocated = 1;
		count = this->blockSize;
		// Custom: 64-bit - sizeof(uintptr_t) header for alignment
		block = (char *)malloc(count * sizeof(ScriptAttachNode) + sizeof(uintptr_t));
		if (block != NULL) {
			*(uint32_t *)block = count;
			arr = (ScriptAttachNode *)(block + sizeof(uintptr_t));
			for (i = 0; (uint32_t)i < count; i++)
				StdTreeNode_Constructor(&arr[i]);
			firstNode = arr;
		} else {
			firstNode = NULL;
		}

		node = firstNode;
		this->allocated = 0;

		arr = firstNode;
		for (i = (int32_t)count - 1; i >= 1; i--) {
			arr[i].next = this->freeHead;
			this->freeHead = &arr[i];
		}

		VG_POOL_ALLOC(this, node, sizeof(ScriptAttachNode));
	}

	StdTreeNode_Constructor(node);
	return node;
}

/*
 * 0x00424D00 - ScriptNodePool::Free
 *
 * Unlinks the node from the global instance list and pushes it onto
 * the pool's free list.
 */
static void
ScriptNodePool_Free(ScriptAttachNode *node)
{
	ScriptAttachNode_UnlinkFromGlobal(node);
	node->next = g_scriptNodePool.freeHead;
	g_scriptNodePool.freeHead = node;
	VG_POOL_FREE(&g_scriptNodePool, node);
}

/*
 * 0x0042685A - TagNodePool::InitA
 *
 * Initializes the global TagNode pool (4096 nodes per block).
 *
 * MODIFIED: also runs the MSVC static init thunks ScriptNodePool_Init,
 * PendingReturnB_Init, and PendingFreeA_Init, since our Linux build
 * has no CRT static init.
 */
void
TagNodePool_InitA(void)
{
	g_tagNodePoolA.freeHead = NULL;
	g_tagNodePoolA.blockSize = 0x1000;
	g_tagNodePoolA.allocated = 0;
	ScriptNodePool_Init();
	PendingReturnB_Init();
	PendingFreeA_Init();
	VG_CREATE_POOL(&g_tagNodePoolA);
	VG_CREATE_POOL(&g_scriptNodePool);
}

/*
 * 0x0042686E - TagNodePool::AllocA
 *
 * Allocates a TagNode from pool A.
 */
TagNode *
TagNodePool_AllocA(void)
{
	return NodePool_Alloc(&g_tagNodePoolA);
}

/*
 * 0x004268AF - PendingFreeA::Init
 *
 * Constructs the deferred-free list for TagNodes.
 */
static void
PendingFreeA_Init(void)
{
	char typeByte;
	StdPtrList_ConstructorWithType(&g_pendingFreeA, &typeByte);
}

/*
 * 0x004268E6 - TagNodePool::FlushA
 *
 * Drains g_pendingFreeA by freeing each queued TagNode.
 */
void
TagNodePool_FlushA(void)
{
	StdPtrNode *it, *endIt, *eraseResult, *tmp;

	StdPtrList_Begin(&g_pendingFreeA, &it);
	while (1) {
		StdPtrList_End(&g_pendingFreeA, &endIt);
		if (!StdPtrIter_Neq(&it, &endIt))
			break;
		TagNode *node = (TagNode *)*StdPtrIter_Deref(&it);
		NodePool_Free(&g_tagNodePoolA, node);
		StdPtrList_Erase(&g_pendingFreeA, &eraseResult, it);
		it = *StdPtrList_Begin(&g_pendingFreeA, &tmp);
	}
}

/*
 * 0x0042695A - TagNode::ReturnToPool
 *
 * Tears down the stored value and queues the node on g_pendingFreeA.
 * node->name is never freed here - names are interned strings owned
 * by CScriptManager.
 */
void
TagNode_ReturnToPool(TagNode *node)
{
	tagnode_destroy_value(node);
	StdPtrList_PushBack(&g_pendingFreeA, node);
}

/*
 * 0x00426983 - TagNode::Init
 *
 * Zero-initializes a TagNode with type=WTYPE_UNKNOWN.
 */
static void
TagNode_PoolInit(TagNode *node)
{
	node->type = 7;
	node->value = 0;
	node->name = NULL;
	node->next = NULL;
}

/*
 * 0x004269B5 - TagNode::~TagNode
 *
 * No-op destructor.
 */
static void
TagNode_Destructor(TagNode *node)
{
	USED(node);
}

/*
 * 0x004269C0 - TagNode value destructor
 *
 * Releases the heap value owned by a TagNode (CString, CUString,
 * CLocation, or CList) based on its WTYPE. Int and Obj are no-ops.
 * Stamps value with 0xABCD as a debug marker.
 */
void
tagnode_destroy_value(TagNode *node)
{
	switch (node->type) {
	case 1: // WTYPE_STRING
		if ((void *)(uintptr_t)node->value != NULL)
			CString_ScalarDelete((CString *)(uintptr_t)node->value, 1);
		break;
	case 2: // WTYPE_USTRING
		if ((void *)(uintptr_t)node->value != NULL)
			CUString_ScalarDelete((CUString *)(uintptr_t)node->value, 1);
		break;
	case 3: // WTYPE_LOC
		free((void *)(uintptr_t)node->value);
		break;
	case 5: // WTYPE_LIST
		if ((void *)(uintptr_t)node->value != NULL)
			CList_ScalarDelete((CList *)(uintptr_t)node->value, 1);
		break;
	default:
		break;
	}
	node->value = 0xABCD;
}

/*
 * 0x00426AB0 - NodePool::Alloc
 *
 * Pops a TagNode from the free list, or allocates a new block of
 * blockSize nodes, links all but the first onto the free list, and
 * returns the first.
 */
static TagNode *
NodePool_Alloc(TagNodePool *pool)
{
	TagNode *node;
	char *block;
	TagNode *nodes;
	uint32_t i;

	if (pool->freeHead != NULL) {
		node = pool->freeHead;
		VG_POOL_ALLOC(pool, node, sizeof(TagNode));
		VG_MAKE_DEFINED(&node->next, sizeof(node->next));
		pool->freeHead = node->next;
		TagNode_PoolInit(node);
		return node;
	}

	pool->allocated = 1;
	// Custom: 64-bit - sizeof(uintptr_t) header for alignment
	block = (char *)malloc(pool->blockSize * sizeof(TagNode) + sizeof(uintptr_t));
	*(uint32_t *)block = pool->blockSize;
	nodes = (TagNode *)(block + sizeof(uintptr_t));

	for (i = 0; i < pool->blockSize; i++)
		TagNode_PoolInit(&nodes[i]);

	pool->allocated = 0;

	for (i = pool->blockSize - 1; (int32_t)i >= 1; i--)
		nodes[i].next = pool->freeHead, pool->freeHead = &nodes[i];

	TagNode_PoolInit(&nodes[0]);
	VG_POOL_ALLOC(pool, &nodes[0], sizeof(TagNode));
	return &nodes[0];
}

/*
 * 0x00426BD0 - NodePool::Free
 *
 * Destructs the TagNode and pushes it onto the pool's free list.
 */
static void
NodePool_Free(TagNodePool *pool, TagNode *node)
{
	TagNode_Destructor(node);
	node->next = pool->freeHead;
	pool->freeHead = node;
	VG_POOL_FREE(pool, node);
}
