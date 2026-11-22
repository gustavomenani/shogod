#include "ui/menu.hpp"

#include "brand.hpp"
#include "ui/pages/pages.hpp"
#include "ui/style.hpp"
#include "ui/ui_icons.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace shogod::ui {
namespace {

constexpr float k_sidebar_w = 192.0f;
constexpr float k_header_h = 60.0f;
constexpr ImVec2 k_menu_size(1000.0f, 650.0f);

ImTextureID g_brand_tex = 0;
int g_brand_w = 0;
int g_brand_h = 0;
ImTextureID g_avatar_tex = 0;
int g_avatar_size = 0;

float approach(float value, float target, float rate, float dt) {
    const float t = 1.0f - std::exp(-rate * dt);
    return value + (target - value) * t;
}

const char* tab_name(tab t) {
    switch (t) {
        case tab::rage: return "RageBot";
        case tab::antiaim: return "Anti-Aim";
        case tab::visuals: return "Visuals";
        case tab::misc: return "Misc";
        case tab::configs: return "Config";
        case tab::settings: return "Settings";
        default: return "";
    }
}

// Categorised vertical layout mirroring Neverlose's sidebar hierarchy.
float tab_y(tab t) {
    switch (t) {
        case tab::rage: return 106.0f;
        case tab::antiaim: return 146.0f;
        case tab::visuals: return 212.0f;
        case tab::misc: return 252.0f;
        case tab::configs: return 318.0f;
        case tab::settings: return 358.0f;
        default: return 106.0f;
    }
}

void icon(tab t, ImVec2 c, ImU32 col, float size = 15.5f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui_icon_id id = ui_icon_id::crosshair;
    switch (t) {
        case tab::rage: id = ui_icon_id::crosshair; break;
        case tab::antiaim: id = ui_icon_id::shield; break;
        case tab::visuals: id = ui_icon_id::eye; break;
        case tab::misc: id = ui_icon_id::zap; break;
        case tab::configs: id = ui_icon_id::folder; break;
        case tab::settings: id = ui_icon_id::settings; break;
        default: return;
    }
    draw_ui_icon(*dl, id, c, size, col, 1.4f);
    if (t == tab::rage) {
        dl->AddCircleFilled(c, 1.35f, col, 12);
    }
}

void sidebar(menu_state& state, const status& live, float height, bool animated) {
    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    dl->AddRectFilled(origin, ImVec2(origin.x + k_sidebar_w, origin.y + height),
                      fade_col(pal.bg_sidebar), 10.0f, ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(origin.x + k_sidebar_w, origin.y),
                ImVec2(origin.x + k_sidebar_w, origin.y + height),
                fade_col(mix_col(pal.border_soft, pal.accent, 0.35f)), 1.0f);

    if (g_brand_tex != 0 && g_brand_w > 0 && g_brand_h > 0) {
        const float target_w = 136.0f;
        const float target_h = target_w * (static_cast<float>(g_brand_h) / static_cast<float>(g_brand_w));
        const ImVec2 logo_a(std::floor(origin.x + (k_sidebar_w - target_w) * 0.5f), origin.y + 10.0f);
        const ImVec2 logo_b(logo_a.x + target_w, logo_a.y + target_h);

        // If Light Theme is active (bright sidebar), keep a sleek dark backdrop plate so white letters pop.
        const bool light_sidebar =
            ((pal.bg_sidebar & 0xFF) + ((pal.bg_sidebar >> 8) & 0xFF) + ((pal.bg_sidebar >> 16) & 0xFF)) > 384;
        if (light_sidebar) {
            dl->AddRectFilled(ImVec2(logo_a.x - 9.0f, logo_a.y - 3.0f),
                              ImVec2(logo_b.x + 9.0f, logo_b.y + 3.0f),
                              fade_col(IM_COL32(16, 20, 29, 255)), 7.0f);
        }

        const ImU32 tint = fade_col(IM_COL32(255, 255, 255, 255));
        dl->AddImage(g_brand_tex, logo_a, logo_b, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);

        ImGui::PushFont(font_small(), 11.0f);
        const ImVec2 sub_sz = ImGui::CalcTextSize("Counter-Strike 2");
        dl->AddText(ImVec2(std::floor(origin.x + (k_sidebar_w - sub_sz.x) * 0.5f),
                           std::floor(logo_b.y + 6.0f)),
                    fade_col(pal.text_faint), "Counter-Strike 2");
        ImGui::PopFont();
        dl->AddLine(ImVec2(origin.x + 16.0f, origin.y + 76.0f),
                    ImVec2(origin.x + k_sidebar_w - 16.0f, origin.y + 76.0f),
                    fade_col(pal.border_soft), 1.0f);
    } else {
        const ImVec2 mark(origin.x + 18.0f, origin.y + 16.0f);
        dl->AddRectFilled(mark, ImVec2(mark.x + 32.0f, mark.y + 32.0f), fade_col(pal.accent_soft),
                          8.0f);
        dl->AddRect(mark, ImVec2(mark.x + 32.0f, mark.y + 32.0f), fade_col(pal.accent, 0.35f), 8.0f);
        draw_ui_icon(*dl, ui_icon_id::brand_mark, ImVec2(mark.x + 16.0f, mark.y + 16.0f), 22.0f,
                     fade_col(pal.accent), 1.55f);

        ImGui::SetCursorPos(ImVec2(60.0f, 13.0f));
        ImGui::PushFont(font_title(), 18.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(pal.text));
        ImGui::TextUnformatted("shogod");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(60.0f, 35.0f));
        ImGui::PushFont(font_small(), 11.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, fade_col(pal.text_faint));
        ImGui::TextUnformatted("Counter-Strike 2");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    auto category_label = [&](float y, const char* text) {
        ImGui::PushFont(font_small(), 10.5f);
        dl->AddText(ImVec2(origin.x + 18.0f, origin.y + y), fade_col(pal.text_faint), text);
        ImGui::PopFont();
    };
    category_label(86.0f, "AIMBOT");
    category_label(192.0f, "COMMON");
    category_label(298.0f, "SYSTEM");

    const float target = tab_y(state.active);
    if (!animated || state.nav_y < 0.0f) state.nav_y = target;
    state.nav_y = approach(state.nav_y, target, 24.0f, ImGui::GetIO().DeltaTime);
    const float selected_y = std::round(origin.y + state.nav_y);
    const ImVec2 pill_a(origin.x + 12.0f, selected_y);
    const ImVec2 pill_b(origin.x + k_sidebar_w - 12.0f, selected_y + 36.0f);
    dl->AddRectFilled(pill_a, pill_b, fade_col(pal.bg_card_hover), 7.0f);
    dl->AddRect(pill_a, pill_b, fade_col(pal.border), 7.0f, 0, 1.0f);
    dl->AddRectFilled(ImVec2(pill_a.x + 2.0f, selected_y + 8.0f),
                      ImVec2(pill_a.x + 4.5f, selected_y + 28.0f), fade_col(pal.accent), 2.0f);

    for (int i = 0; i < k_tab_count; ++i) {
        const tab t = static_cast<tab>(i);
        const bool active = state.active == t;
        const float y = tab_y(t);

        ImGui::SetCursorPos(ImVec2(12.0f, y));
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##nav", ImVec2(k_sidebar_w - 24.0f, 36.0f),
                                                  ImGuiButtonFlags_EnableNav);
        const bool hovered = ImGui::IsItemHovered();
        if (pressed) state.active = t;
        ImGui::PopID();

        const ImVec2 item_pos = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        if (!active && hovered) {
            dl->AddRectFilled(item_pos, item_max, fade_col(pal.bg_card), 7.0f);
        }
        if (ImGui::GetIO().NavVisible && ImGui::IsItemFocused()) {
            dl->AddRect(item_pos, item_max, fade_col(pal.accent), 7.0f);
        }

        const ImU32 col = fade_col(active || hovered ? pal.text : pal.text_muted);
        icon(t, ImVec2(item_pos.x + 19.0f, item_pos.y + 18.0f),
             active ? fade_col(pal.accent) : col);
        dl->AddText(ImVec2(item_pos.x + 35.0f, item_pos.y + (36.0f - ImGui::GetFontSize()) * 0.5f),
                    col, tab_name(t));
    }

