/*
 * Custom - UO chat protocol (packets 0xB2/0xB3/0xB5).
 *
 * Server-side implementation of the Ultima Online chat system. The wire
 * protocol is decompiled from client 1.25.37: the client builds 0xB3/0xB5
 * in PacketManager::MakePacket (0x08081d4c, 0x08081d9c), parses 0xB2 in
 * PacketManager::HandlePacket (PDSTRUCT_CHAT_MSG) at 0x0807e378, and lists
 * its command/action codes in the commandAbbrevs table at 0x08128794.
 * UoDemo.exe's packet dispatcher stops at type 0xB1 and neither binary
 * contains server-side chat logic, so the conference/user model below is
 * a CUSTOM server-side system.
 *
 * Packets:
 *   0xB5 CHAT_OPEN  C->S, fixed 64:  [B5][pad][UTF-16BE name, NUL]
 *   0xB3 CHAT_TEXT  C->S, variable:  [B3][size][ "ENU\0" ][action:2][UTF-16BE text, NUL]
 *   0xB2 CHAT_MSG   S->C, variable:  [B2][size][number:2][lang:4][UTF-16BE p1][UTF-16BE p2]
 *
 * Status notices are sent as numbered CHAT_MSG messages, which the client
 * renders from its own "chat" language section (getStr at 0x0807e868). A
 * few notices with no standard chat-message number are sent as plain
 * system messages instead.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "account.h"
#include "chat.h"
#include "packet_handler.h"
#include "packet_manager.h"
#include "packet_utils.h"
#include "player.h"
#include "vtable.h"

enum {
	CHAT_NAME_MAX = 32,        /* conference name / nickname buffer */
	CHAT_PASSWORD_MAX = 32,
	CHAT_PARAM_MAX = 256,      /* a 0xB3 param after UTF-16 -> ASCII */
};

/*
 * Server->client command numbers carried in the CHAT_MSG (0xB2) number
 * field. Decompiled from the client's HandlePacket(PDSTRUCT_CHAT_MSG)
 * dispatch switch at 0x0807e3ec; the names follow the ChatGump method
 * each command invokes.
 */
enum {
	CHAT_CMD_ADD_CONFERENCE = 0x3E8,    /* ChatGump::AddConference */
	CHAT_CMD_REMOVE_CONFERENCE = 0x3E9, /* ChatGump::RemoveConference */
	CHAT_CMD_ASK_NEW_NICKNAME = 0x3EB,  /* nickname dialog */
	CHAT_CMD_CLOSE_CHAT_WINDOW = 0x3EC, /* destroy the chat gump */
	CHAT_CMD_OPEN_CHAT_WINDOW = 0x3ED,  /* construct the chat gump */
	CHAT_CMD_PLAYER_ENTER = 0x3EE,      /* ChatGump::HandlePlayerEnter */
	CHAT_CMD_PLAYER_LEAVE = 0x3EF,      /* ChatGump::HandlePlayerLeave */
	CHAT_CMD_PLAYER_CLEAR = 0x3F0,      /* ChatGump::HandlePlayerClear */
	CHAT_CMD_CONFERENCE_ENTER = 0x3F1,  /* ChatGump::HandleConferenceEnter */
};

/*
 * Server->client message numbers carried in the CHAT_MSG (0xB2) number
 * field. The client renders each from its own "chat" language section -
 * HandlePacket(PDSTRUCT_CHAT_MSG) at 0x0807e868 does getStr(number + 0x13)
 * and substitutes %1/%2 from param1/param2. The numbers are the standard
 * UO chat protocol. 37-39 carry speech; their param1 is "<digit><name>"
 * where the digit is the speaker color.
 */
enum {
	CHAT_MSG_ALREADY_IGNORING = 0x02,            /* "You are already ignoring %1." */
	CHAT_MSG_NOW_IGNORING = 0x03,                /* "You are now ignoring %1." */
	CHAT_MSG_NO_LONGER_IGNORING = 0x04,          /* "You are no longer ignoring %1." */
	CHAT_MSG_NOT_IGNORING = 0x05,                /* "You are not ignoring %1." */
	CHAT_MSG_NO_LONGER_IGNORING_ANYONE = 0x06,   /* "You are no longer ignoring anyone." */
	CHAT_MSG_MUST_HAVE_OPS = 0x09,               /* "You must have operator status to do this." */
	CHAT_MSG_MUST_BE_IN_CONFERENCE = 0x0B,       /* "You must be in a conference to do this." */
	CHAT_MSG_NO_PLAYER = 0x0C,                   /* "There is no player named '%1'." */
	CHAT_MSG_NO_CONFERENCE = 0x0D,               /* "There is no conference named '%1'." */
	CHAT_MSG_INCORRECT_PASSWORD = 0x0E,          /* "That is not the correct password." */
	CHAT_MSG_PLAYER_IS_IGNORING = 0x0F,          /* "%1 has chosen to ignore you." */
	CHAT_MSG_REVOKED_SPEAKING = 0x10,            /* "The moderator has not given you speaking privileges." */
	CHAT_MSG_RECEIVING_PRIVATE = 0x11,           /* "You can now receive private messages." */
	CHAT_MSG_NO_LONGER_RECEIVING_PRIVATE = 0x12, /* "You will no longer receive private messages." */
	CHAT_MSG_SHOWING_NAME = 0x13,                /* "You are now showing your character name." */
	CHAT_MSG_NOT_SHOWING_NAME = 0x14,            /* "You are no longer showing your character name." */
	CHAT_MSG_PLAYER_ANONYMOUS = 0x15,            /* "%1 is remaining anonymous." */
	CHAT_MSG_PLAYER_NOT_RECEIVING_PRIVATE = 0x16, /* "%1 is not receiving private messages." */
	CHAT_MSG_PLAYER_KNOWN_AS = 0x17,             /* "%1 is known in the lands of Britannia as %2." */
	CHAT_MSG_PLAYER_KICKED = 0x18,               /* "%1 has been kicked out of the conference." */
	CHAT_MSG_MODERATOR_KICKED_YOU = 0x19,        /* "%1, a conference moderator, has kicked you out." */
	CHAT_MSG_ALREADY_IN_CONFERENCE = 0x1A,       /* "You are already in the conference '%1'." */
	CHAT_MSG_NO_LONGER_MODERATOR = 0x1B,         /* "%1 is no longer a conference moderator." */
	CHAT_MSG_NOW_MODERATOR = 0x1C,               /* "%1 is now a conference moderator." */
	CHAT_MSG_REMOVED_AS_MODERATOR = 0x1D,        /* "%1 removed you from the conference moderators." */
	CHAT_MSG_MADE_YOU_MODERATOR = 0x1E,          /* "%1 has made you a conference moderator." */
	CHAT_MSG_NO_LONGER_SPEAKING = 0x1F,          /* "%1 no longer has speaking privileges." */
	CHAT_MSG_NOW_SPEAKING = 0x20,                /* "%1 now has speaking privileges." */
	CHAT_MSG_MODERATOR_REVOKED_SPEAKING = 0x21,  /* "%1, a moderator, removed your speaking privileges." */
	CHAT_MSG_MODERATOR_GRANTED_SPEAKING = 0x22,  /* "%1, a moderator, granted you speaking privileges." */
	CHAT_MSG_SPEAKING_DEFAULT = 0x23,            /* "Everyone in the conference may speak by default." */
	CHAT_MSG_MODERATORS_SPEAK_DEFAULT = 0x24,    /* "Only moderators may speak by default." */
	CHAT_MSG_CHANNEL = 0x25,                     /* "%1: %2" - channel speech */
	CHAT_MSG_EMOTE = 0x26,                       /* "%1 %2" - emote */
	CHAT_MSG_PRIVATE = 0x27,                     /* "[%1]: %2" - private message */
	CHAT_MSG_PASSWORD_CHANGED = 0x28,            /* "The conference password has been changed." */
};

