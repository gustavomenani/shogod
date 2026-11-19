#pragma once

// Target selection for the ragebot: pure function over a candidate array so it
// is unit-testable without the game.

#include <span>

#include "config/config.hpp"

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

// Fire gate: hitchance and minimum damage thresholds from the weapon config.
bool should_fire(float hitchance_percent, float estimated_damage, const config::rage_weapon& w);

}  // namespace shogod::features
