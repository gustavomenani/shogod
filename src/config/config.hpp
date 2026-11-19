#pragma once

// Versioned configuration. The in-memory structs are the single source the
// features and the menu both read; JSON is only a projection. Unknown keys are
// preserved on save so a newer build's config survives a downgrade.

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace shogod::config {

inline constexpr int k_schema_version = 1;

struct rgba {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct keybind {
    int vk = 0;        // 0 = unbound
    bool hold = false; // true = hold, false = toggle
    bool active = false;

    [[nodiscard]] bool bound() const noexcept { return vk != 0; }
};

// ---------------------------------------------------------------- ragebot ----
enum class weapon_group : int { pistol = 0, smg, rifle, sniper, shotgun, heavy, count };
inline constexpr int k_weapon_groups = static_cast<int>(weapon_group::count);

enum class aim_priority : int { distance = 0, lowest_hp, threat, cycle };

namespace aim_point {
inline constexpr std::uint32_t head = 1u << 0;
inline constexpr std::uint32_t neck = 1u << 1;
inline constexpr std::uint32_t chest = 1u << 2;
inline constexpr std::uint32_t stomach = 1u << 3;
inline constexpr std::uint32_t pelvis = 1u << 4;
inline constexpr std::uint32_t legs = 1u << 5;
}  // namespace aim_point

struct rage_weapon {
    float fov = 180.0f;
    float min_damage = 30.0f;
    float hitchance = 60.0f;
    std::uint32_t aim_points = aim_point::head | aim_point::chest;
    float multipoint = 40.0f;   // % of hull used for multi-point spread
    float safe_point = 50.0f;   // % of hull, 100 = centre
    bool auto_fire = true;
    bool auto_scope = true;
    bool auto_stop = true;
    bool auto_crouch = false;
    bool penetration = true;
    float penetration_limit = 1.0f;  // max surfaces to punch
    int delay_ticks = 0;
    bool ignore_limbs = false;
};

struct rage_cfg {
    bool enabled = false;
    keybind toggle{};
    aim_priority priority = aim_priority::distance;
    bool backtrack = true;
    float backtrack_ms = 200.0f;
    bool backtrack_visualise = false;
    int fake_lag_ticks = 0;
    std::array<rage_weapon, k_weapon_groups> weapons{};
};

// ---------------------------------------------------------------- antiaim ----
enum class yaw_mode : int { off = 0, desync, jitter, random, fake_static };
enum class pitch_mode : int { off = 0, down, up, zero };

struct anti_aim_cfg {
    bool enabled = false;
    keybind toggle{};
    yaw_mode base_yaw = yaw_mode::desync;
    float jitter_range = 30.0f;
    float desync_angle = 58.0f;
    pitch_mode base_pitch = pitch_mode::down;
    bool on_shot = true;
    bool on_air = false;
    bool on_move = false;
    bool fake_walk = false;
};

struct resolver_cfg {
    bool enabled = true;
    bool debug_log = false;
};

// ---------------------------------------------------------------- visuals ----
enum class box_style : int { off = 0, flat_2d, corner, box_3d };
enum class chams_flag : int {
    ignore_z = 1 << 0,
    wireframe = 1 << 1,
    flat = 1 << 2,
    overlay = 1 << 3,
};

struct cham_material {
    bool enabled = false;
    rgba visible{0.31f, 0.55f, 1.0f, 1.0f};
    rgba invisible{1.0f, 0.25f, 0.25f, 1.0f};
    int flags = 0;  // chams_flag bits
    float wire_thickness = 1.0f;
};

struct visuals_cfg {
    bool enabled = true;

    box_style box = box_style::corner;
    rgba box_color{0.9f, 0.9f, 0.9f, 1.0f};

    bool health_bar = true;
    bool armor_bar = true;
    bool name = true;
    bool weapon = true;
    bool ammo = true;
    bool flags = true;
    bool distance = true;
    bool skeleton = false;
    bool glow = false;
    bool backtrack_ghost = true;
    bool hitmarker = true;
    bool damage_indicator = true;
    bool bullet_tracer = true;
    bool spectator_list = false;

    cham_material player_chams{};
    cham_material viewmodel_chams{};
    cham_material arms_chams{};

    bool bomb_timer = true;
    bool grenade_helper = false;
    bool projectile_esp = false;
    bool dropped_weapons = false;
    bool radar_hack = false;
    bool no_flash = false;
    bool no_smoke = false;
    bool no_scope_overlay = false;
    float world_modulation = 1.0f;

    int esp_alpha = 220;  // 0..255, global ESP opacity
};

// ------------------------------------------------------------------- misc ----
struct movement_cfg {
    bool bhop = false;
    bool auto_strafe = false;
    bool edge_jump = false;
    bool fast_ladder = false;
    bool slide = false;
};

struct utilities_cfg {
    bool knife_bot = false;
    bool auto_accept = false;
    bool auto_pistol = false;
    bool fake_duck = false;
    bool third_person = false;
    float third_person_distance = 150.0f;
    bool free_look = false;
    keybind free_look_key{};
};

struct info_cfg {
    bool watermark = true;
    bool keybind_list = true;
    bool hit_log = true;
    bool match_info = false;
};

struct misc_cfg {
    movement_cfg movement{};
    utilities_cfg utilities{};
    info_cfg info{};
    // Toggles for features community server plugins react to.
    bool block_server_detected = false;
};

// --------------------------------------------------------------------- ui ----
struct ui_cfg {
    bool dark_theme = true;
    rgba accent{0.36f, 0.55f, 1.0f, 1.0f};
    int menu_key = 0x2D;  // VK_INSERT
    float opacity = 0.97f;
    bool animations = true;
};

// ---------------------------------------------------------------- document ---
struct config_v1 {
    int version = k_schema_version;
    rage_cfg rage{};
    anti_aim_cfg anti_aim{};
    resolver_cfg resolver{};
    visuals_cfg visuals{};
    misc_cfg misc{};
    ui_cfg ui{};
};

// Sane defaults per weapon group (safe profile); the rage profile is a UI
// preset that rewrites these fields, not a separate struct.
void apply_safe_profile(config_v1& cfg);
void apply_rage_profile(config_v1& cfg);

class store {
public:
    store();
    ~store();
    store(store&&) noexcept;
    store& operator=(store&&) noexcept;
    store(const store&) = delete;
    store& operator=(const store&) = delete;

    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    [[nodiscard]] const config_v1& get() const noexcept { return cfg_; }
    config_v1& get() noexcept { return cfg_; }

    // Round-trips the document through JSON without touching the file system.
    [[nodiscard]] std::string to_json() const;
    bool from_json(std::string_view text);

    [[nodiscard]] int clamps_applied() const noexcept { return clamps_; }

    static std::filesystem::path default_dir();

private:
    void clamp_all();

    struct impl;
    std::unique_ptr<impl> impl_;
    config_v1 cfg_{};
    int clamps_ = 0;
};

}  // namespace shogod::config
