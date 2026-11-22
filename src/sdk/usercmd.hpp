#pragma once

// Exact CS2 CUserCommand / CInButtonState / CCSGOInput and zero-dependency
// protobuf message views matching client.dll's compiled protobuf layout.

#include "features/math/angles.hpp"
#include "features/math/vec3.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>

namespace shogod::sdk {

inline constexpr std::uint64_t k_in_attack = 1ULL << 0;
inline constexpr std::uint64_t k_in_jump = 1ULL << 1;
inline constexpr std::uint64_t k_in_duck = 1ULL << 2;
inline constexpr std::uint64_t k_in_forward = 1ULL << 3;
inline constexpr std::uint64_t k_in_back = 1ULL << 4;
inline constexpr std::uint64_t k_in_use = 1ULL << 5;
inline constexpr std::uint64_t k_in_moveleft = 1ULL << 9;
inline constexpr std::uint64_t k_in_moveright = 1ULL << 10;
inline constexpr std::uint64_t k_in_attack2 = 1ULL << 11;

template <typename T>
struct repeated_ptr_field {
    struct rep_t {
        int max;
        T* elements[(INT_MAX - 2 * static_cast<int>(sizeof(int))) / static_cast<int>(sizeof(void*))];
    };

    void* arena;
    int current_size;
    int capacity;
    rep_t* ss;
};

struct msg_qangle_pb {
    void* vtable;
    void* internal_metadata;
    std::uint32_t has_bits;
    int cached_size;
    float x;
    float y;
    float z;

    void set(float pitch, float yaw, float roll = 0.0f) noexcept {
        has_bits |= 0x7u;
        x = pitch;
        y = yaw;
        z = roll;
    }
};
static_assert(offsetof(msg_qangle_pb, x) == 0x18);

struct in_button_state_pb {
    void* vtable;
    void* internal_metadata;
    std::uint32_t has_bits;
    int cached_size;
    std::uint64_t buttonstate1;
    std::uint64_t buttonstate2;
    std::uint64_t buttonstate3;

    void set_all(std::uint64_t held, std::uint64_t toggle, std::uint64_t scroll) noexcept {
        has_bits |= 0x7u;
        buttonstate1 = held;
        buttonstate2 = toggle;
        buttonstate3 = scroll;
    }
};
static_assert(offsetof(in_button_state_pb, buttonstate1) == 0x18);

struct subtick_move_step_pb {
    void* vtable;
    void* internal_metadata;
    std::uint32_t has_bits;
    int cached_size;
    std::uint64_t button;
    bool pressed;
    float when;
    float analog_forward_delta;
    float analog_left_delta;
    float pitch_delta;
    float yaw_delta;

    static constexpr std::uint32_t k_view_delta_bits = 0x60u;

    void clear() noexcept {
        has_bits = 0;
        button = 0;
        pressed = false;
        when = 0.0f;
        analog_forward_delta = 0.0f;
        analog_left_delta = 0.0f;
        pitch_delta = 0.0f;
        yaw_delta = 0.0f;
    }

    void clear_view_deltas() noexcept {
        has_bits &= ~k_view_delta_bits;
        pitch_delta = 0.0f;
        yaw_delta = 0.0f;
    }

    void set_button(std::uint64_t b) noexcept { has_bits |= 0x01u; button = b; }
    void set_pressed(bool p) noexcept { has_bits |= 0x02u; pressed = p; }
    void set_when(float w) noexcept { has_bits |= 0x04u; when = w; }
    void set_analog_forward_delta(float v) noexcept { has_bits |= 0x08u; analog_forward_delta = v; }
    void set_analog_left_delta(float v) noexcept { has_bits |= 0x10u; analog_left_delta = v; }
    void set_pitch_delta(float v) noexcept { has_bits |= 0x20u; pitch_delta = v; }
    void set_yaw_delta(float v) noexcept { has_bits |= 0x40u; yaw_delta = v; }
};
static_assert(offsetof(subtick_move_step_pb, button) == 0x18);
static_assert(offsetof(subtick_move_step_pb, when) == 0x24);
static_assert(offsetof(subtick_move_step_pb, yaw_delta) == 0x34);

struct base_user_cmd_pb {
    void* vtable;
    void* internal_metadata;
    std::uint32_t has_bits;
    int cached_size;
    repeated_ptr_field<subtick_move_step_pb> subtick_moves;
    void* move_crc_ptr;
    in_button_state_pb* buttons_pb;
    msg_qangle_pb* viewangles;
    void* execution_notes;
    std::int32_t legacy_command_number;
    std::int32_t client_tick;
    float forwardmove;
    float leftmove;
    float upmove;
    std::int32_t impulse;
    std::int32_t weaponselect;
    std::int32_t random_seed;
    std::int32_t mousedx;
    std::int32_t mousedy;
    std::uint32_t prediction_offset_ticks_x256;
    std::uint32_t consumed_server_angle_changes;
    std::int32_t cmd_flags;
    std::uint32_t pawn_entity_handle;

