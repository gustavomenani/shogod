#include "config/config.hpp"

#include "brand.hpp"
#include "engine/log.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#include "json.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>

namespace shogod::config {

using json = nlohmann::json;

struct store::impl {
    json doc = json::object();
};

namespace {

template <typename T>
T get_or(const json& obj, const char* key, T fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        return fallback;
    }
}

rgba get_rgba(const json& obj, const char* key, rgba fallback) {
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_object()) return fallback;
    const json& c = obj[key];
    return rgba{
        get_or<float>(c, "r", fallback.r),
        get_or<float>(c, "g", fallback.g),
        get_or<float>(c, "b", fallback.b),
        get_or<float>(c, "a", fallback.a),
    };
}

json to_rgba(const rgba& c) {
    return json{{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
}

json to_keybind(const keybind& kb) {
    return json{{"vk", kb.vk}, {"hold", kb.hold}};
}

keybind get_keybind(const json& obj, const char* key, keybind fallback) {
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_object()) return fallback;
    const json& k = obj[key];
    const int key_code = get_or<int>(k, "vk", fallback.vk);
    fallback.vk = key_code >= 1 && key_code <= 254 ? key_code : 0;
    fallback.hold = get_or<bool>(k, "hold", fallback.hold);
    return fallback;
}

float clampf(const char* name, float v, float lo, float hi, int& counter) {
    if (!std::isfinite(v)) v = lo;
    const float out = std::clamp(v, lo, hi);
    if (out != v) {
        ++counter;
        log::warn("config: {} = {} clamped to {}", name, v, out);
    }
    return out;
}

int clampi(const char* name, int v, int lo, int hi, int& counter) {
    const int out = std::clamp(v, lo, hi);
    if (out != v) {
        ++counter;
        log::warn("config: {} = {} clamped to {}", name, v, out);
    }
    return out;
}

}  // namespace

namespace {

constexpr rage_weapon_spec k_weapon_catalog[] = {
    {61, weapon_group::pistol, "usp_silencer", "USP-S", "USP-S", 0},
    {4, weapon_group::pistol, "glock", "Glock-18", "Glock", 1},
    {32, weapon_group::pistol, "hkp2000", "P2000", "P2000", 2},
    {2, weapon_group::pistol, "elite", "Dual Berettas", "Dualies", 3},
    {36, weapon_group::pistol, "p250", "P250", "P250", 4},
    {3, weapon_group::pistol, "fiveseven", "Five-SeveN", "Five-SeveN", 5},
    {30, weapon_group::pistol, "tec9", "Tec-9", "Tec-9", 6},
    {63, weapon_group::pistol, "cz75a", "CZ75-Auto", "CZ75", 7},
    {1, weapon_group::heavy_pistol, "deagle", "Desert Eagle", "Deagle", 8},
    {64, weapon_group::heavy_pistol, "revolver", "R8 Revolver", "R8", 9},
    {31, weapon_group::pistol, "taser", "Zeus x27", "Zeus", 10},
    {34, weapon_group::smg, "mp9", "MP9", "MP9", 11},
    {17, weapon_group::smg, "mac10", "MAC-10", "MAC-10", 12},
    {33, weapon_group::smg, "mp7", "MP7", "MP7", 13},
    {23, weapon_group::smg, "mp5sd", "MP5-SD", "MP5-SD", 14},
    {24, weapon_group::smg, "ump45", "UMP-45", "UMP-45", 15},
    {19, weapon_group::smg, "p90", "P90", "P90", 16},
    {26, weapon_group::smg, "bizon", "PP-Bizon", "Bizon", 17},
    {7, weapon_group::rifle, "ak47", "AK-47", "AK-47", 18},
    {16, weapon_group::rifle, "m4a1", "M4A4", "M4A4", 19},
    {60, weapon_group::rifle, "m4a1_silencer", "M4A1-S", "M4A1-S", 20},
    {13, weapon_group::rifle, "galilar", "Galil AR", "Galil", 21},
    {10, weapon_group::rifle, "famas", "FAMAS", "FAMAS", 22},
    {8, weapon_group::rifle, "aug", "AUG", "AUG", 23},
    {39, weapon_group::rifle, "sg556", "SG 553", "SG 553", 24},
    {40, weapon_group::scout, "ssg08", "SSG 08", "Scout", 25},
    {38, weapon_group::auto_sniper, "scar20", "SCAR-20", "SCAR-20", 26},
    {11, weapon_group::auto_sniper, "g3sg1", "G3SG1", "G3SG1", 27},
    {9, weapon_group::awp, "awp", "AWP", "AWP", 28},
    {25, weapon_group::heavy, "xm1014", "XM1014", "XM1014", 29},
    {27, weapon_group::heavy, "mag7", "MAG-7", "MAG-7", 30},
    {35, weapon_group::heavy, "nova", "Nova", "Nova", 31},
    {29, weapon_group::heavy, "sawedoff", "Sawed-Off", "Sawed-Off", 32},
    {14, weapon_group::heavy, "m249", "M249", "M249", 33},
    {28, weapon_group::heavy, "negev", "Negev", "Negev", 34},
};

}  // namespace

const rage_weapon_spec* rage_weapon_catalog(std::size_t& out_count) noexcept {
    out_count = std::size(k_weapon_catalog);
    return k_weapon_catalog;
}

const rage_weapon_spec* find_rage_weapon_by_def(int def_index) noexcept {
    for (const rage_weapon_spec& s : k_weapon_catalog) {
        if (s.def_index == def_index) return &s;
    }
    return nullptr;
}

const rage_weapon_spec* find_rage_weapon_by_key(const char* key) noexcept {
    if (!key) return nullptr;
    for (const rage_weapon_spec& s : k_weapon_catalog) {
        if (std::strcmp(s.key, key) == 0) return &s;
    }
    return nullptr;
}

