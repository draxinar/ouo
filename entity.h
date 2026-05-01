#ifndef ENTITY_H_
#define ENTITY_H_

#include <stdint.h>

__extension__ typedef struct CEntity CEntity;
__extension__ typedef struct CEntity_vtable CEntity_vtable;
__extension__ typedef struct CItem CItem;

#include "location.h"

__extension__ typedef struct CList CList;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CResourceEntity CResourceEntity;
__extension__ typedef struct CResourceNode CResourceNode;
__extension__ typedef struct CResourceType CResourceType;
__extension__ typedef struct CString CString;
__extension__ typedef struct ScriptAttachNode ScriptAttachNode;

#include "vtable.h"

/*
 * Root of the entity hierarchy (0x10 bytes). Every world object
 * (CResourceEntity -> CItem -> CContainer -> CMobile -> CPlayer/CNPC,
 * plus siblings like CMulti/CEgg/CSignpost) starts with these fields.
 */
struct CEntity {
	CEntity_vtable *vtable;   // 0x00
	uint16_t bodyType;        // 0x04 - graphic ID
	uint8_t removedFromWorld; // 0x06 - 1=removed/stale, 0=active
	uint8_t _ent_pad07;       // 0x07
	uint16_t color;           // 0x08 - hue
	CLocation location;       // 0x0A
};

/*
 * Virtual dispatch table: entity->vtable->fn[byte_offset / 4]. One
 * instance per concrete class, shared across all objects of that type.
 */
struct CEntity_vtable {
	vfunc_t fn[VTABLE_NUM_SLOTS];
};

/*
 * Entity type tags. Binary uses actual vtable pointers (unique per class).
 * VT_ForType() and VT_GetType() convert between ETYPE_* and vtable pointers.
 * Keep in sync with Save dispatch in dynamic.c.
 */
enum EntityTypeTag {
	ETYPE_ITEM = 1,       // @=D (CItem, 0x50 bytes, vtable 0x005EF620)
	ETYPE_CONTAINER = 2,  // @=C (CContainer, 0x5C bytes, vtable 0x005EE888)
	ETYPE_WEAPON = 3,     // @=W (CWeapon, 0x58 bytes, vtable 0x005F00F8)
	ETYPE_MOBILE = 4,     // @=M (CMobile, 0x37C bytes, vtable 0x005EEF48)
	ETYPE_NPC = 5,        // @=N (CNPC, 0x474 bytes, vtable 0x005EF1F8)
	ETYPE_PLAYER = 6,     // @=P (CPlayer, 0x458 bytes, vtable 0x005EEA50)
	ETYPE_SHOPKEEPER = 7, // @=S (shopkeeper NPC, 0x480 bytes, vtable 0x005EFDE8)
	ETYPE_GUARD = 12,     // @=G (guard NPC, same struct as shopkeeper)
	ETYPE_MULTI = 8,      // @=X (CMulti, 0xCC bytes, vtable 0x005EF7C8)
	ETYPE_EGG = 9,        // @=E (CEgg, 0x50 bytes, vtable 0x005EFB38)
	ETYPE_SIGNPOST = 10,  // @=Z (CSignpost, 0x64 bytes, vtable 0x005EF988)
	ETYPE_BOAT = 11,      // @=B (CItem variant, 0x64 bytes)
};

// Helpers for setting/getting entity type via vtable pointers.
#define CEntity_SetType(ent, type) ((ent)->vtable = VT_ForType(type))
#define CEntity_GetType(ent)       VT_GetType((ent)->vtable)

/*
 * 0x00491210 - CEntity::GetLocation
 *
 * Returns a pointer to the entity's CLocation field.
 */
static inline CLocation *
CEntity_GetLocation(CEntity *ent)
{
	return &ent->location;
}

/*
 * Wombat script event types - sparse indices into CScript trigger array
 * (71 slots at offset 0x18). DispatchEvent jump table at 0x0042D7B8.
 * Events > 0x43 fall through to the default handler with no parameters.
 */
