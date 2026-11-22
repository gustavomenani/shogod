#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "config/bind_state.hpp"
#include "config/config.hpp"
#include "config/keybinds.hpp"
#include "features/aaa/antiaim.hpp"
#include "features/backtrack.hpp"
#include "features/createmove.hpp"
#include "features/math/angles.hpp"
#include "features/math/hitbox_geom.hpp"
#include "features/math/hitchance.hpp"
#include "features/math/hull.hpp"
#include "features/math/vec2.hpp"
#include "features/math/vec3.hpp"
#include "features/rage/target.hpp"
#include "sdk/bones.hpp"
#include "sdk/hitboxes.hpp"
#include "sdk/snapshot.hpp"
#include "sdk/usercmd.hpp"

// Progressive contract feature gates. M1, M2 & M3 contracts are active in shogod_core.
#ifndef SHOGOD_E2E_ENABLE_M1_CONTRACTS
#define SHOGOD_E2E_ENABLE_M1_CONTRACTS 1
#endif
#ifndef SHOGOD_E2E_ENABLE_M2_CONTRACTS
#define SHOGOD_E2E_ENABLE_M2_CONTRACTS 1
#endif
#ifndef SHOGOD_E2E_ENABLE_M3_CONTRACTS
#define SHOGOD_E2E_ENABLE_M3_CONTRACTS 1
#endif

#if SHOGOD_E2E_ENABLE_M1_CONTRACTS
#include "features/lagcomp.hpp"
#endif

using namespace shogod;

namespace hvh_sim {

constexpr float k_pi = 3.14159265358979323846f;
constexpr float k_deg_to_rad = k_pi / 180.0f;
constexpr float k_rad_to_deg = 180.0f / k_pi;

// -----------------------------------------------------------------------------
// 1. TickClock64_300: 64 Hz server tick + 300 Hz (5 subframes/tick) scheduler
// -----------------------------------------------------------------------------
struct TickClock64_300 {
    static constexpr int k_tick_rate = 64;
    static constexpr float k_dt = 1.0f / 64.0f;
    static constexpr int k_subframes_per_tick = 5;

    int tick = 1000;
    int command_number = 500;
    int render_frame = 0;

    [[nodiscard]] float curtime() const noexcept {
        return static_cast<float>(tick) * k_dt;
    }

    template <typename SubframeFn, typename ServerStepFn>
    void step_tick(int subframes, SubframeFn&& on_subframe, ServerStepFn&& on_server_tick) {
        const int count = std::max(1, subframes);
        for (int sf = 0; sf < count; ++sf) {
            on_subframe(command_number, tick, curtime(), sf, render_frame);
            ++render_frame;
        }
        on_server_tick(command_number, tick, curtime());
        ++tick;
        ++command_number;
    }

    template <typename SubframeFn, typename ServerStepFn>
    void run_ticks(int tick_count, int subframes_per_tick,
                   SubframeFn&& on_subframe, ServerStepFn&& on_server_tick) {
        for (int t = 0; t < tick_count; ++t) {
            step_tick(subframes_per_tick, on_subframe, on_server_tick);
        }
    }
};

// -----------------------------------------------------------------------------
// 2. CS2PhysicsEngine: Source 2 movement + sv_quantize_movement_input = 1
// -----------------------------------------------------------------------------
struct CS2PhysicsState {
    math::vec3 pos{0.0f, 0.0f, 0.0f};
    math::vec3 vel{0.0f, 0.0f, 0.0f};
    bool on_ground = true;
    float ground_z = 0.0f;
};

struct CS2PhysicsEngine {
    static constexpr float sv_friction = 5.2f;
    static constexpr float sv_stopspeed = 80.0f;
    static constexpr float sv_accelerate = 5.5f;
    static constexpr float sv_airaccelerate = 12.0f;
    static constexpr float air_wish_cap = 30.0f;
    static constexpr float sv_maxspeed = 250.0f;
    static constexpr float sv_gravity = 800.0f;
    static constexpr float jump_impulse_z = 301.993377f;

    static void quantize_move_input(float fwd, float left, float& q_fwd, float& q_left) noexcept {
        q_fwd = std::clamp(std::round(fwd), -1.0f, 1.0f);
        q_left = std::clamp(std::round(left), -1.0f, 1.0f);
    }

    static math::vec3 wish_dir_from_cmd(float cmd_yaw_deg, float q_fwd, float q_left) noexcept {
        const float rad = math::normalize_yaw(cmd_yaw_deg) * k_deg_to_rad;
        const math::vec3 fwd_xy{std::cos(rad), std::sin(rad), 0.0f};
        const math::vec3 right_xy{std::sin(rad), -std::cos(rad), 0.0f};
        math::vec3 wish = fwd_xy * q_fwd - right_xy * q_left;
        const float len = std::hypot(wish.x, wish.y);
        if (len > 1e-4f) {
            wish.x /= len;
            wish.y /= len;
            wish.z = 0.0f;
        } else {
            wish = {};
        }
        return wish;
    }

    static void step_tick(CS2PhysicsState& st, float cmd_yaw_deg, float raw_fwd, float raw_left,
                          bool do_jump, float maxspeed = sv_maxspeed,
                          float dt = TickClock64_300::k_dt) noexcept {
        float q_fwd = 0.0f;
        float q_left = 0.0f;
        quantize_move_input(raw_fwd, raw_left, q_fwd, q_left);

        const math::vec3 wish = wish_dir_from_cmd(cmd_yaw_deg, q_fwd, q_left);
        const bool has_wish = std::hypot(wish.x, wish.y) > 1e-4f;

        if (do_jump && st.on_ground) {
            st.vel.z = jump_impulse_z;
            st.on_ground = false;
        }

        if (st.on_ground) {
            const float spd = std::hypot(st.vel.x, st.vel.y);
            if (spd > 0.1f) {
                const float control = std::max(spd, sv_stopspeed);
                const float drop = control * sv_friction * dt;
                const float new_spd = std::max(0.0f, spd - drop);
                const float factor = new_spd / spd;
                st.vel.x *= factor;
                st.vel.y *= factor;
            } else {
                st.vel.x = 0.0f;
                st.vel.y = 0.0f;
            }
            if (has_wish) {
                const float cur_spd = st.vel.x * wish.x + st.vel.y * wish.y;
                const float add = maxspeed - cur_spd;
                if (add > 0.0f) {
                    const float accel = std::min(add, sv_accelerate * maxspeed * dt);
                    st.vel.x += accel * wish.x;
                    st.vel.y += accel * wish.y;
                }
            }
            st.vel.z = 0.0f;
        } else {
            if (has_wish) {
                const float cur_spd = st.vel.x * wish.x + st.vel.y * wish.y;
                const float add = air_wish_cap - cur_spd;
                if (add > 0.0f) {
                    const float accel = std::min(add, sv_airaccelerate * maxspeed * dt);
                    st.vel.x += accel * wish.x;
                    st.vel.y += accel * wish.y;
                }
            }
            st.vel.z -= 0.5f * sv_gravity * dt;
        }

        st.pos.x += st.vel.x * dt;
        st.pos.y += st.vel.y * dt;
        st.pos.z += st.vel.z * dt;

        if (!st.on_ground) {
            st.vel.z -= 0.5f * sv_gravity * dt;
            if (st.pos.z <= st.ground_z && st.vel.z <= 0.0f) {
                st.pos.z = st.ground_z;
                st.vel.z = 0.0f;
                st.on_ground = true;
            }
        }
    }

    [[nodiscard]] static float speed_2d(const CS2PhysicsState& st) noexcept {
        return std::hypot(st.vel.x, st.vel.y);
    }
};

// -----------------------------------------------------------------------------
// 3. UserCmdHarness: Zero-dependency aligned sdk::user_command + protobuf mock
// -----------------------------------------------------------------------------
struct UserCmdHarness {
    struct subtick_rep_storage {
        int max = 32;
        sdk::subtick_move_step_pb* elements[32]{};
    };
    struct input_history_rep_storage {
        int max = 16;
        sdk::csgo_input_history_entry_pb* elements[16]{};
    };

    alignas(16) std::array<std::byte, 0x700> input_buf{};
    sdk::button_data btn_attack{};
    sdk::button_data btn_attack2{};

    sdk::user_command cmd{};
    sdk::base_user_cmd_pb base{};
    sdk::msg_qangle_pb base_va{};
    sdk::in_button_state_pb buttons_pb{};
    std::array<sdk::subtick_move_step_pb, 32> subtick_pool{};
    subtick_rep_storage subtick_rep{};
    std::array<sdk::csgo_input_history_entry_pb, 16> history_pool{};
    std::array<sdk::msg_qangle_pb, 16> history_va_pool{};
    input_history_rep_storage history_rep{};

    UserCmdHarness() noexcept { reset_wiring(); }

    void reset_wiring() noexcept {
        input_buf.fill(std::byte{0});
        btn_attack = {};
        btn_attack2 = {};
        btn_attack.button_flag = sdk::k_in_attack;
        btn_attack2.button_flag = sdk::k_in_attack2;

        std::uintptr_t p_atk = reinterpret_cast<std::uintptr_t>(&btn_attack);
        std::uintptr_t p_atk2 = reinterpret_cast<std::uintptr_t>(&btn_attack2);
        std::memcpy(input_buf.data() + sdk::csgo_input_view::k_button_attack_offset, &p_atk, sizeof(p_atk));
        std::memcpy(input_buf.data() + sdk::csgo_input_view::k_button_attack2_offset, &p_atk2, sizeof(p_atk2));

        cmd = {};
        base = {};
        base_va = {};
        buttons_pb = {};
        for (auto& s : subtick_pool) s.clear();
        subtick_rep.max = 32;
        for (int i = 0; i < 32; ++i) subtick_rep.elements[i] = &subtick_pool[static_cast<std::size_t>(i)];

        for (std::size_t i = 0; i < history_pool.size(); ++i) {
            history_pool[i] = {};
            history_va_pool[i] = {};
            history_pool[i].view_angles = &history_va_pool[i];
            history_rep.elements[i] = &history_pool[i];
        }
        history_rep.max = 16;

        base.buttons_pb = &buttons_pb;
        base.viewangles = &base_va;
        base.subtick_moves.current_size = 0;
        base.subtick_moves.capacity = 32;
        base.subtick_moves.ss =
            reinterpret_cast<sdk::repeated_ptr_field<sdk::subtick_move_step_pb>::rep_t*>(&subtick_rep);

        cmd.pb.base = &base;
        cmd.pb.input_history.current_size = 0;
        cmd.pb.input_history.capacity = 16;
        cmd.pb.input_history.ss =
            reinterpret_cast<sdk::repeated_ptr_field<sdk::csgo_input_history_entry_pb>::rep_t*>(&history_rep);
        cmd.pb.attack1_start_history_index = -1;
        cmd.pb.attack2_start_history_index = -1;
    }

    sdk::csgo_input_view* input_view() noexcept {
        return reinterpret_cast<sdk::csgo_input_view*>(input_buf.data());
    }

    void begin_tick(int command_number, int tick, const math::angles& cam_va,
                    std::uint64_t held_buttons, float fwd, float left) noexcept {
        cmd.command_number = command_number;
        cmd.server_tick = tick;
        cmd.simulation_time = static_cast<float>(tick) * TickClock64_300::k_dt;
        cmd.button_state.held = held_buttons;
        cmd.button_state.toggle = 0;
        cmd.button_state.scroll = 0;
        buttons_pb.set_all(held_buttons, 0, 0);

        input_view()->set_viewangles(cam_va);
        base_va.set(cam_va.pitch, cam_va.yaw, 0.0f);
        base.legacy_command_number = command_number;
        base.client_tick = tick;
        base.set_forwardmove(fwd);
        base.set_leftmove(left);
        base.clear_subtick_moves();
        cmd.pb.input_history.current_size = 1;
        history_pool[0].set_render_tick_count(tick);
        history_pool[0].set_player_tick_count(tick);
        history_va_pool[0].set(cam_va.pitch, cam_va.yaw, 0.0f);
    }

    // Replicates createmove.cpp movement rotation + button bit sync for pure harness tests.
    static void rotate_move_harness(float fwd, float left, float from_yaw, float to_yaw,
                                    float& out_fwd, float& out_left) noexcept {
        const float from_rad = math::normalize_yaw(from_yaw) * k_deg_to_rad;
        const float to_rad = math::normalize_yaw(to_yaw) * k_deg_to_rad;
        const math::vec3 old_fwd{std::cos(from_rad), std::sin(from_rad), 0.0f};
        const math::vec3 old_right{std::sin(from_rad), -std::cos(from_rad), 0.0f};
        const math::vec3 wish = old_fwd * fwd - old_right * left;

        const math::vec3 new_fwd{std::cos(to_rad), std::sin(to_rad), 0.0f};
        const math::vec3 new_right{std::sin(to_rad), -std::cos(to_rad), 0.0f};
        const float raw_input_peak = std::max(std::fabs(fwd), std::fabs(left));
        const float rf = wish.x * new_fwd.x + wish.y * new_fwd.y;
        const float rl = -(wish.x * new_right.x + wish.y * new_right.y);
        const float peak = std::max(std::fabs(rf), std::fabs(rl));
        if (raw_input_peak > 0.5f && peak > 1e-3f) {
            out_fwd = std::clamp(rf / peak, -1.0f, 1.0f);
            out_left = std::clamp(rl / peak, -1.0f, 1.0f);
        } else {
            out_fwd = std::clamp(rf, -1.0f, 1.0f);
            out_left = std::clamp(rl, -1.0f, 1.0f);
        }
    }

    static std::uint64_t sync_move_buttons(std::uint64_t held, float fwd, float left) noexcept {
        constexpr std::uint64_t k_move_mask =
            sdk::k_in_forward | sdk::k_in_back | sdk::k_in_moveleft | sdk::k_in_moveright;
        held &= ~k_move_mask;
        if (fwd > 0.01f) held |= sdk::k_in_forward;
        else if (fwd < -0.01f) held |= sdk::k_in_back;
        if (left > 0.01f) held |= sdk::k_in_moveleft;
        else if (left < -0.01f) held |= sdk::k_in_moveright;
        return held;
    }

    [[nodiscard]] bool verify_protobuf_hygiene() const noexcept {
        if (!std::isfinite(base_va.x) || !std::isfinite(base_va.y) || !std::isfinite(base_va.z)) {
            return false;
        }
        if (base_va.x < -89.0f || base_va.x > 89.0f) return false;
        if (!std::isfinite(base.forwardmove) || !std::isfinite(base.leftmove)) return false;
        if (base.subtick_moves.current_size < 0 ||
            base.subtick_moves.current_size > base.subtick_moves.capacity) {
            return false;
        }
        for (int i = 0; i < base.subtick_moves.current_size; ++i) {
            const auto* st = base.subtick_moves.ss ? base.subtick_moves.ss->elements[i] : nullptr;
            if (!st) return false;
            if ((st->has_bits & 0x60u) != 0u &&
                st->pitch_delta == 0.0f && st->yaw_delta == 0.0f) {
                return false;
            }
        }
        return true;
    }
};

// Safe capacity-checked subtick helper modeling F3.3 contract cleanly.
inline sdk::subtick_move_step_pb* safe_add_subtick_step(
    sdk::repeated_ptr_field<sdk::subtick_move_step_pb>& field,
    std::uint64_t button, bool pressed, float when) noexcept {
    if (!field.ss || field.current_size < 0 || field.current_size >= field.capacity) {
        return nullptr;
    }
    sdk::subtick_move_step_pb* step = field.ss->elements[field.current_size];
    if (!step) return nullptr;
    step->clear();
    step->set_button(button);
    step->set_pressed(pressed);
    step->set_when(when);
    ++field.current_size;
    return step;
}

inline void safe_zero_subtick_view_deltas(sdk::base_user_cmd_pb& base) noexcept {
    if (!base.subtick_moves.ss) return;
    const int n = std::min(base.subtick_moves.current_size, base.subtick_moves.capacity);
    for (int i = 0; i < n; ++i) {
        sdk::subtick_move_step_pb* step = base.subtick_moves.ss->elements[i];
        if (!step) continue;
        step->pitch_delta = 0.0f;
        step->yaw_delta = 0.0f;
        step->has_bits &= ~0x60u;
    }
}

// -----------------------------------------------------------------------------
// 4. MultiPlayerHitboxArena: Full 17-hitbox CS2 rig + 16-slot lag ring + backtrack
// -----------------------------------------------------------------------------
struct LagRecordSim {
    std::uint32_t pawn_handle = 0;
    int simulation_tick = 0;
    float simulation_time = 0.0f;
    bool shooting = false;
    bool valid = false;
    math::vec3 origin{};
    std::vector<math::hitbox_geometry> hitboxes{};
};

struct MultiPlayerHitboxArena {
    static math::mat3x4 make_rot_z_tm(const math::vec3& pos, float yaw_deg, float scale = 1.0f) noexcept {
        const float half = math::normalize_yaw(yaw_deg) * 0.5f * k_deg_to_rad;
        const float raw[8] = {pos.x, pos.y, pos.z, scale, 0.0f, 0.0f, std::sin(half), std::cos(half)};
        return math::matrix_from_2x4(raw);
    }

    static std::vector<math::hitbox_geometry> build_player_17_hitboxes(
        const math::vec3& origin, float yaw_deg = 0.0f, int head_shape_type = 2) {
        const math::mat3x4 tm = make_rot_z_tm(origin, yaw_deg, 1.0f);
        std::vector<math::hitbox_geometry> hbs;
        hbs.reserve(17);

        auto add = [&](const math::vec3& mins, const math::vec3& maxs, float r,
                       int shape, int hg, bool foot) {
            hbs.push_back(math::build_hitbox_geometry(mins, maxs, r, shape, hg, foot, tm, 1.0f));
        };

        // 0: Head (hg=1)
        add({-3.0f, -3.0f, 62.0f}, {3.0f, 3.0f, 68.0f}, 4.3f, head_shape_type, 1, false);
        // 1: Neck (hg=8)
        add({0.0f, 0.0f, 57.0f}, {0.0f, 0.0f, 61.0f}, 2.8f, 2, 8, false);
        // 2: Upper Chest (hg=2)
        add({-4.0f, -7.0f, 47.0f}, {4.0f, 7.0f, 56.0f}, 6.0f, 2, 2, false);
        // 3: Lower Chest (hg=2)
        add({-3.5f, -6.5f, 40.0f}, {3.5f, 6.5f, 47.0f}, 5.5f, 2, 2, false);
        // 4: Stomach (hg=3)
        add({-3.5f, -6.0f, 32.0f}, {3.5f, 6.0f, 39.5f}, 5.5f, 2, 3, false);
        // 5: Pelvis (hg=0 generic/pelvis)
        add({-4.0f, -6.0f, 26.0f}, {4.0f, 6.0f, 32.0f}, 5.8f, 2, 0, false);
        // 6-7: Left Upper/Lower Arm (hg=4)
        add({0.0f, 11.0f, 43.0f}, {0.0f, 11.0f, 53.0f}, 3.2f, 2, 4, false);
        add({0.0f, 13.0f, 33.0f}, {0.0f, 13.0f, 43.0f}, 2.7f, 2, 4, false);
        // 8-9: Right Upper/Lower Arm (hg=5)
        add({0.0f, -11.0f, 43.0f}, {0.0f, -11.0f, 53.0f}, 3.2f, 2, 5, false);
        add({0.0f, -13.0f, 33.0f}, {0.0f, -13.0f, 43.0f}, 2.7f, 2, 5, false);
        // 10-12: Left Thigh, Shin, Foot (hg=6)
        add({0.0f, 5.0f, 15.0f}, {0.0f, 5.0f, 26.0f}, 4.2f, 2, 6, false);
        add({0.0f, 5.0f, 4.0f}, {0.0f, 5.0f, 15.0f}, 3.4f, 2, 6, false);
        add({-2.0f, 5.0f, 0.5f}, {5.0f, 5.0f, 3.5f}, 2.6f, 0, 6, true);
        // 13-15: Right Thigh, Shin, Foot (hg=7)
        add({0.0f, -5.0f, 15.0f}, {0.0f, -5.0f, 26.0f}, 4.2f, 2, 7, false);
        add({0.0f, -5.0f, 4.0f}, {0.0f, -5.0f, 15.0f}, 3.4f, 2, 7, false);
        add({-2.0f, -5.0f, 0.5f}, {5.0f, -5.0f, 3.5f}, 2.6f, 0, 7, true);
        // 16: Hand/Forearm extra capsule (hg=4)
        add({2.0f, 14.0f, 30.0f}, {5.0f, 14.0f, 33.0f}, 2.2f, 2, 4, false);

        return hbs;
    }

