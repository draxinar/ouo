#ifndef BANKDEFS_H_
#define BANKDEFS_H_

#include <stdint.h>
#include <stdio.h>

__extension__ typedef struct CEntity CEntity;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CResBankSet CResBankSet;
__extension__ typedef struct CResBankSetMember CResBankSetMember;
__extension__ typedef struct CResBankVertex CResBankVertex;

/*
 * Terrain set member (0x94 bytes). One entry in a CResBankSet's chain.
 */
struct CResBankSetMember {
	int32_t subtype;                // 0x00 - interpretation depends on parent set type
	int32_t tileId;                 // 0x04
	char name[128];                 // 0x08
	CResBankSetMember *prev;        // 0x88
	CResBankSetMember *next;        // 0x8C
	CResBankSet *parent;            // 0x90
};

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CResBankSetMember) == 0x94, "CResBankSetMember size mismatch");
#endif

/*
 * Terrain set definition (0x98 bytes). Loaded from bankdefs.txt or
 * bankdefs.mul and linked into the global list at g_ResBankSetListHead.
 */
struct CResBankSet {
	int32_t type;                   // 0x00 - WALL(0),HOUSE(1),TREE(2),TERRAIN(3),
	                                //        ROOF(4),FLATROOF(5),COASTLINE(6),TRANSITION(7)
	int16_t hue;                    // 0x04
	int16_t color;                  // 0x06
	char name[128];                 // 0x08
	int32_t memberCount;            // 0x88
#if __SIZEOF_POINTER__ == 8
	int32_t _pad64;                 // 64-bit alignment pad
#endif
	CResBankSetMember *memberTail;  // 0x8C
	CResBankSet *next;              // 0x90
	CResBankSet *prev;              // 0x94
};

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CResBankSet) == 0x98, "CResBankSet size mismatch");
#endif

/*
 * Terrain vertex (0x24 bytes). Node in a 4-connected graph used to link
 * terrain sets at (x, y) positions.
 */
// clang-format off
struct CResBankVertex {
	int16_t x;                      // 0x00
	int16_t y;                      // 0x02
	int32_t setIndex;               // 0x04 - index into global set list
	int32_t vertexIndex;            // 0x08
	int32_t n;                      // 0x0C - neighbour vertex index: north
	int32_t s;                      // 0x10 - south
	int32_t w;                      // 0x14 - west
	int32_t e;                      // 0x18 - east
#if __SIZEOF_POINTER__ == 8
	int32_t _pad64;                 // 64-bit alignment pad
#endif
	CResBankVertex *listNext;       // 0x1C
	CResBankVertex *listPrev;       // 0x20
};
// clang-format on

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CResBankVertex) == 0x24, "CResBankVertex size mismatch");
#endif

/*
 * Terrain distribution vertex (0x20 bytes on 32-bit). Linked list node
 * consumed by CResBankManager_ProcessEntry for polygon fill, line draw,
 * and interpolated curve passes.
 */
__extension__ typedef struct CDistribVertex CDistribVertex;
// clang-format off
struct CDistribVertex {
	double x;                       // 0x00
	double y;                       // 0x08
	int32_t setIndex;               // 0x10 - index into CResBankSet global list
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad14[4];              // 64-bit alignment pad
#endif
	CDistribVertex *next;           // 0x14
	CDistribVertex *prev;           // 0x18
	void *parent;                   // 0x1C - back-pointer to owning CDistribEntry
};
// clang-format on

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CDistribVertex) == 0x20, "CDistribVertex size mismatch");
#endif

/*
 * Terrain distribution entry (0x0C bytes on 32-bit). Holds a CDistribVertex
 * chain and a mode flag; processed by CResBankManager_ProcessEntry.
 */
__extension__ typedef struct CDistribEntry CDistribEntry;
struct CDistribEntry {
	int32_t flag;                   // 0x00 - nonzero: polygon mode; zero: line/curve
	int32_t count;                  // 0x04 - vertex/polygon count
	CDistribVertex *headVertex;     // 0x08
};

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(CDistribEntry) == 0x0C, "CDistribEntry size mismatch");
#endif

/*
 * 0x00620468 - TerrainRule
 *
 * Entry in the terrain rule table (0x18 bytes per entry): pairs a
 * terrain set index with an alternate index and four Z thresholds used
 * by the spawn pipeline to pick the appropriate set at a given height.
 */
