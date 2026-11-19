#include "config/config.hpp"

#include "brand.hpp"
#include "engine/log.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
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
    fallback.vk = get_or<int>(k, "vk", fallback.vk);
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

void apply_safe_profile(config_v1& cfg) {
    struct preset {
        float fov, min_damage, hitchance, multipoint, safe_point;
    };
    const preset table[k_weapon_groups] = {
        {90.0f, 25.0f, 50.0f, 30.0f, 70.0f},   // pistol
        {120.0f, 25.0f, 55.0f, 35.0f, 60.0f},  // smg
        {180.0f, 30.0f, 60.0f, 40.0f, 50.0f},  // rifle
        {180.0f, 60.0f, 65.0f, 30.0f, 60.0f},  // sniper
        {90.0f, 20.0f, 40.0f, 20.0f, 80.0f},   // shotgun
        {120.0f, 25.0f, 50.0f, 30.0f, 70.0f},  // heavy
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
    for (int i = 0; i < k_weapon_groups; ++i) {
        rage_weapon& w = cfg.rage.weapons[static_cast<std::size_t>(i)];
        w.hitchance = 75.0f;
        w.multipoint = 55.0f;
        w.safe_point = 35.0f;
        w.penetration = true;
    }
}

// ---------------------------------------------------------------- document ---

namespace {

json weapons_to_json(const rage_cfg& r) {
    json arr = json::array();
    for (const rage_weapon& w : r.weapons) {
        arr.push_back(json{
            {"fov", w.fov},
            {"min_damage", w.min_damage},
            {"hitchance", w.hitchance},
            {"aim_points", w.aim_points},
            {"multipoint", w.multipoint},
            {"safe_point", w.safe_point},
            {"auto_fire", w.auto_fire},
            {"auto_scope", w.auto_scope},
            {"auto_stop", w.auto_stop},
            {"auto_crouch", w.auto_crouch},
            {"penetration", w.penetration},
            {"penetration_limit", w.penetration_limit},
            {"delay_ticks", w.delay_ticks},
            {"ignore_limbs", w.ignore_limbs},
        });
    }
    return arr;
}

void weapons_from_json(const json& arr, rage_cfg& r) {
    if (!arr.is_array() || arr.size() != static_cast<std::size_t>(k_weapon_groups)) return;
    for (int i = 0; i < k_weapon_groups; ++i) {
        const json& j = arr[static_cast<std::size_t>(i)];
        if (!j.is_object()) continue;
        rage_weapon& w = r.weapons[static_cast<std::size_t>(i)];
        w.fov = get_or<float>(j, "fov", w.fov);
        w.min_damage = get_or<float>(j, "min_damage", w.min_damage);
        w.hitchance = get_or<float>(j, "hitchance", w.hitchance);
        w.aim_points = get_or<std::uint32_t>(j, "aim_points", w.aim_points);
        w.multipoint = get_or<float>(j, "multipoint", w.multipoint);
        w.safe_point = get_or<float>(j, "safe_point", w.safe_point);
        w.auto_fire = get_or<bool>(j, "auto_fire", w.auto_fire);
        w.auto_scope = get_or<bool>(j, "auto_scope", w.auto_scope);
        w.auto_stop = get_or<bool>(j, "auto_stop", w.auto_stop);
        w.auto_crouch = get_or<bool>(j, "auto_crouch", w.auto_crouch);
        w.penetration = get_or<bool>(j, "penetration", w.penetration);
        w.penetration_limit = get_or<float>(j, "penetration_limit", w.penetration_limit);
        w.delay_ticks = get_or<int>(j, "delay_ticks", w.delay_ticks);
        w.ignore_limbs = get_or<bool>(j, "ignore_limbs", w.ignore_limbs);
    }
}

void read_rage(const json& obj, rage_cfg& r, int& clamps) {
    r.enabled = get_or<bool>(obj, "enabled", r.enabled);
    r.toggle = get_keybind(obj, "toggle", r.toggle);
    r.priority = static_cast<aim_priority>(
        clampi("rage.priority", get_or<int>(obj, "priority", static_cast<int>(r.priority)), 0, 3,
               clamps));
    r.backtrack = get_or<bool>(obj, "backtrack", r.backtrack);
    r.backtrack_ms = get_or<float>(obj, "backtrack_ms", r.backtrack_ms);
    r.backtrack_visualise = get_or<bool>(obj, "backtrack_visualise", r.backtrack_visualise);
    r.fake_lag_ticks = get_or<int>(obj, "fake_lag_ticks", r.fake_lag_ticks);
    if (obj.contains("weapons")) weapons_from_json(obj["weapons"], r);
}

void write_rage(json& obj, const rage_cfg& r) {
    obj["enabled"] = r.enabled;
    obj["toggle"] = to_keybind(r.toggle);
    obj["priority"] = static_cast<int>(r.priority);
    obj["backtrack"] = r.backtrack;
    obj["backtrack_ms"] = r.backtrack_ms;
    obj["backtrack_visualise"] = r.backtrack_visualise;
    obj["fake_lag_ticks"] = r.fake_lag_ticks;
    obj["weapons"] = weapons_to_json(r);
}

json visuals_to_json(const visuals_cfg& v) {
    return json{
        {"enabled", v.enabled},
        {"box", static_cast<int>(v.box)},
        {"box_color", to_rgba(v.box_color)},
        {"health_bar", v.health_bar},
        {"armor_bar", v.armor_bar},
        {"name", v.name},
        {"weapon", v.weapon},
        {"ammo", v.ammo},
        {"flags", v.flags},
        {"distance", v.distance},
        {"skeleton", v.skeleton},
        {"glow", v.glow},
        {"backtrack_ghost", v.backtrack_ghost},
        {"hitmarker", v.hitmarker},
        {"damage_indicator", v.damage_indicator},
        {"bullet_tracer", v.bullet_tracer},
        {"spectator_list", v.spectator_list},
        {"bomb_timer", v.bomb_timer},
        {"grenade_helper", v.grenade_helper},
        {"projectile_esp", v.projectile_esp},
        {"dropped_weapons", v.dropped_weapons},
        {"radar_hack", v.radar_hack},
        {"no_flash", v.no_flash},
        {"no_smoke", v.no_smoke},
        {"no_scope_overlay", v.no_scope_overlay},
        {"world_modulation", v.world_modulation},
        {"esp_alpha", v.esp_alpha},
        {"player_chams", json{{"enabled", v.player_chams.enabled},
                              {"visible", to_rgba(v.player_chams.visible)},
                              {"invisible", to_rgba(v.player_chams.invisible)},
                              {"flags", v.player_chams.flags},
                              {"wire_thickness", v.player_chams.wire_thickness}}},
        {"viewmodel_chams", json{{"enabled", v.viewmodel_chams.enabled},
                                 {"visible", to_rgba(v.viewmodel_chams.visible)},
                                 {"invisible", to_rgba(v.viewmodel_chams.invisible)},
                                 {"flags", v.viewmodel_chams.flags},
                                 {"wire_thickness", v.viewmodel_chams.wire_thickness}}},
        {"arms_chams", json{{"enabled", v.arms_chams.enabled},
                            {"visible", to_rgba(v.arms_chams.visible)},
                            {"invisible", to_rgba(v.arms_chams.invisible)},
                            {"flags", v.arms_chams.flags},
                            {"wire_thickness", v.arms_chams.wire_thickness}}},
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
    (void)clamps;
}

void read_visuals(const json& obj, visuals_cfg& v, int& clamps) {
    v.enabled = get_or<bool>(obj, "enabled", v.enabled);
    v.box = static_cast<box_style>(clampi("visuals.box", get_or<int>(obj, "box", static_cast<int>(v.box)),
                                          0, 3, clamps));
    v.box_color = get_rgba(obj, "box_color", v.box_color);
    v.health_bar = get_or<bool>(obj, "health_bar", v.health_bar);
    v.armor_bar = get_or<bool>(obj, "armor_bar", v.armor_bar);
    v.name = get_or<bool>(obj, "name", v.name);
    v.weapon = get_or<bool>(obj, "weapon", v.weapon);
    v.ammo = get_or<bool>(obj, "ammo", v.ammo);
    v.flags = get_or<bool>(obj, "flags", v.flags);
    v.distance = get_or<bool>(obj, "distance", v.distance);
    v.skeleton = get_or<bool>(obj, "skeleton", v.skeleton);
    v.glow = get_or<bool>(obj, "glow", v.glow);
    v.backtrack_ghost = get_or<bool>(obj, "backtrack_ghost", v.backtrack_ghost);
    v.hitmarker = get_or<bool>(obj, "hitmarker", v.hitmarker);
    v.damage_indicator = get_or<bool>(obj, "damage_indicator", v.damage_indicator);
    v.bullet_tracer = get_or<bool>(obj, "bullet_tracer", v.bullet_tracer);
    v.spectator_list = get_or<bool>(obj, "spectator_list", v.spectator_list);
    v.bomb_timer = get_or<bool>(obj, "bomb_timer", v.bomb_timer);
    v.grenade_helper = get_or<bool>(obj, "grenade_helper", v.grenade_helper);
    v.projectile_esp = get_or<bool>(obj, "projectile_esp", v.projectile_esp);
    v.dropped_weapons = get_or<bool>(obj, "dropped_weapons", v.dropped_weapons);
    v.radar_hack = get_or<bool>(obj, "radar_hack", v.radar_hack);
    v.no_flash = get_or<bool>(obj, "no_flash", v.no_flash);
    v.no_smoke = get_or<bool>(obj, "no_smoke", v.no_smoke);
    v.no_scope_overlay = get_or<bool>(obj, "no_scope_overlay", v.no_scope_overlay);
    v.world_modulation = get_or<float>(obj, "world_modulation", v.world_modulation);
    v.esp_alpha = get_or<int>(obj, "esp_alpha", v.esp_alpha);
    read_cham(obj, "player_chams", v.player_chams, clamps);
    read_cham(obj, "viewmodel_chams", v.viewmodel_chams, clamps);
    read_cham(obj, "arms_chams", v.arms_chams, clamps);
}

void clamp_all_impl(config_v1& cfg, int& clamps) {
    for (int i = 0; i < k_weapon_groups; ++i) {
        rage_weapon& w = cfg.rage.weapons[static_cast<std::size_t>(i)];
        w.fov = clampf("rage.weapon.fov", w.fov, 0.0f, 180.0f, clamps);
        w.min_damage = clampf("rage.weapon.min_damage", w.min_damage, 0.0f, 150.0f, clamps);
        w.hitchance = clampf("rage.weapon.hitchance", w.hitchance, 0.0f, 100.0f, clamps);
        w.multipoint = clampf("rage.weapon.multipoint", w.multipoint, 0.0f, 100.0f, clamps);
        w.safe_point = clampf("rage.weapon.safe_point", w.safe_point, 0.0f, 100.0f, clamps);
        w.penetration_limit = clampf("rage.weapon.penetration_limit", w.penetration_limit, 0.0f,
                                     3.0f, clamps);
        w.delay_ticks = clampi("rage.weapon.delay_ticks", w.delay_ticks, 0, 16, clamps);
    }
    cfg.rage.backtrack_ms = clampf("rage.backtrack_ms", cfg.rage.backtrack_ms, 0.0f, 400.0f, clamps);
    cfg.rage.fake_lag_ticks = clampi("rage.fake_lag_ticks", cfg.rage.fake_lag_ticks, 0, 16, clamps);

    cfg.anti_aim.jitter_range = clampf("anti_aim.jitter_range", cfg.anti_aim.jitter_range, 0.0f,
                                       180.0f, clamps);
    cfg.anti_aim.desync_angle = clampf("anti_aim.desync_angle", cfg.anti_aim.desync_angle, 0.0f,
                                       60.0f, clamps);

    cfg.visuals.esp_alpha = clampi("visuals.esp_alpha", cfg.visuals.esp_alpha, 0, 255, clamps);
    cfg.visuals.world_modulation =
        clampf("visuals.world_modulation", cfg.visuals.world_modulation, 0.0f, 1.0f, clamps);

    cfg.misc.utilities.third_person_distance =
        clampf("misc.third_person_distance", cfg.misc.utilities.third_person_distance, 0.0f, 500.0f,
               clamps);
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
    // Migration chain lives here as versions accumulate.

    if (doc.contains("rage")) read_rage(doc["rage"], cfg_.rage, clamps_);
    if (doc.contains("anti_aim") && doc["anti_aim"].is_object()) {
        const json& a = doc["anti_aim"];
        cfg_.anti_aim.enabled = get_or<bool>(a, "enabled", cfg_.anti_aim.enabled);
        cfg_.anti_aim.toggle = get_keybind(a, "toggle", cfg_.anti_aim.toggle);
        cfg_.anti_aim.base_yaw =
            static_cast<yaw_mode>(clampi("anti_aim.base_yaw", get_or<int>(a, "base_yaw", 1), 0, 4,
                                         clamps_));
        cfg_.anti_aim.base_pitch =
            static_cast<pitch_mode>(clampi("anti_aim.base_pitch", get_or<int>(a, "base_pitch", 1),
                                           0, 3, clamps_));
        cfg_.anti_aim.jitter_range = get_or<float>(a, "jitter_range", cfg_.anti_aim.jitter_range);
        cfg_.anti_aim.desync_angle = get_or<float>(a, "desync_angle", cfg_.anti_aim.desync_angle);
        cfg_.anti_aim.on_shot = get_or<bool>(a, "on_shot", cfg_.anti_aim.on_shot);
        cfg_.anti_aim.on_air = get_or<bool>(a, "on_air", cfg_.anti_aim.on_air);
        cfg_.anti_aim.on_move = get_or<bool>(a, "on_move", cfg_.anti_aim.on_move);
        cfg_.anti_aim.fake_walk = get_or<bool>(a, "fake_walk", cfg_.anti_aim.fake_walk);
    }
    if (doc.contains("resolver") && doc["resolver"].is_object()) {
        const json& r = doc["resolver"];
        cfg_.resolver.enabled = get_or<bool>(r, "enabled", cfg_.resolver.enabled);
        cfg_.resolver.debug_log = get_or<bool>(r, "debug_log", cfg_.resolver.debug_log);
    }
    if (doc.contains("visuals")) read_visuals(doc["visuals"], cfg_.visuals, clamps_);
    if (doc.contains("misc") && doc["misc"].is_object()) {
        const json& m = doc["misc"];
        if (m.contains("movement") && m["movement"].is_object()) {
            const json& mv = m["movement"];
            cfg_.misc.movement.bhop = get_or<bool>(mv, "bhop", cfg_.misc.movement.bhop);
            cfg_.misc.movement.auto_strafe =
                get_or<bool>(mv, "auto_strafe", cfg_.misc.movement.auto_strafe);
            cfg_.misc.movement.edge_jump =
                get_or<bool>(mv, "edge_jump", cfg_.misc.movement.edge_jump);
            cfg_.misc.movement.fast_ladder =
                get_or<bool>(mv, "fast_ladder", cfg_.misc.movement.fast_ladder);
            cfg_.misc.movement.slide = get_or<bool>(mv, "slide", cfg_.misc.movement.slide);
        }
        if (m.contains("utilities") && m["utilities"].is_object()) {
            const json& u = m["utilities"];
            cfg_.misc.utilities.knife_bot = get_or<bool>(u, "knife_bot", cfg_.misc.utilities.knife_bot);
            cfg_.misc.utilities.auto_accept =
                get_or<bool>(u, "auto_accept", cfg_.misc.utilities.auto_accept);
            cfg_.misc.utilities.auto_pistol =
                get_or<bool>(u, "auto_pistol", cfg_.misc.utilities.auto_pistol);
            cfg_.misc.utilities.fake_duck =
                get_or<bool>(u, "fake_duck", cfg_.misc.utilities.fake_duck);
            cfg_.misc.utilities.third_person =
                get_or<bool>(u, "third_person", cfg_.misc.utilities.third_person);
            cfg_.misc.utilities.third_person_distance =
                get_or<float>(u, "third_person_distance", cfg_.misc.utilities.third_person_distance);
            cfg_.misc.utilities.free_look =
                get_or<bool>(u, "free_look", cfg_.misc.utilities.free_look);
            cfg_.misc.utilities.free_look_key =
                get_keybind(u, "free_look_key", cfg_.misc.utilities.free_look_key);
        }
        if (m.contains("info") && m["info"].is_object()) {
            const json& inf = m["info"];
            cfg_.misc.info.watermark = get_or<bool>(inf, "watermark", cfg_.misc.info.watermark);
            cfg_.misc.info.keybind_list =
                get_or<bool>(inf, "keybind_list", cfg_.misc.info.keybind_list);
            cfg_.misc.info.hit_log = get_or<bool>(inf, "hit_log", cfg_.misc.info.hit_log);
            cfg_.misc.info.match_info = get_or<bool>(inf, "match_info", cfg_.misc.info.match_info);
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
    json doc = impl_->doc.is_object() ? impl_->doc : json::object();

    doc["version"] = k_schema_version;
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
        {"desync_angle", cfg_.anti_aim.desync_angle},
        {"on_shot", cfg_.anti_aim.on_shot},
        {"on_air", cfg_.anti_aim.on_air},
        {"on_move", cfg_.anti_aim.on_move},
        {"fake_walk", cfg_.anti_aim.fake_walk},
    };
    doc["resolver"] = json{
        {"enabled", cfg_.resolver.enabled},
        {"debug_log", cfg_.resolver.debug_log},
    };
    doc["visuals"] = visuals_to_json(cfg_.visuals);
    doc["misc"] = json{
        {"movement", json{{"bhop", cfg_.misc.movement.bhop},
                          {"auto_strafe", cfg_.misc.movement.auto_strafe},
                          {"edge_jump", cfg_.misc.movement.edge_jump},
                          {"fast_ladder", cfg_.misc.movement.fast_ladder},
                          {"slide", cfg_.misc.movement.slide}}},
        {"utilities", json{{"knife_bot", cfg_.misc.utilities.knife_bot},
                           {"auto_accept", cfg_.misc.utilities.auto_accept},
                           {"auto_pistol", cfg_.misc.utilities.auto_pistol},
                           {"fake_duck", cfg_.misc.utilities.fake_duck},
                           {"third_person", cfg_.misc.utilities.third_person},
                           {"third_person_distance", cfg_.misc.utilities.third_person_distance},
                           {"free_look", cfg_.misc.utilities.free_look},
                           {"free_look_key", to_keybind(cfg_.misc.utilities.free_look_key)}}},
        {"info", json{{"watermark", cfg_.misc.info.watermark},
                      {"keybind_list", cfg_.misc.info.keybind_list},
                      {"hit_log", cfg_.misc.info.hit_log},
                      {"match_info", cfg_.misc.info.match_info}}},
        {"block_server_detected", cfg_.misc.block_server_detected},
    };
    doc["ui"] = json{
        {"dark_theme", cfg_.ui.dark_theme},
        {"accent", to_rgba(cfg_.ui.accent)},
        {"menu_key", cfg_.ui.menu_key},
        {"opacity", cfg_.ui.opacity},
        {"animations", cfg_.ui.animations},
    };
    return doc.dump(2);
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

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("config: cannot write {}", path.string());
        return false;
    }
    const std::string text = to_json();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    log::info("config: saved {}", path.string());
    return static_cast<bool>(out);
}

void store::clamp_all() { clamp_all_impl(cfg_, clamps_); }

std::filesystem::path store::default_dir() {
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path root = appdata ? appdata : ".";
    return root / std::string(brand::k_appdata_folder) / std::string(brand::k_configs_subdir);
}

}  // namespace shogod::config
