/*
 * main - CLI parsing, boot sequence, and the outer server tick loop.
 *
 * Walks command line options, loads all data files, opens the
 * listen socket, then runs the fixed-interval tick that drains
 * packets, advances timers, and flushes saves.
 * Signal handlers route SIGINT/SIGTERM into a clean shutdown.
 */

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat.h"

#include "account.h"
#include "blockmanager.h"
#include "chat.h"
#include "container.h"
#include "containerhandle.h"
#include "dynamic.h"
#include "egg.h"
#include "entitymanager.h"
#include "feature.h"
#include "item.h"
#include "listensocket.h"
#include "load.h"
#include "magicfactory.h"
#include "main.h"
#include "multi.h"
#include "nodepool.h"
#include "region.h"
#include "resource_entity.h"
#include "skill.h"
#include "socket.h"
#include "taglist.h"
#include "time.h"
#include "timer.h"
#include "usersock.h"
#include "version.h"
#include "vtable.h"
#include "watchdog.h"
#include "weapon.h"
#include "wombat_compile.h"
#include "wombat_exec.h"
#include "world.h"

#ifndef OUO_VERSION
#define OUO_VERSION "unknown"
#endif

static void StaticInit_WeaponManager(void); // 0x00467BC6
static void shutdown_handler(int sig);

static void parseargs(int argc, char **argv);

/*
 * Tick interval in milliseconds.
 * The binary's regen timers indicate ~4 ticks/second:
 *   mana regen = 20 ticks (5 sec), HP regen = 30-80 ticks (7.5-20 sec),
 *   stamina recovery = ~12 ticks (3 sec) per point when standing still.
 */
#define TICK_INTERVAL_MS 250

// Shutdown flag set by signal handler.
volatile sig_atomic_t g_shutdown; // 0x006999E0

// Custom - SIGHUP reload flag consumed by the main loop
static volatile sig_atomic_t g_reloadAccounts;

/*
 * Custom: auto-initial-spawn timer. Set at startup to enable g_IsInitialSpawn
 * for 60 seconds so the world populates with creatures without needing a
 * GameCentral admin command. 0 = inactive, nonzero = GetTickCount_UO deadline.
 */
uint32_t g_AutoInitialSpawnDeadline;

// 0x00699A04 - increments once per server tick.
// Used by DeathCountDecay (threshold 1200 ticks = ~5 min at 250ms/tick).
uint32_t g_GameTick;

// Timing statistics globals (binary .bss). Declared extern in log.h.
uint32_t g_TimingBuffer[128]; // 0x00699710 - circular buffer of tick elapsed times
uint32_t g_TimingBufIndex;    // 0x006999E8 - write index into circular buffer
uint32_t g_TimingField9EC;    // 0x006999EC - average tick time across buffer
uint32_t g_TimingField9F0;    // 0x006999F0 - peak tick time within buffer
uint32_t g_LastTickElapsed;   // 0x006999F4 - last tick elapsed time (ms)
uint32_t g_TimingTotalTime;   // 0x006999F8 - accumulated tick processing time
uint32_t g_TimingTickCount;   // 0x006999FC - number of timing samples
uint32_t g_TimingPeakTime;    // 0x00699A00 - peak tick processing time

// Stat/skill curve parameters (initialized by InitStatCurve/InitSkillCurve).
QuadCurve g_StatCurve;         // 0x0061C1E0 - stat reduction probability curve
QuadCurve g_SkillCurve;        // 0x0061C210 - skill advancement probability curve
double g_StatCurveBase;        // 0x00699A58 - base y-offset for stat curve segment 1
double g_SkillCurveBase;       // 0x00699A60 - base y-offset for skill curve segment 1

// 0x0063E168 - loading complete flag, set to 1 at end of CBlockManager_LoadAll.
uint32_t g_LoadingComplete; // 0x0063E168

// 0x0064B360 - hue data array (1024 entries of 0x58 bytes).
// HueEntry typedef in terrain.h.
HueEntry *g_HueData;

