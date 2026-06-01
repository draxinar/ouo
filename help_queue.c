/*
 * CHelpQueue - FIFO of counselor/GM help requests posted by players.
 *
 * Each request carries the speaker, their location, and the text of
 * the page; entries are popped in order as GMs respond. The binary
 * used std::list here; we keep the same semantics with a singly-
 * linked list.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "container.h"
#include "dynamic.h"
#include "egg.h"
#include "gm_names.h"
#include "gm_player_menu.h"
#include "help_queue.h"
#include "load.h"
#include "main.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "resbank.h"
#include "resource_regrowth.h"
#include "skill.h"
#include "taglist.h"
#include "template.h"
#include "time.h"
#include "utils.h"
#include "vtable.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "world.h"

/*
 * Help request entry (0x28 bytes on 32-bit) stored in the
 * std::list<CHelpEntry> wrapped by CHelpQueue.
 */
typedef struct CHelpEntry {
	uint32_t serial;
	uint8_t type;
	uint8_t priority;
	uint8_t _pad06[2];
	CString name;
	CString message;
} CHelpEntry;

/*
 * Counselor/GM assistance record (0x38 bytes on 32-bit) submitted via the
 * help-request packet handler.
 */
typedef struct CAssistance {
	uint32_t serial;
	CString name;
	uint8_t type;
	uint8_t level;
	uint8_t _pad[2];
	CString subject;
	CString body;
} CAssistance;

/*
 * Assistance dispatch node (0x34 bytes on 32-bit) paired with CAssistance
 * for the request-type C/D record variants.
 */
typedef struct CAssistanceNode {
	uint32_t id1;
	uint32_t id2;
	uint16_t field;
	uint8_t _pad0A[2];
	CString str1;
	uint8_t typeFlag;
	uint8_t _pad1[3];
	CString str2;
	uint16_t field1;
	uint16_t field2;
} CAssistanceNode;