    void set_forwardmove(float v) noexcept { has_bits |= 0x40u; forwardmove = v; }
    void set_leftmove(float v) noexcept { has_bits |= 0x80u; leftmove = v; }
    void set_upmove(float v) noexcept { has_bits |= 0x100u; upmove = v; }
    void clear_subtick_moves() noexcept { subtick_moves.current_size = 0; }
};
static_assert(offsetof(base_user_cmd_pb, subtick_moves) == 0x18);
static_assert(offsetof(base_user_cmd_pb, buttons_pb) == 0x38);
static_assert(offsetof(base_user_cmd_pb, viewangles) == 0x40);
static_assert(offsetof(base_user_cmd_pb, forwardmove) == 0x58);
static_assert(offsetof(base_user_cmd_pb, leftmove) == 0x5C);

struct csgo_input_history_entry_pb {
    void* vtable;
    void* internal_metadata;
    std::uint32_t has_bits;
    int cached_size;
    msg_qangle_pb* view_angles;
    void* cl_interp;
    void* sv_interp0;
    void* sv_interp1;
    void* player_interp;
    void* shoot_position;
    void* target_head_pos_check;
    void* target_abs_pos_check;
    void* target_abs_ang_check;
    std::int32_t render_tick_count;
    float render_tick_fraction;
    std::int32_t player_tick_count;
    float player_tick_fraction;
    std::int32_t frame_number;
    std::int32_t target_ent_index;

    void set_render_tick_count(std::int32_t v) noexcept { has_bits |= 0x200u; render_tick_count = v; }
    void set_render_tick_fraction(float v) noexcept { has_bits |= 0x400u; render_tick_fraction = v; }
    void set_player_tick_count(std::int32_t v) noexcept { has_bits |= 0x800u; player_tick_count = v; }
    void set_player_tick_fraction(float v) noexcept { has_bits |= 0x1000u; player_tick_fraction = v; }
};
static_assert(offsetof(csgo_input_history_entry_pb, view_angles) == 0x18);
static_assert(offsetof(csgo_input_history_entry_pb, render_tick_count) == 0x60);
static_assert(offsetof(csgo_input_history_entry_pb, player_tick_count) == 0x68);

struct csgo_user_cmd_pb {
    void* vtable;
    void* internal_metadata;
    std::uint32_t has_bits;
    int cached_size;
    repeated_ptr_field<csgo_input_history_entry_pb> input_history;
    base_user_cmd_pb* base;
    bool left_hand_desired;
    bool is_predicting_body_shot_fx;
    bool is_predicting_head_shot_fx;
    bool is_predicting_kill_ragdolls;
    std::int32_t attack1_start_history_index;
    std::int32_t attack2_start_history_index;

    void set_attack1_start_history_index(std::int32_t idx) noexcept {
        has_bits |= 0x20u;
        attack1_start_history_index = idx;
    }
    void clear_attack1_start_history_index() noexcept {
        has_bits &= ~0x20u;
        attack1_start_history_index = -1;
    }
    void set_attack2_start_history_index(std::int32_t idx) noexcept {
        has_bits |= 0x40u;
        attack2_start_history_index = idx;
    }
    void clear_attack2_start_history_index() noexcept {
        has_bits &= ~0x40u;
        attack2_start_history_index = -1;
    }
};
static_assert(offsetof(csgo_user_cmd_pb, input_history) == 0x18);
static_assert(offsetof(csgo_user_cmd_pb, base) == 0x30);
static_assert(offsetof(csgo_user_cmd_pb, attack1_start_history_index) == 0x3C);
static_assert(sizeof(csgo_user_cmd_pb) == 0x48);

struct in_button_state {
    void* vtable;
    std::uint64_t held;
    std::uint64_t toggle;
    std::uint64_t scroll;

    enum mode : std::int8_t {
        up = 0,
        down = 1,
        down_up = 2,
        up_down = 3,
    };

    void set_button(std::uint64_t button, bool pressed) noexcept {
        const std::uint64_t changed = (held ^ (pressed ? button : 0ULL)) & button;
        scroll |= toggle & changed;
        toggle ^= changed;
        held ^= changed;
    }

    void set_button_state(std::uint64_t value, mode state) noexcept {
        switch (state) {
            case up:
                held &= ~value;
                toggle &= ~value;
                scroll &= ~value;
                break;
            case down:
                held |= value;
                toggle &= ~value;
                scroll &= ~value;
                break;
            case down_up:
                held &= ~value;
                toggle |= value;
                scroll &= ~value;
                break;
            case up_down:
                held |= value;
                toggle |= value;
                scroll &= ~value;
                break;
        }
    }
};
static_assert(sizeof(in_button_state) == 0x20);

struct user_command {
    void* vtable;
    int command_number;
    float simulation_time;
    csgo_user_cmd_pb pb;
    in_button_state button_state;
    int target_history_index;
    double creation_time;
    int server_tick;
    bool has_been_predicted;
    bool has_been_predicted_shoot_weapon_timings;
    int last_prediction_reason;
    int next_prediction_reason;
};
static_assert(sizeof(user_command) == 0x98);

struct user_command_manager {
    user_command commands[150];
    std::uint32_t command_number;

