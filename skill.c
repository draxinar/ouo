/*
 * Skills - per-skill definitions and gain logic.
 *
 * Loads skills.txt into the skill manager, runs skill checks against
 * difficulty curves, awards skill and stat advancement on success, and
 * implements skill-specific side effects (meditation, stealth, remove
 * trap) behind the matching feature flags.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "combat.h"
#include "dat.h"
#include "feature.h"
#include "filemanager.h"
#include "io.h"
#include "main.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "skill.h"
#include "terrain.h"
#include "utils.h"
#include "version.h"
#include "vtable.h"

static void StaticInit_SkillManager(void); // 0x0046798F
static CSkillDef *CSkillDef_Constructor(CSkillDef *def); // 0x004D44B1
static void CSkillDef_Destructor(CSkillDef *def); // 0x004D458E
static int CSkillDef_GetStatWeight(CSkillDef *def, int statIndex); // 0x004D4599
static int CSkillDef_GetStatReq(CSkillDef *def, int statIndex); // 0x004D45E5
static void CSkillManager_InitCounters(CSkillManager *mgr); // 0x004D468F
static void CSkillManager_ResetCounters(CSkillManager *mgr); // 0x004D46EE
static void CSkillManager_Stub47A8(CSkillManager *mgr); // 0x004D47A8
static void CSkillManager_LoadSkillDefsInternal(CSkillManager *mgr); // 0x004D47D1
static int SkillCheck_ShouldLogDebug(CMobile *mob, int8_t skillId); // 0x004D50D2
static void CMobile_SkillGain(CMobile *mob, int8_t skillId, int chanceFactor, int gainPercent); // 0x004D5118
static int CSkillManager_CalcDelay(int8_t skillId, int baseSkill); // 0x004D55D2
// CMobile_TestSkillInternal declared in skill.h (non-static for CUSTOM_SKILL_MEDITATION)
static int CalcStatThreshold(CMobile *mob, int flag, int weight, int advRate, int skillValue, int baseStat); // 0x004D5A2C
static void CMobile_TryStatGain(CMobile *mob, int8_t skillId, int skillValue, int flag); // 0x004D5A5D
static int CMobile_GetBaseSkillValue(CMobile *mob, int8_t skillId); // 0x004D60CC
static int CMobile_PostStatDecay(CMobile *mob, int gained); // 0x004D62FD
static void Double3_Clear(CSkillDef *def); // 0x004D6400
static int CSkillDef_GetType(CSkillDef *def); // 0x004D6440
static const char *CSkillDef_GetScriptName(CSkillDef *def); // 0x004D6460
static int CSkillManager_GetTotalUsage(CSkillManager *mgr); // 0x004D6480
static int CSkillManager_GetSkillUsage(CSkillManager *mgr, int8_t skillId); // 0x004D64A0

CSkillManager g_SkillManager;

// Static data relocated from between functions (displaced by address sort).
uint32_t g_SkillDecayIterator;

/*
 * 0x0046798F - Static init wrapper
 *
 * Constructs g_SkillManager: all 50 skill entries and the shuffled
 * decay order array.
 */
static __attribute__((unused)) void
StaticInit_SkillManager(void)
{
	CSkillManager_Constructor(&g_SkillManager);
}

/*
 * 0x00473B80 - CSkillManager::GetMaxSkills
 *
 * Returns the number of loaded skills.
 */
int
CSkillManager_GetMaxSkills(CSkillManager *mgr)
{
	return mgr->numSkills;
}

/*
 * 0x00473BA0 - CSkillManager::GetSkillEntry
 *
 * Calls HasSkill but ignores result, always returns computed pointer.
 */
CSkillDef *
CSkillManager_GetSkillEntry(CSkillManager *mgr, int id)
{
	CSkillManager_HasSkill(mgr, id);
	return &mgr->skills[id];
}

/*
 * 0x0049D9D0 - SkillInfo::InitEmpty
 *
 * Loops 182 times with an empty body (compiler optimized away the stores).
 * Called from CEntityManager constructor.
 */
void
SkillInfo_InitEmpty(CEntityManager *self)
{
	int i;
	USED(self);
	for (i = 0; i < 0xB6; i++) {
		// Empty body - compiler optimized away the stores
	}
}

/*
 * 0x0049DEC0 - CSkillEntry::GetCanUse
 *
 * Returns canUse field: 1 if actively usable by the player, 0 if passive.
 */
int
CSkillEntry_GetCanUse(CSkillDef *entry)
{
	return entry->canUse;
}

/*
 * 0x00624530 - skill id to internal name table
 *
 * 46 {id, name} pairs terminated by a {0, NULL} sentinel. These are the
 * script-level skill identifiers, distinct from the display names in
 * skills.txt.
 */
static const struct {
	int id;
	const char *name;
} g_SkillNames[] = {
	{ 0x00, "ALCHEMY" },
	{ 0x01, "ANATOMY" },
	{ 0x02, "ANIMAL_LORE" },
	{ 0x03, "APPRAISE" },
	{ 0x04, "ARMSLORE" },
	{ 0x05, "BATTLE_DEFENSE" },
	{ 0x06, "BEGGING" },
	{ 0x07, "BLACKSMITH" },
	{ 0x08, "FLETCHER" },
	{ 0x09, "CALM" },
	{ 0x0A, "CAMPING" },
	{ 0x0B, "CARPENTRY" },
	{ 0x0C, "MAPMAKING" },
	{ 0x0D, "COOKING" },
	{ 0x0E, "DETECT_HIDDEN" },
	{ 0x0F, "ENTICE" },
	{ 0x10, "EVALUATE" },
	{ 0x11, "FIRST_AID" },
	{ 0x12, "FISHING" },
	{ 0x13, "FORENSICS" },
	{ 0x14, "HERDING" },
	{ 0x15, "HIDE" },
	{ 0x16, "INCITE" },
	{ 0x17, "INSCRIBE" },
	{ 0x18, "PICK_LOCK" },
	{ 0x19, "MAGIC" },
	{ 0x1A, "MAGIC_DEFENSE" },
	{ 0x1B, "FIGHTING" },
	{ 0x1C, "PEEK" },
	{ 0x1D, "PLAY" },
	{ 0x1E, "POISONING" },
	{ 0x1F, "WEAPON_RANGED" },
	{ 0x20, "SEANCE" },
	{ 0x21, "STEALING" },
	{ 0x22, "TAILOR" },
	{ 0x23, "TAME_ANIMAL" },
	{ 0x24, "TASTE" },
	{ 0x25, "TINKER" },
	{ 0x26, "TRACKING" },
	{ 0x27, "VET" },
	{ 0x28, "WEAPON_SLASHING" },
	{ 0x29, "WEAPON_BASHING" },
	{ 0x2A, "WEAPON_PIERCING" },
	{ 0x2B, "MELEE" },
	{ 0x2C, "LUMBERJACK" },
	{ 0x2D, "MINING" },
	{ 0, NULL },
};

