/*
 * Account management - flat-file account storage backing the login flow.
 *
 * Logins hash into an in-memory table keyed off access.list, with
 * SHA256 + 8-byte salt password hashes stored as salt_hex:hash_hex.
 * Accounts auto-create on first successful login.
 *
 * CUSTOM - no binary equivalent.
 */
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "account.h"
#include "log.h"
#include "sha256.h"

static uint32_t hashLogin(const char *login);
static int Account_ValidateLogin(const char *login);
static int Account_ValidatePassword(const char *password);
static void generateSalt(uint8_t *salt);
static void computePassHash(const uint8_t *salt, const char *password, uint8_t *out);
static void hexEncode(const uint8_t *data, int len, char *out);
static int hexDecode(const char *hex, uint8_t *out, int len);
static void copyChatName(char *dst, const char *src);

#define ACCOUNT_HASH_SIZE 64

// Custom - login-to-account hash table
static CAccount *accountHash[ACCOUNT_HASH_SIZE];
// Custom - next sequential account number to assign
static uint32_t nextAccountNum = 1;

// Custom - path to the account database file
static const char *accessListPath = "../.rundir/access.list";

static uint32_t
hashLogin(const char *login)
{
	uint32_t h = 5381;
	const unsigned char *p = (const unsigned char *)login;

	while (*p) {
		h = h * 33 + tolower(*p);
		p++;
	}
	return h % ACCOUNT_HASH_SIZE;
}

static void
generateSalt(uint8_t *salt)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		read(fd, salt, 8);
		close(fd);
	} else {
		int i;
		for (i = 0; i < 8; i++)
			salt[i] = (uint8_t)rand();
	}
}

static void
computePassHash(const uint8_t *salt, const char *password, uint8_t *out)
{
	Sha256Ctx ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, salt, 8);
	sha256_update(&ctx, (const uint8_t *)password, strlen(password));
	sha256_final(&ctx, out);
}

static void
hexEncode(const uint8_t *data, int len, char *out)
{
	int i;
	for (i = 0; i < len; i++)
		sprintf(out + i * 2, "%02x", data[i]);
}

static int
hexDecode(const char *hex, uint8_t *out, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		unsigned int val;
		if (sscanf(hex + i * 2, "%2x", &val) != 1)
			return 0;
		out[i] = (uint8_t)val;
	}
	return 1;
}

/*
 * Custom - copyChatName
 *
 * Copies the optional trailing chat-name field of an access.list line into
 * a 32-byte account field. The chat name is the last field and may contain
 * spaces, so it is taken as the rest of the line: leading whitespace is
 * skipped, the line terminator and trailing spaces are stripped.
 */
static void
copyChatName(char *dst, const char *src)
{
	int i;

	while (*src == ' ' || *src == '\t')
		src++;
	i = 0;
	while (src[i] != '\0' && src[i] != '\n' && src[i] != '\r' && i < 31) {
		dst[i] = src[i];
		i++;
	}
	while (i > 0 && dst[i - 1] == ' ')
		i--;
	dst[i] = '\0';
}

/*
 * Custom - Account_Init
 */
void
Account_Init(void)
{
	memset(accountHash, 0, sizeof(accountHash));
	nextAccountNum = 1;
}

/*
 * Custom - Account_LoadAll
 *
 * File format: login salt_hex:hash_hex accountNum flags plevel
 */
int
Account_LoadAll(void)
{
	FILE *fp;
	char line[256];
	char login[31], credential[128];
	uint32_t accountNum, flags;
	unsigned int plevel;
	int count = 0;

	fp = fopen(accessListPath, "r");
	if (fp == NULL)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {
		int consumed = 0;
		if (sscanf(line, "%30s %127s %u %u %u%n", login, credential, &accountNum, &flags, &plevel, &consumed) != 5)
			continue;

		CAccount *acct = malloc(sizeof(CAccount));
		strncpy(acct->login, login, 30);
		acct->login[30] = '\0';
		acct->accountNum = accountNum;
		acct->flags = flags;
		acct->plevel = (uint8_t)plevel;
		copyChatName(acct->chatName, line + consumed);

		char *colon = strchr(credential, ':');
		if (colon != NULL && colon - credential == 16 && strlen(colon + 1) == 64) {
			hexDecode(credential, acct->salt, 8);
			hexDecode(colon + 1, acct->passHash, 32);
		} else {
			{
				char buf[128];
				snprintf(buf, sizeof(buf), "skipping '%s' (bad credential format)", login);
				EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "account", "misc", buf);
			}
			free(acct);
			continue;
		}

		uint32_t bucket = hashLogin(acct->login);
		acct->next = accountHash[bucket];
		accountHash[bucket] = acct;

		if (accountNum >= nextAccountNum)
			nextAccountNum = accountNum + 1;
		count++;
	}

	fclose(fp);
	return count;
}

/*
 * Custom - Account_SaveAll
 */