// 0x0064B370 - expanded hue data array (1024 entries of 0x94 bytes).
// HueEntryExpanded typedef in terrain.h.
HueEntryExpanded *g_HueDataExp;

// 0x00698304 - hue data loaded flag.
int g_HueDataLoaded;

// 0x006F0604 - hue file base offset for MapFileManager.
uint32_t g_HueFileOffset;

// 0x006982FC - art files loaded flag (never set in binary, always 0).
int g_ArtLoaded;

// 0x00698300 - tiledata loaded flag.
int g_TileDataLoaded;

// 0x006F05E8 - tiledata file base offset for MapFileManager.
uint32_t g_TileDataFileOffset;

// Serial pool partition globals (binary .bss at 0x6F05xx).
// Set by InitPoolSizes (0x004DE360), called from CBlockManager_Setup and CConfig_Init.
uint32_t g_PoolSizeItems;       // 0x006F05F4 - item serial pool size (0x00049248)
uint32_t g_PoolSizeNPCs;        // 0x006F0600 - NPC serial pool size (0x00010000)
uint32_t g_PoolSizeField_D0;    // 0x006F05D0 - pool section size (0x400)
uint32_t g_PoolSizeField_D8;    // 0x006F05D8 - pool section size (0x800)
uint32_t g_PoolSizeField_0C;    // 0x006F060C - pool section size (0x80)
uint32_t g_PoolSizeField_D4;    // 0x006F05D4 - pool section size (0x2000)
uint32_t g_PoolSizeField_E0;    // 0x006F05E0 - pool section size (0x32)
uint32_t g_PoolSizeField_10;    // 0x006F0610 - pool section size (0x1000)
uint32_t g_PoolBlockCount;      // 0x006F05F0 - gridW * gridH block count
uint32_t g_PoolBlockCount2;     // 0x006F05C8 - same as block count
uint32_t g_PoolTotalSerials;    // 0x006F0608 - total serial pool size
uint32_t g_PoolBaseItems;       // 0x006F05DC - base offset for items
uint32_t g_PoolBaseField_E4;    // 0x006F05E4 - base offset for section 3
uint32_t g_PoolBaseField_EC;    // 0x006F05EC - base offset for section 5
uint32_t g_PoolBaseField_F8;    // 0x006F05F8 - base offset for section 6
uint32_t g_PoolBaseField_C4;    // 0x006F05C4 - base offset for section 7
uint32_t g_PoolBaseField_CC;    // 0x006F05CC - base offset for section 8
uint32_t g_PoolBaseField_FC;    // 0x006F05FC - base offset for section 9

// 0x0064B35C - animation data array (16384 entries of 0x44 bytes).
// Typedef in terrain.h.
AnimDataEntry *g_AnimData;

// 0x0069830C - AnimData loaded flag. Set after animdata.mul is read.
int g_AnimDataLoaded;

// 0x00621998 - global buffer for LVN region name (binary .bss).
char g_LVNDestBuf[256];

/*
 * 0x00467BC6 - StaticInit_WeaponManager
 *
 * Static initializer for g_WeaponManager.
 */
static __attribute__((unused)) void
StaticInit_WeaponManager(void)
{
	CWeaponManager_Constructor(&g_WeaponManager);
}

/*
 * 0x00467FF0 - Server_Loop
 *
 * Phase-A setup, main tick loop, phase-C shutdown. Layout follows the
 * binary's 0x00467FF0 instruction order so that every call in phase A
 * lines up with its binary counterpart or a documented CUSTOM skip.
 *
 * MODIFIED: Linux select() replaces the binary's g_SocketManager vtable
 * dispatch and Sleep(0); listen socket uses CListenSocket (ORPHANED in
 * the binary); shutdown saves world state (no binary equivalent).
 * FIXED: 10-second warning threshold (binary has > 0xA).
 *
 * CUSTOM skips (Win32-only, no Linux equivalent): dialog HWND spin-wait,
 * client-path registry read, two CCriticalSection allocations,
 * SpawnClientThread, ShowWindow calls.
 */
