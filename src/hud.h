// hud.h - all in-game overlay drawing for Hop On Eets: the status line, build/retry + round clocks, deaths,
// online/ghost banners, the persistent series result, the opponent ghost, and the F6 menu. Reads state set by
// match_update (g_buildRemain etc.). Must be #included after ghostview.h and menu.h.
#pragma once
#include "state.h"

// cinematic showdown overlay: letterbox bars wipe in/out, with either a VS face-off (match start) or a
// "ROUND N" card (between rounds). Driven by g_showdownUntil; self-clears when the timer elapses. Drawn
// only in the safe window (in-level, not mid-transition) - see the call site in draw_hud.
static void draw_showdown() {
	if (g_showdownKind == 0) return;
	double now = Time();
	if (now >= g_showdownUntil) { g_showdownKind = 0; return; }
	int sw = ScreenWidth(), sh = ScreenHeight(), cy = sh / 2;
	GFX_ResetViewOffset();                           // draw in screen space (else the in-level camera offset shifts sprites)
	FillRect(0, 0, sw, sh, Color(14, 12, 26, 245));  // dark stage hides the level behind the cinematic
	int barH = (int)(sh * 0.18);                     // letterbox bars pop in/out (no wipe) - present for the whole cinematic
	FillRect(0, 0, sw, barH, Color(0, 0, 0, 255)); FillRect(0, sh - barH, sw, barH, Color(0, 0, 0, 255));
	Color yellow(255, 232, 40, 255), white(255, 255, 255, 255);
	if (g_showdownKind == 1) {                       // match start: two Eets standing at the screen edges
		int th = (int)(sh * 0.55);                   // each Eets ~55% of screen height (fit-to-height = correct at any res)
		int lcx = (int)(sw * 0.17), rcx = (int)(sw * 0.83), eetsY = cy + 24;
		DrawAnimFrozenFit(g_ghostAnim.c_str(), lcx, eetsY, th, white, false);   // left edge, faces center
		DrawAnimFrozenFit(g_ghostAnim.c_str(), rcx, eetsY, th, white, true);    // right edge, mirrored
		DrawTextOutlined(sw / 2 - 26, cy - 24, "VS", FONT_HUGE, yellow);
		int nameY = eetsY - th / 2 - 26;             // just above each Eets' head, centered over it
		char ln[48], rn[48];                         // "Name (ELO)" in ranked, else just the name
		if (g_ranked && g_myElo  > 0) snprintf(ln, sizeof(ln), "%s (%d)", g_playerId.c_str(), g_myElo);  else snprintf(ln, sizeof(ln), "%s", g_playerId.c_str());
		if (g_ranked && g_oppElo > 0) snprintf(rn, sizeof(rn), "%s (%d)", g_oppId.c_str(),    g_oppElo); else snprintf(rn, sizeof(rn), "%s", g_oppId.c_str());
		DrawTextOutlined(lcx - (int)strlen(ln) * 8, nameY, ln, FONT_BIG, Color(150, 220, 255, 255));
		DrawTextOutlined(rcx - (int)strlen(rn) * 8, nameY, rn, FONT_BIG, Color(255, 160, 120, 255));
	} else {                                         // between rounds: a quick ROUND N card
		char rt[32]; snprintf(rt, sizeof(rt), "ROUND %d", g_showdownRound);
		DrawTextOutlined(sw / 2 - (int)strlen(rt) * 12, cy - 24, rt, FONT_HUGE, yellow);
	}
}

