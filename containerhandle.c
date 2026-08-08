/*
 * ContainerHandle - page-cached, Feistel-encrypted file wrapper.
 *
 * Each handle manages a fixed-size page cache over a FILE* opened
 * inside uodemo.dat; pages are decrypted on load and encrypted on
 * flush through the GOST Feistel cipher. A red-black tree keyed on
 * FILE* maps plain file pointers back to their handles.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "containerhandle.h"
#include "feistel.h"
#include "region.h"
#include "stl.h"

/*
 * Binary globals for containerhandle std::map template instantiation.
 * 0x00701650: shared nil sentinel pointer (_Nilnode)
 * 0x0070164C: nil sentinel reference count
 * 0x00701648: container handle map (std::map<FILE*, ContainerHandle*>)
 */
StdTreeNode *g_HandleMapNil;    // 0x00701650
int g_HandleMapNilRef;          // 0x0070164C
StdMapTree *g_HandleMap;                // 0x00701648

/*
 * 0x004E4FDA - InitContainerHandle
 *
 * Wraps a file pointer in a paged cache: picks up the file size,
 * validates the 4-byte logical-size trailer, and registers the handle
 * in the global map.
 *
 * MODIFIED: the four-byte logical-size trailer is validated, where the
 * binary trusts it unconditionally.
 */
ContainerHandle *
InitContainerHandle(ContainerHandle *this, void *fp, int writable, int isQFile)
{
	int fileSize;
	int pageAligned;
	int initialPos;
	int bytesRead;

	this->fp = fp;
	this->pageBuffer = NULL;
	this->pageDataSize = 0;
	this->dirty = 0;
	this->logicalEnd = 0;
	this->remaining = 0;
	this->isQFile = isQFile;

	if (isQFile) {
		FileManagerEntry *entry = (FileManagerEntry *)fp;
		fileSize = (int)entry->dataSize;
		initialPos = (int)entry->curPos;
		pageAligned = fileSize & ~0xFFF;
		FileManager_Seek(g_FileManager, entry, (int)pageAligned, SEEK_SET);
	} else {
		initialPos = (int)ftell((FILE *)fp);
		fseek((FILE *)fp, 0, SEEK_END);
		fileSize = (int)ftell((FILE *)fp);
		pageAligned = fileSize & ~0xFFF;
		fseek((FILE *)fp, pageAligned, SEEK_SET);
	}
	this->logicalSize = fileSize;

	// MODIFIED: the binary always trusts the trailer unconditionally
	// because all files live in a packed container that guarantees
	// trailers. In standalone mode, original data files have no
	// trailer - their last 4 bytes are real data that can be
	// misinterpreted (e.g. statics0.mul yields 11777, truncating
	// a 22MB file to 11KB). We validate by checking that the trailer
	// value, when 4-byte aligned plus 4 bytes for the trailer itself,
	// equals the file size - matching ContainerHandle_UpdateSize's
	// write layout.
	bytesRead = ContainerHandle_AllocPage(this, 0x1000);
	if (bytesRead != 0) {
		int lastPageOff = fileSize - pageAligned;
		int trailer = *(int *)(this->pageBuffer + lastPageOff - 4);
		if (trailer > 0 && ((trailer + 3) & ~3) + 4 == fileSize) {
			this->logicalSize = trailer;
			this->logicalEnd = trailer;
		}
	}

	if (isQFile) {
		FileManagerEntry *entry = (FileManagerEntry *)fp;
		FileManager_Seek(g_FileManager, entry, initialPos, SEEK_SET);
	} else {
		fseek((FILE *)fp, initialPos, SEEK_SET);
	}

	ContainerHandle_Flush(this);

	this->writable = writable;
	this->pageFileOff = initialPos & ~0xFFF;
	this->pageCurOff = 0;

	StdTree_Insert(g_HandleMap, (uintptr_t)fp, (uintptr_t)this);

	return this;
}

/*
 * 0x004E50FA - ContainerHandle::Unregister
 *
 * Removes this handle from the global map, keyed by its file pointer.
 */
void
ContainerHandle_Unregister(ContainerHandle *this)
{
	StdTreeNode *node;
	uintptr_t key = (uintptr_t)this->fp;

	node = StdTree_LowerBound(g_HandleMap, key);
	if (node != g_HandleMap->head && node->key == key)
		StdTree_RBErase(g_HandleMap, node);
}

/*
 * 0x004E515A - ContainerHandle::UpdateSize
 *
 * Writes a fresh 4-byte trailer at the new logical end when the
 * logical size has grown past the stored value.
 */
