/*
 * Book content - pre-authored book pages loaded from data files.
 *
 * Parses books.idx / books.mul into a fixed table of CBookContent
 * entries keyed by serial, and services the client packets that read
 * book title, author, and page text.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "book.h"
#include "filemanager.h"
#include "io.h"
#include "packet_handler.h"
#include "packet_utils.h"
#include "player.h"
#include "region.h"
#include "taglist.h"
#include "usersock.h"
#include "utils.h"
#include "vtable.h"
#include "wombat.h"
#include "wombat_compile.h"
#include "world.h"

static CBookContent *CBookContent_parse(CBookContent *book, uint8_t *data); // 0x00434240
static void CBookContent_Cleanup(CBookContent *book); // 0x004343FC
static char **CBookContent_GetPage(CBookContent *book, int pageNum, int *outLineCount); // 0x004344BF
static uint8_t *MakePacket_BOOKHDR(CItem *item, int *outSize); // 0x0043473D
static void PacketManager_MakePacket_BOOKHEADER(uint8_t *buf, CItem *item, CItem *ownerEntity); // 0x0043509A
static const char *GetBookTitle(CItem *item); // 0x004353E8
static const char *GetBookAuthor(CItem *item); // 0x0043542A
static int GetBookAuthorId(CItem *item); // 0x0043546C
static int GetBookPageCount(CItem *item); // 0x004354A7
static char **GetBookPageText(CItem *item, int pageNum, int *outLineCount); // 0x004354FA
static void ClearBookObjVars(CItem *item); // 0x004358F5
static CBookContent *CBookContent_Destructor(CBookContent *book, int freeMemory); // 0x00435AB0

CBookContent *g_BookTable[BOOK_TABLE_SIZE];
int g_BookWritable; // 0x0063E700

/*
 * 0x00434240 - CBookContent::parse
 *
 * Parses one books.mul record into a pre-allocated CBookContent.
 */
static CBookContent *
CBookContent_parse(CBookContent *book, uint8_t *data)
{
	int i, j;
	int lineLen;

	memcpy(&book->numPages, data, 4);
	SwapEndian(&book->numPages);
	memcpy(book->title, data + 4, 60);
	memcpy(book->author, data + 0x40, 30);
	data += 0x5e;

	if (book->numPages != 0)
		book->pages = (BookPage *)malloc(book->numPages * sizeof(BookPage));
	else
		book->pages = NULL;

	for (i = 0; i < book->numPages; i++) {
		memcpy(&book->pages[i].numLines, data, 4);
		SwapEndian(&book->pages[i].numLines);
		data += 4;

		if (book->pages[i].numLines != 0)
			book->pages[i].lines = (char **)malloc(book->pages[i].numLines * sizeof(char *));
		else
			book->pages[i].lines = NULL;

		for (j = 0; j < book->pages[i].numLines; j++) {
			lineLen = *(signed char *)data;
			data++;
			book->pages[i].lines[j] = (char *)malloc(lineLen);
			memcpy(book->pages[i].lines[j], data, lineLen);
			data += lineLen;
		}
	}

	return book;
}

/*
 * 0x004343FC - CBookContent::Cleanup
 *
 * Frees every line, the per-page line array, and the page array.
 */
static void
CBookContent_Cleanup(CBookContent *book)
{
	int i, j;

	if (book->pages == NULL)
		return;

	for (i = 0; i < book->numPages; i++) {
		if (book->pages[i].lines == NULL)
			continue;
		for (j = 0; j < book->pages[i].numLines; j++)
			OperatorDelete(book->pages[i].lines[j]);
		OperatorDelete(book->pages[i].lines);
	}
	OperatorDelete(book->pages);
}

/*
 * 0x004344BF - CBookContent::GetPage
 *
 * Returns the line array for 1-based pageNum and sets *outLineCount.
 */
