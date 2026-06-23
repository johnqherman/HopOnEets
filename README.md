# eets-multiplayer-mod

**Hop On Eets** — multiplayer / ranked competitive mod for *Eets*, built on the
[`eets-mod-framework`](../eets-mod-framework) (native plugin loader, framework `0.18.0`).

Competitive **solution racing**: both players get the same puzzle, same pinned seed, same
inventory; a fixed build phase, then the deterministic sim runs and you race on completion +
solution time. The design reuses Eets' own deterministic simulation (Park-Miller PRNG + fixed
timestep), reverse-engineered from the game binary. The full design is in
[docs/hop-on-eets-spec.md](docs/hop-on-eets-spec.md).

## Status

- **v0.1 foundation** — replay recorder, determinism harness, ghost race, local mirror scoring.
- **v0.2 realtime online (prototype)** — see the opponent **live** as a ghost Eets over a
  WebSocket relay; host/join by code or ranked matchmaking; 15s build timer with forced sim start;
  opponent's locked-in build shown as ghost items.

Builds clean for Linux (`.so`) and Windows (`.dll`); packs to one `.eetsmod`. Online is Linux-first
(the native socket client is POSIX; Windows online is a follow-up winsock build).

## Features

| | |
|---|---|
| **Recorder (M1)** | build-phase placement set + finish → replay JSON |
| **Determinism (M2)** | pin FPS / seed / det-mode, lock vanilla game speed, state-hash seq, reset-rerun MATCH/DIVERGE |
| **Ghost race (M3)** | record/load an Eets timeline; draw the opponent in-level as a translucent ghost Eets |
| **Mirror scoring (M4)** | score the round by the spec tiebreakers; best-of tally; ranked-ready result JSON |
| **Realtime online (v0.2)** | live opponent position + build over WebSocket; host/join by code; ranked queue |
| **Build timer** | fixed build seconds (default 15), then the sim is force-started for both players |
| **Custom menu** | one `Eets::UI` menu (**F6**) holds all settings + match controls; changes persist via `SaveSet` |

## Architecture

```
mod (this repo, native)  <-- localhost TCP, text -->  bridge (netproto/, Node)  <-- WebSocket -->  relay (netproto/, Node)
   tiny non-blocking socket; no WS/TLS in the plugin        WS client + mod TCP server                  pairs players, relays live frames
```

The realtime leg is a WebSocket; the mod stays a tiny TCP client so the 32-bit plugin needs no
WS/TLS library. Protocol: [docs/net-protocol.md](docs/net-protocol.md).

## Build (mod)

Needs a sibling `eets-mod-framework` checkout and `g++` (+ `i686-w64-mingw32-g++` for the `.dll`).

```sh
make                    # build/hop_on_eets.so  (Linux)
make win                # build/hop_on_eets.dll (Windows)
make pack               # hop_on_eets.eetsmod   (bundle, via the framework's eetsmod CLI)
make FW=/path/to/eets-mod-framework   # if the framework lives elsewhere
```

The mod is one translation unit: `hop_on_eets.cpp` (thin entry layer) `#include`s `src/*.h`.

## Run (online quickstart)

1. **Relay** (one, shared): `cd netproto && npm install && npm run relay` (listens `:38500`).
2. **Bridge** (each player, local): `RELAY_HOST=<relay-ip> PLAYER=<name> npm run bridge`
   (listens for the mod on `127.0.0.1:38600`).
3. **Mod**: `make pack`, drop `hop_on_eets.eetsmod` in `<game>/mods`, launch Eets, open the **MODS**
   button. In the **F6** menu: turn **Online** on, then **Host game** (shares a code) / **Join by
   code** / **Ranked queue**. Both build for 15s, the sim auto-starts, and you see the opponent live.

Offline, it still records and you can race a **ghost file** (`Race last recording` in the menu).

### Controls

- `F6` — open/close the menu (settings, ghost, host/join/queue, score). All config lives here.
- `CTRL+SHIFT+H` — toggle match mode (engages determinism). `CTRL+SHIFT+R` — new match.
- Join code is typed in the menu (text entry); settings persist across restarts.

Outputs land in `<game>/Log/`: `hop_on_eets_replay_*.json`, `*_ghost_*.txt`, `*_result_*.json`.

## netproto (TypeScript)

Zero-runtime-dependency relay + bridge (Node, hand-rolled WebSocket). `cd netproto`:

```sh
npm install        # dev deps only (typescript, tsx)
npm test           # end-to-end: host/join + ranked + realtime relay + build exchange + result
npm run typecheck  # tsc --noEmit (strict)
npm run relay      # start the relay server
npm run bridge     # start a bridge (RELAY_HOST / MOD_PORT / PLAYER env)
```

## Layout

```
hop_on_eets.cpp             thin entry layer (framework callbacks)
src/                        state.h hash.h determinism.h net.h recorder.h ghostview.h menu.h
hop_on_eets.cfg             framework manifest (sim = 1) + first-run setting defaults
hop_on_eets.assets/HopOnEets/{rulesets,levels,net}/   ruleset, level pool, net config (JSON)
netproto/                   relay + bridge + e2e (TypeScript, zero runtime deps)
docs/hop-on-eets-spec.md    technical specification (RE-grounded)
docs/net-protocol.md        realtime protocol
Makefile  compile_flags.txt  .clangd
```

## Relationship to eets-mod-framework

This repo holds the mod, rulesets, level pool, networking, and tooling. It depends on the framework
for loading, hooks, the engine API, and the RE address maps. Ships as one `.eetsmod` and declares
`sim = 1` (it controls the simulation).

Not affiliated with or endorsed by Klei Entertainment; *Eets* and its assets belong to their owners.
