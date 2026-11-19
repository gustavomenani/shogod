#pragma once

// Design system: one palette, one spacing scale, no default ImGui look.
// Fonts are loaded once from the versioned TTF next to the DLL.

#include "config/config.hpp"

#include "imgui.h"

namespace shogod::ui {

struct palette {
    ImU32 bg_window;
    ImU32 bg_sidebar;
    ImU32 bg_card;
    ImU32 bg_card_hover;
    ImU32 bg_input;
    ImU32 border;
    ImU32 border_soft;
    ImU32 text;
    ImU32 text_muted;
    ImU32 text_faint;
    ImU32 accent;
    ImU32 accent_soft;
    ImU32 success;
    ImU32 warning;
    ImU32 error;
};

void load_fonts(float scale);
void apply_style(const config::rgba& accent, bool dark);

[[nodiscard]] const palette& colors();
[[nodiscard]] ImFont* font_body();
[[nodiscard]] ImFont* font_title();
[[nodiscard]] ImFont* font_small();
[[nodiscard]] float font_size_body();
[[nodiscard]] float font_size_title();
[[nodiscard]] float font_size_small();

// Conversions used by widgets that take the config colour type.
ImU32 to_u32(const config::rgba& c);
ImVec4 to_vec4(const config::rgba& c);
config::rgba from_vec4(const ImVec4& v);

// Drawing helpers shared by menu and widgets.
void text_soft(const char* text, ImU32 color = 0);
void separator_y(float pad = 8.0f);

}  // namespace shogod::ui
