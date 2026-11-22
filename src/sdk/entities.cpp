#include "sdk/entities.hpp"

#include "engine/log.hpp"
#include "sdk/bones.hpp"
#include "sdk/trace.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::sdk {

collect_diag g_diag{};

namespace {

constexpr int k_max_chunks = 64;
constexpr int k_chunk_entries = 512;
constexpr std::size_t k_identity_stride = 0x70;
constexpr std::size_t k_bone_stride = 0x20;

template <typename T>
bool safe_read(std::uintptr_t addr, T& out) noexcept {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL) return false;
    __try {
        out = *reinterpret_cast<const T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool safe_write(std::uintptr_t addr, const T& val) noexcept {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL) return false;
    __try {
        *reinterpret_cast<T*>(addr) = val;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_read_bytes(std::uintptr_t addr, void* dst, std::size_t bytes) noexcept {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL || bytes == 0) return false;
    __try {
        std::memcpy(dst, reinterpret_cast<const void*>(addr), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void safe_read_cstr(std::uintptr_t addr, char* dst, std::size_t cap) noexcept {
    if (cap == 0) return;
    dst[0] = '\0';
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL) return;
    __try {
        const auto* src = reinterpret_cast<const char*>(addr);
        std::size_t i = 0;
        for (; i + 1 < cap; ++i) {
            const char c = src[i];
            if (c == '\0') break;
            dst[i] = c;
        }
        dst[i] = '\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dst[0] = '\0';
    }
}

std::uintptr_t identity_entry(std::uintptr_t entity_system, int index) noexcept {
    if (entity_system == 0 || index < 0 || index > 0x7fff) return 0;
    const int chunk_idx = index >> 9;
    if (chunk_idx < 0 || chunk_idx >= k_max_chunks) return 0;

    std::uintptr_t chunk = 0;
    if (!safe_read(entity_system + 0x10 + static_cast<std::uintptr_t>(chunk_idx) * 8, chunk) ||
        chunk == 0) {
        return 0;
    }
    return chunk + static_cast<std::uintptr_t>(index & (k_chunk_entries - 1)) * k_identity_stride;
}

void clean_weapon_name(const char* raw, char* dst, std::size_t cap) noexcept {
    if (cap == 0) return;
    dst[0] = '\0';
    if (raw == nullptr || raw[0] == '\0') return;
    const char* s = raw;
    if (std::strncmp(s, "weapon_", 7) == 0) s += 7;
    std::size_t i = 0;
    for (; i + 1 < cap && s[i] != '\0'; ++i) dst[i] = s[i];
    dst[i] = '\0';
}

bool sane_world_vec(const math::vec3& v) noexcept {
    return v.finite() && std::fabs(v.x) < 20000.0f && std::fabs(v.y) < 20000.0f &&
           std::fabs(v.z) < 20000.0f;
}

void read_bones(const netvars& nv, std::uintptr_t scene_node, player_snapshot& player) noexcept {
    player.bone_count = 0;
    if (scene_node == 0 || nv.skel_model_state == 0) return;

    const std::uintptr_t model_state = scene_node + static_cast<std::uintptr_t>(nv.skel_model_state);
    std::uintptr_t bone_array = 0;
    std::uint32_t raw_count = 0;
    std::uint32_t alloc_flags = 0;
    if (!safe_read(model_state + 0x80, bone_array) || bone_array == 0) {
        g_diag.bone_stage = 1;
        return;
    }
    if (!safe_read(model_state + 0x90, alloc_flags) ||
        !safe_read(model_state + 0x94, raw_count)) {
        g_diag.bone_stage = 2;
        g_diag.bone_ptr = bone_array;
        return;
    }
    g_diag.bone_ptr = bone_array;
    g_diag.bone_count_raw = static_cast<int>(raw_count);
    g_diag.bone_alloc = static_cast<int>(alloc_flags);
    g_diag.bone_stage = 3;

    const std::uint32_t capacity = alloc_flags & 0x3FFFFFFFu;
    if (raw_count == 0 || raw_count > capacity || raw_count > 256) return;

    const int count = std::min<int>(static_cast<int>(raw_count), k_max_bones);
    struct alignas(16) bone_entry {
        math::vec3 pos;
        float scale;
        float quat[4];
    };
    static_assert(sizeof(bone_entry) == k_bone_stride);
    bone_entry raw_bones[k_max_bones]{};
    if (!safe_read_bytes(bone_array, raw_bones, static_cast<std::size_t>(count) * k_bone_stride)) {
        player.bone_count = 0;
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (!sane_world_vec(raw_bones[i].pos)) {
            player.bone_count = 0;
            return;
        }
        player.bones[i] = raw_bones[i].pos;
    }
    player.bone_count = count;
}

}  // namespace

const collect_diag& last_collect() noexcept { return g_diag; }

std::uintptr_t resolve_handle(std::uintptr_t entity_system, std::uint32_t handle) noexcept {
    if (handle == 0 || handle == 0xFFFFFFFFu) return 0;
    const int index = static_cast<int>(handle & 0x7fffu);
    const std::uintptr_t entry = identity_entry(entity_system, index);
    if (entry == 0) return 0;

    std::uint32_t serial = 0;
    if (!safe_read(entry + 0x10, serial) || serial != handle) return 0;

    std::uintptr_t ent = 0;
    if (!safe_read(entry, ent)) return 0;
    return ent;
}

std::uintptr_t entity_by_index(std::uintptr_t entity_system, int index) noexcept {
    const std::uintptr_t entry = identity_entry(entity_system, index);
    if (entry == 0) return 0;

    std::uint32_t serial = 0;
    if (!safe_read(entry + 0x10, serial)) return 0;
    if (static_cast<int>(serial & 0x7fffu) != (index & 0x7fff)) return 0;

    std::uintptr_t ent = 0;
    if (!safe_read(entry, ent)) return 0;
    return ent;
}

void suppress_flash_overlay(const netvars& nv, std::uintptr_t local_pawn) noexcept {
    if (local_pawn == 0 || nv.cs_pawn_flash_overlay_alpha <= 0) return;
    const auto address = local_pawn + static_cast<std::uintptr_t>(nv.cs_pawn_flash_overlay_alpha);
    float alpha = 0.0f;
    if (!safe_read(address, alpha) || !std::isfinite(alpha) || alpha <= 0.0f || alpha > 255.0f) {
        return;
    }
    safe_write(address, 0.0f);
}

bool read_global_vars(const anchors& a, global_vars_view& out) noexcept {
    out = {};
    if (a.global_vars_cell == 0) return false;
    std::uintptr_t gv = 0;
    if (!safe_read(a.global_vars_cell, gv) || gv == 0) return false;
    safe_read(gv + 0x00, out.real_time);
    // Verified in client.dll (0x955af0 / 0x3c74a7 / 0x1e3e32):
    // +0x30 = curtime, +0x34 = frametime, +0x40 = bool flag, +0x44 = tick_count.
    safe_read(gv + 0x30, out.current_time);
    safe_read(gv + 0x34, out.frame_time);
    safe_read(gv + 0x44, out.tick_count);
    out.valid = std::isfinite(out.current_time) && out.current_time >= 0.0f && out.tick_count >= 0;
    return out.valid;
}

bool engine_in_game(const anchors& a) noexcept {
    if (a.engine_client == 0) return false;
    __try {
        auto** vt = *reinterpret_cast<void***>(a.engine_client);
        using in_game_fn = bool (*)(void*);
        return reinterpret_cast<in_game_fn>(vt[39])(reinterpret_cast<void*>(a.engine_client));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_active_weapon(std::uintptr_t entity_system, const netvars& nv, std::uintptr_t pawn,
                        weapon_info& out) noexcept {
    out = {};
    if (entity_system == 0 || pawn == 0 || nv.pawn_weapon_services == 0 || nv.wep_active == 0) {
        return false;
    }

    std::uintptr_t services = 0;
    if (!safe_read(pawn + nv.pawn_weapon_services, services) || services == 0) return false;
    if (nv.wep_next_attack != 0) safe_read(services + nv.wep_next_attack, out.next_attack);

    std::uint32_t handle = 0;
    if (!safe_read(services + nv.wep_active, handle)) return false;

    const std::uintptr_t weapon = resolve_handle(entity_system, handle);
    if (weapon == 0) return false;
    out.entity = weapon;

    if (nv.wep_clip1 != 0) safe_read(weapon + nv.wep_clip1, out.clip);
    int next_primary_tick = -1;
    if (nv.wep_next_primary_tick != 0 &&
        safe_read(weapon + nv.wep_next_primary_tick, next_primary_tick) && next_primary_tick >= 0) {
        out.next_primary_tick = next_primary_tick;
    }
    if (nv.wep_next_primary_ratio != 0) safe_read(weapon + nv.wep_next_primary_ratio, out.next_primary_ratio);
    int next_secondary_tick = -1;
    if (nv.wep_next_secondary_tick != 0 &&
        safe_read(weapon + nv.wep_next_secondary_tick, next_secondary_tick) &&
        next_secondary_tick >= 0) {
        out.next_secondary_tick = next_secondary_tick;
    }
    if (nv.wep_next_secondary_ratio != 0) safe_read(weapon + nv.wep_next_secondary_ratio, out.next_secondary_ratio);
    if (nv.wep_postpone_ready_ticks != 0) safe_read(weapon + nv.wep_postpone_ready_ticks, out.postpone_ready_ticks);
    if (nv.wep_postpone_ready_frac != 0) safe_read(weapon + nv.wep_postpone_ready_frac, out.postpone_ready_frac);
    if (nv.wep_hauled_back != 0) {
        std::uint8_t b = 0;
        if (safe_read(weapon + nv.wep_hauled_back, b)) out.hauled_back = (b != 0);
    }
    if (nv.wep_in_reload != 0) {
        std::uint8_t b = 0;
        if (safe_read(weapon + nv.wep_in_reload, b)) out.in_reload = (b != 0);
    }
    if (nv.wep_burst_mode != 0) {
        std::uint8_t b = 0;
        if (safe_read(weapon + nv.wep_burst_mode, b)) out.burst_mode = (b != 0);
    }
    if (nv.wep_zoom_level != 0) safe_read(weapon + nv.wep_zoom_level, out.zoom_level);

    if (nv.wep_attr_manager != 0 && nv.attr_item != 0 && nv.item_def_index != 0) {
        std::uint16_t def = 0;
        if (safe_read(weapon + nv.wep_attr_manager + nv.attr_item + nv.item_def_index, def)) {
            out.def_index = static_cast<int>(def);
        }
    }

    if (nv.ent_subclass_id != 0) {
        std::uintptr_t vdata = 0;
        if (safe_read(weapon + nv.ent_subclass_id + 0x8, vdata) && vdata != 0) {
            if (nv.vdata_weapon_type != 0) safe_read(vdata + nv.vdata_weapon_type, out.weapon_type);
            if (nv.vdata_is_revolver != 0) {
                std::uint8_t b = 0;
                if (safe_read(vdata + nv.vdata_is_revolver, b)) out.is_revolver = (b != 0);
            }
            if (nv.vdata_is_full_auto != 0) {
                std::uint8_t b = 0;
                if (safe_read(vdata + nv.vdata_is_full_auto, b)) out.is_full_auto = (b != 0);
            }
            if (nv.vdata_num_bullets != 0) {
                int nb = 1;
                if (safe_read(vdata + nv.vdata_num_bullets, nb) && nb >= 1 && nb <= 16) {
                    out.num_bullets = nb;
                }
            }
            if (nv.vdata_zoom_levels != 0) safe_read(vdata + nv.vdata_zoom_levels, out.zoom_levels);
            if (nv.vdata_damage != 0) safe_read(vdata + nv.vdata_damage, out.damage);
            if (nv.vdata_range != 0) safe_read(vdata + nv.vdata_range, out.range);
            if (nv.vdata_penetration != 0) safe_read(vdata + nv.vdata_penetration, out.penetration);
            if (nv.vdata_armor_ratio != 0) safe_read(vdata + nv.vdata_armor_ratio, out.armor_ratio);
            if (nv.vdata_range_modifier != 0) safe_read(vdata + nv.vdata_range_modifier, out.range_modifier);
            if (nv.vdata_headshot_multiplier != 0) safe_read(vdata + nv.vdata_headshot_multiplier, out.headshot_multiplier);
            if (nv.vdata_spread != 0) safe_read(vdata + nv.vdata_spread, out.spread);
            if (nv.vdata_cycle_time != 0) safe_read(vdata + nv.vdata_cycle_time, out.cycle_time);
            if (nv.vdata_name != 0) {
                std::uintptr_t name_ptr = 0;
                if (safe_read(vdata + nv.vdata_name, name_ptr) && name_ptr != 0) {
                    char raw[48]{};
                    safe_read_cstr(name_ptr, raw, sizeof raw);
                    clean_weapon_name(raw, out.name, sizeof out.name);
                }
            }
        }
    }
    return true;
}

bool read_weapon_accuracy(const anchors& a, std::uintptr_t weapon, float& out_spread,
                          float& out_inaccuracy) noexcept {
    if (weapon == 0 || a.weapon_spread == 0 || a.weapon_inaccuracy == 0) return false;
    using spread_fn = float (*)(void*);
    using inaccuracy_fn = float (*)(void*, float*, float*);
    const auto weapon_ptr = reinterpret_cast<void*>(weapon);
    __try {
        float movement = 0.0f;
        float air = 0.0f;
        const float spread = reinterpret_cast<spread_fn>(a.weapon_spread)(weapon_ptr);
        const float inaccuracy = reinterpret_cast<inaccuracy_fn>(a.weapon_inaccuracy)(
            weapon_ptr, &movement, &air);
        if (!std::isfinite(spread) || !std::isfinite(inaccuracy) || spread < 0.0f ||
            inaccuracy < 0.0f || spread + inaccuracy > 1.0f) return false;
        out_spread = spread;
        out_inaccuracy = inaccuracy;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_weapon_accuracy_predicted(const anchors& a, const netvars& nv, std::uintptr_t weapon,
                                    float& spread, float& inaccuracy) noexcept {
    if (weapon == 0) return false;
    if (a.update_accuracy_penalty != 0 && nv.wep_accuracy_penalty != 0 && nv.wep_recoil_index != 0) {
        float saved_penalty = 0.0f;
        float saved_recoil = 0.0f;
        const bool had_p = safe_read(weapon + nv.wep_accuracy_penalty, saved_penalty);
        const bool had_r = safe_read(weapon + nv.wep_recoil_index, saved_recoil);
        using update_fn = void (*)(void*);
        __try {
            reinterpret_cast<update_fn>(a.update_accuracy_penalty)(reinterpret_cast<void*>(weapon));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        const bool ok = read_weapon_accuracy(a, weapon, spread, inaccuracy);
        if (had_p) safe_write(weapon + nv.wep_accuracy_penalty, saved_penalty);
        if (had_r) safe_write(weapon + nv.wep_recoil_index, saved_recoil);
        if (ok) return true;
    }
    return read_weapon_accuracy(a, weapon, spread, inaccuracy);
}

bool compute_aim_punch(const anchors& a, const netvars& nv, std::uintptr_t local_pawn, int tick,
                       float frac, math::angles& out_recoil) noexcept {
    out_recoil = {};
    if (a.compute_aim_punch == 0 || nv.cs_pawn_aim_punch_services == 0 || local_pawn == 0) {
        return false;
    }
    std::uintptr_t svc = 0;
    if (!safe_read(local_pawn + nv.cs_pawn_aim_punch_services, svc) || svc == 0) return false;

    struct taia_t {
        int tick;
        float fraction;
    } time{tick, frac};

    struct vec3_pod {
        float x, y, z;
    } punch{};

    using recoil_fn = void (*)(std::uintptr_t, vec3_pod*, const taia_t*, bool);
    __try {
        reinterpret_cast<recoil_fn>(a.compute_aim_punch)(svc, &punch, &time, true);
        if (!std::isfinite(punch.x) || !std::isfinite(punch.y)) return false;
        out_recoil = {punch.x, punch.y};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int weapon_group_from_def_index(int def_index) noexcept {
    switch (def_index) {
        case 1:   // Desert Eagle
        case 64:  // R8 Revolver
            return 1;  // heavy_pistol
        case 2: case 3: case 4: case 30: case 31: case 32: case 36: case 61: case 63:
            return 0;  // pistol (includes Zeus x27)
        case 17: case 19: case 23: case 24: case 26: case 33: case 34:
            return 2;  // smg
        case 40:  // SSG 08
            return 4;  // scout
        case 11:  // G3SG1
        case 38:  // SCAR-20
            return 5;  // auto_sniper
        case 9:   // AWP
            return 6;  // awp
        case 14: case 25: case 27: case 28: case 29: case 35:
            return 7;  // heavy & shotguns
        default:
            return 3;  // rifle
    }
}

void collect_frame(const anchors& a, const netvars& nv, frame_snapshot& out) noexcept {
    out.count = 0;
    out.spectator_count = 0;
    out.local_index = 0;
    out.local_team = 0;
    out.local_pawn = 0;
    out.local_controller = 0;
    out.in_game = false;
    out.tick = 0;
    out.tick_valid = false;
    out.curtime = 0.0f;
    out.local_origin = {};
    out.local_eye = {};
    out.local_eye_valid = false;
    out.local_alive = false;
    out.bomb_planted = false;
    out.view_matrix = math::mat4{};

    if (a.view_matrix_ok && a.view_matrix != 0) {
        math::mat4 vm{};
        if (safe_read_bytes(a.view_matrix, &vm.m[0][0], sizeof vm.m) && vm.usable_projection()) {
            out.view_matrix = vm;
        }
    }

    if (!a.entities_ok || a.entity_system == 0 || !nv.valid) return;

    g_diag = {};

    std::uintptr_t local_controller = 0;
    for (int i = 1; i <= k_max_players; ++i) {
        const std::uintptr_t ctrl = entity_by_index(a.entity_system, i);
        if (ctrl == 0) continue;
        ++g_diag.controllers;

        std::uint8_t is_local = 0;
        if (safe_read(ctrl + nv.ctrl_is_local, is_local) && is_local != 0) {
            local_controller = ctrl;
            out.local_index = i;
            break;
        }
    }

    global_vars_view gv{};
    const bool has_gv = read_global_vars(a, gv);

    if (local_controller != 0) {
        out.local_controller = local_controller;
        ++g_diag.local_found;
        std::uint32_t local_handle = 0;
        if (safe_read(local_controller + nv.ctrl_player_pawn, local_handle)) {
            out.local_pawn = resolve_handle(a.entity_system, local_handle);
        }
        if (out.local_pawn != 0) ++g_diag.local_pawn;
        if (nv.ctrl_tick_base != 0) {
            int tick = 0;
            if (safe_read(local_controller + nv.ctrl_tick_base, tick) && tick >= 0) {
                out.tick = tick;
                out.tick_valid = true;
                const float tick_curtime = static_cast<float>(tick) * (1.0f / 64.0f);
                out.curtime = (has_gv && gv.current_time > 0.0f)
                                  ? std::max(gv.current_time, tick_curtime)
                                  : tick_curtime;
            }
        }
        if (out.local_pawn != 0) {
            out.in_game = true;
            int local_health = 0;
            std::uint8_t local_life = 0xff;
            out.local_alive = safe_read(out.local_pawn + nv.ent_health, local_health) &&
                              safe_read(out.local_pawn + nv.ent_life_state, local_life) &&
                              local_health > 0 && local_life == 0;
            std::uint8_t local_team = 0;
            if (safe_read(out.local_pawn + nv.ent_team, local_team)) out.local_team = local_team;

            std::uintptr_t node = 0;
            if (safe_read(out.local_pawn + nv.ent_scene_node, node) && node != 0) {
                ++g_diag.local_node;
                math::vec3 org{};
                if (safe_read(node + nv.node_abs_origin, org) && sane_world_vec(org)) {
                    out.local_origin = org;
                    math::vec3 view_off{};
                    if (nv.pawn_view_offset != 0 &&
                        safe_read(out.local_pawn + nv.pawn_view_offset, view_off) &&
                        sane_world_vec(org + view_off)) {
                        out.local_eye = org + view_off;
                        out.local_eye_valid = true;
                    }
                }
            }
        }
    }

    int n = 0;
    for (int i = 1; i <= k_max_players && n < k_max_players; ++i) {
        const std::uintptr_t ctrl = entity_by_index(a.entity_system, i);
        if (ctrl == 0) continue;

        std::uint32_t pawn_handle = 0;
        if (!safe_read(ctrl + nv.ctrl_player_pawn, pawn_handle)) continue;
        const std::uintptr_t pawn = resolve_handle(a.entity_system, pawn_handle);
        if (pawn == 0) continue;
        ++g_diag.pawns;

        int health = 0;
        std::uint8_t team = 0;
        std::uint8_t life_state = 2;
        if (!safe_read(pawn + nv.ent_health, health)) continue;
        if (!safe_read(pawn + nv.ent_team, team)) continue;
        safe_read(pawn + nv.ent_life_state, life_state);

        if (health <= 0 || life_state != 0) continue;
        ++g_diag.alive;

        std::uintptr_t scene_node = 0;
        if (!safe_read(pawn + nv.ent_scene_node, scene_node) || scene_node == 0) continue;

        math::vec3 origin{};
        if (!safe_read(scene_node + nv.node_abs_origin, origin) || !sane_world_vec(origin)) {
            continue;
        }

        player_snapshot& p = out.players[n];
        p = {};
        p.pawn = pawn;
        p.pawn_handle = pawn_handle;
        p.index = i;
        p.team = team;
        p.health = std::clamp(health, 0, 999);
        p.alive = true;
        p.enemy = (out.local_team != 0) ? (team != out.local_team) : (pawn != out.local_pawn);
        p.origin = origin;

        std::uintptr_t collision = 0;
        if (nv.ent_collision > 0 && nv.collision_mins > 0 && nv.collision_maxs > 0 &&
            safe_read(pawn + nv.ent_collision, collision) && collision != 0) {
            math::vec3 mins{}, maxs{};
            if (safe_read(collision + nv.collision_mins, mins) &&
                safe_read(collision + nv.collision_maxs, maxs) &&
                mins.finite() && maxs.finite() &&
                mins.x < maxs.x && mins.y < maxs.y && mins.z < maxs.z &&
                (maxs - mins).length_sq() < 256.0f * 256.0f &&
                mins.length_sq() < 256.0f * 256.0f && maxs.length_sq() < 256.0f * 256.0f) {
                p.bounds_min = mins;
                p.bounds_max = maxs;
                p.bounds_valid = true;
            }
        }

        math::vec3 view_off{};
        if (safe_read(pawn + nv.pawn_view_offset, view_off) && view_off.finite()) {
            p.eye = origin + view_off;
        } else {
            p.eye = origin + math::vec3{0.0f, 0.0f, 64.0f};
        }

        math::vec3 vel{};
        if (safe_read(pawn + nv.ent_velocity, vel) && vel.finite()) {
            p.velocity = vel;
            p.speed_2d = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        }

        if (nv.cs_pawn_armor != 0) safe_read(pawn + nv.cs_pawn_armor, p.armor);
        if (nv.cs_pawn_scoped != 0) {
            std::uint8_t b = 0;
            if (safe_read(pawn + nv.cs_pawn_scoped, b)) p.scoped = (b != 0);
        }
        if (nv.cs_pawn_defusing != 0) {
            std::uint8_t b = 0;
            if (safe_read(pawn + nv.cs_pawn_defusing, b)) p.defusing = (b != 0);
        }
        if (nv.cs_pawn_flash_duration != 0) {
            float fl = 0.0f;
            if (safe_read(pawn + nv.cs_pawn_flash_duration, fl)) p.flashing = (fl > 0.1f);
        }
        if (nv.cs_pawn_item_services != 0) {
            std::uintptr_t items = 0;
            if (safe_read(pawn + nv.cs_pawn_item_services, items) && items != 0) {
                std::uint8_t b = 0;
                if (nv.item_has_defuser && safe_read(items + nv.item_has_defuser, b)) p.has_kit = b != 0;
                if (nv.item_has_helmet && safe_read(items + nv.item_has_helmet, b) && b <= 1) {
                    p.helmet = b != 0;
                    p.helmet_known = true;
                }
            }
        }
        if (nv.ctrl_player_name != 0) {
            safe_read_cstr(ctrl + nv.ctrl_player_name, p.name, sizeof p.name);
        }
        if (nv.ctrl_ping != 0) {
            safe_read(ctrl + nv.ctrl_ping, p.ping);
        }
        if (nv.ctrl_money_services != 0 && nv.money_account != 0) {
            std::uintptr_t money_svc = 0;
            if (safe_read(ctrl + nv.ctrl_money_services, money_svc) && money_svc != 0) {
                safe_read(money_svc + nv.money_account, p.money);
            }
        }

        weapon_info w{};
        if (read_active_weapon(a.entity_system, nv, pawn, w)) {
            p.weapon_id = w.def_index;
            if (w.def_index > 0) p.weapon_family = weapon_group_from_def_index(w.def_index);
            p.ammo = w.clip;
            p.reloading = w.in_reload;
            std::memcpy(p.weapon_name, w.name, sizeof p.weapon_name);
        }

        if (nv.cs_pawn_spotted_state != 0 && nv.spotted_by_mask != 0 &&
            out.local_index > 0 && out.local_index < 64) {
            std::uint32_t mask[2]{};
            const std::uintptr_t state = pawn + nv.cs_pawn_spotted_state + nv.spotted_by_mask;
            if (safe_read_bytes(state, &mask[0], sizeof mask)) {
                const int slot = out.local_index - 1;
                const int word = slot / 32;
                const int bit = slot % 32;
                p.visible = ((mask[word] >> bit) & 1u) != 0;
            }
        }

        read_bones(nv, scene_node, p);
        if (p.bone_count > 0) ++g_diag.with_bones;

        if (p.enemy && trace_ready() && out.local_eye_valid) {
            const math::vec3 head =
                (p.bone_count > sdk::bone::head) ? p.bones[sdk::bone::head] : p.eye;
            const math::vec3 delta = head - out.local_eye;
            const float dist = delta.length();
            if (std::isfinite(dist) && dist > 1.0f) {
                const math::vec3 dir = delta / dist;
                const math::vec3 begin = dist > 28.0f ? out.local_eye + dir * 16.0f : out.local_eye;
                const math::vec3 end = dist > 28.0f ? head - dir * 12.0f : head;
                const float s[3] = {begin.x, begin.y, begin.z};
                const float e[3] = {end.x, end.y, end.z};
                float fraction = 0.0f;
                if (trace_fraction_skip(s, e, reinterpret_cast<void*>(pawn), fraction)) {
                    p.visible = fraction == 1.0f;
                }
            }
        }

        ++n;
    }
    out.count = n;
    out.in_game = out.in_game || n > 0;

    // Collect spectators currently watching our local pawn (or the pawn we're observing).
    if (local_controller != 0 && nv.pawn_observer_services != 0 && nv.obs_target != 0) {
        std::uintptr_t watched_pawn = out.local_pawn;
        if (!out.local_alive && nv.ctrl_observer_pawn != 0) {
            std::uint32_t local_obs_h = 0;
            if (safe_read(local_controller + nv.ctrl_observer_pawn, local_obs_h)) {
                const std::uintptr_t local_obs_pawn = resolve_handle(a.entity_system, local_obs_h);
                if (local_obs_pawn != 0) {
                    std::uintptr_t obs_svc = 0;
                    std::uint32_t target_h = 0;
                    if (safe_read(local_obs_pawn + nv.pawn_observer_services, obs_svc) &&
                        obs_svc != 0 && safe_read(obs_svc + nv.obs_target, target_h)) {
                        const std::uintptr_t target_pawn = resolve_handle(a.entity_system, target_h);
                        if (target_pawn != 0) watched_pawn = target_pawn;
                    }
                }
            }
        }
        if (watched_pawn != 0) {
            int sc = 0;
            for (int i = 1; i <= k_max_players && sc < k_max_spectators; ++i) {
                const std::uintptr_t ctrl = entity_by_index(a.entity_system, i);
                if (ctrl == 0 || ctrl == local_controller) continue;

                std::uintptr_t obs_pawn = 0;
                std::uint32_t h = 0;
                if (safe_read(ctrl + nv.ctrl_player_pawn, h) && h != 0) {
                    obs_pawn = resolve_handle(a.entity_system, h);
                }
                if (obs_pawn == 0 && nv.ctrl_observer_pawn != 0 &&
                    safe_read(ctrl + nv.ctrl_observer_pawn, h) && h != 0) {
                    obs_pawn = resolve_handle(a.entity_system, h);
                }
                if (obs_pawn == 0) continue;

                int hp = 0;
                std::uint8_t life = 2;
                if (safe_read(obs_pawn + nv.ent_health, hp) &&
                    safe_read(obs_pawn + nv.ent_life_state, life) && hp > 0 && life == 0) {
                    // Skip living pawns unless their observer service is explicitly active.
                    std::uint8_t mt = 0;
                    if (nv.ent_actual_move_type == 0 ||
                        !safe_read(obs_pawn + nv.ent_actual_move_type, mt) || mt != 8) {
                        continue;
                    }
                }

                std::uintptr_t obs_svc = 0;
                if (!safe_read(obs_pawn + nv.pawn_observer_services, obs_svc) || obs_svc == 0) {
                    continue;
                }
                std::uint32_t target_h = 0;
                if (!safe_read(obs_svc + nv.obs_target, target_h)) continue;
                const std::uintptr_t target_pawn = resolve_handle(a.entity_system, target_h);
                if (target_pawn != watched_pawn) continue;

                int mode = 0;
                if (nv.obs_mode != 0) safe_read(obs_svc + nv.obs_mode, mode);
                // Ignore mode 0 (none) / roam when not actively locking onto target.
                if (mode != 2 && mode != 3) continue;

                spectator_snapshot& sp = out.spectators[sc];
                sp = {};
                sp.mode = mode;
                std::uint8_t team = 0;
                if (safe_read(ctrl + nv.ent_team, team)) sp.team = team;
                if (nv.ctrl_player_name != 0) {
                    safe_read_cstr(ctrl + nv.ctrl_player_name, sp.name, sizeof sp.name);
                }
                if (sp.name[0] != '\0') ++sc;
            }
            out.spectator_count = sc;
        }
    }

    if (nv.id_designer_name != 0 && nv.c4_blow != 0) {
        static int cached_c4_index = -1;
        static int scan_phase = 0;

        auto inspect_c4 = [&](int idx) -> bool {
            const std::uintptr_t ent = entity_by_index(a.entity_system, idx);
            if (ent == 0) return false;
            const std::uintptr_t entry = identity_entry(a.entity_system, idx);
            std::uintptr_t name_ptr = 0;
            if (!safe_read(entry + nv.id_designer_name, name_ptr) || name_ptr == 0) return false;

            char cls[32]{};
            safe_read_cstr(name_ptr, cls, sizeof cls);
            if (std::strcmp(cls, "planted_c4") != 0) return false;

            std::uint8_t ticking = 1;
            std::uint8_t defused = 0;
            if (nv.c4_ticking != 0) safe_read(ent + nv.c4_ticking, ticking);
            if (nv.c4_defused != 0) safe_read(ent + nv.c4_defused, defused);
            if (ticking == 0 || defused != 0) return true;  // found finished C4; stop scan

            float blow = 0.0f;
            safe_read(ent + nv.c4_blow, blow);
            int site = 0;
            if (nv.c4_site != 0) safe_read(ent + nv.c4_site, site);

            std::uintptr_t c4_node = 0;
            math::vec3 c4_pos{};
            if (safe_read(ent + nv.ent_scene_node, c4_node) && c4_node != 0) {
                safe_read(c4_node + nv.node_abs_origin, c4_pos);
            }
            if (sane_world_vec(c4_pos)) {
                out.bomb_planted = true;
                out.bomb_blow_time = blow;
                out.bomb_site = site;
                out.bomb_pos = c4_pos;
            }
            return true;
        };

        if (cached_c4_index >= 65 && inspect_c4(cached_c4_index)) {
            // Fast O(1) cache hit.
        } else {
            cached_c4_index = -1;
            if ((scan_phase++ & 7) == 0) {
                for (int i = 65; i < 2048; ++i) {
                    if (inspect_c4(i)) {
                        cached_c4_index = i;
                        break;
                    }
                }
            }
        }
    }
}

}  // namespace shogod::sdk
