#include "config/keybinds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace shogod::config {
namespace {

constexpr const char* k_pitch_choices[] = {"Off", "Down", "Up", "Zero"};
constexpr const char* k_yaw_choices[] = {"Off", "Jitter", "Random", "Static backward"};

// Tab indices mirror ui::tab:
// 0=rage, 1=antiaim, 2=players, 3=world, 4=misc, 5=configs, 6=keybinds
constexpr bind_target_spec k_catalog[] = {
    // ---- Ragebot ----
    {"rage.enabled", "Ragebot", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.silent_aim", "Silent aim", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.auto_revolver", "Auto revolver", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.backtrack", "Lag compensation", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.backtrack_ms", "Backtrack window", "Ragebot", 0, bind_value_kind::floating, 0, 200, 100},
    {"rage.extrapolation_ms", "Extrapolation", "Ragebot", 0, bind_value_kind::floating, 0, 50, 15},
    {"rage.fov", "Field of view", "Ragebot", 0, bind_value_kind::floating, 0, 180, 180},
    {"rage.min_damage", "Min damage", "Ragebot", 0, bind_value_kind::min_damage, 1, 120, 10},
    {"rage.hitchance", "Hitchance", "Ragebot", 0, bind_value_kind::floating, 0, 100, 50},
    {"rage.multipoint", "Multi-point", "Ragebot", 0, bind_value_kind::floating, 0, 100, 60},
    {"rage.auto_fire", "Auto fire", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.auto_scope", "Auto scope", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.auto_crouch", "Auto crouch", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.auto_stop", "Auto stop", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.penetration", "Wallbang", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.prefer_body_aim", "Prefer body aim", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},
    {"rage.force_body_aim", "Force body aim", "Ragebot", 0, bind_value_kind::boolean, 0, 1, 1},

    // ---- Anti-Aim ----
    {"aa.enabled", "Anti-Aim", "Anti-Aim", 1, bind_value_kind::boolean, 0, 1, 1},
    {"aa.at_targets", "At targets", "Anti-Aim", 1, bind_value_kind::boolean, 0, 1, 1},
    {"aa.auto_adjust", "Auto adjust", "Anti-Aim", 1, bind_value_kind::boolean, 0, 1, 1},
    {"aa.freestanding", "Auto direction", "Anti-Aim", 1, bind_value_kind::boolean, 0, 1, 1},
    {"aa.base_pitch", "Pitch", "Anti-Aim", 1, bind_value_kind::choice, 0, 3, 1, k_pitch_choices, 4},
    {"aa.base_yaw", "Yaw base", "Anti-Aim", 1, bind_value_kind::choice, 0, 3, 3, k_yaw_choices, 4},
    {"aa.yaw_offset", "Yaw offset", "Anti-Aim", 1, bind_value_kind::floating, -180, 180, -90},
    {"aa.jitter_range", "Jitter range", "Anti-Aim", 1, bind_value_kind::floating, 0, 180, 45},
    {"aa.on_shot", "Safe manual fire", "Anti-Aim", 1, bind_value_kind::boolean, 0, 1, 1},
    {"aa.on_air", "AA in air", "Anti-Aim", 1, bind_value_kind::boolean, 0, 1, 1},
    {"aa.on_move", "AA while moving", "Anti-Aim", 1, bind_value_kind::boolean, 0, 1, 1},

    // ---- Visuals: Players ----
    {"vis.enabled", "Player ESP", "Players", 2, bind_value_kind::boolean, 0, 1, 1},
    {"vis.visible_only", "Visible only", "Players", 2, bind_value_kind::boolean, 0, 1, 1},
    {"vis.skeleton", "Skeleton", "Players", 2, bind_value_kind::boolean, 0, 1, 1},
    {"vis.backtrack_ghost", "Backtrack ghost", "Players", 2, bind_value_kind::boolean, 0, 1, 1},
    {"vis.offscreen", "Off-screen arrows", "Players", 2, bind_value_kind::boolean, 0, 1, 1},

    // ---- Visuals: World & Camera ----
    {"vis.third_person", "Third person", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.third_person_distance", "Camera distance", "World", 3, bind_value_kind::integer, 48, 160, 110},
    {"vis.camera_fov", "Camera FOV", "World", 3, bind_value_kind::integer, 0, 135, 105},
    {"vis.fov_while_scoped", "FOV while scoped", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.no_flash", "No flash", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.no_smoke", "No smoke", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.no_scope", "No scope", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.remove_view_punch", "Remove view punch", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.penetration_crosshair", "Penetration crosshair", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.bomb_timer", "Bomb timer", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.world_modulate", "World modulation", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.light_modulate", "Light modulation", "World", 3, bind_value_kind::boolean, 0, 1, 1},
    {"vis.exposure", "Exposure %", "World", 3, bind_value_kind::floating, 0, 200, 35},

    // ---- Misc ----
    {"misc.bhop", "Bunny hop", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.edge_jump", "Edge jump", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.auto_strafe", "Auto strafe", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.peek_assist", "Peek assist", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.leg_movement", "Slide walk", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.auto_pistol", "Auto pistol", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.hitmarker", "Hitmarker", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.hitsound", "Hitsounds", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
    {"misc.safe_mode", "Safe mode", "Misc", 4, bind_value_kind::boolean, 0, 1, 1},
};

template <typename Fn>
void for_each_rage_weapon(rage_cfg& r, Fn&& fn) noexcept {
    for (auto& w : r.weapons) fn(w);
    for (auto& [_, w] : r.weapon_overrides) fn(w);
}

}  // namespace

std::span<const bind_target_spec> bind_target_catalog() noexcept {
    return std::span<const bind_target_spec>(k_catalog, sizeof(k_catalog) / sizeof(k_catalog[0]));
}

const bind_target_spec* find_bind_target(std::string_view id) noexcept {
    for (const auto& s : k_catalog) {
        if (id == s.id) return &s;
    }
    return nullptr;
}

std::string vk_name(int vk) {
    if (vk <= 0 || vk >= 255) return "None";
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x6F);
    if (vk >= 0x60 && vk <= 0x69) return "Num " + std::to_string(vk - 0x60);
    switch (vk) {
        case 0x01: return "Mouse 1";
        case 0x02: return "Mouse 2";
        case 0x04: return "Mouse 3";
        case 0x05: return "Mouse 4";
        case 0x06: return "Mouse 5";
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x10: return "Shift";
        case 0x11: return "Ctrl";
        case 0x12: return "Alt";
        case 0x14: return "Caps Lock";
        case 0x20: return "Space";
        case 0x21: return "Page Up";
        case 0x22: return "Page Down";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0xA0: return "LShift";
        case 0xA1: return "RShift";
        case 0xA2: return "LCtrl";
        case 0xA3: return "RCtrl";
        case 0xA4: return "LAlt";
        case 0xA5: return "RAlt";
        default: {
            char buf[16]{};
            std::snprintf(buf, sizeof buf, "VK %02X", vk);
            return buf;
        }
    }
}

std::string vk_short_name(int vk) {
    if (vk <= 0 || vk >= 255) return "-";
    switch (vk) {
        case 0x01: return "M1";
        case 0x02: return "M2";
        case 0x04: return "M3";
        case 0x05: return "M4";
        case 0x06: return "M5";
        case 0x10:
        case 0xA0: return "LSHT";
        case 0xA1: return "RSHT";
        case 0x11:
        case 0xA2: return "LCTL";
        case 0xA3: return "RCTL";
        case 0x12:
        case 0xA4: return "ALT";
        case 0xA5: return "RALT";
        case 0x14: return "CAPS";
        case 0x20: return "SPC";
        case 0x2D: return "INS";
        case 0x2E: return "DEL";
        default: return vk_name(vk);
    }
}

std::string format_bind_override(const bind_target_spec& spec, float value) {
    char buf[32]{};
    switch (spec.kind) {
        case bind_value_kind::boolean:
            return value >= 0.5f ? "on" : "off";
        case bind_value_kind::min_damage: {
            const int v = std::clamp(static_cast<int>(std::lround(value)), 1, 120);
            if (v > 100) {
                std::snprintf(buf, sizeof buf, "HP+%d", v - 100);
            } else {
                std::snprintf(buf, sizeof buf, "%d", v);
            }
            return buf;
        }
        case bind_value_kind::choice: {
            const int idx = std::clamp(static_cast<int>(std::lround(value)), 0,
                                       std::max(0, spec.choice_count - 1));
            return (spec.choices && idx < spec.choice_count) ? spec.choices[idx] : "?";
        }
        case bind_value_kind::integer:
            std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::lround(value)));
            return buf;
        case bind_value_kind::floating:
        default:
            std::snprintf(buf, sizeof buf, "%.0f", value);
            return buf;
    }
}

