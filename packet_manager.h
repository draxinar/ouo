#ifndef PACKET_MANAGER_H_
#define PACKET_MANAGER_H_

#include <stdint.h>

__extension__ typedef struct CContainer CContainer;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CList CList;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CRegion CRegion;
__extension__ typedef struct CResourceNode CResourceNode;
struct CItem;
struct CResourceNode;
struct CList;
struct CResList;

/*
 * One row of an OBJPICKER packet (8 bytes on 32-bit).
 */
typedef struct {
	uint16_t typeId;
	uint16_t hue;
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64; // 64-bit alignment pad
#endif
	char *name;
} ObjPickerEntry;

/*
 * One vendor sell-list row emitted inside PacketType_SHOP_SELL
 * (16 bytes on 32-bit).
 */
typedef struct {
	uint32_t serial;   // 0x00
	uint16_t bodyType; // 0x04
	uint16_t color;    // 0x06
	uint16_t amount;   // 0x08
	uint16_t price;    // 0x0A
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;   // 64-bit alignment pad
#endif
	char *name;        // 0x0C
} CVendorSellEntry;

/*
 * Outbound packet opcodes assembled by PacketManager_MakePacket_* helpers.
 * Entries commented client+server also appear in packet_handler.h.
 */
enum {
	PacketType_MOBILESTAT = 0x11,
	PacketType_FOLLOW = 0x15,
	// PacketType_MOVE = 0x1A, // client+server
	PacketType_LOGIN_CONFIRM = 0x1B,
	PacketType_TEXT = 0x1C,
	PacketType_ZMOVE = 0x20,
	PacketType_BLOCKED_MOVE = 0x21,
	// PacketType_OK_MOVE = 0x22, // client+server
	// PacketType_OBJMOVE = 0x23, // client+server
	PacketType_OPEN_GUMP = 0x24,
	PacketType_OBJ_TO_OBJ = 0x25,
	PacketType_GETOBJ_FAILED = 0x27,
	PacketType_DROPOBJ_FAILED = 0x28,
	PacketType_DROPOBJ_OK = 0x29,
	PacketType_GODMODE = 0x2B,
	// PacketType_DEATH = 0x2C, // client+server
	PacketType_HEALTH = 0x2D,
	PacketType_EQUIP_ITEM = 0x2E,
	PacketType_SWING = 0x2F,
	PacketType_ATTACK_OK = 0x30,
	PacketType_ATTACK_END = 0x31,
	PacketType_FOLLOWMOVE = 0x38,
	// PacketType_GROUPS = 0x39, // client+server
	PacketType_SKILLS = 0x3A,
	// PacketType_OFFERACCEPT = 0x3B, // client+server
	PacketType_MULTI_OBJ_TO_OBJ = 0x3C,
	PacketType_VERSIONS = 0x3E,
	PacketType_UPD_OBJCHUNK = 0x3F,
	PacketType_UPD_TERRCHUNK = 0x40,
	PacketType_UPD_TILEDATA = 0x41,
	PacketType_UPD_ART = 0x42,
	PacketType_UPD_ANIM = 0x43,
	PacketType_UPD_HUES = 0x44,
	PacketType_VER_OK = 0x45,
	PacketType_LIGHTCHANGE = 0x4E,
	PacketType_SUNLIGHT = 0x4F,
	PacketType_LOGIN_REJECT = 0x53,
	PacketType_SOUND = 0x54,
	PacketType_LOGIN_COMPLETE = 0x55,
	// PacketType_MAP_COMMAND = 0x56, // client+server
	PacketType_UPD_REGIONS = 0x57,
	PacketType_GAMETIME = 0x5B,
	PacketType_RESTARTVER = 0x5C,
	PacketType_WEATHERCHANGE = 0x65,
	// PacketType_BOOKPAGE = 0x66, // client+server
	// PacketType_FRIENDS = 0x69, // client+server
	PacketType_FRIENDNOTIFY = 0x6A,
	// PacketType_TARGET = 0x6C, // client+server
	PacketType_MUSIC = 0x6D,
	PacketType_ANIM = 0x6E,
	// PacketType_TRADE = 0x6F, // client+server
	PacketType_EFFECT = 0x70,
	// PacketType_BBOARD = 0x71, // client+server
	// PacketType_COMBAT = 0x72, // client+server
	// PacketType_PING = 0x73, // client+server
	PacketType_SHOP_DATA = 0x74,
	PacketType_SERVERCHANGE = 0x76,
	PacketType_NAKED_MOB = 0x77,
	PacketType_EQUIPPED_MOB = 0x78,
	PacketType_SEQUENCE = 0x7B,
	PacketType_OBJPICKER = 0x7C,
	PacketType_ACCT_LOGIN_OK = 0x81,
	PacketType_ACCT_LOGIN_FAIL = 0x82,
	PacketType_CHG_CHAR_RESULT = 0x85,
	PacketType_ALL_CHARACTERS = 0x86,
	PacketType_OPEN_PAPERDOLL = 0x88,
	PacketType_CORPSE_EQ = 0x89,
	PacketType_DISPLAY_SIGN = 0x8B,
	PacketType_USER_SERVER = 0x8C,
	PacketType_OPEN_COURSEGUMP = 0x90,
	PacketType_UPD_MULTI = 0x92,
	// PacketType_BOOKHDR = 0x93, // client+server
	PacketType_UPD_SKILL = 0x94,
	// PacketType_HUEPICKER = 0x95, // client+server
	PacketType_PLAYERMOVE = 0x97,
	// PacketType_MOBNAME = 0x98, // client+server
	PacketType_TARGET_MULTI = 0x99,
	// PacketType_TEXT_ENTRY = 0x9A, // client+server
	PacketType_SHOP_SELL = 0x9E,
	PacketType_HP_HEALTH = 0xA1,
	PacketType_MANA_HEALTH = 0xA2,
	PacketType_FAT_HEALTH = 0xA3,
	PacketType_WEB_BROWSE = 0xA5,
	PacketType_MESSAGE = 0xA6,
	PacketType_BRITANNIA_LIST = 0xA8,
	PacketType_CITIES_AND_CHARS = 0xA9,
	PacketType_CURRENT_TARGET = 0xAA,
	PacketType_STRING_QUERY = 0xAB,
	PacketType_TEXT_UNICODE = 0xAE,
	PacketType_DEATH_ANIM = 0xAF,
	PacketType_GENERIC_GUMP = 0xB0,
	PacketType_CHAT_MSG = 0xB2,
	// PacketType_CHAT_TEXT = 0xB3, // client+server
	PacketType_TARGET_OBJLIST = 0xB4,
	// PacketType_CHAT_OPEN = 0xB5, // client+server
	PacketType_HELP_UNICODE_TEXT = 0xB7,
	// PacketType_CHAR_PROFILE = 0xB8, // client+server
	PacketType_FEATURES = 0xB9,
};