    static sdk::player_snapshot make_enemy_snapshot(
        int index, std::uint32_t handle, const math::vec3& origin,
        const math::vec3& vel = {}, int hp = 100, int armor = 100, bool helmet = true) noexcept {
        sdk::player_snapshot p{};
        p.pawn = static_cast<std::uintptr_t>(0x100000ULL + static_cast<std::uintptr_t>(index) * 0x1000ULL);
        p.pawn_handle = handle;
        p.index = index;
        p.team = 2;
        p.health = hp;
        p.armor = armor;
        p.helmet = helmet;
        p.helmet_known = true;
        p.alive = hp > 0;
        p.enemy = true;
        p.visible = true;
        p.origin = origin;
        p.eye = origin + math::vec3{0.0f, 0.0f, 64.0f};
        p.velocity = vel;
        p.speed_2d = std::hypot(vel.x, vel.y);
        p.bounds_min = {-16.0f, -16.0f, 0.0f};
        p.bounds_max = {16.0f, 16.0f, 72.0f};
        p.bounds_valid = true;
        p.bone_count = 23;
        p.bones[sdk::bone::root] = origin;
        p.bones[sdk::bone::pelvis] = origin + math::vec3{0.0f, 0.0f, 30.0f};
        p.bones[sdk::bone::spine_0] = origin + math::vec3{0.0f, 0.0f, 35.0f};
        p.bones[sdk::bone::spine_1] = origin + math::vec3{0.0f, 0.0f, 40.0f};
        p.bones[sdk::bone::spine_2] = origin + math::vec3{0.0f, 0.0f, 46.0f};
        p.bones[sdk::bone::spine_3] = origin + math::vec3{0.0f, 0.0f, 52.0f};
        p.bones[sdk::bone::neck] = origin + math::vec3{0.0f, 0.0f, 58.0f};
        p.bones[sdk::bone::head] = origin + math::vec3{0.0f, 0.0f, 65.0f};
        p.bones[sdk::bone::clavicle_l] = origin + math::vec3{0.0f, 6.0f, 53.0f};
        p.bones[sdk::bone::upperarm_l] = origin + math::vec3{0.0f, 11.0f, 51.0f};
        p.bones[sdk::bone::lowerarm_l] = origin + math::vec3{0.0f, 13.0f, 40.0f};
        p.bones[sdk::bone::hand_l] = origin + math::vec3{2.0f, 14.0f, 32.0f};
        p.bones[sdk::bone::clavicle_r] = origin + math::vec3{0.0f, -6.0f, 53.0f};
        p.bones[sdk::bone::upperarm_r] = origin + math::vec3{0.0f, -11.0f, 51.0f};
        p.bones[sdk::bone::lowerarm_r] = origin + math::vec3{0.0f, -13.0f, 40.0f};
        p.bones[sdk::bone::hand_r] = origin + math::vec3{2.0f, -14.0f, 32.0f};
        p.bones[sdk::bone::leg_upper_l] = origin + math::vec3{0.0f, 5.0f, 26.0f};
        p.bones[sdk::bone::leg_lower_l] = origin + math::vec3{0.0f, 5.0f, 14.0f};
        p.bones[sdk::bone::ankle_l] = origin + math::vec3{0.0f, 5.0f, 2.0f};
        p.bones[sdk::bone::leg_upper_r] = origin + math::vec3{0.0f, -5.0f, 26.0f};
        p.bones[sdk::bone::leg_lower_r] = origin + math::vec3{0.0f, -5.0f, 14.0f};
        p.bones[sdk::bone::ankle_r] = origin + math::vec3{0.0f, -5.0f, 2.0f};
        return p;
    }

    // 16-slot ring buffer modeling F1.3 / F1.6 contract without requiring m_flOldSimulationTime.
    struct LagTrackSim {
        std::uint32_t pawn_handle = 0;
        float prev_sim_time = 0.0f;
        int next = 0;
        std::array<LagRecordSim, 16> records{};

        void push(std::uint32_t handle, int sim_tick, float sim_time, float last_shot_time,
                  const math::vec3& origin, const std::vector<math::hitbox_geometry>& hbs) {
            if (handle != pawn_handle) {
                pawn_handle = handle;
                prev_sim_time = 0.0f;
                next = 0;
                records = {};
            }
            if (sim_time <= prev_sim_time && prev_sim_time > 0.0f) return;
            const bool shot =
                prev_sim_time > 0.0f && last_shot_time > prev_sim_time && last_shot_time <= sim_time;
            prev_sim_time = sim_time;

            LagRecordSim& slot = records[static_cast<std::size_t>(next)];
            next = (next + 1) % 16;
            slot.pawn_handle = handle;
            slot.simulation_tick = sim_tick;
            slot.simulation_time = sim_time;
            slot.shooting = shot;
            slot.valid = true;
            slot.origin = origin;
            slot.hitboxes = hbs;
        }

        // Selects up to 3 distinct records (newest, shooting, oldest) within sv_maxunlag window.
        [[nodiscard]] std::vector<const LagRecordSim*> select(
            int newest_sim_tick, float window_ms, float ping_ms, float interp_ms) const {
            const float max_unlag_s = std::clamp(window_ms, 0.0f, 200.0f) / 1000.0f;
            const float latency_s = (std::max(0.0f, ping_ms) + std::max(0.0f, interp_ms)) / 1000.0f;
            const float newest_time = static_cast<float>(newest_sim_tick) * TickClock64_300::k_dt;

            const LagRecordSim* newest = nullptr;
            const LagRecordSim* shooting = nullptr;
            const LagRecordSim* oldest = nullptr;

            for (const auto& rec : records) {
                if (!rec.valid || rec.pawn_handle != pawn_handle) continue;
                const float age_s = newest_time - rec.simulation_time;
                // Account for latency + interp against sv_maxunlag cleanly.
                if (age_s < -0.001f || age_s > max_unlag_s || (age_s + latency_s) > max_unlag_s + 1e-5f) {
                    continue;
                }
                if (!newest || rec.simulation_time > newest->simulation_time) newest = &rec;
                if (!oldest || rec.simulation_time < oldest->simulation_time) oldest = &rec;
                if (rec.shooting && (!shooting || rec.simulation_time > shooting->simulation_time)) {
                    shooting = &rec;
                }
            }

            std::vector<const LagRecordSim*> out;
            if (newest) out.push_back(newest);
            if (shooting && shooting != newest) out.push_back(shooting);
            if (oldest && oldest != newest && oldest != shooting) out.push_back(oldest);
            return out;
        }
    };
};

// -----------------------------------------------------------------------------
// 5. CoverAndPoseGeometry: Analytic AABB wall trace + weapon pose twist model
// -----------------------------------------------------------------------------
struct CoverAndPoseGeometry {
    struct WallAABB {
        math::vec3 min{};
        math::vec3 max{};
    };

    static float trace_ray_aabb(const math::vec3& start, const math::vec3& end,
                                const WallAABB& box) noexcept {
        const math::vec3 d = end - start;
        float tmin = 0.0f;
        float tmax = 1.0f;
        const float s[3] = {start.x, start.y, start.z};
        const float dir[3] = {d.x, d.y, d.z};
        const float bmin[3] = {box.min.x, box.min.y, box.min.z};
        const float bmax[3] = {box.max.x, box.max.y, box.max.z};

        for (int i = 0; i < 3; ++i) {
            if (std::fabs(dir[i]) < 1e-6f) {
                if (s[i] < bmin[i] || s[i] > bmax[i]) return 1.0f;
            } else {
                const float inv = 1.0f / dir[i];
                float t1 = (bmin[i] - s[i]) * inv;
                float t2 = (bmax[i] - s[i]) * inv;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) return 1.0f;
            }
        }
        return std::clamp(tmin, 0.0f, 1.0f);
    }

    static float trace_walls(const math::vec3& start, const math::vec3& end,
                             std::span<const WallAABB> walls) noexcept {
        float best = 1.0f;
        for (const auto& w : walls) {
            best = std::min(best, trace_ray_aabb(start, end, w));
        }
        return best;
    }
};

// -----------------------------------------------------------------------------
// 6. DefaultJsonConfigFixture: Embedded parity fixture for user's default.json
// -----------------------------------------------------------------------------
struct DefaultJsonConfigFixture {
    static constexpr const char* k_default_json = R"({
  "anti_aim": {
    "at_targets": true,
    "auto_adjust": true,
    "base_pitch": 1,
    "base_yaw": 4,
    "enabled": false,
    "freestanding": false,
    "jitter_range": 30.0,
    "on_air": true,
    "on_move": true,
    "on_shot": false,
    "toggle": { "hold": false, "vk": 0 },
    "yaw_offset": 0.0
  },
  "misc": {
    "block_server_detected": false,
    "info": { "keybind_list": true, "watermark": true },
    "movement": {
      "auto_strafe": true,
      "auto_strafe_bind": { "hold": false, "vk": 0 },
      "bhop": true,
      "bhop_bind": { "hold": false, "vk": 0 },
      "edge_jump": false,
      "leg_movement": false,
      "peek_assist": false,
      "peek_assist_bind": { "hold": false, "vk": 0 }
    },
    "utilities": { "auto_pistol": false }
  },
  "rage": {
    "auto_revolver": true,
    "backtrack": true,
    "backtrack_ms": 200.0,
    "enabled": true,
    "extrapolation_ms": 50.0,
    "priority": 2,
    "remove_recoil": true,
    "silent_aim": true,
    "toggle": { "hold": false, "vk": 0 },
    "weapon_overrides": {
      "ak47": {
        "aim_points": 47,
        "auto_crouch": false,
        "auto_fire": true,
        "auto_scope": true,
        "auto_stop": true,
        "auto_stop_flags": 7,
        "auto_stop_speed": 25.0,
        "auto_stop_ticks": 6,
        "auto_stop_wait": false,
        "delay_shot": 0,
        "delay_ticks": 2,
        "force_body_aim": false,
        "fov": 180.0,
        "hitchance": 60.0,
        "ignore_limbs": false,
        "min_damage": 4.0,
        "multipoint": 40.0,
        "penetration": true,
        "prefer_body_aim": false,
        "safe_point": 50.0,
        "strict_hitchance": false
      }
    },
    "weapons": [
      { "aim_points": 5, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 25.0, "auto_stop_ticks": 6, "auto_stop_wait": false, "delay_shot": 0, "delay_ticks": 2, "force_body_aim": false, "fov": 90.0, "hitchance": 50.0, "ignore_limbs": false, "min_damage": 25.0, "multipoint": 30.0, "penetration": true, "penetration_limit": 1.0, "prefer_body_aim": false, "safe_point": 70.0, "strict_hitchance": false },
      { "aim_points": 5, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 25.0, "auto_stop_ticks": 6, "auto_stop_wait": false, "delay_shot": 0, "delay_ticks": 2, "force_body_aim": false, "fov": 90.0, "hitchance": 50.0, "ignore_limbs": false, "min_damage": 25.0, "multipoint": 30.0, "penetration": true, "penetration_limit": 1.0, "prefer_body_aim": false, "safe_point": 70.0, "strict_hitchance": false },
      { "aim_points": 5, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 25.0, "auto_stop_ticks": 6, "auto_stop_wait": false, "delay_shot": 0, "delay_ticks": 2, "force_body_aim": false, "fov": 120.0, "hitchance": 55.0, "ignore_limbs": false, "min_damage": 25.0, "multipoint": 35.0, "penetration": true, "penetration_limit": 1.0, "prefer_body_aim": false, "safe_point": 60.0, "strict_hitchance": false },
      { "aim_points": 47, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 25.0, "auto_stop_ticks": 6, "auto_stop_wait": false, "delay_shot": 0, "delay_ticks": 2, "force_body_aim": false, "fov": 180.0, "hitchance": 60.0, "ignore_limbs": false, "min_damage": 4.0, "multipoint": 40.0, "penetration": true, "penetration_limit": 0.5, "prefer_body_aim": false, "safe_point": 50.0, "strict_hitchance": false },
      { "aim_points": 63, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 60.0, "auto_stop_ticks": 6, "auto_stop_wait": true, "delay_shot": 0, "delay_ticks": 6, "force_body_aim": false, "fov": 180.0, "hitchance": 73.0, "ignore_limbs": true, "min_damage": 37.0, "multipoint": 100.0, "penetration": true, "penetration_limit": 1.0, "prefer_body_aim": false, "safe_point": 15.909090995788574, "strict_hitchance": false },
      { "aim_points": 63, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 60.0, "auto_stop_ticks": 6, "auto_stop_wait": true, "delay_shot": 0, "delay_ticks": 6, "force_body_aim": false, "fov": 180.0, "hitchance": 73.0, "ignore_limbs": true, "min_damage": 37.0, "multipoint": 100.0, "penetration": true, "penetration_limit": 1.0, "prefer_body_aim": false, "safe_point": 15.909090995788574, "strict_hitchance": false },
      { "aim_points": 63, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 60.0, "auto_stop_ticks": 6, "auto_stop_wait": true, "delay_shot": 0, "delay_ticks": 6, "force_body_aim": false, "fov": 180.0, "hitchance": 73.0, "ignore_limbs": true, "min_damage": 37.0, "multipoint": 100.0, "penetration": true, "prefer_body_aim": false, "safe_point": 15.909090995788574, "strict_hitchance": false },
      { "aim_points": 5, "auto_crouch": false, "auto_fire": true, "auto_scope": true, "auto_stop": true, "auto_stop_flags": 5, "auto_stop_speed": 25.0, "auto_stop_ticks": 6, "auto_stop_wait": false, "delay_shot": 0, "delay_ticks": 2, "force_body_aim": false, "fov": 120.0, "hitchance": 50.0, "ignore_limbs": false, "min_damage": 25.0, "multipoint": 30.0, "penetration": true, "prefer_body_aim": false, "safe_point": 70.0, "strict_hitchance": false }
    ]
  },
  "ui": {
    "accent": { "a": 1.0, "b": 1.0, "g": 0.6323531866073608, "r": 0.4771241545677185 },
    "animations": true,
    "dark_theme": true,
    "menu_key": 45,
    "opacity": 1.0
  },
  "version": 1,
  "visuals": {
    "ammo": true,
    "armor_bar": true,
    "arms_chams": {
      "enabled": false,
      "flags": 0,
      "fresnel_exponent": 2.0,
      "fresnel_falloff": 2.0,
      "invisible": { "a": 1.0, "b": 0.25, "g": 0.25, "r": 1.0 },
      "material": 0,
      "visible": { "a": 1.0, "b": 1.0, "g": 0.550000011920929, "r": 0.3100000023841858 },
      "wire_thickness": 1.0
    },
    "backtrack_ghost": true,
    "bomb_timer": true,
    "box": 0,
    "box_color": { "a": 1.0, "b": 0.07643312215805054, "g": 0.07643312215805054, "r": 1.0 },
    "camera_fov": 0,
    "distance": true,
    "enabled": true,
    "esp_alpha": 255,
    "exposure": 60.0,
    "flags": true,
    "fov_while_scoped": false,
    "health_bar": true,
    "light_color": { "a": 1.0, "b": 0.06369423866271973, "g": 0.03083289973437786, "r": 0.023530354723334312 },
    "light_modulate": true,
    "local_chams": {
      "enabled": false,
      "flags": 0,
      "fresnel_exponent": 2.0,
      "fresnel_falloff": 2.0,
      "invisible": { "a": 1.0, "b": 0.25, "g": 0.25, "r": 1.0 },
      "material": 0,
      "visible": { "a": 1.0, "b": 1.0, "g": 0.550000011920929, "r": 0.3100000023841858 },
      "wire_thickness": 1.0
    },
    "name": true,
    "no_flash": true,
    "no_scope": true,
    "no_smoke": true,
    "offscreen": true,
    "penetration_crosshair": true,
    "player_chams": {
      "enabled": true,
      "flags": 5,
      "fresnel_exponent": 2.0,
      "fresnel_falloff": 2.0,
      "invisible": { "a": 1.0, "b": 0.9490445852279663, "g": 0.9490445852279663, "r": 0.9490445852279663 },
      "material": 1,
      "visible": { "a": 0.7515923380851746, "b": 1.0, "g": 0.7092220783233643, "r": 0.5541400909423828 },
      "wire_thickness": 1.7727272510528564
    },
    "remove_view_punch": true,
    "scope_blend": false,
    "scope_blend_alpha": 110,
    "skeleton": true,
    "team_chams": {
      "enabled": false,
      "flags": 0,
      "fresnel_exponent": 2.0,
      "fresnel_falloff": 2.0,
      "invisible": { "a": 1.0, "b": 0.25, "g": 0.25, "r": 1.0 },
      "material": 0,
      "visible": { "a": 1.0, "b": 1.0, "g": 0.550000011920929, "r": 0.3100000023841858 },
      "wire_thickness": 1.0
    },
    "third_person": true,
    "third_person_bind": { "hold": false, "vk": 4 },
    "third_person_distance": 79,
    "toggle": { "hold": false, "vk": 0 },
    "visible_only": false,
    "weapon": true,
    "weapon_chams": {
      "enabled": false,
      "flags": 0,
      "fresnel_exponent": 2.0,
      "fresnel_falloff": 2.0,
      "invisible": { "a": 1.0, "b": 0.25, "g": 0.25, "r": 1.0 },
      "material": 0,
      "visible": { "a": 1.0, "b": 1.0, "g": 0.550000011920929, "r": 0.3100000023841858 },
      "wire_thickness": 1.0
    },
    "weapon_icon": true,
    "world_color": { "a": 1.0, "b": 0.5414012670516968, "g": 0.5414012670516968, "r": 0.5414012670516968 },
    "world_modulate": true
  }
})";

    static config::store load() {
        config::store s;
        EXPECT_TRUE(s.from_json(k_default_json));
        return s;
    }
};

}  // namespace hvh_sim

using namespace hvh_sim;

// =============================================================================
// TIER 1 — Happy-Path Functional E2E Tests (18 cases)
// =============================================================================

TEST(E2E_Tier1, T1_R1_PreferBodyAim_HitgroupIsolation_And_ChanceMulti) {
    // F1.1, F1.2, F1.7: Rifle (ak47 override) + prefer_body_aim + arm/pelvis hitgroup isolation.
    config::store cfg_store = DefaultJsonConfigFixture::load();
    config::rage_weapon ak = cfg_store.get().rage.resolve_weapon(7, static_cast<int>(config::weapon_group::rifle));
    EXPECT_FLOAT_EQ(ak.min_damage, 4.0f);
    EXPECT_FLOAT_EQ(ak.hitchance, 60.0f);
    EXPECT_EQ(ak.auto_stop_flags, config::auto_stop_flag::all);
    ak.prefer_body_aim = true;

    const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes({400.0f, 0.0f, 0.0f});
    ASSERT_EQ(hbs.size(), 17u);

    // F1.2 contract: arms (hitgroup 4, 5) must NOT map to pelvis group 3; pelvis/generic (0) maps to 3.
    const math::hitbox_geometry& pelvis = hbs[5];
    const math::hitbox_geometry& left_arm = hbs[6];
    const math::hitbox_geometry& right_arm = hbs[8];
    const math::hitbox_geometry& stomach = hbs[4];
    EXPECT_EQ(pelvis.hitgroup, 0);
    EXPECT_EQ(pelvis.group, 3);
    EXPECT_EQ(stomach.group, 2);
    EXPECT_NE(left_arm.group, pelvis.group);
    EXPECT_NE(right_arm.group, pelvis.group);
    EXPECT_EQ(left_arm.group, 5);
    EXPECT_EQ(right_arm.group, 5);

    // F1.7 contract: stomach chance_multi must NEVER count stray arm rays as torso hits.
    const math::vec3 eye{0.0f, 0.0f, 64.0f};
    const math::weapon_spread_pattern spread{0.02f, 0.08f};
    // Build a synthetic pair: arm hitbox right at aim_point, stomach far away (no torso hit possible).
    std::vector<math::hitbox_geometry> arm_only_with_far_stomach;
    arm_only_with_far_stomach.push_back(hbs[6]);  // left arm (hg 4)
    auto far_stomach = hbs[4];
    far_stomach.center = {400.0f, 200.0f, 36.0f};
    far_stomach.first = {400.0f, 200.0f, 32.0f};
    far_stomach.last = {400.0f, 200.0f, 39.5f};
    arm_only_with_far_stomach.push_back(far_stomach);

    const float hc_stomach_on_arm =
        spread.chance_multi(eye, hbs[6].center, arm_only_with_far_stomach, /*desired_group=*/2);
    EXPECT_FLOAT_EQ(hc_stomach_on_arm, 0.0f);

    // Head desired_group=0 must not count chest/stomach when aimed at chest with tight spread.
    const math::weapon_spread_pattern tight_spread{0.004f, 0.01f};
    const float hc_head_on_chest = tight_spread.chance_multi(eye, hbs[2].center, hbs, /*desired_group=*/0);
    EXPECT_LT(hc_head_on_chest, 0.05f);

    // prefer_body_aim: even if head is lethal, body damage >= min_damage should be scannable.
    const float head_dmg = features::estimate_hit_damage(36.0f, 400.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::head);
    const float stomach_dmg = features::estimate_hit_damage(36.0f, 400.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::stomach);
    EXPECT_GT(head_dmg, 100.0f);
    EXPECT_TRUE(features::should_fire(70.0f, stomach_dmg, ak));
}

