#pragma once
#include <cmath> // atan2f (opponent rotation)
#include "state.h"
#include "animtable.h"

// must report a finish even on failure (death/DNF) or relay stalls forever;
// idempotent per round via g_finishTick
static void report_finish(bool completed) {
  if (g_finishTick >= 0 || g_phase == IDLE)
    return;
  g_finishTick = g_tick + g_deathTicks; // round time incl banked death penalty
  Eets::Log(
      "hop_on_eets: report_finish completed=%d tick=%ld matched=%d ranked=%d",
      completed ? 1 : 0, g_finishTick, g_matched ? 1 : 0, g_ranked ? 1 : 0);
  report_determinism();
  if (g_matched) {
    char fb[80];
    snprintf(fb, sizeof(fb), "finish %ld %d %d %d", g_finishTick,
             completed ? 1 : 0, placed_count(), g_deaths);
    net_sendline(fb);
  } else {
    snprintf(g_roundMsg, sizeof(g_roundMsg), "round: %.2fs",
             g_finishTick / (double)TICK_RATE);
  }
  g_roundCounter++;
  if (g_matched) {
    g_interRound = true; // suppress draws: vanilla victory transition makes
                         // engine draws unsafe
    void *cr = World_GetCreator();
    Creator_ClearWinEffect(cr); // kill deferred win-effect (survives reused
                                // Builder across next-round reload)
    Creator_StopAllModals(cr);
  }
  snprintf(g_status, sizeof(g_status),
           completed ? "complete tick=%ld" : "failed tick=%ld", g_finishTick);
}

static void match_on_event(const char *name, void *a, void *b) {
  if (strcmp(name, "level_load") == 0) {
    begin_build();
  } else if (strcmp(name, "level_reset") == 0) {
    bool wasSim = (g_phase == SIM);
    g_resets++;
    g_phase = BUILD;
    // player Stop mid-sim gets the retry shot clock like a death; exclude our
    // own resets (g_selfStop/g_deathReset)
    if (g_matched && wasSim && !g_selfStop && !g_deathReset &&
        g_finishTick < 0) {
      g_deathTicks += g_tick;
      g_retryActive = true;
      g_retryStart = Time();
      snprintf(g_status, sizeof(g_status), "stopped - rebuild + Go");
    } else {
      snprintf(g_status, sizeof(g_status), "build (reset %d)", g_resets);
    }
  } else if (strcmp(name, "object_spawn") == 0) {
    if (g_phase == BUILD) {
      Object *o = (Object *)a;
      const char *nm = (const char *)b;
      if (o)
        g_placements.push_back({(unsigned long long)Object_GetID(o),
                                nm ? nm : Object_GetBlueprintName(o), 0.f, 0.f,
                                false});
    }
  } else if (strcmp(name, "object_killed") == 0) {
    if (g_phase == BUILD && a) {
      unsigned long long id = (unsigned long long)Object_GetID((Object *)a);
      for (auto &p : g_placements)
        if (p.id == id)
          p.removed = true;
    }
  } else if (strcmp(name, "level_won") == 0) {
    report_finish(true); // real win signal, fires before vanilla WinDialog
  } else if (strcmp(name, "level_complete") == 0) {
    report_finish(
        true); // backup: only if player clicks through vanilla victory dialog
  } else if (strcmp(name, "eets_dying") == 0) {
    // match death = retry not loss; reset deferred to next Update (off engine's
    // death call stack)
    if (g_matched && g_phase == SIM) {
      g_deaths++;
      g_deathTicks += g_tick;
      g_deathReset = true;
      Creator_StopAllModals(World_GetCreator());
    }
  }
}

// tick once per second on final fromSec of a countdown; `last` = last second
// announced
static void countdown_beep(double remain, int &last, int fromSec) {
  if (remain > 0.0 && remain < (double)(fromSec + 1)) {
    int s = (int)remain;
    if (remain > (double)s)
      s++; // ceil
    if (s >= 1 && s <= fromSec && s != last) {
      last = s;
      PlaySound("GUI Click 2");
    }
  } else
    last = -1;
}

static void end_series_to_menu() {
  g_winShow = false;
  g_winForfeit = false;
  g_matched = false;
  g_phase = IDLE;
  g_menuOpen = false;
  g_interRound = false;
  g_seriesOver = false;
  g_roundMsg[0] = 0;
  g_netMsg[0] = 0;   // clear transient status ("forfeiting...") so it doesn't stick on the menu
  g_levelIndex = -1;
  g_liveValid = false;
  g_oppBuild.clear();
  World_StartMainMenu();
}

