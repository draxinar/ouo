/*
 * Wombat EScript runtime - text-format script fallback.
 *
 * Tokenizer and interpreter for the plain-text EScript format used by
 * a handful of scripts that ship outside the bytecode pipeline.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "container.h"
#include "dat.h"
#include "gmedit.h"
#include "io.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "region.h"
#include "terrain.h"
#include "vtable.h"
#include "wombat_compile.h"
#include "wombat_escript.h"
#include "wombat_exec.h"

// ESVar vtable function pointer types (from wombat_exec.c)
typedef void *(*ESVarScalarDestructor)(void *, int);
typedef int (*ESVarGetValue)(void *);
typedef void (*ESVarSetFromInt)(void *, int);
typedef void (*ESVarSetFromString)(void *, const char *);
typedef void *(*ESVarClone)(void *);

/*
 * 0x0045B8D0 - CEScript::ReadLine
 *
 * Thiscall. Reads next line from file into g_esLineBuf (0x00645B50),
 * strips trailing newlines, skips leading whitespace, increments lineNo.
 * Returns pointer past leading whitespace.
 */
char *
CEScript_ReadLine(CEScript *ctx)
{
	char *ptr = g_esLineBuf;
	int ch;

	while (!feof_ServerSide(ctx->fp)) {
		ch = fgetc_ServerSide(ctx->fp);
		if (ch == '\n' || ch == '\r')
			break;
		*ptr++ = (char)ch;
	}
	*ptr = 0;

	ptr = g_esLineBuf;
	while (*ptr && isspace((unsigned char)*ptr))
		ptr++;

	ctx->lineNo++;
	return ptr;
}
/*
 * 0x0045B977 - CEScriptVar::CEScriptVar (base constructor, 30 bytes)
 *
 * Thiscall. Sets vtable to base, zeroes name[0].
 */
__attribute__((unused)) CEScriptVar *
CEScriptVar_Constructor(CEScriptVar *self)
{
	self->vtable = NULL;
	self->name[0] = 0;
	return self;
}

/*
 * 0x0045B995 - CEScriptVar::~CEScriptVar
 *
 * Thiscall base class destructor. Binary sets vtable to 0x005EECA8
 * (CEScriptVar base vtable). No-op on Linux since we don't track
 * vtable pointers.
 */
void
CEScriptVar_Destructor(CEScriptVar *self)
{
	USED(self);
}

/*
 * 0x0045B9A9 - CEScriptVar::GetName
 *
 * Thiscall. Returns pointer to name field at offset +4.
 */
char *
CEScriptVar_GetName(CEScriptVar *self)
{
	return self->name;
}

/*
 * 0x0045B9BA - CEScriptVar::GetType
 *
 * Thiscall. Returns sign-extended byte at offset +0x14.
 */
int
CEScriptVar_GetType(CEScriptVar *self)
{
	return (int)(int8_t)self->type;
}

/*
 * 0x0045B9CC - StaticInit_CEScript
 *
 * Static-init entry that calls EScript_Constructor.
 */
__attribute__((unused)) void
StaticInit_CEScript(void)
{
	EScript_Constructor();
}

/*
 * 0x0045B9D6 - EScript_Constructor
 *
 * Initializes the global CEScript context.
 */
void
EScript_Constructor(void)
{
	CEScript_Init(&g_CEScriptCtx);
}

/*
 * 0x0045B9E5 - CEScript::Init
 *
 * Clears the script context: NULLs the file pointer, zeros all 0x400
 * variable slots, and resets lineNo, loopDepth, and numVars.
 */
void
CEScript_Init(CEScript *ctx)
{
	CEScript *es = (CEScript *)ctx;
	int i;

	es->fp = NULL;
	for (i = 0; i < 1024; i++)
		es->vars[i] = NULL;
	es->lineNo = 0;
	es->loopDepth = 0;
	es->numVars = 0;
}

/*
 * 0x0045BA4D - CEScript::Cleanup
 *
 * Destroys every variable, NULLs each slot, and zeros lineNo, loopDepth,
 * numVars, player, and errorFlag.
 */
void
CEScript_Cleanup(CEScript *ctx)
{
	int i;

	for (i = 0; i < ctx->numVars; i++) {
		void *var = ctx->vars[i];
		if (var != NULL) {
			void **vtable = *(void ***)var;
			((ESVarScalarDestructor)(uintptr_t)vtable[ESVAR_VT_DTOR])(var, 1);
		}
		ctx->vars[i] = NULL;
	}
	ctx->lineNo = 0;
	ctx->loopDepth = 0;
	ctx->numVars = 0;
	ctx->player = NULL;
	ctx->errorFlag = 0;
}

/*
 * 0x0045BAF6 - CEScript::ParseToken
 *
 * Skips leading whitespace, saves the first non-whitespace character as the
 * delimiter into *delimOut, and copies the chars up to the next delimiter
 * into tokenBuf. Returns the cursor past the closing delimiter.
 *
 * FIXED: binary's empty-line early return leaves *delimOut uninitialized,
 * so downstream comparisons against '$' read stack residue. The early
 * return now writes *delimOut = 0.
 */
