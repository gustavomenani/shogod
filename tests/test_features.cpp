#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#include "config/bind_state.hpp"

#include "features/aaa/antiaim.hpp"
#include "features/backtrack.hpp"
#include "features/createmove.hpp"
#include "features/esp/esp.hpp"
#include "features/math/hitbox_geom.hpp"
#include "features/math/hitchance.hpp"
#include "features/rage/target.hpp"
#include "sdk/snapshot.hpp"
#include "sdk/usercmd.hpp"

using namespace shogod;

TEST(binds, focus_loss_disarms_held_keys_without_toggling) {
    config::bind_state state;
    config::keybind toggle{77, false, false};
    EXPECT_FALSE(state.sample(toggle, true, true, true));
    EXPECT_FALSE(state.sample(toggle, true, true, false));
    EXPECT_TRUE(state.sample(toggle, true, true, true));
    EXPECT_TRUE(state.sample(toggle, true, true, true));
    EXPECT_TRUE(state.sample(toggle, true, false, false));
    EXPECT_TRUE(state.sample(toggle, true, true, true));
    EXPECT_TRUE(state.sample(toggle, true, true, false));
    EXPECT_FALSE(state.sample(toggle, true, true, true));
    toggle.hold = true;
    EXPECT_FALSE(state.sample(toggle, true, true, true));
    EXPECT_FALSE(state.sample(toggle, true, true, false));
    EXPECT_TRUE(state.sample(toggle, true, true, true));
    EXPECT_FALSE(state.sample(toggle, true, false, false));
    EXPECT_FALSE(state.sample(toggle, true, true, true));
    EXPECT_FALSE(state.sample(toggle, false, true, true));
    EXPECT_TRUE(state.sample({}, true, false, false));
}

TEST(binds, anti_aim_bind_toggle_hold_and_unbound) {
    config::bind_state state;
    const std::uint32_t aa_bit = config::feature_bit(config::bound_feature::anti_aim);
    EXPECT_NE(aa_bit, 0u);
    EXPECT_NE(aa_bit, config::feature_bit(config::bound_feature::rage));

    const config::keybind unbound{0, false, false};
    EXPECT_FALSE(state.sample(unbound, false, true, false));
    EXPECT_TRUE(state.sample(unbound, true, true, false));

    const config::keybind toggle{0x56, false, false};
    EXPECT_FALSE(state.sample(toggle, true, true, true));   // pre-held ignored
    EXPECT_FALSE(state.sample(toggle, true, true, false));  // armed
    EXPECT_TRUE(state.sample(toggle, true, true, true));    // toggled ON
    std::uint32_t mask = state.sample(toggle, true, false, false) ? aa_bit : 0u;
    EXPECT_NE(mask & aa_bit, 0u);

    const config::keybind hold{0x56, true, false};
    EXPECT_FALSE(state.sample(hold, true, true, false));    // armed
    EXPECT_TRUE(state.sample(hold, true, true, true));      // held ON
    EXPECT_FALSE(state.sample(hold, false, true, true));    // master disabled -> off
}

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

    const auto read = pub.acquire();
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->count, 2);
    EXPECT_EQ(read->players[0].pawn, 0x1234u);
    EXPECT_EQ(read->seq % 2, 0u);

    pub.write_buffer().count = 3;
    pub.publish();
    pub.write_buffer().count = 4;
    pub.publish();
    EXPECT_EQ(read->count, 2);
    const auto latest = pub.acquire();
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->count, 4);
}

TEST(snapshot, rejects_non_finite_matrix) {
    sdk::publisher pub;
    sdk::frame_snapshot& frame = pub.write_buffer();
    frame.count = 1;
    frame.view_matrix.at(1, 2) = std::numeric_limits<float>::quiet_NaN();
    pub.publish();
    EXPECT_FALSE(pub.acquire().has_value());
}

TEST(backtrack, oldest_inside_window_wins) {
    features::backtrack_history history;
    sdk::player_snapshot player;
    player.pawn_handle = 7;
    player.alive = true;
    player.origin = {1.0f, 2.0f, 3.0f};
    player.bone_count = 2;
    player.bones[0] = {1, 2, 3};

    history.push(3, player, 10.0f);
    history.push(3, player, 10.1f);
    history.push(3, player, 10.2f);

    const auto best = history.best(3, 7, 200.0f, 10.25f);
    ASSERT_TRUE(best.has_value());
    EXPECT_FLOAT_EQ(best->curtime, 10.1f);
    EXPECT_EQ(history.record_count(3), 3);
}

TEST(backtrack, expired_and_out_of_range) {
    features::backtrack_history history;
    sdk::player_snapshot player;
    player.alive = true;
    player.bone_count = 1;
    player.pawn_handle = 7;
    history.push(0, player, 1.0f);

    EXPECT_FALSE(history.best(0, 7, 100.0f, 2.0f).has_value());
    EXPECT_FALSE(history.best(-1, 7, 100.0f, 1.0f).has_value());
    EXPECT_FALSE(history.best(65, 7, 100.0f, 1.0f).has_value());
    EXPECT_FALSE(history.best(0, 7, 0.0f, 1.0f).has_value());
}

TEST(backtrack, accepts_last_player_slot_and_rejects_replaced_pawn) {
    features::backtrack_history history;
    sdk::player_snapshot player{};
    player.alive = true;
    player.bone_count = 1;
    player.pawn_handle = 11;
    history.push(64, player, 1.0f);
    EXPECT_TRUE(history.best(64, 11, 100.0f, 1.0f).has_value());
    EXPECT_FALSE(history.best(64, 12, 100.0f, 1.0f).has_value());
    player.pawn_handle = 12;
    history.push(64, player, 1.04f);
    const auto pose = history.best(64, 12, 100.0f, 1.05f);
    ASSERT_TRUE(pose.has_value());
    EXPECT_FLOAT_EQ(pose->curtime, 1.04f);
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
    w.min_damage = 0.0f;
    EXPECT_FALSE(features::should_fire(100.0f, 0.0f, w));
}

TEST(target, estimates_range_hitgroup_and_armor_separately) {
    using math::hitgroup;
    EXPECT_FLOAT_EQ(features::estimate_hit_damage(80, 500, 0.9f, 4, 1, 100, true, hitgroup::chest), 36.0f);
    EXPECT_FLOAT_EQ(features::estimate_hit_damage(80, 500, 0.9f, 4, 1, 100, false, hitgroup::head), 288.0f);
    EXPECT_FLOAT_EQ(features::estimate_hit_damage(80, 500, 0.9f, 4, 1, 100, true, hitgroup::head), 144.0f);
    EXPECT_FLOAT_EQ(features::estimate_hit_damage(80, 500, 0.9f, 4, 1, 1, true, hitgroup::chest), 70.0f);
}

TEST(target, delay_shot_counts_consecutive_ticks_for_the_selected_threshold) {
    config::rage_weapon weapon;
    weapon.delay_ticks = 3;
    weapon.delay_shot = config::delay_shot_mode::damage;
    features::shot_delay delay;
    EXPECT_FALSE(delay.ready(1, 2, 10, weapon, 0.0f, 40.0f));
    EXPECT_FALSE(delay.ready(1, 2, 10, weapon, 0.0f, 40.0f));
    EXPECT_FALSE(delay.ready(1, 2, 11, weapon, 0.0f, 40.0f));
    EXPECT_TRUE(delay.ready(1, 2, 12, weapon, 0.0f, 40.0f));
    EXPECT_FALSE(delay.ready(3, 2, 13, weapon, 0.0f, 40.0f));
    EXPECT_FALSE(delay.ready(3, 2, 15, weapon, 0.0f, 40.0f));
    EXPECT_FALSE(delay.ready(3, 4, 16, weapon, 0.0f, 40.0f));
    EXPECT_FALSE(delay.ready(3, 4, -1, weapon, 70.0f, 40.0f));
    weapon.delay_ticks = 2;
    weapon.delay_shot = config::delay_shot_mode::hitchance;
    EXPECT_FALSE(delay.ready(3, 4, 17, weapon, 70.0f, 0.0f));
    EXPECT_TRUE(delay.ready(3, 4, 18, weapon, 70.0f, 0.0f));
    weapon.delay_shot = config::delay_shot_mode::both;
    EXPECT_FALSE(delay.ready(3, 4, 19, weapon, 70.0f, 0.0f));
    EXPECT_FALSE(delay.ready(3, 4, 20, weapon, 70.0f, 40.0f));
    EXPECT_TRUE(delay.ready(3, 4, 21, weapon, 70.0f, 40.0f));
}

TEST(target, extrapolation_uses_seconds_and_bounds_prediction) {
    const math::vec3 point{10.0f, 20.0f, 30.0f};
    const math::vec3 velocity{200.0f, -100.0f, 100.0f};
    const auto predicted = features::extrapolate_point(point, velocity, 100.0f);
    EXPECT_FLOAT_EQ(predicted.x, 20.0f);
    EXPECT_FLOAT_EQ(predicted.y, 15.0f);
    EXPECT_FLOAT_EQ(predicted.z, 35.0f);
    EXPECT_FLOAT_EQ(features::extrapolate_point(point, velocity, 0.0f).x, point.x);
}

TEST(target, counter_strafe_opposes_world_velocity_in_view_axes) {
    const auto direct = features::counter_strafe({100.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_FLOAT_EQ(direct.x, -1.0f);
    EXPECT_FLOAT_EQ(direct.y, 0.0f);
    const auto turned = features::counter_strafe({100.0f, 0.0f, 0.0f}, 90.0f);
    EXPECT_NEAR(turned.x, 0.0f, 0.0001f);
    EXPECT_FLOAT_EQ(turned.y, -1.0f);
}

TEST(antiaim, disabled_returns_real) {
    config::anti_aim_cfg cfg;
    cfg.enabled = false;
    const math::angles real{10.0f, 45.0f};
    const math::angles out = features::apply_antiaim(cfg, real, {});
    EXPECT_FLOAT_EQ(out.pitch, 10.0f);
    EXPECT_FLOAT_EQ(out.yaw, 45.0f);
}

TEST(antiaim, preserves_camera_pitch_with_legacy_profiles) {
    config::anti_aim_cfg cfg;
    cfg.enabled = true;
    cfg.base_yaw = config::yaw_mode::off;
    const math::angles real{10.0f, 45.0f};

    cfg.base_pitch = config::pitch_mode::down;
    EXPECT_FLOAT_EQ(features::apply_antiaim(cfg, real, {}).pitch, 10.0f);

    cfg.base_pitch = config::pitch_mode::up;
    EXPECT_FLOAT_EQ(features::apply_antiaim(cfg, real, {}).pitch, 10.0f);

    cfg.base_pitch = config::pitch_mode::zero;
    EXPECT_FLOAT_EQ(features::apply_antiaim(cfg, real, {}).pitch, 10.0f);
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

TEST(antiaim, applies_yaw_offset_and_normalizes) {
    config::anti_aim_cfg cfg;
    cfg.enabled = true;
    cfg.base_yaw = config::yaw_mode::fake_static;
    cfg.yaw_offset = 45.0f;
    const math::angles out = features::apply_antiaim(cfg, {0.0f, 170.0f}, {});
    EXPECT_FLOAT_EQ(out.yaw, 35.0f);
}

TEST(antiaim, static_backward_does_not_jitter_while_shooting) {
    config::anti_aim_cfg cfg;
    cfg.enabled = true;
    cfg.base_yaw = config::yaw_mode::fake_static;
    cfg.on_shot = true;

    const math::angles real{0.0f, 45.0f};
    features::antiaim_state s{};
    s.shooting = true;
    s.tick = 0;
    const float y0 = features::apply_antiaim(cfg, real, s).yaw;
    s.tick = 1;
    const float y1 = features::apply_antiaim(cfg, real, s).yaw;
    s.tick = 2;
    const float y2 = features::apply_antiaim(cfg, real, s).yaw;

    EXPECT_FLOAT_EQ(y0, -135.0f);
    EXPECT_FLOAT_EQ(y1, y0);
    EXPECT_FLOAT_EQ(y2, y0);
}

TEST(antiaim, ground_move_alignment_hits_exact_45_deg_sectors) {
    const float cams[] = {-179.0f, -123.4f, -45.0f, 0.0f, 37.8f, 162.5f};
    const float yaws[] = {-170.0f, -110.3f, -22.4f, 15.2f, 99.9f, 179.4f};
    for (float cam : cams) {
        for (float aa : yaws) {
            const float aligned = features::align_aa_yaw_for_ground_move(cam, aa);
            const float rel = math::yaw_delta(cam, aligned);
            EXPECT_NEAR(std::remainder(rel, 45.0f), 0.0f, 0.001f);
            EXPECT_LE(std::fabs(aligned), 180.0f);
        }
    }
}

TEST(antiaim, head_center_trim_converges_to_cancel_pose_twist) {
    // Calibrated default and weapon-type-aware pose offsets for Auto adjust.
    EXPECT_FLOAT_EQ(features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::scout), 89.0f), 36.0f);
    EXPECT_FLOAT_EQ(features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 89.0f), 36.0f);
    EXPECT_FLOAT_EQ(features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::pistol), 89.0f), 18.0f);
    EXPECT_FLOAT_EQ(features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::heavy_pistol), 89.0f), 18.0f);
    EXPECT_FLOAT_EQ(features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::scout), 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), -89.0f), 0.0f);
    // Smooth scaling over [35, 89] deg pitch down, and 0 for knife/C4/grenade.
    EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), -1, 35.0f), 27.0f);
    EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 8, 89.0f), 18.0f);
    EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 0, 89.0f), 0.0f);
    EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 7, 89.0f), 0.0f);
    EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 9, 89.0f), 0.0f);
    // Explicit weapon_type >= 0 takes strict precedence over weapon_group; unknown weapon_type > 9 returns 0.
    EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::pistol), 3, 89.0f), 36.0f);
    EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(-1, 10, 89.0f), 0.0f);
}

