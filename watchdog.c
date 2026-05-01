/*
 * Watchdog - infinite-loop detector for the single-threaded main tick.
 *
 * The server runs all gameplay on one thread; if anything inside
 * CTimeManager_Update enters an infinite loop, the process stays
 * alive but stops responding. Watchdog spawns one POSIX thread that
 * watches a heartbeat written by the main thread at the end of each
 * tick. When the heartbeat goes stale beyond g_WatchdogTimeoutMs the
 * watchdog logs, emits a metric, and calls abort(). The existing
 * systemd pipeline (LimitCORE=infinity, OnFailure=ouo-coredump@,
 * Restart=on-failure) then dumps the stack trace to journal and
 * restarts the unit.
 *
 * CUSTOM - no binary equivalent.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "log.h"
#include "time.h"
#include "watchdog.h"

int g_WatchdogEnabled;
uint32_t g_WatchdogTimeoutMs;

static volatile uint32_t g_WatchdogLastHeartbeatMs;
static volatile int g_WatchdogShouldExit;
static pthread_t g_WatchdogThread;
static int g_WatchdogThreadStarted;

static void *Watchdog_ThreadMain(void *arg);

/*
 * Custom - Watchdog_Init
 */
void
Watchdog_Init(void)
{
	int rc;

	if (!g_WatchdogEnabled)
		return;

	g_WatchdogLastHeartbeatMs = GetTickCount_UO();
	g_WatchdogShouldExit = 0;

	rc = pthread_create(&g_WatchdogThread, NULL, Watchdog_ThreadMain, NULL);
	if (rc != 0) {
		fprintf(stderr, "watchdog: pthread_create failed (%d), disabling\n", rc);
		g_WatchdogEnabled = 0;
		return;
	}
	g_WatchdogThreadStarted = 1;

	{
		char buf[128];
		snprintf(buf, sizeof(buf), "timeout=%ums, aborts on stall (coredump + systemd restart)", g_WatchdogTimeoutMs);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "watchdog", "misc", buf);
	}
}

/*
 * Custom - Watchdog_Shutdown
 */
void
Watchdog_Shutdown(void)
{
	if (!g_WatchdogThreadStarted)
		return;

	g_WatchdogShouldExit = 1;
	pthread_join(g_WatchdogThread, NULL);
	g_WatchdogThreadStarted = 0;
}

/*
 * Custom - Watchdog_Heartbeat
 *
 * Called by the main thread after each successful tick. One 32-bit
 * store; no locking needed because the watchdog thread only reads
 * this value to compare against the current time.
 */
void
Watchdog_Heartbeat(void)
{
	if (!g_WatchdogEnabled)
		return;
	g_WatchdogLastHeartbeatMs = GetTickCount_UO();
}

/*
 * Custom - Watchdog_ThreadMain
 *
 * Wakes once per second, reads the main-thread heartbeat
 * and aborts if it has gone stale.
 * The 1-second cadence bounds shutdown latency: Watchdog_Shutdown
 * sets g_WatchdogShouldExit and joins, which returns within ~1s.
 */
static void *
Watchdog_ThreadMain(void *arg)
{
	struct timespec sleepReq;
	uint32_t now, ageMs;

	(void)arg;

	sleepReq.tv_sec = 1;
	sleepReq.tv_nsec = 0;

	while (!g_WatchdogShouldExit) {
		nanosleep(&sleepReq, NULL);
		if (g_WatchdogShouldExit)
			break;

		now = GetTickCount_UO();
		ageMs = now - g_WatchdogLastHeartbeatMs;

		if (ageMs > g_WatchdogTimeoutMs) {
			char buf[128];
			snprintf(buf, sizeof(buf), "tick stall %ums (timeout=%ums), aborting", ageMs, g_WatchdogTimeoutMs);
			EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "watchdog", "error", buf);

			fflush(stdout);
			fflush(stderr);
			abort();
		}
	}

	return NULL;
}
