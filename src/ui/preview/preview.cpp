#include "ui/preview/preview.hpp"

#include "engine/log.hpp"
#include "features/math/mat4.hpp"
#include "sdk/bones.hpp"
#include "ui/cs2_weapon_icons.hpp"
#include "ui/preview/ctm_sas_mesh.hpp"
#include "ui/style.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace shogod::ui::preview {
namespace {

constexpr float k_pi = 3.14159265358979323846f;
constexpr UINT k_target_size = 512;

const char* k_shader_source = R"(
cbuffer Frame : register(b0) {
    row_major float4x4 view_proj;
    float4 color;
    float4 eye_pos;
    float4 params;   // x = shader mode (0=shaded,1=flat,2=glow,3=glossy), y = override_col, z = fresnel_exp, w = fresnel_falloff
    float4 outline;  // x = inflate, y = is_outline_pass
};

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOR;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOR;
    float3 world : TEXCOORD0;
};

VSOut vs_main(VSIn v) {
    VSOut o;
    float3 n = normalize(v.nrm);
    float3 p = v.pos + n * outline.x;
    o.pos = mul(float4(p, 1.0), view_proj);
    o.nrm = n;
    o.col = v.col;
    o.world = p;
    return o;
}

float4 ps_main(VSOut i) : SV_TARGET {
    float4 base = params.y > 0.5 ? color : float4(i.col.rgb, color.a);
    float3 n = normalize(i.nrm);
    float3 v = normalize(eye_pos.xyz - i.world);
    float ndotv = abs(dot(n, v));

    if (outline.y > 0.5) {
        float rim = pow(saturate(1.0 - ndotv), 1.25);
        float3 halo = lerp(base.rgb, float3(1.0, 0.96, 0.98), 0.78);
        return float4(halo, saturate(base.a * (0.34 + 0.58 * rim)));
    }

    float3 key_dir = normalize(float3(0.55, 0.45, 0.72));
    float3 fill_dir = normalize(float3(-0.65, -0.45, 0.32));
    float key = saturate(dot(n, key_dir));
    float fill = saturate(dot(n, fill_dir)) * 0.35;
    float shade = 0.36 + 0.60 * key + fill;

    int mode = (int)(params.x + 0.5);
    if (mode == 1) {
        float flat_rim = pow(saturate(1.0 - ndotv), 2.0) * 0.30;
        float3 flat_col = base.rgb * (0.76 + 0.28 * shade) + lerp(base.rgb, float3(1.0, 1.0, 1.0), 0.62) * flat_rim;
        return float4(saturate(flat_col), base.a);
    }

    if (mode == 2) {
        float f_exp = max(0.5, params.z);
        float f_fall = max(0.2, params.w);
        float fresnel = saturate(pow(saturate(1.0 - ndotv), f_exp) * (0.55 * f_fall));
        float3 core = base.rgb * (0.55 + 0.45 * shade);
        float3 glow_rim = lerp(base.rgb, float3(1.0, 0.95, 0.97), 0.78);
        return float4(saturate(lerp(core, glow_rim, fresnel)), base.a);
    }

    if (mode == 3) {
        float3 h = normalize(key_dir + v);
        float spec = pow(saturate(dot(n, h)), 28.0) * 0.65;
        float rim = pow(saturate(1.0 - ndotv), max(0.5, params.z)) * 0.40;
        float3 lit = base.rgb * shade + float3(1.0, 1.0, 1.0) * spec + base.rgb * rim;
        return float4(saturate(lit), base.a);
    }

    float rim = pow(saturate(1.0 - ndotv), 2.5) * 0.28;
    float3 lit = base.rgb * shade + lerp(base.rgb, float3(0.95, 0.97, 1.0), 0.35) * rim;
    return float4(saturate(lit), base.a);
}
)";

struct vertex {
    float pos[3];
    float nrm[3];
    float col[4];
};

struct cb_frame {
    float view_proj[16];
    float color[4];
    float eye_pos[4];
    float params[4];
    float outline[4];
};

void part_color(std::uint8_t part, float out[4]) noexcept {
    switch (part) {
        case 0:  // lenses
            out[0] = 0.32f; out[1] = 0.48f; out[2] = 0.58f; out[3] = 1.0f;
            break;
        case 1:  // head_gasmask
            out[0] = 0.17f; out[1] = 0.19f; out[2] = 0.22f; out[3] = 1.0f;
            break;
        case 2:  // bodylegs
            out[0] = 0.22f; out[1] = 0.25f; out[2] = 0.28f; out[3] = 1.0f;
            break;
        case 4:  // rifle metallic
            out[0] = 0.16f; out[1] = 0.18f; out[2] = 0.22f; out[3] = 1.0f;
            break;
        default:  // body (SAS navy jacket & vest)
            out[0] = 0.20f; out[1] = 0.28f; out[2] = 0.39f; out[3] = 1.0f;
            break;
    }
}

struct bone_frame {
    math::vec3 origin{};
    math::vec3 x{}, y{}, z{};
};

bone_frame make_frame(const math::vec3& a, const math::vec3& b, const math::vec3& pole_hint) noexcept {
    const math::vec3 x = (b - a).normalised();
    math::vec3 p = pole_hint - x * pole_hint.dot(x);
    if (p.length_sq() < 1e-6f) {
        p = std::fabs(x.z) < 0.9f ? math::vec3{0.0f, 0.0f, 1.0f} : math::vec3{1.0f, 0.0f, 0.0f};
        p = p - x * p.dot(x);
    }
    const math::vec3 z = p.normalised();
    const math::vec3 y = z.cross(x).normalised();
    return {a, x, y, z};
}

math::vec3 map_point(const bone_frame& rest, const bone_frame& posed, const math::vec3& p) noexcept {
    const math::vec3 d = p - rest.origin;
    const float lx = d.dot(rest.x);
    const float ly = d.dot(rest.y);
    const float lz = d.dot(rest.z);
    return posed.origin + posed.x * lx + posed.y * ly + posed.z * lz;
}

