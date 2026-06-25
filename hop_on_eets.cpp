// hop_on_eets.cpp - Hop On Eets: competitive solution-race mod for Eets.
//
// Built on eets-mod-framework; manifest sets `sim = 1`. This file is the thin entry layer; the logic
// lives in src/*.h, all #included into this one translation unit (the framework compiles/packs a single .cpp):
//   src/state.h       shared config, globals, types, helpers
//   src/hash.h        FNV-1a state snapshot (determinism / desync)
//   src/determinism.h pin seed/FPS/det-mode, lock game speed, force-start
//   src/net.h         realtime net client (WebSocket/TLS -> relay)
//   src/recorder.h    build-placement capture (shared live to the opponent) + build/sim lifecycle
//   src/ghostview.h   draw the live opponent + their build in-level
//   src/menu.h        custom Eets::UI menu (the live config surface)
//   src/match.h       match lifecycle: round resolution, engine-event handlers, per-frame state machine
//   src/hud.h         all in-game overlay drawing (status, clocks, deaths, ghost, series banner, menu)
//
// Coordinate note: classic Eets puzzle levels are single-screen with an identity GFX view, and the
// framework treats render coords as world coords (spawner places at the cursor) - so world position
// == on-screen position here. Scrolling/zoomed levels would need the GFX view matrix (FUN_0048f2c0).
//
// "tick" tracks the TRUE engine sim-tick (Engine_GetSimTick = Simulator::DeterministicFrameAdvance's
// free-running step counter, minus a per-round baseline taken at sim start); it falls back to counting
// mod Update calls only if the counter address is unavailable. Reading the real counter keeps the
// desync-hash tick aligned with the engine and catches sub-stepping a per-Update ++ would miss.
#ifdef _WIN32
#include <winsock2.h>    // must precede any windows.h in the TU (eetsmod.h pulls windows.h)
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
#include "src/ghostview.h"
#include "src/menu.h"
#include "src/match.h"      // round resolution + per-frame match state machine (logic)
#include "src/hud.h"        // all in-game overlay drawing

extern "C" void EetsMod_Init() { mod_init(); }   // identity + connect bootstrap (src/net.h)

extern "C" void EetsMod_OnMouse(int x, int y, int button, int down) { UI::FeedMouse(x, y, button, down); }

extern "C" void EetsMod_OnText(const char* utf8) { mod_on_text(utf8); }   // src/menu.h

extern "C" void EetsMod_Shutdown() { net_close(); }

extern "C" void EetsMod_OnEvent(const char* name, void* a, void* b) { match_on_event(name, a, b); }

extern "C" void EetsMod_OnKey(int key, int mods, int down) { mod_on_key(key, mods, down); }   // src/menu.h

extern "C" void EetsMod_Update() {
	net_poll();
	net_reconnect_tick();   // recover from a mid-match network drop (relay holds the match ~20s)
	match_update();   // match lifecycle / state machine (src/match.h)
	draw_hud();       // all in-game overlay drawing (src/hud.h)
}