TEST(E2E_Tier1, T1_R2_IgnoreLimbs_OBBBoxGeometry_And_ZeusGroup) {
    // F1.2, F1.11, F1.16: Scout ignore_limbs, true OBB box intersection, Zeus -> pistol.
    config::store cfg_store = DefaultJsonConfigFixture::load();
    const config::rage_weapon& scout = cfg_store.get().rage.weapons[static_cast<std::size_t>(config::weapon_group::scout)];
    EXPECT_TRUE(scout.ignore_limbs);
    EXPECT_FLOAT_EQ(scout.hitchance, 73.0f);
    EXPECT_FLOAT_EQ(scout.min_damage, 37.0f);

    // F1.16: Zeus (def_index == 31) maps to weapon_group::pistol in config catalog.
    const config::rage_weapon_spec* zeus = config::find_rage_weapon_by_def(31);
    ASSERT_NE(zeus, nullptr);
    EXPECT_EQ(zeus->group, config::weapon_group::pistol);

    // F1.2: hitgroup_to_group boundary checks (-1, 0..8, 9).
    EXPECT_EQ(math::hitgroup_to_group(-1), -1);
    EXPECT_EQ(math::hitgroup_to_group(0), 3);
    EXPECT_EQ(math::hitgroup_to_group(1), 0);
    EXPECT_EQ(math::hitgroup_to_group(4), 5);
    EXPECT_EQ(math::hitgroup_to_group(5), 5);
    EXPECT_EQ(math::hitgroup_to_group(6), 4);
    EXPECT_EQ(math::hitgroup_to_group(9), -1);

    // F1.11: Flat thin OBB box (20 x 20 x 2) — ray hitting near flat box corner at (9, 9, 1)
    // must hit true OBB, while ray missing outside the thin Z slab at (0, 0, 5) must miss.
    const math::mat3x4 id_tm = MultiPlayerHitboxArena::make_rot_z_tm({300.0f, 0.0f, 50.0f}, 0.0f);
    const math::hitbox_geometry box_hb = math::build_hitbox_geometry(
        {-10.0f, -10.0f, -1.0f}, {10.0f, 10.0f, 1.0f}, 0.0f,
        /*shape_type=*/0, /*hitgroup=*/2, /*foot=*/false, id_tm, 1.0f);
    ASSERT_TRUE(box_hb.valid);
    EXPECT_EQ(box_hb.shape, math::hitbox_shape::box);

    const math::vec3 eye{0.0f, 0.0f, 50.0f};
    const math::vec3 corner_target{300.0f, 9.0f, 50.5f};
    const math::vec3 outside_z_target{300.0f, 0.0f, 55.0f};
    EXPECT_TRUE(math::ray_hits_geometry(eye, (corner_target - eye).normalised(), box_hb));
    EXPECT_FALSE(math::ray_hits_geometry(eye, (outside_z_target - eye).normalised(), box_hb));

    // F1.11: 45-deg rotated thin OBB multipoint generation + scaled ray intersection.
    const math::mat3x4 rot45_tm = MultiPlayerHitboxArena::make_rot_z_tm({300.0f, 0.0f, 50.0f}, 45.0f);
    const math::hitbox_geometry rot_box = math::build_hitbox_geometry(
        {-10.0f, -4.0f, -1.0f}, {10.0f, 4.0f, 1.0f}, 0.0f,
        /*shape_type=*/0, /*hitgroup=*/2, /*foot=*/false, rot45_tm, 1.0f);
    ASSERT_TRUE(rot_box.valid);
    const float eff_mp = math::effective_multipoint_scale(scout.multipoint, scout.safe_point);
    EXPECT_GT(eff_mp, 0.5f);
    std::array<math::vec3, 9> obb_pts{};
    const std::size_t obb_pt_count = math::generate_hitbox_points(rot_box, eye, eff_mp, obb_pts);
    ASSERT_GE(obb_pt_count, 5u);
    for (std::size_t i = 0; i < obb_pt_count; ++i) {
        EXPECT_TRUE(math::ray_hits_geometry_scaled(eye, (obb_pts[i] - eye).normalised(), rot_box, 1.0f));
    }
    // Tighter scale (0.5) rejects outer multipoints that lie at eff_mp (> 0.5).
    EXPECT_FALSE(math::ray_hits_geometry_scaled(eye, (obb_pts[obb_pt_count - 1] - eye).normalised(), rot_box, 0.5f));

    // Check ignore_limbs filtering over the 17-hitbox arena.
    const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes({300.0f, 0.0f, 0.0f});
    int non_limb_count = 0;
    for (const auto& hb : hbs) {
        const bool is_limb = (hb.hitgroup == 4 || hb.hitgroup == 5 || hb.hitgroup == 6 || hb.hitgroup == 7);
        if (!scout.ignore_limbs || !is_limb) ++non_limb_count;
    }
    EXPECT_EQ(non_limb_count, 6);  // head, neck, 2 chest, stomach, pelvis
}

TEST(E2E_Tier1, T1_R3_OnShotLagcompWithoutOldSimTime_And_200msWindow) {
    // F1.3, F1.6: m_flOldSimulationTime absent; detect shooting via prev_sim_time and retain shooting record.
    MultiPlayerHitboxArena::LagTrackSim track;
    constexpr std::uint32_t k_handle = 0x42u;
    const float shot_time = 1003.0f * TickClock64_300::k_dt;

    for (int t = 1000; t <= 1006; ++t) {
        const float sim_time = static_cast<float>(t) * TickClock64_300::k_dt;
        const float x = 400.0f + static_cast<float>(t - 1000) * 4.0f;
        const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes({x, 0.0f, 0.0f});
        const float last_shot = (t >= 1003) ? shot_time : 0.0f;
        track.push(k_handle, t, sim_time, last_shot, {x, 0.0f, 0.0f}, hbs);
    }

    const auto selected = track.select(1006, 200.0f, 20.0f, 15.6f);
    ASSERT_EQ(selected.size(), 3u);
    EXPECT_EQ(selected[0]->simulation_tick, 1006);
    EXPECT_TRUE(selected[1]->shooting);
    EXPECT_EQ(selected[1]->simulation_tick, 1003);
    EXPECT_EQ(selected[2]->simulation_tick, 1000);

#if SHOGOD_E2E_ENABLE_M1_CONTRACTS
    // Exercise production inline features::lag_compensation directly (without ent_old_sim_time).
    features::lag_compensation lc;
    lc.reset();
    constexpr std::uintptr_t k_pawn = 0x200000ULL;
    for (int t = 1000; t <= 1006; ++t) {
        const float sim_time = static_cast<float>(t) * TickClock64_300::k_dt;
        const float x = 400.0f + static_cast<float>(t - 1000) * 4.0f;
        sdk::live_hitbox_set hb_set{};
        hb_set.geometries = MultiPlayerHitboxArena::build_player_17_hitboxes({x, 0.0f, 0.0f});
        hb_set.simulation_tick = t;
        hb_set.simulation_time = sim_time;
        hb_set.valid = true;
        const float last_shot = (t >= 1003) ? shot_time : 0.0f;
        EXPECT_TRUE(lc.push_record(1, k_pawn, k_handle, t, sim_time, {x, 0.0f, 0.0f},
                                   std::move(hb_set), last_shot, t >= 1003));
    }
    // Duplicate simulation_tick push is deduplicated.
    sdk::live_hitbox_set dup_set{};
    dup_set.geometries = MultiPlayerHitboxArena::build_player_17_hitboxes({424.0f, 0.0f, 0.0f});
    dup_set.valid = true;
    EXPECT_FALSE(lc.push_record(1, k_pawn, k_handle, 1006, 1006.0f * TickClock64_300::k_dt,
                                {424.0f, 0.0f, 0.0f}, std::move(dup_set), shot_time, true));

    std::vector<const features::lag_record*> lc_out;
    lc.select(k_pawn, k_handle, 1006, 200.0f, lc_out);
    ASSERT_EQ(lc_out.size(), 3u);
    EXPECT_EQ(lc_out[0]->simulation_tick, 1006);
    EXPECT_TRUE(lc_out[1]->shooting);
    EXPECT_EQ(lc_out[1]->simulation_tick, 1003);
    EXPECT_EQ(lc_out[2]->simulation_tick, 1000);
    ASSERT_NE(lc.latest(k_pawn), nullptr);
    EXPECT_EQ(lc.latest(k_pawn)->simulation_tick, 1006);
    EXPECT_TRUE(features::is_shot_in_interval(shot_time, 1002.0f * TickClock64_300::k_dt, shot_time));
    EXPECT_FALSE(features::is_shot_in_interval(shot_time, shot_time, 1004.0f * TickClock64_300::k_dt));
#endif
}

TEST(E2E_Tier1, T1_R4_Backtrack300HzDedup_And_MapExitReset) {
    // F1.5, F1.17: 250 Hz / 300 Hz CreateMove calls must NOT flood backtrack_history with duplicate curtime.
    features::backtrack_history history;
    const sdk::player_snapshot e1 = MultiPlayerHitboxArena::make_enemy_snapshot(1, 0x101u, {300.0f, 0.0f, 0.0f});
    const sdk::player_snapshot e2 = MultiPlayerHitboxArena::make_enemy_snapshot(2, 0x102u, {500.0f, 50.0f, 0.0f});

    TickClock64_300 clock;
    clock.run_ticks(4, 4,
        [&](int /*cmd*/, int /*tick*/, float curtime, int /*sf*/, int /*rf*/) {
            history.push(1, e1, curtime);
            history.push(2, e2, curtime);
        },
        [](int, int, float) {});

    // 4 server ticks => exactly 4 deduplicated records per player, NOT 16!
    EXPECT_EQ(history.record_count(1), 4);
    EXPECT_EQ(history.record_count(2), 4);

    // Map exit (!in_game) resets history cleanly.
    const bool in_game = false;
    if (!in_game) history.reset();
    EXPECT_EQ(history.record_count(1), 0);
    EXPECT_EQ(history.record_count(2), 0);
}

TEST(E2E_Tier1, T1_R5_ScoutAutoStopHoldOnFireTick_And_CycleCursor) {
    // F1.8, F1.15: AutoStop keeps braking engaged on firing tick (shot_qualified == true); cycle_cursor advances on fire only.
    CS2PhysicsState phys{};
    phys.vel = {220.0f, 0.0f, 0.0f};
    phys.on_ground = true;

    for (int t = 0; t < 6; ++t) {
        features::autostop_input in{};
        in.enabled = true;
        in.flags = config::auto_stop_flag::early | config::auto_stop_flag::between_shots;
        in.on_ground = phys.on_ground;
        in.has_velocity = true;
        in.velocity = phys.vel;
        in.speed_2d = CS2PhysicsEngine::speed_2d(phys);
        in.cmd_yaw = 0.0f;
        in.has_target = true;
        in.weapon_ready = true;
        in.shot_qualified = false;
        const auto dec = features::compute_autostop(in);
        EXPECT_TRUE(dec.active);
        CS2PhysicsEngine::step_tick(phys, 0.0f, dec.forwardmove, dec.leftmove, false);
    }
    EXPECT_LT(CS2PhysicsEngine::speed_2d(phys), 80.0f);

    // Now on the firing tick itself (shot_qualified == true) with speed_2d = 80 > auto_stop_speed (60),
    // AutoStop MUST remain engaged!
    features::autostop_input fire_in{};
    fire_in.enabled = true;
    fire_in.flags = config::auto_stop_flag::early | config::auto_stop_flag::between_shots;
    fire_in.on_ground = true;
    fire_in.has_velocity = true;
    fire_in.velocity = {80.0f, 0.0f, 0.0f};
    fire_in.speed_2d = 80.0f;
    fire_in.cmd_yaw = 0.0f;
    fire_in.has_target = true;
    fire_in.weapon_ready = true;
    fire_in.shot_qualified = true;
    const auto fire_dec = features::compute_autostop(fire_in);
    EXPECT_TRUE(fire_dec.active);
    EXPECT_LT(fire_dec.forwardmove, -0.5f);

#if SHOGOD_E2E_ENABLE_M3_CONTRACTS
    fire_in.auto_stop_speed = 60.0f;
    fire_in.auto_stop_ticks = 6;
    fire_in.auto_stop_wait = true;
    const auto m3_high_dec = features::compute_autostop(fire_in);
    EXPECT_TRUE(m3_high_dec.active);
    EXPECT_LT(m3_high_dec.forwardmove, -0.5f);

    // Separately verify threshold semantics when speed_2d (55) <= auto_stop_speed (60):
    // either already qualified/zero-move or counter-brakes without forward acceleration.
    features::autostop_input low_in = fire_in;
    low_in.velocity = {55.0f, 0.0f, 0.0f};
    low_in.speed_2d = 55.0f;
    const auto m3_low_dec = features::compute_autostop(low_in);
    EXPECT_TRUE(std::isfinite(m3_low_dec.forwardmove));
    EXPECT_LE(m3_low_dec.forwardmove, 0.0f);
#endif

    // F1.15: cycle_cursor advances only when firing.
    std::array<features::target_candidate, 2> cands{{
        {0, 5.0f, 300.0f, 100, 0.8f, true, true},
        {1, 10.0f, 400.0f, 100, 0.7f, true, true},
    }};
    int cursor = 0;
    const int chosen = features::pick_target(cands, config::aim_priority::cycle, 180.0f, true, cursor);
    EXPECT_EQ(chosen, 0);
    EXPECT_EQ(cursor, 1);
}

TEST(E2E_Tier1, T1_R6_RemoveRecoil_UnscopedScoutGate_HelmetAndShotgunDamage) {
    // F1.9, F1.13, F1.14: remove_recoil gate, Scout unscoped hc>=73, helmet & shotgun damage.
    config::store cfg_store = DefaultJsonConfigFixture::load();
    EXPECT_TRUE(cfg_store.get().rage.remove_recoil);

    const math::angles aim{2.5f, 45.0f};
    const math::angles aim_punch{3.0f, -1.5f};
    auto apply_recoil = [&](bool remove_recoil) -> math::angles {
        if (!remove_recoil) return aim;
        return {math::clamp_pitch(aim.pitch - aim_punch.pitch * 2.0f),
                math::normalize_yaw(aim.yaw - aim_punch.yaw * 2.0f)};
    };
    const math::angles compensated = apply_recoil(cfg_store.get().rage.remove_recoil);
    EXPECT_FLOAT_EQ(compensated.pitch, -3.5f);
    EXPECT_FLOAT_EQ(compensated.yaw, 48.0f);
    cfg_store.get().rage.remove_recoil = false;
    const math::angles uncompensated = apply_recoil(cfg_store.get().rage.remove_recoil);
    EXPECT_FLOAT_EQ(uncompensated.pitch, 2.5f);
    EXPECT_FLOAT_EQ(uncompensated.yaw, 45.0f);

    // F1.13: Scout unscoped with hc=76.2 >= 73 qualifies for fire without waiting for scope.
    const config::rage_weapon& scout = cfg_store.get().rage.weapons[static_cast<std::size_t>(config::weapon_group::scout)];
    EXPECT_TRUE(features::should_fire(76.2f, 88.0f, scout));

    // F1.14: Helmet vs no-helmet headshot damage & shotgun pellet multiplier.
    const float hs_helmet = features::estimate_hit_damage(36.0f, 500.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::head);
    const float hs_no_helmet = features::estimate_hit_damage(36.0f, 500.0f, 0.98f, 4.0f, 1.55f, 100, false, math::hitgroup::head);
    EXPECT_GT(hs_no_helmet, hs_helmet);
    const float shotgun_6_pellets =
        6.0f * features::estimate_hit_damage(20.0f, 250.0f, 0.70f, 4.0f, 1.30f, 100, true, math::hitgroup::chest);
    EXPECT_GT(shotgun_6_pellets, 50.0f);
}

TEST(E2E_Tier1, T1_AA1_DefaultJsonFakeStaticAtTargets45Deg) {
    // F2.4: default.json AA (base_pitch=down, base_yaw=fake_static, at_targets=true, yaw_offset=0)
    config::store cfg_store = DefaultJsonConfigFixture::load();
    config::anti_aim_cfg aa = cfg_store.get().anti_aim;
    aa.enabled = true;
    EXPECT_EQ(aa.base_pitch, config::pitch_mode::down);
    EXPECT_EQ(aa.base_yaw, config::yaw_mode::fake_static);
    EXPECT_TRUE(aa.at_targets);

    const float enemy_yaw = 45.0f;
    const float trim = features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 89.0f);
    features::antiaim_state st{};
    st.on_ground = true;
    st.moving = false;
    st.tick = 100;
    const math::angles ref{89.0f, enemy_yaw};
    const math::angles out = features::apply_antiaim(aa, ref, st);
    EXPECT_FLOAT_EQ(out.pitch, 89.0f);
    EXPECT_NEAR(math::yaw_delta(math::normalize_yaw(enemy_yaw + 180.0f), out.yaw), 0.0f, 0.01f);
    EXPECT_FLOAT_EQ(trim, 36.0f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    features::antiaim_context ctx{};
    ctx.command_number = 500;
    ctx.found_enemy = true;
    ctx.enemy_yaw = enemy_yaw;
    ctx.on_ground = true;
    ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
    const math::angles full_out = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx);
    EXPECT_FLOAT_EQ(full_out.pitch, 89.0f);
    EXPECT_NEAR(math::yaw_delta(math::normalize_yaw(enemy_yaw + 180.0f + trim), full_out.yaw), 0.0f, 0.1f);
#endif
}

TEST(E2E_Tier1, T1_AA2_BaseYawOffWithAtTargetsHasNoSecret180Flip) {
    // F2.2, F2.4: base_yaw=off + at_targets=true + freestanding=false -> yaw == enemy_yaw + yaw_offset (NO +180!).
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_yaw = config::yaw_mode::off;
    aa.at_targets = true;
    aa.freestanding = false;
    aa.auto_adjust = false;
    aa.yaw_offset = 15.0f;

    features::antiaim_state st{};
    st.tick = 10;
    const math::angles out = features::apply_antiaim(aa, {0.0f, 45.0f}, st);
    EXPECT_NEAR(math::yaw_delta(60.0f, out.yaw), 0.0f, 0.01f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    features::antiaim_context ctx{};
    ctx.command_number = 501;
    ctx.found_enemy = true;
    ctx.enemy_yaw = 45.0f;
    const math::angles full_out = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx);
    EXPECT_NEAR(math::yaw_delta(60.0f, full_out.yaw), 0.0f, 0.01f);
#endif
}

