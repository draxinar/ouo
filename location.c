/*
 * CLocation - 3D tile coordinate value type.
 *
 * Construction helpers, pairwise distance, and the same-location
 * predicate used throughout the world and AI code.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "wombat_exec.h"

static CLocation *CLocation_Constructor(CLocation *this); // 0x00491320

/*
 * 0x00401760 - Location_Distance2D
 *
 * Wrapped 2D Euclidean distance between two tile coordinates.
 */
int
Location_Distance2D(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
	CLocation a, b;

	CLocation_Constructor3D(&a, x1, y1, 0);
	CLocation_Constructor3D(&b, x2, y2, 0);
	return Location_DistanceTo2D(&b, &a);
}

/*
 * 0x004017A0 - CLocation::CLocation (3-arg constructor)
 *
 * Sets x, y, z from the three int16 args and returns this.
 */
CLocation *
CLocation_Constructor3D(CLocation *this, int16_t x, int16_t y, int16_t z)
{
	this->x = x;
	this->y = y;
	this->z = z;
	return this;
}

/*
 * 0x004017D0 - Location_DistanceTo2D
 *
 * Wrapped 2D Euclidean distance between two CLocation points.
 */
int
Location_DistanceTo2D(CLocation *a, CLocation *b)
{
	CLocation delta;

	CLocation_ComputeDelta(b, &delta, a);
	return CLocation_Magnitude2D(&delta);
}

/*
 * 0x00401800 - CLocation::Magnitude (2D)
 *
 * Integer sqrt of the 2D squared magnitude.
 */
int
CLocation_Magnitude2D(CLocation *delta)
{
	int sqMag;

	sqMag = CLocation_SquaredMagnitude2D(delta);
	return (int)sqrt((double)sqMag);
}

/*
 * 0x00401830 - CLocation::SquaredMagnitude (2D)
 *
 * Returns x*x + y*y.
 */
int
CLocation_SquaredMagnitude2D(CLocation *delta)
{
	int dx, dy;

	dx = (int)(int16_t)delta->x;
	dy = (int)(int16_t)delta->y;
	return dx * dx + dy * dy;
}

/*
 * 0x00401860 - Location_Distance3D
 *
 * Wrapped 3D Euclidean distance between two tile coordinates.
 */
int
Location_Distance3D(int16_t x1, int16_t y1, int16_t z1, int16_t x2, int16_t y2, int16_t z2)
{
	CLocation a, b;

	CLocation_Constructor3D(&a, x1, y1, z1);
	CLocation_Constructor3D(&b, x2, y2, z2);
	return Location_DistanceTo3D(&b, &a);
}

/*
 * 0x004018B0 - Location_DistanceTo3D
 *
 * Wrapped 3D Euclidean distance between two CLocation points.
 */
int
Location_DistanceTo3D(CLocation *a, CLocation *b)
{
	CLocation delta;

	CLocation_ComputeDelta(b, &delta, a);
	return CLocation_Magnitude3D(&delta);
}

/*
 * 0x004018E0 - CLocation::Magnitude (3D)
 *
 * Integer sqrt of the 3D squared magnitude.
 */
int
CLocation_Magnitude3D(CLocation *delta)
{
	int sqMag;

	sqMag = CLocation_SquaredMagnitude3D(delta);
	return (int)sqrt((double)sqMag);
}

/*
 * 0x00401910 - CLocation::SquaredMagnitude3D
 *
 * Returns x*x + y*y + z*z/121. Z is scaled by 121 (11^2) because
 * UO tiles have different horizontal and vertical scales.
 */
int
CLocation_SquaredMagnitude3D(CLocation *delta)
{
	int dx, dy, dz;

	dx = (int)(int16_t)delta->x;
	dy = (int)(int16_t)delta->y;
	dz = (int)(int16_t)delta->z;
	return dx * dx + dy * dy + dz * dz / 121;
}

/*
 * 0x00420D70 - CLocation::CLocation (default constructor)
 *
 * Sets x, y, z to 0xFFFF (the invalid-location sentinel).
 */
