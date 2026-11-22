#include "features/math/hitchance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace shogod::math {
namespace {

constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_golden_angle = 2.399963229728653f;

bool ray_hits_sphere(const vec3& origin, const vec3& dir, const vec3& centre, float radius) {
    const vec3 oc = centre - origin;
    const float t = oc.dot(dir);
    if (t < 0.0f) return false;
    const float dist_sq = oc.cross(dir).length_sq();
    return dist_sq <= radius * radius;
}

class spread_random {
public:
    explicit spread_random(std::int32_t seed) noexcept : state_(seed) {
        for (int index = 39; index >= 0; --index) {
            advance();
            if (index < static_cast<int>(shuffle_.size())) shuffle_[index] = state_;
        }
        last_ = shuffle_[0];
    }

    float next() noexcept {
        advance();
        auto& entry = shuffle_[last_ >> 26];
        last_ = entry;
        entry = state_;
        return std::min(0x1.fffffcp-1f, static_cast<float>(last_) * 0x1p-31f);
    }

private:
    void advance() noexcept {
        state_ = static_cast<std::int32_t>((16807LL * state_) % 2147483647);
    }

    std::int32_t state_;
    std::int32_t last_{};
    std::array<std::int32_t, 32> shuffle_{};
};

}  // namespace

weapon_spread_pattern::weapon_spread_pattern(float spread, float inaccuracy) noexcept {
    struct sample {
        vec2 spread;
        vec2 inaccuracy;
    };
    static const auto unit_samples = [] {
        std::array<sample, sample_count> samples{};
        for (unsigned i = 1; i <= samples.size(); ++i) {
            spread_random random(static_cast<std::int32_t>(i));
            const float inaccuracy_radius = random.next();
            const float inaccuracy_angle = 2.0f * k_pi * random.next();
            const float spread_radius = random.next();
            const float spread_angle = 2.0f * k_pi * random.next();
            samples[i - 1] = {{spread_radius * std::cos(spread_angle), spread_radius * std::sin(spread_angle)},
                              {inaccuracy_radius * std::cos(inaccuracy_angle), inaccuracy_radius * std::sin(inaccuracy_angle)}};
        }
        return samples;
    }();
    for (std::size_t i = 0; i < offsets_.size(); ++i) {
        const auto& unit = unit_samples[i];
        offsets_[i] = {unit.spread.x * spread + unit.inaccuracy.x * inaccuracy,
                       unit.spread.y * spread + unit.inaccuracy.y * inaccuracy};
    }
}

float weapon_spread_pattern::chance(const vec3& eye, const vec3& aim_point, const vec3& hitbox_center,
                                    float radius) const noexcept {
    if (!eye.finite() || !aim_point.finite() || !hitbox_center.finite() ||
        !std::isfinite(radius) || radius <= 0.0f) return 0.0f;

    const auto center = hitbox_center - eye;
    const float radius_sq = radius * radius;
    const float distance_sq = center.length_sq();
    const auto axis = (aim_point - eye).normalised();
    if (!axis.finite() || axis.length_sq() < 0.5f) return 0.0f;
    if (distance_sq <= radius_sq) return 1.0f;
    if (aim_point.x == hitbox_center.x && aim_point.y == hitbox_center.y && aim_point.z == hitbox_center.z) {
        const float limit = radius_sq / (distance_sq - radius_sq);
        const auto hits = std::count_if(offsets_.begin(), offsets_.end(), [limit](const vec2& offset) {
            return offset.x * offset.x + offset.y * offset.y <= limit;
        });
        return static_cast<float>(hits) / static_cast<float>(sample_count);
    }
    const auto helper = std::fabs(axis.z) < 0.99f ? vec3{0.0f, 0.0f, 1.0f} : vec3{1.0f, 0.0f, 0.0f};
    const auto right = axis.cross(helper).normalised();
    const auto up = right.cross(axis);
    int hits = 0;
    for (const auto& offset : offsets_) {
        const auto ray = axis + right * offset.x + up * offset.y;
        if (center.dot(ray) >= 0.0f && center.cross(ray).length_sq() <= radius_sq * ray.length_sq()) {
            ++hits;
        }
    }
    return static_cast<float>(hits) / static_cast<float>(sample_count);
}

float weapon_spread_pattern::chance_multi(const vec3& eye, const vec3& aim_point,
                                          std::span<const hitbox_geometry> hitboxes,
                                          int desired_group) const noexcept {
    return chance_multi(eye, aim_point, hitboxes, desired_group, false, 1.0f);
}

