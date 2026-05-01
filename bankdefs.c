/*
 * ResBank terrain-set definitions.
 *
 * Loads bankdefs.txt / bankdefs.mul into CResBankSet / Member /
 * Vertex records describing tile patterns and their connectivity, and
 * exposes the placement helpers the terrain generator and spawn
 * pipeline use to lay them down.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "bankdefs.h"
#include "container.h"
#include "dat.h"
#include "gmedit.h"
#include "io.h"
#include "region.h"
#include "utils.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "world.h"

static void ResBankDistrib_CalcFillStep(void); // 0x004A0509

// 0x006DA968 - Global CResBankSet linked list head.
CResBankSet *g_ResBankSetListHead;

// 0x006DA964 - Global CResBankVertex linked list head.
CResBankVertex *g_ResBankVertexListHead;

// 0x00620390 - Default spawn tile count.
int g_ResBankSpawnDefault = 4;

// 0x006DA950 - Cubic spline step parameter (binary CRT computes 1.0/16.0).
double g_ResBankFillStep = 1.0 / 16.0;

// 0x006DA95C, 0x006DA960: Line drawing state.
int g_ResBankLineX;
int g_ResBankLineY;

/*
 * 0x0049E9DE - CResBankVertex::GetNext
 *
 * Returns the next vertex in the distribution chain, wrapping to the
 * parent entry's headVertex at the tail.
 */
CDistribVertex *
CResBankVertex_GetNext(CDistribVertex *dv)
{
	if (dv->next != NULL)
		return dv->next;
	return ((CDistribEntry *)dv->parent)->headVertex;
}

/*
 * 0x0049EA03 - ResBankSet_FindByTypeAndColor
 *
 * Walks the global set list to find a set matching both type and color.
 * Returns the set pointer or NULL.
 */
CResBankSet *
ResBankSet_FindByTypeAndColor(int type, int color)
{
	CResBankSet *set;

	set = g_ResBankSetListHead;
	while (set != NULL) {
		if ((uint16_t)set->color == color && set->type == type)
			return set;
		set = set->next;
	}
	return NULL;
}

/*
 * 0x0049EA48 - ResBankSet_LookupByIndex
 *
 * Returns the set 'index' positions back from the list tail, or NULL.
 */
CResBankSet *
ResBankSet_LookupByIndex(int index)
{
	CResBankSet *set;
	int i;

	set = g_ResBankSetListHead;
	while (set != NULL && set->next != NULL)
		set = set->next;

	i = 0;
	while (i < index) {
		if (set != NULL)
			set = set->prev;
		i++;
	}
	return set;
}

/*
 * 0x0049EAAD - ResBankSet_LookupByName
 *
 * Returns the position (counted from the list tail) of the set with
 * the given name.
 */
int
ResBankSet_LookupByName(const char *name)
{
	CResBankSet *set;
	int i;

	set = g_ResBankSetListHead;
	while (set != NULL && set->next != NULL)
		set = set->next;

	i = 0;
	while (set != NULL) {
		if (strcasecmp(set->name, name) == 0)
			break;
		i++;
		set = set->prev;
	}
	return i;
}

/*
 * 0x0049EB23 - CResBankSet::DumpSetInfo
 *
 * Loads a CResBankSet from bankdefs.txt (text) or bankdefs.mul (binary),
 * loads its members, and inserts the set at the head of the global list.
 */
void
CResBankSet_DumpSetInfo(CResBankSet *set, FILE *fp, int textMode)
{
	char typeBuf[128];
	int color;
	int hue;
	int memberCount;
	int i;
	CResBankSetMember *member;

	if (textMode) {
		fscanf(fp, "\nSet      %s\nType     %s\nColor    %d\nHue      %d\n", set->name, typeBuf, &color, &hue);

		for (i = 0; i < 0x80; i++) {
			if (set->name[i] == '_')
				set->name[i] = ' ';
		}

		set->type = 0x29A;

		if (strcasecmp(typeBuf, "WALL") == 0)
			set->type = 0;
		else if (strcasecmp(typeBuf, "HOUSE") == 0)
			set->type = 1;
		else if (strcasecmp(typeBuf, "TREE") == 0)
			set->type = 2;
		else if (strcasecmp(typeBuf, "TERRAIN") == 0)
			set->type = 3;
		else if (strcasecmp(typeBuf, "ROOF") == 0)
			set->type = 4;
		else if (strcasecmp(typeBuf, "FLATROOF") == 0)
			set->type = 5;
		else if (strcasecmp(typeBuf, "COASTLINE") == 0)
			set->type = 6;
		else if (strcasecmp(typeBuf, "TRANSITION") == 0)
			set->type = 7;

		set->color = color;
		set->hue = hue;

		fscanf(fp, "Members  %d\n", &memberCount);
	} else {
		fread_ServerSide(&set->type, 4, 1, fp);
		SwapEndian(&set->type);
		fread_ServerSide(&set->hue, 2, 1, fp);
		SwapEndian(&set->hue);
		fread_ServerSide(&set->color, 2, 1, fp);
		SwapEndian(&set->color);
		fread_ServerSide(set->name, 1, 0x80, fp);
		fread_ServerSide(&memberCount, 4, 1, fp);
		SwapEndian(&memberCount);
	}

	set->memberCount = 0;
	set->memberTail = NULL;

	for (i = 0; i < memberCount; i++) {
		member = (CResBankSetMember *)OperatorNew(sizeof(CResBankSetMember));
		if (member != NULL)
			CResBankSetMember_DumpInfo(member, set, fp, textMode);
	}

	set->next = g_ResBankSetListHead;
	if (set->next != NULL)
		set->next->prev = set;
	set->prev = NULL;
	g_ResBankSetListHead = set;
}

/*
 * 0x0049EF98 - CResBankSet::ContainsMember
 *
 * Returns 1 if the set has a member with matching tileId and subtype.
 */
int
CResBankSet_ContainsMember(CResBankSet *set, int tileId, int subtype)
{
	CResBankSetMember *cur;

	cur = set->memberTail;
	while (cur != NULL) {
		if (cur->subtype == subtype && cur->tileId == tileId)
			return 1;
		cur = cur->prev;
	}
	return 0;
}

/*
 * 0x0049EFE7 - CResBankSet::HasBodyType
 *
 * Returns 1 if any member with subtype in [1,3] or [5,7] has the given
 * tile ID.
 */
int
CResBankSet_HasBodyType(CResBankSet *set, int bodyType)
{
	CResBankSetMember *member;

	member = set->memberTail;
	while (member != NULL) {
		if (member->subtype >= 8)
			goto next;
		if (member->subtype == 0)
			goto next;
		if (member->subtype == 4)
			goto next;
		if (member->tileId == bodyType)
			return 1;
next:
		member = member->prev;
	}
	return 0;
}

/*
 * 0x0049F044 - CResBankSet::HasTransitionType
 *
 * Returns 1 if any member with the given tile ID has a transition subtype
 * (3 or 7).
 */
