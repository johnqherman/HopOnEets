// test-opponent.ts - a fake opponent for manual testing. Connects to the relay, ranked-queues, and
// once matched streams position + cycling anim state (emotion x motion x facing) so you can watch the
// in-game OPPONENT ghost mirror it. Run: `npx tsx test-opponent.ts` (after the relay is up).
import * as ws from './ws';

const RELAY = parseInt(process.env.RELAY_PORT || '38500', 10);
const HZ = 30;

ws.connect('127.0.0.1', RELAY, (conn, err) => {
  if (err || !conn) { console.error('[opp] relay connect failed:', err && err.message); process.exit(1); }
  let tick = 0; let timer: ReturnType<typeof setInterval> | null = null;
  const emos = ['h', 'a', 's'];                 // happy / angry / scared
  const mots = ['w', 'j', 'f', 's'];            // walk / jump / fall / squat

  conn.send({ type: 'hello', player_id: 'GHOSTBOT' });
  conn.send({ type: 'queue' });
  console.log('[opp] queued as GHOSTBOT — in game: F6 -> Online ON -> Ranked queue');

  const startStreaming = () => {
    if (timer) return;
    console.log('[opp] streaming pos + anim state (walks back/forth, cycles emotion/motion/facing)');
    timer = setInterval(() => {
      tick++;
      const phase = Math.sin(tick / 25);
      const x = Math.round(360 + 180 * phase);  // walk back and forth across a single-screen level
      const y = 380;
      const emo = emos[Math.floor(tick / 60) % emos.length];   // change emotion every ~2s
      const mot = mots[Math.floor(tick / 20) % mots.length];   // change motion every ~0.7s
      const flip = phase < 0 ? 1 : 0;                          // face the way it's moving
      conn.send({ type: 'pos', tick, x, y, emo, mot, flip });
    }, 1000 / HZ);
  };

  conn.onJSON((m: any) => {
    if (m.type === 'match_config') { console.log('[opp] matched vs ' + m.opponent + ' — readying'); conn.send({ type: 'ready' }); startStreaming(); }
    else if (m.type === 'round') { console.log('[opp] round ' + m.round + ' (level ' + m.level + ') — readying'); conn.send({ type: 'ready' }); }
    else if (m.type === 'countdown') {
      const build = m.seconds || 15;
      console.log('[opp] countdown ' + build + 's (your build phase) — start a sim to see the ghost');
      // finish the round a few seconds after the build phase so the relay can score it (it needs BOTH
      // finishes). Randomised completion/tick/items -> a real contest; the series runs to series_over.
      setTimeout(() => {
        const completed = Math.random() < 0.8;
        const deaths = Math.floor(Math.random() * 3);
        const finish_tick = 300 + Math.floor(Math.random() * 600);   // ~10-30s @30fps
        const items_used = 4 + Math.floor(Math.random() * 14);
        conn.send({ type: 'finish', completed, finish_tick, items_used, deaths });
        console.log('[opp] sent finish: completed=' + completed + ' tick=' + finish_tick + ' items=' + items_used + ' deaths=' + deaths);
      }, (build + 4) * 1000);
    }
    else if (m.type === 'result') console.log('[opp] result: ' + m.winner + ' (series you ' + m.you_wins + ' - ' + m.opp_wins + ' opp, from bot POV)');
    else if (m.type === 'opponent_left' || m.type === 'series_over') {
      console.log('[opp] match ended (' + m.type + ') — re-queuing');
      if (timer) { clearInterval(timer); timer = null; }
      conn.send({ type: 'queue' });
    }
  });
  conn.onClose(() => { if (timer) clearInterval(timer); console.log('[opp] disconnected'); process.exit(0); });
});
