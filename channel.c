/*
 * CChannel - inter-thread message queue used by the GameCentral monitor.
 *
 * A thread-safe pair of mailboxes with condition-variable signalling,
 * used to hand requests between the accept/listener thread and the
 * main game loop.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "channel.h"
#include "dat.h"
#include "main.h"

static void CChannel_Delete(CChannel *this); // 0x0043759F
static CChannelMsg *CChannel_WaitForMessage(CChannel *this, int msgId, int arg2); // 0x0043799B
static void *OperatorNew_20(void); // 0x00437CA0
static int SpawnClientThread(CChannel *this); // 0x00438218
static char *ReadTokenFromStream(CChannel *this, FILE *f); // 0x0043828F

// 0x0063F848 - head of registered CChannel list
static CChannel *g_channelHead;
// 0x0063F840 - aggregate byte counter across channels
static int g_channelTotalBytes;

/*
 * 0x004374B5 - CChannelMsg::~CChannelMsg
 *
 * Frees the message's payload buffer (data - 4 accounts for the 4-byte
 * size prefix) and the node itself.
 */
void
CChannelMsg_Destructor(CChannelMsg *this)
{
	CChannelMsg *v1;

	v1 = this;
	if (this->data)
		free(this->data - 4);
	free(v1);
}

/*
 * 0x004374F5 - CChannel::CChannel
 *
 * Runs the CSocket constructor, clears the message queue and byte
 * counter, then links the channel at the head of the global list and
 * sets status 2.
 *
 * The binary stamps the CChannel vtable at 0x005EE848 over the CSocket
 * one the base constructor installed. Our CChannel dispatches through
 * direct calls rather than a second vtable object, so the base pointer
 * from CSocket_Constructor is left in place.
 */
static __attribute__((unused)) CChannel *
CChannel_Constructor(CChannel *this)
{
	CSocket_Constructor((CSocket *)this);
	this->msgHead = NULL;
	this->msgTail = NULL;
	this->currentMsg = NULL;
	this->curr = 0;
	this->prevChannel = NULL;
	this->totalQueuedBytes = 0;
	this->socket.name = "CChannel";
	this->nextChannel = g_channelHead;
	if (this->nextChannel != NULL)
		this->nextChannel->prevChannel = this;
	g_channelHead = this;
	this->socket.status = 2;
	return this;
}

/*
 * 0x0043759F - CChannel::Delete
 *
 * Destroys pending messages, unlinks from the global channel list, and
 * delegates socket teardown to CSocket::Delete.
 */
static void
CChannel_Delete(CChannel *this)
{
	CChannelMsg *node;

	while (this->msgHead != NULL) {
		node = this->msgHead;
		this->msgHead = node->next;
		CChannelMsg_Destructor(node);
	}
	if (this->currentMsg != NULL)
		CChannelMsg_Destructor(this->currentMsg);
	if (this->nextChannel != NULL)
		this->nextChannel->prevChannel = this->prevChannel;
	if (this->prevChannel != NULL)
		this->prevChannel->nextChannel = this->nextChannel;
	else
		g_channelHead = this->nextChannel;
	CSocket_Delete((CSocket *)this);
}

/*
 * 0x0043767F - CChannel::Recv
 *
 * Reads bytes from the channel socket, parses them into CChannelMsg nodes
 * (8-byte header + payload) and queues completed messages.
 */
