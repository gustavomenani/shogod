#include "ui/preview/model.hpp"

namespace shogod::ui::preview {
namespace {

model_provider g_provider = nullptr;

model_data template_humanoid() {
    // Standing figure, world units, feet at z=0. Facing +X.
    model_data m;
    const math::vec3 joints[] = {
        {0, 0, 96},     // 0 head
        {0, 0, 84},     // 1 neck
        {0, 0, 66},     // 2 chest
        {0, 0, 50},     // 3 pelvis
        {0, 8, 72},     // 4 left shoulder
        {0, 14, 54},    // 5 left elbow
        {0, 16, 38},    // 6 left hand
        {0, -8, 72},    // 7 right shoulder
        {0, -14, 54},   // 8 right elbow
        {0, -16, 38},   // 9 right hand
        {0, 5, 48},     // 10 left hip
        {0, 7, 26},     // 11 left knee
        {0, 7, 3},      // 12 left foot
        {0, -5, 48},    // 13 right hip
        {0, -7, 26},    // 14 right knee
        {0, -7, 3},     // 15 right foot
    };
    const std::pair<int, int> conns[] = {
        {0, 1}, {1, 2}, {2, 3}, {2, 4}, {4, 5}, {5, 6}, {2, 7}, {7, 8},
        {8, 9}, {3, 10}, {10, 11}, {11, 12}, {3, 13}, {13, 14}, {14, 15},
    };

    m.joint_count = static_cast<int>(std::size(joints));
    for (int i = 0; i < m.joint_count; ++i) m.joints[i] = joints[i];
    m.connection_count = static_cast<int>(std::size(conns));
    for (int i = 0; i < m.connection_count; ++i) m.connections[i] = conns[i];
    return m;
}

}  // namespace

void set_model_provider(model_provider provider) { g_provider = provider; }

model_data current_model() {
    if (g_provider) return g_provider();
    return template_humanoid();
}

}  // namespace shogod::ui::preview
