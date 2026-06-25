# Hop On Eets — Technical Specification

Status: v0 (foundational). Working repo: **eets-multiplayer-mod**.

## Context

**Hop On Eets** is a multiplayer / ranked competitive mod for *Eets*, built on the
[`eets-mod-framework`](../../eets-mod-framework) (native plugin loader, framework **0.18.0**).
This repo (`eets-multiplayer-mod`) holds the mod, its rulesets, curated level pool, networking,
and tooling.

This document is the **final** spec. It supersedes the loose draft by replacing its assumptions
with **evidence from the Eets binary (reverse-engineered in Ghidra) and the framework source**.
The loose draft assumed deterministic physics, a forced fixed timestep, a per-tick input model,
two level instances per process, readable finish state, headless replay validation, and internal
random seeds. Each is resolved below (Part 0).

Design decisions:
1. **v0.1 = feasible foundation** — determinism harness + a live head-to-head race (buildable on
   the framework as-is). Online mirror and ranked follow with an explicit hook-build roadmap.
2. **Solution-race model** — Eets is *build-then-simulate*; reuse the engine's own determinism
   (same puzzle + pinned seed → reproducible run); rank by completion + solution time.
3. **Live head-to-head** — both players race the same puzzle; each streams its position + build to
   the other (drawn as a ghost). The relay is authoritative for the series score, deciding rounds
   from the players' reported finishes. Cross-platform integrity = same-platform desync detection
   (FP physics isn't bit-identical across OS, so cross-platform hashes are never compared).

---

## Part 0 — Reverse-engineering evidence base

Analysis target: the **Windows PE32 (i386) `Eets.exe`** (image base `0x400000`, code
`0x401000–0x55f878`). The framework's hand-built address maps are authoritative for symbol names:
`eets-mod-framework/include/eets_addr.h` (Linux absolutes) and `eets_addr_win.h` (Windows RVAs;
Ghidra addr = `0x400000 + RVA`). Build identity from an in-binary assert path
`C:\Users\flibi\source\repos\Eets1\Project\TBW\Source\Game\cross\LuaLibGame.cpp` → original
**Eets (2006, "Eets1"/TBW)**, native C++, cross-platform shared sim. Linux build-id guard:
`e81cc5504d3ef03324805df3e9fc508c1bf8c628`.

### Open technical questions — answered with evidence

| # | Question | Answer | Evidence |
|---|----------|--------|----------|
| 1 | Two level instances in one process? | **No** | `Simulator`/`ObjectMgr` are singletons (`Simulator_i` → global `DAT_00ee3d9c`; `ObjectMgr_i` → `DAT_00edf168`). One global world. Mirror race = two processes/machines, or one live player + ghost. |
| 2 | Physics deterministic across machines? | **Same build/OS: yes. Cross-OS: not bit-guaranteed.** | Fixed timestep + integer-state PRNG are portable; contraption physics integrates in float/double, so x87/SSE + compiler differences (Win32 vs Linux64) can diverge. |
| 3 | Force fixed timestep? | **Already fixed.** | `World_SetFPS(int)` (`FUN_004f7c80`) writes the sim timestep `*(double*)([sim+0x34]+0x90) = DAT_005625e8 / fps`. The sim integrates at `dt = k/FPS`, decoupled from render rate. |
| 4 | Levels editable without repacking archives? | **Yes.** | `Simulator::LoadWinCondition` (`FUN_005378c0`) builds `LEVELS:Game/<name>` and loads `DATA:Levels/Utility/GoalUtils.lua`. `LEVELS:`/`DATA:` are virtual-FS prefixes; the framework overlays loose files (`assets/` mirrors `Data/`, copied before the engine scans). Goal logic is **Lua**. |
| 5 | Finish state readable? | **Yes.** | `LevelManager::CompleteLevel` (`FUN_004c98f0`): FNV-1a(name) → hub/level map, sets per-level *completed* flag `+0x48`, drives unlock thresholds. Already detoured to fire `level_complete(LevelManager*, Player*)`. `World_ShowSolutionTime(float)` and `World_IncrementStat(name)` expose timing/stats. |
| 6 | Headless sim (no window)? | **Hard.** | The sim is reached only through the running game (mod `Update` fires on `FNA3D_SwapBuffers`; renderer is a singleton bound to an FNA3D device). Headless needs Xvfb (a real GL context, no monitor). Not on the live path — ranked scores from reported finishes (Part 6). |
| 7 | Internal random seeds? | **Yes — and pinnable.** | `World_GetDeterministicIntRange` → `FUN_004bab10`: if det-mode flag `DAT_00ee3bf0 == 0` it uses libc `rand()`; otherwise a **Park-Miller minimal-standard PRNG via Schrage's method** over integer state `DAT_005a1790` (params `0x5a1794`/`0x5a1798`/`0x5a179c`/`0x5a17a0`). The mod can write both the det-mode flag and the seed. |

### The determinism core

```c
// FUN_004bab10  (World_GetDeterministicIntRange core, Win 0x4bab10)
if (DAT_00ee3bf0 == '\0') {              // det-mode OFF -> non-deterministic
    return lo + rand() % (hi-lo+1);      //   libc rand()
}                                        // det-mode ON  -> Park-Miller / Schrage (integer state)
DAT_005a1790 = (seed % q)*a - (seed / q)*r;   // a=DAT_005a1794 q=DAT_005a179c r=DAT_005a17a0 m=DAT_005a1798
return lo + (int)( (double)norm(seed) / (double)m * range );
```

Integer state update ⇒ **bit-reproducible across machines**. With the fixed `dt=1/FPS` and
`Simulator::ResetSimulation` (`FUN_00536090`, which restores objects to their initial snapshot),
the engine is **deterministic by construction given {level, seed, FPS, inputs}** — on the same
build. This is the substrate the live head-to-head race reuses.

### Framework reality (from `loader/loader.cpp`, `API.md`, headers, examples)

- **Native plugin**, C++17. Linux `.so` via `LD_PRELOAD`; Windows `version.dll` proxy. Packaged
  as a `.eetsmod` (stored ustar) bundle. Loader hooks `FNA3D_SwapBuffers`, `SDL_PollEvent`,
  `FNA3D_SetViewport`. Framework version **0.18.0**.
- **Manifest is a `.cfg`** (key=value), not `mod.json`. Keys: `version`, `author`,
  `min_framework`, `priority`, `requires`, and crucially **`sim`** — *"affects simulation
  (replays/leaderboards)"*. The loader warns when a `sim` mod is active. ⇒ **Eets has a native
  deterministic replay/leaderboard system**, and the framework already models mod-integrity
  against it. Hop On Eets MUST set `sim = 1`.
- **Mod entry points:** `EetsMod_Init/Update/OnKey/OnMouse/OnWheel/OnEvent/OnText/Shutdown`.
  `Update` fires once per rendered frame (wall-clock from `CLOCK_MONOTONIC`) — **render rate, not
  sim tick.**
- **Engine events already wired:** `object_spawn`, `object_killed`, `level_load`, `level_reset`,
  `level_complete`, `emotion_change`, `goal_check`, `eets_death`.
- **`Eets::Hook(target, detour, &orig)` is exposed to mods** (32-bit E9 inline hook). ⇒ every
  *missing* hook is buildable from a known RVA + `Hook()`.
- **Rich state-read API** exists: `ForEachObject`, `World_GetEets`, `World_GetObjectByID`,
  `Object_GetID/GetPosition/GetVelocity/GetBlueprintHash/GetBlueprintName`, all extension
  accessors, `World_IsSimulating/IsPaused/IsInLevelEditor`, `World_CreateObject`,
  `World_CopyItem/PasteItem`, `World_SaveLevel`, `World_ShowSolutionTime`.
- **No input *injection*** — mods read SDL input, cannot synthesize engine input. (Fine: an
  opponent is a ghost; their actions are *replayed as data*, not injected.)

### Hook gap analysis (required hooks vs reality)

| Required | Status | Realization |
|----------|--------|-------------|
| `on_tick` / before+after sim step | **Build** | Hook the per-tick `Simulator` step (vftable `0x56cf34`; identify advance method in M2) via `Eets::Hook`, or read the sim tick/time counter from `Simulator` state each frame. `Update` alone is render-rate. |
| `on_place_item` / `on_remove_item` | **Derive/build** | Placement ≈ existing `object_spawn` filtered to `!World_IsSimulating()` + item blueprints. Removal ≈ `object_killed` in build phase or hook the editor/`Creator` delete path. |
| `on_activate_item` | **Build** | Hook the in-sim activation path (item-specific; RE in M4). |
| `on_restart_level` | **Have** | `level_reset` event. |
| `on_finish_level` | **Have** | `level_complete` event (+ `goal_check`). |
| `load_level` | **Have** | `level_load` event. |
| `get_level_hash` | **Build (trivial)** | SHA-256 the loose level file at `LEVELS:Game/<name>`; engine name hash is FNV-1a (`0x811c9dc5`/`0x1000193`) if a cheap key is wanted. |
| `get_inventory` | **Build (RE)** | Read the level's allowed-item inventory (structure on `Simulator`/level; RE in M3). |
| `get_start_state` | **Build** | Snapshot via `ForEachObject` after `level_reset`; engine already restores initial state on reset. |
| `get_state_hash` | **Build** | Hash a snapshot of {id, blueprint hash, position, velocity, key extension states} over `ForEachObject` (Part 5 caveat: same-build only). |
| `get_finish_state` / `get_score_state` | **Build** | From `level_complete` params + solution time + `World_IncrementStat` counters. |

---

## Part 1 — Overview

Competitive **solution racing**: two players receive the **same puzzle, same pinned seed, same
allowed inventory, same ruleset**, build a solution during a build phase, and run the
deterministic simulation; the winner is decided by completion + solution time. Each player sees the
other **live** (streamed position + build, drawn as a ghost); the relay scores the series.

Mod id `hop_on_eets`; built as a `.eetsmod`; framework `eetsmod >= 0.18.0`; manifest declares
`sim = 1`.

## Part 2 — Architecture

```
Eets.exe / Eets (native)            eetsmod framework (0.18.0)
   |  FNA3D_SwapBuffers, SDL_PollEvent (hooked)  |
   +----------------------------------------------+
                        |
                  Hop On Eets mod (.eetsmod, sim=1)
   +--------------+--------------+-------------------+-----------------+
   | Determinism  | Sim-tick     | Build Recorder    | Live Ghost View |
   | Controller   | Source       | (placement set,   | (opponent pos + |
   | (seed + FPS  | (step hook / |  shared live to   |  build, drawn   |
   |  + det-mode  | sim counter) |  the opponent)    |  from the net)  |
   |  + speedlock)|              |                   |                 |
   +--------------+--------------+-------------------+-----------------+
   | State Hasher | Level Loader | Result Reporter   | Net Client      |
   | (snapshot)   | + inventory  | (finish + time)   | (WS/TLS)        |
   +--------------+--------------+-------------------+-----------------+
                        |
                  Ranked server: relay (pairing + live frames + Elo + series scoring)
```

## Part 3 — Gameplay model: Solution Race

Eets gameplay has two phases (confirmed by `World_IsSimulating` / `World_IsInLevelEditor` /
`World_IsPaused`):

1. **Build phase** — player places/removes items from a finite inventory (`!IsSimulating()`).
2. **Simulation phase** — "Go": deterministic sim runs Eets toward the goal; sparse activations
   only. `goal_check` / `level_complete` end it.

A match is one or more **rounds**; a round = (shared puzzle + seed) → both players build → both
sims run → compare. Players never share a board (singleton world) — each runs an isolated copy;
the opponent appears as a **live ghost** streamed from their client (position + anim + build).

**Win conditions:** first to complete. Tiebreakers: completion status → finish tick (solution
time) → puzzle pieces collected → fewer items used → fewer resets. All readable from
`level_complete` + solution time + stats + recorder counters.

## Part 4 — Tick & input model

- **Tick** = the engine's fixed sim step. Pin the rate with `World_SetFPS(TICK_RATE)` at sim start
  so both clients agree. `TICK_RATE = 60` pending confirmation of the native rate (M2); whatever it
  is, **pin it explicitly**. Render rate is irrelevant to outcome.
- **tick 0** = first sim step after the build phase ends (post-`level_reset`, "Go" pressed).
- **Build capture** — placements are recorded from `object_spawn`/removal while `!IsSimulating()`
  (reconciled to the live objects at sim start, so a build reset keeps only the final contraption).
  At sim start the locked-in build is streamed to the opponent (`build … buildend`) to draw as ghost
  items. Mid-sim player activations (launchers etc.) are not recorded — they're played live, not
  logged.

## Part 5 — Determinism model (the core enabler)

At sim start the **Determinism Controller** must:
1. Set det-mode flag `DAT_00ee3bf0 = 1` (Win) so gameplay RNG uses the Park-Miller generator, not
   libc `rand()`.
2. Write the **seed** to the PRNG state `DAT_005a1790` (Win) from `MatchConfig.seed` (resolve the
   Linux equivalent; addresses live next to the generator — RE in M2). Re-seed on each
   `level_reset` so every attempt is reproducible.
3. Pin the timestep with `World_SetFPS(tick_rate)`.
4. **Lock vanilla speed settings** — re-assert `World_SetGameSpeed(NORMAL)` and hold it for the
   whole match (see below).

> Resolve these globals per-platform from the framework's address maps / a small pattern scan,
> guarded by the build-id check the loader already performs.

### Locked settings in head-to-head matches

Eets exposes a player-facing **game-speed** control (`World_SetGameSpeed`, Win `0x4dc5f0` →
`FUN_004f7cb0`): it stores the speed at `[timer+0x24]`, remaps the enum (0→2, 1→0, 2→1) and flips
a timer-mode vtable slot (+0x38) — i.e. **pause / normal / fast** stepping. It is *separate* from
the per-step `dt` (which `World_SetFPS` pins via `FUN_004f7c80`), so by **sim tick the outcome is
speed-independent**. But an unlocked speed control still allows pausing the sim to think,
fast-forwarding for a wall-clock edge, and desyncing the live/spectator view between the two
boards. Therefore, in any head-to-head match (mirror / solution race / ranked):

- The mod **locks the vanilla speed settings to a canonical value** (game speed = normal `1`; no
  player pause/fast). Enforce by re-applying `World_SetGameSpeed(1)` each frame while
  `IsSimulating()` and intercepting the vanilla speed hotkeys (capture in `EetsMod_OnKey` / detour
  the bound action). Also pin any related vanilla pacing setting (`World_SetMaximumSpeed`, Win
  `0xdc770`) to its canonical value.
- Speed/pause tampering is an **integrity violation** → the round is no-contest (Part 9).
- The locked set is declared in the ruleset (`locked_settings`) so it is auditable.

### Cross-platform caveat (drives the ranked design)

The PRNG and timestep are portable, but float physics is **not** bit-identical across
Win32/Linux64. Therefore:
- `get_state_hash` snapshots are **only comparable between clients on the same build/OS** — use
  them for *same-platform* desync detection (Part 10), never as cross-platform truth.
- Ranked results come from the players' **reported finishes**, scored by the relay (Part 6), not
  from comparing physics across platforms.

## Part 6 — Ranked validation

The relay is authoritative for the series score: it pairs the two players, relays their build/finish
data, and decides each round from the players' **reported finishes** (completion → finish_tick →
deaths → items_used; Part 3). Cross-platform float physics is not bit-identical, so results are
**not** decided by comparing client state hashes across platforms — the integrity check is
**same-platform desync detection** (Part 10): a hash mismatch between same-platform clients voids the
match (no-contest), and locked-settings enforcement (Part 9) stops speed/pause tampering.

