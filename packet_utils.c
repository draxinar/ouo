/*
 * Packet byte-buffer helpers.
 *
 * Small typed getters / setters for reading and writing network-order
 * integers, fixed-length strings, and variable-length fields at an
 * offset inside a packet buffer.
 */

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "packet_handler.h"
#include "packet_utils.h"
#include "utils.h"

uint16_t g_PacketOffset;
uint16_t g_PacketSize;

/*
 * 0x004923FC - PutPacketType
 *
 * Writes the packet type byte and positions the cursor past the 1- or
 * 3-byte header, storing size as the packet's intended total length.
 */
uint16_t
PutPacketType(uint8_t *buf, uint8_t packetType, int size)
{
	int dynamicSize;
	uint16_t result;

	InitializeGlobalPacketOffset();
	SetPacketType(buf, packetType);
	dynamicSize = PacketIsDynamicSize(buf);
	result = SetPacketOffset(buf, dynamicSize != 0 ? 3 : 1);
	g_PacketSize = size;
	return result;
}

/*
 * 0x0049244C - GetByte
 *
 * Reads one byte at offset *off (past the packet header) and advances
 * *off by 1.
 */
uint32_t *
GetByte(uint8_t *buf, uint32_t *off, uint8_t *v)
{
	uint8_t *b;
	uint32_t *result;

	b = &buf[*off];
	*v = b[GetSizeLength(buf)];
	result = off;
	++*off;
	return result;
}

/*
 * 0x00492481 - GetWord
 *
 * Reads a big-endian 16-bit word at offset *off and advances *off by 2.
 * The binary emits two identical copies of this routine at 0x00492481
 * and 0x004924CB.
 */
uint32_t *
GetWord(uint8_t *buf, uint32_t *off, uint16_t *v)
{
	uint8_t *b;
	uint16_t headerLen;
	uint32_t *result;

	b = &buf[*off];
	headerLen = GetSizeLength(buf);
	memcpy(v, &b[headerLen], 2u);
	ntohs_inplace(v);
	result = off;
	*off += 2;
	return result;
}

/*
 * 0x00492515 - GetDWord
 *
 * Reads a big-endian 32-bit dword at offset *off and advances *off by 4.
 */
uint32_t *
GetDWord(uint8_t *buf, uint32_t *off, uint32_t *v)
{
	uint8_t *b;
	uint16_t headerLen;
	uint32_t *result;

	b = &buf[*off];
	headerLen = GetSizeLength(buf);
	memcpy(v, &b[headerLen], 4u);
	ntohl_inplace((uint32_t *)v);
	result = off;
	*off += 4;
	return result;
}

/*
 * 0x0049255F - GetString
 *
 * Aims *str at the fixed-length string starting at offset *off inside
 * the packet payload and advances *off by len.
 */
uint32_t *
GetString(uint8_t *buf, uint32_t *off, char **str, int len)
{
	uint8_t *b;
	uint32_t *result;

	b = &buf[*off];
	*str = (char *)&b[GetSizeLength(buf)];
	result = off;
	*off += len;
	return result;
}

/*
 * 0x00492593 - GetNullTermString
 *
 * Aims *str at the null-terminated string at offset *off inside the
 * packet payload and advances *off past the terminator.
 */
void
GetNullTermString(uint8_t *buf, uint32_t *off, char **str)
{
	char *s;

	s = (char *)buf + *off + (GetSizeLength(buf) & 0xFFFF);
	*str = s;
	*off += strlen(*str) + 1;
}

/*
 * 0x004925D6 - PutByte
 *
 * Appends one byte at the current packet offset and advances it by 1.
 */
uint16_t
PutByte(uint8_t *buf, uint8_t v)
{
	uint16_t off;

	off = GetPacketOffset(buf);
	buf[off] = v;
	return SetPacketOffset(buf, off + 1);
}

/*
 * 0x00492610 - PutWord
 *
 * Appends a 16-bit value in network byte order at the current packet
 * offset and advances it by 2. The binary emits a duplicate of this
 * routine at 0x00492660.
 */
uint16_t
PutWord(uint8_t *buf, ...)
{
	int off;
	va_list v;
	uint16_t val;

	va_start(v, buf);
	val = (uint16_t)va_arg(v, int);
	va_end(v);
	htons_inplace(&val);
	off = GetPacketOffset(buf);
	memcpy(&buf[off], &val, 2u);
	return SetPacketOffset(buf, off + 2);
}

