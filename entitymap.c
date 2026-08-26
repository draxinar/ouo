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
