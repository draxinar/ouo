#ifndef FILEMANAGER_H_
#define FILEMANAGER_H_

/*
 * FileManager - file I/O manager for server data files.
 * Binary: singleton pointer at 0x00701640, methods at 0x004E2770..0x004E2F90.
 *
 * In the binary, the FileManager packs all data files into a single container
 * file with a directory of 0x118-byte entries at the start. Each entry
 * stores a filename, data offset within the container, data size, and
 * current position. The fopen_ServerSide wrapper opens files through the
 * FileManager; InitContainerHandle reads entry+0x110 (dataSize) to get the
 * file extent without needing ftell(SEEK_END).
 *
 * Our distribution has individual data files (not packed in a container), so
 * we implement the FileManager in "standalone mode": each entry wraps an
 * individual FILE* and provides the same metadata interface. The container
 * offset translation (dirSize + dataOffset + curPos) reduces to just
 * curPos since dataOffset = 0 and dirSize = 0 for standalone files.
 *
 * Client symbol table: class FileManager (11FileManager).
 */

#include <stdint.h>
#include <stdio.h>

/*
 * Directory entry describing one packed file (0x118 bytes). Binary
 * stores allocatedSize at 0x108; we repurpose that slot as a per-file
 * FILE* since standalone mode opens each data file individually
 * instead of slicing a shared container handle.
 */
typedef struct FileManagerEntry {
	char filename[256];   // 0x000 - null-terminated filename
	uint32_t unk100;      // 0x100
	uint32_t dataOffset;  // 0x104 - data start offset in container
	FILE *fp;             // 0x108 - binary: allocatedSize; standalone: FILE*
	uint32_t isOpen;      // 0x10C - nonzero if currently open
	uint32_t dataSize;    // 0x110 - total data extent (file size)
	uint32_t curPos;      // 0x114 - current read/write position
} FileManagerEntry;

#if __SIZEOF_POINTER__ == 4
_Static_assert(sizeof(FileManagerEntry) == 0x118, "FileManagerEntry size mismatch");
#endif

/*
 * Maximum number of concurrently tracked files.
 * Binary has 3000 entry slots (0x118-byte entries, with numEntries trailing
 * the array). Our standalone mode needs enough for all data files opened
 * during the server's lifetime (scripts ~861, weapons ~152, convo fragments
 * ~107, data ~40+).
 */
#define FM_MAX_ENTRIES 3000

/*
 * FileManager singleton (0xCD274 bytes). The entries[] array and the
 * header/containerFp slots mirror the binary's packed-container layout;
 * standalone mode uses them as a registry of open data files.
 */
// clang-format off
typedef struct FileManager {
	FileManagerEntry header;                  // 0x000 - unused directory slot
	FILE *containerFp;                        // 0x118 - Win32 HANDLE in binary
	uint8_t _mapStorage[0x10];                // 0x11C - std::map placeholder
	FileManagerEntry entries[FM_MAX_ENTRIES]; // 0x12C
	int numEntries;                           // 0xCD26C
	int totalDirSize;                         // 0xCD270
} FileManager;
// clang-format on

/*
 * Maps an OpenByType category ID to a base path (12 bytes; binary
 * table at 0x0061D138).
 */
typedef struct FileTypeEntry {
	const char *basePath;   // 0x00
	int hasPrefix;          // 0x04 - 1 = prepend basePath to filename
	int reserved;           // 0x08
} FileTypeEntry;

extern FileManager *g_FileManager; // 0x00701640

const FileTypeEntry *FileManager_GetCategory(int category); // 0x00480800
FILE *FileManager_OpenByType(int category, const char *filename, const char *mode); // 0x00480877
void FileManager_Init(void); // 0x004E2780
FileManagerEntry *FileManager_Open(FileManager *fm, const char *filename, const char *mode); // 0x004E27FD
int FileManager_Close(FileManager *fm, FileManagerEntry *entry); // 0x004E2995
void FileManager_ReadDirectory(FileManager *fm); // 0x004E2A96
int FileManager_Read(FileManager *fm, void *buf, int elemSize, int elemCount, FileManagerEntry *entry); // 0x004E2CC1
int FileManager_Seek(FileManager *fm, FileManagerEntry *entry, int offset, int whence); // 0x004E2D97
int FileManager_Write(FileManager *fm, const void *buf, int elemSize, int elemCount, FileManagerEntry *entry); // 0x004E2E60
long FileManager_Ftell(FileManager *fm, FileManagerEntry *entry);
void FileManager_Shutdown(FileManager *fm);

#endif /* FILEMANAGER_H_ */