TEST(E2E_Tier1, T1_AA3_JitterDeterministicOnCommandNumber300HzInvariant) {
    // F2.4, F2.6: Jitter must be strictly keyed on command_number, invariant to render-rate subframes.
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_yaw = config::yaw_mode::jitter;
    aa.jitter_range = 30.0f;
    aa.at_targets = false;
    aa.auto_adjust = false;

    std::vector<float> cmd500_yaws;
    std::vector<float> cmd501_yaws;
    for (int sf = 0; sf < 5; ++sf) {
#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
        features::antiaim_context c0{};
        c0.command_number = 500;
        c0.seed = sf;
        cmd500_yaws.push_back(features::apply_antiaim_full(aa, {0.0f, 90.0f}, c0).yaw);

        features::antiaim_context c1{};
        c1.command_number = 501;
        c1.seed = sf;
        cmd501_yaws.push_back(features::apply_antiaim_full(aa, {0.0f, 90.0f}, c1).yaw);
#else
        features::antiaim_state s0{};
        s0.tick = 500;
        cmd500_yaws.push_back(features::apply_antiaim(aa, {0.0f, 90.0f}, s0).yaw);

        features::antiaim_state s1{};
        s1.tick = 501;
        cmd501_yaws.push_back(features::apply_antiaim(aa, {0.0f, 90.0f}, s1).yaw);
#endif
    }

    for (float y : cmd500_yaws) EXPECT_FLOAT_EQ(y, cmd500_yaws.front());
    for (float y : cmd501_yaws) EXPECT_FLOAT_EQ(y, cmd501_yaws.front());
    EXPECT_NEAR(std::fabs(math::yaw_delta(cmd500_yaws.front(), cmd501_yaws.front())), 60.0f, 0.01f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    // yaw_mode::random is also deterministic for a fixed (command_number, seed) pair.
    aa.base_yaw = config::yaw_mode::random;
    features::antiaim_context rc{};
    rc.command_number = 500;
    rc.seed = 42;
    const float r1 = features::apply_antiaim_full(aa, {0.0f, 90.0f}, rc).yaw;
    const float r2 = features::apply_antiaim_full(aa, {0.0f, 90.0f}, rc).yaw;
    EXPECT_FLOAT_EQ(r1, r2);
#endif
}

TEST(E2E_Tier1, T1_AA4_FreestandingCornerTuckIndependentOfAtTargets) {
    // F2.2, F2.3: Cover wall blocks +115 deg probe while center is exposed; freestanding tucks head
    // without forcing enemy_yaw + 180 when at_targets == false.
    const std::array<CoverAndPoseGeometry::WallAABB, 1> walls{{
        {{-60.0f, 15.0f, 0.0f}, {20.0f, 45.0f, 80.0f}},
    }};
    const math::vec3 eye{0.0f, 0.0f, 64.0f};
    const math::vec3 probe_center = eye + math::forward_from_angles({0.0f, 0.0f}) * 42.0f;
    const math::vec3 probe_left = eye + math::forward_from_angles({0.0f, 115.0f}) * 42.0f;
    const math::vec3 probe_right = eye + math::forward_from_angles({0.0f, -115.0f}) * 42.0f;

    const float frac_center = CoverAndPoseGeometry::trace_walls(eye, probe_center, walls);
    const float frac_left = CoverAndPoseGeometry::trace_walls(eye, probe_left, walls);
    const float frac_right = CoverAndPoseGeometry::trace_walls(eye, probe_right, walls);
    EXPECT_GT(frac_center, 0.96f);
    EXPECT_LT(frac_left, 0.96f);
    EXPECT_GT(frac_right, 0.96f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.freestanding = true;
    aa.at_targets = false;
    aa.auto_adjust = false;
    aa.base_yaw = config::yaw_mode::fake_static;

    features::freestanding_state fs_state{};
    std::array<features::freestanding_candidate, 2> cands{{
        {115.0f, frac_left, true},
        {-115.0f, frac_right, true},
    }};
    const auto decision = features::select_freestanding_yaw(90.0f, true, frac_center, cands, fs_state);
    EXPECT_TRUE(decision.tucked);

    features::antiaim_context ctx{};
    ctx.command_number = 510;
    ctx.found_enemy = true;
    ctx.enemy_yaw = 90.0f;
    ctx.freestanding = decision;
    const math::angles out = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx);
    EXPECT_NEAR(math::yaw_delta(decision.selected_yaw, out.yaw), 0.0f, 0.5f);
    EXPECT_NE(std::fabs(math::yaw_delta(math::normalize_yaw(90.0f + 180.0f), out.yaw)), 0.0f);
#endif
}

TEST(E2E_Tier1, T1_AA5_AutoAdjustWeaponAndPitchCalibration) {
    // F2.5: auto_adjust calibrated for pitch down (+89) vs up (-89), rifle vs pistol vs knife, zero wobble.
    const float rifle_down = features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 89.0f);
    const float rifle_up = features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), -89.0f);
    const float pistol_down = features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::pistol), 89.0f);
    const float unpitched = features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 10.0f);
    EXPECT_FLOAT_EQ(unpitched, 0.0f);
    EXPECT_FLOAT_EQ(rifle_up, 0.0f);  // Neutral/pitch-up stances have zero pitch-down spine twist.
    EXPECT_GT(rifle_down, pistol_down);
    EXPECT_GT(pistol_down, 0.0f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    const float knife_down = features::pose_head_yaw_offset(static_cast<int>(config::weapon_group::pistol), 0, 89.0f);
    EXPECT_FLOAT_EQ(knife_down, 0.0f);

    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_pitch = config::pitch_mode::down;
    aa.auto_adjust = true;
    aa.at_targets = false;

    for (config::yaw_mode mode : {config::yaw_mode::fake_static, config::yaw_mode::jitter, config::yaw_mode::random}) {
        aa.base_yaw = mode;
        features::antiaim_context ctx{};
        ctx.command_number = 520;
        ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
        const math::angles with_adj = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx);
        aa.auto_adjust = false;
        const math::angles without_adj = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx);
        aa.auto_adjust = true;
        EXPECT_NEAR(math::yaw_delta(without_adj.yaw, with_adj.yaw), rifle_down, 0.5f);
    }
#endif
}