/*
 * 0x004926B0 - PutDWord
 *
 * Appends a 32-bit value in network byte order at the current packet
 * offset and advances it by 4.
 */
uint16_t
PutDWord(uint8_t *buf, ...)
{
	int off;
	va_list v;
	uint32_t val;

	va_start(v, buf);
	val = va_arg(v, uint32_t);
	va_end(v);
	htonl_inplace(&val);
	off = GetPacketOffset(buf);
	memcpy(&buf[off], &val, 4);
	return SetPacketOffset(buf, off + 4);
}

/*
 * 0x00492700 - PutString
 *
 * Copies len bytes from str into the packet at the current offset and
 * advances it by len.
 */
uint16_t
PutString(uint8_t *buf, char *str, int len)
{
	int off;

	off = GetPacketOffset(buf);
	memcpy(&buf[off], str, len);
	return SetPacketOffset(buf, len + off);
}

/*
 * 0x0049DA00 - htonl_inplace
 *
 * Swaps a 32-bit value's byte order in place and returns the result.
 */
uint32_t
htonl_inplace(uint32_t *v)
{
	*v = htonl(*v);
	return *v;
}

/*
 * 0x004CDCB4 - client.exe 1.25.35 - g_PacketTable_12535
 *
 * Per-opcode packet size table shipped in client.exe 1.25.35. Kept
 * alongside the UoDemo.exe table so the server can answer client
 * version probes with the layout that version expects.
 */