static char **
CBookContent_GetPage(CBookContent *book, int pageNum, int *outLineCount)
{
	*outLineCount = book->pages[pageNum - 1].numLines;
	return book->pages[pageNum - 1].lines;
}

/*
 * 0x004344EB - BookManager_GetTitle
 *
 * Returns g_BookTable[index]->title, or "A Buggy Book" if the slot is
 * invalid or empty.
 */
const char *
BookManager_GetTitle(int index)
{
	CBookContent *entry;

	if (index < 0 || index >= BOOK_TABLE_SIZE)
		return "A Buggy Book";
	entry = g_BookTable[index];
	if (entry == NULL)
		return "A Buggy Book";
	return entry->title;
}

/*
 * 0x00434522 - BookContent_loadAll
 *
 * Clears and repopulates g_BookTable from books.idx (0x34) and books.mul
 * (0x35). Entries with offset 0xFFFFFFFF stay NULL.
 */
void
BookContent_loadAll(void)
{
	FILE *fidx, *fmul;
	uint32_t i;
	uint32_t offset, length, extra;
	uint8_t *data;
	CBookContent *book;

	for (i = 0; i < BOOK_TABLE_SIZE; i++) {
		if (g_BookTable[i] != NULL) {
			CBookContent_Destructor(g_BookTable[i], 1);
			g_BookTable[i] = NULL;
		}
	}

	fidx = FileManager_OpenByType(0x34, NULL, "rb");
	if (fidx == NULL)
		return;
	fmul = FileManager_OpenByType(0x35, NULL, "rb");
	if (fmul == NULL) {
		fclose_ServerSide(fidx);
		return;
	}

	for (i = 0; i < BOOK_TABLE_SIZE; i++) {
		fread_ServerSide(&offset, 4, 1, fidx);
		SwapEndian(&offset);
		fread_ServerSide(&length, 4, 1, fidx);
		SwapEndian(&length);
		fread_ServerSide(&extra, 4, 1, fidx);
		SwapEndian(&extra);

		if (offset == 0xFFFFFFFF)
			continue;

		fseek_ServerSide(fmul, offset, 0);
		data = (uint8_t *)malloc(length);
		fread_ServerSide(data, length, 1, fmul);

		book = (CBookContent *)malloc(sizeof(CBookContent));
		if (book != NULL)
			CBookContent_parse(book, data);
		g_BookTable[i] = book;

		free(data);
	}

	fclose_ServerSide(fidx);
	fclose_ServerSide(fmul);
}

/*
 * 0x0043473D - MakePacket_BOOKHDR
 *
 * Serializes a writable book's header and pages into an allocated
 * 0x1F400 buffer; caller frees.
 */
static __attribute__((unused)) uint8_t *
MakePacket_BOOKHDR(CItem *item, int *outSize)
{
	uint8_t *buf;
	uint8_t *cursor;
	char title[60];
	char author[30];
	int pageCount;
	int tmp;
	int i;
	int lineCount;
	char **lines;
	int j;
	uint8_t lineLen;

	buf = (uint8_t *)malloc(0x1F400);
	cursor = buf;

	memset(title, 0, sizeof(title));
	memset(author, 0, sizeof(author));

	pageCount = GetBookPageCount(item);

	tmp = pageCount;
	SwapEndian(&tmp);
	memcpy(cursor, &tmp, 4);
	cursor += 4;

	strncpy(title, GetBookTitle(item), 60);
	title[59] = '\0';
	memcpy(cursor, title, 60);
	cursor += 60;

	strncpy(author, GetBookAuthor(item), 30);
	author[29] = '\0';
	memcpy(cursor, author, 30);
	cursor += 30;

	for (i = 0; i < pageCount; i++) {
		lines = GetBookPageText(item, i + 1, &lineCount);

		tmp = lineCount;
		SwapEndian(&tmp);
		memcpy(cursor, &tmp, 4);
		cursor += 4;

		for (j = 0; j < lineCount; j++) {
			lineLen = (uint8_t)(strlen(lines[j]) + 1);
			*cursor = lineLen;
			cursor++;
			memcpy(cursor, lines[j], (int)(int8_t)lineLen);
			cursor += (int)(int8_t)lineLen;
		}
	}

	*outSize = (int)(cursor - buf);
	return buf;
}

