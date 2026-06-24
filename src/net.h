// net.h - realtime net client. mod <-> localhost TCP <-> bridge <-> WebSocket <-> relay.
// POSIX sockets (Linux) / winsock (Windows, links ws2_32); zero other deps. Two ways in:
// host/join by code, or ranked matchmaking. See docs/net-protocol.md.
#pragma once
#include "state.h"
#include "levels.h"     // load_match_level on auto-load
#include "ws_client.h"  // direct WebSocket (ws/wss) transport to the relay

// defined further down (platform socket layer + desync), but used by net_handle just below:
static void net_sendline(const std::string& s);
[[maybe_unused]] static void note_opp_hash(long tick, uint64_t h, const char* plat);
[[maybe_unused]] static void note_local_hash(long tick, uint64_t h);

// ---- shared line protocol (platform-independent) ----
static void net_handle(const std::string& ln) {
	char a[40] = { 0 }, b[40] = { 0 }; long t; float x, y; int rk = 0, iv = 0, lv = -1, cap = 0; unsigned sd = 0; unsigned long long hh = 0;
	char eC = 0, mC = 0; int fl = 0;
	int gn = sscanf(ln.c_str(), "g %ld %f %f %c %c %d", &t, &x, &y, &eC, &mC, &fl);
	if (gn >= 3 && strncmp(ln.c_str(), "g ", 2) == 0) { (void)t; if (valid_pos(x, y)) { g_liveX = x; g_liveY = y; g_liveValid = true; if (gn >= 6) { g_liveEmotion = eC; g_liveMotion = mC; g_liveFlip = fl != 0; } } }
	else if (sscanf(ln.c_str(), "oh %ld %llx %39s", &t, &hh, a) == 3) note_opp_hash(t, (uint64_t)hh, a);
	else if (sscanf(ln.c_str(), "elo %d", &iv) == 1) g_myElo = iv;   // current ranked rating (idle, for the F6 menu)
	else if (strncmp(ln.c_str(), "nocontest", 9) == 0) {
		g_noContest = true; long nt = -1; char rs[24] = { 0 }; sscanf(ln.c_str(), "nocontest %23s %ld", rs, &nt);
		snprintf(g_roundMsg, sizeof(g_roundMsg), "NO CONTEST (%s) @t%ld - not ranked", rs[0] ? rs : "desync", nt);
	}
	else if (strncmp(ln.c_str(), "auth ", 5) == 0) {   // authoritative (re-sim) result; overrides the provisional
		char k[16] = { 0 }, w[40] = { 0 }, r[24] = { 0 }; sscanf(ln.c_str() + 5, "%15s %39s %23s", k, w, r);
		const char* outcome = (strcmp(k, "no_contest") == 0) ? "NO CONTEST" : (g_playerId == w ? "WIN" : "LOSS");
		snprintf(g_roundMsg, sizeof(g_roundMsg), "OFFICIAL %s: %s (%s)", k, outcome, r);
	}
	else if (sscanf(ln.c_str(), "ob %39s %f %f", a, &x, &y) == 3) { if (valid_pos(x, y)) { if (g_oppBuildReady) { g_oppBuild.clear(); g_oppBuildReady = false; } g_oppBuild.push_back({ a, x, y }); } }
	else if (strncmp(ln.c_str(), "obend", 5) == 0) g_oppBuildReady = true;
	else if (sscanf(ln.c_str(), "match %39s %39s %d %d %u %d %d", a, b, &rk, &lv, &sd, &g_myElo, &g_oppElo) >= 2) {
		g_matched = true; g_ranked = rk != 0; g_oppId = b; g_levelIndex = lv; if (sd) g_seed = sd; g_liveValid = false; g_oppBuild.clear(); g_oppBuildReady = false;
		g_noContest = false; g_desync = false; g_oppHashes.clear();   // fresh match: clear desync state
		g_seriesOver = false; g_seriesMsg[0] = 0;   // clear the previous series banner
		snprintf(g_netMsg, sizeof(g_netMsg), "matched vs %s%s", b, g_ranked ? " [ranked]" : "");
		g_menuOpen = false;   // a match started: close the F6 menu
		g_pendingShowdown = 1; g_showdownRound = 1;   // match-start VS showdown, shown at the synced countdown
		if (g_autoLoad) { load_match_level(); net_sendline("ready"); }   // both load the same level, then ready up
	} else if (sscanf(ln.c_str(), "round %d %d %u", &iv, &lv, &sd) == 3) {
		// next Bo3 round: a FRESH level (each round is a different level). Load it, then ready up so the relay
		// fires the synced countdown once both clients have re-loaded.
		g_levelIndex = lv; if (sd) g_seed = sd; g_liveValid = false; g_oppBuild.clear(); g_oppBuildReady = false;
		snprintf(g_netMsg, sizeof(g_netMsg), "round %d - loading level", iv);
		g_pendingShowdown = 2; g_showdownRound = iv;   // lighter between-rounds beat (ROUND N), shown at the countdown
		if (g_autoLoad) { load_match_level(); net_sendline("ready"); }
	} else if (sscanf(ln.c_str(), "countdown %d %d", &iv, &cap) >= 1) {
		if (iv > 0) g_buildSeconds = iv;      // synced build phase: align both clients to the relay signal
		if (cap > 0) g_roundCapSeconds = cap;   // relay-authoritative round cap (synced clock)
		g_buildStart = Time(); g_forcedStart = false;
		g_roundStart = 0.0;                   // round/cap clock does NOT run during build - it starts at the first Go (begin_sim)
		snprintf(g_netMsg, sizeof(g_netMsg), "build %ds (synced)", g_buildSeconds);
		if (g_pendingShowdown) {              // build is starting (level loaded, draws safe): fire the synced showdown beat
			g_showdownKind = g_pendingShowdown; g_pendingShowdown = 0;
			g_showdownUntil = Time() + (g_showdownKind == 1 ? SHOWDOWN_SECS_MATCH : SHOWDOWN_SECS_ROUND);
			PlaySound(g_showdownKind == 1 ? "Fanfare" : "Level Complete");
		}
	} else if (sscanf(ln.c_str(), "code %39s", a) == 1) snprintf(g_netMsg, sizeof(g_netMsg), "hosting - code %s", a);
	else if (sscanf(ln.c_str(), "joinfail %39s", a) == 1) snprintf(g_netMsg, sizeof(g_netMsg), "no game for code %s", a);
	else if (strncmp(ln.c_str(), "result ", 7) == 0) {
		char w[16] = { 0 }, r[24] = { 0 }; int yw = 0, ow = 0;
		sscanf(ln.c_str() + 7, "%15s %23s %d %d", w, r, &yw, &ow);
		g_youWins = yw; g_ghostWins = ow;       // relay is authoritative for the online series score
		Eets::Log("hop_on_eets: result %s by %s  series %d-%d", w, r, yw, ow);
		snprintf(g_roundMsg, sizeof(g_roundMsg), "ONLINE round: %s by %s  series %d-%d", w, r, yw, ow);
	} else if (strncmp(ln.c_str(), "series ", 7) == 0) {
		char w[16] = { 0 }; int yw = 0, ow = 0, rk = 0, eo = 0, en = 0;
		sscanf(ln.c_str() + 7, "%15s %d %d %d %d %d", w, &yw, &ow, &rk, &eo, &en);
		g_youWins = yw; g_ghostWins = ow;
		g_interRound = false;
		g_seriesWon = (strcmp(w, "you") == 0);
		g_eloRanked = rk != 0; g_eloOld = eo; g_eloNew = en;
		if (g_eloRanked && en > 0) g_myElo = en;   // update the idle rating shown in the menu
		g_showdownKind = 0; g_pendingShowdown = 0;   // cancel any pending/playing cinematic
		g_winShow = true; g_winStart = Time(); g_winUntil = Time() + WINSCREEN_SECS;   // win screen, then auto-return to menu
		PlaySound(g_seriesWon ? "Fanfare" : "Error");
		g_seriesOver = false; g_seriesMsg[0] = 0;   // replaced by the win screen
	} else if (strncmp(ln.c_str(), "oppleft", 7) == 0) { g_matched = false; g_liveValid = false; g_interRound = false; snprintf(g_netMsg, sizeof(g_netMsg), "opponent left"); }
}

