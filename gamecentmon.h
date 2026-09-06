#ifndef GAMECENTMON_H_
#define GAMECENTMON_H_

#include <stdint.h>

__extension__ typedef struct CPlayer CPlayer;
void GameCentMon_TickAllNPCs(void); // 0x004B37D1
void GameCentMon_TickTemplateChains(int templateId); // 0x004B3AC9
int GameCentMon_CalcZ(int x, int y); // 0x004B79BE
void HandlePacket_GameCentMon(CPlayer *this, uint8_t *buf, uint16_t packetLen); // 0x004B7B47
void LoadAll_LVNData(void); // 0x004B8241
void GameCentMon_CheckTimeout(void); // 0x004B8750
void GameCentMon_BroadcastEvent(char *data, int len); // 0x004B8B25

#endif /* GAMECENTMON_H_ */
