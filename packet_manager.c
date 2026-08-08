/*
 * Outbound packet builders.
 *
 * The PacketManager::MakePacket_* family - one per packet type - formats
 * game state into raw bytes ready to be encrypted and pushed to the
 * client's send buffer.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dat.h"

#include "combat.h"
#include "container.h"
#include "egg.h"
#include "feature.h"
#include "main.h"
#include "multi.h"
#include "npc.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "packet_utils.h"
#include "player.h"
#include "region.h"
#include "shopkeeper.h"
#include "skill.h"
#include "time.h"
#include "usersock.h"
#include "utils.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static int ObjPickerEntryTextLen(ObjPickerEntry *entry); // 0x0049C366

// 0x00698318 - facet id written by DRAW_PLAYER (always 0 in demo)
static uint16_t g_MapFacetId;

uint32_t g_HighlightColorTable[] = {
	0x0000000,
	0x00000FF,
	0x000FF00,
	0x0FF0000,
	0x0FFFF00,
	0x0FF00FF,
	0x000FFFF,
	0x0FFFFFF,
	0x000007F,
	0x0007F00,
	0x007F0000,
	0x007F7F00,
	0x007F007F,
	0x0007F7F,
	0x007F7F7F,
	0x0FF7F00,
};

/*
 * 0x00498771 - PacketManager::MakePacket_GMSingle
 *
 * Builds a GMSingle (0x9D) packet carrying type, serial, model,
 * position, hue, amount, and 30-byte name for GM single-click info.
 */
void
PacketManager_MakePacket_GMSingle(uint8_t *buf, uint8_t type, uint32_t serial, uint32_t model, uint16_t x, uint16_t y, uint16_t z, uint32_t hue, uint8_t amount, char *name)
{
	char nameBuf[30];

	PutPacketType(buf, PacketType_GMSingle, 0x33);
	PutByte(buf, type);
	PutDWord(buf, serial);
	PutDWord(buf, model);
	PutWord(buf, x);
	PutWord(buf, y);
	PutWord(buf, z);
	PutDWord(buf, hue);
	PutByte(buf, amount);
	memset(nameBuf, 0, 30);
	strcpy(nameBuf, name);
	PutString(buf, nameBuf, 30);
}

/*
 * 0x0049899F - PacketManager::MakePacket_STRING_QUERY
 *
 * Builds a STRING_QUERY (0xAB) packet (max size 0x213).
 */
void
PacketManager_MakePacket_STRING_QUERY(uint8_t *buf, uint32_t serial, uint16_t type, char *question, uint8_t cancel, uint8_t style, uint32_t maxLen, char *title)
{
	PutPacketType(buf, 0xAB, 0x213);
	PutDWord(buf, serial);
	PutWord(buf, type);
	PutWord(buf, strlen(question) + 1);
	PutString(buf, question, strlen(question) + 1);
	PutByte(buf, cancel);
	PutByte(buf, style);
	PutDWord(buf, maxLen);
	PutWord(buf, strlen(title) + 1);
	PutString(buf, title, strlen(title) + 1);
}

/*
 * 0x00498A83
 * Packet 0xAF (DEATH_ANIM) - 13 bytes
 * Sent when a mobile dies: triggers corpse animation on clients.
 * mobSerial: the dying mobile, corpseSerial: the corpse container, flag: container indicator.
 */
uint16_t
PacketManager_MakePacket_DEATH_ANIM(uint8_t *buf, uint32_t mobSerial, uint32_t corpseSerial, uint32_t flag)
{
	PutPacketType(buf, PacketType_DEATH_ANIM, 13);
	PutDWord(buf, mobSerial);
	PutDWord(buf, corpseSerial);
	return PutDWord(buf, flag);
}

/*
 * 0x00498ACB - PacketManager::MakePacket_SKILLS_NAMES
 *
 * Packet 0x3A (SKILLS), variable size (max 0x8000). Type byte 0xFE =
 * skill names list. Iterates 0 to maxSkills-1, for each existing skill
 * writes: flags byte (bit 0 = CanUseDirect), name length byte
 * (strlen+1), and skill name string.
 */
void
PacketManager_MakePacket_SKILLS_NAMES(uint8_t *buf, uint16_t maxSkills)
{
	int i;
	uint8_t flags;
	uint8_t nameLen;

	PutPacketType(buf, PacketType_SKILLS, 0x8000);
	PutByte(buf, 0xFE);
	PutWord(buf, maxSkills);
	for (i = 0; i < (maxSkills & 0xFFFF); i++) {
		if (!CSkillManager_HasSkill(&g_SkillManager, i))
			continue;
		flags = 0;
		if (CSkillManager_CanUseDirect(&g_SkillManager, (uint8_t)i) == 1)
			flags |= 1;
		PutByte(buf, flags);
		nameLen = (uint8_t)(strlen((char *)CSkillManager_GetSkillName(&g_SkillManager, (uint8_t)i)) + 1);
		PutByte(buf, nameLen);
		PutString(buf, (char *)CSkillManager_GetSkillName(&g_SkillManager, (uint8_t)i), nameLen & 0xFF);
	}
}

/*
 * 0x00498BCB - PacketManager::MakePacket_SKILLS
 *
 * Builds packet 0x3A (SKILLS) full list. Type byte 0x00 = full list.
 * Iterates i = 0 to maxSkills-1, PutWord(i+1) then PutWord of
 * CMobile_GetSkillValue(player, i, 0). Terminated with PutWord(0).
 */
void
PacketManager_MakePacket_SKILLS(uint8_t *buf, int maxSkills, CItem *player)
{
	int i;

	PutPacketType(buf, PacketType_SKILLS, 0x0404);
	PutByte(buf, 0x00);

	for (i = 0; i < (maxSkills & 0xFFFF); i++) {
		PutWord(buf, (uint16_t)(i + 1));
		PutWord(buf, (uint16_t)CMobile_GetSkillValue((CMobile *)player, (int8_t)i, 0));
	}
	PutWord(buf, 0x0000);
}

/*
 * 0x00498CE3
 * Packet 0x3A (SKILLS) single update - 10 bytes
 * Type byte 0xFF = single skill update
 */
uint16_t
PacketManager_MakePacket_SKILLS_SINGLE(uint8_t *buf, uint16_t skillID, uint16_t value)
{
	PutPacketType(buf, PacketType_SKILLS, 0x0A);
	PutByte(buf, 0xFF);
	PutWord(buf, skillID);
	return PutWord(buf, value);
}

/*
 * 0x00498C5A - PacketManager::MakePacket_SKILLS_TYPED
 *
 * Builds packet 0x3A (SKILLS) exactly as 0x00498BCB does, except the
 * list-type byte is a parameter instead of the fixed 0x00.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_SKILLS_TYPED(uint8_t *buf, int maxSkills, CItem *player, uint8_t listType)
{
	int i;

	PutPacketType(buf, PacketType_SKILLS, 0x0404);
	PutByte(buf, listType);

	for (i = 0; i < (maxSkills & 0xFFFF); i++) {
		PutWord(buf, (uint16_t)(i + 1));
		PutWord(buf, (uint16_t)CMobile_GetSkillValue((CMobile *)player, (int8_t)i, 0));
	}
	PutWord(buf, 0x0000);
}

/*
 * 0x00498D31 - PacketManager::MakePacket_RESTARTVER
 *
 * Builds a RESTARTVER (0x5C) packet (2 bytes, one trailing zero).
 */
void
PacketManager_MakePacket_RESTARTVER(uint8_t *buf)
{
	PutPacketType(buf, PacketType_RESTARTVER, 2);
	PutByte(buf, 0);
}

/*
 * 0x00498D54 - PacketManager::MakePacket_CORPSE_EQ
 *
 * Builds a CORPSE_EQ (0x89) packet listing each non-empty equipment
 * layer (1-based) with its serial, terminated by layer byte 0.
 */
uint16_t
PacketManager_MakePacket_CORPSE_EQ(uint8_t *buf, uint32_t serial, uint32_t *equipSlots)
{
	int i;

	PutPacketType(buf, PacketType_CORPSE_EQ, 0xCF);
	PutDWord(buf, serial);
	for (i = 0; i < 0x1A; i++) {
		if (equipSlots[i] == 0)
			continue;
		PutByte(buf, i + 1);
		PutDWord(buf, equipSlots[i]);
	}
	return PutByte(buf, 0);
}

/*
 * 0x00498DDF - PacketManager::MakePacket_REVISION
 *
 * Builds a REVISION (0x92) packet carrying the serial and name string.
 */
void
PacketManager_MakePacket_REVISION(uint8_t *buf, uint32_t serial, char *name, int maxlen)
{
	PutPacketType(buf, 0x92, 0x1000B);
	PutDWord(buf, serial);
	PutDWord(buf, 0);
	if (maxlen >= 0xFFFF)
		PutString(buf, name, 0);
	else
		PutString(buf, name, maxlen);
}

/*
 * 0x00498ED9
 */
uint16_t
PacketManager_MakePacket_SUNLIGHT(uint8_t *buf, uint8_t lightLevel)
{
	PutPacketType(buf, PacketType_SUNLIGHT, 2);
	return PutByte(buf, lightLevel);
}

/*
 * 0x00498F6D
 */
uint16_t
PacketManager_MakePacket_LIGHTCHANGE(uint8_t *buf, uint32_t serial, uint8_t lightLevel)
{
	PutPacketType(buf, PacketType_LIGHTCHANGE, 6);
	PutDWord(buf, serial);
	return PutByte(buf, lightLevel);
}

/*
 * 0x00498E49 - PacketManager::MakePacket_UPD_SKILL_NAME
 *
 * Builds packet 0x94 (UPD_SKILL), variable length. Writes the skill
 * index, a zero dword, a flag byte carrying bit 0 when the skill is
 * actively usable, then the skill's name.
 *
 * The binary reaches the name through 0x004E30A0, which MSVC folded
 * with `_Container_base::_Container_base` because both just return
 * `this`; CSkillDef::name lives at offset 0, so the def pointer is the
 * name pointer.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_UPD_SKILL_NAME(uint8_t *buf, uint8_t skillIndex, CSkillDef *def)
{
	uint8_t flags;
	char *name;

	PutPacketType(buf, PacketType_UPD_SKILL, 0x10009);
	PutByte(buf, skillIndex);
	PutDWord(buf, 0);

	flags = 0;
	if (CSkillEntry_GetCanUse(def) == 1)
		flags |= 1;
	PutByte(buf, flags);

	name = (char *)def;
	PutString(buf, name, strlen(name) + 1);
}

/*
 * 0x00498EFE - PacketManager::MakePacket_UNK_50
 *
 * Builds packet 0x50, size 0x53: a serial followed by a fixed 0x4C-byte
 * string field. Nothing in the binary sends it and no handler names the
 * opcode, so the enum entry is numeric.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_UNK_50(uint8_t *buf, char *text, uint32_t serial)
{
	PutPacketType(buf, PacketType_UNK_50, 0x53);
	PutDWord(buf, serial);
	PutString(buf, text, 0x4C);
}

/*
 * 0x00498F35 - PacketManager::MakePacket_UNK_51
 *
 * Builds packet 0x51, variable length: a NUL-terminated string.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_UNK_51(uint8_t *buf, char *text)
{
	PutPacketType(buf, PacketType_UNK_51, 0x2879);
	PutString(buf, text, strlen(text) + 1);
}

/*
 * 0x00498FAB - PacketManager::MakePacket_VER_OK
 *
 * Packet 0x45 (VER_OK), size 5. Writes packet type and a dword.
 */
void
PacketManager_MakePacket_VER_OK(uint8_t *buf, uint32_t value)
{
	PutPacketType(buf, PacketType_VER_OK, 5);
	PutDWord(buf, value);
}

/*
 * 0x00498FD0 - PacketManager::MakePacket_WARMODE
 *
 * Packet 0x32 (WARMODE) - 2 bytes. Sends war mode / hidden state flag.
 */
uint16_t
PacketManager_MakePacket_WARMODE(uint8_t *buf, uint8_t flag)
{
	PutPacketType(buf, 0x32, 2);
	return PutByte(buf, flag);
}

/*
 * 0x00498FF5
 * Packet 0x38 (FOLLOWMOVE) - 7 bytes
 */
uint16_t
PacketManager_MakePacket_FOLLOWMOVE(uint8_t *buf, uint16_t x, uint16_t y, uint16_t z)
{
	PutPacketType(buf, PacketType_FOLLOWMOVE, 7);
	PutWord(buf, x);
	PutWord(buf, y);
	return PutWord(buf, z);
}

/*
 * 0x0049903D - PacketManager::MakePacket_FOLLOW
 *
 * Builds packet 0x15 (FOLLOW), size 9: the follower and target serials.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_FOLLOW(uint8_t *buf, uint32_t follower, uint32_t target)
{
	PutPacketType(buf, PacketType_FOLLOW, 9);
	PutDWord(buf, follower);
	PutDWord(buf, target);
}

/*
 * 0x00499072 - PacketManager::MakePacket_GROUPS
 *
 * Packet 0x39 (GROUPS), size 9. Writes packet type and two dwords.
 */
