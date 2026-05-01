#ifndef SCOMMAND_H_
#define SCOMMAND_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
__extension__ typedef struct SCommandManager SCommandManager;
__extension__ typedef struct SCommandEntry SCommandEntry;

/*
 * Singleton .command dispatcher at g_SCommandManager (0x00699910).
 * Just a vtable slot in the binary.
 */
struct SCommandManager {
	uintptr_t *vtable;      // +0x00
};

/*
 * One row of the static .command table at g_SCommandTable
 * (0x0063F968).
 */
struct SCommandEntry {
	char *name;
	int (*handler)(CItem *player, uint32_t serial, char *args, int len);
};

void SCommandManager_Constructor(SCommandManager *mgr); // 0x00447420
char *GetValue(char *line, char *key); // 0x0044744B
char *ObjVar_ParseToken(const char *input, char *output); // 0x00447512
char *SCommand_ParseToken(char *input, char *output); // 0x004475AB
void SCommandManager_Execute(SCommandManager *mgr, CItem *mob, uint32_t serial, char *text, int extra); // 0x0044791F

#endif /* SCOMMAND_H_ */