extern uint32_t g_HighlightColorTable[];

/*
 * One shard entry in the BRITANNIA_LIST login packet.
 */
__extension__ typedef struct Britannia Britannia;
struct Britannia {
	char name[32];
	uint8_t percentFull;
	uint8_t timeZone;
	uint8_t serverIP[4];
};

extern Britannia g_BritanniaList[];

void PacketManager_MakePacket_GMSingle(
        uint8_t *buf, uint8_t type, uint32_t serial, uint32_t model, uint16_t x, uint16_t y, uint16_t z, uint32_t hue, uint8_t amount, char *name); // 0x00498771
void PacketManager_MakePacket_STRING_QUERY(uint8_t *buf, uint32_t serial, uint16_t type, char *question, uint8_t cancel, uint8_t style, uint32_t maxLen, char *title); // 0x0049899F
uint16_t PacketManager_MakePacket_DEATH_ANIM(uint8_t *buf, uint32_t mobSerial, uint32_t corpseSerial, uint32_t flag); // 0x00498A83
void PacketManager_MakePacket_SKILLS_NAMES(uint8_t *buf, uint16_t maxSkills); // 0x00498ACB
void PacketManager_MakePacket_SKILLS(uint8_t *buf, int maxSkills, CItem *player); // 0x00498BCB
uint16_t PacketManager_MakePacket_SKILLS_SINGLE(uint8_t *buf, uint16_t skillID, uint16_t value); // 0x00498CE3
void PacketManager_MakePacket_RESTARTVER(uint8_t *buf); // 0x00498D31
uint16_t PacketManager_MakePacket_CORPSE_EQ(uint8_t *buf, uint32_t serial, uint32_t *equipSlots); // 0x00498D54
void PacketManager_MakePacket_REVISION(uint8_t *buf, uint32_t serial, char *name, int maxlen); // 0x00498DDF
uint16_t PacketManager_MakePacket_SUNLIGHT(uint8_t *buf, uint8_t lightLevel); // 0x00498ED9
uint16_t PacketManager_MakePacket_LIGHTCHANGE(uint8_t *buf, uint32_t serial, uint8_t lightLevel); // 0x00498F6D
void PacketManager_MakePacket_VER_OK(uint8_t *buf, uint32_t value); // 0x00498FAB
uint16_t PacketManager_MakePacket_WARMODE(uint8_t *buf, uint8_t flag); // 0x00498FD0
uint16_t PacketManager_MakePacket_FOLLOWMOVE(uint8_t *buf, uint16_t x, uint16_t y, uint16_t z); // 0x00498FF5
void PacketManager_MakePacket_GROUPS(uint8_t *buf, uint32_t serial1, uint32_t serial2); // 0x00499072
uint16_t PacketManager_MakePacket_DESTROY_OBJECT(uint8_t *buf, uint32_t serial); // 0x0049918A
uint16_t PacketManager_MakePacket_NAKED_MOB(uint8_t *buf, CMobile *mob, uint8_t notoriety); // 0x004991AF
uint16_t PacketManager_MakePacket_EQUIPPED_MOB(uint8_t *buf, CMobile *mob, uint8_t notoriety); // 0x00499281
void PacketManager_MakePacket_MOVE(uint8_t *buf, CItem *item); // 0x00499438
uint16_t PacketManager_MakePacket_ZMOVE(uint8_t *buf, CMobile *mob); // 0x00499679
uint16_t PacketManager_MakePacket_LOGIN_CONFIRM(uint8_t *buf, CPlayer *player); // 0x0049978E
uint16_t PacketManager_MakePacket_BLOCKED_MOVE(uint8_t *buf, uint8_t sequence, uint16_t x, uint16_t y, uint8_t direction, uint8_t z); // 0x004998D3
uint16_t PacketManager_MakePacket_OK_MOVE(uint8_t *buf, uint8_t sequence, uint8_t notoriety); // 0x0049993A
uint16_t PacketManager_MakePacket_OBJMOVE(uint8_t *buf, uint16_t itemID, uint8_t stackable, uint16_t srcX, uint16_t srcY, uint32_t srcSerial, uint16_t srcNewX, uint16_t srcNewY,
        uint8_t srcZ, uint32_t dstSerial, uint16_t dstX, uint16_t dstY, uint8_t dstZ); // 0x0049996F