void
PacketManager_MakePacket_GROUPS(uint8_t *buf, uint32_t serial1, uint32_t serial2)
{
	PutPacketType(buf, 0x39, 9);
	PutDWord(buf, serial1);
	PutDWord(buf, serial2);
}

/*
 * 0x0049918A
 * Packet 0x1D (DESTROY_OBJECT) - 5 bytes
 */
uint16_t
PacketManager_MakePacket_DESTROY_OBJECT(uint8_t *buf, uint32_t serial)
{
	PutPacketType(buf, PacketType_DESTROY_OBJECT, 5);
	return PutDWord(buf, serial);
}

/*
 * 0x004991AF
 * Packet 0x77 (NAKED_MOB) - 17 bytes
 */
uint16_t
PacketManager_MakePacket_NAKED_MOB(uint8_t *buf, CMobile *mob, uint8_t notoriety)
{
	PutPacketType(buf, PacketType_NAKED_MOB, 0x11);
	PutDWord(buf, mob->container.item.serial);
	PutWord(buf, mob->container.item.resourceEntity.entity.bodyType);
	PutWord(buf, mob->container.item.resourceEntity.entity.location.x);
	PutWord(buf, mob->container.item.resourceEntity.entity.location.y);
	PutByte(buf, (uint8_t)mob->container.item.resourceEntity.entity.location.z);
	PutByte(buf, mob->direction);
	PutWord(buf, mob->container.item.resourceEntity.entity.color);
	PutByte(buf, GetStatusFlagsWrapper(&mob->container.item));
	return PutByte(buf, notoriety);
}

/*
 * 0x00499281
 * Packet 0x78 (EQUIPPED_MOB)
 * Parameters:
 *   buf       - packet buffer
 *   mob       - the mobile to describe
 *   notoriety - notoriety value (0-7)
 */
uint16_t
PacketManager_MakePacket_EQUIPPED_MOB(uint8_t *buf, CMobile *mob, uint8_t notoriety)
{
	CEntity *ent;

	ent = &mob->container.item.resourceEntity.entity;

	PutPacketType(buf, PacketType_EQUIPPED_MOB, 0x2013);
	PutDWord(buf, mob->container.item.serial);

	PutWord(buf, ent->bodyType);

	PutWord(buf, ent->location.x);
	PutWord(buf, ent->location.y);
	PutByte(buf, (uint8_t)ent->location.z);
	PutByte(buf, mob->direction);
	PutWord(buf, ent->color);

	PutByte(buf, GetStatusFlagsWrapper(&mob->container.item));

	PutByte(buf, notoriety);

	// Equipment loop: iterate layers 0-29 (skip slot 0=cursor, slot 29=bank)
	{
		int i;
		CItem *item;
		uint16_t graphic;

		for (i = 0; i < 30; i++) {
			if (mob->equipment[i] == NULL)
				continue;
			if (i == 0)
				continue;
			if (i == 0x1D)
				continue;
			item = mob->equipment[i];
			PutDWord(buf, item->serial);
			graphic = item->resourceEntity.entity.bodyType;
			if (item->resourceEntity.entity.color != 0)
				graphic |= 0x8000;
			PutWord(buf, graphic);
			PutByte(buf, (uint8_t)i);
			if (item->resourceEntity.entity.color != 0)
				PutWord(buf, item->resourceEntity.entity.color);
		}
	}

	// Terminator
	PutDWord(buf, 0);

	return 0;
}

/*
 * 0x004990CC - PacketManager::MakePacket_UNK_3D
 *
 * Builds packet 0x3D, size 2 with a 0xFF/0x00 pair baked into the
 * PutPacketType call. Writes a leading 0xFF, a mode byte, the two
 * halves of loc as words, a word argument and a trailing zero word.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_UNK_3D(uint8_t *buf, uint8_t mode, uint32_t loc, uint16_t arg)
{
	PutPacketType(buf, PacketType_UNK_3D, 2);
	PutByte(buf, 0xFF);
	PutByte(buf, mode);
	PutWord(buf, (uint16_t)loc);
	PutWord(buf, (uint16_t)(loc >> 16));
	PutWord(buf, arg);
	PutWord(buf, 0);
}

/*
 * 0x00499154 - PacketManager::MakePacket_UNK_1E
 *
 * Builds packet 0x1E, size 4: a word followed by a byte.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_UNK_1E(uint8_t *buf, uint16_t value, uint8_t flag)
{
	PutPacketType(buf, PacketType_UNK_1E, 4);
	PutWord(buf, value);
	PutByte(buf, flag);
}

/*
 * 0x00499438 - PacketManager::MakePacket_MOVE
 *
 * Builds a MOVE (0x1A) packet announcing an item/mobile's position,
 * graphic, amount, direction, and status flags.
 */
void
PacketManager_MakePacket_MOVE(uint8_t *buf, CItem *item)
{
	int amount;
	int facing;
	uint16_t graphic;

	PutPacketType(buf, 0x1A, 0x15);

	// 0x0049944E-0x0049946D: GetGraphic, check tiledata TF_STACKABLE
	graphic = item->resourceEntity.entity.bodyType;
	if (g_ItemTileData[graphic].flags & TF_STACKABLE) {
		// TF_STACKABLE: use CItem_GetMinResourceRatio
		amount = CItem_GetMinResourceRatio(item);
	} else {
		// Not stackable: use GetAmount (vtable[0x168])
		amount = (uint16_t)((int (*)(void *))VT_FN(item, VT_GET_AMOUNT))(item);
	}

	// 0x00499494-0x0049949A: clamp to min 1
	if (amount == 0)
		amount = 1;

	// 0x004994A1-0x004994D2: serial, bit 31 set if amount != 1
	if (amount != 1)
		PutDWord(buf, item->serial | 0x80000000);
	else
		PutDWord(buf, item->serial);

	// 0x004994D5-0x00499514: graphic from multi typeId or GetGraphic
	if (CItem_IsMultiOwner(item) == 1 && !VT_IsMobile(item)) {
		graphic = (uint16_t)CMultiSlave_GetTypeId(CItem_GetMultiSlave(item));
		graphic |= 0x4000;
	} else {
		graphic = item->resourceEntity.entity.bodyType;
	}

	// 0x00499518-0x00499530: if IsStackable, set graphic bit 15
	if (CItem_IsStackable(item))
		graphic |= 0x8000;
	PutWord(buf, graphic);

	// 0x00499545-0x00499568: if IsStackable, write stack byte
	if (CItem_IsStackable(item))
		PutByte(buf, CItem_IsStackable(item));

	if (amount != 1)
		PutWord(buf, (uint16_t)amount);

	graphic = item->resourceEntity.entity.location.x;
	amount = ((int (*)(void *))VT_FN(item, VT_GET_DIRECTION))(item);
	if (amount != 0)
		graphic |= 0x8000;
	PutWord(buf, graphic);

	// 0x004995C0-0x004995DF: y coordinate with hue and facing flags
	graphic = item->resourceEntity.entity.location.y;
	if (item->resourceEntity.entity.color != 0)
		graphic |= 0x8000;

	// 0x004995E3-0x004995F0: GetStatusFlagsWrapper (0x00490E4D)
	facing = GetStatusFlagsWrapper(item);
	if (facing != 0)
		graphic |= 0x4000;
	PutWord(buf, graphic);

	// 0x00499615-0x00499628: direction byte (if non-zero)
	if (amount != 0)
		PutByte(buf, (uint8_t)amount);

	// 0x0049962B-0x0049963B: z coordinate
	PutByte(buf, (uint8_t)item->resourceEntity.entity.location.z);

	// 0x0049963E-0x0049965C: hue word (if color != 0)
	if (item->resourceEntity.entity.color != 0)
		PutWord(buf, item->resourceEntity.entity.color);

	// 0x0049965F-0x00499672: facing byte (if non-zero)
	if (facing != 0)
		PutByte(buf, (uint8_t)facing);
}

/*
 * 0x00499679 - PacketManager::MakePacket_ZMOVE
 *
 * Builds a ZMOVE (0x20) packet telling the client where and how to
 * render the player's own character.
 */
uint16_t
PacketManager_MakePacket_ZMOVE(uint8_t *buf, CMobile *mob)
{
	uint8_t statusFlags;

	PutPacketType(buf, PacketType_ZMOVE, 0x13);
	PutDWord(buf, mob->container.item.serial);
	PutWord(buf, mob->container.item.resourceEntity.entity.bodyType);
	PutByte(buf, CItem_IsStackable(&mob->container.item));
	PutWord(buf, mob->container.item.resourceEntity.entity.color);
	statusFlags = GetStatusFlagsWrapper(&mob->container.item);
	PutByte(buf, statusFlags);
	PutWord(buf, mob->container.item.resourceEntity.entity.location.x);
	PutWord(buf, mob->container.item.resourceEntity.entity.location.y);
	PutWord(buf, g_MapFacetId);
	PutByte(buf, ((int (*)(void *))VT_FN(&mob->container.item, VT_GET_DIRECTION))(&mob->container.item));
	return PutByte(buf, (uint8_t)mob->container.item.resourceEntity.entity.location.z);
}

/*
 * 0x0049978E - PacketManager::MakePacket_LOGIN_CONFIRM
 *
 * Builds a LOGIN_CONFIRM (0x1B) packet confirming the player's
 * identity, position, facet, and map bounds on login.
 */
uint16_t
PacketManager_MakePacket_LOGIN_CONFIRM(uint8_t *buf, CPlayer *player)
{
	int n;

	PutPacketType(buf, PacketType_LOGIN_CONFIRM, 37);
	PutDWord(buf, player->mobile.container.item.serial);
	PutDWord(buf, 0x00000000);
	PutWord(buf, player->mobile.container.item.resourceEntity.entity.bodyType);
	PutWord(buf, player->mobile.container.item.resourceEntity.entity.location.x);
	PutWord(buf, player->mobile.container.item.resourceEntity.entity.location.y);
	PutWord(buf, (uint16_t)player->mobile.container.item.resourceEntity.entity.location.z);
	PutByte(buf, player->mobile.direction);
	PutByte(buf, g_Config.id);
	n = GetRandomRange(1, 15);
	PutDWord(buf, g_HighlightColorTable[n]);
	PutWord(buf, (uint16_t)g_Config.x);
	PutWord(buf, (uint16_t)g_Config.y);
	PutWord(buf, (uint16_t)g_Config.width);
	PutWord(buf, (uint16_t)g_Config.height);
	PutDWord(buf, 0x00000000);
	return PutWord(buf, 0x0000);
}

/*
 * 0x004998D3
 * Packet 0x21 (BLOCKED_MOVE) - 8 bytes
 */
uint16_t
PacketManager_MakePacket_BLOCKED_MOVE(uint8_t *buf, uint8_t sequence, uint16_t x, uint16_t y, uint8_t direction, uint8_t z)
{
	PutPacketType(buf, PacketType_BLOCKED_MOVE, 8);
	PutByte(buf, sequence);
	PutWord(buf, x);
	PutWord(buf, y);
	PutByte(buf, direction);
	return PutByte(buf, z);
}

/*
 * 0x0049993A
 */
uint16_t
PacketManager_MakePacket_OK_MOVE(uint8_t *buf, uint8_t sequence, uint8_t notoriety)
{
	PutPacketType(buf, PacketType_OK_MOVE, 3);
	PutByte(buf, sequence);
	return PutByte(buf, notoriety);
}

/*
 * 0x0049996F
 * Packet 0x23 (OBJMOVE) - 26 bytes
 * Drag animation: shows an item moving between source and destination.
 * Note: packet writes destination info before source info.
 */
uint16_t
PacketManager_MakePacket_OBJMOVE(uint8_t *buf, uint16_t itemID, uint8_t stackable, uint16_t srcX, uint16_t srcY, uint32_t srcSerial, uint16_t srcNewX, uint16_t srcNewY,
        uint8_t srcZ, uint32_t dstSerial, uint16_t dstX, uint16_t dstY, uint8_t dstZ)
{
	PutPacketType(buf, 0x23, 0x1A);
	PutWord(buf, itemID);
	PutByte(buf, stackable);
	PutWord(buf, srcX);
	PutWord(buf, srcY);
	// Destination written first in packet
	PutDWord(buf, dstSerial);
	PutWord(buf, dstX);
	PutWord(buf, dstY);
	PutByte(buf, dstZ);
	// Source written second
	PutDWord(buf, srcSerial);
	PutWord(buf, srcNewX);
	PutWord(buf, srcNewY);
	return PutByte(buf, srcZ);
}

/*
 * 0x00499A4B
 * Packet 0x24 (OPEN_GUMP) - 7 bytes
 */
uint16_t
PacketManager_MakePacket_OPEN_GUMP(uint8_t *buf, uint32_t gumpSerial, uint16_t gumpType)
{
	PutPacketType(buf, PacketType_OPEN_GUMP, 7);
	PutDWord(buf, gumpSerial);
	return PutWord(buf, gumpType);
}

