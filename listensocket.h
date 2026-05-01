#ifndef LISTENSOCKET_H_
#define LISTENSOCKET_H_

#include <stdint.h>

#include "socket.h"

__extension__ typedef struct CSocket_vtable CSocket_vtable;
__extension__ typedef struct CListenSocket CListenSocket;

/*
 * Accept-side CSocket variant bound to a listening TCP port. Dispatch
 * goes through VTABLE_CListenSocket; concrete subclasses handle each
 * accepted connection.
 */
struct CListenSocket {
	CSocket socket;
	uint16_t port;
};

extern CSocket_vtable VTABLE_CListenSocket;

#endif /* LISTENSOCKET_H_ */