/*
 * 0x004D4290 - Skill_GetInternalName
 *
 * Linear scan of the skill id table. Returns the internal name for
 * skillId, or NULL when the sentinel is reached first.
 */
static __attribute__((unused)) const char *
Skill_GetInternalName(int skillId)
{
	int i;

	for (i = 0; g_SkillNames[i].name != NULL; i++) {
		if (g_SkillNames[i].id == skillId)
			return g_SkillNames[i].name;
	}
	return NULL;
}

/*
 * 0x004D42D4 - SkillManager_GetSkillNumber
 *
 * Case-insensitive lookup of skill number by script name. Returns -1
 * if no name matched. The binary iterates a global skill name/number
 * table; our implementation iterates g_SkillManager.skills[].scriptName
 * which contains the same data.
 */
int
SkillManager_GetSkillNumber(const char *name)
{
	int i;

	for (i = 0; i < g_SkillManager.numSkills; i++) {
		if (g_SkillManager.skills[i].scriptName[0] == '\0')
			continue;
		if (strcasecmp(g_SkillManager.skills[i].scriptName, name) == 0)
			return i;
	}
	return -1;
}

/*
 * Skill Check System (0x004D50A8, 0x004D5118, 0x004D55D2, 0x004D5A5D, 0x004D52ED)
 */

/*
 * 0x004D4330 - Double3_Init
 *
 * Solves for f(x) = B * pow(A, x) + C where:
 *   f(0)         = lower
 *   f(maxSkill/2) = middle
 *   f(maxSkill)   = higher
 * Uses the quadratic formula on u = A^(maxSkill/2):
 *   disc = (lower-higher)^2 - 4*(middle-lower)*(higher-middle)
 *   root = (delta +/- sqrt(disc)) / (2*(middle-lower))
 * Then A = pow(root, 2/maxSkill), B = (higher-lower)/(pow(A,maxSkill)-1),
 * C = lower - B.
 *
 * FIXED: Binary bug in root selection. The quadratic always produces one
 * trivial root (1.0) and one non-trivial root. The binary picks the root
 * with larger exp(root-1), which is the trivial root (exp(0)=1.0) when
 * AdvRate curves are decreasing (e.g. 8000 2000 100 - fast gain at low
 * skill, slow at high). Selecting root 1.0 causes advB = -Inf (division
 * by pow(1,N)-1 = 0), advC = +Inf, and all Double3_Eval calls return NaN,
 * making skill gain zero for every skill. Fix: select the root whose
 * exp(root-1) is further from 1.0, which is always the non-trivial root.
 */
void
Double3_Init(CSkillDef *def, double maxSkill, double lower, double middle, double higher)
{
	double delta, disc, sqrtDisc, denom;
	double root1, root2;
	double exp1, exp2;
	double selectedRoot;

	delta = higher - lower;

	disc = (lower - higher) * (lower - higher) - 4.0 * (middle - lower) * (higher - middle);
	sqrtDisc = sqrt(disc);

	denom = 2.0 * (middle - lower);

	root1 = (delta + sqrtDisc) / denom;
	root2 = (delta - sqrtDisc) / denom;

	// FIXED: select root further from 1.0 (non-trivial root).
	exp1 = exp(root1 - 1.0);
	exp2 = exp(root2 - 1.0);
	if (fabs(exp1 - 1.0) > fabs(exp2 - 1.0))
		selectedRoot = root1;
	else
		selectedRoot = root2;

	def->advA = pow(selectedRoot, 2.0 / maxSkill);

	def->advB = (higher - lower) / (pow(def->advA, maxSkill) - 1.0);

	def->advC = lower - def->advB;
}

/*
 * 0x004D447C - Double3_Eval
 *
 * Computes pow(advA, x) * advB + advC.
 */
double
Double3_Eval(CSkillDef *def, double x)
{
	return pow(def->advA, x) * def->advB + def->advC;
}

/*
 * 0x004D44B1 - CSkillDef::CSkillDef (constructor)
 *
 * Zeroes all fields: advA/B/C via Double3_Clear, empty name and
 * scriptName, then zeroes each int field individually.
 */
static CSkillDef *
CSkillDef_Constructor(CSkillDef *def)
{
	Double3_Clear(def);
	strcpy(def->name, "");
	strcpy(def->scriptName, "");
	def->intReq = 0;
	def->dexReq = 0;
	def->strReq = 0;
	def->intWeight = 0;
	def->dexWeight = 0;
	def->strWeight = 0;
	def->statAdvRate = 0;
	def->skillStat = 0;
	def->canUse = 0;
	def->skillWeight = 0;
	def->type = 0;
	def->version = 0;
	return def;
}

/*
 * 0x004D458E - CSkillDef::~CSkillDef (destructor)
 *
 * No-op.
 */
static void
CSkillDef_Destructor(CSkillDef *def)
{
	USED(def);
}

/*
 * 0x004D4599 - CSkillDef::GetStatWeight
 *
 * Returns strWeight/dexWeight/intWeight by stat index (0/1/2).
 */
static int
CSkillDef_GetStatWeight(CSkillDef *def, int statIndex)
{
	switch (statIndex) {
	case 0:
		return def->strWeight;
	case 1:
		return def->dexWeight;
	case 2:
		return def->intWeight;
	default:
		return 0;
	}
}

/*
 * 0x004D45E5 - CSkillDef::GetStatReq
 *
 * Returns strReq/dexReq/intReq by stat index (0/1/2).
 * Called from TryStatGain but result is a dead parameter to
 * CalcStatThreshold.
 */
static int
CSkillDef_GetStatReq(CSkillDef *def, int statIndex)
{
	switch (statIndex) {
	case 0:
		return def->strReq;
	case 1:
		return def->dexReq;
	case 2:
		return def->intReq;
	default:
		return 0;
	}
}

/*
 * 0x004D4631 - Skill_NormalizeNamePair
 *
 * Splits src into two whitespace-separated tokens and reprints them
 * with a single space between, into a fresh 0x100-byte allocation.
 *
 * The two scan buffers are 256 bytes each and sscanf is unbounded, and
 * the result is printed into 0x100 bytes without a cap, so a long token
 * overruns either. Reproduced as it stands - nothing calls this.
 */
static __attribute__((unused)) char *
Skill_NormalizeNamePair(const char *src)
{
	char first[256];
	char second[256];
	char *out;

	out = (char *)OperatorNew(0x100);
	sscanf(src, "%s %s", first, second);
	sprintf(out, "%s %s", first, second);
	return out;
}

/*
 * 0x004D468F - CSkillManager::InitCounters
 *
 * Zeroes all per-skill and aggregate usage counters.
 */
static void
CSkillManager_InitCounters(CSkillManager *mgr)
{
	int i;

	for (i = 0; i < MAX_SKILLS; i++) {
		mgr->perSkillUsageCounter[i] = 0;
		mgr->perSkillSomeValue[i] = 0;
	}
	mgr->allSkillUsageCounter = 0;
	mgr->allSkillSomeTotal = 0;
}