void
Account_SaveAll(void)
{
	FILE *fp;
	int i;
	char saltHex[17], hashHex[65];

	fp = fopen(accessListPath, "w");
	if (fp == NULL) {
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "account", "misc", "failed to save access.list");
		return;
	}

	for (i = 0; i < ACCOUNT_HASH_SIZE; i++) {
		CAccount *acct;
		for (acct = accountHash[i]; acct != NULL; acct = acct->next) {
			hexEncode(acct->salt, 8, saltHex);
			hexEncode(acct->passHash, 32, hashHex);
			if (acct->chatName[0] != '\0')
				fprintf(fp, "%s %s:%s %u %u %u %s\n", acct->login, saltHex, hashHex, acct->accountNum, acct->flags, (unsigned)acct->plevel, acct->chatName);
			else
				fprintf(fp, "%s %s:%s %u %u %u\n", acct->login, saltHex, hashHex, acct->accountNum, acct->flags, (unsigned)acct->plevel);
		}
	}

	fclose(fp);
}

/*
 * Custom - Account_ReloadAll
 *
 * Re-reads access.list and updates flags/plevel on existing accounts.
 * New accounts found in the file are added. Existing accounts not in the
 * file are left in memory (they may have active sessions).
 * Called from the main loop in response to SIGHUP.
 */
void
Account_ReloadAll(void)
{
	FILE *fp;
	char line[256];
	char login[31], credential[128];
	uint32_t accountNum, flags;
	unsigned int plevel;
	int updated = 0, added = 0;

	fp = fopen(accessListPath, "r");
	if (fp == NULL) {
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "account", "misc", "reload failed - cannot open access.list");
		return;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		int consumed = 0;
		if (sscanf(line, "%30s %127s %u %u %u%n", login, credential, &accountNum, &flags, &plevel, &consumed) != 5)
			continue;

		CAccount *acct = Account_FindByLogin(login);
		if (acct != NULL) {
			acct->flags = flags;
			acct->plevel = (uint8_t)plevel;
			copyChatName(acct->chatName, line + consumed);
			char *colon = strchr(credential, ':');
			if (colon != NULL && colon - credential == 16 && strlen(colon + 1) == 64) {
				hexDecode(credential, acct->salt, 8);
				hexDecode(colon + 1, acct->passHash, 32);
			}
			updated++;
		} else {
			char *colon = strchr(credential, ':');
			if (colon == NULL || colon - credential != 16 || strlen(colon + 1) != 64)
				continue;
			acct = malloc(sizeof(CAccount));
			strncpy(acct->login, login, 30);
			acct->login[30] = '\0';
			acct->accountNum = accountNum;
			acct->flags = flags;
			acct->plevel = (uint8_t)plevel;
			copyChatName(acct->chatName, line + consumed);
			hexDecode(credential, acct->salt, 8);
			hexDecode(colon + 1, acct->passHash, 32);

			uint32_t bucket = hashLogin(acct->login);
			acct->next = accountHash[bucket];
			accountHash[bucket] = acct;

			if (accountNum >= nextAccountNum)
				nextAccountNum = accountNum + 1;
			added++;
		}
	}

	fclose(fp);
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "reloaded access.list (%d updated, %d added)", updated, added);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "account", "misc", buf);
	}
}

/*
 * Custom - Account_FindByLogin
 */
CAccount *
Account_FindByLogin(const char *login)
{
	uint32_t bucket = hashLogin(login);
	CAccount *acct;

	for (acct = accountHash[bucket]; acct != NULL; acct = acct->next) {
		if (strcasecmp(acct->login, login) == 0)
			return acct;
	}
	return NULL;
}

/*
 * Custom - Account_FindByNum
 *
 * Resolve an accountNum back to its CAccount record by scanning all hash
 * buckets. Accounts are keyed by login name in the hash; this scan is used
 * by paths that only have the numeric accountNum (e.g. archived players
 * without a live usersock).
 */
CAccount *
Account_FindByNum(uint32_t accountNum)
{
	int i;

	if (accountNum == 0)
		return NULL;

	for (i = 0; i < ACCOUNT_HASH_SIZE; i++) {
		CAccount *acct;
		for (acct = accountHash[i]; acct != NULL; acct = acct->next) {
			if (acct->accountNum == accountNum)
				return acct;
		}
	}
	return NULL;
}

/*
 * Custom - Account_FindByChatName
 *
 * Returns the account whose persistent chat nickname matches chatName
 * (case-insensitive), or NULL. Used to enforce globally-unique chat
 * nicknames when a player first picks one.
 */
CAccount *
Account_FindByChatName(const char *chatName)
{
	int i;

	if (chatName == NULL || chatName[0] == '\0')
		return NULL;

	for (i = 0; i < ACCOUNT_HASH_SIZE; i++) {
		CAccount *acct;
		for (acct = accountHash[i]; acct != NULL; acct = acct->next) {
			if (strcasecmp(acct->chatName, chatName) == 0)
				return acct;
		}
	}
	return NULL;
}

