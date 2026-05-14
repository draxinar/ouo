#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>
#include <stdio.h>

__extension__ typedef struct CConfig CConfig;
__extension__ typedef struct CConfig_vtable CConfig_vtable;

/*
 * Global server configuration. The binary's singleton g_Config lives at
 * 0x006982F0; the width/height/x/y fields at +0x30..+0x3C overlap the
 * g_mapWidth..g_mapStartY globals at 0x00698320..0x0069832C.
 */
// clang-format off
struct CConfig {
	CConfig_vtable *vtable;
	uint32_t isStaticsW;
	uint32_t isTerrainW;
	uint32_t isArtW;
	uint32_t isTiledataW;
	uint32_t isHuesW;
	uint32_t isResourcesW;
	uint32_t isAnimDataW;
	uint32_t isTemplatesW;
	uint32_t isMultisW;
	uint32_t id;
	uint32_t port;
	uint32_t width;                 // 0x30
	uint32_t height;                // 0x34
	uint32_t x;                     // 0x38
	uint32_t y;                     // 0x3C
	char serverName[128];           // 0x40
	char owner[128];                // 0xC0
	uint32_t borderNorth;           // 0x140 - map edge active flags
	uint32_t borderWest;            // 0x144
	uint32_t borderSouth;           // 0x148
	uint32_t borderEast;            // 0x14C
};
// clang-format on

/*
 * CConfig vtable. The binary has no virtual methods; we keep the pointer
 * for layout compatibility.
 */
struct CConfig_vtable {
	char unused;
};

/*
 * One entry in the named-resource table at 0x00615390 (12 bytes each).
 * Loaded at startup by NamedResource_LoadAll and searched by
 * NamedResource_Find.
 */
__extension__ typedef struct NamedResource NamedResource;
struct NamedResource {
	char *name;     // 0x00
	char *path;     // 0x04
	char *data;     // 0x08 - NULL until loaded
};

extern CConfig g_Config;
extern int g_DebugGM;
extern int g_DebugTest;
extern int g_GainMultiplier;

void Config_Constructor(void); // 0x00467590
CConfig *Config_ConstructorBody(void); // 0x0046759F
CConfig *CConfig_InitAll(CConfig *this); // 0x00468343
void CConfig_NoOp_4688C1(const char *msg); // 0x004688C1
char *GetLine(FILE *f); // 0x004688CE
int CConfig_Init(CConfig *this); // 0x00468A5C

#endif /* CONFIG_H_ */
