#ifndef NODEPOOL_H_
#define NODEPOOL_H_

__extension__ typedef struct ScriptAttachNode ScriptAttachNode;
__extension__ typedef struct TagNode TagNode;
// Pool allocator for TagNode (binary: pool at 0x0063E108).
struct TagNode;

// Pool allocator for ScriptAttachNode (binary: pool at 0x0063D8B0).
struct ScriptAttachNode;

__extension__ typedef struct CScript CScript;

void TagNodePool_FlushB(void); // 0x004244C4
void CScriptInstance_ReturnToPool(ScriptAttachNode *node); // 0x00424538
ScriptAttachNode *ScriptNodePool_AllocAndConstruct(CScript *scriptClass); // 0x00424561
void TagNodePool_InitA(void); // 0x0042685A
TagNode *TagNodePool_AllocA(void); // 0x0042686E
void TagNodePool_FlushA(void); // 0x004268E6
void TagNode_ReturnToPool(TagNode *node); // 0x0042695A
void tagnode_destroy_value(TagNode *node); // 0x004269C0

#endif /* NODEPOOL_H_ */