void
ContainerHandle_UpdateSize(ContainerHandle *this)
{
	int sz;

	if (this->logicalSize == this->logicalEnd)
		return;

	sz = this->logicalSize;

	{
		int pos = ((sz + 3) / 4) * 4;
		ContainerHandle_Seek(this, pos, SEEK_SET);
	}

	ContainerHandle_Write(this, &sz, 4, 1);
}

/*
 * 0x004E519A - Init_UODEMODAT
 *
 * Bootstraps the Feistel S-boxes, the handle-map red-black tree, and
 * the FileManager that backs all packed-container reads.
 */
void
ContainerHandle_InitMap(void)
{
	StdMapTree *tree;
	StdTreeNode *head;

	Feistel_InitSBoxes();

	if (g_HandleMapNil == NULL) {
		g_HandleMapNil = malloc(sizeof(StdTreeNode));
		g_HandleMapNil->left = NULL;
		g_HandleMapNil->parent = NULL;
		g_HandleMapNil->right = NULL;
		g_HandleMapNil->color = 1;
	}
	g_HandleMapNilRef++;

	tree = calloc(1, sizeof(StdMapTree));

	head = malloc(sizeof(StdTreeNode));
	head->parent = g_HandleMapNil;
	head->color = 0;
	head->left = head;
	head->right = head;
	head->key = 0;
	head->value = 0;

	tree->head = head;
	tree->multi = 0;
	tree->size = 0;

	g_HandleMap = tree;

	FileManager_Init();
}

/*
 * 0x004E541A - ContainerHandle::ShutdownAll
 *
 * Tears down the handle-map tree and the FileManager at shutdown.
 */
void
ContainerHandle_ShutdownAll(void)
{
	if (g_HandleMap != NULL) {
		StdTree_Clear(g_HandleMap);
		OperatorDelete(g_HandleMap);
	}

	if (g_FileManager != NULL) {
		FileManager_Shutdown(g_FileManager);
		OperatorDelete(g_FileManager);
	}
}

/*
 * 0x004E545A - ContainerHandle::Flush
 *
 * Encrypts and writes back the current page if dirty, then releases
 * the buffer.
 */
void
ContainerHandle_Flush(ContainerHandle *this)
{
	if (this->dirty) {
		Feistel_Encrypt((uint32_t *)this->pageBuffer, (this->pageDataSize + 7) / 8);

		if (this->isQFile) {
			FileManagerEntry *entry = (FileManagerEntry *)this->fp;
			long saved = FileManager_Ftell(g_FileManager, entry);
			FileManager_Seek(g_FileManager, entry, this->pageFileOff, SEEK_SET);
			FileManager_Write(g_FileManager, this->pageBuffer, 1, this->pageDataSize, entry);
			if (this->pageFileOff + this->pageDataSize == this->logicalSize)
				FileManager_Seek(g_FileManager, entry, this->logicalSize, SEEK_SET);
			else if (saved != this->pageFileOff + this->pageDataSize)
				FileManager_Seek(g_FileManager, entry, (int)saved, SEEK_SET);
		} else {
			long saved = ftell((FILE *)this->fp);
			fseek((FILE *)this->fp, this->pageFileOff, SEEK_SET);
			fwrite(this->pageBuffer, 1, this->pageDataSize, (FILE *)this->fp);
			if (this->pageFileOff + this->pageDataSize == this->logicalSize)
				fseek((FILE *)this->fp, this->logicalSize, SEEK_SET);
			else if (saved != this->pageFileOff + this->pageDataSize)
				fseek((FILE *)this->fp, saved, SEEK_SET);
		}
		this->dirty = 0;
	}

	OperatorDelete(this->pageBuffer);
	this->pageBuffer = NULL;
	this->pageDataSize = 0;
	this->pageCurOff = 0;
	this->remaining = 0;
}

/*
 * 0x004E557A - FindContainerHandle
 *
 * Looks up the cache handle for fp in the global map.
 */
ContainerHandle *
FindContainerHandle(FILE *fp)
{
	StdTreeNode *node;
	uintptr_t key = (uintptr_t)fp;

	node = StdTree_LowerBound(g_HandleMap, key);
	if (node != g_HandleMap->head && node->key == key)
		return (ContainerHandle *)node->value;
	return NULL;
}

/*
 * 0x004E55EA - ContainerHandle::AllocPage
 *
 * Reloads a fresh page buffer at the current file position, decrypting
 * whatever was read. Returns bytes read.
 *
 * MODIFIED: the standalone branch reads through stdio when the file is
 * not inside the container, which the binary has no equivalent for.
 */
