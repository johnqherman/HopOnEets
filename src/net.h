#pragma once
#include <random>
#include "state.h"
#include "levels.h"
#include "ws_client.h"

static void net_sendline(const std::string &s);
[[maybe_unused]] static void note_opp_hash(long tick, uint64_t h,
                                           const char *plat);
[[maybe_unused]] static void note_local_hash(long tick, uint64_t h);

// ---- shared line protocol ----
static void net_handle(const std::string &ln) {
  char a[40] = {0}, b[40] = {0};
  long t;
  float x, y;
  int rk = 0, iv = 0, lv = -1, cap = 0;
  unsigned sd = 0;
  unsigned long long hh = 0;
  char eC = 0, mC = 0;
  int fl = 0;
  float rot = 0.0f;
  char anim[40] = {0};
  int frm = -1;
  int gn = sscanf(ln.c_str(), "g %ld %f %f %c %c %d %f %39s %d", &t, &x, &y, &eC,
                  &mC, &fl, &rot, anim, &frm);
  if (gn >= 3 && strncmp(ln.c_str(), "g ", 2) == 0) {
    if (valid_pos(x, y)) {
      // shift current -> prev, then store the new sample; derive per-tick velocity for extrapolation
      g_livePrevX = g_liveX;
      g_livePrevY = g_liveY;
      g_livePrevTick = g_liveTick;
      g_liveX = x;
      g_liveY = y;
      g_liveTick = t;
      long dtick = g_liveTick - g_livePrevTick;
      if (g_liveValid && dtick > 0 && dtick <= GHOST_MAX_GAP_TICKS) {
        g_liveVX = (g_liveX - g_livePrevX) / (float)dtick;
        g_liveVY = (g_liveY - g_livePrevY) / (float)dtick;
      } else {
        g_liveVX = g_liveVY = 0.0f; // first sample, stale gap, or teleport -> don't fling
      }
      if (!g_liveValid)
        g_ghostRInit = false; // first frame after any reset -> draw snaps instead of sliding across
      g_liveValid = true;
      g_liveLastTime = Time();
      if (gn >= 6) {
        g_liveEmotion = eC;
        g_liveMotion = mC;
        g_liveFlip = fl != 0;
      }
      if (gn >= 7)
        g_liveRot = rot;
      if (gn >= 8) {
        anim[39] = 0;
        strncpy(g_liveAnim, anim, sizeof(g_liveAnim) - 1);
        g_liveAnim[sizeof(g_liveAnim) - 1] = 0;
      }
      g_liveFrame = (gn >= 9) ? frm : -1;
    }
  } else if (sscanf(ln.c_str(), "oh %ld %llx %39s", &t, &hh, a) == 3)
    note_opp_hash(t, (uint64_t)hh, a);
  else if (sscanf(ln.c_str(), "rating %d", &iv) == 1)
    g_myRating = iv;
  else if (strncmp(ln.c_str(), "nocontest", 9) == 0) {
    g_noContest = true;
    long nt = -1;
    char rs[24] = {0};
    sscanf(ln.c_str(), "nocontest %23s %ld", rs, &nt);
    snprintf(g_roundMsg, sizeof(g_roundMsg),
             "NO CONTEST (%s) @t%ld - not ranked", rs[0] ? rs : "desync", nt);
  } else if (sscanf(ln.c_str(), "ob %39s %f %f", a, &x, &y) == 3) {
    if (valid_pos(x, y)) {
      if (g_oppBuildReady) {
        g_oppBuild.clear();
        g_oppBuildReady = false;
      }
      g_oppBuild.push_back({a, x, y});
    }
  } else if (strncmp(ln.c_str(), "obend", 5) == 0)
    g_oppBuildReady = true;
  // match <selfId> <oppId> <ranked> <levelIndex> <seed> <myRating> <oppRating>
  else if (sscanf(ln.c_str(), "match %39s %39s %d %d %u %d %d", a, b, &rk, &lv,
                  &sd, &iv, &g_oppRating) >= 2) {
    g_matched = true;
    g_queueing = false;   // matched -> leave the searching state
    g_hostCode[0] = 0;    // matched -> the host code is spent
    g_ranked = rk != 0;
    if (g_ranked && iv > 0)
      g_myRating = iv;   // ranked match refreshes the ladder rating; casual/private send 0, don't clobber it
    g_oppId = b;
    g_levelIndex = lv;
    if (sd)
      g_seed = sd;
    g_liveValid = false;
    g_oppBuild.clear();
    g_oppBuildReady = false;
    g_noContest = false;
    g_desync = false;
    g_oppHashes.clear();
    g_winForfeit = false;
    g_lastRoundWin = 0;
    g_lastRoundTie = false;
    g_oppDropped = false;
    g_reconnectUntil = 0.0;
    g_seriesOver = false;
    g_seriesMsg[0] = 0;
    snprintf(g_netMsg, sizeof(g_netMsg), "matched vs %s%s", b,
             g_ranked ? " [ranked]" : "");
    g_menuOpen = false;
    g_pendingShowdown = 1;
    g_showdownRound = 1;
    if (g_autoLoad) {
      load_match_level();
      net_sendline("ready");
    }
  } else if (sscanf(ln.c_str(), "round %d %d %u", &iv, &lv, &sd) ==
             3) { // round <n> <levelIndex> <seed>
    g_levelIndex = lv;
    if (sd)
      g_seed = sd;
    g_liveValid = false;
    g_oppBuild.clear();
    g_oppBuildReady = false;
    snprintf(g_netMsg, sizeof(g_netMsg), "round %d - loading level", iv);
    g_pendingShowdown = 2;
    g_showdownRound = iv;
    if (g_autoLoad) {
      load_match_level();
      net_sendline("ready");
    }
  } else if (sscanf(ln.c_str(), "countdown %d %d", &iv, &cap) >=
             1) { // countdown <buildSecs> <roundCap>
    if (iv > 0)
      g_buildSeconds = iv;
    if (cap > 0)
      g_roundCapSeconds = cap;
    g_buildStart = Time();
    g_forcedStart = false;
    g_roundStart =
        0.0; // cap clock idle during build; starts at first Go (begin_sim)
    snprintf(g_netMsg, sizeof(g_netMsg), "build %ds (synced)", g_buildSeconds);
    if (g_pendingShowdown) { // level loaded, draws safe: fire showdown beat
      g_showdownKind = g_pendingShowdown;
      g_pendingShowdown = 0;
      g_showdownUntil = Time() + (g_showdownKind == 1 ? SHOWDOWN_SECS_MATCH
                                                      : SHOWDOWN_SECS_ROUND);
      PlaySound(g_showdownKind == 1 ? "Fanfare" : "Level Complete");
    }
  } else if (sscanf(ln.c_str(), "code %39s", a) == 1) {
    snprintf(g_hostCode, sizeof(g_hostCode), "%s", a);
    snprintf(g_netMsg, sizeof(g_netMsg), "hosting - code %s", a);
  }
  else if (sscanf(ln.c_str(), "joinfail %39s", a) == 1)
    snprintf(g_netMsg, sizeof(g_netMsg), "no game: %s", a);
  // result <winner> <reason> <youWins> <ghostWins>  (relay-authoritative score)
  else if (strncmp(ln.c_str(), "result ", 7) == 0) {
    char w[16] = {0}, r[24] = {0};
    int yw = 0, ow = 0;
    sscanf(ln.c_str() + 7, "%15s %23s %d %d", w, r, &yw, &ow);
    g_youWins = yw;
    g_ghostWins = ow;
    g_lastRoundWin =
        (strcmp(w, "you") == 0) ? 1 : (strcmp(w, "opponent") == 0 ? -1 : 0);
    g_lastRoundTie = (strcmp(w, "tie") == 0); // both DNF -> draw, round replays
    Eets::Log("hop_on_eets: result %s by %s  series %d-%d", w, r, yw, ow);
    snprintf(g_roundMsg, sizeof(g_roundMsg),
             "ONLINE round: %s by %s  series %d-%d", w, r, yw, ow);
    // series <winner> <youWins> <ghostWins> <ranked> <ratingOld> <ratingNew>
    // <forfeit>
  } else if (strncmp(ln.c_str(), "series ", 7) == 0) {
    char w[16] = {0};
    int yw = 0, ow = 0, rk = 0, eo = 0, en = 0, ff = 0;
    sscanf(ln.c_str() + 7, "%15s %d %d %d %d %d %d", w, &yw, &ow, &rk, &eo, &en,
           &ff);
    g_youWins = yw;
    g_ghostWins = ow;
    g_interRound = false;
    g_seriesWon = (strcmp(w, "you") == 0);
    g_winForfeit = ff != 0;
    g_ratingRanked = rk != 0;
    g_ratingOld = eo;
    g_ratingNew = en;
    if (g_ratingRanked && en > 0)
      g_myRating = en;
    g_showdownKind = 0;
    g_pendingShowdown = 0;
    g_winShow = true;
    g_winStart = Time();
    g_winUntil = Time() + WINSCREEN_SECS;
    PlaySound(g_seriesWon ? "Fanfare" : "Error");
    g_seriesOver = false;
    g_seriesMsg[0] = 0;
  } else if (sscanf(ln.c_str(), "oppdrop %d", &iv) ==
             1) { // relay holds match for reconnect window of iv secs
    g_oppDropped = true;
    g_oppDropUntil = Time() + iv;
    g_liveValid = false;
    snprintf(g_netMsg, sizeof(g_netMsg), "opponent dropped - waiting %ds", iv);
  } else if (strncmp(ln.c_str(), "opprejoin", 9) == 0) {
    g_oppDropped = false;
    snprintf(g_netMsg, sizeof(g_netMsg), "opponent reconnected");
  } else if (sscanf(ln.c_str(), "rejoin %39s %d %d %d", a, &rk, &iv, &lv) >=
             1) { // rejoin <oppId> <ranked> <youWins> <ghostWins>
    g_matched = true;
    g_ranked = rk != 0;
    g_oppId = a;
    g_youWins = iv;
    g_ghostWins = lv;
    g_reconnectUntil = 0.0;
    g_winShow = false;
    g_oppDropped = false;
    g_liveValid = false;
    snprintf(g_netMsg, sizeof(g_netMsg), "reconnected vs %s", a);
  } else if (strncmp(ln.c_str(), "oppleft", 7) == 0) {
    g_matched = false;
    g_liveValid = false;
    g_interRound = false;
    g_oppDropped = false;
    snprintf(g_netMsg, sizeof(g_netMsg), "opponent left");
  }
}

