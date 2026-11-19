#pragma once

// Euler angles in Source convention: yaw around Z (0 = +X, CCW positive),
// pitch positive looking down. All helpers reject non-finite input.

#include "features/math/vec3.hpp"

namespace shogod::math {

struct angles {
    float pitch{};  // [-89, 89]
    float yaw{};    // (-180, 180]
};

float normalize_yaw(float yaw_deg) noexcept;
float clamp_pitch(float pitch_deg) noexcept;

// Forward vector for (pitch, yaw).
vec3 forward_from_angles(const angles& a) noexcept;
angles angles_from_forward(const vec3& forward) noexcept;

// Shortest signed delta from `from` to `to` in (-180, 180].
float yaw_delta(float from_deg, float to_deg) noexcept;

}  // namespace shogod::math
