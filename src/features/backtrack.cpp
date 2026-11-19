#include "features/backtrack.hpp"

namespace shogod::features {

void backtrack_history::reset() {
    records_ = {};
    next_ = {};
}

void backtrack_history::push(int player_index, const sdk::player_snapshot& player, float curtime) {
    if (player_index < 0 || player_index >= sdk::k_max_players) return;
    if (!player.alive || player.bone_count <= 0 || player.bone_count > sdk::k_max_bones) return;
    if (!player.origin.finite()) return;

    const int slot = next_[static_cast<std::size_t>(player_index)]++ % k_max_records;
    backtrack_record& rec =
        records_[static_cast<std::size_t>(player_index)][static_cast<std::size_t>(slot)];

    rec = {};
    rec.valid = true;
    rec.curtime = curtime;
    rec.bone_count = player.bone_count;
    rec.origin = player.origin;
    for (int i = 0; i < player.bone_count; ++i) rec.bones[i] = player.bones[i];
}

const backtrack_record* backtrack_history::best(int player_index, float window_ms,
                                                float now) const {
    if (player_index < 0 || player_index >= sdk::k_max_players) return nullptr;
    if (window_ms <= 0.0f) return nullptr;

    const float window = window_ms / 1000.0f;
    const backtrack_record* best = nullptr;
    for (const backtrack_record& rec : records_[static_cast<std::size_t>(player_index)]) {
        if (!rec.valid) continue;
        const float age = now - rec.curtime;
        if (age < 0.0f || age > window) continue;
        if (best == nullptr || rec.curtime > best->curtime) best = &rec;
    }
    return best;
}

int backtrack_history::record_count(int player_index) const {
    if (player_index < 0 || player_index >= sdk::k_max_players) return 0;
    int count = 0;
    for (const backtrack_record& rec : records_[static_cast<std::size_t>(player_index)]) {
        if (rec.valid) ++count;
    }
    return count;
}

}  // namespace shogod::features
