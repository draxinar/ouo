/*
 * Custom - Account Management
 *
 * CUSTOM system restoring the account management that was stubbed out in
 * UoDemo.exe. The binary has the infrastructure (CollectByAccountID at
 * 0x00450BA5, getAccountNum/getCharacterNum script builtins, acct=%d %d save
 * format, access.list FileManager entry at type 0x01) but the login handlers
 * ignore credentials and show all characters to every connection.
 *
 * Design matches the probable original OSI 1998 implementation:
 * - Flat-file storage in access.list (one line per account)
 * - SHA-256 hashed passwords with 8-byte random salt
 * - Auto-create accounts on first login
 * - Per-account character lists via the binary's own CollectByAccountID
 * - Account number is the persistent binding key, stored on characters via
 *   the binary's existing acct=%d %d save tag
 */
#ifndef ACCOUNT_H_
#define ACCOUNT_H_

#include <stdint.h>

__extension__ typedef struct CAccount CAccount;

/*
 * Account record keyed by login, chained in a hash bucket.
 */
struct CAccount {
	char login[31];         // max length per login packet field
	uint8_t salt[8];
	uint8_t passHash[32];   // SHA256(salt + password)
	uint32_t accountNum;    // persistent binding key, stored on characters
	uint32_t flags;
	uint8_t plevel;         // 0=player, 1=counselor, 2=GM
	uint8_t _pad_plevel[3];
	char chatName[32];      // CUSTOM: persistent UO chat nickname, "" = unset
#if __SIZEOF_POINTER__ == 8
	uint32_t _pad64;        // 64-bit alignment pad
#endif
	CAccount *next;
};

/*
 * Bit flags for CAccount.flags.
 */
enum AccountFlag {
	ACCT_BANNED = 0x01,
	ACCT_GODMODE = 0x02,
};

void Account_Init(void);
int Account_LoadAll(void);
void Account_ReloadAll(void);
void Account_SaveAll(void);
CAccount *Account_FindByLogin(const char *login);
CAccount *Account_FindByNum(uint32_t accountNum);
CAccount *Account_FindByChatName(const char *chatName);
void Account_SetChatName(CAccount *acct, const char *chatName);
int Account_CheckPassword(CAccount *acct, const char *password);
CAccount *Account_Create(const char *login, const char *password);
CAccount *Account_FindOrCreate(const char *login, const char *password);
int Account_Count(void);
void Log_Auth(uint32_t addr, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void Log_Game(uint32_t addr, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* ACCOUNT_H_ */