    const float footer_y = height - 68.0f;
    dl->AddLine(ImVec2(origin.x + 14.0f, origin.y + footer_y),
                ImVec2(origin.x + k_sidebar_w - 14.0f, origin.y + footer_y),
                fade_col(pal.border_soft));

    const float av_r = 20.0f;
    const ImVec2 avatar(origin.x + 36.0f, origin.y + height - 34.0f);
    if (g_avatar_tex != 0 && g_avatar_size > 0) {
        dl->AddImage(g_avatar_tex, ImVec2(avatar.x - av_r, avatar.y - av_r),
                     ImVec2(avatar.x + av_r, avatar.y + av_r), ImVec2(0.0f, 0.0f),
                     ImVec2(1.0f, 1.0f), fade_col(IM_COL32(255, 255, 255, 255)));
    } else {
        dl->AddCircleFilled(avatar, av_r, fade_col(pal.accent_soft), 32);
        draw_ui_icon(*dl, ui_icon_id::user, avatar, 20.0f, fade_col(pal.accent), 1.5f);
    }
    dl->AddCircle(avatar, av_r, fade_col(pal.accent, 0.85f), 36, 1.5f);
    dl->AddCircleFilled(ImVec2(avatar.x + 14.5f, avatar.y + 14.5f), 5.0f,
                        fade_col(pal.bg_sidebar), 16);
    dl->AddCircleFilled(ImVec2(avatar.x + 14.5f, avatar.y + 14.5f), 3.6f,
                        fade_col(live.anchors_ok > 0 ? pal.success : pal.accent), 16);

