#include <gtest/gtest.h>

#include "features/aaa/antiaim.hpp"
#include "features/backtrack.hpp"
#include "features/rage/target.hpp"
#include "sdk/snapshot.hpp"

using namespace shogod;

TEST(snapshot, publishes_and_acquires) {
    sdk::publisher pub;
    sdk::frame_snapshot& frame = pub.write_buffer();
    frame.count = 2;
    frame.view_matrix.at(0, 0) = 1.0f;
    frame.view_matrix.at(1, 1) = 1.0f;
    frame.view_matrix.at(2, 2) = 1.0f;
    frame.view_matrix.at(3, 3) = 1.0f;
    frame.players[0].pawn = 0x1234;
    frame.players[0].alive = true;
    pub.publish();

    const sdk::frame_snapshot* read = pub.acquire();
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(read->count, 2);
    EXPECT_EQ(read->players[0].pawn, 0x1234u);
    EXPECT_EQ(read->seq % 2, 0u);
}

TEST(snapshot, rejects_non_finite_matrix) {
    sdk::publisher pub;
    sdk::frame_snapshot& frame = pub.write_buffer();
    frame.count = 1;
    frame.view_matrix.at(1, 2) = std::numeric_limits<float>::quiet_NaN();
    pub.publish();
    EXPECT_EQ(pub.acquire(), nullptr);
}

TEST(backtrack, newest_inside_window_wins) {
    features::backtrack_history history;
    sdk::player_snapshot player;
    player.alive = true;
    player.origin = {1.0f, 2.0f, 3.0f};
    player.bone_count = 2;
    player.bones[0] = {1, 2, 3};

    history.push(3, player, 10.0f);
    history.push(3, player, 10.1f);
    history.push(3, player, 10.2f);

    const auto* best = history.best(3, 200.0f, 10.25f);
    ASSERT_NE(best, nullptr);
    EXPECT_FLOAT_EQ(best->curtime, 10.2f);
    EXPECT_EQ(history.record_count(3), 3);
}

TEST(backtrack, expired_and_out_of_range) {
    features::backtrack_history history;
    sdk::player_snapshot player;
    player.alive = true;
    player.bone_count = 1;
    history.push(0, player, 1.0f);

    EXPECT_EQ(history.best(0, 100.0f, 2.0f), nullptr);
    EXPECT_EQ(history.best(-1, 100.0f, 1.0f), nullptr);
    EXPECT_EQ(history.best(64, 100.0f, 1.0f), nullptr);
    EXPECT_EQ(history.best(0, 0.0f, 1.0f), nullptr);
}

TEST(backtrack, rejects_dead_or_boneless) {
    features::backtrack_history history;
    sdk::player_snapshot player;
    player.alive = false;
    player.bone_count = 4;
    history.push(1, player, 5.0f);

    player.alive = true;
    player.bone_count = 0;
    history.push(1, player, 5.0f);

    EXPECT_EQ(history.record_count(1), 0);
}

TEST(target, lowest_hp_priority) {
    const features::target_candidate candidates[] = {
        {0, 10.0f, 5.0f, 100, 0.2f, true, true},
        {1, 20.0f, 10.0f, 15, 0.9f, true, true},
        {2, 30.0f, 50.0f, 60, 0.5f, false, true},
    };
    int cursor = 0;
    EXPECT_EQ(features::pick_target(candidates, config::aim_priority::lowest_hp, 45.0f, false,
                                    cursor),
              1);
    EXPECT_EQ(features::pick_target(candidates, config::aim_priority::distance, 45.0f, true,
                                    cursor),
              0);
    EXPECT_EQ(features::pick_target(candidates, config::aim_priority::threat, 45.0f, false, cursor),
              1);
}

TEST(target, fov_filter_can_empty_the_set) {
    const features::target_candidate candidates[] = {
        {0, 50.0f, 30.0f, 100, 0.2f, true, true},
    };
    int cursor = 0;
    EXPECT_EQ(features::pick_target(candidates, config::aim_priority::distance, 20.0f, false,
                                    cursor),
              -1);
}

