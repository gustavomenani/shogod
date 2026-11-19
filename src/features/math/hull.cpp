#include "features/math/hull.hpp"

#include <algorithm>

namespace shogod::math {

float default_hitbox_radius(hitgroup group) noexcept {
    // Source hitbox radii, in world units. Overridden per weapon by the
    // schema hitbox data once the SDK is live.
    switch (group) {
        case hitgroup::head:      return 2.0f;
        case hitgroup::neck:      return 1.6f;
        case hitgroup::chest:     return 4.4f;
        case hitgroup::stomach:   return 4.0f;
        case hitgroup::left_arm:
        case hitgroup::right_arm: return 2.2f;
        case hitgroup::left_leg:
        case hitgroup::right_leg: return 2.8f;
        case hitgroup::gear:      return 3.0f;
        case hitgroup::generic:
        default:                  return 3.5f;
    }
}

vec3 safe_point(const vec3& bone, const vec3& direction, float radius, float fraction) noexcept {
    const float t = std::clamp(fraction, 0.0f, 1.0f);
    const vec3 dir = direction.normalised();
    return bone + dir * (radius * t);
}

}  // namespace shogod::math
