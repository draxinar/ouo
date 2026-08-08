/*
 * Dynamic object persistence - mutable world state save and load.
 *
 * Reads dynidx0.mul + dynamic0.mul on boot and writes them (plus
 * .bkp backups) on save. Entity references not resolvable inline
 * during ParseBlock are pushed onto a deferred-link list and fixed up
 * once all blocks are loaded.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dat.h"

#include "bboard.h"
#include "combat.h"
#include "container.h"
#include "corpse.h"
#include "dynamic.h"
#include "egg.h"
#include "entitymanager.h"
#include "fns.h"
#include "io.h"
#include "multi.h"
#include "npc.h"
#include "objvar.h"
#include "packet_handler.h"
#include "player.h"
#include "region.h"
#include "shopkeeper.h"
#include "signpost.h"
#include "taglist.h"
#include "timer.h"
#include "utils.h"
#include "vtable.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "world.h"

static char *ReadToken(char *src, char *dst); // 0x004C8BB7
static CItem *FindContainerBySerial(CItem *container, uint32_t serial); // 0x004C8C1F
static CItem *FindParentBySerial(uint32_t serial); // 0x004C8D14
static CItem *FindParentInBlock(CItem *blockItemHead, uint32_t serial); // 0x004C8D4C
static void Dynamic_ParseDeferredSerial(CItem *entity, CSerialList *list, const char *str); // 0x004C8FFA
static void Dynamic_InitDeferredSerials(void); // 0x004C9095
static void Dynamic_atexitDeferredSerials(void); // 0x004C90AB
static void Dynamic_atexitDeferredSerials_thunk(void); // 0x004C90BD
static void LoadDynamic0_ParseBlock(int blockIdx, char *data, int dataLen, CItem *recursiveParent, CLocation *recursiveLoc); // 0x004C9184

CBulletinBoard *g_BBoardHead; // 0x006933A8

int g_BBoardBroadcastMode; // 0x006DA938

// 0x0064B358 = g_World->isLoading (g_World+0x08).
// Set to 1 during dynamic loading; temporarily cleared for event dispatch.

// 0x006E76A0 - deferred master/follower links (resolved after all blocks load)
static DeferredContainerLink *g_deferredList;

// 0x006E7690 - CVector of serials loaded during LoadDynamic0
static CVector g_deferredLoadedSerials;

/*
 * 0x004654E0 - CIndexedFileManager::CIndexedFileManager
 *
 * Zero-initializes the data/index FILE* pair and mode string.
 */
void
CIndexedFileManager_Constructor(CIndexedFileManager *this)
{
	this->dataFile = NULL;
	this->indexFile = NULL;
	this->mode = NULL;
}

/*
 * 0x0046550B - CIndexedFileManager::~CIndexedFileManager
 *
 * Closes indexFile and dataFile without zeroing the struct fields.
 */
void
CIndexedFileManager_Destructor(CIndexedFileManager *this)
{
	if (this->indexFile != NULL)
		fclose_ServerSide(this->indexFile);
	if (this->dataFile != NULL)
		fclose_ServerSide(this->dataFile);
}

/*
 * 0x00465544 - CIndexedFileManager::Open
 *
 * Opens the index and data files through ContainerHandle paged I/O.
 */
void
CIndexedFileManager_Open(CIndexedFileManager *this, char *indexPath, char *dataPath, char *mode)
{
	this->indexFile = fopen_ServerSide(indexPath, mode);
	this->dataFile = fopen_ServerSide(dataPath, mode);
	this->mode = mode;
}

/*
 * 0x00465585 - CIndexedFileManager::Close
 *
 * Flushes and closes both files and zeroes the struct fields.
 */
void
CIndexedFileManager_Close(CIndexedFileManager *this)
{
	if (this->indexFile != NULL) {
		fclose_ServerSide(this->indexFile);
	}
	if (this->dataFile != NULL) {
		fclose_ServerSide(this->dataFile);
	}
	this->dataFile = NULL;
	this->indexFile = NULL;
	this->mode = NULL;
}

/*
 * 0x004655DB - CIndexedFileManager::Repack
 *
 * Defragments a MUL index+data file pair when waste exceeds ~33%.
 * Copies only valid entries into temp files, then replaces the
 * originals.
 *
 * FIXED: Binary directory-extraction loop uses i++ (forward scan),
 * which never finds '/' and walks past the buffer on Linux. Changed
 * to i-- (backward scan).
 * FIXED: Binary uses sprintf into tmpPath with a dirBuf prefix of the
 * same size. Changed to snprintf to avoid overflow.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
void
CIndexedFileManager_Repack(CIndexedFileManager *this, char *indexPath, char *dataPath)
{
	char dirBuf[256];
	char tmpPath[256];
	int32_t *entries;
	int32_t *entryPtr;
	int32_t numEntries;
	int32_t totalDataSize;
	int32_t maxBlockSize;
	int32_t dataFileSize;
	int32_t blockLen;
	int32_t offset;
	int32_t extra;
	int32_t newOffset;
	int i;
	FILE *tmpIdxFile;
	FILE *tmpDataFile;
	uint8_t *copyBuf;
	int32_t tmp;

	strcpy(dirBuf, dataPath);
	i = (int)strlen(dataPath) - 1;
	for (; i > 0; i--) {
		if (dirBuf[i] == '/')
			break;
	}
	dirBuf[i + 1] = '\0';

	CIndexedFileManager_Open(this, indexPath, dataPath, "rb");

	snprintf(tmpPath, sizeof(tmpPath), "%stempidx.bla", dirBuf);
	remove(tmpPath);
	snprintf(tmpPath, sizeof(tmpPath), "%stempout.bla", dirBuf);
	remove(tmpPath);

	fseek_ServerSide(this->indexFile, 0, SEEK_END);
	numEntries = (int32_t)ftell_ServerSide(this->indexFile) / 12;

	fseek_ServerSide(this->indexFile, 0, SEEK_SET);
	entries = (int32_t *)OperatorNew(numEntries * 3 * sizeof(int32_t));
	fread_ServerSide(entries, 12, numEntries, this->indexFile);

	for (i = 0; i < numEntries * 3; i++) {
		SwapEndian(&entries[i]);
	}

	totalDataSize = 0;
	entryPtr = entries;
	maxBlockSize = 0x10000;
	for (i = 0; i < numEntries; i++) {
		if (*entryPtr != -1) {
			blockLen = entryPtr[1];
			if (blockLen > maxBlockSize)
				maxBlockSize = blockLen;
			totalDataSize += blockLen;
		}
		entryPtr += 3;
	}

	fseek_ServerSide(this->dataFile, 0, SEEK_END);
	dataFileSize = (int32_t)ftell_ServerSide(this->dataFile);

	if (totalDataSize * 150 / 100 >= dataFileSize) {
		CIndexedFileManager_Close(this);
		OperatorDelete(entries);
		return;
	}

	copyBuf = (uint8_t *)OperatorNew(maxBlockSize);

	snprintf(tmpPath, sizeof(tmpPath), "%stempidx.bla", dirBuf);
	tmpIdxFile = fopen_ServerSide(tmpPath, "wb");

	snprintf(tmpPath, sizeof(tmpPath), "%stempout.bla", dirBuf);
	tmpDataFile = fopen_ServerSide(tmpPath, "wb");

	newOffset = 0;
	entryPtr = entries;
	for (i = 0; i < numEntries; i++) {
		offset = *entryPtr;
		entryPtr++;
		blockLen = *entryPtr;
		entryPtr++;
		extra = *entryPtr;
		entryPtr++;

		if (offset == -1) {
			tmp = offset;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, tmpIdxFile);
			fwrite_ServerSide(&tmp, 4, 1, tmpIdxFile);
			tmp = extra;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, tmpIdxFile);
		} else {
			tmp = newOffset;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, tmpIdxFile);

			tmp = blockLen;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, tmpIdxFile);

			tmp = extra;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, tmpIdxFile);

			fseek_ServerSide(this->dataFile, offset, SEEK_SET);
			fread_ServerSide(copyBuf, blockLen, 1, this->dataFile);
			fwrite_ServerSide(copyBuf, blockLen, 1, tmpDataFile);

			newOffset += blockLen;
		}
	}

	OperatorDelete(entries);
	OperatorDelete(copyBuf);

	fclose_ServerSide(tmpIdxFile);
	fclose_ServerSide(tmpDataFile);

	CIndexedFileManager_Close(this);

	remove(indexPath);
	remove(dataPath);

	snprintf(tmpPath, sizeof(tmpPath), "%stempidx.bla", dirBuf);
	rename(tmpPath, indexPath);

	snprintf(tmpPath, sizeof(tmpPath), "%stempout.bla", dirBuf);
	rename(tmpPath, dataPath);
}
#pragma GCC diagnostic pop

/*
 * 0x00465C37 - CIndexedFileManager::WriteBlock
 *
 * Writes (or updates) the index entry and data for a block. In write
 * mode ('w') entries are appended sequentially; otherwise the data
 * overwrites the old slot when it fits, or is appended at end.
 * A dataLen < 1 writes offset=-1, length=-1.
 */
void
CIndexedFileManager_WriteBlock(CIndexedFileManager *this, int blockIdx, uint8_t *data, int dataLen, int extra)
{
	int32_t tmp;
	int32_t tmp2;

	if (this->mode[0] == 'w') {
		if (dataLen < 1) {
			tmp = -1;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, this->indexFile);
			fwrite_ServerSide(&tmp, 4, 1, this->indexFile);
			tmp = extra;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, this->indexFile);
			return;
		}

		tmp = (int32_t)ftell_ServerSide(this->dataFile);
		SwapEndian(&tmp);
		fwrite_ServerSide(&tmp, 4, 1, this->indexFile);

		tmp2 = dataLen;
		SwapEndian(&tmp2);
		fwrite_ServerSide(&tmp2, 4, 1, this->indexFile);

		tmp = extra;
		SwapEndian(&tmp);
		fwrite_ServerSide(&tmp, 4, 1, this->indexFile);

		fwrite_ServerSide(data, dataLen, 1, this->dataFile);
	} else {
		fseek_ServerSide(this->indexFile, blockIdx * 12, SEEK_SET);

		if (dataLen < 1) {
			tmp = -1;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, this->indexFile);
			fwrite_ServerSide(&tmp, 4, 1, this->indexFile);
			tmp = extra;
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, this->indexFile);
			return;
		}

		fread_ServerSide(&tmp, 4, 1, this->indexFile);
		SwapEndian(&tmp);
		fread_ServerSide(&tmp2, 4, 1, this->indexFile);
		SwapEndian(&tmp2);

		if (dataLen > tmp2) {
			fseek_ServerSide(this->dataFile, 0, SEEK_END);
			tmp = (int32_t)ftell_ServerSide(this->dataFile);
			fseek_ServerSide(this->indexFile, blockIdx * 12, SEEK_SET);
			SwapEndian(&tmp);
			fwrite_ServerSide(&tmp, 4, 1, this->indexFile);
		} else {
			fseek_ServerSide(this->dataFile, tmp, SEEK_SET);
			fseek_ServerSide(this->indexFile, blockIdx * 12 + 4, SEEK_SET);
		}

		tmp2 = dataLen;
		SwapEndian(&tmp2);
		fwrite_ServerSide(&tmp2, 4, 1, this->indexFile);

		tmp2 = extra;
		SwapEndian(&tmp2);
		fwrite_ServerSide(&tmp2, 4, 1, this->indexFile);

		fwrite_ServerSide(data, dataLen, 1, this->dataFile);
	}
}

/*
 * 0x00465F39 - CIndexedFileManager::ReadBlock
 *
 * Reads the 12-byte index entry and the matching data block.
 *
 * FIXED: Binary allocates exactly length bytes. On Linux the adjacent
 * heap memory is not NUL, so NUL-scanning parsers read past the buffer.
 * Allocates length+1 to guarantee NUL termination.
 */
void
CIndexedFileManager_ReadBlock(CIndexedFileManager *this, int blockIdx, uint8_t **outData, int *outLen, int *outExtra)
{
	int32_t offset = -1;
	int32_t length = 0;
	int32_t extra = 0;

	fseek_ServerSide(this->indexFile, blockIdx * 12, SEEK_SET);

	fread_ServerSide(&offset, 4, 1, this->indexFile);
	SwapEndian(&offset);
	fread_ServerSide(&length, 4, 1, this->indexFile);
	SwapEndian(&length);
	fread_ServerSide(&extra, 4, 1, this->indexFile);
	SwapEndian(&extra);

	*outExtra = extra;

	if (offset == -1 || length < 1) {
		*outData = NULL;
		*outLen = 0;
		return;
	}

	*outData = calloc(1, length + 1);
	*outLen = length;

	fseek_ServerSide(this->dataFile, offset, SEEK_SET);
	fread_ServerSide(*outData, length, 1, this->dataFile);
}

