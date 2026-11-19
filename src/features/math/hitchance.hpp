#pragma once

// Spread-aware hit chance: concentrates `samples` rays inside the weapon's
// spread cone and counts how many hit the target sphere. Deterministic (fixed
// golden-angle sequence), allocation-free, safe to call per tick.

#include "features/math/vec3.hpp"

namespace shogod::math {

// Returns a probability in [0, 1].
float hitchance(const vec3& eye, const vec3& target, float radius, float half_angle_deg,
                int samples = 256) noexcept;

}  // namespace shogod::math
