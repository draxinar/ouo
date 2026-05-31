/*
 * Low-level socket I/O for game and login connections.
 *
 * CSocket wraps a non-blocking TCP socket with send / receive buffers,
 * drives the select (with epoll fallback) loop that polls every
 * connected client, and handles teardown on disconnect or error.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#ifdef __linux__
#include <sys/epoll.h>
#endif
#include <arpa/inet.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include "dat.h"
#include "huffman.h"
#include "listensocket.h"
#include "packet_handler.h"
#include "stl.h"
#include "time.h"
#include "twofish.h"
#include "usersock.h"
#include "vg_pool.h"

static void Noop_47E018(void); // 0x0047E018
static CSocket *Socket_SelectWithTimeout_Linux(struct timeval *timeout, int receive);
static CSocket *Socket_SelectWithTimeout_Poll(struct timeval *timeout, int receive);

CSocket_vtable VTABLE_CSocket = {
	&CSocket_Destructor,
	NULL,
	&CSocket_Send,
	&CSocket_IsBuffer,
	&CSocket_Close,
};

CSocket *GLOBAL_CSocket_first;
CSocket *GLOBAL_CSocket_last;
CSocketBuffer *GLOBAL_CSocketBuffer;

/*
 * 0x0047DF20 - Noop_47DF20
 *
 * Empty stub called from server init and CBlockManager_LoadAll.
 */
void
Noop_47DF20(const char *msg)
{
	USED(msg);
}

/*
 * 0x0047E018 - (unknown, 5 bytes, no-op)
 *
 * Empty stub called from Server_Select.
 */
static void
Noop_47E018(void)
{
}

/*
 * 0x0047E01D - Socket_Init
 *
 * Constructs the central-monitor CUserSock to 127.0.0.1:0xBEEF. Called by
 * Server_Loop during phase-A setup. The new CUserSock attaches itself to
 * the global socket list from its constructor.
 */
void
Socket_Init(void)
{
	static int g_socketCount; /* 0x00699B58 */
	CUserSock *usock;

	Noop_47FA00();
	g_socketCount = 0;
	USED(g_socketCount);

	void *p = malloc(sizeof(CUserSock));
	usock = (CUserSock *)p;
	if (usock != NULL)
		usock = CUserSock_Constructor(usock, 0xBEEF, 0x7F000001);
	else
		usock = NULL;
	USED(usock);
}

/*
 * 0x0047E09D - Socket_Shutdown
 *
 * Server-exit wrapper around Socket_DestructAll. Called by Server_Loop
 * after the main tick loop exits.
 */
void
Socket_Shutdown(void)
{
	Socket_DestructAll();
}

/*
 * Custom - Socket_DestroyPools
 *
 * Server-shutdown cleanup. Walks the CSocketBuffer freelist marking
 * each node defined so valgrind can read its next pointer, then
 * ends pool tracking. The pool is lazily created on first use, so
 * a NULL freelist head means the pool was never created and the
 * destroy is skipped. The VG_* macros are no-ops when VALGRIND is
 * not defined.
 */
void
Socket_DestroyPools(void)
{
	CSocketBuffer *cur, *next;

	if (GLOBAL_CSocketBuffer == NULL)
		return;
	for (cur = GLOBAL_CSocketBuffer; cur != NULL; cur = next) {
		VG_MAKE_DEFINED(cur, sizeof(*cur));
		next = cur->next;
	}
	VG_DESTROY_POOL(&GLOBAL_CSocketBuffer);
}

/*
 * 0x0047E0A7 - Server_Select
 *
 * Drives the main select() loop with the given timeout in microseconds.
 */
CSocket *
Server_Select(int32_t usec)
{
	struct timeval timeout;

	Noop_47E018();
	timeout.tv_sec = 0;
	timeout.tv_usec = usec;
	return Socket_SelectWithTimeout(&timeout, 1);
}

/*
 * 0x0047E42D - Socket_Copy_To_CSocketBuffer
 *
 * Outer wrapper that resolves packet size when -1, and
 * hands off to Copy_To_CSocketBuffer.
 */