/*
 * 0x004C4020 - AddDeferredContainerLink
 *
 * Queues a master/follower link to be resolved once all blocks load.
 * No-op when parentSerial is zero.
 *
 * FIXED: Binary does not check the malloc return value; added a NULL
 * check to avoid dereferencing on allocation failure.
 */
void
AddDeferredContainerLink(CItem *child, uint32_t parentSerial, uint32_t skipFlag)
{
	DeferredContainerLink *node;

	if (parentSerial == 0)
		return;

	node = OperatorNew(sizeof(DeferredContainerLink));
	if (node == NULL)
		return;

	node->child = child;
	node->parentSerial = parentSerial;
	node->skipFlag = skipFlag;

	node->next = g_deferredList;
	g_deferredList = node;
}

/*
 * 0x004C406C - ProcessDeferredContainerLinks
 *
 * Resolves queued master/follower links once all blocks are loaded.
 */
void
ProcessDeferredContainerLinks(void)
{
	DeferredContainerLink *node;

	while (g_deferredList != NULL) {
		node = g_deferredList;

		g_deferredList = node->next;

		CItem *parent = CWorld_FindBySerial(g_World, node->parentSerial);

		if (parent != NULL) {
			if (node->skipFlag == 0) {
				CMobile_AddFollower((CMobile *)parent, (CMobile *)node->child);
			}
		}

		OperatorDelete(node);
	}
}

/*
 * 0x004C5716 - CMultiComponent::Save
 *
 * Serialises the component: optional "multitype=component" marker,
 * then multislave/multiflags/multioffset fields.
 */
void
CMultiComponent_Save(CMultiComponent *mc, CDataBuffer *buf, int isComponent)
{
	char tmp[0x4000];

	if (isComponent != 0) {
		sprintf(tmp, "multitype=component");
		CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);
	}

	sprintf(tmp, "multislave=%d", mc->serial);
	CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);

	sprintf(tmp, "multiflags=%d", (unsigned int)mc->flags);
	CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);

	sprintf(tmp, "multioffset=%d %d %d", (int)(int16_t)mc->offset.x, (int)(int16_t)mc->offset.y, (int)(int16_t)mc->offset.z);
	CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);
}

/*
 * 0x004C5844 - CMultiSlave::Save
 *
 * Serialises the slave: optional "multitype=slave" marker,
 * mtypeid/mcarry/mrange, the base component, and one
 * "multicomponent" line per component serial.
 */
void
CMultiSlave_Save(CMultiSlave *slave, CDataBuffer *buf, int isComponent)
{
	char tmp[0x4000];
	uintptr_t *iter;

	if (isComponent != 0) {
		sprintf(tmp, "multitype=slave");
		CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);
	}

	sprintf(tmp, "mtypeid=%d", CMultiSlave_GetTypeId(slave));
	CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);

	sprintf(tmp, "mcarry=%d", CMultiSlave_GetCarry(slave));
	CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);

	sprintf(tmp, "mrange=%d", CMultiSlave_GetRange(slave) & 0xFFFF);
	CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);

	CMultiComponent_Save(&slave->base, buf, 0);

	iter = (uintptr_t *)slave->components.begin;
	while (iter != (uintptr_t *)slave->components.end) {
		sprintf(tmp, "multicomponent=%d", (uint32_t)*iter);
		CDataBuffer_Append(buf, tmp, strlen(tmp) + 1);
		iter++;
	}
}

/*
 * 0x004C59EF - CItem::Save
 *
 * Serialises the common CItem/CEntity fields, tracking struct,
 * resource children, multi linkage, equipment position, and the
 * attached scripts and tag vars.
 */
void
CItem_Save(CItem *obj, CDataBuffer *b, int writeMarker)
{
	char buf[0x4000];
	int len;
	CEntity *ent;

	ent = &obj->resourceEntity.entity;

	if (writeMarker)
		CDataBuffer_Append(b, "@=D", 4);

	if (obj->serial != 0)
		CDataBuffer_WriteInt(b, "id", obj->serial);

	if ((CEntity_GetBodyType(obj) & 0xFFFF) != 0)
		CDataBuffer_WriteInt(b, "type", CEntity_GetBodyType(obj) & 0xFFFF);

	if (obj->itemFlags != 0)
		CDataBuffer_WriteInt(b, "stat", obj->itemFlags);

	if (ent->color != 0)
		CDataBuffer_WriteInt(b, "hue", ent->color);

	if (CItem_HasHome(obj) == 1) {
		CLocation homeLoc;
		CLocation_Init(&homeLoc);
		CItem_GetHomeLocation(obj, &homeLoc);
		len = snprintf(buf, sizeof(buf), "home=%d %d %d", (int)(int16_t)homeLoc.x, (int)(int16_t)homeLoc.y, (int)homeLoc.z);
		CDataBuffer_Append(b, buf, len + 1);
	}

	if (obj->tracking != NULL) {
		CItemTracking *tr = obj->tracking;

		if (tr->lastCont != 0) {
			len = snprintf(buf, sizeof(buf), "lastcont=%u", tr->lastCont);
			CDataBuffer_Append(b, buf, len + 1);
		}

		if (tr->lastMob != 0) {
			len = snprintf(buf, sizeof(buf), "lastmob=%u", tr->lastMob);
			CDataBuffer_Append(b, buf, len + 1);
		}

		if (tr->lastMobEqPos != 0x1A) {
			len = snprintf(buf, sizeof(buf), "lastmobeqpos=%d", tr->lastMobEqPos);
			CDataBuffer_Append(b, buf, len + 1);
		}

		if (!CLocation_IsInvalid(&tr->lastLoc)) {
			len = snprintf(buf, sizeof(buf), "lastloc=%d %d %d", (int)(int16_t)tr->lastLoc.x, (int)(int16_t)tr->lastLoc.y, (int)tr->lastLoc.z);
			CDataBuffer_Append(b, buf, len + 1);
		}

		if (!CLocation_IsInvalid(&tr->lastContLoc)) {
			len = snprintf(buf, sizeof(buf), "lastcontloc=%d %d %d", (int)(int16_t)tr->lastContLoc.x, (int)(int16_t)tr->lastContLoc.y, (int)tr->lastContLoc.z);
			CDataBuffer_Append(b, buf, len + 1);
		}
	}

	if ((CResourceEntity_GetTemplateIndex(obj) & 0xFFFF) != 0xFFFF)
		CDataBuffer_WriteInt(b, "template", CResourceEntity_GetTemplateIndex(obj) & 0xFFFF);

	if (obj->decayCount != 0)
		CDataBuffer_WriteInt(b, "decayCount", obj->decayCount);

	if (!CLocation_IsInvalid((CLocation *)&obj->resourceEntity.nextInContainer)) {
		CLocation *cloc = (CLocation *)&obj->resourceEntity.nextInContainer;
		len = snprintf(buf, sizeof(buf), "cloc=%d %d %d", (int)(int16_t)cloc->x, (int)(int16_t)cloc->y, (int)(int16_t)cloc->z);
		CDataBuffer_Append(b, buf, len + 1);
	}

	{
		CResourceNode *rn;

		for (rn = obj->resourceEntity.firstChild; rn != NULL; rn = rn->next) {
			if (rn->id == 0)
				continue;
			len = snprintf(buf, sizeof(buf), "r=%d %d %d %d %d", (int)rn->id, (int)(int8_t)rn->type, rn->value1, rn->value2, rn->value3);
			CDataBuffer_Append(b, buf, len + 1);
		}
	}

	if (CItem_HasMulti(obj)) {
		CMultiComponent *mc = CItem_GetMulti(obj);
		if (CMultiComponent_IsOwner(mc)) {
			CMultiSlave *ms = (CMultiSlave *)mc;

			len = snprintf(buf, sizeof(buf), "multitype=slave");
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "mtypeid=%d", CMultiSlave_GetTypeId(ms));
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "mcarry=%d", CMultiSlave_GetCarry(ms));
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "mrange=%d", (int)CMultiSlave_GetRange(ms));
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "multislave=%u", mc->serial);
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "multiflags=%d", (int)mc->flags);
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "multioffset=%d %d %d", (int)(int16_t)mc->offset.x, (int)(int16_t)mc->offset.y, (int)(int16_t)mc->offset.z);
			CDataBuffer_Append(b, buf, len + 1);

			{
				uintptr_t *cIter = (uintptr_t *)ms->components.begin;
				while (cIter != (uintptr_t *)ms->components.end) {
					len = snprintf(buf, sizeof(buf), "multicomponent=%u", (uint32_t)*cIter);
					CDataBuffer_Append(b, buf, len + 1);
					cIter++;
				}
			}
		} else {
			len = snprintf(buf, sizeof(buf), "multitype=component");
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "multislave=%u", mc->serial);
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "multiflags=%d", (int)mc->flags);
			CDataBuffer_Append(b, buf, len + 1);

			len = snprintf(buf, sizeof(buf), "multioffset=%d %d %d", (int)(int16_t)mc->offset.x, (int)(int16_t)mc->offset.y, (int)(int16_t)mc->offset.z);
			CDataBuffer_Append(b, buf, len + 1);
		}
	}

	len = snprintf(buf, sizeof(buf), "loc=%d %d %d", (int)(int16_t)ent->location.x, (int)(int16_t)ent->location.y, (int)ent->location.z);
	CDataBuffer_Append(b, buf, len + 1);

	if (obj->parent != NULL)
		CDataBuffer_WriteInt(b, "cont", obj->parent->serial);

	if (obj->parent != NULL && VT_IsMobile(obj->parent)) {
		CMobile *mob = (CMobile *)obj->parent;
		int i;
		for (i = 0; i < 30; i++) {
			if (mob->equipment[i] == obj) {
				CDataBuffer_WriteInt(b, "eqpos", i);
				break;
			}
		}
	}

	if (obj->tagList != NULL) {
		static const char *typeNames[] = { "int", "str", "ust", "loc", "obj", "lis", "voi", "unk" };

		if (CItem_HasScripts(obj)) {
			CVector scrVec;
			char typeFlag = 0;
			uintptr_t *scrIter;
			CVector_Constructor(&scrVec, &typeFlag);
			CItem_GetScriptListRaw(obj, &scrVec);
			scrIter = (uintptr_t *)scrVec.begin;
			while (scrIter != (uintptr_t *)scrVec.end) {
				ScriptAttachNode *sn = (ScriptAttachNode *)*scrIter;
				CScript *sc = (CScript *)sn->scriptClassPtr;

				len = snprintf(buf, sizeof(buf), "wom_scr=%s %d", sc->name, sc->namedScope.count);
				CDataBuffer_Append(b, buf, len);

				{
					CNamedScopeEntry *entries = (CNamedScopeEntry *)sc->namedScope.entries;
					int vi;
					for (vi = 0; vi < sc->namedScope.count; vi++) {
						const char *vname = entries[vi].name;
						int typeId = entries[vi].typeId;
						int offset = entries[vi].offset;

						len = snprintf(buf, sizeof(buf), " %3s %s ", typeNames[typeId], vname);
						CDataBuffer_Append(b, buf, len);

						switch (typeId) {
						case WTYPE_INT: {
							uintptr_t val;
							memcpy(&val, (char *)sn->memberScope + offset, sizeof(void *));
							len = snprintf(buf, sizeof(buf), "%d", (int)val);
							CDataBuffer_Append(b, buf, len);
							break;
						}
						case WTYPE_STRING: {
							CString *cs;
							memcpy(&cs, (char *)sn->memberScope + offset, sizeof(void *));
							len = snprintf(buf, sizeof(buf), "%s", ObjVar_EscapeStr(CString_GetData(cs)));
							CDataBuffer_Append(b, buf, len);
							break;
						}
						case WTYPE_USTRING: {
							CUString *cus;
							memcpy(&cus, (char *)sn->memberScope + offset, sizeof(void *));
							len = snprintf(buf, sizeof(buf), "%s", ObjVar_EscapeUStr((const uint16_t *)CUString_GetData(cus)));
							CDataBuffer_Append(b, buf, len);
							break;
						}
						case WTYPE_LOC: {
							int16_t *loc = (int16_t *)((char *)sn->memberScope + offset);
							len = snprintf(buf, sizeof(buf), "%d %d %d", (int)(int16_t)loc[0], (int)(int16_t)loc[1], (int)(int16_t)loc[2]);
							CDataBuffer_Append(b, buf, len);
							break;
						}
						case WTYPE_OBJ: {
							uintptr_t val;
							memcpy(&val, (char *)sn->memberScope + offset, sizeof(void *));
							len = snprintf(buf, sizeof(buf), "%u", (uint32_t)val);
							CDataBuffer_Append(b, buf, len);
							break;
						}
						case WTYPE_LIST: {
							CList *list;
							memcpy(&list, (char *)sn->memberScope + offset, sizeof(void *));
							if (ObjVar_ValidateListPtr(list)) {
								len = snprintf(buf, sizeof(buf), "%d", list->count);
								CDataBuffer_Append(b, buf, len);
								List_SerializeToBuf(b, list);
							} else {
								CDataBuffer_Append(b, "0", 1);
							}
							break;
						}
						default:
							break;
						}
					}
				}

				CDataBuffer_Append(b, "", 1);
				scrIter++;
			}
			CVector_Destructor(&scrVec);
		}

		if (CItem_HasTagDefs(obj)) {
			CVector tagVec;
			char typeFlag = 0;
			uintptr_t *tagIter;
			CVector_Constructor(&tagVec, &typeFlag);
			CItem_GetTagDefListRaw(obj, &tagVec);
			tagIter = (uintptr_t *)tagVec.begin;
			while (tagIter != (uintptr_t *)tagVec.end) {
				TagNode *node = (TagNode *)*tagIter;

				len = snprintf(buf, sizeof(buf), "wom_var=%3s %s ", typeNames[node->type], node->name);

				switch (node->type) {
				case WTYPE_INT:
					len += snprintf(buf + len, sizeof(buf) - len, "%d", (int)node->value);
					CDataBuffer_Append(b, buf, len + 1);
					break;
				case WTYPE_STRING: {
					CString *tagStr = (CString *)(uintptr_t)node->value;
					len += snprintf(buf + len, sizeof(buf) - len, "%s", ObjVar_EscapeStr(CString_GetData(tagStr)));
					CDataBuffer_Append(b, buf, len + 1);
					break;
				}
				case WTYPE_USTRING: {
					CUString *tagUStr = (CUString *)(uintptr_t)node->value;
					len += snprintf(buf + len, sizeof(buf) - len, "%s", ObjVar_EscapeUStr((const uint16_t *)CUString_GetData(tagUStr)));
					CDataBuffer_Append(b, buf, len + 1);
					break;
				}
				case WTYPE_LOC: {
					CLocation *loc = (CLocation *)(uintptr_t)node->value;
					len += snprintf(buf + len, sizeof(buf) - len, "%d %d %d", (int)(int16_t)loc->x, (int)(int16_t)loc->y, (int)(int16_t)loc->z);
					CDataBuffer_Append(b, buf, len + 1);
					break;
				}
				case WTYPE_OBJ:
					len += snprintf(buf + len, sizeof(buf) - len, "%u", (unsigned)node->value);
					CDataBuffer_Append(b, buf, len + 1);
					break;
				case WTYPE_LIST: {
					CList *list = (CList *)(uintptr_t)node->value;
					if (ObjVar_ValidateListPtr(list)) {
						len += snprintf(buf + len, sizeof(buf) - len, "%d", list->count);
						CDataBuffer_Append(b, buf, len);
						List_SerializeToBuf(b, list);
						CDataBuffer_Append(b, "", 1);
					} else {
						CDataBuffer_Append(b, "0", 1);
					}
					break;
				}
				default:
					break;
				}
				tagIter++;
			}
			CVector_Destructor(&tagVec);
		}
	}

	{
		char *ret = CEntity_SaveTimers((CItem *)obj, buf, 0x2000);
		if (ret != NULL)
			CDataBuffer_Append(b, ret, strlen(ret) + 1);
	}

	CDataBuffer_Append(b, "", 1);
}

