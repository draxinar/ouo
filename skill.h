#ifndef SKILL_H_
#define SKILL_H_

#include <stdint.h>

__extension__ typedef struct CEntityManager CEntityManager;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CMobile CMobile;
#define MAX_SKILLS 50

__extension__ typedef struct CSkillDef CSkillDef;
__extension__ typedef struct CSkillManager CSkillManager;

/*
 * One skill definition (0xE8 bytes). An array of these lives inside
 * g_SkillManager at +0x198.
 */
struct CSkillDef {
	char name[80];       // 0x00
	char scriptName[80]; // 0x50 (Wombat handler)
	int32_t strReq;      // 0xA0
	int32_t dexReq;      // 0xA4
	int32_t intReq;      // 0xA8
	int32_t strWeight;   // 0xAC (0-100)
	int32_t dexWeight;   // 0xB0 (0-100)
	int32_t intWeight;   // 0xB4 (0-100)
	double advA;         // 0xB8 (pow() base from Double3_Init)
	double advB;         // 0xC0
	double advC;         // 0xC8
	int32_t statAdvRate; // 0xD0
	int32_t skillStat;   // 0xD4
	int32_t canUse;      // 0xD8 (1=active, 0=passive)
	int32_t skillWeight; // 0xDC
	int32_t type;        // 0xE0 (1=valid)
	int32_t version;     // 0xE4
};

/*
 * Singleton skill system at g_SkillManager (0x00648418): 50 skill
 * definitions plus per-skill and global usage counters.
 */
// clang-format off
struct CSkillManager {
	int32_t perSkillSomeValue[MAX_SKILLS];    // 0x000
	int32_t perSkillUsageCounter[MAX_SKILLS]; // 0x0C8
	uint32_t allSkillSomeTotal;               // 0x190 (sum of perSkillSomeValue[])
	uint32_t allSkillUsageCounter;            // 0x194 (sum of perSkillUsageCounter[])
	CSkillDef skills[MAX_SKILLS];             // 0x198
	int32_t numSkills;                        // 0x2EE8
	int8_t skillDecayOrder[MAX_SKILLS];       // 0x2EEC (shuffled decay order)
};
// clang-format on

/*
 * Piecewise-quadratic curve (6 doubles, 48 bytes). Each segment is
 * chance = (x - xOff)^2 * coeff + base; CMobile_OnStatChange_VT and
 * TryStatGain pick a segment by threshold.
 */
__extension__ typedef struct QuadCurve QuadCurve;
struct QuadCurve {
	double xOff1;   // +0x00
	double coeff1;  // +0x08
	double thresh;  // +0x10
	double midY;    // +0x18
	double xOff2;   // +0x20
	double coeff2;  // +0x28
};

extern CSkillManager g_SkillManager;
extern uint32_t g_SkillDecayIterator;

int CSkillManager_GetMaxSkills(CSkillManager *mgr); // 0x00473B80
CSkillDef *CSkillManager_GetSkillEntry(CSkillManager *mgr, int id); // 0x00473BA0
void SkillInfo_InitEmpty(CEntityManager *self); // 0x0049D9D0
int CSkillEntry_GetCanUse(CSkillDef *entry); // 0x0049DEC0
int SkillManager_GetSkillNumber(const char *name); // 0x004D42D4
void Double3_Init(CSkillDef *def, double maxSkill, double lower, double middle, double higher); // 0x004D4330
double Double3_Eval(CSkillDef *def, double x); // 0x004D447C
void CSkillManager_Update(void); // 0x004D47B3
void CSkillManager_LoadSkillDefs(CSkillManager *mgr); // 0x004D47BE
int CSkillManager_LoadSkills(CSkillManager *mgr); // 0x004D47DC
void CSkillManager_SendSkillUpdate(CMobile *mob, int8_t skillId); // 0x004D4FE8
void CSkillManager_SendSkillList(CSkillManager *mgr, CItem *entity); // 0x004D5045
int CMobile_CalcChance(CMobile *mob, int8_t skillId, int difficulty, int range); // 0x004D50A8
int CMobile_SkillCheck(CMobile *mob, int8_t skillId, int difficulty, int range, int alwaysGain, int gainPercent, int divisor); // 0x004D52ED
void CMobile_HandleWatchingSkill(CMobile *mob, int8_t skillId, int gainFactor); // 0x004D5916
CSkillManager *CSkillManager_Constructor(CSkillManager *mgr); // 0x004D5BA0
void CSkillManager_Destructor(CSkillManager *mgr); // 0x004D5C89
void CSkillManager_SendSkillNames(CSkillManager *mgr, CItem *entity); // 0x004D5CAE
int CSkillManager_HasSkill(CSkillManager *mgr, int id); // 0x004D5CFD
int CSkillManager_CanUseDirect(CSkillManager *mgr, int id); // 0x004D5D44
const char *CSkillManager_GetSkillName(CSkillManager *mgr, int8_t skillId); // 0x004D5D7F
const char *CSkillManager_GetSkillHandler(CSkillManager *mgr, int8_t skillId); // 0x004D5DBA
void CSkillManager_RegisterUsage(int8_t skillId); // 0x004D5DF5
void CMobile_TestSkillInternal(CMobile *mob, int8_t skillId, int gainFactor, int isUsingSkill); // 0x004D567D
int CMobile_DirectUse(CMobile *mob, int8_t skillId); // 0x004D5F09
int CMobile_GetSkillValue(CMobile *this, int8_t skillId, int useBaseOnly); // 0x004D5FBB
int CMobile_PostSkillGain(CMobile *mob, int gained); // 0x004D60E7
int CMobile_StatDecayCheck(CMobile *this, int chance); // 0x004D62FD

#endif /* SKILL_H_ */