/*
 * Client->server action ids carried as the first UTF-16 unit of the
 * CHAT_TEXT (0xB3) payload. The set (0x41, 0x58, 0x61-0x7A) is decompiled
 * from the client's command/action table at commandAbbrevs (0x08128794);
 * 0x61 is the default action for plain typed text.
 */
enum {
	CHAT_ACT_CHANGE_PASSWORD = 0x41,
	CHAT_ACT_LEAVE_CHAT = 0x58,
	CHAT_ACT_MESSAGE = 0x61,
	CHAT_ACT_JOIN_CONFERENCE = 0x62,
	CHAT_ACT_CREATE_CONFERENCE = 0x63,
	CHAT_ACT_RENAME_CONFERENCE = 0x64,
	CHAT_ACT_PRIVATE_MESSAGE = 0x65,
	CHAT_ACT_ADD_IGNORE = 0x66,
	CHAT_ACT_REMOVE_IGNORE = 0x67,
	CHAT_ACT_TOGGLE_IGNORE = 0x68,
	CHAT_ACT_ADD_VOICE = 0x69,
	CHAT_ACT_REMOVE_VOICE = 0x6A,
	CHAT_ACT_TOGGLE_VOICE = 0x6B,
	CHAT_ACT_ADD_MODERATOR = 0x6C,
	CHAT_ACT_REMOVE_MODERATOR = 0x6D,
	CHAT_ACT_TOGGLE_MODERATOR = 0x6E,
	CHAT_ACT_ALLOW_PM = 0x6F,
	CHAT_ACT_DISALLOW_PM = 0x70,
	CHAT_ACT_TOGGLE_PM = 0x71,
	CHAT_ACT_SHOW_NAME = 0x72,
	CHAT_ACT_HIDE_NAME = 0x73,
	CHAT_ACT_TOGGLE_NAME = 0x74,
	CHAT_ACT_WHOIS = 0x75,
	CHAT_ACT_KICK = 0x76,
	CHAT_ACT_ENABLE_VOICE_DEF = 0x77,
	CHAT_ACT_DISABLE_VOICE_DEF = 0x78,
	CHAT_ACT_TOGGLE_VOICE_DEF = 0x79,
	CHAT_ACT_EMOTE = 0x7A,
};

/* Tri-state action mode: set on, set off, or toggle. */
enum {
	CHAT_MODE_OFF = 0,
	CHAT_MODE_ON = 1,
	CHAT_MODE_TOGGLE = -1,
};

__extension__ typedef struct Conference Conference;
__extension__ typedef struct ChatUser ChatUser;

/*
 * A chat conference (the client's term for a channel). Membership is an
 * array grown on demand; moderator/voiced status is held per-user since a
 * ChatUser belongs to exactly one conference at a time.
 */
struct Conference {
	char name[CHAT_NAME_MAX];
	char password[CHAT_PASSWORD_MAX]; /* "" = no password */
	int hasPassword;
	int permanent;       /* seeded conference, not auto-removed when empty */
	int voiceRestricted; /* only moderators/voiced may speak */
	ChatUser **users;    /* member list, grown on demand */
	int userCount;
	int userCap;
	Conference *next;
};

/*
 * A player's chat session. Created when the player opens the chat window
 * and destroyed on leave or disconnect.
 */
struct ChatUser {
	CPlayer *player;
	Conference *conference; /* current conference, NULL if none */
	char username[CHAT_NAME_MAX];
	int moderator;          /* moderator of the current conference */
	int voiced;             /* has voice in the current conference */
	int anonymous;          /* hide character name from whois */
	int ignorePM;           /* block incoming private messages */
	uint32_t *ignored;      /* ignored account numbers, grown on demand */
	int ignoredCount;
	int ignoredCap;
	ChatUser *next;
};

/* Custom - head of the conference list. */
static Conference *g_conferences;
/* Custom - head of the active chat-user list. */
static ChatUser *g_chatUsers;

/*
 * Custom - Chat_ValidateName
 *
 * Validates a chat nickname or conference name: 2-31 printable ASCII
 * characters, not containing the reserved word "system".
 */
static int
Chat_ValidateName(const char *name)
{
	int len;
	int i;

	len = (int)strlen(name);
	if (len < 2 || len > 31)
		return 0;
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];
		if (c < 0x20 || c > 0x7E)
			return 0;
	}
	for (i = 0; i + 6 <= len; i++) {
		if (strncasecmp(name + i, "system", 6) == 0)
			return 0;
	}
	return 1;
}

/*
 * Custom - Chat_ResolveMode
 *
 * Resolves a tri-state action mode against the current flag value.
 */
static int
Chat_ResolveMode(int mode, int current)
{
	if (mode == CHAT_MODE_TOGGLE)
		return !current;
	return mode != 0;
}

/*
 * Custom - Chat_ParseConfName
 *
 * Parses a conference reference from a chat command parameter. The chat
 * client builds these from the conference pulldown: the conference name
 * is wrapped in double quotes, a complete command ends with a '$' marker,
 * and a password follows the closing quote after a space ("name"
 * password). The name is returned in place and *passwordOut points to the
 * password text, or is NULL when none was given. A bare unquoted name
 * (typed by hand, or sent by the test client) is also accepted.
 */
