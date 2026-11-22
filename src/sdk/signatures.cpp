#include "sdk/signatures.hpp"

#include "engine/log.hpp"
#include "engine/module.hpp"
#include "engine/pattern.hpp"

#include <windows.h>

namespace shogod::sdk {
namespace {

anchors g_anchors;

bool read_ptr(std::uintptr_t address, std::uintptr_t& out) {
    if (address == 0) return false;
    __try {
        out = *reinterpret_cast<std::uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uintptr_t find_once(const module::info& m, const char* text, const char* name,
                         bool log_fail = true) {
    if (!m.valid()) return 0;
    const pattern::compiled compiled = pattern::compile(text);
    const std::uintptr_t hit = pattern::find(m.base, m.size, compiled);
    if (hit == 0 && log_fail) {
        log::warn("anchor {}: pattern miss", name);
    }
    return hit;
}

void* find_interface(const char* mod_name, const char* iface_name) {
    const HMODULE mod = GetModuleHandleA(mod_name);
    if (!mod) return nullptr;
    using create_fn = void* (*)(const char*, int*);
    const auto fn = reinterpret_cast<create_fn>(GetProcAddress(mod, "CreateInterface"));
    if (!fn) return nullptr;
    __try {
        return fn(iface_name, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ptr_in_module(std::uintptr_t p, const module::info& m) {
    return m.valid() && p >= m.base && p < m.end();
}

void resolve_entities(const module::info& client) {
    std::uintptr_t match = find_once(
        client, "48 8B 0D ? ? ? ? 4C 8D 05 ? ? ? ? 48 8D 54 24 ? E8",
        "g_pGameEntitySystem", false);
    std::uintptr_t global = 0;
    if (match != 0) {
        global = pattern::resolve_rel32(match, 3);
    } else {
        match = find_once(client, "0F B6 44 24 28 88 44 24 28 48 89 0D ?? ?? ?? ?? E9",
                          "g_pGameEntitySystem");
        if (match != 0) global = pattern::resolve_rel32(match + 9, 3);
    }

    if (global == 0) {
        g_anchors.summary += "entity_system: pattern missing; ";
        return;
    }

    std::uintptr_t object = 0;
    if (!read_ptr(global, object) || object == 0) {
        log::error("anchor entity_system: global at {:#x} is null", global);
        g_anchors.summary += "entity_system: null; ";
        return;
    }

    std::uintptr_t vtable = 0;
    if (!read_ptr(object, vtable) || !ptr_in_module(vtable, client)) {
        log::error("anchor entity_system: invalid vtable {:#x}", vtable);
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
    if (!read_ptr(input, vtable) || !ptr_in_module(vtable, client)) {
        log::error("anchor input: invalid vtable {:#x}", vtable);
        g_anchors.summary += "input: vtable mismatch; ";
        return;
    }

    std::uintptr_t slot4 = 0, slot5 = 0, slot8 = 0;
    read_ptr(vtable + 4 * sizeof(void*), slot4);
    read_ptr(vtable + 5 * sizeof(void*), slot5);
    read_ptr(vtable + 8 * sizeof(void*), slot8);

    if (!ptr_in_module(slot5, client) && !ptr_in_module(slot4, client)) {
        log::error("anchor input: vtable slots invalid");
        g_anchors.summary += "input: slot mismatch; ";
        return;
    }

    g_anchors.input = input;
    g_anchors.input_update = ptr_in_module(slot4, client) ? slot4 : 0;
    g_anchors.create_move = ptr_in_module(slot5, client) ? slot5 : 0;
    g_anchors.send_move = ptr_in_module(slot8, client) ? slot8 : 0;
    g_anchors.input_ok = true;
    log::info("anchor input: OK {:#x} (v4={:#x} v5={:#x} v8={:#x})", input,
              g_anchors.input_update, g_anchors.create_move, g_anchors.send_move);
}

void resolve_schema() {
    void* iface = find_interface("schemasystem.dll", "SchemaSystem_001");
    if (!iface) {
        log::error("anchor schema: SchemaSystem_001 missing");
        g_anchors.summary += "schema: missing; ";
        return;
    }

    std::uintptr_t vtable = 0;
    const module::info schema_module = module::find("schemasystem.dll");
    if (!read_ptr(reinterpret_cast<std::uintptr_t>(iface), vtable) ||
        !ptr_in_module(vtable, schema_module)) {
        log::error("anchor schema: invalid vtable {:#x}", vtable);
        g_anchors.summary += "schema: vtable outside module; ";
        return;
    }

    g_anchors.schema = reinterpret_cast<std::uintptr_t>(iface);
    g_anchors.schema_ok = true;
    log::info("anchor schema: OK {:#x}", g_anchors.schema);
}

void resolve_view_matrix(const module::info& client) {
    std::uintptr_t match = find_once(
        client, "48 63 08 48 8D 05 ?? ?? ?? ?? 48 C1 E1 06 48 03 C8", "dwViewMatrix", false);
    std::uintptr_t matrix = 0;
    if (match != 0) {
        matrix = pattern::resolve_rel32(match + 3, 3);
    } else {
        match = find_once(client, "48 8D 0D ? ? ? ? 48 C1 E0 06", "dwViewMatrix_alt");
        if (match != 0) matrix = pattern::resolve_rel32(match, 3);
    }

    if (!ptr_in_module(matrix, client)) {
        g_anchors.summary += "view_matrix: missing; ";
        return;
    }

    g_anchors.view_matrix = matrix;
    g_anchors.view_matrix_ok = true;
    log::info("anchor view_matrix: OK {:#x}", matrix);
}

void resolve_globals_and_interfaces(const module::info& client) {
    if (const auto gv = find_once(
            client, "48 8B 1D ? ? ? ? FF 15 ? ? ? ? F3 44 0F 10 43 ? 8B C8 8B 43", "GlobalVars")) {
        g_anchors.global_vars_cell = pattern::resolve_rel32(gv, 3);
    }
    if (const auto ph = find_once(
            client, "48 8B 0D ? ? ? ? 48 8D 45 ? C6 44 24 ? ? 4C 8D 4C 24", "Physics")) {
        g_anchors.physics_cell = pattern::resolve_rel32(ph, 3);
    }
    if (const auto cm = find_once(
            client, "48 8D 0D ? ? ? ? 48 69 C0 ? ? ? ? 48 03 C1 C3 CC CC", "ClientMode", false)) {
        g_anchors.client_mode = pattern::resolve_rel32(cm, 3);
    }
    if (const auto mp = find_once(
            client, "48 89 05 ? ? ? ? 48 8D 15 ? ? ? ? 83 FD 03 0F 84 ? ? ? ? 48 8B 05",
            "MovementPlayer", false)) {
        g_anchors.movement_player_cell = pattern::resolve_rel32(mp, 3);
    }

    g_anchors.source2_client = reinterpret_cast<std::uintptr_t>(find_interface("client.dll", "Source2Client002"));
    g_anchors.engine_client = reinterpret_cast<std::uintptr_t>(find_interface("engine2.dll", "Source2EngineToClient001"));
    g_anchors.cvar = reinterpret_cast<std::uintptr_t>(find_interface("tier0.dll", "VEngineCvar007"));
    g_anchors.material_system = reinterpret_cast<std::uintptr_t>(find_interface("materialsystem2.dll", "VMaterialSystem2_001"));
    g_anchors.scene_system = reinterpret_cast<std::uintptr_t>(find_interface("scenesystem.dll", "SceneSystem_002"));

    if (const HMODULE tier0 = GetModuleHandleA("tier0.dll")) {
        g_anchors.kv3_load = reinterpret_cast<void*>(GetProcAddress(
            tier0, "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z"));
        g_anchors.mem_alloc_cell = reinterpret_cast<std::uintptr_t>(GetProcAddress(tier0, "g_pMemAlloc"));
    }
}

void resolve_rage_helpers(const module::info& client) {
    g_anchors.weapon_spread = find_once(
        client,
        "48 63 91 ? ? ? ? 48 8B 81 ? ? ? ? 85 D2 78 ? 48 83 FA ? 73 ? F3 0F 10 84 90 ? ? ? ? C3 F3 0F 10 80 ? ? ? ? C3 CC CC CC CC CC 40 53",
        "WeaponSpread");
    g_anchors.weapon_inaccuracy = find_once(
        client,
        "48 89 5C 24 10 55 56 57 48 81 EC B0 00 00 00 44 0F 29 84 24 80 00 00 00 49 8B F0 48 8B EA 48 8B F9",
        "WeaponInaccuracy");
    g_anchors.update_accuracy_penalty = find_once(
        client, "40 57 41 56 48 83 EC ? 48 8B F9 E8", "UpdateAccuracyPenalty");

    g_anchors.get_command_manager = find_once(
        client, "41 56 41 57 48 83 EC ? 48 8D 54 24", "GetCommandManager");

    if (const auto hit = find_once(
            client, "E8 ? ? ? ? 48 8B D0 48 8B CE E8 ? ? ? ? 48 8B C8", "CreateSubtickMove")) {
        g_anchors.create_subtick_move = pattern::resolve_rel32(hit, 1);
    }
    if (const auto hit = find_once(
            client, "E8 ? ? ? ? 48 8B C8 8B 51", "AddElementToRep")) {
        g_anchors.add_element_to_rep = pattern::resolve_rel32(hit, 1);
    }

    g_anchors.compute_aim_punch = find_once(
        client,
        "4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 57 48 83 EC 70 48 8B D9 44 88 4C 24 38 4D 89 43 B8",
        "ComputeAimPunch");

    g_anchors.get_hitbox_set_name = find_once(
        client, "48 8B 81 E0 01 00 00 0F B6 91 FC 03 00 00 48 85 C0 74 08", "GetHitboxSetName");
    g_anchors.hash_hitbox_name = find_once(
        client, "48 89 5C 24 08 57 48 81 EC C0 00 00 00 8B FA 48 C7 C3 FF FF FF FF", "HashHitboxName");
    g_anchors.get_hitbox_list = find_once(
        client, "4C 8B DC 45 89 4B 20 4D 89 43 18 49 89 53 10 49 89 4B 08 55 49 8D 6B A1",
        "GetHitboxList");
    g_anchors.update_hitbox_history = find_once(
        client,
        "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 56 41 57 48 8D AC 24 90 FA FF FF 48 81 EC 70 06",
        "UpdateHitboxHistory");

    g_anchors.get_eye_time = find_once(
        client,
        "40 53 48 83 EC 30 48 8B 05 ? ? ? ? 48 8B D9 0F 29 74 24 20 48 8D 4C 24 40 F3 0F 10 70 50",
        "GetEyeTime", false);
    g_anchors.from_global_tick = find_once(
        client,
        "40 53 48 83 EC 20 48 8B D9 45 85 C0 74 0B 89 11 48 8B C1 48 83 C4 20 5B C3",
        "FromGlobalTick", false);
    g_anchors.to_global_tick = find_once(
        client,
        "48 83 EC 28 85 D2 75 1B 48 8B 05 ? ? ? ? 48 85 C0 74 0F 44 8B C2 8B D1 48 8B C8",
        "ToGlobalTick", false);
}

void resolve_visual_and_chams(const module::info& client) {
    g_anchors.setup_view_fn = reinterpret_cast<void*>(
        find_once(client, "4C 8B DC 49 89 4B ? 53 55", "SetupView"));
    g_anchors.draw_smoke_fn = reinterpret_cast<void*>(
        find_once(client,
                  "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 48 8B 9C 24 ? ? ? ? 4D 8B F8",
                  "DrawSmoke"));
    g_anchors.draw_scope = reinterpret_cast<void*>(
        find_once(client, "48 8B C4 53 57 48 83 EC ? 48 8B FA", "DrawScopeOverlay"));
    g_anchors.view_punch_fn = reinterpret_cast<void*>(
        find_once(client, "40 55 56 57 41 56 41 57 48 8D 6C 24", "ViewPunch", false));

    if (const auto gem = find_once(
            client,
            "48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 50 ? 4C 8B 05",
            "GameEventManager", false)) {
        g_anchors.game_event_manager_cell = pattern::resolve_rel32(gem, 3);
    }

    const module::info scene = module::find("scenesystem.dll");
    const module::info render = module::find("rendersystemdx11.dll");
    const module::info mat = module::find("materialsystem2.dll");

    if (g_anchors.scene_system != 0) {
        __try {
            auto* obj = reinterpret_cast<void*>(g_anchors.scene_system);
            auto** vt = *reinterpret_cast<void***>(obj);
            using get_obj_fn = void* (*)(void*, const char*);
            auto get_obj = reinterpret_cast<get_obj_fn>(vt[17]);
            if (get_obj) {
                g_anchors.animatable_desc = get_obj(obj, "AnimatableSceneObjectDesc");
                if (void* base_desc = get_obj(obj, "BaseSceneObjectDesc")) {
                    g_anchors.base_draw_array = (*reinterpret_cast<void***>(base_desc))[1];
                }
                if (void* agg_desc = get_obj(obj, "AggregateSceneObjectDesc")) {
                    g_anchors.setup_scene_view = (*reinterpret_cast<void***>(agg_desc))[7];
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log::warn("anchor scene_system: descriptor lookup faulted");
        }
    }

    if (scene.valid()) {
        if (const auto pvs = find_once(
                scene,
                "48 8B 8E A0 00 00 00 48 85 C9 74 ? 0F B6 46 78 C0 E8 03 A8 01 74 ? 49 8B D0 E8 ? ? ? ? 4C 8B 4C 24 ? 84 C0",
                "PlayerPvsCallsite")) {
            g_anchors.player_pvs_callsite = reinterpret_cast<void*>(pvs + 31);
        }
        g_anchors.sort_primitives = reinterpret_cast<void*>(
            find_once(scene, "45 85 C9 0F 84 ? ? ? ? 55", "SortPrimitives"));
        g_anchors.render_layer_draw_list = reinterpret_cast<void*>(
            find_once(scene,
                      "4C 89 4C 24 ? 4C 89 44 24 ? 48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 41 F6 40 48 01",
                      "RenderLayerDrawList"));
        g_anchors.draw_scene_primitive = reinterpret_cast<void*>(
            find_once(scene,
                      "4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 55 41 56",
                      "DrawScenePrimitive"));
        g_anchors.cull_scene_objects = reinterpret_cast<void*>(
            find_once(scene,
                      "48 8B C4 4C 89 40 18 55 53 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 80 B9 ? ? ? ? 00",
                      "CullSceneObjects"));
        g_anchors.test_scene_pvs = reinterpret_cast<void*>(
            find_once(scene,
                      "4C 8B DA 4C 8B C9 48 85 C9 74 ? 0F 1F 44 00 00 45 0F B7 51 08 45 33 C0",
                      "TestScenePvs"));
        g_anchors.setup_light = reinterpret_cast<void*>(
            find_once(scene, "48 89 54 24 ? 55 57 41 56 48 83 EC", "SetupLight"));
        g_anchors.setup_tonemap = reinterpret_cast<void*>(
            find_once(scene, "40 53 48 81 EC ? ? ? ? F2 0F 10 51", "SetupTonemap"));
    }

    if (render.valid()) {
        g_anchors.depth_factory = reinterpret_cast<void*>(find_once(
            render,
            "48 89 5C 24 08 57 48 83 EC 20 48 8B DA 48 8B F9 48 85 D2 74 06 C7 02 00 00 00 00 48 8D 15 ? ? ? ? FF 15 ? ? ? ? 85 C0 75 12 48 8B 05 ? ? ? ?",
            "DepthFactory"));
        g_anchors.create_depth_state = reinterpret_cast<void*>(find_once(
            render,
            "4C 8B DC 48 83 EC 48 49 8D 43 08 49 89 53 10 49 89 43 E0 4D 8D 4B D8 49 8D 43 10 49 C7 43 08 00 00 00 00 49 89 43 E8 4D 8D 43 18 49 8D 43 08 49 89 53 D8 48 81 C1 60 01 00 00 49 89 43 18 E8",
            "CreateDepthState"));
        g_anchors.set_depth_state = reinterpret_cast<void*>(find_once(
            render, "48 8B 81 58 0A 00 00 48 85 C0 74 0A 44 8B 81 60", "SetDepthState"));
        g_anchors.set_depth_override = reinterpret_cast<void*>(find_once(
            render, "48 89 91 58 0A 00 00 44 89 81 60 0A 00 00 C3 CC", "SetDepthOverride"));
        if (const auto dx = find_once(
                render,
                "8B 44 24 78 44 8B CD 48 8B 8B A0 0A 00 00 8B D7 89 44 24 28 8B 44 24 70 89 44 24 20 4C 8B 11 41 FF 92 A0 00 00 00",
                "DrawOccludedDx11")) {
            g_anchors.draw_occluded_dx11 = reinterpret_cast<void*>(dx - 0x109);
        }
        if (const auto sw = find_once(
                render,
                "C7 01 1F 80 18 00 89 71 04 89 69 08 44 89 71 0C 89 41 10 8B 44 24 78 89 41 14 48 83 83 20 04 00 00 18",
                "DrawOccludedSoftware")) {
            g_anchors.draw_occluded_software = reinterpret_cast<void*>(sw - 0x10A);
        }
    }

    if (mat.valid()) {
        g_anchors.find_material_param = reinterpret_cast<void*>(find_once(
            mat,
            "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 59 ? 48 8B F2 48 63 79 ? 48 C1 E7 ? 48 03 FB 48 3B DF",
            "FindMaterialParam"));
    }

    const module::info snd = module::find("soundsystem.dll");
    if (snd.valid()) {
        g_anchors.play_vsnd = reinterpret_cast<void*>(find_once(
            snd,
            "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 48 8D 6C 24 ? 48 81 EC ? ? ? ? 33 F6 C7 45 ? ? ? ? ? 4C 8B C1",
            "PlayVsnd", false));
    }
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
    g_anchors.client_base = client.base;
    g_anchors.client_size = client.size;
    log::info("anchors: client.dll base {:#x} size {:#x}", client.base, client.size);

    resolve_entities(client);
    resolve_input(client);
    resolve_schema();
    resolve_view_matrix(client);
    resolve_globals_and_interfaces(client);
    resolve_rage_helpers(client);
    resolve_visual_and_chams(client);

    log::info("anchors: {}/{} OK — {}", g_anchors.ok_count(), anchors::total_count,
              g_anchors.summary.empty() ? "all resolved" : g_anchors.summary);
    return g_anchors;
}

}  // namespace shogod::sdk
