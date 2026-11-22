#include "features/rage/target.hpp"

#include "features/math/angles.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace shogod::features {
namespace {

bool eligible(const target_candidate& c, float max_fov, bool require_visible) {
    if (!c.alive) return false;
    if (!std::isfinite(c.fov_deg) || c.fov_deg > max_fov) return false;
    if (require_visible && !c.visible) return false;
    return true;
}

}  // namespace

int pick_target(std::span<const target_candidate> candidates, config::aim_priority priority,
                float max_fov, bool require_visible, int& cycle_cursor) {
    int best = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const target_candidate& c = candidates[i];
        if (!eligible(c, max_fov, require_visible)) continue;

        if (best < 0) {
            best = static_cast<int>(i);
            continue;
        }

        const target_candidate& b = candidates[static_cast<std::size_t>(best)];
        switch (priority) {
            case config::aim_priority::distance:
                if (c.distance < b.distance) best = static_cast<int>(i);
                break;
            case config::aim_priority::lowest_hp:
                if (c.health < b.health) best = static_cast<int>(i);
                break;
            case config::aim_priority::threat:
                if (c.threat > b.threat) best = static_cast<int>(i);
                break;
            case config::aim_priority::cycle:
                if (c.index < b.index) best = static_cast<int>(i);
                break;
        }
    }

    if (priority == config::aim_priority::cycle && !candidates.empty()) {
        const int count = static_cast<int>(candidates.size());
        for (int step = 0; step < count; ++step) {
            const int idx = (cycle_cursor + step) % count;
            if (eligible(candidates[static_cast<std::size_t>(idx)], max_fov, require_visible)) {
                cycle_cursor = (idx + 1) % count;
                return idx;
            }
        }
        return -1;
    }
    return best;
}

math::vec3 extrapolate_point(const math::vec3& point, const math::vec3& velocity,
                             float horizon_ms) noexcept {
    if (!point.finite() || !velocity.finite() || !std::isfinite(horizon_ms)) return point;
    const auto predicted = point + velocity * (std::clamp(horizon_ms, 0.0f, 50.0f) / 1000.0f);
    return predicted.finite() ? predicted : point;
}

math::vec2 counter_strafe(const math::vec3& velocity, float yaw_deg) noexcept {
    const float speed = std::hypot(velocity.x, velocity.y);
    if (!std::isfinite(speed) || speed <= 0.0f || !std::isfinite(yaw_deg)) return {};
    const auto forward = math::forward_from_angles({0.0f, yaw_deg});
    const math::vec3 right{forward.y, -forward.x, 0.0f};
    return {-velocity.dot(forward) / speed, -velocity.dot(right) / speed};
}

autostop_decision compute_autostop(const autostop_input& in) noexcept {
    autostop_decision out{};
    if (!in.enabled || !in.has_velocity || !std::isfinite(in.speed_2d) ||
        !in.velocity.finite() || !std::isfinite(in.cmd_yaw)) {
        return out;
    }

    const bool allow_air = (in.flags & config::auto_stop_flag::in_air) != 0;
    if (!in.on_ground && !allow_air) return out;

    const bool ready_brake = in.weapon_ready && in.has_target;
    const bool early_brake = (in.flags & config::auto_stop_flag::early) != 0 &&
                             in.has_target && !in.weapon_ready && in.within_early_window;
    const bool between_brake = (in.flags & config::auto_stop_flag::between_shots) != 0 &&
                               !in.weapon_ready && in.between_shots_active &&
                               (in.has_target || in.recent_shot_active);
    if (!ready_brake && !early_brake && !between_brake) return out;

    out.active = true;
    const float base_deadzone = in.on_ground ? 14.0f : 10.0f;
    const float stop_speed =
        (in.on_ground && std::isfinite(in.auto_stop_speed) && in.auto_stop_speed > 0.0f)
            ? std::max(base_deadzone, in.auto_stop_speed)
            : base_deadzone;
    if (in.speed_2d <= stop_speed) {
        out.zero_move = true;
        return out;
    }

    const math::vec2 stop = counter_strafe(in.velocity, in.cmd_yaw);
    const float peak = std::max(std::fabs(stop.x), std::fabs(stop.y));
    const float norm = peak > 1e-3f ? (1.0f / peak) : 1.0f;
    out.forwardmove = std::clamp(stop.x * norm, -1.0f, 1.0f);
    out.leftmove = std::clamp(-stop.y * norm, -1.0f, 1.0f);
    return out;
}

