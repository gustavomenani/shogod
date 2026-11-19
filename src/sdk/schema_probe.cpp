// Runtime schema probe — temporary diagnostic used to derive the TypeScope
// method that returns a class binding, and the field-entry layout, from the
// live interface. Every call is SEH-guarded; results go to the log only.

#include "sdk/schema_probe.hpp"

#include "engine/log.hpp"
#include "engine/module.hpp"
#include "sdk/signatures.hpp"

#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::sdk {
namespace {

std::uintptr_t find_string(const module::info& m, const char* text) {
    const std::size_t len = std::strlen(text) + 1;
    if (!m.valid() || len == 0 || m.size < len) return 0;

    const auto* begin = reinterpret_cast<const std::uint8_t*>(m.base);
    for (std::size_t i = 0; i + len <= m.size; ++i) {
        if (begin[i] != static_cast<std::uint8_t>(text[0])) continue;
        if (std::memcmp(begin + i, text, len) == 0) return m.base + i;
    }
    return 0;
}

bool readable(const void* address, std::size_t bytes) {
    __try {
        volatile std::uint8_t sink = 0;
        const auto* p = static_cast<const std::uint8_t*>(address);
        for (std::size_t i = 0; i < bytes; i += 0x1000) sink ^= p[i];
        (void)sink;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void scan_for_field_pointer(std::uintptr_t base, std::size_t span, std::uintptr_t string_va,
                            const char* field_name) {
    __try {
        const auto* words = reinterpret_cast<const std::uintptr_t*>(base);
        const std::size_t count = span / sizeof(std::uintptr_t);
        int hits = 0;
        for (std::size_t i = 0; i < count && hits < 4; ++i) {
            if (words[i] != string_va) continue;
            ++hits;
            // Field entries in the schema are {name*, ...}: log the surrounding
            // dwords so the offset field can be identified from data.
            const std::uintptr_t at = base + i * 8;
            const auto* dwords = reinterpret_cast<const std::uint32_t*>(at);
            log::warn("schema: {} reference at {:#x}: {:08x} {:08x} {:08x} {:08x} {:08x} {:08x}",
                      field_name, at, dwords[0], dwords[1], dwords[2], dwords[3], dwords[4],
                      dwords[5]);
        }
        if (hits == 0) log::info("schema: {} reference not found in span", field_name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log::warn("schema: scan for {} faulted", field_name);
    }
}

void scan_memory_for_pointer(std::uintptr_t target, const char* tag) {
    if (target == 0) return;

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto* address = static_cast<std::uint8_t*>(info.lpMinimumApplicationAddress);
    auto* maximum = static_cast<std::uint8_t*>(info.lpMaximumApplicationAddress);

    int found = 0;
    while (address < maximum && found < 6) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(address, &mbi, sizeof mbi) != sizeof mbi) break;
        auto* next = static_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;

        const bool usable = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                            mbi.RegionSize <= 0x4000000 && (mbi.Protect & PAGE_GUARD) == 0 &&
                            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY)) != 0;
        if (usable) {
            __try {
                const auto* words = static_cast<const std::uintptr_t*>(mbi.BaseAddress);
                const std::size_t count = mbi.RegionSize / sizeof(std::uintptr_t);
                for (std::size_t i = 0; i < count && found < 6; ++i) {
                    if (words[i] != target) continue;
                    ++found;
                    const auto* dwords = reinterpret_cast<const std::uint32_t*>(words + i);
                    log::warn("schema: {} pointer at {:p}: {:08x} {:08x} {:08x} {:08x}", tag,
                              static_cast<const void*>(words + i), dwords[0], dwords[1],
                              dwords[2], dwords[3]);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        address = next;
    }
    log::info("schema: {} pointer occurrences logged: {}", tag, found);
}

}  // namespace

void probe_schema_once() {
    static bool done = false;
    if (done) return;
    done = true;

    const module::info client = module::find("client.dll");
    const module::info schema_module = module::find("schemasystem.dll");
    if (!client.valid() || !schema_module.valid()) return;

    const std::uintptr_t class_name = find_string(client, "C_CSPlayerPawn");
    const std::uintptr_t health_name = find_string(client, "m_iHealth");
    log::info("schema: strings C_CSPlayerPawn {:#x} m_iHealth {:#x}", class_name, health_name);

    const std::uintptr_t iface = resolve().schema;
    if (iface == 0) {
        log::warn("schema: no interface");
        return;
    }

    __try {
        auto** vtable = *reinterpret_cast<void***>(iface);
        const std::uintptr_t slot12 = reinterpret_cast<std::uintptr_t>(vtable[12]);
        const std::uintptr_t vtable_bits = reinterpret_cast<std::uintptr_t>(vtable);
        log::info("schema: iface {:#x} vtable {:#x} slot12 {:#x}", iface, vtable_bits, slot12);

        using find_scope_fn = void* (*)(void*, const char*, const char*);
        auto find_scope = reinterpret_cast<find_scope_fn>(vtable[12]);

        void* scope = nullptr;
        __try {
            scope = find_scope(reinterpret_cast<void*>(iface), "client.dll", nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log::warn("schema: FindTypeScopeForModule faulted");
        }
        log::info("schema: client.dll scope {:p}", scope);
        if (scope == nullptr) return;

        // Do not call unknown vtable slots: a wrong guess can run a
        // destructor. Instead locate the binding objects by their string
        // pointers and read the surrounding data.
        scan_memory_for_pointer(class_name, "C_CSPlayerPawn");
        scan_memory_for_pointer(health_name, "m_iHealth");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log::error("schema: probe faulted");
    }
}

}  // namespace shogod::sdk
