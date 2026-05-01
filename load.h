#ifndef LOAD_H_
#define LOAD_H_

void LoadAll_ArtDim(void); // 0x004DE4D7
void Terrain_LoadMap(void); // 0x004E1120
void LoadAll_Statics(void); // 0x004E1287
void LoadDynamicPhase(void); // 0x004E129B
void LoadAll_AnimData(void); // 0x004E12B9
void LoadAll_TemplateIdx(void); // 0x004E1432
void LoadAll_TiledataVersion(void); // 0x004E15B6
void Terrain_LoadTileData(void); // 0x004E1613
void LoadResTypes(void); // 0x004E190A
void LoadGumpDimTable(void); // 0x004E1BFA
void LoadItemRes(void); // 0x004E1DB3
void LoadAll_TemplateStartup(void); // 0x004E1F94
int ValidateRegionBounds(int x1, int y1, int x2, int y2, int worldX, int worldY, int worldEndX, int worldEndY); // 0x004E1FA3
void LoadResRegions(void); // 0x004E1FD1
void LoadAll_ObjScrBitmap(void); // 0x004E2447
void LoadAll_StartupScript(void); // 0x004E2506
void LoadAll_MultiManager(void); // 0x004E2560
void LoadAll_Skills(void); // 0x004E257B
void SaveAll_ItemRes(void); // 0x004E25BE
void SaveAll_ResTypes(void); // 0x004E2694
void SaveAll_ResBank(void); // 0x004E2753

#endif /* LOAD_H_ */