TEST(antiaim, full_independence_matrix_all_modes_orthogonal) {
    const math::angles camera_view{12.0f, 25.0f};
    const float enemy_yaw = -70.0f;

    const std::array<config::yaw_mode, 4> yaws = {
        config::yaw_mode::off,
        config::yaw_mode::fake_static,
        config::yaw_mode::jitter,
        config::yaw_mode::random,
    };
    const std::array<config::pitch_mode, 4> pitches = {
        config::pitch_mode::off,
        config::pitch_mode::down,
        config::pitch_mode::up,
        config::pitch_mode::zero,
    };

    for (bool at_targets : {false, true}) {
        for (bool auto_adjust : {false, true}) {
            for (bool freestanding : {false, true}) {
                for (config::yaw_mode base_yaw : yaws) {
                    for (config::pitch_mode base_pitch : pitches) {
                        config::anti_aim_cfg cfg{};
                        cfg.enabled = true;
                        cfg.at_targets = at_targets;
                        cfg.auto_adjust = auto_adjust;
                        cfg.freestanding = freestanding;
                        cfg.base_yaw = base_yaw;
                        cfg.base_pitch = base_pitch;
                        cfg.jitter_range = 30.0f;
                        cfg.yaw_offset = 15.0f;

                        features::antiaim_context ctx{};
                        ctx.on_ground = true;
                        ctx.found_enemy = true;
                        ctx.enemy_yaw = enemy_yaw;
                        ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
                        ctx.command_number = 100;
                        ctx.seed = 7;
                        // Behind cover: center_blocked = true, tucked = false.
                        ctx.freestanding.center_blocked = true;
                        ctx.freestanding.tucked = false;

                        const math::angles out0 = features::apply_antiaim_full(cfg, camera_view, ctx);

                        float expected_pitch = camera_view.pitch;
                        if (base_pitch == config::pitch_mode::down) expected_pitch = 89.0f;
                        else if (base_pitch == config::pitch_mode::up) expected_pitch = -89.0f;
                        else if (base_pitch == config::pitch_mode::zero) expected_pitch = 0.0f;
                        EXPECT_FLOAT_EQ(out0.pitch, expected_pitch);

                        const float ref_yaw = at_targets ? enemy_yaw : camera_view.yaw;
                        const float trim = auto_adjust
                            ? features::pose_head_yaw_offset(ctx.weapon_group, ctx.weapon_type, expected_pitch)
                            : 0.0f;

                        if (base_yaw == config::yaw_mode::off) {
                            const float exp_yaw = math::normalize_yaw(ref_yaw + cfg.yaw_offset);
                            EXPECT_NEAR(math::yaw_delta(exp_yaw, out0.yaw), 0.0f, 1e-4f);
                        } else if (base_yaw == config::yaw_mode::fake_static) {
                            const float exp_yaw = math::normalize_yaw(ref_yaw + 180.0f + trim + cfg.yaw_offset);
                            EXPECT_NEAR(math::yaw_delta(exp_yaw, out0.yaw), 0.0f, 1e-4f);
                        } else if (base_yaw == config::yaw_mode::jitter) {
                            ctx.command_number = 101;
                            const math::angles out1 = features::apply_antiaim_full(cfg, camera_view, ctx);
                            const float exp0 = math::normalize_yaw(ref_yaw + 180.0f - cfg.jitter_range + trim + cfg.yaw_offset);
                            const float exp1 = math::normalize_yaw(ref_yaw + 180.0f + cfg.jitter_range + trim + cfg.yaw_offset);
                            EXPECT_NEAR(math::yaw_delta(exp0, out0.yaw), 0.0f, 1e-4f);
                            EXPECT_NEAR(math::yaw_delta(exp1, out1.yaw), 0.0f, 1e-4f);
                        } else {
                            const math::angles out_replay = features::apply_antiaim_full(cfg, camera_view, ctx);
                            EXPECT_FLOAT_EQ(out0.yaw, out_replay.yaw);
                            const float center = math::normalize_yaw(ref_yaw + 180.0f + trim + cfg.yaw_offset);
                            EXPECT_LE(std::fabs(math::yaw_delta(center, out0.yaw)), cfg.jitter_range + 1e-4f);
                        }
                    }
                }
            }
        }
    }

    // Verify gates & shooting steadiness.
    config::anti_aim_cfg gate_cfg{};
    gate_cfg.enabled = true;
    gate_cfg.base_pitch = config::pitch_mode::down;
    gate_cfg.base_yaw = config::yaw_mode::fake_static;
    gate_cfg.on_air = false;
    gate_cfg.on_move = false;
    features::antiaim_context gate_ctx{};
    gate_ctx.suppressed = true;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(gate_cfg, camera_view, gate_ctx).yaw, camera_view.yaw);
    gate_ctx.suppressed = false;
    gate_ctx.on_ground = false;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(gate_cfg, camera_view, gate_ctx).yaw, camera_view.yaw);
    gate_ctx.on_ground = true;
    gate_ctx.has_move_input = true;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(gate_cfg, camera_view, gate_ctx).yaw, camera_view.yaw);
    gate_ctx.has_move_input = false;
    gate_ctx.speed_2d = 12.0f;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(gate_cfg, camera_view, gate_ctx).yaw, camera_view.yaw);
    gate_ctx.speed_2d = 0.0f;
    gate_ctx.shooting = true;
    const float shot_y0 = features::apply_antiaim_full(gate_cfg, camera_view, gate_ctx).yaw;
    gate_ctx.shooting = false;
    const float idle_y0 = features::apply_antiaim_full(gate_cfg, camera_view, gate_ctx).yaw;
    EXPECT_FLOAT_EQ(shot_y0, idle_y0);

    // Disabled/suppressed/gated passthrough clamps pitch to [-89, 89] and normalizes yaw to (-180, 180].
    const math::angles extreme_cam{120.0f, 1e9f};
    const math::angles expected_cam{89.0f, -80.0f};
    gate_cfg.enabled = false;
    gate_ctx = {};
    for (const math::angles out : {
             features::apply_antiaim_full(gate_cfg, extreme_cam, gate_ctx),
             [&] {
                 gate_cfg.enabled = true;
                 gate_ctx.suppressed = true;
                 return features::apply_antiaim_full(gate_cfg, extreme_cam, gate_ctx);
             }(),
             [&] {
                 gate_ctx.suppressed = false;
                 gate_ctx.on_ground = false;
                 return features::apply_antiaim_full(gate_cfg, extreme_cam, gate_ctx);
             }(),
             [&] {
                 gate_ctx.on_ground = true;
                 gate_ctx.has_move_input = true;
                 return features::apply_antiaim_full(gate_cfg, extreme_cam, gate_ctx);
             }(),
         }) {
        EXPECT_FLOAT_EQ(out.pitch, expected_cam.pitch);
        EXPECT_FLOAT_EQ(out.yaw, expected_cam.yaw);
    }
}

TEST(antiaim, base_yaw_off_with_at_targets_never_flips_180) {
    config::anti_aim_cfg cfg{};
    cfg.enabled = true;
    cfg.at_targets = true;
    cfg.auto_adjust = true;
    cfg.base_yaw = config::yaw_mode::off;
    cfg.base_pitch = config::pitch_mode::down;
    cfg.yaw_offset = 0.0f;

    const math::angles camera_view{5.0f, -30.0f};
    features::antiaim_context ctx{};
    ctx.found_enemy = true;
    ctx.enemy_yaw = 45.0f;
    const math::angles with_enemy = features::apply_antiaim_full(cfg, camera_view, ctx);
    EXPECT_FLOAT_EQ(with_enemy.pitch, 89.0f);
    EXPECT_NEAR(math::yaw_delta(45.0f, with_enemy.yaw), 0.0f, 0.01f);

    ctx.found_enemy = false;
    const math::angles no_enemy = features::apply_antiaim_full(cfg, camera_view, ctx);
    EXPECT_NEAR(math::yaw_delta(-30.0f, no_enemy.yaw), 0.0f, 0.01f);
}

TEST(antiaim, jitter_and_random_deterministic_by_command_number_with_180_and_auto_adjust) {
    config::anti_aim_cfg cfg{};
    cfg.enabled = true;
    cfg.at_targets = false;
    cfg.auto_adjust = true;
    cfg.freestanding = true;
    cfg.base_pitch = config::pitch_mode::down;
    cfg.base_yaw = config::yaw_mode::jitter;
    cfg.jitter_range = 30.0f;

    const math::angles camera_view{0.0f, 170.0f};
    features::antiaim_context ctx{};
    ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
    ctx.command_number = 500;
    ctx.seed = 3;
    ctx.found_enemy = true;
    ctx.enemy_yaw = -20.0f;
    ctx.freestanding.center_blocked = true;
    ctx.freestanding.tucked = false;

    const float trim = features::pose_head_yaw_offset(ctx.weapon_group, -1, 89.0f);
    EXPECT_FLOAT_EQ(trim, 36.0f);

    const float y500_a = features::apply_antiaim_full(cfg, camera_view, ctx).yaw;
    const float y500_b = features::apply_antiaim_full(cfg, camera_view, ctx).yaw;
    EXPECT_FLOAT_EQ(y500_a, y500_b);  // 250 Hz sub-frame idempotency

    ctx.command_number = 501;
    const float y501 = features::apply_antiaim_full(cfg, camera_view, ctx).yaw;
    const float center = math::normalize_yaw(camera_view.yaw + 180.0f + trim);
    EXPECT_NEAR(math::yaw_delta(center, y500_a), -30.0f, 1e-4f);
    EXPECT_NEAR(math::yaw_delta(center, y501), 30.0f, 1e-4f);

    cfg.base_yaw = config::yaw_mode::random;
    float min_d = 180.0f;
    float max_d = -180.0f;
    for (int cmd = 500; cmd < 532; ++cmd) {
        ctx.command_number = cmd;
        const float y = features::apply_antiaim_full(cfg, camera_view, ctx).yaw;
        const float d = math::yaw_delta(center, y);
        EXPECT_LE(std::fabs(d), 30.0f + 1e-4f);
        min_d = std::min(min_d, d);
        max_d = std::max(max_d, d);
    }
    EXPECT_GT(max_d - min_d, 10.0f);  // spread preserved even with freestanding behind cover
}

TEST(antiaim, freestanding_selector_is_symmetric_hysteretic_and_non_interfering) {
    // 1. Behind cover (center_blocked): tucked = false, state reset, never forces at_targets.
    features::freestanding_state fstate{1, 115.0f};
    const std::array<features::freestanding_candidate, 2> cover_cands = {{
        {115.0f, 0.25f, true},
        {-115.0f, 0.25f, true},
    }};
    const auto behind_cover = features::select_freestanding_yaw(-70.0f, true, 0.40f, cover_cands, fstate);
    EXPECT_TRUE(behind_cover.center_blocked);
    EXPECT_FALSE(behind_cover.tucked);
    EXPECT_EQ(fstate.last_side, 0);

    // Non-interfering with at_targets == false and jitter mode.
    config::anti_aim_cfg cfg{};
    cfg.enabled = true;
    cfg.at_targets = false;
    cfg.freestanding = true;
    cfg.base_yaw = config::yaw_mode::jitter;
    cfg.jitter_range = 25.0f;
    cfg.auto_adjust = false;
    features::antiaim_context ctx{};
    ctx.found_enemy = true;
    ctx.enemy_yaw = -70.0f;
    ctx.freestanding = behind_cover;
    ctx.command_number = 10;
    const float y_even = features::apply_antiaim_full(cfg, {0.0f, 20.0f}, ctx).yaw;
    ctx.command_number = 11;
    const float y_odd = features::apply_antiaim_full(cfg, {0.0f, 20.0f}, ctx).yaw;
    EXPECT_NEAR(math::yaw_delta(math::normalize_yaw(20.0f + 180.0f - 25.0f), y_even), 0.0f, 1e-4f);
    EXPECT_NEAR(math::yaw_delta(math::normalize_yaw(20.0f + 180.0f + 25.0f), y_odd), 0.0f, 1e-4f);

    // 2. Open field: all frac = 1.0f -> not tucked.
    const std::array<features::freestanding_candidate, 2> open_cands = {{
        {115.0f, 1.0f, true},
        {-115.0f, 1.0f, true},
    }};
    const auto open = features::select_freestanding_yaw(0.0f, true, 1.0f, open_cands, fstate);
    EXPECT_FALSE(open.center_blocked);
    EXPECT_FALSE(open.tucked);

    // 3. Symmetric min-frac beats +115 order bias.
    const std::array<features::freestanding_candidate, 4> deep_neg_cands = {{
        {115.0f, 0.85f, true},
        {-115.0f, 0.20f, true},
        {135.0f, 0.90f, true},
        {-135.0f, 0.30f, true},
    }};
    const auto deep_neg = features::select_freestanding_yaw(10.0f, true, 1.0f, deep_neg_cands, fstate);
    EXPECT_TRUE(deep_neg.tucked);
    EXPECT_FLOAT_EQ(deep_neg.selected_offset, -115.0f);
    EXPECT_EQ(deep_neg.selected_side, -1);
    EXPECT_NEAR(math::yaw_delta(-105.0f, deep_neg.selected_yaw), 0.0f, 1e-4f);

    // 4. Hysteresis retains +1 across 0.02 diff (< 0.08) and < 0.98 hold threshold, switches on 0.25 diff.
    fstate = {1, 115.0f};
    const std::array<features::freestanding_candidate, 2> small_diff = {{
        {115.0f, 0.35f, true},
        {-115.0f, 0.33f, true},
    }};
    const auto held = features::select_freestanding_yaw(0.0f, true, 1.0f, small_diff, fstate);
    EXPECT_TRUE(held.tucked);
    EXPECT_EQ(held.selected_side, 1);

    const std::array<features::freestanding_candidate, 2> edge_hold = {{
        {115.0f, 0.97f, true},
        {-115.0f, 0.95f, true},
    }};
    const auto held_edge = features::select_freestanding_yaw(0.0f, true, 1.0f, edge_hold, fstate);
    EXPECT_TRUE(held_edge.tucked);
    EXPECT_EQ(held_edge.selected_side, 1);

    const std::array<features::freestanding_candidate, 2> big_diff = {{
        {115.0f, 0.35f, true},
        {-115.0f, 0.10f, true},
    }};
    const auto switched = features::select_freestanding_yaw(0.0f, true, 1.0f, big_diff, fstate);
    EXPECT_TRUE(switched.tucked);
    EXPECT_EQ(switched.selected_side, -1);
    EXPECT_FLOAT_EQ(switched.selected_offset, -115.0f);

    // Invalid / non-finite center and enemy yaw safety.
    fstate = {1, 115.0f};
    const auto bad_yaw = features::select_freestanding_yaw(
        std::numeric_limits<float>::quiet_NaN(), true, 1.0f, big_diff, fstate);
    EXPECT_FALSE(bad_yaw.tucked);
    EXPECT_EQ(fstate.last_side, 0);

    fstate = {1, 115.0f};
    const auto invalid_center = features::select_freestanding_yaw(0.0f, false, 1.0f, big_diff, fstate);
    EXPECT_FALSE(invalid_center.center_blocked);
    EXPECT_FALSE(invalid_center.tucked);
    EXPECT_EQ(fstate.last_side, 0);

    // 5. Pitch-aware probe offset stays inside 16u hull and scales Z with pitch.
    const math::vec3 eye{100.0f, 200.0f, 64.0f};
    const math::vec3 p_down = features::freestanding_head_probe(eye, 0.0f, 89.0f);
    const math::vec3 p_zero = features::freestanding_head_probe(eye, 0.0f, 0.0f);
    const math::vec3 p_up = features::freestanding_head_probe(eye, 0.0f, -89.0f);
    EXPECT_LT(std::hypot(p_down.x - eye.x, p_down.y - eye.y), 16.0f);
    EXPECT_LT(std::hypot(p_zero.x - eye.x, p_zero.y - eye.y), 16.0f);
    EXPECT_LT(std::hypot(p_up.x - eye.x, p_up.y - eye.y), 16.0f);
    EXPECT_GT(p_down.x - eye.x, 0.0f);
    EXPECT_GT(p_up.x - eye.x, 0.0f);
    EXPECT_LT(p_down.z - eye.z, -5.0f);
    EXPECT_NEAR(p_zero.z - eye.z, 0.0f, 1e-4f);
    EXPECT_GT(p_up.z - eye.z, -3.0f);

    // 6. Enemy-hull-safe trace target backs off 16u along probe vector (and avoids float overflow).
    const math::vec3 enemy_eye{300.0f, 200.0f, 64.0f};
    const math::vec3 target = features::freestanding_trace_target(enemy_eye, p_down, 16.0f);
    EXPECT_NEAR((target - enemy_eye).length(), 16.0f, 0.01f);

    const math::vec3 extreme_target = features::freestanding_trace_target({2e38f, 0.0f, 0.0f}, {-2e38f, 0.0f, 0.0f}, 16.0f);
    EXPECT_TRUE(extreme_target.finite());
    EXPECT_FLOAT_EQ(extreme_target.x, 2e38f);
    EXPECT_FLOAT_EQ(extreme_target.y, 0.0f);
    EXPECT_FLOAT_EQ(extreme_target.z, 0.0f);

    const math::vec3 scaled_target = features::freestanding_trace_target({1e38f, 0.0f, 0.0f}, {-1e38f, 0.0f, 0.0f}, 5e37f);
    EXPECT_TRUE(scaled_target.finite());
    EXPECT_NEAR(scaled_target.x, 5e37f, 1e32f);

    const math::vec3 large_target = features::freestanding_trace_target({0.0f, 0.0f, 0.0f}, {1e20f, 0.0f, 0.0f}, 16.0f);
    EXPECT_FLOAT_EQ(large_target.x, 16.0f);
    EXPECT_FLOAT_EQ(large_target.y, 0.0f);
    EXPECT_FLOAT_EQ(large_target.z, 0.0f);
}

