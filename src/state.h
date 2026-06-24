// state.h - shared config, globals, types, and small helpers for Hop On Eets.
// Single translation unit: every module header is #included once into hop_on_eets.cpp, so these
// file-scope statics have exactly one definition. See hop_on_eets.cpp for the include order.
#pragma once
#ifdef _WIN32
#include <winsock2.h>    // must precede windows.h (which eetsmod.h pulls in) - else winsock 1.1 clashes
#include <ws2tcpip.h>
#endif
#include "eetsmod.h"
#include "eets_ui.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#ifdef _WIN32
#include <windows.h>     // winsock2.h/ws2tcpip.h already included above (must precede windows.h)
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
static std::string g_ghostAnim;             // .anim drawn (animated, semi-transparent) as the ghost
static bool g_showGhost   = true;
// real Eets-character .anim paths (from Data/Animations/Eets/ - split by emotion x motion, no single
// "eets.anim"). Cycle them from the F6 menu; the walk cycle is the default. Last "" = drawn-marker fallback.
static const char* GHOST_ANIM_CANDIDATES[] = {
	"DATA:Animations/Eets/eets_happy_walk.anim",
	"DATA:Animations/Eets/eets_happy_jump.anim",
	"DATA:Animations/Eets/eets_happy_fall.anim",
	"DATA:Animations/Eets/eets_angry_walk.anim",
	"",
};
static int g_ghostAnimIdx = 0;
static const int GHOST_ALPHA = 120;          // ghost transparency tint (0..255)
static bool g_autoLoad = true;               // auto-load the matched level + ready-up on match

// ---- match / phase ----
enum Phase { IDLE, BUILD, SIM };
static Phase g_phase       = IDLE;
static bool  g_interRound   = false;   // round just finished, waiting for the next level: suppress overlay draws
                                       // (the vanilla victory/screen-transition makes engine draw calls unsafe)
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
static int g_levelIndex = -1;                // ranked level index from the relay (resolved % pool size)
static char g_netMsg[96] = "offline";
static bool  g_liveValid = false; static float g_liveX = 0, g_liveY = 0;
// opponent's live anim state, streamed with each pos frame, so the ghost mirrors their Eets exactly
static char  g_liveEmotion = 'h';   // h/a/s = happy/angry/scared
static char  g_liveMotion  = 'w';   // w/j/f/s = walk/jump/fall/squat
static bool  g_liveFlip    = false; // facing (mirrored)
static bool g_codeEntry = false; static std::string g_codeBuf;   // in-menu join-code entry

// ---- checkpoint state-hash exchange (same-platform desync detection; spec Part 10) ----
#ifdef _WIN32
static const char* PLATFORM = "win32";
#else
static const char* PLATFORM = "linux64";
#endif
static std::map<long, uint64_t> g_oppHashes;   // opponent's checkpoint hashes (same-platform), by tick
static bool g_desync     = false;              // this-round same-platform hash mismatch detected
static long g_desyncTick = -1;
static bool g_desyncSent = false;              // reported to the relay once per round
static bool g_noContest  = false;              // relay flagged this match no-contest (score withheld)

// ---- headless re-sim / batch verifier (v0.3; spec Part 6) ----
// In `resim_file` mode the mod loads an input log, drives load -> apply-build -> force-start ->
// read-outcome unattended, and writes a verdict. The authoritative cross-platform result.
enum ResimState { RS_OFF, RS_INIT, RS_LOADING, RS_RUNNING, RS_DONE };
static ResimState g_resimState = RS_OFF;
static std::string g_resimFile;                // input-log JSON to re-simulate ("" = normal interactive mode)
static int  g_resimLevel = -1;                 // level index to load (from cfg or the log)
static long g_resimMaxTicks = 60 * 120;        // DNF guard: 120s at 60Hz
static long g_simMaxTicks   = 60 * 120;        // live-match DNF watchdog: 120s @60Hz (sim that never ends -> failed round)
static bool g_resimClaimDone = false; static long g_resimClaimTick = -1;   // the submitter's finish claim
static bool g_resimExit = true;                // exit the process when the verdict is written (batch mode)
static bool g_resimReproduced = false;         // last verdict: did the re-sim reproduce the claim
struct ResimItem { std::string name; float x, y; };
static std::vector<ResimItem> g_resimBuild;    // build placements parsed from the log
struct GBuild { std::string bp; float x, y; };                   // opponent's locked-in build item
static std::vector<GBuild> g_oppBuild; static bool g_oppBuildReady = false;

// ---- forced build timer ----
static int    g_buildSeconds = 45; static float g_buildSecF = 45.0f;
static int    g_deaths      = 0;       // Eets deaths this round (match): a death resets to build, not a round loss; tiebreaker
static long   g_deathTicks  = 0;       // sim ticks spent in failed (died) attempts this round - added to finish_tick (time penalty)
static bool   g_deathReset  = false;   // a death is pending a reset-to-build (handled next Update, off the engine's death call stack)
static bool   g_selfStop    = false;   // guard: a mod-initiated StopSimulation is in flight (so its level_reset isn't mistaken for a player Stop)
static bool   g_buttonsHidden = false; // hint/solution/speed buttons currently hidden for a match (so we restore on match end)
static bool   g_seriesOver   = false;  // a series just ended: show the result banner everywhere (not just in-level)
static char   g_seriesMsg[160] = "";   // the persistent series-result text
static bool   g_retryActive = false;   // in a post-death retry build: a local shot clock runs, then auto-Go
static double g_retryStart  = 0.0;     // Time() when the current retry build began
static int    g_retrySeconds = 30; static float g_retrySecF = 30.0f;   // retry shot clock (cfg retry_seconds)
static double g_roundStart  = 0.0;     // Time() at round start (countdown); basis for the total round-time cap
static double g_buildRemain = 0.0;     // seconds left on the active build/retry clock (set by match_update, drawn by the HUD)
static int    g_roundCapSeconds = 180; static float g_roundCapF = 180.0f;   // total round wall-clock cap -> DNF (cfg round_cap_seconds)
static int    g_hubIntroSkip = 1;  static float g_hubIntroSkipF = 1.0f;  // intro levels skipped per hub
static double g_buildStart = 0.0; static bool g_forcedStart = false;

static bool in_level() { return !World_IsInMainMenu() && !World_IsInLevelEditor(); }
static int  placed_count() { int n = 0; for (auto& p : g_placements) if (!p.removed) n++; return n; }
// garbage guard: reject NaN/inf and absurd coords (a stale/half-built Eets reads junk positions)
static bool valid_pos(float x, float y) {
	return x == x && y == y && x > -50000.f && x < 50000.f && y > -50000.f && y < 50000.f;
}