int
Server_Loop(void)
{
	CListenSocket *listenSocket;
	uint32_t lastPeriodicTime, lastTickTime, now;
	uint32_t beforeTick, afterTick;
	int i;

	g_GameTick = GetTickCount_UO();

	signal(SIGTERM, shutdown_handler);
	signal(SIGINT, shutdown_handler);

	Noop_47DF20("startup");

	lastPeriodicTime = GetTickCount_UO();

	// 0x00468051: while ([0x6efec0]==0) Sleep(0) - Win32 dialog HWND wait.
	// CUSTOM: skipped - no debug dialog on Linux.

	// 0x00468064: FindClientDataPath(&localBuf) - Win32 registry read.
	// CUSTOM: skipped - no registry on Linux.

	// 0x00468073: [0x8ce228] = 0x7f000001 - localhost literal for Win32
	// client launcher. CUSTOM: skipped.

	// 0x0046807D / 0x00468088: operator new(32) + CCriticalSection_Constructor
	// into [0x701658] and [0x70165c]. CUSTOM: skipped - single-threaded
	// Linux; CCriticalSection_Lock/Unlock stubs in stl.c are no-ops.

	CDebugConsole_AppendText("Initializing...");
	ProgressBar_Update(-1);
	Socket_Init();
	CBlockManager_Setup(&g_SpatialGrid, g_mapWidth, g_mapHeight);

	// CUSTOM: Linux accept path. The binary's main loop polls
	// g_SocketManager->vtable[1/2] populated by Socket_Init;
	// CListenSocket_Constructor_Listen is ORPHANED in the binary and
	// serves as our POSIX equivalent.
	listenSocket = malloc(sizeof(*listenSocket));
	if (listenSocket == NULL)
		return -1;
	CListenSocket_Constructor_Listen(listenSocket, g_ServerPort);
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "listen on port %d", listenSocket->port);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "server", "misc", buf);
	}

	for (i = 0; i < 128; i++)
		g_TimingBuffer[i] = 0;

	Noop_467FEB();

	lastPeriodicTime = GetTickCount_UO();
	lastTickTime = GetTickCount_UO();

	// 0x004680CB: SpawnClientThread(&localBuf) - Win32-only. CUSTOM: skipped.
	// 0x004680DA: ShowWindow(SW_HIDE)          - Win32-only. CUSTOM: skipped.

	while (!g_shutdown) {
		g_GameTick = GetTickCount_UO();

		if (g_reloadAccounts) {
			g_reloadAccounts = 0;
			Account_ReloadAll();
		}

		if (g_GameTick - lastPeriodicTime >= 0x258) {
			lastPeriodicTime = g_GameTick;
		}

		// MODIFIED: binary (0x00468242) polls g_SocketManager's vtable;
		// Server_Select dispatches to epoll_wait (Linux) or poll (other
		// POSIX), called with a 0 timeout here for a non-blocking check.
		Server_Select(0);

		now = GetTickCount_UO();
		if (lastTickTime + TICK_INTERVAL_MS <= now) {
			RecordTickTiming(lastTickTime);
			AccumulateTimingStats();
			lastTickTime = now;

			beforeTick = GetTickCount_UO();
			CTimeManager_Update();
			afterTick = GetTickCount_UO();

			// CUSTOM: watchdog heartbeat - one volatile store telling
			// the watchdog thread the main loop is still ticking.
			Watchdog_Heartbeat();

			// FIXED: binary has > 0xA (10ms) but logs "> 10 seconds".
			// Wrong constant - should be 10000 (10s) to match message.
			if (afterTick - beforeTick > 10000) {
				EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "timing", "error", "CTimeManager::Update() took > 10 seconds.");
			}
		}

		// MODIFIED: binary (0x00468306) Sleep(0)s to yield; we block in
		// epoll_wait/poll for the remaining tick interval instead of
		// busy-looping.
		{
			uint32_t elapsed, remainUs;

			elapsed = GetTickCount_UO() - lastTickTime;
			if (elapsed >= TICK_INTERVAL_MS)
				remainUs = 0;
			else
				remainUs = (TICK_INTERVAL_MS - elapsed) * 1000;
			Server_Select((int32_t)remainUs);
		}
	}

	// Phase C: binary shutdown at 0x00468313.
	// 0x00468313: ShowWindow(SW_NORMAL) - Win32-only. CUSTOM: skipped.
	CDebugConsole_AppendText("Exiting...");

	// CUSTOM: world-state persistence (binary has no disk-save path).
	Watchdog_Shutdown();
	BackupFile(GLOBAL_file_dynidx0_mul, GLOBAL_file_dynidx0_bkp);
	BackupFile(GLOBAL_file_dynamic0_mul, GLOBAL_file_dynamic0_bkp);
	SaveDynamic0();
	Account_SaveAll();
	// CUSTOM (FEAT_CLOSED_ECONOMY): final bank snapshot on clean exit.
	SaveAll_ResBank();
	ContainerHandle_ShutdownAll();

	// CUSTOM: shutdown-only cleanup walkers (no binary equivalent).
	// Release per-entity timer chains, tag lists, and tracking pool
	// nodes that the binary would leak at process exit - for both the
	// live world hash (World_ShutdownEntities) and the dead-entity
	// archive that logged-out players are parked in
	// (EntityManager_ShutdownArchive) - free the WombatArray entries
	// still held in g_WombatArrays, and release the CString / CUString
	// allocations the parser left orphaned inside script ResultNodes,
	// so the valgrind exit report stays focused on real leaks.
	World_ShutdownEntities();
	EntityManager_ShutdownArchive();
	Wombat_ShutdownArrays();
	Wombat_FreeParserStrings();

	Socket_Shutdown();

	// CUSTOM: end valgrind tracking of every custom pool allocator
	// so chained freelist nodes become traversable by the leak
	// detector. No-op when VALGRIND is not defined.
	Multi_DestroyPools();
	Socket_DestroyPools();
	TagList_DestroyPools();
	Timer_DestroyPools();
	Item_DestroyPools();
	NodePool_DestroyAllPools();
	ResourceEntity_DestroyPools();
	WombatCompile_DestroyPools();
	Wombat_FreeNodePoolBlocks();
	Wombat_FreeHandlerTriplets();

	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "server", "misc", "shutdown complete");
	return 0;
}

