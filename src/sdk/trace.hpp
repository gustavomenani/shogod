#pragma once

// Line-of-sight & bullet penetration trace through CS2's physics query.
// All entry points are resolved dynamically via pattern scanning (no fixed DLL size).

#include <cstddef>
#include <cstdint>

namespace shogod::sdk {

bool init_trace(std::uintptr_t client_base, std::size_t image_size) noexcept;

bool trace_ready() noexcept;
bool trace_fraction(const float start[3], const float end[3], float& fraction) noexcept;
bool trace_fraction_skip(const float start[3], const float end[3], void* skip_entity,
                         float& fraction) noexcept;

struct bullet_impact {
    float damage = 0.0f;
    int hitgroup = 0;
    bool penetrated = false;
};

bool bullet_trace_ready() noexcept;

// Runs CTraceData + ClipTraceToPlayer + TraceBullet. When `penetrate` is false,
// max penetrations is 0 so only unobstructed hits succeed.
bool trace_bullet_ex(const float start[3], const float delta[3], std::uintptr_t local,
                     std::uintptr_t target, int team, float damage, float penetration,
                     float range_modifier, bool penetrate, bullet_impact& impact) noexcept;

inline bool trace_bullet(const float start[3], const float delta[3], std::uintptr_t local,
                         std::uintptr_t target, int team, float damage, float penetration,
                         float range_modifier, bullet_impact& impact) noexcept {
    return trace_bullet_ex(start, delta, local, target, team, damage, penetration,
                           range_modifier, true, impact);
}

// Standalone penetration probe along a direction (e.g. for penetration crosshair).
bool trace_wall_damage(const float start[3], const float dir[3], std::uintptr_t local,
                       int team, float damage, float penetration, float range_modifier,
                       float range, float& out_damage) noexcept;

}  // namespace shogod::sdk
