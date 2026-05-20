/*
 * Custom - Runtime Feature Flags
 */

#ifndef FEATURE_H_
#define FEATURE_H_

#include <stdint.h>

/*
 * Bitmask selecting which custom server extensions are active. Queried
 * at runtime via feat(FEAT_*); the -features CLI flag writes g_Features.
 */
enum FeatureFlag {
	FEAT_SKILL_LOCK = 1 << 0,
	FEAT_SKILL_MEDITATION = 1 << 1,  // skill 46
	FEAT_SKILL_STEALTH = 1 << 2,     // skill 47
	FEAT_SKILL_REMOVE_TRAP = 1 << 3, // skill 48
	FEAT_ECOLOGY = 1 << 4,
	FEAT_LIGHTS = 1 << 5,
	FEAT_CREATION_COLORS = 1 << 6,
	FEAT_SPAWN_BUDGET = 1 << 7,
	FEAT_SPAWN_STRICT_TAGS = 1 << 8,
	FEAT_SKILL_TRACKING = 1 << 11,
	FEAT_RESOURCE_REGROWTH = 1 << 12,
	FEAT_PERNPC_RESPAWN = 1 << 13,
	FEAT_CHAT = 1 << 14,
	FEAT_BOAT_MAPSWITCH = 1 << 15,
	FEAT_ALL = FEAT_SKILL_LOCK | FEAT_SKILL_MEDITATION | FEAT_SKILL_STEALTH | FEAT_SKILL_REMOVE_TRAP | FEAT_ECOLOGY | FEAT_LIGHTS | FEAT_CREATION_COLORS | FEAT_SPAWN_BUDGET |
	           FEAT_SPAWN_STRICT_TAGS | FEAT_SKILL_TRACKING | FEAT_RESOURCE_REGROWTH | FEAT_PERNPC_RESPAWN | FEAT_CHAT | FEAT_BOAT_MAPSWITCH,
};

extern uint32_t g_Features;

static inline int
feat(uint32_t f)
{
	return (g_Features & f) != 0;
}

int Features_Parse(const char *arg);

#endif
