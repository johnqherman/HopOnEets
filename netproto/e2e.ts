//   fake mod (TCP) <-> bridge <-> WS <-> relay <-> WS <-> bridge <-> fake mod (TCP)
//   1) host/join by code (private)   2) ranked matchmaking queue
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
  const relay = startRelay(RELAY, () => {});
  const bridges: { close(): void }[] = [];
  const mk = (port: number, player: string) => bridges.push(startBridge({ relayPort: RELAY, modPort: port, player }));
  mk(38691, 'alice'); mk(38692, 'bob'); mk(38693, 'carol'); mk(38694, 'dave');
  await sleep(250);
  const A = fakeMod(38691), B = fakeMod(38692), C = fakeMod(38693), D = fakeMod(38694);
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

  // ready-gate: countdown only after both ready
  A.send('ready');
  await sleep(400);
  if (A.seen((l) => l.startsWith('countdown '))) fail('countdown fired before both ready');
  else ok('countdown gated on both ready');
  B.send('ready');
  const cdA = await A.expect((l) => l.startsWith('countdown '), 'A synced countdown');
  const cdB = await B.expect((l) => l.startsWith('countdown '), 'B synced countdown');
  if (cdA && cdB && cdA.startsWith('countdown 45') && cdB.startsWith('countdown 45')) ok('synced countdown both: ' + cdA);
  else if (cdA) fail('countdown wrong: ' + cdA + ' / ' + cdB);

  // build exchange: A's build reaches B as ghost items
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
  // anim state (emotion/motion/facing) rides along
  A.send('pos 12 130 50 a j 1');
  const ga = await B.expect((l) => l.startsWith('g 12 '), 'B sees A anim state');
  if (ga === 'g 12 130 50 a j 1') ok('opponent anim state relayed: ' + ga); else if (ga) fail('anim state wrong: ' + ga);

  // best-of-3: first to complete wins the round immediately (live race)
  A.send('finish 100 1 5');
  const r1 = await A.expect((l) => l.startsWith('result '), 'A round-1 result');
  if (r1 === 'result you completed 1 0') ok('round 1 -> A (series 1-0): ' + r1);
  else if (r1) fail('round-1 result wrong: ' + r1);
  A.send('finish 90 1 5');
  const so = await A.expect((l) => l.startsWith('series '), 'A series_over');
  if (so === 'series you 2 0 0 0 0 0') ok('best-of-3 -> A (2-0, private/no-elo): ' + so); else if (so) fail('series wrong: ' + so);
  const soB = await B.expect((l) => l.startsWith('series '), 'B series_over');
  if (soB === 'series opponent 0 2 0 0 0 0') ok('B sees series loss: ' + soB); else if (soB) fail('B series wrong: ' + soB);

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
  // hash mismatch -> D reports desync, relay voids for both
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

  // ---- mid-match drop -> reconnect window -> rejoin (not an instant loss) ----
  D.close();   // network drop (no forfeit): relay holds the match
  const od = await C.expect((l) => l.startsWith('oppdrop'), 'C sees opponent dropped', 2000);
  if (od && od.startsWith('oppdrop ')) ok('drop -> reconnect hold (not a loss): ' + od);
  else if (od) fail('oppdrop wrong: ' + od);
  // D reconnects same uuid -> reattach (bridge auto-sends hello on reconnect)
  await sleep(200);
  const D2 = fakeMod(38694);
  const rj = await D2.expect((l) => l.startsWith('rejoin '), 'D rejoins the held match', 3000);
  if (rj && rj.startsWith('rejoin ')) ok('reconnect -> rejoin: ' + rj); else if (rj) fail('rejoin wrong: ' + rj);
  const opr = await C.expect((l) => l === 'opprejoin', 'C sees opponent rejoined', 2000);
  if (opr) ok('opponent_rejoined delivered');
  const rr = await C.expect((l) => l.startsWith('round '), 'round restarts for C', 2000);
  if (rr) ok('round restarts after reconnect: ' + rr);
  D2.close();

  [A, B, C].forEach((m) => m.close());
  bridges.forEach((b) => b.close());
  relay.close();
  await sleep(120);
  console.log(failed ? '\nE2E FAILED' : '\nE2E PASSED');
  process.exit(failed ? 1 : 0);
})();