bool is_between_shots_window(int cycle_ticks, int ticks_since_shot,
                             int ticks_until_ready, int auto_stop_ticks) noexcept {
    const int window = std::clamp(auto_stop_ticks, 1, 12);
    if (ticks_until_ready > 0) {
        return ticks_until_ready <= window;
    }
    if (cycle_ticks > 0 && cycle_ticks <= 24) {
        return ticks_since_shot >= 0 && ticks_since_shot <= cycle_ticks + 4;
    }
    return false;
}

autostrafe_output compute_autostrafe(const autostrafe_input& in) noexcept {
    autostrafe_output out{};
    out.next_strafe_sign = in.strafe_sign >= 0 ? 1 : -1;
    if (!std::isfinite(in.camera_yaw) || !in.velocity.finite() ||
        !std::isfinite(in.forward_wish) || !std::isfinite(in.left_wish)) {
        return out;
    }

    const float speed_2d = std::hypot(in.velocity.x, in.velocity.y);
    const bool has_wish = std::fabs(in.forward_wish) > 0.05f || std::fabs(in.left_wish) > 0.05f;
    if (speed_2d <= 10.0f && !has_wish) return out;

    const float offset_deg = has_wish ? std::atan2(in.left_wish, in.forward_wish) * 57.29578f : 0.0f;
    const float wish_yaw = math::normalize_yaw(in.camera_yaw + offset_deg);

    float steer_yaw = wish_yaw;
    int sign = out.next_strafe_sign;
    if (speed_2d > 24.4f) {
        constexpr float k_air_dot_target = 0.0f;
        const float vel_yaw = std::atan2(in.velocity.y, in.velocity.x) * 57.29578f;
        const float delta = math::yaw_delta(vel_yaw, wish_yaw);
        const float ideal_angle =
            std::acos(std::clamp(k_air_dot_target / speed_2d, 0.0f, 1.0f)) * 57.29578f;

        if (std::fabs(delta) >= 6.0f) {
            sign = delta > 0.0f ? 1 : -1;
        } else if (delta * static_cast<float>(sign) < -2.0f) {
            // Smooth S-curve hysteresis (~2 deg past target heading) avoids 1-tick zigzag braking.
            sign = -sign;
        }
        steer_yaw = math::normalize_yaw(vel_yaw + static_cast<float>(sign) * ideal_angle);
    }

    const float rot = math::yaw_delta(steer_yaw, in.camera_yaw) * (3.14159265f / 180.0f);
    const float raw_f = std::cos(rot);
    const float raw_l = -std::sin(rot);
    const float peak = std::max(std::fabs(raw_f), std::fabs(raw_l));
    const float scale = peak > 1e-3f ? (1.0f / peak) : 1.0f;

    out.active = true;
    out.forwardmove = std::clamp(raw_f * scale, -1.0f, 1.0f);
    out.leftmove = std::clamp(raw_l * scale, -1.0f, 1.0f);
    out.steer_yaw = steer_yaw;
    out.next_strafe_sign = sign;
    return out;
}