/*
 * 0x004C6A1D - CContainer::Save
 *
 * Saves common CItem fields, then recursively saves children.
 */
void
CContainer_Save(CContainer *cont, CDataBuffer *b, int writeMarker)
{
	CItem *child;

	if (writeMarker)
		CDataBuffer_Append(b, "@=C", 4);

	CItem_Save(&cont->item, b, 0);

	child = cont->contents;
	while (child != NULL) {
		((void (*)(CItem *, CDataBuffer *, int))VT_FN(child, VT_SAVE))(child, b, 1);
		child = child->spatialNext;
	}
}

/*
 * 0x004C6A7F - CMulti::Save (type @=X, corpse container)
 *
 * Serializes a CCorpse: writes the "@=X" marker, corpse body type,
 * each occupied equipment slot, then the base container payload.
 */
void
CMulti_Save(CCorpse *corpse, CDataBuffer *b, int writeMarker)
{
	char buf[128];
	int len, i;

	if (writeMarker)
		CDataBuffer_Append(b, "@=X", 4);

	len = snprintf(buf, sizeof(buf), "corpsetype=%u", corpse->corpseBodyType);
	CDataBuffer_Append(b, buf, len + 1);

	for (i = 0; i < 26; i++) {
		if (corpse->equipSlots[i] != 0) {
			len = snprintf(buf, sizeof(buf), "corpitem=%d %u", i, corpse->equipSlots[i]);
			CDataBuffer_Append(b, buf, len + 1);
		}
	}

	CContainer_Save(&corpse->container, b, 0);
}

/*
 * 0x004C6B76 - CBoard::Save (type @=B)
 *
 * Serializes a bulletin board: writes the "@=B" marker, then the base
 * container payload (posted messages live in cont->contents).
 */
void
CBoard_Save(CContainer *cont, CDataBuffer *b, int writeMarker)
{
	if (writeMarker)
		CDataBuffer_Append(b, "@=B", 4);
	CContainer_Save(cont, b, 0);
}

/*
 * 0x004C6BA6 - CSignpost::Save (type @=Z)
 *
 * Writes the signpost's map-extents bounding box and vect entries,
 * then CItem::Save.
 */
void
CSignpost_Save(CSignpost *sp, CDataBuffer *b, int writeMarker)
{
	char buf[128];
	int len;
	VectNode *v;

	if (writeMarker)
		CDataBuffer_Append(b, "@=Z", 4);

	len = snprintf(buf, sizeof(buf), "mapextents=%d %d %d %d %d %d", (int)(uint16_t)sp->mapExtent[0], (int)(uint16_t)sp->mapExtent[1], (int)(uint16_t)sp->mapExtent[2],
	        (int)(uint16_t)sp->mapExtent[3], (int)(uint16_t)sp->mapExtent[4], (int)(uint16_t)sp->mapExtent[5]);
	CDataBuffer_Append(b, buf, len + 1);

	for (v = sp->vectHead; v != NULL; v = v->next) {
		len = snprintf(buf, sizeof(buf), "vect=%d %d", (int)v->x, (int)v->y);
		CDataBuffer_Append(b, buf, len + 1);
	}

	CItem_Save(&sp->item, b, 0);
}

/*
 * 0x004C6CC9 - CWeapon::Save (type @=W)
 *
 * Writes weapon-specific fields, then CItem::Save (no child iteration).
 */
void
CWeapon_Save(CContainer *cont, CDataBuffer *b, int writeMarker)
{
	if (writeMarker)
		CDataBuffer_Append(b, "@=W", 4);

	if (cont->weaponClass != 0)
		CDataBuffer_WriteInt(b, "wtemp", cont->weaponClass);

	if (!CDiceRoll_IsZero(&cont->weaponDamage)) {
		CString diceStr = { NULL, 0, 1, 0 };
		char wcbuf[48];
		int len;
		CDiceRoll_ToString(&cont->weaponDamage, &diceStr);
		len = snprintf(wcbuf, sizeof(wcbuf), "wcstr=%s", CString_GetBuffer(&diceStr));
		CDataBuffer_Append(b, wcbuf, len + 1);
		CString_Destructor(&diceStr);
	}

	if (cont->weaponHitPoints != 0)
		CDataBuffer_WriteInt(b, "mac", cont->weaponHitPoints);

	if (cont->weaponCurrentHP != 0)
		CDataBuffer_WriteInt(b, "chp", cont->weaponCurrentHP);

	if (cont->weaponMaxHP != 0)
		CDataBuffer_WriteInt(b, "mhp", cont->weaponMaxHP);

	CItem_Save(&cont->item, b, 0);
}

/*
 * 0x004C6F3A - SerialList_Save
 *
 * Writes "name=serial flags" for each node in the serial list.
 */
void
SerialList_Save(CSerialList *list, char *buf, char *name, CDataBuffer *b)
{
	CSerialNode *node;
	CSerialNode *sentinel;

	sentinel = list->data;
	for (node = sentinel->next; node != sentinel; node = node->next) {
		sprintf(buf, "%s=%u %d", name, node->serial, (int)node->flags);
		CDataBuffer_Append(b, buf, strlen(buf) + 1);
	}
}

/*
 * 0x004C6FD1 - CMobile::Save (type @=M)
 *
 * Writes all mobile fields, then CContainer::Save, then iterates
 * equipment[0..29] through each item's virtual Save.
 */
void
CMobile_Save(CMobile *mob, CDataBuffer *b, int writeMarker)
{
	char buf[256];
	int len, i;

	if (writeMarker)
		CDataBuffer_Append(b, "@=M", 4);

	for (i = 0; i < 50; i++) {
		if (mob->skills[i] != 0) {
			len = snprintf(buf, sizeof(buf), "skill=%d %d", i, mob->skills[i]);
			CDataBuffer_Append(b, buf, len + 1);
		}
		if (mob->skillBonuses[i] != 0) {
			len = snprintf(buf, sizeof(buf), "skillmod=%d %d", i, mob->skillBonuses[i]);
			CDataBuffer_Append(b, buf, len + 1);
		}
	}

	if (mob->skillWeightBudget != 0)
		CDataBuffer_WriteInt(b, "skillslush", mob->skillWeightBudget);

	if (mob->mobileFlags != 0)
		CDataBuffer_WriteInt(b, "state", mob->mobileFlags);

	if (mob->baseStr != 0)
		CDataBuffer_WriteInt(b, "str", mob->baseStr);
	if (mob->baseDex != 0)
		CDataBuffer_WriteInt(b, "dex", mob->baseDex);
	if (mob->baseInt != 0)
		CDataBuffer_WriteInt(b, "int", mob->baseInt);

	if (mob->strBonus != 0)
		CDataBuffer_WriteInt(b, "strmod", mob->strBonus);
	if (mob->dexBonus != 0)
		CDataBuffer_WriteInt(b, "dexmod", mob->dexBonus);
	if (mob->intBonus != 0)
		CDataBuffer_WriteInt(b, "intmod", mob->intBonus);

	if (mob->maxHp != 0)
		CDataBuffer_WriteInt(b, "hp", mob->maxHp);
	if (mob->hp != mob->maxHp)
		CDataBuffer_WriteInt(b, "curhp", mob->hp);
	if (mob->maxMana != 0)
		CDataBuffer_WriteInt(b, "mana", mob->maxMana);
	if (mob->mana != mob->maxMana)
		CDataBuffer_WriteInt(b, "curmana", mob->mana);
	if (mob->maxStamina != 0)
		CDataBuffer_WriteInt(b, "fat", mob->maxStamina);
	if (mob->stamina != mob->maxStamina)
		CDataBuffer_WriteInt(b, "curfat", mob->stamina);

	len = snprintf(buf, sizeof(buf), "clocks=%d %d %d %d", mob->staminaRegenTimer, mob->staminaLossCounter, mob->manaRegenTimer, mob->hpRegenTimer);
	CDataBuffer_Append(b, buf, len + 1);

	if (mob->notoriety != 0)
		CDataBuffer_WriteInt(b, "not", mob->notoriety);
	if (mob->fame != 0)
		CDataBuffer_WriteInt(b, "fame", mob->fame);
	if (mob->karma != 0)
		CDataBuffer_WriteInt(b, "karma", mob->karma);

	if (mob->hunger != 0)
		CDataBuffer_WriteInt(b, "hung", mob->hunger);
	if (mob->attackMode != 0)
		CDataBuffer_WriteInt(b, "att", mob->attackMode);
	if (mob->speechHue != 0x3B2)
		CDataBuffer_WriteInt(b, "deftexthue", mob->speechHue);

	if (mob->direction != 0)
		CDataBuffer_WriteInt(b, "dir", mob->direction);
	if (mob->statClock != 0)
		CDataBuffer_WriteInt(b, "stclk", mob->statClock);
	if (mob->stomach != 0)
		CDataBuffer_WriteInt(b, "stom", mob->stomach);

	if (mob->isFollower != 0) {
		CMobile *owner = mob->owner;
		CDataBuffer_WriteInt(b, "master", owner->container.item.serial);
	}

	if (mob->sex != 0)
		CDataBuffer_WriteInt(b, "sex", mob->sex);
	CDataBuffer_WriteField(b, "name", mob->name != NULL ? mob->name : "");

	if (mob->lightTime != 0)
		CDataBuffer_WriteInt(b, "lt", mob->lightTime);
	if (mob->lightVal != 0)
		CDataBuffer_WriteInt(b, "lv", mob->lightVal);

	if (mob->lifeclock != 0)
		CDataBuffer_WriteInt(b, "lifeclock", mob->lifeclock);

	if (mob->sfxDie != 0xFFFF)
		CDataBuffer_WriteInt(b, "sfxdie", mob->sfxDie);
	if (mob->sfxNotice != 0xFFFF)
		CDataBuffer_WriteInt(b, "sfxnotice", mob->sfxNotice);
	if (mob->sfxIdle != 0xFFFF)
		CDataBuffer_WriteInt(b, "sfxidle", mob->sfxIdle);
	if (mob->sfxHit != 0xFFFF)
		CDataBuffer_WriteInt(b, "sfxhit", mob->sfxHit);
	if (mob->sfxWasHit != 0xFFFF)
		CDataBuffer_WriteInt(b, "sfxwashit", mob->sfxWasHit);

	if (mob->movementType != 1)
		CDataBuffer_WriteInt(b, "movetype", mob->movementType);
	if (mob->savedRidingSerial != 0)
		CDataBuffer_WriteInt(b, "savedriding", mob->savedRidingSerial);

	if (VT_IsDead((CItem *)mob)) {
		if (CMobile_CheckStatusFlag(mob, 2))
			CMobile_SetStatusFlag(mob, 2, 0);
	}

	if (mob->statusFlags != 0)
		CDataBuffer_WriteInt(b, "mobflags", mob->statusFlags);

	SerialList_Save(&mob->combatTargetList, buf, "attacking", b);
	SerialList_Save(&mob->attackerList, buf, "attackedby", b);

	CContainer_Save(&mob->container, b, 0);

	for (i = 0; i < 30; i++) {
		if (mob->equipment[i] != NULL)
			((void (*)(CItem *, CDataBuffer *, int))VT_FN(mob->equipment[i], VT_SAVE))(mob->equipment[i], b, 1);
	}
}

