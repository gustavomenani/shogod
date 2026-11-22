#pragma once

// Neverlose-structured page entry points. Every control rendered here maps to
// live config fields and universal right-click keybind targets.

#include "config/config.hpp"
#include "imgui.h"
#include "ui/menu.hpp"

namespace shogod::ui::pages {

bool begin_columns(const char* id);
void end_columns();

void rage(config::store& cfg, const status& live);
void antiaim(config::store& cfg);
void players(config::store& cfg, const status& live);
void world(config::store& cfg, const status& live);
void misc(config::store& cfg);
void configs(config::store& cfg, menu_state& state);
void keybinds_and_settings(config::store& cfg, const status& live);

}  // namespace shogod::ui::pages