static char *
Chat_ParseConfName(char *param, char **passwordOut)
{
	char *open;
	char *name;
	char *rest;
	int n;

	*passwordOut = NULL;

	open = strchr(param, '"');
	if (open != NULL) {
		name = open + 1;
		rest = strchr(name, '"');
		if (rest != NULL)
			*rest++ = '\0';
		else
			rest = name + strlen(name);
	} else {
		name = param;
		rest = name + strlen(name);
	}

	/* After the name the client appends either a '$' completeness marker
	   or a space and the conference password. */
	while (*rest == ' ')
		rest++;
	n = (int)strlen(rest);
	while (n > 0 && rest[n - 1] == ' ')
		rest[--n] = '\0';
	if (n > 0 && rest[n - 1] == '$')
		rest[n - 1] = '\0';
	if (rest[0] != '\0')
		*passwordOut = rest;

	/* Trim a trailing '$' or spaces left on a bare, unquoted name. */
	n = (int)strlen(name);
	while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '$'))
		name[--n] = '\0';

	return name;
}

/*
 * Custom - Chat_SplitPassword
 *
 * Splits a "name{password}" conference-create parameter in place: truncates
 * param at the '{' so it is the bare conference name, and returns the
 * password between the braces (trimmed; an empty password becomes NULL).
 * Returns NULL when the parameter carries no brace-wrapped password. This
 * is the client's create-command format.
 */
static char *
Chat_SplitPassword(char *param)
{
	char *brace;
	char *close;
	char *password;
	int n;

	brace = strchr(param, '{');
	if (brace == NULL)
		return NULL;
	*brace = '\0';
	password = brace + 1;
	close = strchr(password, '}');
	if (close != NULL)
		*close = '\0';

	while (*password == ' ')
		password++;
	n = (int)strlen(password);
	while (n > 0 && password[n - 1] == ' ')
		password[--n] = '\0';
	return password[0] != '\0' ? password : NULL;
}

/*
 * Custom - Chat_SplitWord
 *
 * Splits a parameter at the first space in place and returns the text
 * after it, or NULL when the parameter has no space.
 */
static char *
Chat_SplitWord(char *param)
{
	char *sp;

	sp = strchr(param, ' ');
	if (sp == NULL)
		return NULL;
	*sp = '\0';
	return sp + 1;
}

/*
 * Custom - ChatUser_FindByPlayer
 */
static ChatUser *
ChatUser_FindByPlayer(CPlayer *player)
{
	ChatUser *u;

	for (u = g_chatUsers; u != NULL; u = u->next) {
		if (u->player == player)
			return u;
	}
	return NULL;
}

/*
 * Custom - ChatUser_FindByName
 */
static ChatUser *
ChatUser_FindByName(const char *name)
{
	ChatUser *u;

	for (u = g_chatUsers; u != NULL; u = u->next) {
		if (strcmp(u->username, name) == 0)
			return u;
	}
	return NULL;
}

/*
 * Custom - Conference_FindByName
 */
static Conference *
Conference_FindByName(const char *name)
{
	Conference *c;

	for (c = g_conferences; c != NULL; c = c->next) {
		if (strcmp(c->name, name) == 0)
			return c;
	}
	return NULL;
}

/*
 * Custom - Conference_FindUser
 *
 * Returns the named member of a conference, or NULL.
 */
static ChatUser *
Conference_FindUser(Conference *c, const char *name)
{
	int i;

	for (i = 0; i < c->userCount; i++) {
		if (strcmp(c->users[i]->username, name) == 0)
			return c->users[i];
	}
	return NULL;
}

/*
 * Custom - Chat_UserColor
 *
 * Returns the color digit the client prefixes to a speaker's name:
 * '1' for a moderator, '2' for a voiced user, '0' otherwise.
 */
static char
Chat_UserColor(ChatUser *u)
{
	if (u->moderator)
		return '1';
	if (u->voiced)
		return '2';
	return '0';
}

/*
 * Custom - Chat_FormatLabel
 *
 * Builds the "<color><name>" label the client expects in a speech message
 * and in a PLAYER_ENTER command: a single color digit (0 normal, 1
 * moderator, 2 voiced) followed by the chat username. The client reads the
 * color from the first character and the name from the rest, so the two
 * cannot be sent as separate parameters. buf must hold CHAT_NAME_MAX + 1
 * bytes.
 */
static void
Chat_FormatLabel(char *buf, ChatUser *u)
{
	buf[0] = Chat_UserColor(u);
	strncpy(buf + 1, u->username, CHAT_NAME_MAX - 1);
	buf[CHAT_NAME_MAX] = '\0';
}

/*
 * Custom - Chat_IsIgnoring
 *
 * Returns 1 if u has the given account number on its ignore list.
 */
static int
Chat_IsIgnoring(ChatUser *u, uint32_t accountNum)
{
	int i;

	if (accountNum == 0)
		return 0;
	for (i = 0; i < u->ignoredCount; i++) {
		if (u->ignored[i] == accountNum)
			return 1;
	}
	return 0;
}

/*
 * Custom - Chat_AccessLevel
 *
 * A user's effective staff access for chat moderation: 2 for a GameMaster,
 * 1 for a Counselor, 0 for an ordinary player. This reads the live player
 * flags - which the -gm switch also raises - not the stored account level.
 */
static int
Chat_AccessLevel(ChatUser *u)
{
	if (CPlayer_IsGameMaster(u->player))
		return 2;
	if (CPlayer_IsCounselor(u->player))
		return 1;
	return 0;
}

/*
 * Custom - Chat_SendToPlayer
 *
 * Builds a CHAT_MSG (0xB2) and sends it to one player.
 */
static void
Chat_SendToPlayer(CPlayer *player, uint16_t number, const char *p1, const char *p2)
{
	uint8_t buf[0x1000];

	if (player == NULL || player->usersock == NULL)
		return;
	PacketManager_MakePacket_CHAT_MSG(buf, number, p1, p2);
	SendToClient((CItem *)player, buf, -1);
}

/*
 * Custom - Chat_Send
 *
 * Builds a CHAT_MSG (0xB2) and sends it to one chat user.
 */
static void
Chat_Send(ChatUser *u, uint16_t number, const char *p1, const char *p2)
{
	Chat_SendToPlayer(u->player, number, p1, p2);
}

