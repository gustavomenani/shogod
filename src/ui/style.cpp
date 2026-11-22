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

ImU32 mix_col(ImU32 a, ImU32 b, float t) { return mix(a, b, t); }

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
    wchar_t windows_dir[MAX_PATH]{};
    const UINT windows_dir_size = GetWindowsDirectoryW(windows_dir, MAX_PATH);
    std::filesystem::path title_path = path;
    if (windows_dir_size > 0 && windows_dir_size < MAX_PATH) {
        const auto semibold = std::filesystem::path(windows_dir) / "Fonts" / "seguisb.ttf";
        std::error_code ec;
        if (std::filesystem::is_regular_file(semibold, ec)) title_path = semibold;
    }
    g_title = io.Fonts->AddFontFromFileTTF(title_path.string().c_str(), 29.0f * scale, &title_cfg, ranges);
    if (!g_title) g_title = g_body;

    ImFontConfig small_cfg = cfg;
    g_small = io.Fonts->AddFontFromFileTTF(path.c_str(), 12.0f * scale, &small_cfg, ranges);
    if (!g_small) g_small = g_body;
}

ImFont* font_body() { return g_body; }
ImFont* font_title() { return g_title; }
ImFont* font_small() { return g_small; }

float font_size_body() { return 15.0f * g_scale; }
float font_size_title() { return 29.0f * g_scale; }
float font_size_small() { return 12.0f * g_scale; }

const palette& colors() { return g_pal; }

void apply_style(const config::rgba& accent_rgba, bool dark) {
    const ImU32 accent = to_u32(accent_rgba);
    const ImU32 accent_hi = mix(accent, IM_COL32(255, 255, 255, 255), 0.18f);


    if (dark) {
        // Authentic Neverlose dark navy palette (adapted from OverlayMenu menu_palette.h).
        g_pal = palette{
            .bg_window = IM_COL32(11, 13, 20, 255),
            .bg_sidebar = IM_COL32(7, 17, 31, 255),
            .bg_card = IM_COL32(17, 19, 29, 255),
            .bg_card_hover = IM_COL32(25, 29, 44, 255),
            .bg_input = IM_COL32(9, 12, 20, 255),
            .border = IM_COL32(38, 45, 68, 255),
            .border_soft = IM_COL32(25, 30, 46, 255),
            .text = IM_COL32(242, 244, 248, 255),
            .text_muted = IM_COL32(162, 170, 188, 255),
            .text_faint = IM_COL32(104, 114, 136, 255),
            .accent = accent,
            .accent_soft = mix(IM_COL32(14, 22, 38, 255), accent, 0.25f),
            .success = IM_COL32(69, 212, 131, 255),
            .warning = IM_COL32(245, 181, 68, 255),
            .error = IM_COL32(242, 85, 90, 255),
        };
    } else {
        g_pal = palette{
            .bg_window = IM_COL32(241, 244, 249, 255),
            .bg_sidebar = IM_COL32(232, 237, 245, 255),
            .bg_card = IM_COL32(255, 255, 255, 255),
            .bg_card_hover = IM_COL32(236, 241, 249, 255),
            .bg_input = IM_COL32(243, 246, 251, 255),
            .border = IM_COL32(202, 212, 226, 255),
            .border_soft = IM_COL32(223, 230, 240, 255),
            .text = IM_COL32(26, 31, 41, 255),
            .text_muted = IM_COL32(84, 94, 112, 255),
            .text_faint = IM_COL32(120, 130, 148, 255),
            .accent = accent,
            .accent_soft = mix(accent, IM_COL32(255, 255, 255, 255), 0.84f),
            .success = IM_COL32(28, 168, 92, 255),
            .warning = IM_COL32(196, 128, 20, 255),
            .error = IM_COL32(214, 52, 58, 255),
        };
    }

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.ScrollbarRounding = 4.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(10, 5);
    s.ItemSpacing = ImVec2(10, 6);
    s.CellPadding = ImVec2(7, 2);
    s.ItemInnerSpacing = ImVec2(6, 5);
    s.ScrollbarSize = 5.0f;
    s.GrabMinSize = 10.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImColor(g_pal.text);
    c[ImGuiCol_TextDisabled] = ImColor(g_pal.text_faint);
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImColor(g_pal.bg_card);
    c[ImGuiCol_PopupBg] = dark ? ImColor(IM_COL32(19, 22, 31, 255)) : ImColor(g_pal.bg_card);
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
    c[ImGuiCol_ScrollbarGrab] = ImColor(g_pal.border_soft);
    c[ImGuiCol_ScrollbarGrabHovered] = ImColor(g_pal.border);
    c[ImGuiCol_ScrollbarGrabActive] = ImColor(g_pal.text_faint);
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
    c[ImGuiCol_NavHighlight] = ImColor(accent);
    c[ImGuiCol_DragDropTarget] = ImColor(accent);
    c[ImGuiCol_TableHeaderBg] = ImColor(g_pal.bg_sidebar);
    c[ImGuiCol_TableBorderStrong] = ImColor(g_pal.border);
    c[ImGuiCol_TableBorderLight] = ImColor(g_pal.border_soft);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
}

ImU32 fade_col(ImU32 col, float extra_alpha) {
    const float a_mul = std::clamp(ImGui::GetStyle().Alpha * extra_alpha, 0.0f, 1.0f);
    const std::uint32_t orig_a = (col >> IM_COL32_A_SHIFT) & 0xFFu;
    const std::uint32_t new_a = static_cast<std::uint32_t>(static_cast<float>(orig_a) * a_mul);
    return (col & ~(0xFFu << IM_COL32_A_SHIFT)) | (new_a << IM_COL32_A_SHIFT);
}

void text_soft(const char* text, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color ? color : colors().text_muted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void separator_y(float pad) {
    ImGui::Dummy(ImVec2(0, pad * 0.35f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(40.0f, ImGui::GetContentRegionAvail().x - 14.0f);
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y), fade_col(colors().border_soft));
    ImGui::Dummy(ImVec2(0, pad * 0.35f));
}

}  // namespace shogod::ui