char *
CEScript_ParseToken(CEScript *ctx, char *line, char *tokenBuf, char *delimOut)
{
	// Skip leading whitespace
	while (*line && isspace((unsigned char)*line))
		line++;

	if (*line == 0) {
		CEScript_ErrorLog(ctx, "nondelimited line");
		*delimOut = 0;
		return line;
	}

	// Save delimiter character
	*delimOut = *line;
	line++; // skip opening delimiter

	// Copy until delimiter or end
	while (*line && *line != *delimOut) {
		*tokenBuf++ = *line++;
	}
	*tokenBuf = 0;

	// Skip closing delimiter
	if (*line) {
		line++;
	} else {
		CEScript_ErrorLog(ctx, "nondelimited line");
	}
	return line;
}

/*
 * 0x0045BBBD - CEScript::FindOrCreateVar
 *
 * Returns the index of the variable (name, type) in ctx, allocating it if
 * absent. Type '$' creates a string var, '%' creates an integer var.
 */
int
CEScript_FindOrCreateVar(CEScript *ctx, const char *name, char type)
{
	int i;

	// Search existing vars
	for (i = 0; i < ctx->numVars; i++) {
		if (strcasecmp(CEScriptVar_GetName((CEScriptVar *)ctx->vars[i]), name) != 0)
			continue;
		if (CEScriptVar_GetType((CEScriptVar *)ctx->vars[i]) == (int)type)
			return i;
	}

	// Create new var
	if (type == '$') {
		void *newVar = OperatorNew(sizeof(CEScriptStringVar));
		if (newVar != NULL)
			CEScriptStringVar_Constructor((CEScriptStringVar *)newVar, name);
		ctx->vars[ctx->numVars] = newVar;
		ctx->numVars++;
	} else if (type == '%') {
		void *newVar = OperatorNew(sizeof(CEScriptIntVar));
		if (newVar != NULL)
			CEScriptIntVar_Constructor((CEScriptIntVar *)newVar, name);
		ctx->vars[ctx->numVars] = newVar;
		ctx->numVars++;
	} else {
		CEScript_ErrorLog(ctx, "invalid var type");
	}

	return ctx->numVars - 1;
}

/*
 * 0x0045BD3B - CEScriptStringVar::SetFromString (vtable[3])
 *
 * Parses comma-separated integer values from string into values[] array.
 */
void
CEScriptStringVar_SetFromString(CEScriptStringVar *self, const char *str)
{
	int *values = (int *)self->values;

	self->numValues = 0;
	while (*str) {
		values[self->numValues] = 0;
		while (isdigit((unsigned char)*str)) {
			values[self->numValues] = values[self->numValues] * 10 + (*str - '0');
			str++;
		}
		if (*str)
			str++;
		self->numValues++;
	}
}

/*
 * 0x0045BDD9 - CEScriptStringVar::Clone (vtable[4])
 *
 * Allocates new 0x101C string var, copies name and all values.
 */
CEScriptStringVar *
CEScriptStringVar_Clone(CEScriptStringVar *self)
{
	int *srcValues;
	int *dstValues;
	int i;
	CEScriptStringVar *clone = (CEScriptStringVar *)OperatorNew(sizeof(CEScriptStringVar));
	if (clone == NULL)
		return NULL;
	CEScriptStringVar_Constructor(clone, self->base.name);
	clone->numValues = self->numValues;
	srcValues = (int *)self->values;
	dstValues = (int *)clone->values;
	for (i = 0; i < self->numValues; i++)
		dstValues[i] = srcValues[i];
	return clone;
}

/*
 * 0x0045BE94 - CEScriptIntVar::Clone (vtable[4])
 *
 * Allocates new 0x1C int var, copies name and value.
 */
CEScriptIntVar *
CEScriptIntVar_Clone(CEScriptIntVar *self)
{
	CEScriptIntVar *clone = (CEScriptIntVar *)OperatorNew(sizeof(CEScriptIntVar));
	if (clone == NULL)
		return NULL;
	CEScriptIntVar_Constructor(clone, self->base.name);
	clone->value = self->value;
	return clone;
}

/*
 * 0x0045BF17 - CEScriptStringVar::GetValue (vtable[1])
 *
 * If numValues==0, returns 0. Otherwise returns values[rand() % numValues].
 */
int
CEScriptStringVar_GetValue(CEScriptStringVar *self)
{
	int *values = (int *)self->values;
	if (self->numValues == 0)
		return 0;
	return values[rand() % self->numValues];
}

/*
 * 0x0045BF42 - CEScriptIntVar::SetFromInt (vtable[2])
 *
 * Sets this->value to arg.
 */
void
CEScriptIntVar_SetFromInt(CEScriptIntVar *self, int val)
{
	self->value = val;
}

/*
 * 0x0045BF50 - CEScriptIntVar::SetFromString (vtable[3])
 *
 * No-op: ignores the string argument. Int vars cannot be set from strings.
 */
void
CEScriptIntVar_SetFromString(CEScriptIntVar *self, const char *str)
{
	USED(self);
	USED(str);
}

/*
 * 0x0045BF58 - CEScriptIntVar::GetValue (vtable[1])
 *
 * Returns this->value.
 */
int
CEScriptIntVar_GetValue(CEScriptIntVar *self)
{
	return self->value;
}

/*
 * 0x0045BF69 - CEScript::EvalExpression
 *
 * Evaluates an EScript expression. The delimiter selects the form:
 *   '$' or '%' - variable reference (expr is the var name, or "_RAND")
 *   '#'        - calculator expression with chained binary operators
 *   other      - error
 */
