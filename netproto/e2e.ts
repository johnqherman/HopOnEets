// End-to-end test of the real-time path for BOTH connect modes:
//   fake mod (TCP) <-> bridge <-> WS <-> relay <-> WS <-> bridge <-> fake mod (TCP)
//   1) host/join by code (private)   2) ranked matchmaking queue
// Proves: code host/join, queue pairing, REAL-TIME position relay, finish -> provisional result.
import * as net from 'net';
import { startRelay } from './relay';
import { startBridge } from './bridge';

const RELAY = 38590;
let failed = false;
const fail = (m: string) => { failed = true; console.error('FAIL:', m); };
const ok = (m: string) => console.log('ok -', m);
const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

interface Mod {
  send(s: string): void;
  expect(pred: (l: string) => boolean, label: string, ms?: number): Promise<string | null>;
  seen(pred: (l: string) => boolean): boolean;
  close(): void;
}

function fakeMod(port: number): Mod {
  const sock = net.connect(port, '127.0.0.1');
  const lines: string[] = [];
  const waiters: { pred: (l: string) => boolean; resolve: (l: string | null) => void }[] = [];
  sock.on('data', (d) => {
    for (const ln of d.toString('utf8').split('\n')) {
      if (!ln) continue;
      lines.push(ln);
      for (let i = waiters.length - 1; i >= 0; i--) if (waiters[i].pred(ln)) { waiters[i].resolve(ln); waiters.splice(i, 1); }
    }
  });
  return {
    send: (s) => sock.write(s + '\n'),
    expect: (pred, label, ms = 2000) => new Promise<string | null>((resolve) => {
      const hit = lines.find(pred); if (hit) return resolve(hit);
      const w = { pred, resolve }; waiters.push(w);
      setTimeout(() => { if (waiters.includes(w)) { fail('timeout: ' + label); resolve(null); } }, ms);
    }),
    seen: (pred) => lines.some(pred),
    close: () => sock.end(),
  };
}

