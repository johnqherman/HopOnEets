//   fake mod (TCP) <-> bridge <-> WS <-> relay <-> WS <-> bridge <-> fake mod (TCP)
//   1) host/join by code (private)   2) ranked matchmaking queue
import * as net from "net";
import { startRelay } from "./relay";
import { startBridge } from "./bridge";

const RELAY = 38590;
let failed = false;
const fail = (m: string) => {
  failed = true;
  console.error("FAIL:", m);
};
const ok = (m: string) => console.log("ok -", m);
const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

interface Mod {
  send(s: string): void;
  expect(
    pred: (l: string) => boolean,
    label: string,
    ms?: number,
  ): Promise<string | null>;
  seen(pred: (l: string) => boolean): boolean;
  close(): void;
}

function fakeMod(port: number): Mod {
  const sock = net.connect(port, "127.0.0.1");
  const lines: string[] = [];
  const waiters: {
    pred: (l: string) => boolean;
    resolve: (l: string | null) => void;
  }[] = [];
  sock.on("data", (d) => {
    for (const ln of d.toString("utf8").split("\n")) {
      if (!ln) continue;
      lines.push(ln);
      for (let i = waiters.length - 1; i >= 0; i--)
        if (waiters[i].pred(ln)) {
          waiters[i].resolve(ln);
          waiters.splice(i, 1);
        }
    }
  });
  return {
    send: (s) => sock.write(s + "\n"),
    expect: (pred, label, ms = 2000) =>
      new Promise<string | null>((resolve) => {
        const hit = lines.find(pred);
        if (hit) return resolve(hit);
        const w = { pred, resolve };
        waiters.push(w);
        setTimeout(() => {
          if (waiters.includes(w)) {
            fail("timeout: " + label);
            resolve(null);
          }
        }, ms);
      }),
    seen: (pred) => lines.some(pred),
    close: () => sock.end(),
  };
}