/*
 * Custom - Chat_GlobalSend
 *
 * Sends a CHAT_MSG to every chat user (used to broadcast conference
 * list additions and removals).
 */
static void
Chat_GlobalSend(uint16_t number, const char *p1, const char *p2)
{
	ChatUser *u;

	for (u = g_chatUsers; u != NULL; u = u->next)
		Chat_Send(u, number, p1, p2);
}

/*
 * Custom - Conference_Broadcast
 *
 * Sends a CHAT_MSG to every member of a conference, optionally skipping
 * one user.
 */
static void
Conference_Broadcast(Conference *c, ChatUser *skip, uint16_t number, const char *p1, const char *p2)
{
	int i;

	for (i = 0; i < c->userCount; i++) {
		if (c->users[i] != skip)
			Chat_Send(c->users[i], number, p1, p2);
	}
}

/*
 * Custom - Conference_BroadcastSpeech
 *
 * Sends a speech CHAT_MSG to every member of a conference, skipping any
 * member who is ignoring the speaker.
 */
static void
Conference_BroadcastSpeech(Conference *c, ChatUser *speaker, uint16_t number, const char *p1, const char *p2)
{
	uint32_t accountNum;
	int i;

	accountNum = speaker->player->accountNum;
	for (i = 0; i < c->userCount; i++) {
		if (c->users[i] != speaker && Chat_IsIgnoring(c->users[i], accountNum))
			continue;
		Chat_Send(c->users[i], number, p1, p2);
	}
}

/*
 * Custom - Chat_SendConferenceList
 *
 * Sends an ADD_CONFERENCE command for every conference to one user.
 */
static void
Chat_SendConferenceList(ChatUser *u)
{
	Conference *c;

	for (c = g_conferences; c != NULL; c = c->next)
		Chat_Send(u, CHAT_CMD_ADD_CONFERENCE, c->name, c->hasPassword ? "1" : "0");
}

/*
 * Custom - Conference_Create
 *
 * Creates a conference, appends it to the conference list, and
 * broadcasts ADD_CONFERENCE to every chat user.
 */
static Conference *
Conference_Create(const char *name, const char *password)
{
	Conference *c;
	Conference *tail;

	c = malloc(sizeof(Conference));
	memset(c, 0, sizeof(Conference));
	strncpy(c->name, name, CHAT_NAME_MAX - 1);
	if (password != NULL && password[0] != '\0') {
		strncpy(c->password, password, CHAT_PASSWORD_MAX - 1);
		c->hasPassword = 1;
	}

	/* Appended, not prepended: the seeded conference stays the list head,
	   so a new chat user auto-joins it rather than the newest conference. */
	tail = g_conferences;
	if (tail == NULL) {
		g_conferences = c;
	} else {
		while (tail->next != NULL)
			tail = tail->next;
		tail->next = c;
	}

	Chat_GlobalSend(CHAT_CMD_ADD_CONFERENCE, c->name, c->hasPassword ? "1" : "0");
	return c;
}

/*
 * Custom - Conference_Destroy
 *
 * Broadcasts REMOVE_CONFERENCE, unlinks the conference, and frees it.
 */
static void
Conference_Destroy(Conference *c)
{
	Conference **pp;

	Chat_GlobalSend(CHAT_CMD_REMOVE_CONFERENCE, c->name, NULL);

	for (pp = &g_conferences; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == c) {
			*pp = c->next;
			break;
		}
	}
	free(c->users);
	free(c);
}

/*
 * Custom - Conference_RemoveUser
 *
 * Removes a user from a conference: tells the remaining members the
 * player left, clears the leaver's player list, and destroys the
 * conference if it is now empty and not permanent.
 */
static void
Conference_RemoveUser(Conference *c, ChatUser *u)
{
	int i;

	for (i = 0; i < c->userCount; i++) {
		if (c->users[i] == u) {
			memmove(&c->users[i], &c->users[i + 1], (c->userCount - i - 1) * sizeof(ChatUser *));
			c->userCount--;
			break;
		}
	}

	u->conference = NULL;
	u->moderator = 0;
	u->voiced = 0;

	Conference_Broadcast(c, NULL, CHAT_CMD_PLAYER_LEAVE, u->username, NULL);
	Chat_Send(u, CHAT_CMD_PLAYER_CLEAR, NULL, NULL);

	if (c->userCount == 0 && !c->permanent)
		Conference_Destroy(c);
}

/*
 * Custom - Conference_AddUser
 *
 * Joins a user to a conference after a password check. The first member
 * of a non-permanent conference, and any GM, becomes a moderator. Sends
 * the conference name and member list to the joining user, announces the
 * newcomer to the existing members, and tells them when the newcomer
 * joins as a moderator. Returns 1 on success.
 */
static int
Conference_AddUser(Conference *c, ChatUser *u, const char *password)
{
	char label[CHAT_NAME_MAX + 1];
	int i;

	if (u->conference == c) {
		Chat_Send(u, CHAT_MSG_ALREADY_IN_CONFERENCE, c->name, NULL);
		return 1;
	}

	if (c->hasPassword && (password == NULL || strcasecmp(c->password, password) != 0)) {
		Chat_Send(u, CHAT_MSG_INCORRECT_PASSWORD, NULL, NULL);
		return 0;
	}

	if (u->conference != NULL)
		Conference_RemoveUser(u->conference, u);

	u->conference = c;
	u->moderator = 0;
	u->voiced = 0;
	if (Chat_AccessLevel(u) >= 2 || (!c->permanent && c->userCount == 0))
		u->moderator = 1;

	Chat_Send(u, CHAT_CMD_CONFERENCE_ENTER, c->name, NULL);

	for (i = 0; i < c->userCount; i++) {
		Chat_FormatLabel(label, c->users[i]);
		Chat_Send(u, CHAT_CMD_PLAYER_ENTER, label, NULL);
	}

	if (c->userCount == c->userCap) {
		c->userCap = c->userCap != 0 ? c->userCap * 2 : 8;
		c->users = realloc(c->users, c->userCap * sizeof(ChatUser *));
	}
	c->users[c->userCount++] = u;

	Chat_FormatLabel(label, u);
	Conference_Broadcast(c, u, CHAT_CMD_PLAYER_ENTER, label, NULL);
	Chat_Send(u, CHAT_CMD_PLAYER_ENTER, label, NULL);

	if (u->moderator)
		Conference_Broadcast(c, u, CHAT_MSG_NOW_MODERATOR, u->username, NULL);
	return 1;
}

