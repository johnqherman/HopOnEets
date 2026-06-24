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
