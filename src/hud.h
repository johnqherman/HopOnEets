#pragma once
#include "state.h"

// showdown overlay: VS face-off (match start) or ROUND N card; self-clears when
// g_showdownUntil elapses
static void draw_showdown() {
  if (g_showdownKind == 0)
    return;
  double now = Time();
  if (now >= g_showdownUntil) {
    g_showdownKind = 0;
    return;
  }
  int sw = ScreenWidth(), sh = ScreenHeight(), cy = sh / 2;
  GFX_ResetViewOffset(); // screen space; else in-level camera offset shifts
                         // sprites
  FillRect(0, 0, sw, sh, Color(14, 12, 26, 245));
  int barH = (int)(sh * 0.18);
  FillRect(0, 0, sw, barH, Color(0, 0, 0, 255));
  FillRect(0, sh - barH, sw, barH, Color(0, 0, 0, 255));
  Color yellow(255, 232, 40, 255), white(255, 255, 255, 255),
      green(120, 255, 120, 255), red(255, 120, 90, 255);
  if (g_showdownKind == 1) { // match start
    int th =
        (int)(sh *
              0.55); // each Eets ~55% screen height (fit-to-height, any res)
    int lcx = (int)(sw * 0.17), rcx = (int)(sw * 0.83), eetsY = cy + 24;
    DrawAnimFrozenFit(g_ghostAnim.c_str(), lcx, eetsY, th, white, false);
    DrawAnimFrozenFit(g_ghostAnim.c_str(), rcx, eetsY, th, white,
                      true); // mirrored
    DrawTextCenteredOutlined(sw / 2, cy - 24, "VS", FONT_HUGE, yellow,
                             Color(0, 0, 0, 200), STYLE_BRADY);
    int nameY = eetsY - th / 2 - 26;
    char ln[48], rn[48];
    if (g_ranked && g_myRating > 0)
      snprintf(ln, sizeof(ln), "%s (%d)", g_playerId.c_str(), g_myRating);
    else
      snprintf(ln, sizeof(ln), "%s", g_playerId.c_str());
    if (g_ranked && g_oppRating > 0)
      snprintf(rn, sizeof(rn), "%s (%d)", g_oppId.c_str(), g_oppRating);
    else
      snprintf(rn, sizeof(rn), "%s", g_oppId.c_str());
    DrawTextCenteredOutlined(lcx, nameY, ln, FONT_BIG, Color(150, 220, 255, 255),
                             Color(0, 0, 0, 200), STYLE_BRADY);
    DrawTextCenteredOutlined(rcx, nameY, rn, FONT_BIG, Color(255, 160, 120, 255),
                             Color(0, 0, 0, 200), STYLE_BRADY);
  } else { // between rounds
    if (g_lastRoundWin != 0) {
      int prev = g_showdownRound -
                 1; // card shows upcoming round, so prev is the one just won
      char wl[48];
      if (g_lastRoundWin > 0)
        snprintf(wl, sizeof(wl), "You won round %d", prev);
      else
        snprintf(wl, sizeof(wl), "%s won round %d", g_oppId.c_str(), prev);
      DrawTextCenteredOutlined(sw / 2, cy - 74, wl, FONT_BIG,
                               g_lastRoundWin > 0 ? green : red,
                               Color(0, 0, 0, 200), STYLE_BRADY);
    } else if (g_lastRoundTie) { // both DNF: same round number, fresh map -> mulligan
      DrawTextCenteredOutlined(sw / 2, cy - 74, "MULLIGAN!", FONT_BIG, yellow,
                               Color(0, 0, 0, 200), STYLE_BRADY);
    }
    char rt[32];
    snprintf(rt, sizeof(rt), "ROUND %d", g_showdownRound);
    DrawTextCenteredOutlined(sw / 2, cy - 24, rt, FONT_HUGE, yellow,
                             Color(0, 0, 0, 200), STYLE_BRADY);
  }
}