int
CResBankSet_HasTransitionType(CResBankSet *set, int bodyType)
{
	CResBankSetMember *member;

	member = set->memberTail;
	while (member != NULL) {
		if (member->tileId == bodyType) {
			if (member->subtype == 3 || member->subtype == 7)
				return 1;
		}
		member = member->prev;
	}
	return 0;
}

/*
 * 0x0049F099 - CResBankSet::GetMemberIndex
 *
 * Returns the subtype of the member with the given tile ID, or 0.
 */
int
CResBankSet_GetMemberIndex(CResBankSet *set, int bodyType)
{
	CResBankSetMember *member;

	member = set->memberTail;
	while (member != NULL) {
		if (member->tileId == bodyType)
			return member->subtype;
		member = member->prev;
	}
	return 0;
}

/*
 * 0x0049F0DE - CResBankSet::GetTypeMember
 *
 * Returns a random member with the given subtype, or NULL if none exist.
 */
CResBankSetMember *
CResBankSet_GetTypeMember(CResBankSet *set, int subtype)
{
	int count;
	CResBankSetMember *member;
	CResBankSetMember *result;

	count = 0;
	member = set->memberTail;
	while (member != NULL) {
		if (member->subtype == subtype)
			count++;
		member = member->prev;
	}

	if (count == 0)
		return NULL;

	count = rand() % count;

	result = set->memberTail;
	while (result != NULL) {
		if (result->subtype == subtype) {
			if (count == 0)
				return result;
			count--;
		}
		result = result->prev;
	}
	return NULL;
}

/*
 * 0x0049F17D - CResBankSetMember::DumpInfo
 *
 * Loads a CResBankSetMember from bankdefs.txt (text) or bankdefs.mul
 * (binary), then appends it to the parent set's member chain.
 */
void
CResBankSetMember_DumpInfo(CResBankSetMember *member, CResBankSet *parent, FILE *fp, int textMode)
{
	char typeBuf[128];
	int tileId;
	int parentType;
	int i;

	member->parent = parent;

	if (textMode) {
		fscanf(fp, "  Name   %s\n", member->name);

		for (i = 0; i < 0x80; i++) {
			if (member->name[i] == '_')
				member->name[i] = ' ';
		}

		fscanf(fp, "    Type %s\n", typeBuf);

		parentType = parent->type;

		switch (parentType) {
		case 0: // WALL
			if (strcasecmp(typeBuf, "NS(/)") == 0)
				member->subtype = 0;
			else if (strcasecmp(typeBuf, "EW(\\)") == 0)
				member->subtype = 1;
			else if (strcasecmp(typeBuf, "N_Corner") == 0)
				member->subtype = 2;
			else if (strcasecmp(typeBuf, "S_Corner") == 0)
				member->subtype = 7;
			else if (strcasecmp(typeBuf, "Roof_Bit_1") == 0)
				member->subtype = 0xA;
			else if (strcasecmp(typeBuf, "Roof_Bit_2") == 0)
				member->subtype = 0xB;
			else
				member->subtype = 0xC;
			break;

		case 4: // ROOF
			if (strcasecmp(typeBuf, "Piece_1") == 0)
				member->subtype = 0;
			else if (strcasecmp(typeBuf, "Piece_2") == 0)
				member->subtype = 1;
			else if (strcasecmp(typeBuf, "Piece_3") == 0)
				member->subtype = 2;
			else if (strcasecmp(typeBuf, "Piece_4") == 0)
				member->subtype = 3;
			else if (strcasecmp(typeBuf, "Piece_5") == 0)
				member->subtype = 4;
			else if (strcasecmp(typeBuf, "Piece_6") == 0)
				member->subtype = 5;
			else if (strcasecmp(typeBuf, "Piece_7") == 0)
				member->subtype = 6;
			else if (strcasecmp(typeBuf, "Piece_8") == 0)
				member->subtype = 7;
			else if (strcasecmp(typeBuf, "SW_Join") == 0)
				member->subtype = 8;
			else if (strcasecmp(typeBuf, "NE_Join") == 0)
				member->subtype = 9;
			else if (strcasecmp(typeBuf, "NW_Join") == 0)
				member->subtype = 0xA;
			else if (strcasecmp(typeBuf, "SE_Join") == 0)
				member->subtype = 0xB;
			else if (strcasecmp(typeBuf, "X_Join") == 0)
				member->subtype = 0xC;
			else if (strcasecmp(typeBuf, "N_T") == 0)
				member->subtype = 0xD;
			else if (strcasecmp(typeBuf, "S_T") == 0)
				member->subtype = 0xE;
			else if (strcasecmp(typeBuf, "W_T") == 0)
				member->subtype = 0xF;
			else if (strcasecmp(typeBuf, "E_T") == 0)
				member->subtype = 0x10;
			else
				member->subtype = 0x11;
			break;

		case 6: // COASTLINE
			if (strcasecmp(typeBuf, "Bank_TL-BR") == 0)
				member->subtype = 0;
			else if (strcasecmp(typeBuf, "Bank_TR-BL") == 0)
				member->subtype = 1;
			else if (strcasecmp(typeBuf, "Bank_T-B") == 0)
				member->subtype = 2;
			else if (strcasecmp(typeBuf, "Edge_TL-Bank_BR_1") == 0)
				member->subtype = 3;
			else if (strcasecmp(typeBuf, "Edge_TL-Bank_BR_2") == 0)
				member->subtype = 4;
			else if (strcasecmp(typeBuf, "Edge_TR-Bank_BL_1") == 0)
				member->subtype = 5;
			else if (strcasecmp(typeBuf, "Edge_TR-Bank_BL_2") == 0)
				member->subtype = 6;
			else if (strcasecmp(typeBuf, "Edge_BL-Bank_TR_1") == 0)
				member->subtype = 7;
			else if (strcasecmp(typeBuf, "Edge_BL-Bank_TR_2") == 0)
				member->subtype = 8;
			else if (strcasecmp(typeBuf, "Edge_BR-Bank_TL_1") == 0)
				member->subtype = 9;
			else if (strcasecmp(typeBuf, "Edge_BR-Bank_TL_2") == 0)
				member->subtype = 0xA;
			else if (strcasecmp(typeBuf, "Edge_T-Bank_B") == 0)
				member->subtype = 0xB;
			else if (strcasecmp(typeBuf, "Edge_L-Bank_R") == 0)
				member->subtype = 0xC;
			else if (strcasecmp(typeBuf, "Edge_R-Bank_L") == 0)
				member->subtype = 0xD;
			else if (strcasecmp(typeBuf, "Edge_B-Bank_T") == 0)
				member->subtype = 0xE;
			else if (strcasecmp(typeBuf, "Ravine_Width") == 0)
				member->subtype = 0xF;
			else if (strcasecmp(typeBuf, "Ravine_Floor_Tile") == 0)
				member->subtype = 0x10;
			else
				member->subtype = 0x11;
			break;

		case 7: // TRANSITION
			if (strcasecmp(typeBuf, "Tile_1") == 0)
				member->subtype = 0;
			else if (strcasecmp(typeBuf, "Tile_2") == 0)
				member->subtype = 1;
			else if (strcasecmp(typeBuf, "2_Corner_Top") == 0)
				member->subtype = 2;
			else if (strcasecmp(typeBuf, "2_Corner_Right") == 0)
				member->subtype = 3;
			else if (strcasecmp(typeBuf, "2_Corner_Bottom") == 0)
				member->subtype = 4;
			else if (strcasecmp(typeBuf, "2_Corner_Left") == 0)
				member->subtype = 5;
			else if (strcasecmp(typeBuf, "1_Corner_Top") == 0)
				member->subtype = 6;
			else if (strcasecmp(typeBuf, "1_Corner_Right") == 0)
				member->subtype = 7;
			else if (strcasecmp(typeBuf, "1_Corner_Bottom") == 0)
				member->subtype = 8;
			else if (strcasecmp(typeBuf, "1_Corner_Left") == 0)
				member->subtype = 9;
			else if (strcasecmp(typeBuf, "2_TR-1_BL") == 0)
				member->subtype = 0xA;
			else if (strcasecmp(typeBuf, "1_TL-2_BR") == 0)
				member->subtype = 0xB;
			else if (strcasecmp(typeBuf, "1_TR-2_BL") == 0)
				member->subtype = 0xC;
			else if (strcasecmp(typeBuf, "2_TL-1_BR") == 0)
				member->subtype = 0xD;
			else
				member->subtype = 0xE;
			break;

		default:
			// Cases 1,2,3,5: no subtype parsing
			break;
		}

		fscanf(fp, "    Tile %d\n", &tileId);
		member->tileId = tileId;
	} else {
		fread_ServerSide(&member->subtype, 4, 1, fp);
		SwapEndian(&member->subtype);
		fread_ServerSide(&member->tileId, 4, 1, fp);
		SwapEndian(&member->tileId);
		fread_ServerSide(member->name, 1, 0x80, fp);
	}

	// Append to parent's member chain.
	member->prev = parent->memberTail;
	if (member->prev != NULL)
		member->prev->next = member;
	member->next = NULL;
	parent->memberTail = member;
	parent->memberCount++;
}