// clang-format off
uint16_t g_PacketTable_12535[] = {
	100, 0,
	1, 0, 5, 0,
	2, 0, 3, 0,
	3, 0, 32768, 0,
	4, 0, 2, 0,
	5, 0, 5, 0,
	6, 0, 5, 0,
	7, 0, 7, 0,
	8, 0, 14, 0,
	9, 0, 5, 0,
	10, 0, 11, 0,
	11, 0, 266, 0,
	12, 0, 32768, 0,
	13, 0, 3, 0,
	14, 0, 32768, 0,
	15, 0, 61, 0,
	16, 0, 215, 0,
	17, 0, 32768, 0,
	18, 0, 32768, 0,
	19, 0, 10, 0,
	20, 0, 6, 0,
	21, 0, 9, 0,
	22, 0, 1, 0,
	23, 0, 32768, 0,
	24, 0, 32768, 0,
	25, 0, 32768, 0,
	26, 0, 32768, 0,
	27, 0, 37, 0,
	28, 0, 32768, 0,
	29, 0, 5, 0,
	30, 0, 4, 0,
	31, 0, 8, 0,
	32, 0, 19, 0,
	33, 0, 8, 0,
	34, 0, 3, 0,
	35, 0, 26, 0,
	36, 0, 7, 0,
	37, 0, 20, 0,
	38, 0, 5, 0,
	39, 0, 2, 0,
	40, 0, 5, 0,
	41, 0, 1, 0,
	42, 0, 5, 0,
	43, 0, 2, 0,
	44, 0, 2, 0,
	45, 0, 17, 0,
	46, 0, 15, 0,
	47, 0, 10, 0,
	48, 0, 5, 0,
	49, 0, 1, 0,
	50, 0, 2, 0,
	51, 0, 2, 0,
	52, 0, 10, 0,
	53, 0, 653, 0,
	54, 0, 32768, 0,
	55, 0, 8, 0,
	56, 0, 7, 0,
	57, 0, 9, 0,
	58, 0, 32768, 0,
	59, 0, 32768, 0,
	60, 0, 32768, 0,
	61, 0, 2, 0,
	62, 0, 37, 0,
	63, 0, 32768, 0,
	64, 0, 201, 0,
	65, 0, 32768, 0,
	66, 0, 32768, 0,
	67, 0, 553, 0,
	68, 0, 713, 0,
	69, 0, 5, 0,
	70, 0, 32768, 0,
	71, 0, 11, 0,
	72, 0, 73, 0,
	73, 0, 93, 0,
	74, 0, 5, 0,
	75, 0, 9, 0,
	76, 0, 32768, 0,
	77, 0, 32768, 0,
	78, 0, 6, 0,
	79, 0, 2, 0,
	80, 0, 32768, 0,
	81, 0, 32768, 0,
	82, 0, 32768, 0,
	83, 0, 2, 0,
	84, 0, 12, 0,
	85, 0, 1, 0,
	86, 0, 11, 0,
	87, 0, 110, 0,
	88, 0, 106, 0,
	89, 0, 32768, 0,
	90, 0, 32768, 0,
	91, 0, 4, 0,
	92, 0, 2, 0,
	93, 0, 73, 0,
	94, 0, 32768, 0,
	95, 0, 49, 0,
	96, 0, 5, 0,
	97, 0, 9, 0,
	98, 0, 15, 0,
	99, 0, 13, 0,
	100, 0, 1, 0,
	101, 0, 4, 0,
	102, 0, 32768, 0,
	103, 0, 21, 0,
	104, 0, 32768, 0,
	105, 0, 32768, 0,
	106, 0, 3, 0,
	107, 0, 9, 0,
	108, 0, 19, 0,
	109, 0, 3, 0,
	110, 0, 14, 0,
	111, 0, 32768, 0,
	112, 0, 28, 0,
	113, 0, 32768, 0,
	114, 0, 5, 0,
	115, 0, 2, 0,
	116, 0, 32768, 0,
	117, 0, 35, 0,
	118, 0, 16, 0,
	119, 0, 17, 0,
	120, 0, 32768, 0,
	121, 0, 9, 0,
	122, 0, 32768, 0,
	123, 0, 2, 0,
	124, 0, 32768, 0,
	125, 0, 13, 0,
	126, 0, 2, 0,
	127, 0, 32768, 0,
	128, 0, 62, 0,
	129, 0, 32768, 0,
	130, 0, 2, 0,
	131, 0, 39, 0,
	132, 0, 69, 0,
	133, 0, 2, 0,
	134, 0, 32768, 0,
	135, 0, 32768, 0,
	136, 0, 66, 0,
	137, 0, 32768, 0,
	138, 0, 32768, 0,
	139, 0, 32768, 0,
	140, 0, 11, 0,
	141, 0, 32768, 0,
	142, 0, 32768, 0,
	143, 0, 32768, 0,
	144, 0, 19, 0,
	145, 0, 65, 0,
	146, 0, 32768, 0,
	147, 0, 98, 0,
	148, 0, 32768, 0,
	149, 0, 9, 0,
	150, 0, 32768, 0,
	151, 0, 2, 0,
	152, 0, 32768, 0,
	153, 0, 26, 0,
	154, 0, 32768, 0,
	155, 0, 258, 0,
	156, 0, 309, 0,
	157, 0, 51, 0,
	158, 0, 32768, 0,
	159, 0, 32768, 0,
	160, 0, 3, 0,
	161, 0, 9, 0,
	162, 0, 9, 0,
	163, 0, 9, 0,
	164, 0, 149, 0,
	165, 0, 32768, 0,
	166, 0, 32768, 0,
	167, 0, 4, 0,
	168, 0, 32768, 0,
	169, 0, 32768, 0,
	170, 0, 5, 0,
	171, 0, 32768, 0,
	172, 0, 32768, 0,
	173, 0, 32768, 0,
	174, 0, 32768, 0,
	175, 0, 13, 0,
	176, 0, 32768, 0,
	177, 0, 32768, 0,
	178, 0, 32768, 0,
	179, 0, 32768, 0,
	180, 0, 32768, 0,
	181, 0, 64, 0,
	182, 0, 9, 0,
	183, 0, 32768, 0,
	184, 0, 32768, 0,
	185, 0, 3, 0,
};
// clang-format on

/*
 * 0x0061F138 - g_PacketTable
 *
 * Per-opcode packet size table from UoDemo.exe. patchPackets_126()
 * rewrites entry 0x02 from 3 to 7 for 1.26+ clients that add the
 * 4-byte fast-walk prevention key.
 */
