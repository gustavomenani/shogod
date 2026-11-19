#include "ui/style.hpp"

#include "engine/log.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace shogod::ui {
namespace {

palette g_pal;
ImFont* g_body = nullptr;
ImFont* g_title = nullptr;
ImFont* g_small = nullptr;
float g_scale = 1.0f;

ImU32 mix(ImU32 a, ImU32 b, float t) {
    auto lerp_ch = [t](ImU32 x, ImU32 y, int shift) {
        const float a0 = static_cast<float>((x >> shift) & 0xFF);
        const float b0 = static_cast<float>((y >> shift) & 0xFF);
        return a0 + (b0 - a0) * t;
    };
    const float r = lerp_ch(a, b, IM_COL32_R_SHIFT);
    const float g = lerp_ch(a, b, IM_COL32_G_SHIFT);
    const float bl = lerp_ch(a, b, IM_COL32_B_SHIFT);
    const float al = lerp_ch(a, b, IM_COL32_A_SHIFT);
    return IM_COL32(static_cast<int>(r), static_cast<int>(g), static_cast<int>(bl),
                    static_cast<int>(al));
}

std::string font_path() {
    if (const char* env = std::getenv("SHOGOD_FONT_DIR")) {
        return (std::filesystem::path(env) / "Inter-Medium.ttf").string();
    }

    // Our own module, not the host executable: the DLL ships with fonts/.
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&font_path), &self);

    char module[MAX_PATH]{};
    GetModuleFileNameA(self, module, MAX_PATH);
    return (std::filesystem::path(module).parent_path() / "fonts" / "Inter-Medium.ttf").string();
}

}  // namespace

ImU32 to_u32(const config::rgba& c) {
    return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f),
                    static_cast<int>(c.b * 255.0f), static_cast<int>(c.a * 255.0f));
}