/*
 * 0x0049FC46 - CResBankSetMember::RemoveFromSpatialList
 *
 * Unlinks the member from its parent's chain and decrements memberCount.
 */
void
CResBankSetMember_RemoveFromSpatialList(CResBankSetMember *member)
{
	if (member->prev != NULL)
		member->prev->next = member->next;
	if (member->next != NULL) {
		member->next->prev = member->prev;
	} else {
		member->parent->memberTail = member->prev;
	}
	member->parent->memberCount = member->parent->memberCount - 1;
}

/*
 * 0x0049FCD4 - CResBankVertex::DumpInfo
 *
 * Loads a CResBankVertex from bankdefs.txt (text) or bankdefs.mul (binary)
 * and inserts it at the head of the global vertex list.
 */
void
CResBankVertex_DumpInfo(CResBankVertex *vertex, FILE *fp, int textMode)
{
	char nameBuf[128];
	int parsedX, parsedY;
	int i;

	if (textMode) {
		fscanf(fp, "\nVertex %d\n  Set  %s\n  xy   %d %d\n  nswe %d %d %d %d\n", &vertex->vertexIndex, nameBuf, &parsedX, &parsedY, &vertex->n, &vertex->s, &vertex->w,
		        &vertex->e);

		for (i = 0; i < 0x80; i++) {
			if (nameBuf[i] == '_')
				nameBuf[i] = ' ';
		}

		vertex->x = (int16_t)parsedX;
		vertex->y = (int16_t)parsedY;
		vertex->setIndex = ResBankSet_LookupByName(nameBuf);
	} else {
		fread_ServerSide(&vertex->x, 2, 1, fp);
		SwapEndian(&vertex->x);
		fread_ServerSide(&vertex->y, 2, 1, fp);
		SwapEndian(&vertex->y);
		fread_ServerSide(&vertex->setIndex, 4, 1, fp);
		SwapEndian(&vertex->setIndex);
		fread_ServerSide(&vertex->vertexIndex, 4, 1, fp);
		SwapEndian(&vertex->vertexIndex);
		fread_ServerSide(&vertex->n, 4, 1, fp);
		SwapEndian(&vertex->n);
		fread_ServerSide(&vertex->s, 4, 1, fp);
		SwapEndian(&vertex->s);
		fread_ServerSide(&vertex->w, 4, 1, fp);
		SwapEndian(&vertex->w);
		fread_ServerSide(&vertex->e, 4, 1, fp);
		SwapEndian(&vertex->e);
	}

	vertex->listNext = g_ResBankVertexListHead;
	if (vertex->listNext != NULL)
		vertex->listNext->listPrev = vertex;
	vertex->listPrev = NULL;
	g_ResBankVertexListHead = vertex;
}

/*
 * 0x0049FFA2 - ResBankVertex_LookupByIndex
 *
 * Returns the vertex with the given vertexIndex, or NULL.
 */
CResBankVertex *
ResBankVertex_LookupByIndex(int vertexIndex)
{
	CResBankVertex *vertex;

	vertex = g_ResBankVertexListHead;
	while (vertex != NULL) {
		if (vertex->vertexIndex == vertexIndex)
			return vertex;
		vertex = vertex->listNext;
	}
	return NULL;
}

/*
 * 0x0049FFD7 - CResBankVertex::ValidateConnections
 *
 * Returns 1 if every NSWE neighbor points back with matching coordinates.
 */
int
CResBankVertex_ValidateConnections(CResBankVertex *vertex)
{
	CResBankVertex *found;
	CResBankSet *set;

	set = ResBankSet_LookupByIndex(vertex->setIndex);
	if (set == NULL)
		return 0;

	found = ResBankVertex_LookupByIndex(vertex->n);
	if (found != NULL) {
		if (found->s != vertex->vertexIndex)
			return 0;
		if ((uint16_t)found->x != (uint16_t)vertex->x)
			return 0;
		if ((uint16_t)found->y >= (uint16_t)vertex->y)
			return 0;
	}

	found = ResBankVertex_LookupByIndex(vertex->s);
	if (found != NULL) {
		if (found->n != vertex->vertexIndex)
			return 0;
		if ((uint16_t)found->x != (uint16_t)vertex->x)
			return 0;
		if ((uint16_t)found->y <= (uint16_t)vertex->y)
			return 0;
	}

	found = ResBankVertex_LookupByIndex(vertex->w);
	if (found != NULL) {
		if (found->e != vertex->vertexIndex)
			return 0;
		if ((uint16_t)found->y != (uint16_t)vertex->y)
			return 0;
		if ((uint16_t)found->x >= (uint16_t)vertex->x)
			return 0;
	}

	found = ResBankVertex_LookupByIndex(vertex->e);
	if (found != NULL) {
		if (found->w != vertex->vertexIndex)
			return 0;
		if ((uint16_t)found->y != (uint16_t)vertex->y)
			return 0;
		if ((uint16_t)found->x <= (uint16_t)vertex->x)
			return 0;
	}

	return 1;
}

/*
 * 0x004A0159 - CResBankVertex::LinkConnections
 *
 * Sets up bidirectional NSWE links to the vertex's neighbors.
 */