/*
 * 0x00499A81 - PacketManager::MakePacket_MAP_DISPLAY
 *
 * Builds packet 0x90 (19 bytes): serial, gumpArtId, upperLeftX, upperLeftY,
 * lowerRightX, lowerRightY, width, height.
 */
void
PacketManager_MakePacket_MAP_DISPLAY(uint8_t *buf, uint32_t serial, uint16_t gumpId, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t width, uint16_t height)
{
	PutPacketType(buf, 0x90, 0x13);
	PutDWord(buf, serial);
	PutWord(buf, gumpId);
	PutWord(buf, x1);
	PutWord(buf, y1);
	PutWord(buf, x2);
	PutWord(buf, y2);
	PutWord(buf, width);
	PutWord(buf, height);
}

/*
 * 0x00499B20 - PacketManager::MakePacket_TEXT
 *
 * Builds a TEXT (0x1C) speech packet. GM players get a "GM " prefix;
 * non-mobiles send with serial 0xFFFFFFFF, bodyType 0xFFFF, and an
 * empty name. Text is capped at 1023 chars. The target arg is unused
 * (callers pass the speaker twice).
 */
uint16_t
PacketManager_MakePacket_TEXT(uint8_t *buf, CItem *entity, CItem *target, uint8_t speechType, const char *text, uint16_t hue, uint16_t font)
{
	uint32_t serial;
	uint16_t bodyType;
	char nameBuf[35];
	char textBuf[1024];

	USED(target);

	PutPacketType(buf, PacketType_TEXT, 0x42C);

	if (entity != NULL)
		serial = entity->serial;
	else
		serial = 0xFFFFFFFF;

	if (entity != NULL)
		bodyType = entity->resourceEntity.entity.bodyType;
	else
		bodyType = 0xFFFF;

	PutDWord(buf, serial);
	PutWord(buf, bodyType);
	PutByte(buf, speechType);
	PutWord(buf, hue);
	PutWord(buf, font);

	memset(nameBuf, 0, sizeof(nameBuf));

	if (entity != NULL && VT_IsPlayer(entity)) {
		if (CPlayer_IsGMAndManifested((CPlayer *)entity)) {
			sprintf(nameBuf, "GM %s", ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity));
		} else {
			strcpy(nameBuf, ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity));
		}
	} else if (entity != NULL && VT_IsNPC(entity)) {
		// vtable[0x34] GetName, strcpy
		strcpy(nameBuf, ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity));
	}

	PutString(buf, nameBuf, 30);

	strncpy(textBuf, text, 1023);
	textBuf[1023] = '\0';
	return PutString(buf, textBuf, strlen(textBuf) + 1);
}

/*
 * 0x00499CE2 - PacketManager::MakePacket_ASCII_SPEECH
 *
 * Packet 0x1C (ASCII SPEECH), max size 0x42C (1068). Writes serial,
 * model, speech type, color, font, 30-byte name, and variable-length
 * ASCII message text.
 */
void
PacketManager_MakePacket_ASCII_SPEECH(uint8_t *buf, uint32_t serial, uint16_t model, char *name, uint8_t speechType, char *text, uint16_t color, uint16_t font)
{
	char nameBuf[30];
	char textBuf[1024];

	PutPacketType(buf, 0x1C, 0x42C);
	PutDWord(buf, serial);
	PutWord(buf, model);
	PutByte(buf, speechType);
	PutWord(buf, color);
	PutWord(buf, font);
	memset(nameBuf, 0, 30);
	strcpy(nameBuf, name);
	PutString(buf, nameBuf, 30);
	strncpy(textBuf, text, 1023);
	textBuf[1023] = '\0';
	PutString(buf, textBuf, strlen(textBuf) + 1);
}

/*
 * 0x00499DCC - PacketManager::MakePacket_OBJ_TO_OBJ
 *
 * Builds an OBJ_TO_OBJ (0x25) packet placing an item inside a
 * container, with amount resolved via the container's excluded-amount
 * hook, TF_STACKABLE, or vtable GetAmount.
 */
uint16_t
PacketManager_MakePacket_OBJ_TO_OBJ(uint8_t *buf, CItem *item, CItem *container)
{
	PutPacketType(buf, PacketType_OBJ_TO_OBJ, 0x14);
	PutDWord(buf, item->serial);
	PutWord(buf, item->resourceEntity.entity.bodyType);
	PutByte(buf, CItem_IsStackable(item));

	// Amount: 3-way dispatch matching binary
	if (((int (*)(void *))VT_FN(container, VT_EXCLUDED_AMOUNT))(container)) {
		PutWord(buf, (uint16_t)CItem_GetTiledataQuantity(item));
	} else if (g_ItemTileData[item->resourceEntity.entity.bodyType].flags & TF_STACKABLE) {
		PutWord(buf, (uint16_t)CItem_GetMinResourceRatio(item));
	} else {
		PutWord(buf, (uint16_t)((int (*)(void *))VT_FN(item, VT_GET_AMOUNT))(item));
	}

	PutWord(buf, item->resourceEntity.entity.location.x);
	PutWord(buf, item->resourceEntity.entity.location.y);
	PutDWord(buf, container->serial);
	return PutWord(buf, item->resourceEntity.entity.color);
}

/*
 * 0x00499EEB - PacketManager::MakePacket_MULTI_OBJ_TO_OBJ
 *
 * Builds a MULTI_OBJ_TO_OBJ (0x3C) packet listing a container's
 * contents in reverse spatial order (cap 128 items). filterMode skips
 * items with amount <= 0; sellMode uses GetMinResourceRatio instead of
 * GetAmount.
 */
uint16_t
PacketManager_MakePacket_MULTI_OBJ_TO_OBJ(uint8_t *buf, CContainer *cont, int filterMode, int sellMode)
{
	CItem *cur;
	CItem *lastItem;
	uint16_t count, packetLen, amount;
	int isPlayer;

	lastItem = NULL;
	packetLen = 6;
	count = 0;

	// isPlayer flag: filterMode overrides, else vtable[0x100]
	if (filterMode != 0)
		isPlayer = 1;
	else if (((int (*)(void *))VT_FN(&cont->item, VT_EXCLUDED_AMOUNT))(&cont->item))
		isPlayer = 1;
	else
		isPlayer = 0;

	// Forward pass: count items via spatialNext, save last item
	cur = cont->contents;
	while (cur != NULL) {
		// Compute amount (3-way dispatch)
		if (((int (*)(void *))VT_FN(&cont->item, VT_EXCLUDED_AMOUNT))(&cont->item))
			amount = (uint16_t)CItem_GetTiledataQuantity(cur);
		else if (sellMode)
			amount = (uint16_t)CItem_GetMinResourceRatio(cur);
		else
			amount = (uint16_t)((int (*)(void *))VT_FN(cur, VT_GET_ITEM_AMOUNT))(cur);

		// Filter: skip items with amount == 0 when filterMode set
		if (!(filterMode != 0 && (amount & 0xFFFF) <= 0)) {
			packetLen += 0x15;
			count++;
		}

		// Track last item (spatialNext == NULL)
		if (cur->spatialNext == NULL)
			lastItem = cur;

		cur = cur->spatialNext;
	}

	// Clamp to 128 items
	if ((count & 0xFFFF) > 0x80) {
		count = 0x80;
		packetLen = 0x0A86;
	}

	PutPacketType(buf, PacketType_MULTI_OBJ_TO_OBJ, packetLen);
	PutWord(buf, count);

	// Backward pass: write items via spatialPrev in reverse
	cur = lastItem;
	while (cur != NULL) {
		// Compute amount (same 3-way dispatch)
		if (((int (*)(void *))VT_FN(&cont->item, VT_EXCLUDED_AMOUNT))(&cont->item))
			amount = (uint16_t)CItem_GetTiledataQuantity(cur);
		else if (sellMode)
			amount = (uint16_t)CItem_GetMinResourceRatio(cur);
		else
			amount = (uint16_t)((int (*)(void *))VT_FN(cur, VT_GET_ITEM_AMOUNT))(cur);

		// Filter: skip items with amount == 0 when filterMode set
		if (filterMode != 0 && (amount & 0xFFFF) == 0) {
			cur = cur->spatialPrev;
			continue;
		}

		// Write item
		PutDWord(buf, cur->serial);
		PutWord(buf, cur->resourceEntity.entity.bodyType);
		PutByte(buf, CItem_IsStackable(cur));

		// Amount for packet: isPlayer uses pre-computed amount,
		// otherwise tiledata stackable / GetMinResourceRatio / vtable[0x168]
		if (isPlayer) {
			PutWord(buf, amount);
		} else {
			if (g_ItemTileData[cur->resourceEntity.entity.bodyType].flags & TF_STACKABLE)
				PutWord(buf, (uint16_t)CItem_GetMinResourceRatio(cur));
			else
				PutWord(buf, (uint16_t)((int (*)(void *))VT_FN(cur, VT_GET_AMOUNT))(cur));
		}

		PutWord(buf, cur->resourceEntity.entity.location.x);
		PutWord(buf, cur->resourceEntity.entity.location.y);
		PutDWord(buf, cont->item.serial);
		PutWord(buf, cur->resourceEntity.entity.color);

		count--;
		if (count == 0)
			break;

		cur = cur->spatialPrev;
	}

	return 0;
}

/*
 * 0x0049A1AC
 * Packet 0x27 (GETOBJ_FAILED) - 2 bytes
 */
uint16_t
PacketManager_MakePacket_GETOBJ_FAILED(uint8_t *buf, uint8_t reason)
{
	PutPacketType(buf, PacketType_GETOBJ_FAILED, 2);
	return PutByte(buf, reason);
}

/*
 * 0x0049A1D1
 * Packet 0x28 (DROPOBJ_FAILED) - 5 bytes
 */
uint16_t
PacketManager_MakePacket_DROPOBJ_FAILED(uint8_t *buf, uint16_t x, uint16_t y)
{
	PutPacketType(buf, PacketType_DROPOBJ_FAILED, 5);
	PutWord(buf, x);
	return PutWord(buf, y);
}

/*
 * 0x0049A208
 * Packet 0x29 (DROPOBJ_OK) - 1 byte
 */
uint16_t
PacketManager_MakePacket_DROPOBJ_OK(uint8_t *buf)
{
	return PutPacketType(buf, PacketType_DROPOBJ_OK, 1);
}

/*
 * 0x0049A21D
 * Packet 0x2A (DEATHACTION) - 5 bytes
 */
uint16_t
PacketManager_MakePacket_DEATHACTION(uint8_t *buf, uint32_t serial)
{
	PutPacketType(buf, 0x2A, 5);
	return PutDWord(buf, serial);
}

/*
 * 0x0049A242
 */
uint16_t
PacketManager_MakePacket_GODMODE(uint8_t *buf, uint8_t editing)
{
	PutPacketType(buf, PacketType_GODMODE, 2);
	return PutByte(buf, editing);
}

/*
 * 0x0049A267
 * Packet 0x2C (DEATH) - 2 bytes. Shows death/resurrection screen on client.
 * flag=0: dead (show death dialog), flag=1: alive (dismiss death dialog).
 */
uint16_t
PacketManager_MakePacket_DEATH(uint8_t *buf, uint8_t flag)
{
	PutPacketType(buf, PacketType_DEATH, 2);
	return PutByte(buf, flag);
}

/*
 * 0x0049A28C
 * Packet 0x2D (HEALTH / combined stat update) - 17 bytes
 */
uint16_t
PacketManager_MakePacket_HEALTH(uint8_t *buf, uint32_t serial, uint16_t maxHp, uint16_t hp, uint16_t maxMana, uint16_t mana, uint16_t maxStamina, uint16_t stamina)
{
	PutPacketType(buf, PacketType_HEALTH, 17);
	PutDWord(buf, serial);
	PutWord(buf, maxHp);
	PutWord(buf, hp);
	PutWord(buf, maxMana);
	PutWord(buf, mana);
	PutWord(buf, maxStamina);
	return PutWord(buf, stamina);
}

/*
 * 0x0049A317
 * Packet 0xA1 (HP_HEALTH) - 9 bytes
 */
uint16_t
PacketManager_MakePacket_HP_HEALTH(uint8_t *buf, uint32_t serial, uint16_t maxHp, uint16_t hp)
{
	PutPacketType(buf, PacketType_HP_HEALTH, 9);
	PutDWord(buf, serial);
	PutWord(buf, maxHp);
	return PutWord(buf, hp);
}

/*
 * 0x0049A361
 * Packet 0xA2 (MANA_HEALTH) - 9 bytes
 */
uint16_t
PacketManager_MakePacket_MANA_HEALTH(uint8_t *buf, uint32_t serial, uint16_t maxMana, uint16_t mana)
{
	PutPacketType(buf, PacketType_MANA_HEALTH, 9);
	PutDWord(buf, serial);
	PutWord(buf, maxMana);
	return PutWord(buf, mana);
}

/*
 * 0x0049A3AB
 * Packet 0xA3 (FAT_HEALTH / stamina) - 9 bytes
 */
uint16_t
PacketManager_MakePacket_FAT_HEALTH(uint8_t *buf, uint32_t serial, uint16_t maxStamina, uint16_t stamina)
{
	PutPacketType(buf, PacketType_FAT_HEALTH, 9);
	PutDWord(buf, serial);
	PutWord(buf, maxStamina);
	return PutWord(buf, stamina);
}