// series-end win screen: VICTORY/DEFEAT, the series score, and (ranked) the Elo rating counting from old
// to new with the delta. match_update returns the player to the main menu when the timer elapses.
static void draw_winscreen() {
	int sw = ScreenWidth(), sh = ScreenHeight(), cy = sh / 2;
	GFX_ResetViewOffset();
	FillRect(0, 0, sw, sh, Color(14, 12, 26, 252));   // opaque dark stage
	Color green(120, 255, 120, 255), red(255, 90, 80, 255), white(245, 245, 255, 255), grey(180, 180, 200, 255);
	const char* title = g_seriesWon ? "VICTORY" : "DEFEAT";
	DrawTextOutlined(sw / 2 - (int)strlen(title) * 14, cy - 120, title, FONT_HUGE, g_seriesWon ? green : red);
	char sc[32]; snprintf(sc, sizeof(sc), "%d - %d", g_youWins, g_ghostWins);
	DrawTextOutlined(sw / 2 - (int)strlen(sc) * 8, cy - 56, sc, FONT_BIG, white);
	if (g_eloRanked) {
		double a = (Time() - g_winStart - 0.6) / 1.8; if (a < 0) a = 0; if (a > 1) a = 1;   // 0.6s hold, then ~1.8s count
		double v = g_eloOld + (g_eloNew - g_eloOld) * a;
		int shown = (int)(v + 0.5);
		char el[48]; snprintf(el, sizeof(el), "ELO %d", shown);
		DrawTextOutlined(sw / 2 - (int)strlen(el) * 8, cy + 6, el, FONT_BIG, Color(255, 232, 40, 255));
		int d = g_eloNew - g_eloOld;
		char dl[24]; snprintf(dl, sizeof(dl), "%+d", d);
		DrawTextOutlined(sw / 2 - (int)strlen(dl) * 8, cy + 46, dl, FONT_BIG, d >= 0 ? green : red);
	}
	DrawTextOutlined(sw / 2 - 120, sh - 56, "returning to menu...", FONT_NORMAL, grey);
}

static void draw_hud() {
	if (g_winShow && Time() < g_winUntil) { draw_winscreen(); return; }   // win screen owns the whole frame
	if (in_level()) {
		bool inMatch = (g_matched || g_matchActive);
		if (g_showdownKind != 0 && Time() < g_showdownUntil) { draw_showdown(); return; }   // cinematic owns the screen - no other overlay
		// draw the opponent any time we're in a level (not just our SIM): their live ghost shows whenever
		// THEY are simulating (draw_ghost gates on g_liveValid), and their locked-in build shows during ours.
		if (!g_interRound) { float dt = (float)DeltaTime(); draw_ghost(dt); draw_opp_build(); }

		if (!g_interRound) {   // between rounds the engine's victory transition makes draw calls unsafe - skip the overlay
			char hud[200];
			snprintf(hud, sizeof(hud), "HOP ON EETS %s | %s | t=%.2fs", g_matchActive ? "[MATCH]" : "[practice]", g_status, g_tick / (double)TICK_RATE);
			DrawTextOutlined(10, 30, hud, FONT_NORMAL, Color(255, 232, 40, 255));
			if (inMatch && g_phase == BUILD && g_buildRemain > 0) {
				int bs = (int)g_buildRemain; if (g_buildRemain > (double)bs) bs++;   // whole seconds (ceil)
				char cd[64]; snprintf(cd, sizeof(cd), "%s %ds", g_retryActive ? "RETRY" : "BUILD", bs);
				DrawTextOutlined(10, 52, cd, FONT_BIG, g_buildRemain < 5 ? Color(255, 90, 80, 255) : Color(255, 232, 40, 255));
			}
			if (g_matched && g_deaths > 0) { char dc[48]; snprintf(dc, sizeof(dc), "deaths: %d", g_deaths); DrawTextOutlined(10, 130, dc, FONT_NORMAL, Color(255, 160, 90, 255)); }
			if (g_matched && g_roundCapSeconds > 0) {   // round clock: frozen at full during build, counts down from the first Go
				double left = (g_roundStart > 0) ? (g_roundCapSeconds - (Time() - g_roundStart)) : (double)g_roundCapSeconds;
				if (left < 0) left = 0;
				char rc[40]; snprintf(rc, sizeof(rc), "ROUND %d:%02d", (int)left / 60, (int)left % 60);
				DrawTextOutlined(840, 30, rc, FONT_NORMAL, left < 20 ? Color(255, 90, 80, 255) : Color(150, 220, 255, 255));
			}
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
			if (g_desync || g_noContest) { char db[80]; snprintf(db, sizeof(db), g_noContest ? "NO CONTEST (desync) - not ranked" : "DESYNC @t%ld - result withheld", g_desyncTick); DrawTextOutlined(10, 92, db, FONT_NORMAL, Color(255, 90, 80, 255)); }
		}   // end !g_interRound overlay
	}

	if (g_menuOpen && !g_interRound) draw_menu();   // menu works in-level and in the main menu (not mid-transition)
}
