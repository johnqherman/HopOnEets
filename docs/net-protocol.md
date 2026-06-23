# Hop On Eets — Network Protocol (v0.2, implemented)

Two hops, so the 32-bit native plugin needs no WebSocket/TLS library:

```
mod  <-- localhost TCP, newline text -->  bridge  <-- WebSocket JSON -->  relay  <-- ... -->  opponent
```

The realtime leg (bridge ↔ relay) is a WebSocket. The relay pairs two players and **relays live
frames** (position, build) between them in real time — it does not simulate. Authoritative
cross-platform re-sim is still v0.3 (spec Parts 6, 11). Reference impl + e2e test: `netproto/`.

## Connect modes

- **Private:** one player sends `host` → gets a 6-char `code`; the other sends `join <code>`.
- **Ranked:** both send `queue`; the relay matchmakes (`ranked` flag set on the match).

## Relay messages (WebSocket JSON)

| type | dir | payload | meaning |
|------|-----|---------|---------|
| `hello` | C→S | `player_id` | identify |
| `host` | C→S | — | create a private room |
| `room` | S→C | `code` | your room code (share it) |
| `join` | C→S | `code` | join a private room |
| `join_failed` | S→C | `code` | no such room |
| `queue` | C→S | — | enter ranked matchmaking |
| `match_config` | S→C | `match_id, ranked, tick_rate, seed, self, opponent` | paired; start build phase |
| `countdown` | S→C | `seconds` | start the synced build phase; both clients align their build timer to this |
| `pos` / `opp_pos` | C→S / S→C | `tick, x, y` | live Eets position (relayed to opponent) |
| `build` / `opp_build` | C→S / S→C | `name, x, y` | one locked-in build item |
| `buildend` / `opp_buildend` | C→S / S→C | — | end of the build set |
| `finish` / `opp_finish` | C→S / S→C | `finish_tick, completed, items_used` | round finished |
| `result` | S→C | `winner, reason, provisional` | provisional outcome (tiebreakers below) |
| `opponent_left` | S→C | — | opponent disconnected |

Tiebreakers (relay `decide`): completion → finish_tick → items_used (spec Part 3).

## Mod ↔ bridge (localhost TCP, newline text)

```
mod -> bridge:  hello <id> | host | join <CODE> | queue | build <name> <x> <y> | buildend |
                pos <tick> <x> <y> | finish <tick> <0|1> <items>
bridge -> mod:  code <CODE> | joinfail <CODE> | match <id> <opp> <ranked0|1> | countdown <secs> |
                ob <name> <x> <y> | obend | g <tick> <x> <y> |
                oppfin <tick> <0|1> <items> | result <winner> <reason> | oppleft
```

The bridge buffers mod lines until its WebSocket is up, so the mod can connect and queue
immediately.

## Validation status

- **v0.2 (now):** provisional results from the relay; live frames are advisory/visual.
- **v0.3:** authoritative re-sim of the input log on a canonical build; cross-platform checkpoint
  hashes are not compared (FP physics — spec Part 5/6). `wss://` + auth land in the bridge/relay,
  not the plugin.

## Offline

With no relay, the mod records locally and races a **ghost file** (`Race last recording` in the F6
menu) — the same data the live path streams.