/*
 * 0x004D46EE - CSkillManager::ResetCounters
 *
 * Wrapper that calls InitCounters.
 */
static void
CSkillManager_ResetCounters(CSkillManager *mgr)
{
	CSkillManager_InitCounters(mgr);
}

/*
 * 0x004D4701 - CSkillManager::LoadUsageCounters
 *
 * Reads 50 little-endian ints from src. Each is added to the matching
 * perSkillSomeValue entry, that entry is then zeroed, the running total
 * is stored into perSkillUsageCounter, and allSkillUsageCounter
 * accumulates it. allSkillSomeTotal is cleared at the end.
 *
 * The accumulator carries across iterations rather than restarting, so
 * perSkillUsageCounter ends up holding a prefix sum rather than the
 * per-skill value.
 */
static __attribute__((unused)) void
CSkillManager_LoadUsageCounters(CSkillManager *mgr, const char *src)
{
	int running;
	int i;

	CSkillManager_Stub47A8(mgr);
	mgr->allSkillUsageCounter = 0;
	running = 0;
	for (i = 0; i < MAX_SKILLS; i++) {
		memcpy(&running, src, 4);
		src += 4;
		running += mgr->perSkillSomeValue[i];
		mgr->perSkillSomeValue[i] = 0;
		mgr->perSkillUsageCounter[i] = running;
		mgr->allSkillUsageCounter += (uint32_t)running;
	}
	mgr->allSkillSomeTotal = 0;
}

/*
 * 0x004D47A8 - CSkillManager stub
 *
 * No-op.
 */
static __attribute__((unused)) void
CSkillManager_Stub47A8(CSkillManager *mgr)
{
	USED(mgr);
}

/*
 * 0x004D47B3 - CSkillManager::Update
 *
 * Empty stub.
 */
void
CSkillManager_Update(void)
{
}

/*
 * 0x004D47BE - CSkillManager::LoadSkillDefs
 *
 * Wrapper that calls CSkillManager_LoadSkillDefsInternal.
 */
void
CSkillManager_LoadSkillDefs(CSkillManager *mgr)
{
	CSkillManager_LoadSkillDefsInternal(mgr);
}

/*
 * 0x004D47D1 - CSkillManager::LoadSkillDefsInternal
 *
 * No-op stub.
 */
static void
CSkillManager_LoadSkillDefsInternal(CSkillManager *mgr)
{
	USED(mgr);
}

/*
 * 0x004D47DC - CSkillManager::LoadSkills
 *
 * Opens skills.txt, reads header (version, StatCurve, SkillCurve),
 * then reads each skill entry as a fixed sequence of fgets+sscanf calls.
 * Lines are parsed in strict order; the leading field name token is
 * discarded and only the value is used.
 *
 * File format per skill (fields in fixed order, separated by #... lines):
 *   Skill:        <name>
 *   Strength:     <int>         -> strWeight
 *   Dexterity:    <int>         -> dexWeight
 *   Intelligence: <int>         -> intWeight
 *   StrReq        <int>         -> strReq
 *   DexReq        <int>         -> dexReq
 *   IntReq        <int>         -> intReq
 *   AdvRate       <int int int> -> Double3_Init
 *   StatAdvRate   <int>         -> statAdvRate
 *   SkillStat:    <int>         -> skillStat
 *   CanUse:       <TRUE/FALSE>  -> canUse (checks 'F'/'f')
 *   SkillWeight:  <int>         -> skillWeight
 *   SkillScript:  <name>        -> scriptName
 *   Version:      <int>         -> version (only if header version > 0)
 *
 * Returns numSkills on success, 0 on failure.
 *
 * FIXED: Binary uses sprintf(cur->name, "%s", value) where value is a
 * 256-byte stack buffer but cur->name is only 80 bytes. A skill name
 * longer than 79 characters overflows into scriptName and subsequent
 * fields. Fix: strncpy with sizeof(cur->name) - 1 to truncate at the
 * buffer boundary.
 */
int
CSkillManager_LoadSkills(CSkillManager *mgr)
{
	FILE *f;
	char line[512];
	char name[256];
	char value[256];
	int skillIndex;
	int headerVersion;
	int headerFlag;
	int ival, ival2, ival3;
	int hdrStatCurve, hdrSkillCurve, hdrVersion;
	int sscanfRet;

	skillIndex = 0;
	headerVersion = 0;
	headerFlag = 1;

	f = FileManager_OpenByType(0x1C, NULL, "r");
	if (f == NULL)
		return 0;

	do {
		CSkillDef *cur;

		if (fgets_ServerSide(line, 0x1FF, f) == NULL)
			break;

		if (headerFlag == 1) {
			headerFlag = 0;

			sscanfRet = sscanf(line, "%s %d\n", name, &headerVersion);
			if (sscanfRet != 2)
				goto header_done;
			if (strcmp(name, "version") != 0)
				goto header_done;

			fgets_ServerSide(line, 0x1FF, f);
			sscanfRet = sscanf(line, "%s %d\n", name, &hdrStatCurve);
			if (sscanfRet != 2)
				goto header_skip;
			if (strcmp(name, "StatCurve") != 0)
				goto header_skip;
			InitStatCurve(hdrStatCurve);

			fgets_ServerSide(line, 0x1FF, f);
			sscanfRet = sscanf(line, "%s %d\n", name, &hdrSkillCurve);
			if (sscanfRet != 2)
				goto header_skip;
			if (strcmp(name, "SkillCurve") != 0)
				goto header_skip;
			InitSkillCurve(hdrSkillCurve);

			fgets_ServerSide(line, 0x1FF, f);
			goto header_end;

header_done:
			headerVersion = 0;
header_skip:;
header_end:;
		}

		cur = &mgr->skills[skillIndex];

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %[^\n]s\n", name, value);
		// FIXED: binary uses sprintf(cur->name, "%s", value) - overflow
		strncpy(cur->name, value, sizeof(cur->name) - 1);
		cur->name[sizeof(cur->name) - 1] = '\0';

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->strWeight = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->dexWeight = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->intWeight = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->strReq = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->dexReq = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->intReq = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d %d %d", name, &ival, &ival2, &ival3);
		Double3_Init(cur, 1000.0, (double)ival, (double)ival2, (double)ival3);

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->statAdvRate = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->skillStat = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %s", name, value);
		if (value[0] == 'F' || value[0] == 'f')
			cur->canUse = 0;
		else
			cur->canUse = 1;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %d", name, &ival);
		cur->skillWeight = ival;

		fgets_ServerSide(line, 0x1FF, f);
		sscanf(line, "%s %s", name, cur->scriptName);

		cur->type = 1;

		if (headerVersion > 0) {
			fgets_ServerSide(line, 0x1FF, f);
			sscanf(line, "%s %d", name, &hdrVersion);
			cur->version = hdrVersion;
		} else {
			cur->version = 0;
		}

		skillIndex++;
	} while (!feof_ServerSide(f));

	fclose_ServerSide(f);

	mgr->numSkills = skillIndex;
	return mgr->numSkills;
}

