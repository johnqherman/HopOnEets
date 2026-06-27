import * as http from "http";
import type * as net from "net";
import * as fs from "fs";
import * as ws from "./ws";
import { modLineToMsg, msgToModLine } from "./modproto"; // mod text protocol <-> relay JSON

export interface Finish {
  completed: boolean;
  finish_tick: number;
  items_used: number;
  deaths?: number;
}
interface Peer {
  id: string;
  name: string;
  match: string | null;
  opp: ws.WSConn | null;
  finish: Finish | null;
  hostCode: string | null;
  wins: number;
  ready: boolean;
  noContest: boolean;
  ranked: boolean;
  roundTimer: ReturnType<typeof setTimeout> | null;
}
// match held open for reconnect, keyed by dropped player's uuid
interface Held {
  oppConn: ws.WSConn;
  match: string;
  dropWins: number;
  ranked: boolean;
  timer: ReturnType<typeof setTimeout>;
}
// id = stable client UUID (rating key; names are spoofable so must not key the rating); name = display only
const BEST_OF = 3,
  WINS_NEEDED = 2;

// ---- Glicko-2 rating ladder (ranked only); persisted JSON on the relay host, keyed by uuid ----
// per player: rating r (display, 1500 base), deviation rd (350), volatility vol (0.06). TAU bounds
// volatility change. each ranked series = one rating period vs one opponent (lichess-style per-game).
const RATING_FILE =
  process.env.RATING_FILE || process.env.ELO_FILE || "ratings.json";
const R0 = 1500,
  RD0 = 350,
  VOL0 = 0.06,
  TAU = 0.5,
  SCALE = 173.7178;
interface Rating {
  r: number;
  rd: number;
  vol: number;
}
const defRating = (): Rating => ({ r: R0, rd: RD0, vol: VOL0 });
let ratings: Record<string, Rating> = {};
try {
  const raw = JSON.parse(fs.readFileSync(RATING_FILE, "utf8"));
  for (const k in raw)
    ratings[k] =
      typeof raw[k] === "number" ? { r: raw[k], rd: RD0, vol: VOL0 } : raw[k]; // migrate legacy numeric (Elo-era) entries
} catch {
  /* first run, empty */
}
const getRating = (id: string): Rating => ratings[id] ?? defRating();
const displayR = (id: string): number => Math.round(getRating(id).r);
function saveRatings(): void {
  try {
    fs.writeFileSync(RATING_FILE, JSON.stringify(ratings, null, 2));
  } catch (e) {
    console.error("[relay] rating save failed:", e);
  }
}
// one Glicko-2 step for `p` after a game vs `opp` with score s (1 win / 0 loss); returns new state
function glicko2(p: Rating, opp: Rating, s: number): Rating {
  const mu = (p.r - R0) / SCALE,
    phi = p.rd / SCALE;
  const muJ = (opp.r - R0) / SCALE,
    phiJ = opp.rd / SCALE;
  const g = 1 / Math.sqrt(1 + (3 * phiJ * phiJ) / (Math.PI * Math.PI));
  const e = 1 / (1 + Math.exp(-g * (mu - muJ)));
  const v = 1 / (g * g * e * (1 - e));
  const delta = v * g * (s - e);
  // volatility: solve Glickman step 5 via the Illinois (regula-falsi) iteration, bounded by TAU
  const a = Math.log(p.vol * p.vol);
  const f = (x: number) =>
    (Math.exp(x) * (delta * delta - phi * phi - v - Math.exp(x))) /
      (2 * (phi * phi + v + Math.exp(x)) ** 2) -
    (x - a) / (TAU * TAU);
  let A = a,
    B;
  if (delta * delta > phi * phi + v)
    B = Math.log(delta * delta - phi * phi - v);
  else {
    let k = 1;
    while (f(a - k * TAU) < 0) k++;
    B = a - k * TAU;
  }
  let fA = f(A),
    fB = f(B);
  for (let i = 0; i < 100 && Math.abs(B - A) > 1e-6; i++) {
    const C: number = A + ((A - B) * fA) / (fB - fA);
    const fC = f(C);
    if (fC * fB <= 0) {
      A = B;
      fA = fB;
    } else fA = fA / 2;
    B = C;
    fB = fC;
  }
  const vol = Math.exp(A / 2);
  const phiStar = Math.sqrt(phi * phi + vol * vol);
  const phiNew = 1 / Math.sqrt(1 / (phiStar * phiStar) + 1 / v);
  const muNew = mu + phiNew * phiNew * g * (s - e);
  return { r: muNew * SCALE + R0, rd: phiNew * SCALE, vol };
}
// apply a ranked series result + persist; returns both players' display ratings old->new (rounded)
function applyRating(winnerId: string, loserId: string) {
  const wOld = getRating(winnerId),
    lOld = getRating(loserId);
  const wNew = glicko2(wOld, lOld, 1),
    lNew = glicko2(lOld, wOld, 0);
  ratings[winnerId] = wNew;
  ratings[loserId] = lNew;
  saveRatings();
  return {
    wOld: Math.round(wOld.r),
    wNew: Math.round(wNew.r),
    lOld: Math.round(lOld.r),
    lNew: Math.round(lNew.r),
  };
}
export type Verdict = { winner: "you" | "opponent" | "tie"; reason: string };