int
ContainerHandle_AllocPage(ContainerHandle *this, int size)
{
	int toRead;
	int nread;

	if (this->pageBuffer != NULL)
		ContainerHandle_Flush(this);

	this->pageBuffer = OperatorNew(size);
	this->pageCurOff = 0;

	if (this->isQFile) {
		FileManagerEntry *entry = (FileManagerEntry *)this->fp;
		this->pageFileOff = (int)entry->curPos;
	} else {
		this->pageFileOff = (int)ftell((FILE *)this->fp);
	}

	toRead = size;
	if (this->logicalSize < this->pageFileOff + toRead)
		toRead = this->logicalSize - this->pageFileOff;

	if (this->isQFile) {
		FileManagerEntry *entry = (FileManagerEntry *)this->fp;
		nread = FileManager_Read(g_FileManager, this->pageBuffer, 1, toRead, entry);
	} else {
		nread = (int)fread(this->pageBuffer, 1, toRead, (FILE *)this->fp);
	}
	this->pageDataSize = nread;

	if (nread > 0) {
		Feistel_Decrypt((uint32_t *)this->pageBuffer, (nread + 7) / 8);
	}

	return nread;
}

/*
 * 0x004E56AA - ContainerHandle::GrowFile
 *
 * Extends the logical size and grows the page buffer, optionally
 * flushing early pages to bound memory during long sequential writes.
 */
void
ContainerHandle_GrowFile(ContainerHandle *this, int extraBytes)
{
	int newLogical;
	uint8_t *newBuf;
	int newBufSize;

	newLogical = this->logicalSize + extraBytes;
	this->logicalSize = newLogical;

	if (extraBytes <= this->remaining) {
		this->remaining -= extraBytes;
		this->pageDataSize += extraBytes;
		return;
	}

	newBufSize = ((extraBytes >> 12) + 1) << 12;
	newBufSize += this->pageDataSize;

	if (this->pageCurOff > 0x4000) {
		// When the cursor is deep into the buffer, flush the early
		// pages and keep only the tail so memory stays bounded.
		int flushLen = (this->pageCurOff - 0x1000) & ~0xFFF;
		int tailLen = this->pageDataSize - flushLen;
		int smallBufSize = newBufSize - flushLen;

		newBuf = OperatorNew(smallBufSize);
		memcpy(newBuf, this->pageBuffer + flushLen, tailLen);
		{
			int savedPDS = this->pageDataSize;
			int savedPFO = this->pageFileOff;
			int savedPCO = this->pageCurOff;
			this->pageDataSize = flushLen;
			ContainerHandle_Flush(this);
			this->pageDataSize = savedPDS - flushLen;
			this->pageFileOff = savedPFO + flushLen;
			this->pageCurOff = savedPCO - flushLen;
		}

		this->pageBuffer = newBuf;
		this->pageDataSize += extraBytes;
		this->remaining = smallBufSize - this->pageDataSize;
		return;
	}

	newBuf = OperatorNew(newBufSize);
	memcpy(newBuf, this->pageBuffer, this->pageDataSize);
	OperatorDelete(this->pageBuffer);

	this->pageBuffer = newBuf;
	this->pageDataSize += extraBytes;
	this->remaining = newBufSize - this->pageDataSize;
}

/*
 * 0x004E57BA - ContainerHandle::Read
 *
 * Reads data through the page cache. If the request fits within the
 * current page, copies from the buffer directly. Otherwise copies the
 * partial page, flushes, loads the next page, and continues.
 * Returns the number of elements read.
 */
int
ContainerHandle_Read(ContainerHandle *this, void *buf, int elemSize, int elemCount)
{
	int totalBytes;
	int bytesLeft;
	int pages;
	int avail;
	int nread;
	uint8_t *dst;

	totalBytes = elemCount * elemSize;
	pages = (totalBytes >> 12) + 1;
	bytesLeft = totalBytes;
	dst = (uint8_t *)buf;

	if (this->pageBuffer != NULL) {
		if (this->pageCurOff + totalBytes <= this->pageDataSize) {
			memcpy(dst, this->pageBuffer + this->pageCurOff, totalBytes);
			this->pageCurOff += totalBytes;
			return elemCount;
		}

		avail = this->pageDataSize - this->pageCurOff;
		memcpy(dst, this->pageBuffer + this->pageCurOff, avail);
		dst += avail;
		ContainerHandle_Flush(this);
		pages -= (avail >> 12);
		bytesLeft = totalBytes - avail;
	}

	nread = ContainerHandle_AllocPage(this, pages << 12);
	if (bytesLeft < nread)
		nread = bytesLeft;

	memcpy(dst, this->pageBuffer, nread);

	bytesLeft -= nread;
	this->pageCurOff += nread;
	if (bytesLeft != 0)
		this->pageCurOff++;

	return (totalBytes - bytesLeft) / elemSize;
}

