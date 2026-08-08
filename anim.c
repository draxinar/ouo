/*
 * Animation queue - time-ordered animation and sound effect dispatch.
 *
 * Queues AnimEffect nodes keyed on a future tick and, on expiry, fans
 * the effect out to every client within range via the appropriate
 * packet builder.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "anim.h"
#include "dat.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "wombat.h"

static void AnimSequence_Destructor(AnimSequence *this); // 0x004CF67B
static AnimSequence *AnimSequence_Constructor(AnimSequence *this); // 0x004CF650

/*
 * 0x004CF650 - AnimSequence::AnimSequence
 *
 * Initializes state and the location and command lists to empty.
 */
static __attribute__((unused)) AnimSequence *
AnimSequence_Constructor(AnimSequence *this)
{
	this->state = 0;
	this->locList = NULL;
	this->cmdList = NULL;
	return this;
}
/*
 * 0x004CF67B - AnimSequence::~AnimSequence
 *
 * Delegates to AnimSequence::Clear.
 */
static __attribute__((unused)) void
AnimSequence_Destructor(AnimSequence *this)
{
	USED(this);
	AnimSequence_Clear();
}

/*
 * 0x004CF68E - AnimSequence::Process
 *
 * Plays the queued animation sequence. Broadcasts SEQUENCE(0) to all
 * stored locations, dispatches each cmdList entry to the matching
 * effect/animation/sound helper, then broadcasts SEQUENCE(actionId).
 */