TEST(antiaim, auto_adjust_is_pitch_sign_and_weapon_group_aware) {
    for (int g = 0; g < config::k_weapon_groups; ++g) {
        EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(g, -1, 0.0f), 0.0f);
        EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(g, -1, 20.0f), 0.0f);
        EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(g, -1, -20.0f), 0.0f);
        EXPECT_FLOAT_EQ(features::pose_head_yaw_offset(g, -1, -89.0f), 0.0f);
    }

    const float rifle_down = features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), -1, 89.0f);
    const float awp_down = features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::awp), -1, 89.0f);
    const float scout_down = features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::scout), -1, 89.0f);
    const float pistol_down = features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::pistol), -1, 89.0f);
    const float zeus_down = features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 8, 89.0f);
    const float knife_down = features::pose_head_yaw_offset(-1, 0, 89.0f);

    EXPECT_GE(rifle_down, 33.0f);
    EXPECT_LE(rifle_down, 40.0f);
    EXPECT_FLOAT_EQ(awp_down, rifle_down);
    EXPECT_FLOAT_EQ(scout_down, rifle_down);
    EXPECT_GT(pistol_down, 0.0f);
    EXPECT_LT(pistol_down, rifle_down);
    EXPECT_FLOAT_EQ(zeus_down, pistol_down);
    EXPECT_FLOAT_EQ(knife_down, 0.0f);
    EXPECT_NE(features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), -1, -89.0f), rifle_down);

    // Active across fake_static, jitter, and random; zeroed when auto_adjust == false.
    config::anti_aim_cfg cfg{};
    cfg.enabled = true;
    cfg.at_targets = false;
    cfg.base_pitch = config::pitch_mode::down;
    cfg.jitter_range = 20.0f;

    features::antiaim_context ctx{};
    ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
    ctx.command_number = 42;
    ctx.seed = 9;

    for (config::yaw_mode mode : {config::yaw_mode::fake_static, config::yaw_mode::jitter, config::yaw_mode::random}) {
        cfg.base_yaw = mode;
        cfg.auto_adjust = false;
        const float without_adj = features::apply_antiaim_full(cfg, {0.0f, 0.0f}, ctx).yaw;
        cfg.auto_adjust = true;
        const float with_adj = features::apply_antiaim_full(cfg, {0.0f, 0.0f}, ctx).yaw;
        EXPECT_NEAR(math::yaw_delta(without_adj, with_adj), rifle_down, 1e-4f);
    }
}

TEST(antiaim, quantized_ground_move_preserves_arbitrary_aa_yaw_and_tracks_world_dir) {
    // 1. Zero & non-finite wish handling.
    features::quantized_move_state st{};
    float fwd = 9.0f, left = 9.0f;
    features::compute_quantized_ground_move({0.0f, 0.0f, 0.0f}, 25.0f, st, 1, fwd, left);
    EXPECT_FLOAT_EQ(fwd, 0.0f);
    EXPECT_FLOAT_EQ(left, 0.0f);

    features::compute_quantized_ground_move(
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, 25.0f, st, 2, fwd, left);
    EXPECT_FLOAT_EQ(fwd, 0.0f);
    EXPECT_FLOAT_EQ(left, 0.0f);

    // 2. Idempotency per command_number & clean same-tick re-solve when cmd_yaw changes.
    st = {};
    features::compute_quantized_ground_move({1.0f, 0.0f, 0.0f}, 17.3f, st, 77, fwd, left);
    const float ref_f = fwd, ref_l = left;
    EXPECT_TRUE(ref_f == -1.0f || ref_f == 0.0f || ref_f == 1.0f);
    EXPECT_TRUE(ref_l == -1.0f || ref_l == 0.0f || ref_l == 1.0f);
    for (int i = 0; i < 3; ++i) {
        features::compute_quantized_ground_move({1.0f, 0.0f, 0.0f}, 17.3f, st, 77, fwd, left);
        EXPECT_FLOAT_EQ(fwd, ref_f);
        EXPECT_FLOAT_EQ(left, ref_l);
    }
    // Same command_number with rotated fire-tick yaw resolves in new yaw space without corrupting err.
    features::compute_quantized_ground_move({1.0f, 0.0f, 0.0f}, 0.0f, st, 77, fwd, left);
    EXPECT_FLOAT_EQ(fwd, 1.0f);
    EXPECT_FLOAT_EQ(left, 0.0f);

    // 3. 128-tick zero-drift tracking for arbitrary constant, jittering, and rotating cmd_yaws.
    constexpr float k_deg_to_rad = 3.14159265358979323846f / 180.0f;
    auto run_sequence = [&](auto yaw_at_tick, float wish_angle_deg) {
        features::quantized_move_state seq_st{};
        const math::vec3 wish{std::cos(wish_angle_deg * k_deg_to_rad),
                              std::sin(wish_angle_deg * k_deg_to_rad), 0.0f};
        double sum_x = 0.0, sum_y = 0.0;
        for (int cmd = 1; cmd <= 128; ++cmd) {
            const float cmd_yaw = yaw_at_tick(cmd);
            float f = 0.0f, l = 0.0f;
            features::compute_quantized_ground_move(wish, cmd_yaw, seq_st, cmd, f, l);
            EXPECT_TRUE(f == -1.0f || f == 0.0f || f == 1.0f);
            EXPECT_TRUE(l == -1.0f || l == 0.0f || l == 1.0f);
            EXPECT_LE(std::hypot(seq_st.err_x, seq_st.err_y), 0.75f + 1e-4f);

            const float r = cmd_yaw * k_deg_to_rad;
            const float wx = std::cos(r) * f - std::sin(r) * l;
            const float wy = std::sin(r) * f + std::cos(r) * l;
            const float wl = std::hypot(wx, wy);
            ASSERT_GT(wl, 0.5f);
            sum_x += wx / wl;
            sum_y += wy / wl;
        }
        const double total_len = std::hypot(sum_x, sum_y);
        const double fwd_proj = sum_x * wish.x + sum_y * wish.y;
        const double lat_err = std::fabs(-sum_x * wish.y + sum_y * wish.x);
        const double angle_err_deg =
            std::fabs(math::yaw_delta(wish_angle_deg, static_cast<float>(std::atan2(sum_y, sum_x) / k_deg_to_rad)));
        EXPECT_LT(angle_err_deg, 2.0);
        EXPECT_GT(fwd_proj, 0.90 * 128.0);
        EXPECT_GT(fwd_proj, 0.98 * total_len);
        EXPECT_LE(lat_err, 1.0);
    };

    for (float cy : {17.3f, 22.5f, -145.0f, -162.7f, 131.0f}) {
        run_sequence([cy](int) { return cy; }, 0.0f);
    }
    run_sequence([](int cmd) { return (cmd & 1) ? 150.0f : -150.0f; }, 0.0f);
    run_sequence([](int cmd) { return math::normalize_yaw(static_cast<float>(cmd) * 7.5f); }, 35.0f);

    // 4. Exact 45-deg multiples align immediately with zero residual drift.
    for (float mult : {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, -45.0f, -135.0f}) {
        features::quantized_move_state exact_st{};
        float f = 0.0f, l = 0.0f;
        features::compute_quantized_ground_move({1.0f, 0.0f, 0.0f}, mult, exact_st, 1, f, l);
        EXPECT_LE(std::hypot(exact_st.err_x, exact_st.err_y), 1e-4f);
    }

    // 5. Sharp reversal (W -> S, W -> D) and large tick gap (> 64) reset stale error cleanly.
    st = {};
    features::compute_quantized_ground_move({1.0f, 0.0f, 0.0f}, 17.3f, st, 1, fwd, left);
    EXPECT_GT(std::hypot(st.err_x, st.err_y), 0.05f);
    features::compute_quantized_ground_move({-1.0f, 0.0f, 0.0f}, 0.0f, st, 2, fwd, left);
    EXPECT_FLOAT_EQ(fwd, -1.0f);
    EXPECT_FLOAT_EQ(left, 0.0f);
    EXPECT_NEAR(std::hypot(st.err_x, st.err_y), 0.0f, 1e-4f);

    features::compute_quantized_ground_move({1.0f, 0.0f, 0.0f}, 17.3f, st, 10, fwd, left);
    EXPECT_GT(std::hypot(st.err_x, st.err_y), 0.05f);
    features::compute_quantized_ground_move({1.0f, 0.0f, 0.0f}, 0.0f, st, 100, fwd, left);
    EXPECT_FLOAT_EQ(fwd, 1.0f);
    EXPECT_FLOAT_EQ(left, 0.0f);
    EXPECT_NEAR(std::hypot(st.err_x, st.err_y), 0.0f, 1e-4f);

    // 6. Extreme finite diagonal wish vector avoids float hypot overflow.
    st = {};
    features::compute_quantized_ground_move({3e38f, 3e38f, 0.0f}, 0.0f, st, 1, fwd, left);
    EXPECT_FLOAT_EQ(fwd, 1.0f);
    EXPECT_FLOAT_EQ(left, 1.0f);
}

TEST(target, bhop_latches_landing_jump_across_subframe_calls) {
    features::bhop_state bs{};
    // Airborne across 3 render frames on cmd 100 -> always released.
    EXPECT_FALSE(features::step_bhop(bs, 100, false, true));
    EXPECT_FALSE(features::step_bhop(bs, 100, false, true));
    EXPECT_FALSE(features::step_bhop(bs, 100, false, true));

    // Landing on cmd 101, called 3 times within the same tick -> MUST stay true all 3 calls.
    EXPECT_TRUE(features::step_bhop(bs, 101, true, true));
    EXPECT_TRUE(features::step_bhop(bs, 101, true, true));
    EXPECT_TRUE(features::step_bhop(bs, 101, true, true));

    // If still on ground on cmd 102, release cleanly for that tick (re-arm), then press on 103.
    EXPECT_FALSE(features::step_bhop(bs, 102, true, true));
    EXPECT_FALSE(features::step_bhop(bs, 102, true, true));
    EXPECT_TRUE(features::step_bhop(bs, 103, true, true));
}

TEST(esp, computes_valid_player_box_and_rejects_behind) {
    math::mat4 vm{};
    vm.at(0, 0) = 1.0f;
    vm.at(1, 2) = 1.0f;
    vm.at(2, 1) = 1.0f;
    vm.at(3, 1) = 1.0f;  // w = world.y

    sdk::player_snapshot p{};
    p.alive = true;
    p.origin = {0.0f, 200.0f, -36.0f};
    p.eye = {0.0f, 200.0f, 28.0f};

    const auto box = features::esp::compute_player_box(p, vm, 1920, 1080);
    ASSERT_TRUE(box.valid);
    EXPECT_GT(box.h, 100.0f);
    EXPECT_NEAR(box.w, box.h * 0.52f, 0.01f);

    p.origin = {0.0f, -50.0f, 0.0f};
    p.eye = {0.0f, -50.0f, 64.0f};
    EXPECT_FALSE(features::esp::compute_player_box(p, vm, 1920, 1080).valid);
}

TEST(esp, collision_bounds_reflect_the_players_stance) {
    sdk::player_snapshot p{};
    p.alive = true;
    p.bounds_valid = true;
    p.bounds_min = {-16.0f, -16.0f, 0.0f};
    p.bounds_max = {16.0f, 16.0f, 72.0f};
    math::mat4 matrix{};
    matrix.at(0, 0) = 0.01f;
    matrix.at(1, 2) = 0.01f;
    matrix.at(3, 3) = 1.0f;
    const auto standing = features::esp::compute_player_box(p, matrix, 800, 600);
    p.bounds_max.z = 54.0f;
    const auto crouched = features::esp::compute_player_box(p, matrix, 800, 600);
    EXPECT_TRUE(standing.valid);
    EXPECT_TRUE(crouched.valid);
    EXPECT_LT(crouched.h, standing.h);
    EXPECT_FLOAT_EQ(crouched.w, standing.w);
}

