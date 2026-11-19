#include "features/math/hitchance.hpp"

#include <algorithm>
#include <cmath>

namespace shogod::math {
namespace {

constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_golden_angle = 2.399963229728653f;  // radians

bool ray_hits_sphere(const vec3& origin, const vec3& dir, const vec3& centre, float radius) {
    const vec3 oc = centre - origin;
    const float t = oc.dot(dir);
    if (t < 0.0f) return false;
    const float dist_sq = oc.length_sq() - t * t;
    return dist_sq <= radius * radius;
}

}  // namespace

float hitchance(const vec3& eye, const vec3& target, float radius, float half_angle_deg,
                int samples) noexcept {
    if (samples <= 0 || radius <= 0.0f || !eye.finite() || !target.finite()) return 0.0f;

    const vec3 axis = (target - eye).normalised();
    if (axis.length_sq() < 1e-12f) return 0.0f;

    const float half = std::clamp(half_angle_deg, 0.0f, 89.0f) * k_pi / 180.0f;
    const float cos_max = std::cos(half);

    // Orthonormal basis around the aim axis.
    const vec3 helper = std::fabs(axis.z) < 0.99f ? vec3{0.0f, 0.0f, 1.0f} : vec3{1.0f, 0.0f, 0.0f};
    const vec3 right = axis.cross(helper).normalised();
    const vec3 up = right.cross(axis);

    int hits = 0;
    for (int i = 0; i < samples; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
        const float cos_theta = 1.0f - u * (1.0f - cos_max);
        const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
        const float phi = static_cast<float>(i) * k_golden_angle;

        const vec3 dir = axis * cos_theta + right * (std::cos(phi) * sin_theta) +
                         up * (std::sin(phi) * sin_theta);
        if (ray_hits_sphere(eye, dir, target, radius)) ++hits;
    }
    return static_cast<float>(hits) / static_cast<float>(samples);
}

}  // namespace shogod::math
