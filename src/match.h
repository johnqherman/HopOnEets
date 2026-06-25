// match.h - the competitive match lifecycle: round resolution, the engine-event handlers, and the per-frame
// build/sim/retry/round-cap state machine. Pure logic - NO drawing (the HUD lives in hud.h). Pulls in the
// lower layers (recorder/net/determinism) so it must be #included after them.
#pragma once
#include "state.h"

// resolve the round, win OR lose. A match round MUST report a finish even on failure (Eets death / DNF) or
// the relay - which scores only when BOTH players have finished - stalls forever. Idempotent per round
// (guarded by g_finishTick, reset to -1 in begin_sim). completed=false => a failed run (relay decides a
// both-failed round as a tie; see netproto/relay.ts decideOutcome).
static void report_finish(bool completed) {
	if (g_finishTick >= 0 || g_phase == IDLE) return;   // once per round; allows SIM (win/DNF) or BUILD (round-cap DNF)
	g_finishTick = g_tick + g_deathTicks;   // total round time incl. time lost to deaths (the dying penalty)
	Eets::Log("hop_on_eets: report_finish completed=%d tick=%ld matched=%d ranked=%d", completed ? 1 : 0, g_finishTick, g_matched ? 1 : 0, g_ranked ? 1 : 0);
	report_determinism();
	if (g_matched) {   // the relay scores the series from both players' finishes (it's authoritative online)
		char fb[80]; snprintf(fb, sizeof(fb), "finish %ld %d %d %d", g_finishTick, completed ? 1 : 0, placed_count(), g_deaths);
		net_sendline(fb);
	} else {           // solo practice: just note the time (no recording, no ghost)
		snprintf(g_roundMsg, sizeof(g_roundMsg), "round: %.2fs", g_finishTick / (double)TICK_RATE);
	}
	g_roundCounter++;
	if (g_matched) {
		g_interRound = true;              // suppress overlay draws until the next round's level loads (avoids the
		                                  // vanilla victory-screen transition crashing our GraphicsEngine draws)
		void* cr = World_GetCreator();
		Creator_ClearWinEffect(cr);   // kill the deferred win-effect that would pop the "YOU DID IT!" dialog ~0.3s
		                              // later (survives the reused Builder across the next-round reload)
		Creator_StopAllModals(cr);    // dismiss any already-showing vanilla dialog so the match proceeds to the next round
	}
	snprintf(g_status, sizeof(g_status), completed ? "complete tick=%ld" : "failed tick=%ld", g_finishTick);
}

