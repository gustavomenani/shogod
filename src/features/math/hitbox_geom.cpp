#include "features/math/hitbox_geom.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace shogod::math {
namespace {

float dist_sq_point_segment(const vec3& p, const vec3& a, const vec3& b) noexcept {
    const vec3 ab = b - a;
    const float len_sq = ab.length_sq();
    if (len_sq <= 1e-8f) return (p - a).length_sq();
    const float t = std::clamp((p - a).dot(ab) / len_sq, 0.0f, 1.0f);
    const vec3 proj = a + ab * t;
    return (p - proj).length_sq();
}

float dist_sq_ray_segment(const vec3& ray_origin, const vec3& ray_dir, const vec3& seg_a,
                          const vec3& seg_b) noexcept {
    const vec3 u = ray_dir;
    const vec3 v = seg_b - seg_a;
    const vec3 w = ray_origin - seg_a;
    const float a = u.dot(u);
    const float b = u.dot(v);
    const float c = v.dot(v);
    const float d = u.dot(w);
    const float e = v.dot(w);
    const float denom = a * c - b * b;

    float sc = 0.0f;
    float tc = 0.0f;
    if (c <= 1e-8f) {
        tc = 0.0f;
        sc = (a > 1e-8f) ? std::max(0.0f, -d / a) : 0.0f;
    } else if (a <= 1e-8f) {
        sc = 0.0f;
        tc = std::clamp(e / c, 0.0f, 1.0f);
    } else {
        sc = (denom > 1e-6f) ? std::max(0.0f, (b * e - c * d) / denom) : 0.0f;
        const float t_num = e + b * sc;
        if (t_num < 0.0f) {
            tc = 0.0f;
            sc = std::max(0.0f, -d / a);
        } else if (t_num > c) {
            tc = 1.0f;
            sc = std::max(0.0f, (-d + b) / a);
        } else {
            tc = t_num / c;
        }
    }

    const vec3 closest_ray = ray_origin + u * sc;
    const vec3 closest_seg = seg_a + v * tc;
    return (closest_ray - closest_seg).length_sq();
}

}  // namespace

int hitgroup_to_group(int hitgroup) noexcept {
    // 0=generic/pelvis -> 3, 1=head -> 0, 2=chest/8=neck -> 1, 3=stomach -> 2,
    // 4/5=left/right arm -> 5, 6/7=left/right leg -> 4.
    static constexpr int groups[] = {3, 0, 1, 2, 5, 5, 4, 4, 1};
    if (hitgroup < 0 || hitgroup >= 9) return -1;
    return groups[hitgroup];
}

float effective_multipoint_scale(float multipoint_pct, float safe_point_pct) noexcept {
    if (!std::isfinite(multipoint_pct) || !std::isfinite(safe_point_pct)) return 0.0f;
    const float mp = std::clamp(multipoint_pct / 100.0f, 0.0f, 0.85f);
    const float sp = std::clamp(safe_point_pct / 100.0f, 0.0f, 1.0f);
    return mp * (1.0f - sp);
}

vec3 transform_point(const vec3& p, const mat3x4& tm) noexcept {
    return {
        p.x * tm.m[0][0] + p.y * tm.m[0][1] + p.z * tm.m[0][2] + tm.m[0][3],
        p.x * tm.m[1][0] + p.y * tm.m[1][1] + p.z * tm.m[1][2] + tm.m[1][3],
        p.x * tm.m[2][0] + p.y * tm.m[2][1] + p.z * tm.m[2][2] + tm.m[2][3],
    };
}

mat3x4 matrix_from_2x4(const float raw[8]) noexcept {
    // CS2 Matrix2x4_t stores position (x,y,z) + scale in row 0 and unit quaternion (x,y,z,w) in row 1.
    const float px = raw[0], py = raw[1], pz = raw[2], s = raw[3];
    const float x = raw[4], y = raw[5], z = raw[6], w = raw[7];

    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;

    mat3x4 out{};
    out.m[0][0] = (1.0f - 2.0f * (yy + zz)) * s;
    out.m[0][1] = 2.0f * (xy - wz) * s;
    out.m[0][2] = 2.0f * (xz + wy) * s;
    out.m[0][3] = px;

    out.m[1][0] = 2.0f * (xy + wz) * s;
    out.m[1][1] = (1.0f - 2.0f * (xx + zz)) * s;
    out.m[1][2] = 2.0f * (yz - wx) * s;
    out.m[1][3] = py;

    out.m[2][0] = 2.0f * (xz - wy) * s;
    out.m[2][1] = 2.0f * (yz + wx) * s;
    out.m[2][2] = (1.0f - 2.0f * (xx + yy)) * s;
    out.m[2][3] = pz;
    return out;
}