/*
 * Custom - Conference_RefreshUser
 *
 * Re-announces a member to the conference so every client picks up a
 * changed moderator/voiced name color.
 */
static void
Conference_RefreshUser(Conference *c, ChatUser *u)
{
	char label[CHAT_NAME_MAX + 1];

	Chat_FormatLabel(label, u);
	Conference_Broadcast(c, NULL, CHAT_CMD_PLAYER_LEAVE, u->username, NULL);
	Conference_Broadcast(c, NULL, CHAT_CMD_PLAYER_ENTER, label, NULL);
}

/*
 * Custom - Chat_ResendConference
 *
 * Re-sends a user's current conference - the CONFERENCE_ENTER command and
 * a PLAYER_ENTER for every member - to that user's own client. The client
 * discards its ChatGump when the chat window closes, so a re-opened window
 * needs the conference it is already in pushed to it again.
 */
static void
Chat_ResendConference(ChatUser *u)
{
	Conference *c;
	char label[CHAT_NAME_MAX + 1];
	int i;

	c = u->conference;
	if (c == NULL)
		return;

	Chat_Send(u, CHAT_CMD_CONFERENCE_ENTER, c->name, NULL);
	for (i = 0; i < c->userCount; i++) {
		Chat_FormatLabel(label, c->users[i]);
		Chat_Send(u, CHAT_CMD_PLAYER_ENTER, label, NULL);
	}
}

/*
 * Custom - ChatUser_Add
 *
 * Handles a player opening the chat window: creates the chat user, sends
 * the conference list, and auto-joins the seeded conference. Closing the
 * window makes the client discard its ChatGump and send LEAVE_CHAT, so
 * the next open is a fresh start that resends the whole list and rejoins.
 * Should a CHAT_OPEN arrive while the chat user still exists, the list
 * and current conference are resent without creating a duplicate user.
 */
static ChatUser *
ChatUser_Add(CPlayer *player, const char *username)
{
	ChatUser *u;
	Conference *c;

	u = ChatUser_FindByPlayer(player);
	if (u != NULL) {
		Chat_SendConferenceList(u);
		Chat_ResendConference(u);
		return u;
	}

	u = malloc(sizeof(ChatUser));
	memset(u, 0, sizeof(ChatUser));
	u->player = player;
	strncpy(u->username, username, CHAT_NAME_MAX - 1);
	u->next = g_chatUsers;
	g_chatUsers = u;

	Chat_SendConferenceList(u);

	for (c = g_conferences; c != NULL; c = c->next) {
		if (Conference_AddUser(c, u, ""))
			break;
	}
	return u;
}

/*
 * Custom - ChatUser_Remove
 *
 * Removes a player from chat: leaves any conference, tells the client to
 * close the chat window, unlinks the user, and frees it.
 */
static void
ChatUser_Remove(ChatUser *u)
{
	ChatUser **pp;

	if (u->conference != NULL)
		Conference_RemoveUser(u->conference, u);

	Chat_Send(u, CHAT_CMD_CLOSE_CHAT_WINDOW, NULL, NULL);

	for (pp = &g_chatUsers; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == u) {
			*pp = u->next;
			break;
		}
	}
	free(u->ignored);
	free(u);
}

/*
 * Custom - Chat_RequireModerator
 *
 * Verifies the user is in a conference and is one of its moderators,
 * messaging the player and returning 0 when not.
 */
static int
Chat_RequireModerator(ChatUser *u)
{
	if (u->conference == NULL) {
		Chat_Send(u, CHAT_MSG_MUST_BE_IN_CONFERENCE, NULL, NULL);
		return 0;
	}
	if (!u->moderator) {
		Chat_Send(u, CHAT_MSG_MUST_HAVE_OPS, NULL, NULL);
		return 0;
	}
	return 1;
}

/*
 * Custom - Chat_ValidateAccess
 *
 * Verifies the acting moderator's staff access is at least the target's,
 * so a moderator cannot revoke the voice or moderator status of, or kick,
 * a higher-access staff member.
 */
static int
Chat_ValidateAccess(ChatUser *from, ChatUser *target)
{
	if (Chat_AccessLevel(from) < Chat_AccessLevel(target)) {
		CPlayer_SystemMessage(from->player, "Your access level is too low to do this.");
		return 0;
	}
	return 1;
}

/*
 * Custom - ChatAct_Message
 *
 * Action 0x61: broadcast a line of text to the speaker's conference.
 */
static void
ChatAct_Message(ChatUser *u, const char *text)
{
	char p1[CHAT_NAME_MAX + 1];

	if (u->conference == NULL) {
		Chat_Send(u, CHAT_MSG_MUST_BE_IN_CONFERENCE, NULL, NULL);
		return;
	}
	if (u->conference->voiceRestricted && !u->moderator && !u->voiced) {
		Chat_Send(u, CHAT_MSG_REVOKED_SPEAKING, NULL, NULL);
		return;
	}
	Chat_FormatLabel(p1, u);
	Conference_BroadcastSpeech(u->conference, u, CHAT_MSG_CHANNEL, p1, text);
}

/*
 * Custom - ChatAct_Emote
 *
 * Action 0x7A: broadcast an emote to the speaker's conference.
 */
static void
ChatAct_Emote(ChatUser *u, const char *text)
{
	char p1[CHAT_NAME_MAX + 1];

	if (u->conference == NULL) {
		Chat_Send(u, CHAT_MSG_MUST_BE_IN_CONFERENCE, NULL, NULL);
		return;
	}
	if (u->conference->voiceRestricted && !u->moderator && !u->voiced) {
		Chat_Send(u, CHAT_MSG_REVOKED_SPEAKING, NULL, NULL);
		return;
	}
	Chat_FormatLabel(p1, u);
	Conference_BroadcastSpeech(u->conference, u, CHAT_MSG_EMOTE, p1, text);
}

/*
 * Custom - ChatAct_JoinConference
 *
 * Action 0x62: join an existing conference. The client sends the
 * conference name in double quotes with an optional password.
 */
static void
ChatAct_JoinConference(ChatUser *u, char *param)
{
	char *password;
	char *name;
	Conference *c;

	name = Chat_ParseConfName(param, &password);
	c = Conference_FindByName(name);
	if (c == NULL) {
		Chat_Send(u, CHAT_MSG_NO_CONFERENCE, name, NULL);
		return;
	}
	Conference_AddUser(c, u, password != NULL ? password : "");
}

/*
 * Custom - ChatAct_CreateConference
 *
 * Action 0x63: create a conference, parameter "name{password}", and join
 * it. When a conference of that name already exists the player joins it
 * instead - the create and join paths converge.
 */
