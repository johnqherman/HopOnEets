// recorder.h - replay/ghost/result I/O and the build/sim lifecycle (M1/M4).
#pragma once
#include "state.h"
#include "net.h"          // net_sendline: broadcast our build + finish
#include "determinism.h"  // engage_determinism on sim start

// ---- ghost I/O (.txt sidecar: trivial to parse, derived from the replay) ----
static void set_ghost_label() {
	if (!g_haveGhost) { g_ghostLabel = "none"; return; }
	char b[64]; snprintf(b, sizeof(b), "%.2fs%s", g_ghostFinish >= 0 ? g_ghostFinish / (double)TICK_RATE : -1.0,
	                     g_ghostCompleted ? "" : " (dnf)");
	g_ghostLabel = b;
}
static bool load_ghost(const std::string& path) {
	FILE* f = fopen(path.c_str(), "r");
	if (!f) { Eets::Log("hop_on_eets: no ghost at %s", path.c_str()); return false; }
	g_ghost.clear(); g_ghostFinish = -1; g_ghostCompleted = false; g_ghostItems = 0;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#') continue;
		long t; int c, n; float x, y;
		if (sscanf(line, "finish %ld completed %d items %d", &t, &c, &n) == 3) { g_ghostFinish = t; g_ghostCompleted = c != 0; g_ghostItems = n; continue; }
		if (sscanf(line, "%ld %f %f", &t, &x, &y) == 3) g_ghost.push_back({ t, x, y, 0 });
	}
	fclose(f);
	g_haveGhost = !g_ghost.empty() || g_ghostFinish >= 0;
	set_ghost_label();
	if (g_haveGhost) Eets::Log("hop_on_eets: loaded ghost %s (%zu samples, finish=%ld)", path.c_str(), g_ghost.size(), g_ghostFinish);
	return g_haveGhost;
}
static void clear_ghost() { g_ghost.clear(); g_ghostFinish = -1; g_haveGhost = false; set_ghost_label(); }
static void write_ghost(int idx, bool completed) {
	char path[128]; snprintf(path, sizeof(path), "Log/hop_on_eets_ghost_%03d.txt", idx);
	FILE* f = fopen(path, "w"); if (!f) return;
	fprintf(f, "# hop_on_eets ghost v1\nfinish %ld completed %d items %d\n", g_finishTick, completed ? 1 : 0, placed_count());
	for (auto& s : g_samples) fprintf(f, "%ld %.4f %.4f\n", s.tick, s.x, s.y);
	fclose(f);
	g_lastGhostPath = path;
	Eets::Log("hop_on_eets: wrote ghost %s", path);
}
// ghost world position at a given tick (linear interp). false if no data.
static bool ghost_pos_at(long tick, float& ox, float& oy) {
	if (g_ghost.empty()) return false;
	if (tick <= g_ghost.front().tick) { ox = g_ghost.front().x; oy = g_ghost.front().y; return true; }
	if (tick >= g_ghost.back().tick)  { ox = g_ghost.back().x;  oy = g_ghost.back().y;  return true; }
	for (size_t i = 1; i < g_ghost.size(); i++) {
		if (g_ghost[i].tick >= tick) {
			Sample& a = g_ghost[i - 1]; Sample& b = g_ghost[i];
			float f = (b.tick == a.tick) ? 0.f : (float)(tick - a.tick) / (float)(b.tick - a.tick);
			ox = a.x + (b.x - a.x) * f; oy = a.y + (b.y - a.y) * f; return true;
		}
	}
	ox = g_ghost.back().x; oy = g_ghost.back().y; return true;
}