    user_command* get_user_command(int seq = -1) noexcept {
        if (seq == -1) seq = static_cast<int>(command_number);
        if (seq < 0) return nullptr;
        return &commands[seq % 150];
    }
};
static_assert(sizeof(user_command_manager) == 0x5918);

struct button_data {
    std::uint64_t button_flag;
    char* button_name;
    char pad0[8];
    float old_button_fraction;
    char pad1[4];
    float button_pressed_when;
    char pad2[4];
    std::int32_t button_when_start;
    std::int32_t button_pressed_tick;
    bool pressed;
    bool pressed_in_previous_tick;
    char pad3[1];
    bool unpressed;
    char pad4[20];
};

// Partial view of CCSGOInput for fields verified against client.dll.
struct csgo_input_view {
    static constexpr std::size_t k_button_attack_offset = 0x10;
    static constexpr std::size_t k_button_attack2_offset = 0x68;
    static constexpr std::size_t k_viewangles_offset = 0x688;

    button_data* button_attack() const noexcept {
        return *reinterpret_cast<button_data* const*>(reinterpret_cast<const char*>(this) + k_button_attack_offset);
    }
    button_data* button_attack2() const noexcept {
        return *reinterpret_cast<button_data* const*>(reinterpret_cast<const char*>(this) + k_button_attack2_offset);
    }
    math::angles viewangles() const noexcept {
        const float* p = reinterpret_cast<const float*>(reinterpret_cast<const char*>(this) + k_viewangles_offset);
        return {p[0], p[1]};
    }
    void set_viewangles(const math::angles& a) noexcept {
        float* p = reinterpret_cast<float*>(reinterpret_cast<char*>(this) + k_viewangles_offset);
        p[0] = a.pitch;
        p[1] = a.yaw;
        p[2] = 0.0f;
    }
};

struct anchors;

user_command_manager* get_command_manager(const anchors& a, std::uintptr_t controller) noexcept;
subtick_move_step_pb* add_subtick_move_step(const anchors& a, user_command* cmd, float when) noexcept;
bool set_fire_angles(const anchors& a, user_command* cmd, const math::angles& angle,
                     const math::angles& recoil) noexcept;
void sync_buttons_pb(user_command* cmd) noexcept;

[[nodiscard]] inline subtick_move_step_pb* try_reuse_subtick_move_step(
    repeated_ptr_field<subtick_move_step_pb>& field) noexcept {
    if (!field.ss || field.current_size < 0 || field.current_size >= 32) return nullptr;
    if (field.current_size < field.capacity && field.current_size < field.ss->max) {
        if (auto* existing = field.ss->elements[field.current_size]) {
            ++field.current_size;
            return existing;
        }
    }
    return nullptr;
}

inline void sort_last_subtick_move_step(
    repeated_ptr_field<subtick_move_step_pb>& field,
    subtick_move_step_pb* step, float when) noexcept {
    if (!step || !field.ss || field.current_size <= 0 || field.current_size > field.ss->max) {
        return;
    }
    subtick_move_step_pb** elements = field.ss->elements;
    int idx = field.current_size - 1;
    while (idx > 0 && elements[idx - 1] && elements[idx - 1]->when > when) {
        elements[idx] = elements[idx - 1];
        --idx;
    }
    elements[idx] = step;
}

inline void clear_subtick_view_deltas(base_user_cmd_pb* base) noexcept {
    if (!base || !base->subtick_moves.ss) return;
    const auto& field = base->subtick_moves;
    if (field.current_size <= 0 || field.capacity <= 0 || field.ss->max <= 0) return;
    int count = field.current_size;
    if (field.capacity < count) count = field.capacity;
    if (field.ss->max < count) count = field.ss->max;
    for (int i = 0; i < count; ++i) {
        if (auto* step = field.ss->elements[i]) {
            step->clear_view_deltas();
        }
    }
}

inline bool apply_fire_angles(user_command* cmd, const math::angles& angle) noexcept {
    if (!cmd || !cmd->pb.base || !cmd->pb.base->viewangles) return false;
    auto* base = cmd->pb.base;
    auto* va = base->viewangles;
    const float pitch = math::clamp_pitch(angle.pitch);
    const float yaw = math::normalize_yaw(angle.yaw);
    clear_subtick_view_deltas(base);
    va->set(pitch, yaw, 0.0f);
    return true;
}

}  // namespace shogod::sdk
