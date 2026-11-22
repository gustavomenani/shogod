#include "features/aaa/antiaim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace shogod::features {
namespace {

constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_deg_to_rad = k_pi / 180.0f;

std::uint32_t hash(int tick, int seed) noexcept {
    std::uint32_t x = static_cast<std::uint32_t>(tick) * 0x9E3779B1u;
    x ^= static_cast<std::uint32_t>(seed) * 0x85EBCA77u;
    x ^= x >> 15;
    x *= 0x2545F491u;
    x ^= x >> 13;
    return x;
}

void clamp_ground_err(float& ex, float& ey) noexcept {
    constexpr float k_max_err = 0.75f;
    const float n = std::hypot(ex, ey);
    if (!std::isfinite(n)) {
        ex = 0.0f;
        ey = 0.0f;
        return;
    }
    if (n > k_max_err) {
        const float s = k_max_err / n;
        ex *= s;
        ey *= s;
    }
}

}  // namespace

math::angles apply_antiaim(const config::anti_aim_cfg& cfg, const math::angles& real,
                           const antiaim_state& state) {
    if (!cfg.enabled) return real;

    // Conditional gates: when a state toggle is off, that state falls through
    // to the real angles instead of a fabricated pose.
    if (!state.on_ground && !cfg.on_air) return real;
    if (state.moving && !cfg.on_move) return real;

    math::angles out = real;

    const bool alternate = (state.tick & 1) != 0;
    switch (cfg.base_yaw) {
        case config::yaw_mode::off:
            break;
        case config::yaw_mode::jitter:
            out.yaw = real.yaw + (alternate ? cfg.jitter_range : -cfg.jitter_range);
            break;
        case config::yaw_mode::random: {
            const std::uint32_t r = hash(state.tick, state.seed);
            const float unit = static_cast<float>(r & 0xFFFF) / 65535.0f;  // 0..1
            out.yaw = real.yaw + (unit * 2.0f - 1.0f) * cfg.jitter_range;
            break;
        }
        case config::yaw_mode::fake_static:
            out.yaw = real.yaw + 180.0f;
            break;
    }

    out.yaw = math::normalize_yaw(out.yaw + cfg.yaw_offset);
    return out;
}

float align_aa_yaw_for_ground_move(float camera_yaw, float aa_yaw) noexcept {
    const float rel_yaw = math::yaw_delta(camera_yaw, aa_yaw);
    const float snapped_rel = std::round(rel_yaw / 45.0f) * 45.0f;
    return math::normalize_yaw(camera_yaw + snapped_rel);
}

float pose_head_yaw_offset(int weapon_group, int weapon_type, float pitch_deg) noexcept {
    // Neutral and pitch-up stances do not share the forward-hunched pitch-down weapon twist.
    if (!std::isfinite(pitch_deg) || pitch_deg < 35.0f) return 0.0f;

    bool one_handed = false;
    // CCSWeaponBaseVData::m_WeaponType overrides weapon_group when available:
    // 0 = knife, 1 = pistol, 2..6 = SMG/rifle/shotgun/sniper/MG, 7 = C4, 8 = taser (Zeus), 9 = grenade.
    if (weapon_type >= 0) {
        if (weapon_type == 1 || weapon_type == 8) {
            one_handed = true;
        } else if (weapon_type >= 2 && weapon_type <= 6) {
            one_handed = false;
        } else {
            return 0.0f;
        }
    } else {
        if (weapon_group < 0 || weapon_group >= config::k_weapon_groups) return 0.0f;
        one_handed =
            weapon_group == static_cast<int>(config::weapon_group::pistol) ||
            weapon_group == static_cast<int>(config::weapon_group::heavy_pistol);
    }

    // Calibrated from CS2 live bone measurements (+33.1..+44.2 deg twist on pitch-down rifles).
    const float max_trim = one_handed ? 18.0f : 36.0f;
    const float clamped = std::min(pitch_deg, 89.0f);
    const float scale = 0.75f + 0.25f * ((clamped - 35.0f) / 54.0f);
    return max_trim * scale;
}

float default_pose_head_yaw_offset(int weapon_group, float pitch_deg) noexcept {
    return pose_head_yaw_offset(weapon_group, -1, pitch_deg);
}

math::vec3 freestanding_head_probe(const math::vec3& local_eye, float yaw_deg,
                                   float pitch_deg) noexcept {
    if (!local_eye.finite()) return {};
    const float clamped_pitch = math::clamp_pitch(pitch_deg);
    const float sin_p = std::sin(clamped_pitch * k_deg_to_rad);
    // Keep horizontal probe offset strictly inside the 16u player hull so hugging a wall
    // never embeds the probe behind the wall surface, while tracking vertical head lean.
    const float fwd_dist = (sin_p >= 0.0f) ? (7.0f + 6.5f * sin_p) : (7.0f + 2.0f * sin_p);
    const float z_off = (sin_p >= 0.0f) ? (-7.0f * sin_p) : (2.0f * sin_p);
    const math::vec3 dir = math::forward_from_angles({0.0f, yaw_deg});
    return local_eye + dir * fwd_dist + math::vec3{0.0f, 0.0f, z_off};
}

