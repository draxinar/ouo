#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

#include "packet_security.h"
#include "packet_utils.h"

int
PacketSecurity_DecodePacketSize(const uint16_t *packetTable, const uint8_t *buf, int buffered, uint16_t *packetLen, const char **reason)
{
	uint8_t type;
	uint16_t tableSize;

	if (buffered <= 0) {
		if (reason)
			*reason = "no packet bytes buffered";
		return PacketSecurityNeedMore;
	}

	type = buf[0];
	if (type >= 0xE3) {
		if (reason)
			*reason = "unsupported packet type";
		return PacketSecurityBad;
	}

	tableSize = packetTable[5 * type];
	if (tableSize & PacketDynamicSize) {
		uint16_t v;

		if (buffered < 3) {
			if (reason)
				*reason = "incomplete dynamic packet header";
			return PacketSecurityNeedMore;
		}
		memcpy(&v, buf + 1, sizeof(v));
		v = ntohs(v);
		if (v < 3) {
			if (reason)
				*reason = "dynamic packet length smaller than header";
			return PacketSecurityBad;
		}
		*packetLen = v;
		return PacketSecurityOk;
	}

	if (tableSize == 0) {
		if (reason)
			*reason = "zero-sized fixed packet";
		return PacketSecurityBad;
	}

	*packetLen = tableSize;
	return PacketSecurityOk;
}

int
PacketSecurity_TextLengthFromPacket(uint16_t packetLen, uint16_t headerLen, uint16_t payloadOffset, uint16_t maxLen, uint16_t *textLen)
{
	uint32_t absoluteOffset;
	uint32_t len;

	absoluteOffset = (uint32_t)headerLen + payloadOffset;
	if (absoluteOffset > packetLen)
		return 0;

	len = (uint32_t)packetLen - absoluteOffset;
	if (len > maxLen)
		return 0;

	*textLen = (uint16_t)len;
	return 1;
}
