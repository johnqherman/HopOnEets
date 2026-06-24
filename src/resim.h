// resim.h - headless batch re-simulation: the authoritative cross-platform verifier (spec Part 6).
//
// The portable unit of truth is the input log (build placements + seed), NOT a state hash (FP physics
// differs across OS - see hash.h / spec Part 5). To get an authoritative result, re-simulate the log
// on ONE canonical build and read the outcome. This module is the mod-side driver of that re-sim: in
// `resim_file` mode it runs UNATTENDED - load the level, apply the build via World_CreateObject,
// force-start, run to completion (or DNF timeout), then write a verdict comparing the re-sim outcome
// to the submitter's claim. Pair it with a null-backend launch of the game (no window/audio) to make
// it fully headless - see docs/headless-resim.md.
#pragma once
#include "state.h"
#include "levels.h"
#include "determinism.h"

static const long RESIM_TICK_TOL = 2;   // finish-tick tolerance (canonical build vs the submitter's build)

// --- minimal JSON field scanning (only the fields our own replay format emits; not a general parser) ---
// `key` must include the trailing colon, e.g. "\"seed\":". Reads the number right after it.
static bool rs_num_after(const std::string& j, size_t from, const char* key, double& out) {
	size_t k = j.find(key, from); if (k == std::string::npos) return false;
	out = strtod(j.c_str() + k + strlen(key), nullptr);   // strtod skips leading whitespace
	return true;
}

static bool resim_parse(const std::string& path) {
	FILE* f = fopen(path.c_str(), "rb"); if (!f) { Eets::Log("hop_on_eets resim: cannot open %s", path.c_str()); return false; }
	std::string j; char buf[4096]; size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) j.append(buf, n);
	fclose(f);

	double d;
	if (rs_num_after(j, 0, "\"seed\":", d)) g_seed = (uint32_t)d;
	if (rs_num_after(j, 0, "\"level_index\":", d)) g_resimLevel = (int)d;

	size_t fs = j.find("\"finish_state\"");                 // the submitter's finish claim
	if (fs != std::string::npos) {
		size_t c = j.find("\"completed\":", fs);
		if (c != std::string::npos) { size_t cm = j.find_first_of(",}", c); g_resimClaimDone = (j.find("true", c) < cm); }
		if (rs_num_after(j, fs, "\"finish_tick\":", d)) g_resimClaimTick = (long)d;
	}

	g_resimBuild.clear();                                   // build placements: scan each {item_id, x, y}
	size_t bs = j.find("\"build\""); size_t cur = (bs == std::string::npos) ? 0 : bs;
	for (;;) {
		size_t it = j.find("\"item_id\":\"", cur); if (it == std::string::npos) break;
		it += 10; size_t e = j.find('"', it); if (e == std::string::npos) break;
		double x = 0, y = 0;
		if (rs_num_after(j, e, "\"x\":", x) && rs_num_after(j, e, "\"y\":", y))
			g_resimBuild.push_back({ j.substr(it, e - it), (float)x, (float)y });
		cur = e + 1;
	}
	Eets::Log("hop_on_eets resim: parsed %zu items, seed=%u, level=%d, claim=%s@%ld",
	          g_resimBuild.size(), g_seed, g_resimLevel, g_resimClaimDone ? "done" : "dnf", g_resimClaimTick);
	return !g_resimBuild.empty() || g_resimLevel >= 0;
}

static void resim_write_verdict(bool completed, long tick) {
	bool reproduced = (completed == g_resimClaimDone) && (!completed || labs(tick - g_resimClaimTick) <= RESIM_TICK_TOL);
	g_resimReproduced = reproduced;
	FILE* f = fopen("Log/hop_on_eets_verdict.json", "w");
	if (f) {
		fprintf(f, "{\n  \"verdict_version\": 1,\n  \"platform\": \"%s\",\n  \"seed\": %u,\n  \"level_index\": %d,\n"
		           "  \"resim\": {\"completed\": %s, \"finish_tick\": %ld, \"items_applied\": %zu},\n"
		           "  \"claim\": {\"completed\": %s, \"finish_tick\": %ld},\n"
		           "  \"reproduced\": %s\n}\n",
		        PLATFORM, g_seed, g_levelIndex,
		        completed ? "true" : "false", tick, g_resimBuild.size(),
		        g_resimClaimDone ? "true" : "false", g_resimClaimTick,
		        reproduced ? "true" : "false");
		fclose(f);
	}
	Eets::Log("hop_on_eets resim: VERDICT reproduced=%s (resim %s@%ld vs claim %s@%ld) -> Log/hop_on_eets_verdict.json",
	          reproduced ? "yes" : "no", completed ? "done" : "dnf", tick, g_resimClaimDone ? "done" : "dnf", g_resimClaimTick);
}

// write the verdict, end the re-sim, and (batch mode) exit the process with a code the launcher reads:
//   0 = re-sim reproduced the claim | 1 = did not reproduce (claim rejected / DNF mismatch)
static void resim_finish(bool completed, long tick) {
	resim_write_verdict(completed, tick);
	g_resimState = RS_DONE;
	if (g_resimExit) { fflush(nullptr); std::exit(g_resimReproduced ? 0 : 1); }
}

// kick off + watchdog, called each frame while a re-sim is in flight
static void resim_tick() {
	if (g_resimState == RS_INIT) {                          // wait for the catalog, then load the target level
		int idx = (g_resimLevel >= 0) ? g_resimLevel : g_levelIndex;
		g_levelIndex = idx;
		if (pool_size() > 0) { load_match_level(); g_resimState = RS_LOADING; Eets::Log("hop_on_eets resim: loading level idx=%d", idx); }
	} else if (g_resimState == RS_RUNNING && g_tick > g_resimMaxTicks) {
		Eets::Log("hop_on_eets resim: DNF (timeout @%ld)", g_tick);
		resim_finish(false, g_tick);
	}
}

// level just loaded (RS_LOADING): apply the recorded build, pin determinism, force-start the sim
static void resim_on_level_loaded() {
	for (auto& b : g_resimBuild) { Vector2 p; p.x = b.x; p.y = b.y; World_CreateObject(b.name.c_str(), p); }
	g_pinSeed = true; engage_determinism();
	force_start_sim();
	g_resimState = RS_RUNNING;
	Eets::Log("hop_on_eets resim: applied %zu items, started", g_resimBuild.size());
}

static void resim_on_complete() {                          // level_complete during a re-sim
	resim_finish(true, g_tick);
}