// series-end win screen: VICTORY/DEFEAT, score, ranked rating counting old->new
static void draw_winscreen() {
  int sw = ScreenWidth(), sh = ScreenHeight(), cy = sh / 2;
  GFX_ResetViewOffset();
  FillRect(0, 0, sw, sh, Color(14, 12, 26, 252));
  Color green(120, 255, 120, 255), red(255, 90, 80, 255),
      white(245, 245, 255, 255), grey(180, 180, 200, 255),
      gold(230, 220, 120, 255);
  const char *title =
      g_seriesNoContest ? "NO CONTEST" : (g_seriesWon ? "VICTORY" : "DEFEAT");
  const Color shadow(0, 0, 0, 200);
  DrawTextCenteredOutlined(sw / 2, cy - 120, title, FONT_HUGE,
                           g_seriesNoContest ? gold : (g_seriesWon ? green : red),
                           shadow, STYLE_BRADY);
  const char *sub = g_seriesNoContest ? "no result - too many draws"
                    : g_winForfeit    ? "by forfeit"
                                      : nullptr;
  char sc[32];
  if (!sub) {
    snprintf(sc, sizeof(sc), "%d - %d", g_youWins, g_ghostWins);
    sub = sc;
  }
  DrawTextCenteredOutlined(sw / 2, cy - 56, sub, FONT_BIG, white, shadow,
                           STYLE_BRADY);
  if (g_ratingRanked) {
    double a = (Time() - g_winStart - 0.6) / 1.8;
    if (a < 0)
      a = 0;
    if (a > 1)
      a = 1; // 0.6s hold, 1.8s count
    double v = g_ratingOld + (g_ratingNew - g_ratingOld) * a;
    int shown = (int)(v + 0.5);
    char el[48];
    snprintf(el, sizeof(el), "RATING %d", shown);
    DrawTextCenteredOutlined(sw / 2, cy + 6, el, FONT_BIG,
                             Color(255, 232, 40, 255), shadow, STYLE_BRADY);
    int d = g_ratingNew - g_ratingOld;
    char dl[24];
    snprintf(dl, sizeof(dl), "%+d", d);
    DrawTextCenteredOutlined(sw / 2, cy + 46, dl, FONT_BIG, d >= 0 ? green : red,
                             shadow, STYLE_BRADY);
  }
  DrawTextCenteredOutlined(sw / 2, sh - 56, "returning to menu...", FONT_NORMAL,
                           grey, shadow, STYLE_BRADY);
}

