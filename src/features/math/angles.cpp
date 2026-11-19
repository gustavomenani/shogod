#include "features/math/angles.hpp"

#include <algorithm>
#include <cmath>

namespace shogod::math {
namespace {
constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_rad = k_pi / 180.0f;
}  // namespace

float normalize_yaw(float yaw_deg) noexcept {
    if (!std::isfinite(yaw_deg)) return 0.0f;
    float yaw = std::fmod(yaw_deg, 360.0f);
    if (yaw <= -180.0f) yaw += 360.0f;
    if (yaw > 180.0f) yaw -= 360.0f;
    return yaw;
}

float clamp_pitch(float pitch_deg) noexcept {
    if (!std::isfinite(pitch_deg)) return 0.0f;
    return std::clamp(pitch_deg, -89.0f, 89.0f);
}

vec3 forward_from_angles(const angles& a) noexcept {
    const float pitch = clamp_pitch(a.pitch) * k_rad;
    const float yaw = normalize_yaw(a.yaw) * k_rad;
    const float cp = std::cos(pitch);
    return {cp * std::cos(yaw), cp * std::sin(yaw), -std::sin(pitch)};
}

angles angles_from_forward(const vec3& forward) noexcept {
    if (!forward.finite() || forward.length_sq() < 1e-12f) return {};
    const vec3 f = forward.normalised();
    angles out;
    out.pitch = clamp_pitch(std::asin(-f.z) / k_rad);
    out.yaw = normalize_yaw(std::atan2(f.y, f.x) / k_rad);
    return out;
}

float yaw_delta(float from_deg, float to_deg) noexcept {
    return normalize_yaw(to_deg - from_deg);
}

}  // namespace shogod::math