ImVec4 to_vec4(const config::rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

config::rgba from_vec4(const ImVec4& v) { return config::rgba{v.x, v.y, v.z, v.w}; }

void load_fonts(float scale) {
    g_scale = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    const std::string path = font_path();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesDefault();

    g_body = io.Fonts->AddFontFromFileTTF(path.c_str(), 15.0f * scale, &cfg, ranges);
    if (!g_body) {
        g_body = io.Fonts->AddFontDefault(&cfg);
        log::warn("ui: body font missing at {}, using default", path);
    }

    ImFontConfig title_cfg = cfg;
    g_title = io.Fonts->AddFontFromFileTTF(path.c_str(), 21.0f * scale, &title_cfg, ranges);
    if (!g_title) g_title = g_body;

    ImFontConfig small_cfg = cfg;
    g_small = io.Fonts->AddFontFromFileTTF(path.c_str(), 12.0f * scale, &small_cfg, ranges);
    if (!g_small) g_small = g_body;
}

ImFont* font_body() { return g_body; }
ImFont* font_title() { return g_title; }
ImFont* font_small() { return g_small; }

float font_size_body() { return 15.0f * g_scale; }
float font_size_title() { return 21.0f * g_scale; }
float font_size_small() { return 12.0f * g_scale; }

const palette& colors() { return g_pal; }

void apply_style(const config::rgba& accent_rgba, bool dark) {
    const ImU32 accent = to_u32(accent_rgba);
    const ImU32 accent_hi = mix(accent, IM_COL32(255, 255, 255, 255), 0.18f);
    const ImU32 accent_dim = mix(accent, IM_COL32(0, 0, 0, 255), 0.25f);

    if (dark) {
        g_pal = palette{
            .bg_window = IM_COL32(11, 12, 16, 242),
            .bg_sidebar = IM_COL32(13, 15, 20, 255),
            .bg_card = IM_COL32(19, 22, 30, 255),
            .bg_card_hover = IM_COL32(24, 28, 38, 255),
            .bg_input = IM_COL32(15, 17, 24, 255),
            .border = IM_COL32(36, 41, 56, 255),
            .border_soft = IM_COL32(28, 32, 43, 255),
            .text = IM_COL32(232, 235, 242, 255),
            .text_muted = IM_COL32(138, 144, 162, 255),
            .text_faint = IM_COL32(88, 94, 112, 255),
            .accent = accent,
            .accent_soft = mix(accent, IM_COL32(0, 0, 0, 255), 0.78f),
            .success = IM_COL32(69, 212, 131, 255),
            .warning = IM_COL32(245, 181, 68, 255),
            .error = IM_COL32(242, 85, 90, 255),
        };
    } else {
        g_pal = palette{
            .bg_window = IM_COL32(244, 245, 249, 246),
            .bg_sidebar = IM_COL32(235, 237, 244, 255),
            .bg_card = IM_COL32(255, 255, 255, 255),
            .bg_card_hover = IM_COL32(247, 248, 252, 255),
            .bg_input = IM_COL32(238, 240, 246, 255),
            .border = IM_COL32(210, 214, 224, 255),
            .border_soft = IM_COL32(224, 227, 235, 255),
            .text = IM_COL32(28, 31, 40, 255),
            .text_muted = IM_COL32(96, 102, 118, 255),
            .text_faint = IM_COL32(140, 146, 160, 255),
            .accent = accent,
            .accent_soft = mix(accent, IM_COL32(255, 255, 255, 255), 0.82f),
            .success = IM_COL32(28, 168, 92, 255),
            .warning = IM_COL32(196, 128, 20, 255),
            .error = IM_COL32(214, 52, 58, 255),
        };
    }

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 9.0f;
    s.ChildRounding = 7.0f;
    s.FrameRounding = 5.0f;
    s.PopupRounding = 7.0f;
    s.GrabRounding = 5.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(6, 5);
    s.ScrollbarSize = 9.0f;
    s.GrabMinSize = 10.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImColor(g_pal.text);
    c[ImGuiCol_TextDisabled] = ImColor(g_pal.text_faint);
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImColor(g_pal.bg_card);
    c[ImGuiCol_PopupBg] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_Border] = ImColor(g_pal.border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImColor(g_pal.bg_input);
    c[ImGuiCol_FrameBgHovered] = ImColor(g_pal.bg_card_hover);
    c[ImGuiCol_FrameBgActive] = ImColor(g_pal.bg_card_hover);
    c[ImGuiCol_TitleBg] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_TitleBgActive] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_TitleBgCollapsed] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_MenuBarBg] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImColor(g_pal.border);
    c[ImGuiCol_ScrollbarGrabHovered] = ImColor(g_pal.text_faint);
    c[ImGuiCol_ScrollbarGrabActive] = ImColor(g_pal.text_muted);
    c[ImGuiCol_CheckMark] = ImColor(accent);
    c[ImGuiCol_SliderGrab] = ImColor(accent);
    c[ImGuiCol_SliderGrabActive] = ImColor(accent_hi);
    c[ImGuiCol_Button] = ImColor(g_pal.bg_card);
    c[ImGuiCol_ButtonHovered] = ImColor(g_pal.bg_card_hover);
    c[ImGuiCol_ButtonActive] = ImColor(g_pal.bg_input);
    c[ImGuiCol_Header] = ImColor(g_pal.accent_soft);
    c[ImGuiCol_HeaderHovered] = ImColor(g_pal.bg_card_hover);
    c[ImGuiCol_HeaderActive] = ImColor(g_pal.accent_soft);
    c[ImGuiCol_Separator] = ImColor(g_pal.border_soft);
    c[ImGuiCol_SeparatorHovered] = ImColor(g_pal.accent);
    c[ImGuiCol_SeparatorActive] = ImColor(accent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_TabHovered] = ImColor(g_pal.bg_card_hover);
    c[ImGuiCol_TabSelected] = ImColor(g_pal.bg_card);
    c[ImGuiCol_TabDimmed] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_TabDimmedSelected] = ImColor(g_pal.bg_card);
    c[ImGuiCol_PlotLines] = ImColor(accent);
    c[ImGuiCol_PlotHistogram] = ImColor(accent);
    c[ImGuiCol_TextSelectedBg] = ImColor(g_pal.accent_soft);
    c[ImGuiCol_NavHighlight] = ImColor(accent_dim);
    c[ImGuiCol_DragDropTarget] = ImColor(accent);
    c[ImGuiCol_TableHeaderBg] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_TableBorderStrong] = ImColor(g_pal.border);
    c[ImGuiCol_TableBorderLight] = ImColor(g_pal.border_soft);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
}

void text_soft(const char* text, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color ? color : colors().text_muted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void separator_y(float pad) {
    ImGui::Dummy(ImVec2(0, pad * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Separator, colors().border_soft);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, pad * 0.5f));
}

}  // namespace shogod::ui
