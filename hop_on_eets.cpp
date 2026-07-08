// "tick" = Engine_GetSimTick (Simulator::DeterministicFrameAdvance step counter
// minus per-round baseline); falls back to counting mod Update calls if the
// counter addr is unavailable. The real counter catches sub-stepping a
// per-Update ++ would miss.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "eetsmod.h"
#include "eets_ui.h"
#include "src/state.h"
#include "src/hash.h"
#include "src/determinism.h"
#include "src/levels.h"
#include "src/net.h"
#include "src/recorder.h"
#include "src/itemtable.h"
#include "src/ghostview.h"
#include "src/menu.h"
#include "src/match.h"
#include "src/hud.h"

extern "C" void EetsMod_Init() { mod_init(); }

extern "C" void EetsMod_OnMouse(int x, int y, int button, int down) {
  UI::FeedMouse(x, y, button, down);
}

extern "C" void EetsMod_OnText(const char *utf8) { mod_on_text(utf8); }

extern "C" void EetsMod_Shutdown() { net_close(); }

extern "C" void EetsMod_OnEvent(const char *name, void *a, void *b) {
  match_on_event(name, a, b);
}

extern "C" void EetsMod_OnKey(int key, int mods, int down) {
  mod_on_key(key, mods, down);
}

extern "C" void EetsMod_Update() {
  // in a match the HOE overlay owns Esc; keep the engine from seeing it (no vanilla pause/menu/sound)
  Eets::SetEscapeSwallow(g_matched && in_level());
  net_poll();
  net_reconnect_tick(); // relay holds the match ~20s across a mid-match drop
  net_queue_tick();     // re-assert/reconnect while waiting so long queuers don't silently fall out
  match_update();
  draw_hud();
}
