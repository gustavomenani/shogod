#include <gtest/gtest.h>

#include "features/backtrack.hpp"
#include "features/lagcomp.hpp"
#include "features/math/angles.hpp"
#include "features/math/hitbox_geom.hpp"
#include "features/math/hitchance.hpp"
#include "features/math/hull.hpp"
#include "features/math/mat4.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace shogod::math;

namespace {

mat4 identity_projection() {
    mat4 m;
    m.at(0, 0) = 1.0f;
    m.at(1, 1) = 1.0f;
    m.at(2, 2) = 1.0f;
    m.at(3, 3) = 1.0f;
    return m;
}

}  // namespace

TEST(mat4, centre_maps_to_screen_centre) {
    vec2 out{};
    ASSERT_TRUE(identity_projection().world_to_screen({0, 0, 0}, 1920, 1080, out));
    EXPECT_FLOAT_EQ(out.x, 960.0f);
    EXPECT_FLOAT_EQ(out.y, 540.0f);
}

TEST(mat4, hand_computed_quarter_corner) {
    // world (0.5, 0.5, 0) -> ndc (0.5, 0.5) -> screen (0.75W, 0.25H)
    vec2 out{};
    ASSERT_TRUE(identity_projection().world_to_screen({0.5f, 0.5f, 0.0f}, 1920, 1080, out));
    EXPECT_NEAR(out.x, 1440.0f, 1e-3f);
    EXPECT_NEAR(out.y, 270.0f, 1e-3f);
}

TEST(mat4, behind_camera_rejected) {
    mat4 m = identity_projection();
    m.at(3, 3) = 0.0f;  // w == 0 for every point
    vec2 out{};
    EXPECT_FALSE(m.world_to_screen({0, 0, 0}, 1920, 1080, out));

    mat4 neg = identity_projection();
    neg.at(3, 3) = -1.0f;  // w < 0
    EXPECT_FALSE(neg.world_to_screen({0, 0, 0}, 1920, 1080, out));
}

TEST(mat4, non_finite_matrix_rejected) {
    mat4 m = identity_projection();
    m.at(1, 2) = std::numeric_limits<float>::quiet_NaN();
    vec2 out{};
    EXPECT_FALSE(m.world_to_screen({0, 0, 0}, 1920, 1080, out));
}

TEST(angles, normalize_wraps_across_180) {
    EXPECT_FLOAT_EQ(normalize_yaw(190.0f), -170.0f);
    EXPECT_FLOAT_EQ(normalize_yaw(-190.0f), 170.0f);
    EXPECT_FLOAT_EQ(normalize_yaw(180.0f), 180.0f);
    EXPECT_FLOAT_EQ(normalize_yaw(540.0f), 180.0f);
    EXPECT_FLOAT_EQ(normalize_yaw(-540.0f), 180.0f);
}

TEST(angles, pitch_clamped) {
    EXPECT_FLOAT_EQ(clamp_pitch(100.0f), 89.0f);
    EXPECT_FLOAT_EQ(clamp_pitch(-100.0f), -89.0f);
}

TEST(angles, forward_roundtrip) {
    const vec3 f = forward_from_angles({0.0f, 90.0f});
    EXPECT_NEAR(f.x, 0.0f, 1e-5f);
    EXPECT_NEAR(f.y, 1.0f, 1e-5f);
    EXPECT_NEAR(f.z, 0.0f, 1e-5f);

    const angles a = angles_from_forward({1.0f, 1.0f, -1.0f});
    const vec3 back = forward_from_angles(a);
    const vec3 want = vec3{1.0f, 1.0f, -1.0f}.normalised();
    EXPECT_NEAR(back.x, want.x, 1e-5f);
    EXPECT_NEAR(back.y, want.y, 1e-5f);
    EXPECT_NEAR(back.z, want.z, 1e-5f);
}

