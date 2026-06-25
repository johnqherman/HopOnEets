// menu.h - the custom in-game menu (Eets::UI). Head-to-head only: no per-run config knobs (build
// time, hub skip, seed pin and online are fixed/automatic) - the menu is just the match score and the
// matchmaking controls. "Match lock" stays as the one solo escape hatch to exercise match rules
// without an opponent. Nothing here persists - there are no tunable settings left to save.
#pragma once
#include "state.h"
#include "net.h"

static void draw_menu() {
	UI::SetClickSound("GUI Click 1");   // stock sfx on every button/toggle press
	UI::SetHoverSound("GUI MouseOver"); // stock sfx when the mouse enters a button/toggle
	UI::Begin(40, 60, 360, "HOP ON EETS");

	// ---- online (always on; matchmaking only). Solo rule-test (Ctrl+Shift+H) and local score reset
	//      (Ctrl+Shift+R) are dev-only keybinds, not menu controls. ----
	UI::Section("ONLINE");
	// editable online name (defaults to the vanilla profile name; type to override, blank reverts to it)
	if (g_nameEntry) {
		char nl[64]; snprintf(nl, sizeof(nl), "User: %s_", g_nameBuf.c_str()); UI::Label(nl);
		if (UI::Button("Set name")) { set_player_name(g_nameBuf); g_nameEntry = false; StopTextInput(); }
	} else {
		char nm[64];
		if (g_myElo > 0) snprintf(nm, sizeof(nm), "User: %s (%d)", g_playerId.c_str(), g_myElo);   // name + ranked ELO inline
		else             snprintf(nm, sizeof(nm), "User: %s", g_playerId.c_str());
		UI::Label(nm);
		if (UI::Button("Edit name")) { g_nameEntry = true; g_nameBuf = g_playerId; StartTextInput(); }
	}
	if (g_netMsg[0]) UI::Label(g_netMsg);   // status only when there's something to say (host code / matched / offline / error)

	if (g_matched) {
		if (!g_confirmForfeit) {
			if (UI::Button("Leave & forfeit")) g_confirmForfeit = true;
		} else {
			UI::Label("Forfeit the match?");
			UI::BeginColumns(2);
				if (UI::Button("Yes, forfeit")) { g_confirmForfeit = false; forfeit_match(); }
			UI::NextColumn();
				if (UI::Button("Cancel")) g_confirmForfeit = false;
			UI::EndColumns();
		}
	} else {
		g_confirmForfeit = false;   // reset the guard when not in a match
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

// ---- input handlers (forwarded from the EetsMod_OnText/OnKey entry points) ----
// text entry for the two in-menu fields: the online name (printable non-space ASCII, capped) and the
// join code (uppercased alphanumerics, 6 chars).
static void mod_on_text(const char* utf8) {
	if (!utf8) return;
	if (g_nameEntry) {
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
// keys: F6 toggles the menu; backspace/enter/esc drive the active text field; CTRL+SHIFT+H/R are dev
// match-mode / new-match shortcuts.
static void mod_on_key(int key, int mods, int down) {
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
