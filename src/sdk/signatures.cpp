#include "sdk/signatures.hpp"

#include "engine/log.hpp"
#include "engine/module.hpp"
#include "engine/pattern.hpp"

#include <array>
#include <cstring>
#include <format>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::sdk {
namespace {

// Recorded from docs/RE/findings.md for client.dll sha1
// 4E16EA32EA9485A25A67933ACD5A8B4870D855D1. RVAs are validation targets only;
// the address used at runtime always comes from the pattern.
constexpr std::uint32_t k_entity_system_vtable_rva = 0x1c29090;
constexpr std::uint32_t k_input_vtable_rva = 0x1c97330;
constexpr std::uint32_t k_create_move_rva = 0xb781e0;

anchors g_anchors;

std::uintptr_t module_addr(const module::info& m, std::uint32_t rva) {
    return m.base + rva;
}

std::uintptr_t find_once(const module::info& m, const char* text, const char* name) {
    const pattern::compiled compiled = pattern::compile(text);
    const std::uintptr_t hit = pattern::find(m.base, m.size, compiled);
    if (hit == 0) {
        log::error("anchor {}: pattern FAIL ({})", name, text);
    }
    return hit;
}

bool read_ptr(std::uintptr_t address, std::uintptr_t& out) {
    if (address == 0) return false;
    __try {
        out = *reinterpret_cast<std::uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void resolve_entities(const module::info& client) {
    const std::uintptr_t match = find_once(
        client,
        "0F B6 44 24 28 88 44 24 28 48 89 0D ?? ?? ?? ?? E9",
        "g_pGameEntitySystem");
    if (match == 0) {
        g_anchors.summary += "entity_system: pattern missing; ";
        return;
    }

    const std::uintptr_t global = pattern::resolve_rel32(match + 9, 3);
    std::uintptr_t object = 0;
    if (!read_ptr(global, object) || object == 0) {
        log::error("anchor entity_system: global at {:#x} is null", global);
        g_anchors.summary += "entity_system: null; ";
        return;
    }

    std::uintptr_t vtable = 0;
    if (!read_ptr(object, vtable) ||
        vtable != module_addr(client, k_entity_system_vtable_rva)) {
        log::error("anchor entity_system: vtable {:#x} != expected {:#x}", vtable,
                   module_addr(client, k_entity_system_vtable_rva));
        g_anchors.summary += "entity_system: vtable mismatch; ";
        return;
    }

    g_anchors.entity_system = object;
    g_anchors.entities_ok = true;
    log::info("anchor entity_system: OK {:#x}", object);
}

void resolve_input(const module::info& client) {
    const std::uintptr_t match =
        find_once(client, "48 8B 0D ?? ?? ?? ?? 4C 8D 47 14 8B 10", "g_pInput");
    if (match == 0) {
        g_anchors.summary += "input: pattern missing; ";
        return;
    }

    const std::uintptr_t global = pattern::resolve_rel32(match, 3);
    std::uintptr_t input = 0;
    if (!read_ptr(global, input) || input == 0) {
        log::error("anchor input: global {:#x} is null", global);
        g_anchors.summary += "input: null; ";
        return;
    }

    std::uintptr_t vtable = 0;
    if (!read_ptr(input, vtable) || vtable != module_addr(client, k_input_vtable_rva)) {
        log::error("anchor input: vtable {:#x} != expected {:#x}", vtable,
                   module_addr(client, k_input_vtable_rva));
        g_anchors.summary += "input: vtable mismatch; ";
        return;
    }

    // The vtable slot proves both the object and the CreateMove candidate.
    std::uintptr_t create_move = 0;
    if (!read_ptr(vtable + 7 * sizeof(void*), create_move) ||
        create_move != module_addr(client, k_create_move_rva)) {
        log::error("anchor CreateMove: slot 7 {:#x} != expected {:#x}", create_move,
                   module_addr(client, k_create_move_rva));
        g_anchors.summary += "create_move: slot mismatch; ";
        return;
    }

    g_anchors.input = input;
    g_anchors.input_ok = true;
    log::info("anchor input: OK {:#x}; CreateMove slot 7 -> {:#x}", input, create_move);
}

void resolve_schema() {
    const HMODULE schemasystem = GetModuleHandleA("schemasystem.dll");
    if (schemasystem == nullptr) {
        log::error("anchor schema: schemasystem.dll not loaded");
        g_anchors.summary += "schema: module missing; ";
        return;
    }

    using create_interface_fn = void* (*)(const char*, int*);
    const auto create_interface = reinterpret_cast<create_interface_fn>(
        GetProcAddress(schemasystem, "CreateInterface"));
    if (create_interface == nullptr) {
        log::error("anchor schema: CreateInterface export missing");
        g_anchors.summary += "schema: export missing; ";
        return;
    }

    void* iface = create_interface("SchemaSystem_001", nullptr);
    if (iface == nullptr) {
        log::error("anchor schema: CreateInterface returned null");
        g_anchors.summary += "schema: null interface; ";
        return;
    }

    std::uintptr_t vtable = 0;
    if (!read_ptr(reinterpret_cast<std::uintptr_t>(iface), vtable)) {
        g_anchors.summary += "schema: unreadable vtable; ";
        return;
    }

    const module::info schema_module = module::find("schemasystem.dll");
    if (!schema_module.valid() || vtable < schema_module.base || vtable >= schema_module.end()) {
        log::error("anchor schema: vtable {:#x} outside schemasystem.dll", vtable);
        g_anchors.summary += "schema: vtable outside module; ";
        return;
    }

    g_anchors.schema = reinterpret_cast<std::uintptr_t>(iface);
    g_anchors.schema_ok = true;
    log::info("anchor schema: OK {:#x} (vtable {:#x})", g_anchors.schema, vtable);
}

}  // namespace

const anchors& resolve() {
    g_anchors = {};
    g_anchors.summary.clear();

    const module::info client = module::find("client.dll");
    if (!client.valid()) {
        log::error("anchors: client.dll not loaded");
        g_anchors.summary = "client.dll not loaded";
        return g_anchors;
    }
    log::info("anchors: client.dll base {:#x} size {:#x}", client.base, client.size);

    resolve_entities(client);
    resolve_input(client);
    resolve_schema();

    log::info("anchors: {}/{} OK — {}", g_anchors.ok_count(), anchors::total_count,
              g_anchors.summary.empty() ? "all resolved" : g_anchors.summary);
    return g_anchors;
}

}  // namespace shogod::sdk
