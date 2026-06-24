// Hop On Eets bridge (v0.2). Sits between the native mod and the relay:
//   mod  <-- localhost TCP, newline text -->  bridge  <-- WebSocket JSON -->  relay
// Keeps the 32-bit native plugin tiny (no WS/TLS): it speaks a few text lines over a local socket,
// while the real-time leg to the server is a WebSocket.
//
// mod -> bridge: "hello <id>" | "host" | "join <code>" | "queue" | "pos <tick> <x> <y>" |
//                "finish <tick> <0|1> <items>"
// bridge -> mod: "code <CODE>" | "joinfail <CODE>" | "match <id> <opp> <ranked0|1>" |
//                "g <tick> <x> <y>" | "oppfin <tick> <0|1> <items>" | "result <winner> <reason>" |
//                "oppleft"
import * as net from 'net';
import * as ws from './ws';

export interface BridgeOpts {
  relayHost?: string;
  relayPort?: number;
  modPort?: number;
  player?: string;
  log?: (s: string) => void;
}

export function startBridge(opts: BridgeOpts): { tcp: net.Server; close(): void } {
  const relayHost = opts.relayHost ?? '127.0.0.1';
  const relayPort = opts.relayPort ?? 38500;
  const modPort = opts.modPort ?? 38600;
  const player = opts.player ?? 'p1';
  const log = opts.log ?? (() => {});

  let mod: net.Socket | null = null;
  let relay: ws.WSConn | null = null;
  let modBuf = '';
  let pending: string[] = [];   // mod lines queued until the relay WS is up

  const toMod = (line: string) => { if (mod && !mod.destroyed) mod.write(line + '\n'); };

  function handleModLine(line: string): void {
    const t = line.trim(); if (!t) return;
    if (!relay) { pending.push(t); return; }
    const a = t.split(/\s+/);
    switch (a[0]) {
      case 'hello':  relay.send({ type: 'hello', player_id: a[1] || player }); break;
      case 'host':   relay.send({ type: 'host' }); break;
      case 'join':   relay.send({ type: 'join', code: a[1] || '' }); break;
      case 'queue':  relay.send({ type: 'queue' }); break;
      case 'ready':  relay.send({ type: 'ready' }); break;
      case 'build':  relay.send({ type: 'build', name: a[1], x: +a[2], y: +a[3] }); break;
      case 'buildend': relay.send({ type: 'buildend' }); break;
      case 'pos':    relay.send({ type: 'pos', tick: +a[1], x: +a[2], y: +a[3], emo: a[4] || 'h', mot: a[5] || 'w', flip: a[6] ? +a[6] : 0 }); break;   // a[4..6] = opponent anim state
      case 'hash':   relay.send({ type: 'hash', tick: +a[1], hash: a[2], platform: a[3] }); break;
      case 'desync': relay.send({ type: 'desync', tick: +a[1] }); break;
      case 'replay': relay.send({ type: 'submit_replay', round: +a[1], platform: a[2], log: a[3] }); break;   // a[3] = base64 input log
      case 'finish': relay.send({ type: 'finish', finish_tick: +a[1], completed: a[2] === '1', items_used: +a[3], deaths: a[4] ? +a[4] : 0 }); break;
    }
  }

  function openRelay(): void {
    ws.connect(relayHost, relayPort, (conn, err) => {
      if (err || !conn) { log('relay connect failed: ' + (err && err.message)); return; }
      relay = conn;
      conn.send({ type: 'hello', player_id: player });
      log('connected to relay as ' + player);
      const q = pending; pending = []; q.forEach(handleModLine);
      conn.onJSON((m: any) => {
        switch (m.type) {
          case 'match_config':  toMod(`match ${m.match_id} ${m.opponent} ${m.ranked ? 1 : 0} ${m.level ?? -1} ${m.seed ?? 0}`); break;
          case 'countdown':     toMod(`countdown ${m.seconds} ${m.cap ?? 0}`); break;
          case 'round':         toMod(`round ${m.round} ${m.level ?? -1} ${m.seed ?? 0}`); break;
          case 'room':          toMod(`code ${m.code}`); break;
          case 'join_failed':   toMod(`joinfail ${m.code}`); break;
          case 'opp_pos':       toMod(`g ${m.tick} ${m.x} ${m.y} ${m.emo || 'h'} ${m.mot || 'w'} ${m.flip ? 1 : 0}`); break;
          case 'opp_build':     toMod(`ob ${m.name} ${m.x} ${m.y}`); break;
          case 'opp_buildend':  toMod('obend'); break;
          case 'opp_finish':    toMod(`oppfin ${m.finish_tick} ${m.completed ? 1 : 0} ${m.items_used}`); break;
          case 'opp_hash':      toMod(`oh ${m.tick} ${m.hash} ${m.platform}`); break;
          case 'no_contest':    toMod(`nocontest ${m.reason} ${m.tick ?? -1}`); break;
          case 'authoritative': toMod(`auth ${m.kind} ${m.winner || '-'} ${m.reason}`); break;
          case 'result':        toMod(`result ${m.winner} ${m.reason} ${m.you_wins} ${m.opp_wins}`); break;
          case 'series_over':   toMod(`series ${m.winner} ${m.you_wins} ${m.opp_wins}`); break;
          case 'opponent_left': toMod('oppleft'); break;
        }
      });
      conn.onClose(() => { log('relay closed'); relay = null; });
    });
  }

  const tcp = net.createServer((sock) => {
    log('mod connected on :' + modPort);
    mod = sock; modBuf = '';
    if (!relay) openRelay();
    sock.on('data', (d) => {
      modBuf += d.toString('utf8');
      let i: number;
      while ((i = modBuf.indexOf('\n')) >= 0) { handleModLine(modBuf.slice(0, i)); modBuf = modBuf.slice(i + 1); }
    });
    sock.on('close', () => { log('mod disconnected'); mod = null; if (relay) { relay.close(); relay = null; } });  // mod quit -> leave the match
    sock.on('error', () => {});
  });
  tcp.listen(modPort, '127.0.0.1', () => log(`bridge tcp on 127.0.0.1:${modPort} -> relay ${relayHost}:${relayPort}`));
  openRelay();
  return { tcp, close: () => { try { tcp.close(); } catch { /* */ } if (relay) relay.close(); } };
}

if (require.main === module) {
  startBridge({
    relayHost: process.env.RELAY_HOST || '127.0.0.1',
    relayPort: parseInt(process.env.RELAY_PORT || '38500', 10),
    modPort: parseInt(process.env.MOD_PORT || '38600', 10),
    player: process.env.PLAYER || 'p1',
    log: (s) => console.log('[bridge]', s),
  });
}
