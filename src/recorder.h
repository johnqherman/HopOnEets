// recorder.h - the build/sim lifecycle: record the player's build placements (for the live build-share to the
// opponent) and run the determinism self-test. No replay/ghost/result files - the ghost is the LIVE opponent
// only (see ghostview.h) and there is no local recording or playback.
#pragma once
#include "state.h"
#include "net.h"          // net_sendline: broadcast our build + finish
#include "determinism.h"  // engage_determinism on sim start

// ---- build/sim lifecycle ----
static void begin_build() {
	g_phase = BUILD; g_tick = 0; g_finishTick = -1; g_resets = 0; g_interRound = false;
	g_deaths = 0; g_deathTicks = 0; g_deathReset = false; g_retryActive = false; g_roundStart = 0.0;   // fresh round: clear death tally/banked time/retry+round clocks
	g_placements.clear(); g_samples.clear(); g_prevWasResetRerun = false;
	g_buildStart = Time(); g_forcedStart = false;
	g_oppBuild.clear(); g_oppBuildReady = false;
	snprintf(g_status, sizeof(g_status), "build");
}
static void begin_sim(bool fromReset) {
	g_phase = SIM; g_tick = 0; g_finishTick = -1; g_retryActive = false;   // sim started: retry clock no longer applies
	g_engineTickBase = Engine_GetSimTick();   // baseline the true engine sim-tick; subsequent g_tick = counter - this
	g_lastHashBucket = -1;                     // restart hash-sample bucketing for this run
	if (g_matched && g_roundStart == 0.0) g_roundStart = Time();   // round/cap clock starts at the FIRST Go (after build), per round
	std::vector<uint64_t> seq; seq.reserve(g_samples.size());
	for (auto& s : g_samples) seq.push_back(s.hash);
	g_prevHashSeq = seq; g_samples.clear(); g_prevWasResetRerun = fromReset;
	g_oppHashes.clear(); g_desync = false; g_desyncSent = false; g_desyncTick = -1;   // fresh round: reset desync detection
	ForEachObject([&](Object* o) {
		if (!o) return;
		unsigned long long id = Object_GetID(o);
		for (auto& p : g_placements) if (p.id == id) { Vector2 q = Object_GetPosition(o); p.x = q.x; p.y = q.y; p.matched = true; }
	});
	// drop placements with no live object: a build reset gives the re-placed items NEW ids, so entries from
	// prior reset attempts never match here and would otherwise ship as junk (0,0) items. Keep only the final
	// build = what's actually live at sim start.
	g_placements.erase(std::remove_if(g_placements.begin(), g_placements.end(),
	                                   [](const Placement& p) { return !p.matched; }),
	                   g_placements.end());
	if (g_matched) {   // share our locked-in build so the opponent can see it as ghost items
		for (auto& p : g_placements) if (!p.removed && valid_pos(p.x, p.y)) { char bb[96]; snprintf(bb, sizeof(bb), "build %s %.1f %.1f", p.blueprint.c_str(), p.x, p.y); net_sendline(bb); }
		net_sendline("buildend");
	}
	if (g_matchActive) engage_determinism();
	snprintf(g_status, sizeof(g_status), "sim");
}
static void report_determinism() {
	if (!g_prevWasResetRerun || g_prevHashSeq.empty() || g_samples.empty()) return;
	size_t n = g_prevHashSeq.size() < g_samples.size() ? g_prevHashSeq.size() : g_samples.size();
	for (size_t i = 0; i < n; i++)
		if (g_prevHashSeq[i] != g_samples[i].hash) { Eets::Log("hop_on_eets: determinism DIVERGE at sample %zu", i); return; }
	if (g_prevHashSeq.size() != g_samples.size()) Eets::Log("hop_on_eets: determinism length mismatch (%zu vs %zu)", g_prevHashSeq.size(), g_samples.size());
	else Eets::Log("hop_on_eets: determinism MATCH (%zu samples)", n);
}
