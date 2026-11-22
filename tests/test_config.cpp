#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <set>

#include "config/bind_state.hpp"
#include "config/config.hpp"

using namespace shogod::config;

TEST(config, defaults_are_sane) {
    store s;
    EXPECT_EQ(s.get().version, k_schema_version);
    EXPECT_FALSE(s.get().rage.enabled);
    EXPECT_GT(s.get().rage.weapons[2].fov, 0.0f);
    EXPECT_FLOAT_EQ(s.get().ui.opacity, 0.97f);
}

TEST(config, retired_yaw_mode_loads_as_off) {
    store settings;
    ASSERT_TRUE(settings.from_json(R"({"version":1,"anti_aim":{"base_yaw":1,"desync_angle":58}})"));
    EXPECT_EQ(settings.get().anti_aim.base_yaw, yaw_mode::off);
    store reopened;
    ASSERT_TRUE(reopened.from_json(settings.to_json()));
    EXPECT_EQ(reopened.get().anti_aim.base_yaw, yaw_mode::off);
}

TEST(config, json_roundtrip_preserves_values) {
    store a;
    a.get().rage.enabled = true;
    a.get().rage.weapons[2].hitchance = 71.5f;
    a.get().rage.weapons[2].delay_shot = delay_shot_mode::hitchance;
    a.get().rage.weapons[2].delay_ticks = 4;
    a.get().rage.weapons[2].auto_stop_speed = 14.0f;
    a.get().rage.weapons[2].auto_stop_ticks = 9;
    a.get().rage.weapons[2].auto_stop_wait = true;
    a.get().rage.toggle = {77, true, false};
    a.get().visuals.weapon = false;
    a.get().visuals.weapon_icon = true;
    a.get().visuals.no_flash = true;
    a.get().visuals.no_smoke = true;
    a.get().visuals.no_scope = true;
    a.get().visuals.third_person = true;
    a.get().visuals.third_person_distance = 127;
    a.get().visuals.player_chams.enabled = true;
    a.get().visuals.player_chams.visible = {0.1f, 0.2f, 0.3f, 0.4f};
    a.get().ui.accent = {0.5f, 0.6f, 0.7f, 1.0f};

    const std::string text = a.to_json();

    store b;
    ASSERT_TRUE(b.from_json(text));
    EXPECT_TRUE(b.get().rage.enabled);
    EXPECT_FLOAT_EQ(b.get().rage.weapons[2].hitchance, 71.5f);
    EXPECT_EQ(b.get().rage.weapons[2].delay_shot, delay_shot_mode::hitchance);
    EXPECT_EQ(b.get().rage.weapons[2].delay_ticks, 4);
    EXPECT_FLOAT_EQ(b.get().rage.weapons[2].auto_stop_speed, 14.0f);
    EXPECT_EQ(b.get().rage.weapons[2].auto_stop_ticks, 9);
    EXPECT_TRUE(b.get().rage.weapons[2].auto_stop_wait);
    EXPECT_EQ(b.get().rage.toggle.vk, 77);
    EXPECT_TRUE(b.get().rage.toggle.hold);
    EXPECT_FALSE(b.get().visuals.weapon);
    EXPECT_TRUE(b.get().visuals.weapon_icon);
    EXPECT_TRUE(b.get().visuals.no_flash);
    EXPECT_TRUE(b.get().visuals.no_smoke);
    EXPECT_TRUE(b.get().visuals.no_scope);
    EXPECT_TRUE(b.get().visuals.third_person);
    EXPECT_EQ(b.get().visuals.third_person_distance, 127);
    EXPECT_TRUE(b.get().visuals.player_chams.enabled);
    EXPECT_FLOAT_EQ(b.get().visuals.player_chams.visible.b, 0.3f);
    EXPECT_FLOAT_EQ(b.get().ui.accent.g, 0.6f);
}

TEST(config, unknown_keys_survive_roundtrip) {
    const std::string in = R"({
        "version": 1,
        "future_feature": {"enabled": true, "value": 42},
        "rage": {"enabled": true}
    })";

    store s;
    ASSERT_TRUE(s.from_json(in));
    EXPECT_TRUE(s.get().rage.enabled);

    const std::string out = s.to_json();
    EXPECT_NE(out.find("future_feature"), std::string::npos);
    EXPECT_NE(out.find("\"value\": 42"), std::string::npos);
}

