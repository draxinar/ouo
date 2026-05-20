/*
 * CSignpost - item that displays a text sign to players near it.
 *
 * Stores the sign text on the item, serves the double-click request
 * packet, and persists the text through the save / load hooks.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "mobile.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "vtable.h"

/*
 * 0x00484DF9 - CSignpost vtable[0x090] Delete
 *
 * Only calls CItem_DeleteCheck1 (no Check2), then invokes dtor.
 */
void
CSignpost_Delete(CItem *item)
{
	if (!CItem_DeleteCheck1(item))
		return;
	if (item != NULL)
		((void *(*)(void *, int))VT_FN(item, VT_DTOR))(item, 1);
}
/*
 * 0x0048AD46 - CSignpost::CSignpost
 *
 * Constructs a signpost: chains CItem_Constructor, bumps g_SignpostCount,
 * and zeroes vectHead and the mapExtent array.
 */
CSignpost *
CSignpost_Constructor(CSignpost *this)
{
	CItem_Constructor(&this->item);
	CEntity_SetType(&this->item.resourceEntity.entity, ETYPE_SIGNPOST);
	g_SignpostCount++;
	this->vectHead = NULL;
	this->lockOwner = NULL;
	this->mapExtent[0] = 0;
	this->mapExtent[1] = 0;
	this->mapExtent[2] = 0;
	this->mapExtent[3] = 0;
	this->mapExtent[4] = 0;
	this->mapExtent[5] = 0;
	return this;
}

/*
 * 0x0048AE85 - CSignpost::~CSignpost
 *
 * Frees the vectHead list, hides the item if still in world, clears
 * scripts/tags, decrements g_SignpostCount, and chains to CItem_Destructor.
 */
void
CSignpost_Destructor(CSignpost *this)
{
	VectNode *cur, *next;

	cur = this->vectHead;
	while (cur != NULL) {
		next = cur->next;
		free(cur);
		cur = next;
	}
	this->vectHead = NULL;

	if (!this->item.resourceEntity.entity.removedFromWorld)
		CItem_HideVT(&this->item);

	CItem_ClearScriptsAndTags(&this->item);

	g_SignpostCount--;

	CItem_Destructor(&this->item);
}

/*
 * 0x0048AF39 - PlotOnMap
 *
 * Dispatches map-pin commands on this signpost:
 *   cmd 1: append pin
 *   cmd 2: insert pin after position arg (0 = before all)
 *   cmd 3: move pin at position arg
 *   cmd 4: delete pin at position arg
 *   cmd 5: clear all pins
 * Resets decayCount after every operation.
 */
void
PlotOnMap(CSignpost *this, int command, int arg, uint16_t plotX, uint16_t plotY)
{
	VectNode *curNode, *tailTarget, *newNode;
	int i;
	int cmd;

	cmd = (command & 0xFF) - 1;
	if ((unsigned)cmd > 4)
		goto done;

	switch (cmd) {
	case 0:
		curNode = this->vectHead;
		if (curNode != NULL) {
			while (curNode->next != NULL)
				curNode = curNode->next;
			tailTarget = curNode->next;
		} else {
			tailTarget = NULL;
		}

		newNode = (VectNode *)malloc(sizeof(VectNode));
		if (newNode != NULL)
			CLocation_Init((CLocation *)newNode);

		newNode->x = plotX;
		newNode->y = plotY;
		newNode->next = tailTarget;

		if (curNode != NULL)
			curNode->next = newNode;
		else
			this->vectHead = newNode;
		break;

	case 1:
		if ((arg & 0xFF) == 0) {
			curNode = NULL;
			tailTarget = this->vectHead;
		} else {
			curNode = this->vectHead;
			for (i = 0; i < (arg & 0xFF) - 1; i++) {
				if (curNode != NULL)
					curNode = curNode->next;
			}
			if (curNode != NULL)
				tailTarget = curNode->next;
			else
				tailTarget = NULL;
		}

		newNode = (VectNode *)malloc(sizeof(VectNode));
		if (newNode != NULL)
			CLocation_Init((CLocation *)newNode);

		newNode->x = plotX;
		newNode->y = plotY;
		newNode->next = tailTarget;

		if (curNode != NULL)
			curNode->next = newNode;
		else
			this->vectHead = newNode;
		break;

	case 2:
		curNode = this->vectHead;
		for (i = 0; i < (arg & 0xFF); i++) {
			if (curNode != NULL)
				curNode = curNode->next;
		}
		if (curNode != NULL) {
			curNode->x = plotX;
			curNode->y = plotY;
		}
		break;

	case 3:
		tailTarget = this->vectHead;
		for (i = 0; i < (arg & 0xFF); i++) {
			if (tailTarget != NULL)
				tailTarget = tailTarget->next;
		}

		if (tailTarget != this->vectHead) {
			curNode = this->vectHead;
			while (curNode != NULL && curNode->next != tailTarget)
				curNode = curNode->next;
			if (curNode != NULL && tailTarget != NULL)
				curNode->next = tailTarget->next;
		} else {
			if (tailTarget != NULL)
				this->vectHead = tailTarget->next;
		}

		if (tailTarget != NULL)
			free(tailTarget);
		break;

	case 4:
		curNode = this->vectHead;
		while (curNode != NULL) {
			tailTarget = curNode->next;
			free(curNode);
			curNode = tailTarget;
		}
		this->vectHead = NULL;
		break;
	}

done:
	this->item.decayCount = 0;
}