TEST(esp, draws_corner_fallback_when_a_3d_corner_is_behind_camera) {
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();

    config::config_v1 cfg{};
    cfg.misc.info.watermark = false;
    cfg.misc.info.keybind_list = false;
    cfg.visuals = {};
    cfg.visuals.box = config::box_style::box_3d;
    cfg.visuals.health_bar = false;
    cfg.visuals.armor_bar = false;
    cfg.visuals.name = false;
    cfg.visuals.weapon = false;
    cfg.visuals.distance = false;
    cfg.visuals.flags = false;

    sdk::frame_snapshot frame{};
    frame.in_game = true;
    frame.count = 1;
    frame.players[0].pawn = 1;
    frame.players[0].alive = true;
    frame.players[0].enemy = true;
    frame.players[0].health = 100;
    frame.view_matrix.at(1, 2) = 0.01f;
    frame.view_matrix.at(3, 0) = 1.0f;
    frame.view_matrix.at(3, 3) = 1.0f;

    const features::backtrack_history history{};
    const auto stats = features::esp::draw(cfg, &frame, history, {}, 800, 600);
    EXPECT_EQ(stats.enemies, 1);
    EXPECT_EQ(stats.projected, 1);
    EXPECT_EQ(stats.boxes, 1);
    EXPECT_GT(ImGui::GetBackgroundDrawList()->VtxBuffer.Size, 0);

    cfg.visuals.box = config::box_style::off;
    cfg.visuals.health_bar = true;
    cfg.visuals.armor_bar = true;
    cfg.visuals.weapon_icon = true;
    cfg.visuals.ammo = true;
    frame.players[0].armor = 50;
    frame.players[0].weapon_id = 7;
    frame.players[0].weapon_family = static_cast<int>(config::weapon_group::rifle);
    frame.players[0].ammo = 23;
    const auto elements = features::esp::draw(cfg, &frame, history, {}, 800, 600);
    EXPECT_EQ(elements.boxes, 0);
    EXPECT_EQ(elements.health_bars, 1);
    EXPECT_EQ(elements.armor_bars, 1);
    EXPECT_EQ(elements.weapon_icons, 1);
    EXPECT_EQ(elements.weapon_names, 0);
    EXPECT_EQ(elements.ammo_labels, 1);

    ImGui::EndFrame();
    ImGui::DestroyContext(context);
}

TEST(esp, keybinds_hud_renders_anti_aim_and_respects_safe_mode) {
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.0f, 600.0f);
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    auto render_keybinds_vtx = [&](const config::config_v1& cfg, const ui::status& live) {
        ImGui::NewFrame();
        const features::backtrack_history history{};
        features::esp::draw(cfg, nullptr, history, live, 800, 600);
        const int vtx = ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
        ImGui::EndFrame();
        return vtx;
    };

    config::config_v1 cfg{};
    cfg.misc.info.watermark = false;
    cfg.misc.info.keybind_list = true;
    cfg.misc.block_server_detected = false;
    cfg.rage.enabled = false;
    cfg.visuals.enabled = false;
    cfg.visuals.third_person = false;
    cfg.visuals.backtrack_ghost = false;
    cfg.misc.movement.bhop = false;
    cfg.misc.movement.auto_strafe = false;
    cfg.misc.movement.peek_assist = false;
    cfg.misc.utilities.auto_pistol = false;

    // Case 1: unsampled live binds, anti_aim.enabled = true -> Keybinds panel drawn.
    cfg.anti_aim.enabled = true;
    ui::status live{};
    live.binds_sampled = false;
    EXPECT_GT(render_keybinds_vtx(cfg, live), 0);

    // Case 2: sampled live binds with bound_feature::anti_aim -> Keybinds panel drawn.
    cfg.anti_aim.enabled = false;
    live.binds_sampled = true;
    live.active_binds = config::feature_bit(config::bound_feature::anti_aim);
    const int aa_only_vtx = render_keybinds_vtx(cfg, live);
    EXPECT_GT(aa_only_vtx, 0);

    // Case 3: sampled live binds with no active binds -> panel hidden.
    live.active_binds = 0u;
    EXPECT_EQ(render_keybinds_vtx(cfg, live), 0);

    // Case 4: safe mode renders "Safe mode" while suppressing "Anti-Aim".
    cfg.misc.block_server_detected = true;
    live.active_binds = 0u;
    const int safe_only_vtx = render_keybinds_vtx(cfg, live);
    EXPECT_GT(safe_only_vtx, 0);

    live.active_binds = config::feature_bit(config::bound_feature::anti_aim);
    EXPECT_EQ(render_keybinds_vtx(cfg, live), safe_only_vtx);

    live.active_binds = config::feature_bit(config::bound_feature::anti_aim) |
                        config::feature_bit(config::bound_feature::rage);
    EXPECT_GT(render_keybinds_vtx(cfg, live), safe_only_vtx);

    ImGui::DestroyContext(context);
}

