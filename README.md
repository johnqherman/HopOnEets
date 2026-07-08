<p align="center">
  <img width="498" height="323" alt="eets" src="https://github.com/user-attachments/assets/32b49171-952c-469c-80a2-f7511f1df719" />
</p>

[![deploy](https://github.com/johnqherman/HopOnEets/actions/workflows/deploy.yml/badge.svg)](https://github.com/johnqherman/HopOnEets/actions/workflows/deploy.yml)

**Hop On Eets** — a multiplayer / ranked competitive mod for **Eets** (the Klei puzzle
game), built on [eets-mod-framework](https://github.com/johnqherman/EetsMod).

It's competitive **solution racing**: both players get the same puzzle, the same pinned
seed, and the same inventory; a fixed build phase, then the deterministic sim runs and
you race on completion + solution time — while watching your opponent play live as a
translucent ghost. Ranked ladder, best-of series, host/join by code, casual and ranked
queues.

## Playing

1. **Set up mod support** for Eets: install the loader from
   [eets-mod-framework](https://github.com/johnqherman/EetsMod#playing-with-mods)
   (drop one file in the game folder; Linux also sets a launch option).
2. **Install the mod.** Grab `hop_on_eets.eetsmod` from a [release](../../releases) and
   put it in `<game>/mods`. Launch Eets and enable it from the **MODS** button on the
   main menu.
3. **Play.** Press **F6** in game: **Host game** (shares a code), **Join by code**, or
   enter the **ranked / casual queue**. Both players build, the sim auto-starts, and you
   see the opponent live.

The mod connects to the hosted relay (`wss://hoe.raccoonlagoon.com`) out of the box —
no server setup. It also keeps itself current: an outdated client downloads the latest
release in the background (sha256-verified), blocks multiplayer, and shows an
**UPDATING** screen with a Restart button.

### Controls

- **F6** — the one in-game menu: host/join/queue, online name, score, settings, leave &
  forfeit. Join codes are typed here; your name + rating persist across restarts.
- **CTRL+SHIFT+H** — toggle match mode (engages determinism). **CTRL+SHIFT+R** — new match.

## How a match works

- **Build phase.** A fixed number of build seconds, then the sim is force-started for
  both players. Your placements are captured and reconciled to the live build at sim
  start; the opponent's locked-in build shows as ghost items.
- **Deterministic race.** The mod pins FPS, seed, and det-mode and reuses Eets' own
  deterministic simulation (Park-Miller PRNG + fixed timestep, reverse-engineered from
  the game binary), so both clients run the identical puzzle. State hashes verify it.
- **Live ghost.** The opponent is drawn in-level as a translucent, latency-compensated
  ghost Eets streaming over the relay.
- **Scoring.** Rounds score on completion + solution time tiebreakers; best-of tally,
  relay-authoritative. Vote-to-mulligan a bad round; three drawn rounds in a row is a
  NO CONTEST. Disconnects get a reconnect window before forfeit.
- **Ranked.** A Glicko-2 ladder (margin-aware, per-round) with rank badges by player
  names and a cinematic showdown intro.

## Building the mod

Needs a sibling [eets-mod-framework](https://github.com/johnqherman/EetsMod) checkout
and `g++` (+ `i686-w64-mingw32-g++` for the Windows `.dll`).

```sh
make                    # build/hop_on_eets.so  (Linux)
make win                # build/hop_on_eets.dll (Windows)
make pack               # hop_on_eets.eetsmod   (bundle, via the framework's eetsmod CLI)
make FW=/path/to/eets-mod-framework   # if the framework lives elsewhere
```

The mod is one translation unit: `hop_on_eets.cpp` (thin entry layer) `#include`s
`src/*.h`. It declares `sim = 1` in its manifest — it controls the simulation.

## The relay (`netproto/`)

A zero-runtime-dependency Node/TypeScript relay pairs players, relays live frames, and
keeps the ranked ladder. The mod speaks to it directly over `wss://` (WS framing + TLS
hand-rolled in `src/ws_client.h`: dlopen'd OpenSSL on Linux, WinHTTP on Windows).

```sh
cd netproto
npm install        # dev deps only (typescript, tsx)
npm test           # end-to-end: host/join + ranked + realtime relay + build exchange + result
npm run typecheck  # tsc --noEmit (strict)
npm run relay      # start a relay server (listens :38500; front with TLS for wss://)
```

To host your own relay (Docker behind nginx, CI/CD, cutting a release for self-update),
see [`netproto/DEPLOY.md`](netproto/DEPLOY.md).

## Layout

```
hop_on_eets.cpp   thin entry layer (framework callbacks)
src/              match net ghostview hud menu determinism levels recorder ws_client (one TU, all #included)
hop_on_eets.cfg   framework manifest (version, sim = 1)
netproto/         relay + protocol + e2e tests (TypeScript, zero runtime deps)
Makefile          build / win / pack
```

## License

MIT.

Not affiliated with or endorsed by Klei Entertainment; _Eets_ and its assets belong to
their owners. This mod ships no game code, only original code and addresses derived from
our own copy of the game. Use at your own risk.
