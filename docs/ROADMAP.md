# Hop On Eets — Roadmap / TODO

What's done, what's left, in priority order. Pairs with the spec
([hop-on-eets-spec.md](hop-on-eets-spec.md) Parts 15/17) and the protocol
([net-protocol.md](net-protocol.md)). `[x]` done, `[~]` partial, `[ ]` not started.

## Done (v0.1 + v0.2 prototype)

- [x] M1 recorder (build set + finish → replay JSON)
- [x] M2 determinism harness (pin seed/FPS/det-mode, speed lock, reset-rerun MATCH/DIVERGE)
- [x] M3 ghost race (record/load timeline; world-space ghost Eets)
- [x] M4 local mirror scoring + ranked-ready result JSON
- [x] Custom F6 menu = single config surface (persisted via `SaveSet`)
- [x] Realtime online: mod→TCP→bridge→WebSocket→relay; live opponent pos + build
- [x] Private host/join by code + ranked matchmaking queue
- [x] 15s build timer → force-start the sim
- [x] netproto in TypeScript (zero runtime deps; `npm test` e2e green)
- [x] Mod split into `src/*.h` modules (single TU)
- [x] Animated semi-transparent Eets ghost (default-on; cycle anim path in menu; marker fallback)
- [x] Garbage-position **root-cause fix** (Ghidra-confirmed) — `Object::GetPosition` returns a `Vector2&`
      (a POINTER in (E/R)AX via Model@Object+0x38), not a Vector2 by value. The Linux wrapper read it
      by-value, reinterpreting the pointer's 64 bits as two floats → `x≈1e11, y≈0`. Fix: deref the
      pointer on BOTH platforms (Win already did) + guard the Model* at Object+0x38. Same for `GetFacing`
      (returns `&Object+0x14`). Mod-side `valid_pos` kept as belt-and-suspenders.

## 0. Verify first (blocking — needs the game running; can't be done headless here)

> Full step-by-step in-game test plan: **[TESTING.md](TESTING.md)** (phases A–I, dependency-ordered).


- [ ] In-game smoke test: events fire (`object_spawn` in build, `level_complete`); `force_start_sim`
      (now calls the real `Simulator::StartSimulation` on Linux — does the initial-state snapshot +
      RNG reseed) starts + resets cleanly. (Win still uses a flag-flip at `[sim+0xb8]` — needs the Win
      `StartSimulation` address; the Linux sim flag is at `[sim+0x160]`, so offsets differ per build.)
