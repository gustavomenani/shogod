#pragma once

// Antiaim math. Pure: takes the real/camera view angles and player context, returns
// the angles the cheat sends and quantized ground movement inputs. Deterministic for
// a given (command_number, seed) so a replay of the same tick produces the same output.

#include "config/config.hpp"
#include "features/math/angles.hpp"
#include "features/math/vec3.hpp"

#include <array>
#include <span>

namespace shogod::features {

struct antiaim_state {
    bool on_ground = true;
    bool moving = false;
    bool shooting = false;
    int tick = 0;
    int seed = 0;
};

// LEGACY (remove in M4): 3-arg entrypoint retained until createmove.cpp switches to apply_antiaim_full.
math::angles apply_antiaim(const config::anti_aim_cfg& cfg, const math::angles& real,
                           const antiaim_state& state);

// LEGACY (remove in M4): snaps AA yaw to nearest 45-deg camera-relative sector.
float align_aa_yaw_for_ground_move(float camera_yaw, float aa_yaw) noexcept;

// Baseline CS2 weapon-hold spine twist compensation (deg) when pitching down.
float default_pose_head_yaw_offset(int weapon_group, float pitch_deg) noexcept;

// Calibrated weapon-group & weapon-type aware spine twist compensation (deg).
// weapon_type matches CCSWeaponBaseVData::m_WeaponType (-1 = infer from weapon_group).
float pose_head_yaw_offset(int weapon_group, int weapon_type, float pitch_deg) noexcept;

// ---- Freestanding corner-tuck decision & probe geometry --------------------
inline constexpr std::array<float, 6> k_freestanding_offsets = {
    115.0f, -115.0f, 135.0f, -135.0f, 95.0f, -95.0f,
};

struct freestanding_candidate {
    float offset_deg = 0.0f;
    float trace_frac = 1.0f;
    bool valid = false;
};

struct freestanding_state {
    int last_side = 0;       // -1, 0, +1
    float last_offset = 0.0f;
};

struct freestanding_decision {
    bool center_blocked = false;
    bool tucked = false;
    float selected_yaw = 0.0f;
    float selected_offset = 0.0f;
    int selected_side = 0;
};

math::vec3 freestanding_head_probe(const math::vec3& local_eye, float yaw_deg,
                                   float pitch_deg) noexcept;

math::vec3 freestanding_trace_target(const math::vec3& enemy_eye,
                                     const math::vec3& head_probe,
                                     float backoff_units = 16.0f) noexcept;

freestanding_decision select_freestanding_yaw(
    float enemy_yaw, bool center_valid, float center_frac,
    std::span<const freestanding_candidate> candidates,
    freestanding_state& state) noexcept;

// ---- Unified pure Anti-Aim pipeline ----------------------------------------
struct antiaim_context {
    bool on_ground = true;
    bool has_move_input = false;
    float speed_2d = 0.0f;
    // Informational: safe manual fire (cfg.anti_aim.on_shot) aligns on fire ticks in
    // createmove.cpp; apply_antiaim_full keeps AA angles steady across shooting ticks.
    bool shooting = false;
    bool suppressed = false;  // external blockers: safe_mode, k_in_use, ladder/noclip, active grenade
    int command_number = 0;   // monotonic cmd->command_number
    int seed = 0;
    bool found_enemy = false;
    float enemy_yaw = 0.0f;
    int weapon_group = static_cast<int>(config::weapon_group::rifle);
    int weapon_type = -1;     // CCSWeaponBaseVData::m_WeaponType (-1 = infer from weapon_group)
    freestanding_decision freestanding{};
};

math::angles apply_antiaim_full(const config::anti_aim_cfg& cfg,
                                const math::angles& camera_view,
                                const antiaim_context& ctx) noexcept;

// ---- Quantized ground movement solver (sv_quantize_movement_input=1) -------
struct quantized_move_state {
    float err_x = 0.0f;
    float err_y = 0.0f;
    float base_err_x = 0.0f;
    float base_err_y = 0.0f;
    float prev_wish_x = 0.0f;
    float prev_wish_y = 0.0f;
    float last_wish_x = 0.0f;
    float last_wish_y = 0.0f;
    float last_cmd_yaw = 0.0f;
    float last_fwd = 0.0f;
    float last_left = 0.0f;
    int last_cmd = -1;
    bool initialized = false;
    bool had_wish = false;
    bool prev_had_wish = false;
};

// 2D world-space lateral error-diffusion solver for CS2 sv_quantize_movement_input=1:
// chooses exact {-1, 0, +1} (forwardmove, leftmove) in cmd_yaw space to track
// wish_world_dir with zero long-term drift while preserving arbitrary aa_angles.yaw.
// Idempotent across sub-frame calls with the same command_number.
void compute_quantized_ground_move(const math::vec3& wish_world_dir, float cmd_yaw,
                                   quantized_move_state& state, int command_number,
                                   float& out_fwd, float& out_left) noexcept;

}  // namespace shogod::features