int
CEScript_EvalExpression(CEScript *ctx, char delim, const char *expr)
{
	int result;
	char tokenBuf[256];
	int val;
	int op;

	if (delim == '#') {
		// Calculator expression
		result = 0;
		while (*expr) {
			// Skip whitespace
			while (*expr && isspace((unsigned char)*expr))
				expr++;
			if (*expr == 0)
				break;

			// Read operator character
			switch (*expr) {
			case '&':
			case '*':
			case '+':
			case '-':
			case '/':
			case '<':
			case '=':
			case '>':
			case '\\':
			case '^':
			case '|':
				op = *expr;
				expr++;
				break;
			default:
				op = '+';
				break;
			}

			// Skip whitespace
			while (*expr && isspace((unsigned char)*expr))
				expr++;

			// Parse operand
			if (isdigit((unsigned char)*expr)) {
				val = 0;
				while (isdigit((unsigned char)*expr)) {
					val = val * 10 + (*expr - '0');
					expr++;
				}
			} else {
				char subDelim;
				expr = CEScript_ParseToken(ctx, (char *)expr, tokenBuf, &subDelim);
				val = CEScript_EvalExpression(ctx, subDelim, tokenBuf);
			}

			// Apply operator
			switch (op) {
			case '&':
				if (val != 0 && result != 0)
					result = 1;
				else
					result = 0;
				break;
			case '*':
				result *= val;
				break;
			case '-':
				result -= val;
				break;
			case '/':
				if (val == 0)
					CEScript_ErrorLog(ctx, "division by zero");
				else
					result /= val;
				break;
			case '<':
				if (result < val)
					result = 1;
				else
					result = 0;
				break;
			case '=':
				if (val == result)
					result = 1;
				else
					result = 0;
				break;
			case '>':
				if (result > val)
					result = 1;
				else
					result = 0;
				break;
			case '\\':
				if (val == 0)
					CEScript_ErrorLog(ctx, "division by zero");
				else
					result %= val;
				break;
			case '^':
				if ((val == 0 && result == 0) || (val != 0 && result != 0))
					result = 0;
				else
					result = 1;
				break;
			case '|':
				if (val != 0 || result != 0)
					result = 1;
				else
					result = 0;
				break;
			default:
				result += val;
				break;
			}
		}
		return result;
	} else if (delim == '$' || delim == '%') {
		// Variable reference or _RAND
		if (strcmp(expr, "_RAND") == 0)
			return rand();

		{
			int idx = CEScript_FindOrCreateVar(ctx, expr, delim);
			void *var = ctx->vars[idx];
			return ((ESVarGetValue)(uintptr_t)(*(void ***)var)[ESVAR_VT_GET_VALUE])(var);
		}
	} else {
		CEScript_ErrorLog(ctx, "invalid expression");
		return 0;
	}
}

/*
 * 0x0045C440 - CEScript::CmdAssign
 *
 * Parses [varname delim expression] and assigns the evaluated expression
 * back to the variable: '$' uses ParseBracketArgs('(',')') + SetFromString,
 * '%' uses ParseToken + EvalExpression + SetFromInt.
 */
void
CEScript_CmdAssign(CEScript *ctx, char *line)
{
	char tokenBuf[256];
	char delim;
	int idx;
	void *var;

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);

	if (delim == '$') {
		// String assignment: parse bracket args
		idx = CEScript_FindOrCreateVar(ctx, tokenBuf, '$');
		line = CEScript_ParseBracketArgs(ctx, line, tokenBuf, '(', ')');
		var = ctx->vars[idx];
		((ESVarSetFromString)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_STR])(var, line);
	} else if (delim == '%') {
		// Integer assignment: evaluate expression
		char delim2;
		int val;
		idx = CEScript_FindOrCreateVar(ctx, tokenBuf, '%');
		line = CEScript_ParseToken(ctx, line, tokenBuf, &delim2);
		val = CEScript_EvalExpression(ctx, delim2, tokenBuf);
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);
	} else {
		CEScript_ErrorLog(ctx, "invalid assign");
	}
}

/*
 * 0x0045C588 - CEScript::CmdNewObj
 *
 * Parses STATIC/DYNAMIC/CONTAINER plus bodyType/x/y/z, creates the entity
 * and drops it at that location. Non-static entities also run CItem_Setup
 * and ValidateInWorld. Updates the _OBJHEIGHT, _OBJTYPE, _OBJX, _OBJY, and
 * _OBJZ script variables from the resulting entity.
 */
