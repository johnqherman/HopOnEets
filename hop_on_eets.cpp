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
//
// Coordinate note: classic Eets puzzle levels are single-screen with an identity GFX view, and the
// framework treats render coords as world coords (spawner places at the cursor) - so world position
// == on-screen position here. Scrolling/zoomed levels would need the GFX view matrix (FUN_0048f2c0).
//
// Scaffold limits (also in the spec): "tick" is a sim-frame counter (speed locked) until the true
// engine tick hook (clock subsys DAT_00ee3ca0 / step counter _DAT_00ee3da4); Linux seed/det-mode
// globals and Windows online (winsock) are follow-ups.
#include "eetsmod.h"
#include "eets_ui.h"
#include "src/state.h"
#include "src/hash.h"
#include "src/determinism.h"
#include "src/net.h"
#include "src/recorder.h"
#include "src/ghostview.h"
#include "src/menu.h"

extern "C" void EetsMod_Init() {
	// settings live in the F6 menu and persist via SaveSet; .cfg only seeds first-run defaults
	auto cfgI = [](const char* k, int d) { return SaveGetInt(MOD, k, ConfigGetInt(MOD, k, d)); };
	auto cfgS = [](const char* k, const char* d) { const char* v = SaveGet(MOD, k, ConfigGet(MOD, k, d)); return std::string(v ? v : d); };
	TICK_RATE      = cfgI("tick_rate", 60);
	g_autoRecord   = cfgI("auto_record", 1) != 0;
	g_pinSeed      = cfgI("pin_seed", 0) != 0;
	g_showGhost    = cfgI("show_ghost", 1) != 0;
	HASH_INTERVAL  = cfgI("hash_interval", 30); if (HASH_INTERVAL < 1) HASH_INTERVAL = 1;
	g_buildSeconds = cfgI("build_seconds", 15); g_buildSecF = (float)g_buildSeconds;
	g_online       = cfgI("online", 0) != 0;
	g_bridgePort   = cfgI("bridge_port", 38600);
	g_ghostFile    = cfgS("ghost_file", "");
	g_ghostAnim    = cfgS("ghost_anim", GHOST_ANIM_CANDIDATES[0]);   // default: animated Eets ghost
	for (int i = 0; i < (int)(sizeof(GHOST_ANIM_CANDIDATES) / sizeof(*GHOST_ANIM_CANDIDATES)); ++i)
		if (g_ghostAnim == GHOST_ANIM_CANDIDATES[i]) g_ghostAnimIdx = i;
	g_bridgeHost   = cfgS("bridge_host", "127.0.0.1");
	g_playerId     = cfgS("player_id", "p1");
	if (!g_ghostFile.empty()) load_ghost(g_ghostFile);
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

extern "C" void EetsMod_OnEvent(const char* name, void* a, void* b) {
	if (strcmp(name, "level_load") == 0) {
		begin_build();
	} else if (strcmp(name, "level_reset") == 0) {
		g_resets++; g_phase = BUILD; snprintf(g_status, sizeof(g_status), "build (reset %d)", g_resets);
	} else if (strcmp(name, "object_spawn") == 0) {
		if (g_phase == BUILD) {
			Object* o = (Object*)a; const char* nm = (const char*)b;
			if (o) g_placements.push_back({ (unsigned long long)Object_GetID(o), nm ? nm : Object_GetBlueprintName(o), 0.f, 0.f, false });
		}
	} else if (strcmp(name, "object_killed") == 0) {
		if (g_phase == BUILD && a) {
			unsigned long long id = (unsigned long long)Object_GetID((Object*)a);
			for (auto& p : g_placements) if (p.id == id) p.removed = true;
		}
	} else if (strcmp(name, "level_complete") == 0) {
		if (g_phase == SIM) {
			g_finishTick = g_tick;
			report_determinism();
			write_replay(true); write_ghost(g_replayCounter, true); write_result(true);
			if (g_matched) { char fb[64]; snprintf(fb, sizeof(fb), "finish %ld 1 %d", g_finishTick, placed_count()); net_sendline(fb); }
			g_replayCounter++; g_roundCounter++;
			snprintf(g_status, sizeof(g_status), "complete tick=%ld", g_finishTick);
		}
	}
}

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
		g_youWins = g_ghostWins = 0; g_roundCounter = 0; g_roundMsg[0] = 0;
		Eets::Log("hop_on_eets: new match");
	}
}

