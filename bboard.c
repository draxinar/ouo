/*
 * CBulletinBoard - in-world bulletin board with posted messages.
 *
 * Maintains a per-board list of CBulletinMessage entries, responds to
 * the client's browse and compose packets, and persists messages across
 * saves.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "bboard.h"
#include "combat.h"
#include "dynamic.h"
#include "list.h"
#include "main.h"
#include "packet_handler.h"
#include "packet_utils.h"
#include "player.h"
#include "region.h"
#include "taglist.h"
#include "time.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static int BulletinBoard_GetMsgTime(CItem *post); // 0x0043219B
static int CBulletinBoard_FindByMsgId(CBulletinBoard *bb, uint32_t serial); // 0x004320EE
static void MakePacket_BBOARD(uint8_t *buf, int type, CItem *post, int flag); // 0x00431404
static void MakePacket_WritableHeader(uint8_t *buf, int flag, CItem *entity); // 0x004313A0

/*
 * 0x004313A0 - MakePacket_WritableHeader
 *
 * Builds a writable book header packet (0x71) with flag byte,
 * entity serial, and the tiledata name string.
 */
static void
MakePacket_WritableHeader(uint8_t *buf, int flag, CItem *entity)
{
	uint16_t bodyType;

	PutPacketType(buf, 0x71, 0x8000);
	PutByte(buf, (uint8_t)flag);
	PutDWord(buf, entity->serial);
	bodyType = CEntity_GetBodyType(entity) & 0xFFFF;
	PutString(buf, g_ItemTileData[bodyType].name, 0x1E);
}
/*
 * 0x00431404 - MakePacket_BBOARD
 *
 * Builds a BBOARD (0x71) packet. type=1 is the board summary, type=2 is
 * a post body with line data.
 */
