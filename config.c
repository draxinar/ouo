/*
 * CConfig - server runtime configuration.
 *
 * Parses the startup config file, populates the global config struct
 * (paths, tuning values, feature gates), and triggers the initial
 * setup of subsystems that depend on those values (uodemo.dat,
 * Feistel S-boxes, world paths).
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "containerhandle.h"
#include "io.h"
#include "main.h"
#include "region.h"
#include "time.h"
#include "wombat_exec.h"

CConfig g_Config;
int g_DebugGM = 0;
int g_DebugTest = 0;
int g_GainMultiplier = 1;

// 0x00698440 - shared line buffer filled by GetLine
static char lineBuffer[1024];
// 0x0061D130 - EOF sentinel compared against each GetLine result
static char *endMarker = "@@@ END @@@";

/*
 * 0x00467590 - Config_Constructor
 *
 * The binary also registers a CConfig destructor via atexit that calls
 * the MSVC CRT ifstream dtor; that cleanup has no Linux equivalent.
 */
void
Config_Constructor(void)
{
	Config_ConstructorBody();
}

/*
 * 0x0046759F - Config_ConstructorBody
 *
 * Delegates to CConfig_InitAll on the global g_Config instance.
 */
CConfig *
Config_ConstructorBody(void)
{
	return CConfig_InitAll(&g_Config);
}

/*
 * 0x00468343 - CConfig::InitAll
 *
 * Initializes the container-handle map, runs CConfig::Init, and seeds
 * srand with the current tick count.
 */
CConfig *
CConfig_InitAll(CConfig *this)
{
	CConfig *conf;

	conf = this;
	ContainerHandle_InitMap();
	CConfig_Init(conf);
	srand(GetTickCount_UO());
	return conf;
}

/*
 * 0x0046836D - CConfig::ReadLine
 *
 * Reads one line from f into a static 0x100-byte buffer, stopping at
 * newline or EOF and dropping carriage returns. Returns the buffer.
 *
 * There is no bound on the write, so a line longer than the buffer runs
 * past it. Reproduced as it stands - nothing calls this.
 */
static __attribute__((unused)) char *
CConfig_ReadLine(FILE *f)
{
	// 0x00698D90 - static line buffer returned to the caller
	static char lineBuf[0x100];
	char *dst;
	int ch;

	dst = lineBuf;
	while (!feof_ServerSide(f)) {
		ch = fgetc_ServerSide(f);
		if (ch == '\n')
			break;
		if (ch != '\r' && ch != -1) {
			*dst = (char)ch;
			dst++;
		}
	}
	*dst = '\0';
	return lineBuf;
}

/*
 * 0x004683CF - CConfig::~CConfig
 *
 * Registered via atexit; flushes and closes every container handle.
 */
void
CConfig_Destructor(CConfig *this)
{
	USED(this);
	ContainerHandle_ShutdownAll();
}

/*
 * 0x004688C1 - CConfig no-op
 *
 * Takes a string argument and does nothing.
 */
void
CConfig_NoOp_4688C1(const char *msg)
{
	USED(msg);
}

/*
 * 0x004688CE - GetLine
 *
 * Reads the next "<...>" tagged line from f into lineBuffer, returning
 * NULL on EOF or on the end-of-section marker.
 */
char *
GetLine(FILE *f)
{
	char *line;
	int i;
	int begin;
	int end;

	begin = 0;
	do {
		if (feof_ServerSide(f))
			break;
		begin = fgetc_ServerSide(f);
	} while (begin != '<');
	if (begin == '<') {
		for (i = 0; !feof_ServerSide(f); ++i) {
			end = fgetc_ServerSide(f);
			if (end == '>')
				break;
			lineBuffer[i] = end;
		}
		lineBuffer[i] = 0;
		if (strcmp(lineBuffer, endMarker))
			line = lineBuffer;
		else
			line = 0;
	} else
		line = 0;
	return line;
}

/*
 * 0x00468982 - Location_ZRangesOverlap
 *
 * True when two same-tile locations' vertical spans touch: a occupies
 * [a.z, a.z + aHeight] and b occupies [b.z, b.z + bHeight]. Returns 0
 * outright when the x/y do not match.
 */
static __attribute__((unused)) int
Location_ZRangesOverlap(CLocation *a, int aHeight, CLocation *b, int bHeight)
{
	if ((int16_t)a->x != (int16_t)b->x || (int16_t)a->y != (int16_t)b->y)
		return 0;

	if (a->z > b->z) {
		if (a->z <= b->z + bHeight)
			return 1;
	} else {
		if (b->z <= a->z)
			return 1;
		if (b->z <= a->z + aHeight)
			return 1;
	}
	return 0;
}

/*
 * 0x00468A1E - strdup (MSVC CRT)
 *
 * Duplicates src into a freshly malloc'd buffer.
 */
char *
strdup_ServerSide(const char *src)
{
	size_t len;
	char *buf;

	len = strlen(src) + 1;
	buf = (char *)OperatorNew(len);
	strcpy(buf, src);
	return buf;
}

/*
 * 0x00468A5C - CConfig::Init
 *
 * Loads server-side config (server.cfg) into the CConfig singleton,
 * seeding data-file paths and world dimensions from defaults when the
 * config file or individual keys are missing.
 */
