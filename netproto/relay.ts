// Hop On Eets relay server (v0.2). Zero runtime deps; WebSocket. Pairs two players and relays
// REAL-TIME position frames between them (plus build/finish), then reports a provisional result.
// Two ways in: private host/join by code, or ranked matchmaking queue. The server does not
// simulate; authoritative cross-platform re-sim is v0.3 (see docs/hop-on-eets-spec.md Part 6).
import * as http from 'http';
import type * as net from 'net';
import * as fs from 'fs';
import * as ws from './ws';
import { modLineToMsg, msgToModLine } from './modproto';   // mod text protocol <-> relay JSON (direct-connect clients)
import type { RankedVerifier } from './verifier';   // type-only (verifier imports decideOutcome from here)

export interface Finish { completed: boolean; finish_tick: number; items_used: number; deaths?: number; }
interface Peer { id: string; name: string; match: string | null; opp: ws.WSConn | null; finish: Finish | null; hostCode: string | null; wins: number; ready: boolean; noContest: boolean; ranked: boolean; platform: string; log: string | null; roundTimer: ReturnType<typeof setTimeout> | null; }
// id = a stable client-generated UUID (the Elo key; names are mutable/spoofable so they must NOT key Elo).
// name = the display name (the player's profile name), shown to the opponent but never used for identity.
export interface RelayOpts { verify?: RankedVerifier; }   // when set, ranked rounds are re-sim-verified (authoritative)
const BEST_OF = 3, WINS_NEEDED = 2;   // first to 2 round wins takes the series

// ---- ELO ladder (ranked only). Persisted to a JSON file on the relay host (the VPS); keyed by player
// name (informal - names aren't authenticated). Standard Elo, K=32, default 1000. ----
const ELO_FILE = process.env.ELO_FILE || 'elo.json', ELO_DEFAULT = 1000, ELO_K = 32;
let eloStore: Record<string, number> = {};
try { eloStore = JSON.parse(fs.readFileSync(ELO_FILE, 'utf8')); } catch { /* first run: empty */ }
const getElo = (id: string): number => eloStore[id] ?? ELO_DEFAULT;
function saveElo(): void { try { fs.writeFileSync(ELO_FILE, JSON.stringify(eloStore, null, 2)); } catch (e) { console.error('[relay] elo save failed:', e); } }
// apply a ranked series result and persist; returns both players' old/new ratings
function applyElo(winnerId: string, loserId: string) {
  const wOld = getElo(winnerId), lOld = getElo(loserId);
  const expW = 1 / (1 + Math.pow(10, (lOld - wOld) / 400));
  const wNew = Math.round(wOld + ELO_K * (1 - expW)), lNew = Math.round(lOld - ELO_K * (1 - expW));
  eloStore[winnerId] = wNew; eloStore[loserId] = lNew; saveElo();
  return { wOld, wNew, lOld, lNew };
}
export type Verdict = { winner: 'you' | 'opponent' | 'tie'; reason: string };

// tiebreakers: completion, then (completers only) finish_tick, items_used (spec Part 3). Top-level +
// exported so the authoritative verifier (verifier.ts) decides re-sim outcomes with the EXACT same rule as
// the relay. If NEITHER player solves the puzzle the round is a draw - a non-solver must never win on
// finish_tick/items_used, so those tiebreakers only apply when both completed. A draw scores no round win;
// the relay replays the round (next countdown) until someone solves it.
export function decideOutcome(a: Finish, b: Finish): { self: Verdict; opp: Verdict } {
  let win: boolean | null; let reason: string;
  if (a.completed !== b.completed) { win = a.completed; reason = 'completion'; }
  else if (!a.completed) { win = null; reason = 'both_failed'; }   // neither solved -> draw, replay the round
  else if (a.finish_tick !== b.finish_tick) { win = a.finish_tick < b.finish_tick; reason = 'finish_tick'; }
  else if ((a.deaths ?? 0) !== (b.deaths ?? 0)) { win = (a.deaths ?? 0) < (b.deaths ?? 0); reason = 'deaths'; }   // tie on time -> fewer deaths wins
  else if (a.items_used !== b.items_used) { win = a.items_used < b.items_used; reason = 'items_used'; }
  else { win = null; reason = 'tie'; }
  const self: Verdict = { winner: win === null ? 'tie' : (win ? 'you' : 'opponent'), reason };
  const opp: Verdict = { winner: win === null ? 'tie' : (win ? 'opponent' : 'you'), reason };
  return { self, opp };
}