## Part 7 — MatchConfig

```json
{
  "match_id": "hoe_2026_000001",
  "mode": "solution_race",
  "ruleset": "ranked_v0",
  "level_id": "official_023",
  "level_hash": "sha256:...",
  "game_buildid": "e81cc5504d3ef03324805df3e9fc508c1bf8c628",
  "platform": "linux64|win32",
  "framework": "eetsmod",
  "framework_version": ">=0.18.0",
  "tick_rate": 60,
  "seed": 123456789,
  "determinism": { "mode": "park_miller", "seed_pinned": true, "fps_pinned": true, "speed_locked": true },
  "locked_settings": { "game_speed": "normal", "pause": false, "fast_forward": false },
  "players": [ {"player_id":"p1","display_name":"PlayerOne"}, {"player_id":"p2","display_name":"PlayerTwo"} ]
}
```

## Part 8 — Live build share

There is no recorded replay/ghost file. The opponent is shown **live**: each player streams its
position + anim state every frame, and at sim start broadcasts its locked-in build placements
(`build <name> <x> <y>` … `buildend`), which the opponent draws as translucent ghost items. The
relay scores from the per-round `finish` (completion, finish_tick, deaths, items_used). Same-platform
clients also stream periodic checkpoint state-hashes (carrying `platform`, compared same-platform
only) for desync detection (Part 10).

