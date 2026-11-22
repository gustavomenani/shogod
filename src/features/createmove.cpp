#include "features/createmove.hpp"

#include "config/bind_state.hpp"
#include "engine/log.hpp"
#include "features/aaa/antiaim.hpp"
#include "features/math/angles.hpp"
#include "features/math/hitchance.hpp"
#include "features/math/hull.hpp"
#include "sdk/bones.hpp"
#include "sdk/hitboxes.hpp"
#include "sdk/trace.hpp"
#include "sdk/usercmd.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::features {
namespace {

constexpr std::uint32_t k_fl_onground = 1u << 0;
constexpr int k_movetype_ladder = 9;
constexpr int k_movetype_noclip = 8;
constexpr int k_movetype_observer = 10;
constexpr int k_weapontype_knife = 0;
constexpr int k_weapontype_pistol = 1;
constexpr int k_weapontype_machinegun = 6;
constexpr int k_weapontype_taser = 8;
constexpr int k_weapontype_grenade = 9;

template <typename T>
bool safe_read(std::uintptr_t addr, T& out) noexcept {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFULL) return false;
    __try {
        out = *reinterpret_cast<const T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float angle_distance_deg(const math::angles& a, const math::angles& b) noexcept {
    const float dot = math::forward_from_angles(a).dot(math::forward_from_angles(b));
    return std::acos(std::clamp(dot, -1.0f, 1.0f)) * 57.29578f;
}

bool trace_clear_segment(const math::vec3& from, const math::vec3& to,
                         std::uintptr_t skip, bool& clear) noexcept {
    if (!from.finite() || !to.finite()) return false;
    const float start[3] = {from.x, from.y, from.z};
    const float end[3] = {to.x, to.y, to.z};
    float fraction = 0.0f;
    if (!sdk::trace_fraction_skip(start, end, reinterpret_cast<void*>(skip), fraction) ||
        !std::isfinite(fraction)) return false;
    clear = fraction == 1.0f;
    return true;
}

math::vec3 compute_wall_clamped_shift(const math::vec3& origin, const math::vec3& velocity,
                                      float horizon_ms, std::uintptr_t skip_pawn) noexcept {
    if (!origin.finite() || !velocity.finite() || horizon_ms <= 0.5f) return {};
    const math::vec3 raw = extrapolate_point(math::vec3{}, velocity, horizon_ms);
    const float len_sq = raw.length_sq();
    if (!std::isfinite(len_sq) || len_sq < 0.25f) return {};
    if (!sdk::trace_ready()) return raw;

    // Trace from chest height along predicted velocity so enemies stopping against a wall
    // are never extrapolated inside solid geometry.
    const math::vec3 probe = origin + math::vec3{0.0f, 0.0f, 36.0f};
    const math::vec3 dst = probe + raw;
    const float s[3] = {probe.x, probe.y, probe.z};
    const float e[3] = {dst.x, dst.y, dst.z};
    float frac = 1.0f;
    if (sdk::trace_fraction_skip(s, e, reinterpret_cast<void*>(skip_pawn), frac) &&
        std::isfinite(frac) && frac < 1.0f) {
        return raw * std::clamp(frac * 0.85f, 0.0f, 1.0f);
    }
    return raw;
}

bool build_extrapolated_record(const lag_record& src, const math::vec3& origin,
                               const math::vec3& velocity, float horizon_ms,
                               int target_tick, lag_record& dst) {
    if (!src.valid || src.hitboxes.geometries.empty()) return false;
    const math::vec3 shift = compute_wall_clamped_shift(origin, velocity, horizon_ms, src.pawn);
    if (shift.length_sq() < 0.25f) return false;

    dst = src;
    dst.simulation_tick = target_tick;
    dst.origin = dst.origin + shift;
    for (auto& g : dst.hitboxes.geometries) {
        g.first = g.first + shift;
        g.last = g.last + shift;
        g.center = g.center + shift;
    }
    for (auto& tm : dst.hitboxes.transforms) {
        tm.row0[0] += shift.x;
        tm.row0[1] += shift.y;
        tm.row0[2] += shift.z;
    }
    return true;
}

void zero_subtick_view_deltas(sdk::base_user_cmd_pb* base) noexcept {
    sdk::clear_subtick_view_deltas(base);
}

void run_peek_assist(const config::config_v1&, const sdk::frame_snapshot& frame,
                     sdk::user_command* cmd, bool on_ground, int move_type,
                     bool peek_active, peek_assist_state& pa) noexcept {
    if (!peek_active || !frame.local_alive ||
        move_type == k_movetype_ladder || move_type == k_movetype_noclip ||
        move_type == k_movetype_observer) {
        pa = {};
        return;
    }

    if (!pa.active) {
        pa.origin = frame.local_origin;
        pa.active = true;
        pa.returning = false;
        pa.has_moved = false;
        pa.pending_return = false;
        pa.stall_ticks = 0;
        pa.last_cmd = -1;
        pa.best_dist = 0.0f;
    }

    const bool moving_now = (cmd->button_state.held & k_move_button_mask) != 0;
    if (moving_now) pa.has_moved = true;

    if (!pa.returning && pa.pending_return) {
        pa.pending_return = false;
        pa.returning = true;
        pa.best_dist = std::hypot(pa.origin.x - frame.local_origin.x,
                                  pa.origin.y - frame.local_origin.y);
        pa.stall_ticks = 0;
        pa.last_cmd = cmd->command_number;
    }

    if (!pa.returning) return;

    math::vec3 delta = pa.origin - frame.local_origin;
    delta.z = 0.0f;
    const float dist = std::hypot(delta.x, delta.y);
    step_peek_assist_stall(pa, cmd->command_number, dist);

    if ((on_ground && dist < 10.0f) || pa.stall_ticks > 64) {
        pa.returning = false;
        pa.has_moved = false;
        pa.pending_return = false;
        pa.stall_ticks = 0;
        pa.last_cmd = -1;
        cmd->pb.base->set_forwardmove(0.0f);
        cmd->pb.base->set_leftmove(0.0f);
        cmd->button_state.set_button_state(k_move_button_mask, sdk::in_button_state::up);
        return;
    }

    if (!on_ground || dist < 1e-3f) return;
    delta = delta / dist;
    const float yaw = cmd->pb.base->viewangles->y;
    const math::vec3 fwd = math::forward_from_angles({0.0f, yaw});
    const math::vec3 right{fwd.y, -fwd.x, 0.0f};
    const float raw_f = delta.dot(fwd);
    const float raw_l = -delta.dot(right);
    const float peak = std::max(std::fabs(raw_f), std::fabs(raw_l));
    // Normalize onto the unit square so CS2 SetupMove's roundf quantizer always steps cleanly back to origin.
    const float scale = peak > 1e-3f ? (1.0f / peak) : 1.0f;
    const float fmove = std::clamp(raw_f * scale, -1.0f, 1.0f);
    const float lmove = std::clamp(raw_l * scale, -1.0f, 1.0f);

    cmd->pb.base->set_forwardmove(fmove);
    cmd->pb.base->set_leftmove(lmove);
    cmd->button_state.set_button_state(k_move_button_mask, sdk::in_button_state::up);
    sync_move_held_buttons(cmd, fmove, lmove);
}

bool safe_from_global_tick(std::uintptr_t fn_addr, int& out_tick, int global_tick,
                           std::uint32_t world_group) noexcept {
    if (fn_addr == 0) return false;
    using fn_t = void (*)(int*, int, std::uint32_t);
    __try {
        reinterpret_cast<fn_t>(fn_addr)(&out_tick, global_tick, world_group);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void run_createmove(std::uintptr_t csgo_input, const sdk::anchors& anchors,
                    const sdk::netvars& nv, const config::config_v1& cfg,
                    const sdk::frame_snapshot& frame, backtrack_history& bt,
                    createmove_state& state, std::uint32_t active_binds) noexcept {
    if (!frame.in_game) bt.reset();
    if (!anchors.input_ok || csgo_input == 0 || !frame.in_game || !frame.local_alive ||
        frame.local_pawn == 0 || frame.local_controller == 0) {
        state.delay = {};
        state.brake_commands = 0;
        state.lagcomp.reset();
        state.peek_assist = {};
        state.ground_move = {};
        state.freestanding = {};
        reset_strafe_state(state);
        state.bhop_space_latched = false;
        state.bhop_jumped_cmd = -1;
        state.bhop_jumped_when = 0.0f;
        state.tick_latch = {};
        state.auto_pistol_wait = 0;
        state.auto_pistol_release = false;
        state.last_auto_pistol_cmd = -1;
        state.last_bt_command = -1;
        state.rage_cache = {};
        return;
    }

    auto* mgr = sdk::get_command_manager(anchors, frame.local_controller);
    sdk::user_command* cmd = mgr ? mgr->get_user_command() : nullptr;
    if (!cmd || !cmd->pb.base || !cmd->pb.base->viewangles) return;

    auto* base_cmd = cmd->pb.base;
    const bool weapon_switching = base_cmd->weaponselect > 0;

    auto* input_view = reinterpret_cast<sdk::csgo_input_view*>(csgo_input);
    const math::angles camera_view = input_view->viewangles();
    if (!std::isfinite(camera_view.pitch) || !std::isfinite(camera_view.yaw)) return;

    std::uint32_t local_flags = 0;
    if (nv.ent_flags != 0) safe_read(frame.local_pawn + nv.ent_flags, local_flags);
    const bool raw_on_ground = (local_flags & k_fl_onground) != 0;

    int raw_move_type = 2;
    if (nv.ent_actual_move_type != 0) safe_read(frame.local_pawn + nv.ent_actual_move_type, raw_move_type);

    math::vec3 raw_local_vel{};
    const bool raw_has_velocity = nv.ent_velocity != 0 &&
                                  safe_read(frame.local_pawn + nv.ent_velocity, raw_local_vel) &&
                                  raw_local_vel.finite();
    const float raw_speed_2d = raw_has_velocity ? std::hypot(raw_local_vel.x, raw_local_vel.y) : 0.0f;

    // CS2 FinishMove / ResetJumpButton calls ForceButtonState(IN_JUMP) at the end of a jump tick,
    // clearing +jump in CCSGOInput while Spacebar is still physically held. Latch physical Spacebar
    // once in-game jump/movement is active so continuous BunnyHop never drops a landing jump.
    const bool raw_cmd_jump = (cmd->button_state.held & sdk::k_in_jump) != 0;
    const bool vk_space_down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (raw_cmd_jump ||
        (vk_space_down && (!raw_on_ground || (cmd->button_state.held & k_move_button_mask) != 0))) {
        state.bhop_space_latched = vk_space_down;
    } else if (!vk_space_down) {
        state.bhop_space_latched = false;
    }
    const bool raw_jump_held = raw_cmd_jump || (state.bhop_space_latched && vk_space_down);

    std::uintptr_t move_svc = 0;
    if (nv.pawn_movement_services != 0) {
        safe_read(frame.local_pawn + nv.pawn_movement_services, move_svc);
    }
    std::uint64_t raw_prev_move_buttons = 0;
    if (move_svc != 0 && nv.move_buttons != 0) {
        safe_read(move_svc + nv.move_buttons, raw_prev_move_buttons);
    }

    // Latch start-of-tick physics and raw movement intent once per command_number so
    // Sub-frame 0 ClientPrediction (which mutates local_pawn FL_ONGROUND, velocity, and
    // m_nButtons in-place) cannot self-cancel BunnyHop or double-rotate movement inputs
    // across Sub-frames 1..4 of the same 64 Hz command.
    if (state.tick_latch.command_number != cmd->command_number) {
        state.tick_latch.command_number = cmd->command_number;
        state.tick_latch.on_ground = raw_on_ground;
        state.tick_latch.move_type = raw_move_type;
        state.tick_latch.has_velocity = raw_has_velocity;
        state.tick_latch.velocity = raw_local_vel;
        state.tick_latch.speed_2d = raw_speed_2d;
        state.tick_latch.jump_held = raw_jump_held;
        state.tick_latch.prev_jump_held = (raw_prev_move_buttons & sdk::k_in_jump) != 0;
        state.tick_latch.raw_held = cmd->button_state.held;
        state.tick_latch.pre_aa_fwd = base_cmd->forwardmove;
        state.tick_latch.pre_aa_left = base_cmd->leftmove;
    } else {
        if (raw_jump_held) {
            state.tick_latch.jump_held = true;
        }
        // Keep start-of-tick `on_ground` and `prev_jump_held` latched from Sub-frame 0.
        // ClientPrediction mutates `local_pawn->m_fFlags` to the POST-tick state on Sub-frames 1..4;
        // promoting `on_ground` mid-tick would fire a t=0 jump while the player is still airborne
        // at tick start, triggering sv_jump_spam_penalty_time and causing a 1-tick ground-stick.
    }

    const bool on_ground = state.tick_latch.on_ground;
    const int move_type = state.tick_latch.move_type;
    const bool has_velocity = state.tick_latch.has_velocity;
    const math::vec3 local_vel = state.tick_latch.velocity;
    const float speed_2d = state.tick_latch.speed_2d;

    sdk::weapon_info active_wep{};
    const bool has_wep =
        sdk::read_active_weapon(anchors.entity_system, nv, frame.local_pawn, active_wep);
    const int wep_group = has_wep
                              ? sdk::weapon_group_from_def_index(active_wep.def_index)
                              : static_cast<int>(config::weapon_group::rifle);
    config::rage_weapon w_cfg =
        cfg.rage.resolve_weapon(has_wep ? active_wep.def_index : 0, wep_group);

    const bool safe_mode = cfg.misc.block_server_detected;
    if (safe_mode) {
        w_cfg.min_damage = std::max(w_cfg.min_damage, 50.0f);
        w_cfg.multipoint = std::min(w_cfg.multipoint, 25.0f);
        w_cfg.penetration = false;
    }

    // ---- LagCompensation + Backtrack Ghost history -------------------------
    if (cfg.rage.backtrack && cfg.rage.backtrack_ms >= 15.0f && frame.tick_valid) {
        state.lagcomp.update(anchors, nv, frame);
        if (cfg.visuals.backtrack_ghost && cmd->command_number != state.last_bt_command) {
            state.last_bt_command = cmd->command_number;
            for (int i = 0; i < frame.count; ++i) {
                const sdk::player_snapshot& p = frame.players[i];
                if (!p.alive || !p.enemy || p.pawn == frame.local_pawn) continue;
                bt.push(p.index, p, frame.curtime);
            }
        }
    }

    // ---- Movement assists (hybrid native + subtick BunnyHop) ---------------
    float next_jump_frac = 0.0f;
    int last_landed_tick = -1;
    float last_landed_frac = 0.0f;
    if (move_svc != 0 && nv.move_modern_jump != 0) {
        const std::uintptr_t mj = move_svc + static_cast<std::uintptr_t>(nv.move_modern_jump);
        if (anchors.from_global_tick != 0) {
            int client_tick = frame.tick_valid ? frame.tick : 0;
            if (nv.id_world_group != 0) {
                std::uintptr_t identity = 0;
                std::uint32_t world_group = 0;
                if (nv.ent_identity != 0 && safe_read(frame.local_pawn + nv.ent_identity, identity) &&
                    identity != 0 && safe_read(identity + nv.id_world_group, world_group)) {
                    safe_from_global_tick(anchors.from_global_tick, client_tick, frame.tick,
                                          world_group);
                }
            }
            int press_tick = 0;
            float press_frac = 0.0f;
            float penalty_sec = 0.015625f;
            sdk::read_cvar_float(anchors.cvar, "sv_jump_spam_penalty_time", penalty_sec);
            if (nv.jump_last_press_tick != 0 && nv.jump_last_press_frac != 0 &&
                safe_read(mj + nv.jump_last_press_tick, press_tick) &&
                safe_read(mj + nv.jump_last_press_frac, press_frac)) {
                next_jump_frac = compute_next_jump_frac(press_tick, press_frac, client_tick, penalty_sec);
            }
        }
        // m_nLastLandedTick is in global tickbase space (matching Reverie Movement.cpp).
        if (frame.tick_valid && nv.jump_last_landed_tick != 0 && nv.jump_last_landed_frac != 0) {
            int ltick = -1;
            float lfrac = 0.0f;
            if (safe_read(mj + nv.jump_last_landed_tick, ltick) &&
                safe_read(mj + nv.jump_last_landed_frac, lfrac) && ltick == frame.tick &&
                lfrac >= 0.0f && lfrac < 1.0f) {
                last_landed_tick = ltick;
                last_landed_frac = lfrac;
            }
        }
    }

    const bool jump_held = state.tick_latch.jump_held;
    const bool prev_jump_held = state.tick_latch.prev_jump_held;
    const bool valid_move_type = move_type != k_movetype_ladder &&
                                 move_type != k_movetype_noclip &&
                                 move_type != k_movetype_observer;
    const bool bhop_active =
        (active_binds & config::feature_bit(config::bound_feature::bhop)) != 0 &&
        valid_move_type;
    bool bhop_do_jump = false;
    if (bhop_active) {
        const bool has_subtick_alloc =
            anchors.create_subtick_move != 0 && anchors.add_element_to_rep != 0;

        if (state.bhop_jumped_cmd == cmd->command_number) {
            bhop_do_jump = true;
        } else if (next_jump_frac < 1.0f && jump_held) {
            // Jump only on ground at tick start (Sub-frame 0 on_ground) or an exact mid-tick
            // landing confirmed by CS2's own CModernJump prediction (never a bullet ray guess).
            const bool modern_landed =
                !on_ground && last_landed_tick >= 0 &&
                last_landed_frac >= 0.0f && last_landed_frac < 1.0f && has_subtick_alloc;
            const bool landing_this_tick = on_ground || modern_landed;
            if (landing_this_tick) {
                if (has_subtick_alloc || !prev_jump_held) {
                    bhop_do_jump = true;
                    state.bhop.last_cmd = cmd->command_number;
                    state.bhop.was_on_ground = true;
                    state.bhop.cmd_jump = true;
                    state.bhop.release_next = false;
                } else {
                    bhop_do_jump = step_bhop(state.bhop, cmd->command_number, true, true);
                }
                if (bhop_do_jump) {
                    const float land_w = modern_landed ? last_landed_frac : 0.0f;
                    state.bhop_jumped_cmd = cmd->command_number;
                    state.bhop_jumped_when =
                        std::clamp(std::max(land_w, next_jump_frac), 0.0f, 62.0f / 64.0f);
                    state.bhop_used_subtick =
                        modern_landed || (state.bhop_jumped_when > 0.0f) || prev_jump_held;

                    static ULONGLONG next_bhop_log = 0;
                    const ULONGLONG now = GetTickCount64();
                    if (now >= next_bhop_log) {
                        next_bhop_log = now + 2000;
                        log::info("bhop: cmd={} gnd={} mland={} prevj={} sub={} when={:.3f} nj={:.3f} spd={:.0f}",
                                  cmd->command_number, on_ground ? 1 : 0, modern_landed ? 1 : 0,
                                  prev_jump_held ? 1 : 0, state.bhop_used_subtick ? 1 : 0,
                                  state.bhop_jumped_when, next_jump_frac, speed_2d);
                    }
                }
            } else {
                bhop_do_jump = step_bhop(state.bhop, cmd->command_number, false, true);
            }
        } else {
            (void)step_bhop(state.bhop, cmd->command_number, false, jump_held);
            bhop_do_jump = false;
        }

        if (jump_held) {
            strip_subtick_buttons(base_cmd, sdk::k_in_jump);
            if (bhop_do_jump && state.bhop_jumped_cmd == cmd->command_number) {
                if (state.bhop_used_subtick && has_subtick_alloc) {
                    auto* rel = sdk::add_subtick_move_step(anchors, cmd, state.bhop_jumped_when);
                    auto* prs = rel ? sdk::add_subtick_move_step(anchors, cmd, state.bhop_jumped_when)
                                    : nullptr;
                    if (rel && prs) {
                        rel->set_button(sdk::k_in_jump);
                        rel->set_pressed(false);
                        prs->set_button(sdk::k_in_jump);
                        prs->set_pressed(true);
                        // Mid-tick landing: keep held=0 at t=0 so CS2 never sees an early air press;
                        // t=0 grounded jump: keep held=1 (toggle=0) so subtick prs supplies the single edge.
                        cmd->button_state.set_button_state(
                            sdk::k_in_jump,
                            state.bhop_jumped_when > 0.0f ? sdk::in_button_state::up
                                                          : sdk::in_button_state::down);
                    } else {
                        cmd->button_state.set_button_state(sdk::k_in_jump, sdk::in_button_state::up_down);
                    }
                } else {
                    // Clean native 0->1 tick-start press: prev tick had IN_JUMP=0, so up_down
                    // (held=1, toggle=1) fires CheckJumpButton immediately at t=0 before friction.
                    cmd->button_state.set_button_state(sdk::k_in_jump, sdk::in_button_state::up_down);
                }
            } else {
                cmd->button_state.set_button_state(sdk::k_in_jump, sdk::in_button_state::up);
            }
        }
    } else {
        state.bhop = {};
        state.bhop_space_latched = false;
        state.bhop_jumped_cmd = -1;
        state.bhop_used_subtick = false;
    }
    if (should_run_edge_jump(safe_mode, cfg.misc.movement.edge_jump, bhop_active, jump_held,
                             on_ground, speed_2d, move_type) &&
        sdk::trace_ready()) {
        const math::vec3 ahead_dir = math::vec3{local_vel.x, local_vel.y, 0.0f}.normalised();
        if (ahead_dir.length_sq() > 0.5f) {
            const math::vec3 ahead = frame.local_origin + ahead_dir * 8.0f;
            const float s[3] = {ahead.x, ahead.y, ahead.z + 4.0f};
            const float e[3] = {ahead.x, ahead.y, ahead.z - 32.0f};
            float fraction = 0.0f;
            if (sdk::trace_fraction_skip(s, e, reinterpret_cast<void*>(frame.local_pawn), fraction) &&
                fraction == 1.0f) {
                strip_subtick_buttons(base_cmd, sdk::k_in_jump);
                cmd->button_state.set_button_state(sdk::k_in_jump, sdk::in_button_state::up_down);
            }
        }
    }

    // Use the start-of-tick latched camera-space wish inputs so Sub-frames 1..4 of the same
    // command_number never re-read rotated AA/Auto-Strafe movement buttons from Sub-frame 0.
    const float pre_aa_fwd = state.tick_latch.pre_aa_fwd;
    const float pre_aa_left = state.tick_latch.pre_aa_left;
    const std::uint64_t raw_held = state.tick_latch.raw_held;
    const bool key_fwd = (raw_held & sdk::k_in_forward) != 0 || pre_aa_fwd > 0.05f;
    const bool key_back = (raw_held & sdk::k_in_back) != 0 || pre_aa_fwd < -0.05f;
    const bool key_left = (raw_held & sdk::k_in_moveleft) != 0 || pre_aa_left > 0.05f;
    const bool key_right = (raw_held & sdk::k_in_moveright) != 0 || pre_aa_left < -0.05f;
    const float wish_dir_f = (key_fwd ? 1.0f : 0.0f) - (key_back ? 1.0f : 0.0f);
    const float wish_dir_l = (key_left ? 1.0f : 0.0f) - (key_right ? 1.0f : 0.0f);
    const float wish_f = (std::fabs(pre_aa_fwd) > 0.05f) ? pre_aa_fwd : wish_dir_f;
    const float wish_l = (std::fabs(pre_aa_left) > 0.05f) ? pre_aa_left : wish_dir_l;
    const math::vec3 wish_world_dir = compute_wish_world_dir(camera_view.yaw, wish_f, wish_l);
    const bool has_move_input = std::fabs(pre_aa_fwd) > 0.05f || std::fabs(pre_aa_left) > 0.05f ||
                                (raw_held & k_move_button_mask) != 0;

    const bool want_autostrafe =
        !safe_mode && (active_binds & config::feature_bit(config::bound_feature::auto_strafe)) &&
        (!on_ground || bhop_do_jump) && valid_move_type;
    const float strafe_f_wish = want_autostrafe ? wish_dir_f : 0.0f;
    const float strafe_l_wish = want_autostrafe ? wish_dir_l : 0.0f;

    // ---- Anti-Aim (isolated from camera via SendMove) ----------------------
    bool grenade_active = false;
    if (has_wep && active_wep.weapon_type == k_weapontype_grenade) {
        std::uint8_t pin = 0;
        float throw_t = 0.0f;
        if (nv.nade_pin_pulled != 0) safe_read(active_wep.entity + nv.nade_pin_pulled, pin);
        if (nv.nade_throw_time != 0) safe_read(active_wep.entity + nv.nade_throw_time, throw_t);
        grenade_active = pin != 0 || throw_t > 0.0f ||
                         (cmd->button_state.held & (sdk::k_in_attack | sdk::k_in_attack2)) != 0;
    }

    const bool aa_gate = compute_aa_gate(
        cfg.anti_aim, active_binds, safe_mode,
        (cmd->button_state.held & sdk::k_in_use) != 0,
        move_type, grenade_active, on_ground, speed_2d, has_move_input);
    if (aa_gate) {
        float enemy_yaw = camera_view.yaw;
        math::vec3 enemy_pos{};
        bool found_enemy = false;
        if ((cfg.anti_aim.at_targets || cfg.anti_aim.freestanding) && frame.local_eye_valid) {
            float best_vis_d2 = 1e30f;
            float best_any_d2 = 1e30f;
            float vis_yaw = camera_view.yaw;
            float any_yaw = camera_view.yaw;
            math::vec3 vis_pos{};
            math::vec3 any_pos{};
            bool found_vis = false;
            bool found_any = false;
            for (int i = 0; i < frame.count; ++i) {
                const sdk::player_snapshot& p = frame.players[i];
                if (!p.alive || !p.enemy || p.pawn == frame.local_pawn) continue;
                const math::vec3 target_pos =
                    (p.eye - p.origin).length_sq() > 1.0f ? p.eye : p.origin;
                const math::vec3 delta = target_pos - frame.local_eye;
                const float d2 = delta.x * delta.x + delta.y * delta.y;
                if (d2 <= 1.0f) continue;
                const float yaw = std::atan2(delta.y, delta.x) * 57.29578f;
                if (p.visible && d2 < best_vis_d2) {
                    best_vis_d2 = d2;
                    vis_yaw = yaw;
                    vis_pos = target_pos;
                    found_vis = true;
                }
                if (d2 < best_any_d2) {
                    best_any_d2 = d2;
                    any_yaw = yaw;
                    any_pos = target_pos;
                    found_any = true;
                }
            }
            if (found_vis || found_any) {
                found_enemy = true;
                enemy_yaw = found_vis ? vis_yaw : any_yaw;
                enemy_pos = found_vis ? vis_pos : any_pos;
            }
        }

        float aa_pitch = camera_view.pitch;
        switch (cfg.anti_aim.base_pitch) {
            case config::pitch_mode::down: aa_pitch = 89.0f; break;
            case config::pitch_mode::up: aa_pitch = -89.0f; break;
            case config::pitch_mode::zero: aa_pitch = 0.0f; break;
            case config::pitch_mode::off:
            default: break;
        }
        aa_pitch = math::clamp_pitch(aa_pitch);

        freestanding_decision free_dec{};
        if (cfg.anti_aim.freestanding && found_enemy && frame.local_eye_valid && sdk::trace_ready()) {
            auto trace_free_probe = [&](float yaw_deg, float& out_frac) -> bool {
                const math::vec3 probe = freestanding_head_probe(frame.local_eye, yaw_deg, aa_pitch);
                const math::vec3 target = freestanding_trace_target(enemy_pos, probe, 16.0f);
                const float s[3] = {target.x, target.y, target.z};
                const float e[3] = {probe.x, probe.y, probe.z};
                float frac = 1.0f;
                if (!sdk::trace_fraction_skip(s, e, reinterpret_cast<void*>(frame.local_pawn), frac) ||
                    !std::isfinite(frac)) {
                    return false;
                }
                out_frac = frac;
                return true;
            };
            const float center_yaw = math::normalize_yaw(enemy_yaw + 180.0f);
            float center_frac = 1.0f;
            const bool center_valid = trace_free_probe(center_yaw, center_frac);
            if (!center_valid) {
                free_dec = select_freestanding_yaw(enemy_yaw, false, 1.0f, {}, state.freestanding);
            } else if (center_frac < 0.96f) {
                free_dec = select_freestanding_yaw(enemy_yaw, true, center_frac, {}, state.freestanding);
            } else {
                std::array<freestanding_candidate, k_freestanding_offsets.size()> free_cands{};
                for (std::size_t fi = 0; fi < k_freestanding_offsets.size(); ++fi) {
                    const float off = k_freestanding_offsets[fi];
                    const float cand_yaw = math::normalize_yaw(enemy_yaw + off);
                    float frac = 1.0f;
                    const bool ok = trace_free_probe(cand_yaw, frac);
                    free_cands[fi] = freestanding_candidate{off, frac, ok};
                }
                free_dec = select_freestanding_yaw(enemy_yaw, true, center_frac,
                                                   free_cands, state.freestanding);
            }
        } else {
            state.freestanding = {};
        }

        config::anti_aim_cfg aa_cfg = cfg.anti_aim;
        aa_cfg.enabled = true;
        antiaim_context aa_ctx{};
        aa_ctx.on_ground = on_ground;
        aa_ctx.has_move_input = has_move_input;
        aa_ctx.speed_2d = speed_2d;
        aa_ctx.shooting = (cmd->button_state.held & sdk::k_in_attack) != 0;
        aa_ctx.suppressed = false;
        aa_ctx.command_number = cmd->command_number;
        aa_ctx.seed = frame.local_index;
        aa_ctx.found_enemy = found_enemy;
        aa_ctx.enemy_yaw = enemy_yaw;
        aa_ctx.weapon_group = wep_group;
        aa_ctx.weapon_type = has_wep ? active_wep.weapon_type : -1;
        aa_ctx.freestanding = free_dec;

        const math::angles aa_angles = apply_antiaim_full(aa_cfg, camera_view, aa_ctx);
        base_cmd->viewangles->set(aa_angles.pitch, aa_angles.yaw, 0.0f);
        zero_subtick_view_deltas(base_cmd);
        strip_subtick_buttons(base_cmd, k_move_button_mask);
    } else {
        state.freestanding = {};
    }

    // Reverie-adapted Subtick Auto-Strafe: builds smooth continuous air acceleration across
    // up to 16 subtick slices per tick while locking base_cmd->viewangles->y (ZERO jitter).
    bool strafe_applied = false;
    autostrafe_output strafe_out{};
    if (want_autostrafe) {
        strip_subtick_analog_moves(base_cmd);
        strip_subtick_buttons(base_cmd, k_move_button_mask);
        zero_subtick_view_deltas(base_cmd);

        const float ref_yaw = aa_gate ? base_cmd->viewangles->y : camera_view.yaw;
        subtick_strafe_input sin{};
        sin.command_number = cmd->command_number;
        sin.camera_yaw = camera_view.yaw;
        sin.move_ref_yaw = ref_yaw;
        sin.velocity = local_vel;
        sin.buttons = state.tick_latch.raw_held & k_move_button_mask;
        sin.on_ground = on_ground;
        sin.available_slots = std::clamp(32 - base_cmd->subtick_moves.current_size - 2, 1, 16);

        float sv_maxspd = 260.0f;
        float sv_airacc = 12.0f;
        float sv_wish = 30.0f;
        float sv_grav = 800.0f;
        float sv_stam = 60.0f;
        bool sv_quant = true;
        bool sv_ebhop = false;
        sdk::read_cvar_float(anchors.cvar, "sv_maxspeed", sv_maxspd);
        sdk::read_cvar_float(anchors.cvar, "sv_airaccelerate", sv_airacc);
        sdk::read_cvar_float(anchors.cvar, "sv_air_max_wishspeed", sv_wish);
        sdk::read_cvar_float(anchors.cvar, "sv_gravity", sv_grav);
        sdk::read_cvar_float(anchors.cvar, "sv_staminarecoveryrate", sv_stam);
        sdk::read_cvar_bool(anchors.cvar, "sv_quantize_movement_input", sv_quant);
        sdk::read_cvar_bool(anchors.cvar, "sv_enablebunnyhopping", sv_ebhop);

        float grav_scale = 1.0f;
        if (nv.ent_gravity_scale != 0) safe_read(frame.local_pawn + nv.ent_gravity_scale, grav_scale);
        sin.gravity = sv_grav * (std::isfinite(grav_scale) && grav_scale > 0.0f ? grav_scale : 1.0f);
        sin.air_accel = sv_airacc;
        sin.air_max_wishspeed = sv_wish;
        sin.stamina_recovery = sv_stam;
        sin.quantize_movement = sv_quant;
        sin.enable_bunnyhopping = sv_ebhop;

        float max_spd = std::min(sv_maxspd, 260.0f);
        if (move_svc != 0) {
            float svc_max = 0.0f;
            if (nv.move_maxspeed != 0 && safe_read(move_svc + nv.move_maxspeed, svc_max) && svc_max > 0.0f) {
                max_spd = std::min(max_spd, svc_max);
            }
            if (nv.move_surface_friction != 0) {
                float fric = 1.0f;
                if (safe_read(move_svc + nv.move_surface_friction, fric) && std::isfinite(fric)) {
                    sin.surface_friction = fric;
                }
            }
            if (nv.move_stamina != 0) {
                float stam = 0.0f;
                if (safe_read(move_svc + nv.move_stamina, stam) && std::isfinite(stam)) {
                    sin.stamina = stam;
                }
            }
            std::uint64_t prev_btn = 0;
            if (nv.move_buttons != 0) safe_read(move_svc + nv.move_buttons, prev_btn);
            if ((prev_btn & (sdk::k_in_forward | sdk::k_in_back)) != 0 && nv.move_cmd_forward != 0) {
                safe_read(move_svc + nv.move_cmd_forward, sin.prev_cmd_fwd);
            }
            if ((prev_btn & (sdk::k_in_moveleft | sdk::k_in_moveright)) != 0 && nv.move_cmd_left != 0) {
                safe_read(move_svc + nv.move_cmd_left, sin.prev_cmd_left);
            }
        }
        if ((local_flags & 2u /*FL_DUCKING*/) != 0 || (cmd->button_state.held & sdk::k_in_duck) != 0) {
            max_spd *= 0.34f;
        }
        sin.max_speed = max_spd;

        const subtick_strafe_plan plan = build_subtick_autostrafe_plan(state, sin);
        if (plan.active) {
            for (int si = 0; si < plan.step_count; ++si) {
                const subtick_move_entry& e = plan.steps[static_cast<std::size_t>(si)];
                auto* st = sdk::add_subtick_move_step(anchors, cmd, e.when);
                if (!st) break;
                st->set_analog_forward_delta(e.analog_forward_delta);
                st->set_analog_left_delta(e.analog_left_delta);
                if (std::fabs(e.yaw_delta) > 1e-4f) {
                    st->set_yaw_delta(e.yaw_delta);
                }
            }
            const float strafe_pitch =
                aa_gate ? base_cmd->viewangles->x : math::clamp_pitch(camera_view.pitch);
            base_cmd->viewangles->set(strafe_pitch, plan.lock_view_yaw, 0.0f);
            base_cmd->set_forwardmove(plan.final_forwardmove);
            base_cmd->set_leftmove(plan.final_leftmove);
            sync_move_held_buttons(cmd, plan.final_forwardmove, plan.final_leftmove);
            strafe_out.active = true;
            strafe_out.forwardmove = plan.final_forwardmove;
            strafe_out.leftmove = plan.final_leftmove;
            strafe_out.steer_yaw = plan.lock_view_yaw;
            strafe_applied = true;
        } else {
            strafe_out = compute_runtime_air_strafe(
                state, cmd->command_number, camera_view.yaw, ref_yaw,
                local_vel, strafe_f_wish, strafe_l_wish);
            if (strafe_out.active) {
                state.strafe_pending_sign = strafe_out.next_strafe_sign;
                const float strafe_pitch =
                    aa_gate ? base_cmd->viewangles->x : math::clamp_pitch(camera_view.pitch);
                base_cmd->viewangles->set(strafe_pitch, strafe_out.steer_yaw, 0.0f);
                base_cmd->set_forwardmove(strafe_out.forwardmove);
                base_cmd->set_leftmove(strafe_out.leftmove);
                sync_move_held_buttons(cmd, strafe_out.forwardmove, strafe_out.leftmove);
                strafe_applied = true;
            }
        }
    } else {
        reset_strafe_state(state);
    }

    // Ground/Air movement fix preserving arbitrary aa_angles.yaw via quantized error diffusion.
    if (aa_gate && !strafe_applied && valid_move_type) {
        if (on_ground) {
            float q_fwd = 0.0f;
            float q_left = 0.0f;
            compute_quantized_ground_move(wish_world_dir, base_cmd->viewangles->y,
                                          state.ground_move, cmd->command_number, q_fwd, q_left);
            base_cmd->set_forwardmove(q_fwd);
            base_cmd->set_leftmove(q_left);
            sync_move_held_buttons(cmd, q_fwd, q_left);
        } else {
            state.ground_move = {};
            float a_fwd = 0.0f;
            float a_left = 0.0f;
            rotate_move_inputs(wish_f, wish_l, camera_view.yaw, base_cmd->viewangles->y,
                               a_fwd, a_left);
            base_cmd->set_forwardmove(a_fwd);
            base_cmd->set_leftmove(a_left);
            sync_move_held_buttons(cmd, a_fwd, a_left);
        }
    } else {
        state.ground_move = {};
    }

    // ---- Auto pistol (semi-auto only, tick-rate latched) -------------------
    const bool pistol_family =
        wep_group == static_cast<int>(config::weapon_group::pistol) ||
        wep_group == static_cast<int>(config::weapon_group::heavy_pistol);
    const bool pistol_eligible =
        !safe_mode && !weapon_switching && cfg.misc.utilities.auto_pistol && has_wep &&
        pistol_family && !active_wep.is_full_auto && !active_wep.is_revolver &&
        active_wep.clip > 0 && !active_wep.in_reload;
    if (step_auto_pistol(state, cmd->command_number, pistol_eligible,
                         (cmd->button_state.held & sdk::k_in_attack) != 0,
                         active_wep.cycle_time, active_wep.next_attack <= frame.curtime)) {
        cmd->button_state.set_button_state(sdk::k_in_attack, sdk::in_button_state::up);
        cmd->pb.clear_attack1_start_history_index();
        strip_subtick_buttons(base_cmd, sdk::k_in_attack);
    }

    // ---- Revolver reset & auto-cock ----------------------------------------
    if (state.revolver_command == cmd->command_number - 1) {
        auto* btn1 = input_view->button_attack();
        auto* btn2 = input_view->button_attack2();
        if (!btn1 || !btn1->pressed) {
            cmd->button_state.set_button_state(sdk::k_in_attack, sdk::in_button_state::up);
            cmd->pb.clear_attack1_start_history_index();
            strip_subtick_buttons(base_cmd, sdk::k_in_attack);
        }
        if (!btn2 || !btn2->pressed) {
            cmd->button_state.set_button_state(sdk::k_in_attack2, sdk::in_button_state::up);
            cmd->pb.clear_attack2_start_history_index();
            strip_subtick_buttons(base_cmd, sdk::k_in_attack2);
        }
    }

    // ---- Ragebot ------------------------------------------------------------
    bool fired_this_tick = false;
    int requested_target = -1;
    if (state.last_attack_weapon != active_wep.entity ||
        (frame.tick_valid && frame.tick < state.last_attack_tick)) {
        state.last_attack_tick = -1;
        state.requested_primary_tick = -1;
    }

    const int tick_base = frame.tick_valid ? frame.tick : cmd->command_number;
    const int history_index = cmd->pb.input_history.current_size - 1;

    // Auto-revolver cocking when enabled.
    if (!weapon_switching && (active_binds & config::feature_bit(config::bound_feature::rage)) &&
        cfg.rage.auto_revolver && has_wep && active_wep.is_revolver && active_wep.clip > 0 &&
        !active_wep.in_reload && (cmd->button_state.held & (sdk::k_in_attack | sdk::k_in_attack2)) == 0 &&
        history_index >= 0 && active_wep.next_attack <= frame.curtime) {
        std::uint64_t rev_btn = sdk::k_in_attack;
        if (active_wep.postpone_ready_ticks > tick_base) {
            if (tick_base >= state.next_revolver_cock) {
                cmd->button_state.set_button_state(rev_btn, sdk::in_button_state::down);
                cmd->pb.set_attack1_start_history_index(history_index);
                strip_subtick_buttons(base_cmd, sdk::k_in_attack);
                state.revolver_command = cmd->command_number;
            }
        } else if (active_wep.next_secondary_tick - 3 > tick_base) {
            rev_btn = sdk::k_in_attack2;
            cmd->button_state.set_button_state(rev_btn, sdk::in_button_state::down);
            cmd->pb.set_attack2_start_history_index(history_index);
            strip_subtick_buttons(base_cmd, sdk::k_in_attack2);
            state.revolver_command = cmd->command_number;
        } else {
            state.next_revolver_cock = tick_base + 1;
        }
    }

    std::uint8_t wait_no_attack = 0;
    if (nv.cs_pawn_wait_for_no_attack != 0) {
        safe_read(frame.local_pawn + nv.cs_pawn_wait_for_no_attack, wait_no_attack);
    }

    const bool revolver_ready = !active_wep.is_revolver ||
                                (active_wep.hauled_back && active_wep.postpone_ready_ticks <= tick_base);
    const bool is_firearm_or_taser =
        (active_wep.weapon_type >= k_weapontype_pistol &&
         active_wep.weapon_type <= k_weapontype_machinegun) ||
        active_wep.weapon_type == k_weapontype_taser || active_wep.def_index == 31;
    const bool weapon_ready = !weapon_switching && has_wep && active_wep.clip > 0 &&
                              !active_wep.in_reload && wait_no_attack == 0 && active_wep.damage > 0 &&
                              is_firearm_or_taser &&
                              (!frame.tick_valid || active_wep.next_primary_tick < 0 ||
                               active_wep.next_primary_tick <= tick_base) &&
                              active_wep.next_attack <= frame.curtime &&
                              revolver_ready;

    const bool fire_allowed = w_cfg.auto_fire || (cmd->button_state.held & sdk::k_in_attack) != 0;
    const int prior_brake_commands = state.brake_commands;
    state.brake_commands = 0;

    std::uint8_t scoped = 0;
    const bool has_scoped =
        nv.cs_pawn_scoped != 0 && safe_read(frame.local_pawn + nv.cs_pawn_scoped, scoped);
    std::uint8_t resume_zoom = 0;
    if (nv.cs_pawn_resume_zoom != 0) {
        safe_read(frame.local_pawn + nv.cs_pawn_resume_zoom, resume_zoom);
    }
    const bool wants_scope =
        w_cfg.auto_scope && active_wep.zoom_levels > 0 && has_scoped && scoped == 0 &&
        resume_zoom == 0;
    const int cycle_ticks = std::max(
        1, static_cast<int>((active_wep.cycle_time > 0.01f ? active_wep.cycle_time : 0.25f) * 64.0f + 0.5f));
    const int ticks_since_shot =
        (active_wep.entity != 0 && state.last_attack_weapon == active_wep.entity &&
         state.last_attack_tick >= 0 && tick_base >= state.last_attack_tick)
            ? (tick_base - state.last_attack_tick)
            : -1;
    const int ticks_until_ready =
        (frame.tick_valid && active_wep.next_primary_tick > tick_base)
            ? (active_wep.next_primary_tick - tick_base)
            : 0;
    const bool recent_shot_active =
        ticks_since_shot >= 0 && ticks_since_shot <= cycle_ticks + 6;
    const bool between_shots_active =
        !weapon_ready && has_wep && active_wep.clip > 0 && !active_wep.in_reload &&
        is_between_shots_window(cycle_ticks, ticks_since_shot, ticks_until_ready,
                                w_cfg.auto_stop_ticks);
    const int early_ticks = std::clamp(w_cfg.auto_stop_ticks, 1, 12);
    const float early_sec =
        static_cast<float>(early_ticks) * k_lagcomp_tick_ms / 1000.0f + 0.015f;
    const bool within_early_window =
        (!frame.tick_valid || active_wep.next_primary_tick < 0 ||
         active_wep.next_primary_tick <= tick_base + early_ticks) &&
        active_wep.next_attack <= frame.curtime + early_sec;

    const bool stop_env_ok =
        w_cfg.auto_stop && has_velocity &&
        (on_ground || (w_cfg.auto_stop_flags & config::auto_stop_flag::in_air) != 0);
    const bool wants_early_stop =
        stop_env_ok && (w_cfg.auto_stop_flags & config::auto_stop_flag::early) != 0 &&
        within_early_window;
    const bool wants_between_stop =
        stop_env_ok && (w_cfg.auto_stop_flags & config::auto_stop_flag::between_shots) != 0 &&
        between_shots_active;
    const bool wants_stop = stop_env_ok && speed_2d > 5.0f;

    const bool scan_for_prep =
        wants_early_stop || wants_between_stop ||
        (wants_scope && within_early_window) ||
        (w_cfg.delay_shot != config::delay_shot_mode::off &&
         (!frame.tick_valid || active_wep.next_primary_tick <= tick_base + 4));
    const bool can_act_soon = weapon_ready || scan_for_prep;
    if ((active_binds & config::feature_bit(config::bound_feature::rage)) && fire_allowed &&
        can_act_soon && frame.local_eye_valid && has_wep && active_wep.clip > 0 &&
        active_wep.damage > 0 && history_index >= 0) {
        target_candidate cands[sdk::k_max_players]{};
        math::angles cand_aim[sdk::k_max_players]{};
        bool cand_direct[sdk::k_max_players]{};
        float cand_hitchance[sdk::k_max_players]{};
        float cand_damage[sdk::k_max_players]{};
        float cand_min_dmg[sdk::k_max_players]{};
        std::uintptr_t cand_pawn[sdk::k_max_players]{};
        int cand_sim_tick[sdk::k_max_players]{};
        int cand_count = 0;

        const float req_hc = w_cfg.hitchance;
        int next_cursor = state.cycle_cursor;
        int picked = -1;

        const bool cache_hit = can_reuse_rage_scan(
            state.rage_cache, cmd->command_number, tick_base, active_wep.entity,
            weapon_ready, scoped, fire_allowed, on_ground, frame.count, active_wep.clip,
            active_wep.next_primary_tick, frame.local_eye, speed_2d, camera_view);

        if (cache_hit) {
            next_cursor = state.rage_cache.next_cursor;
            if (state.rage_cache.has_picked) {
                cands[0] = state.rage_cache.cand;
                cand_aim[0] = state.rage_cache.aim;
                cand_direct[0] = state.rage_cache.direct;
                cand_hitchance[0] = state.rage_cache.hitchance;
                cand_damage[0] = state.rage_cache.damage;
                cand_min_dmg[0] = state.rage_cache.min_dmg;
                cand_pawn[0] = state.rage_cache.pawn;
                cand_sim_tick[0] = state.rage_cache.sim_tick;
                cand_count = 1;
                picked = 0;
            }
        } else {
            bool has_qualified_direct = false;

            float spread = 0.0f;
            float inaccuracy = 0.0f;
            const bool live_accuracy = sdk::read_weapon_accuracy_predicted(
                anchors, nv, active_wep.entity, spread, inaccuracy);
            const math::weapon_spread_pattern spread_pattern(spread, inaccuracy);
            const float spread_deg = live_accuracy ? std::atan(spread + inaccuracy) * 57.29578f :
                (std::isfinite(active_wep.spread) && active_wep.spread > 0.0f
                     ? std::atan(active_wep.spread) * 57.29578f : 0.15f) +
                (on_ground ? 0.0f : 3.5f);

            const float base_damage = static_cast<float>(active_wep.damage);
            const float num_bullets = static_cast<float>(std::max(1, active_wep.num_bullets));

            constexpr auto k_supported_points = config::aim_point::head | config::aim_point::neck |
                                                config::aim_point::chest | config::aim_point::stomach |
                                                config::aim_point::pelvis | config::aim_point::legs;
            std::uint32_t aim_mask = w_cfg.aim_points & k_supported_points;
            if (aim_mask == 0) aim_mask = config::aim_point::chest;

            std::vector<const lag_record*> records_buf;
            records_buf.reserve(4);

            for (int i = 0; i < frame.count && cand_count < sdk::k_max_players; ++i) {
                const sdk::player_snapshot& p = frame.players[i];
                if (!p.alive || !p.enemy || p.pawn == frame.local_pawn) continue;

                std::uint8_t immune = 0;
                if (nv.cs_pawn_immunity != 0 && safe_read(p.pawn + nv.cs_pawn_immunity, immune) &&
                    immune != 0) {
                    continue;
                }

                const float min_dmg = w_cfg.min_damage > 100.0f
                    ? static_cast<float>(p.health) + (w_cfg.min_damage - 100.0f)
                    : std::min(w_cfg.min_damage, static_cast<float>(p.health) + 1.0f);

                struct shot_point {
                    math::vec3 point{};
                    math::angles aim{};
                    math::vec3 hb_center{};
                    float hb_radius = 4.0f;
                    int hb_group = 0;
                    const lag_record* record = nullptr;
                    float distance = 0.0f;
                    float fov = 0.0f;
                    float hitchance = 0.0f;
                    float damage = 0.0f;
                    float single_pellet_damage = 0.0f;
                    int sim_tick = 0;
                    bool direct = false;
                    bool is_head = false;
                    bool is_center = true;
                    bool hc_evaluated = false;
                };

                std::optional<shot_point> best_body;
                std::optional<shot_point> best_head;

                const auto to_cand = [](const shot_point& s) noexcept -> rage_shot_candidate {
                    return {s.damage, s.hitchance, s.fov, s.direct, s.is_center, s.hc_evaluated};
                };

                const auto better_damage_point = [&](const shot_point& a,
                                                     const std::optional<shot_point>& prev) {
                    const std::optional<rage_shot_candidate> prev_cand =
                        prev ? std::optional<rage_shot_candidate>(to_cand(*prev)) : std::nullopt;
                    return better_rage_shot_point(
                        to_cand(a), prev_cand, p.health, w_cfg.safe_point, w_cfg.strict_hitchance);
                };

                // Collect up to 3 distinct lagcomp records [newest, shooting, oldest] + lag-gap extrapolation.
                records_buf.clear();
                const float bt_window_ms = cfg.rage.backtrack ? std::clamp(cfg.rage.backtrack_ms, 0.0f, 200.0f) : 0.0f;
                if (bt_window_ms >= 15.0f) {
                    state.lagcomp.select(p.pawn, p.pawn_handle, tick_base, bt_window_ms, records_buf);
                }
                sdk::live_hitbox_set live_fallback{};
                lag_record live_rec{};
                if (records_buf.empty()) {
                    if (sdk::read_pawn_hitboxes(anchors, nv, p.pawn, live_fallback) && live_fallback.valid) {
                        live_rec.pawn = p.pawn;
                        live_rec.pawn_handle = p.pawn_handle;
                        live_rec.origin = p.origin;
                        live_rec.simulation_tick = live_fallback.simulation_tick > 0
                                                       ? live_fallback.simulation_tick
                                                       : tick_base;
                        live_rec.hitboxes = std::move(live_fallback);
                        live_rec.valid = true;
                        records_buf.push_back(&live_rec);
                    }
                }

                lag_record extrap_rec{};
                if (!records_buf.empty() && p.speed_2d > 20.0f && p.velocity.finite()) {
                    const float extrap_gap_ms = compute_extrapolation_gap_ms(
                        tick_base, records_buf.front()->simulation_tick, cfg.rage.extrapolation_ms);
                    if (extrap_gap_ms > 0.5f &&
                        build_extrapolated_record(*records_buf.front(), p.origin, p.velocity,
                                                  extrap_gap_ms, tick_base, extrap_rec)) {
                        records_buf.push_back(&extrap_rec);
                    }
                }

                if (!records_buf.empty()) {
                    const float mp_scale =
                        math::effective_multipoint_scale(w_cfg.multipoint, w_cfg.safe_point);
                    const float hc_scale =
                        math::safe_point_hitbox_scale(w_cfg.safe_point, w_cfg.strict_hitchance);
                    const bool use_bullet_trace = sdk::bullet_trace_ready();

                    auto eval_hc = [&](std::optional<shot_point>& sp) {
                        if (!sp || !sp->record || sp->hc_evaluated) return;
                        const std::span<const math::hitbox_geometry> geom_span(
                            sp->record->hitboxes.geometries);
                        sp->hitchance = 100.0f * (live_accuracy
                            ? spread_pattern.chance_multi(frame.local_eye, sp->point, geom_span,
                                                          sp->hb_group, w_cfg.strict_hitchance,
                                                          hc_scale)
                            : math::hitchance(frame.local_eye, sp->point, sp->hb_center,
                                              sp->hb_radius * hc_scale, spread_deg, 128));
                        if (active_wep.num_bullets > 1) {
                            sp->damage = scale_shotgun_damage(
                                sp->single_pellet_damage, active_wep.num_bullets, sp->hitchance);
                        }
                        sp->hc_evaluated = true;
                    };

                    const auto body_early_ok = [&]() -> bool {
                        if (active_wep.num_bullets > 1 || !best_body || !best_body->is_center ||
                            !best_body->direct ||
                            best_body->damage < static_cast<float>(p.health)) {
                            return false;
                        }
                        eval_hc(best_body);
                        return best_body->hitchance >= req_hc &&
                               best_body->damage >= static_cast<float>(p.health);
                    };

                    const auto head_early_ok = [&]() -> bool {
                        if (active_wep.num_bullets > 1 || w_cfg.force_body_aim ||
                            w_cfg.prefer_body_aim || !best_head || !best_head->is_center ||
                            !best_head->direct ||
                            best_head->damage < static_cast<float>(p.health)) {
                            return false;
                        }
                        eval_hc(best_head);
                        return best_head->hitchance >= req_hc &&
                               best_head->damage >= static_cast<float>(p.health);
                    };

                    for (const lag_record* rec : records_buf) {
                        sdk::hitbox_transform_scope scope(nv, p.pawn, rec->hitboxes.transforms);

                        std::array<std::size_t, 64> hb_order{};
                        std::size_t hb_count = 0;
                        const std::size_t total_hbs = rec->hitboxes.geometries.size();
                        for (std::size_t hi = 0; hi < total_hbs && hb_count < hb_order.size(); ++hi) {
                            const auto& hb = rec->hitboxes.geometries[hi];
                            if (!hb.valid || !rage_group_enabled(aim_mask, hb.group,
                                                                 w_cfg.ignore_limbs,
                                                                 w_cfg.force_body_aim)) {
                                continue;
                            }
                            hb_order[hb_count++] = hi;
                        }
                        std::sort(hb_order.begin(),
                                  hb_order.begin() + static_cast<std::ptrdiff_t>(hb_count),
                                  [&](std::size_t a, std::size_t b) {
                                      return rage_group_rank(rec->hitboxes.geometries[a].group,
                                                             w_cfg.force_body_aim,
                                                             w_cfg.prefer_body_aim) <
                                             rage_group_rank(rec->hitboxes.geometries[b].group,
                                                             w_cfg.force_body_aim,
                                                             w_cfg.prefer_body_aim);
                                  });

                        for (std::size_t oi = 0; oi < hb_count; ++oi) {
                            if (body_early_ok()) break;
                            if (head_early_ok()) break;

                            const auto& hb = rec->hitboxes.geometries[hb_order[oi]];
                            const float center_dist = (hb.center - frame.local_eye).length();
                            if (!std::isfinite(center_dist) || center_dist < 1.0f ||
                                (active_wep.range > 10.0f && center_dist > active_wep.range)) {
                                continue;
                            }
                            const float hb_fov = angle_distance_deg(
                                camera_view, math::angles_from_forward(hb.center - frame.local_eye));
                            if (!std::isfinite(hb_fov) || hb_fov > w_cfg.fov + 8.0f) continue;

                            const bool upper_helmet =
                                resolve_effective_helmet(p.helmet_known, p.helmet, p.armor, true);
                            const float upper_dmg = estimate_hit_damage(
                                base_damage, center_dist, active_wep.range_modifier,
                                active_wep.headshot_multiplier, active_wep.armor_ratio, p.armor,
                                upper_helmet, static_cast<math::hitgroup>(hb.hitgroup)) * num_bullets;
                            if (!std::isfinite(upper_dmg) || upper_dmg < min_dmg) continue;

                            auto& existing_slot = (hb.group == 0) ? best_head : best_body;
                            const float skip_tol =
                                (w_cfg.strict_hitchance || w_cfg.safe_point >= 50.0f) ? 10.0f : 4.0f;
                            if (active_wep.num_bullets <= 1 && existing_slot &&
                                existing_slot->direct && existing_slot->is_center &&
                                std::isfinite(existing_slot->damage) &&
                                existing_slot->damage >= upper_dmg + skip_tol &&
                                !existing_slot->hc_evaluated) {
                                eval_hc(existing_slot);
                            }
                            if (should_skip_lower_hitbox(
                                    existing_slot ? std::optional<rage_shot_candidate>(to_cand(*existing_slot))
                                                  : std::nullopt,
                                    existing_slot ? existing_slot->hc_evaluated : false,
                                    upper_dmg, req_hc, w_cfg.safe_point,
                                    w_cfg.strict_hitchance, active_wep.num_bullets)) {
                                continue;
                            }

                            std::array<math::vec3, 9> pts{};
                            std::size_t pt_count = 1;
                            pts[0] = hb.center;
                            if (!hb.foot && hb.group <= 3 && mp_scale > 0.02f) {
                                const std::size_t cap = hb.group == 0 ? 7u : (hb.group <= 2 ? 5u : 3u);
                                pt_count = std::min<std::size_t>(
                                    cap, math::generate_hitbox_points(hb, frame.local_eye, mp_scale, pts));
                            }

                            for (std::size_t pi = 0; pi < pt_count; ++pi) {
                                const math::vec3& pt = pts[pi];
                                const math::vec3 delta = pt - frame.local_eye;
                                const float distance = delta.length();
                                if (!std::isfinite(distance) || distance < 1.0f ||
                                    (active_wep.range > 10.0f && distance > active_wep.range)) {
                                    continue;
                                }
                                const math::angles aim = math::angles_from_forward(delta);
                                const float fov = angle_distance_deg(camera_view, aim);
                                if (!std::isfinite(fov) || fov > w_cfg.fov) continue;

                                float single_dmg = 0.0f;
                                bool penetrated = false;
                                int hit_group = hb.group;
                                math::vec3 hit_center = hb.center;
                                float hit_radius = hb.radius;
                                const bool eff_helmet = resolve_effective_helmet(
                                    p.helmet_known, p.helmet, p.armor, false);
                                if (use_bullet_trace) {
                                    const float trace_range =
                                        active_wep.range > 10.0f ? active_wep.range : 8192.0f;
                                    const math::vec3 ray_delta = delta * (trace_range / distance);
                                    const float s[3] = {frame.local_eye.x, frame.local_eye.y, frame.local_eye.z};
                                    const float d[3] = {ray_delta.x, ray_delta.y, ray_delta.z};
                                    sdk::bullet_impact imp{};
                                    if (sdk::trace_bullet_ex(s, d, frame.local_pawn, p.pawn, frame.local_team,
                                                             base_damage, active_wep.penetration,
                                                             active_wep.range_modifier, w_cfg.penetration,
                                                             imp)) {
                                        const int imp_group = math::hitgroup_to_group(imp.hitgroup);
                                        if (imp_group >= 0) {
                                            if (!rage_group_enabled(aim_mask, imp_group,
                                                                    w_cfg.ignore_limbs,
                                                                    w_cfg.force_body_aim)) {
                                                continue;
                                            }
                                            hit_group = imp_group;
                                            if (imp_group != hb.group) {
                                                const math::hitbox_geometry* matched = nullptr;
                                                for (const auto& geom : rec->hitboxes.geometries) {
                                                    if (!geom.valid) continue;
                                                    if (geom.hitgroup == imp.hitgroup) {
                                                        matched = &geom;
                                                        break;
                                                    }
                                                    if (!matched && geom.group == imp_group) {
                                                        matched = &geom;
                                                    }
                                                }
                                                if (matched) {
                                                    hit_center = matched->center;
                                                    hit_radius = matched->radius;
                                                }
                                            }
                                        }
                                        single_dmg = estimate_hit_damage(
                                            imp.damage, 0.0f, 1.0f, active_wep.headshot_multiplier,
                                            active_wep.armor_ratio, p.armor, eff_helmet,
                                            static_cast<math::hitgroup>(imp.hitgroup));
                                        penetrated = imp.penetrated;
                                    }
                                } else {
                                    bool clear = false;
                                    if (trace_clear_segment(frame.local_eye, pt, p.pawn, clear) && clear) {
                                        single_dmg = estimate_hit_damage(
                                            base_damage, distance, active_wep.range_modifier,
                                            active_wep.headshot_multiplier, active_wep.armor_ratio,
                                            p.armor, eff_helmet,
                                            static_cast<math::hitgroup>(hb.hitgroup));
                                    }
                                }

                                const float est_total_dmg = single_dmg * num_bullets;
                                if (!std::isfinite(est_total_dmg) || est_total_dmg < min_dmg ||
                                    est_total_dmg <= 0.0f) {
                                    continue;
                                }

                                const shot_point sp{pt, aim, hit_center, hit_radius, hit_group, rec,
                                                    distance, fov, 0.0f, est_total_dmg, single_dmg,
                                                    rec->simulation_tick, !penetrated,
                                                    hit_group == 0, pi == 0, false};
                                auto& slot = sp.is_head ? best_head : best_body;
                                const bool replace = update_rage_shot_slot(
                                    slot, sp, p.health, req_hc, w_cfg.safe_point,
                                    w_cfg.strict_hitchance, active_wep.num_bullets, eval_hc);
                                if (replace && pi == 0 && !penetrated &&
                                    active_wep.num_bullets <= 1 &&
                                    est_total_dmg >= static_cast<float>(p.health)) {
                                    break;  // center point is already direct & lethal
                                }
                            }
                        }

                        if (body_early_ok()) break;
                        if (head_early_ok()) break;
                    }

                    // Evaluate spread hitchance on chosen candidate point(s) if not already evaluated.
                    eval_hc(best_body);
                    eval_hc(best_head);
                } else {
                    // Bone-only fallback if CModel hitboxes are unavailable.
                    static constexpr int k_bones[] = {sdk::bone::head, sdk::bone::neck, sdk::bone::spine_3,
                                                      sdk::bone::spine_1, sdk::bone::pelvis, sdk::bone::leg_lower_l};
                    static constexpr math::hitgroup k_hgs[] = {
                        math::hitgroup::head, math::hitgroup::neck, math::hitgroup::chest,
                        math::hitgroup::stomach, math::hitgroup::generic, math::hitgroup::left_leg};
                    static constexpr int k_groups[] = {0, 1, 1, 2, 3, 4};

                    const float hc_scale =
                        math::safe_point_hitbox_scale(w_cfg.safe_point, w_cfg.strict_hitchance);
                    const bool eff_helmet =
                        resolve_effective_helmet(p.helmet_known, p.helmet, p.armor, false);

                    for (int bi = 0; bi < 6; ++bi) {
                        if (!rage_group_enabled(aim_mask, k_groups[bi], w_cfg.ignore_limbs,
                                                w_cfg.force_body_aim)) {
                            continue;
                        }
                        const int bone = k_bones[bi];
                        if (bone >= p.bone_count) continue;
                        const math::vec3 pt = p.bones[bone];
                        const math::vec3 delta = pt - frame.local_eye;
                        const float distance = delta.length();
                        if (!std::isfinite(distance) || distance < 1.0f ||
                            (active_wep.range > 10.0f && distance > active_wep.range)) continue;
                        const math::angles aim = math::angles_from_forward(delta);
                        const float fov = angle_distance_deg(camera_view, aim);
                        if (!std::isfinite(fov) || fov > w_cfg.fov) continue;

                        bool clear = false;
                        if (!trace_clear_segment(frame.local_eye, pt, p.pawn, clear) || !clear) continue;
                        const float radius = math::default_hitbox_radius(k_hgs[bi]);
                        const float hc = 100.0f * (live_accuracy
                            ? spread_pattern.chance(frame.local_eye, pt, pt, radius * hc_scale)
                            : math::hitchance(frame.local_eye, pt, pt, radius * hc_scale,
                                              spread_deg, 128));
                        const float single_dmg = estimate_hit_damage(
                            base_damage, distance, active_wep.range_modifier,
                            active_wep.headshot_multiplier, active_wep.armor_ratio,
                            p.armor, eff_helmet, k_hgs[bi]);
                        const float dmg = (active_wep.num_bullets > 1)
                                              ? scale_shotgun_damage(single_dmg, active_wep.num_bullets, hc)
                                              : single_dmg;
                        if (!std::isfinite(dmg) || dmg < min_dmg || dmg <= 0.0f) continue;

                        const shot_point sp{pt, aim, pt, radius, k_groups[bi], nullptr,
                                            distance, fov, hc, dmg, single_dmg, tick_base,
                                            true, k_groups[bi] == 0, true, true};
                        auto& slot = sp.is_head ? best_head : best_body;
                        if (better_damage_point(sp, slot)) slot = sp;
                    }
                }

                const std::optional<rage_shot_candidate> body_cand =
                    best_body ? std::optional<rage_shot_candidate>(to_cand(*best_body)) : std::nullopt;
                const std::optional<rage_shot_candidate> head_cand =
                    best_head ? std::optional<rage_shot_candidate>(to_cand(*best_head)) : std::nullopt;
                const int pick_side = choose_rage_shot_point(
                    body_cand, head_cand, p.health, min_dmg, req_hc,
                    w_cfg.force_body_aim, w_cfg.prefer_body_aim);
                if (pick_side < 0) continue;
                const shot_point& shot = (pick_side == 1) ? *best_body : *best_head;

                const bool shot_ok = std::isfinite(shot.damage) && std::isfinite(shot.hitchance) &&
                                     shot.damage >= min_dmg && shot.hitchance >= req_hc;
                const float prox = std::clamp(1.0f - (shot.distance / 4000.0f), 0.0f, 1.0f);
                const float lethal = std::clamp(shot.damage / std::max(1.0f, static_cast<float>(p.health)),
                                                0.0f, 1.0f);
                const float threat = std::clamp(0.25f * prox + 0.55f * lethal + (shot.direct ? 0.20f : 0.0f),
                                                0.05f, 1.0f);
                cands[cand_count] = target_candidate{
                    p.index,
                    shot.fov,
                    shot.distance,
                    p.health,
                    threat,
                    true,
                    shot_ok,
                };
                cand_direct[cand_count] = shot.direct;
                has_qualified_direct |= shot.direct && shot_ok;
                cand_aim[cand_count] = shot.aim;
                cand_hitchance[cand_count] = shot.hitchance;
                cand_damage[cand_count] = shot.damage;
                cand_min_dmg[cand_count] = min_dmg;
                cand_pawn[cand_count] = p.pawn;
                cand_sim_tick[cand_count] = shot.sim_tick;
                ++cand_count;
            }

            if (has_qualified_direct) {
                for (int i = 0; i < cand_count; ++i) {
                    if (!cand_direct[i]) cands[i].alive = false;
                }
            }

            const auto candidates = std::span(cands, static_cast<std::size_t>(cand_count));
            picked = pick_target(candidates, cfg.rage.priority, w_cfg.fov, true, next_cursor);
            if (picked < 0 && (wants_stop || wants_scope || w_cfg.delay_shot != config::delay_shot_mode::off)) {
                for (auto& c : candidates) c.alive = true;
                next_cursor = state.cycle_cursor;
                picked = pick_target(candidates, cfg.rage.priority, w_cfg.fov, true, next_cursor);
            }

            state.rage_cache = {};
            state.rage_cache.valid = true;
            state.rage_cache.command_number = cmd->command_number;
            state.rage_cache.tick_base = tick_base;
            state.rage_cache.weapon_entity = active_wep.entity;
            state.rage_cache.weapon_ready = weapon_ready;
            state.rage_cache.scoped = scoped;
            state.rage_cache.fire_allowed = fire_allowed;
            state.rage_cache.on_ground = on_ground;
            state.rage_cache.player_count = frame.count;
            state.rage_cache.clip = active_wep.clip;
            state.rage_cache.next_primary_tick = active_wep.next_primary_tick;
            state.rage_cache.local_eye = frame.local_eye;
            state.rage_cache.speed_2d = speed_2d;
            state.rage_cache.camera_view = camera_view;
            state.rage_cache.next_cursor = next_cursor;
            if (picked >= 0) {
                state.rage_cache.has_picked = true;
                state.rage_cache.cand = cands[picked];
                state.rage_cache.aim = cand_aim[picked];
                state.rage_cache.direct = cand_direct[picked];
                state.rage_cache.hitchance = cand_hitchance[picked];
                state.rage_cache.damage = cand_damage[picked];
                state.rage_cache.min_dmg = cand_min_dmg[picked];
                state.rage_cache.pawn = cand_pawn[picked];
                state.rage_cache.sim_tick = cand_sim_tick[picked];
            }
        }

        if (picked >= 0) {
            const float hc = cand_hitchance[picked];
            const float est_dmg = cand_damage[picked];
            const float picked_min_dmg = cand_min_dmg[picked];

            config::rage_weapon delay_cfg = w_cfg;
            delay_cfg.min_damage = picked_min_dmg;
            delay_cfg.hitchance = req_hc;
            const bool delay_ready = state.delay.ready(cand_pawn[picked], active_wep.entity,
                                                       tick_base, delay_cfg, hc, est_dmg);
            const bool shot_qualified = std::isfinite(est_dmg) && std::isfinite(hc) &&
                                        std::isfinite(req_hc) && std::isfinite(picked_min_dmg) &&
                                        est_dmg > 0.0f && hc >= req_hc && est_dmg >= picked_min_dmg;

            // Preserve BunnyHop momentum when actively hopping and airborne hitchance has not qualified a shot.
            const bool bhop_suppress_prestop =
                bhop_active && jump_held && (!on_ground || bhop_do_jump) && !shot_qualified;
            const autostop_decision stop_dec = bhop_suppress_prestop
                ? autostop_decision{}
                : compute_autostop(autostop_input{
                      w_cfg.auto_stop,
                      w_cfg.auto_stop_flags,
                      on_ground,
                      has_velocity,
                      local_vel,
                      speed_2d,
                      base_cmd->viewangles->y,
                      est_dmg >= picked_min_dmg,
                      weapon_ready,
                      shot_qualified,
                      within_early_window,
                      between_shots_active,
                      recent_shot_active,
                      w_cfg.auto_stop_speed,
                      w_cfg.auto_stop_ticks,
                      w_cfg.auto_stop_wait,
                  });
            const bool should_stop = stop_dec.active;
            if (should_stop) {
                state.brake_commands = prior_brake_commands + 1;
                base_cmd->set_forwardmove(stop_dec.forwardmove);
                base_cmd->set_leftmove(stop_dec.leftmove);
                if (!stop_dec.zero_move) {
                    sync_move_held_buttons(cmd, stop_dec.forwardmove, stop_dec.leftmove);
                } else {
                    sync_move_held_buttons(cmd, 0.0f, 0.0f);
                }
                strip_subtick_buttons(base_cmd, k_move_button_mask);
            }

            if (w_cfg.auto_crouch && on_ground && est_dmg >= picked_min_dmg) {
                cmd->button_state.set_button(sdk::k_in_duck, true);
                strip_subtick_buttons(base_cmd, sdk::k_in_duck);
            }

            const rage_fire_gate_output fire_gate = evaluate_rage_fire_gate(rage_fire_gate_input{
                w_cfg.auto_scope,
                active_wep.zoom_levels,
                has_scoped && scoped != 0,
                resume_zoom != 0,
                active_wep.next_secondary_tick,
                tick_base,
                w_cfg.auto_stop,
                w_cfg.auto_stop_wait,
                stop_env_ok,
                has_velocity,
                speed_2d,
                w_cfg.auto_stop_speed,
                on_ground,
                est_dmg,
                picked_min_dmg,
                hc,
                req_hc,
                delay_ready,
                weapon_ready,
            });
            const bool scope_pending = fire_gate.scope_pending;
            if (scope_pending) {
                cmd->button_state.set_button_state(sdk::k_in_attack, sdk::in_button_state::up);
                cmd->button_state.set_button(sdk::k_in_attack2, true);
                cmd->pb.clear_attack1_start_history_index();
                cmd->pb.set_attack2_start_history_index(history_index);
                strip_subtick_buttons(base_cmd, sdk::k_in_attack | sdk::k_in_attack2);
            }

            const bool will_fire = fire_gate.will_fire;
            if (will_fire) {
                math::angles recoil{};
                if (cfg.rage.remove_recoil) {
                    sdk::compute_aim_punch(anchors, nv, frame.local_pawn, tick_base + 1, 0.0f, recoil);
                }

                math::angles fire_angle = cand_aim[picked];
                fire_angle.pitch = math::clamp_pitch(fire_angle.pitch - recoil.pitch);
                fire_angle.yaw = math::normalize_yaw(fire_angle.yaw - recoil.yaw);

                const auto& ih_field = cmd->pb.input_history;
                if (ih_field.ss && history_index >= 0 && history_index < ih_field.current_size &&
                    history_index < ih_field.capacity && history_index < ih_field.ss->max) {
                    auto* ih = ih_field.ss->elements[history_index];
                    if (ih && ih->view_angles) {
                        ih->view_angles->set(fire_angle.pitch, fire_angle.yaw, 0.0f);
                        ih->set_render_tick_count(cand_sim_tick[picked]);
                        ih->set_player_tick_count(tick_base + 1);
                        ih->set_render_tick_fraction(0.0f);
                        ih->set_player_tick_fraction(0.0f);
                    }
                }

                const float pre_fire_yaw = base_cmd->viewangles->y;
                if (sdk::set_fire_angles(anchors, cmd, fire_angle, recoil)) {
                    if (valid_move_type) {
                        if (should_stop && !stop_dec.zero_move) {
                            float s_fwd = 0.0f;
                            float s_left = 0.0f;
                            rotate_move_inputs(stop_dec.forwardmove, stop_dec.leftmove,
                                               pre_fire_yaw, base_cmd->viewangles->y,
                                               s_fwd, s_left);
                            base_cmd->set_forwardmove(s_fwd);
                            base_cmd->set_leftmove(s_left);
                            sync_move_held_buttons(cmd, s_fwd, s_left);
                            strip_subtick_buttons(base_cmd, k_move_button_mask);
                            strip_subtick_analog_moves(base_cmd);
                        } else if (!should_stop) {
                            float f_fwd = 0.0f;
                            float f_left = 0.0f;
                            resolve_fire_tick_movement(
                                strafe_applied, strafe_out, on_ground, wish_world_dir,
                                wish_f, wish_l, camera_view.yaw, base_cmd->viewangles->y,
                                state.ground_move, cmd->command_number, f_fwd, f_left);
                            base_cmd->set_forwardmove(f_fwd);
                            base_cmd->set_leftmove(f_left);
                            sync_move_held_buttons(cmd, f_fwd, f_left);
                            strip_subtick_buttons(base_cmd, k_move_button_mask);
                            strip_subtick_analog_moves(base_cmd);
                        }
                    }
                    if (active_wep.is_revolver || (cmd->button_state.held & sdk::k_in_attack2) != 0) {
                        cmd->button_state.set_button_state(sdk::k_in_attack2, sdk::in_button_state::up);
                        cmd->pb.clear_attack2_start_history_index();
                        strip_subtick_buttons(base_cmd, sdk::k_in_attack2);
                    }
                    cmd->button_state.set_button(sdk::k_in_attack, true);
                    cmd->pb.set_attack1_start_history_index(history_index);
                    strip_subtick_buttons(base_cmd, sdk::k_in_attack);
                    if (!cfg.rage.silent_aim) {
                        input_view->set_viewangles(fire_angle);
                    }
                    fired_this_tick = true;
                    requested_target = cands[picked].index;
                    state.peek_assist.pending_return = true;
                    if (state.last_target_shot_cmd != cmd->command_number) {
                        state.cycle_cursor = next_cursor;
                    }
                    state.last_target_index = requested_target;
                    state.last_target_shot_cmd = cmd->command_number;
                    state.rage_cache = {};
                }
            } else {
                static ULONGLONG next_hold_log = 0;
                const auto now = GetTickCount64();
                if (now >= next_hold_log) {
                    next_hold_log = now + 1000;
                    log::info("rage: hold target={} dmg={:.1f} hc={:.1f}/{:.0f} spd={:.0f} ready={} "
                              "(na={:.2f}/{:.2f} nt={}/{} wait={}) stop={} scope={}",
                              cands[picked].index, est_dmg, hc, req_hc, speed_2d,
                              weapon_ready ? 1 : 0, active_wep.next_attack, frame.curtime,
                              active_wep.next_primary_tick, tick_base,
                              static_cast<int>(wait_no_attack), should_stop ? 1 : 0,
                              scope_pending ? 1 : 0);
                }
            }
        } else {
            state.delay = {};
        }
    } else {
        state.delay = {};
        state.rage_cache = {};
    }

    // Safe manual fire: align to crosshair strictly on the tick a manual gunshot fires
    // so bullets land cleanly while Anti-Aim stays rock-solid between shots (zero jitter),
    // and re-solve movement when AA or sector-tilted Auto-Strafe shifted base_cmd->viewangles.
    if (((aa_gate && cfg.anti_aim.on_shot) || (!aa_gate && strafe_applied)) &&
        !weapon_switching && !fired_this_tick && weapon_ready &&
        state.brake_commands == 0 && (cmd->button_state.held & sdk::k_in_attack) != 0 &&
        !(active_wep.is_revolver && (cmd->button_state.held & sdk::k_in_attack2) != 0)) {
        if (sdk::set_fire_angles(anchors, cmd, camera_view, {})) {
            if (valid_move_type) {
                float f_fwd = 0.0f;
                float f_left = 0.0f;
                resolve_fire_tick_movement(
                    strafe_applied, strafe_out, on_ground, wish_world_dir,
                    wish_f, wish_l, camera_view.yaw, base_cmd->viewangles->y,
                    state.ground_move, cmd->command_number, f_fwd, f_left);
                base_cmd->set_forwardmove(f_fwd);
                base_cmd->set_leftmove(f_left);
                sync_move_held_buttons(cmd, f_fwd, f_left);
                strip_subtick_buttons(base_cmd, k_move_button_mask);
                strip_subtick_analog_moves(base_cmd);
            }
        }
    }

    const bool peek_active = (active_binds & config::feature_bit(config::bound_feature::peek_assist)) != 0;
    run_peek_assist(cfg, frame, cmd, on_ground, move_type, peek_active, state.peek_assist);
    if (state.peek_assist.returning) strip_subtick_buttons(base_cmd, k_move_button_mask);

    if (!safe_mode && cfg.misc.movement.leg_movement && on_ground && !fired_this_tick &&
        !(bhop_active && jump_held) && state.brake_commands == 0 &&
        !state.peek_assist.returning && move_type == 2 &&
        (cmd->button_state.held &
         (sdk::k_in_jump | sdk::k_in_duck | sdk::k_in_attack | sdk::k_in_attack2)) == 0) {
        const std::uint64_t held = cmd->button_state.held;
        std::uint64_t flipped =
            held & ~(sdk::k_in_forward | sdk::k_in_back | sdk::k_in_moveleft | sdk::k_in_moveright);
        if (held & sdk::k_in_forward) flipped |= sdk::k_in_back;
        if (held & sdk::k_in_back) flipped |= sdk::k_in_forward;
        if (held & sdk::k_in_moveleft) flipped |= sdk::k_in_moveright;
        if (held & sdk::k_in_moveright) flipped |= sdk::k_in_moveleft;
        cmd->button_state.held = flipped;
        cmd->button_state.toggle &= ~k_move_button_mask;
        cmd->button_state.scroll &= ~k_move_button_mask;
        strip_subtick_buttons(base_cmd, k_move_button_mask);
    }

    sdk::sync_buttons_pb(cmd);

    if (fired_this_tick) {
        state.delay = {};
        state.last_attack_weapon = active_wep.entity;
        state.last_attack_tick = tick_base;
        state.requested_primary_tick = active_wep.next_primary_tick;
        static ULONGLONG next_report = 0;
        const auto now = GetTickCount64();
        if (now >= next_report) {
            next_report = now + 1000;
            log::info("rage: fired subtick cmd={} tick={} target={}", cmd->command_number,
                      tick_base, requested_target);
        }
    }
}

bool safe_from_global_tick(std::uintptr_t fn_addr, int& out_tick, int global_tick,
                           std::uint32_t world_group) noexcept {
    if (fn_addr == 0) return false;
    using fn_t = void (*)(int*, int, std::uint32_t);
    __try {
        reinterpret_cast<fn_t>(fn_addr)(&out_tick, global_tick, world_group);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace shogod::features