/*
 * 0x004C7D7E - CNPC::Save (type @=N)
 *
 * Writes NPC-specific fields, then CMobile::Save.
 */
void
CNPC_Save(CNPC *npc, CDataBuffer *b, int writeMarker)
{
	char buf[256];
	int len;

	if (writeMarker)
		CDataBuffer_Append(b, "@=N", 4);

	if (npc->behaviorFlags != 0)
		CDataBuffer_WriteInt(b, "flags", npc->behaviorFlags & ~0x8000);

	if (npc->convoFragList != NULL) {
		StdPtrNode *fragIter, *fragBeginTemp, *fragEndTemp;
		StdPtrNode *fragEndCopy, *fragPostIncTemp;

		StdPtrIter_CopyConstructor(&fragIter, StdPtrList_Begin((StdPtrList *)npc->convoFragList, &fragBeginTemp));
		for (;;) {
			StdPtrIter_CopyConstructor(&fragEndCopy, StdPtrList_End((StdPtrList *)npc->convoFragList, &fragEndTemp));
			if (!(StdPtrIter_Neq(&fragIter, &fragEndCopy) & 0xFF))
				break;
			if (CString_GetCStr((CString *)StdPtrIter_Deref(&fragIter)) != NULL && strlen(CString_GetCStr((CString *)StdPtrIter_Deref(&fragIter))) < 0x78)
				CDataBuffer_WriteField(b, "frag", CString_GetCStr((CString *)StdPtrIter_Deref(&fragIter)));
			StdPtrIter_PostInc(&fragIter, &fragPostIncTemp, 0);
		}
	}

	if (strcmp(npc->npcJob, "no job") != 0)
		CDataBuffer_WriteField(b, "job", npc->npcJob);

	if (strcmp(npc->npcTown, "no town") != 0)
		CDataBuffer_WriteField(b, "town", npc->npcTown);

	if (npc->npcFlee != 0x10)
		CDataBuffer_WriteInt(b, "npcflee", npc->npcFlee);

	len = snprintf(buf, sizeof(buf), "desireLoc=%d %d %d", npc->desireLoc.x, npc->desireLoc.y, npc->desireLoc.z);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "lastDesireLoc=%d %d %d", npc->lastDesireLoc.x, npc->lastDesireLoc.y, npc->lastDesireLoc.z);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "homeLoc=%d %d %d", npc->homeLoc.x, npc->homeLoc.y, npc->homeLoc.z);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "followObjs=%d %d %d", npc->followObj1, npc->followObj2, npc->followObj3);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "npcInfo1=%d %d %d %d", npc->npcInfo1_0, npc->npcInfo1_1, npc->tickCount, npc->npcInfo1_3);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "homeInfo=%d %d %d", npc->homeInfo1, npc->homeInfo2, npc->homeInfo3);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "npcInfo2=%d %d", npc->speechCounter, npc->npcCombatTarget);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "loiterInfo=%d %d %d %d", npc->loiterLoc.x, npc->loiterLoc.y, npc->loiterLoc.z, npc->loiterData);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "stateInfo=%d %d %d %d", npc->npcInfo1_0, npc->aiState, npc->ltype, npc->stateInfo2);
	CDataBuffer_Append(b, buf, len + 1);

	CMobile_Save(&npc->mobile, b, 0);
}

/*
 * 0x004C832B - CGuard::Save (type @=G)
 *
 * Serializes a guard: writes the "@=G" marker, then the base NPC payload.
 */
void
CGuard_Save(CNPC *npc, CDataBuffer *b, int writeMarker)
{
	if (writeMarker)
		CDataBuffer_Append(b, "@=G", 4);
	CNPC_Save(npc, b, 0);
}

/*
 * 0x004C835B - CShopkeeper::Save (type @=S)
 *
 * Writes restockCounter, then CNPC::Save.
 */
void
CShopkeeper_Save(CNPC *npc, CDataBuffer *b, int writeMarker)
{
	if (writeMarker)
		CDataBuffer_Append(b, "@=S", 4);
	CDataBuffer_WriteInt(b, "restockCounter", npc->restockCounter);
	CNPC_Save(npc, b, 0);
}

/*
 * 0x004C83D0 - CEgg::Save (type @=E)
 *
 * Eggs are CItem-sized, so this forwards to CItem::Save (not
 * CContainer::Save). The block-index lookup before the marker check
 * is dead code in the binary and is reproduced verbatim.
 */
void
CEgg_Save(CItem *item, CDataBuffer *b, int writeMarker)
{
	int blockIdx;
	MapBlock *blockPtr;

	blockIdx = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &item->resourceEntity.entity.location, 0);
	blockPtr = &g_MapBlocks[blockIdx];
	USED(blockPtr);

	if (writeMarker)
		CDataBuffer_Append(b, "@=E", 4);
	CItem_Save(item, b, 0);
}

/*
 * 0x004C842C - CPlayer::Save (type @=P)
 *
 * Writes player-specific fields, then CMobile::Save.
 */
void
CPlayer_Save(CPlayer *player, CDataBuffer *b, int writeMarker)
{
	CMobile *mob = &player->mobile;
	uint16_t entityBodyType;
	uint32_t maskedFlags;
	char buf[256];
	int len, i;

	if (writeMarker)
		CDataBuffer_Append(b, "@=P", 4);

	len = snprintf(buf, sizeof(buf), "cct=%d", player->creationTime);
	CDataBuffer_Append(b, buf, len + 1);

	if (player->password[0] != '\0')
		CDataBuffer_WriteField(b, "pswd", player->password);

	for (i = 0; i < 8; i++) {
		if (player->targetHistory[i] == 0)
			break;
		len = snprintf(buf, sizeof(buf), "sid=%u", player->targetHistory[i]);
		CDataBuffer_Append(b, buf, len + 1);
	}

	entityBodyType = mob->container.item.resourceEntity.entity.bodyType;
	if ((entityBodyType & 0xFFFF) != (player->bodyType & 0xFFFF)) {
		len = snprintf(buf, sizeof(buf), "ltype=%d", player->bodyType);
		CDataBuffer_Append(b, buf, len + 1);
	}

	if (player->deathCount != 0)
		CDataBuffer_WriteInt(b, "dc", player->deathCount);

	if (mob->combatByte2 != 0) {
		len = snprintf(buf, sizeof(buf), "fs=%d", (int)(int8_t)mob->combatByte2);
		CDataBuffer_Append(b, buf, len + 1);
	}

	if (mob->combatByte3 != 0) {
		len = snprintf(buf, sizeof(buf), "fa=%d", (int)(int8_t)mob->combatByte3);
		CDataBuffer_Append(b, buf, len + 1);
	}

	if (mob->combatByte4 != 0) {
		len = snprintf(buf, sizeof(buf), "fw=%d", (int)(int8_t)mob->combatByte4);
		CDataBuffer_Append(b, buf, len + 1);
	}

	maskedFlags = player->pflags & 0x2D47A;
	if (maskedFlags != 0) {
		len = snprintf(buf, sizeof(buf), "pflags=%d", maskedFlags);
		CDataBuffer_Append(b, buf, len + 1);
	}

	if (player->friendCount != 0) {
		for (i = 0; (uint32_t)i < player->friendCount; i++) {
			if (player->friendList[i] != 0) {
				len = snprintf(buf, sizeof(buf), "frnd=%d", player->friendList[i]);
				CDataBuffer_Append(b, buf, len + 1);
			}
		}
	}

	if (player->friendAllowCount != 0) {
		for (i = 0; (uint32_t)i < player->friendAllowCount; i++) {
			if (player->friendAllowList[i] != 0) {
				len = snprintf(buf, sizeof(buf), "frndallw=%d", player->friendAllowList[i]);
				CDataBuffer_Append(b, buf, len + 1);
			}
		}
	}

	len = snprintf(buf, sizeof(buf), "lastvalidloc=%d %d %d", (int)(int16_t)player->lastValidLocation.x, (int)(int16_t)player->lastValidLocation.y,
	        (int)(int16_t)player->lastValidLocation.z);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "acct=%d %d", player->accountNum, player->characterNum);
	CDataBuffer_Append(b, buf, len + 1);

	len = snprintf(buf, sizeof(buf), "playage=%u", player->playAge);
	CDataBuffer_Append(b, buf, len + 1);

	for (i = 0; i < 50; i++) {
		len = snprintf(buf, sizeof(buf), "tsk=%d %d", i, mob->skillTimers[i]);
		CDataBuffer_Append(b, buf, len + 1);
	}

	CMobile_Save(mob, b, 0);
}

/*
 * 0x004C8A5C - SaveDynamic0
 *
 * Synchronous world save: faults the archived (logged-out) entities
 * into the grid, serializes every map block, and then removes them
 * again. The binary writes dynidx0.mul / dynamic0.mul in place; we
 * delegate the write to SaveDynamic0_WriteCommit, which stages to temp
 * files and atomically renames them in so an interrupted save can never
 * leave a torn file. The binary leaves this unwired ("dead code - never
 * called"); our periodic cadence and the shutdown save call it.
 *
 * MODIFIED: atomic temp+rename commit replaces the binary's in-place write.
 */
void
SaveDynamic0(void)
{
	EntityManager_AddAllToWorld();
	SaveDynamic0_WriteCommit();
	EntityManager_RemoveAllFromWorld();
}

/*
 * 0x004C8BB7 - ReadToken
 *
 * Copies src into dst until NUL or '=', NUL-terminates dst, and
 * returns src positioned past any consumed '='.
 */
static char *
ReadToken(char *src, char *dst)
{
	int i = 0;

	while (*src != '\0' && *src != '=') {
		*dst++ = *src++;
		i++;
	}
	*dst = '\0';
	if (*src == '=')
		src++;
	USED(i);
	return src;
}

/*
 * 0x004C8C1F - FindContainerBySerial
 *
 * Recursively searches a container hierarchy for an entity with the
 * given serial, descending through mobile equipment and the contents
 * chain.
 */
static CItem *
FindContainerBySerial(CItem *container, uint32_t serial)
{
	int i;
	CItem *child;
	CItem *result;

	if (container->serial == serial)
		return container;

	if (VT_IsMobile(container)) {
		for (i = 0; i < 0x1E; i++) {
			CMobile *mob = (CMobile *)container;
			if (mob->equipment[i] == NULL)
				continue;
			if (!VT_IsMobile2(mob->equipment[i]))
				continue;
			result = FindContainerBySerial(mob->equipment[i], serial);
			if (result != NULL)
				return result;
		}
	}

	child = ((CContainer *)container)->contents;
	while (child != NULL) {
		if (VT_IsMobile2(child)) {
			result = FindContainerBySerial(child, serial);
			if (result != NULL)
				return result;
		}
		child = child->spatialNext;
	}

	return NULL;
}

/*
 * 0x004C8D14 - FindParentBySerial
 *
 * Returns the entity for serial only if it is a container.
 */
static CItem *
FindParentBySerial(uint32_t serial)
{
	CItem *entity;

	entity = CWorld_FindBySerial(g_World, serial);
	if (entity == NULL)
		return NULL;
	if (!VT_IsMobile2(entity))
		return NULL;
	return entity;
}

