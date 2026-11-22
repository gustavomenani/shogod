#pragma once

// Row-major 4x4 installed by Source's world-to-clip transform
// (view_matrix global). Rows [0..2] produce clip.xyz, row 3 produces w.

#include <cmath>

#include "features/math/vec2.hpp"
#include "features/math/vec3.hpp"

namespace shogod::math {

struct mat4 {
    float m[4][4]{};

    constexpr float at(int row, int col) const { return m[row][col]; }
    constexpr float& at(int row, int col) { return m[row][col]; }

    bool finite() const {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (!std::isfinite(m[r][c])) return false;
        return true;
    }

    bool usable_projection() const {
        return finite() &&
               (m[3][0] != 0.0f || m[3][1] != 0.0f || m[3][2] != 0.0f || m[3][3] > 0.001f);
    }

    // World -> screen. Returns false for points behind the camera, on a
    // degenerate w, or when the projection lands outside a sane range.
    bool world_to_screen(const vec3& world, int screen_w, int screen_h, vec2& out) const {
        if (!world.finite() || !finite() || screen_w <= 0 || screen_h <= 0) return false;

        const float clip_x = m[0][0] * world.x + m[0][1] * world.y + m[0][2] * world.z + m[0][3];
        const float clip_y = m[1][0] * world.x + m[1][1] * world.y + m[1][2] * world.z + m[1][3];
        const float clip_z = m[2][0] * world.x + m[2][1] * world.y + m[2][2] * world.z + m[2][3];
        const float w      = m[3][0] * world.x + m[3][1] * world.y + m[3][2] * world.z + m[3][3];

        if (!(w > 0.001f) || !std::isfinite(w)) return false;

        const float ndc_x = clip_x / w;
        const float ndc_y = clip_y / w;
        const float ndc_z = clip_z / w;
        if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)) return false;

        out.x = (ndc_x + 1.0f) * 0.5f * static_cast<float>(screen_w);
        out.y = (1.0f - ndc_y) * 0.5f * static_cast<float>(screen_h);
        return true;
    }
};

}  // namespace shogod::math
