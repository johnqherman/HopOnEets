// mod text line (one per WS frame) -> relay message (null = ignore/unknown)
export function modLineToMsg(line: string, fallbackName = "anon"): any | null {
  const t = line.trim();
  if (!t) return null;
  const a = t.split(/\s+/);
  switch (a[0]) {
    case "hello":
      return {
        type: "hello",
        uuid: a[1] || fallbackName,
        player_id: a[2] || a[1] || fallbackName,
        version: a[3] || "0.0.0",
      };
    case "host":
      return { type: "host" };
    case "join":
      return { type: "join", code: a[1] || "" };
    case "queue":
      return { type: "queue", ranked: a[1] !== "casual" }; // bare `queue` = ranked
    case "ready":
      return { type: "ready" };
    case "build":
      return { type: "build", name: a[1], x: +a[2], y: +a[3] };
    case "buildend":
      return { type: "buildend" };
    case "pos":
      return {
        type: "pos",
        tick: +a[1],
        x: +a[2],
        y: +a[3],
        emo: a[4] || "h",
        mot: a[5] || "w",
        flip: a[6] ? +a[6] : 0,
        rot: a[7] ? +a[7] : 0,
        anim: a[8] || "-",
        frame: a[9] !== undefined ? +a[9] : -1,
        vx: a[10] !== undefined ? +a[10] : 0,
        vy: a[11] !== undefined ? +a[11] : 0,
      };
    case "hash":
      return { type: "hash", tick: +a[1], hash: a[2], platform: a[3] };
    case "desync":
      return { type: "desync", tick: +a[1] };
    case "finish":
      return {
        type: "finish",
        finish_tick: +a[1],
        completed: a[2] === "1",
        items_used: +a[3],
        deaths: a[4] ? +a[4] : 0,
      };
    case "forfeit":
      return { type: "forfeit" };
    case "mullvote":
      return { type: "mullvote", on: a[1] === "1" };
    default:
      return null;
  }
}

// relay message -> mod text line (null = nothing to send to a text client)
export function msgToModLine(m: any): string | null {
  switch (m.type) {
    case "match_config":
      return `match ${m.match_id} ${m.opponent} ${m.ranked ? 1 : 0} ${m.level ?? -1} ${m.seed ?? 0} ${m.self_r ?? 0} ${m.opp_r ?? 0} ${m.self_rank ?? 0} ${m.opp_rank ?? 0}`;
    case "countdown":
      return `countdown ${m.seconds} ${m.cap ?? 0}`;
    case "round":
      return `round ${m.round} ${m.level ?? -1} ${m.seed ?? 0}`;
    case "room":
      return `code ${m.code}`;
    case "join_failed":
      return `joinfail ${m.code}`;
    case "opp_pos":
      return `g ${m.tick} ${m.x} ${m.y} ${m.emo || "h"} ${m.mot || "w"} ${m.flip ? 1 : 0} ${m.rot ?? 0} ${m.anim || "-"} ${m.frame ?? -1} ${m.vx ?? 0} ${m.vy ?? 0}`;
    case "opp_build":
      return `ob ${m.name} ${m.x} ${m.y}`;
    case "opp_buildend":
      return "obend";
    case "opp_finish":
      return `oppfin ${m.finish_tick} ${m.completed ? 1 : 0} ${m.items_used}`;
    case "opp_hash":
      return `oh ${m.tick} ${m.hash} ${m.platform}`;
    case "no_contest":
      return `nocontest ${m.reason} ${m.tick ?? -1}`;
    case "result":
      return `result ${m.winner} ${m.reason} ${m.you_wins} ${m.opp_wins}`;
    case "update":
      return `update ${m.version} ${m.url} ${m.sha256 || "-"} ${m.required ? 1 : 0}`;
    case "rating":
      return `rating ${m.value ?? 0} ${m.rank ?? 0}`;
    case "series_over":
      return `series ${m.winner} ${m.you_wins} ${m.opp_wins} ${m.ranked ? 1 : 0} ${m.r_old ?? 0} ${m.r_new ?? 0} ${m.forfeit ? 1 : 0} ${m.rank ?? 0}`;
    case "opponent_left":
      return "oppleft";
    case "opponent_dropped":
      return `oppdrop ${m.seconds ?? 20}`;
    case "opponent_rejoined":
      return "opprejoin";
    case "rejoin":
      return `rejoin ${m.opponent} ${m.ranked ? 1 : 0} ${m.you_wins} ${m.opp_wins}`;
    case "opp_mulligan_vote":
      return `oppmull ${m.on ? 1 : 0}`;
    case "mulligan":
      return "mulligan";
    default:
      return null;
  }
}