// tiebreakers: completion, then (completers only) finish_tick, deaths, items_used. A non-solver
// must never win on time/items, so those only apply when both completed. Neither solving = draw,
// which scores no round win and replays the round until someone solves it
function decideOutcome(a: Finish, b: Finish): { self: Verdict; opp: Verdict } {
  let win: boolean | null;
  let reason: string;
  if (a.completed !== b.completed) {
    win = a.completed;
    reason = "completion";
  } else if (!a.completed) {
    win = null;
    reason = "both_failed";
  } // draw, replay round
  else if (a.finish_tick !== b.finish_tick) {
    win = a.finish_tick < b.finish_tick;
    reason = "finish_tick";
  } else if ((a.deaths ?? 0) !== (b.deaths ?? 0)) {
    win = (a.deaths ?? 0) < (b.deaths ?? 0);
    reason = "deaths";
  } else if (a.items_used !== b.items_used) {
    win = a.items_used < b.items_used;
    reason = "items_used";
  } else {
    win = null;
    reason = "tie";
  }
  const self: Verdict = {
    winner: win === null ? "tie" : win ? "you" : "opponent",
    reason,
  };
  const opp: Verdict = {
    winner: win === null ? "tie" : win ? "opponent" : "you",
    reason,
  };
  return { self, opp };
}

