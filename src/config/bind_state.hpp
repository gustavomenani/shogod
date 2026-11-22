#pragma once

#include "config/config.hpp"

namespace shogod::config {

enum class bound_feature : unsigned {
    rage,
    anti_aim,
    esp,
    third_person,
    bhop,
    auto_strafe,
    peek_assist,
    count
};

static_assert(static_cast<unsigned>(bound_feature::count) <= 32,
              "bound_feature exceeds std::uint32_t bitmask");

constexpr std::uint32_t feature_bit(bound_feature feature) noexcept {
    return 1u << static_cast<unsigned>(feature);
}

struct bind_state {
    [[nodiscard]] bool sample(const keybind& bind, bool enabled, bool accepts_input, bool down) noexcept {
        if (bind.vk != vk_ || bind.hold != hold_ || !enabled) {
            vk_ = bind.vk;
            hold_ = bind.hold;
            toggled_ = false;
            held_ = false;
            previous_down_ = true;
        }
        if (!enabled || !bind.bound()) return enabled;
        if (!accepts_input) {
            held_ = false;
            previous_down_ = true;
            return !hold_ && toggled_;
        }
        if (!down) {
            previous_down_ = false;
            held_ = false;
            return !hold_ && toggled_;
        }
        if (!previous_down_) {
            if (!hold_) toggled_ = !toggled_;
            held_ = true;
        }
        previous_down_ = true;
        return hold_ ? held_ : toggled_;
    }

private:
    int vk_ = 0;
    bool hold_ = false;
    bool toggled_ = false;
    bool previous_down_ = true;
    bool held_ = false;
};

}  // namespace shogod::config