/*
 * 0x00469472 - main
 *
 * MODIFIED: Linux entry point. The binary's entry is WinMain (__stdcall,
 * 4 args: hInstance, hPrevInstance, lpCmdLine, nCmdShow) - its argument
 * globals at 0x6990AC / 0x63F950-95C are unused here.
 *
 * Server_Init stands in for MSVC's _initterm(_xc_a, _xc_z), which runs
 * the 128-entry C++ static-init table (0x606000-0x60620C) before the
 * binary reaches WinMain. Server_Loop itself handles the phase-A setup
 * the binary does inline in 0x00467FF0.
 *
 * OMITTED WinMain allocations, all Win32-only with no Linux equivalent:
 *   - CDebugConsole::CDebugConsole (0x004D02E0, 16 bytes) - riched32
 *     dialog + message-pump thread for a GUI debug window.
 *   - Two CCriticalSection objects (0x004E7200, 32 bytes each) - single-
 *     thread no-ops; the lock/unlock stubs in stl.c cover the use sites.
 */
int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);

	parseargs(argc, argv);

	if (Server_Init() < 0) {
		fprintf(stderr, "init: error\n");
		return 1;
	}
	if (Server_Loop() < 0) {
		fprintf(stderr, "server: error\n");
		return 1;
	}
	return 0;
}