autostrafe_output compute_sector_matched_autostrafe(
    float camera_yaw, float ref_yaw, const math::vec3& velocity,
    float forward_wish, float left_wish, int strafe_sign,
    int command_number) noexcept {
    (void)command_number;
    autostrafe_output out{};
    const float norm_ref = std::isfinite(ref_yaw) ? math::normalize_yaw(ref_yaw) : 0.0f;
    out.steer_yaw = norm_ref;
    out.next_strafe_sign = strafe_sign >= 0 ? 1 : -1;
    if (!std::isfinite(camera_yaw) || !std::isfinite(ref_yaw) || !velocity.finite() ||
        !std::isfinite(forward_wish) || !std::isfinite(left_wish)) {
        return out;
    }

    const autostrafe_output base =
        compute_autostrafe({camera_yaw, velocity, forward_wish, left_wish, strafe_sign});
    out.next_strafe_sign = base.next_strafe_sign;
    if (!base.active) return out;

    struct sector_move {
        float f;
        float l;
    };
    // Index offset by +4 maps sector_idx in [-4, +4] (multiples of 45 deg CCW from cmd_yaw)
    // to SetupMove {-1, 0, +1}^2 \ {(0,0)} where wish = fwd * q_fwd - right * q_left.
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

    const float rel = math::yaw_delta(norm_ref, base.steer_yaw);
    const int sector_idx = std::clamp(static_cast<int>(std::lround(rel / 45.0f)), -4, 4);
    const float sector_deg = static_cast<float>(sector_idx) * 45.0f;
    const float tilt = std::clamp(math::yaw_delta(sector_deg, rel), -22.5f, 22.5f);
    float cmd_yaw = math::normalize_yaw(norm_ref + tilt);
    if (std::fabs(math::yaw_delta(norm_ref, cmd_yaw)) > 22.5f) {
        cmd_yaw = math::normalize_yaw(norm_ref + std::copysign(22.499f, tilt));
    }

    const sector_move sector = k_sectors[sector_idx + 4];
    out.active = true;
    out.forwardmove = sector.f;
    out.leftmove = sector.l;
    out.steer_yaw = cmd_yaw;
    return out;
}

void rotate_move_inputs(float fwd, float left, float from_yaw, float to_yaw,
                        float& out_fwd, float& out_left) noexcept {
    if (!std::isfinite(fwd) || !std::isfinite(left) ||
        !std::isfinite(from_yaw) || !std::isfinite(to_yaw)) {
        out_fwd = 0.0f;
        out_left = 0.0f;
        return;
    }
    constexpr float k_deg_to_rad = 3.14159265f / 180.0f;
    const float from_rad = math::normalize_yaw(from_yaw) * k_deg_to_rad;
    const float to_rad = math::normalize_yaw(to_yaw) * k_deg_to_rad;
    const math::vec3 old_fwd{std::cos(from_rad), std::sin(from_rad), 0.0f};
    const math::vec3 old_right{std::sin(from_rad), -std::cos(from_rad), 0.0f};
    const math::vec3 wish = old_fwd * fwd - old_right * left;

    const math::vec3 new_fwd{std::cos(to_rad), std::sin(to_rad), 0.0f};
    const math::vec3 new_right{std::sin(to_rad), -std::cos(to_rad), 0.0f};
    const float raw_input_peak = std::max(std::fabs(fwd), std::fabs(left));
    const float rf = wish.x * new_fwd.x + wish.y * new_fwd.y;
    const float rl = -(wish.x * new_right.x + wish.y * new_right.y);
    const float peak = std::max(std::fabs(rf), std::fabs(rl));
    if (raw_input_peak > 0.5f && peak > 1e-3f) {
        out_fwd = std::clamp(rf / peak, -1.0f, 1.0f);
        out_left = std::clamp(rl / peak, -1.0f, 1.0f);
    } else {
        out_fwd = std::clamp(rf, -1.0f, 1.0f);
        out_left = std::clamp(rl, -1.0f, 1.0f);
    }
}

