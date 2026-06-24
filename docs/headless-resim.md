# Headless re-sim — authoritative cross-platform verifier (v0.3)

The top risk in the spec (Parts 6, 17). This is the design + what's already built.

## Why it's needed

Eets float physics is **not bit-identical across OS/build** (Win32 x87/SSE vs Linux64). So a
ranked result CANNOT be decided by comparing client state hashes across platforms (those are only
valid same-platform — see [net-protocol.md](net-protocol.md) checkpoint hashes / spec Part 5).

The portable unit of truth is the **input log** (build placements + seed). The authoritative
outcome is produced by **re-simulating that log on ONE canonical build** and reading the result.
Clients' local sims are visual/advisory; the server's re-sim is the verdict.

## Two pieces

```
  [1] null-backend launch  (infra — game runs with no window/audio)
        +
  [2] mod re-sim mode      (logic — load -> apply build -> start -> read outcome -> verdict)
        =  headless verifier
```

### [2] Mod re-sim mode — BUILT (`src/resim.h`)

Driven entirely by the existing engine hooks; gated by the `resim_file` cfg key. State machine:

```
RS_INIT     parse the input log (seed, level_index, build[], finish claim)
   |        wait for the level catalog, then load_match_level()  (Update/resim_tick)
RS_LOADING  on `level_load`: World_CreateObject() each build item; engage_determinism();
   |        force_start_sim()                                     (resim_on_level_loaded)
RS_RUNNING  sim advances; DNF watchdog at resim_max_ticks (120s)
   |        on `level_complete`: capture finish_tick              (resim_on_complete)
RS_DONE     write Log/hop_on_eets_verdict.json
```

Verdict JSON:
```json
{ "verdict_version":1, "platform":"linux64", "seed":123, "level_index":7,
  "resim": {"completed":true,"finish_tick":1740,"items_applied":5},
  "claim": {"completed":true,"finish_tick":1738},
  "reproduced": true }
```
`reproduced` = same completion + finish_tick within `RESIM_TICK_TOL` (2 ticks, to absorb
canonical-vs-submitter build jitter). The input log is exactly the replay the mod already writes
(`Log/hop_on_eets_replay_*.json`), now self-describing (`level_index` added).

