/*
 * Secure trade window - two-party item staging with dual confirmation.
 *
 * A CTradeSession is a pair of containers, one per trader, plus
 * acceptance flags; the trade commits only when both sides check their
 * box, otherwise every item is returned to its original owner.
 */

#include <stddef.h>
#include <stdint.h>

#include "dat.h"

#include "packet_handler.h"
#include "packet_manager.h"
#include "player.h"
#include "region.h"
#include "trade.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static void CTradeSession_MoveItems(CTradeSession *session); // 0x004D9F08
static void CancelAllTrades(CTradeSession **listHead); // 0x004D9930
static void CTradeSessionList_Destructor(CTradeSession **this); // 0x004D97A7
static void *CTradeSessionList_Constructor(CTradeSession **this); // 0x004D9790

/*
 * 0x004D9790 - CTradeSessionList::CTradeSessionList
 *
 * Constructor for the global trade session list head. Initializes
 * the head pointer to NULL.
 */
static __attribute__((unused)) void *
CTradeSessionList_Constructor(CTradeSession **this)
{
	*this = NULL;
	return this;
}
/*
 * 0x004D97A7 - CTradeSessionList::~CTradeSessionList
 *
 * No-op destructor for the global trade session list head.
 */
static __attribute__((unused)) void
CTradeSessionList_Destructor(CTradeSession **this)
{
	USED(this);
}

/*
 * 0x004D97B2 - CTradeSession::FindOrCreateSession
 *
 * Looks up the trade session between player1 and player2. If found,
 * drops the item into the existing session's container2. Otherwise
 * allocates a new session and appends it to the list.
 */