static void
MakePacket_BBOARD(uint8_t *buf, int type, CItem *post, int flag)
{
	CList *tagResult;
	CListNode *node;
	int len;
	int postTime;
	int day, hour, minute;
	char timeBuf[40];

	tagResult = NULL;
	PutPacketType(buf, 0x71, 0x8000);
	PutByte(buf, (uint8_t)type);
	PutDWord(buf, post->parent->serial);
	PutDWord(buf, post->serial);

	if (flag != 0) {
		uint32_t msgId;

		msgId = CBulletinBoard_GetMsgId(post);
		PutDWord(buf, msgId);

		node = NULL;
		tagResult = CResourceEntity_GetTagEntity(post, "poster");
		if (tagResult != NULL) {
			if (tagResult != NULL)
				node = tagResult->head;
			else
				node = NULL;
		}
		if (node != NULL && node->typeTag == 1) {
			len = CString_GetLength((CString *)(uintptr_t)node->value) + 1;
			PutByte(buf, (uint8_t)len);
			PutString(buf, CString_GetData((CString *)(uintptr_t)node->value), len);
		} else {
			PutByte(buf, 1);
			PutByte(buf, 0);
		}

		node = NULL;
		tagResult = CResourceEntity_GetTagEntity(post, "postText");
		if (tagResult != NULL) {
			if (tagResult != NULL)
				node = tagResult->head;
			else
				node = NULL;
		}
		if (node != NULL && node->typeTag == 1) {
			len = CString_GetLength((CString *)(uintptr_t)node->value) + 1;
			PutByte(buf, (uint8_t)len);
			PutString(buf, CString_GetData((CString *)(uintptr_t)node->value), len);
		} else {
			PutByte(buf, 1);
			PutByte(buf, 0);
		}

		postTime = 0;
		node = NULL;
		CResourceEntity_GetTagInt(post, "postTime", &postTime);
		day = postTime / 1440;
		postTime = postTime - day * 1440;
		hour = postTime / 60;
		postTime = postTime - hour * 60;
		minute = postTime;
		sprintf(timeBuf, "Day %d @ %d:%02d", day, hour, minute);
		PutByte(buf, (uint8_t)(strlen(timeBuf) + 1));
		PutString(buf, timeBuf, strlen(timeBuf) + 1);
	} else {
		node = NULL;
		tagResult = CResourceEntity_GetTagEntity(post, "poster");
		if (tagResult != NULL) {
			if (tagResult != NULL)
				node = tagResult->head;
			else
				node = NULL;
		}
		if (node != NULL && node->typeTag == 1) {
			len = CString_GetLength((CString *)(uintptr_t)node->value) + 1;
			PutByte(buf, (uint8_t)len);
			PutString(buf, CString_GetData((CString *)(uintptr_t)node->value), len);
		} else {
			PutByte(buf, 1);
			PutByte(buf, 0);
		}

		node = NULL;
		tagResult = CResourceEntity_GetTagEntity(post, "postText");
		if (tagResult != NULL) {
			if (tagResult != NULL)
				node = tagResult->head;
			else
				node = NULL;
		}
		if (node != NULL && node->typeTag == 1) {
			len = CString_GetLength((CString *)(uintptr_t)node->value) + 1;
			PutByte(buf, (uint8_t)len);
			PutString(buf, CString_GetData((CString *)(uintptr_t)node->value), len);
		} else {
			PutByte(buf, 1);
			PutByte(buf, 0);
		}

		postTime = 0;
		node = NULL;
		CResourceEntity_GetTagInt(post, "postTime", &postTime);
		day = postTime / 1440;
		postTime = postTime - day * 1440;
		hour = postTime / 60;
		postTime = postTime - hour * 60;
		minute = postTime;
		sprintf(timeBuf, "Day %d @ %d:%02d", day, hour, minute);
		PutByte(buf, (uint8_t)(strlen(timeBuf) + 1));
		PutString(buf, timeBuf, strlen(timeBuf) + 1);

		node = NULL;
		tagResult = CResourceEntity_GetTagEntity(post, "poster");
		if (tagResult != NULL) {
			if (tagResult != NULL)
				node = tagResult->head;
			else
				node = NULL;
		}
		if (node != NULL && node->typeTag == 1) {
			node = node->next;
			if (node != NULL && node->typeTag == 0) {
				PutWord(buf, (uint16_t)node->value);
				node = node->next;
				if (node != NULL && node->typeTag == 0) {
					PutWord(buf, (uint16_t)node->value);
					PutByte(buf, (uint8_t)((tagResult->count - 3) / 2));
					node = node->next;
					while (node != NULL) {
						if (node->typeTag == 0)
							PutWord(buf, (uint16_t)node->value);
						else
							PutWord(buf, 0);
						node = node->next;
						if (node != NULL && node->typeTag == 0)
							PutWord(buf, (uint16_t)node->value);
						else
							PutWord(buf, 0);
						node = node->next;
					}
				} else {
					PutWord(buf, 0);
					PutByte(buf, 0);
				}
			} else {
				PutWord(buf, 0);
				PutWord(buf, 0);
				PutByte(buf, 0);
			}
		} else {
			PutWord(buf, 0);
			PutWord(buf, 0);
			PutByte(buf, 0);
		}

		node = NULL;
		tagResult = CResourceEntity_GetTagEntity(post, "postText");
		if (tagResult != NULL) {
			if (tagResult != NULL)
				node = tagResult->head;
			else
				node = NULL;
		}
		if (node != NULL && node->typeTag == 1) {
			node = node->next;
			PutByte(buf, (uint8_t)(tagResult->count - 1));
			while (node != NULL) {
				if (node->typeTag == 1) {
					len = CString_GetLength((CString *)(uintptr_t)node->value) + 1;
					PutByte(buf, (uint8_t)len);
					PutString(buf, CString_GetData((CString *)(uintptr_t)node->value), len);
				} else {
					PutByte(buf, 1);
					PutByte(buf, 0);
				}
				node = node->next;
			}
		} else {
			PutByte(buf, 0);
		}
	}
}

/*
 * 0x00431B48 - FindClosestBBoardItem
 *
 * Returns the ground-level bulletin board closest to loc by Chebyshev
 * distance, skipping boards inside containers. NULL if none.
 */
CItem *
FindClosestBBoardItem(CLocation *loc)
{
	CBulletinBoard *cur;
	CBulletinBoard *best;
	int bestDist;
	int dist;

	cur = g_BBoardHead;
	while (cur != NULL) {
		if (cur->container.item.parent == NULL)
			break;
		cur = cur->bbNext;
	}

	if (cur == NULL)
		return NULL;

	best = cur;
	bestDist = CLocation_ChebyshevDistance(loc, &cur->container.item.resourceEntity.entity.location);

	cur = cur->bbNext;
	while (cur != NULL) {
		if (cur->container.item.parent != NULL) {
			cur = cur->bbNext;
			continue;
		}
		dist = CLocation_ChebyshevDistance(loc, &cur->container.item.resourceEntity.entity.location);
		if (dist < bestDist) {
			best = cur;
			bestDist = dist;
		}
		cur = cur->bbNext;
	}

	return &best->container.item;
}

/*
 * 0x00431BE8 - CBulletinBoard::CBulletinBoard
 *
 * Constructor. Chains to CContainer_Constructor, sets the CBoard vtable,
 * bumps g_BBoardCount, and inserts at the head of g_BBoardHead.
 */