autostrafe_output compute_quantized_autostrafe(float camera_yaw, float cmd_yaw,
                                               const math::vec3& velocity,
                                               float forward_wish, float left_wish) noexcept {
    autostrafe_output out{};
    if (!std::isfinite(camera_yaw) || !std::isfinite(cmd_yaw) || !velocity.finite()) return out;

    const float speed_2d = std::hypot(velocity.x, velocity.y);
    const bool has_wish = std::fabs(forward_wish) > 0.05f || std::fabs(left_wish) > 0.05f;
    if (speed_2d <= 10.0f && !has_wish) return out;

    const float offset_deg =
        has_wish ? std::atan2(left_wish, forward_wish) * 57.29578f : 0.0f;
    const float wish_yaw = math::normalize_yaw(camera_yaw + offset_deg);
    const bool explicit_turn_key = std::fabs(left_wish) > 0.05f || forward_wish < -0.05f;

    const float vel_yaw =
        speed_2d > 1e-3f ? std::atan2(velocity.y, velocity.x) * 57.29578f : wish_yaw;
    const float wish_err = std::fabs(math::yaw_delta(vel_yaw, wish_yaw));

    const float rad = math::normalize_yaw(cmd_yaw) * (3.14159265f / 180.0f);
    const float fx = std::cos(rad);
    const float fy = std::sin(rad);
    const float rx = fy;
    const float ry = -fx;

    constexpr float k_wish_cap = 30.0f;
    constexpr float k_max_accel_step = 12.0f * 250.0f / 64.0f;  // sv_airaccelerate * maxspeed * dt

    float best_score = -1e30f;
    float best_f = 0.0f;
    float best_l = 0.0f;
    float best_wyaw = wish_yaw;

    for (int df = -1; df <= 1; ++df) {
        for (int dl = -1; dl <= 1; ++dl) {
            if (df == 0 && dl == 0) continue;
            const float f = static_cast<float>(df);
            const float l = static_cast<float>(dl);
            float wx = fx * f - rx * l;
            float wy = fy * f - ry * l;
            const float wl = std::hypot(wx, wy);
            if (wl <= 1e-4f) continue;
            wx /= wl;
            wy /= wl;

            const float cur = velocity.x * wx + velocity.y * wy;
            const float add = k_wish_cap - cur;
            float nvx = velocity.x;
            float nvy = velocity.y;
            if (add > 0.0f) {
                const float acc = std::min(add, k_max_accel_step);
                nvx += acc * wx;
                nvy += acc * wy;
            }
            const float nspd = std::hypot(nvx, nvy);
            const float nvyaw = nspd > 1e-3f ? std::atan2(nvy, nvx) * 57.29578f : wish_yaw;
            const float turn_gain = wish_err - std::fabs(math::yaw_delta(nvyaw, wish_yaw));
            const float spd_gain = nspd - speed_2d;
            const float wyaw = std::atan2(wy, wx) * 57.29578f;

            float score = 0.0f;
            if (speed_2d <= 24.4f) {
                score = -std::fabs(math::yaw_delta(wyaw, wish_yaw));
            } else if (explicit_turn_key && wish_err > 35.0f) {
                score = turn_gain * 7.0f + spd_gain * 1.5f + (add > 0.0f ? 0.2f : -50.0f);
            } else {
                score = spd_gain * 6.0f + turn_gain * 1.2f;
            }

            if (score > best_score) {
                best_score = score;
                best_f = f;
                best_l = l;
                best_wyaw = wyaw;
            }
        }
    }

    out.active = true;
    out.forwardmove = best_f;
    out.leftmove = best_l;
    out.steer_yaw = best_wyaw;
    return out;
}