TEST(config, preserves_nested_unknown_profile_data) {
    store s;
    ASSERT_TRUE(s.from_json(R"({
      "version": 2,
      "rage": {"weapons": [{"hitchance": 40, "future_rule": 17}]},
      "visuals": {"player_chams": {"future_shader": "test"}}
    })"));
    s.get().rage.weapons[0].hitchance = 79.0f;
    const auto text = s.to_json();
    EXPECT_NE(text.find(R"("future_rule")"), std::string::npos);
    EXPECT_NE(text.find(R"("future_shader")"), std::string::npos);
    EXPECT_NE(text.find(R"("version": 2)"), std::string::npos);
    store reloaded;
    ASSERT_TRUE(reloaded.from_json(text));
    EXPECT_FLOAT_EQ(reloaded.get().rage.weapons[0].hitchance, 79.0f);
}

TEST(config, out_of_range_values_are_clamped_and_counted) {
    const std::string in = R"({
        "version": 1,
        "rage": {"weapons": [
            {"hitchance": 500.0, "fov": -20.0, "delay_ticks": 99},
            {}, {}, {}, {}, {}, {}, {}
        ]},
        "visuals": {"esp_alpha": 900}
    })";

    store s;
    ASSERT_TRUE(s.from_json(in));
    EXPECT_FLOAT_EQ(s.get().rage.weapons[0].hitchance, 100.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[0].fov, 0.0f);
    EXPECT_EQ(s.get().rage.weapons[0].delay_ticks, 6);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[0].auto_stop_speed, 25.0f);
    EXPECT_EQ(s.get().rage.weapons[0].auto_stop_ticks, 6);
    EXPECT_FALSE(s.get().rage.weapons[0].auto_stop_wait);
    EXPECT_EQ(s.get().rage.weapons[0].delay_shot, delay_shot_mode::both);
    EXPECT_EQ(s.get().visuals.esp_alpha, 255);
    EXPECT_GE(s.clamps_applied(), 4);
}

TEST(config, min_damage_clamps_to_1_120_and_aa_fields_roundtrip) {
    store s;
    ASSERT_TRUE(s.from_json(R"({
        "version": 1,
        "rage": {"weapons": [
            {"min_damage": 0.0},
            {"min_damage": 150.0},
            {}, {}, {}, {}, {}, {}
        ]},
        "anti_aim": {"enabled": true, "at_targets": false, "auto_adjust": false, "freestanding": true, "yaw_offset": 250.0}
    })"));
    EXPECT_FLOAT_EQ(s.get().rage.weapons[0].min_damage, 1.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[1].min_damage, 120.0f);
    EXPECT_FALSE(s.get().anti_aim.at_targets);
    EXPECT_FALSE(s.get().anti_aim.auto_adjust);
    EXPECT_TRUE(s.get().anti_aim.freestanding);
    EXPECT_FLOAT_EQ(s.get().anti_aim.yaw_offset, 180.0f);

    store reloaded;
    ASSERT_TRUE(reloaded.from_json(s.to_json()));
    EXPECT_FALSE(reloaded.get().anti_aim.at_targets);
    EXPECT_FALSE(reloaded.get().anti_aim.auto_adjust);
    EXPECT_TRUE(reloaded.get().anti_aim.freestanding);
    EXPECT_FLOAT_EQ(reloaded.get().anti_aim.yaw_offset, 180.0f);

    store fov_cfg;
    ASSERT_TRUE(fov_cfg.from_json(R"({
        "version": 1,
        "visuals": {"camera_fov": 25, "fov_while_scoped": true}
    })"));
    EXPECT_EQ(fov_cfg.get().visuals.camera_fov, 50);
    EXPECT_TRUE(fov_cfg.get().visuals.fov_while_scoped);
    store fov_reloaded;
    ASSERT_TRUE(fov_reloaded.from_json(fov_cfg.to_json()));
    EXPECT_EQ(fov_reloaded.get().visuals.camera_fov, 50);
    EXPECT_TRUE(fov_reloaded.get().visuals.fov_while_scoped);

    bind_state bs;
    const keybind hold_kb{0x20, true, false};
    EXPECT_FALSE(bs.sample(hold_kb, true, true, false));
    EXPECT_TRUE(bs.sample(hold_kb, true, true, true));
    EXPECT_TRUE(bs.sample(hold_kb, true, true, true));
    EXPECT_FALSE(bs.sample(hold_kb, true, true, false));
}