TEST(hitbox_geom, builds_capsule_generates_multipoints_and_intersects_rays) {
    // Identity 2x4 bone transform at world (100, 0, 64), unit quaternion (0, 0, 0, 1).
    const float raw[8] = {100.0f, 0.0f, 64.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const math::mat3x4 tm = math::matrix_from_2x4(raw);

    // Hitgroup 1 = head (group 0), capsule extending along Z.
    const auto head = math::build_hitbox_geometry(
        {0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f}, 4.5f,
        static_cast<int>(math::hitbox_shape::capsule), 1, false, tm, 0.85f);
    EXPECT_TRUE(head.valid);
    EXPECT_EQ(head.group, 0);
    EXPECT_NEAR(head.center.x, 100.0f, 0.01f);
    EXPECT_NEAR(head.center.z, 64.0f, 0.01f);

    std::array<math::vec3, 9> pts{};
    const std::size_t n = math::generate_hitbox_points(head, {0.0f, 0.0f, 64.0f}, 0.8f, pts);
    EXPECT_GE(n, 5u);
    EXPECT_NEAR(pts[0].x, head.center.x, 0.01f);

    const math::vec3 hit_dir = (head.center - math::vec3{0.0f, 0.0f, 64.0f}).normalised();
    const math::vec3 miss_dir = (math::vec3{100.0f, 25.0f, 64.0f} - math::vec3{0.0f, 0.0f, 64.0f}).normalised();
    EXPECT_TRUE(math::ray_hits_geometry({0.0f, 0.0f, 64.0f}, hit_dir, head));
    EXPECT_FALSE(math::ray_hits_geometry({0.0f, 0.0f, 64.0f}, miss_dir, head));
}

TEST(hitbox_geom, multi_hitchance_filters_by_group_and_spread) {
    const float raw_head[8] = {300.0f, 0.0f, 64.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float raw_chest[8] = {300.0f, 0.0f, 44.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const auto head = math::build_hitbox_geometry(
        {0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f}, 4.5f,
        static_cast<int>(math::hitbox_shape::capsule), 1, false, math::matrix_from_2x4(raw_head), 1.0f);
    const auto chest = math::build_hitbox_geometry(
        {0.0f, 0.0f, -6.0f}, {0.0f, 0.0f, 6.0f}, 8.0f,
        static_cast<int>(math::hitbox_shape::capsule), 2, false, math::matrix_from_2x4(raw_chest), 1.0f);

    const std::array<math::hitbox_geometry, 2> boxes{head, chest};
    const math::weapon_spread_pattern tight(0.001f, 0.001f);
    const math::weapon_spread_pattern loose(0.03f, 0.05f);

    const float tight_head = tight.chance_multi({0.0f, 0.0f, 64.0f}, head.center, boxes, 0);
    const float loose_head = loose.chance_multi({0.0f, 0.0f, 64.0f}, head.center, boxes, 0);
    const float loose_any = loose.chance_multi({0.0f, 0.0f, 52.0f}, chest.center, boxes, -1);

    EXPECT_GT(tight_head, 0.95f);
    EXPECT_LT(loose_head, tight_head);
    EXPECT_GT(loose_any, loose_head);
}

TEST(config, roundtrips_adapted_rage_chams_world_and_misc_fields) {
    config::store s;
    auto& c = s.get();
    c.rage.silent_aim = false;
    c.rage.remove_recoil = false;
    c.rage.auto_revolver = false;
    c.rage.weapons[0].force_body_aim = true;
    c.rage.weapons[0].prefer_body_aim = true;
    c.rage.weapons[0].auto_stop_flags = config::auto_stop_flag::all;
    c.rage.weapons[0].strict_hitchance = true;
    c.anti_aim.base_pitch = config::pitch_mode::up;
    c.visuals.team_chams.enabled = true;
    c.visuals.team_chams.material = 2;
    c.visuals.team_chams.fresnel_exponent = 3.5f;
    c.visuals.local_chams.enabled = true;
    c.visuals.arms_chams.enabled = true;
    c.visuals.weapon_chams.enabled = true;
    c.visuals.scope_blend = true;
    c.visuals.scope_blend_alpha = 90;
    c.visuals.remove_view_punch = true;
    c.visuals.penetration_crosshair = true;
    c.visuals.world_modulate = true;
    c.visuals.light_modulate = true;
    c.visuals.exposure = 65.0f;
    c.misc.movement.peek_assist = true;
    c.misc.movement.leg_movement = true;

    const std::string json = s.to_json();
    config::store s2;
    ASSERT_TRUE(s2.from_json(json));
    const auto& r = s2.get();
    EXPECT_FALSE(r.rage.silent_aim);
    EXPECT_FALSE(r.rage.remove_recoil);
    EXPECT_FALSE(r.rage.auto_revolver);
    EXPECT_TRUE(r.rage.weapons[0].force_body_aim);
    EXPECT_TRUE(r.rage.weapons[0].prefer_body_aim);
    EXPECT_EQ(r.rage.weapons[0].auto_stop_flags, config::auto_stop_flag::all);
    EXPECT_TRUE(r.rage.weapons[0].strict_hitchance);
    EXPECT_EQ(r.anti_aim.base_pitch, config::pitch_mode::up);
    EXPECT_TRUE(r.visuals.team_chams.enabled);
    EXPECT_EQ(r.visuals.team_chams.material, 2);
    EXPECT_FLOAT_EQ(r.visuals.team_chams.fresnel_exponent, 3.5f);
    EXPECT_TRUE(r.visuals.local_chams.enabled);
    EXPECT_TRUE(r.visuals.arms_chams.enabled);
    EXPECT_TRUE(r.visuals.weapon_chams.enabled);
    EXPECT_TRUE(r.visuals.scope_blend);
    EXPECT_EQ(r.visuals.scope_blend_alpha, 90);
    EXPECT_TRUE(r.visuals.remove_view_punch);
    EXPECT_TRUE(r.visuals.penetration_crosshair);
    EXPECT_TRUE(r.visuals.world_modulate);
    EXPECT_TRUE(r.visuals.light_modulate);
    EXPECT_FLOAT_EQ(r.visuals.exposure, 65.0f);
    EXPECT_TRUE(r.misc.movement.peek_assist);
    EXPECT_TRUE(r.misc.movement.leg_movement);
}

TEST(target, autostop_modes_and_deadzone_behave_cleanly) {
    features::autostop_input in{};
    in.enabled = true;
    in.has_velocity = true;
    in.velocity = {200.0f, 50.0f, 0.0f};
    in.speed_2d = std::hypot(in.velocity.x, in.velocity.y);
    in.cmd_yaw = 0.0f;
    in.has_target = true;

    // Without Early flag, non-ready weapon does not brake.
    in.flags = 0;
    in.weapon_ready = false;
    in.within_early_window = true;
    EXPECT_FALSE(features::compute_autostop(in).active);

    // Early flag engages pre-brake before weapon_ready.
    in.flags = config::auto_stop_flag::early;
    const auto early = features::compute_autostop(in);
    EXPECT_TRUE(early.active);
    EXPECT_FALSE(early.zero_move);
    EXPECT_FLOAT_EQ(early.forwardmove, -1.0f);  // unit-square normalized opposing +X

    // Air gating: disabled unless auto_stop_flag::in_air is set.
    in.on_ground = false;
    in.weapon_ready = true;
    EXPECT_FALSE(features::compute_autostop(in).active);
    in.flags |= config::auto_stop_flag::in_air;
    EXPECT_TRUE(features::compute_autostop(in).active);

    // Between shots holds brake during recovery while target remains active.
    in.on_ground = true;
    in.weapon_ready = false;
    in.within_early_window = false;
    in.between_shots_active = true;
    in.flags = config::auto_stop_flag::between_shots;
    EXPECT_TRUE(features::compute_autostop(in).active);

    // Deadzone (<= 14 u/s on ground) zeroes inputs instead of reversing velocity.
    in.velocity = {8.0f, 0.0f, 0.0f};
    in.speed_2d = 8.0f;
    const auto dead = features::compute_autostop(in);
    EXPECT_TRUE(dead.active);
    EXPECT_TRUE(dead.zero_move);
    EXPECT_FLOAT_EQ(dead.forwardmove, 0.0f);
    EXPECT_FLOAT_EQ(dead.leftmove, 0.0f);
}

TEST(hitbox_geom, head_multipoint_includes_world_up_crown) {
    const float raw_head[8] = {100.0f, 0.0f, 64.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const auto head = math::build_hitbox_geometry(
        {0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f}, 4.5f,
        static_cast<int>(math::hitbox_shape::capsule), 1, false, math::matrix_from_2x4(raw_head), 1.0f);
    std::array<math::vec3, 9> pts{};
    const std::size_t n = math::generate_hitbox_points(head, {0.0f, 0.0f, 90.0f}, 1.0f, pts);
    EXPECT_GE(n, 7u);
    bool has_crown = false;
    for (std::size_t i = 1; i < n; ++i) {
        if (pts[i].z > head.center.z + 3.5f) has_crown = true;
    }
    EXPECT_TRUE(has_crown);
}

TEST(target, autostrafe_steers_with_unit_square_and_hysteresis) {
    features::autostrafe_input idle{};
    idle.camera_yaw = 0.0f;
    idle.velocity = {0.0f, 0.0f, 0.0f};
    EXPECT_FALSE(features::compute_autostrafe(idle).active);

    features::autostrafe_input in{};
    in.camera_yaw = 0.0f;
    in.velocity = {260.0f, 0.0f, 0.0f};
    in.forward_wish = 1.0f;
    in.strafe_sign = 1;
    const auto straight = features::compute_autostrafe(in);
    EXPECT_TRUE(straight.active);
    EXPECT_EQ(straight.next_strafe_sign, 1);
    EXPECT_FLOAT_EQ(std::max(std::fabs(straight.forwardmove), std::fabs(straight.leftmove)), 1.0f);

    // Slight overshoot to +1.0 deg still holds sign=+1 (no 1-tick zigzag flip);
    // only crossing -2.0 deg swaps to -1 cleanly.
    in.velocity = {260.0f, 4.0f, 0.0f};  // vel_yaw ~= +0.88 deg => delta ~= -0.88 deg
    EXPECT_EQ(features::compute_autostrafe(in).next_strafe_sign, 1);
    in.velocity = {260.0f, 12.0f, 0.0f};  // vel_yaw ~= +2.64 deg => delta ~= -2.64 deg
    EXPECT_EQ(features::compute_autostrafe(in).next_strafe_sign, -1);

    // Turning left with A/W+A pulls sign positive smoothly.
    in.velocity = {260.0f, 0.0f, 0.0f};
    in.forward_wish = 0.0f;
    in.left_wish = 1.0f;
    const auto left_turn = features::compute_autostrafe(in);
    EXPECT_EQ(left_turn.next_strafe_sign, 1);
    EXPECT_FLOAT_EQ(std::max(std::fabs(left_turn.forwardmove), std::fabs(left_turn.leftmove)), 1.0f);
}

TEST(target, quantized_autostrafe_accelerates_without_spinning_viewangles) {
    // Simulate 45 ticks (1 jump) with smooth mouse turning and ZERO viewangle spin (AA off and AA 180).
    for (float aa_offset : {0.0f, 180.0f}) {
        math::vec3 vel{250.0f, 0.0f, 0.0f};
        float cam = 0.0f;
        for (int t = 0; t < 45; ++t) {
            cam = math::normalize_yaw(cam + 2.5f);
            const float cmd_yaw = math::normalize_yaw(cam + aa_offset);
            const auto out = features::compute_quantized_autostrafe(cam, cmd_yaw, vel, 0.0f, 0.0f);
            ASSERT_TRUE(out.active);
            // Outputs must be exact SetupMove integers in {-1, 0, +1}.
            EXPECT_FLOAT_EQ(out.forwardmove, std::round(out.forwardmove));
            EXPECT_FLOAT_EQ(out.leftmove, std::round(out.leftmove));

            const float r = cmd_yaw * (3.14159265f / 180.0f);
            const float fx = std::cos(r), fy = std::sin(r);
            const float rx = fy, ry = -fx;
            float wx = fx * out.forwardmove - rx * out.leftmove;
            float wy = fy * out.forwardmove - ry * out.leftmove;
            const float wl = std::hypot(wx, wy);
            ASSERT_GT(wl, 0.5f);
            wx /= wl;
            wy /= wl;
            const float add = 30.0f - (vel.x * wx + vel.y * wy);
            if (add > 0.0f) {
                const float acc = std::min(add, 12.0f * 250.0f / 64.0f);
                vel.x += acc * wx;
                vel.y += acc * wy;
            }
        }
        EXPECT_GT(std::hypot(vel.x, vel.y), 280.0f);
    }
}

namespace {

math::vec3 step_air_physics_tick(math::vec3 vel, float cmd_yaw_deg, float fwd, float left) noexcept {
    const float qf = std::clamp(std::round(fwd), -1.0f, 1.0f);
    const float ql = std::clamp(std::round(left), -1.0f, 1.0f);
    const float rad = math::normalize_yaw(cmd_yaw_deg) * (3.14159265f / 180.0f);
    const float fx = std::cos(rad);
    const float fy = std::sin(rad);
    const float rx = fy;
    const float ry = -fx;
    float wx = fx * qf - rx * ql;
    float wy = fy * qf - ry * ql;
    const float len = std::hypot(wx, wy);
    if (len > 1e-4f) {
        wx /= len;
        wy /= len;
        const float cur = vel.x * wx + vel.y * wy;
        const float add = 30.0f - cur;
        if (add > 0.0f) {
            const float accel = std::min(add, 12.0f * 250.0f / 64.0f);
            vel.x += accel * wx;
            vel.y += accel * wy;
        }
    }
    return vel;
}

}  // namespace

TEST(target, sector_matched_autostrafe_accelerates_without_spin_for_all_inputs) {
    // Straight W under fake_static AA (180 deg) must stay within <= 22.5 deg and exceed 300 u/s in 32 ticks.
    math::vec3 vel{250.0f, 0.0f, 0.0f};
    int sign = 1;
    for (int t = 0; t < 32; ++t) {
        const auto s = features::compute_sector_matched_autostrafe(
            0.0f, 180.0f, vel, 1.0f, 0.0f, sign, 700 + t);
        ASSERT_TRUE(s.active);
        EXPECT_LE(std::fabs(math::yaw_delta(180.0f, s.steer_yaw)), 22.5f);
        EXPECT_FLOAT_EQ(s.forwardmove, std::round(s.forwardmove));
        EXPECT_FLOAT_EQ(s.leftmove, std::round(s.leftmove));
        // Idempotent for identical command_number.
        const auto s_dup = features::compute_sector_matched_autostrafe(
            0.0f, 180.0f, vel, 1.0f, 0.0f, sign, 700 + t);
        EXPECT_FLOAT_EQ(s_dup.steer_yaw, s.steer_yaw);
        EXPECT_EQ(s_dup.next_strafe_sign, s.next_strafe_sign);

        vel = step_air_physics_tick(vel, s.steer_yaw, s.forwardmove, s.leftmove);
        sign = s.next_strafe_sign;
    }
    EXPECT_GT(std::hypot(vel.x, vel.y), 300.0f);

    // Directional turns A (90), D (-90), S (180) from forward velocity.
    struct dir_case {
        float fwd;
        float left;
        float expected_yaw;
    };
    const std::array<dir_case, 3> cases{{
        {0.0f, 1.0f, 90.0f},
        {0.0f, -1.0f, -90.0f},
        {-1.0f, 0.0f, 180.0f},
    }};
    for (const auto& c : cases) {
        math::vec3 dvel{250.0f, 0.0f, 0.0f};
        int dsign = 1;
        for (int t = 0; t < 48; ++t) {
            const auto s = features::compute_sector_matched_autostrafe(
                0.0f, 0.0f, dvel, c.fwd, c.left, dsign, 800 + t);
            ASSERT_TRUE(s.active);
            EXPECT_LE(std::fabs(math::yaw_delta(0.0f, s.steer_yaw)), 22.5f);
            dvel = step_air_physics_tick(dvel, s.steer_yaw, s.forwardmove, s.leftmove);
            dsign = s.next_strafe_sign;
        }
        const float vel_yaw = std::atan2(dvel.y, dvel.x) * (180.0f / 3.14159265f);
        EXPECT_LT(std::fabs(math::yaw_delta(vel_yaw, c.expected_yaw)), 60.0f);
        EXPECT_GT(std::hypot(dvel.x, dvel.y), 150.0f);
    }

    // Idle & non-finite guards.
    const auto idle = features::compute_sector_matched_autostrafe(
        0.0f, 45.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 1, 1);
    EXPECT_FALSE(idle.active);
    EXPECT_FLOAT_EQ(idle.steer_yaw, 45.0f);

    const float qnan = std::numeric_limits<float>::quiet_NaN();
    const auto nan_out = features::compute_sector_matched_autostrafe(
        qnan, 0.0f, {250.0f, 0.0f, 0.0f}, 1.0f, 0.0f, -1, 2);
    EXPECT_FALSE(nan_out.active);
    EXPECT_TRUE(std::isfinite(nan_out.steer_yaw));
}

TEST(target, autostrafe_airaccelerate_exceeds_legacy_24_4_gain) {
    math::vec3 vel{250.0f, 0.0f, 0.0f};
    int sign = 1;
    for (int t = 0; t < 32; ++t) {
        const auto s = features::compute_autostrafe({0.0f, vel, 1.0f, 0.0f, sign});
        ASSERT_TRUE(s.active);
        const float rad = s.steer_yaw * (3.14159265f / 180.0f);
        const float wx = std::cos(rad);
        const float wy = std::sin(rad);
        const float add = 30.0f - (vel.x * wx + vel.y * wy);
        if (add > 0.0f) {
            const float accel = std::min(add, 12.0f * 250.0f / 64.0f);
            vel.x += accel * wx;
            vel.y += accel * wy;
        }
        sign = s.next_strafe_sign;
    }
    EXPECT_GT(std::hypot(vel.x, vel.y), 300.0f);

    // Boundary around 24.4 u/s and non-finite wish safety.
    const auto at_24_4 = features::compute_autostrafe({0.0f, {24.4f, 0.0f, 0.0f}, 1.0f, 0.0f, 1});
    EXPECT_TRUE(at_24_4.active);
    EXPECT_FLOAT_EQ(at_24_4.steer_yaw, 0.0f);
    const auto above_24_4 = features::compute_autostrafe({0.0f, {24.41f, 0.0f, 0.0f}, 1.0f, 0.0f, 1});
    EXPECT_TRUE(above_24_4.active);
    EXPECT_NEAR(above_24_4.steer_yaw, 90.0f, 0.5f);
    EXPECT_FALSE(features::compute_autostrafe(
                     {0.0f, {250.0f, 0.0f, 0.0f}, std::numeric_limits<float>::quiet_NaN(), 0.0f, 1})
                     .active);
}

TEST(target, rotate_move_inputs_preserves_world_wish_on_unit_square) {
    float out_f = 0.0f;
    float out_l = 0.0f;

    // W+A at cam=10 -> rotated to AA=-160 produces S+D (both < -0.5).
    features::rotate_move_inputs(1.0f, 1.0f, 10.0f, -160.0f, out_f, out_l);
    EXPECT_LT(out_f, -0.5f);
    EXPECT_LT(out_l, -0.5f);

    // W at 0 -> 90 produces right/D (0, -1).
    features::rotate_move_inputs(1.0f, 0.0f, 0.0f, 90.0f, out_f, out_l);
    EXPECT_NEAR(out_f, 0.0f, 1e-4f);
    EXPECT_FLOAT_EQ(out_l, -1.0f);

    // W at 0 -> 45 scales onto unit square (1, -1).
    features::rotate_move_inputs(1.0f, 0.0f, 0.0f, 45.0f, out_f, out_l);
    EXPECT_FLOAT_EQ(out_f, 1.0f);
    EXPECT_FLOAT_EQ(out_l, -1.0f);

    // Sub-threshold walk input preserves magnitude without unit-square expansion.
    features::rotate_move_inputs(0.3f, 0.0f, 0.0f, 90.0f, out_f, out_l);
    EXPECT_NEAR(out_f, 0.0f, 1e-4f);
    EXPECT_NEAR(out_l, -0.3f, 1e-4f);

    // Zero and NaN inputs return zero safely.
    features::rotate_move_inputs(0.0f, 0.0f, 0.0f, 90.0f, out_f, out_l);
    EXPECT_FLOAT_EQ(out_f, 0.0f);
    EXPECT_FLOAT_EQ(out_l, 0.0f);
    features::rotate_move_inputs(1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, out_f, out_l);
    EXPECT_FLOAT_EQ(out_f, 0.0f);
    EXPECT_FLOAT_EQ(out_l, 0.0f);
}

TEST(target, autostop_holds_on_fire_tick_and_honors_config_fields) {
    features::autostop_input in{};
    in.enabled = true;
    in.flags = config::auto_stop_flag::early | config::auto_stop_flag::between_shots;
    in.on_ground = true;
    in.has_velocity = true;
    in.velocity = {80.0f, 0.0f, 0.0f};
    in.speed_2d = 80.0f;
    in.cmd_yaw = 0.0f;
    in.has_target = true;
    in.weapon_ready = true;
    in.shot_qualified = true;
    in.auto_stop_speed = 60.0f;
    in.auto_stop_ticks = 6;
    in.auto_stop_wait = true;

    const auto high = features::compute_autostop(in);
    EXPECT_TRUE(high.active);
    EXPECT_FALSE(high.zero_move);
    EXPECT_LT(high.forwardmove, -0.5f);

    in.velocity = {55.0f, 0.0f, 0.0f};
    in.speed_2d = 55.0f;
    const auto low = features::compute_autostop(in);
    EXPECT_TRUE(low.active);
    EXPECT_TRUE(low.zero_move);
    EXPECT_FLOAT_EQ(low.forwardmove, 0.0f);
    EXPECT_FLOAT_EQ(low.leftmove, 0.0f);

    // between_shots does not lock once weapon_ready recovers and target is gone.
    in.has_target = false;
    in.weapon_ready = true;
    in.between_shots_active = true;
    in.recent_shot_active = true;
    in.speed_2d = 200.0f;
    in.velocity = {200.0f, 0.0f, 0.0f};
    EXPECT_FALSE(features::compute_autostop(in).active);

    // Pure between-shots window helper prevents 80-tick Scout/AWP lock while allowing AK rapid bursts.
    EXPECT_FALSE(features::is_between_shots_window(80, 10, 70, 6));
    EXPECT_TRUE(features::is_between_shots_window(80, 76, 4, 6));
    EXPECT_FALSE(features::is_between_shots_window(80, 10, 0, 6));
    EXPECT_TRUE(features::is_between_shots_window(6, 3, 0, 6));
    EXPECT_FALSE(features::is_between_shots_window(6, 20, 0, 6));
}

TEST(target, shot_delay_tolerates_small_tick_gap_and_resets_on_large_gap_or_fail) {
    config::rage_weapon scout{};
    scout.delay_shot = config::delay_shot_mode::both;
    scout.delay_ticks = 6;
    scout.hitchance = 73.0f;
    scout.min_damage = 37.0f;

    features::shot_delay sd{};
    EXPECT_FALSE(sd.ready(0x10u, 0x20u, 100, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 1);
    EXPECT_FALSE(sd.ready(0x10u, 0x20u, 101, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 2);

    // Small gap (101 -> 103, delta=2 <= 3) advances without wiping progress.
    EXPECT_FALSE(sd.ready(0x10u, 0x20u, 103, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 3);

    // Same tick is idempotent.
    EXPECT_FALSE(sd.ready(0x10u, 0x20u, 103, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 3);

    // Large gap (103 -> 107, delta=4 > 3) resets streak to 1.
    EXPECT_FALSE(sd.ready(0x10u, 0x20u, 107, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 1);

    // Backward tick resets streak to 1.
    EXPECT_FALSE(sd.ready(0x10u, 0x20u, 105, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 1);

    // Failing condition resets to 0.
    EXPECT_FALSE(sd.ready(0x10u, 0x20u, 106, scout, 50.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 0);
}

TEST(usercmd, subtick_step_reuse_checks_capacity_and_null_and_clears_0x60_has_bits) {
    struct mock_rep {
        int max = 8;
        sdk::subtick_move_step_pb* elements[8]{};
    };

    std::array<sdk::subtick_move_step_pb, 4> pool{};
    mock_rep rep{};
    for (int i = 0; i < 4; ++i) rep.elements[i] = &pool[static_cast<std::size_t>(i)];

    sdk::repeated_ptr_field<sdk::subtick_move_step_pb> field{};
    field.ss = reinterpret_cast<sdk::repeated_ptr_field<sdk::subtick_move_step_pb>::rep_t*>(&rep);
    field.capacity = 3;
    field.current_size = 0;

    // Valid reuse increments current_size.
    EXPECT_EQ(sdk::try_reuse_subtick_move_step(field), &pool[0]);
    EXPECT_EQ(field.current_size, 1);

    // Null element in slot blocks reuse without incrementing current_size.
    rep.elements[1] = nullptr;
    EXPECT_EQ(sdk::try_reuse_subtick_move_step(field), nullptr);
    EXPECT_EQ(field.current_size, 1);

    // Restore slot 1, test capacity boundary (current_size == capacity < ss->max).
    rep.elements[1] = &pool[1];
    field.current_size = 3;
    EXPECT_EQ(sdk::try_reuse_subtick_move_step(field), nullptr);
    EXPECT_EQ(field.current_size, 3);

    // ss->max boundary (current_size == ss->max < capacity).
    field.capacity = 4;
    rep.max = 2;
    field.current_size = 2;
    EXPECT_EQ(sdk::try_reuse_subtick_move_step(field), nullptr);
    EXPECT_EQ(field.current_size, 2);

    // Negative / >= 32 / null ss guards.
    field.current_size = -1;
    EXPECT_EQ(sdk::try_reuse_subtick_move_step(field), nullptr);
    EXPECT_EQ(field.current_size, -1);
    field.current_size = 32;
    EXPECT_EQ(sdk::try_reuse_subtick_move_step(field), nullptr);
    field.ss = nullptr;
    field.current_size = 0;
    EXPECT_EQ(sdk::try_reuse_subtick_move_step(field), nullptr);

    // sort_last_subtick_move_step orders steps monotonically by `when`.
    rep.max = 8;
    field.ss = reinterpret_cast<sdk::repeated_ptr_field<sdk::subtick_move_step_pb>::rep_t*>(&rep);
    pool[0].set_when(0.8f);
    pool[1].set_when(0.2f);
    field.current_size = 2;
    sdk::sort_last_subtick_move_step(field, &pool[1], 0.2f);
    EXPECT_EQ(rep.elements[0], &pool[1]);
    EXPECT_EQ(rep.elements[1], &pool[0]);

    // apply_fire_angles clears 0x60 view-delta has_bits while preserving button/pressed/when bits.
    pool[0].set_button(sdk::k_in_attack);
    pool[0].set_pressed(true);
    pool[0].set_when(0.2f);
    pool[0].set_pitch_delta(1.5f);
    pool[0].set_yaw_delta(-2.5f);
    pool[1].set_button(sdk::k_in_jump);
    pool[1].set_pitch_delta(0.0f);
    pool[1].set_yaw_delta(0.0f);
    EXPECT_EQ(pool[0].has_bits & 0x60u, 0x60u);

    sdk::msg_qangle_pb va{};
    sdk::base_user_cmd_pb base{};
    base.viewangles = &va;
    base.subtick_moves = field;
    base.subtick_moves.capacity = 2;
    base.subtick_moves.current_size = 2;
    sdk::user_command cmd{};
    cmd.pb.base = &base;

    ASSERT_TRUE(sdk::apply_fire_angles(&cmd, {120.0f, 200.0f}));
    EXPECT_FLOAT_EQ(va.x, 89.0f);
    EXPECT_FLOAT_EQ(va.y, -160.0f);
    EXPECT_EQ(pool[0].has_bits & 0x60u, 0u);
    EXPECT_EQ(pool[1].has_bits & 0x60u, 0u);
    EXPECT_EQ(pool[0].has_bits & 0x07u, 0x07u);
    EXPECT_FLOAT_EQ(pool[0].pitch_delta, 0.0f);
    EXPECT_FLOAT_EQ(pool[0].yaw_delta, 0.0f);
}

TEST(binds, all_seven_bound_features_disjoint_and_focus_loss_safe) {
    constexpr std::array<config::bound_feature, 7> all{{
        config::bound_feature::rage,
        config::bound_feature::anti_aim,
        config::bound_feature::esp,
        config::bound_feature::third_person,
        config::bound_feature::bhop,
        config::bound_feature::auto_strafe,
        config::bound_feature::peek_assist,
    }};
    std::uint32_t combined = 0u;
    for (const auto f : all) {
        const std::uint32_t bit = config::feature_bit(f);
        EXPECT_NE(bit, 0u);
        EXPECT_EQ(bit & (bit - 1u), 0u);
        EXPECT_EQ(combined & bit, 0u);
        combined |= bit;
    }
    EXPECT_EQ(combined, (1u << 7) - 1u);
}

TEST(createmove, rage_group_rank_and_body_fallback_honor_prefer_body_aim) {
    // Default order: head (0) -> stomach (2) -> chest (1) -> pelvis (3) -> limbs (4, 5).
    EXPECT_LT(features::rage_group_rank(0, false, false), features::rage_group_rank(2, false, false));
    EXPECT_LT(features::rage_group_rank(2, false, false), features::rage_group_rank(1, false, false));
    EXPECT_LT(features::rage_group_rank(1, false, false), features::rage_group_rank(3, false, false));
    EXPECT_LT(features::rage_group_rank(3, false, false), features::rage_group_rank(4, false, false));

    // prefer_body_aim / force_body_aim prioritize stomach -> chest -> pelvis before head.
    EXPECT_LT(features::rage_group_rank(2, false, true), features::rage_group_rank(0, false, true));
    EXPECT_LT(features::rage_group_rank(1, false, true), features::rage_group_rank(0, false, true));
    EXPECT_LT(features::rage_group_rank(3, false, true), features::rage_group_rank(0, false, true));
    EXPECT_LT(features::rage_group_rank(2, true, false), features::rage_group_rank(0, true, false));

    // Unqualified lethal head (hc 30.9 < req 73) falls back to qualified body shot (stomach hc 78 >= 73).
    const features::rage_shot_candidate low_hc_head{292.3f, 30.9f, 2.0f, true, true};
    const features::rage_shot_candidate qual_body{75.0f, 78.0f, 3.0f, true, true};
    EXPECT_EQ(features::choose_rage_shot_point(qual_body, low_hc_head, 100, 37.0f, 73.0f, false, false), 1);

    // Non-lethal qualified head beats non-lethal qualified body when prefer_body_aim is false,
    // while prefer_body_aim = true selects qualified body.
    const features::rage_shot_candidate qual_head{95.0f, 80.0f, 1.0f, true, true};
    EXPECT_EQ(features::choose_rage_shot_point(qual_body, qual_head, 100, 37.0f, 73.0f, false, false), 0);
    EXPECT_EQ(features::choose_rage_shot_point(qual_body, qual_head, 100, 37.0f, 73.0f, false, true), 1);

    // Lethal body beats qualified head even without prefer_body_aim.
    const features::rage_shot_candidate lethal_body{102.0f, 75.0f, 3.0f, true, true};
    EXPECT_EQ(features::choose_rage_shot_point(lethal_body, qual_head, 100, 37.0f, 73.0f, false, false), 1);
    EXPECT_EQ(features::choose_rage_shot_point(std::nullopt, std::nullopt, 100, 37.0f, 73.0f, false, false), -1);
}

TEST(createmove, rage_group_enabled_separates_pelvis_from_arms_and_legs) {
    const std::uint32_t torso_and_legs = config::aim_point::head | config::aim_point::chest |
                                         config::aim_point::stomach | config::aim_point::pelvis |
                                         config::aim_point::legs;

    // ignore_limbs = true suppresses legs (4) and arms (5) while keeping pelvis (3) enabled.
    EXPECT_TRUE(features::rage_group_enabled(torso_and_legs, 0, true, false));
    EXPECT_TRUE(features::rage_group_enabled(torso_and_legs, 1, true, false));
    EXPECT_TRUE(features::rage_group_enabled(torso_and_legs, 2, true, false));
    EXPECT_TRUE(features::rage_group_enabled(torso_and_legs, 3, true, false));
    EXPECT_FALSE(features::rage_group_enabled(torso_and_legs, 4, true, false));
    EXPECT_FALSE(features::rage_group_enabled(torso_and_legs, 5, true, false));

    // ignore_limbs = false enables legs (4) and arms (5) when aim_point::legs is set.
    EXPECT_TRUE(features::rage_group_enabled(torso_and_legs, 4, false, false));
    EXPECT_TRUE(features::rage_group_enabled(torso_and_legs, 5, false, false));

    // force_baim disables head (0) regardless of aim_mask.
    EXPECT_FALSE(features::rage_group_enabled(torso_and_legs, 0, false, true));
    EXPECT_TRUE(features::rage_group_enabled(torso_and_legs, 3, true, true));
}

TEST(createmove, extrapolation_gap_zero_when_current_and_clamped_on_lag) {
    // default.json configures extrapolation_ms = 50.0; tick_gap <= 1 must extrapolate 0 ms.
    EXPECT_FLOAT_EQ(features::compute_extrapolation_gap_ms(1000, 1000, 50.0f), 0.0f);
    EXPECT_FLOAT_EQ(features::compute_extrapolation_gap_ms(1000, 999, 50.0f), 0.0f);
    EXPECT_FLOAT_EQ(features::compute_extrapolation_gap_ms(1000, 1001, 50.0f), 0.0f);

    // Genuine lag gap >= 2 steps bridges (tick_gap - 1) ticks clamped by configured_extrap_ms (<= 50 ms).
    EXPECT_NEAR(features::compute_extrapolation_gap_ms(1000, 998, 50.0f), 15.625f, 0.01f);
    EXPECT_NEAR(features::compute_extrapolation_gap_ms(1000, 997, 50.0f), 31.25f, 0.01f);
    EXPECT_FLOAT_EQ(features::compute_extrapolation_gap_ms(1000, 990, 50.0f), 50.0f);

    // Invalid/NaN/non-positive inputs return 0.
    EXPECT_FLOAT_EQ(features::compute_extrapolation_gap_ms(1000, 990, std::numeric_limits<float>::quiet_NaN()), 0.0f);
    EXPECT_FLOAT_EQ(features::compute_extrapolation_gap_ms(0, 990, 50.0f), 0.0f);
    EXPECT_FLOAT_EQ(features::compute_extrapolation_gap_ms(1000, 0, 50.0f), 0.0f);
}

TEST(createmove, shot_point_lethal_center_tiebreak_helmet_and_shotgun_scaling) {
    // When both points are lethal (>= target_health), center beats edge even if edge has higher raw damage.
    const features::rage_shot_candidate lethal_center{115.0f, 70.0f, 3.0f, true, true};
    const features::rage_shot_candidate lethal_edge{140.0f, 60.0f, 1.0f, true, false};
    EXPECT_FALSE(features::better_rage_shot_point(lethal_edge, lethal_center, 100, 0.0f, false));
    EXPECT_TRUE(features::better_rage_shot_point(lethal_center, lethal_edge, 100, 0.0f, false));

    // Safe point >= 50 or strict_hitchance widens damage tolerance from 4 to 10 so center beats edge within 8 dmg.
    const features::rage_shot_candidate nonlethal_center{60.0f, 65.0f, 2.0f, true, true};
    const features::rage_shot_candidate nonlethal_edge{68.0f, 55.0f, 1.5f, true, false};
    EXPECT_TRUE(features::better_rage_shot_point(nonlethal_edge, nonlethal_center, 100, 0.0f, false));
    EXPECT_FALSE(features::better_rage_shot_point(nonlethal_edge, nonlethal_center, 100, 50.0f, false));
    EXPECT_FALSE(features::better_rage_shot_point(nonlethal_edge, nonlethal_center, 100, 0.0f, true));

    // Unknown helmet: optimistic upper bound assumes false; actual estimate checks armor > 0.
    EXPECT_FALSE(features::resolve_effective_helmet(false, true, 100, true));
    EXPECT_TRUE(features::resolve_effective_helmet(false, false, 100, false));
    EXPECT_FALSE(features::resolve_effective_helmet(false, true, 0, false));
    EXPECT_TRUE(features::resolve_effective_helmet(true, true, 0, true));

    // Shotgun damage scales by hitchance fraction instead of assuming 100% pellet hit rate.
    EXPECT_FLOAT_EQ(features::scale_shotgun_damage(25.0f, 1, 50.0f), 25.0f);
    EXPECT_FLOAT_EQ(features::scale_shotgun_damage(20.0f, 8, 50.0f), 80.0f);
    EXPECT_FLOAT_EQ(features::scale_shotgun_damage(20.0f, 8, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(features::scale_shotgun_damage(-5.0f, 8, 80.0f), 0.0f);
}

TEST(createmove, fire_gate_honors_autostop_wait_resume_zoom_and_unscoped_hc) {
    features::rage_fire_gate_input in{};
    in.auto_scope = true;
    in.zoom_levels = 2;
    in.scoped = false;
    in.resume_zoom = false;
    in.next_secondary_tick = 100;
    in.tick_base = 100;
    in.auto_stop = true;
    in.auto_stop_wait = true;
    in.stop_env_ok = true;
    in.has_velocity = true;
    in.speed_2d = 90.0f;
    in.auto_stop_speed = 60.0f;
    in.on_ground = true;
    in.est_dmg = 90.0f;
    in.min_dmg = 37.0f;
    in.hc = 50.0f;
    in.req_hc = 73.0f;
    in.delay_ready = true;
    in.weapon_ready = true;

    // 1. Speed > auto_stop_speed with auto_stop_wait blocks firing and queues auto-scope.
    auto out = features::evaluate_rage_fire_gate(in);
    EXPECT_FALSE(out.stop_wait_ok);
    EXPECT_TRUE(out.wants_scope);
    EXPECT_TRUE(out.scope_pending);
    EXPECT_FALSE(out.will_fire);

    // 2. Speed <= auto_stop_speed and unscoped hc >= req_hc bypasses scope_pending and fires immediately.
    in.speed_2d = 55.0f;
    in.hc = 76.0f;
    out = features::evaluate_rage_fire_gate(in);
    EXPECT_TRUE(out.stop_wait_ok);
    EXPECT_TRUE(out.shot_qualified);
    EXPECT_FALSE(out.scope_pending);
    EXPECT_TRUE(out.will_fire);

    // 3. resume_zoom == true suppresses auto_scope even when unscoped.
    in.hc = 50.0f;
    in.resume_zoom = true;
    out = features::evaluate_rage_fire_gate(in);
    EXPECT_FALSE(out.wants_scope);
    EXPECT_FALSE(out.scope_pending);
    EXPECT_FALSE(out.will_fire);

    // 4. Scout jump-shot (!stop_env_ok because in_air flag unset) allows fire despite speed > auto_stop_speed.
    in.resume_zoom = false;
    in.on_ground = false;
    in.stop_env_ok = false;
    in.speed_2d = 180.0f;
    in.hc = 80.0f;
    out = features::evaluate_rage_fire_gate(in);
    EXPECT_TRUE(out.stop_wait_ok);
    EXPECT_TRUE(out.will_fire);
}

TEST(createmove, scan_cache_reuses_same_command_and_invalidates_on_change) {
    features::rage_scan_cache cache{};
    cache.valid = true;
    cache.command_number = 420;
    cache.tick_base = 1000;
    cache.weapon_entity = 0xCAFEu;
    cache.weapon_ready = true;
    cache.scoped = 1;
    cache.fire_allowed = true;
    cache.on_ground = true;
    cache.player_count = 4;
    cache.clip = 10;
    cache.next_primary_tick = 995;
    cache.local_eye = {100.0f, 200.0f, 64.0f};
    cache.speed_2d = 12.0f;
    cache.camera_view = {5.0f, -90.0f};

    EXPECT_TRUE(features::can_reuse_rage_scan(
        cache, 420, 1000, 0xCAFEu, true, 1, true, true, 4, 10, 995,
        {100.1f, 200.1f, 64.0f}, 12.5f, {5.2f, -90.2f}));

    // Any key change (command_number, weapon, eye shift >= 0.5u, camera shift >= 0.5 deg) invalidates.
    EXPECT_FALSE(features::can_reuse_rage_scan(
        cache, 421, 1000, 0xCAFEu, true, 1, true, true, 4, 10, 995,
        {100.0f, 200.0f, 64.0f}, 12.0f, {5.0f, -90.0f}));
    EXPECT_FALSE(features::can_reuse_rage_scan(
        cache, 420, 1000, 0xBEEFu, true, 1, true, true, 4, 10, 995,
        {100.0f, 200.0f, 64.0f}, 12.0f, {5.0f, -90.0f}));
    EXPECT_FALSE(features::can_reuse_rage_scan(
        cache, 420, 1000, 0xCAFEu, true, 1, true, true, 4, 10, 995,
        {101.0f, 200.0f, 64.0f}, 12.0f, {5.0f, -90.0f}));
    EXPECT_FALSE(features::can_reuse_rage_scan(
        cache, 420, 1000, 0xCAFEu, true, 1, true, true, 4, 10, 995,
        {100.0f, 200.0f, 64.0f}, 12.0f, {5.0f, -91.0f}));
}

TEST(createmove, aa_gate_honors_bind_and_has_move_input_without_flicker) {
    config::anti_aim_cfg cfg{};
    cfg.enabled = false;
    cfg.on_move = false;
    cfg.on_air = true;

    const std::uint32_t aa_bit = config::feature_bit(config::bound_feature::anti_aim);

    // Bind active overrides raw cfg.enabled == false when stationary without move input.
    EXPECT_TRUE(features::compute_aa_gate(cfg, aa_bit, false, false, 2, false, true, 0.0f, false));
    // Bind inactive disables AA even if cfg.enabled == true.
    cfg.enabled = true;
    EXPECT_FALSE(features::compute_aa_gate(cfg, 0u, false, false, 2, false, true, 0.0f, false));

    // !on_move immediately suppresses on has_move_input == true even before speed_2d rises > 5 u/s.
    EXPECT_FALSE(features::compute_aa_gate(cfg, aa_bit, false, false, 2, false, true, 0.0f, true));
    EXPECT_FALSE(features::compute_aa_gate(cfg, aa_bit, false, false, 2, false, true, 6.0f, false));

    // on_move == true stays active while moving; blockers (safe_mode, use_held, grenade, ladder) suppress.
    cfg.on_move = true;
    EXPECT_TRUE(features::compute_aa_gate(cfg, aa_bit, false, false, 2, false, true, 220.0f, true));
    EXPECT_FALSE(features::compute_aa_gate(cfg, aa_bit, true, false, 2, false, true, 0.0f, false));
    EXPECT_FALSE(features::compute_aa_gate(cfg, aa_bit, false, true, 2, false, true, 0.0f, false));
    EXPECT_FALSE(features::compute_aa_gate(cfg, aa_bit, false, false, 2, true, true, 0.0f, false));
    EXPECT_FALSE(features::compute_aa_gate(cfg, aa_bit, false, false, 9, false, true, 0.0f, false));
}

TEST(createmove, edge_jump_yields_when_bhop_active_and_jump_held) {
    EXPECT_FALSE(features::should_run_edge_jump(false, true, true, true, true, 250.0f, 2));
    EXPECT_TRUE(features::should_run_edge_jump(false, true, true, false, true, 250.0f, 2));
    EXPECT_TRUE(features::should_run_edge_jump(false, true, false, true, true, 250.0f, 2));
    EXPECT_FALSE(features::should_run_edge_jump(false, true, false, false, false, 250.0f, 2));
    EXPECT_FALSE(features::should_run_edge_jump(false, true, false, false, true, 10.0f, 2));
    EXPECT_FALSE(features::should_run_edge_jump(true, true, false, false, true, 250.0f, 2));
    EXPECT_FALSE(features::should_run_edge_jump(false, true, false, false, true, 250.0f, 9));
}

TEST(createmove, auto_pistol_and_peek_stall_step_once_per_command_number) {
    features::createmove_state st{};
    // 5 render calls at 300 Hz on cmd 100: initial shot stays pressed (returns false all 5 times).
    for (int sub = 0; sub < 5; ++sub) {
        EXPECT_FALSE(features::step_auto_pistol(st, 100, true, true, 0.15f, true));
    }
    EXPECT_GT(st.auto_pistol_wait, 0);

    // Subsequent commands decrement once per command_number (not per render frame) and release k_in_attack.
    const int wait_after_shot = st.auto_pistol_wait;
    for (int sub = 0; sub < 5; ++sub) {
        EXPECT_TRUE(features::step_auto_pistol(st, 101, true, true, 0.15f, false));
    }
    EXPECT_EQ(st.auto_pistol_wait, wait_after_shot - 1);

    // Exhausting wait while weapon_can_fire == false keeps k_in_attack released until weapon can fire.
    int cmd = 102;
    while (st.auto_pistol_wait > 0) {
        EXPECT_TRUE(features::step_auto_pistol(st, cmd++, true, true, 0.15f, false));
    }
    EXPECT_TRUE(features::step_auto_pistol(st, cmd++, true, true, 0.15f, false));
    EXPECT_FALSE(features::step_auto_pistol(st, cmd++, true, true, 0.15f, true));

    // Ineligible resets cleanly.
    EXPECT_FALSE(features::step_auto_pistol(st, cmd, false, true, 0.15f, true));
    EXPECT_EQ(st.auto_pistol_wait, 0);
    EXPECT_EQ(st.last_auto_pistol_cmd, -1);

    // Peek assist stall advances strictly once per command_number.
    features::peek_assist_state pa{};
    pa.active = true;
    pa.returning = true;
    pa.best_dist = 100.0f;
    pa.last_cmd = 500;
    for (int tick = 501; tick <= 565; ++tick) {
        for (int sub = 0; sub < 5; ++sub) {
            features::step_peek_assist_stall(pa, tick, 100.0f);
        }
        EXPECT_EQ(pa.stall_ticks, tick - 500);
    }
    EXPECT_GT(pa.stall_ticks, 64);
    features::step_peek_assist_stall(pa, 566, 90.0f);
    EXPECT_EQ(pa.stall_ticks, 0);
    EXPECT_FLOAT_EQ(pa.best_dist, 90.0f);
}

TEST(createmove, strafe_sign_latches_once_per_command_number) {
    features::createmove_state st{};
    st.strafe_pending_sign = -1;
    // Call 1 on cmd 200 latches -1; even if pending flips before call 2 on cmd 200, latched stays -1.
    EXPECT_EQ(features::latch_strafe_sign(st, 200), -1);
    st.strafe_pending_sign = 1;
    EXPECT_EQ(features::latch_strafe_sign(st, 200), -1);
    // Advancing to cmd 201 picks up pending +1.
    EXPECT_EQ(features::latch_strafe_sign(st, 201), 1);
    features::reset_strafe_state(st);
    EXPECT_EQ(st.last_strafe_cmd, -1);
    EXPECT_EQ(st.strafe_sign, 1);
}

TEST(createmove, fire_tick_movement_preserves_autostrafe_and_resolves_ground_move) {
    // Airborne with strafe_applied: rotates strafe_out into fire yaw without losing air accel vector.
    features::autostrafe_output strafe_out{};
    strafe_out.active = true;
    strafe_out.forwardmove = 1.0f;
    strafe_out.leftmove = 0.0f;
    strafe_out.steer_yaw = 10.0f;

    features::quantized_move_state gm{};
    float out_f = 0.0f;
    float out_l = 0.0f;
    features::resolve_fire_tick_movement(
        true, strafe_out, false, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f,
        0.0f, 100.0f, gm, 300, out_f, out_l);
    EXPECT_NEAR(out_f, 0.0f, 1e-4f);
    EXPECT_FLOAT_EQ(out_l, -1.0f);

    // Grounded same command_number: first solves for AA yaw (17.3 deg), then re-solves cleanly for fire yaw (0 deg).
    const math::vec3 wish = features::compute_wish_world_dir(0.0f, 1.0f, 0.0f);
    features::compute_quantized_ground_move(wish, 17.3f, gm, 301, out_f, out_l);
    features::resolve_fire_tick_movement(
        false, {}, true, wish, 1.0f, 0.0f, 0.0f, 0.0f, gm, 301, out_f, out_l);
    EXPECT_FLOAT_EQ(out_f, 1.0f);
    EXPECT_FLOAT_EQ(out_l, 0.0f);
}

TEST(createmove, strip_subtick_buttons_bounds_capacity_and_clears_move_mask) {
    struct mock_rep {
        int max = 4;
        sdk::subtick_move_step_pb* elements[8]{};
    };

    std::array<sdk::subtick_move_step_pb, 4> pool{};
    pool[0].set_button(sdk::k_in_forward);
    pool[1].set_button(sdk::k_in_jump);
    pool[2].set_button(sdk::k_in_moveleft);
    pool[3].set_button(sdk::k_in_attack);

    mock_rep rep{};
    for (int i = 0; i < 4; ++i) rep.elements[i] = &pool[static_cast<std::size_t>(i)];

    sdk::base_user_cmd_pb base{};
    base.subtick_moves.ss =
        reinterpret_cast<sdk::repeated_ptr_field<sdk::subtick_move_step_pb>::rep_t*>(&rep);
    base.subtick_moves.capacity = 3;
    base.subtick_moves.current_size = 4;  // intentionally > capacity to verify min({size, cap, max}) clamping

    features::strip_subtick_buttons(&base, features::k_move_button_mask);
    EXPECT_EQ(base.subtick_moves.current_size, 1);
    EXPECT_EQ(rep.elements[0], &pool[1]);

    // sync_move_held_buttons updates held bits cleanly on positive/negative/zero inputs.
    sdk::user_command cmd{};
    cmd.button_state.held = sdk::k_in_jump | sdk::k_in_back | sdk::k_in_moveright;
    features::sync_move_held_buttons(&cmd, 1.0f, -1.0f);
    EXPECT_NE(cmd.button_state.held & sdk::k_in_jump, 0u);
    EXPECT_NE(cmd.button_state.held & sdk::k_in_forward, 0u);
    EXPECT_NE(cmd.button_state.held & sdk::k_in_moveright, 0u);
    EXPECT_EQ(cmd.button_state.held & (sdk::k_in_back | sdk::k_in_moveleft), 0u);
}

TEST(createmove, shot_point_strict_weak_order_permutation_invariant_and_nan_safe) {
    // Challenger 1 counterexample triple: must induce a strict weak order (C > A > B)
    // and select the exact same winner across all 6 scan permutations.
    const std::array<features::rage_shot_candidate, 3> triple{{
        {35.95f, 0.0f, 162.51f, true, false},  // A
        {24.36f, 0.0f, 47.91f, true, true},    // B
        {32.24f, 0.0f, 67.73f, true, false},   // C
    }};
    EXPECT_TRUE(features::better_rage_shot_point(triple[2], triple[0], 100, 0.0f, true));
    EXPECT_FALSE(features::better_rage_shot_point(triple[0], triple[2], 100, 0.0f, true));
    EXPECT_TRUE(features::better_rage_shot_point(triple[0], triple[1], 100, 0.0f, true));
    EXPECT_TRUE(features::better_rage_shot_point(triple[2], triple[1], 100, 0.0f, true));

    constexpr int k_perms[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };
    for (const auto& perm : k_perms) {
        std::optional<features::rage_shot_candidate> best;
        for (int idx : perm) {
            if (features::better_rage_shot_point(triple[idx], best, 100, 0.0f, true)) {
                best = triple[idx];
            }
        }
        ASSERT_TRUE(best.has_value());
        EXPECT_FLOAT_EQ(best->damage, 32.24f);
        EXPECT_FLOAT_EQ(best->fov, 67.73f);
    }

    // Deterministic LCG sweep across 500 triples: zero irreflexivity, asymmetry, or transitivity bugs.
    std::uint32_t rng = 0x12345678u;
    const auto next_u32 = [&]() noexcept {
        rng = rng * 1664525u + 1013904223u;
        return rng;
    };
    const auto rand_cand = [&]() noexcept {
        const float dmg = 1.0f + static_cast<float>(next_u32() % 14000u) * 0.01f;
        const float fov = static_cast<float>(next_u32() % 18000u) * 0.01f;
        const bool direct = (next_u32() & 1u) != 0u;
        const bool center = (next_u32() & 2u) != 0u;
        return features::rage_shot_candidate{dmg, 0.0f, fov, direct, center};
    };
    for (int t = 0; t < 500; ++t) {
        const features::rage_shot_candidate pts[3]{rand_cand(), rand_cand(), rand_cand()};
        const bool strict = (t & 1) != 0;
        const float safe = (t & 2) != 0 ? 50.0f : 0.0f;
        for (int i = 0; i < 3; ++i) {
            EXPECT_FALSE(features::better_rage_shot_point(pts[i], pts[i], 100, safe, strict));
            for (int j = 0; j < 3; ++j) {
                const bool ij = features::better_rage_shot_point(pts[i], pts[j], 100, safe, strict);
                const bool ji = features::better_rage_shot_point(pts[j], pts[i], 100, safe, strict);
                EXPECT_FALSE(ij && ji);
                for (int k = 0; k < 3; ++k) {
                    const bool jk =
                        features::better_rage_shot_point(pts[j], pts[k], 100, safe, strict);
                    const bool ik =
                        features::better_rage_shot_point(pts[i], pts[k], 100, safe, strict);
                    if (ij && jk) {
                        EXPECT_TRUE(ik);
                    }
                }
            }
        }
    }

    // NaN / Inf / negative candidates never win or poison valid candidates.
    const float qnan = std::numeric_limits<float>::quiet_NaN();
    const float pinf = std::numeric_limits<float>::infinity();
    const features::rage_shot_candidate nan_center{qnan, 90.0f, 1.0f, true, true};
    const features::rage_shot_candidate inf_center{pinf, 90.0f, 1.0f, true, true};
    const features::rage_shot_candidate neg_center{-5.0f, 90.0f, 1.0f, true, true};
    const features::rage_shot_candidate nan_fov{95.0f, 90.0f, qnan, true, true};
    const features::rage_shot_candidate valid_edge{95.0f, 50.0f, 5.0f, true, false};

    EXPECT_FALSE(features::better_rage_shot_point(nan_center, std::nullopt, 100, 0.0f, false));
    EXPECT_FALSE(features::better_rage_shot_point(inf_center, std::nullopt, 100, 0.0f, false));
    EXPECT_FALSE(features::better_rage_shot_point(neg_center, std::nullopt, 100, 0.0f, false));
    EXPECT_FALSE(features::better_rage_shot_point(nan_fov, std::nullopt, 100, 0.0f, false));
    EXPECT_FALSE(features::better_rage_shot_point(nan_center, valid_edge, 100, 0.0f, false));
    EXPECT_TRUE(features::better_rage_shot_point(valid_edge, nan_center, 100, 0.0f, false));

    // choose_rage_shot_point skips non-finite / non-positive candidates cleanly.
    const features::rage_shot_candidate nan_body{qnan, qnan, 1.0f, true, true};
    const features::rage_shot_candidate low_valid_head{90.0f, 50.0f, 2.0f, true, true};
    EXPECT_EQ(features::choose_rage_shot_point(
                  nan_body, low_valid_head, 100, 37.0f, 73.0f, false, false),
              0);
    EXPECT_EQ(features::choose_rage_shot_point(
                  low_valid_head, nan_body, 100, 37.0f, 73.0f, false, false),
              1);
    EXPECT_EQ(features::choose_rage_shot_point(
                  nan_body, inf_center, 100, 37.0f, 73.0f, false, false),
              -1);

    // Unevaluated or low-hitchance non-lethal best_body must never skip lower-damage body hitboxes.
    const features::rage_shot_candidate nonlethal_body{85.0f, 40.0f, 2.0f, true, true};
    EXPECT_FALSE(features::should_skip_lower_hitbox(
        nonlethal_body, false, 70.0f, 73.0f, 0.0f, false, 1));
    EXPECT_FALSE(features::should_skip_lower_hitbox(
        nonlethal_body, true, 70.0f, 73.0f, 0.0f, false, 1));
    const features::rage_shot_candidate qual_nonlethal_body{85.0f, 80.0f, 2.0f, true, true};
    EXPECT_TRUE(features::should_skip_lower_hitbox(
        qual_nonlethal_body, true, 70.0f, 73.0f, 0.0f, false, 1));
    EXPECT_FALSE(features::should_skip_lower_hitbox(
        qual_nonlethal_body, true, 70.0f, 73.0f, 0.0f, false, 8));
}

TEST(createmove, fire_gate_fails_closed_on_nonfinite_speed_and_extreme_auto_pistol_cycle) {
    const float qnan = std::numeric_limits<float>::quiet_NaN();
    const float pinf = std::numeric_limits<float>::infinity();
    const float ninf = -std::numeric_limits<float>::infinity();

    features::rage_fire_gate_input in{};
    in.auto_stop = true;
    in.auto_stop_wait = true;
    in.stop_env_ok = true;
    in.has_velocity = true;
    in.auto_stop_speed = 60.0f;
    in.on_ground = true;
    in.est_dmg = 80.0f;
    in.min_dmg = 37.0f;
    in.hc = 80.0f;
    in.req_hc = 73.0f;
    in.delay_ready = true;
    in.weapon_ready = true;

    for (float bad_speed : {qnan, pinf, ninf}) {
        in.speed_2d = bad_speed;
        const auto out = features::evaluate_rage_fire_gate(in);
        EXPECT_FALSE(out.stop_wait_ok);
        EXPECT_FALSE(out.will_fire);
    }

    // Non-finite damage/hitchance inputs also fail closed.
    in.speed_2d = 10.0f;
    in.req_hc = qnan;
    EXPECT_FALSE(features::evaluate_rage_fire_gate(in).shot_qualified);
    EXPECT_FALSE(features::evaluate_rage_fire_gate(in).will_fire);

    in.req_hc = 73.0f;
    in.min_dmg = qnan;
    const auto nan_min_out = features::evaluate_rage_fire_gate(in);
    EXPECT_FALSE(nan_min_out.shot_qualified);
    EXPECT_FALSE(nan_min_out.scope_pending);
    EXPECT_FALSE(nan_min_out.will_fire);

    // Extreme finite cycle_time in step_auto_pistol stays bounded without int overflow.
    features::createmove_state st{};
    EXPECT_FALSE(features::step_auto_pistol(st, 1, true, true, 1e12f, true));
    EXPECT_GT(st.auto_pistol_wait, 0);
    EXPECT_LE(st.auto_pistol_wait, 60 * 64 - 1);
}

TEST(createmove, update_rage_shot_slot_lazy_eval_replaces_unqualified_and_skips_shotgun) {
    struct mock_slot {
        float damage = 0.0f;
        float hitchance = 0.0f;
        float fov = 0.0f;
        bool direct = false;
        bool is_center = true;
        bool hc_evaluated = false;
        int id = 0;
        float oracle_hc = 0.0f;
    };

    const auto run_eval = [](int& eval_count) {
        return [&eval_count](std::optional<mock_slot>& s) {
            if (!s || s->hc_evaluated) return;
            ++eval_count;
            s->hitchance = s->oracle_hc;
            s->hc_evaluated = true;
        };
    };

    // 1. default.json AK-47 scenario: safe_point=50 (skip_tol=10), req_hc=60.
    //    stomach (35 dmg, hc=30 < 60) is within skip_tol of chest (28 dmg, hc=90 >= 60).
    //    slot starts unevaluated, gets lazily evaluated, and is replaced by qualified chest.
    {
        std::optional<mock_slot> slot;
        int eval_count = 0;
        auto eval_hc = run_eval(eval_count);

        const mock_slot stomach{35.0f, 0.0f, 2.0f, true, true, false, 1, 30.0f};
        const mock_slot chest{28.0f, 0.0f, 2.0f, true, true, false, 2, 90.0f};

        EXPECT_TRUE(features::update_rage_shot_slot(
            slot, stomach, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 0);
        ASSERT_TRUE(slot.has_value());
        EXPECT_FALSE(slot->hc_evaluated);
        EXPECT_FALSE(features::should_skip_lower_hitbox(
            features::rage_shot_candidate{slot->damage, slot->hitchance, slot->fov,
                                          slot->direct, slot->is_center, slot->hc_evaluated},
            slot->hc_evaluated, chest.damage, 60.0f, 50.0f, false, 1));

        EXPECT_TRUE(features::update_rage_shot_slot(
            slot, chest, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 2);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(slot->id, 2);
        EXPECT_TRUE(slot->hc_evaluated);
        EXPECT_FLOAT_EQ(slot->damage, 28.0f);
        EXPECT_FLOAT_EQ(slot->hitchance, 90.0f);
    }

    // 2. Same-hitbox multipoint: pi=0 center (is_center=true, hc=25 < 60) vs
    //    pi=1 edge (is_center=false, hc=90 >= 60): edge replaces center.
    {
        std::optional<mock_slot> slot;
        int eval_count = 0;
        auto eval_hc = run_eval(eval_count);

        const mock_slot center{35.0f, 0.0f, 2.0f, true, true, false, 10, 25.0f};
        const mock_slot edge{35.0f, 0.0f, 2.5f, true, false, false, 11, 90.0f};

        EXPECT_TRUE(features::update_rage_shot_slot(
            slot, center, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 0);

        EXPECT_TRUE(features::update_rage_shot_slot(
            slot, edge, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 2);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(slot->id, 11);
        EXPECT_FALSE(slot->is_center);
        EXPECT_TRUE(slot->hc_evaluated);
        EXPECT_FLOAT_EQ(slot->hitchance, 90.0f);
    }

    // 3. Qualified center (pi=0, hc=85 >= 60) beats competing points in better_rage_shot_point:
    //    eval_hc is called ONLY once for slot and NOT redundantly for sp.
    {
        std::optional<mock_slot> slot;
        int eval_count = 0;
        auto eval_hc = run_eval(eval_count);

        const mock_slot center{35.0f, 0.0f, 2.0f, true, true, false, 20, 85.0f};
        const mock_slot edge{35.0f, 0.0f, 2.5f, true, false, false, 21, 95.0f};
        const mock_slot lower{28.0f, 0.0f, 1.5f, true, true, false, 22, 99.0f};

        EXPECT_TRUE(features::update_rage_shot_slot(
            slot, center, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 0);

        EXPECT_FALSE(features::update_rage_shot_slot(
            slot, edge, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 1);

        EXPECT_FALSE(features::update_rage_shot_slot(
            slot, lower, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 1);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(slot->id, 20);
        EXPECT_TRUE(slot->hc_evaluated);
        EXPECT_FLOAT_EQ(slot->hitchance, 85.0f);
    }

    // 4. Non-lethal best_head across lagcomp records (newest hc=20 vs shooting hc=88).
    {
        std::optional<mock_slot> best_head;
        int eval_count = 0;
        auto eval_hc = run_eval(eval_count);

        const mock_slot newest_head{80.0f, 0.0f, 2.0f, true, true, false, 30, 20.0f};
        const mock_slot shooting_head{80.0f, 0.0f, 2.0f, true, true, false, 31, 88.0f};

        EXPECT_TRUE(features::update_rage_shot_slot(
            best_head, newest_head, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_TRUE(features::update_rage_shot_slot(
            best_head, shooting_head, 100, 60.0f, 50.0f, false, 1, eval_hc));
        EXPECT_EQ(eval_count, 2);
        ASSERT_TRUE(best_head.has_value());
        EXPECT_EQ(best_head->id, 31);
        EXPECT_FLOAT_EQ(best_head->hitchance, 88.0f);
    }

    // 5. Shotgun (num_bullets > 1) never triggers lazy eval_hc inside slot update.
    {
        std::optional<mock_slot> slot;
        int eval_count = 0;
        auto eval_hc = run_eval(eval_count);

        const mock_slot pellet1{35.0f, 0.0f, 2.0f, true, true, false, 40, 30.0f};
        const mock_slot pellet2{28.0f, 0.0f, 2.0f, true, true, false, 41, 95.0f};
        const mock_slot pellet3{48.0f, 0.0f, 3.0f, true, false, false, 42, 50.0f};

        EXPECT_TRUE(features::update_rage_shot_slot(
            slot, pellet1, 100, 60.0f, 50.0f, false, 8, eval_hc));
        EXPECT_FALSE(features::update_rage_shot_slot(
            slot, pellet2, 100, 60.0f, 50.0f, false, 8, eval_hc));
        EXPECT_TRUE(features::update_rage_shot_slot(
            slot, pellet3, 100, 60.0f, 50.0f, false, 8, eval_hc));
        EXPECT_EQ(eval_count, 0);
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(slot->id, 42);
        EXPECT_FALSE(slot->hc_evaluated);
        EXPECT_FLOAT_EQ(slot->damage, 48.0f);
    }
}

TEST(createmove, bhop_subframe_prediction_and_clean_move_button_transitions) {
    features::bhop_state bs{};
    // Airborne tick 100: jump is released in air.
    EXPECT_FALSE(features::step_bhop(bs, 100, false, true));

    // Landing tick 101, Sub-frame 0: pawn is on ground -> step_bhop returns true.
    EXPECT_TRUE(features::step_bhop(bs, 101, true, true));
    // Sub-frames 1..4 of the SAME command_number 101 after Sub-frame 0 ClientPrediction
    // cleared FL_ONGROUND to 0 in-place: step_bhop MUST preserve do_jump = true!
    for (int sf = 1; sf < 5; ++sf) {
        EXPECT_TRUE(features::step_bhop(bs, 101, false, true));
    }
    // Next command_number 102 in air: now cleanly transitions to false.
    EXPECT_FALSE(features::step_bhop(bs, 102, false, true));

    // sync_move_held_buttons clears stale toggle and scroll transition bits on W/A/S/D
    // while preserving non-movement transition bits (e.g. jump/attack).
    sdk::user_command cmd{};
    cmd.button_state.held = sdk::k_in_jump | sdk::k_in_forward | sdk::k_in_moveleft;
    cmd.button_state.toggle = sdk::k_in_jump | sdk::k_in_forward | sdk::k_in_back |
                              sdk::k_in_moveleft | sdk::k_in_moveright;
    cmd.button_state.scroll = sdk::k_in_attack | sdk::k_in_forward | sdk::k_in_moveright;
    features::sync_move_held_buttons(&cmd, 0.0f, -1.0f);
    EXPECT_EQ(cmd.button_state.held, sdk::k_in_jump | sdk::k_in_moveright);
    EXPECT_EQ(cmd.button_state.toggle, sdk::k_in_jump);
    EXPECT_EQ(cmd.button_state.scroll, sdk::k_in_attack);
}

TEST(createmove, runtime_air_strafe_eliminates_45deg_snaps_and_64hz_button_chatter) {
    features::createmove_state st{};
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_yaw = config::yaw_mode::fake_static;
    aa.auto_adjust = true;
    aa.at_targets = true;

    // 1. Straight-line jump (W held, diagonal initial velocity 37 deg):
    //    must hold each strafe lobe for >= k_min_strafe_lobe_ticks, exceed 300 u/s,
    //    and never snap steer_yaw by 45 deg between consecutive ticks.
    math::vec3 vel{200.0f, 150.0f, 0.0f};  // 250 u/s at +36.87 deg
    float prev_yaw = 0.0f;
    int sign_flips = 0;
    int prev_sign = 0;
    for (int t = 0; t < 32; ++t) {
        const auto out = features::compute_runtime_air_strafe(
            st, 500 + t, 0.0f, true, aa, vel, 1.0f, 0.0f);
        ASSERT_TRUE(out.active);
        EXPECT_FLOAT_EQ(out.forwardmove, 0.0f);
        EXPECT_FLOAT_EQ(std::fabs(out.leftmove), 1.0f);
        if (t > 0) {
            EXPECT_LT(std::fabs(math::yaw_delta(prev_yaw, out.steer_yaw)), 12.0f);
            if (out.next_strafe_sign != prev_sign) ++sign_flips;
        }
        prev_yaw = out.steer_yaw;
        prev_sign = out.next_strafe_sign;
        vel = step_air_physics_tick(vel, out.steer_yaw, out.forwardmove, out.leftmove);
    }
    EXPECT_GT(std::hypot(vel.x, vel.y), 300.0f);
    EXPECT_LE(sign_flips, 32 / features::k_min_strafe_lobe_ticks + 1);

    // 2. Smooth right mouse turn (-1.5 deg/tick): sign locks to -1 with zero button flips.
    features::reset_strafe_state(st);
    vel = {250.0f, 0.0f, 0.0f};
    float cam_yaw = 0.0f;
    for (int t = 0; t < 24; ++t) {
        cam_yaw = math::normalize_yaw(cam_yaw - 1.5f);
        const auto out = features::compute_runtime_air_strafe(
            st, 600 + t, cam_yaw, true, aa, vel, 1.0f, 0.0f);
        ASSERT_TRUE(out.active);
        if (t >= 1) {
            EXPECT_EQ(out.next_strafe_sign, -1);
            EXPECT_FLOAT_EQ(out.leftmove, 1.0f);
            EXPECT_LT(std::fabs(math::yaw_delta(prev_yaw, out.steer_yaw)), 12.0f);
        }
        prev_yaw = out.steer_yaw;
        vel = step_air_physics_tick(vel, out.steer_yaw, out.forwardmove, out.leftmove);
    }
    EXPECT_GE(std::hypot(vel.x, vel.y), 290.0f);
}