void
CEScript_CmdNewObj(CEScript *ctx, char *line)
{
	char typeBuf[256];
	char tokenBuf[256];
	char delim;
	CItem *obj = NULL;
	int isDynamic = 0;
	int val;
	int idx;
	void *var;

	line = GetValue(line, typeBuf);

	// Type dispatch: STATIC, DYNAMIC, CONTAINER, or default (CItem)
	if (strcasecmp(typeBuf, "STATIC") == 0) {
		obj = CreateStaticEntity();
	} else {
		isDynamic = 1;
		if (strcasecmp(typeBuf, "DYNAMIC") == 0) {
			void *mem = OperatorNew(sizeof(CItem));
			if (mem)
				obj = CItem_Constructor(mem);
			else
				obj = NULL;
		} else if (strcasecmp(typeBuf, "CONTAINER") == 0) {
			void *mem = OperatorNew(sizeof(CContainer));
			if (mem) {
				CContainer_Constructor((CContainer *)mem);
				obj = (CItem *)mem;
			} else {
				obj = NULL;
			}
		} else {
			// Default: create CItem
			void *mem = OperatorNew(sizeof(CItem));
			if (mem)
				obj = CItem_Constructor(mem);
			else
				obj = NULL;
		}
	}

	// Parse bodyType
	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	val = CEScript_EvalExpression(ctx, delim, tokenBuf);
	CEntity_SetBodyType(obj, (uint16_t)val);

	// Parse x
	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	val = CEScript_EvalExpression(ctx, delim, tokenBuf);
	obj->resourceEntity.entity.location.x = (int16_t)val;

	// Parse y
	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	val = CEScript_EvalExpression(ctx, delim, tokenBuf);
	obj->resourceEntity.entity.location.y = (int16_t)val;

	// Parse z
	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	val = CEScript_EvalExpression(ctx, delim, tokenBuf);
	obj->resourceEntity.entity.location.z = (int16_t)val;

	((void (*)(void *, CLocation *))VT_FN(obj, VT_DROP_AT_FEET))(obj, &obj->resourceEntity.entity.location);

	// Non-static: call CItem_Setup + validate
	if (isDynamic) {
		CItem_Setup(obj, 0, &obj->resourceEntity.entity.location, 0, 1);
		if (!ValidateInWorld(obj))
			obj = NULL;
	}

	if (obj == NULL)
		return;

	// Set _OBJHEIGHT = vtable[0x28] GetHeight
	val = VT_GetHeight(obj);
	idx = CEScript_FindOrCreateVar(ctx, "_OBJHEIGHT", '%');
	var = ctx->vars[idx];
	((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);

	// Set _OBJTYPE = bodyType & 0xFFFF
	val = CEntity_GetBodyType(obj) & 0xFFFF;
	idx = CEScript_FindOrCreateVar(ctx, "_OBJTYPE", '%');
	var = ctx->vars[idx];
	((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);

	// Set _OBJX = (int16_t)location.x
	val = (int)(int16_t)obj->resourceEntity.entity.location.x;
	idx = CEScript_FindOrCreateVar(ctx, "_OBJX", '%');
	var = ctx->vars[idx];
	((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);

	// Set _OBJY = (int16_t)location.y
	val = (int)(int16_t)obj->resourceEntity.entity.location.y;
	idx = CEScript_FindOrCreateVar(ctx, "_OBJY", '%');
	var = ctx->vars[idx];
	((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);

	// Set _OBJZ = (int16_t)location.z
	val = (int)(int16_t)obj->resourceEntity.entity.location.z;
	idx = CEScript_FindOrCreateVar(ctx, "_OBJZ", '%');
	var = ctx->vars[idx];
	((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);
}

/*
 * 0x0045CA1D - CEScript::CmdRandJump
 *
 * Splits line on ':' in-place, picks one of the resulting labels at random,
 * rewinds the script file, and scans for the matching ":label" line.
 */
void
CEScript_CmdRandJump(CEScript *ctx, char *line)
{
	char tokenBuf[256];
	char delim;
	char *labelList[32];
	int count = 0;
	int pick;
	char *found;

	// Split line by ':' in-place
	for (;;) {
		if (*line == 0)
			break;

		// Skip to ':'
		while (*line && *line != ':')
			line++;

		if (*line == 0)
			continue;

		// Skip past ':'
		line++;

		// Store label start
		labelList[count] = line;
		count++;

		// Skip to next ':' or end
		while (*line && *line != ':')
			line++;

		if (*line == 0)
			continue;

		// Null-terminate label
		*line = 0;
		line++;
	}

	if (count == 0)
		return;

	pick = rand() % count;

	// Seek to beginning of file
	fseek_ServerSide(ctx->fp, 0, 0);
	ctx->lineNo = 0;

	// Scan for matching label line
	while (!feof_ServerSide(ctx->fp)) {
		found = CEScript_ReadLine(ctx);
		if (*found != ':')
			continue;
		CEScript_ParseToken(ctx, found, tokenBuf, &delim);
		if (strcasecmp(tokenBuf, labelList[pick]) == 0)
			return;
	}

	CEScript_ErrorLog(ctx, "failed to find referenced label");
}

/*
 * 0x0045CBA8 - CEScript::EvalCondition
 *
 * Parses one delimited token from line, evaluates it via EvalExpression,
 * and inverts the boolean result when negate is set.
 */
int
CEScript_EvalCondition(CEScript *ctx, char *line, int negate)
{
	char tokenBuf[256];
	char delim;
	int result;

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	result = CEScript_EvalExpression(ctx, delim, tokenBuf);
	if (negate)
		result = (result == 0) ? 1 : 0;
	return result;
}

/*
 * 0x0045CC1A - CEScript::CmdWhile
 *
 * Evaluates the condition. When true, records the loop's start line and
 * bumps loopDepth; when false, scans forward past the matching ENDLOOP.
 */
void
CEScript_CmdWhile(CEScript *ctx, char *line, int negate)
{
	int cond;
	int depth;
	char *scanLine;
	char cmdBuf[256];
	char delim;

	cond = CEScript_EvalCondition(ctx, line, negate);
	if (cond) {
		// Condition true: save loop start position
		ctx->loopLines[ctx->loopDepth] = ctx->lineNo;
		ctx->loopDepth++;
		return;
	}

	// Condition false: skip to matching ENDLOOP
	depth = 1;
	for (;;) {
		if (feof_ServerSide(ctx->fp))
			break;
		if (depth <= 0)
			break;

		scanLine = CEScript_ReadLine(ctx);
		if (*scanLine != '@')
			continue;

		CEScript_ParseToken(ctx, scanLine, cmdBuf, &delim);

		if (strcasecmp(cmdBuf, "WHILENOT") == 0) {
			depth++;
		} else if (strcasecmp(cmdBuf, "WHILE") == 0) {
			depth++;
		} else if (strcasecmp(cmdBuf, "ENDLOOP") == 0) {
			depth--;
		}
	}

	if (depth > 0)
		CEScript_ErrorLog(ctx, "no corresponding @ENDLOOP@");
}

/*
 * 0x0045CD96 - CEScript::CmdEndLoop
 *
 * Rewinds to the start of the script and re-reads lines up to the saved
 * loop start, then drops loopDepth.
 */
void
CEScript_CmdEndLoop(CEScript *ctx, char *line)
{
	int i;

	USED(line);

	ctx->lineNo = 0;
	fseek_ServerSide(ctx->fp, 0, 0);

	for (i = 1; i < ctx->loopLines[ctx->loopDepth]; i++) {
		CEScript_ReadLine(ctx);
	}

	ctx->loopDepth--;
}

/*
 * 0x0045CE13 - CEScript::CmdIf
 *
 * Evaluates the condition. When false, scans forward counting nested
 * IF/IFNOT/ENDIF blocks until the matching ENDIF is consumed.
 */
void
CEScript_CmdIf(CEScript *ctx, char *line, int negate)
{
	int cond;
	int depth;
	char *scanLine;
	char cmdBuf[256];
	char delim;

	cond = CEScript_EvalCondition(ctx, line, negate);
	if (cond)
		return;

	depth = 1;
	for (;;) {
		if (feof_ServerSide(ctx->fp))
			break;
		if (depth <= 0)
			break;

		scanLine = CEScript_ReadLine(ctx);
		if (*scanLine != '@')
			continue;

		CEScript_ParseToken(ctx, scanLine, cmdBuf, &delim);

		if (strcasecmp(cmdBuf, "IFNOT") == 0) {
			depth++;
		} else if (strcasecmp(cmdBuf, "IF") == 0) {
			depth++;
		} else if (strcasecmp(cmdBuf, "ENDIF") == 0) {
			depth--;
		}
	}

	if (depth > 0)
		CEScript_ErrorLog(ctx, "no corresponding @ENDIF@");
}

/*
 * 0x0045CF4D - CEScript::ParseBracketArgs
 *
 * Copies the content between the next openDelim and the matching closeDelim
 * (or end of line) into outBuf. Returns outBuf.
 */
char *
CEScript_ParseBracketArgs(CEScript *ctx, char *line, char *outBuf, char openDelim, char closeDelim)
{
	char *out = outBuf;

	USED(ctx);

	// Skip to open delimiter
	while (*line && *line != openDelim)
		line++;

	// Skip past open delimiter
	if (*line)
		line++;

	// Copy until close delimiter or end
	while (*line && *line != closeDelim) {
		*out++ = *line++;
	}
	*out = 0;

	return outBuf;
}

/*
 * 0x0045CFD7 - CEScript::CmdMessage
 *
 * Sends "EScr: <text>" as a TEXT packet to the script's bound player.
 */
void
CEScript_CmdMessage(CEScript *ctx, const char *text)
{
	char textBuf[512];
	uint8_t pktBuf[0x42C];

	sprintf(textBuf, "EScr: %s", text);

	PacketManager_MakePacket_TEXT(pktBuf, NULL, ctx->player, 6, textBuf, 0x3B2, 0);
	SendToClient(ctx->player, pktBuf, -1);
}

/*
 * 0x0045D050 - CEScript::ErrorLog
 *
 * Sets errorFlag, formats "EScr: <msg> [<scriptName>]", and sends it as a
 * TEXT packet to the bound player.
 *
 * FIXED: the binary calls sprintf into a 512-byte stack buffer with both a
 * caller-supplied msg and a scriptName up to 508 bytes long, which can
 * smash the adjacent pktBuf and return address. Replaced sprintf with
 * snprintf bounded by sizeof(textBuf).
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
void
CEScript_ErrorLog(CEScript *ctx, const char *msg)
{
	char textBuf[512];
	uint8_t pktBuf[0x42C];

	ctx->errorFlag = 1;

	snprintf(textBuf, sizeof(textBuf), "EScr: %s [%s]", msg, ctx->scriptName);

	PacketManager_MakePacket_TEXT(pktBuf, NULL, ctx->player, 6, textBuf, 0x3B2, 0);
	SendToClient(ctx->player, pktBuf, -1);
}
#pragma GCC diagnostic pop

/*
 * 0x0045D0E6 - CEScript::CmdEScript
 *
 * Spawns a child CEScript that inherits the parent's vars and field1414,
 * runs the named script under it, then tears the child down.
 *
 * FIXED: the binary formats "../.rundir/escripts/%s.esc" into a 128-byte
 * buffer with no length check on scriptName, smashing the stack on names
 * over 103 chars. Replaced sprintf with snprintf bounded by sizeof(path).
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
void
CEScript_CmdEScript(CEScript *ctx, char *line)
{
	CEScript *child;
	int i;
	char scriptName[128];
	char path[128];

	child = (CEScript *)OperatorNew(sizeof(CEScript));
	if (child == NULL)
		return;
	CEScript_Init(child);

	// Copy inherited fields
	child->field1414 = ctx->field1414;
	child->numVars = ctx->numVars;

	// Clone each variable
	for (i = 0; i < ctx->numVars; i++) {
		void *var = ctx->vars[i];
		void **vtable = *(void ***)var;
		child->vars[i] = ((ESVarClone)(uintptr_t)vtable[ESVAR_VT_CLONE])(var);
	}

	// Parse script name from args (skip whitespace)
	line = GetValue(line, scriptName);
	while (*line && isspace((unsigned char)*line))
		line++;

	snprintf(path, sizeof(path), "../.rundir/escripts/%s.esc", scriptName);
	CEScript_Run(child, path, ctx->player, line);

	OperatorDelete(child);
}
#pragma GCC diagnostic pop

/*
 * 0x0045D28B - CEScript::CmdNoUpdate
 *
 * No-op stub.
 */
void
CEScript_CmdNoUpdate(CEScript *ctx, const char *text, int mode)
{
	USED(ctx);
	USED(text);
	USED(mode);
}

/*
 * 0x0045D2A4 - CEScript::CmdGetTerr
 *
 * Reads the terrain tile ID at (x, y) from the map grid into an integer
 * variable.
 */
void
CEScript_CmdGetTerr(CEScript *ctx, char *line)
{
	char tokenBuf[256];
	char delim;
	int x, y;
	int blockIdx;
	int idx;
	void *var;

	// Parse variable name
	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);

	if (delim != '%') {
		CEScript_ErrorLog(ctx, "tried to get terrain to noninteger");
		return;
	}

	// FindOrCreateVar before parsing x,y
	idx = CEScript_FindOrCreateVar(ctx, tokenBuf, delim);

	// Parse X
	{
		char xBuf[256];
		char xDelim;
		line = CEScript_ParseToken(ctx, line, xBuf, &xDelim);
		x = CEScript_EvalExpression(ctx, xDelim, xBuf);
	}

	// Parse Y
	{
		char yBuf[256];
		char yDelim;
		line = CEScript_ParseToken(ctx, line, yBuf, &yDelim);
		y = CEScript_EvalExpression(ctx, yDelim, yBuf);
	}

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y)) {
		CEScript_ErrorLog(ctx, "tried to get terrain off map");
		return;
	}

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);

	// Read tileID: unsigned 16-bit at cell offset
	{
		MapBlock *block = &g_MapBlocks[blockIdx];
		int val = (int)block->cells[(y & 7) * 8 + (x & 7)].tileID;
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);
	}
}