export function startRelay(
  port: number,
  log: (s: string) => void = () => {},
): http.Server {
  let rankedQueue: ws.WSConn[] = [];
  let casualQueue: ws.WSConn[] = [];
  let nextMatch = 1;
  const BUILD_SECONDS = 45; // synced build phase; clients start timer on `countdown`
  const ROUND_CAP_SECONDS = 180; // round wall-clock after build; relay-authoritative, DNFs unfinished peers on expiry
  // the client freezes its build clock during the ~4s round-start cinematic, so its real round start (and its
  // cap clock) lags the countdown by that much. this is grace on the relay backstop so it outlasts the synced
  // client clocks (which DNF at their displayed 0:00) instead of pre-empting them and ending the round early.
  const CAP_GRACE_SECONDS = 12;
  const RECONNECT_SECONDS = 20; // dropped (non-forfeit) player has this long to reconnect
  const peers = new Map<ws.WSConn, Peer>();
  const rooms = new Map<string, ws.WSConn>();
  const held = new Map<string, Held>(); // uuid -> held match

  function makeCode(): string {
    const al = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no ambiguous 0/O/1/I
    let c: string;
    do {
      c = "";
      for (let i = 0; i < 6; i++)
        c += al[Math.floor(Math.random() * al.length)];
    } while (rooms.has(c));
    return c;
  }

  const pickLevel = () => Math.floor(Math.random() * 1000); // clients resolve `level % poolSize` -> same level
  const pickSeed = () => 1 + Math.floor(Math.random() * 0x7ffffffe); // RNG seed valid for both engines

  function pair(a: ws.WSConn, b: ws.WSConn, ranked: boolean): void {
    const match = "hoe_" + String(nextMatch++).padStart(6, "0");
    const A = peers.get(a)!,
      B = peers.get(b)!;
    A.match = match;
    A.opp = b;
    A.finish = null;
    A.wins = 0;
    A.ready = false;
    A.noContest = false;
    A.ranked = ranked;
    B.match = match;
    B.opp = a;
    B.finish = null;
    B.wins = 0;
    B.ready = false;
    B.noContest = false;
    B.ranked = ranked;
    const level = pickLevel(),
      seed = pickSeed(); // round 1; every round picks fresh, see startRound
    const cfg = (
      selfName: string,
      oppName: string,
      selfR: number,
      oppR: number,
    ) => ({
      type: "match_config",
      match_id: match,
      mode: "solution_race",
      ruleset: ranked ? "ranked_v0" : "casual",
      ranked,
      level,
      seed,
      tick_rate: 60,
      self: selfName,
      opponent: oppName,
      self_r: selfR,
      opp_r: oppR,
    });
    const eA = ranked ? displayR(A.id) : 0,
      eB = ranked ? displayR(B.id) : 0;
    a.send(cfg(A.name, B.name, eA, eB)); // display names + rating; uuid stays server-side
    b.send(cfg(B.name, A.name, eB, eA));
    // countdown sent once both report `ready`; see ready handler
    log(
      `match ${match} (${ranked ? "ranked" : "private"}): ${A.name} vs ${B.name} (round 1 level ${level})`,
    );
  }

  // next round of a series: fresh level + seed, reset ready flags, tell clients to load it; synced
  // countdown then fires from ready handler once both re-loaded (round 1 bootstrapped by pair())
  function startRound(a: ws.WSConn, b: ws.WSConn, round: number): void {
    const A = peers.get(a),
      B = peers.get(b);
    if (!A || !B) return;
    A.ready = false;
    B.ready = false;
    A.finish = null;
    B.finish = null;
    const level = pickLevel(),
      seed = pickSeed();
    const msg = { type: "round", round, level, seed };
    a.send(msg);
    b.send(msg);
    log(`round ${round} ${A.match}: level ${level}`);
  }

  function tryMatch(q: ws.WSConn[], ranked: boolean): void {
    while (q.length >= 2) pair(q.shift()!, q.shift()!, ranked);
  }

  function relay(conn: ws.WSConn, msg: unknown): void {
    const p = peers.get(conn);
    if (!p || !p.opp) return;
    p.opp.send(msg);
  }

  function clearRoundTimer(p: Peer | undefined): void {
    if (p && p.roundTimer) {
      clearTimeout(p.roundTimer);
      p.roundTimer = null;
    }
  }
  // round wall-clock ran out: DNF anyone unfinished, then score
  function onRoundTimeout(a: ws.WSConn, b: ws.WSConn): void {
    const A = peers.get(a),
      B = peers.get(b);
    if (!A || !B || A.opp !== b) return; // match still valid?
    const dnf: Finish = {
      completed: false,
      finish_tick: 1 << 30,
      items_used: 0,
      deaths: 9999,
    };
    if (!A.finish) A.finish = dnf;
    if (!B.finish) B.finish = dnf;
    log(`round timeout ${A.match}: cap reached, DNF unfinished`);
    maybeResult(a);
  }

  function sendCountdown(a: ws.WSConn, b: ws.WSConn): void {
    const A = peers.get(a),
      B = peers.get(b);
    a.send({
      type: "countdown",
      seconds: BUILD_SECONDS,
      cap: ROUND_CAP_SECONDS,
    });
    b.send({
      type: "countdown",
      seconds: BUILD_SECONDS,
      cap: ROUND_CAP_SECONDS,
    });
    clearRoundTimer(A);
    clearRoundTimer(B);
    // relay backstops the cap so a stalled/dropped client can't hang the round (cap counts play only)
    const t = setTimeout(
      () => onRoundTimeout(a, b),
      (BUILD_SECONDS + ROUND_CAP_SECONDS + CAP_GRACE_SECONDS) * 1000,
    );
    if (typeof (t as { unref?: () => void }).unref === "function")
      (t as { unref: () => void }).unref();
    if (A) A.roundTimer = t;
    if (B) B.roundTimer = t;
  }

  const decide = decideOutcome;

  function maybeResult(conn: ws.WSConn): void {
    const p = peers.get(conn);
    if (!p || !p.opp) return;
    const q = peers.get(p.opp);
    if (!q || !p.finish) return;
    if (p.noContest || q.noContest) {
      // desynced match: report round, score nothing
      conn.send({ type: "no_contest", reason: "desync" });
      p.opp.send({ type: "no_contest", reason: "desync" });
      p.finish = null;
      q.finish = null;
      log(`no contest ${p.match}: round void (desync)`);
      return;
    }
    // first to complete wins immediately; a failure waits for the opponent (both fail -> draw)
    let r: { self: Verdict; opp: Verdict };
    if (p.finish.completed)
      r = {
        self: { winner: "you", reason: "completed" },
        opp: { winner: "opponent", reason: "completed" },
      };
    else if (q.finish && q.finish.completed)
      r = {
        self: { winner: "opponent", reason: "completed" },
        opp: { winner: "you", reason: "completed" },
      };
    else if (q.finish)
      r = decide(p.finish, q.finish); // both failed -> draw
    else return; // we failed, opponent still racing -> wait
    clearRoundTimer(p);
    clearRoundTimer(q);
    if (r.self.winner === "you") p.wins++;
    else if (r.self.winner === "opponent") q.wins++;
    conn.send({
      type: "result",
      ...r.self,
      you_wins: p.wins,
      opp_wins: q.wins,
    });
    p.opp.send({
      type: "result",
      ...r.opp,
      you_wins: q.wins,
      opp_wins: p.wins,
    });
    p.finish = null;
    q.finish = null;
    log(`result ${p.match}: ${r.self.winner} (series ${p.wins}-${q.wins})`);
    if (p.wins >= WINS_NEEDED || q.wins >= WINS_NEEDED) {
      // series decided
      const pWon = p.wins >= WINS_NEEDED;
      let pE = { old: 0, neu: 0 },
        qE = { old: 0, neu: 0 };
      if (p.ranked) {
        const r = pWon ? applyRating(p.id, q.id) : applyRating(q.id, p.id);
        pE = pWon ? { old: r.wOld, neu: r.wNew } : { old: r.lOld, neu: r.lNew };
        qE = pWon ? { old: r.lOld, neu: r.lNew } : { old: r.wOld, neu: r.wNew };
        log(
          `rating ${p.match}: ${p.name} ${pE.old}->${pE.neu}, ${q.name} ${qE.old}->${qE.neu}`,
        );
      }
      conn.send({
        type: "series_over",
        winner: pWon ? "you" : "opponent",
        you_wins: p.wins,
        opp_wins: q.wins,
        ranked: p.ranked,
        r_old: pE.old,
        r_new: pE.neu,
      });
      p.opp.send({
        type: "series_over",
        winner: pWon ? "opponent" : "you",
        you_wins: q.wins,
        opp_wins: p.wins,
        ranked: p.ranked,
        r_old: qE.old,
        r_new: qE.neu,
      });
      log(
        `series over ${p.match}: ${pWon ? p.name : q.name} (best of ${BEST_OF})`,
      );
      clearRoundTimer(p);
      clearRoundTimer(q);
      p.wins = 0;
      q.wins = 0;
      p.match = null;
      p.opp = null;
      q.match = null;
      q.opp = null; // free both to re-queue
    } else {
      startRound(conn, p.opp, p.wins + q.wins + 1);
    }
  }

  const server = http.createServer((_req, res) => {
    res.writeHead(426);
    res.end("upgrade required");
  });
  server.on("upgrade", (req, socket) => {
    const conn = ws.accept(req, socket as net.Socket);
    peers.set(conn, {
      id: "?",
      name: "?",
      match: null,
      opp: null,
      finish: null,
      hostCode: null,
      wins: 0,
      ready: false,
      noContest: false,
      ranked: false,
      roundTimer: null,
    });
    const dispatch = (m: any) => {
      const p = peers.get(conn);
      if (!p) return;
      switch (m.type) {
        case "hello": {
          p.id = String(m.uuid ?? m.player_id ?? "anon");
          p.name = String(m.player_id ?? p.id);
          const h = held.get(p.id); // reconnecting into a held match?
          const q = h && peers.get(h.oppConn);
          if (h && q && q.match === h.match && !q.opp) {
            // reattach: swap live conn back in
            clearTimeout(h.timer);
            held.delete(p.id);
            p.match = h.match;
            p.opp = h.oppConn;
            p.wins = h.dropWins;
            p.ranked = h.ranked;
            p.finish = null;
            p.ready = false;
            q.opp = conn;
            q.finish = null;
            q.ready = false;
            conn.send({
              type: "rejoin",
              match_id: h.match,
              opponent: q.name,
              ranked: p.ranked,
              you_wins: p.wins,
              opp_wins: q.wins,
            });
            h.oppConn.send({ type: "opponent_rejoined" });
            log(
              `reconnect ${h.match}: ${p.name} rejoined (series ${p.wins}-${q.wins})`,
            );
            startRound(conn, h.oppConn, p.wins + q.wins + 1); // restart current round for both
          } else {
            conn.send({ type: "rating", value: displayR(p.id) });
          }
          break;
        }
        case "host": {
          if (p.hostCode) rooms.delete(p.hostCode);
          const code = makeCode();
          p.hostCode = code;
          rooms.set(code, conn);
          conn.send({ type: "room", code });
          log(`host ${p.name} room ${code}`);
          break;
        }
        case "join": {
          const code = String(m.code ?? "").toUpperCase();
          const host = rooms.get(code);
          if (!host || host === conn) {
            conn.send({ type: "join_failed", code });
            break;
          }
          rooms.delete(code);
          peers.get(host)!.hostCode = null;
          pair(host, conn, false);
          break;
        }
        case "queue": {
          const ranked = m.ranked !== false; // default ranked
          // drop from both pools first: dedupes, and lets a re-queue switch modes
          rankedQueue = rankedQueue.filter((c) => c !== conn);
          casualQueue = casualQueue.filter((c) => c !== conn);
          const q = ranked ? rankedQueue : casualQueue;
          q.push(conn);
          tryMatch(q, ranked);
          break;
        }
        case "ready": {
          // both loaded -> start synced countdown
          const r = peers.get(conn);
          if (!r || !r.opp) break;
          r.ready = true;
          const o = peers.get(r.opp);
          if (o && o.ready) {
            sendCountdown(conn, r.opp);
            r.ready = false;
            o.ready = false;
          }
          break;
        }
        case "pos":
          relay(conn, {
            type: "opp_pos",
            tick: m.tick,
            x: m.x,
            y: m.y,
            emo: m.emo,
            mot: m.mot,
            flip: m.flip,
            rot: m.rot ?? 0,
            anim: m.anim ?? "-",
            frame: m.frame ?? -1,
          });
          break;
        case "build":
          relay(conn, { type: "opp_build", name: m.name, x: m.x, y: m.y });
          break;
        case "buildend":
          relay(conn, { type: "opp_buildend" });
          break;
        case "hash":
          relay(conn, {
            type: "opp_hash",
            tick: m.tick | 0,
            hash: String(m.hash ?? ""),
            platform: String(m.platform ?? ""),
          });
          break;
        case "desync": {
          // same-platform divergence -> whole match no-contest
          const q = p.opp ? peers.get(p.opp) : null;
          p.noContest = true;
          if (q) q.noContest = true;
          const payload = {
            type: "no_contest",
            reason: "desync",
            tick: m.tick | 0,
          };
          conn.send(payload);
          if (p.opp) p.opp.send(payload);
          log(`no contest ${p.match}: desync @${m.tick | 0}`);
          break;
        }
        case "forfeit": {
          // intentional leave: resolve the series now (opponent wins), no reconnect hold; the
          // forfeiter stays connected to see its DEFEAT screen with the real new rating
          const fq = p.opp ? peers.get(p.opp) : undefined;
          if (!p.opp || !p.match || !fq) break;
          clearRoundTimer(p);
          clearRoundTimer(fq);
          let pE = { old: 0, neu: 0 },
            qE = { old: 0, neu: 0 };
          if (p.ranked) {
            const r = applyRating(fq.id, p.id);
            qE = { old: r.wOld, neu: r.wNew };
            pE = { old: r.lOld, neu: r.lNew };
          }
          conn.send({
            type: "series_over",
            winner: "opponent",
            you_wins: p.wins,
            opp_wins: fq.wins,
            ranked: p.ranked,
            r_old: pE.old,
            r_new: pE.neu,
            forfeit: true,
          });
          p.opp.send({
            type: "series_over",
            winner: "you",
            you_wins: fq.wins,
            opp_wins: p.wins,
            ranked: fq.ranked,
            r_old: qE.old,
            r_new: qE.neu,
            forfeit: true,
          });
          log(`forfeit ${p.match}: ${p.name} forfeited to ${fq.name}`);
          p.wins = 0;
          fq.wins = 0;
          p.match = null;
          p.opp = null;
          fq.match = null;
          fq.opp = null;
          break;
        }
        case "finish":
          p.finish = {
            completed: !!m.completed,
            finish_tick: m.finish_tick | 0,
            items_used: m.items_used | 0,
            deaths: m.deaths | 0,
          };
          relay(conn, { type: "opp_finish", ...p.finish });
          maybeResult(conn);
          break;
      }
    };
    // native mod speaks text over wss; browsers speak JSON. Decide from first frame: leading '{' =
    // JSON, else mod-text. Mod clients route through modLineToMsg and override send() to emit mod-text,
    // so internal conn.send() calls work unchanged for both
    let decided = false,
      rawMod = false;
    conn.onText((s: string) => {
      if (!decided) {
        decided = true;
        rawMod = !s.trimStart().startsWith("{");
        if (rawMod)
          conn.send = (obj: any) => {
            const ln = msgToModLine(obj);
            if (ln !== null) conn.sendText(ln);
          };
      }
      if (rawMod) {
        const m = modLineToMsg(s);
        if (m) dispatch(m);
      } else {
        let m: any;
        try {
          m = JSON.parse(s);
        } catch {
          return;
        }
        dispatch(m);
      }
    });
    conn.onClose(() => {
      const p = peers.get(conn);
      if (!p) return;
      clearRoundTimer(p);
      rankedQueue = rankedQueue.filter((c) => c !== conn);
      casualQueue = casualQueue.filter((c) => c !== conn);
      if (p.hostCode) rooms.delete(p.hostCode);
      const q = p.opp ? peers.get(p.opp) : undefined;
      if (p.opp && p.match && q) {
        // unexpected drop (not a forfeit - those resolve in dispatch): hold match for reconnect
        clearRoundTimer(q);
        q.opp = null;
        q.finish = null;
        p.opp.send({ type: "opponent_dropped", seconds: RECONNECT_SECONDS });
        const oppConn = p.opp,
          match = p.match,
          dropWins = p.wins,
          oppWins = q.wins,
          leaverId = p.id,
          leaverName = p.name;
        const timer = setTimeout(() => {
          if (held.get(leaverId)?.timer !== timer) return; // already reattached/handled
          held.delete(leaverId);
          const qq = peers.get(oppConn);
          if (qq && qq.match === match && !qq.opp) {
            // window expired, no reconnect -> award opponent
            let qE = { old: 0, neu: 0 };
            if (qq.ranked) {
              const r = applyRating(qq.id, leaverId);
              qE = { old: r.wOld, neu: r.wNew };
              log(
                `rating dropout ${match}: ${qq.name} ${qE.old}->${qE.neu}, ${leaverName} ${r.lOld}->${r.lNew}`,
              );
            }
            oppConn.send({
              type: "series_over",
              winner: "you",
              you_wins: oppWins,
              opp_wins: dropWins,
              ranked: qq.ranked,
              r_old: qE.old,
              r_new: qE.neu,
              forfeit: true,
            });
            qq.opp = null;
            qq.match = null;
            qq.wins = 0;
          }
        }, RECONNECT_SECONDS * 1000);
        held.set(p.id, {
          oppConn: p.opp,
          match: p.match,
          dropWins: p.wins,
          ranked: p.ranked,
          timer,
        });
        log(
          `drop ${p.match}: ${p.name} dropped - ${RECONNECT_SECONDS}s to reconnect`,
        );
        peers.delete(conn);
        return;
      }
      if (p.opp && q) {
        // opp set but not a held mid-match drop (transient): notify + free
        clearRoundTimer(q);
        p.opp.send({ type: "opponent_left" });
        q.opp = null;
        q.match = null;
        q.wins = 0;
      }
      peers.delete(conn);
    });
  });
  // keepalive: idle queued sockets otherwise die at the proxy read timeout (~min) and the player silently
  // drops from the queue. ping every connection well under that; peers auto-pong, and a dead pipe throws
  // on write -> the socket's close fires -> dequeue. Matched players send frames constantly so are never idle.
  const HEARTBEAT_MS = 25000;
  const heartbeat = setInterval(() => {
    for (const conn of peers.keys()) conn.ping();
  }, HEARTBEAT_MS);
  heartbeat.unref?.(); // don't keep the process alive on its own
  server.on("close", () => clearInterval(heartbeat));
  server.listen(port, () => log(`relay listening on ws://0.0.0.0:${port}`));
  return server;
}

if (require.main === module) {
  const port = parseInt(process.env.PORT || "38500", 10);
  startRelay(port, (s) => console.log("[relay]", s));
}