TEST(target, cycle_walks_the_list) {
    const features::target_candidate candidates[] = {
        {0, 10.0f, 5.0f, 100, 0.2f, true, true},
        {1, 10.0f, 31.0f, 100, 0.2f, true, true},
    };
    int cursor = 0;
    EXPECT_EQ(features::pick_target(candidates, config::aim_priority::cycle, 45.0f, true, cursor),
              0);
    EXPECT_EQ(features::pick_target(candidates, config::aim_priority::cycle, 45.0f, true, cursor),
              1);
    EXPECT_EQ(features::pick_target(candidates, config::aim_priority::cycle, 45.0f, true, cursor),
              0);
}

TEST(target, fire_gate) {
    config::rage_weapon w;
    w.hitchance = 60.0f;
    w.min_damage = 30.0f;
    EXPECT_TRUE(features::should_fire(70.0f, 40.0f, w));
    EXPECT_FALSE(features::should_fire(50.0f, 40.0f, w));
    EXPECT_FALSE(features::should_fire(70.0f, 20.0f, w));
}

TEST(antiaim, disabled_returns_real) {
    config::anti_aim_cfg cfg;
    cfg.enabled = false;
    const math::angles real{10.0f, 45.0f};
    const math::angles out = features::apply_antiaim(cfg, real, {});
    EXPECT_FLOAT_EQ(out.pitch, 10.0f);
    EXPECT_FLOAT_EQ(out.yaw, 45.0f);
}

TEST(antiaim, pitch_modes) {
    config::anti_aim_cfg cfg;
    cfg.enabled = true;
    cfg.base_yaw = config::yaw_mode::off;
    const math::angles real{10.0f, 45.0f};

    cfg.base_pitch = config::pitch_mode::down;
    EXPECT_FLOAT_EQ(features::apply_antiaim(cfg, real, {}).pitch, 89.0f);

    cfg.base_pitch = config::pitch_mode::up;
    EXPECT_FLOAT_EQ(features::apply_antiaim(cfg, real, {}).pitch, -89.0f);

    cfg.base_pitch = config::pitch_mode::zero;
    EXPECT_FLOAT_EQ(features::apply_antiaim(cfg, real, {}).pitch, 0.0f);
}

TEST(antiaim, jitter_alternates_and_normalizes) {
    config::anti_aim_cfg cfg;
    cfg.enabled = true;
    cfg.base_yaw = config::yaw_mode::jitter;
    cfg.jitter_range = 30.0f;
    cfg.base_pitch = config::pitch_mode::off;

    const math::angles real{0.0f, 170.0f};
    features::antiaim_state state;
    state.tick = 0;
    const float even = features::apply_antiaim(cfg, real, state).yaw;
    state.tick = 1;
    const float odd = features::apply_antiaim(cfg, real, state).yaw;

    EXPECT_NE(even, odd);
    EXPECT_LE(std::fabs(even), 180.0f);
    EXPECT_LE(std::fabs(odd), 180.0f);
}

TEST(antiaim, random_is_deterministic_per_tick) {
    config::anti_aim_cfg cfg;
    cfg.enabled = true;
    cfg.base_yaw = config::yaw_mode::random;
    cfg.jitter_range = 60.0f;
    cfg.base_pitch = config::pitch_mode::off;

    features::antiaim_state state;
    state.tick = 120;
    state.seed = 5;
    const float a = features::apply_antiaim(cfg, {0.0f, 0.0f}, state).yaw;
    const float b = features::apply_antiaim(cfg, {0.0f, 0.0f}, state).yaw;
    EXPECT_FLOAT_EQ(a, b);
    EXPECT_LE(std::fabs(a), 60.0f);
}

TEST(antiaim, air_gate_falls_through) {
    config::anti_aim_cfg cfg;
    cfg.enabled = true;
    cfg.base_yaw = config::yaw_mode::fake_static;
    cfg.base_pitch = config::pitch_mode::down;
    cfg.on_air = false;

    features::antiaim_state state;
    state.on_ground = false;
    const math::angles real{5.0f, 20.0f};
    const math::angles out = features::apply_antiaim(cfg, real, state);
    EXPECT_FLOAT_EQ(out.pitch, 5.0f);
    EXPECT_FLOAT_EQ(out.yaw, 20.0f);
}