/*
 * 0x0043491A - BookContent_saveOne
 *
 * Writes a book's serialised header and pages to books.mul and records
 * the offset and length at slot bookIndex of books.idx, then reloads
 * every book. Creates both files when books.idx is missing, seeding the
 * index with 1024 empty slots.
 *
 * The index entry's third word is written as -1 after SwapEndian, and
 * the length is byte-swapped in place before the write, so the slot
 * holds a big-endian offset and length. Nothing checks either fopen.
 */
static __attribute__((unused)) void
BookContent_saveOne(CItem *item, int bookIndex)
{
	FILE *idx;
	FILE *mul;
	int offset;
	int size;
	uint8_t *packet;
	int i;

	idx = fopen_ServerSide("../.rundir/books.idx", "r+b");
	if (idx == NULL) {
		idx = fopen_ServerSide("../.rundir/books.idx", "wb");
		mul = fopen_ServerSide("../.rundir/books.mul", "wb");

		for (i = 0; i < 0x400; i++) {
			offset = -1;
			SwapEndian(&offset);
			fwrite_ServerSide(&offset, 4, 1, idx);
			offset = 0;
			SwapEndian(&offset);
			fwrite_ServerSide(&offset, 4, 1, idx);
			fwrite_ServerSide(&offset, 4, 1, idx);
		}
	} else {
		mul = fopen_ServerSide("../.rundir/books.mul", "r+b");
	}

	fseek_ServerSide(idx, bookIndex * 0xC, SEEK_SET);
	fseek_ServerSide(mul, 0, SEEK_END);

	offset = (int)ftell_ServerSide(mul);
	SwapEndian(&offset);

	packet = MakePacket_BOOKHDR(item, &size);
	fwrite_ServerSide(packet, size, 1, mul);
	OperatorDelete(packet);

	SwapEndian(&size);
	fwrite_ServerSide(&offset, 4, 1, idx);
	fwrite_ServerSide(&size, 4, 1, idx);

	offset = -1;
	SwapEndian(&offset);
	fwrite_ServerSide(&offset, 4, 1, idx);

	fclose_ServerSide(idx);
	fclose_ServerSide(mul);

	BookContent_loadAll();
}

/*
 * 0x00434AF5 - HandlePacket_BOOKHDR
 *
 * Stores a writable book's submitted title, author, and lookAtText.
 * "bookTitle" keeps the raw title; "lookAtText" gets the leading-space-
 * stripped title (or "a book" if empty).
 *
 * MODIFIED: 1.26+ clients send a 99-byte packet with an extra flag byte
 * after the writable flag; skip it so remaining fields parse correctly.
 */
void
HandlePacket_BOOKHDR(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint8_t flag;
	uint16_t pageCount;
	char *title;
	char *author;
	CItem *entity;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetByte(buf, &off, &flag);
	// MODIFIED: 1.26+ has an extra flag byte here.
	if (this->usersock && this->usersock->packetTable[5 * PacketType_BOOKHDR] == 99) {
		uint8_t flag2;
		GetByte(buf, &off, &flag2);
	}
	GetWord(buf, &off, &pageCount);
	GetString(buf, &off, &title, 60);
	GetString(buf, &off, &author, 30);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 0x12);
	if (entity == NULL || !CItem_IsWritableBook(entity))
		return;

	CItem_GetBookNum(entity);

	SetBookTitle(entity, title);

	SetBookAuthor(entity, author, this->mobile.container.item.serial);

	while (isspace((unsigned char)*title))
		title++;
	if (*title == '\0')
		title = "a book";

	{
		CString _v, _n;
		CString_Constructor(&_v, title);
		CString_Constructor(&_n, "lookAtText");
		ObjVar_SetStr(entity, &_n, 1, (uintptr_t)&_v);
		CString_Destructor(&_v);
	}

	USED(flag);
	USED(pageCount);
}