int
CChannel_Recv(CChannel *this)
{
	int result;
	int i;
	int parseOffset;
	int msgId;
	char *dataBuffer;
	int recvBytes;
	int dataSize;

	result = recv(this->socket.s, &this->buf[this->curr + 4], 0x8000 - this->curr, 0);
	recvBytes = result;
	if (result > 0) {
		this->curr += recvBytes;
		parseOffset = 0;
		while (1) {
			while (!this->currentMsg) {
				if (this->curr - parseOffset < 8)
					goto LABEL_18;
				memcpy(&msgId, &this->buf[parseOffset + 4], 4);
				memcpy(&dataSize, &this->buf[parseOffset + 8], 4);
				if (dataSize <= 0)
					dataBuffer = NULL;
				else
					dataBuffer = (char *)malloc(dataSize + 4) + 4;
				this->currentMsg = (CChannelMsg *)malloc(sizeof(CChannelMsg));
				CChannelMsg_Init(this->currentMsg, msgId, dataSize, dataBuffer);
				this->recvOffset = 0;
				if (!dataSize) {
					CChannel_AppendMessage(this, this->currentMsg);
					this->currentMsg = 0;
				}
				parseOffset += 8;
			}
			if ((int)(this->recvOffset + this->curr - parseOffset) < this->currentMsg->dataSize)
				break;
			memcpy(this->currentMsg->data + this->recvOffset, &this->buf[parseOffset + 4], this->currentMsg->dataSize - this->recvOffset);
			parseOffset += this->currentMsg->dataSize - this->recvOffset;
			CChannel_AppendMessage(this, this->currentMsg);
			this->currentMsg = 0;
		}
		memcpy(this->currentMsg->data + this->recvOffset, &this->buf[parseOffset + 4], this->curr - parseOffset);
		this->recvOffset += this->curr - parseOffset;
		parseOffset = this->curr;
LABEL_18:
		result = parseOffset;
		if (parseOffset == this->curr) {
			this->curr = 0;
		} else {
			for (i = parseOffset; i < this->curr; ++i)
				this->buf[i - parseOffset + 4] = this->buf[i + 4];
			result = this->curr - parseOffset;
			this->curr = result;
		}
	} else if (recvBytes == -1) {
		result = errno;
		if (result != EWOULDBLOCK)
			result = (int)(intptr_t)this->socket.vtable->Close((CSocket *)this);
	}
	return result;
}

/*
 * 0x004378F7 - CChannel::ProcessMessages
 *
 * Calls Recv until a new message is queued or the server is shutting down.
 */
void
CChannel_ProcessMessages(CChannel *this)
{
	CChannelMsg *savedTail;

	savedTail = this->msgTail;
	while (!g_shutdown) {
		if (savedTail != this->msgTail)
			break;
		this->socket.vtable->Recv((CSocket *)this);
	}
}

/*
 * 0x0043792E - CChannel::FlushSendBuffer
 *
 * Drains queued socket buffers synchronously.
 */
void
CChannel_FlushSendBuffer(CChannel *this)
{
	CSocket_SetBlock((CSocket *)this, 1);
	while (!g_shutdown) {
		if (this->socket.firstSocketBuffer == NULL)
			break;
		if (this->socket.s == -1)
			break;
		CSocketBuffer_Send(this->socket.firstSocketBuffer);
		if (this->socket.totalSent == this->socket.firstSocketBuffer->size) {
			CSocketBuffer_Delete(this->socket.firstSocketBuffer);
			this->socket.totalSent = 0;
		}
	}
	CSocket_SetBlock((CSocket *)this, 0);
}

/*
 * 0x0043799B - CChannel::WaitForMessage
 *
 * Waits for a message with the given id, blocking until it arrives or the
 * server shuts down. Zero callers in the binary - orphaned code.
 */
static __attribute__((unused)) CChannelMsg *
CChannel_WaitForMessage(CChannel *this, int msgId, int arg2)
{
	CChannelMsg **iter;
	CChannelMsg *node;

	USED(arg2);
	CChannel_FlushSendBuffer(this);
	for (;;) {
		if (g_shutdown)
			return NULL;
		iter = &this->msgHead;
		while (*iter != NULL) {
			node = *iter;
			if (node->msgId == msgId) {
				*iter = node->next;
				if (this->msgTail == node)
					this->msgTail = node->prev;
				this->totalQueuedBytes -= node->dataSize;
				return node;
			}
			iter = &node->next;
		}
		CChannel_ProcessMessages(this);
	}
}

/*
 * 0x00437A47 - CChannel::FlushMessages
 *
 * Dispatches every queued message through the channel's ProcessMessage
 * vtable slot and destroys each node.
 */
void
CChannel_FlushMessages(CChannel *this)
{
	CChannelMsg *node;

	while (this->msgHead != NULL) {
		if (g_shutdown)
			break;
		node = this->msgHead;
		this->msgHead = node->next;
		if (this->msgHead == NULL)
			this->msgTail = NULL;
		this->totalQueuedBytes -= node->dataSize;
		g_channelTotalBytes += node->dataSize;
		((void (*)(CChannel *, CChannelMsg *))((void **)this->socket.vtable)[CH_VT_PROCESS_MSG])(this, node);
		CChannelMsg_Destructor(node);
	}
}

/*
 * 0x00437AD1 - CChannel::IsCChannel
 *
 * Virtual tag returning 1 for any CChannel.
 */
int
CChannel_IsCChannel(CChannel *this, int arg)
{
	USED(this);
	USED(arg);
	return 1;
}

