#ifndef SERIAL_LIST_H_
#define SERIAL_LIST_H_

#include <stdint.h>

__extension__ typedef struct CLocation CLocation;
__extension__ typedef struct CSearchCtx CSearchCtx;
__extension__ typedef struct StdPtrList StdPtrList;
__extension__ typedef struct StdPtrNode StdPtrNode;
/*
 * Payload of a CSerialList node: {serial, flags} (6 bytes).
 */
__extension__ typedef struct CSerialValue CSerialValue;
struct CSerialValue {
	uint32_t serial;   // 0x00
	int16_t flags;     // 0x04
};

/*
 * CSerialList's MSVC std::list node (16 bytes) with sentinel-node
 * semantics: _Next at +0x00, _Prev at +0x04, value at +0x08.
 */
__extension__ typedef struct CSerialNode CSerialNode;
struct CSerialNode {
	CSerialNode *next; // 0x00
	CSerialNode *prev; // 0x04
	uint32_t serial;   // 0x08
	int16_t flags;     // 0x0C
	int16_t _pad0E;    // 0x0E
};

/*
 * Tagged doubly-linked list of entity serials (12 bytes), used for combat
 * target/attacker tracking. data points to the sentinel _Head; an empty
 * list has the sentinel pointing at itself.
 */
__extension__ typedef struct CSerialList CSerialList;
struct CSerialList {
	uint8_t type;       // 0x00
	uint8_t _sl_pad[3]; // 0x01
#if __SIZEOF_POINTER__ == 8
	uint8_t _pad64[4];  // 64-bit alignment pad
#endif
	CSerialNode *data;  // 0x04 (_Head sentinel)
	uint32_t count;     // 0x08 (_Size)
};

__extension__ typedef struct CDataBuffer CDataBuffer;

int CSerialNode_DecrementTTL(CSerialNode *node); // 0x00442960
void CSerialList_Constructor(CSerialList *list); // 0x00442993
void CSerialList_Destructor(CSerialList *list); // 0x004429E7
void CSerialList_AddFromSet(CSerialList *list, uint32_t serial); // 0x00442A3D
int CSerialList_Add(CSerialList *list, uint32_t serial); // 0x00442AAF
int CSerialList_Contains(CSerialList *list, uint32_t serial); // 0x00442B82
void CSerialList_Remove(CSerialList *list, uint32_t serial); // 0x00442C06
void CSerialList_PruneExpired(CSerialList *list); // 0x00442C64
void CSerialList_FindFirstValidTarget(StdPtrList *list, StdPtrNode **outIter); // 0x00442CDD
void CSerialList_FindFirstValidMobile(StdPtrList *list, StdPtrNode **outIter); // 0x00442DB0
void CSerialList_FindNearestMobile(StdPtrList *list, StdPtrNode **outIter, CLocation *loc); // 0x00442E71
void CSerialList_FindNearestTarget(StdPtrList *list, StdPtrNode **outIter, CLocation *loc); // 0x00442F79
int StdSerialValue_Equal(uint32_t *self, CSearchCtx *ctx); // 0x004472C0
void StdSerialList_Distance(StdPtrNode *beginIter, StdPtrNode *endIter, int *countOut); // 0x004472F0
void *StdSerialValue_DestructorWrapper(void *node); // 0x00447320
void *StdSerialValue_Constructor(void *dst, void *src); // 0x00447330
void *StdSerialValue_CopyConstructor(CSerialValue *self, CSerialValue *src); // 0x00447370
void *StdSerialValue_ScalarDelete(CSerialValue *self, int flags); // 0x00447390
void *StdSerialValue_ValInit(void *dst); // 0x004473C0
void StdSerialIter_CountRange(StdPtrNode *beginIter, StdPtrNode *endIter, int *countOut); // 0x004473E0
void StdSerialList_Erase(StdPtrList *list, StdPtrNode **result, StdPtrNode *pos); // 0x00446CB0
void CSerialList_Init(CSerialList *list);
void CSerialList_InsertBack(CSerialList *list, uint32_t serial, int16_t flags);
void CSerialList_Clear(CSerialList *list);

#endif /* SERIAL_LIST_H_ */