const rage_weapon& rage_cfg::resolve_weapon(int def_index, int group_idx) const noexcept {
    if (def_index > 0) {
        const auto it = weapon_overrides.find(def_index);
        if (it != weapon_overrides.end()) return it->second;
    }
    const int safe_group = std::clamp(group_idx, 0, k_weapon_groups - 1);
    return weapons[static_cast<std::size_t>(safe_group)];
}

void apply_safe_profile(config_v1& cfg) {
    cfg.rage.weapon_overrides.clear();
    struct preset {
        float fov, min_damage, hitchance, multipoint, safe_point;
    };
    const preset table[k_weapon_groups] = {
        {90.0f, 20.0f, 50.0f, 30.0f, 70.0f},   // pistol
        {120.0f, 45.0f, 65.0f, 35.0f, 65.0f},  // heavy_pistol
        {120.0f, 20.0f, 50.0f, 35.0f, 60.0f},  // smg
        {180.0f, 28.0f, 60.0f, 40.0f, 50.0f},  // rifle
        {180.0f, 70.0f, 68.0f, 35.0f, 55.0f},  // scout
        {180.0f, 30.0f, 55.0f, 45.0f, 50.0f},  // auto_sniper
        {180.0f, 100.0f, 78.0f, 30.0f, 60.0f}, // awp
        {120.0f, 20.0f, 45.0f, 25.0f, 75.0f},  // heavy
    };
    for (int i = 0; i < k_weapon_groups; ++i) {
        rage_weapon& w = cfg.rage.weapons[static_cast<std::size_t>(i)];
        w.fov = table[i].fov;
        w.min_damage = table[i].min_damage;
        w.hitchance = table[i].hitchance;
        w.multipoint = table[i].multipoint;
        w.safe_point = table[i].safe_point;
    }
}

void apply_rage_profile(config_v1& cfg) {
    apply_safe_profile(cfg);
    cfg.rage.silent_aim = true;
    cfg.rage.remove_recoil = true;
    cfg.rage.auto_revolver = true;
    cfg.rage.backtrack = true;
    cfg.rage.backtrack_ms = 200.0f;

    constexpr std::uint32_t k_hv_points = aim_point::head | aim_point::neck | aim_point::chest |
                                          aim_point::stomach | aim_point::pelvis;
    struct rage_preset {
        float min_dmg, hc, mp, sp;
        std::uint32_t stop_flags;
        std::uint32_t points;
        bool prefer_body;
    };
    const rage_preset table[k_weapon_groups] = {
        {20.0f, 55.0f, 45.0f, 40.0f, auto_stop_flag::early | auto_stop_flag::between_shots, k_hv_points, false},  // pistol
        {50.0f, 70.0f, 55.0f, 40.0f, auto_stop_flag::all, k_hv_points, false},                                    // heavy_pistol
        {20.0f, 55.0f, 45.0f, 35.0f, auto_stop_flag::early | auto_stop_flag::between_shots, k_hv_points, false},  // smg
        {28.0f, 65.0f, 55.0f, 35.0f, auto_stop_flag::early | auto_stop_flag::between_shots, k_hv_points, false},  // rifle
        {75.0f, 72.0f, 65.0f, 35.0f, auto_stop_flag::all, aim_point::head | aim_point::stomach | aim_point::pelvis, false}, // scout
        {30.0f, 60.0f, 70.0f, 30.0f, auto_stop_flag::early | auto_stop_flag::between_shots, k_hv_points, true},   // auto_sniper
        {100.0f, 82.0f, 50.0f, 45.0f, auto_stop_flag::early, aim_point::stomach | aim_point::chest | aim_point::pelvis | aim_point::head, true}, // awp
        {25.0f, 50.0f, 40.0f, 40.0f, auto_stop_flag::early | auto_stop_flag::between_shots, k_hv_points, true},   // heavy
    };
    for (int i = 0; i < k_weapon_groups; ++i) {
        rage_weapon& w = cfg.rage.weapons[static_cast<std::size_t>(i)];
        w.fov = 180.0f;
        w.min_damage = table[i].min_dmg;
        w.hitchance = table[i].hc;
        w.multipoint = table[i].mp;
        w.safe_point = table[i].sp;
        w.auto_fire = true;
        w.auto_scope = true;
        w.auto_stop = true;
        w.auto_stop_flags = table[i].stop_flags;
        w.aim_points = table[i].points;
        w.prefer_body_aim = table[i].prefer_body;
        w.penetration = true;
    }
    cfg.anti_aim.enabled = true;
    cfg.anti_aim.base_pitch = pitch_mode::down;
    cfg.anti_aim.base_yaw = yaw_mode::fake_static;
    cfg.anti_aim.at_targets = true;
    cfg.anti_aim.auto_adjust = true;
    cfg.anti_aim.freestanding = true;
    cfg.anti_aim.yaw_offset = 0.0f;
    cfg.anti_aim.on_shot = true;
    cfg.anti_aim.on_air = true;
    cfg.anti_aim.on_move = true;
}