// clang-format off
uint16_t g_PacketTable[] = {
	100, 0, 0,
	1, 0, 5, 0, 0,
	2, 0, 3, 0, 0,
	3, 0, 32768, 1, 0,
	4, 0, 2, 0, 0,
	5, 0, 5, 0, 0,
	6, 0, 5, 0, 0,
	7, 0, 7, 0, 0,
	8, 0, 14, 0, 0,
	9, 0, 5, 1, 0,
	10, 0, 11, 0, 0,
	255, 0, 0, 1, 0,
	12, 0, 32768, 1, 0,
	13, 0, 3, 1, 0,
	14, 0, 32768, 0, 0,
	15, 0, 61, 0, 0,
	255, 0, 0, 0, 0,
	17, 0, 32768, 0, 0,
	18, 0, 32768, 0, 0,
	19, 0, 10, 1, 0,
	20, 0, 6, 0, 0,
	21, 0, 9, 1, 0,
	22, 0, 1, 1, 0,
	23, 0, 32768, 1, 0,
	24, 0, 32768, 1, 0,
	25, 0, 32768, 0, 0,
	26, 0, 32768, 1, 0,
	27, 0, 37, 0, 0,
	28, 0, 32768, 1, 0,
	29, 0, 5, 1, 0,
	30, 0, 4, 0, 0,
	255, 0, 0, 1, 0,
	32, 0, 19, 1, 0,
	33, 0, 8, 0, 0,
	34, 0, 3, 1, 0,
	35, 0, 26, 1, 0,
	36, 0, 7, 1, 0,
	37, 0, 20, 1, 0,
	38, 0, 5, 1, 0,
	39, 0, 2, 1, 0,
	40, 0, 5, 1, 0,
	41, 0, 1, 1, 0,
	42, 0, 5, 1, 0,
	43, 0, 2, 0, 0,
	44, 0, 2, 1, 0,
	45, 0, 17, 1, 0,
	46, 0, 15, 1, 0,
	47, 0, 10, 1, 0,
	48, 0, 5, 1, 0,
	49, 0, 1, 1, 0,
	50, 0, 2, 1, 0,
	51, 0, 2, 0, 0,
	52, 0, 10, 1, 0,
	53, 0, 653, 1, 0,
	54, 0, 32768, 1, 0,
	55, 0, 8, 0, 0,
	56, 0, 7, 0, 0,
	57, 0, 9, 0, 0,
	58, 0, 32768, 0, 0,
	59, 0, 32768, 1, 0,
	60, 0, 32768, 1, 0,
	61, 0, 2, 0, 0,
	62, 0, 37, 1, 0,
	63, 0, 32768, 1, 0,
	64, 0, 201, 1, 0,
	65, 0, 32768, 1, 0,
	66, 0, 32768, 1, 0,
	67, 0, 553, 1, 0,
	68, 0, 713, 0, 0,
	69, 0, 5, 1, 0,
	70, 0, 32768, 1, 0,
	71, 0, 11, 1, 0,
	72, 0, 73, 1, 0,
	73, 0, 93, 1, 0,
	74, 0, 5, 0, 0,
	75, 0, 9, 1, 0,
	76, 0, 32768, 1, 0,
	77, 0, 32768, 1, 0,
	78, 0, 6, 1, 0,
	79, 0, 2, 0, 0,
	80, 0, 32768, 0, 0,
	81, 0, 32768, 0, 0,
	82, 0, 32768, 1, 0,
	83, 0, 2, 0, 0,
	84, 0, 12, 1, 0,
	85, 0, 1, 0, 0,
	86, 0, 11, 1, 0,
	87, 0, 110, 1, 0,
	88, 0, 106, 1, 0,
	255, 0, 0, 1, 0,
	90, 0, 32768, 1, 0,
	91, 0, 4, 1, 0,
	92, 0, 2, 0, 0,
	93, 0, 73, 1, 0,
	94, 0, 32768, 1, 0,
	95, 0, 49, 1, 0,
	96, 0, 5, 1, 0,
	97, 0, 9, 1, 0,
	98, 0, 15, 0, 0,
	99, 0, 13, 0, 0,
	100, 0, 1, 1, 0,
	101, 0, 4, 0, 0,
	102, 0, 32768, 1, 0,
	103, 0, 21, 1, 0,
	104, 0, 32768, 0, 0,
	105, 0, 32768, 0, 0,
	106, 0, 3, 0, 0,
	107, 0, 9, 0, 0,
	108, 0, 19, 1, 0,
	109, 0, 3, 1, 0,
	110, 0, 14, 0, 0,
	111, 0, 32768, 0, 0,
	112, 0, 28, 0, 0,
	113, 0, 32768, 0, 0,
	114, 0, 5, 0, 0,
	115, 0, 2, 0, 0,
	116, 0, 32768, 0, 0,
	117, 0, 35, 1, 0,
	118, 0, 16, 1, 0,
	119, 0, 17, 1, 0,
	120, 0, 32768, 1, 0,
	121, 0, 9, 1, 0,
	122, 0, 32768, 0, 0,
	123, 0, 2, 0, 0,
	124, 0, 32768, 0, 0,
	125, 0, 13, 1, 0,
	126, 0, 2, 1, 0,
	127, 0, 32768, 0, 0,
	128, 0, 62, 1, 0,
	129, 0, 32768, 1, 0,
	130, 0, 2, 0, 0,
	131, 0, 39, 0, 0,
	132, 0, 69, 1, 0,
	133, 0, 2, 0, 0,
	134, 0, 32768, 0, 0,
	135, 0, 32768, 0, 0,
	136, 0, 66, 1, 0,
	137, 0, 32768, 1, 0,
	138, 0, 32768, 0, 0,
	139, 0, 32768, 0, 0,
	140, 0, 11, 0, 0,
	141, 0, 32768, 0, 0,
	142, 0, 32768, 0, 0,
	143, 0, 32768, 0, 0,
	144, 0, 19, 0, 0,
	145, 0, 65, 1, 0,
	146, 0, 32768, 0, 0,
	147, 0, 98, 1, 0,
	148, 0, 32768, 0, 0,
	149, 0, 9, 1, 0,
	150, 0, 32768, 0, 0,
	151, 0, 2, 0, 0,
	152, 0, 32768, 0, 0,
	153, 0, 26, 0, 0,
	154, 0, 32768, 0, 0,
	155, 0, 258, 1, 0,
	156, 0, 309, 1, 0,
	157, 0, 51, 0, 0,
	158, 0, 32768, 0, 0,
	159, 0, 32768, 0, 0,
	160, 0, 3, 1, 0,
	161, 0, 9, 1, 0,
	162, 0, 9, 1, 0,
	163, 0, 9, 1, 0,
	164, 0, 149, 0, 0,
	165, 0, 32768, 0, 0,
	166, 0, 32768, 0, 0,
	167, 0, 4, 0, 0,
	168, 0, 32768, 0, 0,
	169, 0, 32768, 0, 0,
	170, 0, 5, 0, 0,
	171, 0, 32768, 0, 0,
	172, 0, 32768, 0, 0,
	173, 0, 32768, 0, 0,
	174, 0, 32768, 0, 0,
	175, 0, 13, 0, 0,
	176, 0, 32768, 0, 0,
	177, 0, 32768, 0, 0,
	178, 0, 32768, 0, 0,
	179, 0, 32768, 0, 0,
	180, 0, 32768, 0, 0,
	181, 0, 64, 0, 0,

	// Clients >= 1.25.35
	182, 0, 9, 0, 0,
	183, 0, 32768, 0, 0,
	184, 0, 32768, 0, 0,
	185, 0, 3, 0, 0,

	// Clients >= 1.26.0 (0xBA-0xBF)
	186, 0, 6, 0, 0,      // 0xBA: Quest Arrow (server-to-client)
	187, 0, 9, 0, 0,      // 0xBB: Account ID
	188, 0, 3, 0, 0,      // 0xBC: Season (server-to-client)
	189, 0, 32768, 0, 0,  // 0xBD: Client Version (dynamic)
	190, 0, 32768, 0, 0,  // 0xBE: Assist Version (dynamic)
	191, 0, 32768, 0, 0,  // 0xBF: General Info (dynamic)

	// Clients >= 3.0.x / 4.0.x / 5.0.x (0xC0-0xE2)
	192, 0, 36, 0, 0,     // 0xC0: GraphicEffect Extended (S->C)
	193, 0, 32768, 0, 0,  // 0xC1: Cliloc Message (S->C, dynamic)
	194, 0, 32768, 0, 0,  // 0xC2: Unicode Text Entry (C->S, dynamic)
	195, 0, 32768, 0, 0,  // 0xC3: Cliloc Message Affix (S->C, dynamic)
	196, 0, 6, 0, 0,      // 0xC4: Semivisible Effect (S->C)
	197, 0, 203, 0, 0,    // 0xC5: Invalid Map NAK (C->S)
	198, 0, 1, 0, 0,      // 0xC6: Context Menu Close (S->C)
	199, 0, 49, 0, 0,     // 0xC7: 3D Animation (S->C)
	200, 0, 2, 0, 0,      // 0xC8: Client Update Range (both)
	201, 0, 6, 0, 0,      // 0xC9: Triptime Ping (S->C)
	202, 0, 6, 0, 0,      // 0xCA: Triptime Response (S->C)
	203, 0, 7, 0, 0,      // 0xCB: Global Queue Count (S->C)
	204, 0, 32768, 0, 0,  // 0xCC: Cliloc Message Affix Full (S->C, dynamic)
	205, 0, 1, 0, 0,      // 0xCD: Character Animation (S->C)
	206, 0, 32768, 0, 0,  // 0xCE: Generic Gump Extended (S->C, dynamic)
	207, 0, 0, 0, 0,      // 0xCF: Account Login SE (C->S) - unused
	208, 0, 32768, 0, 0,  // 0xD0: Configuration File (both, dynamic)
	209, 0, 2, 0, 0,      // 0xD1: Logout Status (both)
	210, 0, 25, 0, 0,     // 0xD2: Extended Animation (S->C)
	211, 0, 32768, 0, 0,  // 0xD3: Extended Animation Var (S->C, dynamic)
	212, 0, 32768, 0, 0,  // 0xD4: Book Header New (both, dynamic)
	213, 0, 32768, 0, 0,  // 0xD5: Object Property List (S->C, dynamic)
	214, 0, 32768, 0, 0,  // 0xD6: OPL Request (C->S, dynamic)
	215, 0, 32768, 0, 0,  // 0xD7: Custom House Request (C->S, dynamic)
	216, 0, 32768, 0, 0,  // 0xD8: Custom House Data (S->C, dynamic)
	217, 0, 268, 0, 0,    // 0xD9: Mahjong Game Data (C->S)
	218, 0, 32768, 0, 0,  // 0xDA: Mahjong Command (C->S, dynamic)
	219, 0, 32768, 0, 0,  // 0xDB: Character Transfer Log (S->C, dynamic)
	220, 0, 9, 0, 0,      // 0xDC: OPL Info Legacy (S->C, 4.0.4t+)
	221, 0, 32768, 0, 0,  // 0xDD: Compressed Gump (S->C, dynamic, 5.0.0+)
	222, 0, 32768, 0, 0,  // 0xDE: Update Mobile Status ML (S->C, dynamic, 5.0.2+)
	223, 0, 32768, 0, 0,  // 0xDF: Buff/Debuff (S->C, dynamic, 5.0.2+)
	224, 0, 32768, 0, 0,  // 0xE0: Bug Report New (C->S, dynamic, 5.0.9+)
	225, 0, 32768, 0, 0,  // 0xE1: Client Type Notification (C->S, dynamic, 5.0.9+)
	226, 0, 10, 0, 0,     // 0xE2: Extended Animation AOS (S->C, 5.0.9+)
	// 0xE3-0xFF: unmapped padding. No supported client (1.25-5.0.x) sends
	// these as gameplay packets. The dispatch (CUserSock_Handle_Input /
	// UserSock_DoHandlePacket) reads g_PacketTable[5 * type] BEFORE validating
	// the type (the binary's read-then-check pattern, safe there only because
	// its table lives in a large .data segment). With our heap-allocated table
	// a packet-type byte >= 0xE3 - e.g. a corrupt/torn packet from a dropped
	// connection - would read past the end. These size-0 entries keep that
	// lookup in-bounds for any byte 0-255; size 0 is treated as invalid and
	// the byte is skipped, matching the existing >= 0xE3 reject path.
	227, 0, 0, 0, 0,      // 0xE3
	228, 0, 0, 0, 0,      // 0xE4
	229, 0, 0, 0, 0,      // 0xE5
	230, 0, 0, 0, 0,      // 0xE6
	231, 0, 0, 0, 0,      // 0xE7
	232, 0, 0, 0, 0,      // 0xE8
	233, 0, 0, 0, 0,      // 0xE9
	234, 0, 0, 0, 0,      // 0xEA
	235, 0, 0, 0, 0,      // 0xEB
	236, 0, 0, 0, 0,      // 0xEC
	237, 0, 0, 0, 0,      // 0xED
	238, 0, 0, 0, 0,      // 0xEE
	239, 0, 0, 0, 0,      // 0xEF
	240, 0, 0, 0, 0,      // 0xF0
	241, 0, 0, 0, 0,      // 0xF1
	242, 0, 0, 0, 0,      // 0xF2
	243, 0, 0, 0, 0,      // 0xF3
	244, 0, 0, 0, 0,      // 0xF4
	245, 0, 0, 0, 0,      // 0xF5
	246, 0, 0, 0, 0,      // 0xF6
	247, 0, 0, 0, 0,      // 0xF7
	248, 0, 0, 0, 0,      // 0xF8
	249, 0, 0, 0, 0,      // 0xF9
	250, 0, 0, 0, 0,      // 0xFA
	251, 0, 0, 0, 0,      // 0xFB
	252, 0, 0, 0, 0,      // 0xFC
	253, 0, 0, 0, 0,      // 0xFD
	254, 0, 0, 0, 0,      // 0xFE
	255, 0, 0, 0, 0,      // 0xFF
};
// clang-format on
const size_t g_PacketTableSize = sizeof(g_PacketTable);

