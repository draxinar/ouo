/*
 * CListenSocket - bound TCP listener that accepts new game clients.
 *
 * Binds and listens on the configured server port, and on each
 * accepted connection allocates a CUserSock and wires it into the
 * main socket poll loop.
 */

#include <stdint.h>
#include <stdio.h>

#include "usersock.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
CSocket_vtable VTABLE_CListenSocket = {
	(CSocket * (*)(CSocket *, uint8_t)) CListenSocket_Destructor,
	(CSocket * (*)(CSocket *)) CUserSock_Accept,
	(CSocket * (*)(CSocket *)) CSocket_Send,
	(int (*)(CSocket *))CSocket_IsBuffer,
	(CSocket * (*)(CSocket *)) CSocket_Close,
};
#pragma GCC diagnostic pop
