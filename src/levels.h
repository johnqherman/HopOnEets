// levels.h - ranked map pool, built at runtime from the engine's LevelManager catalog.
// Pool rule (per design): non-tutorial levels only. World 1 is the intro world (skip the whole hub),
// and every other hub starts with its own intro levels - skip the first `g_hubIntroSkip` of each hub.
// (The hub's `tutorial_minimum` field is the intended exact count; reading it reliably needs more RE,
//  so the skip count is configurable for now - tune it in the menu against the real catalog.)
//
// LevelManager hub catalog layout (RE'd both platforms; the std::unordered_map node chain):
//   Linux: GetLevelManager()+0x10 = head; null-terminated; hub+0x00 next, +0x08 id, +0x28 begin,
//          +0x30 end; entry stride 0x70.
//   Win:   GetLevelManager()+0x04 = MSVC list _Myhead SENTINEL; CIRCULAR (end = node==sentinel);
//          hub+0x00 next, +0x08 id, +0x20 begin, +0x24 end; entry stride 0x5c (32-bit build).
// Read-only walk; the framework crash-guard disables the mod if an offset is wrong, so it is safe to probe.
#pragma once
#include "state.h"

struct PoolLevel { void* fnp; int hub; };   // fnp = FileNamePair* (first field of the catalog entry)
static std::vector<PoolLevel> g_pool;
static bool g_poolBuilt = false;

// collect a hub's non-intro level entries (shared by both platforms)
static void pool_add_hub(char* begin, char* end, long stride, int idx) {
	int li = 0;
	for (char* lvl = begin; lvl && lvl != end; lvl += stride, ++li)
		if (li >= g_hubIntroSkip) g_pool.push_back({ (void*)lvl, idx });
}

static void build_pool() {
	g_pool.clear(); g_poolBuilt = true;
	char* lm = (char*)GameUtil_GetLevelManager();
	if (!lm) return;
#ifdef _WIN32
	char* sentinel = *(char**)(lm + 0x4);   // MSVC list head sentinel; chain is circular
	if (!sentinel) return;
	int idx = 0;
	for (char* hub = *(char**)sentinel; hub && hub != sentinel; hub = *(char**)hub, ++idx) {
		if (idx == 0) continue;             // skip World 1 (intro world)
		pool_add_hub(*(char**)(hub + 0x20), *(char**)(hub + 0x24), 0x5c, idx);
	}
#else
	char* hub = *(char**)(lm + 0x10);       // null-terminated chain
	for (int idx = 0; hub; hub = *(char**)hub, ++idx) {
		if (idx == 0) continue;             // skip World 1 (intro world)
		pool_add_hub(*(char**)(hub + 0x28), *(char**)(hub + 0x30), 0x70, idx);
	}
#endif
}

// retry while empty: the catalog may not be loaded yet the first time this is called
static int pool_size() { if (!g_poolBuilt || g_pool.empty()) build_pool(); return (int)g_pool.size(); }
// the actual level for a relay-chosen index: same pool on both clients (same game) -> same level
static int pool_resolve(int idx) { int n = pool_size(); return n > 0 ? ((idx % n) + n) % n : -1; }

// Enter gameplay for a catalog level via the REAL play path (framework World_EnterLevel): StartBuilder with
// LevelDirectory=1 ("LEVELS:Game\\", where the catalog levels live - dir 0 = "USER:Custom Levels\\" =
// file-not-found = empty build phase) PLUS the GUI prime that Creator::LoadLevel only does on its dir==0
// branch (else NonDeterministicUpdate crashes in GUI::OnUpdate/Widget::GetParent on the unprimed tree). The
// framework crash-guard disables the mod safely if an offset/ABI is off. (Win addrs+offsets RE'd; unverified in-game.)
static void load_level(const void* fnp) {
	if (!fnp) return;
	World_EnterLevel(fnp);
}
static void load_match_level() {
	int i = pool_resolve(g_levelIndex);
	if (i >= 0 && i < (int)g_pool.size()) load_level(g_pool[i].fnp);
}
