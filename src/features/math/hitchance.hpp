#pragma once

// Spread-aware hit chance: evaluates deterministic weapon spread offsets against
// either a single sphere or the full multi-hitbox capsule/OBB set of a target.

#include <array>
#include <span>

#include "features/math/hitbox_geom.hpp"
#include "features/math/vec2.hpp"
#include "features/math/vec3.hpp"

namespace shogod::math {

class weapon_spread_pattern {
public:
    weapon_spread_pattern(float spread, float inaccuracy) noexcept;
    [[nodiscard]] float chance(const vec3& eye, const vec3& aim_point, const vec3& hitbox_center,
                               float radius) const noexcept;
    [[nodiscard]] float chance_multi(const vec3& eye, const vec3& aim_point,
                                     std::span<const hitbox_geometry> hitboxes,
                                     int desired_group = -1) const noexcept;
    [[nodiscard]] float chance_multi(const vec3& eye, const vec3& aim_point,
                                     std::span<const hitbox_geometry> hitboxes,
                                     int desired_group, bool strict,
                                     float hitbox_scale = -1.0f) const noexcept;

private:
    static constexpr int sample_count = 256;
    std::array<vec2, sample_count> offsets_{};
};

[[nodiscard]] float safe_point_hitbox_scale(float safe_point_pct, bool strict) noexcept;

float hitchance(const vec3& eye, const vec3& aim_point, const vec3& hitbox_center,
                float radius, float half_angle_deg, int samples = 256) noexcept;

float hitchance(const vec3& eye, const vec3& target, float radius, float half_angle_deg,
                int samples = 256) noexcept;

}  // namespace shogod::math