// ---- transport: a direct WebSocket to the relay (ws:// or wss://); framing/TLS live in ws_client.h.
// Each line is one WS text frame (no newline). net_handle is the per-frame callback. ----
static void net_sendline(const std::string& s) { wsc_send_text(s); }
static void net_close() { wsc_close(); g_matched = false; g_liveValid = false; }
static bool net_up() { return wsc_up(); }
static void net_poll() { wsc_poll(net_handle); }
static bool net_connect() {
	if (!wsc_connect(g_relayUrl)) return false;
	wsc_send_text("hello " + g_playerUuid + " " + g_playerId);
	return true;
}

// adopt the player's active vanilla Eets profile name as the online identity (falls back to the
// configured id when no profile is selected yet - e.g. at startup before the player picks one). Re-sends
// hello when already connected so the relay learns the real name before a match is made. (Win: profile
// read is a no-op for now, so the configured id stands.)
// the wire protocol is whitespace-tokenized (sscanf %Ns on both ends), so the identity must be a single
// safe token: collapse whitespace runs to '_', keep printable ASCII, cap to MAX_PLAYER_NAME (the vanilla
// name field's own limit, well within the %39s buffers).
static std::string net_safe_id(const std::string& s) {
	std::string out; bool gap = false;
	for (char c : s) {
		if ((unsigned char)c <= ' ') { gap = !out.empty(); continue; }   // whitespace/control -> a single gap
		if ((unsigned char)c >= 0x7f) continue;                          // drop non-ASCII
		if (gap) { out += '_'; gap = false; }
		out += c;
		if ((int)out.size() >= MAX_PLAYER_NAME) break;
	}
	return out;
}
static void refresh_player_id() {
	if (g_nameManual) return;   // a custom name was set in the F6 menu - don't override it with the profile
	std::string nm = net_safe_id(Profile_GetName());
	if (nm.empty() || nm == g_playerId) return;
	g_playerId = nm;
	if (net_up()) net_sendline("hello " + g_playerUuid + " " + g_playerId);
}

