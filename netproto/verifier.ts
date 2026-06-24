// verifier.ts - authoritative cross-platform result (spec Part 6; docs/headless-resim.md).
//
// Cross-platform float physics is not bit-identical, so a ranked result is NOT decided from client
// claims or cross-platform hashes. Each player's input log is re-simulated on ONE canonical build
// (tools/resim-runner.sh, headless) and the OUTCOMES are compared with the same tiebreakers the relay
// uses. A claim the re-sim does not reproduce is rejected (DQ / no-contest), never a silent loss.
import { spawn } from 'child_process';
import { decideOutcome } from './relay';

export interface Verdict { reproduced: boolean; completed: boolean; finish_tick: number; items_used: number; }
export interface Submission { player: string; replayPath: string; platform: string; }
export interface SubmissionLog { player: string; platform: string; log: string; }   // inline input log (base64)
export interface OfficialResult { kind: 'decided' | 'dq' | 'no_contest'; winner?: string; reason: string; detail: unknown; }

// the relay hands a completed ranked round to one of these; it persists both logs and re-sims them.
export type RankedVerifier = (matchId: string, round: number, a: SubmissionLog, b: SubmissionLog) => Promise<OfficialResult>;

// build a RankedVerifier from a ResimRunner + a saver (writes a submitted log to disk, returns its path)
export function makeVerifier(runner: ResimRunner, save: (matchId: string, round: number, player: string, log: string) => string): RankedVerifier {
  return (matchId, round, a, b) => verifyMatch(
    { player: a.player, replayPath: save(matchId, round, a.player, a.log), platform: a.platform },
    { player: b.player, replayPath: save(matchId, round, b.player, b.log), platform: b.platform },
    runner,
  );
}

// runs one input log -> verdict. Implementations: the real shell launcher, or a mock for tests.
export interface ResimRunner { run(replayPath: string, platform: string): Promise<Verdict>; }

export async function verifyMatch(a: Submission, b: Submission, runner: ResimRunner): Promise<OfficialResult> {
  const [va, vb] = await Promise.all([runner.run(a.replayPath, a.platform), runner.run(b.replayPath, b.platform)]);
  if (!va.reproduced && !vb.reproduced) return { kind: 'no_contest', reason: 'neither_reproduced', detail: { va, vb } };
  if (!va.reproduced) return { kind: 'dq', winner: b.player, reason: 'a_claim_unverified', detail: { va, vb } };
  if (!vb.reproduced) return { kind: 'dq', winner: a.player, reason: 'b_claim_unverified', detail: { va, vb } };
  const r = decideOutcome(
    { completed: va.completed, finish_tick: va.finish_tick, items_used: va.items_used },
    { completed: vb.completed, finish_tick: vb.finish_tick, items_used: vb.items_used },
  );
  const winner = r.self.winner === 'you' ? a.player : r.self.winner === 'opponent' ? b.player : undefined;
  return { kind: 'decided', winner, reason: r.self.reason, detail: { va, vb } };
}

// canned verdicts keyed by replay path - for unit-testing verify->decide without the game
export class MockResimRunner implements ResimRunner {
  constructor(private map: Record<string, Verdict>) {}
  run(replayPath: string): Promise<Verdict> {
    return Promise.resolve(this.map[replayPath] ?? { reproduced: false, completed: false, finish_tick: -1, items_used: 0 });
  }
}

// spawns tools/resim-runner.sh, which launches the headless game and emits the verdict JSON on stdout
export class ShellResimRunner implements ResimRunner {
  constructor(private script = '../tools/resim-runner.sh', private gameDir = process.env.EETS_DIR || '') {}
  run(replayPath: string): Promise<Verdict> {
    return new Promise((resolve) => {
      const args = [this.script, replayPath];
      if (this.gameDir) args.push('-d', this.gameDir);
      const p = spawn('bash', args);
      let out = '';
      p.stdout.on('data', (d) => (out += d.toString()));
      p.on('close', (code) => {
        let v: { resim?: { completed?: boolean; finish_tick?: number; items_applied?: number }; reproduced?: boolean } = {};
        try { v = JSON.parse(out.trim().split('\n').filter((l) => l.startsWith('{')).pop() || '{}'); } catch { /* no verdict */ }
        resolve({
          reproduced: code === 0 && !!v.reproduced,
          completed: !!v.resim?.completed,
          finish_tick: v.resim?.finish_tick ?? -1,
          items_used: v.resim?.items_applied ?? 0,
        });
      });
    });
  }
}
