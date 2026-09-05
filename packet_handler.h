#ifndef PACKET_HANDLER_H_
#define PACKET_HANDLER_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CString CString;
__extension__ typedef struct CVector CVector;
__extension__ typedef struct TagNode TagNode;
__extension__ typedef struct ResQuerySession ResQuerySession;
__extension__ typedef struct CUserSock CUserSock;
__extension__ typedef struct CWorld CWorld;

/*
 * Inbound packet opcodes, matched by DoHandlePacket_Player and its
 * sibling dispatchers.
 */
enum {
	PacketType_LOGIN = 0x00,
	PacketType_REQ_MOVE = 0x02,
	PacketType_LOGOUT = 0x01,
	PacketType_SPEECH = 0x03,
	PacketType_GODMODE_TOGGLE = 0x04,
	PacketType_ATTACK = 0x05,
	PacketType_REQ_OBJUSE = 0x06,
	PacketType_REQ_GETOBJ = 0x07,
	PacketType_REQ_DROPOBJ = 0x08,
	PacketType_REQ_LOOK = 0x09,
	PacketType_PAPERDOLL = 0x0F, // unimplemented in UoDemo.exe
	PacketType_GODCOMMAND = 0x12,
	PacketType_REQ_OBJEQUIP = 0x13,
	PacketType_OK_MOVE = 0x22,
	PacketType_DEATH = 0x2C,
	PacketType_CLIENTQUERY = 0x34,
	PacketType_MOVEOBJECT = 0x37,
	PacketType_GROUPS = 0x39,
	PacketType_OFFERACCEPT = 0x3B,
	PacketType_CHECK_VER = 0x4B,
	PacketType_POSTMSG = 0x52,
	PacketType_MAP_COMMAND = 0x56,
	PacketType_PRELOGIN = 0x5D,
	PacketType_BOOKPAGE = 0x66,
	PacketType_FRIENDS = 0x69,
	PacketType_KEY_USE = 0x6B,
	PacketType_TARGET = 0x6C,
	PacketType_TRADE = 0x6F,
	PacketType_BBOARD = 0x71,
	PacketType_COMBAT = 0x72,
	PacketType_PING = 0x73,
	PacketType_RENAME_MOB = 0x75,
	PacketType_ResourceQuery = 0x79, // unknown name
	PacketType_PICKEDOBJ = 0x7D,
	PacketType_GodViewQuery = 0x7E, // unknown name
	PacketType_ACCT_LOGIN_REQ = 0x80,
	PacketType_ACCT_DEL_CHAR = 0x83,
	PacketType_CHG_CHAR_PW = 0x84,
	PacketType_SendResources = 0x87, // unknown name
	PacketType_TriggerEdit = 0x8A,   // unknown name
	PacketType_POSTLOGIN = 0x91,
	PacketType_BOOKHDR = 0x93,
	PacketType_HUEPICKER = 0x95,
	PacketType_GameCentMon = 0x96, // unknown name
	PacketType_MOBNAME = 0x98,
	PacketType_TEXT_ENTRY = 0x9A,
	PacketType_REQUEST_ASSIST = 0x9B,
	PacketType_SHOP_OFFER = 0x9F,
	PacketType_BRITANNIA_SELECT = 0xA0,
	PacketType_HARDWARE_INFO = 0xA4,
	PacketType_REQ_TIP = 0xA7,
	PacketType_STRING_RESPONSE = 0xAC,
	PacketType_SPEECH_UNICODE = 0xAD,
	PacketType_GumpMenuSelection = 0xB1, // unknown name
	PacketType_CHAT_TEXT = 0xB3,
	PacketType_CHAT_OPEN = 0xB5,
	PacketType_HELP_REQUEST = 0xB6,
	PacketType_CHAR_PROFILE = 0xB8,
	PacketType_CLIENT_VERSION = 0xBD,

