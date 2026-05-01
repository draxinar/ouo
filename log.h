#ifndef LOG_H_
#define LOG_H_

#include <stdint.h>

// Forward decl: CLogFile print-level config lives in log.c (g_LogFile, 0x00697A40).
__extension__ typedef struct CLogFile CLogFile;

/*
 * Stub event logger at g_EventLogger (0x00699A40). The binary never
 * enables it - EventLogger_Log always returns 0.
 */
__extension__ typedef struct EventLogger EventLogger;
struct EventLogger {
	int enabled;
};

extern EventLogger g_EventLogger; // 0x00699A40

int EventLogger_Log(EventLogger *logger, uint32_t accountNum, uint32_t charNum, uint32_t serial, const char *name, const char *category, const char *subcategory,
        const char *message); // 0x0046CD9C

void RecordTickTiming(uint32_t startTime); // 0x00467E11
void AccumulateTimingStats(void); // 0x00467ECE
void LogTimingStats(void); // 0x00467F14
void Noop_467FEB(void); // 0x00467FEB

#endif /* LOG_H_ */
