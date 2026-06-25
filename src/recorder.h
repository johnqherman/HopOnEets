// recorder.h - build-placement capture (for live build-share) + determinism
// self-test not currently working but kept for future reference; see match.h
#pragma once
#include "state.h"
#include "net.h"
#include "determinism.h"

static void begin_build() {
  g_phase = BUILD;
  g_tick = 0;
  g_finishTick = -1;
  g_resets = 0;
  g_interRound = false;
  g_deaths = 0;
  g_deathTicks = 0;
  g_deathReset = false;
  g_retryActive = false;
  g_roundStart = 0.0;
  g_placements.clear();
  g_samples.clear();
  g_prevWasResetRerun = false;
  g_buildStart = Time();
  g_forcedStart = false;
  g_oppBuild.clear();
  g_oppBuildReady = false;
  snprintf(g_status, sizeof(g_status), "build");
}
static void begin_sim(bool fromReset) {
  g_phase = SIM;
  g_tick = 0;
  g_finishTick = -1;
  g_retryActive = false;
  g_engineTickBase = Engine_GetSimTick(); // g_tick = counter - this baseline
  g_lastHashBucket = -1;
  if (g_matched && g_roundStart == 0.0)
    g_roundStart = Time(); // round clock starts at first Go
  std::vector<uint64_t> seq;
  seq.reserve(g_samples.size());
  for (auto &s : g_samples)
    seq.push_back(s.hash);
  g_prevHashSeq = seq;
  g_samples.clear();
  g_prevWasResetRerun = fromReset;
  g_oppHashes.clear();
  g_desync = false;
  g_desyncSent = false;
  g_desyncTick = -1;
  ForEachObject([&](Object *o) {
    if (!o)
      return;
    unsigned long long id = Object_GetID(o);
    for (auto &p : g_placements)
      if (p.id == id) {
        Vector2 q = Object_GetPosition(o);
        p.x = q.x;
        p.y = q.y;
        p.matched = true;
      }
  });
  // drop placements with no live object: a build reset re-ids re-placed items,
  // so stale entries would ship as junk (0,0)
  g_placements.erase(
      std::remove_if(g_placements.begin(), g_placements.end(),
                     [](const Placement &p) { return !p.matched; }),
      g_placements.end());
  if (g_matched) { // share locked-in build for opponent's ghost items
    for (auto &p : g_placements)
      if (!p.removed && valid_pos(p.x, p.y)) {
        char bb[96];
        snprintf(bb, sizeof(bb), "build %s %.1f %.1f", p.blueprint.c_str(), p.x,
                 p.y);
        net_sendline(bb);
      }
    net_sendline("buildend");
  }
  if (g_matchActive)
    engage_determinism();
  snprintf(g_status, sizeof(g_status), "sim");
}
static void report_determinism() {
  if (!g_prevWasResetRerun || g_prevHashSeq.empty() || g_samples.empty())
    return;
  size_t n = g_prevHashSeq.size() < g_samples.size() ? g_prevHashSeq.size()
                                                     : g_samples.size();
  for (size_t i = 0; i < n; i++)
    if (g_prevHashSeq[i] != g_samples[i].hash) {
      Eets::Log("hop_on_eets: determinism DIVERGE at sample %zu", i);
      return;
    }
  if (g_prevHashSeq.size() != g_samples.size())
    Eets::Log("hop_on_eets: determinism length mismatch (%zu vs %zu)",
              g_prevHashSeq.size(), g_samples.size());
  else
    Eets::Log("hop_on_eets: determinism MATCH (%zu samples)", n);
}
