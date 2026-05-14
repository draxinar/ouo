#ifndef HELP_QUEUE_H_
#define HELP_QUEUE_H_

#include <stdint.h>

#include "cstring.h"

/*
 * Counselor/GM help-request queue at g_HelpQueue (0x006982D8). In the
 * binary this wraps std::list<CHelpRequestEntry>; we replace the list
 * with a singly-linked CHelpRequestNode chain.
 */

__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CString CString;
__extension__ typedef struct StdPtrList StdPtrList;
__extension__ typedef struct StdPtrNode StdPtrNode;
struct CPlayer;

/*
 * One pending help request (0x28 bytes in the binary). level is
 * 'n'=new, 'h'=being handled, 'd'=done.
 */
__extension__ typedef struct CHelpRequestNode CHelpRequestNode;
struct CHelpRequestNode {
	uint32_t serial;        // 0x00
	char level;             // 0x04
	char origLevel;         // 0x05
	CString callerName;     // 0x08
	CString message;        // 0x18
	CHelpRequestNode *next; // C singly-linked replacement for std::list
};

/*
 * Queue head plus counselor accounting (0x10 bytes). _pad04 occupies
 * the slot the binary used for the std::list allocator.
 */
__extension__ typedef struct CHelpQueue CHelpQueue;
struct CHelpQueue {
	CHelpRequestNode *head; // 0x00
	uint32_t _pad04;        // 0x04
	int count;              // 0x08
	int counselorCount;     // 0x0C
};

extern CHelpQueue g_HelpQueue; // 0x006982D8

CHelpRequestNode *CHelpQueue_FindBySerial(CHelpQueue *q, uint32_t serial); // 0x0044E1FE
int CHelpQueue_Add(CHelpQueue *q, uint32_t serial, const char *name, uint8_t level, const char *message); // 0x0044E272
int CHelpQueue_AddEntry(CHelpQueue *q, uint32_t serial, char level, char origLevel, const char *name, const char *message); // 0x0044E2D1
void CHelpQueue_AddWithLevel(CHelpQueue *q, uint32_t serial, const char *name, uint8_t level, const char *message); // 0x0044E37C
int CHelpQueue_UpdateLevel(CHelpQueue *q, uint32_t serial, char level); // 0x0044E389
int CHelpQueue_SetLevel(CHelpQueue *q, uint32_t serial, char level); // 0x0044E3D9
int CHelpQueue_GotoEntity(CHelpQueue *q, uint32_t gmSerial, uint32_t victimSerial); // 0x0044E448
CHelpRequestNode *CHelpQueue_FindNextPending(CHelpQueue *q); // 0x0044E4A2
void GmCommandDispatch(CHelpQueue *q, CPlayer *player, const char *text); // 0x0044E518
void CHelpQueue_OnLogout(CHelpQueue *q, CPlayer *player); // 0x0044E9A0
int CHelpQueue_ShowQueue(CHelpQueue *q, CPlayer *player, int maxEntries); // 0x0044E9B9
int CHelpQueue_GotoCur(CHelpQueue *q, CPlayer *player); // 0x0044EB44
int CHelpQueue_Next(CHelpQueue *q, CPlayer *player); // 0x0044EBFA
int CHelpQueue_TransferEntry(CHelpQueue *q, CPlayer *player, CString *gmName, uint32_t victimSerial); // 0x0044ECF8
int CHelpQueue_GmTransfer(CHelpQueue *q, CPlayer *player, const char *gmName); // 0x0044EE33
int CHelpQueue_GotoBySerial(CHelpQueue *q, CPlayer *player, int queueIndex); // 0x0044EEEF
int CHelpQueue_GotoByName(CHelpQueue *q, CPlayer *player, const char *locName); // 0x0044EFF1
int CHelpQueue_Relinquish(CHelpQueue *q, CPlayer *player); // 0x0044F17C
int CHelpQueue_Clear(CHelpQueue *q, CPlayer *player); // 0x0044F1E3
int CHelpQueue_Who(CHelpQueue *q, CPlayer *player); // 0x0044F24A
void CHelpQueue_NotifyLogin(CHelpQueue *this, CPlayer *player); // 0x0044F47B
void CHelpQueue_DecrCounselors(CHelpQueue *this, CPlayer *player); // 0x0044F497
void StdHelpList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos); // 0x0044F530
void StdHelpList_EraseRange(StdPtrList *list, StdPtrNode **result, StdPtrNode *first, StdPtrNode *last); // 0x004696D0

void TC_CommandDispatch(CPlayer *player, const char *text); // CUSTOM (-test)

#endif /* HELP_QUEUE_H_ */