/*
 * 0x00434C66 - HandlePacket_BOOKPAGE
 *
 * Dual-purpose. lineCount == 0xFFFF reads a page (server sends it back);
 * otherwise the client is submitting page content for a writable book.
 */
void
HandlePacket_BOOKPAGE(CPlayer *this, uint8_t *buf)
{
	uint32_t off;
	uint32_t serial;
	uint16_t pageCount, pageNumber, lineCount;
	CItem *entity;
	uint16_t bookNum;
	uint8_t buf_out[0x8000];
	char *lines[128];
	int i;

	off = 0;
	GetDWord(buf, &off, &serial);
	GetWord(buf, &off, &pageCount);
	GetWord(buf, &off, &pageNumber);
	GetWord(buf, &off, &lineCount);

	entity = CWorld_FindEntityInRange(g_World, &this->mobile.container.item.resourceEntity.entity, serial, 0x12);
	if (entity == NULL)
		return;

	if ((lineCount & 0xFFFF) == 0xFFFF) {
		if (entity != NULL) {
			bookNum = (uint16_t)CItem_GetBookNum(entity);
			PacketManager_MakePacket_BOOKPAGE(buf_out, entity, pageNumber & 0xFFFF, 1);
			SendToClient((CItem *)this, buf_out, -1);
		}
		return;
	}

	if (!CItem_IsWritableBook(entity))
		return;
	if (entity == NULL)
		return;
	if ((lineCount & 0xFFFF) >= 0x80)
		return;

	for (i = 0; i < (int)(lineCount & 0xFFFF); i++) {
		GetNullTermString(buf, &off, &lines[i]);
	}

	SetBookPage(entity, pageNumber & 0xFFFF, lineCount & 0xFFFF, lines);

	USED(pageCount);
	USED(bookNum);
}

/*
 * 0x00434E03 - PacketManager::MakePacket_BOOKPAGE
 *
 * Builds a BOOKPAGE (0x66) response with count consecutive pages starting
 * at startPage. Writable books pull text from ObjVars; read-only books
 * look up g_BookTable.
 */
void
PacketManager_MakePacket_BOOKPAGE(uint8_t *buf, CItem *item, int startPage, int count)
{
	CBookContent *book;
	uint16_t bookIndex;
	uint32_t serial;
	uint16_t ln;
	int i;
	int pageCount;

	serial = item->serial;

	if (CItem_IsWritableBook(item)) {
		PutPacketType(buf, PacketType_BOOKPAGE, 0x8000);
		PutDWord(buf, serial);

		pageCount = GetBookPageCount(item);
		if (startPage < 1 || startPage > pageCount) {
			startPage = 1;
			count = 0;
		}
		if (startPage + count > pageCount)
			count = pageCount - startPage + 1;

		PutWord(buf, (uint16_t)pageCount);

		for (i = startPage; i < startPage + count; i++) {
			int lineCount = 0;
			char **lines;

			lines = GetBookPageText(item, i, &lineCount);
			PutWord(buf, (uint16_t)i);
			PutWord(buf, (uint16_t)lineCount);

			for (ln = 0; ln < lineCount; ln++)
				PutString(buf, lines[ln], (int)strlen(lines[ln]) + 1);
		}
		return;
	}

	PutPacketType(buf, PacketType_BOOKPAGE, 0x8000);
	PutDWord(buf, serial);

	book = NULL;
	bookIndex = (uint16_t)CItem_GetBookNum(item);
	if (bookIndex < BOOK_TABLE_SIZE)
		book = g_BookTable[bookIndex];

	if (book == NULL) {
		PutWord(buf, 0);
		return;
	}

	if (startPage < 1 || startPage > book->numPages) {
		startPage = 1;
		count = 0;
	}
	if (startPage + count > book->numPages)
		count = book->numPages - startPage + 1;

	PutWord(buf, (uint16_t)count);

	for (i = startPage; i < startPage + count; i++) {
		int lineCount = 0;
		char **lines;

		lines = CBookContent_GetPage(book, i, &lineCount);
		PutWord(buf, (uint16_t)i);
		PutWord(buf, (uint16_t)lineCount);

		for (ln = 0; ln < lineCount; ln++)
			PutString(buf, lines[ln], (int)strlen(lines[ln]) + 1);
	}
}