math::vec3 map_normal(const bone_frame& rest, const bone_frame& posed, const math::vec3& n) noexcept {
    const float lx = n.dot(rest.x);
    const float ly = n.dot(rest.y);
    const float lz = n.dot(rest.z);
    return (posed.x * lx + posed.y * ly + posed.z * lz).normalised();
}

float dist_to_segment(const math::vec3& p, const math::vec3& a, const math::vec3& b) noexcept {
    const math::vec3 ab = b - a;
    const float len2 = ab.length_sq();
    if (len2 < 1e-6f) return (p - a).length();
    const float t = std::clamp((p - a).dot(ab) / len2, 0.0f, 1.0f);
    return (p - (a + ab * t)).length();
}

math::vec3 solve_2bone_ik(const math::vec3& root, const math::vec3& target, float l1, float l2,
                          const math::vec3& bend_hint) noexcept {
    const math::vec3 delta = target - root;
    const float raw_d = std::max(1e-3f, delta.length());
    const float d = std::clamp(raw_d, std::fabs(l1 - l2) + 0.25f, l1 + l2 - 0.1f);
    const math::vec3 dir = delta / raw_d;

    const float cos_a = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
    const float sin_a = std::sqrt(std::max(0.0f, 1.0f - cos_a * cos_a));

    math::vec3 bend = bend_hint - dir * bend_hint.dot(dir);
    if (bend.length_sq() < 1e-6f) bend = math::vec3{0.0f, 0.0f, -1.0f};
    bend = bend.normalised();

    return root + dir * (l1 * cos_a) + bend * (l1 * sin_a);
}

void build_rest_joints(std::array<math::vec3, sdk::pj_count>& j) noexcept {
    j = {{
        {0.5f, 0.0f, 70.5f},    // 0 head
        {-0.5f, 0.0f, 63.0f},   // 1 neck
        {-0.5f, 0.0f, 51.0f},   // 2 chest
        {-0.5f, 0.0f, 40.0f},   // 3 pelvis
        {-1.0f, 8.0f, 58.0f},   // 4 l shoulder
        {-1.5f, 14.5f, 47.0f},  // 5 l elbow
        {1.0f, 20.5f, 36.0f},   // 6 l hand
        {-1.0f, -8.0f, 58.0f},  // 7 r shoulder
        {-1.5f, -14.5f, 47.0f}, // 8 r elbow
        {1.0f, -20.5f, 36.0f},  // 9 r hand
        {-0.5f, 4.5f, 37.5f},   // 10 l hip
        {0.5f, 5.5f, 20.5f},    // 11 l knee
        {-0.5f, 6.2f, 3.5f},    // 12 l ankle
        {-0.5f, -4.5f, 37.5f},  // 13 r hip
        {0.5f, -5.5f, 20.5f},   // 14 r knee
        {-0.5f, -6.2f, 3.5f},   // 15 r ankle
    }};
}

void build_stance_joints(stance s, std::array<math::vec3, sdk::pj_count>& j,
                         const std::array<math::vec3, sdk::pj_count>& rest) noexcept {
    j = rest;
    if (s == stance::a_pose) return;

    const float lua = (rest[sdk::pj_elbow_l] - rest[sdk::pj_shoulder_l]).length();
    const float lfa = (rest[sdk::pj_hand_l] - rest[sdk::pj_elbow_l]).length();
    const float rua = (rest[sdk::pj_elbow_r] - rest[sdk::pj_shoulder_r]).length();
    const float rfa = (rest[sdk::pj_hand_r] - rest[sdk::pj_elbow_r]).length();
    const float lth = (rest[sdk::pj_knee_l] - rest[sdk::pj_hip_l]).length();
    const float lsh = (rest[sdk::pj_ankle_l] - rest[sdk::pj_knee_l]).length();
    const float rth = (rest[sdk::pj_knee_r] - rest[sdk::pj_hip_r]).length();
    const float rsh = (rest[sdk::pj_ankle_r] - rest[sdk::pj_knee_r]).length();

    if (s == stance::standing) {
        j[sdk::pj_pelvis] = {-0.5f, 0.0f, 39.5f};
        j[sdk::pj_chest] = {0.8f, -0.2f, 50.5f};
        j[sdk::pj_neck] = {1.8f, -0.3f, 62.5f};
        j[sdk::pj_head] = {3.0f, -0.3f, 69.8f};
        j[sdk::pj_shoulder_l] = {2.2f, 7.5f, 57.5f};
        j[sdk::pj_shoulder_r] = {-0.8f, -7.8f, 57.5f};
        j[sdk::pj_hand_l] = {18.5f, -1.2f, 53.0f};
        j[sdk::pj_hand_r] = {10.5f, -4.4f, 52.6f};
        j[sdk::pj_hip_l] = {0.0f, 4.6f, 37.0f};
        j[sdk::pj_ankle_l] = {3.8f, 6.2f, 3.5f};
        j[sdk::pj_hip_r] = {-1.0f, -4.6f, 37.0f};
        j[sdk::pj_ankle_r] = {-4.2f, -7.0f, 3.5f};
    } else {
        // CS2 combat aim stance: low athletic wide stance holding M4A1-S horizontally toward +X.
        j[sdk::pj_pelvis] = {-1.2f, 0.0f, 31.2f};
        j[sdk::pj_chest] = {3.2f, -0.2f, 42.8f};
        j[sdk::pj_neck] = {6.6f, -0.3f, 54.0f};
        j[sdk::pj_head] = {9.0f, -0.3f, 61.0f};
        j[sdk::pj_shoulder_l] = {6.2f, 6.8f, 49.5f};
        j[sdk::pj_shoulder_r] = {0.8f, -7.8f, 49.2f};
        j[sdk::pj_hand_l] = {22.2f, -0.6f, 48.2f};
        j[sdk::pj_hand_r] = {13.2f, -4.0f, 47.8f};
        j[sdk::pj_hip_l] = {0.2f, 4.8f, 29.2f};
        j[sdk::pj_ankle_l] = {10.5f, 6.5f, 3.5f};
        j[sdk::pj_hip_r] = {-2.5f, -5.0f, 29.2f};
        j[sdk::pj_ankle_r] = {-10.5f, -7.8f, 3.5f};
    }

    j[sdk::pj_elbow_l] = solve_2bone_ik(j[sdk::pj_shoulder_l], j[sdk::pj_hand_l], lua, lfa,
                                        {-0.15f, 0.65f, -0.75f});
    j[sdk::pj_elbow_r] = solve_2bone_ik(j[sdk::pj_shoulder_r], j[sdk::pj_hand_r], rua, rfa,
                                        {-0.20f, -0.65f, -0.70f});
    j[sdk::pj_knee_l] = solve_2bone_ik(j[sdk::pj_hip_l], j[sdk::pj_ankle_l], lth, lsh,
                                       {0.92f, 0.25f, 0.15f});
    j[sdk::pj_knee_r] = solve_2bone_ik(j[sdk::pj_hip_r], j[sdk::pj_ankle_r], rth, rsh,
                                       {0.88f, -0.35f, 0.15f});
}

