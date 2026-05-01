#ifndef MAIN_H_
#define MAIN_H_

#include <signal.h>
#include <stdint.h>

__extension__ typedef struct AnimDataEntry AnimDataEntry;
__extension__ typedef struct HueEntry HueEntry;
__extension__ typedef struct HueEntryExpanded HueEntryExpanded;
__extension__ typedef struct QuadCurve QuadCurve;
extern QuadCurve g_StatCurve; // 0x0061C1E0
extern QuadCurve g_SkillCurve; // 0x0061C210
extern char g_LVNDestBuf[256]; // 0x00621998
extern uint32_t g_LoadingComplete; // 0x0063E168
extern AnimDataEntry *g_AnimData; // 0x0064B35C
extern HueEntry *g_HueData; // 0x0064B360
extern HueEntryExpanded *g_HueDataExp; // 0x0064B370
extern int g_ArtLoaded; // 0x006982FC
extern int g_TileDataLoaded; // 0x00698300
extern int g_HueDataLoaded; // 0x00698304
extern int g_AnimDataLoaded; // 0x0069830C
extern uint32_t g_TimingBuffer[128]; // 0x00699710
extern uint32_t g_TimingBufIndex; // 0x006999E8
extern uint32_t g_TimingField9EC; // 0x006999EC
extern uint32_t g_TimingField9F0; // 0x006999F0
extern uint32_t g_LastTickElapsed; // 0x006999F4
extern uint32_t g_TimingTotalTime; // 0x006999F8
extern uint32_t g_TimingTickCount; // 0x006999FC
extern uint32_t g_TimingPeakTime; // 0x00699A00
extern uint32_t g_GameTick; // 0x00699A04
extern double g_StatCurveBase; // 0x00699A58
extern double g_SkillCurveBase; // 0x00699A60
extern uint32_t g_PoolBaseField_C4; // 0x006F05C4
extern uint32_t g_PoolBlockCount2; // 0x006F05C8
extern uint32_t g_PoolBaseField_CC; // 0x006F05CC
extern uint32_t g_PoolSizeField_D0; // 0x006F05D0
extern uint32_t g_PoolSizeField_D4; // 0x006F05D4
extern uint32_t g_PoolSizeField_D8; // 0x006F05D8
extern uint32_t g_PoolBaseItems; // 0x006F05DC
extern uint32_t g_PoolSizeField_E0; // 0x006F05E0
extern uint32_t g_PoolBaseField_E4; // 0x006F05E4
extern uint32_t g_TileDataFileOffset; // 0x006F05E8
extern uint32_t g_PoolBaseField_EC; // 0x006F05EC
extern uint32_t g_PoolBlockCount; // 0x006F05F0
extern uint32_t g_PoolSizeItems; // 0x006F05F4
extern uint32_t g_PoolBaseField_F8; // 0x006F05F8
extern uint32_t g_PoolBaseField_FC; // 0x006F05FC
extern uint32_t g_PoolSizeNPCs; // 0x006F0600
extern uint32_t g_HueFileOffset; // 0x006F0604
extern uint32_t g_PoolTotalSerials; // 0x006F0608
extern uint32_t g_PoolSizeField_0C; // 0x006F060C
extern uint32_t g_PoolSizeField_10; // 0x006F0610
extern volatile sig_atomic_t g_shutdown;
extern uint32_t g_AutoInitialSpawnDeadline;

int Server_Loop(void); // 0x00467FF0
void CDebugConsole_AppendText(const char *msg); // 0x004D06E6
void ProgressBar_Update(int percent); // 0x004D0720
void InitPoolSizes(void); // 0x004DE360
int Server_Init(void);
int main(int argc, char **argv);

#endif /* MAIN_H_ */