// ---- transport: one line = one WS text frame (no newline) ----
static void net_sendline(const std::string &s) { wsc_send_text(s); }
static void net_close() {
  wsc_close();
  g_matched = false;
  g_liveValid = false;
}
static bool net_up() { return wsc_up(); }
static void net_poll() { wsc_poll(net_handle); }
static bool net_connect() {
  if (!wsc_connect(g_relayUrl))
    return false;
  wsc_send_text("hello " + g_playerUuid + " " + g_playerId);
  return true;
}

// wire protocol is whitespace-tokenized (sscanf %Ns), so identity must be a
// single token
static std::string net_safe_id(const std::string &s) {
  std::string out;
  bool gap = false;
  for (char c : s) {
    if ((unsigned char)c <= ' ') {
      gap = !out.empty();
      continue;
    } // whitespace/control -> gap
    if ((unsigned char)c >= 0x7f)
      continue; // drop non-ASCII
    if (gap) {
      out += '_';
      gap = false;
    }
    out += c;
    if ((int)out.size() >= MAX_PLAYER_NAME)
      break;
  }
  return out;
}
static void refresh_player_id() {
  if (g_nameManual)
    return;
  std::string nm = net_safe_id(Profile_GetName());
  if (nm.empty() || nm == g_playerId)
    return;
  g_playerId = nm;
  if (net_up())
    net_sendline("hello " + g_playerUuid + " " + g_playerId);
}