/*
 * 0x004C8D4C - FindParentInBlock
 *
 * Searches the block's container chain and then every player for an
 * entity with the given serial.
 */
static CItem *
FindParentInBlock(CItem *blockItemHead, uint32_t serial)
{
	CItem *item;
	CItem *result;
	CPlayer *p;

	item = blockItemHead;
	while (item != NULL) {
		if (VT_IsMobile2(item)) {
			result = FindContainerBySerial(item, serial);
			if (result != NULL)
				return result;
		}
		item = item->spatialNext;
	}

	p = g_PlayerList.head;
	while (p != NULL) {
		result = FindContainerBySerial((CItem *)p, serial);
		if (result != NULL)
			return result;
		p = p->next;
	}

	return NULL;
}

/*
 * 0x004C8DD7 - LoadDynamic0
 *
 * Opens dynidx0.mul/dynamic0.mul, parses each map block's object
 * data, resolves deferred links, and drains the player list.
 */
void
LoadDynamic0(void)
{
	CIndexedFileManager indexedFile;
	int blockIdx;

	g_World->isLoading = 1;
	g_deferredList = NULL;

	CIndexedFileManager_Constructor(&indexedFile);
	CIndexedFileManager_Open(&indexedFile, GLOBAL_file_dynidx0_mul, GLOBAL_file_dynamic0_mul, "rb");

	for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
		uint8_t *data;
		int dataLen;
		int extra;

		data = NULL;
		CIndexedFileManager_ReadBlock(&indexedFile, blockIdx, &data, &dataLen, &extra);

		LoadDynamic0_ParseBlock(blockIdx, (char *)data, dataLen, NULL, NULL);

		if (data != NULL)
			OperatorDelete(data);
	}

	CIndexedFileManager_Close(&indexedFile);

	ProcessDeferredContainerLinks();

	g_World->isLoading = 0;

	while (g_PlayerList.head != NULL)
		BroadcastDestroyAndRemove(&g_PlayerList.head->mobile.container.item);

	CIndexedFileManager_Destructor(&indexedFile);
}

/*
 * 0x004C8EEB - Dynamic_LoadEntity
 *
 * Sets an ObjVar, with type-specific wrapping: list frees the old
 * CList before replacing; string and ustring box the raw pointer
 * through a temporary CString/CUString; other types pass through.
 */
void
Dynamic_LoadEntity(CItem *entity, const char *name, int type, uintptr_t value)
{
	if (type == 5) {
		struct TagNode *tn = CEntity_SetObjVar(entity, name, 5, 0);
		CList *oldList = (CList *)(uintptr_t)tn->value;
		if (oldList != NULL)
			CList_ScalarDelete(oldList, 1);
		tn->value = value;
	} else if (type == 1) {
		CString localStr;
		CString_Constructor(&localStr, (const char *)(uintptr_t)value);
		CEntity_SetObjVar(entity, name, 1, (uintptr_t)&localStr);
		CString_Destructor(&localStr);
	} else if (type == 2) {
		CUString localUStr;
		CUString_Constructor(&localUStr, (const void *)(uintptr_t)value);
		CEntity_SetObjVar(entity, name, 2, (uintptr_t)&localUStr);
		CUString_Destructor(&localUStr);
	} else {
		CEntity_SetObjVar(entity, name, type, value);
	}
}

/*
 * 0x004C8FFA - Dynamic_ParseDeferredSerial
 *
 * Parses "serial flags" and appends a CSerialValue to list. The
 * entity argument is unused in the binary.
 */
static __attribute__((unused)) void
Dynamic_ParseDeferredSerial(CItem *entity, CSerialList *list, const char *str)
{
	uint32_t serial = 0;
	int flags = 0;

	USED(entity);
	sscanf(str, "%u %d", &serial, &flags);
	CSerialList_InsertBack(list, serial, (int16_t)flags);
}

/*
 * 0x004C9095 - Dynamic_InitDeferredSerials
 *
 * Constructs the deferred-serial vector with uint32_t element type.
 */
static __attribute__((unused)) void
Dynamic_InitDeferredSerials(void)
{
	char vecType = 0;
	CVector_Constructor(&g_deferredLoadedSerials, &vecType);
}

/*
 * 0x004C90AB - Dynamic_atexitDeferredSerials
 *
 * Registers the dtor thunk for g_deferredLoadedSerials via atexit.
 */
static __attribute__((unused)) void
Dynamic_atexitDeferredSerials(void)
{
	atexit(Dynamic_atexitDeferredSerials_thunk);
}

/*
 * 0x004C90BD - atexit thunk for g_deferredLoadedSerials
 *
 * Destroys g_deferredLoadedSerials at program exit.
 */
static void
Dynamic_atexitDeferredSerials_thunk(void)
{
	CVector_Destructor(&g_deferredLoadedSerials);
}

/*
 * 0x004C90CC - Dynamic_FireObjectLoadedEvents
 *
 * Fires event 0x36 (objectloaded) on every deferred-load entity,
 * temporarily clearing g_World->isLoading, then empties the vector.
 */
void
Dynamic_FireObjectLoadedEvents(void)
{
	uintptr_t *iter;
	int savedLoading;

	iter = (uintptr_t *)g_deferredLoadedSerials.begin;

	while (iter != (uintptr_t *)g_deferredLoadedSerials.end) {
		uint32_t serial = (uint32_t)*iter;

		CItem *ent = CWorld_FindBySerial(g_World, serial);

		savedLoading = g_World->isLoading;
		g_World->isLoading = 0;

		if (ent != NULL) {
			Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x36);
		}

		g_World->isLoading = savedLoading;

		iter++;
	}

	CVector_Erase(&g_deferredLoadedSerials, g_deferredLoadedSerials.begin, g_deferredLoadedSerials.end);
}

/*
 * When g_pendingLoad is set, newly inserted entities have their
 * objectloaded (event 0x36) dispatch collected in g_deferredLoadedSerials
 * and fired in a later batch via Dynamic_ClearPendingAndFireEvents.
 */

int g_pendingLoad; // 0x006EF6A8

/*
 * 0x004C9161 - SetPendingLoad
 *
 * Marks the dynamic load as in-progress so object-load callbacks
 * queue up instead of firing immediately.
 */
void
Dynamic_SetPendingLoad(void)
{
	g_pendingLoad = 1;
}

/*
 * 0x004C9170 - ClearPendingAndFireEvents
 *
 * Clears g_pendingLoad, then fires all deferred objectloaded events.
 */
void
Dynamic_ClearPendingAndFireEvents(void)
{
	g_pendingLoad = 0;
	Dynamic_FireObjectLoadedEvents();
}

/*
 * 0x004C9184 - LoadDynamic0_ParseBlock
 *
 * Parses a block's NUL-delimited object stream. Each object begins
 * with an @=X type marker followed by key=value fields. recursive*
 * args are always NULL from LoadDynamic0.
 *
 * FIXED: After deleting a duplicate egg, the binary leaves obj
 * dangling so a later VT_IsPlayer(obj) reads its freed vtable.
 * Sets obj = NULL after the VT_DELETE call.
 *
 * MODIFIED: the skillmod case bounds the index against the array where
 * the binary calls CMobile::SetSkillBonus, which rejects any id the
 * skill manager does not know. Ids 46 to 49 are stored here and refused
 * by the binary.
 */
