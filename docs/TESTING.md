# Hop On Eets — Test Plan

Everything below the netproto e2e has **never run against the real game**. The mod compiles
(Linux `.so` + Win `.dll`, `-Wall`) and the netcode passes its e2e, but every engine call
(level load, force-start, ghost draw, seed pin, struct offsets) is RE-derived and **unverified
in-game**. This is the plan to verify it, in dependency order — each phase gates the next.

Legend: ✅ already automated · 🎮 needs the game running (not done yet) · 🔍 diagnostic if it fails.

---

## 0. Prereqs / build

```sh
# mod (from repo root)
make            # -> build/hop_on_eets.so   (Linux)
make win        # -> build/hop_on_eets.dll  (Windows, needs i686-w64-mingw32-g++)
make pack       # -> hop_on_eets.eetsmod    (bundle: .so + .dll + source + cfg + assets)

# netcode (from netproto/)
npm test        # e2e: relay + bridge + two fake mods
npx tsc --noEmit
```

Install: drop `hop_on_eets.eetsmod` (or the loose `.so`/`.dll` + `.cfg`) into the framework's
`mods/` dir. Confirm framework **0.18.0+** loads it (loader log lists `hop_on_eets`).

A second machine OR a second game install is needed for the online phases (singleton world —
two boards = two processes). Same-box: two installs on different `player_id`/`bridge_port`.

---

## Phase A — Smoke (mod loads, doesn't crash) 🎮

**Goal:** plugin loads, entry points fire, no crash on a normal level.

1. Launch Eets with the mod installed. Check loader log: `hop_on_eets` loaded, `sim=1` warning
   present (expected — we control the sim).
2. Open any level, play normally (build + Go + finish) with all online/match features OFF
   (`online=0`, default cfg).
3. Confirm: game runs at normal speed, finishes, no hang/crash. `Log/hop_on_eets_replay_*.json`
   written on finish (auto_record=1).

**Pass:** clean run + a replay file with a plausible `finish_tick` and the placed items.
🔍 Crash on load → ABI/addr mismatch; check build-id guard. Garbage `finish_tick`/positions →
the GetPosition pointer-deref fix didn't take (see roadmap "Done" item).

---

## Phase B — Event capture correctness 🎮

**Goal:** recorder captures the right data.

1. Build a known solution (count your placements), Go, finish.
2. Inspect the replay JSON: every placed item present with sane `x,y` (screen coords, not `~1e11`);
   `finish_tick` matches the visible solution time; `items_used` == your count.

**Pass:** replay round-trips your build. 🔍 extra/phantom items → `object_spawn` includes engine
spawns, need the build-phase + player-placement filter (roadmap §2).

---

## Phase C — Determinism harness 🎮 (blocks ranked)

**Goal:** same level + seed + FPS → identical run.

1. F6 menu → enable seed pin / det-mode (or `pin_seed=1`). Set a fixed seed.
2. Run the same solution twice (reset + rerun). Compare the state-hash sample sequences
   (`hash_interval` frames).
3. Repeat with a *different* seed → sequences should differ.

**Pass:** same seed → MATCH every sample; different seed → DIVERGE. 🔍 Linux divergence even with
pin → engine reseeds to `0x57670fd` on StartSimulation; confirm the post-start pin
(`pin_seed_globals`) actually writes `PRNG_seed`/`DetMode_flag` (Linux addrs in `eets_addr.h`).
Confirm `TICK_RATE` 60 (read sim timestep). **This phase is the foundation for ranked — do not
skip.**

---

## Phase D — Ghost rendering 🎮

**Goal:** opponent ghost draws at the right place, animated + translucent.

1. Set `ghost_file` to a replay from Phase B. Load that level.
2. Watch the ghost Eets replay the recorded run.

**Pass:** ghost follows the recorded path, semi-transparent, animated, at the correct screen
position (world==screen assumption). 🔍 ghost at wrong spot → world≠screen on this level (scroll/
zoom; need the GFX view matrix). Blank/marker only → `ghost_anim` path wrong; cycle candidates in
F6 to match `Data/Animations/`.

---

## Phase E — Level load + pool 🎮 (Linux first)

**Goal:** the faked-MainMenu `LoadSimulatorLevel` path loads a pool level without crashing.

1. F6 → "Load match level" (manual). Confirm a non-tutorial level loads into the build phase
   (objects + win condition present, sim NOT auto-started).
2. Walk the pool: confirm World 1 + the first `hub_intro_skip` levels of each hub are excluded.

**Pass:** chosen pool level loads cleanly, build phase ready. 🔍 crash → MainMenu fake buffer /
`this+0x14b0` std::string layout wrong, or `LevelManager` offsets off; the crash-guard should
catch but the load fails. Wrong level → `pool_resolve(index % poolSize)` catalog mismatch.