CSocket *
Socket_Copy_To_CSocketBuffer(CSocket *this, uint8_t *buf, size_t size)
{
	if ((int)size == -1)
		size = GetPacketOffset(buf);
	return Copy_To_CSocketBuffer(this, buf, size);
}

/*
 * 0x0047E42D - Copy_To_CSocketBuffer2
 *
 * Uncompressed variant: copies src into a fresh heap buffer, endian-
 * swaps the dynamic-size field, and queues it.
 */
CSocket *
Copy_To_CSocketBuffer2(CSocket *this, uint8_t *src, size_t size)
{
	uint8_t *dst;

	dst = malloc(size);
	memcpy(dst, src, size);
	PacketGetDynamicSize(dst);
	return Append_To_CSocketBuffer(this, dst, size);
}

/*
 * 0x0047E42D - Copy_To_CSocketBuffer
 *
 * Huffman-compresses (when enabled) and optionally Twofish-encrypts
 * src, then queues the resulting bytes for send.
 */
CSocket *
Copy_To_CSocketBuffer(CSocket *this, uint8_t *src, size_t size)
{
	uint8_t *dst;
	uint8_t s1, s2;

	// Save bytes 1-2 before PacketGetDynamicSize byte-swaps them
	// in place. Without this, BroadcastToNearby reuses the same buffer
	// for multiple players, and the second send reads the already-swapped
	// size (e.g. 55 -> 14080).
	s1 = src[1];
	s2 = src[2];
	dst = malloc(size * 2); // give enough size for compression output
	PacketGetDynamicSize(src);
	if (this->comp == 1)
		size = HuffmanCompress(dst, src, size);
	else
		memcpy(dst, src, size);
	src[1] = s1;
	src[2] = s2;
	/*
	 * Server→client XOR encrypt (2.0.4+): applied after Huffman compression.
	 * Client recv path (0x0042D8E0): XOR decrypt → Huffman decompress.
	 * So server must: Huffman compress → XOR encrypt → send. */
	if (this->sendCipher)
		TwofishSendCipher_Encrypt(this->sendCipher, dst, size);
	return Append_To_CSocketBuffer(this, dst, size);
}

/*
 * 0x0047FA00 - Noop_47FA00
 *
 * Empty stub called from socket init.
 */
void
Noop_47FA00(void)
{
}

/*
 * 0x0047FA05 - Socket_DestructAll
 *
 * Destroys every CSocket still on the global list at shutdown.
 */
void
Socket_DestructAll()
{
	while (GLOBAL_CSocket_first) {
		if (GLOBAL_CSocket_first)
			GLOBAL_CSocket_first->vtable->Destructor(GLOBAL_CSocket_first, 1);
	}
}

/*
 * 0x0047FA46 - Socket_SelectWithTimeout
 *
 * Replaced select() with platform I/O multiplexing to avoid FD_SETSIZE
 * overflow. The binary uses Windows Winsock fd_set (count + array, capped
 * at 64) which has no fd value limit. Linux select() uses a bitfield
 * indexed by fd value and overflows at fd >= FD_SETSIZE (1024).
 *
 * Uses epoll on Linux, poll elsewhere. Both have no fd value limit,
 * matching the Windows behavior.
 */
CSocket *
Socket_SelectWithTimeout(struct timeval *timeout, int receive)
{
#ifdef __linux__
	return Socket_SelectWithTimeout_Linux(timeout, receive);
#else
	return Socket_SelectWithTimeout_Poll(timeout, receive);
#endif
}

/*
 * 0x0047FD63 - Socket_SendAll
 *
 * Drains queued send buffers on every CSocket, looping up to 10 ms or
 * until no socket still has pending output.
 */
uint32_t
Socket_SendAll()
{
	uint32_t result;
	uint32_t startTick;
	signed int end;
	CSocket *i;

	result = GetTickCount_UO();
	startTick = result;
	end = 0;
	do {
		if (end)
			break;
		end = 1;
		for (i = GLOBAL_CSocket_first; i; i = i->next) {
			if (i->s != -1) {
				if (i->firstSocketBuffer) {
					end = 0;
					i->vtable->Send(i);
				}
			}
		}
		result = GetTickCount_UO();
	} while (result >= startTick + 10);
	return result;
}