void append_oriented_box(std::vector<vertex>& verts, std::vector<std::uint16_t>& indices,
                         const math::vec3& center, const math::vec3& ax, const math::vec3& ay,
                         const math::vec3& az, float hx, float hy, float hz, std::uint8_t part) {
    float col[4]{};
    part_color(part, col);

    const auto add_face = [&](const math::vec3& n, const math::vec3& p0, const math::vec3& p1,
                              const math::vec3& p2, const math::vec3& p3) {
        const std::uint16_t base = static_cast<std::uint16_t>(verts.size());
        const math::vec3 pts[4] = {p0, p1, p2, p3};
        for (const auto& pt : pts) {
            verts.push_back({{pt.x, pt.y, pt.z}, {n.x, n.y, n.z}, {col[0], col[1], col[2], col[3]}});
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    };

    const math::vec3 ex = ax * hx;
    const math::vec3 ey = ay * hy;
    const math::vec3 ez = az * hz;
    add_face(ax, center + ex - ey - ez, center + ex + ey - ez, center + ex + ey + ez, center + ex - ey + ez);
    add_face(ax * -1.0f, center - ex + ey - ez, center - ex - ey - ez, center - ex - ey + ez, center - ex + ey + ez);
    add_face(ay, center + ex + ey - ez, center - ex + ey - ez, center - ex + ey + ez, center + ex + ey + ez);
    add_face(ay * -1.0f, center - ex - ey - ez, center + ex - ey - ez, center + ex - ey + ez, center - ex - ey + ez);
    add_face(az, center - ex - ey + ez, center + ex - ey + ez, center + ex + ey + ez, center - ex + ey + ez);
    add_face(az * -1.0f, center - ex + ey - ez, center + ex + ey - ez, center + ex - ey - ez, center - ex - ey - ez);
}

void append_rifle_mesh(std::vector<vertex>& verts, std::vector<std::uint16_t>& indices,
                       const math::vec3& grip_hand, const math::vec3& support_hand) {
    const math::vec3 dir = (support_hand - grip_hand).normalised();
    const math::vec3 up_hint{0.0f, 0.0f, 1.0f};
    const math::vec3 side = dir.cross(up_hint).normalised();
    const math::vec3 up = side.cross(dir).normalised();

    const math::vec3 origin = grip_hand + dir * 2.2f + up * 2.3f + side * 0.2f;
    // Stock
    append_oriented_box(verts, indices, origin - dir * 5.8f - up * 0.4f, dir, side, up, 4.2f, 0.7f, 1.5f, 4);
    append_oriented_box(verts, indices, origin - dir * 1.4f + up * 0.1f, dir, side, up, 1.8f, 0.55f, 0.7f, 4);
    // Upper/lower receiver
    append_oriented_box(verts, indices, origin + dir * 2.8f + up * 0.4f, dir, side, up, 3.8f, 0.85f, 1.35f, 4);
    // Pistol grip
    append_oriented_box(verts, indices, origin + dir * 0.4f - up * 2.0f,
                        (dir * 0.35f - up * 0.94f).normalised(), side,
                        (dir * 0.94f + up * 0.35f).normalised(), 1.8f, 0.65f, 0.85f, 4);
    // Curved magazine
    append_oriented_box(verts, indices, origin + dir * 5.0f - up * 3.1f,
                        (dir * 0.18f - up * 0.98f).normalised(), side,
                        (dir * 0.98f + up * 0.18f).normalised(), 2.8f, 0.6f, 1.15f, 4);
    // Optic / red dot on top rail
    append_oriented_box(verts, indices, origin + dir * 2.8f + up * 2.25f, dir, side, up, 1.8f, 0.72f, 0.75f, 4);
    // Quad-rail handguard
    append_oriented_box(verts, indices, origin + dir * 10.2f + up * 0.55f, dir, side, up, 4.4f, 0.95f, 1.05f, 4);
    // Suppressor / barrel
    append_oriented_box(verts, indices, origin + dir * 17.6f + up * 0.55f, dir, side, up, 3.6f, 0.58f, 0.58f, 4);
}

math::mat4 look_at(const math::vec3& eye, const math::vec3& target) {
    const math::vec3 z = (target - eye).normalised();
    const math::vec3 up{0.0f, 0.0f, 1.0f};
    const math::vec3 x = up.cross(z).normalised();
    const math::vec3 y = z.cross(x);

    math::mat4 out;
    out.at(0, 0) = x.x;
    out.at(0, 1) = y.x;
    out.at(0, 2) = z.x;
    out.at(0, 3) = 0.0f;
    out.at(1, 0) = x.y;
    out.at(1, 1) = y.y;
    out.at(1, 2) = z.y;
    out.at(1, 3) = 0.0f;
    out.at(2, 0) = x.z;
    out.at(2, 1) = y.z;
    out.at(2, 2) = z.z;
    out.at(2, 3) = 0.0f;
    out.at(3, 0) = -x.dot(eye);
    out.at(3, 1) = -y.dot(eye);
    out.at(3, 2) = -z.dot(eye);
    out.at(3, 3) = 1.0f;
    return out;
}

math::mat4 perspective(float fov_deg, float aspect, float zn, float zf) {
    const float y = 1.0f / std::tan(fov_deg * k_pi / 360.0f);
    const float x = y / aspect;
    math::mat4 out;
    out.at(0, 0) = x;
    out.at(1, 1) = y;
    out.at(2, 2) = zf / (zf - zn);
    out.at(2, 3) = 1.0f;
    out.at(3, 2) = -zn * zf / (zf - zn);
    return out;
}

math::mat4 multiply(const math::mat4& a, const math::mat4& b) {
    math::mat4 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.at(r, k) * b.at(k, c);
            out.at(r, c) = sum;
        }
    }
    return out;
}

