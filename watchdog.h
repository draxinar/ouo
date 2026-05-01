#ifndef WATCHDOG_H_
#define WATCHDOG_H_

#include <stdint.h>

extern int g_WatchdogEnabled;
extern uint32_t g_WatchdogTimeoutMs;

void Watchdog_Init(void);
void Watchdog_Shutdown(void);
void Watchdog_Heartbeat(void);

#endif /* WATCHDOG_H_ */