/*
 * Custom - Account_SetChatName
 *
 * Stores a player's chat nickname on the account and persists access.list.
 * The nickname is account-scoped and set once; callers enforce immutability.
 */
void
Account_SetChatName(CAccount *acct, const char *chatName)
{
	strncpy(acct->chatName, chatName, 31);
	acct->chatName[31] = '\0';
	Account_SaveAll();
}

/*
 * Custom - Account_CheckPassword
 */
int
Account_CheckPassword(CAccount *acct, const char *password)
{
	uint8_t checkHash[32];

	computePassHash(acct->salt, password, checkHash);
	return memcmp(acct->passHash, checkHash, 32) == 0;
}

/*
 * Custom - Account_Create
 */
CAccount *
Account_Create(const char *login, const char *password)
{
	CAccount *acct;
	uint32_t bucket;

	acct = malloc(sizeof(CAccount));
	strncpy(acct->login, login, 30);
	acct->login[30] = '\0';
	generateSalt(acct->salt);
	computePassHash(acct->salt, password, acct->passHash);
	acct->accountNum = nextAccountNum++;
	acct->flags = 0;
	acct->plevel = 0;
	acct->chatName[0] = '\0';

	bucket = hashLogin(acct->login);
	acct->next = accountHash[bucket];
	accountHash[bucket] = acct;

	Account_SaveAll();

	{
		char buf[128];
		snprintf(buf, sizeof(buf), "created '%s' accountNum=%u", acct->login, acct->accountNum);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "account", "misc", buf);
	}
	return acct;
}

/*
 * Custom - Account_ValidateLogin
 *
 * Validates a login name. Rules:
 * - Must be non-empty
 * - Only printable ASCII (0x20-0x7E)
 * - No leading or trailing spaces
 * - No trailing period
 * - Forbidden characters: < > : " / \ | ? *
 * Returns 1 if valid, 0 if invalid.
 */
static int
Account_ValidateLogin(const char *login)
{
	int len;
	const unsigned char *p;

	if (login == NULL || login[0] == '\0')
		return 0;

	len = strlen(login);

	if (login[0] == ' ' || login[len - 1] == ' ')
		return 0;

	if (login[len - 1] == '.')
		return 0;

	for (p = (const unsigned char *)login; *p; p++) {
		if (*p < 0x20 || *p > 0x7E)
			return 0;
		if (*p == '<' || *p == '>' || *p == ':' || *p == '"' || *p == '/' || *p == '\\' || *p == '|' || *p == '?' || *p == '*')
			return 0;
	}

	return 1;
}

/*
 * Custom - Account_ValidatePassword
 *
 * Validates a password. Rules:
 * - Must be non-empty
 * - Only printable ASCII (0x20-0x7E)
 * Returns 1 if valid, 0 if invalid.
 */
static int
Account_ValidatePassword(const char *password)
{
	const unsigned char *p;

	if (password == NULL || password[0] == '\0')
		return 0;

	for (p = (const unsigned char *)password; *p; p++) {
		if (*p < 0x20 || *p > 0x7E)
			return 0;
	}

	return 1;
}

/*
 * Custom - Account_FindOrCreate
 */
CAccount *
Account_FindOrCreate(const char *login, const char *password)
{
	CAccount *acct;

	if (!Account_ValidateLogin(login) || !Account_ValidatePassword(password))
		return NULL;

	acct = Account_FindByLogin(login);
	if (acct == NULL)
		return Account_Create(login, password);

	if (acct->flags & ACCT_BANNED)
		return NULL;
	if (!Account_CheckPassword(acct, password))
		return NULL;

	return acct;
}

/*
 * Custom - Account_Count
 *
 * Returns the total number of registered accounts.
 */
int
Account_Count(void)
{
	int count = 0;
	int i;

	for (i = 0; i < ACCOUNT_HASH_SIZE; i++) {
		CAccount *acct;
		for (acct = accountHash[i]; acct != NULL; acct = acct->next)
			count++;
	}
	return count;
}

/*
 * Custom - Log_Auth
 *
 * Logs an authentication event via EventLogger_Log with category "auth".
 * Callers provide a printf-style format for the message body; the client
 * IP is appended automatically before passing to EventLogger_Log.
 */
void
Log_Auth(uint32_t addr, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	snprintf(buf + n, sizeof(buf) - n, " from %u.%u.%u.%u", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);

	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "auth", "misc", buf);
}

/*
 * Custom - Log_Game
 *
 * Logs a game connection event via EventLogger_Log with category "game".
 * Used for events after the login-to-game reconnection: character
 * create/enter/delete, client version, disconnect.
 */
void
Log_Game(uint32_t addr, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	snprintf(buf + n, sizeof(buf) - n, " from %u.%u.%u.%u", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);

	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "game", "misc", buf);
}
