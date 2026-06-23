// determinism.h - pin the engine's RNG seed + deterministic mode + fixed timestep, lock vanilla
// game speed, and force-start the sim. See docs/hop-on-eets-spec.md Part 5.
#pragma once
#include "state.h"

#ifdef _WIN32
// Eets.exe (Eets1/TBW) globals from Ghidra. VA = GetModuleHandle + RVA, RVA = VA - 0x400000.
// VERIFY against the running build-id (the loader logs it) before trusting these.
static void pin_seed_globals(uint32_t seed) {
	uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
	volatile uint8_t* detmode = (uint8_t*)(base + 0xae3bf0);  // DAT_00ee3bf0: 0=libc rand(), nonzero=Park-Miller
	volatile int32_t* prng    = (int32_t*)(base + 0x1a1790);  // DAT_005a1790: Park-Miller integer state
	*detmode = 1;
	*prng    = (int32_t)(seed ? seed : 1u);
}
#else
static void pin_seed_globals(uint32_t /*seed*/) { /* TODO(M2): RE Linux det-mode/seed globals */ }
#endif

static void engage_determinism() {
	World_SetFPS(TICK_RATE);
	World_SetGameSpeed(1);                 // normal; re-asserted every sim frame
	if (g_pinSeed) pin_seed_globals(g_seed);
}

// force the sim to start: ResetSimulation clears the Simulator "simulating" flag [sim+0xb8] and the
// per-step gate (FUN_00536440) reads it - so set it. Best-effort (the clean Start fn is still TODO).
static void force_start_sim() {
	void* sim = FC<void*()>(addr::Simulator_i)();
	if (sim) { *((volatile unsigned char*)sim + 0xb8) = 1; World_SetGameSpeed(1); }
}
