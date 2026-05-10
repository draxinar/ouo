/*
 * resource_regrowth - gamewide regrowth tick (CUSTOM).
 *
 * Walks every block's itemHead chain once per ~68min and refills
 * type=3 resource nodes at their per-template rate (value2). Skips
 * eggs and mobiles. Restores the missing piece of the retail
 * server's renewable-resource design - the per-template
 * `r=N 3 v1 v2 v3` records carry a non-zero v2 on apple/peach/pear,
 * coconut palm, date palm, and other renewable resources, but the
 * binary's call graph for the demo build never reaches the loop
 * that consumes that v2.
 */

#include <stdint.h>
#include <stdio.h>

#include "blockmanager.h"
#include "container.h"
#include "egg.h"
#include "entity.h"
#include "item.h"
#include "resource_entity.h"
#include "resource_regrowth.h"
#include "vtable.h"
#include "wombat_compile.h"

/*
 * Custom - GrowEntity
 *
 * Drives the entity's type=3 nodes back toward the per-bodyType
 * template's value1 cap. We iterate the template (rather than the
 * entity's `firstChild` list) so that a fully-depleted node - one
 * that `transferResources` removed entirely when its value3 hit
 * the transfer amount - can be restored from scratch. Without this,
 * a tree whose node has been fully consumed by harvesting would
 * have an empty `firstChild` list with no node to grow.
 *
 * For each template node with type=3 and value2>0:
 *   - if the entity has a matching id node: value3 = min(value1,
 *     value3 + value2)
 *   - otherwise: re-add the node with value3 = min(value1, value2)
 *
 * Returns 1 if any node was added or grown (so the caller fires
 * NotifyPostModify), 0 otherwise.
 */
static int
GrowEntity(CItem *item)
{
	uint16_t bodyType;
	ResEntitySlot *slot;
	CResourceNode *t;
	int grew;

	bodyType = (uint16_t)(CEntity_GetBodyType(item) & 0xFFFF);
	slot = &g_ResEntitySlots[bodyType];

	grew = 0;
	for (t = slot->nodeHead; t != NULL; t = t->next) {
		CResourceNode *e;

		if (t->type != 3)
			continue;
		if (t->value2 <= 0)
			continue;
		if (t->id == 0)
			continue;

		e = NULL;
		{
			CResourceNode *n;
			for (n = item->resourceEntity.firstChild; n != NULL; n = n->next) {
				if (n->type != 3)
					continue;
				if (n->id == t->id) {
					e = n;
					break;
				}
			}
		}

		if (e != NULL) {
			int next;
			if (e->value3 >= e->value1)
				continue;
			next = e->value3 + e->value2;
			if (next > e->value1)
				next = e->value1;
			if (next == e->value3)
				continue;
			e->value3 = next;
			grew = 1;
		} else {
			int starting = t->value2;
			if (starting > t->value1)
				starting = t->value1;
			CResourceEntity_AddNode(item, t->id, 3, t->value1, t->value2, starting, 0);
			grew = 1;
		}
	}
	return grew;
}

/*
 * Custom - ResourceRegrowthTick
 *
 * Walks every block's itemHead chain. For each non-egg, non-mobile
 * CItem, calls GrowEntity. If anything grew, wraps the per-entity
 * mutation in NotifyPreModify / NotifyPostModify (matches the
 * idiom used by transferResources at resource_entity.c:200-251).
 *
 * Block-walk shape mirrors ProcessDynamicItems (resbank.c:4436).
 * Containers are not recursed into - type=3 gather nodes live on
 * world entities, not container contents.
 */
void
ResourceRegrowthTick(void)
{
	int blockIdx;

	for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
		CItem *ent;

		ent = g_SpatialGrid.cells[blockIdx].itemHead;
		while (ent != NULL) {
			CItem *next;
			int grew;

			next = ent->spatialNext;

			if (((int (*)(void *))VT_FN(ent, VT_ITEM_CHECK_9C))(ent)) {
				ent = next;
				continue;
			}
			if (VT_IsMobile(ent)) {
				ent = next;
				continue;
			}

			if (ent->resourceEntity.firstChild == NULL) {
				ent = next;
				continue;
			}

			if (ent->resourceEntity.entity.removedFromWorld == 0)
				CResourceEntity_NotifyPreModify(ent);

			grew = GrowEntity(ent);

			if (grew) {
				CResourceEntity_NotifyPostModify(ent);
				if (ent->resourceEntity.entity.removedFromWorld == 0)
					CResourceEntity_NotifyPostModifyIfActive(ent);
			}

			ent = next;
		}
	}
}
