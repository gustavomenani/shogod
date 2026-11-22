#pragma once

// Historical positions used by the ESP ghost. Records are validated at write
// time (alive, finite) and at read time (window, bones present). Nothing here
// allocates.

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

#include "sdk/snapshot.hpp"

namespace shogod::features {

struct backtrack_record {
    std::uint32_t pawn_handle = 0;
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

    // Independent copy of the oldest record for this pawn inside the window.
    [[nodiscard]] std::optional<backtrack_record> best(int player_index, std::uint32_t pawn_handle,
                                                      float window_ms, float now) const;

    [[nodiscard]] int record_count(int player_index) const;

private:
    std::array<std::array<backtrack_record, k_max_records>, sdk::k_max_players + 1> records_{};
    std::array<int, sdk::k_max_players + 1> next_{};
    mutable std::mutex mutex_;
};

}  // namespace shogod::features