TEST(config, rejects_garbage) {
    store s;
    EXPECT_FALSE(s.from_json("{not json"));
    EXPECT_FALSE(s.from_json("[1,2,3]"));
}

TEST(config, file_save_and_load) {
    const auto dir = std::filesystem::temp_directory_path() / "shogod_config_test";
    std::filesystem::remove_all(dir);
    const auto path = dir / "test.json";

    store a;
    a.get().misc.movement.bhop = true;
    a.get().anti_aim.base_yaw = yaw_mode::jitter;
    ASSERT_TRUE(a.save(path));
    ASSERT_TRUE(std::filesystem::exists(path));

    store b;
    ASSERT_TRUE(b.load(path));
    EXPECT_TRUE(b.get().misc.movement.bhop);
    EXPECT_EQ(b.get().anti_aim.base_yaw, yaw_mode::jitter);
    b.get().misc.movement.bhop = false;
    ASSERT_TRUE(b.save(path));
    ASSERT_TRUE(a.load(path));
    EXPECT_FALSE(a.get().misc.movement.bhop);

    std::filesystem::remove_all(dir);
}

TEST(config, profiles_differ) {
    config_v1 safe;
    apply_safe_profile(safe);
    config_v1 rage;
    apply_rage_profile(rage);
    const std::size_t rifle_idx = static_cast<std::size_t>(weapon_group::rifle);
    const std::size_t scout_idx = static_cast<std::size_t>(weapon_group::scout);
    const std::size_t auto_idx = static_cast<std::size_t>(weapon_group::auto_sniper);
    const std::size_t awp_idx = static_cast<std::size_t>(weapon_group::awp);
    EXPECT_GT(rage.rage.weapons[rifle_idx].hitchance, safe.rage.weapons[rifle_idx].hitchance);
    EXPECT_LT(rage.rage.weapons[rifle_idx].safe_point, safe.rage.weapons[rifle_idx].safe_point);
    EXPECT_TRUE(rage.anti_aim.enabled);
    EXPECT_EQ(rage.anti_aim.base_pitch, pitch_mode::down);
    EXPECT_EQ(rage.anti_aim.base_yaw, yaw_mode::fake_static);
    EXPECT_TRUE(rage.rage.weapons[scout_idx].auto_stop);
    EXPECT_EQ(rage.rage.weapons[scout_idx].auto_stop_flags, auto_stop_flag::all);
    EXPECT_GT(rage.rage.weapons[awp_idx].min_damage, rage.rage.weapons[auto_idx].min_damage);
    EXPECT_NE(rage.rage.weapons[scout_idx].auto_stop_flags, rage.rage.weapons[awp_idx].auto_stop_flags);
}

TEST(config, legacy_six_group_weapons_migrate_cleanly_to_eight) {
    store s;
    ASSERT_TRUE(s.from_json(R"({
        "version": 1,
        "rage": {"weapons": [
            {"hitchance": 44.0},
            {"hitchance": 51.0},
            {"hitchance": 62.0},
            {"hitchance": 77.0},
            {"hitchance": 39.0},
            {"hitchance": 48.0}
        ]}
    })"));
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::pistol)].hitchance, 44.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::heavy_pistol)].hitchance, 44.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::smg)].hitchance, 51.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::rifle)].hitchance, 62.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::scout)].hitchance, 77.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::auto_sniper)].hitchance, 77.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::awp)].hitchance, 77.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[static_cast<std::size_t>(weapon_group::heavy)].hitchance, 48.0f);
}