## Part 9 — Anti-cheat & validation

The relay is authoritative for the series score (Part 6). Same-platform clients exchange checkpoint
state-hashes; a mismatch ⇒ **no-contest** (Part 10). `sim = 1` mods other than the sanctioned ranked
build invalidate ranked (the framework surfaces this integrity signal). **Locked-settings
enforcement:** vanilla speed settings (game speed / pause / fast-forward via `World_SetGameSpeed`;
`World_SetMaximumSpeed`) are locked for the whole head-to-head match (Part 5); detected tampering
⇒ no-contest. If stronger anti-cheat is ever needed, cheap server-side plausibility checks
(finish-tick within a level's known min-solve bound, sane item/death counts) are preferred.

## Part 10 — Desync handling

`STATE_HASH_INTERVAL = 300` ticks. Cross-platform hash mismatch is **expected, not a desync** —
only same-platform mismatch marks a desync. On desync: mark match no-contest, keep playing visually,
withhold the ranked result, save a diagnostic (tick, platform, seed, both hashes).

## Part 11 — Networking

Client-server. Server: create matches, send MatchConfig, relay live position/build + finish data,
and score the series. Because the board isn't shared, the network path carries **live position
frames + build placements + finish claims + checkpoint hashes**, not a full per-tick physics stream.
A mid-match drop is held briefly for a reconnect window before awarding the opponent.

## Part 12 — Ranked system

1v1, best-of-3, Elo (K=32, default 1000, keyed by client UUID), random ranked-pool map, disconnect =
loss unless reconnect within the window, same-platform desync = no contest. Rank ladder: Bronze Spoon
→ Silver Spoon → Gold Spoon → Master Chef → Iron Stomach → Competitive Eater → Grand Muncher → Top Eets.

## Part 13 — Level pool & format

Levels are loose files under the `LEVELS:` virtual FS (`LEVELS:Game/<name>`), with goal logic in
Lua (`DATA:Levels/Utility/GoalUtils.lua` + per-level win condition). The framework's loose-file
overlay means the ranked pool ships curated level files in the `.eetsmod`'s `assets/` tree
(mirroring `Data/`); `level_hash` = SHA-256 of the level file. Ranked-legal criteria: finishable
<90s by skilled players, multi-route, low physics variance, spectator-clear, no long waits, not
tutorial-only. Per-level metadata JSON carries `level_id`, `name`, `source`, `ranked_legal`,
`estimated_time_seconds`, `difficulty`, `tags`, `level_hash`, `game_buildid`.

## Part 14 — File layout

```
hop_on_eets.cpp                 # the mod (one TU; src/*.h #included; compiled to .dll/.so, packed to .eetsmod)
src/*.h                         # state / hash / determinism / levels / net / ws_client / recorder / ghostview / menu / match / hud
hop_on_eets.cfg                 # framework manifest — MUST include: sim = 1, min_framework
netproto/                       # relay + bridge + protocol + e2e (TypeScript, zero runtime deps)
```
The framework manifest is **`<name>.cfg`**, not `mod.json`. The ranked level pool is built at runtime
by walking the engine's `LevelManager` (`src/levels.h`) — not shipped as data files. Rules/tick/relay
URL are compile-time constants in `src/state.h`.

## Part 15 — Milestones

**v0.1 — Foundation (feasible on framework as-is):** *implemented in `hop_on_eets.cpp` — see README.*
- **M1 Build capture** — *[done]* capture build-phase placements (`object_spawn` while
  `!IsSimulating()`), reconcile to the live objects at sim start, confirm finish tick.
- **M2 Determinism harness** — *[done, with caveat]* pin seed (`DAT_00ee3bf0`/`DAT_005a1790`, Win) /
  FPS; lock vanilla game speed; per-run `get_state_hash` sequence; reset-rerun MATCH/DIVERGE check.
  Now reads the engine's real sim-tick counter (not a per-Update count).
- **M3 Ghost view** — *[done]* draw the opponent as a world-space translucent ghost Eets (identity
  camera on single-screen levels — `spawner` proves render coords == world coords; scrolling levels
  would need the GFX view matrix `FUN_0048f2c0`); HUD round/build clocks. Custom `Eets::UI` menu (F6).
- **M4 Scoring** — *[done]* score the round by the tiebreakers; best-of match tally.

**v0.2 — Online mirror:** *[done — `netproto/` (TS) + mod net client]* realtime relay (mod → WS/TLS
→ relay); private host/join by 6-char code or ranked matchmaking; live opponent position + locked-in
build streamed and drawn as a ghost Eets; build timer force-starts the sim for both; round result by
tiebreakers; see `docs/net-protocol.md`.

**v0.3 — Ranked:** client-UUID identity, ranked queue, Bo3, Elo ladder + win screen, per-round level
pool, reconnect window, forfeit. Relay-authoritative scoring from reported finishes.

## Part 16 — Future features

Attack mode (deterministic disruptions), draft mode, community maps. If a mid-sim **input track** is
ever recorded (the activations the build-only capture omits), deeper validation / spectating opens up.

## Part 17 — Risks

1. **Cross-OS FP divergence** — cross-platform physics isn't bit-identical, so ranked never compares
   physics across platforms; integrity = same-platform desync detection + relay-authoritative scoring.
2. **Sim-step hook / tick source** — uses the engine's real sim-tick counter; confirmed (M2).
3. **Inventory + activation structures** — RE tasks (M3/M4).
4. **Build-id coupling** — addresses/globals are build-specific; reuse the loader's build-id guard
   and resolve per-platform.

---

## Appendix A — Verification approach

- **Spec fidelity:** every open question (Part 0) carries a concrete binary/source citation; no
  loose-draft assumption survives unflagged.
- **Determinism harness (M2):** with the probe active, build+run a known level twice and confirm
  **identical** `get_state_hash` sequences (proves seed/det-mode/FPS pinning + speed lock).
- **Packaging:** `eetsmod pack hop_on_eets.cpp -o hop_on_eets.eetsmod` compiles under
  `g++ -std=c++17`; drop into `<game>/mods`, enable via the in-game MODS button.
