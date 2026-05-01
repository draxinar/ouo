#ifndef VTABLE_H_
#define VTABLE_H_

/*
 * Binary vtable infrastructure.
 *
 * In UoDemo.exe, each entity class has a vtable (array of function pointers)
 * in .rdata. The first field of CEntity (offset 0x00) is a pointer to its
 * class's vtable. Virtual dispatch indexes into this array by byte offset:
 *   mov edx, [ecx]          ; load vtable pointer
 *   call dword [edx + 0xD0] ; call slot at byte offset 0xD0
 *
 * Binary vtable addresses (in UoDemo.exe .rdata):
 *   CEntity:          0x005EF448
 *   CResourceEntity:  0x005EF4E0
 *   CItem:            0x005EF620
 *   CContainer:       0x005EE888
 *   CWeapon:          0x005F00F8
 *   CMobile:          0x005EEF48
 *   CResourceMobile:  0x005EF1F8
 *   CNPC:             0x005EECF0
 *   CPlayer:          0x005EEA50
 *   CShopkeeper:      0x005EFDE8
 *   CMulti:           0x005EF7C8
 *   CEgg:             0x005EFB38
 *   CSignpost:        0x005EF988
 *   CBulletinBoard:   0x005EE690
 */

struct CItem;
__extension__ typedef struct CItem CItem;

// Generic function pointer for vtable entries.
__extension__ typedef void (*vfunc_t)(void);

// CPlayer has the largest vtable, max slot at byte offset 0x250.
#define VTABLE_NUM_SLOTS (0x254 / 4)