enum EventType {
	/* Combat events */
	Speech = 0x00,       /* obj speaker, str text */
	GotAttacked = 0x01,  /* obj attacker */
	KilledTarget = 0x02, /* obj victim */
	Death = 0x04,        /* obj attacker, obj corpse */
	SawDeath = 0x05,     /* obj attacker */
	FightPulse = 0x06,   /* obj opponent */
	WasHit = 0x07,       /* obj attacker, int damage */
	IsHitting = 0x22,    /* obj victim, int damage */
	MobIsHitting = 0x43, /* obj victim, int damage */

	/* Item interaction events */
	UseItem = 0x17,       /* obj user */
	UseObject = 0x33,     /* obj user (ooruse - out-of-range use) */
	LookedAt = 0x1C,      /* obj looker */
	WasDropped = 0x1B,    /* obj user */
	WasGotten = 0x1E,     /* obj user */
	Give = 0x1D,          /* obj givenObj */
	Equip = 0x2C,         /* obj user */
	Unequip = 0x2D,       /* obj user */
	IsStackableOn = 0x2E, /* obj target */
	StackOnto = 0x2F,     /* obj target */

	/* Targeting events */
	TargetObject = 0x18,    /* obj user, obj target */
	TargetLocation = 0x19,  /* obj user, int x, int y, int z */
	TargetObjectPre = 0x38, /* obj user, obj target */

	/* Script/callback events */
	Creation = 0x0F,     /* (filter-based) */
	ObjectLoaded = 0x36, /* (no params) */
	DeleteEntity = 0x2B, /* (no params) */
	Callback = 0x21,     /* int callbackId (filter-based) */
	Message = 0x16,      /* obj arg */

	/* UI events */
	GumpResponse = 0x37,   /* int gumpId, obj user, int button */
	StringResponse = 0x3A, /* obj user, str text */
	PickedObj = 0x24,      /* obj user, int type */
	HueSelected = 0x25,    /* obj user, int hue */

	/* Range events */
	EnterRange = 0x10, /* obj target */
	LeaveRange = 0x11, /* obj target */

	/* Access/snooping */
	ObjAccess = 0x3D,  /* (filter-based) */
	StolenFrom = 0x3C, /* obj thief */

	/* World events */
	LoginAppearance = 0x3F,    /* (no params) */
	CheckTransfer = 0x40,      /* obj player, int flag */
	Transfer = 0x41,           /* obj player, int flag */
	FameChanged = 0x44,        /* (no params) */
	KarmaChanged = 0x45,       /* (no params) */
	MurderCountChanged = 0x46, /* (no params) */

	/* Vendor events */
	Shop = 0x3B,   /* int value */
	CanBuy = 0x42, /* obj buyer, obj vendor, int quantity */
};

// Sentinel pointer stored in spatialNext during RemoveFromWorld/AddToWorld.
// Binary: literal 0x12345678.
#define SPATIAL_SENTINEL ((CItem *)(uintptr_t)0x12345678)

extern uint32_t g_EntityCount; // 0x0068B37C
extern uint32_t g_ResourceEntityCount; // 0x0068B390

void CEntity_SetBodyType(CItem *item, uint16_t bodyType); // 0x00420F60
char *Entity_ExecuteEvent(CEntity *this, int eventType, ...); // 0x0042B92F
int CEntity_GetRemovedFromWorld(CItem *item); // 0x00432290
CEntity *CEntity_Constructor(CEntity *this); // 0x004858AF
void CEntity_Destructor(CEntity *this); // 0x004858FA
uint32_t GetLandTileFlags(uint16_t tileID, int moveType); // 0x004862CF
void CEntity_ProcessNotorietyTimer(CItem *this, int timerType); // 0x0048FFD2
void CEntity_RemoveFromWorld(CItem *this); // 0x004DA050
void CEntity_AddToWorld(CItem *entity); // 0x004DA5C5

#endif /* ENTITY_H_ */
