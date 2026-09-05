/*
 * ServerSide file I/O wrappers (fopen / fread / fwrite / fgetc / ...).
 *
 * Routes each call through the ContainerHandle page-cache layer when
 * the file lives inside uodemo.dat, and falls through to the plain
 * CRT functions otherwise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fns.h"

#include "containerhandle.h"
#include "region.h"

/*
 * 0x004E5CFA - fopen_ServerSide
 *
 * Lowercases the filename, appends ".q", forces binary mode, and
 * opens the file through the FileManager container behind a
 * ContainerHandle. MODIFIED: when no uodemo.dat is present (or the
 * mode is writable) the ".q" is stripped and plain fopen is used.
 */
FILE *
fopen_ServerSide(const char *fileName, const char *mode)
{
	char fileNameBuf[1024];
	char modeBuf[12];
	FileManagerEntry *entry;
	ContainerHandle *ch;
	int writable;
	int isQFile;
	char *qp;

	strcpy(fileNameBuf, fileName);
	strlwr(fileNameBuf);
	strcat(fileNameBuf, ".q");
	strcpy(modeBuf, mode);

	if (!strchr(modeBuf, 'b')) {
		int len = strlen(modeBuf);
		modeBuf[len] = 'b';
		modeBuf[len + 1] = '\0';
	}

	// MODIFIED: standalone mode (no uodemo.dat) opens the bare file.
	if (g_FileManager == NULL || g_FileManager->containerFp == NULL) {
		fileNameBuf[strlen(fileNameBuf) - 2] = '\0';
		return fopen(fileNameBuf, modeBuf);
	}

	// MODIFIED: writes bypass the container so our periodic save
	// lands on disk instead of inside uodemo.dat. The binary never
	// exercises the write path, so this branch is dead there.
	if (strchr(mode, 'w') != NULL || strchr(mode, '+') != NULL) {
		fileNameBuf[strlen(fileNameBuf) - 2] = '\0';
		return fopen(fileNameBuf, modeBuf);
	}

	entry = FileManager_Open(g_FileManager, fileNameBuf, modeBuf);
	if (entry == NULL)
		return NULL;

	isQFile = 1;

	qp = strstr(fileNameBuf, ".q");
	if (qp != NULL && qp[2] == '\0') {
		writable = (strchr(mode, '+') != NULL || strchr(mode, 'w') != NULL) ? 1 : 0;

		ch = OperatorNew(sizeof(ContainerHandle));

		if (ch != NULL)
			InitContainerHandle(ch, entry, writable, isQFile);
	}

	return (FILE *)entry;
}

/*
 * 0x004E5E7A - fread_ServerSide
 *
 * Reads through the ContainerHandle page cache when the file has one,
 * otherwise delegates to fread.
 */
int
fread_ServerSide(void *buf, int elemSize, int elemCount, FILE *f)
{
	ContainerHandle *ch;

	ch = FindContainerHandle(f);
	if (ch == NULL)
		return (int)fread(buf, elemSize, elemCount, f);

	return ContainerHandle_Read(ch, buf, elemSize, elemCount);
}

/*
 * 0x004E5ECA - fwrite_ServerSide
 *
 * Writes through the ContainerHandle page cache when the file has
 * one, otherwise delegates to fwrite.
 */
int
fwrite_ServerSide(const void *buf, int elemSize, int elemCount, FILE *f)
{
	ContainerHandle *ch;

	ch = FindContainerHandle(f);
	if (ch == NULL)
		return (int)fwrite(buf, elemSize, elemCount, f);

	return ContainerHandle_Write(ch, buf, elemSize, elemCount);
}

/*
 * 0x004E5F1A - fseek_ServerSide
 *
 * Seeks through the ContainerHandle page cache when the file has
 * one, otherwise delegates to fseek.
 */
int
fseek_ServerSide(FILE *f, long offset, int whence)
{
	ContainerHandle *ch;

	ch = FindContainerHandle(f);
	if (ch == NULL)
		return fseek(f, offset, whence);

	return ContainerHandle_Seek(ch, (int)offset, whence);
}

/*
 * 0x004E5F5A - fclose_ServerSide
 *
 * Closes the page cache and frees the handle when the file has a
 * ContainerHandle, otherwise delegates to fclose.
 */