static void
ChatAct_CreateConference(ChatUser *u, char *param)
{
	char *password;
	Conference *c;

	password = Chat_SplitPassword(param);
	if (param[0] == '\0')
		return;
	c = Conference_FindByName(param);
	if (c == NULL)
		c = Conference_Create(param, password);
	Conference_AddUser(c, u, password != NULL ? password : "");
}

/*
 * Custom - ChatAct_RenameConference
 *
 * Action 0x64: rename the moderator's conference to the parameter text.
 * Conferences are keyed by name on the client, so the conference members
 * are told to drop the old entry and pick up the new one.
 */
static void
ChatAct_RenameConference(ChatUser *u, char *param)
{
	Conference *c;

	if (!Chat_RequireModerator(u))
		return;

	c = u->conference;
	Conference_Broadcast(c, NULL, CHAT_CMD_REMOVE_CONFERENCE, c->name, NULL);
	strncpy(c->name, param, CHAT_NAME_MAX - 1);
	c->name[CHAT_NAME_MAX - 1] = '\0';
	Conference_Broadcast(c, NULL, CHAT_CMD_ADD_CONFERENCE, c->name, c->hasPassword ? "1" : "0");
	Conference_Broadcast(c, NULL, CHAT_CMD_CONFERENCE_ENTER, c->name, NULL);
}

/*
 * Custom - ChatAct_ChangePassword
 *
 * Action 0x41: set or clear the moderator's conference password.
 */
static void
ChatAct_ChangePassword(ChatUser *u, char *param)
{
	Conference *c;

	if (!Chat_RequireModerator(u))
		return;

	c = u->conference;
	if (param[0] == '\0') {
		c->password[0] = '\0';
		c->hasPassword = 0;
	} else {
		strncpy(c->password, param, CHAT_PASSWORD_MAX - 1);
		c->password[CHAT_PASSWORD_MAX - 1] = '\0';
		c->hasPassword = 1;
	}
	Chat_Send(u, CHAT_MSG_PASSWORD_CHANGED, NULL, NULL);
	Chat_GlobalSend(CHAT_CMD_REMOVE_CONFERENCE, c->name, NULL);
	Chat_GlobalSend(CHAT_CMD_ADD_CONFERENCE, c->name, c->hasPassword ? "1" : "0");
}

/*
 * Custom - ChatAct_PrivateMessage
 *
 * Action 0x65: send a private message, parameter "recipient message".
 * The message reaches only the recipient.
 */
static void
ChatAct_PrivateMessage(ChatUser *u, char *param)
{
	char *text;
	ChatUser *target;
	char p1[CHAT_NAME_MAX + 1];

	text = Chat_SplitWord(param);
	if (text == NULL || text[0] == '\0') {
		CPlayer_SystemMessage(u->player, "Usage: select a chat user and a message.");
		return;
	}
	target = ChatUser_FindByName(param);
	if (target == NULL) {
		Chat_Send(u, CHAT_MSG_NO_PLAYER, param, NULL);
		return;
	}
	if (Chat_IsIgnoring(target, u->player->accountNum)) {
		Chat_Send(u, CHAT_MSG_PLAYER_IS_IGNORING, target->username, NULL);
		return;
	}
	if (target->ignorePM) {
		Chat_Send(u, CHAT_MSG_PLAYER_NOT_RECEIVING_PRIVATE, target->username, NULL);
		return;
	}

	Chat_FormatLabel(p1, u);
	Chat_Send(target, CHAT_MSG_PRIVATE, p1, text);
}

/*
 * Custom - ChatAct_Ignore
 *
 * Actions 0x66/0x67/0x68: add, remove, or toggle a chat user on the
 * caller's ignore list, parameter "username".
 */
static void
ChatAct_Ignore(ChatUser *u, const char *name, int mode)
{
	ChatUser *target;
	uint32_t accountNum;
	int ignoring;
	int want;
	int i;

	target = ChatUser_FindByName(name);
	if (target == NULL) {
		Chat_Send(u, CHAT_MSG_NO_PLAYER, name, NULL);
		return;
	}
	if (target == u)
		return;

	accountNum = target->player->accountNum;
	ignoring = Chat_IsIgnoring(u, accountNum);
	want = Chat_ResolveMode(mode, ignoring);

	if (want && !ignoring) {
		if (u->ignoredCount == u->ignoredCap) {
			u->ignoredCap = u->ignoredCap != 0 ? u->ignoredCap * 2 : 8;
			u->ignored = realloc(u->ignored, u->ignoredCap * sizeof(uint32_t));
		}
		u->ignored[u->ignoredCount++] = accountNum;
		Chat_Send(u, CHAT_MSG_NOW_IGNORING, target->username, NULL);
	} else if (!want && ignoring) {
		for (i = 0; i < u->ignoredCount; i++) {
			if (u->ignored[i] == accountNum) {
				u->ignored[i] = u->ignored[--u->ignoredCount];
				break;
			}
		}
		Chat_Send(u, CHAT_MSG_NO_LONGER_IGNORING, target->username, NULL);
		if (u->ignoredCount == 0)
			Chat_Send(u, CHAT_MSG_NO_LONGER_IGNORING_ANYONE, NULL, NULL);
	} else if (want) {
		Chat_Send(u, CHAT_MSG_ALREADY_IGNORING, target->username, NULL);
	} else {
		Chat_Send(u, CHAT_MSG_NOT_IGNORING, target->username, NULL);
	}
}

/*
 * Custom - ChatAct_Voice
 *
 * Actions 0x69/0x6A/0x6B: grant, revoke, or toggle speaking privileges
 * for a member of the moderator's conference, parameter "username".
 */
static void
ChatAct_Voice(ChatUser *u, const char *name, int mode)
{
	ChatUser *target;
	int voiced;

	if (!Chat_RequireModerator(u))
		return;
	target = Conference_FindUser(u->conference, name);
	if (target == NULL) {
		Chat_Send(u, CHAT_MSG_NO_PLAYER, name, NULL);
		return;
	}
	if ((mode == CHAT_MODE_OFF || (mode == CHAT_MODE_TOGGLE && target->voiced)) && !Chat_ValidateAccess(u, target))
		return;
	if (target->moderator)
		return;

	voiced = Chat_ResolveMode(mode, target->voiced);
	if (voiced == target->voiced)
		return;
	target->voiced = voiced;
	Conference_RefreshUser(u->conference, target);

	if (target->voiced) {
		Conference_Broadcast(u->conference, target, CHAT_MSG_NOW_SPEAKING, target->username, NULL);
		Chat_Send(target, CHAT_MSG_MODERATOR_GRANTED_SPEAKING, u->username, NULL);
	} else {
		Conference_Broadcast(u->conference, target, CHAT_MSG_NO_LONGER_SPEAKING, target->username, NULL);
		Chat_Send(target, CHAT_MSG_MODERATOR_REVOKED_SPEAKING, u->username, NULL);
	}
}

