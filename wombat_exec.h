#ifndef WOMBAT_EXEC_H_
#define WOMBAT_EXEC_H_

#include <stdint.h>

#include "config.h"
#include "wombat.h"
#include "scommand.h"
#include "log.h"

__extension__ typedef struct BuiltinHandlerEntry BuiltinHandlerEntry;
__extension__ typedef struct CExecThread CExecThread;
__extension__ typedef struct CFuncScope CFuncScope;
__extension__ typedef struct CFunction CFunction;
__extension__ typedef struct CHintItem CHintItem;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CResManager CResManager;
__extension__ typedef struct CList CList;
__extension__ typedef struct CVector CVector;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CString CString;
__extension__ typedef struct CUString CUString;
__extension__ typedef struct EventLogger EventLogger;
__extension__ typedef struct EventParam EventParam;
__extension__ typedef struct NamedResource NamedResource;
__extension__ typedef struct EventParamBlock EventParamBlock;
__extension__ typedef struct ResultNode ResultNode;
__extension__ typedef struct SCommandEntry SCommandEntry;
__extension__ typedef struct SCommandManager SCommandManager;

/*
 * NPC hint/quest record (0xFC bytes; serialized at 0x100). Constructor
 * at 0x00463A80. All scalar, so the layout is pointer-width agnostic.
 */
struct CHintItem {
	int32_t hintId;     // +0x00
	int32_t serial;     // +0x04
	int32_t hintFlags;  // +0x08
	char name1[64];     // +0x0C
	char name2[128];    // +0x4C
	CLocation location; // +0xCC
	uint16_t _pad_d2;   // +0xD2
	int32_t sourceObj;  // +0xD4
	char name3[32];     // +0xD8
	int32_t flags;      // +0xF8
};

extern NamedResource g_NamedResources[]; // 0x00615390
extern const int16_t g_DirDeltaX[8]; // 0x0061BB28
extern SCommandEntry g_SCommandTable[128]; // 0x0063F968
extern char g_esLineBuf[4096]; // 0x00645B50
extern CEScript g_CEScriptCtx; // 0x00645C50
extern CResManager g_HintManager; // 0x00698E90
extern SCommandManager g_SCommandManager; // 0x00699910
extern const int16_t g_DirDeltaY[8];
extern int g_ConfigSpawnClient;
extern uint16_t g_ConfigStartX;
extern uint16_t g_ConfigStartY;
extern uint16_t g_ConfigStartZ;
extern void *g_CEScriptIntVar_vtable[];
extern void *g_CEScriptStringVar_vtable[];