// F6 name override; empty -> revert to profile; persisted under save key
// player_id
static void set_player_name(const std::string &raw) {
  std::string nm = net_safe_id(raw);
  if (nm.empty()) {
    g_nameManual = false;
    SaveSet(MOD, "player_id", "");
    refresh_player_id();
    return;
  }
  g_nameManual = true;
  g_playerId = nm;
  SaveSet(MOD, "player_id", nm.c_str());
  if (net_up())
    net_sendline("hello " + g_playerUuid + " " + g_playerId);
}

// leave the match: the relay resolves it immediately (opponent wins) and sends us series_over with the
// real new rating, which drives the DEFEAT screen. no client-side prediction, no reconnect hold.
static void forfeit_match() {
  g_reconnectUntil = 0.0;
  g_confirmForfeit = false;
  net_sendline("forfeit");
  snprintf(g_netMsg, sizeof(g_netMsg), "forfeiting...");
}

// mid-match drop recovery: retry reconnect; relay holds match ~20s then
// `rejoin` reattaches; window passes with no rejoin -> give up to main menu
static void net_reconnect_tick() {
  if (!g_matched || g_winShow)
    return;
  if (net_up() && g_reconnectUntil == 0.0)
    return; // normal connected state
  double now = Time();
  if (g_reconnectUntil == 0.0)
    g_reconnectUntil = now + 25.0; // give-up window: relay's 20s + margin
  if (now > g_reconnectUntil) {
    g_matched = false;
    g_reconnectUntil = 0.0;
    g_oppDropped = false;
    g_interRound = false;
    g_phase = IDLE;
    g_menuOpen = false;
    snprintf(g_netMsg, sizeof(g_netMsg), "disconnected");
    World_StartMainMenu();
    return;
  }
  if (!net_up() && now - g_lastReconnectTry >= 1.0) {
    g_lastReconnectTry = now;
    snprintf(g_netMsg, sizeof(g_netMsg), "reconnecting...");
    net_connect();
  }
}