	// GM
	PacketType_EDIT = 0x0A,
	PacketType_TILEDATA = 0x0C,
	PacketType_TEMPLATEDATA = 0x0E,
	PacketType_ELEVCHANGE = 0x14,
	PacketType_NPCCONVO_DATA = 0x19,
	PacketType_DESTROY_OBJECT = 0x1D,
	PacketType_AddResource = 0x35, // unknown name
	PacketType_RESOURCETILEDATA = 0x36,
	PacketType_NEW_ART = 0x46,
	PacketType_NEW_TERR = 0x47,
	PacketType_NEW_ANIM = 0x48,
	PacketType_NEW_HUES = 0x49,
	PacketType_DESTROY_ART = 0x4A,
	PacketType_NEW_REGION = 0x58,
	PacketType_DESTROY_STATIC = 0x61,
	PacketType_MOVESTATIC = 0x62,
	PacketType_SIMPED = 0x67,
	PacketType_RequestAssistance = 0x9C, // unknown name
	PacketType_GMSingle = 0x9D,          // unknown name
};

__extension__ typedef struct CTradeSession CTradeSession;

/*
 * Starting city entry used to build the CITIES_AND_CHARS packet.
 */
__extension__ typedef struct PlaceName PlaceName;
struct PlaceName {
	char city[31];
	char place[31];
	int16_t x;
	int16_t y;
	int8_t z;
};

extern const uint16_t g_NotorietyHueTable[]; // 0x0061DB10
extern uint32_t g_gcmVersion; // 0x00621A18
extern int g_EditorStaticCreateFlag; // 0x00645B48
/*
 * One slot of a GM editor tile group (0x124 bytes): a 3x3 neighborhood
 * pattern and up to 64 random tile choices. Accessed by the SIMPED
 * handlers at entry + 0x158 + slot * 0x124.
 */
__extension__ typedef struct CEditorTileSlot CEditorTileSlot;
struct CEditorTileSlot {
	int32_t pattern[3][3];      // 0x00
	int32_t tileChoices[64];    // 0x24 (-1 = invalid)
};

/*
 * GM editor tile group, loaded from client editor packets and stored in
 * g_EditorTileGroups[]. Layout: header[0x58] + tileIds[64] at 0x58 +
 * slots[64] at 0x158.
 */
__extension__ typedef struct CEditorTileGroup CEditorTileGroup;
struct CEditorTileGroup {
	uint8_t header[0x58];           // 0x00
	int32_t tileIds[64];            // 0x58 (masked with 0x7FFF)
	CEditorTileSlot slots[64];      // 0x158
};

extern CEditorTileGroup *g_EditorTileGroups[64]; // 0x00648318
extern int g_SignpostCount; // 0x0068B398
extern CTradeSession *g_TradeSessionList; // 0x006933D4
extern ResQuerySession *g_ResSessionSlots[8]; // 0x006E7308
extern char *g_ResQueryEntryNames[100]; // 0x006E7328
extern int32_t g_ResQueryEntryValues[100]; // 0x006E74B8
extern int g_ResQueryTemplateLimit; // 0x006E7648
extern int g_ResQueryEntryIndex; // 0x006E764C
extern uint32_t g_gcmState; // 0x006E7658
extern CItem *g_gcmTarget; // 0x006E765C
extern uint32_t g_gcmTimestamp; // 0x006E7660
extern int g_ResSessionInit; // 0x006E7664
extern int g_ResQueryTypeCount; // 0x006E7668
extern PlaceName g_PlaceNameList[];
extern int g_PlaceNameCount;
extern int g_VendorSawMatch;

