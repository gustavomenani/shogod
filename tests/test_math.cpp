#include <gtest/gtest.h>

#include "features/math/angles.hpp"
#include "features/math/hitchance.hpp"
#include "features/math/hull.hpp"
#include "features/math/mat4.hpp"

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

TEST(hitchance, wider_cone_lowers_count) {
    const vec3 eye{0, 0, 0};
    const vec3 target{0, 100, 0};
    const float tight = hitchance(eye, target, 2.0f, 0.5f);
    const float wide = hitchance(eye, target, 2.0f, 10.0f);
    EXPECT_GT(tight, wide);
    EXPECT_LE(wide, 1.0f);
    EXPECT_GE(wide, 0.0f);
}

TEST(hitchance, bigger_target_raises_count) {
    const vec3 eye{0, 0, 0};
    const vec3 target{0, 100, 0};
    EXPECT_GE(hitchance(eye, target, 6.0f, 3.0f), hitchance(eye, target, 1.0f, 3.0f));
}