static void net_action(const std::string &cmd) {
  refresh_player_id(); // re-hello if profile name changed
  if (!net_up()) {
    if (!net_connect()) {
      snprintf(g_netMsg, sizeof(g_netMsg), "can't reach server (%s)",
               g_relayUrl.c_str());
      return;
    }
  }
  net_sendline(cmd);
}

// keep us in the server's matchmaking queue while searching: re-send "queue" periodically. queue membership
// is per-connection and server-side, so a relay restart / proxy drop / brief blip silently evicts a waiting
// player (their client still shows "searching"). re-asserting heals it within the interval; the relay keeps
// our place if we're already queued (no starvation). also reconnect if the socket dropped while waiting.
static void net_queue_tick() {
  if (!g_queueing || g_matched)
    return;
  double now = Time();
  if (!net_up()) {
    if (now - g_lastReconnectTry >= 1.0) {
      g_lastReconnectTry = now;
      net_connect(); // re-hellos; the re-assert below re-enters the queue once up
    }
    return;
  }
  if (now - g_lastQueueAssert >= 15.0) {
    g_lastQueueAssert = now;
    net_sendline(g_queueRanked ? "queue ranked" : "queue casual");
  }
}

// ---- checkpoint state-hash exchange (same-platform desync detection) ----
// cross-platform hashes legitimately differ (FP physics) so are never compared
// across platforms; same-platform mismatch = real desync -> flag, withhold
// ranked result, report to relay, save diag
static void write_desync_diag(long tick, uint64_t local, uint64_t opp) {
  char path[128];
  snprintf(path, sizeof(path), "Log/hop_on_eets_desync_%03d.json",
           g_roundCounter);
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  fprintf(f,
          "{\n  \"desync_version\": 1,\n  \"round\": %d,\n  \"tick\": %ld,\n  "
          "\"platform\": \"%s\",\n"
          "  \"seed\": %u,\n  \"level_index\": %d,\n  \"local_hash\": "
          "\"%016llx\",\n  \"opp_hash\": \"%016llx\"\n}\n",
          g_roundCounter, tick, PLATFORM, g_seed, g_levelIndex,
          (unsigned long long)local, (unsigned long long)opp);
  fclose(f);
  Eets::Log("hop_on_eets: DESYNC @t%ld local=%016llx opp=%016llx -> %s", tick,
            (unsigned long long)local, (unsigned long long)opp, path);
}
static void flag_desync(long tick, uint64_t local, uint64_t opp) {
  if (g_desync)
    return; // first divergence per round is enough
  g_desync = true;
  g_desyncTick = tick;
  write_desync_diag(tick, local, opp);
  if (!g_desyncSent) {
    g_desyncSent = true;
    char b[48];
    snprintf(b, sizeof(b), "desync %ld", tick);
    net_sendline(b);
  }
  snprintf(g_netMsg, sizeof(g_netMsg), "DESYNC @t%ld - result withheld", tick);
}
[[maybe_unused]] static void note_local_hash(long tick, uint64_t h) {
  if (g_noContest || g_desync)
    return;
  auto it = g_oppHashes.find(tick);
  if (it != g_oppHashes.end() && it->second != h)
    flag_desync(tick, h, it->second);
}
// only net_handle (POSIX) calls this; unused in Win stub build
[[maybe_unused]] static void note_opp_hash(long tick, uint64_t h,
                                           const char *plat) {
  if (g_noContest || g_desync)
    return;
  if (!plat || strcmp(plat, PLATFORM) != 0)
    return; // cross-platform divergence expected, not desync
  g_oppHashes[tick] = h;
  for (auto &s : g_samples)
    if (s.tick == tick) {
      if (s.hash != h)
        flag_desync(tick, s.hash, h);
      break;
    }
}

