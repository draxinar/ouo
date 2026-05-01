#ifndef WOMBAT_ESCRIPT_H_
#define WOMBAT_ESCRIPT_H_

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CEScript CEScript;
__extension__ typedef struct CEScriptVar CEScriptVar;
__extension__ typedef struct CEScriptStringVar CEScriptStringVar;
__extension__ typedef struct CEScriptIntVar CEScriptIntVar;
char *CEScript_ReadLine(CEScript *ctx); // 0x0045B8D0
CEScriptVar *CEScriptVar_Constructor(CEScriptVar *self); // 0x0045B977
void CEScriptVar_Destructor(CEScriptVar *self); // 0x0045B995
char *CEScriptVar_GetName(CEScriptVar *self); // 0x0045B9A9
int CEScriptVar_GetType(CEScriptVar *self); // 0x0045B9BA
void StaticInit_CEScript(void); // 0x0045B9CC
void EScript_Constructor(void); // 0x0045B9D6
void CEScript_Init(CEScript *ctx); // 0x0045B9E5
void CEScript_Cleanup(CEScript *ctx); // 0x0045BA4D
char *CEScript_ParseToken(CEScript *ctx, char *line, char *tokenBuf, char *delimOut); // 0x0045BAF6
int CEScript_FindOrCreateVar(CEScript *ctx, const char *name, char type); // 0x0045BBBD
void CEScriptStringVar_SetFromString(CEScriptStringVar *self, const char *str); // 0x0045BD3B
CEScriptStringVar *CEScriptStringVar_Clone(CEScriptStringVar *self); // 0x0045BDD9
CEScriptIntVar *CEScriptIntVar_Clone(CEScriptIntVar *self); // 0x0045BE94
int CEScriptStringVar_GetValue(CEScriptStringVar *self); // 0x0045BF17
void CEScriptIntVar_SetFromInt(CEScriptIntVar *self, int val); // 0x0045BF42
void CEScriptIntVar_SetFromString(CEScriptIntVar *self, const char *str); // 0x0045BF50
int CEScriptIntVar_GetValue(CEScriptIntVar *self); // 0x0045BF58
int CEScript_EvalExpression(CEScript *ctx, char delim, const char *expr); // 0x0045BF69
void CEScript_CmdAssign(CEScript *ctx, char *line); // 0x0045C440
void CEScript_CmdNewObj(CEScript *ctx, char *line); // 0x0045C588
void CEScript_CmdRandJump(CEScript *ctx, char *line); // 0x0045CA1D
int CEScript_EvalCondition(CEScript *ctx, char *line, int negate); // 0x0045CBA8
void CEScript_CmdWhile(CEScript *ctx, char *line, int negate); // 0x0045CC1A
void CEScript_CmdEndLoop(CEScript *ctx, char *line); // 0x0045CD96
void CEScript_CmdIf(CEScript *ctx, char *line, int negate); // 0x0045CE13
char *CEScript_ParseBracketArgs(CEScript *ctx, char *line, char *outBuf, char openDelim, char closeDelim); // 0x0045CF4D
void CEScript_CmdMessage(CEScript *ctx, const char *text); // 0x0045CFD7
void CEScript_ErrorLog(CEScript *ctx, const char *msg); // 0x0045D050
void CEScript_CmdEScript(CEScript *ctx, char *line); // 0x0045D0E6
void CEScript_CmdNoUpdate(CEScript *ctx, const char *text, int mode); // 0x0045D28B
void CEScript_CmdGetTerr(CEScript *ctx, char *line); // 0x0045D2A4
void CEScript_CmdGetAlt(CEScript *ctx, char *line); // 0x0045D43B
void CEScript_CmdSetTerr(CEScript *ctx, char *line); // 0x0045D5D1
void CEScript_CmdSetAlt(CEScript *ctx, char *line); // 0x0045D700
void CEScript_CmdPrintVars(CEScript *ctx, char *line); // 0x0045D82F
void CEScript_CmdQLoad(CEScript *ctx, char *line); // 0x0045D93C
void CEScript_Run(CEScript *ctx, const char *path, CItem *player, const char *args); // 0x0045D9C3
void CEScriptStringVar_SetFromInt(CEScriptStringVar *self, int val); // 0x0045E140
CEScriptStringVar *CEScriptStringVar_Constructor(CEScriptStringVar *self, const char *name); // 0x0045E180
CEScriptStringVar *CEScriptStringVar_ScalarDtor(CEScriptStringVar *self, int freeObj); // 0x0045E1C0
void CEScriptStringVar_Destructor(CEScriptStringVar *self); // 0x0045E1F0
CEScriptIntVar *CEScriptIntVar_Constructor(CEScriptIntVar *self, const char *name); // 0x0045E210
CEScriptIntVar *CEScriptIntVar_ScalarDtor(CEScriptIntVar *self, int freeObj); // 0x0045E250
void CEScriptIntVar_Destructor(CEScriptIntVar *self); // 0x0045E280

#endif /* WOMBAT_ESCRIPT_H_ */