/*
 * 0x004D06E6 - CDebugConsole::AppendText
 *
 * Updates the status-text label of the CDebugConsole splash dialog.
 * In the binary:
 *   1. if the dialog HWND at [0x006EFEC0] is 0 (dialog not yet created
 *      by the message-pump thread), return immediately.
 *   2. strcpy the arg into a fixed scratch buffer at [0x006EF6C0].
 *   3. SendMessageA(hDlg, WM_USER+2 (0x402), 0, &buffer); the dialog
 *      proc at 0x004D0455 handles WM_USER+2 by calling SetWindowTextA
 *      on child control 1004 (the status label).
 * Only two callers in the binary, both in Server_Loop: "Initializing..."
 * at 0x00468134 and "Exiting..." at 0x00468326.
 *
 * MODIFIED: Linux has no CDebugConsole splash dialog - the whole
 * subsystem is a Win32 riched32 GUI thread with a CreateDialogParamA
 * template, so SendMessageA would have no receiver. Rather than drop
 * the text, we route it through EventLogger_Log to keep the message
 * visible on the console/log; the original scratch buffer at 0x6EF6C0
 * and the WM_USER+2 post are skipped because they have no effect
 * without the dialog HWND. This preserves the "Initializing..." and
 * "Exiting..." log lines that Server_Loop has always produced, now
 * flowing through the binary-equivalent helper instead of an open-
 * coded EventLogger_Log call at each site.
 */
void
CDebugConsole_AppendText(const char *msg)
{
	EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "server", "misc", msg);
}

/*
 * 0x004D0720 - ProgressBar_Update
 *
 * MODIFIED: binary posts a WM_USER+1 to the progress-bar HWND;
 * Linux has no GUI, so this is a no-op.
 */
void
ProgressBar_Update(int percent)
{
	USED(percent);
}

/*
 * 0x004DE360 - InitPoolSizes
 *
 * Sets the serial-pool partition sizes and their cumulative base
 * offsets within the MapFileManager .mul file.
 */
void
InitPoolSizes(void)
{
	g_PoolSizeItems = 0x00049248;
	g_PoolSizeNPCs = 0x00010000;
	g_PoolSizeField_D0 = 0x400;
	g_PoolSizeField_D8 = 0x800;
	g_PoolSizeField_0C = 0x80;
	g_PoolSizeField_D4 = 0x2000;
	g_PoolSizeField_E0 = 0x32;
	g_PoolSizeField_10 = 0x1000;

	// Compute block count: (mapWidth / 8) * (mapHeight / 8)
	// Binary uses MSVC signed division idiom: (x + (x < 0 ? 7 : 0)) >> 3
	g_PoolBlockCount = (g_mapWidth / 8) * (g_mapHeight / 8);
	g_PoolBlockCount2 = g_PoolBlockCount;

	// Total serial pool size
	g_PoolTotalSerials = g_PoolSizeItems + g_PoolSizeNPCs + g_PoolSizeField_D0 + g_PoolSizeField_D8 + g_PoolSizeField_0C + g_PoolSizeField_D4 + g_PoolSizeField_E0 +
	                     g_PoolSizeField_10 + g_PoolBlockCount + g_PoolBlockCount2;

	// Cumulative base offsets for each section
	g_PoolBaseItems = g_PoolSizeItems;
	g_TileDataFileOffset = g_PoolBaseItems + g_PoolSizeNPCs;
	g_PoolBaseField_E4 = g_TileDataFileOffset + g_PoolSizeField_D0;
	g_HueFileOffset = g_PoolBaseField_E4 + g_PoolSizeField_D8;
	g_PoolBaseField_EC = g_HueFileOffset + g_PoolSizeField_0C;
	g_PoolBaseField_F8 = g_PoolBaseField_EC + g_PoolSizeField_D4;
	g_PoolBaseField_C4 = g_PoolBaseField_F8 + g_PoolSizeField_E0;
	g_PoolBaseField_CC = g_PoolBaseField_C4 + g_PoolSizeField_10;
	g_PoolBaseField_FC = g_PoolBaseField_CC + g_PoolBlockCount;
}

static void
shutdown_handler(int sig)
{
	USED(sig);
	g_shutdown = 1;
}

