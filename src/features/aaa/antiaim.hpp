#pragma once

// Antiaim math. Pure: takes the real view angles and the player state, returns
// the angles the cheat sends. Deterministic for a given (tick, seed) so a
// replay of the same tick produces the same output.

#include "config/config.hpp"
#include "features/math/angles.hpp"

namespace shogod::features {

struct antiaim_state {
    bool on_ground = true;
    bool moving = false;
    bool shooting = false;
    int tick = 0;
    int seed = 0;
};

math::angles apply_antiaim(const config::anti_aim_cfg& cfg, const math::angles& real,
                           const antiaim_state& state);

}  // namespace shogod::features
