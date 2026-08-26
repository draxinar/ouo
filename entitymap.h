#ifndef ENTITYMAP_H_
#define ENTITYMAP_H_

#include "stdptrlist.h"

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CVector CVector;
struct CItem;
struct CLocation;
struct CVector;

/*
 * CEntityMap - binary-exact entity spatial map (0x24 bytes).
 *
 * Binary: heap-allocated via operator new(0x24), constructed by
 * CItemMap ctor (0x00457070), CMobileGrid ctor (0x00461dd0).
 * Each map divides the world into blockShift-sized tiles (1 << blockShift),
 * with a StdPtrList per block storing entity pointers.
 *
 * Three instances:
 *   g_ItemMap   (0x00645AE8) - players only, blockShift=6 (64-tile blocks)
 *   g_MobileMap (0x0064706C) - all mobiles, blockShift=6
 *   g_NPCMap    (0x00698974) - NPCs only
 */
__extension__ typedef struct CEntityMap {
	int gridW;          /* +0x00: grid columns */
	int gridH;          /* +0x04: grid rows */
	int blockShift;     /* +0x08: coordinate shift (6 = 64-tile blocks) */
	int originX;        /* +0x0C: grid origin X (startX >> blockShift) */
	int endX;           /* +0x10: grid end X */
	int originY;        /* +0x14: grid origin Y (startY >> blockShift) */
	int endY;           /* +0x18: grid end Y */
#if __SIZEOF_POINTER__ == 8
	int _pad64;         // Custom: 64-bit - align pointer to 8-byte boundary
#endif
	StdPtrList *blocks; /* +0x1C: array of StdPtrList (12 bytes each) */
	int count;          /* +0x20: total entity count */
} CEntityMap;

// 0x0042F8A7 - Sort serial vector and validate Z positions
struct CBlockManager;

/*
 * 0x00421580 - Range query to CList: find entities within Chebyshev
 * distance. Populates list with (WTYPE_OBJ, entity->serial) entries.
 */
struct CList;

extern CEntityMap *g_ItemMap; // 0x00645AE8
extern CEntityMap *g_MobileMap; // 0x0064706C
extern CEntityMap *g_NPCMap; // 0x00698974

#endif /* ENTITYMAP_H_ */