void apply_single_override(config_v1& cfg, std::string_view id, float value) noexcept {
    if (!std::isfinite(value)) return;
    const bool bval = value >= 0.5f;

    if (id == "rage.enabled") cfg.rage.enabled = bval;
    else if (id == "rage.silent_aim") cfg.rage.silent_aim = bval;
    else if (id == "rage.auto_revolver") cfg.rage.auto_revolver = bval;
    else if (id == "rage.backtrack") cfg.rage.backtrack = bval;
    else if (id == "rage.backtrack_ms") cfg.rage.backtrack_ms = std::clamp(value, 0.0f, 200.0f);
    else if (id == "rage.extrapolation_ms") cfg.rage.extrapolation_ms = std::clamp(value, 0.0f, 50.0f);
    else if (id == "rage.fov") {
        const float v = std::clamp(value, 0.0f, 180.0f);
        for_each_rage_weapon(cfg.rage, [v](rage_weapon& w) { w.fov = v; });
    } else if (id == "rage.min_damage") {
        const float v = std::clamp(value, 1.0f, 120.0f);
        for_each_rage_weapon(cfg.rage, [v](rage_weapon& w) { w.min_damage = v; });
    } else if (id == "rage.hitchance") {
        const float v = std::clamp(value, 0.0f, 100.0f);
        for_each_rage_weapon(cfg.rage, [v](rage_weapon& w) { w.hitchance = v; });
    } else if (id == "rage.multipoint") {
        const float v = std::clamp(value, 0.0f, 100.0f);
        for_each_rage_weapon(cfg.rage, [v](rage_weapon& w) { w.multipoint = v; });
    } else if (id == "rage.auto_fire") {
        for_each_rage_weapon(cfg.rage, [bval](rage_weapon& w) { w.auto_fire = bval; });
    } else if (id == "rage.auto_scope") {
        for_each_rage_weapon(cfg.rage, [bval](rage_weapon& w) { w.auto_scope = bval; });
    } else if (id == "rage.auto_crouch") {
        for_each_rage_weapon(cfg.rage, [bval](rage_weapon& w) { w.auto_crouch = bval; });
    } else if (id == "rage.auto_stop") {
        for_each_rage_weapon(cfg.rage, [bval](rage_weapon& w) { w.auto_stop = bval; });
    } else if (id == "rage.penetration") {
        for_each_rage_weapon(cfg.rage, [bval](rage_weapon& w) { w.penetration = bval; });
    } else if (id == "rage.prefer_body_aim") {
        for_each_rage_weapon(cfg.rage, [bval](rage_weapon& w) { w.prefer_body_aim = bval; });
    } else if (id == "rage.force_body_aim") {
        for_each_rage_weapon(cfg.rage, [bval](rage_weapon& w) { w.force_body_aim = bval; });
    } else if (id == "aa.enabled") cfg.anti_aim.enabled = bval;
    else if (id == "aa.at_targets") cfg.anti_aim.at_targets = bval;
    else if (id == "aa.auto_adjust") cfg.anti_aim.auto_adjust = bval;
    else if (id == "aa.freestanding") cfg.anti_aim.freestanding = bval;
    else if (id == "aa.base_pitch") {
        cfg.anti_aim.base_pitch = static_cast<pitch_mode>(std::clamp(static_cast<int>(std::lround(value)), 0, 3));
    } else if (id == "aa.base_yaw") {
        constexpr yaw_mode k_map[4] = {yaw_mode::off, yaw_mode::jitter, yaw_mode::random, yaw_mode::fake_static};
        cfg.anti_aim.base_yaw = k_map[std::clamp(static_cast<int>(std::lround(value)), 0, 3)];
    } else if (id == "aa.yaw_offset") cfg.anti_aim.yaw_offset = std::clamp(value, -180.0f, 180.0f);
    else if (id == "aa.jitter_range") cfg.anti_aim.jitter_range = std::clamp(value, 0.0f, 180.0f);
    else if (id == "aa.on_shot") cfg.anti_aim.on_shot = bval;
    else if (id == "aa.on_air") cfg.anti_aim.on_air = bval;
    else if (id == "aa.on_move") cfg.anti_aim.on_move = bval;
    else if (id == "vis.enabled") cfg.visuals.enabled = bval;
    else if (id == "vis.visible_only") cfg.visuals.visible_only = bval;
    else if (id == "vis.skeleton") cfg.visuals.skeleton = bval;
    else if (id == "vis.backtrack_ghost") cfg.visuals.backtrack_ghost = bval;
    else if (id == "vis.offscreen") cfg.visuals.offscreen = bval;
    else if (id == "vis.third_person") cfg.visuals.third_person = bval;
    else if (id == "vis.third_person_distance") {
        cfg.visuals.third_person_distance = std::clamp(static_cast<int>(std::lround(value)), 48, 160);
    } else if (id == "vis.camera_fov") {
        int fov = std::clamp(static_cast<int>(std::lround(value)), 0, 135);
        if (fov > 0 && fov < 50) fov = 50;
        cfg.visuals.camera_fov = fov;
    } else if (id == "vis.fov_while_scoped") cfg.visuals.fov_while_scoped = bval;
    else if (id == "vis.no_flash") cfg.visuals.no_flash = bval;
    else if (id == "vis.no_smoke") cfg.visuals.no_smoke = bval;
    else if (id == "vis.no_scope") cfg.visuals.no_scope = bval;
    else if (id == "vis.remove_view_punch") cfg.visuals.remove_view_punch = bval;
    else if (id == "vis.penetration_crosshair") cfg.visuals.penetration_crosshair = bval;
    else if (id == "vis.bomb_timer") cfg.visuals.bomb_timer = bval;
    else if (id == "vis.world_modulate") cfg.visuals.world_modulate = bval;
    else if (id == "vis.light_modulate") cfg.visuals.light_modulate = bval;
    else if (id == "vis.exposure") cfg.visuals.exposure = std::clamp(value, 0.0f, 200.0f);
    else if (id == "misc.bhop") cfg.misc.movement.bhop = bval;
    else if (id == "misc.edge_jump") cfg.misc.movement.edge_jump = bval;
    else if (id == "misc.auto_strafe") cfg.misc.movement.auto_strafe = bval;
    else if (id == "misc.peek_assist") cfg.misc.movement.peek_assist = bval;
    else if (id == "misc.leg_movement") cfg.misc.movement.leg_movement = bval;
    else if (id == "misc.auto_pistol") cfg.misc.utilities.auto_pistol = bval;
    else if (id == "misc.hitmarker") cfg.misc.info.hitmarker = bval;
    else if (id == "misc.hitsound") cfg.misc.hitsound.enabled = bval;
    else if (id == "misc.safe_mode") cfg.misc.block_server_detected = bval;
}

void keybind_runtime::apply_overrides(config_v1& effective, const std::vector<menu_keybind>& binds,
                                      std::vector<active_bind_display>* out_hud) const {
    if (out_hud) out_hud->clear();
    const std::size_t n = std::min(binds.size(), states_.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (!states_[i].active) continue;
        const menu_keybind& b = binds[i];
        const bind_target_spec* spec = find_bind_target(b.target);
        if (!spec) continue;
        apply_single_override(effective, b.target, b.override_value);
        if (out_hud && b.show_in_binds) {
            out_hud->push_back(active_bind_display{
                spec->label, format_bind_override(*spec, b.override_value), b.mode});
        }
    }
}

}  // namespace shogod::config