static void
LoadDynamic0_ParseBlock(int blockIdx, char *data, int dataLen, CItem *recursiveParent, CLocation *recursiveLoc)
{
	char *p;
	char keybuf[0x4000];
	CItem *obj;
	uint32_t containerSerial;
	int eqpos;
	int blockX = -1, blockY = -1;
	int blockZ = -1;
	CLocation homeLoc;
	int bookStatus;
	int weaponTemplate;
	int quantity;

	CVector loadedSerials;
	static const char vecType = 0;

	int isRecursive = 0;

	CVector_Constructor(&loadedSerials, &vecType);

	if (recursiveParent != NULL || recursiveLoc != NULL)
		isRecursive = 1;
	USED(isRecursive);

	if (recursiveParent != NULL) {
		data += 4;
		blockX = (int16_t)(*(int16_t *)data & 0x3FFF);
		blockY = (int16_t)(*(int16_t *)(data + 2));
		blockZ = (int)(*(int8_t *)(data + 4));
		blockIdx = CBlockManager_GetBlockIndex(&g_SpatialGrid, blockX, blockY, 0);
		data += 5;
		dataLen -= 9;
	}

	if (dataLen < 1) {
		CVector_Destructor(&loadedSerials);
		return;
	}

	p = data;

	p = ReadToken(p, keybuf);

	for (;;) {
		obj = NULL;
		containerSerial = 0;
		eqpos = -1;
		int inHash = 1;

		if (strcmp(keybuf, "@") == 0) {
			char typeChar = *p;
			if (typeChar >= 'A' && typeChar <= 'Z')
				typeChar += 0x20;

			switch (typeChar) {
			case 'b': {
				CBulletinBoard *bb = calloc(1, sizeof(CBulletinBoard));
				if (bb != NULL)
					CBulletinBoard_Constructor(bb);
				obj = (CItem *)bb;
				break;
			}
			case 'c':
				obj = calloc(1, sizeof(CContainer));
				if (obj != NULL)
					CContainer_Constructor((CContainer *)obj);
				break;
			case 'e':
				obj = CEgg_Constructor(calloc(1, sizeof(CItem)));
				break;
			case 'g': {
				CNPC *npc = calloc(1, sizeof(CNPC));
				if (npc != NULL)
					CGuard_Constructor(npc);
				obj = (CItem *)npc;
				break;
			}
			case 'm': {
				CMobile *mob = calloc(1, sizeof(CMobile));
				if (mob != NULL)
					CMobile_Constructor(mob);
				obj = (CItem *)mob;
				break;
			}
			case 'n': {
				CNPC *npc = calloc(1, sizeof(CNPC));
				if (npc != NULL)
					CResourceMobile_Init(&npc->mobile);
				obj = (CItem *)npc;
				break;
			}
			case 'p': {
				CPlayer *player = calloc(1, sizeof(CPlayer));
				if (player != NULL)
					CPlayer_Constructor(player);
				obj = (CItem *)player;
				break;
			}
			case 's': {
				CNPC *npc = calloc(1, sizeof(CNPC));
				if (npc != NULL)
					CShopkeeper_ConstructorNoArgs(npc);
				obj = (CItem *)npc;
				break;
			}
			case 'w':
				obj = calloc(1, sizeof(CContainer));
				if (obj != NULL)
					CWeapon_ConstructorFromItem(obj);
				break;
			case 'x': {
				CCorpse *corpse = calloc(1, sizeof(CCorpse));
				if (corpse != NULL)
					CCorpse_Constructor(corpse);
				obj = (CItem *)corpse;
				break;
			}
			case 'z': {
				CSignpost *sp = calloc(1, sizeof(CSignpost));
				if (sp != NULL)
					CSignpost_Constructor(sp);
				obj = (CItem *)sp;
				break;
			}
			default:
				break;
			}

			while (*p != '\0')
				p++;
			p++;

			p = ReadToken(p, keybuf);
		} else if (strcmp(keybuf, "end") == 0) {
			break;
		} else {
			break;
		}

		if (obj == NULL)
			obj = CItem_Constructor(calloc(1, sizeof(CItem)));

		CItem_SetLockdown(obj, 1);

		CLocation_Init(&homeLoc);
		CLocation_Invalidate(&homeLoc);
		bookStatus = -1;
		weaponTemplate = -1;
		if (VT_IsWeapon(obj))
			weaponTemplate = 0;
		quantity = -1;

		while (keybuf[0] != '\0') {
			char *val = p;

			if (strcmp(keybuf, "id") == 0) {
				uint32_t parsed = (uint32_t)atoi(val);

				if (parsed != 0) {
					if (VT_IsMobile(obj))
						parsed &= 0xBFFFFFFF;
				}

				if (VT_IsPlayer(obj)) {
					World_PreloadPlayerSerial(parsed);
					inHash = 0;
				} else {
					if (World_FindBySerial_Either(parsed)) {
						parsed = 0;
						inHash = 1;
					} else {
						inHash = 0;
					}
				}

				((void (*)(void *, uint32_t))VT_FN(obj, VT_SET_SERIAL))(obj, parsed);

				if (g_World != NULL) {
					uint32_t ns = (obj->serial | 0x40000000);
					if (ns >= g_World->nextSerial)
						g_World->nextSerial = ns + 1;
				}
			} else if (strcmp(keybuf, "type") == 0) {
				CEntity_SetBodyType(obj, (uint16_t)atoi(val));
			} else if (strcmp(keybuf, "hue") == 0) {
				obj->resourceEntity.entity.color = (uint16_t)atoi(val);
			} else if (strcmp(keybuf, "loc") == 0) {
				int x, y, z;
				if (sscanf(val, "%d %d %d", &x, &y, &z) == 3) {
					obj->resourceEntity.entity.location.x = (uint16_t)x;
					obj->resourceEntity.entity.location.y = (uint16_t)y;
					obj->resourceEntity.entity.location.z = (int16_t)z;
				}
			} else if (strcmp(keybuf, "cloc") == 0) {
				int x, y, z;
				if (sscanf(val, "%d %d %d", &x, &y, &z) == 3) {
					CLocation *cloc = (CLocation *)&obj->resourceEntity.nextInContainer;
					cloc->x = (int16_t)x;
					cloc->y = (int16_t)y;
					cloc->z = (int16_t)z;
				}
			} else if (strcmp(keybuf, "cont") == 0) {
				containerSerial = (uint32_t)atoi(val);
			} else if (strcmp(keybuf, "eqpos") == 0) {
				eqpos = atoi(val);
			} else if (strcmp(keybuf, "stat") == 0) {
				obj->itemFlags = (uint8_t)atoi(val);
			} else if (strcmp(keybuf, "flags") == 0) {

				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					npc->behaviorFlags = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "quan") == 0) {
				quantity = atoi(val);
			} else if (strcmp(keybuf, "name") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					CMobile_SetName(mob, val);
				}
			} else if (strcmp(keybuf, "dir") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->direction = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "str") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->baseStr = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "dex") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->baseDex = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "int") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->baseInt = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "curhp") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->hp = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "hp") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->maxHp = (uint32_t)atoi(val);
					mob->hp = mob->maxHp;
				}
			} else if (strcmp(keybuf, "curmana") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->mana = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "mana") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->maxMana = (uint32_t)atoi(val);
					mob->mana = mob->maxMana;
				}
			} else if (strcmp(keybuf, "curfat") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->stamina = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "fat") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->maxStamina = (uint32_t)atoi(val);
					mob->stamina = mob->maxStamina;
				}
			} else if (strcmp(keybuf, "fame") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->fame = (int16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "karma") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->karma = (int16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "not") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->notoriety = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "att") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->attackMode = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "sex") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->sex = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "hung") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->hunger = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "stom") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->stomach = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "stclk") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->statClock = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "lifeclock") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->lifeclock = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "movetype") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					int moveVal = atoi(val);
					CMobile_SetMovementType(mob, (uint8_t)moveVal);
					if (moveVal == 0)
						CMobile_SetStatusFlag(mob, 2, 1);
				}
			} else if (strcmp(keybuf, "lt") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->lightTime = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "lv") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->lightVal = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "strmod") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->strBonus = (int16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "dexmod") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->dexBonus = (int16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "intmod") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->intBonus = (int16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "clocks") == 0) {

				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					int a, b, c, d;
					if (sscanf(val, "%d %d %d %d", &a, &b, &c, &d) == 4) {
						mob->staminaRegenTimer = (uint32_t)a;
						mob->staminaLossCounter = (int32_t)b;
						mob->manaRegenTimer = (int32_t)c;
						mob->hpRegenTimer = (int32_t)d;
					}
				}
			} else if (strcmp(keybuf, "sfxhit") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->sfxHit = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "sfxwashit") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->sfxWasHit = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "sfxidle") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->sfxIdle = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "sfxnotice") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->sfxNotice = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "sfxdie") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->sfxDie = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "savedriding") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->savedRidingSerial = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "mobflags") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					CMobile_SetStatusFlagsByte(mob, (uint8_t)atoi(val));
				}
			} else if (strcmp(keybuf, "master") == 0) {
				if (VT_IsMobile(obj)) {
					uint32_t masterSerial = (uint32_t)atoi(val);
					AddDeferredContainerLink(obj, masterSerial, 0);
				}
			} else if (strcmp(keybuf, "mac") == 0) {
				if (VT_IsWeapon(obj))
					CWeapon_SetMaxAC(obj, (uint8_t)atoi(val));
			} else if (strcmp(keybuf, "mhp") == 0) {
				if (VT_IsWeapon(obj))
					CWeapon_SetMaxHP(obj, (uint8_t)atoi(val));
			} else if (strcmp(keybuf, "cct") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					player->creationTime = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "pflags") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					player->pflags = (uint32_t)atoi(val);
					if (player->pflags & PlayerIsGameMaster)
						CResManager_InsertByRef(&g_GMPlayerList, (uintptr_t)player);
				}
			} else if (strcmp(keybuf, "pswd") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					strncpy(player->password, val, 31);
					player->password[31] = '\0';
				}
			} else if (strcmp(keybuf, "playage") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					player->playAge = (uint32_t)atoi(val);
				}

			} else if (strcmp(keybuf, "acct") == 0) {

				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					int a, b;
					if (sscanf(val, "%d %d", &a, &b) == 2) {
						player->accountNum = (uint32_t)a;
						player->characterNum = (uint32_t)b;
					}
				}
			} else if (strcmp(keybuf, "lastvalidloc") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					int x, y, z;
					if (sscanf(val, "%d %d %d", &x, &y, &z) == 3) {
						player->lastValidLocation.x = (uint16_t)x;
						player->lastValidLocation.y = (uint16_t)y;
						player->lastValidLocation.z = (int16_t)z;
					}
				}
			} else if (strcmp(keybuf, "tsk") == 0) {
				if (VT_IsPlayer(obj)) {
					CMobile *mob = (CMobile *)obj;
					int idx;
					uint32_t timer;
					if (sscanf(val, "%d %u", &idx, &timer) == 2) {
						if (timer == 0)
							timer = 0x33FA0481;
						mob->skillTimers[idx] = timer;
					}
				}
			} else if (strcmp(keybuf, "skill") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					int idx, value;
					if (sscanf(val, "%d %d", &idx, &value) == 2)
						CMobile_SetSkill(mob, (int8_t)idx, (uint16_t)value);
				}
			} else if (strcmp(keybuf, "skillmod") == 0) {

				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					int idx, value;
					if (sscanf(val, "%d %d", &idx, &value) == 2) {
						if (idx >= 0 && idx < 50)
							mob->skillBonuses[idx] = (int16_t)value;
					}
				}
			} else if (strcmp(keybuf, "r") == 0) {
				int rv1, rv2, rv3, rv4, rv5;
				if (sscanf(val, "%d %d %d %d %d", &rv1, &rv2, &rv3, &rv4, &rv5) == 5) {
					CResourceEntity_AddNode(obj, rv1, rv2, rv3, rv4, rv5, 1);
				}
			} else if (strcmp(keybuf, "job") == 0) {
				{
					CNPC *npc = (CNPC *)obj;
					npc->npcJob = (char *)CScriptManager_InternString(&g_ScriptManager, val);
				}
			} else if (strcmp(keybuf, "town") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					npc->npcTown = (char *)CScriptManager_InternString(&g_ScriptManager, val);
				}
			} else if (strcmp(keybuf, "npcflee") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					npc->npcFlee = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "ltype") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					player->bodyType = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "stateInfo") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int a, b, c, d;
					if (sscanf(val, "%d %d %d %d", &a, &b, &c, &d) == 4) {
						npc->npcInfo1_0 = (uint32_t)a;
						npc->aiState = (uint32_t)b;
						npc->ltype = (uint32_t)c;
						npc->stateInfo2 = (uint32_t)d;
					}
				}
			} else if (strcmp(keybuf, "npcInfo1") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int a, b, c, d;
					if (sscanf(val, "%d %d %d %d", &a, &b, &c, &d) == 4) {
						npc->npcInfo1_0 = (uint32_t)a;
						npc->npcInfo1_1 = (uint32_t)b;
						npc->tickCount = (uint32_t)c;
						npc->npcInfo1_3 = (uint32_t)d;
					}
				}
			} else if (strcmp(keybuf, "npcInfo2") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int a, b;
					if (sscanf(val, "%d %d", &a, &b) == 2) {
						npc->speechCounter = (uint8_t)a;
						npc->npcCombatTarget = (uint32_t)b;
					}
				}
			} else if (strcmp(keybuf, "followObjs") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int a, b, c;
					if (sscanf(val, "%d %d %d", &a, &b, &c) == 3) {
						npc->followObj1 = (uint32_t)a;
						npc->followObj2 = (uint32_t)b;
						npc->followObj3 = (uint32_t)c;
					}
				}
			} else if (strcmp(keybuf, "desireLoc") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int x, y, z;
					if (sscanf(val, "%d %d %d", &x, &y, &z) == 3) {
						npc->desireLoc.x = (uint16_t)x;
						npc->desireLoc.y = (uint16_t)y;
						npc->desireLoc.z = (int16_t)z;
					}
				}
			} else if (strcmp(keybuf, "lastDesireLoc") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int x, y, z;
					if (sscanf(val, "%d %d %d", &x, &y, &z) == 3) {
						npc->lastDesireLoc.x = (uint16_t)x;
						npc->lastDesireLoc.y = (uint16_t)y;
						npc->lastDesireLoc.z = (int16_t)z;
					}
				}
			} else if (strcmp(keybuf, "loiterInfo") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int x, y, z, d;
					if (sscanf(val, "%d %d %d %d", &x, &y, &z, &d) == 4) {
						npc->loiterLoc.x = (uint16_t)x;
						npc->loiterLoc.y = (uint16_t)y;
						npc->loiterLoc.z = (int16_t)z;
						npc->loiterData = (uint32_t)d;
					}
				}
			} else if (strcmp(keybuf, "homeLoc") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int x, y, z;
					if (sscanf(val, "%d %d %d", &x, &y, &z) == 3) {
						npc->homeLoc.x = (uint16_t)x;
						npc->homeLoc.y = (uint16_t)y;
						npc->homeLoc.z = (int16_t)z;
					}
				}
			} else if (strcmp(keybuf, "homeInfo") == 0) {
				if (VT_IsNPC(obj)) {
					CNPC *npc = (CNPC *)obj;
					int a, b, c;
					if (sscanf(val, "%d %d %d", &a, &b, &c) == 3) {
						npc->homeInfo1 = (uint8_t)a;
						npc->homeInfo2 = (uint8_t)b;
						npc->homeInfo3 = (uint32_t)c;
					}
				}
			} else if (strcmp(keybuf, "template") == 0) {
				if (VT_IsContainer(obj))
					CItem_AttachTemplate(obj, (uint16_t)atoi(val));
			} else if (strcmp(keybuf, "decayCount") == 0) {
				{
					int dc = atoi(val);
					if (dc < 0)
						dc = 0xFF;
					obj->decayCount = (uint8_t)dc;
				}
			} else if (strcmp(keybuf, "frame") == 0) {
				if (((int (*)(void *))VT_FN(obj, VT_IS_DOOR))(obj))
					CItem_SetOpen(obj, atoi(val));
			} else if (strcmp(keybuf, "deftexthue") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->speechHue = (uint16_t)atoi(val);
				}
			} else if (strcmp(keybuf, "restockCounter") == 0) {
				if (VT_IsVendor(obj)) {
					CNPC *npc = (CNPC *)obj;
					npc->restockCounter = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "chp") == 0) {
				if (VT_IsWeapon(obj))
					CWeapon_SetCurHP(obj, (uint8_t)atoi(val));
			} else if (strcmp(keybuf, "qual") == 0) {
				bookStatus = atoi(val);
			} else if (strcmp(keybuf, "owner") == 0) {
				CItem_SetMsgOwner(obj, (uint32_t)atoi(val));
			} else if (strcmp(keybuf, "sid") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					int i;
					uint32_t sid = (uint32_t)atoi(val);
					for (i = 0; i < 8; i++) {
						if (player->targetHistory[i] == 0) {
							player->targetHistory[i] = sid;
							break;
						}
					}
				}
			} else if (strcmp(keybuf, "dc") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					player->deathCount = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "fs") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->combatByte2 = (uint8_t)atoi(val);
				}
			} else if (strcmp(keybuf, "fw") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->combatByte4 = (uint8_t)atoi(val);
				}

			} else if (strcmp(keybuf, "state") == 0) {
				uint8_t stateVal = (uint8_t)atoi(val);
				((void (*)(CItem *, int))VT_FN(obj, VT_APPLY_STATUS_FLAGS))(obj, stateVal);
			} else if (strcmp(keybuf, "frag") == 0) {
				if (VT_IsNPC(obj)) {
					CString fragStr;
					CString_Constructor(&fragStr, val);
					CNPC_AddFragment((CNPC *)obj, &fragStr);
					CString_Destructor(&fragStr);
				}

			} else if (strcmp(keybuf, "res") == 0) {
				{
					int v[9];
					if (sscanf(val, "%d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) {
						CResourceEntity_AddNode(obj, v[8], v[7], v[0], v[1], v[2], 1);
					}
				}
			} else if (strcmp(keybuf, "newres") == 0) {
				{
					int rv1, rv2, rv3, rv4, rv5, rv6, rv7;
					if (sscanf(val, "%d %d %d %d %d %d %d", &rv1, &rv2, &rv3, &rv4, &rv5, &rv6, &rv7) >= 5) {
						CResourceEntity_AddNode(obj, rv1, rv2, rv3, rv4, rv5, 1);
					}
				}
			} else if (strcmp(keybuf, "nres2") == 0) {
				{
					int rv1, rv2, rv3, rv4, rv5, rv6, rv7, rv8;
					if (sscanf(val, "%d %d %d %d %d %d %d %d", &rv1, &rv2, &rv3, &rv4, &rv5, &rv6, &rv7, &rv8) >= 5) {
						CResourceEntity_AddNode(obj, rv1, rv2, rv3, rv4, rv5, 1);
					}
				}

			} else if (strcmp(keybuf, "wtemp") == 0) {
				if (VT_IsWeapon(obj)) {
					int templateId = 0xFF;
					sscanf(val, "%d", &templateId);
					if (!CWeapon_SetWeaponDef(obj, (uint8_t)templateId))
						inHash = 1;
				}
			} else if (strcmp(keybuf, "wcstr") == 0) {
				if (VT_IsWeapon(obj)) {
					char diceStr[256];
					CWeaponDice dice;
					if (sscanf(val, "%s", diceStr) == 1)
						CWeapon_SetDamageDice(obj, CDiceRoll_InitParse(&dice, diceStr));
				}
			} else if (strcmp(keybuf, "wom_var") == 0) {
				if (obj->serial != 0)
					ObjVar_LoadFromLine(obj->serial, val);

			} else if (strcmp(keybuf, "script") == 0) {

			} else if (strcmp(keybuf, "wom_scr") == 0) {
				if (obj->serial != 0)
					WomScr_LoadFromLine(obj->serial, val);

			} else if (strcmp(keybuf, "shopscr") == 0) {
				{
					CString valStr;
					CString_Constructor(&valStr, val);
					CItem_SetShopScript(obj, &valStr);
					CString_Destructor(&valStr);
				}
			} else if (strcmp(keybuf, "callback") == 0) {
				if (obj->serial != 0)
					CEntity_LoadTimers((CItem *)obj, val);
			} else if (strcmp(keybuf, "corpitem") == 0) {
				if (((int (*)(void *))VT_FN(obj, VT_HAS_CORPSE_EQ))(obj)) {
					CCorpse *corpse = (CCorpse *)obj;
					int slot;
					uint32_t serial;
					if (sscanf(val, "%d %u", &slot, &serial) == 2) {
						if (slot >= 0 && slot < 26)
							corpse->equipSlots[slot] = serial;
					}
				}
			} else if (strcmp(keybuf, "corpsetype") == 0) {
				if (((int (*)(void *))VT_FN(obj, VT_HAS_CORPSE_EQ))(obj))
					CCorpse_SetCorpseBodyType((CCorpse *)obj, (uint16_t)atoi(val));
			} else if (strcmp(keybuf, "vect") == 0) {
				if (VT_IsSpatial(obj)) {
					int x, y;
					if (sscanf(val, "%d %d", &x, &y) == 2) {
						PlotOnMap((CSignpost *)obj, 1, 0, (uint16_t)x, (uint16_t)y);
					}
				}
			} else if (strcmp(keybuf, "weapontemplate") == 0) {
				if (VT_IsWeapon(obj)) {
					weaponTemplate = 0xFF;
					sscanf(val, "%d", &weaponTemplate);
				}
			} else if (strcmp(keybuf, "skillslush") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->skillWeightBudget = (uint32_t)atoi(val);
				}
			} else if (strcmp(keybuf, "home") == 0) {
				{
					int x, y, z;
					if (sscanf(val, "%d %d %d", &x, &y, &z) == 3) {
						homeLoc.x = (uint16_t)x;
						homeLoc.y = (uint16_t)y;
						homeLoc.z = (int16_t)z;
					}
				}
			} else if (strcmp(keybuf, "lastloc") == 0) {
				{
					int x, y, z;
					if (sscanf(val, "%d %d %d", &x, &y, &z) == 3)
						CItem_SetLastLocation(obj, (int16_t)x, (int16_t)y, (int16_t)z);
				}
			} else if (strcmp(keybuf, "lastcontloc") == 0) {
				{
					int x, y, z;
					if (sscanf(val, "%d %d %d", &x, &y, &z) == 3)
						CItem_SetLastContainerLocation(obj, (int16_t)x, (int16_t)y, (int16_t)z);
				}
			} else if (strcmp(keybuf, "lastmob") == 0) {
				CItem_SetLastMobile(obj, (uint32_t)atoi(val));
			} else if (strcmp(keybuf, "lastmobeqpos") == 0) {
				CItem_SetLastMobileEquipPos(obj, (uint16_t)atoi(val));
			} else if (strcmp(keybuf, "lastcont") == 0) {
				CItem_SetLastContainer(obj, (uint32_t)atoi(val));
			} else if (strcmp(keybuf, "mapextents") == 0) {
				if (VT_IsSpatial(obj)) {
					CSignpost *sp = (CSignpost *)obj;
					int v[6];
					if (sscanf(val, "%d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
						sp->mapExtent[0] = (int16_t)v[0];
						sp->mapExtent[1] = (int16_t)v[1];
						sp->mapExtent[2] = (int16_t)v[2];
						sp->mapExtent[3] = (int16_t)v[3];
						sp->mapExtent[4] = (int16_t)v[4];
						sp->mapExtent[5] = (int16_t)v[5];
					}
				}
			} else if (strcmp(keybuf, "multitype") == 0) {
				if (strcmp(val, "slave") == 0)
					CItem_SetMultiSlave(obj);
				else if (strcmp(val, "component") == 0)
					CItem_SetMultiComponent(obj);
			} else if (strcmp(keybuf, "slave") == 0) {
				CItem_SetMultiSlave(obj);
			} else if (strcmp(keybuf, "component") == 0) {
				CItem_SetMultiComponent(obj);
			} else if (strcmp(keybuf, "multiflags") == 0) {
				if (CItem_HasMulti(obj)) {
					CMultiComponent *mc = CItem_GetMulti(obj);
					CMultiComponent_SetFlags(mc, (uint8_t)atoi(val));
				}
			} else if (strcmp(keybuf, "multislave") == 0) {
				if (CItem_HasMulti(obj)) {
					CMultiComponent *mc = CItem_GetMulti(obj);
					CMultiComponent_SetSerial(mc, (uint32_t)atoi(val));
				}
			} else if (strcmp(keybuf, "multioffset") == 0) {
				if (CItem_HasMulti(obj)) {
					CMultiComponent *mc = CItem_GetMulti(obj);
					int ox, oy, oz;
					if (sscanf(val, "%d %d %d", &ox, &oy, &oz) == 3) {
						CLocation loc;
						loc.x = (uint16_t)(int16_t)ox;
						loc.y = (uint16_t)(int16_t)oy;
						loc.z = (uint16_t)(int16_t)oz;
						CMultiComponent_SetOffset(mc, &loc);
					}
				}
			} else if (strcmp(keybuf, "multicomponent") == 0) {
				if (CItem_IsMultiOwner(obj)) {
					CMultiSlave *ms = CItem_GetMultiSlave(obj);
					CMultiSlave_AddComponent(ms, (uint32_t)atoi(val));
				}
			} else if (strcmp(keybuf, "mtypeid") == 0) {
				if (CItem_IsMultiOwner(obj)) {
					CMultiSlave *ms = CItem_GetMultiSlave(obj);
					CMultiSlave_SetTypeId(ms, atoi(val));
				}
			} else if (strcmp(keybuf, "mcarry") == 0) {
				if (CItem_IsMultiOwner(obj)) {
					CMultiSlave *ms = CItem_GetMultiSlave(obj);
					CMultiSlave_SetCarry(ms, (uint32_t)atoi(val));
				}
			} else if (strcmp(keybuf, "mrange") == 0) {
				if (CItem_IsMultiOwner(obj)) {
					CMultiSlave *ms = CItem_GetMultiSlave(obj);
					CMultiSlave_SetRange(ms, (uint16_t)atoi(val));
				}
			} else if (strcmp(keybuf, "frnd") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					uint32_t serial = (uint32_t)atoi(val);
					uint32_t newCount = player->friendCount + 1;
					uint32_t *newList = realloc(player->friendList, newCount * sizeof(uint32_t));
					if (newList != NULL) {
						newList[newCount - 1] = serial;
						player->friendList = newList;
						player->friendCount = newCount;
					}
				}
			} else if (strcmp(keybuf, "frndallw") == 0) {
				if (VT_IsPlayer(obj)) {
					CPlayer *player = (CPlayer *)obj;
					uint32_t serial = (uint32_t)atoi(val);
					uint32_t newCount = player->friendAllowCount + 1;
					uint32_t *newList = realloc(player->friendAllowList, newCount * sizeof(uint32_t));
					if (newList != NULL) {
						newList[newCount - 1] = serial;
						player->friendAllowList = newList;
						player->friendAllowCount = newCount;
					}
				}
			} else if (strcmp(keybuf, "attacking") == 0 || strcmp(keybuf, "attackedby") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					int isAttacking = (strcmp(keybuf, "attacking") == 0);
					uint32_t targetSerial = 0;
					int flags = 0;
					sscanf(val, "%u %d", &targetSerial, &flags);
					if (targetSerial != 0) {
						if (isAttacking)
							CSerialList_InsertBack(&mob->combatTargetList, targetSerial, (int16_t)flags);
						else
							CSerialList_InsertBack(&mob->attackerList, targetSerial, (int16_t)flags);
					}
				}
			} else if (strcmp(keybuf, "fa") == 0) {
				if (VT_IsMobile(obj)) {
					CMobile *mob = (CMobile *)obj;
					mob->combatByte3 = (uint8_t)atoi(val);
				}
			}

			while (*p != '\0')
				p++;
			p++;

			p = ReadToken(p, keybuf);
		}

		if (CMobile_GetSerial((CMobile *)obj) == 0)
			inHash = 1;

		if (!CLocation_IsInvalid(&homeLoc)) {
			if ((CEntity_GetBodyType(obj) & 0xFFFF) != 0xEB0)
				CItem_SetHome(obj, &homeLoc);
		}

		if (bookStatus >= 0) {
			if (CItem_IsWritableBook(obj))
				CItem_SetBookStatus(obj, (uint8_t)bookStatus);
		}

		if (VT_IsWeapon(obj)) {
			if (weaponTemplate >= 0) {
				if (!CItem_LoadWeaponDef(obj, weaponTemplate))
					inHash = 1;
			} else {
				if (!CWeaponManager_WeaponDefExists(&g_WeaponManager, CItem_GetWeaponDefId(obj)))
					inHash = 1;
			}
		}

		if (quantity >= 0) {
			if (CItem_IsWritableBook(obj))
				CItem_SetBookPages(obj, (uint16_t)quantity);
			else if (CItem_IsRunebook(obj))
				CItem_SetBookNum(obj, (uint16_t)quantity);
			else
				CItem_GetTiledataQuantity(obj);
		}

		CItem_CheckOverloadedWeight(obj);

		if (VT_IsMobile(obj))
			CMobile_SetupMasksFromObjVars((CMobile *)obj);

		if (inHash) {
			if (obj->serial == 0) {
				g_DeleteAllowed = 1;
				g_WorldActive2 = 0;
				if (obj != NULL)
					((void (*)(void *))VT_FN(obj, VT_DELETE))(obj);
				g_WorldActive2 = 1;
			} else {
				g_DeleteAllowed = 1;
				if (obj != NULL)
					((void (*)(void *))VT_FN(obj, VT_DELETE))(obj);
				g_DeleteAllowed = 0;
			}
			obj = NULL;
		}

		if (obj == NULL)
			goto done_with_object;

		if (containerSerial != 0) {
			CItem *parent = NULL;

			if (recursiveLoc == NULL && recursiveParent == NULL) {
				parent = FindParentInBlock(g_MapBlocks[blockIdx].itemHead, containerSerial);
			} else {
				parent = FindParentBySerial(containerSerial);
			}

			if (parent == NULL)
				parent = (CItem *)CPlayerList_FindBySerial(containerSerial);

			if (parent != NULL) {
				if (CMobile_GetSerial((CMobile *)parent) == CMobile_GetSerial((CMobile *)obj))
					parent = NULL;
			}

			if (parent == NULL) {
				if (obj != NULL)
					CItem_SetLockdown(obj, 0);
				if (obj != NULL)
					((void (*)(void *))VT_FN(obj, VT_DELETE))(obj);
				obj = NULL;
			} else if (eqpos != -1) {
				// 0x004CD281: VT_IsMobile (result unused)
				VT_IsMobile(parent);
				int equipped = ((int (*)(void *, void *, int))VT_FN(obj, VT_EQUIP_ON_MOBILE))(obj, parent, (uint8_t)eqpos);
				if (!equipped) {
					// 0x004cd2b5: dead store of parent
					USED(parent);
					if (obj != NULL)
						CItem_SetLockdown(obj, 0);
					if (obj != NULL)
						((void (*)(void *))VT_FN(obj, VT_DELETE))(obj);
					obj = NULL;
				}
			} else {
				((void (*)(void *, void *, void *))VT_FN(obj, VT_ADD_TO_CONTAINER))(obj, parent, &obj->resourceEntity.entity.location);
			}

			// 0x004CD30A: CEntity_GetBodyType (result unused)
			if (obj != NULL)
				CEntity_GetBodyType(obj);
		} else {

			if (recursiveLoc != NULL)
				CEntity_SetLocationByDelta(obj, recursiveLoc);

			// Path unused: recursiveParent always NULL from caller.
			if (recursiveParent != NULL) {
				CLocation_Set(&obj->resourceEntity.entity.location, (int16_t)blockX, (int16_t)blockY, (int16_t)blockZ);
			}

			if (!CBlockManager_IsValidCoord(&g_SpatialGrid, obj->resourceEntity.entity.location.x, obj->resourceEntity.entity.location.y)) {
				if (obj != NULL)
					CItem_SetLockdown(obj, 0);
				if (obj != NULL)
					((void (*)(void *))VT_FN(obj, VT_DELETE))(obj);
				obj = NULL;
			} else {
				// 0x004CD3A4: CEntity_GetBodyType (unused)
				if (obj != NULL)
					CEntity_GetBodyType(obj);

				if (CItem_HasMulti(obj)) {
					CMultiComponent_SetValid(CItem_GetMulti(obj), 0);
					((void (*)(void *, void *))VT_FN(obj, VT_DROP_AT_FEET))(obj, &obj->resourceEntity.entity.location);
					CMultiComponent_SetValid(CItem_GetMulti(obj), 1);
				} else {
					((void (*)(void *, void *))VT_FN(obj, VT_DROP_AT_FEET))(obj, &obj->resourceEntity.entity.location);
				}
			}
		}

		if (obj != NULL) {
			int isEgg = ((int (*)(void *))VT_FN(obj, VT_ITEM_CHECK_9C))(obj);
			if (isEgg) {
				int bi2 = CBlockManager_GetBlockIndexFromLoc(&g_SpatialGrid, &obj->resourceEntity.entity.location, 0);
				CBlock *blk = &g_SpatialGrid.cells[bi2];
				if (blk->chunkEgg == NULL) {
					blk->chunkEgg = obj;
				} else {
					g_ProcessingEggs = 1;
					if (obj != NULL)
						((void (*)(void *))VT_FN(obj, VT_DELETE))(obj);
					g_ProcessingEggs = 0;
					// FIXED: binary does not set
					// obj = NULL here (0x004CD479),
					// causing use-after-free.
					obj = NULL;
				}
			}
		}

		if (obj != NULL) {
			if (!ValidateInWorld(obj))
				obj = NULL;
		}

		// Path unused: recursiveParent/recursiveLoc always NULL.
		if (obj != NULL && (recursiveParent != NULL || recursiveLoc != NULL)) {
			uint32_t savedSerial = CMobile_GetSerial((CMobile *)obj);
			int savedLoading = g_World->isLoading;
			g_World->isLoading = 0;
			Entity_ExecuteEvent(&obj->resourceEntity.entity, 0x32);
			g_World->isLoading = savedLoading;

			if (CWorld_FindBySerial(g_World, savedSerial) != obj)
				obj = NULL;

			if (obj != NULL && VT_IsNPC(obj) && CItem_HasTagDefs(obj)) {
				if (CResourceEntity_HasTag(obj, "wasFollowing", 4)) {
					uint32_t wasSerial;
					CResourceEntity_GetTagObj(obj, "wasFollowing", &wasSerial);
					if (CWorld_FindBySerial(g_World, wasSerial) != NULL) {
						CResourceEntity_DetachScript(obj, "wasFollowing");
						Script_followNpc(obj->serial, wasSerial, 0);
						CNPC_Heartbeat((CNPC *)obj);
					}
				}
			}
		}

		if (obj != NULL)
			CItem_SetLockdown(obj, 0);

		if (obj != NULL && CItem_HasScripts(obj)) {
			if (recursiveParent == NULL && recursiveLoc == NULL) {
				if (g_pendingLoad) {
					Dynamic_AddDeferredLoadedSerial(CMobile_GetSerial((CMobile *)obj));
				} else {
					uint32_t ser = CMobile_GetSerial((CMobile *)obj);
					CVector_PushBack(&loadedSerials, ser);
				}
			}
		}

		if (obj != NULL) {
			if (obj->resourceEntity.entity.removedFromWorld) {
				if (!VT_IsPlayer(obj)) {
					if (obj != NULL) {
						((void (*)(void *))VT_FN(obj, VT_DELETE))(obj);
					}
					obj = NULL;
				}
			}
		}

		if (obj != NULL) {
			if (VT_IsPlayer(obj)) {
				int savedLoading2 = g_World->isLoading;
				g_World->isLoading = 0;
				SendStatusToPlayer((CMobile *)obj, (CPlayer *)obj, obj->serial, 1);
				g_World->isLoading = savedLoading2;
				if (CPlayer_IsGameMaster((CPlayer *)obj)) {
					CPlayer_AddToGMCallQueue((CPlayer *)obj);
				}
			}
		}