/*
 * 0x00437AE3 - CChannel::Flush
 *
 * Walks the message list, invoking FilterMessage on each node and, when
 * it accepts, dispatching ProcessMessage and destroying the node.
 */
void
CChannel_Flush(CChannel *this)
{
	CChannelMsg *prev;
	CChannelMsg *cur;
	CChannelMsg *nextNode;

	prev = NULL;
	cur = this->msgHead;
	while (cur != NULL) {
		if (g_shutdown)
			return;
		nextNode = cur->next;
		if (((int (*)(CChannel *, CChannelMsg *))((void **)this->socket.vtable)[CH_VT_FILTER_MSG])(this, cur)) {
			if (prev != NULL)
				prev->next = nextNode;
			else
				this->msgHead = nextNode;
			if (nextNode == NULL)
				this->msgTail = prev;
			this->totalQueuedBytes -= cur->dataSize;
			g_channelTotalBytes += cur->dataSize;
			((void (*)(CChannel *, CChannelMsg *))((void **)this->socket.vtable)[CH_VT_PROCESS_MSG])(this, cur);
			CChannelMsg_Destructor(cur);
		} else {
			prev = cur;
		}
		cur = nextNode;
	}
}

/*
 * 0x00437BB3 - CChannel::FlushAll
 *
 * Walks the global channel list skipping closed sockets (s == -1).
 * When mode is 0 every channel gets FlushMessages; otherwise every
 * channel except skip gets Flush.
 */
static __attribute__((unused)) void
CChannel_FlushAll(int mode, CChannel *skip)
{
	CChannel *ch;

	if (mode == 0) {
		for (ch = g_channelHead; ch != NULL; ch = ch->nextChannel) {
			if (ch->socket.s != -1)
				CChannel_FlushMessages(ch);
		}
	} else {
		for (ch = g_channelHead; ch != NULL; ch = ch->nextChannel) {
			if (ch->socket.s != -1 && ch != skip)
				CChannel_Flush(ch);
		}
	}
}

/*
 * 0x00437C30 - CChannel::~CChannel
 *
 * Scalar deleting destructor: runs CChannel_Delete and frees the object
 * when freeFlag&1 is set.
 */
CChannel *
CChannel_Destructor(CChannel *this, char freeFlag)
{
	CChannel_Delete(this);
	if (freeFlag & 1)
		free(this);
	return NULL;
}

/*
 * 0x00437C60 - CChannelMsg::Init
 *
 * Initializes a channel message node with id, size, and data payload,
 * clearing the linked-list pointers.
 */
void
CChannelMsg_Init(CChannelMsg *this, int id, int size, char *data)
{
	this->msgId = id;
	this->dataSize = size;
	this->data = data;
	this->prev = NULL;
	this->next = NULL;
}

/*
 * 0x00437CA0 - Operator new wrapper
 *
 * Allocates a 20-byte CChannelMsg.
 */
static __attribute__((unused)) void *
OperatorNew_20(void)
{
	return malloc(sizeof(CChannelMsg));
}

/*
 * 0x00437CB0 - CChannel::AppendMessage
 *
 * Appends msg to the tail of the channel's doubly-linked message queue.
 */
void
CChannel_AppendMessage(CChannel *this, CChannelMsg *msg)
{
	if (this->msgTail != NULL) {
		this->msgTail->next = msg;
		msg->prev = this->msgTail;
	} else {
		this->msgHead = msg;
	}
	this->msgTail = msg;
}

/*
 * 0x00438218 - SpawnClientThread
 *
 * Win32 spawned a UO client thread; the Linux server never does.
 */
static __attribute__((unused)) int
SpawnClientThread(CChannel *this)
{
	USED(this);
	return 0;
}

// 0x0063F850 - ReadTokenFromStream scratch buffer
static char g_readTokenBuf[512];

/*
 * 0x0043828F - CChannel::ReadTokenFromStream
 *
 * Reads a line from f into a static buffer, stripping carriage returns
 * and EOF bytes.
 */
static __attribute__((unused)) char *
ReadTokenFromStream(CChannel *this, FILE *f)
{
	char *buf;
	int ch;

	USED(this);
	buf = g_readTokenBuf;
	for (;;) {
		if (feof(f))
			break;
		ch = fgetc(f);
		if (ch == 0x0A)
			break;
		if (ch == 0x0D)
			continue;
		if (ch == -1)
			continue;
		*buf = (char)ch;
		buf = buf + 1;
	}
	*buf = '\0';
	return g_readTokenBuf;
}
