#pragma once

// Skeleton source for the preview panel. In game the SDK registers a provider
// that returns the local pawn's bones; without one (menu host) a template
// humanoid is shown so the material and camera still have something to render.

#include <utility>

#include "features/math/vec3.hpp"

namespace shogod::ui::preview {

inline constexpr int k_model_joints = 20;
inline constexpr int k_model_connections = 24;

struct model_data {
    math::vec3 joints[k_model_joints]{};
    int joint_count = 0;
    std::pair<int, int> connections[k_model_connections]{};
    int connection_count = 0;
};

using model_provider = model_data (*)();

// Installs the in-game source. Passing nullptr restores the template.
void set_model_provider(model_provider provider);

// Current skeleton: provider output when installed, template otherwise.
model_data current_model();

}  // namespace shogod::ui::preview