/*
 * 0x004D4FE8 - CSkillManager::SendSkillUpdate
 *
 * Sends a SKILLS_SINGLE packet to a player mob for the given skill.
 * Only sends if mob is a player.
 *
 * MODIFIED: Sends extended format (value + base + lock) for 1.26.2+
 * clients. The binary only sends skillID + value (old format).
 */
void
CSkillManager_SendSkillUpdate(CMobile *mob, int8_t skillId)
{
	uint8_t pktBuf[0x404];
	CPlayer *player;

	if (!VT_IsPlayer((CItem *)mob))
		return;

	player = (CPlayer *)mob;
	{
		uint16_t blended = (uint16_t)CMobile_GetSkillValue(mob, skillId, 0);
		uint16_t base = (uint16_t)CMobile_GetTotalSkill(mob, skillId);
		if (Version_GetConnVer(player->usersock, CLIENT_12602) >= CLIENT_12602) {
			uint8_t lock = feat(FEAT_SKILL_LOCK) ? player->skillLocks[(int)(uint8_t)skillId] : 0;
			PacketManager_MakePacket_SKILLS_SINGLE_Ext(pktBuf, (int16_t)skillId, blended, base, lock);
		} else {
			PacketManager_MakePacket_SKILLS_SINGLE(pktBuf, (int16_t)skillId, blended);
		}
	}
	SendToClient((CItem *)mob, pktBuf, -1);
}

/*
 * 0x004D5045 - CSkillManager::SendSkillList
 *
 * Sends a full SKILLS packet to a player entity. Only sends if entity
 * is a player.
 *
 * MODIFIED: Extended format (value+base+lock) for 1.26.2+ clients.
 */
void
CSkillManager_SendSkillList(CSkillManager *mgr, CItem *entity)
{
	CItem *player;
	uint8_t pktBuf[0x404];

	USED(mgr);
	if (!VT_IsPlayer(entity))
		return;

	player = entity;
	if (Version_GetConnVer(((CPlayer *)player)->usersock, CLIENT_12602) >= CLIENT_12602) {
		PacketManager_MakePacket_SKILLS_Ext(pktBuf, (int16_t)g_SkillManager.numSkills, player);
	} else {
		PacketManager_MakePacket_SKILLS(pktBuf, (int16_t)g_SkillManager.numSkills, player);
	}
	SendToClient(player, pktBuf, -1);
}

/*
 * 0x004D50A8 - CMobile::CalcChance
 *
 * Returns chance value (500 = even odds, >500 = easier, <500 = harder).
 * Formula: (skillValue - difficulty) * 100 / range + 500
 */
int
CMobile_CalcChance(CMobile *mob, int8_t skillId, int difficulty, int range)
{
	int skill;

	skill = CMobile_GetSkillValue(mob, skillId, 0);
	return (skill - difficulty) * 100 / range + 500;
}

/*
 * 0x004D50D2 - SkillCheck_ShouldLogDebug
 *
 * Returns 1 if the mob is a player with tag "debugSkillInfo" set to
 * the given skillId or to 666 (meaning log all skills).
 */
static int
SkillCheck_ShouldLogDebug(CMobile *mob, int8_t skillId)
{
	int val;

	if (!VT_IsPlayer((CItem *)mob))
		return 0;
	if (!CItem_GetTagInt((CItem *)mob, "debugSkillInfo", &val))
		return 0;
	if (val == (int)skillId || val == 0x29A)
		return 1;
	return 0;
}

/*
 * 0x004D5118 - CMobile::SkillGain
 *
 * Calculates skill advancement amount and applies it.
 * chanceFactor = 1000 - chance (higher = harder = more gain potential).
 * gainPercent scales the advance rate (100=normal, 50=vs player).
 * Performs probabilistic rounding of the fractional gain and caps the
 * result at 1000, then dispatches the skill-gain notification (player
 * path guards editing/counselor/GM; NPC path triggers weight-based decay).
 */
static void
CMobile_SkillGain(CMobile *mob, int8_t skillId, int chanceFactor, int gainPercent)
{
	int baseSkill;
	int advanceRate;
	int gainAmount;
	int wholeGain;
	int fracGain;
	int actualGain;

	if (chanceFactor <= 0)
		return;
	if (chanceFactor > 1000)
		chanceFactor = 1000;

	if (VT_IsPlayer((CItem *)mob)) {
		if (CPlayer_HasDeadFlag((CPlayer *)mob))
			return;
		if (feat(FEAT_SKILL_LOCK)) {
			// FEAT_SKILL_LOCK: locked or Down skills should not gain.
			if (((CPlayer *)mob)->skillLocks[(int)(uint8_t)skillId] != 0)
				return;
		}
	}

	baseSkill = CMobile_GetBaseSkill(mob, skillId);
	advanceRate = CSkillManager_CalcDelay(skillId, baseSkill);
	advanceRate = (unsigned)(advanceRate * gainPercent) / 100;

	if (SkillCheck_ShouldLogDebug(mob, skillId)) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Learn %s At: %d, Chance: %d, AdvRate: %d", CSkillManager_GetSkillName(&g_SkillManager, skillId), CMobile_GetSkillValue(mob, skillId, 1),
		        chanceFactor, advanceRate);
		CPlayer_SystemMessage((CPlayer *)mob, buf);
	}

	gainAmount = chanceFactor * advanceRate / 1000;
	if (baseSkill < 1000 && advanceRate > 0 && gainAmount == 0) {
		// FIXED: clamp truncation hole to 1 so all skills can reach 100.
		// Ultima Online Patch Notes for 11/10/1998
		// "It will be possible to reach 100 in any skill.
		// Previously, some skills couldn’t reach 100."
		gainAmount = 1;
	}

	// Custom: fast progression multiplier (-fast N).
	gainAmount *= g_GainMultiplier;
	wholeGain = gainAmount / 1000;
	fracGain = gainAmount % 1000;

	if (fracGain > GetRandom(1000))
		wholeGain++;

	if (wholeGain <= 0)
		return;

	// Binary calls GetSkillEntry here (result unused).
	CSkillManager_GetSkillEntry(&g_SkillManager, skillId);

	baseSkill = CMobile_GetBaseSkill(mob, skillId);
	if ((unsigned)(baseSkill + wholeGain) > 1000) {
		baseSkill = CMobile_GetBaseSkill(mob, skillId);
		wholeGain = 1000 - baseSkill;
	}

	actualGain = CMobile_AddToSkill(mob, skillId, wholeGain);

	((void (*)(void *, int, int))VT_FN((CItem *)mob, VT_SKILL_GAIN_NOTIFY))(mob, (int)skillId, actualGain);

	CSkillManager_SendSkillUpdate(mob, skillId);
}

/*
 * 0x004D52ED - CMobile::SkillCheck
 *
 * Performs a skill check against a difficulty. If successful (or alwaysGain),
 * awards skill and stat experience. Returns success value (>0 = success).
 * When divisor == 1 and the mob is a human body, shares the check with
 * nearby, facing, visible mobs via a recursive call at divisor 4.
 */