hitbox_geometry build_hitbox_geometry(const vec3& mins, const vec3& maxs, float radius,
                                      int shape_type, int hitgroup, bool foot,
                                      const mat3x4& tm, float scale) noexcept {
    hitbox_geometry g{};
    g.mins = mins;
    g.maxs = maxs;
    g.hitgroup = hitgroup;
    g.group = hitgroup_to_group(hitgroup);
    g.foot = foot;
    g.shape = static_cast<hitbox_shape>(std::clamp(shape_type, 0, 2));

    if (!mins.finite() || !maxs.finite() || !std::isfinite(radius) || !std::isfinite(scale) ||
        std::fabs(scale) <= 1e-6f || shape_type < 0 || shape_type > 2) {
        return g;
    }

    const vec3 col0{tm.m[0][0], tm.m[1][0], tm.m[2][0]};
    const vec3 col1{tm.m[0][1], tm.m[1][1], tm.m[2][1]};
    const vec3 col2{tm.m[0][2], tm.m[1][2], tm.m[2][2]};
    if (!col0.finite() || !col1.finite() || !col2.finite()) return g;
    const float len0 = col0.length();
    const float len1 = col1.length();
    const float len2 = col2.length();
    if (len0 > 1e-6f) g.axis_x = col0 / len0;
    if (len1 > 1e-6f) g.axis_y = col1 / len1;
    if (len2 > 1e-6f) g.axis_z = col2 / len2;

    const vec3 a = transform_point(mins, tm);
    const vec3 b = transform_point(shape_type == 1 ? mins : maxs, tm);
    if (!a.finite() || !b.finite()) return g;

    g.first = a;
    g.last = b;
    g.center = (a + b) * 0.5f;

    if (shape_type == 0) {
        if (len0 <= 1e-6f || len1 <= 1e-6f || len2 <= 1e-6f) return g;
        g.half_extents = {
            std::fabs(maxs.x - mins.x) * 0.5f * len0,
            std::fabs(maxs.y - mins.y) * 0.5f * len1,
            std::fabs(maxs.z - mins.z) * 0.5f * len2,
        };
        g.bound_radius = g.half_extents.length();
        g.radius = g.bound_radius;
        g.valid = g.center.finite() && g.axis_x.finite() && g.axis_y.finite() &&
                  g.axis_z.finite() && g.half_extents.finite() && std::isfinite(g.radius) &&
                  g.bound_radius > 0.01f;
        return g;
    }

    g.radius = std::max(0.0f, radius * std::fabs(scale));
    g.bound_radius = (b - a).length() * 0.5f + g.radius;
    g.valid = g.center.finite() && std::isfinite(g.radius) && g.bound_radius > 0.01f;
    return g;
}

std::size_t generate_hitbox_points(const hitbox_geometry& geom, const vec3& eye, float scale,
                                   std::array<vec3, 9>& out_points) noexcept {
    if (!geom.valid || !eye.finite()) return 0;

    std::size_t count = 0;
    out_points[count++] = geom.center;

    const float s = std::isfinite(scale) ? std::clamp(scale, 0.0f, 0.95f) : 0.0f;
    if (s <= 0.02f) return count;

    vec3 view = (geom.center - eye).normalised();
    if (view.length_sq() < 0.5f) view = {1.0f, 0.0f, 0.0f};
    const vec3 world_up = std::fabs(view.z) < 0.98f ? vec3{0.0f, 0.0f, 1.0f} : vec3{1.0f, 0.0f, 0.0f};
    const vec3 right = view.cross(world_up).normalised();
    const vec3 up = right.cross(view).normalised();

    if (geom.shape == hitbox_shape::box) {
        const std::array<vec3, 3> axes{geom.axis_x, geom.axis_y, geom.axis_z};
        const std::array<float, 3> ext{geom.half_extents.x * s, geom.half_extents.y * s,
                                       geom.half_extents.z * s};
        const std::array<float, 3> dots{
            std::fabs(view.dot(axes[0])),
            std::fabs(view.dot(axes[1])),
            std::fabs(view.dot(axes[2])),
        };
        int i_view = 0;
        if (dots[1] > dots[i_view]) i_view = 1;
        if (dots[2] > dots[i_view]) i_view = 2;
        const int i0 = (i_view + 1) % 3;
        const int i1 = (i_view + 2) % 3;

        vec3 u = axes[i0];
        if (u.dot(right) < 0.0f) u = u * -1.0f;
        const float eu = ext[i0];

        vec3 v = axes[i1];
        if (std::fabs(v.z) >= 1e-4f ? (v.z < 0.0f) : (v.dot(up) < 0.0f)) v = v * -1.0f;
        const float ev = ext[i1];

        if (eu > 0.05f && count + 2 <= out_points.size()) {
            out_points[count++] = geom.center + u * eu;
            out_points[count++] = geom.center - u * eu;
        }
        if (ev > 0.05f && count + 2 <= out_points.size()) {
            out_points[count++] = geom.center + v * ev;
            out_points[count++] = geom.center - v * ev;
        }
        if (eu > 0.05f && ev > 0.05f && count + 4 <= out_points.size()) {
            out_points[count++] = geom.center + u * eu + v * ev;
            out_points[count++] = geom.center - u * eu + v * ev;
            out_points[count++] = geom.center + u * eu - v * ev;
            out_points[count++] = geom.center - u * eu - v * ev;
        }
        return count;
    }

    const float r = geom.radius * s;
    if (r > 0.05f) {
        out_points[count++] = geom.center + right * r;
        out_points[count++] = geom.center - right * r;
        out_points[count++] = geom.center + up * r;
        if (geom.group == 0) {
            // Head: add pure world-Z crown + diagonal corners for +89 deg pitch-down / head-glitch peeks.
            out_points[count++] = geom.center + vec3{0.0f, 0.0f, r * 0.92f};
            out_points[count++] = geom.center + (right * 0.707f + up * 0.707f) * r;
            out_points[count++] = geom.center + (up * 0.707f - right * 0.707f) * r;
        } else {
            out_points[count++] = geom.center - up * r;
        }
    }

    if ((geom.last - geom.first).length_sq() > 1.0f && count + 2 <= out_points.size()) {
        const vec3 half = (geom.last - geom.first) * (0.5f * s);
        out_points[count++] = geom.center + half;
        out_points[count++] = geom.center - half;
    }

    return count;
}

