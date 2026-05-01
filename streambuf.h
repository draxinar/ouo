#ifndef STREAMBUF_H_
#define STREAMBUF_H_

#include <stdint.h>

/*
 * CStreamBuf vtable slot indices (byte offset / 4). Vtable lives at
 * 0x005ee050: dtor, overflow, pbackfail, showmanyc, underflow, uflow,
 * xsgetn, xsputn, seekoff, seekpos, setbuf, sync.
 */
#define SB_VT_DTOR      0  // 0x00
#define SB_VT_OVERFLOW  1  // 0x04
#define SB_VT_PBACKFAIL 2  // 0x08
#define SB_VT_SHOWMANYC 3  // 0x0C
#define SB_VT_UNDERFLOW 4  // 0x10
#define SB_VT_UFLOW     5  // 0x14
#define SB_VT_XSGETN    6  // 0x18
#define SB_VT_XSPUTN    7  // 0x1C
#define SB_VT_SEEKOFF   8  // 0x20
#define SB_VT_SEEKPOS   9  // 0x24
#define SB_VT_SETBUF    10  // 0x28
#define SB_VT_SYNC      11  // 0x2C

/*
 * MSVC 5.0 std::basic_streambuf<char> (56 bytes, vtable 0x005ee050), used
 * by the Wombat scripting engine for bytecode I/O. Each get/put pointer
 * exists in both direct (char*) and indirect (char**) form; the indirect
 * slots point at the direct storage by default and can be rerouted by
 * derived classes like filebuf.
 */
__extension__ typedef struct CStreamBuf CStreamBuf;
struct CStreamBuf {
	void **vtable;      // +0x00
	char *_Gfirst;      // +0x04 (eback storage)
	char *_Gnext;       // +0x08 (gptr storage)
	char **_IGfirst;    // +0x0C
	intptr_t _Gcount;   // +0x10 (int in binary; widened here because InitPtrs stores a pointer)
	char *_Pfirst;      // +0x14 (pbase storage)
	char *_Pnext;       // +0x18 (pptr storage)
	char **_IGnext;     // +0x1C
	char **_IPnext;     // +0x20
	intptr_t _Pcount;   // +0x24 (int in binary; widened to match _IGcount writes)
	char **_IPfirst;    // +0x28
	intptr_t *_IGcount; // +0x2C (int* in binary; widened to pointer-sized count)
	intptr_t *_IPcount; // +0x30 (int* in binary; widened to pointer-sized count)
	void *_Ploc;        // +0x34
};

/*
 * MSVC 5.0 std::fpos<mbstate_t> (24 bytes), passed by value on the stack.
 */
__extension__ typedef struct CStreamPos CStreamPos;
struct CStreamPos {
	int32_t _Off;    // +0x00 (stream offset)
	int32_t _Fpos;   // +0x04 (file position)
	int32_t _Off2;   // +0x08 (mbstate 0)
	int32_t _State;  // +0x0C (mbstate 1)
	int32_t _State2; // +0x10 (mbstate 2 / facet ptr)
	int32_t _State3; // +0x14 (mbstate 3)
};

/*
 * MSVC 5.0 basic_filebuf<char> (84 bytes). Extends CStreamBuf with a
 * locale facet, stream-position scratch, and the underlying FILE *.
 * Constructor CStdioFile_ConstructorAlloc (0x00403370), destructor
 * CSdbStr_VectorDtor (0x00401C20).
 */
__extension__ typedef struct CStdioFileBuf CStdioFileBuf;
struct CStdioFileBuf {
	CStreamBuf base;   // +0x00
	void *facet;       // +0x38
	int32_t posField1; // +0x3C
	int32_t posField2; // +0x40
	void *allocPtr;    // +0x44
	uint8_t openFlag;  // +0x48
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[7]; // 64-bit alignment pad
#endif
	void *localeFacet; // +0x4C
	void *fileBuf;     // +0x50 (FILE *)
};

int32_t CStreamPos_GetState2(CStreamPos *pos); // 0x00402FA0
int64_t CStreamPos_GetState(CStreamPos *pos); // 0x00402FC0
int32_t CStreamPos_GetEndOffset(CStreamPos *pos); // 0x00402FE0
int CStreamBuf_pubsync(CStreamBuf *sb); // 0x00403000
int CStreamBuf_sputc(CStreamBuf *sb, char c); // 0x00403020
char *CStreamBuf_eback(CStreamBuf *sb); // 0x004030A0
char *CStreamBuf_gptr(CStreamBuf *sb); // 0x004030C0
char *CStreamBuf_pptr(CStreamBuf *sb); // 0x004030E0
char *CStreamBuf_egptr(CStreamBuf *sb); // 0x00403100
void CStreamBuf_gbump(CStreamBuf *sb, int n); // 0x00403120
char *CStreamBuf_epptr(CStreamBuf *sb); // 0x00403160
char *CStreamBuf_Gndec(CStreamBuf *sb); // 0x00403180
char *CStreamBuf_Gninc(CStreamBuf *sb); // 0x004031C0
void CStreamBuf_pbump(CStreamBuf *sb, int n); // 0x00403200
char *CStreamBuf_Pninc(CStreamBuf *sb); // 0x00403240
void CStreamBuf_Constructor(CStreamBuf *this, void *streambuf, char initFlag); // 0x004039A0
void CStreamBuf_InitPtrs(CStreamBuf *this); // 0x00404150
void CStreamBuf_SetAll(CStreamBuf *this, uintptr_t igFirst, uintptr_t igNext, uintptr_t igCount, uintptr_t gCount, uintptr_t ipNext, uintptr_t ipCount); // 0x004041C0

#endif /* STREAMBUF_H_ */