/*
 * 0x0047FDDD - CSocketBuffer::Delete
 *
 * Unlinks the socket-buffer node from its owning socket, frees its
 * payload, and returns the node to the free pool.
 */
CSocketBuffer *
CSocketBuffer_Delete(CSocketBuffer *this)
{
	if (this->next)
		this->next->prev = this->prev;
	else
		this->socket->lastSocketBuffer = this->prev;
	if (this->prev)
		this->prev->next = this->next;
	else
		this->socket->firstSocketBuffer = this->next;
	free(this->buf);
	this->socket->totalSize -= this->size;
	this->next = GLOBAL_CSocketBuffer;
	GLOBAL_CSocketBuffer = this;
	VG_POOL_FREE(&GLOBAL_CSocketBuffer, this);
	return this;
}

/*
 * 0x0047FE78 - Append_To_CSocketBuffer
 *
 * Queues buf onto the socket's send list, grabbing a CSocketBuffer
 * from the free pool (lazily creating it on first use).
 */
CSocket *
Append_To_CSocketBuffer(CSocket *socket, uint8_t *buf, size_t size)
{
	CSocketBuffer *socketBuffer;
	CSocket *result;
	signed int i;
	CSocketBuffer *pool;

	if (GLOBAL_CSocketBuffer) {
		socketBuffer = GLOBAL_CSocketBuffer;
		VG_POOL_ALLOC(&GLOBAL_CSocketBuffer, socketBuffer, sizeof(CSocketBuffer));
		VG_MAKE_DEFINED(&socketBuffer->next, sizeof(socketBuffer->next));
		GLOBAL_CSocketBuffer = socketBuffer->next;
		result = CSocketBuffer_Add(socketBuffer, socket, buf, size);
	} else {
		static int vgPoolCreated;
		if (!vgPoolCreated) {
			VG_CREATE_POOL(&GLOBAL_CSocketBuffer);
			vgPoolCreated = 1;
		}
		pool = malloc(256 * sizeof(CSocketBuffer));
		for (i = 255; i >= 1; --i) {
			pool[i].next = GLOBAL_CSocketBuffer;
			GLOBAL_CSocketBuffer = &pool[i];
		}
		VG_POOL_ALLOC(&GLOBAL_CSocketBuffer, pool, sizeof(CSocketBuffer));
		result = CSocketBuffer_Add(pool, socket, buf, size);
	}
	return result;
}

/*
 * 0x0047FF00 - CSocketBuffer::Send
 *
 * Sends as many remaining bytes as the kernel will accept; closes the
 * socket on fatal errors. Returns 1 on progress, 0 otherwise.
 */
signed int
CSocketBuffer_Send(CSocketBuffer *this)
{
	int result;
	int n;

	n = send(this->socket->s, &this->buf[this->socket->totalSent], this->size - this->socket->totalSent, MSG_NOSIGNAL);
	if (n <= 0) {
		if (n == -1 && errno != EWOULDBLOCK)
			this->socket->vtable->Close(this->socket);
		result = 0;
	} else {
		this->socket->totalSent += n;
		result = 1;
	}
	return result;
}

/*
 * 0x0047FFCE - CSocket::CSocket
 *
 * Initializes a CSocket and links it at the head of the global list.
 */
CSocket *
CSocket_Constructor(CSocket *this)
{
	this->vtable = &VTABLE_CSocket;
	this->next = GLOBAL_CSocket_first;
	if (this->next)
		this->next->prev = this;
	this->prev = 0;
	GLOBAL_CSocket_first = this;
	this->s = -1;
	this->status = 0;
	this->lastSocketBuffer = 0;
	this->firstSocketBuffer = 0;
	this->totalSize = 0;
	this->totalSent = 0;
	this->comp = 0; /* Huffman compression (added field, not in binary) */
	this->sendCipher = NULL;
	this->name = "CSocket";
	return this;
}

