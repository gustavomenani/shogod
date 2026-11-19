#pragma once

// Process-wide state owned by the injected DLL: config store, menu state and
// the live status the menu displays.

#include "config/config.hpp"
#include "ui/menu.hpp"

#include <cstdint>

namespace shogod::runtime {

void init();
void shutdown();

config::store& cfg();
ui::menu_state& menu();
ui::status& live();

// Called once per rendered frame by the Present hook.
void frame_tick();

// Called by feature code when an anchor changes state.
void set_anchor_summary(int ok, int total, std::string detail);

}  // namespace shogod::runtime
