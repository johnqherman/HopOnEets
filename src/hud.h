#pragma once
#include "state.h"

// showdown overlay: VS face-off (match start) or ROUND N card; self-clears when g_showdownUntil elapses
static void draw_showdown() {
	if (g_showdownKind == 0) return;
	double now = Time();
	if (now >= g_showdownUntil) { g_showdownKind = 0; return; }
	int sw = ScreenWidth(), sh = ScreenHeight(), cy = sh / 2;
	GFX_ResetViewOffset();                           // screen space; else in-level camera offset shifts sprites
	FillRect(0, 0, sw, sh, Color(14, 12, 26, 245));
	int barH = (int)(sh * 0.18);
	FillRect(0, 0, sw, barH, Color(0, 0, 0, 255)); FillRect(0, sh - barH, sw, barH, Color(0, 0, 0, 255));
	Color yellow(255, 232, 40, 255), white(255, 255, 255, 255), green(120, 255, 120, 255), red(255, 120, 90, 255);
	if (g_showdownKind == 1) {                       // match start
		int th = (int)(sh * 0.55);                   // each Eets ~55% screen height (fit-to-height, any res)
		int lcx = (int)(sw * 0.17), rcx = (int)(sw * 0.83), eetsY = cy + 24;
		DrawAnimFrozenFit(g_ghostAnim.c_str(), lcx, eetsY, th, white, false);
		DrawAnimFrozenFit(g_ghostAnim.c_str(), rcx, eetsY, th, white, true);    // mirrored
		DrawTextOutlined(sw / 2 - 26, cy - 24, "VS", FONT_HUGE, yellow);
		int nameY = eetsY - th / 2 - 26;
		char ln[48], rn[48];
		if (g_ranked && g_myElo  > 0) snprintf(ln, sizeof(ln), "%s (%d)", g_playerId.c_str(), g_myElo);  else snprintf(ln, sizeof(ln), "%s", g_playerId.c_str());
		if (g_ranked && g_oppElo > 0) snprintf(rn, sizeof(rn), "%s (%d)", g_oppId.c_str(),    g_oppElo); else snprintf(rn, sizeof(rn), "%s", g_oppId.c_str());
		DrawTextOutlined(lcx - (int)strlen(ln) * 8, nameY, ln, FONT_BIG, Color(150, 220, 255, 255));
		DrawTextOutlined(rcx - (int)strlen(rn) * 8, nameY, rn, FONT_BIG, Color(255, 160, 120, 255));
	} else {                                         // between rounds
		if (g_lastRoundWin != 0) {
			int prev = g_showdownRound - 1;   // card shows upcoming round, so prev is the one just won
			char wl[48];
			if (g_lastRoundWin > 0) snprintf(wl, sizeof(wl), "You won round %d", prev);
			else                    snprintf(wl, sizeof(wl), "%s won round %d", g_oppId.c_str(), prev);
			DrawTextOutlined(sw / 2 - (int)strlen(wl) * 8, cy - 74, wl, FONT_BIG, g_lastRoundWin > 0 ? green : red);
		}
		char rt[32]; snprintf(rt, sizeof(rt), "ROUND %d", g_showdownRound);
		DrawTextOutlined(sw / 2 - (int)strlen(rt) * 12, cy - 24, rt, FONT_HUGE, yellow);
	}
}

