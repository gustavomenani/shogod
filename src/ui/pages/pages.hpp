#pragma once

// Page entry points. Each page fills the content rect it is given.

#include "config/config.hpp"
#include "imgui.h"
#include "ui/menu.hpp"

namespace shogod::ui::pages {

// Two equal columns that grow vertically with their content; the page scrolls
// in the parent content region.
bool begin_columns(const char* id);
void end_columns();

void rage(config::store& cfg, const status& live);
void antiaim(config::store& cfg);
void visuals(config::store& cfg, const status& live);
void misc(config::store& cfg);
void configs(config::store& cfg, menu_state& state);
void settings(config::store& cfg, const status& live);

}  // namespace shogod::ui::pages