/*
 * 0x0049A3F5
 * Packet 0x2E (EQUIP_ITEM) - 15 bytes
 */
uint16_t
PacketManager_MakePacket_EQUIP_ITEM(uint8_t *buf, CItem *item, CMobile *wearer, uint8_t layer)
{
	PutPacketType(buf, PacketType_EQUIP_ITEM, 0x0F);
	PutDWord(buf, item->serial);
	PutWord(buf, item->resourceEntity.entity.bodyType);
	PutByte(buf, CItem_IsStackable(item));
	PutByte(buf, layer);
	PutDWord(buf, wearer->container.item.serial);
	return PutWord(buf, item->resourceEntity.entity.color);
}

/*
 * 0x0049A47E
 * Packet 0x2F (SWING) - 10 bytes
 */
uint16_t
PacketManager_MakePacket_SWING(uint8_t *buf, uint8_t flag, uint32_t attackerSerial, uint32_t defenderSerial)
{
	PutPacketType(buf, PacketType_SWING, 0x0A);
	PutByte(buf, flag);
	PutDWord(buf, attackerSerial);
	return PutDWord(buf, defenderSerial);
}

/*
 * 0x0049A4C3
 * Packet 0x30 (ATTACK_OK) - 5 bytes
 */
uint16_t
PacketManager_MakePacket_ATTACK_OK(uint8_t *buf, uint32_t targetSerial)
{
	PutPacketType(buf, PacketType_ATTACK_OK, 5);
	return PutDWord(buf, targetSerial);
}

/*
 * 0x0049A4E8
 * Packet 0x31 (ATTACK_END) - 1 byte
 */
uint16_t
PacketManager_MakePacket_ATTACK_END(uint8_t *buf)
{
	return PutPacketType(buf, PacketType_ATTACK_END, 1);
}

/*
 * 0x0049A4FD - PacketManager::MakePacket_MOBILESTAT
 *
 * Builds a MOBILESTAT (0x11) status bar packet for a mobile. If
 * extended, appends stats/stam/mana/gold/armor/weight. viewerSerial is
 * used to decide whether the NPC is renameable by the viewer.
 */
uint16_t
PacketManager_MakePacket_MOBILESTAT(uint8_t *buf, CMobile *mob, uint32_t viewerSerial, uint8_t extended)
{
	char name[30];
	uint8_t sex;
	int gold;
	uint16_t armor, weight;

	memset(name, 0, sizeof(name));
	sex = 0;

	PutPacketType(buf, PacketType_MOBILESTAT, 0x42);
	PutDWord(buf, mob->container.item.serial);

	if (VT_IsPlayer(&mob->container.item)) {
		char *nameStr = ((char *(*)(void *, int))VT_FN(&mob->container.item, VT_SPEAK_SYS_MSG))(&mob->container.item, 1);
		strcpy(name, nameStr);
		PutString(buf, name, 30);
		PutWord(buf, (uint16_t)mob->hp);
		PutWord(buf, (uint16_t)mob->maxHp);
		PutByte(buf, 0);
		PutByte(buf, extended);
		if (extended & 0xFF)
			PutByte(buf, mob->sex);
	} else if (VT_IsNPC(&mob->container.item)) {
		char *nameStr = ((char *(*)(void *))VT_FN(&mob->container.item, VT_GET_NAME))(&mob->container.item);
		strcpy(name, nameStr);
		PutString(buf, name, 30);
		PutWord(buf, (uint16_t)mob->hp);
		PutWord(buf, (uint16_t)mob->maxHp);
		{
			int canRename;
			if (CMobile_IsPet(mob) && CMobile_CheckOwner(mob, viewerSerial))
				canRename = 1;
			else
				canRename = 0;
			PutByte(buf, (uint8_t)canRename);
		}
		PutByte(buf, extended);
		if (extended & 0xFF)
			PutByte(buf, sex);
	} else {
		PutString(buf, name, 30);
		PutWord(buf, (uint16_t)mob->hp);
		PutWord(buf, (uint16_t)mob->maxHp);
		PutByte(buf, 0);
		PutByte(buf, extended);
		if (extended & 0xFF)
			PutByte(buf, sex);
	}

	if (extended & 0xFF) {
		PutWord(buf, CMobile_GetStat(mob, 0));
		PutWord(buf, CMobile_GetStat(mob, 1));
		PutWord(buf, CMobile_GetStat(mob, 2));

		PutWord(buf, (uint16_t)mob->stamina);
		PutWord(buf, (uint16_t)mob->maxStamina);
		PutWord(buf, (uint16_t)mob->mana);
		PutWord(buf, (uint16_t)mob->maxMana);

		if (VT_IsVendor(&mob->container.item))
			gold = CMobile_AmountGoldInBank(mob);
		else
			gold = CMobile_GetTotalQuantityOfType(mob, 0xEED);
		PutDWord(buf, (uint32_t)gold);

		armor = (uint16_t)Combat_CalcArmorClass(mob);
		PutWord(buf, armor);

		weight = (uint16_t)((int (*)(void *))VT_FN(&mob->container.item, VT_GET_WEIGHT))(&mob->container.item);
		PutWord(buf, weight);
	}

	return 0;
}

/*
 * 0x0049A87D - MakePacket_RESTYPE_DATA
 *
 * Builds a RESTYPE_DATA (0x35) packet with a resource type's five
 * names plus field_288/field_28C for the GM editor.
 */
void
MakePacket_RESTYPE_DATA(uint8_t *buf, int typeId)
{
	CResourceType *rt;

	rt = CResourceTypeManager_GetId(typeId);
	if (rt == NULL)
		return;

	PutPacketType(buf, 0x35, 0x28D);
	PutDWord(buf, typeId);
	PutString(buf, CResourceType_GetInternalName(rt), 0x80);
	PutString(buf, CResourceType_GetFoodName(rt), 0x80);
	PutString(buf, CResourceType_GetName1(rt), 0x80);
	PutString(buf, CResourceType_GetName2(rt), 0x80);
	PutString(buf, CResourceType_GetName3(rt), 0x80);
	PutDWord(buf, CResourceType_GetField288(rt));
	PutDWord(buf, CResourceType_GetField28C(rt));
}

/*
 * 0x0049A970 - PacketManager::MakePacket_ELEVCHANGE
 *
 * Builds packet 0x14 (ELEVCHANGE), size 6: two words and a byte.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_ELEVCHANGE(uint8_t *buf, uint16_t a, uint16_t b, uint8_t c)
{
	PutPacketType(buf, PacketType_ELEVCHANGE_OUT, 6);
	PutWord(buf, a);
	PutWord(buf, b);
	PutByte(buf, c);
}

/*
 * 0x0049A9B7 - PacketManager::MakePacket_RESOURCETILEDATA
 *
 * Builds a single RESOURCETILEDATA (0x36) packet for one resource node.
 * Mode 2, fixed size 0x1f. Fields: isLast, mode(2), serial, index,
 * nodeX/Y/Z (word from int32), nodeType (sign-extended byte), two zero
 * dwords, then nodeId (or 0 if id==0).
 */
void
PacketManager_MakePacket_RESOURCETILEDATA(uint8_t *buf, uint32_t serial, uint32_t isLast, uint16_t index, CResourceNode *node)
{
	uint16_t defaultNodeId = 0;
	uint32_t unused = 0;
	uint16_t mode = 2;

	PutPacketType(buf, PacketType_RESOURCETILEDATA, 0x1f);
	PutWord(buf, (uint16_t)isLast);
	PutWord(buf, mode);
	PutDWord(buf, serial);
	PutWord(buf, index);
	PutWord(buf, (uint16_t)node->value1);
	PutWord(buf, (uint16_t)node->value2);
	PutWord(buf, (uint16_t)node->value3);
	PutWord(buf, (uint16_t)(int8_t)node->type);
	PutDWord(buf, unused);
	PutDWord(buf, unused);
	if (node->id != 0)
		PutWord(buf, node->id);
	else
		PutWord(buf, defaultNodeId);
}

/*
 * 0x0049AACB - PacketManager::MakePacket_RESOURCETILEDATA_Region
 *
 * Builds a RESOURCETILEDATA (0x36) packet for region music. Mode 0
 * writes only the header; mode 1 appends the node's value/type/id.
 */
void
PacketManager_MakePacket_RESOURCETILEDATA_Region(uint8_t *buf, uint32_t regionIndex, uint32_t mode, uint32_t count, CResourceNode *node)
{
	uint16_t defaultNodeId = 0;
	uint32_t unused = 0;

	PutPacketType(buf, PacketType_RESOURCETILEDATA, 0x1f);
	PutWord(buf, (uint16_t)regionIndex);
	PutWord(buf, (uint16_t)mode);
	PutWord(buf, (uint16_t)count);

	if ((mode & 0xFFFF) == 0)
		return;

	// mode != 0: write node data
	PutWord(buf, (uint16_t)node->value1);
	PutWord(buf, (uint16_t)node->value2);
	PutWord(buf, (uint16_t)node->value3);
	PutWord(buf, (uint16_t)(int8_t)node->type);
	PutDWord(buf, unused);
	PutDWord(buf, unused);
	if (node->id != 0)
		PutWord(buf, node->id);
	else
		PutWord(buf, defaultNodeId);
}

/*
 * 0x0049ABDA - PacketManager::MakePacket_VERSIONS_FULL
 *
 * Builds packet 0x3E (VERSIONS), size 0x25: nine consecutive dwords.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_VERSIONS_FULL(uint8_t *buf, uint32_t v1, uint32_t v2, uint32_t v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8, uint32_t v9)
{
	PutPacketType(buf, PacketType_VERSIONS, 0x25);
	PutDWord(buf, v1);
	PutDWord(buf, v2);
	PutDWord(buf, v3);
	PutDWord(buf, v4);
	PutDWord(buf, v5);
	PutDWord(buf, v6);
	PutDWord(buf, v7);
	PutDWord(buf, v8);
	PutDWord(buf, v9);
}

/*
 * 0x0049AC7F - PacketManager::MakePacket_STATIC_DATA
 *
 * Builds a STATIC_DATA (0x3F) packet listing a map block's static
 * items (bodyType, 3-bit tile offsets, z, color).
 */
void
PacketManager_MakePacket_STATIC_DATA(uint8_t *buf, int blockIdx)
{
	int count;
	CItem *iter;
	uint8_t z;

	PutPacketType(buf, 0x3F, 0x10007);
	PutDWord(buf, blockIdx);

	// First pass: count static items in block
	count = 0;
	iter = g_MapBlocks[blockIdx].staticHead;
	while (iter != NULL) {
		count++;
		iter = (CItem *)iter->resourceEntity.nextInContainer;
	}

	PutDWord(buf, count);

	// Write zero padding
	count = 0;
	PutDWord(buf, count);

	// Second pass: write each static item's data
	iter = g_MapBlocks[blockIdx].staticHead;
	while (iter != NULL) {
		PutWord(buf, CEntity_GetBodyType(iter));
		PutByte(buf, (int16_t)CEntity_GetLocation((CEntity *)iter)->x & 7);
		PutByte(buf, (int16_t)CEntity_GetLocation((CEntity *)iter)->y & 7);
		z = CEntity_GetLocation((CEntity *)iter)->z;
		PutByte(buf, z);
		PutWord(buf, iter->resourceEntity.entity.color);
		iter = (CItem *)iter->resourceEntity.nextInContainer;
	}
}

/*
 * 0x0049ADC0 - PacketManager::MakePacket_UPD_TERRCHUNK
 *
 * Builds an UPD_TERRCHUNK (0x40) packet (201 bytes) with a block's
 * 8x8 terrain cells (tileID + z).
 */
void
PacketManager_MakePacket_UPD_TERRCHUNK(uint8_t *buf, int blockIdx)
{
	MapBlock *block;
	int i, j;
	uint16_t tileID;
	int zero;

	PutPacketType(buf, PacketType_UPD_TERRCHUNK, 0xC9);
	block = &g_MapBlocks[blockIdx];
	PutDWord(buf, blockIdx);

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++) {
			tileID = block->cells[i * 8 + j].tileID;
			PutWord(buf, tileID);
			PutByte(buf, block->cells[i * 8 + j].z);
		}
	}

	zero = 0;
	PutDWord(buf, zero);
}

/*
 * 0x0049AE91 - PacketManager::MakePacket_TILEDATA
 *
 * Builds packet 0x41 (TILEDATA). Max size 0x50B.
 * Fields: groupIndex (DWord), 0 (DWord), then 32 entries from either
 * g_LandTileData (if groupIndex < 0x200) or g_ItemTileData (otherwise).
 * Land entries: flags (DWord), textureID (Word), name (String20).
 * Item entries: flags (DWord), weight (Byte), layer (Byte), miscData
 * (DWord), value1 (Word), value2 (Word), height (Word), quantity
 * (Byte), name (String20).
 */