int
fclose_ServerSide(FILE *f)
{
	ContainerHandle *ch;
	int result;

	ch = FindContainerHandle(f);
	if (ch == NULL)
		return fclose(f);

	result = ContainerHandle_Close(ch);
	ContainerHandle_Unregister(ch);
	OperatorDelete(ch);
	return result;
}

/*
 * 0x004E5F9A - fgets_ServerSide
 *
 * Reads up to maxLen-1 bytes into buf, stopping at newline or the
 * 0xFF sentinel. Walks the ContainerHandle page cache directly when
 * the file has one; otherwise falls through to fgets.
 */
char *
fgets_ServerSide(char *buf, int maxLen, FILE *f)
{
	ContainerHandle *ch;
	char *dst;
	int remaining;

	ch = FindContainerHandle(f);
	if (ch == NULL) {
		char *ret = fgets(buf, maxLen, f);
		// Collapse CRLF to LF like the page-cache path below; the
		// binary read these files in Windows text mode and never saw
		// the \r, so plain-stream reads must strip it too.
		if (ret != NULL) {
			size_t len = strlen(ret);
			if (len >= 2 && ret[len - 1] == '\n' && ret[len - 2] == '\r') {
				ret[len - 2] = '\n';
				ret[len - 1] = '\0';
			}
		}
		return ret;
	}

	dst = buf;
	remaining = maxLen;

	for (;;) {
		if (ch->pageCurOff >= ch->pageDataSize) {
			if (ch->pageFileOff + ch->pageCurOff > ch->logicalSize) {
				if (dst == buf)
					*dst = '\0';
				else
					*(dst - 1) = '\0';
				goto done;
			}
			ContainerHandle_AllocPage(ch, 0x1000);
		}

		*dst = ch->pageBuffer[ch->pageCurOff];
		ch->pageCurOff++;
		remaining--;

		if (remaining == 0) {
			*dst = '\0';
			ch->pageCurOff--;
			goto done;
		}

		if (*dst == '\n' || *dst == (char)0xFF) {
			// Collapse CRLF to LF. Data files ship with Windows
			// line endings and downstream %[^\n] scansets would
			// otherwise swallow the \r into the last field.
			if (*dst == '\n' && dst > buf && *(dst - 1) == '\r') {
				*(dst - 1) = '\n';
				*dst = '\0';
			} else {
				*(dst + 1) = '\0';
			}
			goto done;
		}

		dst++;
	}

done:
	if (ch->pageFileOff + ch->pageCurOff > ch->logicalSize)
		return NULL;
	return buf;
}

/*
 * 0x004E5FDA - feof_ServerSide
 *
 * Reports EOF on a ContainerHandle file by comparing the logical
 * position to logicalSize. Falls back to feof() for plain streams.
 */
int
feof_ServerSide(FILE *f)
{
	ContainerHandle *ch;

	ch = FindContainerHandle(f);
	if (ch == NULL)
		return feof(f);

	return (ch->pageFileOff + ch->pageCurOff > ch->logicalSize) ? 1 : 0;
}

/*
 * 0x004E600A - ftell_ServerSide
 *
 * Returns the ContainerHandle's logical position, or ftell(f) for
 * plain streams.
 */
long
ftell_ServerSide(FILE *f)
{
	ContainerHandle *ch;

	ch = FindContainerHandle(f);
	if (ch == NULL)
		return ftell(f);

	return (long)(ch->pageFileOff + ch->pageCurOff);
}

/*
 * 0x004E606A - fgetc_ServerSide
 *
 * Reads one byte from a ContainerHandle's page buffer, or from the
 * plain stream via fgetc.
 */
int
fgetc_ServerSide(FILE *f)
{
	ContainerHandle *ch;
	unsigned char byte;

	ch = FindContainerHandle(f);
	if (ch == NULL)
		return fgetc(f);

	if (ch->pageFileOff + ch->pageCurOff > ch->logicalSize)
		return -1;

	if (ch->pageCurOff >= ch->pageDataSize) {
		ContainerHandle_AllocPage(ch, 0x1000);
		if (ch->pageDataSize == 0) {
			ch->pageCurOff = 1;
			return -1;
		}
	}

	byte = ((unsigned char *)ch->pageBuffer)[ch->pageCurOff];
	ch->pageCurOff++;
	return (int)byte;
}
