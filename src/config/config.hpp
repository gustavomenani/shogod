#pragma once

// Versioned configuration. The in-memory structs are the single source the
// features and the menu both read; JSON is only a projection. Unknown keys are
// preserved on save so a newer build's config survives a downgrade.

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

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
enum class weapon_group : int {
    pistol = 0,
    heavy_pistol,
    smg,
    rifle,
    scout,
    auto_sniper,
    awp,
    heavy,
    count
};
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

enum class delay_shot_mode : int { off, damage, hitchance, both };

namespace auto_stop_flag {
inline constexpr std::uint32_t early = 1u << 0;
inline constexpr std::uint32_t in_air = 1u << 1;
inline constexpr std::uint32_t between_shots = 1u << 2;
inline constexpr std::uint32_t all = early | in_air | between_shots;
}  // namespace auto_stop_flag

struct rage_weapon {
    float fov = 180.0f;
    float min_damage = 30.0f;
    float hitchance = 60.0f;
    std::uint32_t aim_points = aim_point::head | aim_point::chest | aim_point::stomach;
    float multipoint = 55.0f;   // % of hitbox radius used for multi-point spread
    float safe_point = 50.0f;   // % of hull, 100 = centre
    bool auto_fire = true;
    bool auto_scope = true;
    bool auto_stop = true;
    std::uint32_t auto_stop_flags = auto_stop_flag::early | auto_stop_flag::between_shots;
    float auto_stop_speed = 25.0f;
    int auto_stop_ticks = 6;
    bool auto_stop_wait = false;
    bool auto_crouch = false;
    bool penetration = true;
    delay_shot_mode delay_shot = delay_shot_mode::off;
    int delay_ticks = 2;
    bool ignore_limbs = false;
    bool force_body_aim = false;
    bool prefer_body_aim = false;
    bool strict_hitchance = false;
};

struct rage_weapon_spec {
    int def_index = 0;
    weapon_group group = weapon_group::rifle;
    const char* key = "";
    const char* name = "";
    const char* short_name = "";
    int icon_index = 0;
};

[[nodiscard]] const rage_weapon_spec* rage_weapon_catalog(std::size_t& out_count) noexcept;
[[nodiscard]] const rage_weapon_spec* find_rage_weapon_by_def(int def_index) noexcept;
[[nodiscard]] const rage_weapon_spec* find_rage_weapon_by_key(const char* key) noexcept;

struct rage_cfg {
    bool enabled = false;
    keybind toggle{};
    aim_priority priority = aim_priority::distance;
    bool silent_aim = true;
    bool remove_recoil = true;
    bool auto_revolver = true;
    bool backtrack = true;
    float backtrack_ms = 200.0f;
    float extrapolation_ms = 0.0f;
    std::array<rage_weapon, k_weapon_groups> weapons{};
    std::unordered_map<int, rage_weapon> weapon_overrides{};

    [[nodiscard]] const rage_weapon& resolve_weapon(int def_index, int group_idx) const noexcept;
};

// ---------------------------------------------------------------- antiaim ----
enum class yaw_mode : int { off = 0, jitter = 2, random = 3, fake_static = 4 };
enum class pitch_mode : int { off = 0, down, up, zero };

struct anti_aim_cfg {
    bool enabled = false;
    keybind toggle{};
    yaw_mode base_yaw = yaw_mode::off;
    float jitter_range = 30.0f;
    float yaw_offset = 0.0f;
    pitch_mode base_pitch = pitch_mode::off;
    bool at_targets = true;
    bool auto_adjust = true;
    bool freestanding = false;
    bool on_shot = true;
    bool on_air = false;
    bool on_move = false;
};

// ---------------------------------------------------------------- visuals ----
enum class box_style : int { off = 0, flat_2d, corner, box_3d };
enum class chams_flag : int {
    ignore_z = 1 << 0,
    wireframe = 1 << 1,
    flat = 1 << 2,
    overlay = 1 << 3,
};

enum class cham_shader : int { shaded = 0, flat = 1, glow = 2, glossy = 3 };

struct cham_material {
    bool enabled = false;
    rgba visible{0.31f, 0.55f, 1.0f, 1.0f};
    rgba invisible{1.0f, 0.25f, 0.25f, 1.0f};
    int flags = 0;  // chams_flag bits
    float wire_thickness = 1.0f;
    int material = 0;  // cham_shader (0=Shaded, 1=Flat, 2=Glow, 3=Glossy)
    float fresnel_exponent = 2.0f;
    float fresnel_falloff = 2.0f;
};

enum class esp_side : int { top = 0, right = 1, bottom = 2, left = 3 };

enum class esp_elem_id : int {
    name = 0,
    health_bar = 1,
    armor_bar = 2,
    weapon_icon = 3,
    weapon_name = 4,
    ammo = 5,
    distance = 6,
    flag_scoped = 7,
    flag_defuse = 8,
    flag_flash = 9,
    flag_kit = 10,
    flag_reload = 11,
    flag_armor = 12,
    flag_money = 13,
    flag_ping = 14,
    count = 15,
};

inline constexpr int k_esp_elem_count = static_cast<int>(esp_elem_id::count);

struct esp_elem_slot {
    int side = 0;   // 0=Top, 1=Right, 2=Bottom, 3=Left
    int order = 0;
};

