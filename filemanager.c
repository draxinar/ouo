/*
 * FileManager - directory of server data files.
 *
 * When uodemo.dat is present, looks up each requested file through
 * the Feistel-decrypted directory packed inside the container; when it
 * is absent (our dev environment), falls back to plain files under
 * ../.rundir/.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dat.h"
#include "feistel.h"
#include "filemanager.h"
#include "io.h"

/* Global FileManager pointer. Binary: 0x00701640. */
FileManager *g_FileManager;

/* Each entry is 12 bytes: {basePath, hasPrefix, reserved}. */

// 0x0061D138 - file type table for categories 0x01..0x04
static const FileTypeEntry g_FileTypes_1_4[4] = {
	{ "../.rundir/access.list", 0, 0 },     /* 0x01 */
	{ NULL, 0, 0 },                  /* 0x02 */
	{ NULL, 0, 0 },                  /* 0x03 */
	{ NULL, 0, 0 },                  /* 0x04 */
};

// 0x0061D168 - file type table for categories 0x15..0x20
static const FileTypeEntry g_FileTypes_15_20[12] = {
	{ "../.rundir/animdata.mul", 0, 0 },    /* 0x15 */
	{ "../.rundir/art.mul", 0, 0 },         /* 0x16 */
	{ "../.rundir/artidx.mul", 0, 0 },      /* 0x17 */
	{ "../.rundir/hues.mul", 0, 0 },        /* 0x18 */
	{ NULL, 0, 0 },                  /* 0x19 */
	{ NULL, 0, 0 },                  /* 0x1a */
	{ NULL, 0, 0 },                  /* 0x1b */
	{ "../.rundir/skills.txt", 0, 0 },      /* 0x1c */
	{ NULL, 0, 0 },                  /* 0x1d */
	{ NULL, 0, 0 },                  /* 0x1e */
	{ "../.rundir/tiledata.mul", 0, 0 },    /* 0x1f */
	{ NULL, 0, 0 },                  /* 0x20 */
};

// 0x0061D1F8 - file type table for categories 0x29..0x37
static const FileTypeEntry g_FileTypes_29_37[15] = {
	{ "../.rundir/itemres.mul", 0, 0 },     /* 0x29 */
	{ "../.rundir/multi.txt", 0, 0 },       /* 0x2a */
	{ "../.rundir/resregions.txt", 0, 0 },  /* 0x2b */
	{ "../.rundir/restypes.mul", 0, 0 },    /* 0x2c */
	{ "../.rundir/weather.txt", 0, 0 },     /* 0x2d */
	{ "../.rundir/boards/", 1, 0 },         /* 0x2e */
	{ "../.rundir/classifier/", 1, 0 },     /* 0x2f */
	{ "../.rundir/convo/", 1, 0 },          /* 0x30 */
	{ "../.rundir/scripts/", 1, 0 },        /* 0x31 */
	{ "../.rundir/bank/", 1, 0 },           /* 0x32 */
	{ "../.rundir/weapons/", 1, 0 },        /* 0x33 */
	{ "../.rundir/books.idx", 0, 0 },       /* 0x34 */
	{ "../.rundir/books.mul", 0, 0 },       /* 0x35 */
	{ NULL, 0, 1 },                  /* 0x36 */
	{ "../.rundir/loginscr.txt", 0, 0 },    /* 0x37 */
};

// 0x0061D2B0 - file type table for categories 0x3d..0x41
static const FileTypeEntry g_FileTypes_3d_41[5] = {
	{ "../.rundir/resbank.mul", 0, 1 },     /* 0x3d */
	{ "../.rundir/time.mul", 0, 1 },        /* 0x3e */
	{ NULL, 0, 1 },                  /* 0x3f */
	{ NULL, 0, 1 },                  /* 0x40 */
	{ NULL, 1, 1 },                  /* 0x41 */
};

/*
 * 0x00480800 - FileManager_GetCategory
 *
 * Maps a category id to a FileTypeEntry, or NULL if out of range.
 */
const FileTypeEntry *
FileManager_GetCategory(int category)
{
	if (category >= 1 && category < 5)
		return &g_FileTypes_1_4[category - 1];
	if (category >= 0x15 && category < 0x21)
		return &g_FileTypes_15_20[category - 0x15];
	if (category >= 0x29 && category < 0x38)
		return &g_FileTypes_29_37[category - 0x29];
	if (category >= 0x3d && category < 0x42)
		return &g_FileTypes_3d_41[category - 0x3d];
	return NULL;
}

