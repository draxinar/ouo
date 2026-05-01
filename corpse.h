#ifndef CORPSE_H_
#define CORPSE_H_

#include <stdint.h>

__extension__ typedef struct CCorpse CCorpse;

/*
 * Corpse container created on mobile death (0xCC bytes). Extends
 * CContainer with per-equip-slot item serials and a snapshot of the
 * deceased's body type, facing, and notoriety.
 */
struct CCorpse {
	CContainer container;           // 0x00
	uint32_t equipSlots[26];        // 0x5C - original item serial per equip slot
	uint16_t corpseBodyType;        // 0xC4 - deceased body type
	uint8_t corpseDirection;        // 0xC6
	uint8_t corpseNotoriety;        // 0xC7
	uint32_t decayTick;             // 0xC8
};

#define CORPSE_BODYTYPE 0x2006

struct CString;

__extension__ typedef struct CDataBuffer CDataBuffer;

extern char g_CorpseNameBuf[256]; // 0x006BA830
extern const uint8_t g_articleLookup[21];

#endif /* CORPSE_H_ */
