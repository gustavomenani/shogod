#pragma once

// Pure hitbox capsule/OBB geometry and multipoint generation.
// Independent of game memory so tests can verify math directly.

#include "features/math/vec3.hpp"

#include <array>
#include <cstddef>

namespace shogod::math {

enum class hitbox_shape : int { box = 0, sphere = 1, capsule = 2 };

struct hitbox_geometry {
    vec3 mins{};
    vec3 maxs{};
    vec3 first{};
    vec3 last{};
    vec3 center{};
    vec3 axis_x{1.0f, 0.0f, 0.0f};
    vec3 axis_y{0.0f, 1.0f, 0.0f};
    vec3 axis_z{0.0f, 0.0f, 1.0f};
    vec3 half_extents{};
    float radius = 0.0f;
    float bound_radius = 0.0f;
    int hitgroup = 0;
    int group = -1;       // 0=head, 1=neck/chest, 2=stomach, 3=pelvis, 4=legs, 5=arms
    hitbox_shape shape = hitbox_shape::capsule;
    bool foot = false;
    bool valid = false;
};

struct mat3x4 {
    float m[3][4]{};
};

[[nodiscard]] int hitgroup_to_group(int hitgroup) noexcept;
[[nodiscard]] float effective_multipoint_scale(float multipoint_pct, float safe_point_pct) noexcept;

vec3 transform_point(const vec3& p, const mat3x4& tm) noexcept;
mat3x4 matrix_from_2x4(const float raw[8]) noexcept;

hitbox_geometry build_hitbox_geometry(const vec3& mins, const vec3& maxs, float radius,
                                      int shape_type, int hitgroup, bool foot,
                                      const mat3x4& tm, float scale) noexcept;

// Generates up to 9 world-space aim points (index 0 = center, 1..8 = multipoints)
// oriented toward `eye`. Returns the number of points written.
std::size_t generate_hitbox_points(const hitbox_geometry& geom, const vec3& eye, float scale,
                                   std::array<vec3, 9>& out_points) noexcept;

// Exact ray-to-capsule / ray-to-sphere / ray-to-OBB intersection test.
bool ray_hits_geometry(const vec3& origin, const vec3& dir, const hitbox_geometry& geom) noexcept;
bool ray_hits_geometry_scaled(const vec3& origin, const vec3& dir, const hitbox_geometry& geom,
                              float scale) noexcept;

}  // namespace shogod::math