TEST(config, per_weapon_overrides_resolve_independently_and_roundtrip) {
    store s;
    const int hp_group = static_cast<int>(weapon_group::heavy_pistol);
    s.get().rage.weapons[hp_group].min_damage = 48.0f;

    rage_weapon r8_cfg = s.get().rage.weapons[hp_group];
    r8_cfg.min_damage = 82.0f;
    r8_cfg.hitchance = 74.0f;
    r8_cfg.prefer_body_aim = true;
    s.get().rage.weapon_overrides[64] = r8_cfg;  // R8 Revolver

    // Desert Eagle (1) inherits Heavy Pistol group while R8 (64) uses its own override.
    EXPECT_FLOAT_EQ(s.get().rage.resolve_weapon(1, hp_group).min_damage, 48.0f);
    EXPECT_FLOAT_EQ(s.get().rage.resolve_weapon(64, hp_group).min_damage, 82.0f);
    EXPECT_FLOAT_EQ(s.get().rage.resolve_weapon(64, hp_group).hitchance, 74.0f);
    EXPECT_TRUE(s.get().rage.resolve_weapon(64, hp_group).prefer_body_aim);

    store reloaded;
    ASSERT_TRUE(reloaded.from_json(s.to_json()));
    EXPECT_FLOAT_EQ(reloaded.get().rage.resolve_weapon(1, hp_group).min_damage, 48.0f);
    EXPECT_FLOAT_EQ(reloaded.get().rage.resolve_weapon(64, hp_group).min_damage, 82.0f);
    EXPECT_TRUE(reloaded.get().rage.resolve_weapon(64, hp_group).prefer_body_aim);

    // Clearing the override reverts cleanly to the base group after serialization.
    reloaded.get().rage.weapon_overrides.erase(64);
    store cleared;
    ASSERT_TRUE(cleared.from_json(reloaded.to_json()));
    EXPECT_FLOAT_EQ(cleared.get().rage.resolve_weapon(64, hp_group).min_damage, 48.0f);
}

TEST(config, anti_aim_bind_toggle_hold_and_unbound_behavior) {
    const std::array<bound_feature, 7> features = {
        bound_feature::rage,
        bound_feature::anti_aim,
        bound_feature::esp,
        bound_feature::third_person,
        bound_feature::bhop,
        bound_feature::auto_strafe,
        bound_feature::peek_assist,
    };
    EXPECT_LE(static_cast<unsigned>(bound_feature::count), 32u);
    std::set<std::uint32_t> bits;
    for (bound_feature f : features) {
        const std::uint32_t bit = feature_bit(f);
        EXPECT_NE(bit, 0u);
        EXPECT_EQ(bit & (bit - 1u), 0u);  // power of two
        EXPECT_TRUE(bits.insert(bit).second);
    }
    EXPECT_EQ(bits.size(), features.size());

    // Unbound (vk == 0) follows enabled regardless of input/focus state.
    bind_state state;
    const keybind unbound{0, false, false};
    EXPECT_FALSE(state.sample(unbound, false, true, true));
    EXPECT_FALSE(state.sample(unbound, false, false, false));
    EXPECT_TRUE(state.sample(unbound, true, true, false));
    EXPECT_TRUE(state.sample(unbound, true, true, true));
    EXPECT_TRUE(state.sample(unbound, true, false, false));

    // Toggle (vk = 'V', hold = false): pre-held guard, toggle edge, focus-loss preservation, master disable.
    const keybind toggle_kb{0x56, false, false};
    EXPECT_FALSE(state.sample(toggle_kb, true, true, true));   // pre-held ignored
    EXPECT_FALSE(state.sample(toggle_kb, true, true, false));  // armed
    EXPECT_TRUE(state.sample(toggle_kb, true, true, true));    // toggled ON
    EXPECT_TRUE(state.sample(toggle_kb, true, true, true));    // held still ON
    EXPECT_TRUE(state.sample(toggle_kb, true, true, false));   // released still ON
    EXPECT_TRUE(state.sample(toggle_kb, true, false, false));  // focus loss preserves ON
    EXPECT_TRUE(state.sample(toggle_kb, true, true, true));    // pre-held after focus loss ignored
    EXPECT_TRUE(state.sample(toggle_kb, true, true, false));   // re-armed
    EXPECT_FALSE(state.sample(toggle_kb, true, true, true));   // toggled OFF
    EXPECT_FALSE(state.sample(toggle_kb, true, true, false));  // stays OFF
    EXPECT_TRUE(state.sample(toggle_kb, true, true, true));    // toggled ON again
    EXPECT_FALSE(state.sample(toggle_kb, false, true, true));  // master disable clears toggle
    EXPECT_FALSE(state.sample(toggle_kb, true, true, false));  // stays OFF when re-enabled

    // Hold (vk = 'V', hold = true): pre-held guard, focus-loss disarm, press/release edge.
    const keybind hold_kb{0x56, true, false};
    EXPECT_FALSE(state.sample(hold_kb, true, true, true));   // pre-held ignored
    EXPECT_FALSE(state.sample(hold_kb, true, true, false));  // armed
    EXPECT_TRUE(state.sample(hold_kb, true, true, true));    // held ON
    EXPECT_FALSE(state.sample(hold_kb, true, false, true));  // focus loss disarms immediately
    EXPECT_FALSE(state.sample(hold_kb, true, true, true));   // requires release edge first
    EXPECT_FALSE(state.sample(hold_kb, true, true, false));  // released -> re-armed
    EXPECT_TRUE(state.sample(hold_kb, true, true, true));    // held ON
    EXPECT_FALSE(state.sample(hold_kb, true, true, false));  // released OFF
}

