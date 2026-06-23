// menu.h - the custom in-game menu (Eets::UI). The single config surface: settings edited here
// persist via SaveSet (survive restarts); hop_on_eets.cfg only seeds first-run defaults.
#pragma once
#include "state.h"
#include "net.h"
#include "recorder.h"
#include "determinism.h"

static void draw_menu() {
	UI::Begin(40, 70, 300, "HOP ON EETS");
	char sc[64]; snprintf(sc, sizeof(sc), "Match  you %d - %d opp", g_youWins, g_ghostWins);
	UI::Label(sc);
	bool wasMatch = g_matchActive;
	if (UI::Toggle("Match mode (lock sim)", g_matchActive) != wasMatch && g_matchActive && g_phase == SIM) engage_determinism();

	// settings - changes persist via SaveSet (survive restarts); .cfg only seeds first-run defaults
	bool sg = g_showGhost;  if (UI::Toggle("Show ghost", g_showGhost) != sg) SaveSetInt(MOD, "show_ghost", g_showGhost ? 1 : 0);
	bool ps = g_pinSeed;    if (UI::Toggle("Pin RNG seed (Win)", g_pinSeed) != ps) SaveSetInt(MOD, "pin_seed", g_pinSeed ? 1 : 0);
	int bsi = (int)(g_buildSecF + 0.5f);
	char bl[40]; snprintf(bl, sizeof(bl), "Build seconds: %d", bsi); UI::Label(bl);
	UI::Slider("", g_buildSecF, 5.0f, 60.0f);
	if ((int)(g_buildSecF + 0.5f) != bsi) { g_buildSeconds = (int)(g_buildSecF + 0.5f); SaveSetInt(MOD, "build_seconds", g_buildSeconds); }

	// ghost / replay
	char gl[80]; snprintf(gl, sizeof(gl), "Ghost: %s", g_ghostLabel.c_str()); UI::Label(gl);
	char gs[120]; snprintf(gs, sizeof(gs), "Sprite: %s", g_ghostAnim.empty() ? "marker" : g_ghostAnim.c_str()); UI::Label(gs);
	if (UI::Button("Cycle ghost sprite")) {        // find the Eets .anim your install uses (then it persists)
		int n = (int)(sizeof(GHOST_ANIM_CANDIDATES) / sizeof(*GHOST_ANIM_CANDIDATES));
		g_ghostAnimIdx = (g_ghostAnimIdx + 1) % n;
		g_ghostAnim = GHOST_ANIM_CANDIDATES[g_ghostAnimIdx];
		SaveSet(MOD, "ghost_anim", g_ghostAnim.c_str());
	}
	if (UI::Button("Race last recording")) { if (!g_lastGhostPath.empty()) load_ghost(g_lastGhostPath); else Eets::Log("hop_on_eets: no recording yet"); }
	if (UI::Button("Clear ghost")) clear_ghost();
	if (UI::Button("New match (reset score)")) { g_youWins = g_ghostWins = 0; g_roundCounter = 0; g_roundMsg[0] = 0; }

	// online
	bool on = g_online;
	if (UI::Toggle("Online", g_online) != on) { SaveSetInt(MOD, "online", g_online ? 1 : 0); if (g_online) net_action("hello " + g_playerId); else net_close(); }
	char nm[112]; snprintf(nm, sizeof(nm), "Net: %s", g_netMsg); UI::Label(nm);
	if (g_online && !g_matched) {
		if (UI::Button("Host game (get code)")) net_action("host");
		if (g_codeEntry) {
			char ce[64]; snprintf(ce, sizeof(ce), "Code: %s_", g_codeBuf.c_str()); UI::Label(ce);
			if (UI::Button("Connect to code")) { g_codeEntry = false; StopTextInput(); if (!g_codeBuf.empty()) net_action("join " + g_codeBuf); }
		} else if (UI::Button("Join by code")) { g_codeEntry = true; g_codeBuf.clear(); StartTextInput(); }
		if (UI::Button("Ranked queue")) net_action("queue");
	}
	UI::Label("F6 closes this menu");
	UI::End();
}
