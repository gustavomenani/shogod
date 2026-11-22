#pragma once

// Render-thread ESP, world indicators, and misc HUD overlays.
// Draws from a stable copy of the published frame snapshot.

#include "config/config.hpp"
#include "features/backtrack.hpp"
#include "sdk/snapshot.hpp"
#include "ui/menu.hpp"

namespace shogod::features::esp {

struct screen_box {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool valid = false;
};

// Computes a tight 2D bounding box around a player's origin/head or bone cloud.
screen_box compute_player_box(const sdk::player_snapshot& player, const math::mat4& vm,
                              int screen_w, int screen_h) noexcept;

struct draw_stats {
    int enemies = 0;
    int visible = 0;
    int projected = 0;
    int boxes = 0;
    int arrows = 0;
    int health_bars = 0;
    int armor_bars = 0;
    int weapon_icons = 0;
    int weapon_names = 0;
    int ammo_labels = 0;
    bool operator==(const draw_stats&) const = default;
};

// Draws ESP, world indicators, and misc HUD onto ImGui's background draw list.
draw_stats draw(const config::config_v1& cfg, const sdk::frame_snapshot* frame,
          const backtrack_history& bt, const ui::status& live, int screen_w, int screen_h);

}  // namespace shogod::features::esp