/*
 * Custom - ChatAct_Moderator
 *
 * Actions 0x6C/0x6D/0x6E: grant, revoke, or toggle moderator status for
 * a member of the moderator's conference, parameter "username".
 */
static void
ChatAct_Moderator(ChatUser *u, const char *name, int mode)
{
	ChatUser *target;
	int moderator;

	if (!Chat_RequireModerator(u))
		return;
	target = Conference_FindUser(u->conference, name);
	if (target == NULL) {
		Chat_Send(u, CHAT_MSG_NO_PLAYER, name, NULL);
		return;
	}
	if ((mode == CHAT_MODE_OFF || (mode == CHAT_MODE_TOGGLE && target->moderator)) && !Chat_ValidateAccess(u, target))
		return;

	moderator = Chat_ResolveMode(mode, target->moderator);
	if (moderator == target->moderator)
		return;
	target->moderator = moderator;
	if (target->moderator)
		target->voiced = 0;
	Conference_RefreshUser(u->conference, target);

	if (target->moderator) {
		Conference_Broadcast(u->conference, target, CHAT_MSG_NOW_MODERATOR, target->username, NULL);
		Chat_Send(target, CHAT_MSG_MADE_YOU_MODERATOR, u->username, NULL);
	} else {
		Conference_Broadcast(u->conference, target, CHAT_MSG_NO_LONGER_MODERATOR, target->username, NULL);
		Chat_Send(target, CHAT_MSG_REMOVED_AS_MODERATOR, u->username, NULL);
	}
}

/*
 * Custom - ChatAct_Kick
 *
 * Action 0x76: remove a member from the moderator's conference,
 * parameter "username".
 */
static void
ChatAct_Kick(ChatUser *u, const char *name)
{
	ChatUser *target;
	Conference *c;

	if (!Chat_RequireModerator(u))
		return;
	c = u->conference;
	target = Conference_FindUser(c, name);
	if (target == NULL) {
		Chat_Send(u, CHAT_MSG_NO_PLAYER, name, NULL);
		return;
	}
	if (target == u)
		return;
	if (!Chat_ValidateAccess(u, target))
		return;

	Chat_Send(target, CHAT_MSG_MODERATOR_KICKED_YOU, u->username, NULL);
	Conference_RemoveUser(c, target);
	Conference_Broadcast(c, NULL, CHAT_MSG_PLAYER_KICKED, target->username, NULL);
}

/*
 * Custom - ChatAct_TogglePM
 *
 * Actions 0x6F/0x70/0x71: allow, disallow, or toggle the caller's
 * receipt of private messages.
 */
static void
ChatAct_TogglePM(ChatUser *u, int mode)
{
	int allow;

	/* ignorePM is the inverse of "allow private messages". */
	allow = Chat_ResolveMode(mode, !u->ignorePM);
	u->ignorePM = !allow;
	Chat_Send(u, allow ? CHAT_MSG_RECEIVING_PRIVATE : CHAT_MSG_NO_LONGER_RECEIVING_PRIVATE, NULL, NULL);
}

/*
 * Custom - ChatAct_Anonymity
 *
 * Actions 0x72/0x73/0x74: show, hide, or toggle the caller's character
 * name for the whois command.
 */
static void
ChatAct_Anonymity(ChatUser *u, int mode)
{
	int hide;

	hide = Chat_ResolveMode(mode, u->anonymous);
	u->anonymous = hide;
	Chat_Send(u, hide ? CHAT_MSG_NOT_SHOWING_NAME : CHAT_MSG_SHOWING_NAME, NULL, NULL);
}

/*
 * Custom - ChatAct_WhoIs
 *
 * Action 0x75: report the character name behind a chat user's nickname,
 * parameter "username".
 */
static void
ChatAct_WhoIs(ChatUser *u, const char *name)
{
	ChatUser *target;
	const char *charName;

	target = ChatUser_FindByName(name);
	if (target == NULL) {
		Chat_Send(u, CHAT_MSG_NO_PLAYER, name, NULL);
		return;
	}
	if (target->anonymous) {
		Chat_Send(u, CHAT_MSG_PLAYER_ANONYMOUS, target->username, NULL);
		return;
	}
	charName = ((const char *(*)(void *))VT_FN((CItem *)target->player, VT_GET_NAME))((CItem *)target->player);
	Chat_Send(u, CHAT_MSG_PLAYER_KNOWN_AS, target->username, charName);
}

/*
 * Custom - ChatAct_DefaultVoice
 *
 * Actions 0x77/0x78/0x79: set whether everyone in the moderator's
 * conference may speak by default, or restrict speaking to moderators
 * and voiced users.
 */
static void
ChatAct_DefaultVoice(ChatUser *u, int mode)
{
	int everyone;

	if (!Chat_RequireModerator(u))
		return;
	everyone = Chat_ResolveMode(mode, !u->conference->voiceRestricted);
	u->conference->voiceRestricted = !everyone;
	Conference_Broadcast(u->conference, NULL, everyone ? CHAT_MSG_SPEAKING_DEFAULT : CHAT_MSG_MODERATORS_SPEAK_DEFAULT, NULL, NULL);
}

/*
 * Custom - PacketManager::HandlePacket(PDSTRUCT_CHAT_TEXT)
 *
 * Handles the inbound CHAT_TEXT (0xB3) packet: a 4-byte language code, a
 * 2-byte action id, and a UTF-16BE parameter string. Dispatches the
 * action to its handler. Decompiled from the client builder
 * MakePacket(PDSTRUCT_CHAT_TEXT) at 0x08081d4c and the client's
 * action table at commandAbbrevs (0x08128794).
 */
