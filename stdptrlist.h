#ifndef STDPTRLIST_H_
#define STDPTRLIST_H_

#include <stdint.h>

/*
 * MSVC std::list<void*> node (12 bytes), allocated via operator new inside
 * list::insert.
 */
__extension__ typedef struct StdPtrNode StdPtrNode;
struct StdPtrNode {
	StdPtrNode *next; // +0x00 (_Next)
	StdPtrNode *prev; // +0x04 (_Prev)
	void *value;      // +0x08 (_Myval)
};

/*
 * MSVC std::list<void*> header (12 bytes): allocator byte, sentinel _Head,
 * _Size.
 */
__extension__ typedef struct StdPtrList StdPtrList;
struct StdPtrList {
	uint8_t allocator; // +0x00
	uint8_t _pad[3];   // +0x01
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4]; // 64-bit alignment pad
#endif
	StdPtrNode *head;  // +0x04 (_Head sentinel)
	uint32_t size;     // +0x08 (_Size)
};

/*
 * Full std::list<void*>::iterator (8 bytes on 32-bit), used by the orphaned
 * StdPtrIter_Begin/Next/Prev/IsValid helpers.
 */
__extension__ typedef struct StdPtrIterFull StdPtrIterFull;
struct StdPtrIterFull {
	StdPtrNode *sentinel; // +0x00
	StdPtrNode *current;  // +0x04
};

__extension__ typedef struct CString CString;

#endif /* STDPTRLIST_H_ */
