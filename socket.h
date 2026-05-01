#ifndef SOCKET_H_
#define SOCKET_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

__extension__ typedef struct CListenSocket CListenSocket;
__extension__ typedef struct CSocket CSocket;
__extension__ typedef struct CSocketBuffer CSocketBuffer;
__extension__ typedef struct CSocket_vtable CSocket_vtable;
__extension__ typedef struct TwofishSendCipher TwofishSendCipher;

/*
 * Outbound pending-send buffer, linked off CSocket's send queue and the
 * global GLOBAL_CSocketBuffer free chain.
 */
struct CSocketBuffer {
	CSocketBuffer *next;
	CSocketBuffer *prev;
	CSocket *socket;
	uint8_t *buf;
	int size;
};

/*
 * Base non-blocking socket, specialized by CUserSock (game connection)
 * and CListenSocket. The comp flag and sendCipher are CUSTOM fields
 * used for compression selection and the 2.0.4+ Twofish send cipher.
 */
// clang-format off
struct CSocket {
	CSocket_vtable *vtable;
	CSocket *next;
	CSocket *prev;
	int s;
	int status;
	CSocketBuffer *firstSocketBuffer;
	CSocketBuffer *lastSocketBuffer;
	int totalSent;
	int totalSize;
	char *name;
	int comp;                      // CUSTOM: compression selection
#if __SIZEOF_POINTER__ == 8
	int _pad64;                    // 64-bit alignment pad
#endif
	TwofishSendCipher *sendCipher; // CUSTOM: 2.0.4+ server->client XOR
};
// clang-format on

/*
 * Vtable layout for CSocket.
 */
struct CSocket_vtable {
	CSocket *(*Destructor)(CSocket *, uint8_t);
	CSocket *(*Recv)(CSocket *);
	CSocket *(*Send)(CSocket *);
	int (*IsBuffer)(CSocket *);
	CSocket *(*Close)(CSocket *);
};

/*
 * CSocket.status states.
 */
enum {
	SocketClosing = 1,
	SocketClosed = 2,
	SocketThree = 3,
	SocketDestroyed = 5,
};

extern CSocket_vtable VTABLE_CSocket;
extern CSocket *GLOBAL_CSocket_first;
extern CSocket *GLOBAL_CSocket_last;
extern CSocketBuffer *GLOBAL_CSocketBuffer;

void Noop_47DF20(const char *msg); // 0x0047DF20
void Socket_Init(void); // 0x0047E01D
void Socket_Shutdown(void); // 0x0047E09D
CSocket *Server_Select(int32_t usec); // 0x0047E0A7
CSocket *Socket_Copy_To_CSocketBuffer(CSocket *this, uint8_t *buf, size_t size); // 0x0047E42D
CSocket *Copy_To_CSocketBuffer2(CSocket *this, uint8_t *src, size_t size); // 0x0047E42D
CSocket *Copy_To_CSocketBuffer(CSocket *this, uint8_t *src, size_t size); // 0x0047E42D
void Noop_47FA00(void); // 0x0047FA00
void Socket_DestructAll(); // 0x0047FA05
CSocket *Socket_SelectWithTimeout(struct timeval *timeout, int receive); // 0x0047FA46
uint32_t Socket_SendAll(); // 0x0047FD63
CSocketBuffer *CSocketBuffer_Delete(CSocketBuffer *this); // 0x0047FDDD
CSocket *Append_To_CSocketBuffer(CSocket *socket, uint8_t *buf, size_t size); // 0x0047FE78
signed int CSocketBuffer_Send(CSocketBuffer *this); // 0x0047FF00
CSocket *CSocket_Constructor(CSocket *this); // 0x0047FFCE
CSocket *CSocket_Delete(CSocket *this); // 0x0048005F
CSocket *CSocket_SetSocket(CSocket *this, int fd); // 0x004800FB
int CSocket_SetBlock(CSocket *this, int blocking); // 0x00480111
int CSocket_IsBuffer(CSocket *this); // 0x00480131
CSocket *CSocket_Close(CSocket *this); // 0x0048014A
CSocket *CSocket_Send(CSocket *this); // 0x00480183
CSocket *CListenSocket_Constructor(CSocket *this); // 0x004802E3
CListenSocket *CListenSocket_Constructor_Listen(CListenSocket *this, uint16_t port); // 0x0048030C
CSocket *CListenSocket_Delete(CSocket *this); // 0x0048037B
uint16_t CListenSocket_Listen(CListenSocket *this, uint16_t port, int backlog); // 0x00480397
CListenSocket *CListenSocket_ListenRange(CListenSocket *this, int startPort, int endPort, int backlog); // 0x004804E8
int CListenSocket_Accept(CListenSocket *this, uint32_t *addr); // 0x0048065E
int SetSocketBlocking(int s, int blocking); // 0x004806E4
CSocket *CSocketBuffer_Add(CSocketBuffer *this, CSocket *socket, uint8_t *buf, int size); // 0x00480710
CSocket *CSocket_Destructor(CSocket *this, uint8_t v); // 0x004807A0
CSocket *CListenSocket_Destructor(CSocket *this, uint8_t v); // 0x004807D0

#endif /* SOCKET_H_ */
