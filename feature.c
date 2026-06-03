/*
 * Feature flags - runtime toggle table for server-only extensions.
 *
 * Parses -features into a bitmask read by feat(FEAT_*) call sites
 * that gate additions beyond the demo binary (skill lock, ecology,
 * lights, etc.). -features none collapses every call site to its
 * binary-match branch.
 *
 * CUSTOM - no binary equivalent.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dat.h"
#include "feature.h"

uint32_t g_Features = FEAT_ALL;

/*
 * One row of the -features command-line name/bit table.
 */
struct FeatEntry {
	const char *name;
	uint32_t flag;
};

// clang-format off
static const struct FeatEntry featTable[] = {
	{ "skill_lock",        FEAT_SKILL_LOCK },
	{ "skill_meditation",  FEAT_SKILL_MEDITATION },
	{ "skill_stealth",     FEAT_SKILL_STEALTH },
	{ "skill_remove_trap", FEAT_SKILL_REMOVE_TRAP },
	{ "ecology",           FEAT_ECOLOGY },
	{ "lights",            FEAT_LIGHTS },
	{ "creation_colors",   FEAT_CREATION_COLORS },
	{ "hint_rumors",       FEAT_HINT_RUMORS },
	{ "spawn_strict_tags", FEAT_SPAWN_STRICT_TAGS },
	{ "closed_economy",    FEAT_CLOSED_ECONOMY },
	{ "skill_tracking",    FEAT_SKILL_TRACKING },
	{ "resource_regrowth", FEAT_RESOURCE_REGROWTH },
	{ "pernpc_respawn",    FEAT_PERNPC_RESPAWN },
	{ "chat",              FEAT_CHAT },
	{ "boat_mapswitch",    FEAT_BOAT_MAPSWITCH },
	{ "autofill_city",     FEAT_AUTOFILL_CITY },
};
// clang-format on

/*
 * Custom - Features_Parse
 *
 * Parses a -features argument: "all", "none", or comma-separated names.
 * Returns 0 on success, -1 if the argument is NULL.
 * Unknown tokens print a warning but do not fail.
 */
int
Features_Parse(const char *arg)
{
	char buf[256];
	char *tok;
	int i;

	if (arg == NULL)
		return -1;

	if (strcmp(arg, "all") == 0) {
		g_Features = FEAT_ALL;
		return 0;
	}
	if (strcmp(arg, "none") == 0) {
		g_Features = 0;
		return 0;
	}

	g_Features = 0;
	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	for (tok = strtok(buf, ","); tok != NULL; tok = strtok(NULL, ",")) {
		int found = 0;
		// "all" composes inside a list: -features all,closed_economy enables
		// every default feature plus the opt-in FEAT_CLOSED_ECONOMY.
		if (strcmp(tok, "all") == 0) {
			g_Features |= FEAT_ALL;
			continue;
		}
		for (i = 0; i < (int)nelem(featTable); i++) {
			if (strcmp(tok, featTable[i].name) == 0) {
				g_Features |= featTable[i].flag;
				found = 1;
				break;
			}
		}
		if (!found)
			fprintf(stderr, "unknown feature: %s\n", tok);
	}

	return 0;
}
