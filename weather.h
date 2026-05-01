#ifndef WEATHER_H_
#define WEATHER_H_

#include <stdint.h>

/*
 * Weather zone system (0x004E04FC-0x004E0E88). Circular zones loaded
 * from weather.txt simulate precipitation intensity ramp/wane cycles,
 * then push PDSTRUCT_WEATHERCHANGE (0x65) packets at subscribers.
 */

#ifndef WEATHER_H
#define WEATHER_H

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;

/*
 * One circular weather zone (328 bytes) carrying precipitation
 * simulation state. Allocated at 0x004E0692, initialized by
 * CWeatherRegion_Init (0x004E0B18). Regions are linked in prepend order
 * off WeatherManager.first; iteration follows the prev pointer at
 * +0x144.
 */
__extension__ typedef struct CWeatherRegion CWeatherRegion;
struct CWeatherRegion {
	char name[256];               // +0x000
	int16_t centerX;              // +0x100
	int16_t centerY;              // +0x102
	int32_t radius;               // +0x104 (Chebyshev distance)
	int32_t avgTemperature;       // +0x108
	int32_t currentIntensity;     // +0x10C (display value, tracks intensity)
	int32_t currentTemp;          // +0x110
	int32_t _pad114;              // +0x114
	int32_t precipChance;         // +0x118 (0-100 per tick)
	int32_t extremeTempChance;    // +0x11C
	int32_t extremeWeatherChance; // +0x120
	int32_t intensity;            // +0x124 (simulated, ramps +-5 per tick)
	int32_t duration;             // +0x128 (max intensity target, capped at 70)
	int32_t cooldownCounter;      // +0x12C
	int32_t cooldownTarget;       // +0x130
	int32_t moveSpeed;            // +0x134
	int32_t isPrecipitating;      // +0x138
	int32_t isWaning;             // +0x13C
	CWeatherRegion *next;         // +0x140
	CWeatherRegion *prev;         // +0x144
};

/*
 * Singleton at g_WeatherManager (0x00648278) holding the weather-region
 * list head/tail and the current season.
 */
__extension__ typedef struct WeatherManager WeatherManager;
struct WeatherManager {
	int32_t _pad00;        // +0x00
	int32_t season;        // +0x04 (0=winter, 1=spring, 2=summer, 3=fall)
	int32_t _pad08;        // +0x08
#if __SIZEOF_POINTER__ == 8
	int32_t _pad64;        // 64-bit alignment pad
#endif
	CWeatherRegion *first; // +0x0C
	CWeatherRegion *last;  // +0x10
};

#endif

/*
 * A* path/weather-region node (32 bytes). Shared by the weather subsystem
 * and the NPC pathfinder (constructor 0x0049E882).
 */
__extension__ typedef struct WeatherNode WeatherNode;
struct WeatherNode {
	double x;          // +0x00
	double y;          // +0x08
	void *dataPtr;     // +0x10
	WeatherNode *next; // +0x14
	WeatherNode *prev; // +0x18
	void *parent;      // +0x1C (PathNodeList*)
};

/*
 * Header for a WeatherNode chain (12 bytes), constructor
 * PathNodeList_Constructor (0x0049E814) in npc.c.
 */
__extension__ typedef struct PathNodeList PathNodeList;
struct PathNodeList {
	int closed;        // +0x00
	int count;         // +0x04
	WeatherNode *head; // +0x08
};

extern WeatherManager g_WeatherManager;

void WeatherNodeList_DestroyChildren(PathNodeList *this); // 0x0049E83F
void *WeatherNode_NewConstruct(PathNodeList *parent, int x, int y, void *dataPtr); // 0x0049E882
void WeatherNode_Unlink(WeatherNode *self); // 0x0049E97D
int WeatherManager_getAverageWeather(CLocation *loc); // 0x004E04FC
int WeatherManager_startup(void); // 0x004E05DE
void WeatherManager_UpdateWeather(void); // 0x004E096F
const char *GetSeasonAsText(int season); // 0x004E0A95
void WeatherManager_AdvanceSeason(void); // 0x004E0AEB
void CWeatherRegion_Init(CWeatherRegion *r); // 0x004E0B18
int CWeatherRegion_getCurrentWeather(CWeatherRegion *r); // 0x004E0B81
void WeatherManager_sendWeatherToPlayer(CItem *playerPtr); // 0x004E0E3D

#endif /* WEATHER_H_ */
