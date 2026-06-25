#pragma once
#ifdef _WIN32
#include <winsock2.h> // must precede windows.h else WS 1.1 clashes
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
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif
using namespace Eets;

static const char *MOD = "hop_on_eets";
static constexpr int TICK_RATE = 60;
static constexpr int HASH_INTERVAL = 30; // desync state-hash cadence (ticks)
static constexpr bool g_autoLoad = true;
static bool g_pinSeed = false;
static uint32_t g_seed = 123456789u;
static bool g_showGhost = true;
static const std::string g_ghostAnim =
    "DATA:Animations/Eets/eets_happy_walk.anim";
static const int GHOST_ALPHA = 120; // live-ghost alpha tint 0..255
static constexpr int MAX_PLAYER_NAME =
    12; // vanilla "ENTER YOUR NAME" field limit

// ---- match / phase ----
enum Phase { IDLE, BUILD, SIM };
static Phase g_phase = IDLE;
static bool g_interRound =
    false; // suppress overlay draws: vanilla victory/screen-transition makes
           // engine draw calls unsafe
static bool g_matchActive = false;
static bool g_menuOpen = false;
static long g_tick = 0;
static long g_engineTickBase =
    -1; // Engine_GetSimTick() at sim start; g_tick = now-this (-1 =
        // unavailable, g_tick++ fallback)
static long g_lastHashBucket =
    -1; // sample on bucket change so a tick jump (engine sub-step) can't skip a
        // sample + misalign the compare
static int g_resets = 0;
static long g_finishTick = -1;
static int g_roundCounter = 0;
static int g_youWins = 0,
           g_ghostWins =
               0; // g_ghostWins = opponent's score ("ghost" = live opponent)
static char g_status[160] = "idle";
static char g_roundMsg[160] = "";

// ---- recorder: player's build placements + determinism samples ----
struct Placement {
  unsigned long long id;
  std::string blueprint;
  float x, y;
  bool removed;
  bool matched = false;
};
struct Sample {
  long tick;
  float x, y;
  uint64_t hash;
};
static std::vector<Placement> g_placements;
static std::vector<Sample> g_samples;
static std::vector<uint64_t> g_prevHashSeq;
static bool g_prevWasResetRerun = false;

// ---- realtime net ----
static bool g_online = false;
static std::string g_relayUrl = "wss://hoe.raccoonlagoon.com";
static std::string g_playerId =
    "p1";                        // display name (editable); not the identity
static std::string g_playerUuid; // stable client-generated rating identity;
                                 // persisted, never changes
static bool g_matched = false, g_ranked = false;
static std::string g_oppId = "?";
static int g_levelIndex =
    -1; // ranked level index from relay (resolved % pool size)
static char g_netMsg[96] = "offline";
static bool g_liveValid = false;
static float g_liveX = 0, g_liveY = 0;
static double g_liveLastTime =
    0.0; // Time() of last opponent pos frame; ghost draws only while fresh
// opponent's live anim state, streamed per pos frame
static char g_liveEmotion = 'h'; // h/a/s = happy/angry/scared
static char g_liveMotion = 'w';  // w/j/f/s = walk/jump/fall/squat
static bool g_liveFlip = false;  // facing (mirrored)
static bool g_codeEntry = false;
static std::string g_codeBuf;
static bool g_nameEntry = false;
static std::string g_nameBuf;
static bool g_confirmForfeit = false;
// reconnect window: mid-match drop holds the relay match briefly so we can
// rejoin
static bool g_oppDropped = false;
static double g_oppDropUntil = 0.0; // when opponent's reconnect window ends
static double g_reconnectUntil =
    0.0; // we dropped: retry until this time, then give up to menu
static double g_lastReconnectTry = 0.0;
static bool g_nameManual =
    false; // custom name overrides profile; persisted as save key player_id
// pre-match / between-round showdown overlay
static int g_showdownKind = 0; // 0=none, 1=match-start, 2=between-rounds
static int g_showdownRound = 0;
static int g_lastRoundWin = 0; // 1=you, -1=opp, 0=none/tie
static int g_pendingShowdown =
    0; // consumed at synced countdown so both clients show together
static double g_showdownUntil = 0.0;
static constexpr double SHOWDOWN_SECS_MATCH = 4.0;
static constexpr double SHOWDOWN_SECS_ROUND = 3.5;
// ---- series-end win screen ----
static bool g_winShow = false;
static double g_winUntil = 0.0;
static double g_winStart = 0.0;
static bool g_seriesWon = false;
static bool g_winForfeit = false;
static bool g_ratingRanked = false;
static int g_ratingOld = 0, g_ratingNew = 0;
static int g_myRating = 0, g_oppRating = 0;
static constexpr double WINSCREEN_SECS = 6.0;
static int g_lastBuildTick = -1;
static int g_lastRoundTick = -1;

// ---- checkpoint state-hash exchange (same-platform desync detection) ----
#ifdef _WIN32
static const char *PLATFORM = "win32";
#else
static const char *PLATFORM = "linux64";
#endif
static std::map<long, uint64_t>
    g_oppHashes; // opponent's checkpoint hashes (same-platform), by tick
static bool g_desync = false;
static long g_desyncTick = -1;
static bool g_desyncSent = false;
static bool g_noContest = false;

static long g_simMaxTicks =
    60 * 120; // DNF watchdog: 120s @60Hz (endless sim -> failed round)
struct GBuild {
  std::string bp;
  float x, y;
};
static std::vector<GBuild> g_oppBuild;
static bool g_oppBuildReady = false;

// ---- forced build timer ----
static int g_buildSeconds = 45;
static int g_deaths = 0; // death resets to build, not a round loss; tiebreaker
static long g_deathTicks =
    0; // sim ticks in died attempts; added to finish_tick (time penalty)
static bool g_deathReset =
    false; // handled next Update, off engine's death call stack
static bool g_selfStop = false; // guard: mod-initiated StopSimulation, so its
                                // level_reset isn't mistaken for a player Stop
static bool g_buttonsHidden = false;
static bool g_seriesOver = false;
static char g_seriesMsg[160] = "";
static bool g_retryActive = false;
static double g_retryStart = 0.0;
static constexpr int g_retrySeconds = 30;
static double g_roundStart = 0.0;
static double g_buildRemain = 0.0;
static int g_roundCapSeconds =
    180; // round wall-clock cap -> DNF; relay re-syncs via countdown
static constexpr int g_hubIntroSkip =
    1; // ranked pool: intro levels skipped per hub
static double g_buildStart = 0.0;
static bool g_forcedStart = false;

static bool in_level() {
  return !World_IsInMainMenu() && !World_IsInLevelEditor();
}
static int placed_count() {
  int n = 0;
  for (auto &p : g_placements)
    if (!p.removed)
      n++;
  return n;
}
// garbage guard: reject NaN/inf + absurd coords (stale/half-built Eets reads
// junk)
static bool valid_pos(float x, float y) {
  return x == x && y == y && x > -50000.f && x < 50000.f && y > -50000.f &&
         y < 50000.f;
}
