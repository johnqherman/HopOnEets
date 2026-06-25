// determinism.h - pin the engine's RNG seed + deterministic mode + fixed timestep, lock vanilla
// game speed, and force-start the sim.
#pragma once
#include "state.h"

// pin the engine RNG seed + deterministic mode. Globals live in the framework address maps
// (addr::DetMode_flag / addr::PRNG_seed); 0 = unavailable on this build -> skip. (On Linux the engine
// reseeds on StartSimulation anyway, so this is mainly a Windows path.)
static void pin_seed_globals(uint32_t seed) {
	if (!addr::DetMode_flag || !addr::PRNG_seed) return;
	*(volatile uint8_t*)addr::DetMode_flag = 1;
	*(volatile int32_t*)addr::PRNG_seed    = (int32_t)(seed ? seed : 1u);
}

static void engage_determinism() {
	World_SetFPS(TICK_RATE);
	World_SetGameSpeed(1);                 // normal; re-asserted every sim frame
	if (g_pinSeed) pin_seed_globals(g_seed);
}

// force the sim to start (build timer), exactly as pressing "Go" does. Prefer the active builder's real
// Go (Creator::StartSimulation) - it does the full init (input recording, stat tags, releases the Eets);
// the raw Simulator path is only a fallback (e.g. Win, where the Creator addr isn't RE'd yet).
static void force_start_sim() {
	Creator_CancelAction(World_GetCreator());   // clean up any in-progress drag, else StartSimulation no-ops ("...while in action")
	if (!Creator_StartSimulation(World_GetCreator())) {     // real Go on the active Builder
		void* sim = FC<void*()>(addr::Simulator_i)();
		if (!sim) return;
		if (addr::Simulator_StartSimulation) Simulator_StartSimulation(sim);
		else *((volatile unsigned char*)sim + 0xb8) = 1;   // last-resort flag-flip (Win)
	}
	if (g_matched || g_pinSeed) pin_seed_globals(g_seed);  // override the engine's fixed reseed with the match seed
	World_SetGameSpeed(1);
}

// lock vanilla game speed for the whole match (build + sim): no fast-forward, no pause-cheese. Re-asserted
// every frame so the engine's speed buttons / hotkeys can't change it. (0=paused, 1=normal, 2=fast.)
static void lock_match_speed() { World_SetGameSpeed(1); }

// hide/show the build-screen buttons that don't belong in a competitive match: hint, show-solution (anti-
// cheat) and the speed controls (speed is locked). Re-applied per frame while matched; restored on match end.
static const char* const MATCH_LOCKED_WIDGETS[] = {
	"ShowHintablesButton", "ShowHintablesButtonBG", "ShowSolutionButton", "ShowSolutionButtonBG",
	"SpeedFastButton", "SpeedMediumButton", "SpeedSlowButton",
};
static void set_match_buttons_hidden(bool hidden) {
	void* cr = World_GetCreator(); if (!cr) return;
	for (const char* n : MATCH_LOCKED_WIDGETS) Creator_SetWidgetHidden(cr, n, hidden);
}

// abort a player's early "Go": a match sim may only start when the synced build timer expires. Reverts the
// running sim back to build (Creator::StopSimulation -> ResetSimulation restores the initial snapshot).
static void abort_early_sim() { g_selfStop = true; Creator_StopSimulation(World_GetCreator()); g_selfStop = false; }