uint16_t PacketManager_MakePacket_OPEN_GUMP(uint8_t *buf, uint32_t gumpSerial, uint16_t gumpType); // 0x00499A4B
void PacketManager_MakePacket_MAP_DISPLAY(
        uint8_t *buf, uint32_t serial, uint16_t gumpId, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t width, uint16_t height); // 0x00499A81
uint16_t PacketManager_MakePacket_TEXT(uint8_t *buf, CItem *entity, CItem *target, uint8_t speechType, const char *text, uint16_t hue, uint16_t font); // 0x00499B20
void PacketManager_MakePacket_ASCII_SPEECH(uint8_t *buf, uint32_t serial, uint16_t model, char *name, uint8_t speechType, char *text, uint16_t color, uint16_t font); // 0x00499CE2
uint16_t PacketManager_MakePacket_OBJ_TO_OBJ(uint8_t *buf, CItem *item, CItem *container); // 0x00499DCC
uint16_t PacketManager_MakePacket_MULTI_OBJ_TO_OBJ(uint8_t *buf, CContainer *cont, int filterMode, int sellMode); // 0x00499EEB
uint16_t PacketManager_MakePacket_GETOBJ_FAILED(uint8_t *buf, uint8_t reason); // 0x0049A1AC
uint16_t PacketManager_MakePacket_DROPOBJ_FAILED(uint8_t *buf, uint16_t x, uint16_t y); // 0x0049A1D1
uint16_t PacketManager_MakePacket_DROPOBJ_OK(uint8_t *buf); // 0x0049A208
uint16_t PacketManager_MakePacket_DEATHACTION(uint8_t *buf, uint32_t serial); // 0x0049A21D
uint16_t PacketManager_MakePacket_GODMODE(uint8_t *buf, uint8_t editing); // 0x0049A242
uint16_t PacketManager_MakePacket_DEATH(uint8_t *buf, uint8_t flag); // 0x0049A267
uint16_t PacketManager_MakePacket_HEALTH(
        uint8_t *buf, uint32_t serial, uint16_t maxHp, uint16_t hp, uint16_t maxMana, uint16_t mana, uint16_t maxStamina, uint16_t stamina); // 0x0049A28C
