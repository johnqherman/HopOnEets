#pragma once
#include "state.h"

static void draw_ghost_marker(int sx, int sy) {
  Color body(170, 215, 255, 115), edge(220, 240, 255, 180),
      eye(25, 35, 70, 210);
  FillCircle(sx, sy - 6, 20, body, 22);
  FillRect(sx - 20, sy - 6, 40, 22, body);
  for (int i = 0; i < 4; i++)
    FillCircle(sx - 15 + i * 10, sy + 16, 5, body, 10);
  DrawRect(sx - 20, sy - 26, 40, 48, edge, 1.0f);
  FillCircle(sx - 7, sy - 8, 4, eye, 10);
  FillCircle(sx + 7, sy - 8, 4, eye, 10);
}
static void read_eets_anim(Object *e, char &emo, char &mot, int &flip) {
  emo = 'h';
  mot = 'w';
  flip = 0;
  if (!e)
    return;
  if (EmotionExtension *ee = Object_GetEmotionExtension(e)) {
    const char *n = EmotionExtension_GetEmotionName(ee);
    if (n && n[0]) {
      char c = n[0];
      if (c >= 'A' && c <= 'Z')
        c += 32;
      if (c == 'a' || c == 's' || c == 'h')
        emo = c;
    }
  }
  if (WalkingExtension *we = Object_GetWalkingExtension(e)) {
    switch ((WalkState)WalkingExtension_GetState(we)) {
    case WES_Rise:
    case WES_Jumping:
      mot = 'j';
      break;
    case WES_Falling:
      mot = 'f';
      break;
    case WES_Standing:
    case WES_Alert:
    case WES_Inactive:
      mot = 's';
      break;
    default:
      mot = 'w';
      break;
    }
  }
  flip = Object_GetFlipped(e) ? 1 : 0;
}
static const char *live_emo() {
  return g_liveEmotion == 'a' ? "angry" : g_liveEmotion == 's' ? "scared" : "happy";
}
// the opponent's actual current motion token -> .anim (covers eat/land/emote/whale/turn the enum misses);
// "" if no token streamed (old client / not yet sent)
static std::string live_token_path() {
  if (!g_liveAnim[0] || g_liveAnim[0] == '-') return "";
  std::string t = g_liveAnim;
  if (t.rfind("eets_", 0) == 0) return "DATA:Animations/Eets/" + t + ".anim"; // already a full base name
  return std::string("DATA:Animations/Eets/eets_") + live_emo() + "_" + t + ".anim";
}
// coarse emotion x motion fallback (when no token resolves)
static std::string enum_anim_path() {
  const char *mot = g_liveMotion == 'j'   ? "jump"
                    : g_liveMotion == 'f' ? "fall"
                    : g_liveMotion == 's' ? "squat"
                                          : "walk";
  return std::string("DATA:Animations/Eets/eets_") + live_emo() + "_" + mot + ".anim";
}

static void draw_ghost(float dt) {
  if (!g_showGhost)
    return;
  if (!(g_matched && g_liveValid && (Time() - g_liveLastTime) < 0.4))
    return; // freshness gate
  float gx = g_liveX, gy = g_liveY;
  if (!valid_pos(gx, gy))
    return;
  int sx = (int)gx,
      sy = (int)gy; // identity world->screen (single-screen levels)
  // foot-anchor: draw the sprite bottom-center at (sx, footY) so varying-height sprites stay grounded
  // (no float on emotion swap / sink); footY tuned so the feet sit on the ground (was a fixed -32 center).
  int footY = sy + 36; // anchor->feet gap (sprite-height independent via footAnchor); tuned: 28 high / 44 low
  Color tint(255, 255, 255, GHOST_ALPHA);
  bool drew = false;
  // cycle the anim locally (frame -1). frame-sync via g_liveFrame is disabled: the streamed engine frame
  // index doesn't track the live playback frame reliably, so pinning it froze the ghost.
  std::string tok = live_token_path(); // opponent's actual current anim, if streamed
  if (!tok.empty())
    drew = DrawAnim(tok.c_str(), sx, footY, dt, 0.0f, tint, g_liveFlip, 1.0f,
                    g_liveRot, -1, true);
  if (!drew) // token absent/unresolved -> coarse emotion x motion enum
    drew = DrawAnim(enum_anim_path().c_str(), sx, footY, dt, 0.0f, tint,
                    g_liveFlip, 1.0f, g_liveRot, -1, true);
  if (!drew)
    draw_ghost_marker(sx, sy); // last resort if no anim resolves on this install
  char lbuf[48];
  const char *nm = g_oppId.empty() ? "OPPONENT" : g_oppId.c_str();
  const char *label = nm;
  if (g_ranked && g_oppRating > 0) {
    snprintf(lbuf, sizeof(lbuf), "%s (%d)", nm, g_oppRating);
    label = lbuf;
  }
  DrawTextOutlined(sx - (int)strlen(label) * 4, sy - 48, label, FONT_SMALL,
                   Color(180, 220, 255, 255));
}
static void draw_opp_build() {
  if (!g_showGhost || g_oppBuild.empty())
    return;
  for (auto &it : g_oppBuild) {
    if (!valid_pos(it.x, it.y))
      continue;
    int sx = (int)it.x, sy = (int)it.y;
    FillRect(sx - 10, sy - 10, 20, 20, Color(170, 215, 255, 70));
    DrawRect(sx - 10, sy - 10, 20, 20, Color(200, 235, 255, 150), 1.0f);
  }
}
