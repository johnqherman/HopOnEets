#pragma once
#include "state.h"

// must report a finish even on failure (death/DNF) or relay stalls forever; idempotent per round via g_finishTick
static void report_finish(bool completed) {
	if (g_finishTick >= 0 || g_phase == IDLE) return;
	g_finishTick = g_tick + g_deathTicks;   // round time incl banked death penalty
	Eets::Log("hop_on_eets: report_finish completed=%d tick=%ld matched=%d ranked=%d", completed ? 1 : 0, g_finishTick, g_matched ? 1 : 0, g_ranked ? 1 : 0);
	report_determinism();
	if (g_matched) {
		char fb[80]; snprintf(fb, sizeof(fb), "finish %ld %d %d %d", g_finishTick, completed ? 1 : 0, placed_count(), g_deaths);
		net_sendline(fb);
	} else {
		snprintf(g_roundMsg, sizeof(g_roundMsg), "round: %.2fs", g_finishTick / (double)TICK_RATE);
	}
	g_roundCounter++;
	if (g_matched) {
		g_interRound = true;              // suppress draws: vanilla victory transition makes engine draws unsafe
		void* cr = World_GetCreator();
		Creator_ClearWinEffect(cr);       // kill deferred win-effect (survives reused Builder across next-round reload)
		Creator_StopAllModals(cr);
	}
	snprintf(g_status, sizeof(g_status), completed ? "complete tick=%ld" : "failed tick=%ld", g_finishTick);
}

static void match_on_event(const char* name, void* a, void* b) {
	if (strcmp(name, "level_load") == 0) {
		begin_build();
	} else if (strcmp(name, "level_reset") == 0) {
		bool wasSim = (g_phase == SIM);
		g_resets++; g_phase = BUILD;
		// player Stop mid-sim gets the retry shot clock like a death; exclude our own resets (g_selfStop/g_deathReset)
		if (g_matched && wasSim && !g_selfStop && !g_deathReset && g_finishTick < 0) {
			g_deathTicks += g_tick; g_retryActive = true; g_retryStart = Time();
			snprintf(g_status, sizeof(g_status), "stopped - rebuild + Go");
		} else {
			snprintf(g_status, sizeof(g_status), "build (reset %d)", g_resets);
		}
	} else if (strcmp(name, "object_spawn") == 0) {
		if (g_phase == BUILD) {
			Object* o = (Object*)a; const char* nm = (const char*)b;
			if (o) g_placements.push_back({ (unsigned long long)Object_GetID(o), nm ? nm : Object_GetBlueprintName(o), 0.f, 0.f, false });
		}
	} else if (strcmp(name, "object_killed") == 0) {
		if (g_phase == BUILD && a) {
			unsigned long long id = (unsigned long long)Object_GetID((Object*)a);
			for (auto& p : g_placements) if (p.id == id) p.removed = true;
		}
	} else if (strcmp(name, "level_won") == 0) {
		report_finish(true);    // real win signal, fires before vanilla WinDialog
	} else if (strcmp(name, "level_complete") == 0) {
		report_finish(true);    // backup: only if player clicks through vanilla victory dialog
	} else if (strcmp(name, "eets_dying") == 0) {
		// match death = retry not loss; reset deferred to next Update (off engine's death call stack)
		if (g_matched && g_phase == SIM) {
			g_deaths++; g_deathTicks += g_tick; g_deathReset = true;
			Creator_StopAllModals(World_GetCreator());
		}
	}
}

// tick once per second on final fromSec of a countdown; `last` = last second announced
static void countdown_beep(double remain, int& last, int fromSec) {
	if (remain > 0.0 && remain < (double)(fromSec + 1)) {
		int s = (int)remain; if (remain > (double)s) s++;   // ceil
		if (s >= 1 && s <= fromSec && s != last) { last = s; PlaySound("GUI Click 2"); }
	} else last = -1;
}

static void end_series_to_menu() {
	g_winShow = false; g_winForfeit = false; g_matched = false; g_phase = IDLE; g_menuOpen = false;
	g_interRound = false; g_seriesOver = false; g_roundMsg[0] = 0;
	g_levelIndex = -1; g_liveValid = false; g_oppBuild.clear();
	World_StartMainMenu();
}