/*
 * 0x0045D43B - CEScript::CmdGetAlt
 *
 * Reads the terrain altitude (Z) at (x, y) from the map grid into an
 * integer variable.
 */
void
CEScript_CmdGetAlt(CEScript *ctx, char *line)
{
	char tokenBuf[256];
	char delim;
	int x, y;
	int blockIdx;
	int idx;
	void *var;

	// Parse variable name
	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);

	if (delim != '%') {
		CEScript_ErrorLog(ctx, "tried to get terrain to noninteger");
		return;
	}

	// FindOrCreateVar before parsing x,y
	idx = CEScript_FindOrCreateVar(ctx, tokenBuf, delim);

	// Parse X
	{
		char xBuf[256];
		char xDelim;
		line = CEScript_ParseToken(ctx, line, xBuf, &xDelim);
		x = CEScript_EvalExpression(ctx, xDelim, xBuf);
	}

	// Parse Y
	{
		char yBuf[256];
		char yDelim;
		line = CEScript_ParseToken(ctx, line, yBuf, &yDelim);
		y = CEScript_EvalExpression(ctx, yDelim, yBuf);
	}

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y)) {
		CEScript_ErrorLog(ctx, "tried to get altitude off map");
		return;
	}

	blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, x, y, 0);

	// Read z: sign-extended byte at cell offset +2
	{
		MapBlock *block = &g_MapBlocks[blockIdx];
		int val = (int)block->cells[(y & 7) * 8 + (x & 7)].z;
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, val);
	}
}

