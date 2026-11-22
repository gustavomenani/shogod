#include "sdk/schema.hpp"

#include "engine/log.hpp"

#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::sdk {
namespace {

struct schema_field_data {
    const char* name;
    void* type;
    std::int32_t offset;
    std::int32_t metadata_size;
    void* metadata;
};
static_assert(sizeof(schema_field_data) == 0x20);

struct schema_class_info;

struct schema_base_class_info {
    std::uint32_t offset;
    std::uint32_t pad;
    schema_class_info* base_class;
};
static_assert(sizeof(schema_base_class_info) == 0x10);

struct schema_class_info {
    schema_class_info* self;
    const char* name;
    const char* module_name;
    std::uint64_t reserved;
    std::int32_t size;
    std::int16_t field_count;
    std::int16_t static_field_count;
    std::uint8_t alignment;
    std::uint8_t base_class_count;
    std::uint16_t flags;
    std::uint32_t pad;
    schema_field_data* fields;
    schema_base_class_info* base_classes;
};

void* find_client_type_scope(std::uintptr_t schema_system) {
    if (schema_system == 0) return nullptr;
    __try {
        auto** vtable = *reinterpret_cast<void***>(schema_system);
        using find_scope_fn = void* (*)(void*, const char*, const char*);
        auto find_scope = reinterpret_cast<find_scope_fn>(vtable[12]);
        if (find_scope == nullptr) return nullptr;
        return find_scope(reinterpret_cast<void*>(schema_system), "client.dll", nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

schema_class_info* find_declared_class(void* type_scope, const char* class_name) {
    if (type_scope == nullptr || class_name == nullptr) return nullptr;
    __try {
        auto** vtable = *reinterpret_cast<void***>(type_scope);
        using find_class_fn = schema_class_info** (*)(void*, schema_class_info**, const char*);
        auto find_class = reinterpret_cast<find_class_fn>(vtable[2]);
        if (find_class == nullptr) return nullptr;
        schema_class_info* out = nullptr;
        find_class(type_scope, &out, class_name);
        return out;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool find_field_in_hierarchy(const schema_class_info* info, const char* field_name,
                             std::int32_t base_offset, std::int32_t& out_offset, int depth = 0) {
    if (info == nullptr || field_name == nullptr || depth > 12) return false;

    __try {
        if (info->fields != nullptr && info->field_count > 0 && info->field_count < 4096) {
            for (int i = 0; i < info->field_count; ++i) {
                const schema_field_data& f = info->fields[i];
                if (f.name != nullptr && std::strcmp(f.name, field_name) == 0) {
                    out_offset = base_offset + f.offset;
                    return true;
                }
            }
        }

        if (info->base_classes != nullptr && info->base_class_count > 0 &&
            info->base_class_count < 16) {
            for (int i = 0; i < info->base_class_count; ++i) {
                const schema_base_class_info& b = info->base_classes[i];
                if (b.base_class != nullptr &&
                    find_field_in_hierarchy(b.base_class, field_name,
                                            base_offset + static_cast<std::int32_t>(b.offset),
                                            out_offset, depth + 1)) {
                    return true;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

bool lookup(void* scope, const char* class_name, const char* field_name, std::int32_t& out,
            bool required) {
    schema_class_info* cls = find_declared_class(scope, class_name);
    if (cls == nullptr) {
        if (required) log::error("schema: class {} not found", class_name);
        else log::warn("schema: optional class {} not found", class_name);
        return false;
    }

    std::int32_t off = 0;
    if (!find_field_in_hierarchy(cls, field_name, 0, off)) {
        if (required) log::error("schema: {}::{} not found", class_name, field_name);
        else log::warn("schema: optional {}::{} not found", class_name, field_name);
        return false;
    }

    out = off;
    return true;
}

}  // namespace

bool init_schema(std::uintptr_t schema_system, netvars& out) {
    out = {};
    void* scope = find_client_type_scope(schema_system);
    if (scope == nullptr) {
        log::error("schema: FindTypeScopeForModule(client.dll) returned null");
        return false;
    }

    bool ok = true;
    ok &= lookup(scope, "C_CSPlayerPawn", "m_iHealth", out.ent_health, true);
    ok &= lookup(scope, "C_CSPlayerPawn", "m_iTeamNum", out.ent_team, true);
    ok &= lookup(scope, "C_CSPlayerPawn", "m_lifeState", out.ent_life_state, true);
    ok &= lookup(scope, "C_CSPlayerPawn", "m_pGameSceneNode", out.ent_scene_node, true);
    ok &= lookup(scope, "C_CSPlayerPawn", "m_vecVelocity", out.ent_velocity, true);
    ok &= lookup(scope, "C_CSPlayerPawn", "m_fFlags", out.ent_flags, true);
    lookup(scope, "C_BaseEntity", "m_vecAbsVelocity", out.ent_abs_velocity, false);
    lookup(scope, "C_BaseEntity", "m_pEntity", out.ent_identity, false);
    lookup(scope, "C_BaseEntity", "m_hOwnerEntity", out.ent_owner, false);
    lookup(scope, "C_BaseEntity", "m_nActualMoveType", out.ent_actual_move_type, false);
    lookup(scope, "C_BaseEntity", "m_flSimulationTime", out.ent_sim_time, false);
    lookup(scope, "C_BaseEntity", "m_flOldSimulationTime", out.ent_old_sim_time, false);
    lookup(scope, "C_BaseEntity", "m_nSimulationTick", out.ent_sim_tick, false);
    lookup(scope, "C_BaseEntity", "m_hGroundEntity", out.ent_ground_entity, false);
    lookup(scope, "C_BaseEntity", "m_pCollision", out.ent_collision, false);
    lookup(scope, "C_BaseEntity", "m_nSubclassID", out.ent_subclass_id, false);
    lookup(scope, "CCollisionProperty", "m_vecMins", out.collision_mins, false);
    lookup(scope, "CCollisionProperty", "m_vecMaxs", out.collision_maxs, false);
    lookup(scope, "CEntityIdentity", "m_designerName", out.id_designer_name, false);

    ok &= lookup(scope, "CGameSceneNode", "m_vecAbsOrigin", out.node_abs_origin, true);
    lookup(scope, "CGameSceneNode", "m_nodeToWorld", out.node_to_world, false);
    lookup(scope, "CGameSceneNode", "m_angAbsRotation", out.node_abs_rotation, false);
    lookup(scope, "CSkeletonInstance", "m_modelState", out.skel_model_state, false);
    lookup(scope, "CSkeletonInstance", "m_nHitboxSet", out.skel_hitbox_set, false);

    lookup(scope, "C_CSPlayerPawn", "m_vOldOrigin", out.pawn_old_origin, false);
    ok &= lookup(scope, "C_CSPlayerPawn", "m_vecViewOffset", out.pawn_view_offset, true);
    lookup(scope, "C_CSPlayerPawn", "m_pWeaponServices", out.pawn_weapon_services, false);
    lookup(scope, "C_CSPlayerPawn", "m_pMovementServices", out.pawn_movement_services, false);
    lookup(scope, "C_CSPlayerPawn", "m_pCameraServices", out.pawn_camera_services, false);

    lookup(scope, "C_CSPlayerPawn", "m_ArmorValue", out.cs_pawn_armor, false);
    lookup(scope, "C_CSPlayerPawn", "m_bIsScoped", out.cs_pawn_scoped, false);
    lookup(scope, "C_CSPlayerPawn", "m_bResumeZoom", out.cs_pawn_resume_zoom, false);
    lookup(scope, "C_CSPlayerPawn", "m_bIsDefusing", out.cs_pawn_defusing, false);
    lookup(scope, "C_CSPlayerPawn", "m_flFlashDuration", out.cs_pawn_flash_duration, false);
    lookup(scope, "C_CSPlayerPawn", "m_flFlashOverlayAlpha", out.cs_pawn_flash_overlay_alpha, false);
    lookup(scope, "C_CSPlayerPawn", "m_pItemServices", out.cs_pawn_item_services, false);
    lookup(scope, "C_CSPlayerPawn", "m_pAimPunchServices", out.cs_pawn_aim_punch_services, false);
    lookup(scope, "C_CSPlayerPawn", "m_angEyeAngles", out.cs_pawn_eye_angles, false);
    lookup(scope, "C_CSPlayerPawn", "m_entitySpottedState", out.cs_pawn_spotted_state, false);
    lookup(scope, "C_CSPlayerPawn", "m_bGunGameImmunity", out.cs_pawn_immunity, false);
    lookup(scope, "C_CSPlayerPawn", "m_bWaitForNoAttack", out.cs_pawn_wait_for_no_attack, false);
    lookup(scope, "C_CSPlayerPawn", "m_iShotsFired", out.cs_pawn_shots_fired, false);
    lookup(scope, "C_CSPlayerPawnBase", "m_iPlayerState", out.cs_pawn_player_state, false);
    lookup(scope, "C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex", out.cs_pawn_last_spawn_time, false);
    lookup(scope, "EntitySpottedState_t", "m_bSpottedByMask", out.spotted_by_mask, false);

    lookup(scope, "CCSPlayer_ItemServices", "m_bHasDefuser", out.item_has_defuser, false);
    lookup(scope, "CCSPlayer_ItemServices", "m_bHasHelmet", out.item_has_helmet, false);

    ok &= lookup(scope, "CCSPlayerController", "m_bIsLocalPlayerController", out.ctrl_is_local, true);
    ok &= lookup(scope, "CCSPlayerController", "m_hPlayerPawn", out.ctrl_player_pawn, true);
    lookup(scope, "CCSPlayerController", "m_iszPlayerName", out.ctrl_player_name, false);
    lookup(scope, "CCSPlayerController", "m_nTickBase", out.ctrl_tick_base, false);

    lookup(scope, "CPlayer_WeaponServices", "m_hActiveWeapon", out.wep_active, false);
    lookup(scope, "CCSPlayer_WeaponServices", "m_flNextAttack", out.wep_next_attack, false);

    lookup(scope, "CPlayer_CameraServices", "m_vecCsViewPunchAngle", out.cam_view_punch, false);
    lookup(scope, "CPlayer_CameraServices", "m_nCsViewPunchAngleTick", out.cam_view_punch_tick, false);
    lookup(scope, "CPlayer_CameraServices", "m_flCsViewPunchAngleTickRatio", out.cam_view_punch_ratio, false);

    lookup(scope, "C_BasePlayerWeapon", "m_iClip1", out.wep_clip1, false);
    lookup(scope, "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick", out.wep_next_primary_tick, false);
    lookup(scope, "C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio", out.wep_next_primary_ratio, false);
    lookup(scope, "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick", out.wep_next_secondary_tick, false);
    lookup(scope, "C_BasePlayerWeapon", "m_flNextSecondaryAttackTickRatio", out.wep_next_secondary_ratio, false);
    lookup(scope, "C_CSWeaponBase", "m_bInReload", out.wep_in_reload, false);
    lookup(scope, "C_CSWeaponBase", "m_bBurstMode", out.wep_burst_mode, false);
    lookup(scope, "C_CSWeaponBase", "m_nPostponeFireReadyTicks", out.wep_postpone_ready_ticks, false);
    lookup(scope, "C_CSWeaponBase", "m_flPostponeFireReadyFrac", out.wep_postpone_ready_frac, false);
    lookup(scope, "C_CSWeaponBase", "m_bIsHauledBack", out.wep_hauled_back, false);
    lookup(scope, "C_CSWeaponBase", "m_fLastShotTime", out.wep_last_shot_time, false);
    lookup(scope, "C_CSWeaponBase", "m_flWatTickOffset", out.wep_wat_tick_offset, false);
    lookup(scope, "C_CSWeaponBaseGun", "m_zoomLevel", out.wep_zoom_level, false);
    lookup(scope, "C_CSWeaponBase", "m_flRecoilIndex", out.wep_recoil_index, false);
    lookup(scope, "C_CSWeaponBase", "m_fAccuracyPenalty", out.wep_accuracy_penalty, false);
    lookup(scope, "C_BasePlayerWeapon", "m_AttributeManager", out.wep_attr_manager, false);
    lookup(scope, "C_AttributeContainer", "m_Item", out.attr_item, false);
    lookup(scope, "C_EconItemView", "m_iItemDefinitionIndex", out.item_def_index, false);

    lookup(scope, "C_BaseCSGrenade", "m_bPinPulled", out.nade_pin_pulled, false);
    lookup(scope, "C_BaseCSGrenade", "m_fThrowTime", out.nade_throw_time, false);

    lookup(scope, "CCSWeaponBaseVData", "m_WeaponType", out.vdata_weapon_type, false);
    lookup(scope, "CCSWeaponBaseVData", "m_bIsRevolver", out.vdata_is_revolver, false);
    lookup(scope, "CCSWeaponBaseVData", "m_bIsFullAuto", out.vdata_is_full_auto, false);
    lookup(scope, "CCSWeaponBaseVData", "m_nNumBullets", out.vdata_num_bullets, false);
    lookup(scope, "CCSWeaponBaseVData", "m_nZoomLevels", out.vdata_zoom_levels, false);
    lookup(scope, "CCSWeaponBaseVData", "m_szName", out.vdata_name, false);
    lookup(scope, "CCSWeaponBaseVData", "m_nDamage", out.vdata_damage, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flRange", out.vdata_range, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flPenetration", out.vdata_penetration, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flArmorRatio", out.vdata_armor_ratio, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flRangeModifier", out.vdata_range_modifier, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flHeadshotMultiplier", out.vdata_headshot_multiplier, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flSpread", out.vdata_spread, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flCycleTime", out.vdata_cycle_time, false);

    lookup(scope, "C_PlantedC4", "m_flC4Blow", out.c4_blow, false);
    lookup(scope, "C_PlantedC4", "m_nBombSite", out.c4_site, false);
    lookup(scope, "C_PlantedC4", "m_bBombDefused", out.c4_defused, false);
    lookup(scope, "C_PlantedC4", "m_bBombTicking", out.c4_ticking, false);

    lookup(scope, "C_BaseEntity", "m_flActualGravityScale", out.ent_gravity_scale, false);
    lookup(scope, "CEntityIdentity", "m_worldGroupId", out.id_world_group, false);
    lookup(scope, "CPlayer_MovementServices", "m_nButtons", out.move_buttons, false);
    lookup(scope, "CPlayer_MovementServices", "m_flCmdForwardMove", out.move_cmd_forward, false);
    lookup(scope, "CPlayer_MovementServices", "m_flCmdLeftMove", out.move_cmd_left, false);
    lookup(scope, "CPlayer_MovementServices", "m_flMaxspeed", out.move_maxspeed, false);
    lookup(scope, "CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction", out.move_surface_friction, false);
    lookup(scope, "CCSPlayer_MovementServices", "m_flStamina", out.move_stamina, false);
    lookup(scope, "CCSPlayer_MovementServices", "m_ModernJump", out.move_modern_jump, false);
    lookup(scope, "CCSPlayerModernJump", "m_nLastActualJumpPressTick", out.jump_last_press_tick, false);
    lookup(scope, "CCSPlayerModernJump", "m_flLastActualJumpPressFrac", out.jump_last_press_frac, false);
    lookup(scope, "CCSPlayerModernJump", "m_nLastLandedTick", out.jump_last_landed_tick, false);
    lookup(scope, "CCSPlayerModernJump", "m_flLastLandedFrac", out.jump_last_landed_frac, false);
    lookup(scope, "C_CSWeaponBase", "m_weaponMode", out.wep_mode, false);
    lookup(scope, "CCSWeaponBaseVData", "m_flMaxSpeed", out.vdata_max_speed, false);

    lookup(scope, "CCSPlayerController", "m_hObserverPawn", out.ctrl_observer_pawn, false);
    lookup(scope, "CCSPlayerController", "m_pInGameMoneyServices", out.ctrl_money_services, false);
    lookup(scope, "CCSPlayerController_InGameMoneyServices", "m_iAccount", out.money_account, false);
    lookup(scope, "CCSPlayerController", "m_iPing", out.ctrl_ping, false);
    lookup(scope, "C_BasePlayerPawn", "m_pObserverServices", out.pawn_observer_services, false);
    lookup(scope, "CPlayer_ObserverServices", "m_hObserverTarget", out.obs_target, false);
    lookup(scope, "CPlayer_ObserverServices", "m_iObserverMode", out.obs_mode, false);

    if (ok && (out.ent_health <= 0 || out.ent_health >= 0x4000)) {
        log::error("schema: m_iHealth offset {:#x} failed sanity range", out.ent_health);
        ok = false;
    }

    out.valid = ok;
    log::info(
        "schema: {} — health={:#x} team={:#x} node={:#x} origin={:#x} modelState={:#x} "
        "pawn={:#x} local={:#x} aimPunch={:#x} moveSvc={:#x}",
        ok ? "OK" : "FAIL", out.ent_health, out.ent_team, out.ent_scene_node,
        out.node_abs_origin, out.skel_model_state, out.ctrl_player_pawn, out.ctrl_is_local,
        out.cs_pawn_aim_punch_services, out.pawn_movement_services);
    return ok;
}

}  // namespace shogod::sdk
