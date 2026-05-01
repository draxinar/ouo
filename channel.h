#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <stdint.h>

#include "socket.h"

__extension__ typedef struct CChannel CChannel;
__extension__ typedef struct CChannelMsg CChannelMsg;

/*
 * CChannel vtable slot indices (extends CSocket_vtable with 2 additional slots).
 * Binary CSocket_vtable has 5 entries (slots 0-4). CChannel adds:
 *   slot 5 (offset 0x14): ProcessMessage - dispatches a completed message
 *   slot 6 (offset 0x18): FilterMessage - IsCChannel-like acceptance check
 */
#define CH_VT_PROCESS_MSG 5  // 0x14: dispatch completed CChannelMsg message
#define CH_VT_FILTER_MSG  6  // 0x18: filter check (returns 1 to accept)

/*
 * One message node queued on a CChannel (0x14 bytes).
 */
struct CChannelMsg {
	int msgId;              // 0x00
	int dataSize;           // 0x04
	char *data;             // 0x08 - allocated at ptr-4 for hidden length prefix
	CChannelMsg *next;      // 0x0C
	CChannelMsg *prev;      // 0x10
};

/*
 * Reliable message channel on top of CSocket. Receives framed messages
 * into buf and queues them via msgHead/msgTail. Linked into a global
 * list via nextChannel/prevChannel.
 *
 * Our CSocket adds two custom fields (comp, sendCipher) that shift
 * all offsets past the base by 8 bytes relative to the binary; named
 * fields keep the layout correct.
 */
// clang-format off
struct CChannel {
	CSocket socket;
	CChannelMsg *msgHead;           // 0x28
	CChannelMsg *msgTail;           // 0x2C
	CChannelMsg *currentMsg;        // 0x30
	int curr;                       // 0x34
	union {
		uint32_t recvOffset;    // 0x38 - buf[0..3] as uint32 recv counter
		char buf[0x8004];
	};
	CChannel *nextChannel;          // 0x803C
	CChannel *prevChannel;          // 0x8040
	int totalQueuedBytes;           // 0x8044
};
// clang-format on

void CChannelMsg_Destructor(CChannelMsg *this); // 0x004374B5
int CChannel_Recv(CChannel *this); // 0x0043767F
void CChannel_ProcessMessages(CChannel *this); // 0x004378F7
void CChannel_FlushSendBuffer(CChannel *this); // 0x0043792E
void CChannel_FlushMessages(CChannel *this); // 0x00437A47
int CChannel_IsCChannel(CChannel *this, int arg); // 0x00437AD1
void CChannel_Flush(CChannel *this); // 0x00437AE3
CChannel *CChannel_Destructor(CChannel *this, char freeFlag); // 0x00437C30
void CChannelMsg_Init(CChannelMsg *this, int id, int size, char *data); // 0x00437C60
void CChannel_AppendMessage(CChannel *this, CChannelMsg *msg); // 0x00437CB0

#endif /* CHANNEL_H_ */