static void
reload_handler(int sig)
{
	USED(sig);
	g_reloadAccounts = 1;
}

/*
 * Custom - Server_Init
 *
 * Linux surrogate for MSVC's _initterm(_xc_a, _xc_z), which runs the 128
 * C++ static-init constructors before WinMain in the binary. On Linux
 * we have no xc table, so the equivalent constructors are invoked here.
 * Also handles Linux-only signal ignores (SIGPIPE) and the SIGHUP
 * account-reload handler. Binary-phase signals (SIGTERM, SIGINT) live
 * at the top of Server_Loop instead.
 */
int
Server_Init(void)
{
	// CUSTOM: Linux-only signal setup.
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, reload_handler);

	// CUSTOM: Linux needs explicit vtable init (binary uses MSVC C++ vtables).
	VT_Init();

	// MSVC xc-table constructors (run pre-WinMain in the binary).
	WombatArrays_StaticInit();
	Config_Constructor();
	CWeaponManager_Constructor(&g_WeaponManager);
	CMagicItemFactory_Constructor(&g_MagicItemFactory);
	CLightManager_Constructor();
	CRotationTable_Constructor(&g_RotationTable);
	RegionManager_Constructor();

	// CUSTOM: populate light tables (BSS-zeroed in binary, never written).
	LightTables_Init();

	// CUSTOM: chat system - seed the default conference.
	if (feat(FEAT_CHAT))
		Chat_Init();

	// CUSTOM: watchdog subsystem.
	Watchdog_Init();

	return 0;
}

