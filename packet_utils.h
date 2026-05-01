#ifndef PACKET_UTILS_H_
#define PACKET_UTILS_H_

#include <stddef.h>
#include <stdint.h>

enum {
	PacketDynamicSize = 0x8000,
};

extern uint16_t g_PacketTable[]; // 0x0061F138
extern const size_t g_PacketTableSize;
extern uint16_t g_PacketOffset;
extern uint16_t g_PacketSize;
extern uint16_t g_PacketTable_12535[];

uint16_t PutPacketType(uint8_t *buf, uint8_t packetType, int size); // 0x004923FC
uint32_t *GetByte(uint8_t *buf, uint32_t *off, uint8_t *v); // 0x0049244C
uint32_t *GetWord(uint8_t *buf, uint32_t *off, uint16_t *v); // 0x00492481
uint32_t *GetDWord(uint8_t *buf, uint32_t *off, uint32_t *v); // 0x00492515
uint32_t *GetString(uint8_t *buf, uint32_t *off, char **str, int len); // 0x0049255F
void GetNullTermString(uint8_t *buf, uint32_t *off, char **str); // 0x00492593
uint16_t PutByte(uint8_t *buf, uint8_t v); // 0x004925D6
uint16_t PutWord(uint8_t *buf, ...); // 0x00492610
uint16_t PutDWord(uint8_t *buf, ...); // 0x004926B0
uint16_t PutString(uint8_t *buf, char *str, int len); // 0x00492700
uint32_t htonl_inplace(uint32_t *v); // 0x0049DA00
uint16_t htons_inplace(uint16_t *v); // 0x0049DA20
uint32_t ntohl_inplace(uint32_t *v); // 0x0049DA40
uint16_t ntohs_inplace(uint16_t *v); // 0x0049DA60
void WriteInt32LE(char **cursor, int32_t value); // 0x004B38E6
void WriteInt16LE(char **cursor, int16_t value); // 0x004B39D2
int32_t ReadInt32LE(char **cursor); // 0x004B3A48
int16_t ReadInt16LE(char **cursor); // 0x004B3A88

#endif /* PACKET_UTILS_H_ */
