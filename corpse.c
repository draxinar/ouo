/*
 * CCorpse - container produced when a mobile dies.
 *
 * Builds the corpse from the dead mobile's equipment, prefixes the name
 * with the right article, tracks looters, and runs the decay timer that
 * eventually removes the corpse from the world.
 */

#include <stdint.h>

// 0x006BA830: global buffer for corpse name article construction
char g_CorpseNameBuf[256];

/*
 * Binary lookup table at 0x0048ACAD: maps (firstChar - 'a') to case index.
 * Cases 0-4 -> "an "; case 5/default -> "a ".
 * a=0, e=1, i=2, n=3, s=4 -> "an "; all others -> "a ".
 * Binary table is 19 bytes; indices 19-20 overlap into next function code.
 */
const uint8_t g_articleLookup[21] = { 0, 5, 5, 5, 1, 5, 5, 5, 2, 5, 5, 5, 5, 3, 5, 5, 5, 5, 4, 5, 5 };
