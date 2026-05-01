#ifndef CONTAINERHANDLE_H_
#define CONTAINERHANDLE_H_

#include <stdint.h>
#include <stdio.h>

/*
 * ContainerHandle - page-cached file I/O wrapper (binary: 0x004E4FDA..0x004E5F9A)
 *
 * The binary wraps FILE* handles in a ContainerHandle struct that provides
 * 4096-byte page caching for server data files. The container handle registry is a
 * std::map<FILE*,ContainerHandle*> (MSVC std::_Tree red-black tree) at
 * 0x00701648 in the binary. The fopen/fclose/fseek/fread/fwrite_ServerSide
 * wrapper functions intercept I/O on managed files and route it through the
 * page cache.
 *
 * The binary has two I/O backends:
 *   isQFile=1: uses FileManager::read/seek/write via global at 0x00701640
 *   isQFile=0: uses plain fseek/fread/fwrite
 * Our implementation supports both paths. The isQFile=1 path uses the
 * FileManager (see filemanager.h) which provides file metadata at known
 * offsets (entry+0x110 = dataSize, entry+0x114 = curPos).
 *
 * The binary contains a GOST 28147-89 Feistel cipher in CTR mode (0x004E4DB0
 * encrypt, 0x004E4EC5 decrypt) using four 256-entry S-boxes at 0x00700640..0x00701240.
 * Page data is encrypted on flush and decrypted on load. The S-box
 * initialization is part of Init_UODEMODAT (0x004E519A). In the shipping binary,
 * Config_Constructor (0x00467590) which calls Init_UODEMODAT has zero callers,
 * so the S-boxes remain BSS-zeroed and the cipher is a runtime no-op. The
 * cipher is decompiled exactly in feistel.c for completeness.
 */

#include "filemanager.h"

__extension__ typedef struct StdMapTree StdMapTree;
__extension__ typedef struct StdTreeNode StdTreeNode;

/*
 * Page-cached I/O handle wrapping a FileManager entry or a plain FILE*
 * (0x2C bytes; binary allocates with operator new at 0x004E5E2D).
 */
__extension__ typedef struct ContainerHandle ContainerHandle;
struct ContainerHandle {
	void *fp;               // 0x00 - FileManagerEntry* (Q) or FILE* (non-Q)
	uint8_t *pageBuffer;    // 0x04
	int pageDataSize;       // 0x08 - valid bytes in pageBuffer
	int dirty;              // 0x0C - page modified, needs flush
	int pageFileOff;        // 0x10 - file offset of the loaded page
	int pageCurOff;         // 0x14 - cursor within the page buffer
	int logicalSize;        // 0x18 - logical file size (from trailer)
	int logicalEnd;         // 0x1C - logical end position
	int remaining;          // 0x20 - bytes left in the page past the cursor
	int writable;           // 0x24
	int isQFile;            // 0x28 - 1: routed through FileManager
};

extern StdMapTree *g_HandleMap; // 0x00701648
extern int g_HandleMapNilRef; // 0x0070164C
extern StdTreeNode *g_HandleMapNil; // 0x00701650

ContainerHandle *InitContainerHandle(ContainerHandle *this, void *fp, int writable, int isQFile); // 0x004E4FDA
void ContainerHandle_Unregister(ContainerHandle *this); // 0x004E50FA
void ContainerHandle_UpdateSize(ContainerHandle *this); // 0x004E515A
void ContainerHandle_InitMap(void); // 0x004E519A
void ContainerHandle_ShutdownAll(void); // 0x004E541A
void ContainerHandle_Flush(ContainerHandle *this); // 0x004E545A
ContainerHandle *FindContainerHandle(FILE *fp); // 0x004E557A
int ContainerHandle_AllocPage(ContainerHandle *this, int size); // 0x004E55EA
void ContainerHandle_GrowFile(ContainerHandle *this, int extraBytes); // 0x004E56AA
int ContainerHandle_Read(ContainerHandle *this, void *buf, int elemSize, int elemCount); // 0x004E57BA
int ContainerHandle_Write(ContainerHandle *this, const void *buf, int elemSize, int elemCount); // 0x004E58BA
int ContainerHandle_Seek(ContainerHandle *this, int offset, int whence); // 0x004E5A3A
int ContainerHandle_Close(ContainerHandle *this); // 0x004E5BAA

#endif