constexpr std::array<esp_elem_slot, k_esp_elem_count> default_esp_layout() noexcept {
    return {{
        {static_cast<int>(esp_side::top), 0},     // name
        {static_cast<int>(esp_side::left), 0},    // health_bar
        {static_cast<int>(esp_side::bottom), 0},  // armor_bar
        {static_cast<int>(esp_side::bottom), 1},  // weapon_icon
        {static_cast<int>(esp_side::bottom), 2},  // weapon_name
        {static_cast<int>(esp_side::bottom), 3},  // ammo
        {static_cast<int>(esp_side::bottom), 4},  // distance
        {static_cast<int>(esp_side::right), 0},   // flag_scoped
        {static_cast<int>(esp_side::right), 1},   // flag_defuse
        {static_cast<int>(esp_side::right), 2},   // flag_flash
        {static_cast<int>(esp_side::right), 3},   // flag_kit
        {static_cast<int>(esp_side::right), 4},   // flag_reload
        {static_cast<int>(esp_side::right), 5},   // flag_armor
        {static_cast<int>(esp_side::right), 6},   // flag_money
        {static_cast<int>(esp_side::right), 7},   // flag_ping
    }};
}

struct visuals_cfg {
    bool enabled = true;
    keybind toggle{};

    box_style box = box_style::corner;
    rgba box_color{0.9f, 0.9f, 0.9f, 1.0f};

    bool health_bar = true;
    bool armor_bar = true;
    bool name = true;
    bool weapon = true;
    bool weapon_icon = true;
    bool ammo = true;
    bool flags = true;
    bool distance = true;
    bool skeleton = false;
    bool backtrack_ghost = true;
    bool visible_only = false;
    bool offscreen = true;

    // Granular ESP flags & element styling (managed in interactive 3D Preview + synced to ESP)
    bool flag_scoped = true;
    bool flag_defuse = true;
    bool flag_flash = true;
    bool flag_kit = true;
    bool flag_reload = true;
    bool flag_armor = true;
    bool flag_money = false;
    bool flag_ping = false;

    rgba name_color{0.95f, 0.96f, 0.98f, 1.0f};
    rgba weapon_color{0.84f, 0.87f, 0.92f, 1.0f};
    rgba weapon_icon_color{0.84f, 0.87f, 0.92f, 1.0f};
    rgba ammo_color{0.84f, 0.87f, 0.92f, 1.0f};
    rgba distance_color{0.69f, 0.73f, 0.78f, 1.0f};
    rgba skeleton_color{0.94f, 0.96f, 1.0f, 0.85f};
    rgba health_bar_color{0.25f, 0.88f, 0.35f, 1.0f};
    rgba health_bar_grad_color{0.92f, 0.25f, 0.25f, 1.0f};
    bool health_bar_gradient = true;
    bool health_bar_glow = false;
    rgba armor_bar_color{0.37f, 0.69f, 1.0f, 1.0f};
    bool armor_bar_glow = false;

    std::array<esp_elem_slot, k_esp_elem_count> esp_layout = default_esp_layout();

    cham_material player_chams{};
    cham_material team_chams{};
    cham_material local_chams{};
    cham_material arms_chams{};
    cham_material weapon_chams{};
    bool scope_blend = false;
    int scope_blend_alpha = 110;

    bool no_flash = false;
    bool no_smoke = false;
    bool no_scope = false;
    bool remove_view_punch = false;
    bool penetration_crosshair = false;
    bool third_person = false;
    keybind third_person_bind{};
    int third_person_distance = 96;
    int camera_fov = 0;  // 0 = engine default, 50..135 = override
    bool fov_while_scoped = false;
    bool bomb_timer = true;
    int esp_alpha = 220;

    bool world_modulate = false;
    rgba world_color{0.25f, 0.28f, 0.35f, 1.0f};
    bool light_modulate = false;
    rgba light_color{0.45f, 0.55f, 1.0f, 1.0f};
    float exposure = 0.0f;  // 0 = engine default, 5..200 = override %
};

// ------------------------------------------------------------------- misc ----
struct movement_cfg {
    bool bhop = false;
    keybind bhop_bind{};
    bool auto_strafe = false;
    keybind auto_strafe_bind{};
    bool edge_jump = false;
    bool peek_assist = false;
    keybind peek_assist_bind{};
    bool leg_movement = false;
};

struct utilities_cfg {
    bool auto_pistol = false;
};

struct hitsound_cfg {
    bool enabled = false;
    float volume = 1.0f;  // 0.0 .. 2.0
    int body_hit = 1;
    int body_kill = 3;
    int head_hit = 1;
    int head_kill = 5;
};

struct info_cfg {
    bool watermark = true;
    bool keybind_list = true;
    bool spectator_list = true;
    bool hitmarker = true;
    rgba hitmarker_color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct misc_cfg {
    movement_cfg movement{};
    utilities_cfg utilities{};
    hitsound_cfg hitsound{};
    info_cfg info{};
    bool block_server_detected = false;
};

// --------------------------------------------------------------------- ui ----
struct ui_cfg {
    bool dark_theme = true;
    rgba accent{0.15f, 0.55f, 0.60f, 1.0f};
    int menu_key = 0x2D;  // VK_INSERT
    float opacity = 0.97f;
    bool animations = true;
};

// ---------------------------------------------------------------- document ---
struct config_v1 {
    int version = k_schema_version;
    rage_cfg rage{};
    anti_aim_cfg anti_aim{};
    visuals_cfg visuals{};
    misc_cfg misc{};
    ui_cfg ui{};
};

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