/*
 * 0x0043509A - PacketManager::MakePacket_BOOKHEADER
 *
 * Builds the BOOKHDR packet and sets g_BookWritable so BookContent_open
 * knows whether to stream pages. A fresh writable book (bookStatus==0)
 * gets title/author/pages ObjVars initialized from the owner.
 *
 * MODIFIED: 1.26+ clients use a 99-byte packet with an extra flag byte
 * after the writable flag; pick 0x63 vs 0x62 based on the client packet
 * table.
 */
static void
PacketManager_MakePacket_BOOKHEADER(uint8_t *buf, CItem *item, CItem *ownerEntity)
{
	CBookContent *book;
	uint16_t bookNum;
	char titleBuf[60];
	char authorBuf[30];
	int bookStatus;
	int pageCount;

	if (CItem_IsWritableBook(item)) {
		if (((CPlayer *)ownerEntity)->usersock->packetTable[5 * PacketType_BOOKHDR] == 99)
			PutPacketType(buf, PacketType_BOOKHDR, 0x63);
		else
			PutPacketType(buf, PacketType_BOOKHDR, 0x62);
		PutDWord(buf, item->serial);

		bookStatus = CItem_GetBookStatus(item) & 0xFF;
		if (bookStatus == 0) {
			CItem_SetBookStatus(item, 1);
			pageCount = CItem_GetBookPages(item) & 0xFFFF;
			{
				CString _bp;
				CString_Constructor(&_bp, "bookPages");
				ObjVar_SetStr(item, &_bp, 0, (uint32_t)pageCount);
			}
			SetBookTitle(item, "a book");
			SetBookAuthor(item, ((char *(*)(void *))VT_FN(ownerEntity, VT_GET_NAME))(ownerEntity), ownerEntity->serial);
			{
				CString _v, _n;
				CString_Constructor(&_v, "a book");
				CString_Constructor(&_n, "lookAtText");
				ObjVar_SetStr(item, &_n, 1, (uintptr_t)&_v);
				CString_Destructor(&_v);
			}
		}

		bookStatus = CItem_GetBookStatus(item) & 0xFF;
		if (bookStatus == 1) {
			g_BookWritable = 1;
			PutByte(buf, 1);
		} else {
			g_BookWritable = 0;
			PutByte(buf, 0);
		}
		if (((CPlayer *)ownerEntity)->usersock->packetTable[5 * PacketType_BOOKHDR] == 99)
			PutByte(buf, 1);

		pageCount = GetBookPageCount(item);
		PutWord(buf, (uint16_t)pageCount);

		memset(titleBuf, 0, sizeof(titleBuf));
		strncpy(titleBuf, GetBookTitle(item), sizeof(titleBuf));
		titleBuf[59] = '\0';
		PutString(buf, titleBuf, 60);

		memset(authorBuf, 0, sizeof(authorBuf));
		strncpy(authorBuf, GetBookAuthor(item), sizeof(authorBuf));
		authorBuf[29] = '\0';
		PutString(buf, authorBuf, 30);
	} else {
		if (((CPlayer *)ownerEntity)->usersock->packetTable[5 * PacketType_BOOKHDR] == 99)
			PutPacketType(buf, PacketType_BOOKHDR, 0x63);
		else
			PutPacketType(buf, PacketType_BOOKHDR, 0x62);
		PutDWord(buf, item->serial);
		PutByte(buf, 0);
		if (((CPlayer *)ownerEntity)->usersock->packetTable[5 * PacketType_BOOKHDR] == 99)
			PutByte(buf, 0);
		g_BookWritable = 0;

		book = NULL;
		bookNum = (uint16_t)CItem_GetBookNum(item);
		if (bookNum < BOOK_TABLE_SIZE)
			book = g_BookTable[bookNum];

		if (book != NULL) {
			PutWord(buf, (uint16_t)book->numPages);
			PutString(buf, book->title, 60);
			PutString(buf, book->author, 30);
		} else {
			PutWord(buf, 0);
			memset(titleBuf, 0, sizeof(titleBuf));
			strcpy(titleBuf, "A Buggy Book");
			PutString(buf, titleBuf, 60);
			memset(authorBuf, 0, sizeof(authorBuf));
			PutString(buf, authorBuf, 30);
		}
	}
}