math::vec3 freestanding_trace_target(const math::vec3& enemy_eye,
                                     const math::vec3& head_probe,
                                     float backoff_units) noexcept {
    if (!enemy_eye.finite()) return {};
    if (!head_probe.finite() || !std::isfinite(backoff_units)) return enemy_eye;
    const double safe_backoff = std::max(0.0, static_cast<double>(backoff_units));
    const double dx = static_cast<double>(head_probe.x) - static_cast<double>(enemy_eye.x);
    const double dy = static_cast<double>(head_probe.y) - static_cast<double>(enemy_eye.y);
    const double dz = static_cast<double>(head_probe.z) - static_cast<double>(enemy_eye.z);
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (safe_backoff > 0.0 && dist > safe_backoff + 4.0) {
        const double step = safe_backoff / dist;
        return {
            static_cast<float>(static_cast<double>(enemy_eye.x) + dx * step),
            static_cast<float>(static_cast<double>(enemy_eye.y) + dy * step),
            static_cast<float>(static_cast<double>(enemy_eye.z) + dz * step),
        };
    }
    return enemy_eye;
}

freestanding_decision select_freestanding_yaw(
    float enemy_yaw, bool center_valid, float center_frac,
    std::span<const freestanding_candidate> candidates,
    freestanding_state& state) noexcept {
    freestanding_decision out{};
    if (!std::isfinite(enemy_yaw) || !center_valid) {
        state = {};
        return out;
    }

    constexpr float k_block_threshold = 0.96f;
    constexpr float k_hold_threshold = 0.98f;
    constexpr float k_side_hysteresis = 0.08f;

    if (center_valid && std::isfinite(center_frac) &&
        center_frac >= 0.0f && center_frac < k_block_threshold) {
        out.center_blocked = true;
        out.tucked = false;
        state = {};
        return out;
    }

    bool has_pos = false;
    bool has_neg = false;
    freestanding_candidate best_pos{};
    freestanding_candidate best_neg{};

    auto better_on_side = [](const freestanding_candidate& a,
                             const freestanding_candidate& b) noexcept {
        if (a.trace_frac < b.trace_frac - 1e-6f) return true;
        if (b.trace_frac < a.trace_frac - 1e-6f) return false;
        return std::fabs(std::fabs(a.offset_deg) - 115.0f) <
               std::fabs(std::fabs(b.offset_deg) - 115.0f);
    };

    for (const auto& cand : candidates) {
        if (!cand.valid || !std::isfinite(cand.offset_deg) || !std::isfinite(cand.trace_frac) ||
            cand.trace_frac < 0.0f || std::fabs(cand.offset_deg) < 1e-3f) {
            continue;
        }
        const int side = cand.offset_deg > 0.0f ? 1 : -1;
        const float thresh = (state.last_side == side) ? k_hold_threshold : k_block_threshold;
        if (cand.trace_frac >= thresh) continue;

        if (side > 0) {
            if (!has_pos || better_on_side(cand, best_pos)) {
                best_pos = cand;
                has_pos = true;
            }
        } else {
            if (!has_neg || better_on_side(cand, best_neg)) {
                best_neg = cand;
                has_neg = true;
            }
        }
    }

    if (!has_pos && !has_neg) {
        state = {};
        return out;
    }

    int chosen_side = 0;
    freestanding_candidate chosen{};
    if (has_pos && !has_neg) {
        chosen_side = 1;
        chosen = best_pos;
    } else if (has_neg && !has_pos) {
        chosen_side = -1;
        chosen = best_neg;
    } else {
        if (state.last_side == 1) {
            if (best_neg.trace_frac + k_side_hysteresis < best_pos.trace_frac) {
                chosen_side = -1;
                chosen = best_neg;
            } else {
                chosen_side = 1;
                chosen = best_pos;
            }
        } else if (state.last_side == -1) {
            if (best_pos.trace_frac + k_side_hysteresis < best_neg.trace_frac) {
                chosen_side = 1;
                chosen = best_pos;
            } else {
                chosen_side = -1;
                chosen = best_neg;
            }
        } else {
            if (std::fabs(best_pos.trace_frac - best_neg.trace_frac) <= 1e-6f) {
                const bool pos_preferred =
                    std::fabs(std::fabs(best_pos.offset_deg) - 115.0f) <=
                    std::fabs(std::fabs(best_neg.offset_deg) - 115.0f);
                chosen_side = pos_preferred ? 1 : -1;
                chosen = pos_preferred ? best_pos : best_neg;
            } else if (best_pos.trace_frac < best_neg.trace_frac) {
                chosen_side = 1;
                chosen = best_pos;
            } else {
                chosen_side = -1;
                chosen = best_neg;
            }
        }
    }

    out.tucked = true;
    out.selected_offset = chosen.offset_deg;
    out.selected_yaw = math::normalize_yaw(enemy_yaw + chosen.offset_deg);
    out.selected_side = chosen_side;

    state.last_side = chosen_side;
    state.last_offset = chosen.offset_deg;
    return out;
}