/*
 * 0x0049DA20 - htons_inplace
 *
 * Swaps a 16-bit value's byte order in place and returns the result.
 */
uint16_t
htons_inplace(uint16_t *v)
{
	*v = htons(*v);
	return *v;
}

/*
 * 0x0049DA40 - ntohl_inplace
 *
 * Converts a 32-bit network-order value to host order in place.
 */
uint32_t
ntohl_inplace(uint32_t *v)
{
	*v = ntohl(*v);
	return *v;
}

/*
 * 0x0049DA60 - ntohs_inplace
 *
 * Converts a 16-bit network-order value to host order in place.
 */
uint16_t
ntohs_inplace(uint16_t *v)
{
	*v = ntohs(*v);
	return *v;
}

/*
 * 0x004B38E6 - WriteInt32LE
 *
 * Writes a 32-bit little-endian integer at *cursor and advances
 * *cursor by 4.
 */
void
WriteInt32LE(char **cursor, int32_t value)
{
	SwapEndian(&value);
	memcpy(*cursor, &value, 4);
	*cursor += 4;
}

/*
 * 0x004B39D2 - WriteInt16LE
 *
 * Writes a 16-bit little-endian integer at *cursor and advances
 * *cursor by 2.
 */
void
WriteInt16LE(char **cursor, int16_t value)
{
	SwapEndian(&value);
	memcpy(*cursor, &value, 2);
	*cursor += 2;
}

