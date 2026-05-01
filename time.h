#ifndef TIME_H_
#define TIME_H_

#include <stdint.h>

__extension__ typedef struct CString CString;
__extension__ typedef struct TwofishCipher TwofishCipher;
__extension__ typedef struct CTimeManager CTimeManager;
__extension__ typedef struct ScriptAttachNode ScriptAttachNode;

/*
 * Linked-list node for a timed-event subscription (12 bytes, malloc'd).
 * Each time slot (minute 0..59, hour 0..23, ...) owns one singly-linked
 * list; DispatchEventList walks it and fires event 0x0E on each
 * subscriber's entity.
 */
__extension__ typedef struct CTimeEventNode CTimeEventNode;
struct CTimeEventNode {
	void *id;             // +0x00 (ScriptAttachNode* subscriber)
	const char *data;     // +0x04 (time expression)
	CTimeEventNode *next; // +0x08
};

/*
 * Singleton game-clock / event dispatcher at g_TimeManager (0x006482A8).
 * Owns moon phases, calendar fields, and six per-slot subscriber tables
 * driving the periodic event system.
 */
struct CTimeManager {
	uint32_t vtableSlot;           // +0x00
	uint32_t lastTrammelPhase;     // +0x04
	uint32_t lastFeluccaPhase;     // +0x08
	uint32_t tickCount;            // +0x0C
	uint32_t month;                // +0x10
	uint32_t week;                 // +0x14
	uint32_t day;                  // +0x18
	uint32_t hour;                 // +0x1C
	uint32_t minute;               // +0x20
	uint32_t seconds;              // +0x24
	uint32_t year;                 // +0x28
	uint32_t currentTime;          // +0x2C
	uint8_t _pad30[0x24];          // +0x30
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4];             // 64-bit alignment pad
#endif
	CTimeEventNode **minuteEvents; // +0x54
	CTimeEventNode **hourEvents;   // +0x58
	CTimeEventNode **dayEvents;    // +0x5C
	CTimeEventNode **weekEvents;   // +0x60
	CTimeEventNode **monthEvents;  // +0x64
	CTimeEventNode **yearEvents;   // +0x68
};

// Binary aliases: these addresses are fields within g_TimeManager.
#define g_GameMonth   ((int)g_TimeManager.month)   /* 0x006482B8 */
#define g_GameWeek    ((int)g_TimeManager.week)    /* 0x006482BC */
#define g_GameDay     ((int)g_TimeManager.day)     /* 0x006482C0 */
#define g_GameHour    ((int)g_TimeManager.hour)    /* 0x006482C4 */
#define g_GameMinute  ((int)g_TimeManager.minute)  /* 0x006482C8 */
#define g_GameSeconds ((int)g_TimeManager.seconds) /* 0x006482CC */
#define g_GameYear    ((int)g_TimeManager.year)    /* 0x006482D0 */

extern int32_t g_globalLightLevel; // 0x00648290
extern int g_BBoardCount; // 0x0068B394
extern int32_t g_moonPhaseLightTable[8]; // 0x00699A78
extern int32_t g_hourlyLightTable[24]; // 0x00699A98
extern CTimeManager g_TimeManager;

void CLightManager_Constructor(void); // 0x00473BD0
void CLightManager_Destructor(void); // 0x00473C29
int CTimeManager_GetTrammelPhase(void); // 0x00473C3F
int CTimeManager_GetFeluccaPhase(void); // 0x00473C74
void GlobalLightManager_Tick(uint32_t arg); // 0x00473CCD
void CLightManager_RegisterScript(ScriptAttachNode *scriptNode); // 0x00473DCF
void CLightManager_UnregisterScript(ScriptAttachNode *scriptNode); // 0x00473DEB
void CLightManager_NotifyMoonChange(int trammelChanged, int feluccaChanged); // 0x00473E34
void CTimeManager_Init(void); // 0x004D7F23
void CTimeManager_LogEntityStats(int arg); // 0x004D805C
void CTimeManager_Update(void); // 0x004D81B8
void CTimeManager_RegisterTimedEvent(ScriptAttachNode *id, const char *expression); // 0x004D85A8
void CTimeManager_UnregisterTimedEvent(ScriptAttachNode *id, const char *expression); // 0x004D8795
void CTimeManager_Tick(void); // 0x004D8A9A
void CTimeManager_CheckMoonPhases(void); // 0x004D8C5D
void TwofishCipher_Init(TwofishCipher *tc, uint32_t keyParam); // 0x004D8CA0
uint32_t CTimeManager_GetTickCount(void); // 0x004D8CE6
void CTimeManager_FormatTimestamp(CString *dest); // 0x004D8CF7
void EventRingBuffer_Enqueue(uint32_t serial, uint32_t eventType, uintptr_t param); // 0x004D8DD6
int TimeManager_DispatchEvents(void); // 0x004D8DFF
int EventRingBuffer_PurgeScriptEvents(ScriptAttachNode *scriptNode); // 0x004D8EC3
void LightTables_Init(void);
uint32_t GetTickCount_UO(void);

#endif /* TIME_H_ */