static void draw_hud() {
  UI::NewFrame();   // once per frame, before any widget
  if (g_winShow && Time() < g_winUntil) {
    draw_winscreen();
    return;
  } // win screen owns the frame
  if (g_matched && (!net_up() || g_oppDropped)) {
    GFX_ResetViewOffset();
    char b[64];
    if (!net_up())
      snprintf(b, sizeof(b), "RECONNECTING...");
    else {
      double left = g_oppDropUntil - Time();
      if (left < 0)
        left = 0;
      snprintf(b, sizeof(b), "DISCONNECTED. AUTO-WIN IN: %.1f", left);
    }
    DrawTextCenteredOutlined(ScreenWidth() / 2, ScreenHeight() / 2 - 100, b,
                             FONT_BIG, Color(255, 200, 80, 255),
                             Color(0, 0, 0, 200), STYLE_BRADY);
  }
  if (in_level()) {
    bool inMatch = (g_matched || g_matchActive);
    if (g_showdownKind != 0 && Time() < g_showdownUntil) {
      draw_showdown();
      return;
    } // cinematic owns the screen
    if (!g_interRound) {
      float dt = (float)DeltaTime();
      draw_ghost(dt);
      draw_opp_build();
    } // draw_ghost gates on g_liveValid freshness

    if (!g_interRound) { // mid-transition victory makes draw calls unsafe
      const Color shadow(0, 0, 0, 200), cyan(150, 220, 255, 255),
          amber(255, 232, 40, 255), warn(255, 90, 80, 255);
      const int pad = 28; // matching left/right margin from the screen edges
      // who you're playing
      if (g_matched) {
        char on[96];
        snprintf(on, sizeof(on), "ONLINE vs %s   %s", g_oppId.c_str(),
                 g_ranked ? "(RANKED)" : "(CASUAL)");
        DrawTextOutlined(pad, 30, on, FONT_NORMAL, cyan, shadow, STYLE_BRADY);
      }
      // build / retry countdown
      if (inMatch && g_phase == BUILD && g_buildRemain > 0) {
        int bs = (int)g_buildRemain;
        if (g_buildRemain > (double)bs)
          bs++; // ceil
        char cd[64];
        snprintf(cd, sizeof(cd), "%s %ds", g_retryActive ? "RETRY" : "BUILD",
                 bs);
        DrawTextOutlined(pad, 62, cd, FONT_HUGE, g_buildRemain < 5 ? warn : amber,
                         shadow, STYLE_BRADY);
      }
      // round number + the round's remaining time, flush to the right edge
      if (g_matched && g_roundCapSeconds > 0) {
        double left = (g_roundStart > 0)
                          ? (g_roundCapSeconds - (Time() - g_roundStart))
                          : (double)g_roundCapSeconds;
        if (left < 0)
          left = 0;
        char rc[40];
        snprintf(rc, sizeof(rc), "ROUND %d   %d:%02d",
                 g_youWins + g_ghostWins + 1, (int)left / 60, (int)left % 60);
        // anchor on a constant-width template, not the live string: proportional digit widths change every
        // second, so measuring `rc` itself made the position jitter horizontally. fixed template = stable x.
        // the template uses the widest digit (0), so trim a bit to sit flush right with typical digits.
        int tw = MeasureTextWidth("ROUND 0   0:00", FONT_BIG, STYLE_BRADY) - 36;
        if (tw <= 0) tw = 12 * UI::fontPx(FONT_BIG) * 3 / 5; // Win fallback
        DrawTextOutlined(ScreenWidth() - pad - tw, 30, rc, FONT_BIG, // ~pad from right, matching ONLINE's pad from left
                         left < 20 ? warn : cyan, shadow, STYLE_BRADY);
      }
      // mulligan-vote status, just under the round timer (right-aligned to match)
      if (g_matched && (g_oppMull || g_localMull)) {
        const char *mv = g_oppMull ? "OPPONENT VOTED TO MULLIGAN"
                                   : "YOU VOTED - WAITING FOR OPPONENT";
        int mw = MeasureTextWidth(mv, FONT_NORMAL, STYLE_BRADY);
        if (mw <= 0)
          mw = (int)strlen(mv) * UI::fontPx(FONT_NORMAL) * 3 / 5;
        DrawTextOutlined(ScreenWidth() - pad - mw, 62, mv, FONT_NORMAL,
                         g_oppMull ? amber : cyan, shadow, STYLE_BRADY);
      }
    }
  }

  // main menu, or the in-level pause screen. a loading screen (incl. the menu->match load, which can still
  // read as "main menu") suppresses the pill outright, so factor !World_IsLoading() across both.
  bool menu_surface = !World_IsLoading() && (World_IsInMainMenu() || World_IsPaused());
  if (!g_interRound) {
    if (g_menuOpen) {
      if (menu_surface)
        draw_menu();
      else
        g_menuOpen = false;   // resuming the level (or a load screen) dismisses the menu
    } else if (menu_surface) {
      UI::SetClickSound("GUI Click 1");
      UI::SetHoverSound("GUI MouseOver");
      if (UI::TabButton(42, 150, "HOP ON EETS"))   // under + left-aligned with the eets title/tagline
        g_menuOpen = true;
    }
  }
}
