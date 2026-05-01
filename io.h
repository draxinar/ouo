#ifndef IO_H_
#define IO_H_

/*
 * ServerSide file I/O wrappers - binary: 0x004E5CFA..0x004E606A
 *
 * These wrappers check the ContainerHandle registry and route through
 * the page cache when applicable. Files opened with fopen_ServerSide
 * are managed through the FileManager/ContainerHandle subsystem.
 */

#ifndef IO_H
#define IO_H

#include <stdio.h>

#endif

FILE *fopen_ServerSide(const char *fileName, const char *mode); // 0x004E5CFA
int fread_ServerSide(void *buf, int elemSize, int elemCount, FILE *f); // 0x004E5E7A
int fwrite_ServerSide(const void *buf, int elemSize, int elemCount, FILE *f); // 0x004E5ECA
int fseek_ServerSide(FILE *f, long offset, int whence); // 0x004E5F1A
int fclose_ServerSide(FILE *f); // 0x004E5F5A
char *fgets_ServerSide(char *buf, int maxLen, FILE *f); // 0x004E5F9A
int feof_ServerSide(FILE *f); // 0x004E5FDA
long ftell_ServerSide(FILE *f); // 0x004E600A
int fgetc_ServerSide(FILE *f); // 0x004E606A

#endif /* IO_H_ */