/*
 * 0x0048005F - CSocket::Delete
 *
 * Unlinks this CSocket from the global list, closes the fd, and
 * drains any pending send buffers.
 */
CSocket *
CSocket_Delete(CSocket *this)
{
	CSocket *result;

	this->vtable = &VTABLE_CSocket;
	if (GLOBAL_CSocket_last == this)
		GLOBAL_CSocket_last = this->next;
	if (this->next)
		this->next->prev = this->prev;
	if (this->prev) {
		result = this;
		this->prev->next = this->next;
	} else {
		result = this->next;
		GLOBAL_CSocket_first = this->next;
	}
	if (this->s != -1) {
		result = (CSocket *)(intptr_t)close(this->s);
		this->s = -1;
	}
	while (this->firstSocketBuffer)
		result = (CSocket *)CSocketBuffer_Delete(this->firstSocketBuffer);
	return result;
}

/*
 * 0x004800FB - CSocket::SetSocket
 *
 * Stores fd as the socket's underlying file descriptor.
 */
CSocket *
CSocket_SetSocket(CSocket *this, int fd)
{
	this->s = fd;
	return this;
}

/*
 * 0x00480111 - CSocket::SetBlock
 *
 * Toggles blocking mode on the underlying fd.
 */
int
CSocket_SetBlock(CSocket *this, int blocking)
{
	return SetSocketBlocking(this->s, blocking);
}

/*
 * 0x00480131 - CSocket::IsBuffer
 *
 * Returns 1 when the socket has a pending send buffer.
 */
int
CSocket_IsBuffer(CSocket *this)
{
	return this->firstSocketBuffer != 0;
}

/*
 * 0x0048014A - CSocket::Close
 *
 * Marks the socket as closing and closes the underlying fd.
 */
CSocket *
CSocket_Close(CSocket *this)
{
	this->status |= SocketClosing;
	if (this->s != -1) {
		close(this->s);
		this->s = -1;
	}
	return this;
}

/*
 * 0x00480183 - CSocket::Send
 *
 * Flushes the head send-buffer, advancing totalSent and popping the
 * buffer when fully sent; continues until blocked or empty.
 */
CSocket *
CSocket_Send(CSocket *this)
{
	int n;

	do {
		n = CSocketBuffer_Send(this->firstSocketBuffer);
		if (this->totalSent == this->firstSocketBuffer->size) {
			CSocketBuffer_Delete(this->firstSocketBuffer);
			this->totalSent = 0;
		}
	} while (n && this->firstSocketBuffer && this->s != -1);
	return this;
}

/*
 * 0x004802E3 - CListenSocket::CListenSocket
 *
 * Constructs a CListenSocket, installing the CListenSocket vtable on
 * top of the base CSocket.
 */
CSocket *
CListenSocket_Constructor(CSocket *this)
{
	CSocket_Constructor(this);
	this->vtable = &VTABLE_CListenSocket;
	this->name = "CListenSocket";
	return this;
}

/*
 * 0x0048030C - CListenSocket::CListenSocket (listen variant)
 *
 * Constructs and immediately binds a listen socket on the given port.
 */
CListenSocket *
CListenSocket_Constructor_Listen(CListenSocket *this, uint16_t port)
{
	CSocket_Constructor(&this->socket);
	this->socket.vtable = (CSocket_vtable *)&VTABLE_CListenSocket;
	this->port = 0;
	this->socket.name = "CListenSocket";
	CListenSocket_Listen(this, port, 8);
	return this;
}

/*
 * 0x0048037B - CListenSocket::Delete
 *
 * Restores the CListenSocket vtable and chains to CSocket_Delete.
 */
CSocket *
CListenSocket_Delete(CSocket *this)
{
	this->vtable = (CSocket_vtable *)&VTABLE_CListenSocket;
	return CSocket_Delete(this);
}

/*
 * 0x00480397 - CListenSocket::Listen
 *
 * Binds the listen socket to port and starts listening with the given
 * backlog.
 */
