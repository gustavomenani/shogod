#pragma once

// Historical positions reused by the ragebot. Records are validated at write
// time (alive, finite) and at read time (window, bones present). Nothing here
// allocates.

#include <array>
#include <cstdint>

#include "sdk/snapshot.hpp"

namespace shogod::features {

struct backtrack_record {
    bool valid = false;
    float curtime = 0.0f;
    int bone_count = 0;
    math::vec3 origin{};
    math::vec3 bones[sdk::k_max_bones]{};
};

class backtrack_history {
public:
    static constexpr int k_max_records = 16;

    void reset();
    void push(int player_index, const sdk::player_snapshot& player, float curtime);

    // Newest record inside the window; nullptr when nothing qualifies.
    [[nodiscard]] const backtrack_record* best(int player_index, float window_ms,
                                               float now) const;

    [[nodiscard]] int record_count(int player_index) const;

private:
    std::array<std::array<backtrack_record, k_max_records>, sdk::k_max_players> records_{};
    std::array<int, sdk::k_max_players> next_{};
};

}  // namespace shogod::features
