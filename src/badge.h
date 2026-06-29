#pragma once
#include "state.h"

// ladder-rank badge: gold #1, silver #2-10, bronze #11-50; false (no badge) otherwise.
// shared by the VS showdown (hud.h) and the HOE menu name row (menu.h).
static bool rankTier(int rank, Color &bg, Color &fg) {
  if (rank <= 0)
    return false;
  fg = Color(40, 30, 10, 255); // dark text reads on all three metals
  if (rank == 1)
    bg = Color(255, 210, 40, 255); // gold
  else if (rank <= 10)
    bg = Color(200, 205, 215, 255); // silver
  else if (rank <= 50)
    bg = Color(205, 130, 70, 255); // bronze
  else
    return false;
  return true;
}

// draw a "#N rank pill + name" as one left-aligned group centered on cx (text top = topY). Laying
// both out from the same left edge keeps the badge-to-name gap exact - unlike centering the name and
// placing the badge off its measured width, which drifts because the engine's centering width and
// MeasureTextWidth diverge for longer strings. font = the name's FontSize so the pill sits in-line.
static void draw_name_with_badge(int cx, int topY, const char *name, int rank, Color nameCol,
                                 int font = FONT_BIG) {
  int fpx = UI::fontPx(font);
  // widths are char-count estimates, NOT MeasureTextWidth: the engine measurer reads the shared
  // font-size field, which the VS DrawTextPx leaves at a stale px here -> wildly wrong widths (a
  // giant pill). The estimate is layout-only (badge size + group centering); the visible glyphs
  // still render at the real font, and the badge-to-name gap is exact regardless.
  int em = fpx * 3 / 5; // ~0.6em per glyph advance for this cartoon font
  int nlen = 0;
  while (name[nlen])
    nlen++;
  int nameW = nlen * em;
  Color bg, fg;
  int badgeW = 0, gap = 0, bh = fpx + 6;
  char b[8];
  bool hasBadge = rankTier(rank, bg, fg);
  if (hasBadge) {
    snprintf(b, sizeof(b), "#%d", rank);
    int blen = 0;
    while (b[blen])
      blen++;
    badgeW = blen * em + 14;
    gap = 6;
  }
  int gx = cx - (badgeW + gap + nameW) / 2; // group left edge, centered on cx
  if (hasBadge) {
    UI::FillRoundRect(gx, topY - 3, badgeW, bh, bh / 2, bg);
    DrawTextCentered(gx + badgeW / 2, UI::centerY(topY - 3, bh, font), b, font, fg, STYLE_BRADY);
  }
  DrawTextOutlined(gx + badgeW + gap, topY, name, font, nameCol, Color(0, 0, 0, 200), STYLE_BRADY);
}

// "#N" pill with its LEFT edge at x, sized for FONT_SMALL (menu rows). Returns the advance
// (badge width + trailing gap) to place following text, or 0 when there's no badge.
static int draw_rank_badge_left(int x, int textTopY, int rank) {
  Color bg, fg;
  if (!rankTier(rank, bg, fg))
    return 0;
  char b[8];
  snprintf(b, sizeof(b), "#%d", rank);
  int fpx = UI::fontPx(FONT_SMALL);
  int tw = MeasureTextWidth(b, FONT_SMALL, STYLE_BRADY);
  if (tw <= 0) {
    int n = 0;
    while (b[n])
      n++;
    tw = n * fpx * 3 / 5;
  }
  int padX = 6, padY = 2, gap = 6;
  int bw = tw + 2 * padX, bh = fpx + 2 * padY;
  int by = textTopY - padY;
  UI::FillRoundRect(x, by, bw, bh, bh / 2, bg);
  DrawTextCentered(x + bw / 2, UI::centerY(by, bh, FONT_SMALL), b, FONT_SMALL, fg, STYLE_BRADY);
  return bw + gap;
}
