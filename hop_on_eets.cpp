// hop_on_eets.cpp - Hop On Eets: competitive solution-race mod for Eets.
//
// Built on eets-mod-framework; manifest sets `sim = 1`. This file is the thin entry layer; the logic
// lives in src/*.h, all #included into this one translation unit (the framework compiles/packs a single .cpp):
//   src/state.h       shared config, globals, types, helpers
//   src/hash.h        FNV-1a state snapshot (determinism / desync)
//   src/determinism.h pin seed/FPS/det-mode, lock game speed, force-start
//   src/net.h         realtime net client (WebSocket/TLS -> relay)
//   src/recorder.h    build-placement capture (shared live to the opponent) + build/sim lifecycle
//   src/ghostview.h   draw the live opponent + their build in-level
//   src/menu.h        custom Eets::UI menu (the live config surface)
//   src/match.h       match lifecycle: round resolution, engine-event handlers, per-frame state machine
//   src/hud.h         all in-game overlay drawing (status, clocks, deaths, ghost, series banner, menu)
//
// Coordinate note: classic Eets puzzle levels are single-screen with an identity GFX view, and the
// framework treats render coords as world coords (spawner places at the cursor) - so world position
// == on-screen position here. Scrolling/zoomed levels would need the GFX view matrix (FUN_0048f2c0).
//
// "tick" tracks the TRUE engine sim-tick (Engine_GetSimTick = Simulator::DeterministicFrameAdvance's
// free-running step counter, minus a per-round baseline taken at sim start); it falls back to counting
// mod Update calls only if the counter address is unavailable. Reading the real counter keeps the
// desync-hash tick aligned with the engine and catches sub-stepping a per-Update ++ would miss.
#ifdef _WIN32
#include <winsock2.h>    // must precede any windows.h in the TU (eetsmod.h pulls windows.h)
#include <ws2tcpip.h>
#endif
#include <random>
#include "eetsmod.h"
#include "eets_ui.h"
#include "src/state.h"
#include "src/hash.h"
#include "src/determinism.h"
#include "src/levels.h"
#include "src/net.h"
#include "src/recorder.h"
#include "src/ghostview.h"
#include "src/menu.h"
#include "src/match.h"      // round resolution + per-frame match state machine (logic)
#include "src/hud.h"        // all in-game overlay drawing

// a stable 128-bit hex id, generated once per install and persisted. This - not the (editable, spoofable)
// display name - is the ranked Elo identity. Mixes random_device with time + a stack address so a weak
// random_device (seen on some mingw builds) still yields a distinct id per install.
static std::string gen_uuid() {
	std::random_device rd;
	uint64_t a = ((uint64_t)rd() << 32) ^ rd(), b = ((uint64_t)rd() << 32) ^ rd();
	a ^= (uint64_t)(Time() * 1e6); b ^= (uint64_t)(uintptr_t)&a;
	char buf[40]; snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
	return std::string(buf);
}

extern "C" void EetsMod_Init() {
	// Almost everything is a fixed competitive constant (see state.h); the shipped .cfg exposes nothing
	// tunable to end users. Only these hidden dev/deployment knobs are still read from config (absent from
	// the .cfg, so not shown - a dev can still set them in the save file).
	auto cfgI = [](const char* k, int d) { return SaveGetInt(MOD, k, ConfigGetInt(MOD, k, d)); };
	auto cfgS = [](const char* k, const char* d) { const char* v = SaveGet(MOD, k, ConfigGet(MOD, k, d)); return std::string(v ? v : d); };
	g_online       = true;                       // always-on multiplayer
	g_playerUuid   = cfgS("player_uuid", "");    // stable Elo identity; generated once per install, then persisted
	if (g_playerUuid.empty()) { g_playerUuid = gen_uuid(); SaveSet(MOD, "player_uuid", g_playerUuid.c_str()); }
	g_pinSeed      = cfgI("pin_seed", 0) != 0;   // solo determinism self-test (Phase C); matches always pin via g_matched
	g_relayUrl     = cfgS("relay_url", "wss://hoe.raccoonlagoon.com");   // direct relay endpoint (ws:// local, wss:// prod)
	const char* savedName = SaveGet(MOD, "player_id", nullptr);   // a custom name set via the F6 menu (persisted)
	g_nameManual   = (savedName && *savedName);
	g_playerId     = g_nameManual ? net_safe_id(savedName) : std::string("p1");   // else "p1" until the profile name is adopted below
	refresh_player_id();   // adopt the vanilla profile name if one is already active at load
	if (g_online && net_connect()) g_netMsg[0] = 0;   // connected: clear the default "offline" (identity shows as "User: <name>")
	Eets::Log("hop_on_eets: ready (tick=%d build=%ds online=%d) - F6 opens the match menu",
	          TICK_RATE, g_buildSeconds, g_online ? 1 : 0);
}

extern "C" void EetsMod_OnMouse(int x, int y, int button, int down) { UI::FeedMouse(x, y, button, down); }

extern "C" void EetsMod_OnText(const char* utf8) {
	if (!utf8) return;
	if (g_nameEntry) {   // online-name entry: any printable non-space ASCII, capped at the name limit
		for (const char* p = utf8; *p; ++p)
			if ((unsigned char)*p > ' ' && (unsigned char)*p < 0x7f && (int)g_nameBuf.size() < MAX_PLAYER_NAME) g_nameBuf.push_back(*p);
		return;
	}
	if (!g_codeEntry) return;
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
	if (g_nameEntry) {
		if (key == EKEY_BACKSPACE) { if (!g_nameBuf.empty()) g_nameBuf.pop_back(); return; }
		if (key == EKEY_RETURN)    { set_player_name(g_nameBuf); g_nameEntry = false; StopTextInput(); return; }
		if (key == EKEY_ESCAPE)    { g_nameEntry = false; StopTextInput(); return; }
	}
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
	net_reconnect_tick();   // recover from a mid-match network drop (relay holds the match ~20s)
	match_update();   // match lifecycle / state machine (src/match.h)
	draw_hud();       // all in-game overlay drawing (src/hud.h)
}
