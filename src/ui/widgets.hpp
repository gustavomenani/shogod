#pragma once

// Custom controls. Everything here draws with the window draw list so the menu
// does not inherit default ImGui chrome; every control is bound to a config
// field by the page that calls it.

#include <cstdint>

#include "config/config.hpp"
#include "imgui.h"

namespace shogod::ui::widgets {

void section(const char* title);
void tip(const char* text);              // call right after the item
bool toggle(const char* label, bool* value, const char* tooltip = nullptr);
bool slider_float(const char* label, float* value, float lo, float hi, const char* tooltip = nullptr);
bool slider_int(const char* label, int* value, int lo, int hi, const char* tooltip = nullptr);
bool combo(const char* label, int* value, const char* const* items, int count,
           const char* tooltip = nullptr);
bool multi_bits(const char* label, std::uint32_t* bits, const char* const* names,
                const std::uint32_t* masks, int count, const char* tooltip = nullptr);
bool color_row(const char* label, config::rgba* color, const char* tooltip = nullptr);
bool keybind_row(const char* label, config::keybind* kb, const char* tooltip = nullptr);
bool action_button(const char* label, const ImVec2& size = ImVec2(0, 0));
bool chip(const char* label, bool selected);

// Card container: titled panel with consistent padding and border.
bool begin_card(const char* id, const ImVec2& size, bool border = true);
void end_card();

}  // namespace shogod::ui::widgets
