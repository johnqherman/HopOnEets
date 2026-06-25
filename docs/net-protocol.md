# Hop On Eets — Network Protocol (v0.2, implemented)

Two hops, so the 32-bit native plugin needs no WebSocket/TLS library:

```
mod  <-- localhost TCP, newline text -->  bridge  <-- WebSocket JSON -->  relay  <-- ... -->  opponent
```

The realtime leg (bridge ↔ relay) is a WebSocket. The relay pairs two players and **relays live
frames** (position, build) between them in real time — it does not simulate; it scores the series
from the players' reported finishes. Reference impl + e2e test: `netproto/`.

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
| `forfeit` | C→S | — | intentional leave; the following disconnect awards the opponent immediately |
| `result` | S→C | `winner, reason, you_wins, opp_wins` | round outcome + series score |
| `series_over` | S→C | `winner, you_wins, opp_wins, ranked, elo_old, elo_new, forfeit` | best-of-3 decided (or awarded on disconnect/forfeit) |
| `opponent_dropped` | S→C | `seconds` | opponent dropped; match held open for a reconnect window |
| `opponent_rejoined` | S→C | — | opponent reconnected within the window; the round restarts |
| `rejoin` | S→C | `opponent, ranked, you_wins, opp_wins` | you reconnected; resume the held match |
| `opponent_left` | S→C | — | opponent gone (forfeit, or reconnect window expired) → `series_over` for whoever remains |

Tiebreakers (relay `decide`): completion → finish_tick → deaths → items_used (spec Part 3).
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
                forfeit | finish <tick> <0|1> <items>
bridge -> mod:  code <CODE> | joinfail <CODE> | match <id> <opp> <ranked0|1> <level> <seed> |
                countdown <secs> | ob <name> <x> <y> | obend | g <tick> <x> <y> |
                oh <tick> <hex> <platform> | nocontest <reason> <tick> |
                oppfin <tick> <0|1> <items> | result <winner> <reason> <yw> <ow> |
                series <winner> <yw> <ow> <ranked> <eloOld> <eloNew> <forfeit> |
                oppdrop <secs> | opprejoin | rejoin <opp> <ranked> <yw> <ow> | oppleft
```

The bridge buffers mod lines until its WebSocket is up, so the mod can connect and queue
immediately.

## Validation status

- Results come from the players' reported finishes; the relay is authoritative for the series score.
  Live frames are advisory/visual. **Same-platform** desync detection is live via checkpoint hashes
  (mismatch ⇒ no-contest). Cross-platform checkpoint hashes are never compared — FP physics legitimately
  differs (spec Part 5).

## Offline

With no relay, the mod records locally and races a **ghost file** (`Race last recording` in the F6
menu) — the same data the live path streams.