(async () => {
  // fake authoritative verifier: records calls, returns a canned decided result (transport test only)
  const verifyCalls: { mid: string; round: number; aLog: string; bLog: string }[] = [];
  const fakeVerify = async (mid: string, round: number, a: { player: string; log: string }, b: { player: string; log: string }) => {
    verifyCalls.push({ mid, round, aLog: a.log, bLog: b.log });
    return { kind: 'decided' as const, winner: a.player, reason: 'finish_tick', detail: {} };
  };
  const relay = startRelay(RELAY, () => {}, { verify: fakeVerify });
  const bridges: { close(): void }[] = [];
  const mk = (port: number, player: string) => bridges.push(startBridge({ relayPort: RELAY, modPort: port, player }));
  mk(38691, 'alice'); mk(38692, 'bob'); mk(38693, 'carol'); mk(38694, 'dave'); mk(38695, 'erin'); mk(38696, 'finn');
  await sleep(250);
  const A = fakeMod(38691), B = fakeMod(38692), C = fakeMod(38693), D = fakeMod(38694), E = fakeMod(38695), F = fakeMod(38696);
  await sleep(120);

  // ---- 1) private host/join by code ----
  A.send('hello alice'); B.send('hello bob');
  A.send('host');
  const code = await A.expect((l) => l.startsWith('code '), 'A host code');
  if (code) ok('host code issued: ' + code);
  const c = code ? code.split(' ')[1] : 'ZZZZZZ';
  B.send('join ' + c);
  const ma = await A.expect((l) => l.startsWith('match '), 'A match (private)');
  const mb = await B.expect((l) => l.startsWith('match '), 'B match (private)');
  if (ma && mb) ok('private match: ' + ma);
  if (ma && ma.split(' ')[3] !== '0') fail('private match should be ranked=0: ' + ma);
  if (ma && parseInt(ma.split(' ')[4], 10) >= 0) ok('match carries level index: #' + ma.split(' ')[4]);
  else if (ma) fail('match missing level index: ' + ma);
  if (ma && parseInt(ma.split(' ')[5], 10) > 0) ok('match carries per-match seed: ' + ma.split(' ')[5]);
  else if (ma) fail('match missing seed: ' + ma);

  // ready-gate: countdown only after BOTH report ready (level loaded)
  A.send('ready');
  await sleep(400);
  if (A.seen((l) => l.startsWith('countdown '))) fail('countdown fired before both ready');
  else ok('countdown gated on both ready');
  B.send('ready');
  const cdA = await A.expect((l) => l.startsWith('countdown '), 'A synced countdown');
  const cdB = await B.expect((l) => l.startsWith('countdown '), 'B synced countdown');
  if (cdA && cdB && cdA.startsWith('countdown 45') && cdB.startsWith('countdown 45')) ok('synced countdown both: ' + cdA);
  else if (cdA) fail('countdown wrong: ' + cdA + ' / ' + cdB);

  // build exchange: A's locked-in build reaches B as ghost items
  A.send('build marshmallow 100 200'); A.send('buildend');
  const ob = await B.expect((l) => l.startsWith('ob '), 'B sees A build');
  if (ob === 'ob marshmallow 100 200') ok('build relay A->B: ' + ob); else if (ob) fail('build wrong: ' + ob);
  if (await B.expect((l) => l === 'obend', 'B sees buildend')) ok('buildend relay');

  A.send('pos 10 123 45');
  const g1 = await B.expect((l) => l.startsWith('g '), 'B sees A pos');
  if (g1 === 'g 10 123 45 h w 0') ok('realtime A->B: ' + g1); else if (g1) fail('A->B wrong: ' + g1);
  B.send('pos 20 300 60');
  const g2 = await A.expect((l) => l.startsWith('g '), 'A sees B pos');
  if (g2 === 'g 20 300 60 h w 0') ok('realtime B->A: ' + g2); else if (g2) fail('B->A wrong: ' + g2);
  // anim state (emotion/motion/facing) rides along so the ghost mirrors the opponent's Eets
  A.send('pos 12 130 50 a j 1');
  const ga = await B.expect((l) => l.startsWith('g 12 '), 'B sees A anim state');
  if (ga === 'g 12 130 50 a j 1') ok('opponent anim state relayed: ' + ga); else if (ga) fail('anim state wrong: ' + ga);

  // best-of-3: first to COMPLETE the level wins the round immediately (live race - no waiting for both)
  A.send('finish 100 1 5');   // A completes -> wins round 1
  const r1 = await A.expect((l) => l.startsWith('result '), 'A round-1 result');
  if (r1 === 'result you completed 1 0') ok('round 1 -> A (series 1-0): ' + r1);
  else if (r1) fail('round-1 result wrong: ' + r1);
  // round 2: A completes again -> best-of-3 decided
  A.send('finish 90 1 5');
  const so = await A.expect((l) => l.startsWith('series '), 'A series_over');
  if (so === 'series you 2 0 0 0 0') ok('best-of-3 -> A (2-0, private/no-elo): ' + so); else if (so) fail('series wrong: ' + so);
  const soB = await B.expect((l) => l.startsWith('series '), 'B series_over');
  if (soB === 'series opponent 0 2 0 0 0') ok('B sees series loss: ' + soB); else if (soB) fail('B series wrong: ' + soB);

  // ---- 2) ranked matchmaking queue ----
  C.send('hello carol'); D.send('hello dave');
  C.send('queue'); D.send('queue');
  const mc = await C.expect((l) => l.startsWith('match '), 'C match (ranked)');
  const md = await D.expect((l) => l.startsWith('match '), 'D match (ranked)');
  if (mc && md) ok('ranked match: ' + mc);
  if (mc && mc.split(' ')[3] !== '1') fail('ranked match should be ranked=1: ' + mc);
  C.send('pos 5 7 9');
  const g3 = await D.expect((l) => l.startsWith('g '), 'D sees C pos');
  if (g3 === 'g 5 7 9 h w 0') ok('realtime C->D: ' + g3); else if (g3) fail('C->D wrong: ' + g3);

  // ---- checkpoint hash relay + desync = no-contest ----
  C.send('hash 60 aaaaaaaaaaaaaaaa linux64');
  const oh = await D.expect((l) => l.startsWith('oh '), 'D sees C checkpoint hash');
  if (oh === 'oh 60 aaaaaaaaaaaaaaaa linux64') ok('checkpoint hash relay C->D: ' + oh);
  else if (oh) fail('hash relay wrong: ' + oh);
  // D's local hash differs -> D reports the desync; the relay voids the match for BOTH
  D.send('desync 60');
  const ncC = await C.expect((l) => l.startsWith('nocontest '), 'C no-contest', 2000);
  const ncD = await D.expect((l) => l.startsWith('nocontest '), 'D no-contest', 2000);
  if (ncC && ncC.startsWith('nocontest desync')) ok('desync -> no-contest (C): ' + ncC);
  else if (ncC) fail('no-contest wrong: ' + ncC);
  if (ncD) ok('both clients see no-contest');

  // ---- bad join code ----
  C.send('join NOPEXX');
  const jf = await C.expect((l) => l.startsWith('joinfail '), 'bad join rejected', 1500);
  if (jf) ok('bad code rejected: ' + jf);

  // ---- disconnect = loss: D quits mid-match -> C wins the series ----
  D.close();
  const dl = await C.expect((l) => l === 'oppleft', 'C sees opponent left', 2000);
  if (dl) ok('opponent_left delivered: ' + dl);
  const dsv = await C.expect((l) => l.startsWith('series '), 'C series_over on disconnect', 2000);
  if (dsv === 'series you 0 0 1 1000 1016') ok('disconnect = ranked series win + elo for C: ' + dsv);
  else if (dsv) fail('disconnect series wrong: ' + dsv);

  // ---- ranked: authoritative re-sim handoff (relay -> verifier) ----
  E.send('hello erin'); F.send('hello finn');
  E.send('queue'); F.send('queue');
  await E.expect((l) => l.startsWith('match '), 'E ranked match');
  await F.expect((l) => l.startsWith('match '), 'F ranked match');
  E.send('replay 1 linux64 eyJhIjoxfQ=='); F.send('replay 1 win32 eyJiIjoyfQ==');   // base64 input logs (no spaces)
  E.send('finish 100 1 5'); F.send('finish 120 1 6');
  const auth = await E.expect((l) => l.startsWith('auth '), 'E authoritative result', 2500);
  if (auth && auth.startsWith('auth decided ')) ok('ranked authoritative handoff: ' + auth);
  else if (auth) fail('authoritative wrong: ' + auth);
  if (verifyCalls.length > 0 && verifyCalls[0].aLog && verifyCalls[0].bLog) ok('verifier received both submitted logs');
  else fail('verifier not called with both logs');

  [A, B, C, E, F].forEach((m) => m.close());
  bridges.forEach((b) => b.close());
  relay.close();
  await sleep(120);
  console.log(failed ? '\nE2E FAILED' : '\nE2E PASSED');
  process.exit(failed ? 1 : 0);
})();