bool ray_hits_geometry_scaled(const vec3& origin, const vec3& dir, const hitbox_geometry& geom,
                              float scale) noexcept {
    if (!geom.valid || !origin.finite() || !dir.finite() || !std::isfinite(scale) ||
        scale <= 0.0f) {
        return false;
    }
    const float s = std::clamp(scale, 0.05f, 1.0f);
    const float dir_len_sq = dir.length_sq();
    if (!std::isfinite(dir_len_sq) || dir_len_sq <= 1e-12f) return false;
    const vec3 d = std::fabs(dir_len_sq - 1.0f) > 1e-4f ? (dir / std::sqrt(dir_len_sq)) : dir;

    const vec3 to_center = geom.center - origin;
    const float proj = to_center.dot(d);
    const float br = geom.bound_radius * s;
    const float br_sq = br * br;
    if (proj < 0.0f && to_center.length_sq() > br_sq) return false;
    // Bounding-sphere reject eliminates ~90% of spread rays before expensive OBB/capsule math.
    const vec3 perp = to_center - d * std::max(0.0f, proj);
    if (perp.length_sq() > br_sq) return false;

    if (geom.shape == hitbox_shape::box) {
        const vec3 delta = origin - geom.center;
        float t_min = 0.0f;
        float t_max = std::numeric_limits<float>::infinity();
        const std::array<std::pair<vec3, float>, 3> slabs{{
            {geom.axis_x, geom.half_extents.x * s},
            {geom.axis_y, geom.half_extents.y * s},
            {geom.axis_z, geom.half_extents.z * s},
        }};
        for (const auto& [axis, half] : slabs) {
            const float o = delta.dot(axis);
            const float da = d.dot(axis);
            if (std::fabs(da) < 1e-6f) {
                if (o < -half || o > half) return false;
            } else {
                const float inv = 1.0f / da;
                float t1 = (-half - o) * inv;
                float t2 = (half - o) * inv;
                if (t1 > t2) std::swap(t1, t2);
                t_min = std::max(t_min, t1);
                t_max = std::min(t_max, t2);
                if (t_min > t_max) return false;
            }
        }
        return t_max >= 0.0f && t_min <= t_max;
    }

    const float r = geom.radius * s;
    if (geom.shape == hitbox_shape::sphere || (geom.last - geom.first).length_sq() < 1e-4f) {
        const float t = std::max(0.0f, proj);
        const vec3 closest = origin + d * t;
        return (closest - geom.center).length_sq() <= r * r;
    }

    const vec3 seg_a = geom.center + (geom.first - geom.center) * s;
    const vec3 seg_b = geom.center + (geom.last - geom.center) * s;
    return dist_sq_ray_segment(origin, d, seg_a, seg_b) <= r * r;
}

bool ray_hits_geometry(const vec3& origin, const vec3& dir, const hitbox_geometry& geom) noexcept {
    return ray_hits_geometry_scaled(origin, dir, geom, 1.0f);
}

}  // namespace shogod::math