int
CMobile_SkillCheck(CMobile *mob, int8_t skillId, int difficulty, int range, int alwaysGain, int gainPercent, int divisor)
{
	int chance;
	int success;
	int baseSkill;

	if (VT_IsPlayer((CItem *)mob)) {
		if (CPlayer_HasDeadFlag((CPlayer *)mob))
			return 0;
	}

	chance = CMobile_CalcChance(mob, skillId, difficulty, range);

	if (divisor == 1) {
		mob->skillTimers[skillId] = g_GameTick;

		if (VT_IsPlayer((CItem *)mob))
			CSkillManager_RegisterUsage(skillId);

		{
			int statGainVal;
			baseSkill = CMobile_GetBaseSkill(mob, skillId);
			statGainVal = (unsigned)(CSkillManager_CalcDelay(skillId, baseSkill) * gainPercent) / 10000;
			CMobile_TryStatGain(mob, skillId, statGainVal, 1);
		}
	}

	success = chance / divisor - GetRandom(1000);

	if (SkillCheck_ShouldLogDebug(mob, skillId)) {
		char buf[256];
		sprintf(buf, "%s At:(%d/%d), Diff:%d, Focus:%d, Chance:%d, Success:%d", CSkillManager_GetSkillName(&g_SkillManager, skillId), CMobile_GetTotalSkill(mob, skillId),
		        CMobile_GetSkillValue(mob, skillId, 1), difficulty, range, chance, success);
		CPlayer_SystemMessage((CPlayer *)mob, buf);
	}

	if (alwaysGain == 0 && success <= 0)
		return success;

	CMobile_SkillGain(mob, skillId, 1000 - chance, gainPercent);

	// Nearby-mob skill sharing via CEntityMap_RangeQuery.
	if (divisor == 1 && CMobile_IsCreatureBody(mob)) {
		CVector nearbyList;
		char typeFlag;
		uintptr_t *iter;
		CLocation *selfLoc;

		CVector_Constructor(&nearbyList, &typeFlag);

		selfLoc = CEntity_GetLocation(&mob->container.item.resourceEntity.entity);
		CEntityMap_RangeQuery(g_ItemMap, &nearbyList, selfLoc->x, selfLoc->y, 1);

		for (iter = (uintptr_t *)nearbyList.begin; iter != (uintptr_t *)nearbyList.end; iter++) {
			CMobile *other = (CMobile *)*iter;
			if (other == mob)
				continue;
			if (!CEntity_CanSee(&other->container.item, &mob->container.item, 1))
				continue;
			if (!Mobile_IsFacingEntity(other, mob))
				continue;
			CMobile_SkillCheck(other, skillId, difficulty, range, 0, gainPercent, 4);
		}

		CVector_Destructor(&nearbyList);
	}

	return success;
}

/*
 * 0x004D55D2 - CSkillManager::CalcDelay
 *
 * Skill advancement rate for CMobile_SkillGain (not stat gain).
 * baseDelay = clamp(avgCap*1000 / (complexity+1), 500, 5000),
 * scaled by the exponential EvalAdvanceRate(baseSkill) curve.
 */
static int
CSkillManager_CalcDelay(int8_t skillId, int baseSkill)
{
	CSkillManager *mgr = &g_SkillManager;
	CSkillDef *def;
	int baseDelay;
	double advRate;
	uint32_t totalUsage;
	int avgCap;
	int perSkillUsage;

	totalUsage = mgr->allSkillSomeTotal + mgr->allSkillUsageCounter;
	avgCap = (int)(totalUsage / (uint32_t)mgr->numSkills);
	perSkillUsage = mgr->perSkillUsageCounter[skillId] + mgr->perSkillSomeValue[skillId];

	baseDelay = avgCap * 1000 / (perSkillUsage + 1);
	if (baseDelay < 500)
		baseDelay = 500;
	if (baseDelay > 5000)
		baseDelay = 5000;

	def = CSkillManager_GetSkillEntry(mgr, skillId);
	advRate = Double3_Eval(def, (double)baseSkill);

	return (int)((double)baseDelay * advRate / 1000.0);
}

/*
 * 0x004D567D - CMobile::TestSkillInternal
 *
 * Anti-macro gating + passive skill gain path. Each skill has a per-mob
 * repetition counter (skillCounts[]) that increments on use and decays
 * over time. Higher counter = less likely to gain (anti-macro).
 * AttrMod = (1000 - GetSkillValue(mob,skillId,1)) * gainFactor / 100.
 */
void
CMobile_TestSkillInternal(CMobile *mob, int8_t skillId, int gainFactor, int isUsingSkill)
{
	int attrMod;
	int baseSkill;

	if (!CSkillManager_HasSkill(&g_SkillManager, skillId))
		return;

	if (VT_IsPlayer((CItem *)mob)) {
		if (CPlayer_HasDeadFlag((CPlayer *)mob))
			return;
	}

	// Anti-macro counter decay.
	if (mob->skillTimers[skillId] == 0) {
		mob->skillCounts[skillId] = 0;
	} else {
		int elapsed = (int)(g_GameTick - mob->skillTimers[skillId]);
		if (elapsed >= 900) {
			mob->skillCounts[skillId] = 0;
		} else {
			int decayAmount = (int)((double)elapsed / 15.0 * 100.0);
			if ((int)mob->skillCounts[skillId] <= decayAmount)
				mob->skillCounts[skillId] = 0;
			else
				mob->skillCounts[skillId] -= (uint8_t)decayAmount;
		}
	}

	mob->skillCounts[skillId]++;
	if (isUsingSkill)
		mob->skillTimers[skillId] = g_GameTick;
	if (mob->skillCounts[skillId] > 100)
		mob->skillCounts[skillId] = 100;

	// Anti-macro gate.
	// Custom: relax anti-macro gating under fast progression (-fast N).
	// Without this, the anti-macro counter gates rapid training and the
	// skill gain multiplier would only yield ~2-3x effective speed.
	// Dividing the threshold by the multiplier lets repeated use benefit
	// from the full gain rate.
	if ((int)mob->skillCounts[skillId] > GetRandom(100) * g_GainMultiplier)
		return;

	baseSkill = CMobile_GetSkillValue(mob, skillId, 1);
	attrMod = (1000 - baseSkill) * gainFactor / 100;

	if (SkillCheck_ShouldLogDebug(mob, skillId)) {
		char buf[256];
		snprintf(buf, sizeof(buf), "%s At:(%d/%d), AttrMod:%d", CSkillManager_GetSkillName(&g_SkillManager, skillId), CMobile_GetTotalSkill(mob, skillId),
		        CMobile_GetSkillValue(mob, skillId, 1), gainFactor / 2);
		CPlayer_SystemMessage((CPlayer *)mob, buf);
	}

	CMobile_SkillGain(mob, skillId, attrMod, 100);

	if (isUsingSkill) {
		CMobile_TryStatGain(mob, skillId, gainFactor / 2, 1);
		CMobile_HandleWatchingSkill(mob, skillId, gainFactor);
	}
}

