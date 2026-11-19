#pragma once

// Menu shell: sidebar + header + animated tab content. Pages live in
// ui/pages/page_*.cpp and read/write the same config struct the features use.

#include <array>
#include <string>

#include "config/config.hpp"
#include "imgui.h"

namespace shogod::ui {

enum class tab : int { rage = 0, visuals, misc, configs, settings, count };
inline constexpr int k_tab_count = static_cast<int>(tab::count);

struct status {
    float fps = 0.0f;
    int tick = 0;
    int players = 0;
    int anchors_ok = 0;
    int anchors_total = 0;
    bool in_game = false;
    std::string config_name = "default";
    std::string anchor_detail;
};

struct menu_state {
    bool open = false;
    tab active = tab::rage;
    float open_anim = 0.0f;
    std::array<float, k_tab_count> tab_anim{};
    std::string toast;
    float toast_time = 0.0f;

    void notify(std::string message) {
        toast = std::move(message);
        toast_time = 2.5f;
    }
};

// Draws the whole menu. `cfg` is the live store; pages write through it.
void draw_menu(config::store& cfg, menu_state& state, const status& live);

}  // namespace shogod::ui
