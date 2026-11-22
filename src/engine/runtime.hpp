#pragma once

// Process-wide state owned by the injected DLL: config store, menu state and
// the live status the menu displays.

#include "config/config.hpp"
#include "features/backtrack.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"
#include "sdk/snapshot.hpp"
#include "ui/menu.hpp"

#include <cstdint>

namespace shogod::runtime {

void init();

config::store& cfg();
ui::menu_state& menu();
ui::status& live();

sdk::publisher& snapshot_publisher();
const sdk::anchors& anchors();
void set_anchors(const sdk::anchors& a);
const sdk::netvars& netvars();
void set_netvars(const sdk::netvars& nv);
features::backtrack_history& backtrack();

// Called once per rendered frame by the Present hook.
void frame_tick();

// Called by feature code when an anchor changes state.
void set_anchor_summary(int ok, int total, std::string detail);

}  // namespace shogod::runtime
