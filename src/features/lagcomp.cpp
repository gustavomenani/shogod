#include "features/lagcomp.hpp"

#include "sdk/entities.hpp"

#include <algorithm>
#include <cmath>
#include <windows.h>

namespace shogod::features {
namespace {

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

bool read_last_shot_time(const sdk::anchors& a, const sdk::netvars& nv, std::uintptr_t pawn,
                         float& out_last_shot) noexcept {
    out_last_shot = 0.0f;
    if (nv.pawn_weapon_services == 0 || nv.wep_active == 0 || nv.wep_last_shot_time == 0) {
        return false;
    }
    std::uintptr_t svc = 0;
    if (!safe_read(pawn + nv.pawn_weapon_services, svc) || svc == 0) return false;
    std::uint32_t handle = 0;
    if (!safe_read(svc + nv.wep_active, handle)) return false;
    const std::uintptr_t wep = sdk::resolve_handle(a.entity_system, handle);
    if (wep == 0) return false;

    float last_shot = 0.0f;
    if (!safe_read(wep + nv.wep_last_shot_time, last_shot) || !std::isfinite(last_shot)) {
        return false;
    }
    out_last_shot = last_shot;
    return true;
}

}  // namespace

void lag_compensation::update(const sdk::anchors& a, const sdk::netvars& nv,
                              const sdk::frame_snapshot& frame) noexcept {
    for (int i = 0; i < frame.count; ++i) {
        const sdk::player_snapshot& p = frame.players[i];
        if (!p.alive || !p.enemy || p.pawn == frame.local_pawn || p.index < 0 ||
            p.index > sdk::k_max_players) {
            continue;
        }

        track& tr = tracks_[static_cast<std::size_t>(p.index)];
        if (tr.pawn != p.pawn || tr.pawn_handle != p.pawn_handle) {
            tr = {};
            tr.pawn = p.pawn;
            tr.pawn_handle = p.pawn_handle;
        }

        const int prev_idx = (tr.next + k_max_records - 1) % k_max_records;
        const lag_record& prev = tr.records[static_cast<std::size_t>(prev_idx)];

        // Fast-path: check m_flSimulationTime before invoking expensive GetHitboxList + vectors.
        if (nv.ent_sim_time != 0) {
            float live_sim_time = 0.0f;
            if (safe_read(p.pawn + nv.ent_sim_time, live_sim_time) &&
                std::isfinite(live_sim_time) && live_sim_time > 0.0f &&
                live_sim_time < 30000000.0f) {
                const int live_sim_tick = static_cast<int>(live_sim_time * 64.0f + 0.5f);
                if (prev.valid && prev.pawn_handle == p.pawn_handle &&
                    prev.simulation_tick == live_sim_tick && live_sim_tick > 0) {
                    continue;
                }
            }
        }

        sdk::live_hitbox_set hb{};
        if (!sdk::read_pawn_hitboxes(a, nv, p.pawn, hb) || !hb.valid) continue;

        const int eff_sim_tick = hb.simulation_tick > 0 ? hb.simulation_tick : frame.tick;
        const float eff_sim_time = hb.simulation_time > 0.0f ? hb.simulation_time : frame.curtime;

        float last_shot = 0.0f;
        const bool has_shot = read_last_shot_time(a, nv, p.pawn, last_shot);
        push_record(p.index, p.pawn, p.pawn_handle, eff_sim_tick, eff_sim_time, p.origin,
                    std::move(hb), last_shot, has_shot);
    }
}

}  // namespace shogod::features
