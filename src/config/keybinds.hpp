#pragma once

// Universal Neverlose-style keybind & override system (adapted from OverlayMenu).
// Any registered boolean, slider, or choice setting can have one or more keybinds
// (Toggle / Hold / Always) with an override value and Keybinds HUD visibility.

#include "config/config.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shogod::config {

inline constexpr std::size_t k_max_menu_keybinds = 128;

enum class bind_value_kind : int {
    boolean = 0,
    floating = 1,
    integer = 2,
    choice = 3,
    min_damage = 4,
};

struct bind_target_spec {
    const char* id = "";
    const char* label = "";
    const char* category = "";  // e.g. "Ragebot", "Anti-Aim", "Players", "World", "Misc"
    int tab_index = 0;          // matches ui::tab index for instant search navigation
    bind_value_kind kind = bind_value_kind::boolean;
    float min_val = 0.0f;
    float max_val = 1.0f;
    float default_override = 1.0f;
    const char* const* choices = nullptr;
    int choice_count = 0;
};

[[nodiscard]] std::span<const bind_target_spec> bind_target_catalog() noexcept;
[[nodiscard]] const bind_target_spec* find_bind_target(std::string_view id) noexcept;

[[nodiscard]] std::string vk_name(int vk);
[[nodiscard]] std::string vk_short_name(int vk);
[[nodiscard]] std::string format_bind_override(const bind_target_spec& spec, float value);

struct active_bind_display {
    std::string label;
    std::string value;
    bind_mode mode = bind_mode::toggle;
};

class keybind_runtime {
public:
    struct entry_state {
        int vk = 0;
        bind_mode mode = bind_mode::toggle;
        bool down = false;
        bool armed = false;
        bool toggled = false;
        bool active = false;
    };

    template <typename KeyReader>
    void poll(const std::vector<menu_keybind>& binds, KeyReader&& is_key_down, bool accepts_input,
              int menu_vk) {
        if (states_.size() != binds.size()) {
            states_.assign(binds.size(), entry_state{});
        }
        for (std::size_t i = 0; i < binds.size(); ++i) {
            const menu_keybind& b = binds[i];
            entry_state& s = states_[i];
            const bool valid_key = (b.vk >= 1 && b.vk <= 254 && b.vk != menu_vk);
            const bool down = (b.mode != bind_mode::always) && valid_key && accepts_input &&
                              is_key_down(b.vk);

            if (s.vk != b.vk || s.mode != b.mode) {
                s = entry_state{};
                s.vk = b.vk;
                s.mode = b.mode;
                s.down = down;
                s.armed = !down;
            }

            if (b.mode == bind_mode::always) {
                s.active = true;
                s.down = false;
                continue;
            }
            if (!valid_key) {
                s.active = false;
                s.toggled = false;
                s.down = false;
                s.armed = false;
                continue;
            }
            if (!accepts_input) {
                if (b.mode == bind_mode::hold) s.active = false;
                s.down = false;
                s.armed = false;
                continue;
            }
            if (!s.armed) {
                s.armed = !down;
                s.down = down;
                s.active = (b.mode == bind_mode::toggle) ? s.toggled : false;
                continue;
            }
            if (b.mode == bind_mode::toggle) {
                if (down && !s.down) s.toggled = !s.toggled;
                s.active = s.toggled;
            } else {
                s.active = down;
            }
            s.down = down;
        }
    }

    [[nodiscard]] bool is_active(std::size_t index) const noexcept {
        return index < states_.size() && states_[index].active;
    }

    [[nodiscard]] bool any_active_for(const std::vector<menu_keybind>& binds,
                                      std::string_view target) const noexcept {
        const std::size_t n = std::min(binds.size(), states_.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (states_[i].active && binds[i].target == target) return true;
        }
        return false;
    }

    void reset() noexcept { states_.clear(); }

    // Applies all currently active keybind overrides onto `effective` config copy.
    void apply_overrides(config_v1& effective, const std::vector<menu_keybind>& binds,
                         std::vector<active_bind_display>* out_hud = nullptr) const;

private:
    std::vector<entry_state> states_;
};

// Direct field reader/writer for a single target (used by override engine & unit tests).
void apply_single_override(config_v1& cfg, std::string_view target_id, float value) noexcept;

}  // namespace shogod::config