void
CResBankVertex_LinkConnections(CResBankVertex *vertex)
{
	CResBankVertex *found;

	found = ResBankVertex_LookupByIndex(vertex->n);
	if (found != NULL)
		found->s = vertex->vertexIndex;

	found = ResBankVertex_LookupByIndex(vertex->s);
	if (found != NULL)
		found->n = vertex->vertexIndex;

	found = ResBankVertex_LookupByIndex(vertex->w);
	if (found != NULL)
		found->e = vertex->vertexIndex;

	found = ResBankVertex_LookupByIndex(vertex->e);
	if (found != NULL)
		found->w = vertex->vertexIndex;
}

/*
 * 0x004A01F6 - ResBankDistrib_MarkBlock
 *
 * Marks every block within g_ResBankSpawnDefault of (x,y): sets flags
 * 0x60 on the cell, and for cells inside the circular radius calls
 * SetTerrainTile with elevation -666.
 *
 * Note: the binary passes vertex (arg 2) instead of mode (arg 1) as the
 * first argument to SetTerrainTile - apparent bug in UoDemo.exe.
 * Preserved here.
 */
void
ResBankDistrib_MarkBlock(int mode, CEntity *entity, int x, int y)
{
	int dx, dy;
	int blockIdx;
	int distSq;
	USED(mode);

	for (dx = -g_ResBankSpawnDefault; dx <= g_ResBankSpawnDefault; dx++) {
		for (dy = -g_ResBankSpawnDefault; dy <= g_ResBankSpawnDefault; dy++) {
			if (!CBlockManager_IsValidCoord(&g_SpatialGrid, dx + x, dy + y))
				continue;

			blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, dx + x, dy + y, 0);
			g_SpatialGrid.cells[blockIdx].flags111 |= 0x60;

			distSq = dx * dx + dy * dy;
			if (sqrt((double)distSq) >= (double)g_ResBankSpawnDefault)
				continue;

			SetTerrainTile((intptr_t)entity, dx + x, dy + y, (int)0xFFFFFD66, -15);
		}
	}
}

/*
 * 0x004A030F - ResBankDistrib_SetStartPoint
 *
 * Stores the entity-relative (x,y) starting point for line drawing
 * into g_ResBankLineX/Y.
 */
void
ResBankDistrib_SetStartPoint(int mode, CEntity *entity, double x, double y)
{
	USED(mode);
	g_ResBankLineX = (int)x + (int)entity->location.x;
	g_ResBankLineY = (int)y + (int)entity->location.y;
}

/*
 * 0x004A0340 - ResBankDistrib_DrawLine
 *
 * Draws a line from g_ResBankLineX/Y to the new (x,y), calling MarkBlock
 * at each step, then updates g_ResBankLineX/Y to the endpoint.
 */
void
ResBankDistrib_DrawLine(int mode, CEntity *entity, double x, double y)
{
	int newX, newY;
	int absDx, absDy;
	int stepX, stepY;
	int curX, curY;
	float curYf, curXf;
	float slopeYperX, slopeXperY;
	int diffY, diffX;
	int i;

	newX = (int)x + (int)entity->location.x;
	newY = (int)y + (int)entity->location.y;

	absDx = abs(newX - g_ResBankLineX);
	absDy = abs(newY - g_ResBankLineY);

	if (absDx == 0 && absDy == 0) {
		ResBankDistrib_MarkBlock(mode, entity, newX, newY);
		goto done;
	}

	if (absDx >= absDy) {
		curYf = (float)g_ResBankLineY;
		curX = g_ResBankLineX;
		stepX = (newX - g_ResBankLineX <= 0) ? -1 : 1;
		diffY = newY - g_ResBankLineY;
		slopeYperX = (float)diffY / (float)absDx;

		for (i = 0; i < absDx; i++) {
			ResBankDistrib_MarkBlock(mode, entity, curX, (int)curYf);
			curX += stepX;
			curYf += slopeYperX;
		}
	} else {
		curXf = (float)g_ResBankLineX;
		curY = g_ResBankLineY;
		stepY = (newY - g_ResBankLineY <= 0) ? -1 : 1;
		diffX = newX - g_ResBankLineX;
		slopeXperY = (float)diffX / (float)absDy;

		for (i = 0; i < absDy; i++) {
			ResBankDistrib_MarkBlock(mode, entity, (int)curXf, curY);
			curXf += slopeXperY;
			curY += stepY;
		}
	}

done:
	g_ResBankLineX = newX;
	g_ResBankLineY = newY;
}

/*
 * 0x004A0509 - ResBankDistrib_CalcFillStep
 *
 * Stores 1/16 into g_ResBankFillStep.
 */
static __attribute__((unused)) void
ResBankDistrib_CalcFillStep(void)
{
	g_ResBankFillStep = 1.0 / 16.0;
}

/*
 * 0x004A0520 - ResBankDistrib_FillQuad
 *
 * Evaluates a cubic B-spline through 4 control points and fills the
 * curve by calling DrawLine at 16 steps.
 */
void
ResBankDistrib_FillQuad(int mode, CEntity *entity, double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4)
{
	double midX1, slopeX1, midX2, slopeX2;
	double midY1, slopeY1, midY2, slopeY2;
	double t;
	double evalX, evalY;
	int i;

	midX1 = (4.0 * x2 + x1 + x3) / 6.0;
	slopeX1 = (x3 - x1) / 2.0;
	midY1 = (4.0 * y2 + y1 + y3) / 6.0;
	slopeY1 = (y3 - y1) / 2.0;
	midX2 = (x1 - 2.0 * x2 + x3) / 2.0;
	slopeX2 = (x4 - x1 + (x2 - x3) * 3.0) / 6.0;
	midY2 = (y1 - 2.0 * y2 + y3) / 2.0;
	slopeY2 = (y4 - y1 + (y2 - y3) * 3.0) / 6.0;

	t = g_ResBankFillStep;

	ResBankDistrib_SetStartPoint(mode, entity, midX1, midY1);

	for (i = 0; i < 15; i++) {
		evalX = ((slopeX2 * t + midX2) * t + slopeX1) * t + midX1;
		evalY = ((slopeY2 * t + midY2) * t + slopeY1) * t + midY1;
		ResBankDistrib_DrawLine(mode, entity, evalX, evalY);
		t += g_ResBankFillStep;
	}

	// Final step after loop.
	evalX = ((slopeX2 * t + midX2) * t + slopeX1) * t + midX1;
	evalY = ((slopeY2 * t + midY2) * t + slopeY1) * t + midY1;
	ResBankDistrib_DrawLine(mode, entity, evalX, evalY);
}

/*
 * 0x004A06D9 - CResBankManager::ProcessEntry
 *
 * Processes one distrib entry: walks the vertex chain as a polygon fill,
 * line, or curve, then scans spatial grid blocks to replace tiles whose
 * Z-quad matches a g_TerrainRules entry with the set's tile.
 */