void
CTradeSession_FindOrCreateSession(CTradeSession **listHead, CPlayer *player1, CPlayer *player2, CItem *item)
{
	CLocation tmpLoc;
	CTradeSession *cursor;
	CTradeSession *newSession;
	void *mem;

	CLocation_Init(&tmpLoc);

	CWorld_FindBySerial(g_World, ((CItem *)player1)->serial);
	CWorld_FindBySerial(g_World, ((CItem *)player2)->serial);

	cursor = *listHead;
	while (cursor != NULL) {
		if (cursor->player1 == player1 && cursor->player2 == player2)
			break;
		cursor = cursor->next;
	}

	if (cursor != NULL) {
		CLocation_Set(&tmpLoc, -1, -1, 0);
		((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, cursor->container2, &tmpLoc);
		return;
	}

	if (*listHead != NULL) {
		cursor = *listHead;
		while (cursor->next != NULL)
			cursor = cursor->next;

		mem = OperatorNew(sizeof(CTradeSession));
		if (mem != NULL)
			newSession = CTradeSession_CreateSession((CTradeSession *)mem, player1, player2, item);
		else
			newSession = NULL;
		cursor->next = newSession;
	} else {
		mem = OperatorNew(sizeof(CTradeSession));
		if (mem != NULL)
			newSession = CTradeSession_CreateSession((CTradeSession *)mem, player1, player2, item);
		else
			newSession = NULL;
		*listHead = newSession;
	}
}

/*
 * 0x004D9930 - CancelAllTrades
 *
 * Cancels every trade in the session list by calling CloseTrade on
 * each, then clears the list head. Redundant locals and an
 * unreachable null check are preserved from the binary.
 */
static __attribute__((unused)) void
CancelAllTrades(CTradeSession **listHead)
{
	CTradeSession *session;
	CTradeSession *sess2;
	CTradeSession *result;

	while (*listHead != NULL) {
		session = *listHead;
		sess2 = session;
		if (sess2 != NULL) {
			result = CloseTrade(sess2, 1);
		} else {
			result = NULL;
		}
	}
	USED(result);
	*listHead = NULL;
}

/*
 * 0x004D997A - CTradeSession::CreateSession
 *
 * Initializes a new trade session: creates the two trade containers
 * (body 0x1E5E) attached to each player, sends TRADE_OPEN to both
 * clients, and drops the initial item into container2.
 */
CTradeSession *
CTradeSession_CreateSession(CTradeSession *this, CPlayer *player1, CPlayer *player2, CItem *item)
{
	CLocation tmpLoc;
	CItem *container;
	void *mem;
	const char *name;
	uint8_t obuf[48];

	CLocation_Init(&tmpLoc);

	this->player1 = player1;
	this->player2 = player2;
	this->accept2 = 0;
	this->accept1 = 0;
	this->next = NULL;

	CWorld_FindBySerial(g_World, ((CItem *)player1)->serial);
	CWorld_FindBySerial(g_World, ((CItem *)player2)->serial);

	mem = OperatorNew(sizeof(CContainer));
	if (mem != NULL) {
		CContainer_Constructor((CContainer *)mem);
		container = (CItem *)mem;
	} else {
		container = NULL;
	}
	this->container1 = container;
	((CContainer *)this->container1)->lockOwner = this;
	CEntity_SetBodyType(this->container1, 0x1E5E);
	CItem_SetWeight(this->container1, 0);
	CLocation_Set(&tmpLoc, -1, -1, 0);
	((void (*)(void *, CItem *, CLocation *))VT_FN(this->container1, VT_ADD_TO_CONTAINER))(this->container1, (CItem *)player1, &tmpLoc);

	mem = OperatorNew(sizeof(CContainer));
	if (mem != NULL) {
		CContainer_Constructor((CContainer *)mem);
		container = (CItem *)mem;
	} else {
		container = NULL;
	}
	this->container2 = container;
	((CContainer *)this->container2)->lockOwner = this;
	CEntity_SetBodyType(this->container2, 0x1E5E);
	CItem_SetWeight(this->container2, 0);
	CLocation_Set(&tmpLoc, -1, -1, 0);
	((void (*)(void *, CItem *, CLocation *))VT_FN(this->container2, VT_ADD_TO_CONTAINER))(this->container2, (CItem *)player2, &tmpLoc);

	name = ((const char *(*)(void *))VT_FN((CItem *)player2, VT_GET_NAME))((CItem *)player2);
	PacketManager_MakePacket_TRADE(obuf, 0, ((CItem *)player2)->serial, this->container1->serial, this->container2->serial, name);
	SendToClient((CItem *)player1, obuf, -1);

	// Container serials are swapped for player2's view.
	name = ((const char *(*)(void *))VT_FN((CItem *)player1, VT_GET_NAME))((CItem *)player1);
	PacketManager_MakePacket_TRADE(obuf, 0, ((CItem *)player1)->serial, this->container2->serial, this->container1->serial, name);
	SendToClient((CItem *)player2, obuf, -1);

	CLocation_Set(&tmpLoc, -1, -1, 0);
	((void (*)(void *, CItem *, CLocation *))VT_FN(item, VT_ADD_TO_CONTAINER))(item, this->container2, &tmpLoc);

	return this;
}

/*
 * 0x004D9BFE - CTradeSession::ReturnItems
 *
 * Cancels a trade: returns each container's contents to its owner
 * (equip or drop at feet, gold always to pack), unlinks the session,
 * sends TRADE_CLOSE to both players, and deletes both containers.
 */
void
CTradeSession_ReturnItems(CTradeSession *session)
{
	CItem *child;
	uint8_t obuf[0x30];
	CTradeSession *prev;

	// Validation touch - results ignored.
	CWorld_FindBySerial(g_World, session->container1->serial);
	CWorld_FindBySerial(g_World, session->container2->serial);
	CWorld_FindBySerial(g_World, ((CItem *)session->player1)->serial);
	CWorld_FindBySerial(g_World, ((CItem *)session->player2)->serial);

	while (((CContainer *)session->container1)->contents != NULL) {
		child = ((CContainer *)session->container1)->contents;
		((void (*)(CItem *))VT_FN(child, VT_HIDE))(child);
		if (((int (*)(CItem *, CItem *, int))VT_FN((CItem *)session->player1, VT_EQUIP_ITERATE))((CItem *)session->player1, child, 0) != 0 ||
		        (CEntity_GetBodyType(child) & 0xFFFF) == 0xEED) {
			((void (*)(CItem *, CMobile *))VT_FN(child, VT_ADD_TO_EQUIP))(child, &session->player1->mobile);
		} else {
			((void (*)(CItem *, CLocation *))VT_FN(child, VT_DROP_AT_FEET))(child, CEntity_GetLocation(&((CItem *)session->player1)->resourceEntity.entity));
		}
	}

	while (((CContainer *)session->container2)->contents != NULL) {
		child = ((CContainer *)session->container2)->contents;
		((void (*)(CItem *))VT_FN(child, VT_HIDE))(child);
		if (((int (*)(CItem *, CItem *, int))VT_FN((CItem *)session->player2, VT_EQUIP_ITERATE))((CItem *)session->player2, child, 0) != 0 ||
		        (CEntity_GetBodyType(child) & 0xFFFF) == 0xEED) {
			((void (*)(CItem *, CMobile *))VT_FN(child, VT_ADD_TO_EQUIP))(child, &session->player2->mobile);
		} else {
			((void (*)(CItem *, CLocation *))VT_FN(child, VT_DROP_AT_FEET))(child, CEntity_GetLocation(&((CItem *)session->player2)->resourceEntity.entity));
		}
	}

	if (g_TradeSessionList == session) {
		g_TradeSessionList = session->next;
	} else {
		prev = g_TradeSessionList;
		while (prev->next != session)
			prev = prev->next;
		prev->next = session->next;
	}

	PacketManager_MakePacket_TRADE(obuf, 1, session->container1->serial, 0, 0, "");
	SendToClient((CItem *)session->player1, obuf, -1);
	SendToClient((CItem *)session->player2, obuf, -1);

	((CContainer *)session->container1)->lockOwner = 0;
	if (session->container1 != NULL)
		((void (*)(CItem *))VT_FN(session->container1, VT_DELETE))(session->container1);
	session->container1 = NULL;

	((CContainer *)session->container2)->lockOwner = 0;
	if (session->container2 != NULL)
		((void (*)(CItem *))VT_FN(session->container2, VT_DELETE))(session->container2);
	session->container2 = NULL;
}

/*
 * 0x004D9E57 - SetTradeAcceptState
 *
 * Updates each player's accept flag. When both sides have accepted,
 * triggers the cross-transfer via CTradeSession_MoveItems; otherwise
 * sends a TRADE_UPDATE packet to both players with flags swapped for
 * player2's view.
 */
void
SetTradeAcceptState(CTradeSession *session, uint32_t accept1, uint32_t accept2)
{
	uint8_t obuf[0x30];

	session->accept1 = accept1;
	session->accept2 = accept2;

	if (accept1 != 0 && accept2 != 0) {
		CTradeSession_MoveItems(session);
		return;
	}

	if (session->container1 == NULL)
		return;

	PacketManager_MakePacket_TRADE(obuf, 2, session->container1->serial, accept1, accept2, "");
	SendToClient((CItem *)session->player1, obuf, -1);

	// Flags swapped for player2's view.
	PacketManager_MakePacket_TRADE(obuf, 2, session->container1->serial, accept2, accept1, "");
	SendToClient((CItem *)session->player2, obuf, -1);
}

/*
 * 0x004D9F08 - CTradeSession::MoveItems
 *
 * Cross-transfers container1 items to player2 and container2 items
 * to player1, then closes the session via CloseTrade.
 */
static void
CTradeSession_MoveItems(CTradeSession *session)
{
	CItem *child;
	CTradeSession *sess2, *sess3;

	while (((CContainer *)session->container1)->contents != NULL) {
		child = ((CContainer *)session->container1)->contents;
		((void (*)(CItem *))VT_FN(child, VT_HIDE))(child);
		if (((int (*)(CItem *, CItem *, int))VT_FN((CItem *)session->player2, VT_EQUIP_ITERATE))((CItem *)session->player2, child, 0) != 0 ||
		        (CEntity_GetBodyType(child) & 0xFFFF) == 0xEED) {
			((void (*)(CItem *, CMobile *))VT_FN(child, VT_ADD_TO_EQUIP))(child, &session->player2->mobile);
		} else {
			((void (*)(CItem *, CLocation *))VT_FN(child, VT_DROP_AT_FEET))(child, CEntity_GetLocation(&((CItem *)session->player2)->resourceEntity.entity));
		}
	}

	while (((CContainer *)session->container2)->contents != NULL) {
		child = ((CContainer *)session->container2)->contents;
		((void (*)(CItem *))VT_FN(child, VT_HIDE))(child);
		if (((int (*)(CItem *, CItem *, int))VT_FN((CItem *)session->player1, VT_EQUIP_ITERATE))((CItem *)session->player1, child, 0) != 0 ||
		        (CEntity_GetBodyType(child) & 0xFFFF) == 0xEED) {
			((void (*)(CItem *, CMobile *))VT_FN(child, VT_ADD_TO_EQUIP))(child, &session->player1->mobile);
		} else {
			((void (*)(CItem *, CLocation *))VT_FN(child, VT_DROP_AT_FEET))(child, CEntity_GetLocation(&((CItem *)session->player1)->resourceEntity.entity));
		}
	}

	sess2 = session;
	sess3 = sess2;
	if (sess3 != NULL)
		CloseTrade(sess3, 1);
}