bool step_bhop(bhop_state& s, int command_number, bool on_ground, bool jump_held) noexcept {
    if (!jump_held) {
        s.last_cmd = command_number;
        s.cmd_jump = false;
        s.release_next = false;
        s.was_on_ground = on_ground;
        return false;
    }

    const bool new_cmd = (command_number != s.last_cmd);
    if (new_cmd) {
        if (!on_ground) {
            s.cmd_jump = false;
            s.release_next = false;
        } else if (!s.was_on_ground) {
            // Exact landing tick: press jump cleanly with a fresh 0->1 edge.
            s.cmd_jump = true;
            s.release_next = true;
        } else if (s.release_next) {
            // Subsequent ground tick: alternate release<->press once per command_number so
            // IsButtonPressed() sees a fresh 0->1 edge and high-FPS sub-calls never cancel it.
            s.cmd_jump = false;
            s.release_next = false;
        } else {
            s.cmd_jump = true;
            s.release_next = true;
        }
        s.last_cmd = command_number;
        s.was_on_ground = on_ground;
    } else if (on_ground && !s.was_on_ground) {
        // Mid-tick ground contact on the same command_number can only latch jump on,
        // never cancel an already-latched jump after Sub-frame 0 client prediction clears FL_ONGROUND.
        s.cmd_jump = true;
        s.release_next = true;
        s.was_on_ground = true;
    }
    return s.cmd_jump;
}

bool shot_delay::ready(std::uintptr_t target_pawn, std::uintptr_t active_weapon, int tick,
                       const config::rage_weapon& settings, float hitchance, float damage) noexcept {
    using config::delay_shot_mode;
    if (settings.delay_shot == delay_shot_mode::off) {
        *this = {};
        return true;
    }

    const bool damage_passes = std::isfinite(damage) && damage >= settings.min_damage;
    const bool hitchance_passes = std::isfinite(hitchance) && hitchance >= settings.hitchance;
    const bool passes = (settings.delay_shot == delay_shot_mode::damage || hitchance_passes) &&
                        (settings.delay_shot == delay_shot_mode::hitchance || damage_passes);
    if (!passes || tick < 0 || target_pawn == 0 || active_weapon == 0) {
        *this = {};
        return false;
    }

    const int required = std::clamp(settings.delay_ticks, 2, 6);
    const bool same = target == target_pawn && weapon == active_weapon &&
                      mode == settings.delay_shot && required_ticks == required;
    if (same && tick == last_tick) return ticks >= required;

    const bool consecutive = same && last_tick >= 0 && tick > last_tick &&
                             (tick - last_tick) <= 3;
    ticks = consecutive ? std::min(ticks + 1, required) : 1;
    target = target_pawn;
    weapon = active_weapon;
    last_tick = tick;
    mode = settings.delay_shot;
    required_ticks = required;
    return ticks >= required;
}

float estimate_hit_damage(float base, float distance, float range_modifier,
                          float headshot_multiplier, float armor_ratio, int armor,
                          bool helmet, math::hitgroup hitgroup) noexcept {
    if (!std::isfinite(base) || base <= 0.0f || !std::isfinite(distance) || distance < 0.0f) return 0.0f;
    float damage = base;
    if (std::isfinite(range_modifier) && range_modifier > 0.0f && range_modifier <= 1.0f) {
        damage *= std::pow(range_modifier, distance / 500.0f);
    }
    if (hitgroup == math::hitgroup::head) {
        if (std::isfinite(headshot_multiplier) && headshot_multiplier >= 1.0f && headshot_multiplier <= 10.0f) {
            damage *= headshot_multiplier;
        }
    } else if (hitgroup == math::hitgroup::stomach) {
        damage *= 1.25f;
    } else if (hitgroup == math::hitgroup::left_leg || hitgroup == math::hitgroup::right_leg) {
        return damage * 0.75f;
    }
    if (armor > 0 && (hitgroup != math::hitgroup::head || helmet) &&
        std::isfinite(armor_ratio) && armor_ratio > 0.0f && armor_ratio <= 2.0f) {
        const float scaled = damage * armor_ratio * 0.5f;
        damage = std::max(scaled, damage - 2.0f * static_cast<float>(armor));
    }
    return std::max(0.0f, damage);
}

bool should_fire(float hitchance_percent, float estimated_damage, const config::rage_weapon& w) {
    return std::isfinite(hitchance_percent) && std::isfinite(estimated_damage) &&
           estimated_damage > 0.0f && hitchance_percent >= w.hitchance &&
           estimated_damage >= w.min_damage;
}

}  // namespace shogod::features
