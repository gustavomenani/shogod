#pragma once

// Multi-point / safe-point geometry. A hitbox is modelled as a sphere of the
// hitbox radius around the bone origin; safe fraction 0 aims the centre,
// 1 aims the surface. The radius that matters comes from the schema hitbox
// set at runtime; the table here is only the sane fallback.

#include "features/math/vec3.hpp"

namespace shogod::math {

enum class hitgroup : int {
    generic = 0,
    head = 1,
    chest = 2,
    stomach = 3,
    left_arm = 4,
    right_arm = 5,
    left_leg = 6,
    right_leg = 7,
    neck = 8,
    gear = 10,
};

float default_hitbox_radius(hitgroup group) noexcept;

// Point at `fraction` of the way from the bone origin toward the surface along
// `direction`. 0 -> exact bone, 1 -> bone + direction * radius.
vec3 safe_point(const vec3& bone, const vec3& direction, float radius, float fraction) noexcept;

}  // namespace shogod::math