namespace {

json rage_weapon_to_json(const rage_weapon& w) {
    return json{
        {"fov", w.fov},
        {"min_damage", w.min_damage},
        {"hitchance", w.hitchance},
        {"aim_points", w.aim_points},
        {"multipoint", w.multipoint},
        {"safe_point", w.safe_point},
        {"auto_fire", w.auto_fire},
        {"auto_scope", w.auto_scope},
        {"auto_stop", w.auto_stop},
        {"auto_stop_flags", w.auto_stop_flags},
        {"auto_stop_speed", w.auto_stop_speed},
        {"auto_stop_ticks", w.auto_stop_ticks},
        {"auto_stop_wait", w.auto_stop_wait},
        {"auto_crouch", w.auto_crouch},
        {"penetration", w.penetration},
        {"delay_shot", static_cast<int>(w.delay_shot)},
        {"delay_ticks", w.delay_ticks},
        {"ignore_limbs", w.ignore_limbs},
        {"force_body_aim", w.force_body_aim},
        {"prefer_body_aim", w.prefer_body_aim},
        {"strict_hitchance", w.strict_hitchance},
    };
}

void rage_weapon_from_json(const json& j, rage_weapon& w) {
    if (!j.is_object()) return;
    w.fov = get_or<float>(j, "fov", w.fov);
    w.min_damage = get_or<float>(j, "min_damage", w.min_damage);
    w.hitchance = get_or<float>(j, "hitchance", w.hitchance);
    w.aim_points = get_or<std::uint32_t>(j, "aim_points", w.aim_points);
    w.multipoint = get_or<float>(j, "multipoint", w.multipoint);
    w.safe_point = get_or<float>(j, "safe_point", w.safe_point);
    w.auto_fire = get_or<bool>(j, "auto_fire", w.auto_fire);
    w.auto_scope = get_or<bool>(j, "auto_scope", w.auto_scope);
    w.auto_stop = get_or<bool>(j, "auto_stop", w.auto_stop);
    w.auto_stop_flags =
        get_or<std::uint32_t>(j, "auto_stop_flags", w.auto_stop_flags) & auto_stop_flag::all;
    w.auto_stop_speed = get_or<float>(j, "auto_stop_speed", w.auto_stop_speed);
    w.auto_stop_ticks = get_or<int>(j, "auto_stop_ticks", w.auto_stop_ticks);
    w.auto_stop_wait = get_or<bool>(j, "auto_stop_wait", w.auto_stop_wait);
    w.auto_crouch = get_or<bool>(j, "auto_crouch", w.auto_crouch);
    w.penetration = get_or<bool>(j, "penetration", w.penetration);
    const int legacy_ticks = get_or<int>(j, "delay_ticks", 0);
    const auto legacy_mode = legacy_ticks > 0 ? delay_shot_mode::both : delay_shot_mode::off;
    w.delay_shot = static_cast<delay_shot_mode>(
        get_or<int>(j, "delay_shot", static_cast<int>(legacy_mode)));
    w.delay_ticks = legacy_ticks == 0 ? w.delay_ticks : legacy_ticks;
    w.ignore_limbs = get_or<bool>(j, "ignore_limbs", w.ignore_limbs);
    w.force_body_aim = get_or<bool>(j, "force_body_aim", w.force_body_aim);
    w.prefer_body_aim = get_or<bool>(j, "prefer_body_aim", w.prefer_body_aim);
    w.strict_hitchance = get_or<bool>(j, "strict_hitchance", w.strict_hitchance);
}

json weapons_to_json(const rage_cfg& r) {
    json arr = json::array();
    for (const rage_weapon& w : r.weapons) arr.push_back(rage_weapon_to_json(w));
    return arr;
}

void weapons_from_json(const json& arr, rage_cfg& r) {
    if (!arr.is_array()) return;

    if (arr.size() == 6) {
        // Legacy 6-group schema migration: [Pistol, SMG, Rifle, Sniper, Shotgun, Heavy]
        rage_weapon_from_json(arr[0], r.weapons[0]);
        rage_weapon_from_json(arr[0], r.weapons[1]);
        rage_weapon_from_json(arr[1], r.weapons[2]);
        rage_weapon_from_json(arr[2], r.weapons[3]);
        rage_weapon_from_json(arr[3], r.weapons[4]);
        rage_weapon_from_json(arr[3], r.weapons[5]);
        rage_weapon_from_json(arr[3], r.weapons[6]);
        rage_weapon_from_json(arr[5], r.weapons[7]);
        return;
    }

    if (arr.size() != static_cast<std::size_t>(k_weapon_groups)) return;
    for (int i = 0; i < k_weapon_groups; ++i) {
        rage_weapon_from_json(arr[static_cast<std::size_t>(i)], r.weapons[static_cast<std::size_t>(i)]);
    }
}

void read_rage(const json& obj, rage_cfg& r, int& clamps) {
    r.enabled = get_or<bool>(obj, "enabled", r.enabled);
    r.toggle = get_keybind(obj, "toggle", r.toggle);
    r.priority = static_cast<aim_priority>(
        clampi("rage.priority", get_or<int>(obj, "priority", static_cast<int>(r.priority)), 0, 3,
               clamps));
    r.silent_aim = get_or<bool>(obj, "silent_aim", r.silent_aim);
    r.remove_recoil = get_or<bool>(obj, "remove_recoil", r.remove_recoil);
    r.auto_revolver = get_or<bool>(obj, "auto_revolver", r.auto_revolver);
    r.backtrack = get_or<bool>(obj, "backtrack", r.backtrack);
    r.backtrack_ms = get_or<float>(obj, "backtrack_ms", r.backtrack_ms);
    r.extrapolation_ms = get_or<float>(obj, "extrapolation_ms", r.extrapolation_ms);
    if (obj.contains("weapons")) weapons_from_json(obj["weapons"], r);
    if (obj.contains("weapon_overrides") && obj["weapon_overrides"].is_object()) {
        for (auto it = obj["weapon_overrides"].begin(); it != obj["weapon_overrides"].end(); ++it) {
            const rage_weapon_spec* spec = find_rage_weapon_by_key(it.key().c_str());
            if (!spec || !it.value().is_object()) continue;
            rage_weapon w = r.weapons[static_cast<std::size_t>(spec->group)];
            rage_weapon_from_json(it.value(), w);
            r.weapon_overrides[spec->def_index] = w;
        }
    }
}

void write_rage(json& obj, const rage_cfg& r) {
    obj["enabled"] = r.enabled;
    obj["toggle"] = to_keybind(r.toggle);
    obj["priority"] = static_cast<int>(r.priority);
    obj["silent_aim"] = r.silent_aim;
    obj["remove_recoil"] = r.remove_recoil;
    obj["auto_revolver"] = r.auto_revolver;
    obj["backtrack"] = r.backtrack;
    obj["backtrack_ms"] = r.backtrack_ms;
    obj["extrapolation_ms"] = r.extrapolation_ms;
    obj["weapons"] = weapons_to_json(r);
    json ov = json::object();
    for (const auto& [def_idx, w] : r.weapon_overrides) {
        if (const rage_weapon_spec* spec = find_rage_weapon_by_def(def_idx)) {
            ov[spec->key] = rage_weapon_to_json(w);
        }
    }
    obj["weapon_overrides"] = std::move(ov);
}

json cham_to_json(const cham_material& m) {
    return json{
        {"enabled", m.enabled},
        {"visible", to_rgba(m.visible)},
        {"invisible", to_rgba(m.invisible)},
        {"flags", m.flags},
        {"wire_thickness", m.wire_thickness},
        {"material", m.material},
        {"fresnel_exponent", m.fresnel_exponent},
        {"fresnel_falloff", m.fresnel_falloff},
    };
}

void read_cham(const json& obj, const char* key, cham_material& m, int& clamps) {
    if (!obj.contains(key) || !obj[key].is_object()) return;
    const json& j = obj[key];
    m.enabled = get_or<bool>(j, "enabled", m.enabled);
    m.visible = get_rgba(j, "visible", m.visible);
    m.invisible = get_rgba(j, "invisible", m.invisible);
    m.flags = get_or<int>(j, "flags", m.flags);
    m.wire_thickness = get_or<float>(j, "wire_thickness", m.wire_thickness);
    m.material = clampi("visuals.cham.material", get_or<int>(j, "material", m.material), 0, 3, clamps);
    m.fresnel_exponent = get_or<float>(j, "fresnel_exponent", m.fresnel_exponent);
    m.fresnel_falloff = get_or<float>(j, "fresnel_falloff", m.fresnel_falloff);
}

json visuals_to_json(const visuals_cfg& v) {
    json layout_arr = json::array();
    for (const auto& slot : v.esp_layout) {
        layout_arr.push_back(json{{"side", slot.side}, {"order", slot.order}});
    }
    return json{
        {"enabled", v.enabled},
        {"toggle", to_keybind(v.toggle)},
        {"box", static_cast<int>(v.box)},
        {"box_color", to_rgba(v.box_color)},
        {"health_bar", v.health_bar},
        {"armor_bar", v.armor_bar},
        {"name", v.name},
        {"weapon", v.weapon},
        {"weapon_icon", v.weapon_icon},
        {"ammo", v.ammo},
        {"flags", v.flags},
        {"distance", v.distance},
        {"skeleton", v.skeleton},
        {"backtrack_ghost", v.backtrack_ghost},
        {"visible_only", v.visible_only},
        {"offscreen", v.offscreen},
        {"flag_scoped", v.flag_scoped},
        {"flag_defuse", v.flag_defuse},
        {"flag_flash", v.flag_flash},
        {"flag_kit", v.flag_kit},
        {"flag_reload", v.flag_reload},
        {"flag_armor", v.flag_armor},
        {"flag_money", v.flag_money},
        {"flag_ping", v.flag_ping},
        {"name_color", to_rgba(v.name_color)},
        {"weapon_color", to_rgba(v.weapon_color)},
        {"weapon_icon_color", to_rgba(v.weapon_icon_color)},
        {"ammo_color", to_rgba(v.ammo_color)},
        {"distance_color", to_rgba(v.distance_color)},
        {"skeleton_color", to_rgba(v.skeleton_color)},
        {"health_bar_color", to_rgba(v.health_bar_color)},
        {"health_bar_grad_color", to_rgba(v.health_bar_grad_color)},
        {"health_bar_gradient", v.health_bar_gradient},
        {"health_bar_glow", v.health_bar_glow},
        {"armor_bar_color", to_rgba(v.armor_bar_color)},
        {"armor_bar_glow", v.armor_bar_glow},
        {"esp_layout", std::move(layout_arr)},
        {"no_flash", v.no_flash},
        {"no_smoke", v.no_smoke},
        {"no_scope", v.no_scope},
        {"remove_view_punch", v.remove_view_punch},
        {"penetration_crosshair", v.penetration_crosshair},
        {"third_person", v.third_person},
        {"third_person_bind", to_keybind(v.third_person_bind)},
        {"third_person_distance", v.third_person_distance},
        {"camera_fov", v.camera_fov},
        {"fov_while_scoped", v.fov_while_scoped},
        {"bomb_timer", v.bomb_timer},
        {"esp_alpha", v.esp_alpha},
        {"player_chams", cham_to_json(v.player_chams)},
        {"team_chams", cham_to_json(v.team_chams)},
        {"local_chams", cham_to_json(v.local_chams)},
        {"arms_chams", cham_to_json(v.arms_chams)},
        {"weapon_chams", cham_to_json(v.weapon_chams)},
        {"scope_blend", v.scope_blend},
        {"scope_blend_alpha", v.scope_blend_alpha},
        {"world_modulate", v.world_modulate},
        {"world_color", to_rgba(v.world_color)},
        {"light_modulate", v.light_modulate},
        {"light_color", to_rgba(v.light_color)},
        {"exposure", v.exposure},
    };
}

void read_visuals(const json& obj, visuals_cfg& v, int& clamps) {
    v.enabled = get_or<bool>(obj, "enabled", v.enabled);
    v.toggle = get_keybind(obj, "toggle", v.toggle);
    v.box = static_cast<box_style>(clampi("visuals.box", get_or<int>(obj, "box", static_cast<int>(v.box)),
                                          0, 3, clamps));
    v.box_color = get_rgba(obj, "box_color", v.box_color);
    v.health_bar = get_or<bool>(obj, "health_bar", v.health_bar);
    v.armor_bar = get_or<bool>(obj, "armor_bar", v.armor_bar);
    v.name = get_or<bool>(obj, "name", v.name);
    v.weapon = get_or<bool>(obj, "weapon", v.weapon);
    v.weapon_icon = get_or<bool>(obj, "weapon_icon", v.weapon_icon);
    v.ammo = get_or<bool>(obj, "ammo", v.ammo);
    v.flags = get_or<bool>(obj, "flags", v.flags);
    v.distance = get_or<bool>(obj, "distance", v.distance);
    v.skeleton = get_or<bool>(obj, "skeleton", v.skeleton);
    v.backtrack_ghost = get_or<bool>(obj, "backtrack_ghost", v.backtrack_ghost);
    v.visible_only = get_or<bool>(obj, "visible_only", v.visible_only);
    v.offscreen = get_or<bool>(obj, "offscreen", v.offscreen);
    v.flag_scoped = get_or<bool>(obj, "flag_scoped", v.flag_scoped);
    v.flag_defuse = get_or<bool>(obj, "flag_defuse", v.flag_defuse);
    v.flag_flash = get_or<bool>(obj, "flag_flash", v.flag_flash);
    v.flag_kit = get_or<bool>(obj, "flag_kit", v.flag_kit);
    v.flag_reload = get_or<bool>(obj, "flag_reload", v.flag_reload);
    v.flag_armor = get_or<bool>(obj, "flag_armor", v.flag_armor);
    v.flag_money = get_or<bool>(obj, "flag_money", v.flag_money);
    v.flag_ping = get_or<bool>(obj, "flag_ping", v.flag_ping);
    v.name_color = get_rgba(obj, "name_color", v.name_color);
    v.weapon_color = get_rgba(obj, "weapon_color", v.weapon_color);
    v.weapon_icon_color = get_rgba(obj, "weapon_icon_color", v.weapon_icon_color);
    v.ammo_color = get_rgba(obj, "ammo_color", v.ammo_color);
    v.distance_color = get_rgba(obj, "distance_color", v.distance_color);
    v.skeleton_color = get_rgba(obj, "skeleton_color", v.skeleton_color);
    v.health_bar_color = get_rgba(obj, "health_bar_color", v.health_bar_color);
    v.health_bar_grad_color = get_rgba(obj, "health_bar_grad_color", v.health_bar_grad_color);
    v.health_bar_gradient = get_or<bool>(obj, "health_bar_gradient", v.health_bar_gradient);
    v.health_bar_glow = get_or<bool>(obj, "health_bar_glow", v.health_bar_glow);
    v.armor_bar_color = get_rgba(obj, "armor_bar_color", v.armor_bar_color);
    v.armor_bar_glow = get_or<bool>(obj, "armor_bar_glow", v.armor_bar_glow);
    if (obj.contains("esp_layout") && obj["esp_layout"].is_array()) {
        const auto& arr = obj["esp_layout"];
        const std::size_t n = std::min(arr.size(), v.esp_layout.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (!arr[i].is_object()) continue;
            v.esp_layout[i].side = clampi("visuals.esp_layout.side",
                                          get_or<int>(arr[i], "side", v.esp_layout[i].side),
                                          0, 3, clamps);
            v.esp_layout[i].order = clampi("visuals.esp_layout.order",
                                           get_or<int>(arr[i], "order", v.esp_layout[i].order),
                                           0, k_esp_elem_count * 2, clamps);
        }
    }
    v.no_flash = get_or<bool>(obj, "no_flash", v.no_flash);
    v.no_smoke = get_or<bool>(obj, "no_smoke", v.no_smoke);
    v.no_scope = get_or<bool>(obj, "no_scope", v.no_scope);
    v.remove_view_punch = get_or<bool>(obj, "remove_view_punch", v.remove_view_punch);
    v.penetration_crosshair = get_or<bool>(obj, "penetration_crosshair", v.penetration_crosshair);
    v.third_person = get_or<bool>(obj, "third_person", v.third_person);
    v.third_person_bind = get_keybind(obj, "third_person_bind", v.third_person_bind);
    v.third_person_distance = clampi("visuals.third_person_distance",
        get_or<int>(obj, "third_person_distance", v.third_person_distance), 48, 160, clamps);
    v.camera_fov = get_or<int>(obj, "camera_fov", v.camera_fov);
    v.fov_while_scoped = get_or<bool>(obj, "fov_while_scoped", v.fov_while_scoped);
    v.bomb_timer = get_or<bool>(obj, "bomb_timer", v.bomb_timer);
    v.esp_alpha = get_or<int>(obj, "esp_alpha", v.esp_alpha);
    read_cham(obj, "player_chams", v.player_chams, clamps);
    read_cham(obj, "team_chams", v.team_chams, clamps);
    read_cham(obj, "local_chams", v.local_chams, clamps);
    read_cham(obj, "arms_chams", v.arms_chams, clamps);
    read_cham(obj, "weapon_chams", v.weapon_chams, clamps);
    v.scope_blend = get_or<bool>(obj, "scope_blend", v.scope_blend);
    v.scope_blend_alpha = get_or<int>(obj, "scope_blend_alpha", v.scope_blend_alpha);
    v.world_modulate = get_or<bool>(obj, "world_modulate", v.world_modulate);
    v.world_color = get_rgba(obj, "world_color", v.world_color);
    v.light_modulate = get_or<bool>(obj, "light_modulate", v.light_modulate);
    v.light_color = get_rgba(obj, "light_color", v.light_color);
    v.exposure = get_or<float>(obj, "exposure", v.exposure);
}

void overlay_profile(json& existing, const json& known) {
    if (known.is_object() && existing.is_object()) {
        for (auto it = known.begin(); it != known.end(); ++it) {
            overlay_profile(existing[it.key()], it.value());
        }
    } else if (known.is_array() && existing.is_array()) {
        for (std::size_t i = 0; i < known.size(); ++i) overlay_profile(existing[i], known[i]);
    } else {
        existing = known;
    }
}

void clamp_rage_weapon(rage_weapon& w, int& clamps) {
    w.fov = clampf("rage.weapon.fov", w.fov, 0.0f, 180.0f, clamps);
    w.min_damage = clampf("rage.weapon.min_damage", w.min_damage, 1.0f, 120.0f, clamps);
    w.hitchance = clampf("rage.weapon.hitchance", w.hitchance, 0.0f, 100.0f, clamps);
    w.auto_stop_speed = clampf("rage.weapon.auto_stop_speed", w.auto_stop_speed, 0.0f, 60.0f, clamps);
    w.auto_stop_ticks = clampi("rage.weapon.auto_stop_ticks", w.auto_stop_ticks, 1, 12, clamps);
    w.multipoint = clampf("rage.weapon.multipoint", w.multipoint, 0.0f, 100.0f, clamps);
    w.safe_point = clampf("rage.weapon.safe_point", w.safe_point, 0.0f, 100.0f, clamps);
    w.delay_shot = static_cast<delay_shot_mode>(clampi(
        "rage.weapon.delay_shot", static_cast<int>(w.delay_shot), 0, 3, clamps));
    w.delay_ticks = clampi("rage.weapon.delay_ticks", w.delay_ticks, 2, 6, clamps);
}

void clamp_all_impl(config_v1& cfg, int& clamps) {
    if (cfg.ui.menu_key <= 6 || cfg.ui.menu_key >= 255) cfg.ui.menu_key = 0x2d;
    for (int i = 0; i < k_weapon_groups; ++i) {
        clamp_rage_weapon(cfg.rage.weapons[static_cast<std::size_t>(i)], clamps);
    }
    for (auto& [def_idx, w] : cfg.rage.weapon_overrides) {
        (void)def_idx;
        clamp_rage_weapon(w, clamps);
    }
    cfg.rage.backtrack_ms = clampf("rage.backtrack_ms", cfg.rage.backtrack_ms, 0.0f, 200.0f, clamps);
    cfg.rage.extrapolation_ms = clampf("rage.extrapolation_ms", cfg.rage.extrapolation_ms,
                                      0.0f, 50.0f, clamps);

    cfg.anti_aim.jitter_range = clampf("anti_aim.jitter_range", cfg.anti_aim.jitter_range, 0.0f,
                                       180.0f, clamps);
    cfg.anti_aim.yaw_offset = clampf("anti_aim.yaw_offset", cfg.anti_aim.yaw_offset, -180.0f,
                                     180.0f, clamps);

    cfg.visuals.esp_alpha = clampi("visuals.esp_alpha", cfg.visuals.esp_alpha, 0, 255, clamps);
    cfg.visuals.scope_blend_alpha = clampi("visuals.scope_blend_alpha",
                                           cfg.visuals.scope_blend_alpha, 0, 255, clamps);
    cfg.visuals.exposure = clampf("visuals.exposure", cfg.visuals.exposure, 0.0f, 200.0f, clamps);
    cfg.visuals.camera_fov = clampi("visuals.camera_fov", cfg.visuals.camera_fov, 0, 135, clamps);
    if (cfg.visuals.camera_fov > 0 && cfg.visuals.camera_fov < 50) {
        cfg.visuals.camera_fov = 50;
        ++clamps;
    }
    for (auto& slot : cfg.visuals.esp_layout) {
        slot.side = clampi("visuals.esp_layout.side", slot.side, 0, 3, clamps);
        slot.order = clampi("visuals.esp_layout.order", slot.order, 0, k_esp_elem_count * 2, clamps);
    }
    cfg.misc.hitsound.volume = clampf("misc.hitsound.volume", cfg.misc.hitsound.volume, 0.0f, 2.0f, clamps);
    cfg.misc.hitsound.body_hit = clampi("misc.hitsound.body_hit", cfg.misc.hitsound.body_hit, 0, 18, clamps);
    cfg.misc.hitsound.body_kill = clampi("misc.hitsound.body_kill", cfg.misc.hitsound.body_kill, 0, 18, clamps);
    cfg.misc.hitsound.head_hit = clampi("misc.hitsound.head_hit", cfg.misc.hitsound.head_hit, 0, 18, clamps);
    cfg.misc.hitsound.head_kill = clampi("misc.hitsound.head_kill", cfg.misc.hitsound.head_kill, 0, 18, clamps);
    cfg.ui.opacity = clampf("ui.opacity", cfg.ui.opacity, 0.2f, 1.0f, clamps);
}

}  // namespace

