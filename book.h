#ifndef BOOK_H_
#define BOOK_H_

/*
 * Book content system. 1024-entry g_BookTable loaded from books.idx/
 * books.mul; the demo ships with 27 pre-authored books at indices 1-27.
 * Read-only books (graphics 0xFEF/0xFF0) use item->amount as the
 * content index.
 */

#include <stdint.h>

__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CItem CItem;

#define BOOK_TABLE_SIZE         1024
#define BOOK_MAX_LINES_PER_PAGE 8
#define BOOK_MAX_PAGES          64

/*
 * One page of a CBookContent. Each line in lines is individually malloc'd.
 */
typedef struct BookPage {
	int32_t numLines;               // 0x00
#if __SIZEOF_POINTER__ == 8
	int32_t _pad64;                 // 64-bit alignment pad
#endif
	char **lines;                   // 0x04
} BookPage;

/*
 * Parsed book content (0x64 bytes in the binary). Holds an array of
 * numPages BookPage entries.
 */
typedef struct CBookContent {
	int32_t numPages;               // 0x00
#if __SIZEOF_POINTER__ == 8
	int32_t _pad64;                 // 64-bit alignment pad
#endif
	BookPage *pages;                // 0x04
	char title[60];                 // 0x08
	char author[30];                // 0x44
} CBookContent;

extern int g_BookWritable; // 0x0063E700
extern CBookContent *g_BookTable[BOOK_TABLE_SIZE];

const char *BookManager_GetTitle(int index); // 0x004344EB
void BookContent_loadAll(void); // 0x00434522
void HandlePacket_BOOKHDR(CPlayer *this, uint8_t *buf); // 0x00434AF5
void HandlePacket_BOOKPAGE(CPlayer *this, uint8_t *buf); // 0x00434C66
void PacketManager_MakePacket_BOOKPAGE(uint8_t *buf, CItem *item, int startPage, int count); // 0x00434E03
void SetBookTitle(CItem *item, const char *title); // 0x004355D3
void SetBookAuthor(CItem *item, const char *author, int authorId); // 0x0043564D
void SetBookPage(CItem *item, int pageNum, int lineCount, char **lines); // 0x004356EE
void BookContent_open(CItem *player, CItem *item); // 0x004357E6
void RemoveBookTag(CItem *item, const char *name); // 0x004358E4
void CopyBook(CItem *dst, CItem *src); // 0x0043599B

#endif /* BOOK_H_ */
