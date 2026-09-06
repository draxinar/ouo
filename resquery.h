#ifndef RESQUERY_H_
#define RESQUERY_H_

#include <stdint.h>

__extension__ typedef struct CPlayer CPlayer;
void HandlePacket_ResourceQuery(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004B3B0F
void HandlePacket_SendResources(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004B5043

#endif /* RESQUERY_H_ */