TEST(config, default_json_anti_aim_schema_roundtrips_cleanly) {
    const std::string default_aa_json = R"({
        "version": 1,
        "anti_aim": {
            "at_targets": true,
            "auto_adjust": true,
            "base_pitch": 1,
            "base_yaw": 4,
            "enabled": false,
            "freestanding": false,
            "jitter_range": 30.0,
            "on_air": true,
            "on_move": true,
            "on_shot": false,
            "toggle": {"hold": false, "vk": 0},
            "yaw_offset": 0.0
        }
    })";

    store s;
    ASSERT_TRUE(s.from_json(default_aa_json));
    EXPECT_EQ(s.clamps_applied(), 0);
    const auto& aa = s.get().anti_aim;
    EXPECT_FALSE(aa.enabled);
    EXPECT_TRUE(aa.at_targets);
    EXPECT_TRUE(aa.auto_adjust);
    EXPECT_FALSE(aa.freestanding);
    EXPECT_EQ(aa.base_pitch, pitch_mode::down);
    EXPECT_EQ(aa.base_yaw, yaw_mode::fake_static);
    EXPECT_FLOAT_EQ(aa.jitter_range, 30.0f);
    EXPECT_FLOAT_EQ(aa.yaw_offset, 0.0f);
    EXPECT_FALSE(aa.on_shot);
    EXPECT_TRUE(aa.on_air);
    EXPECT_TRUE(aa.on_move);
    EXPECT_EQ(aa.toggle.vk, 0);
    EXPECT_FALSE(aa.toggle.hold);

    s.get().anti_aim.toggle = {0x46, true, false};
    store reloaded;
    ASSERT_TRUE(reloaded.from_json(s.to_json()));
    EXPECT_EQ(reloaded.clamps_applied(), 0);
    const auto& aa2 = reloaded.get().anti_aim;
    EXPECT_EQ(aa2.toggle.vk, 0x46);
    EXPECT_TRUE(aa2.toggle.hold);
    EXPECT_EQ(aa2.base_pitch, pitch_mode::down);
    EXPECT_EQ(aa2.base_yaw, yaw_mode::fake_static);
    EXPECT_TRUE(aa2.at_targets);
    EXPECT_TRUE(aa2.auto_adjust);
    EXPECT_FALSE(aa2.freestanding);
    EXPECT_FLOAT_EQ(aa2.jitter_range, 30.0f);
    EXPECT_FLOAT_EQ(aa2.yaw_offset, 0.0f);
    EXPECT_FALSE(aa2.on_shot);
    EXPECT_TRUE(aa2.on_air);
    EXPECT_TRUE(aa2.on_move);
}
