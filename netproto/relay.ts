// Hop On Eets relay server (v0.2). Zero runtime deps; WebSocket. Pairs two players and relays
// REAL-TIME position frames between them (plus build/finish), then reports a provisional result.
// Two ways in: private host/join by code, or ranked matchmaking queue. The server does not
// simulate; authoritative cross-platform re-sim is v0.3 (see docs/hop-on-eets-spec.md Part 6).
import * as http from 'http';
import type * as net from 'net';
import * as ws from './ws';

interface Finish { completed: boolean; finish_tick: number; items_used: number; }
interface Peer { id: string; match: string | null; opp: ws.WSConn | null; finish: Finish | null; hostCode: string | null; }
type Verdict = { winner: 'you' | 'opponent' | 'tie'; reason: string };

export function startRelay(port: number, log: (s: string) => void = () => {}): http.Server {
  let queue: ws.WSConn[] = [];
  let nextMatch = 1;
  const peers = new Map<ws.WSConn, Peer>();
  const rooms = new Map<string, ws.WSConn>();

  function makeCode(): string {
    const al = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; // no ambiguous 0/O/1/I
    let c: string;
    do { c = ''; for (let i = 0; i < 6; i++) c += al[Math.floor(Math.random() * al.length)]; } while (rooms.has(c));
    return c;
  }

  function pair(a: ws.WSConn, b: ws.WSConn, ranked: boolean): void {
    const match = 'hoe_' + String(nextMatch++).padStart(6, '0');
    const A = peers.get(a)!, B = peers.get(b)!;
    A.match = match; A.opp = b; A.finish = null;
    B.match = match; B.opp = a; B.finish = null;
    const cfg = (selfId: string, oppId: string) => ({
      type: 'match_config', match_id: match, mode: 'solution_race',
      ruleset: ranked ? 'ranked_v0' : 'casual', ranked,
      tick_rate: 60, seed: 123456789, self: selfId, opponent: oppId,
    });
    a.send(cfg(A.id, B.id));
    b.send(cfg(B.id, A.id));
    log(`match ${match} (${ranked ? 'ranked' : 'private'}): ${A.id} vs ${B.id}`);
  }

  function tryMatch(): void {
    while (queue.length >= 2) { const a = queue.shift()!, b = queue.shift()!; pair(a, b, true); }
  }

  function relay(conn: ws.WSConn, msg: unknown): void {
    const p = peers.get(conn); if (!p || !p.opp) return;
    p.opp.send(msg);
  }

  // tiebreakers: completion, finish_tick, items_used (spec Part 3)
  function decide(a: Finish, b: Finish): { self: Verdict; opp: Verdict } {
    let win: boolean | null; let reason: string;
    if (a.completed !== b.completed) { win = a.completed; reason = 'completion'; }
    else if (a.completed && a.finish_tick !== b.finish_tick) { win = a.finish_tick < b.finish_tick; reason = 'finish_tick'; }
    else if (a.items_used !== b.items_used) { win = a.items_used < b.items_used; reason = 'items_used'; }
    else { win = null; reason = 'tie'; }
    const self: Verdict = { winner: win === null ? 'tie' : (win ? 'you' : 'opponent'), reason };
    const opp: Verdict = { winner: win === null ? 'tie' : (win ? 'opponent' : 'you'), reason };
    return { self, opp };
  }

  function maybeResult(conn: ws.WSConn): void {
    const p = peers.get(conn); if (!p || !p.opp) return;
    const q = peers.get(p.opp); if (!q || !p.finish || !q.finish) return;
    const r = decide(p.finish, q.finish);
    conn.send({ type: 'result', ...r.self, provisional: true });
    p.opp.send({ type: 'result', ...r.opp, provisional: true });
    log(`result ${p.match}: ${r.self.winner}`);
  }

  const server = http.createServer((_req, res) => { res.writeHead(426); res.end('upgrade required'); });
  server.on('upgrade', (req, socket) => {
    const conn = ws.accept(req, socket as net.Socket);
    peers.set(conn, { id: '?', match: null, opp: null, finish: null, hostCode: null });
    conn.onJSON((m: any) => {
      const p = peers.get(conn); if (!p) return;
      switch (m.type) {
        case 'hello': p.id = String(m.player_id ?? 'anon'); break;
        case 'host': {
          if (p.hostCode) rooms.delete(p.hostCode);
          const code = makeCode(); p.hostCode = code; rooms.set(code, conn);
          conn.send({ type: 'room', code });
          log(`host ${p.id} room ${code}`);
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
        case 'pos':   relay(conn, { type: 'opp_pos', tick: m.tick, x: m.x, y: m.y }); break;
        case 'build': relay(conn, { type: 'opp_build', name: m.name, x: m.x, y: m.y }); break;
        case 'buildend': relay(conn, { type: 'opp_buildend' }); break;
        case 'finish':
          p.finish = { completed: !!m.completed, finish_tick: m.finish_tick | 0, items_used: m.items_used | 0 };
          relay(conn, { type: 'opp_finish', ...p.finish });
          maybeResult(conn);
          break;
      }
    });
    conn.onClose(() => {
      const p = peers.get(conn); if (!p) return;
      queue = queue.filter((c) => c !== conn);
      if (p.hostCode) rooms.delete(p.hostCode);
      if (p.opp) { p.opp.send({ type: 'opponent_left' }); const q = peers.get(p.opp); if (q) q.opp = null; }
      peers.delete(conn);
    });
  });
  server.listen(port, () => log(`relay listening on ws://0.0.0.0:${port}`));
  return server;
}

if (require.main === module) {
  const port = parseInt(process.env.PORT || '38500', 10);
  startRelay(port, (s) => console.log('[relay]', s));
}
