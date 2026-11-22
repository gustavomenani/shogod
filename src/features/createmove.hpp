#pragma once

// Game-thread CreateMove feature runner: runs AntiAim, movement fix,
// movement assists, LagCompensation, and full hitbox/autowall Ragebot over
// CS2's CUserCommand + protobuf subtick fields.

#include "config/bind_state.hpp"
#include "config/config.hpp"
#include "features/aaa/antiaim.hpp"
#include "features/backtrack.hpp"
#include "features/lagcomp.hpp"
#include "features/math/hitbox_geom.hpp"
#include "features/math/hitchance.hpp"
#include "features/rage/target.hpp"
#include "sdk/cvar.hpp"
#include "sdk/entities.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"
#include "sdk/snapshot.hpp"
#include "sdk/usercmd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace shogod::features {

inline constexpr std::uint64_t k_move_button_mask =
    sdk::k_in_forward | sdk::k_in_back | sdk::k_in_moveleft | sdk::k_in_moveright;

struct peek_assist_state {
    bool active = false;
    bool returning = false;
    bool has_moved = false;
    bool pending_return = false;
    int stall_ticks = 0;
    int last_cmd = -1;
    float best_dist = 0.0f;
    math::vec3 origin{};
};

struct rage_scan_cache {
    bool valid = false;
    int command_number = -1;
    int tick_base = 0;
    std::uintptr_t weapon_entity = 0;
    bool weapon_ready = false;
    std::uint8_t scoped = 0;
    bool fire_allowed = false;
    bool on_ground = false;
    int player_count = 0;
    int clip = 0;
    int next_primary_tick = -1;
    math::vec3 local_eye{};
    float speed_2d = 0.0f;
    math::angles camera_view{};
    int next_cursor = 0;
    bool has_picked = false;
    target_candidate cand{};
    math::angles aim{};
    bool direct = false;
    float hitchance = 0.0f;
    float damage = 0.0f;
    float min_dmg = 0.0f;
    std::uintptr_t pawn = 0;
    int sim_tick = 0;
};

struct tick_input_latch {
    int command_number = -1;
    bool on_ground = true;
    int move_type = 2;
    bool has_velocity = false;
    math::vec3 velocity{};
    float speed_2d = 0.0f;
    bool jump_held = false;
    std::uint64_t raw_held = 0;
    float pre_aa_fwd = 0.0f;
    float pre_aa_left = 0.0f;
};

struct subtick_move_entry {
    float when = 0.0f;
    float analog_forward_delta = 0.0f;
    float analog_left_delta = 0.0f;
    float yaw_delta = 0.0f;
};

struct subtick_strafe_plan {
    bool active = false;
    int step_count = 0;
    std::array<subtick_move_entry, 24> steps{};
    float final_forwardmove = 0.0f;
    float final_leftmove = 0.0f;
    float lock_view_yaw = 0.0f;
    math::vec3 simulated_velocity{};
};

struct createmove_state {
    int cycle_cursor = 0;
    shot_delay delay;
    int brake_commands = 0;
    int strafe_sign = 1;
    int last_strafe_cmd = -1;
    int strafe_pending_sign = 1;
    int strafe_base_sector = 0;
    bool has_strafe_base_sector = false;
    float last_strafe_tilt = 0.0f;
    bool has_last_strafe_output = false;
    autostrafe_output last_strafe_output{};
    std::uint64_t strafe_key_held = 0;
    std::uint64_t strafe_key_priority = 0;
    int last_subtick_strafe_cmd = -1;
    bool has_subtick_strafe_cache = false;
    subtick_strafe_plan subtick_strafe_cache{};
    bool bhop_space_latched = false;
    int bhop_jumped_cmd = -1;
    float bhop_jumped_when = 0.0f;
    tick_input_latch tick_latch{};
    int auto_pistol_wait = 0;
    int last_auto_pistol_cmd = -1;
    bool auto_pistol_release = false;
    bhop_state bhop;
    freestanding_state freestanding{};
    quantized_move_state ground_move{};
    std::uintptr_t last_attack_weapon = 0;
    int last_attack_tick = -1;
    int requested_primary_tick = -1;
    int revolver_command = -1;
    int next_revolver_cock = 0;
    float last_revolver_spawn = 0.0f;
    int last_target_index = -1;
    int last_target_shot_cmd = -1;
    int last_bt_command = -1;
    rage_scan_cache rage_cache{};
    lag_compensation lagcomp;
    peek_assist_state peek_assist;
};

// ---- Pure R1 Ragebot decision helpers (shared with shogod_tests) -----------

[[nodiscard]] inline bool rage_group_enabled(std::uint32_t aim_mask, int hitbox_group,
                                             bool ignore_limbs, bool force_baim) noexcept {
    switch (hitbox_group) {
        case 0: return !force_baim && (aim_mask & config::aim_point::head) != 0;
        case 1: return (aim_mask & (config::aim_point::neck | config::aim_point::chest)) != 0;
        case 2: return (aim_mask & config::aim_point::stomach) != 0;
        case 3: return (aim_mask & config::aim_point::pelvis) != 0;
        case 4:
        case 5: return !ignore_limbs && (aim_mask & config::aim_point::legs) != 0;
        default: return false;
    }
}