/*
 * 0x004353E8 - GetBookTitle
 *
 * Returns the "bookTitle" tag, or "a book" if unset.
 */
static const char *
GetBookTitle(CItem *item)
{
	CString *title;

	CItem_IsWritableBook(item);
	if (CResourceEntity_HasTag(item, "bookTitle", 1)) {
		title = CResourceEntity_GetTagString(item, "bookTitle");
		return CString_GetData(title);
	}
	return "a book";
}

/*
 * 0x0043542A - GetBookAuthor
 *
 * Returns the "bookAuthor" tag, or "Flobbitz the Llamaherder" if unset.
 */
static const char *
GetBookAuthor(CItem *item)
{
	CString *author;

	CItem_IsWritableBook(item);
	if (CResourceEntity_HasTag(item, "bookAuthor", 1)) {
		author = CResourceEntity_GetTagString(item, "bookAuthor");
		return CString_GetData(author);
	}
	return "Flobbitz the Llamaherder";
}

/*
 * 0x0043546C - GetBookAuthorId
 *
 * Returns the "authorId" tag (type 4), or 0 if unset.
 */
static int
GetBookAuthorId(CItem *item)
{
	int val;

	CItem_IsWritableBook(item);
	if (CResourceEntity_HasTag(item, "authorId", 4)) {
		CResourceEntity_GetTagObj(item, "authorId", (uint32_t *)&val);
		return val;
	}
	return 0;
}

/*
 * 0x004354A7 - GetBookPageCount
 *
 * Returns the "bookPages" tag, or falls back to the tiledata miscData.
 */
static int
GetBookPageCount(CItem *item)
{
	int pages;

	CItem_IsWritableBook(item);
	if (CResourceEntity_HasTag(item, "bookPages", 0)) {
		CResourceEntity_GetTagInt(item, "bookPages", &pages);
		return pages;
	}
	return (int)g_ItemTileData[CEntity_GetBodyType(item) & 0xFFFF].miscData;
}

/*
 * 0x004354FA - GetBookPageText
 *
 * Returns a pointer to a static array of line pointers for pageNum, or
 * NULL if the page is absent.
 *
 * MODIFIED: the binary stores pages as a WombatList tag; we store them
 * as a newline-separated string and split on demand.
 */
// Custom - line pointers into g_BookPageBuf for the current page
static char *g_BookPageLines[BOOK_MAX_LINES_PER_PAGE];
// Custom - scratch buffer holding the split page text
static char g_BookPageBuf[4096];

