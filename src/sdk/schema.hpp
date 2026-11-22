#pragma once

// Runtime schema resolver backed by SchemaSystem_001. Every netvar offset is
// queried by class + field name once at init; zero offsets are hardcoded.

#include <cstdint>

namespace shogod::sdk {

struct netvars {
    bool valid = false;

    // C_BaseEntity
    std::int32_t ent_health = 0;
    std::int32_t ent_team = 0;
    std::int32_t ent_life_state = 0;
    std::int32_t ent_scene_node = 0;
    std::int32_t ent_collision = 0;
    std::int32_t ent_velocity = 0;
    std::int32_t ent_abs_velocity = 0;
    std::int32_t ent_flags = 0;
    std::int32_t ent_identity = 0;
    std::int32_t ent_owner = 0;
    std::int32_t ent_actual_move_type = 0;
    std::int32_t ent_sim_time = 0;
    std::int32_t ent_old_sim_time = 0;
    std::int32_t ent_sim_tick = 0;
    std::int32_t ent_ground_entity = 0;
    std::int32_t ent_subclass_id = 0;

    // CCollisionProperty
    std::int32_t collision_mins = 0;
    std::int32_t collision_maxs = 0;

    // CEntityIdentity
    std::int32_t id_designer_name = 0;

    // CGameSceneNode / CSkeletonInstance
    std::int32_t node_abs_origin = 0;
    std::int32_t node_to_world = 0;
    std::int32_t node_abs_rotation = 0;
    std::int32_t skel_model_state = 0;
    std::int32_t skel_hitbox_set = 0;

    // C_BasePlayerPawn
    std::int32_t pawn_old_origin = 0;
    std::int32_t pawn_view_offset = 0;
    std::int32_t pawn_weapon_services = 0;
    std::int32_t pawn_movement_services = 0;
    std::int32_t pawn_camera_services = 0;

    // C_CSPlayerPawn / C_CSPlayerPawnBase
    std::int32_t cs_pawn_armor = 0;
    std::int32_t cs_pawn_scoped = 0;
    std::int32_t cs_pawn_resume_zoom = 0;
    std::int32_t cs_pawn_defusing = 0;
    std::int32_t cs_pawn_flash_duration = 0;
    std::int32_t cs_pawn_flash_overlay_alpha = 0;
    std::int32_t cs_pawn_item_services = 0;
    std::int32_t cs_pawn_aim_punch_services = 0;
    std::int32_t cs_pawn_eye_angles = 0;
    std::int32_t cs_pawn_spotted_state = 0;
    std::int32_t cs_pawn_immunity = 0;
    std::int32_t cs_pawn_wait_for_no_attack = 0;
    std::int32_t cs_pawn_shots_fired = 0;
    std::int32_t cs_pawn_player_state = 0;
    std::int32_t cs_pawn_last_spawn_time = 0;

    // CEntitySpottedState_t
    std::int32_t spotted_by_mask = 0;

    // CCSPlayer_ItemServices
    std::int32_t item_has_defuser = 0;
    std::int32_t item_has_helmet = 0;

    // CCSPlayerController
    std::int32_t ctrl_is_local = 0;
    std::int32_t ctrl_player_pawn = 0;
    std::int32_t ctrl_player_name = 0;
    std::int32_t ctrl_tick_base = 0;

    // CPlayer_WeaponServices / CCSPlayer_WeaponServices
    std::int32_t wep_active = 0;
    std::int32_t wep_next_attack = 0;

    // CPlayer_CameraServices
    std::int32_t cam_view_punch = 0;
    std::int32_t cam_view_punch_tick = 0;
    std::int32_t cam_view_punch_ratio = 0;

    // C_BasePlayerWeapon / C_CSWeaponBase / C_EconEntity
    std::int32_t wep_clip1 = 0;
    std::int32_t wep_next_primary_tick = 0;
    std::int32_t wep_next_primary_ratio = 0;
    std::int32_t wep_next_secondary_tick = 0;
    std::int32_t wep_next_secondary_ratio = 0;
    std::int32_t wep_in_reload = 0;
    std::int32_t wep_burst_mode = 0;
    std::int32_t wep_postpone_ready_ticks = 0;
    std::int32_t wep_postpone_ready_frac = 0;
    std::int32_t wep_hauled_back = 0;
    std::int32_t wep_last_shot_time = 0;
    std::int32_t wep_wat_tick_offset = 0;
    std::int32_t wep_zoom_level = 0;
    std::int32_t wep_recoil_index = 0;
    std::int32_t wep_accuracy_penalty = 0;
    std::int32_t wep_attr_manager = 0;
    std::int32_t attr_item = 0;
    std::int32_t item_def_index = 0;

    // C_BaseCSGrenade
    std::int32_t nade_pin_pulled = 0;
    std::int32_t nade_throw_time = 0;

    // CCSWeaponBaseVData
    std::int32_t vdata_weapon_type = 0;
    std::int32_t vdata_is_revolver = 0;
    std::int32_t vdata_is_full_auto = 0;
    std::int32_t vdata_num_bullets = 0;
    std::int32_t vdata_zoom_levels = 0;
    std::int32_t vdata_name = 0;
    std::int32_t vdata_damage = 0;
    std::int32_t vdata_range = 0;
    std::int32_t vdata_penetration = 0;
    std::int32_t vdata_armor_ratio = 0;
    std::int32_t vdata_range_modifier = 0;
    std::int32_t vdata_headshot_multiplier = 0;
    std::int32_t vdata_spread = 0;
    std::int32_t vdata_cycle_time = 0;

    // C_PlantedC4
    std::int32_t c4_blow = 0;
    std::int32_t c4_site = 0;
    std::int32_t c4_defused = 0;
    std::int32_t c4_ticking = 0;

    // Movement / Jump / Gravity (subtick bhop + auto-strafe)
    std::int32_t ent_gravity_scale = 0;
    std::int32_t id_world_group = 0;
    std::int32_t move_buttons = 0;
    std::int32_t move_cmd_forward = 0;
    std::int32_t move_cmd_left = 0;
    std::int32_t move_maxspeed = 0;
    std::int32_t move_surface_friction = 0;
    std::int32_t move_stamina = 0;
    std::int32_t move_modern_jump = 0;
    std::int32_t jump_last_press_tick = 0;
    std::int32_t jump_last_press_frac = 0;
    std::int32_t jump_last_landed_tick = 0;
    std::int32_t jump_last_landed_frac = 0;
    std::int32_t wep_mode = 0;
    std::int32_t vdata_max_speed = 0;

    // Observer / Spectators & Player Metadata
    std::int32_t ctrl_observer_pawn = 0;
    std::int32_t ctrl_money_services = 0;
    std::int32_t money_account = 0;
    std::int32_t ctrl_ping = 0;
    std::int32_t pawn_observer_services = 0;
    std::int32_t obs_target = 0;
    std::int32_t obs_mode = 0;
};

bool init_schema(std::uintptr_t schema_system, netvars& out);

}  // namespace shogod::sdk
