# eets-multiplayer-mod

**Hop On Eets** — multiplayer / ranked competitive mod for _Eets_, built on the
[`eets-mod-framework`](../eets-mod-framework) (native plugin loader, framework `0.18.0`).

Competitive **solution racing**: both players get the same puzzle, same pinned seed, same
inventory; a fixed build phase, then the deterministic sim runs and you race on completion +
solution time. The design reuses Eets' own deterministic simulation (Park-Miller PRNG + fixed
timestep), reverse-engineered from the game binary.

## Status

- **v0.1 foundation** — determinism harness, build capture, live ghost view, round scoring.
- **v0.2 realtime online** — see the opponent **live** as a ghost Eets over a WebSocket relay;
  host/join by code or ranked matchmaking; build timer with forced sim start; opponent's locked-in
  build shown as ghost items.
- **v0.3 ranked** — client-UUID Elo ladder + win screen, per-round level pool, reconnect window, forfeit.

Builds clean for Linux (`.so`) and Windows (`.dll`); packs to one `.eetsmod`.

## Features

|                            |                                                                                                 |
| -------------------------- | ----------------------------------------------------------------------------------------------- |
| **Build capture (M1)**     | build-phase placements captured + reconciled to the live build at sim start                     |
| **Determinism (M2)**       | pin FPS / seed / det-mode, lock vanilla game speed, state-hash seq, reset-rerun MATCH/DIVERGE   |
| **Live ghost (M3)**        | draw the live opponent in-level as a translucent ghost Eets + their build items                 |
| **Scoring (M4)**           | score the round by the tiebreakers; best-of tally; relay-authoritative                          |
| **Realtime online (v0.2)** | live opponent position + build over WebSocket; host/join by code; ranked queue                  |
| **Build timer**            | fixed build seconds, then the sim is force-started for both players                             |
| **Custom menu**            | one `Eets::UI` menu (**F6**) holds all settings + match controls; changes persist via `SaveSet` |

## Architecture

```
mod (this repo, native)  <-- WebSocket/TLS, text line protocol -->  relay (netproto/, Node)
   hand-rolled WS+TLS client (src/ws_client.h)                       pairs players, relays live frames, Elo + scoring
```

The mod connects directly to the relay over `wss://` (WS framing + TLS hand-rolled in
`src/ws_client.h`: dlopen'd OpenSSL on Linux, WinHTTP WebSocket on Windows). `netproto/bridge.ts`
is a dev-only TCP↔WS shim used by the e2e test.

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

1. **Relay** (one, shared — or use the hosted `wss://hoe.raccoonlagoon.com`): `cd netproto && npm
install && npm run relay` (listens `:38500`; front with TLS for `wss://`).
2. **Mod**: `make pack`, drop `hop_on_eets.eetsmod` in `<game>/mods`, launch Eets, open the **MODS**
   button. The mod connects directly to the relay (`relay_url`, default the hosted one). In the **F6**
   menu: **Host game** (shares a code) / **Join by code** / **Ranked queue**. Both players build, the
   sim auto-starts, and you see the opponent live.

### Controls

- `F6` — open/close the menu (host/join/queue, online name, score, leave & forfeit).
- `CTRL+SHIFT+H` — toggle match mode (engages determinism). `CTRL+SHIFT+R` — new match.
- Join code is typed in the menu (text entry); your online name + UUID persist across restarts.

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
hop_on_eets.cpp   thin entry layer (framework callbacks)
src/              state hash determinism levels net ws_client recorder ghostview menu match hud (one TU, all #included)
hop_on_eets.cfg   framework manifest (sim = 1)
netproto/         relay + bridge + protocol + e2e (TypeScript, zero runtime deps)
Makefile  .clangd
```

## Relationship to eets-mod-framework

This repo holds the mod, rulesets, level pool, networking, and tooling. It depends on the framework
for loading, hooks, the engine API, and the RE address maps. Ships as one `.eetsmod` and declares
`sim = 1` (it controls the simulation).

Not affiliated with or endorsed by Klei Entertainment; _Eets_ and its assets belong to their owners.
