/*
 * SCommandManager - dispatch for .-prefixed staff commands from players.
 *
 * Each command string maps to a Wombat script invocation; the manager
 * parses the speech, looks up the binding, and runs the attached
 * script with the speaking player as the target. Provides the hook
 * that GM tools (.go, .add, .tell) ride on top of.
 */

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "dat.h"

#include "player.h"
#include "wombat_exec.h"

static void *SCommandManager_LookupCommand(char *cmdName); // 0x004478B1
static void SCommandManager_Destructor(SCommandManager *mgr); // 0x00447437

// 0x005EE880 - MSVC vtable array for SCommandManager
static uintptr_t g_vt_SCommandManager[] = { 0 };

/*
 * 0x00447420 - SCommandManager::SCommandManager
 *
 * Sets the vtable pointer.
 */
void
SCommandManager_Constructor(SCommandManager *mgr)
{
	mgr->vtable = g_vt_SCommandManager;
}
/*
 * 0x00447437 - SCommandManager::~SCommandManager
 *
 * Resets the vtable pointer before destruction.
 */
static __attribute__((unused)) void
SCommandManager_Destructor(SCommandManager *mgr)
{
	mgr->vtable = g_vt_SCommandManager;
}

/*
 * 0x0044744B - GetValue
 *
 * Splits "key value" from line: copies the lowercased key into key and
 * returns a pointer to the value portion past the separator.
 */
char *
GetValue(char *line, char *key)
{
	char *value;
	char ch;

	if (line) {
		while (*line && isspace(*line))
			++line;
		while (!isspace(*line) && *line) {
			if (*line < 'A' || *line > 'Z')
				ch = *line;
			else
				ch = *line + ' ';
			*key++ = ch;
			++line;
		}
		*key = 0;
		if (*line)
			++line;
		value = line;
	} else {
		*key = 0;
		value = 0;
	}
	return value;
}

/*
 * 0x00447512 - ObjVar_ParseToken
 *
 * Parses one whitespace-delimited token from input into output and returns
 * the remaining input.
 */
char *
ObjVar_ParseToken(const char *input, char *output)
{
	if (input == NULL) {
		*output = '\0';
		return NULL;
	}

	while (*input != '\0' && isspace((unsigned char)*input))
		input++;

	while (!isspace((unsigned char)*input) && *input != '\0') {
		*output = *input;
		output++;
		input++;
	}

	*output = '\0';

	if (*input != '\0')
		input++;

	return (char *)input;
}

/*
 * 0x004475AB - ParseToken
 *
 * Extracts the next whitespace-delimited token (with double-quote support)
 * into output and returns the remaining input.
 */
char *
SCommand_ParseToken(char *input, char *output)
{
	int inQuotes;

	if (input == NULL) {
		*output = '\0';
		return NULL;
	}
	inQuotes = 0;
	while (*input && isspace((unsigned char)*input))
		input++;
	for (;;) {
		if (inQuotes != 1 && isspace((unsigned char)*input))
			break;
		if (*input == '\0')
			break;
		if (*input == '"') {
			if (inQuotes == 1)
				break;
			inQuotes = 1;
			input++;
			continue;
		}
		*output++ = *input;
		input++;
	}
	*output = '\0';
	if (*input)
		input++;
	return input;
}

/*
 * 0x004478B1 - SCommandManager::LookupCommand
 *
 * Returns the handler for cmdName via case-insensitive linear scan of the
 * global SCommand table, or NULL.
 */
static void *
SCommandManager_LookupCommand(char *cmdName)
{
	int i;

	if (cmdName == NULL)
		return NULL;
	for (i = 0;; i++) {
		if (g_SCommandTable[i].name == NULL)
			break;
		if (g_SCommandTable[i].handler == NULL)
			break;
		if (strcasecmp(g_SCommandTable[i].name, cmdName) == 0)
			return (void *)(uintptr_t)g_SCommandTable[i].handler;
	}
	return NULL;
}

/*
 * 0x0044791F - SCommandManager::Execute
 *
 * Parses a command name from text, looks it up in the global SCommand table,
 * calls the handler if found, and logs the command via EventLogger.
 */
void
SCommandManager_Execute(SCommandManager *mgr, CItem *mob, uint32_t serial, char *text, int extra)
{
	char tokenBuf[256];
	char *savedText;
	int (*funcPtr)(CItem *, uint32_t, char *, int);
	int retval;
	CString logStr;
	CMobile *cmob;

	USED(mgr);
	memset(tokenBuf, 0, sizeof(tokenBuf));
	savedText = text;
	text = SCommand_ParseToken(text, tokenBuf);
	funcPtr = (int (*)(CItem *, uint32_t, char *, int))(uintptr_t)SCommandManager_LookupCommand(tokenBuf);
	if (funcPtr == NULL)
		return;
	retval = funcPtr(mob, serial, text, extra);
	if (retval == 0)
		return;
	CString_Constructor(&logStr, "scommand ");
	CString_AppendCStr(&logStr, savedText);
	if (mob != NULL) {
		cmob = (CMobile *)mob;
		EventLogger_Log(&g_EventLogger, ((CPlayer *)cmob)->accountNum, (uint32_t)(uint8_t)((CPlayer *)cmob)->characterNum, CMobile_GetSerial(cmob), CMobile_GetName_VT(mob),
		        "godcommand", "misc", CString_GetBuffer(&logStr));
	} else {
		EventLogger_Log(&g_EventLogger, 0, 0, serial, "", "godcommand", "misc", CString_GetBuffer(&logStr));
	}
	CString_Destructor(&logStr);
}