static void GM_TargetRename(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetHue(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetMulti(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetSay(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetWalkDest(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetWalkNPC(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetAttackTarget(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetAttackNPC(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static int CounselorDeleteChar(CItem *self, CPlayer *entity); // 0x0044DEE6
static void GM_CreateReagentBag(CPlayer *player, CItem *container);
static void GM_CreateRuneBag(CPlayer *player, CItem *container);
static void *StdHelpList_Init(StdPtrList *list, void *src); // 0x0044F4C0
static void StdHelpList_PushBack(StdPtrList *list, void *value); // 0x0044F500
static void StdHelpList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value); // 0x0044F5F0
static StdPtrNode *StdHelpList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint); // 0x0044F6A0
static void StdHelpList_Destroy(void *list, void *element); // 0x0044F730
static void StdHelpList_ConstructorWrapper(void *list, void *dst, void *src); // 0x0044F770
static void *HelpEntry_DestructorWrapper(void *entry); // 0x0044F790
static void *HelpEntry_Constructor(void *dst, void *src); // 0x0044F7A0
static CHelpEntry *CHelpEntry_ScalarDelete(CHelpEntry *self, int flags); // 0x0044F7E0
static CHelpEntry *CHelpEntry_CopyConstructor(CHelpEntry *self, CHelpEntry *src); // 0x0044F810
static CHelpEntry *CHelpEntry_Constructor(CHelpEntry *self, uint32_t serial, uint8_t type, uint8_t priority, CString *name, CString *message); // 0x0044F8A0
static void CHelpEntry_Destructor(CHelpEntry *self); // 0x0044F920
static void CAssistance_Destructor(CAssistance *this); // 0x0045F1E0
static void CAssistance_NodeDestructor(CAssistanceNode *this); // 0x0045F240
static CAssistance *CAssistance_Constructor(CAssistance *self); // 0x0045F6C0
static uint8_t CAssistance_LoadRecordD(CAssistance *this, uint8_t *buf, int unused); // 0x0045F740
static uint8_t CAssistance_LoadRecordA(CAssistance *this, uint8_t *buf, int unused, uint32_t *serialOut); // 0x0045F8A0
static uint8_t *CAssistance_SaveRecordB(CSkillUseCtx *this); // 0x0045FA20
static int CAssistance_GetSerializedSizeB(CSkillUseCtx *this); // 0x0045FBA0
static uint8_t CAssistance_LoadRecordB(CSkillUseCtx *this, uint8_t *buf, int unused); // 0x0045FBB0
static CAssistanceNode *CAssistanceNode_Constructor(CAssistanceNode *self); // 0x0045FD20
static uint8_t CAssistance_LoadRecordC(CAssistanceNode *this, uint8_t *buf, int unused); // 0x0045FDB0
static void CAssistance_QueueDestructorWrapper(CAssistance *this); // 0x00469530
static uint8_t *CAssistanceQueue_Submit(CAssistance *this, uint8_t requestType); // 0x0049DBD0
static int CAssistanceQueue_GetSerializedSize(CAssistance *this); // 0x0049DD90
static void CHelpQueue_RemoveNode(CHelpQueue *q, CHelpRequestNode *target);
static void GM_ParseSetArgs(const char *arg, int *type, int *skillId, int *value, int *hasValue, char *strArg, size_t strArgSize);
static void GM_ApplySet(CItem *target, int type, int skillId, int value, int hasValue, const char *strArg, char *outMsg, int outMsgSize);
static void GM_TargetSet(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetKill(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetRemove(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetTele(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetMTele(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetMKill(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetMRemove(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetInfo(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetScripts(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetResources(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetAIState(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_ApplyForageMode(CPlayer *player, CItem *target, int enable);
static void GM_TargetForageMode(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetFreeze(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetUnfreeze(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetMute(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetUnmute(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetInvis(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetVis(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetInvuln(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetVuln(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetLight(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetResurrect(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetFillSpellbook(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetBank(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetSpawnNPC(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetLock(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetUnlock(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetItemHP(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetDurtest(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetNpcInv(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetLockItem(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetAttach(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);
static void GM_TargetSetVar(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z);

/*
 * Custom - g_GoLocations
 *
 * Named Felucca destinations recognised by the .go GM command.
 */
static const struct {
	const char *name;
	int16_t x, y, z;
} g_GoLocations[] = {
	// Towns
	{ "britain", 1475, 1645, 20 },
	{ "bucs", 2736, 2166, 0 },
	{ "cove", 2285, 1209, 0 },
	{ "jhelom", 1414, 3816, 0 },
	{ "magincia", 3730, 2161, 20 },
	{ "minoc", 2526, 583, 0 },
	{ "moonglow", 4442, 1122, 5 },
	{ "nujelm", 3668, 1116, 0 },
	{ "ocllo", 3650, 2516, 0 },
	{ "serpent", 3023, 3417, 15 },
	{ "skara", 601, 2171, 0 },
	{ "trinsic", 1927, 2779, 0 },
	{ "vesper", 2882, 788, 0 },
	{ "wind", 1362, 896, 0 },
	{ "yew", 535, 992, 0 },
	{ "delucia", 5228, 3978, 37 },
	{ "papua", 5730, 3208, -4 },
	// Notable sub-locations
	{ "british-castle", 1323, 1624, 55 },
	{ "blackthorn", 1533, 1415, 56 },
	{ "jhelom-pit", 1398, 3742, -21 },
	{ "bucs-tunnels", 2667, 2069, -20 },
	{ "cove-orcfort", 2171, 1332, 0 },
	{ "yew-orcfort", 633, 1499, 0 },
	{ "yew-prison", 354, 836, 20 },
	{ "greenacres", 5445, 1153, 0 },
	// Shrines
	{ "chaos", 1456, 854, 0 },
	{ "compassion", 1856, 872, -1 },
	{ "honesty", 4217, 564, 36 },
	{ "honor", 1730, 3528, 3 },
	{ "humility", 4276, 3699, 0 },
	{ "justice", 1301, 639, 16 },
	{ "sacrifice", 3355, 299, 9 },
	{ "spirituality", 1589, 2485, 5 },
	{ "valor", 2496, 3932, 0 },
	// Dungeon entrances
	{ "covetous", 2499, 919, 0 },
	{ "deceit", 4111, 432, 5 },
	{ "despise", 1298, 1080, 0 },
	{ "destard", 1176, 2637, 0 },
	{ "hythloth", 4721, 3822, 0 },
	{ "shame", 514, 1561, 0 },
	{ "wrong", 2043, 238, 10 },
	{ "terathan", 5451, 3143, -60 },
	{ "fire", 5760, 2908, 15 },
	{ "fire-brit", 2923, 3407, 8 },
	{ "ice", 5210, 2322, 30 },
	{ "ice-brit", 1999, 81, 4 },
	{ "orccave", 1019, 1431, 0 },
	{ "khaldun", 6009, 3775, 19 },
	{ "solen", 729, 1451, 0 },
	// Covetous levels
	{ "covetous1", 5456, 1863, 0 },
	{ "covetous2", 5614, 1997, 0 },
	{ "covetous3", 5579, 1924, 0 },
	{ "covetous-lake", 5467, 1805, 7 },
	{ "covetous-torture", 5552, 1807, 0 },
	// Deceit levels
	{ "deceit1", 5188, 638, 0 },
	{ "deceit2", 5305, 533, 2 },
	{ "deceit3", 5137, 650, 5 },
	{ "deceit4", 5306, 652, 2 },
	// Despise levels
	{ "despise1", 5501, 570, 59 },
	{ "despise2", 5519, 673, 20 },
	{ "despise3", 5407, 859, 45 },
	// Destard levels
	{ "destard1", 5243, 1006, 0 },
	{ "destard2", 5143, 801, 4 },
	{ "destard3", 5137, 986, 5 },
	// Hythloth levels
	{ "hythloth1", 5905, 20, 46 },
	{ "hythloth2", 5976, 169, 0 },
	{ "hythloth3", 6083, 145, -20 },
	{ "hythloth4", 6059, 89, 24 },
	// Shame levels
	{ "shame1", 5395, 126, 0 },
	{ "shame2", 5515, 11, 5 },
	{ "shame3", 5514, 148, 25 },
	{ "shame4", 5875, 20, -5 },
	// Wrong levels
	{ "wrong1", 5825, 630, 0 },
	{ "wrong2", 5690, 569, 25 },
	{ "wrong3", 5703, 639, 0 },
	// Terathan Keep levels
	{ "terathan1", 5342, 1601, 0 },
	{ "terathan-champ", 5205, 1585, 0 },
	{ "terathan-star", 5139, 1767, 0 },
	// Fire levels
	{ "fire1", 5790, 1416, 40 },
	{ "fire2", 5702, 1316, 1 },
	// Ice levels
	{ "ice1", 5875, 150, 15 },
	{ "ice-ratman", 5834, 327, 18 },
	{ "ice-demon", 5700, 305, 0 },
	// Orc Cave levels
	{ "orccave1", 5137, 2014, 0 },
	{ "orccave2", 5332, 1376, 0 },
	{ "orccave3", 5272, 2036, 0 },
	{ NULL, 0, 0, 0 },
};

/*
 * Custom target callbacks for GM commands.
 * TargetCB signature: (CPlayer *, uint8_t type, uint32_t serial,
 *                      uint16_t x, uint16_t y, uint16_t z)
 */

// Custom - template id pending for the next .spawn target
static int g_PendingSpawnTemplate = -1;
// Custom - fame for the next .spawn target
static int16_t g_PendingSpawnFame = 0;
// Custom - karma for the next .spawn target
static int16_t g_PendingSpawnKarma = 0;
// Custom - new name pending for the next .rename target
static char g_PendingRenameBuf[30];
// Custom - hue pending for the next .hue target
static int g_PendingHue = -1;
// Custom - multi type id pending for the next .multi target
static int g_PendingMultiType = -1;
// Custom - text pending for the next .say target
static char g_PendingSayBuf[256];
// Custom - NPC serial pending for the next .walknpc target
static uint32_t g_PendingWalkNPC = 0;
// Custom - NPC serial pending for the next .attacknpc target
static uint32_t g_PendingAttackNPC = 0;
// Custom - HP value pending for the next .itemhp target (-1 = query only)
static int g_PendingItemHP = -1;
// Custom - script name pending for the next .attach target
static char g_PendingAttachScript[64];
// Custom - objvar name/value pending for the next .setvar target
static struct {
	char name[64];
	int value;
} g_PendingSetVar;

// Custom - aiState value pending for the next .aistate target
static int g_PendingAIState;

// Custom - on/off value pending for the next .foragemode target
static int g_PendingForageMode;

/*
 * Target kinds for GM_TargetSet (.set <target> <value>).
 */
enum {
	SET_STR,
	SET_DEX,
	SET_INT,
	SET_HITS,
	SET_MANA,
	SET_STAM,
	SET_SKILL,
	SET_ALL_SKILLS,
	SET_FAME,
	SET_KARMA,
	SET_MAXHP,
	SET_MAXMANA,
	SET_MAXSTAM,
	SET_STRMOD,
	SET_DEXMOD,
	SET_INTMOD,
	SET_NOTORIETY,
	SET_HUNGER,
	SET_STOMACH,
	SET_SKILLMOD,
	SET_ATTITUDE,
	SET_CHANGEFAME,
	SET_CHANGEKARMA,
	SET_NAC,
	SET_NWC,
	SET_BODY,
	SET_PHUE,
	SET_HOMEX,
	SET_HOMEY,
};

/*
 * Staging slot filled by the .set command parser and consumed when the
 * targeting cursor picks a mobile.
 */
// Custom - .set command staging slot consumed by GM_TargetSet
static struct {
	int type;
	int skillId;
	int value;
	int hasValue;
	char strArg[64];
} g_PendingSet;

// Custom - .light command staging slot consumed by GM_TargetLight
static uint8_t g_PendingLightVal;

/*
 * Custom - GM_ParseSetArgs
 *
 * Parse a .set / .settarget argument string into a stat/skill type plus
 * value. On return *type < 0 means the argument did not match any known
 * stat or skill (caller falls back to list/help). strArg receives the
 * raw value portion, used by SET_NWC dice strings.
 */
static void
GM_ParseSetArgs(const char *arg, int *type, int *skillId, int *value, int *hasValue, char *strArg, size_t strArgSize)
{
	const char *p = arg;
	while (*p && *p != ' ')
		p++;
	int nlen = (int)(p - arg);
	while (*p == ' ')
		p++;
	*value = 0;
	*hasValue = 0;
	*type = -1;
	*skillId = -1;
	strArg[0] = '\0';
	if (*p != '\0') {
		int vlen = (int)strlen(p);
		if (vlen >= (int)strArgSize)
			vlen = (int)strArgSize - 1;
		memcpy(strArg, p, vlen);
		strArg[vlen] = '\0';
		if (sscanf(p, "%d", value) == 1)
			*hasValue = 1;
	}
	if (nlen == 3 && strncasecmp(arg, "str", 3) == 0)
		*type = SET_STR;
	else if (nlen == 3 && strncasecmp(arg, "dex", 3) == 0)
		*type = SET_DEX;
	else if (nlen == 3 && strncasecmp(arg, "int", 3) == 0)
		*type = SET_INT;
	else if (nlen == 4 && strncasecmp(arg, "hits", 4) == 0)
		*type = SET_HITS;
	else if (nlen == 4 && strncasecmp(arg, "mana", 4) == 0)
		*type = SET_MANA;
	else if (nlen == 4 && strncasecmp(arg, "stam", 4) == 0)
		*type = SET_STAM;
	else if (nlen == 4 && strncasecmp(arg, "fame", 4) == 0)
		*type = SET_FAME;
	else if (nlen == 5 && strncasecmp(arg, "karma", 5) == 0)
		*type = SET_KARMA;
	else if (nlen == 3 && strncasecmp(arg, "all", 3) == 0)
		*type = SET_ALL_SKILLS;
	else if ((nlen == 5 && strncasecmp(arg, "maxhp", 5) == 0) || (nlen == 7 && strncasecmp(arg, "maxhits", 7) == 0))
		*type = SET_MAXHP;
	else if (nlen == 7 && strncasecmp(arg, "maxmana", 7) == 0)
		*type = SET_MAXMANA;
	else if ((nlen == 7 && strncasecmp(arg, "maxstam", 7) == 0) || (nlen == 10 && strncasecmp(arg, "maxfatigue", 10) == 0))
		*type = SET_MAXSTAM;
	else if ((nlen == 2 && strncasecmp(arg, "sm", 2) == 0) || (nlen == 6 && strncasecmp(arg, "strmod", 6) == 0) || (nlen == 11 && strncasecmp(arg, "strengthmod", 11) == 0))
		*type = SET_STRMOD;
	else if ((nlen == 2 && strncasecmp(arg, "dm", 2) == 0) || (nlen == 6 && strncasecmp(arg, "dexmod", 6) == 0) || (nlen == 12 && strncasecmp(arg, "dexteritymod", 12) == 0))
		*type = SET_DEXMOD;
	else if ((nlen == 2 && strncasecmp(arg, "im", 2) == 0) || (nlen == 6 && strncasecmp(arg, "intmod", 6) == 0) || (nlen == 15 && strncasecmp(arg, "intelligencemod", 15) == 0))
		*type = SET_INTMOD;
	else if ((nlen == 4 && strncasecmp(arg, "noto", 4) == 0) || (nlen == 9 && strncasecmp(arg, "notoriety", 9) == 0))
		*type = SET_NOTORIETY;
	else if (nlen == 6 && strncasecmp(arg, "hunger", 6) == 0)
		*type = SET_HUNGER;
	else if (nlen == 7 && strncasecmp(arg, "stomach", 7) == 0)
		*type = SET_STOMACH;
	else if (nlen == 8 && strncasecmp(arg, "attitude", 8) == 0)
		*type = SET_ATTITUDE;
	else if ((nlen == 5 && strncasecmp(arg, "cfame", 5) == 0) || (nlen == 10 && strncasecmp(arg, "changefame", 10) == 0))
		*type = SET_CHANGEFAME;
	else if ((nlen == 6 && strncasecmp(arg, "ckarma", 6) == 0) || (nlen == 11 && strncasecmp(arg, "changekarma", 11) == 0))
		*type = SET_CHANGEKARMA;
	else if ((nlen == 3 && strncasecmp(arg, "nac", 3) == 0) || (nlen == 9 && strncasecmp(arg, "naturalac", 9) == 0))
		*type = SET_NAC;
	else if ((nlen == 3 && strncasecmp(arg, "nwc", 3) == 0) || (nlen == 9 && strncasecmp(arg, "naturalwc", 9) == 0))
		*type = SET_NWC;
	else if (nlen == 4 && strncasecmp(arg, "body", 4) == 0)
		*type = SET_BODY;
	else if (nlen == 4 && strncasecmp(arg, "phue", 4) == 0)
		*type = SET_PHUE;
	else if (nlen == 6 && strncasecmp(arg, "home_x", 6) == 0)
		*type = SET_HOMEX;
	else if (nlen == 6 && strncasecmp(arg, "home_y", 6) == 0)
		*type = SET_HOMEY;
	else if (nlen == 8 && strncasecmp(arg, "skillmod", 8) == 0) {
		int sid, sv;
		if (sscanf(p, "%d %d", &sid, &sv) == 2 && sid >= 0 && sid < MAX_SKILLS) {
			*type = SET_SKILLMOD;
			*skillId = sid;
			*value = sv;
			*hasValue = 1;
		} else {
			char sn[80];
			const char *lsp = strrchr(p, ' ');
			if (lsp != NULL && sscanf(lsp + 1, "%d", &sv) == 1 && (lsp - p) < (int)sizeof(sn)) {
				memcpy(sn, p, lsp - p);
				sn[lsp - p] = '\0';
				int i;
				for (i = 0; i < MAX_SKILLS; i++) {
					const char *nm = CSkillManager_GetSkillName(&g_SkillManager, (int8_t)i);
					if (nm && strncasecmp(nm, sn, strlen(sn)) == 0 && (nm[strlen(sn)] == '\0' || nm[strlen(sn)] == '\r' || nm[strlen(sn)] == ' ')) {
						*type = SET_SKILLMOD;
						*skillId = i;
						*value = sv;
						*hasValue = 1;
						break;
					}
				}
			}
		}
	} else {
		int sid, sv;
		if (sscanf(arg, "%d %d", &sid, &sv) == 2 && sid >= 0 && sid < MAX_SKILLS && sv >= 0 && sv <= 1000) {
			*type = SET_SKILL;
			*skillId = sid;
			*value = sv;
			*hasValue = 1;
		} else {
			char sn[80];
			const char *lsp = strrchr(arg, ' ');
			if (lsp != NULL && sscanf(lsp + 1, "%d", &sv) == 1 && sv >= 0 && sv <= 1000 && (lsp - arg) < (int)sizeof(sn)) {
				memcpy(sn, arg, lsp - arg);
				sn[lsp - arg] = '\0';
				int i;
				for (i = 0; i < MAX_SKILLS; i++) {
					const char *nm = CSkillManager_GetSkillName(&g_SkillManager, (int8_t)i);
					if (nm && strncasecmp(nm, sn, strlen(sn)) == 0 && (nm[strlen(sn)] == '\0' || nm[strlen(sn)] == '\r' || nm[strlen(sn)] == ' ')) {
						*type = SET_SKILL;
						*skillId = i;
						*value = sv;
						*hasValue = 1;
						break;
					}
				}
			}
		}
	}
}

// Apply a stat or skill change to a mobile. Returns message in outMsg.
static void
GM_ApplySet(CItem *target, int type, int skillId, int value, int hasValue, const char *strArg, char *outMsg, int outMsgSize)
{
	CMobile *mob = (CMobile *)target;

	switch (type) {
	case SET_STR:
		if (hasValue && value >= 1 && value <= 200) {
			mob->baseStr = (uint16_t)value;
			CMobile_SetMaxHP(mob, value);
			CMobile_SetHP(mob, value);
			snprintf(outMsg, outMsgSize, "STR set to %d, HP=%d/%d", value, value, value);
		} else if (!hasValue) {
			snprintf(outMsg, outMsgSize, "STR=%d", mob->baseStr);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be 1-200");
		}
		return;
	case SET_DEX:
		if (hasValue && value >= 1 && value <= 200) {
			mob->baseDex = (uint16_t)value;
			CMobile_SetMaxStamina(mob, value);
			CMobile_SetStamina(mob, value);
			snprintf(outMsg, outMsgSize, "DEX set to %d, Stam=%d/%d", value, value, value);
		} else if (!hasValue) {
			snprintf(outMsg, outMsgSize, "DEX=%d", mob->baseDex);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be 1-200");
		}
		return;
	case SET_INT:
		if (hasValue && value >= 1 && value <= 200) {
			mob->baseInt = (uint16_t)value;
			CMobile_SetMaxMana(mob, value);
			CMobile_SetMana(mob, value);
			snprintf(outMsg, outMsgSize, "INT set to %d, Mana=%d/%d", value, value, value);
		} else if (!hasValue) {
			snprintf(outMsg, outMsgSize, "INT=%d", mob->baseInt);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be 1-200");
		}
		return;
	case SET_HITS:
		if (hasValue && value >= 0 && value <= (int)mob->maxHp) {
			CMobile_SetHP(mob, value);
			snprintf(outMsg, outMsgSize, "HP set to %d/%d", value, mob->maxHp);
		} else if (!hasValue) {
			CMobile_SetHP(mob, (int)mob->maxHp);
			snprintf(outMsg, outMsgSize, "HP refilled to %d/%d", mob->maxHp, mob->maxHp);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be 0-%d", mob->maxHp);
		}
		return;
	case SET_MANA:
		if (hasValue && value >= 0 && value <= (int)mob->maxMana) {
			CMobile_SetMana(mob, value);
			snprintf(outMsg, outMsgSize, "Mana set to %d/%d", value, mob->maxMana);
		} else if (!hasValue) {
			CMobile_SetMana(mob, (int)mob->maxMana);
			snprintf(outMsg, outMsgSize, "Mana refilled to %d/%d", mob->maxMana, mob->maxMana);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be 0-%d", mob->maxMana);
		}
		return;
	case SET_STAM:
		if (hasValue && value >= 0 && value <= (int)mob->maxStamina) {
			CMobile_SetStamina(mob, value);
			snprintf(outMsg, outMsgSize, "Stam set to %d/%d", value, mob->maxStamina);
		} else if (!hasValue) {
			CMobile_SetStamina(mob, (int)mob->maxStamina);
			snprintf(outMsg, outMsgSize, "Stam refilled to %d/%d", mob->maxStamina, mob->maxStamina);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be 0-%d", mob->maxStamina);
		}
		return;
	case SET_FAME:
		if (hasValue && value >= 0 && value <= 20000) {
			CMobile_SetFame(mob, (int16_t)value);
			snprintf(outMsg, outMsgSize, "Fame set to %d", value);
		} else if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Fame=%d", mob->fame);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be 0-20000");
		}
		return;
	case SET_KARMA:
		if (hasValue && value >= -20000 && value <= 20000) {
			CMobile_SetKarma(mob, (int16_t)value);
			snprintf(outMsg, outMsgSize, "Karma set to %d", value);
		} else if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Karma=%d", mob->karma);
		} else {
			snprintf(outMsg, outMsgSize, "Value must be -20000 to 20000");
		}
		return;
	case SET_SKILL:
		if (hasValue && value >= 0 && value <= 1000 && skillId >= 0 && skillId < MAX_SKILLS) {
			CMobile_SetSkill(mob, (int8_t)skillId, (uint16_t)value);
			CSkillManager_SendSkillUpdate(mob, (int8_t)skillId);
			snprintf(outMsg, outMsgSize, "Skill %d set to %d", skillId, value);
		} else {
			snprintf(outMsg, outMsgSize, "Invalid skill/value");
		}
		return;
	case SET_ALL_SKILLS:
		if (hasValue && value >= 0 && value <= 1000) {
			int i;
			int numSkills = CSkillManager_GetMaxSkills(&g_SkillManager);
			for (i = 0; i < numSkills; i++)
				CMobile_SetSkill(mob, (int8_t)i, (uint16_t)value);
			CSkillManager_SendSkillList(&g_SkillManager, (CItem *)mob);
			snprintf(outMsg, outMsgSize, "All skills set to %d", value);
		} else {
			snprintf(outMsg, outMsgSize, "Usage: .settarget all VALUE (0-1000)");
		}
		return;
	case SET_MAXHP:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "MaxHP=%d", mob->maxHp);
			return;
		}
		((void (*)(void *, int))VT_FN(target, VT_SET_MAX_HP))(mob, value);
		snprintf(outMsg, outMsgSize, "MaxHP set to %d", value);
		return;
	case SET_MAXMANA:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "MaxMana=%d", mob->maxMana);
			return;
		}
		((void (*)(void *, int))VT_FN(target, VT_SET_MAX_MANA))(mob, value);
		snprintf(outMsg, outMsgSize, "MaxMana set to %d", value);
		return;
	case SET_MAXSTAM:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "MaxStam=%d", mob->maxStamina);
			return;
		}
		((void (*)(void *, int))VT_FN(target, VT_SET_MAX_STAMINA))(mob, value);
		snprintf(outMsg, outMsgSize, "MaxStam set to %d", value);
		return;
	case SET_STRMOD:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "StrMod=%d", (int)mob->strBonus);
			return;
		}
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_STAT_BONUS))(mob, 0, (int16_t)value);
		snprintf(outMsg, outMsgSize, "StrMod set to %d", value);
		return;
	case SET_DEXMOD:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "DexMod=%d", (int)mob->dexBonus);
			return;
		}
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_STAT_BONUS))(mob, 1, (int16_t)value);
		snprintf(outMsg, outMsgSize, "DexMod set to %d", value);
		return;
	case SET_INTMOD:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "IntMod=%d", (int)mob->intBonus);
			return;
		}
		((void (*)(void *, int, int16_t))VT_FN(target, VT_SET_STAT_BONUS))(mob, 2, (int16_t)value);
		snprintf(outMsg, outMsgSize, "IntMod set to %d", value);
		return;
	case SET_NOTORIETY:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Noto=%d", mob->notoriety);
			return;
		}
		((void (*)(void *, int))VT_FN(target, VT_SET_NOTORIETY))(mob, value);
		snprintf(outMsg, outMsgSize, "Noto set to %d", value);
		return;
	case SET_HUNGER:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Hunger=%d", mob->hunger);
			return;
		}
		mob->hunger = (uint8_t)value;
		snprintf(outMsg, outMsgSize, "Hunger set to %d", (int)(uint8_t)value);
		return;
	case SET_STOMACH:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Stomach=%d", mob->stomach);
			return;
		}
		mob->stomach = (uint8_t)value;
		snprintf(outMsg, outMsgSize, "Stomach set to %d", (int)(uint8_t)value);
		return;
	case SET_SKILLMOD:
		if (hasValue && skillId >= 0 && skillId < MAX_SKILLS) {
			CMobile_SetSkillBonus(mob, (int8_t)skillId, (int16_t)value);
			CSkillManager_SendSkillUpdate(mob, (int8_t)skillId);
			snprintf(outMsg, outMsgSize, "SkillMod %d set to %d", skillId, value);
		} else {
			snprintf(outMsg, outMsgSize, "Usage: .set skillmod <id|name> <value>");
		}
		return;
	case SET_ATTITUDE:
		if (!VT_IsNPC(target)) {
			snprintf(outMsg, outMsgSize, "Not an NPC");
			return;
		}
		if (!hasValue) {
			CNPC *npc = (CNPC *)target;
			snprintf(outMsg, outMsgSize, "Attitude=%d", npc->mobile.attackMode);
			return;
		}
		((CNPC *)target)->mobile.attackMode = (uint8_t)value;
		snprintf(outMsg, outMsgSize, "Attitude set to %d", (int)(uint8_t)value);
		return;
	case SET_CHANGEFAME:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Usage: .set cfame <delta>");
			return;
		}
		CMobile_ChangeFame(mob, value);
		snprintf(outMsg, outMsgSize, "Fame changed by %d, now %d", value, mob->fame);
		return;
	case SET_CHANGEKARMA:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Usage: .set ckarma <delta>");
			return;
		}
		CMobile_ChangeKarma(mob, value);
		snprintf(outMsg, outMsgSize, "Karma changed by %d, now %d", value, mob->karma);
		return;
	case SET_NAC:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Usage: .set nac <value>");
			return;
		}
		CMobile_SetBonusAC(mob, value);
		snprintf(outMsg, outMsgSize, "Natural AC set to %d", value);
		return;
	case SET_NWC:
		if (strArg == NULL || strArg[0] == '\0') {
			snprintf(outMsg, outMsgSize, "Usage: .set nwc <NdM+K>");
			return;
		}
		{
			CWeaponDice dice;
			CDiceRoll_InitParse(&dice, strArg);
			CMobile_SetArmorRating(mob, &dice);
			snprintf(outMsg, outMsgSize, "Natural WC set to %s", strArg);
		}
		return;
	case SET_BODY:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Body=0x%04X", target->resourceEntity.entity.bodyType);
			return;
		}
		CEntity_SetBodyType(target, (uint16_t)value);
		if (!target->resourceEntity.entity.removedFromWorld) {
			((void (*)(void *))VT_FN(target, VT_HIDE))(target);
			((void (*)(void *))VT_FN(target, VT_RETURN_TO_TRACKED))(target);
		}
		snprintf(outMsg, outMsgSize, "Body set to 0x%04X", (uint16_t)value);
		return;
	case SET_PHUE:
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "Hue=0x%04X", target->resourceEntity.entity.color);
			return;
		}
		target->resourceEntity.entity.color = (uint16_t)(value | 0x8000);
		if (!target->resourceEntity.entity.removedFromWorld) {
			((void (*)(void *))VT_FN(target, VT_HIDE))(target);
			((void (*)(void *))VT_FN(target, VT_RETURN_TO_TRACKED))(target);
		}
		snprintf(outMsg, outMsgSize, "Partial hue set to 0x%04X", (uint16_t)(value | 0x8000));
		return;
	case SET_HOMEX:
		if (!VT_IsNPC(target)) {
			snprintf(outMsg, outMsgSize, "Not an NPC");
			return;
		}
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "home_x=%d", (int)(int16_t)((CNPC *)target)->homeLoc.x);
			return;
		}
		{
			CNPC *npc = (CNPC *)target;
			npc->homeLoc.x = (uint16_t)value;
			if ((int16_t)npc->homeLoc.z == -1)
				npc->homeLoc.z = target->resourceEntity.entity.location.z;
			snprintf(outMsg, outMsgSize, "home_x set to %d", value);
		}
		return;
	case SET_HOMEY:
		if (!VT_IsNPC(target)) {
			snprintf(outMsg, outMsgSize, "Not an NPC");
			return;
		}
		if (!hasValue) {
			snprintf(outMsg, outMsgSize, "home_y=%d", (int)(int16_t)((CNPC *)target)->homeLoc.y);
			return;
		}
		{
			CNPC *npc = (CNPC *)target;
			npc->homeLoc.y = (uint16_t)value;
			if ((int16_t)npc->homeLoc.z == -1)
				npc->homeLoc.z = target->resourceEntity.entity.location.z;
			snprintf(outMsg, outMsgSize, "home_y set to %d", value);
		}
		return;
	}
	snprintf(outMsg, outMsgSize, "Unknown set type");
}

static void
GM_TargetSet(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Set cancelled");
		return;
	}
	int itemLevel = (g_PendingSet.type == SET_BODY || g_PendingSet.type == SET_PHUE);
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Target not found");
		return;
	}
	if (!itemLevel && !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	char msg[80];
	GM_ApplySet(target, g_PendingSet.type, g_PendingSet.skillId, g_PendingSet.value, g_PendingSet.hasValue, g_PendingSet.strArg, msg, sizeof(msg));
	char fullMsg[128];
	const char *name = ((char *(*)(void *))VT_FN(target, VT_GET_NAME))(target);
	snprintf(fullMsg, sizeof(fullMsg), "[0x%08X %s] %s", serial, name ? name : "?", msg);
	CPlayer_SystemMessage(player, fullMsg);
}

static void
GM_TargetKill(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Kill cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	if (VT_IsDead(target)) {
		CPlayer_SystemMessage(player, "Already dead");
		return;
	}
	// Mirror combat death (combat.c:1415-1422 / 0x004B... DamageHelper):
	// OnDeathWrap creates the corpse and runs death events but does not
	// remove the mobile; the caller deletes it for non-players.
	((void (*)(void *, void *, int))VT_FN(target, VT_ON_DEATH_WRAP))(target, player, 1);
	if (!VT_IsPlayer(target))
		((void (*)(void *))VT_FN(target, VT_DELETE))(target);
	char msg[80];
	snprintf(msg, sizeof(msg), "Killed 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetRemove(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Remove cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}
	// Don't allow removing players
	if (VT_IsPlayer(target)) {
		CPlayer_SystemMessage(player, "Cannot remove a player");
		return;
	}
	char msg[80];
	snprintf(msg, sizeof(msg), "Removed 0x%08X", serial);
	CWorld_DeleteEntity(g_World, target);
	CPlayer_SystemMessage(player, msg);
}

CHelpQueue g_HelpQueue;

/*
 * 0x0044DEC0 - CHelpQueue::ClearInit (inline)
 *
 * Empties the help list and zeros the counselor count.
 */
static __attribute__((unused)) CHelpQueue *
CHelpQueue_ClearInit(CHelpQueue *this)
{
	void *dummy;
	StdHelpList_Init((StdPtrList *)this, &dummy);
	this->counselorCount = 0;
	return this;
}

/*
 * 0x0044DEE6 - CounselorDeleteChar
 *
 * Demotes a counselor character: hides the entity, clears counselor
 * and dead pflags, restores the body type from sex, nulls notoriety,
 * drops the counType script, detaches trackers, and equips a backpack
 * so the world keeps a decay-eligible container. Returns 1 on success,
 * 0 if not a counselor or the delete check fails.
 */
static int __attribute__((unused))
CounselorDeleteChar(CItem *self, CPlayer *entity)
{
	CItem *container;

	USED(self);

	if (!CPlayer_IsCounselor(entity))
		return 0;

	if (!CItem_DeleteCheck2((CItem *)entity))
		return 0;

	((void (*)(void *))VT_FN((CItem *)entity, VT_HIDE))((CItem *)entity);

	entity->pflags &= ~(uint32_t)PlayerIsCounselor;

	entity->pflags &= ~(uint32_t)PlayerIsDead;

	CEntity_SetBodyType((CItem *)entity, (uint16_t)(entity->mobile.sex + 0x190));

	// Binary bug: writes to color at +0x08 - likely intended
	// location.x at +0x0A. Reproduced exactly.
	entity->mobile.container.item.resourceEntity.entity.color = 0x83EA;

	((void (*)(void *, int))VT_FN((CItem *)entity, VT_SET_NOTORIETY))(entity, 0);

	if (CResourceEntity_HasTag((CItem *)entity, "counType", 0))
		CResourceEntity_DetachScript((CItem *)entity, "counType");

	((int (*)(void *))VT_FN((CItem *)entity, VT_RETURN_TO_TRACKED))((CItem *)entity);

	container = CWorld_CreateContainerItem(g_World, 0xE75);
	if (container != NULL) {
		if (CItem_TryEquipOnMobile(container, (CItem *)entity) == 1) {
			CItem_Setup(container, 1, CEntity_GetLocation(&entity->mobile.container.item.resourceEntity.entity), 0, 1);
			if (!ValidateInWorld(container))
				container = NULL;
			if (container != NULL)
				CItem_DecayProcess(container);
		} else {
			if (container != NULL)
				((void (*)(void *))VT_FN(container, VT_DELETE))(container);
			container = NULL;
		}
	}

	return 1;
}

/*
 * 0x0044E1FE - CHelpQueue::FindBySerial
 *
 * Returns the queue entry with the given serial, or NULL if none.
 */
CHelpRequestNode *
CHelpQueue_FindBySerial(CHelpQueue *q, uint32_t serial)
{
	CHelpRequestNode *node;

	for (node = q->head; node != NULL; node = node->next) {
		if (node->serial == serial)
			return node;
	}
	return NULL;
}

/*
 * 0x0044E272 - CHelpQueue::Add
 *
 * Adds a help request with level 'n' (new). Returns 0 if a request
 * for the same serial is already queued.
 */
int
CHelpQueue_Add(CHelpQueue *q, uint32_t serial, const char *name, uint8_t level, const char *message)
{
	if (CHelpQueue_FindBySerial(q, serial) != NULL)
		return 0;
	CHelpQueue_AddEntry(q, serial, 'n', (char)level, name, message);
	return 1;
}

/*
 * 0x0044E2D1 - CHelpQueue::AddEntry
 *
 * Appends a CHelpRequestEntry with the given fields. Returns 0 if
 * a request for the same serial is already queued.
 */
int
CHelpQueue_AddEntry(CHelpQueue *q, uint32_t serial, char level, char origLevel, const char *name, const char *message)
{
	CHelpRequestNode *node;
	CHelpRequestNode **tail;

	if (CHelpQueue_FindBySerial(q, serial) != NULL)
		return 0;

	node = (CHelpRequestNode *)malloc(sizeof(CHelpRequestNode));
	if (node == NULL)
		return 0;
	node->serial = serial;
	node->level = level;
	node->origLevel = origLevel;
	CString_Constructor(&node->callerName, name);
	CString_Constructor(&node->message, message);
	node->next = NULL;

	for (tail = &q->head; *tail != NULL; tail = &(*tail)->next)
		;
	*tail = node;
	q->count++;
	return 1;
}

/*
 * 0x0044E37C - CHelpQueue::AddWithLevel
 *
 * No-op in the binary.
 */
void
CHelpQueue_AddWithLevel(CHelpQueue *q, uint32_t serial, const char *name, uint8_t level, const char *message)
{
	USED(q);
	USED(serial);
	USED(name);
	USED(level);
	USED(message);
}

/*
 * 0x0044E389 - CHelpQueue::UpdateLevel
 *
 * Delegates to SetLevel when the queued serial is found.
 */
int
CHelpQueue_UpdateLevel(CHelpQueue *q, uint32_t serial, char level)
{
	CHelpRequestNode *node;

	node = CHelpQueue_FindBySerial(q, serial);
	if (node == NULL)
		return 0;
	return CHelpQueue_SetLevel(q, serial, level);
}

/*
 * 0x0044E3D9 - CHelpQueue::SetLevel
 *
 * Updates the level byte for an entry, or removes the entry when
 * level == 'd'. Returns 0 if no match.
 */
int
CHelpQueue_SetLevel(CHelpQueue *q, uint32_t serial, char level)
{
	CHelpRequestNode *node;

	node = CHelpQueue_FindBySerial(q, serial);
	if (node == NULL)
		return 0;
	if (level == 'd') {
		CHelpQueue_RemoveNode(q, node);
	} else {
		node->level = level;
	}
	return 1;
}

/*
 * 0x0044E448 - CHelpQueue::GotoEntity
 *
 * Teleports the GM to the victim's tile via VT_DROP_AT_FEET.
 * Returns 0 if either entity is missing.
 */
int
CHelpQueue_GotoEntity(CHelpQueue *q, uint32_t gmSerial, uint32_t victimSerial)
{
	CPlayer *gm;
	CItem *victim;
	CLocation *loc;

	USED(q);

	gm = CPlayerList_FindBySerial(gmSerial);
	victim = CWorld_FindBySerial(g_World, victimSerial);
	if (gm != NULL && victim != NULL) {
		loc = CEntity_GetLocation(&victim->resourceEntity.entity);
		((void (*)(CItem *, CLocation *))VT_FN((CItem *)gm, VT_DROP_AT_FEET))((CItem *)gm, loc);
		return 1;
	}
	return 0;
}

/*
 * 0x0044E4A2 - CHelpQueue::FindNextPending
 *
 * Returns the first entry with level == 'n' (new/pending), or NULL.
 */
CHelpRequestNode *
CHelpQueue_FindNextPending(CHelpQueue *q)
{
	CHelpRequestNode *node;

	for (node = q->head; node != NULL; node = node->next) {
		if (node->level == 'n')
			return node;
	}
	return NULL;
}

/*
 * 0x0044E518 - GmCommandDispatch
 *
 * Dispatches a GM/counselor command typed with the '.' or '=' prefix.
 */
void
GmCommandDispatch(CHelpQueue *q, CPlayer *player, const char *text)
{
	char cmd[256];
	char name[256];
	CString callerStr;
	CString nameStr;
	const char *pname;
	const char *gmName;

	if (text[0] == '\0')
		return;
	strncpy(cmd, text + 1, 254);
	cmd[255] = '\0';

	if (strcmp(cmd, "q") == 0) {
		CHelpQueue_ShowQueue(q, player, 4);
		return;
	}

	if (strcmp(cmd, "aq") == 0) {
		CHelpQueue_ShowQueue(q, player, -1);
		return;
	}

	if (strcmp(cmd, "next") == 0) {
		CHelpQueue_Next(q, player);
		return;
	}

	if (strcmp(cmd, "gotocur") == 0) {
		CHelpQueue_GotoCur(q, player);
		return;
	}

	// .helpme <message>
	if (strncmp(cmd, "helpme", 6) == 0) {
		if (cmd[6] != '\0' && cmd[6] == ' ') {
			CString_Constructor(&callerStr, "Counselor: ");
			CString_AppendCStr(&callerStr, cmd + 7);
			pname = ((const char *(*)(void *))VT_FN((CItem *)player, VT_GET_NAME))(player);
			CString_Constructor(&nameStr, pname);
			CHelpQueue_AddWithLevel(q, CMobile_GetSerial(&player->mobile), CString_GetBuffer(&nameStr), 0, CString_GetBuffer(&callerStr));
			CString_Destructor(&nameStr);
			CPlayer_SystemMessage(player, "GM help request entered.");
			CString_Destructor(&callerStr);
		}
		return;
	}

	// .gm [name]
	if (strncmp(cmd, "gm", 2) == 0) {
		gmName = "";
		if (cmd[2] != '\0' && cmd[2] == ' ')
			gmName = cmd + 3;
		CHelpQueue_GmTransfer(q, player, gmName);
		return;
	}

	// Custom: .players - opens GM player-menu gump
	if (strcmp(cmd, "players") == 0 && CPlayer_IsEditing(player)) {
		GM_OpenPlayerMenu(player);
		return;
	}

	// Custom: .gotoplayer <name|0xSERIAL|decimal_serial> - teleport adjacent
	// to a connected player. Must be checked before .goto, which prefix-
	// matches "goto".
	if (strncmp(cmd, "gotoplayer ", 11) == 0 && CPlayer_IsEditing(player)) {
		GM_GotoPlayerCommand(player, cmd + 11);
		return;
	}

	// .goto <arg>
	if (strncmp(cmd, "goto", 4) == 0) {
		memset(name, 0, sizeof(name));
		if (sscanf(cmd, "%*s %s", name) == 1) {
			if (isdigit((unsigned char)name[0])) {
				CHelpQueue_GotoBySerial(q, player, (int)atol(name));
			} else {
				CHelpQueue_GotoByName(q, player, name);
			}
		}
		return;
	}

	if (strcmp(cmd, "rel") == 0) {
		CHelpQueue_Relinquish(q, player);
		return;
	}

	if (strcmp(cmd, "clear") == 0) {
		CHelpQueue_Clear(q, player);
		return;
	}

	if (strcmp(cmd, "jail") == 0) {
		DoJailCommand((CItem *)player);
		return;
	}

	if (strcmp(cmd, "unjail") == 0) {
		DoUnjailCommand((CItem *)player);
		return;
	}

	// .release [location]
	if (strncmp(cmd, "release", 7) == 0) {
		if (cmd[7] == '\0' || cmd[7] != ' ') {
			DoReleaseCommand((CItem *)player, NULL);
		} else {
			DoReleaseCommand((CItem *)player, cmd + 8);
		}
		return;
	}

	if (strcmp(cmd, "where") == 0) {
		ShowEntityLocation((CItem *)player);
		return;
	}

	if (strcmp(cmd, "who") == 0) {
		CHelpQueue_Who(q, player);
		return;
	}

	if (strcmp(cmd, "showids") == 0) {
		ShowIds((CItem *)player);
		return;
	}

	// Custom: .nearby - dump entities in nearby blocks
	if (strcmp(cmd, "nearby") == 0 && CPlayer_IsEditing(player)) {
		CLocation *ploc = CEntity_GetLocation(&player->mobile.container.item.resourceEntity.entity);
		int blockBuf[0x100];
		int bi, count = 0;
		char msg[128];
		CBlockManager_GetNearbyBlocks(&g_SpatialGrid, ploc, 0x13, blockBuf, 0x100);
		for (bi = 0; blockBuf[bi] != -1; bi++) {
			CBlock *blk = &g_SpatialGrid.cells[blockBuf[bi]];
			CItem *ent;
			for (ent = blk->itemHead; ent != NULL; ent = ent->spatialNext) {
				if (ent == (CItem *)player)
					continue;
				int dist = CLocation_ChebyshevDistance(ploc, CEntity_GetLocation(&ent->resourceEntity.entity));
				if (dist > 0x12)
					continue;
				CLocation *eloc = CEntity_GetLocation(&ent->resourceEntity.entity);
				int isMob = VT_IsMobile(ent);
				snprintf(msg, sizeof(msg), "0x%08X d=%d at (%d,%d,%d) body=0x%04X mob=%d rmv=%d", ent->serial, dist, (int)(int16_t)eloc->x, (int)(int16_t)eloc->y,
				        (int)eloc->z, ent->resourceEntity.entity.bodyType, isMob, ent->resourceEntity.entity.removedFromWorld);
				CPlayer_SystemMessage(player, msg);
				count++;
				if (count >= 20)
					break;
			}
			if (count >= 20)
				break;
		}
		snprintf(msg, sizeof(msg), "Total nearby: %d entities", count);
		CPlayer_SystemMessage(player, msg);
		return;
	}

	// Custom: .create GRAPHIC|name [count] - create item in player's backpack
	if (strncmp(cmd, "create ", 7) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 7;
		int graphic = -1;
		int count = 0;
		if (isdigit((unsigned char)arg[0]) || arg[0] == '-' || (arg[0] == '0' && arg[1] == 'x')) {
			sscanf(arg, "%i", &graphic);
			// Look for optional count after the number
			const char *sp = strchr(arg, ' ');
			if (sp != NULL)
				sscanf(sp + 1, "%i", &count);
		} else {
			// Meta-items: composite items with
			// special creation logic.
			if (strcasecmp(arg, "reagentbag") == 0) {
				CItem *bp = player->mobile.equipment[21];
				if (bp == NULL) {
					CPlayer_SystemMessage(player, "No backpack");
					return;
				}
				GM_CreateReagentBag(player, bp);
				return;
			}
			if (strcasecmp(arg, "runebag") == 0) {
				CItem *bp = player->mobile.equipment[21];
				if (bp == NULL) {
					CPlayer_SystemMessage(player, "No backpack");
					return;
				}
				GM_CreateRuneBag(player, bp);
				return;
			}
			// Find end of name (before optional count)
			const char *sp = strrchr(arg, ' ');
			if (sp != NULL) {
				// Check if last token is a number
				const char *tok = sp + 1;
				if (isdigit((unsigned char)tok[0])) {
					sscanf(tok, "%i", &count);
					// Lookup name without the count
					char namebuf[64];
					int namelen = (int)(sp - arg);
					if (namelen >= (int)sizeof(namebuf))
						namelen = (int)sizeof(namebuf) - 1;
					memcpy(namebuf, arg, namelen);
					namebuf[namelen] = '\0';
					graphic = GMNameEntry_Lookup(gm_item_names, GM_ITEM_NAMES_COUNT, namebuf);
				} else {
					graphic = GMNameEntry_Lookup(gm_item_names, GM_ITEM_NAMES_COUNT, arg);
				}
			} else {
				graphic = GMNameEntry_Lookup(gm_item_names, GM_ITEM_NAMES_COUNT, arg);
			}
		}
		if (graphic < 0) {
			CPlayer_SystemMessage(player, "Item not found");
			return;
		}
		CItem *backpack = player->mobile.equipment[21];
		if (backpack == NULL) {
			CPlayer_SystemMessage(player, "No backpack");
			return;
		}
		uint32_t newSerial;
		if (count > 1 && (CWorld_GetItemTileFlags((uint16_t)graphic) & TF_STACKABLE)) {
			// Stackable with count: inline create with
			// resource scaling before AddToContainer
			// so the merge picks up the full amount.
			CItem *ci = CWorld_CreateItem(g_World, (uint16_t)graphic);
			newSerial = 0;
			if (ci != NULL) {
				CLocation tl;
				CLocation_Init(&tl);
				CLocation_Set(&tl, -1, -1, 0);
				CItem_Setup(ci, 2, &player->mobile.container.item.resourceEntity.entity.location, 0, 1);
				ci->resourceEntity.entity.color = g_ItemTileData[graphic].value2;
				if (count > 65535)
					count = 65535;
				CResourceNode *rn;
				rn = ci->resourceEntity.firstChild;
				while (rn != NULL) {
					if (rn->type == 3) {
						rn->value1 *= count;
						rn->value3 *= count;
					}
					rn = rn->next;
				}
				newSerial = ci->serial;
				((void (*)(void *, CItem *, CLocation *))VT_FN(ci, VT_ADD_TO_CONTAINER))(ci, backpack, &tl);
				// Match Script_createGlobalObjectIn:
				// verify item survived merge, then
				// ValidateInWorld to attach scripts.
				if (CWorld_FindBySerial(g_World, newSerial) == ci) {
					if (!ValidateInWorld(ci)) {
						newSerial = 0;
					}
				}
			}
		} else {
			newSerial = Script_createGlobalObjectIn(graphic, backpack->serial);
		}
		if (newSerial != 0) {
			const char *iname = GMNameEntry_ReverseLookup(gm_item_names, GM_ITEM_NAMES_COUNT, graphic);
			char createMsg[120];
			if (count > 1) {
				snprintf(createMsg, sizeof(createMsg), "Created %d %s (0x%04X) serial=0x%08X", count, iname ? iname : "?", graphic, newSerial);
			} else {
				snprintf(createMsg, sizeof(createMsg), "Created %s (0x%04X) serial=0x%08X", iname ? iname : "?", graphic, newSerial);
			}
			CPlayer_SystemMessage(player, createMsg);
		} else {
			CPlayer_SystemMessage(player, "Failed to create item");
		}
		return;
	}

	// Custom: .createat graphic x y [z] - create item at world location
	if (strncmp(cmd, "createat ", 9) == 0 && CPlayer_IsEditing(player)) {
		int graphic = -1;
		int cx = 0, cy = 0, cz = 0;
		const char *arg = cmd + 9;
		int nargs = sscanf(arg, "%i %i %i %i", &graphic, &cx, &cy, &cz);
		if (nargs < 3 || graphic < 0 || graphic > 0x4000) {
			CPlayer_SystemMessage(player, "Usage: .createat graphic x y [z]");
			return;
		}
		CLocation loc;
		CLocation_Init(&loc);
		CLocation_Set(&loc, (uint16_t)cx, (uint16_t)cy, (int8_t)cz);
		uint32_t newSerial = Script_createGlobalObjectAt(graphic, &loc);
		if (newSerial != 0) {
			char msg[120];
			snprintf(msg, sizeof(msg),
			        "Created 0x%04X at (%d,%d,%d) "
			        "serial=0x%08X",
			        graphic, cx, cy, cz, newSerial);
			CPlayer_SystemMessage(player, msg);
		} else {
			CPlayer_SystemMessage(player, "Failed to create item");
		}
		return;
	}

	// Custom: .spawnfk ID FAME KARMA - spawn NPC with fame/karma
	if (strncmp(cmd, "spawnfk ", 8) == 0 && CPlayer_IsEditing(player)) {
		int templateId = -1, spFame = 0, spKarma = 0;
		if (sscanf(cmd + 8, "%d %d %d", &templateId, &spFame, &spKarma) == 3 && templateId >= 0 && templateId < TEMPLATE_CHAIN_SIZE) {
			NPCTemplate *tmpl = CResManager_GetTemplateByID((uint16_t)templateId);
			if (tmpl == NULL) {
				CPlayer_SystemMessage(player, "Template not found");
				return;
			}
			g_PendingSpawnTemplate = templateId;
			g_PendingSpawnFame = (int16_t)spFame;
			g_PendingSpawnKarma = (int16_t)spKarma;
			uint8_t tbuf[20];
			player->targetCallback = GM_TargetSpawnNPC;
			PacketManager_MakePacket_TARGET(tbuf, 1, 0, 0);
			SendPacketToPlayer(player, tbuf, -1);
			CPlayer_SystemMessage(player, "Select location");
		} else {
			CPlayer_SystemMessage(player, ".spawnfk ID FAME KARMA");
		}
		return;
	}

	// Custom: .spawn ID|name - spawn NPC at target location
	if (strncmp(cmd, "spawn ", 6) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 6;
		int templateId = -1;
		if (isdigit((unsigned char)arg[0]) || arg[0] == '-' || (arg[0] == '0' && arg[1] == 'x')) {
			if (sscanf(arg, "%i", &templateId) != 1 || templateId < 0 || templateId >= TEMPLATE_CHAIN_SIZE)
				templateId = -1;
		} else {
			// Exact match on runtime names
			int i;
			for (i = 0; i < TEMPLATE_CHAIN_SIZE; i++) {
				if (g_TemplateNames[i] != NULL && strcasecmp(g_TemplateNames[i], arg) == 0) {
					templateId = i;
					break;
				}
			}
			// Fallback: prefix match via static table
			if (templateId < 0)
				templateId = GMNameEntry_Lookup(gm_template_names, GM_TEMPLATE_NAMES_COUNT, arg);
		}
		if (templateId < 0) {
			CPlayer_SystemMessage(player, "Template not found");
			return;
		}
		NPCTemplate *tmpl = CResManager_GetTemplateByID((uint16_t)templateId);
		if (tmpl == NULL) {
			CPlayer_SystemMessage(player, "Template not found");
			return;
		}
		g_PendingSpawnTemplate = templateId;
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetSpawnNPC;
		PacketManager_MakePacket_TARGET(tbuf, 1, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		char msg[80];
		snprintf(msg, sizeof(msg), "Select location to spawn %s (#%d)", g_TemplateNames[templateId] ? g_TemplateNames[templateId] : "?", templateId);
		CPlayer_SystemMessage(player, msg);
		return;
	}

	// Custom: .morph - show current body
	// Custom: .morph list - list creature body names from gm_body_names
	// Custom: .morph <id|name|off> - change player body indefinitely (polymorph)
	// Names resolve through gm_body_names, generated from templatestable.dat
	// with body defines expanded via bank/defines. Numeric IDs are taken as
	// bodyType directly. off|none|0 reverts to the player's natural body
	// (0x190 + sex). Bodies are deduplicated by name, so multiple entries
	// can share the same human-readable label (e.g. dragon = 12 or 59).
	if (strcmp(cmd, "morph list") == 0 && CPlayer_IsEditing(player)) {
		char line[256];
		int pos = 0;
		int i, j;
		CPlayer_SystemMessage(player, "Morph names:");
		for (i = 0; i < (int)GM_BODY_NAMES_COUNT; i++) {
			const char *nm = gm_body_names[i].name;
			int dup = 0;
			for (j = 0; j < i; j++) {
				if (strcasecmp(gm_body_names[j].name, nm) == 0) {
					dup = 1;
					break;
				}
			}
			if (dup)
				continue;
			int len = (int)strlen(nm);
			if (pos + len + 3 > (int)sizeof(line)) {
				CPlayer_SystemMessage(player, line);
				pos = 0;
			}
			if (pos > 0) {
				line[pos++] = ',';
				line[pos++] = ' ';
			}
			memcpy(line + pos, nm, len);
			pos += len;
			line[pos] = '\0';
		}
		if (pos > 0)
			CPlayer_SystemMessage(player, line);
		return;
	}
	if (strcmp(cmd, "morph") == 0 && CPlayer_IsEditing(player)) {
		char msg[120];
		snprintf(msg, sizeof(msg), "Body=0x%04X. Usage: .morph <id|name|off>", player->mobile.container.item.resourceEntity.entity.bodyType);
		CPlayer_SystemMessage(player, msg);
		return;
	}
	if (strncmp(cmd, "morph ", 6) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 6;
		int bodyType = -1;
		char msg[120];

		if (strcasecmp(arg, "off") == 0 || strcasecmp(arg, "none") == 0 || strcmp(arg, "0") == 0) {
			bodyType = (int)player->mobile.sex + 0x190;
		} else if (isdigit((unsigned char)arg[0]) || arg[0] == '-' || (arg[0] == '0' && arg[1] == 'x')) {
			int n;
			if (sscanf(arg, "%i", &n) == 1 && n > 0 && n < 0x10000)
				bodyType = n;
		} else {
			// Multiple bodies can share a name (e.g. dragon = 12 or 59
			// because the DRAGONS define expanded to both). Collect every
			// exact match and pick one at random so the choice mirrors the
			// weighted random the binary uses when spawning the template.
			// Fall back to a prefix match if no exact match exists.
			int matches[GM_BODY_NAMES_COUNT];
			int nMatches = 0;
			int i;
			for (i = 0; i < (int)GM_BODY_NAMES_COUNT; i++) {
				if (strcasecmp(gm_body_names[i].name, arg) == 0)
					matches[nMatches++] = gm_body_names[i].id;
			}
			if (nMatches == 0) {
				int argLen = (int)strlen(arg);
				if (argLen >= 3) {
					for (i = 0; i < (int)GM_BODY_NAMES_COUNT; i++) {
						if (strncasecmp(gm_body_names[i].name, arg, argLen) == 0)
							matches[nMatches++] = gm_body_names[i].id;
					}
				}
			}
			if (nMatches > 0)
				bodyType = matches[GetRandomRange(0, nMatches - 1)];
		}

		if (bodyType < 0) {
			CPlayer_SystemMessage(player, "Body not found");
			return;
		}

		CItem *self = (CItem *)&player->mobile;
		CEntity_SetBodyType(self, (uint16_t)bodyType);
		if (!self->resourceEntity.entity.removedFromWorld) {
			((void (*)(void *))VT_FN(self, VT_HIDE))(self);
			((void (*)(void *))VT_FN(self, VT_RETURN_TO_TRACKED))(self);
		}
		snprintf(msg, sizeof(msg), "Body set to 0x%04X", (uint16_t)bodyType);
		CPlayer_SystemMessage(player, msg);
		return;
	}

	// Custom: .fillspellbook - click spellbook to fill with all spells
	if (strcmp(cmd, "fillspellbook") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetFillSpellbook;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select spellbook to fill");
		return;
	}

	// Custom: .go list - list all named locations
	if (strcmp(cmd, "go list") == 0 && CPlayer_IsEditing(player)) {
		char line[256];
		int pos = 0;
		int i;
		CPlayer_SystemMessage(player, "Named locations:");
		for (i = 0; g_GoLocations[i].name != NULL; i++) {
			int len = (int)strlen(g_GoLocations[i].name);
			if (pos + len + 2 > (int)sizeof(line)) {
				CPlayer_SystemMessage(player, line);
				pos = 0;
			}
			if (pos > 0)
				line[pos++] = ' ';
			memcpy(line + pos, g_GoLocations[i].name, len);
			pos += len;
			line[pos] = '\0';
		}
		if (pos > 0)
			CPlayer_SystemMessage(player, line);
		return;
	}

	// Custom: .go X Y [Z] | .go <name> - GM coordinate/named teleport
	if (strncmp(cmd, "go ", 3) == 0 && CPlayer_IsEditing(player)) {
		int gx, gy;
		int gz = 0;
		int found = 0;
		int gz_explicit = 0;
		const char *arg = cmd + 3;

		// Try named location first
		if (!isdigit((unsigned char)arg[0]) && arg[0] != '-') {
			int i;
			for (i = 0; g_GoLocations[i].name != NULL; i++) {
				if (strcasecmp(g_GoLocations[i].name, arg) == 0) {
					gx = g_GoLocations[i].x;
					gy = g_GoLocations[i].y;
					gz = g_GoLocations[i].z;
					found = 1;
					gz_explicit = 1;
					break;
				}
			}
			if (!found) {
				CPlayer_SystemMessage(player, "Unknown location");
				return;
			}
		} else {
			int n = sscanf(arg, "%d %d %d", &gx, &gy, &gz);
			if (n >= 2) {
				found = 1;
				if (n >= 3)
					gz_explicit = 1;
			}
		}

		if (found) {
			if (CBlockManager_IsValidCoord(&g_SpatialGrid, gx, gy)) {
				CLocation goLoc;

				if (!gz_explicit) {
					int blockIdx;
					blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, gx, gy, 0);
					if (blockIdx >= 0)
						gz = (int)g_MapBlocks[blockIdx].cells[(gy & 7) * 8 + (gx & 7)].z;
				}

				CLocation_Init(&goLoc);
				CLocation_Set(&goLoc, (int16_t)gx, (int16_t)gy, (int16_t)gz);

				// Teleport: hide + drop at new location
				((void (*)(void *))VT_FN((CItem *)player, VT_HIDE))((CItem *)player);
				((void (*)(void *, CLocation *))VT_FN((CItem *)player, VT_DROP_AT_FEET))((CItem *)player, &goLoc);

				// Send nearby entities and wake sleeping NPCs
				CMobile_NotifyNearbyPlayers((CItem *)player);

				char goMsg[80];
				snprintf(goMsg, sizeof(goMsg), "Teleported to %s (%d %d %d)", arg, gx, gy,
				        (int)(int16_t)player->mobile.container.item.resourceEntity.entity.location.z);
				CPlayer_SystemMessage(player, goMsg);
			} else {
				CPlayer_SystemMessage(player, "Invalid coordinates");
			}
			return;
		}
	}

	// Custom: .set <stat|skill> [VALUE]
	// Stats: str|dex|int [1-200] (sets base stat + max + current)
	//        hits|mana|stam [0-max] (sets current only, no value = refill)
	// Skills: <name|ID|all|list> [0-1000] (by skill name or ID)
	if (strncmp(cmd, "set ", 4) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 4;
		char smsg[80];

		int sType, sSkillId, sVal, sHasVal;
		char sStrArg[64];
		GM_ParseSetArgs(arg, &sType, &sSkillId, &sVal, &sHasVal, sStrArg, sizeof(sStrArg));

		if (sType >= 0) {
			GM_ApplySet((CItem *)&player->mobile, sType, sSkillId, sVal, sHasVal, sStrArg, smsg, sizeof(smsg));
			CPlayer_SystemMessage(player, smsg);
			return;
		}

		// Special: .set list - list all skill names
		const char *p = arg;
		while (*p && *p != ' ')
			p++;
		int nlen = (int)(p - arg);
		if (nlen == 4 && strncasecmp(arg, "list", 4) == 0) {
			char line[256];
			int pos = 0;
			int i;
			CPlayer_SystemMessage(player, "Skills:");
			for (i = 0; i < MAX_SKILLS; i++) {
				const char *sn = CSkillManager_GetSkillName(&g_SkillManager, (int8_t)i);
				if (!sn)
					continue;
				char entry[100];
				int elen = snprintf(entry, sizeof(entry), "%d=%s", i, sn);
				if (pos + elen + 2 > (int)sizeof(line)) {
					CPlayer_SystemMessage(player, line);
					pos = 0;
				}
				if (pos > 0)
					line[pos++] = ' ';
				memcpy(line + pos, entry, elen);
				pos += elen;
				line[pos] = '\0';
			}
			if (pos > 0)
				CPlayer_SystemMessage(player, line);
			return;
		}

		CPlayer_SystemMessage(player, ".set <stat|skill|all|list> [VALUE]");
		return;
	}

	// Custom: .settarget <stat|skill> [VALUE] - set stat/skill on target
	if (strncmp(cmd, "settarget ", 10) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 10;

		int sType, sSkillId, sVal, sHasVal;
		char sStrArg[64];
		GM_ParseSetArgs(arg, &sType, &sSkillId, &sVal, &sHasVal, sStrArg, sizeof(sStrArg));

		if (sType < 0) {
			CPlayer_SystemMessage(player, ".settarget <stat|skill|all> [VALUE]");
			return;
		}

		g_PendingSet.type = sType;
		g_PendingSet.skillId = sSkillId;
		g_PendingSet.value = sVal;
		g_PendingSet.hasValue = sHasVal;
		strncpy(g_PendingSet.strArg, sStrArg, sizeof(g_PendingSet.strArg) - 1);
		g_PendingSet.strArg[sizeof(g_PendingSet.strArg) - 1] = '\0';

		uint8_t tbuf[20];
		player->targetCallback = GM_TargetSet;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target");
		return;
	}

	// Custom: .setmob SERIAL STAT VALUE - set stat on mob by serial
	if (strncmp(cmd, "setmob ", 7) == 0 && CPlayer_IsEditing(player)) {
		uint32_t targetSerial;
		char statName[20];
		int statVal;
		if (sscanf(cmd + 7, "0x%x %19s %d", &targetSerial, statName, &statVal) == 3 || sscanf(cmd + 7, "%u %19s %d", &targetSerial, statName, &statVal) == 3) {
			CItem *target = CWorld_FindBySerial(g_World, targetSerial);
			if (target == NULL || !VT_IsMobile(target)) {
				CPlayer_SystemMessage(player, "Not a mobile");
				return;
			}
			int sType, sSkillId, sVal, sHasVal;
			// For .setmob, statName and statVal are
			// already split. Rejoin for GM_ParseSetArgs.
			char setArg[40];
			char sStrArg[64];
			snprintf(setArg, sizeof(setArg), "%s %d", statName, statVal);
			GM_ParseSetArgs(setArg, &sType, &sSkillId, &sVal, &sHasVal, sStrArg, sizeof(sStrArg));
			USED(sHasVal);
			if (sType < 0) {
				CPlayer_SystemMessage(player, "Unknown stat");
				return;
			}
			char msg[80];
			GM_ApplySet(target, sType, sSkillId, sVal, 1, sStrArg, msg, sizeof(msg));
			char fullMsg[128];
			snprintf(fullMsg, sizeof(fullMsg), "[0x%08X] %s", targetSerial, msg);
			CPlayer_SystemMessage(player, fullMsg);
		} else {
			CPlayer_SystemMessage(player, ".setmob SERIAL STAT VALUE");
		}
		return;
	}

	// Custom: .hoardprime NPC TARGET [RESTYPE_NAME] - prime an NPC's
	// PURSE_DESIRES state with a desire target. The NPC must already
	// be a hoarder-tagged creature (e.g. dragon, thief). TARGET is the
	// target item OR a mobile (player). RESTYPE_NAME defaults to "gold"
	// and resolves through CResourceTypeManager_FindByName - pass
	// "jewels", "magic" etc. to test non-gold desires. This shortcut
	// lets ecology tests exercise the hoarder-drop and player-pursuit
	// paths without waiting on the sometimes-unreliable SEEK_DESIRES
	// scan. After calling this, .aistate 4 transitions the NPC to
	// PURSE_DESIRES so the handler walks to the target. A mobile
	// target auto-sets resourceAITarget = NPC_RESTGT_MOBILE (1) so
	// PurseDesiresHandler routes to PurseDesiresPlayer; an item target
	// sets resourceAITarget = NPC_RESTGT_ITEM (2).
	if (strncmp(cmd, "hoardprime ", 11) == 0 && CPlayer_IsEditing(player)) {
		uint32_t npcSerial, targSerial;
		char restypeName[64] = "gold";
		int n;
		n = sscanf(cmd + 11, "0x%x 0x%x %63s", &npcSerial, &targSerial, restypeName);
		if (n < 2)
			n = sscanf(cmd + 11, "%u %u %63s", &npcSerial, &targSerial, restypeName);
		if (n < 2) {
			CPlayer_SystemMessage(player, ".hoardprime NPC_SERIAL TARGET_SERIAL [RESTYPE]");
			return;
		}
		CItem *npcItem = CWorld_FindBySerial(g_World, npcSerial);
		CItem *targItem = CWorld_FindBySerial(g_World, targSerial);
		if (npcItem == NULL || !VT_IsNPC(npcItem)) {
			CPlayer_SystemMessage(player, "hoardprime: NPC not found or not an NPC");
			return;
		}
		if (targItem == NULL) {
			CPlayer_SystemMessage(player, "hoardprime: target not found");
			return;
		}
		CResourceType *rt = CResourceTypeManager_FindByName(restypeName);
		if (rt == NULL) {
			char emsg[160];
			snprintf(emsg, sizeof(emsg), "hoardprime: unknown resource %s", restypeName);
			CPlayer_SystemMessage(player, emsg);
			return;
		}
		CNPC *npc = (CNPC *)npcItem;
		npc->resourceTargetSerial = targSerial;
		npc->resourceType = (uint8_t)rt->typeId;
		npc->resourceRate = 10;
		npc->resourceAITarget = VT_IsMobile(targItem) ? 1 : 2;
		CLocation_SetLoc(&npc->patrolTarget, &targItem->resourceEntity.entity.location);
		npc->isWalking = 1;
		char msg[160];
		snprintf(msg, sizeof(msg), "hoardprime: 0x%08X -> %s 0x%08X (rtype=%s/%d) at (%d,%d)", npcSerial, npc->resourceAITarget == 1 ? "mobile" : "item", targSerial,
		        restypeName, rt->typeId, (int)(int16_t)targItem->resourceEntity.entity.location.x, (int)(int16_t)targItem->resourceEntity.entity.location.y);
		CPlayer_SystemMessage(player, msg);
		return;
	}

	// Custom: .aversionprime NPC_SERIAL THREAT_SERIAL - prime an NPC into
	// RUNAWAY with a specific threat. SEEK_DESIRES's spatial scan for
	// aversion candidates is sometimes unreliable against freshly spawned
	// test entities, so this shortcut lets ecology tests exercise the
	// RunawayTick fleeing behavior directly. Sets actionTarget to the
	// threat serial, clears isWalking and scanTimer, and transitions to
	// NPC_STATE_RUNAWAY (7).
	if (strncmp(cmd, "aversionprime ", 14) == 0 && CPlayer_IsEditing(player)) {
		uint32_t npcSerial, threatSerial;
		int n;
		n = sscanf(cmd + 14, "0x%x 0x%x", &npcSerial, &threatSerial);
		if (n != 2)
			n = sscanf(cmd + 14, "%u %u", &npcSerial, &threatSerial);
		if (n != 2) {
			CPlayer_SystemMessage(player, ".aversionprime NPC_SERIAL THREAT_SERIAL");
			return;
		}
		CItem *npcItem = CWorld_FindBySerial(g_World, npcSerial);
		CItem *threatItem = CWorld_FindBySerial(g_World, threatSerial);
		if (npcItem == NULL || !VT_IsNPC(npcItem)) {
			CPlayer_SystemMessage(player, "aversionprime: NPC not found or not an NPC");
			return;
		}
		if (threatItem == NULL) {
			CPlayer_SystemMessage(player, "aversionprime: threat not found");
			return;
		}
		CNPC *npc = (CNPC *)npcItem;
		npc->actionTarget = threatSerial;
		npc->isWalking = 0;
		npc->scanTimer = 0;
		npc->aiState = 7;
		char msg[120];
		snprintf(msg, sizeof(msg), "aversionprime: 0x%08X fleeing 0x%08X at (%d,%d)", npcSerial, threatSerial, (int)(int16_t)threatItem->resourceEntity.entity.location.x,
		        (int)(int16_t)threatItem->resourceEntity.entity.location.y);
		CPlayer_SystemMessage(player, msg);
		return;
	}

	// Custom: .rename NAME - target a mobile and rename it
	if (strncmp(cmd, "rename ", 7) == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		strncpy(g_PendingRenameBuf, cmd + 7, 29);
		g_PendingRenameBuf[29] = '\0';
		player->targetCallback = GM_TargetRename;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to rename");
		return;
	}

	// Custom: .say TEXT - make target entity speak
	if (strncmp(cmd, "say ", 4) == 0 && CPlayer_IsEditing(player)) {
		strncpy(g_PendingSayBuf, cmd + 4, sizeof(g_PendingSayBuf) - 1);
		g_PendingSayBuf[sizeof(g_PendingSayBuf) - 1] = '\0';
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetSay;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to speak");
		return;
	}

	// Custom: .walk - make NPC walk to destination (2-step target)
	if (strcmp(cmd, "walk") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetWalkNPC;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select NPC to walk");
		return;
	}

	// Custom: .attack - make NPC attack a target (2-step target)
	if (strcmp(cmd, "attack") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetAttackNPC;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select NPC attacker");
		return;
	}

	// Custom: .kill - target a mobile and kill it
	if (strcmp(cmd, "kill") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetKill;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to kill");
		return;
	}

	// Custom: .mkill - repeating click-to-kill (press Esc to stop)
	if (strcmp(cmd, "mkill") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetMKill;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to kill (Esc to stop)");
		return;
	}

	// Custom: .mremove - repeating click-to-remove (press Esc to stop)
	if (strcmp(cmd, "mremove") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetMRemove;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to remove (Esc to stop)");
		return;
	}

	// Custom: .remove - target an entity and delete it
	if (strcmp(cmd, "remove") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetRemove;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to remove");
		return;
	}

	// Custom: .remove SERIAL - inline form of .remove that takes the
	// target serial directly, skipping the target-cursor round trip.
	// Needed under heavy slow-suite load where the cursor flow is
	// unreliable (combat_pvm bow equip in v30 was blocked by a starter
	// shield on LAYER_LEFT_HAND that pickup/drop could not move).
	if (strncmp(cmd, "remove ", 7) == 0 && CPlayer_IsEditing(player)) {
		uint32_t targetSerial;
		if (sscanf(cmd + 7, "0x%x", &targetSerial) == 1 || sscanf(cmd + 7, "%u", &targetSerial) == 1) {
			GM_TargetRemove(player, 0, targetSerial, 0, 0, 0);
		} else {
			CPlayer_SystemMessage(player, ".remove SERIAL");
		}
		return;
	}

	// Custom: .tele - click-to-teleport via targeting cursor
	if (strcmp(cmd, "tele") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetTele;
		PacketManager_MakePacket_TARGET(tbuf, 1, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select destination");
		return;
	}

	// Custom: .mtele - repeating click-to-teleport (press Esc to stop)
	if (strcmp(cmd, "mtele") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetMTele;
		PacketManager_MakePacket_TARGET(tbuf, 1, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select destination (Esc to stop)");
		return;
	}

	// Custom: .info - target an entity and dump its details
	if (strcmp(cmd, "info") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetInfo;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to inspect");
		return;
	}

	// Custom: .scripts - target an entity and list its scripts and objvars
	if (strcmp(cmd, "scripts") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetScripts;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to inspect scripts");
		return;
	}

	// Custom: .resources - target an entity and list its resource nodes
	if (strcmp(cmd, "resources") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetResources;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to inspect resources");
		return;
	}

	// Custom: .dumpnpc SERIAL - inline form of .resources that takes the
	// target serial directly, skipping the target-cursor round trip. The
	// cursor flow is fragile under sustained slow-suite load (60k+ NPCs,
	// 500-700ms frame times) where the TARGET packet can be queued past
	// any reasonable client timeout. ecology_hunger Test 1's stomach poll
	// in v25/v29 hit "stomach=None" for every iteration through a 50min
	// budget when the cursor never arrived inside the 15s wait.
	if (strncmp(cmd, "dumpnpc ", 8) == 0 && CPlayer_IsEditing(player)) {
		uint32_t targetSerial;
		if (sscanf(cmd + 8, "0x%x", &targetSerial) == 1 || sscanf(cmd + 8, "%u", &targetSerial) == 1) {
			GM_TargetResources(player, 0, targetSerial, 0, 0, 0);
		} else {
			CPlayer_SystemMessage(player, ".dumpnpc SERIAL");
		}
		return;
	}

	// Custom: .resbank NAME - report region->quantities[typeid(NAME)]
	// at the player's location. Used by the closed-economy test to
	// verify harvest credits the bank under FEAT_CLOSED_ECONOMY.
	if (strncmp(cmd, "resbank ", 8) == 0 && CPlayer_IsEditing(player)) {
		const char *resName = cmd + 8;
		CResourceType *rt = CResourceTypeManager_FindByName(resName);
		char rmsg[256];
		if (rt == NULL) {
			snprintf(rmsg, sizeof(rmsg), "ResBank: unknown resource %s", resName);
			CPlayer_SystemMessage(player, rmsg);
			return;
		}
		CLocation *ploc = &player->mobile.container.item.resourceEntity.entity.location;
		CResBankRegion *region = CResBankManager_GetRegionByLocation(ploc->x, ploc->y);
		if (region == NULL || region == g_ResBankManager.noRegion) {
			snprintf(rmsg, sizeof(rmsg), "ResBank name=%s id=%d region=none", resName, rt->typeId);
			CPlayer_SystemMessage(player, rmsg);
			return;
		}
		snprintf(rmsg, sizeof(rmsg), "ResBank name=%s id=%d region=%s qty=%d", resName, rt->typeId, region->name, (int)region->quantities[rt->typeId]);
		CPlayer_SystemMessage(player, rmsg);
		return;
	}

	// Custom: .worldresources - summarize every block's chunk-egg node counts
	if (strcmp(cmd, "worldresources") == 0 && CPlayer_IsEditing(player)) {
		int totalBlocks = g_SpatialGrid.gridWidth * g_SpatialGrid.gridHeight;
		int blocksWithEgg = 0;
		int totals[4] = { 0, 0, 0, 0 };
		enum { WORLDRES_MAX_IDS = 64 };
		int desireIds[WORLDRES_MAX_IDS];
		int desireCounts[WORLDRES_MAX_IDS];
		int nDesireIds = 0;
		int bi;
		for (bi = 0; bi < totalBlocks; bi++) {
			CItem *egg = g_MapBlocks[bi].eggHead;
			if (egg == NULL)
				continue;
			blocksWithEgg++;
			CResourceNode *node;
			for (node = egg->resourceEntity.firstChild; node != NULL; node = node->next) {
				if (node->type < 4)
					totals[node->type]++;
				if (node->type == 3) {
					int i;
					int found = -1;
					for (i = 0; i < nDesireIds; i++) {
						if (desireIds[i] == node->id) {
							found = i;
							break;
						}
					}
					if (found < 0 && nDesireIds < WORLDRES_MAX_IDS) {
						desireIds[nDesireIds] = node->id;
						desireCounts[nDesireIds] = 0;
						found = nDesireIds;
						nDesireIds++;
					}
					if (found >= 0)
						desireCounts[found]++;
				}
			}
		}
		char wmsg[128];
		snprintf(wmsg, sizeof(wmsg), "WorldRes blocks=%d eggs=%d food=%d shelter=%d desire=%d prod=%d", totalBlocks, blocksWithEgg, totals[0], totals[1], totals[2],
		        totals[3]);
		CPlayer_SystemMessage(player, wmsg);
		int i;
		for (i = 0; i < nDesireIds; i++) {
			const char *label = "?";
			if (desireIds[i] < MAX_RESOURCE_TYPES && g_ResTypeEntries[desireIds[i]] != NULL)
				label = CResourceType_GetInternalName(g_ResTypeEntries[desireIds[i]]);
			snprintf(wmsg, sizeof(wmsg), "WorldResId id=%d blocks=%d %s", desireIds[i], desireCounts[i], label);
			CPlayer_SystemMessage(player, wmsg);
		}
		return;
	}

	// Custom: .blockresources - dump the chunk egg at the player's block
	if (strcmp(cmd, "blockresources") == 0 && CPlayer_IsEditing(player)) {
		CLocation *ploc = &player->mobile.container.item.resourceEntity.entity.location;
		int blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, ploc, 0);
		if (blockIdx < 0) {
			CPlayer_SystemMessage(player, "BlockResources: bad index");
			return;
		}
		CItem *egg = g_MapBlocks[blockIdx].eggHead;
		char bmsg[128];
		snprintf(bmsg, sizeof(bmsg), "BlockRes idx=%d egg=%s", blockIdx, egg != NULL ? "present" : "none");
		CPlayer_SystemMessage(player, bmsg);
		if (egg == NULL)
			return;
		int counts[4] = { 0, 0, 0, 0 };
		CResourceNode *node;
		for (node = egg->resourceEntity.firstChild; node != NULL; node = node->next) {
			if (node->type < 4)
				counts[node->type]++;
		}
		snprintf(bmsg, sizeof(bmsg), "ResCount food=%d shelter=%d desire=%d prod=%d", counts[0], counts[1], counts[2], counts[3]);
		CPlayer_SystemMessage(player, bmsg);
		for (node = egg->resourceEntity.firstChild; node != NULL; node = node->next) {
			const char *label = "?";
			if (node->id < MAX_RESOURCE_TYPES && g_ResTypeEntries[node->id] != NULL)
				label = CResourceType_GetInternalName(g_ResTypeEntries[node->id]);
			snprintf(bmsg, sizeof(bmsg), "ResNode t=%d id=%d v1=%d v2=%d v3=%d %s", (int)node->type, (int)node->id, node->value1, node->value2, node->value3, label);
			CPlayer_SystemMessage(player, bmsg);
		}
		return;
	}

	// Custom: .aistate N - force an NPC's aiState to N
	if (strncmp(cmd, "aistate ", 8) == 0 && CPlayer_IsEditing(player)) {
		g_PendingAIState = atoi(cmd + 8);
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetAIState;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		char hint[64];
		snprintf(hint, sizeof(hint), "Select NPC to set aiState=%d", g_PendingAIState);
		CPlayer_SystemMessage(player, hint);
		return;
	}

	// Custom: .itemhp [0xSERIAL] [VALUE] - get/set weapon or armor curHP
	// With a leading 0x... serial, acts directly; otherwise opens a
	// target cursor and stages VALUE (or -1 to query) for GM_TargetItemHP.
	if ((strcmp(cmd, "itemhp") == 0 || strncmp(cmd, "itemhp ", 7) == 0) && CPlayer_IsEditing(player)) {
		const char *args = (cmd[6] == ' ') ? cmd + 7 : "";
		uint32_t tgtSerial;
		int newVal = -1;
		if (sscanf(args, "0x%x %d", &tgtSerial, &newVal) >= 1) {
			CItem *tgt = CWorld_FindBySerial(g_World, tgtSerial);
			if (tgt == NULL) {
				CPlayer_SystemMessage(player, "Item not found");
				return;
			}
			if (CItem_GetWeaponDefId(tgt) == 0) {
				CPlayer_SystemMessage(player, "Not a weapon/armor");
				return;
			}
			if (newVal >= 0) {
				CWeapon_SetCurHP(tgt, (uint8_t)newVal);
				char buf[80];
				snprintf(buf, sizeof(buf), "Set HP: %d/%d", (int)CWeapon_GetCurHP(tgt), (int)CWeapon_GetMaxHP(tgt));
				CPlayer_SystemMessage(player, buf);
			} else {
				char buf[80];
				snprintf(buf, sizeof(buf), "HP: %d/%d", (int)CWeapon_GetCurHP(tgt), (int)CWeapon_GetMaxHP(tgt));
				CPlayer_SystemMessage(player, buf);
			}
			return;
		}
		// Bare form: optional VALUE, then open the target cursor.
		int bareVal = -1;
		sscanf(args, "%d", &bareVal);
		g_PendingItemHP = bareVal;
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetItemHP;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select weapon/armor");
		return;
	}

	// Custom: .durtest [0xSERIAL] - force one durability check (threshold=300)
	if ((strcmp(cmd, "durtest") == 0 || strncmp(cmd, "durtest ", 8) == 0) && CPlayer_IsEditing(player)) {
		const char *args = (cmd[7] == ' ') ? cmd + 8 : "";
		uint32_t tgtSerial;
		if (sscanf(args, "0x%x", &tgtSerial) == 1) {
			CItem *tgt = CWorld_FindBySerial(g_World, tgtSerial);
			if (tgt == NULL) {
				CPlayer_SystemMessage(player, "Item not found");
				return;
			}
			if (CItem_GetWeaponDefId(tgt) == 0) {
				CPlayer_SystemMessage(player, "Not a weapon/armor");
				return;
			}
			int result = CItem_DamageDurability(tgt, 300, 0, 0, 0, -1);
			char buf[80];
			snprintf(buf, sizeof(buf), "Durability: %d/%d (result=%d)", (int)CWeapon_GetCurHP(tgt), (int)CWeapon_GetMaxHP(tgt), result);
			CPlayer_SystemMessage(player, buf);
			if (result == 0) {
				CPlayer_SystemMessage(player, "Item destroyed");
				((void (*)(void *))VT_FN(tgt, VT_DELETE))(tgt);
			}
			return;
		}
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetDurtest;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select weapon/armor");
		return;
	}

	// Custom: .setnpcac - deprecated; covered by .settarget nac VALUE.
	if (strncmp(cmd, "setnpcac", 8) == 0 && CPlayer_IsEditing(player) && (cmd[8] == '\0' || cmd[8] == ' ')) {
		CPlayer_SystemMessage(player, "Deprecated: use .settarget nac VALUE");
		return;
	}

	// Custom: .npcinv [0xSERIAL] - list items in a mobile's container chain
	if ((strcmp(cmd, "npcinv") == 0 || strncmp(cmd, "npcinv ", 7) == 0) && CPlayer_IsEditing(player)) {
		const char *args = (cmd[6] == ' ') ? cmd + 7 : "";
		uint32_t tgtSerial;
		if (sscanf(args, "0x%x", &tgtSerial) == 1) {
			CItem *tgt = CWorld_FindBySerial(g_World, tgtSerial);
			if (tgt == NULL || !VT_IsMobile(tgt)) {
				CPlayer_SystemMessage(player, "Mobile not found");
				return;
			}
			CItem *child = ((CContainer *)tgt)->contents;
			int count = 0;
			char buf[160];
			while (child != NULL) {
				snprintf(buf, sizeof(buf), "  [%d] 0x%08X gfx=0x%04X", count, child->serial, (unsigned)CEntity_GetBodyType(child) & 0xFFFF);
				CPlayer_SystemMessage(player, buf);
				count++;
				child = child->spatialNext;
			}
			snprintf(buf, sizeof(buf), "npcinv 0x%08X: %d item(s)", tgtSerial, count);
			CPlayer_SystemMessage(player, buf);
			return;
		}
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetNpcInv;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select mobile");
		return;
	}

	// Custom: .foragemode [0xSERIAL] [0|1] - bias an NPC to roll
	// SEEK_DESIRES from IDLE every tick (a test aid for the unforced
	// ecology loop). With a leading 0x... serial, acts directly;
	// otherwise opens a target cursor. The optional trailing 0|1
	// disables or enables it (default enable).
	if ((strcmp(cmd, "foragemode") == 0 || strncmp(cmd, "foragemode ", 11) == 0) && CPlayer_IsEditing(player)) {
		const char *args = (cmd[10] == ' ') ? cmd + 11 : "";
		uint32_t tgtSerial;
		int onoff = 1;
		if (sscanf(args, "0x%x %d", &tgtSerial, &onoff) >= 1) {
			CItem *tgt = CWorld_FindBySerial(g_World, tgtSerial);
			if (tgt == NULL || !VT_IsNPC(tgt)) {
				CPlayer_SystemMessage(player, "foragemode: NPC not found");
				return;
			}
			GM_ApplyForageMode(player, tgt, onoff);
			return;
		}
		int bareOnoff = 1;
		sscanf(args, "%d", &bareOnoff);
		g_PendingForageMode = bareOnoff;
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetForageMode;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select NPC for foragemode");
		return;
	}

	// Custom: .bank - open target mobile's bank box
	if (strcmp(cmd, "bank") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetBank;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select mobile to open bank");
		return;
	}

	// Custom: .invis - make target invisible (greyed out like spell)
	if (strcmp(cmd, "invis") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetInvis;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to hide");
		return;
	}

	// Custom: .vis - make target visible again
	if (strcmp(cmd, "vis") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetVis;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to reveal");
		return;
	}

	// Custom: .invuln - make target invulnerable
	if (strcmp(cmd, "invuln") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetInvuln;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target for invuln");
		return;
	}

	// Custom: .invuln SERIAL - inline form skipping the cursor flow.
	// Needed by ecology_hunger Test 1 to keep wilderness rabbits from
	// being killed by predators before stomach decay can be observed
	// over the multi-thousand-second budget.
	if (strncmp(cmd, "invuln ", 7) == 0 && CPlayer_IsEditing(player)) {
		uint32_t targetSerial;
		if (sscanf(cmd + 7, "0x%x", &targetSerial) == 1 || sscanf(cmd + 7, "%u", &targetSerial) == 1) {
			GM_TargetInvuln(player, 0, targetSerial, 0, 0, 0);
		} else {
			CPlayer_SystemMessage(player, ".invuln SERIAL");
		}
		return;
	}

	// Custom: .vuln - make target vulnerable
	if (strcmp(cmd, "vuln") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetVuln;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target for vuln");
		return;
	}

	// Custom: .light [0-30] - set a PERMANENT personal light level on a
	// targeted mobile (0 = off/clear, default 30 = full daylight). Calls
	// CMobile_SetLight with lightTime=0, which the per-mobile light decay in
	// CTimeManager_Update skips (it only decrements lightTime > 0), so it
	// never expires. Unlike the Night Sight spell, this bypasses the spell's
	// Q50G target gate, so it works on counselor/invulnerable GM characters.
	if (strncmp(cmd, "light", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ') && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		int val = 30;
		if (cmd[5] == ' ' && (sscanf(cmd + 6, "%d", &val) != 1 || val < 0 || val > 30)) {
			CPlayer_SystemMessage(player, "Usage: .light [0-30]  (0=off, default 30=full)");
			return;
		}
		g_PendingLightVal = (uint8_t)val;
		player->targetCallback = GM_TargetLight;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target for light");
		return;
	}

	// Custom: .speed N - set movement speed (1=normal, 3=medium, 5=fast)
	if (strncmp(cmd, "speed ", 6) == 0 && CPlayer_IsEditing(player)) {
		int spd;
		if (sscanf(cmd + 6, "%d", &spd) == 1 && (spd == 1 || spd == 3 || spd == 5)) {
			CMobile_SetMovementType(&player->mobile, (uint8_t)spd);
			const char *label = (spd == 1) ? "normal" : (spd == 3) ? "medium" : "fast";
			char smsg[80];
			snprintf(smsg, sizeof(smsg), "Speed set to %s (%d)", label, spd);
			CPlayer_SystemMessage(player, smsg);
		} else {
			CPlayer_SystemMessage(player, "Usage: .speed 1|3|5 (normal|medium|fast)");
		}
		return;
	}

	// Custom: .time H - set world clock hour (0-23)
	if (strncmp(cmd, "time ", 5) == 0 && CPlayer_IsEditing(player)) {
		int hour;
		if (sscanf(cmd + 5, "%d", &hour) == 1 && hour >= 0 && hour <= 23) {
			g_TimeManager.hour = (uint32_t)hour;
			g_TimeManager.minute = 0;
			g_TimeManager.seconds = 0;
			GlobalLightManager_Tick(0);
			char tmsg[80];
			snprintf(tmsg, sizeof(tmsg), "Time set to %d:00", hour);
			CPlayer_SystemMessage(player, tmsg);
		} else {
			CPlayer_SystemMessage(player, "Usage: .time HOUR (0-23)");
		}
		return;
	}

	// Custom: .resurrect - resurrect targeted dead player
	if (strcmp(cmd, "resurrect") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetResurrect;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select player to resurrect");
		return;
	}

	// Custom: .weather rain|storm|snow|clear
	if (strncmp(cmd, "weather ", 8) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 8;
		uint8_t wbuf[8];
		int wtype = -1;
		const char *label = arg;
		if (strcasecmp(arg, "rain") == 0)
			wtype = 0;
		else if (strcasecmp(arg, "storm") == 0)
			wtype = 1;
		else if (strcasecmp(arg, "snow") == 0)
			wtype = 2;
		else if (strcasecmp(arg, "clear") == 0)
			wtype = 0xFF;
		if (wtype < 0) {
			CPlayer_SystemMessage(player, "Usage: .weather rain|storm|snow|clear");
			return;
		}
		if (wtype == 0xFF) {
			PacketManager_MakePacket_WEATHERCHANGE(wbuf, 0xFF, 0, 0);
		} else {
			PacketManager_MakePacket_WEATHERCHANGE(wbuf, (uint8_t)wtype, 40, 20);
		}
		CPlayerList_SendPacketToAll(wbuf);
		char wmsg[80];
		snprintf(wmsg, sizeof(wmsg), "Weather set to %s", label);
		CPlayer_SystemMessage(player, wmsg);
		return;
	}

	// Custom: .save - force immediate world save
	if (strcmp(cmd, "save") == 0 && CPlayer_IsEditing(player)) {
		BackupFile(GLOBAL_file_dynidx0_mul, GLOBAL_file_dynidx0_bkp);
		BackupFile(GLOBAL_file_dynamic0_mul, GLOBAL_file_dynamic0_bkp);
		SaveDynamic0();
		// CUSTOM (FEAT_CLOSED_ECONOMY): a manual save persists the live bank too.
		SaveAll_ResBank();
		CPlayer_SystemMessage(player, "World saved");
		return;
	}

	// Custom: .hue ID|name - set hue on target entity
	if (strncmp(cmd, "hue ", 4) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 4;
		int hue = -1;
		if (strcmp(arg, "list") == 0) {
			CPlayer_SystemMessage(player, "Use .hue <number> or .hue <name>");
			CPlayer_SystemMessage(player, "Hue names are from hues.mul "
			                              "(e.g. partial match)");
			return;
		}
		if (isdigit((unsigned char)arg[0]) || arg[0] == '-' || (arg[0] == '0' && arg[1] == 'x')) {
			sscanf(arg, "%i", &hue);
		} else {
			hue = GMNameEntry_Lookup(gm_hue_names, GM_HUE_NAMES_COUNT, arg);
		}
		if (hue < 0 || hue > 0x3FF) {
			CPlayer_SystemMessage(player, "Hue not found (0-1023)");
			return;
		}
		g_PendingHue = hue;
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetHue;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		const char *hname = GMNameEntry_ReverseLookup(gm_hue_names, GM_HUE_NAMES_COUNT, hue);
		char hmsg[120];
		snprintf(hmsg, sizeof(hmsg), "Select target for hue %d (%s)", hue, hname ? hname : "?");
		CPlayer_SystemMessage(player, hmsg);
		return;
	}

	// Custom: .multi ID|name - place multi at target location
	if (strncmp(cmd, "multi ", 6) == 0 && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 6;
		int multiType = -1;
		if (strcmp(arg, "list") == 0) {
			int i;
			for (i = 0; i < (int)GM_MULTI_NAMES_COUNT; i++) {
				char line[120];
				snprintf(line, sizeof(line), "%d: %s", gm_multi_names[i].id, gm_multi_names[i].name);
				CPlayer_SystemMessage(player, line);
			}
			return;
		}
		if (isdigit((unsigned char)arg[0]) || arg[0] == '-' || (arg[0] == '0' && arg[1] == 'x')) {
			sscanf(arg, "%i", &multiType);
		} else {
			multiType = GMNameEntry_Lookup(gm_multi_names, GM_MULTI_NAMES_COUNT, arg);
		}
		if (multiType < 0) {
			CPlayer_SystemMessage(player, "Multi type not found "
			                              "(.multi list to see all)");
			return;
		}
		g_PendingMultiType = multiType;
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetMulti;
		PacketManager_MakePacket_TARGET(tbuf, 1, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		const char *mname = GMNameEntry_ReverseLookup(gm_multi_names, GM_MULTI_NAMES_COUNT, multiType);
		char mmsg[120];
		snprintf(mmsg, sizeof(mmsg), "Select location for %s (type %d)", mname ? mname : "?", multiType);
		CPlayer_SystemMessage(player, mmsg);
		return;
	}

	// Custom: .spell list - list all spells
	if (strcmp(cmd, "spell list") == 0 && CPlayer_IsEditing(player)) {
		int i;
		char line[256];
		int pos = 0;
		for (i = 0; i < (int)GM_SPELL_NAMES_COUNT; i++) {
			int sid = gm_spell_names[i].id;
			int circle = (sid - 1) / 8 + 1;
			char entry[80];
			int elen = snprintf(entry, sizeof(entry), "%d=%s(c%d)", sid, gm_spell_names[i].name, circle);
			if (pos + elen + 2 > (int)sizeof(line)) {
				CPlayer_SystemMessage(player, line);
				pos = 0;
			}
			if (pos > 0)
				line[pos++] = ' ';
			memcpy(line + pos, entry, elen);
			pos += elen;
			line[pos] = '\0';
		}
		if (pos > 0)
			CPlayer_SystemMessage(player, line);
		return;
	}

	// Custom: .freeze - freeze targeted mobile
	if (strcmp(cmd, "freeze") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetFreeze;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to freeze");
		return;
	}

	// Custom: .unfreeze - unfreeze targeted mobile
	if (strcmp(cmd, "unfreeze") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetUnfreeze;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to unfreeze");
		return;
	}

	// Custom: .mute - mute targeted mobile (squelch)
	if (strcmp(cmd, "mute") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetMute;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to mute");
		return;
	}

	// Custom: .unmute - unmute targeted mobile
	if (strcmp(cmd, "unmute") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetUnmute;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to unmute");
		return;
	}

	// Custom: .lock - lock targeted entity (container, door, etc.)
	if (strcmp(cmd, "lock") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetLock;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to lock");
		return;
	}

	// Custom: .unlock - unlock targeted entity (container, door, etc.)
	if (strcmp(cmd, "unlock") == 0 && CPlayer_IsEditing(player)) {
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetUnlock;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select target to unlock");
		return;
	}

	// Custom: .role [+/-flag ...] - show/toggle player flags
	// Gated on g_DebugGM so players can regain flags after clearing.
	if (strncmp(cmd, "role", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ') && g_DebugGM) {
		const char *arg = (cmd[4] == ' ') ? cmd + 5 : NULL;
		if (arg == NULL || *arg == '\0') {
			char rmsg[128];
			snprintf(rmsg, sizeof(rmsg), "godmode=%d gamemaster=%d counselor=%d", CPlayer_IsEditing(player) ? 1 : 0, CPlayer_IsGameMaster(player) ? 1 : 0,
			        CPlayer_IsCounselor(player) ? 1 : 0);
			CPlayer_SystemMessage(player, rmsg);
			CPlayer_SystemMessage(player, ".role +/-godmode|gamemaster|counselor");
		} else {
			const char *p = arg;
			while (*p != '\0') {
				while (*p == ' ')
					p++;
				if (*p == '\0')
					break;
				int set = 1;
				if (*p == '+') {
					set = 1;
					p++;
				} else if (*p == '-') {
					set = 0;
					p++;
				}
				const char *start = p;
				while (*p != '\0' && *p != ' ')
					p++;
				int len = (int)(p - start);
				if (len == 7 && strncasecmp(start, "godmode", 7) == 0) {
					if (set)
						CPlayer_EnableEditing(player);
					else
						CPlayer_DisableEditing(player);
				} else if (len == 10 && strncasecmp(start, "gamemaster", 10) == 0) {
					if (set)
						player->pflags |= PlayerIsGameMaster;
					else
						player->pflags &= ~(uint32_t)PlayerIsGameMaster;
				} else if (len == 9 && strncasecmp(start, "counselor", 9) == 0) {
					if (set)
						player->pflags |= PlayerIsCounselor;
					else
						player->pflags &= ~(uint32_t)PlayerIsCounselor;
				} else {
					char emsg[80];
					snprintf(emsg, sizeof(emsg), "Unknown flag: %.*s", len, start);
					CPlayer_SystemMessage(player, emsg);
					return;
				}
			}
			char rmsg[128];
			snprintf(rmsg, sizeof(rmsg), "godmode=%d gamemaster=%d counselor=%d", CPlayer_IsEditing(player) ? 1 : 0, CPlayer_IsGameMaster(player) ? 1 : 0,
			        CPlayer_IsCounselor(player) ? 1 : 0);
			CPlayer_SystemMessage(player, rmsg);
		}
		return;
	}

	// Custom: .lockitem [0xSERIAL] - attach lockdown script
	// Bare form opens a target cursor; serial form acts directly.
	if (strncmp(cmd, "lockitem", 8) == 0 && CPlayer_IsEditing(player) && (cmd[8] == '\0' || cmd[8] == ' ')) {
		uint32_t serial = 0;
		if (cmd[8] == ' ')
			serial = strtoul(cmd + 9, NULL, 0);
		if (serial != 0) {
			CItem *item = CWorld_FindBySerial(g_World, serial);
			if (item == NULL) {
				CPlayer_SystemMessage(player, "Item not found");
				return;
			}
			Entity_AttachScript(item, "lockdown", 0);
			CPlayer_SystemMessage(player, "Lockdown script attached");
			return;
		}
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetLockItem;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select item to lock");
		return;
	}

	// Custom: .attach [0xSERIAL] SCRIPT - attach script to entity
	// Bare form stages SCRIPT and opens a target cursor.
	if (strncmp(cmd, "attach ", 7) == 0 && CPlayer_IsEditing(player)) {
		const char *args = cmd + 7;
		uint32_t serial = 0;
		char scriptName[64];
		scriptName[0] = '\0';
		if (strncmp(args, "0x", 2) == 0 || strncmp(args, "0X", 2) == 0) {
			if (sscanf(args, "%x %63s", &serial, scriptName) < 2 || serial == 0 || scriptName[0] == '\0') {
				CPlayer_SystemMessage(player, "Usage: .attach [0xSERIAL] SCRIPT");
				return;
			}
			CItem *item = CWorld_FindBySerial(g_World, serial);
			if (item == NULL) {
				CPlayer_SystemMessage(player, "Item not found");
				return;
			}
			Entity_AttachScript(item, scriptName, 1);
			CPlayer_SystemMessage(player, "Script attached");
			return;
		}
		if (sscanf(args, "%63s", scriptName) != 1 || scriptName[0] == '\0') {
			CPlayer_SystemMessage(player, "Usage: .attach [0xSERIAL] SCRIPT");
			return;
		}
		strncpy(g_PendingAttachScript, scriptName, sizeof(g_PendingAttachScript) - 1);
		g_PendingAttachScript[sizeof(g_PendingAttachScript) - 1] = '\0';
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetAttach;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select entity");
		return;
	}

	// Custom: .setvar [0xSERIAL] NAME VALUE - set integer ObjVar on entity
	// Bare form stages NAME/VALUE and opens a target cursor.
	if (strncmp(cmd, "setvar ", 7) == 0 && CPlayer_IsEditing(player)) {
		const char *args = cmd + 7;
		uint32_t serial = 0;
		char varName[64];
		int varValue = 0;
		varName[0] = '\0';
		if (strncmp(args, "0x", 2) == 0 || strncmp(args, "0X", 2) == 0) {
			if (sscanf(args, "%x %63s %d", &serial, varName, &varValue) < 3 || serial == 0 || varName[0] == '\0') {
				CPlayer_SystemMessage(player, "Usage: .setvar [0xSERIAL] NAME VALUE");
				return;
			}
			CItem *item = CWorld_FindBySerial(g_World, serial);
			if (item == NULL) {
				CPlayer_SystemMessage(player, "Item not found");
				return;
			}
			CEntity_SetObjVar(item, varName, 0, (uintptr_t)varValue);
			char msg[160];
			snprintf(msg, sizeof(msg), "Set %s = %d", varName, varValue);
			CPlayer_SystemMessage(player, msg);
			return;
		}
		if (sscanf(args, "%63s %d", varName, &varValue) < 2 || varName[0] == '\0') {
			CPlayer_SystemMessage(player, "Usage: .setvar [0xSERIAL] NAME VALUE");
			return;
		}
		strncpy(g_PendingSetVar.name, varName, sizeof(g_PendingSetVar.name) - 1);
		g_PendingSetVar.name[sizeof(g_PendingSetVar.name) - 1] = '\0';
		g_PendingSetVar.value = varValue;
		uint8_t tbuf[20];
		player->targetCallback = GM_TargetSetVar;
		PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
		SendPacketToPlayer(player, tbuf, -1);
		CPlayer_SystemMessage(player, "Select entity");
		return;
	}

	// Custom: .placehouse - create a small house at player location (no terrain check)
	if (strncmp(cmd, "placehouse", 10) == 0 && CPlayer_IsEditing(player)) {
		CLocation loc;
		CLocation_Init(&loc);
		loc.x = player->mobile.container.item.resourceEntity.entity.location.x;
		loc.y = player->mobile.container.item.resourceEntity.entity.location.y;
		loc.z = player->mobile.container.item.resourceEntity.entity.location.z;
		CItem *house = CMultiManager_MakeMulti(&g_MultiManager, 0x64, &loc, 0);
		if (house == NULL) {
			CPlayer_SystemMessage(player, "Failed to create house");
		} else {
			char hmsg[80];
			snprintf(hmsg, sizeof(hmsg), "House placed serial=0x%08X", CMobile_GetSerial((CMobile *)house));
			CPlayer_SystemMessage(player, hmsg);
		}
		return;
	}

	// Custom: .msg TEXT - broadcast a system message to all connected players
	if (strncmp(cmd, "msg ", 4) == 0 && CPlayer_IsEditing(player)) {
		const char *msg = cmd + 4;
		if (*msg != '\0') {
			uint8_t obuf[0x42C];
			CPlayer *p;
			for (p = g_PlayerList.head; p != NULL; p = p->next) {
				PacketManager_MakePacket_TEXT(obuf, NULL, (CItem *)&p->mobile.container.item, 6, msg, 0x03B2, 3);
				SendPacketToPlayer(p, obuf, -1);
			}
		}
		return;
	}

	// Custom: .decay <mode> - switch decay mode for testing
	// mode 0 = debug, 1 = normal, 2 = off, 3 = fast test (~3s item lifetime)
	if (strncmp(cmd, "decay", 5) == 0 && CPlayer_IsEditing(player)) {
		if (cmd[5] != ' ') {
			CPlayer_SystemMessage(player, ".decay <0-3> - set decay (0=debug 1=normal 2=off 3=fast)");
			return;
		}
		int mode = atoi(cmd + 6);
		CWorld_InitDecay(mode);
		char dmsg[64];
		snprintf(dmsg, sizeof(dmsg), "Decay set to mode %d", mode);
		CPlayer_SystemMessage(player, dmsg);
		return;
	}

	// Custom: .regrow - fire the gamewide resource regrowth tick
	// once, synchronously. Normally invoked by the periodic hook in
	// time.c every ~68 minutes; this lets a GM (or the test suite)
	// fast-forward without waiting.
	if (strcmp(cmd, "regrow") == 0 && CPlayer_IsEditing(player)) {
		ResourceRegrowthTick();
		CPlayer_SystemMessage(player, "Resource regrowth tick fired");
		return;
	}

	// Custom: .respawntick - fire CResBankManager::InitRespawn once,
	// synchronously. Normally invoked by RespawnTimerCheck every
	// 0x1000 ticks (~17 min). InitRespawn ages the per-template inspection
	// countdowns and resets respawnChunkTimer to kick ProcessRespawnChunk's
	// chunk-egg regrow pass. The closed-economy bank refund is synchronous
	// (RefundResourceNodesToBank), so this tick is no longer a refund aid.
	if (strcmp(cmd, "respawntick") == 0 && CPlayer_IsEditing(player)) {
		CResBankManager_InitRespawn();
		CPlayer_SystemMessage(player, "Respawn timer tick fired");
		return;
	}

	// Custom: .spawnoff / .spawnon - halt / resume steady-state SpawnTick.
	// Toggles g_SpawnEnabled, which gates CResBankManager_SpawnTick
	// (resbank.c). Lets tests freeze the world so a single observation
	// of CResBankRegion.quantities[] isn't lost in steady-state drift
	// from background spawn deducts. The binary admin panel
	// (gamecentmon.c) is the binary's equivalent toggle.
	if (strcmp(cmd, "spawnoff") == 0 && CPlayer_IsEditing(player)) {
		g_SpawnEnabled = 0;
		CPlayer_SystemMessage(player, "SpawnTick disabled");
		return;
	}
	if (strcmp(cmd, "spawnon") == 0 && CPlayer_IsEditing(player)) {
		g_SpawnEnabled = 1;
		CPlayer_SystemMessage(player, "SpawnTick enabled");
		return;
	}

	// Custom: .initspawn on [seconds]|off|status - control g_IsInitialSpawn
	// for tests. Boot auto-enables it briefly so the world populates from
	// empty; tests that need to observe steady-state respawn must disable
	// it deterministically. "on" turns it back on with a short deadline
	// (default 30s, optional [seconds] in [1,3600]) so the burst
	// self-terminates; no arg or "status" reports state.
	if (strncmp(cmd, "initspawn", 9) == 0 && (cmd[9] == 0 || cmd[9] == ' ') && CPlayer_IsEditing(player)) {
		const char *arg = cmd + 9;
		while (*arg == ' ')
			arg++;
		char rmsg[128];
		if (*arg == 0 || strcmp(arg, "status") == 0) {
			uint32_t now = GetTickCount_UO();
			int remain = 0;
			if (g_AutoInitialSpawnDeadline > now)
				remain = (int)((g_AutoInitialSpawnDeadline - now) / 1000);
			snprintf(rmsg, sizeof(rmsg), "InitSpawn: enabled=%d auto_remain=%ds", g_IsInitialSpawn, remain);
		} else if (strncmp(arg, "on", 2) == 0 && (arg[2] == 0 || arg[2] == ' ')) {
			int secs = 30;
			const char *rest = arg + 2;
			while (*rest == ' ')
				rest++;
			if (*rest != 0) {
				int parsed = atoi(rest);
				if (parsed < 1 || parsed > 3600) {
					snprintf(rmsg, sizeof(rmsg), "InitSpawn: seconds must be 1..3600");
					CPlayer_SystemMessage(player, rmsg);
					return;
				}
				secs = parsed;
			}
			g_IsInitialSpawn = 1;
			g_AutoInitialSpawnDeadline = GetTickCount_UO() + (uint32_t)secs * 1000;
			snprintf(rmsg, sizeof(rmsg), "InitSpawn: enabled (auto-off in %ds)", secs);
		} else if (strcmp(arg, "off") == 0) {
			g_IsInitialSpawn = 0;
			g_AutoInitialSpawnDeadline = 0;
			snprintf(rmsg, sizeof(rmsg), "InitSpawn: disabled");
		} else {
			snprintf(rmsg, sizeof(rmsg), ".initspawn on [seconds]|off|status");
		}
		CPlayer_SystemMessage(player, rmsg);
		return;
	}

	// Custom: .help - list all commands
	if (strcmp(cmd, "help") == 0) {
		CPlayer_SystemMessage(player, "Counselor commands:");
		CPlayer_SystemMessage(player, ".q - show help queue (first 4)");
		CPlayer_SystemMessage(player, ".aq - show all help queue entries");
		CPlayer_SystemMessage(player, ".next - take next help request");
		CPlayer_SystemMessage(player, ".gotocur - go to current victim");
		CPlayer_SystemMessage(player, ".goto <name|num> - go to location or queue entry");
		CPlayer_SystemMessage(player, ".helpme <msg> - submit GM help request");
		CPlayer_SystemMessage(player, ".gm [name] - transfer to another GM");
		CPlayer_SystemMessage(player, ".rel - relinquish current request");
		CPlayer_SystemMessage(player, ".clear - clear current request");
		CPlayer_SystemMessage(player, ".jail / .unjail - jail/unjail victim or click player");
		CPlayer_SystemMessage(player, ".release [loc] - release victim or click player");
		CPlayer_SystemMessage(player, ".where - show your location");
		CPlayer_SystemMessage(player, ".who - show current victim info");
		CPlayer_SystemMessage(player, ".showids - toggle serial display");
		if (CPlayer_IsEditing(player)) {
			CPlayer_SystemMessage(player, "GM commands:");
			CPlayer_SystemMessage(player, ".go <X Y [Z]|name|list> - teleport to coords/location/list all");
			CPlayer_SystemMessage(player, ".players - open connected-player teleport menu");
			CPlayer_SystemMessage(player, ".gotoplayer <name|0xSERIAL> - teleport adjacent to a connected player");
			CPlayer_SystemMessage(player, ".tele - click to teleport");
			CPlayer_SystemMessage(player, ".mtele - repeating click teleport");
			CPlayer_SystemMessage(player, ".kill - kill target mobile");
			CPlayer_SystemMessage(player, ".mkill - repeating click kill");
			CPlayer_SystemMessage(player, ".remove - delete target entity");
			CPlayer_SystemMessage(player, ".mremove - repeating click remove");
			CPlayer_SystemMessage(player, ".rename NAME - rename target mobile");
			CPlayer_SystemMessage(player, ".say TEXT - make target speak");
			CPlayer_SystemMessage(player, ".walk - make NPC walk to destination");
			CPlayer_SystemMessage(player, ".attack - make NPC attack a target");
			CPlayer_SystemMessage(player, ".info - inspect target entity");
			CPlayer_SystemMessage(player, ".nearby - dump nearby entities");
			CPlayer_SystemMessage(player, ".create <ID|name> [count] - create item in backpack");
			CPlayer_SystemMessage(player, ".spawn <ID|name> - spawn NPC near you");
			CPlayer_SystemMessage(player, ".bank - open target's bank box");
			CPlayer_SystemMessage(player, ".fillspellbook - fill spellbook with all spells");
			CPlayer_SystemMessage(player, ".hue <ID|name> - set hue on target");
			CPlayer_SystemMessage(player, ".multi <ID|name|list> - place multi");
			CPlayer_SystemMessage(player, ".spell list - list all spells");
			CPlayer_SystemMessage(player, ".invis / .vis - hide/reveal target");
			CPlayer_SystemMessage(player, ".invuln / .vuln - invuln/vuln target");
			CPlayer_SystemMessage(player, ".light [0-30] - permanent night sight on target (0=off)");
			CPlayer_SystemMessage(player, ".set <stat|skill|all|list> [N] - set stat/skill");
			CPlayer_SystemMessage(player, ".settarget <stat|skill|all> [N] - set on target");
			CPlayer_SystemMessage(player, ".resurrect - resurrect target player");
			CPlayer_SystemMessage(player, ".freeze / .unfreeze - freeze/unfreeze target");
			CPlayer_SystemMessage(player, ".mute / .unmute - mute/unmute target");
			CPlayer_SystemMessage(player, ".lock / .unlock - lock/unlock target");
			CPlayer_SystemMessage(player, ".weather rain|storm|snow|clear");
			CPlayer_SystemMessage(player, ".save - force world save");
			CPlayer_SystemMessage(player, ".speed 1|3|5 - set move speed");
			CPlayer_SystemMessage(player, ".time H - set world hour (0-23)");
			CPlayer_SystemMessage(player, ".role [+/-godmode|gamemaster|counselor]");
			CPlayer_SystemMessage(player, ".decay <0-3> - set decay (0=debug 1=normal 2=off 3=fast)");
			CPlayer_SystemMessage(player, ".msg TEXT - broadcast system message to all players");
			CPlayer_SystemMessage(player, ".placehouse - create small house at your location");
			CPlayer_SystemMessage(player, ".itemhp [VALUE] - click weapon/armor (serial form still accepted)");
			CPlayer_SystemMessage(player, ".durtest - click weapon/armor for durability test");
			CPlayer_SystemMessage(player, ".lockitem - click item to attach lockdown script");
			CPlayer_SystemMessage(player, ".attach SCRIPT - click entity to attach script");
			CPlayer_SystemMessage(player, ".setvar NAME VALUE - click entity to set objvar");
			CPlayer_SystemMessage(player, ".npcinv - click mobile to list its inventory");
		}
		return;
	}

	CPlayer_SystemMessage(player, "Unknown counselor command");
}

/*
 * 0x0044E9A0 - CHelpQueue::OnLogout
 *
 * Called when a counselor/GM logs out. Delegates to Relinquish
 * to release their current help request.
 */
void
CHelpQueue_OnLogout(CHelpQueue *q, CPlayer *player)
{
	CHelpQueue_Relinquish(q, player);
}

/*
 * 0x0044E9B9 - CHelpQueue::ShowQueue
 *
 * Sends queue contents to the GM. maxEntries bounds output (4 for
 * .q, -1 to show all for .aq).
 */
int
CHelpQueue_ShowQueue(CHelpQueue *q, CPlayer *player, int maxEntries)
{
	CString line;
	CHelpRequestNode *node;
	int i;

	CString_DefaultConstructor(&line);
	CString_ConcatUInt(&line, (unsigned int)q->count);
	CString_AppendCStr(&line, " help request(s) in queue");
	CPlayer_SystemMessage(player, CString_GetBuffer(&line));

	i = 0;
	for (node = q->head; node != NULL; node = node->next) {
		if (maxEntries != -1 && i >= maxEntries)
			break;

		CString_AssignCStr(&line, "");
		CString_ConcatUInt(&line, node->serial);
		CString_AppendCStr(&line, " ");
		CString_ConcatChar(&line, node->level);
		CString_AppendCStr(&line, " \"");
		CString_ConcatCString(&line, &node->callerName);
		CString_AppendCStr(&line, "\" ");
		CString_ConcatCString(&line, &node->message);
		CPlayer_SystemMessage(player, CString_GetBuffer(&line));

		i++;
	}

	CString_Destructor(&line);
	return 1;
}

/*
 * 0x0044EB44 - CHelpQueue::GotoCur
 *
 * Teleports the GM to their current victim from the counVictim tag.
 * Returns 0 when no victim is set or the queued entry is stale.
 */
int
CHelpQueue_GotoCur(CHelpQueue *q, CPlayer *player)
{
	uint32_t victimSerial;
	CHelpRequestNode *node;

	victimSerial = 0;
	if (!GetCountVictimTag((CItem *)player, &victimSerial)) {
		CPlayer_SystemMessage(player, "No current help request.");
		return 0;
	}

	node = CHelpQueue_FindBySerial(q, victimSerial);
	if (node != NULL) {
		CPlayer_SystemMessage(player, "Going to current player");
		CHelpQueue_Who(q, player);
		CHelpQueue_GotoEntity(q, CMobile_GetSerial(&player->mobile), node->serial);
	} else {
		CPlayer_SystemMessage(player, "Current help request invalid");
		return 0;
	}
	return 1;
}

/*
 * 0x0044EBFA - CHelpQueue::Next
 *
 * Closes the GM's current request as 'd' (done) and assigns the
 * next 'n' (new) entry as 'h' (handling), then teleports the GM.
 */
int
CHelpQueue_Next(CHelpQueue *q, CPlayer *player)
{
	uint32_t victimSerial;
	CHelpRequestNode *node;

	victimSerial = 0;
	if (GetCountVictimTag((CItem *)player, &victimSerial)) {
		ClearCountVictim((CItem *)player);
		CHelpQueue_UpdateLevel(q, victimSerial, 'd');
		CPlayer_SystemMessage(player, "Removed previous player from queue");
	}

	node = CHelpQueue_FindNextPending(q);
	if (node == NULL) {
		CPlayer_SystemMessage(player, "There are no pending queue entries");
		return 0;
	}

	node->level = 'h';
	CHelpQueue_UpdateLevel(q, node->serial, 'h');

	SetCountVictim((CItem *)player, node->serial);
	CPlayer_SystemMessage(player, "Going to next player");
	CHelpQueue_Who(q, player);

	CHelpQueue_GotoEntity(q, CMobile_GetSerial(&player->mobile), node->serial);
	return 1;
}

/*
 * 0x0044ECF8 - CHelpQueue::TransferEntry
 *
 * Forwards a queued request to a named GM, prepending "Transfered: "
 * to the message. AddWithLevel is a no-op in the binary, so this
 * effectively just formats the banner.
 */
int
CHelpQueue_TransferEntry(CHelpQueue *q, CPlayer *player, CString *gmName, uint32_t victimSerial)
{
	CHelpRequestNode *node;
	CString transferMsg;

	USED(player);

	node = CHelpQueue_FindBySerial(q, victimSerial);
	if (node == NULL)
		return 0;

	if (CString_IsEmpty(gmName)) {
		CString_Constructor(&transferMsg, "Transfered: ");
		CString_ConcatCString(&transferMsg, &node->message);
		CHelpQueue_AddWithLevel(q, node->serial, CString_GetBuffer(&node->callerName), node->origLevel, CString_GetBuffer(&transferMsg));
		CString_Destructor(&transferMsg);
	} else {
		CString_Constructor(&transferMsg, "Transfered: ");
		CString_ConcatCString(&transferMsg, gmName);
		CHelpQueue_AddWithLevel(q, node->serial, CString_GetBuffer(&node->callerName), node->origLevel, CString_GetBuffer(&transferMsg));
		CString_Destructor(&transferMsg);
	}
	return 1;
}

/*
 * 0x0044EE33 - CHelpQueue::GmTransfer
 *
 * .gm command: hands the current victim's request to a named GM
 * and marks the entry 'd' (done).
 */
int
CHelpQueue_GmTransfer(CHelpQueue *q, CPlayer *player, const char *gmName)
{
	uint32_t victimSerial;
	CString gmStr;

	victimSerial = 0;
	if (!GetCountVictimTag((CItem *)player, &victimSerial)) {
		CPlayer_SystemMessage(player, "You have no current player set, sorry");
		return 0;
	}

	CString_Constructor(&gmStr, gmName);
	CHelpQueue_TransferEntry(q, player, &gmStr, victimSerial);
	CString_Destructor(&gmStr);

	ClearCountVictim((CItem *)player);
	CHelpQueue_UpdateLevel(q, victimSerial, 'd');

	CPlayer_SystemMessage(player, "Transfered request to GM queue");
	return 1;
}

/*
 * 0x0044EEEF - CHelpQueue::GotoBySerial
 *
 * .goto <number>: relinquishes any current victim, then teleports
 * to the queue entry at the given index.
 */
int
CHelpQueue_GotoBySerial(CHelpQueue *q, CPlayer *player, int queueIndex)
{
	uint32_t victimSerial;
	CHelpRequestNode *node;
	int i;

	victimSerial = 0;
	if (GetCountVictimTag((CItem *)player, &victimSerial)) {
		ClearCountVictim((CItem *)player);
		CHelpQueue_UpdateLevel(q, victimSerial, 'n');
		CPlayer_SystemMessage(player, "Player request relinquished");
	}

	i = 0;
	for (node = q->head; node != NULL; node = node->next) {
		if (i >= queueIndex)
			break;
		i++;
	}

	if (node == NULL) {
		CPlayer_SystemMessage(player, "That queue entry does not exist");
		return 0;
	}

	CHelpQueue_GotoEntity(q, CMobile_GetSerial(&player->mobile), node->serial);
	return 1;
}

/*
 * 0x0044EFF1 - CHelpQueue::GotoByName
 *
 * .goto <name>: resolves the named location and teleports the GM
 * (and any attached victim) there.
 */
int
CHelpQueue_GotoByName(CHelpQueue *q, CPlayer *player, const char *locName)
{
	CLocation loc;
	CItem *victim;
	CString msg;
	const char *pname;

	USED(q);

	CLocation_Init(&loc);
	if (!FindLocation(locName, &loc)) {
		CPlayer_SystemMessage(player, "Invalid location");
		return 0;
	}

	victim = GetCountVictim((CItem *)player);
	if (victim != NULL) {
		pname = ((const char *(*)(void *, int))VT_FN((CItem *)player, VT_SPEAK_SYS_MSG))(player, 1);
		CString_Constructor(&msg, pname);
		CString_AppendCStr(&msg, " is transfering you to ");
		CString_AppendCStr(&msg, locName);
		CPlayer_SystemMessage((CPlayer *)victim, CString_GetBuffer(&msg));

		((void (*)(CItem *, CLocation *))VT_FN(victim, VT_TRANSFER_TO))(victim, &loc);

		CString_AssignCStr(&msg, "Transfering ");
		pname = ((const char *(*)(void *))VT_FN(victim, VT_GET_NAME))(victim);
		CString_AppendCStr(&msg, pname);
		CString_AppendCStr(&msg, " to ");
		CString_AppendCStr(&msg, locName);
		CPlayer_SystemMessage(player, CString_GetBuffer(&msg));
		CString_Destructor(&msg);
	}

	CString_Constructor(&msg, "Transfering to ");
	CString_AppendCStr(&msg, locName);
	CPlayer_SystemMessage(player, CString_GetBuffer(&msg));
	((void (*)(CItem *, CLocation *))VT_FN((CItem *)player, VT_TRANSFER_TO))((CItem *)player, &loc);
	CString_Destructor(&msg);
	return 1;
}

/*
 * 0x0044F17C - CHelpQueue::Relinquish
 *
 * .rel command: returns the current victim to the queue at level
 * 'n' (pending). Returns 0 if no victim is set.
 */
int
CHelpQueue_Relinquish(CHelpQueue *q, CPlayer *player)
{
	uint32_t victimSerial;

	victimSerial = 0;
	if (GetCountVictimTag((CItem *)player, &victimSerial)) {
		ClearCountVictim((CItem *)player);
		CHelpQueue_UpdateLevel(q, victimSerial, 'n');
		CPlayer_SystemMessage(player, "Player request relinquished");
		return 1;
	}

	CPlayer_SystemMessage(player, "You have no current player set, sorry");
	return 0;
}

/*
 * 0x0044F1E3 - CHelpQueue::Clear
 *
 * .clear command: closes the current victim's request at level 'd'
 * (done). Returns 0 if no victim is set.
 */
int
CHelpQueue_Clear(CHelpQueue *q, CPlayer *player)
{
	uint32_t victimSerial;

	victimSerial = 0;
	if (GetCountVictimTag((CItem *)player, &victimSerial)) {
		ClearCountVictim((CItem *)player);
		CHelpQueue_UpdateLevel(q, victimSerial, 'd');
		CPlayer_SystemMessage(player, "Player request cleared from queue");
		return 1;
	}

	CPlayer_SystemMessage(player, "You have no current player set, sorry");
	return 0;
}

/*
 * 0x0044F24A - CHelpQueue::Who
 *
 * .who command: shows the current victim's queue entry, or clears
 * the stale victim tag if the entry is gone.
 */
int
CHelpQueue_Who(CHelpQueue *q, CPlayer *player)
{
	uint32_t victimSerial;
	CHelpRequestNode *node;
	CString line;

	victimSerial = 0;
	if (!GetCountVictimTag((CItem *)player, &victimSerial)) {
		CPlayer_SystemMessage(player, "You are not currently handling a help request");
		return 0;
	}

	node = CHelpQueue_FindBySerial(q, victimSerial);
	if (node != NULL) {
		CString_DefaultConstructor(&line);
		CString_AssignCStr(&line, "Current: ");
		CString_ConcatUInt(&line, node->serial);
		CString_AppendCStr(&line, " ");
		CString_ConcatChar(&line, node->level);
		CString_AppendCStr(&line, " \"");
		CString_ConcatCString(&line, &node->callerName);
		CString_AppendCStr(&line, "\" ");
		CString_ConcatCString(&line, &node->message);
		CPlayer_SystemMessage(player, CString_GetBuffer(&line));
		CString_Destructor(&line);
		return 1;
	}

	ClearCountVictim((CItem *)player);
	CHelpQueue_UpdateLevel(q, victimSerial, 'd');
	CPlayer_SystemMessage(player, "Invalid request, player request cleared from queue");
	return 0;
}

/*
 * 0x0044F47B - CHelpQueue::NotifyLogin
 *
 * Called when a counselor logs in. Increments counselorCount; the
 * player argument is unused.
 */
void
CHelpQueue_NotifyLogin(CHelpQueue *this, CPlayer *player)
{
	USED(player);
	this->counselorCount++;
}

/*
 * 0x0044F497 - CHelpQueue::DecrCounselors
 *
 * Counselor logout counterpart: decrements counselorCount.
 */
void
CHelpQueue_DecrCounselors(CHelpQueue *this, CPlayer *player)
{
	USED(player);
	this->counselorCount--;
}

/*
 * 0x0044F4C0 - std::list<CHelpEntry>::_Init
 *
 * std::list constructor template for CHelpEntry. Installs a
 * self-referencing sentinel node and zeros the count.
 */
static void *
StdHelpList_Init(StdPtrList *list, void *src)
{
	*(uint8_t *)list = *(uint8_t *)src;
	list->head = StdHelpList_Buynode(list, NULL, NULL);
	list->size = 0;
	return list;
}

/*
 * 0x0044F500 - std::list<CHelpEntry>::push_back
 *
 * std::list::push_back template for CHelpEntry.
 */
static __attribute__((unused)) void
StdHelpList_PushBack(StdPtrList *list, void *value)
{
	StdPtrNode *endIter;
	StdPtrNode *result;

	StdPtrList_End(list, &endIter);
	StdHelpList_DoInsert(list, &result, endIter, value);
}

/*
 * 0x0044F530 - std::list<CHelpEntry>::erase
 *
 * std::list::erase template for CHelpEntry: post-increments the iterator,
 * unlinks the node, runs the destroy callback on its value, frees the node,
 * and decrements the list size.
 */
void
StdHelpList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos)
{
	StdPtrNode *nextNode;

	nextNode = pos->next;
	pos->prev->next = pos->next;
	pos->next->prev = pos->prev;
	StdHelpList_Destroy(list, &pos->value);
	free(pos);
	list->size--;
	*result = nextNode;
}

/*
 * 0x0044F5F0 - std::list<CHelpEntry>::_Insert
 *
 * std::list::_Insert template for CHelpEntry. Allocates a new
 * node, links it before pos, copies the value in, and returns the
 * iterator via result.
 */
static void
StdHelpList_DoInsert(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos, void *value)
{
	StdPtrNode *node;
	StdPtrNode *newNode;

	node = pos;

	newNode = StdHelpList_Buynode(list, node, *StdPtrNode_GetPrev(node));
	*StdPtrNode_GetPrev(node) = newNode;
	node = *StdPtrNode_GetPrev(node);
	StdPtrNode_GetNext(*StdPtrNode_GetPrev(node))->next = node;

	StdHelpList_ConstructorWrapper(list, StdPtrNode_GetValue(node), value);

	list->size = list->size + 1;
	CIterCtx_Set(result, node);
}

/*
 * 0x0044F6A0 - std::list<CHelpEntry>::_Buynode
 *
 * std::list::_Buynode template for CHelpEntry. Allocates a node and
 * seeds next/prev from hints (self-referencing when NULL).
 */
static StdPtrNode *
StdHelpList_Buynode(StdPtrList *list, StdPtrNode *nextHint, StdPtrNode *prevHint)
{
	StdPtrNode *newNode;
	StdPtrNode *nextVal;
	StdPtrNode *prevVal;

	// 2*sizeof(void*) replaces the binary's hardcoded +8 header
	// so pointer widening is covered in 64-bit builds.
	newNode = (StdPtrNode *)StdPtrList_Charalloc(list, sizeof(CHelpEntry) + 2 * sizeof(void *));

	if (nextHint != NULL)
		nextVal = nextHint;
	else
		nextVal = newNode;
	StdPtrNode_GetNext(newNode)->next = nextVal;

	if (prevHint != NULL)
		prevVal = prevHint;
	else
		prevVal = newNode;
	*StdPtrNode_GetPrev(newNode) = prevVal;

	return newNode;
}

/*
 * 0x0044F730 - std::list<CHelpEntry>::_Destroy
 *
 * std::list erase destroy hook: defers to HelpEntry_DestructorWrapper.
 */
static void
StdHelpList_Destroy(void *list, void *element)
{
	USED(list);
	HelpEntry_DestructorWrapper(element);
}

/*
 * 0x0044F770 - std::list<CHelpEntry>::_Constructor
 *
 * std::list value-construct hook: forwards to HelpEntry_Constructor.
 */
static void
StdHelpList_ConstructorWrapper(void *list, void *dst, void *src)
{
	USED(list);
	HelpEntry_Constructor(dst, src);
}

/*
 * 0x0044F790 - HelpEntry_DestructorWrapper
 *
 * Thunk to ScalarDelete with flags=0 (destroy fields without freeing the
 * entry itself).
 */
static void *
HelpEntry_DestructorWrapper(void *entry)
{
	return CHelpEntry_ScalarDelete(entry, 0);
}

/*
 * 0x0044F7A0 - HelpEntry_Constructor
 *
 * Placement-new wrapper around CHelpEntry::CHelpEntry (copy constructor).
 */
static void *
HelpEntry_Constructor(void *dst, void *src)
{
	void *ptr;

	ptr = (void *)StdKfn_Identity(sizeof(CHelpEntry), (uintptr_t)dst);
	if (ptr == NULL)
		return NULL;
	return CHelpEntry_CopyConstructor(ptr, src);
}

/*
 * 0x0044F7E0 - CHelpEntry::ScalarDelete
 *
 * Scalar deleting destructor: frees the allocation only when flags & 1.
 */
static CHelpEntry *
CHelpEntry_ScalarDelete(CHelpEntry *self, int flags)
{
	CHelpEntry_Destructor(self);

	if (flags & 1)
		free(self);
	return NULL;
}

/*
 * 0x0044F810 - CHelpEntry::CHelpEntry (copy constructor)
 *
 * Copy-constructs a CHelpEntry from src.
 */
static CHelpEntry *
CHelpEntry_CopyConstructor(CHelpEntry *self, CHelpEntry *src)
{
	self->serial = src->serial;
	self->type = src->type;
	self->priority = src->priority;
	CString_CopyConstructor(&self->name, &src->name);
	CString_CopyConstructor(&self->message, &src->message);
	return self;
}

/*
 * 0x0044F8A0 - CHelpEntry::CHelpEntry
 *
 * Constructs a CHelpEntry from explicit fields.
 */
static __attribute__((unused)) CHelpEntry *
CHelpEntry_Constructor(CHelpEntry *self, uint32_t serial, uint8_t type, uint8_t priority, CString *name, CString *message)
{
	self->serial = serial;
	self->type = type;
	self->priority = priority;
	CString_CopyConstructor(&self->name, name);
	CString_CopyConstructor(&self->message, message);
	return self;
}

/*
 * 0x0044F920 - CHelpEntry::~CHelpEntry
 *
 * Destroys the CString fields in reverse declaration order.
 */
static void
CHelpEntry_Destructor(CHelpEntry *self)
{
	CString_Destructor(&self->message);
	CString_Destructor(&self->name);
}

/*
 * 0x0045F1E0 - CAssistance::~CAssistance
 *
 * Destroys the three CString fields in reverse declaration order.
 */
static __attribute__((unused)) void
CAssistance_Destructor(CAssistance *this)
{
	CString_Destructor(&this->body);
	CString_Destructor(&this->subject);
	CString_Destructor(&this->name);
}

/*
 * 0x0045F240 - CAssistanceNode::~CAssistanceNode
 *
 * Destroys the two CString fields in reverse declaration order.
 */
static __attribute__((unused)) void
CAssistance_NodeDestructor(CAssistanceNode *this)
{
	CString_Destructor(&this->str2);
	CString_Destructor(&this->str1);
}

/*
 * 0x0045F6C0 - CAssistance::CAssistance
 *
 * Default-constructs a CAssistance: zeros the scalar fields and
 * default-constructs the three CStrings.
 */
static __attribute__((unused)) CAssistance *
CAssistance_Constructor(CAssistance *self)
{
	self->serial = 0;
	CString_DefaultConstructor(&self->name);
	self->type = 0;
	self->level = 0;
	CString_DefaultConstructor(&self->subject);
	CString_DefaultConstructor(&self->body);
	return self;
}

/*
 * 0x0045F740 - CAssistance::LoadRecord_TypeD
 *
 * Deserializes a CAssistance record from buf (flag + serial + type
 * + level + 30-byte name + 15-byte subject + 256-byte body) and
 * returns the leading flag byte.
 */
static __attribute__((unused)) uint8_t
CAssistance_LoadRecordD(CAssistance *this, uint8_t *buf, int unused)
{
	CAssistance *a = this;
	uint8_t flag;
	char tmp[256];
	int offset;

	USED(unused);

	offset = 0;

	memmove(&flag, buf + offset, 1);
	offset += 1;

	memmove(&a->serial, buf + offset, 4);
	offset += 4;

	memmove(&a->type, buf + offset, 1);
	offset += 1;

	memmove(&a->level, buf + offset, 1);
	offset += 1;

	strncpy(tmp, (char *)(buf + offset), 0x1E);
	offset += 0x1E;
	CString_AssignCStr(&a->name, tmp);

	strncpy(tmp, (char *)(buf + offset), 0x0F);
	offset += 0x0F;
	CString_AssignCStr(&a->subject, tmp);

	strncpy(tmp, (char *)(buf + offset), 0x100);
	offset += 0x100;
	CString_AssignCStr(&a->body, tmp);

	return flag;
}

/*
 * 0x0045F8A0 - CAssistance::LoadRecord_TypeA
 *
 * Like LoadRecord_TypeD, but the buffer begins with a 4-byte target
 * serial that is returned via serialOut.
 */
static __attribute__((unused)) uint8_t
CAssistance_LoadRecordA(CAssistance *this, uint8_t *buf, int unused, uint32_t *serialOut)
{
	CAssistance *a = this;
	uint8_t flag;
	char tmp[256];
	int offset;

	USED(unused);

	offset = 0;

	memmove(serialOut, buf + offset, 4);
	offset += 4;

	// Read flag byte (1 byte)
	memmove(&flag, buf + offset, 1);
	offset += 1;

	// Read serial (4 bytes)
	memmove(&a->serial, buf + offset, 4);
	offset += 4;

	// Read type (1 byte)
	memmove(&a->type, buf + offset, 1);
	offset += 1;

	// Read priority/level (1 byte)
	memmove(&a->level, buf + offset, 1);
	offset += 1;

	// Read name (30 bytes) into tmp, assign to CString name
	strncpy(tmp, (char *)(buf + offset), 0x1E);
	offset += 0x1E;
	CString_AssignCStr(&a->name, tmp);

	// Read subject (15 bytes) into tmp, assign to CString subject
	strncpy(tmp, (char *)(buf + offset), 0x0F);
	offset += 0x0F;
	CString_AssignCStr(&a->subject, tmp);

	// Read body (256 bytes) into tmp, assign to CString body
	strncpy(tmp, (char *)(buf + offset), 0x100);
	offset += 0x100;
	CString_AssignCStr(&a->body, tmp);

	return flag;
}

/*
 * 0x0045FA20 - CAssistance::SaveRecord_TypeB
 *
 * Serializes a CGMPageEntry into a newly allocated 54-byte buffer:
 * type + 3x serial + xyz + dword + byte + 30-byte name.
 */
static __attribute__((unused)) uint8_t *
CAssistance_SaveRecordB(CSkillUseCtx *this)
{
	uint8_t *buf;
	int size;
	int offset;

	size = CAssistance_GetSerializedSizeB(this);
	buf = (uint8_t *)malloc(size);

	offset = 0;

	memmove(buf + offset, &this->type, 1);
	offset += 1;

	memmove(buf + offset, &this->serial, 4);
	offset += 4;

	memmove(buf + offset, &this->field08, 4);
	offset += 4;

	memmove(buf + offset, &this->field0C, 4);
	offset += 4;

	memmove(buf + offset, &this->location.x, 2);
	offset += 2;

	memmove(buf + offset, &this->location.y, 2);
	offset += 2;

	memmove(buf + offset, &this->location.z, 2);
	offset += 2;

	memmove(buf + offset, &this->field18, 4);
	offset += 4;

	memmove(buf + offset, &this->field1C, 1);
	offset += 1;

	strncpy((char *)(buf + offset), this->name, 0x1E);
	offset += 0x1E;

	USED(offset);
	return buf;
}

/*
 * 0x0045FBA0 - CAssistance::GetSerializedSizeB
 *
 * Fixed serialized size of a CGMPageEntry (54 bytes).
 */
static int
CAssistance_GetSerializedSizeB(CSkillUseCtx *this)
{
	USED(this);
	return 0x36;
}

/*
 * 0x0045FBB0 - CAssistance::LoadRecord_TypeB
 *
 * Inverse of SaveRecord_TypeB. Returns the leading type byte.
 */
static __attribute__((unused)) uint8_t
CAssistance_LoadRecordB(CSkillUseCtx *this, uint8_t *buf, int unused)
{
	int offset;

	USED(unused);

	offset = 0;

	memmove(&this->type, buf + offset, 1);
	offset += 1;

	memmove(&this->serial, buf + offset, 4);
	offset += 4;

	memmove(&this->field08, buf + offset, 4);
	offset += 4;

	memmove(&this->field0C, buf + offset, 4);
	offset += 4;

	memmove(&this->location.x, buf + offset, 2);
	offset += 2;

	memmove(&this->location.y, buf + offset, 2);
	offset += 2;

	memmove(&this->location.z, buf + offset, 2);
	offset += 2;

	memmove(&this->field18, buf + offset, 4);
	offset += 4;

	memmove(&this->field1C, buf + offset, 1);
	offset += 1;

	strncpy(this->name, (char *)(buf + offset), 0x1E);
	offset += 0x1E;

	USED(offset);
	return this->type;
}

/*
 * 0x0045FD20 - CAssistanceNode::CAssistanceNode
 *
 * Default constructor: IDs and sentinel word set to -1/0xFFFF, the
 * rest zeroed, and the two CStrings default constructed.
 */
static __attribute__((unused)) CAssistanceNode *
CAssistanceNode_Constructor(CAssistanceNode *self)
{
	CString_DefaultConstructor(&self->str1);
	CString_DefaultConstructor(&self->str2);

	self->id1 = 0xFFFFFFFF;
	self->id2 = 0xFFFFFFFF;
	self->field = 0xFFFF;
	self->typeFlag = 0;
	self->field1 = 0;
	self->field2 = 0;

	return self;
}

/*
 * 0x0045FDB0 - CAssistance::LoadRecord_TypeC
 *
 * Deserializes a CAssistanceNode record (two IDs, a 30-byte name,
 * a 4K data blob, and two trailing words) and returns the type flag.
 */
static __attribute__((unused)) uint8_t
CAssistance_LoadRecordC(CAssistanceNode *this, uint8_t *buf, int unused)
{
	CAssistanceNode *n = this;
	char tmp30[0x1E];
	char tmp4096[0x1000];
	int offset;

	USED(unused);

	offset = 0;

	memmove(&n->id1, buf + offset, 4);
	offset += 4;

	memmove(&n->id2, buf + offset, 4);
	offset += 4;

	memmove(&n->field, buf + offset, 2);
	offset += 2;

	memmove(tmp30, buf + offset, 0x1E);
	offset += 0x1E;
	CString_AssignCStr(&n->str1, tmp30);

	memmove(&n->typeFlag, buf + offset, 1);
	offset += 1;

	memmove(tmp4096, buf + offset, 0x1000);
	offset += 0x1000;
	CString_AssignCStr(&n->str2, tmp4096);

	memmove(&n->field1, buf + offset, 2);
	offset += 2;

	memmove(&n->field2, buf + offset, 2);
	offset += 2;

	USED(offset);
	return n->typeFlag;
}

/*
 * 0x00469530 - CAssistance queue dtor wrapper
 *
 * atexit hook that clears the global help queue via
 * StdPtrList_Destructor_HelpQueue.
 */
static __attribute__((unused)) void
CAssistance_QueueDestructorWrapper(CAssistance *this)
{
	StdPtrList_Destructor_HelpQueue((StdPtrList *)this);
}

/*
 * 0x004696D0 - std::list<void*>::erase (range, help queue variant)
 *
 * Erases [first, last) from the help queue list and writes the
 * resulting iterator to *result.
 */
void
StdHelpList_EraseRange(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last)
{
	StdPtrNode *postIncTemp;

	while (StdPtrIter_Neq(&first, &last) & 0xFF) {
		StdPtrIter_PostInc(&first, &postIncTemp, 0);
		StdHelpList_Erase(list, &postIncTemp, *(StdPtrNode **)&postIncTemp);
	}
	*result = first;
}

static int __attribute__((unused)) CAssistanceQueue_GetSerializedSize(CAssistance *this);

/*
 * 0x0049DBD0 - CAssistanceQueue::Submit
 *
 * Serializes a CAssistance into a fresh 308-byte buffer prefixed by
 * the requestType byte. Caller takes ownership of the allocation.
 */
static __attribute__((unused)) uint8_t *
CAssistanceQueue_Submit(CAssistance *this, uint8_t requestType)
{
	uint8_t *buf;
	int offset;
	int size;
	uint32_t serial;
	char tmp[256];

	size = CAssistanceQueue_GetSerializedSize(this);
	buf = (uint8_t *)malloc(size);
	memset(buf, 0, CAssistanceQueue_GetSerializedSize(this));

	offset = 0;

	memcpy(buf + offset, &requestType, 1);
	offset += 1;

	serial = this->serial;
	memcpy(buf + offset, &serial, 4);
	offset += 4;

	memcpy(buf + offset, &this->type, 1);
	offset += 1;

	memcpy(buf + offset, &this->level, 1);
	offset += 1;

	memset(tmp, 0, 0x100);
	strncpy(tmp, CString_GetBuffer(&this->name), 0x1E);
	strcpy((char *)(buf + offset), tmp);
	offset += 0x1E;

	strncpy(tmp, CString_GetBuffer(&this->subject), 0x0F);
	strcpy((char *)(buf + offset), tmp);
	offset += 0x0F;

	strncpy(tmp, CString_GetBuffer(&this->body), 0x100);
	strcpy((char *)(buf + offset), tmp);
	offset += 0x100;

	USED(offset);
	return buf;
}

/*
 * 0x0049DD90 - CAssistanceQueue::GetSerializedSize
 *
 * Fixed serialized size of a CAssistance record (308 bytes).
 */
static int __attribute__((unused))
CAssistanceQueue_GetSerializedSize(CAssistance *this)
{
	USED(this);
	return 0x134;
}

/*
 * Custom - GM_TargetRename
 */
static void
GM_TargetRename(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Rename cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CMobile_SetName((CMobile *)target, g_PendingRenameBuf);
	char msg[80];
	snprintf(msg, sizeof(msg), "Renamed to %s", g_PendingRenameBuf);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetTele(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(serial);
	player->targetCallback = NULL;
	if (x == 0 && y == 0) {
		CPlayer_SystemMessage(player, "Teleport cancelled");
		return;
	}
	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, (int)x, (int)y)) {
		CPlayer_SystemMessage(player, "Invalid coordinates");
		return;
	}
	int gz = (int)(int16_t)z;
	if (gz == 0) {
		int blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)x, (int)y, 0);
		if (blockIdx >= 0)
			gz = (int)g_MapBlocks[blockIdx].cells[((int)y & 7) * 8 + ((int)x & 7)].z;
	}

	CLocation goLoc;
	CLocation_Init(&goLoc);
	CLocation_Set(&goLoc, (int16_t)x, (int16_t)y, (int16_t)gz);

	((void (*)(void *))VT_FN((CItem *)player, VT_HIDE))((CItem *)player);
	((void (*)(void *, CLocation *))VT_FN((CItem *)player, VT_DROP_AT_FEET))((CItem *)player, &goLoc);
	CMobile_NotifyNearbyPlayers((CItem *)player);

	char msg[80];
	snprintf(msg, sizeof(msg), "Teleported to %d %d %d", (int)x, (int)y, (int)(int16_t)player->mobile.container.item.resourceEntity.entity.location.z);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetMTele(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(serial);
	if ((x == 0 && y == 0) || !CBlockManager_IsValidCoord(&g_SpatialGrid, (int)x, (int)y)) {
		player->targetCallback = NULL;
		CPlayer_SystemMessage(player, "Multi-teleport done");
		return;
	}
	int gz = (int)(int16_t)z;
	if (gz == 0) {
		int blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)x, (int)y, 0);
		if (blockIdx >= 0)
			gz = (int)g_MapBlocks[blockIdx].cells[((int)y & 7) * 8 + ((int)x & 7)].z;
	}

	CLocation goLoc;
	CLocation_Init(&goLoc);
	CLocation_Set(&goLoc, (int16_t)x, (int16_t)y, (int16_t)gz);

	((void (*)(void *))VT_FN((CItem *)player, VT_HIDE))((CItem *)player);
	((void (*)(void *, CLocation *))VT_FN((CItem *)player, VT_DROP_AT_FEET))((CItem *)player, &goLoc);
	CMobile_NotifyNearbyPlayers((CItem *)player);

	char msg[80];
	snprintf(msg, sizeof(msg), "Teleported to %d %d %d", (int)x, (int)y, (int)(int16_t)player->mobile.container.item.resourceEntity.entity.location.z);
	CPlayer_SystemMessage(player, msg);

	// Re-send target cursor for next teleport
	uint8_t tbuf[20];
	PacketManager_MakePacket_TARGET(tbuf, 1, 0, 0);
	SendPacketToPlayer(player, tbuf, -1);
}

static void
GM_TargetMKill(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	if (serial == 0) {
		player->targetCallback = NULL;
		CPlayer_SystemMessage(player, "Multi-kill done");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
	} else if (VT_IsDead(target)) {
		CPlayer_SystemMessage(player, "Already dead");
	} else {
		// Same fix as GM_TargetKill: OnDeathWrap creates the corpse
		// but does not remove the mobile; combat (combat.c:1415-1422)
		// VT_DELETEs the mobile after, and so must we for non-players.
		((void (*)(void *, void *, int))VT_FN(target, VT_ON_DEATH_WRAP))(target, player, 1);
		if (!VT_IsPlayer(target))
			((void (*)(void *))VT_FN(target, VT_DELETE))(target);
		char msg[80];
		snprintf(msg, sizeof(msg), "Killed 0x%08X", serial);
		CPlayer_SystemMessage(player, msg);
	}

	// Re-send target cursor for next kill
	uint8_t tbuf[20];
	PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
	SendPacketToPlayer(player, tbuf, -1);
}

static void
GM_TargetMRemove(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	if (serial == 0) {
		player->targetCallback = NULL;
		CPlayer_SystemMessage(player, "Multi-remove done");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
	} else if (VT_IsPlayer(target)) {
		CPlayer_SystemMessage(player, "Cannot remove a player");
	} else {
		char msg[80];
		snprintf(msg, sizeof(msg), "Removed 0x%08X", serial);
		CWorld_DeleteEntity(g_World, target);
		CPlayer_SystemMessage(player, msg);
	}

	// Re-send target cursor for next remove
	uint8_t tbuf[20];
	PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
	SendPacketToPlayer(player, tbuf, -1);
}

static void
GM_TargetInfo(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Info cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}
	CLocation *tloc = CEntity_GetLocation(&target->resourceEntity.entity);
	uint16_t bodyType = target->resourceEntity.entity.bodyType;
	uint16_t color = target->resourceEntity.entity.color;
	char msg[200];

	// Line 1: serial, body type, tiledata name
	const char *tileName = CWorld_GetItemName(bodyType);
	snprintf(msg, sizeof(msg), "Serial=0x%08X Body=0x%04X (%s)", target->serial, bodyType, (tileName && tileName[0]) ? tileName : "?");
	CPlayer_SystemMessage(player, msg);

	// Line 2: location, hue
	snprintf(msg, sizeof(msg), "Loc=(%d,%d,%d) Hue=%d (0x%04X)", (int)(int16_t)tloc->x, (int)(int16_t)tloc->y, (int)tloc->z, (int)color, (unsigned)color);
	CPlayer_SystemMessage(player, msg);

	// Line 3: entity name (from vtable)
	const char *name = ((char *(*)(void *))VT_FN(target, VT_GET_NAME))(target);
	if (name && name[0]) {
		snprintf(msg, sizeof(msg), "Name=%s", name);
		CPlayer_SystemMessage(player, msg);
	}

	// Template ID (NPCs and templated items)
	int tid = CResourceEntity_GetTemplateIndex(target) & 0xFFFF;
	if (tid != 0xFFFF) {
		const char *tname = (tid < TEMPLATE_CHAIN_SIZE && g_TemplateNames[tid]) ? g_TemplateNames[tid] : NULL;
		if (!tname)
			tname = GMNameEntry_ReverseLookup(gm_template_names, GM_TEMPLATE_NAMES_COUNT, tid);
		snprintf(msg, sizeof(msg), "Template=%d (%s)", tid, tname ? tname : "?");
		CPlayer_SystemMessage(player, msg);
	}

	// Weapon/armor definition
	uint8_t weapId = CItem_GetWeaponDefId(target);
	if (weapId != 0) {
		const char *wname = GMNameEntry_ReverseLookup(gm_weapon_names, GM_WEAPON_NAMES_COUNT, (int)weapId);
		CWeaponDef *wdef = (weapId < MAX_WEAPON_DEFS && g_WeaponManager.data != NULL) ? g_WeaponManager.data[weapId] : NULL;
		if (wdef != NULL) {
			snprintf(msg, sizeof(msg),
			        "Weapon=%d (%s) Dmg=%dd%d+%d "
			        "AC=%dd%d+%d Spd=%d",
			        (int)weapId, wname ? wname : "?", (int)wdef->weaponClass.numDice, (int)wdef->weaponClass.diceFaces, (int)wdef->weaponClass.bonus,
			        (int)wdef->armorClass.numDice, (int)wdef->armorClass.diceFaces, (int)wdef->armorClass.bonus, (int)wdef->speed);
		} else {
			snprintf(msg, sizeof(msg), "Weapon=%d (%s)", (int)weapId, wname ? wname : "?");
		}
		CPlayer_SystemMessage(player, msg);
	}

	// Mobile-specific info
	if (VT_IsMobile(target)) {
		CMobile *mob = (CMobile *)target;
		snprintf(msg, sizeof(msg), "HP=%d/%d STR=%d DEX=%d INT=%d Invuln=%d", (int)mob->hp, (int)mob->maxHp, (int)mob->baseStr, (int)mob->baseDex, (int)mob->baseInt,
		        CMobile_IsInvulnerable(mob));
		CPlayer_SystemMessage(player, msg);
	}

	// Parent container
	if (target->parent != NULL) {
		snprintf(msg, sizeof(msg), "Parent=0x%08X", target->parent->serial);
		CPlayer_SystemMessage(player, msg);
	}
}

static void
GM_TargetScripts(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}
	char msg[256];
	const char *name = ((char *(*)(void *))VT_FN(target, VT_GET_NAME))(target);
	snprintf(msg, sizeof(msg), "--- %s (0x%08X) ---", name ? name : "(null)", target->serial);
	CPlayer_SystemMessage(player, msg);

	// List attached scripts
	CTagListManager *mgr = target->tagList;
	if (mgr == NULL || mgr->scriptList == NULL) {
		CPlayer_SystemMessage(player, "Scripts: (none)");
	} else {
		ScriptAttachNode *node = mgr->scriptList;
		int count = 0;
		while (node != NULL) {
			if (node->scriptClassPtr != NULL && (uintptr_t)node->scriptClassPtr != 0xABCD) {
				const char *sname = *(const char **)node->scriptClassPtr;
				snprintf(msg, sizeof(msg), "Script: %s", sname ? sname : "(null)");
				CPlayer_SystemMessage(player, msg);
				count++;
			}
			node = node->next;
		}
		if (count == 0)
			CPlayer_SystemMessage(player, "Scripts: (none)");
	}

	// List objvars (tags)
	if (mgr != NULL && mgr->head != NULL) {
		TagNode *tag = mgr->head;
		while (tag != NULL) {
			const char *tname = tag->name ? tag->name : "?";
			switch (tag->type) {
			case 0: // INT
				snprintf(msg, sizeof(msg), "ObjVar: %s = %d (int)", tname, (int)tag->value);
				break;
			case 1: // STRING
				snprintf(msg, sizeof(msg), "ObjVar: %s = \"%s\" (str)", tname, tag->value ? CString_GetBuffer((void *)(uintptr_t)tag->value) : "");
				break;
			case 4: // OBJ
				snprintf(msg, sizeof(msg), "ObjVar: %s = 0x%08lX (obj)", tname, (unsigned long)tag->value);
				break;
			default:
				snprintf(msg, sizeof(msg), "ObjVar: %s (type=%d)", tname, (int)tag->type);
				break;
			}
			CPlayer_SystemMessage(player, msg);
			tag = tag->next;
		}
	}
}

/*
 * Custom - GM_TargetResources
 *
 * Dumps the CResourceNode chain on the targeted entity, used by the
 * ecology template-data tests to verify that food/shelter/desire/
 * production nodes attach as expected.
 */
static void
GM_TargetResources(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}

	char msg[128];
	int counts[4] = { 0, 0, 0, 0 };
	CResourceNode *node;
	for (node = target->resourceEntity.firstChild; node != NULL; node = node->next) {
		if (node->type < 4)
			counts[node->type]++;
	}
	snprintf(msg, sizeof(msg), "ResCount food=%d shelter=%d desire=%d prod=%d", counts[0], counts[1], counts[2], counts[3]);
	CPlayer_SystemMessage(player, msg);

	for (node = target->resourceEntity.firstChild; node != NULL; node = node->next) {
		const char *label = "?";
		if (node->id < MAX_RESOURCE_TYPES && g_ResTypeEntries[node->id] != NULL) {
			label = CResourceType_GetInternalName(g_ResTypeEntries[node->id]);
		}
		snprintf(msg, sizeof(msg), "ResNode t=%d id=%d v1=%d v2=%d v3=%d %s", (int)node->type, (int)node->id, node->value1, node->value2, node->value3, label);
		CPlayer_SystemMessage(player, msg);
	}

	if (VT_IsMobile(target) && !VT_IsNPC(target)) {
		snprintf(msg, sizeof(msg), "MobGold gold=%u", (unsigned)CMobile_GetTotalQuantityOfType((CMobile *)target, 0xEED));
		CPlayer_SystemMessage(player, msg);
	}

	if (VT_IsNPC(target)) {
		CNPC *npc = (CNPC *)target;
		snprintf(msg, sizeof(msg), "NpcState state=%u walking=%u aitgt=%u rtype=%u rsrc=0x%08X action=0x%08X criminal=%u", (unsigned)npc->aiState, (unsigned)npc->isWalking,
		        (unsigned)npc->resourceAITarget, (unsigned)npc->resourceType, (unsigned)npc->resourceTargetSerial, (unsigned)npc->actionTarget,
		        (unsigned)CMobile_IsCriminal(&npc->mobile));
		CPlayer_SystemMessage(player, msg);
		snprintf(msg, sizeof(msg), "NpcHome x=%d y=%d z=%d loiter=%d,%d scanTimer=%u hoard=%u", (int)(int16_t)npc->homeLoc.x, (int)(int16_t)npc->homeLoc.y,
		        (int)(int16_t)npc->homeLoc.z, (int)(int16_t)npc->loiterLoc.x, (int)(int16_t)npc->loiterLoc.y, (unsigned)npc->scanTimer, (unsigned)npc->homeInfo3);
		CPlayer_SystemMessage(player, msg);
		snprintf(msg, sizeof(msg), "NpcBody stomach=%u hunger=%u cap=%u tick=%u gold=%u", (unsigned)npc->mobile.stomach, (unsigned)npc->mobile.hunger,
		        (unsigned)npc->hungerCapacity, (unsigned)npc->tickCount, (unsigned)CMobile_GetTotalQuantityOfType(&npc->mobile, 0xEED));
		CPlayer_SystemMessage(player, msg);
		snprintf(msg, sizeof(msg), "NpcPos x=%d y=%d z=%d", (int)(int16_t)target->resourceEntity.entity.location.x, (int)(int16_t)target->resourceEntity.entity.location.y,
		        (int)(int8_t)target->resourceEntity.entity.location.z);
		CPlayer_SystemMessage(player, msg);
	}
}

/*
 * Custom - GM_TargetAIState
 *
 * Force an NPC's aiState to the value stashed in g_PendingAIState.
 * Used by ecology tests to deterministically drive SEEK_SHELTER /
 * SEEK_DESIRES state transitions without waiting for IdleScan's
 * random 1/10 roll.
 */
static void
GM_TargetAIState(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsNPC(target)) {
		CPlayer_SystemMessage(player, "Target must be an NPC");
		return;
	}
	CNPC *npc = (CNPC *)target;
	npc->aiState = (uint32_t)g_PendingAIState;
	npc->isWalking = 0;
	npc->scanTimer = 0;
	char msg[96];
	snprintf(msg, sizeof(msg), "aiState=%u", (unsigned)npc->aiState);
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_ApplyForageMode
 *
 * Sets the "foragemode" objvar on an NPC. While it is non-zero,
 * CNPC_HandleStates makes the NPC roll SEEK_DESIRES from IDLE every
 * tick instead of the binary's 50% gate - a test aid that drives the
 * unforced ecology forage loop without forcing the aiState directly.
 */
static void
GM_ApplyForageMode(CPlayer *player, CItem *target, int enable)
{
	char msg[96];

	CEntity_SetObjVar(target, "foragemode", 0, (uintptr_t)(enable ? 1 : 0));
	snprintf(msg, sizeof(msg), "foragemode 0x%08X: %s", target->serial, enable ? "ON" : "OFF");
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_TargetForageMode
 *
 * Targeting callback for .foragemode: applies the staged on/off value
 * (g_PendingForageMode) to the picked NPC.
 */
static void
GM_TargetForageMode(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsNPC(target)) {
		CPlayer_SystemMessage(player, "Target must be an NPC");
		return;
	}
	GM_ApplyForageMode(player, target, g_PendingForageMode);
}

static void
GM_TargetFreeze(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Freeze cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	if (VT_IsPlayer(target))
		CPlayer_SetMovePrevented((CPlayer *)target, 1);
	if (VT_IsNPC(target))
		((CNPC *)target)->behaviorFlags = 0x1003F;
	char msg[80];
	snprintf(msg, sizeof(msg), "Froze 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetUnfreeze(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Unfreeze cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	if (VT_IsPlayer(target))
		CPlayer_SetMovePrevented((CPlayer *)target, 0);
	if (VT_IsNPC(target))
		((CNPC *)target)->behaviorFlags &= ~0x1003Fu;
	char msg[80];
	snprintf(msg, sizeof(msg), "Unfroze 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetMute(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Mute cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CMobile_SetStatusFlag((CMobile *)target, 4, 1);
	char msg[80];
	snprintf(msg, sizeof(msg), "Muted 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetUnmute(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Unmute cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CMobile_SetStatusFlag((CMobile *)target, 4, 0);
	char msg[80];
	snprintf(msg, sizeof(msg), "Unmuted 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetInvis(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Invis cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	((void (*)(CItem *, int))VT_FN(target, VT_SET_HIDDEN))(target, 1);
	char msg[80];
	snprintf(msg, sizeof(msg), "0x%08X is now invisible", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetVis(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Vis cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	((void (*)(CItem *, int))VT_FN(target, VT_SET_HIDDEN))(target, 0);
	char msg[80];
	snprintf(msg, sizeof(msg), "0x%08X is now visible", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetInvuln(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Invuln cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CMobile_SetStatusFlag((CMobile *)target, 1, 1);
	char msg[80];
	snprintf(msg, sizeof(msg), "0x%08X is now invulnerable", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetVuln(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Vuln cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CMobile_SetStatusFlag((CMobile *)target, 1, 0);
	char msg[80];
	snprintf(msg, sizeof(msg), "0x%08X is now vulnerable", serial);
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_TargetLight
 *
 * Sets a permanent personal light level (g_PendingLightVal) on the targeted
 * mobile via CMobile_SetLight with lightTime=0. The per-mobile light decay in
 * CTimeManager_Update only decrements mobiles with lightTime > 0, so 0 never
 * expires. Value 0 clears the effect. This is the same 0x4E personal-light
 * push the Night Sight spell produces, but without the spell's Q50G target
 * gate (so it works on counselor/invulnerable characters).
 */
static void
GM_TargetLight(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Light cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CMobile_SetLight((CMobile *)target, 0, g_PendingLightVal);
	char msg[80];
	snprintf(msg, sizeof(msg), "0x%08X personal light set to %d", serial, g_PendingLightVal);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetResurrect(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Resurrect cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsPlayer(target)) {
		CPlayer_SystemMessage(player, "Not a player");
		return;
	}
	CPlayer *tp = (CPlayer *)target;
	if (!CPlayer_IsDead(tp)) {
		CPlayer_SystemMessage(player, "Target is not dead");
		return;
	}
	CPlayer_ProcessDeath(tp);
	CPlayer_InstantResurrect(tp);
	char msg[80];
	snprintf(msg, sizeof(msg), "Resurrected 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetFillSpellbook(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Fill cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Item not found");
		return;
	}
	uint16_t bt = target->resourceEntity.entity.bodyType;
	if (bt != 0x0EFA && bt != 0x0E3B) {
		CPlayer_SystemMessage(player, "Not a spellbook");
		return;
	}
	int added = 0;
	int graphic;
	for (graphic = 0x1F2D; graphic <= 0x1F6C; graphic++) {
		uint32_t ser = Script_createGlobalObjectIn(graphic, target->serial);
		if (ser != 0)
			added++;
	}
	char msg[80];
	snprintf(msg, sizeof(msg), "Added %d spells to spellbook", added);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetBank(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Bank cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CMobile *mob = (CMobile *)target;
	CMobile_OpenBankGump(mob, player);
}

static void
GM_TargetSpawnNPC(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(serial);
	player->targetCallback = NULL;
	int templateId = g_PendingSpawnTemplate;
	g_PendingSpawnTemplate = -1;
	if (x == 0xFFFF && y == 0xFFFF) {
		CPlayer_SystemMessage(player, "Spawn cancelled");
		return;
	}
	if (templateId < 0) {
		CPlayer_SystemMessage(player, "No template selected");
		return;
	}
	int gz = (int)(int16_t)z;
	if (gz == 0) {
		int blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, (int)x, (int)y, 0);
		if (blockIdx >= 0)
			gz = (int)g_MapBlocks[blockIdx].cells[((int)y & 7) * 8 + ((int)x & 7)].z;
	}
	CLocation spawnLoc;
	spawnLoc.x = (int16_t)x;
	spawnLoc.y = (int16_t)y;
	spawnLoc.z = (int8_t)gz;
	CItem *npc = CTemplateManager_CreateFromTemplate((uint16_t)templateId, &spawnLoc, 0, 0, NULL);
	if (npc != NULL) {
		// Set fame/karma immediately if requested
		if (g_PendingSpawnFame != 0 || g_PendingSpawnKarma != 0) {
			CMobile *mob = (CMobile *)npc;
			CMobile_SetFame(mob, g_PendingSpawnFame);
			CMobile_SetKarma(mob, g_PendingSpawnKarma);
		}
		g_PendingSpawnFame = 0;
		g_PendingSpawnKarma = 0;
		// CUSTOM (FEAT_CLOSED_ECONOMY): mirror the SpawnAtPoint
		// bank deduct so a GM .spawn behaves symmetrically to a
		// natural spawn. Without this, a GM session can quietly
		// undo the closed-loop gate by creating NPCs whose type-3
		// production nodes are never accounted for.
		DeductSpawnFromBank((uint16_t)templateId, &spawnLoc);
		char spawnMsg[120];
		snprintf(spawnMsg, sizeof(spawnMsg), "Spawned %s (#%d) serial=0x%08X", g_TemplateNames[templateId] ? g_TemplateNames[templateId] : "?", templateId, npc->serial);
		CPlayer_SystemMessage(player, spawnMsg);
	} else {
		g_PendingSpawnFame = 0;
		g_PendingSpawnKarma = 0;
		CPlayer_SystemMessage(player, "Failed to spawn NPC");
	}
}

/*
 * Custom - GM_TargetHue
 */
static void
GM_TargetHue(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	int hue = g_PendingHue;
	g_PendingHue = -1;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Hue cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}
	if (hue < 0) {
		CPlayer_SystemMessage(player, "No hue selected");
		return;
	}
	Script_setHue(serial, hue);
	const char *hname = GMNameEntry_ReverseLookup(gm_hue_names, GM_HUE_NAMES_COUNT, hue);
	char msg[120];
	snprintf(msg, sizeof(msg), "Set hue %d (%s) on 0x%08X", hue, hname ? hname : "?", serial);
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_TargetMulti
 */
static void
GM_TargetMulti(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(serial);
	player->targetCallback = NULL;
	int multiType = g_PendingMultiType;
	g_PendingMultiType = -1;
	if (x == 0xFFFF && y == 0xFFFF) {
		CPlayer_SystemMessage(player, "Multi cancelled");
		return;
	}
	if (multiType < 0) {
		CPlayer_SystemMessage(player, "No multi selected");
		return;
	}
	CLocation loc;
	loc.x = (int16_t)x;
	loc.y = (int16_t)y;
	loc.z = (int8_t)z;
	CItem *multi = CMultiManager_Create(&g_MultiManager, multiType, &loc, 0, NULL);
	if (multi != NULL) {
		const char *mname = GMNameEntry_ReverseLookup(gm_multi_names, GM_MULTI_NAMES_COUNT, multiType);
		char msg[120];
		snprintf(msg, sizeof(msg), "Placed %s (type %d) serial=0x%08X", mname ? mname : "?", multiType, multi->serial);
		CPlayer_SystemMessage(player, msg);
	} else {
		CPlayer_SystemMessage(player, "Failed to place multi");
	}
}

/*
 * Custom - GM_TargetSay
 */
static void
GM_TargetSay(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Say cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}
	((void (*)(CItem *, char *, int, int, int))VT_FN(target, VT_SAY_CSTRING))(target, g_PendingSayBuf, -1, -1, -1);
}

/*
 * Custom - GM_TargetWalkDest
 *
 * Second step of .walk: select destination for the NPC to walk to.
 */
static void
GM_TargetWalkDest(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(serial);
	player->targetCallback = NULL;
	uint32_t npcSerial = g_PendingWalkNPC;
	g_PendingWalkNPC = 0;
	if (x == 0xFFFF && y == 0xFFFF) {
		CPlayer_SystemMessage(player, "Walk cancelled");
		return;
	}
	CItem *npc = CWorld_FindBySerial(g_World, npcSerial);
	if (npc == NULL || !VT_IsNPC(npc)) {
		CPlayer_SystemMessage(player, "NPC no longer valid");
		return;
	}
	CLocation dest;
	dest.x = (int16_t)x;
	dest.y = (int16_t)y;
	dest.z = (int8_t)z;
	// Use 8-direction pathfinder and running mode for GM command
	((CNPC *)npc)->aiByte3 = 1;
	CNPC_SetupPath8Dir((CNPC *)npc, &dest, 0x200);
	char msg[120];
	const char *name = ((char *(*)(void *))VT_FN(npc, VT_GET_NAME))(npc);
	snprintf(msg, sizeof(msg), "%s running to (%d,%d,%d)", name ? name : "NPC", (int)(int16_t)x, (int)(int16_t)y, (int)(int8_t)z);
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_TargetWalkNPC
 *
 * First step of .walk: select the NPC, then prompt for destination.
 */
static void
GM_TargetWalkNPC(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Walk cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsNPC(target)) {
		CPlayer_SystemMessage(player, "Target is not an NPC");
		return;
	}
	g_PendingWalkNPC = serial;
	uint8_t tbuf[20];
	player->targetCallback = GM_TargetWalkDest;
	PacketManager_MakePacket_TARGET(tbuf, 1, 0, 0);
	SendPacketToPlayer(player, tbuf, -1);
	const char *name = ((char *(*)(void *))VT_FN(target, VT_GET_NAME))(target);
	char msg[80];
	snprintf(msg, sizeof(msg), "Select destination for %s", name ? name : "NPC");
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_TargetAttackTarget
 *
 * Second step of .attack: select the target to attack.
 */
static void
GM_TargetAttackTarget(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	uint32_t npcSerial = g_PendingAttackNPC;
	g_PendingAttackNPC = 0;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Attack cancelled");
		return;
	}
	CItem *npc = CWorld_FindBySerial(g_World, npcSerial);
	if (npc == NULL || !VT_IsNPC(npc)) {
		CPlayer_SystemMessage(player, "NPC no longer valid");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsMobile(target)) {
		CPlayer_SystemMessage(player, "Target is not a mobile");
		return;
	}
	CNPC_EngageTarget((CNPC *)npc, (CMobile *)target);
	char msg[120];
	const char *aName = ((char *(*)(void *))VT_FN(npc, VT_GET_NAME))(npc);
	const char *tName = ((char *(*)(void *))VT_FN(target, VT_GET_NAME))(target);
	snprintf(msg, sizeof(msg), "%s attacking %s", aName ? aName : "NPC", tName ? tName : "target");
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_TargetAttackNPC
 *
 * First step of .attack: select the NPC attacker, then prompt for target.
 */
static void
GM_TargetAttackNPC(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Attack cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL || !VT_IsNPC(target)) {
		CPlayer_SystemMessage(player, "Target is not an NPC");
		return;
	}
	g_PendingAttackNPC = serial;
	uint8_t tbuf[20];
	player->targetCallback = GM_TargetAttackTarget;
	PacketManager_MakePacket_TARGET(tbuf, 0, 0, 0);
	SendPacketToPlayer(player, tbuf, -1);
	const char *name = ((char *(*)(void *))VT_FN(target, VT_GET_NAME))(target);
	char msg[80];
	snprintf(msg, sizeof(msg), "Select target for %s to attack", name ? name : "NPC");
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetLock(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Lock cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}
	CEntity_SetObjVar(target, "isLocked", 0, 1);
	char msg[80];
	snprintf(msg, sizeof(msg), "Locked 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

static void
GM_TargetUnlock(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Unlock cancelled");
		return;
	}
	CItem *target = CWorld_FindBySerial(g_World, serial);
	if (target == NULL) {
		CPlayer_SystemMessage(player, "Entity not found");
		return;
	}
	CEntity_SetObjVar(target, "isLocked", 0, 0);
	char msg[80];
	snprintf(msg, sizeof(msg), "Unlocked 0x%08X", serial);
	CPlayer_SystemMessage(player, msg);
}

/*
 * Custom - GM_TargetItemHP
 *
 * Click callback for .itemhp [VALUE]. Sets or reports weapon/armor curHP
 * on the clicked item, mirroring the behaviour of the serial form.
 */
static void
GM_TargetItemHP(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	int newVal = g_PendingItemHP;
	g_PendingItemHP = -1;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *tgt = CWorld_FindBySerial(g_World, serial);
	if (tgt == NULL) {
		CPlayer_SystemMessage(player, "Item not found");
		return;
	}
	if (CItem_GetWeaponDefId(tgt) == 0) {
		CPlayer_SystemMessage(player, "Not a weapon/armor");
		return;
	}
	char buf[80];
	if (newVal >= 0) {
		CWeapon_SetCurHP(tgt, (uint8_t)newVal);
		snprintf(buf, sizeof(buf), "Set HP: %d/%d", (int)CWeapon_GetCurHP(tgt), (int)CWeapon_GetMaxHP(tgt));
	} else {
		snprintf(buf, sizeof(buf), "HP: %d/%d", (int)CWeapon_GetCurHP(tgt), (int)CWeapon_GetMaxHP(tgt));
	}
	CPlayer_SystemMessage(player, buf);
}

/*
 * Custom - GM_TargetDurtest
 *
 * Click callback for .durtest. Forces a durability check (threshold=300)
 * on the clicked weapon/armor.
 */
static void
GM_TargetDurtest(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *tgt = CWorld_FindBySerial(g_World, serial);
	if (tgt == NULL) {
		CPlayer_SystemMessage(player, "Item not found");
		return;
	}
	if (CItem_GetWeaponDefId(tgt) == 0) {
		CPlayer_SystemMessage(player, "Not a weapon/armor");
		return;
	}
	int result = CItem_DamageDurability(tgt, 300, 0, 0, 0, -1);
	char buf[80];
	snprintf(buf, sizeof(buf), "Durability: %d/%d (result=%d)", (int)CWeapon_GetCurHP(tgt), (int)CWeapon_GetMaxHP(tgt), result);
	CPlayer_SystemMessage(player, buf);
	if (result == 0) {
		CPlayer_SystemMessage(player, "Item destroyed");
		((void (*)(void *))VT_FN(tgt, VT_DELETE))(tgt);
	}
}

/*
 * Custom - GM_TargetNpcInv
 *
 * Click callback for .npcinv. Lists items in the clicked mobile's
 * contents chain.
 */
static void
GM_TargetNpcInv(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *tgt = CWorld_FindBySerial(g_World, serial);
	if (tgt == NULL || !VT_IsMobile(tgt)) {
		CPlayer_SystemMessage(player, "Not a mobile");
		return;
	}
	CItem *child = ((CContainer *)tgt)->contents;
	int count = 0;
	char buf[160];
	while (child != NULL) {
		snprintf(buf, sizeof(buf), "  [%d] 0x%08X gfx=0x%04X", count, child->serial, (unsigned)CEntity_GetBodyType(child) & 0xFFFF);
		CPlayer_SystemMessage(player, buf);
		count++;
		child = child->spatialNext;
	}
	snprintf(buf, sizeof(buf), "npcinv 0x%08X: %d item(s)", serial, count);
	CPlayer_SystemMessage(player, buf);
}

/*
 * Custom - GM_TargetLockItem
 *
 * Click callback for .lockitem. Attaches the lockdown script to the
 * clicked item.
 */
static void
GM_TargetLockItem(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		return;
	}
	CItem *item = CWorld_FindBySerial(g_World, serial);
	if (item == NULL) {
		CPlayer_SystemMessage(player, "Item not found");
		return;
	}
	Entity_AttachScript(item, "lockdown", 0);
	CPlayer_SystemMessage(player, "Lockdown script attached");
}

/*
 * Custom - GM_TargetAttach
 *
 * Click callback for .attach SCRIPT. Attaches the staged script name to
 * the clicked item.
 */
static void
GM_TargetAttach(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (g_PendingAttachScript[0] == '\0') {
		CPlayer_SystemMessage(player, "No script staged");
		return;
	}
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		g_PendingAttachScript[0] = '\0';
		return;
	}
	CItem *item = CWorld_FindBySerial(g_World, serial);
	if (item == NULL) {
		CPlayer_SystemMessage(player, "Item not found");
		g_PendingAttachScript[0] = '\0';
		return;
	}
	Entity_AttachScript(item, g_PendingAttachScript, 1);
	char msg[160];
	snprintf(msg, sizeof(msg), "Attached %s", g_PendingAttachScript);
	CPlayer_SystemMessage(player, msg);
	g_PendingAttachScript[0] = '\0';
}

/*
 * Custom - GM_TargetSetVar
 *
 * Click callback for .setvar NAME VALUE. Sets the staged integer objvar
 * on the clicked entity.
 */
static void
GM_TargetSetVar(CPlayer *player, uint8_t type, uint32_t serial, uint16_t x, uint16_t y, uint16_t z)
{
	USED(type);
	USED(x);
	USED(y);
	USED(z);
	player->targetCallback = NULL;
	if (g_PendingSetVar.name[0] == '\0') {
		CPlayer_SystemMessage(player, "No var staged");
		return;
	}
	if (serial == 0) {
		CPlayer_SystemMessage(player, "Cancelled");
		g_PendingSetVar.name[0] = '\0';
		return;
	}
	CItem *item = CWorld_FindBySerial(g_World, serial);
	if (item == NULL) {
		CPlayer_SystemMessage(player, "Item not found");
		g_PendingSetVar.name[0] = '\0';
		return;
	}
	CEntity_SetObjVar(item, g_PendingSetVar.name, 0, (uintptr_t)g_PendingSetVar.value);
	char msg[160];
	snprintf(msg, sizeof(msg), "Set %s = %d", g_PendingSetVar.name, g_PendingSetVar.value);
	CPlayer_SystemMessage(player, msg);
	g_PendingSetVar.name[0] = '\0';
}
/*
 * Custom - GM_CreateReagentBag
 *
 * Creates a bag containing 50 of each standard reagent inside the
 * given container. Used by .create reagentbag meta-item.
 */
static void
GM_CreateReagentBag(CPlayer *player, CItem *container)
{
	static const uint16_t reagents[] = { 0x0F7A, 0x0F7B, 0x0F84, 0x0F85, 0x0F86, 0x0F88, 0x0F8C, 0x0F8D };
	uint32_t bagSer = Script_createGlobalObjectIn(0x0E76, container->serial);
	if (bagSer == 0) {
		CPlayer_SystemMessage(player, "Failed to create bag");
		return;
	}
	int i;
	for (i = 0; i < 8; i++) {
		uint32_t ser = Script_createGlobalObjectIn(reagents[i], bagSer);
		if (ser != 0)
			Script_addGlobalQuantity(ser, 49);
	}
	CPlayer_SystemMessage(player, "Created reagent bag (50 of each)");
}

/*
 * Custom - GM_CreateRuneBag
 *
 * Creates a bag containing one marked recall rune for each .go
 * destination inside the given container.
 */
static void
GM_CreateRuneBag(CPlayer *player, CItem *container)
{
	uint32_t bagSer = Script_createGlobalObjectIn(0x0E76, container->serial);
	if (bagSer == 0) {
		CPlayer_SystemMessage(player, "Failed to create bag");
		return;
	}
	int i, count;
	count = 0;
	for (i = 0; g_GoLocations[i].name != NULL; i++) {
		uint32_t runeSer = Script_createGlobalObjectIn(0x1F14, bagSer);
		if (runeSer == 0)
			continue;
		CItem *rune = CWorld_FindBySerial(g_World, runeSer);
		if (rune == NULL)
			continue;
		CLocation loc;
		CLocation_Set(&loc, g_GoLocations[i].x, g_GoLocations[i].y, (int16_t)g_GoLocations[i].z);
		CEntity_SetObjVar(rune, "markLoc", 3, (uintptr_t)&loc);
		char label[80];
		snprintf(label, sizeof(label), "%s", g_GoLocations[i].name);
		CString labelStr;
		CString_Constructor(&labelStr, label);
		CEntity_SetObjVar(rune, "lookAtText", 1, (uintptr_t)&labelStr);
		CString_Destructor(&labelStr);
		count++;
	}
	char msg[80];
	snprintf(msg, sizeof(msg), "Created rune bag (%d runes)", count);
	CPlayer_SystemMessage(player, msg);
}
/*
 * Helper - CHelpQueue_RemoveNode
 *
 * Removes and frees the given node; decrements count. Stands in for
 * std::list::erase (binary 0x0044F530).
 */
static void
CHelpQueue_RemoveNode(CHelpQueue *q, CHelpRequestNode *target)
{
	CHelpRequestNode **pp;

	for (pp = &q->head; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == target) {
			*pp = target->next;
			CString_Destructor(&target->callerName);
			CString_Destructor(&target->message);
			free(target);
			q->count--;
			return;
		}
	}
}

/*
 * Custom - TC_CommandDispatch
 *
 * Dispatch table for Test Center players (-test flag). Narrow surface
 * (.set / .set list / .where / .help / .resurrect, all self-targeted)
 * that reuses the same primitives as GmCommandDispatch. TC players never
 * reach GmCommandDispatch, so GM-only commands are unreachable by
 * construction.
 */
void
TC_CommandDispatch(CPlayer *player, const char *text)
{
	char cmd[256];

	if (text[0] == '\0')
		return;
	strncpy(cmd, text + 1, 254);
	cmd[255] = '\0';

	// .set <stat|skill|list> [value] - self only
	if (strncmp(cmd, "set ", 4) == 0) {
		const char *arg = cmd + 4;
		const char *p;
		int nlen;

		p = arg;
		while (*p && *p != ' ')
			p++;
		nlen = (int)(p - arg);
		if (nlen == 4 && strncasecmp(arg, "list", 4) == 0) {
			char line[256];
			int pos = 0;
			int i;
			CPlayer_SystemMessage(player, "Skills:");
			for (i = 0; i < MAX_SKILLS; i++) {
				const char *sn = CSkillManager_GetSkillName(&g_SkillManager, (int8_t)i);
				if (!sn)
					continue;
				char entry[100];
				int elen = snprintf(entry, sizeof(entry), "%d=%s", i, sn);
				if (pos + elen + 2 > (int)sizeof(line)) {
					CPlayer_SystemMessage(player, line);
					pos = 0;
				}
				if (pos > 0)
					line[pos++] = ' ';
				memcpy(line + pos, entry, elen);
				pos += elen;
				line[pos] = '\0';
			}
			if (pos > 0)
				CPlayer_SystemMessage(player, line);
			return;
		}

		int sType, sSkillId, sVal, sHasVal;
		char sStrArg[64];
		char smsg[80];
		GM_ParseSetArgs(arg, &sType, &sSkillId, &sVal, &sHasVal, sStrArg, sizeof(sStrArg));
		if (sType < 0) {
			CPlayer_SystemMessage(player, ".set <stat|skill|list> [VALUE]");
			return;
		}
		GM_ApplySet((CItem *)&player->mobile, sType, sSkillId, sVal, sHasVal, sStrArg, smsg, sizeof(smsg));
		CPlayer_SystemMessage(player, smsg);
		return;
	}

	// .where - self position
	if (strcmp(cmd, "where") == 0) {
		ShowEntityLocation((CItem *)player);
		return;
	}

	// .resurrect - self-resurrect when dead
	if (strcmp(cmd, "resurrect") == 0) {
		if (!CPlayer_IsDead(player)) {
			CPlayer_SystemMessage(player, "You are not dead");
			return;
		}
		CPlayer_ProcessDeath(player);
		CPlayer_InstantResurrect(player);
		CPlayer_SystemMessage(player, "Resurrected");
		return;
	}

	// .help - print the TC subset
	if (strcmp(cmd, "help") == 0) {
		CPlayer_SystemMessage(player, "Test Center commands:");
		CPlayer_SystemMessage(player, ".set <stat|skill> [VALUE] - set self stat/skill (e.g. .set str 100, .set magery 1000)");
		CPlayer_SystemMessage(player, ".set list - list all skill names and IDs");
		CPlayer_SystemMessage(player, ".where - report your position");
		CPlayer_SystemMessage(player, ".resurrect - resurrect yourself if dead");
		CPlayer_SystemMessage(player, ".help - this message");
		return;
	}

	CPlayer_SystemMessage(player, "Unknown Test Center command. Type .help for the list.");
}