TEST(hull, endpoints_and_clamp) {
    const vec3 bone{10.0f, 20.0f, 30.0f};
    const vec3 dir{0.0f, 0.0f, 1.0f};
    const vec3 centre = safe_point(bone, dir, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(centre.z, 30.0f);

    const vec3 surface = safe_point(bone, dir, 4.0f, 1.0f);
    EXPECT_NEAR(surface.z, 34.0f, 1e-5f);

    const vec3 over = safe_point(bone, dir, 4.0f, 2.0f);
    EXPECT_NEAR(over.z, 34.0f, 1e-5f);
}

TEST(hitchance, zero_spread_always_hits) {
    const vec3 eye{0, 0, 0};
    const vec3 target{0, 100, 0};
    EXPECT_FLOAT_EQ(hitchance(eye, target, 5.0f, 0.0f), 1.0f);
}

TEST(hitchance, offset_ray_is_measured_against_the_original_hitbox) {
    const vec3 eye{};
    const vec3 center{100.0f, 0.0f, 0.0f};
    EXPECT_FLOAT_EQ(hitchance(eye, center, center, 2.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(hitchance(eye, {100.0f, 4.0f, 0.0f}, center, 2.0f, 0.0f), 0.0f);
    EXPECT_LT(hitchance(eye, {100.0f, 1.5f, 0.0f}, center, 2.0f, 1.0f),
              hitchance(eye, center, center, 2.0f, 1.0f));
}

TEST(hitchance, very_narrow_spread_preserves_small_target_probability) {
    const float chance = hitchance({}, {1000.0f, 0.0f, 0.0f}, 0.01f, 0.01f);
    EXPECT_GT(chance, 0.0f);
    EXPECT_LT(chance, 0.1f);
}

TEST(hitchance, wider_cone_lowers_count) {
    const vec3 eye{0, 0, 0};
    const vec3 target{0, 100, 0};
    const float tight = hitchance(eye, target, 2.0f, 0.5f);
    const float wide = hitchance(eye, target, 2.0f, 10.0f);
    EXPECT_GT(tight, wide);
    EXPECT_LE(wide, 1.0f);
    EXPECT_GE(wide, 0.0f);
}

TEST(hitchance, radial_accuracy_samples_do_not_use_uniform_cone_area) {
    const vec3 eye{};
    const vec3 target{100.0f, 0.0f, 0.0f};
    const weapon_spread_pattern pattern(0.0f, 0.04f);
    // The installed tier0 stream yields 131 qualifying first draws over 256 seeds.
    EXPECT_FLOAT_EQ(pattern.chance(eye, target, target, 2.0f), 131.0f / 256.0f);
    const weapon_spread_pattern zero(0.0f, 0.0f);
    EXPECT_FLOAT_EQ(zero.chance(eye, target, target, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(zero.chance(eye, {100.0f, 4.0f, 0.0f}, target, 2.0f), 0.0f);
}

TEST(hitchance, bigger_target_raises_count) {
    const vec3 eye{0, 0, 0};
    const vec3 target{0, 100, 0};
    EXPECT_GE(hitchance(eye, target, 6.0f, 3.0f), hitchance(eye, target, 1.0f, 3.0f));
}

TEST(mat4, rejects_missing_projection) {
    mat4 matrix{};
    EXPECT_FALSE(matrix.usable_projection());
    matrix.at(3, 3) = 1.0f;
    EXPECT_TRUE(matrix.usable_projection());
}

TEST(hitbox_geom, hitgroup_mapping_separates_pelvis_arms_and_legs) {
    EXPECT_EQ(hitgroup_to_group(0), 3);  // generic / pelvis
    EXPECT_EQ(hitgroup_to_group(1), 0);  // head
    EXPECT_EQ(hitgroup_to_group(2), 1);  // chest
    EXPECT_EQ(hitgroup_to_group(8), 1);  // neck
    EXPECT_EQ(hitgroup_to_group(3), 2);  // stomach
    EXPECT_EQ(hitgroup_to_group(4), 5);  // left arm
    EXPECT_EQ(hitgroup_to_group(5), 5);  // right arm
    EXPECT_EQ(hitgroup_to_group(6), 4);  // left leg
    EXPECT_EQ(hitgroup_to_group(7), 4);  // right leg
    EXPECT_EQ(hitgroup_to_group(-1), -1);
    EXPECT_EQ(hitgroup_to_group(9), -1);

    const float id_raw[8] = {100.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const mat3x4 tm = matrix_from_2x4(id_raw);
    const auto foot_leg = build_hitbox_geometry({-2.0f, -2.0f, -4.0f}, {2.0f, 2.0f, 4.0f}, 0.0f,
                                                static_cast<int>(hitbox_shape::box), 6, true, tm, 1.0f);
    EXPECT_TRUE(foot_leg.valid);
    EXPECT_EQ(foot_leg.group, 4);
    EXPECT_TRUE(foot_leg.foot);

    const auto arm = build_hitbox_geometry({-2.0f, -2.0f, -4.0f}, {2.0f, 2.0f, 4.0f}, 0.0f,
                                           static_cast<int>(hitbox_shape::box), 4, false, tm, 1.0f);
    EXPECT_TRUE(arm.valid);
    EXPECT_EQ(arm.group, 5);
    EXPECT_FALSE(arm.foot);
}

TEST(hitbox_geom, box_obb_ray_intersection_handles_corners_and_rotation) {
    // 1. Axis-aligned cube [-5, 5]^3 at (100, 0, 0).
    const float aa_raw[8] = {100.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const auto aa_box = build_hitbox_geometry({-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}, 0.0f,
                                              static_cast<int>(hitbox_shape::box), 2, false,
                                              matrix_from_2x4(aa_raw), 1.0f);
    ASSERT_TRUE(aa_box.valid);
    const vec3 eye{0.0f, 0.0f, 0.0f};
    EXPECT_TRUE(ray_hits_geometry(eye, (vec3{100.0f, 4.5f, 4.5f} - eye).normalised(), aa_box));
    // (100, 6.0, 0) is inside the bounding sphere (r=8.66) and inside the old diagonal capsule
    // approximation (0.7*8.66=6.06), but outside the actual OBB face [-5, 5].
    EXPECT_FALSE(ray_hits_geometry(eye, (vec3{100.0f, 6.0f, 0.0f} - eye).normalised(), aa_box));
    // Scaled OBB test.
    EXPECT_TRUE(ray_hits_geometry_scaled(eye, (vec3{100.0f, 2.0f, 0.0f} - eye).normalised(), aa_box, 0.5f));
    EXPECT_FALSE(ray_hits_geometry_scaled(eye, (vec3{100.0f, 3.0f, 0.0f} - eye).normalised(), aa_box, 0.5f));

    // 2. Non-uniform thin box [-1, -10, -10] to [1, 10, 10] viewed from -Y.
    const auto thin_box = build_hitbox_geometry({-1.0f, -10.0f, -10.0f}, {1.0f, 10.0f, 10.0f}, 0.0f,
                                                static_cast<int>(hitbox_shape::box), 2, false,
                                                matrix_from_2x4(aa_raw), 1.0f);
    const vec3 side_eye{100.0f, -100.0f, 0.0f};
    EXPECT_TRUE(ray_hits_geometry(side_eye, (vec3{100.5f, 0.0f, 0.0f} - side_eye).normalised(), thin_box));
    EXPECT_FALSE(ray_hits_geometry(side_eye, (vec3{102.0f, 0.0f, 0.0f} - side_eye).normalised(), thin_box));

    // 3. 45-degree yaw rotated cube [-5, 5]^3 at (100, 0, 0).
    constexpr float k_half_angle = 3.14159265f / 8.0f;  // 22.5 deg -> 45 deg total yaw
    const float rot_raw[8] = {100.0f, 0.0f, 0.0f, 1.0f,
                              0.0f, 0.0f, std::sin(k_half_angle), std::cos(k_half_angle)};
    const auto rot_box = build_hitbox_geometry({-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}, 0.0f,
                                               static_cast<int>(hitbox_shape::box), 2, false,
                                               matrix_from_2x4(rot_raw), 1.0f);
    ASSERT_TRUE(rot_box.valid);
    // Along +Y, rotated corner reaches 5 * sqrt(2) = 7.071.
    EXPECT_TRUE(ray_hits_geometry(eye, (vec3{100.0f, 6.5f, 0.0f} - eye).normalised(), rot_box));
    // A 45-degree ray passing through (104.5, -4.5, 0) hits the unrotated [-5,5]^3 box,
    // but stays at |y_local| = 9/sqrt(2) = 6.364 > 5 for the 45-deg rotated OBB -> must MISS!
    const vec3 diag_eye{4.5f, -104.5f, 0.0f};
    const vec3 diag_dir = (vec3{104.5f, -4.5f, 0.0f} - diag_eye).normalised();
    EXPECT_TRUE(ray_hits_geometry(diag_eye, diag_dir, aa_box));
    EXPECT_FALSE(ray_hits_geometry(diag_eye, diag_dir, rot_box));

    // 4. Behind origin misses; origin inside OBB hits.
    EXPECT_FALSE(ray_hits_geometry({200.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, aa_box));
    EXPECT_TRUE(ray_hits_geometry({100.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, aa_box));
}

TEST(hitbox_geom, box_multipoints_stay_inside_obb_and_hit_rays) {
    constexpr float k_half_angle = 3.14159265f / 8.0f;
    const float aa_raw[8] = {200.0f, 10.0f, 40.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float rot_raw[8] = {200.0f, 10.0f, 40.0f, 1.0f,
                              0.0f, 0.0f, std::sin(k_half_angle), std::cos(k_half_angle)};
    const vec3 eye{0.0f, 0.0f, 40.0f};

    for (const float* raw : {aa_raw, rot_raw}) {
        const auto box = build_hitbox_geometry({-4.0f, -6.0f, -8.0f}, {4.0f, 6.0f, 8.0f}, 0.0f,
                                               static_cast<int>(hitbox_shape::box), 3, false,
                                               matrix_from_2x4(raw), 1.0f);
        ASSERT_TRUE(box.valid);
        std::array<vec3, 9> pts{};
        const std::size_t n = generate_hitbox_points(box, eye, 0.9f, pts);
        EXPECT_GE(n, 5u);
        EXPECT_NEAR(pts[0].x, box.center.x, 1e-4f);
        EXPECT_NEAR(pts[0].y, box.center.y, 1e-4f);
        EXPECT_NEAR(pts[0].z, box.center.z, 1e-4f);
        for (std::size_t i = 0; i < n; ++i) {
            const vec3 dir = (pts[i] - eye).normalised();
            EXPECT_TRUE(ray_hits_geometry(eye, dir, box)) << "Multipoint " << i << " fell outside OBB";
        }
    }

    EXPECT_FLOAT_EQ(effective_multipoint_scale(80.0f, 100.0f), 0.0f);
    EXPECT_LT(effective_multipoint_scale(100.0f, 50.0f), effective_multipoint_scale(100.0f, 0.0f));
}

TEST(hitchance, chance_multi_isolates_head_torso_and_rejects_arms_and_legs) {
    const vec3 eye{0.0f, 0.0f, 50.0f};
    const float head_raw[8] = {200.0f, 0.0f, 66.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float chest_raw[8] = {200.0f, 0.0f, 52.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float stomach_raw[8] = {200.0f, 0.0f, 40.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float arm_raw[8] = {200.0f, 9.0f, 40.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float leg_raw[8] = {200.0f, 0.0f, 26.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    const auto head = build_hitbox_geometry({0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f}, 4.5f,
                                            static_cast<int>(hitbox_shape::capsule), 1, false,
                                            matrix_from_2x4(head_raw), 1.0f);
    const auto chest = build_hitbox_geometry({-5.0f, -6.0f, -5.0f}, {5.0f, 6.0f, 5.0f}, 0.0f,
                                             static_cast<int>(hitbox_shape::box), 2, false,
                                             matrix_from_2x4(chest_raw), 1.0f);
    const auto stomach = build_hitbox_geometry({-4.0f, -5.0f, -4.0f}, {4.0f, 5.0f, 4.0f}, 0.0f,
                                               static_cast<int>(hitbox_shape::box), 3, false,
                                               matrix_from_2x4(stomach_raw), 1.0f);
    const auto arm = build_hitbox_geometry({-3.0f, -4.0f, -6.0f}, {3.0f, 4.0f, 6.0f}, 0.0f,
                                           static_cast<int>(hitbox_shape::box), 4, false,
                                           matrix_from_2x4(arm_raw), 1.0f);
    const auto leg = build_hitbox_geometry({-3.0f, -4.0f, -8.0f}, {3.0f, 4.0f, 8.0f}, 0.0f,
                                           static_cast<int>(hitbox_shape::box), 6, false,
                                           matrix_from_2x4(leg_raw), 1.0f);

    const weapon_spread_pattern pat(0.0f, 0.08f);
    const std::array<hitbox_geometry, 1> only_stomach{stomach};
    const std::array<hitbox_geometry, 2> stomach_arm{stomach, arm};
    const std::array<hitbox_geometry, 2> stomach_leg{stomach, leg};
    const std::array<hitbox_geometry, 2> stomach_chest{stomach, chest};
    const std::array<hitbox_geometry, 1> only_head{head};
    const std::array<hitbox_geometry, 2> head_chest{head, chest};

    const float hc_stomach = pat.chance_multi(eye, stomach.center, only_stomach, 2);
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, stomach.center, stomach_arm, 2), hc_stomach);
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, stomach.center, stomach_leg, 2), hc_stomach);
    EXPECT_GT(pat.chance_multi(eye, stomach.center, stomach_chest, 2), hc_stomach);

    const float hc_head = pat.chance_multi(eye, head.center, only_head, 0);
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, head.center, head_chest, 0), hc_head);
}

TEST(hitchance, safe_point_and_strict_hitchance_tighten_spread_and_groups) {
    EXPECT_LT(safe_point_hitbox_scale(100.0f, true), safe_point_hitbox_scale(100.0f, false));
    EXPECT_LT(safe_point_hitbox_scale(100.0f, false), safe_point_hitbox_scale(0.0f, false));

    const vec3 eye{0.0f, 0.0f, 50.0f};
    const float head_raw[8] = {200.0f, 0.0f, 66.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float chest_raw[8] = {200.0f, 0.0f, 52.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float stomach_raw[8] = {200.0f, 0.0f, 40.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    const auto head = build_hitbox_geometry({0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f}, 4.5f,
                                            static_cast<int>(hitbox_shape::capsule), 1, false,
                                            matrix_from_2x4(head_raw), 1.0f);
    const auto chest = build_hitbox_geometry({-5.0f, -6.0f, -5.0f}, {5.0f, 6.0f, 5.0f}, 0.0f,
                                             static_cast<int>(hitbox_shape::box), 2, false,
                                             matrix_from_2x4(chest_raw), 1.0f);
    const auto stomach = build_hitbox_geometry({-4.0f, -5.0f, -4.0f}, {4.0f, 5.0f, 4.0f}, 0.0f,
                                               static_cast<int>(hitbox_shape::box), 3, false,
                                               matrix_from_2x4(stomach_raw), 1.0f);

    const std::array<hitbox_geometry, 2> torso{stomach, chest};
    const std::array<hitbox_geometry, 1> only_head{head};
    const weapon_spread_pattern pat(0.0f, 0.06f);

    EXPECT_LT(pat.chance_multi(eye, stomach.center, torso, 2, true),
              pat.chance_multi(eye, stomach.center, torso, 2, false));
    EXPECT_LT(pat.chance_multi(eye, head.center, only_head, 0, true),
              pat.chance_multi(eye, head.center, only_head, 0, false));
}

TEST(lagcomp, is_shot_in_interval_validates_boundaries) {
    using shogod::features::is_shot_in_interval;
    const float nan_val = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(is_shot_in_interval(10.0f, 0.0f, 10.1f));      // no previous sim time
    EXPECT_FALSE(is_shot_in_interval(10.0f, 10.0f, 10.1f));     // equal to prev_sim_time
    EXPECT_TRUE(is_shot_in_interval(10.05f, 10.0f, 10.1f));     // strictly inside
    EXPECT_TRUE(is_shot_in_interval(10.1f, 10.0f, 10.1f));      // exact upper boundary
    EXPECT_FALSE(is_shot_in_interval(10.15f, 10.0f, 10.1f));    // past sim_time
    EXPECT_FALSE(is_shot_in_interval(nan_val, 10.0f, 10.1f));   // non-finite
}

TEST(lagcomp, push_record_detects_shooting_and_deduplicates) {
    shogod::features::lag_compensation lc;
    auto make_hb = [] {
        shogod::sdk::live_hitbox_set hb{};
        const float raw[8] = {100.0f, 0.0f, 50.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        hb.geometries.push_back(build_hitbox_geometry({0, 0, -2}, {0, 0, 2}, 4.0f, 2, 1, false,
                                                      matrix_from_2x4(raw), 1.0f));
        hb.valid = true;
        return hb;
    };

    const std::uintptr_t pawn = 0x123400;
    const std::uint32_t handle = 0x101u;

    // First record cannot be flagged shooting because prev_sim_time == 0.
    EXPECT_TRUE(lc.push_record(1, pawn, handle, 640, 10.0f, {100, 0, 0}, make_hb(), 9.9f, true));
    ASSERT_NE(lc.latest(pawn), nullptr);
    EXPECT_FALSE(lc.latest(pawn)->shooting);

    // Shot occurred inside (10.0, 10.015625].
    EXPECT_TRUE(lc.push_record(1, pawn, handle, 641, 10.015625f, {101, 0, 0}, make_hb(), 10.01f, true));
    ASSERT_NE(lc.latest(pawn), nullptr);
    EXPECT_TRUE(lc.latest(pawn)->shooting);

    // Next tick with same last_shot_time must NOT double-flag shooting.
    EXPECT_TRUE(lc.push_record(1, pawn, handle, 642, 10.03125f, {102, 0, 0}, make_hb(), 10.01f, true));
    ASSERT_NE(lc.latest(pawn), nullptr);
    EXPECT_FALSE(lc.latest(pawn)->shooting);

    // Duplicate sim_tick is skipped.
    EXPECT_FALSE(lc.push_record(1, pawn, handle, 642, 10.03125f, {102, 0, 0}, make_hb(), 10.02f, true));

    // Handle swap resets track so first record under new handle is not shooting.
    EXPECT_TRUE(lc.push_record(1, pawn, handle + 1, 643, 10.046875f, {103, 0, 0}, make_hb(), 10.01f, true));
    ASSERT_NE(lc.latest(pawn), nullptr);
    EXPECT_FALSE(lc.latest(pawn)->shooting);
}

TEST(lagcomp, select_window_handles_ping_drift_and_preserves_shooting) {
    shogod::features::lag_compensation lc;
    auto make_hb = [] {
        shogod::sdk::live_hitbox_set hb{};
        const float raw[8] = {100.0f, 0.0f, 50.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        hb.geometries.push_back(build_hitbox_geometry({0, 0, -2}, {0, 0, 2}, 4.0f, 2, 1, false,
                                                      matrix_from_2x4(raw), 1.0f));
        hb.valid = true;
        return hb;
    };

    const std::uintptr_t pawn = 0xABCD00;
    const std::uint32_t handle = 0x202u;
    for (int t = 100; t <= 114; ++t) {
        const float sim_t = static_cast<float>(t) / 64.0f;
        const float shot_t = (t == 107) ? (sim_t - 0.002f) : 0.0f;
        ASSERT_TRUE(lc.push_record(2, pawn, handle, t, sim_t, {static_cast<float>(t), 0, 0},
                                   make_hb(), shot_t, t == 107));
    }

    std::vector<const shogod::features::lag_record*> out;
    // Ping/interp drift: current_tick is 10 ticks ahead of newest (114).
    lc.select(pawn, handle, 124, 200.0f, out);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0]->simulation_tick, 114);  // newest
    EXPECT_EQ(out[1]->simulation_tick, 107);  // shooting preserved
    EXPECT_TRUE(out[1]->shooting);
    EXPECT_EQ(out[2]->simulation_tick, 102);  // oldest inside 12-tick window (114 - 12 = 102)

    // Zero window returns only newest.
    lc.select(pawn, handle, 124, 0.0f, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->simulation_tick, 114);

    // Excessive drift (current_tick=140 -> 140 - 102 = 38 > 24) prunes ancient historical records
    // while retaining newest.
    lc.select(pawn, handle, 140, 200.0f, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->simulation_tick, 114);
}

TEST(backtrack, deduplicates_high_fps_pushes_and_keeps_handle_swaps) {
    shogod::features::backtrack_history history;
    shogod::sdk::player_snapshot player{};
    player.pawn_handle = 42;
    player.alive = true;
    player.origin = {10.0f, 20.0f, 30.0f};
    player.bone_count = 1;
    player.bones[0] = player.origin;

    // Simulate 300 Hz pushes (5 frames per 64 Hz tick) across 16 distinct ticks (~0.24s).
    float curtime = 10.0f;
    for (int tick = 0; tick < 16; ++tick) {
        const float tick_time = 10.0f + static_cast<float>(tick) * (1.0f / 64.0f);
        for (int sub = 0; sub < 5; ++sub) {
            curtime = tick_time + static_cast<float>(sub) * 0.002f;
            history.push(4, player, curtime);
        }
    }
    EXPECT_EQ(history.record_count(4), 16);

    const auto oldest = history.best(4, 42, 250.0f, curtime);
    ASSERT_TRUE(oldest.has_value());
    EXPECT_NEAR(oldest->curtime, 10.0f, 1e-4f);

    // Handle swap at identical curtime is NOT deduplicated.
    player.pawn_handle = 43;
    history.push(4, player, curtime);
    const auto swapped = history.best(4, 43, 250.0f, curtime);
    ASSERT_TRUE(swapped.has_value());
    EXPECT_FLOAT_EQ(swapped->curtime, curtime);

    // Negative curtime is rejected while curtime == 0.0f is accepted.
    player.pawn_handle = 44;
    history.push(5, player, -1.0f);
    EXPECT_EQ(history.record_count(5), 0);
    history.push(5, player, 0.0f);
    EXPECT_EQ(history.record_count(5), 1);
    EXPECT_TRUE(history.best(5, 44, 200.0f, 0.05f).has_value());
}

TEST(lagcomp, sim_tick_zero_derivation_rejection_and_nonmonotonic_shooting) {
    shogod::features::lag_compensation lc;
    auto make_hb = [] {
        shogod::sdk::live_hitbox_set hb{};
        const float raw[8] = {100.0f, 0.0f, 50.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        hb.geometries.push_back(build_hitbox_geometry({0, 0, -2}, {0, 0, 2}, 4.0f, 2, 1, false,
                                                      matrix_from_2x4(raw), 1.0f));
        hb.valid = true;
        return hb;
    };

    const std::uintptr_t pawn = 0xCAFE00;
    const std::uint32_t handle = 0x303u;

    // 1. sim_tick == 0 with sim_time <= 0 (or non-finite) is rejected and does not flood ring.
    EXPECT_FALSE(lc.push_record(3, pawn, handle, 0, 0.0f, {10, 0, 0}, make_hb(), 0.0f, false));
    EXPECT_FALSE(lc.push_record(3, pawn, handle, -5, -1.0f, {10, 0, 0}, make_hb(), 0.0f, false));
    EXPECT_FALSE(lc.push_record(3, pawn, handle, 0, std::numeric_limits<float>::infinity(),
                                {10, 0, 0}, make_hb(), 0.0f, false));
    EXPECT_EQ(lc.latest(pawn), nullptr);

    // 2. sim_tick == 0 with positive sim_time derives tick = round(sim_time * 64) and deduplicates.
    EXPECT_TRUE(lc.push_record(3, pawn, handle, 0, 10.0f, {20, 0, 0}, make_hb(), 0.0f, false));
    ASSERT_NE(lc.latest(pawn), nullptr);
    EXPECT_EQ(lc.latest(pawn)->simulation_tick, 640);
    EXPECT_FALSE(lc.push_record(3, pawn, handle, 0, 10.0f, {20, 0, 0}, make_hb(), 0.0f, false));

    // Ancient derived tick 640 cannot leak through select when current_tick is far ahead.
    std::vector<const shogod::features::lag_record*> out;
    lc.select(pawn, handle, 2000, 200.0f, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->simulation_tick, 640);

    // 3. Non-monotonic jitter: 500 -> 505(shooting) -> 502 must reject 502 and keep 505* shooting.
    const std::uintptr_t pawn2 = 0xBEEF00;
    const std::uint32_t handle2 = 0x404u;
    EXPECT_TRUE(lc.push_record(4, pawn2, handle2, 500, 500.0f / 64.0f, {1, 0, 0}, make_hb(), 0.0f, false));
    EXPECT_TRUE(lc.push_record(4, pawn2, handle2, 505, 505.0f / 64.0f, {2, 0, 0}, make_hb(),
                               504.5f / 64.0f, true));
    EXPECT_FALSE(lc.push_record(4, pawn2, handle2, 502, 502.0f / 64.0f, {3, 0, 0}, make_hb(),
                                0.0f, false));
    lc.select(pawn2, handle2, 505, 200.0f, out);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0]->simulation_tick, 505);
    EXPECT_TRUE(out[0]->shooting);
    EXPECT_EQ(out[1]->simulation_tick, 500);

    // 4. Major clock reset (> k_lagcomp_max_drift_ticks) resets track cleanly with shooting = false.
    EXPECT_TRUE(lc.push_record(4, pawn2, handle2, 100, 100.0f / 64.0f, {4, 0, 0}, make_hb(),
                               99.5f / 64.0f, true));
    ASSERT_NE(lc.latest(pawn2), nullptr);
    EXPECT_EQ(lc.latest(pawn2)->simulation_tick, 100);
    EXPECT_FALSE(lc.latest(pawn2)->shooting);
    lc.select(pawn2, handle2, 100, 200.0f, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->simulation_tick, 100);
}

TEST(hitbox_geom, pitch_roll_3d_obb_and_non_unit_scale) {
    // Quaternion combining 30 deg pitch (Y), 45 deg roll (X), and 20 deg yaw (Z) with scale 1.75.
    auto make_quat = [](float rx, float ry, float rz) {
        const float cx = std::cos(rx * 0.5f), sx = std::sin(rx * 0.5f);
        const float cy = std::cos(ry * 0.5f), sy = std::sin(ry * 0.5f);
        const float cz = std::cos(rz * 0.5f), sz = std::sin(rz * 0.5f);
        return std::array<float, 4>{
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz,
        };
    };
    const auto q = make_quat(0.78539816f, 0.52359878f, 0.34906585f);
    for (float s : {0.5f, 1.75f, -0.5f}) {
        const float raw[8] = {150.0f, -20.0f, 45.0f, s, q[0], q[1], q[2], q[3]};
        const mat3x4 tm = matrix_from_2x4(raw);
        const auto box = build_hitbox_geometry({-4.0f, -6.0f, -8.0f}, {4.0f, 6.0f, 8.0f}, 0.0f,
                                               static_cast<int>(hitbox_shape::box), 2, false, tm, s);
        ASSERT_TRUE(box.valid);
        const float abs_s = std::fabs(s);
        EXPECT_NEAR(box.half_extents.x, 4.0f * abs_s, 1e-4f);
        EXPECT_NEAR(box.half_extents.y, 6.0f * abs_s, 1e-4f);
        EXPECT_NEAR(box.half_extents.z, 8.0f * abs_s, 1e-4f);

        const vec3 eye{0.0f, 0.0f, 45.0f};
        // Local offset (3.5, 5.5, 7.5) transformed into world hits; ray parallel to X face at x_local=4.5 misses.
        const vec3 inside_pt = box.center + box.axis_x * (3.5f * abs_s) +
                               box.axis_y * (5.5f * abs_s) + box.axis_z * (7.5f * abs_s);
        const vec3 outside_eye = box.center + box.axis_x * (4.5f * abs_s) + box.axis_y * 25.0f;
        EXPECT_TRUE(ray_hits_geometry(eye, (inside_pt - eye).normalised(), box));
        EXPECT_FALSE(ray_hits_geometry(outside_eye, box.axis_y * -1.0f, box));
        EXPECT_TRUE(ray_hits_geometry_scaled(eye, (box.center - eye).normalised(), box, 0.5f));

        std::array<vec3, 9> pts{};
        const std::size_t n = generate_hitbox_points(box, eye, 0.9f, pts);
        EXPECT_GE(n, 5u);
        for (std::size_t i = 0; i < n; ++i) {
            EXPECT_TRUE(ray_hits_geometry(eye, (pts[i] - eye).normalised(), box));
        }
    }
}

TEST(hitbox_geom, capsule_sphere_intersections_vertical_multipoints_and_nan_guards) {
    const float raw[8] = {100.0f, 0.0f, 60.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const mat3x4 tm = matrix_from_2x4(raw);

    // Capsule along Z from -5 to +5 with radius 3.
    const auto cap = build_hitbox_geometry({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f}, 3.0f,
                                           static_cast<int>(hitbox_shape::capsule), 1, false, tm, 1.0f);
    ASSERT_TRUE(cap.valid);
    const vec3 eye{0.0f, 0.0f, 60.0f};
    EXPECT_TRUE(ray_hits_geometry(eye, (vec3{100.0f, 0.0f, 66.5f} - eye).normalised(), cap));
    EXPECT_FALSE(ray_hits_geometry(eye, (vec3{100.0f, 3.5f, 60.0f} - eye).normalised(), cap));
    EXPECT_TRUE(ray_hits_geometry_scaled(eye, (vec3{100.0f, 0.0f, 63.0f} - eye).normalised(), cap, 0.5f));
    EXPECT_FALSE(ray_hits_geometry_scaled(eye, (vec3{100.0f, 0.0f, 65.5f} - eye).normalised(), cap, 0.5f));
    // Parallel ray along Z inside capsule radius vs outside.
    EXPECT_TRUE(ray_hits_geometry({101.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, cap));
    EXPECT_FALSE(ray_hits_geometry({104.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, cap));
    // Behind origin vs starting inside.
    EXPECT_FALSE(ray_hits_geometry({100.0f, 0.0f, 80.0f}, {0.0f, 0.0f, 1.0f}, cap));
    EXPECT_TRUE(ray_hits_geometry({100.0f, 0.0f, 60.0f}, {0.0f, 0.0f, 1.0f}, cap));

    // Vertical view (|view.z| >= 0.98) multipoints for head (group == 0) stay inside capsule.
    const vec3 top_eye{100.0f, 0.0f, 150.0f};
    std::array<vec3, 9> pts{};
    const std::size_t n = generate_hitbox_points(cap, top_eye, 0.9f, pts);
    EXPECT_GE(n, 7u);
    for (std::size_t i = 0; i < n; ++i) {
        EXPECT_TRUE(ray_hits_geometry(top_eye, (pts[i] - top_eye).normalised(), cap));
    }

    // Sphere (and degenerate capsule first == last).
    const auto sph = build_hitbox_geometry({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 4.0f,
                                           static_cast<int>(hitbox_shape::sphere), 2, false, tm, 1.0f);
    ASSERT_TRUE(sph.valid);
    EXPECT_TRUE(ray_hits_geometry(eye, (vec3{100.0f, 3.8f, 60.0f} - eye).normalised(), sph));
    EXPECT_FALSE(ray_hits_geometry(eye, (vec3{100.0f, 4.2f, 60.0f} - eye).normalised(), sph));

    // NaN / Inf scale guards.
    const float nan_val = std::numeric_limits<float>::quiet_NaN();
    const float inf_val = std::numeric_limits<float>::infinity();
    EXPECT_EQ(generate_hitbox_points(cap, eye, nan_val, pts), 1u);
    EXPECT_EQ(generate_hitbox_points(cap, eye, inf_val, pts), 1u);
    EXPECT_FALSE(ray_hits_geometry_scaled(eye, {1.0f, 0.0f, 0.0f}, cap, nan_val));
    EXPECT_FALSE(ray_hits_geometry_scaled(eye, {1.0f, 0.0f, 0.0f}, cap, inf_val));
    EXPECT_FALSE(build_hitbox_geometry({0, 0, -2}, {0, 0, 2}, 4.0f, 2, 1, false, tm, nan_val).valid);
    EXPECT_FLOAT_EQ(effective_multipoint_scale(nan_val, 50.0f), 0.0f);
    EXPECT_FLOAT_EQ(effective_multipoint_scale(50.0f, inf_val), 0.0f);
}

TEST(hitchance, chance_multi_covers_pelvis_legs_arms_wildcard_and_strict_unit_scale) {
    const vec3 eye{0.0f, 0.0f, 50.0f};
    const float chest_raw[8] = {200.0f, 0.0f, 54.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float stomach_raw[8] = {200.0f, 0.0f, 42.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float pelvis_raw[8] = {200.0f, 0.0f, 32.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float arm_raw[8] = {200.0f, 10.0f, 48.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float leg_raw[8] = {200.0f, 0.0f, 20.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    // hitgroup 8 (neck) -> group 1, hitgroup 0 (pelvis) -> group 3, hitgroup 4 (arm) -> group 5,
    // hitgroup 6 (leg) -> group 4.
    const auto neck = build_hitbox_geometry({-4.0f, -5.0f, -4.0f}, {4.0f, 5.0f, 4.0f}, 0.0f,
                                            static_cast<int>(hitbox_shape::box), 8, false,
                                            matrix_from_2x4(chest_raw), 1.0f);
    const auto stomach = build_hitbox_geometry({-4.0f, -5.0f, -4.0f}, {4.0f, 5.0f, 4.0f}, 0.0f,
                                               static_cast<int>(hitbox_shape::box), 3, false,
                                               matrix_from_2x4(stomach_raw), 1.0f);
    const auto pelvis = build_hitbox_geometry({-4.0f, -5.0f, -4.0f}, {4.0f, 5.0f, 4.0f}, 0.0f,
                                              static_cast<int>(hitbox_shape::box), 0, false,
                                              matrix_from_2x4(pelvis_raw), 1.0f);
    const auto arm = build_hitbox_geometry({-3.0f, -4.0f, -5.0f}, {3.0f, 4.0f, 5.0f}, 0.0f,
                                           static_cast<int>(hitbox_shape::box), 4, false,
                                           matrix_from_2x4(arm_raw), 1.0f);
    const auto leg = build_hitbox_geometry({-3.0f, -4.0f, -6.0f}, {3.0f, 4.0f, 6.0f}, 0.0f,
                                           static_cast<int>(hitbox_shape::box), 6, false,
                                           matrix_from_2x4(leg_raw), 1.0f);

    const weapon_spread_pattern pat(0.0f, 0.08f);
    const std::array<hitbox_geometry, 5> all_boxes{neck, stomach, pelvis, arm, leg};

    // Pelvis (3) counts torso spillover when !strict, but never arms/legs; strict blocks spillover even at hitbox_scale=1.0f.
    const std::array<hitbox_geometry, 1> only_pelvis{pelvis};
    const std::array<hitbox_geometry, 2> pelvis_arm{pelvis, arm};
    const std::array<hitbox_geometry, 2> pelvis_leg{pelvis, leg};
    const float hc_pelvis_only = pat.chance_multi(eye, pelvis.center, only_pelvis, 3, false, 1.0f);
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, pelvis.center, pelvis_arm, 3, false, 1.0f), hc_pelvis_only);
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, pelvis.center, pelvis_leg, 3, false, 1.0f), hc_pelvis_only);
    EXPECT_GT(pat.chance_multi(eye, pelvis.center, all_boxes, 3, false, 1.0f), hc_pelvis_only);
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, pelvis.center, all_boxes, 3, true, 1.0f), hc_pelvis_only);

    // Legs (4) and Arms (5) never accept torso spillover even when !strict.
    const std::array<hitbox_geometry, 1> only_leg{leg};
    const std::array<hitbox_geometry, 1> only_arm{arm};
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, leg.center, all_boxes, 4, false, 1.0f),
                    pat.chance_multi(eye, leg.center, only_leg, 4, false, 1.0f));
    EXPECT_FLOAT_EQ(pat.chance_multi(eye, arm.center, all_boxes, 5, false, 1.0f),
                    pat.chance_multi(eye, arm.center, only_arm, 5, false, 1.0f));

    // Wildcard (-1) matches all valid groups and beats strict single group.
    EXPECT_GT(pat.chance_multi(eye, stomach.center, all_boxes, -1, false, 1.0f),
              pat.chance_multi(eye, stomach.center, all_boxes, 2, true, 1.0f));
    // safe_point_hitbox_scale handles NaN safely and feeds into chance_multi cleanly.
    const float sp_scale = safe_point_hitbox_scale(std::numeric_limits<float>::quiet_NaN(), true);
    EXPECT_FLOAT_EQ(sp_scale, 0.85f);
    EXPECT_LE(pat.chance_multi(eye, neck.center, all_boxes, 1, true, sp_scale),
              pat.chance_multi(eye, neck.center, all_boxes, 1, false, 1.0f));
}