[[nodiscard]] inline int rage_group_rank(int group, bool force_body_aim,
                                         bool prefer_body_aim) noexcept {
    if (force_body_aim || prefer_body_aim) {
        switch (group) {
            case 2: return 0;  // stomach
            case 1: return 1;  // chest / neck
            case 3: return 2;  // pelvis
            case 0: return 3;  // head
            default: return 4; // limbs
        }
    }
    switch (group) {
        case 0: return 0;  // head
        case 2: return 1;  // stomach
        case 1: return 2;  // chest / neck
        case 3: return 3;  // pelvis
        default: return 4; // limbs
    }
}

[[nodiscard]] inline float compute_extrapolation_gap_ms(int tick_base, int rec_sim_tick,
                                                        float configured_extrap_ms) noexcept {
    if (!std::isfinite(configured_extrap_ms) || configured_extrap_ms <= 0.5f ||
        rec_sim_tick <= 0 || tick_base <= 0) {
        return 0.0f;
    }
    const int tick_gap = tick_base - rec_sim_tick;
    if (tick_gap < 2) return 0.0f;
    const float max_gap_ms = static_cast<float>(tick_gap - 1) * k_lagcomp_tick_ms;
    return std::min(std::clamp(configured_extrap_ms, 0.0f, 50.0f), max_gap_ms);
}

[[nodiscard]] inline bool resolve_effective_helmet(bool helmet_known, bool helmet, int armor,
                                                   bool is_upper_bound) noexcept {
    if (helmet_known) return helmet;
    if (is_upper_bound) return false;
    return armor > 0;
}

[[nodiscard]] inline float scale_shotgun_damage(float single_pellet_dmg, int num_bullets,
                                                float hitchance_pct) noexcept {
    if (!std::isfinite(single_pellet_dmg) || single_pellet_dmg <= 0.0f) return 0.0f;
    if (num_bullets <= 1) return single_pellet_dmg;
    const float frac = std::isfinite(hitchance_pct)
                           ? std::clamp(hitchance_pct / 100.0f, 0.0f, 1.0f)
                           : 0.0f;
    return single_pellet_dmg * static_cast<float>(num_bullets) * frac;
}

struct rage_shot_candidate {
    float damage = 0.0f;
    float hitchance = 0.0f;
    float fov = 0.0f;
    bool direct = false;
    bool is_center = true;
    bool hc_evaluated = false;
};

[[nodiscard]] inline bool better_rage_shot_point(
    const rage_shot_candidate& a, const std::optional<rage_shot_candidate>& prev,
    int target_health, float safe_point_pct, bool strict_hitchance) noexcept {
    if (!std::isfinite(a.damage) || !std::isfinite(a.fov) || a.damage < 0.0f) {
        return false;
    }
    if (!prev || !std::isfinite(prev->damage) || !std::isfinite(prev->fov) ||
        prev->damage < 0.0f) {
        return true;
    }
    if (a.direct != prev->direct) return a.direct;
    const float hp = static_cast<float>(std::max(1, target_health));
    const bool a_lethal = a.damage >= hp;
    const bool prev_lethal = prev->damage >= hp;
    if (a_lethal != prev_lethal) return a_lethal;
    if (a_lethal && prev_lethal) {
        if (a.is_center != prev->is_center) return a.is_center;
        if (a.fov != prev->fov) return a.fov < prev->fov;
        return a.damage > prev->damage;
    }
    const double dmg_tol = (strict_hitchance || safe_point_pct >= 50.0f) ? 10.0 : 4.0;
    const double a_bucket = std::floor(static_cast<double>(a.damage) / dmg_tol);
    const double prev_bucket = std::floor(static_cast<double>(prev->damage) / dmg_tol);
    if (a_bucket != prev_bucket) return a_bucket > prev_bucket;
    if (a.is_center != prev->is_center) return a.is_center;
    if (a.fov != prev->fov) return a.fov < prev->fov;
    return a.damage > prev->damage;
}

[[nodiscard]] inline bool should_skip_lower_hitbox(
    const std::optional<rage_shot_candidate>& existing_slot, bool hc_evaluated,
    float upper_dmg, float req_hc, float safe_point_pct, bool strict_hitchance,
    int num_bullets) noexcept {
    if (num_bullets > 1 || !existing_slot || !existing_slot->direct ||
        !existing_slot->is_center || !hc_evaluated) {
        return false;
    }
    if (!std::isfinite(existing_slot->hitchance) || !std::isfinite(req_hc) ||
        existing_slot->hitchance < req_hc) {
        return false;
    }
    const float skip_tol =
        (strict_hitchance || safe_point_pct >= 50.0f) ? 10.0f : 4.0f;
    return std::isfinite(existing_slot->damage) && std::isfinite(upper_dmg) &&
           existing_slot->damage >= upper_dmg + skip_tol;
}

