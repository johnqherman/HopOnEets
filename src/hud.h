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
	double dur = (g_showdownKind == 1 ? SHOWDOWN_SECS_MATCH : SHOWDOWN_SECS_ROUND);
	double t = 1.0 - (g_showdownUntil - now) / dur; if (t < 0) t = 0; if (t > 1) t = 1;
	int sw = ScreenWidth(), sh = ScreenHeight(), cy = sh / 2;
	double f = (t < 0.15) ? (t / 0.15) : (t > 0.85 ? (1.0 - t) / 0.15 : 1.0);   // bars wipe in, hold, wipe out
	int barH = (int)(sh * 0.16 * f);
	if (barH > 0) { FillRect(0, 0, sw, barH, Color(0, 0, 0, 255)); FillRect(0, sh - barH, sw, barH, Color(0, 0, 0, 255)); }
	Color yellow(255, 232, 40, 255), white(255, 255, 255, 255);
	if (g_showdownKind == 1) {                       // match start: two big Eets standing at the screen edges
		const float S = 5.5f; const int sz = (int)(64 * S), half = sz / 2, margin = 10;   // dt=0 -> frozen pose (standing, not walking)
		if (!g_ghostAnim.empty()) {
			DrawAnim(g_ghostAnim.c_str(), margin,           cy - half, 0.0f, 0.0f, white, false, S);   // left edge, faces center
			DrawAnim(g_ghostAnim.c_str(), sw - sz - margin, cy - half, 0.0f, 0.0f, white, true,  S);    // right edge, mirrored to face center
		}
		DrawTextOutlined(sw / 2 - 18, cy - 18, "VS", FONT_BIG, yellow);
		DrawTextOutlined(margin,           cy - half - 28, g_playerId.c_str(), FONT_NORMAL, Color(150, 220, 255, 255));
		DrawTextOutlined(sw - sz - margin, cy - half - 28, g_oppId.c_str(),    FONT_NORMAL, Color(255, 160, 120, 255));
	} else {                                         // between rounds: a quick ROUND N card
		char rt[32]; snprintf(rt, sizeof(rt), "ROUND %d", g_showdownRound);
		DrawTextOutlined(sw / 2 - (int)strlen(rt) * 8, cy - 18, rt, FONT_BIG, yellow);
	}
}

static void draw_hud() {
	if (in_level()) {
		bool inMatch = (g_matched || g_matchActive);
		if (g_phase == SIM && !g_interRound) { float dt = (float)DeltaTime(); draw_ghost(dt); draw_opp_build(); }

		if (!g_interRound) {   // between rounds the engine's victory transition makes draw calls unsafe - skip the overlay
			char hud[200];
			snprintf(hud, sizeof(hud), "HOP ON EETS %s | %s | t=%.2fs", g_matchActive ? "[MATCH]" : "[practice]", g_status, g_tick / (double)TICK_RATE);
			DrawTextOutlined(10, 30, hud, FONT_NORMAL, Color(255, 232, 40, 255));
			if (inMatch && g_phase == BUILD && g_buildRemain > 0) {
				char cd[64]; snprintf(cd, sizeof(cd), "%s %.1fs", g_retryActive ? "RETRY" : "BUILD", g_buildRemain);
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
			draw_showdown();   // cinematic beat on top of the build phase (match start / round change)
		}   // end !g_interRound overlay
	}

	if (g_seriesOver && g_seriesMsg[0]) {   // persistent series result - drawn on any screen until the next match
		int w = (int)strlen(g_seriesMsg) * 16;
		DrawTextOutlined(ScreenWidth() / 2 - w / 2, ScreenHeight() / 2 - 40, g_seriesMsg, FONT_BIG,
		                 strstr(g_seriesMsg, "WON") ? Color(120, 255, 120, 255) : Color(255, 120, 90, 255));
	}
	if (g_menuOpen && !g_interRound) draw_menu();   // menu works in-level and in the main menu (not mid-transition)
}
