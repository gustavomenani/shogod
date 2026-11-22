#pragma once

// Target selection for the ragebot: pure function over a candidate array so it
// is unit-testable without the game.

#include <cstdint>
#include <span>

#include "config/config.hpp"
#include "features/math/hull.hpp"
#include "features/math/vec2.hpp"
#include "features/math/vec3.hpp"

namespace shogod::features {

struct target_candidate {
    int index = 0;
    float fov_deg = 0.0f;
    float distance = 0.0f;
    int health = 0;
    float threat = 0.0f;  // 0..1 estimate: damage potential / proximity
    bool visible = false;
    bool alive = true;
};

// Returns the chosen candidate index into `candidates`, or -1.
// `cycle` advances a caller-owned cursor so repeated calls walk the list.
int pick_target(std::span<const target_candidate> candidates, config::aim_priority priority,
                float max_fov, bool require_visible, int& cycle_cursor);

// Predicts a position from velocity in world units per second.
math::vec3 extrapolate_point(const math::vec3& point, const math::vec3& velocity,
                             float horizon_ms) noexcept;

// Normalized movement input in Source's yaw-relative forward/right axes.
math::vec2 counter_strafe(const math::vec3& velocity, float yaw_deg) noexcept;

struct autostop_input {
    bool enabled = false;
    std::uint32_t flags = 0;
    bool on_ground = true;
    bool has_velocity = false;
    math::vec3 velocity{};
    float speed_2d = 0.0f;
    float cmd_yaw = 0.0f;
    bool has_target = false;
    bool weapon_ready = false;
    bool shot_qualified = false;
    bool within_early_window = false;
    bool between_shots_active = false;
    bool recent_shot_active = false;
    float auto_stop_speed = 0.0f;
    int auto_stop_ticks = 6;
    bool auto_stop_wait = false;
};

struct autostop_decision {
    bool active = false;
    bool zero_move = false;
    float forwardmove = 0.0f;
    float leftmove = 0.0f;
};

autostop_decision compute_autostop(const autostop_input& in) noexcept;

// Pure helper for M4/testing: limits between-shots braking to rapid-fire cycles
// or the final `auto_stop_ticks` window before bolt-action/slow-weapon recovery.
bool is_between_shots_window(int cycle_ticks, int ticks_since_shot,
                             int ticks_until_ready, int auto_stop_ticks) noexcept;

struct autostrafe_input {
    float camera_yaw = 0.0f;
    math::vec3 velocity{};
    float forward_wish = 0.0f;
    float left_wish = 0.0f;
    int strafe_sign = 1;
};

struct autostrafe_output {
    bool active = false;
    float forwardmove = 0.0f;
    float leftmove = 0.0f;
    float steer_yaw = 0.0f;
    int next_strafe_sign = 1;
};

autostrafe_output compute_autostrafe(const autostrafe_input& in) noexcept;

// 45-degree sector-matched continuous AutoStrafe: computes the AirAccelerate-optimal
// world steer angle and matches the nearest discrete 45-degree SetupMove {-1, 0, +1}
// sector relative to `ref_yaw`, keeping |yaw_delta(ref_yaw, steer_yaw)| <= 22.5 deg.
autostrafe_output compute_sector_matched_autostrafe(
    float camera_yaw, float ref_yaw, const math::vec3& velocity,
    float forward_wish, float left_wish, int strafe_sign,
    int command_number) noexcept;

// Rotates SetupMove (fwd, left) from `from_yaw` to `to_yaw` preserving world wish on the unit square.
void rotate_move_inputs(float fwd, float left, float from_yaw, float to_yaw,
                        float& out_fwd, float& out_left) noexcept;

// LEGACY (retained for M4 transition & legacy tests): evaluates all 8 discrete SetupMove
// inputs directly at fixed `cmd_yaw` without viewangle tilt.
autostrafe_output compute_quantized_autostrafe(float camera_yaw, float cmd_yaw,
                                               const math::vec3& velocity,
                                               float forward_wish, float left_wish) noexcept;

struct bhop_state {
    int last_cmd = -1;
    bool cmd_jump = false;
    bool was_on_ground = true;
    bool release_next = false;
};

// Per-command-number Bhop latch: idempotent across multiple render-frame CreateMove calls
// per 64 Hz tick, pressing cleanly on landing and cycling release<->press only on tick advance.
bool step_bhop(bhop_state& s, int command_number, bool on_ground, bool jump_held) noexcept;

struct shot_delay {
    std::uintptr_t target = 0;
    std::uintptr_t weapon = 0;
    int last_tick = -1;
    int ticks = 0;
    config::delay_shot_mode mode = config::delay_shot_mode::off;
    int required_ticks = 0;

    bool ready(std::uintptr_t target_pawn, std::uintptr_t active_weapon, int tick,
               const config::rage_weapon& settings, float hitchance, float damage) noexcept;
};

float estimate_hit_damage(float base, float distance, float range_modifier,
                          float headshot_multiplier, float armor_ratio, int armor,
                          bool helmet, math::hitgroup hitgroup) noexcept;

// Fire gate: hitchance and minimum damage thresholds from the weapon config.
bool should_fire(float hitchance_percent, float estimated_damage, const config::rage_weapon& w);

}  // namespace shogod::features