void
HandlePacket_CHAT_TEXT(CPlayer *player, uint8_t *buf)
{
	uint32_t off;
	char *lang;
	uint16_t action;
	char param[CHAT_PARAM_MAX];
	ChatUser *u;

	if (player == NULL)
		return;
	u = ChatUser_FindByPlayer(player);
	if (u == NULL)
		return;

	off = 0;
	GetString(buf, &off, &lang, 4);
	GetWord(buf, &off, &action);
	GetUnicodeBE(buf, &off, param, sizeof(param));
	(void)lang;

	switch (action) {
	case CHAT_ACT_MESSAGE:
		ChatAct_Message(u, param);
		break;
	case CHAT_ACT_EMOTE:
		ChatAct_Emote(u, param);
		break;
	case CHAT_ACT_LEAVE_CHAT:
		ChatUser_Remove(u);
		break;
	case CHAT_ACT_JOIN_CONFERENCE:
		ChatAct_JoinConference(u, param);
		break;
	case CHAT_ACT_CREATE_CONFERENCE:
		ChatAct_CreateConference(u, param);
		break;
	case CHAT_ACT_RENAME_CONFERENCE:
		ChatAct_RenameConference(u, param);
		break;
	case CHAT_ACT_CHANGE_PASSWORD:
		ChatAct_ChangePassword(u, param);
		break;
	case CHAT_ACT_PRIVATE_MESSAGE:
		ChatAct_PrivateMessage(u, param);
		break;
	case CHAT_ACT_ADD_IGNORE:
		ChatAct_Ignore(u, param, CHAT_MODE_ON);
		break;
	case CHAT_ACT_REMOVE_IGNORE:
		ChatAct_Ignore(u, param, CHAT_MODE_OFF);
		break;
	case CHAT_ACT_TOGGLE_IGNORE:
		ChatAct_Ignore(u, param, CHAT_MODE_TOGGLE);
		break;
	case CHAT_ACT_ADD_VOICE:
		ChatAct_Voice(u, param, CHAT_MODE_ON);
		break;
	case CHAT_ACT_REMOVE_VOICE:
		ChatAct_Voice(u, param, CHAT_MODE_OFF);
		break;
	case CHAT_ACT_TOGGLE_VOICE:
		ChatAct_Voice(u, param, CHAT_MODE_TOGGLE);
		break;
	case CHAT_ACT_ADD_MODERATOR:
		ChatAct_Moderator(u, param, CHAT_MODE_ON);
		break;
	case CHAT_ACT_REMOVE_MODERATOR:
		ChatAct_Moderator(u, param, CHAT_MODE_OFF);
		break;
	case CHAT_ACT_TOGGLE_MODERATOR:
		ChatAct_Moderator(u, param, CHAT_MODE_TOGGLE);
		break;
	case CHAT_ACT_ALLOW_PM:
		ChatAct_TogglePM(u, CHAT_MODE_ON);
		break;
	case CHAT_ACT_DISALLOW_PM:
		ChatAct_TogglePM(u, CHAT_MODE_OFF);
		break;
	case CHAT_ACT_TOGGLE_PM:
		ChatAct_TogglePM(u, CHAT_MODE_TOGGLE);
		break;
	case CHAT_ACT_SHOW_NAME:
		ChatAct_Anonymity(u, CHAT_MODE_OFF);
		break;
	case CHAT_ACT_HIDE_NAME:
		ChatAct_Anonymity(u, CHAT_MODE_ON);
		break;
	case CHAT_ACT_TOGGLE_NAME:
		ChatAct_Anonymity(u, CHAT_MODE_TOGGLE);
		break;
	case CHAT_ACT_WHOIS:
		ChatAct_WhoIs(u, param);
		break;
	case CHAT_ACT_KICK:
		ChatAct_Kick(u, param);
		break;
	case CHAT_ACT_ENABLE_VOICE_DEF:
		ChatAct_DefaultVoice(u, CHAT_MODE_ON);
		break;
	case CHAT_ACT_DISABLE_VOICE_DEF:
		ChatAct_DefaultVoice(u, CHAT_MODE_OFF);
		break;
	case CHAT_ACT_TOGGLE_VOICE_DEF:
		ChatAct_DefaultVoice(u, CHAT_MODE_TOGGLE);
		break;
	default:
		break;
	}
}

/*
 * Custom - PacketManager::HandlePacket(PDSTRUCT_CHAT_OPEN)
 *
 * Handles the inbound CHAT_OPEN (0xB5) packet: a pad byte and a UTF-16BE
 * nickname. The chat nickname is account-scoped, set once, immutable; an
 * empty or unknown nickname triggers the client's nickname dialog. The
 * 0xB5 layout is decompiled from the client builder MakePacket
 * (PDSTRUCT_CHAT_OPEN) at 0x08081d9c.
 */
void
HandlePacket_CHAT_OPEN(CPlayer *player, uint8_t *buf)
{
	uint32_t off;
	uint8_t pad;
	char name[CHAT_NAME_MAX];
	CAccount *acct;
	const char *nickname;

	if (player == NULL)
		return;

	off = 0;
	GetByte(buf, &off, &pad);
	GetUnicodeBE(buf, &off, name, sizeof(name));
	(void)pad;

	acct = Account_FindByNum(player->accountNum);

	if (acct != NULL && acct->chatName[0] != '\0') {
		if (name[0] != '\0' && strcmp(name, acct->chatName) != 0)
			CPlayer_SystemMessage(player, "You cannot change your chat nickname once it has been set.");
		nickname = acct->chatName;
	} else {
		if (name[0] == '\0') {
			Chat_SendToPlayer(player, CHAT_CMD_ASK_NEW_NICKNAME, NULL, NULL);
			return;
		}
		if (!Chat_ValidateName(name) || ChatUser_FindByName(name) != NULL || Account_FindByChatName(name) != NULL) {
			Chat_SendToPlayer(player, CHAT_CMD_ASK_NEW_NICKNAME, NULL, NULL);
			return;
		}
		if (acct != NULL)
			Account_SetChatName(acct, name);
		nickname = (acct != NULL) ? acct->chatName : name;
	}

	Chat_SendToPlayer(player, CHAT_CMD_OPEN_CHAT_WINDOW, nickname, NULL);
	ChatUser_Add(player, nickname);
}

/*
 * Custom - Chat_Init
 *
 * Seeds the permanent default conference at server startup.
 */
void
Chat_Init(void)
{
	Conference *c;

	c = Conference_Create("Newbie Help", NULL);
	c->permanent = 1;
}

/*
 * Custom - Chat_OnPlayerDisconnect
 *
 * Removes a disconnecting player from the chat system.
 */
void
Chat_OnPlayerDisconnect(CPlayer *player)
{
	ChatUser *u;

	u = ChatUser_FindByPlayer(player);
	if (u != NULL)
		ChatUser_Remove(u);
}