template <typename SlotT, typename EvalHcFn>
inline bool update_rage_shot_slot(
    std::optional<SlotT>& slot, const SlotT& sp, int target_health,
    float req_hc, float safe_point_pct, bool strict_hitchance,
    int num_bullets, EvalHcFn&& eval_hc) {
    const auto to_cand = [](const SlotT& s) noexcept -> rage_shot_candidate {
        return {s.damage, s.hitchance, s.fov, s.direct, s.is_center, s.hc_evaluated};
    };
    const auto better_point = [&](const SlotT& a,
                                  const std::optional<SlotT>& prev) noexcept -> bool {
        const std::optional<rage_shot_candidate> prev_cand =
            prev ? std::optional<rage_shot_candidate>(to_cand(*prev)) : std::nullopt;
        return better_rage_shot_point(
            to_cand(a), prev_cand, target_health, safe_point_pct, strict_hitchance);
    };

    const rage_shot_candidate sp_cand = to_cand(sp);
    if (!std::isfinite(sp_cand.damage) || !std::isfinite(sp_cand.fov) ||
        sp_cand.damage < 0.0f) {
        return false;
    }

    if (slot && !slot->hc_evaluated && num_bullets <= 1) {
        eval_hc(slot);
    }

    if (slot && slot->hc_evaluated && num_bullets <= 1) {
        const bool slot_qual = std::isfinite(req_hc) &&
                               std::isfinite(slot->damage) && slot->damage >= 0.0f &&
                               std::isfinite(slot->fov) &&
                               std::isfinite(slot->hitchance) &&
                               slot->hitchance >= req_hc;
        if (!slot_qual || better_point(sp, slot)) {
            std::optional<SlotT> sp_opt = sp;
            eval_hc(sp_opt);
            const bool sp_qual = std::isfinite(req_hc) &&
                                 std::isfinite(sp_opt->damage) && sp_opt->damage >= 0.0f &&
                                 std::isfinite(sp_opt->fov) &&
                                 std::isfinite(sp_opt->hitchance) &&
                                 sp_opt->hitchance >= req_hc;
            const bool replace = (sp_qual != slot_qual)
                                     ? sp_qual
                                     : better_point(*sp_opt, slot);
            if (replace) {
                slot = *sp_opt;
            }
            return replace;
        }
        return false;
    }

    if (better_point(sp, slot)) {
        slot = sp;
        return true;
    }
    return false;
}

// Returns 1 for body, 0 for head, -1 if neither candidate exists.
[[nodiscard]] inline int choose_rage_shot_point(
    const std::optional<rage_shot_candidate>& best_body,
    const std::optional<rage_shot_candidate>& best_head,
    int target_health, float min_dmg, float req_hc,
    bool force_body_aim, bool prefer_body_aim) noexcept {
    const auto valid_cand = [](const std::optional<rage_shot_candidate>& c) noexcept {
        return c && std::isfinite(c->damage) && c->damage > 0.0f && std::isfinite(c->hitchance);
    };
    const bool body_valid = valid_cand(best_body);
    const bool head_valid = valid_cand(best_head);
    if (!body_valid && !head_valid) return -1;

    const auto qualifies = [&](const rage_shot_candidate& s) noexcept {
        return std::isfinite(min_dmg) && std::isfinite(req_hc) &&
               s.damage >= min_dmg && s.hitchance >= req_hc;
    };
    const bool body_ok = body_valid && qualifies(*best_body);
    const bool head_ok = head_valid && qualifies(*best_head);
    const float hp = static_cast<float>(std::max(1, target_health));
    if (body_ok && (force_body_aim || prefer_body_aim || best_body->damage >= hp || !head_ok)) {
        return 1;
    }
    if (head_ok) return 0;
    if (body_valid && head_valid) {
        if (std::isfinite(min_dmg) &&
            best_body->hitchance >= best_head->hitchance - 5.0f &&
            best_body->damage >= min_dmg) {
            return 1;
        }
        return (best_head->damage >= best_body->damage) ? 0 : 1;
    }
    if (body_valid) return 1;
    return 0;
}

struct rage_fire_gate_input {
    bool auto_scope = false;
    int zoom_levels = 0;
    bool scoped = false;
    bool resume_zoom = false;
    int next_secondary_tick = -1;
    int tick_base = 0;
    bool auto_stop = false;
    bool auto_stop_wait = false;
    bool stop_env_ok = false;
    bool has_velocity = false;
    float speed_2d = 0.0f;
    float auto_stop_speed = 0.0f;
    bool on_ground = true;
    float est_dmg = 0.0f;
    float min_dmg = 0.0f;
    float hc = 0.0f;
    float req_hc = 0.0f;
    bool delay_ready = true;
    bool weapon_ready = false;
};

struct rage_fire_gate_output {
    bool wants_scope = false;
    bool stop_wait_ok = true;
    bool shot_qualified = false;
    bool scope_pending = false;
    bool will_fire = false;
};

[[nodiscard]] inline rage_fire_gate_output evaluate_rage_fire_gate(
    const rage_fire_gate_input& in) noexcept {
    rage_fire_gate_output out{};
    out.wants_scope = in.auto_scope && in.zoom_levels > 0 && !in.scoped && !in.resume_zoom;
    const float wait_thresh = (std::isfinite(in.auto_stop_speed) && in.auto_stop_speed > 0.0f)
                                  ? in.auto_stop_speed
                                  : (in.on_ground ? 14.0f : 10.0f);
    out.stop_wait_ok = !(in.auto_stop && in.auto_stop_wait && in.stop_env_ok && in.has_velocity &&
                         (!std::isfinite(in.speed_2d) || in.speed_2d > wait_thresh));
    out.shot_qualified = std::isfinite(in.est_dmg) && std::isfinite(in.hc) &&
                         std::isfinite(in.req_hc) && std::isfinite(in.min_dmg) &&
                         in.est_dmg > 0.0f && in.hc >= in.req_hc && in.est_dmg >= in.min_dmg;
    const bool ready_to_fire_now =
        out.shot_qualified && in.delay_ready && in.weapon_ready && out.stop_wait_ok;
    const bool can_secondary = in.next_secondary_tick < 0 || in.next_secondary_tick <= in.tick_base;
    out.scope_pending = out.wants_scope && !ready_to_fire_now &&
                        std::isfinite(in.est_dmg) && std::isfinite(in.min_dmg) &&
                        in.est_dmg >= in.min_dmg && can_secondary;
    out.will_fire = ready_to_fire_now && !out.scope_pending;
    return out;
}

