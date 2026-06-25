// hash.h - FNV-1a 64 snapshot of the live object set, for determinism / desync detection.
// Same-build only: cross-platform hashes differ (FP physics is not bit-identical across OS).
#pragma once
#include "state.h"

static inline uint64_t fnv_mix(uint64_t h, const void* p, size_t n) {
	const uint8_t* b = (const uint8_t*)p;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}
static uint64_t state_hash() {
	uint64_t h = 1469598103934665603ULL;
	ForEachObject([&](Object* o) {
		if (!o) return;
		unsigned long id = Object_GetID(o), bp = Object_GetBlueprintHash(o);
		Vector2 p = Object_GetPosition(o), v = Object_GetVelocity(o);
		h = fnv_mix(h, &id, sizeof(id)); h = fnv_mix(h, &bp, sizeof(bp));
		h = fnv_mix(h, &p.x, sizeof(float)); h = fnv_mix(h, &p.y, sizeof(float));
		h = fnv_mix(h, &v.x, sizeof(float)); h = fnv_mix(h, &v.y, sizeof(float));
	});
	return h;
}
