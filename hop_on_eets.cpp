// hop_on_eets.cpp - Hop On Eets: competitive solution-race mod for Eets.
//
// v0.1 foundation on top of eets-mod-framework (>=0.18.0); manifest sets `sim = 1`.
// See docs/hop-on-eets-spec.md. This file is the thin entry layer; the logic lives in src/*.h,
// all #included into this one translation unit (the framework compiles/packs a single .cpp):
//   src/state.h       shared config, globals, types, helpers
//   src/hash.h        FNV-1a state snapshot (determinism / desync)
//   src/determinism.h pin seed/FPS/det-mode, lock game speed, force-start
//   src/net.h         realtime net client (TCP -> bridge -> WebSocket -> relay)
//   src/recorder.h    replay/ghost/result I/O + build/sim lifecycle
//   src/ghostview.h   draw the opponent (live or recorded) + their build in-level
//   src/menu.h        custom Eets::UI menu (the live config surface)
//   src/match.h       match lifecycle: round resolution, engine-event handlers, per-frame state machine
//   src/hud.h         all in-game overlay drawing (status, clocks, deaths, ghost, series banner, menu)
//
// Coordinate note: classic Eets puzzle levels are single-screen with an identity GFX view, and the
// framework treats render coords as world coords (spawner places at the cursor) - so world position
// == on-screen position here. Scrolling/zoomed levels would need the GFX view matrix (FUN_0048f2c0).
//
// Scaffold limits (also in the spec): "tick" is a sim-frame counter (speed locked) until the true
// engine tick hook (clock subsys DAT_00ee3ca0 / step counter _DAT_00ee3da4); Linux seed/det-mode
// globals and Windows online (winsock) are follow-ups.
#ifdef _WIN32
#include <winsock2.h>    // must precede any windows.h in the TU (eetsmod.h pulls windows.h)
#include <ws2tcpip.h>
#endif
#include "eetsmod.h"
#include "eets_ui.h"
#include "src/state.h"
#include "src/hash.h"
#include "src/determinism.h"
#include "src/levels.h"
#include "src/net.h"
#include "src/recorder.h"
#include "src/resim.h"
#include "src/ghostview.h"
#include "src/menu.h"
#include "src/match.h"      // round resolution + per-frame match state machine (logic)
#include "src/hud.h"        // all in-game overlay drawing

