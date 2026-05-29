#ifndef ENTITYMANAGER_H_
#define ENTITYMANAGER_H_

#include <stdint.h>

__extension__ typedef struct CItem CItem;
__extension__ typedef struct CVector CVector;
__extension__ typedef struct StdPtrList StdPtrList;

/*
 * Placeholder shell around the global entity registry (4 bytes). The
 * actual registry is the g_EntitySerialHash chain and the StdPtrList
 * exposed via EntityManager_GetListPtr; the constructor only zeroes
 * field0.
 */
__extension__ typedef struct CEntityManager CEntityManager;
struct CEntityManager {
	uint32_t field0; // 0x00
};

void ArchiveHash_Insert(CItem *item); // 0x0049196F
void ArchiveHash_Remove(CItem *item); // 0x004919C2
CItem *ArchiveHash_Find(uint32_t serial); // 0x00491A26
StdPtrList *EntityManager_GetListPtr(void); // 0x00491A65
CItem *EntityManager_RestoreBySerial(uint32_t serial); // 0x00491A74
void BroadcastDestroyAndRemove(CItem *entity); // 0x00491AA7
void EntityManager_AddAllToWorld(void); // 0x00491B48
void EntityManager_RemoveAllFromWorld(void); // 0x00491BBF
void EntityManager_CollectByAccountID(CVector *results, uint32_t accountId); // 0x00491C36
CItem *EntityManager_FindByName(char *name); // 0x00491CC4
CItem *EntityManager_FindBySerial(uint32_t serial); // 0x00491D63
CEntityManager *CEntityManager_Constructor(CEntityManager *this); // 0x00492C6B
void CEntityManager_Destructor(CEntityManager *this); // 0x00492C87

void EntityManager_ShutdownArchive(void); // Custom

#endif /* ENTITYMANAGER_H_ */
