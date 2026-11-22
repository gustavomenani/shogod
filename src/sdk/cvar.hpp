#pragma once

// Lightweight SEH-guarded CS2 ConVar lookup/read/write via VEngineCvar007.
// Verified CConVar / CCvar layout: CCvar::entries at +0x50, CConVar::value at +0x58.

#include <cstdint>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::sdk {

struct cs2_cvar_value {
    union {
        bool i1;
        int32_t i32;
        uint32_t u32;
        float fl;
        const char* sz;
    };
};

struct cs2_convar {
    const char* name;
    cs2_convar* next;
    std::uint8_t pad0[0x10];
    const char* description;
    std::uint32_t type;
    std::uint32_t registered;
    std::uint32_t flags;
    std::uint8_t pad1[0x24];
    cs2_cvar_value value;
};
static_assert(offsetof(cs2_convar, value) == 0x58);

struct cs2_convar_entry {
    cs2_convar* convar;
    std::uint16_t prev;
    std::uint16_t next;
    std::uint32_t pad;
};
static_assert(sizeof(cs2_convar_entry) == 0x10);

inline cs2_convar* find_cvar(std::uintptr_t cvar_iface, const char* name) noexcept {
    if (cvar_iface == 0 || name == nullptr || name[0] == '\0') return nullptr;
    __try {
        const auto* entries = *reinterpret_cast<const cs2_convar_entry* const*>(cvar_iface + 0x50);
        if (entries == nullptr) return nullptr;
        int guard = 0;
        for (std::uint16_t i = 0; i != 0xFFFFu && guard < 16384; i = entries[i].next, ++guard) {
            cs2_convar* cv = entries[i].convar;
            if (cv != nullptr && cv->name != nullptr && std::strcmp(cv->name, name) == 0) {
                return cv;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

inline bool read_cvar_float(std::uintptr_t cvar_iface, const char* name, float& out) noexcept {
    cs2_convar* cv = find_cvar(cvar_iface, name);
    if (!cv) return false;
    __try {
        out = cv->value.fl;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool read_cvar_bool(std::uintptr_t cvar_iface, const char* name, bool& out) noexcept {
    cs2_convar* cv = find_cvar(cvar_iface, name);
    if (!cv) return false;
    __try {
        out = cv->value.i1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool write_cvar_float(std::uintptr_t cvar_iface, const char* name, float value) noexcept {
    cs2_convar* cv = find_cvar(cvar_iface, name);
    if (!cv) return false;
    __try {
        cv->value.fl = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace shogod::sdk