uint16_t PacketManager_MakePacket_HP_HEALTH(uint8_t *buf, uint32_t serial, uint16_t maxHp, uint16_t hp); // 0x0049A317
uint16_t PacketManager_MakePacket_MANA_HEALTH(uint8_t *buf, uint32_t serial, uint16_t maxMana, uint16_t mana); // 0x0049A361
uint16_t PacketManager_MakePacket_FAT_HEALTH(uint8_t *buf, uint32_t serial, uint16_t maxStamina, uint16_t stamina); // 0x0049A3AB
uint16_t PacketManager_MakePacket_EQUIP_ITEM(uint8_t *buf, CItem *item, CMobile *wearer, uint8_t layer); // 0x0049A3F5
uint16_t PacketManager_MakePacket_SWING(uint8_t *buf, uint8_t flag, uint32_t attackerSerial, uint32_t defenderSerial); // 0x0049A47E
uint16_t PacketManager_MakePacket_ATTACK_OK(uint8_t *buf, uint32_t targetSerial); // 0x0049A4C3
uint16_t PacketManager_MakePacket_ATTACK_END(uint8_t *buf); // 0x0049A4E8
uint16_t PacketManager_MakePacket_MOBILESTAT(uint8_t *buf, CMobile *mob, uint32_t viewerSerial, uint8_t extended); // 0x0049A4FD
void MakePacket_RESTYPE_DATA(uint8_t *buf, int typeId); // 0x0049A87D
void PacketManager_MakePacket_RESOURCETILEDATA(uint8_t *buf, uint32_t serial, uint32_t isLast, uint16_t index, CResourceNode *node); // 0x0049A9B7
void PacketManager_MakePacket_RESOURCETILEDATA_Region(uint8_t *buf, uint32_t regionIndex, uint32_t mode, uint32_t count, CResourceNode *node); // 0x0049AACB
void PacketManager_MakePacket_STATIC_DATA(uint8_t *buf, int blockIdx); // 0x0049AC7F
void PacketManager_MakePacket_UPD_TERRCHUNK(uint8_t *buf, int blockIdx); // 0x0049ADC0
void PacketManager_MakePacket_TILEDATA(uint8_t *buf, int groupIndex); // 0x0049AE91
void PacketManager_MakePacket_NEW_ANIMDATA(uint8_t *buf, int groupBase, int mode, uint8_t *groupData); // 0x0049B114
void PacketManager_MakePacket_HUEDATA(uint8_t *buf, int groupIndex); // 0x0049B161
void PacketManager_MakePacket_UPD_REGIONS(uint8_t *buf, CRegion *region); // 0x0049B25E
uint16_t PacketManager_MakePacket_OFFERACCEPT(uint8_t *buf, uint32_t vendorSerial, uint8_t flag); // 0x0049B398
uint16_t PacketManager_MakePacket_LOGIN_REJECT(uint8_t *buf, uint8_t reason); // 0x0049B3D0
uint16_t PacketManager_MakePacket_LOGIN_COMPLETE(uint8_t *buf); // 0x0049B3F5
uint16_t PacketManager_MakePacket_ACCT_LOGIN_OK(uint8_t *buf, uint8_t numCharacters, uint8_t v, char *characterNames, char *characterPasswords); // 0x0049B40A
uint16_t PacketManager_MakePacket_CHG_CHAR_RESULT(uint8_t *buf, uint8_t reason); // 0x0049B5A0
uint16_t PacketManager_MakePacket_ALL_CHARACTERS(uint8_t *buf, uint8_t numCharacters, char *characterNames); // 0x0049B5C8
void PacketManager_MakePacket_MAP_COMMAND(uint8_t *buf, uint32_t serial, uint8_t command, uint8_t arg, uint16_t plotX, uint16_t plotY); // 0x0049B632
uint16_t PacketManager_MakePacket_SOUND(uint8_t *buf, uint8_t flags, uint16_t soundID, CLocation *location, uint16_t volume); // 0x0049B699
uint16_t PacketManager_MakePacket_GAMETIME(uint8_t *buf); // 0x0049B71B
uint16_t PacketManager_MakePacket_WEATHERCHANGE(uint8_t *buf, uint8_t weatherType, uint8_t numEffects, uint8_t temperature); // 0x0049B835
void PacketManager_MakePacket_FRIENDNOTIFY(uint8_t *buf, int friendIndex, int status); // 0x0049BB16
uint16_t PacketManager_MakePacket_TARGET(uint8_t *buf, uint8_t targetType, uint32_t cursorID, uint8_t cursorType); // 0x0049BB4B
uint16_t PacketManager_MakePacket_TARGET_MULTI(uint8_t *buf, uint8_t allowGround, uint32_t deedSerial, uint16_t x, uint16_t y, uint16_t z, uint16_t facing); // 0x0049BBD6
uint16_t PacketManager_MakePacket_TARGET_OBJLIST(uint8_t *buf, uint8_t allowGround, uint32_t cursorId, uint16_t multiId, uint16_t xOff, uint16_t yOff, CList *list); // 0x0049BC98
uint16_t PacketManager_MakePacket_MUSIC(uint8_t *buf, uint16_t musicId); // 0x0049BD52
uint16_t PacketManager_MakePacket_ANIM(
        uint8_t *buf, uint32_t serial, uint16_t action, uint16_t frameCount, uint16_t repeatCount, uint8_t backward, uint8_t repeat, uint8_t delay); // 0x0049BD78