uint16_t
CListenSocket_Listen(CListenSocket *this, uint16_t port, int backlog)
{
	uint32_t optval;
	struct sockaddr_in sin;

	optval = 1;
	this->socket.s = socket(AF_INET, SOCK_STREAM, 0);
	if (this->socket.s == -1) {
		perror("CListenSocket::Init: socket");
		exit(1);
	}
	if (setsockopt(this->socket.s, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) == -1) {
		perror("CListenSocket::Init: setsockopt");
		close(this->socket.s);
		this->socket.s = -1;
		exit(1);
	}
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	if (bind(this->socket.s, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		perror("CListenSocket::Init: bind");
		close(this->socket.s);
		this->socket.s = -1;
		exit(1);
	}
	CSocket_SetBlock(&this->socket, 0);
	if (listen(this->socket.s, backlog) == -1) {
		perror("CListenSocket::Init: listen");
		close(this->socket.s);
		this->socket.s = -1;
		exit(1);
	}
	this->port = port;
	return port;
}

/*
 * 0x004804E8 - CListenSocket::ListenRange
 *
 * Binds the listen socket to the first free port in [startPort, endPort)
 * and starts listening with the given backlog.
 */
CListenSocket *
CListenSocket_ListenRange(CListenSocket *this, int startPort, int endPort, int backlog)
{
	uint32_t optval;
	struct sockaddr_in sin;
	uint32_t port; // binary: uint16_t ports[2] accessed as *(uint32_t *)

	optval = 1;
	this->socket.s = socket(AF_INET, SOCK_STREAM, 0);
	if (this->socket.s == -1) {
		perror("CListenSocket::Init: socket");
		exit(1);
	}
	if (setsockopt(this->socket.s, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) == -1) {
		perror("CListenSocket::Init: setsockopt");
		close(this->socket.s);
		this->socket.s = -1;
		exit(1);
	}
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	for (port = (uint32_t)startPort; port < (uint32_t)endPort; ++port) {
		sin.sin_port = htons((uint16_t)port);
		if (bind(this->socket.s, (struct sockaddr *)&sin, sizeof(sin)) != -1)
			break;
	}
	if (port == (uint32_t)endPort) {
		perror("CListenSocket::Init: bind");
		close(this->socket.s);
		this->socket.s = -1;
		exit(1);
	}
	CSocket_SetBlock(&this->socket, 0);
	if (listen(this->socket.s, backlog) == -1) {
		perror("CListenSocket::Init: listen");
		close(this->socket.s);
		this->socket.s = -1;
		exit(1);
	}
	this->port = (uint16_t)port;
	return this;
}

/*
 * 0x0048065E - CListenSocket::Accept
 *
 * accept()'s the next pending connection and writes the peer's IPv4
 * address into addr; returns the new fd or -1 on error.
 */
int
CListenSocket_Accept(CListenSocket *this, uint32_t *addr)
{
	int result;
	struct sockaddr sa;
	int s;
	socklen_t addrlen;

	*addr = 0;
	addrlen = sizeof(sa);
	memset(&sa, 0, sizeof(sa));
	s = accept(this->socket.s, &sa, &addrlen);
	if (s == -1) {
		result = -1;
	} else if (getpeername(s, &sa, &addrlen) == -1) {
		close(s);
		result = -1;
	} else {
		*addr = ntohl(*(uint32_t *)&((struct sockaddr_in *)&sa)->sin_addr);
		result = s;
	}
	return result;
}

/*
 * 0x004806E4 - SetSocketBlocking
 *
 * Toggles O_NONBLOCK on fd s based on blocking.
 */
int
SetSocketBlocking(int s, int blocking)
{
	int flags;

	flags = fcntl(s, F_GETFL);
	if (blocking == 0)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;
	return fcntl(s, F_SETFL, flags);
}

/*
 * 0x00480710 - CSocketBuffer::Add
 *
 * Links the buffer node into the socket's send list at the tail and
 * bumps totalSize.
 */
CSocket *
CSocketBuffer_Add(CSocketBuffer *this, CSocket *socket, uint8_t *buf, int size)
{
	this->socket = socket;
	this->next = 0;
	this->prev = this->socket->lastSocketBuffer;
	if (this->prev)
		this->prev->next = this;
	else
		this->socket->firstSocketBuffer = this;
	this->socket->lastSocketBuffer = this;
	this->buf = buf;
	this->size = size;
	this->socket->totalSize += this->size;
	return this->socket;
}

/*
 * 0x004807A0 - CSocket::~CSocket
 *
 * Scalar deleting destructor: runs CSocket_Delete and frees the object
 * when v & 1.
 */
CSocket *
CSocket_Destructor(CSocket *this, uint8_t v)
{
	CSocket_Delete(this);
	if (v & 1)
		free(this);
	return NULL;
}

/*
 * 0x004807D0 - CListenSocket::~CListenSocket
 *
 * Scalar deleting destructor: runs CListenSocket_Delete and frees the
 * object when v & 1.
 */
CSocket *
CListenSocket_Destructor(CSocket *this, uint8_t v)
{
	CListenSocket_Delete(this);
	if (v & 1)
		free(this);
	return NULL;
}

/*
 * Custom - Socket_SelectWithTimeout_Linux
 *
 * Linux epoll implementation of Socket_SelectWithTimeout.
 * Uses a persistent epoll fd with EPOLL_CTL_MOD/ADD to track sockets.
 * Closed fds are auto-removed from epoll by the kernel.
 *
 * Entries are keyed by fd, not by raw CSocket pointer. The ready set from
 * epoll_wait is a snapshot; resolving each ready fd back to a live socket by
 * walking GLOBAL_CSocket_first mirrors the binary's post-select FD_ISSET loop
 * (the binary's select returns fd sets, never pointers). This avoids a
 * use-after-free when a socket referenced by a later entry in the same
 * epoll_wait batch is torn down while the batch is still being processed.
 */
static CSocket *
Socket_SelectWithTimeout_Linux(struct timeval *timeout, int receive)
{
#ifdef __linux__
	static int epfd = -1;
	CSocket *result;
	int status;
	CSocket *i;
	struct epoll_event *events;
	struct epoll_event ev;
	int cap;
	int timeoutMs;
	int ret;

	timeoutMs = timeout ? (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;

	if (epfd == -1)
		epfd = epoll_create1(0);

	// Count active sockets.
	cap = 0;
	for (i = GLOBAL_CSocket_first; i; i = i->next) {
		if ((i->status & SocketClosing) || i->s == -1)
			continue;
		cap++;
	}

	if (cap == 0) {
		epoll_wait(epfd, &ev, 1, timeoutMs);
		goto cleanup_phase;
	}

	// Register/update all active sockets in epoll.
	for (i = GLOBAL_CSocket_first; i; i = i->next) {
		if ((i->status & SocketClosing) || i->s == -1)
			continue;
		ev.data.fd = i->s;
		ev.events = EPOLLPRI;
		if (receive)
			ev.events |= EPOLLIN;
		if (i->vtable->IsBuffer(i))
			ev.events |= EPOLLOUT;
		if (epoll_ctl(epfd, EPOLL_CTL_MOD, i->s, &ev) == -1 && errno == ENOENT)
			epoll_ctl(epfd, EPOLL_CTL_ADD, i->s, &ev);
	}

	events = malloc(cap * sizeof(struct epoll_event));
	ret = epoll_wait(epfd, events, cap, timeoutMs);

	if (ret == -1) {
		result = (CSocket *)(intptr_t)errno;
		free(events);
		return result;
	}

	{
		int j;
		for (j = 0; j < ret; j++) {
			int rfd = events[j].data.fd;
			uint32_t rmask = events[j].events;
			for (i = GLOBAL_CSocket_first; i; i = i->next)
				if (i->s == rfd)
					break;
			if (i == NULL || (i->status & SocketClosing) || i->s == -1)
				continue;
			if (rmask & (EPOLLERR | EPOLLHUP))
				i->vtable->Close(i);
			if (!(i->status & SocketClosing) && (rmask & EPOLLOUT))
				i->vtable->Send(i);
			if (receive && !(i->status & SocketClosing) && (rmask & EPOLLIN))
				i->vtable->Recv(i);
		}
	}

	free(events);

cleanup_phase:
	for (i = GLOBAL_CSocket_first; i; i = GLOBAL_CSocket_last) {
		GLOBAL_CSocket_last = i->next;
		result = (CSocket *)(intptr_t)(i->status & SocketClosing);
		if (result) {
			status = i->status;
			if (status == SocketClosing) {
				result = i;
				i->status = SocketDestroyed;
				if (i)
					result = i->vtable->Destructor(i, 1);
			} else if (status == SocketThree) {
				result = i;
				i->status = SocketClosed;
				if (i->s != -1) {
					result = (CSocket *)(intptr_t)close(i->s);
					i->s = -1;
				}
			}
		}
	}
	return NULL;
#else
	USED(timeout);
	USED(receive);
	return NULL;
#endif
}

/*
 * Custom - Socket_SelectWithTimeout_Poll
 *
 * Portable poll() implementation of Socket_SelectWithTimeout.
 * Fallback for non-Linux platforms (BSDs, macOS).
 */
static __attribute__((unused)) CSocket *
Socket_SelectWithTimeout_Poll(struct timeval *timeout, int receive)
{
#ifndef __linux__
	CSocket *result;
	int status;
	CSocket *i;
	struct pollfd *pfds;
	int nfds, cap;
	int timeoutMs;
	int ret;

	timeoutMs = timeout ? (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;

	// Count active sockets.
	cap = 0;
	for (i = GLOBAL_CSocket_first; i; i = i->next) {
		if ((i->status & SocketClosing) || i->s == -1)
			continue;
		cap++;
	}

	if (cap == 0) {
		poll(NULL, 0, timeoutMs);
		goto cleanup_phase;
	}

	pfds = malloc(cap * sizeof(struct pollfd));

	// Build pollfd array.
	nfds = 0;
	for (i = GLOBAL_CSocket_first; i; i = i->next) {
		if ((i->status & SocketClosing) || i->s == -1)
			continue;
		pfds[nfds].fd = i->s;
		pfds[nfds].events = POLLPRI;
		if (receive)
			pfds[nfds].events |= POLLIN;
		if (i->vtable->IsBuffer(i))
			pfds[nfds].events |= POLLOUT;
		pfds[nfds].revents = 0;
		nfds++;
	}

	ret = poll(pfds, nfds, timeoutMs);

	if (ret == -1) {
		result = (CSocket *)(intptr_t)errno;
		free(pfds);
		return result;
	}

	// Resolve each ready fd against the live socket list (see the epoll path):
	// a socket torn down by a handler earlier in this batch is skipped instead
	// of dereferenced through a stale pointer.
	if (ret > 0) {
		int j;
		for (j = 0; j < nfds; j++) {
			int rfd = pfds[j].fd;
			short rmask = pfds[j].revents;
			for (i = GLOBAL_CSocket_first; i; i = i->next)
				if (i->s == rfd)
					break;
			if (i == NULL || (i->status & SocketClosing) || i->s == -1)
				continue;
			if (rmask & (POLLERR | POLLHUP | POLLNVAL))
				i->vtable->Close(i);
			if (!(i->status & SocketClosing) && (rmask & POLLOUT))
				i->vtable->Send(i);
			if (receive && !(i->status & SocketClosing) && (rmask & POLLIN))
				i->vtable->Recv(i);
		}
	}

	free(pfds);

cleanup_phase:
	for (i = GLOBAL_CSocket_first; i; i = GLOBAL_CSocket_last) {
		GLOBAL_CSocket_last = i->next;
		result = (CSocket *)(intptr_t)(i->status & SocketClosing);
		if (result) {
			status = i->status;
			if (status == SocketClosing) {
				result = i;
				i->status = SocketDestroyed;
				if (i)
					result = i->vtable->Destructor(i, 1);
			} else if (status == SocketThree) {
				result = i;
				i->status = SocketClosed;
				if (i->s != -1) {
					result = (CSocket *)(intptr_t)close(i->s);
					i->s = -1;
				}
			}
		}
	}
	return NULL;
#else
	USED(timeout);
	USED(receive);
	return NULL;
#endif
}