Run it: `resim_file = Log/hop_on_eets_replay_000.json` in the cfg → launch the game → the mod
plays it back unattended and drops the verdict. **Linux-first** (re-uses `load_match_level`, which
is Linux-only until the Win `GetLevelManager`/`LoadSimulatorLevel` addrs are RE'd).

Env overrides (the launcher sets these per run, no cfg edit): `HOE_RESIM_FILE` (input log),
`HOE_RESIM_LEVEL`, `HOE_RESIM_EXIT` (1 = exit on verdict). On `RS_DONE` the mod exits with
**0 = reproduced, 1 = not** so the launcher gets a code without parsing.

### [1] Null-backend launch — BUILT (`tools/resim-runner.sh`) + DESIGN notes

The sim is reachable only through the running game: the mod's `Update` fires on
`FNA3D_SwapBuffers`, and the renderer is a singleton bound to an FNA3D device. To run without a
display:

`resim-runner.sh <log> [-d GAME_DIR] [-l LEVEL] [-t SECS]` does the orchestration today: drop the
stale verdict, export the env, launch the game under **xvfb** (when there's no `$DISPLAY`) with
**`SDL_AUDIODRIVER=dummy`** and a `timeout` guard, then print `Log/hop_on_eets_verdict.json` and
exit with the mod's code. xvfb (Option B) is the working default — it needs a real GL context but
no monitor. Option A below (stub FNA3D) removes even the GL dependency and is the future upgrade.

**Option A — null FNA3D backend (BUILT: `nullbackend/`, unvalidated).** `nullbackend/libnullbackend.so`
shadows the exact 29 `FNA3D_*` entry points Eets imports (enumerated from `eets_addr_win.h`) with no-ops
returning dummy opaque handles — no GPU, no GL, no framebuffer. SDL's `dummy` video/audio drivers cover
the rest. `resim-runner.sh --null` `LD_PRELOAD`s it after the loader. Stubs follow the public FNA3D API;
needs validation against the shipped binary (widen a stub if the game derefs a handle field). Notes:
Ship a `libnullbackend.so` preloaded *before* the mod that exports stub `FNA3D_*` / `SDL_*`
symbols satisfying the game's calls but doing nothing visible:
- `FNA3D_CreateDevice` → a dummy device handle; `FNA3D_SwapBuffers` → returns immediately (still
  drives the mod's per-frame `Update`, which is all the re-sim needs).
- `SDL_CreateWindow`/`GL context` → dummy non-null handles; `SDL_PollEvent` → no events.
- Audio (`FAudio`/`SDL_audio`) → no-op.
The game still ticks its fixed-timestep sim (decoupled from render — `World_SetFPS`), so the mod
drives load→apply→start→outcome with zero frames actually drawn. Headless on a server box
(xvfb not even required if windowing is fully stubbed).

**Option B — Xvfb.** Run the real backend under a virtual framebuffer (`xvfb-run ./Eets`). Simpler
(no symbol stubbing) but heavier and still needs GL; use as the fallback if stubbing FNA3D proves
fragile.

**Batch lifecycle.** The verifier needs the process to *exit* after the verdict. Add a re-sim
follow-up: on `RS_DONE`, request quit (engine quit path TBD — RE `MainMenu`/app-exit, or just
let the orchestrator kill the process once `verdict.json` appears + is fresh). The orchestrator
(below) already watches for the verdict file, so kill-on-verdict works today without an in-engine
quit.

## Server integration (verifier flow)

```
both clients submit: replay (input log + seed) + finish claim + platform
        |
  relay/verifier picks the canonical build, for EACH submission:
     write input log -> launch headless game (resim_file=log) -> read verdict.json
        |
  official result = compare the two re-sim outcomes (completion, finish_tick, items)
     - a claim the re-sim does NOT reproduce  -> that player no-contest / DQ (not a silent loss)
     - both reproduced                        -> decide() on the re-sim outcomes (authoritative)
```

This replaces the relay's **provisional** result (today decided from client claims) with an
authoritative one. The relay's `decide()` tiebreakers (completion → finish_tick → items_used) are
unchanged — they just run on re-sim outcomes instead of claims.

**Orchestration shape** (not yet built): a `ResimRunner` with one method
`run(inputLogPath) -> verdict`, implemented as "spawn the headless game, poll for a fresh
`Log/hop_on_eets_verdict.json`, parse it, kill the process." The relay stays transport-only; the
verifier is a separate worker that the relay hands completed matches to. A mock `ResimRunner`
(returns a canned verdict) lets the whole verify→decide path be unit-tested in `netproto`
without the game.

## Status / next

- [x] Mod re-sim driver (`src/resim.h`) — parse log, apply build, start, read outcome, verdict.
- [x] Self-describing replays (`level_index` in the replay JSON).
- [x] Exit-on-done with a verdict exit code + env overrides (`HOE_RESIM_*`) for batch runs.
- [x] Headless launcher (`tools/resim-runner.sh`) — xvfb + dummy audio, watch verdict, exit code.
- [x] `ResimRunner` + `verifyMatch` (`netproto/verifier.ts`) with a `MockResimRunner`; verify→decide
      unit-tested (`verifier.test.ts`, in `npm test`). `ShellResimRunner` spawns the launcher.
- [x] Null FNA3D backend (`nullbackend/libnullbackend.so`, `--null`) — Option A; built + covers all 29
      imports; **unvalidated against the game** (xvfb stays the default until proven).
- [x] Relay → verifier handoff on ranked completion — clients `submit_replay` (base64 input log); on a
      ranked round the relay calls the injected `RankedVerifier` and emits `authoritative` to both. The
      relay CLI enables it when `EETS_DIR` is set (`makeVerifier(new ShellResimRunner(), save)`). e2e-covered.
- [ ] Windows canonical build (needs Win `GetLevelManager`/`LoadSimulatorLevel` addrs first).
- [ ] In-game verification of the whole path (TESTING.md Phase J) — incl. validating `--null`.

## Caveat

Even one canonical build is the *sole* source of truth — small bounce differences can still flip
an outcome, so ranked must always re-sim on the SAME build/platform for both submissions (not
"each on its own platform"). Cross-platform fairness comes from both being judged by the same
canonical runtime, never from trusting either client's local physics.
