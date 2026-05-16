/*
 * Entity visibility map - per-client "who can I see" table.
 *
 * Tracks which entities each connected player has been sent, so that
 * movement updates can compute the minimal set of appear / disappear /
 * refresh packets when the player or a nearby mobile moves.
 */

#include <stdint.h>

#include "blockmanager.h"
#include "player.h"

// 0x00645AE8
CEntityMap *g_ItemMap;
// 0x0064706C
CEntityMap *g_MobileMap;
// 0x00698974
CEntityMap *g_NPCMap;

/*
 * 0x00457920 - CEntityMap::CollectMovementVisibilityExclude
 *
 * Classifies entities by visibility change for a mover stepping to a
 * new position from an old one, skipping the exclude entity. Callers
 * pass the mover's post-move position first and pre-move position
 * second. removeList: in range of both (stays visible). insertList:
 * in range of the new position only (enters visibility). overlapList:
 * in range of the old position only (leaves visibility).
 */
void
CEntityMap_CollectMovementVisibilityExclude(
        CEntityMap *this, CVector *removeList, CVector *insertList, CVector *overlapList, int newX, int newY, int oldX, int oldY, int range, CItem *exclude)
{
	int startBlockX, endBlockX, startBlockY, endBlockY;
	int blockIdx, rowWidth;
	int bx, by;
	int extent;
	StdPtrNode *iter, *endNode, *copyIter, *tmpIter;

	extent = range + ChebyshevDistXY(newX, newY, oldX, oldY);

	startBlockX = (newX - extent) >> this->blockShift;
	startBlockX -= this->originX;
	endBlockX = (newX + extent) >> this->blockShift;
	endBlockX -= this->originX;

	startBlockY = (newY - extent) >> this->blockShift;
	startBlockY -= this->originY;
	endBlockY = (newY + extent) >> this->blockShift;
	endBlockY -= this->originY;

	if (startBlockX < 0)
		startBlockX = 0;
	if (endBlockX >= this->gridW)
		endBlockX = this->gridW - 1;
	if (startBlockY < 0)
		startBlockY = 0;
	if (endBlockY >= this->gridH)
		endBlockY = this->gridH - 1;

	blockIdx = startBlockY * this->gridW + startBlockX;
	rowWidth = endBlockX - startBlockX + 1;

	for (by = startBlockY; by <= endBlockY; by++) {
		for (bx = startBlockX; bx <= endBlockX; bx++) {
			StdPtrIter_Constructor(&iter);
			StdPtrList_Begin(&this->blocks[blockIdx], &copyIter);
			iter = copyIter;

			while (1) {
				StdPtrList_End(&this->blocks[blockIdx], &endNode);
				StdPtrIter_CopyConstructor(&tmpIter, &endNode);
				if (!StdPtrIter_Neq(&iter, &tmpIter))
					break;

				{
					void *entity = *StdPtrIter_Deref(&iter);
					int distNew;

					if (entity == (void *)exclude)
						goto next_excl;

					distNew = CMobile_DistXY(entity, newX, newY);

					if (distNew <= range) {
						int distOld;

						distOld = CMobile_DistXY(entity, oldX, oldY);

						if (distOld <= range) {
							void *e = *StdPtrIter_Deref(&iter);
							CVector_PushBack(removeList, (uintptr_t)e);
						} else {
							void *e = *StdPtrIter_Deref(&iter);
							CVector_PushBack(insertList, (uintptr_t)e);
						}
					} else {
						int distOld;

						distOld = CMobile_DistXY(entity, oldX, oldY);

						if (distOld <= range) {
							void *e = *StdPtrIter_Deref(&iter);
							CVector_PushBack(overlapList, (uintptr_t)e);
						}
					}
				}

next_excl:
				StdPtrIter_PostInc(&iter, &tmpIter, 0);
			}

			blockIdx++;
		}
		blockIdx += this->gridW - rowWidth;
	}
}