void
CResBankManager_ProcessEntry(CDistribEntry *entry, CEntity *entity)
{
	int flag = entry->flag;
	int count = entry->count;
	CDistribVertex *vertex = entry->headVertex;
	CResBankSet *set;
	CResBankSetMember *member;
	int tileTable[17];
	int i;
	int blockX, blockY;
	int x, y;
	int *zQuad;
	CDistribVertex *v2, *v3, *v4;

	set = ResBankSet_LookupByIndex(vertex->setIndex);
	if (set == NULL)
		return;

	member = CResBankSet_GetTypeMember(set, 0xF);
	if (member != NULL)
		g_ResBankSpawnDefault = member->tileId;
	else
		g_ResBankSpawnDefault = 4;

	if (flag != 0) {
		for (i = 0; i < count; i++) {
			v2 = CResBankVertex_GetNext(vertex);
			v3 = CResBankVertex_GetNext(v2);
			v4 = CResBankVertex_GetNext(v3);
			ResBankDistrib_FillQuad(vertex->setIndex, entity, vertex->x, vertex->y, v2->x, v2->y, v3->x, v3->y, v4->x, v4->y);
			vertex = vertex->next;
		}
	} else if (count < 4) {
		ResBankDistrib_SetStartPoint(vertex->setIndex, entity, vertex->x, vertex->y);
		vertex = vertex->next;
		while (vertex != NULL) {
			ResBankDistrib_DrawLine(vertex->setIndex, entity, vertex->x, vertex->y);
			vertex = vertex->next;
		}
	} else {
		ResBankDistrib_SetStartPoint(vertex->setIndex, entity, vertex->x, vertex->y);

		v2 = vertex->next;
		v3 = v2->next;
		double midX = (v2->x * 4.0 + vertex->x + v3->x) / 6.0;
		double midY = (vertex->y + v2->y * 4.0 + v3->y) / 6.0;
		ResBankDistrib_DrawLine(vertex->setIndex, entity, midX, midY);

		while (v3->next != NULL) {
			v4 = v3->next;
			ResBankDistrib_FillQuad(vertex->setIndex, entity, vertex->x, vertex->y, v2->x, v2->y, v3->x, v3->y, v4->x, v4->y);
			vertex = vertex->next;
			v2 = vertex->next;
			v3 = v2->next;
		}

		v2 = vertex->next;
		v3 = v2->next;
		ResBankDistrib_DrawLine(vertex->setIndex, entity, v3->x, v3->y);
	}

	for (i = 0; i < 0x11; i++) {
		member = CResBankSet_GetTypeMember(set, i);
		if (member != NULL)
			tileTable[i] = member->tileId;
		else
			tileTable[i] = 0;
	}

	for (i = 0; i < g_SpatialGrid.totalBlocks; i++) {
		if (!(g_SpatialGrid.cells[i].flags111 & 0x20))
			continue;
		g_SpatialGrid.cells[i].flags111 &= ~0x20;

		CBlockManager_GetBlockOrigin(&g_SpatialGrid, i, &blockX, &blockY);

		for (y = blockY; y < blockY + 8; y++) {
			for (x = blockX; x < blockX + 8; x++) {
				zQuad = CTerrainManager_GetLandZQuad(x, y);

				int j = 0;
				while (g_TerrainRules[j].setIndex != -1) {
					if (zQuad[0] == g_TerrainRules[j].z0 && zQuad[1] == g_TerrainRules[j].z1 && zQuad[2] == g_TerrainRules[j].z2 &&
					        zQuad[3] == g_TerrainRules[j].z3) {
						if ((rand() & 3) != 0)
							SetTerrainTile((intptr_t)entity, x, y, tileTable[g_TerrainRules[j].setIndex - 1], -666);
						else
							SetTerrainTile((intptr_t)entity, x, y, tileTable[g_TerrainRules[j].altSetIndex - 1], -666);
					}
					j++;
				}
			}
		}
	}
}

/*
 * 0x004A0FAB - ResBankDistrib_PlaceStatic
 *
 * Creates a static entity at (x,y) with the given tileId and hue at the
 * cell's Z, then drops it at feet via vtable[0x04].
 */
void
ResBankDistrib_PlaceStatic(int tileId, int x, int y, int hue)
{
	CItem *item;
	int blockIdx;
	int8_t cellZ;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y))
		return;

	item = (CItem *)CreateStaticEntity();
	CEntity_SetBodyType(item, (uint16_t)tileId);
	item->resourceEntity.entity.location.x = (int16_t)x;
	item->resourceEntity.entity.location.y = (int16_t)y;

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	cellZ = g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].z;
	item->resourceEntity.entity.location.z = (int16_t)cellZ;

	item->resourceEntity.entity.color = (uint16_t)hue;

	((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &item->resourceEntity.entity.location);
}

/*
 * 0x004A105E - ResBankDistrib_PlaceStaticNPC
 *
 * Like PlaceStatic, but places the entity at cell Z plus heightOff.
 */
void
ResBankDistrib_PlaceStaticNPC(int tileId, int x, int y, int hue, int heightOff)
{
	CItem *item;
	int blockIdx;
	int8_t cellZ;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y))
		return;

	item = (CItem *)CreateStaticEntity();
	CEntity_SetBodyType(item, (uint16_t)tileId);
	item->resourceEntity.entity.location.x = (int16_t)x;
	item->resourceEntity.entity.location.y = (int16_t)y;

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	cellZ = g_MapBlocks[blockIdx].cells[(y & 7) * 8 + (x & 7)].z;
	item->resourceEntity.entity.location.z = (int16_t)(heightOff + cellZ);

	item->resourceEntity.entity.color = (uint16_t)hue;

	((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &item->resourceEntity.entity.location);
}

/*
 * 0x004A1115 - ResBankDistrib_ScanBorderX
 *
 * Places member-0 tiles along the X border between two vertices.
 */
void
ResBankDistrib_ScanBorderX(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff)
{
	CResBankSet *set;
	CResBankSetMember *member;
	int i;

	set = ResBankSet_LookupByIndex(lower->setIndex);
	if (set == NULL)
		abort();

	member = CResBankSet_GetTypeMember(set, 0);
	if (member == NULL)
		abort();

	for (i = (uint16_t)lower->y + 1; i < (int)(uint16_t)upper->y; i++) {
		ResBankDistrib_PlaceStatic(member->tileId, xOff + (uint16_t)lower->x, yOff + i, (uint16_t)set->hue);
	}
}

/*
 * 0x004A11B6 - ResBankDistrib_ScanBorderY
 *
 * Places member-1 tiles along the Y border between two vertices.
 */
void
ResBankDistrib_ScanBorderY(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff)
{
	CResBankSet *set;
	CResBankSetMember *member;
	int i;

	set = ResBankSet_LookupByIndex(lower->setIndex);
	if (set == NULL)
		abort();

	member = CResBankSet_GetTypeMember(set, 1);
	if (member == NULL)
		abort();

	for (i = (uint16_t)lower->x + 1; i < (int)(uint16_t)upper->x; i++) {
		ResBankDistrib_PlaceStatic(member->tileId, xOff + i, yOff + (uint16_t)lower->y, (uint16_t)set->hue);
	}
}