void
CBulletinBoard_Constructor(CBulletinBoard *bb)
{
	CContainer_Constructor(&bb->container);
	bb->container.item.resourceEntity.entity.vtable = &g_vtable_CBoard;
	g_BBoardCount++;
	bb->bbNext = g_BBoardHead;
	if (bb->bbNext != NULL)
		bb->bbNext->bbPrev = bb;
	g_BBoardHead = bb;
	bb->bbPrev = NULL;
}

/*
 * 0x00431C49 - CBulletinBoard::~CBulletinBoard
 *
 * Destructor. Removes every post unless the server is shutting down,
 * unlinks from g_BBoardHead, decrements g_BBoardCount, and chains to
 * CContainer_Destructor.
 */
void
CBulletinBoard_Destructor(CBulletinBoard *bb)
{
	CItem *item, *next;

	if (g_shutdown)
		goto unlink;

	for (;;) {
		if (bb->container.contents == NULL)
			goto unlink;
		item = bb->container.contents;
		while (item != NULL) {
			next = item->spatialNext;
			CBulletinBoard_RemovePost(bb, item, NULL);
			item = next;
		}
	}

unlink:
	if (bb->container.item.resourceEntity.entity.removedFromWorld == 0)
		CItem_HideVT(&bb->container.item);

	CItem_ClearScriptsAndTags(&bb->container.item);

	if (bb->bbNext != NULL)
		bb->bbNext->bbPrev = bb->bbPrev;
	if (bb->bbPrev != NULL)
		bb->bbPrev->bbNext = bb->bbNext;

	if (g_BBoardHead == bb)
		g_BBoardHead = bb->bbNext;

	g_BBoardCount--;

	CContainer_Destructor(&bb->container.item);
}

/*
 * 0x00431D4A - OpenWritableBook
 *
 * Sends the writable book header to entity's nearby clients and then
 * dispatches the board's contents as container children.
 */
void
OpenWritableBook(CItem *entity, CItem *player, uint32_t playerSerial)
{
	uint8_t buf[0x1904];

	MakePacket_WritableHeader(buf, 0, entity);
	Entity_BroadcastPacket(player, playerSerial, buf);
	CContainer_SendContainerContents((CContainer *)entity, player, playerSerial, 0);
}

/*
 * 0x00431DA7 - CBulletinBoard::SendBoardSummary
 *
 * Sends the board summary BBOARD packet to player.
 */
void
CBulletinBoard_SendBoardSummary(CBulletinBoard *bb, CPlayer *player, CItem *post)
{
	uint8_t buf[0x1904];

	USED(bb);
	MakePacket_BBOARD(buf, 1, post, 1);
	SendToClient((CItem *)player, buf, -1);
}

/*
 * 0x00431DEC - CBulletinBoard::SendPostBody
 *
 * Sends the post body BBOARD packet to player.
 */
void
CBulletinBoard_SendPostBody(CBulletinBoard *bb, CPlayer *player, CItem *post)
{
	uint8_t buf[0x1904];

	USED(bb);
	MakePacket_BBOARD(buf, 2, post, 0);
	SendToClient((CItem *)player, buf, -1);
}

/*
 * 0x00431E31 - CBulletinBoard::PostMessage
 *
 * Creates a new post item in the board with msgId/msgDay/msgTime ObjVars.
 * When player is set, the "poster" tag captures the player's name, body,
 * color, and equipped gear across 26 layers. The "postText" tag stores
 * the subject plus each body line.
 */