// stable 128-bit hex id (not the spoofable display name) is the ranked rating
// identity; mixes time + stack address so a weak random_device (some mingw
// builds) still yields a distinct id per install
static std::string gen_uuid() {
  std::random_device rd;
  uint64_t a = ((uint64_t)rd() << 32) ^ rd(), b = ((uint64_t)rd() << 32) ^ rd();
  a ^= (uint64_t)(Time() * 1e6);
  b ^= (uint64_t)(uintptr_t)&a;
  char buf[40];
  snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)a,
           (unsigned long long)b);
  return std::string(buf);
}

// mod bootstrap (from EetsMod_Init): read hidden dev knobs, establish the rating
// identity, adopt profile name, connect to relay
static void mod_init() {
  auto cfgI = [](const char *k, int d) {
    return SaveGetInt(MOD, k, ConfigGetInt(MOD, k, d));
  };
  auto cfgS = [](const char *k, const char *d) {
    const char *v = SaveGet(MOD, k, ConfigGet(MOD, k, d));
    return std::string(v ? v : d);
  };
  g_online = true;
  g_playerUuid = cfgS("player_uuid", "");
  if (g_playerUuid.empty()) {
    g_playerUuid = gen_uuid();
    SaveSet(MOD, "player_uuid", g_playerUuid.c_str());
  }
  g_pinSeed = cfgI("pin_seed", 0) != 0;
  g_relayUrl = cfgS("relay_url", "wss://hoe.raccoonlagoon.com");
  const char *savedName = SaveGet(MOD, "player_id", nullptr);
  g_nameManual = (savedName && *savedName);
  g_playerId = g_nameManual ? net_safe_id(savedName) : std::string("p1");
  refresh_player_id();
  if (g_online && net_connect())
    g_netMsg[0] = 0; // connected: clear default "offline"
  Eets::Log("hop_on_eets: ready (tick=%d build=%ds online=%d) - F6 opens the "
            "match menu",
            TICK_RATE, g_buildSeconds, g_online ? 1 : 0);
}