math::angles apply_antiaim_full(const config::anti_aim_cfg& cfg,
                                const math::angles& camera_view,
                                const antiaim_context& ctx) noexcept {
    const auto passthrough_camera = [&]() noexcept -> math::angles {
        return {math::clamp_pitch(camera_view.pitch), math::normalize_yaw(camera_view.yaw)};
    };

    if (!cfg.enabled || ctx.suppressed) return passthrough_camera();
    if (!ctx.on_ground && !cfg.on_air) return passthrough_camera();

    const float speed_2d = std::isfinite(ctx.speed_2d) ? ctx.speed_2d : 0.0f;
    const bool moving = speed_2d > 5.0f || ctx.has_move_input;
    if (moving && !cfg.on_move) return passthrough_camera();

    // Safe manual fire (cfg.anti_aim.on_shot) aligns on fire ticks in createmove.cpp;
    // keep AA pose steady across ctx.shooting ticks to avoid firing-tick jitter.
    (void)ctx.shooting;

    float pitch = camera_view.pitch;
    switch (cfg.base_pitch) {
        case config::pitch_mode::down:
            pitch = 89.0f;
            break;
        case config::pitch_mode::up:
            pitch = -89.0f;
            break;
        case config::pitch_mode::zero:
            pitch = 0.0f;
            break;
        case config::pitch_mode::off:
        default:
            break;
    }
    pitch = math::clamp_pitch(pitch);

    const float yaw_offset = std::isfinite(cfg.yaw_offset) ? cfg.yaw_offset : 0.0f;
    const float jitter_range = std::isfinite(cfg.jitter_range) ? cfg.jitter_range : 0.0f;

    const bool use_enemy = cfg.at_targets && ctx.found_enemy && std::isfinite(ctx.enemy_yaw);
    const float ref_yaw = use_enemy
        ? math::normalize_yaw(ctx.enemy_yaw)
        : math::normalize_yaw(camera_view.yaw);

    if (cfg.base_yaw == config::yaw_mode::off) {
        return {pitch, math::normalize_yaw(ref_yaw + yaw_offset)};
    }

    float base_dir = math::normalize_yaw(ref_yaw + 180.0f);
    if (cfg.freestanding && ctx.freestanding.tucked &&
        std::isfinite(ctx.freestanding.selected_yaw)) {
        base_dir = math::normalize_yaw(ctx.freestanding.selected_yaw);
    }

    float mode_offset = 0.0f;
    switch (cfg.base_yaw) {
        case config::yaw_mode::fake_static:
            mode_offset = 0.0f;
            break;
        case config::yaw_mode::jitter:
            mode_offset = ((ctx.command_number & 1) != 0) ? jitter_range : -jitter_range;
            break;
        case config::yaw_mode::random: {
            const std::uint32_t r = hash(ctx.command_number, ctx.seed);
            const float unit = static_cast<float>(r & 0xFFFFu) / 65535.0f;
            mode_offset = (unit * 2.0f - 1.0f) * jitter_range;
            break;
        }
        case config::yaw_mode::off:
        default:
            return {pitch, math::normalize_yaw(ref_yaw + yaw_offset)};
    }

    const float adjust = cfg.auto_adjust
        ? pose_head_yaw_offset(ctx.weapon_group, ctx.weapon_type, pitch)
        : 0.0f;
    return {pitch, math::normalize_yaw(base_dir + mode_offset + adjust + yaw_offset)};
}

