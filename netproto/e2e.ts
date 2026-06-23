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
    close: () => sock.end(),
  };
}

(async () => {
  const relay = startRelay(RELAY);
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
  if (ma && !ma.endsWith(' 0')) fail('private match should be ranked=0: ' + ma);

  // build exchange: A's locked-in build reaches B as ghost items
  A.send('build marshmallow 100 200'); A.send('buildend');
  const ob = await B.expect((l) => l.startsWith('ob '), 'B sees A build');
  if (ob === 'ob marshmallow 100 200') ok('build relay A->B: ' + ob); else if (ob) fail('build wrong: ' + ob);
  if (await B.expect((l) => l === 'obend', 'B sees buildend')) ok('buildend relay');

  A.send('pos 10 123 45');
  const g1 = await B.expect((l) => l.startsWith('g '), 'B sees A pos');
  if (g1 === 'g 10 123 45') ok('realtime A->B: ' + g1); else if (g1) fail('A->B wrong: ' + g1);
  B.send('pos 20 300 60');
  const g2 = await A.expect((l) => l.startsWith('g '), 'A sees B pos');
  if (g2 === 'g 20 300 60') ok('realtime B->A: ' + g2); else if (g2) fail('B->A wrong: ' + g2);

  A.send('finish 100 1 5'); B.send('finish 120 1 6');
  const ra = await A.expect((l) => l.startsWith('result '), 'A result');
  if (ra === 'result you finish_tick') ok('A wins by finish_tick: ' + ra); else if (ra) fail('A result wrong: ' + ra);

  // ---- 2) ranked matchmaking queue ----
  C.send('hello carol'); D.send('hello dave');
  C.send('queue'); D.send('queue');
  const mc = await C.expect((l) => l.startsWith('match '), 'C match (ranked)');
  const md = await D.expect((l) => l.startsWith('match '), 'D match (ranked)');
  if (mc && md) ok('ranked match: ' + mc);
  if (mc && !mc.endsWith(' 1')) fail('ranked match should be ranked=1: ' + mc);
  C.send('pos 5 7 9');
  const g3 = await D.expect((l) => l.startsWith('g '), 'D sees C pos');
  if (g3 === 'g 5 7 9') ok('realtime C->D: ' + g3); else if (g3) fail('C->D wrong: ' + g3);

  // ---- bad join code ----
  C.send('join NOPEXX');
  const jf = await C.expect((l) => l.startsWith('joinfail '), 'bad join rejected', 1500);
  if (jf) ok('bad code rejected: ' + jf);

  [A, B, C, D].forEach((m) => m.close());
  bridges.forEach((b) => b.close());
  relay.close();
  await sleep(120);
  console.log(failed ? '\nE2E FAILED' : '\nE2E PASSED');
  process.exit(failed ? 1 : 0);
})();