/*
 * 0x004A1256 - ResBankDistrib_ScanTransition
 *
 * Places a transition tile at the vertex, selecting the member subtype
 * from the vertex's NWS connectivity pattern.
 */
void
ResBankDistrib_ScanTransition(CResBankVertex *vertex, int xOff, int yOff)
{
	CResBankSet *set;
	CResBankSetMember *member;

	set = ResBankSet_LookupByIndex(vertex->setIndex);
	if (set == NULL)
		abort();

	if (ResBankVertex_LookupByIndex(vertex->n) == NULL && ResBankVertex_LookupByIndex(vertex->w) == NULL) {
		member = CResBankSet_GetTypeMember(set, 2);
		if (member == NULL)
			abort();
	} else if (ResBankVertex_LookupByIndex(vertex->n) != NULL && ResBankVertex_LookupByIndex(vertex->w) != NULL) {
		member = CResBankSet_GetTypeMember(set, 7);
		if (member == NULL)
			abort();
	} else if (ResBankVertex_LookupByIndex(vertex->w) != NULL && ResBankVertex_LookupByIndex(vertex->s) != NULL) {
		member = CResBankSet_GetTypeMember(set, 1);
		if (member == NULL)
			abort();
	} else {
		if (ResBankVertex_LookupByIndex(vertex->n) != NULL || ResBankVertex_LookupByIndex(vertex->s) != NULL) {
			member = CResBankSet_GetTypeMember(set, 0);
			if (member == NULL)
				abort();
		} else {
			member = CResBankSet_GetTypeMember(set, 1);
			if (member == NULL)
				abort();
		}
	}

	ResBankDistrib_PlaceStatic(member->tileId, xOff + (uint16_t)vertex->x, yOff + (uint16_t)vertex->y, (uint16_t)set->hue);
}

/*
 * 0x004A13EF - ResBankManager_ScanNPCsForPlacementX
 *
 * Finds the X-axis vertex pair overlapping (lower, upper) and places
 * wall and roof tiles (members 0xA, 1, 2, 3) across the span.
 */