/*
 * 0x004B3A48 - ReadInt32LE
 *
 * Reads a 32-bit little-endian integer at *cursor and advances
 * *cursor by 4.
 */
int32_t
ReadInt32LE(char **cursor)
{
	int32_t val;
	memcpy(&val, *cursor, 4);
	SwapEndian(&val);
	*cursor += 4;
	return val;
}

/*
 * 0x004B3A88 - ReadInt16LE
 *
 * Reads a 16-bit little-endian integer at *cursor and advances
 * *cursor by 2.
 */
int16_t
ReadInt16LE(char **cursor)
{
	int16_t val;
	memcpy(&val, *cursor, 2);
	SwapEndian(&val);
	*cursor += 2;
	return val;
}

/*
 * Custom - GetUnicodeBE
 *
 * Reads a big-endian UTF-16 NUL-terminated string at offset *off inside a
 * packet payload, down-converting each 16-bit code unit to a single ASCII
 * byte, and advances *off past the 16-bit terminator. Writes at most cap-1
 * bytes plus a NUL into dst; a malformed string with no terminator is
 * bounded so it cannot run away. The UO chat packets (0xB2/0xB3/0xB5)
 * carry every string in this encoding.
 */
void
GetUnicodeBE(uint8_t *buf, uint32_t *off, char *dst, int cap)
{
	int i;
	int guard;
	uint16_t ch;

	i = 0;
	for (guard = 0; guard < 4096; guard++) {
		GetWord(buf, off, &ch);
		if (ch == 0)
			break;
		if (i < cap - 1)
			dst[i++] = (char)ch;
	}
	dst[i] = '\0';
}

/*
 * Custom - PutUnicodeBE
 *
 * Appends an ASCII string as big-endian UTF-16 with a 16-bit NUL
 * terminator at the current packet offset.
 */
void
PutUnicodeBE(uint8_t *buf, const char *str)
{
	int i;

	for (i = 0; str[i] != '\0'; i++)
		PutWord(buf, (uint16_t)(uint8_t)str[i]);
	PutWord(buf, 0);
}