// ---- replay + result JSON ----
static void append_float(std::string& s, float f) { char b[32]; snprintf(b, sizeof(b), "%.6g", f); s += b; }
static void write_replay(bool completed) {
	if (!g_autoRecord) return;
	std::string j = "{\n  \"replay_version\": 1,\n  \"mode\": \"solution_race\",\n";
	char hdr[160]; snprintf(hdr, sizeof(hdr), "  \"seed\": %u,\n  \"tick_rate\": %d,\n  \"resets\": %d,\n", g_seed, TICK_RATE, g_resets);
	j += hdr; j += "  \"build\": [\n";
	bool first = true;
	for (auto& p : g_placements) {
		if (p.removed) continue;
		if (!first) j += ",\n";
		first = false;
		char rb[48]; snprintf(rb, sizeof(rb), "    {\"op\":\"place\",\"ref\":%llu,\"item_id\":\"", p.id);
		j += rb; j += p.blueprint; j += "\",\"x\":"; append_float(j, p.x); j += ",\"y\":"; append_float(j, p.y); j += "}";
	}
	j += "\n  ],\n";
	char fin[200]; snprintf(fin, sizeof(fin), "  \"finish_state\": {\"completed\": %s, \"finish_tick\": %ld, \"items_used\": %d, \"resets\": %d},\n",
	                        completed ? "true" : "false", g_finishTick, placed_count(), g_resets);
	j += fin; j += "  \"checkpoints\": [";
	for (size_t i = 0; i < g_samples.size(); i++) {
		if (i) j += ",";
		char cb[80]; snprintf(cb, sizeof(cb), "{\"tick\":%ld,\"h\":\"%016llx\"}", g_samples[i].tick, (unsigned long long)g_samples[i].hash);
		j += cb;
	}
	j += "]\n}\n";
	char path[128]; snprintf(path, sizeof(path), "Log/hop_on_eets_replay_%03d.json", g_replayCounter);
	FILE* f = fopen(path, "w");
	if (f) { fwrite(j.data(), 1, j.size(), f); fclose(f); Eets::Log("hop_on_eets: wrote replay %s (%d placements)", path, placed_count()); }
}
static void write_result(bool completed) {
	const char* winner = "none"; const char* reason = "no_ghost";
	if (g_haveGhost) {
		if (completed != g_ghostCompleted)                   { winner = completed ? "you" : "ghost"; reason = "completion"; }
		else if (completed && g_finishTick != g_ghostFinish) { winner = (g_finishTick < g_ghostFinish) ? "you" : "ghost"; reason = "finish_tick"; }
		else if (placed_count() != g_ghostItems)             { winner = (placed_count() < g_ghostItems) ? "you" : "ghost"; reason = "items_used"; }
		else                                                 { winner = "tie"; reason = "tie"; }
		if      (strcmp(winner, "you") == 0)   g_youWins++;
		else if (strcmp(winner, "ghost") == 0) g_ghostWins++;
		snprintf(g_roundMsg, sizeof(g_roundMsg), "ROUND: %s by %s  (you %.2fs vs ghost %.2fs)  match %d-%d",
		         winner, reason, g_finishTick / (double)TICK_RATE,
		         g_ghostFinish >= 0 ? g_ghostFinish / (double)TICK_RATE : -1.0, g_youWins, g_ghostWins);
	} else {
		snprintf(g_roundMsg, sizeof(g_roundMsg), "ROUND recorded (practice): %.2fs", g_finishTick / (double)TICK_RATE);
	}
	Eets::Log("hop_on_eets: %s", g_roundMsg);
	std::string j = "{\n  \"result_version\": 1,\n  \"mode\": \"solution_race\",\n";
	char b[400];
	snprintf(b, sizeof(b),
	         "  \"match_id\": \"hoe_local_%03d\",\n  \"round\": %d,\n"
	         "  \"outcome\": {\"winner\": \"%s\", \"reason\": \"%s\"},\n"
	         "  \"you\":   {\"completed\": %s, \"finish_tick\": %ld, \"items_used\": %d},\n"
	         "  \"ghost\": {\"present\": %s, \"completed\": %s, \"finish_tick\": %ld, \"items_used\": %d},\n"
	         "  \"match_score\": {\"you\": %d, \"ghost\": %d}\n}\n",
	         g_roundCounter, g_roundCounter, winner, reason,
	         completed ? "true" : "false", g_finishTick, placed_count(),
	         g_haveGhost ? "true" : "false", g_ghostCompleted ? "true" : "false", g_ghostFinish, g_ghostItems,
	         g_youWins, g_ghostWins);
	j += b;
	char path[128]; snprintf(path, sizeof(path), "Log/hop_on_eets_result_%03d.json", g_roundCounter);
	FILE* f = fopen(path, "w");
	if (f) { fwrite(j.data(), 1, j.size(), f); fclose(f); }
}

// ---- build/sim lifecycle ----
static void begin_build() {
	g_phase = BUILD; g_tick = 0; g_finishTick = -1; g_resets = 0;
	g_placements.clear(); g_samples.clear(); g_prevWasResetRerun = false;
	g_buildStart = Time(); g_forcedStart = false;
	g_oppBuild.clear(); g_oppBuildReady = false;
	if (!g_ghostFile.empty() && !g_haveGhost) load_ghost(g_ghostFile);
	snprintf(g_status, sizeof(g_status), "build");
}
static void begin_sim(bool fromReset) {
	g_phase = SIM; g_tick = 0; g_finishTick = -1;
	std::vector<uint64_t> seq; seq.reserve(g_samples.size());
	for (auto& s : g_samples) seq.push_back(s.hash);
	g_prevHashSeq = seq; g_samples.clear(); g_prevWasResetRerun = fromReset;
	ForEachObject([&](Object* o) {
		if (!o) return;
		unsigned long long id = Object_GetID(o);
		for (auto& p : g_placements) if (p.id == id) { Vector2 q = Object_GetPosition(o); p.x = q.x; p.y = q.y; }
	});
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