store::store() : impl_(std::make_unique<impl>()) { apply_safe_profile(cfg_); }

store::~store() = default;
store::store(store&&) noexcept = default;
store& store::operator=(store&&) noexcept = default;

bool store::from_json(std::string_view text) {
    json doc;
    try {
        doc = json::parse(text.begin(), text.end());
    } catch (const json::exception& e) {
        log::error("config: parse failed: {}", e.what());
        return false;
    }
    if (!doc.is_object()) {
        log::error("config: document root is not an object");
        return false;
    }

    impl_->doc = doc;
    cfg_ = config_v1{};
    apply_safe_profile(cfg_);
    clamps_ = 0;

    const int version = get_or<int>(doc, "version", k_schema_version);
    if (version > k_schema_version) {
        log::warn("config: document version {} is newer than {} — reading known keys only",
                  version, k_schema_version);
    }

    if (doc.contains("rage")) read_rage(doc["rage"], cfg_.rage, clamps_);
    if (doc.contains("anti_aim") && doc["anti_aim"].is_object()) {
        const json& a = doc["anti_aim"];
        cfg_.anti_aim.enabled = get_or<bool>(a, "enabled", cfg_.anti_aim.enabled);
        cfg_.anti_aim.toggle = get_keybind(a, "toggle", cfg_.anti_aim.toggle);
        cfg_.anti_aim.base_yaw =
            static_cast<yaw_mode>(clampi("anti_aim.base_yaw", get_or<int>(a, "base_yaw", 0), 0, 4,
                                         clamps_));
        if (static_cast<int>(cfg_.anti_aim.base_yaw) == 1) cfg_.anti_aim.base_yaw = yaw_mode::off;
        cfg_.anti_aim.base_pitch =
            static_cast<pitch_mode>(clampi("anti_aim.base_pitch", get_or<int>(a, "base_pitch", 0),
                                           0, 3, clamps_));
        cfg_.anti_aim.jitter_range = get_or<float>(a, "jitter_range", cfg_.anti_aim.jitter_range);
        cfg_.anti_aim.yaw_offset = get_or<float>(a, "yaw_offset", cfg_.anti_aim.yaw_offset);
        cfg_.anti_aim.at_targets = get_or<bool>(a, "at_targets", cfg_.anti_aim.at_targets);
        cfg_.anti_aim.auto_adjust = get_or<bool>(a, "auto_adjust", cfg_.anti_aim.auto_adjust);
        cfg_.anti_aim.freestanding = get_or<bool>(a, "freestanding", cfg_.anti_aim.freestanding);
        cfg_.anti_aim.on_shot = get_or<bool>(a, "on_shot", cfg_.anti_aim.on_shot);
        cfg_.anti_aim.on_air = get_or<bool>(a, "on_air", cfg_.anti_aim.on_air);
        cfg_.anti_aim.on_move = get_or<bool>(a, "on_move", cfg_.anti_aim.on_move);
    }
    if (doc.contains("visuals")) read_visuals(doc["visuals"], cfg_.visuals, clamps_);
    if (doc.contains("misc") && doc["misc"].is_object()) {
        const json& m = doc["misc"];
        if (m.contains("movement") && m["movement"].is_object()) {
            const json& mv = m["movement"];
            cfg_.misc.movement.bhop = get_or<bool>(mv, "bhop", cfg_.misc.movement.bhop);
            cfg_.misc.movement.bhop_bind = get_keybind(mv, "bhop_bind", cfg_.misc.movement.bhop_bind);
            cfg_.misc.movement.auto_strafe_bind =
                get_keybind(mv, "auto_strafe_bind", cfg_.misc.movement.auto_strafe_bind);
            cfg_.misc.movement.auto_strafe =
                get_or<bool>(mv, "auto_strafe", cfg_.misc.movement.auto_strafe);
            cfg_.misc.movement.edge_jump =
                get_or<bool>(mv, "edge_jump", cfg_.misc.movement.edge_jump);
            cfg_.misc.movement.peek_assist =
                get_or<bool>(mv, "peek_assist", cfg_.misc.movement.peek_assist);
            cfg_.misc.movement.peek_assist_bind =
                get_keybind(mv, "peek_assist_bind", cfg_.misc.movement.peek_assist_bind);
            cfg_.misc.movement.leg_movement =
                get_or<bool>(mv, "leg_movement", cfg_.misc.movement.leg_movement);
        }
        if (m.contains("utilities") && m["utilities"].is_object()) {
            const json& u = m["utilities"];
            cfg_.misc.utilities.auto_pistol =
                get_or<bool>(u, "auto_pistol", cfg_.misc.utilities.auto_pistol);
        }
        if (m.contains("hitsound") && m["hitsound"].is_object()) {
            const json& hs = m["hitsound"];
            cfg_.misc.hitsound.enabled = get_or<bool>(hs, "enabled", cfg_.misc.hitsound.enabled);
            cfg_.misc.hitsound.volume = get_or<float>(hs, "volume", cfg_.misc.hitsound.volume);
            cfg_.misc.hitsound.body_hit = get_or<int>(hs, "body_hit", cfg_.misc.hitsound.body_hit);
            cfg_.misc.hitsound.body_kill = get_or<int>(hs, "body_kill", cfg_.misc.hitsound.body_kill);
            cfg_.misc.hitsound.head_hit = get_or<int>(hs, "head_hit", cfg_.misc.hitsound.head_hit);
            cfg_.misc.hitsound.head_kill = get_or<int>(hs, "head_kill", cfg_.misc.hitsound.head_kill);
        }
        if (m.contains("info") && m["info"].is_object()) {
            const json& inf = m["info"];
            cfg_.misc.info.watermark = get_or<bool>(inf, "watermark", cfg_.misc.info.watermark);
            cfg_.misc.info.keybind_list =
                get_or<bool>(inf, "keybind_list", cfg_.misc.info.keybind_list);
            cfg_.misc.info.spectator_list =
                get_or<bool>(inf, "spectator_list", cfg_.misc.info.spectator_list);
            cfg_.misc.info.hitmarker = get_or<bool>(inf, "hitmarker", cfg_.misc.info.hitmarker);
            cfg_.misc.info.hitmarker_color =
                get_rgba(inf, "hitmarker_color", cfg_.misc.info.hitmarker_color);
        }
        cfg_.misc.block_server_detected =
            get_or<bool>(m, "block_server_detected", cfg_.misc.block_server_detected);
    }
    if (doc.contains("ui") && doc["ui"].is_object()) {
        const json& u = doc["ui"];
        cfg_.ui.dark_theme = get_or<bool>(u, "dark_theme", cfg_.ui.dark_theme);
        cfg_.ui.accent = get_rgba(u, "accent", cfg_.ui.accent);
        cfg_.ui.menu_key = get_or<int>(u, "menu_key", cfg_.ui.menu_key);
        cfg_.ui.opacity = get_or<float>(u, "opacity", cfg_.ui.opacity);
        cfg_.ui.animations = get_or<bool>(u, "animations", cfg_.ui.animations);
    }

    clamp_all();
    return true;
}