static void
parseargs(int argc, char **argv)
{
	int clientVer = CLIENT_126_AUTO;
	int nocrypt = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-client") == 0 && i + 1 < argc) {
			const char *v = argv[i + 1];
			if (strcmp(v, "t2a") == 0) {
				clientVer = CLIENT_126_AUTO;
			} else if (strcmp(v, "12536h") == 0) {
				clientVer = CLIENT_12536;
			} else {
				const ClientVersionInfo *info = Version_FindByArg(v);
				if (info)
					clientVer = info->clientEnum;
				else
					fprintf(stderr, "unknown client version: %s\n", v);
			}
			i++;
		} else if (strcmp(argv[i], "-nocrypt") == 0) {
			nocrypt = 1;
		} else if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-addr") == 0) && i + 1 < argc) {
			struct in_addr inaddr;
			if (inet_pton(AF_INET, argv[i + 1], &inaddr) == 1)
				memcpy(g_ServerAddr, &inaddr, 4);
			else
				fprintf(stderr, "invalid address: %s\n", argv[i + 1]);
			i++;
		} else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "-port") == 0) && i + 1 < argc) {
			int p = atoi(argv[i + 1]);
			if (p > 0 && p <= 65535)
				g_ServerPort = (uint16_t)p;
			else
				fprintf(stderr, "invalid port: %s\n", argv[i + 1]);
			i++;
		} else if (strcmp(argv[i], "-gm") == 0) {
			g_DebugGM = 1;
		} else if (strcmp(argv[i], "-test") == 0) {
			g_DebugTest = 1;
		} else if (strcmp(argv[i], "-fast") == 0 && i + 1 < argc) {
			int m = atoi(argv[i + 1]);
			if (m >= 1 && m <= 100)
				g_GainMultiplier = m;
			else
				fprintf(stderr, "invalid fast multiplier: %s (1-100)\n", argv[i + 1]);
			i++;
		} else if (strcmp(argv[i], "-watchdog") == 0 && i + 1 < argc) {
			int ms = atoi(argv[i + 1]);
			if (ms >= 1000) {
				g_WatchdogEnabled = 1;
				g_WatchdogTimeoutMs = (uint32_t)ms;
			} else {
				fprintf(stderr, "invalid watchdog timeout: %s (min 1000ms)\n", argv[i + 1]);
			}
			i++;
		} else if (strcmp(argv[i], "-features") == 0 && i + 1 < argc) {
			Features_Parse(argv[i + 1]);
			i++;
		} else if (strcmp(argv[i], "-spawn-cooldown") == 0 && i + 1 < argc) {
			int s = atoi(argv[i + 1]);
			if (s >= 0 && s <= 86400) {
				g_PerNPCRespawnDelayMs = s * 1000;
			} else {
				fprintf(stderr, "invalid spawn-cooldown: %s (seconds, 0-86400)\n", argv[i + 1]);
			}
			i++;
		} else if (strcmp(argv[i], "-version") == 0) {
			printf("ouo %s\n", OUO_VERSION);
			exit(0);
		} else if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("usage: ouo [options]\n"
			       "  -client VERSION  client protocol version (default: t2a auto-detect)\n"
			       "  -nocrypt          force encryption off\n"
			       "  -a IP             server address for relay packets\n"
			       "  -p PORT           listen port (default: 2593)\n"
			       "  -gm               enable GM mode for all players\n"
			       "  -test             enable Test Center mode for all players\n"
			       "  -fast N           skill/stat gain multiplier (1-100)\n"
			       "  -watchdog MS      enable infinite-loop watchdog (timeout ms, min 1000)\n"
			       "  -features LIST    feature flags (all, none, or comma-separated)\n"
			       "  -spawn-cooldown N per-NPC respawn delay in seconds (default 1024)\n"
			       "  -version          print version and exit\n"
			       "  -help             print this help and exit\n");
			exit(0);
		}
	}

	Version_InitPacketTables();
	SetClientVersion(clientVer);
	if (nocrypt) {
		g_NoCrypt = 1;
		g_UseEncryption = 0;
		g_UseBlowfish = 0;
		// g_AutoDetect stays as-is: nocrypt + auto-detect coexist
		// for clients >= 1.26 (identified via CLIENT_VERSION 0xBD).
	}

	{
		char buf[256];
		int n = 0;

		n += snprintf(buf + n, sizeof(buf) - n, "version=%s ", OUO_VERSION);
		if (g_AutoDetect) {
			if (g_NoCrypt)
				n += snprintf(buf + n, sizeof(buf) - n, "client=auto-detect-nocrypt (>= 1.26.0)");
			else
				n += snprintf(buf + n, sizeof(buf) - n, "client=auto-detect (1.25.30 - 5.0.9)");
		} else {
			const ClientVersionInfo *info = Version_Find(g_ClientVersion);
			n += snprintf(buf + n, sizeof(buf) - n, "client=%s", info ? info->name : "unknown");
		}
		if (nocrypt)
			n += snprintf(buf + n, sizeof(buf) - n, " nocrypt");
		if (g_DebugGM)
			n += snprintf(buf + n, sizeof(buf) - n, " gm");
		if (g_DebugTest)
			n += snprintf(buf + n, sizeof(buf) - n, " test");
		if (g_GainMultiplier > 1)
			n += snprintf(buf + n, sizeof(buf) - n, " fast=%dx", g_GainMultiplier);
		if (g_WatchdogEnabled)
			n += snprintf(buf + n, sizeof(buf) - n, " watchdog=%ums", g_WatchdogTimeoutMs);
		if (g_ServerAddr[0] || g_ServerAddr[1] || g_ServerAddr[2] || g_ServerAddr[3])
			n += snprintf(buf + n, sizeof(buf) - n, " addr=%d.%d.%d.%d", g_ServerAddr[0], g_ServerAddr[1], g_ServerAddr[2], g_ServerAddr[3]);
		if (g_Features == FEAT_ALL)
			n += snprintf(buf + n, sizeof(buf) - n, " features=all");
		else if (g_Features == 0)
			n += snprintf(buf + n, sizeof(buf) - n, " features=none");
		else
			n += snprintf(buf + n, sizeof(buf) - n, " features=0x%x", g_Features);
		EventLogger_Log(&g_EventLogger, 0, 0, 0, "", "server", "misc", buf);
	}
}
