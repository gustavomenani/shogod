#include "sdk/trace.hpp"

#include "engine/log.hpp"
#include "engine/pattern.hpp"

#include <cmath>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::sdk {
namespace {

using trace_ray_fn = bool (*)(void* physics, void* ray, const float* start, const float* end,
                              void* filter, void* out);

void** g_physics_cell = nullptr;
trace_ray_fn g_trace_ray = nullptr;
bool g_ready = false;

constexpr std::size_t k_trace_size = 0xc0;
constexpr std::size_t k_filter_size = 0x48;
constexpr std::size_t k_shape_size = 0x40;
constexpr std::size_t k_ray_type_offset = 0x28;
constexpr std::size_t k_fraction_offset = 0xac;

struct bullet_layout {
    static constexpr std::size_t data_size = 0x1d60;
    static constexpr std::size_t segment_count = 0;
    static constexpr std::size_t segments = 0x08;
    static constexpr std::size_t segment_capacity = 0x10;
    static constexpr std::size_t inline_segments = 0x18;
    static constexpr std::size_t segment_size = 0x38;
    static constexpr std::size_t impact_count = 0x1c20;
    static constexpr std::size_t impacts = 0x1c28;
    static constexpr std::size_t impact_capacity = 0x1c30;
    static constexpr std::size_t inline_impacts = 0x1c38;
    static constexpr std::size_t impact_size = 0x18;
    static constexpr std::size_t impact_damage = 0x08;
    static constexpr std::size_t impact_segment = 0x12;
    static constexpr std::size_t impact_flags = 0x14;
    static constexpr std::size_t start = 0x1cf8;
    static constexpr std::size_t end = 0x1d04;
    static constexpr std::size_t filter_exclude = 0x10;
    static constexpr std::size_t filter_ignore = 0x20;
    static constexpr std::size_t filter_flags = 0x39;
    static constexpr std::size_t trace_entity = 0x08;
    static constexpr std::size_t trace_hitbox = 0x10;
    static constexpr std::size_t hitbox_group = 0x38;
};

struct bullet_functions {
    using lifecycle = void (*)(void*);
    void* (*filter)(void*, void*, std::uint64_t, std::uint8_t, std::uint8_t);
    lifecycle construct;
    lifecycle destroy;
    void (*setup)(void*, const float*, const float*, void*, int, bool);
    bool (*clip)(void*, void*, void*, const void*, const void*, int);
    void (*sort)(void*, void*, std::int64_t, std::uint8_t);
    void (*rebuild)(void*, const void*);
    void (*simulate)(void*, float, float, float, int, int, void*);
    void (*decode)(void*, void*, float, const void*);
    void** query;
};

bullet_functions g_bullet{};
bool g_bullet_faulted = false;

template <typename T>
T read_field(const unsigned char* base, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, base + offset, sizeof value);
    return value;
}

template <typename T>
void write_field(unsigned char* base, std::size_t offset, T value) noexcept {
    std::memcpy(base + offset, &value, sizeof value);
}

std::uintptr_t find_pat(std::uintptr_t base, std::size_t size, const char* pat) {
    return pattern::find(base, size, pattern::compile(pat));
}

