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
	char hdr[200]; snprintf(hdr, sizeof(hdr), "  \"seed\": %u,\n  \"tick_rate\": %d,\n  \"level_index\": %d,\n  \"resets\": %d,\n", g_seed, TICK_RATE, g_levelIndex, g_resets);
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
		if (!g_matched) {   // online: the relay is authoritative for the series score
			if      (strcmp(winner, "you") == 0)   g_youWins++;
			else if (strcmp(winner, "ghost") == 0) g_ghostWins++;
		}
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

// ---- authoritative submission: upload our input log (base64) so the server can re-sim it ----
static std::string b64_encode(const std::string& in) {
	static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string o; int val = 0, bits = -6;
	for (unsigned char c : in) { val = (val << 8) + c; bits += 8; while (bits >= 0) { o.push_back(T[(val >> bits) & 0x3F]); bits -= 6; } }
	if (bits > -6) o.push_back(T[((val << 8) >> (bits + 8)) & 0x3F]);
	while (o.size() % 4) o.push_back('=');
	return o;
}
// compact single-line input log (the re-sim verifier parses exactly these fields - see src/resim.h)
static std::string compact_log(bool completed) {
	std::string j = "{\"seed\":" + std::to_string(g_seed) + ",\"tick_rate\":" + std::to_string(TICK_RATE) +
	                ",\"level_index\":" + std::to_string(g_levelIndex) + ",\"build\":[";
	bool first = true;
	for (auto& p : g_placements) {
		if (p.removed) continue;
		if (!first) j += ",";
		first = false;
		j += "{\"item_id\":\""; j += p.blueprint; j += "\",\"x\":"; append_float(j, p.x); j += ",\"y\":"; append_float(j, p.y); j += "}";
	}
	j += "],\"finish_state\":{\"completed\":"; j += completed ? "true" : "false";
	j += ",\"finish_tick\":" + std::to_string(g_finishTick) + ",\"items_used\":" + std::to_string(placed_count()) + "}}";
	return j;
}
static void net_submit_replay(bool completed) {
	int round = g_youWins + g_ghostWins;
	net_sendline("replay " + std::to_string(round) + " " + PLATFORM + " " + b64_encode(compact_log(completed)));
}

// ---- build/sim lifecycle ----
static void begin_build() {
	g_phase = BUILD; g_tick = 0; g_finishTick = -1; g_resets = 0; g_interRound = false;
	g_deaths = 0; g_deathTicks = 0; g_deathReset = false; g_retryActive = false; g_roundStart = 0.0;   // fresh round: clear death tally/banked time/retry+round clocks
	g_placements.clear(); g_samples.clear(); g_prevWasResetRerun = false;
	g_buildStart = Time(); g_forcedStart = false;
	g_oppBuild.clear(); g_oppBuildReady = false;
	if (!g_ghostFile.empty() && !g_haveGhost) load_ghost(g_ghostFile);
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
