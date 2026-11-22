#pragma once

// Frame snapshot published by the game thread and read by the render thread.
// Readers copy the published buffer before the writer can recycle it.

#include <mutex>
#include <optional>
#include <cstdint>

#include "features/math/mat4.hpp"
#include "features/math/vec3.hpp"

namespace shogod::sdk {

inline constexpr int k_max_players = 64;
inline constexpr int k_max_bones = 32;

struct player_snapshot {
    std::uintptr_t pawn = 0;
    std::uint32_t pawn_handle = 0;
    int index = 0;
    int team = 0;
    int health = 0;
    int armor = 0;
    bool alive = false;
    bool enemy = false;
    bool visible = false;
    bool scoped = false;
    bool defusing = false;
    bool flashing = false;
    bool reloading = false;
    bool has_kit = false;
    bool helmet = false;
    bool helmet_known = false;
    int weapon_id = 0;
    int weapon_family = -1;
    int ammo = -1;
    char name[32]{};
    char weapon_name[32]{};
    math::vec3 origin{};
    math::vec3 bounds_min{};
    math::vec3 bounds_max{};
    bool bounds_valid = false;
    math::vec3 eye{};
    math::vec3 velocity{};
    float speed_2d = 0.0f;
    int bone_count = 0;
    math::vec3 bones[k_max_bones]{};
};

struct frame_snapshot {
    std::uint32_t seq = 0;
    int count = 0;
    int local_index = 0;
    int local_team = 0;
    std::uintptr_t local_pawn = 0;
    std::uintptr_t local_controller = 0;
    int tick = 0;
    bool tick_valid = false;
    float curtime = 0.0f;
    bool in_game = false;
    bool bomb_planted = false;
    float bomb_blow_time = 0.0f;
    int bomb_site = 0;
    math::vec3 bomb_pos{};
    math::vec3 local_origin{};
    math::vec3 local_eye{};
    bool local_eye_valid = false;
    bool local_alive = false;
    math::mat4 view_matrix{};
    player_snapshot players[k_max_players]{};
};

// Writer: game thread. Fills a private copy, bumps the sequence, swaps.
class publisher {
public:
    frame_snapshot& write_buffer() noexcept { return buffers_[write_index_]; }

    // Publishes the write buffer and flips to the other one.
    void publish() noexcept;

    // Reader: render thread. Returns an independent copy of a valid frame.
    [[nodiscard]] std::optional<frame_snapshot> acquire() const noexcept;

private:
    frame_snapshot buffers_[2]{};
    int write_index_ = 0;
    const frame_snapshot* published_ = nullptr;
    mutable std::mutex mutex_;
};

}  // namespace shogod::sdk