// set the online name from the F6 menu. Empty input clears the override -> revert to the vanilla profile
// name. A custom name is sanitized, persisted (save key player_id), and re-hello'd if already connected.
static void set_player_name(const std::string& raw) {
	std::string nm = net_safe_id(raw);
	if (nm.empty()) { g_nameManual = false; SaveSet(MOD, "player_id", ""); refresh_player_id(); return; }
	g_nameManual = true; g_playerId = nm; SaveSet(MOD, "player_id", nm.c_str());
	if (net_up()) net_sendline("hello " + g_playerUuid + " " + g_playerId);
}

// leave the current match: dropping the connection is a forfeit (the relay awards the remaining player the
// series on disconnect), then reconnect as a fresh idle client so the player can queue again.
static void forfeit_match() {
	net_close();                         // disconnect = loss; relay sends the opponent series_over
	g_interRound = false; g_roundMsg[0] = 0; g_seriesOver = false; g_seriesMsg[0] = 0;
	g_showdownKind = 0; g_pendingShowdown = 0; g_levelIndex = -1;   // clear match-local overlay state
	g_phase = IDLE; g_menuOpen = false;
	World_StartMainMenu();               // leave the level back to the main menu
	refresh_player_id();                 // re-resolve name in case the profile changed
	if (net_connect()) snprintf(g_netMsg, sizeof(g_netMsg), "left the match");
	else snprintf(g_netMsg, sizeof(g_netMsg), "left - server offline");
}

// connect on demand, then send a command line to the bridge
static void net_action(const std::string& cmd) {
	refresh_player_id();   // use the freshest profile name for host/queue/join (and re-hello if it changed)
	if (!net_up()) { if (!net_connect()) { snprintf(g_netMsg, sizeof(g_netMsg), "can't reach server (%s)", g_relayUrl.c_str()); return; } }
	net_sendline(cmd);
}

// ---- checkpoint state-hash exchange (spec Part 10) -----------------------------------------
// Same-platform divergence at the same tick = a real desync (cheat / nondeterminism); cross-platform
// hashes legitimately differ (FP physics) so we never compare across platforms. On a same-platform
// mismatch: flag it, withhold the ranked result, report to the relay (-> no-contest), save a diag.
static void write_desync_diag(long tick, uint64_t local, uint64_t opp) {
	char path[128]; snprintf(path, sizeof(path), "Log/hop_on_eets_desync_%03d.json", g_roundCounter);
	FILE* f = fopen(path, "w"); if (!f) return;
	fprintf(f, "{\n  \"desync_version\": 1,\n  \"round\": %d,\n  \"tick\": %ld,\n  \"platform\": \"%s\",\n"
	           "  \"seed\": %u,\n  \"level_index\": %d,\n  \"local_hash\": \"%016llx\",\n  \"opp_hash\": \"%016llx\"\n}\n",
	        g_roundCounter, tick, PLATFORM, g_seed, g_levelIndex,
	        (unsigned long long)local, (unsigned long long)opp);
	fclose(f);
	Eets::Log("hop_on_eets: DESYNC @t%ld local=%016llx opp=%016llx -> %s", tick,
	          (unsigned long long)local, (unsigned long long)opp, path);
}
static void flag_desync(long tick, uint64_t local, uint64_t opp) {
	if (g_desync) return;                       // first divergence per round is enough
	g_desync = true; g_desyncTick = tick;
	write_desync_diag(tick, local, opp);
	if (!g_desyncSent) { g_desyncSent = true; char b[48]; snprintf(b, sizeof(b), "desync %ld", tick); net_sendline(b); }
	snprintf(g_netMsg, sizeof(g_netMsg), "DESYNC @t%ld - result withheld", tick);
}
// our checkpoint at `tick` just computed: compare if the opponent's already arrived
[[maybe_unused]] static void note_local_hash(long tick, uint64_t h) {
	if (g_noContest || g_desync) return;
	auto it = g_oppHashes.find(tick);
	if (it != g_oppHashes.end() && it->second != h) flag_desync(tick, h, it->second);
}
// opponent's checkpoint arrived: stash it (same-platform only) and compare to ours if we have it
// (only net_handle calls this, which is the POSIX build; unused in the Windows stub build)
[[maybe_unused]] static void note_opp_hash(long tick, uint64_t h, const char* plat) {
	if (g_noContest || g_desync) return;
	if (!plat || strcmp(plat, PLATFORM) != 0) return;   // cross-platform divergence is expected, not a desync
	g_oppHashes[tick] = h;
	for (auto& s : g_samples) if (s.tick == tick) { if (s.hash != h) flag_desync(tick, s.hash, h); break; }
}