void
PacketManager_MakePacket_TILEDATA(uint8_t *buf, int groupIndex)
{
	int i;
	int startIdx;

	PutPacketType(buf, 0x41, 0x50B);
	PutDWord(buf, groupIndex);
	PutDWord(buf, 0);

	if (groupIndex < 0x200) {
		// Land tile data
		startIdx = groupIndex * 32;
		for (i = 0; i < 0x20; i++) {
			PutDWord(buf, g_LandTileData[startIdx + i].flags);
			PutWord(buf, g_LandTileData[startIdx + i].textureID);
			PutString(buf, g_LandTileData[startIdx + i].name, 0x14);
		}
	} else {
		// Item tile data
		startIdx = groupIndex * 32 - 0x4000;
		for (i = 0; i < 0x20; i++) {
			PutDWord(buf, g_ItemTileData[startIdx + i].flags);
			PutByte(buf, g_ItemTileData[startIdx + i].weight);
			PutByte(buf, g_ItemTileData[startIdx + i].layer);
			PutDWord(buf, g_ItemTileData[startIdx + i].miscData);
			PutWord(buf, g_ItemTileData[startIdx + i].value1);
			PutWord(buf, g_ItemTileData[startIdx + i].value2);
			PutWord(buf, g_ItemTileData[startIdx + i].height);
			PutByte(buf, g_ItemTileData[startIdx + i].quantity);
			PutString(buf, g_ItemTileData[startIdx + i].name, 0x14);
		}
	}
}

/*
 * 0x0049B0A5 - PacketManager::MakePacket_UPD_ART_TEXT
 *
 * Builds packet 0x42 (UPD_ART), variable length: a serial, a trailing
 * dword, then a length-prefixed string. A NULL text forces the length
 * to -1, and the string itself is only appended when the length is
 * positive - so the -1 case writes the length and stops.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_UPD_ART_TEXT(uint8_t *buf, uint32_t serial, char *text, int textLen, uint32_t tail)
{
	PutPacketType(buf, PacketType_UPD_ART, 0x1000F);
	PutDWord(buf, serial);
	PutDWord(buf, tail);
	if (text == NULL)
		textLen = -1;
	PutDWord(buf, (uint32_t)textLen);
	if (textLen > 0)
		PutString(buf, text, textLen);
}

/*
 * 0x0049B114 - PacketManager::MakePacket_NEW_ANIMDATA
 *
 * Builds packet 0x43 (NEW_ANIMDATA response). Size 0x229.
 * Fields: groupBase (DWord), mode (DWord), groupData (0x220 bytes).
 */
void
PacketManager_MakePacket_NEW_ANIMDATA(uint8_t *buf, int groupBase, int mode, uint8_t *groupData)
{
	PutPacketType(buf, 0x43, 0x229);
	PutDWord(buf, groupBase);
	PutDWord(buf, mode);
	PutString(buf, (char *)groupData, 0x220);
}

/*
 * 0x0049B161 - PacketManager::MakePacket_HUEDATA
 *
 * Builds packet 0x44 (HUEDATA). Max size 0x2C9.
 * Fields: groupIndex (DWord), 0 (DWord), then for each of 8 entries
 * in the group: 32 uint16 colors, uint16 startColor, uint16 endColor,
 * char[20] name.
 */
void
PacketManager_MakePacket_HUEDATA(uint8_t *buf, int groupIndex)
{
	int i, j;

	PutPacketType(buf, 0x44, 0x2C9);
	PutDWord(buf, groupIndex);
	PutDWord(buf, 0);

	for (i = groupIndex * 8; i < groupIndex * 8 + 8; i++) {
		for (j = 0; j < 0x20; j++)
			PutWord(buf, g_HueData[i].colors[j]);
		PutWord(buf, g_HueData[i].startColor);
		PutWord(buf, g_HueData[i].endColor);
		PutString(buf, g_HueData[i].name, 0x14);
	}
}

/*
 * 0x0049B25E - PacketManager::MakePacket_UPD_REGIONS
 *
 * Builds an UPD_REGIONS (0x57) packet (110 bytes) describing one
 * region's bounds, weather, and light level.
 */
void
PacketManager_MakePacket_UPD_REGIONS(uint8_t *buf, CRegion *region)
{
	PutPacketType(buf, PacketType_UPD_REGIONS, 0x6E);
	PutString(buf, region->name, 0x28);
	PutDWord(buf, region->prefix);
	PutWord(buf, region->x);
	PutWord(buf, region->y);
	PutWord(buf, region->width);
	PutWord(buf, region->height);
	PutWord(buf, region->zMin);
	PutWord(buf, region->zMax);
	PutDWord(buf, 0);
	PutString(buf, region->name2, 0x28);
	PutWord(buf, region->weatherDay);
	PutWord(buf, region->weatherSeason);
	PutWord(buf, region->weatherNight);
	PutByte(buf, region->type);
	PutWord(buf, region->lightLevel);
}

/*
 * 0x0049B398 - PacketManager::MakePacket_OFFERACCEPT
 *
 * Builds packet 0x3B to close a vendor gump. 56 bytes.
 */
uint16_t
PacketManager_MakePacket_OFFERACCEPT(uint8_t *buf, uint32_t vendorSerial, uint8_t flag)
{
	PutPacketType(buf, PacketType_OFFERACCEPT, 0x708);
	PutDWord(buf, vendorSerial);
	return PutByte(buf, flag);
}

/*
 * 0x0049B3D0 - PacketManager::MakePacket_LOGIN_REJECT
 *
 * Builds a LOGIN_REJECT packet carrying the given reason byte.
 */
uint16_t
PacketManager_MakePacket_LOGIN_REJECT(uint8_t *buf, uint8_t reason)
{
	PutPacketType(buf, PacketType_LOGIN_REJECT, 2);
	return PutByte(buf, reason);
}

/*
 * 0x0049B3F5
 */
uint16_t
PacketManager_MakePacket_LOGIN_COMPLETE(uint8_t *buf)
{
	return PutPacketType(buf, PacketType_LOGIN_COMPLETE, 1);
}

/*
 * 0x0049B40A - PacketManager::MakePacket_ACCT_LOGIN_OK
 *
 * Unused in client >= 1.25.35.
 */
uint16_t
PacketManager_MakePacket_ACCT_LOGIN_OK(uint8_t *buf, uint8_t numCharacters, uint8_t v, char *characterNames, char *characterPasswords)
{
	uint16_t result;
	int i;

	PutPacketType(buf, PacketType_ACCT_LOGIN_OK, PacketDynamicSize);
	PutByte(buf, numCharacters);
	PutByte(buf, v);
	for (i = 0; i < 5; i++) {
		PutString(buf, &characterNames[30 * i], 30);
		PutString(buf, &characterPasswords[30 * i], 30);
	}
	result = v;
	if (v == 0xCD) {
		PutByte(buf, 1);
		PutByte(buf, 0);
		PutString(buf, "Britannia", 16);
		PutByte(buf, 1);
		PutByte(buf, 0);
		PutString(buf, "Ocllo", 31);
		result = PutString(buf, "Bountiful Harvest", 31);
	}
	return result;
}

/*
 * 0x0049B5A0
 * Reasons (from client 1.25.35):
 * 0x00 "That character password is invalid."
 * 0x01 "That character does not exist."
 * 0x02 "That character is being played right now."
 * 0x03 "That character is not old enough to delete. The character must be 7 days old before it can be deleted."
 * 0x04 "That character is currently queued for backup and cannot be deleted."
 * 0x05 "Couldn't carry out your request."
 */
uint16_t
PacketManager_MakePacket_CHG_CHAR_RESULT(uint8_t *buf, uint8_t reason)
{
	PutPacketType(buf, PacketType_CHG_CHAR_RESULT, 2);
	return PutByte(buf, reason);
}

/*
 * 0x0049B55E - PacketManager::MakePacket_USER_SERVER_EMPTY
 *
 * Builds packet 0x8C (USER_SERVER), size 0xB, with every field zero -
 * a zero address, zero port and zero key.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_USER_SERVER_EMPTY(uint8_t *buf)
{
	PutPacketType(buf, PacketType_USER_SERVER, 0xB);
	PutDWord(buf, 0);
	PutWord(buf, 0);
	PutDWord(buf, 0);
}

/*
 * 0x0049B5C8 - PacketManager::MakePacket_ALL_CHARACTERS
 *
 * Builds an ALL_CHARACTERS (0x86) packet: count byte then
 * count x 60-byte name entries.
 */
uint16_t
PacketManager_MakePacket_ALL_CHARACTERS(uint8_t *buf, uint8_t numCharacters, char *characterNames)
{
	int i;

	PutPacketType(buf, PacketType_ALL_CHARACTERS, PacketDynamicSize);
	PutByte(buf, numCharacters);
	for (i = 0; i < numCharacters; i++) {
		PutString(buf, &characterNames[60 * i], 60);
	}
	return 0;
}

/*
 * 0x0049B632 - PacketManager::MakePacket_MAP_COMMAND
 *
 * Packet 0x56 (MAP_COMMAND) - 11 bytes fixed.
 * Builds map pin command response: serial, command, arg, plotX, plotY.
 */
void
PacketManager_MakePacket_MAP_COMMAND(uint8_t *buf, uint32_t serial, uint8_t command, uint8_t arg, uint16_t plotX, uint16_t plotY)
{
	PutPacketType(buf, PacketType_MAP_COMMAND, 0x0B);
	PutDWord(buf, serial);
	PutByte(buf, command);
	PutByte(buf, arg);
	PutWord(buf, plotX);
	PutWord(buf, plotY);
}

Britannia g_BritanniaList[] = {
	{ "Britannia", 0, 0, { 127, 0, 0, 1 } },
};

/*
 * Not present on UoDemo, but required on clients >= 1.25.35.
 * Flag byte: opaque round-trip token. The client stores it as
 * nextLoginKey and echoes it back as byte 61 of the next 0x80
 * ACCT_LOGIN_REQ. No client validates the value. 0x5D is conventional.
 */
uint16_t
PacketManager_MakePacket_BRITANNIA_LIST(uint8_t *buf, const uint8_t *serverAddr)
{
	unsigned int i, j;
	uint8_t flags;

	flags = 0x5D;

	PutPacketType(buf, PacketType_BRITANNIA_LIST, PacketDynamicSize);
	PutByte(buf, flags);
	PutWord(buf, nelem(g_BritanniaList));

	for (i = 0; i < nelem(g_BritanniaList); i++) {
		PutWord(buf, i);
		PutString(buf, g_BritanniaList[i].name, 32);
		PutByte(buf, g_BritanniaList[i].percentFull);
		PutByte(buf, g_BritanniaList[i].timeZone);
		for (j = 0; j < 4; j++)
			PutByte(buf, serverAddr[j]);
	}

	return 0;
}

/*
 * Not present on UoDemo, but required on clients >= 1.25.35.
 * The last 4 bytes are the auth seed that the client will send as
 * its login seed when reconnecting to the game server.
 */
uint16_t
PacketManager_MakePacket_USER_SERVER(uint8_t *buf, uint16_t numBritannia, uint16_t port, uint32_t authSeed, const uint8_t *serverAddr)
{
	unsigned int i;

	PutPacketType(buf, PacketType_USER_SERVER, 11);

	if (numBritannia > nelem(g_BritanniaList) - 1)
		return 0;

	for (i = 0; i < 4; i++)
		PutByte(buf, serverAddr[i]);
	PutWord(buf, port);
	// Auth seed: random nonce from PendingAuth. The client sends this
	// back as its game connection login seed. For Twofish clients
	// (2.0.4+) this also becomes the cipher key.
	PutDWord(buf, authSeed);

	return 0;
}

/*
 * Not present on UoDemo, but required on clients >= 1.25.35.
 */
uint16_t
PacketManager_MakePacket_FEATURES(uint8_t *buf)
{
	PutPacketType(buf, PacketType_FEATURES, 0x00);
	PutWord(buf, 0x03);

	return 0;
}

/*
 * Not present on UoDemo, but required on clients >= 1.25.35.
 */
uint16_t
PacketManager_MakePacket_CITIES_AND_CHARS(uint8_t *buf, char *characterNames, char *characterPasswords)
{
	unsigned int i;
	unsigned int numStartingPlaces;

	numStartingPlaces = g_PlaceNameCount;

	PutPacketType(buf, PacketType_CITIES_AND_CHARS, PacketDynamicSize);
	PutByte(buf, 5);
	for (i = 0; i < 5; i++) {
		PutString(buf, &characterNames[30 * i], 30);
		PutString(buf, &characterPasswords[30 * i], 30);
	}

	PutByte(buf, numStartingPlaces);
	for (i = 0; i < numStartingPlaces; i++) {
		PutByte(buf, i);
		PutString(buf, g_PlaceNameList[i].city, 31);
		PutString(buf, g_PlaceNameList[i].place, 31);
	}
	PutDWord(buf, 0x00);

	return 0;
}

/*
 * 0x0049B699
 * Packet 0x54 (SOUND) - 12 bytes
 */
