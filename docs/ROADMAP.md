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

## 0. Verify first (blocking — needs the game running; can't be done headless here)

- [ ] In-game smoke test: events fire (`object_spawn` in build, `level_complete`), `force_start_sim`
      actually starts the sim **and doesn't break ResetSimulation** (the `[sim+0xb8]` snapshot concern)
- [ ] Confirm native `TICK_RATE` (assumed 60)
- [ ] Confirm ghost draws at the right screen spot (world==screen assumption) on real levels
- [ ] Confirm menu clicks land (UI doesn't consume input — may pass through to the game)
- [ ] Which platform does the user actually run? (online is Linux-first today)

## 1. Make online matches real (v0.2 completion)

- [ ] **Level sync — CRITICAL.** Nothing makes both players load the *same* level yet (relay sends a
      seed, not a level). Need: relay picks a level from the pool → clients **load that specific level**
      programmatically (RE the level-load/`Simulator::LoadLevel` entry) → verify `level_hash` matches.
- [ ] Server-synced countdown (both build phases start at the same real time; today each client times
      locally from `level_load`)
- [ ] Reconnect handling; disconnect = loss (spec Part 12)
- [ ] `wss://` + auth on the bridge/relay (not the plugin)
- [ ] Windows online (winsock build, or dynamic-load ws2_32)
- [ ] Lobby / ready / rematch flow; show opponent name in-menu
- [ ] Checkpoint state-hash exchange over the net (same-platform desync detection, `STATE_HASH_INTERVAL`)

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
- [ ] RE the Linux det-mode flag + PRNG seed globals (seed-pin is Windows-only today)
- [ ] Empirically confirm seed-pin makes a RNG level reproducible (determinism probe on such a level)

## 4. Ranked (v0.3)

- [ ] Accounts / persistent player IDs
- [ ] Best-of-3 series orchestration (today: single-round result + a running tally)
- [ ] Rating (Elo/Glicko) + leaderboard
- [ ] Ranked map-pool selection (depends on level sync, §1)
- [ ] **Authoritative headless re-sim** (top risk; spec Part 6) — FNA3D/SDL null backend or a driven
      client; the source of truth for cross-platform ranked
- [ ] Replay submission + storage on the server

## 5. Content & polish

- [ ] Curated ranked level pool (`pool_v0.json` is a placeholder — real ids/names/hashes)
- [ ] **Animated semi-transparent Eets ghost by default** (today the default is a drawn marker; an
      anim only shows if `ghost_anim` cfg points at a valid `.anim`). Plan: default to an Eets `.anim`
      with a transparent tint (`DrawAnim` already returns a success bool → fall back to the marker if
      it won't load); add an in-menu "cycle ghost sprite" button over candidate paths since the exact
      Eets anim name lives in the game's `Data/Animations/<n>/<n>.anim`, not the exe (can't read it
      headless). Optional: flip to the opponent's facing.
- [ ] World→screen for scrolling/zoomed levels (apply the GFX view matrix `FUN_0048f2c0`)
- [ ] Sound cues (countdown, win/lose); result panel / rematch button

## 6. Engineering

- [ ] CI: build the mod (Linux+Win) + run `netproto` e2e
- [ ] Host-compilable unit tests for scoring/tiebreakers
- [ ] Commit + push (nothing committed yet)