bool init_bullet_trace(std::uintptr_t base, std::size_t size) noexcept {
    bullet_functions entries{};

    entries.filter = reinterpret_cast<decltype(entries.filter)>(find_pat(
        base, size, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B6 41 ? 33 FF 24"));
    entries.construct = reinterpret_cast<decltype(entries.construct)>(find_pat(
        base, size, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8D 79 ? 33 F6 C7 47"));
    if (const auto dtor_call = find_pat(
            base, size, "E8 ? ? ? ? 8B 8D ? ? ? ? 8B C1 48 8B 95 ? ? ? ? 25 FF FF FF 7F")) {
        entries.destroy = reinterpret_cast<decltype(entries.destroy)>(pattern::resolve_rel32(dtor_call, 1));
    }
    entries.setup = reinterpret_cast<decltype(entries.setup)>(find_pat(
        base, size,
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? F2 0F 10 02 4D 8D 71 08 F2 0F 11 81 F8 1C 00 00"));
    entries.clip = reinterpret_cast<decltype(entries.clip)>(find_pat(
        base, size,
        "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC 20 02 00 00"));
    entries.sort = reinterpret_cast<decltype(entries.sort)>(
        find_pat(base, size, "44 88 4C 24 ? 55 53 56"));
    entries.rebuild = reinterpret_cast<decltype(entries.rebuild)>(
        find_pat(base, size, "40 55 56 41 54 41 55 41 56 48 8B EC 48 81 EC 80 00 00 00"));
    entries.simulate = reinterpret_cast<decltype(entries.simulate)>(
        find_pat(base, size, "40 53 57 41 56 48 83 EC ? 8B 84 24"));
    entries.decode = reinterpret_cast<decltype(entries.decode)>(find_pat(
        base, size,
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 81 EC ? ? ? ? 48 8B E9 0F 29 74 24"));
    if (const auto q = find_pat(
            base, size,
            "48 8B 0D ? ? ? ? 4D 8B CF F3 0F 11 74 24 ? 4D 8B C4 C7 44 24 ? ? ? ? ?")) {
        entries.query = reinterpret_cast<void**>(pattern::resolve_rel32(q, 3));
    }

    if (!entries.filter || !entries.construct || !entries.destroy || !entries.setup ||
        !entries.clip || !entries.sort || !entries.rebuild || !entries.simulate ||
        !entries.decode || !entries.query) {
        return false;
    }

    g_bullet = entries;
    return true;
}

bool simulate_bullet(unsigned char* data, const float* start, const float* delta,
                     void* local, void* target, int team, float damage, float penetration,
                     float range_modifier, bool penetrate, bullet_impact& result) noexcept {
    using layout = bullet_layout;
    void* query = *g_bullet.query;
    if (query == nullptr) return false;
    if (read_field<unsigned char*>(data, layout::segments) != data + layout::inline_segments ||
        read_field<unsigned char*>(data, layout::impacts) != data + layout::inline_impacts ||
        read_field<int>(data, layout::segment_count) != 0 ||
        read_field<int>(data, layout::impact_count) != 0) {
        g_bullet_faulted = true;
        return false;
    }
    alignas(16) unsigned char filter[k_filter_size]{};
    alignas(16) unsigned char target_filter[k_filter_size]{};
    constexpr std::uint64_t mask = 0x1c300b;
    const int penetrations = penetrate ? 4 : 0;
    g_bullet.filter(filter, local, mask, 3, 15);
    g_bullet.filter(target_filter, target, mask, 3, 15);
    const auto target_id = read_field<std::uint32_t>(target_filter, layout::filter_ignore);
    if (target_id >= UINT32_MAX - 1) return false;
    write_field(filter, layout::filter_ignore + sizeof(target_id), target_id);
    write_field(filter, layout::filter_flags,
                static_cast<std::uint8_t>(read_field<std::uint8_t>(filter, layout::filter_flags) | 2));
    write_field(filter, layout::filter_exclude,
                read_field<std::uint64_t>(filter, layout::filter_exclude) | UINT64_C(0x4000000000));
    g_bullet.setup(data, start, delta, filter, penetrations, true);
    if (!g_bullet.clip(query, data, target, data + layout::start, data + layout::end, 0)) return false;

    const auto count = read_field<int>(data, layout::segment_count);
    const auto capacity = read_field<int>(data, layout::segment_capacity);
    auto* segments = read_field<unsigned char*>(data, layout::segments);
    if (segments == nullptr || count <= 0 || count > capacity || count > 4096) return false;
    g_bullet.sort(segments, segments + count * layout::segment_size, count, 0);
    g_bullet.rebuild(data, data + layout::end);
    g_bullet.simulate(data, damage, penetration, range_modifier, penetrations, team, nullptr);

    const auto info_count = read_field<int>(data, layout::impact_count);
    const auto info_capacity = read_field<int>(data, layout::impact_capacity);
    const auto* infos = read_field<const unsigned char*>(data, layout::impacts);
    segments = read_field<unsigned char*>(data, layout::segments);
    const auto segment_count = read_field<int>(data, layout::segment_count);
    if (segments == nullptr || segment_count <= 0 ||
        segment_count > read_field<int>(data, layout::segment_capacity) || segment_count > 4096 ||
        infos == nullptr || info_count <= 0 || info_count > info_capacity || info_count > 4096) return false;

    bool penetrated = false;
    for (int i = 0; i < info_count; ++i) {
        const auto* info = infos + i * layout::impact_size;
        const float residual = read_field<float>(info, layout::impact_damage);
        if (!std::isfinite(residual) || residual <= 0.0f || residual > damage + 0.01f) return false;
        if ((read_field<std::uint8_t>(info, layout::impact_flags) & 1) != 0) {
            penetrated = true;
            continue;
        }
        const int index = read_field<std::uint16_t>(info, layout::impact_segment);
        const float fraction = read_field<float>(info, 0);
        if (index >= segment_count || !std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f) return false;
        alignas(16) unsigned char trace[k_trace_size]{};
        g_bullet.decode(data, trace, fraction, segments + index * layout::segment_size);
        const auto* hitbox = read_field<const unsigned char*>(trace, layout::trace_hitbox);
        if (read_field<void*>(trace, layout::trace_entity) != target || hitbox == nullptr) continue;
        const auto group = read_field<int>(hitbox, layout::hitbox_group);
        if (group < 0 || group > 10 || group == 9) return false;
        result = {residual, group, penetrated};
        return true;
    }
    return false;
}

}  // namespace

bool init_trace(std::uintptr_t client_base, std::size_t image_size) noexcept {
    if (g_ready) return true;
    if (client_base == 0 || image_size == 0) return false;

    __try {
        if (const auto ph = find_pat(
                client_base, image_size,
                "48 8B 0D ? ? ? ? 48 8D 45 ? C6 44 24 ? ? 4C 8D 4C 24")) {
            g_physics_cell = reinterpret_cast<void**>(pattern::resolve_rel32(ph, 3));
        }

        const auto trace_ray_addr = find_pat(
            client_base, image_size,
            "48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? B8");
        g_trace_ray = reinterpret_cast<trace_ray_fn>(trace_ray_addr);

        if (init_bullet_trace(client_base, image_size)) {
            log::info("trace: native bullet entries resolved dynamically");
        } else {
            log::warn("trace: native bullet entries unavailable; wallbang disabled");
        }

        g_ready = g_physics_cell != nullptr && g_trace_ray != nullptr && g_bullet.filter != nullptr;
        if (g_ready) log::info("trace: dynamic physics query resolved");
        return g_ready;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log::error("trace: resolving the physics query faulted");
        return false;
    }
}

bool trace_ready() noexcept { return g_ready; }

bool bullet_trace_ready() noexcept { return g_bullet.query != nullptr && !g_bullet_faulted; }

bool trace_bullet_ex(const float start[3], const float delta[3], std::uintptr_t local,
                     std::uintptr_t target, int team, float damage, float penetration,
                     float range_modifier, bool penetrate, bullet_impact& impact) noexcept {
    impact = {};
    if (!bullet_trace_ready() || !start || !delta || !local || !target || local == target ||
        (team != 2 && team != 3) || !std::isfinite(damage) || damage <= 0.0f ||
        !std::isfinite(penetration) || penetration <= 0.0f ||
        !std::isfinite(range_modifier) || range_modifier <= 0.0f || range_modifier > 1.0f) return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(start[axis]) || !std::isfinite(delta[axis])) return false;
    }
    alignas(16) unsigned char data[bullet_layout::data_size]{};
    bool constructed = false;
    bool found = false;
    __try {
        g_bullet.construct(data);
        constructed = true;
        found = simulate_bullet(data, start, delta, reinterpret_cast<void*>(local),
                                reinterpret_cast<void*>(target), team, damage,
                                penetration, range_modifier, penetrate, impact);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        found = false;
        g_bullet_faulted = true;
    }
    if (constructed) {
        __try {
            g_bullet.destroy(data);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            found = false;
            g_bullet_faulted = true;
        }
    }
    if (g_bullet_faulted) log::warn("trace: native bullet query faulted; wallbang disabled");
    if (!found) impact = {};
    return found;
}

bool trace_wall_damage(const float start[3], const float dir[3], std::uintptr_t local,
                       int team, float damage, float penetration, float range_modifier,
                       float range, float& out_damage) noexcept {
    out_damage = 0.0f;
    if (!bullet_trace_ready() || !start || !dir || !local || (team != 2 && team != 3) ||
        damage <= 0.0f || penetration <= 0.0f || range <= 0.0f) {
        return false;
    }
    const float delta[3] = {dir[0] * range, dir[1] * range, dir[2] * range};
    alignas(16) unsigned char data[bullet_layout::data_size]{};
    bool constructed = false;
    bool ok = false;
    __try {
        g_bullet.construct(data);
        constructed = true;
        alignas(16) unsigned char filter[k_filter_size]{};
        g_bullet.filter(filter, reinterpret_cast<void*>(local), 0x1c300bull, 3, 15);
        write_field(filter, bullet_layout::filter_flags,
                    static_cast<std::uint8_t>(read_field<std::uint8_t>(filter, bullet_layout::filter_flags) | 2));
        write_field(filter, bullet_layout::filter_exclude,
                    read_field<std::uint64_t>(filter, bullet_layout::filter_exclude) | UINT64_C(0x4000000000));
        g_bullet.setup(data, start, delta, filter, 4, true);
        g_bullet.simulate(data, damage, penetration, range_modifier, 4, team, nullptr);
        const int info_count = read_field<int>(data, bullet_layout::impact_count);
        const int info_cap = read_field<int>(data, bullet_layout::impact_capacity);
        const auto* infos = read_field<const unsigned char*>(data, bullet_layout::impacts);
        if (infos && info_count > 0 && info_count <= info_cap && info_count <= 4096) {
            for (int i = 0; i < info_count; ++i) {
                const auto* info = infos + i * bullet_layout::impact_size;
                const float dmg = read_field<float>(info, bullet_layout::impact_damage);
                if (!std::isfinite(dmg) || dmg <= 0.0f) break;
                if ((read_field<std::uint8_t>(info, bullet_layout::impact_flags) & 1) == 0) continue;
                out_damage = dmg;
                ok = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (constructed) {
        __try {
            g_bullet.destroy(data);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
    }
    return ok;
}

static bool trace_impl(const float start[3], const float end[3], void* skip_entity,
                       float& fraction) noexcept {
    if (!g_ready || start == nullptr || end == nullptr) return false;

    __try {
        if (g_physics_cell && *g_physics_cell && g_trace_ray && g_bullet.filter) {
            alignas(16) unsigned char ray[k_shape_size]{};
            ray[k_ray_type_offset] = 0;  // CRay::type = line
            alignas(16) unsigned char filter[k_filter_size]{};
            alignas(16) unsigned char out[k_trace_size]{};
            g_bullet.filter(filter, skip_entity, 0x1c300bull, 3, 15);
            g_trace_ray(*g_physics_cell, ray, start, end, filter, out);
            const float raw = *reinterpret_cast<float*>(out + k_fraction_offset);
            if (!std::isfinite(raw) || raw < 0.0f || raw > 1.0f) return false;
            fraction = raw;
            return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool trace_fraction(const float start[3], const float end[3], float& fraction) noexcept {
    return trace_impl(start, end, nullptr, fraction);
}

bool trace_fraction_skip(const float start[3], const float end[3], void* skip_entity,
                         float& fraction) noexcept {
    return trace_impl(start, end, skip_entity, fraction);
}

}  // namespace shogod::sdk