[[nodiscard]] inline bool can_reuse_rage_scan(
    const rage_scan_cache& cache, int command_number, int tick_base,
    std::uintptr_t weapon_entity, bool weapon_ready, std::uint8_t scoped,
    bool fire_allowed, bool on_ground, int player_count, int clip,
    int next_primary_tick, const math::vec3& local_eye, float speed_2d,
    const math::angles& camera_view) noexcept {
    if (!cache.valid || cache.command_number != command_number || cache.tick_base != tick_base) {
        return false;
    }
    if (cache.weapon_entity != weapon_entity || cache.weapon_ready != weapon_ready ||
        cache.scoped != scoped || cache.fire_allowed != fire_allowed ||
        cache.on_ground != on_ground || cache.player_count != player_count ||
        cache.clip != clip || cache.next_primary_tick != next_primary_tick) {
        return false;
    }
    if (!local_eye.finite() || !cache.local_eye.finite() ||
        (local_eye - cache.local_eye).length_sq() >= 0.25f) {
        return false;
    }
    if (!std::isfinite(speed_2d) || !std::isfinite(cache.speed_2d) ||
        std::fabs(speed_2d - cache.speed_2d) >= 2.0f) {
        return false;
    }
    if (!std::isfinite(camera_view.pitch) || !std::isfinite(camera_view.yaw) ||
        !std::isfinite(cache.camera_view.pitch) || !std::isfinite(cache.camera_view.yaw)) {
        return false;
    }
    const float dp = std::fabs(camera_view.pitch - cache.camera_view.pitch);
    const float dy = std::fabs(math::yaw_delta(cache.camera_view.yaw, camera_view.yaw));
    return dp < 0.5f && dy < 0.5f;
}

// ---- Pure R2/R3 Anti-Aim & Movement helpers (shared with shogod_tests) ------

[[nodiscard]] inline bool compute_aa_gate(
    const config::anti_aim_cfg& cfg, std::uint32_t active_binds, bool safe_mode,
    bool use_held, int move_type, bool grenade_active, bool on_ground,
    float speed_2d, bool has_move_input) noexcept {
    constexpr int k_movetype_noclip = 8;
    constexpr int k_movetype_ladder = 9;
    constexpr int k_movetype_observer = 10;
    const bool aa_bind_active =
        (active_binds & config::feature_bit(config::bound_feature::anti_aim)) != 0;
    if (safe_mode || !aa_bind_active || use_held || grenade_active) return false;
    if (move_type == k_movetype_ladder || move_type == k_movetype_noclip ||
        move_type == k_movetype_observer) {
        return false;
    }
    if (!on_ground && !cfg.on_air) return false;
    const float spd = std::isfinite(speed_2d) ? speed_2d : 0.0f;
    if (!cfg.on_move && (spd > 5.0f || has_move_input)) return false;
    return true;
}

[[nodiscard]] inline bool should_run_edge_jump(
    bool safe_mode, bool edge_jump_cfg, bool bhop_active, bool jump_held,
    bool on_ground, float speed_2d, int move_type) noexcept {
    constexpr int k_movetype_noclip = 8;
    constexpr int k_movetype_ladder = 9;
    constexpr int k_movetype_observer = 10;
    if (safe_mode || !edge_jump_cfg || !on_ground) return false;
    if (move_type == k_movetype_ladder || move_type == k_movetype_noclip ||
        move_type == k_movetype_observer) {
        return false;
    }
    if (bhop_active && jump_held) return false;
    return std::isfinite(speed_2d) && speed_2d > 20.0f;
}

inline bool step_auto_pistol(createmove_state& state, int command_number,
                             bool eligible, bool attack_held,
                             float cycle_time, bool weapon_can_fire = true) noexcept {
    if (!eligible) {
        state.auto_pistol_wait = 0;
        state.last_auto_pistol_cmd = -1;
        state.auto_pistol_release = false;
        return false;
    }
    if (command_number != state.last_auto_pistol_cmd) {
        state.last_auto_pistol_cmd = command_number;
        if (state.auto_pistol_wait > 0) {
            --state.auto_pistol_wait;
            state.auto_pistol_release = true;
        } else if (attack_held && weapon_can_fire) {
            const float cycle =
                (std::isfinite(cycle_time) && cycle_time > 0.01f) ? cycle_time : 0.15f;
            state.auto_pistol_wait =
                std::max(1, static_cast<int>(std::clamp(cycle, 0.0f, 60.0f) * 64.0f) - 1);
            state.auto_pistol_release = false;
        } else if (attack_held) {
            state.auto_pistol_release = true;
        } else {
            state.auto_pistol_release = false;
        }
    }
    return state.auto_pistol_release;
}

inline void step_peek_assist_stall(peek_assist_state& pa, int command_number,
                                   float dist) noexcept {
    if (!std::isfinite(dist)) return;
    if (dist < pa.best_dist - 0.5f) {
        pa.best_dist = dist;
        pa.stall_ticks = 0;
        pa.last_cmd = command_number;
    } else if (command_number != pa.last_cmd) {
        pa.last_cmd = command_number;
        ++pa.stall_ticks;
    }
}

