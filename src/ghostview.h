// ghostview.h - draw the opponent (live or recorded) and their build as in-level ghosts.
// world == screen on single-screen levels (identity GFX view); see hop_on_eets.cpp header note.
#pragma once
#include "state.h"
#include "recorder.h"   // ghost_pos_at

static void draw_ghost_marker(int sx, int sy) {
	Color body(170, 215, 255, 115), edge(220, 240, 255, 180), eye(25, 35, 70, 210);
	FillCircle(sx, sy - 6, 20, body, 22);          // head/body
	FillRect(sx - 20, sy - 6, 40, 22, body);       // skirt
	for (int i = 0; i < 4; i++) FillCircle(sx - 15 + i * 10, sy + 16, 5, body, 10);  // wavy hem
	DrawRect(sx - 20, sy - 26, 40, 48, edge, 1.0f);
	FillCircle(sx - 7, sy - 8, 4, eye, 10);
	FillCircle(sx + 7, sy - 8, 4, eye, 10);
}
static void draw_ghost(float dt) {
	if (!g_showGhost) return;
	float gx, gy;
	bool live = (g_matched && g_liveValid);        // realtime opponent takes priority over a recorded ghost
	if (live) { gx = g_liveX; gy = g_liveY; }
	else if (!g_haveGhost || !ghost_pos_at(g_tick, gx, gy)) return;
	if (!valid_pos(gx, gy)) return;                // garbage guard
	int sx = (int)gx, sy = (int)gy;                // identity world->screen (single-screen levels)
	bool drew = false;                             // animated, semi-transparent Eets
	if (!g_ghostAnim.empty())
		drew = DrawAnim(g_ghostAnim.c_str(), sx - 32, sy - 32, dt, 0.0f, Color(255, 255, 255, GHOST_ALPHA));
	if (!drew) draw_ghost_marker(sx, sy);          // fallback if the anim path doesn't resolve on this install
	DrawTextOutlined(sx - 26, sy - 48, live ? "OPPONENT" : "GHOST", FONT_SMALL, Color(180, 220, 255, 255));
}
// the opponent's locked-in build, drawn as translucent ghost items
static void draw_opp_build() {
	if (!g_showGhost || g_oppBuild.empty()) return;
	for (auto& it : g_oppBuild) {
		if (!valid_pos(it.x, it.y)) continue;
		int sx = (int)it.x, sy = (int)it.y;
		FillRect(sx - 10, sy - 10, 20, 20, Color(170, 215, 255, 70));
		DrawRect(sx - 10, sy - 10, 20, 20, Color(200, 235, 255, 150), 1.0f);
	}
}