(async () => {
  const relay = startRelay(RELAY, () => {});
  const bridges: { close(): void }[] = [];
  const mk = (port: number, player: string) =>
    bridges.push(startBridge({ relayPort: RELAY, modPort: port, player }));
  mk(38691, "alice");
  mk(38692, "bob");
  mk(38693, "carol");
  mk(38694, "dave");
  mk(38695, "erin");
  mk(38696, "finn");
  mk(38697, "grace");
  mk(38698, "heath");
  mk(38699, "ivan");
  mk(38700, "june");
  await sleep(250);
  const A = fakeMod(38691),
    B = fakeMod(38692),
    C = fakeMod(38693),
    D = fakeMod(38694),
    E = fakeMod(38695),
    F = fakeMod(38696),
    G = fakeMod(38697),
    H = fakeMod(38698),
    I = fakeMod(38699),
    J = fakeMod(38700);
  await sleep(120);

  // ---- 1) private host/join by code ----
  A.send("hello alice");
  B.send("hello bob");
  A.send("host");
  const code = await A.expect((l) => l.startsWith("code "), "A host code");
  if (code) ok("host code issued: " + code);
  const c = code ? code.split(" ")[1] : "ZZZZZZ";
  B.send("join " + c);
  const ma = await A.expect((l) => l.startsWith("match "), "A match (private)");
  const mb = await B.expect((l) => l.startsWith("match "), "B match (private)");
  if (ma && mb) ok("private match: " + ma);
  if (ma && ma.split(" ")[3] !== "0")
    fail("private match should be ranked=0: " + ma);
  if (ma && parseInt(ma.split(" ")[4], 10) >= 0)
    ok("match carries level index: #" + ma.split(" ")[4]);
  else if (ma) fail("match missing level index: " + ma);
  if (ma && parseInt(ma.split(" ")[5], 10) > 0)
    ok("match carries per-match seed: " + ma.split(" ")[5]);
  else if (ma) fail("match missing seed: " + ma);

  // ready-gate: countdown only after both ready
  A.send("ready");
  await sleep(400);
  if (A.seen((l) => l.startsWith("countdown ")))
    fail("countdown fired before both ready");
  else ok("countdown gated on both ready");
  B.send("ready");
  const cdA = await A.expect(
    (l) => l.startsWith("countdown "),
    "A synced countdown",
  );
  const cdB = await B.expect(
    (l) => l.startsWith("countdown "),
    "B synced countdown",
  );
  if (
    cdA &&
    cdB &&
    cdA.startsWith("countdown 45") &&
    cdB.startsWith("countdown 45")
  )
    ok("synced countdown both: " + cdA);
  else if (cdA) fail("countdown wrong: " + cdA + " / " + cdB);

  // build exchange: A's build reaches B as ghost items
  A.send("build marshmallow 100 200");
  A.send("buildend");
  const ob = await B.expect((l) => l.startsWith("ob "), "B sees A build");
  if (ob === "ob marshmallow 100 200") ok("build relay A->B: " + ob);
  else if (ob) fail("build wrong: " + ob);
  if (await B.expect((l) => l === "obend", "B sees buildend"))
    ok("buildend relay");

  A.send("pos 10 123 45");
  const g1 = await B.expect((l) => l.startsWith("g "), "B sees A pos");
  if (g1 === "g 10 123 45 h w 0 0 - -1") ok("realtime A->B: " + g1);
  else if (g1) fail("A->B wrong: " + g1);
  B.send("pos 20 300 60");
  const g2 = await A.expect((l) => l.startsWith("g "), "A sees B pos");
  if (g2 === "g 20 300 60 h w 0 0 - -1") ok("realtime B->A: " + g2);
  else if (g2) fail("B->A wrong: " + g2);
  // anim state (emotion/motion/facing) rides along
  A.send("pos 12 130 50 a j 1");
  const ga = await B.expect(
    (l) => l.startsWith("g 12 "),
    "B sees A anim state",
  );
  if (ga === "g 12 130 50 a j 1 0 - -1") ok("opponent anim state relayed: " + ga);
  else if (ga) fail("anim state wrong: " + ga);

  // best-of-3: first to complete wins the round immediately (live race)
  A.send("finish 100 1 5");
  const r1 = await A.expect((l) => l.startsWith("result "), "A round-1 result");
  if (r1 === "result you completed 1 0") ok("round 1 -> A (series 1-0): " + r1);
  else if (r1) fail("round-1 result wrong: " + r1);
  A.send("finish 90 1 5");
  const so = await A.expect((l) => l.startsWith("series "), "A series_over");
  if (so === "series you 2 0 0 0 0 0")
    ok("best-of-3 -> A (2-0, private/no-elo): " + so);
  else if (so) fail("series wrong: " + so);
  const soB = await B.expect((l) => l.startsWith("series "), "B series_over");
  if (soB === "series opponent 0 2 0 0 0 0") ok("B sees series loss: " + soB);
  else if (soB) fail("B series wrong: " + soB);

  // ---- 2) ranked matchmaking queue ----
  C.send("hello carol");
  D.send("hello dave");
  C.send("queue");
  D.send("queue");
  const mc = await C.expect((l) => l.startsWith("match "), "C match (ranked)");
  const md = await D.expect((l) => l.startsWith("match "), "D match (ranked)");
  if (mc && md) ok("ranked match: " + mc);
  if (mc && mc.split(" ")[3] !== "1")
    fail("ranked match should be ranked=1: " + mc);
  C.send("pos 5 7 9");
  const g3 = await D.expect((l) => l.startsWith("g "), "D sees C pos");
  if (g3 === "g 5 7 9 h w 0 0 - -1") ok("realtime C->D: " + g3);
  else if (g3) fail("C->D wrong: " + g3);

  // ---- 2b) casual queue: separate pool from ranked, ranked=0 ----
  G.send("hello grace");
  H.send("hello heath");
  // cross-pool isolation: a casual seeker and a ranked seeker must NOT match
  G.send("queue casual");
  H.send("queue ranked");
  await sleep(400);
  if (G.seen((l) => l.startsWith("match ")) || H.seen((l) => l.startsWith("match ")))
    fail("casual and ranked seekers cross-matched");
  else ok("casual and ranked pools isolated");
  // same pool now matches them; H re-queues casual
  H.send("queue casual");
  const mg = await G.expect((l) => l.startsWith("match "), "G match (casual)");
  const mh = await H.expect((l) => l.startsWith("match "), "H match (casual)");
  if (mg && mh) ok("casual match: " + mg);
  if (mg && mg.split(" ")[3] !== "0")
    fail("casual match should be ranked=0: " + mg);

  // ---- checkpoint hash relay + desync = no-contest ----
  C.send("hash 60 aaaaaaaaaaaaaaaa linux64");
  const oh = await D.expect(
    (l) => l.startsWith("oh "),
    "D sees C checkpoint hash",
  );
  if (oh === "oh 60 aaaaaaaaaaaaaaaa linux64")
    ok("checkpoint hash relay C->D: " + oh);
  else if (oh) fail("hash relay wrong: " + oh);
  // hash mismatch -> D reports desync, relay voids for both
  D.send("desync 60");
  const ncC = await C.expect(
    (l) => l.startsWith("nocontest "),
    "C no-contest",
    2000,
  );
  const ncD = await D.expect(
    (l) => l.startsWith("nocontest "),
    "D no-contest",
    2000,
  );
  if (ncC && ncC.startsWith("nocontest desync"))
    ok("desync -> no-contest (C): " + ncC);
  else if (ncC) fail("no-contest wrong: " + ncC);
  if (ncD) ok("both clients see no-contest");

  // ---- bad join code ----
  C.send("join NOPEXX");
  const jf = await C.expect(
    (l) => l.startsWith("joinfail "),
    "bad join rejected",
    1500,
  );
  if (jf) ok("bad code rejected: " + jf);

  // ---- mid-match drop -> reconnect window -> rejoin (not an instant loss) ----
  D.close(); // network drop (no forfeit): relay holds the match
  const od = await C.expect(
    (l) => l.startsWith("oppdrop"),
    "C sees opponent dropped",
    2000,
  );
  if (od && od.startsWith("oppdrop "))
    ok("drop -> reconnect hold (not a loss): " + od);
  else if (od) fail("oppdrop wrong: " + od);
  // D reconnects same uuid -> reattach (bridge auto-sends hello on reconnect)
  await sleep(200);
  const D2 = fakeMod(38694);
  const rj = await D2.expect(
    (l) => l.startsWith("rejoin "),
    "D rejoins the held match",
    3000,
  );
  if (rj && rj.startsWith("rejoin ")) ok("reconnect -> rejoin: " + rj);
  else if (rj) fail("rejoin wrong: " + rj);
  const opr = await C.expect(
    (l) => l === "opprejoin",
    "C sees opponent rejoined",
    2000,
  );
  if (opr) ok("opponent_rejoined delivered");
  const rr = await C.expect(
    (l) => l.startsWith("round "),
    "round restarts for C",
    2000,
  );
  if (rr) ok("round restarts after reconnect: " + rr);
  D2.close();

  // ---- ranked Glicko-2 series: E beats F 2-0 -> rating moves off the 500 seed ----
  E.send("hello erin");
  F.send("hello finn");
  E.send("queue");
  F.send("queue");
  await E.expect((l) => l.startsWith("match "), "E ranked match");
  await F.expect((l) => l.startsWith("match "), "F ranked match");
  for (let r = 1; r <= 2; r++) {
    E.send("finish 100 1 5"); // E completes first -> wins the round
    await E.expect((l) => l.startsWith("result "), "E round " + r);
  }
  const es = await E.expect((l) => l.startsWith("series "), "E series_over");
  const fs2 = await F.expect((l) => l.startsWith("series "), "F series_over");
  // series <winner> <yw> <ow> <ranked> <rOld> <rNew> <forfeit>
  const ep = es ? es.split(" ") : [];
  if (
    es &&
    ep[1] === "you" &&
    ep[4] === "1" &&
    +ep[5] === 500 &&
    +ep[6] > 500 &&
    ep[7] === "0"
  )
    ok(
      "ranked win: Glicko 500 -> " +
        ep[6] +
        " (up, ranked, not forfeit): " +
        es,
    );
  else if (es) fail("E series wrong: " + es);
  const fp = fs2 ? fs2.split(" ") : [];
  if (fs2 && fp[1] === "opponent" && +fp[5] === 500 && +fp[6] < 500)
    ok("ranked loss: Glicko 500 -> " + fp[6] + " (down): " + fs2);
  else if (fs2) fail("F series wrong: " + fs2);
  // both freed -> re-queue; now test FORFEIT (server-authoritative, both get series)
  E.send("queue");
  F.send("queue");
  await E.expect((l) => l.startsWith("match "), "E rematch");
  await F.expect((l) => l.startsWith("match "), "F rematch");
  F.send("forfeit");
  // a forfeit series line ends with the forfeit flag 1 (the prior rating series ended 0)
  const efWin = await E.expect(
    (l) => l.startsWith("series ") && l.endsWith(" 1"),
    "E wins on F forfeit",
  );
  const ffLoss = await F.expect(
    (l) => l.startsWith("series ") && l.endsWith(" 1"),
    "F sees own forfeit loss",
  );
  if (efWin && efWin.split(" ")[1] === "you" && efWin.split(" ")[7] === "1")
    ok("forfeit -> opponent wins (forfeit flag): " + efWin);
  else if (efWin) fail("forfeit winner wrong: " + efWin);
  if (
    ffLoss &&
    ffLoss.split(" ")[1] === "opponent" &&
    ffLoss.split(" ")[7] === "1"
  )
    ok("forfeiter gets server-authoritative DEFEAT + rating: " + ffLoss);
  else if (ffLoss) fail("forfeiter series wrong: " + ffLoss);

  // ---- 3 drawn (both-failed) rounds in a row -> NO CONTEST, nobody wins ----
  I.send("hello ivan");
  J.send("hello june");
  I.send("queue ranked");
  J.send("queue ranked");
  await I.expect((l) => l.startsWith("match "), "I match (nc test)");
  await J.expect((l) => l.startsWith("match "), "J match (nc test)");
  let ncI: string | null = null,
    ncJ: string | null = null;
  for (let r = 1; r <= 3; r++) {
    I.send("finish 100 0 0"); // completed=0 -> both fail -> draw (mulligan)
    J.send("finish 100 0 0");
    if (r < 3) {
      const ri = await I.expect((l) => l.startsWith("result "), `I draw ${r}`);
      if (ri && ri.split(" ")[1] !== "tie") fail(`draw ${r} not a tie: ` + ri);
      await J.expect((l) => l.startsWith("result "), `J draw ${r}`);
      await I.expect((l) => l.startsWith("round "), `round ${r + 1} (I)`);
      await J.expect((l) => l.startsWith("round "), `round ${r + 1} (J)`);
    } else {
      ncI = await I.expect((l) => l.startsWith("series "), "I no-contest");
      ncJ = await J.expect((l) => l.startsWith("series "), "J no-contest");
    }
  }
  if (ncI && ncI.split(" ")[1] === "nocontest")
    ok("3 draws in a row -> NO CONTEST: " + ncI);
  else if (ncI) fail("expected nocontest series: " + ncI);
  if (ncJ && ncJ.split(" ")[1] === "nocontest")
    ok("opponent also sees NO CONTEST");
  else if (ncJ) fail("J nocontest wrong: " + ncJ);

  // ---- vote-to-mulligan: mutual consent replays the round; one-sided does nothing ----
  mk(38701, "kara");
  mk(38702, "liam");
  await sleep(200);
  const K = fakeMod(38701),
    L = fakeMod(38702);
  K.send("hello kara");
  L.send("hello liam");
  K.send("queue ranked");
  L.send("queue ranked");
  await K.expect((l) => l.startsWith("match "), "K match (mull test)");
  await L.expect((l) => l.startsWith("match "), "L match (mull test)");
  // one-sided vote: opponent is notified, but no replay fires
  K.send("mullvote 1");
  const lv1 = await L.expect((l) => l.startsWith("oppmull "), "L sees K vote");
  if (lv1 === "oppmull 1") ok("one-sided vote relayed K->L: " + lv1);
  else if (lv1) fail("oppmull wrong: " + lv1);
  await sleep(300);
  if (K.seen((l) => l === "mulligan") || L.seen((l) => l === "mulligan"))
    fail("mulligan fired on a single vote");
  else ok("single vote does not fire a mulligan");
  // retract clears the opponent's indicator
  K.send("mullvote 0");
  const lv0 = await L.expect((l) => l === "oppmull 0", "L sees K retract");
  if (lv0) ok("vote retract relayed: " + lv0);
  // both vote -> replay: both get `mulligan` then a fresh `round` for the same round slot (1)
  K.send("mullvote 1");
  await L.expect((l) => l === "oppmull 1", "L sees K re-vote");
  L.send("mullvote 1");
  const mK = await K.expect((l) => l === "mulligan", "K mulligan fires");
  const mL = await L.expect((l) => l === "mulligan", "L mulligan fires");
  if (mK && mL) ok("both voted -> mulligan fires for both");
  const rK = await K.expect((l) => l.startsWith("round "), "K round replays");
  const rL = await L.expect((l) => l.startsWith("round "), "L round replays");
  if (rK && rK.split(" ")[1] === "1" && rL && rL.split(" ")[1] === "1")
    ok("mulligan replays the same round slot (round 1): " + rK);
  else if (rK) fail("mulligan round wrong: " + rK + " / " + rL);
  K.close();
  L.close();

  // ---- a real finish after voting clears the vote; it must not persist into the next round ----
  mk(38703, "mira");
  mk(38704, "nash");
  await sleep(200);
  const M = fakeMod(38703),
    N = fakeMod(38704);
  M.send("hello mira");
  N.send("hello nash");
  M.send("queue ranked");
  N.send("queue ranked");
  await M.expect((l) => l.startsWith("match "), "M match (finish-clears test)");
  await N.expect((l) => l.startsWith("match "), "N match (finish-clears test)");
  // M votes to mulligan, then actually completes the round (first-to-complete wins it)
  M.send("mullvote 1");
  await N.expect((l) => l === "oppmull 1", "N sees M vote pre-finish");
  M.send("finish 100 1 5"); // completed=1 -> M wins round 1 immediately
  const mr = await M.expect((l) => l.startsWith("result you"), "M wins round 1");
  if (mr) ok("finish after voting wins the round: " + mr);
  await M.expect((l) => l.startsWith("round 2"), "M round 2 starts");
  await N.expect((l) => l.startsWith("round 2"), "N round 2 starts");
  // the win cleared M's vote on the relay: N voting alone in round 2 must NOT fire a mulligan
  N.send("mullvote 1");
  await M.expect((l) => l === "oppmull 1", "M sees N vote (round 2)");
  await sleep(300);
  if (M.seen((l) => l === "mulligan") || N.seen((l) => l === "mulligan"))
    fail("stale vote survived the finish -> wrongful mulligan next round");
  else ok("finish clears the vote: stale vote cannot fire in the next round");
  M.close();
  N.close();

  [A, B, C, E, F, I, J].forEach((m) => m.close());
  bridges.forEach((b) => b.close());
  relay.close();
  await sleep(120);
  console.log(failed ? "\nE2E FAILED" : "\nE2E PASSED");
  process.exit(failed ? 1 : 0);
})();