void
CLocation_Init(CLocation *loc)
{
	loc->x = 0xFFFF;
	loc->y = 0xFFFF;
	loc->z = (int16_t)0xFFFF;
}

/*
 * 0x00420EC0 - CLocation::ChebyshevDistance
 *
 * max(|dx|, |dy|) between a and b.
 */
int
CLocation_ChebyshevDistance(CLocation *a, CLocation *b)
{
	int dx, dy;

	dx = (int)(int16_t)a->x - (int)(int16_t)b->x;
	if (dx < 0)
		dx = -dx;
	dy = (int)(int16_t)a->y - (int)(int16_t)b->y;
	if (dy < 0)
		dy = -dy;
	return dx > dy ? dx : dy;
}

/*
 * 0x00421010 - CLocation::IsNearXYZ
 *
 * Same X/Y and a->z within [b->z, b->z + 8). Used by spatial queries
 * to match items at the same tile with a Z tolerance of 8.
 */
int
CLocation_IsNearXYZ(CLocation *a, CLocation *b)
{
	if (a->x != b->x)
		return 0;
	if (a->y != b->y)
		return 0;
	if (a->z < b->z)
		return 0;
	if (a->z - 8 >= b->z)
		return 0;
	return 1;
}

/*
 * 0x004210D0 - CLocation::IsEqualXY
 *
 * Returns 1 when both locations share the same (x, y) coordinates.
 */
int
CLocation_IsEqualXY(CLocation *a, CLocation *b)
{
	return a->x == b->x && a->y == b->y;
}

/*
 * 0x00421120 - CLocation::IsEqualXYZ
 *
 * Returns 1 when both locations share the same (x, y, z) coordinates.
 */
int
CLocation_IsEqualXYZ(CLocation *a, CLocation *b)
{
	return a->x == b->x && a->y == b->y && a->z == b->z;
}

/*
 * 0x00446990 - CLocation::ChebyshevMagnitude
 *
 * max(|x|, |y|) of a delta vector.
 */
int
CLocation_ChebyshevMagnitude(CLocation *delta)
{
	int absDx, absDy;

	absDx = abs((int)(int16_t)delta->x);
	absDy = abs((int)(int16_t)delta->y);
	return (absDx > absDy) ? absDx : absDy;
}

/*
 * 0x004469F0 - Location_WrappedChebyshevDistance
 *
 * Chebyshev distance between a and b with map wrapping applied.
 */
int
Location_WrappedChebyshevDistance(CLocation *a, CLocation *b)
{
	CLocation delta;

	CLocation_ComputeDelta(b, &delta, a);
	return CLocation_ChebyshevMagnitude(&delta);
}

/*
 * 0x0044D7A0 - CLocation::AddWrapped
 *
 * Adds delta to this, wrapping X into [0, 0x1400) and Y into
 * [0, 0x1000) when this lies on the Felucca map (x < 0x1400).
 * Writes the result to dst and returns dst.
 */
CLocation *
CLocation_AddWrapped(CLocation *this, CLocation *dst, CLocation *delta)
{
	CLocation temp;

	CLocation_Constructor3D(&temp, (int16_t)this->x + (int16_t)delta->x, (int16_t)this->y + (int16_t)delta->y, (int16_t)this->z + (int16_t)delta->z);

	if ((int16_t)this->x < 0x1400) {
		if ((int16_t)temp.x >= 0x1400)
			temp.x -= 0x1400;
		else if ((int16_t)temp.x < 0)
			temp.x += 0x1400;

		if ((int16_t)temp.y >= 0x1000)
			temp.y -= 0x1000;
		else if ((int16_t)temp.y < 0)
			temp.y += 0x1000;
	}

	CLocation_SetLoc(dst, &temp);
	return dst;
}

/*
 * 0x0044D862 - CLocation::Wrap
 *
 * Folds x into [0, 0x1400) and y into [0, 0x1000) in place. This is the
 * wrapping step CLocation_AddWrapped (0x0044D7A0) performs inline.
 * Unlike AddWrapped it does not gate on the Felucca check first.
 */
