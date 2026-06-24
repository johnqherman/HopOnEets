// Unit test for the authoritative verifier (verify -> decide) using a mock re-sim runner.
// Proves: reproduced claims decide by tiebreakers; an unreproduced claim is DQ'd (not a silent loss);
// both unreproduced = no-contest. No game needed.
import { verifyMatch, MockResimRunner, Submission } from './verifier';

let failed = false;
const fail = (m: string) => { failed = true; console.error('FAIL:', m); };
const ok = (m: string) => console.log('ok -', m);

const A: Submission = { player: 'alice', replayPath: 'a.json', platform: 'linux64' };
const B: Submission = { player: 'bob', replayPath: 'b.json', platform: 'win32' };

(async () => {
  // 1) both reproduce, A finishes sooner -> A wins by finish_tick
  let r = await verifyMatch(A, B, new MockResimRunner({
    'a.json': { reproduced: true, completed: true, finish_tick: 1700, items_used: 5 },
    'b.json': { reproduced: true, completed: true, finish_tick: 1820, items_used: 5 },
  }));
  if (r.kind === 'decided' && r.winner === 'alice' && r.reason === 'finish_tick') ok('both reproduced -> A by finish_tick');
  else fail('decided/finish_tick wrong: ' + JSON.stringify(r));

  // 2) completion beats non-completion regardless of tick
  r = await verifyMatch(A, B, new MockResimRunner({
    'a.json': { reproduced: true, completed: false, finish_tick: -1, items_used: 4 },
    'b.json': { reproduced: true, completed: true, finish_tick: 5000, items_used: 9 },
  }));
  if (r.kind === 'decided' && r.winner === 'bob' && r.reason === 'completion') ok('completion beats DNF -> B');
  else fail('completion rule wrong: ' + JSON.stringify(r));

  // 3) A's claim does NOT reproduce -> A is DQ'd, B wins (not a silent loss)
  r = await verifyMatch(A, B, new MockResimRunner({
    'a.json': { reproduced: false, completed: true, finish_tick: 1, items_used: 1 },   // bogus claim
    'b.json': { reproduced: true, completed: true, finish_tick: 1900, items_used: 6 },
  }));
  if (r.kind === 'dq' && r.winner === 'bob' && r.reason === 'a_claim_unverified') ok('unreproduced claim -> DQ, opponent wins');
  else fail('DQ rule wrong: ' + JSON.stringify(r));

  // 4) neither reproduces -> no contest
  r = await verifyMatch(A, B, new MockResimRunner({
    'a.json': { reproduced: false, completed: true, finish_tick: 1, items_used: 1 },
    'b.json': { reproduced: false, completed: true, finish_tick: 1, items_used: 1 },
  }));
  if (r.kind === 'no_contest' && r.reason === 'neither_reproduced') ok('neither reproduced -> no contest');
  else fail('no-contest rule wrong: ' + JSON.stringify(r));

  // 5) a tie (identical reproduced outcomes) decides 'tie' with no winner
  r = await verifyMatch(A, B, new MockResimRunner({
    'a.json': { reproduced: true, completed: true, finish_tick: 1500, items_used: 5 },
    'b.json': { reproduced: true, completed: true, finish_tick: 1500, items_used: 5 },
  }));
  if (r.kind === 'decided' && r.winner === undefined && r.reason === 'tie') ok('identical outcomes -> tie');
  else fail('tie rule wrong: ' + JSON.stringify(r));

  console.log(failed ? '\nVERIFIER FAILED' : '\nVERIFIER PASSED');
  process.exit(failed ? 1 : 0);
})();
