#include "sdk/usercmd.hpp"

#include "sdk/signatures.hpp"

#include <cmath>
#include <windows.h>

namespace shogod::sdk {

user_command_manager* get_command_manager(const anchors& a, std::uintptr_t controller) noexcept {
    if (a.get_command_manager == 0 || controller == 0) return nullptr;
    using fn_t = user_command_manager* (*)(std::uintptr_t);
    __try {
        return reinterpret_cast<fn_t>(a.get_command_manager)(controller);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

subtick_move_step_pb* add_subtick_move_step(const anchors& a, user_command* cmd, float when) noexcept {
    if (!cmd || !cmd->pb.base) return nullptr;
    auto& field = cmd->pb.base->subtick_moves;
    if (field.current_size < 0 || field.current_size >= 32) return nullptr;

    subtick_move_step_pb* step = nullptr;
    __try {
        step = try_reuse_subtick_move_step(field);
        if (!step && a.create_subtick_move != 0 && a.add_element_to_rep != 0) {
            using create_fn = subtick_move_step_pb* (*)(void*);
            using add_fn = subtick_move_step_pb* (*)(repeated_ptr_field<subtick_move_step_pb>*, void*);
            subtick_move_step_pb* fresh = reinterpret_cast<create_fn>(a.create_subtick_move)(field.arena);
            if (fresh) step = reinterpret_cast<add_fn>(a.add_element_to_rep)(&field, fresh);
        }
        if (!step || !field.ss || field.current_size <= 0 || field.current_size > field.ss->max) {
            return nullptr;
        }

        step->clear();
        step->set_when(when);

        sort_last_subtick_move_step(field, step, when);
        return step;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool set_fire_angles(const anchors&, user_command* cmd, const math::angles& angle,
                     const math::angles&) noexcept {
    // Keep base_cmd->viewangles valid (clamped pitch) matching fire_angle while
    // SendMove preserves csgo_input->current_move_data.viewangles for silent aim.
    return apply_fire_angles(cmd, angle);
}

void sync_buttons_pb(user_command* cmd) noexcept {
    if (!cmd || !cmd->pb.base || !cmd->pb.base->buttons_pb) return;
    cmd->pb.base->buttons_pb->set_all(cmd->button_state.held, cmd->button_state.toggle,
                                      cmd->button_state.scroll);
}

}  // namespace shogod::sdk