// Vtable slot byte offsets (matching binary layout).
// Use VT_FN(ent, VT_SLOT) to get the function pointer.
#define VT_DTOR                    0x000
#define VT_DROP_AT_FEET            0x004
#define VT_SET_LOCATION            0x008
#define VT_HIDE                    0x00C
#define VT_DETACH_SPATIAL          0x010
#define VT_ATTACH_SPATIAL          0x014
#define VT_IS_PLAYER               0x018
#define VT_IS_CONTAINER            0x01C
#define VT_HAS_RESOURCE_FLAG       0x020
#define VT_GET_VALUE               0x024
#define VT_GET_HEIGHT              0x028
#define VT_GET_SURFACE_H           0x02C
#define VT_GET_FLAGS               0x030
#define VT_GET_NAME                0x034
#define VT_IS_HAIR                 0x038
#define VT_REATTACH_SPATIAL        0x03C
#define VT_GET_SURFACE_FLAGS       0x040
#define VT_CHECK_SURFACE           0x044
#define VT_CHECK_SURFACE_OF        0x048
#define VT_SPEAK_SYS_MSG           0x04C
#define VT_SAY_CUSTRING            0x050
#define VT_SAY_CSTRING             0x054
#define VT_EMOTE_CUSTRING          0x058
#define VT_EMOTE_CSTRING           0x05C
#define VT_SAY_HUED_CUSTRING       0x060
#define VT_SAY_HUED_CSTRING        0x064
#define VT_SAY_TO_CUSTRING         0x068
#define VT_SAY_TO_ENTITY           0x06C
#define VT_EMOTE_HUED_CUSTRING     0x070
#define VT_EMOTE_HUED_CSTRING      0x074
#define VT_EMOTE_TO_CUSTRING       0x078
#define VT_EMOTE_TO_ENTITY         0x07C
#define VT_GET_LOCATION            0x080
#define VT_IS_DOOR                 0x084
#define VT_IS_DOOR_NS              0x088
#define VT_DELETE                  0x090
#define VT_GET_MOVEMENT_TYPE       0x094
#define VT_GET_ITEM_AMOUNT         0x098
#define VT_ITEM_CHECK_9C           0x09C
#define VT_GET_RESOURCE_AMT        0x0A0
#define VT_SAVE_TEXT               0x0A4
#define VT_MOVE_TO                 0x0A8
#define VT_RETURN_TO_TRACKED       0x0AC
#define VT_TRANSFER_TO             0x0B0
#define VT_ADD_TO_CONTAINER        0x0B4
#define VT_ADD_TO_CONT_B8          0x0B8
#define VT_ADD_TO_CONT_BC          0x0BC
#define VT_EQUIP_ON_MOBILE         0x0C0
#define VT_ADD_TO_EQUIP            0x0C4
#define VT_SAVE                    0x0C8
#define VT_GET_MONETARY_VAL        0x0CC
#define VT_IS_MOBILE               0x0D0
#define VT_IS_MOBILE2              0x0D4
#define VT_HAS_ACCESSIBLE_CONTENTS 0x0D8
#define VT_CHECK_DC                0x0DC
#define VT_IS_SPATIAL              0x0E0
#define VT_IS_NPC                  0x0E4
#define VT_IS_VENDOR               0x0E8
#define VT_CHECK_EC                0x0EC
#define VT_IS_REMOVED              0x08C
#define VT_IS_DEAD                 0x0F4
#define VT_IS_WEAPON               0x0F8
#define VT_HAS_CORPSE_EQ           0x0FC
#define VT_EXCLUDED_AMOUNT         0x100
#define VT_HAS_CONTAINER           0x104
#define VT_IS_EQUIPPED_ITEM        0x108
#define VT_GET_LAYER               0x10C
#define VT_IS_MOVEABLE             0x110
#define VT_IS_FREELY_USABLE        0x114
#define VT_IS_FREELY_VIEWABLE      0x118
#define VT_IS_IN_WORLD             0x11C
#define VT_GET_WEIGHT              0x120
#define VT_GET_STORED_WEIGHT       0x124
#define VT_MERGE_INTO              0x128
#define VT_GET_NAME_STRING         0x12C
#define VT_NOTIFY_NEARBY           0x130
#define VT_SEND_ENTITY_UPDATE      0x134
#define VT_GET_DIRECTION           0x138
#define VT_GET_CONTAINER_DIM       0x13C
#define VT_SET_SERIAL              0x140
#define VT_DECAY_TICK              0x144
#define VT_DECAY_PLACE             0x148
#define VT_DECAY_RETURN_HOME       0x14C
#define VT_DECAY_CLEANUP           0x150
#define VT_WEIGHT_RELATED          0x154
#define VT_VALIDATE_REG            0x158
#define VT_FIND_IN_WORLD           0x15C
#define VT_COND_VALIDATE           0x160
#define VT_FIND_IN_TAG_LIST        0x164
#define VT_GET_AMOUNT              0x168
#define VT_APPLY_STATUS_FLAGS      0x16C
#define VT_GET_STATUS_FLAGS        0x170
#define VT_IS_HIDDEN               0x174
#define VT_SET_HIDDEN              0x178
#define VT_SET_MURDER_COUNT        0x17C
#define VT_GET_NOTORIETY           0x180
#define VT_REFRESH_AGGRESSION      0x184
#define VT_CAN_BE_FREELY_AGGRESSED 0x188
#define VT_CAN_BE_AGGRESSED        0x18C
#define VT_ADD_TO_AGGRESSOR_LIST   0x190
#define VT_ADD_TO_ATTACKER_LIST    0x194
#define VT_NOTIFY_DAMAGE           0x198
#define VT_FILL_AGGRO_INFO         0x19C
#define VT_FAME_KARMA_CHANGE       0x1A0
#define VT_GET_MAX_WEIGHT          0x1A4
#define VT_EQUIP_DECAY_TICK        0x1A8
#define VT_DELETE_CONTENTS         0x1AC
#define VT_CANCEL_TRADE            0x1B0
#define VT_EQUIP_ITERATE           0x1B4
#define VT_ON_DEATH                0x1B8
#define VT_PRE_DELETE_CLEAN        0x1BC
#define VT_SET_HP                  0x1C0
#define VT_SET_MAX_HP              0x1C4
#define VT_SET_STAMINA             0x1C8
#define VT_SET_MAX_STAMINA         0x1CC
#define VT_SET_MANA                0x1D0
#define VT_SET_MAX_MANA            0x1D4
#define VT_ADD_HP                  0x1D8
#define VT_ADD_MAX_HP              0x1DC
#define VT_ADD_STAMINA             0x1E0
#define VT_ADD_MAX_STAMINA         0x1E4
#define VT_ADD_MANA                0x1E8
#define VT_ADD_MAX_MANA            0x1EC
#define VT_SET_BASE_STAT           0x1F0
#define VT_ADD_BASE_STAT           0x1F4
#define VT_ADD_STAT_BONUS          0x1F8
#define VT_SET_STAT_BONUS          0x1FC
#define VT_SET_NOTORIETY           0x200
#define VT_PAPERDOLL_TITLE         0x204
#define VT_WALK_CHECK              0x208
#define VT_DO_WALK                 0x20C
#define VT_ON_DEATH_WRAP           0x210
#define VT_GET_SPEED               0x214
#define VT_WALK_Z                  0x218
#define VT_GET_STAMINA             0x21C
#define VT_HANDLE_STAM_DRAIN       0x220
#define VT_STAM_REGEN              0x224
#define VT_STAT_CHECK              0x228
#define VT_SEND_HP_UPDATE          0x22C
#define VT_SET_INVULN              0x230
#define VT_CLR_INVULN              0x234
#define VT_ON_STAT_CHANGE          0x238
#define VT_SET_STAT_ABS            0x23C
#define VT_SKILL_GAIN_NOTIFY       0x240
#define VT_TEST_BEHAVIOR           0x244
#define VT_SET_BEHAVIOR            0x248
#define VT_CLR_BEHAVIOR            0x24C
#define VT_KARMA_HANDLER           0x250

