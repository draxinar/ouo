#ifndef LOCATION_H_
#define LOCATION_H_

#include <stdint.h>

__extension__ typedef struct CLocation CLocation;

/*
 * World coordinate triple (6 bytes). Embedded at CEntity+0x0A on every
 * entity; x,y are world-tile unsigned, z is signed altitude.
 */
struct CLocation {
	uint16_t x; // 0x00
	uint16_t y; // 0x02
	int16_t z;  // 0x04
};

int Location_Distance2D(int16_t x1, int16_t y1, int16_t x2, int16_t y2); // 0x00401760
CLocation *CLocation_Constructor(CLocation *this); // 0x00491320
CLocation *CLocation_Constructor3D(CLocation *this, int16_t x, int16_t y, int16_t z); // 0x004017A0
int Location_DistanceTo2D(CLocation *a, CLocation *b); // 0x004017D0
int CLocation_Magnitude2D(CLocation *delta); // 0x00401800
int CLocation_SquaredMagnitude2D(CLocation *delta); // 0x00401830
int Location_Distance3D(int16_t x1, int16_t y1, int16_t z1, int16_t x2, int16_t y2, int16_t z2); // 0x00401860
int Location_DistanceTo3D(CLocation *a, CLocation *b); // 0x004018B0
int CLocation_Magnitude3D(CLocation *delta); // 0x004018E0
int CLocation_SquaredMagnitude3D(CLocation *delta); // 0x00401910
void CLocation_Init(CLocation *loc); // 0x00420D70
int CLocation_ChebyshevDistance(CLocation *a, CLocation *b); // 0x00420EC0
int CLocation_IsNearXYZ(CLocation *a, CLocation *b); // 0x00421010
int CLocation_IsEqualXY(CLocation *a, CLocation *b); // 0x004210D0
int CLocation_IsEqualXYZ(CLocation *a, CLocation *b); // 0x00421120
int CLocation_ChebyshevMagnitude(CLocation *delta); // 0x00446990
int Location_WrappedChebyshevDistance(CLocation *a, CLocation *b); // 0x004469F0
CLocation *CLocation_AddWrapped(CLocation *this, CLocation *dst, CLocation *delta); // 0x0044D7A0
CLocation *CLocation_ComputeDelta(CLocation *src, CLocation *outDelta, CLocation *dst); // 0x0044D8E9
CLocation *CLocation_CopyConstruct(CLocation *loc, CLocation *source); // 0x00461F50
void CLocation_Invalidate(CLocation *loc); // 0x004674D0
void CLocation_CopyFrom(CLocation *dst, CLocation *src); // 0x004697F7
void CLocation_Set(CLocation *loc, int16_t x, int16_t y, int16_t z); // 0x0046982C
void CLocation_MoveDir(CLocation *loc, int dir); // 0x004698A5
void CLocation_DecrY(CLocation *loc); // 0x00469987
void CLocation_IncrY(CLocation *loc); // 0x004699C9
void CLocation_IncrX(CLocation *loc); // 0x00469A0D
void CLocation_DecrX(CLocation *loc); // 0x00469A5D
int CLocation_IsInvalid(CLocation *loc); // 0x004843B0
int CLocation_NotEqual(CLocation *loc, CLocation *other); // 0x0049DB70
int DistBetween(CLocation *a, CLocation *b); // 0x004A8C1C

#endif /* LOCATION_H_ */
