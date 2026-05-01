/*
 * Log - tick-timing histogram and formatted CLogFile output.
 *
 * RecordTickTiming rolls a 128-entry circular buffer of tick durations
 * to surface the running average and peak; CLogFile wraps the binary's
 * printf-style log sink for world events. Both share the stdout/syslog
 * writer used by the server console.
 */

#include <stdint.h>

#include "cstring.h"
#include "dat.h"
#include "log.h"
#include "main.h"
#include "time.h"

static void CLogFile_Destructor(CLogFile *this); // 0x0046CD59

/*
 * 0x00467E11 - RecordTickTiming
 *
 * Records the tick duration into a circular 128-entry buffer and
 * refreshes the running average and peak when the tick took >= 10ms.
 */
void
RecordTickTiming(uint32_t startTime)
{
	int i;

	g_LastTickElapsed = GetTickCount_UO() - startTime;
	if ((int32_t)g_LastTickElapsed < 10)
		return;

	g_TimingBuffer[g_TimingBufIndex] = g_LastTickElapsed;
	g_TimingBufIndex = (g_TimingBufIndex + 1) & 0x7F;
	g_TimingField9EC = 0;
	g_TimingField9F0 = 0;
	for (i = 0; i < 128; i++) {
		if (g_TimingBuffer[i] > g_TimingField9F0)
			g_TimingField9F0 = g_TimingBuffer[i];
		g_TimingField9EC += g_TimingBuffer[i];
	}
	g_TimingField9EC >>= 7;
}

/*
 * 0x00467ECE - AccumulateTimingStats
 *
 * Updates the running peak/total/count from the last tick, ignoring
 * ticks under 10ms.
 */
void
AccumulateTimingStats(void)
{
	if ((int32_t)g_LastTickElapsed < 10)
		return;
	if (g_LastTickElapsed > g_TimingPeakTime)
		g_TimingPeakTime = g_LastTickElapsed;
	g_TimingTotalTime += g_LastTickElapsed;
	g_TimingTickCount++;
}

/*
 * 0x00467F14 - LogTimingStats
 *
 * Flushes accumulated "Average: N Peak: M" timing to the event log
 * and resets the counters. Called every 0x3FF ticks.
 */
void
LogTimingStats(void)
{
	uint32_t avg;
	CString str;

	if (g_TimingTickCount == 0)
		return;

	avg = g_TimingTotalTime / g_TimingTickCount;

	CString_Constructor(&str, "Average: ");
	CString_ConcatInt(&str, (int)avg);
	CString_AppendCStr(&str, " Peak: ");
	CString_ConcatInt(&str, (int)g_TimingPeakTime);

	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "timing", "timing", CString_GetBuffer(&str));

	g_TimingPeakTime = 0;
	g_TimingTotalTime = 0;
	g_TimingTickCount = 0;

	CString_Destructor(&str);
}

/*
 * 0x00467FEB - Noop_467FEB
 *
 * Empty no-op called periodically from the server main loop.
 */
void
Noop_467FEB(void)
{
}

/*
 * 0x0046CD59 - CLogFile::~CLogFile
 *
 * No-op destructor for the printl CLogFile, registered via atexit.
 */
static __attribute__((unused)) void
CLogFile_Destructor(CLogFile *this)
{
	USED(this);
}