std::string store::to_json() const {
    json doc = json::object();
    doc["version"] = std::max(k_schema_version,
                              get_or<int>(impl_->doc, "version", k_schema_version));
    {
        json r;
        write_rage(r, cfg_.rage);
        doc["rage"] = r;
    }
    doc["anti_aim"] = json{
        {"enabled", cfg_.anti_aim.enabled},
        {"toggle", to_keybind(cfg_.anti_aim.toggle)},
        {"base_yaw", static_cast<int>(cfg_.anti_aim.base_yaw)},
        {"base_pitch", static_cast<int>(cfg_.anti_aim.base_pitch)},
        {"jitter_range", cfg_.anti_aim.jitter_range},
        {"yaw_offset", cfg_.anti_aim.yaw_offset},
        {"at_targets", cfg_.anti_aim.at_targets},
        {"auto_adjust", cfg_.anti_aim.auto_adjust},
        {"freestanding", cfg_.anti_aim.freestanding},
        {"on_shot", cfg_.anti_aim.on_shot},
        {"on_air", cfg_.anti_aim.on_air},
        {"on_move", cfg_.anti_aim.on_move},
    };
    doc["visuals"] = visuals_to_json(cfg_.visuals);
    doc["misc"] = json{
        {"movement", json{{"bhop", cfg_.misc.movement.bhop},
                          {"bhop_bind", to_keybind(cfg_.misc.movement.bhop_bind)},
                          {"auto_strafe_bind", to_keybind(cfg_.misc.movement.auto_strafe_bind)},
                          {"auto_strafe", cfg_.misc.movement.auto_strafe},
                          {"edge_jump", cfg_.misc.movement.edge_jump},
                          {"peek_assist", cfg_.misc.movement.peek_assist},
                          {"peek_assist_bind", to_keybind(cfg_.misc.movement.peek_assist_bind)},
                          {"leg_movement", cfg_.misc.movement.leg_movement}}},
        {"utilities", json{{"auto_pistol", cfg_.misc.utilities.auto_pistol}}},
        {"hitsound", json{{"enabled", cfg_.misc.hitsound.enabled},
                          {"volume", cfg_.misc.hitsound.volume},
                          {"body_hit", cfg_.misc.hitsound.body_hit},
                          {"body_kill", cfg_.misc.hitsound.body_kill},
                          {"head_hit", cfg_.misc.hitsound.head_hit},
                          {"head_kill", cfg_.misc.hitsound.head_kill}}},
        {"info", json{{"watermark", cfg_.misc.info.watermark},
                      {"keybind_list", cfg_.misc.info.keybind_list},
                      {"spectator_list", cfg_.misc.info.spectator_list},
                      {"hitmarker", cfg_.misc.info.hitmarker},
                      {"hitmarker_color", to_rgba(cfg_.misc.info.hitmarker_color)}}},
        {"block_server_detected", cfg_.misc.block_server_detected},
    };
    doc["ui"] = json{
        {"dark_theme", cfg_.ui.dark_theme},
        {"accent", to_rgba(cfg_.ui.accent)},
        {"menu_key", cfg_.ui.menu_key},
        {"opacity", cfg_.ui.opacity},
        {"animations", cfg_.ui.animations},
    };
    json saved = impl_->doc.is_object() ? impl_->doc : json::object();
    overlay_profile(saved, doc);
    if (saved.contains("rage") && saved["rage"].is_object() &&
        saved["rage"].contains("weapon_overrides") && saved["rage"]["weapon_overrides"].is_object()) {
        // Drop cleared overrides while preserving unknown future weapon keys.
        auto& saved_ov = saved["rage"]["weapon_overrides"];
        for (auto it = saved_ov.begin(); it != saved_ov.end();) {
            if (const rage_weapon_spec* spec = find_rage_weapon_by_key(it.key().c_str())) {
                if (cfg_.rage.weapon_overrides.find(spec->def_index) == cfg_.rage.weapon_overrides.end()) {
                    it = saved_ov.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
    saved.erase("resolver");
    return saved.dump(2);
}

bool store::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log::warn("config: {} not found — using defaults", path.string());
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const bool ok = from_json(buf.str());
    if (ok) log::info("config: loaded {}", path.string());
    return ok;
}

bool store::save(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        log::error("config: cannot create directory for {} ({})", path.string(), ec.message());
        return false;
    }

    const std::string text = to_json();
    if (text.size() > std::numeric_limits<DWORD>::max()) return false;
    static std::atomic<unsigned> save_serial{0};
    auto staged = path;
    staged += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
              std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(save_serial++);
    const HANDLE file = CreateFileW(staged.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        log::error("config: cannot stage {} ({})", path.string(), GetLastError());
        return false;
    }
    const auto bytes = static_cast<DWORD>(text.size());
    DWORD written = 0;
    const bool complete = WriteFile(file, text.data(), bytes, &written, nullptr) &&
                          written == bytes && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    if (!complete || !closed ||
        !MoveFileExW(staged.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(staged.c_str());
        log::error("config: replacement failed for {}", path.string());
        return false;
    }
    log::info("config: saved {}", path.string());
    return true;
}

void store::clamp_all() { clamp_all_impl(cfg_, clamps_); }

std::filesystem::path store::default_dir() {
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path root = appdata ? appdata : ".";
    return root / std::string(brand::k_appdata_folder) / std::string(brand::k_configs_subdir);
}

}  // namespace shogod::config
