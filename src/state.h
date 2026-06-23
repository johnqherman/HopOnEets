// state.h - shared config, globals, types, and small helpers for Hop On Eets.
// Single translation unit: every module header is #included once into hop_on_eets.cpp, so these
// file-scope statics have exactly one definition. See hop_on_eets.cpp for the include order.
#pragma once
#include "eetsmod.h"
#include "eets_ui.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif
using namespace Eets;

// ---- config (seeded from hop_on_eets.cfg; edited live in the F6 menu, persisted via SaveSet) ----
static const char* MOD = "hop_on_eets";
static int  TICK_RATE     = 60;
static bool g_autoRecord  = true;
static bool g_pinSeed     = false;
static int  HASH_INTERVAL = 30;
static uint32_t g_seed    = 123456789u;
static std::string g_ghostFile;             // explicit opponent ghost (cfg); empty = practice
static std::string g_ghostAnim;             // optional .anim to draw as the ghost (else a marker)
static bool g_showGhost   = true;

// ---- match / phase ----
enum Phase { IDLE, BUILD, SIM };
static Phase g_phase       = IDLE;
static bool  g_matchActive = false;
static bool  g_menuOpen    = false;
static long  g_tick        = 0;
static int   g_resets      = 0;
static long  g_finishTick  = -1;
static int   g_replayCounter = 0, g_roundCounter = 0;
static int   g_youWins = 0, g_ghostWins = 0;
static char  g_status[160] = "idle";
static char  g_roundMsg[160] = "";
static std::string g_lastGhostPath;          // most recent ghost we wrote (for "race last recording")

// ---- recorder data ----
struct Placement { unsigned long long id; std::string blueprint; float x, y; bool removed; };
struct Sample    { long tick; float x, y; uint64_t hash; };
static std::vector<Placement> g_placements;
static std::vector<Sample>    g_samples;
static std::vector<uint64_t>  g_prevHashSeq;
static bool g_prevWasResetRerun = false;

// ---- loaded ghost ----
static std::vector<Sample> g_ghost;
static long g_ghostFinish    = -1;
static bool g_ghostCompleted = false;
static int  g_ghostItems     = 0;
static bool g_haveGhost      = false;
static std::string g_ghostLabel = "none";

// ---- realtime net ----
static bool g_online = false;
static std::string g_bridgeHost = "127.0.0.1";
static int  g_bridgePort = 38600;
static std::string g_playerId = "p1";
static bool g_matched = false, g_ranked = false;
static std::string g_oppId = "?";
static char g_netMsg[96] = "offline";
static bool  g_liveValid = false; static float g_liveX = 0, g_liveY = 0;
static bool g_codeEntry = false; static std::string g_codeBuf;   // in-menu join-code entry
struct GBuild { std::string bp; float x, y; };                   // opponent's locked-in build item
static std::vector<GBuild> g_oppBuild; static bool g_oppBuildReady = false;

// ---- forced build timer ----
static int    g_buildSeconds = 15; static float g_buildSecF = 15.0f;
static double g_buildStart = 0.0; static bool g_forcedStart = false;

static bool in_level() { return !World_IsInMainMenu() && !World_IsInLevelEditor(); }
static int  placed_count() { int n = 0; for (auto& p : g_placements) if (!p.removed) n++; return n; }