/*
 * 0x0045D5D1 - CEScript::CmdSetTerr
 *
 * Parses tileID, x, y from line and writes the new tile via SetTerrainTile.
 */
void
CEScript_CmdSetTerr(CEScript *ctx, char *line)
{
	char tokenBuf[256];
	char delim;
	int tileID, x, y;

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	tileID = CEScript_EvalExpression(ctx, delim, tokenBuf);

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	x = CEScript_EvalExpression(ctx, delim, tokenBuf);

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	y = CEScript_EvalExpression(ctx, delim, tokenBuf);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y)) {
		CEScript_ErrorLog(ctx, "tried to set terrain off map");
		return;
	}

	SetTerrainTile((intptr_t)ctx->player, x, y, tileID, -666);
}

/*
 * 0x0045D700 - CEScript::CmdSetAlt
 *
 * Parses alt, x, y from line and writes the new altitude via SetTerrainTile
 * after validating the coordinates.
 */
void
CEScript_CmdSetAlt(CEScript *ctx, char *line)
{
	char tokenBuf[256];
	char delim;
	int alt, x, y;

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	alt = CEScript_EvalExpression(ctx, delim, tokenBuf);

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	x = CEScript_EvalExpression(ctx, delim, tokenBuf);

	line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
	y = CEScript_EvalExpression(ctx, delim, tokenBuf);

	if (!CBlockManager_IsValidCoord(&g_SpatialGrid, x, y)) {
		CEScript_ErrorLog(ctx, "tried to set terrain off map");
		return;
	}

	SetTerrainTile((intptr_t)ctx->player, x, y, -666, alt);
}