/*
 * 0x00480877 - FileManager_OpenByType
 *
 * Opens a file by category. When the category has hasPrefix=1 the basePath
 * is a directory prefix and filename is appended; otherwise basePath is
 * the full path and filename is ignored. The binary's timing accumulator
 * at 0x0069A788 is not replicated.
 */
FILE *
FileManager_OpenByType(int category, const char *filename, const char *mode)
{
	const FileTypeEntry *entry;
	char buf[256];

	entry = FileManager_GetCategory(category);
	if (entry == NULL)
		return NULL;

	if (entry->hasPrefix) {
		sprintf(buf, "%s%s", entry->basePath, filename);
		return fopen_ServerSide(buf, mode);
	} else {
		return fopen_ServerSide(entry->basePath, mode);
	}
}

/*
 * 0x004E2780 - FileManager::Init
 *
 * Allocates the singleton. If uodemo.dat exists, opens it and reads the
 * directory (container mode); otherwise leaves entries empty for
 * standalone mode. MODIFIED from the binary, which unconditionally opens
 * the container filename passed as an argument.
 */
void
FileManager_Init(void)
{
	g_FileManager = calloc(1, sizeof(FileManager));

	g_FileManager->containerFp = fopen("uodemo.dat", "rb+");
	if (g_FileManager->containerFp != NULL) {
		FileManager_ReadDirectory(g_FileManager);
	}
}

/*
 * 0x004E27FD - FileManager::Open
 *
 * Looks up a file by name and returns its entry, resetting position on
 * write mode. MODIFIED: the binary's std::map lookup is replaced by a
 * linear scan; standalone mode fopens the underlying file since there is
 * no container to index into.
 */
FileManagerEntry *
FileManager_Open(FileManager *fm, const char *filename, const char *mode)
{
	FileManagerEntry *entry;
	int i;
	long fileSize;

	if (fm == NULL)
		return NULL;

	if (fm->containerFp != NULL) {
		// MODIFIED: linear scan instead of std::map lookup.
		for (i = 0; i < fm->numEntries; i++) {
			if (strcasecmp(fm->entries[i].filename, filename) == 0) {
				entry = &fm->entries[i];

				if (strchr(mode, 'w')) {
					if (entry->isOpen)
						return NULL;
					entry->dataSize = 0;
					entry->curPos = 0;
				}
				return entry;
			}
		}
		return NULL;
	}

	for (i = 0; i < fm->numEntries; i++) {
		if (strcasecmp(fm->entries[i].filename, filename) == 0) {
			entry = &fm->entries[i];
			if (strchr(mode, 'w') && entry->isOpen)
				return NULL;
			if (strchr(mode, 'w')) {
				entry->dataSize = 0;
				entry->curPos = 0;
			}
			if (entry->fp != NULL)
				fclose(entry->fp);
			entry->fp = fopen(filename, mode);
			if (entry->fp == NULL)
				return NULL;
			if (!strchr(mode, 'w')) {
				fseek(entry->fp, 0, SEEK_END);
				fileSize = ftell(entry->fp);
				fseek(entry->fp, 0, SEEK_SET);
				entry->dataSize = (uint32_t)fileSize;
			}
			entry->curPos = 0;
			entry->isOpen = 1;
			return entry;
		}
	}

	if (fm->numEntries < FM_MAX_ENTRIES) {
		entry = &fm->entries[fm->numEntries];
		fm->numEntries++;
	} else {
		// Binary container mode pre-allocates from the directory so it
		// never reaches this path; standalone mode accumulates an entry
		// per unique file, so recycle a closed slot to avoid exhaustion.
		entry = NULL;
		for (i = 0; i < fm->numEntries; i++) {
			if (!fm->entries[i].isOpen) {
				entry = &fm->entries[i];
				break;
			}
		}
		if (entry == NULL)
			return NULL;
	}

	memset(entry, 0, sizeof(*entry));
	strncpy(entry->filename, filename, sizeof(entry->filename) - 1);

	entry->fp = fopen(filename, mode);
	if (entry->fp == NULL) {
		// Probing a non-existent file (e.g. missing .bkp on first run)
		// must free the slot we just appended, otherwise each miss
		// wastes an entry until FM_MAX_ENTRIES is exhausted.
		if (fm->numEntries > 0 && &fm->entries[fm->numEntries - 1] == entry)
			fm->numEntries--;
		return NULL;
	}

	if (!strchr(mode, 'w')) {
		fseek(entry->fp, 0, SEEK_END);
		fileSize = ftell(entry->fp);
		fseek(entry->fp, 0, SEEK_SET);
		entry->dataSize = (uint32_t)fileSize;
	}

	entry->curPos = 0;
	entry->dataOffset = 0;
	entry->isOpen = 1;
	return entry;
}

