#include "ui/pages/pages.hpp"

#include "ui/style.hpp"
#include "ui/widgets.hpp"

namespace shogod::ui::pages {
namespace {

using namespace widgets;

const char* const k_groups[] = {"Pistol", "SMG", "Rifle", "Sniper", "Shotgun", "Heavy"};
const char* const k_priorities[] = {"Distance", "Lowest HP", "Threat", "Cycle"};
const char* const k_yaw_modes[] = {"Off", "Desync", "Jitter", "Random", "Fake static"};
const char* const k_pitch_modes[] = {"Off", "Down", "Up", "Zero"};

const char* const k_point_names[] = {"Head", "Neck", "Chest", "Stomach", "Pelvis", "Legs"};
const std::uint32_t k_point_masks[] = {
    config::aim_point::head,    config::aim_point::neck,   config::aim_point::chest,
    config::aim_point::stomach, config::aim_point::pelvis, config::aim_point::legs,
};

}  // namespace

void rage(config::store& cfg, const status& /*live*/) {
    using namespace config;
    config_v1& c = cfg.get();

    static int group = 2;  // rifle default
    rage_weapon& w = c.rage.weapons[static_cast<std::size_t>(group)];

    if (!begin_columns("##rage_cols")) return;

    ImGui::TableNextColumn();
    if (begin_card("##target", ImVec2(0, 0))) {
        section("TARGET");
        toggle("Enabled", &c.rage.enabled, "Master switch for the ragebot");
        combo("Priority", reinterpret_cast<int*>(&c.rage.priority), k_priorities, 4);
        combo("Weapon group", &group, k_groups, k_weapon_groups);
        separator_y();

        section("AIM");
        slider_float("Field of view", &w.fov, 0.0f, 180.0f, "Maximum angular distance to a target");
        slider_float("Min damage", &w.min_damage, 0.0f, 150.0f);
        slider_float("Hitchance", &w.hitchance, 0.0f, 100.0f,
                     "Probability required before the shot is fired");
        multi_bits("Aim points", &w.aim_points, k_point_names, k_point_masks, 6);
        slider_float("Multi-point", &w.multipoint, 0.0f, 100.0f,
                     "Hull scale used to spread aim points");
        slider_float("Safe point", &w.safe_point, 0.0f, 100.0f,
                     "100 aims dead centre, 0 aims the hull edge");
        separator_y();

        section("FIRE");
        toggle("Auto fire", &w.auto_fire);
        toggle("Auto scope", &w.auto_scope);
        toggle("Auto stop", &w.auto_stop);
        toggle("Auto crouch", &w.auto_crouch);
        toggle("Penetration", &w.penetration, "Punch through walls where damage survives");
        if (w.penetration) slider_float("Penetration limit", &w.penetration_limit, 0.0f, 3.0f);
        slider_int("Shot delay (ticks)", &w.delay_ticks, 0, 16);
        toggle("Ignore limbs", &w.ignore_limbs);
    }
    end_card();

    ImGui::TableNextColumn();
    if (begin_card("##aa", ImVec2(0, 0))) {
        section("ANTIAIM");
        toggle("Enabled", &c.anti_aim.enabled);
        combo("Base yaw", reinterpret_cast<int*>(&c.anti_aim.base_yaw), k_yaw_modes, 5);
        if (c.anti_aim.base_yaw == yaw_mode::jitter)
            slider_float("Jitter range", &c.anti_aim.jitter_range, 0.0f, 180.0f);
        if (c.anti_aim.base_yaw == yaw_mode::desync)
            slider_float("Desync angle", &c.anti_aim.desync_angle, 0.0f, 60.0f);
        combo("Base pitch", reinterpret_cast<int*>(&c.anti_aim.base_pitch), k_pitch_modes, 4);
        separator_y();
        section("CONDITIONALS");
        toggle("On shot", &c.anti_aim.on_shot);
        toggle("In air", &c.anti_aim.on_air);
        toggle("While moving", &c.anti_aim.on_move);
        toggle("Fake walk", &c.anti_aim.fake_walk);
    }
    end_card();

    ImGui::Dummy(ImVec2(0, 8));
    if (begin_card("##resolver", ImVec2(0, 0))) {
        section("RESOLVER");
        toggle("Enabled", &c.resolver.enabled, "Angle heuristics driven by per-target history");
        toggle("Debug log", &c.resolver.debug_log, "Logs the heuristic that fired per target");
    }
    end_card();

    ImGui::Dummy(ImVec2(0, 8));
    if (begin_card("##bt", ImVec2(0, 0))) {
        section("BACKTRACK");
        toggle("Enabled", &c.rage.backtrack, "Re-aim at validated historical positions");
        if (c.rage.backtrack) {
            slider_float("Window (ms)", &c.rage.backtrack_ms, 0.0f, 400.0f);
            toggle("Ghost", &c.rage.backtrack_visualise);
        }
        slider_int("Fake lag (ticks)", &c.rage.fake_lag_ticks, 0, 16);
    }
    end_card();

    end_columns();
}

}  // namespace shogod::ui::pages