void
CBulletinBoard_PostMessage(CBulletinBoard *bb, CPlayer *player, CItem *replyTo, char *subject, int numLines, uintptr_t *lines)
{
	// 0x0061DAD8 - equipment layer iteration order.
	static const uint8_t equipLayerOrder[26] = { 0x14, 0x05, 0x04, 0x03, 0x18, 0x0D, 0x11, 0x08, 0x09, 0x0E, 0x0F, 0x13, 0x07, 0x17, 0x16, 0x0C, 0x0A, 0x0B, 0x10, 0x12, 0x06,
		0x01, 0x02, 0x15, 0x19, 0x00 };

	CItem *post;
	uint32_t replySerial;
	CList *list;
	CLocation loc;
	CString str;
	TagNode *node;
	int postTime;
	int layerIdx;
	CItem *equip;
	int i;

	CString_DefaultConstructor(&str);

	CLocation_Init(&loc);

	replySerial = (replyTo != NULL) ? replyTo->serial : 0;

	CLocation_Set(&loc, 0, 0, 0);

	post = (CItem *)OperatorNew(sizeof(CItem));
	if (post != NULL)
		post = CItem_Constructor(post);
	else
		post = NULL;

	CEntity_SetBodyType(post, 0xEB0);

	((void (*)(void *, void *, void *))VT_FN(post, VT_ADD_TO_CONTAINER))(post, (CItem *)bb, &loc);

	CItem_SetMsgId(post, replySerial);

	CItem_SetMsgDay(post, (uint32_t)g_GameDay);

	CEntity_SetObjVar(post, "msgTime", 0, g_GameTick);

	if ((CItem *)player != NULL) {
		node = CEntity_SetObjVar(post, "poster", 5, 0);
		list = (CList *)(uintptr_t)node->value;

		{
			const char *name;
			name = ((const char *(*)(void *))VT_FN((CItem *)player, VT_GET_NAME))((CItem *)player);
			CString_AssignCStr(&str, name);
			CList_Append(list, 1, (uintptr_t)&str);
		}

		CList_Append(list, 0, (uint32_t)(CEntity_GetBodyType((CItem *)player) & 0xFFFF));

		CList_Append(list, 0, (uint32_t)(((CItem *)player)->resourceEntity.entity.color & 0xFFFF));

		for (i = 0; i < 0x1A; i++) {
			layerIdx = equipLayerOrder[i];
			equip = ((CMobile *)player)->equipment[layerIdx];
			if (equip == NULL)
				continue;

			CList_Append(list, 0, (uint32_t)(CEntity_GetBodyType(equip) & 0xFFFF));

			CList_Append(list, 0, (uint32_t)(equip->resourceEntity.entity.color & 0xFFFF));
		}
	}

	postTime = g_GameDay * 24 * 60 + g_GameHour * 60 + g_GameMinute;

	{
		CString tmpName;
		CString_Constructor(&tmpName, "postTime");
		ObjVar_SetStr(post, &tmpName, 0, (uint32_t)postTime);
	}

	node = CEntity_SetObjVar(post, "postText", 5, 0);
	list = (CList *)(uintptr_t)node->value;

	CString_AssignCStr(&str, subject);
	CList_Append(list, 1, (uintptr_t)&str);

	for (i = 0; i < (numLines & 0xFF); i++) {
		CString_AssignCStr(&str, (const char *)lines[i]);
		CList_Append(list, 1, (uintptr_t)&str);
	}

	CString_Destructor(&str);
}

/*
 * 0x004320EE - CBulletinBoard::FindByMsgId
 *
 * Returns 1 if any post on bb has msgId equal to serial.
 */
static int
CBulletinBoard_FindByMsgId(CBulletinBoard *bb, uint32_t serial)
{
	CItem *item;

	for (item = bb->container.contents; item != NULL; item = item->spatialNext) {
		if (CBulletinBoard_GetMsgId(item) == serial)
			return 1;
	}
	return 0;
}

/*
 * 0x00432135 - CBulletinBoard::RemovePost
 *
 * Deletes a post. When mob is non-NULL, only the admin (pflags bit 1) or
 * the poster may remove. Posts with replies are preserved.
 */
void
CBulletinBoard_RemovePost(CBulletinBoard *bb, CItem *post, CMobile *mob)
{
	if (mob != NULL) {
		if ((((CPlayer *)mob)->pflags & 2) == 0) {
			uint32_t posterSerial;
			posterSerial = CItem_GetMsgOwner(post);
			if (posterSerial != CMobile_GetSerial(mob))
				return;
		}
	}

	if (CBulletinBoard_FindByMsgId(bb, post->serial))
		return;

	if (post != NULL)
		((void (*)(void *))VT_FN(post, VT_DELETE))(post);
}

/*
 * 0x0043219B - BulletinBoard_GetMsgTime
 *
 * Returns the post's "msgTime" ObjVar, 0 if absent.
 */
static int
BulletinBoard_GetMsgTime(CItem *post)
{
	int val;

	if (!CResourceEntity_HasTag(post, "msgTime", 0))
		return 0;
	CResourceEntity_GetTagInt(post, "msgTime", &val);
	return val;
}

/*
 * 0x004321CE - CBulletinBoard::PurgeDayOldPosts
 *
 * Removes posts older than 604800 ticks (one week).
 */
void
CBulletinBoard_PurgeDayOldPosts(CBulletinBoard *bb, uint32_t newDay)
{
	CItem *item, *next;
	int msgTime;

	USED(newDay);

	item = bb->container.contents;
	while (item != NULL) {
		next = item->spatialNext;
		msgTime = BulletinBoard_GetMsgTime(item);
		if ((int)(g_GameTick - (uint32_t)msgTime) >= 0x93A80)
			CBulletinBoard_RemovePost(bb, item, NULL);
		item = next;
	}
}

/*
 * 0x00432230 - CBulletinBoard::`scalar deleting destructor'
 *
 * Runs the destructor and frees the object when flags&1 is set.
 */
void *
CBulletinBoard_ScalarDelete(CBulletinBoard *bb, int flags)
{
	CBulletinBoard_Destructor(bb);
	if (flags & 1)
		OperatorDelete(bb);
	return NULL;
}