void
ResBankManager_ScanNPCsForPlacementX(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff)
{
	CResBankVertex *vertex;
	CResBankVertex *found;
	CResBankSet *set;
	CResBankSet *vertexSet;
	CResBankSetMember *member0;
	CResBankSetMember *memberA;
	CResBankSetMember *member1;
	CResBankSetMember *member2;
	CResBankSetMember *member3;
	int baseHeight;
	int span;
	int i, j, k;

	found = NULL;

	vertex = g_ResBankVertexListHead;
	while (vertex != NULL) {
		set = ResBankSet_LookupByIndex(vertex->setIndex);
		if (set != NULL && set->type == 0) {
			if ((uint16_t)vertex->y == (uint16_t)upper->y) {
				found = ResBankVertex_LookupByIndex(vertex->e);
				if (found != NULL) {
					if ((uint16_t)vertex->x < (uint16_t)upper->x && (uint16_t)found->x > (uint16_t)upper->x)
						break;
				}
			}
		}
		vertex = vertex->listNext;
	}

	if (vertex == NULL) {
		vertex = g_ResBankVertexListHead;
		while (vertex != NULL) {
			set = ResBankSet_LookupByIndex(vertex->setIndex);
			if (set != NULL && set->type == 0) {
				if ((uint16_t)vertex->y == (uint16_t)lower->y) {
					found = ResBankVertex_LookupByIndex(vertex->e);
					if (found != NULL) {
						if ((uint16_t)vertex->x < (uint16_t)lower->x && (uint16_t)found->x > (uint16_t)lower->x)
							break;
					}
				}
			}
			vertex = vertex->listNext;
		}
	}

	if (vertex == NULL)
		return;

	set = ResBankSet_LookupByIndex(vertex->setIndex);
	if (set == NULL)
		abort();
	member0 = CResBankSet_GetTypeMember(set, 0);
	if (member0 == NULL)
		abort();

	baseHeight = g_ItemTileData[member0->tileId].quantity;
	span = (uint16_t)found->x - (uint16_t)vertex->x + 1;

	vertexSet = ResBankSet_LookupByIndex(vertex->setIndex);
	memberA = CResBankSet_GetTypeMember(vertexSet, 0xA);

	set = ResBankSet_LookupByIndex(upper->setIndex);
	member1 = CResBankSet_GetTypeMember(set, 1);
	member2 = CResBankSet_GetTypeMember(set, 2);
	member3 = CResBankSet_GetTypeMember(set, 3);

	for (i = 0; i < span / 2; i++) {
		for (j = 0; j < i; j++) {
			if (memberA == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(memberA->tileId, xOff + (uint16_t)found->x - i, yOff + (uint16_t)upper->y, (uint16_t)vertexSet->hue, j * 3 + baseHeight);
			ResBankDistrib_PlaceStaticNPC(memberA->tileId, xOff + (uint16_t)vertex->x + i, yOff + (uint16_t)upper->y, (uint16_t)vertexSet->hue, j * 3 + baseHeight);
		}
		for (k = (uint16_t)lower->y + 1; k < (int)(uint16_t)upper->y + 2; k++) {
			if (member1 == NULL)
				abort();
			if (member2 == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(member1->tileId, xOff + (uint16_t)found->x + 1 - i, yOff + k, (uint16_t)set->hue, i * 3 + baseHeight);
			ResBankDistrib_PlaceStaticNPC(member2->tileId, xOff + (uint16_t)vertex->x + i + 1, yOff + k, (uint16_t)set->hue, i * 3 + baseHeight);
		}
	}

	if (span & 1) {
		for (i = 0; i < span / 2; i++) {
			if (memberA == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(
			        memberA->tileId, xOff + (uint16_t)found->x - span / 2, yOff + (uint16_t)upper->y, (uint16_t)vertexSet->hue, i * 3 + baseHeight);
		}
		for (k = (uint16_t)lower->y + 1; k < (int)(uint16_t)upper->y + 2; k++) {
			if (member3 == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(member3->tileId, xOff + (uint16_t)vertex->x + span / 2 + 1, yOff + k, (uint16_t)set->hue, (span / 2) * 3 + baseHeight);
		}
	}
}

/*
 * 0x004A18D5 - ResBankManager_ScanNPCsForPlacementY
 *
 * Y-axis complement of ScanNPCsForPlacementX using member indices
 * 0xB, 5, 6, 7.
 */
void
ResBankManager_ScanNPCsForPlacementY(CResBankVertex *lower, CResBankVertex *upper, int xOff, int yOff)
{
	CResBankVertex *vertex;
	CResBankVertex *found;
	CResBankSet *set;
	CResBankSet *vertexSet;
	CResBankSetMember *member0;
	CResBankSetMember *memberB;
	CResBankSetMember *member5;
	CResBankSetMember *member6;
	CResBankSetMember *member7;
	int baseHeight;
	int span;
	int i, j, k;

	found = NULL;

	vertex = g_ResBankVertexListHead;
	while (vertex != NULL) {
		set = ResBankSet_LookupByIndex(vertex->setIndex);
		if (set != NULL && set->type == 0) {
			if ((uint16_t)vertex->x == (uint16_t)upper->x) {
				found = ResBankVertex_LookupByIndex(vertex->s);
				if (found != NULL) {
					if ((uint16_t)vertex->y < (uint16_t)upper->y && (uint16_t)found->y > (uint16_t)upper->y)
						break;
				}
			}
		}
		vertex = vertex->listNext;
	}

	if (vertex == NULL) {
		vertex = g_ResBankVertexListHead;
		while (vertex != NULL) {
			set = ResBankSet_LookupByIndex(vertex->setIndex);
			if (set != NULL && set->type == 0) {
				if ((uint16_t)vertex->x == (uint16_t)lower->x) {
					found = ResBankVertex_LookupByIndex(vertex->s);
					if (found != NULL) {
						if ((uint16_t)vertex->y < (uint16_t)lower->y && (uint16_t)found->y > (uint16_t)lower->y)
							break;
					}
				}
			}
			vertex = vertex->listNext;
		}
	}

	if (vertex == NULL)
		return;

	set = ResBankSet_LookupByIndex(vertex->setIndex);
	if (set == NULL)
		abort();
	member0 = CResBankSet_GetTypeMember(set, 0);
	if (member0 == NULL)
		abort();

	baseHeight = g_ItemTileData[member0->tileId].quantity;
	span = (uint16_t)found->y - (uint16_t)vertex->y + 1;

	vertexSet = ResBankSet_LookupByIndex(vertex->setIndex);
	memberB = CResBankSet_GetTypeMember(vertexSet, 0xB);

	set = ResBankSet_LookupByIndex(upper->setIndex);
	member5 = CResBankSet_GetTypeMember(set, 5);
	member6 = CResBankSet_GetTypeMember(set, 6);
	member7 = CResBankSet_GetTypeMember(set, 7);

	for (i = 0; i < span / 2; i++) {
		for (j = 0; j < i; j++) {
			if (memberB == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(memberB->tileId, xOff + (uint16_t)upper->x, yOff + (uint16_t)found->y - i, (uint16_t)vertexSet->hue, j * 3 + baseHeight);
			ResBankDistrib_PlaceStaticNPC(memberB->tileId, xOff + (uint16_t)upper->x, yOff + (uint16_t)vertex->y + i, (uint16_t)vertexSet->hue, j * 3 + baseHeight);
		}
		for (k = (uint16_t)lower->x + 1; k < (int)(uint16_t)upper->x + 2; k++) {
			if (member5 == NULL)
				abort();
			if (member6 == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(member5->tileId, xOff + k, yOff + (uint16_t)found->y + 1 - i, (uint16_t)set->hue, i * 3 + baseHeight);
			ResBankDistrib_PlaceStaticNPC(member6->tileId, xOff + k, yOff + (uint16_t)vertex->y + i + 1, (uint16_t)set->hue, i * 3 + baseHeight);
		}
	}

	if (span & 1) {
		for (i = 0; i < span / 2; i++) {
			if (memberB == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(
			        memberB->tileId, xOff + (uint16_t)upper->x, yOff + (uint16_t)found->y - span / 2, (uint16_t)vertexSet->hue, i * 3 + baseHeight);
		}
		for (k = (uint16_t)lower->x + 1; k < (int)(uint16_t)upper->x + 2; k++) {
			if (member7 == NULL)
				abort();
			ResBankDistrib_PlaceStaticNPC(member7->tileId, xOff + k, yOff + (uint16_t)vertex->y + span / 2 + 1, (uint16_t)set->hue, (span / 2) * 3 + baseHeight);
		}
	}
}

/*
 * 0x004A1DC0 - ResBankDistrib_CreateAndPlace
 *
 * Creates a static entity at (x,y,z) with the given tileId and hue,
 * then drops it at feet via vtable[0x04].
 */
void
ResBankDistrib_CreateAndPlace(int tileId, int x, int y, int z, int hue)
{
	CItem *item;

	item = (CItem *)CreateStaticEntity();
	item->resourceEntity.entity.location.x = (int16_t)x;
	item->resourceEntity.entity.location.y = (int16_t)y;
	item->resourceEntity.entity.location.z = (int16_t)z;
	CEntity_SetBodyType(item, (uint16_t)tileId);
	item->resourceEntity.entity.color = (uint16_t)hue;

	((void (*)(void *, void *))VT_FN(item, VT_DROP_AT_FEET))(item, &item->resourceEntity.entity.location);
}

/*
 * 0x004A1E1B - ResBankDistrib_CheckAdjacentTile
 *
 * Returns 0 if an entity at (x,y) has a transition body type (3 or 7)
 * in the given set, 1 otherwise.
 */
int
ResBankDistrib_CheckAdjacentTile(CResBankSet *set, int x, int y)
{
	int blockIdx;
	CItem *entity;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y))
		return 1;

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	entity = g_SpatialGrid.cells[blockIdx].staticHead;
	while (entity != NULL) {
		if (entity->resourceEntity.entity.location.x == x && entity->resourceEntity.entity.location.y == y) {
			int bodyType = CEntity_GetBodyType(entity) & 0xFFFF;
			if (CResBankSet_HasTransitionType(set, bodyType))
				return 0;
		}
		entity = entity->resourceEntity.nextInContainer;
	}
	return 1;
}

/*
 * 0x004A1EC5 - ResBankManager_SpawnPlacement
 *
 * Collects the two transition tiles at (x,y), removes duplicates below
 * the highest Z, then dispatches to CreateAndPlace with a member index
 * chosen from the two types. Transition pairs (3/7) check four adjacent
 * tiles to pick a directional variant (0xC-0x10).
 */
void
ResBankManager_SpawnPlacement(CItem *unused, CResBankSet *set, int x, int y)
{
	USED(unused);
	int blockIdx;
	int maxZ;
	int npcCount;
	CItem *entity;
	CItem *next;
	int npcCount2;
	int npcTypes[2];
	int npcZ[2];
	int direction;
	CResBankSetMember *member;

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y))
		return;

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);
	maxZ = -127;
	npcCount = 0;

	entity = g_SpatialGrid.cells[blockIdx].staticHead;
	while (entity != NULL) {
		if (entity->resourceEntity.entity.location.x == x && entity->resourceEntity.entity.location.y == y) {
			int bodyType = CEntity_GetBodyType(entity) & 0xFFFF;
			if (CResBankSet_HasBodyType(set, bodyType)) {
				npcCount++;
				int ez = (int16_t)entity->resourceEntity.entity.location.z;
				if (ez > maxZ)
					maxZ = ez;
			}
		}
		entity = entity->resourceEntity.nextInContainer;
	}

	if (npcCount < 2)
		return;

	// Second scan: remove NPCs below highest Z.
	entity = g_SpatialGrid.cells[blockIdx].staticHead;
	while (entity != NULL) {
		next = entity->resourceEntity.nextInContainer;
		if (entity->resourceEntity.entity.location.x == x && entity->resourceEntity.entity.location.y == y) {
			int bodyType = CEntity_GetBodyType(entity) & 0xFFFF;
			if (CResBankSet_HasBodyType(set, bodyType)) {
				int ez = (int16_t)entity->resourceEntity.entity.location.z;
				if (ez < maxZ) {
					npcCount--;
					FreeStaticItem(entity);
				}
			}
		}
		entity = next;
	}

	if (npcCount != 2)
		return;

	npcCount2 = 0;
	entity = g_SpatialGrid.cells[blockIdx].staticHead;
	while (entity != NULL) {
		next = entity->resourceEntity.nextInContainer;
		if (entity->resourceEntity.entity.location.x == x && entity->resourceEntity.entity.location.y == y) {
			int bodyType = CEntity_GetBodyType(entity) & 0xFFFF;
			if (CResBankSet_HasBodyType(set, bodyType)) {
				npcTypes[npcCount2] = CResBankSet_GetMemberIndex(set, bodyType);
				npcZ[npcCount2] = (int16_t)entity->resourceEntity.entity.location.z;
				npcCount2++;
				FreeStaticItem(entity);
			}
		}
		entity = next;
	}

	USED(npcZ);

	if ((npcTypes[0] == 1 && npcTypes[1] == 5) || (npcTypes[0] == 5 && npcTypes[1] == 1)) {
		member = CResBankSet_GetTypeMember(set, 0xB);
		ResBankDistrib_CreateAndPlace(member->tileId, x, y, maxZ, (uint16_t)set->hue);
	}
	if ((npcTypes[0] == 1 && npcTypes[1] == 6) || (npcTypes[0] == 6 && npcTypes[1] == 1)) {
		member = CResBankSet_GetTypeMember(set, 9);
		ResBankDistrib_CreateAndPlace(member->tileId, x, y, maxZ, (uint16_t)set->hue);
	}
	if ((npcTypes[0] == 5 && npcTypes[1] == 2) || (npcTypes[0] == 2 && npcTypes[1] == 5)) {
		member = CResBankSet_GetTypeMember(set, 8);
		ResBankDistrib_CreateAndPlace(member->tileId, x, y, maxZ, (uint16_t)set->hue);
	}
	if ((npcTypes[0] == 2 && npcTypes[1] == 6) || (npcTypes[0] == 6 && npcTypes[1] == 2)) {
		member = CResBankSet_GetTypeMember(set, 0xA);
		ResBankDistrib_CreateAndPlace(member->tileId, x, y, maxZ, (uint16_t)set->hue);
	}
	if ((npcTypes[0] == 3 && npcTypes[1] == 7) || (npcTypes[0] == 7 && npcTypes[1] == 3)) {
		direction = 0xC;
		if (ResBankDistrib_CheckAdjacentTile(set, x, y + 1))
			direction = 0xD;
		else if (ResBankDistrib_CheckAdjacentTile(set, x, y - 1))
			direction = 0xE;
		else if (ResBankDistrib_CheckAdjacentTile(set, x + 1, y))
			direction = 0xF;
		else if (ResBankDistrib_CheckAdjacentTile(set, x - 1, y))
			direction = 0x10;

		member = CResBankSet_GetTypeMember(set, direction);
		ResBankDistrib_CreateAndPlace(member->tileId, x, y, maxZ, (uint16_t)set->hue);
	}
}

/*
 * 0x004A22C6 - ResBankManager_ApplyVertexBorders
 *
 * Three passes over the global vertex list: type-0 sets run the border
 * and transition scans, type-4 sets run the NPC placement scans, and
 * type-4 sets then sweep a +-15 grid around each vertex calling
 * SpawnPlacement. ORPHANED (no callers in the binary).
 */
void
ResBankManager_ApplyVertexBorders(CItem *arg)
{
	CResBankVertex *vertex;
	CResBankVertex *south;
	CResBankSet *set;
	int xOff, yOff;
	int iy, ix;

	xOff = arg->resourceEntity.entity.location.x;
	yOff = arg->resourceEntity.entity.location.y;

	vertex = g_ResBankVertexListHead;
	while (vertex != NULL) {
		set = ResBankSet_LookupByIndex(vertex->setIndex);
		if (set->type != 0) {
			vertex = vertex->listNext;
			continue;
		}

		south = ResBankVertex_LookupByIndex(vertex->s);
		if (south != NULL)
			ResBankDistrib_ScanBorderX(vertex, south, xOff, yOff);

		south = ResBankVertex_LookupByIndex(vertex->e);
		if (south != NULL)
			ResBankDistrib_ScanBorderY(vertex, south, xOff, yOff);

		ResBankDistrib_ScanTransition(vertex, xOff, yOff);
		vertex = vertex->listNext;
	}

	// Pass 2: type 4 - scan NPC placements.
	vertex = g_ResBankVertexListHead;
	while (vertex != NULL) {
		set = ResBankSet_LookupByIndex(vertex->setIndex);
		if (set->type != 4) {
			vertex = vertex->listNext;
			continue;
		}

		south = ResBankVertex_LookupByIndex(vertex->s);
		if (south != NULL)
			ResBankManager_ScanNPCsForPlacementX(vertex, south, xOff, yOff);

		south = ResBankVertex_LookupByIndex(vertex->e);
		if (south != NULL)
			ResBankManager_ScanNPCsForPlacementY(vertex, south, xOff, yOff);

		vertex = vertex->listNext;
	}

	// Pass 3: type 4 - spawn placement in +-15 grid around each vertex.
	vertex = g_ResBankVertexListHead;
	while (vertex != NULL) {
		set = ResBankSet_LookupByIndex(vertex->setIndex);
		if (set->type != 4) {
			vertex = vertex->listNext;
			continue;
		}

		for (iy = (unsigned short)vertex->y + yOff - 15; iy <= (unsigned short)vertex->y + yOff + 15; iy++) {
			for (ix = (unsigned short)vertex->x + xOff - 15; ix <= (unsigned short)vertex->x + xOff + 15; ix++) {
				ResBankManager_SpawnPlacement(arg, ResBankSet_LookupByIndex(vertex->setIndex), ix, iy);
			}
		}

		vertex = vertex->listNext;
	}
}

/*
 * 0x00620468 - Terrain rule table (static data from binary).
 * Each rule: {setIndex, altSetIndex, z0, z1, z2, z3}.
 * Sentinel: setIndex == -1.
 * 0xFFFFFFF1 = -15 is the "don't care" Z value.
 */
// clang-format off
TerrainRule g_TerrainRules[] = {
	{ 1,  1, -15, -15, -15,   0},
	{ 2,  2, -15,   0, -15, -15},
	{ 3,  3,   0, -15, -15, -15},
	{ 3,  3, -15, -15,   0, -15},
	{ 4,  5,   0, -15, -15,   0},
	{ 6,  7,   0,   0, -15, -15},
	{ 8,  9, -15, -15,   0,   0},
	{10, 11, -15,   0,   0, -15},
	{12, 12,   0,   0, -15,   0},
	{13, 13,   0, -15,   0,   0},
	{14, 14,   0,   0,   0, -15},
	{15, 15, -15,   0,   0,   0},
	{17, 17, -15, -15, -15, -15},
	{-1, -1,   0,   0,   0,   0},
};
// clang-format on
