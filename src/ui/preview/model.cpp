#include "ui/preview/model.hpp"

#include <iterator>

#include "sdk/bones.hpp"

namespace shogod::ui::preview {
namespace {

model_provider g_provider = nullptr;

model_data template_humanoid() {
    // Matches ctm_sas.vmdl_c A-pose proportions (feet at z=0, facing +X) in the
    // preview's shared sdk::pj_* joint layout.
    model_data m;
    const math::vec3 joints[] = {
        {0.5f, 0.0f, 70.5f},    // 0 head
        {-0.5f, 0.0f, 63.0f},   // 1 neck
        {-0.5f, 0.0f, 51.0f},   // 2 chest
        {-0.5f, 0.0f, 40.0f},   // 3 pelvis
        {-1.0f, 8.0f, 58.0f},   // 4 left shoulder
        {-1.5f, 14.5f, 47.0f},  // 5 left elbow
        {1.0f, 20.5f, 36.0f},   // 6 left hand
        {-1.0f, -8.0f, 58.0f},  // 7 right shoulder
        {-1.5f, -14.5f, 47.0f}, // 8 right elbow
        {1.0f, -20.5f, 36.0f},  // 9 right hand
        {-0.5f, 4.5f, 37.5f},   // 10 left hip
        {0.5f, 5.5f, 20.5f},    // 11 left knee
        {-0.5f, 6.2f, 3.5f},    // 12 left ankle
        {-0.5f, -4.5f, 37.5f},  // 13 right hip
        {0.5f, -5.5f, 20.5f},   // 14 right knee
        {-0.5f, -6.2f, 3.5f},   // 15 right ankle
    };
    static_assert(std::size(joints) == static_cast<std::size_t>(sdk::pj_count));

    m.joint_count = static_cast<int>(std::size(joints));
    for (int i = 0; i < m.joint_count; ++i) m.joints[i] = joints[i];
    m.connection_count = static_cast<int>(std::size(sdk::k_preview_links));
    for (int i = 0; i < m.connection_count; ++i) m.connections[i] = sdk::k_preview_links[i];
    return m;
}

}  // namespace

void set_model_provider(model_provider provider) { g_provider = provider; }

model_data current_model() {
    if (g_provider) {
        model_data live = g_provider();
        if (live.joint_count > 0) return live;
    }
    return template_humanoid();
}

}  // namespace shogod::ui::preview