uint16_t
PacketManager_MakePacket_SOUND(uint8_t *buf, uint8_t flags, uint16_t soundID, CLocation *location, uint16_t volume)
{
	PutPacketType(buf, PacketType_SOUND, 12);
	PutByte(buf, flags);
	PutWord(buf, soundID);
	PutWord(buf, volume);
	PutWord(buf, location->x);
	PutWord(buf, location->y);
	return PutWord(buf, (uint16_t)location->z);
}

/*
 * 0x0049B71B
 */
uint16_t
PacketManager_MakePacket_GAMETIME(uint8_t *buf)
{
	PutPacketType(buf, PacketType_GAMETIME, 4);
	PutByte(buf, g_TimeManager.hour);
	PutByte(buf, g_TimeManager.minute);
	return PutByte(buf, g_TimeManager.seconds);
}

/*
 * 0x0049B77E - Stub_Return0
 *
 * Returns 0 unconditionally. Called from
 * PacketManager::MakePacket_GAMECENTERDETAIL where the result is
 * unused.
 */
int
Stub_Return0(void *self)
{
	USED(self);
	return 0;
}

/*
 * 0x0049B835
 * Packet 0x65 (WEATHERCHANGE) - 4 bytes
 */
uint16_t
PacketManager_MakePacket_WEATHERCHANGE(uint8_t *buf, uint8_t weatherType, uint8_t numEffects, uint8_t temperature)
{
	PutPacketType(buf, PacketType_WEATHERCHANGE, 4);
	PutByte(buf, weatherType);
	PutByte(buf, numEffects);
	return PutByte(buf, temperature);
}

/*
 * 0x0049B785 - PacketManager::MakePacket_SERVERSTATUS
 *
 * Builds packet 0x63 (SERVERSTATUS), size 0xD: online player count,
 * players near a map border, a zero word, the NPC count, and a final
 * word taken from a stack struct that Stub_Return0 never writes.
 *
 * MODIFIED: that last word is read from uninitialised stack in the
 * binary. It is zeroed here so the build stays free of
 * -Wuninitialized; nothing calls this.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_SERVERSTATUS(uint8_t *buf)
{
	uint16_t detail[8];

	PutPacketType(buf, PacketType_SERVERSTATUS, 0xD);
	PutWord(buf, (uint16_t)CPlayerList_GetCount());
	PutWord(buf, (uint16_t)CPlayerList_CountNearBorder());
	PutWord(buf, 0);
	PutWord(buf, (uint16_t)g_NPCCount);
	memset(detail, 0, sizeof(detail));
	Stub_Return0(detail);
	PutWord(buf, detail[6]);
}

/*
 * 0x0049B87A - PacketManager::MakePacket_FRIENDS
 *
 * Builds a FRIENDS (0x69) packet for the given subcommand: 0 writes a
 * bare zero, 1 lists the allow-list, 2 lists the friend list, and 3
 * writes one byte per friend saying whether that friend allows this
 * player. Names come from the vtable, and an unresolvable serial writes
 * a single NUL from a zeroed scratch global instead - a different one
 * per subcommand, both permanently empty.
 *
 * The counts are written as bytes but compared as ints, so a list
 * longer than 255 writes a truncated count and then overruns it.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_FRIENDS(uint8_t *buf, CPlayer *player, uint8_t subcommand)
{
	CPlayer *friend;
	char *name;
	uint8_t allowed;
	int i;

	PutPacketType(buf, PacketType_FRIENDS, PacketDynamicSize);
	PutByte(buf, subcommand);

	if (subcommand == 0) {
		PutByte(buf, 0);
	} else if (subcommand == 1) {
		PutByte(buf, (uint8_t)player->friendAllowCount);
		for (i = 0; i < (int)player->friendAllowCount; i++) {
			friend = CPlayerList_FindBySerial(player->friendAllowList[i]);
			if (friend == NULL) {
				// 0x006DA940 - zeroed scratch, so one NUL byte
				PutString(buf, "", 1);
			} else {
				name = ((char *(*)(void *))VT_FN((CItem *)friend, VT_GET_NAME))(friend);
				PutString(buf, ((char *(*)(void *))VT_FN((CItem *)friend, VT_GET_NAME))(friend), (int)strlen(name) + 1);
			}
			PutDWord(buf, player->friendAllowList[i]);
		}
	} else if (subcommand == 2) {
		PutByte(buf, (uint8_t)player->friendCount);
		for (i = 0; i < (int)player->friendCount; i++) {
			friend = CPlayerList_FindBySerial(player->friendList[i]);
			if (friend == NULL) {
				// 0x006DA944 - the second zeroed scratch
				PutString(buf, "", 1);
			} else {
				name = ((char *(*)(void *))VT_FN((CItem *)friend, VT_GET_NAME))(friend);
				PutString(buf, ((char *(*)(void *))VT_FN((CItem *)friend, VT_GET_NAME))(friend), (int)strlen(name) + 1);
			}
			PutDWord(buf, player->friendList[i]);
		}
	} else if (subcommand == 3) {
		PutByte(buf, (uint8_t)player->friendCount);
		for (i = 0; i < (int)player->friendCount; i++) {
			friend = CPlayerList_FindBySerial(player->friendList[i]);
			if (friend == NULL) {
				PutByte(buf, 0);
			} else {
				allowed = (uint8_t)CPlayer_IsFriendAllowed(friend, player);
				PutByte(buf, allowed);
			}
		}
	}
}

/*
 * 0x0049BB16 - PacketManager::MakePacket_FRIENDNOTIFY
 *
 * Builds a FRIENDNOTIFY (0x6A) packet (3 bytes) carrying friend index
 * and status.
 */
void
PacketManager_MakePacket_FRIENDNOTIFY(uint8_t *buf, int friendIndex, int status)
{
	PutPacketType(buf, PacketType_FRIENDNOTIFY, 3);
	PutByte(buf, (uint8_t)friendIndex);
	PutByte(buf, (uint8_t)status);
}

/*
 * 0x0049BB4B
 * Packet 0x6C (TARGET) - 19 bytes
 * Outbound only: server sends to client to request a target cursor.
 * The client's response (also packet 0x6C) is discarded by UoDemo.exe.
 */
uint16_t
PacketManager_MakePacket_TARGET(uint8_t *buf, uint8_t targetType, uint32_t cursorID, uint8_t cursorType)
{
	PutPacketType(buf, 0x6C, 19);
	PutByte(buf, targetType); // 0 = object, 1 = location
	PutDWord(buf, cursorID);  // cursor/target request ID
	PutByte(buf, cursorType); // cursor graphic type
	PutDWord(buf, 0);         // target serial (0 = not set)
	PutWord(buf, 0);          // target X
	PutWord(buf, 0);          // target Y
	PutWord(buf, 0);          // target Z
	return PutWord(buf, 0);   // model ID
}

/*
 * 0x0049BBD6
 * Packet 0x99 (TARGET_MULTI) - 26 bytes
 */
uint16_t
PacketManager_MakePacket_TARGET_MULTI(uint8_t *buf, uint8_t allowGround, uint32_t deedSerial, uint16_t x, uint16_t y, uint16_t z, uint16_t facing)
{
	PutPacketType(buf, PacketType_TARGET_MULTI, 0x1A);
	PutByte(buf, allowGround);
	PutDWord(buf, deedSerial);
	PutDWord(buf, 0);
	PutWord(buf, 0);
	PutWord(buf, 0);
	PutWord(buf, 0);
	PutWord(buf, 0);
	PutWord(buf, x);
	PutWord(buf, y);
	PutWord(buf, z);
	return PutWord(buf, facing);
}

/*
 * 0x0049BC98
 * Packet 0xB4 (TARGET_OBJLIST) - variable size
 *
 * Like TARGET_MULTI but includes a list of allowed object type IDs
 * for the client to filter against. Each CList node's value
 * is a uint16_t type ID.
 */
uint16_t
PacketManager_MakePacket_TARGET_OBJLIST(uint8_t *buf, uint8_t allowGround, uint32_t cursorId, uint16_t multiId, uint16_t xOff, uint16_t yOff, CList *list)
{
	CListNode *node;

	PutPacketType(buf, PacketType_TARGET_OBJLIST, 0x2010);
	PutByte(buf, allowGround);
	PutDWord(buf, cursorId);
	PutWord(buf, multiId);
	PutWord(buf, xOff);
	PutWord(buf, yOff);
	PutWord(buf, (uint16_t)(list->count / 5));

	node = list->head;
	while (node != NULL) {
		PutWord(buf, (uint16_t)node->value);
		node = node->next;
	}
	return 0;
}

/*
 * 0x0049BD52
 */
uint16_t
PacketManager_MakePacket_MUSIC(uint8_t *buf, uint16_t musicId)
{
	PutPacketType(buf, PacketType_MUSIC, 3);
	return PutWord(buf, musicId);
}

/*
 * 0x0049BD78
 * Packet 0x6E (ANIM) - 14 bytes
 */
uint16_t
PacketManager_MakePacket_ANIM(uint8_t *buf, uint32_t serial, uint16_t action, uint16_t frameCount, uint16_t repeatCount, uint8_t backward, uint8_t repeat, uint8_t delay)
{
	PutPacketType(buf, PacketType_ANIM, 14);
	PutDWord(buf, serial);
	PutWord(buf, action);
	PutWord(buf, frameCount);
	PutWord(buf, repeatCount);
	PutByte(buf, backward);
	PutByte(buf, repeat);
	return PutByte(buf, delay);
}

/*
 * 0x0049BEBF
 * Packet 0x70 (EFFECT) - 28 bytes
 */
uint16_t
PacketManager_MakePacket_EFFECT(uint8_t *buf, uint8_t type, uint32_t srcSerial, uint32_t dstSerial, uint16_t itemID, uint16_t srcX, uint16_t srcY, uint8_t srcZ, uint16_t dstX,
        uint16_t dstY, uint8_t dstZ, uint8_t speed, uint8_t duration, uint8_t unk1, uint8_t fixedDir, uint8_t explode, uint8_t unk2)
{
	PutPacketType(buf, PacketType_EFFECT, 28);
	PutByte(buf, type);
	PutDWord(buf, srcSerial);
	PutDWord(buf, dstSerial);
	PutWord(buf, itemID);
	PutWord(buf, srcX);
	PutWord(buf, srcY);
	PutByte(buf, srcZ);
	PutWord(buf, dstX);
	PutWord(buf, dstY);
	PutByte(buf, dstZ);
	PutByte(buf, speed);
	PutByte(buf, duration);
	PutByte(buf, unk1);
	PutByte(buf, fixedDir);
	PutByte(buf, explode);
	return PutByte(buf, unk2);
}

/*
 * 0x0049BFFE
 */
uint16_t
PacketManager_MakePacket_COMBAT(uint8_t *buf, uint8_t warMode, uint8_t combatByte2, uint8_t combatByte3, uint8_t combatByte4)
{
	PutPacketType(buf, PacketType_COMBAT, 5);
	PutByte(buf, warMode);
	PutByte(buf, combatByte2);
	PutByte(buf, combatByte3);
	return PutByte(buf, combatByte4);
}

/*
 * 0x0049BE00 - PacketManager::MakePacket_TRADE
 *
 * Builds a TRADE packet (0x6F) into buf. Args: buf, subtype, serial,
 * param1, param2, name. If name has strlen > 0, writes hasName=1 followed
 * by 30-byte padded name string; otherwise writes hasName=0.
 */
void
PacketManager_MakePacket_TRADE(uint8_t *buf, uint8_t subtype, uint32_t serial, uint32_t param1, uint32_t param2, const char *name)
{
	uint8_t temp[0x1E];

	PutPacketType(buf, PacketType_TRADE, 0x8000);
	PutByte(buf, subtype);
	PutDWord(buf, serial);
	PutDWord(buf, param1);
	PutDWord(buf, param2);

	if (strlen(name) > 0) {
		PutByte(buf, 1);
		memset(temp, 0, 0x1E);
		strncpy((char *)temp, name, 0x1E);
		PutString(buf, (char *)temp, 0x1E);
	} else {
		PutByte(buf, 0);
	}
}

/*
 * 0x0049C053 - PacketManager::MakePacket_SHOP_DATA
 *
 * Builds a SHOP_DATA (0x74) OpenBuyWindow packet listing the vendor's
 * stock with prices. filterFlag=1 skips items with no resource ratio
 * (equipment[26] stock); filterFlag=0 includes all (equipment[27]
 * offered). Item count is capped at 250.
 */
