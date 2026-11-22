#pragma once

// Entity list iteration and frame-snapshot collection (game thread only).
// Uses the proven CGameEntitySystem chunk layout (64 x 512 x 0x70) and
// runtime schema offsets; zero heap allocations per frame.

#include "features/math/angles.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"
#include "sdk/snapshot.hpp"

#include <cstdint>

namespace shogod::sdk {

struct weapon_info {
    std::uintptr_t entity = 0;
    int def_index = 0;
    int clip = -1;
    int next_primary_tick = -1;
    float next_primary_ratio = 0.0f;
    int next_secondary_tick = -1;
    float next_secondary_ratio = 0.0f;
    int postpone_ready_ticks = 0;
    float postpone_ready_frac = 0.0f;
    bool hauled_back = false;
    bool in_reload = false;
    bool burst_mode = false;
    bool is_revolver = false;
    bool is_full_auto = true;
    int num_bullets = 1;
    int zoom_levels = 0;
    int zoom_level = 0;
    int weapon_type = 0;
    int damage = 0;
    float range = 0.0f;
    float penetration = 0.0f;
    float armor_ratio = 0.0f;
    float range_modifier = 0.0f;
    float headshot_multiplier = 0.0f;
    float spread = 0.0f;
    float cycle_time = 0.0f;
    float next_attack = 0.0f;
    char name[32]{};
};

struct global_vars_view {
    float real_time = 0.0f;
    float current_time = 0.0f;
    float frame_time = 0.0f;
    int tick_count = 0;
    bool valid = false;
};

std::uintptr_t resolve_handle(std::uintptr_t entity_system, std::uint32_t handle) noexcept;
std::uintptr_t entity_by_index(std::uintptr_t entity_system, int index) noexcept;

struct collect_diag {
    int controllers = 0;
    int local_found = 0;
    int local_pawn = 0;
    int pawns = 0;
    int alive = 0;
    int with_bones = 0;
    int local_node = 0;
    std::uintptr_t bone_ptr = 0;
    int bone_count_raw = 0;
    int bone_alloc = 0;
    int bone_stage = 0;
};

void collect_frame(const anchors& a, const netvars& nv, frame_snapshot& out) noexcept;
void suppress_flash_overlay(const netvars& nv, std::uintptr_t local_pawn) noexcept;
const collect_diag& last_collect() noexcept;

bool read_global_vars(const anchors& a, global_vars_view& out) noexcept;
bool read_active_weapon(std::uintptr_t entity_system, const netvars& nv, std::uintptr_t pawn,
                        weapon_info& out) noexcept;
bool read_weapon_accuracy(const anchors& a, std::uintptr_t weapon, float& spread,
                          float& inaccuracy) noexcept;
bool read_weapon_accuracy_predicted(const anchors& a, const netvars& nv, std::uintptr_t weapon,
                                    float& spread, float& inaccuracy) noexcept;
bool compute_aim_punch(const anchors& a, const netvars& nv, std::uintptr_t local_pawn, int tick,
                       float frac, math::angles& out_recoil) noexcept;
bool engine_in_game(const anchors& a) noexcept;
int weapon_group_from_def_index(int def_index) noexcept;

}  // namespace shogod::sdk
