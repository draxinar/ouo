#ifndef STDLIST_H_
#define STDLIST_H_

#include <stdint.h>

/*
 * Generic MSVC STL container shape shared by std::list, std::vector, and
 * friends: allocator byte, sentinel _Head, _Size. The linker COMDAT-folds
 * their size() methods onto this layout.
 */
__extension__ typedef struct StdList StdList;
struct StdList {
	uint8_t allocator; // +0x00
	uint8_t _pad[3];   // +0x01
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4]; // 64-bit alignment pad
#endif
	void *head;        // +0x04 (_Head sentinel)
	int size;          // +0x08 (_Size)
};

#endif /* STDLIST_H_ */