float weapon_spread_pattern::chance_multi(const vec3& eye, const vec3& aim_point,
                                          std::span<const hitbox_geometry> hitboxes,
                                          int desired_group, bool strict,
                                          float hitbox_scale) const noexcept {
    if (!eye.finite() || !aim_point.finite() || hitboxes.empty()) return 0.0f;
    const vec3 axis = (aim_point - eye).normalised();
    if (!axis.finite() || axis.length_sq() < 0.5f) return 0.0f;

    const float scale = (hitbox_scale > 0.0f && std::isfinite(hitbox_scale))
                            ? std::clamp(hitbox_scale, 0.05f, 1.0f)
                            : (strict ? 0.85f : 1.0f);

    const vec3 helper = std::fabs(axis.z) < 0.99f ? vec3{0.0f, 0.0f, 1.0f} : vec3{1.0f, 0.0f, 0.0f};
    const vec3 right = axis.cross(helper).normalised();
    const vec3 up = right.cross(axis);

    int hits = 0;
    for (const auto& offset : offsets_) {
        const vec3 dir = (axis + right * offset.x + up * offset.y).normalised();
        bool hit = false;
        for (const auto& hb : hitboxes) {
            if (!hb.valid) continue;
            if (desired_group >= 0 && hb.group != desired_group) continue;
            if (ray_hits_geometry_scaled(eye, dir, hb, scale)) {
                hit = true;
                break;
            }
        }
        // When !strict, only count adjacent full-damage torso hitboxes (1=chest/neck,
        // 2=stomach, 3=pelvis) when aiming at torso — never arms (5), legs (4), or head (0).
        if (!hit && !strict && desired_group >= 1 && desired_group <= 3) {
            for (const auto& hb : hitboxes) {
                if (!hb.valid || hb.group == desired_group || hb.group < 1 || hb.group > 3) continue;
                if (ray_hits_geometry_scaled(eye, dir, hb, scale)) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit) ++hits;
    }
    return static_cast<float>(hits) / static_cast<float>(sample_count);
}

float safe_point_hitbox_scale(float safe_point_pct, bool strict) noexcept {
    const float sp = std::isfinite(safe_point_pct) ? std::clamp(safe_point_pct / 100.0f, 0.0f, 1.0f) : 0.0f;
    return (strict ? 0.85f : 1.0f) * (1.0f - 0.25f * sp);
}

float hitchance(const vec3& eye, const vec3& aim_point, const vec3& hitbox_center,
                float radius, float half_angle_deg, int samples) noexcept {
    if (samples <= 0 || !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(half_angle_deg) || !eye.finite() || !aim_point.finite() ||
        !hitbox_center.finite()) return 0.0f;

    const vec3 axis = (aim_point - eye).normalised();
    if (axis.length_sq() < 1e-12f) return 0.0f;

    const float half = std::clamp(half_angle_deg, 0.0f, 89.0f) * k_pi / 180.0f;
    const float sin_half = std::sin(half * 0.5f);
    const float cone_depth = 2.0f * sin_half * sin_half;

    if (aim_point.x == hitbox_center.x && aim_point.y == hitbox_center.y &&
        aim_point.z == hitbox_center.z) {
        const float distance_sq = (hitbox_center - eye).length_sq();
        const float radius_sq = radius * radius;
        if (distance_sq <= radius_sq || cone_depth == 0.0f) return 1.0f;
        const float radius_ratio = radius_sq / distance_sq;
        const float hitbox_depth = radius_ratio / (1.0f + std::sqrt(1.0f - radius_ratio));
        const float cone_fraction = hitbox_depth / cone_depth;
        const float hits = std::clamp(std::floor(cone_fraction * samples + 0.5f),
                                      0.0f, static_cast<float>(samples));
        return hits / static_cast<float>(samples);
    }

    const vec3 helper = std::fabs(axis.z) < 0.99f ? vec3{0.0f, 0.0f, 1.0f} : vec3{1.0f, 0.0f, 0.0f};
    const vec3 right = axis.cross(helper).normalised();
    const vec3 up = right.cross(axis);

    int hits = 0;
    for (int i = 0; i < samples; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
        const float depth = u * cone_depth;
        const float cos_theta = 1.0f - depth;
        const float sin_theta = std::sqrt(depth * (2.0f - depth));
        const float phi = static_cast<float>(i) * k_golden_angle;

        const vec3 dir = axis * cos_theta + right * (std::cos(phi) * sin_theta) +
                         up * (std::sin(phi) * sin_theta);
        if (ray_hits_sphere(eye, dir, hitbox_center, radius)) ++hits;
    }
    return static_cast<float>(hits) / static_cast<float>(samples);
}

float hitchance(const vec3& eye, const vec3& target, float radius, float half_angle_deg,
                int samples) noexcept {
    return hitchance(eye, target, target, radius, half_angle_deg, samples);
}

}  // namespace shogod::math