void Script_trackingTypeSelected(CList *list, uint32_t serial, int trackType, int categoryTypeId, CLocation *filter); // 0x00405F0C
int ResolveResultType(CScript *script, ResultNode *node, int *outFlag); // 0x00409DD2
int CalcResultChainSize(CScript *script, ResultNode *chain); // 0x00409E6F
int TreeEvaluator(CExecThread *thread); // 0x00409F5C
void HandleFuncReturn(CExecThread *thread, int savedUsedBytes, int expectedSize); // 0x0040A2F1
void HandleReturnMismatch(CExecThread *thread); // 0x0040A434
void ExecAbortCurrent(void); // 0x0040A565
void DispatchHandler(CExecThread *thread, const BuiltinHandlerEntry *handler); // 0x0040A6B5
CItem *CExecThread_GetEntity(CExecThread *thread); // 0x0040CBFA
void CLocation_SetLoc(CLocation *dst, const CLocation *src); // 0x0040CCE0
int GetFuncRetType(CFunction *func); // 0x0040CD10
int GetVarType(const BuiltinHandlerEntry *entry); // 0x0040CD30
void CFuncScope_Constructor(CFuncScope *scope); // 0x0040CD72
void CFuncScope_Destructor(CFuncScope *scope); // 0x0040CDE1
int AddVarToScope(CFuncScope *scope, int typeId, const char *name); // 0x0040CE6C
const BuiltinHandlerEntry *LookupHandler(const char *name, const char *typeSig); // 0x0040CF6E
void Handler_FOR(int arg1, int arg2); // 0x0040D1B2
void Handler_ENDFOR_NOP(int arg1); // 0x0040D1C3
void Handler_IF(int condition, char *jumpTarget); // 0x0040D1CE
void Handler_ELSE(char *jumpTarget); // 0x0040D1F7
void Handler_ENDIF(void); // 0x0040D21A
void Handler_ENDIF2(void); // 0x0040D21F
void Handler_SWITCH(void); // 0x0040D224
void Handler_SWITCH2(void); // 0x0040D229
void Handler_CASE(int caseValue, ResultNode *caseChain); // 0x0040D22E
void Handler_WHILE(int condition, char *jumpTarget); // 0x0040D29C
void Handler_ENDWHILE(char *jumpTarget); // 0x0040D2C5
void Handler_ENDFOR(char *jumpTarget); // 0x0040D2F2
void Handler_FOR_BODY(char *jumpTarget); // 0x0040D31F
void Handler_GOTO2(char *jumpTarget); // 0x0040D342
void Handler_RETURN_INT(int value); // 0x0040D365
void Handler_RETURN_INT2(int value); // 0x0040D3B9
void Handler_RETURN_LOC(CLocation *locPtr); // 0x0040D40D
void Handler_RETURN_STR(CString *str); // 0x0040D459
void Handler_RETURN_VOID(void); // 0x0040D509
void Script_split(CList *list, CString *str); // 0x0040D547
int Script_wordWrap(CList *list, CString *str, int wrapWidth); // 0x0040D68E
int Script_textSubstitute(CString *dest, CString *src, CString *find, CString *replace); // 0x0040D7E4
void Script_splitCommaDelimitedString(CList *list, CString *str); // 0x0040D819
void Opr_barkstr(void); // 0x0040D96F
void Opr_barkint(void); // 0x0040D974
int Script_strtoi(CString *str); // 0x0040D979
int Script_strlen(CString *str); // 0x0040D98F
int Script_strContains(CString *haystack, CString *needle); // Custom
void Script_toUpper(CString *str, int start, int end); // 0x0040D99C
void Script_removePrefix(CString *str, CString *prefix); // 0x0040DA08
void Script_append(CList *list, uintptr_t typeTag, uintptr_t value); // 0x0040DA53
void Script_concatList(CList *dst, CList *src); // 0x0040DA68
void Script_prepend(CList *list, uintptr_t typeTag, uintptr_t value); // 0x0040DAA0
void Script_setitem(CList *list, uintptr_t typeTag, uintptr_t value, int index); // 0x0040DAB5
void Script_setLocItem(CList *list, uintptr_t locPtr, int index); // 0x0040DAF5
void Script_insert(CList *list, uintptr_t typeTag, uintptr_t value, int index); // 0x0040DB33
int Script_numinlist(CList *list); // 0x0040DFF9
int Script_isinlist(CList *list, uintptr_t type, uintptr_t value); // 0x0040E006
void Script_removeitem(CList *list, int index); // 0x0040E01B
void Script_truncateList(CList *list, int index); // 0x0040E047
void Script_removespecificitem(CList *list, uintptr_t typeTag, uintptr_t value); // 0x0040E06F
void Script_clearlist(CList *list); // 0x0040E084
int Script_getListItemType(CList *list, int index); // 0x0040E091
void Script_sortList(CList *list, int flags); // 0x0040E1DE
void Opr_assignint(int *dst, int value); // 0x0040E2F4
void Opr_assignobj(int *dst, int serial); // 0x0040E301
void Opr_assignstr(CString *dst, CString *src); // 0x0040E30E
void Opr_assignust(CUString *dst, CUString *src); // 0x0040E31F
void Opr_concat(CString *dst, CString *src); // 0x0040E330
void Opr_assignloc(CLocation *dst, const CLocation *src); // 0x0040E341
void Opr_assignlist(CList *dst, CList *src); // 0x0040E352
int Opr_null(int a); // 0x0040E392
int Opr_plus_int(int a, int b); // 0x0040E39A
int Opr_minus(int a, int b); // 0x0040E3A5
int Opr_mult(int a, int b); // 0x0040E3B0
int Opr_div(int a, int b); // 0x0040E3BC
int Opr_and(int a, int b); // 0x0040E3E0
int Opr_or(int a, int b); // 0x0040E407
int Opr_xor(int a, int b); // 0x0040E42E
int Opr_equiv_int(int a, int b); // 0x0040E439
int Opr_nequiv_int(int a, int b); // 0x0040E44B
int Opr_gt(int a, int b); // 0x0040E45D
int Opr_lt(int a, int b); // 0x0040E46F
int Opr_mod(int a, int b); // 0x0040E481
int Opr_gteq(int a, int b); // 0x0040E48F
int Opr_lteq(int a, int b); // 0x0040E4A1
int Opr_not(int a); // 0x0040E4B3
void Opr_inc(int *ptr); // 0x0040E4C1
void Opr_dec(int *ptr); // 0x0040E4D3
CString *Opr_plus_str_str(CString *out, CString *a, CString *b); // 0x0040E4E5
int Opr_equiv_str(CString *a, CString *b); // 0x0040E515
int Opr_nequiv_str(CString *a, CString *b); // 0x0040E539
int Opr_equiv_loc(const CLocation *loc1, const CLocation *loc2); // 0x0040E558
int Opr_nequiv_loc(const CLocation *loc1, const CLocation *loc2); // 0x0040E595
int Opr_equiv_obj(int a, int b); // 0x0040E5D2
int Opr_nequiv_obj(int a, int b); // 0x0040E5E4
int Opr_listuni(CList *list, int index); // 0x0040E5F6
CList *Opr_listlist(CList *list, int index); // 0x0040E698
int Opr_listint(CList *list, int index); // 0x0040E733
CString *Opr_liststr(CString *dest, CList *list, int index); // 0x0040E760
CUString *Opr_listustr(CUString *dest, CList *list, int index); // 0x0040E7C2
CLocation *Opr_listloc(CLocation *outLoc, CList *list, int index); // 0x0040E863
int Opr_listobj(CList *list, int index); // 0x0040E8C1
CString *Opr_strindex(CString *dest, CString *source, int index); // 0x0040E8EE
void Opr_assignintstr(int *dst, CString *src); // 0x0040E95F
int Opr_objtoint(int serial); // 0x0040E97A
void Opr_assignstrint(CString *dst, int value); // 0x0040E982
void Opr_assignustint(CUString *dst, int value); // 0x0040E9AD
CString *Opr_plus_str_int(CString *out, CString *str, int value); // 0x0040E9BE
CString *Opr_plus_str_loc(CString *out, CString *str, const CLocation *loc); // 0x0040EA04
void Opr_assignlocint(CLocation *dst, int x, int y, int z); // 0x0040EA5E
int Script_isArrayInit(int id); // 0x0040F18E
void Script_initArrayFromFile(int id, int width, int height, CString *filename); // 0x0040F1CC
void Script_initArray(int id, int width, int height, CList *typeList); // 0x0040F680
void Script_setArrayElems(int id, int x, int y, CList *list); // 0x0040F6AB
void Script_deleteArray(int id); // 0x0040F76B
void Script_setArrayIntElem(int id, int x, int y, int val); // 0x0040F77C
void Script_setArrayStrElem(int id, int x, int y, CString *src); // 0x0040F7BB
void Script_setArrayUStrElem(int id, int x, int y, CUString *src); // 0x0040F7FA
int Script_getArrayIntElem(int id, int x, int y); // 0x0040F839
CString *Script_getArrayStrElem(CString *retval, int id, int x, int y); // 0x0040F876
CUString *Script_getArrayUStrElem(CUString *retval, int id, int x, int y); // 0x0040F90E
int Script_getArrayHeight(int id); // 0x0040F9AC
int Script_getArrayWidth(int id); // 0x0040F9E1
int Script_getCompileFlag(int flagId); // 0x0040FA16
void Script_superTargetObj(uint32_t serial, uint32_t cursorId, int cursorType); // 0x0040FB2E
void Script_sendToNearbyPlayers(uint32_t serial, int flags); // 0x0040FCD6
void Script_targetObj(uint32_t serial, uint32_t cursorId); // 0x0040FD7A
void Script_superTargetLoc(uint32_t serial, uint32_t cursorId, int cursorType, int multiId); // 0x0040FD91
void Script_targetLoc(uint32_t serial, uint32_t cursorId); // 0x0040FF39
void Script_targetLocMulti(uint32_t serial, uint32_t cursorId, int multiId, int xOff, int yOff, int facing); // 0x0040FF52
void Script_targetLocObjList(uint32_t serial, uint32_t cursorId, int multiId, int xOff, int yOff, CList *list); // 0x0041010A
void Script_addFrag(uint32_t serial, CString *fragName); // 0x0041030D
void Script_removeFragment(uint32_t serial, CString *fragName); // 0x0041035E
int Script_hasScript(uint32_t serial, CString *scriptName); // 0x004103AF
void Script_attachScript(uint32_t serial, CString *scriptName); // 0x004103EA
void Script_detachScript(uint32_t serial, CString *scriptName); // 0x004104F6
void Script_getcontents(CList *list, uint32_t serial); // 0x0041053D
int Script_canHold(uint32_t containerSerial, uint32_t itemSerial); // 0x004105B1
void Script_getequipment(CList *list, uint32_t serial); // 0x00410601
void Script_callback(uint32_t serial, int delay, int callbackId); // 0x0041068D
int Script_hasCallback(uint32_t serial, int callbackId); // 0x004106D3
int Script_hasCallbackAdvanced(uint32_t serial, int eventType, int callbackId); // 0x00410710
void Script_shortcallback(uint32_t serial, int delay, int callbackId); // 0x0041074F
void Script_callbackAdvanced(uint32_t serial, int delay, int eventType, int callbackId); // 0x00410792
void Script_removeCallback(uint32_t serial, int callbackId); // 0x004107D0
void Script_removeCallbackAdvanced(uint32_t serial, int eventType, int callbackId); // 0x004107FD
void Script_selectType(uint32_t playerSerial, uint32_t serial, uint32_t dialogId, CString *title, CList *list); // 0x00410A31
void Script_selectTypeAndHue(uint32_t playerSerial, uint32_t serial, uint32_t dialogId, CString *title, CList *list); // 0x00410A54
void Script_selectHue(uint32_t playerSerial, uint32_t serial, uint32_t typeID, uint32_t hue); // 0x00410A77
int Script_messageret(uint32_t senderSerial, uint32_t targetSerial, CString *msgName, intptr_t listArgs); // 0x00410B89
void Script_message(uint32_t targetSerial, CString *msgName, intptr_t listArgs); // 0x00410C0B
void Script_multimessage(uint32_t serial, CString *msgName, intptr_t listArgs); // 0x00410C4F
void Script_multiMessageToLoc(CLocation *loc, CString *msgName, intptr_t listArgs); // 0x00410CB5
void Script_multiMessageToRange(CLocation *loc, int range, CString *msgName, intptr_t listArgs); // 0x00410CF9
void Script_messageToRange(CLocation *loc, int range, CString *text, CString *args); // 0x00410D41
void Script_changeLoc(CLocation *loc, int dx, int dy, int dz); // 0x00410FA2
int Script_nullHandler(void); // 0x00410FDB
void Script_copylist(CList *dst, CList *src); // 0x00410FE2
void Script_printList(CList *list); // 0x004110A9
int CalcDirection(const CLocation *loc1, const CLocation *loc2); // 0x004110BC
void Script_setConvoRet(CString *str); // 0x004111D7
void Script_disableBehaviors(uint32_t serial); // 0x004111F2
void Script_enableBehaviors(uint32_t serial); // 0x0041123C
int Script_areBehaviorsEnabled(uint32_t serial); // 0x0041128C
void Script_callGuards(uint32_t serial, CLocation *loc, int range); // 0x004112DD
void Script_seance(uint32_t serial, int mode); // 0x0041150C
int Script_hasObjVar(uint32_t serial, CString *varname); // 0x00411560
int Script_hasObjListVar(uint32_t serial, CString *varname); // 0x00411599
int Script_getObjVar(uint32_t serial, CString *varname); // 0x004115D2
int Script_getObjVar_int(uint32_t serial, CString *varname); // 0x004115EB
CString *Script_getObjVar_str(CString *retbuf, uint32_t serial, CString *varname); // 0x00411642
CLocation *Script_getObjVar_loc(CLocation *retbuf, uint32_t serial, CString *varname); // 0x00411709
uint32_t Script_getObjVar_obj(uint32_t serial, CString *varname); // 0x00411797
void Script_getObjListVar(CList *list, uint32_t serial, CString *varname); // 0x004117EE
void Script_setObjVar(uint32_t serial, CString *varname, int typeTag, uintptr_t value); // 0x00411869
void Script_removeObjVar(uint32_t serial, CString *varname); // 0x004118AF
void Script_copyObjVar(uint32_t destSerial, uint32_t srcSerial, CString *varname); // 0x004118ED
void Script_copyAllObjVars(uint32_t destSerial, uint32_t srcSerial); // 0x0041194E
int Script_addToObjVarListSet(uint32_t serial, CString *varname, int typeTag, uintptr_t value); // 0x00411A2C
int Script_isInObjVarListSet(uint32_t serial, CString *varname, int typeTag, uintptr_t value); // 0x00411A74
void Script_setLastValidTerrainLoc(uint32_t serial, CLocation *loc); // 0x00411ABC
int Script_teleport(uint32_t serial, CLocation *loc); // 0x00411B11
int Script_teleportNoFall(uint32_t serial, CLocation *loc); // 0x00411C42
int Script_random(int min, int max); // 0x00411D0E
int Script_dice(int numDice, int numSides); // 0x00411D25
void Script_deleteObject(uint32_t serial); // 0x00411D3C
void Script_deleteObjectNoFall(uint32_t serial); // 0x00411E09
int Script_putObjContainer(uint32_t thingSerial, uint32_t containerSerial); // 0x00411E64
int Script_toMobile(uint32_t thingSerial, uint32_t mobileSerial); // 0x00411FC4
int Script_putMobContainer(uint32_t thingSerial, uint32_t containerSerial); // 0x00412107
int Script_equipObj(uint32_t itemSerial, uint32_t targetSerial, int layer); // 0x00412189
int Script_dropObj(uint32_t serial, CLocation *loc); // 0x00412291
int Script_walk(uint32_t serial, int dir); // 0x004122A6
int Script_run(uint32_t serial, int dir); // 0x00412311
void Script_setType(uint32_t serial, int typeID); // 0x0041237F
void Script_setHue(uint32_t serial, int hue); // 0x004124F3
void Script_setPartialHue(uint32_t serial, int hue); // 0x0041253B
int Script_getDefaultTextHue(uint32_t serial); // 0x0041259E
void Script_setDefaultTextHue(uint32_t serial, int hue); // 0x004125D1
void Script_animateMobile(uint32_t serial, int animType, int action, int frameCount, int repeat, int backwards); // 0x00412602
void Script_sfx(CLocation *loc, int soundID, int volume); // 0x004127A4
void Script_sfxTo(uint32_t serial, int soundID, int volume); // 0x0041288A
void Script_musicTo(uint32_t serial, int musicID); // 0x00412972
CString *Script_getDirection(CString *dest, CLocation *loc1, CLocation *loc2); // 0x004129A7
int Script_getDistanceInTiles(const CLocation *loc1, const CLocation *loc2); // 0x00412B36
CString *Script_getDistance(CString *dest, CLocation *loc1, CLocation *loc2); // 0x00412B4B
CLocation *Script_interpose(CLocation *outLoc, CLocation *loc1, const CLocation *loc2); // 0x00412CDE
int Script_getFacing(uint32_t serial); // 0x00412D45
int Script_getHue(uint32_t serial); // 0x00412D74
int Script_isFacingPlace(uint32_t serial, CLocation *loc); // 0x00412DA3
int Script_isFacingPerson(uint32_t serial, uint32_t targetSerial); // 0x00412DF2
int Script_facingEachOther(uint32_t serial1, uint32_t serial2); // 0x00412E66
void Script_faceHere(uint32_t serial, int dir); // 0x00412EFF
int Script_getSex(uint32_t serial); // 0x00412F56
int Script_sameSex(uint32_t serial1, uint32_t serial2); // 0x00412FB4
int Script_isHuman(uint32_t serial); // 0x00412FDE
int Script_isHumanBodyType(uint32_t serial); // 0x00413009
int Script_isMobile(uint32_t serial); // 0x00413034
int Script_isPlayer(uint32_t serial); // 0x0041304F
int Script_isSpellbook(uint32_t serial); // 0x0041306A
int Script_isContainer(uint32_t serial); // 0x00413085
int Script_isRealContainer(uint32_t serial); // 0x004130A0
int Script_isMap(uint32_t serial); // 0x004130BB
int Script_isNPC(uint32_t serial); // 0x004130D6
int Script_isShopkeeper(uint32_t serial); // 0x004130F1
int Script_isGuard(uint32_t serial); // 0x0041310C
int Script_isDead(uint32_t serial); // 0x00413127
int Script_isInContainer(uint32_t serial); // 0x00413142
int Script_isEquipped(uint32_t serial); // 0x0041315D
int Script_isMoveable(uint32_t thingSerial, uint32_t playerSerial); // 0x00413178
int Script_isFreelyUsable(uint32_t thingSerial, uint32_t playerSerial); // 0x004131D0
int Script_isFreelyViewable(uint32_t thingSerial, uint32_t playerSerial); // 0x00413228
int Script_isOnline(uint32_t serial); // 0x00413280
int Script_removePlayerFromGame(uint32_t serial); // 0x004132C7
int Script_getEncumbrance(uint32_t serial); // 0x00413316
uint32_t Script_containedBy(uint32_t serial); // 0x00413345
int Script_isNoDrawType(int bodyType); // 0x0041338A
void Script_bark(uint32_t serial, CString *text); // 0x004133AF
void Script_barkUnicode(uint32_t serial, CUString *text); // 0x00413433
void Script_ebark(uint32_t serial, CString *text); // 0x004134B7
void Script_ebarkUnicode(uint32_t serial, CUString *text); // 0x004134F5
void Script_barkTo(uint32_t speakerSerial, uint32_t targetSerial, CString *text); // 0x00413533
void Script_barkToUnicode(uint32_t speakerSerial, uint32_t targetSerial, CUString *text); // 0x00413591
void Script_ebarkTo(uint32_t speakerSerial, uint32_t targetSerial, CString *text); // 0x004135EF
void Script_ebarkToUnicode(uint32_t speakerSerial, uint32_t targetSerial, CUString *text); // 0x0041364D
void Script_barkToHued(uint32_t speakerSerial, uint32_t targetSerial, int hue, CString *text); // 0x004136AB
void Script_barkToHuedUnicode(uint32_t speakerSerial, uint32_t targetSerial, int hue, CUString *text); // 0x0041370E
void Script_actionBark(uint32_t serial, int hue, CString *text1, CString *text2); // 0x00413771
int Script_getObjType(uint32_t serial); // 0x004137B3
int Script_getValue(uint32_t serial); // 0x004137E6
int Script_makeValueless(uint32_t serial); // 0x0041381B
int Script_isInCamp(uint32_t serial); // 0x00413869
CLocation *Script_getLocation(CLocation *retloc, uint32_t serial); // 0x00413884
CLocation *Script_getMasterObjLoc(CLocation *retloc, int index); // 0x004138E9
CLocation *Script_getRelayLoc(CLocation *retloc, uint32_t serial); // 0x0041392E
CLocation *Script_whereIs(CLocation *retloc, uint32_t serial); // 0x0041394F
int Script_numInContainer(uint32_t serial); // 0x004139B4
int Script_isRidable(uint32_t serial); // 0x00413A50
int Script_isRiding(uint32_t serial); // 0x00413A6B
int Script_unRide(uint32_t serial); // 0x00413A86
int Script_getAC(uint32_t serial); // 0x00413AA1
int Script_getMovementType(uint32_t serial); // 0x00413ABC
void Script_setMovementType(uint32_t serial, int moveType); // 0x00413AF5
int Script_getMoney(uint32_t serial); // 0x00413B33
int Script_getGeneric(uint32_t serial, int type); // 0x00413B66
void Script_gainFame(uint32_t serial, int amount); // 0x00413B99
void Script_returnObject(uint32_t serial); // 0x00413BD4
void Script_loseFame(uint32_t serial, int amount); // 0x00413C1D
int Script_getNotoriety(uint32_t serial); // 0x00413C58
int Script_getNotorietyLevel(uint32_t serial); // 0x00413C73
int Script_getNotorietyLevelByNot(int value); // 0x00413C8E
int Script_getFame(uint32_t serial); // 0x00413C9F
int Script_getAdjFame(uint32_t serial); // 0x00413CBA
int Script_getFameLevel(uint32_t serial); // 0x00413CD5
void Script_setFame(uint32_t serial, int fame); // 0x00413CF0
void Script_changeFame(uint32_t serial, int amount); // 0x00413D20
int Script_getKarma(uint32_t serial); // 0x00413D50
int Script_getAdjKarma(uint32_t serial); // 0x00413D6B
int Script_getKarmaLevel(uint32_t serial); // 0x00413D86
void Script_setKarma(uint32_t serial, int karma); // 0x00413DA1
void Script_changeKarma(uint32_t serial, int amount); // 0x00413DD1
void Script_addNotoriety(uint32_t serial, int amount); // 0x00413E01
void Script_removeNotoriety(uint32_t serial, int amount); // 0x00413E3C
void Script_setNotoriety(uint32_t serial, int not_val); // 0x00413E77
int Script_getCurHP(uint32_t serial); // 0x00413EBB
int Script_getMaxHP(uint32_t serial); // 0x00413ED6
int Script_getCurFatigue(uint32_t serial); // 0x00413EF1
int Script_getMaxFatigue(uint32_t serial); // 0x00413F0C
int Script_getCurMana(uint32_t serial); // 0x00413F27
int Script_getMaxMana(uint32_t serial); // 0x00413F42
int Script_getCappedSkillTotal(uint32_t serial); // 0x00413F93
int Script_getHPLevel(uint32_t serial); // 0x00413FAE
int Script_getFatigueLevel(uint32_t serial); // 0x00413FF2
int Script_getManaLevel(uint32_t serial); // 0x00414036
void Script_setCurHP(uint32_t serial, int hp); // 0x0041407A
void Script_setMaxHP(uint32_t serial, int maxhp); // 0x004140B2
void Script_handleHealthGain(uint32_t serial); // 0x004140F6
void Script_setCurFatigue(uint32_t serial, int stamina); // 0x00414128
void Script_setMaxFatigue(uint32_t serial, int maxstam); // 0x0041416B
void Script_setCurMana(uint32_t serial, int mana); // 0x004141A1
void Script_setMaxMana(uint32_t serial, int maxmana); // 0x004141D7
void Script_addHP(uint32_t serial, int amount); // 0x0041420D
void Script_addMana(uint32_t serial, int amount); // 0x00414263
void Script_addFatigue(uint32_t serial, int amount); // 0x004142A9
void Script_setNaturalAC(uint32_t serial, int ac); // 0x004142EF
int Script_getNaturalAC(uint32_t serial); // 0x00414347
void doDamageCore(uint32_t attackerSerial, uint32_t defenderSerial, uint32_t weaponSerial, int damage, int damageType, int flag); // 0x00414362
void Script_doDamageWithWeapon(uint32_t attacker, uint32_t defender, uint32_t weapon, int damage); // 0x0041459F
void Script_doDamage(uint32_t attacker, uint32_t defender, int damage); // 0x004145C0
void Script_doDamageFight(uint32_t attacker, uint32_t defender, int damage, int flag); // 0x004145DF
void Script_doDamageType(uint32_t attacker, uint32_t defender, int damage, int type); // 0x00414600
void Script_loseHP(uint32_t serial, int amount); // 0x00414621
void Script_loseMana(uint32_t serial, int amount); // 0x0041465B
void Script_loseFatigue(uint32_t serial, int amount); // 0x004146A1
void Script_restoreMobile(uint32_t serial); // 0x004146E7
void Script_restoreFatigue(uint32_t serial); // 0x00414752
void Script_restoreMana(uint32_t serial); // 0x0041478D
void Script_restoreHP(uint32_t serial); // 0x004147C8
CString *Script_getHeShe(CString *out, uint32_t serial); // 0x00414805
CString *Script_getHimHer(CString *out, uint32_t serial); // 0x00414887
CString *Script_getHisHer(CString *out, uint32_t serial); // 0x00414909
void Script_attack(uint32_t attackerSerial, uint32_t victimSerial); // 0x0041498B
void Script_peace(uint32_t serial); // 0x004149DD
void Script_stopAttack(uint32_t serial); // 0x00414A11
void Script_stopFight(uint32_t serial1, uint32_t serial2); // 0x00414A3D
void Script_getAttackersNearby(CList *list, uint32_t serial); // 0x00414A82
void Script_getAttackers(CList *list, uint32_t serial); // 0x00414BC0
int Script_getNumTargets(uint32_t serial); // 0x00414C76
int Script_getNumAttackers(uint32_t serial); // 0x00414CAA
uint32_t Script_getFirstTarget(uint32_t serial); // 0x00414CDE
void Script_getTargets(CList *list, uint32_t serial); // 0x00414D5F
int Script_hasObjEquipped(uint32_t mobSerial, uint32_t objSerial); // 0x00414E15
int Script_hasObjTypeEquipped(uint32_t serial, int type); // 0x00414EA3
int Script_getEquipSlot(uint32_t serial); // 0x00414F27
uint32_t Script_getItemAtSlot(uint32_t serial, int slot); // 0x00414F70
uint32_t Script_getWeapon(uint32_t serial); // 0x00414FCA
int Script_getFreeHandSlot(uint32_t serial); // 0x00415018
int Script_isArmed(uint32_t serial); // 0x00415092
int Script_getYear(void); // 0x004150C6
int Script_getMonth(void); // 0x004150D0
int Script_getWeek(void); // 0x004150DA
int Script_getDay(void); // 0x004150E4
int Script_getHour(void); // 0x004150EE
int Script_getMinute(void); // 0x004150F8
int Script_getSeconds(void); // 0x00415102
int Script_isWeapon(uint32_t serial); // 0x0041510C
int Script_isReallyWeapon(uint32_t serial); // 0x00415127
void Script_handleWatchingSkill(uint32_t serial, int skillId); // 0x00415167
int Script_testSkillReal(uint32_t serial, int skillId); // 0x00415199
int Script_testSkill(uint32_t serial, int skillId); // 0x004151CF
int Script_getSkillSuccessChance(uint32_t serial, int skillId, int difficulty, int range); // 0x0041520E
int Script_testAndLearnSkill(uint32_t serial, int skillId, int difficulty, int range); // 0x0041528B
void Script_addSatiety(uint32_t serial, int amount); // 0x0041530E
int Script_getSatiety(uint32_t serial); // 0x00415357
int Script_getRealStrength(uint32_t serial); // 0x00415399
int Script_getRealDexterity(uint32_t serial); // 0x004153CC
int Script_getRealIntelligence(uint32_t serial); // 0x004153FF
int Script_getStrength(uint32_t serial); // 0x00415432
int Script_getDexterity(uint32_t serial); // 0x00415465
int Script_getIntelligence(uint32_t serial); // 0x00415498
int Script_getStat(uint32_t serial, int statId); // 0x004154CB
int Script_getRealStat(uint32_t serial, int statId); // 0x00415500
int Script_getStatMod(uint32_t serial, int statId); // 0x00415535
int Script_modifyStat(uint32_t serial, int statId, int value); // 0x0041556A
int Script_modifyRealStat(uint32_t serial, int statId, int value); // 0x004155AA
int Script_setStatMod(uint32_t serial, int statId, int value); // 0x004155EA
int Script_setRealStat(uint32_t serial, int statId, int value); // 0x0041562A
int Script_getStatAttributeMax(uint32_t serial, int statId); // 0x0041566A
int Script_setStatAttributeMax(uint32_t serial, int statId, int value); // 0x004156CC
int Script_getSkillLevel(uint32_t serial, int skillId); // 0x004157F5
int Script_getSkillLevelReal(uint32_t serial, int skillId); // 0x00415831
int Script_getSkillLevelRealStat(uint32_t serial, int skillId); // 0x00415865
int Script_getSkillLevelNoStat(uint32_t serial, int skillId); // 0x00415899
int Script_getSkillLevelNoStatNoMod(uint32_t serial, int skillId); // 0x004158CB
void Script_setSkillLevel(uint32_t serial, int skillId, int value); // 0x004158FD
void Script_addSkillLevel(uint32_t serial, int skillId, int delta); // 0x00415932
void Script_loseSkillLevel(uint32_t serial, int skillId, int delta); // 0x00415966
int Script_getSkillMod(uint32_t serial, int skillId); // 0x0041599D
int Script_setSkillMod(uint32_t serial, int skillId, int value); // 0x004159D2
int Script_modifySkillMod(uint32_t serial, int skillId, int delta); // 0x00415A0C
CString *Script_getSkillName(CString *out, int skillId); // 0x00415A42
int Script_getSkillNumber(CString *skillName); // 0x00415A74
void Script_setMobFlag(uint32_t serial, int flagId, int value); // 0x00415A8A
int Script_getMobFlag(uint32_t serial, int flagId); // 0x00415ABE
int Script_canWield(uint32_t mobSerial, uint32_t itemSerial); // 0x00415AF0
int Script_isPiercing(uint32_t serial); // 0x00415B56
int Script_isSlashing(uint32_t serial); // 0x00415B84
int Script_isBashing(uint32_t serial); // 0x00415BB2
int Script_isRanged(uint32_t serial); // 0x00415BE0
int Script_getMaxArmorClass(uint32_t serial); // 0x00415C0E
int Script_getCurArmorClass(uint32_t serial); // 0x00415C42
int Script_setMaxArmorClass(uint32_t serial, int maxAC); // 0x00415C76
int Script_getWeaponCurHP(uint32_t serial); // 0x00415CAD
int Script_setWeaponCurHP(uint32_t serial, int curHP); // 0x00415CE1
int Script_getWeaponMaxHP(uint32_t serial); // 0x00415D18
int Script_setWeaponMaxHP(uint32_t serial, int maxHP); // 0x00415D4C
int Script_getWeaponMinStr(uint32_t serial); // 0x00415D83
int Script_getWeaponSpeed(uint32_t serial); // 0x00415DB7
int Script_getWeaponHitSfx(uint32_t serial); // 0x00415DED
int Script_getWeaponMissSfx(uint32_t serial); // 0x00415E20
int Script_getWeaponMinRange(uint32_t serial); // 0x00415E53
int Script_getAmmoType(uint32_t serial); // 0x00415E87
int Script_getBow(uint32_t serial); // 0x00415EBB
int Script_getWeaponRange(uint32_t serial); // 0x00415EEF
int Script_getWeaponHandedness(uint32_t serial); // 0x00415F23
CString *Script_getWeaponName(CString *out, uint32_t weaponSerial); // 0x00415F56
void Script_getWeaponClass(uint32_t serial, int *outNumDice, int *outDiceFaces, int *outBonus, int *outPad); // 0x00415FBE
void Script_setWeaponClass(uint32_t serial, int numDice, int diceFaces, int bonus, int pad); // 0x00416076
int Script_applyWeaponTemplate(uint32_t serial, int templateId); // 0x004160F0
int Script_getAverageDamage(uint32_t serial); // 0x00416128
uint32_t Script_getBackpack(uint32_t serial); // 0x0041615D
uint32_t Script_transferGenericToContainer(uint32_t containerSerial, uint32_t mobileSerial, uint32_t itemType, uint32_t count); // 0x00416224
uint32_t Script_transferGenericToWorld(CLocation *loc, uint32_t mobileSerial, uint32_t itemType, uint32_t count); // 0x0041630A
void Script_destroyGeneric(uint32_t mobileSerial, uint32_t itemType, uint32_t count); // 0x004163AF
uint32_t Script_giveItem(uint32_t mobSerial, uint32_t itemSerial); // 0x00416403
void Script_systemMessage(uint32_t serial, CString *str); // 0x0041651C
void Script_systemMessageHued(uint32_t serial, int hue, CString *str); // 0x00416551
int Script_getPlayAge(uint32_t serial); // 0x004165B6
int Script_getAccountNum(uint32_t serial); // 0x004165E4
int Script_getCharacterNum(uint32_t serial); // 0x00416613
int Script_isGoldAccount(uint32_t serial); // 0x00416642
int Script_getCombatMode(uint32_t serial); // 0x00416670
int Script_isInvulnerable(uint32_t serial); // 0x004166A0
void Script_makeInvulnerable(uint32_t serial); // 0x004166CE
void Script_makeVulnerable(uint32_t serial); // 0x00416700
int Script_isCorpse(uint32_t serial); // 0x00416732
int Script_getCorpseBodyType(uint32_t serial); // 0x0041675D
int Script_textMessage(uint32_t serial, CString *text, int hue, int font, int speechType); // 0x00416790
int Script_superBark(uint32_t serial, CString *text, int hue, int type, int font); // 0x004167D8
void Script_getMobsInRangeOld(CList *list, CLocation *loc, int range); // 0x00416823
void Script_getMobsInRange(CList *list, CLocation *loc, int range); // 0x0041690A
int Script_objIsInRange(CLocation *loc, int range); // 0x0041695B
void Script_getObjectsInRange(CList *list, CLocation *loc, int range); // 0x00416A2F
void Script_getObjectsInRangeWithFlags(CList *list, CLocation *loc, int range, int flags); // 0x00416B16
int Script_getNumAllObjectsInRangeWithFlags(CLocation *loc, int range, int flags); // 0x00416BDD
int Script_getTerrainFlags(int tileID, int mask); // 0x00416D4C
int Script_getObjectFlags(uint32_t serial, int mask); // 0x00416D76
void Script_getObjectsInRangeOfType(CList *list, CLocation *loc, int range, int bodyType); // 0x00416DAA
void Script_getObjectsInSpecRange(CList *list, CLocation *loc, int minRange, int maxRange); // 0x00416E8E
void Script_getPlayersInRange(CList *list, CLocation *loc, int range); // 0x00417067
uint32_t Script_getClosestMobile(CLocation *loc, int range); // 0x00417096
uint32_t Script_getClosestPlayer(CLocation *loc, int range); // 0x004171C1
uint32_t Script_getClosestOnlinePlayer(CLocation *loc, int range); // 0x004172E9
uint32_t Script_getClosestMobileOrOnlinePlayer(CLocation *loc, int range); // 0x00417424
uint32_t Script_getClosestVisibleOnlinePlayer(CLocation *loc, int range); // 0x0041758C
void Script_getNPCsInRangeOld(CList *list, CLocation *loc, int range); // 0x00417716
void Script_getNPCsInRange(CList *list, CLocation *loc, int range); // 0x004177FD
int Script_getTileAt(CLocation *loc); // 0x0041782C
void Script_getMobsAt(CList *list, CLocation *loc); // 0x0041786D
void Script_getNPCsAt(CList *list, CLocation *loc); // 0x00417900
void Script_getPlayersAt(CList *list, CLocation *loc); // 0x00417993
void Script_getObjectsAt(CList *list, CLocation *loc); // 0x00417A23
void Script_getObjectsAtInZRange(CList *list, CLocation *loc, int zMin, int zMax); // 0x00417AB6
uint32_t Script_getFirstObjectOfType(CLocation *loc, int bodyType); // 0x00417B86
uint32_t Script_getNextObjectOfType(CLocation *loc, int bodyType, uint32_t lastSerial); // 0x00417C0C
int Script_getQuantity(uint32_t serial); // 0x00417CDC
int Script_getMiscData(uint32_t serial); // 0x00417D10
int Script_getQuality(uint32_t serial); // 0x00417D43
int Script_getTileHeight(int type); // 0x00417D76
int Script_getHeight(uint32_t serial); // 0x00417DBE
int Script_getSurfaceHeight(uint32_t serial); // 0x00417DEF
void Script_logOut(uint32_t serial); // 0x00417E20
void Script_safeLogOut(uint32_t serial); // 0x00417E4E
int Script_isHidden(uint32_t serial); // 0x00417E7C
void Script_setHidden(uint32_t serial, int value); // 0x00417EB0
void Script_openGump(uint32_t serial, int gumpId); // 0x00417EE6
void Script_closeGump(uint32_t serial, int gumpId); // 0x00417F51
void Script_doMissile_Loc2Loc(CLocation *srcLoc, CLocation *dstLoc, int effectId, int speed, int duration, int hue); // 0x00417FBC
void Script_doMissile_Loc2Mob(CLocation *srcLoc, uint32_t dstSerial, int effectId, int speed, int duration, int hue); // 0x00418125
void Script_doMissile_Mob2Loc(uint32_t srcSerial, CLocation *dstLoc, int effectId, int speed, int duration, int hue); // 0x004182E3
void Script_doMissile_Mob2Mob(uint32_t srcSerial, uint32_t dstSerial, int effectId, int speed, int duration, int hue); // 0x004184A1
void Script_doLocAnimation(CLocation *loc, int effectId, int speed, int duration, int hue, int renderMode); // 0x004186B4
void Script_doMobAnimation(uint32_t serial, int effectId, int speed, int duration, int hue, int renderMode); // 0x00418801
void Script_doLightning(uint32_t serial); // 0x004189A3
void Script_beginSequence(void); // 0x00418AAF
void Script_endSequence(int actionId); // 0x00418AC9
int Script_waitState(uint32_t serial); // 0x00418AF1
void Script_setWaitState(uint32_t serial, int value); // 0x00418B1F
int Script_isInvisible(uint32_t serial); // 0x00418B59
void Script_setInvisible(uint32_t serial, int value); // 0x00418B8D
void Script_setPoisoned(uint32_t serial, int value); // 0x00418BC3
void Script_setCursed(uint32_t serial, int value); // 0x00418C03
CString *Script_getName(CString *out, uint32_t serial); // 0x00418C43
CString *Script_getRealName(CString *out, uint32_t serial); // 0x00418CAB
void Script_setRealName(uint32_t serial, CString *str); // 0x00418D0E
void Script_setRealNameFromTemplate(uint32_t serial, int templateId); // 0x00418D40
CString *Script_getTitledName(CString *out, uint32_t serial); // 0x00418D8A
void Script_walkTo(uint32_t serial, CLocation *loc, int range); // 0x00418E34
int Script_isInMap(CLocation *loc); // 0x00418E8D
int Script_isInWorld(CLocation *loc); // 0x00418EAB
int Script_getElevationAt(int x, int y); // 0x00418EC9
int Script_findGoodSpotNear(CLocation *loc, int zMax, int range, int height); // 0x00418EFA
int Script_findGoodSpotNearMin(CLocation *loc, int zMin, int zMax, int range, int height); // 0x00418F1D
int Script_findGoodSpotNearWithElev(CLocation *loc, int minElev, int maxElev, int zMax, int range, int height); // 0x00418F42
int Script_findGoodZ(CLocation *loc, int currentZ, int direction, int stepHeight, int height); // 0x00418F6D
int Script_canExistAt(CLocation *loc, int currentZ, int stepHeight); // 0x00418FC5
int Script_canSeeLoc(uint32_t serial, CLocation *loc); // 0x00419012
int Script_canSeeObj(uint32_t serialA, uint32_t serialB); // 0x00419094
void Script_getVisableTargets(CList *list, uint32_t serial); // 0x0041911F
uint32_t Script_getFirstVisableTarget(uint32_t serial); // 0x0041926F
uint32_t Script_getFirstVisableTargetInRange(uint32_t serial, int range); // 0x004193F6
int Script_isValid(uint32_t serial); // 0x004195B3
CString *Script_getNameByType(CString *dest, int typeId); // 0x004195DB
int Script_getX(const CLocation *loc); // 0x00419661
int Script_getY(const CLocation *loc); // 0x0041966C
int Script_getZ(const CLocation *loc); // 0x00419678
void Script_setX(CLocation *loc, int value); // 0x00419684
void Script_setY(CLocation *loc, int value); // 0x00419693
void Script_setZ(CLocation *loc, int value); // 0x004196A3
void Script_moveDir(CLocation *loc, int dir); // 0x004196B3
void Script_replyTo(uint32_t npcSerial, uint32_t targetSerial, CString *speechStr); // 0x004196C4
void Script_replyToMobAbout(uint32_t speakerSerial, uint32_t targetSerial, uint32_t aboutSerial, CString *speechStr); // 0x00419747
int Script_getResourceTypeIdByName(int *retval, CString *name); // 0x004197EA
int Script_hasResource(uint32_t serial, int resourceTypeId); // 0x00419822
void Script_getStaticObjectsAt(CList *list, CLocation *loc); // 0x0041987D
void Script_getStaticObjectsAtXYZ(CList *list, CLocation *loc); // 0x00419903
int Script_hasObj(uint32_t mobSerial, uint32_t objSerial); // 0x00419A5A
int Script_hasObjType(uint32_t mobSerial, int type); // 0x00419B1B
int Script_hasObjTypeInBank(uint32_t mobSerial, int type); // 0x00419BE7
int Script_containsObj(uint32_t containerSerial, uint32_t entitySerial); // 0x00419D3F
uint32_t Script_containsObjType(uint32_t containerSerial, int type); // 0x00419DBF
uint32_t Script_getTopmostContainer(uint32_t serial); // 0x00419E26
int Script_mobileContainsObj(uint32_t mobSerial, uint32_t targetSerial); // 0x00419E80
uint32_t Script_mobileContainsObjType(uint32_t mobSerial, int type); // 0x00419FA9
void Script_getObjectsOfTypeIn(CList *list, uint32_t containerSerial, int typeId); // 0x0041A224
void Script_getContainersOnMobile(CList *list, uint32_t serial); // 0x0041A25C
void Script_useItem(uint32_t playerSerial, uint32_t itemSerial); // 0x0041A30F
int Script_getTemplate(uint32_t serial); // 0x0041A36F
void Script_becomeTemplate(uint32_t serial, int templateId); // 0x0041A39F
int Script_getDefaultAlignment(int templateId); // 0x0041A3D1
void Script_setAlignment(uint32_t serial, int alignment); // 0x0041A400
int Script_isAtHome(uint32_t serial); // 0x0041A43E
int Script_thinksItsAtHome(uint32_t serial); // 0x0041A46C
int Script_hasHome(uint32_t serial); // 0x0041A51B
CLocation *Script_getHome(CLocation *retloc, uint32_t serial); // 0x0041A55D
void Script_setHome(uint32_t serial, CLocation *loc); // 0x0041A5A7
CLocation *Script_getCreationLoc(CLocation *retloc, uint32_t serial); // 0x0041A5EA
void Script_doLookAt(uint32_t playerSerial, uint32_t targetSerial); // 0x0041A631
CString *Script_getArticle(CString *dest, int typeId); // 0x0041A665
int Script_getResource(int *outAmount, uint32_t serial, CString *resName, int resType, int flags); // 0x0041A7A9
int Script_getResourcesOnObj(uint32_t serial, int flags, CList *list); // 0x0041A872
CString *Script_getResourceName(CString *outStr, CString *inStr, int resId); // 0x0041A954
void Script_updateHint(int hintId, uint32_t serial, int flags, CString *name1, CString *name2, CLocation *loc, uint32_t objSerial, CString *name3, int param); // 0x0041AA42
int Script_getHint(uint32_t serial, int flags, int *outInt1, uint32_t *outInt2, int *outInt3, CString *outStr1, CString *outStr2, CLocation *outLoc, uint32_t *outObjSerial,
        CString *outStr3, int *outParam); // 0x0041AB34