// engine event hooks (forwarded from EetsMod_OnEvent)
static void match_on_event(const char* name, void* a, void* b) {
	if (strcmp(name, "level_load") == 0) {
		begin_build();
	} else if (strcmp(name, "level_reset") == 0) {
		bool wasSim = (g_phase == SIM);
		g_resets++; g_phase = BUILD;
		// A player pressing Stop mid-sim resets the level (StopSimulation -> ResetSimulation -> here). Give it the
		// same retry shot clock as a death. Our OWN resets (death via g_deathReset, early-Go abort via g_selfStop)
		// are excluded; death sets its own clock. wasSim excludes the early-Go abort (fires during BUILD).
		if (g_matched && wasSim && !g_selfStop && !g_deathReset && g_finishTick < 0) {
			g_deathTicks += g_tick; g_retryActive = true; g_retryStart = Time();   // bank wasted time, no death tally
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
		report_finish(true);    // the real win signal (fires before the vanilla WinDialog); resolves the round
	} else if (strcmp(name, "level_complete") == 0) {
		report_finish(true);    // backup: only fires if the player clicks through the vanilla victory dialog
	} else if (strcmp(name, "eets_dying") == 0) {
		// In a match a death is NOT a round loss: count it, bank the time spent, and reset to build to retry.
		// The lost time (g_deathTicks, folded into finish_tick) + the deaths tiebreaker are the penalty. The
		// actual reset happens next Update (off the engine's death call stack). Practice keeps vanilla death.
		if (g_matched && g_phase == SIM) {
			g_deaths++; g_deathTicks += g_tick; g_deathReset = true;
			Creator_StopAllModals(World_GetCreator());   // hide the death dialog immediately
		}
	}
}

// beep once per whole second on the final 5s of a countdown (5,4,3,2,1). `last` tracks the last second
// announced so it fires once per tick; reset to -1 whenever the countdown is outside the (0,6) window.
static void countdown_beep(double remain, int& last, int fromSec) {
	if (remain > 0.0 && remain < (double)(fromSec + 1)) {
		int s = (int)remain; if (remain > (double)s) s++;   // ceil for positive = seconds remaining
		if (s >= 1 && s <= fromSec && s != last) { last = s; PlaySound("GUI Click 2"); }
	} else last = -1;
}

// end the series: leave the level back to the main menu (the relay already freed the match, so the player
// can re-queue from the menu). Called when the win screen's timer elapses.
static void end_series_to_menu() {
	g_winShow = false; g_winForfeit = false; g_matched = false; g_phase = IDLE; g_menuOpen = false;
	g_interRound = false; g_seriesOver = false; g_roundMsg[0] = 0;
	g_levelIndex = -1; g_liveValid = false; g_oppBuild.clear();
	World_StartMainMenu();
}

// per-frame match state machine (build timer, retry shot clock, round cap, early-Go block, speed/button lock,
// pos/hash streaming, DNF watchdog). Sets g_buildRemain for the HUD. Does NOT draw.
static void match_update() {
	if (g_winShow) { if (Time() >= g_winUntil) end_series_to_menu(); return; }   // win screen owns the frame
	// between rounds keep dismissing the vanilla victory/death dialog every frame: the win-effect timer
	// (Builder+0x2fa4) re-shows it ~1s after the win, so a one-shot dismiss at finish isn't enough. Cleared
	// when the next round's level loads (begin_build -> g_interRound=false; World_EnterLevel also stops modals).
	if (g_interRound && g_matched) { void* cr = World_GetCreator(); Creator_ClearWinEffect(cr); Creator_StopAllModals(cr); }
	if (g_deathReset) {   // a match Eets-death: reset the sim back to build (Eets respawns, contraption intact) so the player retries
		void* cr = World_GetCreator();
		g_selfStop = true; Creator_StopSimulation(cr); g_selfStop = false;   // our reset (its level_reset is not a player Stop)
		Creator_StopAllModals(cr); Simulator_SetPaused(false);
		g_phase = BUILD; g_deathReset = false;
		g_retryActive = true; g_retryStart = Time();   // retry shot clock: limited time to adjust, then auto-Go
		snprintf(g_status, sizeof(g_status), "died x%d - rebuild + Go", g_deaths);
	}

	g_buildRemain = 0;
	if (!in_level()) { if (g_phase != IDLE) { g_phase = IDLE; snprintf(g_status, sizeof(g_status), "idle"); } return; }

	bool inMatch = (g_matched || g_matchActive);
	bool timed   = g_buildSeconds > 0 && inMatch;
	bool simulating = World_IsSimulating();
	bool cine    = (g_showdownKind != 0 && Time() < g_showdownUntil);   // an opening/round cinematic is playing

	// block an early "Go": in a match the sim may only start when the synced build timer fires. If the
	// player presses Go before that, revert to build (Creator::StopSimulation restores the snapshot).
	if (timed && g_phase != SIM && !g_forcedStart && simulating) {
		abort_early_sim(); simulating = false;
		snprintf(g_status, sizeof(g_status), "wait for Go");
	}
	if (inMatch) lock_match_speed();   // lock vanilla speed the whole match (no fast-forward / pause-cheese)
	if (g_matched) { set_match_buttons_hidden(true); g_buttonsHidden = true; }   // hide hint/solution/speed in a match
	else if (g_buttonsHidden) { set_match_buttons_hidden(false); g_buttonsHidden = false; }   // restore when the match ends

	if (g_phase != SIM && simulating) begin_sim(g_resets > 0);
	else if (g_phase == SIM && !simulating && g_finishTick < 0) { g_phase = BUILD; snprintf(g_status, sizeof(g_status), "build"); }

	if (g_phase == BUILD && inMatch) {
		if (cine) {   // freeze the build/retry countdown while the cinematic plays (pin the start each frame)
			g_buildStart = Time(); g_retryStart = Time();
			g_buildRemain = g_retryActive ? g_retrySeconds : g_buildSeconds;
			g_lastBuildTick = -1;
		} else if (g_retryActive) {   // retry shot clock (local, per-player): auto-Go at 0; early manual Go allowed (g_forcedStart already set)
			g_buildRemain = g_retrySeconds - (Time() - g_retryStart);
			if (g_buildRemain <= 0) { g_retryActive = false; force_start_sim(); }
		} else if (timed && !g_forcedStart) {   // initial synced build: auto-Go at 0 (early Go blocked above)
			g_buildRemain = g_buildSeconds - (Time() - g_buildStart);
			if (g_buildRemain <= 0) { g_forcedStart = true; force_start_sim(); }
		}
		if (!cine && (g_retryActive || (timed && !g_forcedStart))) countdown_beep(g_buildRemain, g_lastBuildTick, 5);
		else if (!cine) g_lastBuildTick = -1;
	} else g_lastBuildTick = -1;
	// round time cap (anti-stall): too long without winning -> DNF this round (counts as a failed finish)
	if (!cine && g_matched && !g_interRound && g_finishTick < 0 && g_roundStart > 0 && g_roundCapSeconds > 0) {
		double roundLeft = g_roundCapSeconds - (Time() - g_roundStart);
		countdown_beep(roundLeft, g_lastRoundTick, 10);   // tick the final 10s of the round clock
		if (roundLeft < 0) report_finish(false);
	} else g_lastRoundTick = -1;

	if (g_phase == SIM && simulating && !World_IsPaused()) {
		Object* e = World_GetEets();
		if (e) {                               // skip when Eets isn't live yet (avoids garbage 0,0/junk)
			Vector2 ep = Object_GetPosition(e);
			if (valid_pos(ep.x, ep.y)) {
				if (g_matched) {   // stream pos + anim state so the opponent's ghost mirrors our Eets
					char emo, mot; int flip; read_eets_anim(e, emo, mot, flip);
					char pb[80]; snprintf(pb, sizeof(pb), "pos %ld %.1f %.1f %c %c %d", g_tick, ep.x, ep.y, emo, mot, flip);
					net_sendline(pb);
				}
				if (g_tick / HASH_INTERVAL > g_lastHashBucket) {   // one sample per interval bucket (jump-proof; == g_tick%==0 when ticks step by 1)
					g_lastHashBucket = g_tick / HASH_INTERVAL;
					g_samples.push_back({ g_tick, ep.x, ep.y, state_hash() });   // for the determinism self-test + same-platform desync hashes
					// NO live cross-player hash compare: this is a solution RACE, so the two players build
					// different contraptions and their state hashes legitimately differ every tick. Comparing
					// them flagged a false "desync" -> no_contest. Same-platform desync detection (Part 10) is
					// the only cross-client hash check; the relay scores the series from reported finishes.
				}
			}
		}
		// Advance the round tick from the TRUE engine sim-tick (count of deterministic frame advances since
		// sim start) when the counter is available - catches engine sub-stepping a per-Update ++ would miss
		// and keeps the desync-hash tick aligned with the engine. Fall back to ++ if the counter is unreadable.
		long et = Engine_GetSimTick();
		g_tick = (et >= 0 && g_engineTickBase >= 0) ? (et - g_engineTickBase) : (g_tick + 1);
		// DNF watchdog (match only): a sim that neither completes nor kills the Eets must still resolve
		// the round, or the relay waits forever. begin_sim resets g_tick, so this is per round.
		if (g_matched && g_finishTick < 0 && g_tick > g_simMaxTicks) report_finish(false);
	}
}