static __attribute__((unused)) void
CLocation_Wrap(CLocation *this)
{
	if ((int16_t)this->x >= 0x1400)
		this->x -= 0x1400;
	else if ((int16_t)this->x < 0)
		this->x += 0x1400;

	if ((int16_t)this->y >= 0x1000)
		this->y -= 0x1000;
	else if ((int16_t)this->y < 0)
		this->y += 0x1000;
}

/*
 * 0x0044D8E9 - CLocation::ComputeDelta
 *
 * Writes src - dst into outDelta with map wrapping: x folded into
 * [-0xA00, +0xA00] and y into [-0x800, +0x800]. Locations on
 * different map halves use a fixed cross-map offset instead.
 */
CLocation *
CLocation_ComputeDelta(CLocation *src, CLocation *outDelta, CLocation *dst)
{
	int srcGE, dstGE;

	srcGE = ((int16_t)src->x >= 0x1400) ? 1 : 0;
	dstGE = ((int16_t)dst->x >= 0x1400) ? 1 : 0;

	if (srcGE != dstGE) {
		int offset;
		if ((int16_t)src->x <= (int16_t)dst->x)
			offset = (int32_t)0xFFFFD8F1;
		else
			offset = 0x4E1E + (int32_t)0xFFFFD8F1;
		CLocation_Constructor3D(outDelta, (int16_t)offset, 0, 0);
		return outDelta;
	}

	{
		CLocation temp;

		CLocation_Constructor3D(&temp, (int16_t)src->x - (int16_t)dst->x, (int16_t)src->y - (int16_t)dst->y, (int16_t)src->z - (int16_t)dst->z);

		if ((int16_t)temp.x < 0x1400) {
			if ((int16_t)temp.x > 0x0A00)
				temp.x -= 0x1400;
			else if ((int16_t)temp.x < (int16_t)0xF600)
				temp.x += 0x1400;

			if ((int16_t)temp.y > 0x0800)
				temp.y -= 0x1000;
			else if ((int16_t)temp.y < (int16_t)0xF800)
				temp.y += 0x1000;
		}

		CLocation_SetLoc(outDelta, &temp);
	}
	return outDelta;
}

/*
 * 0x00461F50 - CLocation::CLocation(const CLocation&)
 *
 * Copy constructor: invalidates then copies x, y, z from source.
 */
CLocation *
CLocation_CopyConstruct(CLocation *loc, CLocation *source)
{
	CLocation_Init(loc);
	CLocation_SetLoc(loc, source);
	return loc;
}

/*
 * 0x004674D0 - CLocation::Invalidate
 *
 * Sets x, y, z to 0xFFFF in z,y,x order. Functionally identical
 * to CLocation_Init (0x00420D70) at a separate address.
 */
void
CLocation_Invalidate(CLocation *loc)
{
	loc->z = (int16_t)0xFFFF;
	loc->y = 0xFFFF;
	loc->x = 0xFFFF;
}

/*
 * 0x004697F7 - CLocation::CopyFrom
 *
 * Copies x, y, z from src into dst.
 */
void
CLocation_CopyFrom(CLocation *dst, CLocation *src)
{
	dst->x = src->x;
	dst->y = src->y;
	dst->z = src->z;
}

/*
 * 0x0046982C - CLocation::Set
 *
 * Sets x, y, z.
 */
void
CLocation_Set(CLocation *loc, int16_t x, int16_t y, int16_t z)
{
	loc->x = x;
	loc->y = y;
	loc->z = z;
}

/*
 * 0x004698A5 - CLocation::MoveDir
 *
 * Steps one tile in direction dir (masked to 0-7) using the
 * g_DirDeltaX/g_DirDeltaY tables. On the Felucca map (x < 0x1400)
 * coordinates wrap at the map edges.
 */