static char **
GetBookPageText(CItem *item, int pageNum, int *outLineCount)
{
	const char *pageStr;
	char pageName[20];
	int lineCount;
	const char *p;
	int i;

	CItem_IsWritableBook(item);
	if (pageNum < 1 || pageNum > GetBookPageCount(item)) {
		*outLineCount = 0;
		return NULL;
	}
	sprintf(pageName, "bookPage%02d", pageNum);
	if (!(item->tagList != NULL && TagList_HasTag(item->tagList, pageName, 7))) {
		*outLineCount = 0;
		return NULL;
	}
	{
		CString *pageTag = (item->tagList != NULL) ? TagList_GetTagString(item->tagList, pageName) : NULL;
		pageStr = (pageTag != NULL) ? CString_GetData(pageTag) : NULL;
	}
	if (pageStr == NULL || *pageStr == '\0') {
		*outLineCount = 0;
		return NULL;
	}
	strncpy(g_BookPageBuf, pageStr, sizeof(g_BookPageBuf) - 1);
	g_BookPageBuf[sizeof(g_BookPageBuf) - 1] = '\0';
	lineCount = 0;
	p = g_BookPageBuf;
	for (i = 0; i < BOOK_MAX_LINES_PER_PAGE; i++)
		g_BookPageLines[i] = "";
	p = g_BookPageBuf;
	for (i = 0; i < BOOK_MAX_LINES_PER_PAGE && *p != '\0'; i++) {
		g_BookPageLines[i] = (char *)p;
		while (*p != '\0' && *p != '\n')
			p++;
		if (*p == '\n') {
			*(char *)p = '\0';
			p++;
		}
		lineCount++;
	}
	*outLineCount = lineCount;
	return g_BookPageLines;
}

/*
 * 0x004355D3 - SetBookTitle
 *
 * Stores the book title in the "bookTitle" objvar.
 */
void
SetBookTitle(CItem *item, const char *title)
{
	CString valStr, nameStr;

	CItem_IsWritableBook(item);
	CString_Constructor(&valStr, title);
	CString_Constructor(&nameStr, "bookTitle");
	ObjVar_SetStr(item, &nameStr, 1, (uintptr_t)&valStr);
	CString_Destructor(&valStr);
}

/*
 * 0x0043564D - SetBookAuthor
 *
 * Stores the book author name and author id in objvars.
 */
void
SetBookAuthor(CItem *item, const char *author, int authorId)
{
	CString valStr, nameStr, nameStr2;

	CItem_IsWritableBook(item);
	CString_Constructor(&valStr, author);
	CString_Constructor(&nameStr, "bookAuthor");
	ObjVar_SetStr(item, &nameStr, 1, (uintptr_t)&valStr);
	CString_Constructor(&nameStr2, "authorId");
	ObjVar_SetStr(item, &nameStr2, 4, (uint32_t)authorId);
	CString_Destructor(&valStr);
}

/*
 * 0x004356EE - SetBookPage
 *
 * Stores the lines for pageNum.
 *
 * MODIFIED: the binary stores pages as a WombatList tag; we store them as
 * a newline-separated string.
 */
void
SetBookPage(CItem *item, int pageNum, int lineCount, char **lines)
{
	char pageName[20];
	char pageBuf[4096];
	int pos;
	int i;

	CItem_IsWritableBook(item);
	if (pageNum < 1 || pageNum > GetBookPageCount(item))
		return;
	sprintf(pageName, "bookPage%02d", pageNum);
	pageBuf[0] = '\0';
	pos = 0;
	for (i = 0; i < lineCount; i++) {
		int len;
		if (lines[i] == NULL)
			continue;
		len = (int)strlen(lines[i]);
		if (pos + len + 1 < (int)sizeof(pageBuf)) {
			if (pos > 0)
				pageBuf[pos++] = '\n';
			memcpy(pageBuf + pos, lines[i], len);
			pos += len;
		}
	}
	pageBuf[pos] = '\0';
	{
		CString valStr, nameStr;
		CString_Constructor(&valStr, pageBuf);
		CString_Constructor(&nameStr, pageName);
		ObjVar_SetStr(item, &nameStr, 1, (uintptr_t)&valStr);
		CString_Destructor(&valStr);
	}
}