extern "C" void EetsMod_Init() {
	// settings live in the F6 menu and persist via SaveSet; .cfg only seeds first-run defaults
	auto cfgI = [](const char* k, int d) { return SaveGetInt(MOD, k, ConfigGetInt(MOD, k, d)); };
	auto cfgS = [](const char* k, const char* d) { const char* v = SaveGet(MOD, k, ConfigGet(MOD, k, d)); return std::string(v ? v : d); };
	TICK_RATE      = cfgI("tick_rate", 60);
	g_autoRecord   = cfgI("auto_record", 1) != 0;
	g_pinSeed      = cfgI("pin_seed", 0) != 0;
	g_showGhost    = cfgI("show_ghost", 1) != 0;
	HASH_INTERVAL  = cfgI("hash_interval", 30); if (HASH_INTERVAL < 1) HASH_INTERVAL = 1;
	g_buildSeconds = cfgI("build_seconds", 45); g_buildSecF = (float)g_buildSeconds;
	g_retrySeconds = cfgI("retry_seconds", 30); g_retrySecF = (float)g_retrySeconds;
	g_roundCapSeconds = cfgI("round_cap_seconds", 180); g_roundCapF = (float)g_roundCapSeconds;
	g_hubIntroSkip = cfgI("hub_intro_skip", 1); g_hubIntroSkipF = (float)g_hubIntroSkip;
	g_online       = cfgI("online", 0) != 0;
	g_autoLoad     = cfgI("auto_load_level", 1) != 0;
	g_bridgePort   = cfgI("bridge_port", 38600);
	g_ghostFile    = cfgS("ghost_file", "");
	g_ghostAnim    = cfgS("ghost_anim", GHOST_ANIM_CANDIDATES[0]);   // default: animated Eets ghost
	for (int i = 0; i < (int)(sizeof(GHOST_ANIM_CANDIDATES) / sizeof(*GHOST_ANIM_CANDIDATES)); ++i)
		if (g_ghostAnim == GHOST_ANIM_CANDIDATES[i]) g_ghostAnimIdx = i;
	g_bridgeHost   = cfgS("bridge_host", "127.0.0.1");
	g_playerId     = cfgS("player_id", "p1");
	if (!g_ghostFile.empty()) load_ghost(g_ghostFile);
	// re-sim is parameterized by env first (the headless launcher sets these per run), then cfg
	const char* envFile = getenv("HOE_RESIM_FILE"); const char* envLvl = getenv("HOE_RESIM_LEVEL");
	const char* envExit = getenv("HOE_RESIM_EXIT");
	g_resimFile  = (envFile && *envFile) ? std::string(envFile) : cfgS("resim_file", "");
	g_resimLevel = envLvl ? atoi(envLvl) : cfgI("resim_level", -1);
	g_resimExit  = envExit ? atoi(envExit) != 0 : cfgI("resim_exit", 1) != 0;
	if (!g_resimFile.empty() && resim_parse(g_resimFile)) {   // batch verifier: re-sim an input log, write a verdict
		g_resimState = RS_INIT; g_matchActive = true;
		Eets::Log("hop_on_eets: RESIM mode - re-simulating %s (headless verifier, exit_on_done=%d)", g_resimFile.c_str(), g_resimExit ? 1 : 0);
	}
	if (g_online && net_connect()) snprintf(g_netMsg, sizeof(g_netMsg), "connected as %s", g_playerId.c_str());
	Eets::Log("hop_on_eets: ready (tick=%d build=%ds ghost=%s online=%d) - F6 opens the menu (all settings live there)",
	          TICK_RATE, g_buildSeconds, g_haveGhost ? "yes" : "no", g_online ? 1 : 0);
}

extern "C" void EetsMod_OnMouse(int x, int y, int button, int down) { UI::FeedMouse(x, y, button, down); }

extern "C" void EetsMod_OnText(const char* utf8) {
	if (!g_codeEntry || !utf8) return;
	for (const char* p = utf8; *p; ++p) {
		char ch = *p;
		if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
		if (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) && g_codeBuf.size() < 6) g_codeBuf.push_back(ch);
	}
}

extern "C" void EetsMod_Shutdown() { net_close(); }

extern "C" void EetsMod_OnEvent(const char* name, void* a, void* b) { match_on_event(name, a, b); }

extern "C" void EetsMod_OnKey(int key, int mods, int down) {
	if (!down) return;
	if (key == EKEY_F6) { g_menuOpen = !g_menuOpen; return; }
	if (g_codeEntry) {
		if (key == EKEY_BACKSPACE) { if (!g_codeBuf.empty()) g_codeBuf.pop_back(); return; }
		if (key == EKEY_RETURN)    { g_codeEntry = false; StopTextInput(); if (!g_codeBuf.empty()) net_action("join " + g_codeBuf); return; }
		if (key == EKEY_ESCAPE)    { g_codeEntry = false; g_codeBuf.clear(); StopTextInput(); return; }
	}
	bool cs = (mods & EKMOD_CTRL) && (mods & EKMOD_SHIFT);
	if (cs && (key == 'h' || key == 'H')) {
		g_matchActive = !g_matchActive;
		Eets::Log("hop_on_eets: match mode %s", g_matchActive ? "ON" : "OFF");
		if (g_matchActive && g_phase == SIM) engage_determinism();
	} else if (cs && (key == 'r' || key == 'R')) {
		g_youWins = g_ghostWins = 0; g_roundCounter = 0; g_roundMsg[0] = 0; g_seriesOver = false; g_seriesMsg[0] = 0;
		Eets::Log("hop_on_eets: new match");
	}
}

extern "C" void EetsMod_Update() {
	net_poll();
	if (g_resimState != RS_OFF && g_resimState != RS_DONE) resim_tick();   // drive the headless verifier
	match_update();   // match lifecycle / state machine (src/match.h)
	draw_hud();       // all in-game overlay drawing (src/hud.h)
}