char *NamedResource_Find(char *name); // 0x00447A91
void NamedResource_LoadAll(void); // 0x00447AE9
void CPlayer_MacroHandler_Stub1(CPlayer *self, uint32_t arg1, uint32_t arg2); // 0x004484B4
void CPlayer_MacroHandler_Stub2(CPlayer *self, uint32_t arg1, uint32_t arg2); // 0x004484C1
void CPlayer_MacroHandler_Stub3(CPlayer *self, uint32_t arg1, uint32_t arg2); // 0x004484CE
void LoadAll_LVN_Hostname(CString *str); // 0x004484DB
void ShowIds(CItem *player); // 0x004485E7
CTradeSession *CloseTrade(CTradeSession *session, int deleteFlag); // 0x0044A7A0
CItem *GetCountVictim(CItem *entity); // 0x0044DA10
int GetCountVictimTag(CItem *entity, uint32_t *outSerial); // 0x0044DA72
int SetCountVictim(CItem *entity, uint32_t serial); // 0x0044DAAD
int ClearCountVictim(CItem *entity); // 0x0044DAEA
void ShowEntityLocation(CItem *entity); // 0x0044DB18
void DoJailCommand(CItem *commander); // 0x0044DC6E
void DoUnjailCommand(CItem *commander); // 0x0044DD52
void DoReleaseCommand(CItem *commander, const char *locName); // 0x0044DE70
void CEditorObj_HandleEdit(CPlayer *player, int type, int16_t x, int16_t y, int8_t z, uint16_t id, uint16_t hue); // 0x0045AD13
int FindLocation(const char *name, CLocation *out); // 0x00467500
uint16_t GetPacketOffset(uint8_t *buf); // 0x00478770
uint16_t GetGlobalOffset(void); // 0x004787B0
int PacketIsDynamicSize(uint8_t *buf); // 0x004787C0
void SendToClient(CItem *entity, uint8_t *buf, int range); // 0x0047E0D1
void SendToClientList(CVector *list, uint8_t *buf); // 0x0047E11A
void BroadcastToNearby(uint8_t *buf, CLocation *loc, int maxRecipients); // 0x0047E166
void BroadcastToRangeWithLOS(uint8_t *buf, CLocation *loc, int maxCount, int range); // 0x0047E27D
void HandlePacket_LOGIN(CUserSock *this, uint8_t *buf); // 0x0047E45C
void HandlePacket_POSTLOGIN_UserSock(CUserSock *this); // 0x0047E768
void HandlePacket_PRELOGIN(CUserSock *this, uint8_t *buf); // 0x0047E79E
void HandlePacket_ACCT_LOGIN_REQ_original(CUserSock *this, uint8_t *buf); // 0x0047E8D8
void HandlePacket_ACCT_LOGIN_REQ(CUserSock *this, uint8_t *buf); // 0x0047E8D8
void HandlePacket_ACCT_DEL_CHAR(CUserSock *this, uint8_t *buf, uint16_t packetLen); // 0x0047EA8F
void HandlePacket_CHG_CHAR_PW(CUserSock *this, uint8_t *buf); // 0x0047EADE
void Handle_LookAt(CPlayer *this, uint32_t targetSerial); // 0x0048E779
void Player_Login(CPlayer *this, uint32_t addr); // 0x00492746
void PostLogin(CPlayer *player, uint32_t addr); // 0x0049275B
void Player_LoginSequence(CPlayer *this, uint32_t addr); // 0x00492760
void HandlePacket_GODMODE_TOGGLE(CPlayer *this, uint8_t *buf); // 0x00492CBA
void HandlePacket_POSTLOGIN_Player(CPlayer *this, uint8_t *buf); // 0x00493170
void HandlePacket_POSTMSG(CPlayer *this, uint8_t *buf); // 0x004931CF
void HandlePacket_CHECK_VER(CPlayer *this, uint8_t *buf); // 0x0049320F
void HandlePacket_GumpMenuSelection(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x00493248
void HandlePacket_REQ_MOVE(CPlayer *this, uint8_t *buf); // 0x00493495
void HandlePacket_HARDWARE_INFO(CPlayer *this, uint8_t *buf); // 0x004934DE
void HandlePacket_SPEECH(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x0049398C
void HandlePacket_REQ_OBJUSE(CPlayer *this, uint8_t *buf); // 0x00493E4C
void HandlePacket_REQ_GETOBJ(CPlayer *this, uint8_t *buf); // 0x004942A6
void HandlePacket_REQ_DROPOBJ(CPlayer *this, uint8_t *buf); // 0x00494D6F
void HandlePacket_ATTACK(CPlayer *this, uint8_t *buf); // 0x00495D46
int PacketSecurity_RequireGM(CPlayer *this, uint8_t packetType, const char *handler);
void PacketSecurity_ClosePlayer(CPlayer *this, uint8_t packetType, const char *handler, const char *reason);
void HandlePacket_GODCOMMAND(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x00495DBE
void HandlePacket_REQ_LOOK(CPlayer *this, uint8_t *buf); // 0x00495E18
void HandlePacket_REQ_OBJEQUIP(CPlayer *this, uint8_t *buf); // 0x00495E4D
void HandlePacket_RESOURCETILEDATA(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x0049606F
void SendStatusToPlayer(CMobile *mob, CPlayer *player, uint32_t serial, uint8_t flag); // 0x004963DD
void HandlePacket_CLIENTQUERY(CPlayer *this, uint8_t *buf); // 0x004964CA
void HandlePacket_ELEVCHANGE(CPlayer *this, uint8_t *buf); // 0x00496A03
void HandlePacket_MOVEOBJECT(CPlayer *this, uint8_t *buf); // 0x00496AF7
void HandlePacket_GROUPS(CPlayer *this, uint8_t *buf); // 0x00496BD6
void HandlePacket_OFFERACCEPT(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x00496C0F
void HandlePacket_SHOP_OFFER(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x00496D55
void HandlePacket_DESTROY_OBJECT(CPlayer *this, uint8_t *buf); // 0x00496E7D
void HandlePacket_MAP_COMMAND(CPlayer *this, uint8_t *buf); // 0x0049743B
void HandlePacket_DEATH(CPlayer *this, uint8_t *buf); // 0x004976D3
void HandlePacket_KEY_USE(CPlayer *this, uint8_t *buf); // 0x00497754
void HandlePacket_FRIENDS(CPlayer *this, uint8_t *buf); // 0x00497759
void HandlePacket_TARGET(CPlayer *this, uint8_t *buf); // 0x0049775E
void HandlePacket_TRADE(CPlayer *this, uint8_t *buf); // 0x0049798B
void BBoard_EnableBroadcastMode(uint8_t *buf, uint16_t packetLen); // 0x00497ACD
void HandlePacket_BBOARD(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x00497B05
void HandlePacket_PING(CPlayer *this, uint8_t *buf); // 0x00497F5F
void HandlePacket_COMBAT(CPlayer *this, uint8_t *buf); // 0x00497F90
void HandlePacket_OK_MOVE(CPlayer *this, uint8_t *buf); // 0x00498015
void HandlePacket_RENAME_MOB(CPlayer *this, uint8_t *buf); // 0x00498078
void HandlePacket_PICKEDOBJ(CPlayer *this, uint8_t *buf); // 0x00498185
void HandlePacket_HUEPICKER(CPlayer *this, uint8_t *buf); // 0x00498255
void HandlePacket_MOBNAME(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004982EA
void HandlePacket_BRITANNIA_SELECT(CUserSock *this, uint8_t *buf); // 0x00498335
void HandlePacket_TEXT_ENTRY(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x0049835A
void HandlePacket_REQUEST_ASSIST(CPlayer *this, uint8_t *buf); // 0x00498461
void HandlePacket_RequestAssistance(CPlayer *this, uint8_t *buf); // 0x00498505
void HandlePacket_REQ_TIP(CPlayer *this, uint8_t *buf); // 0x004986A9
void HandlePacket_STRING_RESPONSE(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004986AE
void DoHandlePacket_Player(CPlayer *this, int type, uint8_t *buf, uint16_t packetLen); // 0x0049D19E
uint16_t InitializeGlobalPacketOffset(void); // 0x0049DA80
uint16_t SetPacketType(uint8_t *buf, uint8_t type); // 0x0049DAA0
uint16_t GetSizeLength(uint8_t *buf); // 0x0049DAC0
uint8_t PacketManager_GetPacketByte(uint8_t *ptr); // 0x0049DB00
uint16_t SetPacketOffset(uint8_t *buf, uint16_t off); // 0x0049DB10
uint16_t SetGlobalOffset(uint16_t off); // 0x0049DB50
void BuildTriggerPacket(uint8_t *buf, uint8_t subtype, int32_t datalen, char *data); // 0x004B3390
void HandlePacket_GodViewQuery(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004B4CE4
void SaveResources(void); // 0x004B503E
TagNode *TriggerEdit_FindTagDef(CItem *entity, const char *name); // 0x004B56A1
void HandlePacket_TriggerEdit(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004B5DC7
void Script_createPlaceHolder(uint32_t serial); // 0x004B7616
void HandlePacket_SPEECH_UNICODE(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004DB1C1
void CWorld_SpeechNotifyNearbyUnicode(CWorld *self, uint32_t serial, CLocation *loc, uint16_t *text, int unused, int range); // 0x004DB1C6
void DoorClose(CItem *door, int unused); // 0x004DB6CC
void UseDoor(CItem *door); // 0x004DBC41
void SendOpenGump(CPlayer *player, uint32_t playerSerial, uint32_t containerSerial, uint16_t gumpType); // 0x004DC355
void OpenPaperdoll(CPlayer *player, uint32_t playerSerial, CItem *target); // 0x004DC388
void UseItem_Default(CPlayer *player, CItem *entity); // 0x004DCC42
void UseItemDispatch(CItem *player, CItem *entity); // 0x004DCCB5
void DispatchDoubleClick(CPlayer *this, CItem *entity, int isPaperdoll); // 0x004DCD04
void Vendor_SayTo(CPlayer *player, CMobile *vendor, char *text);
void HandlePacket_CLIENT_VERSION(CUserSock *this, uint8_t *buf, uint16_t packetLen);
void HandlePacket_SKILLOCK(CPlayer *this, uint8_t *buf);
uint16_t ClampStat(int32_t val);
void HandlePacket_TILEDATA(CPlayer *this, uint8_t *buf);
void HandlePacket_DESTROY_STATIC(CPlayer *this, uint8_t *buf);
void HandlePacket_MOVESTATIC(CPlayer *this, uint8_t *buf);
void HandlePacket_EDIT(CPlayer *this, uint8_t *buf);
void HandlePacket_TEMPLATEDATA(CPlayer *this, uint8_t *buf);
void HandlePacket_AddResource(CPlayer *this, uint8_t *buf);
void HandlePacket_NPCCONVO_DATA(CPlayer *this, uint8_t *buf);
void HandlePacket_NEW_HUES(CPlayer *this, uint8_t *buf);
void HandlePacket_NEW_REGION(CPlayer *this, uint8_t *buf);
void HandlePacket_NEW_ANIM(CPlayer *this, uint8_t *buf);
void HandlePacket_DESTROY_ART(CPlayer *this, uint8_t *buf);
void HandlePacket_NEW_ART(CPlayer *this, uint8_t *buf);
void HandlePacket_NEW_TERR(CPlayer *this, uint8_t *buf);
void HandlePacket_SIMPED(CPlayer *this, uint8_t *buf);
void HandlePacket_GMSingle(CPlayer *this, uint8_t *buf);
void HandlePacket_POSTLOGIN(CUserSock *this, uint8_t *buf);

#endif /* PACKET_HANDLER_H_ */
