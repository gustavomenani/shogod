#pragma once

// Dynamic signature & interface table resolved from loaded game modules via
// pattern scanning and CreateInterface. Failed optional entries disable only
// their own feature without aborting init.

#include <cstdint>
#include <string>

namespace shogod::sdk {

struct anchors {
    // Core client anchors
    std::uintptr_t entity_system = 0;   // CGameEntitySystem*
    std::uintptr_t input = 0;           // CCSGOInput*
    std::uintptr_t input_update = 0;    // CCSGOInput::vtable[4]
    std::uintptr_t create_move = 0;     // CCSGOInput::vtable[5]
    std::uintptr_t send_move = 0;       // CCSGOInput::vtable[8]
    std::uintptr_t schema = 0;          // ISchemaSystem*
    std::uintptr_t view_matrix = 0;     // float[16] row-major worldToProjection
    std::uintptr_t view_render = 0;     // CViewRender*

    // Global pointers & interfaces
    std::uintptr_t global_vars_cell = 0;    // CGlobalVariables**
    std::uintptr_t physics_cell = 0;        // CPhysics**
    std::uintptr_t source2_client = 0;      // CSource2Client*
    std::uintptr_t engine_client = 0;       // CEngineToClient*
    std::uintptr_t cvar = 0;                // CCvar*
    std::uintptr_t material_system = 0;     // CMaterialSystem*
    std::uintptr_t scene_system = 0;        // CSceneSystem*
    std::uintptr_t client_mode = 0;         // ClientMode*

    // Weapon / accuracy / command / hitbox / recoil helpers (client.dll)
    std::uintptr_t weapon_spread = 0;
    std::uintptr_t weapon_inaccuracy = 0;
    std::uintptr_t update_accuracy_penalty = 0;
    std::uintptr_t get_command_manager = 0;
    std::uintptr_t create_subtick_move = 0;
    std::uintptr_t add_element_to_rep = 0;
    std::uintptr_t compute_aim_punch = 0;
    std::uintptr_t get_hitbox_set_name = 0;
    std::uintptr_t hash_hitbox_name = 0;
    std::uintptr_t get_hitbox_list = 0;
    std::uintptr_t update_hitbox_history = 0;
    std::uintptr_t get_eye_time = 0;
    std::uintptr_t from_global_tick = 0;
    std::uintptr_t to_global_tick = 0;
    std::uintptr_t movement_player_cell = 0;

    // Visual & world callbacks
    void** setup_view_shared_slot = nullptr;
    void** setup_view_normal_slot = nullptr;
    void* setup_view_fn = nullptr;
    void** draw_smoke_slot = nullptr;
    void* draw_smoke_fn = nullptr;
    void* draw_scope = nullptr;
    void* view_punch_fn = nullptr;

    // Chams (SceneSystem + RenderSystemDX11 + MaterialSystem2 + Tier0)
    void* kv3_load = nullptr;
    std::uintptr_t mem_alloc_cell = 0;
    void* animatable_desc = nullptr;
    void* player_pvs_callsite = nullptr;
    void* sort_primitives = nullptr;
    void* render_layer_draw_list = nullptr;
    void* draw_scene_primitive = nullptr;
    void* cull_scene_objects = nullptr;
    void* test_scene_pvs = nullptr;
    void* depth_factory = nullptr;
    void* create_depth_state = nullptr;
    void* set_depth_state = nullptr;
    void* set_depth_override = nullptr;
    void* draw_occluded_dx11 = nullptr;
    void* draw_occluded_software = nullptr;
    void* find_material_param = nullptr;

    // World modulation hooks
    void* base_draw_array = nullptr;
    void* setup_scene_view = nullptr;
    void* setup_light = nullptr;
    void* setup_tonemap = nullptr;
    void* setup_sky_params = nullptr;
    void* play_vsnd = nullptr;  // soundsystem.dll PlayVsnd(const char*)

    std::uintptr_t client_base = 0;
    std::size_t client_size = 0;
    std::uintptr_t game_event_manager_cell = 0;

    bool entities_ok = false;
    bool input_ok = false;
    bool schema_ok = false;
    bool view_matrix_ok = false;

    int ok_count() const noexcept {
        return (entities_ok ? 1 : 0) + (input_ok ? 1 : 0) + (schema_ok ? 1 : 0) +
               (view_matrix_ok ? 1 : 0);
    }
    static constexpr int total_count = 4;

    std::string summary;
};

const anchors& resolve();

}  // namespace shogod::sdk
