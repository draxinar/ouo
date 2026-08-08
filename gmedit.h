#ifndef GMEDIT_H_
#define GMEDIT_H_

#include <stdint.h>

__extension__ typedef struct CPlayer CPlayer;
__extension__ typedef struct CRegion CRegion;
void SendReadOnlyError(CPlayer *player, const char *name); // 0x004D64E0
int SetTerrainTile(intptr_t mode, int x, int y, int tileID, int elevation); // 0x004D653F
int SetTerrSingle(intptr_t mode, int x, int y, int tileID, int elevation); // 0x004D66CD
int StoreHues(CPlayer *player, int hueIndex, uint16_t *colors, uint16_t startColor, uint16_t endColor, char *name); // 0x004D6841
int ArtTile_WriteEntry(CPlayer *player, int artId, int dataLen, uint8_t *data); // 0x004D6AF6
int StoreLandTileData(CPlayer *player, int tileIndex, uint32_t flags, uint16_t textureID, char *name); // 0x004D6C1F
int StoreItemTileData(CPlayer *player, int itemIndex, uint32_t flags, uint8_t weight, uint8_t layer, uint32_t miscData, uint16_t value1, uint16_t value2, uint16_t height,
        uint8_t quantity, char *name); // 0x004D6D1B
int AnimData_UpdateEntry(CPlayer *player, int animIndex, uint8_t *data); // 0x004D6E92
int MapBlock_ReadEntry(CPlayer *player, CRegion *region); // 0x004D716B
void World_SuppressUpdates(void); // 0x004D721C
void World_RestoreUpdates(void); // 0x004D7238
void Player_StepInDirection(CItem *entity, int direction); // 0x004D73B0

#endif /* GMEDIT_H_ */