void Script_shopKeeperOpenBusiness(uint32_t npcSerial, uint32_t playerSerial); // 0x0041AC3F
void Script_shopKeeperOpenBuying(uint32_t npcSerial, uint32_t playerSerial); // 0x0041AC9C
int Script_hasShopKeyword(CList *list); // 0x0041ACF9
int Script_mobileWillBuy(uint32_t npcSerial, uint32_t itemSerial); // 0x0041AE99
CString *Script_objToStr(CString *out, uint32_t serial); // 0x0041AEF7
void Script_scoreToSpace(CString *str); // 0x0041AF8E
int Script_getLocalizedDesc(CString *outStr, CLocation *outLoc, CLocation *inLoc1, CLocation *inLoc2); // 0x0041AFE2
int Script_isInRegionWithPrefix(uintptr_t prefix_str, uint32_t *loc); // 0x0041B001
int Script_getSmallestArea(uintptr_t outName_str, uint32_t *loc); // 0x0041B025
int Script_getTrammelPhase(void); // 0x0041B0DE
int Script_getFeluccaPhase(void); // 0x0041B0ED
int Script_getBookPages(uint32_t serial); // 0x0041B0FC
void Script_setROBookNum(uint32_t serial, int bookNum); // 0x0041B12F
CString *Script_getROBookTitle(CString *out, int bookNum); // 0x0041B15E
CString *Script_getMoonPhaseStr(CString *out, int phase); // 0x0041B1D6
int Script_getMoonGateDest(int arg); // 0x0041B26A
void Script_splitDice(CString *diceStr, CString *prefixOut, int *numDiceOut, int *facesOut, CString *sepOut, int *bonusOut); // 0x0041B2A5
int Script_getLightTime(uint32_t serial); // 0x0041B4D1
int Script_getLightVal(uint32_t serial); // 0x0041B502
void Script_setLight(uint32_t serial, int lightVal, int lightTime); // 0x0041B533
void Script_makeDice(CString *out, CString *base, int numDice, int faces, CString *sep, int bonus); // 0x0041B567
uint32_t Script_makeMultiInst(CLocation *loc, uint32_t bodyType, int flags); // 0x0041B5CA
uint32_t Script_makeMultiInstCheck(CLocation *loc, uint32_t bodyType, int x, int y, intptr_t z, int checkNum, int artworkId, int validate, int flags); // 0x0041B5FB
int Script_moveMulti(uint32_t serial, CLocation *loc); // 0x0041B646
int Script_canMultiExistAt(uint32_t serial, CLocation *loc, int moveType); // 0x0041B688
int Script_moveMultiCheck(uint32_t serial, CLocation *loc, int checkFlag); // 0x0041B6D9
int Script_recycleMulti(uint32_t serial, int bodyType); // 0x0041B71F
int Script_recycleMultiCheck(uint32_t serial, int bodyType, int checkFlag); // 0x0041B791
int Script_recycleMultiCheckRotate(uint32_t serial, int bodyType, int rotation, int checkFlag); // 0x0041B7F1
int Script_moveMultiMapSwitch(uint32_t serial, CLocation *loc, int mapId); // 0x0041B855
void Script_debugMessage(CString *message); // 0x0041B8A2
void Script_scriptTrig(uint32_t serial, int trigType, uint32_t objArgSerial); // 0x0041B90B
void Script_processTriggerCmds(uint32_t serial, CString *cmdStr); // 0x0041BA50
int Script_withdrawFromBank(uint32_t serial, uint32_t amount); // 0x0041C022
int Script_withdrawAndDestroy(uint32_t serial, uint32_t amount); // 0x0041C07A
int Script_depositIntoBank(uint32_t mobileSerial, uint32_t entitySerial, int amount); // 0x0041C0D4
void Script_openBank(uint32_t serial); // 0x0041C130
int Script_amtGoldInBank(uint32_t mobileSerial); // 0x0041C18A
int Script_getDecayCount(uint32_t serial); // 0x0041C1B9
int Script_getDecayMax(uint32_t serial); // 0x0041C1EF
int Script_setDecayCount(uint32_t serial, int decayCount); // 0x0041C225
int Script_getHomeDecayRate(void); // 0x0041C269
int Script_getNonHomeDecayRate(void); // 0x0041C27D
int Script_getDefaultDieDecay(void); // 0x0041C291
int Script_getDecayInterval(void); // 0x0041C2A5
void Script_setLooksLikeTemplate(uint32_t serial, int templateId); // 0x0041C2DE
void Script_followNpc(uint32_t serial, uint32_t leaderSerial, int range); // 0x0041C325
void Script_stopFollowing(uint32_t serial); // 0x0041C3CC
uint32_t Script_getLeader(uint32_t serial); // 0x0041C43F
void Script_deleteIfValid(uint32_t serial, int bodyType); // 0x0041C498
void Script_deleteIfValidNoFall(uint32_t serial, int bodyType); // 0x0041C54C
int Script_changeRange(uint32_t serial, int isString, int oldValue, int newValue); // 0x0041C591
int Script_isMultiComp(uint32_t serial); // 0x0041C677
int Script_isMultiSlave(uint32_t serial); // 0x0041C6A5
uint32_t Script_getMultiSlaveId(uint32_t serial); // 0x0041C6D3
CLocation *Script_getMultiComponentOffset(CLocation *outLoc, uint32_t serial); // 0x0041C714
int Script_areObjectsOn(uint32_t serial); // 0x0041C770
int Script_getMultiExtents(int typeId, CLocation *minLoc, CLocation *maxLoc); // 0x0041C886
int Script_isHousingOkay(CLocation *loc, int arg); // 0x0041C8A1
int Script_areSpellsOkay(CLocation *loc); // 0x0041C8E1
int Script_inJusticeRegion(CLocation *loc); // 0x0041C91D
int Script_isInCityRegion(CLocation *loc); // 0x0041C938
int Script_getMultiType(uint32_t serial); // 0x0041C974
void Script_resetMultiCarriedDecay(uint32_t serial); // 0x0041C9B5
void Script_getPlayersOnMulti(CList *list, uint32_t serial); // 0x0041CA6C
void Script_getObjectsOnMulti(CList *list, uint32_t serial); // 0x0041CB42
void Script_sendPlayerZmoveStuff(uint32_t serial); // 0x0041CC02
int Script_multiCanExistAt(CLocation *loc, int typeId, int moveType); // 0x0041CCD2
uint32_t Script_mobileHasObjWithListObjOfObj(uint32_t mobSerial, CString *name, uint32_t containerSerial); // 0x0041CCEF
int Script_getPulseNum(void); // 0x0041CD42
int Script_isValidMap(uint32_t serial); // 0x0041CD51
void Script_setMapProperties(uint32_t serial, int unused, int x1, int y1, int x2, int y2, int width, int height); // 0x0041CD9A
int Script_getMapPoint(CLocation *retloc, uint32_t serial, int index); // 0x0041CE00
int Script_copybook(uint32_t srcSerial, uint32_t dstSerial); // 0x0041CE7A
int Script_dropCheck(CLocation *loc, uint32_t serial, int stepHeight); // 0x0041CEF2
int Script_isOnMulti(uint32_t entitySerial, uint32_t multiSerial); // 0x0041CF54
uint32_t Script_isOnAnyMulti(uint32_t entitySerial); // 0x0041D045
uint32_t Script_isAnyMultiAt(CLocation *loc); // 0x0041D12D
uint32_t Script_isAnyMultiBelow(CLocation *loc); // 0x0041D1EF
int Script_openContainer(uint32_t playerSerial, uint32_t containerSerial); // 0x0041D2B1
int Script_getNumInMultiType(int typeId); // 0x0041D336
void Script_multiCompSetSendSlave(uint32_t serial, int value); // 0x0041D35A
int Script_areMobilesInMultiArea(int multiSerial, CLocation *loc); // 0x0041D3A5
void Script_runAway(uint32_t serial, uint32_t targetSerial); // 0x0041D3BC
int Script_getHungerLevel(uint32_t serial); // 0x0041D41B
int Script_eatObject(uint32_t serial, uint32_t foodSerial); // 0x0041D492
void Script_setBehavior(uint32_t serial, int flags); // 0x0041D4EA
void Script_clearBehavior(uint32_t serial, int flags); // 0x0041D53B
void Script_loiter(uint32_t serial, int range); // 0x0041D590
void Script_goLoiter(uint32_t serial, CLocation *loc, int range); // 0x0041D5EB
void Script_setDesireLevel(uint32_t serial, int desireLevel); // 0x0041D641
void Script_setLoiterMode(uint32_t serial, int mode); // 0x0041D68E
int Script_getDesireLevel(uint32_t serial); // 0x0041D72C
void Script_setDecayTest(int mode); // 0x0041D775
int Script_setNPCState(uint32_t serial, int state); // 0x0041D788
int Script_isOwnedPet(uint32_t serial); // 0x0041D7CC
int Script_getNPCState(uint32_t serial); // 0x0041D7FA
int Script_goSleep(uint32_t serial, int steps, int returnState); // 0x0041D82A
void Script_setDefaultReturn(int value); // 0x0041D88C
void Script_addGlobalQuantity(uint32_t serial, int quantity); // 0x0041D8B2
int Script_requestAddQuantity(uint32_t serial, int quantity); // 0x0041D90C
void Script_transferAllResources(uint32_t dstSerial, uint32_t srcSerial); // 0x0041D992
void Script_transferResources(uint32_t dstSerial, uint32_t srcSerial, int amount, CString *resName); // 0x0041DA18
void Script_transferGeneric(uint32_t srcSerial, uint32_t dstSerial, int bodyType); // 0x0041DB70
void Script_returnAllResourcesToBank(uint32_t serial); // 0x0041DC7B
void Script_returnResourcesToBank(uint32_t serial, int amount, CString *resName); // 0x0041DCC0
uint32_t Script_createGlobalObjectAt(int artId, CLocation *loc); // 0x0041DDE4
uint32_t Script_createGlobalObjectIn(int artId, uint32_t containerSerial); // 0x0041DEAF
uint32_t Script_createNoResObjectAt(int artId, CLocation *loc); // 0x0041DFC7
uint32_t Script_createNoResObjectIn(int artId, uint32_t containerSerial); // 0x0041E07D
uint32_t Script_requestCreateObjectAt(int artId, CLocation *loc); // 0x0041E178
uint32_t Script_requestCreateObjectIn(int artId, uint32_t containerSerial); // 0x0041E271
uint32_t Script_createGlobalNPCAtSpecificLoc(int templateId, CLocation *loc); // 0x0041E3C2
uint32_t Script_createGlobalNPCAt(uint32_t templateId, CLocation *loc, int flags); // 0x0041E416
uint32_t Script_requestCreateNPCAt(int templateId, CLocation *loc, int noWander); // 0x0041E4B0
uint32_t Script_createGlobalObjectOn(uint32_t entitySerial, int artId); // 0x0041E4DC
uint32_t Script_findClosestBBoard(CLocation *loc); // 0x0041E559
void Script_setPostTime(uint32_t serial); // 0x0041E58D
uint32_t Script_getChunkEgg(CLocation *loc); // 0x0041E610
int Script_defineResource(uint32_t serial, CString *resourceName, int type, int value1, int value2); // 0x0041E671
void Script_addConsumer(CLocation *loc, int templateIndex, int amount, int templateValue); // 0x0041E736
int Script_whoIsLargestConsumer(CLocation *loc, int templateIndex); // 0x0041E755
int Script_resourceTypeToId(CString *name); // 0x0041E76C
void Script_textEntry(uint32_t serial, uint32_t playerSerial, int gumpId, int parentId, CString *text); // 0x0041E79A
void Script_stringQuery(uint32_t playerSerial, uint32_t serial, int type, CString *question, int cancel, int style, int maxLen, CString *title); // 0x0041E800
void Script_webBrowse(uint32_t serial, CString *str); // 0x0041E874
void Script_makeBeelineFailPathfind(uint32_t serial, int flag); // 0x0041E8CE
void Script_callGuards2(uint32_t serial1, uint32_t serial2, int range); // 0x0041E8FE
void Script_criminalAct(uint32_t criminalSerial, uint32_t victimSerial, int fameAmount, int crimeWeight); // 0x0041E95B
void Script_criminalActAdvanced(uint32_t criminalSerial, uint32_t victimSerial, int fameAmount, int crimeWeight, int bound, int flags); // 0x0041E9C5
int Script_NotorietyCompare(uint32_t serial1, uint32_t serial2); // 0x0041EA33
int Script_witnessCrime(CLocation *loc, uint32_t criminalSerial, uint32_t victimSerial, CString *str, int delay, int priority, int crimeType); // 0x0041EA89
void Script_overloadWeight(uint32_t serial, int weight); // 0x0041EB07
void Script_recalcWeight(uint32_t serial); // 0x0041EB4E
int Script_canBeGeneric(uint32_t serial); // 0x0041EB7E
int Script_isGeneric(uint32_t serial); // 0x0041EBCC
int Script_getTimeSecs(void); // 0x0041EC06
void Script_getCurrentTimeStr(CString *out); // 0x0041EC11
void Script_destroyOne(uint32_t serial); // 0x0041EC24
void Script_removeLeadingWords(CString *str, int count); // 0x0041ED15
int Script_objectsNearby(CList *list, CLocation *loc, int range, int filterType); // 0x0041ED8D
int Script_isHair(uint32_t serial); // 0x0041EEF7
int Script_isEditing(uint32_t serial); // 0x0041EF28
int Script_isCounselor(uint32_t serial); // 0x0041EF56
int Script_isGameMaster(uint32_t serial); // 0x0041EF84
int Script_isManifesting(uint32_t serial); // 0x0041EFB2
int Script_getPlayerBugStat(CList *list, int threshold); // 0x0041EFEA
int Script_doNPCHeartBeat(uint32_t serial); // 0x0041F0CF
int Script_doNPCHandleStates(uint32_t serial); // 0x0041F102
void Script_addHelpRequestToQueue(uint32_t serial, int hasLevel, uint8_t level, CString *message); // 0x0041F135
int Script_getGMCallStatus(void); // 0x0041F217
void Script_setGMCallStatus(int value); // 0x0041F226
void Script_fixBank(uint32_t serial); // 0x0041F239
void Script_setStatus(uint32_t serial, int bit, int value); // 0x0041F267
int Script_getStatus(uint32_t serial, int bit); // 0x0041F299
int Script_putObjBank(uint32_t mobileSerial, uint32_t thingSerial); // 0x0041F2CB
void Script_getScripts(CList *list, uint32_t serial); // 0x0041F300
uint32_t Script_doTakeMoney(uint32_t containerSerial, int bodyType, int amount); // 0x0041F401
void Script_doSCommand(uint32_t serial, CString *str); // 0x0041F48D
void Script_transferPlayer(uint32_t sourceSerial, uint32_t targetSerial, CString *serverStr); // 0x0041F4D1
void Script_checkTransferAccount(uint32_t sourceSerial, uint32_t targetSerial); // 0x0041F522
int Script_resurrect(uint32_t serial, int flag); // 0x0041F564
void Script_destroyContents(uint32_t containerSerial); // 0x0041F596
void Script_committedCrimeAt(uint32_t serial, CLocation *loc, int duration); // 0x0041F5C3
void Script_setCriminal(uint32_t serial, int duration); // 0x0041F5F2
int Script_isCriminal(uint32_t serial); // 0x0041F61D
int Script_isMurderer(uint32_t serial); // 0x0041F638
int Script_getMurderCount(uint32_t serial); // 0x0041F653
void Script_setMurderCount(uint32_t serial, int count); // 0x0041F66E
void Script_setResurrectionResources(uint32_t serial); // 0x0041F69F
int Script_canBeFreelyAggressedBy(uint32_t victimSerial, uint32_t attackerSerial); // 0x0041F6C6
void Script_refreshAggression(uint32_t serial); // 0x0041F721
void Script_receiveAggressionFrom(uint32_t victimSerial, uint32_t aggressorSerial); // 0x0041F753
void Script_receiveUnhealthyActionFrom(uint32_t victimSerial, uint32_t attackerSerial); // 0x0041F79C
void Script_receiveHelpfulActionFrom(uint32_t playerSerial, uint32_t helperSerial); // 0x0041F7E7
void Script_copyControllerInfo(uint32_t controlledSerial, uint32_t controllerSerial); // 0x0041F838
int Script_isObscene(CString *text); // 0x0041F959
void Script_logEntry(int type, int subtype, uint32_t serial, CString *str1, CString *str2, CString *str3, CString *str4); // 0x0041F96F
void Script_setTile(CLocation *loc, int tileID); // 0x0041F9AE
int Script_getTile(CLocation *loc); // 0x0041FA01
void Script_setElevation(CLocation *loc, int elevation); // 0x0041FA21
int Script_getElevation(CLocation *loc); // 0x0041FA74
void Script_createStatic(CLocation *loc, int typeID); // 0x0041FA92
void Script_createStaticHued(CLocation *loc, int typeID, int hue); // 0x0041FAEA
void Script_updatesOn(void); // 0x0041FB4D
void Script_updatesOff(void); // 0x0041FB57
void Script_escript(uint32_t serial, CString *scriptName, CString *args); // 0x0041FB61
void Script_openGenericGump(uint32_t entitySerial, uint32_t playerSerial, int gumpId, int x, int y, CList *cmdList, CList *textList); // 0x0041FBDD
CString *Script_getRattishSyllable(CString *out, int index); // 0x0041FC5E
CString *Script_getLizardishSyllable(CString *out, int index); // 0x00420048
CString *Script_getWispishSyllable(CString *out, int index); // 0x00420306
CString *Script_getOrcishSyllable(CString *out, int index); // 0x00420498
int Script_abs(int value); // 0x00420CE2
int CompareTokenType(const char *tokenBuf, int tokenType); // 0x00420DA0
CExecThread *CExecThread_ScalarDelete(CExecThread *thread, int flags); // 0x0042E160
void *CVector_At(CVector *vec, int index); // 0x0042E220
void CHintItem_Destructor(CHintItem *self); // 0x00463B9E
CHintItem *CHintEntry_Copy(CHintItem *this, CHintItem *src); // 0x00463BA9
void CHintManager_Tick(void); // 0x004642A0
CHintItem *CHintItem_ScalarDelete(CHintItem *this, int flags); // 0x00464350
void SendMultiMessage(uint32_t serial, uint32_t callerSerial, CString *msgName, intptr_t listArgs); // 0x0047D601
int IsNearCampfire(CItem *ent); // 0x00489522
int Script_isInArea(uintptr_t namePrefix_str, uint32_t *loc, int flag); // 0x004A53A8
int Script_findClosestArea(uintptr_t resultName_str, uint32_t *resultLoc, uintptr_t namePrefix_str, uint32_t *fromLoc, int flag); // 0x004A546A
void EventParamBlock_BuildFromEventParams(EventParamBlock *pb, EventParam *params, int numParams, CFuncScope *trigScope);
int VT_GetHeight(CItem *ent);
int Script_getWeight(uint32_t serial);
int Script_isVirtueGuard(uint32_t serial);
int Script_isUsingVirtueShield(uint32_t serial);
int Script_isOrderGuard(uint32_t serial);
int Script_isChaosGuard(uint32_t serial);
int Script_getCanCarry(uint32_t serial);
int Script_getSkillTotal(uint32_t serial);
void Script_intRet(int value);

int check_IsMobile(CItem *ent); // 0x00424370

void Wombat_ShutdownArrays(void); // CUSTOM

extern const BuiltinHandlerEntry g_BuiltInFuncs[];

#endif /* WOMBAT_EXEC_H_ */
