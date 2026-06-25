#pragma once
#include "state.h"

// addr::DetMode_flag / addr::PRNG_seed; 0 = unavailable. Mainly a Windows path: Linux reseeds on StartSimulation.
static void pin_seed_globals(uint32_t seed) {
	if (!addr::DetMode_flag || !addr::PRNG_seed) return;
	*(volatile uint8_t*)addr::DetMode_flag = 1;
	*(volatile int32_t*)addr::PRNG_seed    = (int32_t)(seed ? seed : 1u);
}

static void engage_determinism() {
	World_SetFPS(TICK_RATE);
	World_SetGameSpeed(1);
	if (g_pinSeed) pin_seed_globals(g_seed);
}

// Prefer Creator::StartSimulation (full Go init: input recording, stat tags, releases the Eets);
// raw Simulator path is the fallback where Creator addr isn't RE'd.
static void force_start_sim() {
	Creator_CancelAction(World_GetCreator());   // else StartSimulation no-ops ("...while in action")
	if (!Creator_StartSimulation(World_GetCreator())) {
		void* sim = FC<void*()>(addr::Simulator_i)();
		if (!sim) return;
		if (addr::Simulator_StartSimulation) Simulator_StartSimulation(sim);
		else *((volatile unsigned char*)sim + 0xb8) = 1;   // +0xb8 = sim-running flag (Win)
	}
	if (g_matched || g_pinSeed) pin_seed_globals(g_seed);  // override engine's fixed reseed with match seed
	World_SetGameSpeed(1);
}

static void lock_match_speed() { World_SetGameSpeed(1); }

static const char* const MATCH_LOCKED_WIDGETS[] = {
	"ShowHintablesButton", "ShowHintablesButtonBG", "ShowSolutionButton", "ShowSolutionButtonBG",
	"SpeedFastButton", "SpeedMediumButton", "SpeedSlowButton",
};
static void set_match_buttons_hidden(bool hidden) {
	void* cr = World_GetCreator(); if (!cr) return;
	for (const char* n : MATCH_LOCKED_WIDGETS) Creator_SetWidgetHidden(cr, n, hidden);
}

// Creator::StopSimulation -> ResetSimulation restores initial snapshot (reverts running sim back to build).
static void abort_early_sim() { g_selfStop = true; Creator_StopSimulation(World_GetCreator()); g_selfStop = false; }