/*
 * 0x004E2995 - FileManager::Close
 *
 * Container mode: Feistel-encrypts the entry and writes it back to the
 * directory at its index. Standalone mode: closes the underlying FILE*.
 */
int
FileManager_Close(FileManager *fm, FileManagerEntry *entry)
{
	int result = 0;

	if (entry == NULL)
		return -1;

	if (fm->containerFp != NULL) {
		FileManagerEntry localBuf;
		int index;
		long fileSize;

		if (entry->isOpen)
			return 0;

		index = (int)(entry - fm->entries);
		memcpy(&localBuf, entry, sizeof(FileManagerEntry));
		Feistel_Encrypt((uint32_t *)&localBuf, 0x23);

		fseek(fm->containerFp, 0, SEEK_END);
		fileSize = ftell(fm->containerFp);

		fseek(fm->containerFp, index * (int)sizeof(FileManagerEntry), SEEK_SET);
		fwrite(&localBuf, sizeof(FileManagerEntry), 1, fm->containerFp);

		fseek(fm->containerFp, fileSize, SEEK_SET);
		return 0;
	}

	if (entry->fp != NULL) {
		result = fclose(entry->fp);
		entry->fp = NULL;
	}
	entry->isOpen = 0;
	entry->curPos = 0;
	return result;
}

/*
 * 0x004E2A96 - FileManager::ReadDirectory
 *
 * Reads and Feistel-decrypts 0x118-byte directory entries until the
 * "@@@.@@@" sentinel, populating fm->entries. MODIFIED: POSIX I/O in
 * place of Win32; the std::map filename index is dropped in favor of a
 * linear scan in FileManager_Open.
 */
void
FileManager_ReadDirectory(FileManager *fm)
{
	FileManagerEntry localEntry;
	long fileSize;

	fm->numEntries = 0;
	fm->totalDirSize = 0;

	fseek(fm->containerFp, 0, SEEK_END);
	fileSize = ftell(fm->containerFp);
	USED(fileSize);

	fseek(fm->containerFp, 0, SEEK_SET);

	memset(&localEntry, 0, sizeof(localEntry));

	for (;;) {
		if (fread(&localEntry, sizeof(FileManagerEntry), 1, fm->containerFp) != 1)
			break;

		Feistel_Decrypt((uint32_t *)&localEntry, 0x23);

		fm->totalDirSize += (int)sizeof(FileManagerEntry);

		// "@@@.@@@" is the directory-terminator sentinel written by
		// Init_UODEMODAT at 0x004E532C.
		if (strcmp(localEntry.filename, "@@@.@@@") == 0)
			break;

		if (fm->numEntries >= FM_MAX_ENTRIES)
			break;
		memcpy(&fm->entries[fm->numEntries], &localEntry, sizeof(FileManagerEntry));
		fm->numEntries++;
	}
	fseek(fm->containerFp, 0, SEEK_END);
}

/*
 * 0x004E2CC1 - FileManager::Read
 *
 * Reads from the entry's current position. In container mode the seek is
 * into containerFp at (dirSize + dataOffset + curPos); in standalone mode
 * it seeks the entry's own FILE*.
 */