/*
 * 0x0045D82F - CEScript::CmdPrintVars
 *
 * Builds a single TEXT packet listing the integer-typed variables named on
 * line as "name=value " pairs and sends it to the bound player.
 */
void
CEScript_CmdPrintVars(CEScript *ctx, char *line)
{
	char textBuf[1024];
	char tokenBuf[256];
	char delim;
	uint8_t pktBuf[0x42C];
	void *var;
	int idx;
	int len;

	textBuf[0] = 0;

	while (*line != 0) {
		line = CEScript_ParseToken(ctx, line, tokenBuf, &delim);
		if (delim != '%')
			continue;

		idx = CEScript_FindOrCreateVar(ctx, tokenBuf, '%');
		var = ctx->vars[idx];
		len = strlen(textBuf);
		sprintf(textBuf + len, "%s=%d ", tokenBuf, ((ESVarGetValue)(uintptr_t)(*(void ***)var)[ESVAR_VT_GET_VALUE])(var));
	}

	PacketManager_MakePacket_TEXT(pktBuf, NULL, ctx->player, 6, textBuf, 0x3B2, 0);
	SendToClient(ctx->player, pktBuf, -1);
}

/*
 * 0x0045D93C - CEScript::CmdQLoad
 *
 * Parses the value token and forwards it to the empty Noop_4A3E28 stub,
 * passing the trailing integer when present, else 1.
 */
void
CEScript_CmdQLoad(CEScript *ctx, char *line)
{
	char valueBuf[256];

	line = GetValue(line, valueBuf);

	if (isdigit((unsigned char)*line)) {
		Noop_4A3E28(ctx->player, valueBuf, atoi(line));
	} else {
		Noop_4A3E28(ctx->player, valueBuf, 1);
	}
}

/*
 * 0x0045D9C3 - CEScript::Run
 *
 * Opens the escript at path, seeds _PLAYERX/_PLAYERY/_PLAYERZ from player's
 * location plus _ARG1.._ARGN from comma-separated args, then iterates the
 * file dispatching @COMMAND lines to their handlers (ASSIGN, NEWOBJ,
 * RANDJUMP, IFNOT, IF, WHILENOT, WHILE, ENDLOOP, MESSAGE, ESCRIPT,
 * NOUPDATE, UPDATE, GETTERR, GETALT, SETTERR, SETALT, PRINTVARS, QLOAD).
 */
