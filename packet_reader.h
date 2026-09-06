#ifndef PACKET_READER_H_
#define PACKET_READER_H_

#include <stddef.h>
#include <stdint.h>

typedef struct PacketReader {
	const uint8_t *buf;
	uint16_t len;
	uint16_t off;
	uint16_t headerLen;
} PacketReader;

void PacketReader_Init(PacketReader *reader, const uint8_t *buf, uint16_t packetLen, uint16_t headerLen);
uint16_t PacketReader_Remaining(const PacketReader *reader);
uint16_t PacketReader_Offset(const PacketReader *reader);
int PacketReader_Skip(PacketReader *reader, uint16_t len);
int PacketReader_ReadU8(PacketReader *reader, uint8_t *out);
int PacketReader_ReadU16(PacketReader *reader, uint16_t *out);
int PacketReader_ReadU32(PacketReader *reader, uint32_t *out);
int PacketReader_ReadBytesPtr(PacketReader *reader, const uint8_t **out, uint16_t len);
int PacketReader_ReadCStringCopy(PacketReader *reader, char *dst, size_t dstSize, uint16_t maxBytes);

#endif /* PACKET_READER_H_ */