/*
 * 0x004D5916 - CMobile::HandleWatchingSkill
 *
 * Observational learning: nearby players passively gain skill by watching
 * a practitioner. Iterates g_MobileListHead, finds players within 1 tile
 * who are facing the practitioner, and feeds their gain through
 * TestSkillInternal with isUsingSkill=0 (prevents recursion and stat gain).
 * Dead practitioners cannot share; practitioner must be a human body.
 * Gain formula: watchGain = (selfEffective - otherBase) * gainFactor / 1000.
 */
void
CMobile_HandleWatchingSkill(CMobile *mob, int8_t skillId, int gainFactor)
{
	CMobile *other;
	CLocation *selfLoc;
	int selfSkill;

	if (VT_IsPlayer((CItem *)mob)) {
		if (CPlayer_HasDeadFlag((CPlayer *)mob))
			return;
	}

	if (!CMobile_IsCreatureBody(mob))
		return;

	selfSkill = CMobile_GetSkillValue(mob, skillId, 0);

	selfLoc = &mob->container.item.resourceEntity.entity.location;

	for (other = g_MobileListHead; other != NULL; other = other->nextMobile) {
		int otherSkill, watchGain;

		if (other == mob)
			continue;

		if (!VT_IsPlayer((CItem *)other))
			continue;

		if (CLocation_ChebyshevDistance(&other->container.item.resourceEntity.entity.location, selfLoc) > 1)
			continue;

		if (!CEntity_CanSee(&other->container.item, &mob->container.item, 1))
			continue;

		if (!Mobile_IsFacingEntity(other, mob))
			continue;

		otherSkill = CMobile_GetSkillValue(other, skillId, 1);

		if (selfSkill <= otherSkill)
			continue;

		watchGain = (selfSkill - otherSkill) * gainFactor / 1000;

		// isUsingSkill=0 prevents recursion, stat gain, and re-entering HandleWatchingSkill.
		CMobile_TestSkillInternal(other, skillId, watchGain, 0);
	}
}

/*
 * 0x004D5A2C - CalcStatThreshold
 *
 * Stat gain threshold: if flag > 0, inverts baseStat (100 - baseStat),
 * then returns baseStat * weight * skillValue / 1000.
 * The advRate parameter (from GetStatReq) is never read (dead arg).
 */
static int
CalcStatThreshold(CMobile *mob, int flag, int weight, int advRate, int skillValue, int baseStat)
{
	USED(mob);
	USED(advRate);
	if (flag > 0)
		baseStat = 100 - baseStat;
	return baseStat * weight * skillValue / 1000;
}

/*
 * 0x004D5A5D - CMobile::TryStatGain
 *
 * Iterates 3 stats (STR/DEX/INT) in two passes:
 * Pass 1: compute gain threshold and random roll for each stat.
 * Pass 2: apply gains where threshold > roll. Player path calls
 *   SetStatAbs(statIndex, baseStat + flag); NPC path calls
 *   AddToBaseStat(statIndex, flag).
 *
 * flag parameter starts at 1 (from all callers). Once any stat's
 * (baseStat + flag) > 100, flag is set to 0 for ALL remaining stats.
 */
static void
CMobile_TryStatGain(CMobile *mob, int8_t skillId, int skillValue, int flag)
{
	CSkillDef *def;
	int i;
	int threshold[3];
	int roll[3];
	int baseStat;
	int advRate;
	int weight;

	if (!CSkillManager_HasSkill(&g_SkillManager, skillId))
		return;

	def = CSkillManager_GetSkillEntry(&g_SkillManager, skillId);

	// Pass 1: compute thresholds and rolls.
	for (i = 0; i < 3; i++) {
		baseStat = (int)(int16_t)CMobile_GetBaseStat(mob, i);
		advRate = CSkillDef_GetStatReq(def, i);
		weight = CSkillDef_GetStatWeight(def, i);
		threshold[i] = CalcStatThreshold(mob, flag, weight, advRate, skillValue, baseStat);
		// Custom: fast progression multiplier (-fast N).
		threshold[i] *= g_GainMultiplier;
		roll[i] = GetRandom(20000);
	}

	// Pass 2: apply gains where threshold > roll.
	for (i = 0; i < 3; i++) {
		if (threshold[i] <= roll[i])
			continue;

		baseStat = (int)(int16_t)CMobile_GetBaseStat(mob, i);
		if (baseStat + flag > 100)
			flag = 0;

		// IsPlayer is checked per stat inside the loop, matching the binary.
		if (VT_IsPlayer((CItem *)mob)) {
			int newVal = (int)(int16_t)CMobile_GetBaseStat(mob, i) + flag;
			((void (*)(void *, int, int))VT_FN((CItem *)mob, VT_SET_STAT_ABS))(mob, i, newVal);
		} else {
			((void (*)(void *, int, int16_t))VT_FN((CItem *)mob, VT_ADD_BASE_STAT))(mob, i, (int16_t)flag);
		}
	}
}

/*
 * 0x004D5BA0 - CSkillManager::CSkillManager (constructor)
 *
 * Constructs all 50 CSkillDef entries, zeroes counters, and Fisher-Yates
 * shuffles skillDecayOrder (descending from 50 to 1).
 * Note: shuffle starts at index 50 (off-by-one in binary).
 */
CSkillManager *
CSkillManager_Constructor(CSkillManager *mgr)
{
	int i;

	for (i = 0; i < MAX_SKILLS; i++)
		CSkillDef_Constructor(&mgr->skills[i]);

	mgr->numSkills = 0;
	CSkillManager_InitCounters(mgr);
	CSkillManager_ResetCounters(mgr);

	for (i = 0; i < MAX_SKILLS; i++)
		mgr->skillDecayOrder[i] = (int8_t)i;

	// Fisher-Yates shuffle (binary off-by-one: starts at 50, not 49).
	for (i = MAX_SKILLS; i > 0; i--) {
		int j = GetRandom(i);
		SwapBytes(&mgr->skillDecayOrder[i], &mgr->skillDecayOrder[j]);
	}

	return mgr;
}

/*
 * 0x004D5C89 - CSkillManager::~CSkillManager (destructor)
 *
 * Destructs all 50 CSkillDef entries in reverse order. Since
 * CSkillDef_Destructor is a no-op, this is effectively a no-op.
 */
void
CSkillManager_Destructor(CSkillManager *mgr)
{
	int i;

	for (i = MAX_SKILLS - 1; i >= 0; i--)
		CSkillDef_Destructor(&mgr->skills[i]);
}

/*
 * 0x004D5CAE - CSkillManager::SendSkillNames
 *
 * Sends a SKILLS_NAMES packet (type 0xFE) to the given entity.
 * Zero callers in binary - orphaned dead code.
 */
