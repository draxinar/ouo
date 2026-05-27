#ifndef WOMBAT_COMPILE_H_
#define WOMBAT_COMPILE_H_

#include <stdarg.h>
#include <stdint.h>

__extension__ typedef struct BuiltinHandlerEntry BuiltinHandlerEntry;
__extension__ typedef struct CExecThread CExecThread;
__extension__ typedef struct CFuncScope CFuncScope;
__extension__ typedef struct CNamedScopeEntry CNamedScopeEntry;
__extension__ typedef struct CFunction CFunction;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CScript CScript;
__extension__ typedef struct CScriptCompiler CScriptCompiler;
__extension__ typedef struct CScriptManager CScriptManager;
__extension__ typedef struct CString CString;
__extension__ typedef struct CTrigger CTrigger;
__extension__ typedef struct NodePool NodePool;
__extension__ typedef struct EventParamBlock EventParamBlock;
__extension__ typedef struct ResultNode ResultNode;
__extension__ typedef struct ScriptAttachNode ScriptAttachNode;
__extension__ typedef struct ThreadList ThreadList;
uint16_t CEntity_GetBodyType(CItem *item); // 0x00406A30
void Script_FireTransferEvent(CItem *entity, CItem *target); // 0x00406A50
void Script_FireCheckTransferEvent(CItem *entity, CItem *target); // 0x00406A6C
void StoreSemiResult(ResultNode **chain); // 0x00406AA0
void StoreTriggerVarResult(ResultNode **chain, uintptr_t value); // 0x00406AFC
void StoreLVarRef(ResultNode **chain, uintptr_t value); // 0x00406B57
void StoreLocalVarResult(ResultNode **chain, uintptr_t value); // 0x00406BB2
void StoreTVarRef(ResultNode **chain, uintptr_t value); // 0x00406C0D
void StoreIntLiteral(ResultNode **chain, uintptr_t value); // 0x00406C68
void StoreIdResult(ResultNode **chain, const char *tokenBuf); // 0x00406CC3
void StoreMemberResult(ResultNode **chain, const char *tokenBuf); // 0x00406E05
int StoreHandlerResult(ResultNode **chain, const BuiltinHandlerEntry *handler, ResultNode *argChain); // 0x00406F90
void StoreFuncCallResult(ResultNode **chain, int funcIndex, ResultNode *argChain); // 0x004077AB
void StoreGotoResult(ResultNode **chain, const char *label); // 0x00407805
int ResolveGotoLabels(ResultNode *head); // 0x0040788A
void ScriptCreation_BeginDefer(void); // 0x00425CC0
void ScriptCreation_FlushDeferred(void); // 0x00425CE3
void ScriptCreation_RecordDeferred(CItem *entity, uint32_t scriptInstance); // 0x00425E4B
void TriggerEdit_SetStringProp(CItem *ent, const char *data); // 0x00425ECE
void ObjVar_SetStr(CItem *entity, CString *name, int type, uintptr_t value); // 0x00425EDF
const char *Entity_AttachScript(CItem *entity, const char *scriptName, int fireCreation); // 0x00425F34
int ValidateInWorld(CItem *item); // 0x00425FD2
void CScriptManager_Init(CScriptManager *this); // 0x00426097
void CScriptManager_AddScript(CScriptManager *mgr, CScript *script); // 0x004260E3
CScript *CScriptManager_FindOrLoad(CScriptManager *mgr, const char *name); // 0x00426106
CScript *CScriptManager_LoadScript(CScriptManager *mgr, const char *name); // 0x004261BB
ScriptAttachNode *CScriptManager_CreateInstance(CScriptManager *mgr, CScript *scriptClass); // 0x00426344
const char *CScriptManager_InternString(CScriptManager *mgr, const char *s); // 0x0042635D
void CScriptCompiler_Constructor(CScriptCompiler *compiler); // 0x00426E20
void CScriptCompiler_Destructor(CScriptCompiler *compiler); // 0x00426E7D
CScript *ParseScriptOuter(const char *bytecode, const char *name); // 0x00426EC1
CScript *ParseScriptInner(const char *bytecode, const char *name); // 0x00426ED6
const char *ParseFunctionParams(const char *stream, char *sigBuf, char *paramNamesBuf); // 0x00428149
const char *ParseParamSignature(const char *stream, char *sigBuf); // 0x0042829B
const char *ScriptTokenizer_ReadToken(const char *stream, char *outBuf); // 0x004283E4
int SigCharToTypeId(char c); // 0x004290EF
int GetTypeId(const char *tokenBuf); // 0x0042912B
void StoreTypesFromSig(CFunction *funcEntry, ResultNode *chain); // 0x0042929A
char *BuildSignature(char *sigBuf, ResultNode *chain); // 0x004292B2
int StoreHandlerArgNodes(const BuiltinHandlerEntry *handler, ResultNode *nodes); // 0x00429328
const char *ParseStatementBlock(const char *stream, CFuncScope *scope, int retTypeId); // 0x00429340
const char *EvaluateExpression(CFuncScope *scope, const char *stream, ResultNode **chain, int endToken); // 0x00429F46
const char *ProcessAssignment(CFuncScope *scope, const char *stream, char *varTokenBuf); // 0x0042A847
CNamedScopeEntry *ResolveVarValue(CFuncScope *scope, const char *tokenBuf); // 0x0042B11B
int ScriptTokenizer_MatchToken(const char *stream, int tokenType); // 0x0042B1B0
int GetInlineInt(const char *tokenBuf); // 0x0042B260
ResultNode *AllocResultNode(void); // 0x0042B317
void FreeResultNode(ResultNode *node); // 0x0042B326
NodePool *NodePool_Init(NodePool *pool, int batchSize); // 0x0042B340
ResultNode *NodePool_Pop(NodePool *pool); // 0x0042B370
void NodePool_Push(NodePool *pool, ResultNode *node); // 0x0042B490
NodePool *NodePool_Constructor(NodePool *pool); // 0x0042B4C0
void NodePool_Destructor(NodePool *pool); // 0x0042B4E1
CExecThread *ThreadList_GetCurrent(ThreadList *list); // 0x0042B4EC
void ThreadList_Unlink(ThreadList *list, CExecThread *thread); // 0x0042B4FC
void ThreadList_Push(ThreadList *list, CExecThread *thread); // 0x0042B58F
void ThreadList_MoveToHead(ThreadList *list, CExecThread *thread); // 0x0042B5DB
void ThreadList_Remove(ThreadList *list, CExecThread *thread); // 0x0042B600
void ThreadList_PopHead(ThreadList *list); // 0x0042B619
void ThreadList_FinishCurrent(ThreadList *list, int arg); // 0x0042B63A
void ThreadList_StopByScriptField(ThreadList *list, uintptr_t value); // 0x0042B666
void ThreadList_StopBySerial(ThreadList *list, uintptr_t value); // 0x0042B6BB
void ThreadList_DetachFromEntity(ThreadList *list, CItem *entity, CString *scriptName); // 0x0042B704
void ThreadList_FinishThread(ThreadList *list, CExecThread *thread, int unused); // 0x0042B779
CItem *GetCurrentThreadEntity(void); // 0x0042B822
int BroadcastEventToNearby(CLocation *loc, int range, int eventType, ...); // 0x0042B850
int CWombatManager_FireTrigger_va(CItem *entity, int eventType, va_list ap); // 0x0042B951
int ExecuteTrigger(ScriptAttachNode *scriptNode, CTrigger *trigger, EventParamBlock *paramBlock); // 0x0042D8FA
void EventParamBlock_Constructor(EventParamBlock *pb); // 0x0042DD90
void EventParamBlock_Destructor(EventParamBlock *pb); // 0x0042DE10
void EventParamBlock_AddParam(EventParamBlock *pb, int type, uintptr_t value); // 0x0042DF40
ResultNode *AppendResultNode(ResultNode **chain);

void WombatCompile_DestroyPools(void); // CUSTOM

#endif /* WOMBAT_COMPILE_H_ */