bool project(const math::vec3& point, const math::mat4& view_proj, const ImVec2& origin,
             const ImVec2& size, ImVec2& out) {
    const float x = view_proj.at(0, 0) * point.x + view_proj.at(1, 0) * point.y +
                    view_proj.at(2, 0) * point.z + view_proj.at(3, 0);
    const float y = view_proj.at(0, 1) * point.x + view_proj.at(1, 1) * point.y +
                    view_proj.at(2, 1) * point.z + view_proj.at(3, 1);
    const float w = view_proj.at(0, 3) * point.x + view_proj.at(1, 3) * point.y +
                    view_proj.at(2, 3) * point.z + view_proj.at(3, 3);
    if (w <= 0.001f) return false;
    out.x = origin.x + (x / w + 1.0f) * 0.5f * size.x;
    out.y = origin.y + (1.0f - y / w) * 0.5f * size.y;
    return true;
}

ImU32 to_imu32(const config::rgba& c, int a = 255) {
    return IM_COL32(static_cast<int>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f), a);
}

void draw_esp_overlay(ImDrawList* dl, const config::visuals_cfg& v, const math::mat4& view_proj,
                      const ImVec2& vp_pos, const ImVec2& vp_size, const math::vec3& bmin,
                      const math::vec3& bmax, const math::vec3& head_pos, const char* label_name) {
    const math::vec3 corners[8] = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
        {bmax.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmin.z},
        {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z},
        {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z},
    };
    float left = vp_pos.x + vp_size.x;
    float right = vp_pos.x;
    float top = vp_pos.y + vp_size.y;
    float bottom = vp_pos.y;
    ImVec2 scr[8]{};
    int valid_pts = 0;
    for (int i = 0; i < 8; ++i) {
        if (!project(corners[i], view_proj, vp_pos, vp_size, scr[i])) continue;
        ++valid_pts;
        left = std::min(left, scr[i].x);
        right = std::max(right, scr[i].x);
        top = std::min(top, scr[i].y);
        bottom = std::max(bottom, scr[i].y);
    }
    if (valid_pts < 4 || right - left < 6.0f || bottom - top < 10.0f) return;

    const int alpha = std::clamp(v.esp_alpha, 0, 255);
    const ImU32 box_col = to_imu32(v.box_color, alpha);
    const ImU32 outline = IM_COL32(10, 12, 16, alpha * 200 / 255);
    const float w = right - left;
    const float h = bottom - top;

    if (v.box == config::box_style::flat_2d) {
        dl->AddRect(ImVec2(left - 1, top - 1), ImVec2(right + 1, bottom + 1), outline);
        dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), box_col);
    } else if (v.box == config::box_style::corner) {
        const float cw = std::max(5.0f, w * 0.24f);
        const float ch = std::max(5.0f, h * 0.20f);
        auto corner = [&](float px, float py, float dx, float dy) {
            dl->AddLine(ImVec2(px, py), ImVec2(px + dx, py), outline, 3.0f);
            dl->AddLine(ImVec2(px, py), ImVec2(px, py + dy), outline, 3.0f);
            dl->AddLine(ImVec2(px, py), ImVec2(px + dx, py), box_col, 1.2f);
            dl->AddLine(ImVec2(px, py), ImVec2(px, py + dy), box_col, 1.2f);
        };
        corner(left, top, cw, ch);
        corner(right, top, -cw, ch);
        corner(left, bottom, cw, -ch);
        corner(right, bottom, -cw, -ch);
    } else if (v.box == config::box_style::box_3d && valid_pts == 8) {
        for (int i = 0; i < 4; ++i) {
            const int nxt = (i + 1) & 3;
            dl->AddLine(scr[i], scr[nxt], box_col, 1.0f);
            dl->AddLine(scr[i + 4], scr[nxt + 4], box_col, 1.0f);
            dl->AddLine(scr[i], scr[i + 4], box_col, 1.0f);
        }
    }

    if (v.health_bar) {
        const float bx = left - 7.0f;
        const float hp_frac = 0.88f;
        const float fill_h = h * hp_frac;
        dl->AddRectFilled(ImVec2(bx - 1, top - 1), ImVec2(bx + 4, bottom + 1), outline);
        dl->AddRectFilled(ImVec2(bx, bottom - fill_h), ImVec2(bx + 3, bottom),
                          IM_COL32(120, 235, 95, alpha));
    }
    if (v.armor_bar) {
        const float ay = bottom + 3.0f;
        dl->AddRectFilled(ImVec2(left - 1, ay - 1), ImVec2(right + 1, ay + 3), outline);
        dl->AddRectFilled(ImVec2(left, ay), ImVec2(left + w * 0.92f, ay + 2),
                          IM_COL32(95, 175, 255, alpha));
    }
    if (v.name && label_name && label_name[0] != '\0') {
        ImVec2 head_s{};
        const float nx = project(head_pos, view_proj, vp_pos, vp_size, head_s)
                             ? head_s.x
                             : (left + w * 0.5f);
        const ImVec2 tsz = ImGui::CalcTextSize(label_name);
        dl->AddText(ImVec2(nx - tsz.x * 0.5f, top - tsz.y - 3.0f),
                    IM_COL32(242, 245, 252, alpha), label_name);
    }
    float by = bottom + (v.armor_bar ? 7.0f : 4.0f);
    if (v.weapon_icon) {
        draw_cs2_weapon_icon(*dl, ImVec2(left + w * 0.5f, by + 6.5f), 36.0f, 12.0f,
                             static_cast<int>(cs2_weapon_icon::m4a1_silencer),
                             IM_COL32(215, 222, 235, alpha));
        by += 15.0f;
    }
    if (v.weapon) {
        const char* wname = "M4A1-S";
        const ImVec2 tsz = ImGui::CalcTextSize(wname);
        dl->AddText(ImVec2(left + (w - tsz.x) * 0.5f, by), IM_COL32(215, 222, 235, alpha), wname);
        by += ImGui::GetTextLineHeight();
    }
    if (v.ammo) {
        const char* am = "[20]";
        const ImVec2 tsz = ImGui::CalcTextSize(am);
        dl->AddText(ImVec2(left + (w - tsz.x) * 0.5f, by), IM_COL32(215, 222, 235, alpha), am);
        by += ImGui::GetTextLineHeight();
    }
    if (v.distance) {
        const char* dm = "6m";
        const ImVec2 tsz = ImGui::CalcTextSize(dm);
        dl->AddText(ImVec2(left + (w - tsz.x) * 0.5f, by), IM_COL32(175, 185, 200, alpha), dm);
    }
    if (v.flags) {
        dl->AddText(ImVec2(right + 4.0f, top), IM_COL32(160, 235, 160, alpha), "KIT");
    }
}

}  // namespace