/*
 * 0x004E58BA - ContainerHandle::Write
 *
 * Writes data through the page cache, growing the file as needed.
 */
int
ContainerHandle_Write(ContainerHandle *this, const void *buf, int elemSize, int elemCount)
{
	int totalBytes;
	int bytesLeft;
	int pages;
	int avail;
	const uint8_t *src;

	if (!this->writable)
		return 0;

	totalBytes = elemCount * elemSize;
	bytesLeft = totalBytes;
	pages = (totalBytes >> 12) + 1;
	src = (const uint8_t *)buf;

	if (this->pageBuffer == NULL)
		ContainerHandle_AllocPage(this, pages << 12);

	if (this->pageCurOff + totalBytes <= this->pageDataSize) {
		memcpy(this->pageBuffer + this->pageCurOff, src, totalBytes);
		this->dirty = 1;
		this->pageCurOff += totalBytes;
		return elemCount;
	}

	if (this->pageFileOff + this->pageDataSize < this->logicalSize) {
		avail = this->pageDataSize - this->pageCurOff;
		memcpy(this->pageBuffer + this->pageCurOff, src, avail);
		pages -= (avail >> 12);
		bytesLeft = totalBytes - avail;
		src += avail;
		this->dirty = 1;
		ContainerHandle_Flush(this);
	}

	if (this->pageBuffer == NULL)
		ContainerHandle_AllocPage(this, pages << 12);

	if (bytesLeft + this->pageFileOff + this->pageCurOff > this->logicalSize) {
		int extra = (this->pageCurOff + bytesLeft) - this->pageDataSize;
		ContainerHandle_GrowFile(this, extra);

		memcpy(this->pageBuffer + this->pageCurOff, src, bytesLeft);
		this->dirty = 1;
		this->pageCurOff += bytesLeft;
		return elemCount;
	}

	memcpy(this->pageBuffer, src, bytesLeft);
	this->pageCurOff += bytesLeft;
	this->dirty = 1;
	return totalBytes / elemSize;
}

/*
 * 0x004E5A3A - ContainerHandle::Seek
 *
 * Seeks within the file, adjusting the page buffer without I/O when
 * possible.
 */
int
ContainerHandle_Seek(ContainerHandle *this, int offset, int whence)
{
	int target;
	int pageEnd;

	switch (whence) {
	case SEEK_SET:
		target = offset;
		break;
	case SEEK_CUR:
		target = this->pageFileOff + this->pageCurOff + offset;
		break;
	case SEEK_END:
		target = this->logicalSize + offset;
		break;
	default:
		target = offset;
		break;
	}

	// No pageBuffer NULL guard: when no buffer is held, pageDataSize and
	// remaining are 0 so the in-range check fails naturally.
	pageEnd = this->pageFileOff + this->pageDataSize + this->remaining;
	if (target >= this->pageFileOff && target < pageEnd) {
		int newOff = target - this->pageFileOff;
		this->pageCurOff = newOff;

		if (target > this->pageFileOff + this->pageDataSize) {
			this->remaining = pageEnd - target;
		}
		return 0;
	}

	ContainerHandle_Flush(this);

	{
		int pageStart = target & ~0xFFF;
		int seekResult;

		if (this->isQFile) {
			FileManagerEntry *entry = (FileManagerEntry *)this->fp;
			seekResult = FileManager_Seek(g_FileManager, entry, pageStart, SEEK_SET);
		} else {
			seekResult = fseek((FILE *)this->fp, pageStart, SEEK_SET);
		}
		if (seekResult != 0)
			return seekResult;

		this->pageFileOff = pageStart;

		if ((target & 0xFFF) && pageStart + this->pageCurOff <= this->logicalSize)
			ContainerHandle_AllocPage(this, 0x1000);

		this->pageCurOff = target - this->pageFileOff;
	}

	return 0;
}

/*
 * 0x004E5BAA - ContainerHandle::Close
 *
 * Updates the size trailer, flushes dirty pages, and closes the file.
 */
int
ContainerHandle_Close(ContainerHandle *this)
{
	ContainerHandle_UpdateSize(this);
	ContainerHandle_Flush(this);

	if (this->isQFile) {
		FileManagerEntry *entry = (FileManagerEntry *)this->fp;
		return FileManager_Close(g_FileManager, entry);
	} else {
		return fclose((FILE *)this->fp);
	}
}