inline int latch_strafe_sign(createmove_state& state, int command_number) noexcept {
    if (state.last_strafe_cmd != command_number) {
        state.strafe_sign = (state.strafe_pending_sign >= 0) ? 1 : -1;
        state.last_strafe_cmd = command_number;
    }
    return state.strafe_sign;
}

inline void reset_strafe_state(createmove_state& state) noexcept {
    state.strafe_sign = 1;
    state.strafe_pending_sign = 1;
    state.last_strafe_cmd = -1;
    state.strafe_base_sector = 0;
    state.has_strafe_base_sector = false;
    state.last_strafe_tilt = 0.0f;
    state.has_last_strafe_output = false;
    state.last_strafe_output = {};
    state.strafe_key_held = 0;
    state.strafe_key_priority = 0;
    state.last_subtick_strafe_cmd = -1;
    state.has_subtick_strafe_cache = false;
    state.subtick_strafe_cache = {};
}

struct subtick_strafe_input {
    int command_number = 0;
    float camera_yaw = 0.0f;
    float move_ref_yaw = 0.0f;  // Steady yaw target (AA yaw or camera yaw — never oscillated)
    math::vec3 velocity{};
    std::uint64_t buttons = 0;
    float prev_cmd_fwd = 0.0f;
    float prev_cmd_left = 0.0f;
    float max_speed = 250.0f;
    float air_accel = 12.0f;
    float air_max_wishspeed = 30.0f;
    float gravity = 800.0f;
    float surface_friction = 1.0f;
    float stamina = 0.0f;
    float stamina_recovery = 60.0f;
    bool quantize_movement = true;
    bool on_ground = false;
    bool enable_bunnyhopping = false;
    int available_slots = 16;
};

[[nodiscard]] inline float compute_next_jump_frac(
    int last_actual_jump_press_tick, float last_actual_jump_press_frac,
    int client_tick, float spam_penalty_sec) noexcept {
    if (spam_penalty_sec <= 0.0f || last_actual_jump_press_tick <= 0) return 0.0f;
    const float frac = static_cast<float>(last_actual_jump_press_tick - client_tick) +
                       last_actual_jump_press_frac + spam_penalty_sec * 64.0f;
    if (!std::isfinite(frac) || frac <= 0.0f) return 0.0f;
    return std::max(0.0f, (std::floor(frac * 64.0f) + 1.0f) / 64.0f);
}

// Predicts the exact subtick fraction in [0, 1) when a descending player hits ground
// during the current 1/64s tick, solving 0.5*g*t^2 + (-vel_z)*t = dist_to_ground.
// Returns -1.0f if ground contact will not occur within this tick.
[[nodiscard]] inline float predict_landing_subtick_frac(
    float dist_to_ground, float vel_z, float gravity) noexcept {
    if (!std::isfinite(dist_to_ground) || !std::isfinite(vel_z) || !std::isfinite(gravity)) {
        return -1.0f;
    }
    if (dist_to_ground < 0.0f || vel_z >= -0.5f) return -1.0f;
    if (dist_to_ground <= 0.05f) return 0.0f;

    constexpr float k_tick_dt = 1.0f / 64.0f;
    const float g = std::max(0.0f, gravity);
    const float v0 = -vel_z;  // positive downward speed
    const float max_fall = v0 * k_tick_dt + 0.5f * g * k_tick_dt * k_tick_dt;
    if (dist_to_ground > max_fall) return -1.0f;

    float t = 0.0f;
    if (g > 1.0f) {
        const float disc = v0 * v0 + 2.0f * g * dist_to_ground;
        t = (-v0 + std::sqrt(std::max(0.0f, disc))) / g;
    } else {
        t = dist_to_ground / std::max(0.5f, v0);
    }
    const float raw_frac = t / k_tick_dt;
    if (!std::isfinite(raw_frac) || raw_frac < 0.0f || raw_frac >= 1.0f) return -1.0f;
    // Quantize up to CS2's 64-slice subtick grid so PressJump fires right after contact.
    const float q = std::ceil(raw_frac * 64.0f) / 64.0f;
    return (q < 1.0f) ? std::clamp(q, 0.0f, 62.0f / 64.0f) : -1.0f;
}

