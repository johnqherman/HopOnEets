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
| `match_config` | S→C | `match_id, ranked, level, seed, tick_rate, self, opponent` | paired; both resolve `level % poolSize` to the same level; `seed` pins the RNG |
| `ready` | C→S | — | level loaded; relay sends `countdown` only once BOTH are ready |
| `countdown` | S→C | `seconds` | start the synced build phase; both clients align their build timer to this |
| `pos` / `opp_pos` | C→S / S→C | `tick, x, y` | live Eets position (relayed to opponent) |
| `build` / `opp_build` | C→S / S→C | `name, x, y` | one locked-in build item |
| `buildend` / `opp_buildend` | C→S / S→C | — | end of the build set |
| `hash` / `opp_hash` | C→S / S→C | `tick, hash, platform` | checkpoint state-hash (same-platform desync detection) |
| `desync` | C→S | `tick` | client detected a same-platform hash mismatch |
| `no_contest` | S→C | `reason, tick` | match voided (desync); score withheld |
| `finish` / `opp_finish` | C→S / S→C | `finish_tick, completed, items_used` | round finished |
| `submit_replay` | C→S | `round, platform, log` | ranked: upload the input log (base64) for authoritative re-sim |
| `result` | S→C | `winner, reason, you_wins, opp_wins, provisional` | provisional round outcome + series score |
| `authoritative` | S→C | `kind, winner, reason, round` | official result from the re-sim verifier (overrides provisional) |
| `series_over` | S→C | `winner, you_wins, opp_wins` | best-of-3 decided (or awarded on disconnect) |
| `opponent_left` | S→C | — | opponent disconnected (→ `series_over` win for whoever remains) |

Tiebreakers (relay `decide`): completion → finish_tick → items_used (spec Part 3).
The relay is authoritative for the series score; best-of-3 (`WINS_NEEDED = 2`).

**Checkpoint hashes:** clients sample a state hash every `hash_interval` sim ticks and stream it
with their `platform`. The opponent compares **only same-platform** hashes at the same tick
(cross-platform FP physics legitimately differs — spec Part 5). A mismatch ⇒ the client sends
`desync`; the relay marks the match `no_contest` for both and stops scoring it. The mod writes a
diagnostic (`Log/hop_on_eets_desync_*.json`: tick, platform, seed, both hashes).

## Mod ↔ bridge (localhost TCP, newline text)

```
mod -> bridge:  hello <id> | host | join <CODE> | queue | ready | build <name> <x> <y> | buildend |
                pos <tick> <x> <y> | hash <tick> <hex> <platform> | desync <tick> |
                replay <round> <platform> <base64log> | finish <tick> <0|1> <items>
bridge -> mod:  code <CODE> | joinfail <CODE> | match <id> <opp> <ranked0|1> <level> <seed> |
                countdown <secs> | ob <name> <x> <y> | obend | g <tick> <x> <y> |
                oh <tick> <hex> <platform> | nocontest <reason> <tick> |
                oppfin <tick> <0|1> <items> | result <winner> <reason> <yw> <ow> |
                series <winner> <yw> <ow> | auth <kind> <winner> <reason> | oppleft
```

The bridge buffers mod lines until its WebSocket is up, so the mod can connect and queue
immediately.

## Validation status

- **v0.2 (now):** provisional results from the relay; live frames are advisory/visual.
  **Same-platform** desync detection is live via checkpoint hashes (mismatch ⇒ no-contest).
- **v0.3:** authoritative re-sim of the input log on a canonical build (the only valid
  **cross-platform** truth; cross-platform checkpoint hashes are never compared — FP physics, spec
  Part 5/6). `wss://` + auth land in the bridge/relay, not the plugin.

## Offline

With no relay, the mod records locally and races a **ghost file** (`Race last recording` in the F6
menu) — the same data the live path streams.
