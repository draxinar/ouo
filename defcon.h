#ifndef DEFCON_H_
#define DEFCON_H_

__extension__ typedef struct CDefcon CDefcon;

/*
 * Server load limiter (0x34 bytes; g_Defcon at 0x00699968). Blocks
 * creature spawns once g_NormalNPCCount exceeds maxNPCCount or the
 * player count crosses one of the defconN player thresholds.
 */
struct CDefcon {
	int maxNPCCount;                // 0x00 - default 2800
	int npcKillCount;               // 0x04 - default 100
	int defcon4PThreshold;          // 0x08 - default 1000
	int defcon4Cooldown;            // 0x0C - default 22
	int defcon4NPCThreshold;        // 0x10 - default 75
	int defcon1PThreshold;          // 0x14 - default 700
	int defcon3PThreshold;          // 0x18 - default 900
	int defcon5PThreshold;          // 0x1C - default 1100
	int defcon2PThreshold;          // 0x20 - default 800
	int defcon4Timer;               // 0x24
	int defcon1Active;              // 0x28 - default 1
	int defcon3Active;              // 0x2C
	int defcon2Active;              // 0x30
};

extern CDefcon g_Defcon; // 0x00699968

void CDefcon_Init(void); // 0x00436AC0
void CDefcon_Update(void); // 0x00436B4F
int CDefcon_GetMoveRate(CDefcon *defcon); // 0x00436B82
int CDefcon_IsFull(void); // 0x00436BDB
void Defcon_TeleportStorm(int count); // 0x004D7D4A

#endif /* DEFCON_H_ */