// Reverie-adapted subtick AirStrafer with three key stability fixes:
// 1. Holds one consistent strafe side across all subticks in a command (no intra-tick zigzag).
// 2. Keeps move_ref_yaw (base_cmd->viewangles->y) rock-solid with zero camera/third-person jitter.
// 3. Caches per command_number so ~300 Hz sub-frame calls reproduce identically.
[[nodiscard]] inline subtick_strafe_plan build_subtick_autostrafe_plan(
    createmove_state& state, const subtick_strafe_input& in) noexcept {
    if (state.last_subtick_strafe_cmd == in.command_number && state.has_subtick_strafe_cache) {
        return state.subtick_strafe_cache;
    }

    subtick_strafe_plan out{};
    const float ref_yaw =
        std::isfinite(in.move_ref_yaw) ? math::normalize_yaw(in.move_ref_yaw) : 0.0f;
    out.lock_view_yaw = ref_yaw;

    if (!std::isfinite(in.camera_yaw) || !in.velocity.finite()) {
        return out;
    }

    const int ticks = std::clamp(in.available_slots, 1, 20);
    auto update_prio = [&](std::uint64_t btn, std::uint64_t opp) {
        if ((in.buttons & btn) == 0) {
            state.strafe_key_priority &= ~btn;
        } else if ((state.strafe_key_held & btn) == 0 ||
                   (state.strafe_key_priority & opp) == 0) {
            state.strafe_key_priority = (state.strafe_key_priority & ~opp) | btn;
        }
    };
    update_prio(sdk::k_in_moveleft, sdk::k_in_moveright);
    update_prio(sdk::k_in_moveright, sdk::k_in_moveleft);
    update_prio(sdk::k_in_forward, sdk::k_in_back);
    update_prio(sdk::k_in_back, sdk::k_in_forward);
    state.strafe_key_held = in.buttons;

    float key_offset = 0.0f;
    if ((state.strafe_key_priority & sdk::k_in_moveleft) != 0) key_offset = 90.0f;
    else if ((state.strafe_key_priority & sdk::k_in_moveright) != 0) key_offset = -90.0f;
    if ((state.strafe_key_priority & sdk::k_in_forward) != 0) key_offset *= 0.5f;
    else if ((state.strafe_key_priority & sdk::k_in_back) != 0) {
        key_offset = -key_offset * 0.5f + 180.0f;
    }
    const float target_yaw = math::normalize_yaw(in.camera_yaw + key_offset);

    float move_speed = std::clamp(in.max_speed, 60.0f, 260.0f);
    const float air_max_wishspeed = std::max(1.0f, in.air_max_wishspeed);
    const float air_accel = std::max(0.0f, in.air_accel);
    const float gravity = std::max(0.0f, in.gravity);
    const float stamina_recovery = std::max(0.0f, in.stamina_recovery);

    math::vec3 vel = in.velocity;
    if (in.on_ground && !in.enable_bunnyhopping) {
        const float limit = move_speed * 1.1f;
        const float spd = vel.length();
        if (limit > 0.0f && spd > limit) {
            vel = vel * (limit / spd);
        }
    }

    float surface_friction = std::clamp(in.surface_friction, 0.1f, 1.0f);
    float stamina = std::clamp(in.stamina, 0.0f, 100.0f);
    float last_fwd = std::clamp(in.prev_cmd_fwd, -1.0f, 1.0f);
    float last_left = std::clamp(in.prev_cmd_left, -1.0f, 1.0f);
    float last_yaw_delta = 0.0f;

    constexpr float k_deg2rad = 3.14159265358979323846f / 180.0f;
    constexpr float k_rad2deg = 180.0f / 3.14159265358979323846f;
    const int tick_side = (state.strafe_pending_sign >= 0) ? 1 : -1;
    bool toggled_straight_side = false;

    for (int i = 0; i < ticks; ++i) {
        const float when = static_cast<float>(i) / static_cast<float>(ticks);
        const float next_when = static_cast<float>(i + 1) / static_cast<float>(ticks);
        const float dt = std::max(1.0f / 4096.0f, (next_when - when) * (1.0f / 64.0f));
        const float stamina_scale = std::clamp(1.0f - stamina / 100.0f, 0.0f, 1.0f);
        const float wish_speed = move_speed * stamina_scale * stamina_scale;
        const float max_wish = std::min(wish_speed, air_max_wishspeed);
        const float accel_term = air_accel * wish_speed * surface_friction * dt;

        const float speed = std::hypot(vel.x, vel.y);
        const float vel_angle =
            speed > 0.01f ? std::atan2(vel.y, vel.x) * k_rad2deg : target_yaw;
        const float vel_delta = math::yaw_delta(vel_angle, target_yaw);
        float wish_yaw = target_yaw;

        if (speed > 0.1f) {
            const float cos_angle = (max_wish - accel_term) / speed;
            if (cos_angle < 1.0f) {
                const float ideal_angle =
                    std::acos(std::clamp(cos_angle, 0.0f, 1.0f)) * k_rad2deg;
                if (std::fabs(vel_delta) < 0.1f) {
                    wish_yaw = vel_angle + (tick_side > 0 ? ideal_angle : -ideal_angle);
                    toggled_straight_side = true;
                } else {
                    wish_yaw = vel_angle + (vel_delta > 0.0f ? ideal_angle : -ideal_angle);
                }
            }
        } else {
            toggled_straight_side = true;
        }
        wish_yaw = math::normalize_yaw(wish_yaw);

        const float wish_rad = wish_yaw * k_deg2rad;
        const math::vec3 wish_dir{std::cos(wish_rad), std::sin(wish_rad), 0.0f};
        const float proj = vel.x * wish_dir.x + vel.y * wish_dir.y;
        const float add_speed = max_wish - proj;
        if (add_speed > 0.0f) {
            const float gain = std::min(accel_term, add_speed);
            vel.x += wish_dir.x * gain;
            vel.y += wish_dir.y * gain;
        }

        const float move_yaw = in.quantize_movement ? ref_yaw : wish_yaw;
        const float yaw_delta_val =
            in.quantize_movement ? math::yaw_delta(move_yaw, wish_yaw) : 0.0f;
        const float rot = math::yaw_delta(move_yaw, ref_yaw) * k_deg2rad;
        float fwd = std::clamp(std::cos(rot), -1.0f, 1.0f);
        float left = std::clamp(-std::sin(rot), -1.0f, 1.0f);
        if (std::fabs(fwd) < 0.001f) fwd = 0.0f;
        if (std::fabs(left) < 0.001f) left = 0.0f;

        subtick_move_entry& step = out.steps[static_cast<std::size_t>(out.step_count++)];
        step.when = when;
        step.analog_forward_delta = fwd - last_fwd;
        step.analog_left_delta = left - last_left;
        step.yaw_delta = yaw_delta_val - last_yaw_delta;

        last_fwd = fwd;
        last_left = left;
        last_yaw_delta = yaw_delta_val;
        vel.z -= gravity * dt;
        surface_friction = (vel.z > 0.0f && vel.z <= 140.0f) ? 0.25f : 1.0f;
        stamina = std::max(0.0f, stamina - stamina_recovery * dt);
    }

    if (toggled_straight_side) {
        state.strafe_pending_sign = -tick_side;
    } else if (std::hypot(vel.x, vel.y) > 0.1f) {
        const float final_vel_angle = std::atan2(vel.y, vel.x) * k_rad2deg;
        const float final_delta = math::yaw_delta(final_vel_angle, target_yaw);
        if (std::fabs(final_delta) >= 0.1f) {
            state.strafe_pending_sign = (final_delta > 0.0f) ? 1 : -1;
        }
    }

    out.active = out.step_count > 0;
    out.final_forwardmove = last_fwd;
    out.final_leftmove = last_left;
    out.simulated_velocity = vel;
    state.last_subtick_strafe_cmd = in.command_number;
    state.subtick_strafe_cache = out;
    state.has_subtick_strafe_cache = true;
    return out;
}

