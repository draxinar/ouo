#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

#include "packet_reader.h"

void
PacketReader_Init(PacketReader *reader, const uint8_t *buf, uint16_t packetLen, uint16_t headerLen)
{
	reader->buf = buf;
	reader->len = packetLen;
	reader->off = headerLen;
	reader->headerLen = headerLen;
	if (reader->off > reader->len)
		reader->off = reader->len;
}

uint16_t
PacketReader_Remaining(const PacketReader *reader)
{
	if (reader->off >= reader->len)
		return 0;
	return (uint16_t)(reader->len - reader->off);
}

uint16_t
PacketReader_Offset(const PacketReader *reader)
{
	if (reader->off < reader->headerLen)
		return 0;
	return (uint16_t)(reader->off - reader->headerLen);
}

int
PacketReader_Skip(PacketReader *reader, uint16_t len)
{
	if (PacketReader_Remaining(reader) < len)
		return 0;
	reader->off = (uint16_t)(reader->off + len);
	return 1;
}

int
PacketReader_ReadU8(PacketReader *reader, uint8_t *out)
{
	if (PacketReader_Remaining(reader) < 1)
		return 0;
	*out = reader->buf[reader->off];
	reader->off++;
	return 1;
}

int
PacketReader_ReadU16(PacketReader *reader, uint16_t *out)
{
	uint16_t v;

	if (PacketReader_Remaining(reader) < 2)
		return 0;
	memcpy(&v, reader->buf + reader->off, sizeof(v));
	*out = ntohs(v);
	reader->off = (uint16_t)(reader->off + 2);
	return 1;
}

int
PacketReader_ReadU32(PacketReader *reader, uint32_t *out)
{
	uint32_t v;

	if (PacketReader_Remaining(reader) < 4)
		return 0;
	memcpy(&v, reader->buf + reader->off, sizeof(v));
	*out = ntohl(v);
	reader->off = (uint16_t)(reader->off + 4);
	return 1;
}

int
PacketReader_ReadBytesPtr(PacketReader *reader, const uint8_t **out, uint16_t len)
{
	if (PacketReader_Remaining(reader) < len)
		return 0;
	*out = reader->buf + reader->off;
	reader->off = (uint16_t)(reader->off + len);
	return 1;
}

int
PacketReader_ReadCStringCopy(PacketReader *reader, char *dst, size_t dstSize, uint16_t maxBytes)
{
	uint16_t remaining;
	uint16_t limit;
	uint16_t i;

	if (dstSize == 0)
		return 0;

	remaining = PacketReader_Remaining(reader);
	limit = remaining < maxBytes ? remaining : maxBytes;

	for (i = 0; i < limit; i++) {
		if (reader->buf[reader->off + i] == '\0') {
			if ((size_t)i >= dstSize)
				return 0;
			memcpy(dst, reader->buf + reader->off, i);
			dst[i] = '\0';
			reader->off = (uint16_t)(reader->off + i + 1);
			return 1;
		}
	}

	return 0;
}