void compute_quantized_ground_move(const math::vec3& wish_world_dir, float cmd_yaw,
                                   quantized_move_state& state, int command_number,
                                   float& out_fwd, float& out_left) noexcept {
    if (!wish_world_dir.finite() || !std::isfinite(cmd_yaw)) {
        out_fwd = 0.0f;
        out_left = 0.0f;
        state = {};
        state.last_cmd = command_number;
        state.initialized = true;
        return;
    }

    const double raw_wx = static_cast<double>(wish_world_dir.x);
    const double raw_wy = static_cast<double>(wish_world_dir.y);
    const double wish_len = std::hypot(raw_wx, raw_wy);
    const bool has_wish = wish_len > 1e-4;
    const float wx = has_wish ? static_cast<float>(raw_wx / wish_len) : 0.0f;
    const float wy = has_wish ? static_cast<float>(raw_wy / wish_len) : 0.0f;
    const float norm_yaw = math::normalize_yaw(cmd_yaw);

    if (state.initialized && command_number == state.last_cmd) {
        const bool yaw_same = std::fabs(math::yaw_delta(state.last_cmd_yaw, norm_yaw)) <= 1e-3f;
        const bool wish_same = (has_wish == state.had_wish) &&
                               (!has_wish || (std::fabs(wx - state.last_wish_x) <= 1e-4f &&
                                              std::fabs(wy - state.last_wish_y) <= 1e-4f));
        if (yaw_same && wish_same) {
            out_fwd = state.last_fwd;
            out_left = state.last_left;
            return;
        }
        state.err_x = state.base_err_x;
        state.err_y = state.base_err_y;
        state.had_wish = state.prev_had_wish;
        state.last_wish_x = state.prev_wish_x;
        state.last_wish_y = state.prev_wish_y;
    } else {
        const auto cmd_gap =
            static_cast<std::int64_t>(command_number) - static_cast<std::int64_t>(state.last_cmd);
        if (state.initialized && (command_number < state.last_cmd || cmd_gap > 64)) {
            state.err_x = 0.0f;
            state.err_y = 0.0f;
            state.had_wish = false;
            state.last_wish_x = 0.0f;
            state.last_wish_y = 0.0f;
        }
        state.base_err_x = state.err_x;
        state.base_err_y = state.err_y;
        state.prev_had_wish = state.had_wish;
        state.prev_wish_x = state.last_wish_x;
        state.prev_wish_y = state.last_wish_y;
    }

    if (!has_wish) {
        out_fwd = 0.0f;
        out_left = 0.0f;
        state.err_x = 0.0f;
        state.err_y = 0.0f;
        state.last_wish_x = 0.0f;
        state.last_wish_y = 0.0f;
        state.last_cmd_yaw = norm_yaw;
        state.last_fwd = 0.0f;
        state.last_left = 0.0f;
        state.last_cmd = command_number;
        state.had_wish = false;
        state.initialized = true;
        return;
    }

    if (!state.had_wish || (state.last_wish_x * wx + state.last_wish_y * wy) < 0.5f) {
        state.err_x = 0.0f;
        state.err_y = 0.0f;
    } else {
        const float parallel = state.err_x * wx + state.err_y * wy;
        state.err_x -= parallel * wx;
        state.err_y -= parallel * wy;
    }
    clamp_ground_err(state.err_x, state.err_y);

    const float rad = norm_yaw * k_deg_to_rad;
    const float fx = std::cos(rad);
    const float fy = std::sin(rad);
    const float lx = -fy;
    const float ly = fx;

    const float tx = wx + state.err_x;
    const float ty = wy + state.err_y;

    struct cand_move {
        float f;
        float l;
        float inv_len;
    };
    constexpr float k_inv_sqrt2 = 0.70710678118654752440f;
    constexpr cand_move k_cands[8] = {
        { 1.0f,  0.0f, 1.0f},
        {-1.0f,  0.0f, 1.0f},
        { 0.0f,  1.0f, 1.0f},
        { 0.0f, -1.0f, 1.0f},
        { 1.0f,  1.0f, k_inv_sqrt2},
        { 1.0f, -1.0f, k_inv_sqrt2},
        {-1.0f,  1.0f, k_inv_sqrt2},
        {-1.0f, -1.0f, k_inv_sqrt2},
    };

    float best_dot = -1e9f;
    float best_f = 0.0f;
    float best_l = 0.0f;
    float best_dx = 0.0f;
    float best_dy = 0.0f;
    for (const auto& c : k_cands) {
        const float dx = (c.f * fx + c.l * lx) * c.inv_len;
        const float dy = (c.f * fy + c.l * ly) * c.inv_len;
        const float d = tx * dx + ty * dy;
        if (d > best_dot + 1e-6f) {
            best_dot = d;
            best_f = c.f;
            best_l = c.l;
            best_dx = dx;
            best_dy = dy;
        }
    }

    const float proj = best_dx * wx + best_dy * wy;
    state.err_x += proj * wx - best_dx;
    state.err_y += proj * wy - best_dy;
    clamp_ground_err(state.err_x, state.err_y);

    out_fwd = best_f;
    out_left = best_l;

    state.last_wish_x = wx;
    state.last_wish_y = wy;
    state.last_cmd_yaw = norm_yaw;
    state.last_fwd = best_f;
    state.last_left = best_l;
    state.last_cmd = command_number;
    state.had_wish = true;
    state.initialized = true;
}

}  // namespace shogod::features