void
PacketManager_MakePacket_SHOP_DATA(uint8_t *buf, CMobile *vendor, CContainer *container, int markupPercent, int filterFlag)
{
	CItem *item;
	uint8_t totalCount;
	uint8_t filteredCount;
	int i;
	int price;
	char *name;
	uint8_t nameLen;

	PutPacketType(buf, PacketType_SHOP_DATA, PacketDynamicSize);
	PutDWord(buf, container->item.serial);

	// First loop: count items. Binary maintains two byte counters.
	totalCount = 0;
	filteredCount = totalCount;
	item = container->contents;
	while (item != NULL) {
		if (filterFlag == 0 || Vendor_GetItemPrice(item) > 0)
			filteredCount++;
		totalCount++;
		item = item->spatialNext;
	}

	// Cap filteredCount to 250
	if ((filteredCount & 0xff) > 250)
		filteredCount = 250;
	PutByte(buf, filteredCount);

	// Second loop: iterate totalCount times, writing filtered items.
	item = container->contents;
	for (i = 0; i < (totalCount & 0xff); i++) {
		// Skip items filtered out by resource ratio check
		if (filterFlag != 0 && Vendor_GetItemPrice(item) == 0) {
			item = item->spatialNext;
			continue;
		}

		// Price: CShopkeeper_GetBuyPrice * markupPercent / 100
		price = (int)CShopkeeper_GetBuyPrice(vendor, item) * markupPercent / 100;
		if (price < 0)
			price = 1;
		PutDWord(buf, (uint32_t)price);

		name = ((char *(*)(void *, int))VT_FN(item, VT_SPEAK_SYS_MSG))(item, 0);
		name[0] = toupper(name[0]);

		// nameLen = (byte)(strlen(name) + 1), cap at 100
		nameLen = (uint8_t)(strlen(name) + 1);
		if ((nameLen & 0xff) > 99)
			nameLen = 100;
		PutByte(buf, nameLen);
		PutString(buf, name, nameLen & 0xff);

		item = item->spatialNext;
	}
}

/*
 * 0x0049C1FD - PacketManager::MakePacket_SHOP_SELL
 *
 * Builds a SHOP_SELL (0x9E) SellList packet from sellEntries produced
 * by Shopkeeper_ScanBackpack.
 */
void
PacketManager_MakePacket_SHOP_SELL(uint8_t *buf, CMobile *vendor, uint16_t count, CVendorSellEntry *sellEntries)
{
	int i;

	PutPacketType(buf, PacketType_SHOP_SELL, PacketDynamicSize);
	PutDWord(buf, vendor->container.item.serial);
	PutWord(buf, count);

	for (i = 0; i < (count & 0xffff); i++) {
		PutDWord(buf, sellEntries[i].serial);
		PutWord(buf, sellEntries[i].bodyType);
		PutWord(buf, sellEntries[i].color);
		PutWord(buf, sellEntries[i].amount);
		PutWord(buf, sellEntries[i].price);
		PutWord(buf, strlen(sellEntries[i].name));
		PutString(buf, sellEntries[i].name, strlen(sellEntries[i].name));
	}
}

/*
 * 0x0049C341
 * Packet 0x7B (SEQUENCE) - 2 bytes
 */
uint16_t
PacketManager_MakePacket_SEQUENCE(uint8_t *buf, uint8_t sequence)
{
	PutPacketType(buf, PacketType_SEQUENCE, 2);
	return PutByte(buf, sequence);
}

/*
 * 0x0049C366 - ObjPickerEntryTextLen (static helper)
 *
 * Returns the display text length for an OBJPICKER entry. If the text
 * pointer (at entry offset +4) is NULL, returns 0. Otherwise returns
 * strlen capped at 255, with a minimum of 1 for non-null strings.
 */
static int
ObjPickerEntryTextLen(ObjPickerEntry *entry)
{
	char *text;
	int len;

	text = entry->name;
	if (text == NULL)
		return 0;
	len = strlen(text);
	if (len > 255)
		len = 255;
	if (len == 0)
		return 1;
	return len;
}

/*
 * 0x0049C3B2 - PacketManager::MakePacket_OBJPICKER
 *
 * Builds an OBJPICKER (0x7C) packet: a client dialog listing object
 * types (typeId, hue, optional label) for the player to pick from.
 */
void
PacketManager_MakePacket_OBJPICKER(uint8_t *buf, uint32_t serial, uint16_t gumpId, char *title, uint8_t count, ObjPickerEntry *entries)
{
	int size;
	int titleLen;
	uint8_t i;
	int textLen;

	// Compute total packet size
	titleLen = strlen(title);
	size = titleLen + 0x0B; // header: type(1) + size(2) + serial(4) + gumpId(2) + titleLen(1) + count(1)
	for (i = 0; i < count; i++)
		size += ObjPickerEntryTextLen(&entries[i]) + 5; // typeId(2) + hue(2) + textLen(1)

	PutPacketType(buf, 0x7C, size);
	PutDWord(buf, serial);
	PutWord(buf, gumpId);
	PutByte(buf, (uint8_t)titleLen);
	PutString(buf, title, titleLen);
	PutByte(buf, count);

	for (i = 0; i < count; i++) {
		PutWord(buf, entries[i].typeId);
		PutWord(buf, entries[i].hue);
		textLen = ObjPickerEntryTextLen(&entries[i]);
		PutByte(buf, (uint8_t)textLen);
		PutString(buf, entries[i].name, textLen);
	}
}

/*
 * 0x0049C540 - PacketManager::MakePacket_HUEPICKER
 *
 * Builds a Hue Picker packet (0x95, 9 bytes). The client opens a
 * color selection dialog allowing the player to pick a hue.
 */
uint16_t
PacketManager_MakePacket_HUEPICKER(uint8_t *buf, uint32_t serial, uint16_t typeID, uint16_t hue)
{
	PutPacketType(buf, 0x95, 9);
	PutDWord(buf, serial);
	PutWord(buf, typeID);
	return PutWord(buf, hue);
}

/*
 * 0x0049C58A
 * Packet 0x88 (OPEN_PAPERDOLL) - 66 bytes
 */
uint16_t
PacketManager_MakePacket_OPEN_PAPERDOLL(uint8_t *buf, uint32_t serial, char *title, uint8_t flags)
{
	PutPacketType(buf, PacketType_OPEN_PAPERDOLL, 0x42);
	PutDWord(buf, serial);
	PutString(buf, title, 0x3C);
	return PutByte(buf, flags);
}

/*
 * 0x0049C5D4 - PacketManager::MakePacket_DISPLAY_SIGN
 *
 * Builds a DISPLAY_SIGN (0x8B) packet with serial, gumpID, and
 * length-prefixed title and body strings.
 */
uint16_t
PacketManager_MakePacket_DISPLAY_SIGN(uint8_t *buf, uint32_t serial, uint16_t gumpID, char *title, char *body)
{
	PutPacketType(buf, PacketType_DISPLAY_SIGN, 0x800);
	PutDWord(buf, serial);
	PutWord(buf, gumpID);
	PutWord(buf, (uint16_t)(strlen(title) + 1));
	PutString(buf, title, strlen(title) + 1);
	PutWord(buf, (uint16_t)(strlen(body) + 1));
	return PutString(buf, body, strlen(body) + 1);
}

/*
 * 0x0049C688
 * Packet 0x97 (PLAYERMOVE) - 2 bytes
 */
uint16_t
PacketManager_MakePacket_PLAYERMOVE(uint8_t *buf, uint8_t direction)
{
	PutPacketType(buf, PacketType_PLAYERMOVE, 2);
	return PutByte(buf, direction);
}

/*
 * 0x0049C6B0 - PacketManager::MakePacket_MOBNAME
 *
 * Builds a MOBNAME (0x98) packet with the name of the mobile at the
 * given serial (empty string if not a mobile or out of range).
 */
uint16_t
PacketManager_MakePacket_MOBNAME(uint8_t *buf, CPlayer *viewer, uint32_t serial)
{
	char nameBuf[0x1E];
	CItem *entity;

	memset(nameBuf, 0, sizeof(nameBuf));

	PutPacketType(buf, PacketType_MOBNAME, 0x23);
	PutDWord(buf, serial);

	entity = CWorld_FindEntityInRange(g_World, &viewer->mobile.container.item.resourceEntity.entity, serial, 18);
	if (entity != NULL) {
		if (VT_IsMobile(entity)) {
			char *name = ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity);
			strcpy(nameBuf, name);
		}
	}

	return PutString(buf, nameBuf, 0x1E);
}

/*
 * 0x0049C749 - PacketManager::MakePacket_TEXT_ENTRY
 *
 * Builds a TEXT_ENTRY (0x9A) packet (max size 0x0001000E).
 */
void
PacketManager_MakePacket_TEXT_ENTRY(uint8_t *buf, uint32_t serial, uint32_t gumpId, uint32_t parentId, char *text)
{
	PutPacketType(buf, 0x9A, 0x1000E);
	PutDWord(buf, serial);
	PutDWord(buf, gumpId);
	PutDWord(buf, parentId);
	PutString(buf, text, strlen(text) + 1);
}

/*
 * 0x0049C7B4 - PacketManager::MakePacket_WEB_BROWSE
 *
 * Builds a WEB_BROWSE (0xA5) packet carrying a URL to open.
 */
uint16_t
PacketManager_MakePacket_WEB_BROWSE(uint8_t *buf, char *url)
{
	int len;

	PutPacketType(buf, PacketType_WEB_BROWSE, 0x10002);
	len = strlen(url) + 1;
	return PutString(buf, url, len);
}

/*
 * 0x0049C7EF - PacketManager::MakePacket_TIP
 *
 * Builds a TIP (0xA6) packet. Kind 0 sends the caller's id and the
 * named resource; kind 1 reads update.txt, takes its first line as the
 * id and sends the rest, or "MISSING UPDATE" with id 0 when the file is
 * absent. Any other kind sends an empty body.
 *
 * Both paths flatten the text the same way: each line is copied with
 * carriage returns dropped, and a non-empty line is appended followed
 * by a space, an empty one by a lone carriage return. The 0xFF byte is
 * treated as a terminator everywhere, and the loop that advances past
 * it re-tests it rather than advancing, so a 0xFF in the middle of a
 * line ends the whole walk.
 */
static __attribute__((unused)) void
PacketManager_MakePacket_TIP(uint8_t *buf, uint8_t kind, uint32_t id, char *resourceName)
{
	char text[0x2000];
	char line[0x2000];
	char crlf[2];
	char *src;
	char *dst;
	int parsedId;
	uint16_t textLen;

	PutPacketType(buf, 0xA6, PacketDynamicSize);
	PutByte(buf, kind);

	// 0x006DA948 - a zeroed scratch global, so this clears text
	strcpy(text, "");

	crlf[0] = '\r';
	crlf[1] = '\0';

	if (kind == 0) {
		PutDWord(buf, id);

		src = NamedResource_Find(resourceName);
		if (src != NULL) {
			while (*src != '\0' && *src != -1) {
				dst = line;
				while (*src != '\0' && *src != -1 && *src != '\n') {
					if (*src != '\r' && *src != -1)
						*dst++ = *src;
					if (*src != -1)
						src++;
				}
				*dst = '\0';

				if (strlen(line) != 0) {
					strcat(text, line);
					strcat(text, " ");
				} else {
					strcat(text, crlf);
				}

				if (*src != '\0' && *src != -1)
					src++;
			}
		}
	} else if (kind == 1) {
		src = NamedResource_Find("update.txt");
		if (src == NULL) {
			PutDWord(buf, 0);
			strcpy(text, "MISSING UPDATE");
		} else {
			dst = line;
			while (*src != '\0' && *src != -1 && *src != '\n') {
				if (*src != '\r' && *src != -1)
					*dst++ = *src;
				if (*src != -1)
					src++;
			}
			*dst = '\0';

			sscanf(line, "%d", &parsedId);
			PutDWord(buf, (uint32_t)parsedId);

			while (*src != '\0' && *src != -1) {
				dst = line;
				while (*src != '\0' && *src != -1 && *src != '\n') {
					if (*src != '\r' && *src != -1)
						*dst++ = *src;
					if (*src != -1)
						src++;
				}
				*dst = '\0';

				if (strlen(line) != 0) {
					strcat(text, line);
					strcat(text, " ");
				} else {
					strcat(text, crlf);
				}

				if (*src != '\0' && *src != -1)
					src++;
			}
		}
	}

	textLen = (uint16_t)strlen(text);
	PutWord(buf, textLen);
	PutString(buf, text, textLen);
}

/*
 * 0x0049CC7D - PacketManager::MakePacket_CURRENT_TARGET
 *
 * Builds packet 0xAA (CURRENT_TARGET): 5 bytes.
 */
uint16_t
PacketManager_MakePacket_CURRENT_TARGET(uint8_t *buf, uint32_t serial)
{
	PutPacketType(buf, PacketType_CURRENT_TARGET, 5);
	return PutDWord(buf, serial);
}

/*
 * 0x0049CCA5 - PacketManager::MakePacket_RequestAssistance
 *
 * Builds a RequestAssistance (0x9C) packet (309 bytes) carrying
 * type/subtype, serial, and name/title/description.
 */
void
PacketManager_MakePacket_RequestAssistance(uint8_t *buf, int type, int subtype, uint32_t serial, const char *name, const char *title, const char *description)
{
	char tmp[256];

	PutPacketType(buf, PacketType_RequestAssistance, 0x135);
	PutByte(buf, (uint8_t)type);
	PutByte(buf, (uint8_t)subtype);
	PutDWord(buf, serial);
	strncpy(tmp, name, 0x1F);
	PutString(buf, tmp, 0x1F);
	strncpy(tmp, title, 0x0F);
	PutString(buf, tmp, 0x0F);
	strncpy(tmp, description, 0x100);
	PutString(buf, tmp, 0x100);
}

