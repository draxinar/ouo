#ifndef PACKET_SECURITY_H_
#define PACKET_SECURITY_H_

#include <stdint.h>

enum {
	PacketSecurityNeedMore = 0,
	PacketSecurityOk = 1,
	PacketSecurityBad = -1,
};

int PacketSecurity_DecodePacketSize(const uint16_t *packetTable, const uint8_t *buf, int buffered, uint16_t *packetLen, const char **reason);
int PacketSecurity_TextLengthFromPacket(uint16_t packetLen, uint16_t headerLen, uint16_t payloadOffset, uint16_t maxLen, uint16_t *textLen);

#endif /* PACKET_SECURITY_H_ */
