#include "features/rage/target.hpp"

namespace shogod::features {
namespace {

bool eligible(const target_candidate& c, float max_fov, bool require_visible) {
    if (!c.alive) return false;
    if (c.fov_deg > max_fov) return false;
    if (require_visible && !c.visible) return false;
    return true;
}

}  // namespace

int pick_target(std::span<const target_candidate> candidates, config::aim_priority priority,
                float max_fov, bool require_visible, int& cycle_cursor) {
    int best = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const target_candidate& c = candidates[i];
        if (!eligible(c, max_fov, require_visible)) continue;

        if (best < 0) {
            best = static_cast<int>(i);
            continue;
        }

        const target_candidate& b = candidates[static_cast<std::size_t>(best)];
        switch (priority) {
            case config::aim_priority::distance:
                if (c.distance < b.distance) best = static_cast<int>(i);
                break;
            case config::aim_priority::lowest_hp:
                if (c.health < b.health) best = static_cast<int>(i);
                break;
            case config::aim_priority::threat:
                if (c.threat > b.threat) best = static_cast<int>(i);
                break;
            case config::aim_priority::cycle:
                if (c.index < b.index) best = static_cast<int>(i);
                break;
        }
    }

    if (priority == config::aim_priority::cycle && !candidates.empty()) {
        const int count = static_cast<int>(candidates.size());
        for (int step = 0; step < count; ++step) {
            const int idx = (cycle_cursor + step) % count;
            if (eligible(candidates[static_cast<std::size_t>(idx)], max_fov, require_visible)) {
                cycle_cursor = (idx + 1) % count;
                return idx;
            }
        }
        return -1;
    }
    return best;
}

bool should_fire(float hitchance_percent, float estimated_damage, const config::rage_weapon& w) {
    return hitchance_percent >= w.hitchance && estimated_damage >= w.min_damage;
}

}  // namespace shogod::features