TEST(E2E_Tier1, T1_AA6_No45DegAASnap_QuantizedGroundMove_And_AABind) {
    // F2.1, F2.8: Arbitrary aa_angles.yaw = 137 deg must NOT snap to 45 deg; quantized ground move tracks wish dir.
    const float arbitrary_aa_yaw = 137.0f;
    EXPECT_NE(std::fmod(std::fabs(arbitrary_aa_yaw), 45.0f), 0.0f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    features::quantized_move_state qstate{};
    CS2PhysicsState phys{};
    phys.on_ground = true;
    const math::vec3 wish = CS2PhysicsEngine::wish_dir_from_cmd(0.0f, 1.0f, 1.0f);  // W+A
    for (int t = 0; t < 32; ++t) {
        float qf = 0.0f, ql = 0.0f;
        features::compute_quantized_ground_move(wish, arbitrary_aa_yaw, qstate, 600 + t, qf, ql);
        CS2PhysicsEngine::step_tick(phys, arbitrary_aa_yaw, qf, ql, false);
    }
    const float vel_yaw = std::atan2(phys.vel.y, phys.vel.x) * k_rad_to_deg;
    EXPECT_NEAR(math::yaw_delta(45.0f, vel_yaw), 0.0f, 8.0f);
    EXPECT_GT(CS2PhysicsEngine::speed_2d(phys), 200.0f);

    config::bind_state aa_bind;
    const config::keybind toggle_kb{0x5A, false, false};  // 'Z'
    EXPECT_FALSE(aa_bind.sample(toggle_kb, true, true, false));
    EXPECT_TRUE(aa_bind.sample(toggle_kb, true, true, true));
    EXPECT_TRUE(aa_bind.sample(toggle_kb, true, true, false));
    EXPECT_FALSE(aa_bind.sample(toggle_kb, true, true, true));
    EXPECT_NE(config::feature_bit(config::bound_feature::anti_aim), 0u);
#else
    // Even before M2 lands, bind_state works on arbitrary keybind structs.
    config::bind_state aa_bind;
    const config::keybind toggle_kb{0x5A, false, false};
    EXPECT_FALSE(aa_bind.sample(toggle_kb, true, true, false));
    EXPECT_TRUE(aa_bind.sample(toggle_kb, true, true, true));
    EXPECT_TRUE(aa_bind.sample(toggle_kb, true, true, false));
    EXPECT_FALSE(aa_bind.sample(toggle_kb, true, true, true));
#endif
}

TEST(E2E_Tier1, T1_M1_AirborneStraightW_AutoStrafeAbove300UnderAA) {
    // F3.1, F3.2: Airborne straight W with fake_static AA (180 deg) must accelerate > 300 u/s with <= 22.5 deg yaw delta.
    // M3 Contract (PROJECT.md § Interface Contracts):
    //   autostrafe_output features::compute_sector_matched_autostrafe(
    //       float camera_yaw, float ref_yaw, const math::vec3& velocity,
    //       float forward_wish, float left_wish, int strafe_sign, int command_number) noexcept;
    //   where autostrafe_output::steer_yaw holds the steered cmd_yaw (|yaw_delta(ref_yaw, steer_yaw)| <= 22.5 deg).
    const float cam_yaw = 0.0f;
    const float aa_yaw = 180.0f;

#if !SHOGOD_E2E_ENABLE_M3_CONTRACTS
    CS2PhysicsState phys{};
    phys.pos = {0.0f, 0.0f, 128.0f};
    phys.vel = {250.0f, 0.0f, 0.0f};
    phys.on_ground = false;

    for (int t = 0; t < 32; ++t) {
        const auto q = features::compute_quantized_autostrafe(cam_yaw, aa_yaw, phys.vel, 1.0f, 0.0f);
        ASSERT_TRUE(q.active);
        CS2PhysicsEngine::step_tick(phys, aa_yaw, q.forwardmove, q.leftmove, false);
        phys.pos.z = 128.0f;
        phys.vel.z = 0.0f;
        phys.on_ground = false;
    }
    EXPECT_GT(CS2PhysicsEngine::speed_2d(phys), 300.0f);
#else
    CS2PhysicsState m3_phys{};
    m3_phys.pos = {0.0f, 0.0f, 128.0f};
    m3_phys.vel = {250.0f, 0.0f, 0.0f};
    m3_phys.on_ground = false;
    int sign = 1;
    for (int t = 0; t < 32; ++t) {
        const auto s = features::compute_sector_matched_autostrafe(
            cam_yaw, aa_yaw, m3_phys.vel, 1.0f, 0.0f, sign, 700 + t);
        ASSERT_TRUE(s.active);
        EXPECT_LE(std::fabs(math::yaw_delta(aa_yaw, s.steer_yaw)), 22.5f);
        CS2PhysicsEngine::step_tick(m3_phys, s.steer_yaw, s.forwardmove, s.leftmove, false);
        m3_phys.pos.z = 128.0f;
        m3_phys.vel.z = 0.0f;
        m3_phys.on_ground = false;
        sign = s.next_strafe_sign;
    }
    EXPECT_GT(CS2PhysicsEngine::speed_2d(m3_phys), 300.0f);
#endif
}

TEST(E2E_Tier1, T1_M2_AirborneDirectionalStrafe_A_D_S) {
    // F3.1: Directional air strafing with A, D, S keys turns velocity smoothly into 90, -90, 180 deg.
    struct DirCase {
        float fwd;
        float left;
        float expected_yaw;
    };
    const std::array<DirCase, 3> cases{{
        {0.0f, 1.0f, 90.0f},
        {0.0f, -1.0f, -90.0f},
        {-1.0f, 0.0f, 180.0f},
    }};
    for (const auto& c : cases) {
        CS2PhysicsState phys{};
        phys.pos = {0.0f, 0.0f, 128.0f};
        phys.vel = {250.0f, 0.0f, 0.0f};
        phys.on_ground = false;

        int sign = 1;
        for (int t = 0; t < 48; ++t) {
#if SHOGOD_E2E_ENABLE_M3_CONTRACTS
            const auto s = features::compute_sector_matched_autostrafe(
                0.0f, 0.0f, phys.vel, c.fwd, c.left, sign, 750 + t);
            ASSERT_TRUE(s.active);
            EXPECT_LE(std::fabs(math::yaw_delta(0.0f, s.steer_yaw)), 22.5f);
            sign = s.next_strafe_sign;
            CS2PhysicsEngine::step_tick(phys, s.steer_yaw, s.forwardmove, s.leftmove, false);
#else
            const auto s = features::compute_autostrafe({0.0f, phys.vel, c.fwd, c.left, sign});
            ASSERT_TRUE(s.active);
            sign = s.next_strafe_sign;
            CS2PhysicsEngine::step_tick(phys, 0.0f, s.forwardmove, s.leftmove, false);
#endif
            phys.pos.z = 128.0f;
            phys.vel.z = 0.0f;
            phys.on_ground = false;
        }
        const float vel_yaw = std::atan2(phys.vel.y, phys.vel.x) * k_rad_to_deg;
        EXPECT_LT(std::fabs(math::yaw_delta(vel_yaw, c.expected_yaw)), 60.0f);
        EXPECT_GT(CS2PhysicsEngine::speed_2d(phys), 150.0f);
    }
}

TEST(E2E_Tier1, T1_M3_Bhop250HzLatch_And_EdgeJumpYields) {
    // Proposal D1, F3.4: step_bhop idempotent across 4 subframes/tick; presses on land, releases in air;
    // handles consecutive ground ticks and jump_held release cleanly for edge_jump arbitration.
    features::bhop_state bs{};
    TickClock64_300 clock;
    CS2PhysicsState phys{};
    phys.on_ground = true;

    int jump_presses_on_ground = 0;
    int air_jump_bits = 0;
    int subframe_mismatch = 0;

    clock.run_ticks(20, 4,
        [&](int cmd_num, int /*tick*/, float /*curtime*/, int sf, int /*rf*/) {
            const bool first_jump = (sf == 0) ? features::step_bhop(bs, cmd_num, phys.on_ground, true)
                                              : bs.cmd_jump;
            const bool want_jump = features::step_bhop(bs, cmd_num, phys.on_ground, true);
            if (sf > 0 && want_jump != first_jump) ++subframe_mismatch;
            if (!phys.on_ground && want_jump) ++air_jump_bits;
        },
        [&](int /*cmd_num*/, int /*tick*/, float /*curtime*/) {
            if (phys.on_ground && bs.cmd_jump) ++jump_presses_on_ground;
            CS2PhysicsEngine::step_tick(phys, 0.0f, 1.0f, 0.0f, bs.cmd_jump);
        });

    EXPECT_EQ(subframe_mismatch, 0);
    EXPECT_GE(jump_presses_on_ground, 1);
    EXPECT_EQ(air_jump_bits, 0);

    // Consecutive ground ticks alternate press<->release once per command_number,
    // and releasing jump_held immediately clears bhop output for clean edge_jump handoff.
    features::bhop_state arb{};
    EXPECT_TRUE(features::step_bhop(arb, 100, true, true));
    EXPECT_TRUE(features::step_bhop(arb, 100, true, true));   // Same command_number: idempotent
    EXPECT_FALSE(features::step_bhop(arb, 101, true, true));  // Next ground tick: release edge
    EXPECT_TRUE(features::step_bhop(arb, 102, true, true));   // Next ground tick: fresh press
    EXPECT_FALSE(features::step_bhop(arb, 103, true, false)); // User released jump: clear immediately
    EXPECT_FALSE(arb.cmd_jump);
    EXPECT_FALSE(arb.release_next);
}

TEST(E2E_Tier1, T1_M4_MoveRotation_WA_To_SD_And_AirFirePreservesStrafe) {
    // F3.5, F3.8: camera_yaw=10 -> aa_yaw=-160 with W+A (0x208) rotates to S+D (0x410),
    // and firing in air rotates strafe input into fire_yaw without dropping air speed.
    // M3 Contract (PROJECT.md):
    //   void features::rotate_move_inputs(float fwd, float left, float from_yaw, float to_yaw,
    //                                     float& out_fwd, float& out_left) noexcept;
    float out_f = 0.0f;
    float out_l = 0.0f;
    UserCmdHarness::rotate_move_harness(1.0f, 1.0f, 10.0f, -160.0f, out_f, out_l);
    EXPECT_LT(out_f, -0.5f);
    EXPECT_LT(out_l, -0.5f);

    const std::uint64_t synced = UserCmdHarness::sync_move_buttons(0, out_f, out_l);
    EXPECT_EQ(synced, sdk::k_in_back | sdk::k_in_moveright);  // 0x410

    // Airborne fire tick: compute strafe, rotate from cam_yaw (10) -> fire_yaw (100), strip subticks, step air physics.
    CS2PhysicsState air_phys{};
    air_phys.pos = {0.0f, 0.0f, 128.0f};
    air_phys.vel = {260.0f, 40.0f, 0.0f};
    air_phys.on_ground = false;
    const float pre_speed = CS2PhysicsEngine::speed_2d(air_phys);

    const auto strafe = features::compute_autostrafe({10.0f, air_phys.vel, 1.0f, 0.0f, 1});
    ASSERT_TRUE(strafe.active);
    const float fire_yaw = 100.0f;
    float fire_f = 0.0f, fire_l = 0.0f;
#if SHOGOD_E2E_ENABLE_M3_CONTRACTS
    features::rotate_move_inputs(strafe.forwardmove, strafe.leftmove, 10.0f, fire_yaw, fire_f, fire_l);
#else
    UserCmdHarness::rotate_move_harness(strafe.forwardmove, strafe.leftmove, 10.0f, fire_yaw, fire_f, fire_l);
#endif
    UserCmdHarness h;
    h.begin_tick(305, 1105, {0.0f, 10.0f}, sdk::k_in_attack, fire_f, fire_l);
    h.base_va.set(0.0f, fire_yaw);
    h.cmd.button_state.held = UserCmdHarness::sync_move_buttons(h.cmd.button_state.held, fire_f, fire_l);
    h.base.clear_subtick_moves();
    EXPECT_TRUE(h.verify_protobuf_hygiene());
    EXPECT_NE(h.cmd.button_state.held & sdk::k_in_attack, 0ULL);

    CS2PhysicsEngine::step_tick(air_phys, fire_yaw, fire_f, fire_l, false);
    EXPECT_GE(CS2PhysicsEngine::speed_2d(air_phys), pre_speed - 0.5f);

#if SHOGOD_E2E_ENABLE_M3_CONTRACTS
    float m3_f = 0.0f, m3_l = 0.0f;
    features::rotate_move_inputs(1.0f, 1.0f, 10.0f, -160.0f, m3_f, m3_l);
    EXPECT_LT(m3_f, -0.5f);
    EXPECT_LT(m3_l, -0.5f);
#endif
}

TEST(E2E_Tier1, T1_M5_SubtickPoolNullGuard_And_HasBits0x60Hygiene) {
    // F3.3, F2.10: preallocated pool with null slot + clearing 0x60 bits when zeroing view deltas.
    UserCmdHarness harness;
    harness.begin_tick(101, 1000, {0.0f, 0.0f}, sdk::k_in_forward, 1.0f, 0.0f);

    // Simulate capacity=4, current_size=2, elements[2]==nullptr (F3.3).
    harness.base.subtick_moves.capacity = 4;
    harness.base.subtick_moves.current_size = 2;
    harness.subtick_rep.elements[2] = nullptr;
    EXPECT_EQ(safe_add_subtick_step(harness.base.subtick_moves, sdk::k_in_attack, true, 0.5f), nullptr);

    // Restore slot and test F2.10 0x60 has_bits clearing.
    harness.subtick_rep.elements[2] = &harness.subtick_pool[2];
    auto* st = safe_add_subtick_step(harness.base.subtick_moves, sdk::k_in_attack, true, 0.5f);
    ASSERT_NE(st, nullptr);
    st->set_pitch_delta(1.25f);
    st->set_yaw_delta(-2.5f);
    EXPECT_EQ(st->has_bits & 0x60u, 0x60u);

    safe_zero_subtick_view_deltas(harness.base);
    EXPECT_EQ(st->has_bits & 0x60u, 0u);
    EXPECT_FLOAT_EQ(st->pitch_delta, 0.0f);
    EXPECT_FLOAT_EQ(st->yaw_delta, 0.0f);
    EXPECT_TRUE(harness.verify_protobuf_hygiene());
}

TEST(E2E_Tier1, T1_M6_DefaultJsonRoundtrip_BindHold_And_MenuReset) {
    // F3.6, F3.7: default.json roundtrip preserves penetration_limit & overrides; hold mode & focus reset.
    config::store s = DefaultJsonConfigFixture::load();
    EXPECT_EQ(s.clamps_applied(), 0);
    EXPECT_TRUE(s.get().rage.enabled);
    EXPECT_EQ(s.get().rage.priority, config::aim_priority::threat);
    EXPECT_FLOAT_EQ(s.get().rage.extrapolation_ms, 50.0f);
    EXPECT_FLOAT_EQ(s.get().rage.backtrack_ms, 200.0f);
    EXPECT_TRUE(s.get().misc.movement.bhop);
    EXPECT_TRUE(s.get().misc.movement.auto_strafe);

    const std::string saved = s.to_json();
    EXPECT_NE(saved.find("penetration_limit"), std::string::npos);
    EXPECT_NE(saved.find("arms_chams"), std::string::npos);
    EXPECT_NE(saved.find("box_color"), std::string::npos);

    config::store reloaded;
    ASSERT_TRUE(reloaded.from_json(saved));
    EXPECT_FLOAT_EQ(reloaded.get().rage.weapons[4].safe_point,
                    s.get().rage.weapons[4].safe_point);

    config::bind_state hold_bind;
    const config::keybind kb{0x46, true, false};
    EXPECT_FALSE(hold_bind.sample(kb, true, true, false));
    EXPECT_TRUE(hold_bind.sample(kb, true, true, true));
    EXPECT_TRUE(hold_bind.sample(kb, true, true, true));
    EXPECT_FALSE(hold_bind.sample(kb, true, false, true));  // Focus loss disarms hold immediately.
}

// =============================================================================
// TIER 2 — Boundary & Corner-Case Tests (18 cases)
// =============================================================================

TEST(E2E_Tier2, T2_R1_SvMaxUnlag200msBoundary) {
    // F1.6, F1.3, F1.4: 199.9ms vs 200.1ms lagcomp window check including ping_ms + interp_ms.
    MultiPlayerHitboxArena::LagTrackSim track;
    const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes({400.0f, 0.0f, 0.0f});
    // newest = 1013:
    // - tick 1002 is 11 ticks = 171.875ms old (+ 43.65ms latency = 215.525ms > 200ms -> rejected)
    // - tick 1003 is 10 ticks = 156.250ms old (+ 43.65ms latency = 199.900ms <= 200ms -> accepted;
    //   + 43.85ms latency = 200.100ms > 200ms -> rejected)
    track.push(0x99u, 1002, 1002 * TickClock64_300::k_dt, 0.0f, {400.0f, 0.0f, 0.0f}, hbs);
    track.push(0x99u, 1003, 1003 * TickClock64_300::k_dt, 0.0f, {402.0f, 0.0f, 0.0f}, hbs);
    track.push(0x99u, 1013, 1013 * TickClock64_300::k_dt, 0.0f, {410.0f, 0.0f, 0.0f}, hbs);

    const auto sel_ok = track.select(1013, 200.0f, 28.05f, 15.60f);
    ASSERT_EQ(sel_ok.size(), 2u);
    EXPECT_EQ(sel_ok[0]->simulation_tick, 1013);
    EXPECT_EQ(sel_ok[1]->simulation_tick, 1003);

    const auto sel_over = track.select(1013, 200.0f, 28.25f, 15.60f);
    ASSERT_EQ(sel_over.size(), 1u);
    EXPECT_EQ(sel_over[0]->simulation_tick, 1013);

#if SHOGOD_E2E_ENABLE_M1_CONTRACTS
    // Production inline lag_compensation::select window check (12 ticks = 187.5ms <= 200ms).
    features::lag_compensation lc;
    lc.reset();
    constexpr std::uintptr_t k_pawn = 0x300000ULL;
    for (int t : {1000, 1001, 1013}) {
        sdk::live_hitbox_set hb_set{};
        hb_set.geometries = hbs;
        hb_set.simulation_tick = t;
        hb_set.simulation_time = static_cast<float>(t) * TickClock64_300::k_dt;
        hb_set.valid = true;
        ASSERT_TRUE(lc.push_record(2, k_pawn, 0x99u, t, hb_set.simulation_time,
                                   {400.0f, 0.0f, 0.0f}, std::move(hb_set), 0.0f, false));
    }
    std::vector<const features::lag_record*> lc_sel;
    lc.select(k_pawn, 0x99u, 1013, 200.0f, lc_sel);
    ASSERT_EQ(lc_sel.size(), 2u);
    EXPECT_EQ(lc_sel[0]->simulation_tick, 1013);
    EXPECT_EQ(lc_sel[1]->simulation_tick, 1001);

    lc.select(k_pawn, 0x99u, 1013, 0.0f, lc_sel);
    ASSERT_EQ(lc_sel.size(), 1u);
    EXPECT_EQ(lc_sel[0]->simulation_tick, 1013);
#endif

    // Backtrack_history boundary: 199.9ms accepted, 200.1ms rejected.
    features::backtrack_history bh;
    const auto snap = MultiPlayerHitboxArena::make_enemy_snapshot(1, 0x99u, {400.0f, 0.0f, 0.0f});
    bh.push(1, snap, 10.000f);
    EXPECT_TRUE(bh.best(1, 0x99u, 200.0f, 10.1999f).has_value());
    EXPECT_FALSE(bh.best(1, 0x99u, 200.0f, 10.2001f).has_value());
}

TEST(E2E_Tier2, T2_R2_SafePointBoundaries_And_StrictChanceMulti) {
    // F1.7, F1.2: safe_point 0%, 15.909% (Scout), 50% (AK), 100% (center-only).
    const math::vec3 bone{300.0f, 0.0f, 64.0f};
    const math::vec3 dir{0.0f, 1.0f, 0.0f};
    const float r = 4.0f;

    EXPECT_FLOAT_EQ(math::safe_point(bone, dir, r, 0.0f).y, 0.0f);
    EXPECT_NEAR(math::safe_point(bone, dir, r, 0.1590909f).y, r * 0.1590909f, 1e-4f);
    EXPECT_FLOAT_EQ(math::safe_point(bone, dir, r, 0.50f).y, 2.0f);
    EXPECT_FLOAT_EQ(math::safe_point(bone, dir, r, 1.00f).y, 4.0f);

    // Center-only safe_point=100% reduces effective aim scatter vs edge multipoint.
    const math::vec3 eye{0.0f, 0.0f, 64.0f};
    const math::weapon_spread_pattern pat{0.003f, 0.01f};
    const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes({300.0f, 0.0f, 0.0f});
    const float hc_center = pat.chance_multi(eye, hbs[0].center, hbs, 0);
    const float hc_edge = pat.chance_multi(eye, hbs[0].center + math::vec3{0.0f, 3.8f, 0.0f}, hbs, 0);
    EXPECT_GT(hc_center, hc_edge);

#if SHOGOD_E2E_ENABLE_M1_CONTRACTS
    const float scale_100_strict = math::safe_point_hitbox_scale(100.0f, true);
    const float scale_0 = math::safe_point_hitbox_scale(0.0f, false);
    EXPECT_LT(scale_100_strict, scale_0);
    const float hc_strict = pat.chance_multi(eye, hbs[0].center, hbs, 0, true, scale_100_strict);
    EXPECT_LE(hc_strict, hc_center);
#endif
}

TEST(E2E_Tier2, T2_R3_WeaponRangeGuard_And_MinDamageExtremes) {
    // F1.14, F1.1, F1.16: min_damage clamping (1..120, overkill 101..120), damage estimation boundaries,
    // should_fire thresholds, and Zeus catalog range/group resolution.
    config::store s;
    ASSERT_TRUE(s.from_json(R"({
        "version": 1,
        "rage": {
            "weapons": [
                {"min_damage": 0.0, "hitchance": 60.0},
                {"min_damage": 105.0, "hitchance": 60.0},
                {"min_damage": 120.0, "hitchance": 60.0},
                {"min_damage": 125.0, "hitchance": 60.0},
                {"min_damage": 37.0, "hitchance": 73.0},
                {"min_damage": 37.0, "hitchance": 73.0},
                {"min_damage": 37.0, "hitchance": 73.0},
                {"min_damage": 25.0, "hitchance": 50.0}
            ]
        }
    })"));
    EXPECT_FLOAT_EQ(s.get().rage.weapons[0].min_damage, 1.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[1].min_damage, 105.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[2].min_damage, 120.0f);
    EXPECT_FLOAT_EQ(s.get().rage.weapons[3].min_damage, 120.0f);
    EXPECT_GE(s.clamps_applied(), 2);

    // Damage math boundaries: zero base / negative distance -> 0; leg 0.75x vs stomach 1.25x vs head 4x.
    EXPECT_FLOAT_EQ(features::estimate_hit_damage(0.0f, 100.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::head), 0.0f);
    EXPECT_FLOAT_EQ(features::estimate_hit_damage(36.0f, -1.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::head), 0.0f);
    const float leg_dmg = features::estimate_hit_damage(36.0f, 400.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::left_leg);
    const float stm_dmg = features::estimate_hit_damage(36.0f, 400.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::stomach);
    EXPECT_GT(stm_dmg, leg_dmg);

    const config::rage_weapon& w = s.get().rage.weapons[1];  // min_damage=105, hitchance=60
    EXPECT_FALSE(features::should_fire(80.0f, 104.99f, w));
    EXPECT_TRUE(features::should_fire(80.0f, 105.0f, w));
    EXPECT_FALSE(features::should_fire(59.99f, 120.0f, w));
    EXPECT_FALSE(features::should_fire(std::numeric_limits<float>::quiet_NaN(), 120.0f, w));

    const config::rage_weapon_spec* zeus = config::find_rage_weapon_by_def(31);
    ASSERT_NE(zeus, nullptr);
    EXPECT_EQ(zeus->group, config::weapon_group::pistol);
}

TEST(E2E_Tier2, T2_R4_ShotDelayNonConsecutiveTickGap_And_BothReset) {
    // F1.15: Scout delay_ticks=6 with packet-loss tick gap (100 -> 102) and delay_shot=both reset.
    config::rage_weapon scout{};
    scout.delay_shot = config::delay_shot_mode::both;
    scout.delay_ticks = 6;
    scout.hitchance = 73.0f;
    scout.min_damage = 37.0f;

    features::shot_delay sd{};
    const std::uintptr_t target = 0xDEADu;
    const std::uintptr_t wep = 0xBEEFu;

    EXPECT_FALSE(sd.ready(target, wep, 100, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 1);
    EXPECT_FALSE(sd.ready(target, wep, 101, scout, 80.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 2);

    // Simulate 1-tick packet drop (101 -> 103): must tolerate small gap (<= 2 ticks) without wiping progress.
    EXPECT_FALSE(sd.ready(target, wep, 103, scout, 80.0f, 90.0f));
    EXPECT_GE(sd.ticks, 3);

    // If condition fails on tick 104, resets cleanly.
    EXPECT_FALSE(sd.ready(target, wep, 104, scout, 50.0f, 90.0f));
    EXPECT_EQ(sd.ticks, 0);
}

TEST(E2E_Tier2, T2_R5_BoxOBBDegenerate_Rotated45_ParallelRay_And_InsideOrigin) {
    // F1.11, F1.10: Rotated 45 deg OBB, degenerate zero-extent, parallel ray, origin inside OBB.
    const math::mat3x4 rot45 = MultiPlayerHitboxArena::make_rot_z_tm({200.0f, 0.0f, 50.0f}, 45.0f);
    const math::hitbox_geometry obb = math::build_hitbox_geometry(
        {-16.0f, -4.0f, -4.0f}, {16.0f, 4.0f, 4.0f}, 0.0f, 0, 2, false, rot45, 1.0f);
    ASSERT_TRUE(obb.valid);

    // Point along rotated long axis (local (14, 0, 0) -> world (200 + 9.9, 9.9, 50)) must hit.
    const math::vec3 along_axis{209.9f, 9.9f, 50.0f};
    const math::vec3 eye{0.0f, 0.0f, 50.0f};
    EXPECT_TRUE(math::ray_hits_geometry(eye, (along_axis - eye).normalised(), obb));

    // Ray at y=16 passes inside bounding sphere (r=16.97) yet misses 45-deg rotated OBB (max y=14.142).
    EXPECT_FALSE(math::ray_hits_geometry({0.0f, 16.0f, 50.0f}, {1.0f, 0.0f, 0.0f}, obb));

    // Parallel ray outside box misses.
    EXPECT_FALSE(math::ray_hits_geometry({0.0f, 0.0f, 60.0f}, {1.0f, 0.0f, 0.0f}, obb));

    // Ray origin inside OBB hits.
    EXPECT_TRUE(math::ray_hits_geometry({200.0f, 0.0f, 50.0f}, {1.0f, 0.0f, 0.0f}, obb));

    // F1.11: OBB multipoint generation on 45-deg rotated box produces >= 5 valid aim points all hitting OBB.
    std::array<math::vec3, 9> obb_pts{};
    const std::size_t n_pts = math::generate_hitbox_points(obb, eye, 0.8f, obb_pts);
    ASSERT_GE(n_pts, 5u);
    for (std::size_t i = 0; i < n_pts; ++i) {
        EXPECT_TRUE(math::ray_hits_geometry_scaled(eye, (obb_pts[i] - eye).normalised(), obb, 1.0f));
    }

    // Sphere head (shape_type=1) + 90-deg yaw rotated 17-hitbox rig.
    const auto rot90_hbs = MultiPlayerHitboxArena::build_player_17_hitboxes(
        {200.0f, 50.0f, 0.0f}, /*yaw_deg=*/90.0f, /*head_shape_type=*/1);
    ASSERT_EQ(rot90_hbs.size(), 17u);
    EXPECT_EQ(rot90_hbs[0].shape, math::hitbox_shape::sphere);
    EXPECT_TRUE(rot90_hbs[0].valid);
    EXPECT_TRUE(math::ray_hits_geometry(eye, (rot90_hbs[0].center - eye).normalised(), rot90_hbs[0]));
    // At yaw=90 deg, local +Y left arm rotates to world -X (~189, 50, 48).
    EXPECT_LT(rot90_hbs[6].center.x, 195.0f);
    EXPECT_TRUE(math::ray_hits_geometry(eye, (rot90_hbs[6].center - eye).normalised(), rot90_hbs[6]));

    // Degenerate zero-extent box is marked invalid.
    const math::hitbox_geometry deg = math::build_hitbox_geometry(
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0, 2, false, rot45, 1.0f);
    EXPECT_FALSE(deg.valid);

    // Layout check for sdk::raw_hitbox / sdk::matrix2x4 (F1.10).
    static_assert(sizeof(sdk::raw_hitbox) == 0x70);
    static_assert(sizeof(sdk::matrix2x4) == 0x20);
}

TEST(E2E_Tier2, T2_R6_ScoutBetweenShotsCooldownDoesNotLockAtZeroSpeed) {
    // F1.8, F1.13: Log L2562: nt=2345 vs tick_base=2265 (80 ticks = 1.25s) with between_shots must NOT lock stop=1 at spd=0.
    features::autostop_input in{};
    in.enabled = true;
    in.flags = config::auto_stop_flag::early | config::auto_stop_flag::between_shots;
    in.on_ground = true;
    in.has_velocity = true;
    in.velocity = {0.0f, 0.0f, 0.0f};
    in.speed_2d = 0.0f;
    in.cmd_yaw = 0.0f;
    in.has_target = true;
    in.weapon_ready = false;
    in.within_early_window = false;
    // Simulates long Scout/AWP bolt cycle (> 0.5s remaining): between_shots_active must be false.
    const int ticks_until_ready = 80;
    in.between_shots_active = (ticks_until_ready <= 6);
    in.recent_shot_active = false;

    const auto dec = features::compute_autostop(in);
    EXPECT_FALSE(dec.active);
    EXPECT_FALSE(dec.zero_move);
}

TEST(E2E_Tier2, T2_AA1_YawWrapAround180Seam) {
    // F2.4, F2.5, F2.6: 179.9 + 180 + 30 normalized strictly in (-180, 180].
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_yaw = config::yaw_mode::jitter;
    aa.jitter_range = 30.0f;
    aa.yaw_offset = 180.0f;

    for (int t = 0; t < 10; ++t) {
        features::antiaim_state st{};
        st.tick = t;
        const math::angles out = features::apply_antiaim(aa, {0.0f, 179.9f}, st);
        EXPECT_TRUE(std::isfinite(out.yaw));
        EXPECT_GT(out.yaw, -180.0f);
        EXPECT_LE(out.yaw, 180.0f);
    }
}

TEST(E2E_Tier2, T2_AA2_PitchClampBoundaries) {
    // F2.4: Pitch clamp +/-89, zero, off with +/-95 input.
    EXPECT_FLOAT_EQ(math::clamp_pitch(95.0f), 89.0f);
    EXPECT_FLOAT_EQ(math::clamp_pitch(-95.0f), -89.0f);
    EXPECT_FLOAT_EQ(math::clamp_pitch(std::numeric_limits<float>::quiet_NaN()), 0.0f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    features::antiaim_context ctx{};
    aa.base_pitch = config::pitch_mode::down;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(aa, {15.0f, 0.0f}, ctx).pitch, 89.0f);
    aa.base_pitch = config::pitch_mode::up;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(aa, {15.0f, 0.0f}, ctx).pitch, -89.0f);
    aa.base_pitch = config::pitch_mode::zero;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(aa, {15.0f, 0.0f}, ctx).pitch, 0.0f);
    aa.base_pitch = config::pitch_mode::off;
    EXPECT_FLOAT_EQ(features::apply_antiaim_full(aa, {95.0f, 0.0f}, ctx).pitch, 89.0f);
#endif
}

TEST(E2E_Tier2, T2_AA3_GateOnMoveFalseSpeed5Threshold) {
    // F2.7: on_move=false gates on speed_2d <= 5.0 && !has_move_input (no flicker around 5 u/s).
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_yaw = config::yaw_mode::fake_static;
    aa.on_move = false;
    aa.at_targets = false;
    aa.auto_adjust = false;

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    features::antiaim_context ctx{};
    ctx.on_ground = true;

    ctx.speed_2d = 4.9f;
    ctx.has_move_input = false;
    EXPECT_NEAR(features::apply_antiaim_full(aa, {0.0f, 20.0f}, ctx).yaw, -160.0f, 0.01f);

    ctx.speed_2d = 5.0f;
    ctx.has_move_input = false;
    EXPECT_NEAR(features::apply_antiaim_full(aa, {0.0f, 20.0f}, ctx).yaw, -160.0f, 0.01f);

    ctx.speed_2d = 5.1f;
    ctx.has_move_input = false;
    EXPECT_NEAR(features::apply_antiaim_full(aa, {0.0f, 20.0f}, ctx).yaw, 20.0f, 0.01f);

    ctx.speed_2d = 2.0f;
    ctx.has_move_input = true;
    EXPECT_NEAR(features::apply_antiaim_full(aa, {0.0f, 20.0f}, ctx).yaw, 20.0f, 0.01f);
#else
    features::antiaim_state st{};
    st.moving = false;
    EXPECT_NEAR(features::apply_antiaim(aa, {0.0f, 20.0f}, st).yaw, -160.0f, 0.01f);
    st.moving = true;
    EXPECT_NEAR(features::apply_antiaim(aa, {0.0f, 20.0f}, st).yaw, 20.0f, 0.01f);
#endif
}

TEST(E2E_Tier2, T2_AA4_FreestandingSymmetryAnd096Hysteresis) {
    // F2.3: Symmetric equidistant walls and 0.959 vs 0.961 threshold hysteresis.
    const std::array<CoverAndPoseGeometry::WallAABB, 2> sym_walls{{
        {{-30.0f, 18.0f, 0.0f}, {30.0f, 26.0f, 80.0f}},
        {{-30.0f, -26.0f, 0.0f}, {30.0f, -18.0f, 80.0f}},
    }};
    const math::vec3 eye{0.0f, 0.0f, 64.0f};
    const float fl = CoverAndPoseGeometry::trace_walls(eye, eye + math::forward_from_angles({0.0f, 90.0f}) * 42.0f, sym_walls);
    const float fr = CoverAndPoseGeometry::trace_walls(eye, eye + math::forward_from_angles({0.0f, -90.0f}) * 42.0f, sym_walls);
    EXPECT_NEAR(fl, fr, 1e-4f);
    EXPECT_LT(fl, 0.5f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    // Deterministic tie-break across 10 evaluations (no 240Hz ping-pong) + 0.96 hysteresis.
    features::freestanding_state fs_state{};
    std::array<features::freestanding_candidate, 2> sym_cands{{{115.0f, fl, true}, {-115.0f, fr, true}}};
    const auto d1 = features::select_freestanding_yaw(0.0f, true, 1.0f, sym_cands, fs_state);
    EXPECT_TRUE(d1.tucked);
    for (int i = 0; i < 10; ++i) {
        const auto di = features::select_freestanding_yaw(0.0f, true, 1.0f, sym_cands, fs_state);
        EXPECT_TRUE(di.tucked);
        EXPECT_EQ(di.selected_side, d1.selected_side);
        EXPECT_FLOAT_EQ(di.selected_yaw, d1.selected_yaw);
    }

    // Center blocked (< 0.96) untucks immediately; 0.961 untucked does not enter; 0.959 tucked stays tucked.
    features::freestanding_state fresh{};
    std::array<features::freestanding_candidate, 2> edge_high{{{115.0f, 0.961f, true}, {-115.0f, 1.0f, true}}};
    EXPECT_FALSE(features::select_freestanding_yaw(0.0f, true, 1.0f, edge_high, fresh).tucked);

    std::array<features::freestanding_candidate, 2> edge_in{{{115.0f, 0.959f, true}, {-115.0f, 1.0f, true}}};
    EXPECT_TRUE(features::select_freestanding_yaw(0.0f, true, 1.0f, edge_in, fresh).tucked);
    // Once tucked on +1 side, 0.970 (< 0.98 hold threshold) stays tucked.
    std::array<features::freestanding_candidate, 2> edge_hold{{{115.0f, 0.970f, true}, {-115.0f, 1.0f, true}}};
    EXPECT_TRUE(features::select_freestanding_yaw(0.0f, true, 1.0f, edge_hold, fresh).tucked);
    EXPECT_FALSE(features::select_freestanding_yaw(0.0f, true, 0.90f, edge_hold, fresh).tucked);

    const math::vec3 probe = features::freestanding_head_probe(eye, 115.0f, 89.0f);
    EXPECT_LT(std::hypot(probe.x - eye.x, probe.y - eye.y), 16.0f);
    const math::vec3 target_pt = features::freestanding_trace_target({300.0f, 0.0f, 64.0f}, probe, 16.0f);
    EXPECT_GT((target_pt - math::vec3{300.0f, 0.0f, 64.0f}).length(), 15.0f);
#endif
}

TEST(E2E_Tier2, T2_AA5_AtTargetsTrueWithNoEnemyFallsBackToCamera) {
    // F2.2, F2.4: at_targets=true + found_enemy=false falls back cleanly to camera_view.yaw.
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.at_targets = true;
    aa.base_yaw = config::yaw_mode::fake_static;
    const float cam_yaw = -37.5f;
    const float ref_yaw = /*found_enemy=*/false ? 90.0f : cam_yaw;
    const math::angles out = features::apply_antiaim(aa, {0.0f, ref_yaw}, {});
    EXPECT_TRUE(std::isfinite(out.yaw));
    EXPECT_NEAR(math::yaw_delta(math::normalize_yaw(cam_yaw + 180.0f), out.yaw), 0.0f, 0.01f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    features::antiaim_context ctx{};
    ctx.found_enemy = false;
    ctx.enemy_yaw = std::numeric_limits<float>::quiet_NaN();
    const math::angles full_out = features::apply_antiaim_full(aa, {0.0f, cam_yaw}, ctx);
    EXPECT_TRUE(std::isfinite(full_out.yaw));
    EXPECT_NEAR(math::yaw_delta(math::normalize_yaw(cam_yaw + 180.0f), full_out.yaw), 0.0f, 0.01f);
#endif
}

TEST(E2E_Tier2, T2_AA6_GroundQuantizedMoveZeroAnd22_5DegWorstCase) {
    // F2.1, F3.8: Zero wish outputs (0,0); cmd_yaw = camera_yaw + 22.5 deg converges over 16 ticks.
#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
    features::quantized_move_state st{};
    float qf = 1.0f, ql = 1.0f;
    features::compute_quantized_ground_move({0.0f, 0.0f, 0.0f}, 22.5f, st, 1, qf, ql);
    EXPECT_FLOAT_EQ(qf, 0.0f);
    EXPECT_FLOAT_EQ(ql, 0.0f);

    CS2PhysicsState phys{};
    phys.on_ground = true;
    const math::vec3 wish = CS2PhysicsEngine::wish_dir_from_cmd(0.0f, 1.0f, 0.0f);
    for (int t = 0; t < 16; ++t) {
        features::compute_quantized_ground_move(wish, 22.5f, st, 10 + t, qf, ql);
        CS2PhysicsEngine::step_tick(phys, 22.5f, qf, ql, false);
    }
    const float vel_yaw = std::atan2(phys.vel.y, phys.vel.x) * k_rad_to_deg;
    EXPECT_NEAR(math::yaw_delta(0.0f, vel_yaw), 0.0f, 10.0f);
#else
    float qf = 0.0f, ql = 0.0f;
    CS2PhysicsEngine::quantize_move_input(0.0f, 0.0f, qf, ql);
    EXPECT_FLOAT_EQ(qf, 0.0f);
    EXPECT_FLOAT_EQ(ql, 0.0f);
#endif
}

TEST(E2E_Tier2, T2_M1_AutoStrafeSpeedZeroAnd24_4Boundary) {
    // F3.1, F3.2: Stationary jump (speed_2d=0) & exact 24.4 u/s boundary produce finite non-NaN accel.
    for (float spd : {0.0f, 24.4f, 24.41f}) {
        const auto s = features::compute_autostrafe({0.0f, {spd, 0.0f, 0.0f}, 1.0f, 0.0f, 1});
        EXPECT_TRUE(s.active);
        EXPECT_TRUE(std::isfinite(s.forwardmove));
        EXPECT_TRUE(std::isfinite(s.leftmove));
        EXPECT_TRUE(std::isfinite(s.steer_yaw));
    }
}

TEST(E2E_Tier2, T2_M2_320HzFloodingPerCommandNumber) {
    // F1.5, F3.4, F3.6: 5 identical calls per command_number must advance stateful latches once.
    features::bhop_state bs{};
    features::shot_delay sd{};
    features::backtrack_history bh;
    const auto snap = MultiPlayerHitboxArena::make_enemy_snapshot(1, 0x77u, {350.0f, 0.0f, 0.0f});

    config::rage_weapon w{};
    w.delay_shot = config::delay_shot_mode::hitchance;
    w.delay_ticks = 3;
    w.hitchance = 60.0f;

    int stall_ticks = 3;
    int last_cmd = -1;
    TickClock64_300 clock;
    clock.run_ticks(3, 5,
        [&](int cmd, int tick, float curtime, int /*sf*/, int /*rf*/) {
            (void)features::step_bhop(bs, cmd, true, true);
            (void)sd.ready(0x1111u, 0x2222u, tick, w, 75.0f, 50.0f);
            bh.push(1, snap, curtime);
            if (cmd != last_cmd) {
                last_cmd = cmd;
                if (stall_ticks > 0) --stall_ticks;
            }
        },
        [](int, int, float) {});

    EXPECT_EQ(stall_ticks, 0);
    EXPECT_EQ(sd.ticks, 3);
    EXPECT_EQ(bh.record_count(1), 3);  // F1.5: deduplicated to 1 record per server tick.
}

TEST(E2E_Tier2, T2_M3_SubtickMovesZeroCapacityAndNullSsSafety) {
    // F3.3, F2.9, F2.10: capacity=0, current_size==capacity, ss==nullptr all safely return nullptr.
    UserCmdHarness h;
    h.base.subtick_moves.capacity = 0;
    EXPECT_EQ(safe_add_subtick_step(h.base.subtick_moves, sdk::k_in_jump, true, 0.5f), nullptr);

    h.base.subtick_moves.capacity = 2;
    h.base.subtick_moves.current_size = 2;
    EXPECT_EQ(safe_add_subtick_step(h.base.subtick_moves, sdk::k_in_jump, true, 0.5f), nullptr);

    h.base.subtick_moves.current_size = 0;
    h.base.subtick_moves.ss = nullptr;
    EXPECT_EQ(safe_add_subtick_step(h.base.subtick_moves, sdk::k_in_jump, true, 0.5f), nullptr);
}

TEST(E2E_Tier2, T2_M4_SlideWalkSuppressionBoundaries) {
    // F3.5, F3.7: leg_movement & block_server_detected config roundtrip + sdk::in_button_state
    // suppression bit transitions + compute_autostop ground vs air braking interaction.
    config::store s = DefaultJsonConfigFixture::load();
    EXPECT_FALSE(s.get().misc.movement.leg_movement);
    s.get().misc.movement.leg_movement = true;
    const std::string saved = s.to_json();
    config::store reloaded;
    ASSERT_TRUE(reloaded.from_json(saved));
    EXPECT_TRUE(reloaded.get().misc.movement.leg_movement);

    UserCmdHarness h;
    h.begin_tick(410, 1050, {0.0f, 0.0f}, sdk::k_in_forward, 1.0f, 0.0f);
    constexpr std::uint64_t k_suppress =
        sdk::k_in_jump | sdk::k_in_duck | sdk::k_in_attack | sdk::k_in_attack2;
    EXPECT_EQ(h.cmd.button_state.held & k_suppress, 0ULL);
    EXPECT_NE(h.cmd.button_state.held & sdk::k_in_forward, 0ULL);

    // Setting and clearing suppressive buttons updates held/toggle/scroll cleanly.
    for (std::uint64_t btn : {sdk::k_in_jump, sdk::k_in_duck, sdk::k_in_attack, sdk::k_in_attack2}) {
        h.cmd.button_state.set_button(btn, true);
        EXPECT_NE(h.cmd.button_state.held & k_suppress, 0ULL);
        h.cmd.button_state.set_button(btn, false);
        EXPECT_EQ(h.cmd.button_state.held & k_suppress, 0ULL);
    }

    // Active ground AutoStop engages braking (slide-walk suppressed during brake).
    const auto brake_dec = features::compute_autostop({
        true, config::auto_stop_flag::early, true, true, {180.0f, 0.0f, 0.0f}, 180.0f,
        0.0f, true, true, false, true, false, false
    });
    EXPECT_TRUE(brake_dec.active);
    const std::uint64_t synced = UserCmdHarness::sync_move_buttons(0ULL, brake_dec.forwardmove, brake_dec.leftmove);
    EXPECT_EQ(synced, sdk::k_in_back);
}

TEST(E2E_Tier2, T2_M5_UserCommandManagerRingBoundary) {
    // F1.12: get_user_command ring boundary 0, 149, 150 (wraps to 0), -1 (current), -2 (nullptr).
    auto mgr = std::make_unique<sdk::user_command_manager>();
    mgr->command_number = 42;
    EXPECT_EQ(mgr->get_user_command(0), &mgr->commands[0]);
    EXPECT_EQ(mgr->get_user_command(149), &mgr->commands[149]);
    EXPECT_EQ(mgr->get_user_command(150), &mgr->commands[0]);
    EXPECT_EQ(mgr->get_user_command(-1), &mgr->commands[42]);
    EXPECT_EQ(mgr->get_user_command(-2), nullptr);
}

TEST(E2E_Tier2, T2_M6_ConfigInvalidNaNOutOfRangeClamping) {
    // F1.9, F2.8: Invalid base_yaw=1, 99, backtrack_ms=NaN, vk=255 clamped safely.
    config::store s;
    ASSERT_TRUE(s.from_json(R"({
        "version": 1,
        "anti_aim": { "base_yaw": 1, "jitter_range": 999.0, "yaw_offset": -999.0 },
        "rage": { "backtrack_ms": null, "extrapolation_ms": 500.0, "toggle": { "vk": 255, "hold": true } }
    })"));
    EXPECT_EQ(s.get().anti_aim.base_yaw, config::yaw_mode::off);
    EXPECT_FLOAT_EQ(s.get().anti_aim.jitter_range, 180.0f);
    EXPECT_FLOAT_EQ(s.get().anti_aim.yaw_offset, -180.0f);
    EXPECT_FLOAT_EQ(s.get().rage.extrapolation_ms, 50.0f);
    EXPECT_EQ(s.get().rage.toggle.vk, 0);
    EXPECT_GT(s.clamps_applied(), 0);

    // Explicitly test numeric NaN clamping in JSON string (null falls back; invalid huge clamped).
    config::store s2;
    ASSERT_TRUE(s2.from_json(R"({"version":1,"anti_aim":{"base_yaw":99},"rage":{"backtrack_ms":9999.0}})"));
    EXPECT_EQ(s2.get().anti_aim.base_yaw, config::yaw_mode::fake_static);
    EXPECT_FLOAT_EQ(s2.get().rage.backtrack_ms, 200.0f);
}

// =============================================================================
// TIER 3 — Pairwise Feature Combinations (10 cases)
// =============================================================================

TEST(E2E_Tier3, T3_1_RageFireTimesAAOnShotGate) {
    // default.json: anti_aim.on_shot=false, silent_aim=true.
    config::store cfg_store = DefaultJsonConfigFixture::load();
    config::anti_aim_cfg aa = cfg_store.get().anti_aim;
    aa.enabled = true;
    EXPECT_FALSE(aa.on_shot);
    EXPECT_TRUE(cfg_store.get().rage.silent_aim);

    UserCmdHarness h;
    const math::angles cam{5.0f, 30.0f};
    const math::angles fire_ang{-2.0f, 115.0f};
    h.begin_tick(200, 1000, cam, sdk::k_in_attack, 0.0f, 0.0f);

    // AA applies even when manual on_shot=false; rage silent_aim writes fire_ang to input_history without corrupting cam.
    const math::angles aa_out = features::apply_antiaim(aa, cam, {true, false, true, 200, 0});
    EXPECT_NEAR(math::yaw_delta(math::normalize_yaw(cam.yaw + 180.0f), aa_out.yaw), 0.0f, 0.01f);

    h.base_va.set(aa_out.pitch, aa_out.yaw);
    h.history_va_pool[0].set(fire_ang.pitch, fire_ang.yaw);
    h.cmd.pb.set_attack1_start_history_index(0);
    EXPECT_FLOAT_EQ(h.input_view()->viewangles().yaw, cam.yaw);
    EXPECT_FLOAT_EQ(h.history_va_pool[0].y, fire_ang.yaw);
    EXPECT_EQ(h.cmd.pb.attack1_start_history_index, 0);
}

TEST(E2E_Tier3, T3_2_RageAutoStopTimesAAAndRotateMovement) {
    // F1.8 x F3.5: should_stop=true on firing tick keeps braking engaged (no WASD restore);
    // should_stop=false moving shot restores pre-AA WASD & re-rotates for fire_angle.
    features::autostop_input in{};
    in.enabled = true;
    in.flags = config::auto_stop_flag::all;
    in.on_ground = true;
    in.has_velocity = true;
    in.velocity = {90.0f, 0.0f, 0.0f};
    in.speed_2d = 90.0f;
    in.cmd_yaw = 180.0f;  // fake_static AA
    in.has_target = true;
    in.weapon_ready = true;
    in.shot_qualified = true;

    const auto stop_dec = features::compute_autostop(in);
    EXPECT_TRUE(stop_dec.active);  // Must remain active on firing tick!

    // Moving shot (should_stop=false) restores user WASD and re-rotates from 0 to 90 deg (+X is right/D -> leftmove=-1).
    float f = 0.0f, l = 0.0f;
    UserCmdHarness::rotate_move_harness(1.0f, 0.0f, 0.0f, 90.0f, f, l);
    EXPECT_NEAR(f, 0.0f, 0.01f);
    EXPECT_LT(l, -0.9f);
}

TEST(E2E_Tier3, T3_3_AirborneAutoStrafeTimesRageFire) {
    // F3.1 x F3.5: Jumping Scout (auto_stop_flags=5, in_air=0) preserves strafe_out on air fire tick.
    config::store cfg_store = DefaultJsonConfigFixture::load();
    const config::rage_weapon& scout = cfg_store.get().rage.weapons[static_cast<std::size_t>(config::weapon_group::scout)];
    EXPECT_EQ(scout.auto_stop_flags & config::auto_stop_flag::in_air, 0u);

    features::autostop_input air_stop{};
    air_stop.enabled = scout.auto_stop;
    air_stop.flags = scout.auto_stop_flags;
    air_stop.on_ground = false;
    air_stop.has_velocity = true;
    air_stop.velocity = {270.0f, 30.0f, 120.0f};
    air_stop.speed_2d = 271.66f;
    air_stop.has_target = true;
    air_stop.weapon_ready = true;
    EXPECT_FALSE(features::compute_autostop(air_stop).active);

    const auto strafe = features::compute_autostrafe({0.0f, air_stop.velocity, 1.0f, 0.0f, 1});
    EXPECT_TRUE(strafe.active);
    UserCmdHarness h;
    h.begin_tick(300, 1100, {0.0f, 0.0f}, sdk::k_in_attack, strafe.forwardmove, strafe.leftmove);
    h.cmd.button_state.held = UserCmdHarness::sync_move_buttons(h.cmd.button_state.held, strafe.forwardmove, strafe.leftmove);
    h.base.clear_subtick_moves();
    EXPECT_TRUE(h.verify_protobuf_hygiene());
}

TEST(E2E_Tier3, T3_4_BhopTimesAutoStrafeTimesFakeStaticAA) {
    // F3.1 x F2.1 x T1_M3: Multi-tick bhop chain from running CS2 entry velocity (250 u/s) with fake_static AA (+180).
    CS2PhysicsState phys{};
    phys.vel = {250.0f, 0.0f, 0.0f};
    phys.on_ground = true;
    features::bhop_state bs{};
    TickClock64_300 clock;
    const float cam_yaw = 0.0f;
    const float aa_yaw = 180.0f;

    float max_air_speed = 0.0f;
    int jumps = 0;
    int strafe_sign = 1;

    clock.run_ticks(64, 1,
        [&](int cmd, int /*tick*/, float /*curtime*/, int /*sf*/, int /*rf*/) {
            (void)features::step_bhop(bs, cmd, phys.on_ground, true);
        },
        [&](int cmd, int /*tick*/, float /*curtime*/) {
            float fwd = 0.0f, left = 0.0f;
            float step_yaw = aa_yaw;
            if (phys.on_ground) {
                UserCmdHarness::rotate_move_harness(1.0f, 0.0f, cam_yaw, aa_yaw, fwd, left);
                (void)cmd;
                (void)strafe_sign;
            } else {
#if !SHOGOD_E2E_ENABLE_M3_CONTRACTS
                const auto q = features::compute_quantized_autostrafe(cam_yaw, aa_yaw, phys.vel, 1.0f, 0.0f);
                fwd = q.forwardmove;
                left = q.leftmove;
#else
                const auto s = features::compute_sector_matched_autostrafe(
                    cam_yaw, aa_yaw, phys.vel, 1.0f, 0.0f, strafe_sign, cmd);
                EXPECT_LE(std::fabs(math::yaw_delta(aa_yaw, s.steer_yaw)), 22.5f);
                fwd = s.forwardmove;
                left = s.leftmove;
                step_yaw = s.steer_yaw;
                strafe_sign = s.next_strafe_sign;
#endif
            }
            if (phys.on_ground && bs.cmd_jump) ++jumps;
            CS2PhysicsEngine::step_tick(phys, step_yaw, fwd, left, bs.cmd_jump);
            if (!phys.on_ground) {
                max_air_speed = std::max(max_air_speed, CS2PhysicsEngine::speed_2d(phys));
            }
        });

    EXPECT_GE(jumps, 2);
    EXPECT_GT(max_air_speed, 300.0f);
}

TEST(E2E_Tier3, T3_5_AutoScopeTimesAutoStopWaitTimesDelayShot) {
    // F1.8 x F1.13 x F1.15: Scout (auto_stop_wait=true, delay_ticks=6).
    config::store cfg_store = DefaultJsonConfigFixture::load();
    config::rage_weapon scout = cfg_store.get().rage.weapons[static_cast<std::size_t>(config::weapon_group::scout)];
    scout.delay_shot = config::delay_shot_mode::both;
    EXPECT_TRUE(scout.auto_stop_wait);
    EXPECT_EQ(scout.delay_ticks, 6);

    // Case 1: unscoped hc=76 >= 73 qualifies without waiting for scope.
    EXPECT_TRUE(features::should_fire(76.0f, 90.0f, scout));

    // Case 2: brake down from 220 u/s to < auto_stop_speed (60), then hold 6 delay_ticks before firing.
    CS2PhysicsState phys{};
    phys.vel = {220.0f, 0.0f, 0.0f};
    phys.on_ground = true;
    features::shot_delay sd{};
    int fire_tick = -1;
    for (int t = 0; t < 20; ++t) {
        const float spd = CS2PhysicsEngine::speed_2d(phys);
        const float sim_hc = spd <= scout.auto_stop_speed ? 82.0f : 22.0f;
        if (spd <= scout.auto_stop_speed && sd.ready(0x10u, 0x20u, 500 + t, scout, sim_hc, 85.0f)) {
            fire_tick = 500 + t;
            break;
        }
        const auto dec = features::compute_autostop({
            true, scout.auto_stop_flags, true, true, phys.vel, spd, 0.0f, true, true, false, true, false, false
        });
        CS2PhysicsEngine::step_tick(phys, 0.0f, dec.forwardmove, dec.leftmove, false);
    }
    EXPECT_GE(fire_tick, 506);
}

TEST(E2E_Tier3, T3_6_FreestandingTimesAtTargetsTimesAutoAdjustMatrix) {
    // F2.2 x F2.4 x F2.5: All 4 boolean combos (at_targets x freestanding) with auto_adjust.
    struct Combo {
        bool at_targets;
        bool freestanding;
        bool corner_tucked;
        float expected_yaw;
    };
    const float cam = 10.0f;
    const float enemy = 100.0f;
    const float tuck = -70.0f;
    const std::array<Combo, 4> combos{{
        {false, false, false, math::normalize_yaw(cam + 180.0f)},
        {true,  false, false, math::normalize_yaw(enemy + 180.0f)},
        {false, true,  true,  tuck},
        {true,  true,  false, math::normalize_yaw(enemy + 180.0f)},
    }};
    for (const auto& c : combos) {
        const float base_ref = c.at_targets ? enemy : cam;
        const float out_yaw = (c.freestanding && c.corner_tucked)
                                  ? tuck
                                  : math::normalize_yaw(base_ref + 180.0f);
        EXPECT_NEAR(math::yaw_delta(c.expected_yaw, out_yaw), 0.0f, 0.01f);

#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
        config::anti_aim_cfg aa{};
        aa.enabled = true;
        aa.base_yaw = config::yaw_mode::fake_static;
        aa.at_targets = c.at_targets;
        aa.freestanding = c.freestanding;
        aa.auto_adjust = false;
        features::antiaim_context ctx{};
        ctx.found_enemy = true;
        ctx.enemy_yaw = enemy;
        ctx.freestanding.tucked = c.corner_tucked;
        ctx.freestanding.selected_yaw = tuck;
        const math::angles res = features::apply_antiaim_full(aa, {0.0f, cam}, ctx);
        EXPECT_NEAR(math::yaw_delta(c.expected_yaw, res.yaw), 0.0f, 0.5f);
#endif
    }
}

TEST(E2E_Tier3, T3_7_LagcompTimesExtrapolationTimesIgnoreLimbs) {
    // F1.3 x F1.4 x F1.2: Backtrack selected -> render_tick_count == selected.simulation_tick (no +50ms shift).
    MultiPlayerHitboxArena::LagTrackSim track;
    for (int t = 200; t <= 206; ++t) {
        const float x = 500.0f + static_cast<float>(t - 200) * 3.5f;
        const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes({x, 0.0f, 0.0f});
        track.push(0x55u, t, t * TickClock64_300::k_dt, 0.0f, {x, 0.0f, 0.0f}, hbs);
    }
    const auto sel = track.select(206, 200.0f, 20.0f, 15.6f);
    ASSERT_FALSE(sel.empty());
    const LagRecordSim* chosen = sel.back();  // Backtrack oldest
    UserCmdHarness h;
    h.begin_tick(400, 206, {0.0f, 0.0f}, sdk::k_in_attack, 0.0f, 0.0f);
    // When backtracking a historical record, render_tick_count matches selected simulation_tick (not +50ms forward!).
    h.history_pool[0].set_render_tick_count(chosen->simulation_tick);
    EXPECT_EQ(h.history_pool[0].render_tick_count, chosen->simulation_tick);

    // Extrapolation only applies to current/predicted non-backtracked record and clamps to 50ms.
    const math::vec3 ex = features::extrapolate_point({500.0f, 0.0f, 64.0f}, {250.0f, 0.0f, 0.0f}, 120.0f);
    EXPECT_FLOAT_EQ(ex.x, 512.5f);  // 250 * 0.05s
}

TEST(E2E_Tier3, T3_8_SafeModeTimesSlideWalkAutoPistolRage) {
    // F1.14: block_server_detected & safe_profile clamping via config::store + estimate_hit_damage + should_fire.
    config::store s = DefaultJsonConfigFixture::load();
    config::rage_weapon ak = s.get().rage.resolve_weapon(7, 3);
    EXPECT_FLOAT_EQ(ak.min_damage, 4.0f);

    config::store safe_s;
    ASSERT_TRUE(safe_s.from_json(R"({
        "version": 1,
        "misc": {
            "block_server_detected": true,
            "movement": { "leg_movement": true },
            "utilities": { "auto_pistol": true }
        },
        "rage": {
            "weapons": [
                {"min_damage": 25.0, "hitchance": 50.0},
                {"min_damage": 108.0, "hitchance": 60.0},
                {"min_damage": 25.0, "hitchance": 55.0},
                {"min_damage": 50.0, "hitchance": 60.0},
                {"min_damage": 115.0, "hitchance": 73.0},
                {"min_damage": 120.0, "hitchance": 73.0},
                {"min_damage": 125.0, "hitchance": 73.0},
                {"min_damage": 25.0, "hitchance": 50.0}
            ]
        }
    })"));
    EXPECT_TRUE(safe_s.get().misc.block_server_detected);
    EXPECT_TRUE(safe_s.get().misc.utilities.auto_pistol);
    EXPECT_TRUE(safe_s.get().misc.movement.leg_movement);
    EXPECT_FLOAT_EQ(safe_s.get().rage.weapons[1].min_damage, 108.0f);
    EXPECT_FLOAT_EQ(safe_s.get().rage.weapons[4].min_damage, 115.0f);
    EXPECT_FLOAT_EQ(safe_s.get().rage.weapons[6].min_damage, 120.0f);
    EXPECT_EQ(safe_s.clamps_applied(), 1);

    // Armored AK chest shot (~24.8 dmg) passes default ak (min_damage=4) yet is blocked under safe min_damage=50.
    const float chest_dmg = features::estimate_hit_damage(
        36.0f, 600.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::chest);
    EXPECT_GT(chest_dmg, ak.min_damage);
    EXPECT_LT(chest_dmg, 50.0f);
    EXPECT_TRUE(features::should_fire(70.0f, chest_dmg, ak));
    EXPECT_FALSE(features::should_fire(70.0f, chest_dmg, safe_s.get().rage.weapons[3]));
}

TEST(E2E_Tier3, T3_9_KeybindsToggleHoldTimesMenuFocusLoss) {
    // F2.8 x F3.7: focus loss disarms holds, preserves toggle, and resets movement/delay state.
    config::bind_state bhop_bind;
    config::bind_state peek_bind;
    config::bind_state aa_bind;

    const config::keybind kb_bhop{0x20, true, false};
    const config::keybind kb_peek{0x43, true, false};
    const config::keybind kb_aa{0x58, false, false};

    // Prime unpressed edge so initial held state isn't ignored by bind_state edge detector.
    EXPECT_FALSE(bhop_bind.sample(kb_bhop, true, true, false));
    EXPECT_FALSE(peek_bind.sample(kb_peek, true, true, false));
    EXPECT_FALSE(aa_bind.sample(kb_aa, true, true, false));

    EXPECT_TRUE(bhop_bind.sample(kb_bhop, true, true, true));
    EXPECT_TRUE(peek_bind.sample(kb_peek, true, true, true));
    EXPECT_TRUE(aa_bind.sample(kb_aa, true, true, true));

    // Focus loss / menu open (accepts_input=false)
    EXPECT_FALSE(bhop_bind.sample(kb_bhop, true, false, true));
    EXPECT_FALSE(peek_bind.sample(kb_peek, true, false, true));
    EXPECT_TRUE(aa_bind.sample(kb_aa, true, false, false));

    features::bhop_state bs{500, true, true, true};
    features::shot_delay sd{0x1u, 0x2u, 100, 3, config::delay_shot_mode::both, 6};
    // Menu open / focus loss reset contract (F3.7).
    bs = {};
    sd = {};
    EXPECT_FALSE(bs.cmd_jump);
    EXPECT_EQ(bs.last_cmd, -1);
    EXPECT_EQ(sd.ticks, 0);
}

TEST(E2E_Tier3, T3_10_AutoRevolverTimesInputHistoryTimesOnShot) {
    // F1.12 x F2.10: R8 Revolver (def=64) clears attack2 / attack2_start_history_index on primary fire.
    const config::rage_weapon_spec* r8 = config::find_rage_weapon_by_def(64);
    ASSERT_NE(r8, nullptr);
    EXPECT_EQ(r8->group, config::weapon_group::heavy_pistol);

    UserCmdHarness h;
    h.begin_tick(600, 1200, {0.0f, 0.0f}, sdk::k_in_attack2, 0.0f, 0.0f);
    h.cmd.pb.set_attack2_start_history_index(0);
    EXPECT_EQ(h.cmd.pb.has_bits & 0x40u, 0x40u);

    // Primary fire tick clears attack2 & attack2_start_history_index cleanly and sets attack1 index.
    h.cmd.button_state.set_button(sdk::k_in_attack2, false);
    h.cmd.button_state.set_button(sdk::k_in_attack, true);
    h.cmd.pb.clear_attack2_start_history_index();
    h.cmd.pb.set_attack1_start_history_index(0);
    EXPECT_EQ(h.cmd.button_state.held & sdk::k_in_attack2, 0ULL);
    EXPECT_EQ(h.cmd.pb.attack2_start_history_index, -1);
    EXPECT_EQ(h.cmd.pb.has_bits & 0x40u, 0u);
    EXPECT_EQ(h.cmd.pb.attack1_start_history_index, 0);
}

// =============================================================================
// TIER 4 — Realistic Multi-Tick CS2 HvH Scenarios (7 scenarios)
// =============================================================================

TEST(E2E_Tier4, T4_1_ScoutPeekScopeAndCooldownCycle) {
    // Grounded in default.json weapons[4] (Scout) + shogod-20261121.log L662, L2562.
    config::store cfg_store = DefaultJsonConfigFixture::load();
    const config::rage_weapon& scout = cfg_store.get().rage.weapons[static_cast<std::size_t>(config::weapon_group::scout)];

    CS2PhysicsState phys{};
    phys.vel = {240.0f, 0.0f, 0.0f};
    phys.on_ground = true;

    bool fired = false;
    int fire_tick = -1;
    int lock_ticks_during_cooldown = 0;

    for (int t = 0; t < 100; ++t) {
        const float spd = CS2PhysicsEngine::speed_2d(phys);
        if (!fired) {
            // Simulate hitchance climbing as Scout decelerates below auto_stop_speed (60 u/s).
            const float sim_hc = spd <= scout.auto_stop_speed ? 76.2f : 19.1f;
            const bool qualified = features::should_fire(sim_hc, 109.6f, scout);
            const auto stop_dec = features::compute_autostop({
                scout.auto_stop, scout.auto_stop_flags, true, true, phys.vel, spd, 0.0f,
                true, true, qualified, true, false, false
            });
            EXPECT_TRUE(stop_dec.active);  // Must remain active even on the firing tick (qualified == true).
            CS2PhysicsEngine::step_tick(phys, 0.0f, stop_dec.forwardmove, stop_dec.leftmove, false);
            if (qualified) {
                fired = true;
                fire_tick = t;
            }
        } else {
            // Post-shot 80-tick (1.25s) bolt cycle (log L2562: nt=2345 vs tick_base=2265).
            const int ticks_remaining = (fire_tick + 80) - t;
            const bool between_window = ticks_remaining > 0 && ticks_remaining <= 6;
            const auto stop_dec = features::compute_autostop({
                scout.auto_stop, scout.auto_stop_flags, true, true, phys.vel, spd, 0.0f,
                true, ticks_remaining <= 0, false, false, between_window, false
            });
            if (ticks_remaining > 10 && stop_dec.active) ++lock_ticks_during_cooldown;
            CS2PhysicsEngine::step_tick(phys, 0.0f, stop_dec.active ? stop_dec.forwardmove : 1.0f, 0.0f, false);
        }
    }

    EXPECT_TRUE(fired);
    EXPECT_LT(fire_tick, 20);
    EXPECT_EQ(lock_ticks_during_cooldown, 0);
}

TEST(E2E_Tier4, T4_2_AK47SprayPeekVsArmoredMovingEnemy) {
    // Grounded in default.json weapon_overrides.ak47 (min_dmg=4, hc=60, aim_points=47, stop_flags=7, safe_point=50).
    config::store cfg_store = DefaultJsonConfigFixture::load();
    const config::rage_weapon ak = cfg_store.get().rage.resolve_weapon(7, static_cast<int>(config::weapon_group::rifle));
    EXPECT_EQ(ak.aim_points, 47u);
    EXPECT_EQ(ak.auto_stop_flags, 7u);
    EXPECT_FLOAT_EQ(ak.safe_point, 50.0f);
    EXPECT_FALSE(ak.prefer_body_aim);

    const math::vec3 eye{0.0f, 0.0f, 64.0f};
    const math::vec3 enemy_vel{0.0f, 240.0f, 0.0f};
    const math::weapon_spread_pattern ak_spread{0.004f, 0.006f};
    const float sp_scale = math::safe_point_hitbox_scale(ak.safe_point, ak.strict_hitchance);
    EXPECT_LT(sp_scale, 1.0f);
    EXPECT_GT(sp_scale, 0.7f);

    // Airborne peek: auto_stop_flags includes in_air (bit 1), so compute_autostop brakes even in air.
    const auto air_dec = features::compute_autostop({
        ak.auto_stop, ak.auto_stop_flags, /*on_ground=*/false, true, {215.0f, 0.0f, 0.0f}, 215.0f,
        0.0f, true, true, false, true, false, false
    });
    EXPECT_TRUE(air_dec.active);
    const math::vec2 cs = features::counter_strafe({215.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_LT(cs.x, -0.9f);

    // Armored head vs stomach vs chest damage at ~450u (helmet=true, armor=100, armor_ratio=1.55).
    const float head_dmg = features::estimate_hit_damage(36.0f, 450.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::head);
    const float stomach_dmg = features::estimate_hit_damage(36.0f, 450.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::stomach);
    const float chest_dmg = features::estimate_hit_damage(36.0f, 450.0f, 0.98f, 4.0f, 1.55f, 100, true, math::hitgroup::chest);
    EXPECT_GT(head_dmg, stomach_dmg);
    EXPECT_GT(stomach_dmg, chest_dmg);
    EXPECT_GT(chest_dmg, ak.min_damage);

    // Simulate moving armored enemy across 5 ticks, extrapolating origin and verifying safe_point chance_multi + recoil.
    math::vec3 enemy_origin{450.0f, -20.0f, 0.0f};
    int shots_qualified = 0;
    for (int shot = 0; shot < 5; ++shot) {
        const math::vec3 extrap_origin = features::extrapolate_point(
            enemy_origin, enemy_vel, cfg_store.get().rage.extrapolation_ms);
        EXPECT_GT(extrap_origin.y, enemy_origin.y);

        const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes(
            extrap_origin, /*yaw_deg=*/15.0f * static_cast<float>(shot), /*head_shape_type=*/2);
        const float head_hc = ak_spread.chance_multi(
            eye, hbs[0].center, hbs, 0, ak.strict_hitchance, sp_scale) * 100.0f;
        const float chest_hc = ak_spread.chance_multi(
            eye, hbs[2].center, hbs, 1, ak.strict_hitchance, sp_scale) * 100.0f;
        EXPECT_GT(chest_hc, ak.hitchance);
        if (features::should_fire(head_hc, head_dmg, ak) || features::should_fire(chest_hc, chest_dmg, ak)) {
            ++shots_qualified;
        }

        const math::angles punch{0.6f * static_cast<float>(shot + 1), -0.2f * static_cast<float>(shot)};
        const math::angles aim = math::angles_from_forward((hbs[0].center - eye).normalised());
        const math::angles send{math::clamp_pitch(aim.pitch - punch.pitch * 2.0f),
                                math::normalize_yaw(aim.yaw - punch.yaw * 2.0f)};
        EXPECT_TRUE(std::isfinite(send.pitch));
        EXPECT_TRUE(std::isfinite(send.yaw));
        enemy_origin.y += enemy_vel.y * TickClock64_300::k_dt;
    }
    EXPECT_EQ(shots_qualified, 5);
}

TEST(E2E_Tier4, T4_3_300HzRenderRateCreateMoveBurst) {
    // Grounded in shogod-20261121.log (238-328 Hz CreateMove calls across 64 server ticks).
    TickClock64_300 clock;
    features::backtrack_history bh;
    features::bhop_state bs{};
    features::shot_delay sd{};
    config::rage_weapon scout{};
    scout.delay_shot = config::delay_shot_mode::hitchance;
    scout.delay_ticks = 6;
    scout.hitchance = 73.0f;

    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_yaw = config::yaw_mode::jitter;
    aa.jitter_range = 30.0f;

    const auto enemy = MultiPlayerHitboxArena::make_enemy_snapshot(1, 0x33u, {380.0f, 0.0f, 0.0f});
    int auto_pistol_wait = 6;
    int last_cmd = -1;
    int jitter_mismatch_within_cmd = 0;

    clock.run_ticks(16, 5,
        [&](int cmd, int tick, float curtime, int sf, int rf) {
            (void)rf;
            bh.push(1, enemy, curtime);
            (void)features::step_bhop(bs, cmd, (tick % 8) == 0, true);
            (void)sd.ready(0x10u, 0x20u, tick, scout, 80.0f, 80.0f);
            if (cmd != last_cmd) {
                last_cmd = cmd;
                if (auto_pistol_wait > 0) --auto_pistol_wait;
            }
            // AA jitter seeded by command_number stays constant across all 5 subframes even if subframe seed varies.
#if SHOGOD_E2E_ENABLE_M2_CONTRACTS
            features::antiaim_context ctx0{};
            ctx0.command_number = cmd;
            ctx0.seed = 0;
            features::antiaim_context ctx_sf = ctx0;
            ctx_sf.seed = sf;
            const float y0 = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx0).yaw;
            const float y_sf = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx_sf).yaw;
#else
            const float y0 = features::apply_antiaim(aa, {0.0f, 0.0f}, {true, false, false, cmd, 0}).yaw;
            const float y_sf = features::apply_antiaim(aa, {0.0f, 0.0f}, {true, false, false, cmd, sf}).yaw;
#endif
            if (std::fabs(math::yaw_delta(y0, y_sf)) > 0.01f) ++jitter_mismatch_within_cmd;
        },
        [](int, int, float) {});

    EXPECT_EQ(jitter_mismatch_within_cmd, 0);
    EXPECT_EQ(auto_pistol_wait, 0);
    EXPECT_EQ(sd.ticks, 6);
    EXPECT_EQ(bh.record_count(1), 16);  // F1.5: 16 server ticks deduplicated across 80 calls.
}

TEST(E2E_Tier4, T4_4_BhopAutoStrafeFakeStaticMirageMidRun) {
    // Grounded in default.json + log L75/L2768/L2864 (W+A 0x208 -> S+D 0x410, > 300 u/s from running 250 u/s entry).
    config::store cfg_store = DefaultJsonConfigFixture::load();
    config::anti_aim_cfg aa = cfg_store.get().anti_aim;
    aa.enabled = true;

    CS2PhysicsState phys{};
    phys.vel = {176.7767f, 176.7767f, 0.0f};  // 250 u/s along W+A (45 deg)
    phys.on_ground = true;
    features::bhop_state bs{};
    UserCmdHarness h;
    TickClock64_300 clock;

    int seen_sd_bits = 0;
    int seen_air_jump_cleared = 0;
    float peak_speed = 0.0f;
    int strafe_sign = 1;

    clock.run_ticks(80, 1,
        [&](int cmd, int tick, float /*curtime*/, int /*sf*/, int /*rf*/) {
            h.begin_tick(cmd, tick, {0.0f, 0.0f}, sdk::k_in_jump | sdk::k_in_forward | sdk::k_in_moveleft, 1.0f, 1.0f);
            const math::angles aa_out = features::apply_antiaim(aa, {0.0f, 0.0f}, {phys.on_ground, true, false, cmd, 0});
            const bool jump_bit = features::step_bhop(bs, cmd, phys.on_ground, true);

            float fwd = 0.0f, left = 0.0f;
            float cmd_yaw = aa_out.yaw;
            if (phys.on_ground) {
                UserCmdHarness::rotate_move_harness(1.0f, 1.0f, 0.0f, aa_out.yaw, fwd, left);
                const std::uint64_t btns = UserCmdHarness::sync_move_buttons(0, fwd, left);
                if (btns == (sdk::k_in_back | sdk::k_in_moveright)) ++seen_sd_bits;
                (void)strafe_sign;
            } else {
#if !SHOGOD_E2E_ENABLE_M3_CONTRACTS
                const auto q = features::compute_quantized_autostrafe(0.0f, aa_out.yaw, phys.vel, 1.0f, 1.0f);
                fwd = q.forwardmove;
                left = q.leftmove;
#else
                const auto s = features::compute_sector_matched_autostrafe(
                    0.0f, aa_out.yaw, phys.vel, 1.0f, 1.0f, strafe_sign, cmd);
                EXPECT_LE(std::fabs(math::yaw_delta(aa_out.yaw, s.steer_yaw)), 22.5f);
                fwd = s.forwardmove;
                left = s.leftmove;
                cmd_yaw = s.steer_yaw;
                strafe_sign = s.next_strafe_sign;
#endif
                if (!jump_bit) ++seen_air_jump_cleared;
            }
            h.base.set_forwardmove(fwd);
            h.base.set_leftmove(left);
            h.base_va.set(aa_out.pitch, cmd_yaw);
        },
        [&](int /*cmd*/, int /*tick*/, float /*curtime*/) {
            CS2PhysicsEngine::step_tick(phys, h.base_va.y, h.base.forwardmove, h.base.leftmove, bs.cmd_jump);
            peak_speed = std::max(peak_speed, CS2PhysicsEngine::speed_2d(phys));
        });

    EXPECT_GE(seen_sd_bits, 1);
    EXPECT_GE(seen_air_jump_cleared, 10);
    EXPECT_GT(peak_speed, 300.0f);
}

TEST(E2E_Tier4, T4_5_CornerPeekFreestandingAutoAdjustAtTargets) {
    // Grounded in shogod-20261121.log aa: trim= (L2484..2713) + F2.2-F2.5.
    // Wall sits at X in [10, 80], Y in [-30, 10]:
    // - Exposed phase (t < 20, eye = {0, -80, 64}): all 42u probes clear (tucked == false).
    // - Corner phase (t >= 20, eye = {0, 0, 64}, enemy_yaw = 90): center (+Y) & left (205 deg) clear,
    //   while right (-115 deg -> -25 deg -> (+38.07, -17.75, 64)) intersects wall (frac_right < 0.96 -> tucked == true).
    const std::array<CoverAndPoseGeometry::WallAABB, 1> corner{{
        {{10.0f, -30.0f, 0.0f}, {80.0f, 10.0f, 80.0f}},
    }};

    const float base_trim = features::default_pose_head_yaw_offset(static_cast<int>(config::weapon_group::rifle), 89.0f);
    const float enemy_yaw = 90.0f;
    std::vector<float> yaw_history;
    std::vector<float> tucked_yaw_targets;

    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_pitch = config::pitch_mode::down;
    aa.base_yaw = config::yaw_mode::fake_static;
    aa.at_targets = true;
    aa.freestanding = true;
    aa.auto_adjust = true;
    features::freestanding_state fs_state{};

    for (int t = 0; t < 40; ++t) {
        if (t >= 20) aa.base_yaw = config::yaw_mode::jitter;
        const bool tucked_phase = (t >= 20);
        const math::vec3 eye = tucked_phase ? math::vec3{0.0f, 0.0f, 64.0f} : math::vec3{0.0f, -80.0f, 64.0f};
        const float frac_center = CoverAndPoseGeometry::trace_walls(
            eye, eye + math::forward_from_angles({0.0f, enemy_yaw}) * 42.0f, corner);
        const float frac_left = CoverAndPoseGeometry::trace_walls(
            eye, eye + math::forward_from_angles({0.0f, enemy_yaw + 115.0f}) * 42.0f, corner);
        const float frac_right = CoverAndPoseGeometry::trace_walls(
            eye, eye + math::forward_from_angles({0.0f, enemy_yaw - 115.0f}) * 42.0f, corner);

        std::array<features::freestanding_candidate, 2> cands{{
            {115.0f, frac_left, true},
            {-115.0f, frac_right, true},
        }};
        const auto dec = features::select_freestanding_yaw(enemy_yaw, true, frac_center, cands, fs_state);
        if (tucked_phase) {
            EXPECT_FLOAT_EQ(frac_center, 1.0f);
            EXPECT_LT(frac_right, 0.96f);
            EXPECT_FALSE(dec.center_blocked);
            EXPECT_TRUE(dec.tucked);
            tucked_yaw_targets.push_back(dec.selected_yaw);
        } else {
            EXPECT_FALSE(dec.tucked);
        }

        features::antiaim_context ctx{};
        ctx.command_number = 900 + t;
        ctx.found_enemy = true;
        ctx.enemy_yaw = enemy_yaw;
        ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
        ctx.freestanding = dec;
        const math::angles out = features::apply_antiaim_full(aa, {0.0f, 0.0f}, ctx);
        yaw_history.push_back(out.yaw);
    }

    EXPECT_GT(base_trim, 0.0f);
    EXPECT_LE(std::fabs(base_trim), 45.0f);
    // Exposed ticks (0..19) stay steady at 180 + base_trim without 55-deg oscillation.
    for (int t = 0; t < 20; ++t) {
        EXPECT_NEAR(math::yaw_delta(yaw_history[0], yaw_history[static_cast<std::size_t>(t)]), 0.0f, 0.01f);
    }
    // Tucked ticks (20..39) stay anchored around tucked selected_yaw + base_trim while preserving +/-30 deg jitter.
    ASSERT_EQ(tucked_yaw_targets.size(), 20u);
    for (int t = 20; t < 40; ++t) {
        const float expected_center = math::normalize_yaw(
            tucked_yaw_targets[static_cast<std::size_t>(t - 20)] + base_trim);
        EXPECT_NEAR(std::fabs(math::yaw_delta(expected_center, yaw_history[static_cast<std::size_t>(t)])),
                    aa.jitter_range, 0.01f);
        if (t > 20) {
            EXPECT_NEAR(std::fabs(math::yaw_delta(
                            yaw_history[static_cast<std::size_t>(t - 1)],
                            yaw_history[static_cast<std::size_t>(t)])),
                        aa.jitter_range * 2.0f, 0.01f);
        }
    }
}

TEST(E2E_Tier4, T4_6_OnShotBacktrackWindowMissingOldSimTime) {
    // Grounded in log WARN m_flOldSimulationTime + backtrack=200ms, extrap=50ms.
    MultiPlayerHitboxArena::LagTrackSim track;
    constexpr std::uint32_t k_handle = 0x88u;
    const int shot_tick = 1002;
    const float shot_time = static_cast<float>(shot_tick) * TickClock64_300::k_dt;

    // Enemy shoots at t=1002 while peeking, then ducks behind cover t=1003..1010 (8 ticks = 125ms).
    for (int t = 1000; t <= 1010; ++t) {
        const math::vec3 org = (t <= 1002) ? math::vec3{400.0f, 0.0f, 0.0f} : math::vec3{400.0f, 60.0f, -18.0f};
        const auto hbs = MultiPlayerHitboxArena::build_player_17_hitboxes(org);
        track.push(k_handle, t, static_cast<float>(t) * TickClock64_300::k_dt,
                   (t >= shot_tick) ? shot_time : 0.0f, org, hbs);
    }

    const auto sel = track.select(1010, 200.0f, 35.0f, 15.6f);
    bool found_shooting = false;
    for (const auto* r : sel) {
        if (r->shooting && r->simulation_tick == shot_tick) {
            found_shooting = true;
            // Verify head hitbox at shot_tick is still at exposed origin (y=0, z=65).
            EXPECT_NEAR(r->hitboxes[0].center.y, 0.0f, 1.0f);
        }
    }
    EXPECT_TRUE(found_shooting);

#if SHOGOD_E2E_ENABLE_M1_CONTRACTS
    features::lag_compensation lc;
    lc.reset();
    constexpr std::uintptr_t k_pawn = 0x400000ULL;
    for (int t = 1000; t <= 1010; ++t) {
        const math::vec3 org = (t <= 1002) ? math::vec3{400.0f, 0.0f, 0.0f} : math::vec3{400.0f, 60.0f, -18.0f};
        sdk::live_hitbox_set hb_set{};
        hb_set.geometries = MultiPlayerHitboxArena::build_player_17_hitboxes(org);
        hb_set.simulation_tick = t;
        hb_set.simulation_time = static_cast<float>(t) * TickClock64_300::k_dt;
        hb_set.valid = true;
        ASSERT_TRUE(lc.push_record(3, k_pawn, k_handle, t, hb_set.simulation_time, org,
                                   std::move(hb_set), (t >= shot_tick) ? shot_time : 0.0f, t >= shot_tick));
    }
    std::vector<const features::lag_record*> lc_sel;
    lc.select(k_pawn, k_handle, 1010, 200.0f, lc_sel);
    bool lc_found_shot = false;
    for (const auto* r : lc_sel) {
        if (r->shooting && r->simulation_tick == shot_tick) {
            lc_found_shot = true;
            EXPECT_NEAR(r->hitboxes.geometries[0].center.y, 0.0f, 1.0f);
        }
    }
    EXPECT_TRUE(lc_found_shot);
#endif
}

TEST(E2E_Tier4, T4_7_MidCombatMenuOpenFocusLossAndSaveReentry) {
    // Grounded in shogod-20261121.log L2818-2822 (menu open/edit/close/save).
    config::store s = DefaultJsonConfigFixture::load();
    config::bind_state rage_bind;
    features::bhop_state bs{};
    features::shot_delay sd{};

    // Active combat ticks 0..9
    for (int t = 0; t < 10; ++t) {
        (void)features::step_bhop(bs, 800 + t, (t % 5) == 0, true);
        (void)sd.ready(0x1u, 0x2u, 100 + t, s.get().rage.weapons[4], 80.0f, 80.0f);
    }

    // Menu opens on VK_INSERT (menu_key == 45): accepts_input becomes false, movement/delay reset.
    EXPECT_EQ(s.get().ui.menu_key, 45);
    EXPECT_TRUE(rage_bind.sample({}, true, false, false));
    bs = {};
    sd = {};
    UserCmdHarness h;
    h.begin_tick(810, 1010, {10.0f, 45.0f}, 0ULL, 0.0f, 0.0f);
    EXPECT_EQ(h.cmd.button_state.held & (sdk::k_in_attack | sdk::k_in_jump), 0ULL);
    EXPECT_EQ(sd.ticks, 0);

    // Edit anti_aim.enabled = true and base_pitch = off while in menu, save & reload, resume tick 811.
    s.get().anti_aim.enabled = true;
    s.get().anti_aim.base_pitch = config::pitch_mode::off;
    const std::string json_saved = s.to_json();
    config::store reloaded;
    ASSERT_TRUE(reloaded.from_json(json_saved));
    EXPECT_TRUE(reloaded.get().anti_aim.enabled);
    EXPECT_EQ(reloaded.get().anti_aim.base_pitch, config::pitch_mode::off);

    const math::angles resumed = features::apply_antiaim(
        reloaded.get().anti_aim, {14.7f, 0.0f}, {true, false, false, 811, 0});
    EXPECT_FLOAT_EQ(resumed.pitch, 14.7f);
    EXPECT_NEAR(math::yaw_delta(180.0f, resumed.yaw), 0.0f, 0.01f);
}

TEST(E2E_Tier4, T4_8_300HzBhopSubframePredictionAndAirStrafeContinuity) {
    // Reproduces the exact 300 Hz sub-frame prediction loop + airborne AA (auto_adjust + at_targets)
    // from shogod-20261121.log L3200-3219:
    //   1. Sub-frame 0 lands on ground (on_ground = true) and predicts jump in-place (on_ground -> false).
    //   2. Sub-frames 1..4 of the same command_number see raw_on_ground = false, yet latched on_ground
    //      and step_bhop preserve the jump across all 5 sub-frames so SendMove never drops the jump.
    //   3. While airborne with Backward AA + auto_adjust + at_targets, compute_runtime_air_strafe
    //      accelerates cleanly above 300 u/s with zero 45-degree yaw snaps and zero toggle/scroll noise.
    config::store cfg_store = DefaultJsonConfigFixture::load();
    const config::anti_aim_cfg& aa = cfg_store.get().anti_aim;

    CS2PhysicsState phys{};
    phys.vel = {250.0f, 0.0f, 0.0f};
    phys.on_ground = true;

    features::createmove_state st{};
    UserCmdHarness h;
    TickClock64_300 clock;

    int jumps_sent = 0;
    int max_yaw_step_violations = 0;
    int dirty_move_toggle_bits = 0;
    float prev_air_yaw = 0.0f;
    bool had_prev_air_yaw = false;
    float cam_yaw = -93.6f;

    clock.run_ticks(64, 5,
        [&](int cmd, int tick, float /*curtime*/, int sf, int /*rf*/) {
            // Simulate Sub-frame 0 ClientPrediction mutating raw pawn FL_ONGROUND in-place on landing ticks.
            const bool raw_pawn_on_ground = (sf == 0) ? phys.on_ground : false;
            if (st.tick_latch.command_number != cmd) {
                st.tick_latch.command_number = cmd;
                st.tick_latch.on_ground = raw_pawn_on_ground;
                st.tick_latch.velocity = phys.vel;
                st.tick_latch.speed_2d = CS2PhysicsEngine::speed_2d(phys);
                st.tick_latch.jump_held = true;
            }
            const bool latched_on_ground = st.tick_latch.on_ground;
            const bool do_jump = features::step_bhop(st.bhop, cmd, latched_on_ground, st.tick_latch.jump_held);

            h.begin_tick(cmd, tick, {11.0f, cam_yaw},
                         sdk::k_in_jump | sdk::k_in_forward, 1.0f, 0.0f);
            // Inject dirty transition bits as CCSGOInput::CreateMove[5] table 0x2227fb0 does after button rotation.
            h.cmd.button_state.toggle |= features::k_move_button_mask;
            h.cmd.button_state.scroll |= features::k_move_button_mask;

            h.cmd.button_state.set_button_state(
                sdk::k_in_jump,
                do_jump ? sdk::in_button_state::up_down : sdk::in_button_state::up);

            if (!latched_on_ground) {
                features::antiaim_context aa_ctx{};
                aa_ctx.on_ground = false;
                aa_ctx.has_move_input = true;
                aa_ctx.speed_2d = st.tick_latch.speed_2d;
                aa_ctx.command_number = cmd;
                aa_ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
                aa_ctx.weapon_type = 3;
                const math::angles aa_air = features::apply_antiaim_full(aa, {11.0f, cam_yaw}, aa_ctx);
                const auto strafe = features::compute_runtime_air_strafe(
                    st, cmd, cam_yaw, aa_air.yaw, st.tick_latch.velocity, 1.0f, 0.0f);
                EXPECT_TRUE(strafe.active);
                EXPECT_LE(std::fabs(math::yaw_delta(aa_air.yaw, strafe.steer_yaw)), 27.51f);
                h.base.viewangles->set(aa_air.pitch, strafe.steer_yaw, 0.0f);
                h.base.set_forwardmove(strafe.forwardmove);
                h.base.set_leftmove(strafe.leftmove);
                features::sync_move_held_buttons(&h.cmd, strafe.forwardmove, strafe.leftmove);
            } else {
                had_prev_air_yaw = false;
                float qf = 0.0f, ql = 0.0f;
                const math::vec3 wish = features::compute_wish_world_dir(cam_yaw, 1.0f, 0.0f);
                const float ground_yaw = math::normalize_yaw(cam_yaw + 180.0f + 36.0f);
                h.base.viewangles->set(89.0f, ground_yaw, 0.0f);
                features::compute_quantized_ground_move(wish, ground_yaw, st.ground_move, cmd, qf, ql);
                h.base.set_forwardmove(qf);
                h.base.set_leftmove(ql);
                features::sync_move_held_buttons(&h.cmd, qf, ql);
            }

            if ((h.cmd.button_state.toggle & features::k_move_button_mask) != 0 ||
                (h.cmd.button_state.scroll & features::k_move_button_mask) != 0) {
                ++dirty_move_toggle_bits;
            }
        },
        [&](int /*cmd*/, int /*tick*/, float /*curtime*/) {
            const bool jump_pressed = (h.cmd.button_state.held & sdk::k_in_jump) != 0;
            if (phys.on_ground && jump_pressed) ++jumps_sent;
            if (!phys.on_ground) {
                if (had_prev_air_yaw) {
                    if (std::fabs(math::yaw_delta(prev_air_yaw, h.base.viewangles->y)) > 15.0f) {
                        ++max_yaw_step_violations;
                    }
                }
                prev_air_yaw = h.base.viewangles->y;
                had_prev_air_yaw = true;
            }
            CS2PhysicsEngine::step_tick(phys, h.base.viewangles->y, h.base.forwardmove,
                                        h.base.leftmove, jump_pressed);
            cam_yaw = math::normalize_yaw(cam_yaw + 0.8f);
        });

    EXPECT_GE(jumps_sent, 1);
    EXPECT_EQ(max_yaw_step_violations, 0);
    EXPECT_EQ(dirty_move_toggle_bits, 0);
    EXPECT_GT(CS2PhysicsEngine::speed_2d(phys), 295.0f);
}

TEST(hvh_e2e, subtick_autostrafe_with_antiaim_locked_yaw_and_subframes) {
    config::anti_aim_cfg aa{};
    aa.enabled = true;
    aa.base_pitch = config::pitch_mode::down;
    aa.base_yaw = config::yaw_mode::fake_static;
    aa.auto_adjust = true;
    aa.on_air = true;
    aa.on_move = true;

    TickClock64_300 clock;
    UserCmdHarness h;
    features::createmove_state st{};
    math::vec3 vel{250.0f, 0.0f, 0.0f};
    float cam_yaw = 15.0f;
    int aa_mismatch = 0;

    clock.run_ticks(32, 4,
        [&](int cmd, int tick, float /*curtime*/, int sf, int /*rf*/) {
            if (st.tick_latch.command_number != cmd) {
                st.tick_latch.command_number = cmd;
                st.tick_latch.on_ground = false;
                st.tick_latch.velocity = vel;
            }
            features::antiaim_context aa_ctx{};
            aa_ctx.on_ground = false;
            aa_ctx.has_move_input = true;
            aa_ctx.speed_2d = std::hypot(vel.x, vel.y);
            aa_ctx.command_number = cmd;
            aa_ctx.weapon_group = static_cast<int>(config::weapon_group::rifle);
            aa_ctx.weapon_type = 3;
            const math::angles aa_ang = features::apply_antiaim_full(aa, {5.0f, cam_yaw}, aa_ctx);

            h.begin_tick(cmd, tick, {5.0f, cam_yaw}, sdk::k_in_forward, 1.0f, 0.0f);
            features::subtick_strafe_input sin{};
            sin.command_number = cmd;
            sin.camera_yaw = cam_yaw;
            sin.move_ref_yaw = aa_ang.yaw;
            sin.velocity = st.tick_latch.velocity;
            sin.buttons = sdk::k_in_forward;
            sin.available_slots = 16;

            const auto plan = features::build_subtick_autostrafe_plan(st, sin);
            EXPECT_TRUE(plan.active);
            // Zero AA distortion across all sub-frames (0..3).
            if (std::fabs(math::yaw_delta(aa_ang.yaw, plan.lock_view_yaw)) > 0.01f) {
                ++aa_mismatch;
            }
            if (sf == 0) vel = plan.simulated_velocity;
            h.base.viewangles->set(aa_ang.pitch, plan.lock_view_yaw, 0.0f);
            h.base.set_forwardmove(plan.final_forwardmove);
            h.base.set_leftmove(plan.final_leftmove);
            features::sync_move_held_buttons(&h.cmd, plan.final_forwardmove, plan.final_leftmove);
        },
        [&](int /*cmd*/, int /*tick*/, float /*curtime*/) {
            cam_yaw = math::normalize_yaw(cam_yaw + 0.5f);
        });

    EXPECT_EQ(aa_mismatch, 0);
    EXPECT_GT(std::hypot(vel.x, vel.y), 350.0f);
}

TEST(hvh_e2e, bhop_landing_subtick_prediction_eliminates_ground_stick_and_universal_binds_override) {
    // 1. Simulate a falling trajectory crossing z = 0 mid-tick (so FL_ONGROUND is still 0 at tick start).
    // predict_landing_subtick_frac must detect the exact landing tick and return a valid subtick frac in (0, 1).
    float z = 18.0f;
    float vz = -120.0f;
    constexpr float k_dt = 1.0f / 64.0f;
    constexpr float k_g = 800.0f;
    int predicted_landing_tick = -1;
    float predicted_when = -1.0f;
    int actual_ground_tick = -1;

    for (int t = 0; t < 16; ++t) {
        const float frac = features::predict_landing_subtick_frac(z, vz, k_g);
        if (frac >= 0.0f && predicted_landing_tick < 0) {
            predicted_landing_tick = t;
            predicted_when = frac;
        }
        const float next_z = z + vz * k_dt - 0.5f * k_g * k_dt * k_dt;
        vz -= k_g * k_dt;
        z = next_z;
        if (z <= 0.0f && actual_ground_tick < 0) {
            actual_ground_tick = t;
            break;
        }
    }

    ASSERT_GE(actual_ground_tick, 0);
    EXPECT_EQ(predicted_landing_tick, actual_ground_tick);
    EXPECT_GT(predicted_when, 0.0f);
    EXPECT_LT(predicted_when, 1.0f);

    // 2. Verify universal Neverlose keybind overrides drive live Anti-Aim & Ragebot resolution.
    config::config_v1 base{};
    base.anti_aim.enabled = true;
    base.anti_aim.base_yaw = config::yaw_mode::fake_static;
    base.anti_aim.yaw_offset = 0.0f;
    base.anti_aim.auto_adjust = false;
    base.rage.weapons[static_cast<std::size_t>(config::weapon_group::scout)].min_damage = 101.0f;

    base.keybinds.push_back({"aa.yaw_offset", 0x5A, config::bind_mode::toggle, true, -90.0f});
    base.keybinds.push_back({"rage.min_damage", 0x56, config::bind_mode::hold, true, 10.0f});

    bool key_z = false;
    bool key_v = false;
    auto read_key = [&](int vk) noexcept { return (vk == 0x5A) ? key_z : ((vk == 0x56) ? key_v : false); };

    config::keybind_runtime rt;
    rt.poll(base.keybinds, read_key, true, 0x2D);  // arm
    key_z = true;
    key_v = true;
    rt.poll(base.keybinds, read_key, true, 0x2D);  // activate both

    config::config_v1 eff = base;
    std::vector<config::active_bind_display> hud;
    rt.apply_overrides(eff, base.keybinds, &hud);

    features::antiaim_context ctx{};
    ctx.command_number = 900;
    const math::angles base_aa = features::apply_antiaim_full(base.anti_aim, {0.0f, 0.0f}, ctx);
    const math::angles eff_aa = features::apply_antiaim_full(eff.anti_aim, {0.0f, 0.0f}, ctx);
    EXPECT_NEAR(math::yaw_delta(base_aa.yaw, eff_aa.yaw), -90.0f, 0.5f);

    const auto& scout = eff.rage.resolve_weapon(40, static_cast<int>(config::weapon_group::scout));
    EXPECT_FLOAT_EQ(scout.min_damage, 10.0f);
    EXPECT_EQ(hud.size(), 2u);
}