/*
 * 0x0049CD7C - PacketManager::MakePacket_GUMP_GENERIC
 *
 * Builds a generic gump packet (0xB0). Serializes entity serial, gump ID,
 * position, command layout strings, and text lines (as UTF-16BE).
 * cmdList and textList are CList linked lists.
 */
void
PacketManager_MakePacket_GUMP_GENERIC(uint8_t *buf, uint32_t serial, int gumpId, int x, int y, CList *cmdList, CList *textList)
{
	char cmdBuf[0x4000];
	char textBuf[0x4000]; // binary: dead code, zeroed but never used
	CListNode *iter;
	int cmdLen;
	int charCount;
	int j;
	int textCount; // binary: dead code, counts textList iterations but never read

	PutPacketType(buf, 0xB0, 0x10013);
	PutDWord(buf, serial);
	PutDWord(buf, gumpId);
	PutDWord(buf, x);
	PutDWord(buf, y);

	// Concatenate all string entries from cmdList into cmdBuf
	memset(cmdBuf, 0, sizeof(cmdBuf));
	iter = cmdList->head;
	while (iter != NULL) {
		if (iter->typeTag == WTYPE_STRING)
			strcat(cmdBuf, CString_GetBuffer((void *)(uintptr_t)iter->value));
		iter = iter->next;
	}
	cmdLen = (int)strlen(cmdBuf) + 1;
	PutWord(buf, (uint16_t)cmdLen);
	PutString(buf, cmdBuf, cmdLen);

	// dead code: zeroed but never used
	memset(textBuf, 0, sizeof(textBuf));
	USED(textBuf);

	// Write text lines
	PutWord(buf, (uint16_t)textList->count);
	textCount = 0;
	iter = textList->head;
	while (iter != NULL) {
		textCount++;
		if (iter->typeTag == WTYPE_USTRING) {
			// UTF-16 array (CIntArray)
			charCount = CString_GetLength((void *)(uintptr_t)iter->value);
			PutWord(buf, (uint16_t)charCount);
			for (j = 0; j < charCount; j++)
				PutWord(buf, *CIntArray_At((CIntArray *)(uintptr_t)iter->value, j));
		} else if (iter->typeTag == WTYPE_STRING) {
			// ASCII string, widen each char to uint16
			charCount = CString_GetLength((void *)(uintptr_t)iter->value);
			PutWord(buf, (uint16_t)charCount);
			for (j = 0; j < charCount; j++) {
				char *ch = CString_CharAt((void *)(uintptr_t)iter->value, j);
				PutWord(buf, (int16_t)(signed char)*ch);
			}
		} else {
			// Unknown type: write 0 chars
			PutWord(buf, 0);
		}
		iter = iter->next;
	}
	USED(textCount);
}

/*
 * 0x0049D026 - PlaySoundAtEntity
 *
 * Broadcasts a SOUND packet at the entity's location to clients within
 * range 0x12.
 */
void
PlaySoundAtEntity(CItem *entity, uint16_t soundID, uint16_t volume)
{
	CLocation loc;
	uint8_t buf[20];

	CLocation_Init(&loc);
	CLocation *entLoc = ((CLocation * (*)(void *)) VT_FN(entity, VT_GET_LOCATION))(entity);
	CLocation_SetLoc(&loc, entLoc);
	PacketManager_MakePacket_SOUND(buf, 1, soundID, &loc, volume);
	SendPacketInRange(buf, &loc, 0x12);
}

/*
 * 0x0049D07F - PlaySoundAtLocation
 *
 * Broadcasts a SOUND packet at the given location to clients within
 * range 0x28.
 */
void
PlaySoundAtLocation(CLocation *loc, uint16_t soundID, uint16_t volume)
{
	uint8_t buf[12];

	PacketManager_MakePacket_SOUND(buf, 1, soundID, loc, volume);
	BroadcastToNearby(buf, loc, 0x28);
}

/*
 * 0x0049D0B7 - SendSoundToEntity
 *
 * Sends a SOUND packet (mode 1, positional) at the entity's location
 * to the entity's client only.
 */
void
SendSoundToEntity(CItem *entity, int soundId, int volume)
{
	uint8_t sbuf[12];

	PacketManager_MakePacket_SOUND(sbuf, 1, (uint16_t)soundId, &entity->resourceEntity.entity.location, (uint16_t)volume);

	SendToClient(entity, sbuf, -1);
}

/*
 * 0x0049D0F2 - SendSoundAtEntity
 *
 * Like SendSoundToEntity but uses SOUND mode 0 (fixed location).
 */
void
SendSoundAtEntity(CItem *entity, int soundId, int volume)
{
	uint8_t sbuf[12];

	PacketManager_MakePacket_SOUND(sbuf, 0, (uint16_t)soundId, &entity->resourceEntity.entity.location, (uint16_t)volume);

	SendToClient(entity, sbuf, -1);
}

/*
 * 0x0049D130 - PlayMusicToNearby
 *
 * Broadcasts a MUSIC packet at the entity's location to clients within
 * range 0x12. Dead code in the binary (no callers).
 */
void
PlayMusicToNearby(CItem *entity, uint16_t musicId)
{
	CLocation loc;
	uint8_t buf[4];

	CLocation_SetLoc(&loc, ((CLocation * (*)(void *)) VT_FN(entity, VT_GET_LOCATION))(entity));
	PacketManager_MakePacket_MUSIC(buf, musicId);
	SendPacketInRange(buf, &loc, 0x12);
}

/*
 * 0x0049D173 - PlayMusicToEntity
 *
 * Sends a MUSIC packet to the entity's client.
 */
void
PlayMusicToEntity(CItem *entity, uint16_t musicId)
{
	uint8_t buf[4];

	PacketManager_MakePacket_MUSIC(buf, musicId);
	SendToClient(entity, buf, -1);
}

/*
 * 0x0049E750 - InterpolateBetween
 *
 * Maps t onto the x1..x2 range with PathNode_Interpolate, then lerps
 * between lo and hi by the fraction that comes back.
 */
static __attribute__((unused)) double
InterpolateBetween(double x1, double x2, double lo, double hi, double t)
{
	return (hi - lo) * PathNode_Interpolate(x1, x2, t) + lo;
}

/*
 * 0x0049E78C - RoundToNearest
 *
 * Truncates value toward zero via the CRT's __ftol, then adds one when
 * the discarded fraction is 0.5 or more. Rounds half away from zero for
 * positive input and toward zero for negative input.
 */
static __attribute__((unused)) int
RoundToNearest(double value)
{
	int truncated;

	truncated = (int)value;
	if (value - (double)truncated >= 0.5)
		truncated++;
	return truncated;
}

/*
 * 0x0049E7D0 - SendText_Orphan49E7D0
 *
 * Builds a TEXT (0x1C) packet with NULL entity (sent with serial
 * 0xFFFFFFFF, bodyType 0xFFFF, empty name), speechType 6, hue 0x3B2,
 * font 0, carrying text. Sends the packet to the given entity's
 * client.
 *
 * ORPHANED: zero callers in the binary. A disused system-text helper
 * compiled but never wired up.
 */
static __attribute__((unused)) void
SendText_Orphan49E7D0(CItem *entity, const char *text)
{
	uint8_t buf[0x42C];

	PacketManager_MakePacket_TEXT(buf, NULL, entity, 6, text, 0x3B2, 0);
	SendToClient(entity, buf, -1);
}

/*
 * 0x004DAC38 - PacketManager::MakePacket_TEXT_UNICODE
 *
 * Builds a TEXT_UNICODE (0xAE) packet for server-to-client unicode
 * speech. GM players get a "GM " name prefix; non-mobiles send with
 * serial 0xFFFFFFFF and bodyType 0xFFFF. The second entity argument is
 * unused (callers pass the speaker twice).
 */
void
PacketManager_MakePacket_TEXT_UNICODE(uint8_t *buf, CItem *entity, CItem *entity2, uint8_t speechType, uint16_t *text, uint16_t hue, uint16_t font, uint32_t lang)
{
	uint32_t serial;
	uint16_t bodyType;
	char nameBuf[35];
	int textLen;
	int i;

	USED(entity2);

	PutPacketType(buf, PacketType_TEXT_UNICODE, 0x830);

	// Extract serial from entity+0x40
	if (entity != NULL)
		serial = entity->serial;
	else
		serial = 0xFFFFFFFF;

	// Extract bodyType from entity+0x04
	if (entity != NULL)
		bodyType = entity->resourceEntity.entity.bodyType;
	else
		bodyType = 0xFFFF;

	PutDWord(buf, serial);
	PutWord(buf, bodyType);
	PutByte(buf, speechType);
	PutWord(buf, hue);
	PutWord(buf, font);
	PutDWord(buf, lang);

	// Build name buffer (binary zeroes 35 bytes, writes 30 to packet)
	memset(nameBuf, 0, sizeof(nameBuf));

	if (entity != NULL && VT_IsPlayer(entity)) {
		if (CPlayer_IsGMAndManifested((CPlayer *)entity)) {
			sprintf(nameBuf, "GM %s", ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity));
		} else {
			strcpy(nameBuf, ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity));
		}
	} else if (entity != NULL && VT_IsNPC(entity)) {
		// vtable[0x34] GetName, strcpy
		strcpy(nameBuf, ((char *(*)(void *))VT_FN(entity, VT_GET_NAME))(entity));
	}
	PutString(buf, nameBuf, 30);

	// Write UTF-16 text (length + null terminator)
	textLen = UString_Length(text) + 1;
	for (i = 0; i < textLen; i++)
		PutWord(buf, text[i]);
}

/*
 * Custom - PacketManager_MakePacket_SKILLS_Ext
 *
 * Extended skill packet for 1.26.2+ clients. Type 0x00 (full list).
 * 7 bytes per entry: skillID(2) + value(2) + base(2) + lock(1).
 * Client 2.0.0 stores values for type 0x00; type 0x02 discards them.
 * SkillIDs are 1-based. Terminated by skillID 0.
 */
void
PacketManager_MakePacket_SKILLS_Ext(uint8_t *buf, int maxSkills, CItem *player)
{
	int i;
	CPlayer *p = (CPlayer *)player;

	PutPacketType(buf, PacketType_SKILLS, 0x0404);
	PutByte(buf, 0x00);

	for (i = 0; i < (maxSkills & 0xFFFF); i++) {
		uint16_t val = (uint16_t)CMobile_GetSkillValue((CMobile *)player, (int8_t)i, 0);
		uint16_t base = (uint16_t)CMobile_GetTotalSkill((CMobile *)player, (int8_t)i);
		PutWord(buf, (uint16_t)(i + 1));
		PutWord(buf, val);
		PutWord(buf, base);
		PutByte(buf, feat(FEAT_SKILL_LOCK) ? p->skillLocks[i] : 0);
	}
	PutWord(buf, 0x0000);
}

/*
 * Custom - PacketManager_MakePacket_SKILLS_SINGLE_Ext
 *
 * Extended single skill update for 1.26.2+ clients. Type 0xFF (single update).
 * 7 bytes: skillID(2) + value(2) + base(2) + lock(1).
 * Client 2.0.0: skillID is 0-based for type 0xFF.
 */
void
PacketManager_MakePacket_SKILLS_SINGLE_Ext(uint8_t *buf, uint16_t skillID, uint16_t value, uint16_t base, uint8_t lock)
{
	PutPacketType(buf, PacketType_SKILLS, 0x0D);
	PutByte(buf, 0xFF);
	PutWord(buf, skillID);
	PutWord(buf, value);
	PutWord(buf, base);
	PutByte(buf, lock);
}

/*
 * Custom - PacketManager_MakePacket_ACCT_LOGIN_FAIL
 *
 * Sends 0x82 on the login connection to reject account credentials.
 * Reason codes: 0x00=Invalid, 0x02=Blocked, 0x03=BadPass, 0xFF=BadComm.
 */
uint16_t
PacketManager_MakePacket_ACCT_LOGIN_FAIL(uint8_t *buf, uint8_t reason)
{
	PutPacketType(buf, PacketType_ACCT_LOGIN_FAIL, 2);
	return PutByte(buf, reason);
}

/*
 * Custom - PacketManager::MakePacket_CHAT_MSG
 *
 * Builds a CHAT_MSG (0xB2) server-to-client packet. The wire layout mirrors
 * the 1.25.37 client's PacketManager::HandlePacket(PDSTRUCT_CHAT_MSG) parser
 * at client address 0x0807e378: a 16-bit command/message number, a 4-byte
 * language field (zero from the server), then two big-endian UTF-16
 * NUL-terminated parameter strings. UoDemo.exe has no chat packet builder.
 */
void
PacketManager_MakePacket_CHAT_MSG(uint8_t *buf, uint16_t number, const char *param1, const char *param2)
{
	PutPacketType(buf, PacketType_CHAT_MSG, 0x1000);
	PutWord(buf, number);
	PutDWord(buf, 0);
	PutUnicodeBE(buf, param1 != NULL ? param1 : "");
	PutUnicodeBE(buf, param2 != NULL ? param2 : "");
}