// per-frame match state machine; sets g_buildRemain for HUD; does not draw
static void match_update() {
	if (g_winShow) { if (Time() >= g_winUntil) end_series_to_menu(); return; }
	// win-effect timer (Builder+0x2fa4) re-shows victory dialog ~1s after win, so re-dismiss every frame
	if (g_interRound && g_matched) { void* cr = World_GetCreator(); Creator_ClearWinEffect(cr); Creator_StopAllModals(cr); }
	if (g_deathReset) {
		void* cr = World_GetCreator();
		g_selfStop = true; Creator_StopSimulation(cr); g_selfStop = false;   // our reset, not a player Stop
		Creator_StopAllModals(cr); Simulator_SetPaused(false);
		g_phase = BUILD; g_deathReset = false;
		g_retryActive = true; g_retryStart = Time();
		snprintf(g_status, sizeof(g_status), "died x%d - rebuild + Go", g_deaths);
	}

	g_buildRemain = 0;
	if (!in_level()) { if (g_phase != IDLE) { g_phase = IDLE; snprintf(g_status, sizeof(g_status), "idle"); } return; }

	bool inMatch = (g_matched || g_matchActive);
	bool timed   = g_buildSeconds > 0 && inMatch;
	bool simulating = World_IsSimulating();
	bool cine    = (g_showdownKind != 0 && Time() < g_showdownUntil);

	// block early Go: sim may only start when synced build timer fires
	if (timed && g_phase != SIM && !g_forcedStart && simulating) {
		abort_early_sim(); simulating = false;
		snprintf(g_status, sizeof(g_status), "wait for Go");
	}
	if (inMatch) lock_match_speed();   // no fast-forward / pause-cheese
	if (g_matched) { set_match_buttons_hidden(true); g_buttonsHidden = true; }
	else if (g_buttonsHidden) { set_match_buttons_hidden(false); g_buttonsHidden = false; }

	if (g_phase != SIM && simulating) begin_sim(g_resets > 0);
	else if (g_phase == SIM && !simulating && g_finishTick < 0) { g_phase = BUILD; snprintf(g_status, sizeof(g_status), "build"); }

	if (g_phase == BUILD && inMatch) {
		if (cine) {   // freeze countdown during cinematic (pin start each frame)
			g_buildStart = Time(); g_retryStart = Time();
			g_buildRemain = g_retryActive ? g_retrySeconds : g_buildSeconds;
			g_lastBuildTick = -1;
		} else if (g_retryActive) {   // retry shot clock: auto-Go at 0, early manual Go allowed
			g_buildRemain = g_retrySeconds - (Time() - g_retryStart);
			if (g_buildRemain <= 0) { g_retryActive = false; force_start_sim(); }
		} else if (timed && !g_forcedStart) {   // initial synced build: auto-Go at 0
			g_buildRemain = g_buildSeconds - (Time() - g_buildStart);
			if (g_buildRemain <= 0) { g_forcedStart = true; force_start_sim(); }
		}
		if (!cine && (g_retryActive || (timed && !g_forcedStart))) countdown_beep(g_buildRemain, g_lastBuildTick, 5);
		else if (!cine) g_lastBuildTick = -1;
	} else g_lastBuildTick = -1;
	// round cap (anti-stall): too long without winning -> DNF
	if (!cine && g_matched && !g_interRound && g_finishTick < 0 && g_roundStart > 0 && g_roundCapSeconds > 0) {
		double roundLeft = g_roundCapSeconds - (Time() - g_roundStart);
		countdown_beep(roundLeft, g_lastRoundTick, 10);
		if (roundLeft < 0) report_finish(false);
	} else g_lastRoundTick = -1;

	if (g_phase == SIM && simulating && !World_IsPaused()) {
		Object* e = World_GetEets();
		if (e) {                               // skip when Eets isn't live yet (avoids garbage 0,0)
			Vector2 ep = Object_GetPosition(e);
			if (valid_pos(ep.x, ep.y)) {
				if (g_matched) {   // stream pos + anim so opponent's ghost mirrors our Eets
					char emo, mot; int flip; read_eets_anim(e, emo, mot, flip);
					char pb[80]; snprintf(pb, sizeof(pb), "pos %ld %.1f %.1f %c %c %d", g_tick, ep.x, ep.y, emo, mot, flip);
					net_sendline(pb);
				}
				if (g_tick / HASH_INTERVAL > g_lastHashBucket) {   // one sample per interval bucket
					g_lastHashBucket = g_tick / HASH_INTERVAL;
					g_samples.push_back({ g_tick, ep.x, ep.y, state_hash() });
				}
			}
		}
		// advance from engine sim-tick (catches sub-stepping); fall back to ++ if counter unreadable
		long et = Engine_GetSimTick();
		g_tick = (et >= 0 && g_engineTickBase >= 0) ? (et - g_engineTickBase) : (g_tick + 1);
		// DNF watchdog (match only): sim that neither completes nor kills must resolve or relay waits forever
		if (g_matched && g_finishTick < 0 && g_tick > g_simMaxTicks) report_finish(false);
	}
}