__extension__ typedef struct TerrainRule TerrainRule;
struct TerrainRule {
	int32_t setIndex;
	int32_t altSetIndex;
	int32_t z0;
	int32_t z1;
	int32_t z2;
	int32_t z3;
};

extern int g_ResBankSpawnDefault; // 0x00620390
extern double g_ResBankFillStep; // 0x006DA950
extern int g_ResBankLineX; // 0x006DA95C
extern CResBankVertex *g_ResBankVertexListHead; // 0x006DA964
extern CResBankSet *g_ResBankSetListHead; // 0x006DA968
extern TerrainRule g_TerrainRules[]; // 0xFFFFFFF1
extern int g_ResBankLineY;

CDistribVertex *CResBankVertex_GetNext(CDistribVertex *dv); // 0x0049E9DE
CResBankSet *ResBankSet_FindByTypeAndColor(int type, int color); // 0x0049EA03
CResBankSet *ResBankSet_LookupByIndex(int index); // 0x0049EA48
int ResBankSet_LookupByName(const char *name); // 0x0049EAAD
void CResBankSet_DumpSetInfo(CResBankSet *set, FILE *fp, int textMode); // 0x0049EB23
int CResBankSet_ContainsMember(CResBankSet *set, int tileId, int subtype); // 0x0049EF98
int CResBankSet_HasBodyType(CResBankSet *set, int bodyType); // 0x0049EFE7
int CResBankSet_HasTransitionType(CResBankSet *set, int bodyType); // 0x0049F044
int CResBankSet_GetMemberIndex(CResBankSet *set, int bodyType); // 0x0049F099
CResBankSetMember *CResBankSet_GetTypeMember(CResBankSet *set, int subtype); // 0x0049F0DE
void CResBankSetMember_DumpInfo(CResBankSetMember *member, CResBankSet *parent, FILE *fp, int textMode); // 0x0049F17D
void CResBankSetMember_RemoveFromSpatialList(CResBankSetMember *member); // 0x0049FC46
void CResBankVertex_DumpInfo(CResBankVertex *vertex, FILE *fp, int textMode); // 0x0049FCD4
CResBankVertex *ResBankVertex_LookupByIndex(int vertexIndex); // 0x0049FFA2
int CResBankVertex_ValidateConnections(CResBankVertex *vertex); // 0x0049FFD7
void CResBankVertex_LinkConnections(CResBankVertex *vertex); // 0x004A0159
void ResBankDistrib_MarkBlock(int mode, CEntity *entity, int x, int y); // 0x004A01F6
void ResBankDistrib_SetStartPoint(int mode, CEntity *entity, double x, double y); // 0x004A030F
void ResBankDistrib_DrawLine(int mode, CEntity *entity, double x, double y); // 0x004A0340
void ResBankDistrib_FillQuad(int mode, CEntity *entity, double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4); // 0x004A0520
void CResBankManager_ProcessEntry(CDistribEntry *entry, CEntity *entity); // 0x004A06D9
void ResBankDistrib_PlaceStatic(int tileId, int x, int y, int hue); // 0x004A0FAB
void ResBankDistrib_PlaceStaticNPC(int tileId, int x, int y, int hue, int heightOff); // 0x004A105E
void ResBankDistrib_ScanBorderX(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff); // 0x004A1115
void ResBankDistrib_ScanBorderY(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff); // 0x004A11B6
void ResBankDistrib_ScanTransition(CResBankVertex *vertex, int xOff, int yOff); // 0x004A1256
void ResBankManager_ScanNPCsForPlacementX(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff); // 0x004A13EF
void ResBankManager_ScanNPCsForPlacementY(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff); // 0x004A18D5
void ResBankDistrib_CreateAndPlace(int tileId, int x, int y, int z, int hue); // 0x004A1DC0
int ResBankDistrib_CheckAdjacentTile(CResBankSet *set, int x, int y); // 0x004A1E1B
void ResBankManager_SpawnPlacement(CItem *unused, CResBankSet *set, int x, int y); // 0x004A1EC5
void ResBankManager_ApplyVertexBorders(CItem *arg); // 0x004A22C6

#endif /* BANKDEFS_H_ */