// per-frame match state machine; sets g_buildRemain for HUD; does not draw
static void match_update() {
  if (g_winShow) {
    if (Time() >= g_winUntil)
      end_series_to_menu();
    return;
  }
  // custom cards (face-off / between-round) show BEFORE the level loads: hold gameplay while the
  // card plays, keep the lingering victory dialog dismissed, then load + ready once it ends
  if (g_loadAfterShowdown) {
    if (g_matched) {
      void *cr = World_GetCreator();
      Creator_ClearWinEffect(cr);
      Creator_StopAllModals(cr);
    }
    if (Time() >= g_showdownUntil) {
      if (g_autoLoad)
        load_match_level();
      net_sendline("ready");
      g_loadAfterShowdown = false;
    }
    return;
  }
  // anti-cheat: leaving the match level by ANY route (main menu, CHOOSE LEVEL, LOAD REPLAY,
  // PUZZLE MAP) must forfeit - never a free escape from a loss. The win-screen and between-round
  // loads returned above, so reaching here while matched + no-longer-in-level + not mid-load
  // means the player bailed out themselves. Edge-triggered so it fires once.
  static bool s_inMatchLevel = false;
  bool inLvl = in_level();
  if (g_matched && s_inMatchLevel && !inLvl && !World_IsLoading()) {
    s_inMatchLevel = false;
    forfeit_match();
    g_phase = IDLE;
    return;
  }
  s_inMatchLevel = g_matched && inLvl;
  // win-effect timer (Builder+0x2fa4) re-shows victory dialog ~1s after win, so
  // re-dismiss every frame
  if (g_interRound && g_matched) {
    void *cr = World_GetCreator();
    Creator_ClearWinEffect(cr);
    Creator_StopAllModals(cr);
  }
  if (g_deathReset) {
    void *cr = World_GetCreator();
    g_selfStop = true;
    Creator_StopSimulation(cr);
    g_selfStop = false; // our reset, not a player Stop
    Creator_StopAllModals(cr);
    Simulator_SetPaused(false);
    g_phase = BUILD;
    g_deathReset = false;
    g_retryActive = true;
    g_retryStart = Time();
    snprintf(g_status, sizeof(g_status), "died x%d - rebuild + Go", g_deaths);
  }

  g_buildRemain = 0;
  if (!in_level()) {
    if (g_phase != IDLE) {
      g_phase = IDLE;
      snprintf(g_status, sizeof(g_status), "idle");
    }
    return;
  }

  bool inMatch = (g_matched || g_matchActive);
  bool timed = g_buildSeconds > 0 && inMatch;
  bool simulating = World_IsSimulating();
  bool cine = (g_showdownKind != 0 && Time() < g_showdownUntil);

  // block early Go: sim may only start when synced build timer fires
  if (timed && g_phase != SIM && !g_forcedStart && simulating) {
    abort_early_sim();
    simulating = false;
    snprintf(g_status, sizeof(g_status), "wait for Go");
  }
  if (inMatch)
    lock_match_speed(); // no fast-forward / pause-cheese
  if (g_matched) {
    set_match_buttons_hidden(true);
    g_buttonsHidden = true;
    // the vanilla Esc menu (PuzzleMapDialog: CHOOSE LEVEL / LOAD REPLAY / PUZZLE MAP / QUIT) is a
    // modal - stomp it every frame so it can't stay open mid-match; the HOE overlay replaces it.
    // Esc is swallowed by the loader, but the on-screen HUD MenuButton still opens that modal; treat
    // its appearance as "player wants the menu" and raise our overlay instead. Skip between rounds,
    // where the active modal is the victory dialog (already stomped above), not a player request.
    if (!g_interRound && !cine && Menu_ModalActive())
      g_menuOpen = true;
    Creator_StopAllModals(World_GetCreator());
  } else if (g_buttonsHidden) {
    set_match_buttons_hidden(false);
    g_buttonsHidden = false;
  }

  if (g_phase != SIM && simulating)
    begin_sim(g_resets > 0);
  else if (g_phase == SIM && !simulating && g_finishTick < 0) {
    g_phase = BUILD;
    snprintf(g_status, sizeof(g_status), "build");
  }

  if (g_phase == BUILD && inMatch) {
    if (cine) { // freeze countdown during cinematic (pin start each frame)
      g_buildStart = Time();
      g_retryStart = Time();
      g_buildRemain = g_retryActive ? g_retrySeconds : g_buildSeconds;
      g_lastBuildTick = -1;
    } else if (g_retryActive) { // retry shot clock: auto-Go at 0, early manual
                                // Go allowed
      g_buildRemain = g_retrySeconds - (Time() - g_retryStart);
      if (g_buildRemain <= 0) {
        g_retryActive = false;
        force_start_sim();
      }
    } else if (timed && !g_forcedStart) { // initial synced build: auto-Go at 0
      g_buildRemain = g_buildSeconds - (Time() - g_buildStart);
      if (g_buildRemain <= 0) {
        g_forcedStart = true;
        force_start_sim();
      }
    }
    if (!cine && (g_retryActive || (timed && !g_forcedStart)))
      countdown_beep(g_buildRemain, g_lastBuildTick, 5);
    else if (!cine)
      g_lastBuildTick = -1;
  } else
    g_lastBuildTick = -1;
  // round cap (anti-stall): too long without winning -> DNF
  if (!cine && g_matched && !g_interRound && g_finishTick < 0 &&
      g_roundStart > 0 && g_roundCapSeconds > 0) {
    double roundLeft = g_roundCapSeconds - (Time() - g_roundStart);
    countdown_beep(roundLeft, g_lastRoundTick, 10);
    if (roundLeft < 0)
      report_finish(false);
  } else
    g_lastRoundTick = -1;

  // Esc/pause must never freeze a live match sim. World_IsPaused() and the sim-step gate share one
  // flag (Simulator+0xb9), so a local pause stalls our deterministic stream while the opponent runs
  // on -> desync. Clear it every frame during the sim; the menu is a live overlay (see mod_on_key).
  if (g_matched && g_phase == SIM && !g_interRound)
    Simulator_SetPaused(false);

  if (g_phase == SIM && simulating && !World_IsPaused()) {
    Object *e = World_GetEets();
    if (e) { // skip when Eets isn't live yet (avoids garbage 0,0)
      Vector2 ep = Object_GetPosition(e);
      if (valid_pos(ep.x, ep.y)) {
        if (g_matched && g_tick / g_posSendInterval > g_lastPosBucket) {
          g_lastPosBucket = g_tick / g_posSendInterval; // throttle ~15Hz; receiver extrapolates between
          // stream pos + anim so opponent's ghost mirrors our Eets
          char emo, mot;
          int flip;
          read_eets_anim(e, emo, mot, flip);
          Vector2 fac = Object_GetFacing(e);
          float rot = atan2f(fac.y, fac.x); // engine's GetRotation = orientation off +x; mirrors tumbling
          // the actual current ANIM base name (e.g. "eets_happy_squat"), covering eat/land/windup/etc the
          // emo+mot enum misses. single space-free token, "-" if unavailable.
          char tok[40] = "-";
          if (const char *nm = Object_GetCurrentAnimName(e))
            if (nm[0]) {
              size_t j = 0;
              for (const char *p = nm; *p && j < sizeof(tok) - 1; ++p)
                if ((unsigned char)*p > ' ')
                  tok[j++] = *p;
              tok[j] = 0;
              if (!j) { tok[0] = '-'; tok[1] = 0; }
            }
          int aid = anim_name_to_id(tok); // known anim -> small int id, saves wire bytes
          if (aid >= 0)
            snprintf(tok, sizeof(tok), "%d", aid);
          int frame = Object_GetAnimFrameIndex(e); // exact anim frame -> ghost mirrors it (no looping)
          if (frame > 0) frame--;                   // engine index is 1-based; DrawAnim wants 0-based
          Vector2 vel = Object_GetVelocity(e); // exact per-tick velocity -> clean ghost extrapolation
          char pb[140];
          // quantize: receiver truncates to int px anyway; trims ~12 B/line
          snprintf(pb, sizeof(pb), "pos %ld %.0f %.0f %c %c %d %.2f %s %d %.1f %.1f",
                   g_tick, ep.x, ep.y, emo, mot, flip, rot, tok, frame, vel.x,
                   vel.y);
          net_sendline(pb);
        }
        if (g_tick / HASH_INTERVAL >
            g_lastHashBucket) { // one sample per interval bucket
          g_lastHashBucket = g_tick / HASH_INTERVAL;
          g_samples.push_back({g_tick, ep.x, ep.y, state_hash()});
        }
      }
    }
    // advance from engine sim-tick (catches sub-stepping); fall back to ++ if
    // counter unreadable
    long et = Engine_GetSimTick();
    g_tick = (et >= 0 && g_engineTickBase >= 0) ? (et - g_engineTickBase)
                                                : (g_tick + 1);
    // DNF watchdog (match only): sim that neither completes nor kills must
    // resolve or relay waits forever
    if (g_matched && g_finishTick < 0 && g_tick > g_simMaxTicks)
      report_finish(false);
  }
}