bool panel::init(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    device_ = device;
    device_->AddRef();
    ctx_ = ctx;
    ctx_->AddRef();

    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    ID3DBlob* error = nullptr;

    if (FAILED(D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr,
                          "vs_main", "vs_4_0", 0, 0, &vs_blob, &error))) {
        log::error("preview: vertex shader compile failed: {}",
                   error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
        if (error) error->Release();
        return false;
    }
    if (FAILED(D3DCompile(k_shader_source, std::strlen(k_shader_source), nullptr, nullptr, nullptr,
                          "ps_main", "ps_4_0", 0, 0, &ps_blob, &error))) {
        log::error("preview: pixel shader compile failed: {}",
                   error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
        if (error) error->Release();
        vs_blob->Release();
        return false;
    }

    device_->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr,
                                &vs_);
    device_->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps_);

    const D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(layout_desc, 3, vs_blob->GetBufferPointer(),
                               vs_blob->GetBufferSize(), &layout_);

    vs_blob->Release();
    ps_blob->Release();

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(cb_frame);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cb, nullptr, &cb_);

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = TRUE;
    device_->CreateRasterizerState(&rs, &rs_solid_);
    rs.FillMode = D3D11_FILL_WIREFRAME;
    device_->CreateRasterizerState(&rs, &rs_wire_);
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_FRONT;
    device_->CreateRasterizerState(&rs, &rs_cull_front_);

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    device_->CreateDepthStencilState(&ds, &ds_on_);
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    device_->CreateDepthStencilState(&ds, &ds_off_);

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&bd, &blend_);

    rebuild_mesh();
    ready_ = create_target(k_target_size, k_target_size);
    return ready_;
}

bool panel::create_target(UINT w, UINT h) {
    release_target();

    D3D11_TEXTURE2D_DESC cd{};
    cd.Width = w;
    cd.Height = h;
    cd.MipLevels = 1;
    cd.ArraySize = 1;
    cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cd.SampleDesc.Count = 1;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&cd, nullptr, &color_))) return false;
    if (FAILED(device_->CreateRenderTargetView(color_, nullptr, &rtv_))) return false;
    if (FAILED(device_->CreateShaderResourceView(color_, nullptr, &srv_))) return false;

    D3D11_TEXTURE2D_DESC dd = cd;
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&dd, nullptr, &depth_))) return false;
    if (FAILED(device_->CreateDepthStencilView(depth_, nullptr, &dsv_))) return false;

    width_ = w;
    height_ = h;
    return true;
}

void panel::release_target() {
    if (dsv_) { dsv_->Release(); dsv_ = nullptr; }
    if (depth_) { depth_->Release(); depth_ = nullptr; }
    if (srv_) { srv_->Release(); srv_ = nullptr; }
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
    if (color_) { color_->Release(); color_ = nullptr; }
    width_ = height_ = 0;
}

