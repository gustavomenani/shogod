#include "features/aaa/antiaim.hpp"

#include <cmath>

namespace shogod::features {
namespace {

std::uint32_t hash(int tick, int seed) {
    std::uint32_t x = static_cast<std::uint32_t>(tick) * 0x9E3779B1u;
    x ^= static_cast<std::uint32_t>(seed) * 0x85EBCA77u;
    x ^= x >> 15;
    x *= 0x2545F491u;
    x ^= x >> 13;
    return x;
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

    switch (cfg.base_pitch) {
        case config::pitch_mode::off: break;
        case config::pitch_mode::down: out.pitch = 89.0f; break;
        case config::pitch_mode::up: out.pitch = -89.0f; break;
        case config::pitch_mode::zero: out.pitch = 0.0f; break;
    }

    const bool alternate = (state.tick & 1) != 0;
    switch (cfg.base_yaw) {
        case config::yaw_mode::off:
            break;
        case config::yaw_mode::desync:
            out.yaw = real.yaw + (alternate ? cfg.desync_angle : -cfg.desync_angle);
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

    if (state.shooting && cfg.on_shot) out.yaw += alternate ? 12.0f : -12.0f;

    out.yaw = math::normalize_yaw(out.yaw);
    out.pitch = math::clamp_pitch(out.pitch);
    return out;
}

}  // namespace shogod::features