done_with_object:
		while (*p != '\0')
			p++;
		p++;
		p = ReadToken(p, keybuf);
	}

	if (!g_pendingLoad) {
		uintptr_t *iter;
		int savedLoading;
		for (iter = (uintptr_t *)loadedSerials.begin; iter != (uintptr_t *)loadedSerials.end; iter++) {
			CItem *ent = CWorld_FindBySerial(g_World, (uint32_t)*iter);
			savedLoading = g_World->isLoading;
			g_World->isLoading = 0;
			if (ent != NULL) {
				Entity_ExecuteEvent(&ent->resourceEntity.entity, 0x36);
			}
			g_World->isLoading = savedLoading;
		}
	}

	CVector_Destructor(&loadedSerials);

	USED(blockX);
	USED(blockY);
}

/*
 * Custom - CommitSaveFile
 *
 * Atomically swaps tmpPath in as finalPath, first preserving the
 * current finalPath as bkpPath via a hard link. finalPath is only ever
 * replaced by rename(2) - atomic within a directory - so it is never
 * absent or half-written, and bkpPath keeps the previous save. Replaces
 * the demo build's byte-copy .mul-to-.bkp backup step.
 *
 * fopen_ServerSide lowercases every path it opens, so the on-disk files
 * sit at the lowercased path; rename/link/unlink bypass that layer, so
 * we lowercase here to match. tmpPath is already lowercased by the caller.
 */