void panel::shutdown() {
    release_target();
    if (vb_) vb_->Release();
    if (ib_) ib_->Release();
    if (cb_) cb_->Release();
    if (layout_) layout_->Release();
    if (vs_) vs_->Release();
    if (ps_) ps_->Release();
    if (rs_solid_) rs_solid_->Release();
    if (rs_wire_) rs_wire_->Release();
    if (rs_cull_front_) rs_cull_front_->Release();
    if (ds_on_) ds_on_->Release();
    if (ds_off_) ds_off_->Release();
    if (blend_) blend_->Release();
    vb_ = nullptr;
    ib_ = nullptr;
    cb_ = nullptr;
    layout_ = nullptr;
    vs_ = nullptr;
    ps_ = nullptr;
    rs_solid_ = rs_wire_ = rs_cull_front_ = nullptr;
    ds_on_ = ds_off_ = nullptr;
    blend_ = nullptr;
    index_count_ = 0;
    dirty_ = true;

    if (ctx_) { ctx_->Release(); ctx_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    ready_ = false;
}

void panel::set_stance(stance s) {
    if (stance_ == s) return;
    stance_ = s;
    dirty_ = true;
}

void panel::set_skeleton(std::span<const math::vec3> joints,
                         std::span<const std::pair<int, int>> connections) {
    joint_count_ = std::min<int>(static_cast<int>(joints.size()), k_max_joints);
    for (int i = 0; i < joint_count_; ++i) joints_[i] = joints[i];

    connection_count_ = std::min<int>(static_cast<int>(connections.size()), k_max_joints);
    for (int i = 0; i < connection_count_; ++i) {
        connections_[i][0] = connections[i].first;
        connections_[i][1] = connections[i].second;
    }
}

void panel::rebuild_mesh() {
    if (vb_) { vb_->Release(); vb_ = nullptr; }
    if (ib_) { ib_->Release(); ib_ = nullptr; }

    std::array<math::vec3, sdk::pj_count> rest{};
    std::array<math::vec3, sdk::pj_count> posed{};
    build_rest_joints(rest);
    build_stance_joints(stance_, posed, rest);
    for (int i = 0; i < sdk::pj_count; ++i) posed_joints_[i] = posed[i];

    // 10 anatomical bone frames matching rest & posed joints.
    enum bone_id {
        b_torso = 0, b_head,
        b_lua, b_lfa, b_rua, b_rfa,
        b_lth, b_lsh, b_rth, b_rsh,
        b_count
    };
    std::array<bone_frame, b_count> rf{};
    std::array<bone_frame, b_count> pf{};

    auto set_bone = [&](int id, const math::vec3& ra, const math::vec3& rb, const math::vec3& rpole,
                        const math::vec3& pa, const math::vec3& pb, const math::vec3& ppole) {
        rf[id] = make_frame(ra, rb, rpole);
        pf[id] = make_frame(pa, pb, ppole);
    };

    const math::vec3 forward_pole{1.0f, 0.0f, 0.0f};
    set_bone(b_torso, rest[sdk::pj_pelvis], rest[sdk::pj_neck], forward_pole,
             posed[sdk::pj_pelvis], posed[sdk::pj_neck], {1.0f, -0.20f, 0.05f});
    set_bone(b_head, rest[sdk::pj_neck], rest[sdk::pj_head], forward_pole,
             posed[sdk::pj_neck], posed[sdk::pj_head], {1.0f, -0.10f, 0.05f});

    set_bone(b_lua, rest[sdk::pj_shoulder_l], rest[sdk::pj_elbow_l], forward_pole,
             posed[sdk::pj_shoulder_l], posed[sdk::pj_elbow_l], forward_pole);
    set_bone(b_lfa, rest[sdk::pj_elbow_l], rest[sdk::pj_hand_l], forward_pole,
             posed[sdk::pj_elbow_l], posed[sdk::pj_hand_l], {0.0f, 0.0f, 1.0f});

    set_bone(b_rua, rest[sdk::pj_shoulder_r], rest[sdk::pj_elbow_r], forward_pole,
             posed[sdk::pj_shoulder_r], posed[sdk::pj_elbow_r], forward_pole);
    set_bone(b_rfa, rest[sdk::pj_elbow_r], rest[sdk::pj_hand_r], forward_pole,
             posed[sdk::pj_elbow_r], posed[sdk::pj_hand_r], {0.0f, 0.0f, 1.0f});

    set_bone(b_lth, rest[sdk::pj_hip_l], rest[sdk::pj_knee_l], forward_pole,
             posed[sdk::pj_hip_l], posed[sdk::pj_knee_l], forward_pole);
    set_bone(b_lsh, rest[sdk::pj_knee_l], rest[sdk::pj_ankle_l], forward_pole,
             posed[sdk::pj_knee_l], posed[sdk::pj_ankle_l], forward_pole);

    set_bone(b_rth, rest[sdk::pj_hip_r], rest[sdk::pj_knee_r], forward_pole,
             posed[sdk::pj_hip_r], posed[sdk::pj_knee_r], forward_pole);
    set_bone(b_rsh, rest[sdk::pj_knee_r], rest[sdk::pj_ankle_r], forward_pole,
             posed[sdk::pj_knee_r], posed[sdk::pj_ankle_r], forward_pole);

    std::vector<vertex> verts(ctm_sas::k_vertex_count);
    bounds_min_ = {1e9f, 1e9f, 1e9f};
    bounds_max_ = {-1e9f, -1e9f, -1e9f};

    for (std::uint32_t i = 0; i < ctm_sas::k_vertex_count; ++i) {
        const auto& src = ctm_sas::k_vertices[i];
        const math::vec3 vp{src.px, src.py, src.pz};
        const math::vec3 vn{src.nx, src.ny, src.nz};

        math::vec3 out_p = vp;
        math::vec3 out_n = vn;

        if (stance_ != stance::a_pose) {
            float w[b_count]{};
            const float inv_s2 = 1.0f / (3.2f * 3.2f);
            auto add_w = [&](int id, const math::vec3& a, const math::vec3& b) {
                const float d = dist_to_segment(vp, a, b);
                w[id] = std::exp(-d * d * inv_s2);
            };

            if (vp.z > 61.0f && std::fabs(vp.y) < 7.0f) {
                add_w(b_head, rest[sdk::pj_neck] + math::vec3{0, 0, 1.0f}, rest[sdk::pj_head] + math::vec3{0, 0, 5.0f});
                add_w(b_torso, rest[sdk::pj_pelvis], rest[sdk::pj_neck]);
            } else if (vp.z < 38.8f && std::fabs(vp.y) < 11.0f) {
                if (vp.y >= 0.0f) {
                    add_w(b_lth, rest[sdk::pj_hip_l], rest[sdk::pj_knee_l]);
                    add_w(b_lsh, rest[sdk::pj_knee_l], rest[sdk::pj_ankle_l] + math::vec3{0, 0, -5.0f});
                    if (vp.z > 35.0f) add_w(b_torso, rest[sdk::pj_pelvis], rest[sdk::pj_neck]);
                } else {
                    add_w(b_rth, rest[sdk::pj_hip_r], rest[sdk::pj_knee_r]);
                    add_w(b_rsh, rest[sdk::pj_knee_r], rest[sdk::pj_ankle_r] + math::vec3{0, 0, -5.0f});
                    if (vp.z > 35.0f) add_w(b_torso, rest[sdk::pj_pelvis], rest[sdk::pj_neck]);
                }
            } else if (vp.y > 5.8f) {
                add_w(b_lua, rest[sdk::pj_shoulder_l], rest[sdk::pj_elbow_l]);
                add_w(b_lfa, rest[sdk::pj_elbow_l], rest[sdk::pj_hand_l] + (rest[sdk::pj_hand_l] - rest[sdk::pj_elbow_l]) * 0.5f);
                if (vp.y < 10.5f) add_w(b_torso, rest[sdk::pj_pelvis], rest[sdk::pj_neck]);
            } else if (vp.y < -5.8f) {
                add_w(b_rua, rest[sdk::pj_shoulder_r], rest[sdk::pj_elbow_r]);
                add_w(b_rfa, rest[sdk::pj_elbow_r], rest[sdk::pj_hand_r] + (rest[sdk::pj_hand_r] - rest[sdk::pj_elbow_r]) * 0.5f);
                if (vp.y > -10.5f) add_w(b_torso, rest[sdk::pj_pelvis], rest[sdk::pj_neck]);
            } else {
                w[b_torso] = 1.0f;
            }

            float wsum = 0.0f;
            for (float v : w) wsum += v;
            if (wsum > 1e-5f) {
                math::vec3 acc_p{}, acc_n{};
                for (int b = 0; b < b_count; ++b) {
                    if (w[b] <= 1e-5f) continue;
                    const float wn = w[b] / wsum;
                    acc_p = acc_p + map_point(rf[b], pf[b], vp) * wn;
                    acc_n = acc_n + map_normal(rf[b], pf[b], vn) * wn;
                }
                out_p = acc_p;
                out_n = acc_n.normalised();
            } else {
                out_p = map_point(rf[b_torso], pf[b_torso], vp);
                out_n = map_normal(rf[b_torso], pf[b_torso], vn);
            }
        }

        verts[i].pos[0] = out_p.x;
        verts[i].pos[1] = out_p.y;
        verts[i].pos[2] = out_p.z;
        verts[i].nrm[0] = out_n.x;
        verts[i].nrm[1] = out_n.y;
        verts[i].nrm[2] = out_n.z;
        part_color(src.part, verts[i].col);

        bounds_min_.x = std::min(bounds_min_.x, out_p.x);
        bounds_min_.y = std::min(bounds_min_.y, out_p.y);
        bounds_min_.z = std::min(bounds_min_.z, out_p.z);
        bounds_max_.x = std::max(bounds_max_.x, out_p.x);
        bounds_max_.y = std::max(bounds_max_.y, out_p.y);
        bounds_max_.z = std::max(bounds_max_.z, out_p.z);
    }

    std::vector<std::uint16_t> indices(ctm_sas::k_indices, ctm_sas::k_indices + ctm_sas::k_index_count);
    if (stance_ != stance::a_pose) {
        append_rifle_mesh(verts, indices, posed[sdk::pj_hand_r], posed[sdk::pj_hand_l]);
    }

    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = static_cast<UINT>(verts.size() * sizeof(vertex));
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd{};
    vsd.pSysMem = verts.data();
    device_->CreateBuffer(&vbd, &vsd, &vb_);

    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd{};
    isd.pSysMem = indices.data();
    device_->CreateBuffer(&ibd, &isd, &ib_);

    index_count_ = static_cast<UINT>(indices.size());
    dirty_ = false;
}

void panel::draw(const ImVec2& size, camera& cam, const config::cham_material& material,
                 bool show_skeleton, const config::visuals_cfg* esp_overlay,
                 const char* label_name) {
    if (!ready_ || !device_) return;

    ImGui::PushID("##preview");
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##orbit", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            cam.distance = std::clamp(cam.distance - wheel * 10.0f, 35.0f, 260.0f);
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) cam.reset();
    }
    if (ImGui::IsItemActive()) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            cam.yaw -= delta.x * 0.35f;
            cam.pitch = std::clamp(cam.pitch + delta.y * 0.3f, -85.0f, 85.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            cam.distance = std::clamp(cam.distance + delta.y * 0.45f, 35.0f, 260.0f);
        }
    }
    ImGui::PopID();

    if (dirty_) rebuild_mesh();

    const math::vec3 dir{std::cos(cam.pitch * k_pi / 180.0f) * std::cos(cam.yaw * k_pi / 180.0f),
                         std::cos(cam.pitch * k_pi / 180.0f) * std::sin(cam.yaw * k_pi / 180.0f),
                         std::sin(cam.pitch * k_pi / 180.0f)};
    const math::vec3 eye = cam.target + dir * cam.distance;
    const math::mat4 view = look_at(eye, cam.target);
    const float aspect = (size.y > 1.0f) ? (size.x / size.y) : 1.0f;
    const math::mat4 proj = perspective(44.0f, aspect, 1.0f, 2000.0f);
    const math::mat4 view_proj = multiply(view, proj);

    ID3D11RenderTargetView* old_rtv = nullptr;
    ID3D11DepthStencilView* old_dsv = nullptr;
    ctx_->OMGetRenderTargets(1, &old_rtv, &old_dsv);
    UINT vp_count = 1;
    D3D11_VIEWPORT old_vp{};
    ctx_->RSGetViewports(&vp_count, &old_vp);

    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ctx_->ClearRenderTargetView(rtv_, clear);
    ctx_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    ctx_->RSSetViewports(1, &vp);

    ctx_->IASetInputLayout(layout_);
    const UINT stride = sizeof(vertex);
    const UINT offset = 0;
    ctx_->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
    ctx_->IASetIndexBuffer(ib_, DXGI_FORMAT_R16_UINT, 0);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cb_);
    ctx_->PSSetConstantBuffers(0, 1, &cb_);

    const bool ignore_z = (material.flags & static_cast<int>(config::chams_flag::ignore_z)) != 0;
    const bool wireframe = (material.flags & static_cast<int>(config::chams_flag::wireframe)) != 0;
    const bool flat = (material.flags & static_cast<int>(config::chams_flag::flat)) != 0;
    const bool overlay = (material.flags & static_cast<int>(config::chams_flag::overlay)) != 0;

    const int shader_mode = flat ? 1 : (material.enabled ? material.material : 0);
    const config::rgba& colour = material.enabled ? material.visible
                                                  : config::rgba{1.0f, 1.0f, 1.0f, 1.0f};

    auto upload_cb = [&](float inflate, float is_outline) {
        cb_frame cb{};
        std::memcpy(cb.view_proj, &view_proj.m[0][0], sizeof cb.view_proj);
        cb.color[0] = colour.r;
        cb.color[1] = colour.g;
        cb.color[2] = colour.b;
        cb.color[3] = overlay ? colour.a : 1.0f;
        cb.eye_pos[0] = eye.x;
        cb.eye_pos[1] = eye.y;
        cb.eye_pos[2] = eye.z;
        cb.eye_pos[3] = 1.0f;
        cb.params[0] = static_cast<float>(shader_mode);
        cb.params[1] = material.enabled ? 1.0f : 0.0f;
        cb.params[2] = material.fresnel_exponent;
        cb.params[3] = material.fresnel_falloff;
        cb.outline[0] = inflate;
        cb.outline[1] = is_outline;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx_->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof cb);
            ctx_->Unmap(cb_, 0);
        }
    };

    const float blend_factor[4] = {1, 1, 1, 1};
    // Subtle outer silhouette glow halo when Chams are active.
    if (material.enabled && !wireframe) {
        ctx_->RSSetState(rs_cull_front_);
        ctx_->OMSetDepthStencilState(ds_on_, 0);
        ctx_->OMSetBlendState(blend_, blend_factor, 0xFFFFFFFF);
        upload_cb(0.45f, 1.0f);
        if (vb_ && ib_ && index_count_ > 0) ctx_->DrawIndexed(index_count_, 0, 0);
    }

    ctx_->RSSetState(wireframe ? rs_wire_ : rs_solid_);
    ctx_->OMSetDepthStencilState(ignore_z ? ds_off_ : ds_on_, 0);
    if (overlay) {
        ctx_->OMSetBlendState(blend_, blend_factor, 0xFFFFFFFF);
    } else {
        ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    }
    upload_cb(0.0f, 0.0f);
    if (vb_ != nullptr && ib_ != nullptr && index_count_ > 0) {
        ctx_->DrawIndexed(index_count_, 0, 0);
    }

    ctx_->OMSetRenderTargets(1, &old_rtv, old_dsv);
    if (vp_count > 0) ctx_->RSSetViewports(vp_count, &old_vp);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx_->OMSetDepthStencilState(nullptr, 0);
    ctx_->RSSetState(nullptr);
    if (old_rtv) old_rtv->Release();
    if (old_dsv) old_dsv->Release();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 rect_max(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, rect_max, fade_col(IM_COL32(15, 19, 27, 255)), 6.0f);
    dl->PushClipRect(pos, rect_max, true);

    if (material.enabled) {
        const ImVec2 glow_center(pos.x + size.x * 0.52f, pos.y + size.y * 0.50f);
        const ImU32 aura = to_imu32(colour, 16);
        dl->AddCircleFilled(glow_center, size.y * 0.44f, aura, 48);
        dl->AddCircleFilled(glow_center, size.y * 0.30f, aura, 48);
    }

    dl->AddImage(reinterpret_cast<ImTextureID>(srv_), pos, rect_max, ImVec2(0, 0), ImVec2(1, 1),
                 fade_col(IM_COL32(255, 255, 255, 255)));

    if (show_skeleton) {
        for (const auto& [a, b] : sdk::k_preview_links) {
            if (a >= sdk::pj_count || b >= sdk::pj_count) continue;
            ImVec2 pa{}, pb{};
            if (project(posed_joints_[a], view_proj, pos, size, pa) &&
                project(posed_joints_[b], view_proj, pos, size, pb)) {
                dl->AddLine(pa, pb, fade_col(IM_COL32(10, 12, 18, 170)), 2.4f);
                dl->AddLine(pa, pb, fade_col(IM_COL32(255, 255, 255, 225)), 1.3f);
            }
        }
    }

    if (esp_overlay && esp_overlay->enabled) {
        const math::vec3 bmin{std::min(bounds_min_.x, -12.0f), bounds_min_.y - 1.5f, 0.0f};
        const math::vec3 bmax{bounds_max_.x + 1.5f, bounds_max_.y + 1.5f,
                              std::max(bounds_max_.z + 1.5f, 70.0f)};
        const math::vec3 head_pt = posed_joints_[sdk::pj_head] + math::vec3{0.0f, 0.0f, 4.5f};
        draw_esp_overlay(dl, *esp_overlay, view_proj, pos, size, bmin, bmax, head_pt, label_name);
    }

    dl->PopClipRect();
    dl->AddRect(pos, rect_max, fade_col(colors().border_soft), 6.0f);
}

panel& shared_panel() {
    static panel g_panel;
    return g_panel;
}

camera& shared_camera() {
    static camera g_camera;
    return g_camera;
}

}  // namespace shogod::ui::preview
