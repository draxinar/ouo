#ifndef REGION_H_
#define REGION_H_

#include <stdint.h>

/*
 * Region system. Binary RegionManager lives at 0x006990B0 and loads
 * regions.mul; this server loads the text equivalent regions.txt.
 * Regions are point-tested via CRegion_isCoordInside (0x004A3F8F) and
 * looked up through a spatial grid at RegionManager+0x64C.
 */

#include "stl.h"

__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CResList CResList;
__extension__ typedef struct CResManager CResManager;
__extension__ typedef struct CVector CVector;
__extension__ typedef struct PathNodeList PathNodeList;
__extension__ typedef struct WeatherNode WeatherNode;
#define REGION_NAME_LEN 40      // 0x28 bytes at wrapped CRegion+0x04
#define REGION_DESC_LEN 40      // 0x28 bytes at wrapped CRegion+0x38

/*
 * Wrapped region entry (0x6C bytes, active data through +0x6A) as laid
 * out by the server-side parser (0x004A4A71). The 4-byte prefix sits
 * before the bare CRegion fields; isCoordInside at 0x004A3F8F and
 * RegionGrid_SearchTag at 0x004A56FD both rely on this offset.
 */
__extension__ typedef struct CRegion CRegion;
// clang-format off
struct CRegion {
	uint32_t prefix;               // +0x00
	char name[REGION_NAME_LEN];    // +0x04 (e.g. "Justice_Ocllo")
	int16_t x;                     // +0x2C
	int16_t y;                     // +0x2E
	int16_t width;                 // +0x30
	int16_t height;                // +0x32
	int16_t zMin;                  // +0x34
	int16_t zMax;                  // +0x36
	char name2[REGION_DESC_LEN];   // +0x38 (e.g. "Ocllo City")
	int16_t weatherSeason;         // +0x60
	int16_t weatherDay;            // +0x62
	int16_t weatherNight;          // +0x64
	uint8_t type;                  // +0x66 (dungeon entrance type 0-8)
	// +0x67 natural alignment
	int16_t lightLevel;            // +0x68
};
// clang-format on

/*
 * Spatial grid for region lookups, reached through RegionManager+0x64C.
 * Buckets are indexed by block (x >> shift, y >> shift); the populator
 * at 0x004A6370 walks the matching bucket and filters by isCoordInside.
 */
__extension__ typedef struct CRegionGrid CRegionGrid;
struct CRegionGrid {
	int width;        // +0x00
	int height;       // +0x04
	int shift;        // +0x08 (log2(blockSize))
	int xOffset;      // +0x0C
	int xMaxBlock;    // +0x10
	int yOffset;      // +0x14
	int yMaxBlock;    // +0x18
#if __SIZEOF_POINTER__ == 8
	int _pad64;       // 64-bit alignment pad
#endif
	CVector *buckets; // +0x1C (width*height CVectors)
};

struct CResManager;

struct CLocation;
struct CResList;
struct CResManager;

extern CResManager g_RegionByFileRM; // 0x006990B4
extern CResManager g_RegionByNameRM; // 0x006992CC
extern CResManager g_RegionRM; // 0x006994E4
extern const char *g_RegionsFilePath; // 0x006999DC
extern int g_RegionByNameCount; // 0x006DA96C
extern int g_RegionAllCount; // 0x006DA970
extern CRegionGrid *g_RegionGrid;

int Region_HasWeatherData(CRegion *region); // 0x0042FEA0
void *WeatherNode_ScalarDelete(WeatherNode *this, int flags); // 0x004A3E30
void *WeatherNodeList_ScalarDelete(PathNodeList *this, int flags); // 0x004A3E90
CRegion *CRegion_Init(CRegion *this); // 0x004A3EC0
CRegion *CRegion_Assign(CRegion *this, CRegion *src); // 0x004A3ED6
int CRegion_isCoordInside(CRegion *r, CLocation *loc); // 0x004A3F8F
void CRegion_CopyFrom(CRegion *this, CRegion *src); // 0x004A4021
int CRegion_GetArea(CRegion *this); // 0x004A4103
void CRegion_Destructor(CRegion *this); // 0x004A411F
int CRegion_ValidateBounds(CRegion *this); // 0x004A412A
void RegionManager_Constructor(void); // 0x004A42F0
void RegionManager_InitOccupancy(void); // 0x004A4408
void RegionManager_Init(void); // 0x004A4489
void RegionManager_AddRegion(CRegion *region); // 0x004A45B1
int RegionManager_PopulateByLocation(CResList *outList, CLocation *loc); // 0x004A4DB1
int RegionManager_PopulateFromRM(CResList *outList, CLocation *loc, CResManager *rm); // 0x004A4E53
void RegionManager_LoadRegions(void); // 0x004A4EEC
int RegionManager_GetLocalizedDesc(CString *outStr, int16_t *outLoc, CLocation *inLoc1, CLocation *inLoc2); // 0x004A5164
void RegionManager_SearchByPrefix(CResList *outList, CLocation *loc, const char *prefix); // 0x004A5620
uintptr_t *RegionGrid_SearchTag(CVector *list, const char *tag); // 0x004A56FD
int RegionManager_inJusticeRegion(int16_t x, int16_t y, int16_t z); // 0x004A5762
int RegionManager_isGuardedRegion(int16_t x, int16_t y, int16_t z); // 0x004A5848
int RegionManager_IsInRegion(CLocation *loc, const char *prefix); // 0x004A58F4
int RegionManager_isInCityRegion(int16_t x, int16_t y, int16_t z); // 0x004A599F
void RegionManager_FindLVNRegion(CResManager *rm, int startX, int startY, int endX, int endY, char *outBuf, int maxLen); // 0x004A5A4B
int RegionManager_isHousingOkay(int16_t x, int16_t y, int16_t z, int flag); // 0x004A5BBA
int RegionManager_areSpellsOkay(int16_t x, int16_t y, int16_t z); // 0x004A5DF4
int RegionManager_IsInSpecialArea(CLocation *loc); // 0x004A5EA0
void RegionManager_AllocGrid(void); // 0x004A5FC3
CRegion *CRegion_ScalarDelete(CRegion *this, int flag); // 0x004A6140
CRegionGrid *CRegionGrid_Constructor(CRegionGrid *this, int xMin, int yMin, int xMax, int yMax, int shift); // 0x004A6170
void CRegionGrid_ClearAll(CRegionGrid *grid); // 0x004A62F0
void CRegionGrid_Populate(CRegionGrid *grid, CVector *outList, CLocation *loc); // 0x004A6370
void CRegionGrid_InsertRegion(CRegionGrid *this, CRegion *region, int x, int y, int xEnd, int yEnd); // 0x004A6EA0
void OperatorDelete(void *ptr); // 0x004E8110
void *OperatorNew(uint32_t size); // 0x004E84C0
void Noop_4A3E28(void *player, const char *valueBuf, int param); // 0x004A3E28

#endif /* REGION_H_ */