void PacketManager_MakePacket_TRADE(uint8_t *buf, uint8_t subtype, uint32_t serial, uint32_t param1, uint32_t param2, const char *name); // 0x0049BE00
uint16_t PacketManager_MakePacket_EFFECT(uint8_t *buf, uint8_t type, uint32_t srcSerial, uint32_t dstSerial, uint16_t itemID, uint16_t srcX, uint16_t srcY, uint8_t srcZ,
        uint16_t dstX, uint16_t dstY, uint8_t dstZ, uint8_t speed, uint8_t duration, uint8_t unk1, uint8_t fixedDir, uint8_t explode, uint8_t unk2); // 0x0049BEBF
uint16_t PacketManager_MakePacket_COMBAT(uint8_t *buf, uint8_t warMode, uint8_t combatByte2, uint8_t combatByte3, uint8_t combatByte4); // 0x0049BFFE
void PacketManager_MakePacket_SHOP_DATA(uint8_t *buf, CMobile *vendor, CContainer *container, int markupPercent, int filterFlag); // 0x0049C053
void PacketManager_MakePacket_SHOP_SELL(uint8_t *buf, CMobile *vendor, uint16_t count, CVendorSellEntry *sellEntries); // 0x0049C1FD
uint16_t PacketManager_MakePacket_SEQUENCE(uint8_t *buf, uint8_t sequence); // 0x0049C341
void PacketManager_MakePacket_OBJPICKER(uint8_t *buf, uint32_t serial, uint16_t gumpId, char *title, uint8_t count, ObjPickerEntry *entries); // 0x0049C3B2
uint16_t PacketManager_MakePacket_HUEPICKER(uint8_t *buf, uint32_t serial, uint16_t typeID, uint16_t hue); // 0x0049C540
uint16_t PacketManager_MakePacket_OPEN_PAPERDOLL(uint8_t *buf, uint32_t serial, char *title, uint8_t flags); // 0x0049C58A
uint16_t PacketManager_MakePacket_DISPLAY_SIGN(uint8_t *buf, uint32_t serial, uint16_t gumpID, char *title, char *body); // 0x0049C5D4
uint16_t PacketManager_MakePacket_PLAYERMOVE(uint8_t *buf, uint8_t direction); // 0x0049C688
uint16_t PacketManager_MakePacket_MOBNAME(uint8_t *buf, CPlayer *viewer, uint32_t serial); // 0x0049C6B0
void PacketManager_MakePacket_TEXT_ENTRY(uint8_t *buf, uint32_t serial, uint32_t gumpId, uint32_t parentId, char *text); // 0x0049C749
uint16_t PacketManager_MakePacket_WEB_BROWSE(uint8_t *buf, char *url); // 0x0049C7B4
uint16_t PacketManager_MakePacket_CURRENT_TARGET(uint8_t *buf, uint32_t serial); // 0x0049CC7D
void PacketManager_MakePacket_RequestAssistance(uint8_t *buf, int type, int subtype, uint32_t serial, const char *name, const char *title, const char *description); // 0x0049CCA5
void PacketManager_MakePacket_GUMP_GENERIC(uint8_t *buf, uint32_t serial, int gumpId, int x, int y, CList *cmdList, CList *textList); // 0x0049CD7C
void PlaySoundAtEntity(CItem *entity, uint16_t soundID, uint16_t volume); // 0x0049D026
void PlaySoundAtLocation(CLocation *loc, uint16_t soundID, uint16_t volume); // 0x0049D07F
void SendSoundToEntity(CItem *entity, int soundId, int volume); // 0x0049D0B7
void SendSoundAtEntity(CItem *entity, int soundId, int volume); // 0x0049D0F2
void PlayMusicToNearby(CItem *entity, uint16_t musicId); // 0x0049D130
void PlayMusicToEntity(CItem *entity, uint16_t musicId); // 0x0049D173
void PacketManager_MakePacket_TEXT_UNICODE(
        uint8_t *buf, CItem *entity, CItem *entity2, uint8_t speechType, uint16_t *text, uint16_t hue, uint16_t font, uint32_t lang); // 0x004DAC38
void PacketManager_MakePacket_SKILLS_Ext(uint8_t *buf, int maxSkills, CItem *player);
void PacketManager_MakePacket_SKILLS_SINGLE_Ext(uint8_t *buf, uint16_t skillID, uint16_t value, uint16_t base, uint8_t lock);
uint16_t PacketManager_MakePacket_ACCT_LOGIN_FAIL(uint8_t *buf, uint8_t reason);
uint16_t PacketManager_MakePacket_BRITANNIA_LIST(uint8_t *buf);
uint16_t PacketManager_MakePacket_USER_SERVER(uint8_t *buf, uint16_t numBritannia, uint16_t port, uint32_t authSeed);
uint16_t PacketManager_MakePacket_FEATURES(uint8_t *buf);
uint16_t PacketManager_MakePacket_CITIES_AND_CHARS(uint8_t *buf, uint8_t numCharacters, char *characterNames, char *characterPasswords);
void PacketManager_MakePacket_CHAT_MSG(uint8_t *buf, uint16_t number, const char *param1, const char *param2);

#endif /* PACKET_MANAGER_H_ */