static void
CommitSaveFile(const char *finalPath, const char *bkpPath, const char *tmpPath)
{
	char finalL[1024];
	char bkpL[1024];

	snprintf(finalL, sizeof(finalL), "%s", finalPath);
	strlwr(finalL);
	snprintf(bkpL, sizeof(bkpL), "%s", bkpPath);
	strlwr(bkpL);

	unlink(bkpL);
	link(finalL, bkpL);
	rename(tmpPath, finalL);
}

/*
 * Custom - SaveDynamic0_WriteCommit
 *
 * Serializes every spatial-grid block to temporary index/data files,
 * fsyncs them, and atomically renames them into place (keeping the
 * previous save as .bkp via CommitSaveFile). Used by SaveDynamic0.
 * Assumes the world is already populated (EntityManager_AddAllToWorld
 * done by the caller). The new files are promoted only if every write
 * and fsync
 * succeeded; on any I/O error the previous .mul is left untouched.
 */
void
SaveDynamic0_WriteCommit(void)
{
	CIndexedFileManager indexedFile;
	char idxTmp[1024];
	char dataTmp[1024];
	int blockIdx;
	int ok;

	snprintf(idxTmp, sizeof(idxTmp), "%s.tmp", GLOBAL_file_dynidx0_mul);
	snprintf(dataTmp, sizeof(dataTmp), "%s.tmp", GLOBAL_file_dynamic0_mul);
	// fopen_ServerSide lowercases the path it opens, so the temp files
	// land at the lowercased path. Match that here so the unlink() below
	// and the rename() in CommitSaveFile hit the real files.
	strlwr(idxTmp);
	strlwr(dataTmp);

	CIndexedFileManager_Constructor(&indexedFile);
	CIndexedFileManager_Open(&indexedFile, idxTmp, dataTmp, "wb");

	if (indexedFile.indexFile == NULL || indexedFile.dataFile == NULL) {
		CIndexedFileManager_Close(&indexedFile);
		CIndexedFileManager_Destructor(&indexedFile);
		unlink(idxTmp);
		unlink(dataTmp);
		return;
	}

	for (blockIdx = 0; blockIdx < g_SpatialGrid.totalBlocks; blockIdx++) {
		CItem *itemHead;
		CItem *item;
		CDataBuffer buf;

		CDataBuffer_Constructor(&buf);
		itemHead = g_MapBlocks[blockIdx].itemHead;

		if (itemHead == NULL) {
			CIndexedFileManager_WriteBlock(&indexedFile, blockIdx, NULL, -1, 0);
			CDataBuffer_Destructor(&buf);
			continue;
		}

		item = itemHead;
		while (item != NULL) {
			if (!(item->itemFlags & 0x08)) {
				((void (*)(CItem *, CDataBuffer *, int))VT_FN(item, VT_SAVE))(item, &buf, 1);
			}
			item = item->spatialNext;
		}

		CDataBuffer_Append(&buf, "end", 4);

		CIndexedFileManager_WriteBlock(&indexedFile, blockIdx, buf.data, buf.len, 0);
		CDataBuffer_Destructor(&buf);
	}

	// Promote the temp files only if every write and fsync succeeded so
	// a disk error cannot corrupt the live save.
	ok = (ferror(indexedFile.indexFile) == 0 && ferror(indexedFile.dataFile) == 0);
	if (ok && (fflush(indexedFile.indexFile) != 0 || fsync(fileno(indexedFile.indexFile)) != 0))
		ok = 0;
	if (ok && (fflush(indexedFile.dataFile) != 0 || fsync(fileno(indexedFile.dataFile)) != 0))
		ok = 0;

	CIndexedFileManager_Close(&indexedFile);

	if (ok) {
		CommitSaveFile(GLOBAL_file_dynidx0_mul, GLOBAL_file_dynidx0_bkp, idxTmp);
		CommitSaveFile(GLOBAL_file_dynamic0_mul, GLOBAL_file_dynamic0_bkp, dataTmp);
	} else {
		unlink(idxTmp);
		unlink(dataTmp);
	}

	CIndexedFileManager_Destructor(&indexedFile);
}

/*
 * Helper - Dynamic_AddDeferredLoadedSerial
 *
 * Append to the deferred-loaded serial vector at 0x006E7690.
 */
void
Dynamic_AddDeferredLoadedSerial(uint32_t serial)
{
	CVector_PushBack(&g_deferredLoadedSerials, serial);
}

/*
 * Helper - CDataBuffer_WriteField
 *
 * Appends "key=value\0" to the buffer.
 */
void
CDataBuffer_WriteField(CDataBuffer *b, const char *key, const char *value)
{
	char buf[256];
	int len;

	len = snprintf(buf, sizeof(buf), "%s=%s", key, value);
	CDataBuffer_Append(b, buf, len + 1);
}

/*
 * Helper - CDataBuffer_WriteInt
 *
 * Appends "key=N\0" to the buffer.
 */
void
CDataBuffer_WriteInt(CDataBuffer *b, const char *key, int value)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "%s=%d", key, value);
	CDataBuffer_Append(b, buf, len + 1);
}

/*
 * Per-entity save serializers (vtable[0xC8]).
 *
 * Signature: Save(CDataBuffer *buf, int writeMarker). When writeMarker is 1
 * the function writes its "@=X\0" type header before the fields.
 *
 *   CItem::SaveFields (0x004C59EF) - base item fields
 *   CContainer::Save  (0x004C6A1D) - calls SaveFields, recurses into children
 *   CItem::Save       (0x004C6B76) - @=B, calls CContainer::Save
 *   CWeapon::Save     (0x004C6CC9) - @=W + weapon fields
 *   CSignpost::Save   (0x004C6BA6) - @=Z + mapextents/vect
 *   CMulti::Save      (0x004C6A7F) - @=X + corpsetype/corpitem
 *   CMobile::Save     (0x004C6FD1) - @=M + mobile fields
 *   CNPC::Save        (0x004C7D7E) - @=N + NPC fields
 *   CPlayer::Save     (0x004C842C) - @=P + player fields
 *   CShopkeeper::Save (0x004C835B) - @=S + restockCounter
 *   CEgg::Save        (0x004C83D0) - @=E
 */