void
CEScript_Run(CEScript *ctx, const char *path, CItem *player, const char *args)
{
	char *line;
	char cmdBuf[256];
	char argName[256];
	char delim;
	int argCounter;

	ctx->errorFlag = 0;

	ctx->fp = fopen_ServerSide(path, "r");
	ctx->player = player;
	ctx->scriptName[0] = 0;

	if (ctx->fp == NULL) {
		CEScript_ErrorLog(ctx, "failed to open");
		CEScript_Cleanup(ctx);
		return;
	}

	// Set _PLAYERX, _PLAYERY, _PLAYERZ from player location
	{
		int idx;
		void *var;

		idx = CEScript_FindOrCreateVar(ctx, "_PLAYERX", '%');
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, (int)(int16_t)player->resourceEntity.entity.location.x);

		idx = CEScript_FindOrCreateVar(ctx, "_PLAYERY", '%');
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, (int)(int16_t)player->resourceEntity.entity.location.y);

		idx = CEScript_FindOrCreateVar(ctx, "_PLAYERZ", '%');
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, (int)(int16_t)player->resourceEntity.entity.location.z);
	}

	// Parse _ARG1, _ARG2, ... from comma-separated args
	argCounter = 1;
	while (*args != 0) {
		char valueBuf[256];
		int idx;
		void *var;

		args = GetValue((char *)args, valueBuf);
		sprintf(argName, "_ARG%d", argCounter);

		idx = CEScript_FindOrCreateVar(ctx, argName, '%');
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, atoi(valueBuf));

		argCounter++;
	}

	// Set _ARGC
	{
		int idx;
		void *var;

		idx = CEScript_FindOrCreateVar(ctx, "_ARGC", '%');
		var = ctx->vars[idx];
		((ESVarSetFromInt)(uintptr_t)(*(void ***)var)[ESVAR_VT_SET_FROM_INT])(var, argCounter - 1);
	}

	// Main execution loop
	while (!feof_ServerSide(ctx->fp) && ctx->errorFlag == 0) {
		line = CEScript_ReadLine(ctx);
		if (line == NULL || *line != '@')
			continue;

		// Copy line to scriptName buffer
		strcpy(ctx->scriptName, line);

		// Parse command token using ParseToken with '@' as delimiter
		line = CEScript_ParseToken(ctx, line, cmdBuf, &delim);

		// Dispatch command - each branch calls ParseBracketArgs then handler
		if (strcasecmp(cmdBuf, "ASSIGN") == 0) {
			CEScript_CmdAssign(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "NEWOBJ") == 0) {
			CEScript_CmdNewObj(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "RANDJUMP") == 0) {
			CEScript_CmdRandJump(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "IFNOT") == 0) {
			CEScript_CmdIf(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'), 1);
		} else if (strcasecmp(cmdBuf, "IF") == 0) {
			CEScript_CmdIf(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'), 0);
		} else if (strcasecmp(cmdBuf, "WHILENOT") == 0) {
			CEScript_CmdWhile(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'), 1);
		} else if (strcasecmp(cmdBuf, "WHILE") == 0) {
			CEScript_CmdWhile(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'), 0);
		} else if (strcasecmp(cmdBuf, "ENDLOOP") == 0) {
			CEScript_CmdEndLoop(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "MESSAGE") == 0) {
			CEScript_CmdMessage(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "ESCRIPT") == 0) {
			CEScript_CmdEScript(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "NOUPDATE") == 0) {
			CEScript_CmdNoUpdate(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'), 0);
		} else if (strcasecmp(cmdBuf, "UPDATE") == 0) {
			CEScript_CmdNoUpdate(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'), 1);
		} else if (strcasecmp(cmdBuf, "GETTERR") == 0) {
			CEScript_CmdGetTerr(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "GETALT") == 0) {
			CEScript_CmdGetAlt(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "SETTERR") == 0) {
			CEScript_CmdSetTerr(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "SETALT") == 0) {
			CEScript_CmdSetAlt(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "PRINTVARS") == 0) {
			CEScript_CmdPrintVars(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		} else if (strcasecmp(cmdBuf, "QLOAD") == 0) {
			CEScript_CmdQLoad(ctx, CEScript_ParseBracketArgs(ctx, line, cmdBuf, '[', ']'));
		}
	}

	fclose_ServerSide(ctx->fp);
	CEScript_Cleanup(ctx);
}

/*
 * 0x0045E140 - CEScriptStringVar::SetFromInt (vtable[2])
 *
 * No-op (same function as IntVar's SetFromString).
 */
void
CEScriptStringVar_SetFromInt(CEScriptStringVar *self, int val)
{
	USED(self);
	USED(val);
}

/*
 * 0x0045E150 - CEScriptVar::`scalar deleting destructor'
 *
 * ORPHANED: zero callers in the binary. Base class thunk emitted by
 * MSVC for the abstract CEScriptVar but never placed in a vtable the
 * demo uses.
 */
static __attribute__((unused)) void *
CEScriptVar_ScalarDelete(CEScriptVar *self, int flags)
{
	CEScriptVar_Destructor(self);
	if (flags & 1)
		OperatorDelete(self);
	return NULL;
}

/*
 * 0x0045E180 - CEScriptStringVar::CEScriptStringVar
 *
 * Initializes a string variable with the supplied name and type='$'.
 */
CEScriptStringVar *
CEScriptStringVar_Constructor(CEScriptStringVar *self, const char *name)
{
	CEScriptVar_Constructor(&self->base);
	self->base.vtable = g_CEScriptStringVar_vtable;
	strcpy(self->base.name, name);
	self->base.type = '$';
	return self;
}

/*
 * 0x0045E1C0 - CEScriptStringVar scalar deleting destructor
 *
 * Scalar deleting destructor: runs the destructor and frees the object
 * when freeObj & 1.
 */
CEScriptStringVar *
CEScriptStringVar_ScalarDtor(CEScriptStringVar *self, int freeObj)
{
	CEScriptStringVar_Destructor(self);
	if (freeObj & 1)
		OperatorDelete(self);
	return NULL;
}

/*
 * 0x0045E1F0 - CEScriptStringVar::~CEScriptStringVar
 *
 * Stamps the CEScriptStringVar vtable, then chains to the base destructor.
 */
__attribute__((unused)) void
CEScriptStringVar_Destructor(CEScriptStringVar *self)
{
	self->base.vtable = g_CEScriptStringVar_vtable;
	CEScriptVar_Destructor(&self->base);
}

/*
 * 0x0045E210 - CEScriptIntVar::CEScriptIntVar
 *
 * Initializes an integer variable with the supplied name and type='%'.
 */
CEScriptIntVar *
CEScriptIntVar_Constructor(CEScriptIntVar *self, const char *name)
{
	CEScriptVar_Constructor(&self->base);
	self->base.vtable = g_CEScriptIntVar_vtable;
	strcpy(self->base.name, name);
	self->base.type = '%';
	return self;
}

/*
 * 0x0045E250 - CEScriptIntVar scalar deleting destructor
 *
 * Scalar deleting destructor: runs the destructor and frees the object
 * when freeObj & 1.
 */
CEScriptIntVar *
CEScriptIntVar_ScalarDtor(CEScriptIntVar *self, int freeObj)
{
	CEScriptIntVar_Destructor(self);
	if (freeObj & 1)
		OperatorDelete(self);
	return NULL;
}

/*
 * 0x0045E280 - CEScriptIntVar::~CEScriptIntVar
 *
 * Stamps the CEScriptIntVar vtable, then chains to the base destructor.
 */
__attribute__((unused)) void
CEScriptIntVar_Destructor(CEScriptIntVar *self)
{
	self->base.vtable = g_CEScriptIntVar_vtable;
	CEScriptVar_Destructor(&self->base);
}