inline void strip_subtick_analog_moves(sdk::base_user_cmd_pb* base) noexcept {
    if (!base || !base->subtick_moves.ss) return;
    auto& field = base->subtick_moves;
    if (field.current_size <= 0 || field.capacity <= 0 || field.ss->max <= 0) return;
    const int n = std::min({field.current_size, field.capacity, field.ss->max});
    int write = 0;
    auto** elems = field.ss->elements;
    for (int i = 0; i < n; ++i) {
        auto* step = elems[i];
        // Drop pure movement/yaw subtick slices while preserving button press/release steps.
        if (!step || step->button == 0) continue;
        if (write != i) std::swap(elems[write], elems[i]);
        ++write;
    }
    field.current_size = write;
}

[[nodiscard]] inline int wrap_strafe_sector(int idx) noexcept {
    while (idx > 4) idx -= 8;
    while (idx < -4) idx += 8;
    return idx;
}

[[nodiscard]] inline autostrafe_output compute_runtime_air_strafe(
    createmove_state& state, int command_number, float camera_yaw,
    float ref_yaw, const math::vec3& velocity,
    float forward_wish, float left_wish) noexcept {
    if (state.last_strafe_cmd == command_number && state.has_last_strafe_output) {
        return state.last_strafe_output;
    }

    autostrafe_output out{};
    const float norm_ref = std::isfinite(ref_yaw) ? math::normalize_yaw(ref_yaw) : 0.0f;
    out.steer_yaw = norm_ref;
    out.next_strafe_sign = (state.strafe_pending_sign >= 0) ? 1 : -1;
    if (!std::isfinite(camera_yaw) || !std::isfinite(ref_yaw) || !velocity.finite() ||
        !std::isfinite(forward_wish) || !std::isfinite(left_wish)) {
        return out;
    }

    const int sign_in = latch_strafe_sign(state, command_number);
    const autostrafe_output base =
        compute_autostrafe({camera_yaw, velocity, forward_wish, left_wish, sign_in});
    out.next_strafe_sign = base.next_strafe_sign;
    state.strafe_pending_sign = base.next_strafe_sign;
    if (!base.active) {
        state.last_strafe_output = out;
        state.has_last_strafe_output = true;
        return out;
    }

    struct sector_move {
        float f;
        float l;
    };
    static constexpr sector_move k_sectors[9] = {
        {-1.0f,  0.0f},  // -180 deg
        {-1.0f, -1.0f},  // -135 deg
        { 0.0f, -1.0f},  //  -90 deg
        { 1.0f, -1.0f},  //  -45 deg
        { 1.0f,  0.0f},  //    0 deg
        { 1.0f,  1.0f},  //  +45 deg
        { 0.0f,  1.0f},  //  +90 deg
        {-1.0f,  1.0f},  // +135 deg
        {-1.0f,  0.0f},  // +180 deg
    };

    const float speed_2d = std::hypot(velocity.x, velocity.y);
    const bool has_wish = std::fabs(forward_wish) > 0.05f || std::fabs(left_wish) > 0.05f;
    const float offset_deg =
        has_wish ? std::atan2(left_wish, forward_wish) * 57.29578f : 0.0f;
    const float wish_yaw = math::normalize_yaw(camera_yaw + offset_deg);

    int sec_idx = 0;
    if (speed_2d > 24.4f) {
        const float vel_yaw = std::atan2(velocity.y, velocity.x) * 57.29578f;
        const float delta = math::yaw_delta(vel_yaw, wish_yaw);
        const float center_yaw = (std::fabs(delta) < 10.0f) ? wish_yaw : vel_yaw;
        const float center_rel = math::yaw_delta(norm_ref, center_yaw);
        const float base_rel = math::yaw_delta(norm_ref, vel_yaw);
        if (!state.has_strafe_base_sector ||
            std::fabs(math::yaw_delta(static_cast<float>(state.strafe_base_sector) * 45.0f,
                                      base_rel)) > 27.5f) {
            state.strafe_base_sector =
                std::clamp(static_cast<int>(std::lround(center_rel / 45.0f)), -4, 4);
            state.has_strafe_base_sector = true;
        }
        sec_idx = wrap_strafe_sector(
            state.strafe_base_sector + (base.next_strafe_sign > 0 ? 2 : -2));
    } else {
        const float rel0 = math::yaw_delta(norm_ref, base.steer_yaw);
        sec_idx = std::clamp(static_cast<int>(std::lround(rel0 / 45.0f)), -4, 4);
    }

    const float sec_deg = static_cast<float>(sec_idx) * 45.0f;
    const float rel = math::yaw_delta(norm_ref, base.steer_yaw);
    const float raw_tilt = math::yaw_delta(sec_deg, rel);
    float tilt = std::clamp(raw_tilt, -27.5f, 27.5f);
    if (state.has_last_strafe_output) {
        const float tilt_step = tilt - state.last_strafe_tilt;
        if (std::fabs(tilt_step) > 10.5f) {
            tilt = std::clamp(state.last_strafe_tilt + std::copysign(10.5f, tilt_step),
                              -27.5f, 27.5f);
            const float cur_cmd_yaw = math::normalize_yaw(norm_ref + tilt);
            sec_idx = std::clamp(
                static_cast<int>(std::lround(math::yaw_delta(cur_cmd_yaw, base.steer_yaw) / 45.0f)),
                -4, 4);
        }
    }
    state.last_strafe_tilt = tilt;
    const float cmd_yaw = math::normalize_yaw(norm_ref + tilt);

    const sector_move sector = k_sectors[sec_idx + 4];
    out.active = true;
    out.forwardmove = sector.f;
    out.leftmove = sector.l;
    out.steer_yaw = cmd_yaw;
    state.last_strafe_output = out;
    state.has_last_strafe_output = true;
    return out;
}

