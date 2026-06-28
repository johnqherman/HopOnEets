#pragma once
#include "state.h"
#include "net.h"

static void start_queue(bool ranked) {
  net_action(ranked ? "queue ranked" : "queue casual");
  g_queueRanked = ranked;
  g_queueing = true;
  g_queueStart = Time();
  g_lastQueueAssert = Time();   // just asserted; next re-assert is one interval out
  g_netMsg[0] = 0;   // clear stale host/join status on entering the queue
}

static void draw_menu() {
  UI::SetClickSound("GUI Click 1");
  UI::SetHoverSound("GUI MouseOver");
  UI::Begin(42, 150, 360, "HOP ON EETS");   // top-left aligned with the pill
  if (UI::CloseButton())
    g_menuOpen = false;

  if (net_up()) {
    UI::SectionStatus("CONNECTED", Color(70, 220, 90, 255));
  } else {
    UI::SectionStatus("DISCONNECTED", Color(235, 70, 55, 255));
    if (UI::Button("Retry connection"))
      net_connect();
  }
  if (!g_matched) { // name editing only in the lobby - no mid-match name changes
    if (g_nameEntry) {
      char nl[64];
      snprintf(nl, sizeof(nl), "User: %s_", g_nameBuf.c_str());
      UI::Label(nl);
      if (UI::Button("Set name")) {
        set_player_name(g_nameBuf);
        g_nameEntry = false;
        StopTextInput();
      }
    } else {
      char nm[64];
      if (g_myRating > 0)
        snprintf(nm, sizeof(nm), "User: %s (%d)", g_playerId.c_str(),
                 g_myRating);
      else
        snprintf(nm, sizeof(nm), "User: %s", g_playerId.c_str());
      UI::Label(nm);
      if (UI::Button("Edit name")) {
        g_nameEntry = true;
        g_nameBuf = g_playerId;
        StartTextInput();
      }
    }
  }

  if (g_matched) {
    // vote to replay this round; fires only if the opponent also votes
    if (!g_localMull) {
      if (UI::Button("Vote to mulligan"))
        vote_mulligan(true);
    } else {
      if (UI::Button("Cancel mulligan vote"))
        vote_mulligan(false);
    }
    if (g_oppMull)
      UI::Label("Opponent voted to mulligan");
    if (!g_confirmForfeit) {
      if (UI::Button("Leave & forfeit"))
        g_confirmForfeit = true;
    } else {
      UI::Label("Forfeit the match?");
      UI::BeginColumns(2);
      if (UI::Button("Yes, forfeit")) {
        g_confirmForfeit = false;
        forfeit_match();
      }
      UI::NextColumn();
      if (UI::Button("Cancel"))
        g_confirmForfeit = false;
      UI::EndColumns();
    }
  } else if (g_queueing) {
    char sb[48];
    int secs = (int)(Time() - g_queueStart);
    snprintf(sb, sizeof(sb), "Finding a %s match...  %d:%02d",
             g_queueRanked ? "ranked" : "casual", secs / 60, secs % 60);
    UI::Label(sb);
    UI::State &st = UI::S();
    st.cy += 58;   // room above the Cancel button for the eets
    int th = 56, brd = 6, btnY = st.cy;
    bool cancel = UI::Button("Cancel search");   // its top black border sits at btnY
    // a little eets marches the full panel width (cut at the panel sides), feet on the button's top border
    static int ew = 0;   // fixed once (per-frame AnimFitWidth varies with the walk cycle)
    if (ew <= 8) { ew = AnimFitWidth(g_ghostAnim.c_str(), th); if (ew < 8) ew = th * 4 / 5; }
    int lo = st.px - ew, hi = st.px + st.pw + ew, range = (hi > lo) ? hi - lo : 1;   // off-left -> off-right -> wrap
    static double mpos = 0.0;
    mpos += DeltaTime() * 80.0; while (mpos >= range) mpos -= range;
    int ex = lo + (int)mpos;
    ClipRect(st.px + brd, btnY - th, st.pw - 2 * brd, th + 6);   // panel-width clip
    DrawAnimFit(g_ghostAnim.c_str(), ex, btnY - th / 2 + 2, th, (float)DeltaTime());
    ClipReset();
    if (cancel) {   // no server dequeue cmd; reconnect drops us from the relay queue
      g_queueing = false;
      net_close();
      net_connect();
    }
  } else {
    g_confirmForfeit = false;
    UI::BeginColumns(2);
    if (g_hostCode[0]) {   // hosting -> the Host button becomes Copy code
      if (UI::Button("Copy code"))
        SetClipboard(g_hostCode);
    } else if (UI::Button("Host code"))
      net_action("host");
    UI::NextColumn();
    if (g_codeEntry) {
      char ce[40];
      snprintf(ce, sizeof(ce), "Code: %s_", g_codeBuf.c_str());
      if (UI::Button(ce)) {   // the button is the input; click (or Enter) connects
        g_codeEntry = false;
        StopTextInput();
        if (!g_codeBuf.empty())
          net_action("join " + g_codeBuf);
      }
    } else if (UI::Button("Join code")) {
      g_codeEntry = true;
      g_codeBuf.clear();
      StartTextInput();
    }
    UI::EndColumns();

    UI::BeginColumns(2);   // public matchmaking: Ranked | Casual
    if (UI::Button("Ranked"))
      start_queue(true);
    UI::NextColumn();
    if (UI::Button("Casual"))
      start_queue(false);
    UI::EndColumns();
  }

  if (g_netMsg[0] && !g_queueing)
    UI::Label(g_netMsg);   // host/join status; hidden while queueing

  UI::End();
}

// name: printable non-space ASCII, capped; join code: uppercased alphanumerics,
// 6 chars
static void mod_on_text(const char *utf8) {
  if (!utf8)
    return;
  if (g_nameEntry) {
    for (const char *p = utf8; *p; ++p)
      if ((unsigned char)*p > ' ' && (unsigned char)*p < 0x7f &&
          (int)g_nameBuf.size() < MAX_PLAYER_NAME)
        g_nameBuf.push_back(*p);
    return;
  }
  if (!g_codeEntry)
    return;
  for (const char *p = utf8; *p; ++p) {
    char ch = *p;
    if (ch >= 'a' && ch <= 'z')
      ch = (char)(ch - 'a' + 'A');
    if (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) &&
        g_codeBuf.size() < 6)
      g_codeBuf.push_back(ch);
  }
}
// Ctrl+Shift+H/R are dev match-mode toggle / new-match reset
static void mod_on_key(int key, int mods, int down) {
  if (!down)
    return;
  if (g_nameEntry) {
    if (key == EKEY_BACKSPACE) {
      if (!g_nameBuf.empty())
        g_nameBuf.pop_back();
      return;
    }
    if (key == EKEY_RETURN) {
      set_player_name(g_nameBuf);
      g_nameEntry = false;
      StopTextInput();
      return;
    }
    if (key == EKEY_ESCAPE) {
      g_nameEntry = false;
      StopTextInput();
      return;
    }
  }
  if (g_codeEntry) {
    if (key == EKEY_BACKSPACE) {
      if (!g_codeBuf.empty())
        g_codeBuf.pop_back();
      return;
    }
    if (key == EKEY_RETURN) {
      g_codeEntry = false;
      StopTextInput();
      if (!g_codeBuf.empty())
        net_action("join " + g_codeBuf);
      return;
    }
    if (key == EKEY_ESCAPE) {
      g_codeEntry = false;
      g_codeBuf.clear();
      StopTextInput();
      return;
    }
  }
}
