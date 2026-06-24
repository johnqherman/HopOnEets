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
// read the local Eets's anim state, to stream to the opponent so their ghost mirrors us exactly
static void read_eets_anim(Object* e, char& emo, char& mot, int& flip) {
	emo = 'h'; mot = 'w'; flip = 0;
	if (!e) return;
	if (EmotionExtension* ee = Object_GetEmotionExtension(e)) {
		const char* n = EmotionExtension_GetEmotionName(ee);
		if (n && n[0]) { char c = n[0]; if (c >= 'A' && c <= 'Z') c += 32; if (c == 'a' || c == 's' || c == 'h') emo = c; }
	}
	if (WalkingExtension* we = Object_GetWalkingExtension(e)) {
		switch ((WalkState)WalkingExtension_GetState(we)) {
			case WES_Rise: case WES_Jumping:                      mot = 'j'; break;   // rising / jumping
			case WES_Falling:                                     mot = 'f'; break;
			case WES_Standing: case WES_Alert: case WES_Inactive: mot = 's'; break;   // squat / idle
			default:                                              mot = 'w'; break;   // walking / rolling / thwacking
		}
	}
	flip = Object_GetFlipped(e) ? 1 : 0;
}
// the opponent's streamed state -> a real Eets .anim (eets_<emotion>_<motion>.anim)
static std::string live_anim_path() {
	const char* emo = g_liveEmotion == 'a' ? "angry" : g_liveEmotion == 's' ? "scared" : "happy";
	const char* mot = g_liveMotion == 'j' ? "jump" : g_liveMotion == 'f' ? "fall" : g_liveMotion == 's' ? "squat" : "walk";
	return std::string("DATA:Animations/Eets/eets_") + emo + "_" + mot + ".anim";
}

static void draw_ghost(float dt) {
	if (!g_showGhost) return;
	float gx, gy;
	bool live = (g_matched && g_liveValid);        // realtime opponent takes priority over a recorded ghost
	if (live) { gx = g_liveX; gy = g_liveY; }
	else if (!g_haveGhost || !ghost_pos_at(g_tick, gx, gy)) return;
	if (!valid_pos(gx, gy)) return;                // garbage guard
	int sx = (int)gx, sy = (int)gy;                // identity world->screen (single-screen levels)
	// live opponent: mirror their exact emotion x motion x facing. recorded ghost: the chosen sprite.
	std::string animPath = live ? live_anim_path() : g_ghostAnim;
	bool flip = live && g_liveFlip;
	bool drew = false;                             // animated, semi-transparent Eets
	if (!animPath.empty())
		drew = DrawAnim(animPath.c_str(), sx - 32, sy - 32, dt, 0.0f, Color(255, 255, 255, GHOST_ALPHA), flip);
	if (!drew) draw_ghost_marker(sx, sy);          // fallback if the anim path doesn't resolve on this install
	char lbuf[48];
	const char* nm = g_oppId.empty() ? "OPPONENT" : g_oppId.c_str();
	const char* label;
	if (!live) label = "GHOST";
	else if (g_ranked && g_oppElo > 0) { snprintf(lbuf, sizeof(lbuf), "%s (%d)", nm, g_oppElo); label = lbuf; }
	else label = nm;
	DrawTextOutlined(sx - (int)strlen(label) * 4, sy - 48, label, FONT_SMALL, Color(180, 220, 255, 255));
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
