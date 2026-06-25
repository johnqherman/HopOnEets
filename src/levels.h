#pragma once
#include "state.h"

struct PoolLevel { void* fnp; int hub; };   // fnp = FileNamePair* (first field of catalog entry)
static std::vector<PoolLevel> g_pool;
static bool g_poolBuilt = false;

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
	char* sentinel = *(char**)(lm + 0x4);   // MSVC list head sentinel; circular chain
	if (!sentinel) return;
	int idx = 0;
	for (char* hub = *(char**)sentinel; hub && hub != sentinel; hub = *(char**)hub, ++idx) {
		if (idx == 0) continue;             // skip World 1 (intro)
		pool_add_hub(*(char**)(hub + 0x20), *(char**)(hub + 0x24), 0x5c, idx);
	}
#else
	char* hub = *(char**)(lm + 0x10);
	for (int idx = 0; hub; hub = *(char**)hub, ++idx) {
		if (idx == 0) continue;             // skip World 1 (intro)
		pool_add_hub(*(char**)(hub + 0x28), *(char**)(hub + 0x30), 0x70, idx);
	}
#endif
}

static int pool_size() { if (!g_poolBuilt || g_pool.empty()) build_pool(); return (int)g_pool.size(); }
static int pool_resolve(int idx) { int n = pool_size(); return n > 0 ? ((idx % n) + n) % n : -1; }

// World_EnterLevel uses LevelDirectory=1 ("LEVELS:Game\\"); dir 0 ("USER:Custom Levels\\") = empty build.
// Must keep dir==1's GUI prime (Creator::LoadLevel) else NonDeterministicUpdate crashes in
// GUI::OnUpdate/Widget::GetParent on unprimed tree.
static void load_level(const void* fnp) {
	if (!fnp) return;
	World_EnterLevel(fnp);
}
static void load_match_level() {
	int i = pool_resolve(g_levelIndex);
	if (i >= 0 && i < (int)g_pool.size()) load_level(g_pool[i].fnp);
}
