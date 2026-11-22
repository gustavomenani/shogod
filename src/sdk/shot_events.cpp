#include "sdk/shot_events.hpp"

#include "engine/log.hpp"
#include "features/audio/hitsounds.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string_view>

#include <windows.h>

namespace shogod::sdk {
namespace {

bool ptr_in_client(std::uintptr_t p, std::uintptr_t base, std::size_t size) noexcept {
    return base != 0 && size != 0 && p >= base && p < base + size;
}

constexpr std::uint32_t event_hash(std::string_view key) noexcept {
    constexpr std::uint32_t m = 0x5bd1e995;
    std::uint32_t hash = 0x31415926u ^ static_cast<std::uint32_t>(key.size());
    while (key.size() >= 4) {
        std::uint32_t word = 0;
        for (unsigned i = 0; i < 4; ++i) word |= std::uint32_t{static_cast<unsigned char>(key[i])} << (i * 8);
        word *= m;
        word ^= word >> 24;
        word *= m;
        hash = (hash * m) ^ word;
        key.remove_prefix(4);
    }
    for (unsigned i = 0; i < key.size(); ++i) {
        hash ^= std::uint32_t{static_cast<unsigned char>(key[i])} << (i * 8);
    }
    if (!key.empty()) hash *= m;
    hash ^= hash >> 13;
    hash *= m;
    return hash ^ (hash >> 15);
}

struct event_key {
    std::uint32_t hash;
    std::uint32_t reserved = 0;
    const char* name;
};

constexpr event_key k_actor{event_hash("userid"), 0, "userid"};
constexpr event_key k_attacker{event_hash("attacker"), 0, "attacker"};
constexpr event_key k_health{event_hash("health"), 0, "health"};
constexpr event_key k_damage{event_hash("dmg_health"), 0, "dmg_health"};
constexpr event_key k_hitgroup{event_hash("hitgroup"), 0, "hitgroup"};

std::atomic<std::uintptr_t> g_local_controller{0};
std::atomic<std::uintptr_t> g_client_base{0};
std::atomic<std::size_t> g_client_size{0};
std::atomic<bool> g_hitmarker_enabled{true};
std::mutex g_hs_cfg_mutex;
config::hitsound_cfg g_hs_cfg{};

struct observed_event {
    bool weapon_fire = false;
    int damage = -1;
    int remaining_health = -1;
    int hitgroup = 0;
};

observed_event read_event(void* event) noexcept {
    observed_event result{};
    const auto base = g_client_base.load(std::memory_order_acquire);
    const auto size = g_client_size.load(std::memory_order_acquire);
    const auto local = g_local_controller.load(std::memory_order_acquire);
    if (event == nullptr || base == 0 || size == 0 || local == 0) return result;

    using name_fn = const char* (*)(void*);
    using controller_fn = std::uintptr_t (*)(void*, const event_key&);
    using int_fn = int (*)(void*, const event_key&, int);
    __try {
        const auto table = *reinterpret_cast<const std::uintptr_t* const*>(event);
        if (!ptr_in_client(reinterpret_cast<std::uintptr_t>(table), base, size) ||
            !ptr_in_client(table[1], base, size) || !ptr_in_client(table[7], base, size) ||
            !ptr_in_client(table[16], base, size)) {
            return {};
        }
        const char* name = reinterpret_cast<name_fn>(table[1])(event);
        if (name == nullptr) return {};
        const auto controller = reinterpret_cast<controller_fn>(table[16]);
        if (std::strcmp(name, "weapon_fire") == 0) {
            result.weapon_fire = controller(event, k_actor) == local;
        } else if (std::strcmp(name, "player_hurt") == 0 &&
                   controller(event, k_attacker) == local && controller(event, k_actor) != local) {
            const auto get_int = reinterpret_cast<int_fn>(table[7]);
            result.damage = get_int(event, k_damage, -1);
            result.remaining_health = get_int(event, k_health, -1);
            result.hitgroup = get_int(event, k_hitgroup, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return {};
    }
    return result;
}

class shot_listener {
public:
    virtual ~shot_listener() = default;
    virtual void dispatch(void* event) {
        const auto observed = read_event(event);
        if (observed.weapon_fire) {
            log::info("shot event: local weapon_fire");
        } else if (observed.damage > 0 && observed.remaining_health >= 0) {
            config::hitsound_cfg hs{};
            {
                std::lock_guard lock(g_hs_cfg_mutex);
                hs = g_hs_cfg;
            }
            features::audio::record_hit_feedback(g_hitmarker_enabled.load(std::memory_order_relaxed),
                                                 observed.hitgroup, observed.remaining_health, hs);
            log::info("shot event: local attacker player_hurt damage={} health={} hitgroup={}",
                      observed.damage, observed.remaining_health, observed.hitgroup);
        }
    }
    virtual int debug_id() { return id_; }

private:
    int id_ = 42;
};

shot_listener g_listener;

void* read_manager(std::uintptr_t cell, std::uintptr_t base, std::size_t size) noexcept {
    __try {
        const auto manager = *reinterpret_cast<void* const*>(cell);
        if (!manager) return nullptr;
        const auto table = *reinterpret_cast<const std::uintptr_t* const*>(manager);
        return ptr_in_client(reinterpret_cast<std::uintptr_t>(table), base, size) &&
                       ptr_in_client(table[3], base, size)
                   ? manager
                   : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool subscribe(void* manager, const char* event) noexcept {
    using subscribe_fn = bool (*)(void*, shot_listener*, const char*, bool);
    __try {
        const auto table = *reinterpret_cast<const std::uintptr_t* const*>(manager);
        return reinterpret_cast<subscribe_fn>(table[3])(manager, &g_listener, event, false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void update_shot_events(std::uintptr_t manager_cell, std::uintptr_t client_base,
                        std::size_t client_size, std::uintptr_t local_controller,
                        bool hitmarker_enabled,
                        const config::hitsound_cfg* hitsound_cfg) noexcept {
    g_local_controller.store(local_controller, std::memory_order_release);
    g_hitmarker_enabled.store(hitmarker_enabled, std::memory_order_relaxed);
    if (hitsound_cfg != nullptr) {
        std::lock_guard lock(g_hs_cfg_mutex);
        g_hs_cfg = *hitsound_cfg;
    }
    if (manager_cell == 0 || client_base == 0 || client_size == 0 || local_controller == 0) return;

    static ULONGLONG next_check = 0;
    static void* manager = nullptr;
    static bool firing = false;
    static bool damage = false;
    const auto now = GetTickCount64();
    if (now < next_check) return;
    next_check = now + 2000;
    const auto* previous = manager;
    manager = read_manager(manager_cell, client_base, client_size);
    if (manager != previous) {
        firing = false;
        damage = false;
    }
    if (manager == nullptr) {
        static bool reported_unknown = false;
        if (!reported_unknown) {
            reported_unknown = true;
            log::warn("shot events: manager unavailable or vtable mismatch");
        }
        return;
    }
    if (firing && damage) return;

    g_client_base.store(client_base, std::memory_order_release);
    g_client_size.store(client_size, std::memory_order_release);
    if (!firing) firing = subscribe(manager, "weapon_fire");
    if (!damage) damage = subscribe(manager, "player_hurt");
    log::info("shot events: weapon_fire={} player_hurt={}", firing, damage);
}

}  // namespace shogod::sdk