    ImGui::PushFont(font_body(), 15.0f);
    dl->AddText(ImVec2(origin.x + 64.0f, origin.y + height - 47.0f), fade_col(pal.text), "shoot");
    ImGui::PopFont();

    ImGui::PushFont(font_small(), font_size_small());
    char ver[24];
    std::snprintf(ver, sizeof ver, "v%.*s", static_cast<int>(brand::k_version.size()),
                  brand::k_version.data());
    dl->AddText(ImVec2(origin.x + 64.0f, origin.y + height - 29.0f), fade_col(pal.text_faint), ver);
    ImGui::PopFont();
}

void header(menu_state& state, const status& live, float width) {
    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::SetCursorPos(ImVec2(24.0f, 18.0f));
    ImGui::PushFont(font_title(), 22.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, fade_col(pal.text));
    ImGui::TextUnformatted(tab_name(state.active));
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Top-right status badges: interactive Config pill + live FPS chip.
    ImGui::PushFont(font_small(), font_size_small());
    const std::string fps = std::to_string(static_cast<int>(live.fps)) + " FPS";
    const float fps_w = ImGui::CalcTextSize(fps.c_str()).x + 28.0f;
    const float cfg_label_w = ImGui::CalcTextSize(
        (live.config_name.empty() ? "default" : live.config_name.c_str())).x;
    const float cfg_w = std::max(108.0f, cfg_label_w + 40.0f);
    const float pill_h = 26.0f;
    const float pill_y = 18.0f;

    const float fps_x = width - 24.0f - fps_w;
    const ImVec2 fps_a(origin.x + fps_x, origin.y + pill_y);
    const ImVec2 fps_b(fps_a.x + fps_w, fps_a.y + pill_h);
    dl->AddRectFilled(fps_a, fps_b, fade_col(pal.bg_card), 6.0f);
    dl->AddRect(fps_a, fps_b, fade_col(pal.border_soft), 6.0f);
    dl->AddCircleFilled(ImVec2(fps_a.x + 11.0f, fps_a.y + pill_h * 0.5f), 3.0f,
                        fade_col(pal.success), 12);
    dl->AddText(ImVec2(fps_a.x + 20.0f, fps_a.y + (pill_h - ImGui::GetFontSize()) * 0.5f),
                fade_col(pal.text_muted), fps.c_str());

    const float cfg_x = fps_x - 8.0f - cfg_w;
    ImGui::SetCursorPos(ImVec2(cfg_x, pill_y));
    const ImVec2 cfg_a = ImGui::GetCursorScreenPos();
    const bool cfg_clicked = ImGui::InvisibleButton("##cfg_pill", ImVec2(cfg_w, pill_h));
    const bool cfg_hovered = ImGui::IsItemHovered();
    if (cfg_clicked) state.active = tab::configs;
    const ImVec2 cfg_b(cfg_a.x + cfg_w, cfg_a.y + pill_h);
    dl->AddRectFilled(cfg_a, cfg_b, fade_col(cfg_hovered ? pal.bg_card_hover : pal.bg_card), 6.0f);
    dl->AddRect(cfg_a, cfg_b, fade_col(cfg_hovered ? pal.accent : pal.border_soft), 6.0f);
    icon(tab::configs, ImVec2(cfg_a.x + 13.0f, cfg_a.y + pill_h * 0.5f),
         fade_col(cfg_hovered ? pal.accent : pal.text_muted), 13.5f);
    dl->AddText(ImVec2(cfg_a.x + 26.0f, cfg_a.y + (pill_h - ImGui::GetFontSize()) * 0.5f),
                fade_col(pal.text), live.config_name.empty() ? "default" : live.config_name.c_str());
    ImGui::PopFont();

    dl->AddLine(ImVec2(origin.x, origin.y + k_header_h),
                ImVec2(origin.x + width, origin.y + k_header_h), fade_col(pal.border_soft));
}

}  // namespace

