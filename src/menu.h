// menu.h - the custom in-game menu (Eets::UI). The single config surface: settings edited here
// persist via SaveSet (survive restarts); hop_on_eets.cfg only seeds first-run defaults.
#pragma once
#include "state.h"
#include "net.h"
#include "recorder.h"
#include "determinism.h"
#include "levels.h"

// last path component of a "DATA:Animations/Eets/Eets.anim" style sprite ref (keeps the row short)
static std::string sprite_short() {
	if (g_ghostAnim.empty()) return "marker";
	size_t k = g_ghostAnim.find_last_of("/:");
	return k == std::string::npos ? g_ghostAnim : g_ghostAnim.substr(k + 1);
}

static void draw_menu() {
	UI::Begin(40, 60, 360, "HOP ON EETS");

	char sc[64]; snprintf(sc, sizeof(sc), "Match score:  you %d  -  %d  opp", g_youWins, g_ghostWins);
	UI::Label(sc);
	if (g_roundMsg[0]) UI::Label(g_roundMsg);

	// ---- match settings (toggles | timing sliders) ----
	UI::Section("MATCH");
	UI::BeginColumns(2);
		bool wasMatch = g_matchActive;
		if (UI::Toggle("Match lock", g_matchActive) != wasMatch && g_matchActive && g_phase == SIM) engage_determinism();
		bool sg = g_showGhost; if (UI::Toggle("Show ghost", g_showGhost) != sg) SaveSetInt(MOD, "show_ghost", g_showGhost ? 1 : 0);
		bool ps = g_pinSeed;   if (UI::Toggle("Pin seed", g_pinSeed) != ps) SaveSetInt(MOD, "pin_seed", g_pinSeed ? 1 : 0);
	UI::NextColumn();
		int bsi = (int)(g_buildSecF + 0.5f);
		char bl[32]; snprintf(bl, sizeof(bl), "Build time: %ds", bsi);
		UI::Slider(bl, g_buildSecF, 5.0f, 60.0f);
		if ((int)(g_buildSecF + 0.5f) != bsi) { g_buildSeconds = (int)(g_buildSecF + 0.5f); SaveSetInt(MOD, "build_seconds", g_buildSeconds); }
		int hs = g_hubIntroSkip;
		char hl[40]; snprintf(hl, sizeof(hl), "Skip intro/hub: %d", hs);
		UI::Slider(hl, g_hubIntroSkipF, 0.0f, 5.0f);
		if ((int)(g_hubIntroSkipF + 0.5f) != hs) { g_hubIntroSkip = (int)(g_hubIntroSkipF + 0.5f); SaveSetInt(MOD, "hub_intro_skip", g_hubIntroSkip); g_poolBuilt = false; }
	UI::EndColumns();

	// ---- ghost / replay ----
	UI::Section("GHOST");
	char gl[80]; snprintf(gl, sizeof(gl), "Loaded: %s", g_ghostLabel.c_str()); UI::Label(gl);
	char gs[80]; snprintf(gs, sizeof(gs), "Sprite: %s", sprite_short().c_str()); UI::Label(gs);
	UI::BeginColumns(2);
		if (UI::Button("Cycle sprite")) {        // find the Eets .anim your install uses (then it persists)
			int n = (int)(sizeof(GHOST_ANIM_CANDIDATES) / sizeof(*GHOST_ANIM_CANDIDATES));
			g_ghostAnimIdx = (g_ghostAnimIdx + 1) % n;
			g_ghostAnim = GHOST_ANIM_CANDIDATES[g_ghostAnimIdx];
			SaveSet(MOD, "ghost_anim", g_ghostAnim.c_str());
		}
		if (UI::Button("Race last")) { if (!g_lastGhostPath.empty()) load_ghost(g_lastGhostPath); else Eets::Log("hop_on_eets: no recording yet"); }
	UI::NextColumn();
		if (UI::Button("Clear ghost")) clear_ghost();
		if (UI::Button("New match")) { g_youWins = g_ghostWins = 0; g_roundCounter = 0; g_roundMsg[0] = 0; }
	UI::EndColumns();

	// ---- online ----
	UI::Section("ONLINE");
	bool on = g_online;
	if (UI::Toggle("Online", g_online) != on) { SaveSetInt(MOD, "online", g_online ? 1 : 0); if (g_online) net_action("hello " + g_playerId); else net_close(); }
	char nm[112]; snprintf(nm, sizeof(nm), "Net: %s", g_netMsg); UI::Label(nm);
	char pl[64]; snprintf(pl, sizeof(pl), "Ranked pool: %d (non-tutorial)", pool_size()); UI::Label(pl);

	if (g_matched && g_levelIndex >= 0) {
		char ml[48]; snprintf(ml, sizeof(ml), "Match level: pool #%d", pool_resolve(g_levelIndex)); UI::Label(ml);
		if (UI::Button("Load match level")) { load_match_level(); net_sendline("ready"); }
	}
	if (g_online && !g_matched) {
		UI::BeginColumns(2);
			if (UI::Button("Host code")) net_action("host");
			if (UI::Button("Ranked queue")) net_action("queue");
		UI::NextColumn();
			if (g_codeEntry) {
				char ce[40]; snprintf(ce, sizeof(ce), "Code: %s_", g_codeBuf.c_str()); UI::Label(ce);
				if (UI::Button("Connect")) { g_codeEntry = false; StopTextInput(); if (!g_codeBuf.empty()) net_action("join " + g_codeBuf); }
			} else if (UI::Button("Join code")) { g_codeEntry = true; g_codeBuf.clear(); StartTextInput(); }
		UI::EndColumns();
	}

	UI::Separator();
	UI::Label("F6 closes this menu");
	UI::End();
}
