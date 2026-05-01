#ifndef STDDEQUE_H_
#define STDDEQUE_H_

#include <stdint.h>

__extension__ typedef struct CVector CVector;

/*
 * Entry in the timed-event ring (12 bytes, 0x0C). Constructed by
 * TimeEvent_Constructor (0x004D8F60).
 */
__extension__ typedef struct TimeEvent TimeEvent;
struct TimeEvent {
	uint32_t serial;    // +0x00
	uint32_t eventType; // +0x04
	uintptr_t param;    // +0x08 (pointer-as-int payload)
};

/*
 * MSVC std::deque iterator (16 bytes) pointing at a slot inside a
 * deque page.
 */
__extension__ typedef struct StdDequeIter StdDequeIter;
struct StdDequeIter {
	uint8_t *first;    // +0x00
	uint8_t *last;     // +0x04 (first + 0xFFC)
	uint8_t *cur;      // +0x08
	uint8_t **node;    // +0x0C (slot in the page map)
};

/*
 * MSVC std::deque<TimeEvent> at g_eventRingBuffer (0x006F0518, 48 bytes).
 * Pages hold 341 TimeEvents (4092 bytes) each.
 */
__extension__ typedef struct StdDeque_TimeEvent StdDeque_TimeEvent;
struct StdDeque_TimeEvent {
	uint8_t allocByte;     // +0x00
	uint8_t _pad[3];       // +0x01
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4];     // 64-bit alignment pad
#endif
	StdDequeIter front;    // +0x04 (_First)
	StdDequeIter back;     // +0x14 (_Last)
	uint8_t **map;         // +0x24
	uint32_t mapSize;      // +0x28
	uint32_t size;         // +0x2C
};

// 0x004D8CF7 - CTimeManager::FormatTimestamp
struct CString;

extern StdDeque_TimeEvent g_eventRingBuffer; // 0x006F0518

void *ArenaAllocator_Alloc(uint32_t size, void **outPtr); // 0x0046C796
void *StdDeque_AllocPageSI(CVector *self, int count, int hint); // 0x004D96B0
void StdDeque_DeallocSI(CVector *self, void *ptr, int count); // 0x0046C9E0
TimeEvent *TimeEvent_Constructor(TimeEvent *self, uint32_t serial, uint32_t eventType, uintptr_t param); // 0x004D8F60
void StdDeque_Constructor(StdDeque_TimeEvent *self, uint8_t *allocSrc); // 0x004D8F90
StdDequeIter *StdDeque_CopyFrontIter(StdDeque_TimeEvent *self, StdDequeIter *dest); // 0x004D9010
StdDequeIter *StdDeque_CopyEndIter(StdDeque_TimeEvent *self, StdDequeIter *dest); // 0x004D9040
TimeEvent *EventRingBuffer_Front(void); // 0x004D9070
void EventRingBuffer_Pop(void); // 0x004D9090
void StdDeque_push_back(StdDeque_TimeEvent *self, TimeEvent *src); // 0x004D9100
StdDequeIter *StdDequeIter_InitZero(StdDequeIter *self); // 0x004D9170
StdDequeIter *StdDequeIter_PostInc(StdDequeIter *self, StdDequeIter *out, int unused); // 0x004D91B0
int StdDequeIter_NotEqual(StdDequeIter *self, StdDequeIter *other); // 0x004D9200
int EventRingBuffer_IsEmpty(void); // 0x004D9230
void *StdDeque_AllocNodeSI(void *dest, void *src); // 0x004D9710
TimeEvent *StdDequeIter_Deref(StdDequeIter *self);

#endif /* STDDEQUE_H_ */
