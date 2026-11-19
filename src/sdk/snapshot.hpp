#pragma once

// Frame snapshot published by the game thread and read by the render thread.
// Fixed capacity, no allocation after init, seqlock publication: a reader that
// sees an odd sequence discards the frame instead of tearing.

#include <atomic>
#include <cstdint>

#include "features/math/mat4.hpp"
#include "features/math/vec3.hpp"

namespace shogod::sdk {

inline constexpr int k_max_players = 64;
inline constexpr int k_max_bones = 32;

struct player_snapshot {
    std::uintptr_t pawn = 0;
    int index = 0;
    int team = 0;
    int health = 0;
    bool alive = false;
    bool enemy = false;
    bool visible = false;
    bool scoped = false;
    bool defusing = false;
    bool flashing = false;
    bool reloading = false;
    bool has_kit = false;
    int weapon_id = 0;
    int ammo = 0;
    math::vec3 origin{};
    math::vec3 eye{};
    math::vec3 velocity{};
    float speed_2d = 0.0f;
    int bone_count = 0;
    math::vec3 bones[k_max_bones]{};
};

struct frame_snapshot {
    std::uint32_t seq = 0;
    int count = 0;
    float tick = 0.0f;
    float curtime = 0.0f;
    math::vec3 local_origin{};
    math::vec3 local_eye{};
    math::mat4 view_matrix{};
    player_snapshot players[k_max_players]{};
};

// Writer: game thread. Fills a private copy, bumps the sequence, swaps.
class publisher {
public:
    publisher();

    frame_snapshot& write_buffer() noexcept { return buffers_[write_index_]; }

    // Publishes the write buffer and flips to the other one.
    void publish() noexcept;

    // Reader: render thread. Returns nullptr if the frame is being written.
    [[nodiscard]] const frame_snapshot* acquire() const noexcept;

private:
    frame_snapshot buffers_[2]{};
    std::atomic<int> write_index_{0};
    std::atomic<const frame_snapshot*> published_{nullptr};
};

}  // namespace shogod::sdk