int
FileManager_Read(FileManager *fm, void *buf, int elemSize, int elemCount, FileManagerEntry *entry)
{
	int nread;

	if (entry == NULL)
		return 0;

	// Clamp elemCount so curPos + elemSize*elemCount does not exceed dataSize.
	{
		uint32_t totalBytes = (uint32_t)elemSize * (uint32_t)elemCount;
		if (entry->curPos + totalBytes > entry->dataSize) {
			uint32_t remaining = entry->dataSize - entry->curPos;
			elemCount = (int)(remaining / (uint32_t)elemSize);
		}
	}

	if (elemCount <= 0)
		return 0;

	if (fm->containerFp != NULL) {
		long absPos = (long)fm->totalDirSize + (long)entry->dataOffset + (long)entry->curPos;
		fseek(fm->containerFp, absPos, SEEK_SET);
		nread = (int)fread(buf, elemSize, elemCount, fm->containerFp);
	} else {
		fseek(entry->fp, (long)entry->curPos, SEEK_SET);
		nread = (int)fread(buf, elemSize, elemCount, entry->fp);
	}
	entry->curPos += (uint32_t)(nread * elemSize);
	return nread;
}

/*
 * 0x004E2D97 - FileManager::Seek
 *
 * Updates entry->curPos per whence (clamped to dataSize when open) and
 * seeks the underlying file. Container mode uses dirSize+dataOffset as
 * the base; standalone mode seeks the entry's FILE* directly.
 */
int
FileManager_Seek(FileManager *fm, FileManagerEntry *entry, int offset, int whence)
{
	int newPos;

	if (entry == NULL)
		return -1;

	switch (whence) {
	case SEEK_SET:
		newPos = offset;
		break;
	case SEEK_CUR:
		newPos = (int)entry->curPos + offset;
		break;
	case SEEK_END:
		newPos = (int)entry->dataSize + offset;
		break;
	default:
		return -1;
	}

	if (newPos > (int)entry->dataSize && entry->isOpen)
		newPos = (int)entry->dataSize;

	entry->curPos = (uint32_t)newPos;

	if (fm->containerFp != NULL) {
		long absPos = (long)fm->totalDirSize + (long)entry->dataOffset + (long)entry->curPos;
		return fseek(fm->containerFp, absPos, SEEK_SET);
	}

	if (entry->fp != NULL)
		return fseek(entry->fp, (long)entry->curPos, SEEK_SET);
	return -1;
}

/*
 * 0x004E2E60 - FileManager::Write
 *
 * Writes at the entry's current position, extending dataSize if needed.
 * Container mode addresses within containerFp; standalone mode writes
 * the entry's own FILE*.
 */
int
FileManager_Write(FileManager *fm, const void *buf, int elemSize, int elemCount, FileManagerEntry *entry)
{
	int nwritten;

	if (entry == NULL)
		return 0;

	if (fm->containerFp != NULL) {
		long absPos = (long)fm->totalDirSize + (long)entry->dataOffset + (long)entry->curPos;
		fseek(fm->containerFp, absPos, SEEK_SET);
		nwritten = (int)fwrite(buf, elemSize, elemCount, fm->containerFp);
	} else {
		if (entry->fp == NULL)
			return 0;
		fseek(entry->fp, (long)entry->curPos, SEEK_SET);
		nwritten = (int)fwrite(buf, elemSize, elemCount, entry->fp);
	}
	entry->curPos += (uint32_t)(nwritten * elemSize);

	if (entry->curPos > entry->dataSize)
		entry->dataSize = entry->curPos;

	return nwritten;
}

/*
 * Helper - FileManager_Ftell
 *
 * Returns the entry's current position.
 */
long
FileManager_Ftell(FileManager *fm, FileManagerEntry *entry)
{
	USED(fm);
	if (entry == NULL)
		return -1;
	return (long)entry->curPos;
}

/*
 * Helper - FileManager_Shutdown
 *
 * Closes all entries (standalone mode) and the container file.
 */
void
FileManager_Shutdown(FileManager *fm)
{
	int i;

	if (fm == NULL)
		return;

	for (i = 0; i < fm->numEntries; i++) {
		// entry->fp at offset 0x108 is the binary's allocatedSize
		// field in container mode, not a FILE* - only close it in
		// standalone mode.
		if (fm->containerFp == NULL && fm->entries[i].fp != NULL) {
			fclose(fm->entries[i].fp);
			fm->entries[i].fp = NULL;
		}
		fm->entries[i].isOpen = 0;
	}
	fm->numEntries = 0;

	if (fm->containerFp != NULL) {
		fclose(fm->containerFp);
		fm->containerFp = NULL;
	}
}