export function startRelay(port: number, log: (s: string) => void = () => {}, opts: RelayOpts = {}): http.Server {
  let queue: ws.WSConn[] = [];
  let nextMatch = 1;
  const BUILD_SECONDS = 45;    // synced build phase; both clients start their timer on `countdown`
  const ROUND_CAP_SECONDS = 180;   // total round wall-clock from countdown (build + play + retries); relay-authoritative + broadcast so both clients show an aligned clock. On expiry, unfinished peers are DNF'd.
  const peers = new Map<ws.WSConn, Peer>();
  const rooms = new Map<string, ws.WSConn>();

  function makeCode(): string {
    const al = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; // no ambiguous 0/O/1/I
    let c: string;
    do { c = ''; for (let i = 0; i < 6; i++) c += al[Math.floor(Math.random() * al.length)]; } while (rooms.has(c));
    return c;
  }

  const pickLevel = () => Math.floor(Math.random() * 1000);          // both clients resolve `level % poolSize` -> same level
  const pickSeed  = () => 1 + Math.floor(Math.random() * 0x7ffffffe); // RNG seed valid for both engines' generators

  function pair(a: ws.WSConn, b: ws.WSConn, ranked: boolean): void {
    const match = 'hoe_' + String(nextMatch++).padStart(6, '0');
    const A = peers.get(a)!, B = peers.get(b)!;
    A.match = match; A.opp = b; A.finish = null; A.wins = 0; A.ready = false; A.noContest = false; A.ranked = ranked; A.log = null;
    B.match = match; B.opp = a; B.finish = null; B.wins = 0; B.ready = false; B.noContest = false; B.ranked = ranked; B.log = null;
    const level = pickLevel(), seed = pickSeed();   // round 1's level + seed (every round picks fresh - see startRound)
    const cfg = (selfName: string, oppName: string, selfElo: number, oppElo: number) => ({
      type: 'match_config', match_id: match, mode: 'solution_race',
      ruleset: ranked ? 'ranked_v0' : 'casual', ranked, level, seed,
      tick_rate: 60, self: selfName, opponent: oppName, self_elo: selfElo, opp_elo: oppElo,
    });
    const eA = ranked ? getElo(A.id) : 0, eB = ranked ? getElo(B.id) : 0;
    a.send(cfg(A.name, B.name, eA, eB));   // display names + current Elo; identity (uuid) stays server-side
    b.send(cfg(B.name, A.name, eB, eA));
    // countdown is sent once both clients report `ready` (level loaded) - see the ready handler
    log(`match ${match} (${ranked ? 'ranked' : 'private'}): ${A.name} vs ${B.name} (round 1 level ${level})`);
  }

  // start the next round of an ongoing series: pick a FRESH level + seed (each round is a different level),
  // reset the ready flags, and tell both clients to load it. The synced countdown then fires from the ready
  // handler once both have re-loaded. (Round 1 is bootstrapped by pair()/match_config above.)
  function startRound(a: ws.WSConn, b: ws.WSConn, round: number): void {
    const A = peers.get(a), B = peers.get(b); if (!A || !B) return;
    A.ready = false; B.ready = false; A.finish = null; B.finish = null;
    const level = pickLevel(), seed = pickSeed();
    const msg = { type: 'round', round, level, seed };
    a.send(msg); b.send(msg);
    log(`round ${round} ${A.match}: level ${level}`);
  }

  function tryMatch(): void {
    while (queue.length >= 2) { const a = queue.shift()!, b = queue.shift()!; pair(a, b, true); }
  }

  function relay(conn: ws.WSConn, msg: unknown): void {
    const p = peers.get(conn); if (!p || !p.opp) return;
    p.opp.send(msg);
  }

  function clearRoundTimer(p: Peer | undefined): void {
    if (p && p.roundTimer) { clearTimeout(p.roundTimer); p.roundTimer = null; }
  }
  // the round's wall-clock ran out: DNF anyone who hasn't finished, then score (relay-authoritative cap).
  function onRoundTimeout(a: ws.WSConn, b: ws.WSConn): void {
    const A = peers.get(a), B = peers.get(b);
    if (!A || !B || A.opp !== b) return;   // match still valid?
    const dnf: Finish = { completed: false, finish_tick: 1 << 30, items_used: 0, deaths: 9999 };
    if (!A.finish) A.finish = dnf;
    if (!B.finish) B.finish = dnf;
    log(`round timeout ${A.match}: cap reached, DNF unfinished`);
    maybeResult(a);
  }

  function sendCountdown(a: ws.WSConn, b: ws.WSConn): void {   // synced build phase start (both align to this)
    const A = peers.get(a), B = peers.get(b);
    a.send({ type: 'countdown', seconds: BUILD_SECONDS, cap: ROUND_CAP_SECONDS });
    b.send({ type: 'countdown', seconds: BUILD_SECONDS, cap: ROUND_CAP_SECONDS });
    // relay-authoritative round clock: both clients align their cap display to this countdown; the relay
    // backstops it so a stalled/disconnected client can't hang the round.
    clearRoundTimer(A); clearRoundTimer(B);
    // cap clock runs only AFTER the build phase: fire BUILD + CAP after the countdown signal
    const t = setTimeout(() => onRoundTimeout(a, b), (BUILD_SECONDS + ROUND_CAP_SECONDS) * 1000);
    if (typeof (t as { unref?: () => void }).unref === 'function') (t as { unref: () => void }).unref();
    if (A) A.roundTimer = t;
    if (B) B.roundTimer = t;
  }

  const decide = decideOutcome;   // relay scores rounds with the same rule the verifier uses

  function maybeResult(conn: ws.WSConn): void {
    const p = peers.get(conn); if (!p || !p.opp) return;
    const q = peers.get(p.opp); if (!q || !p.finish) return;
    if (p.noContest || q.noContest) {   // desynced match: report the round, score nothing
      conn.send({ type: 'no_contest', reason: 'desync' });
      p.opp.send({ type: 'no_contest', reason: 'desync' });
      p.finish = null; q.finish = null;
      log(`no contest ${p.match}: round void (desync)`);
      return;
    }
    // First to COMPLETE the level wins the round immediately - don't wait for the opponent (a live race).
    // A failure waits: the opponent may still complete (and win) or also fail (both-failed -> draw).
    let r: { self: Verdict; opp: Verdict };
    if (p.finish.completed) r = { self: { winner: 'you', reason: 'completed' }, opp: { winner: 'opponent', reason: 'completed' } };
    else if (q.finish && q.finish.completed) r = { self: { winner: 'opponent', reason: 'completed' }, opp: { winner: 'you', reason: 'completed' } };
    else if (q.finish) r = decide(p.finish, q.finish);   // both finished, neither completed -> both_failed (draw)
    else return;   // we failed but the opponent is still racing -> wait
    clearRoundTimer(p); clearRoundTimer(q);   // round resolved -> stop the cap clock
    if (r.self.winner === 'you') p.wins++; else if (r.self.winner === 'opponent') q.wins++;
    conn.send({ type: 'result', ...r.self, you_wins: p.wins, opp_wins: q.wins, provisional: true });
    p.opp.send({ type: 'result', ...r.opp, you_wins: q.wins, opp_wins: p.wins, provisional: true });
    p.finish = null; q.finish = null;                 // ready for the next round
    log(`result ${p.match}: ${r.self.winner} (series ${p.wins}-${q.wins})`);
    if (p.ranked && opts.verify && p.log && q.log) {   // hand the round to the authoritative re-sim (async)
      const round = p.wins + q.wins, cA = conn, cB = p.opp, mid = p.match!;
      const subA = { player: p.id, platform: p.platform, log: p.log };
      const subB = { player: q.id, platform: q.platform, log: q.log };
      p.log = null; q.log = null;
      opts.verify(mid, round, subA, subB).then((off) => {
        const msg = { type: 'authoritative', kind: off.kind, winner: off.winner ?? '', reason: off.reason, round };
        cA.send(msg); cB.send(msg);
        log(`authoritative ${mid} r${round}: ${off.kind} ${off.winner ?? '-'} (${off.reason})`);
      }).catch((e) => log(`verify error ${mid}: ${e}`));
    }
    if (p.wins >= WINS_NEEDED || q.wins >= WINS_NEEDED) {   // best-of-3 decided
      const pWon = p.wins >= WINS_NEEDED;
      let pE = { old: 0, neu: 0 }, qE = { old: 0, neu: 0 };
      if (p.ranked) {   // ranked: adjust + persist Elo (winner gains, loser loses)
        const r = pWon ? applyElo(p.id, q.id) : applyElo(q.id, p.id);
        pE = pWon ? { old: r.wOld, neu: r.wNew } : { old: r.lOld, neu: r.lNew };
        qE = pWon ? { old: r.lOld, neu: r.lNew } : { old: r.wOld, neu: r.wNew };
        log(`elo ${p.match}: ${p.name} ${pE.old}->${pE.neu}, ${q.name} ${qE.old}->${qE.neu}`);
      }
      conn.send({ type: 'series_over', winner: pWon ? 'you' : 'opponent', you_wins: p.wins, opp_wins: q.wins, ranked: p.ranked, elo_old: pE.old, elo_new: pE.neu });
      p.opp.send({ type: 'series_over', winner: pWon ? 'opponent' : 'you', you_wins: q.wins, opp_wins: p.wins, ranked: p.ranked, elo_old: qE.old, elo_new: qE.neu });
      log(`series over ${p.match}: ${pWon ? p.name : q.name} (best of ${BEST_OF})`);
      clearRoundTimer(p); clearRoundTimer(q);
      p.wins = 0; q.wins = 0;
      p.match = null; p.opp = null; q.match = null; q.opp = null;   // series done: free both to re-queue after the win screen
    } else {
      startRound(conn, p.opp, p.wins + q.wins + 1);   // series continues: load a fresh level, ready-gate, then countdown
    }
  }

  const server = http.createServer((_req, res) => { res.writeHead(426); res.end('upgrade required'); });
  server.on('upgrade', (req, socket) => {
    const conn = ws.accept(req, socket as net.Socket);
    peers.set(conn, { id: '?', name: '?', match: null, opp: null, finish: null, hostCode: null, wins: 0, ready: false, noContest: false, ranked: false, platform: '', log: null, roundTimer: null });
    const dispatch = (m: any) => {
      const p = peers.get(conn); if (!p) return;
      switch (m.type) {
        case 'hello':
          p.id = String(m.uuid ?? m.player_id ?? 'anon'); p.name = String(m.player_id ?? p.id);
          conn.send({ type: 'elo', value: getElo(p.id) });   // tell the client its current ranked rating (for the F6 menu)
          break;
        case 'host': {
          if (p.hostCode) rooms.delete(p.hostCode);
          const code = makeCode(); p.hostCode = code; rooms.set(code, conn);
          conn.send({ type: 'room', code });
          log(`host ${p.name} room ${code}`);
          break;
        }
        case 'join': {
          const code = String(m.code ?? '').toUpperCase();
          const host = rooms.get(code);
          if (!host || host === conn) { conn.send({ type: 'join_failed', code }); break; }
          rooms.delete(code); peers.get(host)!.hostCode = null;
          pair(host, conn, false);
          break;
        }
        case 'queue': if (!queue.includes(conn)) { queue.push(conn); tryMatch(); } break;
        case 'ready': {   // both clients loaded the level -> start the synced countdown
          const r = peers.get(conn); if (!r || !r.opp) break;
          r.ready = true;
          const o = peers.get(r.opp);
          if (o && o.ready) { sendCountdown(conn, r.opp); r.ready = false; o.ready = false; }
          break;
        }
        case 'pos':   relay(conn, { type: 'opp_pos', tick: m.tick, x: m.x, y: m.y, emo: m.emo, mot: m.mot, flip: m.flip }); break;
        case 'build': relay(conn, { type: 'opp_build', name: m.name, x: m.x, y: m.y }); break;
        case 'buildend': relay(conn, { type: 'opp_buildend' }); break;
        case 'hash':  relay(conn, { type: 'opp_hash', tick: m.tick | 0, hash: String(m.hash ?? ''), platform: String(m.platform ?? '') }); break;
        case 'desync': {   // a client detected a same-platform divergence -> the whole match is no-contest
          const q = p.opp ? peers.get(p.opp) : null;
          p.noContest = true; if (q) q.noContest = true;
          const payload = { type: 'no_contest', reason: 'desync', tick: m.tick | 0 };
          conn.send(payload); if (p.opp) p.opp.send(payload);
          log(`no contest ${p.match}: desync @${m.tick | 0}`);
          break;
        }
        case 'submit_replay':   // client uploads its input log (base64) for authoritative re-sim
          p.platform = String(m.platform ?? p.platform); p.log = String(m.log ?? '');
          break;
        case 'finish':
          p.finish = { completed: !!m.completed, finish_tick: m.finish_tick | 0, items_used: m.items_used | 0, deaths: m.deaths | 0 };
          relay(conn, { type: 'opp_finish', ...p.finish });
          maybeResult(conn);
          break;
      }
    };
    // The native mod connects DIRECTLY over wss speaking its text protocol; browsers/dev tools speak JSON.
    // Decide per-connection from the first frame: a leading '{' = JSON, else mod-text. For a mod client we
    // route each text frame through modLineToMsg and override send() to emit mod-text, so all the relay's
    // internal `conn.send({type:...})` calls work unchanged for both client kinds.
    let decided = false, rawMod = false;
    conn.onText((s: string) => {
      if (!decided) {
        decided = true; rawMod = !s.trimStart().startsWith('{');
        if (rawMod) conn.send = (obj: any) => { const ln = msgToModLine(obj); if (ln !== null) conn.sendText(ln); };
      }
      if (rawMod) { const m = modLineToMsg(s); if (m) dispatch(m); }
      else { let m: any; try { m = JSON.parse(s); } catch { return; } dispatch(m); }
    });
    conn.onClose(() => {
      const p = peers.get(conn); if (!p) return;
      clearRoundTimer(p);
      queue = queue.filter((c) => c !== conn);
      if (p.hostCode) rooms.delete(p.hostCode);
      if (p.opp) {
        const q = peers.get(p.opp);
        clearRoundTimer(q);
        p.opp.send({ type: 'opponent_left' });
        if (p.match && q) {   // disconnect/forfeit = a series loss for the leaver (Elo still applies on ranked)
          let qE = { old: 0, neu: 0 };
          if (q.ranked) { const r = applyElo(q.id, p.id); qE = { old: r.wOld, neu: r.wNew }; log(`elo forfeit ${p.match}: ${q.name} ${qE.old}->${qE.neu}, ${p.name} ${r.lOld}->${r.lNew}`); }
          p.opp.send({ type: 'series_over', winner: 'you', you_wins: q.wins, opp_wins: p.wins, ranked: q.ranked, elo_old: qE.old, elo_new: qE.neu, forfeit: true });
        }
        if (q) { q.opp = null; q.match = null; q.wins = 0; }
      }
      peers.delete(conn);
    });
  });
  server.listen(port, () => log(`relay listening on ws://0.0.0.0:${port}`));
  return server;
}

if (require.main === module) {
  const port = parseInt(process.env.PORT || '38500', 10);
  const opts: RelayOpts = {};
  if (process.env.EETS_DIR) {   // production: re-sim ranked rounds on the canonical build at EETS_DIR
    // eslint-disable-next-line @typescript-eslint/no-var-requires
    const { makeVerifier, ShellResimRunner } = require('./verifier');
    const fs = require('fs'), os = require('os'), path = require('path');
    const save = (mid: string, round: number, player: string, log: string) => {
      const f = path.join(os.tmpdir(), `hoe_${mid}_r${round}_${player}.json`);
      fs.writeFileSync(f, Buffer.from(log, 'base64'));   // logs arrive base64-encoded
      return f;
    };
    opts.verify = makeVerifier(new ShellResimRunner(), save);
    console.log('[relay] authoritative verifier ENABLED (EETS_DIR=' + process.env.EETS_DIR + ')');
  }
  startRelay(port, (s) => console.log('[relay]', s), opts);
}