/*
 * Vtable dispatch helper: get function pointer from entity's vtable.
 * Cast to appropriate signature at call site.
 * Example: ((int (*)(void *))VT_FN(ent, VT_IS_PLAYER))(ent)
 */
#define VT_FN(ent, slot) ((ent)->resourceEntity.entity.vtable->fn[(slot) / 4])

// Shorthand for entity-level vtable access (when you have a CEntity *).
#define VT_ENT_FN(ent, slot) ((ent)->vtable->fn[(slot) / 4])

/*
 * Binary vtable boolean dispatches.
 * Match binary virtual function calls: mov edx,[ecx]; call [edx+SLOT]; test eax,eax
 * Parameter must be CItem* (uses VT_FN).
 */
#define VT_IsPlayer(e)    (((int (*)(void *))VT_FN(e, VT_IS_PLAYER))(e))
#define VT_IsContainer(e) (((int (*)(void *))VT_FN(e, VT_IS_CONTAINER))(e))
#define VT_IsHair(e)      (((int (*)(void *))VT_FN(e, VT_IS_HAIR))(e))
#define VT_IsMobile(e)    (((int (*)(void *))VT_FN(e, VT_IS_MOBILE))(e))
#define VT_IsMobile2(e)   (((int (*)(void *))VT_FN(e, VT_IS_MOBILE2))(e))
#define VT_IsSpatial(e)   (((int (*)(void *))VT_FN(e, VT_IS_SPATIAL))(e))
#define VT_IsNPC(e)       (((int (*)(void *))VT_FN(e, VT_IS_NPC))(e))
#define VT_IsVendor(e)    (((int (*)(void *))VT_FN(e, VT_IS_VENDOR))(e))
#define VT_IsRemoved(e)   (((int (*)(void *))VT_FN(e, VT_IS_REMOVED))(e))
#define VT_IsDead(e)      (((int (*)(void *))VT_FN(e, VT_IS_DEAD))(e))
#define VT_IsWeapon(e)    (((int (*)(void *))VT_FN(e, VT_IS_WEAPON))(e))
#define VT_IsEquipped(e)  (((int (*)(void *))VT_FN(e, VT_IS_EQUIPPED_ITEM))(e))
#define VT_IsHidden(e)    (((int (*)(void *))VT_FN(e, VT_IS_HIDDEN))(e))

// Extern vtable instances for each entity class.
struct CEntity_vtable;
__extension__ typedef struct CEntity_vtable CEntity_vtable;

extern CEntity_vtable g_vtable_CEntity;
extern CEntity_vtable g_vtable_CResourceEntity;
extern CEntity_vtable g_vtable_StaticEntity;
extern CEntity_vtable g_vtable_CItem;
extern CEntity_vtable g_vtable_CContainer;
extern CEntity_vtable g_vtable_CWeapon;
extern CEntity_vtable g_vtable_CMobile;
extern CEntity_vtable g_vtable_CResourceMobile;
extern CEntity_vtable g_vtable_CNPC;
extern CEntity_vtable g_vtable_CPlayer;
extern CEntity_vtable g_vtable_CShopkeeper;
extern CEntity_vtable g_vtable_CMulti;
extern CEntity_vtable g_vtable_CEgg;
extern CEntity_vtable g_vtable_CSignpost;
extern CEntity_vtable g_vtable_CGuard;
extern CEntity_vtable g_vtable_CBoard;

CEntity_vtable *VT_ForType(int etype);
int VT_GetType(CEntity_vtable *vt);
void VT_Init(void);

#endif