[[nodiscard]] inline math::vec3 compute_wish_world_dir(float camera_yaw, float fwd,
                                                       float left) noexcept {
    if (!std::isfinite(camera_yaw) || !std::isfinite(fwd) || !std::isfinite(left)) return {};
    constexpr float k_deg_to_rad = 3.14159265f / 180.0f;
    const float rad = math::normalize_yaw(camera_yaw) * k_deg_to_rad;
    const math::vec3 cam_fwd{std::cos(rad), std::sin(rad), 0.0f};
    const math::vec3 cam_right{std::sin(rad), -std::cos(rad), 0.0f};
    return cam_fwd * fwd - cam_right * left;
}

inline void sync_move_held_buttons(sdk::user_command* cmd, float fwd, float left) noexcept {
    if (!cmd) return;
    cmd->button_state.held &= ~k_move_button_mask;
    cmd->button_state.toggle &= ~k_move_button_mask;
    cmd->button_state.scroll &= ~k_move_button_mask;
    if (std::isfinite(fwd)) {
        if (fwd > 0.01f) cmd->button_state.held |= sdk::k_in_forward;
        else if (fwd < -0.01f) cmd->button_state.held |= sdk::k_in_back;
    }
    if (std::isfinite(left)) {
        if (left > 0.01f) cmd->button_state.held |= sdk::k_in_moveleft;
        else if (left < -0.01f) cmd->button_state.held |= sdk::k_in_moveright;
    }
}

inline void strip_subtick_buttons(sdk::base_user_cmd_pb* base, std::uint64_t mask) noexcept {
    if (!base || !base->subtick_moves.ss) return;
    auto& field = base->subtick_moves;
    if (field.current_size <= 0 || field.capacity <= 0 || field.ss->max <= 0) return;
    const int n = std::min({field.current_size, field.capacity, field.ss->max});
    int write = 0;
    auto** elems = field.ss->elements;
    for (int i = 0; i < n; ++i) {
        auto* step = elems[i];
        if (!step || (step->button & mask) != 0) continue;
        // Preserve skipped element pointers in the tail so RepeatedPtrField reuse never aliases.
        if (write != i) std::swap(elems[write], elems[i]);
        ++write;
    }
    field.current_size = write;
}

inline void resolve_fire_tick_movement(
    bool strafe_applied, const autostrafe_output& strafe_out, bool on_ground,
    const math::vec3& wish_world_dir, float wish_f, float wish_l,
    float camera_yaw, float target_fire_yaw, quantized_move_state& ground_move,
    int command_number, float& out_fwd, float& out_left) noexcept {
    if (strafe_applied && strafe_out.active) {
        rotate_move_inputs(strafe_out.forwardmove, strafe_out.leftmove,
                           strafe_out.steer_yaw, target_fire_yaw, out_fwd, out_left);
        return;
    }
    if (on_ground) {
        compute_quantized_ground_move(wish_world_dir, target_fire_yaw, ground_move,
                                      command_number, out_fwd, out_left);
        return;
    }
    rotate_move_inputs(wish_f, wish_l, camera_yaw, target_fire_yaw, out_fwd, out_left);
}

void run_createmove(std::uintptr_t csgo_input, const sdk::anchors& anchors,
                    const sdk::netvars& nv, const config::config_v1& cfg,
                    const sdk::frame_snapshot& frame, backtrack_history& bt,
                    createmove_state& state, std::uint32_t active_binds) noexcept;

}  // namespace shogod::features