/*
 * 0x0048B296 - CSignpost::SendMapCommand
 *
 * Broadcasts a clear command followed by an add-pin for every VectNode.
 */
void
CSignpost_SendMapCommand(CSignpost *this, CItem *player, uint32_t playerSerial)
{
	uint8_t pkt[11];
	VectNode *pin;

	PacketManager_MakePacket_MAP_COMMAND(pkt, this->item.serial, 5, 0, 0, 0);
	Entity_BroadcastPacket(player, playerSerial, pkt);

	pin = this->vectHead;
	while (pin != NULL) {
		PacketManager_MakePacket_MAP_COMMAND(pkt, this->item.serial, 1, 0, pin->x, pin->y);
		Entity_BroadcastPacket(player, playerSerial, pkt);
		pin = pin->next;
	}
}

/*
 * 0x0048B32A - CSignpost::SendMapDisplay
 *
 * Broadcasts the map-display packet derived from the signpost's layer
 * gump ID and mapExtent bounding box.
 */
void
CSignpost_SendMapDisplay(CSignpost *this, CItem *player, uint32_t playerSerial)
{
	uint8_t pkt[19];
	uint16_t gumpId;

	gumpId = (CItem_GetEquipSlot((CItem *)this) & 0xFF) + 0x139D;
	PacketManager_MakePacket_MAP_DISPLAY(
	        pkt, this->item.serial, gumpId, this->mapExtent[0], this->mapExtent[1], this->mapExtent[2], this->mapExtent[3], this->mapExtent[4], this->mapExtent[5]);
	Entity_BroadcastPacket(player, playerSerial, pkt);
}

/*
 * 0x0048B3A3 - CSignpost::MapPinToWorldCoord
 *
 * Converts a VectNode pin (x,y in map-relative coords) to world
 * coordinates using the signpost's mapExtent bounding box.
 * Returns pointer to static CLocation.
 */
CLocation *
CSignpost_MapPinToWorldCoord(CSignpost *sp, VectNode *node)
{
	static int inited;
	static CLocation loc;
	int xResult, yResult;

	if (!(inited & 1)) {
		inited |= 1;
		CLocation_Init(&loc);
	}

	if (sp->mapExtent[4] == 0) {
		xResult = 0;
	} else {
		xResult = ((int)(int16_t)node->x * ((int)(uint16_t)sp->mapExtent[2] - (int)(uint16_t)sp->mapExtent[0])) / (int)(uint16_t)sp->mapExtent[4] +
		          (int)(uint16_t)sp->mapExtent[0];
	}
	loc.x = (int16_t)xResult;

	if (sp->mapExtent[5] == 0) {
		yResult = 0;
	} else {
		yResult = ((int)(int16_t)node->y * ((int)(uint16_t)sp->mapExtent[3] - (int)(uint16_t)sp->mapExtent[1])) / (int)(uint16_t)sp->mapExtent[5] +
		          (int)(uint16_t)sp->mapExtent[1];
	}
	loc.y = (int16_t)yResult;

	loc.z = node->z;

	return &loc;
}

/*
 * 0x0048B4B3 - CSignpost vtable[0x24] GetValue
 *
 * VT_GET_VALUE override: adds the signpost's map-area contribution
 * sqrt((x2-x1)*(y2-y1))/100 + sqrt(z1*z2)/100 to the base item value.
 */
int
CSignpost_GetValue_VT(CItem *self, int useResource, int normalize)
{
	CSignpost *sp;
	int value;
	int area, zProd;
	int areaVal, zVal;

	sp = (CSignpost *)self;
	value = 0;
	if (CItem_IsValueless(self))
		return value;

	value = CItem_GetValue_VT(self, useResource, 0);

	area = (sp->mapExtent[2] - sp->mapExtent[0]) * (sp->mapExtent[3] - sp->mapExtent[1]);
	areaVal = (int)(sqrt((double)area) / 100.0);

	zProd = sp->mapExtent[4] * sp->mapExtent[5];
	zVal = (int)(sqrt((double)zProd) / 100.0);

	value += areaVal + zVal;

	if (normalize != 0)
		value = CItem_NormalizeValue(self, value);

	return value;
}

/*
 * 0x004912F0 - CSignpost scalar deleting destructor (vtable[0], 44 bytes)
 *
 * Destroys the signpost and frees it when flags&1.
 */
void *
CSignpost_ScalarDelete(CSignpost *sp, int flags)
{
	CSignpost_Destructor(sp);
	if (flags & 1)
		free(sp);
	return NULL;
}