void set_brand_logo(ImTextureID tex, int width, int height) noexcept {
    g_brand_tex = tex;
    g_brand_w = width;
    g_brand_h = height;
}

void set_user_avatar(ImTextureID tex, int size) noexcept {
    g_avatar_tex = tex;
    g_avatar_size = size;
}

void draw_menu(config::store& cfg, menu_state& state, const status& live) {
    const float dt = ImGui::GetIO().DeltaTime;
    const config::config_v1& c = cfg.get();
    const float open_target = state.open ? 1.0f : 0.0f;
    state.open_anim =
        c.ui.animations ? approach(state.open_anim, open_target, 28.0f, dt) : open_target;
    if (!state.open && state.open_anim < 0.01f) {
        state.open_anim = 0.0f;
        return;
    }

    apply_style(c.ui.accent, c.ui.dark_theme);

    for (int i = 0; i < k_tab_count; ++i) {
        const float target = static_cast<int>(state.active) == i ? 1.0f : 0.0f;
        state.tab_anim[static_cast<std::size_t>(i)] =
            c.ui.animations ? approach(state.tab_anim[static_cast<std::size_t>(i)], target, 24.0f, dt)
                            : target;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float alpha = state.open_anim;
    const ImVec2 size(k_menu_size.x, k_menu_size.y);
    const ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f,
                     vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##shogod_menu", nullptr, flags);

    const palette& pal = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win = ImGui::GetWindowPos();
    dl->AddRectFilled(win, ImVec2(win.x + size.x, win.y + size.y),
                      fade_col(pal.bg_window, c.ui.opacity), 10.0f);

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    sidebar(state, live, size.y, c.ui.animations);
    ImGui::PopStyleColor();

    // Draw the blue/accent-tinted outer frame AFTER sidebar() so the left, top-left,
    // and bottom-left borders are never painted over by sidebar background fills.
    const ImU32 frame_border = fade_col(mix_col(pal.border, pal.accent, 0.55f));
    dl->AddRect(win, ImVec2(win.x + size.x, win.y + size.y), frame_border, 10.0f, 0, 1.25f);

    ImGui::SetCursorPos(ImVec2(k_sidebar_w, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##main", ImVec2(size.x - k_sidebar_w, size.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    header(state, live, size.x - k_sidebar_w);

    const float transition = state.tab_anim[static_cast<std::size_t>(state.active)];
    ImGui::SetCursorPos(ImVec2(24.0f, k_header_h + 18.0f));
    ImGui::BeginChild("##content", ImVec2(size.x - k_sidebar_w - 48.0f, size.y - k_header_h - 36.0f),
                      ImGuiChildFlags_None);

    // Keep alpha locked to menu fade when closing so the UI fades out as one piece.
    const float tab_alpha = state.open ? alpha * (0.4f + 0.6f * transition) : alpha;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha);

    switch (state.active) {
        case tab::rage: pages::rage(cfg, live); break;
        case tab::antiaim: pages::antiaim(cfg); break;
        case tab::visuals: pages::visuals(cfg, live); break;
        case tab::misc: pages::misc(cfg); break;
        case tab::configs: pages::configs(cfg, state); break;
        case tab::settings: pages::settings(cfg, live); break;
        default: break;
    }
    ImGui::PopStyleVar();

    // Toast, bottom-right of the content area.
    if (state.toast_time > 0.0f) {
        state.toast_time -= dt;
        const float a = std::clamp(state.toast_time / 0.4f, 0.0f, 1.0f);
        const ImVec2 text = ImGui::CalcTextSize(state.toast.c_str());
        const ImVec2 avail = ImGui::GetWindowSize();
        const ImVec2 p(avail.x - text.x - 46.0f, avail.y - 40.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(win.x + k_sidebar_w + p.x, win.y + k_header_h + p.y),
            ImVec2(win.x + k_sidebar_w + p.x + text.x + 24.0f, win.y + k_header_h + p.y + 28.0f),
            fade_col(pal.accent_soft, a), 6.0f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(win.x + k_sidebar_w + p.x + 12.0f, win.y + k_header_h + p.y + 6.0f),
            fade_col(pal.text, a), state.toast.c_str());
    }

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace shogod::ui