void
CSkillManager_SendSkillNames(CSkillManager *mgr, CItem *entity)
{
	uint8_t pktBuf[0x404];
	int16_t numSkills;

	numSkills = (int16_t)mgr->numSkills;
	PacketManager_MakePacket_SKILLS_NAMES(pktBuf, (uint16_t)numSkills);
	SendToClient(entity, pktBuf, -1);
}

/*
 * 0x004D5CFD - CSkillManager::HasSkill
 *
 * Checks if skillId is a valid skill index with type == 1.
 */
int
CSkillManager_HasSkill(CSkillManager *mgr, int id)
{
	if (id < 0 || id >= mgr->numSkills)
		return 0;
	return mgr->skills[id].type == 1;
}

/*
 * 0x004D5D44 - CSkillManager::CanUseDirect
 *
 * Checks if skill can be used directly (not passive).
 * Returns the canUse flag (1 for TRUE, 0 for FALSE/passive).
 */
int
CSkillManager_CanUseDirect(CSkillManager *mgr, int id)
{
	if (!CSkillManager_HasSkill(mgr, id))
		return 0;
	return mgr->skills[id].canUse;
}

/*
 * 0x004D5D7F - CSkillManager::GetSkillName
 *
 * Returns pointer to CSkillDef.name, or NULL if skill ID is invalid.
 */
const char *
CSkillManager_GetSkillName(CSkillManager *mgr, int8_t skillId)
{
	if (!CSkillManager_HasSkill(mgr, (int)(int8_t)skillId))
		return NULL;
	return mgr->skills[(int)(int8_t)skillId].name;
}

/*
 * 0x004D5DBA - CSkillManager::GetSkillHandler
 *
 * Returns pointer to CSkillDef.scriptName, or NULL if skill ID is invalid.
 */
const char *
CSkillManager_GetSkillHandler(CSkillManager *mgr, int8_t skillId)
{
	if (!CSkillManager_HasSkill(mgr, (int)skillId))
		return NULL;
	return mgr->skills[(int)skillId].scriptName;
}

/*
 * 0x004D5DF5 - CSkillManager::RegisterUsage
 *
 * Tracks per-skill usage counters in CSkillManager. These feed into
 * CSkillManager_CalcDelay to make frequently-used skills advance more slowly
 * and rarely-used skills advance faster (auto-balancing mechanism).
 * When combined total overflows, all counters are right-shifted by 16.
 */
void
CSkillManager_RegisterUsage(int8_t skillId)
{
	CSkillManager *mgr = &g_SkillManager;

	if (!CSkillManager_HasSkill(mgr, skillId))
		return;

	// Check overflow BEFORE incrementing.
	if ((uint32_t)(mgr->allSkillSomeTotal + mgr->allSkillUsageCounter) == 0xFFFFFFFF) {
		int i;
		mgr->allSkillSomeTotal = 0;
		mgr->allSkillUsageCounter = 0;
		for (i = 0; i < MAX_SKILLS; i++) {
			mgr->perSkillUsageCounter[i] >>= 16;
			mgr->perSkillSomeValue[i] >>= 16;
			mgr->allSkillUsageCounter += mgr->perSkillUsageCounter[i];
			mgr->allSkillSomeTotal += mgr->perSkillSomeValue[i];
		}
	}

	// Increment AFTER overflow handling.
	mgr->perSkillUsageCounter[skillId]++;
	mgr->allSkillUsageCounter++;
}

/*
 * 0x004D5F09 - CMobile::DirectUse
 *
 * Direct skill use path for skills with no explicit difficulty
 * (e.g. Hiding, Detect Hidden, Spirit Speak). Tests skill against a
 * random roll, then passes to TestSkillInternal with gainFactor
 * 100 on success, 50 on failure (harder to gain from failure).
 */
int
CMobile_DirectUse(CMobile *mob, int8_t skillId)
{
	int effectiveSkill;
	int result;
	int gainFactor;

	if (!CSkillManager_HasSkill(&g_SkillManager, skillId))
		return 0;

	if (VT_IsPlayer((CItem *)mob)) {
		if (CPlayer_HasDeadFlag((CPlayer *)mob))
			return 0;
		CSkillManager_RegisterUsage(skillId);
	}

	// Binary calls GetSkillEntry here (result unused).
	CSkillManager_GetSkillEntry(&g_SkillManager, skillId);

	effectiveSkill = CMobile_GetSkillValue(mob, skillId, 0);
	result = effectiveSkill - GetRandom(1000);

	gainFactor = (result >= 0) ? 100 : 50;

	CMobile_TestSkillInternal(mob, skillId, gainFactor, 1);

	return result;
}

/*
 * 0x004D5FBB - CMobile::GetSkillValue
 *
 * Blends raw skill value with stat-weighted contribution.
 * The binary computes float versions of the skill value and stat weight
 * in dead locals (never read); the actual computation uses integer math.
 */
int
CMobile_GetSkillValue(CMobile *this, int8_t skillId, int useBaseOnly)
{
	CSkillDef *entry;
	int weightedStats;
	int scaledStatContrib;
	int skillPortion;
	float deadFloat;        // Written but never read.
	float deadFloat1;       // Written but never read.
	float deadFloat2;       // Written but never read.

	entry = CSkillManager_GetSkillEntry(&g_SkillManager, skillId);

	// Dead code: branch on useBaseOnly, convert skill to float.
	if (useBaseOnly)
		deadFloat = (float)(int64_t)CMobile_GetBaseSkill(this, skillId);
	else
		deadFloat = (float)(int64_t)CMobile_GetTotalSkill(this, skillId);

	// Dead code: float stat weight (stored but never read).
	deadFloat1 = (float)entry->skillStat * 10.0f;
	deadFloat2 = 1000.0f - deadFloat1;
	USED(deadFloat);
	USED(deadFloat2);

	weightedStats = (int)(uint16_t)this->baseStr * entry->strWeight + (int)(uint16_t)this->baseDex * entry->dexWeight + (int)(uint16_t)this->baseInt * entry->intWeight;

	scaledStatContrib = weightedStats * (100 - entry->skillStat) / 1000;

	skillPortion = CMobile_GetTotalSkill(this, skillId);
	skillPortion = (unsigned)skillPortion * (1000 - scaledStatContrib) / 1000;

	return skillPortion + scaledStatContrib;
}

/*
 * 0x004D60CC - CMobile::GetBaseSkillValue
 *
 * Wrapper that returns base skill value (without stat blend).
 */
static __attribute__((unused)) int
CMobile_GetBaseSkillValue(CMobile *mob, int8_t skillId)
{
	return CMobile_GetSkillValue(mob, skillId, 1);
}

/*
 * 0x004D60E7 - CMobile::PostSkillGain
 *
 * Probabilistic stat-weighted skill decay. Binary has NO total skill cap.
 * Instead, each gain triggers decay attempts proportional to the gained
 * amount. Skills aligned with the mob's weak stats decay faster, creating
 * natural specialization. Uses a global shuffled order table and a
 * persistent iterator to ensure fairness across calls.
 * Doubles gained for dead mobs. Returns 1 if any skill was decremented.
 * Called from the periodic maintenance loop, not from CMobile_SkillGain.
 */
