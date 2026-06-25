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
#include <algorithm>
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

// ---- fixed constants + a few hidden dev/deployment knobs (read from config if present, but absent from
//      the shipped hop_on_eets.cfg so end users never see them; nothing is user-tunable in-game) ----
static const char* MOD = "hop_on_eets";
// ---- fixed competitive constants (same for every player; not configurable) ----
static constexpr int TICK_RATE     = 60;     // engine fixed timestep (Hz)
static constexpr int HASH_INTERVAL = 30;     // desync state-hash cadence (ticks)
static constexpr bool g_autoRecord = true;   // always record the run (ranked replay submission needs it)
static constexpr bool g_autoLoad   = true;   // always auto-load the matched level + ready-up
static bool g_pinSeed     = false;           // solo determinism self-test only (cfg pin_seed; matches always pin via g_matched)
static uint32_t g_seed    = 123456789u;
static bool g_showGhost   = true;            // head-to-head: the opponent is always drawn
// the opponent's static-fallback sprite (the live ghost picks a per-emotion/motion .anim at draw time)
static const std::string g_ghostAnim = "DATA:Animations/Eets/eets_happy_walk.anim";
static std::string g_ghostFile;              // dormant solo-replay ghost path (no longer configurable)
static const int GHOST_ALPHA = 120;          // ghost transparency tint (0..255)
// player name cap = the vanilla "ENTER YOUR NAME" field's enforced limit (12 chars)
static constexpr int MAX_PLAYER_NAME = 12;

// ---- match / phase ----
enum Phase { IDLE, BUILD, SIM };
static Phase g_phase       = IDLE;
static bool  g_interRound   = false;   // round just finished, waiting for the next level: suppress overlay draws
                                       // (the vanilla victory/screen-transition makes engine draw calls unsafe)
static bool  g_matchActive = false;
static bool  g_menuOpen    = false;
static long  g_tick        = 0;
static long  g_engineTickBase = -1;    // engine sim-tick counter at sim start; g_tick = Engine_GetSimTick() - this (-1 = counter unavailable -> g_tick++ fallback)
static long  g_lastHashBucket = -1;    // last sampled hash bucket (g_tick / HASH_INTERVAL); sample on bucket change so a tick jump (engine sub-step) can't skip a sample and misalign the determinism compare
static int   g_resets      = 0;
static long  g_finishTick  = -1;
static int   g_replayCounter = 0, g_roundCounter = 0;
static int   g_youWins = 0, g_ghostWins = 0;
static char  g_status[160] = "idle";
static char  g_roundMsg[160] = "";
static std::string g_lastGhostPath;          // most recent ghost we wrote (for "race last recording")

