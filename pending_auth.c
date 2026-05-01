/*
 * PendingAuth - nonce-keyed table for login-to-game cipher handoff.
 *
 * When a client picks a shard in BRITANNIA_SELECT, the cipher context
 * is parked here under a random nonce; the client presents the nonce
 * on the new game connection to retrieve it, avoiding the races that
 * the binary's globals would have had under concurrent logins.
 *
 * CUSTOM - no binary equivalent.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pending_auth.h"

#define MAX_PENDING 16
#define EXPIRY_SECS 30

// Custom - nonce-keyed pending authentication slots
static PendingAuth pending[MAX_PENDING];

static uint32_t
generateNonce(void)
{
	uint32_t nonce = 0;
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		read(fd, &nonce, 4);
		close(fd);
	} else {
		nonce = (uint32_t)rand();
	}
	// Avoid zero - a zero seed means "no seed" in the client protocol.
	if (nonce == 0)
		nonce = 1;
	return nonce;
}

/*
 * Custom - PendingAuth_Create
 *
 * Allocate a pending auth entry with the given cipher config.
 * Returns the random nonce to send in USER_SERVER (0x8C).
 */
uint32_t
PendingAuth_Create(int gameCipher, int bfIndex, uint16_t *packetTable, int xorVersion, int clientEnum)
{
	int i;
	PendingAuth *pa;

	PendingAuth_ExpireStale();

	// Find a free slot (nonce == 0 means empty).
	for (i = 0; i < MAX_PENDING; i++) {
		if (pending[i].nonce == 0)
			break;
	}
	if (i == MAX_PENDING) {
		// Table full - evict oldest entry.
		int oldest = 0;
		for (i = 1; i < MAX_PENDING; i++) {
			if (pending[i].created < pending[oldest].created)
				oldest = i;
		}
		i = oldest;
	}

	pa = &pending[i];
	pa->nonce = generateNonce();
	pa->gameCipher = gameCipher;
	pa->bfIndex = bfIndex;
	pa->packetTable = packetTable;
	pa->xorVersion = xorVersion;
	pa->clientEnum = clientEnum;
	pa->created = time(NULL);

	return pa->nonce;
}

/*
 * Custom - PendingAuth_Find
 *
 * Look up a pending auth entry by nonce. Returns NULL if not found.
 */
PendingAuth *
PendingAuth_Find(uint32_t nonce)
{
	int i;

	if (nonce == 0)
		return NULL;
	for (i = 0; i < MAX_PENDING; i++) {
		if (pending[i].nonce == nonce)
			return &pending[i];
	}
	return NULL;
}

/*
 * Custom - PendingAuth_Remove
 *
 * Remove a pending auth entry by nonce (clear the slot).
 */
void
PendingAuth_Remove(uint32_t nonce)
{
	int i;

	for (i = 0; i < MAX_PENDING; i++) {
		if (pending[i].nonce == nonce) {
			memset(&pending[i], 0, sizeof(pending[i]));
			return;
		}
	}
}

/*
 * Custom - PendingAuth_ExpireStale
 *
 * Remove entries older than EXPIRY_SECS seconds.
 */
void
PendingAuth_ExpireStale(void)
{
	int i;
	time_t now = time(NULL);

	for (i = 0; i < MAX_PENDING; i++) {
		if (pending[i].nonce != 0 && now - pending[i].created > EXPIRY_SECS)
			memset(&pending[i], 0, sizeof(pending[i]));
	}
}
