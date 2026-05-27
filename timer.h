#ifndef TIMER_H_
#define TIMER_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
/*
 * Per-entity timer node (20 bytes), allocated 0x400 at a time from the
 * free list at 0x0063F81C. Linked through CItem.timerHead (entity+0x1C);
 * expired nodes dispatch via the 21-entry table at 0x00613C18.
 */
__extension__ typedef struct TimerNode TimerNode;
struct TimerNode {
	int countdown;   // +0x00 (ticks until expiry)
	int timerType;   // +0x04 (callback ID / extra param)
	int eventType;   // +0x08 (dispatch table index)
#if __SIZEOF_POINTER__ == 8
	int _pad64;      // 64-bit alignment pad
#endif
	char *extraData; // +0x0C (heap-allocated text for speech events)
	TimerNode *next; // +0x10
};

/*
 * Entity-tracking node for the timer system (12 bytes), linked into the
 * doubly-linked list at 0x0063F814/0x0063F818 to mark which entities
 * currently own live TimerNodes.
 */
__extension__ typedef struct TimerEntityNode TimerEntityNode;
struct TimerEntityNode {
	uint32_t serial;       // +0x00
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;       // 64-bit alignment pad
#endif
	TimerEntityNode *next; // +0x04
	TimerEntityNode *prev; // +0x08
};

// Event types for ScheduleEvent (0x00436688).
#define TIMER_EVENT_DELETE            1
#define TIMER_EVENT_DECAY_CONTAINER   2
#define TIMER_EVENT_IDLE_DISCONNECT   3
#define TIMER_EVENT_DOOR_CLOSE        4
#define TIMER_EVENT_CALLBACK          5
#define TIMER_EVENT_AI_SCHED_DELETE   6
#define TIMER_EVENT_SPEECH            7
#define TIMER_EVENT_VALUELESS_DECAY   8
#define TIMER_EVENT_ONLINE_CHECK      9
#define TIMER_EVENT_NPC_TICK          10
#define TIMER_EVENT_SET_STRING_PROP   11
#define TIMER_EVENT_WAR_MODE_CLEAR    12
#define TIMER_EVENT_SET_STRING_DETACH 13
#define TIMER_EVENT_UNFREEZE          14
#define TIMER_EVENT_UNSQUELCH         15
#define TIMER_EVENT_CLR_INVULN        16
#define TIMER_EVENT_CRIMINAL          0x11
#define TIMER_EVENT_SET_CHAOS         18
#define TIMER_EVENT_SET_ORDER         19
#define TIMER_EVENT_CTRL_TIMEOUT      20

struct CItem;

char *CEntity_SaveTimers(CItem *entity, char *buf, int bufSize); // 0x0043615B
void CEntity_LoadTimers(CItem *entity, const char *val); // 0x00436261
void CEntity_RemoveTimer(CItem *entity, int eventType, int timerType); // 0x004363E2
int CEntity_HasTimerEx(CItem *entity, int eventType, int callbackId); // 0x00436452
void CEntity_RemoveAllTimers(CItem *entity); // 0x004364AE
void ScheduleEvent(int delay, uint32_t serial, int eventType, int timerType, intptr_t extraData); // 0x00436688
void TimerManager_ProcessTimers(void); // 0x004366CF
void CTimerManager_EnsureTracked(CItem *entity); // 0x0043676C

void Timer_DestroyPools(void); // CUSTOM

#endif /* TIMER_H_ */