// ---- recorder data ----
struct Placement { unsigned long long id; std::string blueprint; float x, y; bool removed; bool matched = false; };
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
static std::string g_relayUrl = "wss://hoe.raccoonlagoon.com";   // direct relay endpoint (ws:// or wss://); override via cfg relay_url
static std::string g_playerId = "p1";        // display name (editable; profile name by default) - NOT the identity
static std::string g_playerUuid;              // stable client-generated id (the Elo identity; persisted, never changes)
static bool g_matched = false, g_ranked = false;
static std::string g_oppId = "?";
static int g_levelIndex = -1;                // ranked level index from the relay (resolved % pool size)
static char g_netMsg[96] = "offline";
static bool  g_liveValid = false; static float g_liveX = 0, g_liveY = 0;
static double g_liveLastTime = 0.0;   // Time() of the last opponent pos frame; the ghost only draws while fresh (else a round-end leaves it lingering at the last spot)
// opponent's live anim state, streamed with each pos frame, so the ghost mirrors their Eets exactly
static char  g_liveEmotion = 'h';   // h/a/s = happy/angry/scared
static char  g_liveMotion  = 'w';   // w/j/f/s = walk/jump/fall/squat
static bool  g_liveFlip    = false; // facing (mirrored)
static bool g_codeEntry = false; static std::string g_codeBuf;   // in-menu join-code entry
static bool g_nameEntry = false; static std::string g_nameBuf;   // in-menu online-name entry
static bool g_confirmForfeit = false;   // two-step guard on the leave-&-forfeit button
// reconnect window: a mid-match network drop holds the relay match briefly so we can rejoin
static bool   g_oppDropped     = false;   // the opponent dropped; relay is holding the match
static double g_oppDropUntil    = 0.0;    // when the opponent's reconnect window ends (for the banner)
static double g_reconnectUntil  = 0.0;    // we dropped: keep retrying until this time, then give up to the menu
static double g_lastReconnectTry = 0.0;
static bool g_nameManual = false;   // true once the player types a custom name (overrides the vanilla profile name; persisted as save key player_id)
// pre-match / between-round showdown overlay (cinematic beat so round starts feel less abrupt)
static int    g_showdownKind     = 0;   // 0=none, 1=match-start (VS + two Eets), 2=between-rounds (ROUND N)
static int    g_showdownRound    = 0;
static int    g_lastRoundWin     = 0;   // previous round result for the between-round card: 1=you, -1=opponent, 0=none/tie
static int    g_pendingShowdown  = 0;   // set on match/round; consumed at the synced countdown (build start) so both clients show it together
static double g_showdownUntil    = 0.0;
static constexpr double SHOWDOWN_SECS_MATCH = 4.0;   // opening VS cinematic length
static constexpr double SHOWDOWN_SECS_ROUND = 3.5;   // between-rounds cinematic length
// series-end win screen: VICTORY/DEFEAT + animated Elo delta, then auto-return to the main menu
static bool   g_winShow   = false;
static double g_winUntil  = 0.0;
static double g_winStart  = 0.0;
static bool   g_seriesWon = false;
static bool   g_winForfeit = false;   // the series ended by forfeit -> show "by forfeit" instead of the score
static bool   g_eloRanked = false;
static int    g_eloOld    = 0, g_eloNew = 0;
static int    g_myElo     = 0, g_oppElo = 0;   // current ratings sent at ranked match start (for "Name (ELO)" display)
static constexpr double WINSCREEN_SECS = 6.0;
static int    g_lastBuildTick    = -1;  // last whole-second beeped on the build/retry countdown (sub-6s tick sfx)
static int    g_lastRoundTick    = -1;  // last whole-second beeped on the round clock

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
static int    g_buildSeconds = 45;             // default match build time; the relay re-syncs both clients via the countdown signal
static int    g_deaths      = 0;       // Eets deaths this round (match): a death resets to build, not a round loss; tiebreaker
static long   g_deathTicks  = 0;       // sim ticks spent in failed (died) attempts this round - added to finish_tick (time penalty)
static bool   g_deathReset  = false;   // a death is pending a reset-to-build (handled next Update, off the engine's death call stack)
static bool   g_selfStop    = false;   // guard: a mod-initiated StopSimulation is in flight (so its level_reset isn't mistaken for a player Stop)
static bool   g_buttonsHidden = false; // hint/solution/speed buttons currently hidden for a match (so we restore on match end)
static bool   g_seriesOver   = false;  // a series just ended: show the result banner everywhere (not just in-level)
static char   g_seriesMsg[160] = "";   // the persistent series-result text
static bool   g_retryActive = false;   // in a post-death retry build: a local shot clock runs, then auto-Go
static double g_retryStart  = 0.0;     // Time() when the current retry build began
static constexpr int g_retrySeconds = 30;      // retry shot clock seconds (fixed)
static double g_roundStart  = 0.0;     // Time() at round start (countdown); basis for the total round-time cap
static double g_buildRemain = 0.0;     // seconds left on the active build/retry clock (set by match_update, drawn by the HUD)
static int    g_roundCapSeconds = 180;         // total round wall-clock cap -> DNF; relay re-syncs it via the countdown signal
static constexpr int g_hubIntroSkip = 1;   // ranked pool: intro levels skipped per hub (fixed)
static double g_buildStart = 0.0; static bool g_forcedStart = false;

static bool in_level() { return !World_IsInMainMenu() && !World_IsInLevelEditor(); }
static int  placed_count() { int n = 0; for (auto& p : g_placements) if (!p.removed) n++; return n; }
// garbage guard: reject NaN/inf and absurd coords (a stale/half-built Eets reads junk positions)
static bool valid_pos(float x, float y) {
	return x == x && y == y && x > -50000.f && x < 50000.f && y > -50000.f && y < 50000.f;
}