void
CLocation_MoveDir(CLocation *loc, int dir)
{
	dir &= 7;

	if ((int16_t)loc->x < 0x1400) {
		loc->x += g_DirDeltaX[dir];
		loc->y += g_DirDeltaY[dir];

		if ((int16_t)loc->x == 0x1400)
			loc->x = 0;
		else if ((int16_t)loc->x == -1)
			loc->x = 0x13FF;

		if ((int16_t)loc->y == 0x1000)
			loc->y = 0;
		else if ((int16_t)loc->y == -1)
			loc->y = 0x0FFF;
	} else {
		loc->x += g_DirDeltaX[dir];
		loc->y += g_DirDeltaY[dir];
	}
}

/*
 * 0x00469987 - CLocation::DecrY
 *
 * Decrements y, wrapping -1 to 0xFFF on the large map (x >= 0x1400).
 */
void
CLocation_DecrY(CLocation *loc)
{
	loc->y -= 1;
	if ((int16_t)loc->x < 0x1400)
		return;
	if ((int16_t)loc->y == -1)
		loc->y = 0xFFF;
}

/*
 * 0x004699C9 - CLocation::IncrY
 *
 * Increments y, wrapping 0x1000 to 0 on the large map (x >= 0x1400).
 */
void
CLocation_IncrY(CLocation *loc)
{
	loc->y += 1;
	if ((int16_t)loc->x < 0x1400)
		return;
	if ((int16_t)loc->y == 0x1000)
		loc->y = 0;
}

/*
 * 0x00469A0D - CLocation::IncrX
 *
 * Increments x, wrapping 0x1400 to 0 on the Felucca map (x < 0x1400).
 */
void
CLocation_IncrX(CLocation *loc)
{
	if ((int16_t)loc->x < 0x1400) {
		loc->x += 1;
		if ((int16_t)loc->x == 0x1400)
			loc->x = 0;
	} else {
		loc->x += 1;
	}
}

/*
 * 0x00469A5D - CLocation::DecrX
 *
 * Decrements x, wrapping -1 to 0x13FF on the Felucca map (x < 0x1400).
 */
void
CLocation_DecrX(CLocation *loc)
{
	if ((int16_t)loc->x < 0x1400) {
		loc->x -= 1;
		if ((int16_t)loc->x == -1)
			loc->x = 0x13FF;
	} else {
		loc->x -= 1;
	}
}

/*
 * 0x004843B0 - CLocation::IsInvalid
 *
 * Returns 1 if x, y, and z all equal the 0xFFFF sentinel.
 */
int
CLocation_IsInvalid(CLocation *loc)
{
	return (int16_t)loc->x == -1 && (int16_t)loc->y == -1 && (int16_t)loc->z == -1;
}

/*
 * 0x00491320 - CLocation::CLocation
 *
 * Default constructor: delegates to CLocation_Init and returns this.
 */
static __attribute__((unused)) CLocation *
CLocation_Constructor(CLocation *this)
{
	CLocation_Init(this);
	return this;
}

/*
 * 0x0049DB70 - CLocation::NotEqual
 *
 * Returns 1 if any of x, y, z differ between the two locations.
 */
int
CLocation_NotEqual(CLocation *loc, CLocation *other)
{
	if ((int16_t)loc->x != (int16_t)other->x)
		return 1;
	if ((int16_t)loc->y != (int16_t)other->y)
		return 1;
	if ((int16_t)loc->z != (int16_t)other->z)
		return 1;
	return 0;
}

/*
 * 0x004A8C1C - DistBetween
 *
 * Squared 2D distance dx*dx + dy*dy, or INT_MAX if either squared
 * component reaches 0x4000000 (overflow guard for 8192+ tile deltas).
 */
int
DistBetween(CLocation *a, CLocation *b)
{
	int dx, dy;

	dx = (int)(int16_t)a->x - (int)(int16_t)b->x;
	dy = (int)(int16_t)a->y - (int)(int16_t)b->y;

	dx = dx * dx;
	dy = dy * dy;

	if (dx >= 0x4000000 || dy >= 0x4000000)
		return 0x7FFFFFFF;

	return dx + dy;
}