- [ ] Confirm native `TICK_RATE` (assumed 60)
- [ ] Confirm ghost draws at the right screen spot (world==screen assumption) on real levels
- [ ] Confirm menu clicks land (UI doesn't consume input — may pass through to the game)
- [ ] Which platform does the user actually run? (online is Linux-first today)

## 1. Make online matches real (v0.2 completion)

- [~] **Level sync** — SELECTION + LOAD wired (Linux). Relay picks a per-match level index; both clients
      resolve `index % poolSize` against the runtime `LevelManager` pool (same catalog → same level). The
      **"Load match level"** menu button calls `MainMenu::LoadSimulatorLevel(FileNamePair*)@0x629ff0` with a
      *faked* MainMenu (it only uses `this` as scratch for the level's "Music" string at `this+0x14b0`, so a
      buffer with a valid empty `std::string` there suffices); the pool entry's `FileNamePair*` is the arg.
      It loads objects + win condition into the build phase (does NOT start the sim). **Auto-load on match**
      is wired (client auto-loads + sends `ready`; `auto_load_level` cfg, default on) with the manual menu
      button as fallback. REMAINING: verify in-game (offsets/ABI; crash-guard protects); Windows
      (`LoadSimulatorLevel`/`GetLevelManager` addrs); `level_hash` verification.
- [x] Server-synced countdown + ready-gate — clients auto-load the matched level then send `ready`; the
      relay sends `countdown {seconds}` only once BOTH are ready (and again after each round). Both start
      the build timer on receipt (within latency) and force-start together. e2e checks it stays gated.
- [x] Disconnect = loss — relay awards the series to whoever remains (sends `opponent_left` +
      `series_over winner=you`); the bridge propagates a mod quit (TCP close) to the relay by closing its
      WS. Fixed a latent bug: upgraded WS sockets allowHalfOpen, so a peer FIN fired `'end'` not `'close'` —
      ws.ts now treats `'end'` as close (guarded). e2e covers it. REMAINING: reconnect window (today any
      disconnect = immediate loss, no grace/rejoin).
- [ ] `wss://` + auth on the bridge/relay (not the plugin)
- [x] Windows online (winsock) — `net.h` socket layer split into shared `net_handle` + per-platform
      sockets; Windows uses winsock2 (`WSAStartup`/`ioctlsocket` non-blocking), links `-lws2_32`. `state.h`
      pulls winsock2 before windows.h. Win can host/join/queue/race/score/desync over the bridge. CAVEAT:
      Win auto-load is a no-op (`GetLevelManager`/`LoadSimulatorLevel` Win addrs not RE'd) — load the level
      manually for now.
- [ ] Lobby / ready / rematch flow; show opponent name in-menu
- [x] Checkpoint state-hash exchange (same-platform desync detection) — the mod streams its periodic
      `state_hash()` (every `hash_interval` ticks) with its `platform`; the opponent compares **only
      same-platform** hashes at the same tick (cross-platform FP physics differs by design). A mismatch ⇒
      the client sends `desync`; the relay marks the match `no_contest` for both and stops scoring; the mod
      writes `Log/hop_on_eets_desync_*.json` + shows a HUD banner. e2e covers hash relay + no-contest.

## 2. Gameplay fidelity (RE tasks)

- [ ] `get_inventory` — read the level's allowed items (enforce same inventory; correct "items used")
- [ ] Puzzle-pieces tiebreaker — read the score/pieces state (today scoring uses items_used, not pieces)
- [ ] `on_activate_item` — capture sparse in-sim activations (currently only build-phase placements)
- [ ] Tighten placement capture (filter `object_spawn` to real player placements vs engine spawns) + removal path
- [ ] Level hashing in the mod (sha256 the `LEVELS:Game/<name>` file) for MatchConfig/validation
- [ ] Read richer finish state from `level_complete` params (`LevelManager*`, `Player*`)

## 3. Determinism hardening

- [ ] Read the true sim tick (`_DAT_00ee3da4`, set in step `FUN_00536440`) instead of the frame counter
      (Linux global address still unknown — only the Win binary was RE'd; or use `[sim+0xbc]`)
- [~] Seeding: the engine already reseeds the deterministic RNG on every `Simulator::StartSimulation`
      (`Util::SetSeedInternal(..., 0x57670fd)`), so runs are reproducible on Linux **without** the manual
      pin (which used Win-only global offsets anyway). To use a per-match seed, set it right after
      StartSimulation or hook `SetSeedInternal`. Win det-mode/seed globals still as-is.
- [x] Per-match seed control — relay sends a random per-match seed (in `match`); the mod pins it
      (`DetMode_flag` + `PRNG_seed`, both platforms) right after `StartSimulation`, overriding the engine's
      fixed `0x57670fd`. Same-platform clients get identical RNG (cross-platform differs — different
      generators — so cross-platform ranked still relies on authoritative re-sim).

## 4. Ranked (v0.3)

- [ ] Accounts / persistent player IDs
- [x] Best-of-3 series orchestration — relay tracks per-match round wins, sends `result` (with the series
      score) each round and `series_over` at 2 wins; the relay is authoritative for the score (the mod no
      longer double-counts when online). e2e plays a full 2-0 series.
- [ ] Rating (Elo/Glicko) + leaderboard
- [~] Ranked map-pool — DONE: built at runtime from `LevelManager` (`src/levels.h`); **non-tutorial** =
      skip World 1 (hub 0) entirely + skip the first `hub_intro_skip` levels of every other hub (intro
      levels; count is a menu-tunable setting, default 1). Relay picks a per-match index; both clients
      resolve `index % poolSize` to the same level (e2e-verified). REMAINING: actually LOAD the chosen
      level (`MainMenu::LoadSimulatorLevel` + `MainMenu::i()`); read the per-hub `tutorial_minimum` for
      an exact intro count instead of the fixed skip; verify the `LevelManager` offsets in-game.
- [~] **Authoritative headless re-sim** (top risk; spec Part 6) — see [headless-resim.md](headless-resim.md).
      DONE: mod-side batch driver (`src/resim.h`, `resim_file` cfg) — parse input log → load level →
      apply build via `World_CreateObject` → `force_start_sim` → read outcome → write
      `Log/hop_on_eets_verdict.json` (`reproduced` vs the submitter's claim); replays now self-describing
      (`level_index`); exit-on-done with a verdict exit code + `HOE_RESIM_*` env overrides. Headless
      launcher `tools/resim-runner.sh` (xvfb + dummy audio, watch verdict, exit code). `netproto/verifier.ts`
      = `verifyMatch` + `ResimRunner` (Mock + Shell); verify→decide unit-tested (`verifier.test.ts`, in
      `npm test`). Null FNA3D backend `nullbackend/libnullbackend.so` (`--null`, all 29 imports stubbed,
      unvalidated). Relay→verifier handoff: clients `submit_replay` (base64 log), relay calls the injected
      `RankedVerifier` on a ranked round and emits `authoritative`; relay CLI enables it when `EETS_DIR` set.
      e2e-covered. REMAINING: Win canonical build (needs Win level addrs); in-game validation (incl. `--null`).
- [ ] Replay submission + storage on the server

## 5. Content & polish

- [ ] Curated ranked level pool (`pool_v0.json` is a placeholder — real ids/names/hashes)
- [x] **Animated Eets ghost — mirrors the opponent.** The ghost now reflects the opponent's exact
      Eets state: each pos frame also streams emotion (happy/angry/scared via `EmotionExtension_GetEmotionName`),
      motion (walk/jump/fall/squat from `WalkingExtension_GetState`) and facing (`Object_GetFlipped`); the
      receiver maps it to the real `DATA:Animations/Eets/eets_<emotion>_<motion>.anim` and draws it flipped
      (framework `DrawAnim`/`DrawSpriteAt` gained a `flip` arg = swapped U). So it looks like the opponent's
      own Eets is in the level. Recorded ghost still uses the menu-chosen sprite. (Real anim names found from
      the installed `Data/Animations/Eets/`; the old cycle list was guesses.)
- [ ] World→screen for scrolling/zoomed levels (apply the GFX view matrix `FUN_0048f2c0`)
- [ ] Sound cues (countdown, win/lose); result panel / rematch button

## 6. Engineering

- [ ] CI: build the mod (Linux+Win) + run `netproto` e2e
- [ ] Host-compilable unit tests for scoring/tiebreakers
- [ ] Commit + push (nothing committed yet)
