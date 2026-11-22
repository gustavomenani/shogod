#pragma once

// Lag compensation record store backed by live CHitbox / CHitboxTransformHistory
// captures per player. Used on the game thread inside CreateMove.

#include "sdk/hitboxes.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"
#include "sdk/snapshot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace shogod::features {

inline constexpr float k_lagcomp_tick_ms = 1000.0f / 64.0f;
inline constexpr float k_lagcomp_max_unlag_ms = 200.0f;
inline constexpr int k_lagcomp_max_unlag_ticks = 12;   // 12 * 15.625ms = 187.5ms <= 200ms
inline constexpr int k_lagcomp_max_drift_ticks = 24;   // sv_maxunlag + max correct (~375ms)

[[nodiscard]] inline bool is_shot_in_interval(float last_shot_time, float prev_sim_time,
                                              float sim_time) noexcept {
    return std::isfinite(last_shot_time) && std::isfinite(prev_sim_time) &&
           std::isfinite(sim_time) && prev_sim_time > 0.0f &&
           last_shot_time > prev_sim_time && last_shot_time <= sim_time;
}

[[nodiscard]] inline int lagcomp_max_age_ticks(float window_ms) noexcept {
    if (!std::isfinite(window_ms) || window_ms <= 0.0f) return 0;
    const float clamped = std::clamp(window_ms, 0.0f, k_lagcomp_max_unlag_ms);
    return std::clamp(static_cast<int>(clamped / k_lagcomp_tick_ms), 0,
                      k_lagcomp_max_unlag_ticks);
}

struct lag_record {
    std::uintptr_t pawn = 0;
    std::uint32_t pawn_handle = 0;
    int simulation_tick = 0;
    float simulation_time = 0.0f;
    bool shooting = false;
    bool valid = false;
    math::vec3 origin{};
    sdk::live_hitbox_set hitboxes;
};

class lag_compensation {
public:
    static constexpr int k_max_records = 16;

    inline void reset() noexcept { tracks_ = {}; }

    // Captures or refreshes records for the players in `frame`.
    void update(const sdk::anchors& a, const sdk::netvars& nv,
                const sdk::frame_snapshot& frame) noexcept;

    // Pure ring-buffer insertion used by update() and deterministic unit tests.
    inline bool push_record(int player_index, std::uintptr_t pawn, std::uint32_t pawn_handle,
                            int sim_tick, float sim_time, const math::vec3& origin,
                            sdk::live_hitbox_set&& hb, float last_shot_time,
                            bool has_shot_time) noexcept {
        if (player_index < 0 || player_index > sdk::k_max_players || pawn == 0 ||
            pawn_handle == 0 || !hb.valid) {
            return false;
        }
        if (sim_tick <= 0) {
            if (std::isfinite(sim_time) && sim_time > 0.0f && sim_time < 30000000.0f) {
                sim_tick = static_cast<int>(sim_time * 64.0f + 0.5f);
            }
            if (sim_tick <= 0) return false;
        }

        track& tr = tracks_[static_cast<std::size_t>(player_index)];
        if (tr.pawn != pawn || tr.pawn_handle != pawn_handle) {
            tr = {};
            tr.pawn = pawn;
            tr.pawn_handle = pawn_handle;
        }

        const int prev_idx = (tr.next + k_max_records - 1) % k_max_records;
        const lag_record& prev = tr.records[static_cast<std::size_t>(prev_idx)];
        if (prev.valid && prev.pawn_handle == pawn_handle && sim_tick <= prev.simulation_tick) {
            if (prev.simulation_tick - sim_tick > k_lagcomp_max_drift_ticks) {
                tr = {};
                tr.pawn = pawn;
                tr.pawn_handle = pawn_handle;
            } else {
                return false;
            }
        }

        const float prev_sim_time =
            (prev.valid && prev.pawn_handle == pawn_handle) ? prev.simulation_time : 0.0f;

        lag_record& rec = tr.records[static_cast<std::size_t>(tr.next)];
        tr.next = (tr.next + 1) % k_max_records;
        rec.pawn = pawn;
        rec.pawn_handle = pawn_handle;
        rec.simulation_tick = sim_tick;
        rec.simulation_time = sim_time;
        rec.origin = origin;
        rec.shooting = has_shot_time && is_shot_in_interval(last_shot_time, prev_sim_time, sim_time);
        rec.hitboxes = std::move(hb);
        rec.valid = true;
        return true;
    }

    // Appends up to 3 distinct records (newest, shooting, oldest) inside `window_ms` for `pawn`.
    inline void select(std::uintptr_t pawn, std::uint32_t pawn_handle, int current_tick,
                       float window_ms, std::vector<const lag_record*>& out) const {
        out.clear();
        if (pawn == 0 || pawn_handle == 0) return;
        const int max_age_ticks = lagcomp_max_age_ticks(window_ms);

        for (const track& tr : tracks_) {
            if (tr.pawn != pawn || tr.pawn_handle != pawn_handle) continue;
            const lag_record* newest = nullptr;
            const lag_record* oldest = nullptr;
            const lag_record* shooting = nullptr;

            for (int step = 0; step < k_max_records; ++step) {
                const int idx = (tr.next + k_max_records - 1 - step) % k_max_records;
                const lag_record& r = tr.records[static_cast<std::size_t>(idx)];
                if (!r.valid || r.pawn != pawn || r.pawn_handle != pawn_handle ||
                    r.simulation_tick <= 0) {
                    continue;
                }

                if (!newest) {
                    newest = &r;
                    if (r.shooting) shooting = &r;
                    oldest = &r;
                    continue;
                }

                if (max_age_ticks <= 0) break;
                if (newest->simulation_tick <= 0) break;

                if (current_tick > 0) {
                    if (r.simulation_tick > current_tick + k_lagcomp_max_unlag_ticks) continue;
                    if (current_tick - r.simulation_tick >
                        k_lagcomp_max_unlag_ticks + max_age_ticks) {
                        continue;
                    }
                }

                const int age_from_newest = newest->simulation_tick - r.simulation_tick;
                if (age_from_newest > max_age_ticks) continue;
                if (age_from_newest < 0) {
                    if (r.shooting && !shooting) shooting = &r;
                    continue;
                }

                if (r.shooting && !shooting) shooting = &r;
                oldest = &r;
            }

            if (newest) out.push_back(newest);
            if (shooting && shooting != newest) out.push_back(shooting);
            if (oldest && oldest != newest && oldest != shooting && max_age_ticks > 0) {
                out.push_back(oldest);
            }
            break;
        }
    }

    inline const lag_record* latest(std::uintptr_t pawn) const noexcept {
        for (const track& tr : tracks_) {
            if (tr.pawn != pawn) continue;
            const int idx = (tr.next + k_max_records - 1) % k_max_records;
            const lag_record& r = tr.records[static_cast<std::size_t>(idx)];
            if (r.valid && r.pawn == pawn) return &r;
        }
        return nullptr;
    }

private:
    struct track {
        std::uintptr_t pawn = 0;
        std::uint32_t pawn_handle = 0;
        int next = 0;
        std::array<lag_record, k_max_records> records{};
    };

    std::array<track, sdk::k_max_players + 1> tracks_{};
};

}  // namespace shogod::features
