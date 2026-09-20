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
 * Maps (firstChar - 'a') to a case index: 0-4 give "an ", 5 and the
 * default give "a ". The non-5 entries are the five vowels - a, e, i, o
 * and u.
 */
const uint8_t g_articleLookup[21] = { 0, 5, 5, 5, 1, 5, 5, 5, 2, 5, 5, 5, 5, 5, 3, 5, 5, 5, 5, 5, 4 }; // 0x0048ACAD