---

## Phase F — Force-start + speed lock 🎮

**Goal:** build timer expiry starts the sim like pressing Go; vanilla speed/pause locked.

1. With `build_seconds=15`, load a level, build, wait. At 0 the sim must auto-start (real
   `Simulator::StartSimulation` on Linux: initial-state snapshot + reseed).
2. During sim, try the vanilla pause / fast-forward hotkeys → must be locked to normal.

**Pass:** auto-start at 0; speed stays normal. 🔍 no start on Linux → `Simulator_StartSimulation`
addr/ABI; on Win the fallback flips `[sim+0xb8]` — needs the Win StartSimulation addr (Linux sim
flag is `[sim+0x160]`, offsets differ per build).

---

## Phase G — Online loopback (same box, 2 installs) 🎮 (the big one)

**Goal:** the full online loop end-to-end through the real game, not fake mods.

Setup:
```sh
cd netproto && npm run relay          # terminal 1
RELAY_PORT=38500 MOD_PORT=38600 PLAYER=p1 npm run bridge   # terminal 2 (install A)
RELAY_PORT=38500 MOD_PORT=38601 PLAYER=p2 npm run bridge   # terminal 3 (install B)
```
Install A cfg: `online=1 bridge_port=38600 player_id=p1`. Install B: `bridge_port=38601 player_id=p2`.

1. **Host/join:** A → F6 → Host (get code). B → Join with code. Both see the match + the **same
   level** (auto_load) + **same seed**.
2. **Ready-gate + countdown:** both auto-load → `ready` → synced `countdown 15`. Build phases
   align.
3. **Build relay:** A's locked-in placements appear as B's ghost items after build ends (and vice
   versa).
4. **Realtime ghost:** during sim, each sees the other's live Eets ghost moving.
5. **Result + Bo3:** faster finisher wins the round; series tracked; `series_over` at 2 wins.
6. **Per-match seed:** with the seed pinned, same-platform A and B get **identical** physics
   (Phase C must pass first). Cross-platform (one Win, one Linux) WILL differ — expected; that's
   what authoritative re-sim is for (not built yet).

**Pass:** a full Bo3 plays out, both clients agree on the winner each round.
🔍 ghost lag/jitter → tune `pos` tick rate. Desync same-platform → Phase C seed pin not holding.

---

## Phase H — Disconnect = loss ✅ (e2e) + 🎮 confirm in-game

✅ Automated: e2e closes a matched client → the other gets `oppleft` + `series you 0 0`.
🎮 Confirm: during a live match, kill install B (or its bridge). Install A should see "opponent
left" and be awarded the series. (No reconnect window yet — any drop = immediate loss.)

---

## Phase I — Ranked queue 🎮

Both installs → F6 → Ranked queue → get matched on a non-tutorial pool level, Bo3, same as
Phase G but via `queue` instead of host/join.

---

## Phase J — Headless re-sim verifier 🎮 (v0.3)

**Goal:** the authoritative verifier reproduces a known finish from its input log, windowless.

1. Record a clean run (Phase B) → `Log/hop_on_eets_replay_000.json`.
2. `EETS_DIR=<game> tools/resim-runner.sh Log/hop_on_eets_replay_000.json` (uses xvfb + dummy audio).
3. It launches the game windowless, the mod re-sims the log, writes `Log/hop_on_eets_verdict.json`,
   and exits.

**Pass:** verdict `"reproduced": true`, `resim.finish_tick` within 2 of the recorded tick; runner
exit code 0. 🔍 no verdict → catalog not ready when `resim_tick` loaded (raise the wait), or the level
didn't load (Phase E). `reproduced:false` with a close tick → raise `RESIM_TICK_TOL`; far off →
determinism not pinned (Phase C). GL error under xvfb → Option A (null backend) needed; until then
ensure xvfb + a GL stack on the box.
✅ The verify→decide logic (`verifyMatch`) is already unit-tested with a mock (`npm run test:verify`).

## What is NOT covered yet (out of scope until built)

- **Cross-platform authoritative re-sim** (v0.3, top risk) — headless canonical build; the only
  valid source of truth for Win-vs-Linux ranked. Until it exists, cross-platform ranked is
  provisional.
- **Windows online** — bridge connection from the Win build (winsock). Win can play
  local/ghost; online is Linux-first.
- **Reconnect window**, `wss://`+auth, accounts/Elo/leaderboard, level-hash validation,
  inventory enforcement, puzzle-pieces tiebreaker.

---

## Suggested order

A → B → C → D → E → F → G → I, with H confirmable any time after G. **C gates everything ranked**
(no determinism, no fair match). E/F gate G (can't race a level you can't load/start). Stop and
fix at the first ✅→🎮 gap before moving on — later phases assume the earlier ones hold.