// series-end win screen: VICTORY/DEFEAT, score, ranked Elo counting old->new
static void draw_winscreen() {
	int sw = ScreenWidth(), sh = ScreenHeight(), cy = sh / 2;
	GFX_ResetViewOffset();
	FillRect(0, 0, sw, sh, Color(14, 12, 26, 252));
	Color green(120, 255, 120, 255), red(255, 90, 80, 255), white(245, 245, 255, 255), grey(180, 180, 200, 255);
	const char* title = g_seriesWon ? "VICTORY" : "DEFEAT";
	DrawTextOutlined(sw / 2 - (int)strlen(title) * 14, cy - 120, title, FONT_HUGE, g_seriesWon ? green : red);
	const char* sub = g_winForfeit ? "by forfeit" : nullptr;
	char sc[32]; if (!sub) { snprintf(sc, sizeof(sc), "%d - %d", g_youWins, g_ghostWins); sub = sc; }
	DrawTextOutlined(sw / 2 - (int)strlen(sub) * 8, cy - 56, sub, FONT_BIG, white);
	if (g_eloRanked) {
		double a = (Time() - g_winStart - 0.6) / 1.8; if (a < 0) a = 0; if (a > 1) a = 1;   // 0.6s hold, 1.8s count
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
	if (g_winShow && Time() < g_winUntil) { draw_winscreen(); return; }   // win screen owns the frame
	if (g_matched && (!net_up() || g_oppDropped)) {
		GFX_ResetViewOffset();
		char b[64];
		if (!net_up()) snprintf(b, sizeof(b), "RECONNECTING...");
		else { double left = g_oppDropUntil - Time(); if (left < 0) left = 0; snprintf(b, sizeof(b), "DISCONNECTED. AUTO-WIN IN: %.1f", left); }
		DrawTextOutlined(ScreenWidth() / 2 - (int)strlen(b) * 8, ScreenHeight() / 2 - 100, b, FONT_BIG, Color(255, 200, 80, 255));
	}
	if (in_level()) {
		bool inMatch = (g_matched || g_matchActive);
		if (g_showdownKind != 0 && Time() < g_showdownUntil) { draw_showdown(); return; }   // cinematic owns the screen
		if (!g_interRound) { float dt = (float)DeltaTime(); draw_ghost(dt); draw_opp_build(); }   // draw_ghost gates on g_liveValid freshness

		if (!g_interRound) {   // mid-transition victory makes draw calls unsafe
			char hud[200];
			snprintf(hud, sizeof(hud), "HOP ON EETS %s | %s | t=%.2fs", g_matchActive ? "[MATCH]" : "[practice]", g_status, g_tick / (double)TICK_RATE);
			DrawTextOutlined(10, 30, hud, FONT_NORMAL, Color(255, 232, 40, 255));
			if (inMatch && g_phase == BUILD && g_buildRemain > 0) {
				int bs = (int)g_buildRemain; if (g_buildRemain > (double)bs) bs++;   // ceil
				char cd[64]; snprintf(cd, sizeof(cd), "%s %ds", g_retryActive ? "RETRY" : "BUILD", bs);
				DrawTextOutlined(10, 52, cd, FONT_BIG, g_buildRemain < 5 ? Color(255, 90, 80, 255) : Color(255, 232, 40, 255));
			}
			if (g_matched && g_deaths > 0) { char dc[48]; snprintf(dc, sizeof(dc), "deaths: %d", g_deaths); DrawTextOutlined(10, 130, dc, FONT_NORMAL, Color(255, 160, 90, 255)); }
			if (g_matched && g_roundCapSeconds > 0) {   // round clock: full during build, counts down from first Go
				double left = (g_roundStart > 0) ? (g_roundCapSeconds - (Time() - g_roundStart)) : (double)g_roundCapSeconds;
				if (left < 0) left = 0;
				char rc[40]; snprintf(rc, sizeof(rc), "ROUND %d:%02d", (int)left / 60, (int)left % 60);
				DrawTextOutlined(840, 30, rc, FONT_NORMAL, left < 20 ? Color(255, 90, 80, 255) : Color(150, 220, 255, 255));
			}
			if (g_matched) { char on[96]; snprintf(on, sizeof(on), "ONLINE vs %s%s", g_oppId.c_str(), g_ranked ? " [RANKED]" : ""); DrawTextOutlined(10, 112, on, FONT_NORMAL, Color(150, 220, 255, 255)); }
			if (g_roundMsg[0]) DrawTextOutlined(10, 74, g_roundMsg, FONT_NORMAL, Color(255, 255, 255, 255));
			if (g_desync || g_noContest) { char db[80]; snprintf(db, sizeof(db), g_noContest ? "NO CONTEST (desync) - not ranked" : "DESYNC @t%ld - result withheld", g_desyncTick); DrawTextOutlined(10, 92, db, FONT_NORMAL, Color(255, 90, 80, 255)); }
		}
	}

	if (g_menuOpen && !g_interRound) draw_menu();
}