int
CConfig_Init(CConfig *this)
{
	int isMultisW;
	int isTemplatesW;
	int isResourcesW;
	int isAnimDataW;
	int isHuesW;
	int isTiledataW;
	int isArtW;
	int isTerrainW;
	int isStaticsW;
	CConfig *conf;
	FILE *f;
	char *line;
	const char *serverName;
	char buf[256];
	char key[256];

	conf = this;
	serverName = getenv("SERVERNAME");
	if (!serverName)
		serverName = "UOGoldDemo";
	strcpy(conf->serverName, serverName);
	strcpy(conf->owner, "Default Owner");
	conf->port = 12346;
	conf->width = 1024;
	conf->height = 1024;
	conf->x = 0;
	conf->y = 0;
	conf->id = 0;
	conf->isStaticsW = 0;
	conf->isTerrainW = 0;
	conf->isArtW = 0;
	conf->isTiledataW = 0;
	conf->isHuesW = 0;
	conf->isResourcesW = 0;
	conf->isAnimDataW = 0;
	conf->isTemplatesW = 0;
	conf->isMultisW = 0;
	sprintf(key, "../.rundir/%s/server.txt", conf->serverName);
	f = fopen_ServerSide(key, "r");
	if (!f)
		exit(1);
	do {
		line = GetLine(f);
		if (line) {
			for (line = GetValue(line, key); *line && isspace(*line); ++line)
				;
			if (strcmp(key, "owner")) {
				if (strcmp(key, "id")) {
					if (strcmp(key, "port")) {
						if (strcmp(key, "x")) {
							if (strcmp(key, "y")) {
								if (strcmp(key, "width")) {
									if (strcmp(key, "height")) {
										if (strcmp(key, "statics")) {
											if (strcmp(key, "terrain")) {
												if (strcmp(key, "art")) {
													if (strcmp(key, "tiledata")) {
														if (strcmp(key, "hues")) {
															if (strcmp(key, "animdata")) {
																if (strcmp(key, "resources")) {
																	if (strcmp(key, "templates")) {
																		if (strcmp(key, "multis")) {
																			if (strcmp(key, "spawnclien"
																			                "t")) {
																				if (strcmp(key,
																				            "start"
																				            "x")) {
																					if (!strcmp(key,
																					            "starty"))
																						g_ConfigStartY =
																						        atoi(line);
																				} else {
																					g_ConfigStartX =
																					        atoi(line);
																				}
																			} else {
																				g_ConfigSpawnClient =
																				        atoi(line);
																			}
																		} else {
																			isMultisW = *line == 'W' ||
																			            *line == 'w';
																			conf->isMultisW = isMultisW;
																		}
																	} else {
																		isTemplatesW = *line == 'W' ||
																		               *line == 'w';
																		conf->isTemplatesW = isTemplatesW;
																	}
																} else {
																	isResourcesW = *line == 'W' || *line == 'w';
																	conf->isResourcesW = isResourcesW;
																}
															} else {
																isAnimDataW = *line == 'W' || *line == 'w';
																conf->isAnimDataW = isAnimDataW;
															}
														} else {
															isHuesW = *line == 'W' || *line == 'w';
															conf->isHuesW = isHuesW;
														}
													} else {
														isTiledataW = *line == 'W' || *line == 'w';
														conf->isTiledataW = isTiledataW;
													}
												} else {
													isArtW = *line == 'W' || *line == 'w';
													conf->isArtW = isArtW;
												}
											} else {
												isTerrainW = *line == 'W' || *line == 'w';
												conf->isTerrainW = isTerrainW;
											}
										} else {
											isStaticsW = *line == 'W' || *line == 'w';
											conf->isStaticsW = isStaticsW;
										}
									} else {
										conf->height = atoi(line);
									}
								} else {
									conf->width = atoi(line);
								}
							} else {
								conf->y = atoi(line);
							}
						} else {
							conf->x = atoi(line);
						}
					} else {
						conf->port = atoi(line);
					}
				} else {
					conf->id = atoi(line);
				}
			} else {
				strcpy(conf->owner, line);
			}
		}
	} while (line);
	fclose_ServerSide(f);
	sprintf(buf, "../.rundir/%s/map0.mul", conf->serverName);
	GLOBAL_file_map0_mul = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/staidx0.mul", conf->serverName);
	GLOBAL_file_staidx0_mul = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/staidx0.bkp", conf->serverName);
	GLOBAL_file_staidx0_bkp = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/statics0.mul", conf->serverName);
	GLOBAL_file_statics0_mul = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/statics0.bkp", conf->serverName);
	GLOBAL_file_statics0_bkp = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/dynidx0.mul", conf->serverName);
	GLOBAL_file_dynidx0_mul = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/dynidx0.bkp", conf->serverName);
	GLOBAL_file_dynidx0_bkp = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/dynamic0.mul", conf->serverName);
	GLOBAL_file_dynamic0_mul = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/dynamic0.bkp", conf->serverName);
	GLOBAL_file_dynamic0_bkp = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/tempidx.mul", conf->serverName);
	GLOBAL_file_tempidx_mul = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/temp.mul", conf->serverName);
	GLOBAL_file_temp_mul = strdup_ServerSide(buf);
	sprintf(buf, "../.rundir/%s/regions.txt", conf->serverName);
	GLOBAL_file_regions_txt = strdup_ServerSide(buf);
	InitPoolSizes();
	return 0;
}