void
AnimSequence_Process(uint8_t actionId)
{
	CLocation loc1, loc2;
	uint8_t seqBuf[4];
	uint8_t effectBuf[28];
	uint8_t animBuf[14];
	SeqCmdNode *cmd;
	uint8_t *data;
	uint32_t serial;
	uint32_t serial2;
	uint32_t itemID;
	uint32_t speed;
	uint32_t duration;
	uint32_t hue;
	uint32_t renderMode;
	uint32_t soundID;
	uint32_t volume;
	CItem *entity;

	CLocation_Init(&loc1);
	CLocation_Init(&loc2);

	PacketManager_MakePacket_SEQUENCE(seqBuf, 0);
	AnimSequence_BroadcastNearby(seqBuf, g_AnimSequence.locList, 0x12);

	cmd = g_AnimSequence.cmdList;
	while (cmd != NULL) {
		data = cmd->data;

		switch (cmd->type) {
		case 0: // doLocAnimation: EFFECT type 2, single broadcast
			memcpy(&itemID, data, 4);
			data += 4;
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&speed, data, 4);
			data += 4;
			memcpy(&duration, data, 4);
			data += 4;
			memcpy(&hue, data, 4);
			data += 4;
			memcpy(&renderMode, data, 4);
			data += 4;
			PacketManager_MakePacket_EFFECT(effectBuf, 2, 0, 0, (uint16_t)itemID, loc1.x, loc1.y, (uint8_t)loc1.z, loc1.x, loc1.y, (uint8_t)loc1.z, (uint8_t)speed,
			        (uint8_t)duration, (uint8_t)hue, (uint8_t)renderMode, 1, 0);
			SendPacketInRange(effectBuf, &loc1, 0x12);
			break;

		case 1: // doMobAnimation: EFFECT type 3, single broadcast
			memcpy(&serial, data, 4);
			data += 4;
			memcpy(&itemID, data, 4);
			data += 4;
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&speed, data, 4);
			data += 4;
			memcpy(&duration, data, 4);
			data += 4;
			memcpy(&hue, data, 4);
			data += 4;
			memcpy(&renderMode, data, 4);
			data += 4;
			PacketManager_MakePacket_EFFECT(effectBuf, 3, serial, 0, (uint16_t)itemID, loc1.x, loc1.y, (uint8_t)loc1.z, loc1.x, loc1.y, (uint8_t)loc1.z, (uint8_t)speed,
			        (uint8_t)duration, (uint8_t)hue, (uint8_t)renderMode, 1, 0);
			SendPacketInRange(effectBuf, &loc1, 0x12);
			break;

		case 2: // doLightning: EFFECT type 1, single broadcast
			memcpy(&serial, data, 4);
			data += 4;
			memcpy(&loc1, data, 6);
			data += 6;
			PacketManager_MakePacket_EFFECT(effectBuf, 1, serial, 0, 0, loc1.x, loc1.y, (uint8_t)loc1.z, loc1.x, loc1.y, (uint8_t)loc1.z, 0, 0, 0, 0, 0, 0);
			SendPacketInRange(effectBuf, &loc1, 0x12);
			break;

		case 3: // doMissile_Loc2Loc: EFFECT type 0, dual broadcast
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&loc2, data, 6);
			data += 6;
			memcpy(&itemID, data, 4);
			data += 4;
			memcpy(&speed, data, 4);
			data += 4;
			memcpy(&duration, data, 4);
			data += 4;
			memcpy(&hue, data, 4);
			data += 4;
			PacketManager_MakePacket_EFFECT(effectBuf, 0, 0, 0, (uint16_t)itemID, loc1.x, loc1.y, (uint8_t)loc1.z, loc2.x, loc2.y, (uint8_t)loc2.z, (uint8_t)speed, 0,
			        0, 0, (uint8_t)duration, (uint8_t)hue);
			CPlayerList_BroadcastToTwoLocs(effectBuf, &loc1, &loc2, 0x12, NULL);
			break;

		case 4: // doMissile_Loc2Mob: EFFECT type 0, dual broadcast
			memcpy(&serial, data, 4);
			data += 4;
			memcpy(&itemID, data, 4);
			data += 4;
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&loc2, data, 6);
			data += 6;
			memcpy(&speed, data, 4);
			data += 4;
			memcpy(&duration, data, 4);
			data += 4;
			memcpy(&hue, data, 4);
			data += 4;
			PacketManager_MakePacket_EFFECT(effectBuf, 0, 0, serial, (uint16_t)itemID, loc1.x, loc1.y, (uint8_t)loc1.z, loc2.x, loc2.y, (uint8_t)loc2.z, (uint8_t)speed,
			        0, 0, 0, (uint8_t)duration, (uint8_t)hue);
			CPlayerList_BroadcastToTwoLocs(effectBuf, &loc1, &loc2, 0x12, NULL);
			break;

		case 5: // doMissile_Mob2Loc: EFFECT type 0, dual broadcast
			memcpy(&serial, data, 4);
			data += 4;
			memcpy(&itemID, data, 4);
			data += 4;
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&loc2, data, 6);
			data += 6;
			memcpy(&speed, data, 4);
			data += 4;
			memcpy(&duration, data, 4);
			data += 4;
			memcpy(&hue, data, 4);
			data += 4;
			PacketManager_MakePacket_EFFECT(effectBuf, 0, serial, 0, (uint16_t)itemID, loc1.x, loc1.y, (uint8_t)loc1.z, loc2.x, loc2.y, (uint8_t)loc2.z, (uint8_t)speed,
			        0, 0, 0, (uint8_t)duration, (uint8_t)hue);
			CPlayerList_BroadcastToTwoLocs(effectBuf, &loc1, &loc2, 0x12, NULL);
			break;

		case 6: // doMissile_Mob2Mob: EFFECT type 0, dual broadcast
			memcpy(&serial, data, 4);
			data += 4;
			memcpy(&serial2, data, 4);
			data += 4;
			memcpy(&itemID, data, 4);
			data += 4;
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&loc2, data, 6);
			data += 6;
			memcpy(&speed, data, 4);
			data += 4;
			memcpy(&duration, data, 4);
			data += 4;
			memcpy(&hue, data, 4);
			data += 4;
			PacketManager_MakePacket_EFFECT(effectBuf, 0, serial, serial2, (uint16_t)itemID, loc1.x, loc1.y, (uint8_t)loc1.z, loc2.x, loc2.y, (uint8_t)loc2.z,
			        (uint8_t)speed, 0, 0, 0, (uint8_t)duration, (uint8_t)hue);
			CPlayerList_BroadcastToTwoLocs(effectBuf, &loc1, &loc2, 0x12, NULL);
			break;

		case 7: // animateMobile: ANIM packet, single broadcast
			memcpy(&serial, data, 4);
			data += 4;
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&itemID, data, 4);
			data += 4;
			memcpy(&speed, data, 4);
			data += 4;
			memcpy(&duration, data, 4);
			data += 4;
			memcpy(&hue, data, 4);
			data += 4;
			memcpy(&renderMode, data, 4);
			data += 4;
			PacketManager_MakePacket_ANIM(animBuf, serial, (uint16_t)itemID, (uint16_t)speed, (uint16_t)duration, (uint8_t)hue, (uint8_t)renderMode, 0);
			SendPacketInRange(animBuf, &loc1, 0x12);
			break;

		case 8: // sfx: PlaySoundAtLocation
			memcpy(&loc1, data, 6);
			data += 6;
			memcpy(&soundID, data, 4);
			data += 4;
			memcpy(&volume, data, 4);
			data += 4;
			PlaySoundAtLocation(&loc1, (uint16_t)soundID, (uint16_t)volume);
			break;

		case 9: // sfxTo: SendSoundToEntity
			memcpy(&entity, data, 4);
			data += 4;
			memcpy(&soundID, data, 4);
			data += 4;
			memcpy(&volume, data, 4);
			data += 4;
			SendSoundToEntity(entity, soundID, (uint16_t)volume);
			break;
		}

		cmd = cmd->next;
	}

	if ((actionId & 0xFF) < 1)
		actionId = 1;
	PacketManager_MakePacket_SEQUENCE(seqBuf, actionId);
	AnimSequence_BroadcastNearby(seqBuf, g_AnimSequence.locList, 0x12);
}