/*
 * 0x004357E6 - BookContent_open
 *
 * Sends the book header to player and, for writable books, eagerly sends
 * all pages in batches of 50. Read-only books rely on the client
 * requesting pages individually via HandlePacket_BOOKPAGE.
 */
void
BookContent_open(CItem *player, CItem *item)
{
	uint8_t hdr[100];
	uint8_t pagesBuf[0x8000];
	uint16_t totalPages;
	int startPage;
	int count;

	PacketManager_MakePacket_BOOKHEADER(hdr, item, player);
	SendToClient(player, hdr, -1);

	if (g_BookWritable != 0) {
		CItem_GetBookNum(item); // binary reads this but never uses result
		totalPages = (uint16_t)GetBookPageCount(item);
		startPage = 1;
		while (startPage <= (int)(totalPages & 0xFFFF)) {
			count = (int)(totalPages & 0xFFFF) - startPage + 1;
			if (count > 50)
				count = 50;
			PacketManager_MakePacket_BOOKPAGE(pagesBuf, item, startPage, count);
			SendToClient(player, pagesBuf, -1);
			startPage += 50;
		}
	}
}

/*
 * 0x004358E4 - RemoveBookTag
 *
 * Cdecl wrapper around CResourceEntity::DetachScript.
 */
void
RemoveBookTag(CItem *item, const char *name)
{
	CResourceEntity_DetachScript(item, name);
}

/*
 * 0x004358F5 - ClearBookObjVars
 *
 * Removes all book-related tags from item.
 */
static void
ClearBookObjVars(CItem *item)
{
	int pageCount;
	int i;
	char pageName[20];

	CItem_IsWritableBook(item);
	pageCount = GetBookPageCount(item);
	RemoveBookTag(item, "bookTitle");
	RemoveBookTag(item, "bookAuthor");
	RemoveBookTag(item, "bookPages");
	RemoveBookTag(item, "authorId");
	for (i = 1; i <= pageCount; i++) {
		sprintf(pageName, "bookPage%02d", i);
		RemoveBookTag(item, pageName);
	}
}

/*
 * 0x0043599B - CopyBook
 *
 * Copies book content from src into dst. The binary reads title, author,
 * and pages from the second argument and writes them to the first, so the
 * first argument is the destination. The GetBookPages/SetBookPages pair
 * near the top reads the current page count from dst and stores it back
 * on src; that write is overwritten immediately after by the bookPages
 * ObjVar store on dst, matching the binary exactly.
 */
void
CopyBook(CItem *dst, CItem *src)
{
	int pageCount;
	int lineCount;
	char **lines;
	const char *title;
	const char *author;
	int authorId;
	int i;

	if (CItem_IsWritableBook(dst))
		CItem_IsWritableBook(src);

	ClearBookObjVars(dst);

	pageCount = CItem_GetBookPages(dst);
	CItem_SetBookPages(src, pageCount);

	title = GetBookTitle(src);
	SetBookTitle(dst, title);

	authorId = GetBookAuthorId(src);
	author = GetBookAuthor(src);
	SetBookAuthor(dst, author, authorId);

	pageCount = GetBookPageCount(src);

	{
		CString nameStr;
		CString_Constructor(&nameStr, "bookPages");
		ObjVar_SetStr(dst, &nameStr, 0, (uint32_t)pageCount);
	}

	for (i = 1; i <= pageCount; i++) {
		char pageName[20];
		sprintf(pageName, "bookPage%02d", i); // dead code in binary
		lines = GetBookPageText(src, i, &lineCount);
		SetBookPage(dst, i, lineCount, lines);
	}
}

/*
 * 0x00435AB0 - CBookContent::~CBookContent
 *
 * Destroys a CBookContent: releases its pages and optionally frees the struct.
 */
static CBookContent *
CBookContent_Destructor(CBookContent *book, int freeMemory)
{
	CBookContent_Cleanup(book);
	if (freeMemory & 1)
		free(book);
	return NULL;
}
