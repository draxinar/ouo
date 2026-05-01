#ifndef DYNAMIC_H_
#define DYNAMIC_H_

/*
 * Dynamic object save/load via dynidx0.mul + dynamic0.mul. Entities
 * not resolvable inline during ParseBlock (master/follower pairs where
 * the master isn't loaded yet) are pushed onto a deferred-link list and
 * fixed up once all blocks have been read.
 */

#include <stdint.h>
#include <stdio.h>

__extension__ typedef struct CBulletinBoard CBulletinBoard;
__extension__ typedef struct CContainer CContainer;
__extension__ typedef struct CCorpse CCorpse;
__extension__ typedef struct CItem CItem;
__extension__ typedef struct CMobile CMobile;
__extension__ typedef struct CMultiComponent CMultiComponent;
__extension__ typedef struct CMultiSlave CMultiSlave;
__extension__ typedef struct CNPC CNPC;
__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CSerialList CSerialList;
__extension__ typedef struct CSignpost CSignpost;

/*
 * Paired index+data MUL files (e.g. dynidx0.mul + dynamic0.mul). 12 bytes
 * on 32-bit; constructed on the stack by the save/load paths.
 */
__extension__ typedef struct CIndexedFileManager CIndexedFileManager;
// clang-format off
struct CIndexedFileManager {
	FILE *indexFile;                // 0x00
	FILE *dataFile;                 // 0x04
	char *mode;                     // 0x08
};
// clang-format on

/*
 * Growable byte buffer used for save serialization (12 bytes). The
 * constructor allocates 0x400 bytes; entries are NUL-terminated strings.
 */
__extension__ typedef struct CDataBuffer CDataBuffer;
struct CDataBuffer {
	uint8_t *data;
	int len;
	int cap;
};

/*
 * One pending master/follower link on the g_deferredList chain at
 * 0x006E76A0 (16 bytes; allocated by AddDeferredContainerLink).
 * Resolved by ProcessDeferredContainerLinks once loading completes.
 */
__extension__ typedef struct DeferredContainerLink DeferredContainerLink;
struct DeferredContainerLink {
	CItem *child;                   // 0x00
	uint32_t parentSerial;          // 0x04
	uint32_t skipFlag;              // 0x08 - nonzero: skip this link
	DeferredContainerLink *next;    // 0x0C
};

extern CBulletinBoard *g_BBoardHead; // 0x006933A8
extern int g_BBoardBroadcastMode; // 0x006DA938
extern int g_pendingLoad; // 0x006EF6A8

void CIndexedFileManager_Constructor(CIndexedFileManager *this); // 0x004654E0
void CIndexedFileManager_Destructor(CIndexedFileManager *this); // 0x0046550B
void CIndexedFileManager_Open(CIndexedFileManager *this, char *indexPath, char *dataPath, char *mode); // 0x00465544
void CIndexedFileManager_Close(CIndexedFileManager *this); // 0x00465585
void CIndexedFileManager_Repack(CIndexedFileManager *this, char *indexPath, char *dataPath); // 0x004655DB
void CIndexedFileManager_WriteBlock(CIndexedFileManager *this, int blockIdx, uint8_t *data, int dataLen, int extra); // 0x00465C37
void CIndexedFileManager_ReadBlock(CIndexedFileManager *this, int blockIdx, uint8_t **outData, int *outLen, int *outExtra); // 0x00465F39
void AddDeferredContainerLink(CItem *child, uint32_t parentSerial, uint32_t skipFlag); // 0x004C4020
void ProcessDeferredContainerLinks(void); // 0x004C406C
void CMultiComponent_Save(CMultiComponent *mc, CDataBuffer *buf, int isComponent); // 0x004C5716
void CMultiSlave_Save(CMultiSlave *slave, CDataBuffer *buf, int isComponent); // 0x004C5844
void CItem_Save(CItem *obj, CDataBuffer *b, int writeMarker); // 0x004C59EF
void CContainer_Save(CContainer *cont, CDataBuffer *b, int writeMarker); // 0x004C6A1D
void CMulti_Save(CCorpse *corpse, CDataBuffer *b, int writeMarker); // 0x004C6A7F
void CBoard_Save(CContainer *cont, CDataBuffer *b, int writeMarker); // 0x004C6B76
void CSignpost_Save(CSignpost *sp, CDataBuffer *b, int writeMarker); // 0x004C6BA6
void CWeapon_Save(CContainer *cont, CDataBuffer *b, int writeMarker); // 0x004C6CC9
void SerialList_Save(CSerialList *list, char *buf, char *name, CDataBuffer *b); // 0x004C6F3A
void CMobile_Save(CMobile *mob, CDataBuffer *b, int writeMarker); // 0x004C6FD1
void CNPC_Save(CNPC *npc, CDataBuffer *b, int writeMarker); // 0x004C7D7E
void CGuard_Save(CNPC *npc, CDataBuffer *b, int writeMarker); // 0x004C832B
void CShopkeeper_Save(CNPC *npc, CDataBuffer *b, int writeMarker); // 0x004C835B
void CEgg_Save(CItem *item, CDataBuffer *b, int writeMarker); // 0x004C83D0
void CPlayer_Save(CPlayer *player, CDataBuffer *b, int writeMarker); // 0x004C842C
void SaveDynamic0(void); // 0x004C8A5C
void LoadDynamic0(void); // 0x004C8DD7
void Dynamic_FireObjectLoadedEvents(void); // 0x004C90CC
void Dynamic_SetPendingLoad(void); // 0x004C9161
void Dynamic_ClearPendingAndFireEvents(void); // 0x004C9170
void BackupFile(const char *src, const char *dst);
void CDataBuffer_WriteField(CDataBuffer *b, const char *key, const char *value);
void CDataBuffer_WriteInt(CDataBuffer *b, const char *key, int value);
void Dynamic_AddDeferredLoadedSerial(uint32_t serial);
void Dynamic_LoadEntity(CItem *entity, const char *name, int type, uintptr_t value); // 0x004C8EEB

#endif /* DYNAMIC_H_ */