/*
 * 0x004D0099 - AnimSequence::Clear
 *
 * Resets state and frees every node in locList and cmdList.
 */
void
AnimSequence_Clear(void)
{
	SeqLocNode *lnext;
	SeqCmdNode *cnext;

	g_AnimSequence.state = 0;
	while (g_AnimSequence.locList != NULL) {
		lnext = g_AnimSequence.locList->next;
		OperatorDelete(g_AnimSequence.locList);
		g_AnimSequence.locList = lnext;
	}
	while (g_AnimSequence.cmdList != NULL) {
		cnext = g_AnimSequence.cmdList->next;
		if (g_AnimSequence.cmdList->data != NULL)
			OperatorDelete(g_AnimSequence.cmdList->data);
		OperatorDelete(g_AnimSequence.cmdList);
		g_AnimSequence.cmdList = cnext;
	}
}

/*
 * 0x004D013D - AnimSequence::AddLocation
 *
 * Appends loc to locList if not already present.
 */
void
AnimSequence_AddLocation(CLocation *loc)
{
	SeqLocNode *cur, *node, *tail;

	for (cur = g_AnimSequence.locList; cur != NULL; cur = cur->next) {
		if (cur->loc.x == loc->x && cur->loc.y == loc->y && cur->loc.z == loc->z)
			return; /* already have this location */
	}
	node = (SeqLocNode *)OperatorNew(sizeof(SeqLocNode));
	if (node != NULL)
		CLocation_Constructor(&node->loc);
	CLocation_CopyFrom(&node->loc, loc);
	node->next = NULL;
	if (g_AnimSequence.locList == NULL) {
		g_AnimSequence.locList = node;
	} else {
		for (tail = g_AnimSequence.locList; tail->next != NULL; tail = tail->next)
			;
		tail->next = node;
	}
}

/*
 * 0x004D024D - AnimSequence::AddCommand
 *
 * Allocates a new cmdList node carrying type and a copy of data, appends it.
 */
void
AnimSequence_AddCommand(uint8_t type, const uint8_t *data, uint32_t dataSize)
{
	SeqCmdNode *node, *tail;

	node = (SeqCmdNode *)OperatorNew(sizeof(SeqCmdNode));
	node->type = type;
	node->data = (uint8_t *)OperatorNew(dataSize);
	memcpy(node->data, data, dataSize);
	node->next = NULL;
	if (g_AnimSequence.cmdList == NULL) {
		g_AnimSequence.cmdList = node;
	} else {
		for (tail = g_AnimSequence.cmdList; tail->next != NULL; tail = tail->next)
			;
		tail->next = node;
	}
}
