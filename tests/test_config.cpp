#include <gtest/gtest.h>

#include <filesystem>

#include "config/config.hpp"

using namespace shogod::config;

TEST(config, defaults_are_sane) {
    store s;
    EXPECT_EQ(s.get().version, k_schema_version);
    EXPECT_FALSE(s.get().rage.enabled);
    EXPECT_GT(s.get().rage.weapons[2].fov, 0.0f);
    EXPECT_FLOAT_EQ(s.get().ui.opacity, 0.97f);
}

TEST(config, json_roundtrip_preserves_values) {
    store a;
    a.get().rage.enabled = true;
    a.get().rage.weapons[2].hitchance = 71.5f;
    a.get().visuals.player_chams.enabled = true;
    a.get().visuals.player_chams.visible = {0.1f, 0.2f, 0.3f, 0.4f};
    a.get().ui.accent = {0.5f, 0.6f, 0.7f, 1.0f};

    const std::string text = a.to_json();

    store b;
    ASSERT_TRUE(b.from_json(text));
    EXPECT_TRUE(b.get().rage.enabled);
    EXPECT_FLOAT_EQ(b.get().rage.weapons[2].hitchance, 71.5f);
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

TEST(config, out_of_range_values_are_clamped_and_counted) {
    const std::string in = R"({
        "version": 1,
        "rage": {"weapons": [
            {"hitchance": 500.0, "fov": -20.0, "delay_ticks": 99},
            {}, {}, {}, {}, {}
        ]},
        "visuals": {"esp_alpha": 900}
    })";

    store s;
    ASSERT_TRUE(s.from_json(in));
    EXPECT_FLOAT_EQ(s.get().rage.weapons[0].hitchance, 100.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[0].fov, 0.0f);
    EXPECT_EQ(s.get().rage.weapons[0].delay_ticks, 16);
    EXPECT_EQ(s.get().visuals.esp_alpha, 255);
    EXPECT_GE(s.clamps_applied(), 4);
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

    std::filesystem::remove_all(dir);
}

TEST(config, profiles_differ) {
    config_v1 safe;
    apply_safe_profile(safe);
    config_v1 rage;
    apply_rage_profile(rage);
    EXPECT_GT(rage.rage.weapons[2].hitchance, safe.rage.weapons[2].hitchance);
    EXPECT_LT(rage.rage.weapons[2].safe_point, safe.rage.weapons[2].safe_point);
}