extern "C" void EetsMod_Update() {
	net_poll();
	if (in_level()) {
		bool simulating = World_IsSimulating();
		if (g_phase != SIM && simulating) begin_sim(g_resets > 0);
		else if (g_phase == SIM && !simulating && g_finishTick < 0) { g_phase = BUILD; snprintf(g_status, sizeof(g_status), "build"); }

		double remain = 0; bool timed = g_buildSeconds > 0 && (g_matched || g_matchActive);
		if (timed && g_phase == BUILD) { remain = g_buildSeconds - (Time() - g_buildStart); if (remain <= 0 && !g_forcedStart) { g_forcedStart = true; force_start_sim(); } }

		if (g_phase == SIM && simulating && !World_IsPaused()) {
			if (g_matchActive) World_SetGameSpeed(1);
			Object* e = World_GetEets();
			if (e) {                               // skip when Eets isn't live yet (avoids garbage 0,0/junk)
				Vector2 ep = Object_GetPosition(e);
				if (valid_pos(ep.x, ep.y)) {
					if (g_matched) { char pb[64]; snprintf(pb, sizeof(pb), "pos %ld %.1f %.1f", g_tick, ep.x, ep.y); net_sendline(pb); }
					if (g_tick % HASH_INTERVAL == 0) g_samples.push_back({ g_tick, ep.x, ep.y, state_hash() });
				}
			}
			g_tick++;
		}
		if (g_phase == SIM) { float dt = (float)DeltaTime(); draw_ghost(dt); draw_opp_build(); }

		char hud[200];
		snprintf(hud, sizeof(hud), "HOP ON EETS %s | %s | t=%.2fs", g_matchActive ? "[MATCH]" : "[practice]", g_status, g_tick / (double)TICK_RATE);
		DrawTextOutlined(10, 30, hud, FONT_NORMAL, Color(255, 232, 40, 255));
		if (timed && g_phase == BUILD && remain > 0) { char cd[48]; snprintf(cd, sizeof(cd), "BUILD %.1fs", remain); DrawTextOutlined(10, 52, cd, FONT_BIG, remain < 5 ? Color(255, 90, 80, 255) : Color(255, 232, 40, 255)); }
		if (g_matched) { char on[96]; snprintf(on, sizeof(on), "ONLINE vs %s%s", g_oppId.c_str(), g_ranked ? " [RANKED]" : ""); DrawTextOutlined(10, 112, on, FONT_NORMAL, Color(150, 220, 255, 255)); }
		if (g_phase == SIM && g_haveGhost && g_ghostFinish > 0) {
			int bx = 10, by = 52, bw = 280, bh = 14;
			float meFrac = (float)g_tick / (float)g_ghostFinish; if (meFrac > 1.f) meFrac = 1.f;
			FillRect(bx, by, bw, bh, Color(0, 0, 0, 140));
			FillRect(bx, by, (int)(bw * meFrac), bh, Color(80, 220, 90, 255));
			DrawRect(bx, by, bw, bh, Color(0, 0, 0, 255), 2.f);
			bool ahead = g_finishTick < 0 ? (g_tick <= g_ghostFinish) : (g_finishTick < g_ghostFinish);
			char gb[96]; snprintf(gb, sizeof(gb), "vs ghost %.2fs  [%s]", g_ghostFinish / (double)TICK_RATE, ahead ? "AHEAD" : "BEHIND");
			DrawTextOutlined(bx + bw + 10, by - 2, gb, FONT_NORMAL, ahead ? Color(120, 255, 120, 255) : Color(255, 140, 90, 255));
		}
		if (g_roundMsg[0]) DrawTextOutlined(10, 74, g_roundMsg, FONT_NORMAL, Color(255, 255, 255, 255));
	} else if (g_phase != IDLE) {
		g_phase = IDLE; snprintf(g_status, sizeof(g_status), "idle");
	}

	if (g_menuOpen) draw_menu();   // menu works in-level and in the main menu
}