int
CMobile_PostSkillGain(CMobile *mob, int gained)
{
	int strGap, dexGap, intGap;
	uint32_t loopEnd;
	int idx;
	int changed;

	if (VT_IsDead((CItem *)mob))
		gained *= 2;

	strGap = 100 - (int)(uint16_t)mob->baseStr;
	dexGap = 100 - (int)(uint16_t)mob->baseDex;
	intGap = 100 - (int)(uint16_t)mob->baseInt;

	changed = 0;

	if (gained > 10000)
		gained /= 10;

	// Small gains probabilistically skip decay entirely.
	if (gained < 1000) {
		if (GetRandom(1000) >= gained)
			return 0;
	}

	{
		int a = GetRandom(MAX_SKILLS);
		int b = GetRandom(MAX_SKILLS);
		SwapBytes(&g_SkillManager.skillDecayOrder[a], &g_SkillManager.skillDecayOrder[b]);
	}

	loopEnd = g_SkillDecayIterator + gained * MAX_SKILLS / 1000;

	while (g_SkillDecayIterator != loopEnd) {
		CSkillDef *def;
		int decayProb;
		int actualChange;

		if (gained <= 0)
			break;

		idx = (int)(uint8_t)g_SkillManager.skillDecayOrder[g_SkillDecayIterator % MAX_SKILLS];

		if (idx >= CSkillManager_GetMaxSkills(&g_SkillManager)) {
			g_SkillDecayIterator++;
			continue;
		}

		if (mob->skills[idx] == 0) {
			g_SkillDecayIterator++;
			continue;
		}

		if (feat(FEAT_SKILL_LOCK)) {
			// FEAT_SKILL_LOCK: Up or Locked skills should not decay.
			if (VT_IsPlayer((CItem *)mob)) {
				uint8_t lock = ((CPlayer *)mob)->skillLocks[idx];
				if (lock == 0 || lock == 2) {
					g_SkillDecayIterator++;
					continue;
				}
			}
		}

		def = CSkillManager_GetSkillEntry(&g_SkillManager, idx);

		decayProb = (def->strWeight * strGap + def->dexWeight * dexGap + def->intWeight * intGap) * (int)mob->skills[idx] / 10000;

		if (decayProb > 0)
			gained -= decayProb;

		if (GetRandom(1000) < decayProb) {
			actualChange = CMobile_AddToSkill(mob, (int8_t)idx, -1);
			((void (*)(void *, int, int))VT_FN((CItem *)mob, VT_SKILL_GAIN_NOTIFY))(mob, idx, actualChange);
			changed = 1;
		}

		g_SkillDecayIterator++;
	}

	return changed;
}
/*
 * 0x004D62FD - CMobile::PostStatDecay
 *
 * Stat decay companion to PostSkillGain. Doubles gained if dead.
 * For gains < 1000, probabilistically skips. Otherwise iterates 3 stats,
 * each for (gained / 1000) rounds: roll = GetRandom(90) + 10, and if
 * roll < baseStat, decrements that stat by 1.
 * Returns 1 if any stat was decremented, 0 otherwise.
 */
static __attribute__((unused)) int
CMobile_PostStatDecay(CMobile *mob, int gained)
{
	int changed;
	int i;
	int budget;

	if (VT_IsDead((CItem *)mob))
		gained *= 2;

	if (gained < 1000) {
		if (GetRandom(1000) >= gained)
			return 0;
	}

	changed = 0;

	for (i = 0; i < 3; i++) {
		for (budget = 0; budget < gained; budget += 1000) {
			int roll;
			int baseStat;

			roll = GetRandom(90) + 10;
			baseStat = (int)(int16_t)CMobile_GetBaseStat(mob, i);
			if (roll < baseStat) {
				((void (*)(void *, int, int16_t))VT_FN((CItem *)mob, VT_ADD_BASE_STAT))(mob, i, (int16_t)-1);
				changed = 1;
			}
		}
	}

	return changed;
}

/*
 * 0x004D62FD - CMobile::StatDecayCheck
 *
 * If mob is dead, doubles chance. If chance < 1000, probabilistically
 * skips. Then for each of 3 base stats, iterates in steps of 1000 up
 * to chance: rolls random(90)+10; if roll < baseStat, decrements that
 * stat by 1. Returns 1 if any stat was decremented.
 */
int
CMobile_StatDecayCheck(CMobile *this, int chance)
{
	int anyDecayed;
	int stat;
	int inner;
	int roll;

	if (VT_IsDead((CItem *)this))
		chance <<= 1;

	if (chance < 0x3E8) {
		if (GetRandom(0x3E8) >= chance)
			return 0;
	}

	anyDecayed = 0;

	for (stat = 0; stat < 3; stat++) {
		for (inner = 0; inner < chance; inner += 0x3E8) {
			roll = GetRandom(0x5A) + 0x0A;
			if (roll < (int16_t)CMobile_GetBaseStat(this, stat)) {
				((void (*)(void *, int, int))VT_FN((CItem *)this, VT_ADD_BASE_STAT))(this, stat, -1);
				anyDecayed = 1;
			}
		}
	}

	return anyDecayed;
}

/*
 * 0x004D6400 - Double3_Clear
 *
 * Zeroes advA, advB, advC.
 */
static void
Double3_Clear(CSkillDef *def)
{
	def->advA = 0.0;
	def->advB = 0.0;
	def->advC = 0.0;
}

/*
 * 0x004D6440 - CSkillDef::GetType
 *
 * Returns this->type.
 */
static __attribute__((unused)) int
CSkillDef_GetType(CSkillDef *def)
{
	return def->type;
}

/*
 * 0x004D6460 - CSkillDef::GetScriptName
 *
 * Returns this->scriptName.
 */
static __attribute__((unused)) const char *
CSkillDef_GetScriptName(CSkillDef *def)
{
	return def->scriptName;
}

/*
 * 0x004D6480 - CSkillManager::GetTotalUsage
 *
 * Returns allSkillUsageCounter + allSkillSomeTotal.
 */
static __attribute__((unused)) int
CSkillManager_GetTotalUsage(CSkillManager *mgr)
{
	return mgr->allSkillUsageCounter + mgr->allSkillSomeTotal;
}

/*
 * 0x004D64A0 - CSkillManager::GetSkillUsage
 *
 * Calls HasSkill (result ignored), returns
 * perSkillUsageCounter[skillId] + perSkillSomeValue[skillId].
 */
static __attribute__((unused)) int
CSkillManager_GetSkillUsage(CSkillManager *mgr, int8_t skillId)
{
	CSkillManager_HasSkill(mgr, (int)(int8_t)skillId);
	return mgr->perSkillUsageCounter[(int)(int8_t)skillId] + mgr->perSkillSomeValue[(int)(int8_t)skillId];
}
